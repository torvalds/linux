/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-26
 * function: rk ftl v6 for rockchip soc base on arm v7 to support 3D/2D
 *	     TLC and MLC.
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
	.file	"rk_zftl_arm_v7.c"
	.syntax unified
	.text
	.align	2
	.fpu softvfp
	.type	flash_mem_cmp8, %function
flash_mem_cmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	cmp	r3, r2
	bne	.L10
	mov	r0, #0
	bx	lr
.L2:
	cmp	r3, r2
	bne	.L5
	mov	r0, #0
	ldr	pc, [sp], #4
.L10:
	str	lr, [sp, #-4]!
	.save {lr}
.L5:
	ldrb	lr, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	lr, ip
	beq	.L2
	mov	r0, r3
	ldr	pc, [sp], #4
	.fnend
	.size	flash_mem_cmp8, .-flash_mem_cmp8
	.global	__aeabi_uidiv
	.global	__aeabi_uidivmod
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	slc_phy_page_address_calc, %function
slc_phy_page_address_calc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r5, .L22
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L12
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L13
.L12:
	ldrh	r6, [r5, #2]
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	mul	r7, r6, r0
	mov	r0, r4
	bl	__aeabi_uidivmod
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	lsl	r1, r1, #1
	cmp	r3, #0
	addeq	r1, r5, r1
	addne	r4, r1, r7
	ldrheq	r4, [r1, #4]
	addeq	r4, r4, r7
.L13:
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, pc}
.L23:
	.align	2
.L22:
	.word	.LANCHOR0
	.fnend
	.size	slc_phy_page_address_calc, .-slc_phy_page_address_calc
	.align	2
	.global	zftl_nandc_get_irq_status
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_nandc_get_irq_status, %function
zftl_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L27
	ldrb	r3, [r3, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	ldreq	r0, [r0, #296]
	ldrne	r0, [r0, #372]
	bx	lr
.L28:
	.align	2
.L27:
	.word	.LANCHOR0
	.fnend
	.size	zftl_nandc_get_irq_status, .-zftl_nandc_get_irq_status
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	isxdigit, %function
isxdigit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bic	r3, r0, #32
	sub	r3, r3, #65
	cmp	r3, #25
	bls	.L31
	sub	r0, r0, #48
	cmp	r0, #9
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L31:
	mov	r0, #1
	bx	lr
	.fnend
	.size	isxdigit, .-isxdigit
	.text
	.align	2
	.global	zftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_get_density, %function
zftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #0
	bne	.L33
	ldr	r3, .L36
	ldr	r0, [r3, #1032]
	bx	lr
.L33:
	cmp	r0, #4
	movcc	r0, #8192
	movcs	r0, #0
	bx	lr
.L37:
	.align	2
.L36:
	.word	.LANCHOR0
	.fnend
	.size	zftl_get_density, .-zftl_get_density
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	_list_remove_node, %function
_list_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #6
	ldr	r6, .L47
	mul	r5, r5, r1
	movw	r3, #65535
	ldr	r8, [r6, #1036]
	ldrh	ip, [r8, r5]
	add	r4, r8, r5
	ldrh	r1, [r4, #2]
	cmp	ip, r3
	ldr	r3, [r0]
	bne	.L39
	cmp	r1, ip
	bne	.L39
	cmp	r4, r3
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
.L39:
	mov	r9, r2
	movw	r2, #65535
	cmp	r1, r2
	mov	r7, r0
	bne	.L41
	cmp	r4, r3
	beq	.L41
	mov	r2, #202
	ldr	r1, .L47+4
	ldr	r0, .L47+8
	bl	printk
	bl	dump_stack
.L41:
	ldr	r3, [r7]
	movw	r2, #65535
	cmp	r4, r3
	ldrh	r3, [r8, r5]
	bne	.L42
	cmp	r3, r2
	ldrne	r2, [r6, #1036]
	movne	r1, #6
	moveq	r3, #0
	streq	r3, [r7]
	mlane	r3, r1, r3, r2
	mvnne	r2, #0
	strne	r3, [r7]
	strhne	r2, [r3, #2]	@ movhi
.L44:
	mvn	r3, #0
	strh	r3, [r8, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	ldrh	r3, [r9]
	sub	r3, r3, #1
	strh	r3, [r9]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L42:
	cmp	r3, r2
	ldrh	r1, [r4, #2]
	bne	.L45
	cmp	r1, r3
	movne	r3, #6
	ldrne	r2, [r6, #1036]
	mulne	r1, r3, r1
	mvnne	r3, #0
	strhne	r3, [r2, r1]	@ movhi
	b	.L44
.L45:
	ldr	r0, [r6, #1036]
	mov	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r4, #2]
	ldrh	r1, [r8, r5]
	ldr	r3, [r6, #1036]
	mul	r2, r2, r0
	strh	r1, [r3, r2]	@ movhi
	b	.L44
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC0
	.fnend
	.size	_list_remove_node, .-_list_remove_node
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ndelay, %function
ndelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L50
	add	r0, r0, #996
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L50+4
	ldr	r3, [r3, #8]
	lsr	r0, r1, #6
	bx	r3	@ indirect register sibling call
.L51:
	.align	2
.L50:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	ndelay, .-ndelay
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	hynix_set_rr_para, %function
hynix_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L58
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	lsl	r9, r0, #8
	ldr	r3, [r2, #1040]
	ldr	r6, [r2, #1044]
	ldrb	ip, [r3, #113]	@ zero_extendqisi2
	add	r4, r3, #112
	ldrb	r2, [r3, #112]	@ zero_extendqisi2
	add	r5, r6, r9
	add	r7, r3, #128
	add	r8, r3, #127
	mul	r1, r1, ip
	cmp	r2, #8
	movne	r2, #160
	add	r1, r1, #32
	smlabbne	r1, r2, r0, r1
	mov	r2, #54
	str	r2, [r5, #2056]
	sub	r2, ip, #1
	add	r4, r4, r1
	add	r7, r7, r2
	sub	r4, r4, #1
.L55:
	cmp	r8, r7
	bne	.L56
	add	r6, r6, r9
	mov	r3, #22
	str	r3, [r6, #2056]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L56:
	ldrb	r3, [r8, #1]!	@ zero_extendqisi2
	mov	r0, #120
	str	r3, [r5, #2052]
	bl	ndelay
	ldrsb	r3, [r4, #1]!
	str	r3, [r5, #2048]
	b	.L55
.L59:
	.align	2
.L58:
	.word	.LANCHOR0
	.fnend
	.size	hynix_set_rr_para, .-hynix_set_rr_para
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_debug_proc_open, %function
zftl_debug_proc_open:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r1
	bl	PDE_DATA
	mov	r2, r0
	mov	r0, r4
	ldr	r1, .L62
	pop	{r4, lr}
	b	single_open
.L63:
	.align	2
.L62:
	.word	zftl_debug_proc_show
	.fnend
	.size	zftl_debug_proc_open, .-zftl_debug_proc_open
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_debug_proc_show, %function
zftl_debug_proc_show:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r2, .L66
	ldr	r1, .L66+4
	bl	seq_printf
	ldr	r3, .L66+8
	mov	r0, r4
	ldr	r1, .L66+12
	ldr	r2, [r3]
	bl	seq_printf
	mov	r0, #0
	pop	{r4, pc}
.L67:
	.align	2
.L66:
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR2
	.word	.LC3
	.fnend
	.size	zftl_debug_proc_show, .-zftl_debug_proc_show
	.align	2
	.global	zftl_flash_suspend
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_flash_suspend, %function
zftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L72
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	ldr	r2, [r3, #1044]
	ldr	r1, [r2]
	str	r1, [r3, #1048]
	ldr	r1, [r2, #4]
	str	r1, [r3, #1052]
	bne	.L69
	ldr	r1, [r2, #16]
	str	r1, [r3, #1056]
	ldr	r1, [r2, #32]
	str	r1, [r3, #1060]
	ldr	r1, [r2, #80]
	str	r1, [r3, #1064]
	ldr	r1, [r2, #84]
	str	r1, [r3, #1068]
	ldr	r1, [r2, #520]
	ldr	r2, [r2, #8]
	str	r1, [r3, #1072]
.L71:
	str	r2, [r3, #1076]
	bx	lr
.L69:
	ldr	r1, [r2, #8]
	str	r1, [r3, #1056]
	ldr	r1, [r2, #12]
	str	r1, [r3, #1060]
	ldr	r1, [r2, #304]
	str	r1, [r3, #1064]
	ldr	r1, [r2, #308]
	str	r1, [r3, #1068]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #1072]
	b	.L71
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L77
	ldrb	r3, [r3, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	mov	r3, #1
	bne	.L75
	ldr	r2, [r0, #292]
	lsl	r1, r3, r1
	orr	r3, r2, r1
	str	r3, [r0, #292]
	ldr	r3, [r0, #288]
	bic	r1, r3, r1
	str	r1, [r0, #288]
	bx	lr
.L75:
	ldr	r2, [r0, #368]
	lsl	r1, r3, r1
	orr	r3, r2, r1
	str	r3, [r0, #368]
	ldr	r3, [r0, #364]
	bic	r1, r3, r1
	str	r1, [r0, #364]
	bx	lr
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
	.fnend
	.size	nandc_irq_disable, .-nandc_irq_disable
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	_insert_free_list, %function
_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L91
	movw	ip, #1080
	ldrh	ip, [r3, ip]
	cmp	ip, r1
	bxls	lr
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r3
	ldrh	ip, [r2]
	add	ip, ip, #1
	strh	ip, [r2]	@ movhi
	mov	ip, #6
	mul	r6, ip, r1
	ldr	r7, [r3, #1036]
	mvn	r3, #0
	add	lr, r7, r6
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	bne	.L81
.L90:
	str	lr, [r0]
	b	.L79
.L81:
	ldr	r5, [r4, #1084]
	add	r2, r4, #1088
	ldrh	r10, [r2]
	lsl	r8, r1, #2
	ldr	r9, [r4, #1036]
	movw	fp, #65535
	ldr	r2, [r5, r1, lsl #2]
	ldrh	r8, [r5, r8]
	str	ip, [sp, #4]
	ubfx	r2, r2, #11, #8
	smulbb	r2, r2, r10
	ubfx	r8, r8, #0, #11
	add	r2, r2, r8
	uxth	r2, r2
	str	r2, [sp]
	sub	r2, r3, r9
	asr	r8, r2, #1
	ldr	r2, .L91+4
	mul	r2, r2, r8
	uxth	r2, r2
.L84:
	ldr	ip, [r5, r2, lsl #2]
	lsl	r8, r2, #2
	ldrh	r8, [r5, r8]
	ubfx	ip, ip, #11, #8
	smulbb	ip, ip, r10
	ubfx	r8, r8, #0, #11
	add	ip, ip, r8
	ldr	r8, [sp]
	uxth	ip, ip
	cmp	r8, ip
	bls	.L82
	ldrh	ip, [r3]
	cmp	ip, fp
	bne	.L83
	strh	r2, [lr, #2]	@ movhi
	strh	r1, [r3]	@ movhi
.L79:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L83:
	ldr	r3, [sp, #4]
	mov	r2, ip
	mla	r3, r3, ip, r9
	b	.L84
.L82:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	strheq	r1, [r3, #2]	@ movhi
	beq	.L90
.L85:
	ldrh	ip, [r3, #2]
	mov	r2, #6
	ldr	r0, [r4, #1036]
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
	b	.L79
.L92:
	.align	2
.L91:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_insert_free_list, .-_insert_free_list
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	_insert_data_list, %function
_insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L116
	movw	ip, #1080
	ldrh	ip, [r3, ip]
	cmp	ip, r1
	bxls	lr
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #6
	ldrh	ip, [r2]
	mul	r5, r5, r1
	mov	r4, r3
	.pad #28
	sub	sp, sp, #28
	add	ip, ip, #1
	strh	ip, [r2]	@ movhi
	ldr	r10, [r3, #1036]
	mvn	r3, #0
	add	lr, r10, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r10, r5]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	bne	.L96
.L115:
	str	lr, [r0]
	b	.L93
.L96:
	ldr	r2, [r4, #1092]
	add	r8, r4, #1088
	ldr	r9, [r4, #1084]
	ldrh	r8, [r8]
	str	r2, [sp, #4]
	lsl	r2, r1, #1
	ldr	ip, [sp, #4]
	ldrh	r6, [lr, #4]
	ldr	r7, [r9, r1, lsl #2]
	ldrh	ip, [ip, r2]
	muls	ip, r6, ip
	str	r8, [sp, #8]
	lsl	r2, r1, #2
	ldrhne	r8, [sp, #8]
	ubfx	r7, r7, #11, #8
	ldrh	r2, [r9, r2]
	smulbbne	r7, r7, r8
	ubfx	r2, r2, #0, #11
	addne	r2, r2, r7
	uxtahne	ip, ip, r2
	ldr	r2, [r4, #1036]
	cmp	r6, #0
	mvneq	ip, #0
	str	r2, [sp, #12]
	sub	r2, r3, r2
	asr	r6, r2, #1
	ldr	r2, .L116+4
	mul	r2, r2, r6
	movw	r6, #1080
	ldrh	r4, [r4, r6]
	mov	r6, #0
	uxth	r2, r2
	str	r4, [sp, #16]
.L103:
	sub	r4, r1, r2
	ldr	r7, [sp, #16]
	add	r6, r6, #1
	clz	r4, r4
	uxth	r6, r6
	lsr	r4, r4, #5
	cmp	r6, r7
	orrhi	r4, r4, #1
	cmp	r4, #0
	bne	.L93
	ldr	r7, [sp, #4]
	lsl	r4, r2, #1
	ldrh	fp, [r3, #4]
	ldr	r8, [r9, r2, lsl #2]
	ldrh	r4, [r7, r4]
	lsl	r7, r2, #2
	muls	r4, fp, r4
	ldrh	r7, [r9, r7]
	ubfx	r8, r8, #11, #8
	ubfx	r7, r7, #0, #11
	str	r7, [sp, #20]
	ldrhne	r7, [sp, #8]
	smulbbne	r8, r8, r7
	ldrne	r7, [sp, #20]
	addne	r7, r7, r8
	uxtahne	r4, r4, r7
	cmp	fp, #0
	cmpne	ip, r4
	bls	.L101
	ldrh	r4, [r3]
	movw	r7, #65535
	cmp	r4, r7
	bne	.L102
	strh	r2, [lr, #2]	@ movhi
	strh	r1, [r3]	@ movhi
.L93:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L102:
	ldr	r2, [sp, #12]
	mov	r3, #6
	mla	r3, r3, r4, r2
	mov	r2, r4
	b	.L103
.L101:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r10, r5]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	strheq	r1, [r3, #2]	@ movhi
	beq	.L115
.L104:
	ldrh	ip, [r3, #2]
	ldr	r2, .L116
	ldr	r0, [r2, #1036]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
	b	.L93
.L117:
	.align	2
.L116:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	_list_update_data_list, %function
_list_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r5, .L129
	ldr	r3, [r5, #1096]
	ldrh	ip, [r3, #16]
	cmp	ip, r1
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	ip, [r3, #48]
	cmp	ip, r1
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r3, [r3, #80]
	cmp	r3, r1
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r8, #6
	ldr	r10, [r5, #1036]
	mul	r8, r8, r1
	ldr	r3, [r0]
	add	r9, r10, r8
	cmp	r9, r3
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r3, [r9, #2]
	mov	r7, r2
	movw	r2, #65535
	mov	r4, r1
	mov	r6, r0
	cmp	r3, r2
	bne	.L122
	ldrh	r2, [r10, r8]
	cmp	r2, r3
	bne	.L122
	movw	r2, #273
	ldr	r1, .L129+4
	ldr	r0, .L129+8
	bl	printk
	bl	dump_stack
.L122:
	ldrh	r3, [r9, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L123
	ldrh	r2, [r10, r8]
	cmp	r2, r3
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
.L123:
	ldr	r0, [r5, #1092]
	lsl	r2, r4, #1
	ldrh	r1, [r9, #4]
	ldrh	r2, [r0, r2]
	cmp	r1, #0
	mvneq	r1, #0
	mulne	r1, r1, r2
	mov	r2, #6
	mul	r2, r2, r3
	ldr	r3, .L129+12
	asr	ip, r2, #1
	mul	r3, r3, ip
	lsl	r3, r3, #1
	ldrh	ip, [r0, r3]
	ldr	r0, [r5, #1036]
	add	r2, r0, r2
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	mulne	r3, r3, ip
	mvneq	r3, #0
	cmp	r1, r3
	popcs	{r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	bl	_list_remove_node
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	_insert_data_list
.L130:
	.align	2
.L129:
	.word	.LANCHOR0
	.word	.LANCHOR1+18
	.word	.LC0
	.word	-1431655765
	.fnend
	.size	_list_update_data_list, .-_list_update_data_list
	.section	.text.unlikely
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_simple_strtoull.constprop.33, %function
rk_simple_strtoull.constprop.33:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #48
	movne	r2, r0
	movne	r5, #10
	bne	.L132
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	add	r2, r0, #1
	orr	r3, r3, #32
	cmp	r3, #120
	bne	.L144
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	bl	isxdigit
	cmp	r0, #0
	addne	r2, r4, #2
	movne	r5, #16
	bne	.L132
.L144:
	mov	r5, #8
.L132:
	mov	r6, #0
.L133:
	mov	r7, r2
	ldrb	r4, [r2], #1	@ zero_extendqisi2
	mov	r0, r4
	bl	isxdigit
	cmp	r0, #0
	bne	.L134
.L140:
	cmp	r1, #0
	mov	r0, r6
	strne	r7, [r1]
	pop	{r4, r5, r6, r7, r8, pc}
.L134:
	sub	r0, r4, #48
	cmp	r0, #9
	orrhi	r0, r4, #32
	subhi	r0, r0, #87
	cmp	r0, r5
	bcs	.L140
	mla	r6, r5, r6, r0
	b	.L133
	.fnend
	.size	rk_simple_strtoull.constprop.33, .-rk_simple_strtoull.constprop.33
	.text
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_de_cs.constprop.35, %function
nandc_de_cs.constprop.35:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L148
	ldr	r2, [r3, #1044]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L149:
	.align	2
.L148:
	.word	.LANCHOR0
	.fnend
	.size	nandc_de_cs.constprop.35, .-nandc_de_cs.constprop.35
	.align	2
	.global	flash_read_status
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_status, %function
flash_read_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #112
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	str	r3, [r0, #8]
	mov	r0, #120
	bl	ndelay
	ldr	r0, [r4]
	uxtb	r0, r0
	pop	{r4, pc}
	.fnend
	.size	flash_read_status, .-flash_read_status
	.align	2
	.global	toshiba_set_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_set_rr_para, %function
toshiba_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r8, r1, r1, lsl #2
	ldr	r9, .L161
	mov	r5, r0
	mov	r6, r1
	mov	r4, #0
	ldr	r7, .L161+4
	add	r10, r9, #41
.L153:
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L157
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L157:
	mov	r3, #85
	mov	r0, #200
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	str	r3, [r5, #4]
	bl	ndelay
	ldrb	r3, [r7, #1100]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L160
	cmp	r3, #35
	addne	r3, r9, r6
	ldrsbne	r3, [r3, #181]
	bne	.L159
	ldr	r2, .L161+8
	add	r3, r4, r8
	add	r3, r2, r3
.L160:
	ldrsb	r3, [r3, #5]
.L159:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L153
.L162:
	.align	2
.L161:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR1+86
	.fnend
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
	.align	2
	.global	hynix_reconfig_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	hynix_reconfig_rr_para, %function
hynix_reconfig_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L169
	ldrb	r2, [r5, #1100]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	pophi	{r4, r5, r6, pc}
	ldr	r2, [r5, #1040]
	mov	r4, r0
	add	r2, r2, r0
	ldrb	r2, [r2, #120]	@ zero_extendqisi2
	cmp	r2, #0
	popeq	{r4, r5, r6, pc}
	mov	r1, #0
	bl	hynix_set_rr_para
	ldr	r3, [r5, #1040]
	mov	r2, #0
	add	r3, r3, r4
	strb	r2, [r3, #120]
	pop	{r4, r5, r6, pc}
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.fnend
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
	.align	2
	.global	nand_flash_print_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L290
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L172
	ldr	r1, .L290+4
	ldr	r0, .L290+8
	bl	printk
.L172:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L173
	ldr	r3, .L290+12
	ldr	r0, [r3, #1104]
	ldrb	ip, [r0, #6]	@ zero_extendqisi2
	ldrb	r3, [r0, #3]	@ zero_extendqisi2
	ldrb	r2, [r0, #2]	@ zero_extendqisi2
	ldrb	r1, [r0, #1]	@ zero_extendqisi2
	str	ip, [sp, #8]
	ldrb	ip, [r0, #5]	@ zero_extendqisi2
	str	ip, [sp, #4]
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L290+16
	bl	printk
.L173:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L174
	ldr	r3, .L290+12
	ldr	r0, .L290+20
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #8]	@ zero_extendqisi2
	bl	printk
.L174:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L175
	ldr	r3, .L290+12
	ldr	r0, .L290+24
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	printk
.L175:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L176
	ldr	r3, .L290+12
	ldr	r0, .L290+28
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #10]
	bl	printk
.L176:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L177
	ldr	r3, .L290+12
	ldr	r0, .L290+32
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	printk
.L177:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L178
	ldr	r3, .L290+12
	ldr	r0, .L290+36
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	bl	printk
.L178:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L179
	ldr	r3, .L290+12
	ldr	r0, .L290+40
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #14]
	bl	printk
.L179:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L180
	ldr	r3, .L290+12
	ldr	r0, .L290+44
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #23]	@ zero_extendqisi2
	bl	printk
.L180:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L181
	ldr	r3, .L290+12
	ldr	r0, .L290+48
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #18]	@ zero_extendqisi2
	bl	printk
.L181:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L182
	ldr	r3, .L290+12
	ldr	r0, .L290+52
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	bl	printk
.L182:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L183
	ldr	r3, .L290+12
	ldr	r0, .L290+56
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	bl	printk
.L183:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L184
	ldr	r3, .L290+12
	ldr	r0, .L290+60
	ldrb	r1, [r3, #1108]	@ zero_extendqisi2
	bl	printk
.L184:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L185
	ldr	r3, .L290+12
	ldr	r0, .L290+64
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #22]	@ zero_extendqisi2
	bl	printk
.L185:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L186
	ldr	r3, .L290+12
	ldr	r0, .L290+68
	ldrb	r1, [r3, #1109]	@ zero_extendqisi2
	bl	printk
.L186:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L187
	ldr	r3, .L290+12
	ldr	r0, .L290+72
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	and	r1, r1, #1
	bl	printk
.L187:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L188
	ldr	r3, .L290+12
	ldr	r0, .L290+76
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #1, #1
	bl	printk
.L188:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L189
	ldr	r3, .L290+12
	ldr	r0, .L290+80
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #2, #1
	bl	printk
.L189:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L190
	ldr	r3, .L290+12
	ldr	r0, .L290+84
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #3, #1
	bl	printk
.L190:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L191
	ldr	r3, .L290+12
	ldr	r0, .L290+88
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #4, #1
	bl	printk
.L191:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L192
	ldr	r3, .L290+12
	ldr	r0, .L290+92
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #5, #1
	bl	printk
.L192:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L193
	ldr	r3, .L290+12
	ldr	r0, .L290+96
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #6, #1
	bl	printk
.L193:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L194
	ldr	r3, .L290+12
	ldr	r0, .L290+100
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #7, #1
	bl	printk
.L194:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L195
	ldr	r3, .L290+12
	ldr	r0, .L290+104
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #8, #1
	bl	printk
.L195:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L196
	ldr	r3, .L290+12
	ldr	r0, .L290+108
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #9, #1
	bl	printk
.L196:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L197
	ldr	r3, .L290+12
	ldr	r0, .L290+112
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #10, #1
	bl	printk
.L197:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L198
	ldr	r3, .L290+12
	ldr	r0, .L290+116
	ldrb	r2, [r3, #1110]	@ zero_extendqisi2
	ldrb	r1, [r3]	@ zero_extendqisi2
	bl	printk
.L198:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L199
	ldr	r3, .L290+12
	ldr	r0, .L290+120
	ldrb	r2, [r3, #1122]	@ zero_extendqisi2
	ldrb	r1, [r3, #1121]	@ zero_extendqisi2
	bl	printk
.L199:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L200
	ldr	r3, .L290+12
	ldr	r0, .L290+124
	ldrb	r2, [r3, #1120]	@ zero_extendqisi2
	ldrb	r1, [r3, #1119]	@ zero_extendqisi2
	bl	printk
.L200:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L171
	ldr	r3, .L290+12
	ldr	r0, .L290+128
	ldrb	r1, [r3, #1143]	@ zero_extendqisi2
	add	sp, sp, #16
	@ sp needed
	pop	{r4, lr}
	b	printk
.L171:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, pc}
.L291:
	.align	2
.L290:
	.word	.LANCHOR2
	.word	.LANCHOR1+189
	.word	.LC4
	.word	.LANCHOR0
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
	.fnend
	.size	nand_flash_print_info, .-nand_flash_print_info
	.align	2
	.global	timer_delay_ns
	.syntax unified
	.arm
	.fpu softvfp
	.type	timer_delay_ns, %function
timer_delay_ns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ndelay
	.fnend
	.size	timer_delay_ns, .-timer_delay_ns
	.align	2
	.global	nandc_set_ddr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_set_ddr_para, %function
nandc_set_ddr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L296
	ldrb	r3, [r2, #1028]	@ zero_extendqisi2
	ldr	r2, [r2, #1044]
	cmp	r3, #9
	lsl	r3, r0, #16
	lsl	r0, r0, #8
	orr	r3, r3, r0
	orr	r3, r3, #3
	streq	r3, [r2, #80]
	strne	r3, [r2, #304]
	bx	lr
.L297:
	.align	2
.L296:
	.word	.LANCHOR0
	.fnend
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L302
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1044]
	cmp	r2, #9
	ldreq	r0, [r3, #80]
	ldrne	r0, [r3, #304]
	ubfx	r0, r0, #8, #8
	bx	lr
.L303:
	.align	2
.L302:
	.word	.LANCHOR0
	.fnend
	.size	nandc_get_ddr_para, .-nandc_get_ddr_para
	.align	2
	.global	nandc_set_if_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_set_if_mode, %function
nandc_set_if_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L312
	ands	ip, r0, #6
	ldr	r3, [r1, #1044]
	ldr	r2, [r3]
	bfieq	r2, ip, #13, #1
	beq	.L308
	ldrb	r1, [r1, #1028]	@ zero_extendqisi2
	orr	r2, r2, #24576
	bfc	r2, #15, #1
	tst	r0, #4
	orr	r2, r2, #196608
	movw	r0, #8321
	orrne	r2, r2, #32768
	cmp	r1, #9
	ldr	r1, .L312+4
	streq	r0, [r3, #8]
	strne	r0, [r3, #344]
	streq	r1, [r3, #80]
	moveq	r1, #38
	strne	r1, [r3, #304]
	movne	r1, #38
	streq	r1, [r3, #84]
	moveq	r1, #39
	strne	r1, [r3, #308]
	movne	r1, #39
	streq	r1, [r3, #84]
	strne	r1, [r3, #308]
.L308:
	str	r2, [r3]
	bx	lr
.L313:
	.align	2
.L312:
	.word	.LANCHOR0
	.word	1052675
	.fnend
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_cs, %function
nandc_cs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L315
	mov	r2, #1
	lsl	r0, r2, r0
	ldr	r1, [r3, #1044]
	ldr	r3, [r1]
	bfi	r3, r0, #0, #8
	str	r3, [r1]
	bx	lr
.L316:
	.align	2
.L315:
	.word	.LANCHOR0
	.fnend
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	flash_wait_device_ready_raw
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_wait_device_ready_raw, %function
flash_wait_device_ready_raw:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r0
	ldr	r4, .L326
	mov	r5, r1
	str	r2, [sp, #4]
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L318
	mov	r2, #812
	ldr	r1, .L326+4
	ldr	r0, .L326+8
	bl	printk
	bl	dump_stack
.L318:
	add	r6, r4, r6
	ldr	r3, [r4, #1044]
	ldrb	r6, [r6, #1144]	@ zero_extendqisi2
	lsr	r8, r5, #8
	lsr	r9, r5, #16
	lsr	r10, r5, #24
	mov	fp, #120
	add	r7, r3, r6, lsl #8
.L320:
	mov	r0, r6
	bl	nandc_cs
	uxtb	r2, r5
	str	fp, [r7, #2056]
	mov	r0, #120
	str	r2, [r7, #2052]
	ldrb	r2, [r4, #1152]	@ zero_extendqisi2
	str	r8, [r7, #2052]
	str	r9, [r7, #2052]
	cmp	r2, #0
	strne	r10, [r7, #2052]
	bl	ndelay
	ldr	r0, [r7, #2048]
	uxtb	r0, r0
	bl	nandc_de_cs.constprop.35
	ldr	r3, [sp, #4]
	bics	r3, r3, r0
	movne	r2, #1
	moveq	r2, #0
	cmp	r0, #255
	orreq	r2, r2, #1
	cmp	r2, #0
	bne	.L320
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L327:
	.align	2
.L326:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC0
	.fnend
	.size	flash_wait_device_ready_raw, .-flash_wait_device_ready_raw
	.align	2
	.global	flash_wait_device_ready
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_wait_device_ready, %function
flash_wait_device_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r3, #1
	ldr	r5, .L339
	tst	r0, #50331648
	mov	r7, r1
	ldrb	r6, [r5, #1153]	@ zero_extendqisi2
	rsb	r2, r6, #24
	lsl	r6, r3, r6
	lsl	r4, r3, r2
	sub	r6, r6, #1
	sub	r4, r4, #1
	and	r6, r6, r0, asr r2
	and	r4, r4, r0
	uxtb	r6, r6
	bne	.L329
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L330
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L329
.L330:
	ldrh	r8, [r5, #2]
	mov	r0, r4
	mov	r1, r8
	bl	__aeabi_uidiv
	mov	r1, r8
	mul	r9, r8, r0
	mov	r0, r4
	bl	__aeabi_uidivmod
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	lsl	r1, r1, #1
	cmp	r3, #0
	addeq	r5, r5, r1
	addne	r4, r1, r9
	ldrheq	r4, [r5, #4]
	addeq	r4, r4, r9
.L329:
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	flash_wait_device_ready_raw
.L340:
	.align	2
.L339:
	.word	.LANCHOR0
	.fnend
	.size	flash_wait_device_ready, .-flash_wait_device_ready
	.align	2
	.global	nandc_de_cs
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_de_cs, %function
nandc_de_cs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L342
	ldr	r2, [r3, #1044]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L343:
	.align	2
.L342:
	.word	.LANCHOR0
	.fnend
	.size	nandc_de_cs, .-nandc_de_cs
	.align	2
	.global	nandc_wait_flash_ready_no_delay
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_wait_flash_ready_no_delay, %function
nandc_wait_flash_ready_no_delay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L350
	ldr	r5, .L350+4
.L346:
	ldr	r3, [r5, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L347
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L346
	mvn	r0, #0
.L344:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L347:
	mov	r0, #0
	b	.L344
.L351:
	.align	2
.L350:
	.word	100000
	.word	.LANCHOR0
	.fnend
	.size	nandc_wait_flash_ready_no_delay, .-nandc_wait_flash_ready_no_delay
	.align	2
	.global	zftl_flash_enter_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_flash_enter_slc_mode, %function
zftl_flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L375
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	cmp	r2, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, [r3, #1044]
	bne	.L355
	ldr	r3, .L375+4
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r4, r5, r0, lsl #8
	bne	.L374
	pop	{r4, r5, r6, r7, r8, pc}
.L355:
	cmp	r2, #2
	mov	r4, r0
	bne	.L357
	add	r3, r3, r0
	ldrb	r2, [r3, #1154]	@ zero_extendqisi2
	cmp	r2, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	ldr	r6, .L375+4
	mov	r2, #0
	strb	r2, [r3, #1154]
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	cmp	r3, r2
	popeq	{r4, r5, r6, r7, r8, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	add	r4, r5, r4, lsl #8
.L374:
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, pc}
.L357:
	cmp	r2, #3
	popne	{r4, r5, r6, r7, r8, pc}
	add	r6, r3, r0
	ldrb	r3, [r6, #1154]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	add	r4, r5, r4, lsl #8
	mov	r7, #0
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #239
	mov	r0, #100
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	strb	r7, [r6, #1154]
	bl	ndelay
	mov	r3, #1
	str	r7, [r4, #2048]
	mov	r0, #150
	str	r3, [r4, #2048]
	str	r7, [r4, #2048]
	str	r7, [r4, #2048]
	bl	ndelay
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #218
	mov	r0, #50
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, lr}
	b	ndelay
.L376:
	.align	2
.L375:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L402
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	cmp	r2, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r7, [r3, #1044]
	bne	.L380
	ldr	r3, .L402+4
	ldrb	r3, [r3, #34]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r4, r7, r0, lsl #8
	bne	.L401
	pop	{r4, r5, r6, r7, r8, pc}
.L380:
	cmp	r2, #2
	mov	r4, r0
	bne	.L382
	add	r3, r3, r0
	ldrb	r2, [r3, #1154]	@ zero_extendqisi2
	cmp	r2, #0
	popne	{r4, r5, r6, r7, r8, pc}
	ldr	r5, .L402+4
	ldrb	r2, [r5, #16]	@ zero_extendqisi2
	cmp	r2, #2
	movne	r2, #4
	strb	r2, [r3, #1154]
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	add	r4, r7, r4, lsl #8
.L401:
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, pc}
.L382:
	cmp	r2, #3
	popne	{r4, r5, r6, r7, r8, pc}
	add	r6, r3, r0
	ldrb	r3, [r6, #1154]	@ zero_extendqisi2
	cmp	r3, #0
	popne	{r4, r5, r6, r7, r8, pc}
	ldr	r8, .L402+4
	lsl	r4, r4, #8
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	add	r5, r7, r4
	mov	r0, #100
	add	r4, r7, r4
	cmp	r3, #2
	movne	r3, #4
	strb	r3, [r6, #1154]
	mov	r3, #239
	str	r3, [r5, #2056]
	mov	r3, #145
	str	r3, [r5, #2052]
	bl	ndelay
	ldrb	r3, [r8, #11]	@ zero_extendqisi2
	mov	r0, #150
	cmp	r3, #9
	mov	r3, #1
	ldrbne	r2, [r6, #1154]	@ zero_extendqisi2
	streq	r3, [r5, #2048]
	strne	r2, [r5, #2048]
	str	r3, [r5, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	bl	ndelay
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #223
	mov	r0, #50
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, lr}
	b	ndelay
.L403:
	.align	2
.L402:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_page_read, %function
flash_start_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mvn	r2, #0
	ldr	r4, .L419
	mov	r7, r0
	mov	r10, r1
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	rsb	r5, r3, #24
	lsr	r6, r1, r5
	bic	r6, r6, r2, lsl r3
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	uxtb	r6, r6
	cmp	r3, r6
	bhi	.L405
	movw	r2, #1013
	ldr	r1, .L419+4
	ldr	r0, .L419+8
	bl	printk
	bl	dump_stack
.L405:
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r6
	popls	{r4, r5, r6, r7, r8, r9, r10, pc}
	add	r6, r4, r6
	mvn	r3, #0
	ldrb	r9, [r6, #1144]	@ zero_extendqisi2
	bic	r5, r10, r3, lsl r5
	ubfx	r10, r10, #24, #2
	ldr	r6, [r4, #1044]
	mov	r0, r9
	bl	nandc_cs
	cmp	r10, #0
	lsl	r8, r9, #8
	bne	.L407
	mov	r0, r5
	bl	slc_phy_page_address_calc
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #0
	beq	.L408
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
.L408:
	ldr	r3, [r4, #1104]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L410
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r6, r8
	moveq	r2, #38
	streq	r2, [r3, #2056]
.L410:
	add	r3, r6, r8
	mov	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r5
	str	r2, [r3, #2052]
	lsr	r2, r5, #8
	str	r2, [r3, #2052]
	lsr	r2, r5, #16
	str	r2, [r3, #2052]
	ldrb	r2, [r4, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	lsrne	r5, r5, #24
	strne	r5, [r3, #2052]
	add	r3, r6, r8
	str	r7, [r3, #2056]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L407:
	ldr	r3, [r4, #1104]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L409
	ldrb	r3, [r4, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L409
	ldrb	r3, [r4, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r6, r8
	streq	r10, [r3, #2056]
	beq	.L408
.L409:
	mov	r0, r9
	bl	zftl_flash_exit_slc_mode
	b	.L408
.L420:
	.align	2
.L419:
	.word	.LANCHOR0
	.word	.LANCHOR1+239
	.word	.LC0
	.fnend
	.size	flash_start_page_read, .-flash_start_page_read
	.align	2
	.global	nandc_wait_flash_ready
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_wait_flash_ready, %function
nandc_wait_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	mov	r0, #150
	ldr	r4, .L427
	ldr	r5, .L427+4
	bl	ndelay
.L423:
	ldr	r3, [r5, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L424
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L423
	mvn	r0, #0
.L421:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L424:
	mov	r0, #0
	b	.L421
.L428:
	.align	2
.L427:
	.word	100000
	.word	.LANCHOR0
	.fnend
	.size	nandc_wait_flash_ready, .-nandc_wait_flash_ready
	.align	2
	.global	sandisk_set_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	sandisk_set_rr_para, %function
sandisk_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #239
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	str	r3, [r0, #8]
	mov	r3, #17
	mov	r5, r0
	mov	r4, r1
	str	r3, [r0, #4]
	mov	r0, #200
	bl	ndelay
	ldr	r0, .L436
	add	r4, r4, r4, lsl #2
	ldr	r1, .L436+4
	mov	r2, #0
	sub	ip, r0, #45
.L430:
	ldrb	r3, [r1, #1101]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L433
	pop	{r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L433:
	ldrb	r3, [r1, #1100]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	ldrsb	r3, [r3, #5]
	add	r2, r2, #1
	str	r3, [r5]
	b	.L430
.L437:
	.align	2
.L436:
	.word	.LANCHOR1+86
	.word	.LANCHOR0
	.fnend
	.size	sandisk_set_rr_para, .-sandisk_set_rr_para
	.align	2
	.global	toshiba_3d_set_tlc_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_3d_set_tlc_rr_para, %function
toshiba_3d_set_tlc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	add	r1, r1, #1
	ldr	r5, .L440
	rsb	r1, r1, r1, lsl #3
	mov	r6, #0
	mov	r7, #213
	mvn	r3, #118
	add	r5, r5, r1
	str	r7, [r0, #8]
	str	r6, [r0, #4]
	mov	r4, r0
	str	r3, [r0, #4]
	movw	r3, #261
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #262
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #263
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	add	r3, r5, #264
	ldrsb	r3, [r3]
	str	r3, [r0]
	bl	nandc_wait_flash_ready
	mvn	r3, #117
	str	r7, [r4, #8]
	str	r6, [r4, #4]
	str	r3, [r4, #4]
	movw	r3, #265
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #266
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #267
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	pop	{r4, r5, r6, r7, r8, lr}
	b	nandc_wait_flash_ready
.L441:
	.align	2
.L440:
	.word	.LANCHOR1
	.fnend
	.size	toshiba_3d_set_tlc_rr_para, .-toshiba_3d_set_tlc_rr_para
	.align	2
	.global	toshiba_3d_set_slc_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_3d_set_slc_rr_para, %function
toshiba_3d_set_slc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #213
	mvn	r2, #116
	str	r3, [r0, #8]
	mov	r3, #0
	str	r3, [r0, #4]
	str	r2, [r0, #4]
	ldr	r2, .L443
	add	r1, r2, r1
	movw	r2, #661
	ldrsb	r2, [r1, r2]
	str	r2, [r0]
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L444:
	.align	2
.L443:
	.word	.LANCHOR1
	.fnend
	.size	toshiba_3d_set_slc_rr_para, .-toshiba_3d_set_slc_rr_para
	.align	2
	.global	toshiba_tlc_set_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_tlc_set_rr_para, %function
toshiba_tlc_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r2, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, .L449
	mov	r6, #239
	beq	.L446
	rsb	r5, r1, r1, lsl #3
	mov	r2, #18
	str	r6, [r0, #8]
	mov	r4, r0
	add	r5, r3, r5
	str	r2, [r0, #4]
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	str	r3, [r0]
	ldrb	r3, [r5, #37]	@ zero_extendqisi2
	str	r3, [r0]
	ldrb	r3, [r5, #38]	@ zero_extendqisi2
	str	r3, [r0]
	ldrb	r3, [r5, #39]	@ zero_extendqisi2
	str	r3, [r0]
	bl	nandc_wait_flash_ready
	mov	r3, #19
	str	r6, [r4, #8]
	str	r3, [r4, #4]
	ldrb	r3, [r5, #40]	@ zero_extendqisi2
	str	r3, [r4]
	ldrb	r3, [r5, #41]	@ zero_extendqisi2
	str	r3, [r4]
	ldrb	r3, [r5, #42]	@ zero_extendqisi2
	str	r3, [r4]
	mov	r3, #0
	str	r3, [r4]
.L447:
	pop	{r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L446:
	mov	ip, #20
	add	r5, r3, r1
	str	r6, [r0, #8]
	str	ip, [r0, #4]
	ldrb	r3, [r5, #365]	@ zero_extendqisi2
	str	r3, [r0]
	str	r2, [r0]
	str	r2, [r0]
	str	r2, [r0]
	b	.L447
.L450:
	.align	2
.L449:
	.word	.LANCHOR2
	.fnend
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	ymtc_3d_set_tlc_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	ymtc_3d_set_tlc_rr_para, %function
ymtc_3d_set_tlc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	rsb	r1, r1, r1, lsl #3
	ldr	r5, .L453
	mov	r7, #239
	mov	r3, #160
	mov	r6, #0
	str	r7, [r0, #8]
	mov	r4, r0
	add	r5, r5, r1
	str	r3, [r0, #4]
	movw	r3, #671
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #675
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	str	r6, [r0]
	str	r6, [r0]
	bl	nandc_wait_flash_ready
	mov	r3, #161
	str	r7, [r4, #8]
	str	r3, [r4, #4]
	add	r3, r5, #672
	ldrsb	r3, [r3]
	str	r3, [r4]
	movw	r3, #674
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	add	r3, r5, #676
	ldrsb	r3, [r3]
	str	r3, [r4]
	str	r6, [r4]
	bl	nandc_wait_flash_ready
	mov	r3, #162
	str	r7, [r4, #8]
	str	r3, [r4, #4]
	movw	r3, #673
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #677
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	str	r6, [r4]
	pop	{r4, r5, r6, r7, r8, lr}
	b	nandc_wait_flash_ready
.L454:
	.align	2
.L453:
	.word	.LANCHOR1
	.fnend
	.size	ymtc_3d_set_tlc_rr_para, .-ymtc_3d_set_tlc_rr_para
	.align	2
	.global	ymtc_3d_set_slc_rr_para
	.syntax unified
	.arm
	.fpu softvfp
	.type	ymtc_3d_set_slc_rr_para, %function
ymtc_3d_set_slc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #239
	str	r3, [r0, #8]
	mov	r3, #163
	str	r3, [r0, #4]
	ldr	r3, .L456
	add	r1, r3, r1
	movw	r3, #1028
	ldrsb	r3, [r1, r3]
	str	r3, [r0]
	mov	r3, #0
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L457:
	.align	2
.L456:
	.word	.LANCHOR1
	.fnend
	.size	ymtc_3d_set_slc_rr_para, .-ymtc_3d_set_slc_rr_para
	.align	2
	.global	flash_start_plane_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_plane_read, %function
flash_start_plane_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r3, #1
	ldr	r4, .L487
	ubfx	r9, r0, #24, #2
	ldrb	r6, [r4, #1153]	@ zero_extendqisi2
	rsb	r2, r6, #24
	lsl	r6, r3, r6
	lsl	r5, r3, r2
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	sub	r6, r6, #1
	sub	r5, r5, #1
	and	r6, r6, r0, lsr r2
	and	r7, r5, r0
	and	r5, r5, r1
	uxtb	r6, r6
	cmp	r3, r6
	bhi	.L459
	movw	r2, #1148
	ldr	r1, .L487+4
	ldr	r0, .L487+8
	bl	printk
	bl	dump_stack
.L459:
	add	r6, r4, r6
	ldr	r8, [r4, #1044]
	ldrb	r6, [r6, #1144]	@ zero_extendqisi2
	mov	r0, r6
	bl	nandc_cs
	cmp	r9, #0
	lsl	r10, r6, #8
	bne	.L460
	mov	r0, r7
	bl	slc_phy_page_address_calc
	mov	r7, r0
	mov	r0, r5
	bl	slc_phy_page_address_calc
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #0
	beq	.L461
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
.L461:
	ldrb	r3, [r4, #1127]	@ zero_extendqisi2
	uxtb	ip, r7
	lsr	r0, r7, #8
	lsr	r1, r7, #16
	cmp	r3, #1
	bne	.L463
	ldrb	r3, [r4, #1119]	@ zero_extendqisi2
	add	r6, r8, r10
	str	r3, [r6, #2056]
	mov	r3, #0
	str	r3, [r6, #2052]
	str	r3, [r6, #2052]
	ldrb	r3, [r4, #1152]	@ zero_extendqisi2
	str	ip, [r6, #2052]
	str	r0, [r6, #2052]
	cmp	r3, #0
	ldrb	r3, [r4, #1120]	@ zero_extendqisi2
	lsrne	r7, r7, #24
	str	r1, [r6, #2052]
	strne	r7, [r6, #2052]
	add	r7, r8, r10
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #1104]
	cmp	r9, #0
	add	r2, r8, r10
	add	r8, r8, r10
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	sub	r3, r3, #3
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	mov	r3, #0
	strne	r9, [r7, #2056]
	str	r3, [r2, #2056]
	str	r3, [r6, #2052]
	str	r3, [r6, #2052]
	uxtb	r3, r5
	str	r3, [r6, #2052]
	lsr	r3, r5, #8
	str	r3, [r6, #2052]
	lsr	r3, r5, #16
	str	r3, [r6, #2052]
	ldrb	r3, [r4, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	lsrne	r5, r5, #24
	strne	r5, [r6, #2052]
.L486:
	mov	r3, #48
	str	r3, [r8, #2056]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L460:
	ldr	r3, [r4, #1104]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L462
	ldrb	r3, [r4, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L462
	ldrb	r3, [r4, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r8, r10
	streq	r9, [r3, #2056]
	beq	.L461
.L462:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	b	.L461
.L463:
	ldr	r2, [r4, #1104]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L468
	ldrb	r3, [r2, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r8, r10
	moveq	lr, #38
	streq	lr, [r3, #2056]
.L468:
	ldrb	lr, [r4, #1119]	@ zero_extendqisi2
	add	r3, r8, r10
	cmp	r9, #0
	add	r8, r8, r10
	str	lr, [r3, #2056]
	str	ip, [r3, #2052]
	str	r0, [r3, #2052]
	str	r1, [r3, #2052]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	sub	r2, r2, #3
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	ldrb	r2, [r4, #1120]	@ zero_extendqisi2
	strne	r9, [r3, #2056]
	str	r2, [r8, #2056]
	uxtb	r2, r5
	str	r2, [r3, #2052]
	lsr	r2, r5, #8
	lsr	r5, r5, #16
	str	r2, [r3, #2052]
	str	r5, [r3, #2052]
	b	.L486
.L488:
	.align	2
.L487:
	.word	.LANCHOR0
	.word	.LANCHOR1+1038
	.word	.LC0
	.fnend
	.size	flash_start_plane_read, .-flash_start_plane_read
	.align	2
	.global	flash_set_interface_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r0
	ldr	r6, .L528
	mov	r10, #0
	ldr	r9, .L528+4
	mov	r7, r6
.L502:
	ldrb	r4, [r9, r10, lsl #3]	@ zero_extendqisi2
	ldr	r5, [r6, #1044]
	cmp	r4, #69
	beq	.L490
	add	r3, r4, #119
	sub	r1, r4, #44
	uxtb	r2, r3
	clz	r1, r1
	lsr	r1, r1, #5
	cmp	r2, #18
	ldrls	r3, .L528+8
	movhi	r3, #1
	mvnls	r3, r3, lsr r2
	and	r3, r3, #1
	eor	r3, r3, #1
	orrs	r3, r1, r3
	beq	.L492
.L490:
	cmp	r8, #1
	ldrb	r1, [r7, #1192]	@ zero_extendqisi2
	bne	.L493
	tst	r1, #1
	beq	.L492
	ldr	r3, .L528+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L494
	ldr	r0, .L528+16
	bl	printk
.L494:
	lsl	r2, r10, #8
	cmp	r4, #137
	cmpne	r4, #44
	mov	r1, #239
	add	r3, r5, r2
	str	r1, [r3, #2056]
	bne	.L495
.L527:
	mov	r1, #1
	str	r1, [r3, #2052]
	mov	r1, #5
	b	.L525
.L495:
	cmp	r4, #155
	movne	r1, #128
	strne	r1, [r3, #2052]
	movne	r1, #1
	beq	.L527
.L525:
	str	r1, [r3, #2048]
	add	r5, r5, r2
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L492:
	add	r10, r10, #1
	cmp	r10, #4
	bne	.L502
	bl	nandc_wait_flash_ready
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L493:
	tst	r1, #4
	beq	.L492
	ldr	r3, .L528+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L498
	ldr	r0, .L528+20
	bl	printk
.L498:
	lsl	r2, r10, #8
	mov	r1, #239
	cmp	r4, #137
	cmpne	r4, #44
	add	r3, r5, r2
	str	r1, [r3, #2056]
	moveq	r1, #1
	moveq	r1, #1
	movne	r1, #0
	streq	r1, [r3, #2052]
	moveq	r1, #35
	beq	.L525
	cmp	r4, #155
	moveq	r1, #1
	movne	r0, #128
	streq	r1, [r3, #2052]
	moveq	r1, #37
	strne	r0, [r3, #2052]
	b	.L525
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
	.word	.LANCHOR0+1160
	.word	294913
	.word	.LANCHOR2
	.word	.LC34
	.word	.LC35
	.fnend
	.size	flash_set_interface_mode, .-flash_set_interface_mode
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	mt_auto_read_calibration_config, %function
mt_auto_read_calibration_config:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r6, r0
	bl	nandc_wait_flash_ready
	ldr	r3, .L532
	mov	r0, #200
	ldr	r4, [r3, #1044]
	mov	r3, #239
	add	r4, r4, r6, lsl #8
	str	r3, [r4, #2056]
	mov	r3, #150
	str	r3, [r4, #2052]
	bl	ndelay
	mov	r3, #0
	str	r5, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	pop	{r4, r5, r6, pc}
.L533:
	.align	2
.L532:
	.word	.LANCHOR0
	.fnend
	.size	mt_auto_read_calibration_config, .-mt_auto_read_calibration_config
	.align	2
	.global	flash_reset
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_reset, %function
flash_reset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L535
	ldr	r3, [r3, #1044]
	add	r0, r3, r0, lsl #8
	mov	r3, #255
	str	r3, [r0, #2056]
	b	nandc_wait_flash_ready
.L536:
	.align	2
.L535:
	.word	.LANCHOR0
	.fnend
	.size	flash_reset, .-flash_reset
	.align	2
	.global	flash_read_id
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_id, %function
flash_read_id:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	mov	r6, r0
	ldr	r3, .L540
	mov	r4, r1
	ldr	r5, [r3, #1044]
	bl	flash_reset
	mov	r0, r6
	bl	nandc_cs
	add	r5, r5, r6, lsl #8
	mov	r3, #144
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #0
	str	r3, [r5, #2052]
	bl	ndelay
	ldr	r3, [r5, #2048]
	strb	r3, [r4]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #1]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #2]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #3]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #4]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #5]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #6]
	ldr	r3, [r5, #2048]
	strb	r3, [r4, #7]
	bl	nandc_de_cs.constprop.35
	ldrb	r2, [r4]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L537
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	ldr	r0, .L540+4
	str	r1, [sp, #12]
	ldrb	r1, [r4, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r4, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r4, #2]	@ zero_extendqisi2
	str	r1, [sp]
	add	r1, r6, #1
	bl	printk
.L537:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L541:
	.align	2
.L540:
	.word	.LANCHOR0
	.word	.LC36
	.fnend
	.size	flash_read_id, .-flash_read_id
	.align	2
	.global	flash_read_spare
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_spare, %function
flash_read_spare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r2
	ldr	r3, .L544
	ldr	r2, .L544+4
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	ldr	r4, [r2, #1044]
	mov	r2, #0
	lsl	r3, r3, #9
	add	r4, r4, r0, lsl #8
	str	r2, [r4, #2056]
	str	r3, [r4, #2052]
	lsr	r3, r3, #8
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsr	r3, r1, #8
	lsr	r1, r1, #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #2048]
	strb	r3, [r5]
	pop	{r4, r5, r6, pc}
.L545:
	.align	2
.L544:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_read_spare, .-flash_read_spare
	.align	2
	.global	flash_read_otp_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_otp_data, %function
flash_read_otp_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L550
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r9, r0
	lsl	r9, r9, #8
	mov	r6, r1
	mov	r5, r2
	ldr	r8, [r3, #1044]
	mov	r10, #144
	bl	nandc_cs
	mov	r3, #239
	mov	r0, #50
	add	r4, r8, r9
	mov	r7, #0
	str	r3, [r4, #2056]
	str	r10, [r4, #2052]
	bl	ndelay
	mov	r3, #1
	str	r3, [r4, #2048]
	str	r7, [r4, #2048]
	str	r7, [r4, #2048]
	str	r7, [r4, #2048]
	bl	nandc_wait_flash_ready
	mov	r3, #238
	ldr	r0, .L550+4
	str	r3, [r4, #2056]
	str	r10, [r4, #2052]
	ldr	r2, [r4, #2048]
	ldr	r3, [r4, #2048]
	ldr	r1, [r4, #2048]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	bl	nandc_wait_flash_ready
	uxtb	r3, r6
	str	r7, [r4, #2056]
	str	r7, [r4, #2052]
	str	r7, [r4, #2052]
	str	r3, [r4, #2052]
	lsr	r3, r6, #8
	lsr	r6, r6, #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r6, [r4, #2052]
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	add	r3, r5, #16384
	mov	r2, r5
.L547:
	ldr	r1, [r4, #2048]
	strb	r1, [r2], #1
	cmp	r2, r3
	bne	.L547
	add	r8, r8, r9
	mov	r3, #239
	str	r3, [r8, #2056]
	mov	r3, #144
	str	r3, [r4, #2052]
	mov	r0, #50
	bl	ndelay
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	nandc_de_cs.constprop.35
.L551:
	.align	2
.L550:
	.word	.LANCHOR0
	.word	.LC37
	.fnend
	.size	flash_read_otp_data, .-flash_read_otp_data
	.align	2
	.global	sandisk_prog_test_bad_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r7, r0, #8
	ldr	r3, .L568
	mov	r8, r1
	ldr	r6, .L568+4
	ldr	r5, [r3, #1044]
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	add	r4, r5, r7
	cmp	r3, #0
	addne	r2, r5, r7
	addeq	r3, r5, r7
	moveq	r2, #162
	strne	r3, [r2, #2056]
	streq	r2, [r3, #2056]
	mov	r3, #128
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsr	r3, r1, #8
	str	r3, [r4, #2052]
	lsr	r3, r1, #16
	str	r3, [r4, #2052]
	mov	r3, #16
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	mov	r3, #112
	mov	r0, #200
	str	r3, [r4, #2056]
	bl	ndelay
	ldr	r2, [r4, #2048]
	cmp	r2, #255
	ldreq	r2, [r4, #2048]
	ands	r4, r2, #5
	beq	.L556
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L556
	mov	r1, r8
	ldr	r0, .L568+8
	bl	printk
.L556:
	ldrb	r3, [r6, #34]	@ zero_extendqisi2
	mov	r0, r4
	cmp	r3, #0
	addne	r5, r5, r7
	strne	r3, [r5, #2056]
	pop	{r4, r5, r6, r7, r8, pc}
.L569:
	.align	2
.L568:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC38
	.fnend
	.size	sandisk_prog_test_bad_block, .-sandisk_prog_test_bad_block
	.align	2
	.global	nandc_rdy_status
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_rdy_status, %function
nandc_rdy_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L572
	.pad #8
	sub	sp, sp, #8
	ldr	r3, [r3, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #9, #1
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L573:
	.align	2
.L572:
	.word	.LANCHOR0
	.fnend
	.size	nandc_rdy_status, .-nandc_rdy_status
	.align	2
	.global	nandc_bch_sel
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_bch_sel, %function
nandc_bch_sel:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
	mov	r3, #0
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #1
	str	r2, [sp, #4]
	ldr	r2, .L586
	ldrb	r1, [r2, #1028]	@ zero_extendqisi2
	strb	r0, [r2, #1193]
	cmp	r1, #9
	bne	.L575
	ldr	r2, [r2, #1044]
	cmp	r0, #70
	ldr	r1, [sp, #4]
	str	r1, [r2, #16]
	beq	.L576
	cmp	r0, #60
	moveq	r3, #3
	beq	.L576
	cmp	r0, #40
	moveq	r3, #2
	movne	r3, #1
.L576:
	mov	r1, #0
	str	r1, [sp]
	ldr	r1, [sp]
	bfi	r1, r3, #25, #3
	str	r1, [sp]
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r2, #32]
.L574:
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L575:
	ldr	r1, [r2, #1044]
	mov	ip, #16
	ldr	r2, [sp, #4]
	cmp	r0, ip
	str	r2, [r1, #8]
	str	r3, [sp]
	ldr	r2, [sp]
	bfi	r2, ip, #8, #8
	str	r2, [sp]
	ldr	r2, [sp]
	bfi	r2, r3, #18, #1
	str	r2, [sp]
	bne	.L578
.L585:
	ldr	r2, [sp]
	bfi	r2, r3, #4, #1
	str	r2, [sp]
	b	.L579
.L578:
	cmp	r0, #24
	ldreq	r3, [sp]
	orreq	r3, r3, #16
	streq	r3, [sp]
	beq	.L579
	ldr	r2, [sp]
	cmp	r0, #40
	orr	r2, r2, #262144
	str	r2, [sp]
	ldr	r2, [sp]
	orr	r2, r2, #16
	str	r2, [sp]
	beq	.L585
.L579:
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r1, #12]
	b	.L574
.L587:
	.align	2
.L586:
	.word	.LANCHOR0
	.fnend
	.size	nandc_bch_sel, .-nandc_bch_sel
	.align	2
	.global	zftl_flash_resume
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_flash_resume, %function
zftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L599
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r2, [r4, #1048]
	cmp	r3, #9
	ldr	r3, [r4, #1044]
	str	r2, [r3]
	ldr	r2, [r4, #1052]
	ldr	r3, [r4, #1044]
	str	r2, [r3, #4]
	ldr	r2, [r4, #1056]
	bne	.L589
	str	r2, [r3, #16]
	ldr	r2, [r4, #1060]
	str	r2, [r3, #32]
	ldr	r2, [r4, #1064]
	str	r2, [r3, #80]
	ldr	r2, [r4, #1068]
	str	r2, [r3, #84]
	ldr	r2, [r4, #1072]
	str	r2, [r3, #520]
	ldr	r2, [r4, #1076]
	str	r2, [r3, #8]
.L591:
	ldr	r6, .L599+4
	mov	r5, #0
	mov	r8, #2
	sub	r7, r6, #6
.L590:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L592
	uxtb	r9, r5
	mov	r0, r9
	bl	flash_reset
	strb	r8, [r5, r7]
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
.L592:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L590
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L593
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r0, r5
	bl	flash_set_interface_mode
	mov	r0, r5
	bl	nandc_set_if_mode
	ldrb	r0, [r4, #1065]	@ zero_extendqisi2
	bl	nandc_set_ddr_para
.L593:
	ldr	r3, .L599+8
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	ldrb	r0, [r3, #24]	@ zero_extendqisi2
	b	nandc_bch_sel
.L589:
	str	r2, [r3, #8]
	ldr	r2, [r4, #1060]
	str	r2, [r3, #12]
	ldr	r2, [r4, #1064]
	str	r2, [r3, #304]
	ldr	r2, [r4, #1068]
	str	r2, [r3, #308]
	ldr	r2, [r4, #1072]
	str	r2, [r3, #336]
	ldr	r2, [r4, #1076]
	str	r2, [r3, #344]
	b	.L591
.L600:
	.align	2
.L599:
	.word	.LANCHOR0
	.word	.LANCHOR0+1160
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_resume, .-zftl_flash_resume
	.align	2
	.global	rk_nandc_flash_ready
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #1
	b	nandc_irq_disable
	.fnend
	.size	rk_nandc_flash_ready, .-rk_nandc_flash_ready
	.align	2
	.global	nandc_iqr_wait_flash_ready
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_iqr_wait_flash_ready, %function
nandc_iqr_wait_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L607
	ldr	r0, [r4, #1044]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, [r4, #1044]
	cmp	r3, #9
	ldreq	r3, [r0, #292]
	ldrne	r3, [r0, #368]
	orreq	r3, r3, #2
	orrne	r3, r3, #2
	streq	r3, [r0, #292]
	strne	r3, [r0, #368]
	ldreq	r3, [r0, #288]
	ldrne	r3, [r0, #364]
	orreq	r3, r3, #2
	orrne	r3, r3, #2
	streq	r3, [r0, #288]
	strne	r3, [r0, #364]
	ldr	r3, [r0]
	tst	r3, #512
	bne	.L605
	pop	{r4, lr}
	b	wait_for_nand_flash_ready
.L605:
	mov	r1, #1
	pop	{r4, lr}
	b	nandc_irq_disable
.L608:
	.align	2
.L607:
	.word	.LANCHOR0
	.fnend
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r7, .L634
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	ldrb	r3, [r7, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L610
	movw	r2, #695
	ldr	r1, .L634+4
	ldr	r0, .L634+8
	bl	printk
	bl	dump_stack
.L610:
	ldrb	r3, [r7, #1109]	@ zero_extendqisi2
	cmp	r3, r4
	bls	.L619
	ldr	r3, .L634+12
	add	r4, r7, r4
	ldrb	r4, [r4, #1144]	@ zero_extendqisi2
	ldr	r9, [r7, #1044]
	ldr	r3, [r3]
	add	r8, r4, #8
	tst	r3, #16
	add	r8, r9, r8, lsl #8
	beq	.L612
	mov	r3, r10
	mov	r2, r6
	mov	r1, r4
	ldr	r0, .L634+16
	bl	printk
.L612:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	bl	nandc_cs
	cmp	r5, #0
	mov	r0, r4
	bne	.L613
	bl	zftl_flash_enter_slc_mode
.L614:
	lsl	r4, r4, #8
	mov	r3, #96
	add	r5, r9, r4
	str	r3, [r5, #2056]
	uxtb	r3, r6
	str	r3, [r5, #2052]
	lsr	r3, r6, #8
	str	r3, [r5, #2052]
	lsr	r3, r6, #16
	str	r3, [r5, #2052]
	ldrb	r3, [r7, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	lsrne	r3, r6, #24
	strne	r3, [r5, #2052]
	ldrb	r3, [r7, #1194]	@ zero_extendqisi2
	cmp	r3, #0
	movne	fp, #0
	bne	.L616
	add	r3, r9, r4
	mov	r2, #208
	str	r2, [r3, #2056]
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	and	fp, r0, #5
.L616:
	mov	r2, #96
	add	r3, r9, r4
	str	r2, [r3, #2056]
	uxtb	r3, r10
	add	r0, r9, r4
	str	r3, [r5, #2052]
	lsr	r3, r10, #8
	str	r3, [r5, #2052]
	lsr	r3, r10, #16
	str	r3, [r5, #2052]
	ldrb	r3, [r7, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r3, #208
	lsrne	r2, r10, #24
	strne	r2, [r5, #2052]
	str	r3, [r0, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	bl	nandc_de_cs.constprop.35
	and	r3, r0, #5
	orrs	fp, r3, fp
	beq	.L618
	mov	r2, r0
	mov	r1, r6
	ldr	r0, .L634+20
	bl	printk
.L618:
	mov	r0, fp
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L613:
	bl	zftl_flash_exit_slc_mode
	b	.L614
.L619:
	mvn	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L635:
	.align	2
.L634:
	.word	.LANCHOR0
	.word	.LANCHOR1+1061
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC39
	.word	.LC40
	.fnend
	.size	flash_erase_duplane_block, .-flash_erase_duplane_block
	.align	2
	.global	flash_erase_block_en
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_erase_block_en, %function
flash_erase_block_en:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r5, .L655
	mov	fp, r1
	mov	r8, r2
	ldrb	r6, [r5, #1153]	@ zero_extendqisi2
	rsb	r3, r6, #24
	mvn	r6, #0
	bic	r6, r2, r6, lsl r3
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L637
	movw	r2, #757
	ldr	r1, .L655+4
	ldr	r0, .L655+8
	bl	printk
	bl	dump_stack
.L637:
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L644
	add	r3, r5, r7
	ldr	r9, [r5, #1044]
	ldrb	r4, [r3, #1144]	@ zero_extendqisi2
	ldr	r3, .L655+12
	add	r10, r4, #8
	ldr	r3, [r3]
	add	r10, r9, r10, lsl #8
	tst	r3, #16
	beq	.L639
	mov	r3, fp
	mov	r2, r8
	mov	r1, r4
	ldr	r0, .L655+16
	bl	printk
.L639:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r4
	bne	.L640
	bl	zftl_flash_enter_slc_mode
.L641:
	lsl	r4, r4, #8
	mov	r2, #96
	add	r3, r9, r4
	add	r4, r9, r4
	str	r2, [r3, #2056]
	uxtb	r2, r6
	str	r2, [r3, #2052]
	lsr	r2, r6, #8
	str	r2, [r3, #2052]
	lsr	r2, r6, #16
	str	r2, [r3, #2052]
	ldrb	r2, [r5, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	lsrne	r6, r6, #24
	strne	r6, [r3, #2052]
	mov	r3, #208
	str	r3, [r4, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	bl	nandc_de_cs.constprop.35
	ands	r4, r0, #5
	beq	.L643
	ldrh	r1, [r5, #2]
	mov	r0, r8
	bl	__aeabi_uidiv
	mov	r3, r4
	mvn	r4, #0
	mov	r2, r0
	mov	r1, r7
	ldr	r0, .L655+20
	bl	printk
.L643:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L640:
	bl	zftl_flash_exit_slc_mode
	b	.L641
.L644:
	mvn	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L656:
	.align	2
.L655:
	.word	.LANCHOR0
	.word	.LANCHOR1+1087
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC41
	.word	.LC42
	.fnend
	.size	flash_erase_block_en, .-flash_erase_block_en
	.align	2
	.global	flash_erase_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_erase_block, %function
flash_erase_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r2, r1
	mov	r1, #0
	b	flash_erase_block_en
	.fnend
	.size	flash_erase_block, .-flash_erase_block
	.align	2
	.global	flash_erase_all
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_erase_all, %function
flash_erase_all:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L664
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r7, .L664+4
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	mov	r8, r7
	smulbb	r4, r4, r3
	uxth	r4, r4
.L659:
	ldrb	r2, [r7, #1109]	@ zero_extendqisi2
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L662
	mov	r1, #0
	ldr	r0, .L664+8
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L662:
	uxtah	r3, r7, r5
	mov	r6, #0
	ldrb	r9, [r3, #1144]	@ zero_extendqisi2
.L660:
	uxth	r3, r6
	cmp	r4, r3
	addls	r5, r5, #1
	bls	.L659
.L661:
	ldrh	r1, [r8, #2]
	mov	r0, r9
	mul	r1, r6, r1
	add	r6, r6, #1
	bl	flash_erase_block
	b	.L660
.L665:
	.align	2
.L664:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC43
	.fnend
	.size	flash_erase_all, .-flash_erase_all
	.align	2
	.global	rk_nandc_flash_xfer_completed
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_nandc_flash_xfer_completed, %function
rk_nandc_flash_xfer_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	b	nandc_irq_disable
	.fnend
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
	.align	2
	.global	nandc_xfer_start
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_xfer_start, %function
nandc_xfer_start:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r5, r0
	ldr	r4, .L684
	mov	r0, r2
	and	r6, r5, #1
	add	ip, r1, #1
	ldrb	r2, [r4, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L668
	mov	r1, #0
	mov	lr, #1
	str	r1, [sp, #4]
	ubfx	ip, ip, #1, #6
	ldr	r2, [sp, #4]
	bfi	r2, r6, #1, #1
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #8
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bfi	r2, lr, #5, #2
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #536870912
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #1024
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bfi	r2, r1, #4, #1
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bfi	r2, ip, #22, #6
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #128
	str	r2, [sp, #4]
	ldrb	r2, [r4, #1195]	@ zero_extendqisi2
	cmp	r2, r1
	beq	.L669
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	cmp	r2, r1
	ldrne	r2, [sp, #4]
	orrne	r2, r2, #512
	strne	r2, [sp, #4]
.L669:
	ldr	r1, [sp, #4]
	mov	r2, r5
	str	r3, [r4, #1208]
	str	r0, [r4, #1204]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	bl	rknand_dma_map_single
	ldr	r1, [sp, #4]
	mov	r2, r5
	str	r0, [r4, #1212]
	clz	r5, r5
	ldr	r0, [r4, #1208]
	lsr	r5, r5, #5
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #2
	bl	rknand_dma_map_single
	mov	r3, #1
	ldr	r2, [r4, #1212]
	str	r3, [r4, #1220]
	mov	r1, #16
	ldr	r3, [r4, #1044]
	str	r0, [r4, #1216]
	str	r2, [r3, #52]
	str	r0, [r3, #56]
	ldr	r2, [r3, #48]
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r1, #9, #5
	mov	r1, #2
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #448
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r1, #3, #3
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #4
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r5, #1, #1
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #1
	str	r2, [sp, #8]
	movw	r2, #1228
	ldrh	r1, [r4, r2]
	ldr	r2, [sp, #8]
	bfi	r2, r1, #16, #11
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	str	r2, [r3, #48]
	ldr	r2, [sp, #4]
	str	r2, [r3, #16]
	ldr	r2, [sp, #4]
	orr	r2, r2, #4
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	str	r2, [r3, #16]
.L667:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L668:
	ldr	r2, [r4, #1044]
	mov	lr, #16
	ubfx	ip, ip, #1, #6
	ldr	r2, [r2, #12]
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bfi	r2, lr, #8, #8
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bfc	r2, #3, #1
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bfc	r2, #5, #3
	str	r2, [sp, #12]
	mov	r2, #0
	str	r2, [sp, #4]
	cmp	r5, r2
	ldr	lr, [sp, #4]
	ldreq	r3, [r4, #1200]
	bfi	lr, r6, #1, #1
	mov	r6, #1
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	orr	lr, lr, #8
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	bfi	lr, r6, #5, #2
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	orr	lr, lr, #536870912
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	orr	lr, lr, #1024
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	bfi	lr, r2, #4, #1
	str	lr, [sp, #4]
	ldr	lr, [sp, #4]
	bfi	lr, ip, #22, #6
	str	lr, [sp, #4]
	streq	r6, [r3]
	beq	.L675
	ldrb	ip, [r4, #1193]	@ zero_extendqisi2
	lsr	r1, r1, #1
	cmp	ip, #25
	mov	ip, r2
	movcc	r6, #64
	movcs	r6, #128
.L673:
	cmp	ip, r1
	add	r7, r3, ip, lsl #2
	add	lr, r6, r2
	blt	.L674
.L675:
	ldr	r1, [sp, #4]
	mov	r2, r5
	ldr	r3, [r4, #1200]
	str	r0, [r4, #1204]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	str	r3, [r4, #1208]
	bl	rknand_dma_map_single
	ldr	r1, [sp, #4]
	mov	r2, r5
	str	r0, [r4, #1212]
	clz	r5, r5
	ldr	r0, [r4, #1208]
	lsr	r5, r5, #5
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #7
	bl	rknand_dma_map_single
	mov	r3, #1
	ldr	r2, [r4, #1212]
	str	r3, [r4, #1220]
	mov	r1, #16
	ldr	r3, [r4, #1044]
	str	r0, [r4, #1216]
	str	r2, [r3, #20]
	mov	r2, #0
	str	r0, [r3, #24]
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r1, #9, #5
	mov	r1, #2
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #448
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r1, #3, #3
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #4
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bfi	r2, r5, #1, #1
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #1
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	str	r2, [r3, #16]
	ldr	r2, [sp, #12]
	str	r2, [r3, #12]
	ldr	r2, [sp, #4]
	str	r2, [r3, #8]
	ldr	r2, [sp, #4]
	orr	r2, r2, #4
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	str	r2, [r3, #8]
	b	.L667
.L674:
	ldr	r8, [r7]	@ unaligned
	bic	r2, r2, #3
	ldr	r7, [r4, #1200]
	add	ip, ip, #1
	str	r8, [r7, r2]
	mov	r2, lr
	b	.L673
.L685:
	.align	2
.L684:
	.word	.LANCHOR0
	.fnend
	.size	nandc_xfer_start, .-nandc_xfer_start
	.align	2
	.global	nandc_set_seed
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_set_seed, %function
nandc_set_seed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L693
	and	r0, r0, #127
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldr	r3, .L693+4
	ldrb	r1, [r3, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	ldrb	r1, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1044]
	orrne	r2, r2, #-1073741824
	cmp	r1, #9
	streq	r2, [r3, #520]
	strne	r2, [r3, #336]
	bx	lr
.L694:
	.align	2
.L693:
	.word	.LANCHOR1+1108
	.word	.LANCHOR0
	.fnend
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	zftl_flash_de_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_flash_de_init, %function
zftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L715
	bl	nandc_wait_flash_ready
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L696
	ldrb	r0, [r4, #1110]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L696
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	beq	.L696
	bl	zftl_flash_exit_slc_mode
.L696:
	mov	r0, #0
	bl	hynix_reconfig_rr_para
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L697
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L697
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #1143]
.L697:
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L698
	mov	r0, #0
	strb	r0, [r4, #1196]
	bl	nandc_set_seed
	mov	r3, #1
	strb	r3, [r4, #1196]
.L698:
	mov	r0, #0
	pop	{r4, pc}
.L716:
	.align	2
.L715:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L718
	strb	r0, [r3, #1196]
	bx	lr
.L719:
	.align	2
.L718:
	.word	.LANCHOR0
	.fnend
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L721
	add	r0, r0, #8
	ldr	r3, [r3, #1044]
	add	r0, r3, r0, lsl #8
	bx	lr
.L722:
	.align	2
.L721:
	.word	.LANCHOR0
	.fnend
	.size	nandc_get_chip_if, .-nandc_get_chip_if
	.align	2
	.global	buf_reinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	buf_reinit, %function
buf_reinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L726
	mov	r3, #0
	mov	r1, r3
.L724:
	uxtb	r0, r3
	add	r3, r3, #1
	cmp	r3, #32
	strb	r1, [r2, #2]
	add	ip, r0, #1
	strb	r0, [r2, #1]
	strb	ip, [r2]
	add	r2, r2, #48
	str	r1, [r2, #-40]
	bne	.L724
	ldr	r2, .L726+4
	mvn	r0, #0
	strb	r0, [r2, #2720]
	strb	r1, [r2, #2768]
	strb	r3, [r2, #2769]
	bx	lr
.L727:
	.align	2
.L726:
	.word	.LANCHOR0+1232
	.word	.LANCHOR0
	.fnend
	.size	buf_reinit, .-buf_reinit
	.align	2
	.global	buf_add_tail
	.syntax unified
	.arm
	.fpu softvfp
	.type	buf_add_tail, %function
buf_add_tail:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mvn	r3, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	strb	r3, [r1]
	mov	r4, r1
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L729
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #255
	bne	.L730
	mov	r2, #74
	ldr	r1, .L736
	ldr	r0, .L736+4
	bl	printk
	bl	dump_stack
.L730:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r3, [r5]
	pop	{r4, r5, r6, pc}
.L729:
	ldr	r2, .L736+8
	mov	r1, #48
	mov	r6, r2
.L734:
	mov	r5, r3
	mla	r3, r1, r3, r2
	ldrb	r3, [r3, #1232]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L734
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L732
	mov	r2, #81
	ldr	r1, .L736
	ldr	r0, .L736+4
	bl	printk
	bl	dump_stack
.L732:
	mov	r3, #48
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	mla	r3, r3, r5, r6
	strb	r2, [r3, #1232]
	pop	{r4, r5, r6, pc}
.L737:
	.align	2
.L736:
	.word	.LANCHOR1+1364
	.word	.LC0
	.word	.LANCHOR0
	.fnend
	.size	buf_add_tail, .-buf_add_tail
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_read_cmd, %function
queue_read_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r0, #24]
	mov	r0, #48
	bl	flash_start_page_read
	mov	r3, #1
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #0
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldr	r0, .L740
	pop	{r4, lr}
	b	buf_add_tail
.L741:
	.align	2
.L740:
	.word	.LANCHOR0+2770
	.fnend
	.size	queue_read_cmd, .-queue_read_cmd
	.align	2
	.global	zbuf_free
	.syntax unified
	.arm
	.fpu softvfp
	.type	zbuf_free, %function
zbuf_free:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	ldr	r4, .L752
	and	r3, r3, #8
	cmp	r3, #0
	strb	r3, [r0, #2]
	beq	.L743
	ldr	r3, [r0, #20]
	cmn	r3, #1
	beq	.L743
	mov	r1, r0
	add	r0, r4, #2768
	bl	buf_add_tail
.L744:
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #2769]
	pop	{r4, pc}
.L743:
	ldrb	r3, [r4, #2768]	@ zero_extendqisi2
	strb	r3, [r0]
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	strb	r3, [r4, #2768]
	b	.L744
.L753:
	.align	2
.L752:
	.word	.LANCHOR0
	.fnend
	.size	zbuf_free, .-zbuf_free
	.align	2
	.global	buf_alloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	buf_alloc, %function
buf_alloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L765
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L755
.L758:
	ldrb	ip, [r4, #2768]	@ zero_extendqisi2
	cmp	r5, #0
	ldr	r3, .L765+4
	add	r0, ip, ip, lsl #1
	mov	r2, r3
	add	r0, r3, r0, lsl #4
	beq	.L756
.L757:
	mov	r1, #48
	mul	r1, r1, ip
	add	r3, r4, r1
	add	r2, r2, r1
	ldrb	ip, [r3, #1232]	@ zero_extendqisi2
	mov	r1, #0
	strh	r1, [r2, #34]	@ movhi
	str	r1, [r3, #1240]
	strb	ip, [r4, #2768]
	ldrb	ip, [r4, #2769]	@ zero_extendqisi2
	strb	r1, [r3, #1272]
	strb	r1, [r3, #1273]
	sub	ip, ip, #1
	strb	ip, [r4, #2769]
	mov	ip, #1
	strb	ip, [r3, #1234]
	mvn	ip, #0
	strb	ip, [r3, #1232]
	str	ip, [r3, #1252]
	pop	{r4, r5, r6, pc}
.L755:
	mov	r2, #121
	ldr	r1, .L765+8
	ldr	r0, .L765+12
	bl	printk
	bl	dump_stack
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L758
.L760:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L756:
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L757
	b	.L760
.L766:
	.align	2
.L765:
	.word	.LANCHOR0
	.word	.LANCHOR0+1232
	.word	.LANCHOR1+1377
	.word	.LC0
	.fnend
	.size	buf_alloc, .-buf_alloc
	.align	2
	.global	buf_remove_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	buf_remove_buf, %function
buf_remove_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrb	ip, [r1, #1]	@ zero_extendqisi2
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	ip, r3
	bne	.L768
	ldrb	r3, [r1]	@ zero_extendqisi2
	strb	r3, [r0]
	mov	r0, #1
	bx	lr
.L771:
	mov	r2, r3
	mla	r3, lr, r3, r0
	ldrb	r3, [r3, #1232]	@ zero_extendqisi2
	cmp	ip, r3
	bne	.L770
	mla	r2, lr, r2, r0
	ldrb	r3, [r1]	@ zero_extendqisi2
	mov	r0, #1
	strb	r3, [r2, #1232]
	mvn	r3, #0
	strb	r3, [r1]
	ldr	pc, [sp], #4
.L768:
	ldr	r0, .L776
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #48
.L770:
	cmp	r3, #255
	bne	.L771
	mov	r0, #0
	ldr	pc, [sp], #4
.L777:
	.align	2
.L776:
	.word	.LANCHOR0
	.fnend
	.size	buf_remove_buf, .-buf_remove_buf
	.align	2
	.global	buf_remove_free
	.syntax unified
	.arm
	.fpu softvfp
	.type	buf_remove_free, %function
buf_remove_free:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L784
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L779
	mov	r2, #172
	ldr	r1, .L784+4
	ldr	r0, .L784+8
	bl	printk
	bl	dump_stack
.L779:
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, pc}
	mov	r1, r5
	ldr	r0, .L784+12
	bl	buf_remove_buf
	cmp	r0, #1
	ldrbeq	r3, [r4, #2769]	@ zero_extendqisi2
	subeq	r3, r3, #1
	strbeq	r3, [r4, #2769]
	ldrbeq	r3, [r5, #2]	@ zero_extendqisi2
	orreq	r3, r3, #1
	strbeq	r3, [r5, #2]
	pop	{r4, r5, r6, pc}
.L785:
	.align	2
.L784:
	.word	.LANCHOR0
	.word	.LANCHOR1+1387
	.word	.LC0
	.word	.LANCHOR0+2768
	.fnend
	.size	buf_remove_free, .-buf_remove_free
	.align	2
	.global	dump_buf_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_buf_info, %function
dump_buf_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r5, .L790
	ldr	r0, .L790+4
	ldrb	r1, [r5, #2770]	@ zero_extendqisi2
	add	r4, r5, #1232
	bl	printk
	ldrb	r1, [r5, #2771]	@ zero_extendqisi2
	add	r5, r5, #2768
	ldr	r0, .L790+8
	bl	printk
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	ldr	r0, .L790+12
	bl	printk
	ldr	r6, .L790+16
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	ldr	r0, .L790+20
	bl	printk
	ldrb	r1, [r5]	@ zero_extendqisi2
	ldr	r0, .L790+24
	bl	printk
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L790+28
	bl	printk
.L787:
	ldr	r0, [r4, #24]
	add	r4, r4, #48
	ldrb	r3, [r4, #-46]	@ zero_extendqisi2
	ldrb	r2, [r4, #-48]	@ zero_extendqisi2
	ldrb	r1, [r4, #-47]	@ zero_extendqisi2
	str	r0, [sp, #16]
	ldr	r0, [r4, #-28]
	str	r0, [sp, #12]
	ldr	r0, [r4, #-12]
	str	r0, [sp, #8]
	ldrb	r0, [r4, #-6]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldrh	r0, [r4, #-14]
	str	r0, [sp]
	mov	r0, r6
	bl	printk
	cmp	r4, r5
	bne	.L787
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, pc}
.L791:
	.align	2
.L790:
	.word	.LANCHOR0
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC50
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.fnend
	.size	dump_buf_info, .-dump_buf_info
	.align	2
	.global	flash_check_bad_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_check_bad_block, %function
flash_check_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L793
	lsr	ip, r1, #5
	and	r1, r1, #31
	ldrb	r3, [r2, #17]	@ zero_extendqisi2
	ldrh	r2, [r2, #18]
	smulbb	r3, r3, r2
	ldr	r2, .L793+4
	uxth	r3, r3
	ldr	r2, [r2, #1040]
	add	r3, r3, #31
	asr	r3, r3, #5
	add	r2, r2, ip, lsl #2
	lsl	r3, r3, #2
	uxth	r3, r3
	mla	r0, r0, r3, r2
	ldr	r0, [r0, #912]
	lsr	r0, r0, r1
	and	r0, r0, #1
	bx	lr
.L794:
	.align	2
.L793:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_check_bad_block, .-flash_check_bad_block
	.align	2
	.global	flash_mask_bad_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_mask_bad_block, %function
flash_mask_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L797
	mov	r2, r1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	mov	r6, r1
	mov	r1, r0
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	ldr	r0, .L797+4
	smulbb	r4, r4, r3
	bl	printk
	uxth	r4, r4
	ldr	r3, .L797+8
	mov	r1, #1
	add	r0, r4, #31
	asr	r0, r0, #5
	ldr	r2, [r3, #1040]
	lsl	r0, r0, #2
	uxth	r0, r0
	mul	r0, r5, r0
	lsr	r5, r6, #5
	and	r6, r6, #31
	add	r0, r0, #912
	add	r0, r0, r5, lsl #2
	ldr	r3, [r2, r0]
	orr	r6, r3, r1, lsl r6
	str	r6, [r2, r0]
	pop	{r4, r5, r6, pc}
.L798:
	.align	2
.L797:
	.word	.LANCHOR2
	.word	.LC51
	.word	.LANCHOR0
	.fnend
	.size	flash_mask_bad_block, .-flash_mask_bad_block
	.align	2
	.global	str2hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	str2hex, %function
str2hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #48
	bne	.L800
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #2
.L800:
	ldrb	r3, [r0]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #1
	sub	r3, r0, #1
	mov	r0, #0
.L802:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L807
	bx	lr
.L807:
	sub	r1, r2, #48
	uxtb	ip, r1
	cmp	ip, #9
	addls	r0, r1, r0, lsl #4
	bls	.L802
	sub	r1, r2, #97
	cmp	r1, #5
	subls	r2, r2, #87
	bls	.L809
	sub	r1, r2, #65
	cmp	r1, #5
	bxhi	lr
	sub	r2, r2, #55
.L809:
	add	r0, r2, r0, lsl #4
	b	.L802
	.fnend
	.size	str2hex, .-str2hex
	.align	2
	.global	zftl_proc_debug_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_proc_debug_init, %function
zftl_proc_debug_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, lr}
	.save {lr}
	.pad #12
	mov	r2, #0
	str	r2, [sp]
	mov	r1, #292
	ldr	r3, .L812
	ldr	r0, .L812+4
	bl	proc_create_data
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L813:
	.align	2
.L812:
	.word	.LANCHOR1+1404
	.word	.LC52
	.fnend
	.size	zftl_proc_debug_init, .-zftl_proc_debug_init
	.align	2
	.global	ftl_print_info_to_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_print_info_to_buf, %function
ftl_print_info_to_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	mov	r6, r0
	ldr	r2, .L816
	ldr	r1, .L816+4
	bl	sprintf
	ldr	r1, .L816+8
	add	r4, r6, r0
	ldr	r5, .L816+12
	ldrb	r0, [r1, #10]	@ zero_extendqisi2
	ldrb	r3, [r1, #6]	@ zero_extendqisi2
	ldrb	r2, [r1, #5]	@ zero_extendqisi2
	str	r0, [sp, #12]
	ldrb	r0, [r1, #9]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldrb	r0, [r1, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	mov	r0, r4
	ldrb	r1, [r1, #7]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, .L816+16
	bl	sprintf
	ldr	r2, [r5, #1032]
	add	r4, r4, r0
	ldr	r1, .L816+20
	mov	r0, r4
	lsr	r2, r2, #11
	bl	sprintf
	ldr	r2, [r5, #2776]
	add	r4, r4, r0
	ldr	r1, .L816+24
	mov	r0, r4
	lsr	r2, r2, #11
	bl	sprintf
	add	r4, r4, r0
	ldr	r1, .L816+28
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r2, [r5, #2780]
	mov	r0, r4
	ldr	r1, .L816+32
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r5, #1032]
	ldr	r1, .L816+36
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+40
	mov	r0, r4
	ldr	r2, [r3, #524]
	bl	sprintf
	ldr	r3, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+44
	mov	r0, r4
	ldr	r2, [r3, #528]
	bl	sprintf
	add	r3, r5, #2784
	add	r4, r4, r0
	ldrh	r2, [r3]
	mov	r0, r4
	ldr	r1, .L816+48
	bl	sprintf
	movw	r3, #2786
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+52
	bl	sprintf
	movw	r3, #2788
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+56
	bl	sprintf
	movw	r3, #2790
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+60
	bl	sprintf
	movw	r3, #2792
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+64
	bl	sprintf
	movw	r3, #2794
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+68
	bl	sprintf
	ldrb	r1, [r5, #2797]	@ zero_extendqisi2
	add	r4, r4, r0
	ldrb	r3, [r5, #2796]	@ zero_extendqisi2
	mov	r0, r4
	ldrb	r2, [r5, #2769]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, .L816+72
	bl	sprintf
	ldr	r2, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+76
	mov	r0, r4
	ldrh	r3, [r2, #146]
	ldrh	r2, [r2, #148]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+80
	mov	r0, r4
	ldr	r2, [r3, #16]
	ldr	r3, [r3, #20]
	add	r2, r3, r2, lsr #11
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+84
	mov	r0, r4
	ldr	r2, [r3, #24]
	ldr	r3, [r3, #28]
	add	r2, r3, r2, lsr #11
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+88
	mov	r0, r4
	ldr	r2, [r3, #64]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+92
	mov	r0, r4
	ldr	r2, [r3, #68]
	bl	sprintf
	ldr	r3, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+96
	mov	r0, r4
	ldr	r2, [r3, #12]
	umull	r2, r3, r2, r1
	ldr	r1, .L816+100
	lsr	r2, r3, #3
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldrb	r2, [r5]	@ zero_extendqisi2
	mov	r0, r4
	ldrh	r1, [r3, #150]
	str	r1, [sp]
	ldr	r1, .L816+104
	ldr	r3, [r3, #156]
	bl	sprintf
	ldr	r3, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+108
	mov	r0, r4
	ldr	r2, [r3, #556]
	bl	sprintf
	ldr	r3, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+112
	mov	r0, r4
	ldr	r2, [r3, #552]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+116
	mov	r0, r4
	ldr	r2, [r3, #52]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+120
	mov	r0, r4
	ldr	r2, [r3, #60]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+124
	mov	r0, r4
	ldr	r2, [r3, #76]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	ldr	r1, .L816+128
	mov	r0, r4
	ldr	r2, [r3, #8]
	bl	sprintf
	ldr	r1, [r5, #1096]
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r1, #22]
	ldrh	r2, [r1, #16]
	ldrb	r1, [r1, #25]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, .L816+132
	bl	sprintf
	ldr	r1, [r5, #1096]
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r1, #54]
	ldrh	r2, [r1, #48]
	ldrb	r1, [r1, #57]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, .L816+136
	bl	sprintf
	ldr	r1, [r5, #1096]
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r3, [r1, #86]
	ldrh	r2, [r1, #80]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, .L816+140
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r1, [r3, #96]
	ldrh	r2, [r3, #74]
	str	r1, [sp, #8]
	ldrh	r1, [r3, #92]
	str	r1, [sp, #4]
	ldrh	r1, [r3, #88]
	str	r1, [sp]
	ldr	r1, .L816+144
	ldr	r3, [r3, #84]
	bl	sprintf
	ldr	r3, [r5, #2800]
	add	r4, r4, r0
	mov	r0, r4
	ldrh	r1, [r3, #98]
	ldrh	r2, [r3, #72]
	str	r1, [sp, #8]
	ldrh	r1, [r3, #94]
	str	r1, [sp, #4]
	ldrh	r1, [r3, #90]
	str	r1, [sp]
	ldr	r1, .L816+148
	ldr	r3, [r3, #80]
	bl	sprintf
	movw	r3, #2804
	add	r4, r4, r0
	ldrh	r2, [r5, r3]
	mov	r0, r4
	ldr	r1, .L816+152
	bl	sprintf
	ldr	r1, [r5, #2812]
	add	r4, r4, r0
	movw	r3, #2806
	movw	r2, #2808
	ldrh	r3, [r5, r3]
	mov	r0, r4
	str	r1, [sp, #8]
	add	r1, r5, #2816
	ldrh	r1, [r1]
	ldrh	r2, [r5, r2]
	str	r1, [sp, #4]
	movw	r1, #2818
	ldrh	r1, [r5, r1]
	str	r1, [sp]
	ldr	r1, .L816+156
	bl	sprintf
	ldr	r1, [r5, #1096]
	add	r4, r4, r0
	movw	r0, #590
	movw	r3, #586
	ldrh	r0, [r1, r0]
	add	r2, r1, #584
	ldrh	r3, [r1, r3]
	add	r1, r1, #588
	ldrh	r2, [r2]
	str	r0, [sp, #4]
	mov	r0, r4
	ldrh	r1, [r1]
	str	r1, [sp]
	ldr	r1, .L816+160
	bl	sprintf
	ldr	r2, [r5, #1096]
	add	r4, r4, r0
	ldr	r1, .L816+164
	mov	r0, r4
	ldr	r3, [r2, #548]
	str	r3, [sp, #8]
	ldr	r3, [r5, #2800]
	ldr	r3, [r3, #44]
	str	r3, [sp, #4]
	ldr	r3, [r2, #544]
	str	r3, [sp]
	ldr	r3, [r2, #540]
	ldr	r2, [r2, #536]
	bl	sprintf
	add	r0, r4, r0
	sub	r0, r0, r6
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L817:
	.align	2
.L816:
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	.word	-858993459
	.word	.LC73
	.word	.LC74
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LC78
	.word	.LC79
	.word	.LC80
	.word	.LC81
	.word	.LC82
	.word	.LC83
	.word	.LC84
	.word	.LC85
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.fnend
	.size	ftl_print_info_to_buf, .-ftl_print_info_to_buf
	.align	2
	.global	zftl_proc_ftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_proc_ftl_read, %function
zftl_proc_ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_print_info_to_buf
	.fnend
	.size	zftl_proc_ftl_read, .-zftl_proc_ftl_read
	.align	2
	.global	ftl_gc_write_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_gc_write_buf, %function
ftl_gc_write_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, r0
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	ldr	r4, .L821
	orr	r3, r3, #2
	strb	r3, [r0, #2]
	add	r0, r4, #2816
	add	r0, r0, #4
	bl	buf_add_tail
	ldrb	r0, [r4, #2796]	@ zero_extendqisi2
	add	r0, r0, #1
	uxtb	r0, r0
	strb	r0, [r4, #2796]
	pop	{r4, pc}
.L822:
	.align	2
.L821:
	.word	.LANCHOR0
	.fnend
	.size	ftl_gc_write_buf, .-ftl_gc_write_buf
	.align	2
	.global	gc_hook
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_hook, %function
gc_hook:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	gc_hook, .-gc_hook
	.align	2
	.global	vpn_check
	.syntax unified
	.arm
	.fpu softvfp
	.type	vpn_check, %function
vpn_check:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	vpn_check, .-vpn_check
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	gc_add_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_add_sblk, %function
gc_add_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r5, r0
	ldr	r8, .L862
	mov	r4, r1
	mov	r9, r2
	ldr	r7, .L862+4
	ldr	r3, [r8]
	tst	r3, #256
	beq	.L827
	movw	r3, #2818
	ldr	r2, [r7, #1092]
	ldrh	r3, [r7, r3]
	str	r3, [sp, #12]
	ldr	r3, .L862+8
	ldrh	r3, [r3, #52]
	str	r3, [sp, #8]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	mov	r2, r1
	mov	r1, r0
	str	r3, [sp, #4]
	ldr	r3, [r7, #1084]
	add	r3, r3, r0, lsl #2
	ldr	r0, .L862+12
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	lsr	r3, r3, #5
	str	r3, [sp]
	mov	r3, r9
	bl	printk
.L827:
	movw	r3, #1080
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L828
	movw	r2, #543
	ldr	r1, .L862+16
	ldr	r0, .L862+20
	bl	printk
	bl	dump_stack
.L828:
	movw	r3, #1080
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L829
	ldr	r0, [r8]
	ands	r0, r0, #1024
	beq	.L826
	mov	r3, r9
	mov	r2, r4
	mov	r1, r5
	ldr	r0, .L862+24
	bl	printk
.L849:
	mov	r0, #0
	b	.L826
.L829:
	ldr	r2, [r7, #1092]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	ldr	r2, [r7, #1084]
	add	r2, r2, r5, lsl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r6, r2, #224
	bne	.L831
	cmp	r3, #0
	beq	.L849
	movw	r2, #553
	ldr	r1, .L862+16
	ldr	r0, .L862+20
	bl	printk
	bl	dump_stack
	b	.L849
.L831:
	movw	r1, #2824
	ldrh	r1, [r7, r1]
	cmp	r1, r5
	beq	.L849
	ldr	r1, [r7, #1096]
	ldrh	r0, [r1, #48]
	cmp	r0, r5
	beq	.L849
	ldrh	r0, [r1, #16]
	cmp	r0, r5
	beq	.L849
	ldrh	r0, [r1, #80]
	cmp	r0, r5
	beq	.L849
	ldr	r0, .L862+8
	mov	ip, #0
	ldrh	r10, [r0, #52]!
.L832:
	cmp	ip, r10
	bcc	.L833
	cmp	r4, #0
	bne	.L837
	ldr	ip, .L862+28
	mov	r0, r4
	ldrh	lr, [ip, #-4]
	cmp	r5, lr
	beq	.L826
	sub	ip, ip, #2
.L836:
	ldrh	r6, [r0, ip]
	cmp	r5, r6
	bne	.L835
	ldr	r1, [r8]
	ands	r0, r1, #256
	beq	.L826
	stm	sp, {r5, lr}
	lsr	r2, r2, #5
	mov	r1, r5
	ldr	r0, .L862+32
	bl	printk
	b	.L849
.L833:
	ldrh	lr, [r0, #2]!
	cmp	lr, r5
	beq	.L849
	add	ip, ip, #1
	b	.L832
.L835:
	add	r0, r0, #2
	cmp	r0, #16
	bne	.L836
	add	r1, r1, r9, lsl #7
	add	r6, r1, #136
.L838:
	ldr	r1, [r8]
	tst	r1, #256
	beq	.L839
	movw	r1, #2818
	stm	sp, {r3, r10}
	ldrh	r1, [r7, r1]
	lsr	r3, r2, #5
	ldr	r0, .L862+36
	mov	r2, r4
	str	r1, [sp, #8]
	mov	r1, r5
	bl	printk
.L839:
	mov	r3, r6
	add	r1, r6, #128
	movw	r0, #65535
.L842:
	mov	r2, r3
	add	r3, r3, #2
	ldrh	ip, [r2]
	cmp	ip, r0
	bne	.L840
	cmp	r4, #0
	strh	r5, [r2]	@ movhi
	ldreq	r3, [r7, #1096]
	ldrne	r2, [r7, #1096]
	addeq	r9, r3, r9, lsl #1
	ldrhne	r3, [r2, #124]
	ldrheq	r3, [r9, #120]
	addne	r3, r3, #1
	strhne	r3, [r2, #124]	@ movhi
	addeq	r3, r3, #1
	strheq	r3, [r9, #120]	@ movhi
.L861:
	mov	r0, #1
.L826:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L837:
	add	r6, r1, #392
	b	.L838
.L840:
	cmp	r3, r1
	bne	.L842
	b	.L861
.L863:
	.align	2
.L862:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2824
	.word	.LC90
	.word	.LANCHOR1+1448
	.word	.LC0
	.word	.LC91
	.word	.LANCHOR3-3152
	.word	.LC92
	.word	.LC93
	.fnend
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L869
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r0
	ldr	r5, .L869+4
	mov	r7, #1
	sub	r2, r3, #3136
	ldrh	r2, [r2, #-2]
	ldrb	r1, [r3, #-3136]	@ zero_extendqisi2
	add	r5, r5, #4096
	lsr	r6, r0, r2
	ldrb	r0, [r5, #-2943]	@ zero_extendqisi2
	uxth	r4, r6
	rsb	r0, r0, #24
	sub	r0, r0, r2
	lsl	r0, r7, r0
	sub	r0, r0, #1
	and	r0, r0, r6
	bl	__aeabi_uidiv
	uxth	r9, r0
	mov	r3, r8
	mov	r2, r4
	ldr	r1, [r5, #916]
	ldr	r0, .L869+8
	bl	printk
	mov	r1, r7
	mov	r2, #0
	mov	r0, r9
	bl	gc_add_sblk
	ldr	r3, [r5, #916]
	mov	r1, #0
	ldr	r0, .L869+12
	movw	ip, #1096
.L865:
	uxth	r2, r1
	cmp	r3, r2
	bhi	.L867
	cmp	r3, #5
	bhi	.L866
	add	r2, r3, #1
	add	r3, r3, #1088
	str	r2, [r5, #916]
	add	r3, r3, #8
	ldr	r2, .L869+12
	lsl	r3, r3, #1
	strh	r4, [r2, r3]	@ movhi
	b	.L866
.L867:
	add	r2, r2, ip
	add	r1, r1, #1
	lsl	r2, r2, #1
	ldrh	r2, [r0, r2]
	cmp	r4, r2
	bne	.L865
.L866:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L870:
	.align	2
.L869:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC94
	.word	.LANCHOR0+2824
	.fnend
	.size	gc_mark_bad_ppa, .-gc_mark_bad_ppa
	.align	2
	.global	gc_get_src_ppa_from_index
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L872
	ldr	r3, [r3, #-3132]
	ldr	r0, [r3, r0, lsl #2]
	bx	lr
.L873:
	.align	2
.L872:
	.word	.LANCHOR3
	.fnend
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_write_completed, %function
gc_write_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.L875:
	ldr	r7, .L902
	ldrb	fp, [r7, #2772]	@ zero_extendqisi2
	mov	r6, r7
	cmp	fp, #255
	bne	.L888
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L888:
	mov	r2, #48
	add	r10, r6, #1232
	mul	r2, r2, fp
	str	r10, [sp, #4]
	add	r0, r6, r2
	add	r2, r10, r2
	ldrb	r1, [r0, #1232]	@ zero_extendqisi2
	ldrh	r8, [r2, #32]
	strb	r1, [r7, #2772]
	ldr	r1, [r0, #1268]
	cmp	r1, #0
	beq	.L876
	ldr	r3, .L902+4
	mov	r2, #1
	strh	r2, [r3]	@ movhi
	ldr	r2, [r0, #1256]
	add	r0, r6, #4096
	str	r2, [r0, #912]
	ldr	r0, .L902+8
	bl	printk
	mov	r2, #956
	ldr	r1, .L902+12
	ldr	r0, .L902+16
	bl	printk
	bl	dump_stack
.L876:
	ldr	r5, .L902+20
	ldrb	r2, [r5, #-3128]	@ zero_extendqisi2
	mov	r10, r5
	cmp	r2, #3
	bne	.L877
	ldrb	r2, [r7, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L877
	ldr	r2, [r7, #1096]
	ldrb	r4, [r2, #89]	@ zero_extendqisi2
	ldrb	r2, [r5, #-3127]	@ zero_extendqisi2
	cmp	r2, r4
	movhi	r4, #1
	bhi	.L878
	cmp	r4, #2
	movcs	r4, #2
.L878:
	add	r4, r4, r4, lsl #1
.L879:
	mov	r2, #48
	ldr	r1, [r5, #-3124]
	mla	r2, r2, fp, r6
	ldrb	r1, [r1, r8]	@ zero_extendqisi2
	mov	r9, r8
	ldrb	r2, [r2, #1233]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L880
	mov	r2, #976
	ldr	r1, .L902+12
	ldr	r0, .L902+16
	bl	printk
	bl	dump_stack
.L880:
	mov	r2, #48
	mla	r3, r2, fp, r6
	ldrb	r3, [r3, #1277]	@ zero_extendqisi2
	cmp	r3, #3
	beq	.L881
	ldrb	r3, [r5, #-3128]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L881
	ldrb	r3, [r5, #-3120]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L881
	ldrb	r3, [r7, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L881
	ldrb	r3, [r7, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L881
	ldrb	r3, [r5, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L882
	ldrb	r3, [r5, #-3125]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L881
.L882:
	ldr	r2, [r5, #-3124]
	mov	r3, #0
	mov	r1, #48
	movw	r0, #1277
	mov	ip, r3
	add	r8, r2, r8
.L883:
	uxth	r2, r3
	cmp	r4, r2
	bls	.L875
	ldrb	r2, [r8, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	mla	r2, r1, r2, r6
	strb	ip, [r2, r0]
	b	.L883
.L877:
	ldrb	r2, [r5, #-3126]	@ zero_extendqisi2
	cmp	r2, #0
	moveq	r4, #1
	beq	.L879
	ldrb	r2, [r5, #-3125]	@ zero_extendqisi2
	cmp	r2, #0
	moveq	r4, #1
	movne	r4, #2
	b	.L879
.L881:
	ldr	r3, .L902+24
	mvn	r5, #0
	mov	r7, #48
	movw	fp, #1277
	strh	r8, [r3]	@ movhi
.L884:
	sub	r2, r9, r8
	uxth	r2, r2
	cmp	r4, r2
	bls	.L875
	ldr	r2, [r10, #-3124]
	mov	r1, #0
	ldr	r3, [sp, #4]
	ldrb	r0, [r2, r9]	@ zero_extendqisi2
	strb	r5, [r2, r9]
	add	r9, r9, #1
	mla	r2, r7, r0, r6
	add	r0, r0, r0, lsl #1
	strb	r1, [r2, fp]
	add	r0, r3, r0, lsl #4
	bl	zbuf_free
	ldrb	r2, [r6, #2831]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r6, #2831]
	b	.L884
.L903:
	.align	2
.L902:
	.word	.LANCHOR0
	.word	.LANCHOR0+5006
	.word	.LC95
	.word	.LANCHOR1+1460
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR0+4928
	.fnend
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L916
	ldr	r3, [r1, #1096]
	ldrh	r2, [r3, #124]
	cmp	r2, #0
	addne	r3, r3, #392
	movne	r2, #1
	bne	.L906
	ldr	r0, .L916+4
	ldrb	r0, [r0, #-3119]	@ zero_extendqisi2
	add	ip, r3, r0, lsl #1
	ldrh	ip, [ip, #120]
	cmp	ip, #0
	beq	.L911
	add	r3, r3, r0, lsl #7
	add	r3, r3, #136
.L906:
	push	{r4, lr}
	.save {r4, lr}
	add	ip, r3, #128
	movw	lr, #65535
.L910:
	mov	r4, r3
	ldrh	r0, [r3], #2
	cmp	r0, lr
	beq	.L908
	cmp	r2, #0
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	ldreq	r3, .L916+4
	ldrne	r2, [r1, #1096]
	ldrbeq	r2, [r3, #-3119]	@ zero_extendqisi2
	ldreq	r3, [r1, #1096]
	ldrhne	r3, [r2, #124]
	addeq	r3, r3, r2, lsl #1
	subne	r3, r3, #1
	strhne	r3, [r2, #124]	@ movhi
	ldrheq	r2, [r3, #120]
	subeq	r2, r2, #1
	strheq	r2, [r3, #120]	@ movhi
	pop	{r4, pc}
.L908:
	cmp	r3, ip
	bne	.L910
	pop	{r4, pc}
.L911:
	movw	r0, #65535
	bx	lr
.L917:
	.align	2
.L916:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_get_src_blk, .-gc_get_src_blk
	.align	2
	.global	gc_free_temp_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_free_temp_buf, %function
gc_free_temp_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L933
	ldrb	r0, [r5, #2831]	@ zero_extendqisi2
	cmp	r0, #0
	popeq	{r4, r5, r6, pc}
	ldrb	r3, [r5, #2769]	@ zero_extendqisi2
	cmp	r3, #1
	bhi	.L925
	ldr	r2, .L933+4
	add	r3, r5, #4928
	ldrh	r4, [r3]
	movw	ip, #1277
	sub	r3, r2, #3104
	mov	r6, r2
	ldrh	r0, [r3, #-14]
	add	r1, r4, #24
	ldrb	r3, [r2, #-3127]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r3, r1
	movcs	r3, r1
	ldr	r1, [r2, #-3124]
	mov	r2, #48
.L920:
	cmp	r4, r3
	bcc	.L923
.L925:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L923:
	ldrb	r0, [r1, r4]	@ zero_extendqisi2
	cmp	r0, #255
	beq	.L921
	mla	lr, r2, r0, r5
	ldrb	lr, [lr, ip]	@ zero_extendqisi2
	cmp	lr, #0
	bne	.L921
	ldr	r3, .L933+8
	add	r0, r0, r0, lsl #1
	add	r0, r3, r0, lsl #4
	bl	zbuf_free
	ldr	r3, .L933+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L922
	ldr	r3, [r6, #-3124]
	mov	r1, r4
	ldr	r0, .L933+16
	ldrb	r2, [r3, r4]	@ zero_extendqisi2
	bl	printk
.L922:
	ldr	r3, [r6, #-3124]
	mvn	r2, #0
	mov	r0, #1
	strb	r2, [r3, r4]
	ldrb	r3, [r5, #2831]	@ zero_extendqisi2
	add	r3, r3, r2
	strb	r3, [r5, #2831]
	pop	{r4, r5, r6, pc}
.L921:
	add	r4, r4, #1
	b	.L920
.L934:
	.align	2
.L933:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1232
	.word	.LANCHOR2
	.word	.LC96
	.fnend
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	get_ink_scaned_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_ink_scaned_blk, %function
get_ink_scaned_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L938
	movw	ip, #2108
	ldrh	r3, [r1, ip]
	cmp	r3, #0
	addne	r2, r3, #1040
	subne	r3, r3, #1
	addne	r2, r2, #11
	strhne	r3, [r1, ip]	@ movhi
	addne	r2, r1, r2, lsl #1
	ldrhne	r0, [r2, #6]
	movweq	r0, #65535
	bx	lr
.L939:
	.align	2
.L938:
	.word	.LANCHOR0+2824
	.fnend
	.size	get_ink_scaned_blk, .-get_ink_scaned_blk
	.align	2
	.global	print_gc_debug_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	print_gc_debug_info, %function
print_gc_debug_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, lr}
	.save {lr}
	.pad #20
	movw	ip, #2804
	ldr	r0, .L942
	movw	r1, #2824
	movw	r3, #2102
	ldrh	ip, [r0, ip]
	add	r2, r0, r1
	ldrh	r3, [r2, r3]
	ldrh	r1, [r0, r1]
	ldrh	r2, [r2, #2]
	str	ip, [sp, #8]
	ldrb	ip, [r0, #2831]	@ zero_extendqisi2
	str	ip, [sp, #4]
	ldrb	r0, [r0, #2769]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L942+4
	bl	printk
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L943:
	.align	2
.L942:
	.word	.LANCHOR0
	.word	.LC97
	.fnend
	.size	print_gc_debug_info, .-print_gc_debug_info
	.align	2
	.global	_list_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, [r0]
	cmp	r5, #0
	beq	.L950
	ldr	r3, .L952
	movw	ip, #65535
	mov	lr, #6
	ldr	r4, [r3, #1036]
.L946:
	cmp	r1, #0
	bne	.L947
.L949:
	sub	r4, r5, r4
	asr	r3, r4, #1
	ldr	r4, .L952+4
	mul	r4, r4, r3
	uxth	r1, r4
	bl	_list_remove_node
	mvn	r3, #0
	uxth	r0, r4
	strh	r3, [r5]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, pc}
.L947:
	ldrh	r3, [r5]
	cmp	r3, ip
	beq	.L949
	sub	r1, r1, #1
	mla	r5, lr, r3, r4
	uxth	r1, r1
	b	.L946
.L950:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L953:
	.align	2
.L952:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_pop_index_node, .-_list_pop_index_node
	.align	2
	.global	_list_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	_list_get_gc_head_node, %function
_list_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, [r0]
	cmp	r0, #0
	beq	.L959
	ldr	r3, .L961
	movw	r2, #65535
	mov	ip, #6
	ldr	r3, [r3, #1036]
.L956:
	cmp	r1, #0
	beq	.L957
	ldrh	r0, [r0]
	cmp	r0, r2
	bne	.L958
	bx	lr
.L958:
	sub	r1, r1, #1
	mla	r0, ip, r0, r3
	uxth	r1, r1
	b	.L956
.L959:
	movw	r0, #65535
	bx	lr
.L957:
	sub	r0, r0, r3
	asr	r3, r0, #1
	ldr	r0, .L961+4
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L962:
	.align	2
.L961:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_get_gc_head_node, .-_list_get_gc_head_node
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_get_gc_node.part.10, %function
zftl_get_gc_node.part.10:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, r0
	ldr	r0, .L964
	b	_list_get_gc_head_node
.L965:
	.align	2
.L964:
	.word	.LANCHOR3-3116
	.fnend
	.size	zftl_get_gc_node.part.10, .-zftl_get_gc_node.part.10
	.align	2
	.global	gc_search_src_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L1086
	str	r1, [sp, #24]
	ldr	r1, [r5, #1096]
	str	r0, [sp, #16]
	str	r2, [sp, #12]
	add	r3, r1, r0, lsl #1
	ldrh	r3, [r3, #120]
	cmp	r3, #0
	str	r3, [sp, #20]
	movne	r0, r3
	bne	.L966
	ldr	r3, .L1086+4
	ldrh	r2, [r3, #52]
	cmp	r2, #1
	ldrls	r0, .L1086+8
	ldrhls	ip, [sp, #20]
	strhls	ip, [r0, #-8]	@ movhi
	strhls	ip, [r0, #-6]	@ movhi
	strhls	ip, [r0, #-4]	@ movhi
	ldr	r0, [sp, #16]
	cmp	r0, #0
	bne	.L970
	ldr	r10, .L1086+12
	mov	r7, r0
	ldr	fp, .L1086+16
	mov	r4, r0
.L971:
	ldr	r3, [sp, #12]
	add	r8, r3, #1
	uxth	r3, r7
	cmp	r3, r8
	bge	.L976
	ldr	r3, .L1086+20
	mov	r0, r3
	str	r3, [sp, #20]
	ldrh	r9, [r0], #6
	mov	r1, r9
	bl	_list_get_gc_head_node
	add	r2, r9, #1
	ldr	r3, [sp, #20]
	movw	r1, #65535
	uxth	r2, r2
	cmp	r0, r1
	mov	r6, r0
	strh	r2, [r3]	@ movhi
	beq	.L972
	ldr	r3, [r10]
	lsl	r9, r0, #1
	tst	r3, #256
	beq	.L973
	ldr	r3, [r5, #1092]
	mov	r1, r0
	mov	r0, fp
	ldrh	r3, [r3, r9]
	bl	printk
.L973:
	ldr	r3, [r5, #1092]
	ldrh	r2, [r3, r9]
	ldr	r3, .L1086+24
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L974
	mov	r2, #0
	mov	r0, r6
	mov	r1, r2
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L975
	add	r4, r4, #1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	cmp	r4, r3
	bcc	.L975
.L976:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L978
	movw	r3, #2794
	ldrh	r3, [r5, r3]
	cmp	r3, #32
	bls	.L978
	ldr	r9, .L1086+28
	mov	fp, #0
	ldr	r6, .L1086+32
	sub	r10, r9, #12
.L979:
	uxth	r3, fp
	cmp	r8, r3
	ble	.L983
	ldr	r7, .L1086+36
	mov	r0, r10
	ldrh	r3, [r7]
	mov	r1, r3
	str	r3, [sp, #20]
	bl	_list_get_gc_head_node
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	strh	r3, [r7]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L980
	ldr	r3, [r5, #1092]
	lsl	r2, r0, #1
	ldrh	r2, [r3, r2]
	ldrh	r3, [r6]
	cmp	r2, r3
	bcs	.L980
	mov	r2, #0
	mov	r1, r2
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L982
	add	r4, r4, #1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	cmp	r4, r3
	bcc	.L982
.L983:
	ldr	r3, [sp, #12]
	cmp	r4, r3
	bcs	.L985
	ldr	r0, .L1086+40
	ldrh	r1, [r9, #-8]
	ldrh	r2, [r6]
	sub	r3, r0, #3104
	ldrh	ip, [r3, #-14]
	ldrb	r3, [r0, #-3127]	@ zero_extendqisi2
	mul	r3, r3, ip
	sub	r3, r3, r1, lsr #2
	cmp	r2, r3
	addlt	r2, r2, r1, lsr #3
	strhlt	r2, [r6]	@ movhi
.L978:
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L986
	ldrh	r6, [sp, #12]
	cmp	r4, r6
	bcs	.L986
	mov	r9, #0
	movw	r8, #65535
.L991:
	ldr	r10, .L1086+44
	ldrh	r7, [r10]
	mov	r0, r7
	add	r7, r7, #1
	bl	zftl_get_gc_node.part.10
	cmp	r0, r8
	strh	r7, [r10]	@ movhi
	beq	.L987
	mov	r2, #0
	mov	r1, r2
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L988
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r6, r4
	bhi	.L988
.L989:
	ldr	r3, .L1086+28
	movw	r2, #2818
	ldrh	r1, [r3, #-8]
.L1085:
	ldrh	r3, [r5, r2]
	cmp	r3, r1, lsr #1
	bls	.L986
	sub	r3, r3, r1, lsr #3
.L1082:
	strh	r3, [r5, r2]	@ movhi
	b	.L986
.L974:
	ldr	r3, .L1086+8
	mov	r2, #0
	strh	r2, [r3, #-6]	@ movhi
	b	.L976
.L972:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L976
.L975:
	add	r7, r7, #1
	b	.L971
.L980:
	mov	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L983
.L982:
	add	fp, fp, #1
	b	.L979
.L985:
	ldrh	r3, [r6]
	ldrh	r2, [r9, #-8]
	cmp	r3, r2
	subhi	r3, r3, r2, lsr #3
	strhhi	r3, [r6]	@ movhi
	b	.L978
.L987:
	mov	r3, #0
	strh	r3, [r10]	@ movhi
.L990:
	cmp	r4, r6
	bcs	.L989
	ldr	r1, .L1086+28
	movw	r2, #2818
	ldrh	r3, [r5, r2]
	ldrh	r1, [r1, #-8]
	cmp	r3, r1
	addcc	r3, r3, r1, lsr #3
	bcc	.L1082
.L986:
	ldr	r3, .L1086+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1021
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #16]
	str	r3, [sp]
	mov	r3, r4
	ldr	r0, .L1086+48
	bl	printk
.L1021:
	mov	r0, r4
.L966:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L988:
	add	r9, r9, #1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L991
	b	.L990
.L970:
	ldr	r4, .L1086+40
	ldrb	r7, [r4, #-3127]	@ zero_extendqisi2
	sub	r4, r4, #3104
	ldrh	r0, [r4, #-14]
	smulbb	r7, r7, r0
	ldr	r0, [sp, #12]
	cmp	r0, #1
	uxth	r7, r7
	bne	.L992
	cmp	r2, #0
	beq	.L992
	ldrh	r0, [r1, #80]
	movw	r1, #65535
	cmp	r0, r1
	movwne	r1, #2102
	ldrhne	r1, [r3, r1]
	subne	r7, r7, r1
	mov	r1, #8
	uxthne	r7, r7
	str	r1, [sp, #12]
.L992:
	mov	r6, #0
	movw	r1, #2180
	strh	r6, [r3, r1]	@ movhi
	ldr	r3, [sp, #24]
	ldr	r1, .L1086+28
	ands	r3, r3, #1
	strh	r6, [r1, #-6]	@ movhi
	mov	r8, r1
	str	r3, [sp, #28]
	ldreq	r6, [sp, #28]
	moveq	r4, r6
	beq	.L993
	movw	r3, #2790
	ldrh	r1, [r1, #-4]
	ldrh	r3, [r5, r3]
	cmp	r3, r1, lsr #2
	bhi	.L994
	movw	r1, #2792
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	movcs	r4, r6
	bcs	.L995
.L994:
	cmp	r2, #1
	bls	.L996
.L998:
	mov	r4, #0
.L997:
	ldr	r9, .L1086+52
	mov	r6, #0
	mov	r8, #64
	movw	fp, #65535
.L1001:
	ldr	r2, .L1086+44
	ldrh	r3, [r2]
	str	r2, [sp, #36]
	mov	r0, r3
	str	r3, [sp, #32]
	bl	zftl_get_gc_node.part.10
	cmp	r0, fp
	mov	r10, r0
	ldr	r2, [sp, #36]
	beq	.L999
	ldr	r3, [sp, #32]
	mov	r1, #0
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r2, [sp, #16]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1000
	ldr	r3, [r5, #1092]
	lsl	r10, r10, #1
	add	r4, r4, #1
	ldr	r2, [sp, #12]
	uxth	r4, r4
	ldrh	r3, [r3, r10]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r7, r6
	movcs	r3, #0
	movcc	r3, #1
	cmp	r4, r2
	orrcs	r3, r3, #1
	cmp	r3, #0
	bne	.L995
	ldr	r2, .L1086+56
	ldrh	r3, [r9]
	ldrh	r2, [r2]
	cmp	r3, r2, lsl #1
	ble	.L1000
.L995:
	movw	r3, #2792
	ldrh	r2, [r5, r3]
	ldr	r3, .L1086+28
	ldrh	r3, [r3, #-4]
	cmp	r2, r3, lsr #2
	bhi	.L1022
	movw	r3, #2790
	ldrh	r3, [r5, r3]
	add	r3, r3, #8
	cmp	r2, r3
	ble	.L993
.L1022:
	cmp	r7, r6
	bls	.L993
	ldrh	r3, [sp, #16]
	mov	r8, #64
	str	r3, [sp, #32]
.L1004:
	ldr	fp, .L1086+20
	mov	r0, fp
	ldrh	r9, [r0], #6
	mov	r1, r9
	bl	_list_get_gc_head_node
	movw	r3, #65535
	mov	r10, r0
	cmp	r0, r3
	beq	.L1002
	add	r9, r9, #1
	ldr	r2, [sp, #32]
	mov	r1, #0
	strh	r9, [fp]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1003
	ldr	r3, [r5, #1092]
	lsl	r10, r10, #1
	add	r4, r4, #1
	ldr	r2, [sp, #12]
	uxth	r4, r4
	ldrh	r3, [r3, r10]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r7, r6
	movcs	r3, #0
	movcc	r3, #1
	cmp	r4, r2
	orrcs	r3, r3, #1
	cmp	r3, #0
	beq	.L1003
.L993:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1005
	movw	r3, #2794
	ldrh	r3, [r5, r3]
	cmp	r3, #32
	movls	r3, #0
	movhi	r3, #1
	cmp	r6, r7
	movcs	r3, #0
	cmp	r3, #0
	beq	.L1005
	ldr	r10, .L1086+40
	mov	r5, #64
	sub	fp, r10, #3088
	sub	r3, fp, #12
	str	r3, [sp, #32]
.L1011:
	ldr	r8, .L1086+8
	ldr	r0, [sp, #32]
	ldrh	r9, [r8, #-4]
	sub	r2, r8, #4
	str	r2, [sp, #36]
	mov	r1, r9
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r2, [sp, #36]
	cmp	r0, r3
	beq	.L1006
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	strh	r9, [r8, #-4]	@ movhi
	cmp	r3, #1
	bne	.L1007
	ldrh	r2, [r8, #-14]
	ldrb	r3, [r10, #-3127]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldrh	r2, [fp, #-8]
	sub	r3, r3, r2, lsr #3
	ldr	r2, .L1086+60
	strh	r3, [r2]	@ movhi
.L1007:
	ldr	r9, .L1086
	lsl	r8, r0, #1
	ldr	r3, [r9, #1092]
	ldrh	r2, [r3, r8]
	ldr	r3, .L1086+60
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L1008
	ldr	r2, [sp, #16]
	mov	r1, #0
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1009
	ldr	r3, [sp, #20]
	add	r4, r4, #1
	ldr	r2, [sp, #12]
	uxth	r4, r4
	add	r0, r3, #1
	uxth	r3, r0
	str	r3, [sp, #20]
	ldr	r3, [r9, #1092]
	ldrh	r3, [r3, r8]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r7, r6
	movcs	r3, #0
	movcc	r3, #1
	cmp	r4, r2
	orrcs	r3, r3, #1
	cmp	r3, #0
	beq	.L1009
.L1010:
	ldr	r3, [sp, #12]
	ldr	r0, .L1086+28
	ldr	r2, .L1086
	cmp	r4, r3
	add	lr, r0, #3088
	bcc	.L1012
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1013
	movw	r3, #2794
	ldrh	r1, [r2, r3]
	ldrh	r3, [r0, #-2]
	cmp	r1, r3
	bls	.L1013
.L1012:
	ldr	r3, .L1086+8
	movw	ip, #2808
	ldrh	r0, [r0, #-8]
	ldrh	r1, [r2, ip]
	ldrh	r5, [r3, #-14]
	ldrb	r3, [lr, #-3127]	@ zero_extendqisi2
	lsr	r0, r0, #3
	mul	r3, r3, r5
	sub	r3, r3, r0
	cmp	r1, r3
	addlt	r1, r1, r0
	strhlt	r1, [r2, ip]	@ movhi
.L1005:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L986
	ldrh	r9, [sp, #12]
	cmp	r6, r7
	cmpcc	r4, r9
	bcs	.L986
	ldr	r5, .L1086
	mov	r8, #64
.L1019:
	ldr	fp, .L1086+20
	mov	r0, fp
	ldrh	r10, [r0], #6
	mov	r1, r10
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1015
	add	r10, r10, #1
	ldr	r2, [r5, #1092]
	ldr	r3, .L1086+64
	strh	r10, [fp]	@ movhi
	lsl	r10, r0, #1
	ldrh	r1, [r2, r10]
	ldrh	r2, [r3]
	cmp	r1, r2
	bcs	.L1016
	ldrh	r2, [fp, #18]
	ldrh	r3, [r3, #-14]
	cmp	r3, r2, lsr #1
	bls	.L1017
.L1016:
	ldr	r2, [sp, #16]
	mov	r1, #0
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1018
	ldr	r3, [r5, #1092]
	add	r4, r4, #1
	uxth	r4, r4
	ldrh	r3, [r3, r10]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r7, r6
	cmpcs	r9, r4
	bhi	.L1018
.L1017:
	ldr	r3, .L1086+28
	cmp	r4, r9
	movw	r2, #2806
	ldrhcc	r1, [r3, #-8]
	bcc	.L1085
	ldrh	r0, [r3, #-8]
	ldr	r3, .L1086+40
	ldrh	r1, [r5, r2]
	ldrb	r3, [r3, #-3127]	@ zero_extendqisi2
	mul	r3, r0, r3
	sub	r3, r3, #32
	cmp	r1, r3
	addlt	r1, r1, r0, lsr #3
	strhlt	r1, [r5, r2]	@ movhi
	b	.L986
.L996:
	lsr	r3, r3, #2
	mov	r0, #0
	strh	r3, [r4, #-8]	@ movhi
	bl	zftl_get_gc_node.part.10
	movw	r2, #65535
	cmp	r0, r2
	beq	.L998
	ldr	r1, [r5, #1092]
	lsl	r3, r0, #1
	ldrh	r2, [r8, #-8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #2
	bcs	.L998
	mov	r3, #1
	ldr	r2, [sp, #16]
	mov	r1, #0
	strh	r3, [r4, #-8]	@ movhi
	bl	gc_add_sblk
	adds	r4, r0, #0
	movne	r4, #1
	b	.L997
.L999:
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L995
.L1000:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1001
	b	.L995
.L1002:
	cmp	r9, #64
	movhi	r3, #0
	strhhi	r3, [fp]	@ movhi
	b	.L993
.L1003:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1004
	b	.L993
.L1008:
	ldr	r3, .L1086+8
	mov	r2, #0
	strh	r2, [r3, #-4]	@ movhi
	b	.L1010
.L1006:
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L1010
.L1009:
	sub	r5, r5, #1
	uxth	r5, r5
	cmp	r5, #0
	bne	.L1011
	b	.L1010
.L1013:
	ldrh	r0, [r0, #-8]
	movw	ip, #2808
	ldrb	r1, [lr, #-3127]	@ zero_extendqisi2
	ldrh	r3, [r2, ip]
	mul	r1, r0, r1
	cmp	r3, r1
	subgt	r3, r3, r0, lsr #3
	strhgt	r3, [r2, ip]	@ movhi
	b	.L986
.L1015:
	cmp	r10, #64
	movhi	r3, #0
	strhhi	r3, [fp]	@ movhi
	b	.L1017
.L1018:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1019
	b	.L1017
.L1087:
	.align	2
.L1086:
	.word	.LANCHOR0
	.word	.LANCHOR0+2824
	.word	.LANCHOR3-3104
	.word	.LANCHOR2
	.word	.LC98
	.word	.LANCHOR3-3110
	.word	.LANCHOR0+2818
	.word	.LANCHOR3-3088
	.word	.LANCHOR0+2816
	.word	.LANCHOR3-3108
	.word	.LANCHOR3
	.word	.LANCHOR3-3112
	.word	.LC99
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+2790
	.word	.LANCHOR0+2808
	.word	.LANCHOR0+2806
	.fnend
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_get_gc_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_get_gc_node, %function
zftl_get_gc_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r1, #5
	mov	r3, r0
	moveq	r1, r0
	ldreq	r0, .L1092
	beq	.L1091
	cmp	r1, #2
	movne	r1, r3
	ldrne	r0, .L1092+4
	bne	.L1091
	b	zftl_get_gc_node.part.10
.L1091:
	b	_list_get_gc_head_node
.L1093:
	.align	2
.L1092:
	.word	.LANCHOR3-3100
	.word	.LANCHOR3-3104
	.fnend
	.size	zftl_get_gc_node, .-zftl_get_gc_node
	.align	2
	.global	zftl_insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_insert_free_list, %function
zftl_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1098
	mov	r1, r0
	ldr	r3, [r2, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	addeq	r2, r2, #2784
	ldreq	r0, .L1098+4
	beq	.L1097
	cmp	r3, #16
	ldr	r0, .L1098+8
	ldreq	r2, .L1098+12
	ldrne	r2, .L1098+16
	subeq	r0, r0, #12
	subne	r0, r0, #8
.L1097:
	b	_insert_free_list
.L1099:
	.align	2
.L1098:
	.word	.LANCHOR0
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2786
	.word	.LANCHOR0+2788
	.fnend
	.size	zftl_insert_free_list, .-zftl_insert_free_list
	.align	2
	.global	zftl_insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_insert_data_list, %function
zftl_insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1105
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	bne	.L1101
	ldr	r2, .L1105+4
	ldr	r0, .L1105+8
.L1104:
	b	_insert_data_list
.L1101:
	cmp	r3, #96
	ldreq	r2, .L1105+12
	ldreq	r0, .L1105+16
	beq	.L1104
.L1102:
	cmp	r3, #160
	bxne	lr
	ldr	r2, .L1105+20
	ldr	r0, .L1105+24
	b	.L1104
.L1106:
	.align	2
.L1105:
	.word	.LANCHOR0
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3116
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3100
	.fnend
	.size	zftl_insert_data_list, .-zftl_insert_data_list
	.align	2
	.global	zftl_gc_get_free_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_gc_get_free_sblk, %function
zftl_gc_get_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	movw	r2, #65535
	ldr	r8, .L1126
	ldr	r3, [r8, #1096]
	add	r3, r3, #588
	ldrh	r4, [r3]
	clz	r3, r0
	lsr	r3, r3, #5
	cmp	r4, r2
	moveq	r3, #0
	cmp	r3, #0
	beq	.L1108
	mov	r1, r4
	ldr	r0, .L1126+4
	bl	printk
	ldr	r3, [r8, #1096]
	mvn	r2, #0
	add	r3, r3, #588
	strh	r2, [r3]	@ movhi
.L1109:
	mov	r0, r4
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1108:
	movw	r3, #2786
	mov	r7, r1
	ldrh	r1, [r8, r3]
	movw	r3, #2788
	ldrh	r3, [r8, r3]
	mov	r6, r0
	mov	r5, r8
	ldr	r0, .L1126+8
	cmp	r1, r3
	bcc	.L1110
	add	r2, r8, #2784
	ldrh	r2, [r2]
	cmp	r3, #0
	cmpne	r2, r1
	bls	.L1111
.L1110:
	cmp	r6, #0
	ldr	r2, .L1126+12
	rsbne	r1, r1, r1, lsl #3
	lsreq	r1, r3, #2
	ubfxne	r1, r1, #3, #16
	sub	r0, r0, #8
.L1125:
	bl	_list_pop_index_node
	uxth	r4, r0
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1116
	movw	r3, #2788
	mov	r2, r7
	ldrh	r3, [r5, r3]
	mov	r1, r4
	ldr	r0, .L1126+16
	str	r3, [sp, #4]
	ldr	r3, .L1126+20
	ldrh	r3, [r3]
	str	r3, [sp]
	ldr	r3, .L1126+24
	ldr	r3, [r3, #-3088]
	bl	printk
.L1116:
	cmp	r6, #0
	beq	.L1109
	ldr	r3, .L1126+28
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1109
	ldr	ip, [r5, #1092]
	lsl	r0, r4, #1
	ldr	r1, [r5, #1084]
	lsl	r3, r4, #2
	ldrh	r0, [ip, r0]
	add	r2, r1, r3
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldrh	r3, [r1, r3]
	ldr	r0, .L1126+32
	ubfx	r3, r3, #0, #11
	str	r3, [sp, #4]
	ldr	r3, [r1, r4, lsl #2]
	mov	r1, r4
	ubfx	r3, r3, #11, #8
	str	r3, [sp]
	ubfx	r3, r2, #3, #2
	lsr	r2, r2, #5
	bl	printk
	b	.L1109
.L1111:
	cmp	r6, #0
	ldr	r2, .L1126+36
	lsrne	r1, r1, #3
	moveq	r1, r6
	sub	r0, r0, #12
	b	.L1125
.L1127:
	.align	2
.L1126:
	.word	.LANCHOR0
	.word	.LC100
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2788
	.word	.LC101
	.word	.LANCHOR0+2784
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC102
	.word	.LANCHOR0+2786
	.fnend
	.size	zftl_gc_get_free_sblk, .-zftl_gc_get_free_sblk
	.align	2
	.global	zftl_get_free_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_get_free_sblk, %function
zftl_get_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #5
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r7, r1
	ldr	r5, .L1141
	bne	.L1129
	movw	r3, #2786
	ldr	r0, .L1141+4
	ldrh	r1, [r5, r3]
	movw	r3, #2788
	ldrh	ip, [r5, r3]
	cmp	r1, ip
	bcc	.L1130
	add	r3, r5, #2784
	ldrh	r3, [r3]
	cmp	ip, #0
	cmpne	r3, r1
	movhi	r1, #1
	movls	r1, #0
	ldrls	r2, .L1141+8
	subls	r0, r0, #12
	bls	.L1140
.L1130:
	ldr	r2, .L1141+12
	lsr	r1, ip, #1
	sub	r0, r0, #8
.L1140:
	bl	_list_pop_index_node
	uxth	r4, r0
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1134
	movw	r3, #2788
	mov	r2, r7
	ldrh	r3, [r5, r3]
	mov	r1, r4
	ldr	r0, .L1141+16
	str	r3, [sp, #4]
	ldr	r3, .L1141+20
	ldrh	r3, [r3]
	str	r3, [sp]
	ldr	r3, .L1141+24
	ldr	r3, [r3, #-3088]
	bl	printk
	b	.L1134
.L1129:
	ldr	r3, [r5, #1096]
	movw	r8, #590
	ldrh	r4, [r3, r8]
	movw	r3, #65535
	cmp	r1, #1
	cmpne	r4, r3
	beq	.L1133
	mov	r1, r4
	ldr	r0, .L1141+28
	bl	printk
	ldr	r3, [r5, #1096]
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
.L1134:
	mov	r0, r4
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1133:
	add	r3, r5, #2784
	movw	r2, #2788
	ldrh	r3, [r3]
	mov	r6, r0
	ldrh	r2, [r5, r2]
	cmp	r3, r2
	bcc	.L1135
	movw	r1, #2786
	ldrh	r1, [r5, r1]
	cmp	r2, #0
	cmpne	r1, r3
	bls	.L1136
.L1135:
	bl	get_ink_scaned_blk
	movw	r3, #65535
	mov	r4, r0
	cmp	r0, r3
	bne	.L1134
	cmp	r7, #1
	ldr	r2, .L1141+12
	movweq	r3, #2788
	ldr	r0, .L1141+32
	ldrheq	r6, [r5, r3]
	lsreq	r6, r6, #1
	mov	r1, r6
	b	.L1140
.L1136:
	cmp	r7, #1
	ldr	r2, .L1141+20
	lsreq	r6, r3, #1
	ldr	r0, .L1141+36
	mov	r1, r6
	b	.L1140
.L1142:
	.align	2
.L1141:
	.word	.LANCHOR0
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2786
	.word	.LANCHOR0+2788
	.word	.LC101
	.word	.LANCHOR0+2784
	.word	.LANCHOR3
	.word	.LC103
	.word	.LANCHOR3-3080
	.word	.LANCHOR3-3088
	.fnend
	.size	zftl_get_free_sblk, .-zftl_get_free_sblk
	.align	2
	.global	zftl_remove_data_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_remove_data_node, %function
zftl_remove_data_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1148
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	bne	.L1144
	ldr	r2, .L1148+4
	ldr	r0, .L1148+8
.L1147:
	b	_list_remove_node
.L1144:
	cmp	r3, #96
	ldreq	r2, .L1148+12
	ldreq	r0, .L1148+16
	beq	.L1147
.L1145:
	cmp	r3, #160
	bxne	lr
	ldr	r2, .L1148+20
	ldr	r0, .L1148+24
	b	.L1147
.L1149:
	.align	2
.L1148:
	.word	.LANCHOR0
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3116
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3100
	.fnend
	.size	zftl_remove_data_node, .-zftl_remove_data_node
	.align	2
	.global	zftl_remove_free_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_remove_free_node, %function
zftl_remove_free_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1154
	mov	r1, r0
	ldr	r3, [r2, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	addeq	r2, r2, #2784
	ldreq	r0, .L1154+4
	beq	.L1153
	cmp	r3, #16
	ldr	r0, .L1154+8
	ldreq	r2, .L1154+12
	ldrne	r2, .L1154+16
	subeq	r0, r0, #12
	subne	r0, r0, #8
.L1153:
	b	_list_remove_node
.L1155:
	.align	2
.L1154:
	.word	.LANCHOR0
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2786
	.word	.LANCHOR0+2788
	.fnend
	.size	zftl_remove_free_node, .-zftl_remove_free_node
	.align	2
	.global	zftl_list_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_list_update_data_list, %function
zftl_list_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1161
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	bne	.L1157
	ldr	r2, .L1161+4
	ldr	r0, .L1161+8
.L1160:
	b	_list_update_data_list
.L1157:
	cmp	r3, #96
	ldreq	r2, .L1161+12
	ldreq	r0, .L1161+16
	beq	.L1160
.L1158:
	cmp	r3, #160
	bxne	lr
	ldr	r2, .L1161+20
	ldr	r0, .L1161+24
	b	.L1160
.L1162:
	.align	2
.L1161:
	.word	.LANCHOR0
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3116
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3100
	.fnend
	.size	zftl_list_update_data_list, .-zftl_list_update_data_list
	.align	2
	.global	print_list_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	print_list_info, %function
print_list_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r4, r0
	ldrh	r2, [r1]
	.pad #36
	sub	sp, sp, #36
	ldr	r1, [r0]
	ldr	r0, .L1168
	bl	printk
	ldr	r4, [r4]
	cmp	r4, #0
	beq	.L1163
	ldr	r6, .L1168+4
	mov	r5, #0
	ldr	r7, .L1168+8
	ldr	r8, .L1168+12
.L1166:
	ldr	r2, [r6, #1036]
	ldr	r9, [r6, #1092]
	ldr	r1, [r6, #1084]
	sub	r2, r4, r2
	ldrh	r3, [r4]
	asr	r2, r2, #1
	mul	r2, r7, r2
	uxth	r2, r2
	lsl	lr, r2, #1
	lsl	r0, r2, #2
	ldrh	lr, [r9, lr]
	add	ip, r1, r0
	str	lr, [sp, #24]
	ldrh	r0, [r1, r0]
	ubfx	r0, r0, #0, #11
	str	r0, [sp, #20]
	mov	r0, r8
	ldr	r1, [r1, r2, lsl #2]
	ubfx	r1, r1, #11, #8
	str	r1, [sp, #16]
	ldrb	r1, [ip, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #3, #2
	str	r1, [sp, #12]
	ldrb	r1, [ip, #2]	@ zero_extendqisi2
	lsr	r1, r1, #5
	str	r1, [sp, #8]
	ldrh	r1, [r4, #4]
	str	r1, [sp, #4]
	ldrh	r1, [r4, #2]
	str	r1, [sp]
	mov	r1, r5
	bl	printk
	ldrh	r4, [r4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1163
	ldr	r3, [r6, #1036]
	mov	r2, #6
	add	r5, r5, #1
	uxth	r5, r5
	mla	r4, r2, r4, r3
	ldr	r3, .L1168+16
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L1166
.L1163:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1169:
	.align	2
.L1168:
	.word	.LC104
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC105
	.word	.LANCHOR3-3076
	.fnend
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_all_list_info, %function
dump_all_list_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L1172
	ldr	r4, .L1172+4
	sub	r6, r5, #3088
	sub	r7, r5, #3072
	mov	r0, r6
	sub	r5, r5, #3104
	add	r1, r4, #2784
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r7, #12
	add	r1, r1, #2
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r7, #8
	add	r1, r1, #4
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r5, #12
	add	r1, r1, #6
	bl	print_list_info
	add	r1, r4, #2784
	mov	r0, r5
	add	r1, r1, #8
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r6, #12
	add	r1, r1, #10
	pop	{r4, r5, r6, r7, r8, lr}
	b	print_list_info
.L1173:
	.align	2
.L1172:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1179
	ldr	r3, [r3, #2800]
	ldr	r2, [r3, #16]
	cmp	r2, #2048
	ldrhi	r1, [r3, #20]
	addhi	r1, r1, r2, lsr #11
	ubfxhi	r2, r2, #0, #11
	strhi	r2, [r3, #16]
	ldr	r2, [r3, #24]
	strhi	r1, [r3, #20]
	cmp	r2, #2048
	ldrhi	r1, [r3, #28]
	addhi	r1, r1, r2, lsr #11
	ubfxhi	r2, r2, #0, #11
	strhi	r2, [r3, #24]
	ldr	r2, [r3, #32]
	strhi	r1, [r3, #28]
	cmp	r2, #1024
	ldrhi	r1, [r3, #36]
	addhi	r1, r1, r2, lsr #10
	ubfxhi	r2, r2, #0, #10
	strhi	r2, [r3, #32]
	ldr	r2, [r3, #40]
	strhi	r1, [r3, #36]
	cmp	r2, #1024
	ldrhi	r1, [r3, #44]
	addhi	r1, r1, r2, lsr #10
	ubfxhi	r2, r2, #0, #10
	strhi	r2, [r3, #40]
	strhi	r1, [r3, #44]
	bx	lr
.L1180:
	.align	2
.L1179:
	.word	.LANCHOR0
	.fnend
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.global	__aeabi_idiv
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, #0
	ldr	r9, .L1189
	mov	r10, r0
	mov	r6, r1
	mov	r7, r5
	ldr	r8, .L1189+4
	ldr	r3, [r9, #1084]
	add	r3, r3, r0, lsl #2
	ldrb	r2, [r3, #3]	@ zero_extendqisi2
	mov	r3, r8
.L1182:
	ldrb	r1, [r8, #-3127]	@ zero_extendqisi2
	cmp	r7, r1
	blt	.L1185
	add	r6, r6, r5, lsl #1
	mov	r2, r5
	mvn	r0, #0
.L1186:
	ldrb	r1, [r3, #-3127]	@ zero_extendqisi2
	cmp	r2, r1
	blt	.L1187
	mov	r0, r5
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1185:
	asr	r1, r2, r7
	tst	r1, #1
	bne	.L1183
	ldrb	r4, [r8, #-3136]	@ zero_extendqisi2
	mov	r0, r7
	stm	sp, {r2, r3}
	lsl	fp, r5, #1
	add	r5, r5, #1
	mov	r1, r4
	bl	__aeabi_idiv
	ldr	ip, .L1189+8
	smulbb	r4, r4, r10
	ldrb	r1, [r9, #1153]	@ zero_extendqisi2
	ldm	sp, {r2, r3}
	ldrh	ip, [ip]
	rsb	r1, r1, #24
	sub	r1, r1, ip
	add	r0, r4, r0, lsl r1
	uxth	r0, r0
	strh	r0, [r6, fp]	@ movhi
	ldrb	r1, [r8, #-3136]	@ zero_extendqisi2
	cmp	r1, #1
	subhi	r1, r1, #1
	andhi	r1, r1, r7
	addhi	r0, r0, r1
	strhhi	r0, [r6, fp]	@ movhi
.L1183:
	add	r7, r7, #1
	b	.L1182
.L1187:
	strh	r0, [r6], #2	@ movhi
	add	r2, r2, #1
	b	.L1186
.L1190:
	.align	2
.L1189:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3138
	.fnend
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_erase_phy_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1197
	ldr	r3, .L1197+4
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r1
	sub	r1, r2, #3136
	sub	r7, r2, #3072
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
	ldrh	r4, [r1, #-2]
	rsb	r3, r3, #24
	sub	r3, r3, r4
	mvn	r4, #0
	asr	r5, r0, r3
	bic	r4, r0, r4, lsl r3
	ldrb	r3, [r2, #-3126]	@ zero_extendqisi2
	sxth	r4, r4
	uxtb	r5, r5
	cmp	r3, #0
	beq	.L1192
	ldrb	r3, [r2, #-3125]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1192
	ldrh	r2, [r7, #-2]
	clz	r1, r6
	lsr	r1, r1, #5
	mov	r0, r5
	mul	r2, r4, r2
	bl	flash_erase_block_en
.L1192:
	ldrh	r2, [r7, #-2]
	uxtb	r1, r6
	mov	r0, r5
	mul	r2, r4, r2
	pop	{r4, r5, r6, r7, r8, lr}
	b	flash_erase_block_en
.L1198:
	.align	2
.L1197:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	ftl_erase_phy_blk, .-ftl_erase_phy_blk
	.align	2
	.global	ftl_erase_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_erase_sblk, %function
ftl_erase_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	fp, r0, #2
	ldr	r4, .L1224
	mov	r8, r0
	mov	r7, r1
	mov	r6, #0
	ldr	r9, .L1224+4
	.pad #84
	sub	sp, sp, #84
	ldr	r3, [r4, #1084]
	add	r3, r3, fp
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	str	r3, [sp, #4]
.L1200:
	ldr	r3, .L1224+4
	ldrb	r3, [r3, #-3072]	@ zero_extendqisi2
	cmp	r6, r3
	bge	.L1211
	ldr	r3, .L1224+4
	mov	r5, #0
	ldrb	r2, [r3, #-3136]	@ zero_extendqisi2
	sub	r10, r2, #1
	mul	r3, r2, r8
	mul	ip, r2, r6
	str	r3, [sp, #8]
	ldr	r3, .L1224+8
	ldrh	r3, [r3]
	str	r3, [sp, #12]
	mov	r3, r5
	b	.L1212
.L1202:
	ldr	lr, [sp, #4]
	add	r1, ip, r3
	asr	r1, lr, r1
	tst	r1, #1
	bne	.L1201
	ldr	r0, [sp, #8]
	add	r1, sp, #80
	add	lr, r1, r5, lsl #2
	and	r1, r3, r10
	add	r5, r5, #1
	add	r1, r1, r0
	ldr	r0, [sp, #12]
	mul	r1, r0, r1
	str	r1, [lr, #-64]
.L1201:
	add	r3, r3, #1
.L1212:
	cmp	r3, r2
	blt	.L1202
	cmp	r2, #4
	bne	.L1203
	uxtb	r3, r7
	mov	r10, #0
	str	r3, [sp, #8]
	uxtb	r3, r6
.L1204:
	cmp	r10, r5
	bne	.L1205
.L1206:
	add	r6, r6, #1
	b	.L1200
.L1205:
	add	r2, sp, #16
	mov	r0, r3
	ldr	r2, [r2, r10, lsl #2]
	add	r10, r10, #1
	ldr	r1, [sp, #8]
	str	r3, [sp, #12]
	bl	flash_erase_block_en
	ldr	r3, [sp, #12]
	b	.L1204
.L1203:
	cmp	r5, #2
	bne	.L1207
	ldrb	r3, [r9, #-3126]	@ zero_extendqisi2
	uxtb	r5, r6
	cmp	r3, #0
	beq	.L1208
	ldrb	r3, [r9, #-3125]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1208
	clz	r1, r7
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #16]
	lsr	r1, r1, #5
	mov	r0, r5
	bl	flash_erase_duplane_block
.L1208:
	ldr	r3, [sp, #20]
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	mov	r0, r5
	bl	flash_erase_duplane_block
	b	.L1206
.L1207:
	cmp	r5, #1
	bne	.L1206
	ldrb	r3, [r9, #-3126]	@ zero_extendqisi2
	uxtb	r5, r6
	cmp	r3, #0
	beq	.L1210
	ldrb	r3, [r9, #-3125]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1210
	clz	r1, r7
	ldr	r2, [sp, #16]
	lsr	r1, r1, #5
	mov	r0, r5
	bl	flash_erase_block_en
.L1210:
	ldr	r2, [sp, #16]
	uxtb	r1, r7
	mov	r0, r5
	bl	flash_erase_block_en
	b	.L1206
.L1211:
	cmp	r7, #0
	bne	.L1213
	ldr	r2, [r4, #1084]
	ldrh	r3, [r2, fp]
	add	r1, r3, #1
	bfi	r3, r1, #0, #11
	strh	r3, [r2, fp]	@ movhi
	ldr	r3, [r4, #2800]
	ldr	r2, [r3, #84]
	ldrh	r0, [r3, #96]
	add	r2, r2, #1
	str	r2, [r3, #84]
	ldr	r2, [r4, #1084]
	ldrh	r2, [r2, fp]
	ubfx	r2, r2, #0, #11
	uxth	r1, r2
	cmp	r0, r1
	strhlt	r2, [r3, #96]	@ movhi
.L1215:
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1213:
	ldr	r1, [r4, #1084]
	ldr	r3, [r1, r8, lsl #2]
	ubfx	r2, r3, #11, #8
	add	r2, r2, #1
	bfi	r3, r2, #11, #8
	str	r3, [r1, r8, lsl #2]
	ldr	r3, [r4, #2800]
	ldr	r2, [r3, #80]
	ldrh	r1, [r3, #98]
	add	r2, r2, #1
	str	r2, [r3, #80]
	ldr	r2, [r4, #1084]
	ldr	r2, [r2, r8, lsl #2]
	ubfx	r2, r2, #11, #8
	cmp	r1, r2
	strhcc	r2, [r3, #98]	@ movhi
	b	.L1215
.L1225:
	.align	2
.L1224:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3074
	.fnend
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1236
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r3
	ldr	r2, [r3, #2800]
	ldrh	r1, [r2, #136]
	cmp	r1, #63
	movhi	r3, #0
	strhhi	r3, [r2, #136]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #0
	bne	.L1228
	movw	r2, #1359
	ldr	r1, .L1236+4
	ldr	r0, .L1236+8
	bl	printk
	bl	dump_stack
.L1228:
	ldr	r3, [r4, #2800]
	movw	lr, #65535
	mov	ip, #0
.L1232:
	ldrh	r2, [r3, #136]
	add	r1, r3, r2, lsl #1
	add	r1, r1, #158
.L1229:
	cmp	r2, #63
	strhgt	ip, [r3, #136]	@ movhi
	bgt	.L1232
.L1231:
	ldrh	r0, [r1, #2]!
	cmp	r0, lr
	addeq	r2, r2, #1
	beq	.L1229
.L1234:
	add	r1, r3, r2, lsl #1
	mvn	ip, #0
	strh	ip, [r1, #160]	@ movhi
	strh	r2, [r3, #136]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, ip
	strh	r2, [r3, #112]	@ movhi
	pop	{r4, pc}
.L1237:
	.align	2
.L1236:
	.word	.LANCHOR0
	.word	.LANCHOR1+1479
	.word	.LC0
	.fnend
	.size	ftl_alloc_sys_blk, .-ftl_alloc_sys_blk
	.align	2
	.global	ftl_free_sys_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_sys_blk, %function
ftl_free_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1247
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	mov	r4, r3
	ldr	r2, [r3, #2800]
	ldrh	r1, [r2, #138]
	cmp	r1, #63
	movhi	r3, #0
	strhhi	r3, [r2, #138]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #63
	bls	.L1240
	movw	r2, #1386
	ldr	r1, .L1247+4
	ldr	r0, .L1247+8
	bl	printk
	bl	dump_stack
.L1240:
	ldr	r3, [r4, #2800]
	movw	ip, #65535
	mov	r0, #0
.L1244:
	ldrh	r2, [r3, #138]
	add	r1, r3, r2, lsl #1
	add	r1, r1, #158
.L1241:
	cmp	r2, #63
	strhgt	r0, [r3, #138]	@ movhi
	bgt	.L1244
.L1243:
	ldrh	lr, [r1, #2]!
	cmp	lr, ip
	bne	.L1242
	add	r1, r3, r2, lsl #1
	strh	r5, [r1, #160]	@ movhi
	strh	r2, [r3, #138]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, #1
	strh	r2, [r3, #112]	@ movhi
	pop	{r4, r5, r6, pc}
.L1242:
	add	r2, r2, #1
	b	.L1241
.L1248:
	.align	2
.L1247:
	.word	.LANCHOR0
	.word	.LANCHOR1+1497
	.word	.LC0
	.fnend
	.size	ftl_free_sys_blk, .-ftl_free_sys_blk
	.align	2
	.global	ftl_info_data_recovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_info_data_recovery, %function
ftl_info_data_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #65535
	ldrh	r6, [r0]
	cmp	r6, r3
	popeq	{r4, r5, r6, r7, r8, pc}
	ldr	r4, .L1259
	lsl	r8, r6, #2
	ldr	r5, [r4, #1084]
	add	r7, r5, r8
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	tst	r3, #224
	popne	{r4, r5, r6, r7, r8, pc}
	ldrb	r2, [r0, #4]	@ zero_extendqisi2
	mov	r0, r6
	bfi	r3, r2, #5, #3
	strb	r3, [r7, #2]
	bl	zftl_remove_free_node
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	ldr	r2, [r4, #2800]
	tst	r3, #8
	ldrhne	r3, [r2, #116]
	subne	r3, r3, #1
	strhne	r3, [r2, #116]	@ movhi
	bne	.L1254
	tst	r3, #24
	ldrheq	r3, [r2, #114]
	ldrhne	r3, [r2, #118]
	subeq	r3, r3, #1
	subne	r3, r3, #1
	strheq	r3, [r2, #114]	@ movhi
	strhne	r3, [r2, #118]	@ movhi
.L1254:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	bne	.L1256
	ldr	r3, [r5, r6, lsl #2]
	ubfx	r2, r3, #11, #8
	add	r2, r2, #1
	bfi	r3, r2, #11, #8
	str	r3, [r5, r6, lsl #2]
	ldr	r2, [r4, #2800]
	ldrh	r3, [r2, #120]
	sub	r3, r3, #1
	strh	r3, [r2, #120]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L1256:
	ldrh	r2, [r5, r8]
	cmp	r3, #64
	add	r1, r2, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r5, r8]	@ movhi
	bne	.L1257
	ldr	r2, [r4, #2800]
	ldrh	r3, [r2, #122]
	sub	r3, r3, #1
	strh	r3, [r2, #122]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L1257:
	cmp	r3, #96
	ldreq	r2, [r4, #2800]
	ldrheq	r3, [r2, #124]
	subeq	r3, r3, #1
	strheq	r3, [r2, #124]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L1260:
	.align	2
.L1259:
	.word	.LANCHOR0
	.fnend
	.size	ftl_info_data_recovery, .-ftl_info_data_recovery
	.align	2
	.global	ftl_get_ppa_from_index
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_ppa_from_index, %function
ftl_get_ppa_from_index:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1266
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r5, [r3, #1096]
	ldr	r3, .L1266+4
	sub	r2, r3, #3088
	ldrb	r3, [r3, #-3127]	@ zero_extendqisi2
	ldrh	r2, [r2, #-8]
	mul	r1, r3, r2
	cmp	r0, r1
	smulbbge	r3, r3, r2
	addlt	r5, r5, #16
	addge	r5, r5, #48
	ldrb	r6, [r5, #9]	@ zero_extendqisi2
	subge	r4, r0, r3
	uxthge	r4, r4
	mov	r1, r6
	mov	r0, r4
	bl	__aeabi_idiv
	smulbb	r6, r0, r6
	movw	r3, #65535
	mov	r7, r0
	sub	r4, r4, r6
	uxth	r4, r4
	add	r4, r5, r4, lsl #1
	ldrh	r4, [r4, #16]
	cmp	r4, r3
	bne	.L1264
	movw	r2, #1945
	ldr	r1, .L1266+8
	ldr	r0, .L1266+12
	bl	printk
	bl	dump_stack
.L1264:
	ldr	r3, .L1266+16
	ldrh	r0, [r3, #-2]
	mla	r0, r4, r0, r7
	pop	{r4, r5, r6, r7, r8, pc}
.L1267:
	.align	2
.L1266:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1514
	.word	.LC0
	.word	.LANCHOR3-3072
	.fnend
	.size	ftl_get_ppa_from_index, .-ftl_get_ppa_from_index
	.align	2
	.global	lpa_hash_get_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	lpa_hash_get_ppa, %function
lpa_hash_get_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1276
	uxtb	r3, r0
	lsl	r3, r3, #1
	sub	r1, r2, #3056
	ldr	ip, [r2, #-2552]
	sub	r1, r1, #14
	ldrh	r3, [r1, r3]
	ldr	r1, [r2, #-2556]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1275
	mvn	r0, #0
	bx	lr
.L1270:
	lsl	r3, r3, #1
	ldrh	r3, [ip, r3]
	cmp	r3, r2
	bne	.L1271
	mvn	r0, #0
	ldr	pc, [sp], #4
.L1275:
	str	lr, [sp, #-4]!
	.save {lr}
.L1271:
	ldr	lr, [r1, r3, lsl #2]
	cmp	r0, lr
	bne	.L1270
	mov	r0, r3
	ldr	lr, [sp], #4
	b	ftl_get_ppa_from_index
.L1277:
	.align	2
.L1276:
	.word	.LANCHOR3
	.fnend
	.size	lpa_hash_get_ppa, .-lpa_hash_get_ppa
	.align	2
	.global	ftl_get_new_free_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_new_free_page, %function
ftl_get_new_free_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r3, #65535
	ldrh	r2, [r0]
	mov	r4, r0
	cmp	r2, r3
	bne	.L1279
	movw	r2, #2088
	ldr	r1, .L1287
	ldr	r0, .L1287+4
	bl	printk
	bl	dump_stack
.L1279:
	ldr	r3, .L1287+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-8]
	cmp	r2, r3
	bne	.L1280
	movw	r2, #2089
	ldr	r1, .L1287
	ldr	r0, .L1287+4
	bl	printk
	bl	dump_stack
.L1280:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L1281
	movw	r2, #2090
	ldr	r1, .L1287
	ldr	r0, .L1287+4
	bl	printk
	bl	dump_stack
.L1281:
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	movw	r2, #65535
	mov	r1, #0
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #16]
	ldr	r3, .L1287+12
	ldrb	ip, [r3, #-3127]	@ zero_extendqisi2
.L1282:
	cmp	r0, r2
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	beq	.L1284
	ldr	r1, .L1287+16
	add	r3, r3, #1
	uxtb	r3, r3
	ldrh	r2, [r4, #2]
	ldrh	r1, [r1, #-2]
	cmp	ip, r3
	strb	r3, [r4, #5]
	addeq	r3, r2, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r4, #5]
	mul	r0, r0, r1
	ldrh	r1, [r4, #6]
	sub	r1, r1, #1
	strh	r1, [r4, #6]	@ movhi
	orr	r0, r0, r2
	ldrh	r1, [r4, #10]
	add	r1, r1, #1
	strh	r1, [r4, #10]	@ movhi
	pop	{r4, pc}
.L1284:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, ip
	strb	r3, [r4, #5]
	ldrheq	r3, [r4, #2]
	strbeq	r1, [r4, #5]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #16]
	b	.L1282
.L1288:
	.align	2
.L1287:
	.word	.LANCHOR1+1537
	.word	.LC0
	.word	.LANCHOR3-3088
	.word	.LANCHOR3
	.word	.LANCHOR3-3072
	.fnend
	.size	ftl_get_new_free_page, .-ftl_get_new_free_page
	.align	2
	.global	ftl_ext_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_ext_alloc_new_blk, %function
ftl_ext_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	bl	ftl_alloc_sys_blk
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r4, r0
	cmp	r3, r2
	bls	.L1290
	movw	r2, #2125
	ldr	r1, .L1292
	ldr	r0, .L1292+4
	bl	printk
	bl	dump_stack
.L1290:
	ldr	r5, .L1292+8
	mov	r1, #0
	mov	r0, r4
	bl	ftl_erase_phy_blk
	ldr	r3, [r5, #2800]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sys_blk
	ldr	r3, [r5, #2800]
	mov	r0, #0
	strh	r4, [r3, #130]	@ movhi
	strh	r0, [r3, #140]	@ movhi
	pop	{r4, r5, r6, pc}
.L1293:
	.align	2
.L1292:
	.word	.LANCHOR1+1559
	.word	.LC0
	.word	.LANCHOR0
	.fnend
	.size	ftl_ext_alloc_new_blk, .-ftl_ext_alloc_new_blk
	.align	2
	.global	ftl_total_vpn_update
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_total_vpn_update, %function
ftl_total_vpn_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1308
	ldrh	r3, [r2, #-4]
	cmp	r3, #4
	cmpls	r0, #0
	bne	.L1295
	add	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bx	lr
.L1295:
	ldr	r3, .L1308+4
	mov	r0, #0
	movw	ip, #1080
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	strh	r0, [r2, #-4]	@ movhi
	movw	r6, #65535
	ldrh	lr, [r3, ip]
	mov	ip, r0
	ldr	r2, [r3, #1084]
	ldr	r1, [r3, #1092]
	add	lr, r2, lr, lsl #2
	sub	r1, r1, #2
.L1297:
	cmp	r2, lr
	bne	.L1300
	ldr	r4, [r3, #1096]
	ldr	r3, [r3, #2800]
	str	ip, [r4, #524]
	str	r0, [r4, #528]
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	popeq	{r4, r5, r6, pc}
	bl	__aeabi_uidiv
	str	r0, [r4, #532]
	pop	{r4, r5, r6, pc}
.L1300:
	ldrh	r4, [r1, #2]!
	cmp	r4, r6
	beq	.L1298
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	and	r5, r5, #224
	cmp	r5, #160
	addeq	r0, r0, r4
	addne	ip, ip, r4
.L1298:
	add	r2, r2, #4
	b	.L1297
.L1309:
	.align	2
.L1308:
	.word	.LANCHOR3-2544
	.word	.LANCHOR0
	.fnend
	.size	ftl_total_vpn_update, .-ftl_total_vpn_update
	.align	2
	.global	ftl_debug_info_fill
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_debug_info_fill, %function
ftl_debug_info_fill:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1317
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L1314
	sub	r3, r0, #2
	cmp	r2, #0
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	streq	r3, [r1]
	streq	r3, [r1, #4]
	beq	.L1314
	ldr	r3, .L1317+4
	mov	r0, r2
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r1
	str	r3, [r1]
	mov	r1, #1024
	bl	js_hash
	str	r0, [r4, #4]
	mov	r0, #0
	pop	{r4, pc}
.L1314:
	mov	r0, #0
	bx	lr
.L1318:
	.align	2
.L1317:
	.word	.LANCHOR3
	.word	1212240712
	.fnend
	.size	ftl_debug_info_fill, .-ftl_debug_info_fill
	.align	2
	.global	ftl_vpn_update
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_vpn_update, %function
ftl_vpn_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	bl	zftl_list_update_data_list
	lsl	r4, r4, #1
	ldr	r3, .L1323
	ldr	r2, [r3, #1092]
	ldrh	r2, [r2, r4]
	cmp	r2, #0
	moveq	r0, #1
	movne	r0, #0
	streq	r0, [r3, #2812]
	pop	{r4, pc}
.L1324:
	.align	2
.L1323:
	.word	.LANCHOR0
	.fnend
	.size	ftl_vpn_update, .-ftl_vpn_update
	.align	2
	.global	ftl_vpn_decrement
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_vpn_decrement, %function
ftl_vpn_decrement:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r0, r3
	mov	r5, r0
	beq	.L1326
	ldr	r1, .L1335
	lsl	r3, r0, #1
	ldr	r2, [r1, #1092]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strhne	r4, [r2, r3]	@ movhi
	bne	.L1326
	ldr	r3, [r1, #1084]
	mov	r2, r4
	mov	r1, r0
	add	r3, r3, r0, lsl #2
	ldr	r0, .L1335+4
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	lsr	r3, r3, #5
	bl	printk
.L1332:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1326:
	ldr	r3, .L1335+8
	ldrh	r0, [r3, #-4]
	mov	r4, r3
	cmp	r5, r0
	beq	.L1332
	movw	r2, #65535
	cmp	r0, r2
	strheq	r5, [r3, #-4]	@ movhi
	beq	.L1332
	bl	ftl_vpn_update
	add	r3, r4, #608
	adds	r0, r0, #0
	ldrh	r2, [r3]
	movne	r0, #1
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, #7
	movhi	r2, #0
	strh	r2, [r3]	@ movhi
	ldrh	r3, [r3]
	ldrh	r2, [r4, #-4]
	strh	r5, [r4, #-4]	@ movhi
	add	r3, r4, r3, lsl #1
	strh	r2, [r3, #-2]	@ movhi
	pop	{r4, r5, r6, pc}
.L1336:
	.align	2
.L1335:
	.word	.LANCHOR0
	.word	.LC106
	.word	.LANCHOR3-3152
	.fnend
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1349
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	uxtb	r7, r0
	movw	r8, #65535
	sub	lr, r3, #3056
	lsl	r7, r7, #1
	sub	ip, lr, #14
	ldr	r6, [r3, #-2556]
	ldrh	ip, [ip, r7]
	mov	r5, r8
	ldr	r9, [r3, #-2552]
.L1338:
	cmp	ip, r5
	beq	.L1342
	ldr	r4, [r6, ip, lsl #2]
	add	r10, r6, ip, lsl #2
	cmp	r0, r4
	lsl	r4, ip, #1
	bne	.L1339
	mvn	ip, #0
	cmp	r8, r5
	str	ip, [r10]
	lslne	r8, r8, #1
	ldr	ip, [r3, #-2552]
	ldrh	r5, [ip, r4]
	subeq	ip, lr, #14
	strheq	r5, [ip, r7]	@ movhi
	strhne	r5, [ip, r8]	@ movhi
	mvn	r5, #0
	ldr	ip, [r3, #-2552]
	strh	r5, [ip, r4]	@ movhi
.L1342:
	ldr	ip, [r3, #-2556]
	cmn	r1, #1
	str	r0, [ip, r2, lsl #2]
	sub	ip, lr, #14
	ldrh	lr, [ip, r7]
	ldr	r0, [r3, #-2552]
	strh	r2, [ip, r7]	@ movhi
	lsl	r2, r2, #1
	strh	lr, [r0, r2]	@ movhi
	beq	.L1344
	ldr	r0, .L1349+4
	ldr	r2, .L1349+8
	ldrb	ip, [r0, #1153]	@ zero_extendqisi2
	mvn	r0, #0
	ldrh	r2, [r2, #-2]
	rsb	ip, ip, #24
	sub	ip, ip, r2
	lsr	r2, r1, r2
	ldrb	r1, [r3, #-3136]	@ zero_extendqisi2
	bic	r0, r2, r0, lsl ip
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L1344:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1339:
	mov	r8, ip
	ldrh	ip, [r9, r4]
	b	.L1338
.L1350:
	.align	2
.L1349:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3136
	.fnend
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r3, #1
	ldr	r7, .L1359
	ldr	ip, .L1359+4
	ldrb	r4, [r7, #1153]	@ zero_extendqisi2
	ldrb	r5, [ip, #-3136]!	@ zero_extendqisi2
	rsb	r1, r4, #24
	lsl	r4, r3, r4
	sub	r4, r4, #1
	and	r4, r4, r0, lsr r1
	uxtb	r4, r4
	smulbb	r2, r4, r5
	uxtb	r9, r2
	ldrh	r2, [ip, #-2]
	sub	r1, r1, r2
	lsl	r3, r3, r1
	mov	r1, r5
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	uxth	r10, r0
	bl	__aeabi_uidiv
	ldr	r3, .L1359+8
	cmp	r5, #1
	subhi	r2, r5, #1
	mov	r6, r0
	andhi	r2, r2, r10
	uxth	r8, r0
	ldr	r3, [r3]
	addhi	r2, r9, r2
	uxtbhi	r9, r2
	tst	r3, #16384
	beq	.L1353
	uxth	r3, r0
	str	r10, [sp]
	mov	r2, r9
	ldr	r0, .L1359+12
	mov	r1, r4
	bl	printk
.L1353:
	movw	r3, #1080
	ldrh	r3, [r7, r3]
	cmp	r3, r8
	bls	.L1351
	ldr	r3, [r7, #1084]
	uxth	r6, r6
	add	r6, r3, r6, lsl #2
	mov	r3, #1
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	orr	r2, r2, r3, lsl r9
	strb	r2, [r6, #3]
.L1351:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1360:
	.align	2
.L1359:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC107
	.fnend
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
	.align	2
	.global	gc_free_bad_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_free_bad_sblk, %function
gc_free_bad_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r6, .L1382
	ldr	r3, [r6, #916]
	cmp	r3, #0
	beq	.L1378
	ldr	r8, .L1382+4
	mov	r7, #0
	str	r0, [sp, #4]
.L1363:
	ldrb	r2, [r8, #-3127]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1373
.L1378:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1373:
	ldrb	r9, [r8, #-3136]	@ zero_extendqisi2
	uxth	r0, r7
	ldr	r10, .L1382+8
	ldr	fp, .L1382+12
	mov	r1, r9
	bl	__aeabi_idiv
	ldrb	r3, [r10, #1153]	@ zero_extendqisi2
	uxth	r5, r9
	ldrh	r2, [sp, #4]
	cmp	r9, #1
	ldrh	r4, [fp], #2
	mov	r9, #0
	rsb	r3, r3, #24
	str	r10, [sp]
	ldr	r10, .L1382+16
	sub	r3, r3, r4
	smulbb	r4, r2, r5
	subhi	r5, r5, #1
	andhi	r5, r5, r7
	add	r4, r4, r0, lsl r3
	uxth	r4, r4
	addhi	r4, r4, r5
	uxthhi	r4, r4
.L1365:
	ldr	r1, [r6, #916]
	uxth	r5, r9
	mov	r3, r5
	cmp	r1, r5
	addls	r7, r7, #1
	bls	.L1363
.L1372:
	add	r3, r3, #1088
	add	r3, r3, #8
	lsl	r3, r3, #1
	ldrh	r3, [r10, r3]
	cmp	r3, r4
	bne	.L1366
	mov	r1, r4
	ldr	r0, .L1382+20
	bl	printk
	ldrb	r3, [r8, #-2542]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1367
	ldrb	r3, [r8, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1368
.L1367:
	ldr	r3, [sp]
	ldr	r2, .L1382+24
	ldr	r3, [r3, #2800]
	ldr	r3, [r3, #156]
	cmp	r3, r2
	beq	.L1369
.L1368:
	ldrh	r0, [fp, #-2]
	lsl	r0, r4, r0
	bl	ftl_mask_bad_block
.L1369:
	ldr	r3, [r6, #916]
	movw	r0, #1097
	movw	ip, #1096
.L1370:
	cmp	r5, r3
	bcc	.L1371
	sub	r3, r3, #1
	str	r3, [r6, #916]
.L1366:
	add	r9, r9, #1
	b	.L1365
.L1371:
	add	r1, r5, r0
	lsl	r1, r1, #1
	ldrh	lr, [r10, r1]
	add	r1, r5, ip
	lsl	r1, r1, #1
	add	r5, r5, #1
	uxth	r5, r5
	strh	lr, [r10, r1]	@ movhi
	b	.L1370
.L1383:
	.align	2
.L1382:
	.word	.LANCHOR0+4096
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3138
	.word	.LANCHOR0+2824
	.word	.LC108
	.word	1145785929
	.fnend
	.size	gc_free_bad_sblk, .-gc_free_bad_sblk
	.align	2
	.global	ftl_free_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	r10, r0, #2
	ldr	r5, .L1407
	.pad #20
	sub	sp, sp, #20
	mov	r7, r0
	ldr	r6, [r5, #1084]
	add	r8, r6, r10
	ldrb	r4, [r8, #2]	@ zero_extendqisi2
	tst	r4, #8
	lsr	r3, r4, #5
	str	r3, [sp, #4]
	beq	.L1385
	ldr	r1, [r5, #2800]
	ldrh	r3, [r6, r10]
	ldr	fp, [r6, r0, lsl #2]
	ldrh	r2, [r1, #74]
	ubfx	r3, r3, #0, #11
	str	r3, [sp, #12]
	ubfx	fp, fp, #11, #8
	add	r2, r3, r2
	uxth	r3, r2
	ldrh	r2, [r1, #72]
	add	r1, r5, #1088
	ldrh	r9, [r1]
	mov	r0, r3
	add	r2, r2, fp
	str	r3, [sp]
	uxth	r2, r2
	mov	r1, r9
	str	r2, [sp, #8]
	bl	__aeabi_uidiv
	ldr	ip, .L1407+4
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	ldrh	r1, [ip, #-12]
	uxtah	r0, r2, r0
	cmp	r0, r1
	ble	.L1386
	movw	r2, #2794
	ldrh	r0, [r5, r2]
	movw	r2, #2786
	ldrh	r2, [r5, r2]
	add	r0, r0, r2
	sub	r2, ip, #560
	ldrh	r1, [r2, #-2]
	add	r1, r1, #8
	cmp	r0, r1
	bge	.L1387
.L1392:
	mov	r2, #2
	bfi	r4, r2, #3, #2
	b	.L1403
.L1387:
	movw	r1, #2790
	add	r0, r5, #2784
	ldrh	r0, [r0]
	ldrh	r1, [r5, r1]
	ldrh	r2, [r2, #-4]
	add	r1, r1, r0
	movw	r0, #2792
	ldrh	r0, [r5, r0]
	add	r2, r2, #8
	add	r1, r1, r0
	cmp	r1, r2
.L1406:
	bge	.L1392
	b	.L1404
.L1386:
	ldr	r1, [sp]
	mla	r2, r2, r9, r1
	ldrh	r1, [ip, #-10]
	cmp	r2, r1
	ble	.L1388
	movw	r2, #2790
	ldrh	r1, [r5, r2]
	add	r2, r5, #2784
	ldrh	r2, [r2]
	add	r1, r1, r2
	movw	r2, #2792
	ldrh	r2, [r5, r2]
	add	r1, r1, r2
	sub	r2, ip, #560
	ldrh	r0, [r2, #-4]
	add	ip, r0, #8
	cmp	r1, ip
	bge	.L1390
.L1404:
	bfc	r4, #3, #2
.L1403:
	strb	r4, [r8, #2]
.L1388:
	ldrb	r2, [r8, #2]	@ zero_extendqisi2
	ands	r2, r2, #24
	bne	.L1393
	mul	r9, r9, fp
	ldrh	r2, [r6, r10]
	lsr	fp, fp, #3
	add	r9, r9, r9, lsl #1
	add	r3, r3, r9, asr #2
	bfi	r2, r3, #0, #11
	strh	r2, [r6, r10]	@ movhi
	ldr	r3, [r6, r7, lsl #2]
	bfi	r3, fp, #11, #8
	str	r3, [r6, r7, lsl #2]
.L1394:
	mov	r0, r7
	bl	zftl_remove_data_node
	ldr	r3, .L1407
	mov	r0, #0
	ldr	r2, [r3, #1084]
	mov	r4, r3
	add	r10, r2, r10
	ldrb	r2, [r10, #2]	@ zero_extendqisi2
	bfc	r2, #5, #3
	strb	r2, [r10, #2]
	lsl	r2, r7, #1
	ldr	r1, [r3, #1092]
	strh	r0, [r1, r2]	@ movhi
	ldr	r2, [sp, #4]
	add	r2, r2, #6
	and	r2, r2, #7
	cmp	r2, #4
	bhi	.L1397
	mov	r0, r7
	bl	gc_free_bad_sblk
.L1397:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1398
	ldr	r3, [r4, #1096]
	movw	r2, #586
	ldrh	r1, [r3, r2]
	cmp	r1, r7
	bne	.L1398
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #590
	ldrh	r0, [r3, r2]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L1398
	strh	r7, [r3, r2]	@ movhi
	mov	r1, r7
	ldr	r0, .L1407+8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1390:
	movw	ip, #2794
	movw	lr, #2786
	ldrh	ip, [r5, ip]
	ldrh	lr, [r5, lr]
	ldrh	r2, [r2, #-2]
	add	ip, ip, lr
	add	r2, r2, #8
	cmp	ip, r2
	blt	.L1392
	add	r0, r0, #24
	cmp	r1, r0
	b	.L1406
.L1393:
	cmp	r2, #16
	bne	.L1394
	mov	r0, r3
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_idiv
	add	r0, r0, r0, lsl #1
	ldr	r2, [r6, r7, lsl #2]
	add	fp, fp, r0, asr #2
	bfi	r2, fp, #11, #8
	str	r2, [r6, r7, lsl #2]
	ldr	r3, [sp]
	ldrh	r2, [r6, r10]
	asr	r3, r3, #5
	bfi	r2, r3, #0, #11
	strh	r2, [r6, r10]	@ movhi
	b	.L1394
.L1385:
	tst	r4, #24
	bne	.L1394
	movw	r3, #2788
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1394
	movw	r3, #2786
	ldrh	r0, [r5, r3]
	cmp	r0, #15
	bhi	.L1394
	movw	r3, #2790
	add	r2, r5, #2784
	ldrh	r2, [r2]
	ldrh	r3, [r5, r3]
	ldr	ip, .L1407+12
	add	r3, r3, r2
	movw	r2, #2792
	ldrh	r2, [r5, r2]
	ldrh	r1, [ip, #-4]
	add	r3, r3, r2
	add	r1, r1, #16
	cmp	r3, r1
	ble	.L1394
	movw	r3, #2794
	ldrh	r2, [r5, r3]
	ldrh	r3, [ip, #-2]
	add	r2, r2, r0
	add	r3, r3, #8
	cmp	r2, r3
	bge	.L1394
	mov	r3, #2
	add	r5, r5, #1088
	bfi	r4, r3, #3, #2
	ldrh	r1, [r5]
	strb	r4, [r8, #2]
	ldrh	r4, [r6, r10]
	ubfx	r4, r4, #0, #11
	mov	r0, r4
	asr	r4, r4, #5
	bl	__aeabi_idiv
	ldr	r3, [r6, r7, lsl #2]
	add	r0, r0, r0, lsl #1
	ubfx	r2, r3, #11, #8
	add	r0, r2, r0, asr #2
	bfi	r3, r0, #11, #8
	str	r3, [r6, r7, lsl #2]
	ldrh	r3, [r6, r10]
	bfi	r3, r4, #0, #11
	strh	r3, [r6, r10]	@ movhi
	b	.L1394
.L1398:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	zftl_insert_free_list
.L1408:
	.align	2
.L1407:
	.word	.LANCHOR0
	.word	.LANCHOR3-2528
	.word	.LC109
	.word	.LANCHOR3-3088
	.fnend
	.size	ftl_free_sblk, .-ftl_free_sblk
	.align	2
	.global	gc_free_src_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_free_src_blk, %function
gc_free_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r8, .L1447
	ldr	r7, .L1447+4
	mov	r6, r8
.L1410:
	ldrh	r2, [r7, #52]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1424
	mov	r3, #0
	strh	r3, [r7, #52]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1424:
	uxth	r3, r5
	add	r3, r7, r3, lsl #1
	ldrh	r4, [r3, #54]
	ldr	r3, [r8, #1092]
	lsl	r9, r4, #1
	ldrh	r2, [r3, r9]
	cmp	r2, #0
	beq	.L1411
	mov	r1, r4
	ldr	r0, .L1447+8
	bl	printk
.L1411:
	ldr	r3, [r6, #1092]
	mov	r2, #0
	strh	r2, [r3, r9]	@ movhi
	ldr	r3, [r6, #1092]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	bne	.L1412
	ldr	r3, .L1447+12
	ldr	r9, [r6, #1084]
	ldr	r3, [r3]
	add	r9, r9, r4, lsl #2
	tst	r3, #256
	beq	.L1413
	ldrb	r2, [r9, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L1447+16
	lsr	r2, r2, #5
	bl	printk
.L1413:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	and	r3, r3, #192
	cmp	r3, #0
	cmpne	r2, #224
	bne	.L1414
	movw	r2, #1363
	ldr	r1, .L1447+20
	ldr	r0, .L1447+24
	bl	printk
	bl	dump_stack
.L1414:
	mov	r0, r4
	bl	ftl_free_sblk
	ldr	r3, [r6, #1096]
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	beq	.L1415
	add	r0, r3, #392
	mov	r2, #0
.L1417:
	ldrh	ip, [r0], #2
	cmp	r4, ip
	bne	.L1416
	add	r2, r2, #196
	mvn	r0, #0
	lsl	r2, r2, #1
	add	r1, r1, r0
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #124]	@ movhi
.L1415:
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	beq	.L1418
	add	r0, r3, #136
	mov	r2, #0
.L1420:
	ldrh	ip, [r0], #2
	cmp	r4, ip
	bne	.L1419
	mvn	r0, #0
	add	r2, r3, r2, lsl #1
	add	r1, r1, r0
	strh	r0, [r2, #136]	@ movhi
	strh	r1, [r3, #120]	@ movhi
.L1418:
	ldrh	r1, [r3, #122]
	cmp	r1, #0
	beq	.L1421
	add	r0, r3, #264
	mov	r2, #0
.L1423:
	ldrh	ip, [r0], #2
	cmp	r4, ip
	bne	.L1422
	add	r2, r2, #132
	mvn	r0, #0
	lsl	r2, r2, #1
	add	r1, r1, r0
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #122]	@ movhi
.L1421:
	add	r5, r5, #1
	b	.L1410
.L1416:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1417
	b	.L1415
.L1419:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1420
	b	.L1418
.L1422:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1423
	b	.L1421
.L1412:
	mov	r1, #1
	mov	r0, r4
	bl	gc_add_sblk
	b	.L1421
.L1448:
	.align	2
.L1447:
	.word	.LANCHOR0
	.word	.LANCHOR0+2824
	.word	.LC110
	.word	.LANCHOR2
	.word	.LC111
	.word	.LANCHOR1+1581
	.word	.LC0
	.fnend
	.size	gc_free_src_blk, .-gc_free_src_blk
	.align	2
	.global	print_ftl_debug_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	print_ftl_debug_info, %function
print_ftl_debug_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	.pad #32
	sub	sp, sp, #32
	ldr	r4, .L1451
	ldr	r1, [r4, #2800]
	add	r3, r4, #2784
	ldr	r0, [r4, #1096]
	ldr	ip, [r4, #2780]
	ldrh	r2, [r1, #146]
	ldrh	r3, [r3]
	ldrh	r1, [r1, #148]
	str	ip, [sp, #28]
	ldr	ip, [r0, #528]
	str	ip, [sp, #24]
	ldr	r0, [r0, #524]
	str	r0, [sp, #20]
	movw	r0, #2794
	ldrh	r0, [r4, r0]
	str	r0, [sp, #16]
	movw	r0, #2792
	ldrh	r0, [r4, r0]
	str	r0, [sp, #12]
	movw	r0, #2790
	ldrh	r0, [r4, r0]
	str	r0, [sp, #8]
	movw	r0, #2788
	ldrh	r0, [r4, r0]
	str	r0, [sp, #4]
	movw	r0, #2786
	ldrh	r0, [r4, r0]
	str	r0, [sp]
	ldr	r0, .L1451+4
	bl	printk
	ldr	r1, [r4, #2800]
	ldrb	r3, [r4, #2797]	@ zero_extendqisi2
	ldr	r0, .L1451+8
	ldr	r2, [r1, #64]
	str	r2, [sp, #4]
	ldr	r2, [r1, #8]
	str	r2, [sp]
	ldr	r2, [r1, #28]
	ldr	r1, [r1, #20]
	bl	printk
	ldr	r1, [r4, #2800]
	ldr	r0, .L1451+12
	ldr	r3, [r1, #16]
	ldr	r2, [r1, #60]
	ldr	r1, [r1, #52]
	lsr	r3, r3, #11
	bl	printk
	ldr	r2, [r4, #2800]
	ldrh	r0, [r2, #98]
	ldrh	r3, [r2, #88]
	ldrh	r1, [r2, #74]
	str	r0, [sp, #24]
	ldrh	r0, [r2, #94]
	str	r0, [sp, #20]
	ldrh	r0, [r2, #90]
	str	r0, [sp, #16]
	ldr	r0, [r2, #80]
	str	r0, [sp, #12]
	ldrh	r0, [r2, #72]
	str	r0, [sp, #8]
	ldrh	r0, [r2, #96]
	str	r0, [sp, #4]
	ldrh	r0, [r2, #92]
	str	r0, [sp]
	ldr	r0, .L1451+16
	ldr	r2, [r2, #84]
	bl	printk
	movw	r0, #2804
	movw	r3, #2818
	ldrh	r0, [r4, r0]
	movw	r2, #2806
	movw	r1, #2808
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, r2]
	str	r0, [sp, #4]
	add	r0, r4, #2816
	ldrh	r0, [r0]
	ldrh	r1, [r4, r1]
	str	r0, [sp]
	ldr	r0, .L1451+20
	bl	printk
	ldr	ip, [r4, #2800]
	movw	r2, #586
	ldr	r0, [r4, #1096]
	ldrh	lr, [ip, #150]
	add	r3, r0, #588
	add	r1, r0, #584
	ldrh	r2, [r0, r2]
	ldrh	r3, [r3]
	ldrh	r1, [r1]
	str	lr, [sp, #8]
	ldr	ip, [ip, #156]
	str	ip, [sp, #4]
	movw	ip, #590
	ldrh	r0, [r0, ip]
	str	r0, [sp]
	ldr	r0, .L1451+24
	bl	printk
	add	sp, sp, #32
	@ sp needed
	pop	{r4, pc}
.L1452:
	.align	2
.L1451:
	.word	.LANCHOR0
	.word	.LC112
	.word	.LC113
	.word	.LC114
	.word	.LC115
	.word	.LC116
	.word	.LC117
	.fnend
	.size	print_ftl_debug_info, .-print_ftl_debug_info
	.align	2
	.global	ftl_write_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_write_buf, %function
ftl_write_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	subs	r4, r0, #0
	bne	.L1454
	movw	r2, #811
	ldr	r1, .L1464
	ldr	r0, .L1464+4
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L1457:
	mov	r1, r4
	ldr	r0, .L1464+8
	bl	buf_add_tail
	ldr	r3, [r6, #2800]
	ldrb	r1, [r4, #40]	@ zero_extendqisi2
	ldrb	r0, [r6, #2796]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	r0, r0, #1
	add	r2, r2, r1
	uxtb	r0, r0
	str	r2, [r3, #16]
	ldr	r2, [r3, #32]
	strb	r0, [r6, #2796]
	add	r2, r2, #1
	str	r2, [r3, #32]
	pop	{r4, r5, r6, pc}
.L1454:
	ldr	r3, .L1464+12
	ldrb	r1, [r4, #40]	@ zero_extendqisi2
	ldrb	r2, [r3, #-2546]	@ zero_extendqisi2
	mov	r5, r3
	cmp	r1, r2
	bls	.L1459
	movw	r2, #818
	ldr	r1, .L1464
	ldr	r0, .L1464+4
	bl	printk
	bl	dump_stack
.L1459:
	ldrb	r3, [r4, #40]	@ zero_extendqisi2
	ldr	r6, .L1464+16
	cmp	r3, #0
	beq	.L1456
	ldrb	r2, [r5, #-2546]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L1457
.L1456:
	mov	r0, r4
	bl	zbuf_free
	ldrb	r0, [r6, #2796]	@ zero_extendqisi2
	pop	{r4, r5, r6, pc}
.L1465:
	.align	2
.L1464:
	.word	.LANCHOR1+1597
	.word	.LC0
	.word	.LANCHOR0+2820
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	ftl_write_buf, .-ftl_write_buf
	.align	2
	.global	ftl_write_completed
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_write_completed, %function
ftl_write_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r10, #0
	ldr	r8, .L1483
	ldr	r9, .L1483+4
	mov	r6, r8
.L1467:
	ldrb	r5, [r8, #2773]	@ zero_extendqisi2
	cmp	r5, #255
	bne	.L1478
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1478:
	mov	r2, #48
	ldr	r3, .L1483+8
	mul	r2, r2, r5
	add	r7, r5, r5, lsl #1
	add	r7, r3, r7, lsl #4
	add	r4, r8, r2
	ldrb	r1, [r4, #1232]	@ zero_extendqisi2
	strb	r1, [r8, #2773]
	ldr	r1, [r4, #1268]
	cmn	r1, #1
	bne	.L1468
	ldr	r1, .L1483+12
	ldrb	r3, [r1, #-2542]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1469
	ldrb	r3, [r1, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1470
.L1469:
	ldr	r3, [r6, #2800]
	ldr	r3, [r3, #156]
	cmp	r3, r9
	beq	.L1471
.L1470:
	mov	r10, #48
	ldr	r3, .L1483+16
	mla	r10, r10, r5, r6
	ldrb	r1, [r1, #-3136]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r6, #1153]	@ zero_extendqisi2
	ldr	r2, [r10, #1256]
	rsb	r3, r3, #24
	sub	r3, r3, r0
	lsr	r2, r2, r0
	mvn	r0, #0
	bic	r0, r2, r0, lsl r3
	bl	__aeabi_uidiv
	ldr	r3, [r6, #1096]
	uxth	r4, r0
	movw	r2, #65535
	ldr	r0, .L1483+20
	ldr	r1, [r3, #560]
	cmp	r1, r2
	mov	r1, r4
	streq	r4, [r3, #560]
	ldreq	r2, [r10, #1256]
	streq	r2, [r3, #564]
	mov	r3, #48
	mla	r5, r3, r5, r6
	ldr	r3, [r5, #1256]
	ldr	r2, [r5, #1252]
	bl	printk
	ldr	r3, [r6, #1096]
	ldr	r2, [r3, #556]
	add	r2, r2, #1
	str	r2, [r3, #556]
	ldrh	r2, [r3, #16]
	cmp	r2, r4
	moveq	r2, #0
	strheq	r2, [r3, #22]	@ movhi
	beq	.L1471
	ldrh	r2, [r3, #48]
	cmp	r2, r4
	moveq	r2, #0
	strheq	r2, [r3, #54]	@ movhi
.L1471:
	mov	r0, r7
	mov	r10, #1
	bl	ftl_write_buf
	b	.L1467
.L1468:
	cmp	r10, #1
	bne	.L1476
	ldr	r2, [r4, #1256]
	ldr	r1, [r4, #1252]
	ldr	r0, .L1483+24
	bl	printk
	mov	r0, r7
	bl	ftl_write_buf
	b	.L1467
.L1476:
	add	r3, r3, r2
	ldr	r1, [r4, #1260]
	ldrh	r2, [r3, #32]
	ldr	r0, [r4, #1252]
	bl	lpa_hash_update_ppa
	ldrb	r3, [r4, #1234]	@ zero_extendqisi2
	tst	r3, #4
	bicne	r3, r3, #2
	strbne	r3, [r4, #1234]
	bne	.L1467
	mov	r0, r7
	bl	zbuf_free
	b	.L1467
.L1484:
	.align	2
.L1483:
	.word	.LANCHOR0
	.word	1145785929
	.word	.LANCHOR0+1232
	.word	.LANCHOR3
	.word	.LANCHOR3-3138
	.word	.LC118
	.word	.LC119
	.fnend
	.size	ftl_write_completed, .-ftl_write_completed
	.align	2
	.global	zftl_add_read_buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_add_read_buf, %function
zftl_add_read_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	subs	r5, r0, #0
	bne	.L1486
	movw	r2, #1151
	ldr	r1, .L1491
	ldr	r0, .L1491+4
	bl	printk
	bl	dump_stack
	pop	{r4, r5, r6, lr}
	b	print_ftl_debug_info
.L1486:
	ldr	r4, .L1491+8
	ldrb	r2, [r5, #40]	@ zero_extendqisi2
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1488
	movw	r2, #1158
	ldr	r1, .L1491
	ldr	r0, .L1491+4
	bl	printk
	bl	dump_stack
.L1488:
	mov	r1, r5
	ldr	r0, .L1491+12
	bl	buf_add_tail
	ldrb	r3, [r4, #-2535]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #-2535]
	pop	{r4, r5, r6, pc}
.L1492:
	.align	2
.L1491:
	.word	.LANCHOR1+1611
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR3-2536
	.fnend
	.size	zftl_add_read_buf, .-zftl_add_read_buf
	.align	2
	.global	sblk_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_init, %function
sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1494
	mvn	r2, #0
	mov	r0, #0
	strb	r2, [r3, #2770]
	strb	r2, [r3, #2773]
	strb	r2, [r3, #2771]
	strb	r2, [r3, #2772]
	bx	lr
.L1495:
	.align	2
.L1494:
	.word	.LANCHOR0
	.fnend
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L1506
	ldr	r0, .L1506+4
	ldrb	r1, [r5, #2770]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r5, #2770]	@ zero_extendqisi2
	cmp	r4, #255
	popeq	{r4, r5, r6, pc}
	add	r4, r4, r4, lsl #1
	add	r5, r5, #1232
	ldr	r6, .L1506+8
	add	r4, r5, r4, lsl #4
.L1498:
	ldr	r3, [r4, #24]
	mov	r0, r6
	ldrb	r2, [r4, #42]	@ zero_extendqisi2
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r4]	@ zero_extendqisi2
	cmp	r4, #255
	popeq	{r4, r5, r6, pc}
	add	r4, r4, r4, lsl #1
	add	r4, r5, r4, lsl #4
	b	.L1498
.L1507:
	.align	2
.L1506:
	.word	.LANCHOR0
	.word	.LC120
	.word	.LC121
	.fnend
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_lun_state, %function
queue_lun_state:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	lr, .L1520
	ldrb	ip, [lr, #2770]	@ zero_extendqisi2
	cmp	ip, #255
	beq	.L1516
	ldrb	r3, [lr, #1153]	@ zero_extendqisi2
	mov	r2, #1
	mov	r8, #48
	movw	r10, #1274
	rsb	r6, r3, #24
	lsl	r3, r2, r3
	sub	r3, r3, #1
	uxth	r3, r3
	and	r4, r3, r0, asr r6
	str	r4, [sp]
	ldr	r4, .L1520+4
	sub	r5, r4, #3136
	ldrb	r4, [r4, #-3136]	@ zero_extendqisi2
	ldrh	r7, [r5, #-2]
	sub	r4, r4, #1
	sub	r5, r6, r7
	uxth	r4, r4
	lsl	r2, r2, r5
	and	r0, r4, r0, asr r7
	sub	r2, r2, #1
	uxth	r2, r2
	and	r0, r0, r2
	str	r0, [sp, #4]
.L1515:
	mla	r0, r8, ip, lr
	ldr	r9, [sp]
	movw	r5, #1256
	ldr	r5, [r0, r5]
	and	fp, r3, r5, lsr r6
	cmp	r9, fp
	bne	.L1510
	and	r5, r4, r5, lsr r7
	ldr	r9, [sp, #4]
	ldrb	r0, [r0, r10]	@ zero_extendqisi2
	and	r5, r5, r2
	cmp	r9, r5
	bne	.L1511
	cmp	r1, #1
	bne	.L1508
.L1513:
	sub	r5, r0, #7
	tst	r5, #253
	beq	.L1510
.L1508:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1511:
	cmp	r1, #3
	ldrls	pc, [pc, r1, asl #2]
	b	.L1510
.L1512:
	.word	.L1508
	.word	.L1513
	.word	.L1514
	.word	.L1508
.L1514:
	cmp	r0, #11
	bne	.L1508
.L1510:
	mla	ip, r8, ip, lr
	ldrb	ip, [ip, #1232]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1515
.L1516:
	mov	r0, #0
	b	.L1508
.L1521:
	.align	2
.L1520:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1548
	mov	ip, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r7, .L1548+4
	mov	r0, #48
	mvn	r4, #0
	movw	r5, #1275
	ldrb	r1, [r3, #2771]	@ zero_extendqisi2
	movw	r6, #1233
	ldrb	r2, [r3, #2770]	@ zero_extendqisi2
	add	r8, r3, #1232
	add	r9, r7, #1
	add	r10, r7, #2
	str	r1, [sp, #8]
	ldrb	r1, [r3, #2773]	@ zero_extendqisi2
	str	r1, [sp, #12]
	ldrb	r1, [r3, #2772]	@ zero_extendqisi2
	str	r1, [sp, #4]
.L1523:
	cmp	r2, #255
	beq	.L1524
	mla	fp, r0, r2, r3
	movw	lr, #1274
	mov	r1, r2
	ldrb	lr, [fp, lr]	@ zero_extendqisi2
	sub	lr, lr, #12
	cmp	lr, #1
	bls	.L1525
	cmp	ip, #0
	beq	.L1522
.L1547:
	strb	r2, [r3, #2770]
	b	.L1522
.L1525:
	mul	ip, r0, r1
	ldrb	r2, [fp, #1232]	@ zero_extendqisi2
	add	lr, r3, ip
	ldrb	fp, [lr, r5]	@ zero_extendqisi2
	strb	r4, [lr, #1232]
	cmp	fp, #1
	bne	.L1528
	add	ip, r8, ip
	ldrh	ip, [ip, #34]
	cmp	ip, #0
	ldreq	ip, [sp, #12]
	moveq	lr, r10
	ldrne	ip, [sp, #4]
	movne	lr, r9
.L1529:
	cmp	ip, #255
	bne	.L1534
	strb	r2, [r3, #2770]
	mov	r2, #48
	mla	r3, r2, r1, r3
	ldrb	r3, [r3, #1233]	@ zero_extendqisi2
	strb	r3, [lr]
.L1522:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1528:
	cmp	fp, #0
	bne	.L1530
	ldr	ip, [lr, #1252]
	cmn	ip, #1
	ldrne	ip, [sp, #8]
	movne	lr, r7
	bne	.L1529
.L1530:
	mov	ip, #1
	b	.L1523
.L1534:
	mov	lr, ip
	mla	ip, r0, ip, r3
	ldrb	ip, [ip, #1232]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1534
	mla	r1, r0, r1, r3
	mla	lr, r0, lr, r3
	ldrb	r1, [r1, r6]	@ zero_extendqisi2
	strb	r1, [lr, #1232]
	b	.L1530
.L1524:
	cmp	ip, #0
	beq	.L1522
	mvn	r2, #0
	b	.L1547
.L1549:
	.align	2
.L1548:
	.word	.LANCHOR0
	.word	.LANCHOR0+2771
	.fnend
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_select_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1561
	mov	r3, #0
	movw	r0, #65535
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r2, r1, #4
.L1552:
	lsl	ip, r3, #3
	uxth	r4, r3
	ldrh	ip, [ip, r1]
	cmp	ip, r0
	beq	.L1551
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1552
	mov	r4, r3
	mov	r1, #0
	mov	r3, #32768
	sub	ip, r2, #4
.L1554:
	add	r0, ip, r1, lsl #3
	uxth	lr, r1
	ldrh	r0, [r0, #2]
	tst	r0, #32768
	bne	.L1553
	cmp	r0, r3
	movcc	r3, r0
	movcc	r4, lr
.L1553:
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L1554
	cmp	r4, #32
	bne	.L1551
	ldr	r3, .L1561+4
	mvn	r1, #0
	sub	r2, r2, #4
	ldrb	lr, [r3, #-2276]	@ zero_extendqisi2
	mov	r3, #0
.L1556:
	lsl	ip, r3, #3
	uxth	r5, r3
	add	r0, r2, ip
	ldrh	r0, [r0, #2]
	cmp	r0, r1
	bcs	.L1555
	ldrh	ip, [ip, r2]
	cmp	ip, lr
	movne	r1, r0
	movne	r4, r5
.L1555:
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1556
	cmp	r4, #32
	bne	.L1551
	movw	r2, #377
	ldr	r1, .L1561+8
	ldr	r0, .L1561+12
	bl	printk
	bl	dump_stack
.L1551:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1562:
	.align	2
.L1561:
	.word	.LANCHOR3-2532
	.word	.LANCHOR3
	.word	.LANCHOR1+1629
	.word	.LC0
	.fnend
	.size	pm_select_ram_region, .-pm_select_ram_region
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
	.global	flash_lsb_page_tbl_build
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_lsb_page_tbl_build, %function
flash_lsb_page_tbl_build:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r5, .L1618
	mov	r0, #1024
	ldr	r3, [r5, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	__aeabi_idiv
	cmp	r6, #0
	uxth	r4, r0
	bne	.L1565
	add	r5, r5, #4
.L1566:
	lsl	r3, r6, #1
	strh	r6, [r3, r5]	@ movhi
	add	r6, r6, #1
	cmp	r6, #512
	bne	.L1566
.L1572:
	mov	r2, #2048
	mov	r1, #255
	ldr	r0, .L1618+4
	bl	ftl_memset
	ldr	ip, .L1618+8
	mov	r3, #0
	ldr	r0, .L1618+4
.L1567:
	lsl	r2, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r2, ip]
	lsl	r1, r2, #1
	strh	r2, [r0, r1]	@ movhi
	uxth	r2, r3
	cmp	r2, r4
	bcc	.L1567
	pop	{r4, r5, r6, pc}
.L1565:
	cmp	r6, #1
	bne	.L1568
	mov	r3, #0
	add	r5, r5, #4
.L1571:
	cmp	r3, #3
	uxth	r2, r3
	bls	.L1569
	tst	r2, #1
	movne	r1, #3
	moveq	r1, #2
	rsb	r2, r1, r2, lsl #1
	uxth	r2, r2
.L1569:
	lsl	r1, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r1, r5]	@ movhi
	bne	.L1571
	b	.L1572
.L1568:
	cmp	r6, #2
	bne	.L1573
	mov	r2, #0
	add	r5, r5, #4
.L1575:
	uxth	r3, r2
	cmp	r2, #1
	lsl	r1, r2, #1
	add	r2, r2, #1
	lslhi	r3, r3, #1
	subhi	r3, r3, #1
	uxthhi	r3, r3
	cmp	r2, #512
	strh	r3, [r1, r5]	@ movhi
	bne	.L1575
	b	.L1572
.L1573:
	cmp	r6, #3
	bne	.L1576
	mov	r3, #0
	add	r5, r5, #4
.L1579:
	cmp	r3, #5
	uxth	r2, r3
	bls	.L1577
	tst	r2, #1
	movne	r1, #5
	moveq	r1, #4
	rsb	r2, r1, r2, lsl #1
	uxth	r2, r2
.L1577:
	lsl	r1, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r1, r5]	@ movhi
	bne	.L1579
	b	.L1572
.L1576:
	cmp	r6, #4
	mov	r2, r5
	mov	r3, #0
	bne	.L1580
	strh	r3, [r5, #4]	@ movhi
	mov	r3, #1
	strh	r3, [r5, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r5, #8]	@ movhi
	mov	r3, #3
	strh	r3, [r5, #10]	@ movhi
	mov	r3, #5
	strh	r3, [r5, #14]	@ movhi
	mov	r3, #7
	strh	r3, [r5, #16]	@ movhi
	mov	r3, #8
	strh	r6, [r5, #12]	@ movhi
	strh	r3, [r5, #18]!	@ movhi
.L1582:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r2, [r5, #2]!	@ movhi
	cmp	r3, #512
	bne	.L1582
	b	.L1572
.L1580:
	cmp	r6, #5
	bne	.L1583
	add	r1, r5, #4
.L1584:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L1584
	add	r2, r2, #34
.L1585:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L1585
	b	.L1572
.L1583:
	cmp	r6, #8
	bne	.L1586
	add	r5, r5, #4
.L1587:
	strh	r3, [r3, r5]	@ movhi
	add	r3, r3, #2
	cmp	r3, #1024
	bne	.L1587
	b	.L1572
.L1586:
	cmp	r6, #9
	bne	.L1588
	strh	r3, [r5, #4]	@ movhi
	mov	r3, #1
	strh	r3, [r5, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r5, #8]!	@ movhi
	movw	r2, #1021
	mov	r3, #3
.L1589:
	strh	r3, [r5, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r2
	bne	.L1589
	b	.L1572
.L1588:
	cmp	r6, #10
	bne	.L1590
	add	r1, r5, #4
.L1591:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L1591
	add	r2, r2, #128
	movw	r1, #961
.L1592:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L1592
	b	.L1572
.L1590:
	cmp	r6, #11
	bne	.L1593
	ldr	r1, .L1618+8
	mov	r3, #0
.L1594:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L1594
	add	r2, r2, #18
.L1596:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L1596
	b	.L1572
.L1593:
	cmp	r6, #13
	bne	.L1572
	ldr	r2, .L1618+12
	mov	r3, #0
.L1597:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #3
	uxth	r3, r3
	cmp	r3, #1536
	bne	.L1597
	b	.L1572
.L1619:
	.align	2
.L1618:
	.word	.LANCHOR0
	.word	.LANCHOR3-2272
	.word	.LANCHOR0+4
	.word	.LANCHOR0+2
	.fnend
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_die_info_init, %function
flash_die_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L1638
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L1621
	ldr	r1, .L1638+4
	ldr	r0, .L1638+8
	bl	printk
.L1621:
	ldrh	r3, [r4, #30]
	mov	r7, #0
	ldr	r5, .L1638+12
	ldr	r9, .L1638+16
	ldr	r6, .L1638+20
	strh	r3, [r5, #2]	@ movhi
	ldrb	r1, [r4, #16]	@ zero_extendqisi2
	add	r8, r9, #6
	ldrh	r0, [r4, #14]
	mov	r10, r8
	strb	r7, [r5, #1109]
	bl	__aeabi_idiv
	mov	r2, #8
	strh	r0, [r6, #-224]	@ movhi
	mov	r1, r7
	ldr	r0, .L1638+24
	bl	ftl_memset
	mov	r2, #32
	mov	r1, r7
	sub	r0, r6, #220
	bl	ftl_memset
.L1623:
	mov	r3, #2
	ldrb	r2, [r4, #4]	@ zero_extendqisi2
	strb	r3, [r7, r9]
	add	r1, r8, r7, lsl #3
	ldr	r0, .L1638+28
	bl	flash_mem_cmp8
	cmp	r0, #0
	ldr	fp, .L1638+28
	bne	.L1622
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	add	r2, r6, r3, lsl #2
	str	r0, [r2, #-220]
	uxtb	r0, r7
	add	r2, r3, #1
	add	r3, r5, r3
	strb	r2, [r5, #1109]
	strb	r0, [r3, #1144]
	bl	zftl_flash_enter_slc_mode
.L1622:
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1623
	ldrb	r3, [r4, #12]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L1624
.L1628:
	ldrb	r2, [r4, #17]	@ zero_extendqisi2
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldrh	r2, [r4, #18]
	smulbb	r3, r3, r2
	strh	r3, [r6, #-188]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1624:
	ldrb	r3, [r4, #4]	@ zero_extendqisi2
	mov	r7, #0
	ldrh	r8, [r5, #2]
	ldrb	r9, [r4, #27]	@ zero_extendqisi2
	str	r3, [sp]
	ldrh	r3, [r4, #18]
	and	r3, r3, #65280
	mul	r3, r8, r3
	ldrb	r8, [r4, #17]	@ zero_extendqisi2
	mul	r8, r8, r3
	lsl	r3, r8, #1
.L1627:
	ldr	r2, [sp]
	add	r1, r10, r7, lsl #3
	mov	r0, fp
	str	r3, [sp, #4]
	bl	flash_mem_cmp8
	cmp	r0, #0
	ldr	r3, [sp, #4]
	bne	.L1625
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	cmp	r9, #0
	moveq	r0, r8
	movne	r0, r3
	add	r1, r6, r2, lsl #2
	str	r0, [r1, #-220]
	add	r1, r2, #1
	add	r2, r5, r2
	strb	r1, [r5, #1109]
	strb	r7, [r2, #1144]
.L1625:
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1627
	b	.L1628
.L1639:
	.align	2
.L1638:
	.word	.LANCHOR2
	.word	.LANCHOR1+1650
	.word	.LC4
	.word	.LANCHOR0
	.word	.LANCHOR0+1154
	.word	.LANCHOR3
	.word	.LANCHOR0+1144
	.word	.LANCHOR2+5
	.fnend
	.size	flash_die_info_init, .-flash_die_info_init
	.align	2
	.global	lpa_hash_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	lpa_hash_init, %function
lpa_hash_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r2, #512
	ldr	r4, .L1642
	mov	r1, #255
	sub	r0, r4, #3056
	sub	r0, r0, #14
	bl	ftl_memset
	sub	r3, r4, #3088
	ldrb	r2, [r4, #-3127]	@ zero_extendqisi2
	ldrh	r3, [r3, #-8]
	mov	r1, #255
	ldr	r0, [r4, #-2552]
	pop	{r4, lr}
	mul	r2, r2, r3
	lsl	r2, r2, #2
	b	ftl_memset
.L1643:
	.align	2
.L1642:
	.word	.LANCHOR3
	.fnend
	.size	lpa_hash_init, .-lpa_hash_init
	.align	2
	.global	lpa_rebuild_hash
	.syntax unified
	.arm
	.fpu softvfp
	.type	lpa_rebuild_hash, %function
lpa_rebuild_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1656
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1645
	mov	r3, #0
	mov	r2, #239
	ldr	r1, .L1656+4
	ldr	r0, .L1656+8
	bl	printk
.L1645:
	ldr	r4, .L1656+12
	mov	r2, #512
	mov	r1, #255
	sub	r5, r4, #3056
	sub	r6, r4, #3088
	sub	r5, r5, #14
	mov	r0, r5
	bl	ftl_memset
	ldrh	r3, [r6, #-8]
	mov	r1, #255
	ldrb	r2, [r4, #-3127]	@ zero_extendqisi2
	ldr	r0, [r4, #-2552]
	mul	r2, r2, r3
	lsl	r2, r2, #2
	bl	ftl_memset
	mov	r1, #0
.L1646:
	ldrh	r0, [r6, #-8]
	uxth	ip, r1
	ldrb	r3, [r4, #-3127]	@ zero_extendqisi2
	mov	r2, ip
	mul	r3, r3, r0
	cmp	ip, r3, lsl #1
	blt	.L1648
	pop	{r4, r5, r6, pc}
.L1648:
	ldr	r3, [r4, #-2556]
	ldr	r3, [r3, r2, lsl #2]
	cmn	r3, #1
	beq	.L1647
	uxtb	r3, r3
	lsl	r2, r2, #1
	lsl	r3, r3, #1
	ldrh	r0, [r5, r3]
	strh	ip, [r5, r3]	@ movhi
	ldr	r3, [r4, #-2552]
	strh	r0, [r3, r2]	@ movhi
.L1647:
	add	r1, r1, #1
	b	.L1646
.L1657:
	.align	2
.L1656:
	.word	.LANCHOR2
	.word	.LANCHOR1+1670
	.word	.LC122
	.word	.LANCHOR3
	.fnend
	.size	lpa_rebuild_hash, .-lpa_rebuild_hash
	.align	2
	.global	zftl_read_flash_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_read_flash_info, %function
zftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r2, #11
	mov	r1, #0
	mov	r4, r0
	bl	ftl_memset
	ldr	r2, .L1662
	mov	ip, #1
	ldr	r3, .L1662+4
	ldrh	r0, [r2, #2]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	smulbb	r1, r1, r0
	ldr	r0, .L1662+8
	strh	r1, [r4, #4]	@ unaligned
	ldrb	r1, [r2, #1193]	@ zero_extendqisi2
	strb	r1, [r4, #7]
	ldr	r1, [r2, #1032]
	str	r1, [r4]	@ unaligned
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	ldrb	r3, [r3, #11]	@ zero_extendqisi2
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #1109]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L1659:
	uxtb	r2, r3
	cmp	r1, r2
	bhi	.L1660
	pop	{r4, pc}
.L1660:
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, lsl lr
	strb	r2, [r4, #10]
	b	.L1659
.L1663:
	.align	2
.L1662:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1144
	.fnend
	.size	zftl_read_flash_info, .-zftl_read_flash_info
	.align	2
	.global	gc_static_wearleveling
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r0, .L1779
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r3, [r0, #2800]
	ldr	r3, [r3, #32]
	cmp	r3, #10240
	bls	.L1665
	bl	ftl_tmp_into_update
.L1665:
	ldr	r3, [r0, #1096]
	ldr	r2, [r3, #568]
	ldr	r1, [r3, #12]
	add	ip, r2, #35840
	add	ip, ip, #160
	cmp	r1, ip
	bcs	.L1666
	ldr	lr, [r0, #2800]
	ldr	ip, [r3, #572]
	ldr	lr, [lr, #36]
	add	ip, ip, #256
	cmp	lr, ip
	movcc	r0, #0
	bcc	.L1664
.L1666:
	add	r2, r2, #860160
	add	r2, r2, #3840
	cmp	r1, r2
	bhi	.L1668
	ldr	ip, [r0, #2800]
	ldr	r2, [r3, #572]
	ldr	ip, [ip, #36]
	add	r2, r2, #32
	cmp	ip, r2
	movls	r7, #0
	movls	r5, r7
	bls	.L1669
.L1668:
	ldr	r2, [r0, #2800]
	mov	r4, #0
	ldr	r5, .L1779
	movw	r10, #65535
	ldr	r8, .L1779+4
	mov	r6, r4
	ldr	r0, [r2, #36]
	mov	fp, r10
	str	r1, [r3, #568]
	ldr	r9, .L1779+8
	str	r0, [r3, #572]
	ldrh	r7, [r2, #134]
	str	r4, [sp, #36]
	str	r4, [sp, #32]
	str	r4, [sp, #28]
	str	r4, [sp, #24]
	str	r4, [sp, #16]
	str	r4, [sp, #20]
.L1670:
	ldrh	r3, [r8]
	cmp	r3, r7
	bhi	.L1679
	ldr	r0, [r5, #1096]
	mov	r3, #0
	mov	r2, #128
	mov	r1, #255
	strh	r3, [r0, #122]	@ movhi
	add	r0, r0, #264
	bl	ftl_memset
	ldr	r2, [r5, #1096]
	movw	r3, #586
	ldr	r8, [r5, #1084]
	ldrh	r1, [r2, r3]
	ldr	r3, .L1779+8
	ldr	r3, [r3]
	lsl	r9, r1, #2
	add	r7, r8, r9
	tst	r3, #1024
	beq	.L1680
	ldr	ip, [r5, #1092]
	lsl	r0, r1, #1
	ldr	r3, [r8, r1, lsl #2]
	ldrh	r2, [r8, r9]
	ldrh	r0, [ip, r0]
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	str	r0, [sp, #12]
	ldrb	r0, [r7, #3]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldrb	r0, [r7, #2]	@ zero_extendqisi2
	lsr	r0, r0, #5
	str	r0, [sp, #4]
	ldrb	r0, [r7, #2]	@ zero_extendqisi2
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldr	r0, .L1779+12
	bl	printk
.L1680:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #32
	bne	.L1681
	ldr	r3, .L1779
	ldr	r3, [r3, #2800]
	add	r3, r3, #688
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrhi	r3, .L1779+16
	movhi	r2, #1
	strhi	r2, [r3, #-184]
.L1681:
	ldrb	r2, [r7, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L1682
	ldr	r5, .L1779
	ldr	r3, [r5, #2800]
	ldrh	r1, [r3, #96]
	ldr	r3, .L1779+16
	ldrh	r0, [r3, #-180]
	ldrh	r3, [r8, r9]
	ubfx	r3, r3, #0, #11
	add	r3, r3, r0, lsr #2
	cmp	r1, r3
	ble	.L1682
	and	r3, r2, #192
	cmp	r3, #64
	bne	.L1683
	ldr	r0, [r5, #1096]
	movw	r3, #586
	mov	r2, #1
	mov	r1, #0
	ldrh	r0, [r0, r3]
	bl	gc_add_sblk
	ldr	r2, .L1779+20
	mov	r1, #1
	movw	r3, #2180
	strh	r1, [r2, r3]	@ movhi
.L1682:
	ldr	r1, .L1779
	ldr	r3, [r1, #1096]
	ldr	r7, [r1, #1084]
	add	r3, r3, #584
	ldrh	r5, [r3]
	ldr	r3, .L1779+8
	ldr	r3, [r3]
	lsl	r2, r5, #2
	add	r8, r7, r2
	tst	r3, #1024
	beq	.L1684
	ldr	r0, [r1, #1092]
	lsl	r1, r5, #1
	ldr	r3, [r7, r5, lsl #2]
	ldrh	r2, [r7, r2]
	ldrh	r1, [r0, r1]
	ubfx	r3, r3, #11, #8
	ldr	r0, .L1779+24
	ubfx	r2, r2, #0, #11
	str	r1, [sp, #12]
	ldrb	r1, [r8, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r8, #2]	@ zero_extendqisi2
	lsr	r1, r1, #5
	str	r1, [sp, #4]
	ldrb	r1, [r8, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #3, #2
	str	r1, [sp]
	mov	r1, r5
	bl	printk
.L1684:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1685
	ldr	r8, .L1779
	ldr	r2, [r8, #2800]
	ldrh	r1, [r2, #98]
	ldr	r2, .L1779+16
	ldrh	r0, [r2, #-178]
	ldr	r2, [r7, r5, lsl #2]
	ubfx	r2, r2, #11, #8
	add	r2, r2, r0, lsr #2
	cmp	r1, r2
	ble	.L1685
	and	r2, r3, #192
	cmp	r2, #64
	bne	.L1686
	ldr	r3, [r8, #1096]
	mov	r2, #1
	mov	r1, #0
	add	r3, r3, #584
	ldrh	r0, [r3]
	bl	gc_add_sblk
	ldr	r2, .L1779+20
	mov	r1, #1
	movw	r3, #2180
	strh	r1, [r2, r3]	@ movhi
.L1685:
	ldr	r3, .L1779+8
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L1687
	ldr	r1, .L1779
	lsl	ip, r4, #2
	ldr	r2, [r1, #1084]
	add	r0, r2, ip
	ldr	r3, [r2, r4, lsl #2]
	ldrh	r2, [r2, ip]
	ldr	ip, [r1, #1092]
	lsl	r1, r4, #1
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	ldrh	r1, [ip, r1]
	str	r1, [sp, #12]
	ldrb	r1, [r0, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r0, #2]	@ zero_extendqisi2
	lsr	r1, r1, #5
	str	r1, [sp, #4]
	ldrb	r1, [r0, #2]	@ zero_extendqisi2
	ldr	r0, .L1779+28
	ubfx	r1, r1, #3, #2
	str	r1, [sp]
	mov	r1, r4
	bl	printk
.L1687:
	ldr	r3, .L1779+8
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L1688
	ldr	r1, .L1779
	lsl	ip, r6, #2
	ldr	r2, [r1, #1084]
	add	r0, r2, ip
	ldr	r3, [r2, r6, lsl #2]
	ldrh	r2, [r2, ip]
	ldr	ip, [r1, #1092]
	lsl	r1, r6, #1
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	ldrh	r1, [ip, r1]
	str	r1, [sp, #12]
	ldrb	r1, [r0, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r0, #2]	@ zero_extendqisi2
	lsr	r1, r1, #5
	str	r1, [sp, #4]
	ldrb	r1, [r0, #2]	@ zero_extendqisi2
	ldr	r0, .L1779+32
	ubfx	r1, r1, #3, #2
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1688:
	ldr	r5, .L1779
	ldrh	r3, [sp, #16]
	ldr	r1, [sp, #24]
	ldr	r4, [r5, #2800]
	ldr	r0, [sp, #36]
	strh	r3, [r4, #96]	@ movhi
	ldrh	r3, [sp, #20]
	strh	r10, [r4, #92]	@ movhi
	strh	fp, [r4, #94]	@ movhi
	strh	r3, [r4, #98]	@ movhi
	bl	__aeabi_uidiv
	strh	r0, [r4, #88]	@ movhi
	mov	r6, r0
	ldr	r1, [sp, #28]
	ldr	r0, [sp, #32]
	bl	__aeabi_uidiv
	strh	r0, [r4, #90]	@ movhi
	ldr	r4, .L1779+8
	ldr	r3, [r4]
	tst	r3, #1024
	beq	.L1689
	uxth	r0, r0
	uxth	r3, r6
	ldr	r2, [sp, #28]
	str	r0, [sp]
	ldr	r1, [sp, #24]
	ldr	r0, .L1779+36
	bl	printk
.L1689:
	ldr	r3, [r4]
	ldr	r4, .L1779+16
	tst	r3, #1024
	beq	.L1690
	ldrh	r3, [r4, #-178]
	mov	r2, fp
	mov	r1, r10
	ldr	r0, .L1779+40
	str	r3, [sp, #8]
	ldrh	r3, [r4, #-180]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	printk
.L1690:
	ldr	r3, [sp, #20]
	sub	r3, r3, fp
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldrh	r3, [r4, #-178]
	cmp	r2, r3
	bgt	.L1691
	ldr	r3, [sp, #16]
	ldrh	r2, [r4, #-180]
	sub	r3, r3, r10
	cmp	r3, r2
	movle	r7, #0
	movle	r5, r7
	ble	.L1692
.L1691:
	ldr	r3, [r5, #1096]
	mov	r7, #0
	ldr	r4, [r3, #580]
	ldr	r3, [r5, #2800]
	mov	r5, r7
	ldrh	r9, [r3, #134]
	uxth	r4, r4
	ldr	r3, .L1779+20
	add	r3, r3, #2176
	add	r3, r3, #4
	str	r3, [sp, #20]
.L1693:
	ldr	r3, .L1779+4
	ldrh	r3, [r3]
	cmp	r9, r3
	bcc	.L1703
.L1702:
	ldr	r3, .L1779
	ldr	r3, [r3, #1096]
	str	r4, [r3, #580]
.L1692:
	cmp	fp, #0
	beq	.L1705
	ldr	r2, .L1779
	ldr	r4, .L1779+4
	ldr	r3, [r2, #2800]
	mov	r0, r2
	ldrh	r3, [r3, #134]
.L1706:
	ldrh	r2, [r4]
	cmp	r2, r3
	bhi	.L1708
	ldr	r3, [r0, #2800]
	ldrh	r2, [r3, #72]
	add	r2, fp, r2
	strh	r2, [r3, #72]	@ movhi
	ldrh	r2, [r3, #98]
	cmp	fp, r2
	subcc	r2, r2, fp
	strhcc	r2, [r3, #98]	@ movhi
.L1705:
	cmp	r10, #0
	beq	.L1711
	ldr	r3, .L1779
	ldr	r6, .L1779+4
	ldr	r2, [r3, #2800]
	mov	ip, r3
	ldrh	r0, [r2, #134]
.L1712:
	ldrh	r3, [r6]
	cmp	r3, r0
	bhi	.L1714
	ldr	r3, [ip, #2800]
	ldrh	r2, [r3, #74]
	add	r2, r10, r2
	strh	r2, [r3, #74]	@ movhi
	ldrh	r2, [r3, #96]
	cmp	r10, r2
	subcc	r2, r2, r10
	strhcc	r2, [r3, #96]	@ movhi
.L1711:
	ldr	r4, .L1779+44
	mov	r1, #0
	sub	r0, r4, #12
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1669
	ldr	r2, .L1779
	lsl	r3, r0, #1
	ldr	r1, [r2, #1092]
	ldrh	r2, [r4, #-8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #1
	bhi	.L1669
	add	r5, r5, #1
	mov	r2, #1
	mov	r1, #0
	bl	gc_add_sblk
.L1669:
	add	r0, r5, r7
.L1664:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1679:
	ldr	r0, [r5, #1084]
	lsl	r1, r7, #2
	add	ip, r0, r1
	ldrb	r3, [ip, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	cmp	r2, #224
	beq	.L1671
	tst	r3, #8
	beq	.L1672
	ldrh	r2, [r0, r1]
	ldr	r3, [r0, r1]
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
.L1673:
	ldr	lr, [sp, #24]
	cmp	r10, r2
	add	lr, lr, #1
	uxth	lr, lr
	str	lr, [sp, #24]
	ldr	lr, [sp, #36]
	add	lr, lr, r2
	str	lr, [sp, #36]
	ldrhi	r10, [r5, #1096]
	movwhi	lr, #586
	strhhi	r7, [r10, lr]	@ movhi
	movhi	r10, r2
.L1717:
	ldr	lr, [sp, #16]
	cmp	lr, r2
	movcc	lr, r2
	movcc	r4, r7
	str	lr, [sp, #16]
	movw	lr, #65535
	cmp	r3, lr
	bne	.L1675
.L1677:
	cmp	r2, #9
	cmphi	r3, #9
	bhi	.L1671
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L1671
	ldr	r3, [r0, r1]
	ldrh	r2, [r0, r1]
	lsl	r1, r7, #1
	ldr	r0, [r5, #1092]
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	ldrh	r1, [r0, r1]
	ldr	r0, .L1779+48
	str	r1, [sp, #12]
	ldrb	r1, [ip, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [ip, #2]	@ zero_extendqisi2
	lsr	r1, r1, #5
	str	r1, [sp, #4]
	ldrb	r1, [ip, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #3, #2
	str	r1, [sp]
	mov	r1, r7
	bl	printk
.L1671:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1670
.L1672:
	tst	r3, #24
	ldrheq	r2, [r0, r1]
	movweq	r3, #65535
	ldrne	r3, [r0, r1]
	movwne	r2, #65535
	ubfxeq	r2, r2, #0, #11
	ubfxne	r3, r3, #11, #8
	beq	.L1673
.L1675:
	ldr	lr, [sp, #28]
	cmp	fp, r3
	movhi	fp, r3
	add	lr, lr, #1
	uxth	lr, lr
	str	lr, [sp, #28]
	ldr	lr, [sp, #32]
	add	lr, lr, r3
	str	lr, [sp, #32]
	ldrhi	lr, [r5, #1096]
	addhi	lr, lr, #584
	strhhi	r7, [lr]	@ movhi
	ldr	lr, [sp, #20]
	cmp	lr, r3
	movcc	lr, r3
	movcc	r6, r7
	str	lr, [sp, #20]
	b	.L1677
.L1683:
	tst	r2, #224
	bne	.L1682
	ldr	r3, [r5, #1096]
	movw	r8, #590
	movw	r2, #65535
	ldrh	r1, [r3, r8]
	cmp	r1, r2
	bne	.L1682
	movw	r7, #586
	ldrh	r0, [r3, r7]
	add	r3, r3, #588
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L1682
	bl	zftl_remove_free_node
	ldr	r3, [r5, #1096]
	ldrh	r2, [r3, r7]
	strh	r2, [r3, r8]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1682
.L1686:
	and	r3, r3, #248
	cmp	r3, #16
	bne	.L1685
	ldr	r3, [r8, #1096]
	add	r2, r3, #588
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1685
	add	r2, r3, #584
	ldrh	r0, [r2]
	movw	r2, #590
	ldrh	r3, [r3, r2]
	cmp	r3, r0
	beq	.L1685
	bl	zftl_remove_free_node
	ldr	r3, [r8, #1096]
	add	r2, r3, #588
	add	r3, r3, #584
	ldrh	r1, [r3]
	strh	r1, [r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L1685
.L1703:
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r3, r4
	ldr	r3, .L1779
	movls	r4, #0
	lsl	r6, r4, #2
	ldr	r8, [r3, #1084]
	add	r2, r8, r6
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	and	ip, r2, #224
	and	r1, r2, #192
	cmp	r1, #0
	cmpne	ip, #224
	beq	.L1695
	ubfx	r2, r2, #3, #2
	ldr	r0, .L1779+16
	ands	r1, r2, #1
	beq	.L1696
	cmp	ip, #160
.L1777:
	bne	.L1698
	ldrh	r2, [r0, #-178]
	ldr	r0, [sp, #24]
	cmp	r0, r2
	ble	.L1699
	ldr	r2, [r8, r6]
	ubfx	r2, r2, #11, #8
	cmp	r2, fp
	bls	.L1700
	cmp	r1, #0
	beq	.L1699
	ldrh	r2, [r8, r6]
	ubfx	r2, r2, #0, #11
	cmp	r2, r10
	bgt	.L1699
.L1700:
	mov	r2, #1
	mov	r1, #0
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r3, [sp, #20]
	mov	r2, #1
	add	r5, r5, r2
	strh	r2, [r3]	@ movhi
	ldr	r2, .L1779+8
	ldr	r3, [sp, #28]
	ldr	r2, [r2]
	tst	r2, #1024
	beq	.L1699
	ldr	r1, [r3, #1092]
	lsl	r2, r4, #1
	ldr	r3, [r3, #1084]
	ldrh	r0, [r1, r2]
	ldr	r1, [r8, r6]
	add	r3, r3, r6
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	ldr	r3, .L1779+20
	ubfx	r1, r1, #11, #8
	str	r1, [sp, #8]
	ldrh	r1, [r8, r6]
	lsr	r2, r2, #5
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	mov	r1, r4
	ldrh	r3, [r3, #52]
	str	r3, [sp]
	mov	r3, r0
	ldr	r0, .L1779+52
.L1778:
	bl	printk
.L1699:
	cmp	r7, #4
	cmpls	r5, #4
	bhi	.L1702
.L1695:
	add	r9, r9, #1
	uxth	r9, r9
	b	.L1693
.L1696:
	cmp	r2, #2
	b	.L1777
.L1698:
	ldr	r2, [sp, #16]
	ldrh	r0, [r0, #-180]
	sub	r2, r2, r10
	cmp	r2, r0
	ble	.L1699
	ldrh	r2, [r8, r6]
	add	r0, r10, #8
	ubfx	r2, r2, #0, #11
	cmp	r2, r0
	ble	.L1701
	cmp	r1, #0
	beq	.L1699
	ldr	r2, [r8, r6]
	add	r1, fp, #4
	ubfx	r2, r2, #11, #8
	cmp	r2, r1
	bgt	.L1699
.L1701:
	mov	r2, #1
	mov	r1, #0
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r3, [sp, #20]
	mov	r2, #1
	add	r7, r7, r2
	strh	r2, [r3]	@ movhi
	ldr	r2, .L1779+8
	ldr	r3, [sp, #28]
	ldr	r2, [r2]
	tst	r2, #1024
	beq	.L1699
	ldr	r1, [r3, #1092]
	lsl	r2, r4, #1
	ldr	r3, [r3, #1084]
	ldrh	r0, [r1, r2]
	ldr	r1, [r8, r6]
	add	r3, r3, r6
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	ldr	r3, .L1779+20
	ubfx	r1, r1, #11, #8
	str	r1, [sp, #8]
	ldrh	r1, [r8, r6]
	lsr	r2, r2, #5
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	mov	r1, r4
	ldrh	r3, [r3, #52]
	str	r3, [sp]
	mov	r3, r0
	ldr	r0, .L1779+56
	b	.L1778
.L1708:
	ldr	ip, [r0, #1084]
	lsl	lr, r3, #2
	ldr	r2, [ip, r3, lsl #2]
	add	r6, ip, lr
	ubfx	r1, r2, #11, #8
	cmp	fp, r1
	bhi	.L1707
	ldrb	r6, [r6, #2]	@ zero_extendqisi2
	tst	r6, #24
	subne	r1, r1, fp
	bfine	r2, r1, #11, #8
	strne	r2, [ip, lr]
.L1707:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1706
.L1714:
	ldr	r4, [ip, #1084]
	lsl	lr, r0, #2
	ldrh	r3, [r4, lr]
	add	r1, r4, lr
	ubfx	r2, r3, #0, #11
	cmp	r2, r10
	blt	.L1713
	ldrb	r1, [r1, #2]	@ zero_extendqisi2
	and	r1, r1, #24
	cmp	r1, #16
	subne	r2, r2, r10
	bfine	r3, r2, #0, #11
	strhne	r3, [r4, lr]	@ movhi
.L1713:
	add	r0, r0, #1
	uxth	r0, r0
	b	.L1712
.L1780:
	.align	2
.L1779:
	.word	.LANCHOR0
	.word	.LANCHOR0+1080
	.word	.LANCHOR2
	.word	.LC124
	.word	.LANCHOR3
	.word	.LANCHOR0+2824
	.word	.LC125
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LC129
	.word	.LANCHOR3-3088
	.word	.LC123
	.word	.LC130
	.word	.LC131
	.fnend
	.size	gc_static_wearleveling, .-gc_static_wearleveling
	.align	2
	.global	zftl_sblk_list_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_sblk_list_init, %function
zftl_sblk_list_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1080
	ldr	r4, .L1813
	mov	r2, #6
	mov	r1, #0
	.pad #20
	sub	sp, sp, #20
	ldr	r7, .L1813+4
	mov	r5, #0
	ldrh	r3, [r4, r3]
	add	r10, r4, #2784
	ldr	r0, [r4, #1036]
	sub	r8, r7, #3072
	sub	r9, r7, #3104
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r3, #32
	ldrh	r1, [r9, #-14]
	strh	r3, [r8, #-4]	@ movhi
	movw	r3, #2786
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2788
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2790
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2794
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2792
	strh	r5, [r4, r3]	@ movhi
	mov	r0, #32768
	ldrb	r3, [r7, #-3127]	@ zero_extendqisi2
	str	r5, [r7, #-3088]
	str	r5, [r7, #-3084]
	str	r5, [r7, #-3080]
	mul	r1, r1, r3
	str	r5, [r7, #-3104]
	str	r5, [r7, #-3116]
	str	r5, [r7, #-3100]
	strh	r5, [r10]	@ movhi
	bl	__aeabi_idiv
	sxth	r3, r0
	str	r7, [sp, #8]
	str	r10, [sp, #12]
	str	r3, [sp, #4]
	ldr	r3, [r4, #2800]
	ldrsh	r6, [r3, #134]
	strh	r5, [r3, #146]	@ movhi
.L1782:
	ldr	r3, .L1813+8
	ldrh	r3, [r3]
	cmp	r6, r3
	blt	.L1799
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #2800]
	ldrh	r2, [r2]
	strh	r2, [r3, #114]	@ movhi
	movw	r2, #2786
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #118]	@ movhi
	movw	r2, #2788
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #116]	@ movhi
	movw	r2, #2790
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #122]	@ movhi
	movw	r2, #2794
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #120]	@ movhi
	movw	r2, #2792
	ldrh	r2, [r4, r2]
	strh	r2, [r3, #124]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1799:
	ldr	r7, [r4, #1084]
	add	r7, r7, r6, lsl #2
	ldrb	r3, [r7, #3]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r1, [sp, #4]
	beq	.L1783
	ldr	r3, [sp, #8]
	ldr	ip, [r4, #2800]
	ldrh	r10, [r9, #-14]
	ldrb	lr, [r3, #-3127]	@ zero_extendqisi2
	mov	r3, #0
	mov	r1, r3
.L1784:
	cmp	r3, lr
	blt	.L1787
	cmp	r1, #0
	beq	.L1788
	mov	r0, #32768
	bl	__aeabi_idiv
	add	r1, r0, #1
	sxth	r1, r1
.L1783:
	lsl	fp, r6, #1
	ldr	r0, [r4, #1036]
	add	r2, fp, r6
	lsl	r2, r2, #1
	add	ip, r0, r2
	strh	r1, [ip, #4]	@ movhi
	mvn	r1, #0
	strh	r1, [ip, #2]	@ movhi
	strh	r1, [r0, r2]	@ movhi
	ldrb	r2, [r7, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #224
	cmpne	r2, #32
	moveq	r10, #1
	movne	r10, #0
	beq	.L1789
	ldr	r1, [r4, #1096]
	ldrh	r0, [r1, #16]
	cmp	r6, r0
	beq	.L1789
	ldrh	r0, [r1, #48]
	cmp	r6, r0
	beq	.L1789
	ldrh	r1, [r1, #80]
	cmp	r6, r1
	beq	.L1789
	cmp	r2, #64
	bne	.L1790
	uxth	r7, r6
	ldr	r2, .L1813+12
	sub	r0, r9, #12
	mov	r1, r7
.L1811:
	bl	_insert_data_list
	ldr	r2, [r4, #1092]
	ldrh	r3, [r2, fp]
	cmp	r3, #7
	movls	r2, r10
	movls	r1, #1
	movls	r0, r7
	bls	.L1809
.L1789:
	add	r6, r6, #1
	sxth	r6, r6
	b	.L1782
.L1787:
	ldrb	r2, [r7, #3]	@ zero_extendqisi2
	asr	r2, r2, r3
	add	r3, r3, #1
	tst	r2, #1
	ldrhne	r2, [ip, #146]
	addeq	r1, r10, r1
	sxtheq	r1, r1
	addne	r2, r2, #1
	strhne	r2, [ip, #146]	@ movhi
	b	.L1784
.L1788:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	mvn	r0, #0
	orr	r3, r3, #224
	strb	r3, [r7, #2]
	lsl	r3, r6, #1
	ldr	r2, [r4, #1092]
	strh	r0, [r2, r3]	@ movhi
	b	.L1783
.L1790:
	cmp	r2, #96
	uxtheq	r7, r6
	ldreq	r2, .L1813+16
	ldreq	r0, .L1813+20
	moveq	r1, r7
	beq	.L1811
.L1791:
	cmp	r2, #160
	uxtheq	r7, r6
	ldreq	r2, .L1813+24
	ldreq	r0, .L1813+28
	moveq	r1, r7
	beq	.L1811
.L1792:
	cmp	r2, #0
	bne	.L1789
	ldr	r2, [r4, #1092]
	uxth	r10, r6
	ldrh	r2, [r2, fp]
	cmp	r2, #0
	beq	.L1793
	cmp	r5, #2
	bgt	.L1794
	mov	r1, r6
	ldr	r0, .L1813+32
	bl	printk
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	add	r5, r5, #1
	sxth	r5, r5
	mov	r1, #1
	mov	r0, r10
	tst	r3, #16
	movne	r2, #5
	moveq	r2, #2
	bfi	r3, r2, #5, #3
	mov	r2, #0
	strb	r3, [r7, #2]
.L1809:
	bl	gc_add_sblk
	b	.L1789
.L1794:
	mov	r2, #656
	ldr	r1, .L1813+36
	ldr	r0, .L1813+40
	bl	printk
	bl	dump_stack
.L1793:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	bne	.L1797
	ldr	r2, .L1813+44
	mov	r1, r10
	ldr	r0, .L1813+48
.L1807:
	bl	_insert_free_list
	b	.L1789
.L1797:
	cmp	r3, #16
	ldreq	r2, .L1813+52
	moveq	r1, r10
	subeq	r0, r8, #12
	ldrne	r2, .L1813+56
	movne	r1, r10
	subne	r0, r8, #8
	b	.L1807
.L1814:
	.align	2
.L1813:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1080
	.word	.LANCHOR0+2790
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3100
	.word	.LC132
	.word	.LANCHOR1+1687
	.word	.LC0
	.word	.LANCHOR0+2784
	.word	.LANCHOR3-3088
	.word	.LANCHOR0+2786
	.word	.LANCHOR0+2788
	.fnend
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	pm_free_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_free_sblk, %function
pm_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 272
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #284
	sub	sp, sp, #284
	ldr	r5, .L1843
	ldrh	r3, [r5, #-176]
	cmp	r3, #128
	bls	.L1816
	mov	r2, #94
	ldr	r1, .L1843+4
	ldr	r0, .L1843+8
	bl	printk
	bl	dump_stack
.L1816:
	ldr	r4, .L1843+12
	mov	r2, #0
	ldrh	ip, [r5, #-176]
	movw	lr, #65535
	ldr	r1, [r4, #2800]
	add	r3, r1, #688
	add	r1, r1, #416
	ldrh	r0, [r3]
	mov	r3, r2
.L1819:
	ldrh	r6, [r1], #2
	cmp	r6, lr
	addne	r2, r2, #1
	uxthne	r2, r2
	cmp	r2, r0
	bcs	.L1818
	cmp	r2, ip
	bcs	.L1818
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #128
	bne	.L1819
.L1818:
	add	r3, r3, #1
	mov	r2, #256
	uxth	fp, r3
	mov	r1, #0
	add	r0, sp, #24
	cmp	fp, #129
	mov	r6, #0
	moveq	fp, #128
	bl	ftl_memset
	ldr	r7, [r4, #2800]
	movw	r3, #698
	ldrb	r8, [r4, #1153]	@ zero_extendqisi2
	ldrb	r2, [r5, #-3136]	@ zero_extendqisi2
	ldrh	r3, [r7, r3]
	add	r10, r7, #704
	rsb	r8, r8, #24
	str	r3, [sp, #12]
	ldr	r3, .L1843+16
	ldrh	r9, [r3, #-2]
	sub	r3, r8, r9
	mvn	r8, #0
	mvn	r8, r8, lsl r3
.L1821:
	ldr	r1, [sp, #12]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L1824
	ldr	r3, .L1843+20
	add	r8, sp, #24
	ldrb	r7, [r5, #-3127]	@ zero_extendqisi2
	movw	r10, #65535
	mov	r6, #0
	ldrh	r3, [r3, #-8]
	str	r6, [sp, #12]
	smulbb	r7, r7, r3
	uxth	r7, r7
.L1830:
	ldr	r2, [r4, #2800]
	add	r3, r6, #208
	lsl	r3, r3, #1
	ldrb	r1, [r5, #-3136]	@ zero_extendqisi2
	uxth	r9, r6
	ldrh	r3, [r2, r3]
	add	r0, r2, #692
	ldrh	r0, [r0]
	str	r2, [sp, #16]
	str	r3, [sp, #20]
	bl	__aeabi_idiv
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #16]
	cmp	r0, r3
	ldreq	r0, .L1843+24
	ldrbeq	r1, [r5, #-3127]	@ zero_extendqisi2
	ldrheq	r0, [r0]
	smulbbeq	r1, r1, r0
	strheq	r1, [r8]	@ movhi
	ldrh	r1, [r8]
	ldrh	ip, [r2, #74]
	cmp	r1, #0
	cmpne	r1, r7
	movcc	r0, #1
	movcs	r0, #0
	cmp	r0, #0
	ldr	r0, [sp, #12]
	movne	r0, r9
	movne	r7, r1
	str	r0, [sp, #16]
	str	r0, [sp, #12]
	movw	r0, #65535
	cmp	r3, r0
	cmpne	ip, #2
	bls	.L1827
	ldr	ip, [r4, #1084]
	lsl	r0, r3, #2
	ldrh	r2, [r2, #92]
	ldrh	r0, [ip, r0]
	add	r2, r2, #4
	ubfx	r0, r0, #0, #11
	cmp	r0, r2
	bgt	.L1827
	mov	r10, r9
	str	r0, [sp]
	mov	r2, r1
	ldr	r0, .L1843+28
	mov	r1, r6
	bl	printk
.L1827:
	ldrh	r2, [r8]
	cmp	r2, #0
	bne	.L1828
	ldr	r1, [r4, #2800]
	add	r9, r6, #208
	lsl	r3, r9, #1
	movw	r0, #65535
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L1828
	ldr	r0, .L1843+32
	ldr	r0, [r0]
	tst	r0, #4096
	beq	.L1829
	add	r1, r1, #688
	ldr	r0, .L1843+36
	ldrh	r1, [r1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1829:
	ldr	r3, [r4, #2800]
	lsl	r9, r9, #1
	ldrh	r0, [r3, r9]
	bl	ftl_free_sblk
	ldr	r3, [r4, #2800]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	sub	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L1828:
	add	r6, r6, #1
	add	r8, r8, #2
	uxth	r3, r6
	cmp	fp, r3
	bhi	.L1830
	ldr	r3, [sp, #16]
	movw	r0, #65535
	cmp	r10, r0
	movne	r3, r10
	mov	r0, r3
	add	sp, sp, #284
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1824:
	ldr	r0, [r10], #4
	mov	r1, r2
	str	r2, [sp, #16]
	and	r0, r8, r0, lsr r9
	bl	__aeabi_uidiv
	ldr	r2, [sp, #16]
	uxth	r0, r0
	add	lr, r7, #416
	add	r1, sp, #24
	mov	r3, #0
.L1823:
	ldrh	ip, [lr], #2
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r1, #2
	cmp	r0, ip
	ldrheq	ip, [r1, #-2]
	addeq	ip, ip, #1
	strheq	ip, [r1, #-2]	@ movhi
	cmp	fp, r3
	bne	.L1823
	add	r6, r6, #1
	b	.L1821
.L1844:
	.align	2
.L1843:
	.word	.LANCHOR3
	.word	.LANCHOR1+1707
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3-3136
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3096
	.word	.LC133
	.word	.LANCHOR2
	.word	.LC134
	.fnend
	.size	pm_free_sblk, .-pm_free_sblk
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
	.global	flash_info_data_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_info_data_init, %function
flash_info_data_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1852
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1847
	ldr	r2, .L1852+4
	mov	r1, #120
	ldr	r0, .L1852+8
	bl	printk
.L1847:
	ldr	r4, .L1852+12
	mov	r2, #2048
	mov	r1, #0
	ldr	r0, [r4, #1040]
	bl	ftl_memset
	ldr	r3, [r4, #1040]
	ldr	r2, .L1852+16
	ldr	r1, .L1852+20
	str	r2, [r3]
	mov	r3, #2032
	ldr	r0, [r4, #1040]
	mov	r2, #32
	str	r3, [r0, #8]
	mov	r3, #1
	strh	r3, [r0, #16]	@ movhi
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1040]
	mov	r2, #32
	ldr	r1, .L1852+24
	pop	{r4, lr}
	add	r0, r0, #48
	b	ftl_memcpy
.L1853:
	.align	2
.L1852:
	.word	.LANCHOR2
	.word	.LANCHOR1+1720
	.word	.LC135
	.word	.LANCHOR0
	.word	1398362953
	.word	.LANCHOR0+1111
	.word	.LANCHOR2+4
	.fnend
	.size	flash_info_data_init, .-flash_info_data_init
	.align	2
	.global	ftl_memcpy32
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #0
.L1855:
	cmp	r3, r2
	bne	.L1856
	bx	lr
.L1856:
	ldr	ip, [r1, r3, lsl #2]
	str	ip, [r0, r3, lsl #2]
	add	r3, r3, #1
	b	.L1855
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
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
	.global	timer_get_time
	.syntax unified
	.arm
	.fpu softvfp
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1859
	ldr	r0, [r3]
	ldr	r3, .L1859+4
	ldr	r3, [r3, #-168]
	sub	r0, r0, r3
	b	jiffies_to_msecs
.L1860:
	.align	2
.L1859:
	.word	jiffies
	.word	.LANCHOR3
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	StorageSysDataLoad
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r1
	mov	r5, r0
	mov	r2, #512
	mov	r1, #0
	mov	r0, r4
	bl	ftl_memset
	bl	rknand_device_lock
	ldr	r3, .L1863
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #12]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1864:
	.align	2
.L1863:
	.word	.LANCHOR3
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	StorageSysDataStore
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r4, r0
	bl	rknand_device_lock
	ldr	r3, .L1867
	mov	r2, r5
	mov	r1, #1
	mov	r0, r4
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #16]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1868:
	.align	2
.L1867:
	.word	.LANCHOR3
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FlashBootVendorRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	bl	rknand_device_lock
	ldr	r3, .L1871
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #4]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1872:
	.align	2
.L1871:
	.word	.LANCHOR3
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	FlashBootVendorWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	bl	rknand_device_lock
	ldr	r3, .L1875
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #8]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1876:
	.align	2
.L1875:
	.word	.LANCHOR3
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	flash_sram_load_store
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_sram_load_store, %function
flash_sram_load_store:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1882
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #-156]
	add	ip, ip, #4096
	add	ip, ip, r1
	moveq	r1, ip
	strne	lr, [sp, #-4]!
	.save {lr}
	movne	r1, r0
	ldrne	lr, [sp], #4
	movne	r2, r3
	movne	r0, ip
.L1881:
	b	ftl_memcpy
.L1883:
	.align	2
.L1882:
	.word	.LANCHOR3
	.fnend
	.size	flash_sram_load_store, .-flash_sram_load_store
	.align	2
	.global	FlashCs123Init
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashCs123Init, %function
FlashCs123Init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	FlashCs123Init, .-FlashCs123Init
	.align	2
	.global	ftl_dma32_malloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #8192
	ble	.L1886
	b	ftl_malloc
.L1886:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r0, #63
	ldr	r5, .L1890
	bic	r4, r4, #63
	ldr	r3, [r5, #-152]
	cmp	r4, r3
	ble	.L1887
	mov	r0, #16384
	bl	ftl_malloc
	mov	r3, #16384
	str	r0, [r5, #-148]
	str	r3, [r5, #-152]
.L1887:
	ldr	r3, [r5, #-152]
	ldr	r0, [r5, #-148]
	sub	r3, r3, r4
	add	r4, r0, r4
	str	r3, [r5, #-152]
	str	r4, [r5, #-148]
	pop	{r4, r5, r6, pc}
.L1891:
	.align	2
.L1890:
	.word	.LANCHOR3
	.fnend
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	2
	.global	nandc_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_init, %function
nandc_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	mov	r3, #0
	ldr	r5, .L1912
	mov	r6, r0
	str	r3, [sp, #12]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1893
	mov	r2, r0
	ldr	r1, .L1912+4
	ldr	r0, .L1912+8
	bl	printk
.L1893:
	ldr	r4, .L1912+12
	mov	r3, #6
	ldr	r2, [r6, #352]
	strb	r3, [r4, #1028]
	ldr	r3, .L1912+16
	str	r6, [r4, #1044]
	cmp	r2, r3
	ldr	r2, [r6, #128]
	moveq	r3, #8
	strbeq	r3, [r4, #1028]
	ldr	r3, .L1912+20
	cmp	r2, r3
	ldr	r2, .L1912+24
	moveq	r3, #9
	strbeq	r3, [r4, #1028]
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L1896
	mov	r3, #1
	mov	r1, #2
	strb	r3, [r4, #1195]
	ldr	r3, [sp, #12]
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r1, #18, #3
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r6]
	mov	r3, #0
	ldr	r0, [r4, #1044]
	str	r3, [r0, #520]
	movw	r3, #4161
	str	r3, [r0, #4]
	movw	r3, #8321
	str	r3, [r0, #8]
	mov	r3, #38
	str	r2, [r0, #80]
	str	r3, [r0, #84]
	mov	r3, #39
	str	r3, [r0, #84]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1898
	ldr	r1, [r0]
	ldr	r2, [r0, #8]
	ldr	r3, [r0, #80]
	ldr	ip, [r0, #84]
	ldr	r0, [r0, #88]
.L1911:
	str	r0, [sp, #4]
	str	ip, [sp]
	ldr	r0, .L1912+28
	bl	printk
.L1898:
	mov	r3, #1
	movw	r2, #1228
	strb	r3, [r4, #1196]
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #1193]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1892
	ldrb	r1, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, .L1912+32
	bl	printk
.L1892:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1896:
	ldr	r3, [sp, #12]
	mov	r0, #1
	mov	r1, #0
	strb	r1, [r4, #1195]
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r0, #24, #3
	mov	r0, #2048
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r6]
	ldr	r3, [r4, #1044]
	str	r1, [r3, #336]
	movw	r1, #4193
	str	r1, [r3, #4]
	movw	r1, #8321
	str	r1, [r3, #344]
	str	r2, [r3, #304]
	mov	r2, #38
	str	r2, [r3, #308]
	mov	r2, #39
	str	r2, [r3, #308]
	bl	ftl_dma32_malloc
	ldr	r3, [r5]
	str	r0, [r4, #1200]
	tst	r3, #4096
	beq	.L1898
	ldr	r0, [r4, #1044]
	ldr	r1, [r0]
	ldr	r2, [r0, #344]
	ldr	r3, [r0, #304]
	ldr	ip, [r0, #308]
	ldr	r0, [r0, #312]
	b	.L1911
.L1913:
	.align	2
.L1912:
	.word	.LANCHOR2
	.word	.LANCHOR1+1741
	.word	.LC136
	.word	.LANCHOR0
	.word	1446522928
	.word	1446588464
	.word	1052675
	.word	.LC137
	.word	.LC138
	.fnend
	.size	nandc_init, .-nandc_init
	.align	2
	.global	zbuf_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	zbuf_init, %function
zbuf_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, #0
	ldr	r4, .L1918
	mov	r6, r5
	ldr	r7, .L1918+4
.L1915:
	ldrb	r0, [r7, #-2546]	@ zero_extendqisi2
	uxtb	r3, r5
	strb	r6, [r4, #2]
	add	r5, r5, #1
	add	r2, r3, #1
	strb	r3, [r4, #1]
	strb	r2, [r4]
	add	r4, r4, #48
	lsl	r0, r0, #9
	str	r6, [r4, #-40]
	bl	ftl_dma32_malloc
	str	r0, [r4, #-44]
	mov	r0, #64
	bl	ftl_dma32_malloc
	cmp	r5, #32
	str	r0, [r4, #-36]
	bne	.L1915
	ldr	r3, .L1918+8
	mvn	r2, #0
	strb	r2, [r3, #2720]
	strb	r6, [r3, #2768]
	strb	r5, [r3, #2769]
	pop	{r4, r5, r6, r7, r8, pc}
.L1919:
	.align	2
.L1918:
	.word	.LANCHOR0+1232
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	zbuf_init, .-zbuf_init
	.align	2
	.global	gc_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_init, %function
gc_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r9, #2824
	ldr	r6, .L1922
	mov	r5, #0
	movw	r2, #2204
	mov	r1, r5
	ldr	r4, .L1922+4
	add	r7, r6, r9
	mov	r0, r7
	sub	r8, r4, #3088
	strb	r5, [r4, #-3119]
	strb	r5, [r4, #-144]
	str	r5, [r4, #-140]
	strh	r5, [r8, #-6]	@ movhi
	bl	ftl_memset
	mvn	r3, #0
	ldrb	r0, [r4, #-3127]	@ zero_extendqisi2
	strh	r3, [r6, r9]	@ movhi
	movw	ip, #2806
	ldrh	r3, [r8, #-8]
	str	r5, [r6, #2832]
	lsr	r2, r3, #1
	lsr	r1, r3, #2
	strh	r2, [r7, #34]	@ movhi
	smulbb	r2, r0, r3
	strh	r1, [r7, #32]	@ movhi
	sub	r7, r4, #3104
	strh	r5, [r7, #-8]	@ movhi
	uxth	r2, r2
	strh	r5, [r7, #-6]	@ movhi
	strh	r5, [r7, #-4]	@ movhi
	sub	lr, r2, #32
	strh	lr, [r6, ip]	@ movhi
	movw	ip, #2808
	strh	r2, [r6, ip]	@ movhi
	add	r2, r6, #2816
	strh	r3, [r2]	@ movhi
	mov	r3, #4
	strh	r3, [r4, #-136]	@ movhi
	movw	r3, #2818
	strh	r1, [r6, r3]	@ movhi
	ldrh	r3, [r7, #-14]
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_dma32_malloc
	ldrh	r3, [r7, #-14]
	str	r0, [r4, #-132]
	ldrb	r0, [r4, #-3127]	@ zero_extendqisi2
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_dma32_malloc
	ldrh	r3, [r7, #-14]
	str	r0, [r4, #-128]
	ldrb	r0, [r4, #-3127]	@ zero_extendqisi2
	mul	r0, r0, r3
	bl	ftl_dma32_malloc
	ldrh	r3, [r7, #-14]
	str	r0, [r4, #-3124]
	ldrb	r0, [r4, #-3127]	@ zero_extendqisi2
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_dma32_malloc
	ldrh	r3, [r7, #-14]
	str	r0, [r4, #-3132]
	ldrb	r0, [r4, #-3127]	@ zero_extendqisi2
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_dma32_malloc
	ldrh	r3, [r8, #-4]
	movw	r2, #2804
	str	r0, [r4, #-124]
	lsr	r3, r3, #2
	strh	r3, [r6, r2]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1923:
	.align	2
.L1922:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_init, .-gc_init
	.align	2
	.global	rk_ftl_de_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r0, .L1926
	bl	printk
	ldr	r3, .L1926+4
	pop	{r4, lr}
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #40]
	bx	r3	@ indirect register sibling call
.L1927:
	.align	2
.L1926:
	.word	.LC139
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1929
	mov	r0, #0
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #32]
	bx	r3	@ indirect register sibling call
.L1930:
	.align	2
.L1929:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	2
	.global	rk_nand_suspend
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1932
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #44]
	bx	r3	@ indirect register sibling call
.L1933:
	.align	2
.L1932:
	.word	.LANCHOR3
	.fnend
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_nand_resume, %function
rk_nand_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1935
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #48]
	bx	r3	@ indirect register sibling call
.L1936:
	.align	2
.L1935:
	.word	.LANCHOR3
	.fnend
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1938
	mov	r0, #0
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #36]
	bx	r3
.L1939:
	.align	2
.L1938:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1941
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #60]
	bx	r3
.L1942:
	.align	2
.L1941:
	.word	.LANCHOR3
	.fnend
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
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
	@ link register save eliminated.
	ldr	r3, .L1944
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #64]
	bx	r3
.L1945:
	.align	2
.L1944:
	.word	.LANCHOR3
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1948
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	ip, [ip, #-160]
	ldr	lr, [ip, #20]
	mov	ip, lr
	ldr	lr, [sp], #4
	bx	ip
.L1949:
	.align	2
.L1948:
	.word	.LANCHOR3
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1951
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #28]
	bx	r3
.L1952:
	.align	2
.L1951:
	.word	.LANCHOR3
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1954
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #52]
	bx	r3
.L1955:
	.align	2
.L1954:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1957
	ldr	r3, [r3, #-160]
	ldr	r3, [r3, #56]
	bx	r3	@ indirect register sibling call
.L1958:
	.align	2
.L1957:
	.word	.LANCHOR3
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r2
	push	{r0, r1, r2, r3, r4, lr}
	.save {lr}
	.pad #20
	mul	r3, r3, ip
	mov	r2, #0
	str	r2, [sp, #12]
	str	ip, [sp]
	stmib	sp, {r1, r3}
	mov	r1, r0
	mov	r3, #16
	ldr	r0, .L1961
	bl	print_hex_dump
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1962:
	.align	2
.L1961:
	.word	.LC140
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	hynix_get_read_retry_default
	.syntax unified
	.arm
	.fpu softvfp
	.type	hynix_get_read_retry_default, %function
hynix_get_read_retry_default:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r6, .L2074
	mvn	r3, #83
	cmp	r5, #2
	mvn	r2, #81
	.pad #52
	sub	sp, sp, #52
	ldr	r4, [r6, #1040]
	strb	r0, [r4, #112]
	mvn	r0, #82
	strb	r3, [r4, #128]
	mvn	r3, #80
	add	r1, r4, #128
	strb	r0, [r4, #129]
	strb	r2, [r4, #130]
	strb	r3, [r4, #131]
	bne	.L1964
	mvn	r3, #88
	mov	r10, #7
	strb	r3, [r4, #128]
	mvn	r2, #8
	ldr	r3, .L2074+4
	strb	r2, [r3, #407]
.L2068:
	mov	fp, #4
	b	.L1965
.L1964:
	cmp	r5, #3
	bne	.L1966
	add	r2, r4, #127
	mov	r3, #176
.L1967:
	strb	r3, [r2, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1967
.L2069:
	mov	r10, #8
	mov	fp, r10
.L1965:
	sub	r3, r5, #1
	cmp	r3, #1
	bhi	.L1973
	sub	r9, fp, #1
	mov	r8, #0
	uxtab	r3, r1, r9
	str	r3, [sp, #4]
.L1974:
	ldrb	r2, [r6, #1109]	@ zero_extendqisi2
	uxtb	r3, r8
	cmp	r2, r3
	bhi	.L1979
.L1980:
	strb	fp, [r4, #113]
	strb	r10, [r4, #114]
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1966:
	cmp	r5, #4
	bne	.L1968
	mvn	ip, #51
	strb	r0, [r4, #133]
	strb	ip, [r4, #128]
	mvn	ip, #64
	strb	ip, [r4, #129]
	mvn	ip, #85
	strb	ip, [r4, #130]
	mvn	ip, #84
	strb	ip, [r4, #131]
	mvn	ip, #50
	strb	ip, [r4, #132]
	strb	r2, [r4, #134]
	strb	r3, [r4, #135]
	b	.L2069
.L1968:
	cmp	r5, #5
	bne	.L1969
	mov	r3, #56
	mov	r10, #8
	strb	r3, [r4, #128]
	mov	r3, #57
	strb	r3, [r4, #129]
	mov	r3, #58
	strb	r3, [r4, #130]
	mov	r3, #59
	strb	r3, [r4, #131]
	b	.L2068
.L1969:
	cmp	r5, #6
	bne	.L1970
	mov	r3, #14
	mov	r10, #12
	strb	r3, [r4, #128]
	mov	r3, #15
	strb	r3, [r4, #129]
	mov	r3, #16
	strb	r3, [r4, #130]
	mov	r3, #17
	strb	r3, [r4, #131]
	b	.L2068
.L1970:
	cmp	r5, #7
	bne	.L1971
	add	r2, r4, #127
	mov	r3, #176
.L1972:
	strb	r3, [r2, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1972
	mvn	r3, #43
	mov	r10, #12
	strb	r3, [r4, #136]
	mvn	r3, #42
	strb	r3, [r4, #137]
	mov	fp, #10
	b	.L1965
.L1971:
	cmp	r5, #8
	mov	r3, #7
	movne	r10, r3
	bne	.L2068
	strb	r3, [r4, #129]
	mov	r3, #9
	mov	r2, #6
	strb	r3, [r4, #131]
	mov	r3, #10
	strb	r2, [r4, #128]
	strb	r5, [r4, #130]
	mov	r10, #50
	strb	r3, [r4, #132]
	mov	fp, #5
	b	.L1965
.L1979:
	add	r3, r6, r3
	mov	r5, #160
	ldrb	r3, [r3, #1144]	@ zero_extendqisi2
	add	r1, r4, #127
	ldr	r7, [r6, #1044]
	mla	r5, r5, r3, r4
	add	r7, r7, r3, lsl #8
	mov	r3, #55
	add	r5, r5, #144
	sub	r9, r5, #1
.L1975:
	str	r3, [r7, #2056]
	mov	r0, #200
	ldrb	r2, [r1, #1]!	@ zero_extendqisi2
	str	r3, [sp, #12]
	str	r2, [r7, #2052]
	str	r1, [sp, #8]
	bl	ndelay
	ldr	r3, [sp, #4]
	ldr	r1, [sp, #8]
	ldr	r2, [r7, #2048]
	cmp	r3, r1
	ldr	r3, [sp, #12]
	strb	r2, [r9, #1]!
	bne	.L1975
	ldr	lr, .L2074+8
	mov	r1, r5
	mov	r2, #0
.L1976:
	mov	r3, #1
	add	ip, lr, r2
.L1977:
	ldrb	r0, [ip, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r7, [r1]	@ zero_extendqisi2
	add	r0, r0, r7
	strb	r0, [r1, r3, lsl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1977
	add	r2, r2, #1
	add	r1, r1, #1
	cmp	r2, #4
	bne	.L1976
	mov	r3, #0
	add	r8, r8, #1
	strb	r3, [r5, #16]
	strb	r3, [r5, #24]
	strb	r3, [r5, #32]
	strb	r3, [r5, #40]
	strb	r3, [r5, #48]
	strb	r3, [r5, #41]
	strb	r3, [r5, #49]
	b	.L1974
.L1973:
	sub	r3, r5, #3
	cmp	r3, #5
	bhi	.L1980
	smulbb	r3, fp, r10
	asr	r2, r3, #1
	lsl	r3, r3, #4
	str	r3, [sp, #44]
	lsl	r3, r2, #2
	str	r2, [sp, #4]
	str	r3, [sp, #36]
	lsl	r3, r2, #1
	str	r3, [sp, #20]
	mov	r3, #0
.L2073:
	str	r3, [sp, #16]
	ldr	r3, .L2074
	ldrb	r2, [r3, #1109]	@ zero_extendqisi2
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1980
	ldr	r2, .L2074
	mov	r7, #255
	add	r3, r2, r3
	ldrb	r8, [r3, #1144]	@ zero_extendqisi2
	mov	r0, r8
	bl	zftl_flash_exit_slc_mode
	mov	r0, #160
	mla	r0, r0, r8, r4
	add	r3, r0, #144
	str	r3, [sp, #32]
	ldr	r3, .L2074
	ldr	r9, [r3, #1044]
	add	r6, r9, r8, lsl #8
	str	r7, [r6, #2056]
	bl	nandc_wait_flash_ready
	cmp	r5, #8
	bne	.L1982
	add	r3, r4, #144
	mov	r2, #23
	str	r3, [sp, #32]
	mov	r3, #120
	str	r3, [r6, #2056]
	mov	r3, #0
	str	r3, [r6, #2052]
	mov	r1, #25
	str	r3, [r6, #2052]
	str	r3, [r6, #2052]
	str	r2, [r6, #2056]
	mov	r2, #4
	str	r2, [r6, #2056]
	str	r1, [r6, #2056]
	mov	r1, #218
	str	r1, [r6, #2056]
	mov	r1, #21
	str	r3, [r6, #2056]
	str	r3, [r6, #2052]
	str	r3, [r6, #2052]
	str	r1, [r6, #2052]
	str	r2, [r6, #2052]
	str	r3, [r6, #2052]
.L1983:
	add	r3, r9, r8, lsl #8
	mov	r2, #48
	str	r2, [r3, #2056]
	bl	nandc_wait_flash_ready
	sub	r3, r5, #5
	cmp	r5, #8
	cmpne	r3, #1
	str	r3, [sp, #40]
	movls	r2, #16
	bls	.L1989
	cmp	r5, #7
	movne	r2, #2
	moveq	r2, #32
.L1989:
	ldr	r3, .L2074+12
	sub	r2, r2, #1
	add	r6, r9, r8, lsl #8
	ldr	r3, [r3, #-120]
	sub	r1, r3, #1
	uxtab	r2, r3, r2
	mov	r0, r1
.L1990:
	ldr	ip, [r6, #2048]
	strb	ip, [r0, #1]!
	cmp	r0, r2
	bne	.L1990
	cmp	r5, #8
	bne	.L1991
	mov	r2, #0
.L1993:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #50
	beq	.L1992
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #5
	beq	.L1992
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1993
.L1994:
	mov	r1, #0
	ldr	r0, .L2074+16
	bl	printk
.L1996:
	b	.L1996
.L1982:
	cmp	r5, #4
	mov	r3, #54
	str	r3, [r6, #2056]
	bne	.L1984
	mov	r3, #64
	str	r7, [r6, #2052]
	str	r3, [r6, #2048]
	mov	r3, #204
.L2070:
	str	r3, [r6, #2052]
	mov	r3, #77
.L2071:
	str	r3, [r6, #2048]
.L1985:
	add	r3, r9, r8, lsl #8
	mov	r2, #22
	cmp	r5, #6
	str	r2, [r3, #2056]
	mov	r2, #23
	str	r2, [r3, #2056]
	mov	r2, #4
	str	r2, [r3, #2056]
	mov	r2, #25
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	moveq	r2, #31
	str	r2, [r3, #2052]
	mov	r2, #2
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2052]
	b	.L1983
.L1984:
	sub	r3, r5, #5
	cmp	r3, #1
	ldrbls	r3, [r4, #128]	@ zero_extendqisi2
	strls	r3, [r6, #2052]
	movls	r3, #82
	bls	.L2071
	cmp	r5, #7
	bne	.L1985
	mov	r3, #174
	str	r3, [r6, #2052]
	mov	r3, #0
	str	r3, [r6, #2048]
	mov	r3, #176
	b	.L2070
.L1992:
	cmp	r1, #6
	bhi	.L1994
.L1995:
	ldr	r3, .L2074+12
	ldr	r2, [r3, #-120]
	mov	r3, r2
.L2005:
	ldr	r0, [sp, #44]
	sub	r1, r3, r2
	cmp	r0, r1
	bgt	.L2006
	ldr	r3, .L2074+12
	ldr	r1, [r3, #-120]
	ldr	r3, [sp, #20]
	add	r0, r1, r3
	mov	r3, #8
.L2008:
	mov	lr, r0
	mov	ip, #0
.L2007:
	ldrh	r7, [lr]
	add	ip, ip, #1
	mvn	r7, r7
	strh	r7, [lr], #2	@ movhi
	ldr	r7, [sp, #4]
	cmp	r7, ip
	bgt	.L2007
	ldr	ip, [sp, #36]
	subs	r3, r3, #1
	add	r0, r0, ip
	bne	.L2008
	str	r1, [sp, #8]
	str	r3, [sp, #12]
.L2009:
	mov	ip, #0
	mov	r0, ip
.L2013:
	mov	lr, #1
	mov	r7, #16
	lsl	lr, lr, r0
	str	r7, [sp, #28]
	mov	r7, #0
	str	lr, [sp, #24]
	ldr	lr, [sp, #8]
.L2011:
	ldrh	r3, [lr]
	mov	r1, r3
	ldr	r3, [sp, #24]
	bics	r3, r3, r1
	ldr	r3, [sp, #20]
	addeq	r7, r7, #1
	add	lr, lr, r3
	ldr	r3, [sp, #28]
	subs	r3, r3, #1
	str	r3, [sp, #28]
	bne	.L2011
	cmp	r7, #8
	add	r0, r0, #1
	ldrhi	r3, [sp, #24]
	orrhi	ip, ip, r3
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L2013
	ldr	r3, [sp, #8]
	strh	ip, [r3], #2	@ movhi
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
	cmp	r3, r1
	bgt	.L2009
	ldr	r3, .L2074+12
	ldr	r1, [r3, #-120]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L2016:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	ip, r0
	bne	.L2016
	cmp	r3, #7
	ble	.L2017
	ldr	r0, .L2074+20
	mov	r3, #1024
	mov	r2, #1
	bl	rknand_print_hex
	mov	r1, #0
	ldr	r0, .L2074+16
	bl	printk
.L2018:
	b	.L2018
.L1991:
	cmp	r5, #7
	bne	.L1997
	mov	r2, #0
.L1999:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L1998
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L1998
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1999
.L2000:
	mov	r1, #0
	ldr	r0, .L2074+16
	bl	printk
.L2001:
	b	.L2001
.L1998:
	cmp	r1, #6
	bls	.L1995
	b	.L2000
.L1997:
	cmp	r5, #6
	bne	.L1995
	add	r3, r3, #7
.L2002:
	ldrb	r2, [r1, #1]!	@ zero_extendqisi2
	cmp	r2, #12
	beq	.L1995
	ldrb	r2, [r1, #8]	@ zero_extendqisi2
	cmp	r2, #4
	beq	.L1995
	cmp	r1, r3
	bne	.L2002
	mov	r1, #0
	ldr	r0, .L2074+16
	bl	printk
.L2004:
	b	.L2004
.L2006:
	ldr	r1, [r6, #2048]
	strb	r1, [r3], #1
	b	.L2005
.L2017:
	cmp	r5, #6
	moveq	ip, #4
	beq	.L2019
	cmp	r5, #7
	moveq	ip, #10
	beq	.L2019
	cmp	r5, #8
	movne	ip, #8
	moveq	ip, #5
.L2019:
	sub	r3, fp, #1
	ldr	r0, [sp, #32]
	uxtb	r3, r3
	mov	lr, #0
	add	r3, r3, #1
	str	r3, [sp, #8]
.L2020:
	mov	r3, r0
	mov	r1, r2
.L2021:
	ldrb	r7, [r1], #1	@ zero_extendqisi2
	strb	r7, [r3], #1
	sub	r7, r1, r2
	uxtb	r7, r7
	cmp	fp, r7
	bhi	.L2021
	ldr	r3, [sp, #8]
	add	lr, lr, #1
	cmp	r10, lr
	add	r0, r0, ip
	add	r2, r2, r3
	bgt	.L2020
	add	r7, r9, r8, lsl #8
	mov	r8, #255
	str	r8, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #40]
	cmp	r3, #1
	bhi	.L2023
	mov	r3, #54
	mov	r2, #22
	str	r3, [r7, #2056]
	ldrb	r3, [r4, #128]	@ zero_extendqisi2
	str	r3, [r6, #2052]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r2, [r7, #2056]
	str	r3, [r7, #2056]
	str	r3, [r6, #2052]
	str	r3, [r6, #2052]
	mov	r3, #48
	str	r8, [r6, #2052]
	str	r8, [r6, #2052]
	str	r8, [r6, #2052]
.L2072:
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L2073
.L2023:
	cmp	r5, #8
	moveq	r3, #190
	movne	r3, #56
	b	.L2072
.L2075:
	.align	2
.L2074:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+390
	.word	.LANCHOR3
	.word	.LC141
	.word	.LC142
	.fnend
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2078
	ldrb	r0, [r3, #23]	@ zero_extendqisi2
	sub	r3, r0, #1
	cmp	r3, #7
	bxhi	lr
	b	hynix_get_read_retry_default
.L2079:
	.align	2
.L2078:
	.word	.LANCHOR2
	.fnend
	.size	flash_get_read_retry_tbl, .-flash_get_read_retry_tbl
	.align	2
	.global	nandc_xfer_done
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_xfer_done, %function
nandc_xfer_done:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r1, #0
	ldr	r4, .L2129
	ldr	r7, .L2129+4
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r6, [r4, #1044]
	strb	r1, [r7, #-116]
	cmp	r3, #9
	bne	.L2081
	ldr	r3, [r6, #16]
	str	r3, [sp]
	ldr	r5, [r6, #48]
	ubfx	r5, r5, #1, #1
	cmp	r5, r1
	bne	.L2082
	ldr	r7, .L2129+8
	ldr	r8, .L2129+12
.L2083:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2093
	ldr	r3, [r4, #1220]
	cmp	r3, #0
	beq	.L2091
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r4, #1212]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #1
	ubfx	r1, r1, #22, #6
	b	.L2128
.L2082:
	mov	r5, r1
.L2084:
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #6
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2086
	ldr	r3, [r4, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2085
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2085
	ldr	r1, [sp, #4]
	ldr	r0, .L2129+16
	ubfx	r1, r1, #17, #1
	bl	printk
.L2086:
	ldr	r3, [r4, #1220]
	cmp	r3, #0
	beq	.L2091
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #1212]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #0
	ubfx	r1, r1, #22, #6
.L2128:
	lsl	r1, r1, #2
.L2126:
	ldr	r0, [r4, #1216]
	bl	rknand_dma_unmap_single
.L2091:
	mov	r3, #0
	str	r3, [r4, #1220]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2085:
	ldr	r3, [sp]
	add	r5, r5, #1
	ubfx	r3, r3, #22, #6
	cmp	r5, r3, lsl #12
	bne	.L2087
	ldr	r2, [r6, #64]
	mov	r1, r5
	ldr	r3, [sp]
	ldr	r0, .L2129+20
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r3, [sp, #4]
	tst	r3, #8192
	mov	r3, #1
	strb	r3, [r7, #-116]
	bne	.L2086
	ldr	r3, .L2129+24
	ldr	r0, .L2129+28
	ldr	r3, [r3, #4]
	blx	r3
	b	.L2086
.L2087:
	mov	r1, #10
	mov	r0, #5
	bl	usleep_range
	b	.L2084
.L2093:
	ldr	r3, [r4, #1044]
	add	r5, r5, #1
	ldr	r3, [r3, #16]
	str	r3, [sp]
	bics	r3, r5, #-16777216
	bne	.L2092
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #64]
	mov	r0, r7
	ubfx	r3, r3, #16, #6
	bl	printk
	mov	r3, #64
	mov	r2, #4
	ldr	r1, [r4, #1044]
	mov	r0, r8
	bl	rknand_print_hex
.L2092:
	mov	r1, #10
	mov	r0, #5
	bl	usleep_range
	b	.L2083
.L2081:
	ldr	r3, [r6, #8]
	str	r3, [sp]
	ldr	r5, [r6, #16]
	ubfx	r5, r5, #1, #1
	cmp	r5, #0
	bne	.L2095
	ldr	r7, .L2129+8
	ldr	r8, .L2129+12
.L2096:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2103
	ldr	r3, [r4, #1220]
	cmp	r3, #0
	beq	.L2091
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r4, #1212]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #1
	ubfx	r1, r1, #22, #6
	b	.L2127
.L2095:
	ldr	r7, .L2129+20
	mov	r5, r1
	ldr	r8, .L2129+12
.L2097:
	ldr	r2, [r6, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2099
	ldr	r3, [r4, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2098
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2098
	ldr	r1, [sp, #4]
	ldr	r0, .L2129+32
	bl	printk
.L2099:
	ldr	r3, [r4, #1220]
	cmp	r3, #0
	beq	.L2091
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #1212]
	ubfx	r1, r1, #22, #6
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #0
	ubfx	r1, r1, #22, #6
.L2127:
	lsl	r1, r1, #7
	b	.L2126
.L2098:
	add	r5, r5, #1
	bics	r3, r5, #-16777216
	bne	.L2100
	ldr	r2, [r6, #28]
	mov	r1, r5
	ldr	r3, [sp]
	mov	r0, r7
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	mov	r3, #64
	mov	r2, #4
	ldr	r1, [r4, #1044]
	mov	r0, r8
	bl	rknand_print_hex
.L2100:
	mov	r1, #10
	mov	r0, #5
	bl	usleep_range
	b	.L2097
.L2103:
	ldr	r3, [r4, #1044]
	add	r5, r5, #1
	ldr	r3, [r3, #8]
	str	r3, [sp]
	bics	r3, r5, #-16777216
	bne	.L2102
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #28]
	mov	r0, r7
	ubfx	r3, r3, #16, #5
	bl	printk
	mov	r3, #64
	mov	r2, #4
	ldr	r1, [r4, #1044]
	mov	r0, r8
	bl	rknand_print_hex
.L2102:
	mov	r1, #10
	mov	r0, #5
	bl	usleep_range
	b	.L2096
.L2130:
	.align	2
.L2129:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC145
	.word	.LC146
	.word	.LC143
	.word	.LC144
	.word	arm_delay_ops
	.word	644245000
	.word	.LC147
	.fnend
	.size	nandc_xfer_done, .-nandc_xfer_done
	.align	2
	.global	nandc_xfer
	.syntax unified
	.arm
	.fpu softvfp
	.type	nandc_xfer, %function
nandc_xfer:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r4, r1
	ldr	r5, [sp, #32]
	mov	r8, r2
	mov	r6, r3
	mov	r1, r8
	mov	r2, r6
	mov	r0, r4
	mov	r3, r5
	bl	nandc_xfer_start
	mov	r0, r4
	bl	nandc_xfer_done
	cmp	r4, #0
	movne	r0, #0
	bne	.L2132
	ldr	r7, .L2170
	ldrb	r3, [r7, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2133
	ldr	r1, [r7, #1044]
	lsr	r8, r8, #2
	mov	r2, #1
	mov	r0, r4
.L2134:
	cmp	r4, r8
	bcc	.L2138
	ldr	r4, [r1]
	cmp	r2, #0
	movne	r0, #512
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2140
	mov	r1, r4
	ldr	r0, .L2170+4
	bl	printk
	ldr	r3, [r7, #1044]
	mvn	r0, #0
	orr	r4, r4, #131072
	str	r4, [r3]
.L2140:
	tst	r4, #8192
	beq	.L2141
	ldr	r3, .L2170+8
	ldrb	r3, [r3, #-116]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2141
	mov	r1, r4
	ldr	r0, .L2170+12
	bl	printk
	ldr	r3, [r7, #1044]
	mov	r2, #1
	str	r2, [r3, #16]
.L2169:
	mvn	r0, #0
.L2132:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2138:
	add	r3, r4, #84
	ldr	r3, [r1, r3, lsl #2]
	str	r3, [sp, #4]
	ldr	ip, [sp, #4]
	ldr	r3, [sp, #4]
	ubfx	r3, r3, #26, #1
	and	r3, r3, ip, lsr #10
	and	r2, r2, r3
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2152
	ldr	r3, [sp, #4]
	tst	r3, #262144
	bne	.L2152
	ldr	ip, [sp, #4]
	ldr	r3, [sp, #4]
	ubfx	ip, ip, #3, #7
	ubfx	r3, r3, #19, #7
	cmp	ip, r3
	ldr	r3, [sp, #4]
	ubfxgt	r3, r3, #3, #7
	ubfxle	r3, r3, #19, #7
	cmp	r0, r3
	movcc	r0, r3
.L2135:
	add	r4, r4, #1
	b	.L2134
.L2152:
	mvn	r0, #0
	b	.L2135
.L2133:
	ldrb	r3, [r7, #1193]	@ zero_extendqisi2
	lsr	r0, r8, #1
	mov	r2, r5
	mov	r1, r4
	cmp	r3, #25
	movcc	lr, #64
	movcs	lr, #128
.L2143:
	cmp	r1, r0
	add	ip, lr, r4
	add	r2, r2, #4
	bcc	.L2144
	ldr	r1, [r7, #1044]
	mov	r2, #0
	lsr	r8, r8, #2
	mov	r0, r2
.L2145:
	cmp	r2, r8
	bcc	.L2149
	mov	r3, #0
	str	r3, [r1, #16]
	ldr	r4, [r1]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2141
	mov	r1, r4
	ldr	r0, .L2170+16
	bl	printk
	ldr	r3, [r7, #1044]
	orr	r4, r4, #131072
	str	r4, [r3]
	b	.L2169
.L2144:
	ldr	r3, [r7, #1200]
	bic	r4, r4, #3
	add	r1, r1, #1
	ldr	r3, [r3, r4]
	strb	r3, [r2, #-4]
	lsr	r4, r3, #8
	strb	r4, [r2, #-3]
	lsr	r4, r3, #16
	lsr	r3, r3, #24
	strb	r4, [r2, #-2]
	mov	r4, ip
	strb	r3, [r2, #-1]
	b	.L2143
.L2149:
	add	r3, r2, #8
	ldr	r3, [r1, r3, lsl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2155
	ldr	r3, [sp, #4]
	tst	r3, #32768
	bne	.L2155
	ldr	ip, [sp, #4]
	ldr	r4, [sp, #4]
	ldr	r3, [sp, #4]
	ldr	lr, [sp, #4]
	ubfx	ip, ip, #3, #5
	ubfx	r4, r4, #27, #1
	ubfx	r3, r3, #16, #5
	orr	ip, ip, r4, lsl #5
	ubfx	lr, lr, #29, #1
	orr	r3, r3, lr, lsl #5
	cmp	ip, r3
	ldr	r3, [sp, #4]
	ldrhi	ip, [sp, #4]
	ldrls	ip, [sp, #4]
	ubfxhi	r3, r3, #3, #5
	ubfxls	r3, r3, #16, #5
	ubfxhi	ip, ip, #27, #1
	ubfxls	ip, ip, #29, #1
	orr	r3, r3, ip, lsl #5
	cmp	r0, r3
	movcc	r0, r3
.L2146:
	add	r2, r2, #1
	b	.L2145
.L2155:
	mvn	r0, #0
	b	.L2146
.L2141:
	cmn	r0, #1
	beq	.L2132
	ldr	r3, [r5]
	cmn	r3, #1
	bne	.L2132
	ldr	r3, [r5, #4]
	cmn	r3, #1
	bne	.L2132
	ldr	r3, [r6]
	cmn	r3, #1
	moveq	r0, #512
	b	.L2132
.L2171:
	.align	2
.L2170:
	.word	.LANCHOR0
	.word	.LC148
	.word	.LANCHOR3
	.word	.LC149
	.word	.LC150
	.fnend
	.size	nandc_xfer, .-nandc_xfer
	.align	2
	.global	flash_read_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_page, %function
flash_read_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r7, .L2188
	.pad #20
	sub	sp, sp, #20
	ubfx	r9, r1, #24, #2
	mov	r8, r3
	str	r2, [sp, #12]
	lsl	r10, r6, #8
	ldrb	r4, [r7, #1153]	@ zero_extendqisi2
	ldr	r5, [r7, #1044]
	rsb	r0, r4, #24
	mvn	r4, #0
	bic	r4, r1, r4, lsl r0
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r9, #0
	bne	.L2173
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
.L2174:
	ldr	r2, [r7, #1104]
	ldrb	r1, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2176
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	cmp	r1, #2
	addeq	r1, r5, r10
	moveq	r0, #38
	streq	r0, [r1, #2056]
.L2176:
	add	fp, r5, r10
	mov	r1, #0
	str	r1, [fp, #2056]
	mov	r0, #48
	str	r1, [fp, #2052]
	str	r1, [fp, #2052]
	uxtb	r1, r4
	str	r1, [fp, #2052]
	lsr	r1, r4, #8
	str	r1, [fp, #2052]
	lsr	r1, r4, #16
	str	r1, [fp, #2052]
	ldrb	r1, [r7, #1152]	@ zero_extendqisi2
	cmp	r1, #0
	lsrne	r1, r4, #24
	strne	r1, [fp, #2052]
	add	r1, r5, r10
	str	r0, [r1, #2056]
	cmp	r9, #0
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	sub	r2, r2, #3
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L2178
	ldrb	r2, [r7, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2178
	ldrb	r3, [r7, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r4, r4, r4, lsl #1
	subeq	r0, r4, #1
	addeq	r0, r0, r9
	beq	.L2187
.L2178:
	mov	r0, r4
.L2187:
	bl	nandc_set_seed
	bl	nandc_wait_flash_ready
	add	r3, r5, r10
	mov	r2, #5
	str	r2, [r3, #2056]
	mov	r1, #0
	mov	r2, #224
	str	r1, [fp, #2052]
	mov	r0, r6
	str	r1, [fp, #2052]
	str	r2, [r3, #2056]
	ldr	r3, [sp, #12]
	ldrb	r2, [sp, #56]	@ zero_extendqisi2
	str	r8, [sp]
	bl	nandc_xfer
	bl	nandc_de_cs.constprop.35
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2173:
	ldr	r2, [r7, #1104]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L2175
	ldrb	r2, [r7, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2175
	ldrb	r2, [r7, #1159]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r2, r5, r10
	streq	r9, [r2, #2056]
	beq	.L2174
.L2175:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	b	.L2174
.L2189:
	.align	2
.L2188:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_page, .-flash_read_page
	.align	2
	.global	micron_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	micron_read_retrial, %function
micron_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L2219
	mov	r6, r0
	mov	r10, r1
	mov	r9, #0
	str	r3, [sp, #16]
	ldrb	r3, [r4, #1193]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	str	r3, [sp, #8]
	bl	nandc_wait_flash_ready
	ldr	r5, [r4, #1044]
	add	r5, r5, r6, lsl #8
.L2191:
	ldr	fp, .L2219+4
	mov	r0, r6
	mov	r7, #0
	mvn	r4, #0
	bl	zftl_flash_enter_slc_mode
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2192:
	ldrb	r3, [fp, #-108]	@ zero_extendqisi2
	cmp	r7, r3
	bcc	.L2197
.L2196:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #137
	str	r3, [r5, #2052]
	bl	ndelay
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bcc	.L2198
	cmn	r4, #1
	movne	r4, #256
.L2198:
	cmn	r4, #1
	movne	r8, #0
	moveq	r8, #1
	cmp	r4, #256
	movne	r1, r8
	orreq	r1, r8, #1
	cmp	r1, #0
	beq	.L2199
	mov	r1, r6
	str	r4, [sp]
	mov	r3, r7
	mov	r2, r10
	ldr	r0, .L2219+8
	bl	printk
	eor	r1, r9, #1
	ands	r1, r8, r1
	beq	.L2200
	mov	r1, #3
	mov	r0, r6
	bl	mt_auto_read_calibration_config
	mov	r9, #1
	b	.L2191
.L2197:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #137
	str	r3, [r5, #2052]
	bl	ndelay
	add	r3, r7, #1
	ldr	r2, [sp, #12]
	str	r3, [r5, #2048]
	mov	r1, r10
	str	r3, [sp, #20]
	mov	r3, #0
	str	r3, [r5, #2048]
	mov	r0, r6
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	ldr	r3, [sp, #64]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	ldr	r3, .L2219+12
	mov	r8, r0
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2193
	str	r0, [sp]
	mov	r3, r4
	mov	r2, r10
	mov	r1, r7
	ldr	r0, .L2219+16
	bl	printk
.L2193:
	cmn	r8, #1
	beq	.L2194
	ldr	r3, [fp, #-120]
	cmn	r4, #1
	moveq	r4, r8
	str	r3, [sp, #12]
	ldr	r3, [fp, #-112]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	cmp	r8, r3
	bcc	.L2203
.L2194:
	ldr	r7, [sp, #20]
	b	.L2192
.L2203:
	mov	r4, r8
	b	.L2196
.L2200:
	cmp	r9, #0
	beq	.L2201
	mov	r0, r6
	bl	mt_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
.L2201:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2199:
	cmp	r9, #0
	beq	.L2201
	mov	r0, r6
	mov	r4, #256
	bl	mt_auto_read_calibration_config
	b	.L2201
.L2220:
	.align	2
.L2219:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC152
	.word	.LANCHOR2
	.word	.LC151
	.fnend
	.size	micron_read_retrial, .-micron_read_retrial
	.align	2
	.global	toshiba_3d_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_3d_read_retrial, %function
toshiba_3d_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r7, .L2269
	mov	r8, r0
	ubfx	r4, r1, #24, #2
	mov	r9, r1
	str	r3, [sp, #20]
	add	r6, r8, #8
	str	r2, [sp, #16]
	mov	r5, #1
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #1044]
	str	r3, [sp, #12]
	add	r6, r3, r6, lsl #8
	ldrb	r3, [r7, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	movne	r3, #56
	moveq	r3, #46
	str	r3, [sp, #28]
	movne	r3, #10
	moveq	r3, #26
	cmp	r4, #0
	str	r3, [sp, #24]
	mvn	r4, #0
	ldr	r3, [sp, #12]
	add	fp, r3, r8, lsl #8
	bne	.L2239
.L2230:
	ldr	r3, .L2269
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2224
	mov	r2, #0
	mov	r1, r5
	mov	r0, r6
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	str	r3, [fp, #2056]
.L2225:
	ldr	r3, [sp, #72]
	mov	r1, r9
	ldr	r2, [sp, #16]
	mov	r0, r8
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	flash_read_page
	ldr	r3, .L2269+4
	mov	r10, r0
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2226
	mov	r3, r0
	mov	r2, r9
	mov	r1, r5
	ldr	r0, .L2269+8
	bl	printk
.L2226:
	cmn	r10, #1
	beq	.L2227
	ldr	r3, .L2269+12
	cmn	r4, #1
	moveq	r4, r10
	ldr	r2, [r3, #-120]
	ldr	r3, [r3, #-112]
	str	r2, [sp, #16]
	str	r3, [sp, #20]
	ldrb	r3, [r7, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r10, r3, asr #2
	bcc	.L2245
.L2227:
	ldr	r3, [sp, #24]
	add	r5, r5, #1
	cmp	r3, r5
	bne	.L2230
.L2229:
	ldr	r3, .L2269
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	moveq	r2, #0
	beq	.L2268
	mov	r1, #0
	mov	r0, r6
	bl	toshiba_3d_set_slc_rr_para
.L2232:
	ldr	r3, .L2269
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2241
	ldr	r3, [sp, #12]
	mov	r2, #85
	add	r3, r3, r8, lsl #8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2048]
	mov	r2, #255
	str	r2, [r3, #2056]
.L2241:
	ldrb	r3, [r7, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L2242
	cmn	r4, #1
	movne	r4, #256
.L2242:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2243
	str	r4, [sp]
	mov	r3, r5
	mov	r2, r9
	mov	r1, r8
	ldr	r0, .L2269+16
	bl	printk
.L2243:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2224:
	uxtb	r1, r5
	mov	r0, r6
	bl	toshiba_3d_set_slc_rr_para
	b	.L2225
.L2245:
	mov	r4, r10
	b	.L2229
.L2239:
	ldr	r3, .L2269
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2233
	mov	r2, #1
	mov	r1, r5
	mov	r0, r6
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
.L2267:
	str	r3, [fp, #2056]
	mov	r1, r9
	ldr	r3, [sp, #72]
	mov	r0, r8
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	flash_read_page
	ldr	r3, .L2269+4
	mov	r10, r0
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2235
	mov	r3, r0
	mov	r2, r9
	mov	r1, r5
	ldr	r0, .L2269+20
	bl	printk
.L2235:
	cmn	r10, #1
	beq	.L2236
	ldr	r3, .L2269+12
	cmn	r4, #1
	moveq	r4, r10
	ldr	r2, [r3, #-120]
	ldr	r3, [r3, #-112]
	str	r2, [sp, #16]
	str	r3, [sp, #20]
	ldrb	r3, [r7, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r10, r3, asr #2
	bcc	.L2246
.L2236:
	ldr	r3, [sp, #28]
	add	r5, r5, #1
	cmp	r3, r5
	bne	.L2239
.L2238:
	ldr	r3, .L2269
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2240
	mov	r2, #1
.L2268:
	mov	r1, #0
	mov	r0, r6
	bl	toshiba_tlc_set_rr_para
	b	.L2232
.L2233:
	uxtb	r1, r5
	mov	r0, r6
	bl	toshiba_3d_set_tlc_rr_para
	mov	r3, #38
	b	.L2267
.L2246:
	mov	r4, r10
	b	.L2238
.L2240:
	mov	r1, #0
	mov	r0, r6
	bl	toshiba_3d_set_tlc_rr_para
	b	.L2232
.L2270:
	.align	2
.L2269:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC153
	.word	.LANCHOR3
	.word	.LC155
	.word	.LC154
	.fnend
	.size	toshiba_3d_read_retrial, .-toshiba_3d_read_retrial
	.align	2
	.global	toshiba_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	toshiba_read_retrial, %function
toshiba_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	.pad #36
	sub	sp, sp, #36
	ldr	r5, .L2303
	add	r9, r6, #8
	str	r3, [sp, #20]
	str	r1, [sp, #12]
	str	r2, [sp, #16]
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	ldrb	r3, [r5, #1100]	@ zero_extendqisi2
	ldr	r7, [r5, #1044]
	sub	r3, r3, #67
	cmp	r3, #1
	lsl	r3, r6, #8
	add	r9, r7, r9, lsl #8
	str	r3, [sp, #24]
	movls	r3, #0
	strls	r3, [sp, #8]
	bls	.L2272
	ldrb	r3, [r5, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2290
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #1
.L2290:
	str	r3, [sp, #8]
	lsl	r3, r6, #8
	mov	r2, #92
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L2272:
	ldr	fp, .L2303+4
	lsl	r3, r6, #8
	mov	r8, #1
	mvn	r10, #0
	str	r3, [sp, #28]
.L2274:
	ldrb	r3, [fp, #-108]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcc	.L2283
	mov	r4, r10
.L2282:
	ldrb	r3, [r5, #1100]	@ zero_extendqisi2
	mov	r1, #0
	mov	r0, r9
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2284
	bl	sandisk_set_rr_para
.L2285:
	add	r6, r7, r6, lsl #8
	mov	r3, #255
	str	r3, [r6, #2056]
	ldrb	r3, [r5, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L2286
	cmn	r4, #1
	movne	r4, #256
.L2286:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2287
	str	r4, [sp]
	mov	r3, r8
	ldr	r2, [sp, #12]
	mov	r1, r8
	ldr	r0, .L2303+8
	bl	printk
.L2287:
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L2271
	mov	r0, #4
	bl	nandc_set_if_mode
.L2271:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2283:
	ldrb	r3, [r5, #1100]	@ zero_extendqisi2
	mov	r0, r9
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2275
	bl	sandisk_set_rr_para
.L2276:
	ldrb	r3, [r5, #1100]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L2277
	ldr	r3, .L2303+4
	ldrb	r3, [r3, #-108]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #28]
	moveq	r2, #179
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L2277:
	ldr	r3, [sp, #24]
	mov	r2, #38
	add	r1, sp, #12
	mov	r0, r6
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldm	r1, {r1, r2, r3}
	bl	flash_read_page
	cmn	r0, #1
	mov	r4, r0
	beq	.L2280
	ldr	r3, [fp, #-120]
	cmn	r10, #1
	moveq	r10, r0
	str	r3, [sp, #16]
	ldr	r3, [fp, #-112]
	str	r3, [sp, #20]
	ldrb	r3, [r5, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L2282
.L2280:
	add	r8, r8, #1
	b	.L2274
.L2275:
	bl	toshiba_set_rr_para
	b	.L2276
.L2284:
	bl	toshiba_set_rr_para
	b	.L2285
.L2304:
	.align	2
.L2303:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC155
	.fnend
	.size	toshiba_read_retrial, .-toshiba_read_retrial
	.align	2
	.global	ymtc_3d_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	ymtc_3d_read_retrial, %function
ymtc_3d_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	.pad #20
	sub	sp, sp, #20
	mov	r8, r1
	mov	r9, r2
	mov	r10, r3
	bl	nandc_wait_flash_ready
	mov	r0, r7
	mvn	r4, #0
	bl	zftl_flash_enter_slc_mode
	mov	r0, r7
	mov	r5, #1
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L2334
	tst	r8, #50331648
	add	r2, r7, #8
	ldr	fp, .L2334+4
	ldr	r6, [r3, #1044]
	str	r3, [sp, #12]
	add	r6, r6, r2, lsl #8
	bne	.L2315
.L2310:
	uxtb	r1, r5
	mov	r0, r6
	bl	ymtc_3d_set_slc_rr_para
	ldr	r3, [sp, #56]
	mov	r2, r9
	mov	r1, r8
	mov	r0, r7
	str	r3, [sp]
	mov	r3, r10
	bl	flash_read_page
	cmn	r0, #1
	beq	.L2307
	ldr	r3, [sp, #12]
	cmn	r4, #1
	moveq	r4, r0
	ldr	r9, [fp, #-120]
	ldr	r10, [fp, #-112]
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L2318
.L2307:
	add	r5, r5, #1
	cmp	r5, #10
	bne	.L2310
.L2309:
	mov	r1, #0
	mov	r0, r6
	bl	ymtc_3d_set_slc_rr_para
.L2311:
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L2316
	cmn	r4, #1
	movne	r4, #256
.L2316:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2317
	str	r4, [sp]
	mov	r3, r5
	mov	r2, r8
	mov	r1, r5
	ldr	r0, .L2334+8
	bl	printk
.L2317:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2318:
	mov	r4, r0
	b	.L2309
.L2315:
	uxtb	r1, r5
	mov	r0, r6
	bl	ymtc_3d_set_tlc_rr_para
	ldr	r3, [sp, #56]
	mov	r2, r9
	mov	r1, r8
	mov	r0, r7
	str	r3, [sp]
	mov	r3, r10
	bl	flash_read_page
	cmn	r0, #1
	beq	.L2312
	ldr	r3, [sp, #12]
	cmn	r4, #1
	moveq	r4, r0
	ldr	r9, [fp, #-120]
	ldr	r10, [fp, #-112]
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L2319
.L2312:
	add	r5, r5, #1
	cmp	r5, #51
	bne	.L2315
.L2314:
	mov	r1, #0
	mov	r0, r6
	bl	ymtc_3d_set_tlc_rr_para
	b	.L2311
.L2319:
	mov	r4, r0
	b	.L2314
.L2335:
	.align	2
.L2334:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC156
	.fnend
	.size	ymtc_3d_read_retrial, .-ymtc_3d_read_retrial
	.align	2
	.global	samsung_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	.pad #28
	sub	sp, sp, #28
	mov	r7, r1
	mov	fp, r3
	str	r2, [sp, #16]
	bl	nandc_wait_flash_ready
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
	mov	r0, r9
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L2374
	tst	r7, #50331648
	ldr	r2, [r3, #1044]
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	bne	.L2337
	lsl	r10, r9, #8
	mvn	r4, #0
	mov	r5, #1
	add	r6, r2, r10
.L2342:
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #141
	str	r3, [r6, #2052]
	ldr	r3, .L2374+4
	ldrsb	r3, [r5, r3]
	str	r3, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #64]
	mov	r1, r7
	ldr	r2, [sp, #16]
	mov	r0, r9
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2374+8
	mov	r8, r0
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2338
	mov	r3, r0
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L2374+12
	bl	printk
.L2338:
	cmn	r8, #1
	beq	.L2339
	ldr	r3, .L2374+16
	cmn	r4, #1
	moveq	r4, r8
	ldr	r2, [r3, #-120]
	ldr	fp, [r3, #-112]
	ldr	r3, [sp, #20]
	str	r2, [sp, #16]
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bcc	.L2351
.L2339:
	add	r5, r5, #1
	cmp	r5, #26
	bne	.L2342
.L2341:
	ldr	r3, [sp, #12]
	add	r10, r3, r10
	mov	r3, #239
	str	r3, [r10, #2056]
	mov	r3, #141
.L2373:
	str	r3, [r6, #2052]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	bl	nandc_wait_flash_ready
	ldr	r3, .L2374
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L2349
	cmn	r4, #1
	movne	r4, #256
.L2349:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2350
	str	r4, [sp]
	mov	r3, r5
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L2374+20
	bl	printk
.L2350:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2351:
	mov	r4, r8
	b	.L2341
.L2337:
	lsl	r3, r9, #8
	lsl	r2, r9, #8
	ldr	r8, .L2374+24
	str	r3, [sp, #20]
	mvn	r4, #0
	ldr	r3, [sp, #12]
	mov	r5, #1
	add	r6, r3, r2
.L2348:
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #137
	str	r3, [r6, #2052]
	ldrb	r3, [r8, #4]	@ zero_extendqisi2
	str	r3, [r6, #2048]
	ldrb	r3, [r8, #5]	@ zero_extendqisi2
	str	r3, [r6, #2048]
	ldrb	r3, [r8, #6]	@ zero_extendqisi2
	str	r3, [r6, #2048]
	ldrb	r3, [r8, #7]	@ zero_extendqisi2
	str	r3, [r6, #2048]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #64]
	mov	r1, r7
	ldr	r2, [sp, #16]
	mov	r0, r9
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2374+8
	mov	r10, r0
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2344
	mov	r3, r0
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L2374+28
	bl	printk
.L2344:
	cmn	r10, #1
	beq	.L2345
	ldr	r3, .L2374+16
	cmn	r4, #1
	moveq	r4, r10
	ldr	r2, [r3, #-120]
	ldr	fp, [r3, #-112]
	ldr	r3, .L2374
	str	r2, [sp, #16]
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r10, r3, asr #2
	bcc	.L2352
.L2345:
	add	r5, r5, #1
	add	r8, r8, #4
	cmp	r5, #26
	bne	.L2348
.L2347:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #20]
	add	r10, r3, r2
	mov	r3, #239
	str	r3, [r10, #2056]
	mov	r3, #137
	b	.L2373
.L2352:
	mov	r4, r10
	b	.L2347
.L2375:
	.align	2
.L2374:
	.word	.LANCHOR0
	.word	.LANCHOR1+1752
	.word	.LANCHOR2
	.word	.LC157
	.word	.LANCHOR3
	.word	.LC159
	.word	.LANCHOR1+1778
	.word	.LC158
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	hynix_read_retrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	hynix_read_retrial, %function
hynix_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	fp, .L2395
	mov	r9, r2
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r8, r1
	mov	r7, #0
	ldr	r3, [fp, #1040]
	mvn	r4, #0
	add	r2, r3, r0
	ldrb	r3, [r3, #114]	@ zero_extendqisi2
	str	r2, [sp, #12]
	ldrb	r5, [r2, #120]	@ zero_extendqisi2
	str	r3, [sp, #8]
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2377:
	ldr	r3, [sp, #8]
	cmp	r7, r3
	bcc	.L2382
.L2381:
	ldr	r3, [sp, #12]
	strb	r5, [r3, #120]
	ldrb	r3, [fp, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L2383
	cmn	r4, #1
	movne	r4, #256
.L2383:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2384
	str	r4, [sp]
	mov	r3, r7
	mov	r2, r8
	mov	r1, r7
	ldr	r0, .L2395+4
	bl	printk
.L2384:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2382:
	ldr	r3, [sp, #8]
	add	r5, r5, #1
	uxtb	r5, r5
	mov	r0, r6
	cmp	r3, r5
	movls	r5, #0
	mov	r1, r5
	bl	hynix_set_rr_para
	ldr	r3, [sp, #56]
	mov	r2, r9
	mov	r1, r8
	mov	r0, r6
	str	r3, [sp]
	mov	r3, r10
	bl	flash_read_page
	cmn	r0, #1
	beq	.L2379
	ldr	r3, .L2395+8
	cmn	r4, #1
	moveq	r4, r0
	ldr	r9, [r3, #-120]
	ldr	r10, [r3, #-112]
	ldrb	r3, [fp, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L2385
.L2379:
	add	r7, r7, #1
	b	.L2377
.L2385:
	mov	r4, r0
	b	.L2381
.L2396:
	.align	2
.L2395:
	.word	.LANCHOR0
	.word	.LC160
	.word	.LANCHOR3
	.fnend
	.size	hynix_read_retrial, .-hynix_read_retrial
	.align	2
	.global	flash_ddr_tuning_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_ddr_tuning_read, %function
flash_ddr_tuning_read:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, #0
	mov	r5, r4
	mov	r6, #1024
	mov	r7, #6
	mvn	r10, #0
	.pad #36
	sub	sp, sp, #36
	mov	r8, r0
	mov	fp, r1
	str	r2, [sp, #16]
	str	r3, [sp, #20]
	bl	nandc_get_ddr_para
	str	r0, [sp, #28]
	str	r4, [sp, #24]
	str	r4, [sp, #12]
.L2403:
	uxtb	r0, r7
	bl	nandc_set_ddr_para
	ldr	r3, [sp, #72]
	mov	r1, fp
	ldr	r2, [sp, #16]
	mov	r0, r8
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	flash_read_page
	ldr	r3, .L2424
	mov	r9, r0
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2398
	mov	r3, r0
	mov	r2, fp
	mov	r1, r7
	ldr	r0, .L2424+4
	bl	printk
.L2398:
	add	r3, r6, #1
	cmp	r9, r3
	bhi	.L2399
	ldr	r3, .L2424+8
	ldr	r3, [r3, #-120]
	str	r3, [sp, #16]
	ldr	r3, .L2424+8
	ldr	r3, [r3, #-112]
	str	r3, [sp, #20]
	ldr	r3, .L2424+12
	ldrb	r3, [r3, #1193]	@ zero_extendqisi2
	cmp	r9, r3, lsr #2
	bcs	.L2409
	add	r5, r5, #1
	cmp	r5, #7
	bls	.L2409
	sub	r4, r7, r5
	mov	r6, r9
	mov	r10, #0
.L2401:
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #24]
	cmp	r5, r2
	movcc	r4, r3
.L2402:
	cmp	r4, #0
	beq	.L2404
	ldr	r3, .L2424+12
	ldrb	r2, [r3, #1193]	@ zero_extendqisi2
	ldr	r3, .L2424+16
	umull	r2, r3, r2, r3
	ubfx	r3, r3, #1, #8
	cmp	r3, r6
	bls	.L2404
	mov	r1, r4
	ldr	r0, .L2424+20
	bl	printk
	uxtb	r0, r4
.L2423:
	bl	nandc_set_ddr_para
	cmn	r10, #1
	bne	.L2397
	ldr	r4, .L2424+12
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	mov	r5, r4
	tst	r3, #1
	beq	.L2397
	mov	r1, r8
	mov	r2, fp
	ldr	r0, .L2424+24
	bl	printk
	mov	r0, r8
	bl	flash_reset
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	add	r3, r4, r8
	mov	r2, #2
	mov	r0, r8
	strb	r2, [r3, #1154]
	bl	zftl_flash_enter_slc_mode
	ldr	r3, [sp, #72]
	mov	r1, fp
	ldr	r2, [sp, #16]
	mov	r0, r8
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	flash_read_page
	mov	r2, fp
	mov	r3, r0
	mov	r6, r0
	mov	r1, r8
	ldr	r0, .L2424+28
	bl	printk
	ldrb	r3, [r4, #1193]	@ zero_extendqisi2
	cmp	r6, r3
	bhi	.L2411
	ldr	r2, .L2424+8
	ldr	r3, [r2, #-104]
	add	r3, r3, #1
	cmp	r3, #100
	str	r3, [r2, #-104]
	movhi	r3, #0
	strbhi	r3, [r4, #1143]
	bhi	.L2397
.L2407:
	ldrb	r0, [r5, #1192]	@ zero_extendqisi2
	bl	flash_set_interface_mode
	ldrb	r0, [r5, #1192]	@ zero_extendqisi2
	bl	nandc_set_if_mode
.L2397:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2399:
	ldr	r3, [sp, #12]
	cmp	r5, r3
	bls	.L2410
	cmp	r5, #7
	sub	r3, r4, r5
	str	r3, [sp, #24]
	bhi	.L2402
	str	r5, [sp, #12]
.L2410:
	mov	r5, #0
	b	.L2400
.L2409:
	mov	r4, r7
	mov	r6, r9
	mov	r10, #0
.L2400:
	add	r7, r7, #2
	cmp	r7, #50
	bne	.L2403
	b	.L2401
.L2404:
	ldrb	r0, [sp, #28]	@ zero_extendqisi2
	b	.L2423
.L2411:
	mov	r6, r10
	b	.L2407
.L2425:
	.align	2
.L2424:
	.word	.LANCHOR2
	.word	.LC161
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC162
	.word	.LC163
	.word	.LC164
	.fnend
	.size	flash_ddr_tuning_read, .-flash_ddr_tuning_read
	.align	2
	.global	flash_read_page_en
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_page_en, %function
flash_read_page_en:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r3
	ldr	r5, .L2454
	mov	r10, r0
	mov	r4, r1
	mov	r7, r2
	ldr	r9, [sp, #48]
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2427
	movw	r2, #431
	ldr	r1, .L2454+4
	ldr	r0, .L2454+8
	bl	printk
	bl	dump_stack
.L2427:
	add	r3, r5, r10
	ldrb	r6, [r3, #1144]	@ zero_extendqisi2
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r10, r3
	bcc	.L2428
	ldr	r2, .L2454+12
	ldr	r2, [r2]
	tst	r2, #64
	bne	.L2429
.L2453:
	mvn	r0, #0
.L2426:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2429:
	str	r3, [sp]
	mov	r2, r10
	mov	r3, r4
	mov	r1, r6
	ldr	r0, .L2454+16
	bl	printk
	b	.L2453
.L2428:
	tst	r4, #50331648
	bne	.L2431
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2432
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2431
.L2432:
	ldrh	r10, [r5, #2]
	mov	r0, r4
	mov	r1, r10
	bl	__aeabi_uidiv
	mov	r1, r10
	mul	fp, r10, r0
	mov	r0, r4
	bl	__aeabi_uidivmod
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	lsl	r1, r1, #1
	cmp	r3, #0
	addeq	r1, r5, r1
	addne	r4, r1, fp
	ldrheq	r4, [r1, #4]
	addeq	r4, r4, fp
.L2431:
	str	r9, [sp]
	mov	r3, r8
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	bl	flash_read_page
	cmn	r0, #1
	bne	.L2426
	ldrb	r10, [r5, #1196]	@ zero_extendqisi2
	cmp	r10, #0
	bne	.L2434
.L2437:
	ldr	r3, .L2454+20
	ldr	r10, [r3, #-100]
	cmp	r10, #0
	bne	.L2435
.L2436:
	ldrb	r3, [r5, #1196]	@ zero_extendqisi2
	mov	r2, r4
	mov	r1, #0
	ldr	r0, .L2454+24
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2453
	str	r9, [sp, #48]
	mov	r3, r8
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	flash_ddr_tuning_read
.L2434:
	mov	r3, #0
	str	r9, [sp]
	strb	r3, [r5, #1196]
	mov	r2, r7
	mov	r3, r8
	mov	r1, r4
	mov	r0, r6
	bl	flash_read_page
	cmn	r0, #1
	strb	r10, [r5, #1196]
	beq	.L2437
	b	.L2426
.L2435:
	str	r9, [sp]
	mov	r3, r8
	mov	r2, r7
	mov	r1, r4
	mov	r0, r6
	blx	r10
	cmn	r0, #1
	bne	.L2426
	b	.L2436
.L2455:
	.align	2
.L2454:
	.word	.LANCHOR0
	.word	.LANCHOR1+1882
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC165
	.word	.LANCHOR3
	.word	.LC166
	.fnend
	.size	flash_read_page_en, .-flash_read_page_en
	.align	2
	.global	flash_get_last_written_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_get_last_written_page, %function
flash_get_last_written_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r1
	ldr	r5, .L2467
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	ldr	r1, .L2467+4
	mov	r9, r3
	ldrh	r6, [r5, #30]
	ldrh	r4, [r1, #-224]
	ldr	r1, [sp, #56]
	mul	r6, r8, r6
	sub	r4, r4, #1
	sxth	r4, r4
	str	r1, [sp]
	add	r1, r4, r6
	bl	flash_read_page_en
	cmp	r0, #512
	str	r5, [sp, #12]
	moveq	r7, #0
	beq	.L2458
.L2457:
	ldr	r3, [sp, #12]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2462
	ldr	r3, [r9]
	mov	r2, r4
	mov	r1, r8
	ldr	r0, .L2467+8
	bl	printk
.L2462:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2461:
	add	r5, r7, r4
	ldr	r3, [sp, #56]
	add	r5, r5, r5, lsr #31
	mov	r2, fp
	mov	r0, r10
	asr	r5, r5, #1
	str	r3, [sp]
	mov	r3, r9
	sxtah	r1, r6, r5
	bl	flash_read_page_en
	cmp	r0, #512
	subeq	r4, r5, #1
	addne	r5, r5, #1
	sxtheq	r4, r4
	sxthne	r7, r5
.L2458:
	cmp	r7, r4
	ble	.L2461
	b	.L2457
.L2468:
	.align	2
.L2467:
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC167
	.fnend
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L2471
	push	{r0, r1, r2, lr}
	.save {lr}
	.pad #12
	ldr	lr, .L2471+4
	ldrb	ip, [ip, #1153]	@ zero_extendqisi2
	ldrh	lr, [lr, #-2]
	str	r3, [sp]
	mov	r3, r2
	rsb	ip, ip, #24
	mov	r2, r1
	sub	ip, ip, lr
	mvn	r1, #0
	asr	lr, r0, ip
	bic	r1, r0, r1, lsl ip
	uxtb	r0, lr
	bl	flash_get_last_written_page
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L2472:
	.align	2
.L2471:
	.word	.LANCHOR0
	.word	.LANCHOR3-3136
	.fnend
	.size	flash_get_last_written_page_ext, .-flash_get_last_written_page_ext
	.align	2
	.global	flash_ddr_para_scan
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_ddr_para_scan, %function
flash_ddr_para_scan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r7, r0
	ldr	r4, .L2480
	mov	r6, #1
	mov	r8, r1
	mov	r9, #4
	ldr	r5, .L2480+4
	ldrb	r0, [r4, #1192]	@ zero_extendqisi2
	strb	r6, [r4, #1143]
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1192]	@ zero_extendqisi2
	bl	nandc_set_if_mode
	ldr	r3, [r5, #-96]
	mov	r1, r8
	ldr	r2, [r5, #-92]
	mov	r0, r7
	str	r9, [sp]
	bl	flash_ddr_tuning_read
	ldr	r3, [r5, #-96]
	mov	r1, r8
	str	r9, [sp]
	mov	r0, r7
	ldr	r2, [r5, #-92]
	bl	flash_read_page
	cmn	r0, #1
	mov	r3, r4
	bne	.L2474
	ldrb	r2, [r4, #1192]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L2474
	mov	r0, r6
	bl	flash_set_interface_mode
	mov	r0, r6
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #1143]
.L2475:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2474:
	mov	r2, #1
	strb	r2, [r3, #1143]
	b	.L2475
.L2481:
	.align	2
.L2480:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.global	flash_prog_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_prog_page, %function
flash_prog_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r6, .L2494
	.pad #20
	sub	sp, sp, #20
	mov	r4, r0
	mov	r7, r1
	str	r2, [sp, #12]
	mvn	r2, #0
	ldrb	r3, [r6, #1153]	@ zero_extendqisi2
	add	r8, r0, #8
	ldr	r5, [r6, #1044]
	rsb	r3, r3, #24
	bic	r10, r1, r2, lsl r3
	add	r8, r5, r8, lsl #8
	bl	nandc_wait_flash_ready
	mov	r0, r4
	bl	hynix_reconfig_rr_para
	mov	r0, r4
	bl	nandc_cs
	tst	r7, #50331648
	mov	r0, r4
	bne	.L2483
	bl	zftl_flash_enter_slc_mode
.L2484:
	lsl	r9, r4, #8
	mov	r1, #128
	mov	r0, r10
	add	r2, r5, r9
	add	r5, r5, r9
	str	r1, [r2, #2056]
	mov	r1, #0
	str	r1, [r2, #2052]
	str	r1, [r2, #2052]
	uxtb	r1, r10
	str	r1, [r2, #2052]
	lsr	r1, r10, #8
	str	r1, [r2, #2052]
	lsr	r1, r10, #16
	str	r1, [r2, #2052]
	ldrb	r1, [r6, #1152]	@ zero_extendqisi2
	cmp	r1, #0
	lsrne	r1, r10, #24
	strne	r1, [r2, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2494+4
	mov	r1, #1
	mov	r0, r4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	str	fp, [sp]
	bl	nandc_xfer
	mov	r3, #16
	str	r3, [r5, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	bl	nandc_de_cs.constprop.35
	ands	r0, r0, #4
	beq	.L2482
	mov	r2, r0
	mov	r1, r7
	ldr	r0, .L2494+8
	bl	printk
	mvn	r0, #0
.L2482:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2483:
	bl	zftl_flash_exit_slc_mode
	b	.L2484
.L2495:
	.align	2
.L2494:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC168
	.fnend
	.size	flash_prog_page, .-flash_prog_page
	.align	2
	.global	flash_test_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_test_blk, %function
flash_test_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ldr	r4, .L2507
	mov	r5, r1
	mov	r2, #32
	mov	r1, #165
	ldr	r0, [r4, #-120]
	bl	ftl_memset
	mov	r2, #8
	mov	r1, #90
	ldr	r0, [r4, #-112]
	bl	ftl_memset
	ldr	r3, .L2507+4
	mov	r0, r6
	ldrh	r3, [r3, #2]
	mul	r5, r5, r3
	mov	r1, r5
	bl	flash_erase_block
	cmn	r0, #1
	bne	.L2497
.L2499:
	mvn	r4, #0
.L2498:
	mov	r1, r5
	mov	r0, r6
	bl	flash_erase_block
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2497:
	ldr	r7, .L2507+8
	mov	r1, r5
	mov	r0, r6
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r4, #-112]
	ldr	r2, [r4, #-120]
	bl	flash_prog_page
	cmn	r0, #1
	beq	.L2499
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r1, r5
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [r4, #-112]
	ldr	r2, [r4, #-120]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L2499
	ldr	r3, [r4, #-120]
	ldr	r2, [r3]
	ldr	r3, .L2507+12
	cmp	r2, r3
	bne	.L2499
	ldr	r3, [r4, #-112]
	ldr	r4, [r3]
	ldr	r3, .L2507+16
	subs	r4, r4, r3
	mvnne	r4, #0
	b	.L2498
.L2508:
	.align	2
.L2507:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1515870811
	.word	1515870810
	.fnend
	.size	flash_test_blk, .-flash_test_blk
	.align	2
	.global	flash_start_one_pass_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_one_pass_page_prog, %function
flash_start_one_pass_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r9, r3
	ldr	r6, .L2519
	mov	r10, r0
	lsl	r7, r9, #8
	mov	r0, r3
	ldr	r5, [sp, #40]
	mov	r8, r2
	ldr	r4, [r6, #1044]
	bl	nandc_cs
	cmp	r10, #0
	mov	r2, #128
	addne	r3, r4, r7
	mov	r0, r5
	strne	r10, [r3, #2056]
	add	r3, r4, r7
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	add	r4, r4, r7
	str	r2, [r3, #2052]
	uxtb	r2, r5
	str	r2, [r3, #2052]
	lsr	r2, r5, #8
	str	r2, [r3, #2052]
	lsr	r2, r5, #16
	str	r2, [r3, #2052]
	ldrb	r2, [r6, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	lsrne	r2, r5, #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2519+4
	mov	r1, #1
	mov	r0, r9
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #48]
	str	r3, [sp]
	ldr	r3, [sp, #44]
	bl	nandc_xfer
	str	r8, [r4, #2056]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2520:
	.align	2
.L2519:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_one_pass_page_prog, .-flash_start_one_pass_page_prog
	.align	2
	.global	flash_dual_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L2534
	mov	r9, r2
	add	r2, r0, #8
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L2534+4
	mov	r4, r0
	ldrb	r5, [r3, #1153]	@ zero_extendqisi2
	mov	r7, r1
	ldr	r8, [r3, #1044]
	ubfx	fp, r1, #24, #2
	rsb	r3, r5, #24
	mvn	r5, #0
	bic	r5, r1, r5, lsl r3
	ldr	r3, [r6]
	add	r8, r8, r2, lsl #8
	tst	r3, #16
	beq	.L2522
	ldr	r3, [sp, #64]
	mov	r2, fp
	ldr	r0, .L2534+8
	bl	printk
.L2522:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r4
	bne	.L2523
	bl	zftl_flash_enter_slc_mode
.L2524:
	mov	r1, #0
	mov	r3, r4
	mov	r2, #16
	mov	r0, r1
	stm	sp, {r5, r9, r10}
	add	r5, r5, #1
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #60]
	mov	r1, #0
	mov	r2, #16
	mov	r0, r1
	str	r5, [sp]
	str	r3, [sp, #8]
	ldr	r3, [sp, #56]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	bl	nandc_de_cs.constprop.35
	ands	r0, r0, #4
	beq	.L2521
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L2526
	mov	r2, r0
	mov	r1, r7
	ldr	r0, .L2534+12
	bl	printk
.L2526:
	mvn	r0, #0
.L2521:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2523:
	bl	zftl_flash_exit_slc_mode
	b	.L2524
.L2535:
	.align	2
.L2534:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC169
	.word	.LC168
	.fnend
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.align	2
	.global	ymtc_flash_tlc_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	ymtc_flash_tlc_page_prog, %function
ymtc_flash_tlc_page_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r10, r3
	ldr	r3, .L2543
	mov	r6, #1
	mov	r5, r0
	mov	r8, r2
	add	r2, r0, #8
	mov	r9, r1
	ldrb	r4, [r3, #1153]	@ zero_extendqisi2
	ldr	r7, [r3, #1044]
	rsb	r4, r4, #24
	lsl	r4, r6, r4
	add	r7, r7, r2, lsl #8
	sub	r4, r4, #1
	and	r4, r4, r1
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	mov	r3, r5
	mov	r2, #26
	mov	r1, r6
	stm	sp, {r4, r8, r10}
	mov	r0, #0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	add	r3, r4, r6
	mov	r2, #26
	mov	r1, r6
	str	r3, [sp]
	mov	r0, #0
	mov	r3, r5
	stmib	sp, {r8, r10}
	add	r4, r4, #2
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	r3, r5
	mov	r2, #16
	mov	r1, r6
	mov	r0, #0
	stmib	sp, {r8, r10}
	str	r4, [sp]
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r7
	bl	flash_read_status
	bl	nandc_de_cs.constprop.35
	ands	r0, r0, #4
	beq	.L2536
	ldr	r3, .L2543+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2538
	mov	r2, r0
	mov	r1, r9
	ldr	r0, .L2543+8
	bl	printk
.L2538:
	mvn	r0, #0
.L2536:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2544:
	.align	2
.L2543:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC170
	.fnend
	.size	ymtc_flash_tlc_page_prog, .-ymtc_flash_tlc_page_prog
	.section	.text.unlikely
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	fw_flash_page_prog.constprop.29, %function
fw_flash_page_prog.constprop.29:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, r1
	ldr	r4, .L2551
	.pad #20
	sub	sp, sp, #20
	mov	r7, r2
	ldr	r3, [r4, #1104]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r5, r0
	ldrb	r0, [r4, #1108]	@ zero_extendqisi2
	ldrb	r8, [r4, #1193]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r3, .L2551+4
	ldrb	r2, [r3, #11]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L2546
	ldrb	r4, [r4, #1110]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L2546
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2547
	mov	r3, r7
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	ymtc_flash_tlc_page_prog
.L2550:
	mov	r4, r0
	mov	r0, r8
	bl	nandc_bch_sel
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2547:
	ldr	r9, .L2551+8
	mov	r2, #16384
	mov	r1, #255
	ldr	r0, [r9, #-120]
	bl	ftl_memset
	ldr	r3, [r9, #-120]
	mov	r2, #4
	str	r2, [sp, #8]
	mov	r1, r5
	mov	r2, r6
	mov	r0, r4
	str	r3, [sp, #4]
	str	r3, [sp]
	mov	r3, r7
	bl	flash_dual_page_prog
	b	.L2550
.L2546:
	mov	r3, #4
	mov	r2, r6
	str	r3, [sp]
	mov	r1, r5
	mov	r3, r7
	mov	r0, #0
	bl	flash_prog_page
	b	.L2550
.L2552:
	.align	2
.L2551:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.fnend
	.size	fw_flash_page_prog.constprop.29, .-fw_flash_page_prog.constprop.29
	.text
	.align	2
	.global	flash_start_tlc_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r3
	ldr	r4, .L2561
	add	r5, sp, #48
	mov	fp, r0
	mov	r7, r1
	mov	r8, r2
	ldm	r5, {r5, r9, r10}
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r6
	bhi	.L2554
	mov	r2, #868
	ldr	r1, .L2561+4
	ldr	r0, .L2561+8
	bl	printk
	bl	dump_stack
.L2554:
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L2553
	add	r6, r4, r6
	ldr	r4, [r4, #1044]
	ldrb	r6, [r6, #1144]	@ zero_extendqisi2
	mov	r0, r6
	bl	nandc_cs
	lsl	r3, r6, #8
	cmp	fp, #0
	addne	r2, r4, r3
	add	r4, r4, r3
	mov	r3, #128
	strne	fp, [r2, #2056]
	str	r7, [r4, #2056]
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r5
	str	r3, [r4, #2052]
	lsr	r3, r5, #8
	str	r3, [r4, #2052]
	lsr	r3, r5, #16
	add	r5, r5, r5, lsl #1
	str	r3, [r4, #2052]
	sub	r0, r5, #1
	add	r0, r0, r7
	bl	nandc_set_seed
	ldr	r3, .L2561+12
	mov	r1, #1
	mov	r0, r6
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r9
	str	r10, [sp]
	bl	nandc_xfer
	str	r8, [r4, #2056]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs.constprop.35
.L2553:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2562:
	.align	2
.L2561:
	.word	.LANCHOR0
	.word	.LANCHOR1+1901
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_tlc_page_prog, .-flash_start_tlc_page_prog
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_tlc_prog_cmd, %function
queue_tlc_prog_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r8, r1
	ldr	r1, .L2568
	mov	r7, #1
	mov	r6, r0
	ldr	r2, [r0]
	ldrb	r3, [r1, #1153]	@ zero_extendqisi2
	ldr	ip, [r2, #24]
	rsb	r0, r3, #24
	lsl	r4, r7, r3
	ldrb	r3, [r1, #1159]	@ zero_extendqisi2
	lsl	r5, r7, r0
	sub	r4, r4, #1
	and	r4, r4, ip, lsr r0
	cmp	r3, #0
	sub	r5, r5, #1
	and	r5, r5, ip
	uxtb	r4, r4
	beq	.L2564
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r6]
	mov	r1, r7
	mov	r0, #0
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r5, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #4]
	mov	r1, r7
	mov	r0, #0
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r3, [sp, #4]
	add	r3, r5, r7
	str	r3, [sp]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #8]
	add	r5, r5, #2
	mov	r1, r7
	mov	r0, #0
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #16
	ldr	r3, [r3, #4]
	str	r5, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
.L2565:
	cmp	r8, #0
	beq	.L2563
	ldr	r1, [r6]
	mov	r3, #4
	ldr	r0, .L2568+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2564:
	ldr	r3, [r2, #12]
	mov	r1, r7
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, [r2, #4]
	mov	r2, #26
	str	r5, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldm	r6, {r2, r3}
	mov	r1, #2
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r5, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #8]
	mov	r1, #3
	ldr	r2, [r6]
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #16
	ldr	r3, [r3, #4]
	str	r5, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	b	.L2565
.L2563:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2569:
	.align	2
.L2568:
	.word	.LANCHOR0
	.word	.LANCHOR0+2770
	.fnend
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r7, #1
	ldr	r2, .L2574
	mov	r5, r0
	ldr	r0, [r0, #24]
	ldrb	r3, [r2, #1153]	@ zero_extendqisi2
	ldrb	r8, [r2, #1159]	@ zero_extendqisi2
	rsb	r1, r3, #24
	lsl	r4, r7, r3
	lsl	r6, r7, r1
	sub	r4, r4, #1
	cmp	r8, #0
	and	r4, r4, r0, lsr r1
	sub	r6, r6, #1
	and	r6, r6, r0
	uxtb	r4, r4
	beq	.L2571
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r5, #12]
	mov	r2, #26
	mov	r1, r7
	mov	r0, #0
	str	r6, [sp]
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #12]
	mov	r2, #26
	mov	r1, r7
	mov	r0, #0
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	add	r3, r6, r7
	str	r3, [sp]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #12]
	add	r6, r6, #2
	str	r6, [sp]
	mov	r2, #16
	mov	r1, r7
	mov	r0, #0
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
.L2572:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r1, #64
	ldr	r0, [r5, #24]
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready
.L2571:
	ldr	r3, [r5, #12]
	mov	r1, r7
	mov	r2, #26
	mov	r0, r8
	str	r6, [sp]
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #12]
	mov	r2, #26
	mov	r1, #2
	mov	r0, r8
	str	r6, [sp]
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #12]
	mov	r2, #16
	str	r6, [sp]
	mov	r1, #3
	mov	r0, r8
	str	r3, [sp, #8]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	b	.L2572
.L2575:
	.align	2
.L2574:
	.word	.LANCHOR0
	.fnend
	.size	sblk_3d_tlc_dump_prog, .-sblk_3d_tlc_dump_prog
	.align	2
	.global	flash_start_3d_mlc_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_3d_mlc_page_prog, %function
flash_start_3d_mlc_page_prog:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r4, .L2580
	mov	r7, r0
	mov	r5, r1
	mov	r6, r2
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r1
	bhi	.L2577
	movw	r2, #903
	ldr	r1, .L2580+4
	ldr	r0, .L2580+8
	bl	printk
	bl	dump_stack
.L2577:
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r5
	bls	.L2576
	add	r5, r4, r5
	ldr	r4, [r4, #1044]
	ldrb	r5, [r5, #1144]	@ zero_extendqisi2
	mov	r0, r5
	add	r4, r4, r5, lsl #8
	bl	nandc_cs
	mov	r3, #128
	mov	r0, r6
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r6
	str	r3, [r4, #2052]
	lsr	r3, r6, #8
	str	r3, [r4, #2052]
	lsr	r3, r6, #16
	str	r3, [r4, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2580+12
	mov	r1, #1
	mov	r0, r5
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	str	r3, [sp]
	mov	r3, r8
	bl	nandc_xfer
	str	r7, [r4, #2056]
.L2576:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2581:
	.align	2
.L2580:
	.word	.LANCHOR0
	.word	.LANCHOR1+1927
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_3d_mlc_page_prog, .-flash_start_3d_mlc_page_prog
	.align	2
	.global	sblk_mlc_dump_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_mlc_dump_prog, %function
sblk_mlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2588
	push	{r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	mov	r5, r0
	ldr	r1, [r0, #24]
	ldrb	r4, [r3, #1153]	@ zero_extendqisi2
	mov	r3, #1
	rsb	r2, r4, #24
	lsl	r4, r3, r4
	lsl	r6, r3, r2
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, r1, lsr r2
	and	r6, r6, r1
	uxtb	r4, r4
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L2588+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2583
	ldr	r2, [r5, #24]
	mov	r1, r6
	ldr	r0, .L2588+8
	add	r3, r2, #1
	bl	printk
.L2583:
	ldr	r3, [r5, #12]
	mov	r2, r6
	mov	r1, r4
	mov	r0, #16
	str	r3, [sp]
	ldr	r3, [r5, #4]
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #12]
	add	r2, r6, #1
	mov	r1, r4
	mov	r0, #16
	str	r3, [sp]
	ldr	r3, [r5, #4]
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r1, #64
	ldr	r0, [r5, #24]
	bl	flash_wait_device_ready
	bl	nandc_de_cs.constprop.35
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L2589:
	.align	2
.L2588:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC171
	.fnend
	.size	sblk_mlc_dump_prog, .-sblk_mlc_dump_prog
	.align	2
	.global	flash_start_page_prog
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r5, .L2605
	mov	fp, r3
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	str	r0, [sp, #12]
	ldrb	r2, [r5, #1153]	@ zero_extendqisi2
	rsb	r4, r2, #24
	lsr	r3, r1, r4
	mvn	r1, #0
	bic	r3, r3, r1, lsl r2
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	uxtb	r9, r3
	cmp	r2, r9
	bhi	.L2591
	mov	r2, #956
	ldr	r1, .L2605+4
	ldr	r0, .L2605+8
	bl	printk
	bl	dump_stack
.L2591:
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	cmp	r2, r9
	bls	.L2590
	mvn	r2, #0
	ldr	r7, [r5, #1044]
	bic	r4, r8, r2, lsl r4
	add	r2, r5, r9
	ldrb	r6, [r2, #1144]	@ zero_extendqisi2
	bl	nandc_rdy_status
	cmp	r0, #0
	bne	.L2593
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2594
	bl	nandc_wait_flash_ready
.L2593:
	mov	r0, r6
	bl	hynix_reconfig_rr_para
	mov	r0, r6
	bl	nandc_cs
	tst	r8, #50331648
	bne	.L2595
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [r5]	@ zero_extendqisi2
	mov	r4, r0
	cmp	r3, #0
	beq	.L2596
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
.L2596:
	lsl	r8, r6, #8
	mov	r2, #128
	mov	r0, r4
	add	r3, r7, r8
	add	r7, r7, r8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r4
	str	r2, [r3, #2052]
	lsr	r2, r4, #8
	str	r2, [r3, #2052]
	lsr	r2, r4, #16
	str	r2, [r3, #2052]
	ldrb	r2, [r5, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	lsrne	r2, r4, #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2605+12
	mov	r1, #1
	mov	r0, r6
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	ldr	r3, [sp, #12]
	str	r3, [r7, #2056]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs.constprop.35
.L2594:
	mov	r2, #64
	mov	r1, r4
	mov	r0, r9
	bl	flash_wait_device_ready_raw
	b	.L2593
.L2595:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	b	.L2596
.L2590:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2606:
	.align	2
.L2605:
	.word	.LANCHOR0
	.word	.LANCHOR1+1956
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_page_prog, .-flash_start_page_prog
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_prog_cmd, %function
queue_prog_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r3, [r0, #12]
	ldr	r2, [r0, #4]
	ldr	r1, [r0, #24]
	mov	r0, #16
	bl	flash_start_page_prog
	ldr	r2, .L2616
	ldr	r0, [r4, #24]
	ldrb	r3, [r2, #2770]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2608
	ldrb	ip, [r2, #1153]	@ zero_extendqisi2
	mvn	r1, #0
	mov	r5, #48
	movw	r8, #1256
	movw	r6, #1274
	rsb	r7, ip, #24
	mvn	r1, r1, lsl ip
	uxth	r1, r1
	and	r0, r1, r0, asr r7
.L2610:
	mla	ip, r5, r3, r2
	ldr	lr, [ip, r8]
	and	lr, r1, lr, lsr r7
	cmp	r0, lr
	bne	.L2609
	add	lr, ip, r6
	ldrb	ip, [ip, r6]	@ zero_extendqisi2
	cmp	ip, #7
	bne	.L2609
	mov	r3, #3
	strb	r3, [lr]
.L2608:
	mov	r3, #3
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #1
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldr	r0, .L2616+4
	pop	{r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2609:
	mla	r3, r5, r3, r2
	ldrb	r3, [r3, #1232]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2610
	b	.L2608
.L2617:
	.align	2
.L2616:
	.word	.LANCHOR0
	.word	.LANCHOR0+2770
	.fnend
	.size	queue_prog_cmd, .-queue_prog_cmd
	.align	2
	.global	flash_complete_plane_page_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_complete_plane_page_read, %function
flash_complete_plane_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r10, r2
	ldr	r5, .L2649
	mvn	r2, #0
	mov	r6, r0
	mov	r9, r1
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	rsb	r4, r3, #24
	lsr	r7, r0, r4
	bic	r7, r7, r2, lsl r3
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	uxtb	r7, r7
	cmp	r3, r7
	bhi	.L2619
	movw	r2, #1070
	ldr	r1, .L2649+4
	ldr	r0, .L2649+8
	bl	printk
	bl	dump_stack
.L2619:
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	mvn	r0, #0
	cmp	r3, r7
	bls	.L2618
	add	r7, r5, r7
	bic	r4, r6, r0, lsl r4
	ldrb	r8, [r7, #1144]	@ zero_extendqisi2
	ubfx	r6, r6, #24, #2
	ldr	r7, [r5, #1044]
	mov	r0, r8
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2621
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2621:
	ldrb	r3, [r5, #1127]	@ zero_extendqisi2
	uxtb	lr, r4
	lsr	ip, r4, #8
	ldrb	r0, [r5, #1152]	@ zero_extendqisi2
	cmp	r3, #1
	lsl	r3, r8, #8
	moveq	r1, #6
	addeq	r2, r7, r3
	beq	.L2648
	ldr	r2, [r5, #1104]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	add	r2, r7, r3
	bne	.L2625
	mov	r1, #5
.L2648:
	str	r1, [r2, #2056]
	mov	r1, #0
	str	r1, [r2, #2052]
	cmp	r0, #0
	str	r1, [r2, #2052]
	lsr	r1, r4, #16
	str	lr, [r2, #2052]
	add	r3, r7, r3
	str	ip, [r2, #2052]
	str	r1, [r2, #2052]
	lsrne	r1, r4, #24
	strne	r1, [r2, #2052]
.L2644:
	mov	r2, #224
	cmp	r6, #0
	str	r2, [r3, #2056]
	ldr	r3, [r5, #1104]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	sub	r3, r3, #3
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2628
	ldrb	r3, [r5, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2628
	ldrb	r3, [r5, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r4, r4, r4, lsl #1
	subeq	r6, r6, #1
	addeq	r0, r4, r6
	beq	.L2645
.L2628:
	mov	r0, r4
.L2645:
	bl	nandc_set_seed
	ldr	r3, .L2649+12
	mov	r1, #0
	mov	r0, r8
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r9
	str	r10, [sp]
	bl	nandc_xfer
	bl	nandc_de_cs.constprop.35
.L2618:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2625:
	mov	r1, #0
	cmp	r0, #0
	str	r1, [r2, #2056]
	add	r3, r7, r3
	str	r1, [r2, #2052]
	str	r1, [r2, #2052]
	lsr	r1, r4, #16
	str	lr, [r2, #2052]
	str	ip, [r2, #2052]
	str	r1, [r2, #2052]
	lsrne	r1, r4, #24
	strne	r1, [r2, #2052]
	mov	r1, #5
	str	r1, [r3, #2056]
	mov	r1, #0
	str	r1, [r2, #2052]
	str	r1, [r2, #2052]
	b	.L2644
.L2650:
	.align	2
.L2649:
	.word	.LANCHOR0
	.word	.LANCHOR1+1978
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_complete_plane_page_read, .-flash_complete_plane_page_read
	.align	2
	.global	flash_complete_page_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #1
	ldr	r4, .L2683
	mov	fp, r2
	.pad #20
	sub	sp, sp, #20
	mov	r10, r1
	str	r0, [sp, #12]
	ubfx	r6, r0, #24, #2
	ldrb	r7, [r4, #1153]	@ zero_extendqisi2
	rsb	r2, r7, #24
	lsl	r7, r3, r7
	lsl	r5, r3, r2
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	sub	r7, r7, #1
	sub	r5, r5, #1
	and	r7, r7, r0, lsr r2
	and	r5, r5, r0
	uxtb	r7, r7
	cmp	r3, r7
	bhi	.L2652
	mov	r2, #1232
	ldr	r1, .L2683+4
	ldr	r0, .L2683+8
	bl	printk
	bl	dump_stack
.L2652:
	add	r7, r4, r7
	ldrb	r9, [r7, #1144]	@ zero_extendqisi2
	ldr	r7, [r4, #1044]
	mov	r0, r9
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2653
	mov	r0, r5
	bl	slc_phy_page_address_calc
	mov	r5, r0
.L2653:
	ldr	r8, .L2683+12
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2654
	add	r7, r7, r9, lsl #8
	mov	r3, #5
	str	r3, [r7, #2056]
	mov	r3, #0
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	uxtb	r3, r5
	str	r3, [r7, #2052]
	lsr	r3, r5, #8
	str	r3, [r7, #2052]
	lsr	r3, r5, #16
	str	r3, [r7, #2052]
	mov	r3, #224
	str	r3, [r7, #2056]
.L2654:
	ldr	r3, [r4, #1104]
	cmp	r6, #0
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	sub	r3, r3, #3
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2655
	ldrb	r3, [r4, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2655
	ldrb	r3, [r4, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r5, r5, lsl #1
	subeq	r0, r6, #1
	addeq	r0, r0, r3
	beq	.L2682
.L2655:
	mov	r0, r5
.L2682:
	bl	nandc_set_seed
	ldrb	r2, [r8, #13]	@ zero_extendqisi2
	mov	r3, r10
	str	fp, [sp]
	mov	r1, #0
	mov	r0, r9
	bl	nandc_xfer
	cmn	r0, #1
	bne	.L2657
	ldrb	r7, [r4, #1196]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L2658
	mov	r3, #0
	mov	r2, r10
	strb	r3, [r4, #1196]
	orr	r1, r5, r6, lsl #24
	ldrb	r3, [r8, #13]	@ zero_extendqisi2
	mov	r0, r9
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	cmp	r6, #0
	strb	r7, [r4, #1196]
	bne	.L2659
.L2664:
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2659
	ldrb	r3, [r4, #1193]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	blt	.L2659
	ldrb	r3, [r8, #23]	@ zero_extendqisi2
	sub	r3, r3, #4
	cmp	r3, #4
	movls	r0, #256
.L2651:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2659:
	cmn	r0, #1
	bne	.L2651
.L2665:
	ldr	r3, .L2683+16
	ldr	r7, [r3, #-100]
	cmp	r7, #0
	bne	.L2661
.L2663:
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r2, [sp, #12]
	ldr	r0, .L2683+20
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	mvneq	r0, #0
	beq	.L2651
	ldrb	r3, [r8, #13]	@ zero_extendqisi2
	mov	r2, r10
	orr	r1, r5, r6, lsl #24
	mov	r0, r9
	str	r3, [sp]
	mov	r3, fp
	bl	flash_ddr_tuning_read
	b	.L2651
.L2661:
	ldrb	r3, [r8, #13]	@ zero_extendqisi2
	mov	r2, r10
	orr	r1, r5, r6, lsl #24
	mov	r0, r9
	str	r3, [sp]
	mov	r3, fp
	blx	r7
	cmn	r0, #1
	bne	.L2651
	b	.L2663
.L2658:
	cmp	r6, #0
	beq	.L2664
	b	.L2665
.L2657:
	cmp	r6, #0
	bne	.L2651
	b	.L2664
.L2684:
	.align	2
.L2683:
	.word	.LANCHOR0
	.word	.LANCHOR1+2009
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC172
	.fnend
	.size	flash_complete_page_read, .-flash_complete_page_read
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	queue_wait_first_req_completed, %function
queue_wait_first_req_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L2779
	ldrb	r5, [r4, #2770]	@ zero_extendqisi2
	cmp	r5, #255
	bne	.L2686
.L2719:
	mov	r7, #0
	b	.L2685
.L2686:
	mov	r8, #48
	mla	r2, r8, r5, r4
	ldrb	r3, [r2, #1274]	@ zero_extendqisi2
	ldr	r7, [r2, #1256]
	sub	r2, r3, #1
	cmp	r2, #10
	ldrls	pc, [pc, r2, asl #2]
	b	.L2719
.L2689:
	.word	.L2688
	.word	.L2690
	.word	.L2691
	.word	.L2691
	.word	.L2691
	.word	.L2691
	.word	.L2692
	.word	.L2693
	.word	.L2694
	.word	.L2691
	.word	.L2694
.L2688:
	mla	r8, r8, r5, r4
	bl	nandc_wait_flash_ready
	ldr	r3, [r8, #1240]
	ldr	r1, [r8, #1236]
	cmp	r3, #0
	beq	.L2695
	ldr	r2, .L2779+4
	ldrb	r0, [r8, #1272]	@ zero_extendqisi2
	ldrb	r2, [r2, #-2546]	@ zero_extendqisi2
	cmp	r0, r2
	moveq	r1, r3
.L2695:
	mov	r3, #48
	mov	r0, r7
	mla	r4, r3, r5, r4
	ldr	r2, [r4, #1244]
	bl	flash_complete_page_read
	str	r0, [r4, #1268]
.L2778:
	mov	r3, #13
	strb	r3, [r4, #1274]
	ldrb	r3, [r4, #1234]	@ zero_extendqisi2
	orr	r3, r3, #8
	strb	r3, [r4, #1234]
	b	.L2719
.L2690:
	bl	nandc_wait_flash_ready
	mla	r3, r8, r5, r4
	ldrb	r6, [r3, #1232]	@ zero_extendqisi2
	ldr	r2, [r3, #1240]
	ldr	r9, [r3, #1236]
	mla	r8, r8, r6, r4
	cmp	r2, #0
	ldr	r10, [r8, #1236]
	beq	.L2696
	ldrb	r1, [r3, #1272]	@ zero_extendqisi2
	ldr	r3, .L2779+4
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r9, r2
.L2696:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #1240]
	cmp	r2, #0
	beq	.L2697
	ldrb	r1, [r3, #1272]	@ zero_extendqisi2
	ldr	r3, .L2779+4
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r10, r2
.L2697:
	mov	r7, #48
	mov	r1, r9
	mla	r3, r7, r5, r4
	mla	r7, r7, r6, r4
	ldr	r2, [r3, #1244]
	ldr	r0, [r3, #1256]
	str	r3, [sp, #12]
	bl	flash_complete_plane_page_read
	ldr	r2, [r7, #1244]
	mov	r8, r0
	mov	r1, r10
	ldr	r0, [r7, #1256]
	bl	flash_complete_plane_page_read
	cmn	r8, #1
	mov	fp, r0
	beq	.L2698
	ldr	r3, [sp, #12]
	ldr	r2, [r3, #1252]
	cmn	r2, #1
	beq	.L2699
	ldr	r3, [r3, #1244]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2699
.L2698:
	mov	r7, #48
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mla	r7, r7, r5, r4
	mvn	ip, #0
	mvn	ip, ip, lsl r3
	rsb	r1, r3, #24
	ldr	lr, [r7, #1256]
	mov	r2, r9
	ldr	r3, .L2779+4
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	and	r0, ip, lr, lsr r1
	bic	r1, lr, ip, lsl r1
	uxtb	r0, r0
	str	r3, [sp]
	ldr	r3, [r7, #1244]
	bl	flash_read_page_en
	ldr	r2, [r7, #1252]
	mov	r8, r0
	cmn	r2, #1
	beq	.L2700
	ldr	r3, [r7, #1244]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2700
	ldr	r0, .L2779+8
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2700
	str	r1, [sp]
	ldr	r0, .L2779+12
	ldr	r3, [r3]
	ldr	r1, [r7, #1256]
	bl	printk
.L2700:
	mov	r3, #48
	mla	r3, r3, r5, r4
	ldr	r2, [r3, #1252]
	cmn	r2, #1
	beq	.L2699
	ldr	r3, [r3, #1244]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2699
	movw	r2, #431
	ldr	r1, .L2779+16
	ldr	r0, .L2779+20
	bl	printk
	bl	dump_stack
.L2699:
	mov	r3, #48
	mov	r2, #13
	mla	r5, r3, r5, r4
	cmn	fp, #1
	strb	r2, [r5, #1274]
	ldrb	r2, [r5, #1234]	@ zero_extendqisi2
	str	r8, [r5, #1268]
	orr	r2, r2, #8
	strb	r2, [r5, #1234]
	beq	.L2701
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #1252]
	cmn	r2, #1
	beq	.L2703
	ldr	r3, [r3, #1244]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2703
.L2701:
	mov	r5, #48
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mla	r5, r5, r6, r4
	mvn	ip, #0
	mvn	ip, ip, lsl r3
	rsb	r1, r3, #24
	ldr	lr, [r5, #1256]
	mov	r2, r10
	ldr	r3, .L2779+4
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	and	r0, ip, lr, lsr r1
	bic	r1, lr, ip, lsl r1
	uxtb	r0, r0
	str	r3, [sp]
	ldr	r3, [r5, #1244]
	bl	flash_read_page_en
	ldr	r2, [r5, #1252]
	cmn	r2, #1
	beq	.L2705
	ldr	r3, [r5, #1244]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2705
	ldr	r0, .L2779+8
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2705
	str	r1, [sp]
	ldr	r0, .L2779+12
	ldr	r3, [r3]
	ldr	r1, [r5, #1256]
	bl	printk
.L2705:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #1252]
	cmn	r2, #1
	beq	.L2703
	ldr	r3, [r3, #1244]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2703
	movw	r2, #450
	ldr	r1, .L2779+16
	ldr	r0, .L2779+20
	bl	printk
	bl	dump_stack
.L2703:
	mov	r3, #48
	mla	r4, r3, r6, r4
	str	r8, [r4, #1268]
	b	.L2778
.L2691:
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r7
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	beq	.L2719
	ands	r2, r0, #5
	mov	r3, #48
	beq	.L2707
	mla	r4, r3, r5, r4
	mov	r3, #12
	mov	r2, r0
	ldr	r0, .L2779+24
	strb	r3, [r4, #1274]
	ldrb	r1, [r4, #1233]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r4, #1256]
	bl	printk
.L2777:
	mvn	r3, #0
	str	r3, [r4, #1268]
	b	.L2685
.L2707:
	mul	r3, r3, r5
	mov	r1, #13
	add	r7, r4, r3
	str	r2, [r7, #1268]
	ldr	r2, [r4, #2800]
	strb	r1, [r7, #1274]
	ldr	r1, [r2, #156]
	ldr	r2, .L2779+28
	cmp	r1, r2
	bne	.L2719
	ldr	r2, .L2779+32
	add	r3, r2, r3
	ldrh	r3, [r3, #34]
	cmp	r3, #0
	bne	.L2719
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mvn	ip, #0
	ldr	lr, [r7, #1256]
	ldr	r6, .L2779+4
	rsb	r1, r3, #24
	mvn	ip, ip, lsl r3
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	and	r0, ip, lr, lsr r1
	bic	r1, lr, ip, lsl r1
	str	r3, [sp]
	uxtb	r0, r0
	ldr	r3, [r6, #-96]
	ldr	r2, [r6, #-92]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2708
	ldr	r1, [r7, #1244]
	ldr	r2, [r6, #-96]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2719
.L2708:
	mov	r2, #48
	ldr	r0, .L2779+36
	mla	r5, r2, r5, r4
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	ldrb	r1, [r5, #1233]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r5, #1256]
	bl	printk
	mvn	r3, #0
	str	r3, [r5, #1268]
	b	.L2719
.L2694:
	ldrb	r1, [r4, #1153]	@ zero_extendqisi2
	mvn	r2, #0
	cmp	r3, #11
	ldr	r9, .L2779+32
	moveq	r3, #10
	movne	r3, #3
	rsb	r0, r1, #24
	mov	lr, r3
	mvn	r2, r2, lsl r1
	add	r3, r5, r5, lsl #1
	and	r1, r2, r7, lsr r0
	add	r3, r9, r3, lsl #4
	uxth	r1, r1
	mov	r6, #48
	movw	r10, #1256
.L2710:
	ldrb	r8, [r3]	@ zero_extendqisi2
	cmp	r8, #255
	mvneq	r7, #0
	beq	.L2685
.L2717:
	mla	ip, r6, r8, r4
	movw	fp, #1274
	add	r3, r8, r8, lsl #1
	ldrb	fp, [ip, fp]	@ zero_extendqisi2
	add	r3, r9, r3, lsl #4
	cmp	fp, lr
	bne	.L2710
	ldr	ip, [ip, r10]
	and	ip, r2, ip, lsr r0
	cmp	r1, ip
	bne	.L2710
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r7
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	bne	.L2712
.L2776:
	mov	r7, #0
	b	.L2713
.L2712:
	ands	r3, r0, #15
	mul	r6, r6, r5
	beq	.L2714
	add	r6, r4, r6
	mov	r9, #12
	ldrb	r1, [r6, #1233]	@ zero_extendqisi2
	mov	r2, r0
	str	r9, [sp]
	ldr	r0, .L2779+40
	ldr	r3, [r6, #1256]
	bl	printk
	mvn	r3, #0
	strb	r9, [r6, #1274]
	str	r3, [r6, #1268]
.L2713:
	mov	r3, #48
	mla	r5, r3, r5, r4
	mla	r8, r3, r8, r4
	ldrb	r2, [r5, #1274]	@ zero_extendqisi2
	ldr	r3, [r5, #1268]
	strb	r2, [r8, #1274]
	str	r3, [r8, #1268]
.L2685:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2714:
	add	r7, r4, r6
	mov	r2, #13
	str	r3, [r7, #1268]
	ldr	r3, [r4, #2800]
	strb	r2, [r7, #1274]
	ldr	r2, [r3, #156]
	ldr	r3, .L2779+28
	cmp	r2, r3
	bne	.L2776
	add	r6, r9, r6
	ldrh	r3, [r6, #34]
	cmp	r3, #0
	bne	.L2776
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mvn	ip, #0
	ldr	lr, [r7, #1256]
	ldr	r6, .L2779+4
	rsb	r1, r3, #24
	mvn	ip, ip, lsl r3
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	and	r0, ip, lr, lsr r1
	bic	r1, lr, ip, lsl r1
	str	r3, [sp]
	uxtb	r0, r0
	ldr	r3, [r6, #-96]
	ldr	r2, [r6, #-92]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2716
	ldr	r1, [r7, #1244]
	ldr	r2, [r6, #-96]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2776
.L2716:
	mov	r6, #48
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	mla	r6, r6, r5, r4
	ldr	r0, .L2779+44
	ldrb	r1, [r6, #1233]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r6, #1256]
	bl	printk
	mvn	r3, #0
	str	r3, [r6, #1268]
	b	.L2776
.L2692:
	mov	r0, r7
	mov	r1, #32
	bl	flash_wait_device_ready
	tst	r0, #32
	mov	r7, r0
	beq	.L2719
	ands	r2, r0, #15
	mov	r3, #48
	mla	r4, r3, r5, r4
	movne	r3, #12
	strbne	r3, [r4, #1274]
	bne	.L2777
.L2718:
	mov	r3, #13
	str	r2, [r4, #1268]
	strb	r3, [r4, #1274]
	b	.L2719
.L2693:
	mov	r1, #64
	mov	r0, r7
	bl	flash_wait_device_ready
	tst	r0, #64
	movne	r3, #48
	mlane	r4, r3, r5, r4
	movne	r3, #7
	strne	r0, [r4, #1268]
	strbne	r3, [r4, #1274]
	b	.L2719
.L2780:
	.align	2
.L2779:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC173
	.word	.LANCHOR1+2034
	.word	.LC0
	.word	.LC174
	.word	1145785929
	.word	.LANCHOR0+1232
	.word	.LC175
	.word	.LC176
	.word	.LC177
	.fnend
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_prog_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_prog_page, %function
sblk_prog_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldrh	r3, [r0, #34]
	mov	r5, r1
	cmp	r3, #0
	beq	.L2782
	ldr	r3, .L2812
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L2782
	mov	r2, r1
	ldr	r1, [r0, #24]
	ldr	r0, .L2812+4
	bl	printk
.L2782:
	ldr	r8, .L2812+8
	mov	r6, #0
	ldr	fp, .L2812+12
.L2783:
	cmp	r5, #0
	bne	.L2794
.L2810:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2794:
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldr	r7, [r4, #24]
.L2784:
	mov	r1, #1
	mov	r0, r7
	bl	queue_lun_state
	cmp	r0, #0
	bne	.L2785
	cmp	r5, #1
	beq	.L2786
	ldrb	r3, [r8, #1194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2786
	ldrb	r3, [r8, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2787
.L2786:
	mov	r0, r4
	bl	queue_prog_cmd
.L2788:
	subs	r5, r5, #1
	beq	.L2810
	ldr	r4, .L2812+16
	add	r9, r9, r9, lsl #1
	add	r4, r4, r9, lsl #4
	b	.L2783
.L2785:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2784
.L2787:
	ldrb	r2, [r8, #1153]	@ zero_extendqisi2
	mvn	r3, #0
	rsb	r1, r2, #24
	mvn	r3, r3, lsl r2
	ldrb	r2, [r4]	@ zero_extendqisi2
	and	r3, r3, r7, lsr r1
	cmp	r2, #255
	uxth	r3, r3
	bne	.L2789
	movw	r2, #697
	ldr	r1, .L2812+20
	ldr	r0, .L2812+24
	str	r3, [sp, #4]
	bl	printk
	bl	dump_stack
	ldr	r3, [sp, #4]
.L2789:
	ldrb	r2, [r4]	@ zero_extendqisi2
	mov	r1, #48
	mov	r10, #1
	mla	r2, r1, r2, r8
	ldrb	r1, [r8, #1153]	@ zero_extendqisi2
	ldr	r0, [r2, #1256]
	rsb	r2, r1, #24
	lsl	r1, r10, r1
	sub	r1, r1, #1
	and	r1, r1, r0, lsr r2
	uxth	r1, r1
	cmp	r3, r1
	bne	.L2790
	ldr	r3, .L2812+28
	ldrb	lr, [fp, #-3136]	@ zero_extendqisi2
	ldrh	ip, [r3, #-2]
	sub	lr, lr, #1
	sub	r3, r2, ip
	lsl	r3, r10, r3
	lsl	r2, r10, ip
	sub	r3, r3, #1
	sub	r2, r2, #1
	and	r3, r3, lr
	uxth	r2, r2
	uxth	r3, r3
	and	r1, r3, r7, lsr ip
	and	r3, r3, r0, lsr ip
	and	ip, r2, r7
	subs	r7, r1, r3
	and	r2, r2, r0
	movne	r7, #1
	cmp	ip, r2
	movne	r7, #0
	cmp	r7, #0
	beq	.L2790
	cmp	r6, lr
	beq	.L2790
	ldr	r3, [r4, #12]
	mov	r0, #17
	ldr	r2, [r4, #4]
	add	r6, r6, r10
	ldr	r1, [r4, #24]
	bl	flash_start_page_prog
	mov	r3, #9
	strb	r10, [r4, #43]
	strb	r3, [r4, #42]
	mvn	r3, #0
	strb	r3, [r4]
	mov	r1, r4
	ldr	r0, .L2812+32
	bl	buf_add_tail
	b	.L2788
.L2790:
	mov	r0, r4
	mov	r6, #0
	bl	queue_prog_cmd
	b	.L2788
.L2813:
	.align	2
.L2812:
	.word	.LANCHOR2
	.word	.LC178
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1232
	.word	.LANCHOR1+2065
	.word	.LC0
	.word	.LANCHOR3-3136
	.word	.LANCHOR0+2770
	.fnend
	.size	sblk_prog_page, .-sblk_prog_page
	.align	2
	.global	sblk_wait_write_queue_completed
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L2818
.L2815:
	ldrb	r3, [r4, #2770]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2816
	pop	{r4, pc}
.L2816:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2815
.L2819:
	.align	2
.L2818:
	.word	.LANCHOR0
	.fnend
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
	.align	2
	.global	ftl_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_flush, %function
ftl_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L2826
	ldr	r5, .L2826+4
	ldrb	r1, [r4, #2797]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2821
	ldrb	r3, [r5, #-88]	@ zero_extendqisi2
	add	r0, r4, #1232
	add	r3, r3, r3, lsl #1
	add	r0, r0, r3, lsl #4
	bl	sblk_prog_page
.L2821:
	mvn	r3, #0
	strb	r3, [r5, #-88]
	mov	r3, #0
	strb	r3, [r4, #2797]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	movw	r0, #65535
	pop	{r4, r5, r6, lr}
	b	ftl_vpn_decrement
.L2827:
	.align	2
.L2826:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	ftl_flush, .-ftl_flush
	.align	2
	.global	zftl_cache_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_cache_flush, %function
zftl_cache_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2836
	ldrb	r3, [r3, #2797]	@ zero_extendqisi2
	cmp	r3, #0
	bxeq	lr
	push	{r4, lr}
	.save {r4, lr}
	bl	timer_get_time
	ldr	r3, .L2836+4
	ldr	r3, [r3, #-84]
	add	r3, r3, #100
	cmp	r0, r3
	popls	{r4, pc}
	pop	{r4, lr}
	b	ftl_flush
.L2837:
	.align	2
.L2836:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	ftl_read_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_read_page, %function
ftl_read_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r8, [sp, #24]
	mov	r5, r1
	mov	r6, r2
	mov	r7, r3
	bl	sblk_wait_write_queue_completed
	mov	r3, r7
	str	r8, [sp, #24]
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, lr}
	b	flash_read_page_en
	.fnend
	.size	ftl_read_page, .-ftl_read_page
	.align	2
	.global	ftl_read_ppa_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_read_ppa_page, %function
ftl_read_ppa_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L2842
	mov	r5, r0
	mvn	r4, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r0, [r3, #1153]	@ zero_extendqisi2
	rsb	r3, r0, #24
	mvn	r4, r4, lsl r0
	and	r4, r4, r5, lsr r3
	uxtb	r4, r4
	bl	sblk_wait_write_queue_completed
	mov	r3, r7
	str	r8, [sp]
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	flash_read_page_en
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2843:
	.align	2
.L2842:
	.word	.LANCHOR0
	.fnend
	.size	ftl_read_ppa_page, .-ftl_read_ppa_page
	.align	2
	.global	sblk_read_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_read_page, %function
sblk_read_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r10, r0
	mov	r7, r1
	mov	r4, r0
	mov	r5, r1
.L2845:
	cmp	r5, #0
	bne	.L2855
.L2868:
	ldr	r4, .L2870
.L2856:
	cmp	r7, #0
	bne	.L2858
	mov	r0, r7
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2855:
	ldrb	r8, [r4]	@ zero_extendqisi2
	ldr	fp, [r4, #24]
.L2846:
	mov	r1, #0
	mov	r0, fp
	bl	queue_lun_state
	cmp	r0, #0
	bne	.L2847
	cmp	r5, #1
	beq	.L2852
	ldr	r3, .L2870+4
	ldrb	r3, [r3, #-80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2849
.L2852:
	mov	r0, r4
	bl	queue_read_cmd
	b	.L2850
.L2847:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2846
.L2849:
	ldr	r3, .L2870+8
	ldrb	r2, [r3, #1153]	@ zero_extendqisi2
	mvn	r3, #0
	rsb	r1, r2, #24
	mvn	r3, r3, lsl r2
	ldrb	r2, [r4]	@ zero_extendqisi2
	and	r3, r3, fp, lsr r1
	cmp	r2, #255
	uxth	r3, r3
	bne	.L2851
	movw	r2, #782
	ldr	r1, .L2870+12
	ldr	r0, .L2870+16
	str	r3, [sp]
	bl	printk
	bl	dump_stack
	ldr	r3, [sp]
.L2851:
	ldr	r2, .L2870+8
	mov	r6, #48
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldrb	ip, [r2, #1153]	@ zero_extendqisi2
	mla	r6, r6, r9, r2
	mvn	r2, #0
	mvn	r2, r2, lsl ip
	rsb	r0, ip, #24
	ldr	r1, [r6, #1256]
	and	r2, r2, r1, lsr r0
	uxth	r2, r2
	cmp	r3, r2
	bne	.L2852
	ldr	r3, .L2870+20
	ldrh	r3, [r3]
	add	fp, r3, fp
	cmp	r1, fp
	bne	.L2852
	ldr	r0, [r4, #24]
	mvn	fp, #0
	ldrb	r8, [r6, #1232]	@ zero_extendqisi2
	add	r9, r9, r9, lsl #1
	add	r5, r5, fp
	bl	flash_start_plane_read
	mov	r2, #2
	mov	r3, #0
	strb	r2, [r4, #42]
	mov	r1, r4
	strb	r3, [r4, #43]
	ldr	r0, .L2870+24
	strb	fp, [r4]
	str	r2, [sp, #4]
	str	r3, [sp]
	bl	buf_add_tail
	ldr	r1, .L2870
	ldr	r2, [sp, #4]
	ldr	r3, [sp]
	strb	fp, [r6, #1232]
	strb	r2, [r6, #1274]
	add	r1, r1, r9, lsl #4
	strb	r3, [r6, #1275]
	ldr	r0, .L2870+24
	bl	buf_add_tail
.L2850:
	subs	r5, r5, #1
	beq	.L2868
	ldr	r4, .L2870
	add	r8, r8, r8, lsl #1
	add	r4, r4, r8, lsl #4
	b	.L2845
.L2858:
	ldrb	r3, [r10, #42]	@ zero_extendqisi2
	cmp	r3, #13
	bne	.L2857
	ldrb	r3, [r10]	@ zero_extendqisi2
	sub	r7, r7, #1
	cmp	r3, #255
	addne	r3, r3, r3, lsl #1
	addne	r10, r4, r3, lsl #4
.L2857:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2856
.L2871:
	.align	2
.L2870:
	.word	.LANCHOR0+1232
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR1+2080
	.word	.LC0
	.word	.LANCHOR3-3074
	.word	.LANCHOR0+2770
	.fnend
	.size	sblk_read_page, .-sblk_read_page
	.align	2
	.global	gc_check_data_one_wl
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_check_data_one_wl, %function
gc_check_data_one_wl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L2906
	ldr	r3, [r5, #2832]
	ldr	r10, [r5, #1096]
	cmp	r3, #0
	bne	.L2873
	mov	r0, #1
	bl	buf_alloc
	str	r0, [r5, #2832]
.L2873:
	ldr	r4, [r5, #2832]
	cmp	r4, #0
	bne	.L2874
	movw	r2, #729
	ldr	r1, .L2906+4
	ldr	r0, .L2906+8
	bl	printk
	bl	dump_stack
.L2874:
	ldr	r7, .L2906+12
	add	fp, r10, #96
	mov	r9, #0
.L2875:
	ldrb	r3, [r10, #89]	@ zero_extendqisi2
	ldr	r6, .L2906+16
	cmp	r9, r3
	bge	.L2886
	mov	r8, #1
	b	.L2887
.L2885:
	ldr	r2, .L2906+20
	ldrb	r1, [r7, #-3128]	@ zero_extendqisi2
	ldrh	r3, [fp]
	ldrh	r2, [r2, #-2]
	cmp	r1, #2
	mul	r2, r2, r3
	beq	.L2876
	ldrb	r3, [r5, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2877
.L2876:
	ldrh	r3, [r6, #12]
	sub	r3, r3, #1
	add	r3, r3, r2
	add	r3, r3, r8
	orr	r3, r3, r1, lsl #24
.L2905:
	str	r3, [r4, #24]
	mov	r1, #1
	mov	r0, r4
	bl	sblk_read_page
	ldr	r2, [r4, #36]
	cmn	r2, #1
	beq	.L2881
	ldr	r0, [r7, #-132]
	ldrh	r1, [r6, #18]
	ldr	r3, [r4, #12]
	ldr	ip, [r0, r1, lsl #2]
	ldr	r0, [r3, #4]
	cmp	ip, r0
	bne	.L2881
	ldr	r0, [r7, #-128]
	ldr	r3, [r3, #8]
	ldr	r1, [r0, r1, lsl #2]
	cmp	r1, r3
	beq	.L2882
.L2881:
	ldrh	r3, [r6, #18]
	ldr	r1, [r7, #-132]
	lsl	r0, r3, #2
	ldr	r3, [r1, r3, lsl #2]
	cmn	r3, #1
	beq	.L2882
	ldr	r1, .L2906+24
	ldr	r1, [r1]
	tst	r1, #1024
	beq	.L2883
	ldr	r1, [r4, #12]
	ldr	ip, [r1, #12]
	str	ip, [sp, #16]
	ldr	ip, [r1, #8]
	str	ip, [sp, #12]
	ldr	ip, [r1, #4]
	str	ip, [sp, #8]
	ldr	r1, [r1]
	str	r1, [sp, #4]
	ldr	r1, [r7, #-128]
	ldr	r1, [r1, r0]
	ldr	r0, .L2906+28
	str	r1, [sp]
	ldr	r1, [r4, #24]
	bl	printk
.L2883:
	ldrh	r3, [r10, #80]
	mov	r1, #0
	ldr	r2, [r5, #1092]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, [r5, #1096]
	ldr	r3, [r2, #556]
	add	r3, r3, #1
	str	r3, [r2, #556]
	ldr	r3, [r5, #2800]
	ldr	r2, [r3, #156]
	ldr	r3, .L2906+32
	cmp	r2, r3
	bne	.L2890
	ldrb	r3, [r7, #-2542]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2890
	ldrb	r3, [r7, #-3126]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2890
	ldr	r0, [r4, #24]
	bl	ftl_mask_bad_block
.L2890:
	mvn	r0, #0
	b	.L2872
.L2877:
	ldrh	r3, [r6, #12]
	cmp	r1, #3
	addne	r3, r3, r2
	bne	.L2905
	ldrb	r1, [r5, #1159]	@ zero_extendqisi2
	cmp	r1, #0
	addne	r3, r3, r3, lsl #1
	addeq	r3, r3, r2
	orreq	r3, r3, r8, lsl #24
	subne	r3, r3, #1
	addne	r3, r3, r2
	addne	r3, r3, r8
	orrne	r3, r3, #50331648
	b	.L2905
.L2882:
	ldrh	r3, [r6, #18]
	add	r8, r8, #1
	add	r3, r3, #1
	strh	r3, [r6, #18]	@ movhi
.L2887:
	ldrh	r3, [r6, #16]
	cmp	r8, r3
	ble	.L2885
	add	r9, r9, #1
	add	fp, fp, #2
	b	.L2875
.L2886:
	ldrh	r3, [r6, #12]
	add	r2, r3, #1
	strh	r2, [r6, #12]	@ movhi
	ldr	r2, .L2906+12
	ldrb	r0, [r2, #-3126]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r0, #0
	addne	r3, r3, #2
	strhne	r3, [r6, #12]	@ movhi
.L2884:
.L2872:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2907:
	.align	2
.L2906:
	.word	.LANCHOR0
	.word	.LANCHOR1+2095
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR0+2824
	.word	.LANCHOR3-3072
	.word	.LANCHOR2
	.word	.LC179
	.word	1145785929
	.fnend
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	sblk_tlc_prog_one_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_tlc_prog_one_page, %function
sblk_tlc_prog_one_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r3, [r0]
	ldr	r6, [r3, #24]
.L2909:
	mov	r1, #1
	mov	r0, r6
	bl	queue_lun_state
	subs	r5, r0, #0
	bne	.L2910
	mov	r0, r4
	mov	r1, #1
	bl	queue_tlc_prog_cmd
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L2910:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2909
	.fnend
	.size	sblk_tlc_prog_one_page, .-sblk_tlc_prog_one_page
	.align	2
	.global	sblk_xlc_prog_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_xlc_prog_pages, %function
sblk_xlc_prog_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r3, [r0]
	.pad #20
	sub	sp, sp, #20
	mov	r7, r1
	mov	fp, r2
	ldr	r4, [r3, #24]
.L2913:
	mov	r1, #1
	mov	r0, r4
	bl	queue_lun_state
	subs	r10, r0, #0
	bne	.L2914
	cmp	fp, #2
	bne	.L2915
	ldr	r1, .L2925
	ldr	lr, [r7]
	ldrb	r3, [r1, #1194]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r4, [lr, #24]
	beq	.L2920
	ldrb	r3, [r1, #1153]	@ zero_extendqisi2
	mov	r9, #1
	ldr	r2, [r5]
	ldr	lr, [lr, #24]
	rsb	r0, r3, #24
	ldr	ip, [r2, #24]
	lsl	r4, r9, r3
	ldrb	r3, [r1, #1159]	@ zero_extendqisi2
	lsl	r6, r9, r0
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, ip, lsr r0
	cmp	r3, #0
	and	r8, ip, r6
	uxtb	r4, r4
	and	r6, r6, lr
	beq	.L2917
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r5]
	mov	r1, r9
	mov	r0, r10
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #17
	ldr	r3, [r3, #4]
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7]
	mov	r1, r9
	mov	r0, r10
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r6, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r1, fp
	mov	r0, r10
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #17
	ldr	r3, [r3, #4]
	str	r3, [sp, #4]
	add	r3, r8, r9
	str	r3, [sp]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #4]
	mov	r1, fp
	mov	r0, r10
	add	r8, r8, #2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r3, [sp, #4]
	add	r3, r6, r9
	str	r3, [sp]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #8]
	mov	r1, #3
	mov	r0, r10
	add	r6, r6, #2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #17
	ldr	r3, [r3, #4]
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #8]
	mov	r1, #3
	mov	r0, r10
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #16
	ldr	r3, [r3, #4]
	str	r6, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
.L2918:
	ldr	r1, [r5]
	mov	r3, #5
	ldr	r0, .L2925+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	bl	buf_add_tail
.L2919:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2914:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2913
.L2917:
	ldr	r3, [r2, #12]
	mov	r1, r9
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, [r2, #4]
	mov	r2, #17
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7]
	mov	r1, r9
	ldr	r2, [r5]
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r6, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldm	r5, {r2, r3}
	mov	r1, fp
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #17
	ldr	r3, [r3, #4]
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #4]
	mov	r1, fp
	ldr	r2, [r5]
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #26
	ldr	r3, [r3, #4]
	str	r6, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #8]
	mov	r1, #3
	ldr	r2, [r5]
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #17
	ldr	r3, [r3, #4]
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #8]
	mov	r1, #3
	ldr	r2, [r5]
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	mov	r2, #16
	ldr	r3, [r3, #4]
	str	r6, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	b	.L2918
.L2921:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
.L2920:
	mov	r1, #1
	mov	r0, r4
	bl	queue_lun_state
	subs	r6, r0, #0
	bne	.L2921
	mov	r1, #1
	mov	r0, r5
	bl	queue_tlc_prog_cmd
	mov	r1, r6
	mov	r0, r7
	bl	queue_tlc_prog_cmd
.L2922:
	mov	r1, #1
	mov	r0, r4
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2919
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2922
.L2915:
	mov	r1, #1
	mov	r0, r5
	bl	queue_tlc_prog_cmd
	b	.L2919
.L2926:
	.align	2
.L2925:
	.word	.LANCHOR0
	.word	.LANCHOR0+2770
	.fnend
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	sblk_3d_mlc_prog_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	sblk_3d_mlc_prog_pages, %function
sblk_3d_mlc_prog_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r7, r1
	ldr	r9, .L2933
	add	r5, r0, #8
	mov	r8, #1
	ldr	r10, .L2933+4
.L2928:
	cmp	r7, #0
	bne	.L2931
	mov	r0, r7
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2931:
	ldr	r3, [r5, #-8]
	ldr	r4, [r3, #24]
.L2929:
	mov	r1, #1
	mov	r0, r4
	bl	queue_lun_state
	cmp	r0, #0
	bne	.L2930
	ldr	r3, [r5, #-8]
	sub	r7, r7, #1
	add	r5, r5, #8
	ldr	r1, [r3, #24]
	ldrb	r3, [r9, #1153]	@ zero_extendqisi2
	rsb	r2, r3, #24
	lsl	r4, r8, r3
	lsl	r6, r8, r2
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, r1, lsr r2
	and	r6, r6, r1
	uxtb	r4, r4
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r5, #-16]
	mov	r1, r4
	mov	r0, #16
	ldr	r2, [r3, #12]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [r3, #4]
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #-12]
	mov	r1, r4
	mov	r0, #16
	ldr	r2, [r3, #12]
	str	r2, [sp]
	add	r2, r6, #1
	ldr	r3, [r3, #4]
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_de_cs.constprop.35
	ldr	r1, [r5, #-16]
	mov	r3, #4
	mov	r0, r10
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r8, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2928
.L2930:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2929
.L2934:
	.align	2
.L2933:
	.word	.LANCHOR0
	.word	.LANCHOR0+2770
	.fnend
	.size	sblk_3d_mlc_prog_pages, .-sblk_3d_mlc_prog_pages
	.align	2
	.global	flash_prog_page_en
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	.fnstart
	@ args = 8, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r10, r3
	ldr	r4, .L2966
	mov	r8, r0
	mov	r5, r1
	ldrb	r3, [sp, #68]	@ zero_extendqisi2
	mov	fp, r2
	ubfx	r7, r1, #24, #2
	str	r3, [sp, #20]
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2936
	movw	r2, #642
	ldr	r1, .L2966+4
	ldr	r0, .L2966+8
	bl	printk
	bl	dump_stack
.L2936:
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r8
	mvnls	r0, #0
	bls	.L2935
	add	r3, r4, r8
	cmp	r7, #0
	ldrb	r3, [r3, #1144]	@ zero_extendqisi2
	str	r3, [sp, #12]
	bne	.L2949
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2939
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2949
.L2939:
	ldrh	r7, [r4, #2]
	mov	r0, r5
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	mul	r6, r7, r0
	mov	r0, r5
	bl	__aeabi_uidivmod
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	lsl	r1, r1, #1
	cmp	r3, #0
	addeq	r4, r4, r1
	ldrheq	r1, [r4, #4]
	add	r6, r1, r6
.L2938:
	ldr	r4, .L2966+12
	ldr	r7, .L2966+16
.L2944:
	ldr	r3, [r4]
	tst	r3, #16
	beq	.L2941
	mov	r3, r6
	mov	r2, r5
	ldr	r1, [sp, #12]
	ldr	r0, .L2966+20
	bl	printk
.L2941:
	ldr	r3, [sp, #64]
	mov	r2, fp
	mov	r1, r6
	ldr	r0, [sp, #12]
	str	r3, [sp]
	mov	r3, r10
	bl	flash_prog_page
	ldr	r3, [sp, #20]
	str	r0, [sp, #16]
	cmp	r3, #0
	beq	.L2942
	ldr	r3, [sp, #64]
	mov	r1, r5
	mov	r0, r8
	str	r3, [sp]
	ldr	r3, [r7, #-96]
	ldr	r2, [r7, #-92]
	bl	flash_read_page_en
	cmn	r0, #1
	cmpne	r0, #512
	mov	r9, r0
	beq	.L2943
	ldr	r3, [r7, #-92]
	ldr	r2, [fp]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2943
	ldr	r3, [r7, #-96]
	ldr	r2, [r10]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2942
.L2943:
	mov	r3, #4
	mov	r1, fp
	mov	r2, r3
	ldr	r0, .L2966+24
	bl	rknand_print_hex
	mov	r3, #4
	mov	r1, r10
	mov	r2, r3
	ldr	r0, .L2966+28
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r7, #-96]
	mov	r2, r3
	ldr	r0, .L2966+32
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r7, #-92]
	mov	r2, r3
	ldr	r0, .L2966+36
	bl	rknand_print_hex
	cmp	r9, #512
	beq	.L2944
.L2946:
	mov	r1, r5
	ldr	r0, .L2966+40
	bl	printk
	mvn	r4, #0
	movw	r2, #685
	ldr	r1, .L2966+4
	ldr	r0, .L2966+8
	bl	printk
	bl	dump_stack
.L2945:
	mov	r0, r4
.L2935:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2949:
	mov	r6, r5
	b	.L2938
.L2942:
	ldr	r4, [sp, #16]
	cmn	r4, #1
	bne	.L2945
	b	.L2946
.L2967:
	.align	2
.L2966:
	.word	.LANCHOR0
	.word	.LANCHOR1+2116
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC180
	.word	.LC181
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.word	.LC185
	.fnend
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_prog_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_prog_page, %function
ftl_prog_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r4, r0
	mov	r7, r3
	mov	r5, r1
	mov	r6, r2
	bl	sblk_wait_write_queue_completed
	mov	r3, #1
	mov	r0, r4
	str	r3, [sp, #4]
	mov	r2, r6
	ldr	r3, [sp, #32]
	mov	r1, r5
	str	r3, [sp]
	mov	r3, r7
	bl	flash_prog_page_en
	cmn	r0, #1
	mov	r4, r0
	bne	.L2968
	ldr	r1, .L2971
	movw	r2, #2678
	ldr	r0, .L2971+4
	bl	printk
	bl	dump_stack
	mov	r1, r5
	ldr	r0, .L2971+8
	bl	printk
.L2968:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2972:
	.align	2
.L2971:
	.word	.LANCHOR1+2135
	.word	.LC0
	.word	.LC185
	.fnend
	.size	ftl_prog_page, .-ftl_prog_page
	.align	2
	.global	ftl_info_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_info_flush, %function
ftl_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L3010
	.pad #20
	sub	sp, sp, #20
	mov	r9, r0
	mov	r8, #0
	ldr	r6, .L3010+4
	ldrb	r2, [r4, #-2546]	@ zero_extendqisi2
	ldr	r0, [r4, #-76]
	ldr	r10, .L3010+8
	ldr	r7, .L3010+12
	lsl	r2, r2, #1
	bl	ftl_memset
	ldr	r3, [r6, #2800]
	ldrh	r2, [r3, #74]
	cmp	r2, #1
	movhi	r2, #0
	strhhi	r2, [r3, #150]	@ movhi
.L2987:
	ldr	r3, .L3010+16
	ldr	r2, [r6, #2800]
	ldrb	r5, [r4, #-56]	@ zero_extendqisi2
	ldrh	fp, [r3]
	ldrh	r3, [r4, #-54]
	str	r3, [sp, #8]
	ldr	r3, [r2, #4]
	add	r3, r3, #1
	str	r3, [r2, #4]
	ldr	r3, [r4, #-76]
	str	r9, [r3]
	ldr	r2, [r6, #2800]
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	ldr	r3, [r4, #-76]
	ldr	r2, [r2, #4]
	ldr	r0, [r4, #-52]
	lsl	r1, r1, #9
	str	r3, [sp, #12]
	str	r2, [r3, #4]
	bl	js_hash
	ldr	r3, [sp, #12]
	str	r0, [r3, #8]
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L2975
	ldr	r3, [r6, #2800]
	ldrh	r2, [r4, #-54]
	ldrb	r1, [r4, #-56]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	ldr	r0, .L3010+20
	bl	printk
.L2975:
	ldr	r3, .L3010+24
	ldrh	r0, [r4, #-54]
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L2976
	ldr	fp, .L3010+28
	ldr	r5, .L3010+32
.L2983:
	ldrb	r3, [r4, #-55]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #7
	strb	r3, [r4, #-55]
	bls	.L2977
	mov	r3, #8
.L2982:
	ldr	r2, [r6, #1040]
	sub	r8, r3, #8
	uxth	r8, r8
	add	r2, r2, r3
	ldrb	r1, [r2, #32]	@ zero_extendqisi2
	add	r2, r1, #127
	uxtb	r2, r2
	cmp	r2, #125
	bhi	.L2978
	movw	r2, #846
	mov	r1, fp
	mov	r0, r5
	bl	printk
	bl	dump_stack
.L2981:
	strb	r8, [r4, #-55]
	mov	r8, #1
.L2977:
	ldr	r3, [r6, #1040]
	ldrb	r2, [r4, #-55]	@ zero_extendqisi2
	add	r3, r3, r2
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	strb	r3, [r4, #-56]
	beq	.L2983
	ldrh	r5, [r7, #-2]
	mov	r0, #0
	mul	r5, r3, r5
	mov	r1, r5
	bl	flash_erase_block
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	mov	r1, r5
	ldr	r2, [r4, #-52]
	mov	r0, #0
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	bl	ftl_prog_page
	mov	r3, #1
	add	r5, r5, r3
	strh	r3, [r4, #-54]	@ movhi
.L2984:
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	mov	r1, r5
	ldr	r2, [r4, #-52]
	mov	r0, #0
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	bl	ftl_prog_page
	ldrh	r3, [r4, #-54]
	cmn	r0, #1
	add	r3, r3, #1
	strh	r3, [r4, #-54]	@ movhi
	beq	.L2985
	ldrb	r3, [r4, #-48]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2986
.L2985:
	mov	r3, #0
	strb	r3, [r4, #-48]
	b	.L2987
.L2978:
	cmp	r1, #255
	bne	.L2981
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L2982
	mov	r8, #8
	b	.L2981
.L2976:
	ldr	r3, [sp, #8]
	cmp	r0, #0
	mla	r5, fp, r5, r3
	bne	.L2984
	mov	r1, r5
	bl	flash_erase_block
	b	.L2984
.L2986:
	cmp	r8, #1
	beq	.L2988
.L2996:
	ldrb	r3, [r4, #-56]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2990
	movw	r2, #890
	ldr	r1, .L3010+28
	ldr	r0, .L3010+32
	bl	printk
	bl	dump_stack
.L2990:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2988:
	ldrb	r5, [r4, #-55]	@ zero_extendqisi2
	ldr	r8, .L3010+28
	ldr	r9, .L3010+32
	add	r5, r5, #1
.L2991:
	cmp	r5, #7
	bhi	.L2996
	ldr	r3, [r6, #1040]
	add	r3, r3, r5
	ldrb	r10, [r3, #40]	@ zero_extendqisi2
	add	r3, r10, #127
	uxtb	r3, r3
	cmp	r3, #125
	bhi	.L2992
	movw	r2, #881
	mov	r1, r8
	mov	r0, r9
	bl	printk
	bl	dump_stack
.L2993:
	ldrh	r1, [r7, #-2]
	mov	r0, #0
	mul	r1, r10, r1
	bl	flash_erase_block
	b	.L2994
.L2992:
	cmp	r10, #255
	bne	.L2993
.L2994:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L2991
.L3011:
	.align	2
.L3010:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3-3072
	.word	.LANCHOR3-3074
	.word	.LC186
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2149
	.word	.LC0
	.fnend
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #1
	ldr	r4, .L3043
	mov	r6, #0
	mov	r1, r6
	mov	r2, #16384
	ldr	r5, .L3043+4
	.pad #20
	sub	sp, sp, #20
	strb	r3, [r4, #-48]
	strb	r3, [r4, #-46]
	movw	r3, #1080
	ldr	r0, [r4, #-52]
	ldrh	r3, [r5, r3]
	strb	r6, [r4, #-47]
	str	r0, [r5, #1084]
	ldr	r10, .L3043+8
	add	r3, r0, r3, lsl #2
	ldr	r9, .L3043+12
	str	r3, [r5, #2800]
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #16384
	ldr	r0, [r4, #-44]
	bl	ftl_memset
	ldr	r3, [r5, #1040]
	strb	r6, [r4, #-55]
	strh	r6, [r4, #-54]	@ movhi
	mov	r6, #7
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	strb	r3, [r4, #-56]
.L3014:
	ldr	r3, [r5, #1040]
	sxth	r8, r6
	add	r3, r3, r6
	ldrb	r7, [r3, #40]	@ zero_extendqisi2
	cmp	r7, #255
	bne	.L3013
.L3018:
	subs	r6, r6, #1
	bcs	.L3014
	mov	r7, r6
	mov	r8, #0
.L3015:
	ldr	r6, .L3043+8
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3019
	ldr	r3, [r5, #2800]
	mov	r2, #4800
	mov	r1, r7
	ldr	r0, .L3043+16
	ldr	r3, [r3]
	bl	printk
.L3019:
	cmn	r7, #1
	bne	.L3020
	mov	r2, #16384
	mov	r1, #0
	ldr	r0, [r4, #-52]
	bl	ftl_memset
	ldr	r3, [r5, #2800]
	ldr	r2, .L3043+12
	str	r2, [r3]
	ldr	r2, .L3043+20
	ldr	r3, [r5, #2800]
	str	r2, [r3, #12]
.L3042:
	mov	r0, r7
.L3012:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3013:
	ldr	r3, .L3043+24
	mov	r0, #0
	ldr	r2, [r4, #-52]
	ldrh	r3, [r3]
	mul	r7, r7, r3
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r1, r7
	ldr	r3, [r4, #-76]
	bl	ftl_read_page
	cmn	r0, #1
	mov	fp, r0
	bne	.L3016
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	add	r1, r7, #1
	ldr	r2, [r4, #-52]
	mov	r0, #0
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	bl	ftl_read_page
	mov	fp, r0
.L3016:
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L3017
	ldr	r3, [r5, #2800]
	mov	r2, fp
	mov	r1, r6
	ldr	r0, .L3043+28
	ldr	r3, [r3]
	str	r3, [sp]
	movw	r3, #749
	bl	printk
.L3017:
	cmn	fp, #1
	beq	.L3018
	ldr	r3, [r5, #2800]
	ldr	r3, [r3]
	cmp	r3, r9
	bne	.L3018
	mov	r7, r8
	b	.L3015
.L3020:
	ldr	r3, [r5, #1040]
	mov	r0, #0
	ldr	r2, [r4, #-52]
	mov	r7, #0
	strb	r8, [r4, #-55]
	add	r3, r3, r8
	ldr	r10, .L3043+12
	ldrb	r1, [r3, #40]	@ zero_extendqisi2
	mov	r3, #4
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	strb	r1, [r4, #-56]
	bl	flash_get_last_written_page
	uxth	r9, r0
	ldrb	r8, [r4, #-56]	@ zero_extendqisi2
	ldr	fp, .L3043+32
	add	r3, r9, #1
	uxth	r3, r3
	str	r3, [sp, #8]
	ldr	r3, .L3043+36
	ldrh	r3, [r3, #-2]
	mla	r8, r3, r8, r0
.L3022:
	sub	r3, r9, r7
	tst	r3, #32768
	bne	.L3026
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	sub	r1, r8, r7
	ldr	r2, [r4, #-52]
	mov	r0, #0
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	bl	ftl_read_page
	cmn	r0, #1
	beq	.L3023
	ldr	r3, [r5, #2800]
	ldr	r3, [r3]
	cmp	r3, r10
	bne	.L3023
	ldr	r3, [r4, #-76]
	ldr	r3, [r3, #8]
	cmp	r3, #0
	bne	.L3024
.L3026:
	ldrh	r3, [sp, #8]
	strh	r3, [r4, #-54]	@ movhi
	bl	ftl_tmp_into_update
	ldr	r2, [r5, #2800]
	mov	r0, #0
	ldr	r3, [r2, #64]
	add	r3, r3, #1
	str	r3, [r2, #64]
	bl	ftl_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	ldr	r7, [r6]
	ands	r7, r7, #16384
	beq	.L3042
	ldr	r3, [r5, #2800]
	ldr	r0, .L3043+40
	ldr	r1, [r3, #156]
	bl	printk
	mov	r0, #0
	b	.L3012
.L3024:
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	ldr	r0, [r4, #-52]
	str	r3, [sp, #12]
	lsl	r1, r1, #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3026
	ldr	r3, [r4, #-76]
	mov	r0, fp
	ldr	r1, [r3, #8]
	bl	printk
.L3023:
	add	r7, r7, #1
	b	.L3022
.L3044:
	.align	2
.L3043:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1229739078
	.word	.LC188
	.word	393252
	.word	.LANCHOR3-3074
	.word	.LC187
	.word	.LC189
	.word	.LANCHOR3-3072
	.word	.LC190
	.fnend
	.size	ftl_info_blk_init, .-ftl_info_blk_init
	.align	2
	.global	ftl_ext_info_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_ext_info_flush, %function
ftl_ext_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	bl	timer_get_time
	ldr	r3, .L3061
	ldr	r5, .L3061+4
	umull	r0, r1, r0, r3
	ldr	r3, [r5, #1096]
	ldr	r0, [r3, #520]
	lsr	r1, r1, #5
	cmp	r1, r0
	bls	.L3046
	ldr	r2, [r3, #12]
	sub	r2, r2, r0
	add	r2, r2, r1
	str	r2, [r3, #12]
.L3060:
	str	r1, [r3, #520]
	b	.L3047
.L3046:
	bcc	.L3060
.L3047:
	ldr	r4, .L3061+8
	mov	r0, #0
	bl	ftl_total_vpn_update
	sub	r7, r4, #3136
.L3051:
	ldr	r3, [r5, #2800]
	ldr	r2, [r3, #56]
	add	r2, r2, #1
	str	r2, [r3, #56]
	ldrh	r2, [r3, #140]
	ldr	r3, .L3061+12
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L3049
	bl	ftl_ext_alloc_new_blk
.L3049:
	ldr	r3, [r5, #2800]
	ldrh	r2, [r3, #130]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3050
	movw	r2, #2211
	ldr	r1, .L3061+16
	ldr	r0, .L3061+20
	bl	printk
	bl	dump_stack
.L3050:
	ldr	r2, [r5, #2800]
	ldrb	r6, [r5, #1153]	@ zero_extendqisi2
	ldrh	r1, [r7, #-2]
	ldrh	r3, [r2, #130]
	rsb	r6, r6, #24
	ldr	r0, [r4, #-76]
	sub	r6, r6, r1
	mvn	r1, #0
	asr	r8, r3, r6
	bic	r6, r3, r1, lsl r6
	ldr	r3, .L3061+24
	sxth	r6, r6
	ldrh	r1, [r3, #-2]
	ldrh	r3, [r2, #140]
	ldrb	r2, [r4, #-2546]	@ zero_extendqisi2
	mla	r6, r1, r6, r3
	lsl	r2, r2, #1
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r4, #-76]
	mov	r2, #0
	str	r2, [r3]
	ldr	r3, [r5, #2800]
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	ldr	r9, [r4, #-76]
	ldr	r3, [r3, #56]
	ldr	r0, [r4, #-44]
	lsl	r1, r1, #9
	str	r3, [r9, #4]
	bl	js_hash
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	mov	r1, r6
	str	r0, [r9, #8]
	uxtb	r0, r8
	ldr	r2, [r4, #-44]
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	bl	ftl_prog_page
	ldr	r2, [r5, #2800]
	ldrh	r3, [r2, #140]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	strh	r3, [r2, #140]	@ movhi
	beq	.L3051
	cmn	r0, #1
	beq	.L3052
	ldrb	r0, [r4, #-46]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3053
.L3052:
.L3048:
	mov	r3, #0
	strb	r3, [r4, #-46]
	b	.L3051
.L3053:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3062:
	.align	2
.L3061:
	.word	1374389535
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2164
	.word	.LC0
	.word	.LANCHOR3-3072
	.fnend
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r2, #0
	ldr	r4, .L3083
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L3083+4
	sub	r3, r4, #2544
	strh	r2, [r3, #-4]	@ movhi
	sub	r2, r4, #3136
	ldr	r3, [r5, #2800]
	ldrh	r2, [r2, #-2]
	ldrh	r6, [r3, #130]
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	sub	r3, r3, r2
	mvn	r2, #0
	asr	r7, r6, r3
	bic	r6, r6, r2, lsl r3
	uxtb	r10, r7
	mov	r3, #4
	str	r3, [sp]
	mov	r1, r6
	ldr	r3, [r4, #-76]
	mov	r0, r10
	ldr	r2, [r4, #-44]
	bl	flash_get_last_written_page
	ldr	r3, .L3083+8
	mov	r8, r0
	ldr	r2, [r3]
	str	r3, [sp, #8]
	tst	r2, #4096
	beq	.L3064
	ldr	r3, [r5, #2800]
	uxth	r7, r7
	mov	r2, #2256
	ldr	r1, .L3083+12
	ldrh	r3, [r3, #130]
	str	r7, [sp]
	str	r3, [sp, #4]
	mov	r3, r0
	ldr	r0, .L3083+16
	bl	printk
.L3064:
	ldr	fp, .L3083+20
	mov	r9, #0
.L3065:
	uxth	r7, r8
	sub	r3, r7, r9
	tst	r3, #32768
	bne	.L3070
	ldr	r3, .L3083+24
	sub	r1, r8, r9
	ldrh	r0, [r3]
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	mla	r1, r6, r0, r1
	str	r3, [sp]
	ldr	r3, [r4, #-76]
	ldr	r2, [r4, #-44]
	mov	r0, r10
	bl	flash_read_page_en
	cmp	r0, #512
	cmnne	r0, #1
	beq	.L3066
	ldr	r3, [r5, #1096]
	ldr	r2, .L3083+28
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L3066
	ldr	r3, [r4, #-76]
	ldr	r3, [r3, #8]
	cmp	r3, #0
	bne	.L3067
.L3070:
	bl	zftl_sblk_list_init
	ldr	r3, [r5, #2800]
	ldrh	r2, [r3, #140]
	cmp	r2, r8
	bgt	.L3069
	add	r7, r7, #1
	strh	r7, [r3, #140]	@ movhi
	bl	ftl_ext_info_flush
.L3069:
	ldr	r4, [r5, #1096]
	bl	timer_get_time
	ldr	r3, .L3083+32
	umull	r0, r1, r0, r3
	lsr	r3, r1, #5
	str	r3, [r4, #520]
	ldr	r4, [r5, #1096]
	bl	timer_get_time
	ldr	r3, [r5, #1096]
	mov	r2, #0
	str	r0, [r4, #604]
	add	r1, r3, #584
	str	r2, [r3, #608]
	mvn	r2, #0
	strh	r2, [r1]	@ movhi
	movw	r1, #586
	strh	r2, [r3, r1]	@ movhi
	add	r1, r3, #588
	strh	r2, [r1]	@ movhi
	movw	r1, #590
	strh	r2, [r3, r1]	@ movhi
	movw	r1, #65535
	str	r2, [r3, #564]
	ldr	r2, [sp, #8]
	str	r1, [r3, #560]
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L3072
	ldr	r4, [r3, #520]
	ldr	r5, [r3, #12]
	bl	timer_get_time
	mov	r2, r4
	str	r0, [sp]
	mov	r3, r5
	ldr	r1, .L3083+12
	ldr	r0, .L3083+36
	bl	printk
.L3072:
	ldr	r3, .L3083+40
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, #-4]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3067:
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	ldr	r0, [r4, #-44]
	str	r3, [sp, #12]
	lsl	r1, r1, #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3070
	ldr	r3, [r4, #-76]
	mov	r0, fp
	ldr	r1, [r3, #8]
	bl	printk
.L3066:
	add	r9, r9, #1
	b	.L3065
.L3084:
	.align	2
.L3083:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+2183
	.word	.LC191
	.word	.LC192
	.word	.LANCHOR3-3074
	.word	1162432070
	.word	1374389535
	.word	.LC193
	.word	.LANCHOR3-3152
	.fnend
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_prog_ppa_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_prog_ppa_page, %function
ftl_prog_ppa_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L3087
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldrb	r5, [ip, #1153]	@ zero_extendqisi2
	mov	ip, #1
	str	r3, [sp]
	mov	r3, r2
	mov	r2, r1
	rsb	r4, r5, #24
	lsl	lr, ip, r4
	lsl	ip, ip, r5
	sub	ip, ip, #1
	sub	lr, lr, #1
	and	ip, ip, r0, lsr r4
	and	r1, lr, r0
	uxtb	r0, ip
	bl	ftl_prog_page
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L3088:
	.align	2
.L3087:
	.word	.LANCHOR0
	.fnend
	.size	ftl_prog_ppa_page, .-ftl_prog_ppa_page
	.align	2
	.global	ftl_write_last_log_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_write_last_log_page, %function
ftl_write_last_log_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r3, [r0, #6]
	cmp	r3, #1
	bne	.L3091
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r5, .L3097
	ldrh	r8, [r0, #12]
	ldr	r6, [r5, #-2556]
	bl	ftl_get_new_free_page
	cmn	r0, #1
	mov	r7, r0
	beq	.L3092
	ldrh	r0, [r4]
	add	r6, r6, r8, lsl #2
	bl	ftl_vpn_decrement
	ldr	r3, [r5, #-40]
	mov	r0, r6
	ldr	r2, .L3097+4
	str	r2, [r3]
	sub	r3, r5, #3088
	ldrh	r3, [r3, #-8]
	ldrb	r1, [r5, #-3127]	@ zero_extendqisi2
	ldr	r4, [r5, #-40]
	mul	r1, r1, r3
	lsl	r1, r1, #2
	bl	js_hash
	ldr	r1, [r5, #-40]
	str	r0, [r4, #4]
	mov	r4, #0
	mov	r2, r4
	mov	r0, #2
	str	r4, [r1, #8]
	str	r4, [r1, #12]
	str	r4, [r1, #16]!
	bl	ftl_debug_info_fill
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	mov	r1, r6
	ldr	r2, [r5, #-40]
	mov	r0, r7
	bl	ftl_prog_ppa_page
.L3092:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3091:
	mvn	r0, #0
	bx	lr
.L3098:
	.align	2
.L3097:
	.word	.LANCHOR3
	.word	-178307901
	.fnend
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1080
	ldr	r4, .L3138
	.pad #108
	sub	sp, sp, #108
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	bls	.L3099
	ldr	r3, .L3138+4
	ldrb	r2, [r3, #-3120]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3101
	ldrb	r2, [r3, #-3126]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3099
.L3101:
	ldrb	r2, [r4, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3099
	ldr	r2, [r4, #1084]
	lsl	r7, r0, #2
	ldr	r5, .L3138+8
	add	r1, sp, #88
	mov	r10, r0
	strh	r0, [sp, #72]	@ movhi
	add	r2, r2, r7
	mov	r6, #0
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #160
	ldrbeq	r8, [r3, #-3128]	@ zero_extendqisi2
	movne	r8, #1
	bl	ftl_get_blk_list_in_sblk
	ldrh	r3, [r5, #-8]
	uxtb	r0, r0
	strh	r6, [sp, #74]	@ movhi
	strb	r0, [sp, #81]
	strb	r6, [sp, #77]
	smulbb	r0, r3, r0
	ldr	r3, [r4, #1084]
	strh	r6, [sp, #82]	@ movhi
	strh	r0, [sp, #78]	@ movhi
	add	r2, r3, r7
	ldr	r1, [r3, r10, lsl #2]
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L3138+12
	ubfx	r1, r1, #11, #8
	str	r1, [sp, #4]
	mov	r1, r10
	ldrh	r3, [r3, r7]
	mov	r7, r6
	ubfx	r3, r3, #0, #11
	str	r3, [sp]
	ubfx	r3, r2, #3, #2
	lsr	r2, r2, #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	r4, r0
	add	r3, r5, #16
	str	r6, [sp, #44]
	str	r3, [sp, #64]
.L3103:
	ldr	r3, .L3138+16
	uxth	r5, r6
	ldrh	r3, [r3]
	cmp	r3, r5
	bls	.L3111
	lsl	r3, r5, #1
	str	r5, [sp, #52]
	mov	fp, #0
	sub	r2, r3, #1
	add	r3, r3, r5
	sub	r3, r3, #1
	str	r2, [sp, #48]
	str	r3, [sp, #56]
	b	.L3114
.L3112:
	ldr	r2, [sp, #60]
	add	r3, sp, #104
	add	r3, r3, r2
	ldrh	r3, [r3, #-32]
	str	r3, [sp, #40]
	movw	r3, #65535
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L3104
.L3110:
	add	r7, r7, #1
	uxth	r7, r7
.L3105:
	cmp	r8, r7
	bcs	.L3112
	add	fp, fp, #1
.L3114:
	uxth	r3, fp
	uxth	r2, fp
	str	r3, [sp, #44]
	ldrb	r3, [sp, #81]	@ zero_extendqisi2
	cmp	r3, r2
	bls	.L3113
	mov	r7, #1
	add	r3, r2, #8
	lsl	r3, r3, r7
	str	r3, [sp, #60]
	b	.L3105
.L3104:
	ldr	r3, [sp, #64]
	cmp	r8, #3
	ldr	r2, [sp, #40]
	ldrh	r3, [r3, #-2]
	mul	r3, r2, r3
	bne	.L3106
	ldr	r2, .L3138
	ldrb	r2, [r2, #1159]	@ zero_extendqisi2
	cmp	r2, #0
	uxtaheq	r3, r3, r6
	ldrne	r2, [sp, #56]
	orreq	r3, r3, r7, lsl #24
	beq	.L3108
.L3137:
	add	r3, r2, r3
	ldr	r2, .L3138+4
	add	r3, r3, r7
	ldrb	r2, [r2, #-3128]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #24
.L3108:
	str	r3, [r4, #24]
	mov	r1, #1
	mov	r0, r4
	str	r3, [sp, #68]
	bl	sblk_read_page
	ldr	r9, [r4, #36]
	ldr	r3, [sp, #68]
	cmn	r9, #1
	cmpne	r9, #512
	bne	.L3110
	ldr	r1, [r4, #12]
	ldr	r2, [r4, #4]
	ldr	r0, [r1, #12]
	str	r0, [sp, #32]
	ldr	r0, [r1, #8]
	str	r0, [sp, #28]
	ldr	r0, [r1, #4]
	str	r0, [sp, #24]
	ldr	r1, [r1]
	ldr	r0, .L3138+20
	str	r1, [sp, #20]
	ldr	r1, [r2, #12]
	str	r1, [sp, #16]
	ldr	r1, [r2, #8]
	str	r1, [sp, #12]
	ldr	r1, [r2, #4]
	str	r1, [sp, #8]
	ldr	r2, [r2]
	ldr	r1, [sp, #40]
	str	r9, [sp]
	str	r2, [sp, #4]
	ldr	r2, [sp, #52]
	bl	printk
	cmp	r9, #512
	bne	.L3110
.L3111:
	uxth	r2, r6
	ldr	r6, .L3138+4
	ldr	r3, [sp, #44]
	mov	r1, r10
	str	r7, [sp]
	ldr	r0, .L3138+24
	bl	printk
	ldrb	r2, [r6, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r4, #4]
	ldr	fp, .L3138+28
	lsl	r2, r2, #9
	bl	ftl_memset
	ldrb	r2, [r6, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r4, #12]
	lsl	r2, r2, #1
	bl	ftl_memset
.L3115:
	ldr	r7, .L3138+16
	ldrh	r3, [r7]
	cmp	r3, r5
	bls	.L3121
	lsl	r3, r5, #1
	mov	r8, #0
	ldr	r9, .L3138+32
	add	r7, r7, #24
	str	r3, [sp, #40]
	b	.L3122
.L3106:
	cmp	r8, #2
	uxtahne	r3, r3, r6
	bne	.L3108
	ldr	r2, [sp, #48]
	b	.L3137
.L3113:
	add	r6, r6, #1
	b	.L3103
.L3120:
	ldr	r3, [fp]
	tst	r3, #4096
	uxth	r3, r8
	beq	.L3116
	mov	r2, r3
	mov	r1, r5
	mov	r0, r9
	str	r3, [sp, #44]
	bl	printk
	ldr	r3, [sp, #44]
.L3116:
	ldrb	r2, [r6, #-3120]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3117
	add	r2, sp, #104
	mov	r0, r4
	add	r3, r2, r3, lsl #1
	ldrh	r2, [r3, #-16]
	ldrh	r3, [r7, #-2]
	mul	r3, r3, r2
	orr	r3, r3, r5
	str	r3, [r4, #24]
	bl	sblk_3d_tlc_dump_prog
.L3118:
	add	r8, r8, #1
.L3122:
	ldrb	r2, [sp, #81]	@ zero_extendqisi2
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L3120
	add	r5, r5, #1
	uxth	r5, r5
	b	.L3115
.L3117:
	ldrb	r1, [r6, #-3128]	@ zero_extendqisi2
	cmp	r1, #2
	bne	.L3119
	add	r2, sp, #104
	mov	r0, r4
	add	r3, r2, r3, lsl #1
	ldrh	r2, [r3, #-16]
	ldrh	r3, [r7, #-2]
	mul	r3, r3, r2
	ldr	r2, [sp, #40]
	orr	r3, r3, r2
	orr	r3, r3, #33554432
	str	r3, [r4, #24]
	bl	sblk_mlc_dump_prog
	b	.L3118
.L3119:
	add	r1, sp, #104
	ldrh	ip, [r7, #-2]
	add	r3, r1, r3, lsl #1
	ldrh	r1, [r3, #-16]
	ldr	r3, .L3138
	mul	ip, ip, r1
	ldrb	r1, [r3, #1153]	@ zero_extendqisi2
	mvn	r3, #0
	rsb	lr, r1, #24
	orr	ip, ip, r5
	mvn	r1, r3, lsl r1
	str	ip, [r4, #24]
	str	r2, [sp, #4]
	and	r0, r1, ip, lsr lr
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	bic	r1, ip, r1, lsl lr
	uxtb	r0, r0
	str	r3, [sp]
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #4]
	bl	flash_prog_page_en
	b	.L3118
.L3121:
	mov	r0, r4
	bl	zbuf_free
	mov	r1, r10
	ldr	r0, .L3138+36
	bl	printk
.L3099:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3139:
	.align	2
.L3138:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC194
	.word	.LANCHOR3-3096
	.word	.LC195
	.word	.LC196
	.word	.LANCHOR2
	.word	.LC197
	.word	.LC198
	.fnend
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	gc_ink_check_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_ink_check_sblk, %function
gc_ink_check_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movw	r3, #2106
	ldr	r4, .L3169
	ldr	r5, .L3169+4
	ldrh	r2, [r4, r3]
	ldr	r6, [r5, #904]
	cmp	r2, #3
	ldrls	pc, [pc, r2, asl #2]
	b	.L3141
.L3143:
	.word	.L3142
	.word	.L3144
	.word	.L3145
	.word	.L3146
.L3142:
	ldr	r8, .L3169+8
	movw	r3, #2788
	ldrh	r3, [r8, r3]
	cmp	r3, #7
	bls	.L3140
	ldrb	r3, [r8, #2769]	@ zero_extendqisi2
	cmp	r3, #2
	bls	.L3140
	ldr	r7, .L3169+12
	mov	r1, #0
	movw	r9, #65535
	sub	r6, r7, #3072
	sub	r6, r6, #8
	mov	r0, r6
	bl	_list_get_gc_head_node
	cmp	r0, r9
	beq	.L3140
	ldr	r3, [r8, #1084]
	lsl	r0, r0, #2
	ldrh	r3, [r3, r0]
	ubfx	r3, r3, #0, #11
	cmp	r3, #2
	bgt	.L3140
	mov	r0, #1
	bl	buf_alloc
	cmp	r0, #0
	str	r0, [r5, #904]
	beq	.L3140
	mov	r0, r6
	ldr	r2, .L3169+16
	mov	r1, #0
	bl	_list_pop_index_node
	uxth	r10, r0
	mov	r6, r0
	cmp	r10, r9
	bne	.L3150
	ldr	r0, [r5, #904]
	bl	zbuf_free
	mov	r3, #0
	str	r3, [r5, #904]
.L3140:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3150:
	add	r1, r4, #2144
	mov	r0, r10
	add	r1, r1, #14
	uxth	r6, r6
	bl	ftl_get_blk_list_in_sblk
	movw	r3, #2142
	mov	r2, #0
	strh	r10, [r4, r3]	@ movhi
	add	r3, r4, #2144
	strh	r2, [r3]	@ movhi
	mov	r2, #1
	movw	r3, #2106
	lsl	r6, r6, #2
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r8, #1084]
	strb	r0, [r5, #879]
	ldrb	r2, [r7, #-2546]	@ zero_extendqisi2
	ldrh	r3, [r3, r6]
	tst	r3, #1
	ldr	r3, [r5, #904]
	lsl	r2, r2, #9
	movne	r1, #85
	moveq	r1, #170
	ldr	r0, [r3, #4]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	ftl_memset
.L3144:
	movw	r3, #2142
	mov	r1, #0
	ldrh	r0, [r4, r3]
	bl	ftl_erase_sblk
	mov	r2, #2
.L3168:
	movw	r3, #2106
.L3166:
	strh	r2, [r4, r3]	@ movhi
	b	.L3140
.L3145:
	ldr	r8, .L3169+12
	mov	r7, #0
	ldr	r10, .L3169+20
	bl	sblk_wait_write_queue_completed
	sub	r9, r8, #3072
.L3152:
	ldrb	r2, [r5, #879]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L3154
	add	r2, r4, #2144
	ldr	r1, .L3169+24
	ldrh	r3, [r2]
	ldrh	r1, [r1, #-8]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r1, r3
	strh	r3, [r2]	@ movhi
	bhi	.L3140
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	mov	r2, #3
	b	.L3168
.L3154:
	uxth	r2, r7
	add	r3, r4, #2128
	add	r3, r3, #14
	add	r3, r3, r2, lsl #1
	ldrh	r1, [r3, #16]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3153
	add	r3, r4, #2144
	ldrh	ip, [r9, #-2]
	ldrh	r3, [r3]
	mla	ip, r1, ip, r3
	ldr	r3, [r6, #4]
	str	ip, [r3]
	ldr	r3, [r6, #4]
	str	r10, [r3, #4]
	mov	r3, #0
	ldr	r2, [r6, #12]
	str	r3, [r2]
	ldr	r2, .L3169+8
	ldrb	r1, [r2, #1153]	@ zero_extendqisi2
	mvn	r2, #0
	str	r3, [sp, #4]
	ldrb	r3, [r8, #-2546]	@ zero_extendqisi2
	rsb	lr, r1, #24
	mvn	r1, r2, lsl r1
	str	r3, [sp]
	and	r0, r1, ip, lsr lr
	ldr	r3, [r6, #12]
	ldr	r2, [r6, #4]
	bic	r1, ip, r1, lsl lr
	uxtb	r0, r0
	bl	flash_prog_page_en
.L3153:
	add	r7, r7, #1
	b	.L3152
.L3146:
	add	r10, r4, #2128
	mov	r9, #0
	add	r10, r10, #14
	bl	sblk_wait_write_queue_completed
.L3155:
	ldrb	r2, [r5, #879]	@ zero_extendqisi2
	uxth	r3, r9
	cmp	r2, r3
	bhi	.L3159
	add	r2, r4, #2144
	ldrh	r3, [r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2]	@ movhi
	ldr	r2, .L3169+24
	ldrh	r2, [r2, #-8]
	cmp	r2, r3
	bhi	.L3140
	movw	r3, #2106
	mov	r6, #0
	ldr	r0, [r5, #904]
	strh	r6, [r4, r3]	@ movhi
	bl	zbuf_free
	movw	r2, #2108
	str	r6, [r5, #904]
	ldrh	r3, [r4, r2]
	cmp	r3, #15
	bhi	.L3160
	add	r1, r3, #1
	add	r3, r3, #1040
	strh	r1, [r4, r2]	@ movhi
	movw	r2, #2142
	add	r3, r3, #12
	ldrh	r2, [r4, r2]
	add	r3, r4, r3, lsl #1
	strh	r2, [r3, #6]	@ movhi
.L3161:
	movw	r3, #2108
	ldr	r0, .L3169+28
	ldrh	r2, [r4, r3]
	movw	r3, #2142
	ldrh	r1, [r4, r3]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L3159:
	uxth	r7, r9
	movw	r2, #65535
	add	r7, r7, #8
	lsl	r7, r7, #1
	ldrh	r3, [r10, r7]
	cmp	r3, r2
	beq	.L3157
	ldr	r2, .L3169+32
	mov	r1, #1
	mov	r0, r6
	ldrh	r8, [r2]
	add	r2, r4, #2144
	ldrh	r2, [r2]
	mla	r8, r3, r8, r2
	str	r8, [r6, #24]
	bl	sblk_read_page
	ldr	r3, [r6, #4]
	ldr	r3, [r3]
	cmp	r8, r3
	beq	.L3157
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r3, #0
	strh	r3, [r10, r7]	@ movhi
.L3157:
	add	r9, r9, #1
	b	.L3155
.L3160:
	movw	r3, #2142
	ldrh	r0, [r4, r3]
	bl	zftl_insert_free_list
	b	.L3161
.L3141:
	mov	r2, #0
	b	.L3166
.L3170:
	.align	2
.L3169:
	.word	.LANCHOR0+2824
	.word	.LANCHOR0+4096
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+2788
	.word	1437269760
	.word	.LANCHOR3-3088
	.word	.LC199
	.word	.LANCHOR3-3074
	.fnend
	.size	gc_ink_check_sblk, .-gc_ink_check_sblk
	.align	2
	.global	ftl_ink_check_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_ink_check_sblk, %function
ftl_ink_check_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	r9, r0, #2
	ldr	r6, .L3190
	mov	r5, r0
	mov	r1, r0
	.pad #52
	sub	sp, sp, #52
	ldr	r2, [r6, #1084]
	ldr	r3, [r2, r0, lsl #2]
	ldrh	r2, [r2, r9]
	ldr	r0, .L3190+4
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	bl	printk
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3171
	movw	r3, #1080
	ldrh	r3, [r6, r3]
	cmp	r3, r5
	bls	.L3171
	mov	r1, #0
	mov	r0, r5
	bl	ftl_erase_sblk
	mov	r0, r5
	add	r1, sp, #32
	strh	r5, [sp, #16]	@ movhi
	mov	r8, #0
	bl	ftl_get_blk_list_in_sblk
	strb	r0, [sp, #25]
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r6, #1084]
	mov	r4, r0
	ldr	r10, .L3190+8
	ldr	fp, .L3190+12
	ldrh	r3, [r3, r9]
	ldrb	r2, [r10, #-2546]	@ zero_extendqisi2
	ldr	r0, [r0, #4]
	ubfx	r3, r3, #0, #11
	cmp	r3, #1
	lsl	r2, r2, #9
	movle	r1, #85
	movgt	r1, #170
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
.L3175:
	ldr	r3, .L3190+16
	ldrh	r2, [r3]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L3178
	mov	r7, #0
	b	.L3179
.L3177:
	uxth	r3, r7
	add	r2, sp, #48
	add	r3, r2, r3, lsl #1
	ldrh	r1, [r3, #-16]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3176
	ldr	r3, .L3190+20
	ldrh	ip, [r3]
	uxth	r3, r8
	mla	ip, r1, ip, r3
	ldr	r3, [r4, #4]
	str	ip, [r3]
	ldr	r3, [r4, #4]
	str	fp, [r3, #4]
	mov	r3, #0
	ldr	r2, [r4, #12]
	str	r3, [r2]
	mvn	r2, #0
	ldrb	r1, [r6, #1153]	@ zero_extendqisi2
	str	r3, [sp, #4]
	ldrb	r3, [r10, #-2546]	@ zero_extendqisi2
	rsb	lr, r1, #24
	mvn	r1, r2, lsl r1
	str	r3, [sp]
	and	r0, r1, ip, lsr lr
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #4]
	bic	r1, ip, r1, lsl lr
	uxtb	r0, r0
	bl	flash_prog_page_en
.L3176:
	add	r7, r7, #1
.L3179:
	ldrb	r2, [sp, #25]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L3177
	add	r8, r8, #1
	b	.L3175
.L3178:
	mov	r10, #0
.L3180:
	ldr	r3, .L3190+24
	uxth	r2, r10
	ldrh	r1, [r3, #-8]
	cmp	r1, r2
	bls	.L3185
	mov	fp, #0
	str	r2, [sp, #12]
	b	.L3186
.L3184:
	uxth	r7, fp
	add	r3, sp, #48
	add	r7, r7, #8
	add	r7, r3, r7, lsl #1
	movw	r3, #65535
	ldrh	r1, [r7, #-32]
	cmp	r1, r3
	beq	.L3182
	ldr	r0, .L3190+20
	ldr	r3, [sp, #12]
	ldrh	r8, [r0]
	mov	r0, r4
	mla	r8, r1, r8, r3
	mov	r1, #1
	str	r8, [r4, #24]
	bl	sblk_read_page
	ldr	r1, [r4, #4]
	ldr	r1, [r1]
	cmp	r8, r1
	beq	.L3182
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r1, #0
	strh	r1, [r7, #-32]	@ movhi
.L3182:
	add	fp, fp, #1
.L3186:
	ldrb	r0, [sp, #25]	@ zero_extendqisi2
	uxth	r1, fp
	cmp	r0, r1
	bhi	.L3184
	add	r10, r10, #1
	b	.L3180
.L3185:
	mov	r0, r4
	bl	zbuf_free
	ldr	r2, [r6, #1084]
	mov	r1, r5
	ldr	r0, .L3190+28
	ldr	r3, [r2, r5, lsl #2]
	ldrh	r2, [r2, r9]
	ubfx	r3, r3, #11, #8
	ubfx	r2, r2, #0, #11
	bl	printk
.L3171:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3191:
	.align	2
.L3190:
	.word	.LANCHOR0
	.word	.LC200
	.word	.LANCHOR3
	.word	1437269760
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3074
	.word	.LANCHOR3-3088
	.word	.LC201
	.fnend
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_alloc_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	uxth	r5, r0
	cmp	r0, #5
	mov	r9, r0
	mov	r1, r5
	mov	r0, #0
	movne	r8, #0
	moveq	r8, #2
	bl	zftl_get_free_sblk
	movw	r3, #65535
	mov	r4, r0
	cmp	r0, r3
	beq	.L3194
	ldr	r6, .L3212
	lsl	r7, r0, #2
	ldr	r5, [r6, #1084]
	add	r5, r5, r7
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L3195
	mov	r2, #1012
	ldr	r1, .L3212+4
	ldr	r0, .L3212+8
	bl	printk
	bl	dump_stack
.L3195:
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	bfi	r3, r9, #5, #3
	uxtb	r3, r3
	ubfx	r2, r3, #3, #2
	orr	r2, r8, r2
	bfi	r3, r2, #3, #2
	clz	r2, r8
	strb	r3, [r5, #2]
	uxtb	r3, r3
	lsr	r2, r2, #5
	and	r1, r3, #24
	cmp	r1, #24
	cmpeq	r8, #0
	moveq	r1, #1
	bfieq	r3, r1, #3, #2
	strbeq	r3, [r5, #2]
	ldr	r3, [r6, #2800]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	beq	.L3197
	ldr	r3, [r6, #1084]
	ldrh	r3, [r3, r7]
	ubfx	r3, r3, #0, #11
	cmp	r3, #0
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L3197
	mov	r0, r4
	bl	ftl_ink_check_sblk
.L3197:
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3194:
	bl	print_ftl_debug_info
	mov	r2, r9
	mov	r1, r4
	ldr	r0, .L3212+12
	bl	printk
	mov	r1, r5
	mov	r0, #0
	bl	zftl_get_free_sblk
	mov	r2, r9
	mov	r1, r0
	mov	r4, r0
	ldr	r0, .L3212+12
	bl	printk
	bl	dump_all_list_info
	movw	r2, #1031
	ldr	r1, .L3212+4
	ldr	r0, .L3212+8
	bl	printk
	bl	dump_stack
	b	.L3197
.L3213:
	.align	2
.L3212:
	.word	.LANCHOR0
	.word	.LANCHOR1+2201
	.word	.LC0
	.word	.LC202
	.fnend
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	ftl_open_sblk_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r5, .L3222
	mov	r6, r1
	mov	r7, #0
	sub	r8, r5, #3088
.L3215:
	movw	r10, #65535
.L3216:
	mov	r0, r6
	bl	ftl_alloc_sblk
	cmp	r0, r10
	mov	r9, r0
	beq	.L3216
	mov	r1, #0
	ldr	fp, .L3222+4
	bl	ftl_erase_sblk
	add	r1, r4, #16
	mov	r0, r9
	bl	ftl_get_blk_list_in_sblk
	strh	r9, [r4]	@ movhi
	cmp	r6, #2
	ldrh	r2, [r8, #-8]
	uxtb	r0, r0
	ldrbne	r3, [r5, #-3127]	@ zero_extendqisi2
	moveq	r3, #0
	ldrb	r1, [r5, #-3127]	@ zero_extendqisi2
	lsl	r10, r9, #1
	strb	r0, [r4, #9]
	smulbb	r0, r0, r2
	strh	r7, [r4, #2]	@ movhi
	smulbbne	r3, r3, r2
	strb	r7, [r4, #5]
	mul	r2, r2, r1
	strh	r7, [r4, #10]	@ movhi
	strh	r0, [r4, #6]	@ movhi
	mov	r1, #255
	ldr	r0, [r5, #-2556]
	uxthne	r3, r3
	strb	r6, [r4, #4]
	strh	r3, [r4, #12]	@ movhi
	lsl	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memset
	ldr	r3, [fp, #1092]
	ldrh	r2, [r4, #6]
	strh	r2, [r3, r10]	@ movhi
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, #0
	popne	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	mov	r1, r9
	ldr	r0, .L3222+8
	bl	printk
	ldr	r3, [fp, #1092]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	mov	r3, #7
	strb	r3, [r4, #4]
	b	.L3215
.L3223:
	.align	2
.L3222:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC203
	.fnend
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	pm_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	movw	r1, #690
	ldr	r4, .L3242
	ldr	r7, .L3242+4
	ldr	r2, [r4, #2800]
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrb	r1, [r7, #-3127]	@ zero_extendqisi2
	cmp	r1, r3
	bls	.L3225
	add	r3, r3, #336
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3226
.L3225:
	ldr	r5, .L3242+8
	movw	r8, #65535
.L3227:
	mov	r0, #1
	bl	ftl_alloc_sblk
	cmp	r0, r8
	mov	r6, r0
	beq	.L3227
	mov	r1, #0
	bl	ftl_erase_sblk
	ldr	r1, [r4, #2800]
	mov	r0, r6
	add	r1, r1, #672
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L3228
	mov	r1, r6
	mov	r0, r5
	bl	printk
	ldr	r3, [r4, #1084]
	add	r6, r3, r6, lsl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	b	.L3227
.L3228:
	ldr	r2, [r4, #2800]
	movw	r3, #690
	mov	r5, #0
	movw	r1, #65535
	strh	r5, [r2, r3]	@ movhi
	add	r2, r2, #416
	mov	r3, #1
	str	r3, [r7, #-36]
.L3230:
	ldrh	r0, [r2], #2
	uxth	r3, r5
	cmp	r0, r1
	beq	.L3229
	add	r5, r5, #1
	cmp	r5, #128
	bne	.L3230
	mov	r2, #264
	ldr	r1, .L3242+12
	ldr	r0, .L3242+16
	bl	printk
	bl	dump_stack
	mov	r3, r5
.L3229:
	ldr	r2, [r4, #2800]
	add	r3, r3, #208
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	add	r3, r2, #688
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L3226:
	ldr	r2, [r4, #2800]
	movw	r3, #690
	ldrh	r3, [r2, r3]
	add	r3, r3, #336
	lsl	r3, r3, #1
	ldrh	r5, [r2, r3]
	movw	r2, #65533
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L3232
	movw	r2, #270
	ldr	r1, .L3242+12
	ldr	r0, .L3242+16
	bl	printk
	bl	dump_stack
.L3232:
	ldr	r1, [r4, #2800]
	mov	r2, #0
	movw	r0, #694
	add	r3, r1, #696
	strh	r2, [r3]	@ movhi
	ldr	r3, .L3242+20
	ldrb	r2, [r4, #1153]	@ zero_extendqisi2
	ldrh	r3, [r3, #-2]
	rsb	r2, r2, #24
	sub	r2, r2, r3
	asr	r3, r5, r2
	strh	r3, [r1, r0]	@ movhi
	add	r1, r1, #692
	strh	r5, [r1]	@ movhi
	ldr	r1, .L3242+24
	ldr	r1, [r1]
	tst	r1, #4096
	beq	.L3236
	uxth	r3, r3
	mvn	r1, #0
	mvn	r2, r1, lsl r2
	ldr	r0, .L3242+28
	str	r3, [sp]
	mov	r1, r5
	mov	r3, r5
	bl	printk
.L3236:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L3243:
	.align	2
.L3242:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC203
	.word	.LANCHOR1+2216
	.word	.LC0
	.word	.LANCHOR3-3136
	.word	.LANCHOR2
	.word	.LC204
	.fnend
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_write_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_write_page, %function
pm_write_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r5, r0
	ldr	r9, .L3258
	mov	r8, r1
	mov	r7, r9
.L3245:
	ldr	r3, [r9, #2800]
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	add	r2, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3258+4
	ldrh	r2, [r2]
	cmp	r1, r2
	bcs	.L3246
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3247
.L3246:
	bl	pm_alloc_new_blk
	mov	r0, #0
	bl	ftl_info_flush
.L3247:
	ldr	r3, [r7, #2800]
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3248
	movw	r2, #303
	ldr	r1, .L3258+8
	ldr	r0, .L3258+12
	bl	printk
	bl	dump_stack
.L3248:
	ldr	r3, [r7, #2800]
	mov	r1, #0
	ldr	r4, .L3258+16
	add	r2, r3, #692
	add	r3, r3, #696
	ldrh	r6, [r2]
	sub	r2, r4, #3072
	ldrh	r2, [r2, #-2]
	ldrh	r3, [r3]
	ldr	r0, [r4, #-32]
	mla	r6, r2, r6, r3
	mov	r2, #64
	bl	ftl_memset
	ldr	r3, [r4, #-32]
	mov	r0, r8
	str	r5, [r3]
	ldr	r3, [r7, #2800]
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	ldr	r10, [r4, #-32]
	ldr	r3, [r3, #48]
	lsl	r1, r1, #9
	str	r3, [r10, #4]
	bl	js_hash
	ldr	r3, [r7, #2800]
	mov	r2, r8
	str	r0, [r10, #8]
	mov	r1, r6
	ldrb	r0, [r3, #694]	@ zero_extendqisi2
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r4, #-32]
	bl	ftl_prog_page
	ldr	r2, [r7, #2800]
	add	r1, r2, #696
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	strh	r3, [r1]	@ movhi
	beq	.L3249
	ldrb	r3, [r4, #-28]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3250
.L3249:
	mov	r3, #0
	strb	r3, [r4, #-28]
	b	.L3245
.L3250:
	cmn	r0, #1
	bne	.L3252
	mov	r1, r6
	ldr	r0, .L3258+20
	bl	printk
	b	.L3245
.L3252:
	movw	r3, #698
	mov	r0, #0
	ldrh	r3, [r2, r3]
	cmp	r5, r3
	addcc	r5, r5, #176
	strcc	r6, [r2, r5, lsl #2]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3259:
	.align	2
.L3258:
	.word	.LANCHOR0
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2233
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC205
	.fnend
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	flash_info_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_info_flush, %function
flash_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3273
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3261
	ldr	r2, .L3273+4
	movw	r1, #365
	ldr	r0, .L3273+8
	bl	printk
.L3261:
	ldr	r4, .L3273+12
	mov	r2, #64
	ldr	r5, .L3273+16
	mov	r1, #0
	ldr	r9, .L3273+20
	mov	r7, #0
	ldr	r0, [r4, #-24]
	mov	r10, r7
	bl	ftl_memset
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [r5, #1040]
	ldr	r0, .L3273+24
	bl	rknand_print_hex
	ldr	r8, .L3273+28
	ldr	r6, [r5, #1040]
	ldr	r1, [r6, #8]
	add	r0, r6, #16
	bl	js_hash
	str	r0, [r6, #12]
.L3262:
	ldrb	r6, [r4, #-20]	@ zero_extendqisi2
	mov	r0, r9
	ldrh	fp, [r4, #-18]
	ldrh	r3, [r5, #2]
	mov	r1, r6
	mov	r2, fp
	str	r3, [sp, #12]
	bl	printk
	ldrh	r2, [r4, #-224]
	ldrh	r0, [r4, #-18]
	ldr	r3, [sp, #12]
	sub	r2, r2, #1
	cmp	r0, r2
	blt	.L3263
	ldr	r6, [r5, #1040]
	ldrb	r2, [r4, #-19]	@ zero_extendqisi2
	strh	r10, [r4, #-18]	@ movhi
	ldr	r3, [r6, #4]
	mov	r0, r6
	add	r3, r3, #1
	str	r3, [r6, #4]
	ldrb	r3, [r4, #-20]	@ zero_extendqisi2
	strb	r2, [r4, #-20]
	strb	r3, [r4, #-19]
	ldrh	r3, [r6, #16]
	add	r3, r3, #1
	strh	r3, [r0, #16]!	@ movhi
	ldr	r1, [r6, #8]
	bl	js_hash
	ldrb	r3, [r4, #-20]	@ zero_extendqisi2
	str	r0, [r6, #12]
	mov	r0, #0
	ldrh	r6, [r5, #2]
	mul	r6, r6, r3
	mov	r1, r6
.L3272:
	bl	flash_erase_block
.L3264:
	ldr	r2, [r5, #1040]
	mov	fp, #1
	ldr	r3, [r4, #-24]
	mov	r1, r6
	mov	r0, #0
	ldr	r2, [r2, #4]
	str	r2, [r3]
	mov	r2, #4
	ldr	r3, [r4, #-24]
	str	r8, [r3, #4]
	stm	sp, {r2, fp}
	ldr	r2, [r5, #1040]
	bl	flash_prog_page_en
	ldrh	r3, [r4, #-18]
	cmn	r0, #1
	add	r3, r3, fp
	strh	r3, [r4, #-18]	@ movhi
	bne	.L3265
	mov	r1, r6
	ldr	r0, .L3273+32
	bl	printk
	b	.L3262
.L3267:
	mov	r7, fp
	b	.L3262
.L3263:
	cmp	r0, #0
	mla	r6, r6, r3, fp
	bne	.L3264
	mov	r1, r6
	b	.L3272
.L3265:
	cmp	r7, #0
	beq	.L3267
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3274:
	.align	2
.L3273:
	.word	.LANCHOR2
	.word	.LANCHOR1+2247
	.word	.LC135
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC207
	.word	.LC206
	.word	1398362953
	.word	.LC208
	.fnend
	.size	flash_info_flush, .-flash_info_flush
	.align	2
	.global	flash_info_blk_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3307
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3307+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3276
	ldr	r3, [r4, #1040]
	mov	r1, #50
	ldr	r2, .L3307+8
	ldr	r0, .L3307+12
	str	r3, [sp]
	mov	r3, #2048
	bl	printk
.L3276:
	ldr	r5, .L3307+16
	mov	r7, #4
	ldr	r8, .L3307+20
.L3280:
	mov	r6, #0
.L3279:
	ldrh	r1, [r4, #2]
	mov	r9, #4
	str	r9, [sp]
	mov	r0, #0
	ldr	r3, [r5, #-24]
	ldr	r2, [r4, #1040]
	mla	r1, r7, r1, r6
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3277
	ldr	r2, [r4, #1040]
	ldr	r3, [r2]
	cmp	r3, r8
	beq	.L3278
.L3277:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L3279
	add	r7, r7, #1
	cmp	r7, #16
	bne	.L3280
.L3306:
	mvn	r0, #0
.L3275:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3287:
	str	r9, [sp]
	sub	r1, r10, r7
	ldr	r3, [r5, #-24]
	mov	r0, #0
	ldr	r2, [r4, #1040]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3285
	ldr	r3, [r4, #1040]
	ldr	r3, [r3]
	cmp	r3, r8
	beq	.L3286
.L3285:
	add	r7, r7, #1
	b	.L3284
.L3286:
	cmp	r7, #1
	bls	.L3290
	bl	flash_info_flush
.L3290:
	mov	r0, #0
	b	.L3275
.L3278:
	ldrb	r1, [r2, #36]	@ zero_extendqisi2
	ldrh	r0, [r4, #2]
	ldrb	r3, [r2, #37]	@ zero_extendqisi2
	strb	r1, [r5, #-20]
	str	r9, [sp]
	strb	r3, [r5, #-19]
	mul	r1, r1, r0
	ldr	r3, [r5, #-24]
	mov	r0, #0
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3289
	ldr	r3, [r4, #1040]
	ldr	r2, [r3]
	cmp	r2, r8
	ldreq	r6, [r3, #4]
	beq	.L3282
.L3289:
	mov	r6, #0
.L3282:
	ldrh	r0, [r4, #2]
	mov	r3, #4
	ldrb	r1, [r5, #-19]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r2, [r4, #1040]
	ldr	r3, [r5, #-24]
	mul	r1, r0, r1
	mov	r0, #0
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3283
	ldr	r3, [r4, #1040]
	ldr	r2, [r3]
	cmp	r2, r8
	bne	.L3283
	ldr	r2, [r3, #4]
	cmp	r6, r2
	ldrbcc	r2, [r3, #37]	@ zero_extendqisi2
	ldrbcc	r3, [r3, #36]	@ zero_extendqisi2
	strbcc	r2, [r5, #-20]
	strbcc	r3, [r5, #-19]
.L3283:
	mov	r9, #4
	ldr	r3, [r5, #-24]
	ldrb	r1, [r5, #-20]	@ zero_extendqisi2
	mov	r0, #0
	str	r9, [sp]
	mov	r7, #0
	ldr	r2, [r4, #1040]
	bl	flash_get_last_written_page
	uxth	fp, r0
	ldrb	r10, [r5, #-20]	@ zero_extendqisi2
	add	r3, fp, #1
	strh	r3, [r5, #-18]	@ movhi
	ldrh	r3, [r4, #2]
	mla	r10, r3, r10, r0
.L3284:
	sub	r0, fp, r7
	sxth	r6, r0
	cmp	r6, #0
	bge	.L3287
	cmn	r6, #1
	bne	.L3286
	ldr	r3, [r4, #1040]
	ldr	r0, .L3307+24
	ldr	r1, [r3]
	bl	printk
	b	.L3306
.L3308:
	.align	2
.L3307:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1+2264
	.word	.LC209
	.word	.LANCHOR3
	.word	1398362953
	.word	.LC210
	.fnend
	.size	flash_info_blk_init, .-flash_info_blk_init
	.align	2
	.global	nand_flash_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	nand_flash_init, %function
nand_flash_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L3395
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3310
	ldr	r2, .L3395+4
	movw	r1, #3450
	ldr	r0, .L3395+8
	bl	printk
.L3310:
	ldr	r5, .L3395+12
	mov	r7, #0
	mov	r0, r4
	ldr	r4, .L3395+16
	ldr	r9, .L3395+20
	mov	fp, #44
	str	r7, [r5, #-104]
	bl	nandc_init
	ldr	r3, .L3395+24
	mov	r2, #8
	mov	r1, r7
	ldr	r0, .L3395+28
	mov	r10, r9
	str	r3, [r4, #1104]
	mov	r3, #1
	strb	r3, [r4, #1109]
	mov	r3, #3
	strb	r3, [r4, #1153]
	bl	ftl_memset
	mov	r2, #32
	mov	r1, r7
	sub	r0, r5, #220
	bl	ftl_memset
.L3316:
	lsl	r8, r7, #3
	uxtb	r0, r7
	add	r1, r9, r8
	bl	flash_read_id
	cmp	r7, #0
	bne	.L3311
	ldrb	r3, [r4, #1160]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bls	.L3312
.L3314:
	mvn	r7, #1
.L3309:
	mov	r0, r7
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3312:
	ldrb	r3, [r4, #1161]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3314
.L3311:
	ldrb	r3, [r8, r10]	@ zero_extendqisi2
	add	r7, r7, #1
	cmp	r3, #181
	strbeq	fp, [r8, r10]
	cmp	r7, #4
	bne	.L3316
	ldr	r9, .L3395+32
	mov	r7, #0
	ldr	r10, .L3395+20
.L3319:
	lsl	r8, r7, #5
	ldrb	r2, [r9, r7, lsl #5]	@ zero_extendqisi2
	mov	r1, r10
	add	r0, r8, #1
	add	r0, r9, r0
	bl	flash_mem_cmp8
	cmp	r0, #0
	bne	.L3317
	add	r9, r9, r8
	ldr	r3, .L3395+36
	add	r8, r6, r8
	ldrb	r2, [r8, #440]	@ zero_extendqisi2
	mov	r1, r3
.L3318:
	ldrb	ip, [r3, r0, lsl #5]	@ zero_extendqisi2
	cmp	ip, r2
	beq	.L3320
	add	r0, r0, #1
	cmp	r0, #4
	bne	.L3318
.L3320:
	add	r1, r1, r0, lsl #5
	mov	r2, #32
	ldr	r0, .L3395+40
	bl	ftl_memcpy
	mov	r2, #32
	mov	r1, r9
	ldr	r0, .L3395+24
	bl	ftl_memcpy
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #8
	bhi	.L3321
	ldrb	r2, [r6, #24]	@ zero_extendqisi2
	cmp	r2, #60
	movhi	r2, #60
	strbhi	r2, [r6, #24]
	cmp	r3, #6
	beq	.L3314
.L3321:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3323
	ldr	r2, .L3395+4
	movw	r1, #3480
	ldr	r0, .L3395+8
	bl	printk
.L3323:
	ldr	r3, [r4, #1104]
	mov	r0, #16384
	mov	r7, #0
	ldrh	r3, [r3, #10]
	cmp	r3, #1024
	movcs	r3, #2
	strbcs	r3, [r4, #1153]
	bl	ftl_malloc
	str	r0, [r5, #-92]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-120]
	mov	r0, #2048
	bl	ftl_dma32_malloc
	str	r0, [r4, #1040]
	mov	r0, #64
	bl	ftl_dma32_malloc
	str	r0, [r5, #-96]
	mov	r0, #64
	bl	ftl_dma32_malloc
	str	r0, [r5, #-112]
	mov	r0, #64
	bl	ftl_dma32_malloc
	strb	r7, [r5, #-16]
	str	r0, [r5, #-24]
	bl	flash_die_info_init
	ldrb	r0, [r6, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrb	r0, [r6, #24]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r1, [r4, #1104]
	str	r7, [r5, #-100]
	ldrh	r3, [r1, #16]
	ubfx	r2, r3, #8, #3
	strb	r2, [r4, #1192]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #-80]
	ubfx	r2, r3, #4, #1
	strb	r2, [r4, #1194]
	ubfx	r2, r3, #12, #1
	strb	r2, [r5, #-3120]
	ubfx	r2, r3, #13, #1
	strb	r2, [r5, #-3126]
	ubfx	r2, r3, #11, #1
	strb	r2, [r4, #1158]
	ldrb	r2, [r1, #31]	@ zero_extendqisi2
	ldrb	r1, [r1, #28]	@ zero_extendqisi2
	ubfx	r0, r2, #1, #1
	strb	r0, [r5, #-2542]
	ubfx	r0, r2, #2, #1
	strb	r0, [r5, #-15]
	ubfx	r0, r3, #14, #1
	lsr	r3, r3, #15
	strb	r0, [r4, #1]
	strb	r1, [r4]
	strb	r3, [r5, #-3125]
	ubfx	r3, r2, #3, #1
	strb	r3, [r4, #1152]
	mov	r3, #60
	strb	r3, [r4, #1108]
	ubfx	r2, r2, #4, #1
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	strb	r2, [r4, #1159]
	strb	r1, [r4, #1110]
	cmp	r3, #9
	moveq	r2, #70
	strbeq	r2, [r4, #1108]
	ldrb	r2, [r6, #35]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L3326
	ldrb	r2, [r6, #33]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, #2
	moveq	r2, #3
	strb	r2, [r4, #1110]
.L3326:
	cmp	r3, #8
	bne	.L3328
	ldrb	r3, [r4, #1160]	@ zero_extendqisi2
	cmp	r3, #137
	cmpne	r3, #44
	bne	.L3328
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #3
	moveq	r3, #0
	strbeq	r3, [r4, #1110]
.L3328:
	ldrh	r2, [r6, #20]
	ldrb	r3, [r6, #23]	@ zero_extendqisi2
	tst	r2, #64
	strb	r3, [r4, #1100]
	beq	.L3330
	sub	r2, r3, #17
	cmp	r3, #21
	cmpne	r2, #2
	bhi	.L3331
	cmp	r3, #21
	ldr	r2, .L3395+44
	movne	r3, #15
	moveq	r3, #4
	str	r2, [r5, #-100]
.L3391:
	strb	r3, [r5, #-108]
.L3330:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3341
	ldr	r2, .L3395+4
	movw	r1, #3573
	ldr	r0, .L3395+8
	bl	printk
.L3341:
	mov	r3, #0
	strb	r3, [r4, #1143]
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	tst	r3, #1
	moveq	r0, #4
	beq	.L3393
	ldrb	r3, [r4, #1160]	@ zero_extendqisi2
	cmp	r3, #155
	beq	.L3343
	mov	r0, #4
	bl	flash_set_interface_mode
	mov	r0, #4
	bl	nandc_set_if_mode
.L3343:
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
.L3393:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	r0, #1
	mov	r7, r0
	bne	.L3345
	ldr	r3, [r4, #1040]
	mov	r2, #17
	mov	r0, #0
	strb	r0, [r4, #1154]
	strb	r2, [r3, #32]
	bl	zftl_flash_exit_slc_mode
	b	.L3309
.L3317:
	add	r7, r7, #1
	cmp	r7, #49
	bne	.L3319
	b	.L3314
.L3331:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	bhi	.L3334
	ldr	r3, .L3395+48
	str	r3, [r5, #-100]
	mov	r3, #4
	strb	r3, [r4, #1101]
.L3394:
	mov	r3, #7
	b	.L3391
.L3334:
	sub	r2, r3, #67
	sub	r1, r3, #34
	uxtb	r2, r2
	cmp	r2, #1
	cmphi	r1, #1
	movls	r1, #1
	movhi	r1, #0
	bhi	.L3335
	cmp	r3, #68
	cmpne	r3, #35
	ldr	r1, .L3395+48
	movne	r3, #7
	moveq	r3, #17
	cmp	r2, #1
	strb	r3, [r5, #-108]
	movls	r3, #4
	movhi	r3, #5
	str	r1, [r5, #-100]
	strb	r3, [r4, #1101]
	b	.L3330
.L3335:
	sub	r2, r3, #36
	cmp	r2, #1
	ldrls	r3, .L3395+52
	strls	r3, [r5, #-100]
	bls	.L3394
.L3339:
	cmp	r3, #50
	ldreq	r3, .L3395+56
	streq	r3, [r5, #-100]
	moveq	r3, #25
	beq	.L3391
.L3340:
	cmp	r3, #81
	ldreq	r3, .L3395+60
	strbeq	r1, [r4, #1195]
	streq	r3, [r5, #-100]
	moveq	r3, #7
	strbeq	r3, [r5, #-108]
	b	.L3330
.L3345:
	ldrb	r3, [r6, #11]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L3346
	ldr	r3, [r4, #1040]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	strbne	r3, [r4, #1110]
.L3346:
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	ldrls	r3, .L3395+64
	strls	r3, [r5, #-100]
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	tst	r3, #4
	beq	.L3349
	ldr	r3, [r4, #1040]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3349
	ldrb	r3, [r5, #-20]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r1, [r4, #2]
	mul	r1, r1, r3
	bl	flash_ddr_para_scan
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3349
	ldr	r2, [r4, #1040]
	strb	r3, [r2, #19]
	bl	flash_info_flush
.L3349:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3351
	ldr	r2, .L3395+4
	movw	r1, #3676
	ldr	r0, .L3395+8
	bl	printk
.L3351:
	bl	nand_flash_print_info
	mov	r7, #0
	b	.L3309
.L3396:
	.align	2
.L3395:
	.word	.LANCHOR2
	.word	.LANCHOR1+2284
	.word	.LC135
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR0+1160
	.word	.LANCHOR2+4
	.word	.LANCHOR0+1144
	.word	.LANCHOR2+418
	.word	.LANCHOR2+1986
	.word	.LANCHOR0+1111
	.word	micron_read_retrial
	.word	toshiba_read_retrial
	.word	toshiba_3d_read_retrial
	.word	samsung_read_retrial
	.word	ymtc_3d_read_retrial
	.word	hynix_read_retrial
	.fnend
	.size	nand_flash_init, .-nand_flash_init
	.align	2
	.global	ftl_sysblk_dump
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sysblk_dump, %function
ftl_sysblk_dump:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	.pad #52
	sub	sp, sp, #52
	mov	r0, #1
	mov	r5, #0
	ldr	r10, .L3408
	mov	r6, r5
	bl	buf_alloc
	ldr	r3, [r0, #4]
	mov	r4, r0
	str	r3, [sp, #44]
.L3398:
	ldr	r3, .L3408+4
	ldrh	r2, [r3, #-8]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L3400
	ldr	r1, [sp, #44]
	mov	r3, #32
	mov	r2, #4
	ldr	r0, .L3408+8
	add	r1, r1, #704
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	cmp	r6, #0
	beq	.L3401
	movw	r2, #1619
	ldr	r1, .L3408+12
	ldr	r0, .L3408+16
	bl	printk
	bl	dump_stack
.L3401:
	mov	r0, r6
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3400:
	ldr	r3, .L3408+20
	uxth	fp, r5
	ldr	r2, [r4, #12]
	add	r5, r5, #1
	ldr	r1, [r4, #4]
	ldrh	r7, [r3]
	ldrb	r3, [r10, #-2546]	@ zero_extendqisi2
	mla	r7, r9, r7, fp
	mov	r0, r7
	bl	ftl_read_ppa_page
	ldr	r2, [r4, #12]
	mov	r8, r0
	ldr	r3, [r4, #4]
	ldr	r1, [r2, #12]
	str	r1, [sp, #32]
	ldr	r1, [r2, #8]
	str	r1, [sp, #28]
	ldr	r1, [r2, #4]
	str	r1, [sp, #24]
	mov	r1, r9
	ldr	r2, [r2]
	str	r2, [sp, #20]
	ldr	r2, [r3, #12]
	str	r2, [sp, #16]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	mov	r2, fp
	ldr	r3, [r3]
	str	r0, [sp]
	ldr	r0, .L3408+24
	str	r3, [sp, #4]
	mov	r3, r7
	bl	printk
	cmn	r8, #1
	cmpne	r8, #512
	moveq	r6, #1
	b	.L3398
.L3409:
	.align	2
.L3408:
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC211
	.word	.LANCHOR1+2300
	.word	.LC0
	.word	.LANCHOR3-3074
	.word	.LC195
	.fnend
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	ftl_open_sblk_recovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 216
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #228
	sub	sp, sp, #228
	ldr	r5, .L3532
	mov	r4, r0
	str	r1, [sp, #28]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3411
	ldrh	r1, [r0, #2]
	ldr	r0, .L3532+4
	bl	printk
.L3411:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3412
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	ldr	r0, .L3532+8
	bl	printk
.L3412:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3413
	ldrh	r1, [r4]
	ldr	r0, .L3532+12
	bl	printk
.L3413:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3414
	ldrh	r2, [r4, #18]
	ldrh	r1, [r4, #16]
	ldr	r0, .L3532+16
	bl	printk
.L3414:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3415
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldr	r0, .L3532+20
	bl	printk
.L3415:
	ldrh	r3, [r4, #10]
	ldr	r1, .L3532+24
	ldrh	r2, [r4]
	strh	r3, [r4, #14]	@ movhi
	movw	r3, #1080
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcs	.L3410
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, .L3532+28
	mov	r1, #255
	ldr	r2, [r0, #4]
	mov	r5, r0
	add	r0, sp, #32
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	sub	r3, r3, #2
	add	r3, r2, r3, lsl #9
	mov	r2, #64
	str	r3, [sp, #16]
	bl	ftl_memset
	mov	r2, #64
	mov	r1, #255
	add	r0, sp, #96
	bl	ftl_memset
	mov	r2, #64
	mov	r1, #255
	add	r0, sp, #160
	bl	ftl_memset
	ldrb	r10, [r4, #5]	@ zero_extendqisi2
	mov	r3, #2
	ldrh	r9, [r4, #2]
	str	r3, [sp, #12]
	mov	r3, #0
	str	r3, [sp, #24]
.L3417:
	ldr	r3, .L3532+32
	ldrh	r3, [r3]
	cmp	r3, r9
	bhi	.L3434
.L3420:
	ldrh	r3, [r4, #10]
	ldr	r1, .L3532+36
	ldrh	r2, [r4, #6]
	ldrb	r0, [r4, #9]	@ zero_extendqisi2
	strh	r9, [r4, #2]	@ movhi
	add	r2, r2, r3
	ldrh	r3, [r1, #-8]
	strb	r10, [r4, #5]
	str	r1, [sp, #16]
	mul	r3, r3, r0
	cmp	r2, r3
	beq	.L3435
	movw	r2, #1802
	ldr	r1, .L3532+40
	ldr	r0, .L3532+44
	bl	printk
	bl	dump_stack
.L3435:
	ldr	r6, .L3532+28
	mov	r7, #0
	ldrh	r0, [r4, #10]
	mov	r2, r7
	ldr	r3, [r6, #-2556]
.L3436:
	cmp	r2, r0
	bcc	.L3438
	ldr	r2, [sp, #16]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldr	r9, .L3532
	ldrh	r2, [r2, #-8]
	ldr	r10, .L3532+24
	smulbb	r3, r3, r2
	sub	r3, r3, r0
	add	r7, r7, r3
	ldr	r3, [r9]
	uxth	r7, r7
	tst	r3, #4096
	beq	.L3439
	ldrh	r1, [r4]
	ldr	r2, [r10, #1092]
	ldr	r0, .L3532+48
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	mov	r2, r7
	bl	printk
.L3439:
	ldrh	r3, [r4]
	ldr	r2, [r10, #1092]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldr	r3, [r9]
	tst	r3, #16384
	beq	.L3440
	ldr	r3, [sp, #44]
	add	r1, sp, #32
	ldr	r0, .L3532+52
	str	r3, [sp]
	ldm	r1, {r1, r2, r3}
	bl	printk
.L3440:
	ldrb	r2, [r6, #-2546]	@ zero_extendqisi2
	mov	r8, #0
	mov	r1, #0
	ldr	r0, [r5, #4]
	lsl	r2, r2, #9
	bl	ftl_memset
.L3441:
	ldrb	r3, [r6, #-3127]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r2, r3
	cmp	r8, r3
	bcc	.L3453
	ldr	fp, .L3532+56
	mov	r7, #0
	add	r8, sp, #32
.L3454:
	ldrb	r3, [r6, #-3127]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r2, r3
	cmp	r7, r3
	bcc	.L3460
	mov	r0, r5
	bl	zbuf_free
	ldr	r3, [sp, #16]
	ldrh	r2, [r4, #12]
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldrh	r3, [r3, #-8]
	mla	r3, r1, r3, r2
	ldr	r2, [r6, #-2556]
	sub	r3, r3, #-1073741823
	ldr	r3, [r2, r3, lsl #2]
	cmn	r3, #1
	beq	.L3461
	movw	r2, #1917
	ldr	r1, .L3532+40
	ldr	r0, .L3532+44
	bl	printk
	bl	dump_stack
.L3461:
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L3410
	mov	r0, r4
	bl	ftl_write_last_log_page
.L3410:
	add	sp, sp, #228
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3434:
	ldrb	r10, [r4, #5]	@ zero_extendqisi2
	ldr	r6, .L3532+28
.L3418:
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, r10
	movls	r3, #0
	strbls	r3, [r4, #5]
	addls	r3, r9, #1
	uxthls	r9, r3
	bls	.L3417
.L3433:
	add	r3, r4, r10, lsl #1
	ldrh	r8, [r3, #16]
	movw	r3, #65535
	cmp	r8, r3
	beq	.L3419
	ldr	r3, .L3532+60
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	ldrh	r3, [r3, #-2]
	mla	r3, r8, r3, r9
	str	r3, [sp, #20]
	ldr	r0, [sp, #20]
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmp	r0, #512
	mov	r7, r0
	beq	.L3420
	cmn	r0, #1
	beq	.L3421
	ldr	r3, [r5, #12]
	ldr	r2, [r3]
	cmn	r2, #1
	bne	.L3421
	ldr	r3, [r3, #4]
	cmn	r3, #1
	bne	.L3421
	ldr	r3, [r5, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3420
.L3421:
	mov	r3, #1
	ldrh	r2, [r4, #10]
	strb	r3, [r6, #-47]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mla	r3, r9, r3, r10
	cmp	r2, r3
	beq	.L3422
	movw	r2, #1694
	ldr	r1, .L3532+40
	ldr	r0, .L3532+44
	bl	printk
	bl	dump_stack
.L3422:
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #6]
	add	r2, r2, r3
	ldr	r3, .L3532+32
	ldrh	r1, [r3]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L3423
	movw	r2, #1695
	ldr	r1, .L3532+40
	ldr	r0, .L3532+44
	bl	printk
	bl	dump_stack
.L3423:
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L3424
	ldr	r3, [r5, #12]
	ldr	r1, .L3532+64
	ldr	r2, [r3]
	cmp	r2, r1
	beq	.L3424
	cmn	r7, #1
	beq	.L3426
	ldr	r2, [r3, #4]
	cmn	r2, #1
	bne	.L3427
.L3430:
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #4]
	cmn	r0, #1
	bne	.L3428
.L3426:
	ldrh	r3, [r4, #6]
	sub	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	mov	r3, #4
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #24]
.L3419:
	add	r3, r10, #1
	uxth	r10, r3
	b	.L3418
.L3427:
	ldr	r2, [r3, #16]
	ldr	r1, .L3532+68
	cmp	r2, r1
	bne	.L3430
	ldr	fp, [r3, #20]
	mov	r1, #1024
	ldr	r0, [sp, #16]
	bl	js_hash
	cmp	fp, r0
	beq	.L3430
	mov	r1, #1024
	ldr	r0, [sp, #16]
	bl	js_hash
	mov	r2, r9
	mov	r1, r8
	str	r0, [sp, #4]
	str	r7, [sp]
	ldr	r3, [sp, #20]
	ldr	r0, .L3532+72
	bl	printk
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [sp, #16]
	ldr	r0, .L3532+76
	bl	rknand_print_hex
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r1, [r5, #12]
	ldr	r0, .L3532+80
	lsr	r3, r3, #1
	bl	rknand_print_hex
	b	.L3426
.L3424:
	cmn	r7, #1
	bne	.L3430
	b	.L3426
.L3428:
	ldr	r3, [r3]
	ldr	r2, .L3532+64
	cmp	r3, r2
	beq	.L3426
	bl	lpa_hash_get_ppa
	ldr	r3, [sp, #28]
	mov	r8, r0
	cmp	r3, #0
	beq	.L3431
	ldr	fp, [r5, #12]
	ldr	r3, [fp, #8]
	cmp	r3, r0
	cmnne	r0, #1
	beq	.L3431
	ldr	r3, .L3532+84
	mov	r7, #1
	ldrb	r1, [r6, #-3136]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldr	r3, .L3532+24
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	sub	r3, r3, r0
	lsl	r3, r7, r3
	sub	r3, r3, #1
	and	r0, r3, r8, lsr r0
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	uxth	r0, r0
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L3431
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r0, r8
	ldr	r2, [r6, #-76]
	ldr	r1, [r5, #4]
	ldr	fp, [fp]
	bl	ftl_read_ppa_page
	ldr	r3, [r6, #-76]
	ldr	r3, [r3]
	cmp	fp, r3
	bhi	.L3431
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #8]
	cmn	r0, #1
	beq	.L3426
	ldr	r3, .L3532+84
	ldrb	r1, [r6, #-3136]	@ zero_extendqisi2
	ldrh	r2, [r3]
	ldr	r3, .L3532+24
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	sub	r3, r3, r2
	lsl	r7, r7, r3
	sub	r7, r7, #1
	and	r0, r7, r0, lsr r2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L3426
.L3438:
	ldrh	r1, [r4, #12]
	add	r1, r1, r2
	add	r2, r2, #1
	ldr	r1, [r3, r1, lsl #2]
	cmn	r1, #1
	addne	r7, r7, #1
	uxthne	r7, r7
	b	.L3436
.L3453:
	add	r3, sp, #32
	ldr	r0, [r3, r8, lsl #2]
	cmn	r0, #1
	bne	.L3442
.L3446:
	ldr	r2, [r10, #2800]
	mvn	r7, #0
	ldr	r3, [r5, #12]
	mov	r0, #2
	ldr	r2, [r2, #8]
	str	r2, [r3]
	mov	r2, #0
	ldr	r3, [r5, #12]
	str	r7, [r3, #4]
	ldr	r3, [r5, #12]
	str	r7, [r3, #8]
	ldr	r3, [r5, #12]
	str	r2, [r3, #12]
	ldr	r3, [r5, #12]
	str	r2, [r3, #16]
	ldr	r3, [r5, #4]
	str	r2, [r3]
	ldr	r1, [r5, #12]
	add	r1, r1, #16
	bl	ftl_debug_info_fill
.L3443:
	ldr	r3, [sp, #24]
	cmp	r3, #1
	bne	.L3448
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bls	.L3448
.L3507:
	mov	r0, r4
	bl	ftl_get_new_free_page
	ldr	r3, [r9]
	mov	fp, r0
	tst	r3, #16384
	beq	.L3450
	ldrh	r2, [r4, #12]
	mov	r1, r0
	ldrh	r3, [r4, #10]
	ldr	r0, .L3532+88
	add	r3, r3, r2
	ldr	r2, [r5, #12]
	sub	r3, r3, #1
	ldr	r2, [r2, #4]
	bl	printk
.L3450:
	ldrb	r3, [r6, #-3127]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	ldrh	r2, [r4, #6]
	mul	r3, r1, r3
	add	r3, r3, #1
	sub	r3, r3, r8
	cmp	r2, r3
	bls	.L3448
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r0, fp
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	bl	ftl_prog_ppa_page
	str	r0, [sp, #20]
	ldrh	r0, [r4]
	bl	ftl_vpn_decrement
	ldr	r2, [sp, #20]
	adds	r3, r7, #1
	movne	r3, #1
	cmn	r2, #1
	cmnne	r7, #1
	beq	.L3451
	add	r3, sp, #96
	ldrh	r2, [r4, #12]
	str	fp, [r3, r8, lsl #2]
	ldrh	r3, [r4, #10]
	add	r3, r3, r2
	add	r2, sp, #160
	sub	r3, r3, #1
	str	r3, [r2, r8, lsl #2]
.L3448:
	add	r8, r8, #1
	b	.L3441
.L3442:
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	bl	ftl_read_ppa_page
	ldr	r3, [r5, #12]
	mov	r7, r0
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	ldr	r3, [r9]
	mov	fp, r0
	tst	r3, #16384
	beq	.L3444
	ldr	r3, [r5, #12]
	add	r2, sp, #32
	mov	r1, r0
	ldr	r2, [r2, r8, lsl #2]
	ldr	r0, .L3532+92
	ldr	r3, [r3, #4]
	bl	printk
.L3444:
	add	r3, sp, #32
	mov	r2, #1
	ldr	r3, [r3, r8, lsl #2]
	mov	r0, #2
	cmp	fp, r3
	ldr	r3, [r5, #12]
	mvnne	r7, #0
	str	fp, [r3, #8]
	ldr	r3, [r5, #12]
	str	r2, [r3, #12]
	mov	r2, #0
	ldr	r3, [r5, #12]
	str	r2, [r3, #16]
	ldr	r1, [r5, #12]
	add	r1, r1, #16
	bl	ftl_debug_info_fill
	cmn	r7, #1
	bne	.L3443
	b	.L3446
.L3451:
	ldrh	r2, [r4, #6]
	cmp	r2, #1
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L3507
	b	.L3448
.L3460:
	add	r10, sp, #96
	ldr	r3, [r10, r7, lsl #2]
	cmn	r3, #1
	beq	.L3456
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	ldr	r0, [r8, r7, lsl #2]
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #256
	bne	.L3456
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	ldr	r0, [r10, r7, lsl #2]
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	bl	ftl_read_ppa_page
	ldr	r3, [r9]
	mov	r10, r0
	tst	r3, #16384
	beq	.L3458
	ldr	r2, [r5, #12]
	mov	r1, r0
	ldr	r3, [r8, r7, lsl #2]
	ldr	r0, .L3532+88
	ldr	r2, [r2, #8]
	bl	printk
.L3458:
	cmn	r10, #1
	beq	.L3456
	ldr	r1, [r5, #12]
	ldr	r2, [r8, r7, lsl #2]
	ldr	r3, [r1, #8]
	cmp	r2, r3
	bne	.L3456
	ldr	r3, [r9]
	add	r10, sp, #160
	tst	r3, #16384
	beq	.L3459
	ldr	r3, [r10, r7, lsl #2]
	mov	r0, fp
	ldr	r1, [r1, #4]
	bl	printk
.L3459:
	ldr	r2, [r10, r7, lsl #2]
	ldr	r3, [r5, #12]
	uxth	r2, r2
	ldmib	r3, {r0, r1}
	bl	lpa_hash_update_ppa
.L3456:
	add	r7, r7, #1
	b	.L3454
.L3431:
	ldr	r1, [r5, #12]
	ldr	r2, .L3532+24
	ldr	r0, [r1, #4]
	ldr	r3, [r2, #2780]
	cmp	r0, r3
	bcs	.L3426
	ldr	r3, .L3532+32
	ldrb	ip, [r4, #9]	@ zero_extendqisi2
	ldrh	r0, [r4, #10]
	ldrh	r3, [r3]
	mul	r3, r3, ip
	sub	r3, r3, #1
	cmp	r0, r3
	bge	.L3426
	ldr	r3, [r2, #2800]
	ldr	r1, [r1]
	ldr	r2, [r3, #8]
	cmp	r1, r2
	strhi	r1, [r3, #8]
	ldrh	r1, [r4, #12]
	ldrh	r2, [r4, #10]
	ldr	r3, [r5, #12]
	add	r2, r2, r1
	uxth	r2, r2
	ldmib	r3, {r0, r1}
	bl	lpa_hash_update_ppa
	ldr	r3, [sp, #36]
	str	r3, [sp, #32]
	ldr	r3, [sp, #40]
	str	r3, [sp, #36]
	ldr	r3, [sp, #44]
	str	r3, [sp, #40]
	ldr	r3, [sp, #20]
	str	r3, [sp, #44]
	b	.L3426
.L3533:
	.align	2
.L3532:
	.word	.LANCHOR2
	.word	.LC212
	.word	.LC213
	.word	.LC214
	.word	.LC215
	.word	.LC216
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3088
	.word	.LANCHOR1+2316
	.word	.LC0
	.word	.LC219
	.word	.LC220
	.word	.LC223
	.word	.LANCHOR3-3072
	.word	-178307901
	.word	1212240712
	.word	.LC217
	.word	.LC218
	.word	.LC183
	.word	.LANCHOR3-3138
	.word	.LC222
	.word	.LC221
	.fnend
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	dump_ftl_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_ftl_info, %function
dump_ftl_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	movw	r7, #1080
	ldr	r4, .L3536
	ldr	r5, .L3536+4
	ldrb	r1, [r4, #2769]	@ zero_extendqisi2
	ldr	r0, .L3536+8
	bl	printk
	ldrh	r3, [r5, #-54]
	sub	r6, r5, #3088
	ldrb	r2, [r5, #-55]	@ zero_extendqisi2
	ldrb	r1, [r5, #-56]	@ zero_extendqisi2
	ldr	r0, .L3536+12
	bl	printk
	ldr	r3, [r4, #2800]
	ldr	r0, .L3536+16
	ldrh	r2, [r3, #140]
	ldrh	r1, [r3, #130]
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #26]
	ldrb	r3, [r0, #21]	@ zero_extendqisi2
	ldrh	r2, [r0, #18]
	ldrh	r1, [r0, #16]
	str	ip, [sp, #4]
	ldrh	r0, [r0, #22]
	str	r0, [sp]
	ldr	r0, .L3536+20
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #58]
	ldrb	r3, [r0, #53]	@ zero_extendqisi2
	ldrh	r2, [r0, #50]
	ldrh	r1, [r0, #48]
	str	ip, [sp, #4]
	ldrh	r0, [r0, #54]
	str	r0, [sp]
	ldr	r0, .L3536+24
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #90]
	ldrb	r3, [r0, #85]	@ zero_extendqisi2
	ldrh	r2, [r0, #82]
	ldrh	r1, [r0, #80]
	str	ip, [sp, #4]
	ldrh	r0, [r0, #86]
	str	r0, [sp]
	ldr	r0, .L3536+28
	bl	printk
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3127]	@ zero_extendqisi2
	ldr	r1, [r5, #-2556]
	ldr	r0, .L3536+32
	mul	r3, r3, r2
	mov	r2, #4
	lsl	r3, r3, #1
	bl	rknand_print_hex
	ldrh	r3, [r4, r7]
	mov	r2, #2
	ldr	r1, [r4, #1092]
	ldr	r0, .L3536+36
	bl	rknand_print_hex
	ldr	r1, [r4, #2800]
	movw	r3, #698
	mov	r2, #4
	ldr	r0, .L3536+40
	ldrh	r3, [r1, r3]
	add	r1, r1, #704
	bl	rknand_print_hex
	ldrh	r3, [r4, r7]
	mov	r2, #4
	ldr	r1, [r4, #1084]
	ldr	r0, .L3536+44
	bl	rknand_print_hex
	sub	r1, r5, #3056
	mov	r3, #256
	mov	r2, #2
	sub	r1, r1, #14
	ldr	r0, .L3536+48
	bl	rknand_print_hex
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3127]	@ zero_extendqisi2
	ldr	r1, [r5, #-2552]
	ldr	r0, .L3536+52
	mul	r3, r3, r2
	mov	r2, #2
	lsl	r3, r3, #1
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, lr}
	b	rknand_print_hex
.L3537:
	.align	2
.L3536:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC224
	.word	.LC225
	.word	.LC226
	.word	.LC227
	.word	.LC228
	.word	.LC229
	.word	.LC230
	.word	.LC231
	.word	.LC211
	.word	.LC232
	.word	.LC233
	.word	.LC234
	.fnend
	.size	dump_ftl_info, .-dump_ftl_info
	.align	2
	.global	pm_ppa_update_check
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_ppa_update_check, %function
pm_ppa_update_check:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3542
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r2
	ldr	r7, .L3542+4
	mov	r4, r0
	sub	r2, r3, #3136
	mov	r5, r1
	ldrh	r0, [r2, #-2]
	ldrb	ip, [r7, #1153]	@ zero_extendqisi2
	ldrb	r1, [r3, #-3136]	@ zero_extendqisi2
	mvn	r3, #0
	rsb	ip, ip, #24
	sub	ip, ip, r0
	lsr	r0, r6, r0
	bic	r0, r0, r3, lsl ip
	bl	__aeabi_uidiv
	ldr	r3, [r7, #1084]
	uxth	r0, r0
	add	r0, r3, r0, lsl #2
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	lsr	r0, r0, #5
	cmp	r0, #7
	cmpne	r0, #1
	moveq	r0, #1
	movne	r0, #0
	popne	{r4, r5, r6, r7, r8, pc}
	mov	r3, r6
	mov	r2, r5
	mov	r1, r4
	ldr	r0, .L3542+8
	bl	printk
	bl	dump_ftl_info
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3543:
	.align	2
.L3542:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC235
	.fnend
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #31
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r7, r0
	mov	r6, r1
	bls	.L3545
	mov	r2, #32
	ldr	r1, .L3559
	ldr	r0, .L3559+4
	bl	printk
	bl	dump_stack
.L3545:
	ldr	r8, .L3559+8
	movw	r10, #698
	ldr	r5, .L3559+12
	ldr	r3, [r8, #2800]
	mov	r9, r8
	ldrh	r2, [r3, r10]
	cmp	r2, r7
	bcs	.L3546
	mov	r1, r7
	ldr	r0, .L3559+16
	mov	r4, #0
	bl	printk
	ldrh	r2, [r5, #-14]
	mov	r1, #255
	ldr	r0, [r4, #4]
	bl	ftl_memset
	ldr	r3, [r8, #2800]
	ldrh	r3, [r3, r10]
	cmp	r3, r7
	bcc	.L3547
.L3557:
	mov	r0, #0
.L3544:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3547:
	mov	r2, #37
.L3558:
	ldr	r1, .L3559
	ldr	r0, .L3559+4
	bl	printk
	bl	dump_stack
	b	.L3557
.L3546:
	add	r2, r7, #176
	lsl	r6, r6, #3
	ldr	r4, [r3, r2, lsl #2]
	ldr	r3, .L3559+20
	add	r8, r5, r6
	add	r2, r3, r6
	strh	r7, [r3, r6]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strh	r3, [r2, #2]	@ movhi
	bne	.L3549
	mov	r2, r4
	mov	r1, r7
	ldr	r0, .L3559+24
	bl	printk
	ldrh	r2, [r5, #-14]
	mov	r1, #255
	ldr	r0, [r8, #-2528]
	bl	ftl_memset
	b	.L3557
.L3549:
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [r5, #-32]
	ldr	r1, [r8, #-2528]
	bl	ftl_read_ppa_page
	ldr	r2, [r5, #-32]
	mov	r3, r0
	ldr	r2, [r2]
	cmp	r2, r7
	bne	.L3550
	cmn	r0, #1
	cmpne	r0, #512
	beq	.L3550
.L3554:
	ldr	r3, [r5, #-32]
	ldr	r3, [r3]
	cmp	r7, r3
	beq	.L3557
	mov	r2, #73
	b	.L3558
.L3550:
	mov	r1, r7
	str	r4, [sp]
	ldr	r0, .L3559+28
	add	r6, r5, r6
	bl	printk
	ldr	r1, [r9, #2800]
	movw	r3, #698
	mov	r2, #4
	ldr	r0, .L3559+32
	ldrh	r3, [r1, r3]
	add	r1, r1, #704
	bl	rknand_print_hex
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r1, [r6, #-2528]
	ldr	r0, .L3559+36
	lsl	r3, r3, #7
	bl	rknand_print_hex
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [r5, #-32]
	ldr	r0, .L3559+40
	bl	rknand_print_hex
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [r5, #-32]
	ldr	r1, [r6, #-2528]
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #512
	bne	.L3553
	ldrh	r2, [r5, #-14]
	mov	r1, #255
	ldr	r0, [r6, #-2528]
	bl	ftl_memset
.L3555:
	mvn	r0, #0
	b	.L3544
.L3553:
	ldr	r3, [r5, #-32]
	ldr	r3, [r3]
	cmp	r7, r3
	beq	.L3554
	b	.L3555
.L3560:
	.align	2
.L3559:
	.word	.LANCHOR1+2339
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC236
	.word	.LANCHOR3-2532
	.word	.LC237
	.word	.LC238
	.word	.LC239
	.word	.LC218
	.word	.LC240
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	pm_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_gc, %function
pm_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r6, .L3579
	ldr	r4, .L3579+4
	ldr	r3, [r6, #2800]
	add	r3, r3, #688
	ldrh	r2, [r3]
	ldrh	r3, [r4, #-176]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L3562
	ldr	r3, [r4, #-184]
	cmp	r3, #0
	beq	.L3563
.L3562:
	bl	pm_free_sblk
	ldr	r2, [r6, #2800]
	add	r3, r2, #688
	ldrh	r1, [r3]
	ldrh	r3, [r4, #-176]
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3564
	ldr	r3, [r4, #-184]
	cmp	r3, #0
	beq	.L3563
.L3564:
	uxth	r0, r0
	movw	r5, #65535
	mov	r3, #0
	add	r0, r0, #208
	str	r3, [r4, #-184]
	lsl	r0, r0, #1
	ldrh	r9, [r2, r0]
	cmp	r9, r5
	bne	.L3566
	mov	r2, #182
	ldr	r1, .L3579+8
	ldr	r0, .L3579+12
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	uxth	r0, r0
	ldr	r3, [r6, #2800]
	add	r0, r0, #208
	lsl	r0, r0, #1
	ldrh	r9, [r3, r0]
	cmp	r9, r5
	beq	.L3563
.L3566:
	ldr	r5, .L3579+16
	bl	pm_select_ram_region
	lsl	r7, r0, #3
	mov	r10, r0
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	add	r8, r5, r7
	add	r5, r5, #4
	cmp	r0, r3
	beq	.L3567
	add	r3, r4, r7
	ldr	r1, [r3, #-2528]
	cmp	r1, #0
	beq	.L3567
	ldrsh	r3, [r8, #2]
	cmp	r3, #0
	bge	.L3567
	bl	pm_write_page
	ldrh	r3, [r8, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r8, #2]	@ movhi
.L3567:
	sub	r5, r5, #4
	mov	r8, #0
	add	r5, r5, r7
.L3568:
	ldr	r3, [r6, #2800]
	movw	r1, #698
	uxth	r2, r8
	ldrh	r1, [r3, r1]
	cmp	r1, r2
	bhi	.L3571
	bl	pm_free_sblk
.L3563:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3571:
	ldr	r1, .L3579+20
	uxth	fp, r8
	ldrb	r0, [r6, #1153]	@ zero_extendqisi2
	add	ip, fp, #176
	str	r2, [sp, #4]
	ldrh	r1, [r1]
	ldr	r3, [r3, ip, lsl #2]
	rsb	r0, r0, #24
	mvn	ip, #0
	sub	r0, r0, r1
	lsr	r3, r3, r1
	ldrb	r1, [r4, #-3136]	@ zero_extendqisi2
	bic	r0, r3, ip, lsl r0
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldr	r2, [sp, #4]
	cmp	r9, r0
	bne	.L3569
	mov	r1, r10
	mov	r0, r2
	bl	load_l2p_region
	cmp	r0, #0
	bne	.L3570
	add	r3, r4, r7
	mov	r0, fp
	ldr	r1, [r3, #-2528]
	bl	pm_write_page
.L3570:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L3569:
	add	r8, r8, #1
	b	.L3568
.L3580:
	.align	2
.L3579:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2355
	.word	.LC0
	.word	.LANCHOR3-2532
	.word	.LANCHOR3-3138
	.fnend
	.size	pm_gc, .-pm_gc
	.align	2
	.global	pm_flush_id
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_flush_id, %function
pm_flush_id:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	lsl	r0, r0, #3
	ldr	r4, .L3587
	sub	r3, r4, #2528
	add	r2, r4, r0
	sub	r3, r3, #4
	ldr	r1, [r2, #-2528]
	add	r5, r3, r0
	ldrh	r0, [r3, r0]
	bl	pm_write_page
	ldrh	r3, [r5, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #-36]
	cmp	r3, #0
	beq	.L3582
	bl	pm_gc
	mov	r3, #0
	str	r3, [r4, #-36]
.L3582:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L3588:
	.align	2
.L3587:
	.word	.LANCHOR3
	.fnend
	.size	pm_flush_id, .-pm_flush_id
	.align	2
	.global	pm_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_flush, %function
pm_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r5, .L3594
.L3591:
	add	r3, r5, r4, lsl #3
	uxth	r0, r4
	ldrsh	r3, [r3, #2]
	cmp	r3, #0
	bge	.L3590
	bl	pm_flush_id
.L3590:
	add	r4, r4, #1
	cmp	r4, #32
	bne	.L3591
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L3595:
	.align	2
.L3594:
	.word	.LANCHOR3-2532
	.fnend
	.size	pm_flush, .-pm_flush
	.align	2
	.global	flt_sys_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	flt_sys_flush, %function
flt_sys_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, #0
	pop	{r4, lr}
	b	ftl_info_flush
	.fnend
	.size	flt_sys_flush, .-flt_sys_flush
	.align	2
	.global	zftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_deinit, %function
zftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	zftl_flash_de_init
	bl	flt_sys_flush
	pop	{r4, lr}
	b	zftl_flash_de_init
	.fnend
	.size	zftl_deinit, .-zftl_deinit
	.align	2
	.global	pm_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_init, %function
pm_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #0
	ldr	r8, .L3623
	mvn	r9, #0
	mov	r10, r5
	mov	r3, #1
	ldr	r4, .L3623+4
	.pad #20
	sub	sp, sp, #20
	mov	r7, r0
	mov	r0, #64
	str	r5, [r4, #-184]
	str	r5, [r4, #-36]
	strb	r3, [r4, #-28]
	bl	ftl_dma32_malloc
	str	r0, [r4, #-32]
.L3602:
	cmp	r7, #0
	mov	r6, r8
	strh	r9, [r6, r5]!	@ movhi
	strh	r10, [r6, #2]	@ movhi
	beq	.L3601
	ldrb	r0, [r4, #-2546]	@ zero_extendqisi2
	lsl	r0, r0, #9
	bl	ftl_dma32_malloc
	str	r0, [r6, #4]
.L3601:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L3602
	ldr	r5, .L3623+8
	ldr	r9, [r4, #-2528]
	ldr	r6, [r4, #-32]
	ldr	r3, [r5, #2800]
	add	r2, r3, #692
	ldrb	r0, [r3, #694]	@ zero_extendqisi2
	mov	r3, #4
	ldrh	r1, [r2]
	str	r3, [sp]
	mov	r2, r9
	mov	r3, r6
	bl	flash_get_last_written_page
	ldr	r3, [r5, #2800]
	mov	r8, r0
	add	r2, r3, #696
	ldrh	r2, [r2]
	cmp	r2, r0
	bgt	.L3603
	add	r1, r3, #692
	mov	r3, r0
	ldrh	r1, [r1]
	ldr	r0, .L3623+12
	bl	printk
	ldr	r3, [r5, #2800]
	add	r3, r3, #696
	ldrsh	r7, [r3]
	add	r3, r8, #1
	str	r3, [sp, #12]
.L3604:
	ldr	r3, [sp, #12]
	cmp	r7, r3
	blt	.L3607
	mov	r3, #1
	ldrh	r2, [sp, #12]
	strb	r3, [r4, #-47]
	ldr	r3, [r5, #2800]
	add	r3, r3, #696
	strh	r2, [r3]	@ movhi
	bl	pm_free_sblk
.L3603:
	ldrh	r2, [r4, #-14]
	mov	r1, #255
	ldr	r0, [r4, #-2528]
	bl	ftl_memset
	ldr	r1, [r4, #-2528]
	mvn	r0, #0
	bl	pm_write_page
	ldrb	r3, [r4, #-47]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3608
	ldr	r1, [r4, #-2528]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r4, #-2528]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r4, #-2528]
	mvn	r0, #0
	bl	pm_write_page
.L3608:
	bl	pm_free_sblk
	bl	pm_gc
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3607:
	ldr	r3, [r5, #2800]
	movw	fp, #694
	add	r2, r3, #692
	ldrb	r0, [r3, fp]	@ zero_extendqisi2
	ldrh	r8, [r2]
	ldr	r2, .L3623+16
	ldrb	r3, [r4, #-2546]	@ zero_extendqisi2
	ldrh	r2, [r2]
	str	r3, [sp]
	mov	r3, r6
	mla	r8, r2, r8, r7
	mov	r2, r9
	mov	r1, r8
	bl	flash_read_page_en
	ldr	r3, [r5, #2800]
	mov	r10, r0
	ldr	r0, .L3623+20
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	mov	r2, r8
	ldrh	r3, [r3, fp]
	ldr	r1, [r6]
	bl	printk
	cmp	r10, #512
	cmnne	r10, #1
	beq	.L3605
	ldr	r2, [r5, #2800]
	movw	r3, #698
	ldrh	r3, [r2, r3]
	ldr	r2, [r6]
	cmp	r2, r3
	bcs	.L3605
	ldr	r10, [r6, #8]
	cmp	r10, #0
	beq	.L3606
	ldrb	r1, [r4, #-2546]	@ zero_extendqisi2
	mov	r0, r9
	lsl	r1, r1, #9
	bl	js_hash
	cmp	r10, r0
	beq	.L3606
	ldr	r1, [r6, #8]
	ldr	r0, .L3623+24
	bl	printk
.L3605:
	add	r7, r7, #1
	sxth	r7, r7
	b	.L3604
.L3606:
	ldr	r3, [r6]
	ldr	r2, [r5, #2800]
	add	r3, r3, #176
	str	r8, [r2, r3, lsl #2]
	b	.L3605
.L3624:
	.align	2
.L3623:
	.word	.LANCHOR3-2532
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC241
	.word	.LANCHOR3-3074
	.word	.LC242
	.word	.LC243
	.fnend
	.size	pm_init, .-pm_init
	.align	2
	.global	pm_log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	pm_log2phys, %function
pm_log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r1
	ldr	r8, .L3641
	mov	r6, r0
	mov	r9, r2
	ldr	r10, .L3641+4
	ldrb	r4, [r8, #-2546]	@ zero_extendqisi2
	lsl	r1, r4, #7
	lsl	r4, r4, #7
	bl	__aeabi_uidiv
	str	r0, [sp, #4]
	ldrh	fp, [sp, #4]
	ldr	r2, [r10, #2780]
	smulbb	r4, r4, fp
	cmp	r6, r2
	sub	r4, r6, r4
	bcc	.L3626
	mov	r1, r6
	ldr	r0, .L3641+8
	bl	printk
	cmp	r9, #0
	mvn	r0, #0
	streq	r0, [r7]
.L3625:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3626:
	ldr	r1, .L3641+12
	uxth	r4, r4
	mov	r2, #0
	sub	r6, r8, #2528
.L3632:
	lsl	r0, r2, #3
	uxth	r5, r2
	add	ip, r1, r0
	ldr	ip, [ip, #4]
	cmp	ip, #0
	beq	.L3628
	ldrh	r0, [r0, r1]
	cmp	r0, fp
	bne	.L3628
.L3629:
	cmp	r9, #0
	lsl	r5, r5, #3
	bne	.L3630
	add	r2, r8, r5
	ldr	r2, [r2, #-2528]
	ldr	r1, [r2, r4, lsl #2]
	mvn	r2, #0
	str	r1, [r7]
	ldrb	r0, [r10, #1153]	@ zero_extendqisi2
	ldrb	r3, [r8, #-3072]	@ zero_extendqisi2
	rsb	ip, r0, #24
	mvn	r2, r2, lsl r0
	and	r2, r2, r1, lsr ip
	cmp	r2, r3
	mvncs	r3, #0
	strcs	r3, [r7]
.L3631:
	sub	r6, r6, #4
	movw	r2, #32767
	add	r6, r6, r5
	mov	r0, #0
	ldrh	r3, [r6, #2]
	ubfx	r1, r3, #0, #15
	cmp	r1, r2
	addne	r3, r3, #1
	strhne	r3, [r6, #2]	@ movhi
	b	.L3625
.L3630:
	add	r2, r8, r5
	ldr	r1, [r7]
	ldr	r2, [r2, #-2528]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	str	r1, [r2, r4, lsl #2]
	sub	r1, r6, #4
	add	r1, r1, r5
	strb	r3, [r8, #-2276]
	ldrh	r2, [r1, #2]
	mvn	r2, r2, lsl #17
	mvn	r2, r2, lsr #17
	strh	r2, [r1, #2]	@ movhi
	b	.L3631
.L3628:
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L3632
	bl	pm_select_ram_region
	lsl	r1, r0, #3
	sub	r2, r6, #4
	mov	r5, r0
	add	ip, r2, r1
	ldrh	r1, [r2, r1]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L3633
	ldrsh	r2, [ip, #2]
	cmp	r2, #0
	bge	.L3633
	bl	pm_flush_id
.L3633:
	mov	r1, r5
	mov	r0, fp
	strb	r5, [r8, #-12]
	bl	load_l2p_region
	b	.L3629
.L3642:
	.align	2
.L3641:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC244
	.word	.LANCHOR3-2532
	.fnend
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_recovery, %function
gc_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #65535
	ldr	r8, .L3744
	mov	r5, #0
	.pad #76
	sub	sp, sp, #76
	ldr	r7, .L3744+4
	ldr	r4, [r8, #1096]
	strb	r5, [r7, #-3119]
	ldrh	r2, [r4, #80]
	strb	r5, [r7, #-144]
	cmp	r2, r3
	beq	.L3644
	mvn	r3, #0
	mov	r0, #1
	strh	r3, [r4, #130]	@ movhi
	bl	buf_alloc
	ldrb	r3, [r4, #89]	@ zero_extendqisi2
	mov	r6, r0
	ldrb	r1, [r7, #-3128]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	cmp	r1, #2
	ldrh	r2, [r3, #94]
	sub	r3, r7, #3072
	ldrh	r3, [r3, #-2]
	mul	r2, r3, r2
	str	r2, [sp, #64]
	beq	.L3645
	ldrb	r3, [r8, #1158]	@ zero_extendqisi2
	cmp	r3, r5
	bne	.L3645
	ldrb	r3, [r8, #1159]	@ zero_extendqisi2
	cmp	r3, r5
	beq	.L3646
.L3645:
	ldr	r3, .L3744+8
	ldrh	r3, [r3, #-14]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, r1, lsl #24
.L3734:
	str	r3, [r6, #24]
.L3647:
	mov	r1, #1
	mov	r0, r6
	bl	sblk_read_page
	ldr	r3, [r6, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3648
	ldr	r3, [r6, #12]
	ldr	r9, .L3744+12
	ldr	r3, [r3]
	cmp	r3, r9
	beq	.L3649
.L3648:
	mov	r0, r6
	bl	zbuf_free
	ldr	r2, [r6, #12]
	ldr	r3, [r6, #4]
	ldr	r0, .L3744+16
	ldr	r1, [r2, #12]
	str	r1, [sp, #24]
	ldr	r1, [r2, #8]
	str	r1, [sp, #20]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r6, #36]
	ldr	r1, [r6, #24]
	bl	printk
.L3742:
	ldrh	r3, [r4, #80]
	mov	r5, #0
	ldr	r2, [r8, #1092]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r2, [r4, #80]
	ldr	r3, [r8, #1096]
	strh	r2, [r3, #130]	@ movhi
.L3650:
	ldrh	r0, [r4, #80]
	ldr	r6, .L3744
	ldr	r2, [r6, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3694
	bl	ftl_dump_write_open_sblk
.L3694:
	mov	r2, r5
	ldrh	r1, [r4, #80]
	ldr	r0, .L3744+20
	bl	printk
	mvn	r3, #0
	strh	r3, [r4, #80]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r6, #1096]
	movw	r2, #65535
	ldrh	r3, [r3, #130]
	cmp	r3, r2
	beq	.L3695
	movw	r2, #1080
	ldrh	r2, [r6, r2]
	cmp	r2, r3
	bhi	.L3696
	movw	r2, #517
	ldr	r1, .L3744+24
	ldr	r0, .L3744+28
	bl	printk
	bl	dump_stack
.L3696:
	ldr	r3, [r6, #1096]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
.L3695:
	ldr	r2, [r6, #2800]
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, #126]	@ movhi
	ldr	r2, [r6, #1096]
	strh	r3, [r2, #130]	@ movhi
	bl	ftl_info_flush
.L3643:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3646:
	cmp	r1, #3
	bne	.L3647
	sub	r3, r7, #3088
	ldrh	r3, [r3, #-8]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, #50331648
	b	.L3734
.L3649:
	ldrb	r3, [r7, #-11]	@ zero_extendqisi2
	ldr	r5, .L3744+8
	cmp	r3, #2
	bne	.L3651
	ldrb	r3, [r7, #-3128]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L3651
	ldrh	r2, [r5, #-14]
	ldrh	r0, [r7, #-14]
	ldr	r1, [r6, #4]
	lsl	r3, r2, #2
	ldrb	r2, [r7, #-3127]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldr	r3, [r7, #-132]
	sub	r2, r2, r0
	lsr	r0, r0, #2
	uxth	r2, r2
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	ldrb	r3, [r8, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3652
	ldrb	r3, [r8, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3653
.L3652:
	ldr	r3, [r6, #24]
	sub	r3, r3, #1
.L3735:
	str	r3, [r6, #24]
	mov	r1, #1
	mov	r0, r6
	bl	sblk_read_page
	ldr	r3, [r6, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3655
	ldr	r3, [r6, #12]
	ldr	r3, [r3]
	cmp	r3, r9
	beq	.L3656
.L3655:
	mov	r0, r6
	bl	zbuf_free
	b	.L3742
.L3653:
	ldr	r3, [sp, #64]
	ldrh	r2, [r5, #8]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, #33554432
	b	.L3735
.L3656:
	ldrh	r2, [r7, #-14]
.L3736:
	ldr	r1, [r6, #4]
	ldr	r0, [r7, #-132]
	bl	ftl_memcpy
	ldrh	r3, [r5, #-14]
	ldrb	r2, [r7, #-3127]	@ zero_extendqisi2
	mul	r2, r2, r3
	ldrb	r3, [r7, #-2546]	@ zero_extendqisi2
	cmp	r3, r2, asr #6
	lsl	r2, r2, #2
	bge	.L3658
	mov	r1, #0
	ldr	r0, [r7, #-128]
	bl	ftl_memset
	ldrb	r10, [r7, #-11]	@ zero_extendqisi2
	cmp	r10, #1
	movne	r10, #1
	bne	.L3737
	ldrh	r1, [r5, #-14]
	ldrb	r3, [r7, #-3127]	@ zero_extendqisi2
	ldrh	r5, [r7, #-14]
	ldr	r0, [r7, #-128]
	mul	r3, r3, r1
	ldr	r1, [r6, #4]
	lsl	r3, r3, #2
	sub	r5, r5, r3
	add	r1, r1, r3
	mov	r2, r5
	bl	ftl_memcpy
.L3659:
	ldr	r3, .L3744+32
	ldrh	r3, [r3, #-8]
	str	r3, [sp, #44]
	ldrb	r3, [r7, #-3128]	@ zero_extendqisi2
	cmp	r3, #2
	str	r3, [sp, #36]
	bne	.L3660
	ldrb	r3, [r7, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3661
.L3660:
	ldrb	r3, [r8, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3662
.L3661:
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #44]
	mul	r3, r3, r2
	str	r3, [sp, #44]
	mov	r3, #1
	str	r3, [sp, #36]
.L3662:
	ldr	r8, .L3744+4
	mov	r7, #0
	str	r7, [sp, #48]
.L3663:
	ldr	r3, [sp, #44]
	cmp	r3, r7
	bls	.L3676
	add	r3, r7, r7, lsl #1
	sub	r3, r3, #1
	str	r3, [sp, #52]
	mov	r3, #0
	b	.L3739
.L3651:
	ldrh	r3, [r5, #-14]
	ldrb	r2, [r7, #-3127]	@ zero_extendqisi2
	mul	r2, r2, r3
	lsl	r2, r2, #2
	b	.L3736
.L3658:
	ldr	r1, [r6, #4]
	mov	r10, #0
	ldr	r0, [r7, #-128]
	add	r1, r1, r2
	bl	ftl_memcpy
.L3737:
	mov	r5, #0
	b	.L3659
.L3672:
	ldr	r3, [sp, #56]
	ldrb	r1, [r8, #-3128]	@ zero_extendqisi2
	ldrh	r2, [r3, #96]
	ldr	r3, .L3744+36
	cmp	r1, #2
	ldrh	r3, [r3, #-2]
	mul	r3, r3, r2
	str	r3, [sp, #64]
	beq	.L3664
	ldr	r2, .L3744
	ldrb	r2, [r2, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3665
.L3664:
	sub	r2, r7, #1
	add	r3, r2, r3
	add	r3, r3, fp
	orr	r3, r3, r1, lsl #24
.L3738:
	str	r3, [r6, #24]
	mov	r1, #1
	mov	r0, r6
	bl	sblk_read_page
	cmp	r10, #0
	beq	.L3669
	ldr	r3, [r8, #-128]
	ldr	r2, [r3, r9]
	cmp	r2, #0
	ldreq	r2, [r6, #12]
	ldreq	r2, [r2, #8]
	streq	r2, [r3, r9]
.L3669:
	ldr	r3, [r8, #-132]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L3670
	ldr	r0, [r8, #-128]
	ldr	ip, [r0, r9]
	ldr	r0, [r2, #8]
	cmp	ip, r0
	beq	.L3671
.L3670:
	ldr	r0, [r2, #12]
	str	r0, [sp, #16]
	ldr	r0, [r2, #8]
	str	r1, [sp, #8]
	str	r0, [sp, #12]
	ldr	r2, [r2]
	ldr	r0, .L3744+40
	str	r2, [sp, #4]
	ldr	r2, [r8, #-128]
	ldr	r2, [r2, r9]
	str	r2, [sp]
	ldr	r2, [r6, #36]
	ldr	r1, [r6, #24]
	bl	printk
	ldr	r3, [r8, #-132]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L3671
	mov	r0, r6
	bl	zbuf_free
	ldrh	r3, [r4, #80]
	mov	r0, #0
	ldr	r2, .L3744
	ldr	r1, [r2, #1092]
	lsl	r3, r3, #1
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r4, #80]
	ldr	r3, [r2, #1096]
	strh	r1, [r3, #130]	@ movhi
	b	.L3650
.L3665:
	cmp	r1, #3
	addne	r3, r7, r3
	bne	.L3738
	ldr	r2, .L3744
	ldrb	r2, [r2, #1159]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [sp, #52]
	addeq	r3, r7, r3
	orreq	r3, r3, fp, lsl #24
	addne	r3, r3, r2
	addne	r3, r3, fp
	orrne	r3, r3, #50331648
	b	.L3738
.L3671:
	ldr	r3, [sp, #48]
	add	fp, fp, #1
	add	r9, r9, #4
	add	r3, r3, #1
	str	r3, [sp, #48]
.L3674:
	ldr	r3, [sp, #36]
	cmp	r3, fp
	bcs	.L3672
	ldr	r3, [sp, #40]
	add	r3, r3, #1
.L3739:
	str	r3, [sp, #40]
	ldr	r2, [sp, #40]
	ldrb	r3, [r4, #89]	@ zero_extendqisi2
	cmp	r2, r3
	bge	.L3673
	ldr	r3, [sp, #48]
	mov	fp, #1
	lsl	r9, r3, #2
	add	r3, r4, r2, lsl #1
	str	r3, [sp, #56]
	b	.L3674
.L3673:
	ldrb	r3, [r8, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r7, r7, #1
	add	r7, r7, #1
	b	.L3663
.L3676:
	mov	r0, r6
	mov	r5, #0
	bl	zbuf_free
	ldr	r3, .L3744
	mov	r6, r5
	ldrb	r1, [r4, #89]	@ zero_extendqisi2
	mov	r10, r5
	ldrh	r2, [r4, #80]
	ldr	r0, [r3, #1092]
	ldr	r3, .L3744+8
	lsl	r2, r2, #1
	ldrh	ip, [r3, #-14]
	add	r9, r3, #3104
	smulbb	r1, r1, ip
	strh	r1, [r0, r2]	@ movhi
.L3678:
	ldr	r3, [sp, #44]
	cmp	r3, r6
	bls	.L3692
	add	r2, r6, r6, lsl #1
	sub	r3, r2, #1
	str	r3, [sp, #52]
	mov	r3, #0
	b	.L3741
.L3688:
	ldr	r2, [r9, #-132]
	ldr	fp, [r2, r10, lsl #2]
	cmn	fp, #1
	beq	.L3679
	ldr	r2, [r9, #-128]
	mov	r0, fp
	ldr	r3, [r2, r10, lsl #2]
	str	r3, [sp, #48]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #68]
	bne	.L3680
	mov	r2, #0
	add	r1, sp, #68
	mov	r0, fp
	bl	pm_log2phys
.L3680:
	ldr	r3, [sp, #56]
	ldr	r1, .L3744+44
	ldrb	r0, [r9, #-3128]	@ zero_extendqisi2
	ldrh	r2, [r3, #96]
	ldrh	r1, [r1]
	cmp	r0, #2
	mul	r2, r1, r2
	beq	.L3681
	ldr	r1, .L3744
	ldrb	ip, [r1, #1158]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L3682
.L3681:
	sub	r1, r6, #1
	add	r2, r1, r2
	add	r2, r2, r8
	orr	r2, r2, r0, lsl #24
.L3740:
	str	r2, [sp, #64]
	ldr	r2, .L3744+48
	ldr	r3, [sp, #48]
	ldrb	r1, [r9, #-3136]	@ zero_extendqisi2
	ldrh	ip, [r2, #-2]
	ldr	r2, .L3744
	ldrb	r0, [r2, #1153]	@ zero_extendqisi2
	mov	r2, #1
	str	r2, [sp, #60]
	rsb	r0, r0, #24
	sub	r0, r0, ip
	lsl	r0, r2, r0
	sub	r0, r0, #1
	and	r0, r0, r3, lsr ip
	bl	__aeabi_uidiv
	ldr	r1, [sp, #68]
	mov	r7, r0
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #60]
	cmp	r3, r1
	bne	.L3686
	add	r1, sp, #64
	mov	r0, fp
	bl	pm_log2phys
	uxth	r0, r7
	add	r5, r5, #1
	bl	ftl_vpn_decrement
.L3687:
	ldr	r1, .L3744
	uxth	r7, r7
	add	r10, r10, #1
	ldr	r2, [r1, #1084]
	add	r2, r2, r7, lsl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r0, r2, #224
	bne	.L3679
	ldr	r2, [r1, #1092]
	lsl	r7, r7, #1
	ldrh	r1, [r2, r7]
	cmp	r1, #0
	strhne	r0, [r2, r7]	@ movhi
.L3679:
	add	r8, r8, #1
.L3690:
	ldr	r3, [sp, #36]
	cmp	r3, r8
	bcs	.L3688
	ldr	r3, [sp, #40]
	add	r3, r3, #1
.L3741:
	str	r3, [sp, #40]
	ldrb	r2, [r4, #89]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r3, r2
	bge	.L3689
	add	r3, r4, r3, lsl #1
	mov	r8, #1
	str	r3, [sp, #56]
	b	.L3690
.L3682:
	cmp	r0, #3
	addne	r2, r6, r2
	bne	.L3740
	ldrb	r1, [r1, #1159]	@ zero_extendqisi2
	cmp	r1, #0
	lsl	r1, r8, #24
	ldrne	r3, [sp, #52]
	addeq	r2, r6, r2
	addne	r2, r2, r3
	addne	r2, r2, r8
	orr	r2, r2, r1
	b	.L3740
.L3686:
	ldr	r2, [sp, #64]
	cmp	r1, r2
	addeq	r5, r5, #1
	b	.L3687
.L3689:
	ldrb	r2, [r9, #-3126]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r6, r6, #1
	add	r6, r6, #1
	b	.L3678
.L3692:
	ldrh	r3, [r4, #80]
	ldr	r2, .L3744
	ldr	r2, [r2, #1092]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r0, [r4, #80]
	bl	zftl_insert_data_list
	b	.L3650
.L3644:
	ldrh	r3, [r4, #130]
	cmp	r3, r2
	beq	.L3643
	ldr	r2, [r8, #2800]
	ldrh	r2, [r2, #126]
	cmp	r2, r3
	bne	.L3698
	bl	pm_flush
	ldr	r3, [r8, #1096]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
	ldr	r3, [r8, #2800]
	mvn	r2, #0
	mov	r0, r5
	strh	r2, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L3698:
	ldr	r3, [r8, #1096]
	mvn	r2, #0
	strh	r2, [r3, #130]	@ movhi
	b	.L3643
.L3745:
	.align	2
.L3744:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LC245
	.word	.LC247
	.word	.LANCHOR1+2361
	.word	.LC0
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3072
	.word	.LC246
	.word	.LANCHOR3-3074
	.word	.LANCHOR3-3136
	.fnend
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	gc_update_l2p_map_new
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L3781
	ldr	r3, .L3781+4
	ldr	r5, [r4, #1096]
	ldrh	r3, [r3, #-14]
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	mul	r3, r2, r3
	str	r3, [sp]
	ldr	r3, .L3781+8
	ldr	r2, [r3]
	mov	fp, r3
	tst	r2, #256
	beq	.L3747
	ldrh	r1, [r5, #80]
	ldr	r0, .L3781+12
	bl	printk
.L3747:
	ldr	r2, .L3781+16
	mov	r7, #0
	ldrh	r3, [r5, #80]
	mov	r6, r7
	ldr	r0, [sp]
	ldrb	r2, [r2, #-11]	@ zero_extendqisi2
	ldr	r8, .L3781+16
	ldr	r1, [r4, #1092]
	lsl	r3, r3, #1
	sub	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
.L3748:
	ldr	r3, [sp]
	cmp	r6, r3
	bne	.L3757
	ldr	r3, [fp]
	tst	r3, #256
	beq	.L3758
	ldrh	r2, [r5, #80]
	mov	r3, r7
	ldr	r0, [r4, #1092]
	ldr	r1, [r4, #1096]
	lsl	r2, r2, #1
	ldrh	r1, [r1, #80]
	ldrh	r2, [r0, r2]
	ldr	r0, .L3781+20
	bl	printk
.L3758:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r7, r3
	beq	.L3759
	movw	r2, #898
	ldr	r1, .L3781+24
	ldr	r0, .L3781+28
	bl	printk
	bl	dump_stack
.L3759:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3757:
	ldr	r3, [r8, #-132]
	lsl	r9, r6, #2
	ldr	r10, [r3, r6, lsl #2]
	cmn	r10, #1
	beq	.L3749
	ldrb	r1, [r8, #-2546]	@ zero_extendqisi2
	mov	r0, r10
	lsl	r1, r1, #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #12]
	ldr	r3, [fp]
	tst	r3, #256
	beq	.L3750
	mov	r3, r6
	mov	r2, r10
	ldr	r1, [sp, #12]
	ldr	r0, .L3781+32
	bl	printk
.L3750:
	str	r6, [sp, #8]
.L3756:
	ldr	r3, [r8, #-132]
	ldr	r10, [r3, r9]
	cmn	r10, #1
	beq	.L3751
	ldrb	r1, [r8, #-2546]	@ zero_extendqisi2
	mov	r0, r10
	lsl	r1, r1, #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	uxth	r0, r0
	cmp	r3, r0
	bne	.L3751
	ldr	r3, [r8, #-128]
	mov	r0, r10
	ldr	r3, [r3, r9]
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3752
	mov	r2, #0
	add	r1, sp, #20
	mov	r0, r10
	bl	pm_log2phys
.L3752:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L3753
	ldr	r3, .L3781+16
	add	r1, sp, #24
	mov	r2, #1
	mov	r0, r10
	add	r7, r7, #1
	ldr	r3, [r3, #-124]
	ldr	r3, [r3, r9]
	str	r3, [r1, #-8]!
	bl	pm_log2phys
	ldr	r3, .L3781+36
	ldr	r2, [sp, #4]
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	sub	r3, r3, r0
	lsr	r0, r2, r0
	ldr	r2, .L3781+16
	ldrb	r1, [r2, #-3136]	@ zero_extendqisi2
	mvn	r2, #0
	bic	r0, r0, r2, lsl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
.L3780:
	bl	ftl_vpn_decrement
	ldr	r3, [r8, #-132]
	mvn	r2, #0
	str	r2, [r3, r9]
.L3751:
	ldr	r3, [sp, #8]
	add	r9, r9, #4
	add	r3, r3, #1
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [sp]
	cmp	r3, r2
	bne	.L3756
.L3749:
	add	r6, r6, #1
	b	.L3748
.L3753:
	ldr	r2, [fp]
	tst	r2, #256
	beq	.L3755
	ldr	r2, [sp, #4]
	mov	r1, r10
	ldr	r0, .L3781+40
	bl	printk
.L3755:
	ldrh	r0, [r5, #80]
	b	.L3780
.L3782:
	.align	2
.L3781:
	.word	.LANCHOR0
	.word	.LANCHOR3-3104
	.word	.LANCHOR2
	.word	.LC248
	.word	.LANCHOR3
	.word	.LC251
	.word	.LANCHOR1+2373
	.word	.LC0
	.word	.LC249
	.word	.LANCHOR3-3138
	.word	.LC250
	.fnend
	.size	gc_update_l2p_map_new, .-gc_update_l2p_map_new
	.align	2
	.global	gc_scan_src_blk_one_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_scan_src_blk_one_page, %function
gc_scan_src_blk_one_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r1, #0
	ldr	r5, .L3810
	mov	lr, r1
	movw	r0, #65535
	ldr	r4, .L3810+4
	ldrb	r3, [r5, #2828]	@ zero_extendqisi2
	ldr	r7, .L3810+8
	add	r2, r4, r3, lsl #1
	ldrb	ip, [r7, #-3127]	@ zero_extendqisi2
	ldrh	r8, [r2, #36]
	ldrh	r2, [r4, #2]
.L3784:
	cmp	r8, r0
	beq	.L3786
	cmp	lr, #0
	mov	r0, #1
	strhne	r2, [r4, #2]	@ movhi
	cmp	r1, #0
	strbne	r3, [r5, #2828]
	mov	r9, #1
	bl	buf_alloc
	mov	r6, r0
.L3789:
	ldrb	r1, [r5, #2830]	@ zero_extendqisi2
	cmp	r9, r1
	ble	.L3799
	mov	r0, r6
	bl	zbuf_free
	ldrb	r3, [r5, #2828]	@ zero_extendqisi2
	ldrb	r2, [r7, #-3127]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r2, r3
	strb	r3, [r5, #2828]
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r5, #2828]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3786:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, ip
	moveq	r3, #0
	addeq	r2, r2, #1
	add	r1, r4, r3, lsl #1
	uxtheq	r2, r2
	ldrh	r8, [r1, #36]
	moveq	lr, #1
	mov	r1, #1
	b	.L3784
.L3799:
	ldr	r3, .L3810+12
	cmp	r1, #2
	ldrh	r3, [r3]
	mul	r2, r8, r3
	ldrheq	r3, [r4, #2]
	addeq	r3, r2, r3, lsl #1
	subeq	r3, r3, #1
	beq	.L3809
	cmp	r1, #3
	ldrhne	r3, [r4, #2]
	addne	r3, r3, r2
	bne	.L3808
	ldrb	r3, [r5, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3793
	ldrb	r3, [r5, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, r2
	orreq	r3, r3, r9, lsl #24
	beq	.L3808
.L3793:
	ldrh	r3, [r4, #2]
	add	r3, r3, r3, lsl #1
	sub	r3, r3, #1
	add	r3, r3, r2
.L3809:
	ldrb	r2, [r7, #-3128]	@ zero_extendqisi2
	add	r3, r3, r9
	orr	r3, r3, r2, lsl #24
.L3808:
	str	r3, [r6, #24]
	mov	r1, #1
	mov	r0, r6
	bl	sblk_read_page
	ldr	r3, [r6, #36]
	cmp	r3, #512
	cmnne	r3, #1
	beq	.L3796
	ldr	r3, [r6, #12]
	ldr	r10, [r3, #4]
	mov	r0, r10
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	bne	.L3797
	ldr	r3, [r5, #2780]
	cmp	r10, r3
	bcs	.L3797
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r10
	bl	pm_log2phys
.L3797:
	ldr	r3, [r6, #24]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	ldrheq	r1, [r4, #20]
	ldreq	r2, [r7, #-3132]
	streq	r3, [r2, r1, lsl #2]
	ldrheq	r3, [r4, #20]
	addeq	r3, r3, #1
	strheq	r3, [r4, #20]	@ movhi
.L3796:
	ldrh	r3, [r4, #22]
	add	r9, r9, #1
	add	r3, r3, #1
	strh	r3, [r4, #22]	@ movhi
	b	.L3789
.L3811:
	.align	2
.L3810:
	.word	.LANCHOR0
	.word	.LANCHOR0+2824
	.word	.LANCHOR3
	.word	.LANCHOR3-3074
	.fnend
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2824
	ldr	r4, .L3867
	.pad #28
	sub	sp, sp, #28
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3813
	movw	r2, #1505
	ldr	r1, .L3867+4
	ldr	r0, .L3867+8
	bl	printk
	bl	dump_stack
.L3813:
	movw	r3, #2824
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	moveq	r0, #0
	beq	.L3812
	ldr	r3, .L3867+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3815
	ldr	r2, [r4, #1092]
	lsl	r3, r1, #1
	ldr	r0, .L3867+16
	ldrh	r2, [r2, r3]
	bl	printk
.L3815:
	ldr	r7, .L3867+20
	movw	r5, #2824
	bl	timer_get_time
	ldrh	r0, [r4, r5]
	add	r1, r7, #36
	bl	ftl_get_blk_list_in_sblk
	uxtb	r0, r0
	cmp	r0, #0
	strb	r0, [r4, #2829]
	mvneq	r3, #0
	strheq	r3, [r4, r5]	@ movhi
	beq	.L3812
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #1084]
	add	r2, r2, r3, lsl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #32
	beq	.L3817
	cmp	r2, #0
	cmpne	r2, #224
	beq	.L3818
	ldr	r2, [r4, #1096]
	ldrh	r1, [r2, #16]
	cmp	r1, r3
	beq	.L3817
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3817
	ldrh	r2, [r2, #80]
	cmp	r2, r3
	bne	.L3860
.L3817:
	mvn	r2, #0
	movw	r3, #2824
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	strh	r0, [r7, #20]	@ movhi
.L3812:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3818:
	cmp	r2, #0
	bne	.L3817
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3820
	movw	r2, #1530
	ldr	r1, .L3867+4
	ldr	r0, .L3867+8
	bl	printk
	bl	dump_stack
.L3820:
	movw	r3, #2824
	ldr	r2, [r4, #1092]
	ldrh	r3, [r4, r3]
	mov	r1, #0
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L3817
.L3860:
	add	r0, r7, r0, lsl #1
	movw	r3, #65535
	ldrh	r10, [r0, #34]
	cmp	r10, r3
	bne	.L3821
	movw	r2, #1540
	ldr	r1, .L3867+4
	ldr	r0, .L3867+8
	bl	printk
	bl	dump_stack
.L3821:
	movw	r2, #2824
	ldr	r8, .L3867+24
	ldrh	r0, [r4, r2]
	ldr	r2, [r4, #1084]
	sub	r1, r8, #3072
	sub	fp, r8, #3088
	ldrh	r6, [fp, #-8]
	ldrh	r3, [r1, #-2]
	add	r2, r2, r0, lsl #2
	str	r1, [sp, #4]
	sub	r6, r6, #1
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	uxth	r6, r6
	mul	r3, r10, r3
	and	r2, r2, #224
	cmp	r2, #160
	movne	r9, #1
	bne	.L3822
	ldrb	r2, [r8, #-3128]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r9, r2
	orr	r3, r3, r2, lsl #24
	subeq	r2, r8, #3104
	ldrheq	r6, [r2, #-14]
	ldrb	r2, [r4, #1158]	@ zero_extendqisi2
	subeq	r6, r6, #1
	uxtheq	r6, r6
	cmp	r2, #0
	bne	.L3824
	ldrb	r2, [r4, #1159]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3822
.L3824:
	ldr	r2, .L3867+28
	ldrh	r6, [r2, #-14]
	sub	r6, r6, #1
	uxth	r6, r6
.L3822:
	orr	r6, r6, r3
	strb	r9, [r4, #2830]
	mov	r3, #0
	mov	r0, #1
	strh	r3, [r7, #2]	@ movhi
	strb	r3, [r4, #2828]
	strh	r3, [r7, #22]	@ movhi
	strh	r3, [r7, #24]	@ movhi
	str	r3, [sp]
	bl	buf_alloc
	mov	r1, #1
	str	r6, [r0, #24]
	mov	r5, r0
	bl	sblk_read_page
	ldr	r3, [sp]
	strh	r3, [r7, #20]	@ movhi
	ldr	r3, [r5, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L3825
.L3865:
	mov	r0, r5
	bl	zbuf_free
.L3863:
	mvn	r0, #0
	b	.L3812
.L3825:
	ldr	r1, [r5, #12]
	ldr	r3, .L3867+32
	ldr	r2, [r1]
	str	r3, [sp, #8]
	cmp	r2, r3
	beq	.L3826
	movw	r2, #1578
.L3866:
	ldr	r1, .L3867+4
	ldr	r0, .L3867+8
	bl	printk
	bl	dump_stack
	b	.L3865
.L3826:
	ldrb	r2, [r8, #-3127]	@ zero_extendqisi2
	ldrh	r3, [fp, #-8]
	smulbb	r3, r3, r2
	smulbb	r3, r3, r9
	uxth	r3, r3
	str	r3, [sp]
	ldrb	r3, [r8, #-11]	@ zero_extendqisi2
	cmp	r3, #2
	cmpeq	r9, #3
	bne	.L3827
	ldr	r3, .L3867+28
	ldrh	r0, [r8, #-14]
	ldr	r1, [r5, #4]
	ldrh	r9, [r3, #-14]
	ldr	r3, [r8, #-3132]
	lsl	r9, r9, #2
	smulbb	r9, r9, r2
	sub	r9, r9, r0
	lsr	r0, r0, #2
	uxth	r9, r9
	add	r0, r3, r0, lsl #2
	mov	r2, r9
	bl	ftl_memcpy
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3828
	mov	r1, r9
	ldr	r0, [r5, #4]
	str	r3, [sp, #12]
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3828
	mov	r0, r5
	bl	zbuf_free
	ldr	r1, [r5, #12]
	mov	r3, r9
.L3864:
	ldr	r2, [r5, #24]
	ldr	r1, [r1, #4]
	ldr	r0, .L3867+36
	bl	printk
	b	.L3863
.L3828:
	ldrb	r3, [r4, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3829
	ldrb	r3, [r4, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3830
.L3829:
	sub	r6, r6, #1
	str	r6, [r5, #24]
.L3831:
	mov	r1, #1
	mov	r0, r5
	bl	sblk_read_page
	ldr	r3, [r5, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3865
	ldr	r3, [r5, #12]
	ldr	r2, [sp, #8]
	ldr	r3, [r3]
	cmp	r3, r2
	movwne	r2, #1619
	ldrheq	r2, [r8, #-14]
	bne	.L3866
.L3862:
	ldr	r1, [r5, #4]
	mov	r9, #0
	ldr	r0, [r8, #-3132]
	mov	r10, #1
	bl	ftl_memcpy
	ldr	r6, [r8, #-3132]
	sub	r6, r6, #4
.L3836:
	ldr	r3, [sp]
	cmp	r9, r3
	blt	.L3841
	mov	r0, r5
	bl	zbuf_free
	movw	r3, #2824
	ldr	r2, [r4, #1092]
	ldrh	r1, [r4, r3]
	lsl	r3, r1, #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r7, #20]
	cmp	r2, r3
	beq	.L3842
	ldr	r0, .L3867+40
	bl	printk
.L3842:
	movw	r3, #2824
	ldrh	r1, [r7, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r7, #24]	@ movhi
	ldrh	r0, [r7, #20]
	b	.L3812
.L3830:
	ldr	r2, [sp, #4]
	ldrh	r3, [fp, #-8]
	ldrh	r2, [r2, #-2]
	sub	r3, r3, #1
	uxth	r3, r3
	mul	r10, r10, r2
	orr	r3, r3, #33554432
	orr	r10, r3, r10
	str	r10, [r5, #24]
	b	.L3831
.L3827:
	ldr	r3, [sp]
	ldr	r9, [r1, #4]
	ldr	r0, [r5, #4]
	lsl	r6, r3, #2
	mov	r1, r6
	bl	js_hash
	cmp	r9, r0
	moveq	r2, r6
	beq	.L3862
	mov	r0, r5
	bl	zbuf_free
	ldr	r1, [r5, #12]
	mov	r3, r6
	b	.L3864
.L3841:
	ldr	r0, [r6, #4]!
	cmn	r0, #1
	beq	.L3838
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3839
	mov	r2, #0
	add	r1, sp, #20
	ldr	r0, [r6]
	bl	pm_log2phys
.L3839:
	ldr	r3, .L3867+44
	ldr	fp, [sp, #20]
	ldrb	r1, [r8, #-3136]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	sub	r3, r3, r0
	lsl	r3, r10, r3
	sub	r3, r3, #1
	and	r0, r3, fp, lsr r0
	bl	__aeabi_uidiv
	ldrh	r3, [r7]
	cmp	r0, r3
	ldreq	r3, [r8, #-3132]
	ldrheq	r2, [r7, #20]
	streq	fp, [r3, r2, lsl #2]
	ldrheq	r3, [r7, #20]
	addeq	r3, r3, #1
	strheq	r3, [r7, #20]	@ movhi
.L3838:
	bl	timer_get_time
	add	r9, r9, #1
	b	.L3836
.L3868:
	.align	2
.L3867:
	.word	.LANCHOR0
	.word	.LANCHOR1+2395
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC252
	.word	.LANCHOR0+2824
	.word	.LANCHOR3
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LC253
	.word	.LC254
	.word	.LANCHOR3-3138
	.fnend
	.size	gc_scan_src_blk, .-gc_scan_src_blk
	.align	2
	.global	gc_scan_static_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_scan_static_data, %function
gc_scan_static_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3886
	ldr	r3, [r4, #1096]
	ldr	r2, [r3, #544]
	cmn	r2, #1
	beq	.L3870
	ldr	r6, .L3886+4
	mov	r7, #0
	ldr	r9, .L3886+8
	sub	r10, r6, #3136
.L3878:
	ldr	r3, [r4, #1096]
	mov	r2, #0
	add	r1, sp, #4
	uxth	r8, r7
	ldr	r0, [r3, #544]
	bl	pm_log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3871
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [sp, #4]
	mov	r5, r0
	mov	r1, #1
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r5, #36]
	cmp	r3, #256
	bne	.L3872
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mov	fp, #1
	ldrh	r2, [r10, #-2]
	ldr	r0, [sp, #4]
	rsb	r3, r3, #24
	ldrb	r1, [r6, #-3136]	@ zero_extendqisi2
	sub	r3, r3, r2
	lsl	r3, fp, r3
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	bl	__aeabi_uidiv
	mov	r2, #0
	mov	r1, fp
	uxth	r0, r0
	bl	gc_add_sblk
.L3872:
	ldr	r2, [r5, #12]
	ldr	r3, [r4, #1096]
	ldr	r2, [r2, #4]
	ldr	r3, [r3, #544]
	cmp	r2, r3
	beq	.L3873
	movw	r2, #2163
	mov	r1, r9
	ldr	r0, .L3886+12
	bl	printk
	bl	dump_stack
.L3873:
	mov	r0, r5
	bl	zbuf_free
.L3871:
	ldr	r3, [r4, #1096]
	ldr	r1, [r4, #2780]
	ldr	r2, [r3, #544]
	add	r2, r2, #1
	cmp	r2, r1
	str	r2, [r3, #544]
	bcc	.L3874
	mvn	r2, #0
	str	r2, [r3, #544]
	ldr	r2, [r3, #548]
	add	r2, r2, #1
	str	r2, [r3, #548]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
.L3869:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3874:
	ldr	r3, [sp, #4]
	cmn	r3, #1
	bne	.L3869
	ldrh	r3, [r6, #-14]
	add	r7, r7, #1
	cmp	r8, r3, lsr #2
	bcc	.L3878
	b	.L3869
.L3870:
	ldr	r2, [r3, #536]
	ldr	r1, [r3, #12]
	add	r2, r2, #12910592
	add	r2, r2, #49408
	cmp	r1, r2
	bhi	.L3880
	ldr	r0, [r4, #2800]
	ldr	r2, [r3, #540]
	ldr	r0, [r0, #44]
	add	r2, r2, #4992
	add	r2, r2, #8
	cmp	r0, r2
	bls	.L3869
.L3880:
	ldr	r2, [r4, #2800]
	ldr	r2, [r2, #44]
	str	r1, [r3, #536]
	str	r2, [r3, #540]
	mov	r2, #0
	str	r2, [r3, #544]
	b	.L3869
.L3887:
	.align	2
.L3886:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2411
	.word	.LC0
	.fnend
	.size	gc_scan_static_data, .-gc_scan_static_data
	.align	2
	.global	gc_block_vpn_scan
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_block_vpn_scan, %function
gc_block_vpn_scan:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r5, #1080
	ldr	r4, .L3922
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r4, #1096]
	ldrh	r3, [r4, r5]
	ldr	r2, [r2, #608]
	cmp	r2, r3
	bcs	.L3888
	bl	timer_get_time
	ldr	r6, [r4, #1096]
	ldr	r3, [r6, #604]
	add	r3, r3, #29952
	add	r3, r3, #48
	cmp	r0, r3
	bls	.L3888
	bl	timer_get_time
	ldr	r3, [r4, #1096]
	str	r0, [r6, #604]
	ldrh	r1, [r4, r5]
	ldr	r2, [r3, #600]
	cmp	r2, r1
	bcs	.L3890
	ldr	r1, [r4, #2800]
	ldrh	r1, [r1, #134]
	cmp	r2, r1
	bcs	.L3891
.L3890:
	ldr	r2, [r4, #2800]
	ldrh	r2, [r2, #134]
	str	r2, [r3, #600]
.L3891:
	ldr	r5, [r3, #600]
	movw	r3, #65535
	uxth	r7, r5
	cmp	r7, r3
	bne	.L3892
	movw	r2, #2504
	ldr	r1, .L3922+4
	ldr	r0, .L3922+8
	bl	printk
	bl	dump_stack
.L3892:
	ldr	r3, [r4, #1096]
	add	r1, sp, #24
	mov	r0, r7
	ldr	r2, [r3, #600]
	add	r2, r2, #1
	str	r2, [r3, #600]
	ldr	r2, [r3, #608]
	add	r2, r2, #1
	str	r2, [r3, #608]
	bl	ftl_get_blk_list_in_sblk
	uxth	r3, r0
	cmp	r3, #0
	beq	.L3888
	ldr	r10, [r4, #1084]
	uxth	r5, r5
	add	r10, r10, r5, lsl #2
	ldrb	r3, [r10, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	and	r3, r3, #192
	cmp	r3, #0
	cmpne	r2, #224
	beq	.L3893
	ldr	r3, [r4, #1096]
	ldrh	r1, [r3, #16]
	cmp	r1, r7
	beq	.L3893
	ldrh	r1, [r3, #48]
	cmp	r1, r7
	beq	.L3893
	ldrh	r3, [r3, #80]
	cmp	r3, r7
	bne	.L3894
.L3893:
	cmp	r2, #0
	bne	.L3888
	ldr	r3, [r4, #1092]
	lsl	r5, r5, #1
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L3895
	movw	r2, #2521
	ldr	r1, .L3922+4
	ldr	r0, .L3922+8
	bl	printk
	bl	dump_stack
.L3895:
	ldr	r3, [r4, #1092]
	mov	r2, #0
	strh	r2, [r3, r5]	@ movhi
.L3888:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3894:
	uxth	r0, r0
	add	r3, sp, #40
	add	r0, r3, r0, lsl #1
	movw	r3, #65535
	ldrh	r8, [r0, #-18]
	cmp	r8, r3
	bne	.L3897
	movw	r2, #2529
	ldr	r1, .L3922+4
	ldr	r0, .L3922+8
	bl	printk
	bl	dump_stack
.L3897:
	ldr	r6, .L3922+12
	sub	fp, r6, #3088
	ldrh	r2, [fp, #-8]
	sub	r2, r2, #1
	uxth	r3, r2
	sub	r2, r6, #3072
	ldrh	r1, [r2, #-2]
	ldrb	r2, [r10, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	mul	r1, r8, r1
	cmp	r2, #160
	sub	r2, r6, #3104
	movne	r8, #1
	bne	.L3898
	ldrb	r8, [r6, #-3128]	@ zero_extendqisi2
	cmp	r8, #2
	ldrheq	r3, [r2, #-14]
	orr	r1, r1, r8, lsl #24
	uxthne	r8, r8
	subeq	r3, r3, #1
	uxtheq	r3, r3
.L3898:
	orr	r3, r3, r1
	str	r2, [sp, #12]
	mov	r0, #1
	str	r3, [sp, #8]
	bl	buf_alloc
	ldr	r3, [sp, #8]
	mov	r1, #1
	mov	r9, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r2, [sp, #12]
	mov	r1, #255
	ldr	r0, [r6, #-3132]
	ldrh	r3, [r2, #-14]
	ldrb	r2, [r6, #-3127]	@ zero_extendqisi2
	mul	r2, r2, r3
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r9, #36]
	cmn	r2, #1
	cmpne	r2, #512
	moveq	r2, #1
	movne	r2, #0
	moveq	r2, #0
	beq	.L3921
	ldr	r3, [r9, #12]
	ldr	r1, [r3]
	ldr	r3, .L3922+16
	cmp	r1, r3
	beq	.L3901
.L3921:
	mov	r1, #1
	mov	r0, r7
	bl	gc_add_sblk
	mov	r0, r9
	bl	zbuf_free
	b	.L3888
.L3901:
	ldrh	r3, [fp, #-8]
	mov	fp, r2
	ldrb	r1, [r6, #-3127]	@ zero_extendqisi2
	smulbb	r1, r1, r3
	smulbb	r8, r1, r8
	uxth	r3, r8
	mov	r8, r2
	str	r3, [sp, #12]
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3902:
	ldr	r3, [sp, #12]
	cmp	fp, r3
	blt	.L3905
	mov	r0, r9
	lsl	r6, r5, #1
	bl	zbuf_free
	ldr	r3, .L3922+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3906
	ldr	r3, [r4, #1092]
	mov	r1, r5
	ldr	r0, .L3922+24
	ldrh	r2, [r3, r6]
	ldrb	r3, [r10, #2]	@ zero_extendqisi2
	lsr	r3, r3, #5
	str	r3, [sp]
	mov	r3, r8
	bl	printk
.L3906:
	ldr	r3, [r4, #1092]
	cmp	r8, #31
	strh	r8, [r3, r6]	@ movhi
	bhi	.L3888
	mov	r2, #0
	mov	r1, #1
	mov	r0, r7
	bl	gc_add_sblk
	b	.L3888
.L3905:
	ldr	r3, [sp, #8]
	ldr	r0, [r3, fp, lsl #2]
	cmn	r0, #1
	beq	.L3903
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3904
	ldr	r3, [sp, #8]
	mov	r2, #0
	add	r1, sp, #20
	ldr	r0, [r3, fp, lsl #2]
	bl	pm_log2phys
.L3904:
	ldr	r2, .L3922+28
	mov	r3, #1
	ldr	r0, [sp, #20]
	ldrb	r1, [r6, #-3136]	@ zero_extendqisi2
	ldrh	ip, [r2]
	ldrb	r2, [r4, #1153]	@ zero_extendqisi2
	rsb	r2, r2, #24
	sub	r2, r2, ip
	lsl	r2, r3, r2
	sub	r2, r2, #1
	and	r0, r2, r0, lsr ip
	bl	__aeabi_uidiv
	cmp	r5, r0
	addeq	r8, r8, #1
	uxtheq	r8, r8
.L3903:
	add	fp, fp, #1
	b	.L3902
.L3923:
	.align	2
.L3922:
	.word	.LANCHOR0
	.word	.LANCHOR1+2431
	.word	.LC0
	.word	.LANCHOR3
	.word	-178307901
	.word	.LANCHOR2
	.word	.LC255
	.word	.LANCHOR3-3138
	.fnend
	.size	gc_block_vpn_scan, .-gc_block_vpn_scan
	.align	2
	.global	ftl_sblk_dump
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sblk_dump, %function
ftl_sblk_dump:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	r3, r0, #2
	ldr	r4, .L3961
	.pad #124
	sub	sp, sp, #124
	lsl	r2, r0, #2
	mov	r7, r0
	str	r3, [sp, #68]
	ldr	r3, [r4, #1084]
	str	r1, [sp, #60]
	ldr	r1, [r3, r0, lsl #2]
	add	r2, r3, r2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #11, #8
	str	r1, [sp, #4]
	lsl	r1, r0, #2
	ldrh	r3, [r3, r1]
	mov	r1, r0
	ldr	r0, .L3961+4
	ubfx	r3, r3, #0, #11
	str	r3, [sp]
	ubfx	r3, r2, #3, #2
	lsr	r2, r2, #5
	bl	printk
	movw	r3, #65535
	cmp	r7, r3
	beq	.L3947
	movw	r3, #1080
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bls	.L3947
	ldr	r3, [r4, #1084]
	lsl	r2, r7, #2
	add	r1, sp, #104
	strh	r7, [sp, #88]	@ movhi
	mov	r0, r7
	mov	r5, #0
	add	r3, r3, r2
	mov	fp, r5
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	ldreq	r3, .L3961+8
	movne	r9, #1
	ldrbeq	r9, [r3, #-3128]	@ zero_extendqisi2
	bl	ftl_get_blk_list_in_sblk
	ldr	r3, .L3961+12
	uxtb	r0, r0
	lsl	r2, r7, #2
	strh	r5, [sp, #90]	@ movhi
	strb	r0, [sp, #97]
	mov	r1, r7
	ldrh	r3, [r3, #-8]
	strb	r5, [sp, #93]
	strh	r5, [sp, #98]	@ movhi
	smulbb	r3, r3, r0
	strh	r3, [sp, #94]	@ movhi
	ldr	r3, [r4, #1084]
	add	r3, r3, r2
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	mov	r3, r9
	str	r0, [sp]
	ldr	r0, .L3961+16
	lsr	r2, r2, #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	r6, r0
	str	r5, [sp, #44]
	str	r5, [sp, #52]
.L3927:
	ldr	r3, .L3961+20
	ldrh	r2, [r3]
	uxth	r3, r5
	cmp	r2, r3
	bls	.L3944
	str	r3, [sp, #72]
	lsl	r3, r3, #1
	sub	r2, r3, #1
	str	r2, [sp, #76]
	uxth	r2, r5
	add	r3, r3, r2
	sub	r3, r3, #1
	str	r3, [sp, #64]
	mov	r3, #0
	b	.L3960
.L3941:
	ldrh	r3, [sp, #48]
	add	r2, sp, #120
	add	r3, r2, r3, lsl #1
	ldrh	r3, [r3, #-16]
	str	r3, [sp, #56]
	movw	r3, #65535
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L3928
	ldr	r3, .L3961+24
	cmp	r9, #3
	ldrh	r4, [r3, #-2]
	mul	r3, r2, r4
	bne	.L3929
	ldr	r2, .L3961
	ldrb	r1, [r2, #1158]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r2, [sp, #64]
	bne	.L3958
	ldrb	r2, [r2, #1159]	@ zero_extendqisi2
	lsl	r4, r8, #24
	cmp	r2, #0
	ldrne	r2, [sp, #64]
	uxtaheq	r3, r3, r5
	addne	r3, r3, r2
	addne	r3, r3, r8
	orr	r4, r4, r3
.L3931:
	mov	r1, #1
	str	r4, [r6, #24]
	mov	r0, r6
	bl	sblk_read_page
	ldr	r2, [r6, #12]
	ldr	r3, [r6, #4]
	ldr	r10, [r6, #36]
	ldr	r1, [r2, #12]
	ldr	r0, .L3961+28
	str	r1, [sp, #32]
	ldr	r1, [r2, #8]
	str	r1, [sp, #28]
	ldr	r1, [r2, #4]
	str	r1, [sp, #24]
	ldr	r2, [r2]
	ldr	r1, [sp, #56]
	str	r2, [sp, #20]
	ldr	r2, [r3, #12]
	str	r2, [sp, #16]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	ldr	r3, [r3]
	ldr	r2, [sp, #72]
	str	r10, [sp]
	str	r3, [sp, #4]
	mov	r3, r4
	bl	printk
	ldr	r3, [sp, #52]
	cmn	r10, #1
	cmpne	r10, #512
	moveq	r3, #1
	ldr	r0, .L3961+32
	str	r3, [sp, #52]
	ldr	r3, .L3961+36
	ldr	r3, [r3, #4]
	blx	r3
	ldr	r3, .L3961
	ldr	r2, [sp, #68]
	ldr	r3, [r3, #1084]
	add	r3, r3, r2
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #32
	cmpne	r2, #224
	moveq	r10, #1
	movne	r10, #0
	beq	.L3928
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #84]
	bne	.L3935
	ldr	r3, [r6, #12]
	mov	r2, r10
	add	r1, sp, #84
	ldr	r0, [r3, #4]
	bl	pm_log2phys
.L3935:
	ldr	r3, [sp, #84]
	cmp	r4, r3
	bne	.L3936
	ldr	r3, [sp, #44]
	mov	r1, r4
	ldr	r2, [r6, #12]
	ldr	r0, .L3961+40
	add	r3, r3, #1
	ldr	r2, [r2, #4]
	str	r3, [sp, #44]
	bl	printk
.L3936:
	ldr	r3, [sp, #60]
	cmp	r3, #0
	beq	.L3938
	ldr	r2, [r3, fp, lsl #2]
	lsl	r4, fp, #2
	ldr	r3, [r6, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	beq	.L3939
	ldr	r3, .L3961+44
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3939
	mov	r1, fp
	ldr	r0, .L3961+48
	bl	printk
.L3939:
	ldr	r3, [sp, #60]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r4]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	cmnne	r3, #1
	beq	.L3938
	movw	r2, #1575
	ldr	r1, .L3961+52
	ldr	r0, .L3961+56
	bl	printk
	bl	dump_stack
.L3938:
	add	fp, fp, #1
.L3928:
	add	r8, r8, #1
	uxth	r8, r8
.L3943:
	cmp	r9, r8
	bcs	.L3941
	ldr	r3, [sp, #48]
	add	r3, r3, #1
.L3960:
	str	r3, [sp, #48]
	ldrb	r2, [sp, #97]	@ zero_extendqisi2
	ldrh	r3, [sp, #48]
	cmp	r2, r3
	bls	.L3942
	mov	r8, #1
	b	.L3943
.L3929:
	cmp	r9, #2
	uxtahne	r4, r3, r5
	bne	.L3931
	ldr	r2, [sp, #76]
.L3958:
	add	r4, r3, r2
	ldr	r2, .L3961+8
	add	r3, r4, r8
	ldrb	r4, [r2, #-3128]	@ zero_extendqisi2
	orr	r4, r3, r4, lsl #24
	b	.L3931
.L3942:
	add	r5, r5, #1
	b	.L3927
.L3944:
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, .L3961
	lsl	r2, r7, #1
	ldr	r0, .L3961+60
	ldr	r1, [r3, #1092]
	ldr	r3, [sp, #44]
	ldrh	r2, [r1, r2]
	mov	r1, r7
	bl	printk
	ldr	r0, [sp, #52]
.L3924:
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3947:
	mov	r0, #0
	b	.L3924
.L3962:
	.align	2
.L3961:
	.word	.LANCHOR0
	.word	.LC256
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC257
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3072
	.word	.LC195
	.word	644245000
	.word	arm_delay_ops
	.word	.LC258
	.word	.LANCHOR2
	.word	.LC259
	.word	.LANCHOR1+2449
	.word	.LC0
	.word	.LC260
	.fnend
	.size	ftl_sblk_dump, .-ftl_sblk_dump
	.align	2
	.global	zftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_read, %function
zftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r3, .L4026
	.pad #76
	sub	sp, sp, #76
	mov	r4, r0
	mov	r6, r1
	str	r2, [sp, #24]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3964
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	ldr	r0, .L4026+4
	bl	printk
.L3964:
	cmp	r4, #0
	bne	.L3965
	ldr	r3, .L4026+8
	mov	r4, #24576
	ldr	r3, [r3, #1032]
.L3966:
	ldr	r2, [sp, #24]
	cmp	r3, r2
	cmpcs	r3, r6
	movls	r5, #1
	movhi	r5, #0
	bls	.L3995
	add	r2, r6, r2
	cmp	r3, r2
	bcc	.L3995
	add	r3, r4, r6
	ldr	r4, .L4026+8
	str	r3, [sp, #28]
	ldr	r1, [sp, #24]
	ldr	r2, [r4, #2800]
	ldr	r0, [sp, #28]
	ldr	r3, [r2, #24]
	add	r3, r3, r1
	str	r3, [r2, #24]
	ldr	r3, .L4026+12
	ldrb	r6, [r3, #-2546]	@ zero_extendqisi2
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #24]
	mov	r1, r6
	ldr	r2, [sp, #28]
	str	r0, [sp, #36]
	add	r3, r3, r2
	sub	r0, r3, #1
	str	r3, [sp, #44]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	str	r5, [sp, #40]
	ldr	r6, [sp, #36]
	ldr	r5, .L4026+12
	rsb	r3, r3, #1
	add	r3, r3, r0
	str	r0, [sp, #48]
	str	r3, [sp, #32]
.L3968:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	bne	.L3992
	bl	timer_get_time
	str	r0, [r5, #-8]
	ldr	r0, [sp, #40]
.L3963:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3965:
	cmp	r4, #3
	bhi	.L3995
	lsl	r4, r4, #13
	mov	r3, #8192
	b	.L3966
.L3992:
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #48]
	ldrb	r1, [r5, #-2546]	@ zero_extendqisi2
	cmp	r6, r2
	cmpne	r6, r3
	moveq	r10, #1
	movne	r10, #0
	uxth	r8, r1
	bne	.L3969
	cmp	r6, r3
	bne	.L3970
	ldr	r0, [sp, #28]
	bl	__aeabi_uidivmod
	uxth	r10, r1
	ldr	r3, [sp, #24]
	sub	r8, r8, r10
	uxth	r8, r8
	cmp	r3, r8
	ldrhcc	r8, [sp, #24]
.L3969:
	ldr	r2, .L4026+16
	mov	r3, #0
	mov	r9, r2
.L3973:
	ldr	r1, [r2, #20]
	cmp	r6, r1
	bne	.L3971
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L3971
	mov	r2, #48
	lsl	r8, r8, #9
	mov	r0, r7
	mla	r3, r2, r3, r4
	add	r7, r7, r8
	mov	r2, r8
	ldr	r1, [r3, #1236]
	add	r1, r1, r10, lsl #9
	bl	ftl_memcpy
.L3972:
	ldr	r3, [sp, #32]
	add	r6, r6, #1
	sub	r3, r3, #1
	str	r3, [sp, #32]
.L3979:
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	cmp	r2, #0
	cmpne	r3, #2
	bhi	.L3968
	ldrb	r1, [r5, #-2535]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3968
	ldrb	r0, [r5, #-2536]	@ zero_extendqisi2
	add	r0, r0, r0, lsl #1
	add	r0, r9, r0, lsl #4
	bl	sblk_read_page
.L3981:
	ldrb	r3, [r5, #-2535]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3991
	mvn	r2, #0
	strb	r3, [r5, #-2535]
	strb	r2, [r5, #-2536]
	b	.L3968
.L3970:
	ldr	r3, [sp, #28]
	mov	r10, #0
	ldr	r2, [sp, #24]
	add	r8, r3, r2
	mls	r1, r6, r1, r8
	uxtb	r8, r1
	b	.L3969
.L3971:
	add	r3, r3, #1
	add	r2, r2, #48
	cmp	r3, #32
	bne	.L3973
	mov	r0, r6
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #68]
	bne	.L3974
	mov	r2, #0
	add	r1, sp, #68
	mov	r0, r6
	bl	pm_log2phys
.L3974:
	ldr	r3, [sp, #68]
	cmn	r3, #1
	moveq	r8, #0
	beq	.L3976
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L3979
	ldr	r1, [r4, #2800]
	ldr	r2, [r1, #40]
	add	r2, r2, #1
	str	r2, [r1, #40]
	ldr	r2, [sp, #68]
	str	r7, [r3, #8]
	add	r7, r7, r8, lsl #9
	str	r6, [r3, #20]
	str	r2, [r3, #24]
	str	r2, [r3, #28]
	strb	r8, [r3, #40]
	strb	r10, [r3, #41]
	bl	zftl_add_read_buf
	b	.L3972
.L3978:
	mla	r3, r3, r6, r8
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #44]
	cmp	r2, r3
	movls	r2, #1
	movhi	r2, #0
	cmp	r1, r3
	movls	r2, #0
	cmp	r2, #0
	beq	.L3977
	mov	r0, r7
	add	r7, r7, #512
	mov	r2, #512
	mov	r1, #0
	bl	ftl_memset
.L3977:
	add	r8, r8, #1
.L3976:
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	cmp	r8, r3
	bcc	.L3978
	b	.L3972
.L3991:
	ldrb	r3, [r5, #-2536]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L3982
	movw	r2, #1284
	ldr	r1, .L4026+20
	ldr	r0, .L4026+24
	bl	printk
	bl	dump_stack
.L3982:
	ldr	r3, .L4026+12
	ldr	r1, .L4026+12
	ldrb	r9, [r3, #-2536]	@ zero_extendqisi2
	ldr	r3, .L4026+16
	add	r2, r9, r9, lsl #1
	add	r3, r3, r2, lsl #4
	mov	r2, #48
	mla	r2, r2, r9, r4
	str	r3, [sp, #52]
	ldr	r10, [r2, #1268]
	ldrb	r3, [r2, #1232]	@ zero_extendqisi2
	cmn	r10, #1
	strb	r3, [r1, #-2536]
	bne	.L3983
	ldr	r2, [r4, #1096]
	str	r10, [sp, #40]
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
.L3984:
	mov	r1, #48
	cmn	r10, #1
	mla	r1, r1, r9, r4
	movne	r2, #0
	moveq	r2, #1
	ldr	r3, [r1, #1244]
	ldr	r0, [r1, #1252]
	ldr	ip, [r3, #4]
	cmp	ip, r0
	orrne	r2, r2, #1
	cmp	r2, #0
	beq	.L3985
	ldrb	r0, [r4, #1153]	@ zero_extendqisi2
	mvn	ip, #0
	ldr	lr, [r1, #1256]
	ldrb	fp, [r1, #1272]	@ zero_extendqisi2
	ldrb	r8, [r5, #-2546]	@ zero_extendqisi2
	mvn	ip, ip, lsl r0
	ldr	r10, [r1, #1236]
	ldr	r2, [r1, #1240]
	rsb	r1, r0, #24
	and	r0, ip, lr, lsr r1
	cmp	fp, r8
	movcc	r2, r10
	str	r8, [sp]
	bic	r1, lr, ip, lsl r1
	uxtb	r0, r0
	bl	flash_read_page_en
	mov	r10, r0
.L3985:
	mov	r8, #48
	cmn	r10, #1
	mla	r8, r8, r9, r4
	movne	fp, #0
	moveq	fp, #1
	ldr	r3, [r8, #1244]
	ldr	r2, [r8, #1252]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	moveq	r3, fp
	orrne	r3, fp, #1
	cmp	r3, #0
	beq	.L3987
	ldr	r2, [r4, #1096]
	ldr	r0, .L4026+28
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r8, #1244]
	ldrb	r1, [r8, #1233]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	str	r2, [sp, #16]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	mov	r2, r10
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [r8, #1256]
	str	r3, [sp]
	ldr	r3, [r8, #1252]
	bl	printk
	ldr	r3, .L4026+32
	ldr	r2, [r8, #1256]
	ldrb	r1, [r5, #-3136]	@ zero_extendqisi2
	ldrh	r0, [r3, #-2]
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	lsr	r2, r2, r0
	rsb	r3, r3, #24
	sub	r3, r3, r0
	mvn	r0, #0
	bic	r0, r2, r0, lsl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
.L3987:
	mov	r3, #48
	mla	r3, r3, r9, r4
	ldr	r2, [r3, #1244]
	ldr	r3, [r3, #1252]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	orrne	fp, fp, #1
	cmp	fp, #0
	beq	.L3988
	movw	r2, #1320
	ldr	r1, .L4026+20
	ldr	r0, .L4026+24
	bl	printk
	bl	dump_stack
.L3988:
	mov	r3, #48
	mla	r9, r3, r9, r4
	ldrb	r3, [r5, #-2546]	@ zero_extendqisi2
	ldrb	r2, [r9, #1272]	@ zero_extendqisi2
	cmp	r3, r2
	ldrbls	r3, [r9, #1234]	@ zero_extendqisi2
	bicls	r3, r3, #8
	strbls	r3, [r9, #1234]
	bls	.L3990
	ldrb	r1, [r9, #1273]	@ zero_extendqisi2
	lsl	r2, r2, #9
	ldr	r3, [r9, #1236]
	ldr	r0, [r9, #1240]
	add	r1, r3, r1, lsl #9
	bl	ftl_memcpy
.L3990:
	ldr	r1, [sp, #52]
	ldr	r0, .L4026+36
	bl	buf_remove_buf
	ldr	r0, [sp, #52]
	bl	zbuf_free
	ldrb	r3, [r5, #-2535]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r5, #-2535]
	b	.L3981
.L3983:
	cmp	r10, #256
	bne	.L3984
	ldr	r1, .L4026+40
	mov	fp, #1
	ldrb	r0, [r4, #1153]	@ zero_extendqisi2
	ldr	r3, [r2, #1256]
	ldrh	ip, [r1]
	rsb	r0, r0, #24
	str	r2, [sp, #60]
	ldr	r2, .L4026+12
	sub	r0, r0, ip
	str	r3, [sp, #56]
	lsl	r0, fp, r0
	ldrb	r1, [r2, #-3136]	@ zero_extendqisi2
	sub	r0, r0, #1
	and	r0, r0, r3, lsr ip
	bl	__aeabi_uidiv
	ldr	r2, [sp, #60]
	mov	r8, r0
	uxth	r1, r0
	ldr	r3, [sp, #56]
	ldr	r0, .L4026+44
	ldr	r2, [r2, #1252]
	bl	printk
	mov	r2, #0
	mov	r1, fp
	uxth	r0, r8
	bl	gc_add_sblk
	b	.L3984
.L3995:
	mvn	r0, #0
	b	.L3963
.L4027:
	.align	2
.L4026:
	.word	.LANCHOR2
	.word	.LC261
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1232
	.word	.LANCHOR1+2463
	.word	.LC0
	.word	.LC263
	.word	.LANCHOR3-3136
	.word	.LANCHOR0+2771
	.word	.LANCHOR3-3138
	.word	.LC262
	.fnend
	.size	zftl_read, .-zftl_read
	.align	2
	.global	zftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_vendor_read, %function
zftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	add	r1, r0, #512
	mov	r0, #2
	b	zftl_read
	.fnend
	.size	zftl_vendor_read, .-zftl_vendor_read
	.align	2
	.global	zftl_sys_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_sys_read, %function
zftl_sys_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #2
	b	zftl_read
	.fnend
	.size	zftl_sys_read, .-zftl_sys_read
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_debug_proc_write, %function
zftl_debug_proc_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r2, #79
	.pad #132
	sub	sp, sp, #132
	mvnhi	r0, #21
	add	r6, sp, #48
	str	r6, [sp, #44]
	bhi	.L4030
	mov	r0, r6
	mov	r4, r2
	bl	rk_copy_from_user
	cmp	r0, #0
	mvnne	r0, #13
	bne	.L4030
	add	r3, sp, #128
	mov	r1, r6
	add	r3, r3, r4
	strb	r0, [r3, #-80]
	ldr	r0, .L4056
	bl	printk
	mov	r3, #16
	mov	r2, #1
	mov	r1, r6
	ldr	r0, .L4056+4
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	r2, #7
	ldr	r1, .L4056+8
	mov	r0, r6
	bl	memcmp
	subs	r7, r0, #0
	bne	.L4032
	ldr	r5, .L4056+12
	movw	r3, #698
	mov	r2, #4
	ldr	r0, .L4056+16
	ldr	r6, .L4056+20
	movw	r8, #65535
	ldr	r1, [r5, #2800]
	ldrh	r3, [r1, r3]
	add	r1, r1, #704
	bl	rknand_print_hex
	ldr	r1, [r5, #2800]
	mov	r2, #2
	ldrh	r3, [r6, #-176]
	ldr	r0, .L4056+24
	add	r1, r1, #416
	bl	rknand_print_hex
.L4033:
	ldrh	r3, [r6, #-176]
	cmp	r7, r3
	blt	.L4035
.L4036:
	bl	rknand_device_unlock
	mov	r0, r4
.L4030:
	add	sp, sp, #132
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4035:
	mov	r0, #300
	bl	msleep
	ldr	r2, [r5, #2800]
	add	r3, r7, #208
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, r8
	beq	.L4034
	mov	r1, #0
	bl	ftl_sblk_dump
.L4034:
	add	r7, r7, #1
	b	.L4033
.L4032:
	mov	r2, #7
	ldr	r1, .L4056+28
	mov	r0, r6
	bl	memcmp
	subs	r5, r0, #0
	bne	.L4037
	ldr	r8, .L4056+12
	movw	r3, #698
	mov	r2, #4
	ldr	r0, .L4056+16
	ldr	r7, .L4056+20
	ldr	r1, [r8, #2800]
	ldr	r10, .L4056+32
	ldr	fp, .L4056+36
	ldrh	r3, [r1, r3]
	add	r1, r1, #704
	bl	rknand_print_hex
	ldr	r1, [r8, #2800]
	mov	r2, #2
	ldrh	r3, [r7, #-176]
	ldr	r0, .L4056+24
	add	r1, r1, #416
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
	ldr	r0, .L4056+40
	add	r6, sp, #128
	add	r1, sp, #55
	str	r1, [r6, #-84]!
	bl	printk
	mov	r1, r6
	ldr	r0, [sp, #44]
	bl	rk_simple_strtoull.constprop.33
	str	r8, [sp, #36]
	uxth	r3, r0
	ldr	r8, .L4056+44
	mov	r6, r0
	str	r0, [sp, #40]
	str	r3, [sp, #32]
.L4039:
	add	r9, r8, r5
	ldrh	r1, [r8, r5]
	ldrh	r2, [r9, #2]
	mov	r0, r10
	bl	printk
	ldrh	r3, [r5, r8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L4038
	ldrb	r3, [r7, #-2546]	@ zero_extendqisi2
	mov	r0, fp
	mov	r2, #4
	ldr	r1, [r9, #4]
	lsl	r3, r3, #7
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
.L4038:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L4039
	mov	r0, #300
	uxth	r6, r6
	bl	msleep
	mov	r0, #1
	add	r6, r6, #176
	bl	buf_alloc
	ldr	r3, [sp, #36]
	mov	r5, r0
	mov	r1, #1
	ldr	r3, [r3, #2800]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r0, #24]
	str	r3, [sp, #40]
	bl	sblk_read_page
	ldr	r2, [r5, #12]
	ldr	r3, [r5, #4]
	ldr	r0, .L4056+48
	ldr	r1, [r2, #12]
	str	r1, [sp, #24]
	ldr	r1, [r2, #8]
	str	r1, [sp, #20]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	ldr	r1, [sp, #40]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r5, #36]
	bl	printk
	ldrb	r3, [r7, #-2546]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r1, [r5, #4]
	ldr	r0, .L4056+52
	lsl	r3, r3, #7
.L4055:
	bl	rknand_print_hex
	mov	r0, r5
	bl	zbuf_free
	b	.L4036
.L4037:
	mov	r2, #7
	ldr	r1, .L4056+56
	mov	r0, r6
	bl	memcmp
	cmp	r0, #0
	bne	.L4040
	bl	dump_ftl_info
	b	.L4036
.L4040:
	mov	r2, #9
	ldr	r1, .L4056+60
	mov	r0, r6
	bl	memcmp
	cmp	r0, #0
	bne	.L4041
	add	r1, sp, #128
	add	r0, sp, #57
	str	r0, [r1, #-84]!
	bl	rk_simple_strtoull.constprop.33
	ldr	r3, .L4056+64
	str	r0, [sp, #40]
	strh	r0, [r3, #-4]	@ movhi
	bl	dump_all_list_info
	b	.L4036
.L4041:
	mov	r2, #8
	ldr	r1, .L4056+68
	mov	r0, r6
	bl	memcmp
	cmp	r0, #0
	beq	.L4036
	mov	r2, #8
	ldr	r1, .L4056+72
	mov	r0, r6
	bl	memcmp
	cmp	r0, #0
	bne	.L4043
	add	r5, sp, #128
	add	r1, sp, #56
	ldr	r0, .L4056+40
	str	r1, [r5, #-84]!
	bl	printk
	mov	r1, r5
	ldr	r0, [sp, #44]
	bl	rk_simple_strtoull.constprop.33
	str	r0, [sp, #40]
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [sp, #40]
	mov	r5, r0
	mov	r1, #1
	ldr	r6, .L4056+20
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r2, [r5, #12]
	ldr	r3, [r5, #4]
	ldr	r0, .L4056+48
	ldr	r1, [r2, #12]
	str	r1, [sp, #24]
	ldr	r1, [r2, #8]
	str	r1, [sp, #20]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	ldr	r1, [sp, #40]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r5, #36]
	bl	printk
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r1, [r5, #4]
	ldr	r0, .L4056+76
	lsl	r3, r3, #7
	bl	rknand_print_hex
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r1, [r5, #12]
	ldr	r0, .L4056+80
	lsl	r3, r3, #1
	b	.L4055
.L4043:
	mov	r2, #8
	ldr	r1, .L4056+84
	mov	r0, r6
	bl	memcmp
	subs	r7, r0, #0
	bne	.L4044
	add	r5, sp, #128
	add	r1, sp, #56
	ldr	r0, .L4056+40
	str	r1, [r5, #-84]!
	bl	printk
	mov	r1, r5
	ldr	r0, [sp, #44]
	bl	rk_simple_strtoull.constprop.33
	mov	r1, r7
	str	r0, [sp, #40]
	uxth	r0, r0
	bl	ftl_sblk_dump
	b	.L4036
.L4044:
	mov	r2, #10
	ldr	r1, .L4056+88
	mov	r0, r6
	bl	memcmp
	cmp	r0, #0
	bne	.L4045
	add	r1, sp, #58
	add	r5, sp, #128
	ldr	r0, .L4056+40
	str	r1, [r5, #-84]!
	bl	printk
	mov	r1, r5
	ldr	r0, [sp, #44]
	bl	rk_simple_strtoull.constprop.33
	ldr	r3, .L4056+92
	str	r0, [sp, #40]
	str	r0, [r3]
	b	.L4036
.L4045:
	mov	r0, r6
	mov	r2, #8
	ldr	r1, .L4056+96
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4046
	add	r5, sp, #128
	add	r1, sp, #56
	ldr	r0, .L4056+40
	str	r1, [r5, #-84]!
	bl	printk
	mov	r1, r5
	ldr	r0, [sp, #44]
	bl	rk_simple_strtoull.constprop.33
	mov	r5, r0
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #40]
	bne	.L4047
	mov	r2, r6
	add	r1, sp, #40
	mov	r0, r5
	bl	pm_log2phys
.L4047:
	ldr	r2, [sp, #40]
	mov	r1, r5
	ldr	r0, .L4056+100
	bl	printk
	b	.L4036
.L4046:
	ldr	r0, .L4056+104
	bl	printk
	ldr	r0, .L4056+108
	bl	printk
	ldr	r0, .L4056+112
	bl	printk
	ldr	r0, .L4056+116
	bl	printk
	ldr	r0, .L4056+120
	bl	printk
	ldr	r0, .L4056+124
	bl	printk
	ldr	r0, .L4056+128
	bl	printk
	ldr	r0, .L4056+132
	bl	printk
	ldr	r0, .L4056+136
	bl	printk
	b	.L4036
.L4057:
	.align	2
.L4056:
	.word	.LC264
	.word	.LC265
	.word	.LC266
	.word	.LANCHOR0
	.word	.LC267
	.word	.LANCHOR3
	.word	.LC268
	.word	.LC269
	.word	.LC271
	.word	.LC272
	.word	.LC270
	.word	.LANCHOR3-2532
	.word	.LC245
	.word	.LC273
	.word	.LC274
	.word	.LC275
	.word	.LANCHOR3-3072
	.word	.LC276
	.word	.LC277
	.word	.LC218
	.word	.LC240
	.word	.LC278
	.word	.LC279
	.word	.LANCHOR2
	.word	.LC280
	.word	.LC281
	.word	.LC282
	.word	.LC283
	.word	.LC284
	.word	.LC285
	.word	.LC286
	.word	.LC287
	.word	.LC288
	.word	.LC289
	.word	.LC290
	.fnend
	.size	zftl_debug_proc_write, .-zftl_debug_proc_write
	.global	__aeabi_idivmod
	.align	2
	.global	ftl_update_l2p_map
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r6, .L4088
	.pad #28
	sub	sp, sp, #28
	ldrb	r8, [r0, #9]	@ zero_extendqisi2
	sub	r3, r6, #3088
	ldr	r4, [r6, #-2556]
	ldrh	r3, [r3, #-8]
	mul	r8, r8, r3
	ldrh	r3, [r0, #12]
	add	r4, r4, r3, lsl #2
	sub	r3, r8, #-1073741823
	ldr	r3, [r4, r3, lsl #2]
	cmn	r3, #1
	beq	.L4059
	movw	r2, #1998
	ldr	r1, .L4088+4
	ldr	r0, .L4088+8
	bl	printk
	bl	dump_stack
.L4059:
	mov	r5, #0
	sub	r4, r4, #4
	mov	r10, r5
.L4060:
	cmp	r10, r8
	bne	.L4066
	ldr	r3, .L4088+12
	ldr	r4, .L4088+16
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4067
	ldrh	r1, [r7]
	ldr	r2, [r4, #1092]
	ldr	r0, .L4088+20
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	mov	r2, r5
	bl	printk
.L4067:
	ldrh	r3, [r7]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4066:
	ldr	r9, [r4, #4]!
	cmn	r9, #1
	beq	.L4061
	ldrb	r1, [r6, #-2546]	@ zero_extendqisi2
	mov	r0, r9
	lsl	r1, r1, #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #4]
	ldr	r3, .L4088+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4062
	mov	r3, r10
	mov	r2, r9
	ldr	r1, [sp, #4]
	ldr	r0, .L4088+24
	bl	printk
.L4062:
	mov	r9, r4
	mov	fp, r10
.L4065:
	ldr	r0, [r9]
	cmn	r0, #1
	beq	.L4063
	ldrb	r1, [r6, #-2546]	@ zero_extendqisi2
	lsl	r1, r1, #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxth	r0, r0
	cmp	r3, r0
	bne	.L4063
	ldrb	r3, [r7, #9]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r3
	str	r3, [sp, #12]
	bl	__aeabi_idivmod
	ldr	r2, .L4088+28
	add	r1, r7, r1, lsl #1
	ldr	r3, [sp, #12]
	ldrh	r0, [r1, #16]
	ldrh	r2, [r2]
	mov	r1, r3
	mul	r2, r2, r0
	mov	r0, fp
	str	r2, [sp, #8]
	bl	__aeabi_idiv
	ldr	r2, [sp, #8]
	add	r2, r2, r0
	ldr	r0, .L4088+32
	str	r2, [sp, #20]
	ldr	r1, [r9]
	bl	pm_ppa_update_check
	cmp	r0, #0
	beq	.L4064
	mov	r3, r8
	mov	r2, #4
	ldr	r1, [r6, #-2556]
	ldr	r0, .L4088+36
	bl	rknand_print_hex
.L4064:
	add	r5, r5, #1
	mov	r2, #1
	uxth	r5, r5
	add	r1, sp, #20
	ldr	r0, [r9]
	bl	pm_log2phys
	mvn	r3, #0
	str	r3, [r9]
.L4063:
	add	fp, fp, #1
	add	r9, r9, #4
	cmp	r8, fp
	bne	.L4065
.L4061:
	add	r10, r10, #1
	b	.L4060
.L4089:
	.align	2
.L4088:
	.word	.LANCHOR3
	.word	.LANCHOR1+2473
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC294
	.word	.LC291
	.word	.LANCHOR3-3074
	.word	.LC292
	.word	.LC293
	.fnend
	.size	ftl_update_l2p_map, .-ftl_update_l2p_map
	.align	2
	.global	ftl_alloc_new_data_sblk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_alloc_new_data_sblk, %function
ftl_alloc_new_data_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r6, [r0]
	bl	ftl_update_l2p_map
	bl	pm_flush
	ldrh	r0, [r5]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4091
	bl	zftl_insert_data_list
.L4091:
	ldr	r4, .L4099
	mov	r0, r5
	ldr	r1, [r4, #1096]
	add	r1, r1, #16
	cmp	r5, r1
	movw	r5, #65535
	moveq	r1, #2
	movne	r1, #3
	bl	ftl_open_sblk_init
	ldr	r3, [r4, #1096]
	cmp	r6, r5
	ldr	r3, [r3, #560]
	sub	r3, r3, r6
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4093
	mov	r1, r6
	ldr	r0, .L4099+4
	bl	printk
	ldr	r3, [r4, #1096]
	ldr	r0, [r3, #564]
	bl	gc_mark_bad_ppa
	ldr	r3, [r4, #1096]
	mvn	r2, #0
	str	r5, [r3, #560]
	str	r2, [r3, #564]
.L4093:
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L4100:
	.align	2
.L4099:
	.word	.LANCHOR0
	.word	.LC295
	.fnend
	.size	ftl_alloc_new_data_sblk, .-ftl_alloc_new_data_sblk
	.align	2
	.global	ftl_write_commit
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_write_commit, %function
ftl_write_commit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r8, .L4167
	mov	r9, r8
.L4102:
	ldrb	r2, [r8, #2796]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4104
	ldrb	r3, [r8, #2820]	@ zero_extendqisi2
	sub	r2, r2, #1
	ldr	r1, .L4167+4
	strb	r2, [r8, #2796]
	str	r3, [sp, #12]
	add	r3, r3, r3, lsl #1
	add	r3, r1, r3, lsl #4
	ldr	r1, [sp, #12]
	str	r3, [sp, #20]
	mov	r3, #48
	mla	r3, r3, r1, r8
	ldrb	r1, [r3, #1232]	@ zero_extendqisi2
	ldr	r2, [r3, #1252]
	ldr	r3, [r8, #2780]
	strb	r1, [r8, #2820]
	cmp	r2, r3
	bcc	.L4106
	movw	r2, #607
	ldr	r1, .L4167+8
	ldr	r0, .L4167+12
	bl	printk
	bl	dump_stack
.L4106:
	ldr	r2, [sp, #12]
	mov	r3, #48
	mla	r3, r3, r2, r9
	ldr	r2, [r9, #2780]
	ldr	r7, [r3, #1252]
	cmp	r7, r2
	bcc	.L4107
	ldr	r0, [sp, #20]
	bl	zbuf_free
	mvn	r0, #0
.L4101:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4107:
	ldr	r2, [r3, #1244]
	ldr	fp, [r3, #1236]
	ldrb	r5, [r3, #1273]	@ zero_extendqisi2
	ldrb	r3, [r3, #1272]	@ zero_extendqisi2
	str	r2, [sp, #8]
	str	r3, [sp, #16]
	ldrb	r3, [r9, #2797]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4109
	ldr	r6, .L4167+16
	ldr	r3, .L4167+4
	ldrb	r4, [r6, #-88]	@ zero_extendqisi2
	add	r4, r4, r4, lsl #1
	add	r4, r3, r4, lsl #4
.L4110:
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L4111
	ldr	r3, [r4, #20]
	cmp	r7, r3
	bne	.L4109
	ldr	r3, [sp, #16]
	lsl	r5, r5, #9
	ldr	r0, [r4, #4]
	add	r1, fp, r5
	lsl	r2, r3, #9
	add	r0, r0, r5
	bl	ftl_memcpy
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	mov	r0, #2
	ldr	r2, [r4, #4]
	ldr	r1, [r4, #12]
	sub	r3, r3, #2
	add	r2, r2, r3, lsl #9
	add	r1, r1, #16
	bl	ftl_debug_info_fill
	ldr	r0, [sp, #20]
	bl	zbuf_free
	b	.L4102
.L4111:
	ldr	r2, .L4167+4
	add	r3, r3, r3, lsl #1
	add	r4, r2, r3, lsl #4
	b	.L4110
.L4109:
	mov	r0, r7
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4113
	mov	r2, #0
	add	r1, sp, #28
	mov	r0, r7
	bl	pm_log2phys
.L4113:
	ldr	r6, [r9, #1096]
	mov	r1, #0
	ldr	r3, .L4167+4
	mov	r4, r1
	ldr	ip, [sp, #28]
	add	r6, r6, #16
	add	r0, r3, #1536
.L4115:
	ldr	r2, [r3, #20]
	cmp	r7, r2
	bne	.L4114
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	tst	r2, #8
	ldrne	r4, [r3, #4]
	movne	r1, #1
	ldrne	ip, [r3, #24]
	bicne	r2, r2, #8
	strbne	r2, [r3, #2]
.L4114:
	add	r3, r3, #48
	cmp	r3, r0
	bne	.L4115
	ldr	r3, .L4167+16
	cmp	r1, #0
	ldr	r2, [sp, #16]
	strne	ip, [sp, #28]
	ldrb	r3, [r3, #-2546]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L4142
	cmp	r4, #0
	beq	.L4118
	cmp	r5, #0
	beq	.L4119
	lsl	r2, r5, #9
	mov	r1, r4
	mov	r0, fp
	bl	ftl_memcpy
	ldr	r6, [r9, #1096]
	add	r6, r6, #48
.L4119:
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	ldr	r3, .L4167+16
	ldrb	r2, [r3, #-2546]	@ zero_extendqisi2
	cmp	r5, r2
	ldrcs	r6, [r9, #1096]
	addcs	r6, r6, #16
	bcs	.L4142
	lsl	r0, r5, #9
	sub	r2, r2, r5
	lsl	r2, r2, #9
	add	r1, r4, r0
	add	r0, fp, r0
	bl	ftl_memcpy
.L4142:
	mov	r10, #0
	b	.L4117
.L4118:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4121
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [sp, #28]
	mov	r4, r0
	str	r7, [r0, #20]
	mov	r1, #1
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #4]
	ldr	r10, [r3, #12]
	cmp	r7, r2
	add	r10, r10, #1
	bne	.L4122
	ldr	r2, [r4, #36]
	cmn	r2, #1
	bne	.L4123
.L4122:
	ldrb	r2, [r9, #1153]	@ zero_extendqisi2
	mvn	ip, #0
	ldr	lr, [r4, #24]
	rsb	r1, r2, #24
	mvn	ip, ip, lsl r2
	ldr	r2, .L4167+16
	and	r0, ip, lr, lsr r1
	bic	r1, lr, ip, lsl r1
	ldrb	r2, [r2, #-2546]	@ zero_extendqisi2
	uxtb	r0, r0
	str	r2, [sp]
	ldr	r2, [r4, #4]
	bl	flash_read_page_en
	str	r0, [r4, #36]
.L4123:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r7, r3
	bne	.L4124
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4125
.L4124:
	ldr	r2, [r8, #1096]
	ldr	r0, .L4167+20
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r4, #36]
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	str	r3, [sp]
	mov	r3, r7
	bl	printk
	mov	r3, #4
	ldr	r1, [r4, #12]
	mov	r2, r3
	ldr	r0, .L4167+24
	bl	rknand_print_hex
.L4125:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r7, r3
	bne	.L4126
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4127
.L4126:
	movw	r2, #699
	ldr	r1, .L4167+8
	ldr	r0, .L4167+12
	bl	printk
	bl	dump_stack
.L4127:
	cmp	r5, #0
	beq	.L4128
	ldr	r3, [sp, #28]
	lsl	r2, r5, #9
	cmn	r3, #1
	beq	.L4129
	ldr	r1, [r4, #4]
	mov	r0, fp
	bl	ftl_memcpy
.L4130:
	ldr	r6, [r8, #1096]
	add	r6, r6, #48
.L4128:
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	ldr	r3, .L4167+16
	ldrb	r2, [r3, #-2546]	@ zero_extendqisi2
	cmp	r5, r2
	bcc	.L4131
	ldrhi	r6, [r8, #1096]
	addhi	r6, r6, #16
.L4132:
	cmp	r4, #0
	beq	.L4117
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L4167+28
	bic	r3, r3, #8
	strb	r3, [r4, #2]
	bl	buf_remove_buf
	mov	r0, r4
	bl	zbuf_free
.L4117:
	ldrh	r3, [r6, #6]
	cmp	r3, #0
	bne	.L4134
	bl	ftl_flush
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4134:
	mov	r0, r6
	mov	r4, #48
	bl	ftl_get_new_free_page
	ldr	r3, [sp, #12]
	str	r0, [sp, #16]
	mov	r0, #2
	mul	r4, r4, r3
	ldr	r3, [sp, #8]
	mov	r1, r3
	add	r5, r9, r4
	ldr	r2, [r5, #1248]
	str	r10, [r3, #12]
	stm	r3, {r2, r7}
	ldr	r2, [sp, #28]
	str	r2, [r3, #8]
	mov	r2, #0
	ldr	r3, .L4167+16
	str	r2, [r1, #16]!
	ldrb	r2, [r3, #-2546]	@ zero_extendqisi2
	sub	r2, r2, #2
	add	r2, fp, r2, lsl #9
	bl	ftl_debug_info_fill
	ldr	r3, [sp, #16]
	ldr	r1, [sp, #20]
	ldr	r0, .L4167+32
	str	r3, [r5, #1256]
	ldr	r3, [sp, #28]
	str	r3, [r5, #1260]
	mvn	r3, #0
	strb	r3, [r5, #1232]
	ldrb	r3, [r5, #1234]	@ zero_extendqisi2
	orr	r3, r3, #10
	strb	r3, [r5, #1234]
	ldr	r3, .L4167+4
	ldrh	r2, [r6, #12]
	add	r4, r3, r4
	ldrh	r3, [r6, #10]
	add	r3, r3, r2
	sub	r3, r3, #1
	strh	r3, [r4, #32]	@ movhi
	bl	buf_add_tail
	ldrb	r3, [r9, #2797]	@ zero_extendqisi2
	ldr	r4, .L4167
	add	r3, r3, #1
	strb	r3, [r9, #2797]
	bl	timer_get_time
	ldr	r3, .L4167+16
	ldrh	r2, [r6, #6]
	str	r0, [r3, #-84]
	ldrb	r3, [r9, #2797]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4135
	cmp	r2, #1
	bne	.L4105
.L4135:
	ldr	lr, .L4167+16
	mov	r5, #48
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	ldrb	r0, [lr, #-88]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, #0
	moveq	ip, #1
	movne	ip, #2
	cmp	r2, #1
	mov	r2, r0
	moveq	ip, r3
.L4139:
	cmp	r1, ip
	bne	.L4140
	uxtb	r1, r1
	add	r0, r0, r0, lsl #1
	strb	r2, [lr, #-88]
	sub	r3, r3, r1
	strb	r3, [r4, #2797]
	ldr	r3, .L4167+4
	add	r0, r3, r0, lsl #4
	bl	sblk_prog_page
	ldrh	r3, [r6, #6]
	cmp	r3, #1
	bne	.L4105
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	r0, r6
	bl	ftl_write_last_log_page
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4105:
	ldrb	r3, [r4, #2796]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4102
.L4104:
	bl	ftl_write_completed
	mov	r0, #0
	b	.L4101
.L4131:
	ldr	r3, [sp, #28]
	sub	r2, r2, r5
	lsl	r2, r2, #9
	lsl	r0, r5, #9
	cmn	r3, #1
	beq	.L4133
	ldr	r1, [r4, #4]
	add	r1, r1, r0
	add	r0, fp, r0
	bl	ftl_memcpy
	b	.L4132
.L4133:
	mov	r1, #0
	add	r0, fp, r0
	bl	ftl_memset
	b	.L4132
.L4140:
	mla	r2, r5, r2, r4
	add	r1, r1, #1
	ldrb	r2, [r2, #1232]	@ zero_extendqisi2
	b	.L4139
.L4121:
	cmp	r5, #0
	moveq	r10, r5
	moveq	r4, r5
	beq	.L4128
	lsl	r2, r5, #9
	mov	r10, r4
.L4129:
	mov	r1, #0
	mov	r0, fp
	bl	ftl_memset
	b	.L4130
.L4168:
	.align	2
.L4167:
	.word	.LANCHOR0
	.word	.LANCHOR0+1232
	.word	.LANCHOR1+2492
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC296
	.word	.LC240
	.word	.LANCHOR0+2771
	.word	.LANCHOR3-88
	.fnend
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r7, .L4307
	ldrb	r0, [r7, #-3119]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L4170
	bl	buf_alloc
	subs	r4, r0, #0
	beq	.L4169
	ldr	r5, .L4307+4
	ldrh	r2, [r5, #22]
	mov	r0, r2
	bl	gc_get_src_ppa_from_index
	mov	r1, #1
	add	r2, r2, #1
	str	r0, [r4, #24]
	mov	r6, r0
	mov	r0, r4
	strh	r2, [r5, #22]	@ movhi
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4172
	movw	r2, #1032
	ldr	r1, .L4307+8
	ldr	r0, .L4307+12
	bl	printk
	bl	dump_stack
.L4172:
	ldr	r3, [r4, #12]
	ldr	r8, [r3, #4]
	mov	r0, r8
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #56]
	bne	.L4173
	mov	r2, #0
	add	r1, sp, #56
	mov	r0, r8
	bl	pm_log2phys
.L4173:
	ldr	r9, [sp, #56]
	cmp	r6, r9
	bne	.L4174
	ldr	r2, .L4307+16
	mov	r3, #0
	sub	r10, r2, #1232
.L4177:
	add	r1, r2, r3
	ldr	r0, [r1, #20]
	cmp	r8, r0
	bne	.L4175
	ldrb	r1, [r1, #2]	@ zero_extendqisi2
	tst	r1, #2
	beq	.L4175
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L4307+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4169
	ldrh	r3, [r5, #22]
	mov	r2, r6
	mov	r1, r8
	ldr	r0, .L4307+24
	bl	printk
.L4169:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4175:
	add	r3, r3, #48
	cmp	r3, #1536
	bne	.L4177
	ldrb	r3, [r7, #-2546]	@ zero_extendqisi2
	mov	r2, #0
	str	r8, [r4, #20]
	strb	r2, [r4, #41]
	strb	r3, [r4, #40]
	ldr	r3, [r4, #12]
	ldr	r1, [r3]
	str	r1, [r4, #16]
	str	r2, [r3, #16]
	ldr	r3, .L4307+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4178
	mov	r0, r8
	bl	lpa_hash_get_ppa
	ldrh	r3, [r5, #22]
	mov	r2, r9
	str	r6, [sp]
	mov	r1, r8
	str	r3, [sp, #4]
	mov	r3, r0
	ldr	r0, .L4307+28
	bl	printk
.L4178:
	mov	r0, r4
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	r2, [r10, #2800]
	ldr	r3, [r2, #60]
	add	r3, r3, #1
	str	r3, [r2, #60]
	ldrh	r3, [r5, #24]
	add	r3, r3, #1
	strh	r3, [r5, #24]	@ movhi
	b	.L4169
.L4174:
	ldr	r3, .L4307+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4179
	mov	r0, r8
	bl	lpa_hash_get_ppa
	ldrh	r3, [r5, #22]
	mov	r2, r9
	str	r6, [sp]
	mov	r1, r8
	str	r3, [sp, #4]
	mov	r3, r0
	ldr	r0, .L4307+28
	bl	printk
.L4179:
	mov	r0, r4
	bl	zbuf_free
	b	.L4169
.L4170:
	ldr	r10, .L4307+32
	ldr	r3, [r10, #1096]
	ldrb	r9, [r3, #89]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldrb	r3, [r7, #-3127]	@ zero_extendqisi2
	uxth	r8, r9
	cmp	r3, r8
	movhi	r6, #1
	bhi	.L4180
	cmp	r8, #2
	movcc	r6, r8
	movcs	r6, #2
.L4180:
	ldrb	r5, [r7, #-3128]	@ zero_extendqisi2
	ldr	r3, .L4307+4
	cmp	r5, #3
	bne	.L4181
	ldrb	r2, [r10, #1158]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4182
	movw	r2, #2102
	mov	r1, r9
	ldrh	fp, [r3, r2]
	mov	r0, fp
	bl	__aeabi_idiv
	ldrb	r3, [r7, #-2542]	@ zero_extendqisi2
	uxth	r4, r0
	cmp	r3, #0
	beq	.L4183
	ldr	r3, .L4307+36
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #0
	moveq	r5, #1
	movne	r5, #2
.L4184:
	smulbb	r8, r8, r4
	sub	r8, fp, r8
	uxth	r3, r8
	str	r3, [sp, #20]
.L4185:
	mul	r3, r6, r5
	ldr	r9, .L4307+36
	mov	r8, #0
	str	r3, [sp, #16]
	add	r9, r9, r4, lsl #1
	ldr	r3, .L4307+4
.L4189:
	ldr	r1, [sp, #16]
	uxth	r0, r8
	mov	r2, r0
	cmp	r0, r1
	bge	.L4242
	ldr	r1, .L4307+40
	add	r2, fp, r2
	ldrh	ip, [r1]
	ldr	r1, [sp, #12]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	mul	r1, r1, ip
	ldrb	ip, [r7, #-11]	@ zero_extendqisi2
	sub	r1, r1, ip
	cmp	r2, r1
	blt	.L4190
	ldrb	r1, [r7, #-2542]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4242
	ldrh	r1, [r9]
	cmp	r1, r4
	bcc	.L4191
.L4242:
	mov	r3, #1
	str	r3, [sp, #24]
	b	.L4187
.L4183:
	ldrb	r3, [r7, #-15]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r5, #1
	beq	.L4184
	sub	r3, r4, #62
	cmp	r3, #2160
	movcs	r5, #2
	bcs	.L4184
	mov	r1, r5
	mov	r0, r4
	bl	__aeabi_uidivmod
	uxth	r1, r1
	cmp	r1, #0
	movne	r5, #1
	moveq	r5, #2
	b	.L4184
.L4182:
	ldrb	r2, [r7, #-3120]	@ zero_extendqisi2
	cmp	r2, #0
	movw	r2, #2102
	beq	.L4186
	ldrh	fp, [r3, r2]
	add	r1, r9, r9, lsl #1
	mov	r0, fp
	bl	__aeabi_idiv
	mul	r9, r0, r9
	ldr	r2, .L4307+44
	uxth	r4, r0
	sub	r9, r9, r9, lsl #2
	add	r9, r9, fp
	smull	r2, r3, r9, r2
	sub	r9, r3, r9, asr #31
	uxth	r3, r9
	str	r3, [sp, #20]
	ldrb	r3, [r10, #1159]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r4, r4, r4, lsl #1
	uxthne	r4, r4
	b	.L4185
.L4186:
	ldrh	r10, [r3, r2]
	mov	r1, r9
	mov	r0, r10
	bl	__aeabi_idiv
	ldr	r2, .L4307+48
	lsl	r3, r0, #1
	smulbb	r8, r0, r8
	ldrh	r3, [r2, r3]
	sub	r10, r10, r8
	lsr	r4, r3, #3
	and	r3, r3, #7
	str	r3, [sp, #24]
	uxth	r3, r10
	mla	r9, r9, r4, r3
	str	r3, [sp, #20]
	ldr	r3, [sp, #24]
	add	r9, r9, r9, lsl #1
	cmp	r3, #1
	uxth	fp, r9
	beq	.L4185
.L4187:
	mul	r3, r6, r5
	str	r3, [sp, #36]
	lsl	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #44]
	ldrh	r3, [sp, #44]
	smulbb	r3, r3, r5
	sub	r3, fp, r3
	uxth	r3, r3
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #32]
	ldr	r3, .L4307+36
	add	r3, r3, r4, lsl #1
	str	r3, [sp, #40]
.L4192:
	ldrh	r9, [sp, #32]
	ldr	r2, [sp, #36]
	ldr	r7, .L4307
	str	r9, [sp, #28]
	cmp	r9, r2
	blt	.L4216
	ldrb	r3, [r7, #-3128]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L4217
	ldr	r3, .L4307+32
	ldrb	r1, [r3, #1158]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r2
	movne	r1, #0
	subne	r2, r3, #1
	bne	.L4219
	ldrb	r2, [r7, #-3120]	@ zero_extendqisi2
	ldr	r3, [sp, #56]
	cmp	r2, #0
	strbne	r1, [r3, #44]
	bne	.L4305
	ldr	r1, [sp, #24]
	cmp	r1, #1
	moveq	r2, #9
	beq	.L4225
	ldr	r1, [sp, #24]
	cmp	r1, #2
	moveq	r2, #13
.L4225:
	strb	r2, [r3, #44]
.L4305:
	mov	r2, r6
	add	r1, sp, #68
	add	r0, sp, #56
	bl	sblk_xlc_prog_pages
.L4221:
	ldrb	r3, [r7, #-3120]	@ zero_extendqisi2
	ldr	r2, .L4307+32
	cmp	r3, #0
	addne	r6, r6, r6, lsl #1
	uxthne	r6, r6
	bne	.L4231
	ldrb	r3, [r7, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r6, [sp, #44]
	bne	.L4231
	ldrb	r3, [r2, #1158]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4231
	smulbb	r5, r6, r5
	ldrb	r3, [r7, #-2542]	@ zero_extendqisi2
	uxth	r5, r5
	cmp	r3, #0
	moveq	r6, r5
	beq	.L4231
	ldr	r1, .L4307+36
	lsl	r3, r4, #1
	ldrh	r3, [r1, r3]
	cmp	r3, r4
	movcs	r6, r5
.L4231:
	ldr	r1, [r2, #2800]
	ldr	r0, .L4307+4
	ldr	r3, [r1, #52]
	add	r3, r3, r6
	str	r3, [r1, #52]
	movw	r1, #2102
	ldrh	r3, [r0, r1]
	add	r6, r6, r3
	ldr	r3, .L4307+52
	uxth	r6, r6
	strh	r6, [r0, r1]	@ movhi
	ldrh	r1, [r3, #-14]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r6, r3
	ldrge	r3, [r2, #1096]
	movge	r2, #0
	strhge	r2, [r3, #86]	@ movhi
	bl	gc_write_completed
	b	.L4169
.L4181:
	ldrb	r2, [r7, #-3126]	@ zero_extendqisi2
	mov	r1, r9
	cmp	r2, #0
	movw	r2, #2102
	ldrh	fp, [r3, r2]
	mov	r0, fp
	bne	.L4188
	bl	__aeabi_idiv
	uxth	r4, r0
	mov	r5, #1
	smulbb	r8, r8, r4
	sub	r8, fp, r8
	uxth	r3, r8
	str	r3, [sp, #20]
	b	.L4185
.L4188:
	bl	__aeabi_idiv
	uxth	r4, r0
	mov	r5, #2
	smulbb	r8, r8, r4
	sub	r8, fp, r8
	uxth	r3, r8
	str	r3, [sp, #20]
	b	.L4185
.L4191:
	tst	r0, #1
	beq	.L4242
.L4190:
	ldr	r10, [r7, #-128]
	ldr	r1, [r10, r2, lsl #2]
	cmn	r1, #1
	bne	.L4193
	ldrh	r1, [r3, #22]
	ldrh	ip, [r3, #20]
	cmp	ip, r1
	bls	.L4169
	ldrb	ip, [r7, #-2542]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L4194
	ldrh	ip, [r9]
	cmp	ip, r4
	bcs	.L4194
	tst	r0, #1
	ldrne	r1, [r10, ip, lsl #2]
	strne	r1, [r10, r2, lsl #2]
	bne	.L4193
.L4194:
	mov	r0, r1
	str	r3, [sp, #24]
	bl	gc_get_src_ppa_from_index
	ldr	r3, [sp, #24]
	add	r1, r1, #1
	strh	r1, [r3, #22]	@ movhi
	str	r0, [r10, r2, lsl #2]
.L4193:
	add	r8, r8, #1
	b	.L4189
.L4216:
	ldr	r3, [sp, #28]
	add	r3, fp, r3
	str	r3, [sp, #16]
	ldr	r2, [sp, #16]
	ldr	r3, [r7, #-3124]
	ldrb	r3, [r3, r2]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L4196
	mov	r0, #0
	bl	buf_alloc
	subs	r7, r0, #0
	bne	.L4197
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	r0, r7
	bl	buf_alloc
	subs	r7, r0, #0
	beq	.L4169
.L4197:
	ldr	r2, .L4307
	ldrb	r1, [r7, #1]	@ zero_extendqisi2
	ldr	r0, [sp, #16]
	ldr	r3, [r2, #-3124]
	strb	r1, [r3, r0]
	ldr	r1, .L4307+32
	ldrb	r3, [r1, #2831]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r1, #2831]
	add	r3, r9, fp
	strh	r3, [r7, #32]	@ movhi
	ldrb	r3, [sp, #24]	@ zero_extendqisi2
	strb	r3, [r7, #45]
	sub	r3, r2, #3104
	ldrh	r1, [r3, #-14]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r1
	ldrb	r1, [r2, #-11]	@ zero_extendqisi2
	sub	r3, r3, r1
	cmp	r0, r3
	blt	.L4198
	ldrb	r3, [r2, #-2542]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4199
	ldr	r3, [sp, #40]
	ldrh	r3, [r3]
	cmp	r3, r4
	bcs	.L4199
	tst	r9, #1
	bne	.L4198
.L4199:
	ldr	r8, .L4307
	mvn	r2, #0
	ldr	r1, [sp, #16]
	ldr	r10, .L4307+56
	ldr	r3, [r8, #-132]
	str	r2, [r3, r1, lsl #2]
	ldrb	r3, [r8, #-11]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L4200
	sub	r3, r8, #3104
	ldr	r2, [sp, #16]
	ldrh	r1, [r3, #-14]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r1, r3
	sub	r3, r3, #2
	cmp	r2, r3
	ldrh	r2, [r8, #-14]
	bne	.L4201
	ldr	r1, [r8, #-132]
	ldr	r0, [r7, #4]
	bl	ftl_memcpy
	ldrb	r2, [r8, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r7, #12]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldr	r3, [r7, #12]
	str	r10, [r3]
	ldrh	r1, [r8, #-14]
	ldr	r0, [r7, #4]
	ldr	r10, [r7, #12]
	bl	js_hash
	ldr	r2, .L4307+32
	str	r0, [r10, #4]
	ldr	r3, [r7, #12]
	ldr	r2, [r2, #1096]
	ldr	r2, [r2, #132]
	str	r2, [r3, #8]
	mov	r2, #0
	ldr	r3, [r7, #12]
.L4301:
	str	r2, [r3, #12]
.L4202:
	ldr	r3, [r7, #12]
	mov	r2, #0
	str	r2, [r3, #16]
.L4196:
	ldr	r3, .L4307
	ldr	r1, [sp, #16]
	ldrb	lr, [sp, #24]	@ zero_extendqisi2
	ldr	r2, [r3, #-3124]
	ldrb	r7, [r2, r1]	@ zero_extendqisi2
	add	r2, sp, #80
	ldr	r1, [sp, #28]
	add	r0, r7, r7, lsl #1
	add	ip, r2, r1, lsl #2
	ldr	r2, .L4307+32
	add	r1, r2, #1232
	add	r0, r1, r0, lsl #4
	str	r0, [ip, #-24]
	mov	ip, #48
	mul	ip, ip, r7
	add	r1, r1, ip
	add	r0, r2, ip
	mov	ip, #2
	strb	lr, [r0, #1277]
	strh	ip, [r1, #34]	@ movhi
	ldrb	r1, [r3, #-3128]	@ zero_extendqisi2
	cmp	r1, #3
	bne	.L4207
	ldrb	r1, [r2, #1158]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4208
	ldrb	r3, [r3, #-2542]	@ zero_extendqisi2
	and	r8, r9, #1
	add	r8, r8, r4
	cmp	r3, #0
	beq	.L4209
	ldr	r3, [sp, #40]
	ldr	r2, [sp, #32]
	ldrh	r3, [r3]
	cmp	r3, r4
	movcs	r2, #0
	andcc	r2, r2, #1
	cmp	r2, #0
	movne	r8, r3
.L4209:
	ldr	r3, .L4307+32
	mov	r10, #48
	mov	r1, r5
	mov	r0, r9
	mla	r10, r10, r7, r3
	bl	__aeabi_uidiv
	ldr	r3, [sp, #20]
	ldr	r2, .L4307+60
	uxtah	r0, r3, r0
	ldr	r3, [sp, #12]
	ldrh	r2, [r2]
	add	r0, r3, r0, lsl #1
	ldrh	r3, [r0, #96]
	mla	r8, r2, r3, r8
	orr	r8, r8, #50331648
	str	r8, [r10, #1256]
.L4214:
	mov	r2, #48
	ldr	r3, .L4307+32
	mul	r7, r2, r7
	add	r2, r3, r7
	add	r3, r3, #1232
	add	r3, r3, r7
	ldr	r2, [r2, #1256]
	ldrh	r1, [r3, #32]
	ldr	r3, .L4307
	ldr	r3, [r3, #-124]
	str	r2, [r3, r1, lsl #2]
	b	.L4233
.L4201:
	ldrb	r3, [r8, #-3127]	@ zero_extendqisi2
	ldr	r0, [r8, #-132]
	mul	r3, r1, r3
	lsr	r1, r2, #2
	add	r1, r0, r1, lsl #2
	ldr	r0, [r7, #4]
	rsb	r3, r2, r3, lsl #2
	mov	r2, r3
	str	r3, [sp, #52]
	bl	ftl_memcpy
	ldrb	r2, [r8, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r7, #12]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldr	r2, [r7, #12]
	ldr	r3, [sp, #52]
	str	r10, [r2]
	mov	r1, r3
	ldr	r0, [r7, #4]
	ldr	r8, [r7, #12]
	bl	js_hash
	ldr	r2, .L4307+32
	str	r0, [r8, #4]
	ldr	r3, [r7, #12]
	ldr	r2, [r2, #1096]
	ldr	r2, [r2, #132]
	str	r2, [r3, #8]
	mov	r2, #1
	ldr	r3, [r7, #12]
	b	.L4301
.L4200:
	ldr	r3, .L4307+52
	ldrb	r2, [r8, #-3127]	@ zero_extendqisi2
	ldr	r1, [r8, #-132]
	ldrh	r3, [r3, #-14]
	ldr	r0, [r7, #4]
	mul	r2, r2, r3
	lsl	r2, r2, #2
	bl	ftl_memcpy
	ldr	r3, .L4307+52
	ldrb	r1, [r8, #-2546]	@ zero_extendqisi2
	ldr	r0, [r7, #4]
	ldrh	r2, [r3, #-14]
	ldrb	r3, [r8, #-3127]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r1, r3, asr #6
	lsl	r2, r3, #2
	ldrhlt	r1, [r8, #-14]
	mov	r3, r2
	ldr	r8, .L4307
	add	r0, r0, r3
	sublt	r2, r1, r2
	ldr	r1, [r8, #-128]
	bl	ftl_memcpy
	ldrb	r2, [r8, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r7, #12]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldr	r3, [r7, #12]
	str	r10, [r3]
	sub	r3, r8, #3104
	ldrh	r3, [r3, #-14]
	ldrb	r1, [r8, #-3127]	@ zero_extendqisi2
	ldr	r0, [r7, #4]
	ldr	r10, [r7, #12]
	mul	r1, r1, r3
	lsl	r1, r1, #2
	bl	js_hash
	ldr	r2, .L4307+32
	str	r0, [r10, #4]
	ldr	r3, [r7, #12]
	ldr	r2, [r2, #1096]
	ldr	r2, [r2, #132]
.L4302:
	str	r2, [r3, #8]
	b	.L4202
.L4198:
	ldr	r8, .L4307
	mov	r1, #1
	ldr	r2, [sp, #16]
	mov	r0, r7
	ldr	r3, [r8, #-128]
	ldr	r3, [r3, r2, lsl #2]
	str	r3, [r7, #24]
	bl	sblk_read_page
	ldr	r3, [r7, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4205
	ldr	r3, .L4307+64
	mvn	r0, #0
	ldr	r2, [r7, #24]
	ldrh	r1, [r3]
	ldr	r3, .L4307+32
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
	lsr	r2, r2, r1
	rsb	r3, r3, #24
	sub	r3, r3, r1
	ldrb	r1, [r8, #-3136]	@ zero_extendqisi2
	bic	r0, r2, r0, lsl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
	ldr	r3, [r7, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4205
	ldr	r3, [r7, #12]
	mvn	r2, #0
	str	r2, [r3, #4]
	ldr	r3, [r7, #36]
	cmp	r3, r2
	cmpne	r3, #512
	bne	.L4205
	movw	r2, #1223
	ldr	r1, .L4307+8
	ldr	r0, .L4307+12
	bl	printk
	bl	dump_stack
.L4205:
	ldr	r3, [r7, #12]
	ldr	r2, .L4307+32
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #2780]
	cmp	r1, r2
	ldr	r1, [sp, #16]
	mvncs	r2, #0
	strcs	r2, [r3, #4]
	ldr	r3, [r7, #12]
	ldr	r2, [r3, #4]
	ldr	r3, .L4307
	ldr	r3, [r3, #-132]
	str	r2, [r3, r1, lsl #2]
	ldr	r3, [r7, #12]
	ldr	r2, [r7, #24]
	b	.L4302
.L4208:
	ldrb	r3, [r2, #1159]	@ zero_extendqisi2
	ldr	r1, .L4307+68
	cmp	r3, #0
	umull	r2, r3, r9, r1
	beq	.L4211
	ldr	r2, [sp, #20]
	lsr	r3, r3, #1
	ldr	ip, [sp, #12]
	uxtah	r2, r2, r3
	add	r3, r3, r3, lsl #1
	add	r2, ip, r2, lsl #1
	ldr	ip, .L4307+60
	sub	r3, r9, r3
	ldrh	r2, [r2, #96]
	ldrh	ip, [ip]
	mla	r2, ip, r2, r4
	uxtah	r3, r2, r3
.L4306:
	str	r3, [r0, #1256]
	mov	r0, #48
	mul	r7, r0, r7
	ldr	r3, .L4307+32
	umull	r0, r1, r9, r1
	add	r2, r3, #1232
	add	r2, r2, r7
	add	r7, r3, r7
	ldrh	ip, [r2, #32]
	ldr	r2, .L4307
	lsr	r1, r1, #1
	ldr	r3, [r7, #1256]
	add	r1, r1, r1, lsl #1
	ldr	r2, [r2, #-124]
	sub	r1, r9, r1
	lsl	r1, r1, #24
	add	r1, r1, #16777216
	orr	r1, r1, r3
	str	r1, [r2, ip, lsl #2]
.L4233:
	ldr	r3, .L4307
	ldrb	r2, [r3, #-2542]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4215
	ldr	r2, [sp, #40]
	ldrh	r2, [r2]
	cmp	r2, r4
	bcs	.L4215
	tst	r9, #1
	beq	.L4215
	ldr	r1, [r3, #-3124]
	mvn	r2, #0
	ldr	r0, [sp, #16]
	ldr	ip, [sp, #48]
	strb	r2, [r1, r0]
	add	r1, sp, #80
	ldr	r0, [sp, #28]
	add	r1, r1, r0, lsl #2
	ldrh	r0, [sp, #48]
	ldr	r1, [r1, #-24]
	strh	r0, [r1, #32]	@ movhi
	ldrb	r0, [r1, #1]	@ zero_extendqisi2
	ldr	r1, [r3, #-3124]
	strb	r0, [r1, ip]
	ldr	r1, [sp, #16]
	ldr	r3, [r3, #-128]
	str	r2, [r3, r1, lsl #2]
.L4215:
	ldr	r3, [sp, #32]
	add	r3, r3, #1
	str	r3, [sp, #32]
	b	.L4192
.L4211:
	ldr	r2, [sp, #20]
	ubfx	r3, r3, #1, #16
	add	r3, r2, r3
	ldr	r2, [sp, #12]
	add	r3, r2, r3, lsl #1
	ldr	r2, .L4307+60
	ldrh	r3, [r3, #96]
	ldrh	r2, [r2]
	mla	r3, r2, r3, r4
	b	.L4306
.L4207:
	cmp	r1, #2
	bne	.L4214
	ldrb	r3, [r3, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4213
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #28]
	add	r3, r3, r2
	ldr	r2, [sp, #12]
	add	r3, r2, r3, lsl #1
	ldr	r2, .L4307+60
	ldrh	r3, [r3, #96]
	ldrh	r2, [r2]
	mla	r3, r2, r3, r4
.L4303:
	orr	r3, r3, #33554432
	str	r3, [r0, #1256]
	b	.L4214
.L4213:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #12]
	add	r3, r3, r9, lsr #1
	add	r3, r2, r3, lsl #1
	ldr	r2, .L4307+60
	ldrh	r3, [r3, #96]
	ldrh	r2, [r2]
	mla	r3, r2, r3, r4
	and	r2, r9, #1
	add	r3, r3, r2
	b	.L4303
.L4220:
	add	r0, sp, #80
	add	ip, sp, #80
	add	r0, r0, r3, lsl #2
	add	r3, ip, r3, lsl #2
	ldr	r3, [r3, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [r0]
.L4219:
	uxth	r3, r1
	add	r1, r1, #1
	cmp	r3, r2
	blt	.L4220
	add	r3, sp, #80
	add	r2, r3, r2, lsl #2
	ldr	r3, [r2, #-24]
.L4304:
	smulbb	r1, r6, r5
	mvn	r2, #0
	strb	r2, [r3]
	ldr	r0, [sp, #56]
	uxtb	r1, r1
	bl	sblk_prog_page
	b	.L4221
.L4217:
	ldrb	r3, [r7, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4226
	ldrb	r3, [r7, #-3125]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4227
.L4226:
	ldr	r2, [sp, #36]
	sub	r1, r2, #1
.L4228:
	uxth	r2, r3
	add	r3, r3, #1
	cmp	r2, r1
	blt	.L4229
	add	r3, sp, #80
	add	r1, r3, r1, lsl #2
	ldr	r3, [r1, #-24]
	b	.L4304
.L4227:
	mov	r1, r6
	add	r0, sp, #56
	bl	sblk_3d_mlc_prog_pages
	b	.L4221
.L4229:
	add	r0, sp, #80
	add	ip, sp, #80
	add	r0, r0, r2, lsl #2
	add	r2, ip, r2, lsl #2
	ldr	r2, [r2, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r2, [r2, #1]	@ zero_extendqisi2
	strb	r2, [r0]
	b	.L4228
.L4308:
	.align	2
.L4307:
	.word	.LANCHOR3
	.word	.LANCHOR0+2824
	.word	.LANCHOR1+2509
	.word	.LC0
	.word	.LANCHOR0+1232
	.word	.LANCHOR2
	.word	.LC297
	.word	.LC298
	.word	.LANCHOR0
	.word	.LANCHOR2+2114
	.word	.LANCHOR3-3118
	.word	1431655766
	.word	.LANCHOR2+3650
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LANCHOR3-3074
	.word	.LANCHOR3-3138
	.word	-1431655765
	.fnend
	.size	gc_do_copy_back, .-gc_do_copy_back
	.align	2
	.global	zftl_do_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_do_gc, %function
zftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2788
	ldr	r4, .L4535
	cmp	r0, #1
	.pad #36
	sub	sp, sp, #36
	mov	r10, r0
	ldr	r5, .L4535+4
	add	r9, r4, #2784
	ldrh	r8, [r4, r3]
	ldrh	r6, [r9]
	movw	r3, #2786
	ldr	r7, [r4, #1096]
	ldrh	fp, [r4, r3]
	add	r6, r8, r6
	uxth	r6, r6
	beq	.L4310
.L4322:
	ldrb	r3, [r5, #-144]	@ zero_extendqisi2
	cmp	r3, #6
	ldrls	pc, [pc, r3, asl #2]
	b	.L4435
.L4313:
	.word	.L4312
	.word	.L4314
	.word	.L4315
	.word	.L4316
	.word	.L4317
	.word	.L4318
	.word	.L4319
.L4318:
	ldr	r8, .L4535+40
	mov	r7, #0
.L4320:
	bl	gc_check_data_one_wl
	subs	r9, r0, #0
	beq	.L4431
	ldr	r3, [r4, #1096]
	mov	r6, #0
	strh	r6, [r8, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r2, [r4, #1096]
	mvn	r3, #0
	ldr	r1, [r4, #2800]
	ldr	r0, [r4, #2832]
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	bl	zbuf_free
	str	r6, [r4, #2832]
	strb	r6, [r5, #-144]
	b	.L4532
.L4310:
	ldr	r3, [r5, #-8]
	cmp	r3, #0
	bne	.L4321
	ldr	r2, [r5, #-4]
	cmp	r2, #0
	beq	.L4322
.L4321:
	ldr	r2, .L4535+60
	ldrh	r2, [r2, #-4]
	cmp	r6, r2, lsr #2
	bls	.L4322
	movw	r2, #2804
	ldrh	r2, [r4, r2]
	cmp	r2, r6
	bcs	.L4322
	str	r3, [sp, #24]
	bl	timer_get_time
	ldr	r3, [sp, #24]
	add	r3, r3, #20
	cmp	r3, r0
	movcc	r3, #0
	strcc	r3, [r5, #-8]
	ldr	r3, [r5, #-4]
	add	r3, r3, #20
	str	r3, [sp, #24]
	bl	timer_get_time
	ldr	r3, [sp, #24]
	cmp	r3, r0
	movcc	r3, #0
	strcc	r3, [r5, #-4]
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4322
.L4435:
	mov	r7, #16
	b	.L4309
.L4312:
	movw	r3, #2790
	ldrh	r2, [r7, #80]
	ldrh	r9, [r4, r3]
	movw	r3, #2792
	ldrh	r3, [r4, r3]
	add	r8, r8, fp
	uxth	r8, r8
	add	r9, r9, r3
	movw	r3, #65535
	cmp	r2, r3
	uxth	r9, r9
	beq	.L4325
	cmp	r10, #0
	bne	.L4326
	movw	r3, #2804
	ldrh	r3, [r4, r3]
	cmp	r6, r3, lsl #1
	bge	.L4435
.L4326:
	ldr	r10, .L4535+60
	ldrh	r1, [r10, #-6]
	sub	r0, r10, #12
	add	r1, r1, #1
	uxth	r1, r1
	strh	r1, [r10, #-6]	@ movhi
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4328
	ldr	r1, [r4, #1092]
	lsl	r2, r0, #1
	ldr	r3, [r5, #-140]
	ldrh	r2, [r1, r2]
	ldrh	r1, [r10, #-8]
	add	r3, r3, #1
	str	r3, [r5, #-140]
	cmp	r1, r2
	bcs	.L4329
	movw	r1, #1080
	ldrh	r1, [r4, r1]
	cmp	r3, r1, lsr #4
	bls	.L4328
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bls	.L4328
.L4329:
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	mov	r1, #0
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L4330
	mov	r3, #1
	strb	r3, [r5, #-144]
	mov	r3, #0
	str	r3, [r5, #-140]
	b	.L4435
.L4328:
	mov	r3, #0
	strh	r3, [r10, #-6]	@ movhi
.L4330:
	cmp	r8, #15
	movls	r9, #2
	bls	.L4331
	movw	r3, #2794
	clz	r9, r9
	ldrh	r2, [r4, r3]
	lsr	r9, r9, #5
	ldrh	r3, [r10, #-2]
	cmp	r2, r3
	orrhi	r9, r9, #1
	cmp	r9, #0
	movne	r9, #2
	moveq	r9, #1
.L4331:
	ldr	r3, .L4535+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4332
	ldrh	r1, [r7, #80]
	ldr	r3, [r4, #1096]
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	str	r1, [sp, #16]
	ldrh	r1, [r3, #122]
	ldr	r0, .L4535+12
	str	r1, [sp, #12]
	ldrh	r1, [r3, #120]
	str	r1, [sp, #8]
	movw	r1, #2807
	ldrh	r3, [r3, #124]
	str	r8, [sp]
	str	r3, [sp, #4]
	mov	r3, r6
	bl	printk
.L4332:
	mov	r2, #1
	mov	r1, r9
	ldrb	r0, [r5, #-3119]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	ble	.L4333
.L4334:
	mov	r3, #1
.L4533:
	strb	r3, [r5, #-144]
	b	.L4435
.L4333:
	mov	r2, #1
	mov	r1, #3
	ldrb	r0, [r5, #-3119]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	bgt	.L4334
	b	.L4435
.L4325:
	cmp	r10, #1
	bne	.L4335
	ldr	r3, [r4, #2800]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	beq	.L4336
	movw	r3, #2788
	ldrh	r3, [r4, r3]
	cmp	r3, #8
	bls	.L4336
	bl	gc_ink_check_sblk
.L4336:
	bl	gc_scan_static_data
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #122]
	cmp	r3, #0
	beq	.L4337
.L4338:
	mov	r3, #1
	strb	r3, [r5, #-3119]
	b	.L4533
.L4337:
	bl	gc_static_wearleveling
	subs	r7, r0, #0
	bne	.L4338
	bl	gc_block_vpn_scan
	cmp	r8, #0
	ldr	fp, .L4535+60
	beq	.L4339
	cmp	r6, r9
	bcs	.L4340
	ldrh	r3, [fp, #-4]
	cmp	r3, r6
	bhi	.L4341
.L4340:
	ldrh	r2, [fp, #-4]
	add	r3, r6, r9
	cmp	r3, r2
	blt	.L4341
	movw	r3, #2794
	ldrh	r2, [r4, r3]
	ldrh	r3, [fp, #-2]
	cmp	r2, r3
	bcc	.L4339
.L4341:
	mov	r1, #1
	mov	r0, #16
	strb	r1, [r5, #-3119]
	str	r1, [sp, #24]
	bl	zftl_get_gc_node.part.10
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4342
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldr	r1, [sp, #24]
	ldrh	r3, [r2, r3]
	ldrh	r2, [r5, #-136]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r6, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4342
	mov	r2, #0
	strb	r2, [r5, #-3119]
	str	r2, [r5, #-140]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4334
.L4342:
	mov	r1, #0
	ldr	r0, .L4535+16
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4343
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r5, #-136]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r6, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4343
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r5, #-3119]
	str	r2, [r5, #-140]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4334
.L4343:
	ldrh	r3, [r5, #-136]
	ldrh	r2, [fp, #-8]
	ldr	r0, .L4535+16
	add	r3, r3, #1
	ldr	r1, [r5, #-140]
	uxth	r3, r3
	str	r2, [sp, #28]
	cmp	r3, r2, lsr #5
	strh	r3, [r5, #-136]	@ movhi
	add	r1, r1, #1
	movhi	r3, #4
	str	r1, [r5, #-140]
	strhhi	r3, [r5, #-136]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-8]	@ movhi
	strh	r3, [r0, #-6]	@ movhi
	strh	r3, [r0, #-4]	@ movhi
	movw	r0, #1080
	ldrh	r0, [r4, r0]
	cmp	r1, r0, lsr #5
	bls	.L4345
	movw	r1, #2794
	ldrh	r1, [r4, r1]
	cmp	r1, r8
	bls	.L4345
	mov	r1, r3
	sub	r0, fp, #12
	str	r3, [sp, #24]
	bl	_list_get_gc_head_node
	movw	r1, #65535
	ldr	r3, [sp, #24]
	cmp	r0, r1
	ldr	r2, [sp, #28]
	bne	.L4346
.L4519:
	mov	r7, #16
	b	.L4347
.L4346:
	ldr	r1, [r4, #1092]
	lsl	r0, r0, #1
	ldrh	r1, [r1, r0]
	ldrb	r0, [r5, #-3127]	@ zero_extendqisi2
	mul	r2, r2, r0
	cmp	r1, r2
	strle	r3, [r5, #-140]
	movle	r2, #4
	bgt	.L4519
.L4527:
	mov	r1, #2
.L4518:
	ldrb	r0, [r5, #-3119]	@ zero_extendqisi2
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	beq	.L4519
	b	.L4334
.L4345:
	movw	r3, #2794
	ldrh	r2, [r4, r3]
	ldrh	r3, [fp, #-2]
	cmp	r2, r3
	movcs	r2, #1
	bcs	.L4527
.L4350:
	movw	r3, #2790
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L4351
	movw	r3, #2792
	ldrh	r3, [r4, r3]
	cmp	r3, #8
	bls	.L4352
.L4351:
	mov	r2, #4
	mov	r1, #1
	b	.L4518
.L4352:
	ldr	r2, .L4535+40
	movw	r3, #2106
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L4519
.L4347:
	ldr	r3, [r4, #2812]
	cmp	r3, #0
	beq	.L4353
	mov	r3, #0
	mov	r1, #0
	str	r3, [r4, #2812]
	subs	r3, r8, r3
	movne	r3, #1
	cmp	r6, #15
	movhi	r3, #0
	ldr	r0, .L4535+20
	cmp	r3, #0
	movne	r3, #1
	strb	r3, [r5, #-3119]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4356
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #8
	bhi	.L4356
	mov	r3, #1
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	mov	r1, #0
	str	r3, [r4, #2812]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4334
.L4356:
	mov	r1, #0
	ldr	r0, .L4535+16
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4357
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4357
	mov	r3, #1
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	mov	r1, #0
	str	r3, [r4, #2812]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4334
.L4357:
	mov	r0, #0
	bl	zftl_get_gc_node.part.10
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4353
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4353
	mov	r1, #1
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	str	r1, [r4, #2812]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4334
.L4353:
	ldr	r3, [r4, #1096]
	mov	r1, #1
	strb	r1, [r5, #-3119]
	ldrh	fp, [r3, #124]
	cmp	fp, #0
	movne	r3, #0
	strbne	r1, [r5, #-144]
	strbne	r3, [r5, #-3119]
	bne	.L4309
	movw	r3, #2804
	ldrh	r2, [r4, r3]
	cmp	r6, r2
	bcs	.L4359
	cmp	r8, #0
	beq	.L4360
	cmp	r8, #16
	bls	.L4361
	ldr	r6, .L4535+60
	movw	r3, #2794
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, #-2]
	cmp	r2, r3
	bhi	.L4361
	mov	r2, #4
	mov	r0, r1
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4362
.L4523:
	ldrb	r0, [r5, #-3119]	@ zero_extendqisi2
	mov	r2, #4
	mov	r1, #3
.L4525:
	bl	gc_search_src_blk
	uxth	r0, r0
.L4363:
	cmp	r0, #0
	bne	.L4365
	b	.L4309
.L4339:
	ldrh	r3, [r5, #-136]
	ldrh	r2, [fp, #-8]
	cmp	r3, r2, lsr #5
	movcs	r3, #4
	strhcs	r3, [r5, #-136]	@ movhi
	b	.L4519
.L4335:
	movw	r3, #2804
	ldrh	r3, [r4, r3]
	cmp	r3, r6
	bcs	.L4519
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4435
	b	.L4519
.L4362:
	mov	r1, fp
	sub	r0, r6, #12
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4365
	ldr	r1, [r4, #1092]
	lsl	r2, r0, #1
	ldr	r3, [r5, #-140]
	ldrh	r2, [r1, r2]
	ldrh	r1, [r6, #-8]
	add	r3, r3, #1
	str	r3, [r5, #-140]
	cmp	r1, r2
	bcs	.L4366
	movw	r1, #1080
	ldrh	r1, [r4, r1]
	cmp	r3, r1, lsr #4
	bls	.L4365
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bls	.L4365
.L4366:
	ldrb	r2, [r5, #-3119]	@ zero_extendqisi2
	mov	r1, #0
	bl	gc_add_sblk
	mov	r3, #1
	str	r3, [r4, #2812]
	mov	r3, #0
	str	r3, [r5, #-140]
.L4365:
	mov	r3, #1
	b	.L4526
.L4361:
	mov	r2, #1
	mov	r1, #2
	mov	r0, r2
.L4529:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4365
	b	.L4523
.L4360:
	cmp	r6, #16
	strb	r8, [r5, #-3119]
	movhi	r2, #4
	movhi	r1, #3
	movhi	r0, r8
	bhi	.L4525
	mov	r2, r1
	mov	r0, r8
	b	.L4529
.L4359:
	cmp	r10, #1
	bne	.L4309
	cmp	r6, r2, lsl #1
	bge	.L4369
	cmp	r9, r8, lsr #1
	bcs	.L4370
	ldr	r2, .L4535+60
	movw	r1, #2794
	ldrh	r0, [r4, r1]
	ldrh	r1, [r2, #-2]
	cmp	r0, r1
	ldrhcc	r2, [r2, #-4]
	lsrcc	r2, r2, #2
	strhcc	r2, [r4, r3]	@ movhi
	bcc	.L4309
.L4370:
	mov	r0, #8
	bl	zftl_get_gc_node.part.10
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4371
	ldr	r2, [r4, #1092]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #3
	movhi	r3, #0
	movls	r3, #1
	cmp	r6, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4371
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r5, #-3119]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4365
.L4371:
	mov	r1, #0
	ldr	r0, .L4535+20
	bl	_list_get_gc_head_node
	movw	r3, #65535
	mov	fp, r0
	cmp	r0, r3
	ldr	r10, .L4535+60
	bne	.L4372
.L4377:
	cmp	r8, #1
	bhi	.L4373
.L4374:
	cmp	r6, r9
	bcs	.L4381
	mov	r6, #0
	mov	r0, #4
	strb	r6, [r5, #-3119]
	bl	zftl_get_gc_node.part.10
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4381
	ldr	r3, [r4, #1092]
	lsl	r0, r0, #1
	ldrh	r1, [r10, #-8]
	ldrh	r2, [r3, r0]
	ldrb	r3, [r5, #-3127]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asr #1
	ble	.L4383
.L4381:
	ldrh	r3, [r10, #-4]
	movw	r2, #2804
	lsr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
.L4309:
	mov	r0, r7
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4372:
	movw	r2, #1080
	ldr	r3, [r5, #-140]
	ldrh	r2, [r4, r2]
	add	r3, r3, #1
	str	r3, [r5, #-140]
	cmp	r3, r2, lsr #4
	lsl	r3, r0, #1
	str	r3, [sp, #24]
	bls	.L4375
	ldr	r3, [r4, #1092]
	lsl	r2, r0, #1
	mov	r1, #0
	str	r1, [r5, #-140]
	ldrh	r2, [r3, r2]
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r2, r3
	bcs	.L4375
	mov	r3, #1
	mov	r2, r3
	strb	r3, [r5, #-3119]
	str	r3, [sp, #24]
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	r3, [sp, #24]
	beq	.L4375
.L4526:
	strb	r3, [r5, #-144]
	b	.L4309
.L4375:
	ldr	r3, [r4, #1092]
	lsl	r2, fp, #1
	ldrh	r2, [r3, r2]
	ldrh	r3, [r10, #-8]
	cmp	r2, r3, lsr #1
	bhi	.L4376
	mov	r2, #0
	mov	r1, #1
	mov	r0, fp
	bl	gc_add_sblk
	b	.L4365
.L4376:
	movw	r3, #2790
	movw	r1, #2792
	ldrh	r1, [r4, r1]
	ldrh	r3, [r4, r3]
	add	r3, r3, r1
	ldrh	r1, [r10, #-4]
	cmp	r3, r1, lsl #1
	ble	.L4377
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bcc	.L4374
	b	.L4377
.L4373:
	cmp	r8, #16
	mov	r1, #1
	strb	r1, [r5, #-3119]
	bls	.L4378
	movw	r3, #2794
	ldrh	r2, [r4, r3]
	ldrh	r3, [r10, #-2]
	cmp	r2, r3
	movls	r2, #4
	movls	r0, r1
	bls	.L4531
.L4378:
	mov	r2, #1
	mov	r1, #2
	mov	r0, r2
.L4531:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4379
	mov	r2, #4
	mov	r1, #3
	ldrb	r0, [r5, #-3119]	@ zero_extendqisi2
	bl	gc_search_src_blk
	uxth	r0, r0
.L4379:
	ldrh	r3, [r10, #-4]
	cmp	r6, r9, lsr #1
	movw	r2, #2804
	lsrhi	r3, r3, #2
	lsrls	r3, r3, #1
.L4524:
	strh	r3, [r4, r2]	@ movhi
	b	.L4363
.L4383:
	mov	r2, #4
	mov	r1, #3
	mov	r0, r6
	bl	gc_search_src_blk
	ldrh	r3, [r10, #-4]
	uxth	r0, r0
	movw	r2, #2804
	lsr	r3, r3, #1
	b	.L4524
.L4369:
	ldr	r2, .L4535+60
	ldrh	r2, [r2, #-4]
	lsr	r2, r2, #2
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #2106
	ldr	r2, .L4535+40
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	moveq	r7, #0
	b	.L4309
.L4314:
	movw	r6, #2824
	movw	r3, #65535
	ldrh	r2, [r4, r6]
	cmp	r2, r3
	bne	.L4384
	bl	gc_get_src_blk
	strh	r0, [r4, r6]	@ movhi
.L4384:
	movw	r3, #2824
	movw	r1, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, r1
	beq	.L4385
	movw	r1, #1080
	ldrh	r1, [r4, r1]
	cmp	r1, r2
	mvnls	r2, #0
	strhls	r2, [r4, r3]	@ movhi
.L4385:
	movw	r3, #2824
	movw	r1, #65535
	ldrh	r3, [r4, r3]
	ldr	r2, .L4535+40
	cmp	r3, r1
	beq	.L4534
	ldrh	ip, [r2, #52]
	ldr	r1, [r4, #1084]
	cmp	ip, #0
	addne	r2, r2, #52
	movne	r0, #0
	add	r1, r1, r3, lsl #2
	bne	.L4388
.L4387:
	ldrb	r2, [r1, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r2, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r2, #224
	movne	r2, r1
	orreq	r2, r1, #1
	cmp	r2, #0
	beq	.L4390
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4392
	movw	r2, #3306
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
	b	.L4392
.L4388:
	uxth	lr, r0
	cmp	lr, ip
	bcs	.L4387
	ldrh	lr, [r2, #2]!
	add	r0, r0, #1
	cmp	lr, r3
	bne	.L4388
.L4392:
	mvn	r2, #0
	movw	r3, #2824
	strh	r2, [r4, r3]	@ movhi
	b	.L4435
.L4390:
	mov	r3, #2
	b	.L4533
.L4315:
	bl	gc_scan_src_blk
	cmn	r0, #1
	moveq	r3, #3
	beq	.L4533
	movw	r3, #2824
	ldr	r2, .L4535+40
	ldrh	r3, [r4, r3]
	movw	r1, #65535
	mov	r6, r2
	cmp	r3, r1
	beq	.L4334
	ldrh	r1, [r2, #20]
	cmp	r1, #0
	movne	r3, #4
	strbne	r3, [r5, #-144]
	movne	r3, #0
	strhne	r3, [r2, #22]	@ movhi
	bne	.L4435
.L4394:
	mov	r2, #1
	lsl	r3, r3, #1
	strb	r2, [r5, #-144]
	ldr	r2, [r4, #1092]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4395
	movw	r2, #3336
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
.L4395:
	movw	r5, #2824
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	ldrh	r3, [r4, r5]
	mov	r5, #0
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r6, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strhls	r3, [r6, #26]	@ movhi
	bls	.L4392
	strh	r5, [r6, #26]	@ movhi
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, r5
	bl	ftl_info_flush
	b	.L4392
.L4316:
	ldr	r7, .L4535+40
	mov	r8, r7
.L4489:
	bl	gc_scan_src_blk_one_page
	ldr	r3, .L4535+24
	ldrh	r2, [r7, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L4398
	cmp	r6, #7
	bls	.L4489
	b	.L4435
.L4398:
	ldrh	r3, [r7, #20]
	ldrh	r1, [r7]
	cmp	r3, #0
	beq	.L4399
	mov	r2, #4
	ldr	r0, [r4, #1092]
	strb	r2, [r5, #-144]
	mov	r2, #0
	strh	r2, [r7, #22]	@ movhi
	lsl	r2, r1, #1
	ldrh	r2, [r0, r2]
	cmp	r3, r2
	beq	.L4400
	ldr	r0, .L4535+8
	ldr	r0, [r0]
	tst	r0, #1024
	beq	.L4400
	ldr	r0, .L4535+28
	bl	printk
.L4400:
	movw	r3, #2824
	ldr	r2, [r4, #1092]
	ldrh	r3, [r4, r3]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r8, #20]
	cmp	r2, r3
	beq	.L4401
	movw	r2, #3379
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
.L4401:
	movw	r3, #2824
	ldrh	r1, [r8, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L4435
.L4399:
	mov	r3, #1
	ldr	r6, [r4, #1084]
	strb	r3, [r5, #-144]
	ldr	r3, .L4535+8
	add	r6, r6, r1, lsl #2
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4402
	ldrb	r2, [r6, #2]	@ zero_extendqisi2
	ldr	r0, .L4535+32
	lsr	r2, r2, #5
	bl	printk
.L4402:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	and	r3, r3, #192
	cmp	r3, #0
	cmpne	r2, #224
	bne	.L4403
	movw	r2, #3389
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
.L4403:
	movw	r5, #2824
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	mvn	r3, #0
	strh	r3, [r4, r5]	@ movhi
	ldrh	r3, [r8, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	movhi	r3, #0
	strhls	r3, [r8, #26]	@ movhi
	strhhi	r3, [r8, #26]	@ movhi
	bls	.L4435
.L4532:
	bl	flt_sys_flush
	b	.L4435
.L4317:
	cmp	r10, #0
	bne	.L4405
	movw	r3, #2804
	ldrh	r3, [r4, r3]
	cmp	r3, r6
	bcc	.L4435
.L4405:
	ldrh	r2, [r7, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4406
	ldrb	r8, [r5, #-3119]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L4406
	ldr	r9, .L4535+40
	bl	ftl_flush
	movw	r3, #2180
	mov	r1, #5
	ldrh	r0, [r9, r3]
	cmp	r0, #0
	movne	r0, r8
	bl	zftl_gc_get_free_sblk
	movw	r3, #65535
	mov	r6, r0
	cmp	r0, r3
	beq	.L4409
	ldr	r8, [r4, #1084]
	add	r8, r8, r0, lsl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L4410
	movw	r2, #3423
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
.L4410:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	and	r3, r3, #15
	orr	r3, r3, #176
	strb	r3, [r8, #2]
.L4434:
	mov	r1, #1
	mov	r0, r6
	bl	ftl_erase_sblk
	mov	r3, #5
	add	r1, r7, #96
	strb	r3, [r7, #84]
	mov	r0, r6
	bl	ftl_get_blk_list_in_sblk
	ldr	r2, .L4535+60
	uxtb	r0, r0
	mov	r8, #0
	strh	r6, [r7, #80]	@ movhi
	strb	r0, [r7, #89]
	mov	r1, #255
	ldrh	r3, [r2, #-8]
	strh	r8, [r7, #82]	@ movhi
	strb	r8, [r7, #85]
	strh	r8, [r7, #90]	@ movhi
	smulbb	r0, r3, r0
	ldrh	r3, [r2, #-30]
	strh	r0, [r7, #86]	@ movhi
	sub	r7, r2, #16
	ldrb	r2, [r5, #-3127]	@ zero_extendqisi2
	ldr	r0, [r5, #-132]
	mul	r2, r2, r3
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r3, [r7, #-14]
	mov	r1, #255
	ldrb	r2, [r5, #-3127]	@ zero_extendqisi2
	ldr	r0, [r5, #-128]
	mul	r2, r2, r3
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r3, [r7, #-14]
	mov	r1, #255
	ldrb	r2, [r5, #-3127]	@ zero_extendqisi2
	ldr	r0, [r5, #-3124]
	mvn	r5, #0
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, [r4, #1096]
	strh	r5, [r3, #128]	@ movhi
	strh	r5, [r3, #130]	@ movhi
	str	r6, [r3, #132]
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2800]
	movw	r2, #2182
	mov	r0, r8
	strh	r8, [r9, #52]	@ movhi
	strh	r6, [r3, #126]	@ movhi
	movw	r3, #2102
	strh	r8, [r9, r3]	@ movhi
	ldr	r3, .L4535+36
	strh	r8, [r9, r2]	@ movhi
	strh	r8, [r3]	@ movhi
	str	r5, [r3, #80]
	bl	ftl_info_flush
	b	.L4435
.L4536:
	.align	2
.L4535:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC299
	.word	.LANCHOR3-3104
	.word	.LANCHOR3-3100
	.word	.LANCHOR3-3096
	.word	.LC300
	.word	.LC301
	.word	.LANCHOR0+4928
	.word	.LANCHOR0+2824
	.word	.LANCHOR0+4096
	.word	1145785929
	.word	.LANCHOR3-3136
	.word	.LANCHOR0+1088
	.word	.LANCHOR3-3088
	.word	.LANCHOR1+2525
	.word	.LC0
.L4406:
	cmp	r10, #1
	movne	r8, #1
	moveq	r8, #4
	cmp	r6, #15
	ldr	r6, .L4535+40
	addls	r8, r8, #4
	add	r10, r6, #2176
	add	r10, r10, #6
.L4413:
	sub	r8, r8, #1
	uxtb	r8, r8
	cmp	r8, #255
	beq	.L4435
	bl	gc_do_copy_back
	ldrb	r3, [r5, #-3119]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4414
	ldrb	r3, [r4, #2769]	@ zero_extendqisi2
	cmp	r3, #3
	bhi	.L4415
	bl	ftl_write_commit
.L4415:
	ldrh	r2, [r6, #22]
	ldrh	r3, [r6, #20]
	cmp	r2, r3
	bcc	.L4413
	mov	r3, #1
	strb	r3, [r5, #-144]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	r3, [r6]
	ldr	r2, [r4, #1092]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4417
	movw	r2, #3507
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
.L4417:
	movw	r3, #2824
	ldr	r2, [r4, #1092]
	ldrh	r0, [r4, r3]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L4418
	bl	ftl_free_sblk
	b	.L4392
.L4418:
	mov	r2, #1
	mov	r1, #0
	bl	gc_add_sblk
	b	.L4392
.L4414:
	ldrh	r3, [r10]
	cmp	r3, #0
	beq	.L4419
	ldr	r8, .L4535+44
	mov	r3, #0
	strh	r3, [r10]	@ movhi
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	r0, [r8, #912]
	cmn	r0, #1
	beq	.L4420
	ldrb	r3, [r5, #-2542]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4421
	ldrb	r3, [r5, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4422
.L4421:
	ldr	r3, [r4, #2800]
	ldr	r2, [r3, #156]
	ldr	r3, .L4535+48
	cmp	r2, r3
	bne	.L4422
	ldr	r7, .L4535+52
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	ldrb	r1, [r5, #-3136]	@ zero_extendqisi2
	ldrh	r2, [r7, #-2]
	rsb	r3, r3, #24
	sub	r3, r3, r2
	lsr	r2, r0, r2
	mvn	r0, #0
	bic	r0, r2, r0, lsl r3
	bl	__aeabi_uidiv
	ldr	r3, [r4, #1084]
	lsl	lr, r0, #2
	add	ip, r3, lr
	ldrb	r2, [ip, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L4420
	ldrh	r1, [r9]
	ldrh	r7, [r7, #44]
	add	r1, r1, #8
	cmp	r1, r7
	bge	.L4420
	ldr	r1, .L4535+56
	bfc	r2, #3, #2
	strb	r2, [ip, #2]
	ldr	r2, [r3, r0, lsl #2]
	ldrh	r1, [r1]
	ldrh	ip, [r3, lr]
	ubfx	r2, r2, #11, #8
	mul	r1, r2, r1
	ubfx	r7, ip, #0, #11
	lsr	r2, r2, #3
	add	r1, r1, r1, lsl #1
	add	r1, r7, r1, asr #2
	bfi	ip, r1, #0, #11
	strh	ip, [r3, lr]	@ movhi
	ldr	r1, [r3, r0, lsl #2]
	bfi	r1, r2, #11, #8
	str	r1, [r3, r0, lsl #2]
.L4420:
	ldr	r3, [r4, #1096]
	mov	r7, #0
	str	r7, [r8, #912]
	strh	r7, [r6, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r0, [r4, #2832]
	mvn	r3, #0
	ldr	r2, [r4, #1096]
	ldr	r1, [r4, #2800]
	cmp	r0, r7
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	beq	.L4423
	bl	zbuf_free
.L4423:
	str	r7, [r4, #2832]
	bl	flt_sys_flush
	movw	r2, #3567
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	strb	r7, [r5, #-144]
	bl	printk
	bl	dump_stack
	b	.L4435
.L4422:
	bl	ftl_mask_bad_block
	b	.L4420
.L4419:
	ldrh	r3, [r7, #86]
	ldrh	r2, [r6, #22]
	cmp	r3, #1
	ldrh	r3, [r6, #20]
	bls	.L4424
	cmp	r2, r3
	bcc	.L4413
	mov	r3, #1
	strb	r3, [r5, #-144]
	ldrh	r3, [r6, #52]
	add	r2, r3, #1
	strh	r2, [r6, #52]	@ movhi
	add	r3, r6, r3, lsl #1
	ldrh	r2, [r6]
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
	b	.L4435
.L4424:
	cmp	r2, r3
	mov	r1, #5
	strb	r1, [r5, #-144]
	bcc	.L4425
	ldrh	r3, [r6, #52]
	add	r2, r3, #1
	strh	r2, [r6, #52]	@ movhi
	add	r3, r6, r3, lsl #1
	ldrh	r2, [r6]
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L4425:
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldrh	r2, [r7, #80]
	ldr	r3, [r4, #1096]
	strh	r2, [r3, #128]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r2, .L4535+60
	mov	r3, #0
	ldrb	r1, [r4, #1158]	@ zero_extendqisi2
	strh	r3, [r6, #12]	@ movhi
	ldrh	r3, [r2, #-8]
	cmp	r1, #0
	ldrhne	r2, [r2, #-30]
	strh	r3, [r6, #14]	@ movhi
	ldrb	r3, [r5, #-3128]	@ zero_extendqisi2
	strhne	r2, [r6, #14]	@ movhi
	movne	r2, #1
	strh	r3, [r6, #16]	@ movhi
	strhne	r2, [r6, #16]	@ movhi
	cmp	r3, #2
	bne	.L4428
	ldrh	r3, [r6, #14]
	lsl	r3, r3, #1
	strh	r3, [r6, #14]	@ movhi
	ldrb	r3, [r5, #-3126]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r3, #1
	strheq	r3, [r6, #16]	@ movhi
.L4428:
	mov	r3, #0
	strh	r3, [r6, #18]	@ movhi
	b	.L4435
.L4431:
	ldrh	r2, [r8, #12]
	ldrh	r3, [r8, #14]
	cmp	r2, r3
	bcc	.L4432
	mov	r3, #6
	ldr	r0, [r4, #2832]
	strb	r3, [r5, #-144]
	bl	zbuf_free
	str	r9, [r4, #2832]
	b	.L4435
.L4432:
	cmp	r6, #15
	bls	.L4320
	cmp	r10, #1
	bne	.L4435
	add	r7, r7, #1
	uxtb	r7, r7
	cmp	r7, #4
	bls	.L4320
	b	.L4435
.L4319:
	bl	gc_update_l2p_map_new
	bl	gc_free_src_blk
	bl	ftl_flush
	mvn	r6, #0
	bl	pm_flush
	strh	r6, [r7, #80]	@ movhi
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2800]
	mov	r0, #0
	strh	r6, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L4534:
	mov	r3, #0
	b	.L4533
.L4409:
	movw	r2, #3430
	ldr	r1, .L4535+64
	ldr	r0, .L4535+68
	bl	printk
	bl	dump_stack
	b	.L4434
	.fnend
	.size	zftl_do_gc, .-zftl_do_gc
	.align	2
	.global	zftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_init, %function
zftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mvn	r3, #0
	ldr	r5, .L4617
	mov	r6, #0
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L4617+4
	ldr	r9, .L4617+8
	ldr	r1, .L4617+12
	ldr	r0, .L4617+16
	strb	r3, [r4, #2820]
	strb	r3, [r5, #-2536]
	strb	r3, [r5, #-88]
	str	r3, [r5]
	strb	r6, [r4, #2796]
	strb	r6, [r5, #-2535]
	strb	r6, [r4, #2797]
	bl	printk
	ldrb	r3, [r9, #16]	@ zero_extendqisi2
	sub	r2, r5, #3104
	ldrb	r8, [r9, #13]	@ zero_extendqisi2
	str	r3, [sp, #4]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	lsl	r10, r8, #9
	strb	r8, [r5, #-2546]
	strb	r3, [r5, #-3128]
	uxth	r10, r10
	ldrh	r3, [r9, #14]
	strh	r3, [r2, #-14]	@ movhi
	mov	r0, r3
	str	r3, [sp, #12]
	bl	__aeabi_idiv
	str	r0, [sp, #8]
	sub	r2, r5, #3088
	ldrh	r3, [sp, #8]
	mov	r1, #1
	ldrb	r7, [r4, #1109]	@ zero_extendqisi2
	ldrh	fp, [r9, #18]
	strh	r3, [r2, #-8]	@ movhi
	ldrb	r2, [r9, #17]	@ zero_extendqisi2
	strb	r7, [r5, #-3072]
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #2]
	smulbb	r7, r7, r2
	strb	r2, [r5, #-3136]
	movw	r2, #1080
	strh	r10, [r5, #-14]	@ movhi
	strh	fp, [r4, r2]	@ movhi
	sub	r2, r5, #3072
	uxtb	r7, r7
	strh	r0, [r2, #-2]	@ movhi
	strb	r7, [r5, #-3127]
.L4538:
	cmp	r0, r1
	uxth	r2, r6
	add	r6, r6, #1
	bcs	.L4539
	ldr	r1, .L4617+20
	sub	r2, r2, #1
	mov	ip, #0
	mov	r0, #1
	strh	r2, [r1, #-2]	@ movhi
	mul	r2, r3, r8
	mul	r1, r2, fp
	str	r2, [sp, #12]
	lsr	lr, r1, #21
.L4540:
	cmp	lr, r0
	uxth	r2, ip
	add	ip, ip, #1
	bcs	.L4541
	ldr	r9, .L4617+24
	sub	r2, r2, #1
	uxth	r2, r2
	mul	r1, r7, r1
	ldr	r6, .L4617
	lsr	fp, fp, #4
	str	r3, [sp, #20]
	mul	r0, r9, r7
	str	r1, [r4, #2776]
	mov	r1, r8
	sub	r8, r6, #3088
	lsl	r9, r0, r2
	add	r2, r9, #24576
	str	r9, [r4, #1032]
	str	r2, [r6, #4]
	mov	r0, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidiv
	sub	ip, r10, #1
	str	r0, [r4, #2780]
	mov	r1, r10
	add	r0, ip, r0, lsl #2
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	strh	r0, [r6, #8]	@ movhi
	uxth	r0, r0
	lsl	r0, r0, #4
	mul	r1, r7, r3
	bl	__aeabi_idiv
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #16]
	strh	r0, [r6, #-176]	@ movhi
	mul	r1, r7, r3
	mov	r0, r2
	sub	r1, r1, #1
	bl	__aeabi_uidiv
	cmp	fp, #79
	strh	fp, [r8, #-4]	@ movhi
	movls	r2, #80
	mov	r1, #2000
	strhls	r2, [r8, #-4]	@ movhi
	add	r0, r0, #8
	ldr	r2, .L4617+28
	strh	r0, [r8, #-2]	@ movhi
	mov	r0, #32
	ldr	r3, [sp, #20]
	strh	r1, [r2, #-10]	@ movhi
	mov	r1, #50
	strh	r1, [r2, #-12]	@ movhi
	mov	r1, #256
	strh	r1, [r6, #-180]	@ movhi
	mov	r1, #48
	strh	r1, [r6, #-178]	@ movhi
	ldr	r1, .L4617+32
	strh	r0, [r1]	@ movhi
	ldr	r1, [sp, #4]
	cmp	r1, #2
	mov	r1, r2
	beq	.L4543
	ldrb	ip, [r6, #-3120]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L4544
.L4543:
	mov	r2, #150
	mov	r0, #12
	strh	r2, [r1, #-12]	@ movhi
	mov	r2, #64
	strh	r2, [r6, #-178]	@ movhi
	ldr	r2, .L4617+32
	strh	r0, [r2]	@ movhi
	ldrb	r0, [r4]	@ zero_extendqisi2
	cmp	r0, #0
	moveq	r0, #4
	strheq	r0, [r2]	@ movhi
	moveq	r2, #600
	strheq	r2, [r1, #-10]	@ movhi
	moveq	r2, #128
	strheq	r2, [r6, #-180]	@ movhi
	ldrb	r2, [r4, #1159]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, #200
	strhne	r2, [r1, #-12]	@ movhi
	movne	r2, #2000
	strhne	r2, [r1, #-10]	@ movhi
.L4547:
	mul	r7, r7, r3
	mov	r2, #0
	str	r2, [r4, #2812]
	mov	r2, #1
	strb	r2, [r6, #-11]
	cmp	r10, r7, lsl #2
	ldr	r7, .L4617+8
	movlt	r3, #2
	strblt	r3, [r6, #-11]
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4550
	mov	r1, r9
	ldr	r0, .L4617+36
	bl	printk
.L4550:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4551
	ldr	r1, [r4, #2776]
	ldr	r0, .L4617+40
	bl	printk
.L4551:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4552
	ldr	r1, [r4, #2780]
	ldr	r0, .L4617+44
	bl	printk
.L4552:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4553
	ldr	r1, [r6, #4]
	ldr	r0, .L4617+48
	bl	printk
.L4553:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4554
	ldrh	r1, [r6, #8]
	ldr	r0, .L4617+52
	bl	printk
.L4554:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4555
	ldrh	r1, [r6, #-14]
	ldr	r0, .L4617+56
	bl	printk
.L4555:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4556
	ldrh	r1, [r6, #-176]
	ldr	r0, .L4617+60
	bl	printk
.L4556:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4557
	ldrh	r1, [r8, #-4]
	ldr	r0, .L4617+64
	bl	printk
.L4557:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4558
	ldrh	r1, [r8, #-2]
	ldr	r0, .L4617+68
	bl	printk
.L4558:
	bl	zbuf_init
	mov	r0, #16384
	movw	r9, #1080
	bl	ftl_malloc
	str	r0, [r5, #-52]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-44]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #12]
	mov	r0, #256
	bl	ftl_dma32_malloc
	str	r0, [r5, #-76]
	mov	r0, #256
	bl	ftl_dma32_malloc
	ldrh	r3, [r4, r9]
	str	r0, [r5, #-40]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_dma32_malloc
	ldrh	r3, [r8, #-8]
	str	r0, [r4, #1036]
	ldrb	r0, [r5, #-3127]	@ zero_extendqisi2
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_dma32_malloc
	ldrh	r1, [r8, #-8]
	str	r0, [r5, #-2552]
	ldrb	r0, [r5, #-3127]	@ zero_extendqisi2
	ldr	r2, [r5, #-44]
	ldrh	ip, [r4, r9]
	mul	r0, r0, r1
	str	r2, [r5, #-2556]
	lsl	r1, r0, #1
	add	r3, r2, r0, lsl #3
	ldr	r0, [r7]
	add	r1, r1, ip, lsr #1
	str	r3, [r4, #1092]
	add	r1, r2, r1, lsl #2
	tst	r0, #4096
	str	r1, [r4, #1096]
	beq	.L4559
	ldr	r0, .L4617+72
	bl	printk
.L4559:
	ldrh	r2, [r8, #-8]
	movw	r3, #1080
	ldrb	r5, [r6, #-3127]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	mul	r5, r5, r2
	ldrh	r2, [r6, #8]
	lsl	r8, r3, #2
	add	r5, r3, r5, lsl #2
	ldr	r3, [r7]
	add	r8, r8, r2, lsl #2
	lsl	r5, r5, #1
	tst	r3, #4096
	add	r5, r5, #632
	add	r8, r8, #704
	beq	.L4560
	ldrh	r3, [r6, #-14]
	mov	r2, r8
	mov	r1, r5
	ldr	r0, .L4617+76
	bl	printk
.L4560:
	ldrh	r1, [r6, #-14]
	cmp	r8, r1
	cmpls	r5, r1
	movhi	r5, #1
	movls	r5, #0
	bls	.L4561
.L4615:
	b	.L4615
.L4539:
	lsl	r1, r1, #1
	b	.L4538
.L4541:
	lsl	r0, r0, #1
	b	.L4540
.L4544:
	ldrb	r1, [r4, #1158]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, #1200
	strhne	r0, [r2, #-12]	@ movhi
	strhne	r1, [r2, #-10]	@ movhi
	strhne	r0, [r6, #-178]	@ movhi
	b	.L4547
.L4561:
	bl	sblk_init
	bl	gc_init
	bl	ftl_info_blk_init
	cmn	r0, #1
	beq	.L4537
	bl	ftl_ext_info_init
	mov	r0, #1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	r0, [r4, #1096]
	mov	r1, r5
	add	r0, r0, #16
	bl	ftl_open_sblk_recovery
	ldr	r0, [r4, #1096]
	add	r1, r0, #16
	add	r0, r0, #48
	bl	ftl_open_sblk_recovery
	ldr	r2, [r4, #2800]
	ldr	r0, [r4, #1096]
	ldr	r3, [r2, #8]
	add	r0, r0, #16
	add	r3, r3, #16
	str	r3, [r2, #8]
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #1096]
	add	r0, r0, #48
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #1096]
	add	r0, r0, #80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	r0, #1
	bl	ftl_total_vpn_update
	ldrb	r3, [r6, #-47]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r2, [r4, #2800]
	ldrne	r3, [r2, #68]
	addne	r3, r3, #1
	strne	r3, [r2, #68]
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4571
	ldr	r3, .L4617+80
	movw	r2, #2788
	ldrh	r2, [r4, r2]
	ldrh	r3, [r3]
	add	r3, r3, r2
	cmp	r3, #7
	ble	.L4571
.L4567:
	mov	r0, #0
.L4537:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4571:
	ldr	r6, .L4617+80
	mov	r5, #16384
.L4568:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r1, #1
	mov	r0, r1
	bl	zftl_do_gc
	ldr	r3, [r4, #1096]
	ldrh	r2, [r3, #124]
	cmp	r2, #0
	bne	.L4566
	ldrh	r2, [r3, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4566
	ldr	r2, .L4617+84
	ldrh	r3, [r6]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4567
.L4566:
	subs	r5, r5, #1
	bne	.L4568
	b	.L4567
.L4618:
	.align	2
.L4617:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR3-3136
	.word	1892352
	.word	.LANCHOR3-2528
	.word	.LANCHOR0+1088
	.word	.LC302
	.word	.LC303
	.word	.LC304
	.word	.LC305
	.word	.LC306
	.word	.LC307
	.word	.LC308
	.word	.LC309
	.word	.LC310
	.word	.LC311
	.word	.LC312
	.word	.LANCHOR0+2784
	.word	.LANCHOR0+2788
	.fnend
	.size	zftl_init, .-zftl_init
	.align	2
	.global	rk_ftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4634
	mov	r0, #68
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, #0
	ldr	r4, .L4634+4
	ldr	r3, [r3]
	str	r5, [r4, #-164]
	strb	r5, [r4, #16]
	str	r3, [r4, #-168]
	str	r5, [r4, #-152]
	bl	ftl_dma32_malloc
	cmp	r0, r5
	str	r0, [r4, #-160]
	bne	.L4620
.L4622:
	mvn	r5, #0
.L4619:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L4620:
	mov	r0, #2048
	bl	ftl_dma32_malloc
	mov	r1, r4
	str	r0, [r4, #20]
	str	r5, [r1, #24]!
	sub	r0, r4, #156
	str	r5, [r4, #-156]
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #-156]
	cmp	r3, #0
	beq	.L4622
	bl	rk_nandc_irq_init
	mov	r3, #2048
	mov	r2, r5
	mov	r1, r5
	ldr	r0, [r4, #20]
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	r3, [r4, #-160]
	ldr	r2, .L4634+8
	ldr	r0, [r4, #-156]
	str	r2, [r3, #40]
	ldr	r2, .L4634+12
	str	r2, [r3, #32]
	ldr	r2, .L4634+16
	str	r2, [r3, #44]
	ldr	r2, .L4634+20
	str	r2, [r3, #48]
	ldr	r2, .L4634+24
	str	r2, [r3, #36]
	ldr	r2, .L4634+28
	str	r2, [r3, #56]
	ldr	r2, .L4634+32
	str	r2, [r3, #20]
	ldr	r2, .L4634+36
	str	r2, [r3, #24]
	ldr	r2, .L4634+40
	str	r2, [r3, #12]
	ldr	r2, .L4634+44
	str	r2, [r3, #16]
	ldr	r2, .L4634+48
	str	r2, [r3, #4]
	ldr	r2, .L4634+52
	str	r2, [r3, #8]
	ldr	r2, .L4634+56
	str	r2, [r3, #60]
	ldr	r2, .L4634+60
	str	r2, [r3, #64]
	ldr	r2, .L4634+64
	str	r2, [r3, #52]
	ldr	r2, .L4634+68
	str	r2, [r3, #28]
	bl	nand_flash_init
	subs	r7, r0, #0
	bne	.L4623
	bl	zftl_init
	mov	r5, r0
	bl	zftl_proc_debug_init
	mov	r3, #1
	strb	r3, [r4, #16]
.L4624:
	mov	r1, r5
	ldr	r0, .L4634+72
	bl	printk
	b	.L4619
.L4623:
	ldr	r3, [r4, #-160]
	mov	r6, r4
	ldr	r2, .L4634+76
	ldr	r0, [r4, #-156]
	ldr	r4, .L4634+80
	str	r2, [r3, #40]
	ldr	r2, .L4634+84
	str	r2, [r3, #32]
	ldr	r2, .L4634+88
	str	r2, [r3, #44]
	ldr	r2, .L4634+92
	str	r2, [r3, #48]
	ldr	r2, .L4634+96
	str	r2, [r3, #36]
	ldr	r2, .L4634+100
	str	r2, [r3, #56]
	ldr	r2, .L4634+104
	str	r2, [r3, #20]
	ldr	r2, .L4634+108
	str	r2, [r3, #24]
	ldr	r2, .L4634+112
	str	r2, [r3, #12]
	ldr	r2, .L4634+116
	str	r2, [r3, #16]
	ldr	r2, .L4634+120
	str	r2, [r3, #4]
	ldr	r2, .L4634+124
	str	r2, [r3, #8]
	ldr	r2, .L4634+128
	str	r2, [r3, #60]
	ldr	r2, .L4634+132
	str	r2, [r3, #64]
	ldr	r2, .L4634+136
	str	r2, [r3, #52]
	ldr	r2, .L4634+140
	str	r2, [r3, #28]
	bl	FlashInit
	cmn	r7, #2
	mov	r5, r0
	bne	.L4625
	mov	r2, #32
	ldr	r1, .L4634+144
	add	r0, r4, #4
	bl	ftl_memcpy
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrh	r3, [r4, #14]
	strh	r3, [r4, #30]	@ movhi
.L4625:
	ldr	r3, .L4634+148
	ldr	r2, [r3]
	ldr	r3, .L4634+152
	cmp	r2, r3
	ldr	r2, .L4634+156
	bne	.L4626
	ldr	r3, .L4634+160
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4627
.L4626:
	mov	r3, #0
	strb	r3, [r2, #1110]
	strb	r3, [r2]
.L4627:
	ldr	r1, .L4634+164
	mov	r0, #1
	ldrh	ip, [r4, #14]
	mov	r3, #0
	str	r3, [r6, #-100]
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1108]
	ldr	r1, .L4634+168
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1193]
.L4628:
	cmp	ip, r0
	uxth	r1, r3
	add	r3, r3, #1
	bcs	.L4629
	sub	r3, r1, #1
	ldr	r1, .L4634+172
	strh	r3, [r1, #-2]	@ movhi
	mov	r3, #0
	cmp	r5, r3
	strb	r3, [r2, #1143]
	bne	.L4624
	bl	FtlInit
	mov	r5, r0
	b	.L4624
.L4629:
	lsl	r0, r0, #1
	b	.L4628
.L4635:
	.align	2
.L4634:
	.word	jiffies
	.word	.LANCHOR3
	.word	zftl_deinit
	.word	zftl_cache_flush
	.word	zftl_flash_suspend
	.word	zftl_flash_resume
	.word	zftl_get_density
	.word	zftl_read_flash_info
	.word	zftl_read
	.word	zftl_write
	.word	zftl_sys_read
	.word	zftl_sys_write
	.word	zftl_vendor_read
	.word	zftl_vendor_write
	.word	zftl_nandc_get_irq_status
	.word	zftl_proc_ftl_read
	.word	zftl_do_gc
	.word	zftl_discard
	.word	.LC313
	.word	ftl_deinit
	.word	.LANCHOR2
	.word	ftl_cache_flush
	.word	ftl_flash_suspend
	.word	ftl_flash_resume
	.word	ftl_get_density
	.word	ftl_read_flash_info
	.word	ftl_read
	.word	ftl_write
	.word	ftl_sys_read
	.word	ftl_sys_write
	.word	ftl_vendor_read
	.word	ftl_vendor_write
	.word	ftl_nandc_get_irq_status
	.word	ftl_proc_ftl_read
	.word	ftl_do_gc
	.word	ftl_discard
	.word	gNandParaInfo
	.word	g_nandc_version_data
	.word	1446522928
	.word	.LANCHOR0
	.word	gFlashSlcMode
	.word	gNandFlashIDBEccBits
	.word	gNandFlashEccBits
	.word	.LANCHOR3-3136
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	zftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_write, %function
zftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L4666
	.pad #36
	sub	sp, sp, #36
	mov	r4, r0
	mov	r6, r1
	mov	r5, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4637
	ldr	r3, [r10]
	str	r3, [sp]
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	ldr	r0, .L4666+4
	bl	printk
.L4637:
	cmp	r4, #0
	bne	.L4638
	ldr	r3, .L4666+8
	mov	r4, #24576
	ldr	r2, [r3, #1032]
.L4639:
	cmp	r2, r5
	cmpcs	r2, r6
	movls	fp, #1
	movhi	fp, #0
	bls	.L4657
	add	r1, r6, r5
	cmp	r2, r1
	bcc	.L4657
	ldr	r8, .L4666+12
	add	r4, r4, r6
	mov	r0, r4
	ldrb	r6, [r8, #-2546]	@ zero_extendqisi2
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r9, r0
	sub	r0, r5, #1
	mov	r1, r6
	add	r0, r0, r4
	bl	__aeabi_uidiv
	str	r8, [sp, #16]
	sub	r7, r0, r9
	ldr	r8, .L4666+8
	add	r7, r7, #1
	mov	r6, r9
	add	r3, r4, r5
	str	r0, [sp, #12]
	str	fp, [sp, #20]
	str	r3, [sp, #24]
.L4641:
	cmp	r7, #0
	bne	.L4650
	bl	ftl_write_commit
	mov	r1, #1
	mov	r0, r7
	bl	zftl_do_gc
	ldr	r3, [r8, #1096]
	ldr	r4, .L4666+16
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4651
	movw	r2, #2788
	ldrh	r3, [r4]
	ldrh	r2, [r8, r2]
	add	r3, r3, r2
	cmp	r3, #11
	bgt	.L4652
.L4651:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
.L4652:
	ldr	r5, .L4666+20
.L4653:
	ldrh	r3, [r4]
	ldrh	r2, [r5]
	add	r3, r3, r2
	cmp	r3, #7
	ble	.L4654
	bl	timer_get_time
	ldr	r3, [sp, #16]
	str	r0, [r3, #-4]
	mov	r0, #0
.L4636:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4638:
	cmp	r4, #3
	bhi	.L4657
	lsl	r4, r4, #13
	mov	r2, #8192
	b	.L4639
.L4650:
	ldrb	r3, [r8, #2796]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4642
	ldrb	r3, [r8, #2769]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4642
	bl	ftl_write_commit
.L4642:
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	bne	.L4643
	bl	ftl_write_commit
	b	.L4641
.L4643:
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [fp, #41]
	ldr	r3, [sp, #16]
	ldrb	r2, [r3, #-2546]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	strb	r2, [fp, #40]
	cmp	r6, r3
	cmpne	r6, r9
	bne	.L4646
	cmp	r6, r9
	smulbbne	r2, r6, r2
	ldrne	r3, [sp, #24]
	subne	r2, r3, r2
	bne	.L4665
	mov	r1, r2
	mov	r0, r4
	str	r2, [sp, #28]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #28]
	uxtb	r1, r1
	strb	r1, [fp, #41]
	sub	r2, r2, r1
	uxtb	r2, r2
	cmp	r5, r2
	strbcc	r5, [fp, #40]
	bcc	.L4646
.L4665:
	strb	r2, [fp, #40]
.L4646:
	ldrb	ip, [fp, #41]	@ zero_extendqisi2
	mov	r1, r10
	ldrb	r2, [fp, #40]	@ zero_extendqisi2
	sub	r7, r7, #1
	ldr	r0, [fp, #4]
	lsl	r2, r2, #9
	add	r0, r0, ip, lsl #9
	bl	ftl_memcpy
	ldr	r1, [r8, #2800]
	str	r6, [fp, #20]
	add	r6, r6, #1
	ldr	r2, [r1, #8]
	add	r0, r2, #1
	str	r0, [r1, #8]
	mov	r0, fp
	str	r2, [fp, #16]
	bl	ftl_write_buf
	ldrb	r3, [fp, #40]	@ zero_extendqisi2
	add	r10, r10, r3, lsl #9
	b	.L4641
.L4654:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r1, #1
	mov	r0, r1
	bl	zftl_do_gc
	b	.L4653
.L4657:
	mvn	r0, #0
	b	.L4636
.L4667:
	.align	2
.L4666:
	.word	.LANCHOR2
	.word	.LC314
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+2784
	.word	.LANCHOR0+2788
	.fnend
	.size	zftl_write, .-zftl_write
	.align	2
	.global	zftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_vendor_write, %function
zftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	add	r1, r0, #512
	mov	r0, #2
	b	zftl_write
	.fnend
	.size	zftl_vendor_write, .-zftl_vendor_write
	.align	2
	.global	zftl_sys_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_sys_write, %function
zftl_sys_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #2
	b	zftl_write
	.fnend
	.size	zftl_sys_write, .-zftl_sys_write
	.align	2
	.global	zftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	zftl_discard, %function
zftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r7, .L4720
	ldr	r3, [r7, #1032]
	cmp	r1, r3
	cmpls	r0, r3
	movcs	r2, #1
	movcc	r2, #0
	bcs	.L4692
	mov	r4, r1
	add	r1, r0, r1
	cmp	r3, r1
	bcc	.L4692
	ldr	r3, .L4720+4
	add	r8, r0, #24576
	ldr	r6, .L4720+8
	ldr	r0, [r3]
	ldr	r1, [r6, #28]
	str	r3, [sp, #16]
	tst	r0, #4096
	add	r1, r4, r1
	str	r1, [r6, #28]
	beq	.L4672
	str	r2, [sp]
	mov	r3, r4
	mov	r2, r8
	ldr	r0, .L4720+12
	bl	printk
.L4672:
	ldr	r3, [r7, #2800]
	ldr	r10, [r3, #8]
	add	r2, r10, #1
	str	r2, [r3, #8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	r9, [r6, #-2546]	@ zero_extendqisi2
	mov	r0, r8
	mov	r1, r9
	bl	__aeabi_uidiv
	mov	r1, r9
	mov	r5, r0
	mov	r0, r8
	bl	__aeabi_uidivmod
	subs	fp, r1, #0
	beq	.L4673
	sub	r9, r9, fp
	mov	r0, r5
	cmp	r9, r4
	movcs	r9, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4674
	mov	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	pm_log2phys
.L4674:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	uxth	r3, r9
	str	r3, [sp, #12]
	beq	.L4676
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4676
	ldr	r0, [r3, #4]
	mov	r1, #0
	str	r5, [r3, #20]
	strb	fp, [r3, #41]
	strb	r9, [r3, #40]
	str	r10, [r3, #16]
	add	r0, r0, fp, lsl #9
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	lsl	r2, r3, #9
	bl	ftl_memset
	ldr	r3, [sp, #20]
	mov	r0, r3
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r7, #2800]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4676:
	ldr	r3, [sp, #12]
	add	r5, r5, #1
	sub	r4, r4, r3
.L4673:
	cmp	r4, #0
	beq	.L4678
	bl	ftl_flush
.L4678:
	mov	r9, #0
	mvn	r3, #0
	str	r3, [sp, #28]
.L4679:
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L4684
	cmp	r4, #0
	beq	.L4686
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4687
	mov	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	pm_log2phys
.L4687:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4686
	mov	r0, #0
	bl	buf_alloc
	subs	r9, r0, #0
	beq	.L4686
	mov	r3, #0
	str	r5, [r9, #20]
	strb	r3, [r9, #41]
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	strb	r4, [r9, #40]
	str	r10, [r9, #16]
	cmp	r4, r3
	bcc	.L4689
	movw	r2, #1496
	ldr	r1, .L4720+16
	ldr	r0, .L4720+20
	bl	printk
	bl	dump_stack
.L4689:
	lsl	r2, r4, #9
	mov	r1, #0
	ldr	r0, [r9, #4]
	bl	ftl_memset
	mov	r0, r9
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r7, #2800]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4686:
	ldr	r1, [r6, #28]
	cmp	r1, #8192
	bls	.L4693
	ldr	r3, [sp, #16]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4690
	mov	r3, #0
	mov	r2, r8
	str	r3, [sp]
	mov	r3, r4
	ldr	r0, .L4720+12
	bl	printk
.L4690:
	mov	r4, #0
	str	r4, [r6, #28]
	bl	flt_sys_flush
	mov	r3, #1
	str	r3, [r7, #2812]
.L4693:
	mov	r0, #0
	b	.L4670
.L4684:
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	beq	.L4680
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	beq	.L4682
	ldrb	r2, [r6, #-2546]	@ zero_extendqisi2
	mov	r1, #0
	str	r5, [fp, #20]
	strb	r9, [fp, #41]
	strb	r2, [fp, #40]
	str	r10, [fp, #16]
	lsl	r2, r2, #9
	ldr	r0, [fp, #4]
	bl	ftl_memset
	mov	r0, fp
	bl	ftl_write_buf
	bl	ftl_write_commit
.L4719:
	ldr	r2, [r7, #2800]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4682:
	ldrb	r3, [r6, #-2546]	@ zero_extendqisi2
	add	r5, r5, #1
	sub	r4, r4, r3
	b	.L4679
.L4680:
	mov	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	pm_log2phys
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4682
	mov	r2, #1
	add	r1, sp, #28
	mov	r0, r5
	bl	pm_log2phys
	ldr	r3, .L4720+24
	ldr	r2, [sp, #24]
	ldrb	r1, [r6, #-3136]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r7, #1153]	@ zero_extendqisi2
	lsr	r2, r2, r0
	rsb	r3, r3, #24
	sub	r3, r3, r0
	mvn	r0, #0
	bic	r0, r2, r0, lsl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L4719
.L4692:
	mvn	r0, #0
.L4670:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4721:
	.align	2
.L4720:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC315
	.word	.LANCHOR1+2536
	.word	.LC0
	.word	.LANCHOR3-3138
	.fnend
	.size	zftl_discard, .-zftl_discard
	.align	2
	.global	dump_pm_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_pm_blk, %function
dump_pm_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #698
	ldr	r4, .L4727
	mov	r2, #4
	mov	r5, #0
	movw	r7, #65535
	ldr	r0, .L4727+4
	ldr	r1, [r4, #2800]
	ldr	r6, .L4727+8
	ldrh	r3, [r1, r3]
	add	r1, r1, #704
	bl	rknand_print_hex
	ldr	r1, [r4, #2800]
	mov	r2, #2
	ldrh	r3, [r6, #-176]
	ldr	r0, .L4727+12
	add	r1, r1, #416
	bl	rknand_print_hex
.L4723:
	ldrh	r2, [r6, #-176]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L4725
	pop	{r4, r5, r6, r7, r8, pc}
.L4725:
	uxth	r3, r5
	ldr	r2, [r4, #2800]
	add	r3, r3, #208
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, r7
	beq	.L4724
	mov	r1, #0
	bl	ftl_sblk_dump
.L4724:
	add	r5, r5, #1
	b	.L4723
.L4728:
	.align	2
.L4727:
	.word	.LANCHOR0
	.word	.LC267
	.word	.LANCHOR3
	.word	.LC268
	.fnend
	.size	dump_pm_blk, .-dump_pm_blk
	.align	2
	.global	id_block_prog_msb_ff_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	id_block_prog_msb_ff_data, %function
id_block_prog_msb_ff_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L4736
	ldrb	r3, [r5, #1110]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4729
	ldr	r3, [r5, #1104]
	mov	r4, r2
	mov	r10, r0
	mov	fp, r1
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r2, r3, #5
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L4731
	sub	r3, r3, #19
	tst	r3, #239
	bne	.L4729
.L4731:
	ldr	r6, .L4736+4
	sub	r7, r6, #2272
.L4733:
	ldr	r3, [r5, #1104]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L4734
.L4729:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4734:
	lsl	r9, r4, #1
	add	r8, r4, fp
	mov	r2, r8
	ldr	r0, .L4736+8
	ldrh	r3, [r7, r9]
	mov	r1, r4
	bl	printk
	ldrh	r2, [r7, r9]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4729
	mov	r2, #16384
	mov	r1, #255
	ldr	r0, [r6, #-120]
	add	r4, r4, #1
	bl	ftl_memset
	ldr	r3, [r5, #1104]
	mov	r1, r8
	mov	r0, r10
	uxth	r4, r4
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r6, #-120]
	mov	r2, r3
	bl	flash_prog_page
	b	.L4733
.L4737:
	.align	2
.L4736:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC316
	.fnend
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 160
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4848
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #180
	sub	sp, sp, #180
	ldr	lr, [r1]
	ldrb	ip, [r3]	@ zero_extendqisi2
	ldr	r4, .L4848+4
	ldr	r10, .L4848+8
	str	ip, [sp, #76]
	ldrh	ip, [sp, #76]
	ldr	r9, .L4848+12
	cmp	lr, r4
	cmpne	lr, r10
	str	ip, [sp, #40]
	movne	ip, #1
	moveq	ip, #0
	cmp	lr, r9
	moveq	ip, #0
	andne	ip, ip, #1
	cmp	ip, #0
	beq	.L4739
.L4847:
	mvn	r0, #0
.L4738:
	add	sp, sp, #180
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4739:
	cmp	r0, #15
	bls	.L4847
	mov	r5, r2
	ldr	r2, [r3, #1104]
	mov	fp, r0
	mov	r0, #256000
	str	r3, [sp, #16]
	ldrb	r6, [r2, #9]	@ zero_extendqisi2
	ldrh	r8, [r2, #10]
	ldr	r2, .L4848+16
	str	r1, [sp, #32]
	ldrh	r7, [r2, #30]
	ldrb	r2, [r3, #1110]	@ zero_extendqisi2
	str	r2, [sp, #96]
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #20]
	beq	.L4847
	ldr	r2, .L4848+20
	ldrb	r2, [r2, #16]	@ zero_extendqisi2
	cmp	r2, #0
	streq	r2, [sp, #16]
	beq	.L4742
	ldr	r3, [sp, #16]
	ldrb	r2, [r3]	@ zero_extendqisi2
	ldrb	r3, [r3, #1110]	@ zero_extendqisi2
	cmp	r2, #0
	clzne	r3, r3
	lsrne	r3, r3, #5
	bne	.L4844
	cmp	r3, #3
	streq	r3, [sp, #40]
	moveq	r3, #2
	beq	.L4844
	cmp	r3, #2
	ldr	r3, [sp, #40]
	moveq	r3, #2
	str	r3, [sp, #40]
	movne	r3, #0
	moveq	r3, #3
.L4844:
	str	r3, [sp, #16]
.L4742:
	add	fp, fp, #508
	add	fp, fp, #3
	lsr	fp, fp, #9
	cmp	fp, #8
	bls	.L4798
	cmp	fp, #500
	bhi	.L4745
.L4744:
	ldr	r3, [sp, #32]
	ldr	r3, [r3]
	cmp	r3, r4
	cmpne	r3, r10
	movne	r4, #1
	moveq	r4, #0
	cmp	r3, r9
	moveq	r9, #0
	andne	r9, r4, #1
	cmp	r9, #0
	beq	.L4746
.L4745:
	ldr	r0, [sp, #20]
	bl	ftl_free
	b	.L4847
.L4798:
	mov	fp, #8
	b	.L4744
.L4746:
	mov	r0, r9
	mul	r8, r6, r8
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4848
	sub	r0, r8, #1
	mov	r1, r8
	add	r0, r0, fp
	ldrb	r2, [r3, #1110]	@ zero_extendqisi2
	strb	r2, [r3]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #32]
	movw	r2, #63871
	ldr	ip, .L4848+24
	str	r0, [sp, #100]
	movw	r0, #4094
	add	r3, r3, #254976
	add	r3, r3, #512
.L4752:
	ldr	r1, [r3, #-4]!
	cmp	r1, #0
	bne	.L4747
	cmp	r9, #0
	sub	r2, r2, #1
	ldrne	r1, [sp, #32]
	streq	ip, [r3, #512]
	ldrne	r1, [r1, r9, lsl #2]
	add	r9, r9, #1
	strne	r1, [r3, #512]
	cmp	r9, r0
	movhi	r9, #0
	cmp	r2, #4096
	bne	.L4752
.L4751:
	mul	r3, r6, r7
	mov	r2, #4
	mov	r1, r5
	ldr	r0, .L4848+28
	str	r3, [sp, #72]
	mov	r3, #5
	bl	rknand_print_hex
	mov	r2, fp
	mov	r1, fp
	ldr	r0, .L4848+32
	bl	printk
	sub	r3, r5, #4
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #36]
	str	r3, [sp, #24]
.L4792:
	ldr	r3, .L4848+20
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4753
	ldr	r3, [sp, #48]
	ldr	r1, [sp, #24]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #72]
	mul	r10, r2, r3
	ldr	r3, [sp, #100]
	cmp	r1, #0
	cmpne	r3, #1
	bls	.L4754
	ldr	r3, [sp, #48]
	ldr	r3, [r3]
	add	r3, r3, #1
	cmp	r2, r3
	bne	.L4754
.L4755:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	ldr	r3, [sp, #48]
	add	r3, r3, #4
	str	r3, [sp, #48]
	ldr	r3, [sp, #24]
	cmp	r3, #4
	bne	.L4792
.L4794:
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4848
	mov	r0, #0
	ldrb	r2, [sp, #76]	@ zero_extendqisi2
	strb	r2, [r3]
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldrbne	r2, [sp, #96]	@ zero_extendqisi2
	strbne	r2, [r3, #1110]
	mov	r2, #2
	strb	r2, [r3, #1154]
	bl	zftl_flash_enter_slc_mode
	ldr	r0, [sp, #20]
	bl	ftl_free
	ldr	r3, [sp, #36]
	clz	r0, r3
	lsr	r0, r0, #5
	rsb	r0, r0, #0
	b	.L4738
.L4747:
	ldr	r0, .L4848+36
	bl	printk
	b	.L4751
.L4753:
	ldr	r5, .L4848
	ldr	r2, [sp, #24]
	ldr	r3, [r5, #1040]
	add	r3, r3, r2
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L4755
	ldr	r2, [sp, #72]
	mul	r10, r2, r3
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L4754
	ldr	r3, [sp, #24]
	and	r4, r3, #1
	ldr	r3, [sp, #16]
	cmp	r3, #3
	orreq	r4, r4, #1
	cmp	r4, #0
	ldrbne	r3, [sp, #40]	@ zero_extendqisi2
	strbne	r3, [r5]
	strbne	r3, [r5, #1110]
	bne	.L4754
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	strb	r4, [r5]
	strb	r4, [r5, #1110]
.L4754:
	ldr	r5, .L4848
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #20]
	bl	ftl_memset
	ldr	r3, [r5, #1104]
	mov	r6, #0
	mov	r0, r10
	ldrb	r8, [r3, #9]	@ zero_extendqisi2
	ldrh	r7, [r3, #10]
	ldr	r3, .L4848+16
	mov	r1, r8
	ldrh	r4, [r3, #30]
	mul	r7, r8, r7
	ldrb	r3, [r5, #1196]	@ zero_extendqisi2
	strb	r6, [r5, #1196]
	str	r3, [sp, #104]
	mul	r4, r8, r4
	bl	__aeabi_uidiv
	mov	r1, r0
	mov	r0, r6
	bl	flash_erase_block
	cmp	r7, fp
	movcs	r0, #1
	bcs	.L4757
	add	r1, r10, r4
	mov	r0, r6
	bl	flash_erase_block
	mov	r0, #2
.L4757:
	ldr	r2, [r5, #1104]
	ldrh	r3, [r2, #10]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	lsl	r3, r3, #2
	mul	r0, r0, r3
	bl	__aeabi_idiv
	mov	r1, r4
	str	r0, [sp, #64]
	mov	r0, r10
	bl	__aeabi_uidivmod
	sub	r3, r10, r1
	str	r1, [sp, #56]
	cmp	r10, r3
	str	r3, [sp, #44]
	bne	.L4801
	ldrb	r3, [r5, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L4801
	ldr	r3, .L4848+20
	mov	r2, #1024
	mov	r1, #0
	ldr	r4, [r3, #-92]
	mov	r0, r4
	bl	ftl_memset
	ldr	r2, .L4848+40
	mov	r3, #12
	stm	r4, {r2, r3}
	mov	r3, #0
	strb	r3, [r4, #16]
	str	r3, [r4, #12]
	ldrb	r3, [r5, #1110]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r5, #1104]
	ldrbne	r3, [r3, #29]	@ zero_extendqisi2
	strbne	r3, [r4, #16]
	mov	r3, #4
	strb	r3, [r4, #17]
	ldr	r3, .L4848
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	bl	__aeabi_idiv
	cmp	r8, #8
	mov	r3, #0
	movhi	r2, #70
	movls	r2, #16
	mov	r1, #12
	strh	r0, [r4, #18]	@ movhi
	strb	r3, [r4, #20]
	add	r0, r4, r1
	strh	r3, [r4, #22]	@ movhi
	strb	r2, [r4, #21]
	bl	js_hash
	sub	r3, fp, #4
	str	r0, [r4, #8]
	str	r3, [sp, #28]
.L4758:
	ldr	r9, [sp, #32]
	mov	r6, #0
	ldr	r7, .L4848
.L4761:
	ldr	r3, [sp, #64]
	cmp	r3, r6
	bhi	.L4772
	ldr	r3, .L4848+16
	mov	r4, #0
	strb	r4, [r7, #1196]
	mov	r0, r10
	ldr	r5, .L4848
	mov	r8, #4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r1, [r3, #30]
	str	r2, [sp, #80]
	mul	r1, r2, r1
	bl	__aeabi_uidivmod
	sub	r3, r10, r1
	ldr	r6, [sp, #20]
	str	r3, [sp, #84]
	and	r3, r1, #3
	str	r3, [sp, #56]
	ldr	r3, [sp, #28]
	str	r1, [sp, #52]
	str	r4, [sp, #64]
	str	r3, [sp, #68]
.L4773:
	ldr	r3, [sp, #68]
	cmp	r4, r3
	bcc	.L4787
	ldrb	r3, [sp, #104]	@ zero_extendqisi2
	ldr	r4, .L4848
	strb	r3, [r4, #1196]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L4788
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #1110]
.L4788:
	ldr	r3, [sp, #28]
	mov	r2, #0
	lsl	r3, r3, #7
.L4790:
	ldr	r1, [sp, #20]
	ldr	r0, [r1, r2, lsl #2]
	ldr	r1, [sp, #32]
	ldr	r1, [r1, r2, lsl #2]
	cmp	r0, r1
	beq	.L4789
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #20]
	bl	ftl_memset
	mov	r1, r10
	mov	r0, #0
	bl	flash_erase_block
	b	.L4755
.L4801:
	str	fp, [sp, #28]
	mov	r4, #0
	b	.L4758
.L4772:
	ldr	r3, [sp, #56]
	ldrb	r2, [r7, #1110]	@ zero_extendqisi2
	add	r5, r3, r6
	ubfx	r5, r5, #2, #16
	cmp	r2, #0
	add	r0, r5, #1
	lsl	r1, r0, #1
	add	r3, r7, r1
	ldrh	r3, [r3, #4]
	beq	.L4763
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r3, r0
	movne	r3, r1
.L4763:
	ldrb	r1, [r7, #1028]	@ zero_extendqisi2
	cmp	r1, #9
	subne	r3, r3, #1
	lslne	r3, r3, #2
	cmp	r2, #0
	str	r3, [sp, #112]
	movw	r3, #61424
	str	r3, [sp, #116]
	lsl	r3, r5, #1
	add	r1, r7, r3
	ldrh	r1, [r1, #4]
	moveq	r5, r1
	beq	.L4768
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r5, r3
.L4768:
	cmp	r4, #0
	mul	r0, r5, r8
	bne	.L4769
	ldr	r3, [sp, #44]
	add	r3, r0, r3
	ldr	r0, .L4848+44
	str	r3, [sp, #60]
	movw	r3, #61424
	str	r3, [sp, #12]
	add	r3, sp, #176
	mov	r2, r3
	ldr	r1, [sp, #60]
	ldr	r3, [r2, #-64]!
	str	r3, [sp, #8]
	ldr	r3, [r9]
	str	r2, [sp, #52]
	mov	r2, r6
	str	r9, [sp]
	str	r3, [sp, #4]
	mov	r3, fp
	bl	printk
	mov	r1, r9
	ldr	r2, [sp, #52]
	ldr	r0, [sp, #60]
	bl	fw_flash_page_prog.constprop.29
	ldrb	r3, [r7, #1110]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4770
	add	r5, r5, #1
	mov	r1, r8
	uxth	r5, r5
	ldr	r0, [sp, #44]
	bl	__aeabi_uidiv
	mov	r2, r5
	mov	r1, r0
	mov	r0, r4
	bl	id_block_prog_msb_ff_data
.L4770:
	ldr	r3, [sp, #28]
	add	r9, r9, #2048
	add	r3, r3, #16
	cmp	r6, r3
	bcc	.L4771
	ldr	r3, [sp, #28]
	add	r3, r3, #20
	cmp	r6, r3
	ldrcc	r3, [sp, #32]
	addcc	r9, r3, #2048
.L4771:
	add	r6, r6, #4
	mov	r4, #0
	uxth	r6, r6
	b	.L4761
.L4769:
	ldr	r3, [sp, #44]
	add	r2, sp, #112
	mov	r1, r4
	add	r0, r0, r3
	bl	fw_flash_page_prog.constprop.29
	b	.L4771
.L4787:
	ldr	r3, [sp, #56]
	mov	r1, r8
	sub	r3, r8, r3
	uxth	r3, r3
	str	r3, [sp, #60]
	ldr	r3, [sp, #52]
	add	r0, r3, r4
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldrb	r1, [r5, #1110]	@ zero_extendqisi2
	lsl	r3, r0, #1
	cmp	r1, #0
	add	r2, r5, r3
	ldrh	r2, [r2, #4]
	moveq	r0, r2
	beq	.L4775
	ldrb	r2, [r5, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r0, r3
.L4775:
	ldr	r2, [sp, #84]
	ldr	r3, [sp, #80]
	ldrb	r9, [r5, #1108]	@ zero_extendqisi2
	mla	r3, r0, r3, r2
	str	r3, [sp, #88]
	ldr	r3, [r5, #1104]
	ldr	r2, [sp, #88]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [sp, #56]
	add	r0, r3, r2
	bl	__aeabi_uidiv
	ldrb	r3, [r5, #1193]	@ zero_extendqisi2
	str	r0, [sp, #44]
	mov	r0, r9
	str	r3, [sp, #108]
	bl	nandc_bch_sel
.L4776:
	str	r8, [sp]
	add	r3, sp, #112
	mov	r2, r6
	ldr	r1, [sp, #44]
	mov	r0, #0
	bl	flash_read_page
	cmn	r0, #1
	mov	r7, r0
	bne	.L4777
	ldrb	r3, [r5, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	str	r3, [sp, #92]
	bne	.L4778
.L4781:
	ldr	r3, .L4848+20
	ldr	r3, [r3, #-100]
	subs	ip, r3, #0
	bne	.L4779
.L4780:
	ldrb	r3, [r5, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4777
	str	r8, [sp]
	add	r3, sp, #112
	mov	r2, r6
	ldr	r1, [sp, #44]
	mov	r0, #0
	bl	flash_ddr_tuning_read
	b	.L4846
.L4778:
	mov	r0, #0
	add	r3, sp, #112
	strb	r0, [r5, #1196]
	mov	r2, r6
	str	r8, [sp]
	ldr	r1, [sp, #44]
	bl	flash_read_page
	ldrb	r3, [sp, #92]	@ zero_extendqisi2
	cmn	r0, #1
	strb	r3, [r5, #1196]
	beq	.L4781
.L4846:
	mov	r7, r0
.L4777:
	cmn	r7, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r9, #16
	moveq	r9, #0
	andne	r9, r7, #1
	cmp	r9, #0
	beq	.L4783
	mov	r0, #16
	mov	r9, #16
	bl	nandc_bch_sel
	b	.L4776
.L4779:
	str	r8, [sp]
	add	r3, sp, #112
	mov	r2, r6
	ldr	r1, [sp, #44]
	mov	r0, #0
	blx	ip
	cmn	r0, #1
	beq	.L4780
	b	.L4846
.L4783:
	ldr	r0, [sp, #108]
	bl	nandc_bch_sel
	ldr	r3, [sp, #64]
	cmp	r7, #0
	mvnne	r3, #0
	str	r3, [sp, #64]
	ldr	r3, [sp, #84]
	ldr	r2, [sp, #64]
	cmp	r10, r3
	cmpeq	r4, #0
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L4785
	ldr	r3, [r6]
	ldr	r2, .L4848+40
	cmp	r3, r2
	bne	.L4785
	ldr	r3, [sp, #68]
	ldr	r2, [sp, #60]
	ldrb	r8, [r6, #17]	@ zero_extendqisi2
	add	r3, r3, r2
	str	r3, [sp, #68]
.L4786:
	ldr	r3, [sp, #60]
	add	r4, r3, r4
	uxth	r4, r4
	b	.L4773
.L4785:
	ldr	r3, [sp, #60]
	mov	r2, r4
	ldr	r1, [sp, #88]
	ldr	r0, .L4848+48
	add	r6, r6, r3, lsl #9
	ldr	r3, [sp, #116]
	str	r3, [sp]
	ldr	r3, [sp, #112]
	bl	printk
	mov	r3, #0
	str	r3, [sp, #56]
	b	.L4786
.L4789:
	add	r2, r2, #1
	cmp	r3, r2
	bne	.L4790
	ldr	r3, [sp, #36]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #36]
	bls	.L4755
	b	.L4794
.L4849:
	.align	2
.L4848:
	.word	.LANCHOR0
	.word	-52655045
	.word	1397640018
	.word	1397967698
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	1314014539
	.word	.LC318
	.word	.LC319
	.word	.LC317
	.word	1179535694
	.word	.LC320
	.word	.LC321
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.syntax unified
	.arm
	.fpu softvfp
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	.pad #48
	sub	sp, sp, #48
	mov	r6, r1
	mov	r8, r2
	ldr	r4, .L4877
	bne	.L4851
	ldr	r3, .L4877+4
	ldr	r2, [r2]
	ldr	r1, .L4877+8
	cmp	r2, r1
	cmpne	r2, r3
	add	r1, r1, #327680
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r1
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L4851
	mov	r3, #1
	mov	r0, #256000
	strb	r3, [r4, #32]
	bl	ftl_malloc
	mov	r2, #256000
	mov	r1, #0
	str	r0, [r4, #36]
	bl	ftl_memset
	str	r5, [r4, #40]
.L4851:
	str	r6, [sp]
	mov	r3, r5
	ldr	r2, [r8]
	ldr	r1, [r4, #36]
	ldr	r0, .L4877+12
	bl	printk
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4850
	ldr	r7, [r4, #36]
	cmp	r7, #0
	beq	.L4850
	sub	r0, r5, #64
	cmp	r0, #500
	bcs	.L4853
	rsb	r2, r5, #564
	mov	r1, r8
	cmp	r6, r2
	add	r0, r7, r0, lsl #9
	movcc	r2, r6
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L4854:
	ldr	r3, [r4, #40]
	cmp	r5, r3
	beq	.L4863
	mov	r8, #0
	mov	r0, r7
	strb	r8, [r4, #32]
	bl	ftl_free
	str	r8, [r4, #36]
	b	.L4863
.L4853:
	cmp	r5, #564
	bcc	.L4854
	ldr	r3, .L4877+16
	ldr	r0, [r4, #40]
	ldr	r3, [r3, #1104]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L4855
	mov	r3, #2
	str	r3, [sp, #8]
	mov	r3, #3
	str	r3, [sp, #12]
	mov	r3, #4
	str	r3, [sp, #16]
	mov	r3, #5
	str	r3, [sp, #20]
	mov	r3, #6
	str	r3, [sp, #24]
.L4856:
	mov	r3, #61952
.L4862:
	ldr	r2, [r7, r3, lsl #2]
	cmp	r2, #0
	beq	.L4860
	add	r3, r3, #2048
	lsl	r0, r3, #2
.L4861:
	mov	r1, r7
	add	r2, sp, #8
	mov	r7, #0
	bl	write_idblock
	ldr	r0, [r4, #36]
	strb	r7, [r4, #32]
	bl	ftl_free
	str	r7, [r4, #36]
.L4863:
	add	r5, r5, r6
	str	r5, [r4, #40]
.L4850:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L4855:
	mov	r2, #0
	add	r3, sp, #8
.L4859:
	cmp	r0, #256
	lslhi	r1, r2, #1
	strls	r2, [r3, r2, lsl #2]
	strhi	r1, [r3, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #5
	bne	.L4859
	b	.L4856
.L4860:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L4862
	lsl	r0, r0, #9
	b	.L4861
.L4878:
	.align	2
.L4877:
	.word	.LANCHOR3
	.word	-52655045
	.word	1397640018
	.word	.LC322
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r2
	sub	r2, r1, #64
	mov	r4, r1
	cmp	r2, #1984
	mov	r7, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	mov	r5, r0
	movne	r2, #0
	cmp	r2, #0
	beq	.L4880
	mov	r2, r3
	mov	r1, r6
	mov	r0, r4
	bl	write_loader_lba
.L4880:
	ldr	r3, .L4885
	mov	r2, r6
	mov	r1, r4
	mov	r0, r5
	ldr	r3, [r3, #-160]
	ldr	lr, [r3, #24]
	mov	r3, r7
	mov	ip, lr
	pop	{r4, r5, r6, r7, r8, lr}
	bx	ip
.L4886:
	.align	2
.L4885:
	.word	.LANCHOR3
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4896
	cmp	r1, r3
	bne	.L4891
	push	{r4, lr}
	.save {r4, lr}
	bl	rknand_dev_flush
	mov	r1, #0
	ldr	r0, .L4896+4
	bl	printk
	mov	r0, #0
	pop	{r4, pc}
.L4891:
	mvn	r0, #21
	bx	lr
.L4897:
	.align	2
.L4896:
	.word	1074029332
	.word	.LC323
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L4899
	mov	r2, #0
	mvn	r1, #0
	strb	r2, [r3, #32]
	str	r1, [r3, #40]
	str	r2, [r3, #36]
	str	r2, [r3, #44]
	b	rknand_sys_storage_init
.L4900:
	.align	2
.L4899:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.align	2
	.global	rk_ftl_vendor_storage_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r6, .L4913
	ldr	r3, [r6, #48]
	cmp	r3, #0
	bne	.L4903
	mov	r0, #65536
	bl	ftl_malloc
	str	r0, [r6, #48]
.L4903:
	ldr	r3, [r6, #48]
	cmp	r3, #0
	beq	.L4908
	ldr	r10, .L4913+4
	mov	r7, #0
	ldr	r9, .L4913+8
	mov	r4, r7
	mov	r8, r7
.L4906:
	ldr	r2, [r6, #48]
	mov	r1, #128
	lsl	r0, r8, #7
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4909
	ldr	r1, [r6, #48]
	mov	r0, r10
	add	r2, r1, #61440
	ldr	r3, [r1, #4]
	ldr	r2, [r2, #4092]
	ldr	r1, [r1]
	bl	printk
	ldr	r5, [r6, #48]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4905
	add	r2, r5, #61440
	ldr	r3, [r5, #4]
	ldr	r2, [r2, #4092]
	cmp	r3, r4
	sub	r2, r2, r3
	clz	r2, r2
	lsr	r2, r2, #5
	movls	r2, #0
	cmp	r2, #0
	movne	r7, r8
	movne	r4, r3
.L4905:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L4906
	cmp	r4, #0
	beq	.L4907
	mov	r2, r5
	mov	r1, #128
	lsl	r0, r7, #7
	bl	FlashBootVendorRead
	adds	r0, r0, #0
	movne	r0, #1
	rsb	r0, r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4907:
	mov	r2, #65536
	mov	r1, r4
	mov	r0, r5
	bl	memset
	mov	r3, #1
	add	r2, r5, #61440
	str	r3, [r5, #4]
	mov	r0, r4
	str	r9, [r5]
	str	r3, [r2, #4092]
	ldr	r3, .L4913+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4908:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4909:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4914:
	.align	2
.L4913:
	.word	.LANCHOR3
	.word	.LC324
	.word	1380668996
	.word	-1032
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4925
	ldr	ip, [r3, #48]
	cmp	ip, #0
	beq	.L4920
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldrh	r4, [ip, #10]
.L4917:
	cmp	r3, r4
	bcc	.L4919
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L4919:
	add	lr, ip, r3, lsl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4918
	ldrh	r4, [lr, #20]
	mov	r0, r1
	ldrh	r1, [lr, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, ip, r1
	bl	memcpy
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L4918:
	add	r3, r3, #1
	b	.L4917
.L4920:
	mvn	r0, #0
	bx	lr
.L4926:
	.align	2
.L4925:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4948
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r3, #48]
	cmp	r4, #0
	beq	.L4942
	mov	r8, r2
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r3, [r4, #8]
	mov	fp, r1
	bic	r6, r6, #63
	mov	r7, #0
	str	r3, [sp, #4]
.L4929:
	cmp	r7, r2
	bcc	.L4937
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L4942
	add	r3, r4, r2, lsl #3
	uxth	r6, r6
	strh	r0, [r3, #16]	@ movhi
	ldrh	r2, [r4, #12]
	strh	r8, [r3, #20]	@ movhi
	strh	r2, [r3, #18]	@ movhi
	add	r2, r2, r6
	sub	r6, r1, r6
	strh	r2, [r4, #12]	@ movhi
	strh	r6, [r4, #14]	@ movhi
	mov	r2, r8
	ldrh	r0, [r3, #18]
	mov	r1, fp
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	b	.L4947
.L4937:
	add	r5, r4, r7, lsl #3
	ldrh	r3, [r5, #16]
	cmp	r3, r0
	str	r3, [sp, #8]
	bne	.L4930
	ldrh	r1, [r5, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L4931
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	subls	r2, r2, #1
	ldrhls	r10, [r5, #18]
	strls	r2, [sp, #16]
	bls	.L4932
.L4942:
	mvn	r0, #0
	b	.L4927
.L4933:
	ldrh	r9, [r5, #20]
	add	r0, r3, r10
	ldrh	r2, [r5, #16]
	add	r7, r7, #1
	ldrh	r1, [r5, #18]
	strh	r9, [r5, #12]	@ movhi
	add	r9, r9, #63
	bic	r9, r9, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r10, [r5, #10]	@ movhi
	add	r1, r3, r1
	mov	r2, r9
	str	r3, [sp, #20]
	bl	memcpy
	ldr	r3, [sp, #20]
	add	r10, r10, r9
.L4932:
	ldr	r2, [sp, #16]
	add	r5, r5, #8
	cmp	r7, r2
	bcc	.L4933
	ldrh	r2, [sp, #8]
	add	r7, r4, r7, lsl #3
	uxth	r5, r10
	uxtah	r0, r3, r10
	strh	r8, [r7, #20]	@ movhi
	strh	r2, [r7, #16]	@ movhi
	mov	r1, fp
	strh	r5, [r7, #18]	@ movhi
	mov	r2, r8
	bl	memcpy
	uxth	r3, r6
	ldrh	r6, [r4, #14]
	add	r5, r5, r3
	sub	r6, r6, r3
	ldr	r3, [sp, #12]
	strh	r5, [r4, #12]	@ movhi
	add	r6, r6, r3
	strh	r6, [r4, #14]	@ movhi
.L4947:
	ldr	r3, [r4, #4]
	add	r2, r4, #61440
	mov	r1, #128
	add	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	mov	r2, r4
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [sp, #4]
	lsl	r0, r3, #7
	bl	FlashBootVendorWrite
	mov	r0, #0
.L4927:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4931:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, fp
	add	r0, r3, r0
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L4947
.L4930:
	add	r7, r7, #1
	b	.L4929
.L4949:
	.align	2
.L4948:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #9216
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L4950
	ldr	r3, .L4966
	cmp	r6, r3
	beq	.L4953
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4954
.L4964:
	mvn	r5, #13
	b	.L4952
.L4953:
	mov	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4964
	ldr	r2, [r4]
	ldr	r3, .L4966+4
	cmp	r2, r3
	beq	.L4956
.L4957:
	mvn	r5, #0
.L4952:
	mov	r0, r4
	bl	kfree
.L4950:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L4956:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4957
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	subs	r5, r0, #0
	beq	.L4952
	b	.L4964
.L4954:
	mov	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4964
	ldr	r2, [r4]
	ldr	r3, .L4966+4
	cmp	r2, r3
	bne	.L4957
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4957
	add	r2, r2, #8
	mov	r1, r5
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4964
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4952
.L4967:
	.align	2
.L4966:
	.word	1074034177
	.word	1448232273
	.fnend
	.size	rk_ftl_vendor_storage_ioctl, .-rk_ftl_vendor_storage_ioctl
	.global	SecureBootUnlockTryCount
	.global	SecureBootCheckOK
	.global	SecureBootEn
	.global	gpVendor1Info
	.global	gpVendor0Info
	.global	g_idb_buffer
	.global	gSnSectorData
	.global	gpDrmKeyInfo
	.global	gpBootConfig
	.global	ftl_dma32_buffer_size
	.global	ftl_dma32_buffer
	.global	gLoaderBootInfo
	.global	RK29_NANDC1_REG_BASE
	.global	RK29_NANDC_REG_BASE
	.global	gp_ftl_api
	.global	rk_zftl_enable
	.global	g_pm_spare
	.global	pm_first_write
	.global	pm_force_gc
	.global	pm_gc_enable
	.global	pm_last_load_ram_id
	.global	pm_last_update_ram_id
	.global	pm_ram_info
	.global	sblk_gc_write_completed_queue_head
	.global	sblk_read_completed_queue_head
	.global	sblk_write_completed_queue_head
	.global	sblk_queue_head
	.global	slc_cache_sblk
	.global	xlc_data_sblk
	.global	slc_data_sblk
	.global	free_mix_sblk
	.global	free_xlc_sblk
	.global	free_slc_sblk
	.global	gp_data_xlc_data_head
	.global	gp_data_slc_data_head
	.global	gp_data_slc_cache_head
	.global	gp_free_mix_head
	.global	gp_free_xlc_head
	.global	gp_free_slc_head
	.global	gp_sblk_list_tbl
	.global	zftl_print_list_count
	.global	ftl_ext_info_first_write
	.global	ftl_sys_info_first_write
	.global	ftl_low_format_cur_blk
	.global	ftl_power_lost_flag
	.global	ftl_vpn_update_count
	.global	ftl_sblk_update_list_offset
	.global	ftl_sblk_update_list
	.global	ftl_sblk_vpn_update_id
	.global	ftl_sblk_lpa_tbl
	.global	ftl_sblk_vpn
	.global	gp_ftl_ext_info
	.global	gp_ftl_info
	.global	gp_blk_info
	.global	ftl_tmp_buffer
	.global	ftl_ext_info_data_buffer
	.global	ftl_info_data_buffer
	.global	ftl_tmp_spare
	.global	ftl_info_spare
	.global	g_ftl_info_blk
	.global	tlc_b05a_prog_tbl
	.global	tlc_prog_order
	.global	gc_des_ppa_tbl
	.global	gc_valid_page_ppa
	.global	gc_page_buf_id
	.global	gc_pre_ppa_tbl
	.global	gc_lpa_tbl
	.global	g_gc_info
	.global	gc_xlc_search_index
	.global	gc_xlc_data_index
	.global	gc_slc_cache_index
	.global	gc_slc_data_index
	.global	gc_free_slc_sblk_th
	.global	gc_slc_mode_vpn_th
	.global	gc_slc_mode_slc_vpn_th
	.global	gc_slc_mode_tlc_vpn_th
	.global	gc_tlc_mode_tlc_vpn_th
	.global	gc_tlc_mode_slc_vpn_th
	.global	gc_state
	.global	gc_mode
	.global	p_read_ahead_ext_buf
	.global	discard_sector_count
	.global	read_ahead_lpa
	.global	_ftl_gc_tag_page_num
	.global	read_buf_count
	.global	read_buf_head
	.global	write_commit_count
	.global	write_commit_head
	.global	write_buf_count
	.global	write_buf_head
	.global	ftl_flush_jiffies
	.global	lpa_hash
	.global	lpa_hash_index
	.global	_c_slc_to_xlc_ec_ratio
	.global	_c_mix_max_xlc_ec_count
	.global	_c_mix_max_slc_ec_count
	.global	_c_swl_xlc_gc_th
	.global	_c_swl_slc_gc_th
	.global	_gc_after_discard_en
	.global	_last_write_time
	.global	_last_read_time
	.global	_min_slc_super_block
	.global	_max_xlc_super_block
	.global	_c_max_pm_sblk
	.global	_c_ftl_pm_page_num
	.global	_c_totle_log_page
	.global	_c_totle_data_density
	.global	_c_user_data_density
	.global	_c_totle_phy_density
	.global	_c_ftl_block_addr_log2
	.global	_c_ftl_block_align_addr
	.global	_c_ftl_byte_pre_page
	.global	_c_ftl_nand_blks_per_die
	.global	_c_ftl_page_pre_slc_blk
	.global	_c_ftl_page_pre_blk
	.global	_c_ftl_blk_pre_plane
	.global	_c_ftl_nand_planes_num
	.global	_c_ftl_planes_per_die
	.global	_c_ftl_sec_per_page
	.global	_c_ftl_nand_die_num
	.global	_c_ftl_nand_type
	.global	zftl_debug
	.global	g_flash_blk_info
	.global	gp_flash_info
	.global	p_free_buf_head
	.global	free_buf_count
	.global	g_buf
	.global	nandc_ecc_sts
	.global	g_nandc_v6_master_info
	.global	nandc_randomizer_en
	.global	nandc_hw_seed
	.global	fill_spare_size
	.global	g_nandc_ecc_bits
	.global	g_nandc_tran_timeout
	.global	g_nandc_ver
	.global	gp_nandc
	.global	hy_f26_ref_value
	.global	sd15_tlc_rr
	.global	sd15_slc_rr
	.global	g_nand_para_info
	.global	gp_nand_para_info
	.global	g_nand_opt_para
	.global	g_msb_page_tbl
	.global	g_lsb_page_tbl
	.global	g_die_addr
	.global	g_die_cs_idx
	.global	IDByte
	.global	flash_read_retry
	.global	_c_ftl_cs_bits
	.global	g_maxRetryCount
	.global	g_maxRegNum
	.global	g_retryMode
	.global	g_flash_toggle_mode_en
	.global	g_flash_ymtc_3d_tlc_flag
	.global	g_flash_micron_3d_tlc_b16a
	.global	g_flash_micron_3d_tlc_b05a
	.global	g_flash_micron_3d_tlc_flag
	.global	g_flash_3d_mlc_flag
	.global	g_flash_3d_tlc_flag
	.global	g_flash_multi_page_prog_en
	.global	g_flash_multi_page_read_en
	.global	g_flash_interface_mode
	.global	g_idb_ecc_bits
	.global	g_idb_slc_mode_enable
	.global	g_one_pass_program
	.global	g_slc_mode_addr2
	.global	g_slc_mode_enable
	.global	g_flash_cur_mode
	.global	g_flash_six_addr
	.global	g_flash_slc_mode
	.global	g_slc_page_num
	.global	g_totle_phy_block
	.global	g_block_align_addr
	.global	g_flash_reversd_blks
	.global	g_nand_max_die
	.global	g_flash_tmp_spare_buffer
	.global	g_flash_tmp_page_buffer
	.global	g_flash_sys_spare_buffer
	.global	g_flash_spare_buffer
	.global	g_flash_page_buffer
	.section	.rodata
	.align	2
	.set	.LANCHOR1,. + 0
	.type	__func__.42945, %object
	.size	__func__.42945, 18
__func__.42945:
	.ascii	"_list_remove_node\000"
	.type	__func__.42970, %object
	.size	__func__.42970, 23
__func__.42970:
	.ascii	"_list_update_data_list\000"
	.type	toshiba_A19ref_value, %object
	.size	toshiba_A19ref_value, 45
toshiba_A19ref_value:
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	13
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	4
	.byte	4
	.byte	124
	.byte	126
	.byte	0
	.byte	0
	.byte	124
	.byte	120
	.byte	120
	.byte	0
	.byte	124
	.byte	118
	.byte	116
	.byte	114
	.byte	0
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	0
	.byte	11
	.byte	126
	.byte	118
	.byte	116
	.byte	0
	.byte	16
	.byte	118
	.byte	114
	.byte	112
	.byte	0
	.byte	2
	.byte	0
	.byte	126
	.byte	124
	.byte	0
	.type	toshiba_15ref_value, %object
	.size	toshiba_15ref_value, 95
toshiba_15ref_value:
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	13
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	2
	.byte	4
	.byte	2
	.byte	0
	.byte	0
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	0
	.byte	124
	.byte	0
	.byte	124
	.byte	124
	.byte	0
	.byte	122
	.byte	0
	.byte	122
	.byte	122
	.byte	0
	.byte	11
	.byte	126
	.byte	118
	.byte	116
	.byte	0
	.byte	120
	.byte	2
	.byte	120
	.byte	122
	.byte	0
	.byte	126
	.byte	4
	.byte	126
	.byte	122
	.byte	0
	.byte	16
	.byte	118
	.byte	114
	.byte	112
	.byte	0
	.byte	118
	.byte	4
	.byte	118
	.byte	120
	.byte	0
	.byte	4
	.byte	4
	.byte	4
	.byte	118
	.byte	0
	.byte	2
	.byte	0
	.byte	126
	.byte	124
	.byte	0
	.byte	6
	.byte	10
	.byte	6
	.byte	2
	.byte	0
	.byte	116
	.byte	124
	.byte	116
	.byte	118
	.byte	0
	.byte	4
	.byte	4
	.byte	124
	.byte	126
	.byte	0
	.byte	0
	.byte	124
	.byte	120
	.byte	120
	.byte	0
	.byte	124
	.byte	118
	.byte	116
	.byte	114
	.byte	0
	.type	toshiba_ref_value, %object
	.size	toshiba_ref_value, 8
toshiba_ref_value:
	.byte	0
	.byte	4
	.byte	124
	.byte	120
	.byte	116
	.byte	8
	.byte	12
	.byte	112
	.type	__func__.23463, %object
	.size	__func__.23463, 22
__func__.23463:
	.ascii	"nand_flash_print_info\000"
	.type	__func__.22936, %object
	.size	__func__.22936, 28
__func__.22936:
	.ascii	"flash_wait_device_ready_raw\000"
	.type	__func__.23000, %object
	.size	__func__.23000, 22
__func__.23000:
	.ascii	"flash_start_page_read\000"
	.type	toshiba_3D_tlc_value, %object
	.size	toshiba_3D_tlc_value, 399
toshiba_3D_tlc_value:
	.byte	-119
	.byte	-119
	.byte	-119
	.byte	-119
	.byte	-118
	.byte	-118
	.byte	-118
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	-2
	.byte	-1
	.byte	0
	.byte	-3
	.byte	-2
	.byte	6
	.byte	-9
	.byte	-12
	.byte	-9
	.byte	-7
	.byte	-13
	.byte	-12
	.byte	-7
	.byte	-6
	.byte	-15
	.byte	-15
	.byte	-2
	.byte	-12
	.byte	-16
	.byte	-6
	.byte	-2
	.byte	-19
	.byte	-19
	.byte	-6
	.byte	-4
	.byte	-12
	.byte	-14
	.byte	-2
	.byte	-11
	.byte	-23
	.byte	-34
	.byte	-4
	.byte	-20
	.byte	-22
	.byte	-2
	.byte	-7
	.byte	-31
	.byte	-30
	.byte	-12
	.byte	-20
	.byte	-18
	.byte	2
	.byte	-15
	.byte	-19
	.byte	-36
	.byte	-12
	.byte	-28
	.byte	-34
	.byte	-6
	.byte	-15
	.byte	-11
	.byte	2
	.byte	-12
	.byte	-8
	.byte	-2
	.byte	2
	.byte	-3
	.byte	-7
	.byte	-10
	.byte	-4
	.byte	-8
	.byte	-6
	.byte	-6
	.byte	-11
	.byte	-27
	.byte	-38
	.byte	-16
	.byte	-12
	.byte	-2
	.byte	2
	.byte	-7
	.byte	-31
	.byte	-22
	.byte	-4
	.byte	-16
	.byte	-22
	.byte	-7
	.byte	-31
	.byte	-23
	.byte	-22
	.byte	-28
	.byte	-28
	.byte	-26
	.byte	2
	.byte	-7
	.byte	-11
	.byte	-14
	.byte	-8
	.byte	-12
	.byte	-10
	.byte	-10
	.byte	-27
	.byte	-25
	.byte	-22
	.byte	-20
	.byte	-28
	.byte	-22
	.byte	-7
	.byte	-23
	.byte	-29
	.byte	-34
	.byte	-24
	.byte	-32
	.byte	-22
	.byte	-10
	.byte	-11
	.byte	-29
	.byte	-18
	.byte	-12
	.byte	-24
	.byte	-22
	.byte	6
	.byte	1
	.byte	-3
	.byte	-6
	.byte	0
	.byte	-4
	.byte	-2
	.byte	10
	.byte	-3
	.byte	-7
	.byte	-6
	.byte	4
	.byte	-4
	.byte	-2
	.byte	-10
	.byte	-23
	.byte	-39
	.byte	-22
	.byte	-19
	.byte	-24
	.byte	-18
	.byte	-14
	.byte	-23
	.byte	-29
	.byte	-30
	.byte	-15
	.byte	-30
	.byte	-30
	.byte	-7
	.byte	-27
	.byte	-35
	.byte	-26
	.byte	-15
	.byte	-24
	.byte	-26
	.byte	6
	.byte	-11
	.byte	5
	.byte	-2
	.byte	-16
	.byte	-16
	.byte	-2
	.byte	-2
	.byte	-15
	.byte	-15
	.byte	-20
	.byte	-8
	.byte	-16
	.byte	-18
	.byte	6
	.byte	5
	.byte	-15
	.byte	-2
	.byte	-24
	.byte	-28
	.byte	-22
	.byte	10
	.byte	-15
	.byte	-3
	.byte	-30
	.byte	-8
	.byte	-24
	.byte	-30
	.byte	-10
	.byte	-27
	.byte	-19
	.byte	-30
	.byte	-12
	.byte	-16
	.byte	-10
	.byte	14
	.byte	-19
	.byte	-3
	.byte	-30
	.byte	4
	.byte	4
	.byte	6
	.byte	2
	.byte	1
	.byte	-3
	.byte	-10
	.byte	-8
	.byte	-4
	.byte	-6
	.byte	-2
	.byte	-15
	.byte	-11
	.byte	-26
	.byte	-8
	.byte	-20
	.byte	-30
	.byte	6
	.byte	-19
	.byte	-3
	.byte	-46
	.byte	0
	.byte	0
	.byte	2
	.byte	6
	.byte	9
	.byte	5
	.byte	2
	.byte	4
	.byte	8
	.byte	6
	.byte	8
	.byte	9
	.byte	9
	.byte	6
	.byte	8
	.byte	8
	.byte	6
	.byte	10
	.byte	13
	.byte	9
	.byte	6
	.byte	8
	.byte	12
	.byte	10
	.byte	2
	.byte	5
	.byte	1
	.byte	-2
	.byte	0
	.byte	0
	.byte	6
	.byte	12
	.byte	1
	.byte	13
	.byte	2
	.byte	12
	.byte	12
	.byte	14
	.byte	-12
	.byte	-14
	.byte	-20
	.byte	-18
	.byte	-16
	.byte	-16
	.byte	-14
	.byte	-12
	.byte	-10
	.byte	-21
	.byte	-14
	.byte	-12
	.byte	-12
	.byte	-10
	.byte	-12
	.byte	-18
	.byte	-22
	.byte	-24
	.byte	-18
	.byte	-18
	.byte	-18
	.byte	-12
	.byte	-14
	.byte	-23
	.byte	-20
	.byte	-20
	.byte	-20
	.byte	-20
	.byte	-12
	.byte	-24
	.byte	-24
	.byte	-30
	.byte	-24
	.byte	-28
	.byte	-28
	.byte	-12
	.byte	-26
	.byte	-25
	.byte	-34
	.byte	-24
	.byte	-24
	.byte	-24
	.byte	-12
	.byte	-13
	.byte	-26
	.byte	-20
	.byte	-14
	.byte	-18
	.byte	-18
	.byte	-12
	.byte	-15
	.byte	-27
	.byte	-22
	.byte	-20
	.byte	-24
	.byte	-22
	.byte	-12
	.byte	-21
	.byte	-28
	.byte	-28
	.byte	-24
	.byte	-26
	.byte	-24
	.byte	20
	.byte	16
	.byte	6
	.byte	10
	.byte	16
	.byte	12
	.byte	12
	.byte	16
	.byte	16
	.byte	8
	.byte	8
	.byte	12
	.byte	12
	.byte	12
	.byte	18
	.byte	18
	.byte	10
	.byte	8
	.byte	14
	.byte	14
	.byte	14
	.byte	16
	.byte	14
	.byte	6
	.byte	6
	.byte	12
	.byte	14
	.byte	8
	.byte	20
	.byte	18
	.byte	8
	.byte	6
	.byte	14
	.byte	14
	.byte	10
	.byte	20
	.byte	20
	.byte	6
	.byte	10
	.byte	10
	.byte	12
	.byte	12
	.byte	10
	.byte	13
	.byte	5
	.byte	2
	.byte	14
	.byte	8
	.byte	6
	.byte	6
	.byte	13
	.byte	9
	.byte	4
	.byte	14
	.byte	10
	.byte	10
	.byte	10
	.byte	13
	.byte	9
	.byte	6
	.byte	8
	.byte	12
	.byte	10
	.byte	2
	.byte	5
	.byte	1
	.byte	-2
	.byte	0
	.byte	0
	.byte	6
	.byte	12
	.byte	1
	.byte	13
	.byte	2
	.byte	12
	.byte	12
	.byte	14
	.type	toshiba_3D_slc_value, %object
	.size	toshiba_3D_slc_value, 11
toshiba_3D_slc_value:
	.byte	-117
	.byte	0
	.byte	-8
	.byte	8
	.byte	-16
	.byte	-24
	.byte	24
	.byte	-40
	.byte	40
	.byte	-56
	.byte	56
	.type	ymtc_3D_tlc_value, %object
	.size	ymtc_3D_tlc_value, 357
ymtc_3D_tlc_value:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-10
	.byte	-10
	.byte	-6
	.byte	-6
	.byte	-2
	.byte	2
	.byte	2
	.byte	-6
	.byte	-6
	.byte	-4
	.byte	-4
	.byte	-4
	.byte	-6
	.byte	-8
	.byte	6
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-2
	.byte	-2
	.byte	-2
	.byte	-4
	.byte	-4
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-11
	.byte	-2
	.byte	2
	.byte	4
	.byte	4
	.byte	6
	.byte	6
	.byte	6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-6
	.byte	-8
	.byte	-14
	.byte	-6
	.byte	-15
	.byte	-11
	.byte	2
	.byte	-12
	.byte	-8
	.byte	-2
	.byte	2
	.byte	-3
	.byte	-7
	.byte	-10
	.byte	-4
	.byte	-8
	.byte	-6
	.byte	-18
	.byte	-18
	.byte	-14
	.byte	-14
	.byte	-10
	.byte	-5
	.byte	-5
	.byte	-14
	.byte	-14
	.byte	-12
	.byte	-12
	.byte	-12
	.byte	-13
	.byte	-15
	.byte	-2
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	-7
	.byte	-7
	.byte	-10
	.byte	-10
	.byte	-10
	.byte	-12
	.byte	-12
	.byte	-13
	.byte	-13
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-13
	.byte	-18
	.byte	-10
	.byte	-6
	.byte	-4
	.byte	-4
	.byte	-2
	.byte	-1
	.byte	-1
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-14
	.byte	-15
	.byte	-21
	.byte	-12
	.byte	-11
	.byte	-7
	.byte	-7
	.byte	-3
	.byte	1
	.byte	1
	.byte	-8
	.byte	-7
	.byte	-5
	.byte	-5
	.byte	-5
	.byte	-7
	.byte	-9
	.byte	4
	.byte	-1
	.byte	-1
	.byte	-1
	.byte	-1
	.byte	-1
	.byte	-1
	.byte	-4
	.byte	-3
	.byte	-3
	.byte	-5
	.byte	-5
	.byte	-7
	.byte	-7
	.byte	-8
	.byte	-7
	.byte	-7
	.byte	-7
	.byte	-7
	.byte	-7
	.byte	-12
	.byte	-4
	.byte	1
	.byte	3
	.byte	3
	.byte	5
	.byte	5
	.byte	5
	.byte	-8
	.byte	-7
	.byte	-7
	.byte	-7
	.byte	-7
	.byte	-9
	.byte	-15
	.byte	2
	.byte	-7
	.byte	-11
	.byte	-14
	.byte	-8
	.byte	-12
	.byte	-10
	.byte	6
	.byte	1
	.byte	-3
	.byte	-6
	.byte	0
	.byte	-4
	.byte	-2
	.byte	10
	.byte	-3
	.byte	-7
	.byte	-6
	.byte	4
	.byte	-4
	.byte	-2
	.byte	-10
	.byte	-23
	.byte	-39
	.byte	-22
	.byte	-19
	.byte	-24
	.byte	-18
	.byte	-7
	.byte	-27
	.byte	-35
	.byte	-26
	.byte	-15
	.byte	-24
	.byte	-26
	.byte	6
	.byte	-11
	.byte	5
	.byte	-2
	.byte	-16
	.byte	-16
	.byte	-2
	.byte	-2
	.byte	-15
	.byte	-15
	.byte	-20
	.byte	-8
	.byte	-16
	.byte	-18
	.byte	2
	.byte	1
	.byte	-3
	.byte	-10
	.byte	-8
	.byte	-4
	.byte	-6
	.byte	-2
	.byte	-15
	.byte	-11
	.byte	-26
	.byte	-8
	.byte	-20
	.byte	-30
	.byte	6
	.byte	-19
	.byte	-3
	.byte	-46
	.byte	0
	.byte	0
	.byte	2
	.byte	6
	.byte	9
	.byte	5
	.byte	2
	.byte	4
	.byte	8
	.byte	6
	.byte	8
	.byte	9
	.byte	9
	.byte	6
	.byte	8
	.byte	8
	.byte	6
	.byte	10
	.byte	13
	.byte	9
	.byte	6
	.byte	8
	.byte	12
	.byte	10
	.byte	2
	.byte	5
	.byte	1
	.byte	-2
	.byte	0
	.byte	0
	.byte	6
	.byte	12
	.byte	1
	.byte	13
	.byte	2
	.byte	12
	.byte	12
	.byte	14
	.byte	-12
	.byte	-14
	.byte	-20
	.byte	-18
	.byte	-16
	.byte	-16
	.byte	-14
	.byte	-12
	.byte	-10
	.byte	-21
	.byte	-14
	.byte	-12
	.byte	-12
	.byte	-10
	.byte	-12
	.byte	-18
	.byte	-22
	.byte	-24
	.byte	-18
	.byte	-18
	.byte	-18
	.byte	-12
	.byte	-14
	.byte	-23
	.byte	-20
	.byte	-20
	.byte	-20
	.byte	-20
	.byte	16
	.byte	16
	.byte	8
	.byte	8
	.byte	12
	.byte	12
	.byte	12
	.byte	18
	.byte	18
	.byte	10
	.byte	8
	.byte	14
	.byte	14
	.byte	14
	.byte	16
	.byte	14
	.byte	6
	.byte	6
	.byte	12
	.byte	14
	.byte	8
	.byte	10
	.byte	13
	.byte	5
	.byte	2
	.byte	14
	.byte	8
	.byte	6
	.byte	6
	.byte	13
	.byte	9
	.byte	4
	.byte	14
	.byte	10
	.byte	10
	.byte	10
	.byte	13
	.byte	9
	.byte	6
	.byte	8
	.byte	12
	.byte	10
	.byte	2
	.byte	5
	.byte	1
	.byte	-2
	.byte	0
	.byte	0
	.byte	6
	.byte	12
	.byte	1
	.byte	13
	.byte	2
	.byte	12
	.byte	12
	.byte	14
	.type	ymtc_3D_slc_value, %object
	.size	ymtc_3D_slc_value, 10
ymtc_3D_slc_value:
	.byte	0
	.byte	-8
	.byte	8
	.byte	-16
	.byte	-20
	.byte	24
	.byte	-26
	.byte	40
	.byte	-12
	.byte	56
	.type	__func__.23025, %object
	.size	__func__.23025, 23
__func__.23025:
	.ascii	"flash_start_plane_read\000"
	.type	__func__.22911, %object
	.size	__func__.22911, 26
__func__.22911:
	.ascii	"flash_erase_duplane_block\000"
	.type	__func__.22922, %object
	.size	__func__.22922, 21
__func__.22922:
	.ascii	"flash_erase_block_en\000"
	.type	random_seed, %object
	.size	random_seed, 256
random_seed:
	.short	22378
	.short	1512
	.short	25245
	.short	17827
	.short	25756
	.short	19440
	.short	9026
	.short	10030
	.short	29528
	.short	20467
	.short	29676
	.short	24432
	.short	31328
	.short	6872
	.short	13426
	.short	13842
	.short	8783
	.short	1108
	.short	782
	.short	28837
	.short	30729
	.short	9505
	.short	18676
	.short	23085
	.short	18730
	.short	1085
	.short	32609
	.short	14697
	.short	20858
	.short	15170
	.short	30365
	.short	1607
	.short	32298
	.short	4995
	.short	18905
	.short	1976
	.short	9592
	.short	20204
	.short	17443
	.short	13615
	.short	23330
	.short	29369
	.short	13947
	.short	9398
	.short	32398
	.short	8984
	.short	27600
	.short	21785
	.short	6019
	.short	6311
	.short	31598
	.short	30210
	.short	19327
	.short	13896
	.short	11347
	.short	27545
	.short	3107
	.short	26575
	.short	32270
	.short	19852
	.short	20601
	.short	8349
	.short	9290
	.short	29819
	.short	13579
	.short	3661
	.short	28676
	.short	27331
	.short	32574
	.short	8693
	.short	31253
	.short	9081
	.short	5399
	.short	6842
	.short	20087
	.short	5537
	.short	1274
	.short	11617
	.short	9530
	.short	4866
	.short	8035
	.short	23219
	.short	1178
	.short	23272
	.short	7383
	.short	18944
	.short	12488
	.short	12871
	.short	29340
	.short	20532
	.short	11022
	.short	22514
	.short	228
	.short	22363
	.short	24978
	.short	14584
	.short	12138
	.short	3092
	.short	17916
	.short	16863
	.short	14554
	.short	31457
	.short	29474
	.short	25311
	.short	24121
	.short	3684
	.short	28037
	.short	22865
	.short	22839
	.short	25217
	.short	13217
	.short	27186
	.short	14938
	.short	11180
	.short	29754
	.short	24180
	.short	15150
	.short	32455
	.short	20434
	.short	23848
	.short	29983
	.short	16120
	.short	14769
	.short	20041
	.short	29803
	.short	28406
	.short	17598
	.short	28087
	.type	__func__.23728, %object
	.size	__func__.23728, 13
__func__.23728:
	.ascii	"buf_add_tail\000"
	.type	__func__.23741, %object
	.size	__func__.23741, 10
__func__.23741:
	.ascii	"buf_alloc\000"
	.type	__func__.23755, %object
	.size	__func__.23755, 16
__func__.23755:
	.ascii	"buf_remove_free\000"
	.space	1
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 44
zftl_debug_proc_fops:
	.space	4
	.word	zftl_debug_proc_open
	.word	seq_read
	.space	4
	.word	zftl_debug_proc_write
	.word	seq_lseek
	.word	single_release
	.space	16
	.type	__func__.42478, %object
	.size	__func__.42478, 12
__func__.42478:
	.ascii	"gc_add_sblk\000"
	.type	__func__.42570, %object
	.size	__func__.42570, 19
__func__.42570:
	.ascii	"gc_write_completed\000"
	.type	__func__.43176, %object
	.size	__func__.43176, 18
__func__.43176:
	.ascii	"ftl_alloc_sys_blk\000"
	.type	__func__.43186, %object
	.size	__func__.43186, 17
__func__.43186:
	.ascii	"ftl_free_sys_blk\000"
	.type	__func__.43307, %object
	.size	__func__.43307, 23
__func__.43307:
	.ascii	"ftl_get_ppa_from_index\000"
	.type	__func__.43347, %object
	.size	__func__.43347, 22
__func__.43347:
	.ascii	"ftl_get_new_free_page\000"
	.type	__func__.43358, %object
	.size	__func__.43358, 22
__func__.43358:
	.ascii	"ftl_ext_alloc_new_blk\000"
	.type	__func__.42627, %object
	.size	__func__.42627, 16
__func__.42627:
	.ascii	"gc_free_src_blk\000"
	.type	__func__.42221, %object
	.size	__func__.42221, 14
__func__.42221:
	.ascii	"ftl_write_buf\000"
	.type	__func__.42266, %object
	.size	__func__.42266, 18
__func__.42266:
	.ascii	"zftl_add_read_buf\000"
	.type	__func__.43780, %object
	.size	__func__.43780, 21
__func__.43780:
	.ascii	"pm_select_ram_region\000"
	.type	__func__.23453, %object
	.size	__func__.23453, 20
__func__.23453:
	.ascii	"flash_die_info_init\000"
	.type	__func__.42153, %object
	.size	__func__.42153, 17
__func__.42153:
	.ascii	"lpa_rebuild_hash\000"
	.type	__func__.43045, %object
	.size	__func__.43045, 20
__func__.43045:
	.ascii	"zftl_sblk_list_init\000"
	.type	__func__.43712, %object
	.size	__func__.43712, 13
__func__.43712:
	.ascii	"pm_free_sblk\000"
	.type	__func__.23791, %object
	.size	__func__.23791, 21
__func__.23791:
	.ascii	"flash_info_data_init\000"
	.type	__func__.23549, %object
	.size	__func__.23549, 11
__func__.23549:
	.ascii	"nandc_init\000"
	.type	samsung_14nm_slc_rr, %object
	.size	samsung_14nm_slc_rr, 26
samsung_14nm_slc_rr:
	.byte	0
	.byte	10
	.byte	-10
	.byte	20
	.byte	-20
	.byte	30
	.byte	-30
	.byte	40
	.byte	-40
	.byte	50
	.byte	-50
	.byte	60
	.byte	-60
	.byte	-70
	.byte	-80
	.byte	-90
	.byte	-100
	.byte	-110
	.byte	-120
	.byte	-9
	.byte	70
	.byte	80
	.byte	90
	.byte	-125
	.byte	-115
	.byte	100
	.type	samsung_14nm_mlc_rr, %object
	.size	samsung_14nm_mlc_rr, 104
samsung_14nm_mlc_rr:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-4
	.byte	3
	.byte	-4
	.byte	-6
	.byte	6
	.byte	0
	.byte	6
	.byte	-10
	.byte	-10
	.byte	4
	.byte	-10
	.byte	16
	.byte	12
	.byte	-4
	.byte	12
	.byte	8
	.byte	-16
	.byte	10
	.byte	-16
	.byte	24
	.byte	18
	.byte	-14
	.byte	18
	.byte	-4
	.byte	-22
	.byte	-16
	.byte	-22
	.byte	-8
	.byte	24
	.byte	-9
	.byte	24
	.byte	8
	.byte	-28
	.byte	-4
	.byte	-28
	.byte	16
	.byte	30
	.byte	10
	.byte	30
	.byte	10
	.byte	-34
	.byte	6
	.byte	-34
	.byte	0
	.byte	36
	.byte	-8
	.byte	36
	.byte	-8
	.byte	-40
	.byte	-2
	.byte	-40
	.byte	-20
	.byte	-46
	.byte	-4
	.byte	-46
	.byte	-30
	.byte	3
	.byte	0
	.byte	3
	.byte	-3
	.byte	-2
	.byte	-4
	.byte	-2
	.byte	-6
	.byte	-4
	.byte	-4
	.byte	-4
	.byte	-10
	.byte	-6
	.byte	-8
	.byte	-6
	.byte	-14
	.byte	-9
	.byte	-8
	.byte	-9
	.byte	-18
	.byte	-52
	.byte	22
	.byte	-52
	.byte	10
	.byte	42
	.byte	4
	.byte	42
	.byte	4
	.byte	48
	.byte	-9
	.byte	48
	.byte	4
	.byte	-58
	.byte	12
	.byte	-58
	.byte	0
	.byte	-64
	.byte	-24
	.byte	-64
	.byte	-6
	.byte	9
	.byte	18
	.byte	9
	.byte	8
	.type	__func__.22834, %object
	.size	__func__.22834, 19
__func__.22834:
	.ascii	"flash_read_page_en\000"
	.type	__func__.22960, %object
	.size	__func__.22960, 26
__func__.22960:
	.ascii	"flash_start_tlc_page_prog\000"
	.type	__func__.22971, %object
	.size	__func__.22971, 29
__func__.22971:
	.ascii	"flash_start_3d_mlc_page_prog\000"
	.type	__func__.22989, %object
	.size	__func__.22989, 22
__func__.22989:
	.ascii	"flash_start_page_prog\000"
	.type	__func__.23013, %object
	.size	__func__.23013, 31
__func__.23013:
	.ascii	"flash_complete_plane_page_read\000"
	.type	__func__.23038, %object
	.size	__func__.23038, 25
__func__.23038:
	.ascii	"flash_complete_page_read\000"
	.type	__func__.43545, %object
	.size	__func__.43545, 31
__func__.43545:
	.ascii	"queue_wait_first_req_completed\000"
	.type	__func__.43599, %object
	.size	__func__.43599, 15
__func__.43599:
	.ascii	"sblk_prog_page\000"
	.type	__func__.43626, %object
	.size	__func__.43626, 15
__func__.43626:
	.ascii	"sblk_read_page\000"
	.type	__func__.42525, %object
	.size	__func__.42525, 21
__func__.42525:
	.ascii	"gc_check_data_one_wl\000"
	.type	__func__.22895, %object
	.size	__func__.22895, 19
__func__.22895:
	.ascii	"flash_prog_page_en\000"
	.type	__func__.43422, %object
	.size	__func__.43422, 14
__func__.43422:
	.ascii	"ftl_prog_page\000"
	.type	__func__.43079, %object
	.size	__func__.43079, 15
__func__.43079:
	.ascii	"ftl_info_flush\000"
	.type	__func__.43384, %object
	.size	__func__.43384, 19
__func__.43384:
	.ascii	"ftl_ext_info_flush\000"
	.type	__func__.43396, %object
	.size	__func__.43396, 18
__func__.43396:
	.ascii	"ftl_ext_info_init\000"
	.type	__func__.43134, %object
	.size	__func__.43134, 15
__func__.43134:
	.ascii	"ftl_alloc_sblk\000"
	.type	__func__.43752, %object
	.size	__func__.43752, 17
__func__.43752:
	.ascii	"pm_alloc_new_blk\000"
	.type	__func__.43762, %object
	.size	__func__.43762, 14
__func__.43762:
	.ascii	"pm_write_page\000"
	.type	__func__.23813, %object
	.size	__func__.23813, 17
__func__.23813:
	.ascii	"flash_info_flush\000"
	.type	__func__.23776, %object
	.size	__func__.23776, 20
__func__.23776:
	.ascii	"flash_info_blk_init\000"
	.type	__func__.23508, %object
	.size	__func__.23508, 16
__func__.23508:
	.ascii	"nand_flash_init\000"
	.type	__func__.43249, %object
	.size	__func__.43249, 16
__func__.43249:
	.ascii	"ftl_sysblk_dump\000"
	.type	__func__.43274, %object
	.size	__func__.43274, 23
__func__.43274:
	.ascii	"ftl_open_sblk_recovery\000"
	.type	__func__.43696, %object
	.size	__func__.43696, 16
__func__.43696:
	.ascii	"load_l2p_region\000"
	.type	__func__.43736, %object
	.size	__func__.43736, 6
__func__.43736:
	.ascii	"pm_gc\000"
	.type	__func__.42468, %object
	.size	__func__.42468, 12
__func__.42468:
	.ascii	"gc_recovery\000"
	.type	__func__.42557, %object
	.size	__func__.42557, 22
__func__.42557:
	.ascii	"gc_update_l2p_map_new\000"
	.type	__func__.42671, %object
	.size	__func__.42671, 16
__func__.42671:
	.ascii	"gc_scan_src_blk\000"
	.type	__func__.42732, %object
	.size	__func__.42732, 20
__func__.42732:
	.ascii	"gc_scan_static_data\000"
	.type	__func__.42795, %object
	.size	__func__.42795, 18
__func__.42795:
	.ascii	"gc_block_vpn_scan\000"
	.type	__func__.43225, %object
	.size	__func__.43225, 14
__func__.43225:
	.ascii	"ftl_sblk_dump\000"
	.type	__func__.42299, %object
	.size	__func__.42299, 10
__func__.42299:
	.ascii	"zftl_read\000"
	.type	__func__.43328, %object
	.size	__func__.43328, 19
__func__.43328:
	.ascii	"ftl_update_l2p_map\000"
	.type	__func__.42203, %object
	.size	__func__.42203, 17
__func__.42203:
	.ascii	"ftl_write_commit\000"
	.type	__func__.42600, %object
	.size	__func__.42600, 16
__func__.42600:
	.ascii	"gc_do_copy_back\000"
	.type	__func__.42850, %object
	.size	__func__.42850, 11
__func__.42850:
	.ascii	"zftl_do_gc\000"
	.type	__func__.42331, %object
	.size	__func__.42331, 13
__func__.42331:
	.ascii	"_ftl_discard\000"
	.data
	.align	2
	.set	.LANCHOR2,. + 0
	.type	zftl_debug, %object
	.size	zftl_debug, 4
zftl_debug:
	.word	17476
	.type	g_nand_para_info, %object
	.size	g_nand_para_info, 32
g_nand_para_info:
	.byte	6
	.byte	-104
	.byte	58
	.byte	-104
	.byte	-77
	.byte	118
	.byte	114
	.byte	1
	.byte	1
	.byte	32
	.short	768
	.byte	3
	.byte	2
	.short	758
	.short	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	2
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.type	sd15_tlc_rr, %object
	.size	sd15_tlc_rr, 329
sd15_tlc_rr:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-8
	.byte	0
	.byte	16
	.byte	8
	.byte	8
	.byte	0
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	-16
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	-24
	.byte	0
	.byte	0
	.byte	0
	.byte	-8
	.byte	-16
	.byte	-32
	.byte	0
	.byte	8
	.byte	-8
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	-16
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	0
	.byte	-16
	.byte	-24
	.byte	-16
	.byte	8
	.byte	8
	.byte	-8
	.byte	-16
	.byte	-16
	.byte	0
	.byte	8
	.byte	8
	.byte	8
	.byte	8
	.byte	-8
	.byte	-8
	.byte	-24
	.byte	0
	.byte	-16
	.byte	0
	.byte	-8
	.byte	-16
	.byte	-8
	.byte	-8
	.byte	0
	.byte	8
	.byte	0
	.byte	0
	.byte	-8
	.byte	0
	.byte	-24
	.byte	-8
	.byte	0
	.byte	0
	.byte	-8
	.byte	-24
	.byte	-8
	.byte	8
	.byte	-8
	.byte	0
	.byte	-8
	.byte	8
	.byte	-16
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	-8
	.byte	-8
	.byte	8
	.byte	-8
	.byte	-8
	.byte	0
	.byte	0
	.byte	-8
	.byte	-16
	.byte	-16
	.byte	-8
	.byte	0
	.byte	0
	.byte	-8
	.byte	0
	.byte	-16
	.byte	8
	.byte	0
	.byte	8
	.byte	0
	.byte	-16
	.byte	-8
	.byte	-16
	.byte	16
	.byte	0
	.byte	16
	.byte	0
	.byte	-8
	.byte	8
	.byte	0
	.byte	-24
	.byte	0
	.byte	-16
	.byte	-8
	.byte	-16
	.byte	-16
	.byte	-16
	.byte	-16
	.byte	0
	.byte	8
	.byte	-8
	.byte	-24
	.byte	0
	.byte	8
	.byte	8
	.byte	16
	.byte	16
	.byte	0
	.byte	8
	.byte	-8
	.byte	8
	.byte	16
	.byte	-8
	.byte	24
	.byte	0
	.byte	8
	.byte	-4
	.byte	0
	.byte	16
	.byte	8
	.byte	24
	.byte	8
	.byte	0
	.byte	-4
	.byte	-8
	.byte	24
	.byte	16
	.byte	16
	.byte	0
	.byte	0
	.byte	0
	.byte	-16
	.byte	0
	.byte	0
	.byte	4
	.byte	0
	.byte	-4
	.byte	-4
	.byte	-4
	.byte	8
	.byte	8
	.byte	16
	.byte	0
	.byte	16
	.byte	-4
	.byte	16
	.byte	0
	.byte	16
	.byte	8
	.byte	0
	.byte	16
	.byte	-4
	.byte	16
	.byte	-8
	.byte	0
	.byte	0
	.byte	-8
	.byte	16
	.byte	-4
	.byte	16
	.byte	-16
	.byte	-8
	.byte	-8
	.byte	-8
	.byte	8
	.byte	-4
	.byte	8
	.byte	-24
	.byte	4
	.byte	-16
	.byte	0
	.byte	8
	.byte	0
	.byte	0
	.byte	-24
	.byte	8
	.byte	-16
	.byte	8
	.byte	0
	.byte	8
	.byte	-24
	.byte	-32
	.byte	16
	.byte	-24
	.byte	8
	.byte	-8
	.byte	8
	.byte	-24
	.byte	-32
	.byte	8
	.byte	0
	.byte	16
	.byte	0
	.byte	16
	.byte	0
	.byte	-32
	.byte	4
	.byte	0
	.byte	-8
	.byte	-16
	.byte	-8
	.byte	0
	.byte	-32
	.byte	4
	.byte	0
	.byte	8
	.byte	-24
	.byte	8
	.byte	0
	.byte	-32
	.byte	4
	.byte	0
	.byte	0
	.byte	-32
	.byte	-4
	.byte	0
	.byte	-24
	.byte	4
	.byte	0
	.byte	16
	.byte	-24
	.byte	16
	.byte	0
	.byte	-24
	.byte	-4
	.byte	0
	.byte	8
	.byte	-32
	.byte	8
	.byte	0
	.byte	0
	.byte	-4
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-4
	.byte	0
	.byte	0
	.byte	0
	.byte	-4
	.byte	0
	.byte	0
	.byte	-4
	.byte	0
	.byte	-8
	.byte	0
	.byte	-8
	.byte	0
	.byte	0
	.byte	-4
	.byte	0
	.byte	-16
	.byte	0
	.byte	-16
	.byte	0
	.byte	0
	.byte	-16
	.byte	0
	.byte	-16
	.byte	0
	.byte	-16
	.byte	0
	.byte	0
	.byte	-8
	.byte	0
	.byte	-16
	.byte	0
	.byte	-16
	.byte	0
	.byte	0
	.byte	-16
	.byte	0
	.byte	-24
	.byte	0
	.byte	-24
	.byte	0
	.byte	0
	.byte	-24
	.byte	0
	.byte	-24
	.byte	0
	.byte	-24
	.byte	0
	.byte	0
	.byte	-24
	.byte	0
	.byte	-16
	.byte	0
	.byte	-16
	.byte	0
	.byte	0
	.byte	-24
	.byte	0
	.byte	-32
	.byte	0
	.byte	-32
	.byte	0
	.type	sd15_slc_rr, %object
	.size	sd15_slc_rr, 25
sd15_slc_rr:
	.byte	0
	.byte	8
	.byte	-8
	.byte	16
	.byte	-16
	.byte	24
	.byte	-24
	.byte	32
	.byte	-32
	.byte	32
	.byte	-40
	.byte	48
	.byte	-48
	.byte	56
	.byte	-56
	.byte	64
	.byte	-64
	.byte	72
	.byte	-72
	.byte	80
	.byte	-80
	.byte	88
	.byte	96
	.byte	104
	.byte	112
	.type	hy_f26_ref_value, %object
	.size	hy_f26_ref_value, 28
hy_f26_ref_value:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	10
	.byte	6
	.byte	0
	.byte	-3
	.byte	-7
	.byte	-8
	.byte	0
	.byte	-6
	.byte	-13
	.byte	-15
	.byte	0
	.byte	-11
	.byte	-20
	.byte	-23
	.byte	0
	.byte	0
	.byte	-26
	.byte	-30
	.byte	0
	.byte	0
	.byte	-32
	.byte	-37
	.type	zftl_nand_flash_para_tbl, %object
	.size	zftl_nand_flash_para_tbl, 1568
zftl_nand_flash_para_tbl:
	.byte	6
	.byte	-104
	.byte	58
	.byte	-104
	.byte	-77
	.byte	118
	.byte	114
	.byte	1
	.byte	1
	.byte	32
	.short	768
	.byte	3
	.byte	2
	.short	758
	.short	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	60
	.byte	-104
	.byte	-77
	.byte	118
	.byte	114
	.byte	1
	.byte	1
	.byte	32
	.short	768
	.byte	3
	.byte	2
	.short	1478
	.short	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	58
	.byte	-104
	.byte	-93
	.byte	118
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	384
	.byte	3
	.byte	2
	.short	1446
	.short	1497
	.byte	0
	.byte	36
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	2092
	.short	17857
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	58
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2106
	.short	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1056
	.short	17857
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	80
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	17857
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	80
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	17881
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	17857
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1058
	.short	17881
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	-47
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	69
	.byte	58
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2106
	.short	17881
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	17881
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	87
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1058
	.short	17881
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	17881
	.byte	2
	.byte	67
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	6
	.byte	69
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	17857
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.short	256
	.byte	1
	.byte	-94
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	100
	.byte	68
	.byte	50
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1048
	.short	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	512
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	86
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	24
	.short	512
	.byte	2
	.byte	2
	.short	700
	.short	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	68
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1064
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	256
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	84
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	0
	.byte	0
	.byte	0
	.byte	1
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	84
	.byte	-92
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1024
	.short	455
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	100
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	4
	.short	1024
	.short	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	256
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	52
	.byte	-92
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	512
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1044
	.short	471
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	50
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	512
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	-60
	.byte	52
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	512
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	52
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	512
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	6
	.byte	-101
	.byte	73
	.byte	1
	.byte	0
	.byte	-101
	.byte	73
	.byte	9
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	2144
	.short	-23097
	.byte	8
	.byte	21
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	8
	.byte	0
	.short	256
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	60
	.byte	-87
	.byte	4
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	0
	.byte	0
	.byte	0
	.byte	1
	.byte	5
	.byte	44
	.byte	-124
	.byte	88
	.byte	50
	.byte	-95
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	768
	.byte	3
	.byte	1
	.short	1440
	.short	3527
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	1024
	.byte	3
	.byte	0
	.byte	0
	.byte	2
	.byte	6
	.byte	44
	.byte	-92
	.byte	8
	.byte	50
	.byte	-95
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	2304
	.byte	3
	.byte	1
	.short	1008
	.short	3521
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	4096
	.byte	3
	.byte	0
	.byte	0
	.byte	4
	.byte	6
	.byte	44
	.byte	-92
	.byte	100
	.byte	50
	.byte	-86
	.byte	4
	.byte	4
	.byte	1
	.byte	32
	.short	1024
	.byte	2
	.byte	1
	.short	2192
	.short	9671
	.byte	10
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	1024
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.byte	6
	.byte	-101
	.byte	-61
	.byte	72
	.byte	37
	.byte	16
	.byte	0
	.byte	9
	.byte	1
	.byte	32
	.short	1152
	.byte	3
	.byte	2
	.short	1006
	.short	-27169
	.byte	13
	.byte	81
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.short	2048
	.byte	1
	.byte	-38
	.byte	-33
	.byte	24
	.byte	6
	.byte	-83
	.byte	-34
	.byte	20
	.byte	-85
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1056
	.short	455
	.byte	2
	.byte	6
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	2
	.byte	-65
	.byte	-66
	.byte	0
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-21
	.byte	116
	.byte	68
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	473
	.byte	1
	.byte	7
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-83
	.byte	-34
	.byte	20
	.byte	-89
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1060
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	2
	.byte	-65
	.byte	-66
	.byte	0
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-111
	.byte	96
	.byte	68
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1046
	.short	473
	.byte	1
	.byte	3
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-83
	.byte	58
	.byte	20
	.byte	-85
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2092
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-83
	.byte	-41
	.byte	20
	.byte	-98
	.byte	52
	.byte	74
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1056
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-83
	.byte	58
	.byte	20
	.byte	3
	.byte	8
	.byte	80
	.byte	2
	.byte	1
	.byte	32
	.short	388
	.byte	2
	.byte	2
	.short	1362
	.short	473
	.byte	9
	.byte	8
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.short	512
	.byte	0
	.byte	-65
	.byte	-66
	.byte	1
	.byte	5
	.byte	-119
	.byte	100
	.byte	100
	.byte	60
	.byte	-95
	.byte	0
	.byte	7
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1024
	.short	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	-119
	.byte	-124
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	7
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	512
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-119
	.byte	100
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	4
	.short	1024
	.short	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	-124
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	4
	.short	1024
	.short	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-119
	.byte	-120
	.byte	4
	.byte	75
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	4
	.short	1024
	.short	449
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	256
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	-119
	.byte	-92
	.byte	8
	.byte	50
	.byte	-95
	.byte	0
	.byte	7
	.byte	1
	.byte	32
	.short	2304
	.byte	3
	.byte	1
	.short	1008
	.short	3521
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.short	4096
	.byte	3
	.byte	0
	.byte	0
	.byte	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-108
	.byte	-61
	.byte	-92
	.byte	-54
	.byte	0
	.byte	1
	.byte	32
	.short	792
	.byte	2
	.byte	1
	.short	688
	.short	1217
	.byte	11
	.byte	50
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.short	1024
	.byte	1
	.byte	-38
	.byte	-33
	.byte	0
	.type	nand_opt_para, %object
	.size	nand_opt_para, 128
nand_opt_para:
	.byte	1
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	0
	.byte	50
	.byte	17
	.byte	-128
	.byte	112
	.byte	120
	.byte	120
	.byte	3
	.byte	1
	.byte	0
	.space	14
	.byte	2
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	0
	.byte	0
	.byte	17
	.byte	-127
	.byte	112
	.byte	-15
	.byte	-14
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.byte	3
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	96
	.byte	96
	.byte	17
	.byte	-127
	.byte	112
	.byte	-15
	.byte	-14
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.byte	4
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	96
	.byte	96
	.byte	17
	.byte	-127
	.byte	112
	.byte	112
	.byte	112
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.type	tlc_b05a_prog_tbl, %object
	.size	tlc_b05a_prog_tbl, 1536
tlc_b05a_prog_tbl:
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	9
	.short	0
	.short	11
	.short	0
	.short	13
	.short	0
	.short	15
	.short	0
	.short	17
	.short	0
	.short	19
	.short	0
	.short	21
	.short	0
	.short	23
	.short	0
	.short	25
	.short	0
	.short	27
	.short	0
	.short	29
	.short	0
	.short	31
	.short	0
	.short	33
	.short	0
	.short	35
	.short	0
	.short	37
	.short	0
	.short	39
	.short	0
	.short	41
	.short	0
	.short	43
	.short	0
	.short	45
	.short	0
	.short	47
	.short	0
	.short	49
	.short	0
	.short	51
	.short	0
	.short	53
	.short	0
	.short	55
	.short	0
	.short	25
	.short	58
	.short	0
	.short	27
	.short	61
	.short	0
	.short	29
	.short	64
	.short	0
	.short	31
	.short	67
	.short	0
	.short	33
	.short	70
	.short	0
	.short	35
	.short	73
	.short	0
	.short	37
	.short	76
	.short	0
	.short	39
	.short	79
	.short	0
	.short	41
	.short	82
	.short	0
	.short	43
	.short	85
	.short	0
	.short	45
	.short	88
	.short	0
	.short	47
	.short	91
	.short	0
	.short	49
	.short	94
	.short	0
	.short	51
	.short	97
	.short	0
	.short	53
	.short	100
	.short	0
	.short	55
	.short	103
	.short	0
	.short	58
	.short	106
	.short	0
	.short	61
	.short	109
	.short	0
	.short	64
	.short	112
	.short	0
	.short	67
	.short	115
	.short	0
	.short	70
	.short	118
	.short	0
	.short	73
	.short	121
	.short	0
	.short	76
	.short	124
	.short	0
	.short	79
	.short	127
	.short	0
	.short	82
	.short	130
	.short	0
	.short	85
	.short	133
	.short	0
	.short	88
	.short	136
	.short	0
	.short	91
	.short	139
	.short	0
	.short	94
	.short	142
	.short	0
	.short	97
	.short	145
	.short	0
	.short	100
	.short	148
	.short	0
	.short	103
	.short	151
	.short	0
	.short	106
	.short	154
	.short	0
	.short	109
	.short	157
	.short	0
	.short	112
	.short	160
	.short	0
	.short	115
	.short	163
	.short	0
	.short	118
	.short	166
	.short	0
	.short	121
	.short	169
	.short	0
	.short	124
	.short	172
	.short	0
	.short	127
	.short	175
	.short	0
	.short	130
	.short	178
	.short	0
	.short	133
	.short	181
	.short	0
	.short	136
	.short	184
	.short	0
	.short	139
	.short	187
	.short	0
	.short	142
	.short	190
	.short	0
	.short	145
	.short	193
	.short	0
	.short	148
	.short	196
	.short	0
	.short	151
	.short	199
	.short	0
	.short	154
	.short	202
	.short	0
	.short	157
	.short	205
	.short	0
	.short	160
	.short	208
	.short	0
	.short	163
	.short	211
	.short	0
	.short	166
	.short	214
	.short	0
	.short	169
	.short	217
	.short	0
	.short	172
	.short	220
	.short	0
	.short	175
	.short	223
	.short	0
	.short	178
	.short	226
	.short	0
	.short	181
	.short	229
	.short	0
	.short	184
	.short	232
	.short	0
	.short	187
	.short	235
	.short	0
	.short	190
	.short	238
	.short	0
	.short	193
	.short	241
	.short	0
	.short	196
	.short	244
	.short	0
	.short	199
	.short	247
	.short	0
	.short	202
	.short	250
	.short	0
	.short	205
	.short	253
	.short	0
	.short	208
	.short	256
	.short	0
	.short	211
	.short	259
	.short	0
	.short	214
	.short	262
	.short	0
	.short	217
	.short	265
	.short	0
	.short	220
	.short	268
	.short	0
	.short	223
	.short	271
	.short	0
	.short	226
	.short	274
	.short	0
	.short	229
	.short	277
	.short	0
	.short	232
	.short	280
	.short	0
	.short	235
	.short	283
	.short	0
	.short	238
	.short	286
	.short	0
	.short	241
	.short	289
	.short	0
	.short	244
	.short	292
	.short	0
	.short	247
	.short	295
	.short	0
	.short	250
	.short	298
	.short	0
	.short	253
	.short	301
	.short	0
	.short	256
	.short	304
	.short	0
	.short	259
	.short	307
	.short	0
	.short	262
	.short	310
	.short	0
	.short	265
	.short	313
	.short	0
	.short	268
	.short	316
	.short	0
	.short	271
	.short	319
	.short	0
	.short	274
	.short	322
	.short	0
	.short	277
	.short	325
	.short	0
	.short	280
	.short	328
	.short	0
	.short	283
	.short	331
	.short	0
	.short	286
	.short	334
	.short	0
	.short	289
	.short	337
	.short	0
	.short	292
	.short	340
	.short	0
	.short	295
	.short	343
	.short	0
	.short	298
	.short	346
	.short	0
	.short	301
	.short	349
	.short	0
	.short	304
	.short	352
	.short	0
	.short	307
	.short	355
	.short	0
	.short	310
	.short	358
	.short	0
	.short	313
	.short	361
	.short	0
	.short	316
	.short	364
	.short	0
	.short	319
	.short	367
	.short	0
	.short	322
	.short	370
	.short	0
	.short	325
	.short	373
	.short	0
	.short	328
	.short	376
	.short	0
	.short	331
	.short	379
	.short	0
	.short	334
	.short	382
	.short	0
	.short	337
	.short	385
	.short	0
	.short	340
	.short	388
	.short	0
	.short	343
	.short	391
	.short	0
	.short	346
	.short	394
	.short	0
	.short	349
	.short	397
	.short	0
	.short	352
	.short	400
	.short	0
	.short	355
	.short	403
	.short	0
	.short	358
	.short	406
	.short	0
	.short	361
	.short	409
	.short	0
	.short	364
	.short	412
	.short	0
	.short	367
	.short	415
	.short	0
	.short	370
	.short	418
	.short	0
	.short	373
	.short	421
	.short	0
	.short	376
	.short	424
	.short	0
	.short	379
	.short	427
	.short	0
	.short	382
	.short	430
	.short	0
	.short	385
	.short	433
	.short	0
	.short	388
	.short	436
	.short	0
	.short	391
	.short	439
	.short	0
	.short	394
	.short	442
	.short	0
	.short	397
	.short	445
	.short	0
	.short	400
	.short	448
	.short	0
	.short	403
	.short	451
	.short	0
	.short	406
	.short	454
	.short	0
	.short	409
	.short	457
	.short	0
	.short	412
	.short	460
	.short	0
	.short	415
	.short	463
	.short	0
	.short	418
	.short	466
	.short	0
	.short	421
	.short	469
	.short	0
	.short	424
	.short	472
	.short	0
	.short	427
	.short	475
	.short	0
	.short	430
	.short	478
	.short	0
	.short	433
	.short	481
	.short	0
	.short	436
	.short	484
	.short	0
	.short	439
	.short	487
	.short	0
	.short	442
	.short	490
	.short	0
	.short	445
	.short	493
	.short	0
	.short	448
	.short	496
	.short	0
	.short	451
	.short	499
	.short	0
	.short	454
	.short	502
	.short	0
	.short	457
	.short	505
	.short	0
	.short	460
	.short	508
	.short	0
	.short	463
	.short	511
	.short	0
	.short	466
	.short	514
	.short	0
	.short	469
	.short	517
	.short	0
	.short	472
	.short	520
	.short	0
	.short	475
	.short	523
	.short	0
	.short	478
	.short	526
	.short	0
	.short	481
	.short	529
	.short	0
	.short	484
	.short	532
	.short	0
	.short	487
	.short	535
	.short	0
	.short	490
	.short	538
	.short	0
	.short	493
	.short	541
	.short	0
	.short	496
	.short	544
	.short	0
	.short	499
	.short	547
	.short	0
	.short	502
	.short	550
	.short	0
	.short	505
	.short	553
	.short	0
	.short	508
	.short	556
	.short	0
	.short	511
	.short	559
	.short	0
	.short	514
	.short	562
	.short	0
	.short	517
	.short	565
	.short	0
	.short	520
	.short	568
	.short	0
	.short	523
	.short	571
	.short	0
	.short	526
	.short	574
	.short	0
	.short	529
	.short	577
	.short	0
	.short	532
	.short	580
	.short	0
	.short	535
	.short	583
	.short	0
	.short	538
	.short	586
	.short	0
	.short	541
	.short	589
	.short	0
	.short	544
	.short	592
	.short	0
	.short	547
	.short	595
	.short	0
	.short	550
	.short	598
	.short	0
	.short	553
	.short	601
	.short	0
	.short	556
	.short	604
	.short	0
	.short	559
	.short	607
	.short	0
	.short	562
	.short	610
	.short	0
	.short	565
	.short	613
	.short	0
	.short	568
	.short	616
	.short	0
	.short	571
	.short	619
	.short	0
	.short	574
	.short	622
	.short	0
	.short	577
	.short	625
	.short	0
	.short	580
	.short	628
	.short	0
	.short	583
	.short	631
	.short	0
	.short	586
	.short	634
	.short	0
	.short	589
	.short	637
	.short	0
	.short	592
	.short	640
	.short	0
	.short	595
	.short	643
	.short	0
	.short	598
	.short	646
	.short	0
	.short	601
	.short	649
	.short	0
	.short	604
	.short	652
	.short	0
	.short	607
	.short	655
	.short	0
	.short	610
	.short	658
	.short	0
	.short	613
	.short	661
	.short	0
	.short	616
	.short	664
	.short	0
	.short	619
	.short	667
	.short	0
	.short	622
	.short	670
	.short	0
	.short	625
	.short	673
	.short	0
	.short	628
	.short	676
	.short	0
	.short	631
	.short	679
	.short	0
	.short	634
	.short	682
	.short	0
	.short	637
	.short	685
	.short	0
	.short	640
	.short	688
	.short	0
	.short	643
	.short	691
	.short	0
	.short	646
	.short	694
	.short	0
	.short	649
	.short	697
	.short	0
	.short	652
	.short	700
	.short	0
	.short	655
	.short	703
	.short	0
	.short	658
	.short	706
	.short	0
	.short	661
	.short	709
	.short	0
	.short	664
	.short	712
	.short	0
	.short	667
	.short	715
	.short	0
	.short	670
	.short	718
	.short	0
	.short	673
	.short	721
	.short	0
	.short	676
	.short	724
	.short	0
	.short	679
	.short	727
	.short	0
	.short	682
	.short	730
	.short	0
	.short	685
	.short	733
	.short	0
	.short	688
	.short	736
	.short	0
	.short	691
	.short	739
	.short	0
	.short	694
	.short	742
	.short	0
	.short	697
	.short	745
	.short	0
	.short	700
	.short	748
	.short	0
	.short	703
	.short	751
	.short	0
	.short	706
	.short	0
	.short	709
	.short	0
	.short	712
	.short	0
	.short	715
	.short	0
	.short	718
	.short	0
	.short	721
	.short	0
	.short	724
	.short	0
	.short	727
	.short	0
	.type	tlc_prog_order, %object
	.size	tlc_prog_order, 768
tlc_prog_order:
	.short	1
	.short	9
	.short	2
	.short	17
	.short	10
	.short	3
	.short	25
	.short	18
	.short	11
	.short	33
	.short	26
	.short	19
	.short	41
	.short	34
	.short	27
	.short	49
	.short	42
	.short	35
	.short	57
	.short	50
	.short	43
	.short	65
	.short	58
	.short	51
	.short	73
	.short	66
	.short	59
	.short	81
	.short	74
	.short	67
	.short	89
	.short	82
	.short	75
	.short	97
	.short	90
	.short	83
	.short	105
	.short	98
	.short	91
	.short	113
	.short	106
	.short	99
	.short	121
	.short	114
	.short	107
	.short	129
	.short	122
	.short	115
	.short	137
	.short	130
	.short	123
	.short	145
	.short	138
	.short	131
	.short	153
	.short	146
	.short	139
	.short	161
	.short	154
	.short	147
	.short	169
	.short	162
	.short	155
	.short	177
	.short	170
	.short	163
	.short	185
	.short	178
	.short	171
	.short	193
	.short	186
	.short	179
	.short	201
	.short	194
	.short	187
	.short	209
	.short	202
	.short	195
	.short	217
	.short	210
	.short	203
	.short	225
	.short	218
	.short	211
	.short	233
	.short	226
	.short	219
	.short	241
	.short	234
	.short	227
	.short	249
	.short	242
	.short	235
	.short	257
	.short	250
	.short	243
	.short	265
	.short	258
	.short	251
	.short	273
	.short	266
	.short	259
	.short	281
	.short	274
	.short	267
	.short	289
	.short	282
	.short	275
	.short	297
	.short	290
	.short	283
	.short	305
	.short	298
	.short	291
	.short	313
	.short	306
	.short	299
	.short	321
	.short	314
	.short	307
	.short	329
	.short	322
	.short	315
	.short	337
	.short	330
	.short	323
	.short	345
	.short	338
	.short	331
	.short	353
	.short	346
	.short	339
	.short	361
	.short	354
	.short	347
	.short	369
	.short	362
	.short	355
	.short	377
	.short	370
	.short	363
	.short	385
	.short	378
	.short	371
	.short	393
	.short	386
	.short	379
	.short	401
	.short	394
	.short	387
	.short	409
	.short	402
	.short	395
	.short	417
	.short	410
	.short	403
	.short	425
	.short	418
	.short	411
	.short	433
	.short	426
	.short	419
	.short	441
	.short	434
	.short	427
	.short	449
	.short	442
	.short	435
	.short	457
	.short	450
	.short	443
	.short	465
	.short	458
	.short	451
	.short	473
	.short	466
	.short	459
	.short	481
	.short	474
	.short	467
	.short	489
	.short	482
	.short	475
	.short	497
	.short	490
	.short	483
	.short	505
	.short	498
	.short	491
	.short	513
	.short	506
	.short	499
	.short	521
	.short	514
	.short	507
	.short	529
	.short	522
	.short	515
	.short	537
	.short	530
	.short	523
	.short	545
	.short	538
	.short	531
	.short	553
	.short	546
	.short	539
	.short	561
	.short	554
	.short	547
	.short	569
	.short	562
	.short	555
	.short	577
	.short	570
	.short	563
	.short	585
	.short	578
	.short	571
	.short	593
	.short	586
	.short	579
	.short	601
	.short	594
	.short	587
	.short	609
	.short	602
	.short	595
	.short	617
	.short	610
	.short	603
	.short	625
	.short	618
	.short	611
	.short	633
	.short	626
	.short	619
	.short	641
	.short	634
	.short	627
	.short	649
	.short	642
	.short	635
	.short	657
	.short	650
	.short	643
	.short	665
	.short	658
	.short	651
	.short	673
	.short	666
	.short	659
	.short	681
	.short	674
	.short	667
	.short	689
	.short	682
	.short	675
	.short	697
	.short	690
	.short	683
	.short	705
	.short	698
	.short	691
	.short	713
	.short	706
	.short	699
	.short	721
	.short	714
	.short	707
	.short	729
	.short	722
	.short	715
	.short	737
	.short	730
	.short	723
	.short	745
	.short	738
	.short	731
	.short	753
	.short	746
	.short	739
	.short	761
	.short	754
	.short	747
	.short	769
	.short	762
	.short	755
	.short	777
	.short	770
	.short	763
	.short	785
	.short	778
	.short	771
	.short	793
	.short	786
	.short	779
	.short	801
	.short	794
	.short	787
	.short	809
	.short	802
	.short	795
	.short	817
	.short	810
	.short	803
	.short	825
	.short	818
	.short	811
	.short	833
	.short	826
	.short	819
	.short	841
	.short	834
	.short	827
	.short	849
	.short	842
	.short	835
	.short	857
	.short	850
	.short	843
	.short	865
	.short	858
	.short	851
	.short	873
	.short	866
	.short	859
	.short	881
	.short	874
	.short	867
	.short	889
	.short	882
	.short	875
	.short	897
	.short	890
	.short	883
	.short	905
	.short	898
	.short	891
	.short	913
	.short	906
	.short	899
	.short	921
	.short	914
	.short	907
	.short	929
	.short	922
	.short	915
	.short	937
	.short	930
	.short	923
	.short	945
	.short	938
	.short	931
	.short	953
	.short	946
	.short	939
	.short	961
	.short	954
	.short	947
	.short	969
	.short	962
	.short	955
	.short	977
	.short	970
	.short	963
	.short	985
	.short	978
	.short	971
	.short	993
	.short	986
	.short	979
	.short	1001
	.short	994
	.short	987
	.short	1009
	.short	1002
	.short	995
	.short	1017
	.short	1010
	.short	1003
	.short	1018
	.short	1011
	.short	1019
	.bss
	.align	6
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 8184
	.type	g_flash_slc_mode, %object
	.size	g_flash_slc_mode, 1
g_flash_slc_mode:
	.space	1
	.type	g_slc_mode_addr2, %object
	.size	g_slc_mode_addr2, 1
g_slc_mode_addr2:
	.space	1
	.type	g_block_align_addr, %object
	.size	g_block_align_addr, 2
g_block_align_addr:
	.space	2
	.type	g_lsb_page_tbl, %object
	.size	g_lsb_page_tbl, 1024
g_lsb_page_tbl:
	.space	1024
	.type	g_nandc_ver, %object
	.size	g_nandc_ver, 1
g_nandc_ver:
	.space	1
	.space	3
	.type	_c_user_data_density, %object
	.size	_c_user_data_density, 4
_c_user_data_density:
	.space	4
	.type	gp_sblk_list_tbl, %object
	.size	gp_sblk_list_tbl, 4
gp_sblk_list_tbl:
	.space	4
	.type	gp_flash_info, %object
	.size	gp_flash_info, 4
gp_flash_info:
	.space	4
	.type	gp_nandc, %object
	.size	gp_nandc, 4
gp_nandc:
	.space	4
	.type	NANDC_FMCTL, %object
	.size	NANDC_FMCTL, 4
NANDC_FMCTL:
	.space	4
	.type	NANDC_FMWAIT, %object
	.size	NANDC_FMWAIT, 4
NANDC_FMWAIT:
	.space	4
	.type	NANDC_FLCTL, %object
	.size	NANDC_FLCTL, 4
NANDC_FLCTL:
	.space	4
	.type	NANDC_BCHCTL, %object
	.size	NANDC_BCHCTL, 4
NANDC_BCHCTL:
	.space	4
	.type	NANDC_DLL_CTL_REG0, %object
	.size	NANDC_DLL_CTL_REG0, 4
NANDC_DLL_CTL_REG0:
	.space	4
	.type	NANDC_DLL_CTL_REG1, %object
	.size	NANDC_DLL_CTL_REG1, 4
NANDC_DLL_CTL_REG1:
	.space	4
	.type	NANDC_RANDMZ_CFG, %object
	.size	NANDC_RANDMZ_CFG, 4
NANDC_RANDMZ_CFG:
	.space	4
	.type	NANDC_FMWAIT_SYN, %object
	.size	NANDC_FMWAIT_SYN, 4
NANDC_FMWAIT_SYN:
	.space	4
	.type	_c_ftl_blk_pre_plane, %object
	.size	_c_ftl_blk_pre_plane, 2
_c_ftl_blk_pre_plane:
	.space	2
	.space	2
	.type	gp_blk_info, %object
	.size	gp_blk_info, 4
gp_blk_info:
	.space	4
	.type	_c_slc_to_xlc_ec_ratio, %object
	.size	_c_slc_to_xlc_ec_ratio, 2
_c_slc_to_xlc_ec_ratio:
	.space	2
	.space	2
	.type	ftl_sblk_vpn, %object
	.size	ftl_sblk_vpn, 4
ftl_sblk_vpn:
	.space	4
	.type	gp_ftl_ext_info, %object
	.size	gp_ftl_ext_info, 4
gp_ftl_ext_info:
	.space	4
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.space	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.space	1
	.space	2
	.type	gp_nand_para_info, %object
	.size	gp_nand_para_info, 4
gp_nand_para_info:
	.space	4
	.type	g_idb_ecc_bits, %object
	.size	g_idb_ecc_bits, 1
g_idb_ecc_bits:
	.space	1
	.type	g_nand_max_die, %object
	.size	g_nand_max_die, 1
g_nand_max_die:
	.space	1
	.type	g_idb_slc_mode_enable, %object
	.size	g_idb_slc_mode_enable, 1
g_idb_slc_mode_enable:
	.space	1
	.type	g_nand_opt_para, %object
	.size	g_nand_opt_para, 32
g_nand_opt_para:
	.space	32
	.type	g_flash_toggle_mode_en, %object
	.size	g_flash_toggle_mode_en, 1
g_flash_toggle_mode_en:
	.space	1
	.type	g_die_cs_idx, %object
	.size	g_die_cs_idx, 8
g_die_cs_idx:
	.space	8
	.type	g_flash_six_addr, %object
	.size	g_flash_six_addr, 1
g_flash_six_addr:
	.space	1
	.type	_c_ftl_cs_bits, %object
	.size	_c_ftl_cs_bits, 1
_c_ftl_cs_bits:
	.space	1
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 4
g_flash_cur_mode:
	.space	4
	.type	g_flash_micron_3d_tlc_flag, %object
	.size	g_flash_micron_3d_tlc_flag, 1
g_flash_micron_3d_tlc_flag:
	.space	1
	.type	g_flash_ymtc_3d_tlc_flag, %object
	.size	g_flash_ymtc_3d_tlc_flag, 1
g_flash_ymtc_3d_tlc_flag:
	.space	1
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.space	32
	.type	g_flash_interface_mode, %object
	.size	g_flash_interface_mode, 1
g_flash_interface_mode:
	.space	1
	.type	g_nandc_ecc_bits, %object
	.size	g_nandc_ecc_bits, 1
g_nandc_ecc_bits:
	.space	1
	.type	g_flash_multi_page_prog_en, %object
	.size	g_flash_multi_page_prog_en, 1
g_flash_multi_page_prog_en:
	.space	1
	.type	nandc_hw_seed, %object
	.size	nandc_hw_seed, 1
nandc_hw_seed:
	.space	1
	.type	nandc_randomizer_en, %object
	.size	nandc_randomizer_en, 1
nandc_randomizer_en:
	.space	1
	.space	3
	.type	g_nandc_v6_master_info, %object
	.size	g_nandc_v6_master_info, 28
g_nandc_v6_master_info:
	.space	28
	.type	fill_spare_size, %object
	.size	fill_spare_size, 2
fill_spare_size:
	.space	2
	.space	2
	.type	g_buf, %object
	.size	g_buf, 1536
g_buf:
	.space	1536
	.type	p_free_buf_head, %object
	.size	p_free_buf_head, 1
p_free_buf_head:
	.space	1
	.type	free_buf_count, %object
	.size	free_buf_count, 1
free_buf_count:
	.space	1
	.type	sblk_queue_head, %object
	.size	sblk_queue_head, 1
sblk_queue_head:
	.space	1
	.type	sblk_read_completed_queue_head, %object
	.size	sblk_read_completed_queue_head, 1
sblk_read_completed_queue_head:
	.space	1
	.type	sblk_gc_write_completed_queue_head, %object
	.size	sblk_gc_write_completed_queue_head, 1
sblk_gc_write_completed_queue_head:
	.space	1
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.space	1
	.space	2
	.type	_c_totle_phy_density, %object
	.size	_c_totle_phy_density, 4
_c_totle_phy_density:
	.space	4
	.type	_c_totle_log_page, %object
	.size	_c_totle_log_page, 4
_c_totle_log_page:
	.space	4
	.type	free_slc_sblk, %object
	.size	free_slc_sblk, 2
free_slc_sblk:
	.space	2
	.type	free_xlc_sblk, %object
	.size	free_xlc_sblk, 2
free_xlc_sblk:
	.space	2
	.type	free_mix_sblk, %object
	.size	free_mix_sblk, 2
free_mix_sblk:
	.space	2
	.type	slc_data_sblk, %object
	.size	slc_data_sblk, 2
slc_data_sblk:
	.space	2
	.type	slc_cache_sblk, %object
	.size	slc_cache_sblk, 2
slc_cache_sblk:
	.space	2
	.type	xlc_data_sblk, %object
	.size	xlc_data_sblk, 2
xlc_data_sblk:
	.space	2
	.type	write_buf_count, %object
	.size	write_buf_count, 1
write_buf_count:
	.space	1
	.type	write_commit_count, %object
	.size	write_commit_count, 1
write_commit_count:
	.space	1
	.space	2
	.type	gp_ftl_info, %object
	.size	gp_ftl_info, 4
gp_ftl_info:
	.space	4
	.type	gc_free_slc_sblk_th, %object
	.size	gc_free_slc_sblk_th, 2
gc_free_slc_sblk_th:
	.space	2
	.type	gc_tlc_mode_slc_vpn_th, %object
	.size	gc_tlc_mode_slc_vpn_th, 2
gc_tlc_mode_slc_vpn_th:
	.space	2
	.type	gc_tlc_mode_tlc_vpn_th, %object
	.size	gc_tlc_mode_tlc_vpn_th, 2
gc_tlc_mode_tlc_vpn_th:
	.space	2
	.space	2
	.type	_gc_after_discard_en, %object
	.size	_gc_after_discard_en, 4
_gc_after_discard_en:
	.space	4
	.type	gc_slc_mode_tlc_vpn_th, %object
	.size	gc_slc_mode_tlc_vpn_th, 2
gc_slc_mode_tlc_vpn_th:
	.space	2
	.type	gc_slc_mode_vpn_th, %object
	.size	gc_slc_mode_vpn_th, 2
gc_slc_mode_vpn_th:
	.space	2
	.type	write_buf_head, %object
	.size	write_buf_head, 1
write_buf_head:
	.space	1
	.space	3
	.type	g_gc_info, %object
	.size	g_gc_info, 2204
g_gc_info:
	.space	2204
	.type	ftl_sblk_vpn_update_id, %object
	.size	ftl_sblk_vpn_update_id, 2
ftl_sblk_vpn_update_id:
	.space	2
	.type	ftl_sblk_update_list, %object
	.size	ftl_sblk_update_list, 16
ftl_sblk_update_list:
	.space	16
	.type	_c_ftl_block_addr_log2, %object
	.size	_c_ftl_block_addr_log2, 2
_c_ftl_block_addr_log2:
	.space	2
	.type	_c_ftl_planes_per_die, %object
	.size	_c_ftl_planes_per_die, 1
_c_ftl_planes_per_die:
	.space	1
	.space	3
	.type	gc_valid_page_ppa, %object
	.size	gc_valid_page_ppa, 4
gc_valid_page_ppa:
	.space	4
	.type	_c_ftl_nand_type, %object
	.size	_c_ftl_nand_type, 1
_c_ftl_nand_type:
	.space	1
	.type	_c_ftl_nand_planes_num, %object
	.size	_c_ftl_nand_planes_num, 1
_c_ftl_nand_planes_num:
	.space	1
	.type	g_flash_3d_mlc_flag, %object
	.size	g_flash_3d_mlc_flag, 1
g_flash_3d_mlc_flag:
	.space	1
	.type	g_one_pass_program, %object
	.size	g_one_pass_program, 1
g_one_pass_program:
	.space	1
	.type	gc_page_buf_id, %object
	.size	gc_page_buf_id, 4
gc_page_buf_id:
	.space	4
	.type	g_flash_3d_tlc_flag, %object
	.size	g_flash_3d_tlc_flag, 1
g_flash_3d_tlc_flag:
	.space	1
	.type	gc_mode, %object
	.size	gc_mode, 1
gc_mode:
	.space	1
	.type	_c_ftl_page_pre_blk, %object
	.size	_c_ftl_page_pre_blk, 2
_c_ftl_page_pre_blk:
	.space	2
	.type	gp_data_slc_data_head, %object
	.size	gp_data_slc_data_head, 4
gp_data_slc_data_head:
	.space	4
	.type	gc_slc_data_index, %object
	.size	gc_slc_data_index, 2
gc_slc_data_index:
	.space	2
	.type	gc_slc_cache_index, %object
	.size	gc_slc_cache_index, 2
gc_slc_cache_index:
	.space	2
	.type	gc_xlc_data_index, %object
	.size	gc_xlc_data_index, 2
gc_xlc_data_index:
	.space	2
	.space	2
	.type	gp_data_slc_cache_head, %object
	.size	gp_data_slc_cache_head, 4
gp_data_slc_cache_head:
	.space	4
	.type	gp_data_xlc_data_head, %object
	.size	gp_data_xlc_data_head, 4
gp_data_xlc_data_head:
	.space	4
	.type	_c_ftl_page_pre_slc_blk, %object
	.size	_c_ftl_page_pre_slc_blk, 2
_c_ftl_page_pre_slc_blk:
	.space	2
	.type	gc_xlc_search_index, %object
	.size	gc_xlc_search_index, 2
gc_xlc_search_index:
	.space	2
	.type	_min_slc_super_block, %object
	.size	_min_slc_super_block, 2
_min_slc_super_block:
	.space	2
	.type	_max_xlc_super_block, %object
	.size	_max_xlc_super_block, 2
_max_xlc_super_block:
	.space	2
	.type	gp_free_slc_head, %object
	.size	gp_free_slc_head, 4
gp_free_slc_head:
	.space	4
	.type	gp_free_xlc_head, %object
	.size	gp_free_xlc_head, 4
gp_free_xlc_head:
	.space	4
	.type	gp_free_mix_head, %object
	.size	gp_free_mix_head, 4
gp_free_mix_head:
	.space	4
	.type	zftl_print_list_count, %object
	.size	zftl_print_list_count, 2
zftl_print_list_count:
	.space	2
	.type	_c_ftl_block_align_addr, %object
	.size	_c_ftl_block_align_addr, 2
_c_ftl_block_align_addr:
	.space	2
	.type	_c_ftl_nand_die_num, %object
	.size	_c_ftl_nand_die_num, 1
_c_ftl_nand_die_num:
	.space	1
	.space	1
	.type	lpa_hash, %object
	.size	lpa_hash, 512
lpa_hash:
	.space	512
	.space	2
	.type	ftl_sblk_lpa_tbl, %object
	.size	ftl_sblk_lpa_tbl, 4
ftl_sblk_lpa_tbl:
	.space	4
	.type	lpa_hash_index, %object
	.size	lpa_hash_index, 4
lpa_hash_index:
	.space	4
	.type	ftl_vpn_update_count, %object
	.size	ftl_vpn_update_count, 2
ftl_vpn_update_count:
	.space	2
	.type	_c_ftl_sec_per_page, %object
	.size	_c_ftl_sec_per_page, 1
_c_ftl_sec_per_page:
	.space	1
	.space	1
	.type	ftl_sblk_update_list_offset, %object
	.size	ftl_sblk_update_list_offset, 2
ftl_sblk_update_list_offset:
	.space	2
	.type	g_flash_micron_3d_tlc_b05a, %object
	.size	g_flash_micron_3d_tlc_b05a, 1
g_flash_micron_3d_tlc_b05a:
	.space	1
	.space	1
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.space	2
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.space	2
	.type	read_buf_head, %object
	.size	read_buf_head, 1
read_buf_head:
	.space	1
	.type	read_buf_count, %object
	.size	read_buf_count, 1
read_buf_count:
	.space	1
	.space	2
	.type	pm_ram_info, %object
	.size	pm_ram_info, 256
pm_ram_info:
	.space	256
	.type	pm_last_update_ram_id, %object
	.size	pm_last_update_ram_id, 1
pm_last_update_ram_id:
	.space	1
	.space	3
	.type	g_msb_page_tbl, %object
	.size	g_msb_page_tbl, 2048
g_msb_page_tbl:
	.space	2048
	.type	g_slc_page_num, %object
	.size	g_slc_page_num, 2
g_slc_page_num:
	.space	2
	.space	2
	.type	g_die_addr, %object
	.size	g_die_addr, 32
g_die_addr:
	.space	32
	.type	g_totle_phy_block, %object
	.size	g_totle_phy_block, 2
g_totle_phy_block:
	.space	2
	.space	2
	.type	pm_force_gc, %object
	.size	pm_force_gc, 4
pm_force_gc:
	.space	4
	.type	_c_swl_slc_gc_th, %object
	.size	_c_swl_slc_gc_th, 2
_c_swl_slc_gc_th:
	.space	2
	.type	_c_swl_xlc_gc_th, %object
	.size	_c_swl_xlc_gc_th, 2
_c_swl_xlc_gc_th:
	.space	2
	.type	_c_max_pm_sblk, %object
	.size	_c_max_pm_sblk, 2
_c_max_pm_sblk:
	.space	2
	.space	6
	.type	power_on_init_jiffies, %object
	.size	power_on_init_jiffies, 8
power_on_init_jiffies:
	.space	8
	.type	gp_ftl_api, %object
	.size	gp_ftl_api, 4
gp_ftl_api:
	.space	4
	.type	RK29_NANDC_REG_BASE, %object
	.size	RK29_NANDC_REG_BASE, 4
RK29_NANDC_REG_BASE:
	.space	4
	.type	ftl_dma32_buffer_size, %object
	.size	ftl_dma32_buffer_size, 4
ftl_dma32_buffer_size:
	.space	4
	.type	ftl_dma32_buffer, %object
	.size	ftl_dma32_buffer, 4
ftl_dma32_buffer:
	.space	4
	.type	gc_state, %object
	.size	gc_state, 1
gc_state:
	.space	1
	.space	3
	.type	gc_search_count, %object
	.size	gc_search_count, 4
gc_search_count:
	.space	4
	.type	gc_slc_mode_slc_vpn_th, %object
	.size	gc_slc_mode_slc_vpn_th, 2
gc_slc_mode_slc_vpn_th:
	.space	2
	.space	2
	.type	gc_lpa_tbl, %object
	.size	gc_lpa_tbl, 4
gc_lpa_tbl:
	.space	4
	.type	gc_pre_ppa_tbl, %object
	.size	gc_pre_ppa_tbl, 4
gc_pre_ppa_tbl:
	.space	4
	.type	gc_des_ppa_tbl, %object
	.size	gc_des_ppa_tbl, 4
gc_des_ppa_tbl:
	.space	4
	.type	g_flash_tmp_page_buffer, %object
	.size	g_flash_tmp_page_buffer, 4
g_flash_tmp_page_buffer:
	.space	4
	.type	g_nandc_tran_timeout, %object
	.size	g_nandc_tran_timeout, 1
g_nandc_tran_timeout:
	.space	1
	.space	3
	.type	g_flash_tmp_spare_buffer, %object
	.size	g_flash_tmp_spare_buffer, 4
g_flash_tmp_spare_buffer:
	.space	4
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.space	1
	.space	3
	.type	flash_ddr_tuning_sdr_read_count, %object
	.size	flash_ddr_tuning_sdr_read_count, 4
flash_ddr_tuning_sdr_read_count:
	.space	4
	.type	flash_read_retry, %object
	.size	flash_read_retry, 4
flash_read_retry:
	.space	4
	.type	g_flash_spare_buffer, %object
	.size	g_flash_spare_buffer, 4
g_flash_spare_buffer:
	.space	4
	.type	g_flash_page_buffer, %object
	.size	g_flash_page_buffer, 4
g_flash_page_buffer:
	.space	4
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.space	1
	.space	3
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.space	4
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
	.space	1
	.space	3
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 4
ftl_info_spare:
	.space	4
	.space	16
	.type	g_ftl_info_blk, %object
	.size	g_ftl_info_blk, 4
g_ftl_info_blk:
	.space	4
	.type	ftl_info_data_buffer, %object
	.size	ftl_info_data_buffer, 4
ftl_info_data_buffer:
	.space	4
	.type	ftl_sys_info_first_write, %object
	.size	ftl_sys_info_first_write, 1
ftl_sys_info_first_write:
	.space	1
	.type	ftl_power_lost_flag, %object
	.size	ftl_power_lost_flag, 1
ftl_power_lost_flag:
	.space	1
	.type	ftl_ext_info_first_write, %object
	.size	ftl_ext_info_first_write, 1
ftl_ext_info_first_write:
	.space	1
	.space	1
	.type	ftl_ext_info_data_buffer, %object
	.size	ftl_ext_info_data_buffer, 4
ftl_ext_info_data_buffer:
	.space	4
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 4
ftl_tmp_spare:
	.space	4
	.type	pm_gc_enable, %object
	.size	pm_gc_enable, 4
pm_gc_enable:
	.space	4
	.type	g_pm_spare, %object
	.size	g_pm_spare, 4
g_pm_spare:
	.space	4
	.type	pm_first_write, %object
	.size	pm_first_write, 1
pm_first_write:
	.space	1
	.space	3
	.type	g_flash_sys_spare_buffer, %object
	.size	g_flash_sys_spare_buffer, 4
g_flash_sys_spare_buffer:
	.space	4
	.type	g_flash_blk_info, %object
	.size	g_flash_blk_info, 4
g_flash_blk_info:
	.space	4
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.space	1
	.type	g_flash_micron_3d_tlc_b16a, %object
	.size	g_flash_micron_3d_tlc_b16a, 1
g_flash_micron_3d_tlc_b16a:
	.space	1
	.type	_c_ftl_byte_pre_page, %object
	.size	_c_ftl_byte_pre_page, 2
_c_ftl_byte_pre_page:
	.space	2
	.type	pm_last_load_ram_id, %object
	.size	pm_last_load_ram_id, 1
pm_last_load_ram_id:
	.space	1
	.type	_ftl_gc_tag_page_num, %object
	.size	_ftl_gc_tag_page_num, 1
_ftl_gc_tag_page_num:
	.space	1
	.space	2
	.type	_last_read_time, %object
	.size	_last_read_time, 4
_last_read_time:
	.space	4
	.type	_last_write_time, %object
	.size	_last_write_time, 4
_last_write_time:
	.space	4
	.type	read_ahead_lpa, %object
	.size	read_ahead_lpa, 4
read_ahead_lpa:
	.space	4
	.type	_c_totle_data_density, %object
	.size	_c_totle_data_density, 4
_c_totle_data_density:
	.space	4
	.type	_c_ftl_pm_page_num, %object
	.size	_c_ftl_pm_page_num, 2
_c_ftl_pm_page_num:
	.space	2
	.space	2
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 4
ftl_tmp_buffer:
	.space	4
	.type	rk_zftl_enable, %object
	.size	rk_zftl_enable, 1
rk_zftl_enable:
	.space	1
	.space	3
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 4
gLoaderBootInfo:
	.space	4
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 4
RK29_NANDC1_REG_BASE:
	.space	4
	.type	discard_sector_count, %object
	.size	discard_sector_count, 4
discard_sector_count:
	.space	4
	.type	idb_write_enable, %object
	.size	idb_write_enable, 1
idb_write_enable:
	.space	1
	.space	3
	.type	idb_buf, %object
	.size	idb_buf, 4
idb_buf:
	.space	4
	.type	idb_last_lba, %object
	.size	idb_last_lba, 4
idb_last_lba:
	.space	4
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 4
g_idb_buffer:
	.space	4
	.type	g_vendor, %object
	.size	g_vendor, 4
g_vendor:
	.space	4
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.space	4
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.space	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.space	4
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 4
gpVendor1Info:
	.space	4
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 4
gpVendor0Info:
	.space	4
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.space	512
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 4
gpDrmKeyInfo:
	.space	4
	.type	gpBootConfig, %object
	.size	gpBootConfig, 4
gpBootConfig:
	.space	4
	.type	ftl_low_format_cur_blk, %object
	.size	ftl_low_format_cur_blk, 2
ftl_low_format_cur_blk:
	.space	2
	.space	2
	.type	p_read_ahead_ext_buf, %object
	.size	p_read_ahead_ext_buf, 4
p_read_ahead_ext_buf:
	.space	4
	.type	_c_ftl_nand_blks_per_die, %object
	.size	_c_ftl_nand_blks_per_die, 2
_c_ftl_nand_blks_per_die:
	.space	2
	.type	nandc_ecc_sts, %object
	.size	nandc_ecc_sts, 16
nandc_ecc_sts:
	.space	16
	.type	g_slc_mode_enable, %object
	.size	g_slc_mode_enable, 1
g_slc_mode_enable:
	.space	1
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC1:
	.ascii	"FTL version: 6.0.24 20210716\000"
.LC2:
	.ascii	"%s\012\000"
.LC3:
	.ascii	"zftl_debug:0x%x\012\000"
.LC4:
	.ascii	"...%s enter...\012\000"
.LC5:
	.ascii	"No.0 FLASH ID: %x %x %x %x %x %x\012\000"
.LC6:
	.ascii	"DiePerChip: %x\012\000"
.LC7:
	.ascii	"SectPerPage: %x\012\000"
.LC8:
	.ascii	"PagePerBlk: %x\012\000"
.LC9:
	.ascii	"Cell: %x\012\000"
.LC10:
	.ascii	"PlanePerDie: %x\012\000"
.LC11:
	.ascii	"BlkPerPlane: %x\012\000"
.LC12:
	.ascii	"die gap: %x\012\000"
.LC13:
	.ascii	"lsbMode: %x\012\000"
.LC14:
	.ascii	"ReadRetryMode: %x\012\000"
.LC15:
	.ascii	"ecc: %x\012\000"
.LC16:
	.ascii	"idb ecc: %x\012\000"
.LC17:
	.ascii	"OptMode: %x\012\000"
.LC18:
	.ascii	"g_nand_max_die: %x\012\000"
.LC19:
	.ascii	"Cache read enable: %x\012\000"
.LC20:
	.ascii	"Cache random read enable: %x\012\000"
.LC21:
	.ascii	"Cache prog enable: %x\012\000"
.LC22:
	.ascii	"multi read enable: %x\012\000"
.LC23:
	.ascii	"multi prog enable: %x\012\000"
.LC24:
	.ascii	"interleave enable: %x\012\000"
.LC25:
	.ascii	"read retry enable: %x\012\000"
.LC26:
	.ascii	"randomizer enable: %x\012\000"
.LC27:
	.ascii	"SDR enable: %x\012\000"
.LC28:
	.ascii	"ONFI enable: %x\012\000"
.LC29:
	.ascii	"TOGGLE enable: %x\012\000"
.LC30:
	.ascii	"g_flash_slc_mode: %x %x\012\000"
.LC31:
	.ascii	"MultiPlaneProgCmd: %x %x\012\000"
.LC32:
	.ascii	"MultiPlaneReadCmd: %x %x\012\000"
.LC33:
	.ascii	"g_flash_toggle_mode_en: %x\012\000"
.LC34:
	.ascii	"nand sdr mode %x\012\000"
.LC35:
	.ascii	"nand ddr mode %x\012\000"
.LC36:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC37:
	.ascii	"otp:%x %x %x %x\012\000"
.LC38:
	.ascii	"bad block test:%x %x\012\000"
.LC39:
	.ascii	"flash_erase_duplane_block %x %x %x\012\000"
.LC40:
	.ascii	"flash_erase_duplane_block pageadd = %x status = %x\012"
	.ascii	"\000"
.LC41:
	.ascii	"flash_erase_block %x %x %x\012\000"
.LC42:
	.ascii	"flash_erase_block %d block = %x status = %x\012\000"
.LC43:
	.ascii	"erase done: %x\012\000"
.LC44:
	.ascii	"sblk_queue_head = %d\012\000"
.LC45:
	.ascii	"sblk_read_completed_queue_head = %d\012\000"
.LC46:
	.ascii	"sblk_gc_write_completed_queue_head = %d\012\000"
.LC47:
	.ascii	"sblk_write_completed_queue_head = %d\012\000"
.LC48:
	.ascii	"p_free_buf_head = %d\012\000"
.LC49:
	.ascii	"free_buf_count = %d\012\000"
.LC50:
	.ascii	"buf = %d, next=%d, flag=%d gc_write_flag=%d, lun_st"
	.ascii	"ate=%d, op_status = %d lpa=%x, ppa=%x\012\000"
.LC51:
	.ascii	"flash_mask_bad_block %d %d\012\000"
.LC52:
	.ascii	"zftl_debug\000"
.LC53:
	.ascii	"FLASH ID: %x %x %x %x %x %x\012\000"
.LC54:
	.ascii	"density: %d MB\012\000"
.LC55:
	.ascii	"device density: %d MB\012\000"
.LC56:
	.ascii	"FTL INFO:\012\000"
.LC57:
	.ascii	"max_lpn = 0x%x\012\000"
.LC58:
	.ascii	"density = 0x%x\012\000"
.LC59:
	.ascii	"slc vpn = 0x%x\012\000"
.LC60:
	.ascii	"xlc vpn = 0x%x\012\000"
.LC61:
	.ascii	"free slc blk = 0x%x\012\000"
.LC62:
	.ascii	"free xlc blk = 0x%x\012\000"
.LC63:
	.ascii	"free mix blk = 0x%x\012\000"
.LC64:
	.ascii	"slc data blk = 0x%x\012\000"
.LC65:
	.ascii	"slc cache blk = 0x%x\012\000"
.LC66:
	.ascii	"xlc data blk = 0x%x\012\000"
.LC67:
	.ascii	"free buf = %d, %d, %d\012\000"
.LC68:
	.ascii	"bad blk = %d %d\012\000"
.LC69:
	.ascii	"TBW = %d MB\012\000"
.LC70:
	.ascii	"TBR = %d MB\012\000"
.LC71:
	.ascii	"POC = %d\012\000"
.LC72:
	.ascii	"PLC = %d\012\000"
.LC73:
	.ascii	"sys run time = %d S\012\000"
.LC74:
	.ascii	"slc mode = %x %x %x\012\000"
.LC75:
	.ascii	"prog err = %d\012\000"
.LC76:
	.ascii	"read err = %d\012\000"
.LC77:
	.ascii	"GC XLC page = %d\012\000"
.LC78:
	.ascii	"GC SLC page = %d\012\000"
.LC79:
	.ascii	"discard page = 0x%x\012\000"
.LC80:
	.ascii	"version = %d\012\000"
.LC81:
	.ascii	"acblk = 0x%x %d %d\012\000"
.LC82:
	.ascii	"tmblk = 0x%x %d %d\012\000"
.LC83:
	.ascii	"gcblk = 0x%x %d %d\012\000"
.LC84:
	.ascii	"slc ec = %d, %d, %d, %d, %d\012\000"
.LC85:
	.ascii	"xlc ec = %d, %d, %d, %d, %d\012\000"
.LC86:
	.ascii	"gc free blk th = %d\012\000"
.LC87:
	.ascii	"gc vpn th = %d %d %d %d %d\012\000"
.LC88:
	.ascii	"swl blk = %x %x %x %x\012\000"
.LC89:
	.ascii	"rf info = %x %x %x %x %x\012\000"
.LC90:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\012\000"
.LC91:
	.ascii	"gc_add_sblk = %d, %d, %d\012\000"
.LC92:
	.ascii	"gc_add_sblk = %d, %d, %d,last update:%d, %d\012\000"
.LC93:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d\012\000"
.LC94:
	.ascii	"gc_mark_bad_ppa %d %x %x\012\000"
.LC95:
	.ascii	"status: %x, ppa: %x\012\000"
.LC96:
	.ascii	"%d gc_free_temp_buf buf id= %x\012\000"
.LC97:
	.ascii	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\012"
	.ascii	"\000"
.LC98:
	.ascii	"zftl_get_gc_node cache = %x index = %d vpn = %x\012"
	.ascii	"\000"
.LC99:
	.ascii	"gc_search_src_blk mode = %x, src mode = %x, count= "
	.ascii	"%d %d\012\000"
.LC100:
	.ascii	"swl_tlc_free_mini_ec_blk alloc sblk %x\012\000"
.LC101:
	.ascii	"zftl_get_free_sblk %x %d, %p %d %d\012\000"
.LC102:
	.ascii	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\012\000"
.LC103:
	.ascii	"swl_slc_free_mini_ec_blk alloc sblk %x\012\000"
.LC104:
	.ascii	"list count:%p %d\012\000"
.LC105:
	.ascii	"%d: node:%x %x %x %x, %d %d %d %d %d\012\000"
.LC106:
	.ascii	"ftl_vpn_decrement %x = %d, %d\012\000"
.LC107:
	.ascii	"mask bad block:cs %x %x block: %x %x\012\000"
.LC108:
	.ascii	"gc_free_bad_sblk 0x%x\012\000"
.LC109:
	.ascii	"swl_slc_free_mini_ec_blk sblk %x\012\000"
.LC110:
	.ascii	"gc_free_src_blk = %x, vpn = %d\012\000"
.LC111:
	.ascii	"gc_free_src_blk %x, %d\012\000"
.LC112:
	.ascii	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk"
	.ascii	":s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\012\000"
.LC113:
	.ascii	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\012\000"
.LC114:
	.ascii	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\012\000"
.LC115:
	.ascii	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\012\000"
.LC116:
	.ascii	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:"
	.ascii	"%d free_th: %d\012\000"
.LC117:
	.ascii	"swl : %x %x %x %x %x %x\012\000"
.LC118:
	.ascii	"ftl prog error =%x, lpa = %x, ppa= %x\012\000"
.LC119:
	.ascii	"ftl re prog: lpa = %x, ppa= %x\012\000"
.LC120:
	.ascii	"dump_sblk_queue: %d\012\000"
.LC121:
	.ascii	"buf id= %d state = %d ppa = %x\012\000"
.LC122:
	.ascii	"%s %d %d\012\000"
.LC123:
	.ascii	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d"
	.ascii	" ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC124:
	.ascii	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC125:
	.ascii	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC126:
	.ascii	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC127:
	.ascii	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC128:
	.ascii	"gc_static_wearleveling: slc blk: %x, tlc blk: %d av"
	.ascii	"g slc ec: %d, avg tlc ec: %d \012\000"
.LC129:
	.ascii	"gc_static_wearleveling: min slc ec: %x, min tlc ec:"
	.ascii	" %d max slc ec: %d, max tlc ec: %d; %d %d\012\000"
.LC130:
	.ascii	"swl add tlc gc = %x, %d, %d, %d, %d, %d\012\000"
.LC131:
	.ascii	"swl add slc gc  = %x, %d, %d, %d, %d, %d\012\000"
.LC132:
	.ascii	"free blk vpn error: %x %x\012\000"
.LC133:
	.ascii	"GC PM block %x %x %x %d\012\000"
.LC134:
	.ascii	"ftl_free_no_use_map_blk %x %x %x %d\012\000"
.LC135:
	.ascii	"...%d @ %s\012\000"
.LC136:
	.ascii	"...%s enter... %p\012\000"
.LC137:
	.ascii	"0:%x %x %x %x %x\012\000"
.LC138:
	.ascii	"g_nandc_ver...%d\012\000"
.LC139:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC140:
	.ascii	"\0013\000"
.LC141:
	.ascii	"otp error! %d\000"
.LC142:
	.ascii	"rr\000"
.LC143:
	.ascii	"flash_abort_clear = %d\012\000"
.LC144:
	.ascii	"%d mtrans_cnt = %d page_num = %d\012\000"
.LC145:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC146:
	.ascii	"nandc:\000"
.LC147:
	.ascii	"nandc_xfer_done read error %x\012\000"
.LC148:
	.ascii	"dqs data abort %x\012\000"
.LC149:
	.ascii	"dqs data timeout %x\012\000"
.LC150:
	.ascii	"xfer error %x\012\000"
.LC151:
	.ascii	"MT %d row=%x,last status %d,status = %d\012\000"
.LC152:
	.ascii	"MT RR %d row=%x,count %d,status=%d\012\000"
.LC153:
	.ascii	"toshiba SRR %d row=%x, status=%d\012\000"
.LC154:
	.ascii	"toshiba TRR %d row=%x, status=%d\012\000"
.LC155:
	.ascii	"toshiba RR %d row=%x,count %d,status=%d\012\000"
.LC156:
	.ascii	"YMTC RR %d row=%x,count %d,status=%d\012\000"
.LC157:
	.ascii	"samsung SRR %d row=%x, status=%d\012\000"
.LC158:
	.ascii	"samsung TRR %d row=%x, status=%d\012\000"
.LC159:
	.ascii	"samsung RR %d row=%x,count %d,status=%d\012\000"
.LC160:
	.ascii	"hynix RR %d row=%x, count %d, status=%d\012\000"
.LC161:
	.ascii	"%d flash_ddr_tuning_read %x ecc=%d\012\000"
.LC162:
	.ascii	"sync para %d\012\000"
.LC163:
	.ascii	"DDR mode Read error %x %x\012\000"
.LC164:
	.ascii	"SDR mode Read %x %x ecc:%x\012\000"
.LC165:
	.ascii	"flash_read_page_en %x %x %x %x\012\000"
.LC166:
	.ascii	"flash_read_page_en %x %x error_ecc %d %d\012\000"
.LC167:
	.ascii	"flash_get_last_written_page: %x %x %x\012\000"
.LC168:
	.ascii	"flash_prog_page page_addr = %x status = %x\012\000"
.LC169:
	.ascii	"flash_prog_page %x %x %x\012\000"
.LC170:
	.ascii	"ymtc_flash_tlc_page_prog page_addr = %x status = %x"
	.ascii	"\012\000"
.LC171:
	.ascii	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\012"
	.ascii	"\000"
.LC172:
	.ascii	"flash_complete_page_read %x %x error_ecc %d %d\012\000"
.LC173:
	.ascii	"read: %x %x %x %x\012\000"
.LC174:
	.ascii	"0set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC175:
	.ascii	"prog end %x %x error_ecc %d %d\012\000"
.LC176:
	.ascii	"1set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC177:
	.ascii	"dp prog end %x %x error_ecc %d %d\012\000"
.LC178:
	.ascii	"sblk_prog_page ppa = %x, count = %d\012\000"
.LC179:
	.ascii	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %"
	.ascii	"x\012\000"
.LC180:
	.ascii	"flash_prog_page_en:%x %x %x\012\000"
.LC181:
	.ascii	"w d:\000"
.LC182:
	.ascii	"w s:\000"
.LC183:
	.ascii	"spare\000"
.LC184:
	.ascii	"data\000"
.LC185:
	.ascii	"write error: %x\012\000"
.LC186:
	.ascii	"g_ftl_info_blk blk = %x, page = %x version = %d\012"
	.ascii	"\000"
.LC187:
	.ascii	"%d %x @%d %x\012\000"
.LC188:
	.ascii	"ftl_info_blk_init %d %d %x\012\000"
.LC189:
	.ascii	"ftl info hash %x error\012\000"
.LC190:
	.ascii	"ink flag: %x\012\000"
.LC191:
	.ascii	"%s %d %d %x %x\012\000"
.LC192:
	.ascii	"ext info hash %x error\012\000"
.LC193:
	.ascii	"%s %x %x %x\012\000"
.LC194:
	.ascii	"ftl_sblk_dump_write = %x %d %d %d %d\012\000"
.LC195:
	.ascii	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x"
	.ascii	" %x %x, spare: %x %x %x %x\012\000"
.LC196:
	.ascii	"ftl_sblk_dump_write2 = %x %d %d %d\012\000"
.LC197:
	.ascii	"ftl_sblk_dump_write = %x %x\012\000"
.LC198:
	.ascii	"ftl_sblk_dump_write done = %x\012\000"
.LC199:
	.ascii	"%x: ink_scaned_blk_num %x\012\000"
.LC200:
	.ascii	"ftl_ink_check_sblk = %x %d %d\012\000"
.LC201:
	.ascii	"ftl_ink_check_sblk = %x %d %d end\012\000"
.LC202:
	.ascii	"alloc sblk %x %d\012\000"
.LC203:
	.ascii	"blk %x is bad block\012\000"
.LC204:
	.ascii	"pm_alloc_new_blk: %x %x %x %x\012\000"
.LC205:
	.ascii	"pm_write_page write error: %x\012\000"
.LC206:
	.ascii	"finfo:\000"
.LC207:
	.ascii	"flash_info_flush id = %x, page = %x\012\000"
.LC208:
	.ascii	"sys_info_flush error:%x\012\000"
.LC209:
	.ascii	"...%d @ %s %d %p\012\000"
.LC210:
	.ascii	"no sys info %x\012\000"
.LC211:
	.ascii	"l2p:\000"
.LC212:
	.ascii	"saved_active_page  = %x\012\000"
.LC213:
	.ascii	"saved_active_plane = %x\012\000"
.LC214:
	.ascii	"sblk = %x\012\000"
.LC215:
	.ascii	"phy_blk = %x %x\012\000"
.LC216:
	.ascii	"num_planes = %x\012\000"
.LC217:
	.ascii	"recovery blk=%x, page=%x, ppa = %x, status = %x, ha"
	.ascii	"sh:%x\012\000"
.LC218:
	.ascii	"data:\000"
.LC219:
	.ascii	"sblk = %x, vpn0 = %d, vpn1 = %d\012\000"
.LC220:
	.ascii	"dump_write_lpa = %x %x %x %x\012\000"
.LC221:
	.ascii	"dump write new ppa = %x, last ppa = %x lpa = %x\012"
	.ascii	"\000"
.LC222:
	.ascii	"dump write = %x %x %x\012\000"
.LC223:
	.ascii	"dump write hash update = %x %x %x\012\000"
.LC224:
	.ascii	"free_buf_count: %d\012\000"
.LC225:
	.ascii	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\012\000"
.LC226:
	.ascii	"ftl_ext_info_blk blk:0x%x, page:0x%x\012\000"
.LC227:
	.ascii	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC228:
	.ascii	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, pag"
	.ascii	"e_index:0x%x\012\000"
.LC229:
	.ascii	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC230:
	.ascii	"lpa:\000"
.LC231:
	.ascii	"vpn:\000"
.LC232:
	.ascii	"sblk:\000"
.LC233:
	.ascii	"lpa_hash:\000"
.LC234:
	.ascii	"lpa_hash_index:\000"
.LC235:
	.ascii	"%s w error lpn = %x, max ppa = %d\012\000"
.LC236:
	.ascii	"region_id = %d, pm_max_region = %d\012\000"
.LC237:
	.ascii	"load_l2p_region no ppa = %x , %x, all setting 0xff."
	.ascii	"...\012\000"
.LC238:
	.ascii	"load_l2p_region = %x,%x,%x, %x\012\000"
.LC239:
	.ascii	"pm_ppa:\000"
.LC240:
	.ascii	"spare:\000"
.LC241:
	.ascii	"pm_init posr %x %x %x\012\000"
.LC242:
	.ascii	"pm_init recovery %x %x %x\012\000"
.LC243:
	.ascii	"pm_init hash %x error\012\000"
.LC244:
	.ascii	"pm_log2phys  lpn = %d, max lpn = %d\012\000"
.LC245:
	.ascii	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x "
	.ascii	"%x %x %x\012\000"
.LC246:
	.ascii	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\012"
	.ascii	"\000"
.LC247:
	.ascii	"gc_recovery: %x vpn = %x\012\000"
.LC248:
	.ascii	"gc_update_l2p_map_new sblk %x\012\000"
.LC249:
	.ascii	"gc_update_l2p_map_new: %x %x %x\012\000"
.LC250:
	.ascii	"lpa: %x %x %x\012\000"
.LC251:
	.ascii	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\012"
	.ascii	"\000"
.LC252:
	.ascii	"gc_scan_src_blk = %x, vpn = %d\012\000"
.LC253:
	.ascii	"js hash error:%x %x %x\012\000"
.LC254:
	.ascii	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\012\000"
.LC255:
	.ascii	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:"
	.ascii	"%d\012\000"
.LC256:
	.ascii	"ftl_sblk_dump = %x %d %d %d %d\012\000"
.LC257:
	.ascii	"ftl_sblk_dump = %x %x %x %x\012\000"
.LC258:
	.ascii	"page_addr = %x, lpa=%x vpn = %d\012\000"
.LC259:
	.ascii	"index= %x, lpa=%x\012\000"
.LC260:
	.ascii	"block = %x, vpn=%x check vpn = %x\012\000"
.LC261:
	.ascii	"ftl_read %x %x %x\012\000"
.LC262:
	.ascii	"ftl_read refresh =%x, lpa = %x, ppa= %x\012\000"
.LC263:
	.ascii	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %"
	.ascii	"x %x %x\012\000"
.LC264:
	.ascii	"zftl debug cmd: %s\012\000"
.LC265:
	.ascii	"cmd:\000"
.LC266:
	.ascii	"dumpl2p\000"
.LC267:
	.ascii	"pm l2p:\000"
.LC268:
	.ascii	"pm blk:\000"
.LC269:
	.ascii	"dumppm:\000"
.LC270:
	.ascii	"p_cmd: %s\012\000"
.LC271:
	.ascii	"pm ram = %x, %x\012\000"
.LC272:
	.ascii	"ram:\000"
.LC273:
	.ascii	"pm:\000"
.LC274:
	.ascii	"dumpsys\000"
.LC275:
	.ascii	"dumplist:\000"
.LC276:
	.ascii	"vpncheck\000"
.LC277:
	.ascii	"dumpppa:\000"
.LC278:
	.ascii	"dumpblk:\000"
.LC279:
	.ascii	"setzdebug:\000"
.LC280:
	.ascii	"lpa2ppa:\000"
.LC281:
	.ascii	"lpa: %x--> ppa: %x\012\000"
.LC282:
	.ascii	"help:\012\000"
.LC283:
	.ascii	"1. echo dumpl2p > /proc/zftl_debug\012\000"
.LC284:
	.ascii	"2. echo dumppm:x > /proc/zftl_debug\012\000"
.LC285:
	.ascii	"3. echo dumpsys > /proc/zftl_debug\012\000"
.LC286:
	.ascii	"4. echo dumpppa:x > /proc/zftl_debug\012\000"
.LC287:
	.ascii	"5. echo vpncheck > /proc/zftl_debug\012\000"
.LC288:
	.ascii	"6. echo setzdebug:x > /proc/zftl_debug\012\000"
.LC289:
	.ascii	"7. echo dumplist:x > /proc/zftl_debug\012\000"
.LC290:
	.ascii	"8. echo lpa2ppa:x> /proc/zftl_debug\012\000"
.LC291:
	.ascii	"ftl_update_l2p_map: %x %x %x\012\000"
.LC292:
	.ascii	"ftl_update_l2p_map\000"
.LC293:
	.ascii	"lpa_tbl:\000"
.LC294:
	.ascii	"sblk %x vpn: %d %d\012\000"
.LC295:
	.ascii	"error gc_add_sblk: %x\012\000"
.LC296:
	.ascii	"%d read error: ppa:%x, lpa:%x, status:%x\012\000"
.LC297:
	.ascii	"gc page in buf: lpa %x ppa = %x pageindex= %x\012\000"
.LC298:
	.ascii	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x "
	.ascii	"page_index= %d\012\000"
.LC299:
	.ascii	"gc %d: %d %d %d %d %d %d %d\012\000"
.LC300:
	.ascii	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\012"
	.ascii	"\000"
.LC301:
	.ascii	"gc free %x, %d\012\000"
.LC302:
	.ascii	"_c_user_data_density := %d\012\000"
.LC303:
	.ascii	"_c_totle_phy_density := %d\012\000"
.LC304:
	.ascii	"_c_totle_log_page := %d\012\000"
.LC305:
	.ascii	"_c_totle_data_density := %d\012\000"
.LC306:
	.ascii	"_c_ftl_pm_page_num := %d\012\000"
.LC307:
	.ascii	"_c_ftl_byte_pre_page := %d\012\000"
.LC308:
	.ascii	"_c_max_pm_sblk := %d\012\000"
.LC309:
	.ascii	"_min_slc_super_block := %d\012\000"
.LC310:
	.ascii	"_max_xlc_super_block := %d\012\000"
.LC311:
	.ascii	"gp_ftl_ext_info %p %p %p\012\000"
.LC312:
	.ascii	"flash info size: %d %d %d\012\000"
.LC313:
	.ascii	"ftl_init %x\012\000"
.LC314:
	.ascii	"ftlwrite %x %x %x %x\012\000"
.LC315:
	.ascii	"ftl_discard:(%x, %x, %x, %x)\012\000"
.LC316:
	.ascii	"id_block_prog_msb_ff_data slc page = %d pageadd=%x "
	.ascii	"%x\012\000"
.LC317:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC318:
	.ascii	"idblk:\000"
.LC319:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC320:
	.ascii	"prog page: %x %x %x, %p %x %x %x\012\000"
.LC321:
	.ascii	"read page: %x %x %x %x\012\000"
.LC322:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC323:
	.ascii	"return ret = %lx\012\000"
.LC324:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
