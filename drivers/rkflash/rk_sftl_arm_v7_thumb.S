/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-09-30
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
	ldr	r2, .L114
	ldrh	r3, [r2, #40]
	mov	r5, r2
	cmp	r3, r0
	bls	.L101
	movs	r4, #6
	ldr	ip, [r2, #2316]
	muls	r4, r0, r4
	movw	r3, #65535
	add	r1, ip, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [ip, r4]	@ movhi
	ldr	r3, [r2, #2320]
	cbnz	r3, .L102
	str	r1, [r2, #2320]
.L101:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L102:
	ldrh	r6, [r1, #4]
	lsl	r10, r0, #1
	ldr	r8, [r2, #2324]
	ldrh	r2, [r8, r0, lsl #1]
	cmp	r6, #0
	beq	.L112
	muls	r6, r2, r6
.L103:
	ldr	lr, [r5, #2316]
	ldrh	r7, [r5, #40]
	sub	r2, r3, lr
	asr	r9, r2, #1
	ldr	r2, .L114+4
	str	r7, [sp]
	mul	r2, r2, r9
	ldr	r9, [r5, #2328]
	add	r5, r9, r10
	uxth	r2, r2
	str	r5, [sp, #4]
	movs	r5, #0
.L110:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L101
	cmp	r0, r2
	beq	.L101
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L113
	mul	r7, r7, fp
.L105:
	cmp	r6, r7
	bne	.L106
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L108
.L107:
	strh	r2, [ip, r4]	@ movhi
	ldr	r4, .L114
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
	b	.L101
.L112:
	mov	r6, #-1
	b	.L103
.L113:
	mov	r7, #-1
	b	.L105
.L106:
	bcc	.L107
.L108:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L109
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L114
	str	r1, [r3, #2332]
	b	.L101
.L109:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, lr
	b	.L110
.L115:
	.align	2
.L114:
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
	ldr	r2, .L118
	ldrh	r3, [r2, #2336]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2336]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L116
	movs	r2, #214
	ldr	r1, .L118+4
	ldr	r0, .L118+8
	pop	{r3, lr}
	b	sftl_printk
.L116:
	pop	{r3, pc}
.L119:
	.align	2
.L118:
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
	beq	.L121
	ldr	r2, .L127
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2316]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2340]
	cbnz	r3, .L122
	str	r6, [r2, #2340]
.L121:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L122:
	ldr	lr, [r2, #2316]
	ldr	r8, [r2, #2328]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L127+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L125:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L123
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L124
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L121
.L124:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L125
.L123:
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
	b	.L121
.L128:
	.align	2
.L127:
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
	ldr	r2, .L131
	ldrh	r3, [r2, #2344]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2344]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L129
	movs	r2, #207
	ldr	r1, .L131+4
	ldr	r0, .L131+8
	pop	{r3, lr}
	b	sftl_printk
.L129:
	pop	{r3, pc}
.L132:
	.align	2
.L131:
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
	ldr	r4, .L139
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L134
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L134
	mov	r2, #372
	ldr	r1, .L139+4
	ldr	r0, .L139+8
	bl	sftl_printk
.L134:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L135
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
.L137:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L135:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L138
	cmp	r1, r3
	beq	.L137
	movs	r2, #6
	ldr	r0, [r4, #2316]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L137
.L138:
	ldr	r0, [r4, #2316]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2316]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L137
.L140:
	.align	2
.L139:
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
	cbz	r3, .L147
	ldr	r2, .L148
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2316]
.L143:
	cbnz	r1, .L144
.L146:
	ldr	r4, .L148+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L144:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L146
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L143
.L147:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L149:
	.align	2
.L148:
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
	ldr	r2, .L157
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2320]
	cbz	r3, .L156
	ldr	r1, [r2, #2316]
	movs	r4, #6
	movw	r2, #65535
.L153:
	cbz	r0, .L154
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L155
.L156:
	movw	r0, #65535
	pop	{r4, pc}
.L155:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L153
.L154:
	ldr	r0, .L157+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L158:
	.align	2
.L157:
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
	ldr	r4, .L170
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L161
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L161
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	beq	.L161
	movs	r7, #6
	ldr	r9, [r4, #2316]
	muls	r7, r0, r7
	ldr	r3, [r4, #2320]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L161
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L168
	muls	r6, r3, r6
.L163:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L164
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L164
	movw	r2, #463
	ldr	r1, .L170+4
	ldr	r0, .L170+8
	bl	sftl_printk
.L164:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L165
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L161
.L165:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L170+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #2324]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2316]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L169
	muls	r3, r0, r3
.L166:
	cmp	r6, r3
	bcs	.L161
	mov	r1, r5
	ldr	r0, .L170+16
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L167
	mov	r2, #474
	ldr	r1, .L170+4
	ldr	r0, .L170+8
	bl	sftl_printk
.L167:
	ldrh	r3, [r4, #2336]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2336]	@ movhi
	bl	INSERT_DATA_LIST
.L161:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L168:
	mov	r6, #-1
	b	.L163
.L169:
	mov	r3, #-1
	b	.L166
.L171:
	.align	2
.L170:
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
	ldr	r5, .L182
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #138]
	ldr	r3, [r5, #2492]
.L173:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L175
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L176:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L178
	cmp	r4, r2
	bcc	.L174
	ldrh	r7, [r5, #2496]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L179:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L181
	cmp	r4, r2
	bcc	.L174
	movw	r2, #787
	ldr	r1, .L182+4
	ldr	r0, .L182+8
	bl	sftl_printk
	b	.L174
.L175:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L173
.L174:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L178:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L177
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L177:
	adds	r1, r1, #1
	b	.L176
.L181:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L180
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L180:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L179
.L183:
	.align	2
.L182:
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
	ldr	r1, .L190
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2498]
	cmp	r2, #4
	bhi	.L185
	cbnz	r0, .L185
	adds	r2, r2, #1
	strh	r2, [r1, #2498]	@ movhi
	pop	{r4, r5, pc}
.L185:
	movs	r2, #0
	ldrh	r1, [r3, #40]
	strh	r2, [r3, #2498]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2500]
	ldr	r2, [r3, #2324]
	add	r1, r2, r1, lsl #1
.L186:
	cmp	r2, r1
	bne	.L188
	pop	{r4, r5, pc}
.L188:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2500]
	addne	r0, r0, r4
	strne	r0, [r3, #2500]
	b	.L186
.L191:
	.align	2
.L190:
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
	cbz	r1, .L193
	b	ftl_set_blk_mode.part.6
.L193:
	ldr	r2, .L194
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #24]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L195:
	.align	2
.L194:
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
	ldr	r3, .L197
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #24]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L198:
	.align	2
.L197:
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
	ldr	r3, .L206
	adds	r4, r4, #14
	ldrh	r5, [r3, #32]
.L200:
	cmp	r2, r5
	bcc	.L202
	ldrh	r3, [r3, #102]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L203:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L205
	pop	{r4, r5, r6, r7, pc}
.L202:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L200
.L205:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L203
.L207:
	.align	2
.L206:
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
	cbz	r3, .L208
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L208
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L214
	ldrh	r4, [r2, #32]
	mov	r2, r3
.L211:
	cmp	r1, r2
	beq	.L213
.L208:
	pop	{r4, r5, pc}
.L213:
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
	b	.L211
.L215:
	.align	2
.L214:
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
	ldr	r6, .L221
	ldrh	r2, [r0]
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L217
	movw	r2, #2110
	ldr	r1, .L221+4
	ldr	r0, .L221+8
	bl	sftl_printk
.L217:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L221+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L218:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L220
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	movs	r0, #0
	ldrh	r2, [r6, #102]
	strb	r0, [r4, #9]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L220:
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L219
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L219:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L218
.L222:
	.align	2
.L221:
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
	ldr	r6, .L229
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #32]
	add	r10, r6, #60
	ldrh	r9, [r6, #102]
.L224:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L226
	cbz	r4, .L228
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L227:
	ldr	r1, [r6, #2316]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L226:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L225
	add	r4, r4, r9
	uxth	r4, r4
.L225:
	adds	r5, r5, #1
	b	.L224
.L228:
	mov	r0, r4
	b	.L227
.L230:
	.align	2
.L229:
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
	ldr	r2, .L234
	ldr	r0, [r2, #2340]
	cbz	r0, .L232
	ldr	r3, [r2, #2316]
	subs	r0, r0, r3
	ldr	r3, .L234+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2328]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L232:
	bx	lr
.L235:
	.align	2
.L234:
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
	ldr	r1, .L244
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #2340]
	cbz	r3, .L242
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
	ldr	r2, .L244+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L239:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L241
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L243
.L241:
	ldr	r2, [r1, #2328]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L243:
	mov	r3, r5
	b	.L239
.L242:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L245:
	.align	2
.L244:
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
	ldr	r5, .L258
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L258+4
	bl	strcpy
	ldr	r3, [r5, #116]
	movs	r1, #64
	ldr	r2, .L258+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L258+12
	mov	r0, r4
	adds	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2504]
	movs	r1, #64
	ldr	r2, .L258+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2500]
	ldr	r2, .L258+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2508]
	ldr	r2, .L258+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2512]
	ldr	r2, .L258+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2516]
	ldr	r2, .L258+32
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2520]
	ldr	r2, .L258+36
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2524]
	ldr	r2, .L258+40
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2528]
	ldr	r2, .L258+44
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2532]
	add	r4, r4, r0
	ldr	r2, .L258+48
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2536]
	add	r4, r4, r0
	ldr	r2, .L258+52
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2540]
	ldr	r2, .L258+56
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2544]
	ldr	r2, .L258+60
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #158]
	ldr	r2, .L258+64
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2344]
	ldr	r2, .L258+68
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L258+72
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L258+76
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2556]
	ldr	r2, .L258+80
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L258+84
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L258+88
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L258+92
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2298]
	ldr	r2, .L258+96
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2296]
	ldr	r2, .L258+100
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #140]
	ldr	r2, .L258+104
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #132]
	ldr	r2, .L258+108
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #28]
	ldr	r2, .L258+112
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #218]
	ldr	r2, .L258+116
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #40]
	ldr	r2, .L258+120
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2572]
	ldr	r2, .L258+124
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #44]
	ldr	r2, .L258+128
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2576]
	ldr	r2, .L258+132
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #152]
	ldr	r2, .L258+136
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2350]
	ldr	r2, .L258+140
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2354]	@ zero_extendqisi2
	ldr	r2, .L258+144
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2348]
	ldr	r2, .L258+148
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2356]	@ zero_extendqisi2
	ldr	r2, .L258+152
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2352]
	ldr	r2, .L258+156
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #2348]
	add	r4, r4, r0
	ldr	r3, [r5, #2324]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L258+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2398]
	ldr	r2, .L258+164
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2402]	@ zero_extendqisi2
	ldr	r2, .L258+168
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2396]
	ldr	r2, .L258+172
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2404]	@ zero_extendqisi2
	ldr	r2, .L258+176
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2400]
	ldr	r2, .L258+180
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #2396]
	add	r4, r4, r0
	ldr	r3, [r5, #2324]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L258+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2446]
	ldr	r2, .L258+188
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2450]	@ zero_extendqisi2
	ldr	r2, .L258+192
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2444]
	ldr	r2, .L258+196
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2452]	@ zero_extendqisi2
	ldr	r2, .L258+200
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2448]
	ldr	r2, .L258+204
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2590]
	ldr	r2, .L258+208
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2594]	@ zero_extendqisi2
	ldr	r2, .L258+212
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2588]
	ldr	r2, .L258+216
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2596]	@ zero_extendqisi2
	ldr	r2, .L258+220
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2592]
	b	.L259
.L260:
	.align	2
