/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2020-03-30
 */
	.arch armv7-a
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 2
	.eabi_attribute 34, 1
	.eabi_attribute 18, 2
	.file	"rk_sftl.c"
	.syntax divided
	.syntax unified
	.arm
	.syntax unified
	.text
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart.part.0, %function
IsBlkInVendorPart.part.0:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r2, [r3]
	cmp	r2, #0
	beq	.L8
	ldr	r3, [r3, #4]
	ldrh	r1, [r3]
	cmp	r1, r0
	beq	.L7
	sub	r2, r2, #1
	uxth	r2, r2
	add	r2, r3, r2, lsl #1
	b	.L3
.L4:
	ldrh	r1, [r3, #2]!
	cmp	r1, r0
	beq	.L7
.L3:
	cmp	r3, r2
	bne	.L4
.L8:
	mov	r0, #0
	bx	lr
.L7:
	mov	r0, #1
	bx	lr
	.fnend
	.size	IsBlkInVendorPart.part.0, .-IsBlkInVendorPart.part.0
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list.part.1, %function
insert_data_list.part.1:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	r2, r0, #1
	movw	lr, #:lower16:.LANCHOR0
	movt	lr, #:upper16:.LANCHOR0
	add	r7, r2, r0
	ldr	r9, [lr, #8]
	lsl	r7, r7, #1
	mvn	r3, #0
	.pad #12
	sub	sp, sp, #12
	add	r8, r9, r7
	strh	r3, [r8, #2]	@ movhi
	strh	r3, [r9, r7]	@ movhi
	ldr	r1, [lr, #12]
	cmp	r1, #0
	beq	.L23
	ldr	r10, [lr, #16]
	ldrh	ip, [r8, #4]
	ldr	r4, [lr, #8]
	ldrh	r2, [r10, r2]
	cmp	ip, #0
	ldrh	r5, [lr, #20]
	mulne	r3, ip, r2
	movw	ip, #43691
	movt	ip, 43690
	str	r3, [sp, #4]
	sub	r3, r1, r4
	asr	r3, r3, #1
	mul	ip, ip, r3
	uxth	ip, ip
	cmp	r5, #0
	cmpne	r0, ip
	beq	.L10
	mov	r3, #1
	movw	r6, #65535
	b	.L16
.L25:
	ldrh	fp, [r10, fp]
	mul	r2, r2, fp
	ldr	fp, [sp, #4]
	cmp	r2, fp
	bcs	.L14
	ldrh	r2, [r1]
	cmp	r2, r6
	add	fp, r2, r2, lsl #1
	beq	.L24
	cmp	r3, r5
	mov	ip, r2
	movls	r2, #0
	movhi	r2, #1
	cmp	r0, ip
	orreq	r2, r2, #1
	add	r1, r4, fp, lsl #1
	cmp	r2, #0
	bne	.L10
.L16:
	ldrh	r2, [r1, #4]
	add	r3, r3, #1
	lsl	fp, ip, #1
	uxth	r3, r3
	cmp	r2, #0
	bne	.L25
.L14:
	strh	ip, [r9, r7]	@ movhi
	ldrh	r3, [r1, #2]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, [lr, #12]
	cmp	r3, r1
	beq	.L26
	ldrh	r3, [r1, #2]
	ldr	r2, [lr, #8]
	add	r3, r3, r3, lsl #1
	lsl	r3, r3, #1
	strh	r0, [r2, r3]	@ movhi
	strh	r0, [r1, #2]	@ movhi
.L10:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L24:
	strh	ip, [r8, #2]	@ movhi
	strh	r0, [r1]	@ movhi
	str	r8, [lr, #24]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L26:
	strh	r0, [r1, #2]	@ movhi
.L23:
	str	r8, [lr, #12]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	insert_data_list.part.1, .-insert_data_list.part.1
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn.part.5, %function
FtlUpdateVaildLpn.part.5:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r0, #:lower16:.LANCHOR0
	movt	r0, #:upper16:.LANCHOR0
	ldrh	r3, [r0, #20]
	cmp	r3, #0
	bxeq	lr
	str	lr, [sp, #-4]!
	.save {lr}
	sub	r3, r3, #1
	ldr	ip, [r0, #16]
	uxth	r2, r3
	movw	lr, #65535
	sub	r3, ip, #2
	add	ip, ip, r2, lsl #1
.L30:
	ldrh	r1, [r3, #2]!
	cmp	r1, lr
	ldrne	r2, [r0, #28]
	addne	r2, r2, r1
	strne	r2, [r0, #28]
	cmp	ip, r3
	bne	.L30
	ldr	pc, [sp], #4
	.fnend
	.size	FtlUpdateVaildLpn.part.5, .-FtlUpdateVaildLpn.part.5
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	lsr	r1, r0, #5
	movt	r3, #:upper16:.LANCHOR0
	ldr	r2, [r3, #32]
	and	r0, r0, #31
	mov	ip, #1
	ldr	r3, [r2, r1, lsl #2]
	orr	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	bx	lr
	.fnend
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck.part.7, %function
FtlSlcSuperblockCheck.part.7:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r2, #65535
	add	r1, r0, r3, lsl #1
	ldrh	r1, [r1, #16]
	cmp	r1, r2
	bxne	lr
	movw	r2, #:lower16:.LANCHOR0
	str	lr, [sp, #-4]!
	.save {lr}
	movt	r2, #:upper16:.LANCHOR0
	ldrh	lr, [r2, #36]
.L42:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, lr
	mov	r2, r3
	ldrheq	ip, [r0, #2]
	moveq	r3, #0
	moveq	r2, r3
	add	r2, r0, r2, lsl #1
	addeq	ip, ip, #1
	strheq	ip, [r0, #2]	@ movhi
	ldrh	r2, [r2, #16]
	cmp	r2, r1
	beq	.L42
	strb	r3, [r0, #6]
	ldr	pc, [sp], #4
	.fnend
	.size	FtlSlcSuperblockCheck.part.7, .-FtlSlcSuperblockCheck.part.7
	.align	2
	.global	ftl_print_sblk_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r1, #:lower16:.LC0
	movw	r0, #:lower16:.LC1
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movt	r1, #:upper16:.LC0
	movt	r0, #:upper16:.LC1
	.pad #20
	sub	sp, sp, #20
	bl	sftl_printk
	ldrh	r2, [r4, #40]
	movw	r0, #:lower16:.LC2
	ldr	lr, [r4, #16]
	movt	r0, #:upper16:.LC2
	ldrh	r5, [r4, #44]
	mov	r1, r2
	ldrb	ip, [r4, #48]	@ zero_extendqisi2
	lsl	r2, r2, #1
	ldrb	r3, [r4, #46]	@ zero_extendqisi2
	ldrh	lr, [lr, r2]
	ldrh	r2, [r4, #42]
	str	ip, [sp]
	stmib	sp, {r5, lr}
	bl	sftl_printk
	ldrh	r2, [r4, #88]
	movw	r0, #:lower16:.LC3
	ldr	lr, [r4, #16]
	movt	r0, #:upper16:.LC3
	ldrh	r5, [r4, #92]
	mov	r1, r2
	ldrb	ip, [r4, #96]	@ zero_extendqisi2
	lsl	r2, r2, #1
	ldrb	r3, [r4, #94]	@ zero_extendqisi2
	ldrh	lr, [lr, r2]
	ldrh	r2, [r4, #90]
	str	ip, [sp]
	stmib	sp, {r5, lr}
	bl	sftl_printk
	ldrh	r2, [r4, #136]
	movw	r0, #:lower16:.LC4
	ldr	lr, [r4, #16]
	movt	r0, #:upper16:.LC4
	ldrh	r5, [r4, #140]
	mov	r1, r2
	ldrb	ip, [r4, #144]	@ zero_extendqisi2
	lsl	r2, r2, #1
	ldrb	r3, [r4, #142]	@ zero_extendqisi2
	ldrh	lr, [lr, r2]
	ldrh	r2, [r4, #138]
	str	ip, [sp]
	stmib	sp, {r5, lr}
	bl	sftl_printk
	ldrh	r2, [r4, #184]
	movw	r0, #:lower16:.LC5
	ldr	lr, [r4, #16]
	movt	r0, #:upper16:.LC5
	ldrh	r5, [r4, #188]
	mov	r1, r2
	ldrb	ip, [r4, #192]	@ zero_extendqisi2
	lsl	r2, r2, #1
	ldrb	r3, [r4, #190]	@ zero_extendqisi2
	ldrh	lr, [lr, r2]
	ldrh	r2, [r4, #186]
	str	ip, [sp]
	stmib	sp, {r5, lr}
	bl	sftl_printk
	movw	r0, #:lower16:.LC6
	ldrh	r3, [r4, #232]
	ldrh	r2, [r4, #234]
	movt	r0, #:upper16:.LC6
	ldrh	r1, [r4, #236]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, lr}
	b	sftl_printk
	.fnend
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	subs	r1, r0, #0
	beq	.L54
	mov	r2, #1
	mov	r0, #0
	b	.L53
.L55:
	mov	r0, r3
.L53:
	lsl	r2, r2, #1
	add	r3, r0, #1
	uxth	r3, r3
	cmp	r1, r2
	bcs	.L55
	bx	lr
.L54:
	movw	r0, #65535
	bx	lr
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
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
	.align	2
	.global	FtlSysBlkNumInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	str	lr, [sp, #-4]!
	.save {lr}
	movt	r3, #:upper16:.LANCHOR0
	cmp	r0, #24
	movcs	lr, r0
	movcc	lr, #24
	ldrh	r2, [r3, #36]
	mov	r0, #0
	ldrh	ip, [r3, #248]
	ldr	r1, [r3, #256]
	str	lr, [r3, #240]
	mul	r2, lr, r2
	sub	ip, ip, lr
	strh	ip, [r3, #20]	@ movhi
	sub	r1, r1, r2
	str	r2, [r3, #244]
	str	r1, [r3, #252]
	ldr	pc, [sp], #4
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.global	__aeabi_uidiv
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	ldrh	r8, [r0, #14]
	movt	r4, #:upper16:.LANCHOR0
	movw	r3, #262
	add	r1, r4, #260
	ldrh	r5, [r0, #2]
	add	r2, r4, #264
	ldrh	r6, [r0]
	cmp	r8, #4
	ldrh	r9, [r0, #4]
	.pad #12
	sub	sp, sp, #12
	ldrh	r7, [r0, #6]
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #266
	strh	r6, [r1]	@ movhi
	strh	r9, [r2]	@ movhi
	strh	r7, [r4, #248]	@ movhi
	strh	r8, [r4, r3]	@ movhi
	beq	.L78
	cmp	r8, #8
	cmpeq	r9, #1
	beq	.L62
	lsl	ip, r8, #9
	mov	r1, r8
	uxth	ip, ip
	lsr	lr, ip, #8
	b	.L61
.L78:
	mov	r2, #8
	lsr	r7, r7, #1
	mov	r1, r2
	strh	r2, [r4, r3]	@ movhi
	mov	r8, r2
	mov	lr, #16
	mov	ip, #4096
	strh	r7, [r4, #248]	@ movhi
.L61:
	ldr	r2, .L79
	mov	r3, #0
.L63:
	strb	r3, [r2, #1]!
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L63
	ldr	r3, .L79+4
	cmp	r6, #1
	mov	fp, #5
	smulbb	r5, r5, r9
	movw	r2, #302
	strh	fp, [r3]	@ movhi
	mov	r10, #0
	strheq	r6, [r3]	@ movhi
	smulbb	r6, r9, r7
	strh	r10, [r4, r2]	@ movhi
	uxth	r5, r5
	ldr	r2, .L79+8
	movw	r3, #306
	uxth	r6, r6
	mov	r9, #640
	strh	r5, [r4, #36]	@ movhi
	cmp	r6, #0
	strh	r9, [r2]	@ movhi
	strh	r6, [r4, r3]	@ movhi
	beq	.L72
	mov	r9, #0
	mov	r2, #1
	b	.L66
.L73:
	mov	r9, r3
.L66:
	lsl	r2, r2, #1
	add	r3, r9, #1
	uxth	r3, r3
	cmp	r6, r2
	bcs	.L73
.L65:
	ldrh	r6, [r0, #12]
	movw	r10, #310
	ldr	r3, .L79+12
	cmp	r8, #0
	movw	r2, #314
	strh	r6, [r4, r10]	@ movhi
	smulbb	r10, r5, r6
	strh	r9, [r3], #4	@ movhi
	strh	r6, [r3]	@ movhi
	strh	r10, [r4, r2]	@ movhi
	beq	.L74
	mov	r2, #0
	mov	r9, #1
	b	.L68
.L75:
	mov	r2, r3
.L68:
	lsl	r9, r9, #1
	add	r3, r2, #1
	uxth	r3, r3
	cmp	r9, r8
	bls	.L75
	mov	r8, #640
	add	r10, r2, #9
	asr	r8, r8, r2
	add	r8, r8, #2
	uxth	r8, r8
.L67:
	mul	r9, r7, r5
	ldrh	fp, [r0, #20]
	movw	r3, #322
	ldr	r0, .L79+16
	strh	fp, [r4, r3]	@ movhi
	mul	r3, r1, r9
	strh	r2, [r0], #4	@ movhi
	movw	r2, #318
	strh	lr, [r0]	@ movhi
	strh	ip, [r4, r2]	@ movhi
	mul	r1, r1, r6
	str	r9, [r4, #256]
	mov	r0, #5120
	mul	r3, r6, r3
	lsl	r6, r6, #2
	asr	r3, r3, #11
	str	r3, [r4, #324]
	bl	__aeabi_idiv
	lsl	r2, r7, #6
	ldr	r3, .L79+20
	uxth	r0, r0
	movw	ip, #330
	asr	r2, r2, r10
	mov	r1, r5
	strh	r8, [r3]	@ movhi
	cmp	r0, #4
	uxth	r3, r2
	addhi	fp, r0, #3
	strh	r2, [r4, ip]	@ movhi
	movls	r0, #4
	mul	r8, r5, r3
	add	r3, r3, #8
	uxthhi	fp, fp
	movls	fp, #7
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	cmp	r5, #1
	ldr	r2, .L79+24
	add	r8, r8, #3
	movw	r1, #342
	mov	ip, #32
	strh	fp, [r4]	@ movhi
	uxtah	r3, r3, r0
	str	r8, [r4, #332]
	strh	ip, [r4, r1]	@ movhi
	addeq	r3, r3, #4
	uxth	r3, r3
	cmp	r3, #24
	movcc	r3, #24
	mul	r5, r3, r5
	sub	r7, r7, r3
	str	r3, [r4, #240]
	str	r3, [r4, #336]
	mov	r3, #0
	mov	r0, r3
	strh	r7, [r4, #20]	@ movhi
	sub	r9, r9, r5
	str	r5, [r4, #244]
	mul	r6, r9, r6
	str	r9, [r4, #252]
	str	r3, [r4, #344]
	lsr	r10, r6, r10
	add	r10, r10, #2
	strh	r10, [r2]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L62:
	lsr	r7, r7, #1
	mov	r3, #2
	mov	r1, #8
	strh	r3, [r2]	@ movhi
	mov	r9, r3
	mov	r8, r1
	mov	lr, #16
	mov	ip, #4096
	strh	r7, [r4, #248]	@ movhi
	b	.L61
.L74:
	mov	r10, #8
	movw	r2, #65535
	movt	r10, 1
	mov	r8, #2
	b	.L67
.L72:
	movw	r9, #65535
	b	.L65
.L80:
	.align	2
.L79:
	.word	.LANCHOR0+267
	.word	.LANCHOR0+300
	.word	.LANCHOR0+304
	.word	.LANCHOR0+308
	.word	.LANCHOR0+316
	.word	.LANCHOR0+328
	.word	.LANCHOR0+340
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L83
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L82
	b	IsBlkInVendorPart.part.0
.L82:
	mov	r0, r3
	bx	lr
.L84:
	.align	2
.L83:
	.word	.LANCHOR0+348
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	sftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldr	r0, [r3, #344]
	bx	lr
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r4, #:lower16:.LANCHOR0
	movw	r3, #306
	movt	r4, #:upper16:.LANCHOR0
	.pad #12
	sub	sp, sp, #12
	mov	r7, r0
	ldrh	r5, [r4, r3]
	mov	r9, #1
	mov	r1, r5
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r5
	mov	r0, r7
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	lr, [r2, #380]
	lsr	r5, r3, #5
	and	r8, r3, #31
	movw	r0, #:lower16:.LC7
	add	r4, r4, #352
	ldr	ip, [lr, r5, lsl #2]
	mov	r1, r7
	mov	r2, r6
	movt	r0, #:upper16:.LC7
	orr	ip, ip, r9, lsl r8
	str	ip, [lr, r5, lsl #2]
	str	ip, [sp]
	bl	sftl_printk
	ldrh	r3, [r4, #6]
	mov	r0, #0
	add	r3, r3, r9
	strh	r3, [r4, #6]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movw	r3, #306
	movt	r4, #:upper16:.LANCHOR0
	mov	r5, r0
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r5
	uxth	r5, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r5, #5
	add	r4, r4, r0, lsl #2
	and	r5, r5, #31
	ldr	r3, [r4, #380]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r5
	and	r0, r0, #1
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
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
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	ldr	r6, .L100
	mov	r5, r1
	movw	r8, #:lower16:.LANCHOR1
	movw	r7, #:lower16:.LC8
	mov	r0, r1
	mov	r1, #0
	ldrh	r2, [r6], #-106
	movt	r8, #:upper16:.LANCHOR1
	movt	r7, #:upper16:.LC8
	add	r9, r4, #1024
	lsl	r2, r2, #2
	bl	memset
	b	.L95
.L94:
	and	r1, r3, #31
	lsr	r3, r3, #5
	add	r4, r4, #2
	ldr	r2, [r5, r3, lsl #2]
	mov	r0, #1
	cmp	r9, r4
	orr	r2, r2, r0, lsl r1
	str	r2, [r5, r3, lsl #2]
	beq	.L99
.L95:
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r2, [r6]
	cmp	r2, r3
	bhi	.L94
	mov	r2, #74
	mov	r1, r8
	mov	r0, r7
	bl	sftl_printk
	ldrh	r3, [r4]
	b	.L94
.L99:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L101:
	.align	2
.L100:
	.word	.LANCHOR0+412
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r2, #:lower16:.LANCHOR0
	mvn	r3, #0
	movt	r2, #:upper16:.LANCHOR0
	mov	ip, #0
	add	r0, r2, #352
	str	r3, [r2, #364]
	strh	ip, [r0, #6]	@ movhi
	strh	r3, [r0]	@ movhi
	str	r3, [r2, #368]
	str	r3, [r2, #372]
	str	r3, [r2, #376]
	bx	lr
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r8, #:lower16:.LANCHOR0
	movt	r8, #:upper16:.LANCHOR0
	movw	r2, #306
	movw	r3, #262
	mov	r7, #0
	ldrh	r6, [r8, r2]
	ldrh	r9, [r8, r3]
	mul	r9, r6, r9
	cmp	r9, #0
	ble	.L104
	mov	r4, r7
.L106:
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r5, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r5, #5
	add	r0, r8, r0, lsl #2
	and	r5, r5, #31
	add	r4, r4, #1
	ldr	r3, [r0, #380]
	uxth	r4, r4
	add	r1, r7, #1
	ldr	r3, [r3, r2, lsl #2]
	lsr	r5, r3, r5
	tst	r5, #1
	uxthne	r7, r1
	cmp	r4, r9
	blt	.L106
.L104:
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r5, r1
	add	r3, r4, #264
	mov	r7, r0
	ldrh	r6, [r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	movw	r3, #306
	smulbb	r5, r6, r5
	ldrh	r4, [r4, r3]
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	add	r0, r5, r1
	add	r0, r4, r0
	uxth	r0, r0
	pop	{r4, r5, r6, r7, r8, pc}
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movt	r3, #:upper16:.LANCHOR0
	movw	r1, #306
	add	r2, r3, #264
	mov	r5, r0
	ldrh	r4, [r2]
	ldrh	r1, [r3, r1]
	bl	__aeabi_uidiv
	mov	r1, r4
	smulbb	r4, r0, r4
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r4, r4, r1
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r4, #:lower16:.LANCHOR0
	movw	r3, #306
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r1, [r4, r3]
	add	r4, r4, #264
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L120
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L120:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	ip, #:lower16:.LANCHOR0
	push	{r4, lr}
	.save {r4, lr}
	movt	ip, #:upper16:.LANCHOR0
	mov	r4, #0
	add	r3, ip, #416
	mov	r1, r4
	strh	r0, [r3]	@ movhi
	mov	r2, #2048
	add	r0, ip, #424
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	memset
	mov	r0, r4
	pop	{r4, pc}
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L125
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L126:
	.align	2
.L125:
	.word	.LANCHOR0+416
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L128
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L129:
	.align	2
.L128:
	.word	.LANCHOR0+416
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r0, .L142
	ldrh	r3, [r0, #6]
	cmp	r3, #0
	bxeq	lr
	ldr	r3, .L142+4
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r4, [r3, #28]
	ands	r4, r4, #31
	popeq	{r4, pc}
	ldrh	r2, [r0, #2]
	mov	r1, #0
	ldrh	r3, [r0, #4]
.L133:
	add	lr, r0, r2, lsl #1
	add	r1, r1, #1
	uxth	r1, r1
	add	ip, r0, r3, lsl #1
	ldrh	lr, [lr, #8]
	cmp	r4, r1
	add	r2, r2, #1
	add	r3, r3, #1
	ubfx	r2, r2, #0, #10
	strh	lr, [ip, #8]	@ movhi
	ubfx	r3, r3, #0, #10
	bgt	.L133
	strh	r2, [r0, #2]	@ movhi
	strh	r3, [r0, #4]	@ movhi
	pop	{r4, pc}
.L143:
	.align	2
.L142:
	.word	.LANCHOR0+416
	.word	.LANCHOR0+2472
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L157
	ldrh	r1, [ip, #6]
	cmp	r1, #1024
	beq	.L149
	cmp	r1, #0
	beq	.L149
	str	lr, [sp, #-4]!
	.save {lr}
	ldrh	lr, [ip, #2]
	ubfx	r3, lr, #0, #10
	add	r3, ip, r3, lsl #1
	ldrh	r3, [r3, #8]
	cmp	r3, r0
	beq	.L152
	mov	r2, #0
	b	.L147
.L148:
	ldrh	r3, [r3, #8]
	cmp	r3, r0
	beq	.L152
.L147:
	add	r2, r2, #1
	add	r3, r2, lr
	cmp	r2, r1
	ubfx	r3, r3, #0, #10
	add	r3, ip, r3, lsl #1
	bcc	.L148
	mov	r0, #0
	ldr	pc, [sp], #4
.L152:
	mov	r0, #1
	ldr	pc, [sp], #4
.L149:
	mov	r0, #0
	bx	lr
.L158:
	.align	2
.L157:
	.word	.LANCHOR0+416
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r2, [r4, #20]
	cmp	r0, r2
	bcc	.L163
.L160:
	movw	r1, #2520
	ldrh	r3, [r4, r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	strh	r3, [r4, r1]	@ movhi
	popls	{r4, pc}
	movw	r0, #:lower16:.LC8
	mov	r2, #214
	movt	r0, #:upper16:.LC8
	ldr	r1, .L164
	pop	{r4, lr}
	b	sftl_printk
.L163:
	bl	insert_data_list.part.1
	ldrh	r2, [r4, #20]
	b	.L160
.L165:
	.align	2
.L164:
	.word	.LANCHOR1+16
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r3, [r3, #20]
	cmp	r3, r0
	bls	.L169
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_data_list.part.1
	mov	r0, #0
	pop	{r4, pc}
.L169:
	mov	r0, #0
	bx	lr
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	ip, #65535
	cmp	r0, ip
	beq	.L186
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r5, #:lower16:.LANCHOR0
	lsl	r4, r0, #1
	movt	r5, #:upper16:.LANCHOR0
	mvn	r3, #0
	ldr	r7, [r5, #8]
	add	r6, r4, r0
	lsl	r6, r6, #1
	add	r8, r7, r6
	strh	r3, [r8, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r9, [r5, #2524]
	cmp	r9, #0
	beq	.L189
	ldr	r1, [r5, #8]
	movw	r3, #43691
	movt	r3, 43690
	ldr	lr, [r5, #2528]
	sub	r2, r9, r1
	asr	r2, r2, #1
	ldrh	r4, [lr, r4]
	mul	r3, r3, r2
	uxth	r2, r3
	mov	r3, r2
	lsl	r2, r2, #1
	ldrh	r2, [lr, r2]
	cmp	r2, r4
	bcs	.L181
	ldrh	r2, [r9]
	cmp	r2, ip
	bne	.L178
	b	.L191
.L179:
	ldrh	r3, [r1, r3]
	cmp	r3, ip
	beq	.L176
	mov	r2, r3
.L178:
	lsl	r3, r2, #1
	ldrh	r9, [lr, r3]
	add	r3, r3, r2
	lsl	r3, r3, #1
	cmp	r9, r4
	add	r9, r1, r3
	bcc	.L179
.L175:
	ldrh	r3, [r9, #2]
	strh	r3, [r8, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r3, [r5, #2524]
	cmp	r3, r9
	beq	.L192
	ldrh	r3, [r9, #2]
	ldr	r2, [r5, #8]
	add	r3, r3, r3, lsl #1
	lsl	r3, r3, #1
	strh	r0, [r2, r3]	@ movhi
	strh	r0, [r9, #2]	@ movhi
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L191:
	mov	r2, r3
.L176:
	strh	r2, [r8, #2]	@ movhi
	strh	r0, [r9]	@ movhi
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L186:
	mov	r0, #0
	bx	lr
.L192:
	strh	r0, [r9, #2]	@ movhi
.L189:
	str	r8, [r5, #2524]
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L181:
	mov	r2, r3
	b	.L175
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_free_list
	movw	r2, #:lower16:.LANCHOR0
	movt	r2, #:upper16:.LANCHOR0
	ldrh	r3, [r2, #236]
	ldrh	r1, [r2, #20]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r1, r3
	strh	r3, [r2, #236]	@ movhi
	popcs	{r4, pc}
	movw	r0, #:lower16:.LC8
	mov	r2, #207
	movt	r0, #:upper16:.LC8
	ldr	r1, .L196
	pop	{r4, lr}
	b	sftl_printk
.L197:
	.align	2
.L196:
	.word	.LANCHOR1+36
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r6, #:lower16:.LANCHOR0
	add	r1, r1, r1, lsl #1
	movt	r6, #:upper16:.LANCHOR0
	movw	r3, #65535
	ldr	r7, [r6, #8]
	lsl	r4, r1, #1
	mov	r8, r0
	add	r5, r7, r4
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	beq	.L211
.L199:
	ldr	r3, [r8]
	cmp	r5, r3
	beq	.L203
	ldrh	r3, [r7, r4]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L212
	ldr	r2, [r6, #8]
	add	r3, r3, r3, lsl #1
	ldrh	r1, [r5, #2]
	add	r3, r2, r3, lsl #1
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldr	r2, [r6, #8]
	ldrh	r1, [r7, r4]
	add	r3, r3, r3, lsl #1
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
.L205:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L211:
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L203
	movw	r0, #:lower16:.LC8
	mov	r2, #372
	movt	r0, #:upper16:.LC8
	ldr	r1, .L214
	bl	sftl_printk
	b	.L199
.L212:
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	beq	.L205
	add	r2, r2, r2, lsl #1
	ldr	r3, [r6, #8]
	mvn	r1, #0
	lsl	r2, r2, #1
	strh	r1, [r3, r2]	@ movhi
	b	.L205
.L203:
	ldrh	r3, [r7, r4]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L213
	ldr	r2, [r6, #8]
	add	r3, r3, r3, lsl #1
	mvn	r1, #0
	add	r3, r2, r3, lsl #1
	str	r3, [r8]
	strh	r1, [r3, #2]	@ movhi
	b	.L205
.L213:
	mov	r3, #0
	str	r3, [r8]
	b	.L205
.L215:
	.align	2
.L214:
	.word	.LANCHOR1+56
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, [r0]
	cmp	ip, #0
	beq	.L222
	cmp	r1, #0
	push	{r4, lr}
	.save {r4, lr}
	beq	.L232
	ldrh	r3, [ip]
	movw	lr, #65535
	cmp	r3, lr
	beq	.L232
	movw	r2, #:lower16:.LANCHOR0
	movt	r2, #:upper16:.LANCHOR0
	ldr	r2, [r2, #8]
	b	.L221
.L233:
	ldrh	r3, [r2, r3]
	cmp	r3, lr
	beq	.L219
.L221:
	sub	r1, r1, #1
	add	r3, r3, r3, lsl #1
	uxth	r1, r1
	lsl	r3, r3, #1
	cmp	r1, #0
	add	ip, r2, r3
	bne	.L233
.L219:
	sub	r2, ip, r2
	movw	r4, #43691
	asr	r2, r2, #1
	movt	r4, 43690
	mul	r4, r4, r2
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, pc}
.L222:
	movw	r0, #65535
	bx	lr
.L232:
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldr	r2, [r3, #8]
	b	.L219
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	b	List_pop_index_node
	.fnend
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r2, #:lower16:.LANCHOR0
	movt	r2, #:upper16:.LANCHOR0
	ldr	r1, [r2, #12]
	cmp	r1, #0
	beq	.L240
	cmp	r0, #0
	beq	.L237
	ldrh	r3, [r1]
	movw	ip, #65535
	cmp	r3, ip
	beq	.L240
	ldr	r2, [r2, #8]
	b	.L239
.L247:
	ldrh	r3, [r2, r3]
	cmp	r3, ip
	beq	.L240
.L239:
	sub	r0, r0, #1
	add	r3, r3, r3, lsl #1
	uxth	r0, r0
	lsl	r3, r3, #1
	cmp	r0, #0
	add	r1, r2, r3
	bne	.L247
.L238:
	sub	r2, r1, r2
	movw	r3, #43691
	asr	r0, r2, #1
	movt	r3, 43690
	mul	r3, r3, r0
	uxth	r0, r3
	bx	lr
.L240:
	movw	r0, #65535
	bx	lr
.L237:
	ldr	r2, [r2, #8]
	b	.L238
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r3, [r4, #40]
	cmp	r3, r0
	beq	.L250
	ldrh	r3, [r4, #88]
	cmp	r3, r0
	beq	.L250
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	beq	.L250
	ldr	r2, [r4, #8]
	lsl	r3, r0, #1
	ldr	r1, [r4, #12]
	add	r7, r3, r0
	add	r7, r2, r7, lsl #1
	cmp	r7, r1
	beq	.L250
	ldr	r1, [r4, #16]
	mov	r5, r0
	ldrh	r8, [r7, #4]
	ldrh	r6, [r7, #2]
	ldrh	r3, [r1, r3]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, r3
	movw	r3, #65535
	cmp	r6, r3
	beq	.L261
.L253:
	add	r6, r6, r6, lsl #1
	movw	r3, #43691
	movt	r3, 43690
	lsl	r6, r6, #1
	asr	r0, r6, #1
	add	r6, r2, r6
	ldrh	r2, [r6, #4]
	mul	r3, r3, r0
	cmp	r2, #0
	mvneq	r2, #0
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	mulne	r2, r2, r3
	cmp	r8, r2
	bcc	.L262
.L250:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L262:
	mov	r1, r5
	ldr	r0, .L266
	movw	r6, #2520
	bl	List_remove_node
	ldrh	r3, [r4, r6]
	cmp	r3, #0
	beq	.L263
.L257:
	sub	r3, r3, #1
	mov	r0, r5
	movw	r2, #2520
	strh	r3, [r4, r2]	@ movhi
	bl	INSERT_DATA_LIST
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L261:
	ldrh	r3, [r7]
	cmp	r3, r6
	beq	.L264
.L254:
	ldr	r2, [r4, #8]
	ldr	r1, [r4, #16]
	b	.L253
.L263:
	movw	r0, #:lower16:.LC8
	movw	r2, #474
	movt	r0, #:upper16:.LC8
	ldr	r1, .L266+4
	bl	sftl_printk
	ldrh	r3, [r4, r6]
	b	.L257
.L264:
	movw	r0, #:lower16:.LC8
	movw	r2, #463
	movt	r0, #:upper16:.LC8
	ldr	r1, .L266+4
	bl	sftl_printk
	ldrh	r3, [r7, #2]
	cmp	r3, r6
	bne	.L265
	ldrh	r3, [r7]
	cmp	r3, r6
	beq	.L250
	b	.L254
.L265:
	mov	r6, r3
	ldr	r2, [r4, #8]
	ldr	r1, [r4, #16]
	b	.L253
.L267:
	.align	2
.L266:
	.word	.LANCHOR0+12
	.word	.LANCHOR1+76
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r5, #:lower16:.LANCHOR0
	movw	r3, #342
	movt	r5, #:upper16:.LANCHOR0
	ldrh	r4, [r5, r3]
	cmp	r4, #0
	beq	.L269
	ldr	r2, [r5, #2532]
	movw	ip, #65535
	ldrh	r3, [r2]
	cmp	r3, ip
	movne	r3, r2
	movne	r0, #0
	bne	.L272
	b	.L291
.L273:
	ldrh	r1, [r3, #12]!
	cmp	r1, ip
	beq	.L292
.L272:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r4
	bne	.L273
	mov	r1, #0
	mov	r4, r0
	mov	lr, r1
	mov	ip, #-2147483648
.L275:
	ldr	r3, [r2, #4]
	add	r1, r1, #1
	add	r2, r2, #12
	cmp	r3, ip
	movcs	r6, #0
	movcc	r6, #1
	cmp	r3, #0
	movlt	r6, #0
	cmp	r6, #0
	movne	r4, lr
	uxth	lr, r1
	movne	ip, r3
	cmp	r0, lr
	bhi	.L275
	cmp	r4, r0
	bcc	.L282
	movw	r2, #2536
	ldr	r3, [r5, #2532]
	ldrh	r6, [r5, r2]
	mov	r2, #0
	mov	r1, r2
	mov	r4, r0
	mvn	lr, #0
.L277:
	ldr	ip, [r3, #4]
	add	r2, r2, #1
	cmp	ip, lr
	bcs	.L276
	ldrh	r5, [r3]
	cmp	r5, r6
	movne	lr, ip
	movne	r4, r1
.L276:
	uxth	r1, r2
	add	r3, r3, #12
	cmp	r1, r0
	bcc	.L277
	cmp	r4, r0
	bcs	.L269
.L282:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L292:
	pop	{r4, r5, r6, pc}
.L269:
	movw	r0, #:lower16:.LC8
	movw	r2, #789
	movt	r0, #:upper16:.LC8
	ldr	r1, .L293
	bl	sftl_printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L291:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L294:
	.align	2
.L293:
	.word	.LANCHOR1+100
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movw	r2, #2538
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r1, [r3, r2]
	cmp	r1, #4
	cmpls	r0, #0
	beq	.L299
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	str	r1, [r3, #28]
	b	FtlUpdateVaildLpn.part.5
.L299:
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	bx	lr
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r1, #0
	mov	r3, r0
	bne	.L302
	movw	r2, #:lower16:.LANCHOR0
	lsr	r0, r0, #5
	movt	r2, #:upper16:.LANCHOR0
	ldr	r1, [r2, #32]
	and	r3, r3, #31
	mov	ip, #1
	ldr	r2, [r1, r0, lsl #2]
	bic	r3, r2, ip, lsl r3
	str	r3, [r1, r0, lsl #2]
	bx	lr
.L302:
	b	ftl_set_blk_mode.part.6
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	lsr	r2, r0, #5
	movt	r3, #:upper16:.LANCHOR0
	ldr	r3, [r3, #32]
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r1, #0
	movw	r5, #342
	ldr	r2, [r4, #332]
	ldr	r0, [r4, #2540]
	lsl	r2, r2, #1
	bl	memset
	movw	r3, #318
	ldrh	r2, [r4, r5]
	ldrh	r3, [r4, r3]
	mov	r1, #255
	ldr	r0, [r4, #2544]
	mul	r2, r2, r3
	bl	memset
	ldrh	r3, [r4, r5]
	cmp	r3, #0
	beq	.L307
	ldr	r6, .L311
	mov	r2, #0
	ldr	r0, [r4, #2532]
	mov	r5, r2
	mvn	ip, #0
	add	lr, r6, #24
.L306:
	add	r3, r2, r2, lsl #1
	add	r9, r2, #1
	lsl	r3, r3, #2
	add	r1, r0, r3
	str	r5, [r1, #4]
	strh	ip, [r0, r3]	@ movhi
	ldrh	r1, [r6]
	ldrh	r8, [lr]
	ldr	r7, [r4, #2544]
	ldr	r0, [r4, #2532]
	mul	r1, r2, r1
	uxth	r2, r9
	cmp	r8, r2
	add	r3, r0, r3
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r3, #8]
	bhi	.L306
.L307:
	ldr	r3, .L311+4
	movw	r1, #61634
	ldr	r0, [r4, #332]
	movt	r1, 65535
	ldr	lr, [r4, #2596]
	mvn	r2, #0
	sub	ip, r3, #2208
	ldrh	r7, [r3, #44]
	ldrh	r6, [ip]
	movw	r5, #2548
	strh	r0, [r3, #10]	@ movhi
	ldr	ip, [r4, #2600]
	strh	r1, [r3, #4]	@ movhi
	ldr	r0, [r4, #2540]
	ldr	r1, [r4, #2604]
	strh	r7, [r3, #8]	@ movhi
	strh	r6, [r3, #6]	@ movhi
	strh	r2, [r3, #2]	@ movhi
	strh	r2, [r4, r5]	@ movhi
	str	lr, [r4, #2560]
	str	ip, [r4, #2564]
	str	r0, [r4, #2568]
	str	r1, [r4, #2572]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L312:
	.align	2
.L311:
	.word	.LANCHOR0+318
	.word	.LANCHOR0+2548
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r3, #0
	ldrh	ip, [r4, #36]
	strh	r3, [r0, #4]	@ movhi
	cmp	r2, ip
	bcs	.L319
	sub	lr, ip, #1
	add	r5, r2, #8
	sub	lr, lr, r2
	add	r3, r0, r5, lsl #1
	uxtah	lr, r5, lr
	movw	r5, #65535
	sub	r3, r3, #2
	add	lr, r0, lr, lsl #1
.L318:
	ldrh	r2, [r3, #2]!
	cmp	r2, r5
	ldrhne	r2, [r0, #4]
	addne	r2, r2, #1
	strhne	r2, [r0, #4]	@ movhi
	cmp	r3, lr
	bne	.L318
.L319:
	cmp	ip, #0
	beq	.L325
	movw	r3, #310
	sub	ip, ip, #1
	ldrh	r2, [r4, r3]
	uxth	ip, ip
	add	r3, r0, #14
	add	ip, ip, #8
	movw	lr, #65535
	sub	r2, r2, #1
	add	ip, r0, ip, lsl #1
	sub	r1, r2, r1
	uxth	r1, r1
.L321:
	ldrh	r2, [r3, #2]!
	cmp	r2, lr
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	cmp	r3, ip
	bne	.L321
	pop	{r4, r5, pc}
.L325:
	pop	{r4, r5, pc}
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldrh	r2, [r0, #4]
	cmp	r2, #0
	bxeq	lr
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	bxeq	lr
	b	FtlSlcSuperblockCheck.part.7
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r9, #:lower16:.LANCHOR0
	ldrh	r3, [r0]
	.pad #20
	sub	sp, sp, #20
	movt	r9, #:upper16:.LANCHOR0
	str	r0, [sp, #8]
	mov	r2, r3
	str	r3, [sp, #12]
	ldrh	r3, [r9, #20]
	cmp	r3, r2
	bls	.L340
.L332:
	ldrh	r3, [r9, #36]
	mov	r2, #0
	ldr	lr, [sp, #8]
	cmp	r3, r2
	strh	r2, [lr, #4]	@ movhi
	strb	r2, [lr, #7]
	beq	.L337
	ldr	r1, .L341
	sub	r0, r3, #1
	ldrh	r3, [sp, #12]
	movw	ip, #306
	ldrh	r7, [r9, ip]
	add	r6, lr, #16
	add	r10, r1, #3
	ldrh	r8, [r1], #4
	mov	r5, r2
	mov	fp, r10
	smulbb	ip, r3, r8
	uxtah	r3, r1, r0
	str	r3, [sp]
	uxth	r3, ip
	str	r3, [sp, #4]
.L335:
	ldrb	r10, [fp, #1]!	@ zero_extendqisi2
	mov	r1, r8
	add	r6, r6, #2
	mov	r0, r10
	bl	__aeabi_uidiv
	mov	r1, r8
	smulbb	r4, r0, r7
	mov	r0, r10
	bl	__aeabi_uidivmod
	mvn	r3, #0	@ movhi
	add	r4, r4, r1
	strh	r3, [r6, #-2]	@ movhi
	mov	r1, r7
	ldr	r3, [sp, #4]
	add	r4, r3, r4
	uxth	r4, r4
	mov	r0, r4
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r10, r1
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	lr, r10, #5
	add	r0, r9, r0, lsl #2
	and	ip, r10, #31
	add	r1, r5, #1
	ldr	r2, [r0, #380]
	ldr	r2, [r2, lr, lsl #2]
	lsr	r2, r2, ip
	tst	r2, #1
	ldreq	r3, [sp, #8]
	uxtbeq	r5, r1
	strheq	r4, [r6, #-2]	@ movhi
	strbeq	r5, [r3, #7]
	ldr	r3, [sp]
	cmp	r3, fp
	bne	.L335
	uxth	r2, r5
.L333:
	movw	r3, #310
	ldr	lr, [sp, #8]
	ldrh	r1, [r9, r3]
	mov	r0, #0
	ldr	r3, [sp, #12]
	ldr	ip, [r9, #2528]
	strb	r0, [lr, #9]
	movw	r0, #10000
	smulbb	r2, r1, r2
	lsl	r3, r3, #1
	strh	r2, [lr, #4]	@ movhi
	ldrh	r3, [ip, r3]
	cmp	r3, r0
	mov	r0, #0
	movhi	r3, #1
	strbhi	r3, [lr, #9]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L340:
	movw	r0, #:lower16:.LC8
	movw	r2, #2148
	movt	r0, #:upper16:.LC8
	ldr	r1, .L341+4
	bl	sftl_printk
	ldr	r3, [sp, #8]
	ldrh	r3, [r3]
	str	r3, [sp, #12]
	b	.L332
.L337:
	mov	r2, r3
	b	.L333
.L342:
	.align	2
.L341:
	.word	.LANCHOR0+264
	.word	.LANCHOR1+124
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r7, #:lower16:.LANCHOR0
	movt	r7, #:upper16:.LANCHOR0
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	ldrh	r2, [r7, #248]
	ldr	r0, [r7, #8]
	add	r2, r2, r2, lsl #1
	lsl	r2, r2, #1
	bl	memset
	ldrh	r1, [r7, #20]
	mov	r3, #0
	movw	r2, #2520
	str	r3, [r7, #2524]
	str	r3, [r7, #12]
	cmp	r1, r3
	str	r3, [r7, #24]
	strh	r3, [r7, #236]	@ movhi
	strh	r3, [r7, r2]	@ movhi
	beq	.L354
	str	r3, [sp, #20]
	str	r3, [sp, #16]
	str	r3, [sp, #8]
	add	r3, r7, #268
	str	r3, [sp, #12]
.L352:
	ldrh	r9, [r7, #36]
	cmp	r9, #0
	beq	.L348
	ldr	r3, [sp, #12]
	sub	r9, r9, #1
	ldr	fp, .L366
	mov	r8, #0
	uxtah	r3, r3, r9
	add	r2, fp, #42
	ldrh	r6, [fp], #3
	str	r3, [sp, #4]
	ldrh	r3, [r2, #4]
	ldrh	r5, [r2]
	mov	r9, fp
	str	r3, [sp]
	ldrh	r3, [sp, #8]
	smulbb	r10, r3, r6
	uxth	r10, r10
	mov	fp, r10
.L347:
	ldrb	r10, [r9, #1]!	@ zero_extendqisi2
	mov	r1, r6
	mov	r0, r10
	bl	__aeabi_uidiv
	smulbb	r4, r0, r5
	mov	r1, r6
	mov	r0, r10
	bl	__aeabi_uidivmod
	add	r4, r4, r1
	mov	r1, r5
	add	r4, fp, r4
	uxth	r4, r4
	mov	r0, r4
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r4, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r4, #5
	ldr	r3, [sp]
	add	r0, r7, r0, lsl #2
	and	r4, r4, #31
	ldr	r0, [r0, #380]
	add	r1, r3, r8
	ldr	r3, [sp, #4]
	ldr	r2, [r0, r2, lsl #2]
	lsr	r4, r2, r4
	tst	r4, #1
	uxtheq	r8, r1
	cmp	r3, r9
	bne	.L347
	cmp	r8, #0
	beq	.L348
	mov	r1, r8
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #8]
	uxth	r0, r0
	lsl	r1, r3, #1
.L349:
	ldr	lr, [sp, #8]
	ldr	r2, [r7, #8]
	ldrh	ip, [r7, #40]
	add	r3, r1, lr
	add	r3, r2, r3, lsl #1
	cmp	ip, lr
	strh	r0, [r3, #4]	@ movhi
	beq	.L350
	ldrh	r3, [r7, #88]
	cmp	r3, lr
	beq	.L350
	ldrh	r3, [r7, #136]
	cmp	r3, lr
	beq	.L350
	ldr	r3, [r7, #16]
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L351
	ldr	r3, [sp, #20]
	mov	r0, lr
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #20]
	bl	INSERT_FREE_LIST
.L350:
	ldr	r3, [sp, #8]
	add	r2, r3, #1
	ldrh	r3, [r7, #20]
	uxth	r2, r2
	cmp	r3, r2
	str	r2, [sp, #8]
	bhi	.L352
	ldr	r0, [sp, #16]
	movw	r2, #2520
	ldr	ip, [sp, #20]
	strh	r0, [r7, r2]	@ movhi
	add	r1, r0, ip
	strh	ip, [r7, #236]	@ movhi
	cmp	r1, r3
	ble	.L354
	movw	r0, #:lower16:.LC8
	movw	r2, #2210
	movt	r0, #:upper16:.LC8
	ldr	r1, .L366+4
	bl	sftl_printk
.L354:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L348:
	ldr	r2, [sp, #8]
	mov	r0, #0
	ldr	r3, [r7, #16]
	lsl	r1, r2, #1
	mvn	r2, #0
	strh	r2, [r3, r1]	@ movhi
	b	.L349
.L351:
	ldr	r3, [sp, #16]
	ldr	r0, [sp, #8]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #16]
	bl	INSERT_DATA_LIST
	b	.L350
.L367:
	.align	2
.L366:
	.word	.LANCHOR0+264
	.word	.LANCHOR1+140
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r8, #:lower16:.LANCHOR0
	movt	r8, #:upper16:.LANCHOR0
	.pad #20
	sub	sp, sp, #20
	ldrh	fp, [r8, #36]
	str	r0, [sp, #12]
	cmp	fp, #0
	beq	.L369
	add	r5, r8, #264
	movw	r3, #310
	ldrh	r7, [r5], #3
	movw	r1, #306
	ldrh	r3, [r8, r3]
	sub	r2, fp, #1
	add	r10, r8, #268
	ldrh	r6, [r8, r1]
	uxtah	r10, r10, r2
	mov	fp, #0
	smulbb	r9, r0, r7
	str	r3, [sp, #4]
	uxth	r3, r9
	str	r3, [sp, #8]
.L371:
	ldrb	r9, [r5, #1]!	@ zero_extendqisi2
	mov	r1, r7
	mov	r0, r9
	bl	__aeabi_uidiv
	mov	r1, r7
	smulbb	r4, r0, r6
	mov	r0, r9
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	add	r3, r3, r1
	mov	r1, r6
	add	r4, r4, r3
	uxth	r4, r4
	mov	r0, r4
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r3, r4, #5
	ldr	r2, [sp, #4]
	add	r0, r8, r0, lsl #2
	and	r4, r4, #31
	ldr	r0, [r0, #380]
	add	r1, r2, fp
	ldr	r3, [r0, r3, lsl #2]
	lsr	r4, r3, r4
	tst	r4, #1
	uxtheq	fp, r1
	cmp	r10, r5
	bne	.L371
	cmp	fp, #0
	beq	.L369
	mov	r1, fp
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	fp, r0
.L369:
	ldr	r3, [sp, #12]
	mov	r0, #0
	ldr	r1, [r8, #8]
	add	r3, r3, r3, lsl #1
	add	r3, r1, r3, lsl #1
	strh	fp, [r3, #4]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r2, #:lower16:.LANCHOR0
	movt	r2, #:upper16:.LANCHOR0
	ldr	r0, [r2, #2524]
	cmp	r0, #0
	bxeq	lr
	ldr	r1, [r2, #8]
	movw	r3, #43691
	movt	r3, 43690
	ldr	r2, [r2, #2528]
	sub	r0, r0, r1
	asr	r0, r0, #1
	mul	r0, r3, r0
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r0, [r2, r0]
	bx	lr
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L391
	ldrh	r2, [r4, #236]
	movw	ip, #43691
	ldr	r5, [r4, #8]
	movt	ip, 43690
	rsb	r2, r2, r2, lsl #3
	sub	r3, r3, r5
	asr	r3, r3, #1
	asr	r2, r2, #3
	mul	ip, ip, r3
	cmp	r0, r2
	uxthgt	r0, r2
	cmp	r0, #0
	uxth	ip, ip
	beq	.L387
	add	r3, ip, ip, lsl #1
	movw	lr, #65535
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r2, lr
	movne	r1, #0
	bne	.L389
	b	.L387
.L390:
	ldrh	r2, [r5, r3]
	cmp	r2, lr
	beq	.L387
.L389:
	add	r1, r1, #1
	add	r3, r2, r2, lsl #1
	uxth	r1, r1
	mov	ip, r2
	lsl	r3, r3, #1
	cmp	r0, r1
	bne	.L390
.L387:
	ldr	r3, [r4, #2528]
	lsl	ip, ip, #1
	ldrh	r0, [r3, ip]
	pop	{r4, r5, pc}
.L391:
	mov	r0, r3
	pop	{r4, r5, pc}
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LC9
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	r3, #:upper16:.LC9
	mov	r7, r0
	movw	r6, #:lower16:.LANCHOR0
	mov	r4, r7
	ldmia	r3!, {r0, r1, r2}
	movt	r6, #:upper16:.LANCHOR0
	.pad #20
	sub	sp, sp, #20
	add	r8, r6, #352
	ldr	r9, .L409
	ldrb	r3, [r3]	@ zero_extendqisi2
	str	r2, [r7, #8]	@ unaligned
	movw	r2, #:lower16:.LC10
	str	r0, [r7]	@ unaligned
	movt	r2, #:upper16:.LC10
	str	r1, [r7, #4]	@ unaligned
	mov	r1, #64
	strb	r3, [r4, #12]!
	mov	r0, r4
	ldr	r3, [r6, #324]
	bl	snprintf
	movw	r3, #:lower16:.LC11
	add	ip, r4, r0
	movt	r3, #:upper16:.LC11
	mov	lr, r0
	ldmia	r3!, {r0, r1}
	mov	r5, ip
	movw	r2, #:lower16:.LC12
	movt	r2, #:upper16:.LC12
	str	r0, [r4, lr]	@ unaligned
	ldrh	lr, [r3]	@ unaligned
	ldrb	r0, [r3, #2]	@ zero_extendqisi2
	str	r1, [ip, #4]	@ unaligned
	mov	r1, #64
	strh	lr, [ip, #8]	@ unaligned
	ldr	r3, [r6, #2608]
	strb	r0, [r5, #10]!
	mov	r0, r5
	bl	snprintf
	movw	r2, #:lower16:.LC13
	add	r4, r5, r0
	mov	r0, r4
	ldr	r3, [r6, #28]
	movt	r2, #:upper16:.LC13
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC14
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2612]
	movt	r2, #:upper16:.LC14
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC15
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2616]
	movt	r2, #:upper16:.LC15
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC16
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2620]
	movt	r2, #:upper16:.LC16
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC17
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2624]
	movt	r2, #:upper16:.LC17
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC18
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2628]
	movt	r2, #:upper16:.LC18
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC19
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2632]
	movt	r2, #:upper16:.LC19
	mov	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2636]
	add	r4, r4, r0
	movw	r2, #:lower16:.LC20
	mov	r0, r4
	movt	r2, #:upper16:.LC20
	mov	r1, #64
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r6, #2640]
	add	r4, r4, r0
	movw	r2, #:lower16:.LC21
	mov	r0, r4
	movt	r2, #:upper16:.LC21
	mov	r1, #64
	lsr	r3, r3, #11
	bl	snprintf
	movw	r2, #:lower16:.LC22
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2644]
	movt	r2, #:upper16:.LC22
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC23
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2648]
	movt	r2, #:upper16:.LC23
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC24
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r8, #6]
	movt	r2, #:upper16:.LC24
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC25
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #236]
	movt	r2, #:upper16:.LC25
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC26
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2652]
	movt	r2, #:upper16:.LC26
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC27
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2656]
	movt	r2, #:upper16:.LC27
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC28
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2660]
	movt	r2, #:upper16:.LC28
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC29
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2664]
	movt	r2, #:upper16:.LC29
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC30
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2668]
	movt	r2, #:upper16:.LC30
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC31
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2672]
	movt	r2, #:upper16:.LC31
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC32
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r9, #30]
	movt	r2, #:upper16:.LC32
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC33
	add	r4, r4, r0
	ldrh	r3, [r9, #28]
	mov	r0, r4
	movt	r2, #:upper16:.LC33
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC34
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #344]
	movt	r2, #:upper16:.LC34
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC35
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #336]
	movt	r2, #:upper16:.LC35
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC36
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #240]
	movt	r2, #:upper16:.LC36
	mov	r1, #64
	bl	snprintf
	add	r3, r6, #416
	add	r4, r4, r0
	movw	r2, #:lower16:.LC37
	mov	r0, r4
	ldrh	r3, [r3, #6]
	movt	r2, #:upper16:.LC37
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC38
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #20]
	movt	r2, #:upper16:.LC38
	mov	r1, #64
	bl	snprintf
	movw	r3, #2676
	add	r4, r4, r0
	movw	r2, #:lower16:.LC39
	mov	r0, r4
	ldrh	r3, [r6, r3]
	movt	r2, #:upper16:.LC39
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC40
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #252]
	movt	r2, #:upper16:.LC40
	mov	r1, #64
	bl	snprintf
	movw	r3, #2680
	add	r4, r4, r0
	movw	r2, #:lower16:.LC41
	mov	r0, r4
	ldrh	r3, [r6, r3]
	movt	r2, #:upper16:.LC41
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC42
	add	r4, r4, r0
	ldrh	r3, [r8]
	mov	r0, r4
	movt	r2, #:upper16:.LC42
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC43
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #42]
	movt	r2, #:upper16:.LC43
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC44
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #46]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC44
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC45
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #40]
	movt	r2, #:upper16:.LC45
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC46
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #48]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC46
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC47
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #44]
	movt	r2, #:upper16:.LC47
	mov	r1, #64
	bl	snprintf
	ldrh	r3, [r6, #40]
	add	r4, r4, r0
	ldr	ip, [r6, #16]
	movw	r2, #:lower16:.LC48
	mov	r0, r4
	movt	r2, #:upper16:.LC48
	mov	r1, #64
	lsl	r3, r3, #1
	ldrh	r3, [ip, r3]
	bl	snprintf
	movw	r2, #:lower16:.LC49
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #90]
	movt	r2, #:upper16:.LC49
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC50
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #94]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC50
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC51
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #88]
	movt	r2, #:upper16:.LC51
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC52
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #96]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC52
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC53
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #92]
	movt	r2, #:upper16:.LC53
	mov	r1, #64
	bl	snprintf
	ldrh	r3, [r6, #88]
	add	r4, r4, r0
	ldr	ip, [r6, #16]
	movw	r2, #:lower16:.LC54
	mov	r0, r4
	movt	r2, #:upper16:.LC54
	mov	r1, #64
	lsl	r3, r3, #1
	ldrh	r3, [ip, r3]
	bl	snprintf
	movw	r2, #:lower16:.LC55
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #138]
	movt	r2, #:upper16:.LC55
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC56
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #142]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC56
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC57
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #136]
	movt	r2, #:upper16:.LC57
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC58
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC58
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC59
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #140]
	movt	r2, #:upper16:.LC59
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC60
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #186]
	movt	r2, #:upper16:.LC60
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC61
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #190]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC61
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC62
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #184]
	movt	r2, #:upper16:.LC62
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC63
	add	r4, r4, r0
	mov	r0, r4
	ldrb	r3, [r6, #192]	@ zero_extendqisi2
	movt	r2, #:upper16:.LC63
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC64
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r6, #188]
	movt	r2, #:upper16:.LC64
	mov	r1, #64
	bl	snprintf
	ldr	ip, [r6, #2772]
	add	r4, r4, r0
	ldr	r1, [r6, #2780]
	movw	r2, #:lower16:.LC65
	ldr	r3, [r6, #2776]
	mov	r0, r4
	str	ip, [sp, #4]
	movt	r2, #:upper16:.LC65
	str	r1, [sp]
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC66
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2768]
	movt	r2, #:upper16:.LC66
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC67
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #2792]
	movt	r2, #:upper16:.LC67
	mov	r1, #64
	bl	snprintf
	movw	r3, #3208
	add	r4, r4, r0
	movw	r2, #:lower16:.LC68
	mov	r0, r4
	ldrh	r3, [r6, r3]
	movt	r2, #:upper16:.LC68
	mov	r1, #64
	bl	snprintf
	movw	r3, #3210
	add	r4, r4, r0
	movw	r2, #:lower16:.LC69
	mov	r0, r4
	ldrh	r3, [r6, r3]
	movt	r2, #:upper16:.LC69
	mov	r1, #64
	bl	snprintf
	movw	r2, #:lower16:.LC70
	add	r4, r4, r0
	mov	r0, r4
	ldr	r3, [r6, #3212]
	movt	r2, #:upper16:.LC70
	mov	r1, #64
	bl	snprintf
	add	r3, r6, #3216
	add	r4, r4, r0
	movw	r2, #:lower16:.LC71
	mov	r0, r4
	ldrh	r3, [r3]
	movt	r2, #:upper16:.LC71
	mov	r1, #64
	bl	snprintf
	ldr	r3, [r6, #2524]
	add	r4, r4, r0
	cmp	r3, #0
	beq	.L394
	ldr	r0, [r6, #8]
	movw	r2, #43691
	movt	r2, 43690
	ldr	r1, [r6, #2528]
	sub	r3, r3, r0
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r3, r3
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
.L394:
	movw	r2, #:lower16:.LC72
	mov	r1, #64
	movt	r2, #:upper16:.LC72
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r6, #236]
	bl	GetFreeBlockMaxEraseCount
	movw	r2, #:lower16:.LC73
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	movt	r2, #:upper16:.LC73
	bl	snprintf
	movw	r3, #:lower16:.LANCHOR2
	add	r4, r4, r0
	movt	r3, #:upper16:.LANCHOR2
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L408
.L402:
	sub	r0, r4, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L408:
	ldrh	r3, [r6, #184]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L397
	ldr	r0, [r6, #16]
	lsl	r3, r3, #1
	movw	r2, #:lower16:.LC74
	movt	r2, #:upper16:.LC74
	mov	r1, #64
	ldrh	r3, [r0, r3]
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
.L397:
	mov	r0, #0
	movw	r9, #65535
	bl	List_get_gc_head_node
	uxth	ip, r0
	cmp	ip, r9
	beq	.L398
	movw	r10, #:lower16:.LC75
	ldr	r1, [r6, #8]
	movt	r10, #:upper16:.LC75
	mov	r8, #0
	b	.L399
.L401:
	cmp	ip, r9
	beq	.L400
.L399:
	lsl	r0, ip, #1
	ldr	r2, [r6, #2528]
	ldr	lr, [r6, #16]
	mov	r3, r8
	add	r5, r0, ip
	add	r8, r8, #1
	ldrh	r2, [r2, r0]
	lsl	r5, r5, #1
	add	r1, r1, r5
	str	r2, [sp, #12]
	mov	r2, r10
	ldrh	fp, [r1, #4]
	mov	r1, #64
	str	fp, [sp, #8]
	ldrh	lr, [lr, r0]
	mov	r0, r4
	stm	sp, {ip, lr}
	bl	snprintf
	ldr	r1, [r6, #8]
	cmp	r8, #16
	add	r4, r4, r0
	ldrh	ip, [r1, r5]
	bne	.L401
.L400:
	ldr	r3, [r6, #2524]
	movw	r0, #43691
	movt	r0, 43690
	movw	r9, #65535
	sub	r3, r3, r1
	asr	r3, r3, #1
	mul	r0, r0, r3
	uxth	r0, r0
	cmp	r0, r9
	beq	.L402
	movw	r10, #:lower16:.LC76
	mov	r8, #0
	movt	r10, #:upper16:.LC76
.L404:
	lsl	r2, r0, #1
	ldr	ip, [r6, #2528]
	mov	r3, r8
	add	r5, r2, r0
	add	r8, r8, #1
	ldrh	lr, [ip, r2]
	lsl	r5, r5, #1
	mov	r2, r10
	add	ip, r1, r5
	mov	r1, #64
	str	lr, [sp, #8]
	ldrh	ip, [ip, #4]
	str	r0, [sp]
	mov	r0, r4
	str	ip, [sp, #4]
	bl	snprintf
	cmp	r8, #4
	add	r4, r4, r0
	beq	.L402
	ldr	r1, [r6, #8]
	ldrh	r0, [r1, r5]
	cmp	r0, r9
	bne	.L404
	b	.L402
.L398:
	ldr	r1, [r6, #8]
	b	.L400
.L410:
	.align	2
.L409:
	.word	.LANCHOR0+2472
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #2048
	bge	.L418
	mov	r0, #0
	bx	lr
.L418:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #:lower16:.LC0
	mov	r5, r1
	movw	r2, #:lower16:.LC77
	movt	r3, #:upper16:.LC0
	movt	r2, #:upper16:.LC77
	mov	r1, #64
	mov	r0, r5
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	pop	{r4, r5, r6, pc}
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	.pad #36
	sub	sp, sp, #36
	ldr	r6, [r4, #2660]
	ldr	r7, [r4, #2672]
	cmp	r6, r7
	bcs	.L420
	ldrh	r1, [r4, #20]
	mov	r5, #0
	str	r5, [r4, #2652]
	cmp	r1, r5
	beq	.L435
	ldr	r3, [r4, #2528]
	add	r0, r3, r1, lsl #1
.L422:
	ldrh	r2, [r3], #2
	cmp	r0, r3
	add	r5, r5, r2
	str	r5, [r4, #2652]
	bne	.L422
.L421:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, .L457
	mov	r6, r0
	ldr	r0, [r4, #2656]
	str	r6, [r4, #2660]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2652]
.L423:
	add	r3, r7, #256
	cmp	r3, r6
	bls	.L425
.L456:
	ldr	r3, [r4, #2668]
	add	r2, r7, #768
	cmp	r2, r3
	bls	.L425
.L427:
	movw	r5, #65535
.L426:
	mov	r0, r5
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L420:
	ldr	r3, [r4, #2668]
	cmp	r6, r3
	bls	.L423
	ldrh	r2, [r4, #20]
	add	r3, r3, #1
	str	r3, [r4, #2668]
	cmp	r2, #0
	beq	.L423
	mov	r3, #0
.L424:
	ldr	r0, [r4, #2528]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	ldrh	r2, [r4, #20]
	cmp	r2, r3
	bhi	.L424
	ldr	r7, [r4, #2672]
	ldr	r6, [r4, #2660]
	add	r3, r7, #256
	cmp	r3, r6
	bhi	.L456
.L425:
	ldrh	r0, [r4, #236]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r7, #64
	str	r0, [sp, #20]
	cmp	r0, r3
	bcc	.L427
	ldr	ip, [r4, #12]
	cmp	ip, #0
	beq	.L427
	ldrh	r3, [ip]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L427
	ldrh	r9, [r4, #20]
	cmp	r9, #0
	beq	.L427
	ldr	r1, [r4, #2528]
	movw	r8, #43691
	str	r6, [sp, #24]
	mov	r5, r2
	mov	r10, r2
	movt	r8, 43690
	ldr	lr, [r4, #8]
	mov	r0, #1
	str	r1, [sp, #28]
	mov	r6, r1
	b	.L428
.L431:
	cmp	r0, r9
	bhi	.L427
.L428:
	sub	r1, ip, lr
	ldrh	ip, [ip, #4]
	add	r3, r3, r3, lsl #1
	asr	r1, r1, #1
	lsl	r3, r3, #1
	cmp	ip, #0
	add	r0, r0, #1
	mul	r1, r8, r1
	uxth	r0, r0
	add	ip, lr, r3
	beq	.L429
	uxth	fp, r1
	mov	r1, fp
	lsl	fp, fp, #1
	ldrh	fp, [r6, fp]
	cmp	r7, fp
	bcs	.L436
	cmp	fp, r2
	movcc	r2, fp
	movcc	r5, r1
.L429:
	ldrh	r3, [lr, r3]
	cmp	r3, r10
	bne	.L431
	ldr	r6, [sp, #24]
.L430:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L427
	ldr	r8, [sp, #28]
	lsl	ip, r5, #1
	ldrh	r1, [r8, ip]
	cmp	r7, r1
	bcs	.L432
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L432
	sub	r0, r3, lr
	movw	r3, #43691
	asr	r0, r0, #1
	movt	r3, 43690
	mul	r3, r3, r0
	uxth	r3, r3
	lsl	r3, r3, #1
	ldrh	r3, [r8, r3]
	cmp	r7, r3
	strcc	r2, [r4, #2672]
.L432:
	cmp	r1, r6
	bcs	.L427
	ldr	r2, [sp, #20]
	add	r3, r1, #128
	cmp	r2, r3
	ble	.L427
	add	r3, r1, #256
	cmp	r3, r6
	ldr	r3, [r4, #2668]
	bcc	.L434
	add	r1, r1, #768
	cmp	r1, r3
	bcs	.L427
.L434:
	ldr	r2, [sp, #20]
	movw	r0, #:lower16:.LC78
	ldr	lr, [r4, #16]
	movt	r0, #:upper16:.LC78
	str	r2, [sp, #8]
	ldr	r2, [sp, #28]
	ldrh	r1, [r2, ip]
	mov	r2, r6
	str	r1, [sp, #4]
	mov	r1, r5
	ldrh	ip, [lr, ip]
	str	ip, [sp]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3220]
	b	.L426
.L435:
	mov	r5, r1
	b	.L421
.L436:
	ldr	r6, [sp, #24]
	mov	r5, r1
	b	.L430
.L458:
	.align	2
.L457:
	.word	.LANCHOR0+300
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	cmp	r0, r2
	beq	.L462
	movw	r2, #:lower16:.LANCHOR0
	push	{r4, lr}
	.save {r4, lr}
	movt	r2, #:upper16:.LANCHOR0
	lsl	r3, r0, #1
	mov	r1, #0
	ldr	r2, [r2, #16]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
	mov	r0, #0
	pop	{r4, pc}
.L462:
	mov	r0, #0
	bx	lr
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #65535
	ldrh	r2, [r0]
	mov	r4, r0
	cmp	r2, r3
	beq	.L485
.L466:
	movw	r6, #:lower16:.LANCHOR0
	movw	r3, #310
	movt	r6, #:upper16:.LANCHOR0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	beq	.L486
.L467:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L487
.L468:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	mov	r2, #0
	strb	r2, [r4, #10]
	movw	r2, #65535
	add	r1, r4, r3, lsl #1
	ldrh	r0, [r1, #16]
	cmp	r0, r2
	ldrhne	r1, [r4, #2]
	bne	.L470
	ldrh	lr, [r6, #36]
	mov	ip, r0
	ldrh	r1, [r4, #2]
.L473:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, lr
	mov	r2, r3
	moveq	r3, #0
	addeq	r1, r1, #1
	moveq	r2, r3
	uxtheq	r1, r1
	add	r2, r4, r2, lsl #1
	strheq	r1, [r4, #2]	@ movhi
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L473
	strb	r3, [r4, #6]
.L470:
	movw	r3, #310
	ldrh	r7, [r6, r3]
	cmp	r7, r1
	movwls	r5, #65535
	bhi	.L488
.L465:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L488:
	ldrh	lr, [r4, #4]
	orr	r5, r1, r0, lsl #10
	ldrh	ip, [r6, #36]
	movw	r0, #65535
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	sub	lr, lr, #1
	uxth	lr, lr
	strh	lr, [r4, #4]	@ movhi
.L477:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, ip
	mov	r2, r3
	moveq	r3, #0
	addeq	r1, r1, #1
	moveq	r2, r3
	uxtheq	r1, r1
	add	r2, r4, r2, lsl #1
	strheq	r1, [r4, #2]	@ movhi
	ldrh	r2, [r2, #16]
	cmp	r2, r0
	beq	.L477
	adds	lr, lr, #0
	strb	r3, [r4, #6]
	movne	lr, #1
	cmp	r7, r1
	movne	lr, #0
	cmp	lr, #0
	beq	.L465
	movw	r0, #:lower16:.LC8
	movw	r2, #2806
	movt	r0, #:upper16:.LC8
	ldr	r1, .L489
	bl	sftl_printk
	b	.L465
.L487:
	movw	r0, #:lower16:.LC8
	movw	r2, #2783
	movt	r0, #:upper16:.LC8
	ldr	r1, .L489
	bl	sftl_printk
	b	.L468
.L485:
	movw	r0, #:lower16:.LC8
	movw	r2, #2781
	movt	r0, #:upper16:.LC8
	ldr	r1, .L489
	bl	sftl_printk
	b	.L466
.L486:
	movw	r0, #:lower16:.LC8
	movw	r2, #2782
	movt	r0, #:upper16:.LC8
	ldr	r1, .L489
	bl	sftl_printk
	b	.L467
.L490:
	.align	2
.L489:
	.word	.LANCHOR1+160
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #:lower16:.LANCHOR0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movt	r2, #:upper16:.LANCHOR0
	mov	ip, #0
	ldrh	lr, [r2, #36]
	str	ip, [r2, #3224]
	cmp	lr, ip
	beq	.L492
	ldr	r4, [r2, #3228]
	add	r0, r2, #320
	ldr	r1, .L502
	mov	r7, #1
.L493:
	ldrh	r3, [r1]
	add	r5, ip, ip, lsl #1
	ldr	r9, [r2, #3232]
	add	r6, ip, ip, lsl #2
	lsl	lr, r5, #2
	add	r8, ip, #1
	mul	r3, ip, r3
	add	r10, r4, lr
	str	r7, [r10, #8]
	cmp	r3, #0
	add	r10, r3, #3
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, r9, r3
	str	r3, [r4, r5, lsl #2]
	ldrh	r3, [r0]
	ldr	r10, [r2, #3240]
	ldr	r4, [r2, #3228]
	ldr	r9, [r2, #3236]
	mul	r3, ip, r3
	uxth	ip, r8
	add	r6, r10, r6, lsl #2
	add	r8, r4, lr
	ldrh	lr, [r2, #36]
	cmp	r3, #0
	add	r10, r3, #3
	movlt	r3, r10
	cmp	lr, ip
	bic	r3, r3, #3
	add	r3, r9, r3
	str	r3, [r8, #4]
	ldr	r3, [r4, r5, lsl #2]
	str	r3, [r6, #8]
	ldr	r3, [r8, #4]
	str	r3, [r6, #12]
	bhi	.L493
.L492:
	ldr	r3, [r2, #3244]
	cmp	r3, lr
	popls	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldr	r1, .L502
	mov	r4, #0
	ldr	ip, [r2, #3228]
	add	r0, r1, #2
.L495:
	ldrh	r3, [r1]
	add	r7, lr, lr, lsl #1
	ldr	r8, [r2, #3232]
	add	r6, lr, #1
	lsl	r5, r7, #2
	mul	r3, lr, r3
	add	r9, ip, r5
	str	r4, [r9, #8]
	cmp	r3, #0
	add	r9, r3, #3
	movlt	r3, r9
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [ip, r7, lsl #2]
	ldrh	r3, [r0]
	ldr	r8, [r2, #3244]
	ldr	r7, [r2, #3236]
	ldr	ip, [r2, #3228]
	mul	r3, lr, r3
	uxth	lr, r6
	add	r5, ip, r5
	cmp	r3, #0
	add	r6, r3, #3
	movlt	r3, r6
	cmp	lr, r8
	bic	r3, r3, #3
	add	r3, r7, r3
	str	r3, [r5, #4]
	bcc	.L495
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L503:
	.align	2
.L502:
	.word	.LANCHOR0+318
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r5, #0
	mvn	r3, #0
	add	lr, r4, #3248
	ldrh	r2, [r4]
	add	ip, r4, #348
	mov	r1, r5
	ldr	r0, [r4, #4]
	strh	r3, [lr]	@ movhi
	str	r3, [r4, #3260]
	lsl	r2, r2, #1
	strh	r5, [ip]	@ movhi
	str	r5, [r4, #3252]
	str	r5, [r4, #3256]
	bl	memset
	ldrh	r2, [r4, #248]
	mov	r1, r5
	ldr	r0, [r4, #2528]
	lsl	r2, r2, #1
	bl	memset
	ldrh	r2, [r4, #248]
	mov	r1, r5
	ldr	r0, [r4, #3264]
	lsl	r2, r2, #1
	bl	memset
	add	r0, r4, #2464
	mov	r1, r5
	mov	r2, #48
	add	r0, r0, #8
	bl	memset
	add	r0, r4, #2688
	mov	r1, r5
	mov	r2, #512
	add	r0, r0, #8
	bl	memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #0
	bxeq	lr
	movw	r2, #:lower16:.LANCHOR0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movt	r2, #:upper16:.LANCHOR0
	mov	r3, #0
	mov	r6, r3
	mov	r7, r3
	ldr	r5, [r2, #3244]
	ldr	ip, [r2, #3228]
.L508:
	cmp	r5, #0
	beq	.L510
	add	r3, r3, r3, lsl #2
	ldr	r2, [ip]
	add	r3, r0, r3, lsl #2
	ldr	r4, [r3, #8]
	cmp	r2, r4
	beq	.L514
	mov	r3, #0
	b	.L509
.L511:
	ldr	lr, [ip, r2, lsl #2]
	add	r2, ip, r2, lsl #2
	cmp	lr, r4
	beq	.L512
.L509:
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, r5
	add	r2, r3, r3, lsl #1
	bcc	.L511
.L510:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r1
	mov	r3, r6
	bcc	.L508
	pop	{r4, r5, r6, r7, pc}
.L514:
	mov	r2, ip
.L512:
	str	r7, [r2, #8]
	b	.L510
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #0
	bxeq	lr
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movt	r3, #:upper16:.LANCHOR0
	mov	r6, #0
	mov	r5, r6
	mov	r7, #1
	ldr	lr, [r3, #3244]
	ldr	r4, [r3, #3228]
.L528:
	cmp	lr, #0
	beq	.L530
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L534
	mov	r3, #0
	b	.L529
.L531:
	ldr	ip, [r2, #8]
	cmp	ip, #0
	beq	.L532
.L529:
	add	r3, r3, #1
	uxth	r3, r3
	add	r2, r3, r3, lsl #1
	cmp	r3, lr
	add	r2, r4, r2, lsl #2
	bcc	.L531
.L530:
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r5, r1
	mov	r6, r5
	bcc	.L528
	pop	{r4, r5, r6, r7, pc}
.L534:
	mov	r2, r4
.L532:
	add	r6, r6, r6, lsl #2
	ldr	r3, [r2]
	str	r7, [r2, #8]
	add	r6, r0, r6, lsl #2
	str	r3, [r6, #8]
	ldr	r3, [r2, #4]
	str	r3, [r6, #12]
	b	.L530
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movw	r2, #3268
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L553
	ldr	r3, [r3, #3272]
	ldrh	r1, [r3]
	cmp	r1, r0
	beq	.L552
	sub	r2, r2, #1
	uxth	r2, r2
	add	r2, r3, r2, lsl #1
	b	.L548
.L549:
	ldrh	r1, [r3, #2]!
	cmp	r1, r0
	beq	.L552
.L548:
	cmp	r3, r2
	bne	.L549
.L553:
	mov	r0, #0
	bx	lr
.L552:
	mov	r0, #1
	bx	lr
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r3, #306
	mov	r5, r0
	mov	r6, r1
	ubfx	r0, r0, #10, #16
	ldrh	r1, [r4, r3]
	mov	r7, r2
	bl	__aeabi_uidivmod
	add	r3, r4, #264
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	movw	r3, #3268
	uxth	r0, r0
	ldrh	lr, [r4, r3]
	cmp	lr, #0
	moveq	r3, lr
	ldreq	r2, [r4, #3272]
	beq	.L556
	ldr	r2, [r4, #3272]
	ldrh	r3, [r2]
	cmp	r3, r0
	movne	r1, r2
	movne	r3, #0
	bne	.L559
	b	.L557
.L560:
	ldrh	ip, [r1, #2]!
	cmp	ip, r0
	beq	.L557
.L559:
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, lr
	bne	.L560
	lsl	r3, r3, #1
.L556:
	movw	r1, #3268
	strh	r0, [r2, r3]	@ movhi
	ldrh	r3, [r4, r1]
	add	r3, r3, #1
	strh	r3, [r4, r1]	@ movhi
.L557:
	ldr	r2, .L563
	ldr	r1, [r4, #3276]
	ldrh	r3, [r2]
	add	r3, r3, r3, lsl #1
	lsl	r3, r3, #2
	add	r0, r1, r3
	stmib	r0, {r6, r7}
	str	r5, [r1, r3]
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L564:
	.align	2
.L563:
	.word	.LANCHOR0+3280
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r5, #314
	movw	lr, #3268
	mov	r3, #0
	ldrh	r2, [r4, r5]
	add	ip, r4, #3280
	ldr	r0, [r4, #3272]
	mov	r1, #255
	strh	r3, [r4, lr]	@ movhi
	strh	r3, [ip]	@ movhi
	lsl	r2, r2, #1
	bl	memset
	ldrh	r2, [r4, r5]
	mov	r1, #255
	ldr	r0, [r4, #3276]
	add	r2, r2, r2, lsl #1
	lsl	r2, r2, #2
	bl	memset
	pop	{r4, r5, r6, lr}
	b	FtlGcBufInit
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	movw	r0, #:lower16:.LC79
	mov	r1, r4
	movt	r0, #:upper16:.LC79
	bl	sftl_printk
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r2, [r3, #234]
	cmp	r4, r2
	beq	.L568
	ldrh	r1, [r3, #232]
	cmp	r4, r1
	beq	.L568
	movw	r0, #65535
	cmp	r2, r0
	strheq	r4, [r3, #234]	@ movhi
	beq	.L568
	cmp	r1, r0
	strheq	r4, [r3, #232]	@ movhi
.L568:
	mov	r0, #0
	pop	{r4, pc}
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	movw	r3, #306
	mov	r4, r0
	movw	r6, #3282
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidivmod
	add	r3, r5, #264
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r7, r0
	movw	r0, #:lower16:.LC80
	ldrh	r1, [r5, r6]
	mov	r2, r4
	movt	r0, #:upper16:.LC80
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	beq	.L577
	movw	r3, #3284
	ldr	ip, .L580
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L573
	sub	r1, r0, #1
	mov	r3, ip
	uxth	r1, r1
	add	r1, ip, r1, lsl #1
	b	.L575
.L576:
	ldrh	r2, [r3, #2]!
	cmp	r2, r4
	beq	.L573
.L575:
	cmp	r3, r1
	bne	.L576
	cmp	r0, #15
	bhi	.L573
	add	r3, r0, #1
	uxth	r3, r3
.L572:
	lsl	r0, r0, #1
	movw	r2, #3282
	strh	r3, [r5, r2]	@ movhi
	strh	r4, [ip, r0]	@ movhi
.L573:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L577:
	ldr	ip, .L580
	mov	r3, #1
	b	.L572
.L581:
	.align	2
.L580:
	.word	.LANCHOR0+3284
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r4, #:lower16:.LANCHOR0
	movw	r3, #3282
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L583
	ldrh	r1, [r4, #234]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L590
.L583:
	mov	r0, #0
	pop	{r4, pc}
.L590:
	movw	r1, #3318
	ldrh	r2, [r4, r1]
	cmp	r3, r2
	movls	r3, #0
	strhls	r3, [r4, r1]	@ movhi
	movls	r2, r3
	ldr	r3, .L591
	lsl	r2, r2, #1
	movw	r1, #306
	ldrh	r1, [r4, r1]
	ldrh	r0, [r3, r2]
	bl	__aeabi_uidivmod
	ldr	r3, .L591+4
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	FtlGcRefreshBlock
	movw	r2, #3318
	mov	r0, #0
	ldrh	r3, [r4, r2]
	add	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	pop	{r4, pc}
.L592:
	.align	2
.L591:
	.word	.LANCHOR0+3284
	.word	.LANCHOR0+264
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_malloc, %function
ftl_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #193
	movt	r1, 96
	b	__kmalloc
	.fnend
	.size	ftl_malloc, .-ftl_malloc
	.align	2
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r6, #314
	movw	r1, #3208
	mov	ip, #32
	ldrh	r0, [r4, r6]
	movw	r3, #3210
	mov	r2, #128
	movw	lr, #65535
	mov	r5, #0
	strh	ip, [r4, r1]	@ movhi
	strh	r2, [r4, r3]	@ movhi
	movw	ip, #3282
	movw	r1, #3318
	add	r3, r4, #3216
	mvn	r2, #0
	.pad #12
	sub	sp, sp, #12
	lsl	r0, r0, #1
	str	lr, [r4, #3324]
	strh	r5, [r4, ip]	@ movhi
	add	fp, r4, #320
	strh	r5, [r4, r1]	@ movhi
	add	r7, r4, #3392
	strh	r5, [r3]	@ movhi
	add	r10, r4, #328
	strh	r2, [r4, #234]	@ movhi
	movw	r8, #342
	strh	r2, [r4, #232]	@ movhi
	str	r5, [r4, #2644]
	str	r5, [r4, #2648]
	str	r5, [r4, #2632]
	str	r5, [r4, #2620]
	str	r5, [r4, #2616]
	str	r5, [r4, #2624]
	str	r5, [r4, #2628]
	str	r5, [r4, #2612]
	str	r5, [r4, #2652]
	str	r5, [r4, #2656]
	str	r5, [r4, #2664]
	str	r5, [r4, #2668]
	str	r5, [r4, #2672]
	str	r5, [r4, #3320]
	str	r5, [r4, #3220]
	str	r5, [r4, #3328]
	str	r5, [r4, #3212]
	str	r5, [r4, #3332]
	bl	ftl_malloc
	ldrh	r2, [r4, r6]
	str	r0, [r4, #3272]
	add	r2, r2, r2, lsl #1
	lsl	r0, r2, #2
	bl	ftl_malloc
	ldrh	r6, [r4, #36]
	str	r0, [r4, #3276]
	add	r6, r6, r6, lsl #2
	lsl	r9, r6, #4
	lsl	r6, r6, #2
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3336]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3340]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3344]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3348]
	mov	r0, r6
	bl	ftl_malloc
	ldrh	r2, [r4, #36]
	movw	r3, #318
	ldrh	r9, [r4, r3]
	movw	r6, #262
	str	r0, [r4, #3240]
	lsl	r2, r2, #1
	mov	r0, r9
	add	r2, r2, #1
	str	r2, [r4, #3244]
	bl	ftl_malloc
	str	r0, [r4, #3352]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r2, [r4, #3244]
	str	r0, [r4, #3360]
	mul	r0, r2, r9
	bl	ftl_malloc
	str	r0, [r4, #3232]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3364]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r2, [r4, #3244]
	str	r0, [r4, #3368]
	add	r2, r2, r2, lsl #1
	lsl	r0, r2, #2
	bl	ftl_malloc
	str	r0, [r4, #3228]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3372]
	mov	r0, r9
	bl	ftl_malloc
	movw	r2, #266
	str	r0, [r4, #3376]
	ldrh	r0, [r4, r2]
	add	r9, r4, #412
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r1, [fp]
	ldrh	r2, [r4, #36]
	str	r0, [r4, #3380]
	mul	r2, r2, r1
	mov	r0, r2
	str	r2, [sp, #4]
	bl	ftl_malloc
	ldr	r2, [sp, #4]
	str	r0, [r4, #3384]
	lsl	r0, r2, #2
	bl	ftl_malloc
	ldrh	r2, [fp]
	str	r0, [r4, #3388]
	ldr	r0, [r4, #3244]
	mul	r0, r0, r2
	bl	ftl_malloc
	ldrh	r2, [r4, #248]
	str	r0, [r4, #3236]
	lsl	r2, r2, #1
	uxth	r2, r2
	mov	r0, r2
	strh	r2, [r7]	@ movhi
	bl	ftl_malloc
	ldrh	r2, [r7]
	str	r0, [r4, #3264]
	mov	r0, #65024
	movt	r0, 511
	add	r2, r2, #544
	add	r2, r2, #3
	lsr	r2, r2, #9
	and	r0, r0, r2, lsl #9
	strh	r2, [r7]	@ movhi
	bl	ftl_malloc
	ldrh	fp, [r4, #248]
	add	r3, r0, #32
	str	r0, [r4, #3396]
	str	r3, [r4, #2528]
	lsl	fp, fp, #1
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #3400]
	mov	r0, fp
	bl	ftl_malloc
	ldr	fp, [r4, #332]
	str	r0, [r4, #16]
	lsl	fp, fp, #1
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #2596]
	mov	r0, fp
	bl	ftl_malloc
	ldrh	r3, [r4, #248]
	str	r0, [r4, #2540]
	lsr	r0, r3, #3
	add	r0, r0, #4
	bl	ftl_malloc
	ldrh	r3, [r4]
	str	r0, [r4, #32]
	lsl	r0, r3, #1
	bl	ftl_malloc
	ldrh	r3, [r4]
	str	r0, [r4, #4]
	lsl	r0, r3, #1
	bl	ftl_malloc
	ldrh	r3, [r4]
	str	r0, [r4, #3404]
	lsl	r0, r3, #2
	bl	ftl_malloc
	ldrh	r3, [r10]
	str	r0, [r4, #3408]
	lsl	r0, r3, #2
	bl	ftl_malloc
	ldrh	r2, [r10]
	mov	r1, r5
	str	r0, [r4, #3412]
	lsl	r2, r2, #2
	bl	memset
	add	r3, r4, #340
	ldrh	r0, [r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldr	r3, [r4, #332]
	str	r0, [r4, #2604]
	lsl	r0, r3, #2
	bl	ftl_malloc
	ldrh	r3, [r4, r8]
	str	r0, [r4, #2600]
	add	r3, r3, r3, lsl #1
	lsl	r0, r3, #2
	bl	ftl_malloc
	movw	r2, #318
	ldrh	r3, [r4, r8]
	ldrh	r2, [r4, r2]
	str	r0, [r4, #2532]
	mul	r0, r2, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #248]
	str	r0, [r4, #2544]
	add	r3, r3, r3, lsl #1
	lsl	r0, r3, #1
	bl	ftl_malloc
	movw	r3, #306
	ldrh	r2, [r4, r6]
	ldrh	r3, [r4, r3]
	str	r0, [r4, #8]
	add	r3, r3, #31
	asr	r3, r3, #5
	mul	r0, r3, r2
	strh	r3, [r9]	@ movhi
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, r6]
	str	r0, [r4, #380]
	cmp	r3, #1
	bls	.L603
	ldrh	lr, [r9]
	add	ip, r4, #352
	add	r6, r3, #7
	add	r1, r4, #384
	add	r5, ip, r6, lsl #2
	lsl	lr, lr, #2
	mov	r2, lr
	b	.L597
.L704:
	ldr	r0, [r4, #380]
.L597:
	add	r0, r0, r2
	add	r2, r2, lr
	str	r0, [r1], #4
	cmp	r5, r1
	bne	.L704
	cmp	r3, #7
	bhi	.L601
.L595:
	add	r2, ip, r6, lsl #2
	mov	r1, #0
	sub	r2, r2, #4
.L600:
	add	r3, r3, #1
	str	r1, [r2, #4]!
	cmp	r3, #8
	bne	.L600
.L601:
	ldr	r3, [r4, #2596]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2604]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2600]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2532]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2544]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #380]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #16]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3276]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3344]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3240]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3352]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3356]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3232]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3368]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3228]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3236]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #2528]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #4]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3404]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3408]
	cmp	r3, #0
	beq	.L598
	ldr	r3, [r4, #3412]
	cmp	r3, #0
	beq	.L598
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L598:
	movw	r0, #:lower16:.LC81
	ldr	r1, .L705
	movt	r0, #:upper16:.LC81
	bl	sftl_printk
	mvn	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L603:
	mov	r3, #1
	add	ip, r4, #352
	mov	r6, #8
	b	.L595
.L706:
	.align	2
.L705:
	.word	.LANCHOR1+180
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	ftl_free
	.syntax unified
	.arm
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
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	subs	r8, r3, #0
	.pad #12
	sub	sp, sp, #12
	beq	.L716
	mov	r5, #0
	movw	r10, #:lower16:.LC83
	movw	r9, #:lower16:.LC86
	movw	fp, #:lower16:.LC85
	mov	r6, r2
	mov	r7, r1
	str	r0, [sp, #4]
	mov	r4, r5
	movt	r10, #:upper16:.LC83
	movt	r9, #:upper16:.LC86
	movt	fp, #:upper16:.LC85
	b	.L717
.L715:
	add	r4, r4, #1
	cmp	r4, r8
	bcs	.L716
.L717:
	cmp	r5, #0
	addne	r5, r5, #1
	beq	.L710
	cmp	r6, #4
	beq	.L723
.L712:
	cmp	r6, #2
	lsleq	r3, r4, #1
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	movne	r0, r9
	ldrheq	r1, [r7, r3]
	bl	sftl_printk
.L713:
	cmp	r5, #15
	bls	.L715
	movw	r0, #:lower16:.LC82
	add	r4, r4, #1
	movt	r0, #:upper16:.LC82
	bl	sftl_printk
	cmp	r8, r4
	bls	.L716
.L710:
	mul	r3, r4, r6
	mov	r2, r7
	ldr	r1, [sp, #4]
	mov	r0, r10
	mov	r5, #1
	bl	sftl_printk
	cmp	r6, #4
	bne	.L712
.L723:
	movw	r0, #:lower16:.LC84
	ldr	r1, [r7, r4, lsl #2]
	movt	r0, #:upper16:.LC84
	bl	sftl_printk
	b	.L713
.L716:
	movw	r0, #:lower16:.LC82
	movt	r0, #:upper16:.LC82
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	subs	r10, r1, #0
	ldr	r6, .L769
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r6, #12]
	str	r3, [sp, #12]
	beq	.L763
	movw	r3, #:lower16:.LC8
	str	r10, [sp, #4]
	movt	r3, #:upper16:.LC8
	mov	r5, r0
	str	r3, [sp, #8]
	mov	r7, #0
	b	.L740
.L727:
	ldrh	r3, [r6, #14]
	ldrh	fp, [r6, #8]
	ldr	r10, [r5, #4]
	ldrh	r4, [r6, #10]
	cmp	r3, #4
	lsreq	fp, fp, #1
	lsr	r3, r10, #10
	lsleq	r4, r4, #1
	mov	r1, fp
	uxth	r0, r3
	str	r3, [sp]
	uxtheq	r4, r4
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldr	r3, [sp]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r5]
	bhi	.L730
	uxth	r1, r3
	ldr	r9, [r5, #8]
	ubfx	ip, r10, #0, #10
	uxtb	r10, r0
	movw	r8, #:lower16:.LANCHOR0
	ldr	r3, [r5, #12]
	mls	r1, fp, r0, r1
	tst	r9, #63
	movt	r8, #:upper16:.LANCHOR0
	mov	r0, r10
	mla	r4, r4, r1, ip
	ldrne	r9, [r8, #3372]
	ldr	ip, [r8, #3452]
	mov	r2, r9
	mov	r1, r4
	blx	ip
	str	r0, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	beq	.L766
.L733:
	ldr	r3, [r8, #3372]
	cmp	r9, r3
	beq	.L767
.L730:
	ldr	r3, [sp, #4]
	add	r7, r7, #1
	add	r5, r5, #20
	cmp	r3, r7
	beq	.L763
.L740:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L726
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L727
.L726:
	mov	r2, #96
	ldr	r1, .L769+4
	ldr	r0, [sp, #8]
	bl	sftl_printk
	b	.L727
.L763:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L767:
	ldr	r0, [r5, #8]
	cmp	r9, r0
	beq	.L730
	ldr	r3, .L769+8
	mov	r1, r9
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	memcpy
	b	.L730
.L766:
	ldr	r3, [r5, #12]
	mov	r0, r10
	ldr	r1, [sp, #12]
	add	r2, r9, #2048
	ldr	r10, [r8, #3452]
	add	r3, r3, #8
	add	r1, r1, r4
	blx	r10
	cmn	r0, #1
	mov	r1, r0
	beq	.L734
	ldr	r3, [r5, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	beq	.L768
.L735:
	ldr	r3, [r5]
	sub	r0, r1, #256
	clz	r0, r0
	lsr	r0, r0, #5
	cmn	r3, #1
	moveq	r0, #0
	cmp	r0, #0
	movne	r3, #256
	strne	r3, [r5]
	bne	.L736
	cmn	r3, #1
	cmpne	r3, #256
	bne	.L733
	b	.L736
.L768:
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L735
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L735
.L734:
	mvn	r3, #0
	str	r3, [r5]
.L736:
	movw	r0, #:lower16:.LC87
	ldr	r1, [r5, #4]
	mov	r2, r4
	movt	r0, #:upper16:.LC87
	bl	sftl_printk
	ldr	r1, [r5, #8]
	cmp	r1, #0
	beq	.L739
	mov	r3, #4
	movw	r0, #:lower16:.LC88
	mov	r2, r3
	movt	r0, #:upper16:.LC88
	bl	rknand_print_hex
.L739:
	ldr	r1, [r5, #12]
	cmp	r1, #0
	beq	.L733
	mov	r3, #4
	movw	r0, #:lower16:.LC89
	mov	r2, r3
	movt	r0, #:upper16:.LC89
	bl	rknand_print_hex
	b	.L733
.L770:
	.align	2
.L769:
	.word	.LANCHOR0+3416
	.word	.LANCHOR1+192
	.word	.LANCHOR0+266
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	fp, #:lower16:.LANCHOR0
	movt	fp, #:upper16:.LANCHOR0
	movw	r2, #262
	ldrh	r1, [fp, r2]
	ldr	r8, [fp, #3384]
	ldr	r2, [fp, #3352]
	cmp	r1, #0
	str	r8, [fp, #3476]
	str	r2, [fp, #3472]
	beq	.L785
	ldr	r6, .L788
	add	r10, fp, #364
	ldr	r7, .L788+4
	mov	r5, #0
	movw	r9, #61664
.L778:
	ldrh	r2, [r6]
	mvn	r1, #0
	strh	r1, [r10]	@ movhi
	add	r4, r2, r1
	sub	r1, r2, #16
	uxth	r4, r4
	cmp	r4, r1
	ble	.L774
.L776:
	mla	ip, r5, r2, r4
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	lsl	ip, ip, #10
	str	ip, [fp, #3468]
	bl	FlashReadPages
	ldr	r2, [fp, #3464]
	sub	r1, r4, #1
	cmn	r2, #1
	beq	.L775
	ldrh	r2, [r8]
	cmp	r2, r9
	beq	.L787
.L775:
	ldrh	r2, [r6]
	uxth	r4, r1
	sub	r1, r2, #16
	cmp	r4, r1
	bgt	.L776
.L774:
	ldr	r3, .L788+8
	add	r5, r5, #1
	add	r10, r10, #2
	ldrh	r2, [r3]
	cmp	r2, r5
	bhi	.L778
.L785:
	mov	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L787:
	strh	r4, [r10]	@ movhi
	b	.L774
.L789:
	.align	2
.L788:
	.word	.LANCHOR0+306
	.word	.LANCHOR0+3464
	.word	.LANCHOR0+262
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r7, #:lower16:__stack_chk_guard
	movt	r7, #:upper16:__stack_chk_guard
	cmp	r1, #1
	.pad #92
	sub	sp, sp, #92
	movwne	r2, #310
	ldr	r3, [r7]
	lsl	r5, r0, #10
	mov	r6, r1
	mov	r0, sp
	str	r3, [sp, #84]
	movwne	r3, #:lower16:.LANCHOR0
	ldreq	r3, .L807
	movtne	r3, #:upper16:.LANCHOR0
	ldrhne	r8, [r3, r2]
	ldrheq	r8, [r3]
	subeq	r3, r3, #312
	ldr	r2, [r3, #3376]
	add	r3, sp, #20
	str	r3, [sp, #12]
	sub	r8, r8, #1
	sxth	r8, r8
	str	r2, [sp, #8]
	mov	r2, r6
	orr	r1, r8, r5
	str	r1, [sp, #4]
	mov	r1, #1
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	cmn	r3, #1
	beq	.L805
.L790:
	ldr	r2, [sp, #84]
	mov	r0, r8
	ldr	r3, [r7]
	cmp	r2, r3
	bne	.L806
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L805:
	cmp	r8, #0
	blt	.L790
	mov	r9, #0
	b	.L798
.L796:
	add	r4, r4, #1
	sxth	r9, r4
.L797:
	cmp	r9, r8
	bgt	.L790
.L798:
	add	r3, r9, r8
	mov	r1, #1
	add	r3, r3, r3, lsr #31
	mov	r2, r6
	mov	r0, sp
	asr	r4, r3, r1
	orr	r3, r4, r5
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	cmn	r3, #1
	bne	.L796
	ldr	r3, [sp, #24]
	cmn	r3, #1
	bne	.L796
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L796
	sub	r4, r4, #1
	sxth	r8, r4
	b	.L797
.L806:
	bl	__stack_chk_fail
.L808:
	.align	2
.L807:
	.word	.LANCHOR0+312
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r3, #306
	add	r7, r4, #352
	mov	r0, #0
	ldrh	r2, [r4, r3]
	mvn	r3, #0
	ldr	ip, [r4, #3352]
	ldr	r6, [r4, #3384]
	add	r5, r2, r3
	sub	r2, r2, #16
	uxth	r5, r5
	str	ip, [r4, #3472]
	str	r6, [r4, #3476]
	cmp	r2, r5
	strh	r0, [r7, #6]	@ movhi
	strh	r3, [r7]	@ movhi
	str	r3, [r4, #364]
	str	r3, [r4, #368]
	str	r3, [r4, #372]
	str	r3, [r4, #376]
	ldrlt	r8, .L848
	ldrlt	r9, .L848+4
	blt	.L817
	b	.L818
.L815:
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L844
.L814:
	ldrh	r3, [r9]
	sub	r5, r5, #1
	uxth	r5, r5
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L845
.L817:
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	cmn	r3, #1
	bne	.L815
	ldr	r3, [r4, #3468]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	add	r3, r3, r2
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	cmn	r3, #1
	beq	.L814
	b	.L815
.L845:
	ldrh	r5, [r7]
.L816:
	movw	r2, #65535
	cmp	r5, r2
	beq	.L818
	ldrh	r3, [r7, #4]
	cmp	r3, r2
	beq	.L819
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L848
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	cmn	r3, #1
	beq	.L821
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L846
.L821:
	ldrh	r5, [r7]
.L819:
	mov	r0, r5
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r7, #2]	@ movhi
	cmp	r5, #0
	blt	.L822
	ldr	r8, .L848
	movw	r9, #61649
.L825:
	ldrh	r3, [r7]
	mov	r2, #1
	ldr	ip, [r4, #3352]
	mov	r1, r2
	mov	r0, r8
	orr	r3, r5, r3, lsl #10
	str	ip, [r4, #3472]
	sub	r5, r5, #1
	str	r3, [r4, #3468]
	sxth	r5, r5
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	cmn	r3, #1
	beq	.L823
	ldrh	r3, [r6]
	cmp	r3, r9
	beq	.L824
.L823:
	cmn	r5, #1
	bne	.L825
.L822:
	movw	r0, #:lower16:.LC8
	mov	r2, #251
	movt	r0, #:upper16:.LC8
	ldr	r1, .L848+8
	bl	sftl_printk
.L824:
	ldrh	r3, [r6, #12]
	movw	r1, #65535
	ldrh	r2, [r6, #10]
	cmp	r3, r1
	strh	r2, [r7, #6]	@ movhi
	beq	.L826
	ldr	r2, [r4, #240]
	cmp	r3, r2
	beq	.L826
	ldrh	r0, [r4, #248]
	lsr	r1, r0, #2
	cmp	r3, r1
	cmpcc	r2, r1
	bcc	.L847
.L826:
	movw	r3, #262
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L829
	ldr	r6, .L848+12
	mov	r5, #0
	add	r8, r6, #32
	sub	r7, r6, #118
.L828:
	ldrh	r2, [r8]
	ldr	r1, [r4, #3472]
	ldr	r0, [r6], #4
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	memcpy
	ldrh	r3, [r7]
	cmp	r3, r5
	bhi	.L828
.L829:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L844:
	ldr	r3, [r6, #4]
	strh	r5, [r7]	@ movhi
	str	r3, [r4, #360]
	ldrh	r3, [r6, #8]
	strh	r3, [r7, #4]	@ movhi
	b	.L816
.L847:
	ldrh	r1, [r4, #36]
	cmp	r3, #24
	movcc	r3, #24
	ldr	r2, [r4, #256]
	sub	r0, r0, r3
	str	r3, [r4, #240]
	strh	r0, [r4, #20]	@ movhi
	mul	r3, r3, r1
	sub	r2, r2, r3
	str	r3, [r4, #244]
	str	r2, [r4, #252]
	b	.L826
.L846:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #360]
	cmp	r3, r2
	bls	.L821
	ldrh	r5, [r7, #4]
	str	r3, [r4, #360]
	ldrh	r3, [r6, #8]
	strh	r5, [r7]	@ movhi
	strh	r3, [r7, #4]	@ movhi
	b	.L819
.L818:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L849:
	.align	2
.L848:
	.word	.LANCHOR0+3464
	.word	.LANCHOR0+306
	.word	.LANCHOR1+208
	.word	.LANCHOR0+380
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	.pad #16
	sub	sp, sp, #16
	mov	r1, #0
	movw	r6, #2680
	ldrh	r2, [r4, #20]
	ldr	r3, [r4, #3384]
	ldr	ip, [r4, #3352]
	ldr	r0, [r4, #16]
	lsl	r2, r2, #1
	str	r3, [r4, #3476]
	str	ip, [r4, #3472]
	bl	memset
	ldrh	r0, [r4, r6]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L865
	mov	r1, #1
	ldr	r9, .L887
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	ldrsh	r6, [r4, r6]
	cmp	r5, #0
	strh	r0, [r9, #2]	@ movhi
	blt	.L853
	ldr	r7, .L887+4
	movw	r8, #19539
	ldr	r10, .L887+8
	movt	r8, 18004
.L862:
	ldr	ip, [r4, #3352]
	orr	r3, r5, r6, lsl #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	ip, [r4, #3472]
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldr	r3, [r4, #3476]
	ldr	ip, [r3, #12]
	ldr	r3, [r4, #3464]
	cmp	ip, #0
	beq	.L854
	cmn	r3, #1
	beq	.L855
	ldrh	r0, [r10]
	movw	r2, #42982
	ldr	r1, [r4, #3472]
	cmp	r0, #0
	beq	.L872
	add	r0, r1, r0
	movt	r2, 18374
.L857:
	lsr	r3, r2, #2
	ldrb	lr, [r1], #1	@ zero_extendqisi2
	add	r3, r3, r2, lsl #5
	cmp	r1, r0
	add	r3, r3, lr
	eor	r2, r2, r3
	bne	.L857
.L856:
	cmp	ip, r2
	beq	.L858
	ldrh	r1, [r9, #4]
	movw	r0, #:lower16:.LC90
	str	r2, [sp, #8]
	movt	r0, #:upper16:.LC90
	str	ip, [sp, #4]
	mov	r3, r5
	str	r1, [sp]
	mov	r2, r6
	ldr	r1, .L887+12
	bl	sftl_printk
	cmp	r5, #0
	bne	.L859
	ldrh	r3, [r9, #4]
	cmp	r6, r3
	beq	.L859
	ldr	r2, .L887+16
	sxth	r6, r3
	ldrh	r5, [r2]
	sub	r5, r5, #1
	sxth	r5, r5
.L860:
	cmp	r5, #0
	bge	.L862
.L853:
	movw	r0, #:lower16:.LC8
	movw	r2, #1465
	movt	r0, #:upper16:.LC8
	ldr	r1, .L887+12
	bl	sftl_printk
.L861:
	movw	r3, #318
	ldrh	r7, [r4, #20]
	ldrh	r3, [r4, r3]
	add	r2, r7, #24
	cmp	r3, r2, lsl #1
	bcc	.L885
.L863:
	ldr	ip, [r4, #3472]
	ldr	r5, .L887+20
	add	r8, ip, #48
	mov	r6, r5
.L864:
	ldr	r0, [ip]	@ unaligned
	add	ip, ip, #16
	ldr	r1, [ip, #-12]	@ unaligned
	mov	lr, r6
	ldr	r2, [ip, #-8]	@ unaligned
	add	r6, r6, #16
	ldr	r3, [ip, #-4]	@ unaligned
	cmp	ip, r8
	stmia	lr!, {r0, r1, r2, r3}
	bne	.L864
	mov	r1, ip
	lsl	r2, r7, #1
	ldr	r0, [r4, #16]
	bl	memcpy
	ldrh	r2, [r4, #20]
	ldr	r1, [r4, #3472]
	ldr	r0, [r4, #32]
	add	r3, r2, #24
	lsl	r3, r3, #1
	lsr	r2, r2, #3
	bic	r3, r3, #3
	add	r2, r2, #4
	add	r1, r1, r3
	bl	memcpy
	ldr	r2, [r4, #2472]
	movw	r3, #19539
	movt	r3, 18004
	cmp	r2, r3
	bne	.L865
	movw	r3, #262
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	ldrh	r6, [r5, #8]
	cmp	r2, r3
	strh	r6, [r9, #6]	@ movhi
	bne	.L865
	movw	r3, #310
	movw	r2, #266
	ldrh	r3, [r4, r3]
	movw	r7, #2676
	ldrh	r2, [r4, r2]
	ldr	r1, .L887+24
	ldr	r8, [r4, #252]
	mul	r3, r6, r3
	str	r6, [r4, #3484]
	ldrh	r0, [r1, #6]
	ldrh	r1, [r4, #36]
	mul	r2, r3, r2
	sub	r0, r8, r0
	str	r3, [r4, #2608]
	sub	r0, r0, r6
	str	r2, [r4, #344]
	bl	__aeabi_uidiv
	cmp	r6, r8
	strh	r0, [r4, r7]	@ movhi
	bhi	.L886
.L866:
	ldrh	r0, [r5, #16]
	mov	r3, #0
	ldrh	r1, [r5, #20]
	mvn	lr, #0
	ldrh	r2, [r5, #24]
	ldrh	ip, [r5, #14]
	lsr	r7, r0, #6
	and	r0, r0, #63
	lsr	r6, r1, #6
	strb	r0, [r4, #46]
	lsr	r0, r2, #6
	and	r1, r1, #63
	and	r2, r2, #63
	strh	r7, [r4, #42]	@ movhi
	strh	r6, [r4, #90]	@ movhi
	ldrb	r7, [r4, #2483]	@ zero_extendqisi2
	ldrh	r6, [r5, #22]
	strb	r1, [r4, #94]
	strh	r0, [r4, #138]	@ movhi
	ldrh	r1, [r5, #18]
	ldrb	r0, [r4, #2485]	@ zero_extendqisi2
	ldrb	r5, [r4, #2484]	@ zero_extendqisi2
	strb	r2, [r4, #142]
	ldr	r2, [r4, #2504]
	strb	r0, [r4, #144]
	strh	r3, [r4, #186]	@ movhi
	str	r2, [r4, #2652]
	strb	r3, [r4, #190]
	strb	r3, [r4, #192]
	str	r3, [r4, #2632]
	str	r3, [r4, #2620]
	str	r3, [r4, #2612]
	str	r3, [r4, #2628]
	str	r3, [r4, #2656]
	strh	ip, [r4, #40]	@ movhi
	strb	r7, [r4, #48]
	strh	r1, [r4, #88]	@ movhi
	strh	r6, [r4, #136]	@ movhi
	strb	r5, [r4, #96]
	strh	lr, [r4, #184]	@ movhi
	str	r3, [r4, #2668]
	ldr	r2, [r4, #2512]
	ldr	r0, [r4, #2644]
	str	r3, [r4, #2624]
	ldr	r3, [r4, #2508]
	cmp	r2, r0
	strhi	r2, [r4, #2644]
	ldr	r2, [r4, #2648]
	cmp	r3, r2
	strhi	r3, [r4, #2648]
	movw	r3, #65535
	cmp	ip, r3
	beq	.L869
	ldr	r0, .L887+28
	bl	make_superblock
	ldrh	r1, [r4, #88]
.L869:
	movw	r3, #65535
	cmp	r1, r3
	beq	.L870
	ldr	r0, .L887+32
	bl	make_superblock
.L870:
	ldrh	r2, [r4, #136]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L871
	ldr	r0, .L887+36
	bl	make_superblock
.L871:
	ldrh	r2, [r4, #184]
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, #0
	beq	.L850
	ldr	r0, .L887+40
	bl	make_superblock
	mov	r0, #0
.L850:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L859:
	mvn	r3, #0
	str	r3, [r4, #3464]
.L855:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L860
.L854:
	cmn	r3, #1
	beq	.L855
.L858:
	ldr	r3, [r4, #3352]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L855
	ldr	r2, [r4, #3384]
	movw	r3, #61604
	ldrh	r2, [r2]
	cmp	r2, r3
	beq	.L861
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L860
.L885:
	movw	r0, #:lower16:.LC8
	movw	r2, #1467
	movt	r0, #:upper16:.LC8
	ldr	r1, .L887+12
	bl	sftl_printk
	ldrh	r7, [r4, #20]
	b	.L863
.L872:
	movt	r2, 18374
	b	.L856
.L886:
	movw	r0, #:lower16:.LC8
	movw	r2, #1489
	movt	r0, #:upper16:.LC8
	ldr	r1, .L887+12
	bl	sftl_printk
	b	.L866
.L865:
	mvn	r0, #0
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L888:
	.align	2
.L887:
	.word	.LANCHOR0+2680
	.word	.LANCHOR0+3464
	.word	.LANCHOR0+318
	.word	.LANCHOR1+220
	.word	.LANCHOR0+312
	.word	.LANCHOR0+2472
	.word	.LANCHOR0+352
	.word	.LANCHOR0+40
	.word	.LANCHOR0+88
	.word	.LANCHOR0+136
	.word	.LANCHOR0+184
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	ip, #:lower16:__stack_chk_guard
	ldr	r7, .L946
	.pad #68
	sub	sp, sp, #68
	movt	ip, #:upper16:__stack_chk_guard
	cmp	r1, #0
	str	r1, [sp]
	str	r3, [sp, #28]
	ldr	r1, [ip]
	ldrh	r3, [r7, #12]
	str	ip, [sp, #24]
	str	r0, [sp, #20]
	str	r2, [sp, #32]
	str	r3, [sp, #16]
	str	r1, [sp, #60]
	beq	.L900
	ldr	r1, [sp, #16]
	movw	r3, #:lower16:.LC8
	movt	r3, #:upper16:.LC8
	movw	r2, #:lower16:.LANCHOR0
	str	r3, [sp, #12]
	movt	r2, #:upper16:.LANCHOR0
	ldr	r3, [sp, #20]
	mov	r8, #0
	lsl	r1, r1, #3
	str	r2, [sp, #4]
	str	r1, [sp, #8]
	mov	r5, r3
	str	r3, [sp, #36]
	b	.L906
.L943:
	uxth	r4, r4
	cmp	r6, #0
	ubfx	r10, r10, #0, #10
	mls	r4, r9, r6, r4
	mla	r4, fp, r4, r10
	bne	.L898
	ldr	r3, [sp, #8]
	cmp	r3, r4
	bhi	.L940
.L898:
	ldr	r9, [r5, #8]
	tst	r9, #63
	beq	.L901
	ldr	r3, [sp, #4]
	ldr	r0, [r3, #3372]
	cmp	r9, r0
	beq	.L901
	ldr	r3, .L946+4
	mov	r1, r9
	mov	r9, r0
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	memcpy
.L901:
	ldr	r3, [sp, #4]
	uxtb	r6, r6
	mov	r2, r9
	mov	r0, r6
	mov	r1, r4
	ldr	r10, [r3, #3448]
	ldr	r3, [r5, #12]
	blx	r10
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r7, #14]
	cmp	r3, #4
	beq	.L941
.L897:
	ldr	r3, [sp]
	add	r8, r8, #1
	add	r5, r5, #20
	cmp	r3, r8
	beq	.L942
.L906:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L893
	ldr	r3, [r5, #12]
	cmp	r3, #0
	beq	.L893
.L894:
	ldrh	r2, [r7, #14]
	ldrh	fp, [r7, #10]
	ldrh	r9, [r7, #8]
	ldr	r10, [r5, #4]
	cmp	r2, #4
	lsleq	r4, fp, #1
	lsreq	r9, r9, #1
	uxtheq	fp, r4
	lsr	r4, r10, #10
	mov	r1, r9
	uxth	r0, r4
	bl	__aeabi_uidiv
	uxth	r6, r0
	cmp	r6, #3
	bls	.L943
	mvn	r3, #0
	str	r3, [r5]
	b	.L897
.L893:
	mov	r2, #148
	ldr	r1, .L946+8
	ldr	r0, [sp, #12]
	bl	sftl_printk
	b	.L894
.L941:
	ldr	r3, [r5, #12]
	add	r2, r9, #2048
	ldr	r1, [sp, #16]
	mov	r0, r6
	ldr	ip, [sp, #4]
	add	r3, r3, #8
	add	r1, r1, r4
	ldr	r4, [ip, #3448]
	blx	r4
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
	b	.L897
.L940:
	ldr	r5, .L946+8
	movw	r4, #:lower16:.LC91
	ldr	r7, [sp, #36]
	movt	r4, #:upper16:.LC91
.L899:
	mvn	r3, #0
	ldr	r2, [r7, #4]
	str	r3, [r7]
	mov	r1, r5
	mov	r0, r4
	add	r6, r6, #1
	bl	sftl_printk
	movw	r0, #:lower16:.LC92
	ldr	r1, [r7, #8]
	mov	r3, #16
	movt	r0, #:upper16:.LC92
	mov	r2, #4
	add	r7, r7, #20
	bl	rknand_print_hex
	mov	r3, #4
	movw	r0, #:lower16:.LC93
	mov	r2, r3
	ldr	r1, [r7, #-8]
	movt	r0, #:upper16:.LC93
	bl	rknand_print_hex
	ldr	r3, [sp]
	cmp	r3, r6
	bne	.L899
	bl	dump_stack
.L900:
	ldr	r3, [sp, #24]
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L944
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L942:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L900
	movw	r8, #:lower16:.LC95
	movw	r5, #:lower16:.LANCHOR0
	movw	r9, #:lower16:.LC94
	movw	r7, #:lower16:.LC96
	movt	r8, #:upper16:.LC95
	ldr	r10, [sp]
	ldr	r4, [sp, #20]
	movt	r5, #:upper16:.LANCHOR0
	movt	r9, #:upper16:.LC94
	movt	r7, #:upper16:.LC96
	mov	r6, #0
	ldr	fp, [sp, #32]
	str	r8, [sp]
	b	.L912
.L908:
	cmp	r8, #256
	beq	.L945
.L909:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L910
	ldr	r1, [r5, #3380]
	ldr	r2, [r3]
	ldr	r3, [r1]
	cmp	r2, r3
	beq	.L910
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L910:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L911
	ldr	r1, [r5, #3372]
	ldr	r2, [r3]
	ldr	r3, [r1]
	cmp	r2, r3
	beq	.L911
	movw	r0, #:lower16:.LC97
	ldr	r1, [r4, #4]
	movt	r0, #:upper16:.LC97
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L911:
	add	r6, r6, #1
	add	r4, r4, #20
	cmp	r10, r6
	beq	.L900
.L912:
	ldr	r0, [r5, #3372]
	mov	r3, #0
	mov	r2, fp
	mov	r1, #1
	str	r3, [r0]
	add	r0, sp, #40
	ldr	ip, [r5, #3380]
	str	r3, [ip]
	ldr	lr, [r4, #4]
	ldr	ip, [r5, #3372]
	ldr	r3, [r5, #3380]
	str	lr, [sp, #44]
	str	ip, [sp, #48]
	str	r3, [sp, #52]
	bl	FlashReadPages
	ldr	r8, [sp, #40]
	cmn	r8, #1
	bne	.L908
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	str	r8, [r4]
	ldr	r8, [sp, #40]
	cmp	r8, #256
	bne	.L909
.L945:
	ldr	r1, [r4, #4]
	ldr	r0, [sp]
	bl	sftl_printk
	str	r8, [r4]
	b	.L909
.L944:
	bl	__stack_chk_fail
.L947:
	.align	2
.L946:
	.word	.LANCHOR0+3416
	.word	.LANCHOR0+266
	.word	.LANCHOR1+236
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	subs	r10, r2, #0
	ldr	r7, .L968
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r7, #12]
	str	r3, [sp, #8]
	beq	.L950
	lsl	r3, r3, #3
	movw	r9, #:lower16:.LANCHOR0
	str	r0, [sp, #12]
	movt	r9, #:upper16:.LANCHOR0
	str	r10, [sp]
	add	r6, r0, #4
	str	r3, [sp, #4]
	mov	r8, #0
	b	.L959
.L957:
	ldr	r3, [sp]
	add	r6, r6, #20
	cmp	r3, r8
	beq	.L950
.L959:
	ldrh	r4, [r7, #8]
	add	r8, r8, #1
	ldrh	r3, [r7, #14]
	ldr	r5, [r6]
	ldrh	r10, [r7, #10]
	cmp	r3, #4
	lsreq	r4, r4, #1
	lsr	fp, r5, #10
	ubfx	r5, r5, #0, #10
	lsleq	r2, r10, #1
	mov	r1, r4
	uxth	r0, fp
	uxtheq	r10, r2
	bl	__aeabi_uidiv
	uxth	ip, fp
	uxth	r1, r0
	mls	r4, r4, r1, ip
	uxtb	fp, r1
	cmp	r1, #0
	mov	r0, fp
	mla	r4, r10, r4, r5
	bne	.L952
	ldr	r3, [sp, #4]
	cmp	r3, r4
	bhi	.L967
.L952:
	ldr	r2, [r9, #3444]
	mov	r1, r4
	blx	r2
	cmp	r0, #0
	mvnne	r0, #0
	str	r0, [r6, #-4]
	ldrh	r2, [r7, #14]
	cmp	r2, #4
	bne	.L957
	ldr	r2, [sp, #8]
	mov	r0, fp
	ldr	r3, [r9, #3444]
	add	r6, r6, #20
	add	r1, r2, r4
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r6, #-24]
	ldr	r3, [sp]
	cmp	r3, r8
	bne	.L959
.L950:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L967:
	ldr	r8, .L968+4
	movw	r7, #:lower16:.LC91
	ldr	r6, [sp, #12]
	mov	r5, r1
	ldr	r9, [sp]
	movt	r7, #:upper16:.LC91
.L953:
	mvn	r3, #0
	mov	r2, r4
	str	r3, [r6]
	mov	r1, r8
	mov	r0, r7
	add	r5, r5, #1
	bl	sftl_printk
	movw	r0, #:lower16:.LC92
	ldr	r1, [r6, #8]
	mov	r3, #16
	movt	r0, #:upper16:.LC92
	mov	r2, #4
	add	r6, r6, #20
	bl	rknand_print_hex
	mov	r3, #4
	movw	r0, #:lower16:.LC93
	ldr	r1, [r6, #-8]
	mov	r2, r3
	movt	r0, #:upper16:.LC93
	bl	rknand_print_hex
	cmp	r9, r5
	bne	.L953
	bl	dump_stack
	b	.L950
.L969:
	.align	2
.L968:
	.word	.LANCHOR0+3416
	.word	.LANCHOR1+252
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn.part.10, %function
FtlFreeSysBlkQueueIn.part.10:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	bne	.L976
.L971:
	ldr	r3, .L977
	ldrh	r2, [r3, #4]
	ldrh	r0, [r3, #6]
	add	r1, r2, #1
	add	r2, r3, r2, lsl #1
	add	r0, r0, #1
	ubfx	r1, r1, #0, #10
	strh	r0, [r3, #6]	@ movhi
	strh	r1, [r3, #4]	@ movhi
	strh	r5, [r2, #8]	@ movhi
	pop	{r4, r5, r6, pc}
.L976:
	movw	r4, #:lower16:.LANCHOR0
	movw	r3, #306
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	add	r3, r4, #264
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3348]
	lsl	ip, r5, #10
	mov	r2, #1
	uxth	r6, r0
	mov	r1, r2
	str	ip, [r3, #4]
	mov	r0, r3
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2528]
	lsl	r0, r6, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r4, #2664]
	add	r3, r3, #1
	str	r3, [r4, #2664]
	b	.L971
.L978:
	.align	2
.L977:
	.word	.LANCHOR0+416
	.fnend
	.size	FtlFreeSysBlkQueueIn.part.10, .-FtlFreeSysBlkQueueIn.part.10
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bxhi	lr
	ldr	r3, .L984
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	bxeq	lr
	b	FtlFreeSysBlkQueueIn.part.10
.L985:
	.align	2
.L984:
	.word	.LANCHOR0+416
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r8, r0
	.pad #12
	sub	sp, sp, #12
	ldr	r7, [r0, #20]
	ldr	r4, [r0, #12]
	ldr	r5, [r0, #24]
	lsl	r2, r2, #1
	mov	r0, r7
	bl	memset
	ldrh	r3, [r8, #6]
	cmp	r3, #0
	ldrhne	r1, [r8, #10]
	movne	r6, #0
	beq	.L992
.L991:
	ldr	lr, [r5, r6, lsl #2]
	cmp	r1, #0
	movne	r3, #0
	ubfx	lr, lr, #10, #16
	beq	.L995
.L994:
	lsl	r0, r3, #1
	add	r3, r3, #1
	uxth	r3, r3
	ldrh	r2, [r4, r0]
	adds	ip, r2, #0
	movne	ip, #1
	cmp	lr, r2
	movne	ip, #0
	cmp	ip, #0
	ldrhne	r2, [r7, r0]
	addne	r2, r2, #1
	strhne	r2, [r7, r0]	@ movhi
	ldrhne	r1, [r8, #10]
	cmp	r1, r3
	bhi	.L994
	ldrh	r3, [r8, #6]
.L995:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L991
.L992:
	ldrh	r3, [r8]
	ldr	fp, .L1018
	ldrh	r2, [fp]
	lsl	r3, r3, #1
	strh	r2, [r7, r3]	@ movhi
	ldrh	r1, [r8, #10]
	ldrh	ip, [r7]
	cmp	r1, #0
	moveq	r0, r1
	beq	.L986
	mov	r10, #0
	add	fp, fp, #104
	mov	r5, r10
	mov	r2, r10
	movw	r3, #65535
	b	.L1000
.L1017:
	ldrh	r0, [r4, r6]
	add	r6, r4, r6
	cmp	r0, #0
	beq	.L998
	cmp	r9, #0
	movne	ip, r9
	movne	r10, r5
	bne	.L998
	mov	r10, r5
.L1001:
	cmp	r0, r3
	beq	.L999
	ldrh	r1, [fp, #6]
	cmp	r1, #1024
	beq	.L999
	mov	r1, #1
	str	r2, [sp, #4]
	str	r3, [sp]
	bl	FtlFreeSysBlkQueueIn.part.10
	ldr	r2, [sp, #4]
	ldr	r3, [sp]
.L999:
	strh	r2, [r6]	@ movhi
	mov	ip, r9
	ldrh	r0, [r8, #8]
	ldrh	r1, [r8, #10]
	sub	r0, r0, #1
	strh	r0, [r8, #8]	@ movhi
.L998:
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r1, r5
	bls	.L1016
.L1000:
	lsl	r6, r5, #1
	ldrh	r9, [r7, r6]
	cmp	r9, ip
	bcc	.L1017
	cmp	r9, #0
	bne	.L998
	ldrh	r0, [r4, r6]
	add	r6, r4, r6
	cmp	r0, #0
	movne	r9, ip
	bne	.L1001
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r1, r5
	bhi	.L1000
.L1016:
	mov	r0, r10
.L986:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1019:
	.align	2
.L1018:
	.word	.LANCHOR0+312
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r9, #:lower16:.LANCHOR0
	movt	r9, #:upper16:.LANCHOR0
	mov	r4, #0
	add	r3, r9, #2592
	mov	r1, r4
	ldr	r2, [r9, #332]
	.pad #36
	sub	sp, sp, #36
	strh	r4, [r3]	@ movhi
	ldr	r3, .L1143
	ldr	r0, [r9, #2600]
	lsl	r2, r2, #2
	strh	r4, [r3]	@ movhi
	bl	memset
	ldr	r2, [r9, #332]
	mov	r1, r4
	ldr	r0, [r9, #2596]
	lsl	r2, r2, #1
	bl	memset
	ldrh	r2, [r9]
	mov	r1, r4
	ldr	r0, [r9, #3408]
	lsl	r2, r2, #2
	bl	memset
	ldrh	r2, [r9]
	mov	r1, r4
	ldr	r0, [r9, #4]
	lsl	r2, r2, #1
	bl	memset
	ldrh	r3, [r9, #20]
	ldrh	r2, [r9, #248]
	ldr	r0, .L1143+4
	mov	r1, r3
	str	r3, [sp, #24]
	cmp	r2, r1
	mvn	r3, #0
	str	r3, [r9, #2680]
	movhi	r8, r9
	str	r3, [r0, #4]
	str	r3, [r0, #8]
	str	r3, [r0, #12]
	addhi	r3, r9, #268
	strhi	r3, [sp, #28]
	bls	.L1022
.L1026:
	ldrh	r3, [r8, #36]
	cmp	r3, #0
	beq	.L1066
	ldr	r1, [sp, #28]
	sub	r3, r3, #1
	ldr	fp, .L1143+8
	mov	r10, #0
	ldr	r9, [r8, #3336]
	uxtah	r3, r1, r3
	add	r2, fp, #42
	ldrh	r7, [fp], #3
	str	r3, [sp, #8]
	ldrh	r3, [r2, #14]
	ldrh	r6, [r2]
	ldr	r2, [r8, #3232]
	str	r3, [sp, #4]
	ldrh	r3, [sp, #24]
	str	r2, [sp, #16]
	ldr	r2, [r8, #3236]
	smulbb	r3, r3, r7
	str	r2, [sp, #20]
	uxth	r3, r3
	str	r3, [sp, #12]
.L1024:
	ldrb	r5, [fp, #1]!	@ zero_extendqisi2
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidiv
	mov	r1, r7
	smulbb	r4, r0, r6
	mov	r0, r5
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #12]
	add	r4, r4, r1
	mov	r1, r6
	add	r4, r3, r4
	uxth	r4, r4
	mov	r0, r4
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r5, r1
	mov	r1, r6
	lsl	r4, r4, #10
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	ip, r5, #5
	ldr	r3, [sp, #4]
	add	r0, r8, r0, lsl #2
	and	r5, r5, #31
	ldr	r1, [r0, #380]
	add	r0, r10, #1
	mul	r2, r3, r10
	add	r3, r10, r10, lsl #2
	ldr	r1, [r1, ip, lsl #2]
	add	r3, r9, r3, lsl #2
	add	lr, r2, #3
	lsr	r5, r1, r5
	tst	r5, #1
	bne	.L1023
	cmp	r2, #0
	ldr	r1, [sp, #20]
	movlt	r2, lr
	uxth	r10, r0
	bic	r2, r2, #3
	str	r4, [r3, #4]
	add	r2, r1, r2
	ldr	r1, [sp, #16]
	str	r2, [r3, #12]
	str	r1, [r3, #8]
.L1023:
	ldr	r3, [sp, #8]
	cmp	r3, fp
	bne	.L1024
	cmp	r10, #0
	bne	.L1025
.L1066:
	ldr	r3, [sp, #24]
	add	r2, r3, #1
	ldrh	r3, [r8, #248]
	uxth	r2, r2
	cmp	r3, r2
	str	r2, [sp, #24]
	bhi	.L1026
	mov	r9, r8
.L1022:
	ldr	r1, [r9, #2596]
	ldrh	r3, [r1]
	cmp	r3, #0
	bne	.L1072
	ldr	r2, .L1143+12
	ldrh	r2, [r2]
	cmp	r2, #0
	beq	.L1070
	ldr	ip, [r9, #332]
	cmp	ip, #0
	bne	.L1074
	b	.L1072
.L1076:
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1134
.L1074:
	add	r3, r3, #1
	sxth	r3, r3
	cmp	r3, ip
	lsl	r2, r3, #1
	bcc	.L1076
.L1072:
	ldr	r0, [r9, #4]
	ldrh	r3, [r0]
	cmp	r3, #0
	bne	.L1069
.L1068:
	ldr	r3, .L1143
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L1069
.L1088:
	ldrh	lr, [r9]
	cmp	lr, #0
	movne	r3, #0
	bne	.L1080
	b	.L1069
.L1082:
	ldrh	r1, [r0, r2]
	cmp	r1, #0
	bne	.L1135
.L1080:
	add	r3, r3, #1
	sxth	r3, r3
	cmp	r3, lr
	lsl	r2, r3, #1
	mov	ip, r3
	blt	.L1082
.L1069:
	ldr	r3, .L1143+12
	ldrh	r2, [r3]
	ldr	r3, [r9, #332]
	cmp	r2, r3
	bhi	.L1136
.L1109:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1025:
	sub	r6, r10, #1
	mov	r0, r9
	uxth	r6, r6
	mov	r2, #1
	mov	r1, r10
	add	r6, r6, #1
	movw	fp, #:lower16:.LC8
	add	r6, r6, r6, lsl #2
	movt	fp, #:upper16:.LC8
	mov	r4, #0
	bl	FlashReadPages
	lsl	r6, r6, #2
	b	.L1065
.L1029:
	ldr	r3, [r8, #2644]
	cmn	r3, #1
	beq	.L1137
	ldr	r2, [r10, #4]
	cmp	r3, r2
	bhi	.L1033
.L1032:
	cmn	r2, #1
	addne	r3, r2, #1
	strne	r3, [r8, #2644]
.L1033:
	ldrh	r3, [r10]
	movw	r1, #61604
	cmp	r3, r1
	beq	.L1035
	bls	.L1138
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1038
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1034
	sub	r3, r9, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1034
	ldr	r3, .L1143+16
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	beq	.L1034
	mov	r0, r9
	mov	r1, #0
	bl	FtlFreeSysBlkQueueIn.part.10
.L1034:
	add	r4, r4, #20
	cmp	r6, r4
	beq	.L1066
.L1065:
	ldr	r0, [r8, #3336]
	ldr	r2, [r0, r4]
	add	r0, r0, r4
	ldr	r3, [r0, #4]
	ldr	r10, [r0, #12]
	cmn	r2, #1
	ubfx	r9, r3, #10, #16
	bne	.L1029
	mov	r5, #16
	movw	r7, #65535
	b	.L1031
.L1140:
	ldr	r3, [r0, #4]
.L1031:
	add	r3, r3, #1
	mov	r2, #1
	str	r3, [r0, #4]
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r3, [r10]
	sub	r5, r5, #1
	uxth	r5, r5
	cmp	r3, r7
	beq	.L1139
	ldr	r0, [r8, #3336]
	ldr	r3, [r0, r4]
	add	r0, r0, r4
	cmn	r3, #1
	bne	.L1029
	cmp	r5, #0
	bne	.L1140
.L1030:
	sub	r3, r9, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1034
	ldr	r3, .L1143+16
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	beq	.L1034
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn.part.10
	b	.L1034
.L1138:
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1034
	ldr	r3, .L1143
	ldrh	r5, [r8]
	ldrh	r7, [r3]
	cmp	r7, r5
	bhi	.L1141
.L1052:
	sub	r0, r5, #1
	sxth	r2, r0
	sub	r0, r0, r7
	cmp	r0, r2
	bge	.L1053
	ldr	r1, [r8, #3408]
	lsl	lr, r2, #2
	ldr	ip, [r10, #4]
	ldr	r3, [r1, r2, lsl #2]
	cmp	ip, r3
	bls	.L1056
	b	.L1054
.L1061:
	ldr	r3, [r1, r2, lsl #2]
	cmp	r3, ip
	bcc	.L1054
.L1056:
	sub	r2, r2, #1
	sxth	r2, r2
	cmp	r2, r0
	lsl	lr, r2, #2
	bgt	.L1061
.L1053:
	cmp	r2, #0
	blt	.L1034
	ldr	r1, .L1143
	ldrh	r3, [r8]
	ldrh	r1, [r1]
	sub	r3, r3, #1
	sub	r3, r3, r1
	sxth	r3, r3
	cmp	r3, r2
	blt	.L1034
	ldr	ip, [r10, #4]
	add	r1, r1, #1
	ldr	r0, [r8, #3408]
	ldr	r3, .L1143
	strh	r1, [r3]	@ movhi
	lsl	r3, r2, #1
	str	ip, [r0, r2, lsl #2]
	ldr	r2, [r8, #4]
	strh	r9, [r2, r3]	@ movhi
	b	.L1034
.L1137:
	ldr	r2, [r10, #4]
	b	.L1032
.L1038:
	ldr	r3, .L1143+12
	ldr	r5, [r8, #332]
	ldrh	lr, [r3]
	cmp	lr, r5
	bhi	.L1142
.L1040:
	uxth	r3, r5
	sub	r2, r3, lr
	sub	r3, r3, #1
	sub	r2, r2, #1
	sxth	r3, r3
	sxth	r2, r2
	cmp	r3, r2
	ble	.L1041
	ldr	r1, [r8, #2600]
	lsl	ip, r3, #2
	ldr	r0, [r10, #4]
	ldr	r7, [r1, r3, lsl #2]
	cmp	r0, r7
	bls	.L1044
	b	.L1042
.L1050:
	ldr	r7, [r1, r3, lsl #2]
	cmp	r7, r0
	bcc	.L1042
.L1044:
	sub	r3, r3, #1
	sxth	r3, r3
	cmp	r2, r3
	lsl	ip, r3, #2
	blt	.L1050
.L1041:
	cmp	r3, #0
	blt	.L1034
	lsl	ip, r3, #2
	lsl	r1, r3, #1
.L1086:
	ldr	r3, [r8, #2600]
	ldr	r2, [r10, #4]
	add	lr, lr, #1
	ldr	r0, .L1143+12
	strh	lr, [r0]	@ movhi
	str	r2, [r3, ip]
	ldr	r3, [r8, #2596]
	strh	r9, [r3, r1]	@ movhi
	b	.L1034
.L1035:
	ldr	r3, .L1143+4
	ldrh	r1, [r3]
	movw	r3, #65535
	cmp	r1, r3
	ldr	r3, .L1143+4
	streq	r2, [r8, #2688]
	strheq	r9, [r3]	@ movhi
	beq	.L1034
	ldrh	r0, [r3, #4]
	movw	r1, #65533
	sub	r3, r0, #1
	uxth	r3, r3
	cmp	r3, r1
	bhi	.L1063
	ldr	r3, .L1143+16
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	beq	.L1063
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn.part.10
	ldr	r2, [r10, #4]
.L1063:
	ldr	r3, [r8, #2688]
	cmp	r3, r2
	ldr	r3, .L1143+4
	ldrcc	r2, .L1143+4
	ldrhcc	r3, [r3]
	strhcs	r9, [r3, #4]	@ movhi
	strhcc	r9, [r2]	@ movhi
	strhcc	r3, [r2, #4]	@ movhi
	ldrcc	r3, [r10, #4]
	strcc	r3, [r8, #2688]
	b	.L1034
.L1054:
	sub	r5, r5, r7
	ldr	r3, [r1]
	clz	r5, r5
	lsr	r5, r5, #5
	cmp	r3, #0
	orrne	r5, r5, #1
	cmp	r5, #0
	ldreq	r3, .L1143
	addeq	r7, r7, #1
	strheq	r7, [r3]	@ movhi
	cmp	r2, #0
	beq	.L1058
	mov	r3, #0
.L1060:
	add	ip, r1, r3, lsl #2
	lsl	r0, r3, #1
	ldr	r5, [ip, #4]
	add	ip, r3, #1
	str	r5, [r1, r3, lsl #2]
	sxth	r3, ip
	ldr	r1, [r8, #4]
	cmp	r2, r3
	add	ip, r1, r0
	ldrh	ip, [ip, #2]
	strh	ip, [r1, r0]	@ movhi
	ldr	r1, [r8, #3408]
	bne	.L1060
	ldr	ip, [r10, #4]
.L1058:
	str	ip, [r1, lr]
	lsl	r3, r2, #1
	ldr	r1, [r8, #4]
	strh	r9, [r1, r3]	@ movhi
	b	.L1053
.L1042:
	ldr	r2, [r1]
	cmp	r2, #0
	bne	.L1045
	cmp	lr, r5
	ldrne	r2, .L1143+12
	addne	lr, lr, #1
	strhne	lr, [r2]	@ movhi
.L1045:
	cmp	r3, #0
	beq	.L1046
	mov	r2, #0
.L1048:
	add	lr, r1, r2, lsl #2
	lsl	r0, r2, #1
	ldr	r5, [lr, #4]
	add	lr, r2, #1
	str	r5, [r1, r2, lsl #2]
	sxth	r2, lr
	ldr	r1, [r8, #2596]
	cmp	r3, r2
	add	lr, r1, r0
	ldrh	lr, [lr, #2]
	strh	lr, [r1, r0]	@ movhi
	ldr	r1, [r8, #2600]
	bne	.L1048
	ldr	r0, [r10, #4]
.L1046:
	str	r0, [r1, ip]
	cmp	r3, #0
	ldr	r2, [r8, #2596]
	lsl	r1, r3, #1
	strh	r9, [r2, r1]	@ movhi
	blt	.L1034
	ldr	r2, .L1143+12
	ldrh	lr, [r2]
	ldr	r2, [r8, #332]
	sub	r2, r2, lr
	sub	r2, r2, #1
	sxth	r2, r2
	cmp	r2, r3
	blt	.L1034
	b	.L1086
.L1139:
	ldr	r3, [r8, #3336]
	mvn	r2, #0
	str	r2, [r3, r4]
	ldr	r3, [r8, #3336]
	ldr	r3, [r3, r4]
	cmp	r3, r2
	beq	.L1030
	b	.L1029
.L1142:
	movw	r2, #1225
	ldr	r1, .L1143+20
	mov	r0, fp
	bl	sftl_printk
	ldr	r3, .L1143+12
	ldr	r5, [r8, #332]
	ldrh	lr, [r3]
	b	.L1040
.L1141:
	movw	r2, #1266
	ldr	r1, .L1143+20
	mov	r0, fp
	bl	sftl_printk
	ldr	r3, .L1143
	ldrh	r5, [r8]
	ldrh	r7, [r3]
	b	.L1052
.L1136:
	movw	r0, #:lower16:.LC8
	movw	r2, #1391
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1143+20
	bl	sftl_printk
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1070:
	ldr	r0, [r9, #4]
	ldrh	r3, [r0]
	cmp	r3, #0
	bne	.L1109
	ldr	r3, .L1143
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L1109
	b	.L1088
.L1135:
	mov	r5, #0
.L1081:
	ldr	r1, [r9, #4]
	sub	r0, r3, ip
	lsl	lr, r0, #1
	add	r4, r3, #1
	ldrh	r6, [r1, r2]
	strh	r6, [r1, lr]	@ movhi
	ldr	r1, [r9, #3408]
	ldr	lr, [r1, r3, lsl #2]
	sxth	r3, r4
	str	lr, [r1, r0, lsl #2]
	ldr	r1, [r9, #4]
	strh	r5, [r1, r2]	@ movhi
	lsl	r2, r3, #1
	ldrh	r1, [r9]
	cmp	r3, r1
	blt	.L1081
	b	.L1069
.L1134:
	mov	r4, r3
	mov	lr, #0
	b	.L1087
.L1075:
	ldr	r1, [r9, #2596]
	ldrh	r0, [r1, r2]
.L1087:
	sub	ip, r3, r4
	add	r5, r3, #1
	lsl	r6, ip, #1
	strh	r0, [r1, r6]	@ movhi
	ldr	r1, [r9, #2600]
	ldr	r0, [r1, r3, lsl #2]
	sxth	r3, r5
	str	r0, [r1, ip, lsl #2]
	ldr	r1, [r9, #2596]
	strh	lr, [r1, r2]	@ movhi
	lsl	r2, r3, #1
	ldr	r1, [r9, #332]
	cmp	r3, r1
	bcc	.L1075
	ldr	r0, [r9, #4]
	ldrh	r3, [r0]
	cmp	r3, #0
	beq	.L1068
	b	.L1069
.L1144:
	.align	2
.L1143:
	.word	.LANCHOR0+348
	.word	.LANCHOR0+2680
	.word	.LANCHOR0+264
	.word	.LANCHOR0+2592
	.word	.LANCHOR0+416
	.word	.LANCHOR1+272
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	add	r4, r5, #416
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r6, #65535
	bne	.L1151
.L1146:
	movw	r0, #:lower16:.LC98
	mov	r1, r6
	movt	r0, #:upper16:.LC98
	bl	sftl_printk
.L1148:
	b	.L1148
.L1151:
	ldrh	r0, [r4, #2]
	sub	r3, r2, #1
	mov	r2, #1
	strh	r3, [r4, #6]	@ movhi
	ldr	ip, [r5, #3348]
	mov	r1, r2
	add	r3, r0, r2
	add	r0, r4, r0, lsl r2
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #2]	@ movhi
	ldrh	r7, [r0, #8]
	mov	r0, ip
	mov	r6, r7
	lsl	r3, r7, #10
	str	r3, [ip, #4]
	bl	FlashEraseBlocks
	sub	r2, r7, #1
	ldr	r3, [r5, #2664]
	uxth	r2, r2
	movw	r1, #65533
	cmp	r2, r1
	add	r3, r3, #1
	str	r3, [r5, #2664]
	bhi	.L1152
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, pc}
.L1152:
	ldrh	r2, [r4, #6]
	b	.L1146
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r1, [r0, #10]
	ldr	r5, [r0, #12]
	cmp	r1, #0
	beq	.L1161
	ldrh	r4, [r5]
	mov	r6, r0
	cmp	r4, #0
	addne	r3, r5, #2
	movne	r4, #0
	bne	.L1158
	b	.L1156
.L1162:
	ldrh	r2, [r5]
	cmp	r2, #0
	beq	.L1156
.L1158:
	add	r4, r4, #1
	mov	r5, r3
	uxth	r4, r4
	add	r3, r3, #2
	cmp	r4, r1
	bne	.L1162
.L1161:
	movw	r0, #:lower16:.LC8
	movw	r2, #578
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1171
	bl	sftl_printk
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1156:
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r5]	@ movhi
	cmp	r3, r2
	bhi	.L1170
	ldrh	r0, [r6, #10]
	mov	r1, #0
	ldr	r2, [r6, #28]
	ldrh	r3, [r6, #8]
	cmp	r0, r4
	strh	r4, [r6]	@ movhi
	add	r2, r2, #1
	strh	r1, [r6, #2]	@ movhi
	add	r3, r3, #1
	str	r2, [r6, #28]
	strh	r3, [r6, #8]	@ movhi
	bls	.L1161
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1170:
	ldr	r3, .L1171+4
	movw	r0, #:lower16:.LC99
	movt	r0, #:upper16:.LC99
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L1160:
	b	.L1160
.L1172:
	.align	2
.L1171:
	.word	.LANCHOR1+288
	.word	.LANCHOR0+416
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	beq	.L1190
	lsl	r3, r3, #1
	ldrh	r2, [r0, #2]
	movw	r6, #:lower16:.LANCHOR0
	ldr	ip, [r0, #28]
	ldrh	r8, [r5, r3]
	movt	r6, #:upper16:.LANCHOR0
	ldr	r5, [r6, #3384]
	movw	r0, #64245
	ldr	r1, [r6, #3352]
	movt	r0, 65535
	ldr	r7, [r4, #24]
	orr	r3, r2, r8, lsl #10
	str	r5, [r6, #3476]
	str	r1, [r6, #3472]
	add	r2, r6, #312
	str	r3, [r6, #3468]
	mov	r1, #255
	str	ip, [r5, #4]
	strh	r0, [r5, #8]	@ movhi
	ldrh	r3, [r4, #4]
	strh	r8, [r5, #2]	@ movhi
	strh	r3, [r5]	@ movhi
	ldrh	r2, [r2]
	ldr	r0, [r6, #3352]
	lsl	r2, r2, #3
	bl	memset
	ldrh	ip, [r4, #6]
	cmp	ip, #0
	movne	r0, #0
	movne	r3, r0
	bne	.L1181
.L1182:
	movw	r3, #318
	movw	r1, #42982
	ldrh	ip, [r6, r3]
	ldr	r2, [r6, #3472]
	cmp	ip, #0
	movteq	r1, 18374
	beq	.L1179
	add	ip, r2, ip
	movt	r1, 18374
.L1183:
	lsr	r3, r1, #2
	ldrb	r0, [r2], #1	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	ip, r2
	add	r3, r3, r0
	eor	r1, r1, r3
	bne	.L1183
.L1179:
	mov	r2, #1
	str	r1, [r5, #12]
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L1192
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1191:
	ldr	r1, [r6, #3352]
	add	r0, r0, #1
	uxth	r0, r0
	str	r3, [r1, r0, lsl #3]
	ldr	r1, [r6, #3352]
	ldr	ip, [r7, r3, lsl #2]
	add	r3, r1, r0, lsl #3
	str	ip, [r3, #4]
	ldrh	ip, [r4, #6]
.L1180:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L1182
.L1181:
	ldr	r1, [r7, r3, lsl #2]
	add	r2, r3, #1
	cmp	r8, r1, lsr #10
	bne	.L1180
	b	.L1191
.L1190:
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1175
	movw	r0, #:lower16:.LC8
	movw	r2, #641
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1192+4
	bl	sftl_printk
	ldrh	r3, [r4, #8]
.L1175:
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5]	@ movhi
	mov	r2, #0
	ldr	r3, [r4, #28]
	mov	r0, #0
	strh	r2, [r4, #2]	@ movhi
	strh	r2, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	pop	{r4, r5, r6, r7, r8, pc}
.L1193:
	.align	2
.L1192:
	.word	.LANCHOR0+3464
	.word	.LANCHOR1+316
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r5, #:lower16:.LANCHOR0
	ldr	r10, .L1221
	movt	r5, #:upper16:.LANCHOR0
	movw	fp, #:lower16:.LC8
	uxth	r9, r1
	ldr	r8, .L1221+4
	mov	r4, r0
	mov	r7, r2
	add	r6, r5, #312
	movt	fp, #:upper16:.LC8
	.pad #12
	sub	sp, sp, #12
	mov	r3, #0
	str	r1, [sp, #4]
	str	r3, [sp]
.L1195:
	ldr	r2, [r5, #2628]
	ldrh	r3, [r6]
	add	r2, r2, #1
	str	r2, [r5, #2628]
	sub	r3, r3, #1
	ldrh	r2, [r4, #2]
	cmp	r2, r3
	bge	.L1196
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1196
.L1197:
	ldr	r1, [r4, #12]
	lsl	r2, r3, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1217
	ldrh	r2, [r4, #10]
	cmp	r2, r3
	bls	.L1218
.L1199:
	ldr	r0, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #2]
	mov	r2, #0
	ldrh	r0, [r0, r3]
	ldr	r3, [r5, #3384]
	str	r7, [r5, #3472]
	orr	r1, r1, r0, lsl #10
	str	r3, [r5, #3476]
	str	r2, [r3, #8]	@ unaligned
	str	r1, [r5, #3468]
	movw	r1, #42982
	str	r2, [r3]	@ unaligned
	str	r2, [r3, #4]	@ unaligned
	str	r2, [r3, #12]	@ unaligned
	ldr	r3, [r4, #28]
	ldr	ip, [r5, #3476]
	strh	r9, [ip, #8]	@ movhi
	str	r3, [ip, #4]
	ldrh	r3, [r4, #4]
	strh	r0, [ip, #2]	@ movhi
	strh	r3, [ip]	@ movhi
	ldrh	lr, [r10]
	ldr	r0, [r5, #3472]
	cmp	lr, r2
	beq	.L1210
	add	lr, r0, lr
	movt	r1, 18374
.L1201:
	lsr	r3, r1, #2
	ldrb	r2, [r0], #1	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	lr, r0
	add	r3, r3, r2
	eor	r1, r1, r3
	bne	.L1201
.L1200:
	mov	r3, #1
	str	r1, [ip, #12]
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3464]
	cmn	r2, #1
	beq	.L1219
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1208
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L1209
.L1208:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1195
.L1196:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
	ldrh	r3, [r4]
	b	.L1197
.L1217:
	mov	r2, #700
	ldr	r1, .L1221+8
	mov	r0, fp
	bl	sftl_printk
	ldrh	r3, [r4]
	ldrh	r2, [r4, #10]
	cmp	r2, r3
	bhi	.L1199
.L1218:
	movw	r2, #701
	ldr	r1, .L1221+8
	mov	r0, fp
	bl	sftl_printk
	ldrh	r3, [r4]
	b	.L1199
.L1219:
	movw	r0, #:lower16:.LC100
	ldr	r1, [r5, #3468]
	movt	r0, #:upper16:.LC100
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	ldr	r2, [sp]
	cmp	r3, #2
	ldrhls	r3, [r6]
	add	r2, r2, #1
	uxth	r2, r2
	subls	r3, r3, #1
	str	r2, [sp]
	strhls	r3, [r4, #2]	@ movhi
	ldr	r3, [sp]
	cmp	r3, #3
	bhi	.L1220
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1195
.L1215:
	b	.L1215
.L1210:
	movt	r1, 18374
	b	.L1200
.L1209:
	ldr	r2, [r5, #3468]
	ldr	r3, [r4, #24]
	ldr	r1, [sp, #4]
	str	r2, [r3, r1, lsl #2]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1220:
	movw	r0, #:lower16:.LC101
	mov	r2, r3
	ldr	r1, [r5, #3468]
	movt	r0, #:upper16:.LC101
	bl	sftl_printk
.L1205:
	b	.L1205
.L1222:
	.align	2
.L1221:
	.word	.LANCHOR0+318
	.word	.LANCHOR0+3464
	.word	.LANCHOR1+348
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	load_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, r0
	ldr	r8, .L1235
	mov	r5, r1
	ldrh	r3, [r8]
	cmp	r3, r0
	bcc	.L1233
.L1224:
	movw	r4, #:lower16:.LANCHOR0
	add	r5, r5, r5, lsl #1
	movt	r4, #:upper16:.LANCHOR0
	ldr	r3, [r4, #2604]
	ldr	r7, [r3, r6, lsl #2]
	ldr	r3, [r4, #2532]
	cmp	r7, #0
	beq	.L1234
	lsl	r5, r5, #2
	ldr	r0, [r4, #3384]
	mov	r2, #1
	str	r7, [r4, #3468]
	add	r3, r3, r5
	mov	r1, r2
	ldr	r3, [r3, #8]
	str	r0, [r4, #3476]
	ldr	r0, .L1235+4
	str	r3, [r4, #3472]
	bl	FlashReadPages
	ldr	r9, [r4, #3476]
	ldrh	r3, [r9, #8]
	cmp	r3, r6
	beq	.L1227
	movw	r0, #:lower16:.LC102
	mov	r2, r7
	movt	r0, #:upper16:.LC102
	mov	r1, r6
	bl	sftl_printk
	mov	r3, #4
	movw	r0, #:lower16:.LC89
	mov	r2, r3
	movt	r0, #:upper16:.LC89
	ldr	r1, [r4, #3476]
	bl	rknand_print_hex
	movw	r0, #:lower16:.LC103
	ldrh	r3, [r8]
	movt	r0, #:upper16:.LC103
	mov	r2, #4
	ldr	r1, [r4, #2604]
	bl	rknand_print_hex
.L1228:
	ldrh	r3, [r9, #8]
	cmp	r3, r6
	beq	.L1230
	movw	r0, #:lower16:.LC8
	mov	r2, #508
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1235+8
	bl	sftl_printk
.L1230:
	ldr	r3, [r4, #2532]
	mov	r1, #0
	mov	r0, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1227:
	ldr	r3, [r4, #3464]
	cmp	r3, #256
	bne	.L1230
	movw	r0, #:lower16:.LC104
	mov	r2, r7
	mov	r1, r6
	movt	r0, #:upper16:.LC104
	bl	sftl_printk
	ldr	r3, [r4, #2532]
	mov	r1, r6
	ldr	r0, .L1235+12
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1228
.L1234:
	lsl	r5, r5, #2
	movw	r2, #318
	ldrh	r2, [r4, r2]
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	memset
	ldr	r3, [r4, #2532]
	mov	r0, #0
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #2532]
	add	r5, r3, r5
	str	r7, [r5, #4]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1233:
	movw	r0, #:lower16:.LC8
	movw	r2, #485
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1235+8
	bl	sftl_printk
	b	.L1224
.L1236:
	.align	2
.L1235:
	.word	.LANCHOR0+340
	.word	.LANCHOR0+3464
	.word	.LANCHOR1+364
	.word	.LANCHOR0+2548
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r4, [r0, #12]
	ldr	r5, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r6, #10]
	ldrh	r2, [r6, #8]
	sub	r3, r3, #5
	cmp	r2, r3
	blt	.L1272
	uxth	r3, r0
	lsl	r3, r3, #1
	ldrh	r7, [r4, r3]
	cmp	r7, #0
	bne	.L1266
.L1272:
	ldr	r10, .L1276
.L1238:
	ldrh	r3, [r10]
	ldrh	r2, [r6, #2]
	cmp	r2, r3
	bcc	.L1261
	mov	r0, r6
	bl	ftl_map_blk_alloc_new_blk
.L1261:
	mov	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1266:
	ldr	r2, [r6, #32]
	ldr	r10, .L1276
	cmp	r2, #0
	bne	.L1238
	mov	r1, #1
	str	r1, [r6, #32]
	strh	r2, [r4, r3]	@ movhi
	ldrh	r1, [r6, #2]
	ldrh	r2, [r10]
	ldrh	r3, [r6, #8]
	cmp	r1, r2
	sub	r3, r3, #1
	strh	r3, [r6, #8]	@ movhi
	bcs	.L1274
.L1239:
	ldrh	r2, [r6, #6]
	cmp	r2, #0
	movwne	r9, #:lower16:.LANCHOR0
	movne	r4, #0
	movtne	r9, #:upper16:.LANCHOR0
	bne	.L1249
	b	.L1250
.L1243:
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r2, r4
	bls	.L1250
.L1249:
	ldr	r3, [r5, r4, lsl #2]
	add	r8, r5, r4, lsl #2
	cmp	r7, r3, lsr #10
	bne	.L1243
	ldr	r3, [r9, #3356]
	mov	r2, #1
	ldr	fp, [r9, #3384]
	mov	r1, r2
	ldr	r0, .L1276+4
	str	r3, [r9, #3472]
	str	fp, [r9, #3476]
	ldr	r3, [r5, r4, lsl #2]
	str	r3, [r9, #3468]
	bl	FlashReadPages
	ldrh	r3, [fp, #8]
	movw	r0, #:lower16:.LC8
	movw	r2, #611
	ldr	r1, .L1276+8
	movt	r0, #:upper16:.LC8
	cmp	r3, r4
	bne	.L1275
	ldr	r3, [r9, #3464]
	cmn	r3, #1
	beq	.L1247
.L1252:
	ldrh	r2, [fp]
	mov	r1, r4
	ldrh	r3, [r6, #4]
	mov	r0, r6
	cmp	r2, r3
	bne	.L1247
	ldr	r2, [r9, #3472]
	add	r4, r4, #1
	bl	FtlMapWritePage
	ldrh	r2, [r6, #6]
	uxth	r4, r4
	cmp	r2, r4
	bhi	.L1249
.L1250:
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1241
	ldr	r3, .L1276+12
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	beq	.L1241
	mov	r0, r7
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn.part.10
.L1241:
	mov	r3, #0
	str	r3, [r6, #32]
	b	.L1238
.L1275:
	bl	sftl_printk
	ldr	r3, [r9, #3464]
	cmn	r3, #1
	beq	.L1247
	ldrh	r3, [fp, #8]
	cmp	r3, r4
	beq	.L1252
.L1247:
	mov	r3, #0
	str	r3, [r8]
.L1246:
	b	.L1246
.L1274:
	mov	r0, r6
	bl	ftl_map_blk_alloc_new_blk
	b	.L1239
.L1277:
	.align	2
.L1276:
	.word	.LANCHOR0+312
	.word	.LANCHOR0+3464
	.word	.LANCHOR1+380
	.word	.LANCHOR0+416
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r2, [r0, #24]
	.pad #44
	sub	sp, sp, #44
	movw	r5, #:lower16:.LANCHOR0
	mov	r1, #0
	ldrh	r10, [r0, #6]
	movt	r5, #:upper16:.LANCHOR0
	str	r0, [sp, #8]
	mov	r0, r2
	str	r2, [sp, #24]
	ldrh	r2, [r4, #8]
	ldr	r3, [r4, #16]
	mov	r6, r2
	str	r2, [sp, #20]
	ldr	r2, [r4, #12]
	str	r3, [sp, #36]
	str	r2, [sp, #28]
	lsl	r2, r10, #2
	bl	memset
	ldr	r3, [r5, #3352]
	mov	r2, #1
	ldr	r7, [r5, #3384]
	cmp	r6, #0
	str	r2, [r4, #36]
	mvn	r2, #0
	str	r3, [r5, #3472]
	mov	r3, #0
	str	r7, [r5, #3476]
	str	r3, [r4, #32]
	strh	r2, [r4]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	str	r3, [r4, #28]
	beq	.L1333
	ldr	r2, [sp, #20]
	subs	r2, r2, #1
	str	r2, [sp, #32]
	beq	.L1307
	add	r6, r5, #312
	str	r3, [sp, #12]
	add	r8, r6, #3152
	str	r3, [sp, #16]
.L1284:
	ldr	ip, [r5, #3352]
	mov	r2, #1
	ldr	r3, [sp, #16]
	mov	r1, r2
	str	ip, [r5, #3472]
	ldr	ip, [sp, #28]
	lsl	r0, r3, #1
	ldrh	r3, [r6]
	add	r9, ip, r0
	ldrh	ip, [ip, r0]
	sub	r3, r3, #1
	mov	r0, r8
	orr	r3, r3, ip, lsl #10
	str	r3, [r5, #3468]
	bl	FlashReadPages
	ldr	r3, [r5, #3464]
	cmn	r3, #1
	beq	.L1292
	ldr	r3, [sp, #8]
	ldrh	r2, [r7]
	ldrh	r3, [r3, #4]
	cmp	r2, r3
	beq	.L1334
.L1292:
	ldrh	r3, [r6]
	cmp	r3, #0
	movne	fp, #0
	movne	r4, fp
	beq	.L1296
.L1304:
	ldrh	r3, [r9]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	orr	r3, fp, r3, lsl #10
	str	r3, [r5, #3468]
	bl	FlashReadPages
	ldr	r3, [r5, #3476]
	ldr	ip, [r3, #12]
	ldr	r3, [r5, #3464]
	cmp	ip, #0
	beq	.L1299
	cmn	r3, #1
	beq	.L1306
	ldr	r3, .L1336
	movw	r1, #42982
	ldr	r0, [r5, #3472]
	ldrh	lr, [r3]
	cmp	lr, #0
	beq	.L1309
	add	lr, r0, lr
	movt	r1, 18374
.L1302:
	lsr	r3, r1, #2
	ldrb	r2, [r0], #1	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	lr, r0
	add	r3, r3, r2
	eor	r1, r1, r3
	bne	.L1302
.L1301:
	cmp	ip, r1
	beq	.L1303
	movw	r0, #:lower16:.LC106
	str	r1, [sp, #4]
	mov	r3, fp
	str	ip, [sp]
	movt	r0, #:upper16:.LC106
	ldr	r2, [sp, #12]
	ldr	r1, .L1336+4
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3464]
.L1306:
	add	r4, r4, #1
	ldrh	r3, [r6]
	sxth	r4, r4
	cmp	r4, r3
	mov	fp, r4
	blt	.L1304
.L1296:
	ldr	r3, [sp, #16]
	ldr	r1, [sp, #20]
	add	r3, r3, #1
	uxth	r3, r3
	sxth	r2, r3
	cmp	r2, r1
	str	r2, [sp, #12]
	str	r2, [sp, #16]
	bge	.L1285
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L1284
	mov	r4, r3
	lsl	r8, r1, #2
	lsl	r3, r1, #1
.L1282:
	ldr	r2, [sp, #28]
	mov	r1, #1
	ldrh	r0, [r2, r3]
	add	r3, r2, r3
	str	r3, [sp, #16]
	bl	FtlGetLastWrittenPage
	ldr	r2, [sp, #8]
	sxth	r9, r0
	ldr	r1, [sp, #36]
	add	r0, r0, #1
	add	r9, r9, #1
	strh	r4, [r2]	@ movhi
	cmp	r9, #0
	strh	r0, [r2, #2]	@ movhi
	ldr	r3, [r1, r8]
	str	r3, [r2, #28]
	ble	.L1285
	str	r6, [sp, #20]
	mov	r8, #0
	ldr	fp, .L1336+8
	mov	r4, r8
	ldr	r6, [sp, #16]
.L1291:
	ldrh	r3, [r6]
	mov	r2, #1
	mov	r1, r2
	mov	r0, fp
	orr	r3, r8, r3, lsl #10
	str	r3, [r5, #3468]
	bl	FlashReadPages
	ldr	r3, [r5, #3476]
	ldr	ip, [r3, #12]
	ldr	r3, [r5, #3464]
	cmp	ip, #0
	beq	.L1286
	cmn	r3, #1
	beq	.L1305
	ldr	r3, .L1336
	movw	r1, #42982
	ldr	r0, [r5, #3472]
	ldrh	lr, [r3]
	cmp	lr, #0
	beq	.L1308
	add	lr, r0, lr
	movt	r1, 18374
.L1289:
	lsr	r3, r1, #2
	ldrb	r2, [r0], #1	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	r0, lr
	add	r3, r3, r2
	eor	r1, r1, r3
	bne	.L1289
.L1288:
	cmp	ip, r1
	beq	.L1290
	movw	r0, #:lower16:.LC105
	str	r1, [sp, #4]
	mov	r3, r8
	str	ip, [sp]
	movt	r0, #:upper16:.LC105
	ldr	r2, [sp, #12]
	ldr	r1, .L1336+4
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3464]
.L1305:
	add	r4, r4, #1
	sxth	r4, r4
	cmp	r4, r9
	mov	r8, r4
	blt	.L1291
	ldr	r6, [sp, #20]
.L1285:
	ldr	r4, [sp, #8]
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r6]
	ldrh	r2, [r4, #2]
	cmp	r2, r3
	beq	.L1335
.L1281:
	ldr	r4, [sp, #8]
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1299:
	cmn	r3, #1
	beq	.L1306
.L1303:
	ldrh	r3, [r7, #8]
	cmp	r10, r3
	bls	.L1306
	ldr	r2, [sp, #8]
	ldrh	r1, [r7]
	ldrh	r2, [r2, #4]
	cmp	r1, r2
	ldreq	r2, [r5, #3468]
	ldreq	r1, [sp, #24]
	streq	r2, [r1, r3, lsl #2]
	b	.L1306
.L1309:
	movt	r1, 18374
	b	.L1301
.L1334:
	ldrh	r2, [r7, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L1292
	ldrh	r3, [r6]
	sub	r3, r3, #1
	cmp	r3, #0
	ble	.L1296
	ldr	r0, [sp, #24]
	mov	ip, #0
	mov	r1, ip
.L1298:
	ldr	r2, [r5, #3352]
	lsl	lr, ip, #3
	add	r1, r1, #1
	sxth	r1, r1
	ldr	r3, [r2, ip, lsl #3]
	add	r2, r2, lr
	mov	ip, r1
	uxth	lr, r3
	cmp	r10, lr
	ldrhi	r2, [r2, #4]
	strhi	r2, [r0, lr, lsl #2]
	ldrh	r3, [r6]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L1298
	b	.L1296
.L1286:
	cmn	r3, #1
	beq	.L1305
.L1290:
	ldrh	r3, [r7, #8]
	cmp	r10, r3
	bls	.L1305
	ldr	r2, [sp, #8]
	ldrh	r1, [r7]
	ldrh	r2, [r2, #4]
	cmp	r1, r2
	ldreq	r2, [r5, #3468]
	ldreq	r1, [sp, #24]
	streq	r2, [r1, r3, lsl #2]
	b	.L1305
.L1335:
	ldr	r0, [sp, #8]
	bl	ftl_map_blk_alloc_new_blk
	b	.L1281
.L1308:
	movt	r1, 18374
	b	.L1288
.L1333:
	add	r6, r5, #312
	b	.L1285
.L1307:
	ldr	r3, [sp, #32]
	add	r6, r5, #312
	str	r3, [sp, #12]
	mov	r8, r3
	mov	r4, r3
	b	.L1282
.L1337:
	.align	2
.L1336:
	.word	.LANCHOR0+318
	.word	.LANCHOR1+396
	.word	.LANCHOR0+3464
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movt	r3, #:upper16:.LANCHOR0
	movw	r1, #61574
	add	r4, r3, #348
	add	ip, r3, #328
	ldr	r5, [r3, #4]
	add	r2, r3, #3488
	ldr	lr, [r3, #3408]
	movt	r1, 65535
	ldrh	r4, [r4]
	mov	r0, r2
	ldrh	ip, [ip]
	ldr	r6, [r3, #3404]
	str	r5, [r3, #3500]
	str	lr, [r3, #3504]
	ldr	r5, [r3, #3412]
	ldrh	lr, [r3]
	str	r6, [r3, #3508]
	str	r5, [r3, #3512]
	strh	r4, [r2, #8]	@ movhi
	strh	lr, [r2, #10]	@ movhi
	strh	ip, [r2, #6]	@ movhi
	strh	r1, [r2, #4]	@ movhi
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L1342
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR0+2548
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	add	r0, r0, r0, lsl #1
	ldr	r3, [r5, #2532]
	lsl	r4, r0, #2
	add	r0, r5, #2544
	add	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2532]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r4, r5, r6, pc}
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r6, #:lower16:.LANCHOR0
	movt	r6, #:upper16:.LANCHOR0
	mov	r9, r1
	add	r1, r6, #316
	.pad #12
	sub	sp, sp, #12
	ldr	r3, [r6, #2608]
	mov	r7, r0
	mov	r8, r2
	ldrh	r10, [r1]
	cmp	r0, r3
	bcs	.L1347
.L1351:
	movw	r3, #342
	add	r10, r10, #7
	ldrh	r3, [r6, r3]
	lsr	r5, r7, r10
	uxth	r5, r5
	cmp	r3, #0
	beq	.L1349
	ldr	r2, [r6, #2532]
	ldrh	r1, [r2]
	cmp	r1, r5
	beq	.L1367
	sub	r3, r3, #1
	mov	r4, #12
	uxth	r3, r3
	add	r3, r3, #1
	add	r3, r3, r3, lsl #1
	lsl	r3, r3, #2
	b	.L1355
.L1358:
	ldrh	r0, [r2, r4]
	cmp	r0, r5
	beq	.L1352
	mov	r4, r1
.L1355:
	cmp	r4, r3
	add	ip, r2, r4
	add	r1, r4, #12
	bne	.L1358
.L1349:
	bl	select_l2p_ram_region
	add	ip, r0, r0, lsl #1
	ldr	r3, [r6, #2532]
	movw	r2, #65535
	lsl	fp, ip, #2
	mov	r1, r0
	ldrh	ip, [r3, fp]
	mov	r4, fp
	add	r3, r3, fp
	cmp	ip, r2
	beq	.L1359
	ldr	r3, [r3, #4]
	cmp	r3, #0
	blt	.L1368
.L1359:
	mov	r0, r5
	bl	load_l2p_region
	ldr	ip, [r6, #2532]
	add	ip, ip, fp
.L1352:
	mvn	r3, #0
	cmp	r8, #0
	bic	r7, r7, r3, lsl r10
	uxth	r7, r7
	bne	.L1356
	ldr	r3, [ip, #8]
	ldr	r3, [r3, r7, lsl #2]
	str	r3, [r9]
	ldr	r3, [r6, #2532]
	add	r4, r3, r4
	ldr	r3, [r4, #4]
.L1357:
	cmn	r3, #1
	moveq	r0, #0
	movne	r0, #0
	addne	r3, r3, #1
	strne	r3, [r4, #4]
.L1346:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1347:
	movw	r0, #:lower16:.LC8
	movw	r2, #813
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1369
	bl	sftl_printk
	ldr	r3, [r6, #2608]
	cmp	r7, r3
	bcc	.L1351
	mvn	r0, #0
	cmp	r8, #0
	streq	r0, [r9]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1356:
	ldr	r1, [r9]
	movw	r2, #2536
	ldr	r3, [ip, #8]
	str	r1, [r3, r7, lsl #2]
	ldr	r3, [r6, #2532]
	strh	r5, [r6, r2]	@ movhi
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	orr	r3, r3, #-2147483648
	str	r3, [r4, #4]
	b	.L1357
.L1368:
	str	r0, [sp, #4]
	bl	flush_l2p_region
	ldr	r1, [sp, #4]
	b	.L1359
.L1367:
	mov	ip, r2
	mov	r4, #0
	b	.L1352
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR1+416
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r4, #:lower16:.LANCHOR0
	ubfx	r2, r1, #10, #16
	movt	r4, #:upper16:.LANCHOR0
	movw	r3, #306
	.pad #12
	sub	sp, sp, #12
	str	r1, [sp, #4]
	mov	r5, r0
	ldrh	r1, [r4, r3]
	mov	r0, r2
	bl	__aeabi_uidivmod
	add	r3, r4, #264
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r7, r0
	ldr	r3, [r4, #16]
	lsl	r6, r7, #1
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	addne	r2, r2, #1
	strhne	r2, [r3, r6]	@ movhi
	bne	.L1373
	ldr	r1, [r4, #2524]
	cmp	r1, #0
	beq	.L1373
	ldr	ip, [r4, #8]
	movw	r3, #43691
	movt	r3, 43690
	ldrh	lr, [r4, #236]
	sub	r1, r1, ip
	asr	r1, r1, #1
	cmp	lr, #0
	mul	r3, r3, r1
	uxth	r1, r3
	beq	.L1373
	cmp	r1, r7
	beq	.L1374
	add	r3, r1, r1, lsl #1
	movw	r0, #65535
	lsl	r3, r3, #1
	ldrh	r1, [ip, r3]
	cmp	r1, r0
	bne	.L1378
	b	.L1373
.L1379:
	cmp	r7, r1
	beq	.L1374
	ldrh	r1, [ip, r3]
	cmp	r1, r0
	beq	.L1373
.L1378:
	add	r2, r2, #1
	add	r3, r1, r1, lsl #1
	uxth	r2, r2
	lsl	r3, r3, #1
	cmp	lr, r2
	bne	.L1379
.L1373:
	mov	r0, r5
	mov	r2, #1
	add	r1, sp, #4
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1374:
	mov	r1, r7
	ldr	r0, .L1388
	bl	List_remove_node
	ldrh	r3, [r4, #236]
	cmp	r3, #0
	bne	.L1377
	movw	r0, #:lower16:.LC8
	movw	r2, #1733
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1388+4
	bl	sftl_printk
	ldrh	r3, [r4, #236]
.L1377:
	sub	r3, r3, #1
	mov	r0, r7
	strh	r3, [r4, #236]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r4, #16]
	ldrh	r3, [r2, r6]
	add	r3, r3, #1
	strh	r3, [r2, r6]	@ movhi
	b	.L1373
.L1389:
	.align	2
.L1388:
	.word	.LANCHOR0+2524
	.word	.LANCHOR1+428
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:__stack_chk_guard
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	r3, #:upper16:__stack_chk_guard
	.pad #20
	sub	sp, sp, #20
	movw	r0, #:lower16:.LC107
	movw	r6, #:lower16:.LANCHOR0
	str	r3, [sp, #4]
	movt	r0, #:upper16:.LC107
	ldr	r3, [r3]
	movt	r6, #:upper16:.LANCHOR0
	ldr	r1, .L1431
	str	r3, [sp, #12]
	bl	sftl_printk
	movw	r0, #:lower16:check_vpc_table
	mov	r2, #8192
	movt	r0, #:upper16:check_vpc_table
	mov	r1, #0
	bl	memset
	ldr	r3, [r6, #2608]
	cmp	r3, #0
	beq	.L1396
	ldr	r7, .L1431+4
	movw	r5, #:lower16:check_vpc_table
	movt	r5, #:upper16:check_vpc_table
	mov	r4, #0
.L1395:
	mov	r2, #0
	add	r1, sp, #8
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #8]
	cmn	r0, #1
	beq	.L1394
	ubfx	r0, r0, #10, #16
	ldrh	r1, [r7]
	bl	__aeabi_uidivmod
	ldr	r3, .L1431+8
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L1394:
	ldr	r3, [r6, #2608]
	add	r4, r4, #1
	cmp	r3, r4
	bhi	.L1395
.L1396:
	ldrh	r8, [r6, #20]
	cmp	r8, #0
	beq	.L1429
	mov	r8, #0
	movw	r7, #:lower16:check_vpc_table
	movw	r9, #:lower16:.LC108
	mov	r4, r8
	movt	r7, #:upper16:check_vpc_table
	movt	r9, #:upper16:.LC108
	movw	r10, #65535
.L1398:
	ldr	r2, [r6, #16]
	lsl	r5, r4, #1
	ldrh	r3, [r7, r5]
	ldrh	r2, [r2, r5]
	cmp	r2, r3
	beq	.L1397
	mov	r1, r4
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r6, #16]
	ldrh	r3, [r3, r5]
	cmp	r3, r10
	beq	.L1397
	ldrh	r2, [r7, r5]
	cmp	r2, r3
	movhi	r8, #1
.L1397:
	add	r4, r4, #1
	ldrh	r3, [r6, #20]
	uxth	r4, r4
	cmp	r3, r4
	bhi	.L1398
	ldr	r3, [r6, #2524]
	cmp	r3, #0
	beq	.L1399
	ldr	r2, [r6, #8]
	movw	r4, #43691
	movt	r4, 43690
	ldrh	r9, [r6, #236]
	sub	r3, r3, r2
	asr	r3, r3, #1
	cmp	r9, #0
	mul	r4, r4, r3
	uxth	r4, r4
	beq	.L1399
.L1405:
	movw	r3, #:lower16:.LC109
	movw	fp, #:lower16:check_vpc_table
	movt	r3, #:upper16:.LC109
	movt	fp, #:upper16:check_vpc_table
	str	r3, [sp]
	mov	r7, #0
	movw	r10, #65535
	b	.L1401
.L1400:
	add	r5, r5, r4
	ldr	r3, [r6, #8]
	lsl	r5, r5, #1
	ldrh	r4, [r3, r5]
	cmp	r4, r10
	beq	.L1399
	add	r7, r7, #1
	uxth	r3, r7
	cmp	r9, r3
	bls	.L1399
.L1401:
	ldr	r3, [r6, #16]
	lsl	r5, r4, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	beq	.L1400
	ldrh	r3, [fp, r5]
	mov	r1, r4
	ldr	r0, [sp]
	mov	r8, #1
	bl	sftl_printk
	b	.L1400
.L1399:
	cmp	r8, #0
	beq	.L1390
	movw	r0, #:lower16:.LC8
	movw	r2, #2383
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1431
	bl	sftl_printk
.L1390:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L1430
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1429:
	ldr	r3, [r6, #2524]
	cmp	r3, #0
	beq	.L1390
	ldr	r1, [r6, #8]
	movw	r2, #43691
	movt	r2, 43690
	ldrh	r9, [r6, #236]
	sub	r3, r3, r1
	asr	r3, r3, #1
	cmp	r9, #0
	mul	r3, r2, r3
	uxth	r4, r3
	bne	.L1405
	b	.L1390
.L1430:
	bl	__stack_chk_fail
.L1432:
	.align	2
.L1431:
	.word	.LANCHOR1+444
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r9, #:lower16:__stack_chk_guard
	movt	r9, #:upper16:__stack_chk_guard
	movw	r0, #:lower16:.LC110
	movw	r4, #:lower16:.LANCHOR0
	.pad #32
	sub	sp, sp, #32
	ldr	r3, [r9]
	movt	r0, #:upper16:.LC110
	mov	r1, #0
	movt	r4, #:upper16:.LANCHOR0
	str	r3, [sp, #28]
	bl	sftl_printk
	ldr	r3, [r4, #2608]
	cmp	r3, #0
	beq	.L1433
	movw	r8, #:lower16:.LC111
	movw	r6, #:lower16:.LC112
	ldr	r7, .L1446
	movt	r8, #:upper16:.LC111
	movt	r6, #:upper16:.LC112
	mov	r5, #0
	b	.L1440
.L1435:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L1437
	ldr	lr, [r4, #3352]
	mov	ip, #0
	ldr	r10, [r4, #3384]
	mov	r2, ip
	mov	r1, #1
	mov	r0, r7
	str	r3, [r4, #3468]
	str	r5, [r4, #3480]
	str	lr, [r4, #3472]
	str	r10, [r4, #3476]
	str	ip, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3464]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1438
	ldr	r3, [r10, #8]
	cmp	r3, r5
	beq	.L1437
.L1438:
	ldr	r2, [r4, #3472]
	mov	r1, r5
	ldr	ip, [r4, #3476]
	mov	r0, r6
	ldr	lr, [r2, #4]
	ldr	r3, [ip]
	str	lr, [sp, #16]
	ldr	lr, [r2]
	ldr	r2, [r4, #3468]
	str	lr, [sp, #12]
	ldr	lr, [ip, #12]
	str	lr, [sp, #8]
	ldr	lr, [ip, #8]
	str	lr, [sp, #4]
	ldr	ip, [ip, #4]
	str	ip, [sp]
	bl	sftl_printk
.L1437:
	ldr	r3, [r4, #2608]
	add	r5, r5, #1
	cmp	r5, r3
	bcs	.L1433
.L1440:
	mov	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1435
	ldr	r2, [sp, #24]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
	b	.L1435
.L1433:
	ldr	r2, [sp, #28]
	ldr	r3, [r9]
	cmp	r2, r3
	bne	.L1445
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1445:
	bl	__stack_chk_fail
.L1447:
	.align	2
.L1446:
	.word	.LANCHOR0+3464
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR2
	movw	r2, #:lower16:__stack_chk_guard
	movt	r3, #:upper16:.LANCHOR2
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	r2, #:upper16:__stack_chk_guard
	ldrh	r10, [r3, #4]
	.pad #68
	sub	sp, sp, #68
	mov	ip, r2
	str	r3, [sp, #24]
	str	r2, [sp, #28]
	movw	r2, #65535
	ldr	r3, [ip]
	cmp	r10, r2
	str	r0, [sp, #8]
	str	r1, [sp, #20]
	str	r3, [sp, #60]
	beq	.L1477
	cmp	r10, #0
	beq	.L1450
.L1449:
	movw	fp, #:lower16:.LANCHOR0
	movw	r3, #310
	movt	fp, #:upper16:.LANCHOR0
	ldr	r2, [sp, #20]
	ldrh	r3, [fp, r3]
	cmp	r3, r2
	beq	.L1450
.L1451:
	ldr	r0, [sp, #8]
	movw	r2, #65535
	mov	r3, #0
	ldrh	r1, [r0]
	strb	r3, [r0, #8]
	cmp	r1, r2
	beq	.L1452
	movw	fp, #:lower16:.LANCHOR0
	str	r3, [sp, #16]
	movt	fp, #:upper16:.LANCHOR0
.L1475:
	ldrh	r2, [fp, #36]
	cmp	r2, #0
	beq	.L1453
.L1498:
	sub	r6, r2, #1
	ldr	r2, [fp, #3232]
	ldr	r3, .L1499
	uxth	r6, r6
	ldr	r0, [fp, #3336]
	mov	r4, #0
	str	r2, [sp, #4]
	add	r6, r6, #8
	ldr	r2, [sp, #8]
	sub	r3, r3, #2
	ldrh	r9, [r3, #2]
	movw	r7, #65535
	ldrh	r3, [r3]
	ldr	r8, [fp, #3236]
	add	r5, r2, #14
	add	r6, r2, r6, lsl #1
	str	fp, [sp, #12]
	mov	lr, r3
.L1455:
	ldrh	r1, [r5, #2]!
	cmp	r1, r7
	beq	.L1454
	mul	r3, lr, r4
	add	r2, r4, r4, lsl #2
	add	fp, r4, #1
	orr	r1, r10, r1, lsl #10
	mul	ip, r9, r4
	add	r2, r0, r2, lsl #2
	uxth	r4, fp
	str	r1, [r2, #4]
	cmp	r3, #0
	add	fp, r3, #3
	add	r1, ip, #3
	movlt	r3, fp
	cmp	ip, #0
	bic	r3, r3, #3
	movlt	ip, r1
	ldr	r1, [sp, #4]
	bic	ip, ip, #3
	add	ip, r8, ip
	add	r3, r1, r3
	str	ip, [r2, #12]
	str	r3, [r2, #8]
.L1454:
	cmp	r5, r6
	bne	.L1455
	mov	r2, #0
	mov	r1, r4
	ldr	fp, [sp, #12]
	bl	FlashReadPages
	cmp	r4, #0
	beq	.L1457
	sub	r4, r4, #1
	mov	r5, #0
	uxth	r4, r4
	movw	r7, #65535
	add	r4, r4, #1
	add	r4, r4, r4, lsl #2
	lsl	r4, r4, #2
.L1471:
	ldr	r3, [fp, #3336]
	ldr	r6, [r3, r5]
	add	r3, r3, r5
	ldr	r8, [r3, #4]
	ldr	r9, [r3, #12]
	cmp	r6, #0
	bne	.L1459
	ldrh	r3, [r9]
	cmp	r3, r7
	beq	.L1460
	ldr	r0, [r9, #8]
	ldr	r3, [fp, #2608]
	cmp	r0, r3
	bhi	.L1460
	mov	r2, r6
	add	r1, sp, #36
	bl	log2phys
	ldr	r0, [r9, #12]
	ldr	r3, [sp, #36]
	sub	r2, r0, r3
	cmn	r3, #1
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1463
	ldr	ip, [fp, #3368]
	mov	r2, r6
	ldr	r3, [fp, #3388]
	mov	r1, #1
	str	r0, [sp, #44]
	add	r0, sp, #40
	str	ip, [sp, #48]
	str	r3, [sp, #52]
	bl	FlashReadPages
	ldr	r3, .L1499+4
	ldrh	ip, [r3]
	lsls	ip, ip, #7
	beq	.L1493
	ldr	r2, [fp, #3336]
	ldr	r3, [sp, #48]
	add	r2, r2, r5
	ldr	r1, [r2, #8]
	ldr	r2, [r3]
	ldr	r0, [r1]
	cmp	r0, r2
	bne	.L1465
	mov	r2, r6
	b	.L1467
.L1468:
	ldr	lr, [r1, #4]!
	ldr	r0, [r3, #4]!
	cmp	lr, r0
	bne	.L1465
.L1467:
	add	r2, r2, #1
	cmp	r2, ip
	bne	.L1468
.L1493:
	ldr	r0, [r9, #12]
.L1463:
	ldr	r2, [r9, #8]
	mov	r1, r8
	bl	FtlGcUpdatePage
.L1469:
	add	r5, r5, #20
	cmp	r4, r5
	bne	.L1471
.L1457:
	ldr	r3, [sp, #16]
	ldr	r0, [sp, #20]
	add	r3, r3, #1
	mov	r1, r3
	str	r3, [sp, #16]
	cmp	r0, r1
	add	r3, r10, #1
	uxth	r10, r3
	bhi	.L1473
	ldr	r0, [sp, #24]
	movw	r2, #65535
	ldrh	r3, [r0, #4]
	cmp	r3, r2
	beq	.L1473
	movw	r2, #310
	add	r3, r3, r1
	ldrh	r2, [fp, r2]
	strh	r3, [r0, #4]	@ movhi
	cmp	r2, r10
	bhi	.L1474
.L1452:
	ldr	r3, [sp, #8]
	mov	r2, #0
	ldr	ip, [sp, #24]
	mov	r1, r10
	strh	r10, [r3, #2]	@ movhi
	mov	r0, r3
	strb	r2, [r3, #6]
	mvn	r3, #0
	strh	r3, [ip, #4]	@ movhi
	bl	ftl_sb_update_avl_pages
.L1474:
	ldr	r3, [sp, #28]
	mvn	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L1497
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1450:
	bl	FtlGcPageVarInit
	b	.L1451
.L1459:
	mov	r1, r8
	ldr	r0, [r9, #12]
	mvn	r2, #0
	bl	FtlGcUpdatePage
	b	.L1469
.L1465:
	ldr	r4, [sp, #8]
	movw	r0, #:lower16:.LC113
	ldr	r2, [sp, #44]
	movt	r0, #:upper16:.LC113
	ldrh	r1, [r4]
	bl	sftl_printk
.L1494:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [fp, #16]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [fp, #184]	@ movhi
	bl	FtlGcPageVarInit
	ldr	r0, [sp, #8]
	movw	r2, #65535
	mov	r3, #0
	mov	r10, r3
	ldrh	r1, [r0]
	strb	r3, [r0, #8]
	cmp	r1, r2
	beq	.L1452
	ldrh	r2, [fp, #36]
	cmp	r2, #0
	bne	.L1498
.L1453:
	mov	r1, r2
	ldr	r0, [fp, #3336]
	bl	FlashReadPages
	b	.L1457
.L1460:
	ldr	r4, [sp, #8]
	b	.L1494
.L1473:
	ldr	r3, .L1499+8
	ldrh	r3, [r3]
	cmp	r3, r10
	bhi	.L1475
	b	.L1452
.L1477:
	mov	r10, #0
	b	.L1449
.L1497:
	bl	__stack_chk_fail
.L1500:
	.align	2
.L1499:
	.word	.LANCHOR0+320
	.word	.LANCHOR0+266
	.word	.LANCHOR0+310
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r6, #:lower16:__stack_chk_guard
	movt	r6, #:upper16:__stack_chk_guard
	.pad #100
	sub	sp, sp, #100
	ldr	r5, [r4, #2776]
	ldr	r3, [r6]
	cmp	r5, #0
	str	r3, [sp, #92]
	beq	.L1502
	ldr	r3, [r4, #2780]
	ldr	r2, [r4, #2608]
	cmp	r3, r2
	movcc	r5, #2048
	bcs	.L1523
.L1506:
	mov	r0, r3
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r1, [sp, #4]
	ldr	r3, [r4, #2780]
	cmn	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #2780]
	bne	.L1524
	subs	r5, r5, #1
	beq	.L1508
	ldr	r2, [r4, #2608]
	cmp	r2, r3
	bhi	.L1506
.L1508:
	mvn	r0, #0
	b	.L1501
.L1502:
	ldr	r9, [r4, #2612]
	movw	r7, #10000
	ldr	r1, [r4, #2668]
	ldr	r8, [r4, #2772]
	add	r3, r9, #1048576
	cmp	r1, r7
	movls	r7, #63
	movhi	r7, #31
	cmp	r8, r3
	bls	.L1512
.L1514:
	ldr	r3, .L1526
	ldrh	r2, [r3, #28]
.L1513:
	mov	r3, #0
	mov	r1, #1
	mov	r0, r3
	str	r2, [r4, #2796]
	str	r9, [r4, #2772]
	str	r1, [r4, #2776]
	str	r3, [r4, #2780]
.L1501:
	ldr	r2, [sp, #92]
	ldr	r3, [r6]
	cmp	r2, r3
	bne	.L1525
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1512:
	ldr	r3, [r4, #2608]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r9, r0
	bhi	.L1514
	ldr	r3, .L1526
	ldrh	r2, [r3, #28]
	ands	r1, r7, r2
	movne	r0, r5
	bne	.L1501
	ldr	r3, [r4, #2796]
	cmp	r2, r3
	moveq	r0, r1
	bne	.L1513
	b	.L1501
.L1523:
	ldr	r2, [r4, #2612]
	mov	r3, #0
	str	r3, [r4, #2776]
	mov	r0, r3
	str	r3, [r4, #2780]
	str	r2, [r4, #2772]
	b	.L1501
.L1524:
	ldr	lr, [r4, #3376]
	mov	ip, #0
	add	r0, sp, #96
	mov	r2, ip
	str	ip, [r0, #-88]!
	add	ip, sp, #28
	str	r1, [sp, #12]
	mov	r1, #1
	str	r3, [sp, #24]
	str	lr, [sp, #16]
	str	ip, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L1508
	ldr	r0, [sp, #4]
	movw	r3, #306
	ldrh	r1, [r4, r3]
	ubfx	r0, r0, #10, #16
	bl	__aeabi_uidivmod
	ldr	r3, .L1526+4
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	FtlGcRefreshBlock
	b	.L1508
.L1525:
	bl	__stack_chk_fail
.L1527:
	.align	2
.L1526:
	.word	.LANCHOR0+2472
	.word	.LANCHOR0+264
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r6, #:lower16:.LANCHOR0
	movw	r3, #:lower16:__stack_chk_guard
	movt	r6, #:upper16:.LANCHOR0
	mov	ip, r3
	add	r3, r6, #304
	mov	r5, r1
	ldrh	r1, [r3]
	mov	r3, ip
	.pad #108
	sub	sp, sp, #108
	movt	r3, #:upper16:__stack_chk_guard
	mov	r8, r0
	str	r3, [sp, #12]
	add	r0, r0, r5
	ldr	r3, [r3]
	cmp	r0, r1
	str	r3, [sp, #100]
	mvnhi	r3, #0
	strhi	r3, [sp]
	bls	.L1541
.L1528:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #100]
	ldr	r0, [sp]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L1542
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1541:
	cmp	r5, #0
	beq	.L1537
	add	r3, r6, #316
	mov	fp, r2
	ldrh	r10, [r3]
	mov	r3, #0
	str	r3, [sp]
	lsr	r10, r8, r10
	lsl	r9, r10, #2
	b	.L1534
.L1543:
	ldr	lr, [r6, #3360]
	mov	r2, #1
	str	r0, [sp, #20]
	add	ip, sp, #36
	mov	r1, r2
	add	r0, sp, #16
	str	lr, [sp, #24]
	str	ip, [sp, #28]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
.L1532:
	ldr	r0, [r6, #3360]
	lsl	r2, r4, #9
	mov	r1, fp
	add	fp, fp, r2
	add	r8, r8, r7
	add	r9, r9, #4
	add	r0, r0, r3, lsl #9
	bl	memcpy
	mov	r1, r10
	ldr	r2, [r6, #3360]
	ldr	r0, .L1544
	add	r10, r10, #1
	bl	FtlMapWritePage
	cmn	r0, #1
	ldr	r3, [sp]
	mvneq	r3, #0
	subs	r5, r5, r7
	str	r3, [sp]
	beq	.L1528
.L1534:
	ldr	r3, .L1544+4
	mov	r0, r8
	ldrh	r2, [r3]
	ldr	r3, [r6, #3412]
	mov	r1, r2
	str	r2, [sp, #8]
	ldr	r3, [r3, r9]
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #8]
	mov	r3, r1
	ldr	r0, [sp, #4]
	str	r3, [sp, #4]
	sub	r4, r2, r1
	uxth	r4, r4
	mov	r1, r0
	cmp	r4, r5
	mov	r7, r4
	uxthhi	r4, r5
	movhi	r7, r4
	cmp	r0, #0
	cmpne	r4, r2
	movne	r1, #1
	moveq	r1, #0
	bne	.L1543
	ldr	r3, .L1544+8
	ldr	r0, [r6, #3360]
	ldrh	r2, [r3]
	bl	memset
	ldr	r3, [sp, #4]
	b	.L1532
.L1537:
	str	r5, [sp]
	b	.L1528
.L1542:
	bl	__stack_chk_fail
.L1545:
	.align	2
.L1544:
	.word	.LANCHOR0+3488
	.word	.LANCHOR0+266
	.word	.LANCHOR0+318
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data.part.17, %function
Ftl_save_ext_data.part.17:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movt	r3, #:upper16:.LANCHOR0
	mov	ip, #83
	movt	ip, 20480
	add	r2, r3, #2688
	ldr	lr, [r3, #2636]
	add	r2, r2, #8
	ldr	r6, [r3, #2632]
	mov	r1, #1
	ldr	r5, [r3, #2620]
	mov	r0, #0
	str	ip, [r3, #2700]
	ldr	r4, [r3, #2612]
	str	lr, [r3, #2784]
	ldr	ip, [r3, #2656]
	ldr	lr, [r3, #2628]
	ldr	r7, [r3, #2640]
	str	r6, [r3, #2704]
	str	r5, [r3, #2708]
	ldr	r6, [r3, #2664]
	ldr	r5, [r3, #2616]
	str	r4, [r3, #2712]
	str	lr, [r3, #2716]
	ldr	r4, [r3, #2624]
	ldr	lr, [r3, #2668]
	str	ip, [r3, #2724]
	ldr	ip, [r3, #2672]
	str	r7, [r3, #2788]
	str	r6, [r3, #2728]
	str	r5, [r3, #2732]
	str	r4, [r3, #2736]
	str	lr, [r3, #2740]
	str	ip, [r3, #2744]
	pop	{r4, r5, r6, r7, lr}
	b	FtlVendorPartWrite
	.fnend
	.size	Ftl_save_ext_data.part.17, .-Ftl_save_ext_data.part.17
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movw	r2, #19539
	movt	r3, #:upper16:.LANCHOR0
	movt	r2, 18004
	ldr	r3, [r3, #2696]
	cmp	r3, r2
	bxne	lr
	b	Ftl_save_ext_data.part.17
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r4, #:lower16:.LANCHOR0
	movw	r1, #3532
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r3, [r4, r1]
	cmp	r3, #31
	addls	r3, r3, #1
	movls	r2, #1
	strhls	r3, [r4, r1]	@ movhi
	movhi	r2, #32
	ldr	r3, [r4, #3396]
	cmp	r0, #0
	ldrne	r1, [r3, #16]
	bne	.L1553
	ldr	r0, [r3, #20]
	ldr	r1, [r3, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1554
.L1553:
	movw	r2, #17221
	ldr	ip, .L1559
	movt	r2, 16980
	str	r1, [r3, #20]
	str	r2, [r3]
	mov	r1, #0
	ldr	r3, [r4, #3396]
	mov	r0, #64
	ldrh	ip, [ip]
	ldr	lr, [r3, #8]
	mov	r2, r3
	str	r1, [r3, #4]
	mov	r1, ip
	lsl	ip, ip, #9
	add	lr, lr, #1
	str	ip, [r3, #12]
	str	lr, [r3, #8]
	bl	FtlVendorPartWrite
	ldr	r2, [r4, #2696]
	movw	r3, #19539
	movt	r3, 18004
	cmp	r2, r3
	beq	.L1558
.L1554:
	mov	r0, #0
	pop	{r4, pc}
.L1558:
	bl	Ftl_save_ext_data.part.17
	mov	r0, #0
	pop	{r4, pc}
.L1560:
	.align	2
.L1559:
	.word	.LANCHOR0+3392
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	sftl_vendor_write
	.syntax unified
	.arm
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
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r7, #:lower16:.LANCHOR0
	movw	r3, #:lower16:__stack_chk_guard
	movt	r7, #:upper16:.LANCHOR0
	mov	ip, r3
	add	r3, r7, #304
	mov	r5, r1
	ldrh	r1, [r3]
	mov	r3, ip
	.pad #116
	sub	sp, sp, #116
	movt	r3, #:upper16:__stack_chk_guard
	mov	r8, r0
	str	r3, [sp, #16]
	add	r0, r0, r5
	ldr	r3, [r3]
	cmp	r0, r1
	str	r3, [sp, #108]
	mvnhi	r3, #0
	strhi	r3, [sp, #8]
	bls	.L1576
.L1562:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #108]
	ldr	r0, [sp, #8]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L1577
	add	sp, sp, #116
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1576:
	cmp	r5, #0
	beq	.L1572
	add	r3, r7, #316
	mov	r9, r2
	ldrh	r3, [r3]
	movw	r2, #:lower16:.LC114
	movt	r2, #:upper16:.LC114
	str	r2, [sp, #20]
	mov	r2, #0
	str	r2, [sp, #8]
	lsr	r3, r8, r3
	str	r3, [sp, #4]
	lsl	r10, r3, #2
	b	.L1569
.L1579:
	ldr	lr, [r7, #3360]
	mov	r2, #1
	mov	r1, r2
	add	ip, sp, #44
	add	r0, sp, #24
	str	fp, [sp, #28]
	str	lr, [sp, #32]
	str	ip, [sp, #36]
	bl	FlashReadPages
	ldr	r1, [sp, #24]
	ldr	r2, [r7, #3464]
	ldr	r3, [sp, #8]
	cmn	r1, #1
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	beq	.L1578
.L1567:
	ldr	r1, [r7, #3360]
	lsl	r4, r4, #9
	mov	r0, r9
	mov	r2, r4
	add	r1, r1, r3, lsl #9
	bl	memcpy
.L1568:
	ldr	r3, [sp, #4]
	subs	r5, r5, r6
	add	r8, r8, r6
	add	r9, r9, r4
	add	r10, r10, #4
	add	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1562
.L1569:
	ldr	r3, .L1580
	mov	r0, r8
	ldrh	r4, [r3]
	ldr	r3, [r7, #3412]
	mov	r1, r4
	ldr	fp, [r3, r10]
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	str	r1, [sp, #12]
	uxth	r4, r4
	cmp	r4, r5
	mov	r6, r4
	uxthhi	r4, r5
	movhi	r6, r4
	cmp	fp, #0
	bne	.L1579
	lsl	r4, r4, #9
	mov	r1, fp
	mov	r0, r9
	mov	r2, r4
	bl	memset
	b	.L1568
.L1578:
	mov	r2, fp
	ldr	fp, [sp, #4]
	ldr	r0, [sp, #20]
	mov	r1, fp
	bl	sftl_printk
	ldr	r2, [r7, #3360]
	mov	r1, fp
	ldr	r0, .L1580+4
	bl	FtlMapWritePage
	ldr	r3, [sp, #12]
	b	.L1567
.L1572:
	str	r5, [sp, #8]
	b	.L1562
.L1577:
	bl	__stack_chk_fail
.L1581:
	.align	2
.L1580:
	.word	.LANCHOR0+266
	.word	.LANCHOR0+3488
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r0, #64
	add	r5, r4, #3392
	ldr	r2, [r4, #3396]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r2, [r4, #3396]
	movw	r3, #17221
	movt	r3, 16980
	ldr	r2, [r2]
	cmp	r2, r3
	beq	.L1583
	movw	r1, #:lower16:.LC115
	movw	r0, #:lower16:.LC77
	movt	r1, #:upper16:.LC115
	movt	r0, #:upper16:.LC77
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3396]
	lsl	r2, r2, #9
	bl	memset
.L1583:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	ldr	r2, .L1590
	movt	r4, #:upper16:.LANCHOR0
	mov	r1, #1
	mov	r0, #0
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2696]
	movw	r5, #19539
	movt	r5, 18004
	cmp	r3, r5
	beq	.L1589
	mov	r2, #512
	mov	r1, #0
	ldr	r0, .L1590
	bl	memset
	mov	r3, #0
	str	r5, [r4, #2696]
	mov	ip, r3
	mov	lr, r3
	mov	r5, r3
	mov	r6, r3
	mov	r2, r3
	mov	r7, r3
	mov	r8, r3
	mov	r9, r3
	mov	r10, r3
	mov	fp, r3
	mov	r0, r3
.L1587:
	ldr	r1, .L1590+4
	str	r0, [r4, #2636]
	str	fp, [r4, #2640]
	ldrh	r0, [r1]
	ldr	r1, [r4, #2652]
	str	r10, [r4, #2632]
	str	r9, [r4, #2620]
	str	r8, [r4, #2612]
	mla	r0, r1, r0, r2
	str	r7, [r4, #2628]
	str	r2, [r4, #2656]
	str	r6, [r4, #2664]
	str	r5, [r4, #2616]
	str	lr, [r4, #2624]
	str	ip, [r4, #2668]
	str	r3, [r4, #2672]
	ldrh	r1, [r4, #20]
	bl	__aeabi_uidiv
	str	r0, [r4, #2660]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1589:
	ldr	r0, [r4, #2784]
	ldr	fp, [r4, #2788]
	ldr	r10, [r4, #2704]
	ldr	r9, [r4, #2708]
	ldr	r8, [r4, #2712]
	ldr	r7, [r4, #2716]
	ldr	r2, [r4, #2724]
	ldr	r6, [r4, #2728]
	ldr	r5, [r4, #2732]
	ldr	lr, [r4, #2736]
	ldr	ip, [r4, #2740]
	ldr	r3, [r4, #2744]
	b	.L1587
.L1591:
	.align	2
.L1590:
	.word	.LANCHOR0+2696
	.word	.LANCHOR0+300
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	sftl_vendor_read
	.syntax unified
	.arm
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
	.align	2
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0, #36]
	cmp	r3, #0
	bxeq	lr
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	ldrh	r6, [r0, #6]
	movt	r4, #:upper16:.LANCHOR0
	mov	r5, r0
	mov	r1, #0
	ldr	r3, [r0, #24]
	ldr	r2, [r4, #3384]
	sub	r6, r6, #1
	ldr	r0, [r4, #3356]
	uxth	r6, r6
	str	r1, [r5, #36]
	str	r2, [r4, #3476]
	str	r0, [r4, #3472]
	ldr	r3, [r3, r6, lsl #2]
	cmp	r3, r1
	str	r3, [r4, #3468]
	bne	.L1600
	movw	r3, #318
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	memset
.L1596:
	ldr	r2, [r4, #3472]
	mov	r1, r6
	mov	r0, r5
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1600:
	mov	r2, #1
	ldr	r0, .L1601
	mov	r1, r2
	bl	FlashReadPages
	b	.L1596
.L1602:
	.align	2
.L1601:
	.word	.LANCHOR0+3464
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r1, #2680
	movw	r3, #61604
	mov	r0, #0
	ldr	r6, [r4, #3384]
	movt	r3, 65535
	ldr	ip, [r4, #3352]
	movw	r2, #262
	ldrh	r1, [r4, r1]
	movw	lr, #19539
	str	r6, [r4, #3476]
	movt	lr, 18004
	str	ip, [r4, #3472]
	mov	ip, #83
	strh	r3, [r6]	@ movhi
	movt	ip, 20480
	strh	r1, [r6, #2]	@ movhi
	movw	r8, #318
	ldr	r3, [r4, #2688]
	.pad #12
	sub	sp, sp, #12
	str	r0, [r6, #8]
	mov	r1, #255
	ldr	r5, .L1631
	str	r3, [r6, #4]
	str	r0, [r6, #12]
	ldrh	r7, [r4, r2]
	ldrb	r0, [r4, #96]	@ zero_extendqisi2
	ldrb	r2, [r4, #46]	@ zero_extendqisi2
	ldrh	r9, [r4, #42]
	ldrb	r3, [r4, #94]	@ zero_extendqisi2
	str	lr, [r4, #2472]
	strb	r0, [r4, #2484]
	ldrh	lr, [r4, #90]
	orr	r2, r2, r9, lsl #6
	ldrh	r0, [r5, #6]
	str	ip, [r4, #2476]
	strh	r2, [r5, #-192]	@ movhi
	ldrb	ip, [r4, #48]	@ zero_extendqisi2
	orr	r3, r3, lr, lsl #6
	ldrh	r2, [r4, #40]
	strh	r0, [r5, #-200]	@ movhi
	ldrh	r0, [r4, #88]
	strb	r7, [r4, #2482]
	sub	r7, r5, #208
	strb	ip, [r4, #2483]
	strh	r2, [r5, #-194]	@ movhi
	strh	r0, [r5, #-190]	@ movhi
	ldrh	r2, [r4, #136]
	strh	r3, [r5, #-188]	@ movhi
	ldrh	ip, [r4, #138]
	ldrb	r3, [r4, #142]	@ zero_extendqisi2
	ldrb	r0, [r4, #144]	@ zero_extendqisi2
	strh	r2, [r5, #-186]	@ movhi
	ldrh	r2, [r4, r8]
	sub	r8, r5, #160
	orr	r3, r3, ip, lsl #6
	ldr	ip, [r4, #2652]
	strb	r0, [r4, #2485]
	ldr	r0, [r4, #2644]
	str	ip, [r4, #2504]
	ldr	ip, [r4, #2648]
	str	r0, [r4, #2512]
	ldr	r0, [r4, #3472]
	strh	r3, [r5, #-184]	@ movhi
	str	ip, [r4, #2508]
	bl	memset
	ldr	lr, [r4, #3472]
.L1604:
	mov	ip, r7
	add	lr, lr, #16
	ldmia	ip!, {r0, r1, r2, r3}
	add	r7, r7, #16
	cmp	ip, r8
	str	r0, [lr, #-16]	@ unaligned
	str	r1, [lr, #-12]	@ unaligned
	str	r2, [lr, #-8]	@ unaligned
	str	r3, [lr, #-4]	@ unaligned
	bne	.L1604
	ldrh	r8, [r4, #20]
	ldr	r9, [r4, #3472]
	ldr	r1, [r4, #16]
	lsl	r7, r8, #1
	add	r0, r9, #48
	mov	r2, r7
	bl	memcpy
	add	r0, r7, #48
	lsr	r2, r8, #3
	bic	r0, r0, #3
	ldr	r1, [r4, #32]
	add	r2, r2, #4
	add	r0, r9, r0
	bl	memcpy
	movw	r2, #2538
	ldrh	r3, [r4, r2]
	cmp	r3, #4
	bhi	.L1605
	add	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
.L1606:
	movw	r3, #:lower16:.LC8
	ldr	r8, .L1631+4
	movt	r3, #:upper16:.LC8
	mov	r7, #0
	str	r3, [sp, #4]
	movw	fp, #65535
	ldrh	r3, [r5, #2]
	add	r10, r8, #6
	add	r9, r8, #3152
.L1607:
	ldrh	r1, [r5]
	ldrh	r2, [r8]
	ldr	r0, [r4, #3352]
	sub	r2, r2, #1
	orr	ip, r3, r1, lsl #10
	cmp	r3, r2
	ldr	r3, [r4, #3384]
	str	ip, [r4, #3468]
	str	r0, [r4, #3472]
	str	r3, [r4, #3476]
	bge	.L1626
	ldrh	ip, [r10]
	movw	r1, #42982
	cmp	ip, #0
	beq	.L1619
.L1628:
	add	ip, r0, ip
	movt	r1, 18374
.L1610:
	lsr	r3, r1, #2
	ldrb	r2, [r0], #1	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	ip, r0
	add	r3, r3, r2
	eor	r1, r1, r3
	bne	.L1610
.L1609:
	mov	r3, #1
	str	r1, [r6, #12]
	mov	r2, r3
	mov	r1, r3
	mov	r0, r9
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3464]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	beq	.L1627
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1607
	sub	r3, fp, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1618
	ldr	r3, .L1631+8
	ldrh	r3, [r3, #6]
	cmp	r3, #1024
	beq	.L1618
	mov	r0, fp
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn.part.10
.L1618:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1626:
	mov	r3, #0
	ldrh	fp, [r5, #4]
	strh	r3, [r5, #2]	@ movhi
	strh	r1, [r5, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldrh	ip, [r10]
	lsl	r2, r0, #10
	ldr	r3, [r4, #2644]
	movw	r1, #42982
	str	r2, [r4, #3468]
	cmp	ip, #0
	strh	r0, [r5]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #2688]
	str	r2, [r4, #2644]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
	ldr	r0, [r4, #3352]
	bne	.L1628
.L1619:
	movt	r1, 18374
	b	.L1609
.L1627:
	cmp	r3, #1
	beq	.L1629
	add	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #3
	bhi	.L1630
.L1615:
	ldrh	r3, [r5, #2]
	b	.L1607
.L1629:
	movw	r2, #1138
	ldr	r1, .L1631+12
	ldr	r0, [sp, #4]
	add	r7, r7, #1
	bl	sftl_printk
	ldrh	r3, [r5, #2]
	uxth	r7, r7
	cmp	r3, #1
	ldrheq	r3, [r8]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r7, #3
	bls	.L1615
.L1630:
	movw	r0, #:lower16:.LC116
	mov	r2, r7
	ldr	r1, [r4, #3468]
	movt	r0, #:upper16:.LC116
	bl	sftl_printk
.L1616:
	b	.L1616
.L1605:
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	str	r3, [r4, #28]
	bl	FtlUpdateVaildLpn.part.5
	b	.L1606
.L1632:
	.align	2
.L1631:
	.word	.LANCHOR0+2680
	.word	.LANCHOR0+312
	.word	.LANCHOR0+416
	.word	.LANCHOR1+460
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r6, #:lower16:.LANCHOR0
	movt	r6, #:upper16:.LANCHOR0
	.pad #28
	sub	sp, sp, #28
	ldrh	r3, [r6, #36]
	str	r0, [r6, #3332]
	cmp	r3, #0
	beq	.L1655
	mov	r5, #0
	str	r1, [sp]
	str	r0, [sp, #4]
	mov	r8, r5
	mov	r10, r5
	b	.L1638
.L1635:
	mov	r0, r4
	mov	r1, r7
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r9, r1
	mov	r1, r7
	lsl	r4, r4, #10
	bl	__aeabi_uidiv
	uxth	r2, r0
	lsr	r0, r9, #5
	add	r1, r8, r8, lsl #2
	add	r2, r6, r2, lsl #2
	and	lr, r9, #31
	add	ip, r8, #1
	ldr	r3, [r2, #380]
	add	r2, r10, #1
	ldr	r3, [r3, r0, lsl #2]
	lsr	r3, r3, lr
	tst	r3, #1
	uxthne	r10, r2
	bne	.L1636
	ldr	r3, .L1696
	ldr	r0, [r6, #3348]
	ldr	r2, [r6, #3388]
	ldrh	r3, [r3]
	add	r1, r0, r1, lsl #2
	mul	r3, r8, r3
	uxth	r8, ip
	ldr	ip, [r6, #3368]
	str	r4, [r1, #4]
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	str	ip, [r1, #8]
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r1, #12]
.L1636:
	add	r5, r5, #1
	ldrh	r3, [r6, #36]
	uxth	r5, r5
	cmp	r3, r5
	bls	.L1691
.L1638:
	ldr	r1, [r6, #3348]
	add	r3, r5, r5, lsl #2
	mov	r0, #0
	add	r2, r6, r5
	str	r0, [r1, r3, lsl #2]
	ldr	r3, .L1696+4
	ldrb	fp, [r2, #268]	@ zero_extendqisi2
	ldrh	r9, [r3]
	ldrh	r7, [r3, #42]
	mov	r0, fp
	ldrh	r3, [sp, #4]
	mov	r1, r9
	smulbb	r4, r3, r9
	bl	__aeabi_uidivmod
	mov	r0, fp
	add	r4, r4, r1
	mov	r1, r9
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	smlabb	r4, r0, r7, r4
	cmp	r3, #0
	uxth	r4, r4
	beq	.L1635
	ldr	r3, .L1696+8
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L1635
	mov	r0, r4
	bl	IsBlkInVendorPart.part.0
	cmp	r0, #0
	bne	.L1636
	b	.L1635
.L1691:
	cmp	r8, #0
	bne	.L1692
.L1654:
	mov	r0, r10
.L1633:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1655:
	mov	r0, r3
	b	.L1633
.L1692:
	sub	r4, r8, #1
	mov	r1, #0
	uxth	r4, r4
	mov	r2, r8
	ldr	r0, [r6, #3348]
	mov	r5, r1
	add	r4, r4, #1
	add	r4, r4, r4, lsl #2
	bl	FlashEraseBlocks
	lsl	r4, r4, #2
	b	.L1642
.L1641:
	cmp	r4, r5
	beq	.L1693
.L1642:
	ldr	r3, [r6, #3348]
	ldr	r2, [r3, r5]
	add	r3, r3, r5
	add	r5, r5, #20
	cmn	r2, #1
	bne	.L1641
	ldr	r0, [r3, #4]
	add	r10, r10, #1
	uxth	r10, r10
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	b	.L1641
.L1693:
.L1643:
	ldr	r3, [sp]
	str	r10, [sp, #12]
	cmp	r3, #0
	ldrne	r3, .L1696+12
	movne	r2, #1
	ldreq	r3, [sp]
	strne	r2, [sp, #16]
	ldrhne	r3, [r3]
	streq	r3, [sp, #16]
	moveq	r3, #2
	str	r3, [sp, #20]
	mov	r3, #0
	str	r3, [sp, #8]
.L1651:
	ldrh	r3, [r6, #36]
	cmp	r3, #0
	beq	.L1648
	mov	r5, #0
	mov	r8, r5
	mov	r9, r5
	b	.L1647
.L1645:
	mov	r0, r4
	mov	r1, r7
	bl	__aeabi_uidivmod
	mov	r0, r4
	uxth	r10, r1
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldr	r3, [sp, #8]
	lsr	ip, r10, #5
	add	r1, r8, r8, lsl #2
	add	r2, r6, r0, lsl #2
	and	r0, r10, #31
	add	r4, r3, r4, lsl #10
	ldr	r3, [r2, #380]
	add	r2, r8, #1
	ldr	r3, [r3, ip, lsl #2]
	lsr	r3, r3, r0
	tst	r3, #1
	bne	.L1646
	ldr	r3, .L1696
	ldr	ip, [r6, #3348]
	ldr	r0, [r6, #3368]
	ldrh	r3, [r3]
	add	r1, ip, r1, lsl #2
	ldr	ip, [r6, #3364]
	mul	r3, r8, r3
	uxth	r8, r2
	str	r4, [r1, #4]
	str	ip, [r1, #8]
	add	r2, r3, #3
	cmp	r3, #0
	movlt	r3, r2
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L1646:
	add	r5, r5, #1
	ldrh	r3, [r6, #36]
	uxth	r5, r5
	cmp	r3, r5
	bls	.L1694
.L1647:
	ldr	r1, [r6, #3348]
	add	r3, r5, r5, lsl #2
	add	r2, r6, r5
	str	r9, [r1, r3, lsl #2]
	ldr	r3, .L1696+4
	ldrb	fp, [r2, #268]	@ zero_extendqisi2
	ldrh	r10, [r3]
	ldrh	r7, [r3, #42]
	mov	r0, fp
	ldrh	r3, [sp, #4]
	mov	r1, r10
	smulbb	r4, r3, r10
	bl	__aeabi_uidivmod
	mov	r0, fp
	add	r4, r4, r1
	mov	r1, r10
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	smlabb	r4, r0, r7, r4
	cmp	r3, #0
	uxth	r4, r4
	beq	.L1645
	ldr	r3, .L1696+8
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L1645
	mov	r0, r4
	bl	IsBlkInVendorPart.part.0
	cmp	r0, #0
	bne	.L1646
	b	.L1645
.L1694:
	cmp	r8, #0
	beq	.L1648
	sub	r5, r8, #1
	mov	r3, #1
	uxth	r5, r5
	ldr	r2, [sp, #16]
	mov	r1, r8
	ldr	r0, [r6, #3348]
	add	r5, r5, #1
	mov	r4, #0
	add	r5, r5, r5, lsl #2
	bl	FlashProgPages
	lsl	r5, r5, #2
	ldr	r7, [sp, #12]
	b	.L1650
.L1649:
	cmp	r5, r4
	beq	.L1695
.L1650:
	ldr	r3, [r6, #3348]
	ldr	r2, [r3, r4]
	add	r3, r3, r4
	add	r4, r4, #20
	cmp	r2, #0
	beq	.L1649
	ldr	r0, [r3, #4]
	add	r7, r7, #1
	uxth	r7, r7
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	cmp	r5, r4
	bne	.L1650
.L1695:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #20]
	str	r7, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #8]
	uxth	r3, r3
	cmp	r3, r2
	bcc	.L1651
	mov	r9, r8
	str	r8, [sp, #8]
	ldr	fp, [sp]
	mov	r4, #0
	ldr	r8, .L1696+16
	mov	r10, r7
	mov	r5, r4
	movw	r7, #65533
.L1653:
	cmp	fp, #0
	add	r5, r5, #1
	beq	.L1652
	ldr	r3, [r6, #3348]
	ldr	r2, [r3, r4]
	add	r3, r3, r4
	cmp	r2, #0
	bne	.L1652
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	sub	r3, r0, #1
	uxth	r3, r3
	cmp	r3, r7
	bhi	.L1652
	ldrh	r3, [r8, #6]
	mov	r1, #1
	cmp	r3, #1024
	beq	.L1652
	bl	FtlFreeSysBlkQueueIn.part.10
.L1652:
	uxth	r3, r5
	add	r4, r4, #20
	cmp	r9, r3
	bhi	.L1653
	ldr	r3, [sp]
	ldr	r2, [sp, #4]
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L1654
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #16]
	ldr	r0, [r6, #3348]
	bl	FlashEraseBlocks
	b	.L1654
.L1648:
	ldr	r10, [sp, #12]
	mov	r0, r10
	b	.L1633
.L1697:
	.align	2
.L1696:
	.word	.LANCHOR0+320
	.word	.LANCHOR0+264
	.word	.LANCHOR0+348
	.word	.LANCHOR0+312
	.word	.LANCHOR0+416
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r2, #318
	.pad #20
	sub	sp, sp, #20
	mov	r1, #0
	ldr	r3, [r4, #3352]
	ldr	ip, [r4, #3384]
	ldrh	r2, [r4, r2]
	mov	r0, r3
	str	r3, [r4, #3472]
	str	ip, [r4, #3476]
	bl	memset
	movw	r3, #262
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1702
	ldr	r8, .L1717
	add	r6, r4, #380
	mov	r5, #0
	add	r7, r4, #412
.L1701:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3472]
	ldr	r1, [r6], #4
	mul	r0, r5, r2
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	memcpy
	ldrh	r3, [r8]
	cmp	r3, r5
	bgt	.L1701
.L1702:
	ldr	r8, [r4, #3476]
	movw	r2, #61649
	ldr	r5, .L1717+4
	mvn	r3, #0
	mov	r1, #0
	movt	r2, 65535
	str	r3, [r8]	@ unaligned
	mov	r6, r1
	strh	r2, [r8]	@ movhi
	movw	r9, #:lower16:.LC117
	str	r3, [r8, #4]	@ unaligned
	sub	fp, r5, #40
	str	r3, [r8, #8]	@ unaligned
	movt	r9, #:upper16:.LC117
	ldr	r3, [r4, #360]
	add	r10, fp, #3152
	str	r1, [sp, #12]
	ldrh	r1, [r5]
	str	r3, [r8, #4]
	strh	r1, [r8, #2]	@ movhi
	ldr	r2, [r4, #2660]
	ldrh	ip, [r5, #6]
	ldr	r0, [r4, #240]
	ldrh	r3, [r5, #4]
	strh	r2, [r8, #14]	@ movhi
	strh	ip, [r8, #10]	@ movhi
	strh	r0, [r8, #12]	@ movhi
	strh	r3, [r8, #8]	@ movhi
	ldrh	r2, [r5, #2]
	b	.L1700
.L1703:
	mov	r3, #1
	mov	r0, r10
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r2, [r5, #2]
	ldr	r3, [r4, #3464]
	add	r2, r2, #1
	uxth	r2, r2
	cmn	r3, #1
	strh	r2, [r5, #2]	@ movhi
	beq	.L1714
	add	r6, r6, #1
	cmp	r6, #1
	ble	.L1708
	cmp	r3, #256
	bne	.L1715
.L1707:
	ldrh	r1, [r5]
	ldrh	r3, [r5, #4]
.L1700:
	ldr	lr, [r4, #3352]
	orr	ip, r2, r1, lsl #10
	ldr	r0, [r4, #3384]
	mov	r7, #0
	str	ip, [r4, #3468]
	str	lr, [r4, #3472]
	str	r0, [r4, #3476]
	mov	r0, r9
	ldrh	lr, [r8, #10]
	str	r7, [r4, #3464]
	str	lr, [sp]
	bl	sftl_printk
	ldrh	r3, [fp]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1703
	ldr	r3, [r4, #360]
	mov	r2, #1
	ldrh	lr, [r5]
	mov	r1, r2
	ldr	ip, [r4, #3348]
	add	r3, r3, r2
	strh	r7, [r5, #2]	@ movhi
	str	r3, [r4, #360]
	str	r3, [r8, #4]
	mov	r0, ip
	ldrh	r3, [r5, #4]
	strh	lr, [r8, #8]	@ movhi
	strh	lr, [r5, #4]	@ movhi
	strh	r3, [r5]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r4, #3468]
	str	r3, [ip, #4]
	bl	FlashEraseBlocks
	b	.L1703
.L1708:
	mov	r6, #1
	b	.L1707
.L1714:
	movw	r0, #:lower16:.LC118
	ldr	r1, [r4, #3468]
	movt	r0, #:upper16:.LC118
	bl	sftl_printk
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #3
	str	r3, [sp, #12]
	bhi	.L1716
	ldrh	r2, [r5, #2]
	b	.L1707
.L1715:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1716:
	movw	r0, #:lower16:.LC119
	mov	r2, r3
	ldr	r1, [r4, #3468]
	movt	r0, #:upper16:.LC119
	bl	sftl_printk
.L1706:
	b	.L1706
.L1718:
	.align	2
.L1717:
	.word	.LANCHOR0+262
	.word	.LANCHOR0+352
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk.part.12, %function
FtlGcFreeBadSuperBlk.part.12:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r8, #:lower16:.LANCHOR0
	movt	r8, #:upper16:.LANCHOR0
	.pad #12
	sub	sp, sp, #12
	ldrh	r3, [r8, #36]
	cmp	r3, #0
	beq	.L1723
	ldr	fp, .L1736
	movw	r9, #:lower16:.LC120
	movt	r9, #:upper16:.LC120
	mov	r7, #0
	str	r0, [sp, #4]
	add	r10, fp, #2
.L1722:
	ldr	r2, .L1736+4
	add	r3, r8, r7
	ldrb	r4, [r3, #268]	@ zero_extendqisi2
	ldrh	r3, [sp, #4]
	ldrh	r6, [r2]
	mov	r0, r4
	mov	r1, r6
	smulbb	r5, r6, r3
	bl	__aeabi_uidiv
	ldr	r3, .L1736+8
	mov	r1, r6
	ldrh	r2, [r3]
	smulbb	r6, r2, r0
	mov	r0, r4
	bl	__aeabi_uidivmod
	ldrh	r3, [fp]
	add	r1, r5, r1
	add	r6, r6, r1
	uxth	r6, r6
	cmp	r3, #0
	movne	r4, #0
	bne	.L1729
	b	.L1730
.L1735:
	add	r0, r4, #1
	ldrh	r3, [fp]
	uxth	r4, r0
	cmp	r4, r3
	bcs	.L1730
.L1729:
	lsl	r3, r4, #1
	ldrh	r2, [r10, r3]
	add	r5, r10, r3
	cmp	r2, r6
	bne	.L1735
	mov	r1, r6
	mov	r0, r9
	bl	sftl_printk
	mov	r0, r6
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r3, [fp]
	add	r0, r4, #1
	cmp	r3, r4
	sub	r1, r3, #1
	bls	.L1728
	sub	r4, r1, r4
	mov	r3, r5
	uxtah	r4, r0, r4
	add	r4, r10, r4, lsl #1
.L1727:
	ldrh	r2, [r3, #2]
	strh	r2, [r3], #2	@ movhi
	cmp	r4, r3
	bne	.L1727
.L1728:
	uxth	r3, r1
	uxth	r4, r0
	cmp	r4, r3
	strh	r3, [fp]	@ movhi
	bcc	.L1729
.L1730:
	add	r7, r7, #1
	ldrh	r3, [r8, #36]
	uxth	r7, r7
	cmp	r3, r7
	bhi	.L1722
.L1723:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FtlGcReFreshBadBlk
.L1737:
	.align	2
.L1736:
	.word	.LANCHOR0+3282
	.word	.LANCHOR0+264
	.word	.LANCHOR0+306
	.fnend
	.size	FtlGcFreeBadSuperBlk.part.12, .-FtlGcFreeBadSuperBlk.part.12
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	movw	r2, #3282
	movt	r3, #:upper16:.LANCHOR0
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	beq	.L1744
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlGcFreeBadSuperBlk.part.12
	mov	r0, #0
	pop	{r4, pc}
.L1744:
	mov	r0, #0
	bx	lr
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	lsl	r6, r0, #1
	ldr	r3, [r4, #16]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1748
	ldrh	r3, [r4, #234]
	mov	r5, r0
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #234]	@ movhi
	ldrh	r3, [r4, #232]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #232]	@ movhi
	ldrh	r3, [r4, #184]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #184]	@ movhi
	beq	.L1752
	ldrh	r3, [r4, #40]
	cmp	r3, r0
	beq	.L1759
	ldrh	r3, [r4, #88]
	cmp	r3, r0
	beq	.L1759
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	beq	.L1759
.L1752:
	mov	r1, r5
	ldr	r0, .L1768
	movw	r7, #2520
	bl	List_remove_node
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	beq	.L1765
.L1754:
	movw	r2, #65535
	sub	r3, r3, #1
	cmp	r5, r2
	movw	r2, #2520
	strh	r3, [r4, r2]	@ movhi
	beq	.L1755
	ldr	r3, [r4, #16]
	mov	r2, #0
	mov	r0, r5
	strh	r2, [r3, r6]	@ movhi
	bl	INSERT_FREE_LIST
.L1755:
	movw	r3, #3282
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1766
.L1756:
	movw	r2, #2520
	ldrh	r3, [r4, #236]
	ldrh	r1, [r4, r2]
	ldrh	r2, [r4, #20]
	add	r3, r3, r1
	cmp	r3, r2
	bgt	.L1767
	mov	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L1759:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1748:
	bl	List_update_data_list
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1765:
	movw	r0, #:lower16:.LC8
	movw	r2, #2824
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1768+4
	bl	sftl_printk
	ldrh	r3, [r4, r7]
	b	.L1754
.L1767:
	movw	r0, #:lower16:.LC8
	movw	r2, #2827
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1768+4
	bl	sftl_printk
	mov	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L1766:
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk.part.12
	b	.L1756
.L1769:
	.align	2
.L1768:
	.word	.LANCHOR0+12
	.word	.LANCHOR1+476
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	cmp	r0, r3
	mov	r5, r0
	beq	.L1771
	movw	r4, #:lower16:.LANCHOR0
	lsl	r7, r0, #1
	movt	r4, #:upper16:.LANCHOR0
	ldr	r1, [r4, #16]
	ldrh	r2, [r1, r7]
	cmp	r2, #0
	beq	.L1784
	sub	r2, r2, #1
	add	r6, r4, #3248
	strh	r2, [r1, r7]	@ movhi
	ldrh	r0, [r6]
	cmp	r0, r3
	beq	.L1777
	cmp	r5, r0
	bne	.L1778
.L1783:
	mov	r4, #0
.L1770:
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, pc}
.L1771:
	ldr	r6, .L1785
	ldrh	r0, [r6]
	cmp	r0, r5
	subne	r4, r6, #3248
	beq	.L1777
.L1778:
	bl	update_vpc_list
	ldr	r2, [r4, #12]
	movw	r3, #43691
	ldr	ip, [r4, #8]
	movt	r3, 43690
	ldr	r1, [r4, #16]
	adds	r4, r0, #0
	strh	r5, [r6]	@ movhi
	movne	r4, #1
	sub	r2, r2, ip
	asr	r2, r2, #1
	mul	r3, r3, r2
	uxth	r2, r3
	mov	r3, r2
	lsl	r2, r2, #1
	cmp	r3, r5
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1770
	movw	r0, #:lower16:.LC8
	movw	r2, #2858
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1785+4
	bl	sftl_printk
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, pc}
.L1784:
	movw	r0, #:lower16:.LC121
	mov	r1, r5
	movt	r0, #:upper16:.LC121
	bl	sftl_printk
	ldr	r3, [r4, #16]
	ldrh	r4, [r3, r7]
	cmp	r4, #0
	bne	.L1783
	movw	r0, #:lower16:.LC8
	movw	r2, #2842
	movt	r0, #:upper16:.LC8
	ldr	r1, .L1785+4
	bl	sftl_printk
	b	.L1770
.L1777:
	strh	r5, [r6]	@ movhi
	b	.L1783
.L1786:
	.align	2
.L1785:
	.word	.LANCHOR0+3248
	.word	.LANCHOR1+492
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r6, #:lower16:.LANCHOR0
	movt	r6, #:upper16:.LANCHOR0
	.pad #28
	sub	sp, sp, #28
	mov	r4, r0
	mvn	r1, #2
	ldr	r7, [r6, #3384]
	mvn	r0, #0
	ldr	r3, [r6, #3352]
	mvn	r2, #1
	str	r0, [sp, #16]
	movw	r8, #:lower16:__stack_chk_guard
	str	r7, [sp, #12]
	movt	r8, #:upper16:__stack_chk_guard
	str	r3, [sp, #8]
	mov	r3, #0
	str	r1, [r7, #8]
	movw	r1, #61589
	str	r2, [r7, #12]
	ldrh	r2, [r4]
	ldr	r0, [r8]
	strh	r3, [r7]	@ movhi
	movw	r3, #22136
	strh	r2, [r7, #2]	@ movhi
	movt	r3, 4660
	ldr	r2, [r6, #3352]
	str	r0, [sp, #20]
	str	r1, [r2]
	ldr	r2, [r6, #3352]
	str	r3, [r2, #4]
	ldrh	r3, [r4, #4]
	tst	r3, #1
	beq	.L1809
	mov	r5, #6
.L1788:
	sub	r5, r5, #1
	mov	r9, #0
	b	.L1791
.L1792:
	ldr	lr, [r6, #2648]
	mov	r1, #1
	mov	r3, #0
	mov	r0, sp
	mov	r2, r3
	add	ip, lr, r1
	str	lr, [r7, #4]
	cmn	ip, #1
	moveq	ip, r9
	str	ip, [r6, #2648]
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	cmn	r5, #1
	ldrh	r3, [r4, #4]
	beq	.L1790
	cmp	r3, #0
	sub	r5, r5, #1
	beq	.L1790
.L1791:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	bne	.L1792
	ldrh	r3, [r4, #4]
.L1790:
	ldrh	r2, [r4]
	movw	lr, #310
	ldr	ip, [r6, #16]
	mov	r0, #0
	lsl	r2, r2, #1
	ldrh	r1, [ip, r2]
	sub	r3, r1, r3
	strh	r3, [ip, r2]	@ movhi
	ldrh	r3, [r6, lr]
	ldr	r2, [sp, #20]
	strb	r0, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r8]
	strh	r0, [r4, #4]	@ movhi
	cmp	r2, r3
	bne	.L1810
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1809:
	cmp	r3, #0
	beq	.L1790
	mov	r5, #5
	b	.L1788
.L1810:
	bl	__stack_chk_fail
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r5, #:lower16:__stack_chk_guard
	movt	r5, #:upper16:__stack_chk_guard
	.pad #36
	sub	sp, sp, #36
	ldrh	r3, [r4, #44]
	ldr	r2, [r5]
	cmp	r3, #0
	str	r2, [sp, #28]
	beq	.L1812
	ldrb	r2, [r4, #48]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1834
.L1812:
	mov	r3, #0
	strb	r3, [r4, #50]
.L1811:
	ldr	r2, [sp, #28]
	ldr	r3, [r5]
	cmp	r2, r3
	bne	.L1835
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1834:
	movw	r2, #310
	ldrb	r1, [r4, #47]	@ zero_extendqisi2
	ldrh	r2, [r4, r2]
	mul	r2, r2, r1
	cmp	r3, r2
	beq	.L1812
	ldrb	r7, [r4, #50]	@ zero_extendqisi2
	cmp	r7, #0
	bne	.L1811
	ldr	r9, [r4, #2608]
	mov	r2, r7
	add	r1, sp, #4
	ldrh	r8, [r4, #36]
	sub	r9, r9, #1
	mov	r0, r9
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r6, [r4, #3384]
	ldr	r0, [r4, #3352]
	cmn	r3, #1
	str	r9, [sp, #24]
	str	r3, [sp, #12]
	str	r0, [sp, #16]
	str	r6, [sp, #20]
	str	r7, [r6, #4]
	beq	.L1814
	mov	r2, r7
	mov	r1, #1
	add	r0, sp, #8
	bl	FlashReadPages
.L1815:
	lsl	r8, r8, #2
	movw	r3, #61589
	movt	r3, 65535
	cmp	r8, #0
	strh	r3, [r6]	@ movhi
	sub	r8, r8, #1
	beq	.L1817
	ldrh	r3, [r4, #44]
	cmp	r3, #0
	beq	.L1817
	mov	r7, #0
	ldr	r10, .L1836
	mov	fp, r7
	b	.L1818
.L1821:
	ldrh	r3, [r4, #44]
	add	r7, r7, #1
	cmp	r3, #0
	beq	.L1817
.L1818:
	ldr	r2, [sp, #12]
	mov	r0, r10
	ldrh	r3, [r4, #40]
	str	r9, [r6, #8]
	str	r2, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	lr, [r4, #2648]
	mov	r1, #1
	mov	r3, #0
	str	r0, [sp, #12]
	mov	r2, r3
	add	r0, sp, #8
	add	ip, lr, r1
	str	lr, [r6, #4]
	cmn	ip, #1
	moveq	ip, fp
	str	ip, [r4, #2648]
	bl	FlashProgPages
	ldrh	r0, [r4, #40]
	bl	decrement_vpc_count
	cmp	r8, r7
	bne	.L1821
.L1817:
	mov	r3, #1
	strb	r3, [r4, #50]
	b	.L1811
.L1814:
	movw	r3, #318
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	memset
	b	.L1815
.L1835:
	bl	__stack_chk_fail
.L1837:
	.align	2
.L1836:
	.word	.LANCHOR0+40
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r5, #:lower16:.LANCHOR0
	bl	FtlWriteDump_data
	movt	r5, #:upper16:.LANCHOR0
	movw	r3, #342
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L1844
	ldr	r6, .L1846
	mov	r4, #0
	b	.L1841
.L1840:
	add	r4, r4, #1
	ldrh	r3, [r6]
	uxth	r4, r4
	cmp	r3, r4
	bls	.L1844
.L1841:
	ldr	r3, [r5, #2532]
	add	r2, r4, r4, lsl #1
	add	r3, r3, r2, lsl #2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1840
	mov	r0, r4
	bl	flush_l2p_region
	b	.L1840
.L1844:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1847:
	.align	2
.L1846:
	.word	.LANCHOR0+342
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	pop	{r4, pc}
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR2
	push	{r4, lr}
	.save {r4, lr}
	movt	r3, #:upper16:.LANCHOR2
	ldr	r4, [r3]
	cmp	r4, #1
	beq	.L1853
	mov	r0, #0
	pop	{r4, pc}
.L1853:
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	pop	{r4, pc}
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #:lower16:__stack_chk_guard
	ldrh	r1, [r0]
	.pad #60
	sub	sp, sp, #60
	movt	r3, #:upper16:__stack_chk_guard
	ldr	r2, [r3]
	str	r3, [sp, #32]
	movw	r3, #65535
	cmp	r1, r3
	str	r0, [sp, #16]
	str	r2, [sp, #52]
	beq	.L1856
	movw	r4, #:lower16:.LANCHOR0
	movw	r2, #310
	movt	r4, #:upper16:.LANCHOR0
	ldrh	r10, [r0, #2]
	ldrh	r2, [r4, r2]
	ldrb	r1, [r0, #6]	@ zero_extendqisi2
	cmp	r2, r10
	str	r1, [sp, #12]
	beq	.L2024
	ldr	r1, [sp, #16]
	ldrh	r0, [r1, #16]
	cmp	r0, r3
	bne	.L1957
	mov	r2, r0
	mov	r5, #0
.L1859:
	add	r5, r5, #1
	uxth	r5, r5
	add	r3, r1, r5, lsl #1
	ldrh	r0, [r3, #16]
	cmp	r0, r2
	beq	.L1859
.L1858:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	fp, r0
	beq	.L1860
	ldrh	r2, [r4, #36]
	cmp	r2, #0
	beq	.L2025
	sub	r7, r2, #1
	movw	r2, #318
	ldrh	r2, [r4, r2]
	uxth	r7, r7
	ldr	r3, .L2044
	mov	r5, #0
	add	r7, r7, #8
	ldr	r0, [r4, #3336]
	str	r2, [sp, #4]
	movw	r8, #65535
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	ldr	r9, [r4, #3232]
	add	r6, r2, #14
	str	r4, [sp, #20]
	add	r7, r2, r7, lsl #1
	ldr	r2, [r4, #3236]
	mov	r4, r3
	str	r10, [sp, #8]
	mov	lr, r2
.L1866:
	ldrh	r1, [r6, #2]!
	cmp	r1, r8
	beq	.L1865
	ldr	r3, [sp, #4]
	mul	ip, r4, r5
	add	r2, r5, r5, lsl #2
	add	r10, r5, #1
	orr	r1, fp, r1, lsl #10
	add	r2, r0, r2, lsl #2
	mul	r3, r3, r5
	uxth	r5, r10
	str	r1, [r2, #4]
	add	r1, ip, #3
	cmp	r3, #0
	add	r10, r3, #3
	movlt	r3, r10
	cmp	ip, #0
	movlt	ip, r1
	bic	r3, r3, #3
	bic	ip, ip, #3
	add	r3, r9, r3
	add	ip, lr, ip
	str	r3, [r2, #8]
	str	ip, [r2, #12]
.L1865:
	cmp	r7, r6
	bne	.L1866
	ldr	r4, [sp, #20]
	mov	r2, #0
	mov	r1, r5
	ldr	r10, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [r4, #2648]
	cmp	r5, #0
	sub	r3, r3, #1
	str	r3, [sp, #20]
	beq	.L2026
	ldr	ip, [r4, #3336]
	uxth	r6, fp
	movw	r7, #65535
	str	r6, [sp, #24]
	mov	r3, #0
	mov	r2, ip
	b	.L1874
.L2028:
	ldr	lr, [r2, #12]
	ldr	r1, [lr, #4]
	cmn	r1, #1
	beq	.L1870
	ldr	r0, [r4, #2648]
	cmp	r1, r0
	bls	.L1871
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	bhi	.L1870
.L1872:
	add	r1, r1, #1
	str	r1, [r4, #2648]
.L1870:
	ldr	r1, [lr]
	cmn	r1, #1
	beq	.L2027
.L1869:
	add	r3, r3, #1
	add	r2, r2, #20
	uxth	r3, r3
	cmp	r5, r3
	beq	.L1868
.L1874:
	ldr	r1, [r2]
	cmp	r1, #0
	beq	.L2028
	mov	r7, r6
	b	.L1869
.L1860:
	cmp	r10, #0
	bne	.L2029
.L1863:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	cmpne	r3, r5
	beq	.L1864
	movw	r0, #:lower16:.LC8
	movw	r2, #1801
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2044+4
	bl	sftl_printk
.L1864:
	ldr	r2, [sp, #16]
	mov	r3, #0
	strh	r3, [r2, #2]	@ movhi
	strb	r3, [r2, #6]
.L1856:
	ldr	r3, [sp, #32]
	mov	r0, #0
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2030
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1871:
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	bhi	.L1872
	ldr	r1, [lr]
	cmn	r1, #1
	bne	.L1869
.L2027:
	cmp	r5, r3
	beq	.L1868
	ldr	r2, .L2044+8
	add	r3, r3, r3, lsl #2
	add	ip, ip, r3, lsl #2
	mov	r0, r2
	str	r2, [sp, #8]
	movw	r2, #306
	ldrh	r9, [r0]
	ldrh	r1, [r4, r2]
	ldr	r5, [ip, #4]
.L2013:
	ubfx	r5, r5, #10, #16
	mov	r0, r5
	bl	__aeabi_uidiv
	smulbb	r8, r0, r9
	mov	r1, r9
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r1, r8, r1
	ldr	r2, [sp, #24]
	uxth	r3, r1
	str	r3, [sp, #28]
	movw	r3, #310
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	beq	.L2031
.L1878:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #12]
	cmp	r3, r10
	cmpeq	r2, r1
	beq	.L2032
	movw	r3, #65535
	cmp	r7, r3
	beq	.L2033
.L1880:
	ldr	r3, [r4, #3260]
	uxth	r2, fp
	ldr	r0, [r4, #3336]
	cmn	r3, #1
	ldreq	r3, [sp, #20]
	streq	r3, [r4, #3260]
	add	r3, r10, #7
	cmp	r2, r3
	subgt	r8, r6, #7
	movle	r8, r10
	uxthgt	r8, r8
	cmp	r6, r8
	bcc	.L1895
	ldr	r9, [sp, #16]
	movw	r7, #65535
.L1885:
	ldrh	r2, [r4, #36]
	cmp	r2, #0
	beq	.L2034
	sub	ip, r2, #1
	mov	r5, #0
	uxth	ip, ip
	add	r2, r9, #14
	add	ip, ip, #8
	add	ip, r9, ip, lsl #1
.L1887:
	ldrh	r3, [r2, #2]!
	cmp	r3, r7
	beq	.L1886
	add	r1, r5, r5, lsl #2
	add	r5, r5, #1
	uxth	r5, r5
	orr	r3, r8, r3, lsl #10
	add	r1, r0, r1, lsl #2
	str	r3, [r1, #4]
.L1886:
	cmp	ip, r2
	bne	.L1887
	mov	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	cmp	r5, #0
	ldr	r0, [r4, #3336]
	beq	.L1889
	ldr	r3, [r0]
	cmp	r3, #0
	bne	.L1881
	sub	r3, r5, #1
	mov	r2, r0
	uxth	r3, r3
	add	r3, r3, r3, lsl #2
	add	r3, r0, r3, lsl #2
	b	.L1892
.L1894:
	ldr	r1, [r2, #20]!
	cmp	r1, #0
	bne	.L1881
.L1892:
	ldr	r1, [r2, #12]
	ldrh	ip, [r1]
	cmp	ip, r7
	beq	.L1893
	ldr	r1, [r1, #4]
	cmn	r1, #1
	strne	r1, [r4, #3260]
.L1893:
	cmp	r3, r2
	bne	.L1894
.L1889:
	add	r8, r8, #1
	uxth	r8, r8
	cmp	r6, r8
	bcs	.L1885
.L1895:
	mvn	r3, #0
	str	r3, [r4, #3260]
.L1881:
.L1898:
	movw	r3, #:lower16:.LC123
	mov	r2, #1
	movt	r3, #:upper16:.LC123
	mov	fp, r10
	str	r3, [sp, #36]
	movw	r3, #3534
	strh	r2, [r4, r3]	@ movhi
	ldrh	r2, [r4, #36]
	cmp	r2, #0
	beq	.L1899
.L2038:
	ldr	r3, [sp, #16]
	sub	lr, r2, #1
	uxth	lr, lr
	mov	r5, #0
	movw	r6, #65535
	add	lr, lr, #8
	add	r1, r3, #14
	add	lr, r3, lr, lsl #1
.L1901:
	ldrh	r3, [r1, #2]!
	cmp	r3, r6
	beq	.L1900
	add	r2, r5, r5, lsl #2
	add	r5, r5, #1
	uxth	r5, r5
	orr	r3, fp, r3, lsl #10
	add	r2, r0, r2, lsl #2
	str	r3, [r2, #4]
.L1900:
	cmp	lr, r1
	bne	.L1901
	mov	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	cmp	r5, #0
	beq	.L1903
	sub	r7, r5, #1
	mov	r8, #0
	uxth	r7, r7
	add	r7, r7, #1
	add	r7, r7, r7, lsl #2
	lsl	r7, r7, #2
	b	.L1942
.L2035:
	ldr	r3, [sp, #16]
	ldrh	r0, [r3]
	bl	decrement_vpc_count
.L1904:
	add	r8, r8, #20
	cmp	r8, r7
	beq	.L1903
.L1942:
	ldr	r6, [r4, #3336]
	cmp	fp, r10
	add	r6, r6, r8
	ldr	r9, [r6, #4]
	str	r9, [sp, #48]
	bcc	.L1904
	ldr	r3, [sp, #8]
	ubfx	r2, r9, #10, #16
	mov	r0, r2
	str	r2, [sp, #4]
	ldrh	r5, [r3]
	ldr	r3, .L2044+12
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	mov	r1, r5
	smulbb	r5, r0, r5
	mov	r0, r2
	bl	__aeabi_uidivmod
	add	r5, r5, r1
	ldr	r3, [sp, #12]
	uxth	r5, r5
	sub	r2, fp, r10
	clz	r2, r2
	lsr	r2, r2, #5
	cmp	r3, r5
	movls	r2, #0
	cmp	r2, #0
	bne	.L1904
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #28]
	cmp	fp, r3
	cmpeq	r2, r5
	beq	.L1905
	ldr	r2, [r6]
	cmn	r2, #1
	beq	.L1906
	ldr	r6, [r6, #12]
	movw	r2, #61589
	ldrh	r1, [r6]
	cmp	r1, r2
	bne	.L2035
	ldr	r3, [r6, #4]
	cmn	r3, #1
	str	r3, [sp, #20]
	beq	.L1909
	ldr	r2, [r4, #2648]
	cmp	r3, r2
	bls	.L1910
	sub	r2, r3, r2
	cmp	r2, #-2147483648
	bhi	.L1909
.L1912:
	ldr	r3, [sp, #20]
	movw	r2, #61589
	add	r1, r3, #1
	str	r1, [r4, #2648]
	ldrh	r1, [r6]
	cmp	r1, r2
	beq	.L1909
	movw	r0, #:lower16:.LC8
	mov	r2, #1952
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2044+4
	bl	sftl_printk
.L1909:
	ldr	r5, [r6, #8]
	mov	r2, #0
	ldr	ip, [r6, #12]
	add	r1, sp, #44
	mov	r0, r5
	str	ip, [sp, #40]
	bl	log2phys
	ldr	r2, [r4, #3260]
	cmn	r2, #1
	beq	.L1913
	ldr	r3, [sp, #20]
	cmp	r3, r2
	bls	.L1914
	sub	r2, r3, r2
	cmp	r2, #-2147483648
	bhi	.L1913
.L1915:
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L2036
	ldr	ip, [r4, #3336]
	mov	r2, #0
	mov	r1, #1
	add	ip, ip, r8
	str	r0, [ip, #4]
	mov	r0, ip
	ldr	r6, [ip, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3336]
	ldr	r1, [r2, r8]
	add	ip, r2, r8
	cmn	r1, #1
	beq	.L1917
	ldr	r2, [r6, #8]
	cmp	r5, r2
	beq	.L2037
.L1917:
	add	r8, r8, #20
	mvn	r2, #0
	cmp	r8, r7
	str	r2, [sp, #40]
	bne	.L1942
.L1903:
	ldr	r2, .L2044+16
	add	fp, fp, #1
	uxth	fp, fp
	ldrh	r2, [r2]
	cmp	r2, fp
	beq	.L1943
	ldrh	r2, [r4, #36]
	ldr	r0, [r4, #3336]
	cmp	r2, #0
	bne	.L2038
.L1899:
	mov	r1, r2
	bl	FlashReadPages
	b	.L1903
.L2024:
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #6]
	b	.L1856
.L1906:
	ldr	r2, [r4, #3536]
	ldr	r3, [sp, #16]
	cmp	r2, #31
	addls	r1, r4, r2, lsl #2
	addls	r2, r2, #1
	strls	r2, [r4, #3536]
	strls	r9, [r1, #3540]
	ldrh	r0, [r3]
	bl	decrement_vpc_count
	ldr	r2, [r4, #3260]
	ldr	r3, [sp, #20]
	cmn	r2, #1
	beq	.L2015
	cmp	r2, r3
	bls	.L1904
.L2015:
	str	r3, [r4, #3260]
	b	.L1904
.L1914:
	ldr	r3, [sp, #20]
	sub	r2, r2, r3
	cmp	r2, #-2147483648
	bhi	.L1915
.L1913:
	ldr	r1, [sp, #44]
	ldr	r2, [sp, #48]
	cmp	r1, r2
	beq	.L2014
	ldr	r1, [sp, #40]
	cmn	r1, #1
	beq	.L1930
	ldr	r2, [r4, #256]
	cmp	r2, r1, lsr #10
	bls	.L2039
.L1930:
	mov	r2, #1
	add	r1, sp, #48
	mov	r0, r5
	bl	log2phys
	ldr	r6, [sp, #44]
	ldr	r9, [sp, #40]
	cmn	r6, #1
	beq	.L1929
	cmp	r6, r9
	beq	.L1932
	ldr	r3, .L2044+12
	ubfx	r0, r6, #10, #16
	ldrh	r1, [r3]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldrh	r2, [r4, #40]
	uxth	r0, r0
	cmp	r2, r0
	beq	.L1933
	ldrh	r2, [r4, #88]
	cmp	r2, r0
	beq	.L1933
	ldrh	r2, [r4, #136]
	cmp	r2, r0
	beq	.L1933
.L1929:
	cmn	r9, #1
	beq	.L1904
.L1932:
	ldr	r3, .L2044+12
	ubfx	r0, r9, #10, #16
	ldrh	r1, [r3]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldrh	r2, [r4, #20]
	uxth	r5, r0
	cmp	r2, r5
	bls	.L2040
.L1938:
	ldr	r1, [r4, #16]
	lsl	r2, r5, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1939
	mov	r0, r5
	bl	decrement_vpc_count
	b	.L1904
.L1910:
	ldr	r3, [sp, #20]
	sub	r2, r2, r3
	cmp	r2, #-2147483648
	bhi	.L1912
	b	.L1909
.L1933:
	ldr	ip, [r4, #3336]
	mov	r2, #0
	mov	r1, #1
	str	r6, [ip, #4]
	mov	r0, ip
	ldr	r6, [ip, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3336]
	ldr	r2, [r2]
	cmn	r2, #1
	beq	.L2014
	ldr	r2, [r6, #4]
	ldr	r3, [sp, #20]
	cmp	r3, r2
	bls	.L1935
	sub	r2, r3, r2
	cmp	r2, #-2147483648
	bls	.L2014
.L1936:
	add	r1, sp, #44
	mov	r0, r5
	mov	r2, #1
	bl	log2phys
.L2014:
	ldr	r9, [sp, #40]
	b	.L1929
.L1939:
	mov	r1, r5
	ldr	r0, [sp, #36]
	bl	sftl_printk
	b	.L1904
.L2036:
	ldr	r1, [sp, #44]
	ldr	r2, [sp, #48]
	cmp	r1, r2
	bne	.L1904
	mov	r0, r5
	mov	r2, #1
	add	r1, sp, #40
	bl	log2phys
	b	.L1904
.L2040:
	movw	r0, #:lower16:.LC8
	movw	r2, #2057
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2044+4
	bl	sftl_printk
	b	.L1938
.L1905:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #24]
	mov	r0, r3
	strb	r2, [r3, #6]
	strh	r1, [r3, #2]	@ movhi
	bl	ftl_sb_update_avl_pages
	b	.L1856
.L2039:
	movw	r0, #:lower16:.LC122
	movt	r0, #:upper16:.LC122
	bl	sftl_printk
	b	.L1904
.L2031:
	ldr	r2, [sp, #16]
	mov	r3, #0
	ldrh	r1, [sp, #24]
	strb	r3, [r2, #6]
	strh	r1, [r2, #2]	@ movhi
	strh	r3, [r2, #4]	@ movhi
	b	.L1878
.L2026:
	ldr	ip, [r4, #3336]
	uxth	r6, fp
	movw	r7, #65535
.L1868:
	ldr	r3, .L2044+8
	movw	r2, #306
	ldr	r5, [ip, #4]
	ldrh	r1, [r4, r2]
	mov	r0, r3
	str	r3, [sp, #8]
	add	r3, r6, #1
	ldrh	r9, [r0]
	uxth	r3, r3
	str	r3, [sp, #24]
	b	.L2013
.L2033:
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1880
	ldr	r0, [r4, #3336]
	b	.L1881
.L2032:
	mov	r1, r3
	ldr	r0, [sp, #16]
	bl	ftl_sb_update_avl_pages
	b	.L1856
.L2037:
	ldr	r9, [r6, #4]
	ldr	r2, [r4, #3260]
	cmp	r9, r2
	bcc	.L2041
	sub	r2, r9, r2
	cmp	r2, #-2147483648
	bls	.L1917
.L1920:
	ldr	r2, [sp, #44]
	ldr	r1, [sp, #48]
	cmp	r2, r1
	beq	.L1923
	ldr	r1, [sp, #40]
	cmp	r2, r1
	beq	.L1917
	cmn	r2, #1
	streq	r2, [ip]
	beq	.L1922
	str	r2, [ip, #4]
	mov	r1, #1
	mov	r2, #0
	mov	r0, ip
	ldr	r6, [ip, #12]
	bl	FlashReadPages
.L1922:
	ldr	r2, [r4, #3336]
	ldr	r2, [r2, r8]
	cmn	r2, #1
	beq	.L1923
	ldr	r2, [r6, #4]
	ldr	r1, [r4, #3260]
	cmp	r2, r1
	bcs	.L1924
	sub	r1, r1, r2
	cmp	r1, #-2147483648
	bhi	.L1923
.L1926:
	cmp	r9, r2
	bls	.L2042
	sub	r2, r9, r2
	cmp	r2, #-2147483648
	bhi	.L1917
.L1923:
	mov	r0, r5
	ldr	r1, [sp, #40]
	bl	FtlReUsePrevPpa
	b	.L1917
.L1935:
	ldr	r3, [sp, #20]
	sub	r2, r2, r3
	cmp	r2, #-2147483648
	bhi	.L2014
	b	.L1936
.L1943:
	ldrh	r0, [r4, #36]
	mov	r3, #0
	ldr	r2, [sp, #16]
	cmp	r0, r3
	strh	fp, [r2, #2]	@ movhi
	strh	r3, [r2, #4]	@ movhi
	beq	.L1856
	ldrh	ip, [r2, #16]
	movw	r2, #65535
	cmp	ip, r2
	ldreq	r2, [sp, #16]
	addeq	r2, r2, #16
	beq	.L1947
	b	.L1945
.L1948:
	ldrh	r1, [r2, #2]!
	cmp	r1, ip
	bne	.L2043
.L1947:
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, r0
	bne	.L1948
	b	.L1856
.L1957:
	mov	r5, #0
	b	.L1858
.L2029:
	movw	r0, #:lower16:.LC8
	movw	r2, #1800
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2044+4
	bl	sftl_printk
	b	.L1863
.L2034:
	mov	r1, r2
	bl	FlashReadPages
	ldr	r0, [r4, #3336]
	b	.L1889
.L2041:
	sub	r2, r2, r9
	cmp	r2, #-2147483648
	bhi	.L1917
	b	.L1920
.L2025:
	mov	r1, r2
	ldr	r0, [r4, #3336]
	bl	FlashReadPages
	ldr	r3, [r4, #2648]
	uxth	r6, fp
	movw	r7, #65535
	ldr	ip, [r4, #3336]
	sub	r3, r3, #1
	str	r3, [sp, #20]
	b	.L1868
.L2043:
	uxtb	r3, r3
.L1945:
	ldr	r2, [sp, #16]
	strb	r3, [r2, #6]
	b	.L1856
.L1924:
	sub	r1, r2, r1
	cmp	r1, #-2147483648
	bhi	.L1926
	b	.L1923
.L2042:
	sub	r2, r2, r9
	cmp	r2, #-2147483648
	bls	.L1917
	b	.L1923
.L2030:
	bl	__stack_chk_fail
.L2045:
	.align	2
.L2044:
	.word	.LANCHOR0+320
	.word	.LANCHOR1+512
	.word	.LANCHOR0+264
	.word	.LANCHOR0+306
	.word	.LANCHOR0+310
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	sftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_discard, %function
sftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r6, #:lower16:.LANCHOR0
	movt	r6, #:upper16:.LANCHOR0
	movw	r8, #:lower16:__stack_chk_guard
	movt	r8, #:upper16:__stack_chk_guard
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r6, #344]
	ldr	r2, [r8]
	cmp	r3, r1
	cmpcs	r3, r0
	str	r2, [sp, #12]
	bls	.L2058
	add	r2, r0, r1
	mov	r4, r1
	cmp	r3, r2
	mov	r7, r0
	bcc	.L2058
	cmp	r1, #31
	bls	.L2052
	movw	r3, #266
	ldrh	r9, [r6, r3]
	mov	r1, r9
	bl	__aeabi_uidiv
	smulbb	r3, r0, r9
	mov	r5, r0
	sub	r7, r7, r3
	uxth	r7, r7
	cmp	r7, #0
	beq	.L2049
	sub	r7, r9, r7
	add	r5, r0, #1
	cmp	r7, r4
	movcs	r7, r4
	uxth	r7, r7
	sub	r4, r4, r7
.L2049:
	cmp	r4, r9
	mvn	r3, #0
	ldrcs	r7, .L2070
	str	r3, [sp, #8]
	addcs	r9, r7, #40
	bcc	.L2055
.L2054:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2053
	ldr	ip, [r6, #3668]
	mov	r2, #1
	ldr	r3, [r6, #2616]
	add	r1, sp, #8
	mov	r0, r5
	add	ip, ip, r2
	add	r3, r3, r2
	str	ip, [r6, #3668]
	str	r3, [r6, #2616]
	bl	log2phys
	ldr	r0, [sp, #4]
	ldrh	r1, [r9]
	ubfx	r0, r0, #10, #16
	bl	__aeabi_uidivmod
	ldr	r3, .L2070+4
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	decrement_vpc_count
.L2053:
	ldrh	r3, [r7]
	add	r5, r5, #1
	sub	r4, r4, r3
	cmp	r3, r4
	bls	.L2054
.L2055:
	ldr	r3, [r6, #3668]
	cmp	r3, #32
	bhi	.L2068
.L2052:
	mov	r0, #0
.L2046:
	ldr	r2, [sp, #12]
	ldr	r3, [r8]
	cmp	r2, r3
	bne	.L2069
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2068:
	mov	r4, #0
	str	r4, [r6, #3668]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	r0, r4
	b	.L2046
.L2058:
	mvn	r0, #0
	b	.L2046
.L2069:
	bl	__stack_chk_fail
.L2071:
	.align	2
.L2070:
	.word	.LANCHOR0+266
	.word	.LANCHOR0+264
	.fnend
	.size	sftl_discard, .-sftl_discard
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	movw	r10, #:lower16:__stack_chk_guard
	movt	r10, #:upper16:__stack_chk_guard
	movw	r3, #65535
	ldrh	ip, [r5, #136]
	.pad #12
	sub	sp, sp, #12
	ldr	r2, [r10]
	movw	r1, #310
	ldrh	r1, [r5, r1]
	cmp	ip, r3
	str	r2, [sp, #4]
	beq	.L2073
	cmp	r0, #0
	movw	r4, #:lower16:.LANCHOR2
	movt	r4, #:upper16:.LANCHOR2
	beq	.L2074
	ldrh	r2, [r4, #4]
	cmp	r2, r3
	beq	.L2075
.L2076:
	mov	r1, #2
.L2074:
	ldr	r0, .L2104
	bl	FtlGcScanTempBlk
	ldrh	r3, [r4, #4]
	movw	r2, #65535
	str	r0, [sp]
	cmp	r3, r2
	movne	r0, #1
	beq	.L2100
.L2072:
	ldr	r2, [sp, #4]
	ldr	r3, [r10]
	cmp	r2, r3
	bne	.L2101
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2100:
	ldrh	r2, [r5, #136]
	mov	r1, #0
	str	r1, [r5, #3256]
	cmp	r2, r3
	beq	.L2089
	movw	r4, #310
	ldrb	r3, [r5, #143]	@ zero_extendqisi2
	ldrh	r1, [r5, r4]
	ldr	r9, .L2104+4
	mul	r0, r1, r3
	ldrh	ip, [r9]
	cmp	ip, r0
	beq	.L2079
	movw	r0, #:lower16:.LC8
	mov	r2, #164
	ldr	r1, .L2104+8
	movt	r0, #:upper16:.LC8
	bl	sftl_printk
	ldrh	r1, [r5, r4]
	ldrh	r2, [r5, #136]
	ldrb	r3, [r5, #143]	@ zero_extendqisi2
.L2079:
	smulbb	r3, r3, r1
	ldr	r1, [r5, #16]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	ldrh	r2, [r9]
	ldr	r3, [r5, #2632]
	cmp	r2, #0
	add	r3, r3, r2
	ldrne	fp, .L2104+12
	str	r3, [r5, #2632]
	movne	r4, #0
	beq	.L2087
.L2086:
	ldr	r7, [r5, #3276]
	add	r8, r4, r4, lsl #1
	ldr	r3, [r5, #2608]
	add	r6, r7, r8, lsl #2
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcs	.L2098
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r0, [r7, r8, lsl #2]
	ldr	r3, [sp]
	cmp	r0, r3
	beq	.L2102
	ldr	r2, [r6, #4]
	cmp	r3, r2
	beq	.L2084
.L2098:
	ldrh	r0, [r5, #136]
	bl	decrement_vpc_count
.L2084:
	add	r4, r4, #1
	ldrh	r3, [r9]
	uxth	r4, r4
	cmp	r3, r4
	bhi	.L2086
.L2087:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r5, #136]
	ldr	r2, [r5, #16]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2103
	bl	INSERT_DATA_LIST
.L2088:
	movw	r3, #3268
	mvn	r6, #0
	mov	r4, #0
	strh	r6, [r5, #136]	@ movhi
	strh	r4, [r5, r3]	@ movhi
	strh	r4, [r9]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #2676
	ldrh	r2, [r5, #236]
	ldrh	r3, [r5, r3]
	strh	r6, [r5, #184]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L2089
	movw	r3, #3208
	mov	r2, #20
	mov	r0, r4
	strh	r2, [r5, r3]	@ movhi
	b	.L2072
.L2102:
	ldrh	r1, [fp]
	ubfx	r0, r0, #10, #16
	bl	__aeabi_uidivmod
	ldr	r3, .L2104+16
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r7, r0
	mov	r2, #1
	add	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
	bl	decrement_vpc_count
	b	.L2084
.L2073:
	mov	r3, #0
	str	r3, [r5, #3256]
.L2089:
	mov	r0, #0
	b	.L2072
.L2103:
	bl	INSERT_FREE_LIST
	b	.L2088
.L2075:
	ldrh	r3, [r5, #236]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	cmp	r3, #17
	bhi	.L2076
	b	.L2074
.L2101:
	bl	__stack_chk_fail
.L2105:
	.align	2
.L2104:
	.word	.LANCHOR0+136
	.word	.LANCHOR0+3280
	.word	.LANCHOR1+536
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r5, #310
	add	r0, r4, #136
	ldrh	r1, [r4, r5]
	bl	FtlGcScanTempBlk
	ldrh	r3, [r4, r5]
	ldrh	r2, [r4, #138]
	cmp	r2, r3
	popcc	{r4, r5, r6, pc}
	ldr	r0, .L2109
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3256]
	pop	{r4, r5, r6, pc}
.L2110:
	.align	2
.L2109:
	.word	.LANCHOR0+2548
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r4, #:lower16:.LANCHOR0
	mov	r3, #0
	movt	r4, #:upper16:.LANCHOR0
	mov	r2, r3
	ldr	r1, [r4, #3224]
	ldr	r0, [r4, #3340]
	bl	FlashProgPages
	ldr	r3, [r4, #3224]
	cmp	r3, #0
	beq	.L2112
	mov	r3, #0
	mov	r5, r3
.L2115:
	ldr	r1, [r4, #3340]
	add	r3, r3, r3, lsl #2
	add	r5, r5, #1
	uxth	r5, r5
	ldr	r2, [r1, r3, lsl #2]
	add	r3, r1, r3, lsl #2
	ldr	r0, [r3, #12]
	cmn	r2, #1
	ldr	r1, [r3, #4]
	ldrne	r2, [r0, #8]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	ldr	r6, [r4, #3224]
	mov	r3, r5
	cmp	r5, r6
	bcc	.L2115
	cmp	r6, #0
	ldr	r7, [r4, #3340]
	beq	.L2112
	ldr	lr, [r4, #3244]
	mov	r5, #0
	ldr	r1, [r4, #3228]
	mov	r3, r5
	mov	r8, r5
.L2116:
	cmp	lr, #0
	beq	.L2118
	add	r3, r3, r3, lsl #2
	ldr	r2, [r1]
	add	r3, r7, r3, lsl #2
	ldr	ip, [r3, #8]
	cmp	r2, ip
	beq	.L2123
	mov	r3, #0
	b	.L2117
.L2119:
	ldr	r0, [r1, r2, lsl #2]
	add	r2, r1, r2, lsl #2
	cmp	r0, ip
	beq	.L2120
.L2117:
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, lr
	add	r2, r3, r3, lsl #1
	bcc	.L2119
.L2118:
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r6, r5
	mov	r3, r5
	bhi	.L2116
.L2112:
	ldrh	r3, [r4, #140]
	mov	r0, #0
	str	r0, [r4, #3224]
	cmp	r3, r0
	popne	{r4, r5, r6, r7, r8, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L2123:
	mov	r2, r1
.L2120:
	str	r8, [r2, #8]
	b	.L2118
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r3, #0
	add	r0, r4, #40
	str	r3, [r4, #3536]
	bl	FtlRecoverySuperblock
	ldrh	r3, [r4, #44]
	cmp	r3, #0
	beq	.L2139
	ldrh	r2, [r4, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2139
	add	r0, r4, #40
	bl	FtlSlcSuperblockCheck.part.7
.L2139:
	ldr	r0, .L2148
	bl	FtlRecoverySuperblock
	ldrh	r3, [r4, #92]
	cmp	r3, #0
	beq	.L2140
	ldrh	r2, [r4, #88]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2140
	ldr	r0, .L2148
	bl	FtlSlcSuperblockCheck.part.7
.L2140:
	bl	FtlGcPageRecovery
	movw	r0, #65535
	bl	decrement_vpc_count
	mov	r0, #0
	pop	{r4, pc}
.L2149:
	.align	2
.L2148:
	.word	.LANCHOR0+88
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movw	r7, #:lower16:__stack_chk_guard
	movt	r7, #:upper16:__stack_chk_guard
	movw	r0, #:lower16:.LC107
	movw	r5, #:lower16:.LANCHOR0
	.pad #12
	sub	sp, sp, #12
	ldr	r3, [r7]
	movt	r5, #:upper16:.LANCHOR0
	movt	r0, #:upper16:.LC107
	ldr	r1, .L2172
	str	r3, [sp, #4]
	bl	sftl_printk
	ldrh	r2, [r5, #248]
	mov	r1, #0
	ldr	r0, [r5, #3400]
	lsl	r2, r2, #1
	bl	memset
	ldr	r3, [r5, #2608]
	cmp	r3, #0
	ldrne	r6, .L2172+4
	movne	r4, #0
	beq	.L2156
.L2155:
	mov	r2, #0
	mov	r1, sp
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L2154
	ldrh	r1, [r6]
	ubfx	r0, r0, #10, #16
	bl	__aeabi_uidivmod
	ldr	r3, .L2172+8
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldr	r2, [r5, #3400]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L2154:
	ldr	r3, [r5, #2608]
	add	r4, r4, #1
	cmp	r3, r4
	bhi	.L2155
.L2156:
	ldrh	r3, [r5, #20]
	cmp	r3, #0
	movwne	r9, #:lower16:.LC124
	movne	r4, #0
	movtne	r9, #:upper16:.LC124
	movwne	r8, #65535
	beq	.L2153
.L2161:
	ldr	r2, [r5, #16]
	lsl	r6, r4, #1
	ldr	r3, [r5, #3400]
	ldrh	r2, [r2, r6]
	ldrh	r3, [r3, r6]
	cmp	r2, r8
	cmpne	r2, r3
	beq	.L2158
	mov	r1, r4
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r5, #40]
	cmp	r3, r4
	beq	.L2158
	ldrh	r3, [r5, #136]
	cmp	r3, r4
	beq	.L2158
	ldrh	r3, [r5, #88]
	cmp	r3, r4
	beq	.L2158
	ldr	r3, [r5, #16]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	ldr	r2, [r5, #3400]
	bne	.L2160
	ldrh	r2, [r2, r6]
	strh	r2, [r3, r6]	@ movhi
.L2158:
	add	r4, r4, #1
	ldrh	r3, [r5, #20]
	uxth	r4, r4
	cmp	r3, r4
	bhi	.L2161
.L2153:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	bne	.L2171
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2160:
	ldrh	r2, [r2, r6]
	mov	r0, r4
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	b	.L2158
.L2171:
	bl	__stack_chk_fail
.L2173:
	.align	2
.L2172:
	.word	.LANCHOR1+556
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	movw	r2, #2520
	movw	r3, #:lower16:.LC8
	mov	r7, r0
	mov	ip, r3
	ldrh	r1, [r5, r2]
	ldrh	r3, [r5, #236]
	mov	r0, ip
	ldrh	r2, [r5, #20]
	.pad #12
	sub	sp, sp, #12
	movt	r0, #:upper16:.LC8
	add	r3, r3, r1
	str	r0, [sp]
.L2175:
	cmp	r3, r2
	bgt	.L2223
.L2176:
	ldr	r3, .L2232
	cmp	r7, r3
	movne	r1, #0
	beq	.L2224
.L2177:
	ldr	r0, .L2232+4
	bl	List_pop_index_node
	ldrh	r3, [r5, #236]
	mov	r9, r0
	uxth	r8, r0
	cmp	r3, #0
	beq	.L2225
.L2178:
	ldrh	r2, [r5, #20]
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r8, r2
	strh	r3, [r5, #236]	@ movhi
	bcs	.L2179
	uxth	r9, r9
	ldr	r1, [r5, #16]
	lsl	r9, r9, #1
	ldrh	r1, [r1, r9]
	cmp	r1, #0
	bne	.L2179
	strh	r8, [r7]	@ movhi
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2226
	ldr	r2, .L2232+8
	ldrh	r3, [r5, #236]
	ldrh	r1, [r2]
	ldrh	r2, [r5, #20]
	add	r3, r3, r1
	cmp	r3, r2
	bgt	.L2227
	ldrh	r3, [r5, #36]
	cmp	r3, #0
	beq	.L2184
.L2230:
	sub	r0, r3, #1
	ldr	r10, [r5, #3348]
	uxth	r0, r0
	mov	r4, #0
	mov	lr, r4
	add	r0, r0, #1
	mov	r2, r10
	add	r0, r0, r0, lsl #2
	add	ip, r7, #14
	movw	fp, #65535
	add	r0, r10, r0, lsl #2
.L2186:
	str	lr, [r2, #8]
	add	r3, r4, r4, lsl #2
	str	lr, [r2, #12]
	add	r6, r4, #1
	ldrh	r1, [ip, #2]!
	add	r3, r10, r3, lsl #2
	add	r2, r2, #20
	cmp	r1, fp
	lsl	r1, r1, #10
	uxthne	r4, r6
	strne	r1, [r3, #4]
	cmp	r0, r2
	bne	.L2186
	cmp	r4, #0
	movne	r6, r4
	beq	.L2184
	ldrh	r3, [r5, #184]
	cmp	r3, r8
	beq	.L2228
.L2188:
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	ldr	r2, [r5, #2528]
	cmp	r3, #0
	bne	.L2189
	ldrh	r3, [r2, r9]
	lsr	r0, r8, #5
	mov	ip, #1
	cmp	r3, #0
	ldrne	r1, .L2232+12
	moveq	r3, #2
	ldrhne	r1, [r1]
	addne	r3, r3, r1
	strh	r3, [r2, r9]	@ movhi
	and	r3, r8, #31
	ldr	r2, [r5, #2652]
	ldr	r1, [r5, #32]
	add	r2, r2, ip
	str	r2, [r5, #2652]
	ldr	r2, [r1, r0, lsl #2]
	bic	r3, r2, ip, lsl r3
	str	r3, [r1, r0, lsl #2]
.L2192:
	ldr	r3, [r5, #2528]
	ldr	r2, [r5, #2668]
	ldr	r0, [r5, #2652]
	ldrh	r3, [r3, r9]
	ldrh	r1, [r5, #20]
	cmp	r3, r2
	strhi	r3, [r5, #2668]
	ldr	r3, .L2232+12
	ldrh	r2, [r3]
	ldr	r3, [r5, #2656]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r5, #3396]
	cmp	r4, #0
	str	r0, [r5, #2660]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	beq	.L2194
	sub	r4, r4, #1
	ldr	r0, [r5, #3348]
	uxth	r4, r4
	mov	r1, #64512
	movt	r1, 65535
	add	r4, r4, #1
	mov	r3, r0
	add	r4, r4, r4, lsl #2
	lsl	r4, r4, #2
	add	ip, r0, r4
.L2195:
	ldr	r2, [r3, #4]
	add	r3, r3, #20
	and	r2, r2, r1
	str	r2, [r3, #-16]
	cmp	ip, r3
	bne	.L2195
	mov	r2, r6
	ldrb	r1, [r7, #8]	@ zero_extendqisi2
	mov	r6, #0
	add	r10, r7, #16
	bl	FlashEraseBlocks
	str	r8, [sp, #4]
	mov	r8, r7
	mov	r7, r6
	b	.L2198
.L2197:
	cmp	r4, r6
	add	r10, r10, #2
	beq	.L2229
.L2198:
	ldr	r3, [r5, #3348]
	ldr	fp, [r3, r6]
	add	r3, r3, r6
	add	r6, r6, #20
	cmn	fp, #1
	bne	.L2197
	ldr	r0, [r3, #4]
	add	r7, r7, #1
	add	r10, r10, #2
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	strh	fp, [r10, #-2]	@ movhi
	cmp	r4, r6
	ldrb	r3, [r8, #7]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r8, #7]
	bne	.L2198
.L2229:
	mov	r2, r7
	mov	r7, r8
	cmp	r2, #0
	ldr	r8, [sp, #4]
	ble	.L2199
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2199:
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2200
.L2231:
	ldr	r3, [r5, #16]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
.L2222:
	ldr	r2, .L2232+8
	ldrh	r3, [r5, #236]
	ldrh	r1, [r2]
	ldrh	r2, [r5, #20]
	add	r3, r3, r1
	cmp	r3, r2
	ble	.L2176
.L2223:
	ldr	r1, .L2232+16
	mov	r2, #2656
	ldr	r0, [sp]
	bl	sftl_printk
	ldr	r3, .L2232
	cmp	r7, r3
	movne	r1, #0
	bne	.L2177
.L2224:
	ldrh	r2, [r5, #236]
	ldr	r3, [r5, #3220]
	lsr	r1, r2, #1
	mul	r3, r3, r2
	add	r1, r1, #1
	add	r1, r1, r3, lsr #2
	uxth	r1, r1
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2177
.L2179:
	ldr	r1, .L2232+8
	ldrh	r1, [r1]
	add	r3, r3, r1
	b	.L2175
.L2225:
	movw	r2, #2665
	ldr	r1, .L2232+16
	ldr	r0, [sp]
	bl	sftl_printk
	ldrh	r3, [r5, #236]
	b	.L2178
.L2226:
	ldr	r3, [r5, #16]
	mvn	r2, #0
	mov	r0, r8
	strh	r2, [r3, r9]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, .L2232+8
	ldrh	r3, [r5, #236]
	ldrh	r1, [r2]
	ldrh	r2, [r5, #20]
	add	r3, r3, r1
	cmp	r3, r2
	ble	.L2175
	movw	r2, #2679
	ldr	r1, .L2232+16
	ldr	r0, [sp]
	bl	sftl_printk
	b	.L2222
.L2189:
	ldrh	r3, [r2, r9]
	mov	r0, r8
	add	r3, r3, #1
	strh	r3, [r2, r9]	@ movhi
	ldr	r3, [r5, #2656]
	add	r3, r3, #1
	str	r3, [r5, #2656]
	bl	ftl_set_blk_mode.part.6
	b	.L2192
.L2227:
	movw	r2, #2682
	ldr	r1, .L2232+16
	ldr	r0, [sp]
	bl	sftl_printk
	ldrh	r3, [r5, #36]
	cmp	r3, #0
	bne	.L2230
.L2184:
	movw	r2, #2693
	ldr	r1, .L2232+16
	ldr	r0, [sp]
	mov	r4, #0
	bl	sftl_printk
	ldrh	r3, [r5, #184]
	mov	r6, r4
	cmp	r3, r8
	bne	.L2188
.L2228:
	movw	r2, #2695
	ldr	r1, .L2232+16
	ldr	r0, [sp]
	bl	sftl_printk
	b	.L2188
.L2194:
	mov	r2, r6
	ldrb	r1, [r7, #8]	@ zero_extendqisi2
	ldr	r0, [r5, #3348]
	bl	FlashEraseBlocks
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2231
.L2200:
	movw	r2, #310
	mov	r0, #0
	ldrh	r2, [r5, r2]
	strh	r0, [r7, #2]	@ movhi
	strh	r8, [r7]	@ movhi
	ldr	r1, [r5, #2644]
	smulbb	r3, r2, r3
	ldr	ip, [r5, #16]
	strb	r0, [r7, #6]
	add	r2, r1, #1
	str	r1, [r7, #12]
	str	r2, [r5, #2644]
	uxth	r3, r3
	ldrh	r2, [r7]
	strh	r3, [r7, #4]	@ movhi
	lsl	r2, r2, #1
	strh	r3, [ip, r2]	@ movhi
	ldrh	r3, [r7, #4]
	cmp	r3, r0
	beq	.L2201
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	cmp	r3, r0
	bne	.L2212
.L2201:
	movw	r0, #:lower16:.LC8
	movw	r2, #2748
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2232+16
	bl	sftl_printk
.L2212:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2233:
	.align	2
.L2232:
	.word	.LANCHOR0+136
	.word	.LANCHOR0+2524
	.word	.LANCHOR0+2520
	.word	.LANCHOR0+300
	.word	.LANCHOR1+580
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r5, #:lower16:.LANCHOR0
	movt	r5, #:upper16:.LANCHOR0
	ldrh	r4, [r0]
	mov	r6, r0
	ldrh	r3, [r5, #20]
	cmp	r3, r4
	bcc	.L2251
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2236
.L2253:
	ldr	r3, [r5, #16]
	lsl	r8, r4, #1
	mov	r0, r4
	ldrh	r3, [r3, r8]
	cmp	r3, #0
	bne	.L2252
	bl	INSERT_FREE_LIST
.L2238:
	ldr	r7, .L2254
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r2, #65535
	ldrh	r3, [r7]
	cmp	r3, r2
	beq	.L2240
	cmp	r4, r3
	bne	.L2244
	ldr	r3, [r5, #16]
	ldrh	r3, [r3, r8]
	cmp	r3, #0
	bne	.L2241
.L2242:
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L2240:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2252:
	bl	INSERT_DATA_LIST
	b	.L2238
.L2244:
	mov	r4, r3
.L2241:
	mov	r0, r4
	bl	update_vpc_list
	b	.L2242
.L2251:
	movw	r0, #:lower16:.LC8
	movw	r2, #2755
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2254+4
	bl	sftl_printk
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2253
.L2236:
	ldr	r7, .L2254
	mov	r2, #1
	strb	r2, [r6, #8]
	ldrh	r4, [r7]
	cmp	r4, r3
	beq	.L2240
	mov	r0, r4
	bl	update_vpc_list
	b	.L2242
.L2255:
	.align	2
.L2254:
	.word	.LANCHOR0+3248
	.word	.LANCHOR1+608
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #:lower16:__stack_chk_guard
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	r2, #:upper16:__stack_chk_guard
	.pad #20
	sub	sp, sp, #20
	mov	fp, r1
	mov	r5, r3
	ldr	ip, [r2]
	mov	r4, r0
	str	r2, [sp, #4]
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movw	r10, #:lower16:.LANCHOR0
	str	ip, [sp, #12]
	bl	FlashProgPages
	cmp	fp, #0
	movteq	r10, #:upper16:.LANCHOR0
	beq	.L2257
	ldr	r7, .L2280
	movw	r8, #:lower16:.LC125
	movt	r10, #:upper16:.LANCHOR0
	movt	r8, #:upper16:.LC125
	mov	r9, #0
.L2258:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	ldreq	r6, .L2280+4
	beq	.L2260
	b	.L2265
.L2259:
	mov	r0, r5
	bl	get_new_active_ppa
	mov	ip, r0
	mov	r2, #0
	str	ip, [r4, #4]
	mov	r0, r4
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r1, #1
	str	ip, [sp, #8]
	bl	FlashProgPages
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	bne	.L2265
.L2260:
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ldrh	r1, [r7]
	ubfx	r0, r0, #10, #16
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r6]
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L2259
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L2259
.L2265:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #36]
	cmp	r2, r3
	bcs	.L2277
.L2261:
	ldr	r3, [r4, #4]
	mov	r2, #1
	add	r1, sp, #8
	ldr	r0, [r4, #16]
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2262
	ldr	r6, .L2280+4
	ubfx	r0, r0, #10, #16
	ldrh	r1, [r7]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r6]
	bl	__aeabi_uidiv
	uxth	r6, r0
	ldr	r2, [r10, #16]
	lsl	r3, r6, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2278
.L2263:
	mov	r0, r6
	bl	decrement_vpc_count
.L2262:
	add	r9, r9, #1
	add	r4, r4, #20
	cmp	fp, r9
	bne	.L2258
.L2257:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #36]
	cmp	r2, r3
	bcc	.L2256
	movw	r0, #:lower16:.LC8
	mov	r2, #1000
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2280+8
	bl	sftl_printk
.L2256:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2279
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2277:
	movw	r0, #:lower16:.LC8
	movw	r2, #985
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2280+8
	bl	sftl_printk
	b	.L2261
.L2278:
	movw	r0, #:lower16:.LC126
	mov	r1, r6
	movt	r0, #:upper16:.LC126
	bl	sftl_printk
	b	.L2263
.L2279:
	bl	__stack_chk_fail
.L2281:
	.align	2
.L2280:
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.word	.LANCHOR1+640
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r3, #0
	add	ip, r4, #416
	mov	r1, r3
	ldrh	lr, [r4, #244]
	mov	r2, #2048
	movw	r5, #3534
	add	r0, r4, #424
	strh	r3, [r4, r5]	@ movhi
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [ip, #4]	@ movhi
	strh	r3, [ip, #6]	@ movhi
	strh	lr, [ip]	@ movhi
	bl	memset
	bl	FtlScanSysBlk
	movw	r2, #2680
	movw	r3, #65535
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	beq	.L2285
	bl	FtlLoadSysInfo
	subs	r5, r0, #0
	bne	.L2285
	bl	FtlL2PDataInit
	ldr	r0, .L2298
	bl	FtlMapTblRecovery
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movw	r3, #2538
	str	r5, [r4, #28]
	strh	r5, [r4, r3]	@ movhi
	bl	FtlUpdateVaildLpn.part.5
	movw	r3, #342
	ldrh	r0, [r4, r3]
	cmp	r0, #0
	beq	.L2286
	ldr	r3, [r4, #2532]
	ldr	r2, [r3, #4]
	cmp	r2, #0
	blt	.L2287
	mov	r2, r5
	b	.L2288
.L2289:
	ldr	r1, [r3, #16]
	add	r3, r3, #12
	cmp	r1, #0
	blt	.L2287
.L2288:
	add	r2, r2, #1
	cmp	r2, r0
	blt	.L2289
.L2286:
	ldr	r6, .L2298+4
	movw	r3, #3534
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, #28]
	cmp	r2, #0
	add	r3, r3, #1
	strh	r3, [r6, #28]	@ movhi
	bne	.L2291
.L2292:
	ldrh	r0, [r4, #40]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2290
	ldrh	r3, [r4, #44]
	cmp	r3, #0
	beq	.L2297
.L2290:
	bl	FtlVpcCheckAndModify
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L2287:
	ldr	r6, .L2298+4
	ldrh	r3, [r6, #28]
	add	r3, r3, #1
	strh	r3, [r6, #28]	@ movhi
.L2291:
	ldr	r0, .L2298+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2298+12
	bl	FtlSuperblockPowerLostFix
	ldrh	r2, [r4, #40]
	movw	r0, #310
	ldr	ip, [r4, #16]
	mov	r3, #0
	ldrh	lr, [r4, #44]
	lsl	r2, r2, #1
	ldrh	r1, [ip, r2]
	sub	r1, r1, lr
	strh	r1, [ip, r2]	@ movhi
	ldrh	r2, [r4, #88]
	ldr	ip, [r4, #16]
	ldrh	r1, [r4, r0]
	ldrh	lr, [r4, #92]
	lsl	r2, r2, #1
	strb	r3, [r4, #46]
	strh	r3, [r4, #44]	@ movhi
	strh	r1, [r4, #42]	@ movhi
	ldrh	r1, [ip, r2]
	sub	r1, r1, lr
	strh	r1, [ip, r2]	@ movhi
	ldrh	r2, [r6, #30]
	ldrh	r1, [r4, r0]
	strb	r3, [r4, #94]
	add	r2, r2, #1
	strh	r3, [r4, #92]	@ movhi
	strh	r1, [r4, #90]	@ movhi
	strh	r2, [r6, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L2292
.L2297:
	ldrh	r3, [r4, #92]
	cmp	r3, #0
	bne	.L2290
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #88]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L2298+8
	bl	allocate_new_data_superblock
	ldr	r0, .L2298+12
	bl	allocate_new_data_superblock
	b	.L2290
.L2285:
	mvn	r5, #0
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L2299:
	.align	2
.L2298:
	.word	.LANCHOR0+2548
	.word	.LANCHOR0+2472
	.word	.LANCHOR0+40
	.word	.LANCHOR0+88
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	movw	r3, #65535
	ldrh	r2, [r4, #136]
	cmp	r2, r3
	beq	.L2301
	ldrh	r3, [r4, #140]
	cmp	r3, #0
	bne	.L2302
.L2301:
	mov	r0, #0
	mov	r5, r0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2304
	strb	r5, [r4, #144]
	bl	allocate_data_superblock
	ldr	r3, .L2304+4
	movw	r2, #3268
	strh	r5, [r4, r2]	@ movhi
	strh	r5, [r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2302:
	ldr	r0, .L2304
	pop	{r4, r5, r6, lr}
	b	get_new_active_ppa
.L2305:
	.align	2
.L2304:
	.word	.LANCHOR0+136
	.word	.LANCHOR0+3280
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #:lower16:__stack_chk_guard
	movw	fp, #:lower16:.LANCHOR0
	.pad #52
	sub	sp, sp, #52
	movt	r3, #:upper16:__stack_chk_guard
	movt	fp, #:upper16:.LANCHOR0
	mov	r2, r3
	str	r3, [sp, #20]
	ldr	r3, [fp, #3320]
	ldr	r2, [r2]
	str	r0, [sp, #16]
	cmp	r3, #0
	str	r2, [sp, #44]
	bne	.L2411
	movw	r2, #2520
	ldrh	r2, [fp, r2]
	cmp	r2, #47
	bls	.L2411
	movw	r3, #:lower16:.LANCHOR2
	movw	r2, #65535
	movt	r3, #:upper16:.LANCHOR2
	ldrh	r3, [r3, #4]
	cmp	r3, r2
	beq	.L2311
	ldrh	r3, [fp, #136]
	cmp	r3, r2
	beq	.L2311
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L2311
	mov	r0, #1
	b	.L2306
.L2411:
	mov	r0, #0
.L2306:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2478
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2311:
	ldrh	r3, [fp, #232]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L2310
	ldrh	r1, [fp, #234]
	cmp	r1, r2
	mvneq	r2, #0
	strheq	r3, [fp, #234]	@ movhi
	strheq	r2, [fp, #232]	@ movhi
.L2310:
	bl	FtlReadRefresh
	ldr	r3, [fp, #3212]
	movw	r2, #65535
	ldr	r0, [sp, #16]
	ldrh	r1, [fp, #184]
	add	r3, r3, #1
	cmp	r1, r2
	add	r3, r3, r0, lsl #7
	strne	r2, [sp, #12]
	movne	r5, #1
	str	r3, [fp, #3212]
	beq	.L2479
.L2312:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	movne	r3, #0
	andeq	r3, r5, #1
	cmp	r3, #0
	beq	.L2323
.L2405:
	ldrh	r3, [fp, #236]
	cmp	r3, #24
	movhi	r10, #1
	bhi	.L2324
	cmp	r3, #16
	movw	r2, #310
	bls	.L2325
	ldrh	r2, [fp, r2]
	lsr	r10, r2, #5
.L2324:
	movw	r2, #3208
	ldrh	r1, [fp, r2]
	cmp	r1, r3
	bcs	.L2328
	ldrh	r3, [fp, #136]
	movw	r1, #65535
	cmp	r3, r1
	beq	.L2480
.L2329:
	movw	r3, #2676
	movw	r2, #3208
	ldrh	r3, [fp, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [fp, r2]	@ movhi
.L2328:
	movw	r3, #3282
	ldrh	r3, [fp, r3]
	cmp	r3, #0
	addne	r3, r10, #32
	uxthne	r10, r3
.L2334:
	ldrh	r4, [fp, #184]
	movw	r3, #65535
	cmp	r4, r3
	strne	r3, [sp, #12]
	beq	.L2346
.L2338:
	ldrh	r3, [fp, #40]
	cmp	r3, r4
	beq	.L2355
.L2484:
	ldrh	r3, [fp, #88]
	cmp	r3, r4
	beq	.L2355
	ldrh	r3, [fp, #136]
	cmp	r3, r4
	beq	.L2355
.L2400:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2357
.L2485:
	ldrh	r3, [fp, #186]
	ldr	r2, .L2499
	mov	r1, r3
	str	r2, [sp, #24]
.L2358:
	ldr	r2, [sp, #16]
	cmp	r2, #0
	mov	r2, #1
	str	r2, [fp, #3320]
	ldr	r2, [sp, #24]
	ldrh	r2, [r2]
	mov	lr, r2
	str	r2, [sp, #28]
	beq	.L2375
	ldrh	r0, [fp, #184]
	ldrh	r2, [fp, #36]
	ldr	ip, [fp, #16]
	lsl	r0, r0, #1
	mul	r2, lr, r2
	ldrh	r0, [ip, r0]
	sub	r2, r2, r0
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	add	r10, r10, r2, asr #2
	uxth	r10, r10
.L2375:
	ldr	r2, [sp, #28]
	add	r1, r10, r1
	cmp	r1, r2
	subgt	r10, r2, r3
	uxthgt	r10, r10
	cmp	r10, #0
	movne	r7, #0
	strne	r10, [sp, #8]
	beq	.L2378
.L2389:
	ldrh	r1, [fp, #36]
	cmp	r1, #0
	beq	.L2390
	sub	ip, r1, #1
	ldr	r2, .L2499+4
	uxth	ip, ip
	ldrh	lr, [fp, #186]
	ldr	r0, [fp, #3240]
	mov	r4, #0
	add	ip, ip, #9
	sub	r3, r2, #16
	add	ip, r3, ip, lsl #1
	add	lr, lr, r7
	movw	r5, #65535
.L2380:
	ldrh	r3, [r2], #2
	cmp	r3, r5
	beq	.L2379
	add	r1, r4, r4, lsl #2
	add	r4, r4, #1
	uxth	r4, r4
	orr	r3, lr, r3, lsl #10
	add	r1, r0, r1, lsl #2
	str	r3, [r1, #4]
.L2379:
	cmp	ip, r2
	bne	.L2380
	ldrb	r2, [fp, #192]	@ zero_extendqisi2
	mov	r1, r4
	bl	FlashReadPages
	cmp	r4, #0
	beq	.L2382
	sub	r8, r4, #1
	movw	r5, #:lower16:.LC8
	uxth	r8, r8
	ldr	r6, .L2499+8
	movt	r5, #:upper16:.LC8
	add	r8, r8, #1
	mov	r4, #0
	add	r8, r8, r8, lsl #2
	lsl	r8, r8, #2
	b	.L2387
.L2384:
	add	r4, r4, #20
	cmp	r8, r4
	beq	.L2382
.L2387:
	ldr	r3, [fp, #3240]
	ldr	r2, [r3, r4]
	add	r3, r3, r4
	cmn	r2, #1
	beq	.L2384
	ldr	r9, [r3, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L2384
	ldr	r10, [r9, #8]
	cmn	r10, #1
	beq	.L2481
.L2385:
	mov	r2, #0
	add	r1, sp, #36
	mov	r0, r10
	bl	log2phys
	ldr	r3, [fp, #3240]
	ldr	r2, [sp, #36]
	add	r3, r3, r4
	ldr	r1, [r3, #4]
	cmp	r1, r2
	bne	.L2384
	ldr	r2, [fp, #3224]
	ldr	r1, [r3, #16]
	ldr	r10, [fp, #3340]
	ldr	r3, .L2499+12
	add	r2, r2, r2, lsl #2
	ldrh	r3, [r3]
	add	r10, r10, r2, lsl #2
	ldr	r2, .L2499+12
	str	r1, [r10, #16]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	Ftl_get_new_temp_ppa
	ldr	ip, [fp, #3224]
	mov	r1, #1
	ldr	r2, [fp, #3240]
	ldr	r3, [fp, #3340]
	str	r0, [r10, #4]
	add	r2, r2, r4
	add	ip, ip, ip, lsl #2
	ldr	lr, [r2, #8]
	add	r3, r3, ip, lsl #2
	ldrh	r0, [fp, #136]
	ldr	ip, [sp, #36]
	str	lr, [r3, #8]
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	strh	r0, [r9, #2]	@ movhi
	ldr	r3, [fp, #3224]
	ldr	r2, [fp, #2648]
	ldr	r0, [fp, #3240]
	add	r3, r3, r1
	str	ip, [r9, #12]
	str	r2, [r9, #4]
	add	r0, r0, r4
	str	r3, [fp, #3224]
	bl	FtlGcBufAlloc
	ldrb	r2, [fp, #143]	@ zero_extendqisi2
	ldr	r3, [fp, #3224]
	cmp	r2, r3
	beq	.L2386
	ldrh	r3, [fp, #140]
	cmp	r3, #0
	bne	.L2384
.L2386:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L2384
	mov	r3, #0
	mvn	r1, #0
	movw	r2, #3672
	str	r3, [fp, #3320]
	strh	r3, [fp, #186]	@ movhi
	strh	r1, [fp, #184]	@ movhi
	ldrh	r0, [fp, r2]
	b	.L2306
.L2323:
	ldrh	r2, [fp, #136]
	movw	r3, #65535
	cmp	r2, r3
	ldrhne	r4, [fp, #184]
	beq	.L2482
.L2336:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2483
.L2418:
	ldrh	r3, [fp, #40]
	mov	r10, #1
	cmp	r3, r4
	bne	.L2484
.L2355:
	mvn	r3, #0
	movw	r4, #65535
	strh	r3, [fp, #184]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2485
.L2357:
	ldr	r3, .L2499+16
	ldr	r5, .L2499
	ldrh	r6, [r3]
	mov	r3, #0
	str	r3, [fp, #3220]
.L2359:
	mov	r0, r6
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	strh	r3, [fp, #184]	@ movhi
	beq	.L2360
	ldr	r2, .L2499+20
	ldrh	lr, [r2]
	cmp	lr, #0
	beq	.L2361
	ldr	r2, [fp, #3272]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L2362
	sub	ip, lr, #1
	uxth	ip, ip
	add	ip, r2, ip, lsl #1
	b	.L2364
.L2365:
	ldrh	r1, [r2, #2]!
	cmp	r1, r3
	beq	.L2362
.L2364:
	cmp	ip, r2
	bne	.L2365
.L2361:
	ldrh	r1, [r5]
	uxth	r2, r0
	ldrh	r0, [fp, #36]
	add	ip, r6, #1
	ldr	r8, .L2499+16
	uxth	ip, ip
	ldr	r7, [fp, #16]
	lsl	r2, r2, #1
	mov	r6, r5
	mul	r0, r0, r1
	strh	ip, [r8]	@ movhi
	ldrh	r1, [r7, r2]
	add	r8, r0, r0, lsr #31
	cmp	r1, r8, asr #1
	bgt	.L2403
	cmp	r1, #8
	cmphi	ip, #48
	movhi	r8, #1
	movls	r8, #0
	cmp	lr, #35
	movhi	lr, #0
	andls	lr, r8, #1
	cmp	lr, #0
	bne	.L2403
	ldr	r2, [sp, #12]
	cmp	r1, r0
	cmpge	r2, r4
	bne	.L2367
	cmp	ip, #3
	bls	.L2368
.L2367:
	cmp	r1, #0
	bne	.L2369
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2499+16
	ldrh	r6, [r3]
	add	r6, r6, #1
	uxth	r6, r6
	strh	r6, [r3]	@ movhi
	b	.L2359
.L2390:
	ldrb	r2, [fp, #192]	@ zero_extendqisi2
	ldr	r0, [fp, #3240]
	bl	FlashReadPages
.L2382:
	add	r7, r7, #1
	ldr	r2, [sp, #8]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L2389
	ldrh	r3, [fp, #186]
	mov	r10, r2
.L2378:
	add	r3, r10, r3
	ldr	r2, [sp, #28]
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [fp, #186]	@ movhi
	bhi	.L2391
	ldr	r3, [fp, #3224]
	cmp	r3, #0
	bne	.L2486
.L2392:
	ldr	r3, .L2499+12
	ldrh	r4, [r3]
	cmp	r4, #0
	bne	.L2393
	ldrh	r2, [fp, #184]
	ldr	r3, [fp, #16]
	lsl	r2, r2, #1
	ldrh	r1, [r3, r2]
	add	r3, r3, r2
	cmp	r1, #0
	beq	.L2393
	ldr	r2, [fp, #2608]
	cmp	r2, #0
	beq	.L2407
	ldr	r5, .L2499+24
	b	.L2397
.L2395:
	ldr	r3, [fp, #2608]
	add	r4, r4, #1
	cmp	r4, r3
	bcs	.L2487
.L2397:
	mov	r2, #0
	add	r1, sp, #40
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L2395
	ubfx	r0, r0, #10, #16
	ldrh	r1, [r5]
	bl	__aeabi_uidivmod
	ldr	r3, .L2499+28
	uxth	r0, r1
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldrh	r2, [fp, #184]
	uxth	r0, r0
	cmp	r2, r0
	bne	.L2395
	ldr	r3, [fp, #2608]
	cmp	r3, r4
	bls	.L2488
.L2393:
	mvn	r3, #0
	strh	r3, [fp, #184]	@ movhi
.L2391:
	ldrh	r0, [fp, #236]
	mov	r3, #0
	str	r3, [fp, #3320]
	cmp	r0, #2
	bhi	.L2399
	ldr	r3, [sp, #24]
	ldrh	r4, [fp, #184]
	ldrh	r10, [r3]
	b	.L2400
.L2481:
	movw	r2, #753
	mov	r1, r6
	mov	r0, r5
	bl	sftl_printk
	b	.L2385
.L2362:
	add	r6, r6, #1
	ldr	r3, .L2499+16
	uxth	r6, r6
	strh	r6, [r3]	@ movhi
	b	.L2359
.L2403:
	ldr	ip, .L2499+16
	mov	r1, #0
	strh	r1, [ip]	@ movhi
	ldrh	r1, [r7, r2]
	ldr	r2, [sp, #12]
	cmp	r0, r1
	cmple	r2, r4
	bne	.L2367
.L2368:
	mvn	r1, #0
	mov	r2, #0
	strh	r1, [fp, #184]	@ movhi
	movw	r3, #3672
	ldr	r1, .L2499+16
	ldrh	r0, [fp, r3]
	strh	r2, [r1]	@ movhi
	b	.L2306
.L2360:
	ldr	r2, .L2499+16
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r2]	@ movhi
	b	.L2306
.L2479:
	ldrh	r2, [fp, #136]
	cmp	r2, r1
	beq	.L2489
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L2405
.L2419:
	mov	r10, #1
.L2346:
	ldrh	r4, [fp, #234]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2490
	ldr	r1, [fp, #16]
	lsl	r2, r4, #1
	str	r3, [sp, #12]
	ldrh	r2, [r1, r2]
	mvn	r1, #0
	strh	r1, [fp, #234]	@ movhi
	cmp	r2, #0
	moveq	r4, r3
	strh	r4, [fp, #184]	@ movhi
.L2347:
	movw	r2, #65535
	mov	r3, #0
	cmp	r4, r2
	strb	r3, [fp, #192]
	beq	.L2338
	movw	r3, #3268
	ldrh	r2, [fp, r3]
	cmp	r2, #0
	beq	.L2350
	ldr	r3, [fp, #3272]
	ldrh	r1, [r3]
	cmp	r1, r4
	beq	.L2351
	sub	r2, r2, #1
	uxth	r2, r2
	add	r2, r3, r2, lsl #1
	b	.L2353
.L2354:
	ldrh	r1, [r3, #2]!
	cmp	r1, r4
	beq	.L2351
.L2353:
	cmp	r2, r3
	bne	.L2354
.L2350:
	ldr	r0, .L2499+32
	bl	make_superblock
	ldrh	r4, [fp, #184]
	mov	r3, #0
	ldr	r1, [fp, #16]
	movw	r0, #3674
	strh	r3, [fp, #186]	@ movhi
	strb	r3, [fp, #190]
	lsl	r2, r4, #1
	strh	r3, [fp, r0]	@ movhi
	movw	r3, #3676
	ldrh	r2, [r1, r2]
	strh	r2, [fp, r3]	@ movhi
	b	.L2338
.L2369:
	ldrh	r2, [fp, #40]
	mov	r1, #0
	str	r6, [sp, #24]
	strb	r1, [fp, #192]
	cmp	r2, r3
	beq	.L2491
.L2371:
	ldrh	r2, [fp, #88]
	cmp	r2, r3
	beq	.L2492
.L2372:
	ldrh	r2, [fp, #136]
	cmp	r2, r3
	beq	.L2493
.L2373:
	ldr	r0, .L2499+32
	bl	make_superblock
	ldrh	r0, [fp, #184]
	mov	r2, #0
	ldr	r3, .L2499+12
	mov	r1, r2
	ldr	lr, [fp, #16]
	ldr	ip, .L2499+36
	lsl	r0, r0, #1
	strh	r2, [r3]	@ movhi
	mov	r3, r2
	ldrh	r0, [lr, r0]
	strh	r2, [fp, #186]	@ movhi
	strb	r2, [fp, #190]
	strh	r0, [ip]	@ movhi
	b	.L2358
.L2486:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L2392
	mov	r2, #0
	movw	r3, #3672
	str	r2, [fp, #3320]
	ldrh	r0, [fp, r3]
	b	.L2306
.L2325:
	cmp	r3, #12
	ldrhhi	r2, [fp, r2]
	lsrhi	r10, r2, #4
	bhi	.L2324
.L2326:
	cmp	r3, #8
	ldrhhi	r2, [fp, r2]
	ldrhls	r10, [fp, r2]
	lsrhi	r10, r2, #2
	b	.L2324
.L2489:
	ldrh	r5, [fp, #234]
	cmp	r5, r2
	beq	.L2494
.L2314:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L2405
	movw	r4, #65535
	cmp	r5, r4
	beq	.L2408
.L2409:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2419
.L2402:
	strh	r4, [fp, #184]	@ movhi
	mov	r10, #1
	str	r4, [sp, #12]
	b	.L2347
.L2487:
	ldrh	r2, [fp, #184]
	ldr	r3, [fp, #16]
	add	r3, r3, r2, lsl #1
.L2407:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	ldrh	r0, [fp, #184]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2393
.L2482:
	ldrh	r3, [fp, #234]
	ldrh	r4, [fp, #184]
	cmp	r3, r2
	bne	.L2336
	cmp	r5, #0
	bne	.L2495
	cmp	r4, r3
	bne	.L2418
	ldr	r4, [sp, #12]
	b	.L2402
.L2480:
	ldrh	r1, [fp, #234]
	cmp	r1, r3
	bne	.L2329
	movw	r3, #3672
	ldrh	r0, [fp, r3]
	cmp	r0, #0
	bne	.L2330
	ldr	r3, [fp, #2608]
	ldr	r1, [fp, #28]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	strhcs	r3, [fp, r2]	@ movhi
	bcs	.L2332
.L2330:
	movw	r3, #2676
	movw	r2, #3208
	ldrh	r3, [fp, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [fp, r2]	@ movhi
.L2332:
	mov	r3, #0
	str	r3, [fp, #3220]
	b	.L2306
.L2351:
	mvn	r3, #0
	movw	r4, #65535
	strh	r3, [fp, #184]	@ movhi
	b	.L2338
.L2490:
	str	r4, [sp, #12]
	ldrh	r4, [fp, #184]
	b	.L2347
.L2483:
	ldr	r4, [sp, #12]
	b	.L2409
.L2495:
	cmp	r4, r3
	strne	r3, [sp, #12]
	movne	r10, #1
	bne	.L2338
.L2408:
	movw	r2, #3208
	ldrh	r1, [fp, #236]
	ldrh	r3, [fp, r2]
	mov	r0, #0
	str	r0, [fp, #3220]
	cmp	r1, r3
	bls	.L2339
	movw	r3, #3672
	ldrh	r3, [fp, r3]
	cmp	r3, r0
	bne	.L2340
	ldr	r3, [fp, #2608]
	ldr	r1, [fp, #28]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	strhcs	r3, [fp, r2]	@ movhi
	bcs	.L2342
.L2340:
	movw	r3, #2676
	movw	r2, #3208
	ldrh	r3, [fp, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [fp, r2]	@ movhi
.L2342:
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r3, r0
	ldr	r2, [fp, #16]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L2496
.L2339:
	movw	r5, #3672
	ldrh	r0, [fp, r5]
	cmp	r0, #0
	beq	.L2343
.L2345:
	mov	r10, #1
	b	.L2334
.L2496:
	movw	r3, #3672
	ldrh	r0, [fp, r3]
	b	.L2306
.L2494:
	ldrh	r2, [fp, #236]
	cmp	r2, #23
	movhi	r2, #1024
	movls	r2, #5120
	cmp	r3, r2
	bls	.L2314
	mov	r7, #0
	movw	r3, #3672
	str	r7, [fp, #3212]
	strh	r7, [fp, r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	str	r0, [sp, #12]
	beq	.L2497
	ldrh	r6, [fp, #236]
	ldr	r1, [sp, #12]
	ldr	r5, [fp, #16]
.L2321:
	movw	r3, #3208
	ldr	lr, [fp, #2528]
	ldrh	r0, [fp, r3]
	lsl	ip, r1, #1
	mov	r2, r6
	str	r1, [sp, #12]
	ldrh	r3, [r5, ip]
	mov	r5, #0
	str	r0, [sp, #4]
	movw	r0, #:lower16:.LC127
	ldrh	ip, [lr, ip]
	movt	r0, #:upper16:.LC127
	str	ip, [sp]
	bl	sftl_printk
.L2319:
	bl	FtlGcReFreshBadBlk
	b	.L2312
.L2343:
	movw	r3, #2676
	movw	r2, #3208
	ldrh	r4, [fp, r3]
	add	r3, r4, r4, lsl #1
	asr	r3, r3, #2
	strh	r3, [fp, r2]	@ movhi
	bl	List_get_gc_head_node
	ldr	r1, .L2499+40
	uxth	r2, r0
	ldrh	r3, [fp, #36]
	ldr	r0, [fp, #16]
	lsl	r2, r2, #1
	ldrh	r1, [r1]
	ldrh	r2, [r0, r2]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L2344
	ldrh	r3, [fp, #236]
	sub	r4, r4, #1
	cmp	r3, r4
	bge	.L2498
.L2344:
	cmp	r2, #0
	bne	.L2345
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [fp, #236]
	add	r0, r0, #1
	b	.L2306
.L2497:
	movw	r4, #3210
	ldrh	r6, [fp, #236]
	ldrh	r3, [fp, r4]
	cmp	r3, r6
	bls	.L2317
	mov	r0, #64
	bl	List_get_gc_head_node
	ldr	r2, [sp, #12]
	uxth	r3, r0
	cmp	r3, r2
	beq	.L2322
	uxth	r3, r0
	ldr	r5, [fp, #16]
	lsl	r3, r3, #1
	ldrh	r3, [r5, r3]
	cmp	r3, #7
	bhi	.L2320
	mov	r0, r7
	bl	List_get_gc_head_node
	mov	r3, #128
	uxth	r1, r0
	strh	r3, [fp, r4]	@ movhi
	ldr	r3, [sp, #12]
	cmp	r1, r3
	bne	.L2321
.L2322:
	mov	r5, #1
	b	.L2319
.L2317:
	mov	r3, #80
	mov	r5, #1
	strh	r3, [fp, r4]	@ movhi
	b	.L2319
.L2498:
	bl	FtlReadRefresh
	ldrh	r0, [fp, r5]
	b	.L2306
.L2320:
	mov	r3, #64
	mov	r5, #1
	strh	r3, [fp, r4]	@ movhi
	b	.L2319
.L2399:
	movw	r3, #3672
	ldrh	r3, [fp, r3]
	cmp	r3, #0
	movne	r0, r3
	addeq	r0, r0, #1
	b	.L2306
.L2488:
	ldr	r3, [fp, #16]
	add	r3, r3, r2, lsl #1
	b	.L2407
.L2478:
	bl	__stack_chk_fail
.L2493:
	movw	r0, #:lower16:.LC8
	movw	r2, #719
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2499+8
	bl	sftl_printk
	b	.L2373
.L2492:
	movw	r0, #:lower16:.LC8
	movw	r2, #718
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2499+8
	bl	sftl_printk
	ldrh	r3, [fp, #184]
	b	.L2372
.L2491:
	movw	r0, #:lower16:.LC8
	movw	r2, #717
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2499+8
	bl	sftl_printk
	ldrh	r3, [fp, #184]
	b	.L2371
.L2500:
	.align	2
.L2499:
	.word	.LANCHOR0+310
	.word	.LANCHOR0+200
	.word	.LANCHOR1+656
	.word	.LANCHOR0+3674
	.word	.LANCHOR0+3216
	.word	.LANCHOR0+3268
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.word	.LANCHOR0+184
	.word	.LANCHOR0+3676
	.word	.LANCHOR0+312
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	ip, #:lower16:__stack_chk_guard
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	ip, #:upper16:__stack_chk_guard
	mov	r8, r1
	cmp	r0, #16
	.pad #76
	sub	sp, sp, #76
	ldr	r1, [ip]
	str	ip, [sp, #28]
	str	r2, [sp, #24]
	str	r3, [sp, #8]
	str	r1, [sp, #68]
	beq	.L2545
	movw	r5, #:lower16:.LANCHOR0
	ldr	r7, [sp, #24]
	movt	r5, #:upper16:.LANCHOR0
	ldr	r3, [r5, #344]
	cmp	r7, r3
	cmpls	r8, r3
	movcs	r4, #1
	movcc	r4, #0
	bcs	.L2526
	add	r10, r8, r7
	cmp	r3, r10
	bcc	.L2526
	movw	r3, #:lower16:.LANCHOR2
	movt	r3, #:upper16:.LANCHOR2
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2527
	movw	r3, #266
	mov	r0, r8
	ldrh	r6, [r5, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	mov	r9, r0
	str	r0, [sp, #16]
	sub	r0, r10, #1
	bl	__aeabi_uidiv
	ldr	r2, [r5, #2612]
	rsb	r1, r9, #1
	ldr	r3, [r5, #2640]
	mov	ip, r9
	add	r9, r1, r0
	str	r0, [sp, #12]
	cmp	r9, #0
	add	r2, r2, r9
	add	r3, r3, r7
	str	r2, [r5, #2612]
	str	r3, [r5, #2640]
	beq	.L2528
	ldr	fp, .L2555
	mov	r3, r4
	str	r4, [sp, #40]
	mov	r6, r3
	str	r9, [sp, #4]
	mov	r4, ip
	mov	r9, r8
	str	r3, [sp, #36]
	str	r3, [sp, #32]
	str	r3, [sp, #20]
.L2505:
	mov	r2, #0
	add	r1, sp, #64
	mov	r0, r4
	bl	log2phys
	ldr	r2, [sp, #64]
	cmn	r2, #1
	beq	.L2546
	ldr	r1, [r5, #3336]
	add	r7, r6, r6, lsl #2
	ldr	r3, [sp, #16]
	add	r7, r1, r7, lsl #2
	cmp	r3, r4
	str	r2, [r7, #4]
	beq	.L2547
	ldr	r3, [sp, #12]
	cmp	r3, r4
	beq	.L2548
	ldrh	r2, [fp]
	mul	r2, r4, r2
.L2542:
	ldr	r3, [sp, #8]
	sub	r2, r2, r9
	add	r2, r3, r2, lsl #9
	str	r2, [r7, #8]
.L2511:
	ldr	r3, .L2555+4
	ldr	r1, [r5, #3388]
	str	r4, [r7, #16]
	ldrh	r2, [r3]
	mul	r2, r6, r2
	add	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r7, #12]
.L2507:
	ldr	r3, [sp, #4]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L2513
	ldrh	r2, [r5, #36]
	cmp	r6, r2, lsl #2
	bne	.L2505
	cmp	r6, #0
	beq	.L2505
.L2523:
	ldr	r3, [sp, #40]
	mov	r2, #0
	ldr	r0, [r5, #3336]
	mov	r1, r6
	mov	r8, #0
	lsl	r3, r3, #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #32]
	lsl	r3, r3, #9
	str	r3, [sp, #52]
	ldr	r3, [sp, #36]
	lsl	r3, r3, #9
	str	r3, [sp, #56]
	bl	FlashReadPages
	str	r10, [sp, #44]
	str	r4, [sp, #48]
	ldr	r10, [sp, #16]
	ldr	r4, [sp, #12]
	b	.L2521
.L2516:
	cmp	r4, r1
	beq	.L2549
.L2517:
	ldr	r0, [r2, #12]
	ldr	r0, [r0, #8]
	cmp	r0, r1
	ldrne	r1, [r5, #2768]
	addne	r1, r1, #1
	strne	r1, [r5, #2768]
	ldr	r1, [r2]
	cmn	r1, #1
	beq	.L2550
	cmp	r1, #256
	beq	.L2551
.L2520:
	add	r8, r8, #1
	cmp	r6, r8
	bls	.L2552
.L2521:
	add	r7, r8, r8, lsl #2
	ldr	r2, [r5, #3336]
	lsl	r7, r7, #2
	add	r2, r2, r7
	ldr	r1, [r2, #16]
	cmp	r10, r1
	bne	.L2516
	ldr	r1, [r2, #8]
	ldr	r0, [r5, #3364]
	cmp	r1, r0
	movne	r1, r10
	bne	.L2517
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #56]
	ldr	r0, [sp, #8]
	add	r1, r1, r3
	bl	memcpy
	ldr	r2, [r5, #3336]
	add	r2, r2, r7
	ldr	r1, [r2, #16]
	b	.L2517
.L2546:
	ldrh	r0, [fp]
	cmp	r0, #0
	beq	.L2507
	mov	r7, #0
	b	.L2509
.L2508:
	ldrh	r0, [fp]
	add	r7, r7, #1
	cmp	r0, r7
	bls	.L2507
.L2509:
	mla	r0, r0, r4, r7
	cmp	r9, r0
	movls	r3, #1
	movhi	r3, #0
	cmp	r10, r0
	movls	r3, #0
	cmp	r3, #0
	beq	.L2508
	ldr	r3, [sp, #8]
	sub	r0, r0, r9
	mov	r2, #512
	mov	r1, #0
	add	r0, r3, r0, lsl #9
	bl	memset
	b	.L2508
.L2550:
	ldr	r1, [r5, #2768]
	mvn	r3, #0
	str	r3, [sp, #20]
	add	r1, r1, #1
	str	r1, [r5, #2768]
	ldr	r1, [r2]
	cmp	r1, #256
	bne	.L2520
.L2551:
	ldr	r0, [r2, #4]
	add	r8, r8, #1
	ldr	r3, .L2555+8
	ubfx	r0, r0, #10, #16
	ldrh	r1, [r3]
	bl	__aeabi_uidivmod
	ldr	r2, .L2555+12
	uxth	r0, r1
	ldrh	r1, [r2]
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	FtlGcRefreshBlock
	cmp	r6, r8
	bhi	.L2521
.L2552:
	ldr	r3, [sp, #4]
	ldr	r10, [sp, #44]
	ldr	r4, [sp, #48]
	cmp	r3, #0
	bne	.L2553
.L2504:
	movw	r3, #3282
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2522
	ldrh	r3, [r5, #236]
	cmp	r3, #31
	bhi	.L2501
.L2522:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
.L2501:
	ldr	r3, [sp, #28]
	ldr	r2, [sp, #68]
	ldr	r0, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2554
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2549:
	ldr	r1, [r2, #8]
	ldr	r0, [r5, #3368]
	cmp	r1, r0
	movne	r1, r4
	bne	.L2517
	ldrh	r0, [fp]
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #60]
	mul	r0, r4, r0
	sub	r0, r0, r9
	add	r0, r3, r0, lsl #9
	bl	memcpy
	ldr	r2, [r5, #3336]
	add	r2, r2, r7
	ldr	r1, [r2, #16]
	b	.L2517
.L2545:
	mov	r2, r3
	ldr	r1, [sp, #24]
	add	r0, r8, #256
	bl	FtlVendorPartRead
	str	r0, [sp, #20]
	b	.L2501
.L2528:
	str	r9, [sp, #20]
	b	.L2504
.L2526:
	mvn	r3, #0
	str	r3, [sp, #20]
	b	.L2501
.L2527:
	str	r3, [sp, #20]
	b	.L2501
.L2513:
	cmp	r6, #0
	bne	.L2523
	b	.L2504
.L2547:
	ldr	r2, [r5, #3364]
	mov	r0, r9
	ldrh	r8, [fp]
	str	r2, [r7, #8]
	mov	r1, r8
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #24]
	sub	r2, r8, r1
	str	r1, [sp, #32]
	cmp	r3, r2
	movcs	r3, r2
	cmp	r3, r8
	str	r3, [sp, #36]
	ldreq	r3, [sp, #8]
	streq	r3, [r7, #8]
	b	.L2511
.L2554:
	bl	__stack_chk_fail
.L2548:
	ldrh	r1, [fp]
	ldr	r0, [r5, #3368]
	mul	r2, r3, r1
	str	r0, [r7, #8]
	sub	r3, r10, r2
	cmp	r1, r3
	str	r3, [sp, #40]
	bne	.L2511
	b	.L2542
.L2553:
	mov	r6, #0
	b	.L2505
.L2556:
	.align	2
.L2555:
	.word	.LANCHOR0+266
	.word	.LANCHOR0+320
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.syntax unified
	.arm
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
	mov	r0, #0
	b	FtlRead
	.fnend
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	ip, #:lower16:__stack_chk_guard
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	ip, #:upper16:__stack_chk_guard
	.pad #100
	sub	sp, sp, #100
	cmp	r0, #16
	str	r1, [sp, #12]
	ldr	r1, [ip]
	str	ip, [sp, #52]
	str	r2, [sp, #48]
	str	r3, [sp, #16]
	str	r1, [sp, #92]
	beq	.L2626
	movw	r9, #:lower16:.LANCHOR0
	ldr	r2, [sp, #12]
	movt	r9, #:upper16:.LANCHOR0
	ldr	r6, [sp, #48]
	ldr	r3, [r9, #344]
	cmp	r6, r3
	cmpls	r2, r3
	bcs	.L2601
	mov	r1, r2
	add	r2, r2, r6
	cmp	r3, r2
	mov	r7, r2
	str	r2, [sp, #60]
	bcc	.L2601
	movw	r3, #:lower16:.LANCHOR2
	movt	r3, #:upper16:.LANCHOR2
	ldr	r0, [r3]
	mov	r5, r3
	str	r3, [sp, #44]
	cmn	r0, #1
	beq	.L2558
	movw	r2, #266
	mov	r3, #2048
	ldrh	r4, [r9, r2]
	mov	r0, r1
	str	r3, [r9, #3680]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	mov	r8, r0
	str	r0, [sp, #4]
	sub	r0, r7, #1
	bl	__aeabi_uidiv
	sub	r3, r0, r8
	ldr	r1, [r5, #8]
	str	r3, [sp, #56]
	add	r3, r3, #1
	ldr	r2, [r9, #2620]
	str	r0, [sp, #20]
	mov	r0, r3
	str	r3, [sp, #28]
	cmp	r1, #0
	ldr	r3, [r9, #2636]
	add	r2, r2, r0
	str	r2, [r9, #2620]
	addeq	fp, r9, #40
	add	r3, r3, r6
	str	r3, [r9, #2636]
	bne	.L2627
.L2561:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L2562
	ldr	r10, [sp, #4]
.L2563:
	ldrb	r2, [fp, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #36]
	cmp	r2, r3
	bcs	.L2628
.L2566:
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	bne	.L2567
	ldr	r2, .L2642
	cmp	fp, r2
	beq	.L2629
	ldr	r1, [sp, #44]
	str	r3, [r1, #8]
	ldrh	r3, [r9, #44]
	cmp	r3, #0
	movne	fp, r2
	beq	.L2630
.L2567:
	ldr	r1, [sp, #28]
	ldrb	r2, [fp, #7]	@ zero_extendqisi2
	ldrb	r0, [fp, #6]	@ zero_extendqisi2
	cmp	r3, r1
	movcs	r3, r1
	ldrh	r1, [r9, #36]
	lsl	r2, r2, #2
	cmp	r2, r3
	movcc	r3, r2
	cmp	r0, r1
	str	r3, [sp, #24]
	bcs	.L2631
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L2572
.L2639:
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	beq	.L2572
	movw	r3, #:lower16:.LC129
	mov	r8, #0
	movt	r3, #:upper16:.LC129
	str	r3, [sp, #40]
	ldr	r3, [sp, #24]
	add	r3, r3, r10
	str	r3, [sp, #8]
.L2575:
	mov	r2, #0
	add	r1, sp, #68
	mov	r0, r10
	add	r6, r8, r8, lsl #2
	bl	log2phys
	mov	r0, fp
	lsl	r6, r6, #2
	bl	get_new_active_ppa
	ldr	r3, .L2642+4
	mov	r1, #0
	ldr	r7, [r9, #3388]
	ldrh	r4, [r3]
	ldr	r3, [r9, #3344]
	mov	r2, r4
	mul	r4, r8, r4
	add	r3, r3, r6
	str	r0, [r3, #4]
	str	r10, [r3, #16]
	bic	r4, r4, #3
	add	r5, r7, r4
	str	r5, [r3, #12]
	mov	r0, r5
	bl	memset
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	cmp	r2, r10
	cmpne	r3, r10
	bne	.L2576
	cmp	r3, r10
	ldr	r3, .L2642+8
	ldrh	r3, [r3]
	beq	.L2632
	smulbb	r2, r10, r3
	ldr	r1, [sp, #60]
	sub	r2, r1, r2
	uxth	r2, r2
	cmp	r3, r2
	str	r2, [sp, #32]
	beq	.L2633
	ldr	r3, [r9, #3344]
	mov	r2, #0
	str	r2, [sp, #36]
	ldr	r2, [r9, #3368]
	add	r3, r3, r6
	str	r2, [r3, #8]
.L2598:
	ldr	r0, [sp, #68]
	cmn	r0, #1
	beq	.L2583
	str	r10, [sp, #88]
	mov	r2, #0
	ldr	ip, [r3, #8]
	mov	r1, #1
	ldr	r3, [r3, #12]
	str	r0, [sp, #76]
	add	r0, sp, #72
	str	ip, [sp, #80]
	str	r3, [sp, #84]
	bl	FlashReadPages
	ldr	r3, [sp, #72]
	cmn	r3, #1
	beq	.L2634
	ldr	r3, [r5, #8]
	cmp	r3, r10
	beq	.L2588
	ldr	r3, [r9, #2768]
	mov	r2, r10
	ldr	r0, [sp, #40]
	add	r3, r3, #1
	str	r3, [r9, #2768]
	ldr	r1, [r5, #8]
	bl	sftl_printk
	ldr	r3, [r5, #8]
	cmp	r3, r10
	beq	.L2588
	movw	r0, #:lower16:.LC8
	movw	r2, #1128
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2642+12
	bl	sftl_printk
.L2588:
	ldr	r3, [sp, #4]
	cmp	r3, r10
	beq	.L2635
	ldr	r3, .L2642+8
	ldrh	r1, [r3]
	ldr	r3, [sp, #32]
	mul	r1, r10, r1
	lsl	r2, r3, #9
	ldr	r3, [r9, #3344]
	add	r6, r3, r6
	ldr	r3, [sp, #12]
	ldr	r0, [r6, #8]
	sub	r1, r1, r3
	ldr	r3, [sp, #16]
	add	r1, r3, r1, lsl #9
	bl	memcpy
.L2582:
	ldrb	r2, [fp, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #36]
	cmp	r2, r3
	bcs	.L2636
.L2590:
	movw	r3, #61589
	add	r8, r8, #1
	movt	r3, 65535
	strh	r3, [r7, r4]	@ movhi
	ldr	r1, [sp, #68]
	ldr	r2, [r9, #2648]
	str	r10, [r5, #8]
	add	r10, r10, #1
	str	r1, [r5, #12]
	ldr	r1, [sp, #8]
	add	r3, r2, #1
	cmn	r3, #1
	str	r2, [r5, #4]
	ldrh	r2, [fp]
	moveq	r3, #0
	cmp	r1, r10
	str	r3, [r9, #2648]
	strh	r2, [r5, #2]	@ movhi
	beq	.L2574
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	bne	.L2575
	str	r8, [sp, #24]
.L2574:
	ldr	r4, [sp, #24]
	mov	r2, #0
	mov	r3, fp
	ldr	r0, [r9, #3344]
	mov	r1, r4
	bl	FtlProgPages
	ldr	r2, [sp, #28]
	cmp	r4, r2
	bhi	.L2637
.L2594:
	ldr	r3, [sp, #28]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #28]
	bne	.L2563
.L2562:
	ldr	r1, [sp, #56]
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r9, #236]
	cmp	r3, #5
	bhi	.L2565
	ldr	r6, .L2642+16
	mov	r4, #256
	movw	r5, #65535
	sub	r7, r6, #2
	b	.L2597
.L2596:
	mov	r1, #1
	mov	r3, #128
	mov	r0, r1
	strh	r3, [r6]	@ movhi
	strh	r3, [r7]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r9, #236]
	cmp	r3, #2
	bhi	.L2565
	subs	r4, r4, #1
	beq	.L2565
.L2597:
	ldrh	r3, [r9, #184]
	cmp	r3, r5
	bne	.L2596
	ldrh	r3, [r9, #234]
	cmp	r3, r5
	bne	.L2596
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
	b	.L2596
.L2565:
	mov	r0, #0
.L2558:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2638
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2576:
	ldr	r3, .L2642+8
	ldr	r2, [r9, #3344]
	ldrh	r3, [r3]
	mul	r3, r10, r3
.L2622:
	add	r6, r2, r6
	ldr	r2, [sp, #12]
	sub	r3, r3, r2
	ldr	r2, [sp, #16]
	add	r3, r2, r3, lsl #9
	str	r3, [r6, #8]
	ldrb	r2, [fp, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #36]
	cmp	r2, r3
	bcc	.L2590
.L2636:
	movw	r0, #:lower16:.LC8
	movw	r2, #1143
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2642+12
	bl	sftl_printk
	b	.L2590
.L2633:
	mul	r3, r10, r3
	ldr	r2, [r9, #3344]
	b	.L2622
.L2583:
	ldr	r0, [r3, #8]
	mov	r1, #0
	ldr	r3, .L2642+20
	ldrh	r2, [r3]
	bl	memset
	b	.L2588
.L2634:
	ldr	r1, [r9, #2768]
	movw	r0, #:lower16:.LC128
	movt	r0, #:upper16:.LC128
	mov	r2, r10
	add	r1, r1, #1
	str	r1, [r9, #2768]
	ldr	r1, [r5, #8]
	bl	sftl_printk
	b	.L2588
.L2637:
	movw	r0, #:lower16:.LC8
	mov	r2, #1152
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2642+12
	bl	sftl_printk
	b	.L2594
.L2631:
	movw	r0, #:lower16:.LC8
	movw	r2, #1074
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2642+12
	bl	sftl_printk
	ldr	r3, [sp, #24]
	cmp	r3, #0
	bne	.L2639
.L2572:
	mov	r2, #0
	mov	r3, fp
	mov	r1, r2
	ldr	r0, [r9, #3344]
	bl	FtlProgPages
	ldrb	r2, [fp, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #36]
	cmp	r2, r3
	bcc	.L2566
.L2628:
	movw	r0, #:lower16:.LC8
	movw	r2, #1041
	movt	r0, #:upper16:.LC8
	ldr	r1, .L2642+12
	bl	sftl_printk
	b	.L2566
.L2630:
	mov	r0, fp
	bl	allocate_new_data_superblock
.L2570:
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	bne	.L2567
	mov	r0, fp
	bl	allocate_new_data_superblock
	ldrh	r3, [fp, #4]
	b	.L2567
.L2629:
	ldrh	r4, [r9, #92]
	cmp	r4, #0
	beq	.L2640
.L2569:
	ldr	r0, .L2642
	bl	allocate_new_data_superblock
	ldr	r2, [sp, #44]
	ldr	r3, .L2642+24
	ldr	r1, [r2, #8]
	sub	r2, r3, #48
	cmp	r1, #0
	movne	fp, r3
	moveq	fp, r2
	b	.L2570
.L2627:
	ldrh	r3, [r9, #44]
	cmp	r3, #0
	addne	fp, r9, #40
	addeq	fp, r9, #88
	b	.L2561
.L2640:
	add	r0, fp, #48
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #44]
	str	r4, [r3, #8]
	b	.L2569
.L2626:
	mov	r2, r3
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #48]
	add	r0, r3, #256
	bl	FtlVendorPartWrite
	b	.L2558
.L2601:
	mvn	r0, #0
	b	.L2558
.L2632:
	mov	r1, r3
	ldr	r0, [sp, #12]
	str	r3, [sp, #32]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #32]
	str	r1, [sp, #36]
	sub	r2, r3, r1
	ldr	r1, [sp, #48]
	cmp	r2, r1
	movcs	r2, r1
	cmp	r2, r3
	str	r2, [sp, #32]
	beq	.L2641
	ldr	r3, [r9, #3344]
	ldr	r2, [r9, #3364]
	add	r3, r3, r6
	str	r2, [r3, #8]
	b	.L2598
.L2635:
	ldr	r3, [r9, #3344]
	ldr	r2, [sp, #32]
	ldr	r1, [sp, #16]
	add	r6, r3, r6
	ldr	r3, [sp, #36]
	ldr	r0, [r6, #8]
	lsl	r2, r2, #9
	add	r0, r0, r3, lsl #9
	bl	memcpy
	b	.L2582
.L2638:
	bl	__stack_chk_fail
.L2641:
	ldr	r3, [r9, #3344]
	add	r6, r3, r6
	ldr	r3, [sp, #16]
	str	r3, [r6, #8]
	b	.L2582
.L2643:
	.align	2
.L2642:
	.word	.LANCHOR0+40
	.word	.LANCHOR0+320
	.word	.LANCHOR0+266
	.word	.LANCHOR1+680
	.word	.LANCHOR0+3210
	.word	.LANCHOR0+318
	.word	.LANCHOR0+88
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #1
	mov	r0, r1
	b	rk_ftl_garbage_collect
	.fnend
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	sftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r1, #:lower16:.LC0
	movw	r5, #:lower16:.LANCHOR2
	movw	r0, #:lower16:.LC77
	mvn	r3, #0
	movt	r1, #:upper16:.LC0
	movt	r0, #:upper16:.LC77
	movt	r5, #:upper16:.LANCHOR2
	str	r3, [r5]
	movw	r4, #:lower16:.LANCHOR0
	bl	sftl_printk
	ldr	r0, .L2653
	movt	r4, #:upper16:.LANCHOR0
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	lr, [r4, #244]
	mov	ip, #0
	add	r3, r4, #416
	mov	r1, ip
	mov	r2, #2048
	add	r0, r4, #424
	strh	lr, [r3]	@ movhi
	strh	ip, [r3, #2]	@ movhi
	strh	ip, [r3, #4]	@ movhi
	strh	ip, [r3, #6]	@ movhi
	bl	memset
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2652
.L2649:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2652:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L2649
	ldrh	r3, [r4, #236]
	mov	r2, #1
	str	r2, [r5]
	cmp	r3, #15
	bhi	.L2649
	movw	r4, #8129
.L2648:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L2648
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2654:
	.align	2
.L2653:
	.word	.LANCHOR0+3416
	.fnend
	.size	sftl_init, .-sftl_init
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk.part.14, %function
FlashTestBlk.part.14:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #:lower16:.LANCHOR0
	movw	r5, #:lower16:__stack_chk_guard
	movt	r3, #:upper16:.LANCHOR0
	movt	r5, #:upper16:__stack_chk_guard
	.pad #88
	sub	sp, sp, #88
	ldr	r6, [r3, #3376]
	add	ip, sp, #20
	ldr	lr, [r5]
	mov	r4, r0
	mov	r2, #32
	mov	r0, ip
	mov	r1, #165
	str	ip, [sp, #12]
	str	lr, [sp, #84]
	lsl	r4, r4, #10
	str	r6, [sp, #8]
	bl	memset
	movw	r3, #23130
	mov	r2, #1
	movt	r3, 23130
	mov	r1, r2
	str	r3, [r6]	@ unaligned
	mov	r0, sp
	str	r3, [r6, #4]	@ unaligned
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
	mov	r3, #1
	mov	r0, sp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp]
	cmp	r3, #0
	mvnne	r6, #0
	beq	.L2660
.L2656:
	mov	r2, #1
	mov	r0, sp
	mov	r1, #0
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
	ldr	r2, [sp, #84]
	mov	r0, r6
	ldr	r3, [r5]
	cmp	r2, r3
	bne	.L2661
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L2660:
	mov	r3, #1
	mov	r0, sp
	add	ip, r4, r3
	mov	r2, r3
	mov	r1, r3
	str	ip, [sp, #4]
	bl	FlashProgPages
	ldr	r6, [sp]
	adds	r6, r6, #0
	movne	r6, #1
	rsb	r6, r6, #0
	b	.L2656
.L2661:
	bl	__stack_chk_fail
	.fnend
	.size	FlashTestBlk.part.14, .-FlashTestBlk.part.14
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #11
	bhi	.L2664
	mov	r0, #0
	bx	lr
.L2664:
	b	FlashTestBlk.part.14
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #256
	mov	r5, r1
	mov	r1, #255
	mov	r4, r0
	bl	memset
	movw	r3, #:lower16:.LANCHOR0
	mov	r1, r5
	movt	r3, #:upper16:.LANCHOR0
	mov	r0, r4
	ldr	r3, [r3, #3440]
	blx	r3
	uxth	r1, r0
	cmp	r1, #50
	bhi	.L2666
	ldr	r3, .L2676
	mov	r0, r1
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	popne	{r4, r5, r6, pc}
	cmp	r1, #0
	popeq	{r4, r5, r6, pc}
	sub	r1, r1, #1
	sub	r3, r4, #2
	uxth	r1, r1
	add	r1, r4, r1, lsl #1
.L2669:
	ldrh	r2, [r3, #2]
	lsr	r2, r2, #1
	strh	r2, [r3, #2]!	@ movhi
	cmp	r3, r1
	bne	.L2669
	pop	{r4, r5, r6, pc}
.L2666:
	mov	r0, r4
	mov	r2, #256
	mov	r1, #255
	bl	memset
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2677:
	.align	2
.L2676:
	.word	.LANCHOR0+3416
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mvn	r3, #0
	add	r7, r4, #364
	add	r2, r4, #352
	str	r3, [r4, #364]
	mov	r10, #0
	str	r3, [r7, #4]
	.pad #20
	sub	sp, sp, #20
	str	r3, [r7, #8]
	str	r3, [r7, #12]
	strh	r3, [r2]	@ movhi
	strh	r10, [r2, #6]	@ movhi
	bl	FtlLoadFactoryBbt
	movw	r3, #262
	ldrh	r3, [r4, r3]
	cmp	r3, r10
	beq	.L2688
	movw	r3, #61664
	ldr	fp, .L2708
	movt	r3, 65535
	str	r3, [sp, #8]
	add	r3, r4, #380
	str	r3, [sp, #4]
	b	.L2687
.L2707:
	ldrh	r5, [fp]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L2708+4
	mla	r5, r10, r5, r3
	lsl	r3, r5, #10
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldrh	r2, [fp]
	ldr	r3, [sp, #4]
	ldr	r1, [r4, #3472]
	add	r2, r2, #7
	ldr	r0, [r3]
	asr	r2, r2, #3
	bl	memcpy
.L2683:
	uxth	r0, r5
	add	r10, r10, #1
	add	r7, r7, #2
	bl	FtlBbmMapBadBlock
	ldr	r3, .L2708+8
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	add	r2, r2, #4
	str	r2, [sp, #4]
	cmp	r3, r10
	bls	.L2688
.L2687:
	ldrh	r3, [r7]
	movw	r2, #65535
	ldr	r0, [r4, #3352]
	ldr	r8, [r4, #3384]
	cmp	r3, r2
	str	r0, [r4, #3472]
	str	r8, [r4, #3476]
	bne	.L2707
	mov	r1, r10
	bl	FlashGetBadBlockList
	ldr	r3, [sp, #4]
	ldr	r0, [r4, #3472]
	ldr	r1, [r3]
	uxth	r3, r10
	mov	r9, r3
	str	r3, [sp, #12]
	bl	FtlBbt2Bitmap
	ldrh	r5, [fp]
	sub	r6, r5, #1
	smulbb	r3, r9, r5
	uxth	r6, r6
	str	r3, [sp]
.L2684:
	ldr	r3, [sp]
	mov	r1, r5
	add	r9, r6, r3
	uxth	r9, r9
	mov	r0, r9
	bl	__aeabi_uidivmod
	mov	r0, r9
	uxth	r9, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	ip, r9, #5
	add	r0, r4, r0, lsl #2
	and	r1, r9, #31
	mov	r2, #4096
	ldr	r0, [r0, #380]
	ldr	r9, [r0, ip, lsl #2]
	lsr	r9, r9, r1
	ands	r9, r9, #1
	mov	r1, r9
	bne	.L2685
	ldr	r3, [r4, #3384]
	strh	r6, [r7]	@ movhi
	str	r9, [r3]	@ unaligned
	str	r9, [r3, #4]	@ unaligned
	str	r9, [r3, #8]	@ unaligned
	str	r9, [r3, #12]	@ unaligned
	ldr	r0, [r4, #3352]
	bl	memset
	ldrh	r3, [sp, #8]
	str	r9, [r8, #4]
	ldr	r2, .L2708+12
	strh	r3, [r8]	@ movhi
	ldrh	r3, [r7]
	ldrh	r5, [fp]
	ldrh	r2, [r2]
	strh	r3, [r8, #2]	@ movhi
	ldrh	r3, [r7]
	ldr	r1, [sp, #4]
	lsl	r2, r2, #2
	ldr	r0, [r4, #3472]
	mla	r5, r10, r5, r3
	ldr	r1, [r1]
	lsl	r3, r5, #10
	str	r3, [r4, #3468]
	bl	memcpy
	mov	r2, #1
	ldr	r0, .L2708+4
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L2708+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3464]
	uxth	r0, r5
	cmn	r3, #1
	bne	.L2683
	bl	FtlBbmMapBadBlock
	ldrh	r5, [fp]
	ldrh	r3, [sp, #12]
	smulbb	r3, r3, r5
	str	r3, [sp]
	b	.L2684
.L2685:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2684
.L2688:
	movw	r3, #322
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	ldrne	r6, .L2708+16
	movne	r5, #0
	beq	.L2681
.L2691:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	ldrh	r3, [r6]
	uxth	r5, r5
	cmp	r3, r5
	bhi	.L2691
.L2681:
	ldr	r3, .L2708+20
	ldrh	r3, [r3, #12]
	sub	r7, r3, #1
	sub	r3, r3, #48
	uxth	r7, r7
	cmp	r3, r7
	ldrlt	fp, .L2708
	movwlt	r6, #65535
	blt	.L2696
	b	.L2706
.L2694:
	ldr	r3, .L2708+20
	ldrh	r3, [r3]
	cmp	r3, r6
	bne	.L2695
	ldr	r3, .L2708+20
	strh	r7, [r3]	@ movhi
.L2693:
	ldr	r3, .L2708+20
	sub	r7, r7, #1
	uxth	r7, r7
	ldrh	r3, [r3, #12]
	sub	r3, r3, #48
	cmp	r7, r3
	ble	.L2706
.L2696:
	ldrh	r8, [fp]
	mov	r0, r7
	mov	r1, r8
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r5, r1
	mov	r1, r8
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r5, #5
	add	r0, r4, r0, lsl #2
	and	r5, r5, #31
	ldr	r3, [r0, #380]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r5, r3, r5
	tst	r5, #1
	bne	.L2693
	cmp	r7, #11
	bls	.L2694
	mov	r0, r7
	bl	FlashTestBlk.part.14
	cmp	r0, #0
	beq	.L2694
	mov	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L2693
.L2706:
	ldr	r3, .L2708+20
	ldrh	r3, [r3]
.L2692:
	ldr	ip, [r4, #3348]
	lsl	r3, r3, #10
	mov	r5, #0
	ldr	lr, .L2708+20
	str	r5, [r4, #360]
	mov	r2, #2
	str	r3, [ip, #4]
	mov	r0, ip
	ldr	r3, .L2708+20
	mov	r1, #1
	strh	r5, [lr, #2]	@ movhi
	ldrh	r3, [r3, #4]
	lsl	r3, r3, #10
	str	r3, [ip, #24]
	bl	FlashEraseBlocks
	ldr	r3, .L2708+20
	ldrh	r0, [r3]
	bl	FtlBbmMapBadBlock
	ldr	r3, .L2708+20
	ldrh	r0, [r3, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r2, .L2708+20
	ldr	r3, [r4, #360]
	ldr	r0, .L2708+20
	ldrh	r1, [r2]
	ldrh	r2, [r2, #4]
	add	r3, r3, #1
	strh	r5, [r0, #2]	@ movhi
	str	r3, [r4, #360]
	strh	r1, [r0, #4]	@ movhi
	strh	r2, [r0]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2695:
	ldr	r2, .L2708+20
	strh	r7, [r2, #4]	@ movhi
	b	.L2692
.L2709:
	.align	2
.L2708:
	.word	.LANCHOR0+306
	.word	.LANCHOR0+3464
	.word	.LANCHOR0+262
	.word	.LANCHOR0+412
	.word	.LANCHOR0+322
	.word	.LANCHOR0+352
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	ftl_low_format
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #:lower16:.LANCHOR0
	movt	r4, #:upper16:.LANCHOR0
	mov	r3, #0
	add	r7, r4, #416
	mov	r1, r3
	ldrh	ip, [r4, #244]
	mov	r2, #2048
	add	r0, r4, #424
	strh	r3, [r7, #2]	@ movhi
	strh	r3, [r7, #4]	@ movhi
	strh	ip, [r7]	@ movhi
	strh	r3, [r7, #6]	@ movhi
	str	r3, [r4, #2644]
	str	r3, [r4, #2648]
	str	r3, [r4, #2660]
	bl	memset
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2744
.L2711:
	movw	r3, #266
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2716
	ldr	lr, .L2747
	mov	r2, #0
	movw	ip, #23752
	mov	r3, r2
	movt	ip, 2575
.L2715:
	ldr	r0, [r4, #3364]
	mvn	r2, r2
	orr	r2, r3, r2, lsl #16
	add	r1, r3, #1
	str	r2, [r0, r3, lsl #2]
	ldr	r2, [r4, #3368]
	str	ip, [r2, r3, lsl #2]
	uxth	r3, r1
	ldrh	r1, [lr]
	mov	r2, r3
	cmp	r3, r1, lsl #7
	blt	.L2715
.L2716:
	ldrh	r6, [r4, #20]
	ldrh	r3, [r4, #248]
	cmp	r3, r6
	movhi	r8, #0
	bls	.L2742
.L2717:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	ldrh	r5, [r4, #248]
	uxth	r6, r6
	add	r8, r8, r0
	uxth	r8, r8
	cmp	r5, r6
	bhi	.L2717
	ldrh	r6, [r4, #36]
	sub	r3, r8, #3
	cmp	r3, r6, lsl #1
	blt	.L2742
	mov	r1, r6
	mov	r0, r8
	bl	__aeabi_uidiv
	ldr	r3, [r4, #336]
	mov	r2, #0
	ldr	ip, [r4, #256]
	mov	r1, r2
	strh	r2, [r7, #2]	@ movhi
	add	r3, r0, r3
	strh	r2, [r7, #4]	@ movhi
	uxth	r3, r3
	strh	r2, [r7, #6]	@ movhi
	add	r0, r4, #424
	cmp	r3, #24
	mov	r2, #2048
	movcc	r3, #24
	mul	r6, r3, r6
	sub	r5, r5, r3
	uxth	r5, r5
	str	r3, [r4, #240]
	strh	r5, [r4, #20]	@ movhi
	sub	r3, ip, r6
	str	r6, [r4, #244]
	str	r3, [r4, #252]
	strh	r6, [r7]	@ movhi
	bl	memset
	ldrh	r3, [r4, #248]
	cmp	r3, r5
	bls	.L2719
.L2720:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	ldrh	r3, [r4, #248]
	uxth	r5, r5
	cmp	r3, r5
	bhi	.L2720
.L2742:
	ldrh	r5, [r4, #20]
.L2719:
	cmp	r5, #0
	beq	.L2721
	mov	r5, #0
	mov	r6, r5
.L2722:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	ldrh	r3, [r4, #20]
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
	cmp	r3, r6
	bhi	.L2722
.L2721:
	ldrh	r3, [r4, #248]
	movw	r8, #2676
	ldrh	r6, [r4, #36]
	ldr	r10, [r4, #252]
	str	r3, [r4, #3332]
	mov	r1, r6
	mov	r0, r10
	bl	__aeabi_uidiv
	add	r2, r6, r6, lsl #1
	ubfx	r9, r0, #5, #16
	add	r3, r9, #36
	str	r0, [r4, #2608]
	cmp	r5, r2, lsl #3
	uxth	r3, r3
	mov	r7, r0
	strh	r3, [r4, r8]	@ movhi
	ble	.L2723
	sub	r0, r10, r5
	mov	r1, r6
	bl	__aeabi_uidiv
	lsr	r3, r0, #5
	str	r0, [r4, #2608]
	add	r3, r3, #24
	uxth	r3, r3
	strh	r3, [r4, r8]	@ movhi
.L2723:
	movw	r2, #302
	ldrh	r2, [r4, r2]
	cmp	r2, #0
	beq	.L2743
	mul	r1, r6, r2
	add	r3, r3, r2, lsr #1
	movw	r0, #2676
	uxth	r3, r3
	cmp	r1, r5
	strh	r3, [r4, r0]	@ movhi
	bgt	.L2726
.L2743:
	ldr	r7, [r4, #2608]
.L2725:
	sub	r3, r7, r3
	movw	r2, #310
	mul	r3, r6, r3
	ldrh	r2, [r4, r2]
	movw	r1, #266
	mvn	r7, #0
	ldrh	r1, [r4, r1]
	mov	r6, r7
	ldr	r5, .L2747+4
	mul	r2, r3, r2
	str	r3, [r4, #3484]
	mul	r3, r2, r1
	str	r2, [r4, #2608]
	str	r3, [r4, #344]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #248]
	mov	r1, #0
	ldr	r0, [r4, #16]
	lsl	r2, r2, #1
	bl	memset
	ldrh	r2, [r4, #20]
	mov	r3, #0
	mov	ip, #1
	mov	r1, #255
	ldr	r0, [r4, #32]
	strh	r7, [r4, #184]	@ movhi
	lsr	r2, r2, #3
	str	r3, [r4, #28]
	strh	r3, [r4, #186]	@ movhi
	strb	r3, [r4, #190]
	strb	r3, [r4, #192]
	strh	r3, [r4, #42]	@ movhi
	strb	r3, [r4, #46]
	strh	r3, [r4, #40]	@ movhi
	strb	ip, [r4, #48]
	bl	memset
	b	.L2727
.L2745:
	ldrh	r3, [r4, #40]
	ldr	r2, [r4, #16]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, #40]
	add	r3, r3, #1
	strh	r3, [r4, #40]	@ movhi
.L2727:
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r4, #47]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2745
	ldrh	r2, [r4, #40]
	mov	r0, #1
	ldr	r3, [r4, #2644]
	mov	r1, #0
	ldrh	lr, [r4, #44]
	mvn	r6, #0
	ldr	ip, [r4, #16]
	lsl	r2, r2, r0
	str	r3, [r4, #52]
	add	r3, r3, r0
	ldr	r5, .L2747+8
	str	r3, [r4, #2644]
	strh	lr, [ip, r2]	@ movhi
	ldrh	r3, [r4, #40]
	strh	r1, [r4, #90]	@ movhi
	strb	r1, [r4, #94]
	add	r3, r3, r0
	strb	r0, [r4, #96]
	strh	r3, [r4, #88]	@ movhi
	b	.L2729
.L2746:
	ldr	r2, [r4, #16]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, #88]
	add	r3, r3, #1
	strh	r3, [r4, #88]	@ movhi
.L2729:
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r4, #95]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #88]
	beq	.L2746
	ldr	r2, [r4, #2644]
	lsl	r3, r3, #1
	ldr	r1, [r4, #16]
	mvn	r5, #0
	ldrh	r0, [r4, #92]
	add	ip, r2, #1
	str	r2, [r4, #100]
	str	ip, [r4, #2644]
	strh	r0, [r1, r3]	@ movhi
	strh	r5, [r4, #136]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r2, [r4, #2644]
	movw	r1, #2680
	ldr	r3, .L2747+12
	mov	lr, #0
	ldr	ip, [r4, #3484]
	strh	r0, [r4, r1]	@ movhi
	add	r1, r2, #1
	strh	r5, [r3, #4]	@ movhi
	strh	lr, [r3, #2]	@ movhi
	strh	ip, [r3, #6]	@ movhi
	str	r2, [r4, #2688]
	str	r1, [r4, #2644]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	movweq	r3, #:lower16:.LANCHOR2
	moveq	r2, #1
	movteq	r3, #:upper16:.LANCHOR2
	streq	r2, [r3]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2726:
	add	r2, r2, #32
	add	r2, r9, r2
	uxth	r3, r2
	strh	r3, [r4, r0]	@ movhi
	b	.L2725
.L2744:
	bl	FtlMakeBbt
	b	.L2711
.L2748:
	.align	2
.L2747:
	.word	.LANCHOR0+266
	.word	.LANCHOR0+40
	.word	.LANCHOR0+88
	.word	.LANCHOR0+2680
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
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
	.align	2
	.global	js_hash
	.syntax unified
	.arm
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r1, #0
	beq	.L2755
	mov	r2, r0
	movw	r0, #42982
	add	r1, r2, r1
	movt	r0, 18374
.L2754:
	lsr	r3, r0, #2
	ldrb	ip, [r2], #1	@ zero_extendqisi2
	add	r3, r3, r0, lsl #5
	cmp	r1, r2
	add	r3, r3, ip
	eor	r0, r0, r3
	bne	.L2754
	bx	lr
.L2755:
	movw	r0, #42982
	movt	r0, 18374
	bx	lr
	.fnend
	.size	js_hash, .-js_hash
	.align	2
	.global	FtlWriteToIDB
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	.fnstart
	@ args = 0, pretend = 0, frame = 128
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	add	r8, r1, r0
	sub	r7, r8, #1
	movw	r3, #:lower16:__stack_chk_guard
	cmp	r7, #63
	.pad #140
	sub	sp, sp, #140
	movt	r3, #:upper16:__stack_chk_guard
	movhi	r4, #0
	movls	r4, #1
	str	r3, [sp, #16]
	cmp	r0, #576
	orrcs	r4, r4, #1
	ldr	r3, [r3]
	cmp	r4, #0
	str	r3, [sp, #132]
	bne	.L2839
	cmp	r0, #64
	mov	r5, r0
	mov	r6, r1
	mov	r9, r2
	beq	.L2840
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldr	r2, [r3, #3684]
	cmp	r2, #0
	beq	.L2836
	cmp	r0, #63
	ldr	fp, [r3, #3688]
	bhi	.L2797
	rsb	r1, r0, #64
	sub	r6, r6, r1
	add	r1, r9, r1, lsl #9
.L2800:
	cmp	r7, #576
	subcs	r6, r6, r8
	mov	r0, fp
	subcs	r6, r6, #444
	subcs	r6, r6, #2
	lsl	r2, r6, #9
	bl	memcpy
	b	.L2836
.L2839:
	movw	fp, #:lower16:.LANCHOR0
	movt	fp, #:upper16:.LANCHOR0
	ldr	r3, [fp, #3684]
	cmp	r3, #0
	bne	.L2841
.L2836:
	mov	r4, #0
.L2757:
	ldr	r3, [sp, #16]
	mov	r0, r4
	ldr	r2, [sp, #132]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2842
	add	sp, sp, #140
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2841:
	ldr	r3, [fp, #3688]
	mov	r2, r3
	str	r3, [sp, #20]
	ldr	r2, [r2]
	movw	r3, #35899
	movt	r3, 64732
	cmp	r2, r3
	mvnne	r4, #0
	beq	.L2843
.L2761:
	mov	r3, #0
	ldr	r0, [sp, #20]
	str	r3, [fp, #3684]
	bl	kfree
	ldr	r0, [fp, #3692]
	bl	kfree
	b	.L2757
.L2840:
	movw	r10, #:lower16:.LANCHOR0
	mov	r0, #262144
	movt	r10, #:upper16:.LANCHOR0
	bl	ftl_malloc
	str	r0, [r10, #3688]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	fp, [r10, #3688]
	str	r0, [r10, #3692]
	cmp	fp, #0
	cmpne	r0, #0
	beq	.L2796
	mov	r0, #1
	mov	r1, r4
	str	r0, [r10, #3684]
	mov	r2, #262144
	mov	r0, fp
	bl	memset
.L2797:
	movw	r3, #65472
	mov	r1, r9
	movt	r3, 127
	add	r3, r5, r3
	add	fp, fp, r3, lsl #9
	b	.L2800
.L2843:
	ldr	r4, [sp, #20]
	mov	r3, #0
	ldr	r0, .L2851
	movw	r2, #65023
	add	r1, r4, #260096
	add	ip, r4, #262144
	ldrh	lr, [r0, #10]
	b	.L2766
.L2845:
	ldr	r0, [r4, r3, lsl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r0, [ip, #-4]!
	beq	.L2844
.L2766:
	ldr	r0, [r1, #-4]!
	cmp	r0, #0
	beq	.L2845
	add	r2, r2, #127
	lsr	r3, r2, #7
	add	r0, r3, #4
	str	r3, [sp, #60]
.L2765:
	ldr	r4, [sp, #60]
	lsl	lr, lr, #2
	movw	r1, #65532
	and	r1, r1, lr
	lsl	r3, r4, #7
	str	r3, [sp, #48]
	bl	__aeabi_uidiv
	mov	r3, #0
	add	r10, r0, #1
	mov	r1, r4
	str	r3, [sp, #52]
	mov	r4, r3
	movw	r0, #:lower16:.LC130
	movw	r3, #18766
	mov	r2, r10
	mov	r5, r3
	movt	r0, #:upper16:.LC130
	movw	r3, #:lower16:.LC131
	mov	r6, r3
	bl	sftl_printk
	mov	r3, r5
	str	r4, [sp, #12]
	movt	r3, 17998
	str	r3, [sp, #32]
	mov	r3, r6
	movt	r3, #:upper16:.LC131
	str	r3, [sp, #56]
.L2767:
	ldr	r3, [sp, #12]
	add	r3, r10, r3
	cmp	r3, #8
	str	r3, [sp, #36]
	bhi	.L2846
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [fp, #3692]
	bl	memset
	ldr	r3, [fp, #3456]
	ldr	r2, .L2851
	cmp	r3, #0
	ldr	r3, [sp, #12]
	ldrh	r5, [r2, #10]
	mul	r3, r5, r3
	mov	r8, r3
	beq	.L2807
	ldr	r3, [fp, #3460]
	cmp	r3, #0
	moveq	r3, #6
	movne	r3, #9
	str	r3, [sp, #28]
.L2768:
	mov	r6, r8
	mov	r4, #0
.L2769:
	mov	r1, r6
	add	r4, r4, #1
	ldr	r3, [fp, #3444]
	mov	r0, #0
	blx	r3
	cmp	r10, r4
	add	r6, r6, r5
	bne	.L2769
	ldr	r3, [sp, #28]
	cmp	r3, #9
	movne	r3, #0
	strne	r3, [sp, #24]
	beq	.L2847
.L2770:
	mul	r5, r4, r5
	cmp	r5, #0
	beq	.L2778
	str	r10, [sp, #40]
	mov	r6, #0
	ldr	r9, [sp, #20]
	add	r7, sp, #68
	ldr	r10, [sp, #28]
	str	r4, [sp, #44]
	mov	r4, r8
.L2780:
	cmp	r10, #9
	beq	.L2848
	lsl	r3, r6, #2
	movw	r2, #61424
	str	r2, [sp, #72]
	str	r3, [sp, #68]
.L2777:
	ldr	r8, [fp, #3448]
	mov	r3, r7
	mov	r2, r9
	add	r1, r4, r6
	mov	r0, #0
	blx	r8
	cmn	r0, #1
	beq	.L2831
	add	r9, r9, #2048
	add	r8, r6, #1
.L2779:
	cmp	r8, r5
	mov	r6, r8
	bcc	.L2780
.L2831:
	ldr	r10, [sp, #40]
	ldr	r4, [sp, #44]
.L2778:
	ldr	r2, [fp, #3456]
	ldr	r3, .L2851
	ldr	r6, [fp, #3692]
	cmp	r2, #0
	ldr	r2, .L2851
	ldrh	r3, [r3, #10]
	moveq	r8, #6
	ldrb	r2, [r2, #14]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldr	r2, [sp, #12]
	mul	r9, r3, r2
	beq	.L2774
	ldr	r2, [fp, #3460]
	cmp	r2, #0
	moveq	r8, #6
	movne	r8, #9
.L2774:
	mul	r4, r4, r3
	cmp	r4, #0
	beq	.L2781
	sub	r8, r8, #9
	mov	r5, #0
	clz	r8, r8
	add	r7, sp, #68
	lsr	r8, r8, #5
.L2785:
	cmp	r5, #0
	movne	r0, #0
	andeq	r0, r8, #1
	cmp	r0, #0
	beq	.L2782
	ldr	r3, [fp, #3456]
	mov	r0, #70
	blx	r3
	ldr	r3, [fp, #3460]
	mov	r0, #2
	blx	r3
	mov	r2, r6
	mov	r1, r9
	ldr	ip, [fp, #3452]
	mov	r3, r7
	mov	r0, #0
	blx	ip
	ldr	r3, [fp, #3460]
	ldr	r0, [sp, #24]
	blx	r3
	ldr	r3, [fp, #3456]
	ldrb	r0, [fp, #3438]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r6]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2781
.L2784:
	add	r5, r5, #1
	cmp	r5, r4
	bne	.L2785
.L2781:
	ldr	r7, [fp, #3692]
	ldr	r3, [sp, #20]
	ldr	r2, [r7]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2849
	ldr	r3, [sp, #20]
	add	r2, r7, #4
	mov	r4, #0
	ldr	ip, [sp, #48]
	add	r3, r3, #4
	b	.L2790
.L2792:
	ldr	r0, [r2]
	mov	r5, r2
	ldr	r1, [r3]
	mov	r6, r3
	add	r2, r2, #4
	add	r3, r3, #4
	cmp	r0, r1
	bne	.L2850
.L2790:
	add	r4, r4, #1
	cmp	r4, ip
	bne	.L2792
.L2791:
	ldr	r3, [sp, #52]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #52]
	bhi	.L2835
.L2787:
	ldr	r3, [sp, #36]
	str	r3, [sp, #12]
	b	.L2767
.L2782:
	mov	r3, r7
	mov	r2, r6
	add	r1, r9, r5
	ldr	ip, [fp, #3452]
	blx	ip
	cmn	r0, #1
	beq	.L2781
	ldr	r3, [sp, #72]
	movw	r2, #61424
	cmp	r3, r2
	bne	.L2781
	add	r6, r6, #2048
	b	.L2784
.L2848:
	cmp	r6, #0
	add	r8, r6, #1
	movw	r3, #61424
	str	r8, [sp, #68]
	str	r3, [sp, #72]
	bne	.L2777
	ldr	r3, [fp, #3456]
	mov	r0, #70
	blx	r3
	mov	r0, r6
	mov	r3, r7
	ldr	r6, [fp, #3448]
	mov	r1, r4
	ldr	r2, [sp, #24]
	blx	r6
	ldr	r3, [fp, #3456]
	mov	r6, r0
	ldrb	r0, [fp, #3438]	@ zero_extendqisi2
	blx	r3
	cmn	r6, #1
	bne	.L2779
	b	.L2831
.L2850:
	mov	r2, #512
	mov	r1, #0
	mov	r0, r7
	bl	memset
	ldr	r3, [r6]
	ldr	r6, [sp, #12]
	ldr	r2, [r5]
	ldr	r0, [sp, #56]
	mov	r1, r6
	str	r4, [sp]
	bl	sftl_printk
	ldr	r3, .L2851
	mov	r0, #0
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3444]
	mul	r1, r6, r1
	blx	r3
	ldr	r3, [sp, #48]
	cmp	r4, r3
	bcc	.L2787
	b	.L2791
.L2807:
	mov	r3, #6
	str	r3, [sp, #28]
	b	.L2768
.L2847:
	ldr	r3, [fp, #3692]
	mov	r2, #1024
	mov	r1, #0
	mov	r0, r3
	mov	r6, r3
	str	r3, [sp, #24]
	bl	memset
	mov	r3, #12
	ldr	r1, [sp, #32]
	str	r3, [r6, #4]
	add	r0, r6, #11
	ldr	r3, .L2851
	add	ip, r6, #23
	str	r1, [r6]
	movw	r1, #42982
	movt	r1, 18374
	mov	r2, #4
	ldrh	r3, [r3, #10]
	strb	r2, [r6, #17]
	strh	r3, [r6, #18]	@ movhi
	mov	r3, #16
	strb	r3, [r6, #21]
	mov	r3, #0
	strb	r3, [r6, #16]
	strb	r3, [r6, #20]
	strh	r3, [r6, #22]	@ movhi
.L2771:
	lsr	r3, r1, #2
	ldrb	r2, [r0, #1]!	@ zero_extendqisi2
	add	r3, r3, r1, lsl #5
	cmp	ip, r0
	add	r3, r3, r2
	eor	r1, r1, r3
	bne	.L2771
	ldr	r3, [sp, #24]
	str	r1, [r3, #8]
	b	.L2770
.L2846:
	ldr	r3, [sp, #52]
	cmp	r3, #0
	bne	.L2835
	ldr	r3, [fp, #3688]
	mvn	r4, #0
	str	r3, [sp, #20]
	b	.L2761
.L2849:
	mov	r1, #0
	mov	r2, #512
	mov	r0, r7
	mov	r4, r1
	bl	memset
	ldr	r5, [sp, #12]
	ldr	r3, [sp, #20]
	ldr	r2, [r7]
	mov	r1, r5
	ldr	r0, [sp, #56]
	ldr	r3, [r3]
	str	r4, [sp]
	bl	sftl_printk
	ldr	r3, .L2851
	mov	r0, r4
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3444]
	mul	r1, r5, r1
	blx	r3
	b	.L2787
.L2796:
	movw	r0, #:lower16:.LC132
	ldr	r1, .L2851+4
	movt	r0, #:upper16:.LC132
	bl	sftl_printk
	ldr	r2, [r10, #3684]
	cmp	r2, #0
	beq	.L2836
	ldr	fp, [r10, #3688]
	b	.L2797
.L2835:
	ldr	r3, [fp, #3688]
	ldr	r4, [sp, #60]
	str	r3, [sp, #20]
	b	.L2761
.L2844:
	mov	r3, #512
	mov	r0, #516
	str	r3, [sp, #60]
	b	.L2765
.L2842:
	bl	__stack_chk_fail
.L2852:
	.align	2
.L2851:
	.word	.LANCHOR0+3416
	.word	.LANCHOR1+692
	.fnend
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
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
	mov	r0, #0
	pop	{r4, r5, r6, lr}
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
	.align	2
	.global	rk_sftl_vendor_dev_ops_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movw	r3, #:lower16:.LANCHOR0
	movt	r3, #:upper16:.LANCHOR0
	ldr	r2, [r3, #3696]
	cmp	r2, #0
	streq	r0, [r3, #3696]
	moveq	r0, r2
	streq	r1, [r3, #3700]
	mvnne	r0, #0
	bx	lr
	.fnend
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	bl	ftl_malloc
	movw	r6, #:lower16:.LANCHOR0
	cmp	r0, #0
	movt	r6, #:upper16:.LANCHOR0
	str	r0, [r6, #3704]
	beq	.L2864
	mov	r7, #0
	movw	r9, #22084
	mov	r4, r0
	mov	r8, r7
	mov	r5, r7
	movt	r9, 21067
.L2862:
	mov	r2, r4
	ldr	r3, [r6, #3696]
	mov	r1, #128
	lsl	r0, r5, #7
	blx	r3
	subs	r10, r0, #0
	bne	.L2860
	ldr	r4, [r6, #3704]
	ldr	r3, [r4]
	cmp	r3, r9
	beq	.L2868
.L2861:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2862
	cmp	r8, #0
	beq	.L2863
	mov	r2, r4
	lsl	r0, r7, #7
	ldr	r3, [r6, #3696]
	mov	r1, #128
	blx	r3
	subs	r10, r0, #0
	bne	.L2860
.L2858:
	mov	r0, r10
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2868:
	add	r2, r4, #61440
	ldr	r3, [r4, #4]
	ldr	r2, [r2, #4092]
	cmp	r3, r8
	sub	r2, r2, r3
	clz	r2, r2
	lsr	r2, r2, #5
	movls	r2, #0
	cmp	r2, #0
	movne	r7, r5
	movne	r8, r3
	b	.L2861
.L2863:
	mov	r1, r8
	mov	r2, #65536
	mov	r0, r4
	bl	memset
	movw	r1, #22084
	movw	r3, #64504
	mov	r2, #1
	add	r0, r4, #61440
	movt	r1, 21067
	movt	r3, 65535
	stm	r4, {r1, r2}
	str	r2, [r0, #4092]
	mov	r0, r10
	strh	r8, [r4, #12]	@ movhi
	strh	r3, [r4, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2860:
	ldr	r0, [r6, #3704]
	mvn	r10, #0
	bl	kfree
	mov	r3, #0
	mov	r0, r10
	str	r3, [r6, #3704]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2864:
	mvn	r10, #11
	b	.L2858
	.fnend
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movt	r3, #:upper16:.LANCHOR0
	ldr	r5, [r3, #3704]
	cmp	r5, #0
	beq	.L2876
	ldrh	r4, [r5, #10]
	cmp	r4, #0
	beq	.L2876
	ldrh	r3, [r5, #16]
	cmp	r0, r3
	movne	ip, r5
	movne	r3, #0
	bne	.L2873
	b	.L2882
.L2874:
	ldrh	lr, [ip, #24]
	add	ip, ip, #8
	cmp	lr, r0
	beq	.L2871
.L2873:
	add	r3, r3, #1
	cmp	r3, r4
	bne	.L2874
.L2876:
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L2882:
	mov	r3, #0
.L2871:
	add	r3, r5, r3, lsl #3
	mov	r0, r1
	ldrh	r4, [r3, #20]
	ldrh	r1, [r3, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	add	r1, r5, r1
	mov	r2, r4
	bl	memcpy
	mov	r0, r4
	pop	{r4, r5, r6, pc}
	.fnend
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #:lower16:.LANCHOR0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movt	r3, #:upper16:.LANCHOR0
	.pad #28
	sub	sp, sp, #28
	ldr	r9, [r3, #3704]
	str	r3, [sp, #12]
	cmp	r9, #0
	beq	.L2902
	ldrh	r8, [r9, #10]
	add	r3, r2, #63
	bic	r3, r3, #63
	str	r2, [sp]
	str	r3, [sp, #4]
	mov	r10, r0
	ldrh	r3, [r9, #8]
	cmp	r8, #0
	str	r1, [sp, #16]
	str	r3, [sp, #8]
	beq	.L2885
	ldrh	r3, [r9, #16]
	cmp	r0, r3
	movne	r3, r9
	movne	r7, #0
	bne	.L2888
	b	.L2915
.L2895:
	ldrh	r2, [r3, #24]
	add	r3, r3, #8
	cmp	r2, r10
	beq	.L2886
.L2888:
	add	r7, r7, #1
	cmp	r8, r7
	bne	.L2895
.L2885:
	ldrh	r2, [r9, #14]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bhi	.L2902
	add	r8, r9, r8, lsl #3
	uxth	r3, r3
	ldr	r1, [sp, #16]
	strh	r10, [r8, #16]	@ movhi
	sub	r0, r2, r3
	ldrh	ip, [r9, #12]
	ldr	r2, [sp]
	add	r3, ip, r3
	strh	ip, [r8, #18]	@ movhi
	strh	r2, [r8, #20]	@ movhi
	strh	r3, [r9, #12]	@ movhi
	strh	r0, [r9, #14]	@ movhi
	ldrh	r0, [r8, #18]
	add	r0, r0, #1024
	add	r0, r9, r0
	bl	memcpy
	ldr	r3, [r9, #4]
	add	r2, r9, #61440
	ldrh	r1, [r9, #10]
	add	r3, r3, #1
	add	r1, r1, #1
	strh	r1, [r9, #10]	@ movhi
.L2913:
	str	r3, [r9, #4]
	str	r3, [r2, #4092]
	mov	r2, r9
	ldrh	r3, [r9, #8]
	ldr	r1, [sp, #8]
	add	r3, r3, #1
	uxth	r3, r3
	lsl	r0, r1, #7
	mov	r1, #128
	cmp	r3, #1
	movhi	r3, #0
	strh	r3, [r9, #8]	@ movhi
	ldr	r3, [sp, #12]
	ldr	r3, [r3, #3700]
	blx	r3
	mov	r0, #0
.L2883:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2915:
	mov	r7, #0
.L2886:
	add	r4, r9, r7, lsl #3
	add	fp, r9, #1024
	ldrh	r2, [r4, #20]
	add	r2, r2, #63
	bic	r3, r2, #63
	ldr	r2, [sp]
	str	r3, [sp, #20]
	cmp	r2, r3
	bls	.L2889
	ldrh	r2, [r9, #14]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bhi	.L2902
	sub	r8, r8, #1
	ldrh	r6, [r4, #18]
	cmp	r7, r8
	bcs	.L2901
.L2891:
	ldrh	r3, [r4, #28]
	add	r0, fp, r6
	ldrh	ip, [r4, #24]
	add	r7, r7, #1
	ldrh	r1, [r4, #26]
	add	r4, r4, #8
	add	r5, r3, #63
	strh	r6, [r4, #10]	@ movhi
	bic	r5, r5, #63
	strh	r3, [r4, #12]	@ movhi
	strh	ip, [r4, #8]	@ movhi
	mov	r2, r5
	add	r1, fp, r1
	add	r6, r6, r5
	bl	memcpy
	cmp	r7, r8
	bcc	.L2891
.L2890:
	ldr	r2, [sp]
	add	r8, r9, r8, lsl #3
	uxth	r4, r6
	ldr	r1, [sp, #16]
	strh	r10, [r8, #16]	@ movhi
	strh	r2, [r8, #20]	@ movhi
	add	r0, fp, r4
	strh	r4, [r8, #18]	@ movhi
	bl	memcpy
	ldrh	r3, [r9, #14]
	ldrh	r2, [sp, #4]
	add	r4, r4, r2
	sub	r3, r3, r2
	ldr	r2, [sp, #20]
	strh	r4, [r9, #12]	@ movhi
	add	r3, r3, r2
	strh	r3, [r9, #14]	@ movhi
.L2892:
	ldr	r3, [r9, #4]
	add	r2, r9, #61440
	add	r3, r3, #1
	b	.L2913
.L2902:
	mvn	r0, #0
	b	.L2883
.L2889:
	ldr	r5, [sp]
	ldrh	r0, [r4, #18]
	ldr	r1, [sp, #16]
	mov	r2, r5
	add	r0, fp, r0
	bl	memcpy
	strh	r5, [r4, #20]	@ movhi
	b	.L2892
.L2901:
	mov	r8, r7
	b	.L2890
	.fnend
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.align	2
	.global	rk_sftl_vendor_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r0, #4096
	mov	r6, r1
	mov	r4, r2
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2932
	movw	r3, #30209
	movt	r3, 16388
	cmp	r6, r3
	beq	.L2919
	movw	r3, #30210
	movt	r3, 16388
	cmp	r6, r3
	bne	.L2937
	mov	r6, #57344
	mov	r3, sp
	movt	r6, 65535
	and	r3, r3, r6
	ldr	r3, [r3, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2936
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	rsbne	r3, r0, #8
	bne	.L2925
	ldr	r2, [r5]
	movw	r3, #17745
	movt	r3, 22098
	cmp	r2, r3
	beq	.L2941
.L2927:
	mvn	r6, #0
	b	.L2918
.L2919:
	mov	r6, #57344
	mov	r3, sp
	movt	r6, 65535
	and	r3, r3, r6
	ldr	r3, [r3, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2934
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	subs	r2, r0, #0
	rsbne	r0, r2, #8
	bne	.L2921
	ldr	r2, [r5]
	movw	r3, #17745
	movt	r3, 22098
	cmp	r2, r3
	bne	.L2927
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L2927
	mov	r3, sp
	strh	r0, [r5, #6]	@ movhi
	uxth	r0, r0
	and	r6, r6, r3
	ldr	r3, [r6, #8]
	add	r2, r0, #8
	.syntax divided
@ 132 "./include/linux/uaccess.h" 1
	adds r1, r4, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2937
	mov	r0, r4
	mov	r1, r5
	bl	arm_copy_to_user
	subs	r6, r0, #0
	beq	.L2918
.L2937:
	mvn	r6, #13
.L2918:
	mov	r0, r5
	bl	kfree
.L2916:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, pc}
.L2941:
	ldrh	r3, [r5, #6]
	movw	r2, #4087
	cmp	r3, r2
	bhi	.L2927
	mov	r2, sp
	add	r7, r3, #8
	and	r6, r6, r2
	ldr	r3, [r6, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r4, r7; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2928
	mov	r2, r7
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_from_user
	subs	r3, r0, #0
	bne	.L2942
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	mov	r6, r0
	b	.L2918
.L2936:
	mov	r3, #0
	mov	r0, #8
.L2925:
	mov	r2, r0
	mov	r1, #0
	add	r0, r5, r3
	mvn	r6, #13
	bl	memset
	movw	r0, #:lower16:.LC133
	mov	r2, r4
	mov	r1, #276
	movt	r0, #:upper16:.LC133
	bl	sftl_printk
	b	.L2918
.L2934:
	mov	r0, #0
	mov	r2, #8
.L2921:
	add	r0, r5, r0
	mov	r1, #0
	bl	memset
	movw	r0, #:lower16:.LC133
	mov	r2, r4
	mov	r1, #256
	movt	r0, #:upper16:.LC133
	mvn	r6, #13
	bl	sftl_printk
	b	.L2918
.L2942:
	sub	r0, r7, r3
	mov	r7, r3
.L2928:
	mov	r2, r7
	add	r0, r5, r0
	mov	r1, #0
	mvn	r6, #13
	bl	memset
	movw	r0, #:lower16:.LC133
	mov	r2, r4
	movw	r1, #283
	movt	r0, #:upper16:.LC133
	bl	sftl_printk
	b	.L2918
.L2932:
	mvn	r6, #0
	b	.L2916
	.fnend
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	2
	.global	rk_sftl_vendor_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L2944
	b	misc_register
.L2945:
	.align	2
.L2944:
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
	.type	__func__.7397, %object
	.size	__func__.7397, 14
__func__.7397:
	.ascii	"FtlBbt2Bitmap\000"
	.space	2
	.type	__func__.7560, %object
	.size	__func__.7560, 17
__func__.7560:
	.ascii	"INSERT_DATA_LIST\000"
	.space	3
	.type	__func__.7555, %object
	.size	__func__.7555, 17
__func__.7555:
	.ascii	"INSERT_FREE_LIST\000"
	.space	3
	.type	__func__.7591, %object
	.size	__func__.7591, 17
__func__.7591:
	.ascii	"List_remove_node\000"
	.space	3
	.type	__func__.7623, %object
	.size	__func__.7623, 22
__func__.7623:
	.ascii	"List_update_data_list\000"
	.space	2
	.type	__func__.7730, %object
	.size	__func__.7730, 22
__func__.7730:
	.ascii	"select_l2p_ram_region\000"
	.space	2
	.type	__func__.8057, %object
	.size	__func__.8057, 16
__func__.8057:
	.ascii	"make_superblock\000"
	.type	__func__.8078, %object
	.size	__func__.8078, 18
__func__.8078:
	.ascii	"SupperBlkListInit\000"
	.space	2
	.type	__func__.8232, %object
	.size	__func__.8232, 19
__func__.8232:
	.ascii	"get_new_active_ppa\000"
	.space	1
	.type	__func__.7170, %object
	.size	__func__.7170, 11
__func__.7170:
	.ascii	"FtlMemInit\000"
	.space	1
	.type	__func__.13900, %object
	.size	__func__.13900, 15
__func__.13900:
	.ascii	"FlashReadPages\000"
	.space	1
	.type	__func__.7440, %object
	.size	__func__.7440, 11
__func__.7440:
	.ascii	"FtlLoadBbt\000"
	.space	1
	.type	__func__.7898, %object
	.size	__func__.7898, 15
__func__.7898:
	.ascii	"FtlLoadSysInfo\000"
	.space	1
	.type	__func__.13919, %object
	.size	__func__.13919, 15
__func__.13919:
	.ascii	"FlashProgPages\000"
	.space	1
	.type	__func__.13943, %object
	.size	__func__.13943, 17
__func__.13943:
	.ascii	"FlashEraseBlocks\000"
	.space	3
	.type	__func__.7842, %object
	.size	__func__.7842, 14
__func__.7842:
	.ascii	"FtlScanSysBlk\000"
	.space	2
	.type	__func__.7665, %object
	.size	__func__.7665, 26
__func__.7665:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.space	2
	.type	__func__.7691, %object
	.size	__func__.7691, 31
__func__.7691:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.space	1
	.type	__func__.7705, %object
	.size	__func__.7705, 16
__func__.7705:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.7632, %object
	.size	__func__.7632, 16
__func__.7632:
	.ascii	"load_l2p_region\000"
	.type	__func__.7676, %object
	.size	__func__.7676, 15
__func__.7676:
	.ascii	"ftl_map_blk_gc\000"
	.space	1
	.type	__func__.7920, %object
	.size	__func__.7920, 18
__func__.7920:
	.ascii	"FtlMapTblRecovery\000"
	.space	2
	.type	__func__.7747, %object
	.size	__func__.7747, 9
__func__.7747:
	.ascii	"log2phys\000"
	.space	3
	.type	__func__.7966, %object
	.size	__func__.7966, 16
__func__.7966:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.8119, %object
	.size	__func__.8119, 14
__func__.8119:
	.ascii	"ftl_check_vpc\000"
	.space	2
	.type	__func__.7820, %object
	.size	__func__.7820, 15
__func__.7820:
	.ascii	"FtlVpcTblFlush\000"
	.space	1
	.type	__func__.8245, %object
	.size	__func__.8245, 16
__func__.8245:
	.ascii	"update_vpc_list\000"
	.type	__func__.8252, %object
	.size	__func__.8252, 20
__func__.8252:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.8000, %object
	.size	__func__.8000, 22
__func__.8000:
	.ascii	"FtlRecoverySuperblock\000"
	.space	2
	.type	__func__.8322, %object
	.size	__func__.8322, 19
__func__.8322:
	.ascii	"FtlGcFreeTempBlock\000"
	.space	1
	.type	__func__.8103, %object
	.size	__func__.8103, 21
__func__.8103:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.8204, %object
	.size	__func__.8204, 25
__func__.8204:
	.ascii	"allocate_data_superblock\000"
	.space	3
	.type	__func__.8225, %object
	.size	__func__.8225, 29
__func__.8225:
	.ascii	"allocate_new_data_superblock\000"
	.space	3
	.type	__func__.7294, %object
	.size	__func__.7294, 13
__func__.7294:
	.ascii	"FtlProgPages\000"
	.space	3
	.type	__func__.8435, %object
	.size	__func__.8435, 23
__func__.8435:
	.ascii	"rk_ftl_garbage_collect\000"
	.space	1
	.type	__func__.7322, %object
	.size	__func__.7322, 9
__func__.7322:
	.ascii	"FtlWrite\000"
	.space	3
	.type	__func__.14062, %object
	.size	__func__.14062, 14
__func__.14062:
	.ascii	"FtlWriteToIDB\000"
	.space	2
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 128
rk_sftl_vendor_storage_fops:
	.space	36
	.word	rk_sftl_vendor_storage_ioctl
	.word	rk_sftl_vendor_storage_ioctl
	.space	84
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
	.type	c_ftl_nand_max_vendor_blks, %object
	.size	c_ftl_nand_max_vendor_blks, 2
c_ftl_nand_max_vendor_blks:
	.space	2
	.space	2
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 4
p_vendor_block_table:
	.space	4
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
	.type	c_ftl_nand_data_blks_per_plane, %object
	.size	c_ftl_nand_data_blks_per_plane, 2
c_ftl_nand_data_blks_per_plane:
	.space	2
	.space	2
	.type	p_data_block_list_tail, %object
	.size	p_data_block_list_tail, 4
p_data_block_list_tail:
	.space	4
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.space	4
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
	.type	c_ftl_nand_planes_num, %object
	.size	c_ftl_nand_planes_num, 2
c_ftl_nand_planes_num:
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
	.type	c_ftl_nand_max_sys_blks, %object
	.size	c_ftl_nand_max_sys_blks, 4
c_ftl_nand_max_sys_blks:
	.space	4
	.type	c_ftl_nand_blk_pre_plane, %object
	.size	c_ftl_nand_blk_pre_plane, 2
c_ftl_nand_blk_pre_plane:
	.space	2
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
	.type	c_ftl_nand_vendor_region_num, %object
	.size	c_ftl_nand_vendor_region_num, 2
c_ftl_nand_vendor_region_num:
	.space	2
	.type	c_ftl_nand_map_blks_per_plane, %object
	.size	c_ftl_nand_map_blks_per_plane, 2
c_ftl_nand_map_blks_per_plane:
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
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.space	2
	.space	2
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
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
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 44
gL2pMapInfo:
	.space	44
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	p_map_block_table, %object
	.size	p_map_block_table, 4
p_map_block_table:
	.space	4
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 4
p_map_block_ver_table:
	.space	4
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 4
p_map_region_ppn_table:
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
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 4
p_swl_mul_table:
	.space	4
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.space	2
	.space	2
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 4
p_gc_blk_tbl:
	.space	4
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
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
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
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 4
gp_ect_tbl_info:
	.space	4
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 4
p_valid_page_count_check_table:
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
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.space	24
	.type	g_nand_ops, %object
	.size	g_nand_ops, 24
g_nand_ops:
	.space	24
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
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.space	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.space	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.space	128
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
	.section	.rodata.str1.4,"aMS",%progbits,1
	.align	2
.LC0:
	.ascii	"SFTL version: 5.0.53 20200303\000"
	.space	2
.LC1:
	.ascii	"\012%s\012\000"
	.space	3
.LC2:
	.ascii	"act blk: %x %x %x %x %x %x\012\000"
.LC3:
	.ascii	"buf blk: %x %x %x %x %x %x\012\000"
.LC4:
	.ascii	"tmp blk: %x %x %x %x %x %x\012\000"
.LC5:
	.ascii	"gc blk: %x %x %x %x %x %x\012\000"
	.space	1
.LC6:
	.ascii	"free blk: %x %x %x\012\000"
.LC7:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
	.space	2
.LC8:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
	.space	2
.LC9:
	.ascii	"FLASH INFO:\012\000"
	.space	3
.LC10:
	.ascii	"Device Capacity: %d MB\012\000"
.LC11:
	.ascii	"FTL INFO:\012\000"
	.space	1
.LC12:
	.ascii	"g_MaxLpn = 0x%x\012\000"
	.space	3
.LC13:
	.ascii	"g_VaildLpn = 0x%x\012\000"
	.space	1
.LC14:
	.ascii	"read_page_count = 0x%x\012\000"
.LC15:
	.ascii	"discard_page_count = 0x%x\012\000"
	.space	1
.LC16:
	.ascii	"write_page_count = 0x%x\012\000"
	.space	3
.LC17:
	.ascii	"cache_write_count = 0x%x\012\000"
	.space	2
.LC18:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC19:
	.ascii	"gc_page_count = 0x%x\012\000"
	.space	2
.LC20:
	.ascii	"totle_write = %d MB\012\000"
	.space	3
.LC21:
	.ascii	"totle_read = %d MB\012\000"
.LC22:
	.ascii	"GSV = 0x%x\012\000"
.LC23:
	.ascii	"GDV = 0x%x\012\000"
.LC24:
	.ascii	"bad blk num = %d\012\000"
	.space	2
.LC25:
	.ascii	"free_superblocks = 0x%x\012\000"
	.space	3
.LC26:
	.ascii	"mlc_EC = 0x%x\012\000"
	.space	1
.LC27:
	.ascii	"slc_EC = 0x%x\012\000"
	.space	1
.LC28:
	.ascii	"avg_EC = 0x%x\012\000"
	.space	1
.LC29:
	.ascii	"sys_EC = 0x%x\012\000"
	.space	1
.LC30:
	.ascii	"max_EC = 0x%x\012\000"
	.space	1
.LC31:
	.ascii	"min_EC = 0x%x\012\000"
	.space	1
.LC32:
	.ascii	"PLT = 0x%x\012\000"
.LC33:
	.ascii	"POT = 0x%x\012\000"
.LC34:
	.ascii	"MaxSector = 0x%x\012\000"
	.space	2
.LC35:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
	.space	3
.LC36:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"free sysblock = 0x%x\012\000"
	.space	2
.LC38:
	.ascii	"data_blks_pp = 0x%x\012\000"
	.space	3
.LC39:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC40:
	.ascii	"max_data_blks = 0x%x\012\000"
	.space	2
.LC41:
	.ascii	"Sys.id = 0x%x\012\000"
	.space	1
.LC42:
	.ascii	"Bbt.id = 0x%x\012\000"
	.space	1
.LC43:
	.ascii	"ACT.page = 0x%x\012\000"
	.space	3
.LC44:
	.ascii	"ACT.plane = 0x%x\012\000"
	.space	2
.LC45:
	.ascii	"ACT.id = 0x%x\012\000"
	.space	1
.LC46:
	.ascii	"ACT.mode = 0x%x\012\000"
	.space	3
.LC47:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC48:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC49:
	.ascii	"BUF.page = 0x%x\012\000"
	.space	3
.LC50:
	.ascii	"BUF.plane = 0x%x\012\000"
	.space	2
.LC51:
	.ascii	"BUF.id = 0x%x\012\000"
	.space	1
.LC52:
	.ascii	"BUF.mode = 0x%x\012\000"
	.space	3
.LC53:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC54:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC55:
	.ascii	"TMP.page = 0x%x\012\000"
	.space	3
.LC56:
	.ascii	"TMP.plane = 0x%x\012\000"
	.space	2
.LC57:
	.ascii	"TMP.id = 0x%x\012\000"
	.space	1
.LC58:
	.ascii	"TMP.mode = 0x%x\012\000"
	.space	3
.LC59:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC60:
	.ascii	"GC.page = 0x%x\012\000"
.LC61:
	.ascii	"GC.plane = 0x%x\012\000"
	.space	3
.LC62:
	.ascii	"GC.id = 0x%x\012\000"
	.space	2
.LC63:
	.ascii	"GC.mode = 0x%x\012\000"
.LC64:
	.ascii	"GC.a_pages = 0x%x\012\000"
	.space	1
.LC65:
	.ascii	"WR_CHK = %x %x %x\012\000"
	.space	1
.LC66:
	.ascii	"Read Err Cnt = 0x%x\012\000"
	.space	3
.LC67:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
	.space	3
.LC68:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
	.space	2
.LC69:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC70:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
	.space	1
.LC71:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC72:
	.ascii	"free min EC= 0x%x\012\000"
	.space	1
.LC73:
	.ascii	"free max EC= 0x%x\012\000"
	.space	1
.LC74:
	.ascii	"GC__SB VPC = 0x%x\012\000"
	.space	1
.LC75:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC76:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC77:
	.ascii	"%s\012\000"
.LC78:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
	.space	3
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
	.space	3
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC81:
	.ascii	"%s error allocating memory. return -1\012\000"
	.space	1
.LC82:
	.ascii	"\012\000"
	.space	2
.LC83:
	.ascii	"%s %p + 0x%x:\000"
	.space	2
.LC84:
	.ascii	"0x%08x,\000"
.LC85:
	.ascii	"0x%04x,\000"
.LC86:
	.ascii	"0x%02x,\000"
.LC87:
	.ascii	"FlashReadPages %x %x error_ecc_bits %d\012\000"
.LC88:
	.ascii	"data:\000"
	.space	2
.LC89:
	.ascii	"spare:\000"
	.space	1
.LC90:
	.ascii	"%s hash error this.id =%x page =%x pre_id =%x hash "
	.ascii	"=%x hash_r =%x\012\000"
	.space	1
.LC91:
	.ascii	"%s: addr: %x is in id block!!!!!!!!!!\012\000"
	.space	1
.LC92:
	.ascii	"not free: w: d:\000"
.LC93:
	.ascii	"not free: w: s:\000"
.LC94:
	.ascii	"prog read error: = %x\012\000"
	.space	1
.LC95:
	.ascii	"prog read REFRESH: = %x\012\000"
	.space	3
.LC96:
	.ascii	"prog read s error: = %x %x %x\012\000"
	.space	1
.LC97:
	.ascii	"prog read d error: = %x %x %x\012\000"
	.space	1
.LC98:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC99:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
	.space	3
.LC100:
	.ascii	"FtlMapWritePage error = %x \012\000"
	.space	3
.LC101:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
	.space	3
.LC102:
	.ascii	"region_id = %x phyAddr = %x\012\000"
	.space	3
.LC103:
	.ascii	"map_ppn:\000"
	.space	3
.LC104:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
	.space	1
.LC105:
	.ascii	"%s last blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC106:
	.ascii	"%s scan blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC107:
	.ascii	"...%s enter...\012\000"
.LC108:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
	.space	2
.LC109:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC110:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC111:
	.ascii	"scan lpa = %x ppa= %x\012\000"
	.space	1
.LC112:
	.ascii	"lpa = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
	.space	3
.LC113:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
	.space	3
.LC114:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
	.space	3
.LC115:
	.ascii	"no ect\000"
	.space	1
.LC116:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC117:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
	.space	1
.LC118:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
	.space	3
.LC119:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC120:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
	.space	1
.LC121:
	.ascii	"decrement_vpc_count %x = %d\012\000"
	.space	3
.LC122:
	.ascii	"data prev_ppa = %x error...................\012\000"
	.space	3
.LC123:
	.ascii	"spuer block %x vpn is 0\012 \000"
	.space	2
.LC124:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
	.space	3
.LC125:
	.ascii	"FtlProgPages error %x = %d\012\000"
.LC126:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
	.space	2
.LC127:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
	.space	2
.LC128:
	.ascii	"FtlWrite: ecc error:%x %x %x\012\000"
	.space	2
.LC129:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
	.space	1
.LC130:
	.ascii	"write_idblock %x %x\012\000"
	.space	3
.LC131:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
	.space	3
.LC132:
	.ascii	"%s idb buffer alloc fail\012\000"
	.space	2
.LC133:
	.ascii	"copy_from_user error %d %p %p\012\000"
	.space	1
.LC134:
	.ascii	"vendor_storage\000"
	.ident	"GCC: (Linaro GCC 6.3-2017.05) 6.3.1 20170404"
	.section	.note.GNU-stack,"",%progbits
