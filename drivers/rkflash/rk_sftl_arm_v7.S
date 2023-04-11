/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018-2023 Rockchip Electronics Co. Ltd.
 * date: 2023-04-11
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
	.syntax divided
	.syntax unified
	.global	__aeabi_uidiv
	.arm
	.syntax unified
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L3
	mov	r7, r1
	mov	r8, r2
	ldrh	r9, [r3, #8]
	ldrh	r6, [r3, #10]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	lsleq	r4, r6, #1
	lsreq	r9, r9, #1
	mov	r1, r9
	uxtheq	r6, r4
	ldr	r4, [r0, #4]
	lsr	r5, r4, #10
	ubfx	r0, r4, #10, #16
	bl	__aeabi_uidiv
	uxth	r0, r0
	uxth	r5, r5
	ubfx	r4, r4, #0, #10
	mls	r5, r0, r9, r5
	mla	r4, r6, r5, r4
	str	r4, [r7]
	str	r0, [r8]
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
	.type	_copy_from_user, %function
_copy_from_user:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, sp
	bic	ip, r3, #8128
	mov	r6, r0
	bic	ip, ip, #63
	mov	r4, r2
	ldr	r3, [ip, #8]
	.syntax divided
@ 157 "./include/linux/uaccess.h" 1
	.syntax unified
adds r5, r1, r2; sbcscc r5, r5, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L8
	.syntax divided
@ 89 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r7, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r7, #12
	orr	r3, r3, #4
	.syntax divided
@ 99 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 102 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	bl	arm_copy_from_user
	mov	r5, r0
	.syntax divided
@ 99 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r7, c3, c0	@ set domain
@ 0 "" 2
@ 102 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	beq	.L5
.L6:
	sub	r4, r4, r5
	mov	r2, r5
	add	r0, r6, r4
	mov	r1, #0
	bl	memset
.L5:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L8:
	mov	r5, r2
	b	.L6
	.size	_copy_from_user, .-_copy_from_user
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"SFTL version: 5.0.59 20221121\000"
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
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L13
	ldr	r1, .L13+4
	ldr	r0, .L13+8
	bl	sftl_printk
	ldr	r0, .L13+12
	ldrh	r1, [r4, #24]
	ldr	r2, [r4, #72]
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #28]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	ldrh	r2, [r4, #26]
	str	r3, [sp]
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #76]
	ldr	r2, [r4, #72]
	ldr	r0, .L13+16
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #80]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	ldrh	r2, [r4, #78]
	str	r3, [sp]
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #124]
	ldr	r2, [r4, #72]
	ldr	r0, .L13+20
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #128]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	ldrh	r2, [r4, #126]
	str	r3, [sp]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r1, [r4, #172]
	ldr	r2, [r4, #72]
	ldr	r0, .L13+24
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #176]
	str	r3, [sp, #4]
	ldrb	r3, [r4, #180]	@ zero_extendqisi2
	ldrh	r2, [r4, #174]
	str	r3, [sp]
	ldrb	r3, [r4, #178]	@ zero_extendqisi2
	bl	sftl_printk
	ldrh	r3, [r4, #220]
	ldrh	r2, [r4, #222]
	ldrh	r1, [r4, #224]
	ldr	r0, .L13+28
	bl	sftl_printk
	sub	sp, fp, #16
	ldmfd	sp, {r4, fp, sp, pc}
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r1, #0
	mov	r2, #1
.L16:
	uxth	r3, r1
	cmp	r2, r0
	add	r1, r1, #1
	bls	.L17
	sub	r0, r3, #1
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.L17:
	lsl	r2, r2, #1
	b	.L16
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L20
	cmp	r0, #24
	movcc	r0, #24
	ldrh	r2, [r3, #232]
	ldrh	r1, [r3, #242]
	str	r0, [r3, #228]
	mul	r2, r0, r2
	sub	r0, r1, r0
	ldr	r1, [r3, #248]
	str	r2, [r3, #236]
	strh	r0, [r3, #240]	@ movhi
	sub	r2, r1, r2
	mov	r0, #0
	str	r2, [r3, #244]
	ldmfd	sp, {fp, sp, pc}
.L21:
	.align	2
.L20:
	.word	.LANCHOR0
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
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
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L36
	mov	r9, r0
	ldrh	r2, [r0]
	movw	lr, #258
	add	r1, r4, #256
	ldrh	r6, [r0, #2]
	ldrh	ip, [r0, #4]
	ldrh	r3, [r0, #6]
	ldrh	r0, [r0, #14]
	strh	r2, [r4, #252]	@ movhi
	cmp	r0, #4
	strh	r6, [r4, #254]	@ movhi
	strh	ip, [r1]	@ movhi
	strh	r3, [r4, #242]	@ movhi
	strh	r0, [r4, lr]	@ movhi
	bne	.L23
	lsr	r3, r3, #1
	strh	r3, [r4, #242]	@ movhi
	mov	r3, #8
	strh	r3, [r4, lr]	@ movhi
.L24:
	ldr	r0, .L36+4
	mov	r3, #0
.L25:
	strb	r3, [r0], #1
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L25
	movw	r3, #294
	ldr	r5, .L36+8
	ldrh	r7, [r4, #242]
	mov	r0, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, r5
	ldrh	r0, [r1]
	cmp	r2, #1
	mov	r8, r5
	add	r5, r5, #28
	movne	r2, #5
	strh	r2, [r3], #4	@ movhi
	mov	r2, #640
	smulbb	r6, r6, r0
	strh	r2, [r3]	@ movhi
	smulbb	r0, r0, r7
	movw	r3, #298
	uxth	r6, r6
	strh	r6, [r4, #232]	@ movhi
	uxth	r0, r0
	strh	r0, [r4, r3]	@ movhi
	bl	Ftl_log2
	ldrh	r2, [r9, #12]
	movw	r3, #302
	strh	r0, [r5, #-20]	@ movhi
	strh	r2, [r5, #-16]	@ movhi
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #306
	smulbb	r1, r6, r2
	str	r2, [fp, #-52]
	strh	r1, [r4, r3]	@ movhi
	movw	r3, #258
	ldrh	r1, [r4, r3]
	str	r1, [fp, #-48]
	mov	r0, r1
	bl	Ftl_log2
	ldr	r1, [fp, #-48]
	ldr	r2, [fp, #-52]
	mov	r10, r0
	strh	r0, [r8, #16]!	@ movhi
	movw	r0, #310
	lsl	r3, r1, #9
	uxth	r3, r3
	strh	r3, [r4, r0]	@ movhi
	ldrh	r0, [r9, #20]
	mul	r1, r2, r1
	lsr	r3, r3, #8
	strh	r3, [r5, #-8]	@ movhi
	movw	r3, #314
	strh	r0, [r4, r3]	@ movhi
	mul	r3, r7, r6
	str	r3, [r4, #248]
	mov	r0, #5120
	lsl	r7, r7, #6
	mul	r3, r1, r3
	asr	r3, r3, #11
	str	r3, [r4, #316]
	bl	__aeabi_idiv
	cmp	r0, #4
	movw	r2, #322
	strhgt	r0, [r5]	@ movhi
	movle	r3, #4
	strhle	r3, [r5]	@ movhi
	mov	r3, #640
	mov	r1, r6
	asr	r3, r3, r10
	add	r10, r10, #9
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	ldr	r3, .L36+12
	asr	r7, r7, r10
	ldrh	r0, [r5]
	strh	r7, [r3]	@ movhi
	uxth	r7, r7
	mul	r3, r7, r6
	str	r3, [r4, #328]
	bl	__aeabi_uidiv
	cmp	r6, #1
	uxtah	r7, r7, r0
	addne	r7, r7, #8
	addeq	r7, r7, #12
	str	r7, [r4, #228]
	ldrh	r0, [r4, #228]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #228]
	str	r3, [r4, #332]
	movw	r3, #302
	ldr	r2, [r4, #244]
	mov	r0, #0
	ldrh	r3, [r4, r3]
	str	r0, [r4, #340]
	mul	r3, r2, r3
	ldrh	r2, [r8]
	add	r2, r2, #9
	lsl	r3, r3, #2
	lsr	r3, r3, r2
	ldr	r2, .L36+16
	add	r3, r3, #2
	strh	r3, [r2]	@ movhi
	movw	r3, #338
	mov	r2, #32
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #3
	strh	r3, [r5]	@ movhi
	ldr	r3, [r4, #328]
	add	r3, r3, #3
	str	r3, [r4, #328]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L23:
	cmp	r0, #8
	cmpeq	ip, #1
	lsreq	r3, r3, #1
	strheq	r3, [r4, #242]	@ movhi
	moveq	r3, #2
	strheq	r3, [r1]	@ movhi
	b	.L24
.L37:
	.align	2
.L36:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.word	.LANCHOR0+292
	.word	.LANCHOR0+324
	.word	.LANCHOR0+336
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L44
	mov	r1, r0
	add	r2, r3, #344
	ldrh	r0, [r2]
	cmp	r0, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldr	r2, [r3, #348]
	add	r3, r3, #320
	ldrh	r3, [r3]
	add	r3, r2, r3, lsl #1
.L40:
	cmp	r2, r3
	bne	.L41
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L41:
	ldrh	r0, [r2], #2
	cmp	r1, r0
	bne	.L40
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L45:
	.align	2
.L44:
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L48
	ldr	r0, [r3, #340]
	ldmfd	sp, {fp, sp, pc}
.L49:
	.align	2
.L48:
	.word	.LANCHOR0
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.section	.rodata.str1.1
.LC7:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
	.text
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
	sub	sp, sp, #8
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L51
	movw	r3, #298
	mov	r5, r0
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r3, r4, r6, lsl #2
	ubfx	r0, r1, #5, #11
	and	ip, r1, #31
	add	r4, r4, #352
	mov	lr, #1
	ldr	r3, [r3, #380]
	ldr	r2, [r3, r0, lsl #2]
	orr	r2, r2, lr, lsl ip
	str	r2, [r3, r0, lsl #2]
	uxth	r3, r1
	ldr	r0, .L51+4
	mov	r1, r5
	str	r2, [sp]
	mov	r2, r6
	bl	sftl_printk
	ldrh	r3, [r4, #6]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L52:
	.align	2
.L51:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L54
	movw	r3, #298
	mov	r6, r0
	ldrh	r7, [r5, r3]
	mov	r1, r7
	bl	__aeabi_uidivmod
	mov	r0, r6
	uxth	r4, r1
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r4, #5
	and	r4, r4, #31
	add	r5, r5, r0, lsl #2
	ldr	r3, [r5, #380]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L55:
	.align	2
.L54:
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L64
	movw	r2, #298
	mov	r5, #0
	mov	r4, r5
	ldrh	r6, [r3, r2]
	ldrh	r3, [r3, #254]
	mul	r6, r3, r6
.L58:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L60
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L60:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	add	r5, r5, #1
	uxthne	r4, r4
	b	.L58
.L65:
	.align	2
.L64:
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r8, .L67
	mov	r4, r1
	mov	r6, r0
	add	r3, r8, #256
	ldrh	r7, [r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	movw	r3, #298
	smulbb	r4, r7, r4
	mov	r1, r7
	ldrh	r5, [r8, r3]
	smulbb	r5, r5, r0
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r4, r4, r1
	add	r4, r5, r4
	uxth	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L68:
	.align	2
.L67:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L70
	mov	r5, r0
	add	r2, r3, #256
	ldrh	r6, [r2]
	movw	r2, #298
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r6
	smulbb	r4, r0, r6
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r0, r4, r1
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L71:
	.align	2
.L70:
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L73
	movw	r3, #298
	ldrh	r1, [r4, r3]
	add	r4, r4, #256
	bl	__aeabi_uidivmod
	mov	r0, r1
	ldrh	r1, [r4]
	uxth	r0, r0
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L74:
	.align	2
.L73:
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
	push	{lr}
	bl	__gnu_mcount_nc
	cmp	r0, r1
	bls	.L76
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L76:
	sub	r1, r1, r0
	cmp	r1, #-2147483648
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L79
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L80:
	.align	2
.L79:
	.word	.LANCHOR0+412
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L82
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L83:
	.align	2
.L82:
	.word	.LANCHOR0+412
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L93
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	ldmfdeq	sp, {r4, r5, fp, sp, pc}
	ldr	r2, .L93+4
	mov	r0, #0
	ldrh	r1, [r3, #2]
	mov	lr, r0
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #4]
	and	ip, ip, #31
.L87:
	uxth	r4, r0
	cmp	ip, r4
	bhi	.L88
	cmp	lr, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L88:
	add	lr, r3, r1, lsl #1
	add	r1, r1, #1
	add	r0, r0, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, lsl #1
	add	r2, r2, #1
	strh	r4, [lr, #8]	@ movhi
	ubfx	r2, r2, #0, #10
	mov	lr, #1
	b	.L87
.L94:
	.align	2
.L93:
	.word	.LANCHOR0+412
	.word	.LANCHOR0+2468
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r1, .L101
	ldrh	ip, [r1, #6]
	cmp	ip, #1024
	movne	r2, #0
	ldrhne	lr, [r1, #2]
	bne	.L97
.L99:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L98:
	add	r3, lr, r2
	ubfx	r3, r3, #0, #10
	add	r3, r1, r3, lsl #1
	ldrh	r3, [r3, #8]
	cmp	r3, r0
	beq	.L100
	add	r2, r2, #1
.L97:
	cmp	ip, r2
	bhi	.L98
	b	.L99
.L100:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L102:
	.align	2
.L101:
	.word	.LANCHOR0+412
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L117
	ldrh	r3, [r4, #240]
	cmp	r3, r0
	bls	.L105
	mov	lr, #6
	ldr	r5, [r4, #2516]
	mvn	ip, #0
	mul	lr, lr, r0
	add	r1, r5, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r5, lr]	@ movhi
	ldr	r2, [r4, #2520]
	cmp	r2, #0
	streq	r1, [r4, #2520]
	beq	.L105
	ldr	r8, [r4, #72]
	lsl	r3, r0, #1
	ldr	r7, [r4, #2516]
	movw	r10, #65535
	ldrh	r4, [r4, #240]
	ldrh	r6, [r8, r3]
	ldrh	r3, [r1, #4]
	str	r4, [fp, #-48]
	mov	r4, #0
	cmp	r3, #0
	mulne	ip, r3, r6
	sub	r3, r2, r7
	ldr	r6, .L117+4
	asr	r3, r3, #1
	mul	r3, r6, r3
	uxth	r3, r3
.L111:
	sub	r6, r0, r3
	add	r4, r4, #1
	clz	r6, r6
	ldr	r9, [fp, #-48]
	uxth	r4, r4
	lsr	r6, r6, #5
	cmp	r4, r9
	orrhi	r6, r6, #1
	cmp	r6, #0
	bne	.L105
	ldrh	r9, [r2, #4]
	cmp	r9, #0
	beq	.L109
	lsl	r6, r3, #1
	ldrh	r6, [r8, r6]
	mul	r6, r9, r6
	cmp	r6, ip
	bcs	.L109
	ldrh	r6, [r2]
	cmp	r6, r10
	bne	.L110
	strh	r3, [r1, #2]	@ movhi
	ldr	r3, .L117
	strh	r0, [r2]	@ movhi
	str	r1, [r3, #2524]
.L105:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L110:
	mov	r3, #6
	mla	r2, r3, r6, r7
	mov	r3, r6
	b	.L111
.L109:
	strh	r3, [r5, lr]	@ movhi
	ldr	ip, .L117
	ldrh	r3, [r2, #2]
	strh	r3, [r1, #2]	@ movhi
	ldr	lr, [ip, #2520]
	cmp	r2, lr
	strheq	r0, [r2, #2]	@ movhi
	streq	r1, [ip, #2520]
	ldrne	r1, [ip, #2516]
	movne	ip, #6
	mulne	r3, ip, r3
	strhne	r0, [r1, r3]	@ movhi
	strhne	r0, [r2, #2]	@ movhi
	b	.L105
.L118:
	.align	2
.L117:
	.word	.LANCHOR0
	.word	-1431655765
	.size	insert_data_list, .-insert_data_list
	.section	.rodata.str1.1
.LC8:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	insert_data_list
	ldr	r2, .L121
	add	r1, r2, #2528
	ldrh	r2, [r2, #240]
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r1]	@ movhi
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	ldr	r1, .L121+4
	mov	r2, #214
	ldr	r0, .L121+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L122:
	.align	2
.L121:
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	movw	r8, #65535
	cmp	r0, r8
	beq	.L124
	ldr	r1, .L131
	mov	r6, #6
	mvn	r3, #0
	mul	lr, r6, r0
	ldr	r4, [r1, #2516]
	add	ip, r4, lr
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r4, lr]	@ movhi
	ldr	r3, [r1, #2532]
	cmp	r3, #0
	bne	.L125
.L130:
	str	ip, [r1, #2532]
	b	.L124
.L125:
	lsl	r2, r0, #1
	ldr	r7, [r1, #2536]
	ldr	r5, [r1, #2516]
	ldr	r10, .L131+4
	ldrh	r9, [r7, r2]
	sub	r2, r3, r5
	asr	r2, r2, #1
	mul	r2, r10, r2
	uxth	r2, r2
.L128:
	lsl	r10, r2, #1
	ldrh	r10, [r7, r10]
	cmp	r10, r9
	bcs	.L126
	ldrh	r10, [r3]
	cmp	r10, r8
	bne	.L127
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L124:
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L127:
	mla	r3, r6, r10, r5
	mov	r2, r10
	b	.L128
.L126:
	ldrh	r5, [r3, #2]
	strh	r5, [ip, #2]	@ movhi
	strh	r2, [r4, lr]	@ movhi
	ldr	r2, [r1, #2532]
	cmp	r2, r3
	strheq	r0, [r3, #2]	@ movhi
	beq	.L130
.L129:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #2516]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L124
.L132:
	.align	2
.L131:
	.word	.LANCHOR0
	.word	-1431655765
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	insert_free_list
	ldr	r2, .L135
	ldrh	r3, [r2, #224]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #224]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	ldr	r1, .L135+4
	mov	r2, #207
	ldr	r0, .L135+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L136:
	.align	2
.L135:
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r8, .L148
	mov	r4, #6
	movw	r3, #65535
	mov	r5, r0
	mul	r4, r4, r1
	ldr	r7, [r8, #2516]
	add	r6, r7, r4
	ldrh	r2, [r6, #2]
	cmp	r2, r3
	bne	.L138
	ldr	r3, [r0]
	cmp	r6, r3
	bne	.L139
.L142:
	ldrh	r2, [r7, r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L146
	mov	r3, #0
	str	r3, [r5]
.L144:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r4]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L139:
	ldr	r1, .L148+4
	mov	r2, #372
	ldr	r0, .L148+8
	bl	sftl_printk
.L138:
	ldr	r3, [r5]
	ldrh	r1, [r7, r4]
	cmp	r3, r6
	beq	.L142
	movw	r2, #65535
	ldrh	r3, [r6, #2]
	cmp	r1, r2
	bne	.L145
	cmp	r3, r1
	beq	.L144
	mov	r2, #6
	ldr	r1, [r8, #2516]
	mul	r3, r2, r3
	mvn	r2, #0
	strh	r2, [r1, r3]	@ movhi
	b	.L144
.L146:
	ldr	r3, [r8, #2516]
	mov	r1, #6
	mla	r3, r1, r2, r3
	mvn	r2, #0
	str	r3, [r5]
	strh	r2, [r3, #2]	@ movhi
	b	.L144
.L145:
	ldr	r2, [r8, #2516]
	mov	r0, #6
	mla	r2, r0, r1, r2
	strh	r3, [r2, #2]	@ movhi
	mul	r3, r0, r3
	ldr	r2, [r8, #2516]
	ldrh	r1, [r7, r4]
	strh	r1, [r2, r3]	@ movhi
	b	.L144
.L149:
	.align	2
.L148:
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L155
	ldr	r2, .L159
	movw	ip, #65535
	mov	lr, #6
	ldr	r4, [r2, #2516]
.L152:
	cmp	r1, #0
	beq	.L153
	ldrh	r2, [r3]
	cmp	r2, ip
	bne	.L154
.L153:
	sub	r4, r3, r4
	ldr	r3, .L159+4
	asr	r4, r4, #1
	mul	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L154:
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L152
.L155:
	movw	r0, #65535
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L160:
	.align	2
.L159:
	.word	.LANCHOR0
	.word	-1431655765
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r2, .L168
	ldr	r3, [r2, #2520]
	cmp	r3, #0
	movwne	r1, #65535
	movne	lr, #6
	ldrne	ip, [r2, #2516]
	bne	.L164
.L167:
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L166:
	sub	r2, r0, #1
	mla	r3, lr, r3, ip
	uxth	r0, r2
.L164:
	cmp	r0, #0
	beq	.L165
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L166
	b	.L167
.L165:
	sub	r0, r3, ip
	ldr	r3, .L168+4
	asr	r0, r0, #1
	mul	r0, r3, r0
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.L169:
	.align	2
.L168:
	.word	.LANCHOR0
	.word	-1431655765
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L181
	mov	r4, r0
	ldrh	r3, [r5, #24]
	cmp	r3, r0
	beq	.L172
	ldrh	r3, [r5, #76]
	cmp	r3, r0
	beq	.L172
	ldrh	r3, [r5, #124]
	cmp	r3, r0
	beq	.L172
	mov	r7, #6
	ldr	r9, [r5, #2516]
	ldr	r3, [r5, #2520]
	mul	r7, r7, r0
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L172
	ldr	r2, [r5, #72]
	lsl	r3, r0, #1
	ldrh	r6, [r2, r3]
	movw	r2, #65535
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	mvneq	r6, #0
	mulne	r6, r3, r6
	ldrh	r3, [r8, #2]
	cmp	r3, r2
	bne	.L175
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L175
	ldr	r1, .L181+4
	movw	r2, #463
	ldr	r0, .L181+8
	bl	sftl_printk
.L175:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L176
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L172
.L176:
	mov	r2, #6
	ldr	r1, .L181+12
	mul	r3, r2, r3
	asr	r2, r3, #1
	mul	r2, r1, r2
	ldr	r1, [r5, #72]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	ldr	r1, [r5, #2516]
	add	r3, r1, r3
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	mvneq	r3, #0
	mulne	r3, r3, r2
	cmp	r6, r3
	bcs	.L172
	ldr	r5, .L181+16
	mov	r1, r4
	ldr	r0, .L181+20
	bl	List_remove_node
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L178
	ldr	r1, .L181+4
	movw	r2, #474
	ldr	r0, .L181+8
	bl	sftl_printk
.L178:
	ldrh	r3, [r5]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5]	@ movhi
	bl	INSERT_DATA_LIST
.L172:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L182:
	.align	2
.L181:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	-1431655765
	.word	.LANCHOR0+2528
	.word	.LANCHOR0+2520
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L193
	movw	r2, #338
	mov	r1, #0
	mov	ip, #12
	movw	lr, #65535
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #2540]
	sub	r0, r3, #12
.L184:
	uxth	r4, r1
	cmp	r2, r4
	bhi	.L186
	mov	r4, r2
	add	r5, r3, #4
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r6, #12
.L187:
	uxth	lr, r1
	cmp	r2, lr
	bhi	.L189
	cmp	r2, r4
	bhi	.L185
	ldr	r1, .L193+4
	mov	r4, r2
	mvn	r0, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L190:
	uxth	ip, r1
	cmp	r2, ip
	bhi	.L192
	cmp	r2, r4
	bhi	.L185
	ldr	r1, .L193+8
	mov	r2, #796
	ldr	r0, .L193+12
	bl	sftl_printk
	b	.L185
.L186:
	add	r1, r1, #1
	mul	r5, ip, r1
	ldrh	r5, [r0, r5]
	cmp	r5, lr
	bne	.L184
.L185:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L189:
	mul	ip, r6, r1
	add	r1, r1, #1
	ldr	ip, [r5, ip]
	mvn	r7, ip
	cmp	ip, r0
	lsr	r7, r7, #31
	movcs	r7, #0
	cmp	r7, #0
	movne	r0, ip
	movne	r4, lr
	b	.L187
.L192:
	ldr	lr, [r3, #4]
	cmp	lr, r0
	bcs	.L191
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L191:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L190
.L194:
	.align	2
.L193:
	.word	.LANCHOR0
	.word	.LANCHOR0+2544
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L201
	movw	r1, #2546
	ldrh	r2, [r3, r1]
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L196
	add	r2, r2, #1
	strh	r2, [r3, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L196:
	mov	r2, #0
	strh	r2, [r3, r1]	@ movhi
	str	r2, [r3, #2548]
	movw	lr, #65535
	ldr	r2, [r3, #72]
	ldrh	r1, [r3, #240]
	add	r1, r2, r1, lsl #1
.L197:
	cmp	r2, r1
	ldmfdeq	sp, {fp, sp, pc}
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2548]
	addne	r0, r0, ip
	strne	r0, [r3, #2548]
	b	.L197
.L202:
	.align	2
.L201:
	.word	.LANCHOR0
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L211
	add	ip, r2, #8
	mov	r3, #0
	movw	r5, #65535
	add	ip, r0, ip, lsl #1
	strh	r3, [r0, #4]	@ movhi
	ldrh	lr, [r4, #232]
.L204:
	cmp	lr, r2
	bhi	.L206
	movw	r3, #302
	add	ip, r0, #16
	mov	r2, #0
	ldrh	r3, [r4, r3]
	movw	r4, #65535
	sub	r3, r3, r1
	sub	r3, r3, #1
	sxth	r3, r3
.L207:
	uxth	r1, r2
	cmp	lr, r1
	ldmfdls	sp, {r4, r5, fp, sp, pc}
	ldrh	r1, [ip], #2
	add	r2, r2, #1
	cmp	r1, r4
	ldrhne	r1, [r0, #4]
	addne	r1, r3, r1
	strhne	r1, [r0, #4]	@ movhi
	b	.L207
.L206:
	ldrh	r3, [ip], #2
	add	r2, r2, #1
	cmp	r3, r5
	uxth	r2, r2
	ldrhne	r3, [r0, #4]
	addne	r3, r3, #1
	strhne	r3, [r0, #4]	@ movhi
	b	.L204
.L212:
	.align	2
.L211:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {fp, sp, pc}
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	mov	ip, #0
	ldr	r1, .L219
	add	r3, r0, r3, lsl #1
	ldrh	r1, [r1, #232]
	ldrh	r3, [r3, #16]
.L216:
	cmp	r3, r2
	ldmfdne	sp, {fp, sp, pc}
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r0, #6]
	cmp	r3, r1
	strbeq	ip, [r0, #6]
	ldrheq	r3, [r0, #2]
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r0, r3, lsl #1
	ldrh	r3, [r3, #16]
	b	.L216
.L220:
	.align	2
.L219:
	.word	.LANCHOR0
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r7, .L227
	mov	r4, r0
	ldrh	r2, [r0]
	ldrh	r3, [r7, #240]
	cmp	r2, r3
	bcc	.L222
	ldr	r1, .L227+4
	movw	r2, #2157
	ldr	r0, .L227+8
	bl	sftl_printk
.L222:
	ldrh	r6, [r7, #232]
	mov	r3, #0
	strh	r3, [r4, #4]	@ movhi
	add	r5, r4, #16
	strb	r3, [r4, #7]
	mvn	r9, #0
	add	r6, r4, r6, lsl #1
	ldr	r3, .L227+12
	add	r6, r6, #16
.L223:
	ldrh	r1, [r4]
	cmp	r6, r5
	ldrb	r8, [r4, #7]	@ zero_extendqisi2
	bne	.L225
	movw	r3, #302
	lsl	r1, r1, #1
	mov	r0, #0
	ldrh	r3, [r7, r3]
	smulbb	r8, r3, r8
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r7, #2536]
	strh	r8, [r4, #4]	@ movhi
	ldrh	r2, [r3, r1]
	movw	r3, #10000
	cmp	r2, r3
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L225:
	ldrb	r0, [r3], #1	@ zero_extendqisi2
	add	r5, r5, #2
	str	r3, [fp, #-48]
	bl	V2P_block
	strh	r9, [r5, #-2]	@ movhi
	mov	r10, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r8, r8, #1
	strheq	r10, [r5, #-2]	@ movhi
	ldr	r3, [fp, #-48]
	strbeq	r8, [r4, #7]
	b	.L223
.L228:
	.align	2
.L227:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+260
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L237
	movw	r3, #302
	mov	r7, #0
	mov	r5, r0
	add	r8, r6, #260
	mov	r4, r7
	ldrh	r9, [r6, #232]
	ldrh	r10, [r6, r3]
.L230:
	uxth	r3, r7
	cmp	r9, r3
	bhi	.L232
	cmp	r4, #0
	beq	.L233
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L233:
	ldr	r0, [r6, #2516]
	mov	r3, #6
	mla	r5, r3, r5, r0
	mov	r0, #0
	strh	r4, [r5, #4]	@ movhi
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L232:
	mov	r1, r5
	ldrb	r0, [r8], #1	@ zero_extendqisi2
	bl	V2P_block
	add	r7, r7, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r10, r4
	uxtheq	r4, r4
	b	.L230
.L238:
	.align	2
.L237:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r2, .L242
	ldr	r0, [r2, #2532]
	cmp	r0, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldr	r3, [r2, #2516]
	ldr	r2, [r2, #2536]
	sub	r3, r0, r3
	ldr	r0, .L242+4
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r3, r3
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {fp, sp, pc}
.L243:
	.align	2
.L242:
	.word	.LANCHOR0
	.word	-1431655765
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r1, .L251
	mov	r2, r0
	ldr	r0, [r1, #2532]
	cmp	r0, #0
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	ldrh	r3, [r1, #224]
	mov	r5, #6
	ldr	lr, [r1, #2516]
	movw	r6, #65535
	rsb	r3, r3, r3, lsl #3
	asr	ip, r3, #3
	cmp	r2, r3, asr #3
	sub	r3, r0, lr
	ldr	r0, .L251+4
	uxthgt	r2, ip
	mov	ip, #0
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r3, r3
.L247:
	uxth	r4, ip
	mov	r0, r3
	cmp	r4, r2
	bcs	.L248
	mul	r3, r5, r3
	add	ip, ip, #1
	ldrh	r3, [lr, r3]
	cmp	r3, r6
	bne	.L247
.L248:
	ldr	r2, [r1, #2536]
	lsl	r3, r0, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L252:
	.align	2
.L251:
	.word	.LANCHOR0
	.word	-1431655765
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
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L265
	mov	r7, r0
	ldr	r1, .L265+4
	add	r5, r7, #12
	bl	strcpy
	ldr	r2, .L265+8
	mov	r0, r5
	mov	r1, #64
	ldr	r3, [r6, #316]
	add	r8, r6, #352
	bl	snprintf
	add	r5, r5, r0
	ldr	r1, .L265+12
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r3, [r6, #2552]
	mov	r0, r5
	ldr	r2, .L265+16
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2548]
	mov	r0, r5
	ldr	r2, .L265+20
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2556]
	mov	r0, r5
	ldr	r2, .L265+24
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2560]
	mov	r0, r5
	ldr	r2, .L265+28
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2564]
	mov	r0, r5
	ldr	r2, .L265+32
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2568]
	mov	r0, r5
	ldr	r2, .L265+36
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2572]
	mov	r0, r5
	ldr	r2, .L265+40
	mov	r1, #64
	bl	snprintf
	add	r5, r5, r0
	ldr	r3, [r6, #2576]
	mov	r0, r5
	ldr	r2, .L265+44
	mov	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2580]
	add	r5, r5, r0
	ldr	r2, .L265+48
	mov	r0, r5
	mov	r1, #64
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r6, #2584]
	add	r5, r5, r0
	ldr	r2, .L265+52
	mov	r0, r5
	mov	r1, #64
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r5, r0
	ldr	r3, [r6, #2588]
	mov	r0, r4
	ldr	r2, .L265+56
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2592]
	mov	r0, r4
	ldr	r2, .L265+60
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #6]
	mov	r0, r4
	ldr	r2, .L265+64
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #224]
	mov	r0, r4
	ldr	r2, .L265+68
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2596]
	mov	r0, r4
	ldr	r2, .L265+72
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2600]
	mov	r0, r4
	ldr	r2, .L265+76
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2604]
	mov	r0, r4
	ldr	r2, .L265+80
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2608]
	mov	r0, r4
	ldr	r2, .L265+84
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2612]
	mov	r0, r4
	ldr	r2, .L265+88
	mov	r1, #64
	ldr	r5, .L265+92
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2616]
	ldr	r2, .L265+96
	mov	r0, r4
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	mov	r0, r4
	ldr	r2, .L265+100
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	mov	r0, r4
	ldr	r2, .L265+104
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #340]
	mov	r0, r4
	ldr	r2, .L265+108
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #332]
	mov	r0, r4
	ldr	r2, .L265+112
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #228]
	mov	r0, r4
	ldr	r2, .L265+116
	mov	r1, #64
	bl	snprintf
	add	r3, r6, #412
	add	r4, r4, r0
	ldr	r2, .L265+120
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r3, #6]
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #240]
	mov	r0, r4
	ldr	r2, .L265+124
	mov	r1, #64
	bl	snprintf
	movw	r3, #2620
	add	r4, r4, r0
	ldr	r2, .L265+128
	ldrh	r3, [r6, r3]
	mov	r0, r4
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #244]
	mov	r0, r4
	ldr	r2, .L265+132
	mov	r1, #64
	bl	snprintf
	add	r3, r6, #2624
	add	r4, r4, r0
	ldr	r2, .L265+136
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r3]
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8]
	mov	r0, r4
	ldr	r2, .L265+140
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #26]
	mov	r0, r4
	ldr	r2, .L265+144
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #30]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+148
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #24]
	mov	r0, r4
	ldr	r2, .L265+152
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+156
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #28]
	mov	r0, r4
	ldr	r2, .L265+160
	mov	r1, #64
	bl	snprintf
	ldrh	r3, [r6, #24]
	ldr	r2, [r6, #72]
	add	r4, r4, r0
	mov	r0, r4
	mov	r1, #64
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L265+164
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #78]
	mov	r0, r4
	ldr	r2, .L265+168
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #82]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+172
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #76]
	mov	r0, r4
	ldr	r2, .L265+176
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #84]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+180
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #80]
	mov	r0, r4
	ldr	r2, .L265+184
	mov	r1, #64
	bl	snprintf
	ldrh	r3, [r6, #76]
	ldr	r2, [r6, #72]
	add	r4, r4, r0
	mov	r0, r4
	mov	r1, #64
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L265+188
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #126]
	mov	r0, r4
	ldr	r2, .L265+192
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #130]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+196
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #124]
	mov	r0, r4
	ldr	r2, .L265+200
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #132]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+204
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #128]
	mov	r0, r4
	ldr	r2, .L265+208
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #174]
	mov	r0, r4
	ldr	r2, .L265+212
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #178]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+216
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #172]
	mov	r0, r4
	ldr	r2, .L265+220
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r6, #180]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, .L265+224
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r6, #176]
	mov	r0, r4
	ldr	r2, .L265+228
	mov	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2716]
	add	r4, r4, r0
	str	r3, [sp, #4]
	ldr	r3, [r6, #2724]
	mov	r0, r4
	ldr	r2, .L265+232
	mov	r1, #64
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2712]
	mov	r0, r4
	ldr	r2, .L265+236
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #2736]
	mov	r0, r4
	ldr	r2, .L265+240
	mov	r1, #64
	bl	snprintf
	add	r3, r6, #3152
	add	r4, r4, r0
	ldr	r2, .L265+244
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r3]
	bl	snprintf
	movw	r3, #3154
	add	r4, r4, r0
	ldr	r2, .L265+248
	ldrh	r3, [r6, r3]
	mov	r0, r4
	mov	r1, #64
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r6, #3156]
	mov	r0, r4
	ldr	r2, .L265+252
	mov	r1, #64
	bl	snprintf
	movw	r3, #3160
	add	r4, r4, r0
	ldr	r2, .L265+256
	ldrh	r3, [r6, r3]
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L265+260
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r6, #224]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L265+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L265+268
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L254
.L259:
	sub	r0, r4, r7
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L254:
	ldrh	r3, [r6, #172]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L256
	ldr	r2, [r6, #72]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L265+272
	bl	snprintf
	add	r4, r4, r0
.L256:
	mov	r0, #0
	ldr	r10, .L265+276
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r5, #0
	movw	r9, #65535
.L258:
	ldr	r3, [r6, #2516]
	cmp	r0, r9
	beq	.L257
	mov	r8, #6
	lsl	r2, r0, #1
	ldr	r1, [r6, #2536]
	mul	r8, r8, r0
	ldrh	r1, [r1, r2]
	add	r3, r3, r8
	str	r1, [sp, #12]
	mov	r1, #64
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r6, #72]
	ldrh	r3, [r3, r2]
	mov	r2, r10
	stm	sp, {r0, r3}
	mov	r3, r5
	mov	r0, r4
	add	r5, r5, #1
	bl	snprintf
	ldr	r3, [r6, #2516]
	add	r4, r4, r0
	cmp	r5, #16
	ldrh	r0, [r3, r8]
	bne	.L258
.L257:
	ldr	r3, [r6, #2516]
	mov	r8, #0
	ldr	r1, [r6, #2532]
	movw	r9, #65535
	ldr	r10, .L265+280
	sub	r1, r1, r3
	ldr	r3, .L265+284
	asr	r1, r1, #1
	mul	r1, r3, r1
	uxth	r1, r1
.L260:
	cmp	r1, r9
	beq	.L259
	ldr	r2, [r6, #2536]
	lsl	r3, r1, #1
	mov	r5, #6
	mov	r0, r4
	ldrh	r3, [r2, r3]
	mul	r5, r5, r1
	str	r3, [sp, #8]
	mov	r2, r10
	ldr	r3, [r6, #2516]
	add	r3, r3, r5
	ldrh	r3, [r3, #4]
	stm	sp, {r1, r3}
	mov	r3, r8
	mov	r1, #64
	add	r8, r8, #1
	bl	snprintf
	cmp	r8, #4
	add	r4, r4, r0
	beq	.L259
	ldr	r3, [r6, #2516]
	ldrh	r1, [r3, r5]
	b	.L260
.L266:
	.align	2
.L265:
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
	.word	.LANCHOR0+2468
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
	.word	-1431655765
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.section	.rodata.str1.1
.LC77:
	.ascii	"%s\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r5, r1
	cmp	r0, #2048
	blt	.L269
	ldr	r3, .L270
	add	r4, r5, #30
	ldr	r2, .L270+4
	mov	r1, #64
	mov	r0, r5
	bl	snprintf
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L269:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L271:
	.align	2
.L270:
	.word	.LC0
	.word	.LC77
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.section	.rodata.str1.1
.LC78:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
	.text
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L296
	ldr	r2, [r4, #2604]
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	bcs	.L273
	ldr	r2, [r4, #2536]
	mov	r3, #0
	ldrh	r1, [r4, #240]
	sub	r2, r2, #2
	str	r3, [r4, #2596]
.L274:
	ldr	r5, [r4, #2596]
	cmp	r1, r3
	bhi	.L275
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, .L296+4
	str	r0, [r4, #2604]
	ldr	r0, [r4, #2600]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2596]
.L276:
	ldr	r6, [r4, #2616]
	ldr	r9, [r4, #2604]
	add	r3, r6, #256
	cmp	r3, r9
	bls	.L281
	ldr	r2, [r4, #2612]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L281
.L283:
	movw	r5, #65535
.L282:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L275:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	add	r5, r0, r5
	str	r5, [r4, #2596]
	b	.L274
.L273:
	ldr	r3, [r4, #2612]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2612]
	movhi	r3, #0
	bls	.L276
.L278:
	ldrh	r2, [r4, #240]
	cmp	r2, r3
	bls	.L276
	lsl	r1, r3, #1
	ldr	r0, [r4, #2536]
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L278
.L281:
	ldrh	r0, [r4, #224]
	add	r0, r0, r0, lsl #1
	lsr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r7, r0
	cmp	r0, r3
	bcc	.L283
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L283
	ldrh	r2, [r4, #240]
	movw	r8, #65535
	ldr	ip, [r4, #2516]
	mov	r1, #0
	ldr	r10, [r4, #2536]
	mov	lr, #6
	str	r2, [fp, #-48]
	mov	r2, r8
.L284:
	ldrh	r0, [r3]
	movw	r5, #65535
	cmp	r0, r5
	bne	.L287
	mov	r5, r2
.L286:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L283
	lsl	r1, r5, #1
	ldrh	r2, [r10, r1]
	cmp	r6, r2
	bcs	.L288
	str	r2, [fp, #-52]
	str	r1, [fp, #-48]
	bl	GetFreeBlockMinEraseCount
	ldr	r1, [fp, #-48]
	cmp	r6, r0
	ldr	r2, [fp, #-52]
	strcc	r8, [r4, #2616]
.L288:
	cmp	r9, r2
	bls	.L283
	add	r3, r2, #128
	cmp	r7, r3
	ble	.L283
	add	r0, r2, #256
	ldr	r3, [r4, #2612]
	cmp	r9, r0
	bhi	.L289
	add	r2, r2, #768
	cmp	r2, r3
	bcs	.L283
.L289:
	str	r7, [sp, #8]
	ldr	r0, .L296+8
	ldrh	r2, [r10, r1]
	str	r2, [sp, #4]
	ldr	r2, [r4, #72]
	ldrh	r2, [r2, r1]
	mov	r1, r5
	str	r2, [sp]
	mov	r2, r9
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3164]
	b	.L282
.L287:
	add	r1, r1, #1
	ldr	r5, [fp, #-48]
	uxth	r1, r1
	cmp	r5, r1
	bcc	.L283
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L285
	sub	r3, r3, ip
	ldr	r5, .L296+12
	asr	r3, r3, #1
	mul	r3, r5, r3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r10, r3]
	cmp	r6, r3
	bcs	.L286
	cmp	r3, r8
	movcc	r8, r3
	movcc	r2, r5
.L285:
	mla	r3, lr, r0, ip
	b	.L284
.L297:
	.align	2
.L296:
	.word	.LANCHOR0
	.word	.LANCHOR0+292
	.word	.LC78
	.word	-1431655765
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
	push	{lr}
	bl	__gnu_mcount_nc
	movw	r2, #65535
	cmp	r0, r2
	beq	.L299
	ldr	r2, .L300
	lsl	r3, r0, #1
	mov	r1, #0
	ldr	r2, [r2, #72]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L299:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L301:
	.align	2
.L300:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldrh	r2, [r0]
	movw	r3, #65535
	mov	r4, r0
	cmp	r2, r3
	bne	.L303
	ldr	r1, .L317
	movw	r2, #2792
	ldr	r0, .L317+4
	bl	sftl_printk
.L303:
	ldr	r6, .L317+8
	movw	r3, #302
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bne	.L304
	ldr	r1, .L317
	movw	r2, #2793
	ldr	r0, .L317+4
	bl	sftl_printk
.L304:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L305
	ldr	r1, .L317
	movw	r2, #2794
	ldr	r0, .L317+4
	bl	sftl_printk
.L305:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	mov	r1, #0
	strb	r1, [r4, #10]
	movw	ip, #65535
	ldrh	r0, [r6, #232]
	add	r3, r4, r3, lsl #1
	ldrh	r5, [r3, #16]
.L306:
	ldrh	r2, [r4, #2]
	cmp	r5, ip
	beq	.L308
	movw	r3, #302
	ldrh	ip, [r6, r3]
	cmp	ip, r2
	movwls	r5, #65535
	bls	.L302
	ldrh	r3, [r4, #4]
	orr	r5, r2, r5, lsl #10
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movw	lr, #65535
	sub	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #4]	@ movhi
.L311:
	add	r1, r2, #1
	uxtb	r2, r1
	cmp	r0, r2
	mov	r1, r2
	moveq	r2, #0
	ldrheq	r1, [r4, #2]
	addeq	r1, r1, #1
	strheq	r1, [r4, #2]	@ movhi
	add	r1, r4, r2, lsl #1
	ldrh	r1, [r1, #16]
	cmp	r1, lr
	beq	.L311
	strb	r2, [r4, #6]
	cmp	r3, #0
	ldrh	r2, [r4, #2]
	sub	r2, r2, ip
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L302
	ldr	r1, .L317
	movw	r2, #2817
	ldr	r0, .L317+4
	bl	sftl_printk
.L302:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L308:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r3, r0
	strbeq	r1, [r4, #6]
	addeq	r2, r2, #1
	strheq	r2, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r5, [r3, #16]
	b	.L306
.L318:
	.align	2
.L317:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L325
	mov	r1, #0
	ldr	r6, .L325+4
	mov	r4, #12
	mov	r5, #1
	mov	r7, #20
	str	r1, [r3, #3168]
.L320:
	ldrh	r2, [r3, #232]
	uxth	r0, r1
	cmp	r2, r0
	bhi	.L321
	ldr	r4, .L325+4
	mov	ip, #12
	mov	lr, #0
.L322:
	ldr	r1, [r3, #3188]
	cmp	r2, r1
	ldmfdcs	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	ldrh	r0, [r4]
	mul	r5, ip, r2
	ldr	r6, [r3, #3172]
	add	r1, r6, r5
	mul	r0, r2, r0
	cmp	r0, #0
	str	lr, [r1, #8]
	add	r1, r0, #3
	movge	r1, r0
	ldr	r0, [r3, #3176]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [r6, r5]
	ldr	r1, [r3, #3172]
	add	r5, r1, r5
	ldr	r1, .L325+8
	ldrh	r0, [r1]
	mul	r0, r2, r0
	add	r2, r2, #1
	cmp	r0, #0
	add	r1, r0, #3
	movge	r1, r0
	ldr	r0, [r3, #3180]
	bic	r1, r1, #3
	uxth	r2, r2
	add	r1, r0, r1
	str	r1, [r5, #4]
	b	.L322
.L321:
	uxth	ip, r1
	ldrh	lr, [r6]
	ldr	r8, [r3, #3172]
	add	r1, r1, #1
	mul	r0, r4, ip
	mul	lr, ip, lr
	add	r2, r8, r0
	cmp	lr, #0
	str	r5, [r2, #8]
	add	r2, lr, #3
	movge	r2, lr
	ldr	lr, [r3, #3176]
	bic	r2, r2, #3
	add	r2, lr, r2
	str	r2, [r8, r0]
	ldr	r2, .L325+8
	ldr	r8, [r3, #3172]
	add	lr, r8, r0
	ldrh	r9, [r2]
	mul	r9, ip, r9
	cmp	r9, #0
	add	r2, r9, #3
	movge	r2, r9
	ldr	r9, [r3, #3180]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [lr, #4]
	ldr	r2, [r3, #3184]
	mla	ip, r7, ip, r2
	ldr	r2, [r8, r0]
	str	r2, [ip, #8]
	ldr	r2, [lr, #4]
	str	r2, [ip, #12]
	b	.L320
.L326:
	.align	2
.L325:
	.word	.LANCHOR0
	.word	.LANCHOR0+310
	.word	.LANCHOR0+312
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L334
	mov	lr, #0
	mov	r8, lr
	mov	r5, #20
	mov	r7, #12
	ldr	r6, [r3, #3188]
	ldr	r4, [r3, #3172]
.L328:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L329:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L330
	mul	r3, r7, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L344
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r5, [r3, #3188]
	ldr	r4, [r3, #3172]
.L337:
	uxth	r2, ip
	cmp	r1, r2
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	mov	lr, #0
.L338:
	uxth	r3, lr
	cmp	r5, r3
	bls	.L339
	mla	r3, r6, r3, r4
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
.L345:
	.align	2
.L344:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r2, .L351
	movw	r1, #3196
	ldr	r3, [r2, #3192]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L347:
	cmp	r3, r2
	bne	.L349
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L349:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L347
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L352:
	.align	2
.L351:
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	r3, .L357
	movw	r2, #3196
	mov	ip, #0
	ldrh	r7, [r3, r2]
	ldr	r2, [r3, #3192]
	sub	r1, r2, #2
.L354:
	uxth	lr, ip
	cmp	r7, lr
	bhi	.L356
	bne	.L355
	lsl	ip, ip, #1
	movw	r1, #3196
	strh	r0, [r2, ip]	@ movhi
	ldrh	r2, [r3, r1]
	add	r2, r2, #1
	strh	r2, [r3, r1]	@ movhi
	b	.L355
.L356:
	ldrh	lr, [r1, #2]!
	add	ip, ip, #1
	cmp	lr, r0
	bne	.L354
.L355:
	movw	r2, #3204
	mov	r1, #12
	ldrh	ip, [r3, r2]
	mul	ip, r1, ip
	ldr	r1, [r3, #3200]
	add	r0, r1, ip
	stmib	r0, {r5, r6}
	str	r4, [r1, ip]
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L358:
	.align	2
.L357:
	.word	.LANCHOR0
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.section	.rodata.str1.1
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
	.text
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r4, r0
	mov	r1, r0
	ldr	r0, .L362
	bl	sftl_printk
	ldr	r3, .L362+4
	ldrh	r0, [r3, #222]
	cmp	r0, r4
	beq	.L360
	ldrh	r1, [r3, #220]
	cmp	r1, r4
	beq	.L360
	movw	r2, #65535
	cmp	r0, r2
	strheq	r4, [r3, #222]	@ movhi
	beq	.L360
	cmp	r1, r2
	strheq	r4, [r3, #220]	@ movhi
.L360:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L363:
	.align	2
.L362:
	.word	.LC79
	.word	.LANCHOR0
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.section	.rodata.str1.1
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L368
	movw	r7, #3206
	mov	r4, r0
	bl	P2V_block_in_plane
	mov	r2, r4
	mov	r6, r0
	ldr	r0, .L368+4
	ldrh	r1, [r5, r7]
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r1, .L368+8
	ldrh	r3, [r5, r7]
	mov	r2, #0
	mov	r0, r1
.L365:
	uxth	ip, r2
	cmp	r3, ip
	bhi	.L367
	cmp	r3, #15
	bhi	.L366
	add	r1, r3, #1
	lsl	r3, r3, #1
	movw	r2, #3206
	strh	r4, [r0, r3]	@ movhi
	strh	r1, [r5, r2]	@ movhi
	b	.L366
.L367:
	ldrh	ip, [r1], #2
	add	r2, r2, #1
	cmp	ip, r4
	bne	.L365
.L366:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L369:
	.align	2
.L368:
	.word	.LANCHOR0
	.word	.LC80
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L376
	movw	r3, #3206
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L371
	ldrh	r1, [r4, #222]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L371
	movw	r3, #3242
	movw	r5, #3242
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldr	r2, .L376+4
	ldrh	r3, [r4, r5]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L371:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L377:
	.align	2
.L376:
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
	push	{lr}
	bl	__gnu_mcount_nc
	movw	r1, #3265
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	kfree
	ldmfd	sp, {fp, sp, pc}
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
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r10, .L389
	mov	r7, #0
	mov	r9, r0
	mov	r5, r1
	mov	r8, r2
	mov	r6, r7
	mov	r4, r7
	str	r3, [fp, #-48]
.L381:
	ldr	r3, [fp, #-48]
	cmp	r3, r4
	bhi	.L387
	ldr	r0, .L389+4
	bl	sftl_printk
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L387:
	cmp	r6, #0
	bne	.L382
	ldr	r0, .L389+8
	mov	r3, r7
	mov	r2, r5
	mov	r1, r9
	bl	sftl_printk
.L382:
	cmp	r8, #4
	ldreq	r1, [r5, r4, lsl #2]
	ldreq	r0, .L389+12
	beq	.L388
	cmp	r8, #2
	lsleq	r3, r4, #1
	movne	r0, r10
	ldreq	r0, .L389+16
	ldrheq	r1, [r5, r3]
	ldrbne	r1, [r5, r4]	@ zero_extendqisi2
.L388:
	add	r6, r6, #1
	bl	sftl_printk
	cmp	r6, #15
	bls	.L386
	mov	r6, #0
	ldr	r0, .L389+4
	bl	sftl_printk
.L386:
	add	r4, r4, #1
	add	r7, r7, r8
	b	.L381
.L390:
	.align	2
.L389:
	.word	.LC84
	.word	.LC85
	.word	.LC81
	.word	.LC82
	.word	.LC83
	.size	rknand_print_hex, .-rknand_print_hex
	.section	.rodata.str1.1
.LC86:
	.ascii	"%s: addr: %x is in id block!!!!!!!!!!\012\000"
.LC87:
	.ascii	"not free: w: d:\000"
.LC88:
	.ascii	"not free: w: s:\000"
	.text
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #36
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r10, .L407
	bic	r9, r3, #8128
	mov	r5, r0
	bic	r9, r9, #63
	mov	r7, r2
	mov	r4, #0
	ldr	r3, [r9, #24]
	str	r3, [fp, #-48]
	ldrh	r3, [r10, #12]
	str	r3, [fp, #-64]
	lsl	r3, r3, #3
	str	r3, [fp, #-68]
	add	r3, r2, r2, lsl #2
	str	r3, [fp, #-72]
.L392:
	ldr	r3, [fp, #-72]
	cmp	r4, r3
	beq	.L395
	sub	r2, fp, #56
	sub	r1, fp, #52
	add	r0, r5, r4, lsl #2
	mov	r6, r5
	bl	l2p_addr_tran
	ldr	r8, [fp, #-56]
	cmp	r8, #0
	bne	.L393
	ldr	r2, [fp, #-52]
	ldr	r3, [fp, #-68]
	cmp	r3, r2
	bls	.L393
	ldr	r5, .L407+4
	ldr	r4, .L407+8
.L394:
	mvn	r3, #0
	ldr	r2, [fp, #-52]
	str	r3, [r6]
	mov	r1, r5
	mov	r0, r4
	add	r8, r8, #1
	bl	sftl_printk
	ldr	r1, [r6, #8]
	ldr	r0, .L407+12
	mov	r3, #16
	mov	r2, #4
	add	r6, r6, #20
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r6, #-8]
	mov	r2, r3
	ldr	r0, .L407+16
	bl	rknand_print_hex
	cmp	r7, r8
	bne	.L394
	bl	dump_stack
.L395:
	ldr	r2, [fp, #-48]
	ldr	r3, [r9, #24]
	cmp	r2, r3
	beq	.L402
	bl	__stack_chk_fail
.L393:
	ldr	r2, [r10, #3248]
	uxtb	r0, r8
	ldr	r1, [fp, #-52]
	blx	r2
	cmp	r0, #0
	streq	r0, [r5, r4, lsl #2]
	mvnne	r2, #0
	strne	r2, [r5, r4, lsl #2]
	ldrh	r2, [r10, #14]
	cmp	r2, #4
	bne	.L399
	ldr	r1, [fp, #-52]
	ldr	r3, [fp, #-64]
	ldr	r2, [r10, #3248]
	ldrb	r0, [fp, #-56]	@ zero_extendqisi2
	add	r1, r3, r1
	blx	r2
	cmp	r0, #0
	mvnne	r2, #0
	strne	r2, [r5, r4, lsl #2]
.L399:
	add	r4, r4, #5
	b	.L392
.L402:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L408:
	.align	2
.L407:
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
	push	{lr}
	bl	__gnu_mcount_nc
	sub	r3, r0, #1
	movw	r2, #65533
	mov	r6, r0
	uxth	r3, r3
	cmp	r3, r2
	ldmfdhi	sp, {r4, r5, r6, r7, fp, sp, pc}
	ldr	r7, .L418
	add	r4, r7, #412
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	cmp	r1, #0
	beq	.L411
	bl	P2V_block_in_plane
	mov	r5, r0
	ldr	r0, [r7, #3268]
	lsl	r3, r6, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	lsl	r3, r5, #1
	ldr	r1, [r7, #2536]
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r7, #2608]
	add	r3, r3, #1
	str	r3, [r7, #2608]
.L411:
	ldrh	r3, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #4]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #4]	@ movhi
	strh	r6, [r2, #8]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L419:
	.align	2
.L418:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.section	.rodata.str1.1
.LC89:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L425
	add	r4, r6, #412
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L421
	ldrh	r3, [r4, #2]
	sub	r2, r2, #1
	ldr	r0, [r6, #3268]
	strh	r2, [r4, #6]	@ movhi
	mov	r2, #1
	add	r1, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #2]	@ movhi
	ldrh	r5, [r1, #8]
	mov	r1, r2
	lsl	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r6, #2608]
	movw	r2, #65533
	add	r3, r3, #1
	str	r3, [r6, #2608]
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L422
.L421:
	ldrh	r2, [r4, #6]
	mov	r1, r5
	ldr	r0, .L425+4
	bl	sftl_printk
.L423:
	b	.L423
.L422:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L426:
	.align	2
.L425:
	.word	.LANCHOR0
	.word	.LC89
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.section	.rodata.str1.1
.LC90:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldrh	r1, [r0, #10]
	mov	r4, r0
	ldr	r2, [r0, #12]
	mov	r3, #0
.L428:
	uxth	r5, r3
	cmp	r1, r5
	bls	.L433
	mov	r7, r2
	add	r3, r3, #1
	add	r2, r2, #2
	ldrh	r6, [r7]
	cmp	r6, #0
	bne	.L428
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	mov	r1, r0
	uxth	r3, r3
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L429
	ldr	r3, .L435
	ldr	r0, .L435+4
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L430:
	b	.L430
.L429:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L431
.L433:
	ldr	r1, .L435+8
	movw	r2, #578
	ldr	r0, .L435+12
	bl	sftl_printk
.L431:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L436:
	.align	2
.L435:
	.word	.LANCHOR0+412
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	memset
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_memset, .-ftl_memset
	.section	.rodata.str1.1
.LC91:
	.ascii	"%s error allocating memory. return -1\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L537
	movw	r3, #65535
	mov	r2, #32
	movw	r6, #306
	mov	r5, #0
	mov	r7, #12
	mov	r8, #80
	add	r9, r4, #312
	str	r3, [r4, #3276]
	mvn	r3, #0
	strh	r3, [r4, #222]	@ movhi
	strh	r3, [r4, #220]	@ movhi
	add	r3, r4, #3152
	ldrh	r0, [r4, r6]
	str	r5, [r4, #2588]
	strh	r2, [r3]	@ movhi
	movw	r3, #3154
	mov	r2, #128
	lsl	r0, r0, #1
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3160
	str	r5, [r4, #2592]
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3206
	str	r5, [r4, #2576]
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3242
	str	r5, [r4, #2564]
	strh	r5, [r4, r3]	@ movhi
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
	str	r5, [r4, #3280]
	str	r5, [r4, #3156]
	str	r5, [r4, #3284]
	bl	ftl_malloc
	str	r0, [r4, #3192]
	ldrh	r0, [r4, r6]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r4, #232]
	str	r0, [r4, #3200]
	mul	r8, r8, r6
	mov	r0, r8
	bl	ftl_malloc
	mov	r3, #20
	str	r0, [r4, #3288]
	mul	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3292]
	mov	r0, r8
	movw	r8, #310
	bl	ftl_malloc
	str	r0, [r4, #3296]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3268]
	mov	r0, r6
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	ldrh	r6, [r4, r8]
	str	r0, [r4, #3184]
	lsl	r3, r3, #1
	mov	r0, r6
	add	r3, r3, #1
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
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3176]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	ldr	r0, [r4, #3188]
	mul	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	mov	r0, r6
	bl	ftl_malloc
	movw	r3, #258
	str	r0, [r4, #3324]
	ldrh	r0, [r4, r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	ldrh	r6, [r9]
	str	r0, [r4, #3328]
	mul	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3332]
	lsl	r0, r6, #2
	movw	r6, #3340
	bl	ftl_malloc
	ldrh	r3, [r9]
	str	r0, [r4, #3336]
	ldr	r0, [r4, #3188]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3180]
	ldrh	r0, [r4, #242]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, r6]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3344]
	ldrh	r0, [r4, r6]
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r3, r0, #9
	bic	r0, r0, #508
	bic	r0, r0, #3
	strh	r3, [r4, r6]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r4, #242]
	str	r0, [r4, #3348]
	add	r0, r0, #32
	str	r0, [r4, #2536]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3352]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #328]
	str	r0, [r4, #72]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	add	r6, r4, #320
	bl	ftl_malloc
	str	r0, [r4, #3360]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #348]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3364]
	ldrh	r0, [r6]
	movw	r6, #322
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r4, r6]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, r6]
	mov	r1, r5
	str	r0, [r4, #3372]
	movw	r5, #338
	movw	r6, #3388
	lsl	r2, r2, #2
	bl	ftl_memset
	add	r3, r4, #336
	ldrh	r0, [r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldr	r0, [r4, #328]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldrh	r0, [r4, r5]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, r5]
	str	r0, [r4, #2540]
	ldrh	r0, [r4, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #242]
	str	r0, [r4, #3384]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #298
	str	r0, [r4, #2516]
	ldrh	r0, [r4, r3]
	ldrh	r3, [r4, #254]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r4, r6]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, r6]
	ldrh	r3, [r4, #254]
	mov	r5, r0
	str	r0, [r4, #380]
	mov	r1, #1
	lsl	ip, r2, #2
	add	r2, r0, r2, lsl #2
	add	r0, r4, #384
.L439:
	cmp	r3, r1
	bhi	.L440
	cmp	r3, #0
	ldr	r0, .L537+4
	mov	r1, #0
	moveq	r3, #1
	rsb	r2, r3, #8
	add	r3, r3, #7
	lsl	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	memset
	ldr	r3, [r4, #3356]
	cmp	r3, #0
	bne	.L441
.L443:
	ldr	r1, .L537+8
	ldr	r0, .L537+12
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L440:
	str	r2, [r0], #4
	add	r1, r1, #1
	add	r2, r2, ip
	b	.L439
.L441:
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3376]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #2516]
	cmp	r3, #0
	beq	.L443
	cmp	r5, #0
	beq	.L443
	ldr	r3, [r4, #72]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3192]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3200]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3332]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #2536]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #3344]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r4, #348]
	cmp	r3, #0
	beq	.L443
	ldr	r3, .L537
	ldr	r2, [r3, #3364]
	cmp	r2, #0
	beq	.L443
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L443
	ldr	r3, [r3, #3372]
	cmp	r3, #0
	beq	.L443
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L538:
	.align	2
.L537:
	.word	.LANCHOR0
	.word	.LANCHOR0+352
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r2, .L544
	movw	r3, #3388
	mov	r4, r0
	ldr	r7, .L544+4
	sub	r6, r4, #2
	ldr	r8, .L544+8
	add	r4, r4, #1020
	mov	r5, r1
	ldrh	r2, [r2, r3]
	add	r4, r4, #2
	mov	r0, r5
	mov	r1, #0
	lsl	r2, r2, #2
	bl	ftl_memset
.L542:
	ldrh	r3, [r6, #2]!
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	ldrh	r2, [r7]
	cmp	r2, r3
	bhi	.L541
	ldr	r0, .L544+12
	mov	r1, r8
	mov	r2, #74
	bl	sftl_printk
.L541:
	ldrh	r3, [r6]
	mov	r0, #1
	cmp	r4, r6
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r5, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r5, r1, lsl #2]
	bne	.L542
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L545:
	.align	2
.L544:
	.word	.LANCHOR0
	.word	.LANCHOR0+298
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r0, .L547
	mvn	r2, #0
	mov	r1, #255
	add	r3, r0, #352
	add	r0, r0, #364
	strh	r2, [r3]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #6]	@ movhi
	mov	r2, #16
	bl	ftl_memset
	ldmfd	sp, {fp, sp, pc}
.L548:
	.align	2
.L547:
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	ip, .L550
	mov	r4, #0
	mov	r2, #2048
	mov	r1, r4
	add	r3, ip, #412
	strh	r0, [r3]	@ movhi
	add	r0, ip, #420
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L551:
	.align	2
.L550:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	mov	r1, #0
	ldr	r8, [r0, #12]
	ldr	r6, [r0, #24]
	lsl	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	mov	r2, #0
.L553:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L557
	ldr	r3, .L570
	mov	r7, #0
	mov	r10, r7
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r3, [r5]
.L558:
	ldrh	r1, [r4, #10]
	uxth	r2, r7
	cmp	r1, r2
	bhi	.L561
	mov	r0, r10
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L557:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L554:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L553
.L556:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r8, r3]
	subs	lr, ip, #0
	movne	lr, #1
	cmp	ip, r0
	movne	lr, #0
	cmp	lr, #0
	ldrhne	ip, [r5, r3]
	addne	ip, ip, #1
	strhne	ip, [r5, r3]	@ movhi
	b	.L554
.L561:
	uxth	r6, r7
	lsl	r6, r6, #1
	ldrh	r9, [r5, r6]
	cmp	r9, r3
	bcs	.L559
	ldrh	r1, [r8, r6]
	cmp	r1, #0
	movne	r10, r2
	movne	r3, r9
.L559:
	cmp	r9, #0
	bne	.L560
	ldrh	r0, [r8, r6]
	cmp	r0, #0
	beq	.L560
	mov	r1, #1
	str	r3, [fp, #-48]
	bl	FtlFreeSysBlkQueueIn
	strh	r9, [r8, r6]	@ movhi
	ldr	r3, [fp, #-48]
	ldrh	r2, [r4, #8]
	sub	r2, r2, #1
	strh	r2, [r4, #8]	@ movhi
.L560:
	add	r7, r7, #1
	b	.L558
.L571:
	.align	2
.L570:
	.word	.LANCHOR0+304
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L575
	mov	r1, #0
	mvn	r6, #0
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3360]
	lsl	r2, r2, #1
	bl	ftl_memset
	movw	r3, #310
	movw	r2, #338
	ldr	r0, [r4, #3384]
	ldrh	r3, [r4, r3]
	mov	r1, #255
	ldrh	r2, [r4, r2]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	ip, .L575+4
	mov	r3, #0
	mov	r5, r3
	sub	r7, ip, #28
	mov	lr, #12
.L573:
	ldrh	r1, [ip]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L574
	ldr	r3, .L575+8
	ldr	r2, [r4, #328]
	strh	r2, [r3, #10]	@ movhi
	mvn	r2, #0
	str	r2, [r4, #3392]
	ldr	r2, .L575+12
	strh	r2, [r3, #4]	@ movhi
	movw	r2, #3436
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #8]	@ movhi
	sub	r2, r3, #3056
	ldrh	r2, [r2]
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r4, #3356]
	str	r3, [r4, #3404]
	ldr	r3, [r4, #3380]
	str	r3, [r4, #3408]
	ldr	r3, [r4, #3360]
	str	r3, [r4, #3412]
	ldr	r3, [r4, #3376]
	str	r3, [r4, #3416]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L574:
	uxth	r0, r3
	ldr	r2, [r4, #2540]
	add	r3, r3, #1
	mul	r1, lr, r0
	add	r8, r2, r1
	str	r5, [r8, #4]
	strh	r6, [r2, r1]	@ movhi
	ldr	r2, [r4, #2540]
	add	r1, r2, r1
	ldrh	r2, [r7]
	mul	r2, r0, r2
	ldr	r0, [r4, #3384]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	b	.L573
.L576:
	.align	2
.L575:
	.word	.LANCHOR0
	.word	.LANCHOR0+338
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L578
	movw	r2, #3438
	mvn	r3, #0
	mov	r5, #0
	mov	r1, r5
	strh	r3, [r4, r2]	@ movhi
	str	r3, [r4, #3448]
	add	r3, r4, #344
	ldr	r0, [r4, #348]
	str	r5, [r4, #3440]
	strh	r5, [r3]	@ movhi
	add	r3, r4, #320
	str	r5, [r4, #3444]
	ldrh	r2, [r3]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #2536]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #3344]
	lsl	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2464
	mov	r1, r5
	mov	r2, #48
	add	r0, r0, #4
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #512
	add	r0, r4, #2640
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L579:
	.align	2
.L578:
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
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L591
	mov	r2, #6
	mov	r5, #0
	mov	r1, #0
	add	r10, r4, #2528
	mov	r9, r5
	mov	r7, r5
	ldrh	r3, [r4, #242]
	ldr	r0, [r4, #2516]
	mul	r2, r2, r3
	bl	ftl_memset
	str	r5, [r4, #2532]
	str	r5, [r4, #2520]
	str	r5, [r4, #2524]
	strh	r5, [r10]	@ movhi
	strh	r5, [r4, #224]	@ movhi
.L581:
	ldrh	r3, [r4, #240]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L588
	ldrh	r3, [r4, #232]
	str	r3, [fp, #-52]
	ldr	r3, .L591+4
	ldr	r2, .L591+8
	ldrh	r3, [r3]
	str	r3, [fp, #-48]
	mov	r3, #0
	mov	r6, r3
	b	.L589
.L583:
	ldrb	r0, [r2], #1	@ zero_extendqisi2
	mov	r1, r8
	str	r3, [fp, #-60]
	str	r2, [fp, #-56]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-56]
	ldreq	r1, [fp, #-48]
	add	r3, r3, #1
	addeq	r6, r1, r6
	uxtheq	r6, r6
.L589:
	ldr	r0, [fp, #-52]
	uxth	r1, r3
	cmp	r0, r1
	bhi	.L583
	uxth	r3, r5
	cmp	r6, #0
	beq	.L584
	mov	r1, r6
	mov	r0, #32768
	str	r3, [fp, #-48]
	bl	__aeabi_idiv
	ldr	r3, [fp, #-48]
	uxth	r6, r0
.L585:
	ldr	r2, [r4, #2516]
	mov	r1, #6
	mla	r2, r1, r3, r2
	strh	r6, [r2, #4]	@ movhi
	ldrh	r2, [r4, #24]
	cmp	r2, r8
	beq	.L586
	ldrh	r2, [r4, #76]
	cmp	r2, r8
	beq	.L586
	ldrh	r2, [r4, #124]
	cmp	r2, r8
	beq	.L586
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L587
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L586:
	add	r5, r5, #1
	b	.L581
.L584:
	ldr	r1, [r4, #72]
	lsl	r2, r3, #1
	mvn	r0, #0	@ movhi
	strh	r0, [r1, r2]	@ movhi
	b	.L585
.L587:
	add	r7, r7, #1
	mov	r0, r8
	uxth	r7, r7
	bl	INSERT_DATA_LIST
	b	.L586
.L588:
	strh	r7, [r10]	@ movhi
	add	r7, r7, r9
	cmp	r7, r3
	strh	r9, [r4, #224]	@ movhi
	ble	.L590
	ldr	r1, .L591+12
	movw	r2, #2219
	ldr	r0, .L591+16
	bl	sftl_printk
.L590:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L592:
	.align	2
.L591:
	.word	.LANCHOR0
	.word	.LANCHOR0+302
	.word	.LANCHOR0+260
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L594
	movw	r2, #3196
	mov	r3, #0
	movw	r5, #306
	mov	r1, #255
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3204
	ldr	r0, [r4, #3192]
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r4, r5]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, r5]
	mov	r2, #12
	mov	r1, #255
	ldr	r0, [r4, #3200]
	mul	r2, r2, r3
	bl	ftl_memset
	bl	FtlGcBufInit
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L595:
	.align	2
.L594:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L603
	mov	r4, r0
	mov	r5, r1
	mov	r2, #256
	mov	r1, #255
	bl	ftl_memset
	mov	r1, r5
	ldr	r3, [r6, #3244]
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L597
	mov	r0, r4
	mov	r2, #256
	mov	r1, #255
	bl	ftl_memset
	mov	r0, #0
.L597:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	ldmfdne	sp, {r4, r5, r6, r7, fp, sp, pc}
.L599:
	cmp	r3, r1
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L599
.L604:
	.align	2
.L603:
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	memcpy
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_memcpy, .-ftl_memcpy
	.section	.rodata.str1.1
.LC92:
	.ascii	"FlashReadPages %x %x error_ecc_bits %d\012\000"
.LC93:
	.ascii	"data:\000"
.LC94:
	.ascii	"spare:\000"
	.text
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r6, .L647
	bic	r7, r3, #8128
	ldr	r10, .L647+4
	bic	r7, r7, #63
	ldr	r9, .L647+8
	mov	r4, r0
	str	r1, [fp, #-68]
	mov	r8, #0
	ldr	r3, [r7, #24]
	str	r3, [fp, #-48]
	ldrh	r3, [r6, #12]
	str	r3, [fp, #-64]
.L607:
	ldr	r3, [fp, #-68]
	cmp	r8, r3
	bne	.L620
	ldr	r2, [fp, #-48]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L621
	bl	__stack_chk_fail
.L620:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L608
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L609
.L608:
	mov	r1, r10
	mov	r0, r9
	mov	r2, #96
	bl	sftl_printk
.L609:
	sub	r2, fp, #56
	sub	r1, fp, #52
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [fp, #-56]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r4]
	bhi	.L611
	ldr	r5, [r4, #8]
	uxtb	r0, r0
	ldr	r3, [r4, #12]
	tst	r5, #63
	ldr	r1, [fp, #-52]
	ldr	ip, [r6, #3256]
	ldrne	r5, [r6, #3320]
	mov	r2, r5
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L614
	ldr	r0, [fp, #-64]
	add	r2, r5, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [fp, #-52]
	ldr	ip, [r6, #3256]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [fp, #-56]	@ zero_extendqisi2
	blx	ip
	cmn	r0, #1
	beq	.L615
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L616
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L616
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L616
.L615:
	mvn	r3, #0
.L646:
	str	r3, [r4]
.L617:
	ldr	r3, [r4]
	cmn	r3, #1
	cmpne	r3, #256
	bne	.L614
	ldr	r1, [r4, #4]
	ldr	r2, [fp, #-52]
	ldr	r0, .L647+12
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L619
	mov	r3, #4
	ldr	r0, .L647+16
	mov	r2, r3
	bl	rknand_print_hex
.L619:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L614
	mov	r3, #4
	ldr	r0, .L647+20
	mov	r2, r3
	bl	rknand_print_hex
.L614:
	ldr	r3, [r6, #3320]
	cmp	r3, r5
	bne	.L611
	ldr	r0, [r4, #8]
	cmp	r0, r5
	beq	.L611
	ldr	r3, .L647+24
	mov	r1, r5
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L611:
	add	r8, r8, #1
	add	r4, r4, #20
	b	.L607
.L616:
	ldr	r3, [r4]
	sub	r0, r0, #256
	clz	r0, r0
	cmn	r3, #1
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L617
	mov	r3, #256
	b	.L646
.L621:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L648:
	.align	2
.L647:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
	.word	.LC8
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.word	.LANCHOR0+258
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L659
	mov	r6, #0
	add	r7, r5, #364
	sub	r9, r7, #66
	ldr	r3, [r5, #3300]
	ldr	r8, [r5, #3332]
	str	r3, [r5, #3460]
	str	r8, [r5, #3464]
.L650:
	ldrh	r3, [r5, #254]
	cmp	r3, r6
	bhi	.L655
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L655:
	ldrh	r4, [r9]
	mvn	r3, #0
	ldr	r10, .L659+4
	add	r4, r4, r3
	strh	r3, [r7]	@ movhi
	uxth	r4, r4
.L651:
	ldrh	r3, [r9]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L653
	mla	r3, r6, r3, r4
	mov	r2, #1
	mov	r0, r10
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r5, #3456]
	bl	FlashReadPages
	ldr	r3, [r5, #3452]
	cmn	r3, #1
	beq	.L652
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L652
	strh	r4, [r7]	@ movhi
.L653:
	add	r6, r6, #1
	add	r7, r7, #2
	b	.L650
.L652:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L651
.L660:
	.align	2
.L659:
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
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #88
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	cmp	r1, #1
	bic	r6, r3, #8128
	mov	r9, r1
	bic	r6, r6, #63
	movwne	r2, #302
	lsl	r7, r0, #10
	ldr	r3, [r6, #24]
	str	r3, [fp, #-40]
	ldr	r3, .L674
	addeq	r2, r3, #304
	ldrhne	r4, [r3, r2]
	ldrheq	r4, [r2]
	mov	r2, r1
	ldr	r3, [r3, #3324]
	mov	r1, #1
	sub	r4, r4, #1
	str	r3, [fp, #-116]
	sub	r3, fp, #104
	str	r3, [fp, #-112]
	sxth	r4, r4
	orr	r0, r4, r0, lsl #10
	str	r0, [fp, #-120]
	sub	r0, fp, #124
	bl	FlashReadPages
	ldr	r3, [fp, #-104]
	cmn	r3, #1
	bne	.L665
	mov	r8, #0
	b	.L664
.L668:
	add	r3, r8, r4
	mov	r2, r9
	sub	r0, fp, #124
	mov	r1, #1
	add	r3, r3, r3, lsr #31
	sbfx	r5, r3, #1, #16
	orr	r3, r7, r3, asr #1
	str	r3, [fp, #-120]
	bl	FlashReadPages
	ldrd	r2, [fp, #-104]
	and	r3, r3, r2
	cmn	r3, #1
	bne	.L666
	ldr	r3, [fp, #-124]
	cmn	r3, #1
	subne	r4, r5, #1
	sxthne	r4, r4
	bne	.L664
.L666:
	add	r5, r5, #1
	sxth	r8, r5
.L664:
	cmp	r8, r4
	ble	.L668
.L665:
	ldr	r2, [fp, #-40]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L669
	bl	__stack_chk_fail
.L669:
	mov	r0, r4
	sub	sp, fp, #36
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L675:
	.align	2
.L674:
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
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L756
	movw	r3, #3436
	mov	r5, #0
	mov	r1, r5
	add	r6, r4, #320
	ldr	r2, [r4, #328]
	strh	r5, [r4, r3]	@ movhi
	add	r3, r4, #344
	ldr	r0, [r4, #3380]
	lsl	r2, r2, #2
	strh	r5, [r3]	@ movhi
	bl	ftl_memset
	ldr	r2, [r4, #328]
	mov	r1, r5
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6]
	mov	r1, r5
	ldr	r0, [r4, #3368]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r6]
	mov	r1, r5
	ldr	r0, [r4, #348]
	lsl	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2624
	mov	r2, #16
	mov	r1, #255
	bl	ftl_memset
	ldrh	r3, [r4, #240]
.L755:
	str	r3, [fp, #-48]
	ldr	r2, [fp, #-48]
	ldrh	r3, [r4, #242]
	cmp	r3, r2
	bls	.L716
	ldr	r5, .L756+4
	mov	r7, #0
	ldrh	r6, [r4, #232]
	mov	r9, #20
	ldr	r8, [r4, #3288]
	ldr	r2, [r4, #3176]
	ldrh	r10, [r5], #-52
	ldr	r3, [r4, #3180]
	add	r6, r6, r5
	b	.L717
.L679:
	ldr	r1, [fp, #-48]
	ldrb	r0, [r5], #1	@ zero_extendqisi2
	str	r3, [fp, #-60]
	str	r2, [fp, #-56]
	bl	V2P_block
	str	r0, [fp, #-52]
	bl	FtlBbmIsBadBlock
	ldr	r2, [fp, #-56]
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	bne	.L717
	mla	ip, r9, r7, r8
	ldr	r1, [fp, #-52]
	mul	r0, r10, r7
	add	r7, r7, #1
	cmp	r0, #0
	lsl	r1, r1, #10
	stmib	ip, {r1, r2}
	add	r1, r0, #3
	movge	r1, r0
	uxth	r7, r7
	bic	r1, r1, #3
	add	r1, r3, r1
	str	r1, [ip, #12]
.L717:
	cmp	r6, r5
	bne	.L679
	cmp	r7, #0
	bne	.L680
.L715:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	uxth	r3, r3
	b	.L755
.L680:
	mov	r1, r7
	mov	r0, r8
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
	str	r3, [fp, #-52]
.L714:
	ldr	r3, [fp, #-52]
	mov	r8, #20
	mul	r8, r8, r3
	ldr	r3, [r4, #3288]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L683
	mov	r9, #16
	movw	r10, #65535
.L685:
	ldr	r0, [r4, #3288]
	mov	r2, #1
	mov	r1, r2
	add	r0, r0, r8
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r6]
	cmp	r3, r10
	ldr	r3, [r4, #3288]
	bne	.L682
	mvn	r2, #0
	str	r2, [r3, r8]
	ldr	r3, [r4, #3288]
	ldr	r3, [r3, r8]
	cmp	r3, r2
	beq	.L684
.L683:
	ldr	r2, [r4, #2588]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L732
	cmp	r2, r3
	bhi	.L686
.L732:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r4, #2588]
.L686:
	ldrh	r2, [r6]
	movw	r1, #61634
	cmp	r2, r1
	beq	.L687
	bhi	.L688
	movw	r1, #61574
	cmp	r2, r1
	beq	.L689
	movw	r1, #61604
	cmp	r2, r1
	beq	.L690
.L691:
	ldr	r3, [fp, #-52]
	add	r3, r3, #1
	str	r3, [fp, #-52]
	ldrh	r3, [fp, #-52]
	cmp	r7, r3
	bhi	.L714
	b	.L715
.L682:
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L683
	sub	r9, r9, #1
	uxth	r9, r9
	cmp	r9, #0
	bne	.L685
.L684:
	mov	r1, #1
	b	.L753
.L688:
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L691
.L753:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L691
.L687:
	ldr	r8, .L756+8
	ldr	r3, [r4, #328]
	ldrh	r2, [r8]
	cmp	r2, r3
	bls	.L693
	ldr	r1, .L756+12
	mov	r2, #1232
	ldr	r0, .L756+16
	bl	sftl_printk
.L693:
	ldr	lr, [r4, #328]
	ldrh	r1, [r8]
	uxth	r2, lr
	sub	r3, r2, #1
	sub	r2, r2, r1
	sub	r2, r2, #1
	sxth	r3, r3
	sxth	r2, r2
	str	r2, [fp, #-60]
	ldr	r2, [r4, #3380]
	str	r2, [fp, #-56]
	mov	r2, r3
	ldr	r0, [fp, #-56]
	add	r10, r0, #4
.L694:
	ldr	r0, [fp, #-60]
	cmp	r0, r3
	bge	.L699
	lsl	r0, r2, #2
	str	r0, [fp, #-64]
	sub	r0, r2, #1
	ldr	r9, [r6, #4]
	ldr	ip, [r10, r0, lsl #2]
	cmp	r9, ip
	bls	.L695
	ldr	r0, [fp, #-56]
	ldr	r0, [r0]
	cmp	r0, #0
	bne	.L696
	cmp	lr, r1
	addne	r1, r1, #1
	strhne	r1, [r8]	@ movhi
.L696:
	uxth	lr, r3
	mov	ip, #0
.L697:
	uxth	r8, ip
	ldr	r0, [r4, #3380]
	sxth	r1, ip
	cmp	lr, r8
	bhi	.L698
	ldr	ip, [fp, #-64]
	lsl	r2, r2, #1
	ldr	r1, [r6, #4]
	str	r1, [r0, ip]
	ldr	r1, [r4, #3356]
	strh	r5, [r1, r2]	@ movhi
.L699:
	cmp	r3, #0
	blt	.L691
	ldr	r0, .L756+8
	ldr	r2, [r4, #328]
	ldrh	r1, [r0]
	sub	r2, r2, r1
	sub	r2, r2, #1
	sxth	r2, r2
	cmp	r2, r3
	blt	.L691
	ldr	r2, [r4, #3380]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3356]
.L752:
	strh	r5, [r2, r3]	@ movhi
	b	.L691
.L698:
	add	r1, r1, #1
	add	ip, ip, #1
	ldr	r8, [r0, r1, lsl #2]
	add	r0, r0, r1, lsl #2
	lsl	r1, r1, #1
	str	r8, [r0, #-4]
	ldr	r0, [r4, #3356]
	ldrh	r1, [r0, r1]!
	strh	r1, [r0, #-2]	@ movhi
	b	.L697
.L695:
	sub	r3, r3, #1
	mov	r2, r0
	sxth	r3, r3
	b	.L694
.L689:
	ldr	r8, .L756+20
	ldrh	r2, [r8]
	ldrh	r3, [r8, #-24]
	cmp	r2, r3
	bls	.L703
	ldr	r1, .L756+12
	movw	r2, #1273
	ldr	r0, .L756+16
	bl	sftl_printk
.L703:
	ldrh	r2, [r8, #-24]
	ldrh	r1, [r8]
	sub	r0, r2, #1
	ldr	ip, [r4, #3368]
	sxth	r3, r0
	sub	r0, r0, r1
.L704:
	cmp	r3, r0
	ble	.L709
	ldr	r10, [r6, #4]
	lsl	lr, r3, #2
	ldr	r9, [ip, r3, lsl #2]
	cmp	r10, r9
	bls	.L705
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	uxth	r0, r3
	cmp	r2, #0
	addeq	r1, r1, #1
	strheq	r1, [r8]	@ movhi
	mov	r1, #0
.L707:
	uxth	r8, r1
	ldr	ip, [r4, #3368]
	sxth	r2, r1
	cmp	r0, r8
	bhi	.L708
	ldr	r2, [r6, #4]
	str	r2, [ip, lr]
	lsl	r2, r3, #1
	ldr	r1, [r4, #348]
	strh	r5, [r1, r2]	@ movhi
.L709:
	cmp	r3, #0
	blt	.L691
	ldr	r0, .L756+20
	ldrh	r2, [r0, #-24]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r2, r3
	blt	.L691
	ldr	r2, [r4, #3368]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #348]
	b	.L752
.L708:
	add	r2, r2, #1
	add	r1, r1, #1
	ldr	r8, [ip, r2, lsl #2]
	add	ip, ip, r2, lsl #2
	lsl	r2, r2, #1
	str	r8, [ip, #-4]
	ldr	ip, [r4, #348]
	ldrh	r2, [ip, r2]!
	strh	r2, [ip, #-2]	@ movhi
	b	.L707
.L705:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L704
.L690:
	ldr	r8, .L756+24
	movw	r2, #65535
	ldrh	r1, [r8]
	cmp	r1, r2
	strheq	r5, [r8]	@ movhi
	beq	.L754
	ldrh	r0, [r8, #4]
	cmp	r0, r2
	beq	.L712
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L712:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #2632]
	cmp	r2, r3
	strhcs	r5, [r8, #4]	@ movhi
	bcs	.L691
	ldrh	r3, [r8]
	strh	r3, [r8, #4]	@ movhi
	strh	r5, [r8]	@ movhi
	ldr	r3, [r6, #4]
.L754:
	str	r3, [r4, #2632]
	b	.L691
.L716:
	ldr	r1, [r4, #3356]
	ldrh	r3, [r1]
	cmp	r3, #0
	beq	.L718
.L721:
	ldr	r0, [r4, #348]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L719
.L720:
	movw	r3, #3436
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L750
	ldr	r1, .L756+12
	movw	r2, #1398
	ldr	r0, .L756+16
	bl	sftl_printk
.L750:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L718:
	movw	r2, #3436
	ldrh	r2, [r4, r2]
	cmp	r2, #0
	ldrne	r0, [r4, #328]
	beq	.L721
.L722:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L721
	lsl	ip, r2, #1
	add	r3, r3, #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L722
	mov	r3, r2
	mov	lr, #0
	b	.L723
.L724:
	sub	ip, r3, r2
	ldr	r0, [r4, #3356]
	lsl	r1, r3, #1
	lsl	r5, ip, #1
	ldrh	r6, [r0, r1]
	strh	r6, [r0, r5]	@ movhi
	ldr	r0, [r4, #3380]
	ldr	r5, [r0, r3, lsl #2]
	add	r3, r3, #1
	str	r5, [r0, ip, lsl #2]
	sxth	r3, r3
	ldr	r0, [r4, #3356]
	strh	lr, [r0, r1]	@ movhi
.L723:
	ldr	r1, [r4, #328]
	cmp	r3, r1
	bcc	.L724
	b	.L721
.L719:
	ldr	r1, .L756+20
	ldrh	r3, [r1]
	cmp	r3, #0
	ldrhne	ip, [r1, #-24]!
	beq	.L720
.L728:
	sxth	r3, r2
	mov	lr, r3
	cmp	r3, ip
	bge	.L720
	lsl	r5, r3, #1
	add	r2, r2, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L728
	mov	r5, #0
.L729:
	ldrh	r2, [r1]
	cmp	r3, r2
	bge	.L720
	sub	ip, r3, lr
	ldr	r0, [r4, #348]
	lsl	r2, r3, #1
	lsl	r6, ip, #1
	ldrh	r7, [r0, r2]
	strh	r7, [r0, r6]	@ movhi
	ldr	r0, [r4, #3368]
	ldr	r6, [r0, r3, lsl #2]
	add	r3, r3, #1
	str	r6, [r0, ip, lsl #2]
	sxth	r3, r3
	ldr	r0, [r4, #348]
	strh	r5, [r0, r2]	@ movhi
	b	.L729
.L757:
	.align	2
.L756:
	.word	.LANCHOR0
	.word	.LANCHOR0+312
	.word	.LANCHOR0+3436
	.word	.LANCHOR1+231
	.word	.LC8
	.word	.LANCHOR0+344
	.word	.LANCHOR0+2624
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L792
	ldr	r7, .L792+4
	ldr	r8, .L792+8
	ldr	r3, [r4, #3300]
	ldr	r6, [r4, #3332]
	str	r3, [r4, #3460]
	str	r6, [r4, #3464]
	bl	FtlBbtMemInit
	movw	r3, #298
	ldrh	r5, [r4, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L759:
	ldrh	r3, [r7]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L764
	mov	r2, #1
	mov	r0, r8
	mov	r1, r2
	lsl	r3, r5, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L760
.L763:
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L762
	ldr	r2, [r6, #4]
	ldr	r3, .L792+12
	str	r2, [r4, #360]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
.L764:
	ldr	r5, .L792+12
	movw	r2, #65535
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L778
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L768
	mov	r2, #1
	ldr	r0, .L792+8
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L768
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L768
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #360]
	cmp	r3, r2
	bls	.L768
	str	r3, [r4, #360]
	ldrh	r2, [r5, #4]
	ldrh	r3, [r6, #8]
	strh	r2, [r5]	@ movhi
	strh	r3, [r5, #4]	@ movhi
.L768:
	ldrh	r0, [r5]
	mov	r1, #1
	ldr	r10, .L792+8
	bl	FtlGetLastWrittenPage
	sxth	r9, r0
	mov	r8, r0
	add	r3, r9, #1
	mov	r7, #0
	strh	r3, [r5, #2]	@ movhi
.L770:
	sub	r3, r8, r7
	tst	r3, #32768
	beq	.L773
	ldr	r1, .L792+16
	mov	r2, #251
	ldr	r0, .L792+20
	bl	sftl_printk
.L772:
	ldrh	r0, [r6, #12]
	ldrh	r3, [r6, #10]
	strh	r3, [r5, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L775
	ldr	r2, [r4, #228]
	cmp	r0, r2
	beq	.L775
	ldrh	r1, [r4, #242]
	cmp	r0, r1, lsr #2
	movcc	r3, #1
	movcs	r3, #0
	cmp	r2, r1, lsr #2
	andcc	r3, r3, #1
	movcs	r3, #0
	cmp	r3, #0
	beq	.L775
	bl	FtlSysBlkNumInit
.L775:
	ldr	r6, .L792+24
	mov	r5, #0
	add	r7, r6, #3008
.L776:
	ldrh	r3, [r4, #254]
	cmp	r3, r5
	bhi	.L777
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L760:
	ldr	r3, [r4, #3456]
	mov	r2, #1
	mov	r0, r8
	mov	r1, r2
	add	r3, r3, #1
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	bne	.L763
.L762:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L759
.L773:
	ldrh	r2, [r5]
	sub	r3, r9, r7
	mov	r0, r10
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	str	r3, [r4, #3456]
	mov	r1, r2
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L771
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L772
.L771:
	add	r7, r7, #1
	b	.L770
.L777:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3460]
	ldr	r0, [r6], #4
	mul	r1, r5, r2
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	b	.L776
.L778:
	mvn	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L793:
	.align	2
.L792:
	.word	.LANCHOR0
	.word	.LANCHOR0+298
	.word	.LANCHOR0+3452
	.word	.LANCHOR0+352
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+380
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
	sub	sp, sp, #60
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r6, .L833
	bic	r3, r3, #8128
	mov	r7, r0
	bic	r3, r3, #63
	mov	r10, r1
	mov	r4, r0
	mov	r5, r0
	str	r2, [fp, #-88]
	mov	r9, #0
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	ldrh	r3, [r6, #12]
	str	r3, [fp, #-80]
	lsl	r3, r3, #3
	str	r3, [fp, #-84]
.L795:
	cmp	r9, r10
	bne	.L808
	mov	r0, #20
	ldr	r5, .L833+4
	ldr	r8, .L833+8
	mla	r7, r0, r9, r7
.L809:
	cmp	r7, r4
	beq	.L802
	ldr	r2, [r6, #3320]
	mov	r3, #0
	sub	r0, fp, #68
	mov	r1, #1
	str	r3, [r2]
	ldr	r2, [r6, #3328]
	str	r3, [r2]
	ldr	r2, [fp, #-88]
	ldr	r3, [r4, #4]
	str	r3, [fp, #-64]
	ldr	r3, [r6, #3320]
	str	r3, [fp, #-60]
	ldr	r3, [r6, #3328]
	str	r3, [fp, #-56]
	bl	FlashReadPages
	ldr	r9, [fp, #-68]
	cmn	r9, #1
	bne	.L810
	ldr	r1, [r4, #4]
	mov	r0, r5
	bl	sftl_printk
	str	r9, [r4]
.L810:
	ldr	r9, [fp, #-68]
	cmp	r9, #256
	bne	.L811
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	str	r9, [r4]
.L811:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L812
	ldr	r1, .L833
	ldr	r2, [r3]
	ldr	r1, [r1, #3328]
	ldr	r0, [r1]
	cmp	r2, r0
	bne	.L813
	ldr	lr, [r3, #8]
	ldr	ip, [r1, #8]
	cmp	lr, ip
	beq	.L812
.L813:
	ldr	r1, [r1, #4]
	strd	r0, [sp]
	ldr	r0, .L833+12
	ldr	r3, [r3, #4]
	ldr	r1, [r4, #4]
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L812:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L814
	ldr	r1, .L833
	ldr	r2, [r3]
	ldr	r1, [r1, #3320]
	ldr	r0, [r1]
	cmp	r2, r0
	bne	.L815
	ldr	lr, [r3, #2048]
	ldr	ip, [r1, #2048]
	cmp	lr, ip
	beq	.L814
.L815:
	ldr	r1, [r1, #4]
	strd	r0, [sp]
	ldr	r0, .L833+16
	ldr	r3, [r3, #4]
	ldr	r1, [r4, #4]
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L814:
	add	r4, r4, #20
	b	.L809
.L808:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L796
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L797
.L796:
	ldr	r1, .L833+20
	mov	r2, #148
	ldr	r0, .L833+24
	bl	sftl_printk
.L797:
	sub	r2, fp, #76
	sub	r1, fp, #72
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r8, [fp, #-76]
	cmp	r8, #3
	bls	.L798
.L832:
	mvn	r3, #0
	str	r3, [r5]
	b	.L799
.L798:
	cmp	r8, #0
	bne	.L800
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-84]
	cmp	r2, r3
	bls	.L800
	ldr	r6, .L833+20
	ldr	r5, .L833+28
.L801:
	ldr	r2, [r4, #4]
	mvn	r3, #0
	mov	r1, r6
	str	r3, [r4]
	mov	r0, r5
	add	r8, r8, #1
	bl	sftl_printk
	ldr	r1, [r4, #8]
	ldr	r0, .L833+32
	mov	r3, #16
	mov	r2, #4
	add	r4, r4, #20
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L833+36
	bl	rknand_print_hex
	cmp	r10, r8
	bne	.L801
	bl	dump_stack
.L802:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L817
	bl	__stack_chk_fail
.L800:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r8, r1
	beq	.L803
	ldr	r8, [r6, #3320]
	cmp	r8, r1
	beq	.L803
	ldr	r3, .L833+40
	mov	r0, r8
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L803:
	ldr	r3, [r5, #12]
	mov	r2, r8
	ldr	r1, [fp, #-72]
	ldrb	r0, [fp, #-76]	@ zero_extendqisi2
	ldr	ip, [r6, #3252]
	blx	ip
	cmp	r0, #0
	streq	r0, [r5]
	mvnne	r3, #0
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L799
	ldr	r0, [fp, #-80]
	add	r2, r8, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [fp, #-72]
	ldr	ip, [r6, #3252]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [fp, #-76]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	bne	.L832
.L799:
	add	r9, r9, #1
	add	r5, r5, #20
	b	.L795
.L817:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L834:
	.align	2
.L833:
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
	.word	.LANCHOR0+258
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L877
	mov	r9, #0
	mov	r6, r9
	mov	r5, r9
	add	r10, r4, #312
	strd	r0, [fp, #-52]
	mov	r8, #20
	str	r0, [r4, #3284]
.L836:
	ldrh	r1, [r4, #232]
	uxth	r2, r9
	cmp	r1, r2
	bhi	.L840
	cmp	r6, #0
	beq	.L835
	mov	r7, #0
	mov	r8, #20
	ldr	r0, [r4, #3268]
	mov	r2, r6
	mov	r1, #0
	bl	FlashEraseBlocks
.L844:
	mul	r3, r8, r7
	ldr	r2, [r4, #3268]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L843
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L843:
	add	r7, r7, #1
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L844
.L845:
	ldr	r3, [fp, #-48]
	mov	r9, #0
	cmp	r3, #0
	moveq	r3, #2
	movne	r8, #1
	ldrne	r3, .L877+4
	ldreq	r8, [fp, #-48]
	streq	r3, [fp, #-56]
	ldrhne	r3, [r3]
	strne	r3, [fp, #-56]
.L853:
	mov	r10, #0
	mov	r6, r10
.L846:
	ldrh	r1, [r4, #232]
	uxth	r2, r10
	cmp	r1, r2
	bhi	.L849
	cmp	r6, #0
	beq	.L835
	mov	r7, #0
	mov	r10, #20
	ldr	r0, [r4, #3268]
	mov	r2, r8
	mov	r1, r6
	mov	r3, #1
	bl	FlashProgPages
.L852:
	mul	r3, r10, r7
	ldr	r2, [r4, #3268]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L851
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L851:
	add	r7, r7, #1
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L852
	add	r9, r9, #1
	ldr	r2, [fp, #-56]
	uxth	r3, r9
	cmp	r2, r3
	bhi	.L853
	mov	r7, #0
	mov	r9, #20
.L855:
	ldr	r3, [fp, #-48]
	cmp	r3, #0
	beq	.L854
	mul	r3, r9, r7
	ldr	r2, [r4, #3268]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L854
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L854:
	add	r7, r7, #1
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L855
	ldr	r3, [fp, #-48]
	subs	r1, r3, #0
	ldr	r3, [fp, #-52]
	movne	r1, #1
	cmp	r3, #63
	orrls	r1, r1, #1
	cmp	r1, #0
	beq	.L835
	ldr	r0, [r4, #3268]
	mov	r2, r6
	mov	r1, r8
	bl	FlashEraseBlocks
.L835:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L840:
	uxth	r2, r9
	ldr	r0, [r4, #3268]
	mov	r3, #0
	mul	r1, r8, r2
	add	r2, r4, r2
	str	r3, [r0, r1]
	ldr	r1, [fp, #-52]
	ldrb	r0, [r2, #260]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [fp, #-48]
	mov	r7, r0
	cmp	r3, #0
	beq	.L837
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L838
.L837:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L838
	ldr	r2, [r4, #3268]
	lsl	r7, r7, #10
	ldrh	r0, [r10]
	ldr	r1, [r4, #3316]
	mla	r2, r8, r6, r2
	mul	r0, r6, r0
	add	r6, r6, #1
	cmp	r0, #0
	uxth	r6, r6
	str	r1, [r2, #8]
	add	r1, r0, #3
	movge	r1, r0
	ldr	r0, [r4, #3336]
	bic	r1, r1, #3
	str	r7, [r2, #4]
	add	r1, r0, r1
	str	r1, [r2, #12]
.L838:
	add	r9, r9, #1
	b	.L836
.L849:
	uxth	r2, r10
	mov	r3, #20
	ldr	r0, [r4, #3268]
	mul	r1, r3, r2
	add	r2, r4, r2
	mov	r3, #0
	str	r3, [r0, r1]
	ldr	r1, [fp, #-52]
	ldrb	r0, [r2, #260]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [fp, #-48]
	mov	r7, r0
	cmp	r3, #0
	beq	.L847
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L848
.L847:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L848
	ldr	r2, [r4, #3268]
	mov	r3, #20
	ldr	r1, [r4, #3312]
	add	r7, r9, r7, lsl #10
	mla	r2, r3, r6, r2
	ldr	r3, .L877+8
	ldrh	r0, [r3]
	str	r1, [r2, #8]
	str	r7, [r2, #4]
	mul	r0, r6, r0
	add	r6, r6, #1
	cmp	r0, #0
	add	r1, r0, #3
	movge	r1, r0
	ldr	r0, [r4, #3316]
	bic	r1, r1, #3
	uxth	r6, r6
	add	r1, r0, r1
	str	r1, [r2, #12]
.L848:
	add	r10, r10, #1
	b	.L846
.L878:
	.align	2
.L877:
	.word	.LANCHOR0
	.word	.LANCHOR0+304
	.word	.LANCHOR0+312
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #92
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	cmp	r0, #11
	bic	r6, r3, #8128
	mov	r4, r0
	bic	r6, r6, #63
	movls	r5, #0
	ldr	r3, [r6, #24]
	str	r3, [fp, #-32]
	bls	.L879
	ldr	r5, .L885
	sub	r0, fp, #96
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-104]
	lsl	r4, r4, #10
	ldr	r3, [r5, #3324]
	str	r3, [fp, #-108]
	bl	ftl_memset
	ldr	r0, [r5, #3324]
	mov	r2, #8
	mov	r1, #90
	bl	ftl_memset
	mov	r2, #1
	sub	r0, fp, #116
	mov	r1, r2
	str	r4, [fp, #-112]
	bl	FlashEraseBlocks
	mov	r3, #1
	sub	r0, fp, #116
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [fp, #-116]
	cmp	r3, #0
	mvnne	r5, #0
	bne	.L881
	add	r3, r4, #1
	sub	r0, fp, #116
	str	r3, [fp, #-112]
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r5, [fp, #-116]
	subs	r5, r5, #0
	movne	r5, #1
	rsb	r5, r5, #0
.L881:
	sub	r0, fp, #116
	mov	r2, #1
	mov	r1, #0
	str	r4, [fp, #-112]
	bl	FlashEraseBlocks
.L879:
	ldr	r2, [fp, #-32]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L882
	bl	__stack_chk_fail
.L882:
	mov	r0, r5
	sub	sp, fp, #24
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L886:
	.align	2
.L885:
	.word	.LANCHOR0
	.size	FlashTestBlk, .-FlashTestBlk
	.section	.rodata.str1.1
.LC99:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC100:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC101:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
	.text
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
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L895
	mov	r5, #0
	mov	r1, #0
	add	r6, r4, #380
	add	r7, r6, #3008
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	movw	r3, #310
	ldr	r0, [r4, #3300]
	ldrh	r2, [r4, r3]
	str	r0, [r4, #3460]
	bl	ftl_memset
.L888:
	ldrh	r3, [r4, #254]
	cmp	r3, r5
	bgt	.L889
	ldr	r6, [r4, #3464]
	mov	r2, #16
	ldr	r5, .L895+4
	mov	r1, #255
	mov	r0, r6
	mov	r7, #0
	bl	ftl_memset
	ldr	r3, .L895+8
	strh	r3, [r6]	@ movhi
	mov	r9, r5
	mov	r8, r7
	ldr	r3, [r4, #360]
	str	r3, [r6, #4]
	ldrh	r3, [r9], #-48
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #356]
	str	r3, [r6, #8]
	ldr	r3, [r4, #228]
	strh	r3, [r6, #12]	@ movhi
	ldr	r3, [r4, #2604]
	strh	r3, [r6, #14]	@ movhi
.L890:
	ldr	r3, [r4, #3300]
	mov	r10, #0
	ldrh	r2, [r5, #2]
	ldrh	r1, [r5]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	orr	r3, r2, r1, lsl #10
	str	r3, [r4, #3456]
	ldrh	r3, [r6, #10]
	str	r3, [sp]
	ldr	r0, .L895+12
	ldrh	r3, [r5, #4]
	str	r10, [r4, #3452]
	bl	sftl_printk
	ldrh	r3, [r9]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L891
	ldr	r3, [r4, #360]
	ldr	r0, [r4, #3268]
	add	r3, r3, #1
	str	r3, [r4, #360]
	str	r3, [r6, #4]
	ldrh	r2, [r5]
	ldrh	r3, [r5, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	mov	r2, #1
	strh	r3, [r5]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3456]
	str	r3, [r0, #4]
	strh	r10, [r5, #2]	@ movhi
	bl	FlashEraseBlocks
.L891:
	mov	r3, #1
	ldr	r0, .L895+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r0, [r4, #3452]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	cmn	r0, #1
	bne	.L892
	add	r7, r7, #1
	ldr	r1, [r4, #3456]
	ldr	r0, .L895+20
	uxth	r7, r7
	bl	sftl_printk
	cmp	r7, #3
	bls	.L890
	ldr	r1, [r4, #3456]
	mov	r2, r7
	ldr	r0, .L895+24
	bl	sftl_printk
.L894:
	b	.L894
.L889:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3460]
	ldr	r1, [r6], #4
	mul	r0, r5, r2
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L888
.L892:
	add	r8, r8, #1
	cmp	r0, #256
	cmpne	r8, #1
	movle	r0, #1
	movgt	r0, #0
	ble	.L890
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L896:
	.align	2
.L895:
	.word	.LANCHOR0
	.word	.LANCHOR0+352
	.word	-3887
	.word	.LC99
	.word	.LANCHOR0+3452
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L929
	mov	r5, r0
.L898:
	ldr	r2, .L929+4
	ldrh	r3, [r4, #224]
	ldrh	r2, [r2]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L899
	ldr	r1, .L929+8
	movw	r2, #2667
	ldr	r0, .L929+12
	bl	sftl_printk
.L899:
	ldr	r3, .L929+16
	cmp	r5, r3
	movne	r1, #0
	bne	.L900
	ldrh	r3, [r4, #224]
	ldr	r1, [r4, #3164]
	mul	r2, r1, r3
	lsr	r1, r3, #1
	movw	r3, #65535
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cmp	r1, r3
	moveq	r1, #0
.L900:
	ldr	r0, .L929+20
	bl	List_pop_index_node
	ldrh	r3, [r4, #224]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L901
	ldr	r1, .L929+8
	movw	r2, #2676
	ldr	r0, .L929+12
	bl	sftl_printk
.L901:
	ldrh	r3, [r4, #224]
	sub	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #240]
	cmp	r3, r8
	bls	.L898
	uxth	r6, r6
	ldr	r3, [r4, #72]
	lsl	r6, r6, #1
	ldrh	r7, [r3, r6]
	cmp	r7, #0
	bne	.L898
	mov	r0, r5
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L903
	ldr	r3, [r4, #72]
	mvn	r2, #0
	mov	r0, r8
	strh	r2, [r3, r6]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, .L929+4
	ldrh	r3, [r4, #224]
	ldrh	r2, [r2]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L898
	ldr	r1, .L929+8
	movw	r2, #2690
	ldr	r0, .L929+12
	bl	sftl_printk
	b	.L898
.L903:
	ldr	r2, .L929+4
	ldrh	r3, [r4, #224]
	ldrh	r2, [r2]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L905
	ldr	r1, .L929+8
	movw	r2, #2693
	ldr	r0, .L929+12
	bl	sftl_printk
.L905:
	ldr	r0, [r4, #3268]
	add	r10, r5, #16
	ldrh	r3, [r4, #232]
	mov	r9, r10
	str	r3, [fp, #-48]
	mov	r2, r0
	mov	r3, #0
	mov	ip, r3
.L906:
	ldr	lr, [fp, #-48]
	uxth	r1, r3
	cmp	lr, r1
	bhi	.L908
	cmp	r7, #0
	bne	.L909
	ldr	r1, .L929+8
	mov	r2, #2704
	ldr	r0, .L929+12
	bl	sftl_printk
.L909:
	ldrh	r3, [r4, #172]
	cmp	r3, r8
	bne	.L910
	ldr	r1, .L929+8
	movw	r2, #2706
	ldr	r0, .L929+12
	bl	sftl_printk
.L910:
	ldr	r1, [r4, #2536]
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	ldrh	r3, [r1, r6]
	cmp	r2, #0
	ldr	r2, .L929+24
	bne	.L911
	cmp	r3, #0
	moveq	r3, #2
	ldrhne	r0, [r2]
	addne	r3, r3, r0
	uxthne	r3, r3
	strh	r3, [r1, r6]	@ movhi
	ldr	r3, [r4, #2596]
	add	r3, r3, #1
	str	r3, [r4, #2596]
.L913:
	ldr	r3, [r4, #2536]
	ldr	r1, [r4, #2612]
	ldrh	r2, [r2]
	ldrh	r3, [r3, r6]
	ldr	r0, [r4, #2596]
	cmp	r3, r1
	strhi	r3, [r4, #2612]
	ldr	r3, [r4, #2600]
	ldrh	r1, [r4, #240]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3348]
	str	r0, [r4, #2604]
	ldr	r0, [r4, #3268]
	ldr	r3, [r2, #16]
	ldr	ip, .L929+28
	add	r3, r3, #1
	str	r3, [r2, #16]
	add	r3, r0, #4
	mov	r2, #0
.L915:
	uxth	r1, r2
	add	r3, r3, #20
	cmp	r7, r1
	bhi	.L916
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	bl	FlashEraseBlocks
	mov	r9, #0
	mov	r3, r9
	mov	r1, #20
.L917:
	uxth	r2, r9
	cmp	r7, r2
	bhi	.L919
	cmp	r3, #0
	ble	.L920
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L920:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L921
	ldr	r3, [r4, #72]
	mvn	r2, #0
	strh	r2, [r3, r6]	@ movhi
	b	.L898
.L908:
	str	ip, [r2, #8]
	movw	lr, #65535
	str	ip, [r2, #12]
	ldrh	r1, [r9], #2
	cmp	r1, lr
	beq	.L907
	mov	lr, #20
	lsl	r1, r1, #10
	mla	lr, lr, r7, r0
	add	r7, r7, #1
	uxth	r7, r7
	str	r1, [lr, #4]
.L907:
	add	r3, r3, #1
	add	r2, r2, #20
	b	.L906
.L911:
	add	r3, r3, #1
	strh	r3, [r1, r6]	@ movhi
	ldr	r3, [r4, #2600]
	add	r3, r3, #1
	str	r3, [r4, #2600]
	b	.L913
.L916:
	ldr	r1, [r3, #-20]
	add	r2, r2, #1
	and	r1, r1, ip
	str	r1, [r3, #-20]
	b	.L915
.L919:
	mul	r2, r1, r9
	ldr	r0, [r4, #3268]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	cmn	r2, #1
	bne	.L918
	ldr	r0, [ip, #4]
	add	r3, r3, #1
	str	r2, [fp, #-52]
	str	r3, [fp, #-48]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-52]
	strh	r2, [r10]	@ movhi
	mov	r1, #20
	ldr	r3, [fp, #-48]
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r5, #7]
.L918:
	add	r9, r9, #1
	add	r10, r10, #2
	b	.L917
.L921:
	movw	r3, #302
	ldrh	r3, [r4, r3]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2588]
	uxth	r3, r3
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #2588]
	ldr	r1, [r4, #72]
	ldrh	r2, [r5]
	strh	r3, [r5, #4]	@ movhi
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L922
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L923
.L922:
	ldr	r1, .L929+8
	movw	r2, #2759
	ldr	r0, .L929+12
	bl	sftl_printk
.L923:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L930:
	.align	2
.L929:
	.word	.LANCHOR0
	.word	.LANCHOR0+2528
	.word	.LANCHOR1+271
	.word	.LC8
	.word	.LANCHOR0+124
	.word	.LANCHOR0+2532
	.word	.LANCHOR0+292
	.word	-1024
	.size	allocate_data_superblock, .-allocate_data_superblock
	.section	.rodata.str1.1
.LC102:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
	.text
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r9, .L939
	movw	r3, #3206
	str	r0, [fp, #-48]
	ldrh	r3, [r9, r3]
	cmp	r3, #0
	bne	.L938
.L933:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L937:
	uxtah	r3, r9, r7
	ldr	r1, [fp, #-48]
	mov	r8, #0
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	r4, r0
.L934:
	ldr	r3, .L939+4
	uxth	r2, r8
	ldrh	r1, [r3]
	cmp	r1, r2
	bhi	.L936
	add	r7, r7, #1
.L932:
	ldrh	r2, [r9, #232]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L937
	bl	FtlGcReFreshBadBlk
	b	.L933
.L936:
	uxth	r6, r8
	lsl	r1, r6, #1
	add	r3, r10, r6, lsl #1
	ldrh	r1, [r10, r1]
	cmp	r1, r4
	bne	.L935
	mov	r1, r4
	ldr	r0, .L939+8
	str	r3, [fp, #-56]
	str	r2, [fp, #-52]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, .L939+4
	ldr	r2, [fp, #-52]
	add	r1, r6, #1
	add	r1, r10, r1, lsl #1
	ldrh	r5, [r3]
	ldr	r3, [fp, #-56]
	sub	r0, r5, r2
	cmp	r2, r5
	sub	r5, r5, #1
	uxth	r0, r0
	movhi	r2, #0
	lslls	r2, r0, #1
	mov	r0, r3
	bl	memmove
	ldr	r3, .L939+4
	strh	r5, [r3]	@ movhi
.L935:
	add	r8, r8, #1
	b	.L934
.L938:
	ldr	r10, .L939+12
	mov	r7, #0
	b	.L932
.L940:
	.align	2
.L939:
	.word	.LANCHOR0
	.word	.LANCHOR0+3206
	.word	.LC102
	.word	.LANCHOR0+3208
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L953
	lsl	r3, r0, #1
	mov	r5, r0
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L942
	ldrh	r3, [r4, #222]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #220]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #220]	@ movhi
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #172]	@ movhi
	beq	.L946
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L951
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L951
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	beq	.L951
.L946:
	ldr	r6, .L953+4
	mov	r1, r5
	ldr	r0, .L953+8
	bl	List_remove_node
	ldrh	r3, [r6]
	cmp	r3, #0
	bne	.L948
	ldr	r1, .L953+12
	movw	r2, #2835
	ldr	r0, .L953+16
	bl	sftl_printk
.L948:
	ldrh	r3, [r6]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r6]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r6]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L952
	ldr	r1, .L953+12
	movw	r2, #2838
	ldr	r0, .L953+16
	bl	sftl_printk
.L952:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L942:
	bl	List_update_data_list
.L951:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L954:
	.align	2
.L953:
	.word	.LANCHOR0
	.word	.LANCHOR0+2528
	.word	.LANCHOR0+2520
	.word	.LANCHOR1+296
	.word	.LC8
	.size	update_vpc_list, .-update_vpc_list
	.section	.rodata.str1.1
.LC103:
	.ascii	"decrement_vpc_count %x = %d\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	movw	r3, #65535
	ldr	r6, .L967
	mov	r5, r0
	cmp	r0, r3
	beq	.L956
	ldr	r3, [r6, #72]
	lsl	r4, r0, #1
	ldrh	r2, [r3, r4]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r4]	@ movhi
	bne	.L956
	mov	r1, r0
	ldr	r0, .L967+4
	bl	sftl_printk
	ldr	r3, [r6, #72]
	ldrh	r4, [r3, r4]
	cmp	r4, #0
	beq	.L958
.L965:
	mov	r4, #0
.L955:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L958:
	movw	r2, #2853
.L966:
	ldr	r1, .L967+8
	ldr	r0, .L967+12
	bl	sftl_printk
	b	.L955
.L956:
	movw	r7, #3438
	movw	r3, #65535
	ldrh	r0, [r6, r7]
	cmp	r0, r3
	strheq	r5, [r6, r7]	@ movhi
	beq	.L965
.L960:
	cmp	r0, r5
	beq	.L965
	bl	update_vpc_list
	ldr	r2, [r6, #2516]
	ldr	r3, [r6, #2520]
	subs	r4, r0, #0
	ldr	r1, [r6, #72]
	sub	r3, r3, r2
	ldr	r2, .L967+16
	strh	r5, [r6, r7]	@ movhi
	movne	r4, #1
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r2, r3
	uxth	r3, r3
	cmp	r3, r5
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L955
	movw	r2, #2869
	b	.L966
.L968:
	.align	2
.L967:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+312
	.word	.LC8
	.word	-1431655765
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L981
	mov	r3, sp
	bic	r8, r3, #8128
	mov	r4, r0
	bic	r8, r8, #63
	mvn	r0, #2
	mvn	r1, #1
	mov	r9, #0
	ldr	r7, [r5, #3332]
	movw	r2, #61589
	ldr	r3, [r8, #24]
	str	r3, [fp, #-40]
	mvn	r3, #0
	str	r3, [fp, #-44]
	ldr	r3, [r5, #3300]
	str	r3, [fp, #-52]
	str	r7, [fp, #-48]
	strd	r0, [r7, #8]
	ldrh	r3, [r4]
	strh	r3, [r7, #2]	@ movhi
	strh	r9, [r7]	@ movhi
	ldr	r3, [r5, #3300]
	str	r2, [r3]
	ldr	r2, .L981+4
	ldr	r3, [r5, #3300]
	str	r2, [r3, #4]
	ldrh	r6, [r4, #4]
	and	r6, r6, #1
	add	r6, r6, #6
.L970:
	ldrh	r3, [r4, #4]
	cmp	r6, #0
	beq	.L972
	cmp	r3, #0
	bne	.L971
.L972:
	ldrh	r3, [r4]
	ldr	r1, [r5, #72]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #302
	ldr	r2, [fp, #-40]
	ldrh	r3, [r5, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	ldr	r3, [r8, #24]
	cmp	r2, r3
	beq	.L976
	bl	__stack_chk_fail
.L971:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-56]
	beq	.L972
	ldr	r3, [r5, #2592]
	mov	r1, #1
	str	r3, [r7, #4]
	sub	r0, fp, #60
	cmn	r3, #2
	add	r2, r3, #1
	movne	r3, r2
	moveq	r3, r9
	str	r3, [r5, #2592]
	mov	r3, #0
	mov	r2, r3
	sub	r6, r6, #1
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L970
.L976:
	sub	sp, fp, #36
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L982:
	.align	2
.L981:
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
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r8, .L1002
	mov	r7, #0
	bl	FtlBbtMemInit
	sub	r9, r8, #16
	sub	r4, r8, #380
	bl	FtlLoadFactoryBbt
.L984:
	ldrh	r3, [r4, #254]
	cmp	r3, r7
	bhi	.L990
	ldr	r6, .L1002+4
	mov	r5, #0
.L991:
	ldrh	r3, [r6]
	uxth	r0, r5
	cmp	r3, r0
	bhi	.L992
	ldr	r5, .L1002+8
	movw	r7, #65535
	ldrh	r6, [r5, #12]
	sub	r6, r6, #1
	uxth	r6, r6
.L993:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L997
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L994
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L995
	mov	r0, r6
	bl	FtlBbmMapBadBlock
.L994:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L993
.L990:
	ldrh	r5, [r9]
	ldr	r3, [r4, #3332]
	ldr	r0, [r4, #3300]
	str	r3, [fp, #-48]
	str	r3, [r4, #3464]
	movw	r3, #65535
	ldr	r10, .L1002+12
	cmp	r5, r3
	str	r0, [r4, #3460]
	beq	.L985
	ldrh	r3, [r10]
	mov	r2, #1
	ldr	r0, .L1002+16
	mov	r1, r2
	mla	r5, r7, r3, r5
	lsl	r3, r5, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldrh	r2, [r10]
	ldr	r1, [r4, #3460]
	add	r2, r2, #7
	ldr	r0, [r8]
	lsr	r2, r2, #3
	bl	ftl_memcpy
.L986:
	uxth	r0, r5
	add	r7, r7, #1
	add	r8, r8, #4
	add	r9, r9, #2
	bl	FtlBbmMapBadBlock
	b	.L984
.L985:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r4, #3460]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r10]
.L988:
	sub	r6, r6, #1
	uxth	r6, r6
.L987:
	ldrh	r0, [r10]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L988
	mov	r2, #16
	mov	r1, #0
	ldr	r0, [r4, #3332]
	strh	r6, [r9]	@ movhi
	bl	ftl_memset
	mov	r2, #4096
	mov	r1, #0
	ldr	r0, [r4, #3300]
	bl	ftl_memset
	ldr	r2, [fp, #-48]
	ldr	r3, .L1002+20
	strh	r3, [r2]	@ movhi
	mov	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [r10]
	ldrh	r5, [r9]
	strh	r5, [r2, #2]	@ movhi
	ldr	r1, [r8]
	mla	r5, r7, r3, r5
	ldr	r0, [r4, #3460]
	lsl	r3, r5, #10
	str	r3, [r4, #3456]
	ldr	r3, .L1002+24
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1002+16
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1002+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3452]
	uxth	r0, r5
	cmn	r3, #1
	bne	.L986
	bl	FtlBbmMapBadBlock
	b	.L987
.L992:
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	b	.L991
.L995:
	ldrh	r3, [r5]
	cmp	r3, r7
	strheq	r6, [r5]	@ movhi
	beq	.L994
.L996:
	strh	r6, [r5, #4]	@ movhi
.L997:
	ldr	r0, [r4, #3268]
	mov	r6, #0
	ldrh	r3, [r5]
	mov	r1, #1
	str	r6, [r4, #360]
	mov	r2, #2
	strh	r6, [r5, #2]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r5, #4]
	lsl	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r5]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #360]
	ldrh	r2, [r5, #4]
	add	r3, r3, #1
	str	r3, [r4, #360]
	ldrh	r3, [r5]
	strh	r6, [r5, #2]	@ movhi
	strh	r2, [r5]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1003:
	.align	2
.L1002:
	.word	.LANCHOR0+380
	.word	.LANCHOR0+314
	.word	.LANCHOR0+352
	.word	.LANCHOR0+298
	.word	.LANCHOR0+3452
	.word	-3872
	.word	.LANCHOR0+3388
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, r0
	add	r1, r0, r1
	ldr	r0, .L1009
.L1006:
	cmp	r3, r1
	ldmfdeq	sp, {fp, sp, pc}
	lsr	r2, r0, #2
	ldrb	ip, [r3], #1	@ zero_extendqisi2
	add	r2, r2, r0, lsl #5
	add	r2, r2, ip
	eor	r0, r0, r2
	b	.L1006
.L1010:
	.align	2
.L1009:
	.word	1204201446
	.size	js_hash, .-js_hash
	.section	.rodata.str1.1
.LC104:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC105:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
	.text
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L1035
	mov	r4, r0
	mov	r7, r1
	mov	r8, r2
	lsl	r10, r1, #2
	add	r9, r5, #304
	mov	r6, #0
.L1012:
	ldr	r3, [r5, #2572]
	add	r3, r3, #1
	str	r3, [r5, #2572]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1013
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1014
.L1013:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1014:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1015
	ldr	r1, .L1035+4
	mov	r2, #700
	ldr	r0, .L1035+8
	bl	sftl_printk
.L1015:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1016
	ldr	r1, .L1035+4
	movw	r2, #701
	ldr	r0, .L1035+8
	bl	sftl_printk
.L1016:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3332]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r4, #2]
	str	r3, [fp, #-48]
	str	r8, [r5, #3460]
	orr	r2, r2, r3, lsl #10
	str	r2, [r5, #3456]
	mov	r2, #16
	str	r0, [r5, #3464]
	bl	ftl_memset
	ldr	r2, [r5, #3464]
	ldr	r1, [r4, #28]
	ldr	r3, [fp, #-48]
	str	r1, [r2, #4]
	strh	r7, [r2, #8]	@ movhi
	str	r2, [fp, #-48]
	ldrh	r1, [r4, #4]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, .L1035+12
	strh	r1, [r2]	@ movhi
	ldr	r0, [r5, #3460]
	ldrh	r1, [r3]
	bl	js_hash
	ldr	r2, [fp, #-48]
	mov	r3, #1
	mov	r1, r3
	str	r0, [r2, #12]
	mov	r2, r3
	ldr	r0, .L1035+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r5, #3452]
	cmn	r3, #1
	bne	.L1017
	ldr	r1, [r5, #3456]
	add	r6, r6, #1
	ldr	r0, .L1035+20
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ldrhls	r3, [r9]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1019
	ldr	r1, [r5, #3456]
	mov	r2, r6
	ldr	r0, .L1035+24
	bl	sftl_printk
.L1020:
	b	.L1020
.L1019:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1012
.L1022:
	b	.L1022
.L1017:
	ldr	r3, [r4, #24]
	ldr	r2, [r5, #3456]
	str	r2, [r3, r10]
	ldrh	r3, [r4, #2]
	cmp	r3, #1
	beq	.L1023
	ldr	r3, [r5, #3452]
	cmp	r3, #256
	beq	.L1023
	ldr	r2, [r4, #36]
	cmp	r2, #0
	beq	.L1024
.L1023:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1012
.L1024:
	cmn	r3, #1
	bne	.L1025
	ldr	r1, .L1035+4
	movw	r2, #745
	ldr	r0, .L1035+8
	bl	sftl_printk
.L1025:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1036:
	.align	2
.L1035:
	.word	.LANCHOR0
	.word	.LANCHOR1+332
	.word	.LC8
	.word	.LANCHOR0+310
	.word	.LANCHOR0+3452
	.word	.LC104
	.word	.LC105
	.size	FtlMapWritePage, .-FtlMapWritePage
	.section	.rodata.str1.1
.LC106:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC107:
	.ascii	"map_ppn:\000"
.LC108:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
	.text
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r9, .L1046
	mov	r5, r0
	mov	r6, r1
	ldrh	r3, [r9]
	cmp	r3, r0
	bcs	.L1038
	ldr	r1, .L1046+4
	movw	r2, #485
	ldr	r0, .L1046+8
	bl	sftl_printk
.L1038:
	ldr	r4, .L1046+12
	mov	r1, #12
	mul	r6, r1, r6
	ldr	r3, [r4, #3376]
	ldr	r7, [r3, r5, lsl #2]
	ldr	r3, [r4, #2540]
	cmp	r7, #0
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	bne	.L1039
	movw	r3, #310
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	ldr	r3, [r4, #2540]
	strh	r5, [r3, r6]	@ movhi
	ldr	r1, [r4, #2540]
	add	r6, r1, r6
	str	r7, [r6, #4]
.L1040:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1039:
	ldr	r3, [r4, #3332]
	mov	r2, #1
	str	r0, [r4, #3460]
	mov	r1, r2
	ldr	r0, .L1046+16
	str	r3, [r4, #3464]
	str	r7, [r4, #3456]
	bl	FlashReadPages
	ldr	r8, [r4, #3464]
	ldrh	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1041
	ldr	r0, .L1046+20
	mov	r2, r7
	mov	r1, r5
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3464]
	mov	r2, r3
	ldr	r0, .L1046+24
	bl	rknand_print_hex
	ldrh	r3, [r9]
	ldr	r1, [r4, #3376]
	mov	r2, #4
	ldr	r0, .L1046+28
	bl	rknand_print_hex
.L1042:
	ldrh	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1044
	ldr	r1, .L1046+4
	mov	r2, #508
	ldr	r0, .L1046+8
	bl	sftl_printk
.L1044:
	ldr	r3, [r4, #2540]
	mov	r1, #0
	add	r2, r3, r6
	str	r1, [r2, #4]
	strh	r5, [r3, r6]	@ movhi
	b	.L1040
.L1041:
	ldr	r3, [r4, #3452]
	cmp	r3, #256
	bne	.L1044
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L1046+32
	bl	sftl_printk
	ldr	r3, [r4, #2540]
	mov	r1, r5
	add	r0, r4, #3392
	add	r3, r3, r6
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1042
.L1047:
	.align	2
.L1046:
	.word	.LANCHOR0+336
	.word	.LANCHOR1+348
	.word	.LC8
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LC106
	.word	.LC94
	.word	.LC107
	.word	.LC108
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
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r10, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #5
	cmp	r2, r3
	blt	.L1049
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r9, [r5, r0]
	cmp	r9, #0
	beq	.L1049
	ldr	r6, [r4, #32]
	cmp	r6, #0
	bne	.L1049
	mov	r3, #1
	str	r3, [r4, #32]
	strh	r6, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L1064
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1050
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1050:
	ldr	r5, .L1064+4
.L1051:
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1058
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1049:
	ldr	r3, .L1064
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1059
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1059:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1058:
	uxth	r7, r6
	add	r2, r10, r7, lsl #2
	str	r2, [fp, #-48]
	ldr	r2, [r10, r7, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L1052
	ldr	r2, [r5, #3304]
	ldr	r8, [r5, #3332]
	str	r2, [r5, #3460]
	str	r8, [r5, #3464]
	ldr	r0, .L1064+8
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	str	r3, [fp, #-52]
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	ldr	r3, [fp, #-52]
	cmp	r2, r3
	beq	.L1053
	ldr	r1, .L1064+12
	movw	r2, #611
	ldr	r0, .L1064+16
	bl	sftl_printk
	ldr	r3, [fp, #-52]
.L1053:
	ldr	r2, [r5, #3452]
	cmn	r2, #1
	bne	.L1054
.L1056:
	ldr	r2, [fp, #-48]
	mov	r3, #0
	str	r3, [r2]
.L1055:
	b	.L1055
.L1054:
	ldrh	r2, [r8, #8]
	cmp	r2, r3
	bne	.L1056
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1056
	ldr	r2, [r5, #3460]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1052:
	add	r6, r6, #1
	b	.L1051
.L1065:
	.align	2
.L1064:
	.word	.LANCHOR0+304
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LANCHOR1+364
	.word	.LC8
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldrh	r3, [r0]
	movw	r2, #65535
	ldr	r5, [r0, #12]
	mov	r4, r0
	cmp	r3, r2
	bne	.L1067
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1068
	ldr	r1, .L1076
	movw	r2, #641
	ldr	r0, .L1076+4
	bl	sftl_printk
.L1068:
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5]	@ movhi
	mov	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
.L1069:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1067:
	lsl	r3, r3, #1
	ldr	r7, [r0, #24]
	mov	r1, #255
	ldrh	r8, [r5, r3]
	ldr	r5, .L1076+8
	ldrh	r3, [r0, #2]
	ldr	r6, [r5, #3332]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #3456]
	ldr	r3, [r5, #3300]
	str	r3, [r5, #3460]
	ldr	r3, [r0, #28]
	str	r6, [r5, #3464]
	str	r3, [r6, #4]
	ldr	r3, .L1076+12
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r3, [r6]	@ movhi
	add	r3, r5, #304
	strh	r8, [r6, #2]	@ movhi
	ldrh	r2, [r3]
	ldr	r0, [r5, #3300]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L1070:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1072
	movw	r3, #310
	ldr	r0, [r5, #3460]
	ldrh	r1, [r5, r3]
	bl	js_hash
	mov	r2, #1
	str	r0, [r6, #12]
	mov	r1, r2
	mov	r3, #0
	ldr	r0, .L1076+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1069
.L1072:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r8, r0, lsr #10
	bne	.L1071
	add	r3, r3, #1
	ldr	r0, [r5, #3300]
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3300]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1071:
	add	r2, r2, #1
	b	.L1070
.L1077:
	.align	2
.L1076:
	.word	.LANCHOR1+379
	.word	.LC8
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3452
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L1079
	mov	r4, #12
	mul	r4, r4, r0
	add	r0, r5, #3392
	ldr	r3, [r5, #2540]
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r0, [r5, #2540]
	add	r4, r0, r4
	mov	r0, #0
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1080:
	.align	2
.L1079:
	.word	.LANCHOR0
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1093
	mvn	r6, #0
	mov	r5, r0
	mov	r8, r1
	add	r3, r4, #308
	mov	r9, r2
	ldrh	r3, [r3]
	add	r3, r3, #7
	lsr	r7, r0, r3
	bic	r6, r0, r6, lsl r3
	ldr	r3, [r4, #2552]
	uxth	r7, r7
	uxth	r6, r6
	cmp	r3, r0
	bhi	.L1082
	ldr	r1, .L1093+4
	mov	r2, #820
	ldr	r0, .L1093+8
	bl	sftl_printk
	ldr	r3, [r4, #2552]
	cmp	r3, r5
	bhi	.L1082
	mvn	r0, #0
	cmp	r9, #0
	streq	r0, [r8]
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1088:
	add	r3, r3, #1
	mul	r0, r10, r3
	ldrh	r0, [r2, r0]
	cmp	r0, r7
	bne	.L1084
.L1085:
	mov	r1, #12
	ldr	r3, [r4, #2540]
	cmp	r9, #0
	mul	r5, r1, r5
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	ldreq	r3, [r3, r6, lsl #2]
	streq	r3, [r8]
	beq	.L1087
	ldr	r2, [r8]
	str	r2, [r3, r6, lsl #2]
	ldr	r3, [r4, #2540]
	add	r3, r3, r5
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L1093+12
	strh	r7, [r3]	@ movhi
.L1087:
	ldr	r1, [r4, #2540]
	mov	r0, #0
	add	r5, r1, r5
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1082:
	ldr	r2, [r4, #2540]
	movw	r3, #338
	mov	r10, #12
	sub	r2, r2, #12
	ldrh	r1, [r4, r3]
	mov	r3, #0
.L1084:
	uxth	r5, r3
	cmp	r1, r5
	bhi	.L1088
	bl	select_l2p_ram_region
	mul	r10, r10, r0
	ldr	r3, [r4, #2540]
	mov	r5, r0
	add	r2, r3, r10
	ldrh	r1, [r3, r10]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1089
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1089
	bl	flush_l2p_region
.L1089:
	mov	r1, r5
	mov	r0, r7
	bl	load_l2p_region
	b	.L1085
.L1094:
	.align	2
.L1093:
	.word	.LANCHOR0
	.word	.LANCHOR1+410
	.word	.LC8
	.word	.LANCHOR0+2544
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #36
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1114
	mov	r3, sp
	bic	r8, r3, #8128
	bic	r8, r8, #63
	ldrh	r2, [r4, #28]
	ldr	r3, [r8, #24]
	cmp	r2, #0
	str	r3, [fp, #-48]
	beq	.L1096
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1096
	movw	r1, #302
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	ldrh	r1, [r4, r1]
	mul	r3, r1, r3
	cmp	r2, r3
	beq	.L1096
	ldrb	r9, [r4, #34]	@ zero_extendqisi2
	cmp	r9, #0
	bne	.L1095
	ldr	r7, [r4, #2552]
	mov	r2, r9
	sub	r1, fp, #72
	ldrh	r6, [r4, #232]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r5, [r4, #3332]
	ldr	r3, [fp, #-72]
	ldr	r0, [r4, #3300]
	str	r3, [fp, #-64]
	cmn	r3, #1
	str	r7, [fp, #-52]
	str	r0, [fp, #-60]
	str	r5, [fp, #-56]
	str	r9, [r5, #4]
	beq	.L1098
	mov	r2, r9
	sub	r0, fp, #68
	mov	r1, #1
	bl	FlashReadPages
.L1099:
	ldr	r9, .L1114+4
	lsl	r6, r6, #2
	ldr	r3, .L1114+8
	mov	r10, #0
	strh	r3, [r5]	@ movhi
.L1100:
	cmp	r6, #0
	bne	.L1104
.L1101:
	mov	r3, #1
.L1113:
	strb	r3, [r4, #34]
.L1095:
	ldr	r2, [fp, #-48]
	ldr	r3, [r8, #24]
	cmp	r2, r3
	beq	.L1106
	bl	__stack_chk_fail
.L1098:
	movw	r3, #310
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1099
.L1104:
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	beq	.L1101
	ldr	r3, [fp, #-64]
	mov	r0, r9
	str	r3, [r5, #12]
	sub	r6, r6, #1
	ldrh	r3, [r4, #24]
	strh	r3, [r5, #2]	@ movhi
	str	r7, [r5, #8]
	bl	get_new_active_ppa
	ldr	r3, [r4, #2592]
	str	r0, [fp, #-64]
	mov	r1, #1
	cmn	r3, #2
	add	r2, r3, #1
	str	r3, [r5, #4]
	sub	r0, fp, #68
	movne	r3, r2
	moveq	r3, r10
	str	r3, [r4, #2592]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #24]
	bl	decrement_vpc_count
	b	.L1100
.L1096:
	mov	r3, #0
	b	.L1113
.L1106:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1115:
	.align	2
.L1114:
	.word	.LANCHOR0
	.word	.LANCHOR0+24
	.word	-3947
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1126
	mov	r6, r0
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-40]
	bl	P2V_block_in_plane
	lsl	r5, r0, #1
	mov	r7, r0
	ldr	r3, [r4, #72]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L1118
	ldr	r3, [r4, #2532]
	cmp	r3, #0
	beq	.L1118
	ldr	r1, [r4, #2516]
	mov	lr, #6
	ldr	ip, .L1126+4
	movw	r8, #65535
	sub	r3, r3, r1
	ldrh	r0, [r4, #224]
	asr	r3, r3, #1
	mul	r3, ip, r3
	uxth	r3, r3
.L1119:
	uxth	ip, r2
	cmp	r0, ip
	bls	.L1118
	cmp	r3, r7
	bne	.L1120
	ldr	r0, .L1126+8
	mov	r1, r7
	bl	List_remove_node
	ldrh	r3, [r4, #224]
	cmp	r3, #0
	bne	.L1121
	ldr	r1, .L1126+12
	movw	r2, #1742
	ldr	r0, .L1126+16
	bl	sftl_printk
.L1121:
	ldrh	r3, [r4, #224]
	mov	r0, r7
	sub	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r5]
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
.L1118:
	sub	r1, fp, #40
	mov	r0, r6
	mov	r2, #1
	bl	log2phys
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1120:
	mul	r3, lr, r3
	add	r2, r2, #1
	ldrh	r3, [r1, r3]
	cmp	r3, r8
	bne	.L1119
	b	.L1118
.L1127:
	.align	2
.L1126:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2532
	.word	.LANCHOR1+419
	.word	.LC8
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.section	.rodata.str1.1
.LC109:
	.ascii	"data prev_ppa = %x error...................\012\000"
.LC110:
	.ascii	"spuer block %x vpn is 0\012 \000"
	.text
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #68
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	movw	r1, #65535
	bic	r3, r3, #8128
	mov	r10, r0
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	ldrh	r3, [r0]
	cmp	r3, r1
	beq	.L1130
	ldrh	r3, [r0, #2]
	ldr	r5, .L1257
	str	r3, [fp, #-72]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	str	r3, [fp, #-92]
	movw	r3, #302
	ldrh	r2, [r5, r3]
	ldr	r3, [fp, #-72]
	cmp	r2, r3
	mov	r3, #0
	strheq	r2, [r0, #2]	@ movhi
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1132
.L1255:
	strb	r3, [r10, #6]
.L1130:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1196
	bl	__stack_chk_fail
.L1133:
	uxth	r2, r3
	add	r2, r10, r2, lsl #1
	ldrh	r0, [r2, #16]
.L1132:
	uxth	r6, r3
	cmp	r0, r1
	add	r3, r3, #1
	beq	.L1133
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	mov	r4, r0
	cmn	r0, #1
	beq	.L1134
	ldr	r3, [r5, #3176]
	add	r2, r10, #16
	str	r3, [fp, #-64]
	movw	r3, #310
	ldr	r0, [r5, #3288]
	movw	r8, #65535
	ldrh	lr, [r5, r3]
	mov	r9, #20
	ldr	r3, .L1257+4
	ldr	r6, [r5, #3180]
	ldrh	r7, [r3]
	add	r3, r10, #16
	str	r3, [fp, #-84]
	ldrh	r3, [r5, #232]
	mov	r5, #0
	add	r3, r10, r3, lsl #1
	add	r3, r3, #16
	str	r3, [fp, #-68]
.L1135:
	ldr	r3, [fp, #-68]
	cmp	r3, r2
	bne	.L1139
	mov	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, .L1257
	mov	r8, #0
	ldr	r6, [r3, #3288]
	ldr	r2, [r3, #2592]
	mov	r9, r6
	sub	r2, r2, #1
	str	r2, [fp, #-64]
	movw	r2, #65535
	str	r2, [fp, #-80]
	uxth	r2, r4
	str	r2, [fp, #-68]
.L1140:
	uxth	r2, r8
	cmp	r5, r2
	bhi	.L1145
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-68]
	bne	.L1143
.L1249:
	ldr	r0, [r6, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldr	r3, .L1257
	movw	r2, #302
	ldr	r1, [fp, #-68]
	str	r0, [fp, #-76]
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	bne	.L1147
	mov	r2, #0
	strh	r1, [r10, #2]	@ movhi
	strb	r2, [r10, #6]
	strh	r2, [r10, #4]	@ movhi
.L1147:
	sub	r0, fp, #76
	ldr	ip, [fp, #-92]
	ldm	r0, {r0, r1, r2}
	cmp	r2, r1
	cmpeq	r0, ip
	moveq	r2, r0
	ldreq	r1, [fp, #-68]
	beq	.L1256
	ldr	r1, [fp, #-80]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1149
	ldrb	r2, [r10, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1150
.L1149:
	ldr	r2, [r3, #3448]
	uxth	r9, r4
	ldr	r6, .L1257
	cmn	r2, #1
	ldreq	r2, [fp, #-64]
	streq	r2, [r3, #3448]
	uxth	r2, r4
	ldr	r3, [fp, #-72]
	add	r3, r3, #7
	cmp	r3, r2
	sublt	r4, r4, #7
	ldrge	r4, [fp, #-72]
	uxthlt	r4, r4
.L1153:
	ldr	r0, [r6, #3288]
	cmp	r4, r9
	ldrh	ip, [r6, #232]
	bls	.L1198
	mvn	r3, #0
	str	r3, [r6, #3448]
.L1150:
	ldr	r4, .L1257+8
	mov	r3, #1
	ldr	r9, [fp, #-72]
	strh	r3, [r4]	@ movhi
	sub	r4, r4, #3472
.L1161:
	ldrh	ip, [r4, #232]
	mov	r3, #0
	ldr	r0, [r4, #3288]
	movw	lr, #65535
	ldr	r1, [fp, #-84]
	mov	r5, #20
	str	r3, [fp, #-80]
.L1162:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1164
	ldr	r1, [fp, #-80]
	mov	r2, #0
	bl	FlashReadPages
	mov	r3, #0
.L1254:
	str	r3, [fp, #-88]
	ldr	r2, [fp, #-80]
	ldrh	r3, [fp, #-88]
	cmp	r3, r2
	bcc	.L1192
	add	r3, r9, #1
	uxth	r9, r3
	ldr	r3, .L1257+12
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1161
	ldrh	r1, [r4, #232]
	mov	r3, #0
	strh	r9, [r10, #2]	@ movhi
	movw	r0, #65535
	strh	r3, [r10, #4]	@ movhi
.L1193:
	uxth	r2, r3
	cmp	r1, r2
	bls	.L1130
	ldr	r2, [fp, #-84]
	ldrh	ip, [r2], #2
	str	r2, [fp, #-84]
	add	r2, r3, #1
	cmp	ip, r0
	bne	.L1255
	mov	r3, r2
	b	.L1193
.L1134:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	beq	.L1136
	ldr	r1, .L1257+16
	movw	r2, #1809
	ldr	r0, .L1257+20
	bl	sftl_printk
.L1136:
	ldr	r3, [fp, #-92]
	cmp	r3, #0
	cmpne	r3, r6
	beq	.L1137
	ldr	r1, .L1257+16
	movw	r2, #1810
	ldr	r0, .L1257+20
	bl	sftl_printk
.L1137:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1255
.L1139:
	ldrh	r3, [r2], #2
	cmp	r3, r8
	beq	.L1135
	mla	r1, r9, r5, r0
	orr	r3, r4, r3, lsl #10
	mul	ip, lr, r5
	cmp	ip, #0
	str	r3, [r1, #4]
	add	r3, ip, #3
	movge	r3, ip
	ldr	ip, [fp, #-64]
	bic	r3, r3, #3
	add	r3, ip, r3
	mul	ip, r7, r5
	str	r3, [r1, #8]
	add	r5, r5, #1
	cmp	ip, #0
	add	r3, ip, #3
	movge	r3, ip
	uxth	r5, r5
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r1, #12]
	b	.L1135
.L1145:
	ldr	r2, [r9]
	cmp	r2, #0
	bne	.L1141
	ldr	r2, [r9, #12]
	ldr	r7, [r2, #4]
	cmn	r7, #1
	beq	.L1142
	ldr	r1, [r3, #2592]
	mov	r0, r7
	str	r2, [fp, #-76]
	bl	ftl_cmp_data_ver
	ldr	r3, .L1257
	cmp	r0, #0
	ldr	r2, [fp, #-76]
	addne	r7, r7, #1
	strne	r7, [r3, #2592]
.L1142:
	ldr	r2, [r2]
	cmn	r2, #1
	bne	.L1144
.L1143:
	uxth	r3, r4
	str	r3, [fp, #-68]
	mov	r3, #20
	mla	r6, r3, r8, r6
	b	.L1249
.L1141:
	ldr	r2, [fp, #-68]
	str	r2, [fp, #-80]
.L1144:
	add	r8, r8, #1
	add	r9, r9, #20
	b	.L1140
.L1155:
	ldrh	r2, [r1], #2
	cmp	r2, lr
	beq	.L1154
	mla	r8, r7, r5, r0
	add	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r8, #4]
.L1154:
	add	r3, r3, #1
.L1160:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1155
	mov	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, [r6, #3288]
	mov	r2, #0
	movw	r0, #65535
.L1156:
	uxth	r1, r2
	cmp	r5, r1
	bhi	.L1159
	add	r4, r4, #1
	uxth	r4, r4
	b	.L1153
.L1198:
	mov	r3, #0
	ldr	r1, [fp, #-84]
	mov	r5, r3
	movw	lr, #65535
	mov	r7, #20
	b	.L1160
.L1159:
	ldr	r1, [r3]
	cmp	r1, #0
	bne	.L1150
	ldr	r1, [r3, #12]
	ldrh	ip, [r1]
	cmp	ip, r0
	beq	.L1158
	ldr	r1, [r1, #4]
	cmn	r1, #1
	strne	r1, [r6, #3448]
.L1158:
	add	r2, r2, #1
	add	r3, r3, #20
	b	.L1156
.L1164:
	ldrh	r2, [r1], #2
	cmp	r2, lr
	beq	.L1163
	ldr	r6, [fp, #-80]
	orr	r2, r9, r2, lsl #10
	mla	r6, r5, r6, r0
	str	r2, [r6, #4]
	ldr	r2, [fp, #-80]
	add	r2, r2, #1
	uxth	r2, r2
	str	r2, [fp, #-80]
.L1163:
	add	r3, r3, #1
	b	.L1162
.L1192:
	ldr	r3, [fp, #-88]
	mov	r5, #20
	ldr	r8, [r4, #3288]
	mul	r5, r5, r3
	ldr	r3, [fp, #-72]
	add	r7, r8, r5
	cmp	r9, r3
	ldr	r6, [r7, #4]
	str	r6, [fp, #-52]
	bcc	.L1166
	ubfx	r0, r6, #10, #16
	bl	P2V_plane
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-92]
	sub	r3, r9, r3
	clz	r3, r3
	cmp	r2, r0
	lsr	r3, r3, #5
	movls	r3, #0
	cmp	r3, #0
	bne	.L1166
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-76]
	cmp	r9, r3
	cmpeq	r2, r0
	bne	.L1167
	ldr	r3, [fp, #-68]
	strb	r2, [r10, #6]
	mov	r1, r3
	strh	r3, [r10, #2]	@ movhi
.L1256:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1130
.L1167:
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L1168
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r6, [r7]
	cmp	r6, r3
	ldrhne	r0, [r10]
	bne	.L1251
	ldr	r3, [r7, #4]
	str	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1171
	ldr	r1, [r4, #2592]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1171
	ldr	r3, [fp, #-64]
	add	r3, r3, #1
	str	r3, [r4, #2592]
	ldrh	r3, [r7]
	cmp	r3, r6
	beq	.L1171
	ldr	r1, .L1257+16
	movw	r2, #1961
	ldr	r0, .L1257+20
	bl	sftl_printk
.L1171:
	ldr	r6, [r7, #8]
	sub	r1, fp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [fp, #-60]
	bl	log2phys
	ldr	r1, [r4, #3448]
	cmn	r1, #1
	beq	.L1173
	ldr	r0, [fp, #-64]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1173
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	beq	.L1174
	ldr	r0, [r4, #3288]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r8, [r4, #3288]
	add	r3, r8, r5
	ldr	r2, [r8, r5]
	cmn	r2, #1
	bne	.L1175
.L1176:
	mvn	r3, #0
	str	r3, [fp, #-60]
.L1183:
	ldr	r0, [fp, #-60]
	cmn	r0, #1
	beq	.L1166
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #240]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1188
	ldr	r1, .L1257+16
	movw	r2, #2066
	ldr	r0, .L1257+20
	bl	sftl_printk
.L1188:
	ldr	r2, [r4, #72]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1189
	mov	r0, r5
.L1251:
	bl	decrement_vpc_count
	b	.L1166
.L1174:
	ldrd	r2, [fp, #-56]
	cmp	r2, r3
	bne	.L1166
	sub	r1, fp, #60
	mov	r0, r6
	mov	r2, #1
	bl	log2phys
.L1166:
	ldr	r3, [fp, #-88]
	add	r3, r3, #1
	b	.L1254
.L1175:
	ldr	r2, [r7, #8]
	str	r2, [fp, #-100]
	cmp	r2, r6
	bne	.L1176
	str	r3, [fp, #-104]
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #3448]
	mov	r1, r3
	str	r3, [fp, #-96]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1176
	ldr	r0, [fp, #-52]
	ldr	r2, [fp, #-56]
	ldr	r1, [fp, #-60]
	cmp	r2, r0
	ldr	r3, [fp, #-104]
	ldreq	r0, [fp, #-100]
	beq	.L1250
	cmp	r2, r1
	beq	.L1176
	cmn	r2, #1
	streq	r2, [r8, r5]
	beq	.L1180
	str	r2, [r3, #4]
	mov	r0, r3
	mov	r2, #0
	mov	r1, #1
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L1180:
	ldr	r3, [r4, #3288]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L1181
	ldr	r5, [r7, #4]
	ldr	r0, [r4, #3448]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1181
	ldr	r0, [fp, #-96]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1176
.L1181:
	ldr	r1, [fp, #-60]
	mov	r0, r6
.L1250:
	bl	FtlReUsePrevPpa
	b	.L1176
.L1173:
	ldrd	r2, [fp, #-56]
	cmp	r2, r3
	beq	.L1183
	ldr	r1, [fp, #-60]
	cmn	r1, #1
	beq	.L1185
	ldr	r3, [r4, #248]
	cmp	r3, r1, lsr #10
	ldrls	r0, .L1257+24
	bls	.L1253
.L1185:
	sub	r1, fp, #52
	mov	r0, r6
	mov	r2, #1
	bl	log2phys
	ldr	r7, [fp, #-56]
	cmn	r7, #1
	beq	.L1183
	ldr	r3, [fp, #-60]
	cmp	r7, r3
	beq	.L1183
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L1183
.L1187:
	ldr	r0, [r4, #3288]
	mov	r2, #0
	mov	r1, #1
	str	r7, [r0, #4]
	ldr	r5, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3288]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1183
	ldr	r1, [r5, #4]
	ldr	r0, [fp, #-64]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1183
	sub	r1, fp, #56
	mov	r0, r6
	mov	r2, #1
	bl	log2phys
	b	.L1183
.L1189:
	ldr	r0, .L1257+28
	mov	r1, r5
.L1253:
	bl	sftl_printk
	b	.L1166
.L1168:
	ldr	r3, [r4, #3476]
	cmp	r3, #31
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3476]
	strls	r6, [r2, #3480]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3448]
	cmn	r3, #1
	ldreq	r3, [fp, #-64]
	beq	.L1252
	ldr	r2, [fp, #-64]
	cmp	r3, r2
	bls	.L1166
	mov	r3, r2
.L1252:
	str	r3, [r4, #3448]
	b	.L1166
.L1196:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1258:
	.align	2
.L1257:
	.word	.LANCHOR0
	.word	.LANCHOR0+312
	.word	.LANCHOR0+3472
	.word	.LANCHOR0+302
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LC109
	.word	.LC110
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.section	.rodata.str1.1
.LC111:
	.ascii	"...%s enter...\012\000"
.LC112:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC113:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
	.text
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r5, .L1284
	bic	r6, r3, #8128
	ldr	r1, .L1284+4
	bic	r6, r6, #63
	ldr	r0, .L1284+8
	mov	r4, #0
	ldr	r3, [r6, #24]
	str	r3, [fp, #-48]
	bl	sftl_printk
	ldr	r0, .L1284+12
	mov	r2, #8192
	mov	r1, #0
	bl	ftl_memset
.L1260:
	ldr	r3, [r5, #2552]
	cmp	r3, r4
	bhi	.L1262
	ldr	r9, .L1284+12
	mov	r4, #0
	ldr	r10, .L1284+16
	mov	r7, r4
.L1263:
	ldrh	r2, [r5, #240]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1265
	ldr	r4, [r5, #2532]
	cmp	r4, #0
	beq	.L1266
	ldr	r1, [r5, #2516]
	mov	r9, #0
	ldrh	r8, [r5, #224]
	sub	r4, r4, r1
	ldr	r1, .L1284+20
	ldr	r10, .L1284+12
	asr	r4, r4, #1
	ldr	r0, .L1284+24
	mul	r4, r1, r4
	uxth	r4, r4
.L1267:
	uxth	r3, r9
	cmp	r8, r3
	bls	.L1266
	ldr	r2, [r5, #72]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1268
	ldrh	r3, [r10, r3]
	mov	r1, r4
	bl	sftl_printk
	ldr	r0, .L1284+24
	mov	r7, #1
.L1268:
	mov	r3, #6
	ldr	r2, [r5, #2516]
	add	r9, r9, #1
	mul	r3, r3, r4
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1267
.L1266:
	cmp	r7, #0
	beq	.L1259
	ldr	r1, .L1284+4
	movw	r2, #2394
	ldr	r0, .L1284+28
	bl	sftl_printk
.L1259:
	ldr	r2, [fp, #-48]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1271
	bl	__stack_chk_fail
.L1262:
	sub	r1, fp, #52
	mov	r0, r4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [fp, #-52]
	cmn	r0, #1
	beq	.L1261
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L1284+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1261:
	add	r4, r4, #1
	b	.L1260
.L1265:
	uxth	r1, r4
	ldr	r3, [r5, #72]
	lsl	r8, r1, #1
	ldrh	r2, [r3, r8]
	ldrh	r3, [r9, r8]
	cmp	r2, r3
	beq	.L1264
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #72]
	movw	r2, #65535
	ldrh	r3, [r3, r8]
	cmp	r3, r2
	beq	.L1264
	ldrh	r2, [r9, r8]
	cmp	r2, r3
	movhi	r7, #1
.L1264:
	add	r4, r4, #1
	b	.L1263
.L1271:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1285:
	.align	2
.L1284:
	.word	.LANCHOR0
	.word	.LANCHOR1+457
	.word	.LC111
	.word	check_vpc_table
	.word	.LC112
	.word	-1431655765
	.word	.LC113
	.word	.LC8
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
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #32
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r4, .L1295
	bic	r6, r3, #8128
	mov	r5, #0
	bic	r6, r6, #63
	ldr	r0, .L1295+4
	mov	r1, #0
	ldr	r3, [r6, #24]
	str	r3, [fp, #-32]
	bl	sftl_printk
.L1287:
	ldr	r3, [r4, #2552]
	cmp	r3, r5
	bhi	.L1293
	ldr	r2, [fp, #-32]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1294
	bl	__stack_chk_fail
.L1293:
	sub	r1, fp, #36
	mov	r0, r5
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1288
	ldr	r2, [fp, #-36]
	mov	r1, r5
	ldr	r0, .L1295+8
	bl	sftl_printk
.L1288:
	ldr	r3, [fp, #-36]
	cmn	r3, #1
	beq	.L1290
	ldr	r7, [r4, #3332]
	mov	r2, #0
	str	r3, [r4, #3456]
	mov	r1, #1
	ldr	r3, [r4, #3300]
	ldr	r0, .L1295+12
	str	r3, [r4, #3460]
	str	r5, [r4, #3468]
	str	r7, [r4, #3464]
	str	r2, [r4, #3452]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1291
	ldr	r3, [r7, #8]
	cmp	r3, r5
	beq	.L1290
.L1291:
	ldr	r2, [r4, #3460]
	ldr	r3, [r4, #3464]
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
	ldr	r2, [r4, #3456]
	ldr	r3, [r3]
	bl	sftl_printk
.L1290:
	add	r5, r5, #1
	b	.L1287
.L1294:
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1296:
	.align	2
.L1295:
	.word	.LANCHOR0
	.word	.LC114
	.word	.LC115
	.word	.LANCHOR0+3452
	.word	.LC116
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.section	.rodata.str1.1
.LC117:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
	.text
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #60
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	mov	r4, r0
	bic	r3, r3, #8128
	str	r1, [fp, #-84]
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	ldr	r3, .L1329
	ldrh	r5, [r3, #4]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1322
	cmp	r5, #0
	beq	.L1299
.L1298:
	ldr	r2, .L1329+4
	movw	r3, #302
	ldrh	r3, [r2, r3]
	ldr	r2, [fp, #-84]
	cmp	r3, r2
	bne	.L1300
.L1299:
	bl	FtlGcPageVarInit
.L1300:
	ldr	r6, .L1329+4
	mov	r3, #0
	str	r3, [fp, #-80]
.L1317:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1301
.L1302:
	ldr	r3, [r6, #3176]
	mov	r7, #0
	str	r3, [fp, #-88]
	movw	lr, #65535
	ldr	r3, .L1329+8
	mov	r8, #20
	ldr	r0, [r6, #3288]
	ldr	r10, [r6, #3180]
	ldrh	r2, [r3]
	ldrh	r9, [r3, #2]
	ldrh	r3, [r6, #232]
	str	r2, [fp, #-92]
	add	r2, r4, #16
	add	r3, r4, r3, lsl #1
	add	r3, r3, #16
	str	r3, [fp, #-96]
.L1303:
	ldr	r3, [fp, #-96]
	cmp	r3, r2
	bne	.L1305
	mov	r10, #0
	mov	r1, r7
	mov	r2, #0
	bl	FlashReadPages
.L1306:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1318
	ldr	r3, [fp, #-80]
	add	r5, r5, #1
	add	r3, r3, #1
	str	r3, [fp, #-80]
	ldr	r2, [fp, #-80]
	uxth	r5, r5
	ldr	r3, [fp, #-84]
	cmp	r3, r2
	bhi	.L1319
	ldr	r2, .L1329
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1319
	ldr	r1, [fp, #-80]
	add	r3, r3, r1
	strh	r3, [r2, #4]	@ movhi
	ldr	r2, .L1329+4
	movw	r3, #302
	ldrh	r3, [r2, r3]
	cmp	r3, r5
	bls	.L1301
.L1320:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1321
	bl	__stack_chk_fail
.L1322:
	mov	r5, #0
	b	.L1298
.L1305:
	ldrh	r3, [r2], #2
	cmp	r3, lr
	beq	.L1303
	mla	r1, r8, r7, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [fp, #-92]
	mul	ip, r3, r7
	cmp	ip, #0
	add	r3, ip, #3
	movge	r3, ip
	ldr	ip, [fp, #-88]
	bic	r3, r3, #3
	add	r3, ip, r3
	mul	ip, r9, r7
	str	r3, [r1, #8]
	add	r7, r7, #1
	cmp	ip, #0
	add	r3, ip, #3
	movge	r3, ip
	uxth	r7, r7
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #12]
	b	.L1303
.L1318:
	mov	r9, #20
	ldr	r2, [r6, #3288]
	mul	r9, r9, r10
	add	r1, r2, r9
	ldr	r2, [r2, r9]
	ldr	r3, [r1, #4]
	ldr	r8, [r1, #12]
	cmp	r2, #0
	bne	.L1307
	ldrh	r0, [r8]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1327
	ldr	r0, [r8, #8]
	ldr	r1, [r6, #2552]
	cmp	r0, r1
	bls	.L1309
.L1327:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r6, #72]
	mov	r5, #0
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #172]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1317
.L1309:
	sub	r1, fp, #72
	str	r3, [fp, #-92]
	str	r2, [fp, #-88]
	bl	log2phys
	ldr	r0, [fp, #-72]
	ldr	r1, [r8, #12]
	cmn	r0, #1
	ldr	r2, [fp, #-88]
	sub	ip, r1, r0
	ldr	r3, [fp, #-92]
	clz	ip, ip
	lsr	ip, ip, #5
	moveq	ip, #0
	cmp	ip, #0
	bne	.L1311
.L1316:
	ldr	r2, [r8, #8]
.L1328:
	ldr	r0, [r8, #12]
	mov	r1, r3
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L1306
.L1311:
	str	r1, [fp, #-64]
	sub	r0, fp, #68
	ldr	r1, [r6, #3316]
	str	r1, [fp, #-60]
	ldr	r1, [r6, #3336]
	str	r1, [fp, #-56]
	mov	r1, #1
	str	r3, [fp, #-92]
	str	r2, [fp, #-88]
	bl	FlashReadPages
	ldr	r1, .L1329+12
	ldr	r0, [r6, #3288]
	ldr	r2, [fp, #-88]
	add	r9, r0, r9
	ldr	r3, [fp, #-92]
	ldrh	r1, [r1]
	ldr	r0, [fp, #-60]
	lsl	r1, r1, #7
.L1313:
	cmp	r1, r2
	beq	.L1316
	ldr	ip, [r9, #8]
	ldr	lr, [ip, r2, lsl #2]
	ldr	ip, [r0, r2, lsl #2]
	cmp	lr, ip
	beq	.L1314
	ldr	r2, [fp, #-64]
	ldrh	r1, [r4]
	ldr	r0, .L1329+16
	bl	sftl_printk
	b	.L1327
.L1314:
	add	r2, r2, #1
	b	.L1313
.L1307:
	mvn	r2, #0
	b	.L1328
.L1319:
	ldr	r3, .L1329+20
	ldrh	r3, [r3]
	cmp	r3, r5
	bhi	.L1302
.L1301:
	ldr	r3, .L1329
	mvn	r2, #0
	mov	r1, r5
	mov	r0, r4
	strh	r5, [r4, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1320
.L1321:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1330:
	.align	2
.L1329:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+310
	.word	.LANCHOR0+258
	.word	.LC117
	.word	.LANCHOR0+302
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #100
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L1347
	mov	r3, sp
	bic	r6, r3, #8128
	bic	r6, r6, #63
	ldr	r9, [r5, #2720]
	ldr	r3, [r6, #24]
	str	r3, [fp, #-48]
	cmp	r9, #0
	ldr	r3, [r5, #2552]
	beq	.L1332
	ldr	r2, [r5, #2724]
	cmp	r2, r3
	bcs	.L1333
	mov	r4, #2048
.L1338:
	ldr	r0, [r5, #2724]
	ldr	r3, [r5, #2552]
	cmp	r0, r3
	bcc	.L1334
.L1337:
	mvn	r0, #0
.L1331:
	ldr	r2, [fp, #-48]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1342
	bl	__stack_chk_fail
.L1334:
	mov	r2, #0
	sub	r1, fp, #136
	bl	log2phys
	ldr	r3, [r5, #2724]
	ldr	r2, [fp, #-136]
	add	r3, r3, #1
	str	r3, [r5, #2724]
	cmn	r2, #1
	beq	.L1336
	str	r2, [fp, #-128]
	sub	r0, fp, #132
	str	r3, [fp, #-116]
	mov	r2, #0
	ldr	r3, [r5, #3324]
	mov	r1, #1
	str	r3, [fp, #-124]
	sub	r3, fp, #112
	str	r2, [fp, #-132]
	str	r3, [fp, #-120]
	bl	FlashReadPages
	ldr	r3, [fp, #-132]
	cmp	r3, #256
	bne	.L1337
	ldr	r0, [fp, #-136]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1337
.L1336:
	subs	r4, r4, #1
	bne	.L1338
	b	.L1337
.L1333:
	ldr	r3, [r5, #2556]
	mov	r0, #0
	str	r3, [r5, #2716]
	str	r0, [r5, #2720]
	str	r0, [r5, #2724]
	b	.L1331
.L1332:
	ldr	r2, .L1347+4
	movw	r4, #10000
	ldr	r1, [r5, #2612]
	ldr	r8, [r5, #2556]
	cmp	r1, r4
	ldr	r10, [r5, #2716]
	ldrh	r7, [r2, #28]
	add	r2, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r10, r2
	bhi	.L1341
	mov	r0, #1000
	lsr	r1, r1, #10
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__aeabi_uidiv
	add	r0, r0, r10
	cmp	r8, r0
	bhi	.L1341
	ands	r0, r4, r7
	movne	r0, r9
	bne	.L1331
	ldr	r3, [r5, #2740]
	cmp	r3, r7
	beq	.L1331
.L1341:
	mov	r3, #1
	mov	r0, #0
	str	r7, [r5, #2740]
	str	r3, [r5, #2720]
	str	r0, [r5, #2724]
	str	r8, [r5, #2716]
	b	.L1331
.L1342:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1348:
	.align	2
.L1347:
	.word	.LANCHOR0
	.word	.LANCHOR0+2468
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L1353
	mov	r4, #0
	ldr	r5, .L1353+4
	bl	FtlWriteDump_data
.L1350:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1352
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1352:
	ldr	r2, [r6, #2540]
	uxth	r3, r4
	mov	r1, #12
	mla	r3, r1, r3, r2
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
	.word	.LANCHOR0+338
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #108
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r7, .L1365
	bic	r10, r3, #8128
	str	r2, [fp, #-136]
	bic	r10, r10, #63
	add	r2, r0, r1
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r10, #24]
	str	r3, [fp, #-48]
	mov	r3, r7
	ldrh	r8, [r3], #-12
	lsr	r8, r0, r8
	ldrh	r3, [r3]
	cmp	r2, r3
	subls	r7, r7, #308
	movls	r9, #0
	bls	.L1357
	mvn	r9, #0
	b	.L1355
.L1362:
	ldr	r3, [r7, #3372]
	mov	r0, r6
	ldr	r2, [r3, r8, lsl #2]
	ldr	r3, .L1365+4
	str	r2, [fp, #-148]
	ldrh	r3, [r3]
	str	r3, [fp, #-144]
	mov	r1, r3
	bl	__aeabi_uidivmod
	ldr	r3, [fp, #-144]
	ldr	r2, [fp, #-148]
	sub	r4, r3, r1
	str	r1, [fp, #-140]
	ldr	r0, [r7, #3308]
	uxth	r4, r4
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	r2, #0
	cmpne	r3, r4
	movne	r1, #1
	moveq	r1, #0
	beq	.L1359
	str	r2, [fp, #-128]
	mov	r2, #1
	str	r0, [fp, #-124]
	mov	r1, r2
	sub	r0, fp, #132
	sub	r3, fp, #112
	str	r3, [fp, #-120]
	bl	FlashReadPages
.L1360:
	lsl	r3, r4, #9
	ldr	r0, [r7, #3308]
	mov	r2, r3
	str	r3, [fp, #-144]
	ldr	r3, [fp, #-140]
	sub	r5, r5, r4
	ldr	r1, [fp, #-136]
	add	r6, r6, r4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	ldr	r2, [r7, #3308]
	mov	r1, r8
	ldr	r0, .L1365+8
	bl	FtlMapWritePage
	ldr	r2, [fp, #-136]
	ldr	r3, [fp, #-144]
	cmn	r0, #1
	add	r8, r8, #1
	add	r3, r2, r3
	mvneq	r9, #0
	str	r3, [fp, #-136]
.L1357:
	cmp	r5, #0
	bne	.L1362
.L1355:
	ldr	r2, [fp, #-48]
	ldr	r3, [r10, #24]
	cmp	r2, r3
	beq	.L1363
	bl	__stack_chk_fail
.L1359:
	ldr	r3, .L1365+12
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1360
.L1363:
	mov	r0, r9
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1366:
	.align	2
.L1365:
	.word	.LANCHOR0+308
	.word	.LANCHOR0+258
	.word	.LANCHOR0+3608
	.word	.LANCHOR0+310
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r2, .L1369
	ldr	r3, .L1369+4
	ldr	r1, [r2, #2640]
	cmp	r1, r3
	ldmfdne	sp, {fp, sp, pc}
	ldr	r3, .L1369+8
	add	r2, r2, #2640
	str	r3, [r2, #4]
	mov	r1, #1
	ldr	r3, [r2, #-60]
	mov	r0, #0
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
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177369
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L1376
	movw	r1, #3652
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	strhls	r2, [r3, r1]	@ movhi
	ldr	r2, [r3, #3348]
	movls	r1, #1
	movhi	r1, #32
	cmp	r0, #0
	ldr	ip, [r2, #16]
	bne	.L1373
	ldr	r0, [r2, #20]
	add	r1, r1, r0
	cmp	r1, ip
	bhi	.L1374
.L1373:
	ldr	r1, .L1376+4
	mov	r0, #64
	str	r1, [r2]
	str	ip, [r2, #20]
	movw	r2, #3340
	ldrh	r1, [r3, r2]
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
.L1374:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1377:
	.align	2
.L1376:
	.word	.LANCHOR0
	.word	1112818501
	.size	FtlEctTblFlush, .-FtlEctTblFlush
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
	push	{lr}
	bl	__gnu_mcount_nc
	add	r0, r0, #256
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_vendor_write, .-sftl_vendor_write
	.section	.rodata.str1.1
.LC118:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
	.text
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #108
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r6, .L1390
	bic	r3, r3, #8128
	mov	r9, r2
	bic	r3, r3, #63
	str	r3, [fp, #-136]
	add	r2, r0, r1
	mov	r8, r0
	mov	r5, r1
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	mov	r3, r6
	ldrh	r7, [r3], #-12
	lsr	r7, r0, r7
	ldrh	r3, [r3]
	cmp	r2, r3
	subls	r6, r6, #308
	movls	r10, #0
	bls	.L1381
	mvn	r10, #0
	b	.L1379
.L1387:
	ldr	r2, .L1390+4
	mov	r0, r8
	ldr	r3, [r6, #3372]
	ldrh	r4, [r2]
	ldr	r3, [r3, r7, lsl #2]
	mov	r1, r4
	str	r3, [fp, #-144]
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	ldr	r3, [fp, #-144]
	uxth	r4, r4
	str	r1, [fp, #-140]
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	r3, #0
	lsl	r2, r4, #9
	str	r2, [fp, #-144]
	beq	.L1383
	ldr	r2, [r6, #3308]
	sub	r0, fp, #132
	str	r2, [fp, #-124]
	sub	r2, fp, #112
	str	r2, [fp, #-120]
	mov	r2, #1
	mov	r1, r2
	str	r3, [fp, #-128]
	str	r3, [fp, #-148]
	bl	FlashReadPages
	ldr	r2, [fp, #-132]
	ldr	r3, [fp, #-148]
	cmn	r2, #1
	ldr	r2, [r6, #3452]
	mvneq	r10, #0
	cmp	r2, #256
	bne	.L1385
	ldr	r0, .L1390+8
	mov	r2, r3
	mov	r1, r7
	bl	sftl_printk
	ldr	r2, [r6, #3308]
	ldr	r0, .L1390+12
	mov	r1, r7
	bl	FtlMapWritePage
.L1385:
	ldr	r1, [r6, #3308]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-140]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1386:
	ldr	r3, [fp, #-144]
	add	r7, r7, #1
	sub	r5, r5, r4
	add	r8, r8, r4
	add	r9, r9, r3
.L1381:
	cmp	r5, #0
	bne	.L1387
.L1379:
	ldr	r3, [fp, #-136]
	ldr	r2, [fp, #-48]
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1388
	bl	__stack_chk_fail
.L1383:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r9
	bl	ftl_memset
	b	.L1386
.L1388:
	mov	r0, r10
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1391:
	.align	2
.L1390:
	.word	.LANCHOR0+308
	.word	.LANCHOR0+258
	.word	.LC118
	.word	.LANCHOR0+3608
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.section	.rodata.str1.1
.LC119:
	.ascii	"no ect\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1394
	movw	r5, #3340
	mov	r0, #64
	ldr	r2, [r4, #3348]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3348]
	ldr	r2, [r3]
	ldr	r3, .L1394+4
	cmp	r2, r3
	beq	.L1393
	ldr	r1, .L1394+8
	ldr	r0, .L1394+12
	bl	sftl_printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #3348]
	mov	r1, #0
	lsl	r2, r2, #9
	bl	ftl_memset
.L1393:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1395:
	.align	2
.L1394:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1398
	mov	r1, #1
	mov	r0, #0
	ldr	r5, .L1398+4
	add	r2, r4, #2640
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2640]
	cmp	r3, r5
	beq	.L1397
	add	r0, r4, #2640
	mov	r2, #512
	mov	r1, #0
	bl	ftl_memset
	str	r5, [r4, #2640]
.L1397:
	ldr	r2, [r4, #2672]
	str	r2, [r4, #2608]
	ldr	r2, [r4, #2676]
	str	r2, [r4, #2560]
	ldr	r2, [r4, #2680]
	ldr	r3, [r4, #2728]
	str	r2, [r4, #2568]
	ldr	r2, [r4, #2684]
	str	r3, [r4, #2580]
	str	r2, [r4, #2612]
	ldr	r3, [r4, #2732]
	ldr	r2, [r4, #2688]
	str	r3, [r4, #2584]
	str	r2, [r4, #2616]
	ldr	r3, [r4, #2648]
	ldr	r2, .L1398+8
	str	r3, [r4, #2576]
	ldr	r3, [r4, #2652]
	str	r3, [r4, #2564]
	ldr	r3, [r4, #2656]
	str	r3, [r4, #2556]
	ldr	r3, [r4, #2660]
	ldrh	r2, [r2]
	str	r3, [r4, #2572]
	ldr	r0, [r4, #2596]
	ldr	r3, [r4, #2668]
	ldrh	r1, [r4, #240]
	str	r3, [r4, #2600]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2604]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1399:
	.align	2
.L1398:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR0+292
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	push	{lr}
	bl	__gnu_mcount_nc
	add	r0, r0, #256
	bl	FtlVendorPartRead
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_vendor_read, .-sftl_vendor_read
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, [r0, #36]
	mov	r4, r0
	cmp	r3, #0
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	ldrh	r6, [r0, #6]
	mov	r2, #0
	ldr	r5, .L1408
	sub	r6, r6, #1
	str	r2, [r0, #36]
	ldr	r3, [r0, #24]
	uxth	r6, r6
	ldr	r0, [r5, #3304]
	ldr	r2, [r5, #3332]
	str	r0, [r5, #3460]
	str	r2, [r5, #3464]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r5, #3456]
	cmp	r3, #0
	beq	.L1403
	mov	r2, #1
	ldr	r0, .L1408+4
	mov	r1, r2
	bl	FlashReadPages
.L1404:
	ldr	r2, [r5, #3460]
	mov	r1, r6
	mov	r0, r4
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1403:
	movw	r3, #310
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
	b	.L1404
.L1409:
	.align	2
.L1408:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.section	.rodata.str1.1
.LC120:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
	.text
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1420
	mov	r6, #0
	mov	r1, #255
	add	r5, r4, #2624
	add	r9, r4, #304
	sub	r8, r5, #156
	ldr	r7, [r4, #3332]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldrh	r3, [r5]
	str	r7, [r4, #3464]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, .L1420+4
	strh	r3, [r7]	@ movhi
	str	r6, [r7, #12]
	ldr	r3, [r4, #2632]
	str	r3, [r7, #4]
	ldr	r3, .L1420+8
	str	r3, [r4, #2468]
	ldr	r3, .L1420+12
	str	r3, [r4, #2472]
	ldrh	r3, [r5, #6]
	str	r6, [r7, #8]
	strh	r3, [r5, #-148]	@ movhi
	ldrh	r3, [r4, #254]
	strb	r3, [r4, #2478]
	ldrh	r3, [r4, #24]
	ldrh	r2, [r4, #26]
	strh	r3, [r5, #-142]	@ movhi
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-140]	@ movhi
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	strb	r3, [r4, #2479]
	ldrh	r3, [r4, #76]
	ldrh	r2, [r4, #78]
	strh	r3, [r5, #-138]	@ movhi
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-136]	@ movhi
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	strb	r3, [r4, #2480]
	ldrh	r3, [r4, #124]
	strh	r3, [r5, #-134]	@ movhi
	ldrh	r2, [r4, #126]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	ldr	r0, [r4, #3460]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-132]	@ movhi
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	strb	r3, [r4, #2481]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2500]
	ldr	r3, [r4, #2588]
	str	r3, [r4, #2508]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2504]
	movw	r3, #310
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3460]
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #3460]
	movw	r8, #65535
	ldr	r1, [r4, #72]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.L1411:
	ldrh	r1, [r5, #2]
	ldrh	r2, [r5]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3456]
	ldrh	r3, [r9]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L1412
	mov	r3, #0
	ldrh	r8, [r5, #4]
	strh	r3, [r5, #2]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2588]
	str	r3, [r4, #2632]
	add	r2, r3, #1
	strh	r0, [r5]	@ movhi
	str	r2, [r4, #2588]
	lsl	r2, r0, #10
	str	r2, [r4, #3456]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L1412:
	ldr	r3, .L1420+16
	ldr	r0, [r4, #3300]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1420+20
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3452]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r5, #2]	@ movhi
	bne	.L1413
	cmp	r3, #1
	bne	.L1415
	ldr	r1, .L1420+24
	movw	r2, #1145
	ldr	r0, .L1420+28
	bl	sftl_printk
	ldrh	r3, [r5, #2]
	cmp	r3, #1
	ldrheq	r3, [r9]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
.L1415:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, #3
	bls	.L1411
	ldr	r1, [r4, #3456]
	mov	r2, r6
	ldr	r0, .L1420+32
	bl	sftl_printk
.L1418:
	b	.L1418
.L1413:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1411
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1419
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1419:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1421:
	.align	2
.L1420:
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177369
	.word	.LANCHOR0+310
	.word	.LANCHOR0+3452
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	push	{lr}
	bl	__gnu_mcount_nc
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
	.global	sftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_discard, %function
sftl_discard:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r8, .L1444
	bic	r7, r3, #8128
	mov	r9, r0
	bic	r7, r7, #63
	mov	r4, r1
	ldr	r3, [r7, #24]
	str	r3, [fp, #-40]
	ldr	r3, [r8, #340]
	cmp	r3, r1
	cmpcs	r3, r0
	bls	.L1437
	add	r2, r0, r1
	cmp	r3, r2
	bcc	.L1437
	cmp	r1, #31
	bhi	.L1429
.L1434:
	mov	r0, #0
.L1427:
	ldr	r2, [fp, #-40]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L1435
	bl	__stack_chk_fail
.L1429:
	movw	r3, #258
	ldrh	r5, [r8, r3]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r6, r0
	smulbb	r0, r0, r5
	sub	r9, r9, r0
	uxth	r9, r9
	cmp	r9, #0
	beq	.L1430
	sub	r5, r5, r9
	add	r6, r6, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	sub	r4, r4, r5
.L1430:
	ldr	r5, .L1444+4
	mvn	r3, #0
	str	r3, [fp, #-44]
.L1431:
	ldrh	r3, [r5]
	cmp	r3, r4
	bls	.L1433
	ldr	r3, [r8, #3656]
	cmp	r3, #32
	bls	.L1434
	mov	r4, #0
	str	r4, [r8, #3656]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1434
.L1433:
	sub	r1, fp, #48
	mov	r0, r6
	mov	r2, #0
	bl	log2phys
	ldr	r3, [fp, #-48]
	cmn	r3, #1
	beq	.L1432
	ldr	r3, [r8, #3656]
	sub	r1, fp, #44
	mov	r2, #1
	mov	r0, r6
	add	r3, r3, #1
	str	r3, [r8, #3656]
	ldr	r3, [r8, #2560]
	add	r3, r3, #1
	str	r3, [r8, #2560]
	bl	log2phys
	ldr	r0, [fp, #-48]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1432:
	ldrh	r3, [r5]
	add	r6, r6, #1
	sub	r4, r4, r3
	b	.L1431
.L1437:
	mvn	r0, #0
	b	.L1427
.L1435:
	sub	sp, fp, #36
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1445:
	.align	2
.L1444:
	.word	.LANCHOR0
	.word	.LANCHOR0+258
	.size	sftl_discard, .-sftl_discard
	.section	.rodata.str1.1
.LC121:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
	.text
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r4, .L1461
	bic	r7, r3, #8128
	ldr	r1, .L1461+4
	bic	r7, r7, #63
	ldr	r0, .L1461+8
	mov	r5, #0
	ldr	r3, [r7, #24]
	str	r3, [fp, #-48]
	bl	sftl_printk
	ldrh	r2, [r4, #242]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1447:
	ldr	r3, [r4, #2552]
	cmp	r3, r5
	bhi	.L1449
	ldr	r9, .L1461+12
	mov	r8, #0
	ldr	r10, .L1461+16
.L1450:
	ldrh	r3, [r4, #240]
	uxth	r5, r8
	cmp	r3, r5
	bhi	.L1456
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [fp, #-48]
	ldr	r3, [r7, #24]
	cmp	r2, r3
	beq	.L1457
	bl	__stack_chk_fail
.L1449:
	sub	r1, fp, #52
	mov	r0, r5
	mov	r2, #0
	bl	log2phys
	ldr	r0, [fp, #-52]
	cmn	r0, #1
	beq	.L1448
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3352]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1448:
	add	r5, r5, #1
	b	.L1447
.L1456:
	uxth	r1, r8
	ldr	r3, [r4, #72]
	movw	r0, #65535
	lsl	r6, r1, #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r4, #3352]
	ldrh	r3, [r3, r6]
	cmp	r2, r0
	cmpne	r2, r3
	beq	.L1452
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #24]
	cmp	r3, r5
	beq	.L1452
	ldrh	r3, [r4, #124]
	cmp	r3, r5
	beq	.L1452
	ldrh	r3, [r4, #76]
	cmp	r3, r5
	beq	.L1452
	ldr	r3, [r4, #72]
	ldr	r2, [r4, #3352]
	ldrh	r1, [r3, r6]
	ldrh	r2, [r2, r6]
	cmp	r1, #0
	strh	r2, [r3, r6]	@ movhi
	bne	.L1454
	mov	r1, r5
	mov	r0, r10
	bl	List_remove_node
	ldrh	r3, [r4, #224]
	cmp	r3, #0
	bne	.L1455
	ldr	r1, .L1461+4
	movw	r2, #2330
	ldr	r0, .L1461+20
	bl	sftl_printk
.L1455:
	ldrh	r3, [r4, #224]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	bl	INSERT_DATA_LIST
.L1452:
	add	r8, r8, #1
	b	.L1450
.L1454:
	mov	r0, r5
	bl	update_vpc_list
	b	.L1452
.L1457:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1462:
	.align	2
.L1461:
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC121
	.word	.LANCHOR0+2532
	.word	.LC8
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r6, .L1473
	mov	r5, r0
	ldrh	r4, [r0]
	ldrh	r3, [r6, #240]
	cmp	r3, r4
	bcs	.L1464
	ldr	r1, .L1473+4
	movw	r2, #2766
	ldr	r0, .L1473+8
	bl	sftl_printk
.L1464:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1465
	ldr	r2, [r6, #72]
	lsl	r3, r4, #1
	mov	r0, r4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1466
	bl	INSERT_DATA_LIST
.L1465:
	mov	r3, #1
	strb	r3, [r5, #8]
	movw	r3, #3438
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1467
	cmp	r0, r4
	bne	.L1468
	ldr	r3, [r6, #72]
	lsl	r4, r4, #1
	ldrh	r3, [r3, r4]
	cmp	r3, #0
	beq	.L1469
.L1468:
	bl	update_vpc_list
.L1469:
	movw	r3, #3438
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1467:
	mov	r0, r5
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1466:
	bl	INSERT_FREE_LIST
	b	.L1465
.L1474:
	.align	2
.L1473:
	.word	.LANCHOR0
	.word	.LANCHOR1+507
	.word	.LC8
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.section	.rodata.str1.1
.LC122:
	.ascii	"FtlProgPages error %x = %d\012\000"
.LC123:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
	.text
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r5, r3
	mov	r3, sp
	bic	r6, r3, #8128
	ldr	r9, .L1493
	bic	r6, r6, #63
	mov	r7, #0
	mov	r8, r7
	mov	r2, #0
	mov	r4, r0
	str	r1, [fp, #-56]
	ldr	r3, [r6, #24]
	str	r3, [fp, #-48]
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1476:
	ldr	r3, [fp, #-56]
	cmp	r8, r3
	beq	.L1482
	ldr	r10, .L1493+4
	b	.L1483
.L1478:
	ldr	r1, [r4, #4]
	mov	r0, r10
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1477
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1477:
	mov	r0, r5
	mov	r7, #1
	bl	get_new_active_ppa
	str	r0, [r4, #4]
	str	r0, [fp, #-52]
	mov	r2, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	mov	r1, #1
	bl	FlashProgPages
.L1483:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	beq	.L1478
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #232]
	cmp	r2, r3
	bcc	.L1479
	ldr	r1, .L1493+8
	movw	r2, #986
	ldr	r0, .L1493+12
	bl	sftl_printk
.L1479:
	ldr	r3, [r4, #4]
	sub	r1, fp, #52
	ldr	r0, [r4, #16]
	mov	r2, #1
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L1480
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #72]
	lsl	r3, r0, #1
	mov	r10, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1481
	mov	r1, r0
	ldr	r0, .L1493+16
	bl	sftl_printk
.L1481:
	mov	r0, r10
	bl	decrement_vpc_count
.L1480:
	add	r8, r8, #1
	add	r4, r4, #20
	b	.L1476
.L1482:
	cmp	r7, #0
	beq	.L1484
	bl	FtlSysFlush
.L1484:
	ldr	r3, .L1493
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #232]
	cmp	r2, r3
	bcc	.L1475
	ldr	r1, .L1493+8
	movw	r2, #1005
	ldr	r0, .L1493+12
	bl	sftl_printk
.L1475:
	ldr	r2, [fp, #-48]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1486
	bl	__stack_chk_fail
.L1486:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1494:
	.align	2
.L1493:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	ldr	r4, .L1520
	bic	r6, r3, #8128
	bic	r6, r6, #63
	ldrh	r2, [r4, #124]
	ldr	r3, [r6, #24]
	str	r3, [fp, #-48]
	movw	r3, #302
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1496
.L1503:
	ldrh	r2, [r4, #124]
	mov	r3, #0
	str	r3, [r4, #3444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1517
.L1497:
	mov	r0, #0
	b	.L1495
.L1496:
	ldr	r5, .L1520+4
	cmp	r0, #0
	beq	.L1499
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1500
.L1501:
	mov	r1, #2
.L1499:
	ldr	r0, .L1520+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [fp, #-52]
	cmp	r2, r3
	movne	r0, #1
	beq	.L1503
.L1495:
	ldr	r2, [fp, #-48]
	ldr	r3, [r6, #24]
	cmp	r2, r3
	beq	.L1512
	bl	__stack_chk_fail
.L1500:
	mov	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, #17
	bhi	.L1501
	b	.L1499
.L1517:
	movw	r3, #3204
	movw	r1, #302
	ldrh	r2, [r4, r3]
	ldrh	r1, [r4, r1]
	ldrb	r3, [r4, #131]	@ zero_extendqisi2
	mul	r3, r1, r3
	cmp	r2, r3
	beq	.L1504
	ldr	r1, .L1520+12
	mov	r2, #164
	ldr	r0, .L1520+16
	bl	sftl_printk
.L1504:
	movw	r0, #302
	ldrh	r3, [r4, #124]
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	mov	r5, #0
	ldrh	r0, [r4, r0]
	mov	r10, #12
	lsl	r3, r3, #1
	ldr	r1, [r4, #72]
	ldr	r9, .L1520+20
	smulbb	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #3204
	ldr	r2, [r4, #2576]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2576]
.L1505:
	ldrh	r2, [r9]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1509
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #124]
	ldr	r2, [r4, #72]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1510
	bl	INSERT_DATA_LIST
.L1511:
	movw	r3, #3204
	mov	r5, #0
	mvn	r7, #0
	strh	r7, [r4, #124]	@ movhi
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3196
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #2620
	ldrh	r2, [r4, #224]
	ldrh	r3, [r4, r3]
	strh	r7, [r4, #172]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	ldrgt	r3, .L1520+24
	movgt	r2, #20
	strhgt	r2, [r3]	@ movhi
	b	.L1497
.L1509:
	uxth	r8, r5
	ldr	r3, [r4, #3200]
	ldr	r2, [r4, #2552]
	mul	r8, r10, r8
	add	r7, r3, r8
	ldr	r0, [r7, #8]
	cmp	r0, r2
	bcc	.L1506
.L1518:
	ldrh	r0, [r4, #124]
	b	.L1519
.L1506:
	mov	r2, #0
	sub	r1, fp, #52
	str	r3, [fp, #-56]
	bl	log2phys
	ldr	r3, [fp, #-56]
	ldr	r2, [fp, #-52]
	ldr	r0, [r3, r8]
	cmp	r0, r2
	bne	.L1508
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r7, #4
	mov	r8, r0
	mov	r2, #1
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L1519:
	bl	decrement_vpc_count
	b	.L1507
.L1508:
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bne	.L1518
.L1507:
	add	r5, r5, #1
	b	.L1505
.L1510:
	bl	INSERT_FREE_LIST
	b	.L1511
.L1512:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1521:
	.align	2
.L1520:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+124
	.word	.LANCHOR1+549
	.word	.LC8
	.word	.LANCHOR0+3204
	.word	.LANCHOR0+3152
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1524
	movw	r5, #302
	add	r0, r4, #124
	ldrh	r1, [r4, r5]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #126]
	ldrh	r3, [r4, r5]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	add	r0, r4, #3392
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3444]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1525:
	.align	2
.L1524:
	.word	.LANCHOR0
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1527
	mov	r5, #0
	add	r6, r4, #24
	add	r4, r4, #76
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
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1528:
	.align	2
.L1527:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1537
	mov	r5, #0
	mov	r6, #20
	mov	r3, #0
	mov	r2, r3
	ldr	r1, [r4, #3168]
	ldr	r0, [r4, #3292]
	bl	FlashProgPages
.L1530:
	ldr	r1, [r4, #3168]
	uxth	r3, r5
	ldr	r2, [r4, #3292]
	cmp	r3, r1
	bcc	.L1533
	mov	r0, r2
	bl	FtlGcBufFree
	ldrh	r3, [r4, #128]
	mov	r0, #0
	str	r0, [r4, #3168]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, r7, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1533:
	mul	r3, r6, r3
	add	r5, r5, #1
	add	r1, r2, r3
	ldr	r2, [r2, r3]
	ldr	ip, [r1, #12]
	cmn	r2, #1
	ldr	r1, [r1, #4]
	ldr	r0, [ip, #12]
	ldrne	r2, [ip, #8]
	bl	FtlGcUpdatePage
	b	.L1530
.L1538:
	.align	2
.L1537:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1542
	movw	r3, #65535
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1540
	ldrh	r3, [r4, #128]
	cmp	r3, #0
	bne	.L1541
.L1540:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1542+4
	strb	r5, [r4, #132]
	bl	allocate_data_superblock
	movw	r3, #3196
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3204
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1541:
	ldr	r0, .L1542+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1543:
	.align	2
.L1542:
	.word	.LANCHOR0
	.word	.LANCHOR0+124
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.section	.rodata.str1.1
.LC124:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
	.text
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
	sub	sp, sp, #44
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1660
	mov	r3, sp
	bic	r3, r3, #8128
	str	r0, [fp, #-72]
	bic	r3, r3, #63
	ldr	r0, [r4, #3272]
	ldr	r3, [r3, #24]
	cmp	r0, #0
	str	r3, [fp, #-48]
	movne	r0, #0
	bne	.L1544
	add	r3, r4, #2528
	ldrh	r3, [r3]
	cmp	r3, #47
	bls	.L1544
	ldr	r3, .L1660+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1546
.L1549:
	ldrh	r3, [r4, #220]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1547
.L1548:
	bl	FtlReadRefresh
	ldrh	r0, [r4, #124]
	ldrh	r5, [r4, #172]
	movw	r6, #65535
	ldr	r3, [r4, #3156]
	and	r5, r5, r0
	ldrh	r0, [r4, #222]
	ldr	r2, [fp, #-72]
	add	r3, r3, #1
	and	r5, r5, r0
	cmp	r5, r6
	add	r3, r3, r2, lsl #7
	movne	r5, r6
	str	r3, [r4, #3156]
	bne	.L1550
	ldrh	r2, [r4, #224]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1550
	movw	r3, #3660
	mov	r7, #0
	str	r7, [r4, #3156]
	strh	r7, [r4, r3]	@ movhi
	bl	GetSwlReplaceBlock
	mov	r5, r0
	cmp	r0, r6
	bne	.L1552
	movw	r6, #3154
	ldrh	r2, [r4, #224]
	ldrh	r3, [r4, r6]
	cmp	r2, r3
	bcs	.L1553
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1555
	mov	r0, r3
	ldr	r3, [r4, #72]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1556
	mov	r0, r7
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r3, #128
	cmp	r0, r5
	strh	r3, [r4, r6]	@ movhi
	beq	.L1555
	mov	r5, r0
.L1552:
	ldr	r1, .L1660+8
	lsl	r2, r5, #1
	ldr	r3, [r4, #72]
	ldr	r0, .L1660+12
	ldrh	r1, [r1]
	ldrh	r3, [r3, r2]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2536]
	ldrh	r2, [r1, r2]
	mov	r1, r5
	str	r2, [sp]
	ldrh	r2, [r4, #224]
	bl	sftl_printk
	b	.L1555
.L1546:
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1549
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1549
	mov	r0, #1
	b	.L1544
.L1547:
	ldrh	r1, [r4, #222]
	cmp	r1, r2
	strheq	r3, [r4, #222]	@ movhi
	mvneq	r3, #0
	strheq	r3, [r4, #220]	@ movhi
	b	.L1548
.L1556:
	mov	r3, #64
.L1656:
	strh	r3, [r4, r6]	@ movhi
.L1555:
	bl	FtlGcReFreshBadBlk
.L1550:
	ldr	r2, [fp, #-72]
	movw	r3, #65535
	cmp	r2, #0
	cmpeq	r5, r3
	moveq	r2, #1
	movne	r2, #0
	bne	.L1558
	ldrh	r3, [r4, #224]
	cmp	r3, #24
	movhi	r6, #1
	bhi	.L1559
	movw	r2, #302
	cmp	r3, #16
	ldrh	r6, [r4, r2]
	lsrhi	r6, r6, #5
	bhi	.L1559
	cmp	r3, #12
	lsrhi	r6, r6, #4
	bhi	.L1559
	cmp	r3, #8
	lsrhi	r6, r6, #2
.L1559:
	ldr	r2, .L1660+8
	ldrh	r1, [r2]
	cmp	r1, r3
	bcs	.L1562
	ldrh	r1, [r4, #222]
	ldrh	r3, [r4, #124]
	and	r3, r3, r1
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1563
	movw	r3, #3660
	ldrh	r0, [r4, r3]
	cmp	r0, #0
	bne	.L1564
	ldr	r3, [r4, #2552]
	ldr	r1, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1565
.L1564:
	movw	r3, #2620
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	lsr	r3, r3, #2
.L1565:
	strh	r3, [r2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3164]
.L1544:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1619
	bl	__stack_chk_fail
.L1553:
	mov	r3, #80
	b	.L1656
.L1563:
	movw	r3, #2620
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r2]	@ movhi
.L1562:
	movw	r3, #3206
	movw	r5, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	addne	r6, r6, #32
	uxthne	r6, r6
.L1567:
	ldrh	r3, [r4, #172]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1575
	cmp	r5, r3
	strhne	r5, [r4, #172]	@ movhi
	bne	.L1577
	ldrh	r3, [r4, #222]
	cmp	r3, r5
	beq	.L1577
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #222]
	strh	r3, [r4, #172]	@ movhi
	mvn	r3, #0
	strh	r3, [r4, #222]	@ movhi
.L1577:
	ldrh	r0, [r4, #172]
	mov	r3, #0
	strb	r3, [r4, #180]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1575
	bl	IsBlkInGcList
	subs	r7, r0, #0
	beq	.L1580
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
.L1575:
	ldrh	r3, [r4, #172]
	ldrh	r2, [r4, #24]
	cmp	r2, r3
	beq	.L1581
	ldrh	r2, [r4, #76]
	cmp	r2, r3
	beq	.L1581
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	bne	.L1582
.L1581:
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
.L1582:
	ldr	r4, .L1660
.L1617:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1583
	ldr	r7, .L1660+16
	mov	r3, #0
	str	r3, [r4, #3164]
.L1584:
	ldrh	r9, [r7]
	mov	r0, r9
	bl	List_get_gc_head_node
	uxth	r10, r0
	movw	r3, #65535
	mov	r8, r0
	cmp	r10, r3
	strh	r10, [r4, #172]	@ movhi
	bne	.L1585
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r7]	@ movhi
	b	.L1544
.L1558:
	ldrh	r6, [r4, #124]
	ldrh	r1, [r4, #222]
	and	r6, r6, r1
	and	r6, r6, r5
	cmp	r6, r3
	bne	.L1629
	ldrh	r3, [r4, #172]
	cmp	r3, r6
	beq	.L1568
.L1572:
	mov	r5, r6
.L1629:
	mov	r6, #1
	b	.L1567
.L1568:
	ldr	r8, .L1660+8
	str	r2, [r4, #3164]
	ldrh	r2, [r4, #224]
	ldrh	r3, [r8]
	cmp	r2, r3
	bls	.L1569
	movw	r3, #3660
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1570
	ldr	r3, [r4, #2552]
	ldr	r2, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L1571
.L1570:
	movw	r3, #2620
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	lsr	r3, r3, #2
.L1571:
	strh	r3, [r8]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #72]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1569
.L1659:
	movw	r3, #3660
	ldrh	r0, [r4, r3]
	b	.L1544
.L1569:
	movw	r5, #3660
	ldrh	r0, [r4, r5]
	cmp	r0, #0
	bne	.L1572
	movw	r3, #2620
	ldrh	r7, [r4, r3]
	add	r3, r7, r7, lsl #1
	asr	r3, r3, #2
	strh	r3, [r8]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #72]
	lsl	r0, r0, #1
	ldrh	r1, [r4, #232]
	ldrh	r2, [r3, r0]
	ldr	r3, .L1660+20
	ldrh	r3, [r3]
	mul	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1573
	ldrh	r3, [r4, #224]
	sub	r7, r7, #1
	cmp	r3, r7
	blt	.L1573
	bl	FtlReadRefresh
	ldrh	r0, [r4, r5]
	b	.L1544
.L1573:
	cmp	r2, #0
	bne	.L1572
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #224]
	add	r0, r0, #1
	b	.L1544
.L1585:
	mov	r0, r10
	bl	IsBlkInGcList
	add	r3, r9, #1
	cmp	r0, #0
	uxth	r3, r3
	beq	.L1586
.L1657:
	strh	r3, [r7]	@ movhi
	b	.L1584
.L1586:
	ldr	r2, .L1660+24
	uxth	r8, r8
	ldrh	ip, [r4, #232]
	ldr	r1, [r4, #72]
	lsl	r8, r8, #1
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r2]
	ldrh	r0, [r1, r8]
	mul	r2, ip, r2
	add	ip, r2, r2, lsr #31
	cmp	r0, ip, asr #1
	bgt	.L1588
	cmp	r0, #8
	cmphi	r3, #48
	bls	.L1589
	ldr	r3, .L1660+28
	ldrh	r3, [r3]
	cmp	r3, #35
	bhi	.L1589
.L1588:
	mov	r3, #0
	strh	r3, [r7]	@ movhi
.L1589:
	ldrh	r3, [r1, r8]
	movw	r1, #65535
	cmp	r2, r3
	cmple	r5, r1
	bne	.L1590
	ldrh	r2, [r7]
	cmp	r2, #3
	bhi	.L1590
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
	mov	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L1659
.L1590:
	cmp	r3, #0
	bne	.L1591
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r7]
	add	r3, r3, #1
	b	.L1657
.L1591:
	mov	r3, #0
	strb	r3, [r4, #180]
	ldrh	r3, [r4, #24]
	cmp	r3, r10
	bne	.L1592
	ldr	r1, .L1660+32
	movw	r2, #717
	ldr	r0, .L1660+36
	bl	sftl_printk
.L1592:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #76]
	cmp	r2, r3
	bne	.L1593
	ldr	r1, .L1660+32
	movw	r2, #718
	ldr	r0, .L1660+36
	bl	sftl_printk
.L1593:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #124]
	cmp	r2, r3
	bne	.L1594
	ldr	r1, .L1660+32
	movw	r2, #719
	ldr	r0, .L1660+36
	bl	sftl_printk
.L1594:
	ldr	r0, .L1660+40
	bl	make_superblock
	ldr	r1, .L1660+44
	ldrh	r2, [r4, #172]
	mov	r3, #0
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r2, [r1, #2]	@ movhi
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
.L1583:
	mov	r3, #1
	str	r3, [r4, #3272]
	ldr	r3, .L1660+24
	ldrh	r3, [r3]
	str	r3, [fp, #-64]
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	beq	.L1595
	ldr	r2, [fp, #-64]
	ldrh	r3, [r4, #232]
	ldr	r1, [r4, #72]
	mul	r3, r2, r3
	ldrh	r2, [r4, #172]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	sub	r3, r3, r2
	cmp	r3, #0
	add	r2, r3, #3
	movge	r2, r3
	add	r6, r6, r2, asr #2
	uxth	r6, r6
.L1595:
	ldrh	r3, [r4, #174]
	ldr	r1, [fp, #-64]
	add	r2, r3, r6
	cmp	r2, r1
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r6, r3
	mov	r3, #0
.L1658:
	str	r3, [fp, #-68]
	ldrh	r8, [r4, #174]
	ldrh	r3, [fp, #-68]
	cmp	r6, r3
	bls	.L1605
	ldr	r3, [fp, #-68]
	movw	r9, #65535
	ldrh	ip, [r4, #232]
	mov	r10, #20
	add	r8, r8, r3
	ldr	r0, [r4, #3184]
	mov	r3, #0
	ldr	r1, .L1660+48
	mov	r7, r3
	b	.L1606
.L1599:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1598
	mla	lr, r10, r7, r0
	add	r7, r7, #1
	orr	r2, r8, r2, lsl #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L1598:
	add	r3, r3, #1
.L1606:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1599
	mov	r10, #0
	ldrb	r2, [r4, #180]	@ zero_extendqisi2
	mov	r1, r7
	bl	FlashReadPages
.L1600:
	uxth	r3, r10
	cmp	r7, r3
	ldrls	r3, [fp, #-68]
	addls	r3, r3, #1
	bls	.L1658
.L1604:
	mov	r8, #20
	ldr	r3, [r4, #3184]
	mul	r8, r8, r10
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1601
	ldr	r9, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L1601
	ldr	r3, [r9, #8]
	cmn	r3, #1
	bne	.L1602
	ldr	r1, .L1660+32
	movw	r2, #753
	ldr	r0, .L1660+36
	str	r3, [fp, #-76]
	bl	sftl_printk
	ldr	r3, [fp, #-76]
.L1602:
	sub	r1, fp, #56
	mov	r0, r3
	mov	r2, #0
	bl	log2phys
	ldr	r1, [r4, #3184]
	ldr	r3, [fp, #-56]
	add	r1, r1, r8
	ldr	r2, [r1, #4]
	cmp	r2, r3
	bne	.L1601
	ldr	r2, .L1660+44
	ldr	r0, [r4, #3168]
	ldr	r1, [r1, #16]
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r3, [r4, #3292]
	mov	r2, #20
	mla	r3, r2, r0, r3
	str	r3, [fp, #-76]
	str	r1, [r3, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [fp, #-76]
	ldr	r1, [r4, #3184]
	mov	r2, #20
	str	r0, [r3, #4]
	add	r1, r1, r8
	ldr	r0, [r4, #3168]
	ldr	r3, [r4, #3292]
	mla	r3, r2, r0, r3
	ldr	r2, [r1, #8]
	str	r2, [r3, #8]
	ldr	r2, [r1, #12]
	mov	r1, #1
	str	r2, [r3, #12]
	ldr	r3, [fp, #-56]
	str	r3, [r9, #12]
	ldrh	r3, [r4, #124]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #2592]
	ldr	r0, [r4, #3184]
	str	r3, [r9, #4]
	ldr	r3, [r4, #3168]
	add	r0, r0, r8
	add	r3, r3, #1
	str	r3, [r4, #3168]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	ldr	r3, [r4, #3168]
	cmp	r2, r3
	beq	.L1603
	ldrh	r3, [r4, #128]
	cmp	r3, #0
	bne	.L1601
.L1603:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1601
	ldr	r3, .L1660
	mov	r2, #0
	str	r2, [r3, #3272]
	movw	r2, #65535
	str	r2, [r3, #172]
	movw	r2, #3660
	ldrh	r0, [r3, r2]
	b	.L1544
.L1601:
	add	r10, r10, #1
	b	.L1600
.L1605:
	add	r6, r6, r8
	ldr	r3, [fp, #-64]
	uxth	r6, r6
	strh	r6, [r4, #174]	@ movhi
	cmp	r3, r6
	bhi	.L1607
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L1608
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r4, #3272]
	bne	.L1659
.L1608:
	ldr	r3, .L1660+44
	ldrh	r6, [r3]
	cmp	r6, #0
	bne	.L1609
	ldrh	r3, [r4, #172]
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1609
.L1610:
	ldr	r3, [r4, #2552]
	cmp	r6, r3
	bcc	.L1613
.L1615:
	ldrh	r3, [r4, #172]
	mov	r1, #0
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4, #172]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1609:
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
.L1607:
	mov	r3, #0
	str	r3, [r4, #3272]
	ldrh	r3, [r4, #224]
	cmp	r3, #2
	bhi	.L1616
	ldr	r3, .L1660+24
	ldrh	r6, [r3]
	b	.L1617
.L1613:
	sub	r1, fp, #52
	mov	r0, r6
	mov	r2, #0
	bl	log2phys
	ldr	r0, [fp, #-52]
	cmn	r0, #1
	beq	.L1611
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	beq	.L1612
.L1611:
	add	r6, r6, #1
	b	.L1610
.L1612:
	ldr	r3, [r4, #2552]
	cmp	r6, r3
	bcc	.L1609
	b	.L1615
.L1616:
	movw	r2, #3660
	ldrh	r0, [r4, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1544
.L1580:
	ldr	r0, .L1660+40
	bl	make_superblock
	movw	r3, #3662
	ldr	r2, [r4, #72]
	strh	r7, [r4, r3]	@ movhi
	ldrh	r3, [r4, #172]
	strh	r7, [r4, #174]	@ movhi
	strb	r7, [r4, #178]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	ldr	r3, .L1660+52
	strh	r2, [r3]	@ movhi
	b	.L1575
.L1619:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1661:
	.align	2
.L1660:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3152
	.word	.LC124
	.word	.LANCHOR0+3160
	.word	.LANCHOR0+304
	.word	.LANCHOR0+302
	.word	.LANCHOR0+3196
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+172
	.word	.LANCHOR0+3662
	.word	.LANCHOR0+188
	.word	.LANCHOR0+3664
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #60
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r8, r3
	mov	r3, sp
	bic	r3, r3, #8128
	mov	r5, r1
	bic	r3, r3, #63
	cmp	r0, #16
	str	r2, [fp, #-60]
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	bne	.L1663
	ldr	r1, [fp, #-60]
	mov	r2, r8
	add	r0, r5, #256
	bl	FtlVendorPartRead
	mov	r6, r0
.L1662:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1685
	bl	__stack_chk_fail
.L1663:
	ldr	r4, .L1703
	ldr	r2, [fp, #-60]
	ldr	r3, [r4, #340]
	cmp	r3, r2
	cmpcs	r3, r1
	movls	r7, #1
	movhi	r7, #0
	bls	.L1687
	add	r2, r1, r2
	str	r2, [fp, #-64]
	cmp	r3, r2
	bcc	.L1687
	ldr	r3, .L1703+4
	ldr	r6, [r3]
	cmn	r6, #1
	beq	.L1662
	movw	r3, #258
	mov	r0, r5
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [fp, #-64]
	mov	r1, r6
	str	r0, [fp, #-72]
	sub	r0, r3, #1
	mov	r6, r7
	bl	__aeabi_uidiv
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-60]
	rsb	r3, r3, #1
	ldr	r9, [fp, #-72]
	add	r3, r3, r0
	str	r3, [fp, #-68]
	ldr	r3, [r4, #2584]
	str	r0, [fp, #-76]
	add	r3, r3, r2
	ldr	r2, [fp, #-68]
	str	r3, [r4, #2584]
	ldr	r3, [r4, #2556]
	str	r7, [fp, #-56]
	add	r3, r3, r2
	str	r7, [fp, #-84]
	str	r3, [r4, #2556]
	str	r7, [fp, #-80]
.L1665:
	ldr	r3, [fp, #-68]
	cmp	r3, #0
	bne	.L1683
	ldr	r3, .L1703
	movw	r2, #3206
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1684
	ldrh	r3, [r3, #224]
	cmp	r3, #31
	bhi	.L1662
.L1684:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1662
.L1683:
	sub	r1, fp, #52
	mov	r0, r9
	mov	r2, #0
	bl	log2phys
	ldr	r3, [fp, #-52]
	cmn	r3, #1
	bne	.L1700
	ldr	r3, .L1703+8
	mov	r10, #0
	b	.L1666
.L1669:
	mla	r0, r9, r0, r10
	ldr	r1, [fp, #-64]
	cmp	r5, r0
	movls	r2, #1
	movhi	r2, #0
	cmp	r1, r0
	movls	r2, #0
	cmp	r2, #0
	beq	.L1668
	sub	r0, r0, r5
	mov	r2, #512
	mov	r1, #0
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
	ldr	r3, .L1703+8
.L1668:
	add	r10, r10, #1
.L1666:
	ldrh	r0, [r3]
	cmp	r0, r10
	bhi	.L1669
.L1670:
	ldr	r3, [fp, #-68]
	add	r9, r9, #1
	subs	r3, r3, #1
	str	r3, [fp, #-68]
	beq	.L1675
	ldrh	r3, [r4, #232]
	cmp	r7, r3, lsl #2
	bne	.L1665
.L1675:
	cmp	r7, #0
	beq	.L1665
	ldr	r0, [r4, #3288]
	mov	r1, r7
	mov	r2, #0
	mov	r10, #0
	bl	FlashReadPages
	ldr	r3, [fp, #-56]
	lsl	r3, r3, #9
	str	r3, [fp, #-96]
	ldr	r3, [fp, #-80]
	lsl	r3, r3, #9
	str	r3, [fp, #-88]
	ldr	r3, [fp, #-84]
	lsl	r3, r3, #9
	str	r3, [fp, #-92]
.L1682:
	mov	r3, #20
	ldr	r2, [r4, #3288]
	ldr	r0, [fp, #-72]
	mul	r3, r3, r10
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r1, r0
	bne	.L1677
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3312]
	cmp	r1, r2
	bne	.L1678
	str	r3, [fp, #-100]
	mov	r0, r8
	ldrd	r2, [fp, #-92]
	add	r1, r1, r3
.L1702:
	bl	ftl_memcpy
	ldr	r3, [fp, #-100]
.L1678:
	ldr	r1, [r4, #3288]
	add	r2, r1, r3
	ldr	r0, [r2, #12]
	ldr	ip, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	ip, r0
	ldrne	r0, [r4, #2712]
	addne	r0, r0, #1
	strne	r0, [r4, #2712]
	ldr	ip, [r1, r3]
	cmn	ip, #1
	moveq	r6, ip
	ldreq	r0, [r4, #2712]
	addeq	r0, r0, #1
	streq	r0, [r4, #2712]
	ldr	r3, [r1, r3]
	cmp	r3, #256
	bne	.L1681
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1681:
	add	r10, r10, #1
	cmp	r7, r10
	bne	.L1682
	mov	r7, #0
	b	.L1665
.L1700:
	ldr	r10, [r4, #3288]
	mov	r2, #20
	mla	r10, r2, r7, r10
	str	r3, [r10, #4]
	ldr	r3, [fp, #-72]
	cmp	r9, r3
	ldr	r3, .L1703+8
	bne	.L1671
	ldrh	r3, [r3]
	mov	r0, r5
	ldr	r2, [r4, #3312]
	mov	r1, r3
	str	r2, [r10, #8]
	str	r3, [fp, #-84]
	bl	__aeabi_uidivmod
	ldr	r3, [fp, #-84]
	str	r1, [fp, #-80]
	sub	r2, r3, r1
	ldr	r1, [fp, #-60]
	cmp	r1, r2
	movcc	r2, r1
	str	r2, [fp, #-84]
	cmp	r3, r2
	streq	r8, [r10, #8]
.L1672:
	ldr	r3, .L1703+12
	ldr	r2, [r4, #3336]
	str	r9, [r10, #16]
	ldrh	r3, [r3]
	mul	r3, r7, r3
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r10, #12]
	b	.L1670
.L1671:
	ldrh	r2, [r3]
	ldr	r1, [fp, #-76]
	cmp	r9, r1
	mul	r3, r2, r9
	bne	.L1673
	ldr	r1, [fp, #-64]
	sub	r1, r1, r3
	str	r1, [fp, #-56]
	cmp	r1, r2
	ldrne	r3, [r4, #3316]
	bne	.L1701
.L1673:
	sub	r3, r3, r5
	add	r3, r8, r3, lsl #9
.L1701:
	str	r3, [r10, #8]
	b	.L1672
.L1677:
	ldr	r0, [fp, #-76]
	cmp	r1, r0
	bne	.L1678
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1678
	ldr	r2, .L1703+8
	str	r3, [fp, #-100]
	ldr	r3, [fp, #-76]
	ldrh	r0, [r2]
	ldr	r2, [fp, #-96]
	mul	r0, r3, r0
	sub	r0, r0, r5
	add	r0, r8, r0, lsl #9
	b	.L1702
.L1687:
	mvn	r6, #0
	b	.L1662
.L1685:
	mov	r0, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1704:
	.align	2
.L1703:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+258
	.word	.LANCHOR0+312
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #0
	bl	FtlRead
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_read, .-sftl_read
	.section	.rodata.str1.1
.LC125:
	.ascii	"FtlWrite: ecc error:%x %x %x\012\000"
.LC126:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
	.text
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #84
	push	{lr}
	bl	__gnu_mcount_nc
	str	r3, [fp, #-92]
	mov	r3, sp
	bic	r3, r3, #8128
	mov	r8, r1
	bic	r3, r3, #63
	cmp	r0, #16
	str	r2, [fp, #-88]
	ldr	r3, [r3, #24]
	str	r3, [fp, #-48]
	bne	.L1707
	ldr	r2, [fp, #-92]
	add	r0, r8, #256
	ldr	r1, [fp, #-88]
	bl	FtlVendorPartWrite
.L1706:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1744
	bl	__stack_chk_fail
.L1707:
	ldr	r4, .L1759
	ldr	r2, [fp, #-88]
	ldr	r3, [r4, #340]
	cmp	r3, r2
	cmpcs	r3, r1
	bls	.L1746
	add	r6, r1, r2
	cmp	r3, r6
	bcc	.L1746
	ldr	r5, .L1759+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1706
	mov	r3, #2048
	str	r3, [r4, #3668]
	movw	r3, #258
	mov	r0, r8
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [fp, #-84]
	sub	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [fp, #-84]
	str	r0, [fp, #-108]
	sub	r3, r0, r2
	ldr	r2, [fp, #-88]
	str	r3, [fp, #-104]
	add	r3, r3, #1
	str	r3, [fp, #-80]
	ldr	r3, [r4, #2580]
	add	r3, r3, r2
	ldr	r2, [fp, #-80]
	str	r3, [r4, #2580]
	ldr	r3, [r4, #2564]
	add	r3, r3, r2
	str	r3, [r4, #2564]
	ldr	r3, [r5, #8]
	cmp	r3, #0
	addeq	r4, r4, #24
	beq	.L1709
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	addne	r4, r4, #24
	addeq	r4, r4, #76
.L1709:
	ldr	r6, [fp, #-84]
	ldr	r5, .L1759
.L1710:
	ldr	r3, [fp, #-80]
	cmp	r3, #0
	bne	.L1740
	mov	r0, r3
	ldr	r1, [fp, #-104]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #224]
	cmp	r3, #5
	bls	.L1751
.L1743:
	mov	r0, #0
	b	.L1706
.L1740:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1711
	ldr	r1, .L1759+8
	movw	r2, #1046
	ldr	r0, .L1759+12
	bl	sftl_printk
.L1711:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1712
	ldr	r2, .L1759+16
	ldr	r7, .L1759+4
	cmp	r4, r2
	bne	.L1713
	ldrh	r9, [r5, #80]
	cmp	r9, #0
	bne	.L1714
	add	r0, r4, #52
	bl	allocate_new_data_superblock
	str	r9, [r7, #8]
.L1714:
	ldr	r0, .L1759+16
	bl	allocate_new_data_superblock
	ldr	r3, [r7, #8]
	cmp	r3, #0
	ldrne	r4, .L1759+20
	bne	.L1715
.L1716:
	ldr	r4, .L1759+16
.L1715:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1712
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1712:
	ldrh	r3, [r4, #4]
	ldr	r1, [fp, #-80]
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	cmp	r3, r1
	movcs	r3, r1
	lsl	r2, r2, #2
	cmp	r2, r3
	movcc	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [fp, #-120]
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1717
	ldr	r1, .L1759+8
	movw	r2, #1079
	ldr	r0, .L1759+12
	bl	sftl_printk
.L1717:
	mov	r10, #0
.L1718:
	ldr	r3, [fp, #-120]
	cmp	r3, r10
	bne	.L1738
.L1719:
	mov	r3, r4
	ldr	r0, [r5, #3296]
	mov	r1, r10
	mov	r2, #0
	bl	FtlProgPages
	ldr	r3, [fp, #-80]
	cmp	r3, r10
	bcs	.L1739
	ldr	r1, .L1759+8
	movw	r2, #1157
	ldr	r0, .L1759+12
	bl	sftl_printk
.L1739:
	ldr	r3, [fp, #-80]
	sub	r3, r3, r10
	str	r3, [fp, #-80]
	b	.L1710
.L1713:
	str	r3, [r7, #8]
	ldrh	r3, [r5, #28]
	cmp	r3, #0
	bne	.L1716
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1715
.L1738:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1719
	sub	r1, fp, #72
	mov	r2, #0
	mov	r0, r6
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	mov	r3, #20
	mul	r3, r3, r10
	str	r3, [fp, #-96]
	ldr	r2, [fp, #-96]
	ldr	r3, [r5, #3296]
	add	r3, r3, r2
	ldr	r2, .L1759+24
	str	r0, [r3, #4]
	ldrh	r2, [r2]
	str	r6, [r3, #16]
	mul	r1, r10, r2
	bic	r1, r1, #3
	str	r1, [fp, #-112]
	ldr	r0, [fp, #-112]
	ldr	r1, [r5, #3336]
	str	r1, [fp, #-116]
	add	r9, r1, r0
	mov	r1, #0
	str	r9, [r3, #12]
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [fp, #-84]
	ldr	r2, [fp, #-108]
	cmp	r6, r2
	cmpne	r6, r3
	ldr	r3, .L1759+28
	bne	.L1720
	ldr	r2, [fp, #-84]
	ldrh	r3, [r3]
	cmp	r6, r2
	bne	.L1721
	mov	r1, r3
	mov	r0, r8
	str	r3, [fp, #-124]
	bl	__aeabi_uidivmod
	ldr	r3, [fp, #-124]
	ldr	r2, [fp, #-88]
	sub	r7, r3, r1
	str	r1, [fp, #-100]
	cmp	r7, r2
	movcs	r7, r2
.L1722:
	ldr	r0, [r5, #3296]
	cmp	r3, r7
	ldr	r2, [fp, #-96]
	add	r0, r0, r2
	bne	.L1723
	ldr	r2, [fp, #-84]
	cmp	r6, r2
	mulne	r3, r6, r3
	ldrne	r2, [fp, #-92]
	ldreq	r3, [fp, #-92]
	subne	r3, r3, r8
	addne	r3, r2, r3, lsl #9
	str	r3, [r0, #8]
.L1725:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1735
	ldr	r1, .L1759+8
	movw	r2, #1148
	ldr	r0, .L1759+12
	bl	sftl_printk
.L1735:
	ldr	r2, [fp, #-116]
	add	r10, r10, #1
	ldr	r1, [fp, #-112]
	ldr	r3, .L1759+32
	strh	r3, [r2, r1]	@ movhi
	str	r6, [r9, #8]
	add	r6, r6, #1
	ldr	r3, [r5, #2592]
	str	r3, [r9, #4]
	cmn	r3, #2
	add	r2, r3, #1
	strne	r2, [r5, #2592]
	moveq	r3, #0
	streq	r3, [r5, #2592]
	ldr	r3, [fp, #-72]
	str	r3, [r9, #12]
	ldrh	r3, [r4]
	strh	r3, [r9, #2]	@ movhi
	b	.L1718
.L1721:
	ldr	r2, [fp, #-88]
	add	r7, r8, r2
	smulbb	r2, r6, r3
	sub	r7, r7, r2
	mov	r2, #0
	str	r2, [fp, #-100]
	uxth	r7, r7
	b	.L1722
.L1723:
	ldr	r3, [fp, #-84]
	cmp	r6, r3
	ldreq	r3, [r5, #3312]
	ldrne	r3, [r5, #3316]
	str	r3, [r0, #8]
	ldr	r3, [fp, #-72]
	cmn	r3, #1
	beq	.L1728
	str	r6, [fp, #-52]
	mov	r2, #0
	str	r3, [fp, #-64]
	mov	r1, #1
	ldr	r3, [r0, #8]
	str	r3, [fp, #-60]
	ldr	r3, [r0, #12]
	sub	r0, fp, #68
	str	r3, [fp, #-56]
	bl	FlashReadPages
	ldr	r3, [fp, #-68]
	cmn	r3, #1
	bne	.L1729
	ldr	r2, [r5, #2712]
	ldr	r0, .L1759+36
	add	r2, r2, #1
	str	r2, [r5, #2712]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1733:
	ldr	r3, [fp, #-84]
	lsl	r2, r7, #9
	cmp	r6, r3
	bne	.L1734
	ldr	r1, [fp, #-96]
	ldr	r3, [r5, #3296]
	add	r3, r3, r1
	ldr	r1, [fp, #-92]
	ldr	r0, [r3, #8]
	ldr	r3, [fp, #-100]
	add	r0, r0, r3, lsl #9
.L1758:
	bl	ftl_memcpy
	b	.L1725
.L1729:
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L1733
	ldr	r3, [r5, #2712]
	mov	r2, r6
	ldr	r0, .L1759+40
	add	r3, r3, #1
	str	r3, [r5, #2712]
	ldr	r1, [r9, #8]
	bl	sftl_printk
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L1733
	ldr	r1, .L1759+8
	movw	r2, #1133
	ldr	r0, .L1759+12
	bl	sftl_printk
	b	.L1733
.L1728:
	ldr	r3, .L1759+44
	mov	r1, #0
	ldr	r0, [r0, #8]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1733
.L1734:
	ldr	r3, .L1759+28
	ldr	r0, [fp, #-96]
	ldrh	r1, [r3]
	ldr	r3, [r5, #3296]
	add	r3, r3, r0
	ldr	r0, [fp, #-92]
	mul	r1, r6, r1
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1758
.L1720:
	ldrh	r3, [r3]
	ldr	r1, [fp, #-96]
	ldr	r2, [r5, #3296]
	mul	r3, r6, r3
	add	r2, r2, r1
	ldr	r1, [fp, #-92]
	sub	r3, r3, r8
	add	r3, r1, r3, lsl #9
	str	r3, [r2, #8]
	b	.L1725
.L1751:
	ldr	r6, .L1759+48
	mov	r4, #256
.L1741:
	ldrh	r2, [r5, #222]
	ldrh	r3, [r5, #172]
	and	r3, r3, r2
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1742
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1742:
	ldr	r2, .L1759+52
	mov	r1, #1
	mov	r0, r1
	mov	r3, #128
	strh	r3, [r6]	@ movhi
	strh	r3, [r2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #224]
	cmp	r3, #2
	bhi	.L1743
	subs	r4, r4, #1
	bne	.L1741
	b	.L1743
.L1746:
	mvn	r0, #0
	b	.L1706
.L1744:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1760:
	.align	2
.L1759:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+312
	.word	.LANCHOR0+258
	.word	-3947
	.word	.LC125
	.word	.LC126
	.word	.LANCHOR0+310
	.word	.LANCHOR0+3154
	.word	.LANCHOR0+3152
	.size	FtlWrite, .-FtlWrite
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r1, #1
	mov	r0, r1
	bl	rk_ftl_garbage_collect
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_gc, .-sftl_gc
	.section	.rodata.str1.1
.LC127:
	.ascii	"%s hash error this.id =%x page =%x pre_id =%x hash "
	.ascii	"=%x hash_r =%x\012\000"
	.text
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1790
	mov	r1, #0
	add	r6, r4, #2624
	ldrh	r2, [r4, #240]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	bl	ftl_memset
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1763
.L1775:
	mvn	r0, #0
.L1762:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1763:
	mov	r1, #1
	ldr	r9, .L1790+4
	bl	FtlGetLastWrittenPage
	ldr	r10, .L1790+8
	ldrsh	r7, [r6]
	sxth	r5, r0
	add	r3, r5, #1
	strh	r3, [r6, #2]	@ movhi
.L1765:
	cmp	r5, #0
	bge	.L1772
	ldr	r1, .L1790+12
	movw	r2, #1474
	ldr	r0, .L1790+16
	bl	sftl_printk
	b	.L1771
.L1772:
	orr	r3, r5, r7, lsl #10
	mov	r2, #1
	str	r3, [r4, #3456]
	mov	r1, r2
	ldr	r3, [r4, #3300]
	mov	r0, r9
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	ldr	r3, [r3, #12]
	str	r3, [fp, #-48]
	cmp	r3, #0
	beq	.L1766
	ldr	r2, [r4, #3452]
	cmn	r2, #1
	beq	.L1767
	ldr	r8, .L1790+20
	ldr	r0, [r4, #3460]
	ldrh	r1, [r8]
	bl	js_hash
	ldr	r3, [fp, #-48]
	cmp	r3, r0
	beq	.L1768
	str	r0, [sp, #8]
	mov	r2, r7
	str	r3, [sp, #4]
	ldr	r1, .L1790+12
	ldrh	r3, [r6, #4]
	str	r3, [sp]
	mov	r3, r5
	ldr	r0, .L1790+24
	bl	sftl_printk
	cmp	r5, #0
	bne	.L1769
	ldrh	r3, [r6, #4]
	cmp	r7, r3
	sxthne	r7, r3
	ldrhne	r5, [r8, #-6]
	bne	.L1767
.L1769:
	mvn	r3, #0
	str	r3, [r4, #3452]
.L1767:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1765
.L1766:
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L1767
.L1768:
	ldr	r3, [r4, #3300]
	ldr	r3, [r3]
	cmp	r3, r10
	bne	.L1767
	ldr	r3, [r4, #3332]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1767
.L1771:
	movw	r2, #310
	ldrh	r3, [r4, #240]
	ldrh	r2, [r4, r2]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1774
	ldr	r1, .L1790+12
	movw	r2, #1476
	ldr	r0, .L1790+16
	bl	sftl_printk
.L1774:
	ldr	r5, .L1790+28
	mov	r2, #48
	ldr	r1, [r4, #3460]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r1, [r4, #3460]
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldr	r2, [r4, #2468]
	ldr	r3, .L1790+8
	cmp	r2, r3
	bne	.L1775
	ldrb	r2, [r4, #2478]	@ zero_extendqisi2
	ldrh	r3, [r4, #254]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1775
	movw	r3, #302
	movw	r2, #258
	ldr	r6, [r4, #244]
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, r2]
	ldrh	r1, [r4, #232]
	str	r7, [r4, #3672]
	mul	r3, r7, r3
	str	r3, [r4, #2552]
	mul	r3, r3, r2
	str	r3, [r4, #340]
	ldr	r3, .L1790+32
	ldrh	r0, [r3, #6]
	add	r0, r0, r7
	sub	r0, r6, r0
	bl	__aeabi_uidiv
	movw	r3, #2620
	cmp	r7, r6
	strh	r0, [r4, r3]	@ movhi
	bls	.L1776
	ldr	r1, .L1790+12
	movw	r2, #1498
	ldr	r0, .L1790+16
	bl	sftl_printk
.L1776:
	ldrh	r3, [r5, #16]
	ldrh	r1, [r5, #14]
	strh	r1, [r4, #24]	@ movhi
	lsr	r2, r3, #6
	strh	r2, [r4, #26]	@ movhi
	ldrh	r2, [r5, #18]
	and	r3, r3, #63
	strh	r2, [r4, #76]	@ movhi
	ldrh	r2, [r5, #20]
	strb	r3, [r4, #30]
	ldrb	r3, [r4, #2479]	@ zero_extendqisi2
	lsr	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #82]
	ldrb	r2, [r4, #2480]	@ zero_extendqisi2
	strb	r2, [r4, #84]
	ldrh	r2, [r5, #22]
	strh	r2, [r4, #124]	@ movhi
	ldrh	r2, [r5, #24]
	strh	r0, [r4, #78]	@ movhi
	strb	r3, [r4, #32]
	movw	r3, #65535
	lsr	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #130]
	ldrb	r2, [r4, #2481]	@ zero_extendqisi2
	strb	r2, [r4, #132]
	ldr	r2, [r4, #2500]
	str	r3, [r4, #172]
	mov	r3, #0
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
	strhi	r3, [r4, #2588]
	ldr	r3, [r4, #2504]
	cmp	r3, r2
	strhi	r3, [r4, #2592]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1779
	ldr	r0, .L1790+36
	bl	make_superblock
.L1779:
	ldrh	r2, [r4, #76]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1780
	ldr	r0, .L1790+40
	bl	make_superblock
.L1780:
	ldrh	r2, [r4, #124]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1781
	ldr	r0, .L1790+44
	bl	make_superblock
.L1781:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1782
	ldr	r0, .L1790+48
	bl	make_superblock
.L1782:
	mov	r0, #0
	b	.L1762
.L1791:
	.align	2
.L1790:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	1179929683
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LANCHOR0+310
	.word	.LC127
	.word	.LANCHOR0+2468
	.word	.LANCHOR0+352
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+124
	.word	.LANCHOR0+172
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.section	.rodata.str1.1
.LC128:
	.ascii	"%s last blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC129:
	.ascii	"%s scan blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
	.text
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #44
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, [r0, #12]
	mov	r4, r0
	str	r3, [fp, #-56]
	mov	r1, #0
	ldr	r3, [r0, #16]
	mov	r6, #0
	str	r3, [fp, #-64]
	ldrh	r3, [r0, #6]
	str	r3, [fp, #-48]
	ldrh	r3, [r0, #8]
	ldr	r5, .L1831
	str	r3, [fp, #-60]
	ldr	r3, [fp, #-48]
	ldr	r9, [r0, #24]
	lsl	r2, r3, #2
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [r5, #3300]
	ldr	r8, [r5, #3332]
	str	r3, [r5, #3460]
	mvn	r3, #0
	str	r8, [r5, #3464]
	str	r6, [r4, #32]
	str	r3, [r4]
	mov	r3, #1
	str	r6, [r4, #28]
	str	r3, [r4, #36]
.L1793:
	ldr	r3, [fp, #-60]
	sxth	r7, r6
	uxth	r10, r6
	cmp	r7, r3
	bge	.L1813
	ldr	r2, [fp, #-56]
	lsl	r3, r7, #1
	add	r2, r2, r7, lsl #1
	str	r2, [fp, #-52]
	ldr	r2, [fp, #-60]
	sub	r2, r2, #1
	cmp	r2, r7
	bne	.L1794
	ldr	r3, [fp, #-52]
	mov	r1, #1
	ldr	r5, .L1831
	ldrh	r0, [r3]
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	str	r3, [fp, #-56]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [fp, #-64]
	strh	r10, [r4]	@ movhi
	mov	r10, #0
	ldr	r3, [r3, r7, lsl #2]
	str	r3, [r4, #28]
.L1795:
	ldr	r3, [fp, #-56]
	sxth	r6, r10
	add	r2, r3, #1
	cmp	r6, r2
	blt	.L1799
.L1813:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L1831+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1801
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1801:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1799:
	ldr	r3, [fp, #-52]
	ldr	r0, .L1831+8
	ldrh	r2, [r3]
	orr	r2, r6, r2, lsl #10
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3464]
	ldr	r2, [r2, #12]
	str	r2, [fp, #-60]
	cmp	r2, #0
	beq	.L1796
	ldr	r1, [r5, #3452]
	cmn	r1, #1
	beq	.L1814
	ldr	r1, .L1831+12
	ldr	r0, [r5, #3460]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-60]
	cmp	r2, r0
	beq	.L1798
	mov	r3, r6
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r7
	ldr	r1, .L1831+16
	ldr	r0, .L1831+20
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3452]
.L1814:
	add	r10, r10, #1
	b	.L1795
.L1796:
	ldr	r3, [r5, #3452]
	cmn	r3, #1
	beq	.L1814
.L1798:
	ldrh	r3, [r8, #8]
	ldr	r2, [fp, #-48]
	cmp	r3, r2
	bcs	.L1814
	ldrh	r2, [r4, #4]
	ldrh	r1, [r8]
	cmp	r1, r2
	ldreq	r2, [r5, #3456]
	streq	r2, [r9, r3, lsl #2]
	b	.L1814
.L1794:
	ldr	r2, [r5, #3300]
	str	r2, [r5, #3460]
	ldr	r2, [fp, #-56]
	ldr	r0, .L1831+8
	ldrh	r2, [r2, r3]
	ldr	r3, .L1831+4
	ldrh	r3, [r3]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r5, #3456]
	bl	FlashReadPages
	ldr	r3, [r5, #3452]
	cmn	r3, #1
	beq	.L1817
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1817
	ldrh	r2, [r8, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1818
.L1817:
	mov	r10, #0
	b	.L1809
.L1805:
	ldr	r2, [r5, #3300]
	lsl	lr, ip, #3
	ldr	r7, [fp, #-48]
	add	r1, r1, #1
	ldr	r3, [r2, ip, lsl #3]
	uxth	ip, r3
	cmp	r7, ip
	addhi	r2, r2, lr
	movhi	r3, ip
	ldrhi	r2, [r2, #4]
	strhi	r2, [r9, r3, lsl #2]
.L1803:
	ldrh	r3, [r0]
	sxth	ip, r1
	sub	r3, r3, #1
	cmp	ip, r3
	blt	.L1805
.L1811:
	add	r6, r6, #1
	b	.L1793
.L1818:
	ldr	r0, .L1831+4
	mov	r1, #0
	b	.L1803
.L1810:
	ldr	r2, [fp, #-52]
	ldr	r0, .L1831+8
	str	r3, [fp, #-72]
	ldrh	r2, [r2]
	orr	r2, r3, r2, lsl #10
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3464]
	ldr	r2, [r2, #12]
	str	r2, [fp, #-68]
	cmp	r2, #0
	beq	.L1806
	ldr	r1, [r5, #3452]
	cmn	r1, #1
	beq	.L1815
	ldr	r1, .L1831+12
	ldr	r0, [r5, #3460]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-68]
	cmp	r2, r0
	beq	.L1808
	ldr	r3, [fp, #-72]
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r7
	ldr	r1, .L1831+16
	ldr	r0, .L1831+24
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3452]
.L1815:
	add	r10, r10, #1
.L1809:
	ldr	r2, .L1831+4
	sxth	r3, r10
	ldrh	r2, [r2]
	cmp	r3, r2
	blt	.L1810
	b	.L1811
.L1806:
	ldr	r3, [r5, #3452]
	cmn	r3, #1
	beq	.L1815
.L1808:
	ldrh	r3, [r8, #8]
	ldr	r2, [fp, #-48]
	cmp	r3, r2
	bcs	.L1815
	ldrh	r2, [r4, #4]
	ldrh	r1, [r8]
	cmp	r1, r2
	ldreq	r2, [r5, #3456]
	streq	r2, [r9, r3, lsl #2]
	b	.L1815
.L1832:
	.align	2
.L1831:
	.word	.LANCHOR0
	.word	.LANCHOR0+304
	.word	.LANCHOR0+3452
	.word	.LANCHOR0+310
	.word	.LANCHOR1+615
	.word	.LC128
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L1834
	add	r0, r3, #3600
	add	r2, r3, #320
	add	r0, r0, #8
	ldrh	r2, [r2]
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L1834+4
	strh	r2, [r0, #4]	@ movhi
	add	r2, r3, #344
	ldrh	r2, [r2]
	strh	r2, [r0, #8]	@ movhi
	movw	r2, #322
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #348]
	str	r2, [r3, #3620]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3624]
	ldr	r2, [r3, #3364]
	str	r2, [r3, #3628]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3632]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1835:
	.align	2
.L1834:
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
	push	{lr}
	bl	__gnu_mcount_nc
	bl	FtlL2PDataInit
	ldr	r0, .L1837
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1838:
	.align	2
.L1837:
	.word	.LANCHOR0+3392
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1855
	mov	r3, #0
	add	r7, r4, #3472
	ldrh	r0, [r4, #236]
	strh	r3, [r7]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	r3, r4, #2624
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1840
.L1842:
	mvn	r5, #0
.L1839:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1840:
	bl	FtlLoadSysInfo
	subs	r5, r0, #0
	bne	.L1842
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	movw	r3, #338
	ldr	r2, [r4, #2540]
	mov	r0, #12
	ldrh	r1, [r4, r3]
	mov	r3, r5
	add	r2, r2, #4
.L1843:
	cmp	r3, r1
	bge	.L1848
	mul	ip, r0, r3
	ldr	ip, [r2, ip]
	cmp	ip, #0
	bge	.L1844
.L1848:
	ldr	r6, .L1855+4
	cmp	r3, r1
	ldrh	r2, [r6, #28]
	add	r2, r2, #1
	strh	r2, [r6, #28]	@ movhi
	bge	.L1854
.L1845:
	ldr	r0, .L1855+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1855+12
	bl	FtlSuperblockPowerLostFix
	ldrh	r3, [r4, #24]
	ldr	r1, [r4, #72]
	ldrh	r0, [r4, #28]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r0, #302
	ldrh	r2, [r4, #76]
	ldr	ip, [r4, #72]
	ldrh	r3, [r4, r0]
	lsl	r2, r2, #1
	strh	r3, [r4, #26]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #30]
	strh	r3, [r4, #28]	@ movhi
	ldrh	lr, [r4, #80]
	ldrh	r1, [ip, r2]
	sub	r1, r1, lr
	strh	r1, [ip, r2]	@ movhi
	strb	r3, [r4, #82]
	strh	r3, [r4, #80]	@ movhi
	ldrh	r3, [r6, #30]
	ldrh	r2, [r4, r0]
	add	r3, r3, #1
	strh	r2, [r4, #78]	@ movhi
	strh	r3, [r6, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1849
.L1844:
	add	r3, r3, #1
	b	.L1843
.L1854:
	ldrh	r3, [r7]
	cmp	r3, #0
	bne	.L1845
.L1849:
	ldrh	r0, [r4, #24]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1850
	ldrh	r2, [r4, #28]
	ldrh	r3, [r4, #80]
	orrs	r3, r2, r3
	bne	.L1850
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #76]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1855+8
	bl	allocate_new_data_superblock
	ldr	r0, .L1855+12
	bl	allocate_new_data_superblock
.L1850:
	bl	FtlVpcCheckAndModify
	b	.L1839
.L1856:
	.align	2
.L1855:
	.word	.LANCHOR0
	.word	.LANCHOR0+2468
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r4, .L1881
	mov	r3, #0
	ldrh	r0, [r4, #236]
	str	r3, [r4, #2588]
	str	r3, [r4, #2592]
	str	r3, [r4, #2604]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1858
	bl	FtlMakeBbt
.L1858:
	ldr	r0, .L1881+4
	mov	r2, #0
	ldr	ip, .L1881+8
.L1859:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1860
	ldrh	r6, [r4, #240]
	mov	r5, #0
.L1861:
	ldrh	r3, [r4, #242]
	cmp	r3, r6
	bhi	.L1862
	ldrh	r1, [r4, #232]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1863
.L1867:
	mov	r5, #0
	mov	r6, r5
.L1864:
	ldrh	r3, [r4, #240]
	uxth	r0, r5
	cmp	r3, r0
	bhi	.L1868
	ldrh	r9, [r4, #232]
	movw	r8, #2620
	ldr	r10, [r4, #244]
	ldrh	r3, [r4, #242]
	mov	r1, r9
	mov	r0, r10
	str	r3, [r4, #3284]
	bl	__aeabi_uidiv
	ubfx	r7, r0, #5, #16
	add	r3, r7, #36
	strh	r3, [r4, r8]	@ movhi
	mov	r3, #24
	mov	r5, r0
	str	r0, [r4, #2552]
	mul	r3, r3, r9
	cmp	r6, r3
	ble	.L1869
	mov	r1, r9
	sub	r0, r10, r6
	bl	__aeabi_uidiv
	lsr	r3, r0, #5
	add	r3, r3, #24
	str	r0, [r4, #2552]
	strh	r3, [r4, r8]	@ movhi
.L1869:
	movw	r3, #294
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1871
	movw	r2, #2620
	ldrh	r1, [r4, r2]
	add	r1, r1, r3, lsr #1
	strh	r1, [r4, r2]	@ movhi
	mul	r1, r9, r3
	cmp	r6, r1
	strlt	r5, [r4, #2552]
	addlt	r3, r3, #32
	addlt	r3, r7, r3
	strhlt	r3, [r4, r2]	@ movhi
.L1871:
	movw	r3, #2620
	ldr	r1, [r4, #2552]
	ldr	r6, .L1881+12
	mvn	r5, #0
	ldrh	r3, [r4, r3]
	sub	r1, r1, r3
	movw	r3, #302
	ldrh	r3, [r4, r3]
	mul	r1, r9, r1
	str	r1, [r4, #3672]
	mul	r1, r1, r3
	movw	r3, #258
	str	r1, [r4, #2552]
	ldrh	r3, [r4, r3]
	mul	r1, r1, r3
	str	r1, [r4, #340]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #242]
	mov	r1, #0
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r3, #0
	movw	r2, #65535
	str	r3, [r4, #2548]
	strb	r3, [r4, #178]
	strb	r3, [r4, #180]
	strb	r3, [r4, #30]
	str	r3, [r4, #24]
	mov	r3, #1
	str	r2, [r4, #172]
	strb	r3, [r4, #32]
.L1873:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #24]
	bne	.L1874
	lsl	r3, r3, #1
	ldr	r2, [r4, #72]
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #24]
	add	r3, r3, #1
	strh	r3, [r4, #24]	@ movhi
	b	.L1873
.L1860:
	ldr	lr, [r4, #3312]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #3316]
	str	ip, [r1, r3, lsl #2]
	b	.L1859
.L1862:
	mov	r0, r6
	mov	r1, #1
	add	r6, r6, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r0, r5
	uxth	r6, r6
	uxth	r5, r5
	b	.L1861
.L1863:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #332]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #240]
.L1865:
	ldrh	r3, [r4, #242]
	cmp	r3, r5
	bls	.L1867
	mov	r0, r5
	add	r5, r5, #1
	mov	r1, #1
	uxth	r5, r5
	bl	FtlLowFormatEraseBlock
	b	.L1865
.L1868:
	mov	r1, #0
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r0, r6
	uxth	r6, r6
	b	.L1864
.L1874:
	ldr	r2, [r4, #2588]
	lsl	r3, r3, #1
	str	r2, [r4, #36]
	mvn	r5, #0
	add	r2, r2, #1
	str	r2, [r4, #2588]
	ldr	r2, [r4, #72]
	ldrh	r1, [r4, #28]
	ldr	r6, .L1881+16
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #78]	@ movhi
	strb	r3, [r4, #82]
	ldrh	r3, [r4, #24]
	add	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #84]
.L1875:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #83]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #76]
	bne	.L1876
	lsl	r3, r3, #1
	ldr	r2, [r4, #72]
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #76]
	add	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	b	.L1875
.L1876:
	ldr	r2, [r4, #2588]
	lsl	r3, r3, #1
	str	r2, [r4, #88]
	mvn	r5, #0
	add	r2, r2, #1
	str	r2, [r4, #2588]
	ldr	r2, [r4, #72]
	ldrh	r1, [r4, #80]
	strh	r1, [r2, r3]	@ movhi
	strh	r5, [r4, #124]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L1881+20
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r4, #3672]
	strh	r2, [r3, #6]	@ movhi
	strh	r5, [r3, #4]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, [r4, #2588]
	str	r3, [r4, #2632]
	add	r3, r3, #1
	str	r3, [r4, #2588]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	moveq	r2, #1
	ldreq	r3, .L1881+24
	streq	r2, [r3]
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1882:
	.align	2
.L1881:
	.word	.LANCHOR0
	.word	.LANCHOR0+258
	.word	168778952
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+2624
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L1887
	mvn	r3, #0
	ldr	r4, .L1887+4
	ldr	r1, .L1887+8
	ldr	r0, .L1887+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L1884
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1884
	mov	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #224]
	cmp	r3, #15
	bhi	.L1884
	movw	r4, #8129
.L1885:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1885
.L1884:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1888:
	.align	2
.L1887:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC0
	.word	.LC77
	.size	sftl_init, .-sftl_init
	.section	.rodata.str1.1
.LC130:
	.ascii	"write_idblock %x %x\012\000"
.LC131:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC132:
	.ascii	"%s idb buffer alloc fail\012\000"
	.text
	.align	2
	.global	FtlWriteToIDB
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #116
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r3, sp
	add	r8, r1, r0
	bic	r3, r3, #8128
	sub	r9, r8, #1
	bic	r3, r3, #63
	cmp	r9, #63
	ldr	r5, .L1948
	mov	r4, r0
	movhi	r10, #0
	movls	r10, #1
	ldr	r3, [r3, #24]
	cmp	r0, #576
	orrcs	r10, r10, #1
	mov	r6, r1
	mov	r7, r2
	cmp	r10, #0
	str	r3, [fp, #-48]
	beq	.L1890
	ldr	r3, [r5, #3676]
	cmp	r3, #0
	bne	.L1891
.L1947:
	mov	r4, #0
	b	.L1889
.L1891:
	ldr	r8, [r5, #3680]
	ldr	r3, .L1948+4
	ldr	r2, [r8]
	cmp	r2, r3
	bne	.L1893
	ldrh	r1, [r5, #10]
	add	r0, r8, #260096
	mov	r2, #0
	movw	r3, #65023
.L1897:
	ldr	ip, [r0, #-4]!
	cmp	ip, #0
	bne	.L1894
	ldr	ip, [r8, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	sub	r3, r3, #1
	str	ip, [r0, #2048]
	movhi	r2, #0
	cmp	r3, #4096
	bne	.L1897
	mov	r3, #512
	b	.L1946
.L1894:
	add	r3, r3, #127
	lsr	r3, r3, #7
.L1946:
	str	r3, [fp, #-120]
	lsl	r1, r1, #2
	ldr	r3, [fp, #-120]
	uxth	r1, r1
	add	r0, r3, #4
	bl	__aeabi_uidiv
	add	r3, r0, #1
	mov	r2, r3
	ldr	r1, [fp, #-120]
	ldr	r0, .L1948+8
	str	r3, [fp, #-124]
	bl	sftl_printk
	ldr	r3, [fp, #-120]
	lsl	r3, r3, #7
	str	r3, [fp, #-140]
	mov	r3, #0
	mov	r10, r3
	str	r3, [fp, #-128]
.L1898:
	ldr	r3, [fp, #-124]
	add	r3, r3, r10
	str	r3, [fp, #-132]
	cmp	r3, #8
	bls	.L1920
	ldr	r3, [fp, #-128]
	cmp	r3, #0
	bne	.L1919
.L1893:
	mvn	r3, #0
	str	r3, [fp, #-120]
	b	.L1919
.L1920:
	ldr	r4, .L1948
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [r4, #3684]
	bl	memset
	ldrh	r5, [r4, #10]
	mul	r3, r5, r10
	str	r3, [fp, #-136]
	ldr	r3, [r4, #3260]
	cmp	r3, #0
	moveq	r6, #6
	beq	.L1899
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r6, #6
	movne	r6, #9
.L1899:
	mul	r9, r5, r10
	ldr	r7, .L1948
	mov	r4, #0
.L1900:
	ldr	r3, [r7, #3248]
	mov	r1, r9
	mov	r0, #0
	add	r4, r4, #1
	blx	r3
	ldr	r3, [fp, #-124]
	add	r9, r9, r5
	cmp	r3, r4
	bhi	.L1900
	cmp	r6, #9
	movne	r4, #0
	bne	.L1901
	ldr	r4, [r7, #3684]
	mov	r2, #1024
	mov	r1, #0
	mov	r0, r4
	bl	ftl_memset
	ldr	r3, .L1948+12
	str	r3, [r4]
	mov	r1, #12
	str	r1, [r4, #4]
	mov	r3, #0
	str	r3, [r4, #12]
	mov	r3, #1024
	strh	r3, [r4, #16]	@ movhi
	add	r0, r4, r1
	ldrh	r3, [r7, #10]
	strh	r3, [r4, #18]	@ movhi
	mov	r3, #4096
	str	r3, [r4, #20]
	bl	js_hash
	str	r0, [r4, #8]
.L1901:
	ldr	r3, [fp, #-124]
	mov	r9, r8
	ldr	r7, .L1948
	mul	r3, r5, r3
	mov	r5, #0
	str	r3, [fp, #-144]
.L1902:
	ldr	r3, [fp, #-144]
	cmp	r5, r3
	beq	.L1909
	cmp	r6, #9
	addeq	r3, r5, #1
	lslne	r3, r5, #2
	cmp	r5, #0
	cmpeq	r6, #9
	str	r3, [fp, #-112]
	movw	r3, #61424
	str	r3, [fp, #-108]
	moveq	r0, #1
	movne	r0, #0
	bne	.L1905
	ldr	r3, [r7, #3260]
	mov	r0, #70
	blx	r3
	mov	r2, r4
	ldr	r1, [fp, #-136]
	sub	r3, fp, #112
	ldr	ip, [r7, #3252]
	mov	r0, #0
	blx	ip
	ldr	r3, [r7, #3260]
	str	r0, [fp, #-148]
	ldrb	r0, [r7, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r2, [fp, #-148]
	cmn	r2, #1
	bne	.L1906
.L1909:
	ldr	r3, .L1948
	ldrb	r2, [r3, #14]	@ zero_extendqisi2
	ldr	r1, [r3, #3260]
	str	r2, [fp, #-136]
	ldrh	r2, [r3, #10]
	cmp	r1, #0
	ldr	r9, [r3, #3684]
	moveq	r6, #6
	mul	r7, r2, r10
	beq	.L1908
	ldr	r3, [r3, #3264]
	cmp	r3, #0
	moveq	r6, #6
	movne	r6, #9
.L1908:
	ldr	r3, [fp, #-124]
	mov	r5, #0
	ldr	r4, .L1948
	mul	r3, r2, r3
	str	r3, [fp, #-144]
.L1911:
	ldr	r3, [fp, #-144]
	cmp	r5, r3
	beq	.L1915
	cmp	r5, #0
	cmpeq	r6, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L1912
	ldr	r3, [r4, #3260]
	mov	r0, #70
	blx	r3
	ldr	r3, [r4, #3264]
	mov	r0, #2
	blx	r3
	mov	r2, r9
	ldr	ip, [r4, #3256]
	mov	r1, r7
	sub	r3, fp, #112
	mov	r0, #0
	blx	ip
	ldr	r3, [r4, #3264]
	ldr	r0, [fp, #-136]
	blx	r3
	ldr	r3, [r4, #3260]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r9]
	ldr	r2, .L1948+12
	cmp	r3, r2
	beq	.L1913
.L1915:
	ldr	r3, .L1948
	mov	r4, #0
	ldr	r0, [r3, #3684]
	mov	r3, r8
	mov	r2, r0
.L1914:
	mov	r5, r2
	mov	r6, r3
	add	r2, r2, #4
	add	r3, r3, #4
	ldr	ip, [r5]
	ldr	r1, [r6]
	cmp	ip, r1
	beq	.L1917
	mov	r2, #512
	mov	r1, #0
	bl	memset
	str	r4, [sp]
	ldr	r0, .L1948+16
	mov	r1, r10
	ldr	r3, [r6]
	ldr	r2, [r5]
	bl	sftl_printk
	ldr	r3, .L1948
	mov	r0, #0
	ldrh	r1, [r3, #10]
	ldr	r3, [r3, #3248]
	mul	r1, r10, r1
	blx	r3
.L1918:
	ldr	r10, [fp, #-132]
	b	.L1898
.L1905:
	ldr	r1, [fp, #-136]
	sub	r3, fp, #112
	ldr	ip, [r7, #3252]
	mov	r2, r9
	add	r1, r1, r5
	blx	ip
	cmn	r0, #1
	beq	.L1909
	add	r9, r9, #2048
.L1906:
	add	r5, r5, #1
	b	.L1902
.L1912:
	ldr	ip, [r4, #3256]
	sub	r3, fp, #112
	mov	r2, r9
	add	r1, r7, r5
	blx	ip
	cmn	r0, #1
	beq	.L1915
	ldr	r1, [fp, #-108]
	movw	r3, #61424
	cmp	r1, r3
	bne	.L1915
	add	r9, r9, #2048
.L1913:
	add	r5, r5, #1
	b	.L1911
.L1917:
	ldr	r1, [fp, #-140]
	add	r4, r4, #1
	cmp	r4, r1
	bcc	.L1914
	ldr	r3, [fp, #-128]
	add	r3, r3, #1
	str	r3, [fp, #-128]
	cmp	r3, #5
	bls	.L1918
.L1919:
	ldr	r5, .L1948
	mov	r3, #0
	ldr	r4, [fp, #-120]
	ldr	r0, [r5, #3680]
	str	r3, [r5, #3676]
	bl	kfree
	ldr	r0, [r5, #3684]
	bl	kfree
.L1889:
	mov	r3, sp
	ldr	r2, [fp, #-48]
	bic	r3, r3, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #24]
	cmp	r2, r3
	beq	.L1928
	bl	__stack_chk_fail
.L1890:
	cmp	r0, #64
	bne	.L1921
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3680]
	mov	r0, #262144
	bl	ftl_malloc
	mov	r3, r0
	str	r0, [r5, #3684]
	ldr	r0, [r5, #3680]
	cmp	r0, #0
	cmpne	r3, #0
	beq	.L1922
	mov	r1, r10
	mov	r2, #262144
	mov	r3, #1
	str	r3, [r5, #3676]
	bl	ftl_memset
.L1923:
	cmp	r4, #63
	ldr	r0, [r5, #3680]
	sub	r3, r4, #64
	rsbls	r4, r4, #64
	addls	r6, r6, r3
	addls	r7, r7, r4, lsl #9
	addhi	r0, r0, r3, lsl #9
	cmp	r9, #576
	mov	r1, r7
	rsbcs	r8, r8, #576
	addcs	r6, r6, r8
	lsl	r2, r6, #9
	bl	ftl_memcpy
	b	.L1947
.L1922:
	ldr	r1, .L1948+20
	ldr	r0, .L1948+24
	bl	sftl_printk
.L1921:
	ldr	r3, [r5, #3676]
	cmp	r3, #0
	bne	.L1923
	b	.L1947
.L1928:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1949:
	.align	2
.L1948:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC130
	.word	1179535694
	.word	.LC131
	.word	.LANCHOR1+633
	.word	.LC132
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r5, r1
	mov	r6, r2
	mov	r4, r0
	bl	FtlWriteToIDB
	mov	r3, r6
	mov	r2, r5
	mov	r1, r4
	mov	r0, #0
	bl	FtlWrite
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L1954
	mov	r2, r0
	ldr	r0, [r3, #3688]
	cmp	r0, #0
	streq	r2, [r3, #3688]
	streq	r1, [r3, #3692]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1955:
	.align	2
.L1954:
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r5, .L1965
	mov	r0, #65536
	bl	ftl_malloc
	cmp	r0, #0
	mvneq	r4, #11
	str	r0, [r5, #3696]
	beq	.L1956
	ldr	r9, .L1965+4
	mov	r8, #0
	mov	r7, r8
	mov	r6, r8
.L1960:
	ldr	r3, [r5, #3688]
	lsl	r0, r6, #7
	ldr	r2, [r5, #3696]
	mov	r1, #128
	blx	r3
	subs	r4, r0, #0
	bne	.L1958
	ldr	r3, [r5, #3696]
	ldr	r2, [r3]
	cmp	r2, r9
	bne	.L1959
	add	r1, r3, #61440
	ldr	r2, [r3, #4]
	cmp	r2, r7
	ldr	r1, [r1, #4092]
	sub	r1, r1, r2
	clz	r1, r1
	lsr	r1, r1, #5
	movls	r1, #0
	cmp	r1, #0
	movne	r8, r6
	movne	r7, r2
.L1959:
	cmp	r6, #1
	bne	.L1964
	cmp	r7, #0
	beq	.L1961
	ldr	r4, [r5, #3688]
	mov	r2, r3
	lsl	r0, r8, #7
	mov	r1, #128
	blx	r4
	subs	r4, r0, #0
	beq	.L1956
.L1958:
	ldr	r0, [r5, #3696]
	mvn	r4, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r5, #3696]
	b	.L1956
.L1964:
	mov	r6, #1
	b	.L1960
.L1961:
	mov	r2, #65536
	mov	r1, r7
	mov	r0, r3
	bl	memset
	add	r2, r0, #61440
	str	r6, [r0, #4]
	str	r9, [r0]
	str	r6, [r2, #4092]
	ldr	r2, .L1965+8
	strh	r2, [r0, #14]	@ movhi
.L1956:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1966:
	.align	2
.L1965:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r3, .L1973
	mov	r4, r0
	mov	r0, r1
	ldr	r3, [r3, #3696]
	cmp	r3, #0
	addne	r5, r3, #16
	movne	lr, #0
	ldrhne	r1, [r3, #10]
	bne	.L1969
.L1972:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1970:
	add	lr, lr, #1
.L1969:
	cmp	r1, lr
	bls	.L1972
	lsl	ip, lr, #3
	ldrh	r6, [r5, ip]
	cmp	r6, r4
	bne	.L1970
	add	ip, r3, ip
	ldrh	r4, [ip, #20]
	ldrh	r1, [ip, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, r3, r1
	bl	memcpy
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1974:
	.align	2
.L1973:
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
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r9, .L1994
	mov	r8, r1
	mov	r7, r2
	str	r0, [fp, #-52]
	ldr	r4, [r9, #3696]
	cmp	r4, #0
	beq	.L1990
	add	r3, r7, #63
	ldrh	r2, [r4, #10]
	bic	r3, r3, #63
	str	r3, [fp, #-48]
	ldrh	r3, [r4, #8]
	mov	r6, #0
	str	r3, [fp, #-56]
	add	r3, r4, #16
.L1977:
	cmp	r6, r2
	bcc	.L1985
	ldrh	r1, [r4, #14]
	ldr	r3, [fp, #-48]
	cmp	r1, r3
	bcc	.L1990
	add	r3, r4, r2, lsl #3
	ldr	r2, [fp, #-52]
	strh	r2, [r3, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r0, [r3, #18]	@ movhi
	strh	r7, [r3, #20]	@ movhi
	ldrh	r3, [fp, #-48]
	add	r2, r0, r3
	add	r0, r0, #1024
	sub	r3, r1, r3
	strh	r2, [r4, #12]	@ movhi
	strh	r3, [r4, #14]	@ movhi
	mov	r2, r7
	mov	r1, r8
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
	ldr	r3, [r9, #3692]
	b	.L1993
.L1985:
	lsl	r5, r6, #3
	ldr	r0, [fp, #-52]
	ldrh	r1, [r3, r5]
	cmp	r1, r0
	bne	.L1978
	add	r5, r4, r5
	add	r3, r4, #1024
	ldrh	r1, [r5, #20]
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [fp, #-60]
	cmp	r7, r1
	bls	.L1979
	ldrh	r1, [r4, #14]
	ldr	r0, [fp, #-48]
	cmp	r1, r0
	subcs	r2, r2, #1
	strcs	r2, [fp, #-64]
	ldrhcs	r10, [r5, #18]
	bcs	.L1980
.L1990:
	mvn	r0, #0
	b	.L1975
.L1981:
	ldrh	r2, [r5, #16]
	add	r0, r3, r10
	strh	r2, [r5, #8]	@ movhi
	add	r6, r6, #1
	ldrh	r2, [r5, #20]
	ldrh	r1, [r5, #18]
	strh	r2, [r5, #12]	@ movhi
	add	r2, r2, #63
	strh	r9, [r5, #10]	@ movhi
	bic	r9, r2, #63
	add	r1, r3, r1
	mov	r2, r9
	str	r3, [fp, #-68]
	bl	memcpy
	ldr	r3, [fp, #-68]
	add	r10, r10, r9
.L1980:
	ldr	r2, [fp, #-64]
	uxth	r9, r10
	add	r5, r5, #8
	cmp	r2, r6
	bhi	.L1981
	add	r6, r4, r6, lsl #3
	ldr	r2, [fp, #-52]
	uxtah	r0, r3, r10
	mov	r1, r8
	strh	r2, [r6, #16]	@ movhi
	mov	r2, r7
	strh	r9, [r6, #18]	@ movhi
	strh	r7, [r6, #20]	@ movhi
	bl	memcpy
	ldrh	r2, [fp, #-48]
	ldrh	r3, [r4, #14]
	add	r9, r2, r9
	strh	r9, [r4, #12]	@ movhi
	sub	r3, r3, r2
	ldr	r2, [fp, #-60]
	add	r3, r3, r2
	strh	r3, [r4, #14]	@ movhi
.L1982:
	add	r2, r4, #61440
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
	ldr	r3, .L1994
	ldr	r3, [r3, #3692]
.L1993:
	ldr	r0, [fp, #-56]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1975:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1979:
	ldrh	r0, [r5, #18]
	mov	r2, r7
	mov	r1, r8
	add	r0, r3, r0
	bl	memcpy
	strh	r7, [r5, #20]	@ movhi
	b	.L1982
.L1978:
	add	r6, r6, #1
	b	.L1977
.L1995:
	.align	2
.L1994:
	.word	.LANCHOR0
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.section	.rodata.str1.1
.LC133:
	.ascii	"copy_from_user error %d %p %p\012\000"
	.text
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
	push	{lr}
	bl	__gnu_mcount_nc
	mov	r0, #4096
	mov	r4, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r5, r0, #0
	mvneq	r4, #0
	beq	.L1996
	ldr	r3, .L2014
	cmp	r6, r3
	beq	.L1998
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L1999
.L2012:
	mvn	r4, #13
.L2000:
	mov	r0, r5
	bl	kfree
.L1996:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1998:
	mov	r1, r4
	mov	r2, #8
	bl	_copy_from_user
	cmp	r0, #0
	beq	.L2001
	mov	r2, r4
	mov	r1, #256
.L2013:
	ldr	r0, .L2014+4
	bl	sftl_printk
	b	.L2012
.L2001:
	ldr	r2, [r5]
	ldr	r3, .L2014+8
	cmp	r2, r3
	beq	.L2002
.L2003:
	mvn	r4, #0
	b	.L2000
.L2002:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L2003
	mov	r3, sp
	strh	r0, [r5, #6]	@ movhi
	bic	r1, r3, #8128
	uxth	r2, r0
	bic	r1, r1, #63
	add	r2, r2, #8
	ldr	r3, [r1, #8]
	.syntax divided
@ 177 "./include/linux/uaccess.h" 1
	.syntax unified
adds r0, r4, r2; sbcscc r0, r0, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2012
	.syntax divided
@ 89 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r6, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r6, #12
	orr	r3, r3, #4
	.syntax divided
@ 99 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 102 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_to_user
	mov	r4, r0
	.syntax divided
@ 99 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r6, c3, c0	@ set domain
@ 0 "" 2
@ 102 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	beq	.L2000
	b	.L2012
.L1999:
	mov	r1, r4
	mov	r2, #8
	bl	_copy_from_user
	cmp	r0, #0
	movne	r2, r4
	movne	r1, #276
	bne	.L2013
	ldr	r2, [r5]
	ldr	r3, .L2014+8
	cmp	r2, r3
	bne	.L2003
	ldrh	r2, [r5, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2003
	add	r2, r2, #8
	mov	r1, r4
	mov	r0, r5
	uxth	r2, r2
	bl	_copy_from_user
	cmp	r0, #0
	movne	r2, r4
	movwne	r1, #283
	bne	.L2013
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	mov	r4, r0
	b	.L2000
.L2015:
	.align	2
.L2014:
	.word	1074034177
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
	push	{lr}
	bl	__gnu_mcount_nc
	ldr	r0, .L2017
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L2018:
	.align	2
.L2017:
	.word	.LANCHOR2+12
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