.L258:
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
.L259:
	ldr	r2, .L261
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2712]
	add	r4, r4, r0
	ldr	r2, .L261+4
	movs	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2720]
	str	r3, [sp]
	ldr	r3, [r5, #2716]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2708]
	ldr	r2, .L261+8
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2732]
	ldr	r2, .L261+12
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3148]
	ldr	r2, .L261+16
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3150]
	ldr	r2, .L261+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3152]
	ldr	r2, .L261+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3156]
	ldr	r2, .L261+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L261+32
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #2344]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L261+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L261+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L247
.L252:
	subs	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L247:
	ldrh	r3, [r5, #2588]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L249
	ldr	r2, [r5, #2324]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L261+44
	bl	snprintf
	add	r4, r4, r0
.L249:
	movs	r0, #0
	ldr	r9, .L261+52
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L251:
	cmp	r0, fp
	beq	.L250
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
	bne	.L251
.L250:
	ldr	r2, [r5, #2316]
	movs	r7, #0
	ldr	r3, [r5, #2340]
	movw	r9, #65535
	ldr	fp, .L261+56
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L261+48
	muls	r3, r2, r3
	uxth	r3, r3
.L253:
	cmp	r3, r9
	beq	.L252
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
	beq	.L252
	ldr	r3, [r5, #2316]
	ldrh	r3, [r3, r8]
	b	.L253
.L262:
	.align	2
.L261:
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
	blt	.L265
	ldr	r3, .L266
	movs	r1, #64
	ldr	r2, .L266+4
	mov	r0, r5
	bl	snprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L265:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L267:
	.align	2
.L266:
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
	ldr	r4, .L292
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L269
	ldrh	r1, [r4, #40]
	movs	r3, #0
	ldr	r0, [r4, #2328]
	str	r3, [r4, #2548]
.L270:
	cmp	r3, r1
	bcc	.L271
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2548]
.L272:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L277
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L277
.L279:
	movw	r5, #65535
.L278:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L271:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2548]
	add	r2, r2, r5
	str	r2, [r4, #2548]
	b	.L270
.L269:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	bls	.L272
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	movs	r3, #0
.L274:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L272
	ldr	r1, [r4, #2328]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L274
.L277:
	ldrh	r0, [r4, #2344]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L279
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L279
	ldrh	r2, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	movs	r0, #0
	ldr	r8, [r4, #2328]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L280:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L283
	mov	r5, r2
.L282:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L279
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L284
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2568]
.L284:
	cmp	r7, r10
	bls	.L279
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L279
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L285
	ldr	r3, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L279
.L285:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L292+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3160]
	b	.L278
.L283:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L279
	ldrh	r5, [r3, #4]
	cbz	r5, .L281
	ldr	r5, .L292+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L282
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L281:
	mla	r3, fp, ip, lr
	b	.L280
.L293:
	.align	2
.L292:
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
	beq	.L295
	ldr	r2, .L296
	movs	r1, #0
	ldr	r2, [r2, #2324]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L295:
	movs	r0, #0
	pop	{r3, pc}
.L297:
	.align	2
.L296:
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
	bne	.L299
	movw	r2, #2740
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L299:
	ldr	r5, .L312+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L300
	movw	r2, #2741
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L300:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L301
	movw	r2, #2742
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L301:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r5, #32]
	movw	r6, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L302:
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L304
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	subs	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L306:
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
	beq	.L306
	strb	r3, [r4, #6]
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r1, r3
	bne	.L298
	cbz	r2, .L298
	movw	r2, #2763
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L298:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L304:
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
	b	.L302
.L313:
	.align	2
.L312:
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
	ldr	r3, .L323
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3164]
.L315:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L318
	ldr	r7, .L323
	mov	ip, #12
	movs	r6, #0
.L319:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L322
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L318:
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
	b	.L315
.L322:
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
	b	.L319
.L324:
	.align	2
.L323:
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
	ldr	r3, .L332
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3184]
	ldr	r6, [r3, #3168]
.L326:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L325
	mla	r4, r10, r3, r0
	movs	r2, #0
.L327:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L328
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L327
	str	r8, [r9, #8]
.L328:
	adds	r5, r5, #1
	b	.L326
.L325:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L333:
	.align	2
.L332:
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
	ldr	r3, .L341
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3184]
	ldr	r6, [r3, #3168]
.L335:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L339
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L339:
	mov	r9, #0
.L336:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L337
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L336
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L337:
	adds	r4, r4, #1
	b	.L335
.L342:
	.align	2
.L341:
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
	ldr	r2, .L348
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, #3192]
	add	r2, r3, r2, lsl #1
.L344:
	cmp	r3, r2
	bne	.L346
	movs	r0, #0
	bx	lr
.L346:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L344
	movs	r0, #1
	bx	lr
.L349:
	.align	2
.L348:
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
	ldr	r3, .L354
	ldrh	r1, [r3, #3192]
	ldr	r2, [r3, #3188]
.L351:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L353
	bne	.L352
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3192]
	adds	r0, r0, #1
	strh	r0, [r3, #3192]	@ movhi
	b	.L352
.L353:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L351
.L352:
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
.L355:
	.align	2
.L354:
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
	ldr	r0, .L359
	bl	sftl_printk
	ldr	r3, .L359+4
	ldrh	r0, [r3, #3202]
	cmp	r4, r0
	beq	.L357
	ldrh	r1, [r3, #3204]
	cmp	r4, r1
	beq	.L357
	movw	r2, #65535
	cmp	r0, r2
	bne	.L358
	strh	r4, [r3, #3202]	@ movhi
.L357:
	movs	r0, #0
	pop	{r4, pc}
.L358:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #3204]	@ movhi
	b	.L357
.L360:
	.align	2
.L359:
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
	ldr	r4, .L365
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3206]
	ldr	r0, .L365+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3206]
	movs	r2, #0
	addw	r0, r4, #3208
.L362:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L364
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3206]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3208]	@ movhi
	b	.L363
.L364:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L362
.L363:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L366:
	.align	2
.L365:
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
	ldr	r4, .L373
	ldrh	r3, [r4, #3206]
	cbz	r3, .L368
	ldrh	r1, [r4, #3202]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L368
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
.L368:
	movs	r0, #0
	pop	{r4, pc}
.L374:
	.align	2
.L373:
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
	ldr	r1, .L376
	b	__kmalloc
.L377:
	.align	2
.L376:
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
	ldr	fp, .L388+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L380:
	cmp	r4, r9
	bcc	.L386
	ldr	r0, .L388
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L386:
	cbnz	r5, .L381
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L388+4
	bl	sftl_printk
.L381:
	cmp	r8, #4
	bne	.L382
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L388+8
.L387:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L385
	movs	r5, #0
	ldr	r0, .L388
	bl	sftl_printk
.L385:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L380
.L382:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L388+12
	b	.L387
.L389:
	.align	2
.L388:
	.word	.LC79
	.word	.LC75
	.word	.LC76
	.word	.LC78
	.word	.LC77
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r5, .L406
	mov	r4, r0
	mov	fp, r0
	movs	r7, #0
	mov	r10, #-1
	.pad #20
	sub	sp, sp, #20
	ldrh	r9, [r5, #12]
	lsl	r3, r9, #3
	str	r3, [sp, #4]
.L391:
	cmp	r7, r8
	beq	.L395
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, fp
	bl	l2p_addr_tran
	ldr	r6, [sp, #8]
	cbnz	r6, .L392
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bls	.L392
	ldr	r5, .L406+4
	ldr	r7, .L406+8
	ldr	r9, .L406+16
	b	.L405
.L394:
	mov	r3, #-1
	ldr	r2, [sp, #12]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r7
	adds	r6, r6, #1
	bl	sftl_printk
	movs	r3, #16
	movs	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r9
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L406+12
	bl	rknand_print_hex
.L405:
	cmp	r6, r8
	add	r4, r4, #20
	bne	.L394
	bl	dump_stack
.L395:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L392:
	ldr	r2, [r5, #3248]
	uxtb	r0, r6
	ldr	r1, [sp, #12]
	blx	r2
	cmp	r0, #0
	mov	r2, r0
	it	ne
	movne	r2, r10
	str	r2, [fp]
	ldrh	r2, [r5, #14]
	cmp	r2, #4
	bne	.L399
	ldr	r1, [sp, #12]
	ldr	r2, [r5, #3248]
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r2
	cbz	r0, .L399
	str	r10, [fp]
.L399:
	adds	r7, r7, #1
	add	fp, fp, #20
	b	.L391
.L407:
	.align	2
.L406:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC80
	.word	.LC82
	.word	.LC81
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
	bhi	.L408
	ldr	r4, .L417
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	beq	.L408
	cbz	r1, .L410
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3260]
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
.L410:
	ldrh	r3, [r4, #218]
	adds	r3, r3, #1
	strh	r3, [r4, #218]	@ movhi
	ldrh	r3, [r4, #216]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #220]	@ movhi
	strh	r3, [r4, #216]	@ movhi
.L408:
	pop	{r4, r5, r6, pc}
.L418:
	.align	2
.L417:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r4, .L424
	ldrh	r2, [r4, #218]
	cbz	r2, .L423
	ldrh	r3, [r4, #214]
	subs	r2, r2, #1
	ldr	r0, [r4, #3260]
	strh	r2, [r4, #218]	@ movhi
	movs	r2, #1
	add	r1, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r1, #220]
	mov	r1, r2
	strh	r3, [r4, #214]	@ movhi
	lsls	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2560]
	adds	r3, r3, #1
	str	r3, [r4, #2560]
.L420:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L421
	ldrh	r2, [r4, #218]
	mov	r1, r5
	ldr	r0, .L424+4
	bl	sftl_printk
.L422:
	b	.L422
.L423:
	movw	r5, #65535
	b	.L420
.L421:
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L425:
	.align	2
.L424:
	.word	.LANCHOR0
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
.L427:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L430
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L427
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L428
	ldr	r3, .L434
	ldr	r0, .L434+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L429:
	b	.L429
.L428:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L430:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L432
	movw	r2, #578
	ldr	r1, .L434+8
	ldr	r0, .L434+12
	bl	sftl_printk
.L432:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L435:
	.align	2
.L434:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LANCHOR1+147
	.word	.LC1
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	ldr	r5, .L443
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
	bls	.L437
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	memset
	movs	r0, #0
.L437:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L442
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L439:
	cmp	r3, r1
	bne	.L440
.L442:
	pop	{r4, r5, r6, pc}
.L440:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L439
.L444:
	.align	2
.L443:
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
	cbz	r2, .L446
	bl	memset
.L446:
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
	ldr	r4, .L551
	movs	r5, #0
	movs	r7, #12
	movs	r6, #20
	ldrh	r0, [r4, #106]
	str	r3, [r4, #3268]
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
	str	r5, [r4, #3264]
	str	r5, [r4, #3160]
	str	r5, [r4, #3272]
	str	r5, [r4, #3152]
	str	r5, [r4, #3276]
	strh	r5, [r4, #3156]	@ movhi
	strh	r5, [r4, #3206]	@ movhi
	strh	r5, [r4, #3242]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3188]
	ldrh	r0, [r4, #106]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	str	r0, [r4, #3196]
	muls	r6, r3, r6
	lsl	r8, r6, #2
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3280]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3284]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3288]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3260]
	mov	r0, r6
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	ldrh	r6, [r4, #110]
	str	r0, [r4, #3180]
	lsls	r3, r3, #1
	mov	r0, r6
	adds	r3, r3, #1
	str	r3, [r4, #3184]
	bl	ftl_malloc
	str	r0, [r4, #3292]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3296]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3300]
	ldr	r0, [r4, #3184]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3308]
	ldr	r0, [r4, #3184]
	muls	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3168]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	ldrh	r0, [r4, #58]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r6, [r4, #32]
	str	r0, [r4, #3320]
	muls	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	lsls	r0, r6, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3328]
	ldr	r0, [r4, #3184]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3332]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3332]
	str	r0, [r4, #3336]
	ldr	r0, .L551+4
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3332]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r4, #42]
	str	r0, [r4, #3340]
	adds	r0, r0, #32
	str	r0, [r4, #2328]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3344]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #128]
	str	r0, [r4, #2324]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3348]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3352]
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
	str	r0, [r4, #3356]
	ldrh	r0, [r4, #120]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3360]
	ldrh	r0, [r4, #122]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3364]
	add	r5, r4, #180
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldr	r0, [r4, #128]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r4, #138]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #42]
	str	r0, [r4, #3376]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2316]
	ldrh	r0, [r4, #98]
	ldrh	r3, [r4, #54]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3380]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3380]
	movs	r3, #1
	ldrh	r6, [r4, #54]
	str	r0, [r4, #180]
	lsls	r2, r2, #2
	mov	r1, r2
.L451:
	cmp	r3, r6
	bcc	.L452
	ldr	r2, .L551+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L453:
	cmp	r2, r3
	bne	.L454
	ldr	r3, [r4, #3348]
	cbnz	r3, .L455
.L457:
	ldr	r1, .L551+12
	ldr	r0, .L551+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L452:
	ldr	r0, [r4, #180]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L451
.L454:
	str	r1, [r3, #4]!
	b	.L453
.L552:
	.align	2
.L551:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+173
	.word	.LC85
.L455:
	ldr	r3, [r4, #3352]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3368]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3372]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3376]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3280]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3260]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3324]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3328]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L457
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L457
	ldr	r3, .L553
	ldr	r2, [r3, #3356]
	cmp	r2, #0
	beq	.L457
	ldr	r2, [r3, #3360]
	cmp	r2, #0
	beq	.L457
	ldr	r3, [r3, #3364]
	cmp	r3, #0
	beq	.L457
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L554:
	.align	2
.L553:
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
	ldr	r6, .L561
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L561+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3380]
	movs	r1, #0
	ldr	r10, .L561+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L558:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L555
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L557
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L557:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L558
.L555:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L562:
	.align	2
.L561:
	.word	.LANCHOR0
	.word	.LANCHOR1+184
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	movs	r3, #0
.L564:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L568
	ldr	r3, .L583
	movs	r6, #0
	mov	r9, r6
	mov	fp, r6
	ldrh	r2, [r3, #104]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	r10, [r5]
.L569:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L573
	mov	r0, r9
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L568:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L565:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L567
	adds	r3, r3, #1
	b	.L564
.L567:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L566
	cbz	r1, .L566
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L566:
	adds	r2, r2, #1
	b	.L565
.L573:
	uxth	r2, r6
	ldrh	r3, [r5, r2, lsl #1]
	lsl	r8, r2, #1
	cmp	r10, r3
	bls	.L570
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cbnz	r0, .L571
.L572:
	adds	r6, r6, #1
	b	.L569
.L570:
	cmp	r3, #0
	bne	.L572
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cmp	r0, #0
	beq	.L572
.L574:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r8]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L572
.L571:
	mov	r9, r1
	mov	r10, r3
	cmp	r3, #0
	beq	.L574
	b	.L572
.L584:
	.align	2
.L583:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
	ldr	r4, .L588
	movs	r6, #12
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3352]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	movs	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3376]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	mov	r7, r2
.L586:
	ldrh	r4, [r3, #138]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L587
	ldr	r2, [r3, #128]
	strh	r1, [r3, #3386]	@ movhi
	strh	r1, [r3, #3384]	@ movhi
	strh	r2, [r3, #3394]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3388]	@ movhi
	ldrh	r2, [r3, #3428]
	strh	r2, [r3, #3392]	@ movhi
	ldrh	r2, [r3, #136]
	strh	r2, [r3, #3390]	@ movhi
	ldr	r2, [r3, #3348]
	str	r2, [r3, #3396]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3400]
	ldr	r2, [r3, #3352]
	str	r2, [r3, #3404]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3408]
	pop	{r3, r4, r5, r6, r7, pc}
.L587:
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
	ldr	r4, [r3, #3376]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L586
.L589:
	.align	2
.L588:
	.word	.LANCHOR0
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
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
	ldr	r4, .L591
	movw	r3, #65535
	mov	r1, r5
	ldrh	r2, [r4, #120]
	strh	r3, [r4, #3430]	@ movhi
	mov	r3, #-1
	ldr	r0, [r4, #148]
	str	r3, [r4, #3440]
	lsls	r2, r2, #1
	str	r5, [r4, #3432]
	str	r5, [r4, #3436]
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3336]
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
.L592:
	.align	2
.L591:
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
	ldr	r4, .L604
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
.L594:
	ldrh	r3, [r4, #40]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L601
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #32]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L602
.L596:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L595
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L595:
	adds	r3, r3, #1
.L602:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L596
	uxth	r3, r5
	cbz	r7, .L597
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L598:
	ldr	r1, [r4, #2316]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #2348]
	cmp	r2, r8
	beq	.L599
	ldrh	r2, [r4, #2396]
	cmp	r2, r8
	beq	.L599
	ldrh	r2, [r4, #2444]
	cmp	r2, r8
	beq	.L599
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L600
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L599:
	adds	r5, r5, #1
	b	.L594
.L597:
	ldr	r2, [r4, #2324]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L598
.L600:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L599
.L601:
	strh	r6, [r4, #2336]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #2344]	@ movhi
	ble	.L603
	movw	r2, #2170
	ldr	r1, .L604+4
	ldr	r0, .L604+8
	bl	sftl_printk
.L603:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L605:
	.align	2
.L604:
	.word	.LANCHOR0
	.word	.LANCHOR1+198
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
	ldr	r4, .L607
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
.L608:
	.align	2
.L607:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r1
	ldr	r5, .L637
	mov	r4, r0
	mov	r8, #0
	.pad #20
	sub	sp, sp, #20
	ldr	r10, .L637+4
	ldr	fp, .L637+8
	ldrh	r3, [r5, #12]
	str	r3, [sp, #4]
.L611:
	cmp	r8, r9
	bne	.L621
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L621:
	ldr	r3, [r4, #8]
	cbz	r3, .L612
	ldr	r3, [r4, #12]
	cbnz	r3, .L613
.L612:
	movs	r2, #96
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L613:
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r6, [r4, #8]
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #12]
	lsls	r2, r6, #26
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	it	ne
	ldrne	r6, [r5, #3312]
	ldr	r7, [r5, #3256]
	mov	r2, r6
	blx	r7
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L616
	ldr	r0, [sp, #4]
	add	r2, r6, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #12]
	ldr	r7, [r5, #3256]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	blx	r7
	adds	r3, r0, #1
	beq	.L617
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L618
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L618
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L618
.L617:
	mov	r3, #-1
	str	r3, [r4]
.L618:
	ldr	r3, [r4]
	adds	r3, r3, #1
	beq	.L616
	cmp	r0, #256
	it	eq
	streq	r0, [r4]
.L616:
	ldr	r3, [r5, #3312]
	cmp	r6, r3
	bne	.L620
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L620
	ldrh	r2, [r5, #58]
	mov	r1, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L620:
	add	r8, r8, #1
	adds	r4, r4, #20
	b	.L611
.L638:
	.align	2
.L637:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
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
	ldr	r4, .L649
	movw	r10, #61664
	ldr	r3, [r4, #3292]
	add	r7, r4, #162
	ldr	r8, [r4, #3324]
	addw	r9, r4, #3444
	str	r3, [r4, #3452]
	str	r8, [r4, #3456]
.L640:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L645
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L645:
	ldrh	r5, [r4, #98]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L642:
	ldrh	r3, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L643
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3448]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	beq	.L642
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L642
	strh	r5, [r7]	@ movhi
.L643:
	adds	r6, r6, #1
	b	.L640
.L650:
	.align	2
.L649:
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
	ldr	r3, .L662
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
	ldr	r3, [r3, #3316]
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
	bne	.L654
	mov	r8, #0
.L655:
	cmp	r8, r5
	ble	.L658
.L654:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L658:
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
	bne	.L656
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L656
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L656
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L655
.L656:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L655
.L663:
	.align	2
.L662:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
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
	ldr	r4, .L746
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3372]
	strh	r5, [r4, #3428]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3348]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3360]
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
.L665:
	ldrh	r3, [r4, #42]
	cmp	r3, r8
	bls	.L707
	movs	r5, #0
	ldrh	fp, [r4, #32]
	ldr	r9, [r4, #3280]
	mov	r7, r5
	ldr	r3, [r4, #3172]
	mov	r10, #20
	ldr	r2, [r4, #3176]
	ldrh	r6, [r4, #112]
	b	.L708
.L668:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L746+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L666
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
.L666:
	adds	r5, r5, #1
.L708:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L668
	cbnz	r7, .L669
.L706:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L665
.L669:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L670:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L706
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3280]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L673
	mov	fp, #16
	movw	r3, #65535
.L675:
	ldr	r0, [r4, #3280]
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
	bne	.L672
	ldr	r3, [r4, #3280]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3280]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L673
.L674:
	movs	r1, #1
	b	.L745
.L672:
	ldr	r2, [r4, #3280]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L673
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L675
	b	.L674
.L673:
	ldr	r2, [r4, #2540]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L676
	cmp	r2, r3
	bhi	.L677
.L676:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2540]
.L677:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L679
	bhi	.L680
	movw	r3, #61574
	cmp	r2, r3
	beq	.L681
.L678:
	add	r9, r9, #1
	b	.L670
.L680:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L682
	movw	r3, #65535
	cmp	r2, r3
	bne	.L678
	movs	r1, #0
.L745:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L678
.L682:
	ldrh	r2, [r4, #3428]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L684
	movw	r2, #1222
	ldr	r1, .L746+8
	ldr	r0, .L746+12
	bl	sftl_printk
.L684:
	ldr	r1, [r4, #128]
	ldrh	r0, [r4, #3428]
	ldr	r10, [r4, #3372]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L685:
	cmp	r3, ip
	bgt	.L691
	cmp	r3, #0
	bge	.L723
	b	.L678
.L691:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L686
	ldr	r2, [r10]
	cbnz	r2, .L687
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3428]	@ movhi
.L687:
	uxth	ip, r3
	movs	r2, #0
.L688:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L689
	ldr	r2, [r4, #3372]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3348]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L678
	ldrh	r0, [r4, #3428]
	ldr	r2, [r4, #128]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L678
.L723:
	ldr	r2, [r4, #3372]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3428]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3348]
.L743:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L678
.L689:
	ldr	r0, [r4, #3372]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3348]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L688
.L686:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L685
.L747:
	.align	2
.L746:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR1+231
	.word	.LC1
.L681:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L694
	movw	r2, #1263
	ldr	r1, .L748
	ldr	r0, .L748+4
	bl	sftl_printk
.L694:
	ldrh	lr, [r4, #120]
	ldrh	r0, [r4, #144]
	ldr	r10, [r4, #3360]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L695:
	cmp	r3, ip
	ble	.L700
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L696
	ldr	r2, [r10]
	cbnz	r2, .L697
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #144]	@ movhi
.L697:
	uxth	ip, r3
	movs	r2, #0
.L698:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L699
	ldr	r2, [r4, #3360]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #148]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L700:
	cmp	r3, #0
	blt	.L678
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L678
	adds	r1, r1, #1
	ldr	r2, [r4, #3360]
	strh	r1, [r4, #144]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #148]
	b	.L743
.L699:
	ldr	r0, [r4, #3360]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #148]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L698
.L696:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L695
.L679:
	ldrh	r1, [r4, #2576]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L702
	strh	r5, [r4, #2576]	@ movhi
.L744:
	str	r3, [r4, #2584]
	b	.L678
.L702:
	ldrh	r0, [r4, #2580]
	cmp	r0, r2
	beq	.L703
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L703:
	ldr	r2, [r4, #2584]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L704
	ldrh	r3, [r4, #2576]
	strh	r5, [r4, #2576]	@ movhi
	strh	r3, [r4, #2580]	@ movhi
	ldr	r3, [r6, #4]
	b	.L744
.L704:
	strh	r5, [r4, #2580]	@ movhi
	b	.L678
.L707:
	ldr	r1, [r4, #3348]
	ldrh	r3, [r1]
	cbz	r3, .L709
.L712:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L710
.L711:
	ldrh	r2, [r4, #3428]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L742
	movw	r2, #1388
	ldr	r1, .L748
	ldr	r0, .L748+4
	bl	sftl_printk
.L742:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L709:
	ldrh	r2, [r4, #3428]
	cmp	r2, #0
	beq	.L712
	ldr	r0, [r4, #128]
.L713:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L712
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L713
	mov	r3, r2
	movs	r5, #0
.L714:
	ldr	r1, [r4, #128]
	cmp	r3, r1
	bcs	.L712
	ldr	r1, [r4, #3348]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3372]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3348]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L714
.L710:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	beq	.L711
	ldrh	r0, [r4, #120]
.L719:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L711
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L719
	movs	r0, #0
.L720:
	ldrh	r2, [r4, #120]
	cmp	r3, r2
	bge	.L711
	ldr	r2, [r4, #148]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3360]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #148]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L720
.L749:
	.align	2
.L748:
	.word	.LANCHOR1+231
	.word	.LC1
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
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
	ldr	r4, .L779
	ldr	r3, [r4, #3292]
	addw	r7, r4, #3444
	ldr	r6, [r4, #3324]
	str	r3, [r4, #3452]
	str	r6, [r4, #3456]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
.L751:
	ldrh	r3, [r4, #98]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L754
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3448]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	bne	.L752
	ldr	r3, [r4, #3448]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3448]
	bl	FlashReadPages
.L752:
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	beq	.L753
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L753
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L754:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L768
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L758
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L779+4
	str	r3, [r4, #3448]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	beq	.L758
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L758
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	bls	.L758
	ldrh	r2, [r4, #156]
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
.L758:
	ldr	r7, .L779+4
	movs	r1, #1
	ldrh	r0, [r4, #152]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L760:
	cmp	r5, #0
	bge	.L763
	movs	r2, #253
	ldr	r1, .L779+8
	ldr	r0, .L779+12
	bl	sftl_printk
.L762:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L765
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L765
	ldrh	r3, [r4, #42]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L765
	cmp	r0, r3
	bcs	.L765
	bl	FtlSysBlkNumInit
.L765:
	ldr	r6, .L779+16
	movs	r5, #0
.L766:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L767
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L753:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L751
.L763:
	ldrh	r3, [r4, #152]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3448]
	ldr	r3, [r4, #3292]
	str	r3, [r4, #3452]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	beq	.L761
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L762
.L761:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L760
.L767:
	ldrh	r2, [r4, #3380]
	ldr	r1, [r4, #3452]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L766
.L768:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L780:
	.align	2
.L779:
	.word	.LANCHOR0
	.word	.LANCHOR0+3444
	.word	.LANCHOR1+245
	.word	.LC1
	.word	.LANCHOR0+176
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
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
	ldr	r4, .L802
	ldr	r3, [r4, #3292]
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3452]
	ldr	r3, [r4, #3324]
	lsls	r2, r2, #1
	str	r3, [r4, #3456]
	bl	ftl_memset
	ldrh	r0, [r4, #2576]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L782
.L790:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L782:
	ldr	r7, .L802+4
	movs	r1, #1
	addw	r6, r4, #3444
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2578]	@ movhi
.L784:
	cmp	r5, #0
	bge	.L787
	mov	r2, #1448
	ldr	r1, .L802+8
	ldr	r0, .L802+12
	bl	sftl_printk
.L786:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L789
	movw	r2, #1450
	ldr	r1, .L802+8
	ldr	r0, .L802+12
	bl	sftl_printk
.L789:
	movs	r2, #48
	ldr	r1, [r4, #3452]
	ldr	r0, .L802+16
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3452]
	ldr	r0, [r4, #2324]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3452]
	ldr	r0, [r4, #24]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L802+4
	cmp	r2, r3
	bne	.L790
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r5, [r4, #2276]
	cmp	r2, r3
	strh	r5, [r4, #2582]	@ movhi
	bne	.L790
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	ldrh	r0, [r4, #158]
	muls	r3, r5, r3
	str	r5, [r4, #3464]
	ldrh	r1, [r4, #32]
	subs	r0, r6, r0
	subs	r0, r0, r5
	str	r3, [r4, #2504]
	muls	r3, r2, r3
	str	r3, [r4, #140]
	bl	__aeabi_uidiv
	cmp	r5, r6
	strh	r0, [r4, #2572]	@ movhi
	bls	.L791
	mov	r2, #1472
	ldr	r1, .L802+8
	ldr	r0, .L802+12
	bl	sftl_printk
.L791:
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
	beq	.L794
	ldr	r0, .L802+20
	bl	make_superblock
.L794:
	ldrh	r2, [r4, #2396]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L795
	ldr	r0, .L802+24
	bl	make_superblock
.L795:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L796
	ldr	r0, .L802+28
	bl	make_superblock
.L796:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L797
	ldr	r0, .L802+32
	bl	make_superblock
.L797:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L787:
	ldrh	r3, [r4, #2576]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r6
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3448]
	ldr	r3, [r4, #3292]
	str	r3, [r4, #3452]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	beq	.L785
	ldr	r3, [r4, #3292]
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L785
	ldr	r3, [r4, #3324]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L786
.L785:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L784
.L803:
	.align	2
.L802:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR1+256
	.word	.LC1
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2588
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FlashProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L840
	mov	fp, r1
	mov	r4, r0
	mov	r5, r0
	str	r3, [sp, #12]
	mov	r10, #0
	ldrh	r3, [r6, #12]
	mov	r8, r6
	str	r2, [sp, #8]
	str	r3, [sp]
	lsls	r3, r3, #3
	str	r3, [sp, #4]
.L805:
	cmp	r10, fp
	bne	.L818
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L811
	movs	r5, #0
	ldr	r7, .L840+4
	ldr	r8, .L840+24
	ldr	r9, .L840+28
	b	.L819
.L818:
	ldr	r3, [r5, #8]
	cbz	r3, .L806
	ldr	r3, [r5, #12]
	cbnz	r3, .L807
.L806:
	movs	r2, #136
	ldr	r1, .L840+8
	ldr	r0, .L840+12
	bl	sftl_printk
.L807:
	add	r2, sp, #20
	add	r1, sp, #24
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #20]
	cbnz	r7, .L808
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L808
	ldr	r5, .L840+8
	ldr	r6, .L840+16
	ldr	r8, .L840+32
	b	.L839
.L810:
	mov	r3, #-1
	ldr	r2, [r4, #-16]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r6
	adds	r7, r7, #1
	bl	sftl_printk
	movs	r3, #16
	movs	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r8
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L840+20
	bl	rknand_print_hex
.L839:
	cmp	r7, fp
	add	r4, r4, #20
	bne	.L810
	bl	dump_stack
.L811:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L808:
	ldr	r1, [r5, #8]
	lsls	r3, r1, #26
	beq	.L825
	ldr	r9, [r8, #3312]
	cmp	r1, r9
	beq	.L812
	ldrh	r2, [r8, #58]
	mov	r0, r9
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L812:
	ldr	r3, [r5, #12]
	mov	r2, r9
	ldr	r1, [sp, #24]
	ldrb	r0, [sp, #20]	@ zero_extendqisi2
	ldr	r7, [r8, #3252]
	blx	r7
	cbnz	r0, .L813
	str	r0, [r5]
.L814:
	ldrh	r3, [r8, #14]
	cmp	r3, #4
	bne	.L816
	ldr	r0, [sp]
	add	r2, r9, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [sp, #24]
	ldr	r7, [r8, #3252]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #20]	@ zero_extendqisi2
	blx	r7
	cbz	r0, .L816
	mov	r3, #-1
	str	r3, [r5]
.L816:
	add	r10, r10, #1
	adds	r5, r5, #20
	b	.L805
.L825:
	mov	r9, r1
	b	.L812
.L813:
	mov	r3, #-1
	str	r3, [r5]
	b	.L814
.L823:
	ldr	r2, [r6, #3312]
	movs	r3, #0
	movs	r1, #1
	add	r0, sp, #28
	str	r3, [r2]
	ldr	r2, [r6, #3320]
	str	r3, [r2]
	ldr	r3, [r4, #4]
	ldr	r2, [sp, #8]
	str	r3, [sp, #32]
	ldr	r3, [r6, #3312]
	str	r3, [sp, #36]
	ldr	r3, [r6, #3320]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	fp, [sp, #28]
	cmp	fp, #-1
	bne	.L820
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L820:
	ldr	r3, [r4, #12]
	cbz	r3, .L821
	ldr	r2, [r3]
	ldr	r3, [r6, #3320]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L821
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L821:
	ldr	r3, [r4, #8]
	cbz	r3, .L822
	ldr	r2, [r3]
	ldr	r3, [r6, #3312]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L822
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L822:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L819:
	cmp	r10, r5
	bne	.L823
	b	.L811
.L841:
	.align	2
.L840:
	.word	.LANCHOR0
	.word	.LC86
	.word	.LANCHOR1+271
	.word	.LC1
	.word	.LC80
	.word	.LC82
	.word	.LC87
	.word	.LC88
	.word	.LC81
	.fnend
	.size	FlashProgPages, .-FlashProgPages
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
	ldr	r4, .L887
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp, #4]
	str	r0, [r4, #3276]
.L843:
	ldrh	r1, [r4, #32]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L848
	cmp	r6, #0
	beq	.L842
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3260]
	bl	FlashEraseBlocks
.L851:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L853
	cmp	r7, #0
	beq	.L870
	ldrh	r3, [r4, #104]
	mov	r10, #1
	str	r3, [sp, #8]
.L854:
	mov	r8, #0
	mov	fp, #20
.L864:
	mov	r9, #0
	mov	r6, r9
.L855:
	ldrh	r1, [r4, #32]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L859
	cbz	r6, .L842
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3260]
	bl	FlashProgPages
.L861:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L863
	add	r8, r8, #1
	ldr	r2, [sp, #8]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L864
	mov	r8, #0
	mov	r9, #20
.L865:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L867
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L868
	cbz	r7, .L842
.L868:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3260]
	bl	FlashEraseBlocks
.L842:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L848:
	uxth	r3, r10
	ldr	r0, [r4, #3260]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	cbz	r7, .L844
	bl	IsBlkInVendorPart
	cbnz	r0, .L845
.L844:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L846
	ldr	r1, [r4, #3260]
	lsl	r3, r8, #10
	ldr	r0, [fp, #3328]
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [r4, #3308]
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
.L845:
	add	r10, r10, #1
	b	.L843
.L846:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L845
.L853:
	mul	r3, r9, r8
	ldr	r2, [r4, #3260]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L852
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L852:
	add	r8, r8, #1
	b	.L851
.L870:
	movs	r3, #2
	mov	r10, r7
	str	r3, [sp, #8]
	b	.L854
.L859:
	uxth	r3, r9
	ldr	r0, [r4, #3260]
	movs	r2, #0
	mul	r1, fp, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	cbz	r7, .L856
	bl	IsBlkInVendorPart
	cbnz	r0, .L857
.L856:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L857
	ldr	r1, [r4, #3260]
	ldr	r3, [sp, #12]
	ldr	r2, .L887
	mla	r1, fp, r6, r1
	ldr	r0, [r2, #3308]
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r4, #3304]
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
.L857:
	add	r9, r9, #1
	b	.L855
.L863:
	mul	r3, fp, r9
	ldr	r2, [r4, #3260]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L862
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L862:
	add	r9, r9, #1
	b	.L861
.L867:
	cbz	r7, .L866
	mul	r3, r9, r8
	ldr	r2, [r4, #3260]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L866
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L866:
	add	r8, r8, #1
	b	.L865
.L888:
	.align	2
.L887:
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	bne	.L890
	ldrh	r3, [r0, #8]
	cbz	r3, .L891
	movw	r2, #641
	ldr	r1, .L899
	ldr	r0, .L899+4
	bl	sftl_printk
.L891:
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
.L892:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L890:
	ldrh	r6, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L899+8
	ldr	r2, [r0, #28]
	ldr	r7, [r0, #24]
	orr	r3, r3, r6, lsl #10
	str	r3, [r5, #3448]
	ldr	r3, [r5, #3292]
	str	r3, [r5, #3452]
	ldr	r3, [r5, #3324]
	str	r3, [r5, #3456]
	str	r2, [r3, #4]
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3292]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L893:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L895
	movs	r2, #1
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L899+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L892
.L895:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L894
	ldr	r0, [r5, #3292]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3292]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L894:
	adds	r2, r2, #1
	b	.L893
.L900:
	.align	2
.L899:
	.word	.LANCHOR1+286
	.word	.LC1
	.word	.LANCHOR0
	.word	.LANCHOR0+3444
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
	ldr	r7, .L925
	mov	r8, r1
	mov	r10, r2
	movs	r6, #0
	ldr	r9, .L925+20
	mov	r5, r7
.L902:
	ldr	r3, [r7, #2524]
	adds	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L903
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L904
.L903:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L904:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L905
	movw	r2, #699
	mov	r1, r9
	ldr	r0, .L925+4
	bl	sftl_printk
.L905:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L906
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L925+4
	bl	sftl_printk
.L906:
	ldrh	r2, [r4]
	movs	r1, #16
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3324]
	ldrh	fp, [r3, r2, lsl #1]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3456]
	str	r10, [r5, #3452]
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #3448]
	bl	__memzero
	ldr	r3, [r5, #3456]
	ldr	r2, [r4, #28]
	ldr	r0, .L925+8
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
	ldr	r2, [r5, #3444]
	adds	r1, r2, #1
	bne	.L907
	ldr	r1, [r5, #3448]
	adds	r6, r6, #1
	ldr	r0, .L925+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #104]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L909
	mov	r2, r6
	ldr	r1, [r5, #3448]
	ldr	r0, .L925+16
	bl	sftl_printk
.L910:
	b	.L910
.L909:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L902
.L924:
	b	.L924
.L907:
	cmp	r3, #1
	beq	.L913
	cmp	r2, #256
	beq	.L913
	ldr	r0, [r4, #36]
	cbz	r0, .L914
.L913:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L902
.L914:
	ldr	r2, [r5, #3448]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L926:
	.align	2
.L925:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+3444
	.word	.LC89
	.word	.LC90
	.word	.LANCHOR1+317
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
	ldr	r4, .L934
	mov	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	bcs	.L928
	movw	r2, #485
	ldr	r1, .L934+4
	ldr	r0, .L934+8
	bl	sftl_printk
.L928:
	ldr	r3, [r4, #3368]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L929
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
.L930:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L929:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	movs	r2, #1
	ldr	r0, .L934+12
	mov	r1, r2
	str	r7, [r4, #3448]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3452]
	ldr	r3, [r4, #3324]
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r8, [r4, #3456]
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L931
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L934+16
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3456]
	mov	r2, r3
	ldr	r0, .L934+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	movs	r2, #4
	ldr	r1, [r4, #3368]
	ldr	r0, .L934+24
	bl	rknand_print_hex
.L932:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L933
	mov	r2, #508
	ldr	r1, .L934+4
	ldr	r0, .L934+8
	bl	sftl_printk
.L933:
	ldr	r3, [r4, #2492]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L930
.L931:
	ldr	r3, [r4, #3444]
	cmp	r3, #256
	bne	.L932
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L934+28
	bl	sftl_printk
	ldr	r3, [r4, #2492]
	mov	r1, r6
	ldr	r0, .L934+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L932
.L935:
	.align	2
.L934:
	.word	.LANCHOR0
	.word	.LANCHOR1+333
	.word	.LC1
	.word	.LANCHOR0+3444
	.word	.LC91
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.word	.LANCHOR0+3384
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
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L952
	subs	r3, r3, #5
	cmp	r2, r3
	blt	.L937
	uxth	r0, r0
	ldrh	r9, [r6, r0, lsl #1]
	cmp	r9, #0
	beq	.L937
	ldr	r3, [r4, #32]
	cbnz	r3, .L937
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L938
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L938:
	movs	r6, #0
.L939:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L946
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L937:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L947
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L947:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L946:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L940
	ldr	r2, [r5, #3296]
	ldr	r8, [r5, #3324]
	ldr	r0, .L952+4
	str	r2, [r5, #3452]
	str	r8, [r5, #3456]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3448]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L941
	movw	r2, #611
	ldr	r1, .L952+8
	ldr	r0, .L952+12
	bl	sftl_printk
.L941:
	ldr	r2, [r5, #3444]
	adds	r2, r2, #1
	bne	.L942
.L944:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L943:
	b	.L943
.L942:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L944
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L944
	ldr	r2, [r5, #3452]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L940:
	adds	r6, r6, #1
	b	.L939
.L953:
	.align	2
.L952:
	.word	.LANCHOR0
	.word	.LANCHOR0+3444
	.word	.LANCHOR1+349
	.word	.LC1
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
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
	ldr	r5, .L981
	ldr	r9, [r0, #12]
	lsl	r2, r10, #2
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #4]
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #3292]
	movs	r2, #1
	ldr	r6, [r5, #3324]
	str	r2, [r4, #36]
	str	r3, [r5, #3452]
	movw	r3, #65535
	str	r6, [r5, #3456]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	str	r3, [r4, #32]
	mov	r8, r3
	str	r3, [r4, #28]
	ldr	r3, [sp, #8]
	add	fp, r3, #-1
	mov	r3, r5
.L955:
	ldr	r1, [sp, #8]
	sxth	r2, r8
	cmp	r2, r1
	bge	.L972
	cmp	r2, fp
	lsl	r7, r2, #1
	bne	.L956
	ldrh	r0, [r9, fp, lsl #1]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, [sp, #12]
	add	r7, r7, r9
	strh	r8, [r4]	@ movhi
	sxth	r9, r0
	ldr	r8, .L981+4
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	add	r9, r9, #1
	ldr	r3, [r3, fp, lsl #2]
	mov	fp, #0
	str	r3, [r4, #28]
.L957:
	sxth	r2, fp
	cmp	r2, r9
	blt	.L959
.L972:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L961
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L961:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L959:
	ldrh	r1, [r7]
	mov	r0, r8
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3448]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3444]
	adds	r2, r2, #1
	beq	.L958
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L958
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r5, #3448]
	ldreq	r3, [sp, #4]
	streq	r1, [r3, r2, lsl #2]
.L958:
	add	fp, fp, #1
	b	.L957
.L956:
	ldr	r1, [r5, #3292]
	str	r3, [sp, #20]
	add	r3, r9, r7
	ldr	r0, .L981+4
	str	r1, [r5, #3452]
	ldrh	r1, [r9, r2, lsl #1]
	ldrh	r2, [r5, #104]
	str	r3, [sp, #16]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3448]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3444]
	ldr	r3, [sp, #20]
	adds	r2, r2, #1
	beq	.L974
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L974
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L963
.L974:
	movs	r7, #0
.L964:
	ldrh	r1, [r3, #104]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L970
	ldr	r1, [sp, #16]
	ldr	r0, .L981+4
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r3, #3448]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #3444]
	adds	r2, r2, #1
	beq	.L968
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L968
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r3, #3448]
	ldreq	r0, [sp, #4]
	streq	r1, [r0, r2, lsl #2]
.L968:
	adds	r7, r7, #1
	b	.L964
.L963:
	movs	r0, #0
	mov	ip, #4
.L965:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L967
.L970:
	add	r8, r8, #1
	b	.L955
.L967:
	ldr	r7, [r5, #3292]
	ldr	r1, [r7, r2, lsl #3]
	uxth	lr, r1
	cmp	r10, lr
	bls	.L966
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
	str	r2, [r7, lr, lsl #2]
.L966:
	adds	r0, r0, #1
	b	.L965
.L982:
	.align	2
.L981:
	.word	.LANCHOR0
	.word	.LANCHOR0+3444
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
	ldr	r0, .L984
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #120]
	strh	r3, [r0, #3478]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3472]	@ movhi
	ldrh	r3, [r0, #144]
	strh	r3, [r0, #3476]	@ movhi
	ldrh	r3, [r0, #122]
	strh	r3, [r0, #3474]	@ movhi
	ldr	r3, [r0, #148]
	addw	r0, r0, #3468
	str	r3, [r0, #12]
	ldr	r3, [r0, #-108]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-112]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-104]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L985:
	.align	2
.L984:
	.word	.LANCHOR0
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	ldr	r0, .L987
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L988:
	.align	2
.L987:
	.word	.LANCHOR0+3384
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	ldr	r5, .L990
	muls	r4, r0, r4
	ldr	r3, [r5, #2492]
	addw	r0, r5, #3384
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
.L991:
	.align	2
.L990:
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r4, .L1005
	mov	r5, r1
	mov	r7, r2
	ldr	r3, [r4, #2504]
	ldrh	r10, [r4, #108]
	cmp	r0, r3
	bcc	.L993
	movw	r2, #811
	ldr	r1, .L1005+4
	ldr	r0, .L1005+8
	bl	sftl_printk
.L993:
	ldr	r3, [r4, #2504]
	cmp	r9, r3
	bcs	.L994
	add	r10, r10, #7
	ldrh	r2, [r4, #138]
	lsr	r6, r9, r10
	ldr	r1, [r4, #2492]
	movs	r3, #0
	uxth	r6, r6
	mov	fp, #12
.L995:
	uxth	r8, r3
	cmp	r8, r2
	bcc	.L1000
	bl	select_l2p_ram_region
	mul	fp, fp, r0
	ldr	r3, [r4, #2492]
	mov	r8, r0
	ldrh	r1, [r3, fp]
	add	r2, r3, fp
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1001
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1001
	bl	flush_l2p_region
.L1001:
	mov	r1, r8
	mov	r0, r6
	bl	load_l2p_region
	b	.L997
.L994:
	mov	r0, #-1
	cbnz	r7, .L992
	str	r0, [r5]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1000:
	adds	r3, r3, #1
	mla	r0, fp, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	bne	.L995
.L997:
	movs	r0, #1
	movs	r3, #12
	lsl	r0, r0, r10
	subs	r0, r0, #1
	and	r0, r0, r9
	uxth	r0, r0
	cbnz	r7, .L998
	ldr	r2, [r4, #2492]
	mla	r3, r3, r8, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r0, lsl #2]
	str	r3, [r5]
.L999:
	ldr	r2, [r4, #2492]
	movs	r3, #12
	mla	r8, r3, r8, r2
	ldr	r3, [r8, #4]
	adds	r2, r3, #1
	beq	.L1003
	adds	r3, r3, #1
	str	r3, [r8, #4]
.L1003:
	movs	r0, #0
.L992:
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L998:
	mul	r3, r3, r8
	ldr	r2, [r4, #2492]
	ldr	r1, [r5]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r0, lsl #2]
	ldr	r2, [r4, #2492]
	strh	r6, [r4, #2496]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L999
.L1006:
	.align	2
.L1005:
	.word	.LANCHOR0
	.word	.LANCHOR1+364
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
	ldr	r5, .L1017
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #2324]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L1008
	ldr	r4, [r5, #2340]
	cbz	r4, .L1009
	ldr	r1, [r5, #2316]
	mov	ip, #6
	ldr	r2, .L1017+4
	movw	lr, #65535
	ldrh	r0, [r5, #2344]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L1010:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L1009
	cmp	r4, r7
	bne	.L1011
	mov	r1, r4
	ldr	r0, .L1017+8
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L1012
	movw	r2, #1699
	ldr	r1, .L1017+12
	ldr	r0, .L1017+16
	bl	sftl_printk
.L1012:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #2324]
	ldrh	r3, [r2, r7, lsl #1]
.L1008:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L1009
.L1011:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L1010
.L1009:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1018:
	.align	2
.L1017:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2340
	.word	.LANCHOR1+373
	.word	.LC1
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r6, .L1043
	ldr	r7, .L1043+4
	ldr	r1, .L1043+8
	mov	r5, r6
	ldr	r0, .L1043+12
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L1043+4
	bl	__memzero
.L1020:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L1022
	ldr	r8, .L1043+4
	movs	r4, #0
	ldr	r10, .L1043+24
	mov	r7, r4
	movw	r9, #65535
.L1023:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1025
	ldr	r4, [r5, #2340]
	cbz	r4, .L1026
	ldr	r3, [r5, #2316]
	movs	r6, #0
	ldrh	r8, [r5, #2344]
	mov	fp, #6
	ldr	r9, .L1043+4
	subs	r4, r4, r3
	ldr	r3, .L1043+16
	asrs	r4, r4, #1
	ldr	r10, .L1043+28
	muls	r4, r3, r4
	uxth	r4, r4
.L1027:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L1026
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L1028
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1028:
	mul	r4, fp, r4
	ldr	r3, [r5, #2316]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1027
.L1026:
	cbz	r7, .L1019
	movw	r2, #2343
	ldr	r1, .L1043+8
	ldr	r0, .L1043+20
	bl	sftl_printk
.L1019:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1022:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1021
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L1021:
	adds	r4, r4, #1
	b	.L1020
.L1025:
	ldr	r3, [r5, #2324]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L1024
	mov	r1, r6
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L1024
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L1024:
	adds	r4, r4, #1
	b	.L1023
.L1044:
	.align	2
.L1043:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+389
	.word	.LC95
	.word	-1431655765
	.word	.LC1
	.word	.LC96
	.word	.LC97
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
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
	ldr	r6, .L1059
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L1059+16
	mov	r4, r6
	ldr	r0, .L1059+4
	bl	sftl_printk
.L1046:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L1052
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1052:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L1047
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L1047:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L1049
	str	r3, [r4, #3448]
	movs	r2, #0
	ldr	r3, [r4, #3292]
	movs	r1, #1
	ldr	r7, [r4, #3324]
	ldr	r0, .L1059+8
	str	r3, [r4, #3452]
	str	r5, [r4, #3460]
	str	r7, [r4, #3456]
	str	r2, [r4, #3444]
	bl	FlashReadPages
	ldr	r3, [r4, #3444]
	cmp	r3, #256
	beq	.L1050
	adds	r3, r3, #1
	beq	.L1050
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1049
.L1050:
	ldr	r2, [r4, #3452]
	ldr	r3, [r4, #3456]
	ldr	r0, .L1059+12
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
	ldr	r2, [r4, #3448]
	bl	sftl_printk
.L1049:
	adds	r5, r5, #1
	b	.L1046
.L1060:
	.align	2
.L1059:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LANCHOR0+3444
	.word	.LC100
	.word	.LC99
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	1
	.global	FtlGcScanTempBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1093
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	mov	r4, r0
	str	r1, [sp, #12]
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1087
	cbnz	r6, .L1062
.L1063:
	bl	FtlGcPageVarInit
	b	.L1064
.L1087:
	movs	r6, #0
.L1062:
	ldr	r3, .L1093+4
	ldr	r2, [sp, #12]
	ldrh	r3, [r3, #102]
	cmp	r3, r2
	beq	.L1063
.L1064:
	movw	r9, #65535
	movs	r3, #0
	str	r3, [sp, #4]
.L1065:
	movs	r3, #0
	strb	r3, [r4, #8]
	ldrh	r3, [r4]
	cmp	r3, r9
	beq	.L1066
	ldr	r5, .L1093+4
.L1084:
	ldr	r3, [r5, #3280]
	movs	r2, #0
	ldrh	ip, [r5, #32]
	add	lr, r4, #16
	ldrh	fp, [r5, #110]
	mov	r7, r2
	str	r3, [sp, #8]
	mov	r8, #20
	ldr	r3, [r5, #3172]
	ldr	r10, [r5, #3176]
	str	r3, [sp, #16]
	ldrh	r3, [r5, #112]
	str	r3, [sp, #20]
.L1067:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1071
	mov	fp, #0
	movs	r2, #0
	mov	r1, r7
	ldr	r0, [sp, #8]
	bl	FlashReadPages
.L1072:
	uxth	r3, fp
	cmp	r7, r3
	bhi	.L1082
	ldr	r3, [sp, #4]
	adds	r6, r6, #1
	uxth	r6, r6
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bls	.L1083
.L1085:
	ldrh	r3, [r5, #102]
	cmp	r3, r6
	bhi	.L1084
.L1066:
	ldr	r3, .L1093
	movw	r2, #65535
	strh	r6, [r4, #2]	@ movhi
	mov	r1, r6
	mov	r0, r4
	strh	r2, [r3, #4]	@ movhi
	movs	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1086
.L1071:
	ldrh	r3, [lr], #2
	cmp	r3, r9
	beq	.L1068
	ldr	r1, [sp, #8]
	orr	r3, r6, r3, lsl #10
	mla	r1, r8, r7, r1
	str	r3, [r1, #4]
	mov	r3, fp
	muls	r3, r7, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r0, r3
	ldr	r3, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #20]
	muls	r3, r7, r3
	add	r7, r7, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r7, r7
	bic	r3, r3, #3
	str	r0, [r1, #8]
	add	r3, r3, r10
	str	r3, [r1, #12]
.L1068:
	adds	r2, r2, #1
	b	.L1067
.L1082:
	movs	r3, #20
	ldr	r1, [r5, #3280]
	mul	r3, r3, fp
	adds	r2, r1, r3
	ldr	r8, [r1, r3]
	ldr	r0, [r2, #4]
	str	r0, [sp, #8]
	cmp	r8, #0
	bne	.L1073
	ldr	r10, [r2, #12]
	ldrh	r2, [r10]
	cmp	r2, r9
	bne	.L1074
	ldrh	r2, [r4]
	ldr	r3, [r5, #2324]
	strh	r8, [r3, r2, lsl #1]	@ movhi
.L1092:
	ldrh	r0, [r4]
	movs	r6, #0
	bl	INSERT_FREE_LIST
	strh	r9, [r4]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1065
.L1074:
	mov	r2, r8
	add	r1, sp, #24
	ldr	r0, [r10, #8]
	str	r3, [sp, #16]
	bl	log2phys
	ldr	r2, [r10, #12]
	ldr	r1, [sp, #24]
	ldr	r3, [sp, #16]
	cmp	r2, r1
	beq	.L1076
.L1080:
	ldr	r2, [r10, #8]
	add	fp, fp, #1
	ldr	r1, [sp, #8]
	ldr	r0, [r10, #12]
	bl	FtlGcUpdatePage
	b	.L1072
.L1076:
	str	r2, [sp, #32]
	movs	r1, #1
	ldr	r2, [r5, #3308]
	add	r0, sp, #28
	str	r3, [sp, #16]
	str	r2, [sp, #36]
	ldr	r2, [r5, #3328]
	str	r2, [sp, #40]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r5, #58]
	ldr	r1, [r5, #3280]
	ldr	r3, [sp, #16]
	ldr	r0, [sp, #36]
	lsls	r2, r2, #7
	add	ip, r3, r1
.L1077:
	cmp	r8, r2
	beq	.L1080
	ldr	r1, [ip, #8]
	ldr	r3, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r3
	beq	.L1078
	ldr	r2, [sp, #32]
	ldrh	r1, [r4]
	ldr	r0, .L1093+8
	bl	sftl_printk
.L1073:
	ldrh	r2, [r4]
	movs	r1, #0
	ldr	r3, [r5, #2324]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	b	.L1092
.L1078:
	add	r8, r8, #1
	b	.L1077
.L1083:
	ldr	r2, .L1093
	ldrh	r3, [r2, #4]
	cmp	r3, r9
	beq	.L1085
	ldr	r1, [sp, #4]
	add	r3, r3, r1
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r6
	bls	.L1085
.L1086:
	mov	r0, #-1
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1094:
	.align	2
.L1093:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC101
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldr	r3, .L1108
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #88
	sub	sp, sp, #88
	mov	r4, r3
	ldr	r0, [r3, #2716]
	cmp	r0, #0
	beq	.L1096
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L1097
	mov	r5, #2048
.L1102:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L1099
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r4, #2720]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L1100
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, [r4, #3316]
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
	bne	.L1099
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1099:
	mov	r0, #-1
.L1095:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1100:
	subs	r5, r5, #1
	bne	.L1102
	b	.L1099
.L1097:
	ldr	r2, [r3, #2508]
	movs	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L1095
.L1096:
	ldr	r1, [r3, #2508]
	ldr	r6, [r3, #2712]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L1105
	ldr	r2, [r3, #2564]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
	add	r2, r2, r6
	cmp	r1, r2
	bhi	.L1105
	ldrb	r3, [r3, #2296]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1095
.L1105:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L1095
.L1109:
	.align	2
.L1108:
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	cbz	r3, .L1110
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L1114
	ldr	r2, [r0, #24]
	str	r3, [r0, #36]
	subs	r5, r5, #1
	ldr	r0, [r4, #3296]
	uxth	r5, r5
	ldr	r1, [r4, #3324]
	str	r0, [r4, #3452]
	str	r1, [r4, #3456]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r4, #3448]
	cbz	r2, .L1112
	movs	r2, #1
	addw	r0, r4, #3444
	mov	r1, r2
	bl	FlashReadPages
.L1113:
	ldr	r2, [r4, #3452]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1112:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L1113
.L1110:
	pop	{r4, r5, r6, pc}
.L1115:
	.align	2
.L1114:
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
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
	bls	.L1118
	ldr	r3, .L1119
	add	r0, sp, #24
	movs	r2, #32
	movs	r1, #165
	lsls	r4, r4, #10
	str	r0, [sp, #16]
	ldr	r5, [r3, #3316]
	str	r5, [sp, #12]
	bl	memset
	mov	r3, #1515870810
	movs	r2, #1
	str	r3, [r5]	@ unaligned
	mov	r1, r2
	str	r3, [r5, #4]	@ unaligned
	add	r0, sp, #4
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
.L1116:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L1118:
	movs	r4, #0
	b	.L1116
.L1120:
	.align	2
.L1119:
	.word	.LANCHOR0
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
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
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movs	r5, #0
	ldr	r4, .L1130
	movs	r1, #0
	ldr	r0, [r4, #3292]
	add	r6, r4, #176
	ldr	r3, [r4, #3324]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3452]
	str	r3, [r4, #3456]
	bl	ftl_memset
.L1122:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1123
	ldr	r6, [r4, #3456]
	movs	r2, #16
	movs	r1, #255
	ldr	r8, .L1130+12
	ldr	r9, .L1130+16
	movs	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	memset
	movw	r3, #61649
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
.L1124:
	ldr	r3, [r4, #3292]
	mov	r10, #0
	ldrh	r2, [r4, #154]
	ldrh	r1, [r4, #152]
	str	r3, [r4, #3452]
	ldr	r3, [r4, #3324]
	str	r10, [r4, #3444]
	str	r3, [r4, #3456]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3448]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r8
	bl	sftl_printk
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1125
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3260]
	adds	r3, r3, #1
	strh	r10, [r4, #154]	@ movhi
	str	r3, [r4, #160]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #156]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #156]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #152]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3448]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L1125:
	movs	r3, #1
	mov	r0, r9
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #154]
	adds	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	bne	.L1126
	adds	r5, r5, #1
	ldr	r1, [r4, #3448]
	uxth	r5, r5
	ldr	r0, .L1130+4
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1124
	mov	r2, r5
	ldr	r1, [r4, #3448]
	ldr	r0, .L1130+8
	bl	sftl_printk
.L1128:
	b	.L1128
.L1123:
	ldrh	r2, [r4, #3380]
	ldr	r3, [r4, #3452]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1122
.L1129:
	movs	r7, #1
	b	.L1124
.L1126:
	cmp	r7, #0
	beq	.L1129
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1131:
	.align	2
.L1130:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LC104
	.word	.LC102
	.word	.LANCHOR0+3444
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
	ldr	r4, .L1168
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L1168+20
.L1133:
	ldr	r3, .L1168
	ldr	r2, .L1168
	ldrh	r3, [r3, #2344]
	ldrh	r2, [r2, #2336]
	add	r3, r3, r2
	ldr	r2, .L1168
	ldrh	r2, [r2, #40]
	cmp	r3, r2
	ble	.L1134
	movw	r2, #2615
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1134:
	ldr	r3, .L1168+8
	cmp	r5, r3
	bne	.L1160
	ldrh	r3, [r4, #2344]
	ldr	r1, [r4, #3160]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L1135
	subs	r1, r1, #1
	uxth	r1, r1
.L1135:
	ldr	r0, .L1168+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #2344]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1136
	mov	r2, #2624
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1136:
	ldrh	r3, [r4, #2344]
	subs	r3, r3, #1
	strh	r3, [r4, #2344]	@ movhi
	ldrh	r3, [r4, #40]
	cmp	r3, r8
	bls	.L1133
	ldr	r3, [r4, #2324]
	uxth	r6, r6
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L1133
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1138
	ldr	r3, [r4, #2324]
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1133
	movw	r2, #2638
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
	b	.L1133
.L1160:
	movs	r1, #0
	b	.L1135
.L1138:
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1140
	movw	r2, #2641
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1140:
	ldr	r1, [r4, #3260]
	mov	r10, #20
	ldrh	r2, [r4, #32]
	add	fp, r5, #16
	mov	r0, fp
	mov	ip, #0
	mov	r3, r1
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L1141:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1143
	cbnz	r7, .L1144
	movw	r2, #2652
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1144:
	ldrh	r3, [r4, #2588]
	cmp	r3, r8
	bne	.L1145
	movw	r2, #2654
	mov	r1, r9
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1145:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2328]
	cmp	r3, #0
	bne	.L1146
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1147
	ldrh	r1, [r4, #92]
	add	r3, r3, r1
.L1167:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2548]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2548]
	bl	ftl_set_blk_mode
.L1149:
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
	ldr	r2, [r4, #3340]
	ldr	r1, [r4, #3260]
	str	r0, [r4, #2556]
	ldr	r3, [r2, #16]
	ldr	r0, .L1168+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1151:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L1152
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3260]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L1153:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1155
	cmp	r3, #0
	ble	.L1156
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1156:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1157
	ldr	r3, [r4, #2324]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L1133
.L1143:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1142
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L1142:
	adds	r3, r3, #20
	b	.L1141
.L1147:
	movs	r3, #2
	b	.L1167
.L1146:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2552]
	adds	r3, r3, #1
	str	r3, [r4, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1149
.L1152:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1151
.L1155:
	mul	r2, r1, r10
	ldr	r0, [r4, #3260]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1154
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
.L1154:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1153
.L1157:
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
	cbz	r3, .L1158
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1159
.L1158:
	movw	r2, #2707
	ldr	r1, .L1168+20
	ldr	r0, .L1168+4
	bl	sftl_printk
.L1159:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1169:
	.align	2
.L1168:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2340
	.word	-1024
	.word	.LANCHOR1+403
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
	ldr	r4, .L1182
	ldrh	r3, [r4, #3206]
	cbz	r3, .L1171
	movs	r7, #0
	addw	r10, r4, #3208
.L1172:
	ldrh	r2, [r4, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1178
	bl	FtlGcReFreshBadBlk
.L1171:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1178:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1173:
	ldrh	r3, [r4, #3206]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1177
	adds	r7, r7, #1
	b	.L1172
.L1177:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3208]
	cmp	r3, r9
	bne	.L1174
	mov	r1, r9
	ldr	r0, .L1182+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3206]
	add	r3, r10, r6, lsl #1
.L1175:
	cmp	r5, r1
	bcc	.L1176
	subs	r1, r1, #1
	strh	r1, [r4, #3206]	@ movhi
.L1174:
	add	fp, fp, #1
	b	.L1173
.L1176:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L1175
.L1183:
	.align	2
.L1182:
	.word	.LANCHOR0
	.word	.LC105
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
	ldr	r2, .L1194
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	mov	r4, r2
	ldr	r3, [r2, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1185
	ldrh	r1, [r2, #2588]
	cmp	r1, r0
	bne	.L1186
	movw	r3, #65535
	strh	r3, [r2, #2588]	@ movhi
.L1187:
	mov	r1, r5
	ldr	r0, .L1194+4
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L1189
	movw	r2, #2777
	ldr	r1, .L1194+8
	ldr	r0, .L1194+12
	bl	sftl_printk
.L1189:
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
	ble	.L1193
	movw	r2, #2780
	ldr	r1, .L1194+8
	ldr	r0, .L1194+12
	bl	sftl_printk
.L1193:
	movs	r3, #1
	b	.L1184
.L1186:
	ldrh	r1, [r2, #2348]
	cmp	r1, r0
	beq	.L1184
	ldrh	r1, [r2, #2396]
	cmp	r1, r0
	beq	.L1184
	ldrh	r2, [r2, #2444]
	cmp	r2, r0
	bne	.L1187
.L1184:
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.L1185:
	bl	List_update_data_list
	movs	r3, #0
	b	.L1184
.L1195:
	.align	2
.L1194:
	.word	.LANCHOR0
	.word	.LANCHOR0+2320
	.word	.LANCHOR1+428
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
	ldr	r5, .L1205
	beq	.L1197
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1198
	mov	r1, r0
	ldr	r0, .L1205+4
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L1199
.L1203:
	movs	r6, #0
.L1196:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1199:
	movw	r2, #2795
.L1204:
	ldr	r1, .L1205+8
	ldr	r0, .L1205+12
	bl	sftl_printk
	b	.L1196
.L1198:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1197:
	ldrh	r0, [r5, #3430]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1201
	strh	r4, [r5, #3430]	@ movhi
	b	.L1203
.L1201:
	cmp	r4, r0
	beq	.L1203
	bl	update_vpc_list
	ldr	r2, [r5, #2316]
	adds	r6, r0, #0
	ldr	r3, [r5, #2320]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #3430]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1205+16
	muls	r3, r2, r3
	ldr	r2, [r5, #2324]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L1196
	cmp	r4, r1
	beq	.L1196
	movw	r2, #2811
	b	.L1204
.L1206:
	.align	2
.L1205:
	.word	.LANCHOR0
	.word	.LC106
	.word	.LANCHOR1+444
	.word	.LC1
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	beq	.L1331
	ldrh	r3, [r0, #2]
	ldr	r4, .L1338
	str	r3, [sp, #4]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #102]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1210
	strh	r3, [r0, #4]	@ movhi
.L1336:
	strb	r3, [fp, #6]
.L1331:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1210:
	ldrh	r0, [r0, #16]
.L1211:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1212
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r5, r0
	beq	.L1213
	ldr	r3, [r4, #3172]
	movs	r2, #0
	ldrh	lr, [r4, #32]
	movw	r9, #65535
	ldr	r0, [r4, #3280]
	mov	r10, #20
	str	r3, [sp]
	ldrh	r3, [r4, #110]
	ldr	r8, [r4, #3176]
	ldrh	r7, [r4, #112]
	mov	r4, r2
	str	r3, [sp, #8]
	add	r3, fp, #16
	mov	ip, r3
	str	r3, [sp, #16]
.L1214:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1220
	ldr	r6, .L1338
	movs	r2, #0
	mov	r1, r4
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r6, #2544]
	uxth	r2, r5
	ldr	r8, [r6, #3280]
	str	r2, [sp, #8]
	subs	r3, r3, #1
	str	r3, [sp]
	mov	r7, r8
	movs	r3, #0
.L1221:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1226
	bne	.L1224
	adds	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L1332:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #102]
	ldr	r2, [sp, #12]
	str	r0, [sp, #8]
	cmp	r3, r2
	bne	.L1228
	ldrh	r3, [sp, #12]
	strh	r3, [fp, #2]	@ movhi
	movs	r3, #0
	strb	r3, [fp, #6]
	strh	r3, [fp, #4]	@ movhi
.L1228:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L1229
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #20]
	cmp	r3, r2
	bne	.L1229
	ldr	r1, [sp, #12]
	mov	r2, r3
.L1337:
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L1331
.L1212:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1211
.L1213:
	ldr	r3, [sp, #4]
	cbz	r3, .L1215
	movw	r2, #1766
	ldr	r1, .L1338+4
	ldr	r0, .L1338+8
	bl	sftl_printk
.L1215:
	ldr	r3, [sp, #20]
	cbz	r3, .L1216
	cmp	r6, r3
	beq	.L1216
	movw	r2, #1767
	ldr	r1, .L1338+4
	ldr	r0, .L1338+8
	bl	sftl_printk
.L1216:
	movs	r3, #0
	strh	r3, [fp, #2]	@ movhi
	b	.L1336
.L1220:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1217
	mla	r1, r10, r4, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #8]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r6, r3
	ldr	r3, [sp]
	add	r6, r6, r3
	mov	r3, r7
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r4, r4
	bic	r3, r3, #3
	str	r6, [r1, #8]
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1217:
	adds	r2, r2, #1
	b	.L1214
.L1226:
	ldr	r2, [r7]
	cbnz	r2, .L1222
	ldr	r10, [r7, #12]
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1223
	ldr	r1, [r6, #2544]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1223
	adds	r2, r2, #1
	str	r2, [r6, #2544]
.L1223:
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1225
.L1224:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [sp, #12]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1332
.L1222:
	ldr	r9, [sp, #8]
.L1225:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1221
.L1229:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1230
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1231
.L1230:
	ldr	r3, [r6, #3440]
	uxth	r7, r5
	uxth	r5, r5
	movw	r8, #65535
	adds	r3, r3, #1
	itt	eq
	ldreq	r3, [sp]
	streq	r3, [r6, #3440]
	ldr	r3, [sp, #4]
	ldr	r6, .L1338
	adds	r3, r3, #7
	cmp	r5, r3
	itet	gt
	subgt	r4, r7, #7
	ldrle	r4, [sp, #4]
	uxthgt	r4, r4
.L1234:
	cmp	r4, r7
	bhi	.L1241
	movs	r3, #0
	ldrh	lr, [r6, #32]
	ldr	r0, [r6, #3280]
	mov	r5, r3
	ldr	r1, [sp, #16]
	mov	ip, #20
	b	.L1242
.L1236:
	ldrh	r2, [r1], #2
	cmp	r2, r8
	beq	.L1235
	mla	r9, ip, r5, r0
	adds	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r9, #4]
.L1235:
	adds	r3, r3, #1
.L1242:
	uxth	r2, r3
	cmp	r2, lr
	bcc	.L1236
	movs	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, [r6, #3280]
	movs	r2, #20
	mla	r5, r2, r5, r3
.L1237:
	cmp	r5, r3
	bne	.L1240
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1234
.L1240:
	ldr	r2, [r3]
	cbnz	r2, .L1231
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r8
	beq	.L1239
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r6, #3440]
.L1239:
	adds	r3, r3, #20
	b	.L1237
.L1241:
	mov	r3, #-1
	str	r3, [r6, #3440]
.L1231:
	ldr	r10, .L1338
	movs	r3, #1
	ldr	r5, [sp, #4]
	mov	r4, r10
	strh	r3, [r10, #3512]	@ movhi
.L1243:
	ldrh	r6, [r4, #32]
	movs	r3, #0
	ldr	r0, [r4, #3280]
	mov	r9, r3
	ldr	r1, [sp, #16]
	movw	r7, #65535
	mov	ip, #20
.L1244:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1246
	movs	r2, #0
	mov	r1, r9
	bl	FlashReadPages
	movs	r3, #0
.L1335:
	str	r3, [sp, #24]
	ldrh	r3, [sp, #24]
	cmp	r9, r3
	bhi	.L1275
	adds	r5, r5, #1
	ldrh	r3, [r4, #102]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1243
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
.L1276:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1331
	ldr	r1, [sp, #16]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #16]
	add	r1, r3, #1
	bne	.L1336
	mov	r3, r1
	b	.L1276
.L1246:
	ldrh	r2, [r1], #2
	cmp	r2, r7
	beq	.L1245
	mla	lr, ip, r9, r0
	orr	r2, r5, r2, lsl #10
	str	r2, [lr, #4]
	add	r2, r9, #1
	uxth	r9, r2
.L1245:
	adds	r3, r3, #1
	b	.L1244
.L1339:
	.align	2
.L1338:
	.word	.LANCHOR0
	.word	.LANCHOR1+464
	.word	.LC1
.L1275:
	ldr	r3, [sp, #24]
	movs	r7, #20
	muls	r7, r3, r7
	ldr	r3, [r4, #3280]
	str	r3, [sp, #28]
	add	r8, r3, r7
	ldr	r6, [r8, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp, #4]
	cmp	r5, r3
	bcc	.L1248
	ldr	r3, [sp, #28]
	bne	.L1249
	ldr	r2, [sp, #20]
	cmp	r2, r0
	bhi	.L1248
.L1249:
	ldr	r2, [sp, #12]
	cmp	r5, r2
	bne	.L1250
	ldr	r2, [sp, #8]
	cmp	r2, r0
	beq	.L1251
.L1250:
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1252
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1253
	ldrh	r0, [fp]
.L1333:
	bl	decrement_vpc_count
	b	.L1248
.L1253:
	ldr	r3, [r8, #4]
	adds	r6, r3, #1
	str	r3, [sp]
	beq	.L1254
	ldr	r1, [r4, #2544]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L1254
	ldr	r3, [sp]
	adds	r3, r3, #1
	str	r3, [r4, #2544]
.L1254:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1255
	movw	r2, #1918
	ldr	r1, .L1340
	ldr	r0, .L1340+4
	bl	sftl_printk
.L1255:
	ldr	r6, [r8, #8]
	add	r1, sp, #40
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #3440]
	adds	r0, r1, #1
	beq	.L1256
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1256
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1257
	ldr	r0, [r4, #3280]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r7
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3280]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1258
.L1259:
	mov	r3, #-1
	str	r3, [sp, #36]
.L1266:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1248
.L1279:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1271
	movw	r2, #2019
	ldr	r1, .L1340
	ldr	r0, .L1340+4
	bl	sftl_printk
.L1271:
	ldr	r3, [r10, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1272
	mov	r0, r6
	b	.L1333
.L1257:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1248
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
.L1248:
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	b	.L1335
.L1258:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1259
	ldr	r1, [r8, #4]
	ldr	r0, [r4, #3440]
	str	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1259
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1261
.L1264:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1259
.L1261:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1259
	adds	r0, r1, #1
	beq	.L1262
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1263:
	ldr	r3, [r4, #3280]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1264
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #3440]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1264
	mov	r1, r3
	ldr	r0, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1259
	b	.L1264
.L1262:
	str	r1, [r2, r7]
	b	.L1263
.L1256:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1266
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1266
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1269
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L1270
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L1270
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	bne	.L1266
.L1270:
	ldr	r0, [r10, #3280]
	movs	r2, #0
	movs	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r10, #3280]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1266
	ldr	r1, [r7, #4]
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1266
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1266
.L1272:
	mov	r1, r6
	ldr	r0, .L1340+8
	bl	sftl_printk
	b	.L1248
.L1252:
	ldr	r3, [r4, #3516]
	cmp	r3, #31
	itttt	ls
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3516]
	strls	r6, [r2, #3520]
	ldrh	r0, [fp]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3440]
	adds	r2, r3, #1
	bne	.L1274
	ldr	r3, [sp]
.L1334:
	str	r3, [r4, #3440]
	b	.L1248
.L1274:
	ldr	r2, [sp]
	cmp	r2, r3
	bcs	.L1248
	mov	r3, r2
	b	.L1334
.L1251:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [fp, #2]	@ movhi
	ldr	r2, [sp, #8]
	strb	r3, [fp, #6]
	b	.L1337
.L1269:
	mov	r0, r8
	b	.L1279
.L1341:
	.align	2
.L1340:
	.word	.LANCHOR1+464
	.word	.LC1
	.word	.LC107
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	ldr	r4, .L1360
	ldrh	r2, [r4, #2352]
	cmp	r2, #0
	beq	.L1343
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1343
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1343
	ldrb	r7, [r4, #2358]	@ zero_extendqisi2
	cbnz	r7, .L1342
	ldr	r6, [r4, #2504]
	mov	r2, r7
	mov	r1, sp
	ldrh	r8, [r4, #32]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #3324]
	ldr	r0, [r4, #3292]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r7, [r5, #4]
	beq	.L1345
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1346:
	ldr	r9, .L1360+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1347:
	cmp	r8, r7
	bne	.L1351
.L1348:
	movs	r3, #1
.L1359:
	strb	r3, [r4, #2358]
.L1342:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1345:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L1346
.L1351:
	ldrh	r3, [r4, #2352]
	cmp	r3, #0
	beq	.L1348
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
	b	.L1347
.L1343:
	movs	r3, #0
	b	.L1359
.L1361:
	.align	2
.L1360:
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
	ldr	r5, .L1366
	movs	r6, #12
	bl	FtlWriteDump_data
.L1363:
	ldrh	r3, [r5, #138]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1365
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1365:
	ldr	r2, [r5, #2492]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1364
	bl	flush_l2p_region
.L1364:
	adds	r4, r4, #1
	b	.L1363
.L1367:
	.align	2
.L1366:
	.word	.LANCHOR0
	.fnend
	.size	l2p_flush, .-l2p_flush
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
	ldr	r5, .L1381
	mov	r3, #-1
	movs	r7, #0
	movw	r1, #61589
	str	r3, [sp, #20]
	mov	r4, r0
	ldr	r2, [r5, #3292]
	ldr	r6, [r5, #3324]
	str	r2, [sp, #12]
	mvn	r2, #2
	str	r6, [sp, #16]
	str	r2, [r6, #8]
	mvn	r2, #1
	str	r2, [r6, #12]
	ldrh	r2, [r0]
	strh	r7, [r6]	@ movhi
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, [r5, #3292]
	str	r1, [r2]
	add	r1, r1, #304087040
	ldr	r2, [r5, #3292]
	add	r1, r1, #1269760
	addw	r1, r1, #1507
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	ite	eq
	moveq	r8, #6
	movne	r8, #7
.L1374:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1370
.L1371:
	ldr	r2, [r5, #2324]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #102]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1370:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L1371
	ldr	r3, [r5, #2544]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r7
	str	r3, [r5, #2544]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r8, r8, #1
	bne	.L1374
	b	.L1371
.L1382:
	.align	2
.L1381:
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	ldr	r4, .L1395
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1391
	ldrh	r7, [r4, #108]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1385:
	cbnz	r6, .L1390
.L1383:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1390:
	ldr	r3, [r4, #3364]
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
	cbz	r2, .L1387
	cmp	r5, r3
	beq	.L1387
	ldr	r3, [r4, #3300]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1388:
	lsls	r3, r5, #9
	ldr	r0, [r4, #3300]
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
	ldr	r2, [r4, #3300]
	ldr	r0, .L1395+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1385
.L1387:
	ldrh	r2, [r4, #110]
	movs	r1, #0
	ldr	r0, [r4, #3300]
	bl	ftl_memset
	b	.L1388
.L1391:
	mov	r9, #-1
	b	.L1383
.L1396:
	.align	2
.L1395:
	.word	.LANCHOR0
	.word	.LANCHOR0+3468
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
	ldr	r2, .L1399
	ldr	r3, .L1399+4
	ldr	r1, [r2, #2636]
	cmp	r1, r3
	bne	.L1397
	ldr	r3, .L1399+8
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
.L1397:
	bx	lr
.L1400:
	.align	2
.L1399:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
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
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L1406
	ldrh	r2, [r3, #3648]
	cmp	r2, #31
	itett	ls
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, #3648]	@ movhi
	movls	r2, #1
	cbnz	r0, .L1403
	ldr	r1, [r3, #3340]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1404
.L1403:
	ldr	r2, [r3, #3340]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1406+4
	str	r1, [r2]
	ldrh	r1, [r3, #3332]
	ldr	r2, [r3, #3340]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1404:
	movs	r0, #0
	pop	{r3, pc}
.L1407:
	.align	2
.L1406:
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
	ldr	r4, .L1419
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1418
	ldrh	r6, [r4, #108]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1411:
	cbnz	r7, .L1417
.L1409:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1417:
	ldr	r3, [r4, #3364]
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
	cbz	r3, .L1413
	ldr	r2, [r4, #3300]
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
	ldr	r2, [r4, #3444]
	it	eq
	moveq	r9, #-1
	cmp	r2, #256
	bne	.L1415
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1419+4
	bl	sftl_printk
	ldr	r2, [r4, #3300]
	mov	r1, r6
	ldr	r0, .L1419+8
	bl	FtlMapWritePage
.L1415:
	ldr	r1, [r4, #3300]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1416:
	ldr	r3, [sp, #8]
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L1411
.L1413:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1416
.L1418:
	mov	r9, #-1
	b	.L1409
.L1420:
	.align	2
.L1419:
	.word	.LANCHOR0
	.word	.LC108
	.word	.LANCHOR0+3468
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
	ldr	r4, .L1423
	ldr	r2, [r4, #3340]
	ldrh	r1, [r4, #3332]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3340]
	ldr	r2, [r3]
	ldr	r3, .L1423+4
	cmp	r2, r3
	beq	.L1422
	ldr	r1, .L1423+8
	ldr	r0, .L1423+12
	bl	sftl_printk
	ldrh	r2, [r4, #3332]
	movs	r1, #0
	ldr	r0, [r4, #3340]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1422:
	movs	r0, #0
	pop	{r4, pc}
.L1424:
	.align	2
.L1423:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC109
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
	ldr	r4, .L1428
	movs	r0, #0
	ldr	r5, .L1428+4
	addw	r2, r4, #2636
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1426
	mov	r1, #512
	addw	r0, r4, #2636
	bl	__memzero
	str	r5, [r4, #2636]
.L1426:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1427
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
.L1427:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	pop	{r3, r4, r5, pc}
.L1429:
	.align	2
.L1428:
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
	ldr	r4, .L1446
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1446+16
	ldr	r3, [r4, #3292]
	addw	r8, r4, #3444
	ldr	r6, [r4, #3324]
	ldr	r10, .L1446+20
	str	r3, [r4, #3452]
	ldrh	r3, [r4, #2576]
	str	r6, [r4, #3456]
	str	r5, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2584]
	str	r5, [r6, #8]
	ldrh	r2, [r4, #2350]
	str	r3, [r6, #4]
	ldr	r3, .L1446+4
	str	r3, [r4, #2268]
	ldr	r3, .L1446+8
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
	ldr	r0, [r4, #3452]
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
	ldr	r0, [r4, #3452]
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3452]
	ldr	r1, [r4, #2324]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3452]
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
.L1432:
	ldr	r3, [r4, #3292]
	ldrh	r1, [r4, #2578]
	ldrh	r2, [r4, #2576]
	str	r3, [r4, #3452]
	ldr	r3, [r4, #3324]
	str	r3, [r4, #3456]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3448]
	ldrh	r3, [r4, #104]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L1433
	movs	r3, #0
	ldrh	r7, [r4, #2580]
	strh	r3, [r4, #2578]	@ movhi
	strh	r2, [r4, #2580]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2540]
	strh	r0, [r4, #2576]	@ movhi
	adds	r2, r3, #1
	str	r3, [r4, #2584]
	str	r2, [r4, #2540]
	lsls	r2, r0, #10
	str	r2, [r4, #3448]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L1433:
	movs	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2578]
	ldr	r2, [r4, #3444]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r1, r2, #1
	strh	r3, [r4, #2578]	@ movhi
	bne	.L1434
	cmp	r3, #1
	bne	.L1435
	movw	r2, #1135
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1435:
	ldrh	r3, [r4, #2578]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #104]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2578]	@ movhi
	cmp	r5, #3
	bls	.L1432
	mov	r2, r5
	ldr	r1, [r4, #3448]
	ldr	r0, .L1446+12
	bl	sftl_printk
.L1438:
	b	.L1438
.L1434:
	cmp	r3, #1
	beq	.L1432
	cmp	r2, #256
	beq	.L1432
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1439
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1439:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1447:
	.align	2
.L1446:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
	.word	.LC110
	.word	.LANCHOR1+486
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
	ldr	r3, .L1451
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1450
	bl	FtlSysFlush
.L1450:
	movs	r0, #0
	pop	{r3, pc}
.L1452:
	.align	2
.L1451:
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
	ldr	r4, .L1468
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1461
	cmp	r1, #31
	bhi	.L1455
.L1460:
	movs	r0, #0
.L1453:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1455:
	ldrh	r5, [r4, #58]
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1456
	subs	r5, r5, r3
	adds	r7, r7, #1
	cmp	r5, r6
	it	cs
	movcs	r5, r6
	uxth	r5, r5
	subs	r6, r6, r5
.L1456:
	mov	r3, #-1
	str	r3, [sp, #4]
.L1457:
	ldrh	r3, [r4, #58]
	cmp	r6, r3
	bcs	.L1459
	ldr	r3, [r4, #3652]
	cmp	r3, #32
	bls	.L1460
	movs	r5, #0
	str	r5, [r4, #3652]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1460
.L1459:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1458
	ldr	r3, [r4, #3652]
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3652]
	ldr	r3, [r4, #2512]
	adds	r3, r3, #1
	str	r3, [r4, #2512]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1458:
	ldrh	r3, [r4, #58]
	adds	r7, r7, #1
	subs	r6, r6, r3
	b	.L1457
.L1461:
	mov	r0, #-1
	b	.L1453
.L1469:
	.align	2
.L1468:
	.word	.LANCHOR0
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
	ldr	r4, .L1483
	ldr	r1, .L1483+4
	ldr	r0, .L1483+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #3344]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1471:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1473
	ldr	r9, .L1483+12
	movs	r7, #0
	movw	r8, #65535
.L1474:
	ldrh	r3, [r4, #40]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1479
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1473:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1472
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3344]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1472:
	adds	r5, r5, #1
	b	.L1471
.L1479:
	ldr	r3, [r4, #2324]
	uxth	r5, r7
	ldrh	r2, [r3, r5, lsl #1]
	ldr	r3, [r4, #3344]
	ldrh	r3, [r3, r5, lsl #1]
	cmp	r2, r3
	beq	.L1476
	cmp	r2, r8
	beq	.L1476
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #2348]
	cmp	r3, r6
	beq	.L1476
	ldrh	r3, [r4, #2444]
	cmp	r3, r6
	beq	.L1476
	ldrh	r3, [r4, #2396]
	cmp	r3, r6
	beq	.L1476
	ldr	r3, [r4, #2324]
	ldrh	r2, [r3, r5, lsl #1]
	cbnz	r2, .L1478
	ldr	r2, [r4, #3344]
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
.L1476:
	adds	r7, r7, #1
	b	.L1474
.L1478:
	ldr	r2, [r4, #3344]
	mov	r0, r6
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	update_vpc_list
	b	.L1476
.L1484:
	.align	2
.L1483:
	.word	.LANCHOR0
	.word	.LANCHOR1+501
	.word	.LC95
	.word	.LC111
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
	ldr	r4, .L1495
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1486
	movw	r2, #2714
	ldr	r1, .L1495+4
	ldr	r0, .L1495+8
	bl	sftl_printk
.L1486:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1487
	ldr	r3, [r4, #2324]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1488
	bl	INSERT_DATA_LIST
.L1487:
	ldrh	r0, [r4, #3430]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1489
	cmp	r5, r0
	bne	.L1490
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1491
.L1490:
	bl	update_vpc_list
.L1491:
	movw	r3, #65535
	strh	r3, [r4, #3430]	@ movhi
.L1489:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1488:
	bl	INSERT_FREE_LIST
	b	.L1487
.L1496:
	.align	2
.L1495:
	.word	.LANCHOR0
	.word	.LANCHOR1+522
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
	ldr	r6, .L1511
	.pad #20
	sub	sp, sp, #20
	mov	r4, r3
	movs	r2, #0
	ldr	r10, .L1511+8
	mov	r5, r0
	ldr	fp, .L1511+12
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1498:
	cmp	r7, r9
	beq	.L1505
	mov	r8, #0
	b	.L1506
.L1501:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1499
	ldr	r1, [r6, #2324]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r6, #102]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1499:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1500
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1500:
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
.L1506:
	ldr	r3, [r5]
	adds	r3, r3, #1
	beq	.L1501
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1502
	movw	r2, #955
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1502:
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
	beq	.L1503
	ldr	r3, [r6, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1504
	mov	r1, r0
	ldr	r0, .L1511+4
	bl	sftl_printk
.L1504:
	mov	r0, r8
	bl	decrement_vpc_count
.L1503:
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1498
.L1505:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1497
	movw	r2, #970
	ldr	r1, .L1511+8
	ldr	r0, .L1511+12
	bl	sftl_printk
.L1497:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1512:
	.align	2
.L1511:
	.word	.LANCHOR0
	.word	.LC112
	.word	.LANCHOR1+551
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
	ldr	r4, .L1539
	ldrh	r5, [r4, #2444]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1514
.L1524:
	ldrh	r2, [r4, #2444]
	movs	r3, #0
	str	r3, [r4, #3436]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1537
.L1515:
	movs	r0, #0
.L1513:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1514:
	cbz	r0, .L1517
	ldr	r3, .L1539+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1518
.L1519:
	movs	r1, #2
.L1517:
	ldr	r0, .L1539+8
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L1520
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1521
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L1521:
	ldr	r3, [r4, #3436]
	cbnz	r3, .L1522
	ldr	r3, [r4, #2732]
	ldr	r0, [sp, #4]
	adds	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1522:
	movs	r3, #0
	str	r3, [r4, #3436]
.L1533:
	movs	r0, #1
	b	.L1513
.L1518:
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r4, #2344]
	cmp	r3, #17
	bhi	.L1519
	b	.L1517
.L1520:
	ldr	r3, .L1539+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1533
	b	.L1524
.L1537:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #3200]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1525
	movs	r2, #162
	ldr	r1, .L1539+12
	ldr	r0, .L1539+16
	bl	sftl_printk
.L1525:
	ldrb	r3, [r4, #2451]	@ zero_extendqisi2
	movs	r6, #0
	ldrh	r0, [r4, #102]
	mov	r9, #12
	ldr	r2, [r4, #2324]
	ldrh	r1, [r4, #2444]
	ldr	r10, .L1539+12
	smulbb	r3, r3, r0
	ldr	fp, .L1539+16
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r4, #3200]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1526:
	ldrh	r2, [r4, #3200]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1530
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2444]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1531
	bl	INSERT_DATA_LIST
.L1532:
	movw	r6, #65535
	movs	r5, #0
	strh	r6, [r4, #2444]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	strh	r5, [r4, #3192]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r4, #2572]
	ldrh	r2, [r4, #2344]
	strh	r6, [r4, #2588]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1515
	movs	r3, #20
	strh	r3, [r4, #3148]	@ movhi
	b	.L1515
.L1530:
	uxth	r7, r6
	ldr	r8, [r4, #3196]
	ldr	r3, [r4, #2504]
	mul	r7, r9, r7
	add	r5, r8, r7
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1527
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1527:
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [r8, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1528
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
.L1538:
	bl	decrement_vpc_count
.L1529:
	adds	r6, r6, #1
	b	.L1526
.L1528:
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1529
	ldrh	r0, [r4, #2444]
	b	.L1538
.L1531:
	bl	INSERT_FREE_LIST
	b	.L1532
.L1540:
	.align	2
.L1539:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2444
	.word	.LANCHOR1+564
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
	ldr	r4, .L1543
	ldrh	r1, [r4, #102]
	addw	r0, r4, #2444
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2446]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	bcc	.L1541
	addw	r0, r4, #3384
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3436]
.L1541:
	pop	{r4, pc}
.L1544:
	.align	2
.L1543:
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
	ldr	r4, .L1546
	addw	r6, r4, #2348
	str	r5, [r4, #3516]
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
.L1547:
	.align	2
.L1546:
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
	ldr	r4, .L1555
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3284]
	bl	FlashProgPages
.L1549:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1552
	ldr	r0, [r4, #3284]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #2448]
	movs	r0, #0
	str	r0, [r4, #3164]
	cbnz	r3, .L1548
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1554
.L1552:
	muls	r3, r6, r3
	ldr	r2, [r4, #3284]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1550
	ldrh	r0, [r4, #2444]
	movs	r5, #0
	ldr	r1, [r4, #2324]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	strh	r2, [r4, #2444]	@ movhi
	ldr	r2, [r4, #2732]
	adds	r2, r2, #1
	str	r2, [r4, #2732]
	ldr	r2, [r4, #3284]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1554:
	movs	r0, #1
.L1548:
	pop	{r4, r5, r6, pc}
.L1550:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1549
.L1556:
	.align	2
.L1555:
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
	ldr	r4, .L1560
	ldrh	r2, [r4, #2444]
	cmp	r2, r3
	beq	.L1558
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1559
.L1558:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1560+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	strh	r5, [r4, #3192]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1559:
	ldr	r0, .L1560+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1561:
	.align	2
.L1560:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	r4, .L1577
	ldrh	r0, [r4, #36]
	strh	r3, [r4, #3512]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2576]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1563
.L1565:
	mov	r5, #-1
.L1562:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1563:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1565
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
.L1566:
	cmp	r3, r1
	bge	.L1571
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1567
.L1571:
	ldrh	r2, [r4, #2296]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2296]	@ movhi
	bge	.L1576
.L1568:
	ldr	r0, .L1577+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1577+8
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
	strb	r3, [r4, #2402]
	strh	r3, [r4, #2400]	@ movhi
	ldrh	r3, [r4, #2298]
	ldrh	r2, [r4, #102]
	adds	r3, r3, #1
	strh	r2, [r4, #2398]	@ movhi
	strh	r3, [r4, #2298]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1572
.L1567:
	adds	r3, r3, #1
	b	.L1566
.L1576:
	ldrh	r3, [r4, #3512]
	cmp	r3, #0
	bne	.L1568
.L1572:
	bl	FtlVpcCheckAndModify
	ldrh	r0, [r4, #2348]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1562
	ldrh	r3, [r4, #2352]
	cmp	r3, #0
	bne	.L1562
	ldrh	r3, [r4, #2400]
	cmp	r3, #0
	bne	.L1562
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #2396]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1577+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1577+8
	bl	allocate_new_data_superblock
	b	.L1562
.L1578:
	.align	2
.L1577:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	rk_ftl_garbage_collect
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1701
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	.pad #44
	sub	sp, sp, #44
	mov	r4, r3
	ldr	r0, [r3, #3264]
	cmp	r0, #0
	bne	.L1656
	ldrh	r2, [r3, #2336]
	cmp	r2, #47
	bls	.L1579
	ldr	r2, .L1701+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1581
.L1584:
	ldrh	r2, [r4, #3204]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1582
.L1583:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3152]
	movw	r6, #65535
	ldrh	r1, [r4, #2588]
	adds	r3, r3, #1
	add	r3, r3, r9, lsl #7
	cmp	r1, r6
	str	r3, [r4, #3152]
	bne	.L1585
	ldrh	r2, [r4, #2444]
	cmp	r2, r1
	bne	.L1586
	ldrh	r7, [r4, #3202]
	cmp	r7, r2
	bne	.L1587
	ldrh	r2, [r4, #2344]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1587
	movs	r5, #0
	str	r5, [r4, #3152]
	strh	r5, [r4, #3656]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r6, r0
	bne	.L1660
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r4, #3150]
	cmp	r2, r3
	bcs	.L1590
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r6
	beq	.L1592
	mov	r0, r3
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1593
	mov	r0, r5
	bl	List_get_gc_head_node
	uxth	r5, r0
	movs	r3, #128
	strh	r3, [r4, #3150]	@ movhi
	cmp	r5, r6
	bne	.L1589
.L1592:
	bl	FtlGcReFreshBadBlk
	cmp	r9, #0
	bne	.L1595
	movw	r3, #65535
	cmp	r6, r3
	bne	.L1595
.L1654:
	ldrh	r3, [r4, #2344]
	cmp	r3, #24
	bhi	.L1661
	cmp	r3, #16
	ldrh	r5, [r4, #102]
	bls	.L1597
	lsrs	r5, r5, #5
.L1596:
	ldrh	r2, [r4, #3148]
	cmp	r2, r3
	bcs	.L1600
	ldrh	r3, [r4, #2444]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1601
	ldrh	r2, [r4, #3202]
	cmp	r2, r3
	bne	.L1601
	ldrh	r0, [r4, #3656]
	cbnz	r0, .L1602
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1603
.L1602:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1697:
	strh	r3, [r4, #3148]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3160]
.L1579:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1581:
	ldrh	r3, [r3, #2444]
	cmp	r3, r2
	beq	.L1584
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1584
	movs	r0, #1
	b	.L1579
.L1582:
	ldrh	r3, [r4, #3202]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #3202]	@ movhi
	strheq	r3, [r4, #3204]	@ movhi
	b	.L1583
.L1593:
	movs	r3, #64
.L1696:
	strh	r3, [r4, #3150]	@ movhi
	b	.L1592
.L1590:
	movs	r3, #80
	b	.L1696
.L1660:
	mov	r5, r0
.L1589:
	ldr	r3, [r4, #2324]
	mov	r6, r5
	ldrh	r1, [r4, #3148]
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r3, r5, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2328]
	ldr	r0, .L1701+8
	ldrh	r1, [r1, r5, lsl #1]
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1592
.L1597:
	cmp	r3, #12
	bls	.L1598
	lsrs	r5, r5, #4
	b	.L1596
.L1598:
	cmp	r3, #8
	bls	.L1596
	lsrs	r5, r5, #2
	b	.L1596
.L1661:
	movs	r5, #1
	b	.L1596
.L1603:
	movs	r3, #18
	b	.L1697
.L1601:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3148]	@ movhi
.L1600:
	ldrh	r3, [r4, #3206]
	cbz	r3, .L1662
	adds	r5, r5, #32
	uxth	r5, r5
.L1662:
	movw	r6, #65535
.L1606:
	ldrh	r3, [r4, #2588]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1615
	cmp	r6, r3
	beq	.L1616
	strh	r6, [r4, #2588]	@ movhi
.L1617:
	ldrh	r0, [r4, #2588]
	movw	r7, #65535
	movs	r3, #0
	strb	r3, [r4, #2596]
	cmp	r0, r7
	beq	.L1615
	bl	IsBlkInGcList
	cbz	r0, .L1620
	strh	r7, [r4, #2588]	@ movhi
.L1620:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1615
	ldr	r0, .L1701+12
	bl	make_superblock
	movs	r3, #0
	ldrh	r2, [r4, #2588]
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r3, [r4, #3658]	@ movhi
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r2, lsl #1]
	strh	r3, [r4, #3660]	@ movhi
.L1615:
	ldrh	r3, [r4, #2588]
	ldrh	r2, [r4, #2348]
	cmp	r2, r3
	beq	.L1621
	ldrh	r2, [r4, #2396]
	cmp	r2, r3
	bne	.L1622
.L1621:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1622:
	ldr	r7, .L1701
	mov	r4, r7
.L1652:
	ldrh	r10, [r7, #2588]
	movw	r3, #65535
	cmp	r10, r3
	bne	.L1623
	movs	r3, #0
	mov	fp, r3
	str	r3, [r7, #3160]
.L1624:
	ldrh	r8, [r4, #3156]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r10
	strh	r2, [r4, #2588]	@ movhi
	bne	.L1625
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3156]	@ movhi
	b	.L1579
.L1607:
	movs	r3, #0
	ldrh	r2, [r4, #2344]
	str	r3, [r4, #3160]
	ldrh	r3, [r4, #3148]
	cmp	r2, r3
	bls	.L1608
	ldrh	r3, [r4, #3656]
	cbnz	r3, .L1609
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1610
.L1609:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1698:
	strh	r3, [r4, #3148]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r4, #2324]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L1608
.L1700:
	ldrh	r0, [r4, #3656]
	b	.L1579
.L1610:
	movs	r3, #18
	b	.L1698
.L1608:
	ldrh	r0, [r4, #3656]
	cmp	r0, #0
	bne	.L1612
	ldrh	r5, [r4, #2572]
	add	r3, r5, r5, lsl #1
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
	ble	.L1613
	ldrh	r3, [r4, #2344]
	subs	r5, r5, #1
	cmp	r3, r5
	blt	.L1613
	bl	FtlReadRefresh
	b	.L1700
.L1613:
	cmp	r2, #0
	bne	.L1612
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2344]
	adds	r0, r0, #1
	b	.L1579
.L1702:
	.align	2
.L1701:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC113
	.word	.LANCHOR0+2588
.L1616:
	ldrh	r3, [r4, #3202]
	cmp	r3, r6
	beq	.L1617
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1618
	strh	r6, [r4, #3202]	@ movhi
.L1618:
	ldrh	r3, [r4, #3202]
	strh	r3, [r4, #2588]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
	b	.L1617
.L1625:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r8, r8, #1
	bl	IsBlkInGcList
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	cbz	r0, .L1626
	strh	r8, [r4, #3156]	@ movhi
	b	.L1624
.L1626:
	ldrh	lr, [r4, #32]
	uxth	r8, r8
	ldrh	r1, [r4, #102]
	uxth	r3, r3
	ldr	r0, [r4, #2324]
	strh	r8, [r4, #3156]	@ movhi
	mul	r1, lr, r1
	ldrh	ip, [r0, r3, lsl #1]
	add	lr, r1, r1, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1628
	cmp	r8, #48
	bls	.L1629
	cmp	ip, #8
	bls	.L1629
	ldrh	ip, [r4, #3192]
	cmp	ip, #35
	bhi	.L1629
.L1628:
	strh	fp, [r4, #3156]	@ movhi
.L1629:
	ldrh	r3, [r0, r3, lsl #1]
	cmp	r1, r3
	bgt	.L1630
	cmp	r6, r10
	bne	.L1630
	ldrh	r1, [r4, #3156]
	cmp	r1, #3
	bhi	.L1630
	movs	r3, #0
	strh	r6, [r4, #2588]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	b	.L1700
.L1630:
	cbnz	r3, .L1631
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3156]
	adds	r3, r3, #1
	strh	r3, [r4, #3156]	@ movhi
	b	.L1624
.L1631:
	movs	r3, #0
	strb	r3, [r4, #2596]
	ldrh	r3, [r4, #2348]
	cmp	r3, r2
	bne	.L1632
	mov	r2, #700
	ldr	r1, .L1703
	ldr	r0, .L1703+4
	bl	sftl_printk
.L1632:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2396]
	cmp	r2, r3
	bne	.L1633
	movw	r2, #701
	ldr	r1, .L1703
	ldr	r0, .L1703+4
	bl	sftl_printk
.L1633:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2444]
	cmp	r2, r3
	bne	.L1634
	movw	r2, #702
	ldr	r1, .L1703
	ldr	r0, .L1703+4
	bl	sftl_printk
.L1634:
	ldr	r0, .L1703+8
	bl	make_superblock
	ldrh	r1, [r4, #2588]
	movs	r3, #0
	ldr	r2, [r4, #2324]
	strh	r3, [r4, #3658]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r2, [r4, #3660]	@ movhi
.L1623:
	movs	r3, #1
	str	r3, [r4, #3264]
	ldrh	r3, [r4, #102]
	str	r3, [sp, #16]
	cmp	r9, #0
	beq	.L1635
	ldr	r2, [sp, #16]
	ldrh	r3, [r4, #32]
	ldrh	r1, [r4, #2588]
	muls	r3, r2, r3
	ldr	r2, [r4, #2324]
	ldrh	r2, [r2, r1, lsl #1]
	subs	r3, r3, r2
	it	mi
	addmi	r3, r3, #3
	add	r5, r5, r3, asr #2
	uxth	r5, r5
.L1635:
	ldrh	r3, [r4, #2590]
	ldr	r1, [sp, #16]
	adds	r2, r3, r5
	cmp	r2, r1
	itt	gt
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	it	gt
	uxthgt	r5, r5
.L1699:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	cmp	r5, r3
	bls	.L1646
	ldr	r3, [sp, #20]
	movw	r10, #65535
	ldrh	r8, [r7, #2590]
	mov	fp, #20
	ldrh	ip, [r7, #32]
	ldr	r0, [r7, #3180]
	add	r8, r8, r3
	ldr	r3, .L1703+12
	str	r3, [sp, #24]
	movs	r3, #0
	str	r3, [sp, #12]
	b	.L1647
.L1640:
	ldr	r1, [sp, #24]
	ldrh	r2, [r1, #2]!
	cmp	r2, r10
	str	r1, [sp, #24]
	beq	.L1639
	ldr	r1, [sp, #12]
	orr	r2, r8, r2, lsl #10
	mla	lr, fp, r1, r0
	str	r2, [lr, #4]
	mov	r2, r1
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1639:
	adds	r3, r3, #1
.L1647:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1640
	mov	fp, #0
	ldrb	r2, [r7, #2596]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	bl	FlashReadPages
.L1641:
	ldr	r2, [sp, #12]
	uxth	r3, fp
	cmp	r2, r3
	bhi	.L1645
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1699
.L1645:
	mov	r8, #20
	ldr	r3, [r7, #3180]
	mul	r8, r8, fp
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L1642
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1642
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1643
	mov	r2, #736
	ldr	r1, .L1703
	ldr	r0, .L1703+4
	str	r3, [sp, #24]
	bl	sftl_printk
	ldr	r3, [sp, #24]
.L1643:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #36
	bl	log2phys
	ldr	r0, [r4, #3180]
	ldr	r3, [sp, #36]
	add	r0, r0, r8
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1642
	ldrh	r3, [r4, #3658]
	movs	r2, #20
	ldr	r1, [r4, #3164]
	str	r2, [sp, #28]
	adds	r3, r3, #1
	strh	r3, [r4, #3658]	@ movhi
	ldr	r3, [r4, #3284]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r1, [r3, #16]
	str	r3, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #24]
	ldr	r1, [r4, #3284]
	ldr	r2, [sp, #28]
	str	r0, [r3, #4]
	ldr	r3, [r4, #3164]
	mla	r3, r2, r3, r1
	ldr	r2, [r4, #3180]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #36]
	str	r3, [r10, #12]
	ldrh	r3, [r4, #2444]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #2544]
	ldr	r0, [r4, #3180]
	str	r3, [r10, #4]
	ldr	r3, [r4, #3164]
	add	r0, r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #3164]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	ldr	r3, [r4, #3164]
	cmp	r2, r3
	beq	.L1644
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1642
.L1644:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1642
	ldr	r3, .L1703+16
	movs	r2, #0
	str	r2, [r3, #3264]
	ldrh	r0, [r3, #3656]
	b	.L1579
.L1642:
	add	fp, fp, #1
	b	.L1641
.L1646:
	ldrh	r3, [r7, #2590]
	add	r5, r5, r3
	ldr	r3, [sp, #16]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r7, #2590]	@ movhi
	bhi	.L1648
	ldr	r3, [r7, #3164]
	cbz	r3, .L1649
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1649
	movs	r3, #0
	ldrh	r0, [r7, #3656]
	str	r3, [r7, #3264]
	b	.L1579
.L1649:
	ldrh	r1, [r7, #3658]
	cbnz	r1, .L1650
	ldrh	r2, [r7, #2588]
	ldr	r3, [r7, #2324]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L1650
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r7, #2588]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1650:
	movw	r3, #65535
	strh	r3, [r7, #2588]	@ movhi
.L1648:
	movs	r3, #0
	str	r3, [r7, #3264]
	ldrh	r3, [r7, #2344]
	cmp	r3, #2
	bhi	.L1651
	ldrh	r5, [r7, #102]
	b	.L1652
.L1651:
	ldrh	r0, [r7, #3656]
	cmp	r0, #0
	bne	.L1579
	adds	r0, r3, #1
	b	.L1579
.L1656:
	movs	r0, #0
	b	.L1579
.L1585:
	cmp	r9, #0
	beq	.L1654
.L1595:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1665
.L1655:
	ldrh	r3, [r4, #3202]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1665
	cmp	r6, r3
	bne	.L1665
	ldrh	r3, [r4, #2588]
	cmp	r3, r6
	beq	.L1607
.L1612:
	movw	r6, #65535
.L1665:
	movs	r5, #1
	b	.L1606
.L1587:
	cmp	r9, #0
	beq	.L1654
	movw	r6, #65535
	b	.L1655
.L1586:
	cmp	r9, #0
	bne	.L1612
	b	.L1654
.L1704:
	.align	2
.L1703:
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+2602
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	ldr	r4, .L1711
	ldr	r5, .L1711+4
	ldr	r1, .L1711+8
	ldr	r0, .L1711+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1709
	bl	FtlSysBlkInit
	cbnz	r0, .L1709
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bhi	.L1709
	movw	r4, #8129
.L1708:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1708
.L1709:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1712:
	.align	2
.L1711:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC70
	.word	.LC71
	.fnend
	.size	sftl_init, .-sftl_init
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
	str	r2, [sp, #20]
	bne	.L1715
	mov	r2, r3
	ldr	r1, [sp, #20]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	mov	r8, r0
.L1714:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1715:
	ldr	r3, [sp, #20]
	ldr	r4, .L1747
	adds	r3, r1, r3
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1736
	ldr	r3, .L1747+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1737
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
	ldr	r2, [sp, #20]
	ldr	r5, [sp, #12]
	rsb	r3, r3, #1
	str	r0, [sp, #16]
	add	r3, r3, r0
	str	r9, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [r4, #2536]
	str	r9, [sp, #24]
	add	r3, r3, r2
	ldr	r2, [sp, #4]
	str	r3, [r4, #2536]
	ldr	r3, [r4, #2508]
	add	r3, r3, r2
	str	r3, [r4, #2508]
.L1717:
	ldr	r3, [sp, #4]
	cbnz	r3, .L1734
	ldr	r3, .L1747
	ldrh	r2, [r3, #3206]
	cbnz	r2, .L1735
	ldrh	r3, [r3, #2344]
	cmp	r3, #31
	bhi	.L1714
.L1735:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1714
.L1734:
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1718
	mov	fp, #0
.L1719:
	ldrh	r0, [r4, #58]
	cmp	fp, r0
	bcc	.L1721
.L1722:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1726
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1717
.L1726:
	cmp	r6, #0
	beq	.L1717
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3280]
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #28]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1733:
	movs	r3, #20
	ldr	r2, [r4, #3280]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1728
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3304]
	cmp	r1, r2
	bne	.L1729
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1746:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1729:
	ldr	ip, [r4, #3280]
	add	r2, ip, r3
	ldr	r0, [r2, #12]
	ldr	r1, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	r1, r0
	ittt	ne
	ldrne	r1, [r4, #2708]
	addne	r1, r1, #1
	strne	r1, [r4, #2708]
	ldr	r0, [ip, r3]
	adds	r1, r0, #1
	itttt	eq
	ldreq	r1, [r4, #2708]
	moveq	r8, r0
	addeq	r1, r1, #1
	streq	r1, [r4, #2708]
	ldr	r3, [ip, r3]
	cmp	r3, #256
	bne	.L1732
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1732:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1733
	movs	r6, #0
	b	.L1717
.L1721:
	mla	r0, r0, r5, fp
	cmp	r7, r0
	bhi	.L1720
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1720
	subs	r0, r0, r7
	mov	r1, #512
	add	r0, r10, r0, lsl #9
	bl	__memzero
.L1720:
	add	fp, fp, #1
	b	.L1719
.L1718:
	ldr	r3, [r4, #3280]
	movs	r1, #20
	ldrh	fp, [r4, #58]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [sp, #12]
	cmp	r5, r2
	bne	.L1723
	ldr	r2, [r4, #3304]
	mov	r1, fp
	mov	r0, r7
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	bl	__aeabi_uidivmod
	sub	r2, fp, r1
	str	r1, [sp, #24]
	ldr	r1, [sp, #20]
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	cmp	r3, fp
	str	r3, [sp, #28]
	ldr	r3, [sp, #32]
	bne	.L1724
	str	r10, [r3, #8]
.L1724:
	ldrh	r2, [r4, #112]
	ldr	r1, [r4, #3328]
	str	r5, [r3, #16]
	muls	r2, r6, r2
	adds	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1722
.L1723:
	ldr	r2, [sp, #16]
	cmp	r5, r2
	bne	.L1725
	ldr	r2, [r4, #3308]
	ldr	r1, [sp, #8]
	str	r2, [r3, #8]
	mul	r2, fp, r5
	sub	r9, r1, r2
	cmp	fp, r9
	bne	.L1724
.L1745:
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1724
.L1725:
	mul	r2, r5, fp
	b	.L1745
.L1728:
	ldr	r0, [sp, #16]
	cmp	r0, r1
	bne	.L1729
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3308]
	cmp	r1, r2
	bne	.L1729
	str	r3, [sp, #44]
	ldrh	r0, [r4, #58]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1746
.L1736:
	mov	r8, #-1
	b	.L1714
.L1737:
	mov	r8, r3
	b	.L1714
.L1748:
	.align	2
.L1747:
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
	mov	r10, r1
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bne	.L1751
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r10, #256
	bl	FtlVendorPartWrite
.L1750:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1751:
	ldr	r3, [sp, #12]
	ldr	r5, .L1802
	adds	r6, r1, r3
	ldr	r3, [r5, #140]
	cmp	r6, r3
	bhi	.L1788
	ldr	r4, .L1802+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1750
	ldrh	r7, [r5, #58]
	mov	r3, #2048
	str	r3, [r5, #3664]
	mov	r0, r10
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [sp, #4]
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	subs	r3, r0, r2
	ldr	r2, [sp, #12]
	str	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [r5, #2532]
	add	r3, r3, r2
	ldr	r2, [sp]
	str	r3, [r5, #2532]
	ldr	r3, [r5, #2516]
	add	r3, r3, r2
	str	r3, [r5, #2516]
	ldr	r3, [r4, #8]
	cbz	r3, .L1790
	ldrh	r2, [r5, #2352]
	addw	r3, r5, #2348
	addw	r5, r5, #2396
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1753:
	ldr	r7, [sp, #4]
	ldr	r4, .L1802
.L1754:
	ldr	r3, [sp]
	cbnz	r3, .L1783
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #2344]
	cmp	r3, #5
	bls	.L1784
.L1786:
	movs	r0, #0
	b	.L1750
.L1790:
	addw	r5, r5, #2348
	b	.L1753
.L1783:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1755
	movw	r2, #1011
	ldr	r1, .L1802+8
	ldr	r0, .L1802+12
	bl	sftl_printk
.L1755:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1756
	ldr	r3, .L1802+16
	ldr	r6, .L1802+4
	cmp	r5, r3
	bne	.L1757
	ldrh	r8, [r4, #2400]
	cmp	r8, #0
	bne	.L1758
	add	r0, r5, #48
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1758:
	ldr	r5, .L1802+16
	ldr	r0, .L1802+16
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1759:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1756
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1756:
	ldrh	r2, [r5, #4]
	ldr	r3, [sp]
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	lsls	r3, r3, #2
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrh	r3, [r4, #32]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1760
	movw	r2, #1044
	ldr	r1, .L1802+8
	ldr	r0, .L1802+12
	bl	sftl_printk
.L1760:
	mov	fp, #0
.L1761:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1781
.L1762:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3288]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1782
	movw	r2, #1121
	ldr	r1, .L1802+8
	ldr	r0, .L1802+12
	bl	sftl_printk
.L1782:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1754
.L1757:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #2352]
	cbnz	r2, .L1793
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1759
.L1793:
	mov	r5, r3
	b	.L1756
.L1781:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1762
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r7
	mov	r8, #20
	bl	log2phys
	mov	r0, r5
	mul	r8, r8, fp
	bl	get_new_active_ppa
	ldrh	r2, [r4, #112]
	ldr	r3, [r4, #3288]
	mul	r1, r2, fp
	add	r3, r3, r8
	str	r0, [r3, #4]
	str	r7, [r3, #16]
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3328]
	add	r9, r1, r0
	str	r1, [sp, #32]
	str	r9, [r3, #12]
	movs	r1, #0
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r7, r3
	beq	.L1763
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1798
	ldrh	r2, [r4, #58]
	ldr	r3, [sp, #12]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1766
.L1763:
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
.L1766:
	ldrh	r3, [r4, #58]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1767
	cmp	r7, r3
	ldr	r3, [r4, #3288]
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
.L1769:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1778
	mov	r2, #1112
	ldr	r1, .L1802+8
	ldr	r0, .L1802+12
	bl	sftl_printk
.L1778:
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
	b	.L1761
.L1767:
	cmp	r7, r3
	ldr	r3, [r4, #3288]
	ite	eq
	ldreq	r2, [r4, #3304]
	ldrne	r2, [r4, #3308]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
	beq	.L1772
	str	r3, [sp, #48]
	movs	r1, #1
	ldr	r3, [r4, #3288]
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
	bne	.L1773
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
.L1776:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1777
	ldr	r3, [r4, #3288]
	ldr	r1, [sp, #8]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1800:
	bl	ftl_memcpy
	b	.L1769
.L1803:
	.align	2
.L1802:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+606
	.word	.LC1
	.word	.LANCHOR0+2348
.L1773:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1775
	ldr	r3, [r4, #2708]
	mov	r2, r7
	ldr	r0, .L1804
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1775:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1776
	movw	r2, #1097
	ldr	r1, .L1804+4
	ldr	r0, .L1804+8
	bl	sftl_printk
	b	.L1776
.L1772:
	ldr	r3, [r4, #3288]
	movs	r1, #0
	ldrh	r2, [r4, #110]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1776
.L1777:
	ldrh	r1, [r4, #58]
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #3288]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1800
.L1798:
	ldr	r3, [r4, #3288]
	ldr	r2, [sp, #8]
	add	r8, r8, r3
	ldrh	r3, [r4, #58]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1769
.L1784:
	mov	r5, #256
	movw	r6, #65535
.L1787:
	ldrh	r3, [r4, #2588]
	cmp	r3, r6
	bne	.L1785
	ldrh	r3, [r4, #3202]
	cmp	r3, r6
	bne	.L1785
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1785:
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
	cmp	r3, #2
	bhi	.L1786
	subs	r5, r5, #1
	bne	.L1787
	b	.L1786
.L1788:
	mov	r0, #-1
	b	.L1750
.L1805:
	.align	2
.L1804:
	.word	.LC114
	.word	.LANCHOR1+606
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
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	adds	r5, r0, r1
	subs	r6, r5, #1
	.pad #132
	sub	sp, sp, #132
	cmp	r6, #63
	mov	fp, r0
	str	r1, [sp, #20]
	str	r2, [sp, #40]
	ldr	r4, .L1849
	bls	.L1807
	cmp	r0, #576
	bls	.L1808
.L1807:
	ldr	r3, [r4, #3668]
	cbz	r3, .L1809
	ldr	r10, [r4, #3672]
	ldr	r3, .L1849+4
	ldr	r2, [r10]
	cmp	r2, r3
	beq	.L1810
.L1827:
	ldr	r4, .L1849
	movs	r3, #0
	ldr	r0, [r4, #3672]
	str	r3, [r4, #3668]
	bl	kfree
	ldr	r0, [r4, #3676]
	bl	kfree
.L1809:
	ldr	r3, [sp, #40]
	mov	r1, fp
	ldr	r2, [sp, #20]
	movs	r0, #0
	bl	FtlWrite
	add	sp, sp, #132
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1810:
	ldrh	r5, [r4, #14]
	add	r0, r10, #262144
	ldrh	r4, [r4, #10]
	movs	r3, #0
	movw	r2, #65535
.L1814:
	ldr	r1, [r0, #-4]
	cbnz	r1, .L1811
	ldr	r1, [r10, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #-4]!
	bne	.L1814
	mov	r3, #512
	str	r3, [sp, #8]
	b	.L1813
.L1811:
	add	r3, r2, #127
	ldr	r0, .L1849+8
	lsrs	r3, r3, #7
	str	r3, [sp, #8]
	bl	sftl_printk
.L1813:
	uxtb	r5, r5
	smulbb	r5, r5, r4
	ldr	r4, .L1849
	uxth	r3, r5
	movs	r5, #0
	str	r3, [sp, #12]
	subs	r0, r3, #1
	mov	r1, r3
	ldr	r3, [sp, #8]
	add	r0, r0, r3
	bl	__aeabi_uidiv
	ldr	r2, [sp, #12]
	mov	r3, r0
	str	r0, [sp, #16]
	str	r0, [sp, #36]
	str	r5, [sp, #28]
	muls	r3, r2, r3
	str	r3, [sp, #24]
	ldr	r3, [sp, #8]
	lsls	r3, r3, #7
	str	r3, [sp, #52]
	b	.L1826
.L1834:
	str	r3, [sp, #36]
.L1826:
	mov	r1, #512
	ldr	r0, [r4, #3676]
	bl	__memzero
	ldrh	r7, [r4, #14]
	mov	r0, r5
	ldrh	r6, [r4, #10]
	mov	r1, r7
	uxtb	r8, r7
	uxth	r3, r8
	str	r3, [sp, #32]
	ldrh	r3, [sp, #32]
	smulbb	r6, r6, r3
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3248]
	mov	r1, r0
	movs	r0, #0
	uxth	r6, r6
	blx	r3
	ldr	r3, [sp, #24]
	cmp	r3, r6
	bls	.L1815
	mov	r1, r7
	adds	r0, r6, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3248]
	mov	r1, r0
	movs	r0, #0
	blx	r3
.L1815:
	mov	r1, r6
	mov	r0, r5
	bl	__aeabi_uidivmod
	movs	r6, #0
	mov	r9, r1
	subs	r3, r5, r1
	str	r3, [sp, #44]
.L1816:
	ldr	r3, [sp, #24]
	cmp	r3, r6
	bhi	.L1817
	ldrb	r8, [r4, #14]	@ zero_extendqisi2
	mov	r0, r5
	ldr	r3, [r4, #3676]
	movs	r6, #0
	ldrh	r1, [r4, #10]
	str	r3, [sp, #48]
	uxth	r3, r8
	str	r3, [sp, #44]
	ldrh	r3, [sp, #44]
	smulbb	r1, r1, r3
	uxth	r1, r1
	bl	__aeabi_uidivmod
	str	r1, [sp, #32]
	subs	r3, r5, r1
	ldrh	r0, [sp, #32]
	mov	r1, r8
	str	r3, [sp, #56]
	bl	__aeabi_idivmod
	uxth	r7, r1
.L1818:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcs	.L1822
	ldr	r3, [sp, #44]
	mov	r1, r8
	sub	r9, r3, r7
	ldr	r3, [sp, #48]
	uxth	r9, r9
	add	r2, r3, r6, lsl #9
	ldr	r3, [sp, #32]
	str	r2, [sp, #60]
	adds	r0, r3, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #56]
	uxth	r0, r0
	mov	r1, r8
	add	r7, r7, r3
	mla	r0, r8, r0, r7
	bl	__aeabi_uidiv
	ldr	r7, [r4, #3256]
	mov	r1, r0
	add	r3, sp, #64
	ldr	r2, [sp, #60]
	movs	r0, #0
	blx	r7
	adds	r0, r0, #1
	bne	.L1819
.L1822:
	ldr	r0, [r4, #3676]
	mov	r1, r10
	movs	r6, #0
.L1820:
	mov	r8, r1
	ldr	r2, [r0, r6, lsl #2]
	ldr	r3, [r8]
	lsls	r7, r6, #2
	adds	r1, r1, #4
	cmp	r2, r3
	beq	.L1823
	mov	r1, #512
	bl	__memzero
	ldr	r2, [r4, #3676]
	mov	r1, r5
	str	r6, [sp]
	ldr	r0, .L1849+12
	ldr	r3, [r8]
	ldr	r2, [r2, r7]
	bl	sftl_printk
	ldr	r3, [r4, #3248]
	mov	r1, r5
	movs	r0, #0
	blx	r3
	ldr	r3, [sp, #16]
	cmp	r3, #1
	bls	.L1833
	ldr	r2, [sp, #12]
	movs	r0, #0
	ldr	r3, [r4, #3248]
	adds	r1, r2, r5
	blx	r3
.L1833:
	ldr	r2, [sp, #36]
	ldr	r3, [sp, #16]
	add	r3, r3, r2
	ldr	r2, [sp, #24]
	add	r5, r5, r2
	ldr	r2, [sp, #36]
	cmp	r2, #15
	bls	.L1834
	b	.L1827
.L1817:
	mov	r1, r8
	add	r0, r9, r6
	bl	__aeabi_uidiv
	uxth	r7, r0
	add	r2, r10, r6, lsl #9
	ldr	r0, [sp, #44]
	mul	r3, r8, r7
	mov	r1, r8
	str	r2, [sp, #48]
	str	r3, [sp, #64]
	movw	r3, #61424
	str	r3, [sp, #68]
	bl	__aeabi_uidiv
	adds	r1, r0, r7
	add	r3, sp, #64
	ldr	r2, [sp, #48]
	movs	r0, #0
	ldr	r7, [r4, #3252]
	blx	r7
	ldr	r3, [sp, #32]
	add	r6, r6, r3
	uxth	r6, r6
	b	.L1816
.L1819:
	add	r6, r6, r9
	movs	r7, #0
	uxth	r6, r6
	b	.L1818
.L1823:
	ldr	r3, [sp, #52]
	adds	r6, r6, #1
	cmp	r6, r3
	bne	.L1820
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #28]
	bls	.L1833
	b	.L1827
.L1808:
	cmp	r0, #64
	bne	.L1828
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r4, #3672]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r4, #3672]
	str	r0, [r4, #3676]
	cbz	r3, .L1829
	cbz	r0, .L1829
	movs	r2, #1
	mov	r1, #262144
	mov	r0, r3
	str	r2, [r4, #3668]
	bl	__memzero
.L1828:
	cmp	fp, #63
	ldr	r0, [r4, #3672]
	bhi	.L1830
	ldr	r3, [sp, #20]
	rsb	r1, fp, #64
	subs	r2, r3, r1
	ldr	r3, [sp, #40]
	add	r1, r3, r1, lsl #9
.L1831:
	cmp	r6, #576
	ittt	hi
	subhi	r2, r2, r5
	mvnhi	r3, #446
	addhi	r2, r2, r3
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L1809
.L1829:
	ldr	r1, .L1849+16
	ldr	r0, .L1849+20
	bl	sftl_printk
	b	.L1828
.L1830:
	ldr	r3, .L1849+24
	ldr	r1, [sp, #40]
	ldr	r2, [sp, #20]
	add	r3, r3, fp
	add	r0, r0, r3, lsl #9
	b	.L1831
.L1850:
	.align	2
.L1849:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC115
	.word	.LC116
	.word	.LANCHOR1+615
	.word	.LC117
	.word	8388544
	.fnend
	.size	sftl_write, .-sftl_write
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
	ldr	r9, .L1870+4
	bl	FtlBbtMemInit
	sub	r5, r9, #180
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L1852:
	ldrh	r3, [r5, #54]
	cmp	r8, r3
	bcc	.L1858
	movs	r5, #0
.L1859:
	ldrh	r3, [r4, #114]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1860
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1861:
	ldrh	r3, [r4, #164]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1865
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1862
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1863
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1862:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1861
.L1858:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3292]
	ldr	fp, [r5, #3324]
	cmp	r3, r2
	str	r0, [r5, #3452]
	str	fp, [r5, #3456]
	beq	.L1853
	ldrh	r7, [r5, #98]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1870
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r5, #3448]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3452]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L1854:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L1852
.L1853:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3452]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1856:
	subs	r6, r6, #1
	uxth	r6, r6
.L1855:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1856
	movs	r1, #16
	ldr	r0, [r4, #3324]
	strh	r6, [r10]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3292]
	bl	__memzero
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	movs	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r10]
	ldrh	r7, [r4, #98]
	ldrh	r2, [r4, #3380]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3452]
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r4, #3448]
	bl	ftl_memcpy
	movs	r2, #1
	ldr	r0, .L1870
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1870
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3444]
	adds	r3, r3, #1
	bne	.L1854
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1855
.L1860:
	bl	FtlBbmMapBadBlock
	b	.L1859
.L1863:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	bne	.L1864
	strh	r5, [r4, #152]	@ movhi
	b	.L1862
.L1864:
	strh	r5, [r4, #156]	@ movhi
.L1865:
	ldrh	r3, [r4, #152]
	movs	r5, #0
	ldr	r0, [r4, #3260]
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
.L1871:
	.align	2
.L1870:
	.word	.LANCHOR0+3444
	.word	.LANCHOR0+180
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	ldr	r4, .L1896
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1873
	bl	FtlMakeBbt
.L1873:
	ldr	r0, .L1896+4
	movs	r2, #0
.L1874:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1875
	ldrh	r6, [r4, #40]
	movs	r5, #0
.L1876:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1877
	ldrh	r1, [r4, #32]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1878
.L1882:
	movs	r5, #0
	mov	r6, r5
.L1879:
	ldrh	r3, [r4, #40]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1883
	ldrh	r3, [r4, #42]
	ldrh	r5, [r4, #32]
	ldr	r9, [r4, #44]
	str	r3, [r4, #3276]
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
	ble	.L1884
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2504]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2572]	@ movhi
.L1884:
	ldrh	r3, [r4, #94]
	cbz	r3, .L1886
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
.L1886:
	ldrh	r2, [r4, #2572]
	ldr	r3, [r4, #2504]
	ldr	r6, .L1896+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	ldrh	r3, [r4, #102]
	str	r5, [r4, #3464]
	muls	r5, r3, r5
	ldrh	r3, [r4, #58]
	str	r5, [r4, #2504]
	muls	r5, r3, r5
	str	r5, [r4, #140]
	movw	r5, #65535
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #2324]
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
	strh	r5, [r4, #2588]	@ movhi
	strb	r3, [r4, #2356]
	bl	ftl_memset
.L1888:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	ldrh	r2, [r4, #2348]
	cmp	r3, #0
	bne	.L1889
	ldr	r3, [r4, #2324]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2348]
	adds	r3, r3, #1
	strh	r3, [r4, #2348]	@ movhi
	b	.L1888
.L1875:
	ldr	r5, [r4, #3304]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3308]
	str	r0, [r1, r3, lsl #2]
	b	.L1874
.L1877:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1876
.L1878:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1880:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1882
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1880
.L1883:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1879
.L1889:
	ldr	r3, [r4, #2540]
	movw	r5, #65535
	ldrh	r1, [r4, #2352]
	ldr	r6, .L1896+12
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
.L1890:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	ldrh	r2, [r4, #2396]
	cbnz	r3, .L1891
	ldr	r3, [r4, #2324]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2396]
	adds	r3, r3, #1
	strh	r3, [r4, #2396]	@ movhi
	b	.L1890
.L1891:
	ldr	r3, [r4, #2540]
	movw	r5, #65535
	ldrh	r1, [r4, #2400]
	str	r3, [r4, #2408]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	ldr	r3, [r4, #2324]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r5, [r4, #2444]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r0, [r4, #2576]	@ movhi
	strh	r3, [r4, #2578]	@ movhi
	ldr	r3, [r4, #3464]
	strh	r5, [r4, #2580]	@ movhi
	strh	r3, [r4, #2582]	@ movhi
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2584]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L1892
	ldr	r3, .L1896+16
	movs	r2, #1
	str	r2, [r3]
.L1892:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1897:
	.align	2
.L1896:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.word	.LANCHOR2
	.fnend
	.size	ftl_low_format, .-ftl_low_format
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
	ldr	r2, .L1902
	ldr	r3, [r2, #3680]
	cbnz	r3, .L1901
	str	r0, [r2, #3680]
	mov	r0, r3
	str	r1, [r2, #3684]
	bx	lr
.L1901:
	mov	r0, #-1
	bx	lr
.L1903:
	.align	2
.L1902:
	.word	.LANCHOR0
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
	ldr	r4, .L1912
	bl	ftl_malloc
	str	r0, [r4, #3688]
	cmp	r0, #0
	beq	.L1910
	ldr	r8, .L1912+4
	mov	r9, #0
	mov	r7, r9
	mov	r6, r9
.L1908:
	ldr	r3, [r4, #3680]
	movs	r1, #128
	ldr	r2, [r4, #3688]
	lsls	r0, r6, #7
	blx	r3
	mov	r5, r0
	cbnz	r0, .L1906
	ldr	r2, [r4, #3688]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1907
	movw	r3, #65532
	ldr	r1, [r2, r3]
	ldr	r3, [r2, #4]
	cmp	r1, r3
	bne	.L1907
	cmp	r1, r7
	itt	hi
	movhi	r9, r6
	movhi	r7, r1
.L1907:
	adds	r6, r6, #1
	cmp	r6, #2
	bne	.L1908
	cbz	r7, .L1909
	ldr	r3, [r4, #3680]
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r5, r0
	cbz	r0, .L1904
.L1906:
	ldr	r0, [r4, #3688]
	mov	r5, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r4, #3688]
	b	.L1904
.L1909:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #3688]
	movs	r2, #1
	movw	r1, #65532
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r3, r1]
	movw	r2, #64504
	strh	r7, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1904:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1910:
	mvn	r5, #11
	b	.L1904
.L1913:
	.align	2
.L1912:
	.word	.LANCHOR0
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
	ldr	r3, .L1920
	mov	r0, r1
	ldr	r5, [r3, #3688]
	cbz	r5, .L1919
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L1916:
	cmp	r3, r6
	bcc	.L1918
.L1919:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1918:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L1917
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
.L1917:
	adds	r3, r3, #1
	b	.L1916
.L1921:
	.align	2
.L1920:
	.word	.LANCHOR0
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
	ldr	r2, .L1941
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #3688]
	mov	r9, r2
	cmp	r4, #0
	beq	.L1937
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L1924:
	cmp	r7, r2
	bcc	.L1932
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1937
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
	ldr	r3, [r9, #3684]
	b	.L1940
.L1932:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L1925
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L1926
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1937
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L1927:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L1928
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
.L1929:
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
	ldr	r3, .L1941
	ldr	r3, [r3, #3684]
.L1940:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L1922:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1928:
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
	b	.L1927
.L1926:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L1929
.L1925:
	adds	r7, r7, #1
	b	.L1924
.L1937:
	mov	r0, #-1
	b	.L1922
.L1942:
	.align	2
.L1941:
	.word	.LANCHOR0
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
	beq	.L1957
	ldr	r3, .L1969
	cmp	r6, r3
	beq	.L1946
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L1947
.L1968:
	mvn	r5, #13
	b	.L1945
.L1946:
	ldr	r6, .L1969+4
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
	bne	.L1953
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1968
	ldr	r2, [r4]
	ldr	r3, .L1969+8
	cmp	r2, r3
	beq	.L1951
.L1952:
	mov	r5, #-1
.L1945:
	mov	r0, r4
	bl	kfree
.L1943:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1951:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L1952
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
	bne	.L1968
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L1968
.L1959:
	mov	r5, r0
	b	.L1945
.L1947:
	ldr	r6, .L1969+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L1953
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1968
	ldr	r2, [r4]
	ldr	r3, .L1969+8
	cmp	r2, r3
	bne	.L1952
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1952
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
	cbnz	r3, .L1955
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1968
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L1959
.L1953:
	movs	r1, #8
.L1967:
	bl	__memzero
	b	.L1968
.L1955:
	mov	r1, r2
	mov	r0, r4
	b	.L1967
.L1957:
	mov	r5, #-1
	b	.L1943
.L1970:
	.align	2
.L1969:
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
	ldr	r0, .L1972
	b	misc_register
.L1973:
	.align	2
.L1972:
	.word	.LANCHOR2+12
	.fnend
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
	.type	__func__.6750, %object
	.size	__func__.6750, 17
__func__.6750:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6745, %object
	.size	__func__.6745, 17
__func__.6745:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6781, %object
	.size	__func__.6781, 17
__func__.6781:
	.ascii	"List_remove_node\000"
	.type	__func__.6813, %object
	.size	__func__.6813, 22
__func__.6813:
	.ascii	"List_update_data_list\000"
	.type	__func__.6920, %object
	.size	__func__.6920, 22
__func__.6920:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7243, %object
	.size	__func__.7243, 16
__func__.7243:
	.ascii	"make_superblock\000"
	.type	__func__.7418, %object
	.size	__func__.7418, 19
__func__.7418:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.17812, %object
	.size	__func__.17812, 17
__func__.17812:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.6855, %object
	.size	__func__.6855, 26
__func__.6855:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6282, %object
	.size	__func__.6282, 11
__func__.6282:
	.ascii	"FtlMemInit\000"
	.type	__func__.6587, %object
	.size	__func__.6587, 14
__func__.6587:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.7264, %object
	.size	__func__.7264, 18
__func__.7264:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.17771, %object
	.size	__func__.17771, 15
__func__.17771:
	.ascii	"FlashReadPages\000"
	.type	__func__.7032, %object
	.size	__func__.7032, 14
__func__.7032:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.6630, %object
	.size	__func__.6630, 11
__func__.6630:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.7089, %object
	.size	__func__.7089, 15
__func__.7089:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.17789, %object
	.size	__func__.17789, 15
__func__.17789:
	.ascii	"FlashProgPages\000"
	.type	__func__.6881, %object
	.size	__func__.6881, 31
__func__.6881:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6895, %object
	.size	__func__.6895, 16
__func__.6895:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6822, %object
	.size	__func__.6822, 16
__func__.6822:
	.ascii	"load_l2p_region\000"
	.type	__func__.6866, %object
	.size	__func__.6866, 15
__func__.6866:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6937, %object
	.size	__func__.6937, 9
__func__.6937:
	.ascii	"log2phys\000"
	.type	__func__.7152, %object
	.size	__func__.7152, 16
__func__.7152:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7305, %object
	.size	__func__.7305, 14
__func__.7305:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.7390, %object
	.size	__func__.7390, 25
__func__.7390:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7431, %object
	.size	__func__.7431, 16
__func__.7431:
	.ascii	"update_vpc_list\000"
	.type	__func__.7438, %object
	.size	__func__.7438, 20
__func__.7438:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7186, %object
	.size	__func__.7186, 22
__func__.7186:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.7010, %object
	.size	__func__.7010, 15
__func__.7010:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7289, %object
	.size	__func__.7289, 21
__func__.7289:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7411, %object
	.size	__func__.7411, 29
__func__.7411:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6406, %object
	.size	__func__.6406, 13
__func__.6406:
	.ascii	"FtlProgPages\000"
	.type	__func__.7508, %object
	.size	__func__.7508, 19
__func__.7508:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7619, %object
	.size	__func__.7619, 23
__func__.7619:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6434, %object
	.size	__func__.6434, 9
__func__.6434:
	.ascii	"FtlWrite\000"
	.type	__func__.6546, %object
	.size	__func__.6546, 14
__func__.6546:
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
	.size	g_nand_ops, 16
g_nand_ops:
	.space	16
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
	.ascii	"SFTL version: 5.0.48 20180930\000"
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
	.ascii	"write_idblock fix data %x %x %x\012\000"
.LC116:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC117:
	.ascii	"%s idb buffer alloc fail\012\000"
.LC118:
	.ascii	"vendor_storage\000"
