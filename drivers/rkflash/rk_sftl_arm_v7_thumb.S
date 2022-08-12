/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018-2022 Rockchip Electronics Co. Ltd.
 * date: 2022-08-15
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
	.eabi_attribute 18, 2
	.file	"rk_sftl.c"
	.text
	.syntax unified
	.syntax unified
	.global	__aeabi_uidiv
	.thumb
	.syntax unified
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
	mov	r7, r1
	ldr	r3, .L3
	mov	r8, r2
	ldrh	r9, [r3, #8]
	ldrh	r6, [r3, #10]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	itt	eq
	lsleq	r4, r6, #1
	lsreq	r9, r9, #1
	mov	r1, r9
	it	eq
	uxtheq	r6, r4
	ldr	r4, [r0, #4]
	lsrs	r5, r4, #10
	ubfx	r0, r4, #10, #16
	bl	__aeabi_uidiv
	uxth	r0, r0
	uxth	r5, r5
	ubfx	r4, r4, #0, #10
	mls	r5, r0, r9, r5
	mla	r4, r6, r5, r4
	str	r4, [r7]
	str	r0, [r8]
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
	.type	_copy_from_user, %function
_copy_from_user:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r6, r0
	mov	r4, r2
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #8]
	.syntax unified
@ 157 "./include/linux/uaccess.h" 1
	.syntax unified
adds r7, r1, r2; sbcscc r7, r7, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L8
	bl	arm_copy_from_user
	mov	r5, r0
	cbz	r0, .L5
.L6:
	subs	r4, r4, r5
	mov	r2, r5
	adds	r0, r6, r4
	movs	r1, #0
	bl	memset
.L5:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L8:
	mov	r5, r2
	b	.L6
	.fnend
	.size	_copy_from_user, .-_copy_from_user
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"SFTL version: 5.0.58 20220814\000"
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
	.text
	.align	1
	.global	ftl_print_sblk_info
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L13
	ldr	r1, .L13+4
	ldr	r0, .L13+8
	bl	sftl_printk
	ldr	r0, .L13+12
	ldrh	r1, [r4, #24]
	ldr	r3, [r4, #72]
	ldrh	r2, [r4, #26]
	ldrh	r3, [r3, r1, lsl #1]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #28]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	str	r3, [sp]
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #76]
	ldr	r3, [r4, #72]
	ldrh	r2, [r4, #78]
	ldr	r0, .L13+16
	ldrh	r3, [r3, r1, lsl #1]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #80]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	str	r3, [sp]
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #124]
	ldr	r3, [r4, #72]
	ldrh	r2, [r4, #126]
	ldr	r0, .L13+20
	ldrh	r3, [r3, r1, lsl #1]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #128]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	str	r3, [sp]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #172]
	ldr	r3, [r4, #72]
	ldrh	r2, [r4, #174]
	ldr	r0, .L13+24
	ldrh	r3, [r3, r1, lsl #1]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #176]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #180]	@ zero_extendqisi2
	str	r3, [sp]
	ldrb	r3, [r4, #178]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r3, [r4, #220]
	ldrh	r2, [r4, #222]
	ldrh	r1, [r4, #224]
	ldr	r0, .L13+28
	add	sp, sp, #16
	@ sp needed
	pop	{r4, lr}
	b	sftl_printk
.L14:
	.align	2
.L13:
	.word	.LANCHOR0
	.word	.LC0
	.word	.LC1
	.word	.LC2
	.word	.LC3
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.fnend
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
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
.L16:
	uxth	r3, r1
	cmp	r2, r0
	add	r1, r1, #1
	bls	.L17
	subs	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L17:
	lsls	r2, r2, #1
	b	.L16
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
	ldr	r3, .L20
	cmp	r0, #24
	it	cc
	movcc	r0, #24
	ldrh	r2, [r3, #232]
	ldrh	r1, [r3, #242]
	str	r0, [r3, #228]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #248]
	str	r2, [r3, #236]
	strh	r0, [r3, #240]	@ movhi
	subs	r2, r1, r2
	movs	r0, #0
	str	r2, [r3, #244]
	bx	lr
.L21:
	.align	2
.L20:
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
	mov	r7, r0
	ldr	r4, .L33
	ldrh	r2, [r0]
	ldrh	r1, [r7, #14]
	ldrh	r5, [r0, #2]
	ldrh	r3, [r7, #6]
	cmp	r1, #4
	ldrh	r0, [r0, #4]
	strh	r2, [r4, #252]	@ movhi
	strh	r5, [r4, #254]	@ movhi
	strh	r0, [r4, #256]	@ movhi
	strh	r3, [r4, #242]	@ movhi
	strh	r1, [r4, #258]	@ movhi
	bne	.L23
	lsrs	r3, r3, #1
	strh	r3, [r4, #242]	@ movhi
	movs	r3, #8
	strh	r3, [r4, #258]	@ movhi
.L24:
	ldr	r1, .L33+4
	movs	r3, #0
.L25:
	strb	r3, [r1], #1
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L25
	ldrh	r6, [r4, #242]
	cmp	r2, #1
	ldrh	r0, [r4, #256]
	mov	r3, #0
	it	ne
	movne	r2, #5
	strh	r3, [r4, #294]	@ movhi
	strh	r2, [r4, #292]	@ movhi
	mov	r9, #640
	strh	r9, [r4, #296]	@ movhi
	smulbb	r5, r5, r0
	smulbb	r0, r0, r6
	uxth	r5, r5
	strh	r5, [r4, #232]	@ movhi
	uxth	r0, r0
	strh	r0, [r4, #298]	@ movhi
	bl	Ftl_log2
	ldrh	fp, [r7, #12]
	ldrh	r10, [r4, #258]
	strh	r0, [r4, #300]	@ movhi
	mov	r0, r10
	strh	fp, [r4, #302]	@ movhi
	smulbb	r3, r5, fp
	strh	fp, [r4, #304]	@ movhi
	strh	r3, [r4, #306]	@ movhi
	bl	Ftl_log2
	lsl	r3, r10, #9
	mul	r1, fp, r10
	mov	r8, r0
	strh	r0, [r4, #308]	@ movhi
	uxth	r3, r3
	strh	r3, [r4, #310]	@ movhi
	asr	r9, r9, r8
	add	r8, r8, #9
	lsrs	r3, r3, #8
	strh	r3, [r4, #312]	@ movhi
	ldrh	r3, [r7, #20]
	mov	r0, #5120
	strh	r3, [r4, #314]	@ movhi
	mul	r3, r6, r5
	str	r3, [r4, #248]
	lsls	r6, r6, #6
	add	r9, r9, #2
	asr	r6, r6, r8
	muls	r3, r1, r3
	asrs	r3, r3, #11
	str	r3, [r4, #316]
	bl	__aeabi_idiv
	cmp	r0, #4
	strh	r6, [r4, #324]	@ movhi
	uxth	r6, r6
	itet	le
	movle	r3, #4
	strhgt	r0, [r4, #320]	@ movhi
	strhle	r3, [r4, #320]	@ movhi
	mov	r1, r5
	mul	r3, r6, r5
	strh	r9, [r4, #322]	@ movhi
	ldrh	r0, [r4, #320]
	str	r3, [r4, #328]
	bl	__aeabi_uidiv
	cmp	r5, #1
	uxtah	r6, r6, r0
	ite	ne
	addne	r6, r6, #8
	addeq	r6, r6, #12
	str	r6, [r4, #228]
	ldrh	r0, [r4, #228]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #228]
	ldr	r2, [r4, #244]
	movs	r0, #0
	str	r3, [r4, #332]
	ldrh	r3, [r4, #302]
	str	r0, [r4, #340]
	muls	r3, r2, r3
	ldrh	r2, [r4, #308]
	adds	r2, r2, #9
	lsls	r3, r3, #2
	lsrs	r3, r3, r2
	adds	r3, r3, #2
	strh	r3, [r4, #336]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #338]	@ movhi
	ldrh	r3, [r4, #320]
	adds	r3, r3, #3
	strh	r3, [r4, #320]	@ movhi
	ldr	r3, [r4, #328]
	adds	r3, r3, #3
	str	r3, [r4, #328]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L23:
	cmp	r1, #8
	bne	.L24
	cmp	r0, #1
	itttt	eq
	lsreq	r3, r3, #1
	strheq	r3, [r4, #242]	@ movhi
	moveq	r3, #2
	strheq	r3, [r4, #256]	@ movhi
	b	.L24
.L34:
	.align	2
.L33:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
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
	ldr	r2, .L41
	mov	r1, r0
	ldrh	r0, [r2, #344]
	cbz	r0, .L35
	ldr	r3, [r2, #348]
	ldrh	r2, [r2, #320]
	add	r2, r3, r2, lsl #1
.L37:
	cmp	r3, r2
	bne	.L38
	movs	r0, #0
	bx	lr
.L38:
	ldrh	r0, [r3], #2
	cmp	r1, r0
	bne	.L37
	movs	r0, #1
.L35:
	bx	lr
.L42:
	.align	2
.L41:
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
	ldr	r3, .L45
	ldr	r0, [r3, #340]
	bx	lr
.L46:
	.align	2
.L45:
	.word	.LANCHOR0
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.section	.rodata.str1.1
.LC7:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
	.text
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
	mov	r6, r0
	ldr	r5, .L48
	ldrh	r4, [r5, #298]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r7, r0
	mov	r1, r4
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r3, r5, r7, lsl #2
	ubfx	r2, r1, #5, #11
	and	r0, r1, #31
	movs	r4, #1
	ldr	r3, [r3, #380]
	lsls	r4, r4, r0
	ldr	r0, [r3, r2, lsl #2]
	orrs	r4, r4, r0
	ldr	r0, .L48+4
	str	r4, [r3, r2, lsl #2]
	uxth	r3, r1
	mov	r2, r7
	mov	r1, r6
	str	r4, [sp]
	bl	sftl_printk
	ldrh	r3, [r5, #358]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #358]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L49:
	.align	2
.L48:
	.word	.LANCHOR0
	.word	.LC7
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
	mov	r6, r0
	ldr	r5, .L51
	ldrh	r7, [r5, #298]
	mov	r1, r7
	bl	__aeabi_uidivmod
	mov	r0, r6
	uxth	r4, r1
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsrs	r2, r4, #5
	and	r4, r4, #31
	add	r5, r5, r0, lsl #2
	ldr	r3, [r5, #380]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L52:
	.align	2
.L51:
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
	ldr	r3, .L61
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r6, [r3, #298]
	ldrh	r3, [r3, #254]
	muls	r6, r3, r6
.L55:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L57
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L57:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L56
	adds	r4, r4, #1
	uxth	r4, r4
.L56:
	adds	r5, r5, #1
	b	.L55
.L62:
	.align	2
.L61:
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
	mov	r4, r1
	ldr	r5, .L64
	mov	r6, r0
	ldrh	r7, [r5, #256]
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	r5, [r5, #298]
	smulbb	r4, r7, r4
	mov	r1, r7
	smulbb	r5, r5, r0
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r4, r4, r1
	add	r4, r4, r5
	uxth	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L65:
	.align	2
.L64:
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
	ldr	r3, .L67
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r6, [r3, #256]
	ldrh	r1, [r3, #298]
	bl	__aeabi_uidiv
	mov	r1, r6
	smulbb	r4, r0, r6
	mov	r0, r5
	bl	__aeabi_uidivmod
	adds	r0, r4, r1
	uxth	r0, r0
	pop	{r4, r5, r6, pc}
.L68:
	.align	2
.L67:
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
	ldr	r4, .L70
	ldrh	r1, [r4, #298]
	bl	__aeabi_uidivmod
	mov	r0, r1
	ldrh	r1, [r4, #256]
	uxth	r0, r0
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L71:
	.align	2
.L70:
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
	bls	.L73
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L73:
	subs	r1, r1, r0
	cmp	r1, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
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
	ldr	r3, .L76
	ldrh	r0, [r3, #418]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L77:
	.align	2
.L76:
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
	ldr	r3, .L79
	ldrh	r0, [r3, #418]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L80:
	.align	2
.L79:
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
	ldr	r3, .L90
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #418]
	cbz	r2, .L81
	ldrh	r4, [r3, #2496]
	movs	r0, #0
	ldrh	r1, [r3, #414]
	mov	r5, r0
	ldrh	r2, [r3, #416]
	and	r4, r4, #31
.L84:
	uxth	r6, r0
	cmp	r4, r6
	bhi	.L85
	cbz	r5, .L81
	strh	r1, [r3, #414]	@ movhi
	strh	r2, [r3, #416]	@ movhi
.L81:
	pop	{r4, r5, r6, pc}
.L85:
	add	r5, r3, r1, lsl #1
	adds	r1, r1, #1
	adds	r0, r0, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r5, #420]
	add	r5, r3, r2, lsl #1
	adds	r2, r2, #1
	strh	r6, [r5, #420]	@ movhi
	ubfx	r2, r2, #0, #10
	movs	r5, #1
	b	.L84
.L91:
	.align	2
.L90:
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
	ldr	r1, .L98
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r1, #418]
	cmp	r4, #1024
	beq	.L96
	ldrh	r5, [r1, #414]
	movs	r2, #0
.L94:
	cmp	r4, r2
	bhi	.L95
.L96:
	movs	r0, #0
.L92:
	pop	{r4, r5, pc}
.L95:
	adds	r3, r5, r2
	ubfx	r3, r3, #0, #10
	add	r3, r1, r3, lsl #1
	ldrh	r3, [r3, #420]
	cmp	r3, r0
	beq	.L97
	adds	r2, r2, #1
	b	.L94
.L97:
	movs	r0, #1
	b	.L92
.L99:
	.align	2
.L98:
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
	ldr	r3, .L115
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldrh	r2, [r3, #240]
	cmp	r2, r0
	bls	.L102
	movs	r5, #6
	ldr	ip, [r3, #2516]
	movw	r2, #65535
	muls	r5, r0, r5
	add	r4, ip, r5
	strh	r2, [r4, #2]	@ movhi
	strh	r2, [ip, r5]	@ movhi
	ldr	r1, [r3, #2520]
	cbnz	r1, .L103
.L114:
	str	r4, [r3, #2520]
	b	.L102
.L103:
	ldrh	r2, [r4, #4]
	ldr	r9, [r3, #72]
	ldrh	r7, [r9, r0, lsl #1]
	cbz	r2, .L110
	muls	r7, r2, r7
.L104:
	ldr	r8, [r3, #2516]
	movw	fp, #65535
	ldr	r6, .L115+4
	sub	r2, r1, r8
	asrs	r2, r2, #1
	muls	r2, r6, r2
	ldrh	r6, [r3, #240]
	str	r6, [sp, #4]
	movs	r6, #0
	uxth	r2, r2
.L108:
	adds	r6, r6, #1
	ldr	lr, [sp, #4]
	uxth	r6, r6
	cmp	r6, lr
	bhi	.L102
	cmp	r0, r2
	beq	.L102
	ldrh	r10, [r1, #4]
	cmp	r10, #0
	beq	.L106
	ldrh	lr, [r9, r2, lsl #1]
	mul	lr, r10, lr
	cmp	lr, r7
	bcs	.L106
	ldrh	lr, [r1]
	cmp	lr, fp
	bne	.L107
	strh	r2, [r4, #2]	@ movhi
	strh	r0, [r1]	@ movhi
	str	r4, [r3, #2524]
.L102:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L110:
	mov	r7, #-1
	b	.L104
.L107:
	movs	r2, #6
	mla	r1, r2, lr, r8
	mov	r2, lr
	b	.L108
.L106:
	strh	r2, [ip, r5]	@ movhi
	ldrh	r2, [r1, #2]
	strh	r2, [r4, #2]	@ movhi
	ldr	r5, [r3, #2520]
	cmp	r1, r5
	bne	.L109
	strh	r0, [r1, #2]	@ movhi
	b	.L114
.L109:
	movs	r4, #6
	ldr	r3, [r3, #2516]
	muls	r2, r4, r2
	strh	r0, [r3, r2]	@ movhi
	strh	r0, [r1, #2]	@ movhi
	b	.L102
.L116:
	.align	2
.L115:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.section	.rodata.str1.1
.LC8:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
	.text
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
	ldr	r2, .L119
	ldrh	r3, [r2, #2528]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2528]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	bcs	.L117
	pop	{r3, lr}
	movs	r2, #214
	ldr	r1, .L119+4
	ldr	r0, .L119+8
	b	sftl_printk
.L117:
	pop	{r3, pc}
.L120:
	.align	2
.L119:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC8
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
	movw	r5, #65535
	cmp	r0, r5
	beq	.L122
	ldr	r1, .L129
	mov	lr, #6
	mul	r6, lr, r0
	ldr	r7, [r1, #2516]
	adds	r4, r7, r6
	strh	r5, [r4, #2]	@ movhi
	strh	r5, [r7, r6]	@ movhi
	ldr	r3, [r1, #2532]
	cbnz	r3, .L123
.L128:
	str	r4, [r1, #2532]
	b	.L122
.L123:
	ldr	ip, [r1, #2516]
	ldr	r10, .L129+4
	sub	r2, r3, ip
	ldr	r8, [r1, #2536]
	asrs	r2, r2, #1
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r10, r2
	uxth	r2, r2
.L126:
	ldrh	r10, [r8, r2, lsl #1]
	cmp	r10, r9
	bcs	.L124
	ldrh	r10, [r3]
	cmp	r10, r5
	bne	.L125
	strh	r2, [r4, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L122:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L125:
	mla	r3, lr, r10, ip
	mov	r2, r10
	b	.L126
.L124:
	ldrh	r5, [r3, #2]
	strh	r5, [r4, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r2, [r1, #2532]
	cmp	r2, r3
	bne	.L127
	strh	r0, [r3, #2]	@ movhi
	b	.L128
.L127:
	ldrh	r2, [r3, #2]
	movs	r4, #6
	ldr	r1, [r1, #2516]
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L122
.L130:
	.align	2
.L129:
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
	ldr	r2, .L133
	ldrh	r3, [r2, #224]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #224]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	bcs	.L131
	pop	{r3, lr}
	movs	r2, #207
	ldr	r1, .L133+4
	ldr	r0, .L133+8
	b	sftl_printk
.L131:
	pop	{r3, pc}
.L134:
	.align	2
.L133:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC8
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
	movs	r4, #6
	ldr	r8, .L146
	muls	r4, r1, r4
	movw	r3, #65535
	mov	r5, r0
	ldr	r7, [r8, #2516]
	adds	r6, r7, r4
	ldrh	r2, [r6, #2]
	cmp	r2, r3
	bne	.L136
	ldr	r3, [r0]
	cmp	r6, r3
	bne	.L137
.L140:
	ldrh	r1, [r7, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L144
	movs	r3, #0
	str	r3, [r5]
.L142:
	movw	r3, #65535
	strh	r3, [r7, r4]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L137:
	ldr	r1, .L146+4
	mov	r2, #372
	ldr	r0, .L146+8
	bl	sftl_printk
.L136:
	ldr	r3, [r5]
	ldrh	r2, [r7, r4]
	cmp	r3, r6
	beq	.L140
	movw	r1, #65535
	ldrh	r3, [r6, #2]
	cmp	r2, r1
	bne	.L143
	cmp	r3, r2
	beq	.L142
	movs	r0, #6
	ldr	r1, [r8, #2516]
	muls	r3, r0, r3
	strh	r2, [r1, r3]	@ movhi
	b	.L142
.L144:
	ldr	r3, [r8, #2516]
	movs	r0, #6
	mla	r3, r0, r1, r3
	str	r3, [r5]
	strh	r2, [r3, #2]	@ movhi
	b	.L142
.L143:
	ldr	r1, [r8, #2516]
	movs	r0, #6
	mla	r2, r0, r2, r1
	strh	r3, [r2, #2]	@ movhi
	muls	r3, r0, r3
	ldr	r2, [r8, #2516]
	ldrh	r1, [r7, r4]
	strh	r1, [r2, r3]	@ movhi
	b	.L142
.L147:
	.align	2
.L146:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC8
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
	cbz	r3, .L153
	ldr	r2, .L157
	movw	r5, #65535
	movs	r6, #6
	ldr	r4, [r2, #2516]
.L150:
	cbz	r1, .L151
	ldrh	r2, [r3]
	cmp	r2, r5
	bne	.L152
.L151:
	subs	r4, r3, r4
	ldr	r3, .L157+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
.L148:
	pop	{r4, r5, r6, pc}
.L152:
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L150
.L153:
	movw	r0, #65535
	b	.L148
.L158:
	.align	2
.L157:
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
	ldr	r2, .L166
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #2520]
	cbz	r3, .L165
	ldr	r4, [r2, #2516]
	movw	r1, #65535
	movs	r5, #6
.L162:
	cbz	r0, .L163
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L164
.L165:
	movw	r0, #65535
.L160:
	pop	{r4, r5, pc}
.L164:
	subs	r2, r0, #1
	mla	r3, r5, r3, r4
	uxth	r0, r2
	b	.L162
.L163:
	subs	r0, r3, r4
	ldr	r3, .L166+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	uxth	r0, r0
	b	.L160
.L167:
	.align	2
.L166:
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
	ldr	r4, .L179
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L170
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L170
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	beq	.L170
	movs	r6, #6
	ldr	r9, [r4, #2516]
	ldr	r3, [r4, #2520]
	muls	r6, r0, r6
	add	r8, r9, r6
	cmp	r8, r3
	beq	.L170
	ldr	r3, [r4, #72]
	ldrh	r7, [r3, r0, lsl #1]
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L177
	muls	r7, r3, r7
.L172:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L173
	ldrh	r2, [r9, r6]
	cmp	r2, r3
	bne	.L173
	ldr	r1, .L179+4
	movw	r2, #463
	ldr	r0, .L179+8
	bl	sftl_printk
.L173:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L174
	ldrh	r2, [r9, r6]
	cmp	r2, r3
	beq	.L170
.L174:
	movs	r2, #6
	ldr	r1, .L179+12
	muls	r3, r2, r3
	asrs	r2, r3, #1
	muls	r2, r1, r2
	ldr	r1, [r4, #72]
	ldrh	r2, [r1, r2, lsl #1]
	ldr	r1, [r4, #2516]
	add	r3, r3, r1
	ldrh	r3, [r3, #4]
	cbz	r3, .L178
	muls	r3, r2, r3
.L175:
	cmp	r7, r3
	bcs	.L170
	ldr	r0, .L179+16
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #2528]
	cbnz	r3, .L176
	ldr	r1, .L179+4
	mov	r2, #474
	ldr	r0, .L179+8
	bl	sftl_printk
.L176:
	ldrh	r3, [r4, #2528]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2528]	@ movhi
	bl	INSERT_DATA_LIST
.L170:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L177:
	mov	r7, #-1
	b	.L172
.L178:
	mov	r3, #-1
	b	.L175
.L180:
	.align	2
.L179:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	-1431655765
	.word	.LANCHOR0+2520
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
	ldr	r0, .L191
	movs	r1, #0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r6, #12
	movw	r7, #65535
	ldr	r3, [r0, #2540]
	ldrh	r2, [r0, #338]
	sub	r5, r3, #12
.L182:
	uxth	r4, r1
	cmp	r2, r4
	bhi	.L184
	mov	r4, r2
	add	ip, r3, #4
	movs	r1, #0
	mov	r7, #-2147483648
	mov	lr, #12
.L185:
	uxth	r6, r1
	cmp	r2, r6
	bhi	.L187
	cmp	r2, r4
	bhi	.L183
	mov	r4, r2
	ldrh	r7, [r0, #2544]
	movs	r1, #0
	mov	r0, #-1
.L188:
	uxth	r5, r1
	cmp	r2, r5
	bhi	.L190
	cmp	r2, r4
	bhi	.L183
	ldr	r1, .L191+4
	mov	r2, #796
	ldr	r0, .L191+8
	bl	sftl_printk
	b	.L183
.L184:
	adds	r1, r1, #1
	mul	ip, r6, r1
	ldrh	ip, [r5, ip]
	cmp	ip, r7
	bne	.L182
.L183:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L187:
	mul	r5, lr, r1
	ldr	r5, [ip, r5]
	cmp	r5, #0
	blt	.L186
	cmp	r5, r7
	itt	cc
	movcc	r7, r5
	movcc	r4, r6
.L186:
	adds	r1, r1, #1
	b	.L185
.L190:
	ldr	r6, [r3, #4]
	cmp	r6, r0
	bcs	.L189
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L189:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L188
.L192:
	.align	2
.L191:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
	.word	.LC8
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
	ldr	r3, .L199
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r3, #2546]
	cmp	r2, #4
	bhi	.L194
	cbnz	r0, .L194
	adds	r2, r2, #1
	strh	r2, [r3, #2546]	@ movhi
.L193:
	pop	{r4, r5, pc}
.L194:
	movs	r2, #0
	ldrh	r1, [r3, #240]
	strh	r2, [r3, #2546]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2548]
	ldr	r2, [r3, #72]
	add	r1, r2, r1, lsl #1
.L195:
	cmp	r2, r1
	beq	.L193
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2548]
	addne	r0, r0, r4
	strne	r0, [r3, #2548]
	b	.L195
.L200:
	.align	2
.L199:
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
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
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	add	r4, r2, #8
	ldr	r6, .L208
	add	r4, r0, r4, lsl #1
	movs	r3, #0
	movw	r7, #65535
	strh	r3, [r0, #4]	@ movhi
	ldrh	r5, [r6, #232]
.L202:
	cmp	r5, r2
	bhi	.L204
	ldrh	r3, [r6, #302]
	add	r4, r0, #16
	movs	r2, #0
	movw	r6, #65535
	subs	r3, r3, r1
	subs	r3, r3, #1
	sxth	r3, r3
.L205:
	uxth	r1, r2
	cmp	r5, r1
	bhi	.L207
	pop	{r4, r5, r6, r7, pc}
.L204:
	ldrh	r3, [r4], #2
	adds	r2, r2, #1
	cmp	r3, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r3, [r0, #4]
	addne	r3, r3, #1
	strhne	r3, [r0, #4]	@ movhi
	b	.L202
.L207:
	ldrh	r1, [r4], #2
	adds	r2, r2, #1
	cmp	r1, r6
	ittt	ne
	ldrhne	r1, [r0, #4]
	addne	r1, r1, r3
	strhne	r1, [r0, #4]	@ movhi
	b	.L205
.L209:
	.align	2
.L208:
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
	push	{r4, lr}
	.save {r4, lr}
	cbz	r3, .L210
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L210
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movs	r4, #0
	ldr	r1, .L216
	adds	r3, r3, #8
	ldrh	r1, [r1, #232]
	ldrh	r3, [r0, r3, lsl #1]
.L213:
	cmp	r3, r2
	beq	.L215
.L210:
	pop	{r4, pc}
.L215:
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r0, #6]
	cmp	r3, r1
	itttt	eq
	strbeq	r4, [r0, #6]
	ldrheq	r3, [r0, #2]
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r3, [r0, r3, lsl #1]
	b	.L213
.L217:
	.align	2
.L216:
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
	ldr	r8, .L224
	ldrh	r2, [r0]
	ldrh	r3, [r8, #240]
	cmp	r2, r3
	bcc	.L219
	ldr	r1, .L224+4
	movw	r2, #2157
	ldr	r0, .L224+8
	bl	sftl_printk
.L219:
	ldrh	r6, [r8, #232]
	add	r5, r4, #16
	ldr	r9, .L224+12
	movs	r3, #0
	movw	r10, #65535
	strh	r3, [r4, #4]	@ movhi
	add	r6, r4, r6, lsl #1
	strb	r3, [r4, #7]
	adds	r6, r6, #16
.L220:
	ldrh	r1, [r4]
	cmp	r6, r5
	ldrb	fp, [r4, #7]	@ zero_extendqisi2
	bne	.L222
	ldrh	r3, [r8, #302]
	movs	r0, #0
	smulbb	r3, r3, fp
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r8, #2536]
	ldrh	r2, [r3, r1, lsl #1]
	movw	r3, #10000
	cmp	r2, r3
	itt	hi
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L222:
	ldrb	r0, [r9], #1	@ zero_extendqisi2
	bl	V2P_block
	strh	r10, [r5]	@ movhi
	mov	r7, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L221
	add	r3, fp, #1
	strh	r7, [r5]	@ movhi
	strb	r3, [r4, #7]
.L221:
	adds	r5, r5, #2
	b	.L220
.L225:
	.align	2
.L224:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+260
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
	movs	r6, #0
	ldr	r5, .L234
	mov	r8, r0
	mov	r4, r6
	add	r7, r5, #260
	ldrh	r9, [r5, #232]
	ldrh	r10, [r5, #302]
.L227:
	uxth	r3, r6
	cmp	r9, r3
	bhi	.L229
	cbz	r4, .L230
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L230:
	ldr	r0, [r5, #2516]
	movs	r2, #6
	mla	r0, r2, r8, r0
	strh	r4, [r0, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L229:
	mov	r1, r8
	ldrb	r0, [r7], #1	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L228
	add	r4, r4, r10
	uxth	r4, r4
.L228:
	adds	r6, r6, #1
	b	.L227
.L235:
	.align	2
.L234:
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
	ldr	r3, .L239
	ldr	r0, [r3, #2532]
	cbz	r0, .L237
	ldr	r2, [r3, #2516]
	ldr	r3, [r3, #2536]
	subs	r0, r0, r2
	ldr	r2, .L239+4
	asrs	r0, r0, #1
	muls	r0, r2, r0
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L237:
	bx	lr
.L240:
	.align	2
.L239:
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
	ldr	r1, .L248
	mov	r2, r0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r0, [r1, #2532]
	cbz	r0, .L242
	ldrh	r3, [r1, #224]
	movs	r7, #6
	movw	ip, #65535
	rsb	r3, r3, r3, lsl #3
	asrs	r4, r3, #3
	cmp	r2, r3, asr #3
	it	gt
	uxthgt	r2, r4
	ldr	r4, [r1, #2516]
	subs	r3, r0, r4
	ldr	r0, .L248+4
	asrs	r3, r3, #1
	muls	r3, r0, r3
	movs	r0, #0
	uxth	r3, r3
.L244:
	uxth	r6, r0
	mov	r5, r3
	cmp	r6, r2
	bcs	.L245
	muls	r3, r7, r3
	adds	r0, r0, #1
	ldrh	r3, [r4, r3]
	cmp	r3, ip
	bne	.L244
.L245:
	ldr	r3, [r1, #2536]
	ldrh	r0, [r3, r5, lsl #1]
.L242:
	pop	{r4, r5, r6, r7, pc}
.L249:
	.align	2
.L248:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.section	.rodata.str1.1
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
	.text
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
	ldr	r6, .L262
	ldr	r1, .L262+4
	add	r5, r7, #12
	.pad #20
	sub	sp, sp, #20
	bl	strcpy
	ldr	r2, .L262+8
	ldr	r3, [r6, #316]
	mov	r0, r5
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r1, .L262+12
	mov	r0, r5
	adds	r5, r5, #10
	bl	strcpy
	ldr	r3, [r6, #2552]
	mov	r0, r5
	ldr	r2, .L262+16
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2548]
	mov	r0, r5
	ldr	r2, .L262+20
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2556]
	mov	r0, r5
	ldr	r2, .L262+24
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2560]
	mov	r0, r5
	ldr	r2, .L262+28
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2564]
	mov	r0, r5
	ldr	r2, .L262+32
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2568]
	mov	r0, r5
	ldr	r2, .L262+36
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2572]
	mov	r0, r5
	ldr	r2, .L262+40
	movs	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2576]
	mov	r0, r5
	ldr	r2, .L262+44
	movs	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2580]
	add	r5, r5, r0
	ldr	r2, .L262+48
	mov	r0, r5
	movs	r1, #64
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r6, #2584]
	add	r5, r5, r0
	ldr	r2, .L262+52
	movs	r1, #64
	mov	r0, r5
	lsrs	r3, r3, #11
	bl	snprintf
	adds	r4, r5, r0
	ldr	r3, [r6, #2588]
	mov	r0, r4
	ldr	r2, .L262+56
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2592]
	mov	r0, r4
	ldr	r2, .L262+60
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #358]
	mov	r0, r4
	ldr	r2, .L262+64
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #224]
	mov	r0, r4
	ldr	r2, .L262+68
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2596]
	mov	r0, r4
	ldr	r2, .L262+72
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2600]
	mov	r0, r4
	ldr	r2, .L262+76
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2604]
	mov	r0, r4
	ldr	r2, .L262+80
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2608]
	mov	r0, r4
	ldr	r2, .L262+84
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2612]
	mov	r0, r4
	ldr	r2, .L262+88
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2616]
	mov	r0, r4
	ldr	r2, .L262+92
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #2498]
	mov	r0, r4
	ldr	r2, .L262+96
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #2496]
	mov	r0, r4
	ldr	r2, .L262+100
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #340]
	mov	r0, r4
	ldr	r2, .L262+104
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #332]
	mov	r0, r4
	ldr	r2, .L262+108
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #228]
	mov	r0, r4
	ldr	r2, .L262+112
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #418]
	mov	r0, r4
	ldr	r2, .L262+116
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #240]
	mov	r0, r4
	ldr	r2, .L262+120
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #2620]
	mov	r0, r4
	ldr	r2, .L262+124
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #244]
	mov	r0, r4
	ldr	r2, .L262+128
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #2624]
	mov	r0, r4
	ldr	r2, .L262+132
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #352]
	mov	r0, r4
	ldr	r2, .L262+136
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #26]
	mov	r0, r4
	ldr	r2, .L262+140
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #30]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+144
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #24]
	mov	r0, r4
	ldr	r2, .L262+148
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+152
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #28]
	mov	r0, r4
	ldr	r2, .L262+156
	movs	r1, #64
	bl	snprintf
	ldrh	r2, [r6, #24]
	ldr	r3, [r6, #72]
	add	r4, r4, r0
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L262+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #78]
	mov	r0, r4
	ldr	r2, .L262+164
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #82]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+168
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #76]
	mov	r0, r4
	ldr	r2, .L262+172
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #84]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+176
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #80]
	mov	r0, r4
	ldr	r2, .L262+180
	movs	r1, #64
	bl	snprintf
	ldrh	r2, [r6, #76]
	ldr	r3, [r6, #72]
	add	r4, r4, r0
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L262+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #126]
	mov	r0, r4
	ldr	r2, .L262+188
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #130]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+192
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #124]
	mov	r0, r4
	ldr	r2, .L262+196
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #132]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+200
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #128]
	mov	r0, r4
	ldr	r2, .L262+204
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #174]
	mov	r0, r4
	ldr	r2, .L262+208
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #178]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+212
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #172]
	mov	r0, r4
	ldr	r2, .L262+216
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #180]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L262+220
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #176]
	mov	r0, r4
	b	.L263
.L264:
	.align	2
.L262:
	.word	.LANCHOR0
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
.L263:
	ldr	r2, .L265
	movs	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2716]
	add	r4, r4, r0
	str	r3, [sp, #4]
	ldr	r3, [r6, #2724]
	mov	r0, r4
	ldr	r2, .L265+4
	movs	r1, #64
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2712]
	mov	r0, r4
	ldr	r2, .L265+8
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2736]
	mov	r0, r4
	ldr	r2, .L265+12
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #3152]
	mov	r0, r4
	ldr	r2, .L265+16
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #3154]
	mov	r0, r4
	ldr	r2, .L265+20
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #3156]
	mov	r0, r4
	ldr	r2, .L265+24
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #3160]
	mov	r0, r4
	ldr	r2, .L265+28
	movs	r1, #64
	bl	snprintf
	add	r4, r4, r0
	movs	r1, #64
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L265+32
	mov	r3, r0
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r6, #224]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L265+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L265+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L251
.L256:
	subs	r0, r4, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L251:
	ldrh	r3, [r6, #172]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L253
	ldr	r2, [r6, #72]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L265+44
	bl	snprintf
	add	r4, r4, r0
.L253:
	movs	r0, #0
	ldr	fp, .L265+48
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r5, #0
	movw	r10, #65535
	mov	r9, #6
.L255:
	ldr	r3, [r6, #2516]
	cmp	r0, r10
	beq	.L254
	ldr	r2, [r6, #2536]
	mul	r8, r9, r0
	movs	r1, #64
	add	r3, r3, r8
	ldrh	r2, [r2, r0, lsl #1]
	str	r2, [sp, #12]
	mov	r2, fp
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r6, #72]
	ldrh	r3, [r3, r0, lsl #1]
	strd	r0, r3, [sp]
	mov	r3, r5
	mov	r0, r4
	adds	r5, r5, #1
	bl	snprintf
	ldr	r3, [r6, #2516]
	add	r4, r4, r0
	cmp	r5, #16
	ldrh	r0, [r3, r8]
	bne	.L255
.L254:
	ldr	r3, [r6, #2516]
	movs	r5, #0
	ldr	r1, [r6, #2532]
	movw	r9, #65535
	ldr	fp, .L265+52
	mov	r10, #6
	subs	r1, r1, r3
	ldr	r3, .L265+56
	asrs	r1, r1, #1
	muls	r1, r3, r1
	uxth	r1, r1
.L257:
	cmp	r1, r9
	beq	.L256
	ldr	r3, [r6, #2536]
	mul	r8, r10, r1
	mov	r0, r4
	mov	r2, fp
	ldrh	r3, [r3, r1, lsl #1]
	str	r3, [sp, #8]
	ldr	r3, [r6, #2516]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	strd	r1, r3, [sp]
	mov	r3, r5
	movs	r1, #64
	adds	r5, r5, #1
	bl	snprintf
	cmp	r5, #4
	add	r4, r4, r0
	beq	.L256
	ldr	r3, [r6, #2516]
	ldrh	r1, [r3, r8]
	b	.L257
.L266:
	.align	2
.L265:
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
	.word	-1431655765
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.section	.rodata.str1.1
.LC77:
	.ascii	"%s\012\000"
	.text
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
	cmp	r0, #2048
	mov	r5, r1
	blt	.L269
	ldr	r3, .L270
	add	r4, r5, #30
	ldr	r2, .L270+4
	movs	r1, #64
	mov	r0, r5
	bl	snprintf
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
.L267:
	pop	{r3, r4, r5, pc}
.L269:
	movs	r0, #0
	b	.L267
.L271:
	.align	2
.L270:
	.word	.LC0
	.word	.LC77
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.section	.rodata.str1.1
.LC78:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
	.text
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
	ldr	r4, .L296
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r4, #2604]
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	bcs	.L273
	ldrh	r1, [r4, #240]
	movs	r3, #0
	ldr	r0, [r4, #2536]
	str	r3, [r4, #2596]
.L274:
	ldr	r5, [r4, #2596]
	cmp	r1, r3
	bhi	.L275
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2604]
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #292]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2596]
.L276:
	ldr	r6, [r4, #2616]
	ldr	r8, [r4, #2604]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L281
	ldr	r2, [r4, #2612]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L281
.L283:
	movw	r5, #65535
.L282:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L275:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	add	r5, r5, r2
	str	r5, [r4, #2596]
	b	.L274
.L273:
	ldr	r3, [r4, #2612]
	cmp	r2, r3
	bls	.L276
	adds	r3, r3, #1
	str	r3, [r4, #2612]
	movs	r3, #0
.L278:
	ldrh	r2, [r4, #240]
	cmp	r2, r3
	bls	.L276
	ldr	r1, [r4, #2536]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L278
.L281:
	ldrh	r0, [r4, #224]
	add	r0, r0, r0, lsl #1
	lsrs	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r1, r0
	cmp	r0, r3
	bcc	.L283
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L283
	ldrh	r2, [r4, #240]
	movw	r7, #65535
	ldr	lr, [r4, #2516]
	mov	r10, r7
	ldr	r9, [r4, #2536]
	movs	r0, #0
	str	r2, [sp, #20]
	mov	r2, r7
	mov	fp, #6
.L284:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L287
	mov	r5, r2
.L286:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L283
	ldrh	r10, [r9, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L288
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r7, [r4, #2616]
.L288:
	cmp	r8, r10
	bls	.L283
	add	r3, r10, #128
	cmp	r1, r3
	ble	.L283
	add	r2, r10, #256
	ldr	r3, [r4, #2612]
	cmp	r8, r2
	bhi	.L289
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L283
.L289:
	str	r1, [sp, #8]
	mov	r1, r5
	ldr	r0, .L296+4
	ldrh	r2, [r9, fp]
	str	r2, [sp, #4]
	ldr	r2, [r4, #72]
	ldrh	r2, [r2, fp]
	str	r2, [sp]
	mov	r2, r8
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3164]
	b	.L282
.L287:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r5, r0
	bcc	.L283
	ldrh	r5, [r3, #4]
	cbz	r5, .L285
	sub	r3, r3, lr
	ldr	r5, .L296+8
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r9, r5, lsl #1]
	cmp	r6, r3
	bcs	.L286
	cmp	r3, r7
	itt	cc
	movcc	r7, r3
	movcc	r2, r5
.L285:
	mla	r3, fp, ip, lr
	b	.L284
.L297:
	.align	2
.L296:
	.word	.LANCHOR0
	.word	.LC78
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
	beq	.L299
	ldr	r2, .L300
	movs	r1, #0
	ldr	r2, [r2, #72]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L299:
	movs	r0, #0
	pop	{r3, pc}
.L301:
	.align	2
.L300:
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
	mov	r4, r0
	ldrh	r2, [r0]
	cmp	r2, r3
	bne	.L303
	ldr	r1, .L317
	movw	r2, #2792
	ldr	r0, .L317+4
	bl	sftl_printk
.L303:
	ldr	r6, .L317+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, #302]
	cmp	r2, r3
	bne	.L304
	ldr	r1, .L317
	movw	r2, #2793
	ldr	r0, .L317+4
	bl	sftl_printk
.L304:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L305
	ldr	r1, .L317
	movw	r2, #2794
	ldr	r0, .L317+4
	bl	sftl_printk
.L305:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movs	r1, #0
	strb	r1, [r4, #10]
	movw	r7, #65535
	adds	r3, r3, #8
	ldrh	r0, [r6, #232]
	ldrh	r5, [r4, r3, lsl #1]
.L306:
	ldrh	r2, [r4, #2]
	cmp	r5, r7
	beq	.L308
	ldrh	r6, [r6, #302]
	cmp	r6, r2
	bls	.L312
	ldrh	r3, [r4, #4]
	orr	r5, r2, r5, lsl #10
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movw	r7, #65535
	subs	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #4]	@ movhi
.L311:
	adds	r1, r2, #1
	uxtb	r2, r1
	cmp	r0, r2
	itttt	eq
	ldrheq	r2, [r4, #2]
	addeq	r2, r2, #1
	strheq	r2, [r4, #2]	@ movhi
	moveq	r2, #0
	add	r1, r2, #8
	ldrh	r1, [r4, r1, lsl #1]
	cmp	r1, r7
	beq	.L311
	strb	r2, [r4, #6]
	ldrh	r2, [r4, #2]
	cmp	r2, r6
	bne	.L302
	cbz	r3, .L302
	ldr	r1, .L317
	movw	r2, #2817
	ldr	r0, .L317+4
	bl	sftl_printk
.L302:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L308:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r3, r0
	ittt	eq
	strbeq	r1, [r4, #6]
	addeq	r2, r2, #1
	strheq	r2, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r5, [r4, r3, lsl #1]
	b	.L306
.L312:
	movw	r5, #65535
	b	.L302
.L318:
	.align	2
.L317:
	.word	.LANCHOR1+111
	.word	.LC8
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
	movs	r1, #0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	ip, #20
	movs	r6, #12
	movs	r7, #1
	str	r1, [r3, #3168]
.L320:
	ldrh	r2, [r3, #232]
	uxth	r0, r1
	cmp	r2, r0
	bhi	.L323
	ldr	r7, .L328
	movs	r5, #12
	movs	r6, #0
.L324:
	ldr	r1, [r3, #3188]
	cmp	r2, r1
	bcc	.L327
	pop	{r4, r5, r6, r7, r8, pc}
.L323:
	uxth	r0, r1
	ldr	r5, [r3, #3172]
	ldr	lr, [r3, #3176]
	adds	r1, r1, #1
	mul	r4, r6, r0
	adds	r2, r5, r4
	str	r7, [r2, #8]
	ldrh	r2, [r3, #310]
	muls	r2, r0, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, lr
	str	r2, [r5, r4]
	ldrh	r2, [r3, #312]
	ldr	r5, [r3, #3172]
	ldr	r8, [r3, #3180]
	add	lr, r5, r4
	muls	r2, r0, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, r8
	str	r2, [lr, #4]
	ldr	r2, [r3, #3184]
	mla	r2, ip, r0, r2
	ldr	r0, [r5, r4]
	str	r0, [r2, #8]
	ldr	r0, [lr, #4]
	str	r0, [r2, #12]
	b	.L320
.L327:
	mul	r0, r5, r2
	ldr	r4, [r3, #3172]
	ldr	ip, [r3, #3176]
	adds	r1, r4, r0
	str	r6, [r1, #8]
	ldrh	r1, [r3, #310]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	add	r1, r1, ip
	str	r1, [r4, r0]
	ldr	r4, [r7, #3180]
	ldr	r1, [r3, #3172]
	add	r0, r0, r1
	ldrh	r1, [r3, #312]
	muls	r1, r2, r1
	add	r2, r2, #1
	it	mi
	addmi	r1, r1, #3
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, r1, r4
	str	r1, [r0, #4]
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
	mov	r8, r5
	movs	r7, #20
	mov	lr, #12
	ldr	ip, [r3, #3188]
	ldr	r6, [r3, #3172]
.L331:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L330
	mla	r4, r7, r3, r0
	movs	r2, #0
.L332:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L333
	mul	r3, lr, r3
	ldr	r10, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r10
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
	ldr	r6, [r3, #3188]
	ldr	r5, [r3, #3172]
.L340:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L344
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L344:
	mov	r9, #0
.L341:
	uxth	r3, r9
	cmp	r6, r3
	bls	.L342
	mla	r3, ip, r3, r5
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
	ldr	r3, [r2, #3192]
	ldrh	r2, [r2, #3196]
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
	mov	r6, r2
	mov	r7, r1
	bl	P2V_block_in_plane
	ldr	r3, .L359
	movs	r4, #0
	ldr	r2, [r3, #3192]
	ldrh	ip, [r3, #3196]
	sub	lr, r2, #2
.L356:
	uxth	r1, r4
	cmp	ip, r1
	bhi	.L358
	itttt	eq
	strheq	r0, [r2, r4, lsl #1]	@ movhi
	ldrheq	r2, [r3, #3196]
	addeq	r2, r2, #1
	strheq	r2, [r3, #3196]	@ movhi
	b	.L357
.L358:
	adds	r4, r4, #1
	ldrh	r1, [lr, r4, lsl #1]
	cmp	r1, r0
	bne	.L356
.L357:
	ldrh	r4, [r3, #3204]
	movs	r2, #12
	muls	r4, r2, r4
	ldr	r2, [r3, #3200]
	adds	r1, r2, r4
	strd	r7, r6, [r1, #4]
	str	r5, [r2, r4]
	ldrh	r2, [r3, #3204]
	adds	r2, r2, #1
	strh	r2, [r3, #3204]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L360:
	.align	2
.L359:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.section	.rodata.str1.1
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
	.text
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
	mov	r1, r0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r0, .L364
	bl	sftl_printk
	ldr	r3, .L364+4
	ldrh	r0, [r3, #222]
	cmp	r0, r4
	beq	.L362
	ldrh	r1, [r3, #220]
	cmp	r1, r4
	beq	.L362
	movw	r2, #65535
	cmp	r0, r2
	bne	.L363
	strh	r4, [r3, #222]	@ movhi
.L362:
	movs	r0, #0
	pop	{r4, pc}
.L363:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #220]	@ movhi
	b	.L362
.L365:
	.align	2
.L364:
	.word	.LC79
	.word	.LANCHOR0
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.section	.rodata.str1.1
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
	.text
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
	ldr	r4, .L370
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldr	r0, .L370+4
	ldrh	r1, [r4, #3206]
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3206]
	addw	r1, r4, #3208
	movs	r2, #0
.L367:
	uxth	r0, r2
	cmp	r3, r0
	bhi	.L369
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3206]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3208]	@ movhi
	b	.L368
.L369:
	ldrh	r0, [r1], #2
	adds	r2, r2, #1
	cmp	r0, r5
	bne	.L367
.L368:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L371:
	.align	2
.L370:
	.word	.LANCHOR0
	.word	.LC80
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
	ldr	r4, .L378
	ldrh	r3, [r4, #3206]
	cbz	r3, .L373
	ldrh	r1, [r4, #222]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L373
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
.L373:
	movs	r0, #0
	pop	{r4, pc}
.L379:
	.align	2
.L378:
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
	movw	r1, #3265
	b	__kmalloc
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
	.section	.rodata.str1.1
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
	.text
	.align	1
	.global	rknand_print_hex
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r7, #0
	ldr	r10, .L391
	ldr	fp, .L391+4
	mov	r9, r0
	mov	r5, r1
	mov	r8, r2
	mov	r6, r7
	mov	r4, r7
	str	r3, [sp, #4]
.L383:
	ldr	r3, [sp, #4]
	cmp	r3, r4
	bhi	.L389
	ldr	r0, .L391+8
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L389:
	cbnz	r6, .L384
	ldr	r0, .L391+12
	mov	r3, r7
	mov	r2, r5
	mov	r1, r9
	bl	sftl_printk
.L384:
	cmp	r8, #4
	bne	.L385
	ldr	r1, [r5, r4, lsl #2]
	ldr	r0, .L391+16
.L390:
	adds	r6, r6, #1
	bl	sftl_printk
	cmp	r6, #15
	bls	.L388
	movs	r6, #0
	ldr	r0, .L391+8
	bl	sftl_printk
.L388:
	adds	r4, r4, #1
	add	r7, r7, r8
	b	.L383
.L385:
	cmp	r8, #2
	itete	eq
	moveq	r0, fp
	movne	r0, r10
	ldrheq	r1, [r5, r4, lsl #1]
	ldrbne	r1, [r5, r4]	@ zero_extendqisi2
	b	.L390
.L392:
	.align	2
.L391:
	.word	.LC84
	.word	.LC83
	.word	.LC85
	.word	.LC81
	.word	.LC82
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.section	.rodata.str1.1
.LC86:
	.ascii	"%s: addr: %x is in id block!!!!!!!!!!\012\000"
.LC87:
	.ascii	"not free: w: d:\000"
.LC88:
	.ascii	"not free: w: s:\000"
	.text
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	fp, .L409
	.pad #28
	sub	sp, sp, #28
	mov	r7, r2
	mov	r3, sp
	movs	r4, #0
	bic	r9, r3, #8160
	bic	r9, r9, #31
	ldrh	r10, [fp, #12]
	ldr	r3, [r9, #24]
	str	r3, [sp, #20]
	lsl	r3, r10, #3
	str	r3, [sp]
	add	r3, r2, r2, lsl #2
	str	r3, [sp, #4]
.L394:
	ldr	r3, [sp, #4]
	cmp	r4, r3
	beq	.L397
	add	r2, sp, #12
	add	r1, sp, #16
	add	r0, r6, r4, lsl #2
	mov	r5, r6
	bl	l2p_addr_tran
	ldr	r8, [sp, #12]
	cmp	r8, #0
	bne	.L395
	ldr	r2, [sp, #16]
	ldr	r3, [sp]
	cmp	r3, r2
	bls	.L395
	ldr	r6, .L409+4
	ldr	r4, .L409+8
.L396:
	mov	r3, #-1
	ldr	r2, [sp, #16]
	str	r3, [r5]
	mov	r1, r6
	mov	r0, r4
	add	r8, r8, #1
	bl	sftl_printk
	ldr	r1, [r5, #8]
	ldr	r0, .L409+12
	movs	r3, #16
	movs	r2, #4
	adds	r5, r5, #20
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r5, #-8]
	mov	r2, r3
	ldr	r0, .L409+16
	bl	rknand_print_hex
	cmp	r7, r8
	bne	.L396
	bl	dump_stack
.L397:
	ldr	r2, [sp, #20]
	ldr	r3, [r9, #24]
	cmp	r2, r3
	beq	.L404
	bl	__stack_chk_fail
.L395:
	ldr	r2, [fp, #3248]
	uxtb	r0, r8
	ldr	r1, [sp, #16]
	blx	r2
	cmp	r0, #0
	mov	r2, r0
	it	ne
	movne	r2, #-1
	str	r2, [r6, r4, lsl #2]
	ldrh	r2, [fp, #14]
	cmp	r2, #4
	bne	.L401
	ldr	r1, [sp, #16]
	ldr	r2, [fp, #3248]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r1, r1, r10
	blx	r2
	cbz	r0, .L401
	mov	r3, #-1
	str	r3, [r6, r4, lsl #2]
.L401:
	adds	r4, r4, #5
	b	.L394
.L404:
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L410:
	.align	2
.L409:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC86
	.word	.LC87
	.word	.LC88
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r2, #65533
	uxth	r3, r3
	mov	r5, r0
	cmp	r3, r2
	bhi	.L411
	ldr	r4, .L420
	ldrh	r3, [r4, #418]
	cmp	r3, #1024
	beq	.L411
	cbz	r1, .L413
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3268]
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2536]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2608]
	adds	r3, r3, #1
	str	r3, [r4, #2608]
.L413:
	ldrh	r3, [r4, #418]
	adds	r3, r3, #1
	strh	r3, [r4, #418]	@ movhi
	ldrh	r3, [r4, #416]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #416]	@ movhi
	strh	r5, [r2, #420]	@ movhi
.L411:
	pop	{r4, r5, r6, pc}
.L421:
	.align	2
.L420:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.section	.rodata.str1.1
.LC89:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
	.text
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
	ldr	r4, .L427
	ldrh	r2, [r4, #418]
	cbz	r2, .L426
	ldrh	r3, [r4, #414]
	subs	r2, r2, #1
	ldr	r0, [r4, #3268]
	strh	r2, [r4, #418]	@ movhi
	movs	r2, #1
	add	r1, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #414]	@ movhi
	ldrh	r5, [r1, #420]
	mov	r1, r2
	lsls	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2608]
	movw	r2, #65533
	adds	r3, r3, #1
	str	r3, [r4, #2608]
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L424
.L423:
	ldrh	r2, [r4, #418]
	mov	r1, r5
	ldr	r0, .L427+4
	bl	sftl_printk
.L425:
	b	.L425
.L426:
	movw	r5, #65535
	b	.L423
.L424:
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L428:
	.align	2
.L427:
	.word	.LANCHOR0
	.word	.LC89
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.section	.rodata.str1.1
.LC90:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
	.text
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
	movs	r3, #0
	ldrh	r1, [r0, #10]
	ldr	r2, [r0, #12]
.L430:
	uxth	r5, r3
	cmp	r1, r5
	bls	.L435
	mov	r7, r2
	adds	r3, r3, #1
	adds	r2, r2, #2
	ldrh	r6, [r7]
	cmp	r6, #0
	bne	.L430
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	mov	r1, r0
	uxth	r3, r3
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L431
	ldr	r3, .L437
	ldr	r0, .L437+4
	ldrh	r2, [r3, #418]
	bl	sftl_printk
.L432:
	b	.L432
.L431:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L433
.L435:
	ldr	r1, .L437+8
	movw	r2, #578
	ldr	r0, .L437+12
	bl	sftl_printk
.L433:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L438:
	.align	2
.L437:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	@ link register save eliminated.
	b	memset
	.fnend
	.size	ftl_memset, .-ftl_memset
	.section	.rodata.str1.1
.LC91:
	.ascii	"%s error allocating memory. return -1\012\000"
	.text
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
	ldr	r4, .L539
	movs	r5, #0
	movs	r7, #12
	mov	r8, #80
	ldrh	r0, [r4, #306]
	str	r5, [r4, #2588]
	str	r5, [r4, #2592]
	lsls	r0, r0, #1
	str	r5, [r4, #2576]
	str	r5, [r4, #2564]
	str	r5, [r4, #2560]
	str	r5, [r4, #2568]
	str	r5, [r4, #2572]
	str	r5, [r4, #2556]
	str	r5, [r4, #2596]
	str	r5, [r4, #2600]
	str	r5, [r4, #2608]
	str	r5, [r4, #2612]
	str	r5, [r4, #2616]
	str	r5, [r4, #3272]
	str	r5, [r4, #3164]
	str	r3, [r4, #3276]
	str	r5, [r4, #3280]
	str	r5, [r4, #3156]
	str	r5, [r4, #3284]
	strh	r3, [r4, #222]	@ movhi
	strh	r3, [r4, #220]	@ movhi
	movs	r3, #32
	strh	r5, [r4, #3160]	@ movhi
	strh	r3, [r4, #3152]	@ movhi
	movs	r3, #128
	strh	r5, [r4, #3206]	@ movhi
	strh	r3, [r4, #3154]	@ movhi
	strh	r5, [r4, #3242]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3192]
	ldrh	r0, [r4, #306]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r4, #232]
	str	r0, [r4, #3200]
	mul	r8, r8, r6
	mov	r0, r8
	bl	ftl_malloc
	movs	r3, #20
	str	r0, [r4, #3288]
	muls	r6, r3, r6
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
	ldrh	r3, [r4, #232]
	ldrh	r6, [r4, #310]
	str	r0, [r4, #3184]
	lsls	r3, r3, #1
	mov	r0, r6
	adds	r3, r3, #1
	str	r3, [r4, #3188]
	bl	ftl_malloc
	str	r0, [r4, #3300]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3308]
	ldr	r0, [r4, #3188]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	ldr	r0, [r4, #3188]
	muls	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	ldrh	r0, [r4, #258]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	ldrh	r6, [r4, #312]
	str	r0, [r4, #3328]
	muls	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3332]
	lsls	r0, r6, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #312]
	str	r0, [r4, #3336]
	ldr	r0, [r4, #3188]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3180]
	ldrh	r0, [r4, #242]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3340]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3344]
	ldrh	r0, [r4, #3340]
	addw	r0, r0, #547
	lsrs	r3, r0, #9
	and	r0, r0, #130560
	strh	r3, [r4, #3340]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r4, #242]
	str	r0, [r4, #3348]
	adds	r0, r0, #32
	str	r0, [r4, #2536]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3352]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #328]
	str	r0, [r4, #72]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #348]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3364]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r4, #322]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #322]
	mov	r1, r5
	str	r0, [r4, #3372]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #336]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldr	r0, [r4, #328]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldrh	r0, [r4, #338]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #338]
	str	r0, [r4, #2540]
	ldrh	r0, [r4, #310]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #242]
	str	r0, [r4, #3384]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2516]
	ldrh	r0, [r4, #298]
	ldrh	r3, [r4, #254]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3388]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3388]
	ldrh	r3, [r4, #254]
	mov	r5, r0
	str	r0, [r4, #380]
	movs	r1, #1
	lsls	r6, r2, #2
	add	r2, r0, r2, lsl #2
	add	r0, r4, #384
.L441:
	cmp	r3, r1
	bhi	.L442
	cmp	r3, #0
	ldr	r0, .L539+4
	mov	r1, #0
	it	eq
	moveq	r3, #1
	rsb	r2, r3, #8
	adds	r3, r3, #7
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	memset
	ldr	r3, [r4, #3356]
	cbnz	r3, .L443
.L445:
	ldr	r0, .L539+8
	ldr	r1, .L539+12
	bl	sftl_printk
	mov	r0, #-1
.L440:
	pop	{r4, r5, r6, r7, r8, pc}
.L442:
	str	r2, [r0], #4
	adds	r1, r1, #1
	add	r2, r2, r6
	b	.L441
.L540:
	.align	2
.L539:
	.word	.LANCHOR0
	.word	.LANCHOR0+352
	.word	.LC91
	.word	.LANCHOR1+173
.L443:
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3376]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #2516]
	cmp	r3, #0
	beq	.L445
	cmp	r5, #0
	beq	.L445
	ldr	r3, [r4, #72]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3192]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3200]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3332]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #2536]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #3344]
	cmp	r3, #0
	beq	.L445
	ldr	r3, [r4, #348]
	cmp	r3, #0
	beq	.L445
	ldr	r3, .L541
	ldr	r2, [r3, #3364]
	cmp	r2, #0
	beq	.L445
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L445
	ldr	r3, [r3, #3372]
	cmp	r3, #0
	beq	.L445
	movs	r0, #0
	b	.L440
.L542:
	.align	2
.L541:
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
	mov	r4, r0
	ldr	r7, .L548
	ldr	r9, .L548+4
	subs	r6, r4, #2
	ldr	r10, .L548+8
	addw	r4, r4, #1022
	mov	r5, r1
	movw	r8, #65535
	ldrh	r2, [r7, #3388]
	mov	r0, r5
	movs	r1, #0
	lsls	r2, r2, #2
	bl	ftl_memset
.L546:
	ldrh	r3, [r6, #2]!
	cmp	r3, r8
	beq	.L543
	ldrh	r2, [r7, #298]
	cmp	r2, r3
	bhi	.L545
	mov	r1, r9
	mov	r0, r10
	movs	r2, #74
	bl	sftl_printk
.L545:
	ldrh	r3, [r6]
	movs	r2, #1
	cmp	r4, r6
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r5, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r5, r1, lsl #2]
	bne	.L546
.L543:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L549:
	.align	2
.L548:
	.word	.LANCHOR0
	.word	.LANCHOR1+184
	.word	.LC8
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	ldr	r0, .L551
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	add	r0, r0, #364
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L552:
	.align	2
.L551:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
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
	ldr	r3, .L554
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	mov	r1, r4
	strh	r0, [r3, #412]	@ movhi
	add	r0, r3, #420
	strh	r4, [r3, #414]	@ movhi
	strh	r4, [r3, #416]	@ movhi
	strh	r4, [r3, #418]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L555:
	.align	2
.L554:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
	mov	r4, r0
	ldrh	r2, [r0, #10]
	ldrd	r5, r6, [r0, #20]
	ldr	r7, [r0, #12]
	lsls	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	movs	r3, #0
.L557:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L561
	ldr	r3, .L574
	movs	r6, #0
	mov	r10, r6
	ldrh	r2, [r3, #304]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	fp, [r5]
.L562:
	ldrh	r2, [r4, #10]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L565
	mov	r0, r10
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L561:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L558:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L560
	adds	r3, r3, #1
	b	.L557
.L560:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L559
	cbz	r1, .L559
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L559:
	adds	r2, r2, #1
	b	.L558
.L565:
	uxth	r8, r6
	ldrh	r9, [r5, r8, lsl #1]
	cmp	r9, fp
	bcs	.L563
	ldrh	r2, [r7, r8, lsl #1]
	cmp	r2, #0
	itt	ne
	movne	r10, r3
	movne	fp, r9
.L563:
	cmp	r9, #0
	bne	.L564
	ldrh	r0, [r7, r8, lsl #1]
	cbz	r0, .L564
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r9, [r7, r8, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L564:
	adds	r6, r6, #1
	b	.L562
.L575:
	.align	2
.L574:
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
	ldr	r4, .L579
	movw	r6, #65535
	movs	r7, #12
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3360]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #310]
	ldrh	r2, [r4, #338]
	movs	r1, #255
	ldr	r0, [r4, #3384]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r3, #0
	mov	ip, r3
.L577:
	ldrh	r1, [r4, #338]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L578
	ldr	r3, [r4, #328]
	strh	r3, [r4, #3402]	@ movhi
	mov	r3, #-1
	str	r3, [r4, #3392]
	movw	r3, #61634
	strh	r3, [r4, #3396]	@ movhi
	ldrh	r3, [r4, #3436]
	strh	r3, [r4, #3400]	@ movhi
	ldrh	r3, [r4, #336]
	strh	r3, [r4, #3398]	@ movhi
	ldr	r3, [r4, #3356]
	str	r3, [r4, #3404]
	ldr	r3, [r4, #3380]
	str	r3, [r4, #3408]
	ldr	r3, [r4, #3360]
	str	r3, [r4, #3412]
	ldr	r3, [r4, #3376]
	str	r3, [r4, #3416]
	pop	{r3, r4, r5, r6, r7, pc}
.L578:
	uxth	r5, r3
	ldr	r2, [r4, #2540]
	adds	r3, r3, #1
	mul	r0, r7, r5
	adds	r1, r2, r0
	str	ip, [r1, #4]
	strh	r6, [r2, r0]	@ movhi
	ldrh	r2, [r4, #310]
	ldr	r1, [r4, #2540]
	add	r1, r1, r0
	ldr	r0, [r4, #3384]
	muls	r2, r5, r2
	bic	r2, r2, #3
	add	r2, r2, r0
	str	r2, [r1, #8]
	b	.L577
.L580:
	.align	2
.L579:
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
	ldr	r4, .L582
	mov	r1, r5
	movw	r3, #65535
	ldrh	r2, [r4, #320]
	ldr	r0, [r4, #348]
	strh	r3, [r4, #3438]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r4, #3448]
	str	r5, [r4, #3440]
	str	r5, [r4, #3444]
	strh	r5, [r4, #344]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #2536]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #3344]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r5
	addw	r0, r4, #2468
	movs	r2, #48
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #512
	add	r0, r4, #2640
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L583:
	.align	2
.L582:
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
	mov	r10, #6
	ldr	r4, .L595
	movs	r5, #0
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	mov	r7, r5
	movw	fp, #65535
	ldrh	r2, [r4, #242]
	ldr	r0, [r4, #2516]
	mul	r2, r10, r2
	bl	ftl_memset
	str	r5, [r4, #2532]
	str	r5, [r4, #2520]
	str	r5, [r4, #2524]
	strh	r5, [r4, #2528]	@ movhi
	strh	r5, [r4, #224]	@ movhi
.L585:
	ldrh	r3, [r4, #240]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L592
	ldrh	r3, [r4, #232]
	str	r3, [sp, #4]
	ldrh	r3, [r4, #302]
	str	r3, [sp]
	movs	r3, #0
	ldr	r2, .L595+4
	mov	r6, r3
	b	.L593
.L587:
	ldrb	r0, [r2], #1	@ zero_extendqisi2
	mov	r1, r8
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldrd	r2, r3, [sp, #8]
	cbnz	r0, .L586
	ldr	r1, [sp]
	add	r6, r6, r1
	uxth	r6, r6
.L586:
	adds	r3, r3, #1
.L593:
	ldr	r0, [sp, #4]
	uxth	r1, r3
	cmp	r0, r1
	bhi	.L587
	uxth	r3, r5
	cbz	r6, .L588
	mov	r1, r6
	mov	r0, #32768
	str	r3, [sp]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r6, r0
.L589:
	ldr	r2, [r4, #2516]
	mla	r2, r10, r3, r2
	strh	r6, [r2, #4]	@ movhi
	ldrh	r2, [r4, #24]
	cmp	r2, r8
	beq	.L590
	ldrh	r2, [r4, #76]
	cmp	r2, r8
	beq	.L590
	ldrh	r2, [r4, #124]
	cmp	r2, r8
	beq	.L590
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L591
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L590:
	adds	r5, r5, #1
	b	.L585
.L588:
	ldr	r2, [r4, #72]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L589
.L591:
	adds	r7, r7, #1
	mov	r0, r8
	uxth	r7, r7
	bl	INSERT_DATA_LIST
	b	.L590
.L592:
	strh	r7, [r4, #2528]	@ movhi
	add	r7, r7, r9
	cmp	r7, r3
	strh	r9, [r4, #224]	@ movhi
	ble	.L594
	ldr	r1, .L595+8
	movw	r2, #2219
	ldr	r0, .L595+12
	bl	sftl_printk
.L594:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L596:
	.align	2
.L595:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.word	.LANCHOR1+198
	.word	.LC8
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
	movs	r1, #255
	ldr	r4, .L598
	movs	r3, #0
	ldrh	r2, [r4, #306]
	ldr	r0, [r4, #3192]
	strh	r3, [r4, #3196]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #3204]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r4, #306]
	movs	r2, #12
	ldr	r0, [r4, #3200]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r4, lr}
	b	FtlGcBufInit
.L599:
	.align	2
.L598:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	ldr	r6, .L606
	mov	r4, r0
	mov	r5, r1
	movs	r1, #255
	bl	ftl_memset
	mov	r1, r5
	ldr	r3, [r6, #3244]
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L601
	mov	r0, r4
	mov	r2, #256
	movs	r1, #255
	bl	ftl_memset
	movs	r0, #0
.L601:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L605
	mov	r3, r4
	add	r1, r4, r0, lsl #1
.L603:
	cmp	r3, r1
	bne	.L604
.L605:
	pop	{r4, r5, r6, pc}
.L604:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L603
.L607:
	.align	2
.L606:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	.section	.rodata.str1.1
.LC92:
	.ascii	"FlashReadPages %x %x error_ecc_bits %d\012\000"
.LC93:
	.ascii	"data:\000"
.LC94:
	.ascii	"spare:\000"
	.text
	.align	1
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r5, .L650
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L650+4
	mov	r3, sp
	ldr	r10, .L650+8
	bic	r8, r3, #8160
	ldr	fp, .L650+12
	bic	r8, r8, #31
	str	r1, [sp, #4]
	mov	r9, #0
	ldr	r3, [r8, #24]
	str	r3, [sp, #20]
	ldrh	r3, [r5, #12]
	str	r3, [sp]
.L610:
	ldr	r3, [sp, #4]
	cmp	r9, r3
	bne	.L624
	ldr	r2, [sp, #20]
	ldr	r3, [r8, #24]
	cmp	r2, r3
	beq	.L625
	bl	__stack_chk_fail
.L624:
	ldr	r3, [r4, #8]
	cbz	r3, .L611
	ldr	r3, [r4, #12]
	cbnz	r3, .L612
.L611:
	mov	r1, r7
	mov	r0, r10
	movs	r2, #96
	bl	sftl_printk
.L612:
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [sp, #12]
	cmp	r0, #3
	bls	.L613
	mov	r3, #-1
	str	r3, [r4]
.L614:
	add	r9, r9, #1
	adds	r4, r4, #20
	b	.L610
.L613:
	ldr	r6, [r4, #8]
	uxtb	r0, r0
	ldr	r1, [sp, #16]
	ldr	ip, [r5, #3256]
	lsls	r3, r6, #26
	ldr	r3, [r4, #12]
	it	ne
	ldrne	r6, [r5, #3320]
	mov	r2, r6
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L617
	ldr	r0, [sp]
	add	r2, r6, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #16]
	ldr	ip, [r5, #3256]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	blx	ip
	adds	r1, r0, #1
	beq	.L618
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L619
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L619
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L619
.L618:
	mov	r3, #-1
	str	r3, [r4]
.L620:
	ldr	r3, [r4]
	adds	r2, r3, #1
	beq	.L621
	cmp	r3, #256
	bne	.L617
.L621:
	ldr	r1, [r4, #4]
	mov	r0, fp
	ldr	r2, [sp, #16]
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cbz	r1, .L623
	movs	r3, #4
	ldr	r0, .L650+16
	mov	r2, r3
	bl	rknand_print_hex
.L623:
	ldr	r1, [r4, #12]
	cbz	r1, .L617
	movs	r3, #4
	ldr	r0, .L650+20
	mov	r2, r3
	bl	rknand_print_hex
.L617:
	ldr	r3, [r5, #3320]
	cmp	r3, r6
	bne	.L614
	ldr	r0, [r4, #8]
	cmp	r0, r6
	beq	.L614
	ldrh	r2, [r5, #258]
	mov	r1, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L614
.L619:
	ldr	r3, [r4]
	adds	r3, r3, #1
	beq	.L620
	cmp	r0, #256
	it	eq
	streq	r0, [r4]
	b	.L620
.L625:
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L651:
	.align	2
.L650:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
	.word	.LC8
	.word	.LC92
	.word	.LC93
	.word	.LC94
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
	ldr	r4, .L662
	movw	r10, #61664
	add	r7, r4, #364
	addw	r9, r4, #3452
	ldr	r3, [r4, #3300]
	ldr	r8, [r4, #3332]
	str	r3, [r4, #3460]
	str	r8, [r4, #3464]
.L653:
	ldrh	r3, [r4, #254]
	cmp	r3, r6
	bhi	.L658
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L658:
	ldrh	r5, [r4, #298]
	movw	r3, #65535
	strh	r3, [r7]	@ movhi
.L655:
	ldrh	r3, [r4, #298]
	subs	r5, r5, #1
	sub	r2, r3, #16
	uxth	r5, r5
	cmp	r5, r2
	ble	.L656
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r0, r9
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L655
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L655
	strh	r5, [r7]	@ movhi
.L656:
	adds	r6, r6, #1
	adds	r7, r7, #2
	b	.L653
.L663:
	.align	2
.L662:
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
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	cmp	r1, #1
	mov	r2, r1
	.pad #92
	sub	sp, sp, #92
	mov	r8, r1
	mov	r3, sp
	lsl	r6, r0, #10
	bic	r7, r3, #8160
	mov	r1, #1
	bic	r7, r7, #31
	ldr	r3, [r7, #24]
	str	r3, [sp, #84]
	ldr	r3, .L677
	ite	eq
	ldrheq	r4, [r3, #304]
	ldrhne	r4, [r3, #302]
	ldr	r3, [r3, #3324]
	subs	r4, r4, #1
	str	r3, [sp, #8]
	add	r3, sp, #20
	str	r3, [sp, #12]
	sxth	r4, r4
	orr	r0, r4, r0, lsl #10
	str	r0, [sp, #4]
	mov	r0, sp
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	bne	.L668
	mov	r9, #0
	b	.L667
.L671:
	add	r3, r9, r4
	mov	r2, r8
	mov	r0, sp
	movs	r1, #1
	add	r3, r3, r3, lsr #31
	sbfx	r5, r3, #1, #16
	orr	r3, r6, r3, asr #1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldrd	r3, r2, [sp, #20]
	ands	r3, r3, r2
	adds	r3, r3, #1
	bne	.L669
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L669
	subs	r4, r5, #1
	sxth	r4, r4
.L667:
	cmp	r9, r4
	ble	.L671
.L668:
	ldr	r2, [sp, #84]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L672
	bl	__stack_chk_fail
.L669:
	adds	r5, r5, #1
	sxth	r9, r5
	b	.L667
.L672:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L678:
	.align	2
.L677:
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
	ldr	r4, .L759
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3380]
	strh	r5, [r4, #3436]	@ movhi
	lsls	r2, r2, #2
	strh	r5, [r4, #344]	@ movhi
	bl	ftl_memset
	ldr	r2, [r4, #328]
	mov	r1, r5
	ldr	r0, [r4, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #320]
	mov	r1, r5
	ldr	r0, [r4, #3368]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r4, #320]
	mov	r1, r5
	ldr	r0, [r4, #348]
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2624
	movs	r2, #16
	movs	r1, #255
	bl	ftl_memset
	ldrh	r8, [r4, #240]
.L680:
	ldrh	r3, [r4, #242]
	cmp	r3, r8
	bls	.L720
	ldr	r5, .L759+4
	movs	r7, #0
	ldrh	r6, [r4, #232]
	mov	fp, #20
	ldrh	r1, [r4, #312]
	ldr	r10, [r4, #3288]
	add	r6, r6, r5
	ldr	r2, [r4, #3176]
	ldr	r3, [r4, #3180]
	str	r1, [sp, #4]
	b	.L721
.L683:
	mov	r1, r8
	ldrb	r0, [r5], #1	@ zero_extendqisi2
	strd	r2, r3, [sp, #8]
	bl	V2P_block
	mov	r9, r0
	bl	FtlBbmIsBadBlock
	ldrd	r2, r3, [sp, #8]
	cbnz	r0, .L721
	mla	ip, fp, r7, r10
	lsl	r1, r9, #10
	strd	r1, r2, [ip, #4]
	ldr	r1, [sp, #4]
	muls	r1, r7, r1
	add	r7, r7, #1
	it	mi
	addmi	r1, r1, #3
	uxth	r7, r7
	bic	r1, r1, #3
	add	r1, r1, r3
	str	r1, [ip, #12]
.L721:
	cmp	r6, r5
	bne	.L683
	cbnz	r7, .L684
.L719:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L680
.L684:
	mov	r1, r7
	mov	r0, r10
	movs	r2, #1
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #4]
.L718:
	ldr	r3, [sp, #4]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3288]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L687
	mov	r10, #16
	movw	fp, #65535
.L689:
	ldr	r0, [r4, #3288]
	movs	r2, #1
	mov	r1, r2
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	adds	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r6]
	cmp	r3, fp
	ldr	r3, [r4, #3288]
	bne	.L686
	mov	r2, #-1
	str	r2, [r3, r9]
	ldr	r3, [r4, #3288]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	beq	.L688
.L687:
	ldr	r2, [r4, #2588]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L736
	cmp	r2, r3
	bhi	.L690
.L736:
	adds	r0, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2588]
.L690:
	ldrh	r2, [r6]
	movw	r1, #61634
	cmp	r2, r1
	beq	.L691
	bhi	.L692
	movw	r1, #61574
	cmp	r2, r1
	beq	.L693
	movw	r1, #61604
	cmp	r2, r1
	beq	.L694
.L695:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldrh	r3, [sp, #4]
	cmp	r7, r3
	bhi	.L718
	b	.L719
.L686:
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	bne	.L687
	add	r10, r10, #-1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L689
.L688:
	movs	r1, #1
	b	.L757
.L692:
	movw	r3, #65535
	cmp	r2, r3
	bne	.L695
	movs	r1, #0
.L757:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L695
.L691:
	ldrh	r2, [r4, #3436]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L697
	ldr	r1, .L759+8
	mov	r2, #1232
	ldr	r0, .L759+12
	bl	sftl_printk
.L697:
	ldr	r1, [r4, #328]
	ldrh	r0, [r4, #3436]
	ldr	r9, [r4, #3380]
	uxth	r2, r1
	subs	r3, r2, #1
	subs	r2, r2, r0
	subs	r2, r2, #1
	add	fp, r9, #4
	sxth	r3, r3
	mov	ip, r3
	sxth	r2, r2
	str	r2, [sp, #8]
.L698:
	ldr	r2, [sp, #8]
	cmp	r2, r3
	bge	.L703
	add	r10, ip, #-1
	lsl	lr, ip, #2
	str	lr, [sp, #12]
	ldr	lr, [r6, #4]
	ldr	r2, [fp, r10, lsl #2]
	cmp	lr, r2
	bls	.L699
	ldr	r2, [r9]
	cbnz	r2, .L700
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3436]	@ movhi
.L700:
	uxth	lr, r3
	movs	r0, #0
.L701:
	uxth	r9, r0
	ldr	r1, [r4, #3380]
	sxth	r2, r0
	cmp	lr, r9
	bhi	.L702
	ldr	r0, [sp, #12]
	ldr	r2, [r6, #4]
	str	r2, [r1, r0]
	ldr	r2, [r4, #3356]
	strh	r5, [r2, ip, lsl #1]	@ movhi
.L703:
	cmp	r3, #0
	blt	.L695
	ldrh	r1, [r4, #3436]
	ldr	r2, [r4, #328]
	subs	r2, r2, r1
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r2, r3
	blt	.L695
	ldr	r2, [r4, #3380]
	adds	r1, r1, #1
	strh	r1, [r4, #3436]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3356]
.L756:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L695
.L702:
	adds	r2, r2, #1
	adds	r0, r0, #1
	ldr	r9, [r1, r2, lsl #2]
	add	r1, r1, r2, lsl #2
	str	r9, [r1, #-4]
	ldr	r1, [r4, #3356]
	ldrh	r9, [r1, r2, lsl #1]
	add	r2, r1, r2, lsl #1
	strh	r9, [r2, #-2]	@ movhi
	b	.L701
.L699:
	subs	r3, r3, #1
	mov	ip, r10
	sxth	r3, r3
	b	.L698
.L760:
	.align	2
.L759:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.word	.LANCHOR1+231
	.word	.LC8
.L693:
	ldrh	r2, [r4, #344]
	ldrh	r3, [r4, #320]
	cmp	r2, r3
	bls	.L707
	ldr	r1, .L761
	movw	r2, #1273
	ldr	r0, .L761+4
	bl	sftl_printk
.L707:
	ldrh	lr, [r4, #320]
	ldrh	r0, [r4, #344]
	add	ip, lr, #-1
	ldr	r2, [r4, #3368]
	sxth	r3, ip
	sub	ip, ip, r0
.L708:
	cmp	r3, ip
	ble	.L713
	ldr	r1, [r6, #4]
	lsl	r10, r3, #2
	ldr	r9, [r2, r3, lsl #2]
	cmp	r1, r9
	bls	.L709
	ldr	r2, [r2]
	cbnz	r2, .L710
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #344]	@ movhi
.L710:
	uxth	ip, r3
	movs	r0, #0
.L711:
	uxth	lr, r0
	ldr	r1, [r4, #3368]
	sxth	r2, r0
	cmp	ip, lr
	bhi	.L712
	ldr	r2, [r6, #4]
	str	r2, [r1, r10]
	ldr	r2, [r4, #348]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L713:
	cmp	r3, #0
	blt	.L695
	ldrh	r2, [r4, #320]
	ldrh	r1, [r4, #344]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r2, r3
	blt	.L695
	ldr	r2, [r4, #3368]
	adds	r1, r1, #1
	strh	r1, [r4, #344]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #348]
	b	.L756
.L712:
	adds	r2, r2, #1
	adds	r0, r0, #1
	ldr	lr, [r1, r2, lsl #2]
	add	r1, r1, r2, lsl #2
	str	lr, [r1, #-4]
	ldr	r1, [r4, #348]
	ldrh	lr, [r1, r2, lsl #1]
	add	r2, r1, r2, lsl #1
	strh	lr, [r2, #-2]	@ movhi
	b	.L711
.L709:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L708
.L694:
	ldrh	r1, [r4, #2624]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L715
	strh	r5, [r4, #2624]	@ movhi
.L758:
	str	r3, [r4, #2632]
	b	.L695
.L715:
	ldrh	r0, [r4, #2628]
	cmp	r0, r2
	beq	.L716
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L716:
	ldr	r2, [r4, #2632]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L717
	ldrh	r3, [r4, #2624]
	strh	r3, [r4, #2628]	@ movhi
	strh	r5, [r4, #2624]	@ movhi
	ldr	r3, [r6, #4]
	b	.L758
.L717:
	strh	r5, [r4, #2628]	@ movhi
	b	.L695
.L720:
	ldr	r1, [r4, #3356]
	ldrh	r3, [r1]
	cbz	r3, .L722
.L725:
	ldr	r1, [r4, #348]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L723
.L724:
	ldrh	r2, [r4, #3436]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L754
	ldr	r1, .L761
	movw	r2, #1398
	ldr	r0, .L761+4
	bl	sftl_printk
.L754:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L722:
	ldrh	r2, [r4, #3436]
	cmp	r2, #0
	beq	.L725
	ldr	r0, [r4, #328]
.L726:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L725
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L726
	mov	r3, r2
	movs	r5, #0
	b	.L727
.L728:
	ldr	r1, [r4, #3356]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3380]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3356]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
.L727:
	ldr	r1, [r4, #328]
	cmp	r3, r1
	bcc	.L728
	b	.L725
.L723:
	ldrh	r3, [r4, #344]
	cmp	r3, #0
	beq	.L724
	ldrh	r0, [r4, #320]
.L732:
	sxth	r3, r2
	mov	r5, r3
	cmp	r3, r0
	bge	.L724
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L732
	movs	r0, #0
.L733:
	ldrh	r2, [r4, #320]
	cmp	r3, r2
	bge	.L724
	ldr	r2, [r4, #348]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3368]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #348]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L733
.L762:
	.align	2
.L761:
	.word	.LANCHOR1+231
	.word	.LC8
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
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r7, #61649
	ldr	r4, .L794
	addw	r8, r4, #3452
	ldr	r3, [r4, #3300]
	ldr	r6, [r4, #3332]
	str	r3, [r4, #3460]
	str	r6, [r4, #3464]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #298]
	subs	r5, r5, #1
	uxth	r5, r5
.L764:
	ldrh	r3, [r4, #298]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L769
	movs	r2, #1
	mov	r0, r8
	mov	r1, r2
	lsls	r3, r5, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L765
.L768:
	ldrh	r3, [r6]
	cmp	r3, r7
	bne	.L767
	ldr	r3, [r6, #4]
	str	r3, [r4, #360]
	strh	r5, [r4, #352]	@ movhi
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #356]	@ movhi
.L769:
	ldrh	r3, [r4, #352]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L783
	ldrh	r3, [r4, #356]
	cmp	r3, r2
	beq	.L773
	movs	r2, #1
	ldr	r0, .L794+4
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L773
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L773
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #360]
	cmp	r3, r2
	bls	.L773
	str	r3, [r4, #360]
	ldrh	r2, [r4, #356]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #352]	@ movhi
	strh	r3, [r4, #356]	@ movhi
.L773:
	ldrh	r0, [r4, #352]
	movs	r1, #1
	ldr	r9, .L794+4
	bl	FtlGetLastWrittenPage
	sxth	r8, r0
	mov	r7, r0
	add	r3, r8, #1
	movs	r5, #0
	strh	r3, [r4, #354]	@ movhi
	movw	r10, #61649
.L775:
	subs	r3, r7, r5
	lsls	r3, r3, #16
	bpl	.L778
	ldr	r1, .L794+8
	movs	r2, #251
	ldr	r0, .L794+12
	bl	sftl_printk
.L777:
	ldrh	r0, [r6, #12]
	ldrh	r3, [r6, #10]
	strh	r3, [r4, #358]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L780
	ldr	r3, [r4, #228]
	cmp	r0, r3
	beq	.L780
	ldrh	r2, [r4, #242]
	lsrs	r1, r2, #2
	cmp	r3, r2, lsr #2
	bcs	.L780
	cmp	r0, r1
	bcs	.L780
	bl	FtlSysBlkNumInit
.L780:
	ldr	r6, .L794+16
	movs	r5, #0
.L781:
	ldrh	r3, [r4, #254]
	cmp	r3, r5
	bhi	.L782
	movs	r0, #0
.L763:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L765:
	ldr	r3, [r4, #3456]
	movs	r2, #1
	mov	r0, r8
	mov	r1, r2
	adds	r3, r3, #1
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	bne	.L768
.L767:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L764
.L778:
	ldrh	r2, [r4, #352]
	sub	r3, r8, r5
	mov	r0, r9
	orr	r3, r3, r2, lsl #10
	movs	r2, #1
	str	r3, [r4, #3456]
	mov	r1, r2
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L776
	ldrh	r3, [r6]
	cmp	r3, r10
	beq	.L777
.L776:
	adds	r5, r5, #1
	b	.L775
.L782:
	ldrh	r2, [r4, #3388]
	ldr	r3, [r4, #3460]
	ldr	r0, [r6], #4
	mul	r1, r5, r2
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	b	.L781
.L783:
	mov	r0, #-1
	b	.L763
.L795:
	.align	2
.L794:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+380
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.section	.rodata.str1.1
.LC95:
	.ascii	"prog read error: = %x\012\000"
.LC96:
	.ascii	"prog read REFRESH: = %x\012\000"
.LC97:
	.ascii	"prog read s error: = %x %x %x %x %x\012\000"
.LC98:
	.ascii	"prog read d error: = %x %x %x %x %x\012\000"
	.text
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
	mov	r8, r0
	ldr	r6, .L835
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	mov	r3, sp
	mov	r5, r0
	bic	r10, r3, #8160
	mov	fp, #0
	bic	r10, r10, #31
	str	r1, [sp, #12]
	str	r2, [sp, #20]
	ldr	r3, [r10, #24]
	str	r3, [sp, #52]
	ldrh	r3, [r6, #12]
	mov	r9, r3
	lsls	r3, r3, #3
	str	r3, [sp, #16]
.L797:
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bne	.L810
	movs	r0, #20
	ldr	r7, .L835+4
	ldr	r9, .L835+8
	mla	r8, r0, fp, r8
	ldr	r5, .L835
.L811:
	cmp	r8, r4
	beq	.L804
	ldr	r2, [r6, #3320]
	movs	r3, #0
	add	r0, sp, #32
	movs	r1, #1
	str	r3, [r2]
	ldr	r2, [r6, #3328]
	str	r3, [r2]
	ldr	r2, [sp, #20]
	ldr	r3, [r4, #4]
	str	r3, [sp, #36]
	ldr	r3, [r6, #3320]
	str	r3, [sp, #40]
	ldr	r3, [r6, #3328]
	str	r3, [sp, #44]
	bl	FlashReadPages
	ldr	fp, [sp, #32]
	cmp	fp, #-1
	bne	.L812
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L812:
	ldr	fp, [sp, #32]
	cmp	fp, #256
	bne	.L813
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	str	fp, [r4]
.L813:
	ldr	r3, [r4, #12]
	cbz	r3, .L814
	ldr	r1, [r5, #3328]
	ldr	r2, [r3]
	ldr	r0, [r1]
	cmp	r2, r0
	bne	.L815
	ldr	ip, [r3, #8]
	ldr	lr, [r1, #8]
	cmp	ip, lr
	beq	.L814
.L815:
	ldr	r1, [r1, #4]
	strd	r0, r1, [sp]
	ldr	r0, .L835+12
	ldr	r3, [r3, #4]
	ldr	r1, [r4, #4]
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L814:
	ldr	r3, [r4, #8]
	cbz	r3, .L816
	ldr	r1, [r5, #3320]
	ldr	r2, [r3]
	ldr	r0, [r1]
	cmp	r2, r0
	bne	.L817
	ldr	ip, [r3, #2048]
	ldr	lr, [r1, #2048]
	cmp	ip, lr
	beq	.L816
.L817:
	ldr	r1, [r1, #4]
	strd	r0, r1, [sp]
	ldr	r0, .L835+16
	ldr	r3, [r3, #4]
	ldr	r1, [r4, #4]
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L816:
	adds	r4, r4, #20
	b	.L811
.L810:
	ldr	r3, [r5, #8]
	cbz	r3, .L798
	ldr	r3, [r5, #12]
	cbnz	r3, .L799
.L798:
	ldr	r1, .L835+20
	movs	r2, #148
	ldr	r0, .L835+24
	bl	sftl_printk
.L799:
	add	r2, sp, #24
	add	r1, sp, #28
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #24]
	cmp	r7, #3
	bls	.L800
.L834:
	mov	r3, #-1
	str	r3, [r5]
	b	.L801
.L800:
	cbnz	r7, .L802
	ldr	r3, [sp, #28]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	bls	.L802
	ldr	r6, .L835+20
	ldr	r5, .L835+28
.L803:
	ldr	r2, [r4, #4]
	mov	r3, #-1
	mov	r1, r6
	str	r3, [r4]
	mov	r0, r5
	adds	r7, r7, #1
	bl	sftl_printk
	ldr	r1, [r4, #8]
	ldr	r0, .L835+32
	movs	r3, #16
	movs	r2, #4
	adds	r4, r4, #20
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L835+36
	bl	rknand_print_hex
	ldr	r3, [sp, #12]
	cmp	r3, r7
	bne	.L803
	bl	dump_stack
.L804:
	ldr	r2, [sp, #52]
	ldr	r3, [r10, #24]
	cmp	r2, r3
	beq	.L819
	bl	__stack_chk_fail
.L802:
	ldr	r1, [r5, #8]
	lsls	r3, r1, #26
	beq	.L820
	ldr	r7, [r6, #3320]
	cmp	r7, r1
	beq	.L805
	ldrh	r2, [r6, #258]
	mov	r0, r7
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L805:
	ldr	r3, [r5, #12]
	mov	r2, r7
	ldr	r1, [sp, #28]
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
	ldr	ip, [r6, #3252]
	blx	ip
	cbnz	r0, .L806
	str	r0, [r5]
.L807:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L801
	ldr	r3, [r5, #12]
	add	r2, r7, #2048
	ldr	r1, [sp, #28]
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldr	r7, [r6, #3252]
	add	r1, r1, r9
	blx	r7
	cmp	r0, #0
	bne	.L834
.L801:
	add	fp, fp, #1
	adds	r5, r5, #20
	b	.L797
.L820:
	mov	r7, r1
	b	.L805
.L806:
	mov	r3, #-1
	str	r3, [r5]
	b	.L807
.L819:
	movs	r0, #0
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L836:
	.align	2
.L835:
	.word	.LANCHOR0
	.word	.LC95
	.word	.LC96
	.word	.LC97
	.word	.LC98
	.word	.LANCHOR1+256
	.word	.LC8
	.word	.LC86
	.word	.LC87
	.word	.LC88
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r10, #0
	ldr	r4, .L882
	mov	r8, r0
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	str	r0, [r4, #3284]
.L838:
	ldrh	r1, [r4, #232]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L843
	cmp	r6, #0
	beq	.L837
	mov	r9, #0
	mov	r10, #20
	ldr	r0, [r4, #3268]
	mov	r2, r6
	movs	r1, #0
	bl	FlashEraseBlocks
.L847:
	mul	r3, r10, r9
	ldr	r2, [r4, #3268]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L846
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L846:
	add	r9, r9, #1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L847
	cmp	r7, #0
	beq	.L862
	ldrh	r3, [r4, #304]
	mov	fp, #1
	str	r3, [sp, #4]
.L848:
	mov	r10, #0
.L857:
	mov	r9, #0
	mov	r6, r9
.L849:
	ldrh	r1, [r4, #232]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L853
	cmp	r6, #0
	beq	.L837
	mov	r9, #0
	ldr	r0, [r4, #3268]
	mov	r2, fp
	mov	r1, r6
	movs	r3, #1
	bl	FlashProgPages
.L856:
	movs	r3, #20
	ldr	r2, [r4, #3268]
	mul	r3, r3, r9
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L855
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L855:
	add	r9, r9, #1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L856
	add	r10, r10, #1
	ldr	r2, [sp, #4]
	uxth	r3, r10
	cmp	r2, r3
	bhi	.L857
	mov	r9, #0
	mov	r10, #20
.L859:
	cbz	r7, .L858
	mul	r3, r10, r9
	ldr	r2, [r4, #3268]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L858
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L858:
	add	r9, r9, #1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L859
	cmp	r8, #63
	bls	.L860
	cbz	r7, .L837
.L860:
	ldr	r0, [r4, #3268]
	mov	r2, r6
	mov	r1, fp
	bl	FlashEraseBlocks
.L837:
	mov	r0, r5
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L843:
	uxth	r3, r10
	ldr	r0, [r4, #3268]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	mov	r1, r8
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	fp, r0
	cbz	r7, .L839
	bl	IsBlkInVendorPart
	cbnz	r0, .L840
.L839:
	mov	r0, fp
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L841
	ldr	r1, [r4, #3268]
	lsl	r0, fp, #10
	ldr	r3, [r4, #3316]
	mla	r1, r9, r6, r1
	str	r3, [r1, #8]
	ldrh	r3, [r4, #312]
	str	r0, [r1, #4]
	ldr	r0, [r4, #3336]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L840:
	add	r10, r10, #1
	b	.L838
.L841:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L840
.L862:
	movs	r3, #2
	mov	fp, r7
	str	r3, [sp, #4]
	b	.L848
.L853:
	uxth	r3, r9
	movs	r2, #20
	ldr	r0, [r4, #3268]
	mul	r1, r2, r3
	add	r3, r3, r4
	movs	r2, #0
	str	r2, [r0, r1]
	mov	r1, r8
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp]
	cbz	r7, .L850
	bl	IsBlkInVendorPart
	cbnz	r0, .L851
.L850:
	ldr	r0, [sp]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L851
	ldr	r1, [r4, #3268]
	movs	r3, #20
	ldr	r2, .L882
	mla	r1, r3, r6, r1
	ldr	r3, [sp]
	add	r0, r10, r3, lsl #10
	ldr	r3, [r4, #3312]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #312]
	str	r0, [r1, #4]
	ldr	r0, [r2, #3316]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L851:
	add	r9, r9, #1
	b	.L849
.L883:
	.align	2
.L882:
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r0, #11
	mov	r4, r0
	.pad #88
	sub	sp, sp, #88
	mov	r3, sp
	bic	r6, r3, #8160
	bic	r6, r6, #31
	ldr	r3, [r6, #24]
	str	r3, [sp, #84]
	bls	.L888
	ldr	r5, .L890
	add	r0, sp, #20
	movs	r2, #32
	movs	r1, #165
	str	r0, [sp, #12]
	lsls	r4, r4, #10
	ldr	r3, [r5, #3324]
	str	r3, [sp, #8]
	bl	ftl_memset
	movs	r2, #8
	movs	r1, #90
	ldr	r0, [r5, #3324]
	bl	ftl_memset
	movs	r2, #1
	mov	r0, sp
	mov	r1, r2
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
	movs	r3, #1
	mov	r0, sp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp]
	cbnz	r3, .L889
	adds	r3, r4, #1
	mov	r0, sp
	str	r3, [sp, #4]
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r5, [sp]
	subs	r5, r5, #0
	it	ne
	movne	r5, #1
	rsbs	r5, r5, #0
.L886:
	mov	r0, sp
	movs	r2, #1
	movs	r1, #0
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
.L884:
	ldr	r2, [sp, #84]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L887
	bl	__stack_chk_fail
.L889:
	mov	r5, #-1
	b	.L886
.L888:
	movs	r5, #0
	b	.L884
.L887:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L891:
	.align	2
.L890:
	.word	.LANCHOR0
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.section	.rodata.str1.1
.LC99:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC100:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC101:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
	.text
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
	ldr	r4, .L903
	movs	r1, #0
	add	r6, r4, #380
	ldr	r0, [r4, #3300]
	ldr	r3, [r4, #3332]
	ldrh	r2, [r4, #310]
	str	r0, [r4, #3460]
	str	r3, [r4, #3464]
	bl	ftl_memset
.L893:
	ldrh	r3, [r4, #254]
	cmp	r3, r5
	bgt	.L894
	ldr	r5, [r4, #3464]
	movs	r2, #16
	movs	r1, #255
	ldr	r8, .L903+4
	mov	r0, r5
	ldr	r9, .L903+8
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r5]	@ movhi
	movs	r6, #0
	mov	r7, r6
	ldr	r3, [r4, #360]
	str	r3, [r5, #4]
	ldrh	r3, [r4, #352]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #356]
	str	r3, [r5, #8]
	ldr	r3, [r4, #228]
	strh	r3, [r5, #12]	@ movhi
	ldr	r3, [r4, #2604]
	strh	r3, [r5, #14]	@ movhi
.L895:
	ldr	r3, [r4, #3300]
	mov	r0, r8
	ldrh	r2, [r4, #354]
	mov	r10, #0
	str	r3, [r4, #3460]
	ldrh	r1, [r4, #352]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	orr	r3, r2, r1, lsl #10
	str	r3, [r4, #3456]
	ldrh	r3, [r5, #10]
	str	r3, [sp]
	ldrh	r3, [r4, #356]
	str	r10, [r4, #3452]
	bl	sftl_printk
	ldrh	r3, [r4, #304]
	ldrh	r2, [r4, #354]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L896
	ldr	r3, [r4, #360]
	ldr	r0, [r4, #3268]
	adds	r3, r3, #1
	str	r3, [r4, #360]
	str	r3, [r5, #4]
	ldrh	r2, [r4, #352]
	ldrh	r3, [r4, #356]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [r4, #356]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #352]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3456]
	str	r3, [r0, #4]
	strh	r10, [r4, #354]	@ movhi
	bl	FlashEraseBlocks
.L896:
	movs	r3, #1
	mov	r0, r9
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #354]
	adds	r3, r3, #1
	strh	r3, [r4, #354]	@ movhi
	ldr	r3, [r4, #3452]
	adds	r2, r3, #1
	bne	.L897
	adds	r6, r6, #1
	ldr	r1, [r4, #3456]
	ldr	r0, .L903+12
	uxth	r6, r6
	bl	sftl_printk
	cmp	r6, #3
	bls	.L895
	ldr	r1, [r4, #3456]
	mov	r2, r6
	ldr	r0, .L903+16
	bl	sftl_printk
.L899:
	b	.L899
.L894:
	ldrh	r2, [r4, #3388]
	ldr	r3, [r4, #3460]
	ldr	r1, [r6], #4
	mul	r0, r5, r2
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L893
.L897:
	adds	r7, r7, #1
	cmp	r7, #1
	ble	.L895
	cmp	r3, #256
	beq	.L895
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L904:
	.align	2
.L903:
	.word	.LANCHOR0
	.word	.LC99
	.word	.LANCHOR0+3452
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, r0
	ldr	r4, .L937
	ldr	r9, .L937+4
.L906:
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L907
	ldr	r0, .L937+8
	mov	r1, r9
	movw	r2, #2667
	bl	sftl_printk
.L907:
	ldr	r3, .L937+12
	cmp	r5, r3
	bne	.L932
	ldrh	r3, [r4, #224]
	ldr	r1, [r4, #3164]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	movw	r3, #65535
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cmp	r1, r3
	it	eq
	moveq	r1, #0
.L908:
	ldr	r0, .L937+16
	bl	List_pop_index_node
	ldrh	r3, [r4, #224]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L909
	ldr	r0, .L937+8
	mov	r1, r9
	movw	r2, #2676
	bl	sftl_printk
.L909:
	ldrh	r3, [r4, #224]
	subs	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #240]
	cmp	r3, r8
	bls	.L906
	ldr	r3, [r4, #72]
	uxth	r6, r6
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L906
	mov	r0, r5
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L911
	ldr	r3, [r4, #72]
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L906
	ldr	r0, .L937+8
	mov	r1, r9
	movw	r2, #2690
	bl	sftl_printk
	b	.L906
.L932:
	movs	r1, #0
	b	.L908
.L911:
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L913
	ldr	r0, .L937+8
	mov	r1, r9
	movw	r2, #2693
	bl	sftl_printk
.L913:
	ldr	r0, [r4, #3268]
	add	r10, r5, #16
	ldrh	r3, [r4, #232]
	mov	ip, r10
	str	r3, [sp]
	mov	r2, r0
	movs	r3, #0
	mov	lr, r3
.L914:
	ldr	fp, [sp]
	uxth	r1, r3
	cmp	fp, r1
	bhi	.L916
	cbnz	r7, .L917
	ldr	r0, .L937+8
	mov	r1, r9
	mov	r2, #2704
	bl	sftl_printk
.L917:
	ldrh	r3, [r4, #172]
	cmp	r3, r8
	bne	.L918
	ldr	r0, .L937+8
	mov	r1, r9
	movw	r2, #2706
	bl	sftl_printk
.L918:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2536]
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r1, #0
	bne	.L919
	cmp	r3, #0
	beq	.L933
	ldrh	r1, [r4, #292]
	add	r3, r3, r1
	uxth	r3, r3
.L920:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2596]
	adds	r3, r3, #1
	str	r3, [r4, #2596]
.L921:
	ldr	r3, [r4, #2536]
	ldr	r2, [r4, #2612]
	ldrh	r0, [r4, #292]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r4, #240]
	cmp	r3, r2
	ldr	r2, [r4, #2596]
	it	hi
	strhi	r3, [r4, #2612]
	ldr	r3, [r4, #2600]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3348]
	str	r0, [r4, #2604]
	ldr	r0, [r4, #3268]
	ldr	r3, [r2, #16]
	ldr	ip, .L937+20
	adds	r3, r3, #1
	str	r3, [r2, #16]
	adds	r3, r0, #4
	movs	r2, #0
.L923:
	uxth	r1, r2
	adds	r3, r3, #20
	cmp	r7, r1
	bhi	.L924
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	bl	FlashEraseBlocks
	mov	fp, #0
	mov	r2, fp
	movs	r1, #20
.L925:
	uxth	r3, fp
	cmp	r7, r3
	bhi	.L927
	cmp	r2, #0
	ble	.L928
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L928:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L929
	ldr	r3, [r4, #72]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L906
.L916:
	strd	lr, lr, [r2, #8]
	movw	fp, #65535
	ldrh	r1, [ip], #2
	cmp	r1, fp
	beq	.L915
	mov	fp, #20
	lsls	r1, r1, #10
	mla	fp, fp, r7, r0
	adds	r7, r7, #1
	uxth	r7, r7
	str	r1, [fp, #4]
.L915:
	adds	r3, r3, #1
	adds	r2, r2, #20
	b	.L914
.L933:
	movs	r3, #2
	b	.L920
.L919:
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2600]
	adds	r3, r3, #1
	str	r3, [r4, #2600]
	b	.L921
.L924:
	ldr	r1, [r3, #-20]
	adds	r2, r2, #1
	and	r1, r1, ip
	str	r1, [r3, #-20]
	b	.L923
.L927:
	mul	r3, r1, fp
	ldr	r0, [r4, #3268]
	add	ip, r0, r3
	ldr	r3, [r0, r3]
	adds	r0, r3, #1
	bne	.L926
	ldr	r0, [ip, #4]
	adds	r2, r2, #1
	str	r3, [sp, #4]
	str	r2, [sp]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r3, [sp, #4]
	strh	r3, [r10, fp, lsl #1]	@ movhi
	movs	r1, #20
	ldr	r2, [sp]
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	subs	r3, r3, #1
	strb	r3, [r5, #7]
.L926:
	add	fp, fp, #1
	b	.L925
.L929:
	ldrh	r3, [r4, #302]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2588]
	uxth	r3, r3
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #2588]
	ldr	r2, [r4, #72]
	ldrh	r1, [r5]
	strh	r3, [r5, #4]	@ movhi
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #4]
	cbz	r3, .L930
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L931
.L930:
	ldr	r1, .L937+4
	movw	r2, #2759
	ldr	r0, .L937+8
	bl	sftl_printk
.L931:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L938:
	.align	2
.L937:
	.word	.LANCHOR0
	.word	.LANCHOR1+271
	.word	.LC8
	.word	.LANCHOR0+124
	.word	.LANCHOR0+2532
	.word	-1024
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.section	.rodata.str1.1
.LC102:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
	.text
	.align	1
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r4, .L947
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r4, #3206]
	cmp	r3, #0
	bne	.L946
.L941:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L945:
	uxtah	r3, r4, r8
	mov	r1, r9
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	r5, r0
	movs	r3, #0
.L942:
	ldrh	r1, [r4, #3206]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L944
	add	r8, r8, #1
.L940:
	ldrh	r2, [r4, #232]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L945
	bl	FtlGcReFreshBadBlk
	b	.L941
.L944:
	uxth	r7, r3
	lsls	r1, r7, #1
	str	r1, [sp, #4]
	add	r1, r4, r7, lsl #1
	ldrh	r1, [r1, #3208]
	cmp	r1, r5
	bne	.L943
	mov	r1, r5
	mov	r0, fp
	strd	r2, r3, [sp, #8]
	bl	sftl_printk
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r6, [r4, #3206]
	ldr	r2, [sp, #8]
	adds	r1, r7, #1
	subs	r0, r6, r2
	add	r1, r10, r1, lsl #1
	uxth	r0, r0
	lsls	r0, r0, #1
	cmp	r2, r6
	ite	ls
	movls	r2, r0
	movhi	r2, #0
	ldr	r0, [sp, #4]
	subs	r6, r6, #1
	add	r0, r0, r10
	bl	memmove
	ldr	r3, [sp, #12]
	strh	r6, [r4, #3206]	@ movhi
.L943:
	adds	r3, r3, #1
	b	.L942
.L946:
	ldr	fp, .L947+4
	addw	r10, r4, #3208
	mov	r8, #0
	b	.L940
.L948:
	.align	2
.L947:
	.word	.LANCHOR0
	.word	.LC102
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L961
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L950
	ldrh	r3, [r4, #222]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #220]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #220]	@ movhi
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	bne	.L953
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
.L954:
	ldr	r0, .L961+4
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #2528]
	cbnz	r3, .L956
	ldr	r1, .L961+8
	movw	r2, #2835
	ldr	r0, .L961+12
	bl	sftl_printk
.L956:
	ldrh	r3, [r4, #2528]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2528]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L960
	ldr	r1, .L961+8
	movw	r2, #2838
	ldr	r0, .L961+12
	bl	sftl_printk
.L960:
	movs	r0, #1
	b	.L949
.L953:
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L959
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L959
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L954
.L959:
	movs	r0, #0
.L949:
	pop	{r3, r4, r5, pc}
.L950:
	bl	List_update_data_list
	b	.L959
.L962:
	.align	2
.L961:
	.word	.LANCHOR0
	.word	.LANCHOR0+2520
	.word	.LANCHOR1+296
	.word	.LC8
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.section	.rodata.str1.1
.LC103:
	.ascii	"decrement_vpc_count %x = %d\012\000"
	.text
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldr	r6, .L972
	mov	r4, r0
	cmp	r0, r3
	beq	.L964
	ldr	r3, [r6, #72]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L965
	mov	r1, r0
	ldr	r0, .L972+4
	bl	sftl_printk
	ldr	r3, [r6, #72]
	ldrh	r5, [r3, r4, lsl #1]
	cbz	r5, .L966
.L970:
	movs	r5, #0
.L963:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L966:
	movw	r2, #2853
.L971:
	ldr	r1, .L972+8
	ldr	r0, .L972+12
	bl	sftl_printk
	b	.L963
.L965:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L964:
	ldrh	r0, [r6, #3438]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L968
	strh	r4, [r6, #3438]	@ movhi
	b	.L970
.L968:
	cmp	r0, r4
	beq	.L970
	bl	update_vpc_list
	ldr	r2, [r6, #2516]
	ldr	r3, [r6, #2520]
	subs	r5, r0, #0
	strh	r4, [r6, #3438]	@ movhi
	sub	r3, r3, r2
	ldr	r2, .L972+16
	it	ne
	movne	r5, #1
	asrs	r3, r3, #1
	muls	r3, r2, r3
	ldr	r2, [r6, #72]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L963
	cmp	r1, r4
	beq	.L963
	movw	r2, #2869
	b	.L971
.L973:
	.align	2
.L972:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+312
	.word	.LC8
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mvn	r2, #2
	ldr	r5, .L986
	.pad #28
	sub	sp, sp, #28
	mov	r9, #0
	mov	r3, sp
	mov	r4, r0
	bic	r8, r3, #8160
	bic	r8, r8, #31
	ldr	r7, [r5, #3332]
	str	r7, [sp, #12]
	ldr	r3, [r8, #24]
	str	r3, [sp, #20]
	mov	r3, #-1
	str	r3, [sp, #16]
	ldr	r3, [r5, #3300]
	str	r3, [sp, #8]
	mvn	r3, #1
	strd	r2, r3, [r7, #8]
	movw	r2, #61589
	ldrh	r3, [r0]
	strh	r3, [r7, #2]	@ movhi
	strh	r9, [r7]	@ movhi
	ldr	r3, [r5, #3300]
	str	r2, [r3]
	add	r2, r2, #304087040
	add	r2, r2, #1269760
	ldr	r3, [r5, #3300]
	addw	r2, r2, #1507
	str	r2, [r3, #4]
	ldrh	r6, [r0, #4]
	and	r6, r6, #1
	adds	r6, r6, #6
.L975:
	ldrh	r3, [r4, #4]
	cbz	r6, .L977
	cbnz	r3, .L976
.L977:
	ldrh	r1, [r4]
	ldr	r2, [r5, #72]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [sp, #20]
	ldrh	r3, [r5, #302]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	ldr	r3, [r8, #24]
	cmp	r2, r3
	beq	.L981
	bl	__stack_chk_fail
.L976:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L977
	ldr	r3, [r5, #2592]
	movs	r1, #1
	str	r3, [r7, #4]
	mov	r0, sp
	adds	r2, r3, #1
	adds	r3, r3, #2
	it	eq
	moveq	r2, r9
	movs	r3, #0
	str	r2, [r5, #2592]
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	subs	r6, r6, #1
	bl	decrement_vpc_count
	b	.L975
.L981:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L987:
	.align	2
.L986:
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	ldr	r7, .L1007
	bl	FtlBbtMemInit
	sub	r9, r7, #16
	sub	r4, r7, #380
	add	fp, r7, #3072
	bl	FtlLoadFactoryBbt
.L989:
	ldrh	r3, [r4, #254]
	cmp	r3, r8
	bhi	.L995
	movs	r5, #0
.L996:
	ldrh	r3, [r4, #314]
	uxth	r0, r5
	cmp	r3, r0
	bhi	.L997
	ldrh	r5, [r4, #364]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L998:
	ldrh	r3, [r4, #364]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1002
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L999
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1000
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L999:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L998
.L995:
	ldrh	r5, [r9]
	movw	r3, #65535
	ldr	r0, [r4, #3300]
	ldr	r10, [r4, #3332]
	cmp	r5, r3
	str	r0, [r4, #3460]
	str	r10, [r4, #3464]
	beq	.L990
	ldrh	r3, [r4, #298]
	movs	r2, #1
	mov	r0, fp
	mov	r1, r2
	mla	r5, r8, r3, r5
	lsls	r3, r5, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldrh	r2, [r4, #298]
	ldr	r1, [r4, #3460]
	adds	r2, r2, #7
	ldr	r0, [r7]
	lsrs	r2, r2, #3
	bl	ftl_memcpy
.L991:
	uxth	r0, r5
	add	r8, r8, #1
	adds	r7, r7, #4
	add	r9, r9, #2
	bl	FtlBbmMapBadBlock
	b	.L989
.L990:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r7]
	ldr	r0, [r4, #3460]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r4, #298]
.L993:
	subs	r6, r6, #1
	uxth	r6, r6
.L992:
	ldrh	r0, [r4, #298]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L993
	movs	r2, #16
	movs	r1, #0
	ldr	r0, [r4, #3332]
	strh	r6, [r9]	@ movhi
	bl	ftl_memset
	mov	r2, #4096
	movs	r1, #0
	ldr	r0, [r4, #3300]
	bl	ftl_memset
	movw	r3, #61664
	strh	r3, [r10]	@ movhi
	movs	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r4, #298]
	ldrh	r5, [r9]
	ldrh	r2, [r4, #3388]
	strh	r5, [r10, #2]	@ movhi
	ldr	r1, [r7]
	mla	r5, r8, r3, r5
	lsls	r2, r2, #2
	ldr	r0, [r4, #3460]
	lsls	r3, r5, #10
	str	r3, [r4, #3456]
	bl	ftl_memcpy
	movs	r2, #1
	mov	r0, fp
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	mov	r0, fp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3452]
	uxth	r0, r5
	adds	r3, r3, #1
	bne	.L991
	bl	FtlBbmMapBadBlock
	b	.L992
.L997:
	adds	r5, r5, #1
	bl	FtlBbmMapBadBlock
	b	.L996
.L1000:
	ldrh	r3, [r4, #352]
	cmp	r3, r6
	bne	.L1001
	strh	r5, [r4, #352]	@ movhi
	b	.L999
.L1001:
	strh	r5, [r4, #356]	@ movhi
.L1002:
	ldr	r0, [r4, #3268]
	movs	r5, #0
	ldrh	r3, [r4, #352]
	movs	r1, #1
	str	r5, [r4, #360]
	movs	r2, #2
	strh	r5, [r4, #354]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #356]
	lsls	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #352]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #356]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #360]
	ldrh	r2, [r4, #356]
	adds	r3, r3, #1
	str	r3, [r4, #360]
	ldrh	r3, [r4, #352]
	strh	r5, [r4, #354]	@ movhi
	strh	r2, [r4, #352]	@ movhi
	strh	r3, [r4, #356]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1008:
	.align	2
.L1007:
	.word	.LANCHOR0+380
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	.global	js_hash
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, r0
	add	r1, r1, r0
	ldr	r0, .L1013
	push	{r4, lr}
	.save {r4, lr}
.L1011:
	cmp	r3, r1
	bne	.L1012
	pop	{r4, pc}
.L1012:
	lsrs	r2, r0, #2
	ldrb	r4, [r3], #1	@ zero_extendqisi2
	add	r2, r2, r0, lsl #5
	add	r2, r2, r4
	eors	r0, r0, r2
	b	.L1011
.L1014:
	.align	2
.L1013:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.section	.rodata.str1.1
.LC104:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC105:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
	.text
	.align	1
	.global	FtlMapWritePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r5, .L1039
	ldr	r10, .L1039+4
	mov	r8, r1
	lsl	fp, r1, #2
	str	r2, [sp, #4]
	movs	r6, #0
.L1016:
	ldr	r3, [r5, #2572]
	adds	r3, r3, #1
	str	r3, [r5, #2572]
	ldrh	r3, [r5, #304]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L1017
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1018
.L1017:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1018:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L1019
	ldr	r0, .L1039+8
	mov	r1, r10
	mov	r2, #700
	bl	sftl_printk
.L1019:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1020
	ldr	r0, .L1039+8
	mov	r1, r10
	movw	r2, #701
	bl	sftl_printk
.L1020:
	ldrh	r2, [r4]
	movs	r1, #0
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3332]
	ldrh	r9, [r3, r2, lsl #1]
	ldrh	r2, [r4, #2]
	ldr	r3, [sp, #4]
	str	r3, [r5, #3460]
	orr	r2, r2, r9, lsl #10
	str	r2, [r5, #3456]
	movs	r2, #16
	str	r0, [r5, #3464]
	bl	ftl_memset
	ldr	r7, [r5, #3464]
	ldr	r2, [r4, #28]
	str	r2, [r7, #4]
	strh	r8, [r7, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r2, [r7]	@ movhi
	strh	r9, [r7, #2]	@ movhi
	ldrh	r1, [r5, #310]
	ldr	r0, [r5, #3460]
	bl	js_hash
	movs	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1039+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r5, #3452]
	adds	r3, r3, #1
	bne	.L1021
	ldr	r1, [r5, #3456]
	adds	r6, r6, #1
	ldr	r0, .L1039+16
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #304]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1023
	ldr	r1, [r5, #3456]
	mov	r2, r6
	ldr	r0, .L1039+20
	bl	sftl_printk
.L1024:
	b	.L1024
.L1023:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1016
.L1026:
	b	.L1026
.L1021:
	ldr	r3, [r4, #24]
	ldr	r2, [r5, #3456]
	str	r2, [r3, fp]
	ldrh	r3, [r4, #2]
	cmp	r3, #1
	beq	.L1027
	ldr	r3, [r5, #3452]
	cmp	r3, #256
	beq	.L1027
	ldr	r2, [r4, #36]
	cbz	r2, .L1028
.L1027:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L1016
.L1028:
	adds	r3, r3, #1
	bne	.L1029
	ldr	r1, .L1039+4
	movw	r2, #745
	ldr	r0, .L1039+8
	bl	sftl_printk
.L1029:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1040:
	.align	2
.L1039:
	.word	.LANCHOR0
	.word	.LANCHOR1+332
	.word	.LC8
	.word	.LANCHOR0+3452
	.word	.LC104
	.word	.LC105
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.section	.rodata.str1.1
.LC106:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC107:
	.ascii	"map_ppn:\000"
.LC108:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
	.text
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
	mov	r5, r0
	ldr	r4, .L1050
	mov	r6, r1
	ldrh	r3, [r4, #336]
	cmp	r3, r0
	bcs	.L1042
	ldr	r1, .L1050+4
	movw	r2, #485
	ldr	r0, .L1050+8
	bl	sftl_printk
.L1042:
	ldr	r3, [r4, #3376]
	movs	r1, #12
	muls	r6, r1, r6
	ldr	r7, [r3, r5, lsl #2]
	ldr	r3, [r4, #2540]
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	cbnz	r7, .L1043
	movs	r1, #255
	ldrh	r2, [r4, #310]
	bl	ftl_memset
	ldr	r3, [r4, #2540]
	strh	r5, [r3, r6]	@ movhi
	ldr	r1, [r4, #2540]
	add	r6, r6, r1
	str	r7, [r6, #4]
.L1044:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1043:
	ldr	r3, [r4, #3332]
	movs	r2, #1
	str	r0, [r4, #3460]
	mov	r1, r2
	ldr	r0, .L1050+12
	str	r3, [r4, #3464]
	str	r7, [r4, #3456]
	bl	FlashReadPages
	ldr	r8, [r4, #3464]
	ldrh	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1045
	ldr	r0, .L1050+16
	mov	r2, r7
	mov	r1, r5
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3464]
	mov	r2, r3
	ldr	r0, .L1050+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #336]
	ldr	r1, [r4, #3376]
	movs	r2, #4
	ldr	r0, .L1050+24
	bl	rknand_print_hex
.L1046:
	ldrh	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1048
	ldr	r1, .L1050+4
	mov	r2, #508
	ldr	r0, .L1050+8
	bl	sftl_printk
.L1048:
	ldr	r3, [r4, #2540]
	movs	r1, #0
	adds	r2, r3, r6
	str	r1, [r2, #4]
	strh	r5, [r3, r6]	@ movhi
	b	.L1044
.L1045:
	ldr	r3, [r4, #3452]
	cmp	r3, #256
	bne	.L1048
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L1050+28
	bl	sftl_printk
	ldr	r3, [r4, #2540]
	mov	r1, r5
	ldr	r0, .L1050+32
	add	r3, r3, r6
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1046
.L1051:
	.align	2
.L1050:
	.word	.LANCHOR0
	.word	.LANCHOR1+348
	.word	.LC8
	.word	.LANCHOR0+3452
	.word	.LC106
	.word	.LC94
	.word	.LC107
	.word	.LC108
	.word	.LANCHOR0+3392
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
	ldr	r5, .L1068
	ldr	r7, [r0, #12]
	ldr	r10, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	subs	r3, r3, #5
	cmp	r2, r3
	blt	.L1053
	uxth	r0, r0
	ldrh	r9, [r7, r0, lsl #1]
	cmp	r9, #0
	beq	.L1053
	ldr	r6, [r4, #32]
	cbnz	r6, .L1053
	movs	r3, #1
	str	r3, [r4, #32]
	strh	r6, [r7, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #304]
	cmp	r2, r3
	bcc	.L1055
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1055:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L1062
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L1053:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #304]
	cmp	r2, r3
	bcc	.L1063
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1063:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1062:
	uxth	r7, r6
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	ldr	r2, [r10, r7, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L1056
	ldr	r2, [r5, #3304]
	ldr	r8, [r5, #3332]
	str	r2, [r5, #3460]
	str	r8, [r5, #3464]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3456]
	movs	r2, #1
	ldr	r0, .L1068+4
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L1057
	ldr	r1, .L1068+8
	movw	r2, #611
	ldr	r0, .L1068+12
	bl	sftl_printk
.L1057:
	ldr	r2, [r5, #3452]
	adds	r2, r2, #1
	bne	.L1058
.L1060:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L1059:
	b	.L1059
.L1058:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L1060
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1060
	ldr	r2, [r5, #3460]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1056:
	adds	r6, r6, #1
	b	.L1055
.L1069:
	.align	2
.L1068:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LANCHOR1+364
	.word	.LC8
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r2, #65535
	mov	r4, r0
	ldrh	r3, [r0]
	ldr	r5, [r0, #12]
	cmp	r3, r2
	bne	.L1071
	ldrh	r3, [r0, #8]
	cbz	r3, .L1072
	ldr	r1, .L1080
	movw	r2, #641
	ldr	r0, .L1080+4
	bl	sftl_printk
.L1072:
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5]	@ movhi
	movs	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #28]
	adds	r3, r3, #1
	str	r3, [r4, #28]
.L1073:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1071:
	ldrh	r7, [r5, r3, lsl #1]
	movs	r1, #255
	ldr	r5, .L1080+8
	ldrh	r3, [r0, #2]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #3332]
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #3456]
	ldr	r3, [r5, #3300]
	str	r3, [r5, #3460]
	ldr	r3, [r0, #28]
	str	r6, [r5, #3464]
	str	r3, [r6, #4]
	movw	r3, #64245
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r3, [r6]	@ movhi
	strh	r7, [r6, #2]	@ movhi
	ldrh	r2, [r5, #304]
	ldr	r0, [r5, #3300]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L1074:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1076
	ldrh	r1, [r5, #310]
	ldr	r0, [r5, #3460]
	bl	js_hash
	movs	r2, #1
	str	r0, [r6, #12]
	mov	r1, r2
	movs	r3, #0
	ldr	r0, .L1080+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1073
.L1076:
	uxth	r0, r2
	ldr	r1, [r8, r0, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L1075
	adds	r3, r3, #1
	ldr	r1, [r5, #3300]
	uxth	r3, r3
	str	r0, [r1, r3, lsl #3]
	ldr	r1, [r5, #3300]
	ldr	r0, [r8, r0, lsl #2]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1075:
	adds	r2, r2, #1
	b	.L1074
.L1081:
	.align	2
.L1080:
	.word	.LANCHOR1+379
	.word	.LC8
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
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
	ldr	r5, .L1083
	muls	r4, r0, r4
	add	r0, r5, #3392
	ldr	r3, [r5, #2540]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r0, [r5, #2540]
	add	r4, r4, r0
	movs	r0, #0
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L1084:
	.align	2
.L1083:
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
	mov	r5, r0
	ldr	r9, .L1097
	mov	r6, r1
	mov	r7, r2
	ldrh	r4, [r9, #308]
	adds	r3, r4, #7
	movs	r4, #1
	lsls	r4, r4, r3
	subs	r4, r4, #1
	lsr	r8, r0, r3
	ands	r4, r4, r0
	ldr	r3, [r9, #2552]
	uxth	r8, r8
	uxth	r4, r4
	cmp	r3, r0
	bhi	.L1086
	ldr	r1, .L1097+4
	mov	r2, #820
	ldr	r0, .L1097+8
	bl	sftl_printk
	ldr	r3, [r9, #2552]
	cmp	r3, r5
	bhi	.L1086
	mov	r0, #-1
	cbnz	r7, .L1085
	str	r0, [r6]
.L1085:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1092:
	adds	r3, r3, #1
	mul	r0, r10, r3
	ldrh	r0, [r2, r0]
	cmp	r0, r8
	bne	.L1088
.L1089:
	movs	r1, #12
	ldr	r3, [r9, #2540]
	muls	r5, r1, r5
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	cbnz	r7, .L1090
	ldr	r3, [r3, r4, lsl #2]
	str	r3, [r6]
.L1091:
	ldr	r1, [r9, #2540]
	add	r5, r5, r1
	ldr	r3, [r5, #4]
	adds	r2, r3, #1
	beq	.L1095
	adds	r3, r3, #1
	str	r3, [r5, #4]
.L1095:
	movs	r0, #0
	b	.L1085
.L1090:
	ldr	r2, [r6]
	str	r2, [r3, r4, lsl #2]
	strh	r8, [r9, #2544]	@ movhi
	ldr	r3, [r9, #2540]
	add	r3, r3, r5
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L1091
.L1086:
	ldr	r2, [r9, #2540]
	movs	r3, #0
	ldrh	r1, [r9, #338]
	mov	r10, #12
	subs	r2, r2, #12
.L1088:
	uxth	r5, r3
	cmp	r1, r5
	bhi	.L1092
	bl	select_l2p_ram_region
	mul	r10, r10, r0
	ldr	r3, [r9, #2540]
	mov	r5, r0
	add	r2, r3, r10
	ldrh	r1, [r3, r10]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1093
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1093
	bl	flush_l2p_region
.L1093:
	mov	r1, r5
	mov	r0, r8
	bl	load_l2p_region
	b	.L1089
.L1098:
	.align	2
.L1097:
	.word	.LANCHOR0
	.word	.LANCHOR1+410
	.word	.LC8
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlWriteDump_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L1118
	.pad #32
	sub	sp, sp, #32
	mov	r3, sp
	bic	r8, r3, #8160
	bic	r8, r8, #31
	ldrh	r2, [r4, #28]
	ldr	r3, [r8, #24]
	str	r3, [sp, #28]
	cmp	r2, #0
	beq	.L1100
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1100
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	ldrh	r1, [r4, #302]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1100
	ldrb	r6, [r4, #34]	@ zero_extendqisi2
	cbnz	r6, .L1099
	ldr	r7, [r4, #2552]
	mov	r2, r6
	add	r1, sp, #4
	ldrh	r9, [r4, #232]
	subs	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r5, [r4, #3332]
	ldr	r3, [sp, #4]
	ldr	r0, [r4, #3300]
	str	r3, [sp, #12]
	adds	r3, r3, #1
	str	r7, [sp, #24]
	str	r0, [sp, #16]
	str	r5, [sp, #20]
	str	r6, [r5, #4]
	beq	.L1102
	mov	r2, r6
	add	r0, sp, #8
	movs	r1, #1
	bl	FlashReadPages
.L1103:
	lsl	r6, r9, #2
	ldr	r9, .L1118+4
	movw	r3, #61589
	mov	r10, #0
	strh	r3, [r5]	@ movhi
.L1104:
	cbnz	r6, .L1108
.L1105:
	movs	r3, #1
.L1117:
	strb	r3, [r4, #34]
.L1099:
	ldr	r2, [sp, #28]
	ldr	r3, [r8, #24]
	cmp	r2, r3
	beq	.L1110
	bl	__stack_chk_fail
.L1102:
	ldrh	r2, [r4, #310]
	movs	r1, #255
	bl	ftl_memset
	b	.L1103
.L1108:
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	beq	.L1105
	ldr	r3, [sp, #12]
	mov	r0, r9
	str	r3, [r5, #12]
	subs	r6, r6, #1
	ldrh	r3, [r4, #24]
	strh	r3, [r5, #2]	@ movhi
	str	r7, [r5, #8]
	bl	get_new_active_ppa
	ldr	r3, [r4, #2592]
	str	r0, [sp, #12]
	movs	r1, #1
	adds	r2, r3, #1
	str	r3, [r5, #4]
	adds	r3, r3, #2
	add	r0, sp, #8
	it	eq
	moveq	r2, r10
	movs	r3, #0
	str	r2, [r4, #2592]
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #24]
	bl	decrement_vpc_count
	b	.L1104
.L1100:
	movs	r3, #0
	b	.L1117
.L1110:
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1119:
	.align	2
.L1118:
	.word	.LANCHOR0
	.word	.LANCHOR0+24
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
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
	mov	r5, r0
	ldr	r6, .L1130
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	mov	r4, r0
	ldr	r3, [r6, #72]
	ldrh	r2, [r3, r0, lsl #1]
	cmp	r2, #0
	bne	.L1121
	ldr	r3, [r6, #2532]
	cbz	r3, .L1122
	ldr	r1, [r6, #2516]
	mov	ip, #6
	ldr	r0, .L1130+4
	movw	lr, #65535
	subs	r3, r3, r1
	ldrh	r7, [r6, #224]
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r3, r3
.L1123:
	uxth	r0, r2
	cmp	r7, r0
	bls	.L1122
	cmp	r3, r4
	bne	.L1124
	ldr	r0, .L1130+8
	mov	r1, r4
	bl	List_remove_node
	ldrh	r3, [r6, #224]
	cbnz	r3, .L1125
	ldr	r1, .L1130+12
	movw	r2, #1742
	ldr	r0, .L1130+16
	bl	sftl_printk
.L1125:
	ldrh	r3, [r6, #224]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r6, #224]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #72]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
.L1122:
	add	r1, sp, #4
	mov	r0, r5
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1124:
	mul	r3, ip, r3
	adds	r2, r2, #1
	ldrh	r3, [r1, r3]
	cmp	r3, lr
	bne	.L1123
	b	.L1122
.L1121:
	adds	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
	b	.L1122
.L1131:
	.align	2
.L1130:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2532
	.word	.LANCHOR1+419
	.word	.LC8
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.section	.rodata.str1.1
.LC109:
	.ascii	"data prev_ppa = %x error...................\012\000"
.LC110:
	.ascii	"spuer block %x vpn is 0\012 \000"
	.text
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
	movw	r1, #65535
	mov	r10, r0
	.pad #52
	sub	sp, sp, #52
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	str	r3, [sp, #44]
	ldrh	r3, [r0]
	cmp	r3, r1
	beq	.L1134
	ldr	r5, .L1264
	ldrh	r3, [r0, #2]
	str	r3, [sp]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldrh	r2, [r5, #302]
	str	r3, [sp, #24]
	ldr	r3, [sp]
	cmp	r2, r3
	mov	r3, #0
	bne	.L1135
	strh	r3, [r0, #4]	@ movhi
	strh	r2, [r0, #2]	@ movhi
.L1262:
	strb	r3, [r10, #6]
.L1134:
	mov	r3, sp
	ldr	r2, [sp, #44]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1203
	bl	__stack_chk_fail
.L1135:
	ldrh	r0, [r0, #16]
.L1136:
	uxth	r6, r3
	cmp	r0, r1
	add	r3, r3, #1
	beq	.L1137
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	mov	r4, r0
	adds	r0, r4, #1
	beq	.L1138
	ldr	r3, [r5, #3176]
	movs	r2, #0
	ldrh	fp, [r5, #232]
	movw	r9, #65535
	mov	lr, r3
	add	r3, r10, #16
	ldr	r0, [r5, #3288]
	mov	ip, r3
	ldrh	r6, [r5, #310]
	ldr	r8, [r5, #3180]
	ldrh	r7, [r5, #312]
	mov	r5, r2
	str	r3, [sp, #20]
.L1139:
	uxth	r3, r2
	cmp	fp, r3
	bhi	.L1145
	ldr	r6, .L1264
	movs	r2, #0
	mov	r1, r5
	movs	r7, #0
	bl	FlashReadPages
	movw	r9, #65535
	uxth	r2, r4
	str	r2, [sp, #8]
	ldr	r3, [r6, #2592]
	add	fp, r3, #-1
	ldr	r3, [r6, #3288]
	mov	r8, r3
.L1146:
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L1151
	bne	.L1149
	adds	r2, r4, #1
	uxth	r2, r2
	str	r2, [sp, #8]
.L1256:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #302]
	ldr	r2, [sp, #8]
	str	r0, [sp, #4]
	cmp	r3, r2
	bne	.L1153
	movs	r3, #0
	strh	r2, [r10, #2]	@ movhi
	strb	r3, [r10, #6]
	strh	r3, [r10, #4]	@ movhi
.L1153:
	ldr	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bne	.L1154
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bne	.L1154
	ldr	r1, [sp, #8]
	mov	r2, r3
.L1263:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1134
.L1137:
	uxth	r2, r3
	adds	r2, r2, #8
	ldrh	r0, [r10, r2, lsl #1]
	b	.L1136
.L1138:
	ldr	r3, [sp]
	cbz	r3, .L1140
	ldr	r1, .L1264+4
	movw	r2, #1809
	ldr	r0, .L1264+8
	bl	sftl_printk
.L1140:
	ldr	r3, [sp, #24]
	cbz	r3, .L1141
	cmp	r3, r6
	beq	.L1141
	ldr	r1, .L1264+4
	movw	r2, #1810
	ldr	r0, .L1264+8
	bl	sftl_printk
.L1141:
	movs	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1262
.L1145:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1142
	movs	r1, #20
	orr	r3, r4, r3, lsl #10
	mla	r1, r1, r5, r0
	str	r3, [r1, #4]
	mov	r3, r6
	muls	r3, r5, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, lr
	str	r3, [r1, #8]
	mov	r3, r7
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r5, r5
	bic	r3, r3, #3
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1142:
	adds	r2, r2, #1
	b	.L1139
.L1151:
	ldr	r2, [r8]
	cbnz	r2, .L1147
	ldr	r2, [r8, #12]
	str	r2, [sp, #4]
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	beq	.L1148
	ldr	r1, [r6, #2592]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1148
	adds	r2, r2, #1
	str	r2, [r6, #2592]
.L1148:
	ldr	r2, [sp, #4]
	ldr	r2, [r2]
	adds	r2, r2, #1
	bne	.L1150
.L1149:
	uxth	r2, r4
	str	r2, [sp, #8]
	movs	r2, #20
	mla	r3, r2, r7, r3
	b	.L1256
.L1147:
	ldr	r9, [sp, #8]
.L1150:
	adds	r7, r7, #1
	add	r8, r8, #20
	b	.L1146
.L1154:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1155
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cbnz	r3, .L1156
.L1155:
	ldr	r3, [r6, #3448]
	uxth	r2, r4
	uxth	r7, r4
	movw	r8, #65535
	adds	r3, r3, #1
	ldr	r3, [sp]
	it	eq
	streq	fp, [r6, #3448]
	adds	r3, r3, #7
	ldr	r6, .L1264
	cmp	r3, r2
	itet	lt
	sublt	r4, r4, #7
	ldrge	r4, [sp]
	uxthlt	r4, r4
.L1159:
	ldr	r0, [r6, #3288]
	cmp	r4, r7
	ldrh	ip, [r6, #232]
	bls	.L1205
	mov	r3, #-1
	str	r3, [r6, #3448]
.L1156:
	ldr	r4, .L1264
	movs	r3, #1
	ldr	r5, [sp]
	strh	r3, [r4, #3472]	@ movhi
.L1167:
	ldrh	r6, [r4, #232]
	movs	r3, #0
	ldr	r0, [r4, #3288]
	movw	r7, #65535
	ldr	r1, [sp, #20]
	mov	ip, #20
	str	r3, [sp, #12]
.L1168:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1170
	ldr	r1, [sp, #12]
	movs	r2, #0
	bl	FlashReadPages
	movs	r3, #0
.L1261:
	str	r3, [sp, #16]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #16]
	cmp	r2, r3
	bhi	.L1199
	adds	r5, r5, #1
	ldrh	r3, [r4, #302]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1167
	ldrh	r2, [r4, #232]
	movs	r3, #0
	strh	r5, [r10, #2]	@ movhi
	movw	r0, #65535
	strh	r3, [r10, #4]	@ movhi
.L1200:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1134
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	str	r1, [sp, #20]
	adds	r1, r3, #1
	cmp	r4, r0
	bne	.L1262
	mov	r3, r1
	b	.L1200
.L1161:
	ldrh	r2, [r1], #2
	cmp	r2, r8
	beq	.L1160
	mla	r9, lr, r5, r0
	adds	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r9, #4]
.L1160:
	adds	r3, r3, #1
.L1166:
	uxth	r2, r3
	cmp	r2, ip
	bcc	.L1161
	mov	r1, r5
	movs	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3288]
	movs	r1, #0
.L1162:
	uxth	r2, r1
	cmp	r5, r2
	bhi	.L1165
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1159
.L1205:
	movs	r3, #0
	ldr	r1, [sp, #20]
	mov	r5, r3
	mov	lr, #20
	b	.L1166
.L1265:
	.align	2
.L1264:
	.word	.LANCHOR0
	.word	.LANCHOR1+435
	.word	.LC8
.L1165:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1156
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r8
	beq	.L1164
	ldr	r2, [r2, #4]
	adds	r0, r2, #1
	it	ne
	strne	r2, [r6, #3448]
.L1164:
	adds	r1, r1, #1
	adds	r3, r3, #20
	b	.L1162
.L1170:
	ldrh	r2, [r1], #2
	cmp	r2, r7
	beq	.L1169
	ldr	lr, [sp, #12]
	orr	r2, r5, r2, lsl #10
	mla	lr, ip, lr, r0
	str	r2, [lr, #4]
	ldr	r2, [sp, #12]
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1169:
	adds	r3, r3, #1
	b	.L1168
.L1199:
	ldr	r3, [sp, #16]
	movs	r6, #20
	ldr	r9, [r4, #3288]
	muls	r6, r3, r6
	ldr	r3, [sp]
	add	r8, r9, r6
	cmp	r5, r3
	ldr	r7, [r8, #4]
	str	r7, [sp, #40]
	bcc	.L1172
	ubfx	r0, r7, #10, #16
	bl	P2V_plane
	ldr	r3, [sp]
	cmp	r5, r3
	bne	.L1173
	ldr	r3, [sp, #24]
	cmp	r3, r0
	bhi	.L1172
.L1173:
	ldr	r3, [sp, #8]
	cmp	r5, r3
	bne	.L1174
	ldr	r3, [sp, #4]
	cmp	r3, r0
	bne	.L1174
	mov	r2, r3
	mov	r1, r5
	strb	r3, [r10, #6]
	strh	r5, [r10, #2]	@ movhi
	b	.L1263
.L1174:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L1175
	ldr	r8, [r8, #12]
	movw	r2, #61589
	ldrh	r3, [r8]
	cmp	r3, r2
	beq	.L1176
	ldrh	r0, [r10]
.L1258:
	bl	decrement_vpc_count
	b	.L1172
.L1176:
	ldr	fp, [r8, #4]
	cmp	fp, #-1
	beq	.L1178
	ldr	r1, [r4, #2592]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cbz	r0, .L1178
	add	r2, fp, #1
	str	r2, [r4, #2592]
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1178
	ldr	r1, .L1266
	movw	r2, #1961
	ldr	r0, .L1266+4
	bl	sftl_printk
.L1178:
	ldrd	r7, r3, [r8, #8]
	add	r1, sp, #36
	movs	r2, #0
	str	r3, [sp, #32]
	mov	r0, r7
	bl	log2phys
	ldr	r1, [r4, #3448]
	adds	r0, r1, #1
	beq	.L1180
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1180
	ldr	r3, [sp, #32]
	adds	r1, r3, #1
	beq	.L1181
	ldr	r0, [r4, #3288]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3288]
	add	r9, r3, r6
	ldr	r2, [r3, r6]
	adds	r2, r2, #1
	bne	.L1182
.L1183:
	mov	r3, #-1
	str	r3, [sp, #32]
.L1190:
	ldr	r0, [sp, #32]
	adds	r1, r0, #1
	beq	.L1172
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #240]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1195
	ldr	r1, .L1266
	movw	r2, #2066
	ldr	r0, .L1266+4
	bl	sftl_printk
.L1195:
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1196
	mov	r0, r6
	b	.L1258
.L1181:
	ldrd	r2, r3, [sp, #36]
	cmp	r2, r3
	bne	.L1172
	add	r1, sp, #32
	mov	r0, r7
	movs	r2, #1
	bl	log2phys
.L1172:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L1261
.L1182:
	ldr	r2, [r8, #8]
	cmp	r2, r7
	bne	.L1183
	ldr	r1, [r8, #4]
	ldr	r0, [r4, #3448]
	str	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1183
	ldrd	r1, r0, [sp, #32]
	ldr	ip, [sp, #40]
	cmp	r0, ip
	bne	.L1185
	mov	r0, r2
.L1257:
	bl	FtlReUsePrevPpa
	b	.L1183
.L1185:
	cmp	r1, r0
	beq	.L1183
	adds	r2, r0, #1
	beq	.L1186
	str	r0, [r9, #4]
	movs	r2, #0
	mov	r0, r9
	movs	r1, #1
	ldr	r8, [r9, #12]
	bl	FlashReadPages
.L1187:
	ldr	r3, [r4, #3288]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L1188
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #3448]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L1188
	ldr	r0, [sp, #28]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1183
.L1188:
	ldr	r1, [sp, #32]
	mov	r0, r7
	b	.L1257
.L1186:
	str	r0, [r3, r6]
	b	.L1187
.L1180:
	ldrd	r2, r3, [sp, #36]
	cmp	r2, r3
	beq	.L1190
	ldr	r1, [sp, #32]
	adds	r0, r1, #1
	beq	.L1192
	ldr	r3, [r4, #248]
	cmp	r3, r1, lsr #10
	bhi	.L1192
	ldr	r0, .L1266+8
.L1260:
	bl	sftl_printk
	b	.L1172
.L1192:
	add	r1, sp, #40
	mov	r0, r7
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #36]
	cmp	r8, #-1
	beq	.L1190
	ldr	r3, [sp, #32]
	cmp	r8, r3
	beq	.L1190
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L1194
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L1194
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L1190
.L1194:
	ldr	r0, [r4, #3288]
	movs	r2, #0
	movs	r1, #1
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3288]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1190
	ldr	r1, [r6, #4]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1190
	add	r1, sp, #36
	mov	r0, r7
	movs	r2, #1
	bl	log2phys
	b	.L1190
.L1196:
	ldr	r0, .L1266+12
	mov	r1, r6
	b	.L1260
.L1175:
	ldr	r3, [r4, #3476]
	cmp	r3, #31
	itttt	ls
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3476]
	strls	r7, [r2, #3480]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3448]
	adds	r2, r3, #1
	bne	.L1198
.L1259:
	str	fp, [r4, #3448]
	b	.L1172
.L1198:
	cmp	r3, fp
	bls	.L1172
	b	.L1259
.L1203:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1267:
	.align	2
.L1266:
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LC109
	.word	.LC110
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.section	.rodata.str1.1
.LC111:
	.ascii	"...%s enter...\012\000"
.LC112:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC113:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
	.text
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
	ldr	r5, .L1293
	mov	r3, sp
	ldr	r7, .L1293+4
	bic	r6, r3, #8160
	ldr	r1, .L1293+8
	bic	r6, r6, #31
	ldr	r0, .L1293+12
	ldr	r3, [r6, #24]
	str	r3, [sp, #4]
	bl	sftl_printk
	ldr	r0, .L1293+4
	mov	r2, #8192
	movs	r1, #0
	bl	ftl_memset
.L1269:
	ldr	r3, [r5, #2552]
	cmp	r3, r4
	bhi	.L1271
	ldr	r9, .L1293+4
	movs	r4, #0
	ldr	fp, .L1293+16
	mov	r7, r4
	movw	r10, #65535
.L1272:
	ldrh	r2, [r5, #240]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1274
	ldr	r4, [r5, #2532]
	cbz	r4, .L1275
	ldr	r1, [r5, #2516]
	mov	r9, #0
	ldrh	r8, [r5, #224]
	mov	fp, #6
	subs	r4, r4, r1
	ldr	r1, .L1293+20
	ldr	r10, .L1293+4
	asrs	r4, r4, #1
	ldr	r0, .L1293+24
	muls	r4, r1, r4
	uxth	r4, r4
.L1276:
	uxth	r3, r9
	cmp	r8, r3
	bls	.L1275
	ldr	r3, [r5, #72]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L1277
	ldrh	r3, [r10, r4, lsl #1]
	mov	r1, r4
	bl	sftl_printk
	ldr	r0, .L1293+24
	movs	r7, #1
.L1277:
	mul	r4, fp, r4
	ldr	r3, [r5, #2516]
	add	r9, r9, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1276
.L1275:
	cbz	r7, .L1268
	ldr	r1, .L1293+8
	movw	r2, #2394
	ldr	r0, .L1293+28
	bl	sftl_printk
.L1268:
	ldr	r2, [sp, #4]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1280
	bl	__stack_chk_fail
.L1271:
	mov	r1, sp
	mov	r0, r4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	adds	r3, r0, #1
	beq	.L1270
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L1270:
	adds	r4, r4, #1
	b	.L1269
.L1274:
	ldr	r3, [r5, #72]
	uxth	r8, r4
	ldrh	r2, [r3, r8, lsl #1]
	ldrh	r3, [r9, r8, lsl #1]
	cmp	r2, r3
	beq	.L1273
	mov	r1, r8
	mov	r0, fp
	bl	sftl_printk
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r8, lsl #1]
	cmp	r3, r10
	beq	.L1273
	ldrh	r2, [r9, r8, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L1273:
	adds	r4, r4, #1
	b	.L1272
.L1280:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1294:
	.align	2
.L1293:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+457
	.word	.LC111
	.word	.LC112
	.word	-1431655765
	.word	.LC113
	.word	.LC8
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.section	.rodata.str1.1
.LC114:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC115:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC116:
	.ascii	"lpa = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
	.text
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
	ldr	r4, .L1310
	.pad #32
	sub	sp, sp, #32
	ldr	r8, .L1310+4
	mov	r3, sp
	ldr	r0, .L1310+8
	bic	r6, r3, #8160
	movs	r1, #0
	bic	r6, r6, #31
	ldr	r3, [r6, #24]
	str	r3, [sp, #28]
	bl	sftl_printk
.L1296:
	ldr	r3, [r4, #2552]
	cmp	r3, r5
	bhi	.L1302
	ldr	r2, [sp, #28]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1303
	bl	__stack_chk_fail
.L1302:
	add	r1, sp, #24
	mov	r0, r5
	movs	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L1297
	ldr	r2, [sp, #24]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L1297:
	ldr	r3, [sp, #24]
	adds	r2, r3, #1
	beq	.L1299
	ldr	r7, [r4, #3332]
	movs	r2, #0
	str	r3, [r4, #3456]
	movs	r1, #1
	ldr	r3, [r4, #3300]
	ldr	r0, .L1310+12
	str	r3, [r4, #3460]
	str	r5, [r4, #3468]
	str	r7, [r4, #3464]
	str	r2, [r4, #3452]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmp	r3, #256
	beq	.L1300
	adds	r3, r3, #1
	beq	.L1300
	ldr	r3, [r7, #8]
	cmp	r3, r5
	beq	.L1299
.L1300:
	ldr	r2, [r4, #3460]
	ldr	r3, [r4, #3464]
	ldr	r0, .L1310+16
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
	ldr	r2, [r4, #3456]
	ldr	r3, [r3]
	bl	sftl_printk
.L1299:
	adds	r5, r5, #1
	b	.L1296
.L1303:
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1311:
	.align	2
.L1310:
	.word	.LANCHOR0
	.word	.LC115
	.word	.LC114
	.word	.LANCHOR0+3452
	.word	.LC116
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.section	.rodata.str1.1
.LC117:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
	.text
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	.pad #52
	sub	sp, sp, #52
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	str	r1, [sp, #4]
	ldr	r3, [r3, #24]
	str	r3, [sp, #44]
	ldr	r3, .L1349
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1339
	cbz	r6, .L1314
.L1313:
	ldr	r3, .L1349+4
	ldr	r2, [sp, #4]
	ldrh	r3, [r3, #302]
	cmp	r3, r2
	bne	.L1315
.L1314:
	bl	FtlGcPageVarInit
.L1315:
	mov	r9, #0
.L1334:
	ldrh	r2, [r5]
	movs	r3, #0
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1316
	ldr	r4, .L1349+4
.L1317:
	ldr	r3, [r4, #3176]
	movs	r2, #0
	ldrh	ip, [r4, #232]
	add	lr, r5, #16
	ldr	r0, [r4, #3288]
	mov	r8, r3
	ldrh	fp, [r4, #310]
	mov	r7, r2
	ldr	r10, [r4, #3180]
	ldrh	r3, [r4, #312]
	str	r3, [sp, #8]
.L1318:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1322
	mov	r10, #0
	mov	r1, r7
	movs	r2, #0
	bl	FlashReadPages
.L1323:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1335
	ldr	r3, [sp, #4]
	adds	r6, r6, #1
	add	r9, r9, #1
	uxth	r6, r6
	cmp	r3, r9
	bhi	.L1336
	ldr	r2, .L1349
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1336
	add	r3, r3, r9
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r4, #302]
	cmp	r3, r6
	bls	.L1316
.L1337:
	mov	r3, sp
	ldr	r2, [sp, #44]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1338
	bl	__stack_chk_fail
.L1339:
	movs	r6, #0
	b	.L1313
.L1322:
	ldrh	r3, [lr], #2
	movw	r1, #65535
	cmp	r3, r1
	beq	.L1319
	movs	r1, #20
	orr	r3, r6, r3, lsl #10
	mla	r1, r1, r7, r0
	str	r3, [r1, #4]
	mov	r3, fp
	muls	r3, r7, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r8
	str	r3, [r1, #8]
	ldr	r3, [sp, #8]
	muls	r3, r7, r3
	add	r7, r7, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r7, r7
	bic	r3, r3, #3
	add	r3, r3, r10
	str	r3, [r1, #12]
.L1319:
	adds	r2, r2, #1
	b	.L1318
.L1335:
	movs	r3, #20
	ldr	r2, [r4, #3288]
	mul	fp, r3, r10
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	ldr	r3, [r1, #4]
	ldr	r8, [r1, #12]
	cmp	r2, #0
	bne	.L1324
	ldrh	r0, [r8]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1347
	ldr	r0, [r8, #8]
	ldr	r1, [r4, #2552]
	cmp	r0, r1
	bls	.L1326
.L1347:
	ldrh	r2, [r5]
	movs	r1, #0
	ldr	r3, [r4, #72]
	movs	r6, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r5]	@ movhi
	strh	r3, [r4, #172]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1334
.L1326:
	add	r1, sp, #20
	strd	r2, r3, [sp, #8]
	bl	log2phys
	ldr	r1, [r8, #12]
	ldr	r0, [sp, #20]
	ldrd	r2, r3, [sp, #8]
	cmp	r1, r0
	beq	.L1328
.L1330:
	ldr	r2, [r8, #8]
.L1348:
	ldr	r0, [r8, #12]
	mov	r1, r3
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L1323
.L1328:
	adds	r0, r1, #1
	beq	.L1330
	str	r1, [sp, #28]
	add	r0, sp, #24
	ldr	r1, [r4, #3316]
	str	r1, [sp, #32]
	ldr	r1, [r4, #3336]
	str	r1, [sp, #36]
	movs	r1, #1
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	bl	FlashReadPages
	ldr	r0, [r4, #3288]
	ldrh	r1, [r4, #258]
	add	fp, fp, r0
	ldr	r0, [sp, #32]
	ldrd	r2, r3, [sp, #8]
	lsls	r1, r1, #7
.L1331:
	cmp	r1, r2
	beq	.L1330
	ldr	ip, [fp, #8]
	ldr	lr, [ip, r2, lsl #2]
	ldr	ip, [r0, r2, lsl #2]
	cmp	lr, ip
	beq	.L1332
	ldr	r2, [sp, #28]
	ldrh	r1, [r5]
	ldr	r0, .L1349+8
	ldr	r4, .L1349+4
	bl	sftl_printk
	b	.L1347
.L1332:
	adds	r2, r2, #1
	b	.L1331
.L1324:
	mov	r2, #-1
	b	.L1348
.L1336:
	ldrh	r3, [r4, #302]
	cmp	r3, r6
	bhi	.L1317
.L1316:
	ldr	r3, .L1349
	movw	r2, #65535
	mov	r1, r6
	mov	r0, r5
	strh	r6, [r5, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	movs	r2, #0
	strb	r2, [r5, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1337
.L1338:
	mov	r0, #-1
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1350:
	.align	2
.L1349:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC117
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L1367
	.pad #96
	sub	sp, sp, #96
	mov	r3, sp
	bic	r6, r3, #8160
	bic	r6, r6, #31
	ldr	r9, [r4, #2720]
	ldr	r3, [r6, #24]
	str	r3, [sp, #92]
	ldr	r3, [r4, #2552]
	cmp	r9, #0
	beq	.L1352
	ldr	r2, [r4, #2724]
	cmp	r2, r3
	bcs	.L1353
	mov	r5, #2048
.L1358:
	ldr	r0, [r4, #2724]
	ldr	r3, [r4, #2552]
	cmp	r0, r3
	bcc	.L1354
.L1357:
	mov	r0, #-1
.L1351:
	ldr	r2, [sp, #92]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1362
	bl	__stack_chk_fail
.L1354:
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r3, [r4, #2724]
	ldr	r2, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r4, #2724]
	adds	r1, r2, #1
	beq	.L1356
	str	r2, [sp, #12]
	add	r0, sp, #8
	str	r3, [sp, #24]
	movs	r2, #0
	ldr	r3, [r4, #3324]
	movs	r1, #1
	str	r3, [sp, #16]
	add	r3, sp, #28
	str	r2, [sp, #8]
	str	r3, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L1357
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1357
.L1356:
	subs	r5, r5, #1
	bne	.L1358
	b	.L1357
.L1353:
	ldr	r3, [r4, #2556]
	movs	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r0, [r4, #2724]
	b	.L1351
.L1352:
	ldr	r8, [r4, #2556]
	movw	r5, #10000
	ldr	r1, [r4, #2612]
	ldr	r10, [r4, #2716]
	add	r2, r8, #1048576
	cmp	r1, r5
	ite	hi
	movhi	r5, #31
	movls	r5, #63
	ldrh	r7, [r4, #2496]
	cmp	r10, r2
	bhi	.L1361
	mov	r0, #1000
	lsrs	r1, r1, #10
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r10
	cmp	r8, r0
	bhi	.L1361
	ands	r0, r5, r7
	bne	.L1364
	ldr	r3, [r4, #2740]
	cmp	r3, r7
	beq	.L1351
.L1361:
	movs	r3, #1
	movs	r0, #0
	str	r7, [r4, #2740]
	str	r3, [r4, #2720]
	str	r0, [r4, #2724]
	str	r8, [r4, #2716]
	b	.L1351
.L1364:
	mov	r0, r9
	b	.L1351
.L1362:
	add	sp, sp, #96
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1368:
	.align	2
.L1367:
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r5, .L1373
	movs	r6, #12
	bl	FtlWriteDump_data
.L1370:
	ldrh	r3, [r5, #338]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1372
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1372:
	ldr	r3, [r5, #2540]
	uxth	r2, r4
	mla	r3, r6, r2, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1371
	bl	flush_l2p_region
.L1371:
	adds	r4, r4, #1
	b	.L1370
.L1374:
	.align	2
.L1373:
	.word	.LANCHOR0
	.fnend
	.size	l2p_flush, .-l2p_flush
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
	mov	fp, r2
	ldr	r4, .L1388
	.pad #108
	sub	sp, sp, #108
	adds	r2, r0, r1
	mov	r3, sp
	mov	r7, r0
	bic	r10, r3, #8160
	mov	r5, r1
	bic	r10, r10, #31
	ldrh	r8, [r4, #308]
	ldr	r3, [r10, #24]
	lsr	r8, r0, r8
	str	r3, [sp, #100]
	ldrh	r3, [r4, #296]
	cmp	r2, r3
	bhi	.L1384
	mov	r9, #0
.L1377:
	cbnz	r5, .L1382
.L1375:
	ldr	r2, [sp, #100]
	ldr	r3, [r10, #24]
	cmp	r2, r3
	beq	.L1383
	bl	__stack_chk_fail
.L1382:
	ldr	r3, [r4, #3372]
	mov	r0, r7
	ldr	r2, [r3, r8, lsl #2]
	ldrh	r3, [r4, #258]
	str	r2, [sp, #12]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	subs	r6, r3, r1
	ldr	r0, [r4, #3308]
	str	r1, [sp, #4]
	uxth	r6, r6
	cmp	r6, r5
	it	hi
	uxthhi	r6, r5
	cbz	r2, .L1379
	cmp	r3, r6
	beq	.L1379
	strd	r2, r0, [sp, #20]
	movs	r2, #1
	add	r0, sp, #16
	mov	r1, r2
	add	r3, sp, #36
	str	r3, [sp, #28]
	bl	FlashReadPages
.L1380:
	lsls	r3, r6, #9
	ldr	r0, [r4, #3308]
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	mov	r1, fp
	subs	r5, r5, r6
	add	r7, r7, r6
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r8
	ldr	r2, [r4, #3308]
	add	r8, r8, #1
	ldr	r0, .L1388+4
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	add	fp, fp, r3
	it	eq
	moveq	r9, #-1
	b	.L1377
.L1379:
	ldrh	r2, [r4, #310]
	movs	r1, #0
	bl	ftl_memset
	b	.L1380
.L1384:
	mov	r9, #-1
	b	.L1375
.L1383:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1389:
	.align	2
.L1388:
	.word	.LANCHOR0
	.word	.LANCHOR0+3608
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
	ldr	r2, .L1392
	ldr	r3, .L1392+4
	ldr	r1, [r2, #2640]
	cmp	r1, r3
	bne	.L1390
	ldr	r3, .L1392+8
	add	r2, r2, #2640
	str	r3, [r2, #4]
	movs	r1, #1
	ldr	r3, [r2, #-60]
	movs	r0, #0
	str	r3, [r2, #88]
	ldr	r3, [r2, #-56]
	str	r3, [r2, #92]
	ldr	r3, [r2, #-64]
	str	r3, [r2, #8]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #12]
	ldr	r3, [r2, #-84]
	str	r3, [r2, #16]
	ldr	r3, [r2, #-68]
	str	r3, [r2, #20]
	ldr	r3, [r2, #-40]
	str	r3, [r2, #28]
	ldr	r3, [r2, #-32]
	str	r3, [r2, #32]
	ldr	r3, [r2, #-80]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-72]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-28]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-24]
	str	r3, [r2, #48]
	b	FtlVendorPartWrite
.L1390:
	bx	lr
.L1393:
	.align	2
.L1392:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177367
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
	ldr	r3, .L1399
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r2, [r3, #3652]
	cmp	r2, #31
	itt	ls
	addls	r2, r2, #1
	strhls	r2, [r3, #3652]	@ movhi
	ldr	r2, [r3, #3348]
	ite	ls
	movls	r1, #1
	movhi	r1, #32
	ldr	r4, [r2, #16]
	cbnz	r0, .L1396
	ldr	r0, [r2, #20]
	add	r1, r1, r0
	cmp	r1, r4
	bhi	.L1397
.L1396:
	ldr	r1, .L1399+4
	movs	r0, #64
	str	r1, [r2]
	str	r4, [r2, #20]
	ldrh	r1, [r3, #3340]
	ldr	r2, [r3, #3348]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1397:
	movs	r0, #0
	pop	{r4, pc}
.L1400:
	.align	2
.L1399:
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
	.section	.rodata.str1.1
.LC118:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
	.text
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
	mov	r9, r2
	ldr	r5, .L1413
	.pad #108
	sub	sp, sp, #108
	adds	r2, r0, r1
	mov	r3, sp
	mov	r8, r0
	bic	r3, r3, #8160
	mov	r6, r1
	bic	r3, r3, #31
	ldrh	r7, [r5, #308]
	str	r3, [sp, #4]
	ldr	r3, [r3, #24]
	lsr	r7, r0, r7
	str	r3, [sp, #100]
	ldrh	r3, [r5, #296]
	cmp	r2, r3
	bhi	.L1412
	mov	r10, #0
.L1404:
	cbnz	r6, .L1410
.L1402:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #100]
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1411
	bl	__stack_chk_fail
.L1410:
	ldr	r3, [r5, #3372]
	mov	r0, r8
	ldrh	r4, [r5, #258]
	ldr	r3, [r3, r7, lsl #2]
	mov	r1, r4
	str	r3, [sp, #12]
	bl	__aeabi_uidivmod
	subs	r4, r4, r1
	ldr	r3, [sp, #12]
	str	r1, [sp, #8]
	uxth	r4, r4
	cmp	r4, r6
	it	hi
	uxthhi	r4, r6
	lsl	fp, r4, #9
	cbz	r3, .L1406
	ldr	r2, [r5, #3308]
	add	r0, sp, #16
	str	r2, [sp, #24]
	add	r2, sp, #36
	str	r2, [sp, #28]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #20]
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r2, r2, #1
	ldr	r2, [r5, #3452]
	it	eq
	moveq	r10, #-1
	cmp	r2, #256
	bne	.L1408
	ldr	r0, .L1413+4
	mov	r2, r3
	mov	r1, r7
	bl	sftl_printk
	ldr	r2, [r5, #3308]
	ldr	r0, .L1413+8
	mov	r1, r7
	bl	FtlMapWritePage
.L1408:
	ldr	r1, [r5, #3308]
	mov	r2, fp
	ldr	r3, [sp, #8]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1409:
	adds	r7, r7, #1
	subs	r6, r6, r4
	add	r8, r8, r4
	add	r9, r9, fp
	b	.L1404
.L1406:
	mov	r2, fp
	mov	r1, r3
	mov	r0, r9
	bl	ftl_memset
	b	.L1409
.L1412:
	mov	r10, #-1
	b	.L1402
.L1411:
	mov	r0, r10
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR0
	.word	.LC118
	.word	.LANCHOR0+3608
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.section	.rodata.str1.1
.LC119:
	.ascii	"no ect\000"
	.text
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
	ldr	r4, .L1417
	ldr	r2, [r4, #3348]
	ldrh	r1, [r4, #3340]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3348]
	ldr	r2, [r3]
	ldr	r3, .L1417+4
	cmp	r2, r3
	beq	.L1416
	ldr	r1, .L1417+8
	ldr	r0, .L1417+12
	bl	sftl_printk
	ldrh	r2, [r4, #3340]
	ldr	r0, [r4, #3348]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L1416:
	movs	r0, #0
	pop	{r4, pc}
.L1418:
	.align	2
.L1417:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC119
	.word	.LC77
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
	ldr	r4, .L1421
	movs	r0, #0
	ldr	r5, .L1421+4
	add	r2, r4, #2640
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2640]
	cmp	r3, r5
	beq	.L1420
	add	r0, r4, #2640
	mov	r2, #512
	movs	r1, #0
	bl	ftl_memset
	str	r5, [r4, #2640]
.L1420:
	ldr	r3, [r4, #2728]
	ldr	r2, [r4, #2672]
	str	r3, [r4, #2580]
	ldr	r3, [r4, #2732]
	str	r3, [r4, #2584]
	str	r2, [r4, #2608]
	ldr	r3, [r4, #2648]
	ldr	r2, [r4, #2676]
	str	r3, [r4, #2576]
	str	r2, [r4, #2560]
	ldr	r3, [r4, #2652]
	ldr	r2, [r4, #2680]
	str	r3, [r4, #2564]
	str	r2, [r4, #2568]
	ldr	r3, [r4, #2656]
	ldr	r2, [r4, #2684]
	str	r3, [r4, #2556]
	str	r2, [r4, #2612]
	ldr	r3, [r4, #2660]
	ldr	r2, [r4, #2688]
	str	r3, [r4, #2572]
	str	r2, [r4, #2616]
	ldr	r3, [r4, #2668]
	ldrh	r2, [r4, #292]
	ldr	r0, [r4, #2596]
	ldrh	r1, [r4, #240]
	str	r3, [r4, #2600]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2604]
	pop	{r3, r4, r5, pc}
.L1422:
	.align	2
.L1421:
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
	mov	r5, r0
	ldr	r3, [r0, #36]
	cbz	r3, .L1424
	ldrh	r6, [r0, #6]
	movs	r2, #0
	ldr	r4, .L1428
	subs	r6, r6, #1
	str	r2, [r0, #36]
	ldr	r3, [r0, #24]
	uxth	r6, r6
	ldr	r0, [r4, #3304]
	ldr	r2, [r4, #3332]
	str	r0, [r4, #3460]
	str	r2, [r4, #3464]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r4, #3456]
	cbz	r3, .L1426
	movs	r2, #1
	addw	r0, r4, #3452
	mov	r1, r2
	bl	FlashReadPages
.L1427:
	ldr	r2, [r4, #3460]
	mov	r1, r6
	mov	r0, r5
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1426:
	ldrh	r2, [r4, #310]
	movs	r1, #255
	bl	ftl_memset
	b	.L1427
.L1424:
	pop	{r4, r5, r6, pc}
.L1429:
	.align	2
.L1428:
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.section	.rodata.str1.1
.LC120:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
	.text
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
	ldr	r9, .L1446+4
	ldr	r10, .L1446+8
	addw	r8, r4, #3452
	movw	r7, #65535
	ldr	r6, [r4, #3332]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldrh	r3, [r4, #2624]
	str	r6, [r4, #3464]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2632]
	str	r3, [r6, #4]
	ldr	r3, .L1446+12
	str	r3, [r4, #2468]
	ldr	r3, .L1446+16
	str	r3, [r4, #2472]
	ldrh	r3, [r4, #2630]
	strd	r5, r5, [r6, #8]
	strh	r3, [r4, #2476]	@ movhi
	ldrh	r3, [r4, #254]
	strb	r3, [r4, #2478]
	ldrh	r3, [r4, #24]
	ldrh	r2, [r4, #26]
	strh	r3, [r4, #2482]	@ movhi
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2484]	@ movhi
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	strb	r3, [r4, #2479]
	ldrh	r3, [r4, #76]
	ldrh	r2, [r4, #78]
	strh	r3, [r4, #2486]	@ movhi
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2488]	@ movhi
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	strb	r3, [r4, #2480]
	ldrh	r3, [r4, #124]
	strh	r3, [r4, #2490]	@ movhi
	ldrh	r2, [r4, #126]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	ldr	r0, [r4, #3460]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2492]	@ movhi
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	strb	r3, [r4, #2481]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2500]
	ldr	r3, [r4, #2588]
	ldrh	r2, [r4, #310]
	str	r3, [r4, #2508]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2504]
	bl	ftl_memset
	addw	r1, r4, #2468
	movs	r2, #48
	ldr	r0, [r4, #3460]
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #3460]
	ldr	r1, [r4, #72]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.L1431:
	ldrh	r1, [r4, #2626]
	ldrh	r2, [r4, #2624]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3456]
	ldrh	r3, [r4, #304]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L1432
	movs	r3, #0
	ldrh	r7, [r4, #2628]
	strh	r3, [r4, #2626]	@ movhi
	strh	r2, [r4, #2628]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2588]
	str	r3, [r4, #2632]
	adds	r2, r3, #1
	strh	r0, [r4, #2624]	@ movhi
	str	r2, [r4, #2588]
	lsls	r2, r0, #10
	str	r2, [r4, #3456]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L1432:
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3300]
	bl	js_hash
	movs	r3, #1
	str	r0, [r6, #12]
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
	ldrh	r3, [r4, #2626]
	ldr	r2, [r4, #3452]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #2626]	@ movhi
	bne	.L1433
	cmp	r3, #1
	bne	.L1435
	mov	r1, r9
	mov	r0, r10
	movw	r2, #1145
	bl	sftl_printk
	ldrh	r3, [r4, #2626]
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #304]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2626]	@ movhi
.L1435:
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r5, #3
	bls	.L1431
	ldr	r1, [r4, #3456]
	mov	r2, r5
	ldr	r0, .L1446+20
	bl	sftl_printk
.L1438:
	b	.L1438
.L1433:
	cmp	r3, #1
	beq	.L1431
	cmp	r2, #256
	beq	.L1431
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1439
	mov	r0, r7
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1439:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1447:
	.align	2
.L1446:
	.word	.LANCHOR0
	.word	.LANCHOR1+471
	.word	.LC8
	.word	1179929683
	.word	1342177367
	.word	.LC120
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
	.global	sftl_discard
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_discard, %function
sftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, r0
	ldr	r5, .L1471
	.pad #20
	sub	sp, sp, #20
	mov	r4, r1
	mov	r3, sp
	bic	r9, r3, #8160
	bic	r9, r9, #31
	ldr	r3, [r9, #24]
	str	r3, [sp, #12]
	ldr	r3, [r5, #340]
	cmp	r3, r0
	bls	.L1464
	cmp	r3, r1
	bcc	.L1464
	adds	r2, r0, r1
	cmp	r3, r2
	bcc	.L1464
	cmp	r1, #31
	bhi	.L1455
.L1460:
	movs	r0, #0
.L1453:
	ldr	r2, [sp, #12]
	ldr	r3, [r9, #24]
	cmp	r2, r3
	beq	.L1461
	bl	__stack_chk_fail
.L1455:
	ldrh	r8, [r5, #258]
	mov	r1, r8
	bl	__aeabi_uidiv
	mov	r7, r0
	smulbb	r0, r0, r8
	subs	r6, r6, r0
	uxth	r6, r6
	cbz	r6, .L1456
	sub	r8, r8, r6
	adds	r7, r7, #1
	cmp	r8, r4
	it	cs
	movcs	r8, r4
	uxth	r8, r8
	sub	r4, r4, r8
.L1456:
	mov	r3, #-1
	str	r3, [sp, #8]
.L1457:
	ldrh	r3, [r5, #258]
	cmp	r3, r4
	bls	.L1459
	ldr	r3, [r5, #3656]
	cmp	r3, #32
	bls	.L1460
	movs	r4, #0
	str	r4, [r5, #3656]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1460
.L1459:
	add	r1, sp, #4
	mov	r0, r7
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1458
	ldr	r3, [r5, #3656]
	add	r1, sp, #8
	movs	r2, #1
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r5, #3656]
	ldr	r3, [r5, #2560]
	adds	r3, r3, #1
	str	r3, [r5, #2560]
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1458:
	ldrh	r3, [r5, #258]
	adds	r7, r7, #1
	subs	r4, r4, r3
	b	.L1457
.L1464:
	mov	r0, #-1
	b	.L1453
.L1461:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1472:
	.align	2
.L1471:
	.word	.LANCHOR0
	.fnend
	.size	sftl_discard, .-sftl_discard
	.section	.rodata.str1.1
.LC121:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
	.text
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r5, #0
	ldr	r4, .L1488
	mov	r3, sp
	ldr	r1, .L1488+4
	bic	r7, r3, #8160
	ldr	r0, .L1488+8
	bic	r7, r7, #31
	ldr	r3, [r7, #24]
	str	r3, [sp, #4]
	bl	sftl_printk
	ldrh	r2, [r4, #242]
	movs	r1, #0
	ldr	r0, [r4, #3352]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1474:
	ldr	r3, [r4, #2552]
	cmp	r3, r5
	bhi	.L1476
	ldr	r9, .L1488+12
	mov	r8, #0
	ldr	r10, .L1488+16
	ldr	fp, .L1488+4
.L1477:
	ldrh	r3, [r4, #240]
	uxth	r5, r8
	cmp	r3, r5
	bhi	.L1483
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L1484
	bl	__stack_chk_fail
.L1476:
	mov	r1, sp
	mov	r0, r5
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	adds	r3, r0, #1
	beq	.L1475
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3352]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1475:
	adds	r5, r5, #1
	b	.L1474
.L1483:
	ldr	r3, [r4, #72]
	uxth	r6, r8
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3352]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1479
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1479
	mov	r1, r6
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #24]
	cmp	r3, r5
	beq	.L1479
	ldrh	r3, [r4, #124]
	cmp	r3, r5
	beq	.L1479
	ldrh	r3, [r4, #76]
	cmp	r3, r5
	beq	.L1479
	ldr	r3, [r4, #72]
	ldr	r2, [r4, #3352]
	ldrh	r1, [r3, r6, lsl #1]
	ldrh	r2, [r2, r6, lsl #1]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	cbnz	r1, .L1481
	mov	r1, r5
	mov	r0, r10
	bl	List_remove_node
	ldrh	r3, [r4, #224]
	cbnz	r3, .L1482
	ldr	r0, .L1488+20
	mov	r1, fp
	movw	r2, #2330
	bl	sftl_printk
.L1482:
	ldrh	r3, [r4, #224]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	bl	INSERT_DATA_LIST
.L1479:
	add	r8, r8, #1
	b	.L1477
.L1481:
	mov	r0, r5
	bl	update_vpc_list
	b	.L1479
.L1484:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1489:
	.align	2
.L1488:
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC121
	.word	.LANCHOR0+2532
	.word	.LC8
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
	mov	r5, r0
	ldr	r6, .L1500
	ldrh	r4, [r0]
	ldrh	r3, [r6, #240]
	cmp	r3, r4
	bcs	.L1491
	ldr	r1, .L1500+4
	movw	r2, #2766
	ldr	r0, .L1500+8
	bl	sftl_printk
.L1491:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1492
	ldr	r3, [r6, #72]
	mov	r0, r4
	ldrh	r3, [r3, r4, lsl #1]
	cbz	r3, .L1493
	bl	INSERT_DATA_LIST
.L1492:
	ldrh	r0, [r6, #3438]
	movs	r3, #1
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1494
	cmp	r0, r4
	bne	.L1495
	ldr	r3, [r6, #72]
	ldrh	r3, [r3, r4, lsl #1]
	cbz	r3, .L1496
.L1495:
	bl	update_vpc_list
.L1496:
	movw	r3, #65535
	strh	r3, [r6, #3438]	@ movhi
.L1494:
	mov	r0, r5
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1493:
	bl	INSERT_FREE_LIST
	b	.L1492
.L1501:
	.align	2
.L1500:
	.word	.LANCHOR0
	.word	.LANCHOR1+507
	.word	.LC8
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.section	.rodata.str1.1
.LC122:
	.ascii	"FtlProgPages error %x = %d\012\000"
.LC123:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
	.text
	.align	1
	.global	FtlProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, r3
	ldr	fp, .L1522
	mov	r3, sp
	ldr	r10, .L1522+4
	bic	r7, r3, #8160
	mov	r9, #0
	bic	r7, r7, #31
	movs	r2, #0
	mov	r4, r0
	mov	r8, r1
	ldr	r3, [r7, #24]
	str	r3, [sp, #4]
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1503:
	cmp	r9, r8
	bne	.L1510
	ldr	r3, .L1522+4
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #232]
	cmp	r2, r3
	bcc	.L1502
	ldr	r1, .L1522+8
	mov	r2, #1000
	ldr	r0, .L1522+12
	bl	sftl_printk
.L1502:
	ldr	r2, [sp, #4]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L1512
	bl	__stack_chk_fail
.L1505:
	ldr	r1, [r4, #4]
	mov	r0, fp
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1504
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1504:
	mov	r0, r5
	bl	get_new_active_ppa
	str	r0, [r4, #4]
	str	r0, [sp]
	movs	r2, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	movs	r1, #1
	bl	FlashProgPages
.L1510:
	ldr	r2, [r4]
	adds	r1, r2, #1
	beq	.L1505
	cmp	r2, #256
	beq	.L1505
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #232]
	cmp	r2, r3
	bcc	.L1506
	ldr	r1, .L1522+8
	movw	r2, #985
	ldr	r0, .L1522+12
	bl	sftl_printk
.L1506:
	ldr	r3, [r4, #4]
	mov	r1, sp
	ldr	r0, [r4, #16]
	movs	r2, #1
	str	r3, [sp]
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L1507
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r10, #72]
	mov	r6, r0
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1508
	mov	r1, r0
	ldr	r0, .L1522+16
	bl	sftl_printk
.L1508:
	mov	r0, r6
	bl	decrement_vpc_count
.L1507:
	add	r9, r9, #1
	adds	r4, r4, #20
	b	.L1503
.L1512:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1523:
	.align	2
.L1522:
	.word	.LC122
	.word	.LANCHOR0
	.word	.LANCHOR1+536
	.word	.LC8
	.word	.LC123
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
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	ldr	r4, .L1549
	mov	r3, sp
	bic	r6, r3, #8160
	bic	r6, r6, #31
	ldrh	r2, [r4, #124]
	ldrh	r1, [r4, #302]
	ldr	r3, [r6, #24]
	str	r3, [sp, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1525
.L1532:
	ldrh	r2, [r4, #124]
	movs	r3, #0
	str	r3, [r4, #3444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1546
.L1526:
	movs	r0, #0
.L1524:
	ldr	r2, [sp, #4]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1541
	bl	__stack_chk_fail
.L1525:
	ldr	r5, .L1549+4
	cbz	r0, .L1528
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1529
.L1530:
	movs	r1, #2
.L1528:
	ldr	r0, .L1549+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [sp]
	cmp	r2, r3
	beq	.L1532
	movs	r0, #1
	b	.L1524
.L1529:
	movs	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, #17
	bhi	.L1530
	b	.L1528
.L1546:
	ldrb	r3, [r4, #131]	@ zero_extendqisi2
	ldrh	r1, [r4, #302]
	ldrh	r2, [r4, #3204]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1533
	ldr	r1, .L1549+12
	movs	r2, #164
	ldr	r0, .L1549+16
	bl	sftl_printk
.L1533:
	ldrb	r3, [r4, #131]	@ zero_extendqisi2
	movs	r5, #0
	ldrh	r0, [r4, #302]
	mov	r9, #12
	ldrh	r1, [r4, #124]
	ldr	r2, [r4, #72]
	smulbb	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2576]
	ldrh	r3, [r4, #3204]
	add	r3, r3, r2
	str	r3, [r4, #2576]
.L1534:
	ldrh	r2, [r4, #3204]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1538
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #124]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1539
	bl	INSERT_DATA_LIST
.L1540:
	movw	r7, #65535
	movs	r5, #0
	strh	r7, [r4, #124]	@ movhi
	strh	r5, [r4, #3204]	@ movhi
	strh	r5, [r4, #3196]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r4, #2620]
	ldrh	r2, [r4, #224]
	strh	r7, [r4, #172]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	ble	.L1526
	movs	r3, #20
	strh	r3, [r4, #3152]	@ movhi
	b	.L1526
.L1538:
	uxth	r8, r5
	ldr	r10, [r4, #3200]
	ldr	r3, [r4, #2552]
	mul	r8, r9, r8
	add	r7, r10, r8
	ldr	r0, [r7, #8]
	cmp	r0, r3
	bcc	.L1535
.L1547:
	ldrh	r0, [r4, #124]
	b	.L1548
.L1535:
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r0, [r10, r8]
	ldr	r3, [sp]
	cmp	r0, r3
	bne	.L1537
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	adds	r1, r7, #4
	mov	r8, r0
	movs	r2, #1
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L1548:
	bl	decrement_vpc_count
	b	.L1536
.L1537:
	ldr	r2, [r7, #4]
	cmp	r3, r2
	bne	.L1547
.L1536:
	adds	r5, r5, #1
	b	.L1534
.L1539:
	bl	INSERT_FREE_LIST
	b	.L1540
.L1541:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1550:
	.align	2
.L1549:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+124
	.word	.LANCHOR1+549
	.word	.LC8
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
	ldr	r4, .L1553
	add	r0, r4, #124
	ldrh	r1, [r4, #302]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #126]
	ldrh	r3, [r4, #302]
	cmp	r2, r3
	bcc	.L1551
	add	r0, r4, #3392
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3444]
.L1551:
	pop	{r4, pc}
.L1554:
	.align	2
.L1553:
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
	ldr	r4, .L1556
	add	r6, r4, #24
	adds	r4, r4, #76
	mov	r0, r6
	str	r5, [r4, #3400]
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
.L1557:
	.align	2
.L1556:
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r5, #0
	ldr	r4, .L1566
	movs	r6, #20
	movs	r3, #0
	mov	r2, r3
	ldr	r1, [r4, #3168]
	ldr	r0, [r4, #3292]
	bl	FlashProgPages
.L1559:
	ldr	r1, [r4, #3168]
	uxth	r3, r5
	ldr	r2, [r4, #3292]
	cmp	r3, r1
	bcc	.L1562
	mov	r0, r2
	bl	FtlGcBufFree
	ldrh	r3, [r4, #128]
	movs	r0, #0
	str	r0, [r4, #3168]
	cbnz	r3, .L1558
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	movs	r0, #1
.L1558:
	pop	{r3, r4, r5, r6, r7, pc}
.L1562:
	muls	r3, r6, r3
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	ldr	r7, [r1, #12]
	adds	r3, r2, #1
	ldr	r1, [r1, #4]
	ldr	r0, [r7, #12]
	it	ne
	ldrne	r2, [r7, #8]
	bl	FtlGcUpdatePage
	b	.L1559
.L1567:
	.align	2
.L1566:
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
	ldr	r4, .L1571
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1569
	ldrh	r3, [r4, #128]
	cbnz	r3, .L1570
.L1569:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1571+4
	strb	r5, [r4, #132]
	bl	allocate_data_superblock
	strh	r5, [r4, #3196]	@ movhi
	strh	r5, [r4, #3204]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1570:
	pop	{r3, r4, r5, lr}
	ldr	r0, .L1571+4
	b	get_new_active_ppa
.L1572:
	.align	2
.L1571:
	.word	.LANCHOR0
	.word	.LANCHOR0+124
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.section	.rodata.str1.1
.LC124:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
	.text
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L1687
	.pad #44
	sub	sp, sp, #44
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	str	r0, [sp, #16]
	ldr	r0, [r4, #3272]
	ldr	r3, [r3, #24]
	str	r3, [sp, #36]
	cmp	r0, #0
	bne	.L1651
	ldrh	r3, [r4, #2528]
	cmp	r3, #47
	bls	.L1573
	ldr	r3, .L1687+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1575
.L1578:
	ldrh	r2, [r4, #220]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1576
.L1577:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3156]
	ldr	r2, [sp, #16]
	adds	r3, r3, #1
	ldrh	r6, [r4, #172]
	add	r3, r3, r2, lsl #7
	ldrh	r2, [r4, #124]
	str	r3, [r4, #3156]
	ands	r6, r6, r2
	ldrh	r2, [r4, #222]
	ands	r6, r6, r2
	movw	r2, #65535
	cmp	r6, r2
	bne	.L1579
	ldrh	r2, [r4, #224]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1579
	movs	r7, #0
	str	r7, [r4, #3156]
	strh	r7, [r4, #3660]	@ movhi
	bl	GetSwlReplaceBlock
	mov	r5, r0
	cmp	r0, r6
	bne	.L1581
	ldrh	r2, [r4, #224]
	ldrh	r3, [r4, #3154]
	cmp	r2, r3
	bcs	.L1582
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1584
	mov	r0, r3
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1585
	mov	r0, r7
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r3, #128
	cmp	r0, r5
	strh	r3, [r4, #3154]	@ movhi
	beq	.L1584
	mov	r5, r0
.L1581:
	ldr	r3, [r4, #72]
	mov	r1, r5
	ldrh	r2, [r4, #3152]
	ldr	r0, .L1687+8
	ldrh	r3, [r3, r5, lsl #1]
	str	r2, [sp, #4]
	ldr	r2, [r4, #2536]
	ldrh	r2, [r2, r5, lsl #1]
	str	r2, [sp]
	ldrh	r2, [r4, #224]
	bl	sftl_printk
	b	.L1584
.L1575:
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1578
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1578
	movs	r0, #1
	b	.L1573
.L1576:
	ldrh	r3, [r4, #222]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #222]	@ movhi
	strheq	r3, [r4, #220]	@ movhi
	b	.L1577
.L1585:
	movs	r3, #64
.L1683:
	strh	r3, [r4, #3154]	@ movhi
.L1584:
	bl	FtlGcReFreshBadBlk
	ldr	r3, [sp, #16]
	cmp	r3, #0
	bne	.L1587
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1587
.L1649:
	ldrh	r3, [r4, #224]
	cmp	r3, #24
	bhi	.L1656
	ldrh	r6, [r4, #302]
	cmp	r3, #16
	bls	.L1589
	lsrs	r6, r6, #5
.L1588:
	ldrh	r2, [r4, #3152]
	cmp	r2, r3
	bcs	.L1591
	ldrh	r2, [r4, #222]
	ldrh	r3, [r4, #124]
	ands	r3, r3, r2
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1592
	ldrh	r0, [r4, #3660]
	cbnz	r0, .L1593
	ldr	r3, [r4, #2552]
	ldr	r2, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1657
.L1593:
	ldrh	r3, [r4, #2620]
	add	r3, r3, r3, lsl #1
	lsrs	r3, r3, #2
.L1594:
	strh	r3, [r4, #3152]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3164]
.L1573:
	mov	r3, sp
	ldr	r2, [sp, #36]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1650
	bl	__stack_chk_fail
.L1582:
	movs	r3, #80
	b	.L1683
.L1589:
	cmp	r3, #12
	bls	.L1590
	lsrs	r6, r6, #4
	b	.L1588
.L1590:
	cmp	r3, #8
	bls	.L1588
	lsrs	r6, r6, #2
	b	.L1588
.L1656:
	movs	r6, #1
	b	.L1588
.L1657:
	movs	r3, #18
	b	.L1594
.L1592:
	ldrh	r3, [r4, #2620]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3152]	@ movhi
.L1591:
	ldrh	r3, [r4, #3206]
	cbz	r3, .L1658
	adds	r6, r6, #32
	uxth	r6, r6
.L1658:
	movw	r5, #65535
.L1596:
	ldrh	r3, [r4, #172]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1604
	cmp	r5, r3
	beq	.L1605
	strh	r5, [r4, #172]	@ movhi
.L1606:
	ldrh	r0, [r4, #172]
	movs	r3, #0
	movw	r8, #65535
	strb	r3, [r4, #180]
	cmp	r0, r8
	beq	.L1604
	bl	IsBlkInGcList
	mov	r7, r0
	cmp	r0, #0
	beq	.L1609
	strh	r8, [r4, #172]	@ movhi
.L1604:
	ldrh	r3, [r4, #172]
	ldrh	r2, [r4, #24]
	cmp	r2, r3
	beq	.L1610
	ldrh	r2, [r4, #76]
	cmp	r2, r3
	beq	.L1610
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	bne	.L1611
.L1610:
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
.L1611:
	ldr	r4, .L1687
.L1647:
	ldrh	r8, [r4, #172]
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1612
	mov	r9, #0
	str	r9, [r4, #3164]
.L1613:
	ldrh	r7, [r4, #3160]
	mov	r0, r7
	bl	List_get_gc_head_node
	uxth	fp, r0
	mov	r10, r0
	cmp	fp, r8
	strh	fp, [r4, #172]	@ movhi
	bne	.L1614
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3160]	@ movhi
	b	.L1573
.L1597:
	movs	r3, #0
	ldrh	r2, [r4, #224]
	str	r3, [r4, #3164]
	ldrh	r3, [r4, #3152]
	cmp	r2, r3
	bls	.L1598
	ldrh	r3, [r4, #3660]
	cbnz	r3, .L1599
	ldr	r3, [r4, #2552]
	ldr	r2, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1660
.L1599:
	ldrh	r3, [r4, #2620]
	add	r3, r3, r3, lsl #1
	lsrs	r3, r3, #2
.L1600:
	strh	r3, [r4, #3152]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r4, #72]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L1598
.L1686:
	ldrh	r0, [r4, #3660]
	b	.L1573
.L1660:
	movs	r3, #18
	b	.L1600
.L1598:
	ldrh	r0, [r4, #3660]
	cmp	r0, #0
	bne	.L1601
	ldrh	r5, [r4, #2620]
	add	r3, r5, r5, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3152]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r4, #72]
	uxth	r0, r0
	ldrh	r1, [r4, #232]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #304]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1602
	ldrh	r1, [r4, #224]
	subs	r3, r5, #1
	cmp	r1, r3
	blt	.L1602
	bl	FtlReadRefresh
	b	.L1686
.L1688:
	.align	2
.L1687:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
.L1602:
	cmp	r2, #0
	bne	.L1601
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #224]
	adds	r0, r0, #1
	b	.L1573
.L1605:
	ldrh	r3, [r4, #222]
	cmp	r3, r5
	beq	.L1606
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1607
	strh	r5, [r4, #222]	@ movhi
.L1607:
	ldrh	r3, [r4, #222]
	strh	r3, [r4, #172]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #222]	@ movhi
	b	.L1606
.L1614:
	mov	r0, fp
	bl	IsBlkInGcList
	adds	r3, r7, #1
	uxth	r3, r3
	cbz	r0, .L1615
.L1684:
	strh	r3, [r4, #3160]	@ movhi
	b	.L1613
.L1615:
	ldrh	r7, [r4, #232]
	uxth	r10, r10
	ldrh	r2, [r4, #302]
	ldr	r1, [r4, #72]
	strh	r3, [r4, #3160]	@ movhi
	muls	r2, r7, r2
	ldrh	r0, [r1, r10, lsl #1]
	add	r7, r2, r2, lsr #31
	cmp	r0, r7, asr #1
	bgt	.L1617
	cmp	r3, #48
	bls	.L1618
	cmp	r0, #8
	bls	.L1618
	ldrh	r3, [r4, #3196]
	cmp	r3, #35
	bhi	.L1618
.L1617:
	strh	r9, [r4, #3160]	@ movhi
.L1618:
	ldrh	r3, [r1, r10, lsl #1]
	cmp	r2, r3
	bgt	.L1619
	cmp	r5, r8
	bne	.L1619
	ldrh	r2, [r4, #3160]
	cmp	r2, #3
	bhi	.L1619
	movs	r3, #0
	strh	r5, [r4, #172]	@ movhi
	strh	r3, [r4, #3160]	@ movhi
	b	.L1686
.L1619:
	cbnz	r3, .L1620
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3160]
	adds	r3, r3, #1
	b	.L1684
.L1620:
	movs	r3, #0
	strb	r3, [r4, #180]
	ldrh	r3, [r4, #24]
	cmp	r3, fp
	bne	.L1621
	ldr	r1, .L1689
	movw	r2, #717
	ldr	r0, .L1689+4
	bl	sftl_printk
.L1621:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #76]
	cmp	r2, r3
	bne	.L1622
	ldr	r1, .L1689
	movw	r2, #718
	ldr	r0, .L1689+4
	bl	sftl_printk
.L1622:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #124]
	cmp	r2, r3
	bne	.L1623
	ldr	r1, .L1689
	movw	r2, #719
	ldr	r0, .L1689+4
	bl	sftl_printk
.L1623:
	ldr	r0, .L1689+8
	bl	make_superblock
	ldrh	r1, [r4, #172]
	ldr	r2, [r4, #72]
	movs	r3, #0
	strh	r3, [r4, #3662]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r2, [r4, #3664]	@ movhi
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
.L1612:
	movs	r3, #1
	str	r3, [r4, #3272]
	ldr	r3, [sp, #16]
	ldrh	fp, [r4, #302]
	cbz	r3, .L1624
	ldrh	r3, [r4, #232]
	ldrh	r1, [r4, #172]
	ldr	r2, [r4, #72]
	mul	r3, fp, r3
	ldrh	r2, [r2, r1, lsl #1]
	subs	r3, r3, r2
	it	mi
	addmi	r3, r3, #3
	add	r6, r6, r3, asr #2
	uxth	r6, r6
.L1624:
	ldrh	r3, [r4, #174]
	adds	r2, r3, r6
	cmp	r2, fp
	itt	gt
	subgt	r3, fp, r3
	uxthgt	r6, r3
	movs	r3, #0
.L1685:
	str	r3, [sp, #12]
	ldrh	r3, [r4, #174]
	ldrh	r2, [sp, #12]
	cmp	r6, r2
	bls	.L1635
	ldr	r2, [sp, #12]
	movw	r9, #65535
	ldrh	lr, [r4, #232]
	mov	r10, #20
	add	r3, r3, r2
	ldr	r0, [r4, #3184]
	movs	r2, #0
	ldr	ip, .L1689+12
	mov	r7, r2
	b	.L1636
.L1629:
	ldrh	r1, [ip], #2
	cmp	r1, r9
	beq	.L1628
	mla	r8, r10, r7, r0
	adds	r7, r7, #1
	orr	r1, r3, r1, lsl #10
	uxth	r7, r7
	str	r1, [r8, #4]
.L1628:
	adds	r2, r2, #1
.L1636:
	uxth	r1, r2
	cmp	lr, r1
	bhi	.L1629
	mov	r10, #0
	ldrb	r2, [r4, #180]	@ zero_extendqisi2
	mov	r1, r7
	bl	FlashReadPages
.L1630:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1634
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	b	.L1685
.L1634:
	mov	r8, #20
	ldr	r3, [r4, #3184]
	mul	r8, r8, r10
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L1631
	ldr	r9, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L1631
	ldr	r3, [r9, #8]
	adds	r2, r3, #1
	bne	.L1632
	ldr	r1, .L1689
	movw	r2, #753
	ldr	r0, .L1689+4
	str	r3, [sp, #20]
	bl	sftl_printk
	ldr	r3, [sp, #20]
.L1632:
	mov	r0, r3
	movs	r2, #0
	add	r1, sp, #28
	bl	log2phys
	ldr	r0, [r4, #3184]
	ldr	r3, [sp, #28]
	add	r0, r0, r8
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1631
	ldrh	r3, [r4, #3662]
	movs	r2, #20
	ldr	r1, [r4, #3168]
	adds	r3, r3, #1
	strh	r3, [r4, #3662]	@ movhi
	ldr	r3, [r4, #3292]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r3, [sp, #20]
	str	r1, [r3, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #20]
	ldr	r1, [r4, #3168]
	movs	r2, #20
	str	r0, [r3, #4]
	ldr	r3, [r4, #3292]
	ldr	r0, [r4, #3184]
	add	r0, r0, r8
	mla	r3, r2, r1, r3
	movs	r1, #1
	ldr	r2, [r0, #8]
	str	r2, [r3, #8]
	ldr	r2, [r0, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #28]
	str	r3, [r9, #12]
	ldrh	r3, [r4, #124]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #2592]
	ldr	r0, [r4, #3184]
	str	r3, [r9, #4]
	ldr	r3, [r4, #3168]
	add	r0, r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #3168]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	ldr	r3, [r4, #3168]
	cmp	r2, r3
	beq	.L1633
	ldrh	r3, [r4, #128]
	cbnz	r3, .L1631
.L1633:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1631
	ldr	r3, .L1689+16
	movs	r2, #0
	ldrh	r0, [r3, #3660]
	str	r2, [r3, #3272]
	movw	r2, #65535
	str	r2, [r3, #172]
	b	.L1573
.L1631:
	add	r10, r10, #1
	b	.L1630
.L1635:
	add	r6, r6, r3
	uxth	r6, r6
	strh	r6, [r4, #174]	@ movhi
	cmp	fp, r6
	bhi	.L1637
	ldr	r3, [r4, #3168]
	cbz	r3, .L1638
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1638
	movs	r3, #0
	str	r3, [r4, #3272]
	b	.L1686
.L1638:
	ldrh	r6, [r4, #3662]
	cbnz	r6, .L1639
	ldrh	r2, [r4, #172]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L1639
.L1640:
	ldr	r3, [r4, #2552]
	cmp	r6, r3
	bcc	.L1643
.L1645:
	ldrh	r2, [r4, #172]
	movs	r1, #0
	ldr	r3, [r4, #72]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4, #172]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1639:
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
.L1637:
	movs	r3, #0
	str	r3, [r4, #3272]
	ldrh	r3, [r4, #224]
	cmp	r3, #2
	bhi	.L1646
	ldrh	r6, [r4, #302]
	b	.L1647
.L1643:
	add	r1, sp, #32
	mov	r0, r6
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #32]
	adds	r3, r0, #1
	beq	.L1641
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	beq	.L1642
.L1641:
	adds	r6, r6, #1
	b	.L1640
.L1642:
	ldr	r3, [r4, #2552]
	cmp	r6, r3
	bcc	.L1639
	b	.L1645
.L1646:
	ldrh	r0, [r4, #3660]
	cmp	r0, #0
	bne	.L1573
	adds	r0, r3, #1
	b	.L1573
.L1651:
	movs	r0, #0
	b	.L1573
.L1579:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L1649
	movw	r5, #65535
.L1587:
	ldrh	r3, [r4, #222]
	ldrh	r6, [r4, #124]
	ands	r6, r6, r3
	movw	r3, #65535
	ands	r6, r6, r5
	uxth	r6, r6
	cmp	r6, r3
	bne	.L1659
	ldrh	r3, [r4, #172]
	cmp	r3, r6
	beq	.L1597
.L1601:
	mov	r5, r6
.L1659:
	movs	r6, #1
	b	.L1596
.L1609:
	ldr	r0, .L1689+8
	bl	make_superblock
	ldrh	r2, [r4, #172]
	ldr	r3, [r4, #72]
	strh	r7, [r4, #174]	@ movhi
	strb	r7, [r4, #178]
	strh	r7, [r4, #3662]	@ movhi
	ldrh	r3, [r3, r2, lsl #1]
	strh	r3, [r4, #3664]	@ movhi
	b	.L1604
.L1690:
	.align	2
.L1689:
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+172
	.word	.LANCHOR0+188
	.word	.LANCHOR0
.L1650:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	mov	r5, r1
	.pad #60
	sub	sp, sp, #60
	cmp	r0, #16
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	str	r2, [sp, #4]
	ldr	r2, [r3, #24]
	str	r2, [sp, #52]
	bne	.L1692
	ldr	r2, [sp, #52]
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1693
.L1730:
	bl	__stack_chk_fail
.L1693:
	ldr	r1, [sp, #4]
	mov	r2, r8
	add	r0, r5, #256
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FtlVendorPartRead
.L1692:
	ldr	r4, .L1731
	ldr	r3, [r4, #340]
	cmp	r3, r1
	bls	.L1718
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bcc	.L1718
	adds	r2, r1, r2
	str	r2, [sp, #8]
	cmp	r3, r2
	bcc	.L1718
	ldr	r3, .L1731+4
	ldr	r6, [r3]
	adds	r7, r6, #1
	beq	.L1691
	ldrh	r6, [r4, #258]
	mov	r0, r5
	mov	r10, #0
	mov	r7, r10
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	mov	r1, r6
	str	r0, [sp, #16]
	subs	r0, r3, #1
	mov	r6, r10
	bl	__aeabi_uidiv
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #4]
	rsb	r3, r3, #1
	ldr	r9, [sp, #16]
	add	r3, r3, r0
	str	r3, [sp, #12]
	ldr	r3, [r4, #2584]
	str	r0, [sp, #20]
	add	r3, r3, r2
	ldr	r2, [sp, #12]
	str	r3, [r4, #2584]
	ldr	r3, [r4, #2556]
	strd	r10, r10, [sp, #24]
	add	r3, r3, r2
	str	r3, [r4, #2556]
.L1695:
	ldr	r3, [sp, #12]
	cbnz	r3, .L1713
	ldr	r3, .L1731
	ldrh	r2, [r3, #3206]
	cbnz	r2, .L1714
	ldrh	r3, [r3, #224]
	cmp	r3, #31
	bhi	.L1691
.L1714:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
.L1691:
	mov	r3, sp
	ldr	r2, [sp, #52]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	bne	.L1730
	mov	r0, r6
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1713:
	add	r1, sp, #48
	mov	r0, r9
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #48]
	adds	r0, r3, #1
	bne	.L1728
	mov	fp, #0
	b	.L1696
.L1699:
	mla	r0, r9, r0, fp
	cmp	r5, r0
	bhi	.L1698
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1698
	subs	r0, r0, r5
	mov	r2, #512
	movs	r1, #0
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L1698:
	add	fp, fp, #1
.L1696:
	ldrh	r0, [r4, #258]
	cmp	r0, fp
	bhi	.L1699
.L1700:
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	subs	r3, r3, #1
	str	r3, [sp, #12]
	beq	.L1705
	ldrh	r3, [r4, #232]
	cmp	r7, r3, lsl #2
	bne	.L1695
.L1705:
	cmp	r7, #0
	beq	.L1695
	ldr	r0, [r4, #3288]
	mov	r1, r7
	movs	r2, #0
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r10, #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #28]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1712:
	movs	r3, #20
	ldr	r2, [r4, #3288]
	ldr	r0, [sp, #16]
	mul	r3, r3, fp
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r1, r0
	bne	.L1707
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3312]
	cmp	r1, r2
	bne	.L1708
	str	r3, [sp, #44]
	mov	r0, r8
	ldrd	r3, r2, [sp, #32]
	add	r1, r1, r3
.L1729:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1708:
	ldr	ip, [r4, #3288]
	add	r2, ip, r3
	ldrd	r0, r1, [r2, #12]
	ldr	r0, [r0, #8]
	cmp	r1, r0
	ittt	ne
	ldrne	r1, [r4, #2712]
	addne	r1, r1, #1
	strne	r1, [r4, #2712]
	ldr	r0, [ip, r3]
	adds	r1, r0, #1
	itttt	eq
	moveq	r6, r0
	ldreq	r1, [r4, #2712]
	addeq	r1, r1, #1
	streq	r1, [r4, #2712]
	ldr	r3, [ip, r3]
	cmp	r3, #256
	bne	.L1711
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1711:
	add	fp, fp, #1
	cmp	r7, fp
	bne	.L1712
	movs	r7, #0
	b	.L1695
.L1728:
	ldr	r2, [r4, #3288]
	mov	fp, #20
	mla	fp, fp, r7, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #16]
	cmp	r9, r3
	ldrh	r3, [r4, #258]
	bne	.L1701
	ldr	r2, [r4, #3312]
	mov	r1, r3
	str	r2, [fp, #8]
	mov	r0, r5
	str	r3, [sp, #28]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #28]
	str	r1, [sp, #24]
	subs	r2, r3, r1
	ldr	r1, [sp, #4]
	cmp	r2, r1
	it	cs
	movcs	r2, r1
	str	r2, [sp, #28]
	cmp	r3, r2
	bne	.L1702
	str	r8, [fp, #8]
.L1702:
	ldrh	r3, [r4, #312]
	ldr	r2, [r4, #3336]
	muls	r3, r7, r3
	adds	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	strd	r3, r9, [fp, #12]
	b	.L1700
.L1701:
	ldr	r1, [sp, #20]
	mul	r2, r3, r9
	cmp	r9, r1
	bne	.L1703
	ldr	r1, [sp, #8]
	sub	r10, r1, r2
	cmp	r10, r3
	beq	.L1703
	ldr	r3, [r4, #3316]
	str	r3, [fp, #8]
	b	.L1702
.L1703:
	subs	r2, r2, r5
	add	r2, r8, r2, lsl #9
	str	r2, [fp, #8]
	b	.L1702
.L1707:
	ldr	r0, [sp, #20]
	cmp	r1, r0
	bne	.L1708
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1708
	ldrh	r0, [r4, #258]
	str	r3, [sp, #44]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r5
	add	r0, r8, r0, lsl #9
	b	.L1729
.L1718:
	mov	r6, #-1
	b	.L1691
.L1732:
	.align	2
.L1731:
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
	.section	.rodata.str1.1
.LC125:
	.ascii	"FtlWrite: ecc error:%x %x %x\012\000"
.LC126:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
	.text
	.align	1
	.global	FtlWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r0, #16
	mov	r8, r1
	.pad #76
	sub	sp, sp, #76
	strd	r2, r3, [sp, #4]
	mov	r3, sp
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r2, [r3, #24]
	str	r2, [sp, #68]
	bne	.L1735
	ldr	r2, [sp, #68]
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1736
.L1790:
	bl	__stack_chk_fail
.L1736:
	ldrd	r1, r2, [sp, #4]
	add	r0, r8, #256
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FtlVendorPartWrite
.L1735:
	ldr	r5, .L1791
	ldr	r3, [r5, #340]
	cmp	r3, r1
	bls	.L1777
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bcc	.L1777
	adds	r6, r1, r2
	cmp	r3, r6
	bcc	.L1777
	ldr	r4, .L1791+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1734
	ldrh	r7, [r5, #258]
	mov	r3, #2048
	mov	r0, r8
	str	r3, [r5, #3668]
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [sp]
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp]
	str	r0, [sp, #24]
	subs	r3, r0, r2
	ldr	r2, [sp, #4]
	add	fp, r3, #1
	str	r3, [sp, #20]
	ldr	r3, [r5, #2580]
	add	r3, r3, r2
	str	r3, [r5, #2580]
	ldr	r3, [r5, #2564]
	add	r3, r3, fp
	str	r3, [r5, #2564]
	ldr	r3, [r4, #8]
	cbz	r3, .L1779
	ldrh	r2, [r5, #28]
	add	r3, r5, #24
	adds	r5, r5, #76
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1738:
	ldr	r7, [sp]
	ldr	r4, .L1791
.L1739:
	cmp	fp, #0
	bne	.L1770
	ldr	r1, [sp, #20]
	mov	r0, fp
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #224]
	cmp	r3, #5
	bls	.L1782
.L1773:
	movs	r0, #0
.L1734:
	mov	r3, sp
	ldr	r2, [sp, #68]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	bne	.L1790
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1779:
	adds	r5, r5, #24
	b	.L1738
.L1770:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #232]
	cmp	r2, r3
	bcc	.L1740
	ldr	r1, .L1791+8
	movw	r2, #1041
	ldr	r0, .L1791+12
	bl	sftl_printk
.L1740:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1741
	ldr	r2, .L1791+16
	ldr	r6, .L1791+4
	cmp	r5, r2
	bne	.L1742
	ldrh	r9, [r4, #80]
	cmp	r9, #0
	bne	.L1743
	add	r0, r5, #52
	bl	allocate_new_data_superblock
	str	r9, [r6, #8]
.L1743:
	ldr	r0, .L1791+16
	bl	allocate_new_data_superblock
	ldr	r3, [r6, #8]
	cmp	r3, #0
	bne	.L1781
.L1745:
	ldr	r5, .L1791+16
.L1744:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1741
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1741:
	ldrh	r3, [r5, #4]
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r3, fp
	it	cs
	movcs	r3, fp
	lsls	r2, r2, #2
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrh	r3, [r4, #232]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1746
	ldr	r1, .L1791+8
	movw	r2, #1074
	ldr	r0, .L1791+12
	bl	sftl_printk
.L1746:
	mov	r10, #0
.L1747:
	ldr	r3, [sp, #36]
	cmp	r3, r10
	bne	.L1768
.L1748:
	ldr	r0, [r4, #3296]
	mov	r3, r5
	mov	r1, r10
	movs	r2, #0
	bl	FtlProgPages
	cmp	fp, r10
	bcs	.L1769
	ldr	r1, .L1791+8
	mov	r2, #1152
	ldr	r0, .L1791+12
	bl	sftl_printk
.L1769:
	sub	fp, fp, r10
	b	.L1739
.L1742:
	str	r3, [r6, #8]
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	bne	.L1745
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1744
.L1781:
	ldr	r5, .L1791+20
	b	.L1744
.L1768:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1748
	add	r1, sp, #44
	movs	r2, #0
	mov	r0, r7
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	movs	r3, #20
	mul	r3, r3, r10
	str	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #3296]
	add	r3, r3, r2
	ldrh	r2, [r4, #312]
	str	r0, [r3, #4]
	mul	r1, r10, r2
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3336]
	str	r1, [sp, #32]
	add	r9, r1, r0
	movs	r1, #0
	strd	r9, r7, [r3, #12]
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [sp]
	ldrh	r6, [r4, #258]
	cmp	r7, r3
	beq	.L1749
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #3296]
	add	r3, r3, r2
	ldr	r2, [sp, #24]
	cmp	r7, r2
	bne	.L1787
	ldr	r3, [sp, #4]
	add	r2, r8, r3
	smulbb	r3, r7, r6
	subs	r6, r2, r3
	movs	r3, #0
	str	r3, [sp, #16]
	uxth	r6, r6
	b	.L1752
.L1749:
	mov	r1, r6
	mov	r0, r8
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	subs	r6, r6, r1
	str	r1, [sp, #16]
	cmp	r6, r3
	it	cs
	movcs	r6, r3
.L1752:
	ldrh	r2, [r4, #258]
	ldr	r3, [r4, #3296]
	ldr	r1, [sp, #12]
	cmp	r2, r6
	ldr	r2, [sp]
	add	r3, r3, r1
	bne	.L1753
	cmp	r7, r2
	bne	.L1787
	ldr	r2, [sp, #8]
	str	r2, [r3, #8]
.L1755:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #232]
	cmp	r2, r3
	bcc	.L1765
	ldr	r1, .L1791+8
	movw	r2, #1143
	ldr	r0, .L1791+12
	bl	sftl_printk
.L1765:
	ldrd	r1, r2, [sp, #28]
	movw	r3, #61589
	add	r10, r10, #1
	strh	r3, [r2, r1]	@ movhi
	str	r7, [r9, #8]
	adds	r7, r7, #1
	ldr	r3, [r4, #2592]
	str	r3, [r9, #4]
	adds	r2, r3, #1
	adds	r3, r3, #2
	itee	ne
	strne	r2, [r4, #2592]
	moveq	r3, #0
	streq	r3, [r4, #2592]
	ldr	r3, [sp, #44]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	b	.L1747
.L1753:
	cmp	r7, r2
	ite	eq
	ldreq	r2, [r4, #3312]
	ldrne	r2, [r4, #3316]
	str	r2, [r3, #8]
	ldr	r2, [sp, #44]
	adds	r1, r2, #1
	beq	.L1758
	str	r7, [sp, #64]
	add	r0, sp, #48
	str	r2, [sp, #52]
	movs	r1, #1
	ldr	r2, [r3, #8]
	str	r2, [sp, #56]
	movs	r2, #0
	ldr	r3, [r3, #12]
	str	r3, [sp, #60]
	bl	FlashReadPages
	ldr	r3, [sp, #48]
	adds	r2, r3, #1
	bne	.L1759
	ldr	r2, [r4, #2712]
	ldr	r0, .L1791+24
	adds	r2, r2, #1
	str	r2, [r4, #2712]
	mov	r2, r7
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1763:
	ldr	r3, [sp]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1764
	ldr	r1, [sp, #12]
	ldr	r3, [r4, #3296]
	add	r3, r3, r1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1789:
	bl	ftl_memcpy
	b	.L1755
.L1792:
	.align	2
.L1791:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LC125
.L1759:
	ldr	r3, [r9, #8]
	cmp	r3, r7
	beq	.L1763
	ldr	r3, [r4, #2712]
	mov	r2, r7
	ldr	r0, .L1793
	adds	r3, r3, #1
	str	r3, [r4, #2712]
	ldr	r1, [r9, #8]
	bl	sftl_printk
	ldr	r3, [r9, #8]
	cmp	r3, r7
	beq	.L1763
	ldr	r1, .L1793+4
	mov	r2, #1128
	ldr	r0, .L1793+8
	bl	sftl_printk
	b	.L1763
.L1758:
	ldrh	r2, [r4, #310]
	movs	r1, #0
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1763
.L1764:
	ldrh	r1, [r4, #258]
	ldr	r0, [sp, #12]
	ldr	r3, [r4, #3296]
	muls	r1, r7, r1
	add	r3, r3, r0
	ldr	r0, [sp, #8]
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1789
.L1787:
	muls	r6, r7, r6
	ldr	r2, [sp, #8]
	sub	r6, r6, r8
	add	r6, r2, r6, lsl #9
	str	r6, [r3, #8]
	b	.L1755
.L1782:
	mov	r5, #256
	movw	r6, #65535
.L1771:
	ldrh	r2, [r4, #222]
	ldrh	r3, [r4, #172]
	ands	r3, r3, r2
	cmp	r3, r6
	bne	.L1772
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1772:
	movs	r1, #1
	mov	r0, r1
	movs	r3, #128
	strh	r3, [r4, #3154]	@ movhi
	strh	r3, [r4, #3152]	@ movhi
	bl	rk_ftl_garbage_collect
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #224]
	cmp	r3, #2
	bhi	.L1773
	subs	r5, r5, #1
	bne	.L1771
	b	.L1773
.L1777:
	mov	r0, #-1
	b	.L1734
.L1794:
	.align	2
.L1793:
	.word	.LC126
	.word	.LANCHOR1+591
	.word	.LC8
	.fnend
	.size	FtlWrite, .-FtlWrite
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
	.section	.rodata.str1.1
.LC127:
	.ascii	"%s hash error this.id =%x page =%x pre_id =%x hash "
	.ascii	"=%x hash_r =%x\012\000"
	.text
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
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	movs	r1, #0
	ldr	r4, .L1824
	ldrh	r2, [r4, #240]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldr	r0, [r4, #72]
	lsls	r2, r2, #1
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	bl	ftl_memset
	ldrh	r0, [r4, #2624]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1797
.L1809:
	mov	r0, #-1
.L1796:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1797:
	movs	r1, #1
	ldr	r8, .L1824+4
	bl	FtlGetLastWrittenPage
	ldr	r9, .L1824+8
	ldrsh	r6, [r4, #2624]
	sxth	r5, r0
	addw	r7, r4, #3452
	adds	r3, r5, #1
	strh	r3, [r4, #2626]	@ movhi
.L1799:
	cmp	r5, #0
	bge	.L1806
	ldr	r1, .L1824+8
	movw	r2, #1474
	ldr	r0, .L1824+12
	bl	sftl_printk
	b	.L1805
.L1806:
	orr	r3, r5, r6, lsl #10
	movs	r2, #1
	str	r3, [r4, #3456]
	mov	r1, r2
	ldr	r3, [r4, #3300]
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	ldr	r10, [r3, #12]
	ldr	r3, [r4, #3452]
	cmp	r10, #0
	beq	.L1800
	adds	r3, r3, #1
	beq	.L1801
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3460]
	bl	js_hash
	cmp	r10, r0
	beq	.L1802
	ldrh	r3, [r4, #2628]
	mov	r2, r6
	strd	r10, r0, [sp, #4]
	mov	r1, r9
	str	r3, [sp]
	mov	r3, r5
	ldr	r0, .L1824+16
	bl	sftl_printk
	cbnz	r5, .L1803
	ldrh	r3, [r4, #2628]
	cmp	r6, r3
	beq	.L1803
	ldrh	r5, [r4, #304]
	sxth	r6, r3
.L1801:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1799
.L1803:
	mov	r3, #-1
	str	r3, [r4, #3452]
	b	.L1801
.L1800:
	adds	r3, r3, #1
	beq	.L1801
.L1802:
	ldr	r3, [r4, #3300]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1801
	ldr	r3, [r4, #3332]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1801
.L1805:
	ldrh	r3, [r4, #240]
	ldrh	r2, [r4, #310]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1808
	ldr	r1, .L1824+8
	movw	r2, #1476
	ldr	r0, .L1824+12
	bl	sftl_printk
.L1808:
	ldr	r1, [r4, #3460]
	movs	r2, #48
	ldr	r0, .L1824+20
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r1, [r4, #3460]
	ldr	r0, [r4, #72]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldr	r2, [r4, #2468]
	ldr	r3, .L1824+4
	cmp	r2, r3
	bne	.L1809
	ldrb	r2, [r4, #2478]	@ zero_extendqisi2
	ldrh	r3, [r4, #254]
	ldrh	r5, [r4, #2476]
	cmp	r2, r3
	strh	r5, [r4, #2630]	@ movhi
	bne	.L1809
	ldrh	r3, [r4, #302]
	ldrh	r2, [r4, #258]
	ldrh	r0, [r4, #358]
	ldr	r6, [r4, #244]
	muls	r3, r5, r3
	add	r0, r0, r5
	str	r3, [r4, #2552]
	subs	r0, r6, r0
	ldrh	r1, [r4, #232]
	str	r5, [r4, #3672]
	muls	r3, r2, r3
	str	r3, [r4, #340]
	bl	__aeabi_uidiv
	cmp	r5, r6
	strh	r0, [r4, #2620]	@ movhi
	bls	.L1810
	ldr	r1, .L1824+8
	movw	r2, #1498
	ldr	r0, .L1824+12
	bl	sftl_printk
.L1810:
	ldrh	r3, [r4, #2484]
	ldrh	r1, [r4, #2482]
	strh	r1, [r4, #24]	@ movhi
	lsrs	r2, r3, #6
	strh	r2, [r4, #26]	@ movhi
	ldrh	r2, [r4, #2486]
	and	r3, r3, #63
	strh	r2, [r4, #76]	@ movhi
	ldrh	r2, [r4, #2488]
	strb	r3, [r4, #30]
	ldrb	r3, [r4, #2479]	@ zero_extendqisi2
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #82]
	ldrb	r2, [r4, #2480]	@ zero_extendqisi2
	strb	r2, [r4, #84]
	ldrh	r2, [r4, #2490]
	strh	r2, [r4, #124]	@ movhi
	ldrh	r2, [r4, #2492]
	strh	r0, [r4, #78]	@ movhi
	strb	r3, [r4, #32]
	movw	r3, #65535
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #130]
	ldrb	r2, [r4, #2481]	@ zero_extendqisi2
	strb	r2, [r4, #132]
	ldr	r2, [r4, #2500]
	str	r3, [r4, #172]
	movs	r3, #0
	str	r2, [r4, #2596]
	strb	r3, [r4, #178]
	strb	r3, [r4, #180]
	str	r3, [r4, #2576]
	str	r3, [r4, #2564]
	str	r3, [r4, #2556]
	str	r3, [r4, #2572]
	str	r3, [r4, #2600]
	str	r3, [r4, #2612]
	strh	r0, [r4, #126]	@ movhi
	str	r3, [r4, #2568]
	ldr	r3, [r4, #2508]
	ldr	r2, [r4, #2588]
	cmp	r3, r2
	ldr	r2, [r4, #2592]
	it	hi
	strhi	r3, [r4, #2588]
	ldr	r3, [r4, #2504]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #2592]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1813
	ldr	r0, .L1824+24
	bl	make_superblock
.L1813:
	ldrh	r2, [r4, #76]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1814
	ldr	r0, .L1824+28
	bl	make_superblock
.L1814:
	ldrh	r2, [r4, #124]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1815
	ldr	r0, .L1824+32
	bl	make_superblock
.L1815:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1816
	ldr	r0, .L1824+36
	bl	make_superblock
.L1816:
	movs	r0, #0
	b	.L1796
.L1825:
	.align	2
.L1824:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LC127
	.word	.LANCHOR0+2468
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+124
	.word	.LANCHOR0+172
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.section	.rodata.str1.1
.LC128:
	.ascii	"%s last blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC129:
	.ascii	"%s scan blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
	.text
	.align	1
	.global	FtlMapTblRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L1865
	.pad #44
	sub	sp, sp, #44
	ldr	r3, [r0, #24]
	movs	r1, #0
	addw	r9, r4, #3452
	movs	r7, #0
	str	r3, [sp, #12]
	ldr	r3, [r0, #12]
	str	r3, [sp, #24]
	ldr	r3, [r0, #16]
	str	r3, [sp, #32]
	ldrh	r3, [r0, #6]
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	str	r3, [sp, #28]
	ldr	r3, [sp, #16]
	ldr	r0, [sp, #12]
	lsls	r2, r3, #2
	bl	ftl_memset
	ldr	r3, [r4, #3300]
	ldr	fp, [r4, #3332]
	str	r3, [r4, #3460]
	mov	r3, #-1
	str	fp, [r4, #3464]
	strd	r7, r7, [r5, #28]
	str	r3, [r5]
	movs	r3, #1
	str	r3, [r5, #36]
.L1827:
	ldr	r3, [sp, #28]
	sxth	r6, r7
	uxth	r8, r7
	cmp	r6, r3
	bge	.L1847
	ldr	r3, [sp, #24]
	add	r3, r3, r6, lsl #1
	str	r3, [sp, #20]
	ldr	r3, [sp, #28]
	subs	r3, r3, #1
	cmp	r3, r6
	bne	.L1828
	ldr	r3, [sp, #20]
	movs	r1, #1
	ldr	r10, .L1865+4
	ldrh	r0, [r3]
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	adds	r3, r7, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [sp, #32]
	strh	r8, [r5]	@ movhi
	mov	r8, #0
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r5, #28]
.L1829:
	sxth	r9, r8
	adds	r2, r7, #1
	cmp	r9, r2
	blt	.L1833
.L1847:
	mov	r0, r5
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #304]
	cmp	r2, r3
	bne	.L1835
	mov	r0, r5
	bl	ftl_map_blk_alloc_new_blk
.L1835:
	mov	r0, r5
	bl	ftl_map_blk_gc
	mov	r0, r5
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1833:
	ldr	r3, [sp, #20]
	ldr	r0, .L1865+8
	ldrh	r2, [r3]
	orr	r2, r9, r2, lsl #10
	str	r2, [r4, #3456]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3464]
	ldr	r2, [r2, #12]
	str	r2, [sp, #24]
	cbz	r2, .L1830
	ldr	r1, [r4, #3452]
	adds	r1, r1, #1
	beq	.L1848
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3460]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	beq	.L1832
	strd	r2, r0, [sp]
	mov	r3, r9
	ldr	r0, .L1865+12
	mov	r2, r6
	mov	r1, r10
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3452]
.L1848:
	add	r8, r8, #1
	b	.L1829
.L1830:
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L1848
.L1832:
	ldrh	r3, [fp, #8]
	ldr	r2, [sp, #16]
	cmp	r3, r2
	bcs	.L1848
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r1, [sp, #12]
	ldreq	r2, [r4, #3456]
	streq	r2, [r1, r3, lsl #2]
	b	.L1848
.L1828:
	ldr	r3, [r4, #3300]
	mov	r0, r9
	str	r3, [r4, #3460]
	ldr	r3, [sp, #24]
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r4, #304]
	subs	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L1851
	ldrh	r2, [fp]
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bne	.L1851
	ldrh	r2, [fp, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1852
.L1851:
	mov	r8, #0
	b	.L1843
.L1839:
	ldr	r2, [r4, #3300]
	lsls	r6, r0, #3
	ldr	ip, [sp, #16]
	ldr	r3, [r2, r0, lsl #3]
	uxth	r0, r3
	cmp	ip, r0
	bls	.L1838
	add	r2, r2, r6
	mov	r3, r0
	ldr	r0, [sp, #12]
	ldr	r2, [r2, #4]
	str	r2, [r0, r3, lsl #2]
.L1838:
	adds	r1, r1, #1
.L1837:
	ldrh	r3, [r4, #304]
	sxth	r0, r1
	subs	r3, r3, #1
	cmp	r0, r3
	blt	.L1839
.L1845:
	adds	r7, r7, #1
	b	.L1827
.L1852:
	movs	r1, #0
	b	.L1837
.L1844:
	ldr	r3, [sp, #20]
	mov	r0, r9
	ldrh	r2, [r3]
	orr	r2, r10, r2, lsl #10
	str	r2, [r4, #3456]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3464]
	ldr	r2, [r2, #12]
	str	r2, [sp, #36]
	cbz	r2, .L1840
	ldr	r1, [r4, #3452]
	adds	r1, r1, #1
	beq	.L1849
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3460]
	bl	js_hash
	ldr	r2, [sp, #36]
	cmp	r2, r0
	beq	.L1842
	strd	r2, r0, [sp]
	mov	r3, r10
	ldr	r1, .L1865+4
	mov	r2, r6
	ldr	r0, .L1865+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3452]
.L1849:
	add	r8, r8, #1
.L1843:
	ldrh	r2, [r4, #304]
	sxth	r10, r8
	cmp	r10, r2
	blt	.L1844
	b	.L1845
.L1840:
	ldr	r3, [r4, #3452]
	adds	r3, r3, #1
	beq	.L1849
.L1842:
	ldrh	r3, [fp, #8]
	ldr	r2, [sp, #16]
	cmp	r3, r2
	bcs	.L1849
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r1, [sp, #12]
	ldreq	r2, [r4, #3456]
	streq	r2, [r1, r3, lsl #2]
	b	.L1849
.L1866:
	.align	2
.L1865:
	.word	.LANCHOR0
	.word	.LANCHOR1+615
	.word	.LANCHOR0+3452
	.word	.LC128
	.word	.LC129
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
	ldr	r0, .L1868
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #320]
	strh	r3, [r0, #3618]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3612]	@ movhi
	ldrh	r3, [r0, #344]
	strh	r3, [r0, #3616]	@ movhi
	ldrh	r3, [r0, #322]
	strh	r3, [r0, #3614]	@ movhi
	ldr	r3, [r0, #348]
	addw	r0, r0, #3608
	str	r3, [r0, #12]
	ldr	r3, [r0, #-240]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-244]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-236]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1869:
	.align	2
.L1868:
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
	ldr	r0, .L1871
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1872:
	.align	2
.L1871:
	.word	.LANCHOR0+3392
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	ldr	r4, .L1889
	ldrh	r0, [r4, #236]
	strh	r3, [r4, #3472]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2624]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1874
.L1876:
	mov	r5, #-1
.L1873:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1874:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1876
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r4, #2540]
	ldrh	r1, [r4, #338]
	mov	r3, r5
	adds	r2, r2, #4
	movs	r0, #12
.L1877:
	cmp	r3, r1
	bge	.L1882
	mul	r6, r0, r3
	ldr	r6, [r2, r6]
	cmp	r6, #0
	bge	.L1878
.L1882:
	ldrh	r2, [r4, #2496]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2496]	@ movhi
	bge	.L1888
.L1879:
	ldr	r0, .L1889+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1889+8
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #24]
	ldr	r2, [r4, #72]
	ldrh	r0, [r4, #28]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r0, [r4, #76]
	ldr	r1, [r4, #72]
	ldrh	r3, [r4, #302]
	strh	r3, [r4, #26]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #30]
	strh	r3, [r4, #28]	@ movhi
	ldrh	r6, [r4, #80]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	strb	r3, [r4, #82]
	strh	r3, [r4, #80]	@ movhi
	ldrh	r3, [r4, #2498]
	ldrh	r2, [r4, #302]
	adds	r3, r3, #1
	strh	r2, [r4, #78]	@ movhi
	strh	r3, [r4, #2498]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1883
.L1878:
	adds	r3, r3, #1
	b	.L1877
.L1888:
	ldrh	r3, [r4, #3472]
	cmp	r3, #0
	bne	.L1879
.L1883:
	ldrh	r0, [r4, #24]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1884
	ldrh	r3, [r4, #28]
	ldrh	r2, [r4, #80]
	orrs	r3, r3, r2
	uxth	r3, r3
	cbnz	r3, .L1884
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #76]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1889+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1889+8
	bl	allocate_new_data_superblock
.L1884:
	bl	FtlVpcCheckAndModify
	b	.L1873
.L1890:
	.align	2
.L1889:
	.word	.LANCHOR0
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
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
	ldr	r4, .L1915
	ldrh	r0, [r4, #236]
	str	r3, [r4, #2588]
	str	r3, [r4, #2592]
	str	r3, [r4, #2604]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1892
	bl	FtlMakeBbt
.L1892:
	ldr	r0, .L1915+4
	movs	r2, #0
.L1893:
	ldrh	r1, [r4, #258]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1894
	ldrh	r6, [r4, #240]
	movs	r5, #0
.L1895:
	ldrh	r3, [r4, #242]
	cmp	r3, r6
	bhi	.L1896
	ldrh	r1, [r4, #232]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1897
.L1901:
	movs	r5, #0
	mov	r8, r5
.L1898:
	ldrh	r3, [r4, #240]
	uxth	r0, r5
	cmp	r3, r0
	bhi	.L1902
	ldrh	r5, [r4, #232]
	ldr	r9, [r4, #244]
	ldrh	r3, [r4, #242]
	mov	r1, r5
	mov	r0, r9
	str	r3, [r4, #3284]
	bl	__aeabi_uidiv
	ubfx	r7, r0, #5, #16
	add	r3, r7, #36
	strh	r3, [r4, #2620]	@ movhi
	movs	r3, #24
	mov	r6, r0
	str	r0, [r4, #2552]
	muls	r3, r5, r3
	cmp	r8, r3
	ble	.L1903
	mov	r1, r5
	sub	r0, r9, r8
	bl	__aeabi_uidiv
	lsrs	r3, r0, #5
	adds	r3, r3, #24
	str	r0, [r4, #2552]
	strh	r3, [r4, #2620]	@ movhi
.L1903:
	ldrh	r3, [r4, #294]
	cbz	r3, .L1905
	ldrh	r2, [r4, #2620]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #2620]	@ movhi
	mul	r2, r5, r3
	cmp	r8, r2
	itttt	lt
	strlt	r6, [r4, #2552]
	addlt	r3, r3, #32
	addlt	r3, r3, r7
	strhlt	r3, [r4, #2620]	@ movhi
.L1905:
	ldrh	r1, [r4, #2620]
	ldr	r3, [r4, #2552]
	ldr	r6, .L1915+8
	subs	r3, r3, r1
	mul	r1, r5, r3
	ldrh	r3, [r4, #302]
	str	r1, [r4, #3672]
	movw	r5, #65535
	muls	r1, r3, r1
	ldrh	r3, [r4, #258]
	str	r1, [r4, #2552]
	muls	r1, r3, r1
	str	r1, [r4, #340]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #242]
	movs	r1, #0
	ldr	r0, [r4, #72]
	lsls	r2, r2, #1
	bl	ftl_memset
	movs	r3, #0
	str	r5, [r4, #172]
	str	r3, [r4, #2548]
	strb	r3, [r4, #178]
	strb	r3, [r4, #180]
	strb	r3, [r4, #30]
	str	r3, [r4, #24]
	movs	r3, #1
	strb	r3, [r4, #32]
.L1907:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	ldrh	r2, [r4, #24]
	cmp	r3, #0
	bne	.L1908
	ldr	r3, [r4, #72]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #24]
	adds	r3, r3, #1
	strh	r3, [r4, #24]	@ movhi
	b	.L1907
.L1894:
	ldr	r5, [r4, #3312]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3316]
	str	r0, [r1, r3, lsl #2]
	b	.L1893
.L1896:
	mov	r0, r6
	movs	r1, #1
	adds	r6, r6, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, r0
	uxth	r6, r6
	uxth	r5, r5
	b	.L1895
.L1897:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #332]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #240]
.L1899:
	ldrh	r3, [r4, #242]
	cmp	r3, r5
	bls	.L1901
	mov	r0, r5
	adds	r5, r5, #1
	movs	r1, #1
	uxth	r5, r5
	bl	FtlLowFormatEraseBlock
	b	.L1899
.L1902:
	movs	r1, #0
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	add	r8, r8, r0
	uxth	r8, r8
	b	.L1898
.L1908:
	ldr	r3, [r4, #2588]
	movw	r5, #65535
	str	r3, [r4, #36]
	adds	r3, r3, #1
	str	r3, [r4, #2588]
	ldr	r3, [r4, #72]
	ldrh	r1, [r4, #28]
	ldr	r6, .L1915+12
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #78]	@ movhi
	strb	r3, [r4, #82]
	ldrh	r3, [r4, #24]
	adds	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #84]
.L1909:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #83]	@ zero_extendqisi2
	ldrh	r2, [r4, #76]
	cbnz	r3, .L1910
	ldr	r3, [r4, #72]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #76]
	adds	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	b	.L1909
.L1910:
	ldr	r3, [r4, #2588]
	movw	r5, #65535
	str	r3, [r4, #88]
	adds	r3, r3, #1
	str	r3, [r4, #2588]
	ldr	r3, [r4, #72]
	ldrh	r1, [r4, #80]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r5, [r4, #124]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r4, #2626]	@ movhi
	ldr	r3, [r4, #3672]
	strh	r3, [r4, #2630]	@ movhi
	ldr	r3, [r4, #2588]
	str	r3, [r4, #2632]
	strh	r0, [r4, #2624]	@ movhi
	adds	r3, r3, #1
	strh	r5, [r4, #2628]	@ movhi
	str	r3, [r4, #2588]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L1911
	ldr	r3, .L1915+16
	movs	r2, #1
	str	r2, [r3]
.L1911:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1916:
	.align	2
.L1915:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
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
	ldr	r5, .L1921
	ldr	r4, .L1921+4
	ldr	r1, .L1921+8
	ldr	r0, .L1921+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1918
	bl	FtlSysBlkInit
	cbnz	r0, .L1918
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #224]
	cmp	r3, #15
	bhi	.L1918
	movw	r4, #8129
.L1919:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1919
.L1918:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1922:
	.align	2
.L1921:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC0
	.word	.LC77
	.fnend
	.size	sftl_init, .-sftl_init
	.section	.rodata.str1.1
.LC130:
	.ascii	"write_idblock %x %x\012\000"
.LC131:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC132:
	.ascii	"%s idb buffer alloc fail\012\000"
	.text
	.align	1
	.global	FtlWriteToIDB
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	add	r8, r1, r0
	add	r9, r8, #-1
	.pad #116
	sub	sp, sp, #116
	ldr	r5, .L1988
	mov	r3, sp
	mov	r4, r0
	bic	r3, r3, #8160
	mov	r6, r1
	bic	r3, r3, #31
	mov	r7, r2
	cmp	r9, #63
	ldr	r3, [r3, #24]
	str	r3, [sp, #108]
	bls	.L1924
	cmp	r0, #576
	bcc	.L1925
.L1924:
	ldr	r3, [r5, #3676]
	cbnz	r3, .L1926
.L1987:
	movs	r4, #0
	b	.L1923
.L1926:
	ldr	r3, [r5, #3680]
	str	r3, [sp, #12]
	ldr	r2, [r3]
	ldr	r3, .L1988+4
	cmp	r2, r3
	bne	.L1928
	ldr	r3, [sp, #12]
	movs	r2, #0
	ldrh	r1, [r5, #10]
	add	r0, r3, #260096
	movw	r3, #65023
.L1932:
	ldr	r4, [r0, #-4]!
	cbnz	r4, .L1929
	ldr	r4, [sp, #12]
	subs	r3, r3, #1
	ldr	r4, [r4, r2, lsl #2]
	adds	r2, r2, #1
	cmp	r2, #4096
	str	r4, [r0, #2048]
	it	hi
	movhi	r2, #0
	cmp	r3, #4096
	bne	.L1932
	mov	r9, #512
	b	.L1931
.L1929:
	adds	r3, r3, #127
	lsr	r9, r3, #7
.L1931:
	lsls	r1, r1, #2
	add	r0, r9, #4
	ldr	r4, .L1988
	uxth	r1, r1
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	mov	r2, r3
	ldr	r0, .L1988+8
	mov	r1, r9
	str	r3, [sp, #16]
	bl	sftl_printk
	lsl	r3, r9, #7
	str	r3, [sp, #32]
	movs	r3, #0
	mov	fp, r3
	str	r3, [sp, #20]
.L1933:
	ldr	r3, [sp, #16]
	add	r3, r3, fp
	str	r3, [sp, #24]
	cmp	r3, #8
	bls	.L1955
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1954
.L1928:
	mov	r9, #-1
	b	.L1954
.L1955:
	ldr	r0, [r4, #3684]
	mov	r2, #512
	movs	r1, #0
	bl	memset
	ldrh	r6, [r4, #10]
	mul	r3, r6, fp
	str	r3, [sp, #28]
	ldr	r3, [r4, #3260]
	cmp	r3, #0
	beq	.L1964
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	ite	eq
	moveq	r8, #6
	movne	r8, #9
.L1934:
	mul	r7, r6, fp
	movs	r5, #0
.L1935:
	ldr	r3, [r4, #3248]
	mov	r1, r7
	movs	r0, #0
	adds	r5, r5, #1
	blx	r3
	ldr	r3, [sp, #16]
	add	r7, r7, r6
	cmp	r3, r5
	bhi	.L1935
	cmp	r8, #9
	bne	.L1966
	ldr	r5, [r4, #3684]
	mov	r2, #1024
	movs	r1, #0
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L1988+12
	movs	r1, #12
	strd	r3, r1, [r5]
	movs	r3, #0
	str	r3, [r5, #12]
	mov	r3, #1024
	strh	r3, [r5, #16]	@ movhi
	ldrh	r3, [r4, #10]
	adds	r0, r5, r1
	strh	r3, [r5, #18]	@ movhi
	mov	r3, #4096
	str	r3, [r5, #20]
	bl	js_hash
	str	r0, [r5, #8]
.L1936:
	ldr	r3, [sp, #16]
	ldr	r10, [sp, #12]
	muls	r3, r6, r3
	movs	r6, #0
	str	r3, [sp, #36]
.L1937:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	beq	.L1944
	cmp	r8, #9
	ite	eq
	addeq	r3, r6, #1
	lslne	r3, r6, #2
	str	r3, [sp, #44]
	movw	r3, #61424
	str	r3, [sp, #48]
	cmp	r6, #0
	bne	.L1940
	cmp	r8, #9
	bne	.L1940
	ldr	r3, [r4, #3260]
	movs	r0, #70
	blx	r3
	ldr	r7, [r4, #3252]
	ldr	r1, [sp, #28]
	add	r3, sp, #44
	mov	r2, r5
	mov	r0, r6
	blx	r7
	ldr	r3, [r4, #3260]
	mov	r7, r0
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	adds	r7, r7, #1
	bne	.L1941
.L1944:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldrh	r10, [r4, #10]
	str	r3, [sp, #28]
	ldr	r3, [r4, #3260]
	ldr	r5, [r4, #3684]
	mul	r7, r10, fp
	cmp	r3, #0
	bne	.L1942
	mov	r8, #6
.L1943:
	ldr	r3, [sp, #16]
	movs	r6, #0
	mul	r10, r10, r3
.L1946:
	cmp	r6, r10
	beq	.L1950
	cmp	r6, #0
	bne	.L1947
	cmp	r8, #9
	bne	.L1947
	ldr	r3, [r4, #3260]
	movs	r0, #70
	blx	r3
	ldr	r3, [r4, #3264]
	movs	r0, #2
	blx	r3
	mov	r2, r5
	ldr	ip, [r4, #3256]
	mov	r1, r7
	add	r3, sp, #44
	mov	r0, r6
	blx	ip
	ldr	r3, [r4, #3264]
	ldr	r0, [sp, #28]
	blx	r3
	ldr	r3, [r4, #3260]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r5]
	ldr	r2, .L1988+12
	cmp	r3, r2
	beq	.L1948
.L1950:
	ldr	r0, [r4, #3684]
	movs	r5, #0
	ldr	r1, [sp, #12]
	mov	ip, r0
.L1949:
	mov	r6, ip
	mov	r7, r1
	add	ip, ip, #4
	adds	r1, r1, #4
	ldr	r2, [r6]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1952
	mov	r2, #512
	movs	r1, #0
	bl	memset
	str	r5, [sp]
	ldr	r3, [r7]
	mov	r1, fp
	ldr	r0, .L1988+16
	ldr	r2, [r6]
	bl	sftl_printk
	ldrh	r1, [r4, #10]
	ldr	r3, [r4, #3248]
	movs	r0, #0
	mul	r1, fp, r1
	blx	r3
.L1953:
	ldr	fp, [sp, #24]
	b	.L1933
.L1964:
	mov	r8, #6
	b	.L1934
.L1966:
	movs	r5, #0
	b	.L1936
.L1940:
	ldr	r1, [sp, #28]
	add	r3, sp, #44
	ldr	r7, [r4, #3252]
	mov	r2, r10
	add	r1, r1, r6
	movs	r0, #0
	blx	r7
	adds	r0, r0, #1
	beq	.L1944
	add	r10, r10, #2048
.L1941:
	adds	r6, r6, #1
	b	.L1937
.L1942:
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	ite	eq
	moveq	r8, #6
	movne	r8, #9
	b	.L1943
.L1947:
	ldr	ip, [r4, #3256]
	add	r3, sp, #44
	mov	r2, r5
	adds	r1, r7, r6
	movs	r0, #0
	blx	ip
	adds	r0, r0, #1
	beq	.L1950
	ldr	r1, [sp, #48]
	movw	r3, #61424
	cmp	r1, r3
	bne	.L1950
	add	r5, r5, #2048
.L1948:
	adds	r6, r6, #1
	b	.L1946
.L1989:
	.align	2
.L1988:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC130
	.word	1179535694
	.word	.LC131
.L1952:
	ldr	r3, [sp, #32]
	adds	r5, r5, #1
	cmp	r5, r3
	bcc	.L1949
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	str	r3, [sp, #20]
	cmp	r3, #5
	bls	.L1953
.L1954:
	ldr	r5, .L1990
	movs	r3, #0
	mov	r4, r9
	ldr	r0, [r5, #3680]
	str	r3, [r5, #3676]
	bl	kfree
	ldr	r0, [r5, #3684]
	bl	kfree
.L1923:
	mov	r3, sp
	ldr	r2, [sp, #108]
	bic	r3, r3, #8160
	bic	r3, r3, #31
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1963
	bl	__stack_chk_fail
.L1925:
	cmp	r0, #64
	bne	.L1956
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3680]
	mov	r0, #262144
	bl	ftl_malloc
	mov	r3, r0
	str	r0, [r5, #3684]
	ldr	r0, [r5, #3680]
	cbz	r0, .L1957
	cbz	r3, .L1957
	mov	r2, #262144
	movs	r1, #0
	movs	r3, #1
	str	r3, [r5, #3676]
	bl	ftl_memset
.L1958:
	cmp	r4, #63
	ldr	r0, [r5, #3680]
	sub	r3, r4, #64
	ittte	ls
	rsbls	r4, r4, #64
	addls	r6, r6, r3
	addls	r7, r7, r4, lsl #9
	addhi	r0, r0, r3, lsl #9
	cmp	r9, #576
	mov	r1, r7
	itt	cs
	subcs	r6, r6, r8
	subcs	r6, r6, #446
	lsls	r2, r6, #9
	bl	ftl_memcpy
	b	.L1987
.L1957:
	ldr	r1, .L1990+4
	ldr	r0, .L1990+8
	bl	sftl_printk
.L1956:
	ldr	r3, [r5, #3676]
	cmp	r3, #0
	bne	.L1958
	b	.L1987
.L1963:
	mov	r0, r4
	add	sp, sp, #116
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1991:
	.align	2
.L1990:
	.word	.LANCHOR0
	.word	.LANCHOR1+633
	.word	.LC132
	.fnend
	.size	FtlWriteToIDB, .-FtlWriteToIDB
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r6, r2
	mov	r4, r0
	bl	FtlWriteToIDB
	mov	r3, r6
	mov	r2, r5
	mov	r1, r4
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
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
	ldr	r3, .L1996
	mov	r2, r0
	ldr	r0, [r3, #3688]
	cbnz	r0, .L1995
	str	r2, [r3, #3688]
	str	r1, [r3, #3692]
	bx	lr
.L1995:
	mov	r0, #-1
	bx	lr
.L1997:
	.align	2
.L1996:
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
	ldr	r5, .L2006
	bl	ftl_malloc
	str	r0, [r5, #3696]
	cmp	r0, #0
	beq	.L2004
	ldr	r9, .L2006+4
	mov	r8, #0
	mov	r7, r8
	mov	r6, r8
.L2002:
	ldr	r3, [r5, #3688]
	lsls	r0, r6, #7
	ldr	r2, [r5, #3696]
	movs	r1, #128
	blx	r3
	mov	r4, r0
	cbnz	r0, .L2000
	ldr	r3, [r5, #3696]
	ldr	r2, [r3]
	cmp	r2, r9
	bne	.L2001
	add	r2, r3, #61440
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #4092]
	cmp	r2, r1
	bne	.L2001
	cmp	r2, r7
	itt	hi
	movhi	r8, r6
	movhi	r7, r2
.L2001:
	cbz	r6, .L2005
	cbz	r7, .L2003
	ldr	r4, [r5, #3688]
	mov	r2, r3
	lsl	r0, r8, #7
	movs	r1, #128
	blx	r4
	mov	r4, r0
	cbz	r0, .L1998
.L2000:
	ldr	r0, [r5, #3696]
	mov	r4, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r5, #3696]
	b	.L1998
.L2005:
	movs	r6, #1
	b	.L2002
.L2003:
	mov	r2, #65536
	mov	r1, r7
	mov	r0, r3
	bl	memset
	add	r2, r0, #61440
	strd	r9, r6, [r0]
	str	r6, [r2, #4092]
	movw	r2, #64504
	strh	r2, [r0, #14]	@ movhi
.L1998:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2004:
	mvn	r4, #11
	b	.L1998
.L2007:
	.align	2
.L2006:
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
	mov	r6, r0
	ldr	r3, .L2014
	mov	r0, r1
	ldr	r5, [r3, #3696]
	cbz	r5, .L2013
	ldrh	r1, [r5, #10]
	add	r4, r5, #16
	movs	r3, #0
.L2010:
	cmp	r1, r3
	bhi	.L2012
.L2013:
	mov	r0, #-1
	b	.L2008
.L2012:
	ldrh	r7, [r4, r3, lsl #3]
	cmp	r7, r6
	bne	.L2011
	add	r3, r5, r3, lsl #3
	ldrh	r4, [r3, #20]
	ldrh	r1, [r3, #18]
	cmp	r4, r2
	it	cs
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, r1, r5
	bl	memcpy
	mov	r0, r4
.L2008:
	pop	{r3, r4, r5, r6, r7, pc}
.L2011:
	adds	r3, r3, #1
	b	.L2010
.L2015:
	.align	2
.L2014:
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
	mov	r9, r1
	ldr	r5, .L2035
	.pad #28
	sub	sp, sp, #28
	mov	r8, r2
	ldr	r4, [r5, #3696]
	str	r0, [sp, #4]
	cmp	r4, #0
	beq	.L2031
	ldrh	r3, [r4, #8]
	add	r7, r8, #63
	ldrh	r2, [r4, #10]
	bic	r7, r7, #63
	str	r3, [sp, #8]
	add	r3, r4, #16
	movs	r6, #0
.L2018:
	cmp	r6, r2
	bcc	.L2026
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L2031
	add	r3, r4, r2, lsl #3
	ldr	r2, [sp, #4]
	uxth	r7, r7
	strh	r2, [r3, #16]	@ movhi
	mov	r2, r8
	ldrh	r0, [r4, #12]
	strh	r0, [r3, #18]	@ movhi
	strh	r8, [r3, #20]	@ movhi
	adds	r3, r0, r7
	add	r0, r0, #1024
	subs	r7, r1, r7
	strh	r3, [r4, #12]	@ movhi
	mov	r1, r9
	add	r0, r0, r4
	strh	r7, [r4, #14]	@ movhi
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r2, r4, #61440
	adds	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	ldr	r3, [r4, #4]
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [r5, #3692]
	b	.L2034
.L2026:
	ldrh	r1, [r3, r6, lsl #3]
	ldr	r0, [sp, #4]
	cmp	r1, r0
	bne	.L2019
	add	r5, r4, r6, lsl #3
	add	r3, r4, #1024
	ldrh	r1, [r5, #20]
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #12]
	cmp	r8, r1
	bls	.L2020
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L2031
	ldrh	fp, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L2021:
	ldr	r2, [sp, #16]
	uxth	r10, fp
	adds	r5, r5, #8
	cmp	r2, r6
	bhi	.L2022
	add	r6, r4, r6, lsl #3
	ldr	r2, [sp, #4]
	uxtah	r0, r3, fp
	mov	r1, r9
	strh	r2, [r6, #16]	@ movhi
	mov	r2, r8
	strh	r10, [r6, #18]	@ movhi
	strh	r8, [r6, #20]	@ movhi
	bl	memcpy
	uxth	r3, r7
	ldrh	r7, [r4, #14]
	add	r10, r10, r3
	strh	r10, [r4, #12]	@ movhi
	subs	r7, r7, r3
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	strh	r7, [r4, #14]	@ movhi
.L2023:
	add	r2, r4, #61440
	ldr	r3, [r4, #4]
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2035
	ldr	r3, [r3, #3692]
.L2034:
	ldr	r0, [sp, #8]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L2016:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2022:
	ldrh	r2, [r5, #16]
	add	r0, r3, fp
	strh	r2, [r5, #8]	@ movhi
	adds	r6, r6, #1
	ldrh	r2, [r5, #20]
	ldrh	r1, [r5, #18]
	strh	r2, [r5, #12]	@ movhi
	adds	r2, r2, #63
	strh	r10, [r5, #10]	@ movhi
	bic	r10, r2, #63
	add	r1, r1, r3
	mov	r2, r10
	str	r3, [sp, #20]
	add	fp, fp, r10
	bl	memcpy
	ldr	r3, [sp, #20]
	b	.L2021
.L2020:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r9
	add	r0, r0, r3
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L2023
.L2019:
	adds	r6, r6, #1
	b	.L2018
.L2031:
	mov	r0, #-1
	b	.L2016
.L2036:
	.align	2
.L2035:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.section	.rodata.str1.1
.LC133:
	.ascii	"copy_from_user error %d %p %p\012\000"
	.text
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
	mov	r4, r2
	bl	ftl_malloc
	mov	r5, r0
	cmp	r0, #0
	beq	.L2048
	ldr	r3, .L2055
	cmp	r6, r3
	beq	.L2039
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L2040
.L2053:
	mvn	r4, #13
.L2041:
	mov	r0, r5
	bl	kfree
.L2037:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L2039:
	mov	r1, r4
	movs	r2, #8
	bl	_copy_from_user
	cbz	r0, .L2042
	mov	r2, r4
	mov	r1, #256
.L2054:
	ldr	r0, .L2055+4
	bl	sftl_printk
	b	.L2053
.L2042:
	ldr	r2, [r5]
	ldr	r3, .L2055+8
	cmp	r2, r3
	beq	.L2043
.L2044:
	mov	r4, #-1
	b	.L2041
.L2043:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L2044
	mov	r3, sp
	strh	r0, [r5, #6]	@ movhi
	bic	r3, r3, #8160
	uxth	r2, r0
	bic	r3, r3, #31
	adds	r2, r2, #8
	ldr	r3, [r3, #8]
	.syntax unified
@ 177 "./include/linux/uaccess.h" 1
	.syntax unified
adds r1, r4, r2; sbcscc r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L2053
	mov	r0, r4
	mov	r1, r5
	bl	arm_copy_to_user
	mov	r4, r0
	cmp	r0, #0
	beq	.L2041
	b	.L2053
.L2040:
	mov	r1, r4
	movs	r2, #8
	bl	_copy_from_user
	cbz	r0, .L2046
	mov	r2, r4
	mov	r1, #276
	b	.L2054
.L2046:
	ldr	r2, [r5]
	ldr	r3, .L2055+8
	cmp	r2, r3
	bne	.L2044
	ldrh	r2, [r5, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2044
	adds	r2, r2, #8
	mov	r1, r4
	mov	r0, r5
	uxth	r2, r2
	bl	_copy_from_user
	cbz	r0, .L2047
	mov	r2, r4
	movw	r1, #283
	b	.L2054
.L2047:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	mov	r4, r0
	b	.L2041
.L2048:
	mov	r4, #-1
	b	.L2037
.L2056:
	.align	2
.L2055:
	.word	1074034177
	.word	.LC133
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
	ldr	r0, .L2058
	b	misc_register
.L2059:
	.align	2
.L2058:
	.word	.LANCHOR2+12
	.fnend
	.size	rk_sftl_vendor_register, .-rk_sftl_vendor_register
	.section	.rodata.str1.1
.LC134:
	.ascii	"vendor_storage\000"
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
	.type	__func__.30, %object
	.size	__func__.30, 17
__func__.30:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.31, %object
	.size	__func__.31, 17
__func__.31:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.29, %object
	.size	__func__.29, 17
__func__.29:
	.ascii	"List_remove_node\000"
	.type	__func__.28, %object
	.size	__func__.28, 22
__func__.28:
	.ascii	"List_update_data_list\000"
	.type	__func__.22, %object
	.size	__func__.22, 22
__func__.22:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.14, %object
	.size	__func__.14, 16
__func__.14:
	.ascii	"make_superblock\000"
	.type	__func__.8, %object
	.size	__func__.8, 19
__func__.8:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.1, %object
	.size	__func__.1, 17
__func__.1:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.26, %object
	.size	__func__.26, 26
__func__.26:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.37, %object
	.size	__func__.37, 11
__func__.37:
	.ascii	"FtlMemInit\000"
	.type	__func__.34, %object
	.size	__func__.34, 14
__func__.34:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.13, %object
	.size	__func__.13, 18
__func__.13:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.3, %object
	.size	__func__.3, 15
__func__.3:
	.ascii	"FlashReadPages\000"
	.type	__func__.19, %object
	.size	__func__.19, 14
__func__.19:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.32, %object
	.size	__func__.32, 11
__func__.32:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.2, %object
	.size	__func__.2, 15
__func__.2:
	.ascii	"FlashProgPages\000"
	.type	__func__.10, %object
	.size	__func__.10, 25
__func__.10:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7, %object
	.size	__func__.7, 16
__func__.7:
	.ascii	"update_vpc_list\000"
	.type	__func__.6, %object
	.size	__func__.6, 20
__func__.6:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.23, %object
	.size	__func__.23, 16
__func__.23:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.27, %object
	.size	__func__.27, 16
__func__.27:
	.ascii	"load_l2p_region\000"
	.type	__func__.25, %object
	.size	__func__.25, 15
__func__.25:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.24, %object
	.size	__func__.24, 31
__func__.24:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.21, %object
	.size	__func__.21, 9
__func__.21:
	.ascii	"log2phys\000"
	.type	__func__.16, %object
	.size	__func__.16, 16
__func__.16:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.15, %object
	.size	__func__.15, 22
__func__.15:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.11, %object
	.size	__func__.11, 14
__func__.11:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.20, %object
	.size	__func__.20, 15
__func__.20:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.12, %object
	.size	__func__.12, 21
__func__.12:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.9, %object
	.size	__func__.9, 29
__func__.9:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.36, %object
	.size	__func__.36, 13
__func__.36:
	.ascii	"FtlProgPages\000"
	.type	__func__.5, %object
	.size	__func__.5, 19
__func__.5:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4, %object
	.size	__func__.4, 23
__func__.4:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.35, %object
	.size	__func__.35, 9
__func__.35:
	.ascii	"FtlWrite\000"
	.type	__func__.18, %object
	.size	__func__.18, 15
__func__.18:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.17, %object
	.size	__func__.17, 18
__func__.17:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.0, %object
	.size	__func__.0, 14
__func__.0:
	.ascii	"FtlWriteToIDB\000"
	.space	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 128
rk_sftl_vendor_storage_fops:
	.space	40
	.word	rk_sftl_vendor_storage_ioctl
	.word	rk_sftl_vendor_storage_ioctl
	.space	80
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
	.ident	"GCC: (GNU Toolchain for the A-profile Architecture 10.3-2021.07 (arm-10.29)) 10.3.1 20210621"
	.section	.note.GNU-stack,"",%progbits
