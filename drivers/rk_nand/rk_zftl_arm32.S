/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-03-15
 * function: rk ftl v6 for rockchip soc base on arm v7 to support 3D/2D
 *	     TLC and MLC.
 */
	.arch armv7-a
	.fpu softvfp
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 4
	.file	"rk_zftl_arm_v7.S"
#APP
	.syntax unified
	.global	__aeabi_uidiv
	.text
	.align	2
	.type	slc_phy_page_address_calc, %function
slc_phy_page_address_calc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r6, .L14
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3
.L2:
	ldrh	r5, [r6, #2]
	mov	r0, r4
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L14
	mul	r0, r5, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L3:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L15:
	.align	2
.L14:
	.word	.LANCHOR0
	.fnend
	.size	slc_phy_page_address_calc, .-slc_phy_page_address_calc
	.align	2
	.global	zftl_nandc_get_irq_status
	.type	zftl_nandc_get_irq_status, %function
zftl_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L19
	ldrb	r3, [r3, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	ldreq	r0, [r0, #296]
	ldrne	r0, [r0, #372]
	bx	lr
.L20:
	.align	2
.L19:
	.word	.LANCHOR0
	.fnend
	.size	zftl_nandc_get_irq_status, .-zftl_nandc_get_irq_status
	.align	2
	.global	zftl_get_density
	.type	zftl_get_density, %function
zftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #0
	bne	.L22
	ldr	r3, .L25
	ldr	r0, [r3, #1032]
	bx	lr
.L22:
	cmp	r0, #4
	movcc	r0, #8192
	movcs	r0, #0
	bx	lr
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.fnend
	.size	zftl_get_density, .-zftl_get_density
	.align	2
	.type	_list_remove_node, %function
_list_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #6
	mul	r5, r5, r1
	ldr	r6, .L36
	mov	r7, r0
	movw	r3, #65535
	ldr	r8, [r6, #1036]
	add	r4, r8, r5
	ldrh	r0, [r8, r5]
	ldrh	r1, [r4, #2]
	cmp	r0, r3
	ldr	r3, [r7]
	bne	.L28
	cmp	r1, r0
	bne	.L28
	cmp	r4, r3
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L28:
	mov	r9, r2
	movw	r2, #65535
	cmp	r1, r2
	bne	.L30
	cmp	r4, r3
	beq	.L30
	ldr	r1, .L36+4
	mov	r2, #202
	ldr	r0, .L36+8
	bl	printk
	bl	dump_stack
.L30:
	ldr	r3, [r7]
	movw	r2, #65535
	cmp	r4, r3
	ldrh	r3, [r8, r5]
	bne	.L31
	cmp	r3, r2
	ldrne	r2, [r6, #1036]
	movne	r1, #6
	moveq	r3, #0
	streq	r3, [r7]
	mlane	r3, r1, r3, r2
	mvnne	r2, #0
	strne	r3, [r7]
	strneh	r2, [r3, #2]	@ movhi
	b	.L33
.L31:
	cmp	r3, r2
	ldrh	r1, [r4, #2]
	bne	.L34
	cmp	r1, r3
	movne	r3, #6
	ldrne	r2, [r6, #1036]
	mulne	r1, r3, r1
	mvnne	r3, #0
	strneh	r3, [r2, r1]	@ movhi
	b	.L33
.L34:
	ldr	r0, [r6, #1036]
	mov	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r4, #2]
	ldrh	r1, [r8, r5]
	ldr	r3, [r6, #1036]
	mul	r2, r2, r0
	strh	r1, [r3, r2]	@ movhi
.L33:
	mvn	r3, #0
	strh	r3, [r8, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	ldrh	r3, [r9]
	sub	r3, r3, #1
	strh	r3, [r9]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L37:
	.align	2
.L36:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC0
	.fnend
	.size	_list_remove_node, .-_list_remove_node
	.align	2
	.type	zftl_debug_proc_open, %function
zftl_debug_proc_open:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r1
	bl	PDE_DATA
	ldr	r1, .L40
	mov	r2, r0
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	single_open
.L41:
	.align	2
.L40:
	.word	zftl_debug_proc_show
	.fnend
	.size	zftl_debug_proc_open, .-zftl_debug_proc_open
	.align	2
	.type	zftl_debug_proc_show, %function
zftl_debug_proc_show:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, .L44
	ldr	r2, .L44+4
	bl	seq_printf
	ldr	r3, .L44+8
	mov	r0, r4
	ldr	r1, .L44+12
	ldr	r2, [r3]
	bl	seq_printf
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L45:
	.align	2
.L44:
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR2
	.word	.LC3
	.fnend
	.size	zftl_debug_proc_show, .-zftl_debug_proc_show
	.align	2
	.global	zftl_flash_suspend
	.type	zftl_flash_suspend, %function
zftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L50
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	ldr	r2, [r3, #1040]
	ldr	r1, [r2]
	str	r1, [r3, #1044]
	ldr	r1, [r2, #4]
	str	r1, [r3, #1048]
	bne	.L47
	ldr	r1, [r2, #16]
	str	r1, [r3, #1052]
	ldr	r1, [r2, #32]
	str	r1, [r3, #1056]
	ldr	r1, [r2, #80]
	str	r1, [r3, #1060]
	ldr	r1, [r2, #84]
	str	r1, [r3, #1064]
	ldr	r1, [r2, #520]
	ldr	r2, [r2, #8]
	str	r1, [r3, #1068]
	b	.L49
.L47:
	ldr	r1, [r2, #8]
	str	r1, [r3, #1052]
	ldr	r1, [r2, #12]
	str	r1, [r3, #1056]
	ldr	r1, [r2, #304]
	str	r1, [r3, #1060]
	ldr	r1, [r2, #308]
	str	r1, [r3, #1064]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #1068]
.L49:
	str	r2, [r3, #1072]
	bx	lr
.L51:
	.align	2
.L50:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L62
	movw	ip, #1076
	ldrh	ip, [r3, ip]
	cmp	ip, r1
	bxls	lr
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r3
	ldrh	ip, [r2]
	add	ip, ip, #1
	strh	ip, [r2]	@ movhi
	mov	ip, #6
	ldr	r7, [r3, #1036]
	mul	r6, ip, r1
	mvn	r3, #0
	add	lr, r7, r6
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L61
	ldr	r5, [r4, #1080]
	movw	r8, #1084
	ldrh	r10, [r4, r8]
	mov	r2, r1, asl #2
	ldr	r9, [r4, #1036]
	movw	fp, #65535
	ldr	r8, [r5, r1, asl #2]
	ldrh	r2, [r5, r2]
	ubfx	r8, r8, #11, #8
	str	ip, [sp, #4]
	ubfx	r2, r2, #0, #11
	smulbb	r8, r8, r10
	add	r2, r8, r2
	rsb	r8, r9, r3
	uxth	r2, r2
	str	r2, [sp]
	ldr	r2, .L62+4
	mov	r8, r8, asr #1
	mul	r2, r2, r8
	uxth	r2, r2
.L57:
	ldr	ip, [r5, r2, asl #2]
	mov	r8, r2, asl #2
	ubfx	ip, ip, #11, #8
	ldrh	r8, [r5, r8]
	smulbb	ip, ip, r10
	ubfx	r8, r8, #0, #11
	add	r8, ip, r8
	ldr	ip, [sp]
	uxth	r8, r8
	cmp	r8, ip
	bcs	.L55
	ldrh	ip, [r3]
	cmp	ip, fp
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L52
.L56:
	ldr	r3, [sp, #4]
	mov	r2, ip
	mla	r3, r3, ip, r9
	b	.L57
.L55:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L58
	strh	r1, [r3, #2]	@ movhi
.L61:
	str	lr, [r0]
	b	.L52
.L58:
	ldrh	ip, [r3, #2]
	mov	r2, #6
	ldr	r0, [r4, #1036]
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L52:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L63:
	.align	2
.L62:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_insert_free_list, .-_insert_free_list
	.align	2
	.type	_insert_data_list, %function
_insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L85
	movw	r3, #1076
	ldrh	r3, [ip, r3]
	cmp	r3, r1
	bxls	lr
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #6
	ldrh	r3, [r2]
	.pad #28
	sub	sp, sp, #28
	mul	r5, r5, r1
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	mvn	r3, #0
	ldr	r10, [ip, #1036]
	add	lr, r10, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r10, r5]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L84
	ldr	r2, [ip, #1088]
	movw	r8, #1084
	ldr	r9, [ip, #1080]
	ldrh	ip, [ip, r8]
	str	r2, [sp, #4]
	mov	r2, r1, asl #1
	ldr	r6, [sp, #4]
	ldrh	r4, [lr, #4]
	str	ip, [sp, #8]
	ldrh	r7, [r6, r2]
	muls	ip, r4, r7
	ldr	r6, [r9, r1, asl #2]
	mov	r2, r1, asl #2
	ldrneh	r7, [sp, #8]
	ubfx	r6, r6, #11, #8
	ldrh	r2, [r9, r2]
	smulbbne	r6, r6, r7
	ubfx	r2, r2, #0, #11
	addne	r2, r2, r6
	uxtahne	ip, ip, r2
	cmp	r4, #0
	ldr	r4, .L85
	mvneq	ip, #0
	ldr	r2, [r4, #1036]
	rsb	r6, r2, r3
	str	r2, [sp, #12]
	ldr	r2, .L85+4
	mov	r6, r6, asr #1
	mul	r2, r2, r6
	movw	r6, #1076
	ldrh	r4, [r4, r6]
	mov	r6, #0
	str	r4, [sp, #16]
	uxth	r2, r2
.L74:
	add	r6, r6, #1
	ldr	r4, [sp, #16]
	uxth	r6, r6
	cmp	r6, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r1, r2
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L64
	ldr	r7, [sp, #4]
	mov	r4, r2, asl #1
	ldr	r8, [r9, r2, asl #2]
	ldrh	fp, [r3, #4]
	ldrh	r4, [r7, r4]
	mov	r7, r2, asl #2
	muls	r4, fp, r4
	ubfx	r8, r8, #11, #8
	ldrh	r7, [r9, r7]
	ubfx	r7, r7, #0, #11
	str	r7, [sp, #20]
	ldrneh	r7, [sp, #8]
	smulbbne	r8, r8, r7
	ldrne	r7, [sp, #20]
	addne	r8, r7, r8
	uxtahne	r4, r4, r8
	cmp	fp, #0
	cmpne	r4, ip
	bcs	.L72
	ldrh	r4, [r3]
	movw	r7, #65535
	cmp	r4, r7
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L64
.L73:
	ldr	r2, [sp, #12]
	mov	r3, #6
	mla	r3, r3, r4, r2
	mov	r2, r4
	b	.L74
.L72:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r10, r5]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L75
	strh	r1, [r3, #2]	@ movhi
.L84:
	str	lr, [r0]
	b	.L64
.L75:
	ldr	r2, .L85
	ldrh	ip, [r3, #2]
	ldr	r0, [r2, #1036]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L64:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L86:
	.align	2
.L85:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_update_data_list, %function
_list_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r1
	ldr	r1, .L98
	mov	r6, r0
	ldr	r3, [r1, #1092]
	ldrh	r0, [r3, #16]
	cmp	r0, r4
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r5, r1
	ldrh	r1, [r3, #48]
	cmp	r1, r4
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r3, [r3, #80]
	cmp	r3, r4
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r8, #6
	ldr	r10, [r5, #1036]
	mul	r8, r8, r4
	ldr	r3, [r6]
	add	r9, r10, r8
	cmp	r9, r3
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r3, [r9, #2]
	mov	r7, r2
	movw	r2, #65535
	cmp	r3, r2
	bne	.L91
	ldrh	r2, [r10, r8]
	cmp	r2, r3
	bne	.L91
	ldr	r1, .L98+4
	movw	r2, #273
	ldr	r0, .L98+8
	bl	printk
	bl	dump_stack
.L91:
	ldrh	r2, [r9, #2]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L92
	ldrh	r3, [r10, r8]
	cmp	r3, r2
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L92:
	ldr	r0, [r5, #1088]
	mov	r3, r4, asl #1
	ldrh	r1, [r0, r3]
	ldrh	r3, [r9, #4]
	cmp	r3, #0
	mulne	r1, r3, r1
	mov	r3, #6
	mul	r2, r3, r2
	ldr	r3, .L98+12
	mvneq	r1, #0
	mov	ip, r2, asr #1
	mul	r3, r3, ip
	mov	r3, r3, asl #1
	ldrh	r0, [r0, r3]
	ldr	r3, [r5, #1036]
	add	r2, r3, r2
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	mulne	r3, r3, r0
	mvneq	r3, #0
	cmp	r1, r3
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r0, r6
	mov	r1, r4
	mov	r2, r7
	bl	_list_remove_node
	mov	r0, r6
	mov	r1, r4
	mov	r2, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	_insert_data_list
.L99:
	.align	2
.L98:
	.word	.LANCHOR0
	.word	.LANCHOR1+20
	.word	.LC0
	.word	-1431655765
	.fnend
	.size	_list_update_data_list, .-_list_update_data_list
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	rk_simple_strtoull.constprop.34, %function
rk_simple_strtoull.constprop.34:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #48
	movne	r2, r0
	movne	lr, #10
	bne	.L101
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	add	r2, r0, #1
	orr	r3, r3, #32
	cmp	r3, #120
	bne	.L112
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	bic	ip, r3, #32
	sub	ip, ip, #65
	cmp	ip, #25
	bls	.L102
	sub	r3, r3, #48
	cmp	r3, #9
	bls	.L102
.L112:
	mov	lr, #8
.L101:
	mov	r0, #0
.L103:
	mov	r4, r2
	ldrb	ip, [r2], #1	@ zero_extendqisi2
	bic	r3, ip, #32
	sub	r3, r3, #65
	cmp	r3, #25
	sub	r3, ip, #48
	bls	.L104
	cmp	r3, #9
	bls	.L104
.L108:
	cmp	r1, #0
	bne	.L105
	ldmfd	sp!, {r4, pc}
.L105:
	str	r4, [r1]
	ldmfd	sp!, {r4, pc}
.L102:
	add	r2, r0, #2
	mov	lr, #16
	b	.L101
.L104:
	cmp	r3, #9
	orrhi	ip, ip, #32
	subhi	r3, ip, #87
	cmp	r3, lr
	bcs	.L108
	mla	r0, lr, r0, r3
	b	.L103
	.fnend
	.size	rk_simple_strtoull.constprop.34, .-rk_simple_strtoull.constprop.34
	.text
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L234
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L116
	ldr	r0, .L234+4
	ldr	r1, .L234+8
	bl	printk
.L116:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L117
	ldr	r3, .L234+12
	ldr	r0, [r3, #1096]
	ldrb	ip, [r0, #4]	@ zero_extendqisi2
	ldrb	r1, [r0, #1]	@ zero_extendqisi2
	ldrb	r2, [r0, #2]	@ zero_extendqisi2
	ldrb	r3, [r0, #3]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #5]	@ zero_extendqisi2
	str	ip, [sp, #4]
	ldrb	r0, [r0, #6]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, .L234+16
	bl	printk
.L117:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L118
	ldr	r3, .L234+12
	ldr	r0, .L234+20
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #8]	@ zero_extendqisi2
	bl	printk
.L118:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L119
	ldr	r3, .L234+12
	ldr	r0, .L234+24
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	printk
.L119:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L120
	ldr	r3, .L234+12
	ldr	r0, .L234+28
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #10]
	bl	printk
.L120:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L121
	ldr	r3, .L234+12
	ldr	r0, .L234+32
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	printk
.L121:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L122
	ldr	r3, .L234+12
	ldr	r0, .L234+36
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	bl	printk
.L122:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L123
	ldr	r3, .L234+12
	ldr	r0, .L234+40
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #14]
	bl	printk
.L123:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L124
	ldr	r3, .L234+12
	ldr	r0, .L234+44
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #23]	@ zero_extendqisi2
	bl	printk
.L124:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L125
	ldr	r3, .L234+12
	ldr	r0, .L234+48
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #18]	@ zero_extendqisi2
	bl	printk
.L125:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L126
	ldr	r3, .L234+12
	ldr	r0, .L234+52
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	bl	printk
.L126:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L127
	ldr	r3, .L234+12
	ldr	r0, .L234+56
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	bl	printk
.L127:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L128
	ldr	r3, .L234+12
	ldr	r0, .L234+60
	ldrb	r1, [r3, #1100]	@ zero_extendqisi2
	bl	printk
.L128:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L129
	ldr	r3, .L234+12
	ldr	r0, .L234+64
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #22]	@ zero_extendqisi2
	bl	printk
.L129:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L130
	ldr	r3, .L234+12
	ldr	r0, .L234+68
	ldrb	r1, [r3, #1101]	@ zero_extendqisi2
	bl	printk
.L130:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L131
	ldr	r3, .L234+12
	ldr	r0, .L234+72
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	and	r1, r1, #1
	bl	printk
.L131:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L132
	ldr	r3, .L234+12
	ldr	r0, .L234+76
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #1, #1
	bl	printk
.L132:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L133
	ldr	r3, .L234+12
	ldr	r0, .L234+80
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #2, #1
	bl	printk
.L133:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L134
	ldr	r3, .L234+12
	ldr	r0, .L234+84
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #3, #1
	bl	printk
.L134:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L135
	ldr	r3, .L234+12
	ldr	r0, .L234+88
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #4, #1
	bl	printk
.L135:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L136
	ldr	r3, .L234+12
	ldr	r0, .L234+92
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #5, #1
	bl	printk
.L136:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L137
	ldr	r3, .L234+12
	ldr	r0, .L234+96
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #6, #1
	bl	printk
.L137:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L138
	ldr	r3, .L234+12
	ldr	r0, .L234+100
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #7, #1
	bl	printk
.L138:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L139
	ldr	r3, .L234+12
	ldr	r0, .L234+104
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #8, #1
	bl	printk
.L139:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L140
	ldr	r3, .L234+12
	ldr	r0, .L234+108
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #9, #1
	bl	printk
.L140:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L141
	ldr	r3, .L234+12
	ldr	r0, .L234+112
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #10, #1
	bl	printk
.L141:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L142
	ldr	r3, .L234+12
	ldr	r0, .L234+116
	ldrb	r1, [r3]	@ zero_extendqisi2
	ldrb	r2, [r3, #1102]	@ zero_extendqisi2
	bl	printk
.L142:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L143
	ldr	r3, .L234+12
	ldr	r0, .L234+120
	ldrb	r1, [r3, #1113]	@ zero_extendqisi2
	ldrb	r2, [r3, #1114]	@ zero_extendqisi2
	bl	printk
.L143:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L144
	ldr	r3, .L234+12
	ldr	r0, .L234+124
	ldrb	r1, [r3, #1111]	@ zero_extendqisi2
	ldrb	r2, [r3, #1112]	@ zero_extendqisi2
	bl	printk
.L144:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L115
	ldr	r3, .L234+12
	ldr	r0, .L234+128
	ldrb	r1, [r3, #1135]	@ zero_extendqisi2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, lr}
	b	printk
.L115:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L235:
	.align	2
.L234:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+44
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
	.global	nandc_init
	.type	nandc_init, %function
nandc_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L256
	mov	r3, #0
	str	r3, [sp, #12]
	mov	r7, r0
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L237
	ldr	r0, .L256+4
	mov	r2, r7
	ldr	r1, .L256+8
	bl	printk
.L237:
	ldr	r4, .L256+12
	mov	r3, #6
	ldr	r2, [r7, #352]
	ldr	r6, .L256+12
	strb	r3, [r4, #1028]
	ldr	r3, .L256+16
	str	r7, [r4, #1040]
	cmp	r2, r3
	ldr	r2, [r7, #128]
	moveq	r3, #8
	streqb	r3, [r4, #1028]
	ldr	r3, .L256+20
	cmp	r2, r3
	ldr	r2, .L256+24
	moveq	r3, #9
	streqb	r3, [r4, #1028]
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L240
	mov	r3, #1
	strb	r3, [r6, #1136]
	ldr	r3, [sp, #12]
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #1835008
	orr	r3, r3, #524288
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r7]
	mov	r3, #0
	ldr	r0, [r6, #1040]
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
	beq	.L242
	ldr	r1, [r0]
	ldr	r2, [r0, #8]
	ldr	r3, [r0, #80]
	ldr	ip, [r0, #84]
	ldr	r0, [r0, #88]
	b	.L255
.L240:
	ldr	r3, [sp, #12]
	mov	r1, #0
	strb	r1, [r6, #1136]
	mov	r0, #2048
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #117440512
	orr	r3, r3, #16777216
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r7]
	ldr	r3, [r6, #1040]
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
	bl	ftl_malloc
	ldr	r3, [r5]
	tst	r3, #4096
	str	r0, [r6, #1140]
	beq	.L242
	ldr	r0, [r6, #1040]
	ldr	r1, [r0]
	ldr	r2, [r0, #344]
	ldr	r3, [r0, #304]
	ldr	ip, [r0, #308]
	ldr	r0, [r0, #312]
.L255:
	str	r0, [sp, #4]
	str	ip, [sp]
	ldr	r0, .L256+28
	bl	printk
.L242:
	movw	r2, #1170
	mov	r3, #1
	strb	r3, [r4, #1168]
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #1172]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L236
	ldr	r3, .L256+12
	ldr	r0, .L256+32
	ldrb	r1, [r3, #1028]	@ zero_extendqisi2
	bl	printk
.L236:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L257:
	.align	2
.L256:
	.word	.LANCHOR2
	.word	.LC34
	.word	.LANCHOR1+68
	.word	.LANCHOR0
	.word	1446522928
	.word	1446588464
	.word	1052675
	.word	.LC35
	.word	.LC36
	.fnend
	.size	nandc_init, .-nandc_init
	.align	2
	.global	timer_delay_ns
	.type	timer_delay_ns, %function
timer_delay_ns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L259
	add	r0, r0, #996
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L259+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	bx	r3	@ indirect register sibling call
.L260:
	.align	2
.L259:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	timer_delay_ns, .-timer_delay_ns
	.align	2
	.global	flash_read_status
	.type	flash_read_status, %function
flash_read_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r3, #112
	mov	r4, r0
	str	r3, [r0, #8]
	mov	r0, #120
	bl	timer_delay_ns
	ldr	r0, [r4]
	uxtb	r0, r0
	ldmfd	sp!, {r4, pc}
	.fnend
	.size	flash_read_status, .-flash_read_status
	.align	2
	.global	toshiba_set_rr_para
	.type	toshiba_set_rr_para, %function
toshiba_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r8, r1, r1, asl #2
	ldr	r9, .L273
	mov	r5, r0
	ldr	r7, .L273+4
	mov	r6, r1
	add	r10, r9, #80
	mov	r4, #0
.L264:
	ldrb	r3, [r7, #1174]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L272
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	timer_delay_ns
	ldrb	r3, [r7, #1173]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L271
	cmp	r3, #35
	addne	r3, r9, r6
	ldrnesb	r3, [r3, #224]
	bne	.L270
	ldr	r3, .L273+8
	add	r2, r4, r8
	add	r3, r3, r2
.L271:
	ldrsb	r3, [r3, #5]
.L270:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L264
.L272:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L274:
	.align	2
.L273:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR1+128
	.fnend
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
	.align	2
	.type	hynix_set_rr_para, %function
hynix_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, r0, asl #8
	ldr	r3, .L282
	ldr	r4, [r3, #1176]
	ldr	r7, [r3, #1040]
	add	lr, r4, #128
	add	r9, r4, #127
	mov	r3, r4
	ldrb	ip, [r4, #113]	@ zero_extendqisi2
	ldrb	r2, [r3, #112]!	@ zero_extendqisi2
	add	r6, r7, r5
	cmp	r2, #8
	mul	r1, r1, ip
	movne	r2, #160
	mlane	r1, r2, r0, r1
	sub	ip, ip, #1
	add	r8, lr, ip
	mov	r2, #54
	str	r2, [r6, #2056]
	add	r1, r1, #32
	add	r3, r3, r1
	sub	r4, r3, #1
.L278:
	cmp	r9, r8
	beq	.L281
	ldrb	r3, [r9, #1]!	@ zero_extendqisi2
	mov	r0, #120
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	ldrsb	r3, [r4, #1]!
	str	r3, [r6, #2048]
	b	.L278
.L281:
	add	r5, r7, r5
	mov	r3, #22
	str	r3, [r5, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L283:
	.align	2
.L282:
	.word	.LANCHOR0
	.fnend
	.size	hynix_set_rr_para, .-hynix_set_rr_para
	.align	2
	.global	hynix_reconfig_rr_para
	.type	hynix_reconfig_rr_para, %function
hynix_reconfig_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, .L290
	ldrb	r2, [r5, #1173]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	ldmhifd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r5, #1176]
	add	r2, r2, r0
	ldrb	r2, [r2, #120]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r1, #0
	mov	r4, r0
	bl	hynix_set_rr_para
	ldr	r2, [r5, #1176]
	mov	r3, #0
	add	r4, r2, r4
	strb	r3, [r4, #120]
	ldmfd	sp!, {r3, r4, r5, pc}
.L291:
	.align	2
.L290:
	.word	.LANCHOR0
	.fnend
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
	.align	2
	.global	nandc_set_ddr_para
	.type	nandc_set_ddr_para, %function
nandc_set_ddr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L295
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1040]
	cmp	r2, #9
	mov	r2, r0, asl #16
	mov	r0, r0, asl #8
	orr	r0, r2, r0
	orr	r0, r0, #3
	streq	r0, [r3, #80]
	strne	r0, [r3, #304]
	bx	lr
.L296:
	.align	2
.L295:
	.word	.LANCHOR0
	.fnend
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L301
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1040]
	cmp	r2, #9
	ldreq	r0, [r3, #80]
	ldrne	r0, [r3, #304]
	ubfx	r0, r0, #8, #8
	bx	lr
.L302:
	.align	2
.L301:
	.word	.LANCHOR0
	.fnend
	.size	nandc_get_ddr_para, .-nandc_get_ddr_para
	.align	2
	.global	nandc_set_if_mode
	.type	nandc_set_if_mode, %function
nandc_set_if_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L311
	ands	ip, r0, #6
	ldr	r3, [r1, #1040]
	ldr	r2, [r3]
	bfieq	r2, ip, #13, #1
	beq	.L307
	orr	r2, r2, #24576
	ldrb	r1, [r1, #1028]	@ zero_extendqisi2
	bfc	r2, #15, #1
	tst	r0, #4
	orr	r2, r2, #196608
	movw	r0, #8321
	orrne	r2, r2, #32768
	cmp	r1, #9
	ldr	r1, .L311+4
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
.L307:
	str	r2, [r3]
	bx	lr
.L312:
	.align	2
.L311:
	.word	.LANCHOR0
	.word	1052675
	.fnend
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L314
	mov	r2, #1
	mov	r0, r2, asl r0
	ldr	r1, [r3, #1040]
	ldr	r3, [r1]
	bfi	r3, r0, #0, #8
	str	r3, [r1]
	bx	lr
.L315:
	.align	2
.L314:
	.word	.LANCHOR0
	.fnend
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L317
	ldr	r2, [r3, #1040]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L318:
	.align	2
.L317:
	.word	.LANCHOR0
	.fnend
	.size	nandc_de_cs, .-nandc_de_cs
	.align	2
	.global	flash_wait_device_ready_raw
	.type	flash_wait_device_ready_raw, %function
flash_wait_device_ready_raw:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r0
	ldr	r4, .L328
	mov	r5, r1
	mov	r7, r2
	ldrb	r3, [r4, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L320
	ldr	r1, .L328+4
	mov	r2, #812
	ldr	r0, .L328+8
	bl	printk
	bl	dump_stack
.L320:
	add	r6, r4, r6
	ldr	r3, [r4, #1040]
	mov	r9, r5, lsr #16
	mov	r10, r5, lsr #24
	ldrb	r6, [r6, #1180]	@ zero_extendqisi2
	mov	fp, #120
	mov	r2, r5, lsr #8
	str	r2, [sp]
	add	r8, r3, r6, asl #8
.L322:
	mov	r0, r6
	bl	nandc_cs
	str	fp, [r8, #2056]
	uxtb	r2, r5
	str	r2, [r8, #2052]
	ldrb	r2, [r4, #1188]	@ zero_extendqisi2
	mov	r0, #120
	cmp	r2, #0
	ldr	r3, [sp]
	str	r3, [r8, #2052]
	str	r9, [r8, #2052]
	strne	r10, [r8, #2052]
	bl	timer_delay_ns
	ldr	r2, [r8, #2048]
	mov	r0, r6
	uxtb	r2, r2
	str	r2, [sp, #4]
	bl	nandc_de_cs
	ldr	r2, [sp, #4]
	sub	r1, r2, #255
	and	r0, r7, r2
	clz	r1, r1
	mov	r1, r1, lsr #5
	cmp	r0, r7
	orrne	r1, r1, #1
	cmp	r1, #0
	bne	.L322
	mov	r0, r2
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L329:
	.align	2
.L328:
	.word	.LANCHOR0
	.word	.LANCHOR1+232
	.word	.LC0
	.fnend
	.size	flash_wait_device_ready_raw, .-flash_wait_device_ready_raw
	.align	2
	.global	flash_wait_device_ready
	.type	flash_wait_device_ready, %function
flash_wait_device_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #1
	ldr	r6, .L342
	tst	r0, #50331648
	mov	r8, r1
	ldrb	r2, [r6, #1189]	@ zero_extendqisi2
	rsb	r5, r2, #24
	mov	r4, r3, asl r5
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	sub	r4, r4, #1
	and	r5, r3, r0, asr r5
	and	r4, r4, r0
	uxtb	r5, r5
	bne	.L331
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L332
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L331
.L332:
	ldrh	r7, [r6, #2]
	mov	r0, r4
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L342
	mul	r0, r7, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L331:
	mov	r0, r5
	mov	r1, r4
	mov	r2, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready_raw
.L343:
	.align	2
.L342:
	.word	.LANCHOR0
	.fnend
	.size	flash_wait_device_ready, .-flash_wait_device_ready
	.align	2
	.global	nandc_wait_flash_ready_no_delay
	.type	nandc_wait_flash_ready_no_delay, %function
nandc_wait_flash_ready_no_delay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L350
	ldr	r5, .L350+4
.L346:
	ldr	r3, [r5, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L347
	mov	r0, #10
	bl	timer_delay_ns
	subs	r4, r4, #1
	bne	.L346
	mvn	r0, #0
	b	.L345
.L347:
	mov	r0, #0
.L345:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L351:
	.align	2
.L350:
	.word	100000
	.word	.LANCHOR0
	.fnend
	.size	nandc_wait_flash_ready_no_delay, .-nandc_wait_flash_ready_no_delay
	.align	2
	.global	zftl_flash_enter_slc_mode
	.type	zftl_flash_enter_slc_mode, %function
zftl_flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r3, .L371
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldr	r5, [r3, #1040]
	bne	.L355
	ldr	r3, .L371+4
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r5, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L355:
	cmp	r2, #2
	mov	r4, r0
	bne	.L357
	add	r3, r3, r0
	ldrb	r2, [r3, #1192]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r6, .L371+4
	mov	r2, #0
	strb	r2, [r3, #1192]
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	cmp	r3, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	add	r4, r5, r4, asl #8
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L357:
	cmp	r2, #3
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	add	r6, r3, r0
	ldrb	r3, [r6, #1192]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	add	r4, r5, r4, asl #8
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #239
	mov	r0, #100
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	mov	r7, #0
	strb	r7, [r6, #1192]
	bl	timer_delay_ns
	str	r7, [r4, #2048]
	mov	r3, #1
	mov	r0, #150
	str	r3, [r4, #2048]
	str	r7, [r4, #2048]
	str	r7, [r4, #2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #218
	mov	r0, #50
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	timer_delay_ns
.L372:
	.align	2
.L371:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L394
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r4, [r3]	@ zero_extendqisi2
	cmp	r4, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	cmp	r4, #1
	ldr	r7, [r3, #1040]
	bne	.L376
	ldr	r3, .L394+4
	ldrb	r3, [r3, #34]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r7, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L376:
	cmp	r4, #2
	mov	r5, r0
	bne	.L378
	add	r3, r3, r0
	ldrb	r2, [r3, #1192]	@ zero_extendqisi2
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r4, .L394+4
	ldrb	r2, [r4, #16]	@ zero_extendqisi2
	cmp	r2, #2
	movne	r2, #4
	strb	r2, [r3, #1192]
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	add	r5, r7, r5, asl #8
	str	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L378:
	cmp	r4, #3
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	add	r4, r3, r0
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r8, .L394+4
	bl	nandc_wait_flash_ready_no_delay
	mov	r5, r5, asl #8
	mov	r0, #100
	add	r6, r7, r5
	add	r5, r7, r5
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	cmp	r3, #2
	movne	r3, #4
	strb	r3, [r4, #1192]
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #145
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	ldrb	r3, [r8, #11]	@ zero_extendqisi2
	mov	r0, #150
	cmp	r3, #9
	mov	r3, #1
	streq	r3, [r6, #2048]
	ldrneb	r2, [r4, #1192]	@ zero_extendqisi2
	strne	r2, [r6, #2048]
	str	r3, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #223
	mov	r0, #50
	str	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	timer_delay_ns
.L395:
	.align	2
.L394:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mvn	r2, #0
	ldr	r7, .L411
	mov	r5, r0
	mov	r8, r1
	ldrb	r3, [r7, #1189]	@ zero_extendqisi2
	rsb	r6, r3, #24
	mvn	r2, r2, asl r3
	and	r2, r2, r1, lsr r6
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	uxtb	r4, r2
	cmp	r3, r4
	bhi	.L397
	ldr	r1, .L411+4
	movw	r2, #1013
	ldr	r0, .L411+8
	bl	printk
	bl	dump_stack
.L397:
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	ldr	r9, .L411
	cmp	r3, r4
	ldmlsfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	add	r4, r9, r4
	mvn	r3, #0
	bic	r6, r8, r3, asl r6
	ubfx	r8, r8, #24, #2
	ldrb	fp, [r4, #1180]	@ zero_extendqisi2
	ldr	r4, [r9, #1040]
	mov	r0, fp
	mov	r10, fp, asl #8
	bl	nandc_cs
	cmp	r8, #0
	bne	.L399
	mov	r0, r6
	bl	slc_phy_page_address_calc
	ldrb	r3, [r9]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L400
	mov	r0, fp
	bl	zftl_flash_enter_slc_mode
	b	.L400
.L399:
	ldr	r3, [r9, #1096]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L401
	ldrb	r3, [r9, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L401
	ldrb	r3, [r9, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r4, r10
	streq	r8, [r3, #2056]
	beq	.L400
.L401:
	mov	r0, fp
	bl	zftl_flash_exit_slc_mode
.L400:
	ldr	r3, [r7, #1096]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L402
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r4, r10
	moveq	r2, #38
	streq	r2, [r3, #2056]
.L402:
	add	r2, r4, r10
	mov	r3, #0
	str	r3, [r2, #2056]
	str	r3, [r2, #2052]
	str	r3, [r2, #2052]
	uxtb	r3, r6
	str	r3, [r2, #2052]
	mov	r3, r6, lsr #8
	str	r3, [r2, #2052]
	mov	r3, r6, lsr #16
	str	r3, [r2, #2052]
	ldrb	r3, [r7, #1188]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, r6, lsr #24
	strne	r3, [r2, #2052]
	add	r3, r4, r10
	str	r5, [r3, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L412:
	.align	2
.L411:
	.word	.LANCHOR0
	.word	.LANCHOR1+260
	.word	.LC0
	.fnend
	.size	flash_start_page_read, .-flash_start_page_read
	.align	2
	.global	nandc_wait_flash_ready
	.type	nandc_wait_flash_ready, %function
nandc_wait_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	mov	r0, #150
	bl	timer_delay_ns
	ldr	r4, .L419
	ldr	r5, .L419+4
.L415:
	ldr	r3, [r5, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L416
	mov	r0, #10
	bl	timer_delay_ns
	subs	r4, r4, #1
	bne	.L415
	mvn	r0, #0
	b	.L414
.L416:
	mov	r0, #0
.L414:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L420:
	.align	2
.L419:
	.word	100000
	.word	.LANCHOR0
	.fnend
	.size	nandc_wait_flash_ready, .-nandc_wait_flash_ready
	.align	2
	.global	sandisk_set_rr_para
	.type	sandisk_set_rr_para, %function
sandisk_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r3, #239
	mov	r5, r0
	str	r3, [r0, #8]
	mov	r3, #17
	str	r3, [r0, #4]
	mov	r0, #200
	mov	r4, r1
	bl	timer_delay_ns
	ldr	r0, .L429
	ldr	r1, .L429+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L422:
	ldrb	r3, [r1, #1174]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L428
	ldrb	r3, [r1, #1173]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L422
.L428:
	ldmfd	sp!, {r3, r4, r5, lr}
	b	nandc_wait_flash_ready
.L430:
	.align	2
.L429:
	.word	.LANCHOR1+128
	.word	.LANCHOR0
	.fnend
	.size	sandisk_set_rr_para, .-sandisk_set_rr_para
	.align	2
	.global	toshiba_3d_set_tlc_rr_para
	.type	toshiba_3d_set_tlc_rr_para, %function
toshiba_3d_set_tlc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	add	r1, r1, #1
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	rsb	r1, r1, r1, asl #3
	ldr	r5, .L433
	mov	r6, #0
	mov	r7, #213
	mvn	r3, #118
	add	r5, r5, r1
	str	r7, [r0, #8]
	str	r6, [r0, #4]
	mov	r4, r0
	str	r3, [r0, #4]
	add	r3, r5, #284
	ldrsb	r3, [r3]
	str	r3, [r0]
	movw	r3, #285
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #286
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #287
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mvn	r3, #117
	str	r6, [r4, #4]
	str	r3, [r4, #4]
	add	r3, r5, #288
	ldrsb	r3, [r3]
	str	r3, [r4]
	movw	r3, #289
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #290
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	nandc_wait_flash_ready
.L434:
	.align	2
.L433:
	.word	.LANCHOR1
	.fnend
	.size	toshiba_3d_set_tlc_rr_para, .-toshiba_3d_set_tlc_rr_para
	.align	2
	.global	toshiba_3d_set_slc_rr_para
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
	ldr	r2, .L436
	add	r2, r2, r1
	movw	r1, #685
	ldrsb	r2, [r2, r1]
	str	r2, [r0]
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L437:
	.align	2
.L436:
	.word	.LANCHOR1
	.fnend
	.size	toshiba_3d_set_slc_rr_para, .-toshiba_3d_set_slc_rr_para
	.align	2
	.global	toshiba_tlc_set_rr_para
	.type	toshiba_tlc_set_rr_para, %function
toshiba_tlc_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r2, #0
	ldr	r3, .L442
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, #239
	mov	r4, r0
	str	r6, [r0, #8]
	beq	.L439
	rsb	r1, r1, r1, asl #3
	mov	r2, #18
	add	r5, r3, r1
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
	str	r6, [r4, #8]
	mov	r3, #19
	str	r3, [r4, #4]
	ldrb	r3, [r5, #40]	@ zero_extendqisi2
	str	r3, [r4]
	ldrb	r3, [r5, #41]	@ zero_extendqisi2
	str	r3, [r4]
	ldrb	r3, [r5, #42]	@ zero_extendqisi2
	str	r3, [r4]
	mov	r3, #0
	str	r3, [r4]
	b	.L440
.L439:
	add	r3, r3, r1
	mov	r0, #20
	str	r0, [r4, #4]
	ldrb	r3, [r3, #368]	@ zero_extendqisi2
	str	r3, [r4]
	str	r2, [r4]
	str	r2, [r4]
	str	r2, [r4]
.L440:
	ldmfd	sp!, {r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L443:
	.align	2
.L442:
	.word	.LANCHOR2
	.fnend
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	ymtc_3d_set_tlc_rr_para
	.type	ymtc_3d_set_tlc_rr_para, %function
ymtc_3d_set_tlc_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	rsb	r1, r1, r1, asl #3
	ldr	r5, .L446
	mov	r7, #239
	mov	r3, #160
	str	r7, [r0, #8]
	add	r5, r5, r1
	str	r3, [r0, #4]
	add	r3, r5, #696
	mov	r4, r0
	mov	r6, #0
	ldrsb	r3, [r3]
	str	r3, [r0]
	add	r3, r5, #700
	ldrsb	r3, [r3]
	str	r3, [r0]
	str	r6, [r0]
	str	r6, [r0]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mov	r3, #161
	str	r3, [r4, #4]
	movw	r3, #697
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #699
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #701
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mov	r3, #162
	str	r3, [r4, #4]
	movw	r3, #698
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #702
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	str	r6, [r4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	nandc_wait_flash_ready
.L447:
	.align	2
.L446:
	.word	.LANCHOR1
	.fnend
	.size	ymtc_3d_set_tlc_rr_para, .-ymtc_3d_set_tlc_rr_para
	.align	2
	.global	ymtc_3d_set_slc_rr_para
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
	ldr	r3, .L449
	ldrsb	r3, [r3, r1]
	str	r3, [r0]
	mov	r3, #0
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L450:
	.align	2
.L449:
	.word	.LANCHOR1+1056
	.fnend
	.size	ymtc_3d_set_slc_rr_para, .-ymtc_3d_set_slc_rr_para
	.align	2
	.global	flash_start_plane_read
	.type	flash_start_plane_read, %function
flash_start_plane_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #1
	ldr	r7, .L479
	ubfx	r9, r0, #24, #2
	ldrb	r2, [r7, #1189]	@ zero_extendqisi2
	rsb	r5, r2, #24
	mov	r4, r3, asl r5
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	sub	r4, r4, #1
	and	r5, r3, r0, lsr r5
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	and	r6, r4, r0
	and	r4, r4, r1
	uxtb	r5, r5
	cmp	r3, r5
	bhi	.L452
	ldr	r1, .L479+4
	movw	r2, #1148
	ldr	r0, .L479+8
	bl	printk
	bl	dump_stack
.L452:
	add	r5, r7, r5
	ldr	r8, [r7, #1040]
	ldr	fp, .L479
	ldrb	r5, [r5, #1180]	@ zero_extendqisi2
	mov	r0, r5
	mov	r10, r5, asl #8
	bl	nandc_cs
	cmp	r9, #0
	bne	.L453
	mov	r0, r6
	bl	slc_phy_page_address_calc
	mov	r6, r0
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [fp]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r4, r0
	beq	.L454
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L454
.L453:
	ldr	r3, [fp, #1096]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L455
	ldrb	r3, [fp, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L455
	ldrb	r3, [fp, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r8, r10
	streq	r9, [r3, #2056]
	beq	.L454
.L455:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L454:
	ldrb	r2, [r7, #1119]	@ zero_extendqisi2
	uxtb	ip, r6
	ldr	r3, .L479
	mov	r0, r6, lsr #8
	cmp	r2, #1
	mov	r1, r6, lsr #16
	bne	.L456
	ldrb	r2, [r3, #1111]	@ zero_extendqisi2
	add	r5, r8, r10
	ldrb	r3, [r3, #1188]	@ zero_extendqisi2
	str	r2, [r5, #2056]
	mov	r2, #0
	cmp	r3, r2
	ldrb	r3, [r7, #1112]	@ zero_extendqisi2
	str	r2, [r5, #2052]
	movne	r6, r6, lsr #24
	str	r2, [r5, #2052]
	str	ip, [r5, #2052]
	str	r0, [r5, #2052]
	str	r1, [r5, #2052]
	strne	r6, [r5, #2052]
	add	r6, r8, r10
	str	r3, [r6, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #1096]
	ldrb	r2, [r3, #12]	@ zero_extendqisi2
	adds	r3, r9, #0
	movne	r3, #1
	cmp	r2, #3
	add	r2, r8, r10
	add	r10, r8, r10
	movne	r3, #0
	cmp	r3, #0
	mov	r3, #0
	strne	r9, [r6, #2056]
	str	r3, [r2, #2056]
	str	r3, [r5, #2052]
	str	r3, [r5, #2052]
	uxtb	r3, r4
	str	r3, [r5, #2052]
	mov	r3, r4, lsr #8
	str	r3, [r5, #2052]
	mov	r3, r4, lsr #16
	str	r3, [r5, #2052]
	ldrb	r3, [r7, #1188]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r3, #48
	movne	r4, r4, lsr #24
	strne	r4, [r5, #2052]
	str	r3, [r10, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L456:
	ldr	r2, [r3, #1096]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L461
	ldrb	r3, [r2, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r8, r10
	moveq	lr, #38
	streq	lr, [r3, #2056]
.L461:
	ldrb	lr, [r7, #1111]	@ zero_extendqisi2
	add	r3, r8, r10
	add	r8, r8, r10
	str	lr, [r3, #2056]
	str	ip, [r3, #2052]
	str	r0, [r3, #2052]
	str	r1, [r3, #2052]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r9, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	ldrb	r2, [r7, #1112]	@ zero_extendqisi2
	strne	r9, [r3, #2056]
	str	r2, [r8, #2056]
	uxtb	r2, r4
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #8
	mov	r4, r4, lsr #16
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	mov	r3, #48
	str	r3, [r8, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L480:
	.align	2
.L479:
	.word	.LANCHOR0
	.word	.LANCHOR1+1068
	.word	.LC0
	.fnend
	.size	flash_start_plane_read, .-flash_start_plane_read
	.align	2
	.global	flash_set_interface_mode
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r0
	ldr	r6, .L515
	mov	r9, #0
	ldr	r10, .L515+4
	mov	r7, r6
.L493:
	ldr	r3, .L515+8
	ldr	r5, [r6, #1040]
	ldrb	r4, [r3, r9, asl #3]	@ zero_extendqisi2
	cmp	r4, #69
	cmpne	r4, #152
	beq	.L482
	cmp	r4, #137
	cmpne	r4, #44
	moveq	r3, #1
	movne	r3, #0
	cmp	r4, #155
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L483
.L482:
	cmp	r8, #1
	ldrb	r1, [r7, #1232]	@ zero_extendqisi2
	bne	.L484
	tst	r1, #1
	beq	.L483
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L485
	ldr	r0, .L515+12
	bl	printk
.L485:
	mov	r2, r9, asl #8
	cmp	r4, #44
	cmpne	r4, #137
	add	r3, r5, r2
	mov	r1, #239
	str	r1, [r3, #2056]
	beq	.L514
	cmp	r4, #155
	movne	r1, #128
	strne	r1, [r3, #2052]
	movne	r1, #1
	bne	.L512
.L514:
	mov	r1, #1
	str	r1, [r3, #2052]
	mov	r1, #5
	b	.L512
.L484:
	tst	r1, #4
	beq	.L483
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L489
	ldr	r0, .L515+16
	bl	printk
.L489:
	mov	r2, r9, asl #8
	cmp	r4, #44
	cmpne	r4, #137
	add	r3, r5, r2
	mov	r1, #239
	str	r1, [r3, #2056]
	moveq	r1, #1
	movne	r1, #0
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #35
	beq	.L512
	cmp	r4, #155
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #37
	movne	r0, #128
	strne	r0, [r3, #2052]
.L512:
	add	r5, r5, r2
	str	r1, [r3, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L483:
	add	r9, r9, #1
	cmp	r9, #4
	bne	.L493
	bl	nandc_wait_flash_ready
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L516:
	.align	2
.L515:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1200
	.word	.LC37
	.word	.LC38
	.fnend
	.size	flash_set_interface_mode, .-flash_set_interface_mode
	.align	2
	.global	flash_reset
	.type	flash_reset, %function
flash_reset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L518
	mov	r2, #255
	ldr	r3, [r3, #1040]
	add	r0, r3, r0, asl #8
	str	r2, [r0, #2056]
	b	nandc_wait_flash_ready
.L519:
	.align	2
.L518:
	.word	.LANCHOR0
	.fnend
	.size	flash_reset, .-flash_reset
	.align	2
	.global	flash_read_id
	.type	flash_read_id, %function
flash_read_id:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	mov	r6, r0
	ldr	r3, .L523
	mov	r4, r1
	ldr	r5, [r3, #1040]
	bl	flash_reset
	mov	r0, r6
	add	r5, r5, r6, asl #8
	bl	nandc_cs
	mov	r3, #144
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #0
	str	r3, [r5, #2052]
	bl	timer_delay_ns
	ldr	r3, [r5, #2048]
	mov	r0, r6
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
	bl	nandc_de_cs
	ldrb	r2, [r4]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L520
	ldrb	r1, [r4, #2]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	ldr	r0, .L523+4
	str	r1, [sp]
	ldrb	r1, [r4, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r4, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r6, #1
	bl	printk
.L520:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L524:
	.align	2
.L523:
	.word	.LANCHOR0
	.word	.LC39
	.fnend
	.size	flash_read_id, .-flash_read_id
	.align	2
	.global	flash_read_spare
	.type	flash_read_spare, %function
flash_read_spare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r2
	ldr	r3, .L527
	ldr	r2, .L527+4
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	ldr	r4, [r2, #1040]
	mov	r2, #0
	add	r4, r4, r0, asl #8
	mov	r3, r3, asl #9
	str	r2, [r4, #2056]
	str	r3, [r4, #2052]
	mov	r3, r3, lsr #8
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	mov	r3, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #2048]
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L528:
	.align	2
.L527:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_read_spare, .-flash_read_spare
	.align	2
	.global	flash_read_otp_data
	.type	flash_read_otp_data, %function
flash_read_otp_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L533
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r0
	ldr	r7, [r3, #1040]
	mov	r10, r8, asl #8
	mov	r5, r1
	mov	r9, r2
	add	r4, r7, r10
	bl	nandc_cs
	mov	fp, #144
	mov	r3, #239
	mov	r0, #50
	str	r3, [r4, #2056]
	str	fp, [r4, #2052]
	mov	r6, #0
	bl	timer_delay_ns
	mov	r3, #1
	str	r3, [r4, #2048]
	str	r6, [r4, #2048]
	str	r6, [r4, #2048]
	str	r6, [r4, #2048]
	bl	nandc_wait_flash_ready
	mov	r3, #238
	str	r3, [r4, #2056]
	str	fp, [r4, #2052]
	ldr	r2, [r4, #2048]
	ldr	r3, [r4, #2048]
	ldr	r1, [r4, #2048]
	ldr	r0, .L533+4
	str	r1, [sp]
	mov	r1, r5
	bl	printk
	bl	nandc_wait_flash_ready
	str	r6, [r4, #2056]
	uxtb	r3, r5
	str	r6, [r4, #2052]
	str	r6, [r4, #2052]
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #8
	mov	r5, r5, lsr #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r5, [r4, #2052]
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	add	r3, r9, #16384
.L530:
	ldr	r2, [r4, #2048]
	strb	r2, [r9], #1
	cmp	r9, r3
	bne	.L530
	add	r7, r7, r10
	mov	r3, #239
	mov	r0, #50
	str	r3, [r7, #2056]
	mov	r3, #144
	str	r3, [r4, #2052]
	bl	timer_delay_ns
	mov	r3, #0
	mov	r0, r8
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L534:
	.align	2
.L533:
	.word	.LANCHOR0
	.word	.LC40
	.fnend
	.size	flash_read_otp_data, .-flash_read_otp_data
	.align	2
	.global	sandisk_prog_test_bad_block
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L551
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0, asl #8
	ldr	r6, .L551+4
	mov	r8, r1
	ldr	r5, [r3, #1040]
	ldrb	r4, [r6, #33]	@ zero_extendqisi2
	add	r3, r5, r7
	cmp	r4, #0
	strne	r4, [r3, #2056]
	add	r4, r5, r7
	moveq	r2, #162
	streq	r2, [r3, #2056]
	mov	r3, #128
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	mov	r3, r1, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r1, lsr #16
	str	r3, [r4, #2052]
	mov	r3, #16
	str	r3, [r4, #2056]
	bl	nandc_wait_flash_ready
	mov	r3, #112
	mov	r0, #200
	str	r3, [r4, #2056]
	bl	timer_delay_ns
	ldr	r2, [r4, #2048]
	cmp	r2, #255
	ldreq	r2, [r4, #2048]
	ands	r4, r2, #5
	beq	.L539
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L539
	ldr	r0, .L551+8
	mov	r1, r8
	bl	printk
.L539:
	ldrb	r3, [r6, #34]	@ zero_extendqisi2
	mov	r0, r4
	cmp	r3, #0
	addne	r5, r5, r7
	strne	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L552:
	.align	2
.L551:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC41
	.fnend
	.size	sandisk_prog_test_bad_block, .-sandisk_prog_test_bad_block
	.align	2
	.global	nandc_rdy_status
	.type	nandc_rdy_status, %function
nandc_rdy_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L555
	.pad #8
	sub	sp, sp, #8
	ldr	r3, [r3, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #9, #1
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L556:
	.align	2
.L555:
	.word	.LANCHOR0
	.fnend
	.size	nandc_rdy_status, .-nandc_rdy_status
	.align	2
	.global	nandc_bch_sel
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
	ldr	r2, .L570
	ldrb	r1, [r2, #1028]	@ zero_extendqisi2
	strb	r0, [r2, #1172]
	cmp	r1, #9
	bne	.L558
	ldr	r1, [r2, #1040]
	cmp	r0, #70
	ldr	r2, [sp, #4]
	str	r2, [r1, #16]
	beq	.L559
	cmp	r0, #60
	moveq	r3, #3
	beq	.L559
	cmp	r0, #40
	moveq	r3, #2
	movne	r3, #1
.L559:
	mov	r2, #0
	str	r2, [sp]
	ldr	r2, [sp]
	bic	r2, r2, #234881024
	orr	r3, r2, r3, asl #25
	str	r3, [sp]
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r1, #32]
	b	.L557
.L558:
	ldr	r1, [sp, #4]
	cmp	r0, #16
	ldr	r2, [r2, #1040]
	str	r1, [r2, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	bic	r3, r3, #65280
	orr	r3, r3, #4096
	str	r3, [sp]
	ldr	r3, [sp]
	bic	r3, r3, #262144
	str	r3, [sp]
	beq	.L568
	ldr	r3, [sp]
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L569
	orr	r3, r3, #262144
	str	r3, [sp]
	ldr	r3, [sp]
	cmp	r0, #40
	orr	r3, r3, #16
	str	r3, [sp]
	bne	.L562
.L568:
	ldr	r3, [sp]
	bic	r3, r3, #16
.L569:
	str	r3, [sp]
.L562:
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r2, #12]
.L557:
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L571:
	.align	2
.L570:
	.word	.LANCHOR0
	.fnend
	.size	nandc_bch_sel, .-nandc_bch_sel
	.align	2
	.global	zftl_flash_resume
	.type	zftl_flash_resume, %function
zftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r4, .L583
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r2, [r4, #1044]
	cmp	r3, #9
	ldr	r3, [r4, #1040]
	str	r2, [r3]
	ldr	r2, [r4, #1048]
	ldr	r3, [r4, #1040]
	str	r2, [r3, #4]
	ldr	r2, [r4, #1052]
	bne	.L573
	str	r2, [r3, #16]
	ldr	r2, [r4, #1056]
	str	r2, [r3, #32]
	ldr	r2, [r4, #1060]
	str	r2, [r3, #80]
	ldr	r2, [r4, #1064]
	str	r2, [r3, #84]
	ldr	r2, [r4, #1068]
	str	r2, [r3, #520]
	ldr	r2, [r4, #1072]
	str	r2, [r3, #8]
	b	.L574
.L573:
	str	r2, [r3, #8]
	ldr	r2, [r4, #1056]
	str	r2, [r3, #12]
	ldr	r2, [r4, #1060]
	str	r2, [r3, #304]
	ldr	r2, [r4, #1064]
	str	r2, [r3, #308]
	ldr	r2, [r4, #1068]
	str	r2, [r3, #336]
	ldr	r2, [r4, #1072]
	str	r2, [r3, #344]
.L574:
	ldr	r6, .L583+4
	mov	r5, #0
	mov	r8, #2
	sub	r7, r6, #8
.L576:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L575
	uxtb	r9, r5
	mov	r0, r9
	bl	flash_reset
	strb	r8, [r5, r7]
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
.L575:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L576
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L577
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r0, r5
	bl	flash_set_interface_mode
	mov	r0, r5
	bl	nandc_set_if_mode
	ldr	r3, .L583
	ldrb	r0, [r3, #1061]	@ zero_extendqisi2
	bl	nandc_set_ddr_para
.L577:
	ldr	r3, .L583+8
	ldrb	r0, [r3, #24]	@ zero_extendqisi2
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	nandc_bch_sel
.L584:
	.align	2
.L583:
	.word	.LANCHOR0
	.word	.LANCHOR0+1200
	.word	.LANCHOR2
	.fnend
	.size	zftl_flash_resume, .-zftl_flash_resume
	.align	2
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L588
	ldrb	r3, [r3, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	ldreq	r3, [r0, #292]
	ldrne	r3, [r0, #368]
	orreq	r3, r3, #2
	orrne	r3, r3, #2
	streq	r3, [r0, #292]
	strne	r3, [r0, #368]
	ldreq	r3, [r0, #288]
	ldrne	r3, [r0, #364]
	biceq	r3, r3, #2
	bicne	r3, r3, #2
	streq	r3, [r0, #288]
	strne	r3, [r0, #364]
	bx	lr
.L589:
	.align	2
.L588:
	.word	.LANCHOR0
	.fnend
	.size	rk_nandc_flash_ready, .-rk_nandc_flash_ready
	.align	2
	.global	nandc_iqr_wait_flash_ready
	.type	nandc_iqr_wait_flash_ready, %function
nandc_iqr_wait_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L597
	ldr	r0, [r4, #1040]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, [r4, #1040]
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
	bne	.L593
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L593:
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	ldreq	r3, [r0, #292]
	ldrne	r3, [r0, #368]
	orreq	r3, r3, #2
	orrne	r3, r3, #2
	streq	r3, [r0, #292]
	strne	r3, [r0, #368]
	ldreq	r3, [r0, #288]
	ldrne	r3, [r0, #364]
	biceq	r3, r3, #2
	bicne	r3, r3, #2
	streq	r3, [r0, #288]
	strne	r3, [r0, #364]
	ldmfd	sp!, {r4, pc}
.L598:
	.align	2
.L597:
	.word	.LANCHOR0
	.fnend
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r3
	ldr	fp, .L624
	mov	r4, r0
	mov	r5, r1
	mov	r9, r2
	ldrb	r3, [fp, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L600
	ldr	r1, .L624+4
	movw	r2, #695
	ldr	r0, .L624+8
	bl	printk
	bl	dump_stack
.L600:
	ldrb	r2, [fp, #1101]	@ zero_extendqisi2
	ldr	r3, .L624
	cmp	r2, r4
	mvnls	r0, #0
	bls	.L601
	add	r4, r3, r4
	ldr	r7, [r3, #1040]
	ldr	r3, .L624+12
	ldrb	r6, [r4, #1180]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r7, r10, asl #8
	beq	.L602
	ldr	r0, .L624+16
	mov	r1, r6
	mov	r2, r9
	mov	r3, r8
	bl	printk
.L602:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r5, #0
	mov	r0, r6
	bne	.L603
	bl	zftl_flash_enter_slc_mode
	b	.L604
.L603:
	bl	zftl_flash_exit_slc_mode
.L604:
	mov	r3, r6, asl #8
	mov	r2, #96
	add	r4, r7, r3
	str	r2, [r4, #2056]
	uxtb	r2, r9
	str	r2, [r4, #2052]
	mov	r2, r9, lsr #8
	str	r2, [r4, #2052]
	mov	r2, r9, lsr #16
	str	r2, [r4, #2052]
	ldrb	r2, [fp, #1188]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r9, lsr #24
	strne	r2, [r4, #2052]
	ldrb	r2, [fp, #1233]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r5, #0
	bne	.L606
	add	r2, r7, r3
	mov	r1, #208
	str	r3, [sp, #4]
	str	r1, [r2, #2056]
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	ldr	r3, [sp, #4]
	and	r5, r0, #5
.L606:
	add	r2, r7, r3
	mov	r1, #96
	add	r7, r7, r3
	mov	r3, #208
	str	r1, [r2, #2056]
	uxtb	r2, r8
	str	r2, [r4, #2052]
	mov	r2, r8, lsr #8
	str	r2, [r4, #2052]
	mov	r2, r8, lsr #16
	str	r2, [r4, #2052]
	ldrb	r2, [fp, #1188]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r8, r8, lsr #24
	strne	r8, [r4, #2052]
	str	r3, [r7, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r6
	bl	nandc_de_cs
	and	r3, r4, #5
	orrs	r5, r3, r5
	beq	.L608
	ldr	r0, .L624+20
	mov	r1, r9
	mov	r2, r4
	bl	printk
.L608:
	mov	r0, r5
.L601:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L625:
	.align	2
.L624:
	.word	.LANCHOR0
	.word	.LANCHOR1+1092
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC42
	.word	.LC43
	.fnend
	.size	flash_erase_duplane_block, .-flash_erase_duplane_block
	.align	2
	.global	flash_erase_block_en
	.type	flash_erase_block_en, %function
flash_erase_block_en:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mvn	r4, #0
	ldr	r5, .L645
	mov	r7, r0
	mov	fp, r1
	mov	r8, r2
	ldrb	r3, [r5, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	bic	r4, r2, r4, asl r3
	ldrb	r3, [r5, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L627
	ldr	r1, .L645+4
	movw	r2, #757
	ldr	r0, .L645+8
	bl	printk
	bl	dump_stack
.L627:
	ldrb	r2, [r5, #1101]	@ zero_extendqisi2
	ldr	r3, .L645
	cmp	r2, r7
	bls	.L634
	add	r2, r3, r7
	ldr	r9, [r3, #1040]
	ldr	r3, .L645+12
	ldrb	r6, [r2, #1180]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r9, r10, asl #8
	beq	.L629
	ldr	r0, .L645+16
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L629:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r6
	bne	.L630
	bl	zftl_flash_enter_slc_mode
	b	.L631
.L630:
	bl	zftl_flash_exit_slc_mode
.L631:
	mov	r2, r6, asl #8
	mov	r1, #96
	add	r3, r9, r2
	add	r9, r9, r2
	str	r1, [r3, #2056]
	uxtb	r1, r4
	str	r1, [r3, #2052]
	mov	r1, r4, lsr #8
	str	r1, [r3, #2052]
	mov	r1, r4, lsr #16
	str	r1, [r3, #2052]
	ldrb	r1, [r5, #1188]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r4, r4, lsr #24
	strne	r4, [r3, #2052]
	mov	r3, #208
	str	r3, [r9, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r6
	bl	nandc_de_cs
	ands	r4, r4, #5
	beq	.L633
	ldrh	r1, [r5, #2]
	mov	r0, r8
	bl	__aeabi_uidiv
	mov	r3, r4
	mov	r1, r7
	mvn	r4, #0
	mov	r2, r0
	ldr	r0, .L645+20
	bl	printk
.L633:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L634:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L646:
	.align	2
.L645:
	.word	.LANCHOR0
	.word	.LANCHOR1+1120
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC44
	.word	.LC45
	.fnend
	.size	flash_erase_block_en, .-flash_erase_block_en
	.align	2
	.global	flash_erase_block
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
	.type	flash_erase_all, %function
flash_erase_all:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #0
	ldr	r3, .L656
	ldr	r7, .L656+4
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	mov	r8, r7
	smulbb	r4, r4, r3
	uxth	r4, r4
.L649:
	ldrb	r2, [r7, #1101]	@ zero_extendqisi2
	uxth	r3, r6
	cmp	r2, r3
	bls	.L654
	add	r3, r7, r3
	mov	r5, #0
	ldrb	r9, [r3, #1180]	@ zero_extendqisi2
.L650:
	uxth	r3, r5
	cmp	r3, r4
	bcs	.L655
	ldrh	r1, [r8, #2]
	mov	r0, r9
	mul	r1, r1, r5
	bl	flash_erase_block
	add	r5, r5, #1
	b	.L650
.L655:
	add	r6, r6, #1
	b	.L649
.L654:
	ldr	r0, .L656+8
	mov	r1, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L657:
	.align	2
.L656:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC46
	.fnend
	.size	flash_erase_all, .-flash_erase_all
	.align	2
	.global	rk_nandc_flash_xfer_completed
	.type	rk_nandc_flash_xfer_completed, %function
rk_nandc_flash_xfer_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L661
	ldrb	r3, [r3, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	ldreq	r3, [r0, #292]
	ldrne	r3, [r0, #368]
	orreq	r3, r3, #1
	orrne	r3, r3, #1
	streq	r3, [r0, #292]
	strne	r3, [r0, #368]
	ldreq	r3, [r0, #288]
	ldrne	r3, [r0, #364]
	biceq	r3, r3, #1
	bicne	r3, r3, #1
	streq	r3, [r0, #288]
	strne	r3, [r0, #364]
	bx	lr
.L662:
	.align	2
.L661:
	.word	.LANCHOR0
	.fnend
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
	.align	2
	.global	nandc_xfer_start
	.type	nandc_xfer_start, %function
nandc_xfer_start:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r5, r0
	ldr	r4, .L680
	mov	r0, r2
	.pad #20
	sub	sp, sp, #20
	and	r6, r5, #1
	add	lr, r1, #1
	ldrb	r2, [r4, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L664
	mov	r2, #0
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	ubfx	lr, lr, #1, #6
	bic	r2, r2, #2
	orr	r2, r2, r6, asl #1
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #8
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #96
	orr	r2, r2, #32
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #536870912
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #1024
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #16
	str	r2, [sp, #4]
	ldr	ip, [sp, #4]
	bic	ip, ip, #264241152
	orr	lr, ip, lr, asl #22
	str	lr, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #128
	str	r2, [sp, #4]
	ldrb	r2, [r4, #1136]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L665
	ldrb	r2, [r4, #1168]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [sp, #4]
	orrne	r2, r2, #512
	strne	r2, [sp, #4]
.L665:
	ldr	r1, [sp, #4]
	mov	r2, r5
	str	r3, [r4, #1148]
	ubfx	r1, r1, #22, #6
	str	r0, [r4, #1144]
	mov	r1, r1, asl #10
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #1152]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #1148]
	mov	r1, r1, asl #2
	bl	rknand_dma_map_single
	ldr	r2, [r4, #1040]
	mov	r3, #1
	str	r3, [r4, #1160]
	ldr	r3, [r4, #1152]
	str	r3, [r2, #52]
	str	r0, [r2, #56]
	ldr	r3, [r2, #48]
	str	r0, [r4, #1156]
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	bic	r3, r3, #15872
	orr	r3, r3, #8192
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	orr	r3, r3, #448
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	bic	r3, r3, #56
	orr	r3, r3, #16
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	orr	r3, r3, #4
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	bic	r3, r3, #2
	orr	r5, r3, r5, asl #1
	str	r5, [sp, #8]
	ldr	r3, [sp, #8]
	orr	r3, r3, #1
	str	r3, [sp, #8]
	movw	r3, #1170
	ldrh	r1, [r4, r3]
	ldr	r3, [sp, #8]
	ubfx	r1, r1, #0, #11
	bic	r3, r3, #133169152
	bic	r3, r3, #983040
	orr	r3, r3, r1, asl #16
	str	r3, [sp, #8]
	ldr	r3, [sp, #8]
	str	r3, [r2, #48]
	ldr	r3, [sp, #4]
	str	r3, [r2, #16]
	ldr	r3, [sp, #4]
	orr	r3, r3, #4
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	str	r3, [r2, #16]
	b	.L663
.L664:
	ldr	r2, [r4, #1040]
	mov	ip, #0
	cmp	r5, ip
	ubfx	lr, lr, #1, #6
	ldr	r2, [r2, #12]
	ldreq	r3, [r4, #1140]
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bic	r2, r2, #65280
	orr	r2, r2, #4096
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bic	r2, r2, #8
	str	r2, [sp, #12]
	ldr	r2, [sp, #12]
	bic	r2, r2, #224
	str	r2, [sp, #12]
	str	ip, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #2
	orr	r2, r2, r6, asl #1
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #8
	str	r2, [sp, #4]
	ldr	r6, [sp, #4]
	bic	r6, r6, #96
	orr	r6, r6, #32
	str	r6, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #536870912
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	orr	r2, r2, #1024
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #16
	str	r2, [sp, #4]
	ldr	r6, [sp, #4]
	moveq	r2, #1
	bic	r6, r6, #264241152
	orr	lr, r6, lr, asl #22
	str	lr, [sp, #4]
	streq	r2, [r3]
	beq	.L671
	ldrb	r2, [r4, #1172]	@ zero_extendqisi2
	mov	r1, r1, lsr #1
	mov	r6, ip
	cmp	r2, #25
	movcc	r7, #64
	movcs	r7, #128
.L669:
	cmp	r6, r1
	add	r3, r3, #4
	add	r8, ip, r7
	bge	.L671
	ldrh	lr, [r3, #-2]
	mov	ip, ip, lsr #2
	ldr	r9, [r4, #1140]
	add	r6, r6, #1
	ldrh	r2, [r3, #-4]
	orr	lr, r2, lr, asl #16
	str	lr, [r9, ip, asl #2]
	mov	ip, r8
	b	.L669
.L671:
	ldr	r1, [sp, #4]
	mov	r2, r5
	ldr	r3, [r4, #1140]
	ubfx	r1, r1, #22, #6
	str	r0, [r4, #1144]
	mov	r1, r1, asl #10
	str	r3, [r4, #1148]
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #1152]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #1148]
	mov	r1, r1, asl #7
	bl	rknand_dma_map_single
	ldr	r2, [r4, #1152]
	mov	r3, #1
	str	r3, [r4, #1160]
	ldr	r3, [r4, #1040]
	str	r2, [r3, #20]
	mov	r2, #0
	str	r0, [r3, #24]
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	str	r0, [r4, #1156]
	bic	r2, r2, #15872
	orr	r2, r2, #8192
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #448
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bic	r2, r2, #56
	orr	r2, r2, #16
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	orr	r2, r2, #4
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	bic	r2, r2, #2
	orr	r5, r2, r5, asl #1
	str	r5, [sp, #8]
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
.L663:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L681:
	.align	2
.L680:
	.word	.LANCHOR0
	.fnend
	.size	nandc_xfer_start, .-nandc_xfer_start
	.align	2
	.global	nandc_set_seed
	.type	nandc_set_seed, %function
nandc_set_seed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L689
	and	r0, r0, #127
	ldr	r3, .L689+4
	mov	r0, r0, asl #1
	ldrb	r1, [r2, #1168]	@ zero_extendqisi2
	ldrh	r3, [r3, r0]
	cmp	r1, #0
	ldr	r1, .L689
	ldrb	r2, [r2, #1028]	@ zero_extendqisi2
	orrne	r3, r3, #-1073741824
	cmp	r2, #9
	ldr	r2, [r1, #1040]
	streq	r3, [r2, #520]
	strne	r3, [r2, #336]
	bx	lr
.L690:
	.align	2
.L689:
	.word	.LANCHOR0
	.word	.LANCHOR1+1144
	.fnend
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	zftl_flash_de_init
	.type	zftl_flash_de_init, %function
zftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	bl	nandc_wait_flash_ready
	ldr	r4, .L711
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L692
	ldrb	r0, [r4, #1102]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L692
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	beq	.L692
	bl	zftl_flash_exit_slc_mode
.L692:
	mov	r0, #0
	ldr	r5, .L711
	bl	hynix_reconfig_rr_para
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L693
	ldrb	r3, [r5, #1232]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L693
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r5, #1135]
.L693:
	ldrb	r3, [r4, #1168]	@ zero_extendqisi2
	ldr	r5, .L711
	cmp	r3, #0
	beq	.L694
	mov	r0, #0
	strb	r0, [r5, #1168]
	bl	nandc_set_seed
	mov	r3, #1
	strb	r3, [r5, #1168]
.L694:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L712:
	.align	2
.L711:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L714
	strb	r0, [r3, #1168]
	bx	lr
.L715:
	.align	2
.L714:
	.word	.LANCHOR0
	.fnend
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L717
	add	r0, r0, #8
	ldr	r3, [r3, #1040]
	add	r0, r3, r0, asl #8
	bx	lr
.L718:
	.align	2
.L717:
	.word	.LANCHOR0
	.fnend
	.size	nandc_get_chip_if, .-nandc_get_chip_if
	.align	2
	.global	zbuf_init
	.type	zbuf_init, %function
zbuf_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, #0
	ldr	r4, .L723
	mov	r7, r5
	ldr	r8, .L723+4
.L720:
	ldrb	r0, [r8, #2772]	@ zero_extendqisi2
	uxtb	r3, r5
	add	r2, r3, #1
	strb	r3, [r4, #1]
	strb	r2, [r4]
	add	r5, r5, #1
	mov	r0, r0, asl #9
	strb	r7, [r4, #2]
	str	r7, [r4, #8]
	bl	ftl_malloc
	ldr	r6, .L723+4
	add	r4, r4, #48
	str	r0, [r4, #-44]
	mov	r0, #64
	bl	ftl_malloc
	cmp	r5, #32
	str	r0, [r4, #-36]
	bne	.L720
	mvn	r3, #0
	strb	r5, [r6, #2774]
	strb	r3, [r6, #2724]
	mov	r3, #0
	strb	r3, [r6, #2773]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L724:
	.align	2
.L723:
	.word	.LANCHOR0+1236
	.word	.LANCHOR0
	.fnend
	.size	zbuf_init, .-zbuf_init
	.align	2
	.global	buf_reinit
	.type	buf_reinit, %function
buf_reinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L728
	mov	r3, #0
	mov	r1, r3
.L726:
	uxtb	r0, r3
	add	r3, r3, #1
	cmp	r3, #32
	add	ip, r0, #1
	strb	r0, [r2, #1]
	add	r2, r2, #48
	strb	ip, [r2, #-48]
	strb	r1, [r2, #-46]
	str	r1, [r2, #-40]
	bne	.L726
	ldr	r2, .L728+4
	mvn	r1, #0
	strb	r3, [r2, #2774]
	strb	r1, [r2, #2724]
	mov	r1, #0
	strb	r1, [r2, #2773]
	bx	lr
.L729:
	.align	2
.L728:
	.word	.LANCHOR0+1236
	.word	.LANCHOR0
	.fnend
	.size	buf_reinit, .-buf_reinit
	.align	2
	.global	buf_add_tail
	.type	buf_add_tail, %function
buf_add_tail:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mvn	r3, #0
	strb	r3, [r1]
	mov	r4, r1
	ldrb	r3, [r0]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #255
	ldrne	r2, .L738
	movne	r1, #48
	bne	.L736
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L732
	ldr	r1, .L738+4
	mov	r2, #74
	ldr	r0, .L738+8
	bl	printk
	bl	dump_stack
.L732:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L736:
	mov	r5, r3
	mla	r3, r1, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L736
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L734
	ldr	r1, .L738+4
	mov	r2, #81
	ldr	r0, .L738+8
	bl	printk
	bl	dump_stack
.L734:
	ldr	r2, .L738
	mov	r1, #48
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	mla	r5, r1, r5, r2
	strb	r3, [r5, #1236]
	ldmfd	sp!, {r3, r4, r5, pc}
.L739:
	.align	2
.L738:
	.word	.LANCHOR0
	.word	.LANCHOR1+1400
	.word	.LC0
	.fnend
	.size	buf_add_tail, .-buf_add_tail
	.align	2
	.type	queue_read_cmd, %function
queue_read_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r4, #24]
	mov	r0, #48
	bl	flash_start_page_read
	mov	r3, #1
	mov	r1, r4
	strb	r3, [r4, #42]
	ldr	r0, .L742
	mov	r3, #0
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, lr}
	b	buf_add_tail
.L743:
	.align	2
.L742:
	.word	.LANCHOR0+2775
	.fnend
	.size	queue_read_cmd, .-queue_read_cmd
	.align	2
	.global	zbuf_free
	.type	zbuf_free, %function
zbuf_free:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	ldr	r4, .L754
	and	r3, r3, #8
	uxtb	r3, r3
	strb	r3, [r0, #2]
	cmp	r3, #0
	beq	.L745
	ldr	r3, [r0, #20]
	cmn	r3, #1
	beq	.L745
	mov	r1, r0
	ldr	r0, .L754+4
	bl	buf_add_tail
	b	.L746
.L745:
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	strb	r3, [r0]
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	strb	r3, [r4, #2773]
.L746:
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #2774]
	ldmfd	sp!, {r4, pc}
.L755:
	.align	2
.L754:
	.word	.LANCHOR0
	.word	.LANCHOR0+2773
	.fnend
	.size	zbuf_free, .-zbuf_free
	.align	2
	.global	buf_alloc
	.type	buf_alloc, %function
buf_alloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L767
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L757
.L760:
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	cmp	r5, #0
	ldr	r3, .L767+4
	mov	r0, #48
	mla	r0, r0, r1, r3
	beq	.L758
	b	.L759
.L757:
	ldr	r1, .L767+8
	mov	r2, #121
	ldr	r0, .L767+12
	bl	printk
	bl	dump_stack
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L760
.L762:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L758:
	ldr	r2, .L767
	ldrb	r2, [r2, #2774]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L762
.L759:
	mov	r2, #48
	mul	r1, r2, r1
	add	r2, r4, r1
	add	r3, r3, r1
	mov	r1, #0
	ldrb	ip, [r2, #1236]	@ zero_extendqisi2
	strh	r1, [r3, #34]	@ movhi
	str	r1, [r2, #1244]
	strb	ip, [r4, #2773]
	ldrb	ip, [r4, #2774]	@ zero_extendqisi2
	strb	r1, [r2, #1276]
	sub	ip, ip, #1
	strb	r1, [r2, #1277]
	strb	ip, [r4, #2774]
	mov	ip, #1
	strb	ip, [r2, #1238]
	mvn	ip, #0
	strb	ip, [r2, #1236]
	str	ip, [r2, #1256]
	ldmfd	sp!, {r3, r4, r5, pc}
.L768:
	.align	2
.L767:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+1416
	.word	.LC0
	.fnend
	.size	buf_alloc, .-buf_alloc
	.align	2
	.global	buf_remove_buf
	.type	buf_remove_buf, %function
buf_remove_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrb	ip, [r1, #1]	@ zero_extendqisi2
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	ip, r3
	bne	.L770
	ldrb	r3, [r1]	@ zero_extendqisi2
	strb	r3, [r0]
	mov	r0, #1
	bx	lr
.L770:
	ldr	r0, .L777
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #48
.L772:
	cmp	r3, #255
	beq	.L776
	mov	r2, r3
	mla	r3, lr, r3, r0
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, ip
	bne	.L772
	ldr	r0, .L777
	mov	ip, #48
	ldrb	r3, [r1]	@ zero_extendqisi2
	mla	r2, ip, r2, r0
	mov	r0, #1
	strb	r3, [r2, #1236]
	mvn	r3, #0
	strb	r3, [r1]
	ldr	pc, [sp], #4
.L776:
	mov	r0, #0
	ldr	pc, [sp], #4
.L778:
	.align	2
.L777:
	.word	.LANCHOR0
	.fnend
	.size	buf_remove_buf, .-buf_remove_buf
	.align	2
	.global	buf_remove_free
	.type	buf_remove_free, %function
buf_remove_free:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L785
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrb	r2, [r3, #2774]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	bne	.L780
	ldr	r1, .L785+4
	mov	r2, #172
	ldr	r0, .L785+8
	bl	printk
	bl	dump_stack
.L780:
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	ldr	r6, .L785
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L785+12
	mov	r1, r5
	bl	buf_remove_buf
	cmp	r0, #1
	ldreqb	r3, [r6, #2774]	@ zero_extendqisi2
	subeq	r3, r3, #1
	streqb	r3, [r6, #2774]
	ldreqb	r3, [r5, #2]	@ zero_extendqisi2
	orreq	r3, r3, #1
	streqb	r3, [r5, #2]
	ldmfd	sp!, {r4, r5, r6, pc}
.L786:
	.align	2
.L785:
	.word	.LANCHOR0
	.word	.LANCHOR1+1428
	.word	.LC0
	.word	.LANCHOR0+2773
	.fnend
	.size	buf_remove_free, .-buf_remove_free
	.align	2
	.global	dump_buf_info
	.type	dump_buf_info, %function
dump_buf_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L791
	ldr	r0, .L791+4
	ldrb	r1, [r4, #2775]	@ zero_extendqisi2
	bl	printk
	ldrb	r1, [r4, #2776]	@ zero_extendqisi2
	ldr	r0, .L791+8
	bl	printk
	ldrb	r1, [r4, #2777]	@ zero_extendqisi2
	ldr	r0, .L791+12
	bl	printk
	ldrb	r1, [r4, #2778]	@ zero_extendqisi2
	ldr	r0, .L791+16
	bl	printk
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	ldr	r0, .L791+20
	bl	printk
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	ldr	r0, .L791+24
	bl	printk
	ldr	r4, .L791+28
	add	r5, r4, #1536
.L788:
	ldrh	r0, [r4, #34]
	add	r4, r4, #48
	ldrb	r1, [r4, #-47]	@ zero_extendqisi2
	ldrb	r2, [r4, #-48]	@ zero_extendqisi2
	str	r0, [sp]
	ldrb	r0, [r4, #-6]	@ zero_extendqisi2
	ldrb	r3, [r4, #-46]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldr	r0, [r4, #-12]
	str	r0, [sp, #8]
	ldr	r0, [r4, #-28]
	str	r0, [sp, #12]
	ldr	r0, [r4, #-24]
	str	r0, [sp, #16]
	ldr	r0, .L791+32
	bl	printk
	cmp	r4, r5
	bne	.L788
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L792:
	.align	2
.L791:
	.word	.LANCHOR0
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	.LANCHOR0+1236
	.word	.LC53
	.fnend
	.size	dump_buf_info, .-dump_buf_info
	.align	2
	.global	flash_check_bad_block
	.type	flash_check_bad_block, %function
flash_check_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L794
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L794+4
	ldr	r2, [r2, #1176]
	uxth	r3, r3
	add	r3, r3, #31
	mov	r3, r3, asr #5
	mov	r3, r3, asl #2
	uxth	r3, r3
	mul	r0, r3, r0
	add	r3, r0, #912
	mov	r0, r1, lsr #5
	and	r1, r1, #31
	add	r2, r2, r0, asl #2
	ldr	r0, [r2, r3]
	mov	r0, r0, lsr r1
	and	r0, r0, #1
	bx	lr
.L795:
	.align	2
.L794:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_check_bad_block, .-flash_check_bad_block
	.align	2
	.global	flash_mask_bad_block
	.type	flash_mask_bad_block, %function
flash_mask_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L798
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	mov	r6, r0
	ldrh	r4, [r3, #18]
	mov	r1, r6
	ldr	r0, .L798+4
	smulbb	r4, r2, r4
	mov	r2, r5
	bl	printk
	ldr	r3, .L798+8
	mov	r2, r5, lsr #5
	mov	r1, #1
	and	r5, r5, #31
	ldr	r3, [r3, #1176]
	uxth	r4, r4
	add	r4, r4, #31
	mov	r4, r4, asr #5
	mov	r4, r4, asl #2
	uxth	r4, r4
	mul	r4, r4, r6
	add	r4, r4, #912
	add	r4, r3, r4
	ldr	r3, [r4, r2, asl #2]
	orr	r5, r3, r1, asl r5
	str	r5, [r4, r2, asl #2]
	ldmfd	sp!, {r4, r5, r6, pc}
.L799:
	.align	2
.L798:
	.word	.LANCHOR2
	.word	.LC54
	.word	.LANCHOR0
	.fnend
	.size	flash_mask_bad_block, .-flash_mask_bad_block
	.align	2
	.global	str2hex
	.type	str2hex, %function
str2hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #48
	bne	.L801
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #2
.L801:
	ldrb	r3, [r0]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #1
	sub	r3, r0, #1
	mov	r0, #0
.L803:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L812
	sub	r1, r2, #48
	uxtb	ip, r1
	cmp	ip, #9
	addls	r0, r1, r0, asl #4
	bls	.L803
	sub	r1, r2, #97
	cmp	r1, #5
	subls	r2, r2, #87
	bls	.L811
	sub	r1, r2, #65
	cmp	r1, #5
	bxhi	lr
	sub	r2, r2, #55
.L811:
	add	r0, r2, r0, asl #4
	b	.L803
.L812:
	bx	lr
	.fnend
	.size	str2hex, .-str2hex
	.align	2
	.global	zftl_proc_debug_init
	.type	zftl_proc_debug_init, %function
zftl_proc_debug_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, lr}
	.save {lr}
	.pad #12
	mov	r2, #0
	mov	r1, #292
	str	r2, [sp]
	ldr	r3, .L815
	ldr	r0, .L815+4
	bl	proc_create_data
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L816:
	.align	2
.L815:
	.word	.LANCHOR1+1448
	.word	.LC55
	.fnend
	.size	zftl_proc_debug_init, .-zftl_proc_debug_init
	.align	2
	.global	ftl_print_info_to_buf
	.type	ftl_print_info_to_buf, %function
ftl_print_info_to_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	mov	r5, r0
	ldr	r1, .L819
	ldr	r2, .L819+4
	bl	sprintf
	ldr	r1, .L819+8
	ldr	r6, .L819+12
	ldrb	r3, [r1, #6]	@ zero_extendqisi2
	ldrb	r2, [r1, #5]	@ zero_extendqisi2
	add	r4, r5, r0
	ldrb	r0, [r1, #7]	@ zero_extendqisi2
	str	r0, [sp]
	ldrb	r0, [r1, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldrb	r0, [r1, #9]	@ zero_extendqisi2
	str	r0, [sp, #8]
	mov	r0, r4
	ldrb	r1, [r1, #10]	@ zero_extendqisi2
	str	r1, [sp, #12]
	ldr	r1, .L819+16
	bl	sprintf
	ldr	r2, [r6, #1032]
	ldr	r1, .L819+20
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r6, #2780]
	ldr	r1, .L819+24
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L819+28
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r2, [r6, #2784]
	mov	r0, r4
	ldr	r1, .L819+32
	bl	sprintf
	ldr	r2, [r6, #1032]
	ldr	r1, .L819+36
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r1, .L819+40
	ldr	r2, [r3, #524]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r1, .L819+44
	ldr	r2, [r3, #528]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2788
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+48
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2790
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+52
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2792
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+56
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2794
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+60
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2796
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+64
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2798
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+68
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r1, [r6, #2801]	@ zero_extendqisi2
	ldrb	r2, [r6, #2774]	@ zero_extendqisi2
	ldrb	r3, [r6, #2800]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L819+72
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+76
	ldrh	r2, [r3, #148]
	ldrh	r3, [r3, #146]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+80
	ldr	r2, [r3, #16]
	ldr	r3, [r3, #20]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+84
	ldr	r2, [r3, #24]
	ldr	r3, [r3, #28]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+88
	ldr	r2, [r3, #64]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+92
	ldr	r2, [r3, #68]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r2, .L819+96
	ldr	r1, .L819+100
	ldr	r3, [r3, #12]
	umull	r2, r3, r3, r2
	mov	r2, r3, lsr #3
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldrb	r2, [r6]	@ zero_extendqisi2
	ldrh	r1, [r3, #150]
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L819+104
	mov	r0, r4
	ldr	r3, [r3, #156]
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r1, .L819+108
	ldr	r2, [r3, #556]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r1, .L819+112
	ldr	r2, [r3, #552]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+116
	ldr	r2, [r3, #52]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+120
	ldr	r2, [r3, #60]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+124
	ldr	r2, [r3, #76]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L819+128
	ldr	r2, [r3, #8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1092]
	ldrh	r2, [r1, #16]
	ldrh	r3, [r1, #22]
	ldrb	r1, [r1, #25]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L819+132
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1092]
	ldrh	r2, [r1, #48]
	ldrh	r3, [r1, #54]
	ldrb	r1, [r1, #57]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L819+136
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1092]
	ldrh	r2, [r1, #80]
	ldrh	r3, [r1, #86]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L819+140
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldrh	r1, [r3, #88]
	ldrh	r2, [r3, #74]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #92]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #96]
	str	r1, [sp, #8]
	ldr	r1, .L819+144
	ldr	r3, [r3, #84]
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldrh	r1, [r3, #90]
	ldrh	r2, [r3, #72]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #94]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #98]
	str	r1, [sp, #8]
	ldr	r1, .L819+148
	ldr	r3, [r3, #80]
	bl	sprintf
	movw	r3, #2808
	ldrh	r2, [r6, r3]
	ldr	r1, .L819+152
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r1, #2814
	ldrh	r1, [r6, r1]
	movw	r3, #2810
	ldrh	r2, [r6, r3]
	movw	r3, #2812
	ldrh	r3, [r6, r3]
	str	r1, [sp]
	add	r1, r6, #2816
	add	r4, r4, r0
	ldrh	r1, [r1]
	mov	r0, r4
	str	r1, [sp, #4]
	ldr	r1, [r6, #2820]
	str	r1, [sp, #8]
	ldr	r1, .L819+156
	bl	sprintf
	ldr	r1, [r6, #1092]
	add	r3, r1, #584
	ldrh	r2, [r3]
	movw	r3, #586
	ldrh	r3, [r1, r3]
	add	r4, r4, r0
	add	r0, r1, #588
	ldrh	r0, [r0]
	str	r0, [sp]
	movw	r0, #590
	ldrh	r1, [r1, r0]
	mov	r0, r4
	str	r1, [sp, #4]
	ldr	r1, .L819+160
	bl	sprintf
	ldr	r3, [r6, #1092]
	ldr	r1, .L819+164
	ldr	r2, [r3, #544]
	str	r2, [sp]
	add	r4, r4, r0
	ldr	r2, [r6, #2804]
	mov	r0, r4
	ldr	r2, [r2, #44]
	str	r2, [sp, #4]
	ldr	r2, [r3, #548]
	str	r2, [sp, #8]
	ldr	r2, [r3, #536]
	ldr	r3, [r3, #540]
	bl	sprintf
	add	r0, r4, r0
	rsb	r0, r5, r0
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L820:
	.align	2
.L819:
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	.word	.LC74
	.word	.LC75
	.word	-858993459
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
	.word	.LC90
	.word	.LC91
	.word	.LC92
	.fnend
	.size	ftl_print_info_to_buf, .-ftl_print_info_to_buf
	.align	2
	.global	zftl_proc_ftl_read
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
	.type	ftl_gc_write_buf, %function
ftl_gc_write_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r1, r0
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	ldr	r4, .L824
	orr	r3, r3, #2
	strb	r3, [r0, #2]
	add	r0, r4, #2816
	add	r0, r0, #8
	bl	buf_add_tail
	ldrb	r0, [r4, #2800]	@ zero_extendqisi2
	add	r0, r0, #1
	uxtb	r0, r0
	strb	r0, [r4, #2800]
	ldmfd	sp!, {r4, pc}
.L825:
	.align	2
.L824:
	.word	.LANCHOR0
	.fnend
	.size	ftl_gc_write_buf, .-ftl_gc_write_buf
	.align	2
	.global	gc_hook
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
	.type	gc_add_sblk, %function
gc_add_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r5, r0
	ldr	r8, .L866
	mov	r4, r1
	mov	r9, r2
	ldr	r7, .L866+4
	ldr	r3, [r8]
	tst	r3, #256
	beq	.L830
	ldr	r3, [r7, #1080]
	mov	r1, r5
	ldr	r2, [r7, #1088]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r0, asl #1
	ldr	r0, .L866+8
	ldrh	r3, [r2, r3]
	mov	r2, r4
	str	r3, [sp, #4]
	ldr	r3, .L866+12
	ldrh	r3, [r3, #52]
	str	r3, [sp, #8]
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	str	r3, [sp, #12]
	mov	r3, r9
	bl	printk
.L830:
	movw	r3, #1076
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L831
	ldr	r1, .L866+16
	movw	r2, #543
	ldr	r0, .L866+20
	bl	printk
	bl	dump_stack
.L831:
	movw	r3, #1076
	ldr	r1, .L866+4
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L832
	ldr	r0, [r8]
	ands	r0, r0, #1024
	beq	.L833
	mov	r1, r5
	mov	r2, r4
	mov	r3, r9
	ldr	r0, .L866+24
	bl	printk
	b	.L852
.L832:
	ldr	r2, [r1, #1088]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	ldr	r2, [r1, #1080]
	add	r2, r2, r5, asl #2
	ldrb	ip, [r2, #2]	@ zero_extendqisi2
	ands	r6, ip, #224
	bne	.L834
	cmp	r3, #0
	beq	.L852
	ldr	r1, .L866+16
	movw	r2, #553
	ldr	r0, .L866+20
	bl	printk
	bl	dump_stack
	b	.L852
.L834:
	movw	r2, #2828
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	beq	.L852
	ldr	r2, [r1, #1092]
	ldrh	r0, [r2, #48]
	cmp	r0, r5
	beq	.L852
	ldrh	r0, [r2, #16]
	cmp	r0, r5
	beq	.L852
	ldrh	r0, [r2, #80]
	cmp	r0, r5
	beq	.L852
	ldr	r0, .L866+12
	add	r1, r1, #2880
	ldrh	lr, [r0, #52]
	mov	r0, #0
.L835:
	cmp	r0, lr
	bcs	.L865
	ldrh	r6, [r1, #2]!
	cmp	r6, r5
	beq	.L852
	add	r0, r0, #1
	b	.L835
.L865:
	cmp	r4, #0
	bne	.L840
	ldr	r0, .L866+28
	ldrh	r6, [r0]
	cmp	r6, r5
	moveq	r0, r4
	beq	.L833
	add	r0, r0, #4
	mov	r1, r4
.L839:
	ldrh	r10, [r1, r0]
	cmp	r5, r10
	bne	.L838
	ldr	r0, [r8]
	ands	r0, r0, #256
	beq	.L833
	stmia	sp, {r5, r6}
	mov	r1, r5
	mov	r2, ip, lsr #5
	ldr	r0, .L866+32
	bl	printk
	b	.L852
.L838:
	add	r1, r1, #2
	cmp	r1, #16
	bne	.L839
	add	r6, r2, r9, asl #7
	add	r6, r6, #136
	b	.L841
.L840:
	add	r6, r2, #392
.L841:
	ldr	r2, [r8]
	tst	r2, #256
	beq	.L842
	stmia	sp, {r3, lr}
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	mov	r1, r5
	ldr	r0, .L866+36
	mov	r2, r4
	str	r3, [sp, #8]
	mov	r3, ip, lsr #5
	bl	printk
.L842:
	add	r1, r6, #128
	mov	r3, r6
	movw	r0, #65535
.L845:
	mov	r2, r3
	add	r3, r3, #2
	ldrh	ip, [r2]
	cmp	ip, r0
	bne	.L843
	cmp	r4, #0
	strh	r5, [r2]	@ movhi
	ldreq	r3, [r7, #1092]
	ldrne	r2, [r7, #1092]
	addeq	r9, r3, r9, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r3, [r9, #120]
	addne	r3, r3, #1
	addeq	r3, r3, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r3, [r9, #120]	@ movhi
	b	.L864
.L843:
	cmp	r3, r1
	bne	.L845
.L864:
	mov	r0, #1
	b	.L833
.L852:
	mov	r0, #0
.L833:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L867:
	.align	2
.L866:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC93
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+1608
	.word	.LC0
	.word	.LC94
	.word	.LANCHOR3-3152
	.word	.LC95
	.word	.LC96
	.fnend
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L874
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	sub	r2, r3, #3120
	ldr	r5, .L874+4
	mov	r6, #1
	ldrh	r1, [r2, #-12]
	mov	r7, r0
	add	r5, r5, #4096
	ldrb	r2, [r5, #-2907]	@ zero_extendqisi2
	mov	r0, r0, lsr r1
	rsb	r2, r2, #24
	uxth	r4, r0
	rsb	r2, r1, r2
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	mov	r2, r6, asl r2
	sub	r2, r2, #1
	and	r0, r0, r2
	bl	__aeabi_uidiv
	mov	r3, r7
	ldr	r1, [r5, #920]
	mov	r2, r4
	uxth	r8, r0
	ldr	r0, .L874+8
	bl	printk
	mov	r1, r6
	mov	r0, r8
	mov	r2, #0
	bl	gc_add_sblk
	ldr	r3, [r5, #920]
	ldr	r0, .L874+12
	mov	r1, #0
.L869:
	uxth	r2, r1
	cmp	r2, r3
	bcs	.L873
	add	r2, r2, #1088
	add	r1, r1, #1
	add	r2, r2, #8
	mov	r2, r2, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r4
	bne	.L869
	b	.L870
.L873:
	cmp	r3, #5
	bhi	.L870
	add	r2, r3, #1
	add	r3, r3, #1088
	add	r3, r3, #8
	str	r2, [r5, #920]
	ldr	r2, .L874+12
	mov	r3, r3, asl #1
	strh	r4, [r2, r3]	@ movhi
.L870:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L875:
	.align	2
.L874:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC97
	.word	.LANCHOR0+2828
	.fnend
	.size	gc_mark_bad_ppa, .-gc_mark_bad_ppa
	.align	2
	.global	gc_get_src_ppa_from_index
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L877
	ldr	r3, [r3, #-3128]
	ldr	r0, [r3, r0, asl #2]
	bx	lr
.L878:
	.align	2
.L877:
	.word	.LANCHOR3
	.fnend
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.type	gc_write_completed, %function
gc_write_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L910
	mov	r6, r5
.L880:
	ldrb	fp, [r5, #2777]	@ zero_extendqisi2
	cmp	fp, #255
	beq	.L909
	mov	r3, #48
	mul	r3, r3, fp
	add	r2, r5, r3
	ldrb	r1, [r2, #1236]	@ zero_extendqisi2
	strb	r1, [r5, #2777]
	ldr	r1, .L910+4
	add	r3, r1, r3
	ldr	r1, [r2, #1272]
	cmp	r1, #0
	ldrh	r8, [r3, #32]
	beq	.L881
	ldr	r3, .L910+8
	mov	r0, #1
	ldr	r2, [r2, #1260]
	strh	r0, [r3]	@ movhi
	ldr	r3, .L910+12
	ldr	r0, .L910+16
	str	r2, [r3, #916]
	bl	printk
	ldr	r1, .L910+20
	mov	r2, #956
	ldr	r0, .L910+24
	bl	printk
	bl	dump_stack
.L881:
	ldr	r7, .L910+28
	ldrb	r3, [r7, #-3124]	@ zero_extendqisi2
	mov	r10, r7
	cmp	r3, #3
	bne	.L882
	ldrb	r3, [r6, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L882
	ldr	r3, [r5, #1092]
	ldrb	r2, [r7, #-3123]	@ zero_extendqisi2
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	cmp	r2, r3
	movhi	r3, #1
	bhi	.L884
	cmp	r3, #3
	movcs	r3, #2
.L884:
	add	r4, r3, r3, asl #1
	b	.L885
.L882:
	ldrb	r3, [r7, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L885
	ldr	r3, .L910+28
	ldrb	r3, [r3, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
.L885:
	mov	r2, #48
	ldr	r1, [r7, #-3120]
	mla	r2, r2, fp, r6
	mov	r9, r8
	ldrb	r1, [r1, r8]	@ zero_extendqisi2
	ldrb	r2, [r2, #1237]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L886
	ldr	r1, .L910+20
	mov	r2, #976
	ldr	r0, .L910+24
	bl	printk
	bl	dump_stack
.L886:
	mov	r2, #48
	mla	fp, r2, fp, r6
	ldrb	r2, [fp, #1281]	@ zero_extendqisi2
	cmp	r2, #3
	beq	.L887
	ldrb	r1, [r7, #-3124]	@ zero_extendqisi2
	ldr	r2, .L910+28
	cmp	r1, #3
	bne	.L887
	ldrb	r1, [r2, #-3116]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L887
	ldrb	r1, [r5, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L887
	ldrb	r1, [r5, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L887
	ldrb	r1, [r2, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L888
	ldrb	r2, [r2, #-3121]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L887
.L888:
	ldr	r2, [r7, #-3120]
	mov	r3, #0
	mov	r1, #48
	mov	r0, r3
	add	ip, r2, r8
	b	.L889
.L887:
	ldr	r2, .L910+32
	mvn	r7, #0
	mov	fp, #48
	strh	r8, [r2]	@ movhi
	mov	r2, #0
.L890:
	rsb	r1, r8, r9
	uxth	r1, r1
	cmp	r1, r4
	bcs	.L880
	ldr	r1, [r10, #-3120]
	ldr	r3, .L910+4
	str	r2, [sp, #4]
	ldrb	r0, [r1, r9]	@ zero_extendqisi2
	strb	r7, [r1, r9]
	add	r9, r9, #1
	mul	r0, fp, r0
	add	r1, r6, r0
	add	r0, r3, r0
	strb	r2, [r1, #1281]
	bl	zbuf_free
	ldrb	r1, [r6, #2835]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r6, #2835]
	ldr	r2, [sp, #4]
	b	.L890
.L889:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L880
	ldrb	r2, [ip, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	mla	r2, r1, r2, r6
	strb	r0, [r2, #1281]
	b	.L889
.L909:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L911:
	.align	2
.L910:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+5010
	.word	.LANCHOR0+4096
	.word	.LC98
	.word	.LANCHOR1+1620
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR0+4932
	.fnend
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L922
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r1, #1092]
	ldr	ip, .L922+4
	ldrh	r2, [r3, #124]
	ldrb	r0, [ip, #-3115]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r3, r3, #392
	movne	r2, #1
	bne	.L914
	add	lr, r3, r0, asl #1
	ldrh	lr, [lr, #120]
	cmp	lr, #0
	beq	.L919
	add	r3, r3, r0, asl #7
	add	r3, r3, #136
.L914:
	add	lr, r3, #128
	movw	r4, #65535
.L918:
	mov	r5, r3
	ldrh	r0, [r3], #2
	cmp	r0, r4
	beq	.L916
	cmp	r2, #0
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	ldreqb	r2, [ip, #-3115]	@ zero_extendqisi2
	ldreq	r3, [r1, #1092]
	ldrne	r2, [r1, #1092]
	addeq	r3, r3, r2, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r2, [r3, #120]
	subne	r3, r3, #1
	subeq	r2, r2, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r2, [r3, #120]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L916:
	cmp	r3, lr
	bne	.L918
	ldmfd	sp!, {r4, r5, pc}
.L919:
	movw	r0, #65535
	ldmfd	sp!, {r4, r5, pc}
.L923:
	.align	2
.L922:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_get_src_blk, .-gc_get_src_blk
	.align	2
	.global	gc_free_temp_buf
	.type	gc_free_temp_buf, %function
gc_free_temp_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L939
	ldrb	r0, [r5, #2835]	@ zero_extendqisi2
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r5, #2774]	@ zero_extendqisi2
	cmp	r3, #1
	bhi	.L931
	ldr	r2, .L939+4
	movw	r3, #2104
	ldr	r1, .L939+8
	ldrh	r4, [r2, r3]
	sub	r3, r1, #3104
	mov	r6, r1
	ldrh	r0, [r3, #-10]
	add	r2, r4, #24
	ldrb	r3, [r1, #-3123]	@ zero_extendqisi2
	mul	r3, r3, r0
	ldr	r0, [r1, #-3120]
	mov	r1, #48
	cmp	r3, r2
	movcs	r3, r2
.L926:
	cmp	r4, r3
	bcs	.L931
	ldrb	r2, [r0, r4]	@ zero_extendqisi2
	cmp	r2, #255
	beq	.L927
	mul	r2, r1, r2
	add	ip, r5, r2
	ldrb	ip, [ip, #1281]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L927
	ldr	r0, .L939+12
	add	r0, r0, r2
	bl	zbuf_free
	ldr	r3, .L939+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L928
	ldr	r3, [r6, #-3120]
	mov	r1, r4
	ldr	r0, .L939+20
	ldrb	r2, [r3, r4]	@ zero_extendqisi2
	bl	printk
.L928:
	ldr	r3, [r6, #-3120]
	mvn	r2, #0
	mov	r0, #1
	strb	r2, [r3, r4]
	ldrb	r3, [r5, #2835]	@ zero_extendqisi2
	add	r3, r3, r2
	strb	r3, [r5, #2835]
	ldmfd	sp!, {r4, r5, r6, pc}
.L927:
	add	r4, r4, #1
	b	.L926
.L931:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L940:
	.align	2
.L939:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR2
	.word	.LC99
	.fnend
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	get_ink_scaned_blk
	.type	get_ink_scaned_blk, %function
get_ink_scaned_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L944
	movw	ip, #2108
	ldrh	r3, [r1, ip]
	cmp	r3, #0
	addne	r2, r3, #1040
	subne	r3, r3, #1
	addne	r2, r2, #11
	strneh	r3, [r1, ip]	@ movhi
	addne	r2, r1, r2, asl #1
	ldrneh	r0, [r2, #6]
	movweq	r0, #65535
	bx	lr
.L945:
	.align	2
.L944:
	.word	.LANCHOR0+2828
	.fnend
	.size	get_ink_scaned_blk, .-get_ink_scaned_blk
	.align	2
	.global	print_gc_debug_info
	.type	print_gc_debug_info, %function
print_gc_debug_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, lr}
	.save {lr}
	.pad #20
	movw	r2, #2828
	ldr	r0, .L948
	movw	ip, #2102
	add	r3, r0, r2
	ldrh	r1, [r0, r2]
	ldrh	r2, [r3, #2]
	ldrh	r3, [r3, ip]
	ldrb	ip, [r0, #2774]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #2835]	@ zero_extendqisi2
	str	ip, [sp, #4]
	movw	ip, #2808
	ldrh	r0, [r0, ip]
	str	r0, [sp, #8]
	ldr	r0, .L948+4
	bl	printk
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L949:
	.align	2
.L948:
	.word	.LANCHOR0
	.word	.LC100
	.fnend
	.size	print_gc_debug_info, .-print_gc_debug_info
	.align	2
	.global	_list_pop_index_node
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, [r0]
	cmp	r5, #0
	beq	.L956
	ldr	r3, .L958
	movw	ip, #65535
	mov	lr, #6
	ldr	r4, [r3, #1036]
.L952:
	cmp	r1, #0
	bne	.L953
.L955:
	rsb	r4, r4, r5
	ldr	r3, .L958+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	_list_remove_node
	mvn	r3, #0
	mov	r0, r4
	strh	r3, [r5]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L953:
	ldrh	r3, [r5]
	cmp	r3, ip
	beq	.L955
	sub	r1, r1, #1
	mla	r5, lr, r3, r4
	uxth	r1, r1
	b	.L952
.L956:
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, pc}
.L959:
	.align	2
.L958:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_pop_index_node, .-_list_pop_index_node
	.align	2
	.global	_list_get_gc_head_node
	.type	_list_get_gc_head_node, %function
_list_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, [r0]
	cmp	r0, #0
	beq	.L965
	ldr	r3, .L968
	mov	ip, #6
	ldr	r2, [r3, #1036]
	movw	r3, #65535
.L962:
	cmp	r1, #0
	beq	.L963
	ldrh	r0, [r0]
	cmp	r0, r3
	subne	r1, r1, #1
	mlane	r0, ip, r0, r2
	uxthne	r1, r1
	bne	.L962
.L967:
	bx	lr
.L965:
	movw	r0, #65535
	bx	lr
.L963:
	rsb	r0, r2, r0
	ldr	r3, .L968+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	uxth	r0, r0
	bx	lr
.L969:
	.align	2
.L968:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_get_gc_head_node, .-_list_get_gc_head_node
	.align	2
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L1093
	str	r0, [sp, #8]
	str	r1, [sp, #16]
	ldr	r7, [r6, #1092]
	add	r3, r7, r0, asl #1
	ldrh	r4, [r3, #120]
	cmp	r4, #0
	bne	.L1025
	ldr	r3, .L1093+4
	mov	r5, r2
	ldrh	r2, [r3, #52]
	cmp	r2, #1
	ldrls	r1, .L1093+8
	strlsh	r4, [r1, #-8]	@ movhi
	strlsh	r4, [r1, #-6]	@ movhi
	strlsh	r4, [r1, #-4]	@ movhi
	ldr	r1, [sp, #8]
	cmp	r1, #0
	bne	.L1092
	ldr	r8, [sp, #8]
	ldr	r10, .L1093+12
	ldr	fp, .L1093+16
	mov	r4, r8
.L974:
	add	r9, r5, #1
	uxth	r3, r8
	cmp	r3, r9
	bge	.L980
	ldrh	r2, [r10]
	ldr	ip, .L1093+12
	mov	r1, r2
	add	r0, ip, #6
	str	ip, [sp, #20]
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r2, [sp, #12]
	cmp	r0, r3
	mov	r7, r0
	ldr	ip, [sp, #20]
	add	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r10]	@ movhi
	beq	.L976
	ldr	r3, [fp]
	mov	ip, r0, asl #1
	tst	r3, #256
	beq	.L977
	ldr	r3, [r6, #1088]
	mov	r1, r7
	ldr	r0, .L1093+20
	str	ip, [sp, #12]
	ldrh	r3, [r3, ip]
	bl	printk
	ldr	ip, [sp, #12]
.L977:
	ldr	r3, [r6, #1088]
	ldrh	r2, [r3, ip]
	ldr	r3, .L1093+24
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L978
	mov	r1, #0
	mov	r0, r7
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L979
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	bcc	.L979
	b	.L980
.L978:
	ldr	r3, .L1093+8
	mov	r2, #0
	strh	r2, [r3, #-6]	@ movhi
	b	.L980
.L976:
	mov	r3, #0
	strh	r3, [ip]	@ movhi
	b	.L980
.L979:
	add	r8, r8, #1
	b	.L974
.L980:
	ldr	r3, [sp, #16]
	tst	r3, #2
	beq	.L982
	movw	r3, #2798
	ldrh	r3, [r6, r3]
	cmp	r3, #32
	movhi	r10, #0
	ldrhi	r8, .L1093+28
	bls	.L982
.L983:
	uxth	r2, r10
	ldr	r7, .L1093+32
	cmp	r2, r9
	bge	.L987
	ldrh	fp, [r8]
	ldr	r2, .L1093+28
	mov	r1, fp
	add	r0, r2, #8
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	movw	ip, #65535
	add	fp, fp, #1
	strh	fp, [r8]	@ movhi
	cmp	r0, ip
	ldr	r2, [sp, #12]
	beq	.L984
	ldr	fp, [r6, #1088]
	mov	r1, r0, asl #1
	ldr	r3, .L1093+32
	ldrh	ip, [fp, r1]
	ldrh	r1, [r3]
	cmp	ip, r1
	bcs	.L984
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L986
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	bcc	.L986
	b	.L987
.L984:
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L987
.L986:
	add	r10, r10, #1
	b	.L983
.L987:
	cmp	r4, r5
	ldr	r2, .L1093+36
	bcs	.L989
	ldrh	ip, [r2, #-26]
	ldrb	r3, [r2, #-35]	@ zero_extendqisi2
	ldrh	r0, [r2, #-8]
	ldrh	r1, [r7]
	mul	r3, r3, ip
	sub	r3, r3, r0, lsr #2
	cmp	r1, r3
	ldrlt	r3, .L1093+32
	addlt	r1, r1, r0, lsr #3
	strlth	r1, [r3]	@ movhi
	b	.L982
.L989:
	ldrh	r2, [r2, #-8]
	ldrh	r3, [r7]
	cmp	r3, r2
	subhi	r3, r3, r2, lsr #3
	ldrhi	r2, .L1093+32
	strhih	r3, [r2]	@ movhi
.L982:
	ldr	r3, [sp, #16]
	tst	r3, #1
	beq	.L990
	uxth	r7, r5
	cmp	r4, r7
	bcs	.L990
	ldr	r8, .L1093+40
	mov	fp, #0
	movw	r9, #65535
.L995:
	ldrh	r10, [r8]
	ldr	r3, .L1093+40
	mov	r1, r10
	add	r0, r3, #20
	str	r3, [sp, #12]
	bl	_list_get_gc_head_node
	add	r10, r10, #1
	strh	r10, [r8]	@ movhi
	cmp	r0, r9
	ldr	r3, [sp, #12]
	beq	.L991
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L992
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r7
	bcc	.L992
	b	.L993
.L991:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L994
.L992:
	add	fp, fp, #1
	uxth	r3, fp
	cmp	r3, r7
	bcc	.L995
.L994:
	cmp	r4, r7
	bcs	.L993
	ldr	r1, .L1093+36
	movw	r2, #2814
	ldrh	r3, [r6, r2]
	ldrh	r1, [r1, #-8]
	cmp	r3, r1
	addcc	r3, r3, r1, lsr #3
	bcc	.L1091
	b	.L990
.L993:
	ldr	r1, .L1093+36
	movw	r2, #2814
	ldrh	r3, [r6, r2]
	ldrh	r1, [r1, #-8]
	cmp	r3, r1, lsr #1
	bls	.L990
	sub	r3, r3, r1, lsr #3
.L1091:
	ldr	r1, .L1093
	b	.L1090
.L1092:
	ldr	r8, .L1093+44
	cmp	r5, #1
	sub	r8, r8, #3104
	ldrb	r0, [r8, #-19]	@ zero_extendqisi2
	ldrh	r1, [r8, #-10]
	smulbb	r1, r0, r1
	uxth	r1, r1
	str	r1, [sp, #12]
	bne	.L996
	cmp	r2, #0
	beq	.L996
	ldrh	r0, [r7, #80]
	movw	r1, #65535
	mov	r5, #8
	cmp	r0, r1
	movwne	r1, #2102
	ldrne	r0, [sp, #12]
	ldrneh	r1, [r3, r1]
	rsbne	r1, r1, r0
	uxthne	r1, r1
	strne	r1, [sp, #12]
.L996:
	movw	r0, #2180
	mov	r7, #0
	strh	r7, [r3, r0]	@ movhi
	ldr	r3, [sp, #16]
	ldr	r4, .L1093+36
	ands	r3, r3, #1
	str	r3, [sp, #20]
	ldr	r1, .L1093
	strh	r7, [r4]	@ movhi
	ldreq	r7, [sp, #20]
	moveq	r4, r7
	beq	.L997
	ldrh	r0, [r4, #2]
	movw	r3, #2794
	ldrh	r3, [r1, r3]
	add	r4, r4, #16
	str	r4, [sp, #24]
	cmp	r3, r0, lsr #2
	bhi	.L998
	movw	r0, #2796
	ldrh	r1, [r1, r0]
	cmp	r1, r3
	movcs	r4, r7
	bcs	.L999
.L998:
	cmp	r2, #1
	bls	.L1000
.L1002:
	mov	r4, #0
	b	.L1001
.L1000:
	ldr	r4, .L1093+8
	mov	r1, #0
	mov	r3, r3, lsr #2
	strh	r3, [r8, #-8]	@ movhi
	add	r0, r4, #12
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1002
	ldr	r1, [r6, #1088]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, #8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #2
	bcs	.L1002
	mov	r1, #0
	ldr	r2, [sp, #8]
	mov	r3, #1
	strh	r3, [r4, #-8]	@ movhi
	bl	gc_add_sblk
	adds	r4, r0, #0
	movne	r4, #1
.L1001:
	ldr	r9, .L1093+40
	mov	r8, #64
	mov	r7, #0
.L1005:
	ldrh	fp, [r9]
	ldr	r3, .L1093+40
	mov	r1, fp
	add	r0, r3, #20
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	mov	r10, r0
	ldr	r3, [sp, #28]
	beq	.L1003
	mov	r1, #0
	ldr	r2, [sp, #8]
	add	fp, fp, #1
	strh	fp, [r9]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1004
	ldr	r3, [r6, #1088]
	mov	r10, r10, asl #1
	add	r4, r4, #1
	ldrh	r3, [r3, r10]
	uxth	r4, r4
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcs	.L999
	ldr	r2, .L1093+48
	ldr	r3, .L1093+52
	ldrh	r2, [r2]
	ldrh	r3, [r3]
	cmp	r3, r2, asl #1
	ble	.L1004
	b	.L999
.L1003:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L999
.L1004:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1005
.L999:
	movw	r3, #2796
	ldrh	r2, [r6, r3]
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #-14]
	cmp	r2, r3, lsr #2
	bhi	.L1026
	ldr	r1, .L1093
	movw	r3, #2794
	ldrh	r3, [r1, r3]
	add	r3, r3, #8
	cmp	r2, r3
	bgt	.L1026
.L997:
	ldr	r3, [sp, #16]
	tst	r3, #2
	beq	.L1009
	movw	r3, #2798
	ldrh	r2, [r6, r3]
	ldr	r3, [sp, #12]
	cmp	r7, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r2, #32
	movls	r3, #0
	cmp	r3, #0
	beq	.L1009
	ldr	r9, .L1093+28
	mov	r8, #64
	mov	r6, #0
.L1015:
	ldrh	r3, [r9]
	ldr	r10, .L1093+28
	ldr	fp, .L1093
	add	r0, r10, #8
	mov	r1, r3
	str	r3, [sp, #24]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1010
	ldr	r3, [sp, #24]
	cmp	r5, #1
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	bne	.L1011
	ldr	r3, .L1093+44
	ldrb	r2, [r3, #-3123]	@ zero_extendqisi2
	ldrh	r3, [r10, #-6]
	smulbb	r3, r2, r3
	ldrh	r2, [r10, #12]
	sub	r3, r3, r2, lsr #3
	ldr	r2, .L1093+56
	strh	r3, [r2]	@ movhi
.L1011:
	ldr	r3, [fp, #1088]
	mov	r10, r0, asl #1
	ldrh	r2, [r3, r10]
	ldr	r3, .L1093+56
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L1012
	mov	r1, #0
	ldr	r2, [sp, #8]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1013
	ldr	r3, .L1093
	add	r4, r4, #1
	add	r6, r6, #1
	uxth	r4, r4
	ldr	r3, [r3, #1088]
	uxth	r6, r6
	ldrh	r3, [r3, r10]
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcc	.L1013
	b	.L1014
.L1012:
	ldr	r3, .L1093+8
	mov	r2, #0
	strh	r2, [r3, #-4]	@ movhi
	b	.L1014
.L1010:
	mov	r3, #0
	strh	r3, [r10]	@ movhi
	b	.L1014
.L1013:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1015
.L1014:
	ldr	r0, .L1093+44
	cmp	r4, r5
	sub	r2, r0, #3088
	bcc	.L1016
	cmp	r6, #0
	bne	.L1017
	ldr	ip, .L1093
	sub	r3, r0, #3072
	movw	r1, #2798
	ldrh	r3, [r3, #-12]
	ldrh	r1, [ip, r1]
	cmp	r1, r3
	bls	.L1017
.L1016:
	ldr	r3, .L1093+8
	movw	r1, #2810
	ldrh	r2, [r2, #-8]
	ldrh	ip, [fp, r1]
	ldrh	lr, [r3, #-10]
	ldrb	r3, [r0, #-3123]	@ zero_extendqisi2
	mov	r2, r2, lsr #3
	mul	r3, r3, lr
	rsb	r3, r2, r3
	cmp	ip, r3
	addlt	r3, r2, ip
	ldrlt	r2, .L1093
	strlth	r3, [r2, r1]	@ movhi
	b	.L1009
.L1017:
	ldrh	ip, [r2, #-8]
	movw	r1, #2810
	ldrb	r2, [r0, #-3123]	@ zero_extendqisi2
	ldrh	r3, [fp, r1]
	mul	r2, r2, ip
	cmp	r3, r2
	subgt	r3, r3, ip, lsr #3
	bgt	.L1089
	b	.L990
.L1009:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L990
	ldr	r3, [sp, #12]
	uxth	r9, r5
	cmp	r4, r9
	cmpcc	r7, r3
	bcs	.L990
	ldr	r10, .L1093
	mov	r6, #64
.L1023:
	ldr	r2, .L1093+12
	ldr	fp, .L1093
	add	r0, r2, #6
	ldrh	r8, [r2]
	str	r2, [sp, #20]
	mov	r1, r8
	bl	_list_get_gc_head_node
	movw	r1, #65535
	cmp	r0, r1
	ldr	r2, [sp, #20]
	beq	.L1019
	ldr	r3, .L1093+12
	add	r8, r8, #1
	ldr	r2, [r10, #1088]
	strh	r8, [r3]	@ movhi
	mov	r8, r0, asl #1
	ldrh	r1, [r2, r8]
	ldr	r2, .L1093+60
	ldrh	r2, [r2]
	cmp	r1, r2
	bcs	.L1020
	ldr	r2, .L1093+52
	ldrh	r1, [r3, #24]
	ldrh	r2, [r2]
	cmp	r2, r1, lsr #1
	bls	.L1021
.L1020:
	mov	r1, #0
	ldr	r2, [sp, #8]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L1022
	ldr	r2, [r10, #1088]
	add	r4, r4, #1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	ldrh	r2, [r2, r8]
	add	r7, r7, r2
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r9
	bcc	.L1022
	b	.L1021
.L1019:
	cmp	r8, #64
	movhi	r3, #0
	strhih	r3, [r2]	@ movhi
	b	.L1021
.L1022:
	sub	r6, r6, #1
	uxth	r6, r6
	cmp	r6, #0
	bne	.L1023
.L1021:
	cmp	r4, r9
	ldr	r0, .L1093+36
	movw	r1, #2812
	bcs	.L1024
	ldrh	r2, [r0, #-8]
	ldrh	r3, [fp, r1]
	cmp	r3, r2, lsr #1
	bls	.L990
	sub	r3, r3, r2, lsr #3
.L1089:
	ldr	r2, .L1093
.L1090:
	strh	r3, [r2, r1]	@ movhi
	b	.L990
.L1024:
	ldr	r3, .L1093+44
	ldrh	r0, [r0, #-8]
	ldrh	r2, [fp, r1]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	mul	r3, r3, r0
	sub	r3, r3, #32
	cmp	r2, r3
	addlt	r2, r2, r0, lsr #3
	ldrlt	r3, .L1093
	strlth	r2, [r3, r1]	@ movhi
.L990:
	ldr	r3, .L1093+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1025
	str	r5, [sp]
	mov	r3, r4
	ldr	r0, .L1093+64
	ldr	r1, [sp, #8]
	ldr	r2, [sp, #16]
	bl	printk
.L1025:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1026:
	ldr	r3, [sp, #12]
	cmp	r7, r3
	bcs	.L997
	ldr	r9, .L1093+12
	mov	r8, #64
	movw	r10, #65535
.L1008:
	ldrh	fp, [r9]
	ldr	r2, .L1093+12
	mov	r1, fp
	add	r0, r2, #6
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	cmp	r0, r10
	mov	r3, r0
	str	r0, [sp, #24]
	ldr	r2, [sp, #28]
	beq	.L1006
	mov	r1, #0
	ldr	r2, [sp, #8]
	add	fp, fp, #1
	strh	fp, [r9]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	r3, [sp, #24]
	beq	.L1007
	ldr	r2, [r6, #1088]
	mov	r3, r3, asl #1
	add	r4, r4, #1
	ldrh	r3, [r2, r3]
	uxth	r4, r4
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcc	.L1007
	b	.L997
.L1006:
	cmp	fp, #64
	movhi	r3, #0
	strhih	r3, [r2]	@ movhi
	b	.L997
.L1007:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L1008
	b	.L997
.L1094:
	.align	2
.L1093:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3-3104
	.word	.LANCHOR3-3110
	.word	.LANCHOR2
	.word	.LC101
	.word	.LANCHOR0+2814
	.word	.LANCHOR3-3108
	.word	.LANCHOR0+2816
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3112
	.word	.LANCHOR3
	.word	.LANCHOR0+2794
	.word	.LANCHOR0+2796
	.word	.LANCHOR0+2810
	.word	.LANCHOR0+2812
	.word	.LC102
	.fnend
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_get_gc_node
	.type	zftl_get_gc_node, %function
zftl_get_gc_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r1, #5
	mov	r3, r0
	ldreq	r0, .L1099
	beq	.L1098
	cmp	r1, #2
	ldreq	r0, .L1099+4
	ldrne	r0, .L1099+8
.L1098:
	mov	r1, r3
	b	_list_get_gc_head_node
.L1100:
	.align	2
.L1099:
	.word	.LANCHOR3-3100
	.word	.LANCHOR3-3092
	.word	.LANCHOR3-3104
	.fnend
	.size	zftl_get_gc_node, .-zftl_get_gc_node
	.align	2
	.global	zftl_insert_free_list
	.type	zftl_insert_free_list, %function
zftl_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1105
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1105+4
	ldreq	r2, .L1105+8
	beq	.L1104
.L1102:
	cmp	r3, #16
	ldreq	r0, .L1105+12
	ldreq	r2, .L1105+16
	ldrne	r0, .L1105+20
	ldrne	r2, .L1105+24
.L1104:
	b	_insert_free_list
.L1106:
	.align	2
.L1105:
	.word	.LANCHOR0
	.word	.LANCHOR3-3080
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3076
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.fnend
	.size	zftl_insert_free_list, .-zftl_insert_free_list
	.align	2
	.global	zftl_insert_data_list
	.type	zftl_insert_data_list, %function
zftl_insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1112
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1112+4
	ldreq	r2, .L1112+8
	beq	.L1111
.L1108:
	cmp	r3, #96
	ldreq	r0, .L1112+12
	ldreq	r2, .L1112+16
	beq	.L1111
.L1109:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1112+20
	ldr	r2, .L1112+24
.L1111:
	b	_insert_data_list
.L1113:
	.align	2
.L1112:
	.word	.LANCHOR0
	.word	.LANCHOR3-3092
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3100
	.word	.LANCHOR0+2798
	.fnend
	.size	zftl_insert_data_list, .-zftl_insert_data_list
	.align	2
	.global	zftl_gc_get_free_sblk
	.type	zftl_gc_get_free_sblk, %function
zftl_gc_get_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	movw	r2, #65535
	ldr	r8, .L1129
	mov	r7, r0
	ldr	r3, [r8, #1092]
	add	r3, r3, #588
	ldrh	r4, [r3]
	clz	r3, r0
	cmp	r4, r2
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L1115
	ldr	r0, .L1129+4
	mov	r1, r4
	bl	printk
	ldr	r3, [r8, #1092]
	mvn	r2, #0
	add	r3, r3, #588
	strh	r2, [r3]	@ movhi
	b	.L1116
.L1115:
	movw	r3, #2790
	mov	r6, r1
	ldrh	r1, [r8, r3]
	movw	r3, #2792
	ldrh	r3, [r8, r3]
	mov	r5, r8
	cmp	r1, r3
	bcc	.L1117
	movw	r2, #2788
	ldrh	r2, [r8, r2]
	cmp	r3, #0
	cmpne	r2, r1
	bls	.L1118
.L1117:
	cmp	r7, #0
	ldr	r0, .L1129+8
	rsbne	r1, r1, r1, asl #3
	moveq	r1, r3, lsr #2
	ubfxne	r1, r1, #3, #16
	ldr	r2, .L1129+12
	b	.L1128
.L1118:
	cmp	r0, #0
	ldr	r2, .L1129+16
	movne	r1, r1, lsr #3
	moveq	r1, r0
	ldr	r0, .L1129+20
.L1128:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1123
	movw	r3, #2788
	ldr	r0, .L1129+24
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r6
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1129+28
	ldr	r3, [r3, #-3080]
	bl	printk
.L1123:
	cmp	r7, #0
	beq	.L1116
	ldr	r3, .L1129+32
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1116
	ldr	r2, [r5, #1080]
	mov	r0, r4, asl #2
	add	r3, r2, r0
	ldr	r1, [r2, r4, asl #2]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #11, #8
	str	r1, [sp]
	ldrh	r2, [r2, r0]
	ldr	r0, .L1129+36
	ubfx	r2, r2, #0, #11
	str	r2, [sp, #4]
	ldr	r1, [r5, #1088]
	mov	r2, r4, asl #1
	ldrh	r2, [r1, r2]
	mov	r1, r4
	str	r2, [sp, #8]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
.L1116:
	mov	r0, r4
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1130:
	.align	2
.L1129:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3076
	.word	.LC104
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC105
	.fnend
	.size	zftl_gc_get_free_sblk, .-zftl_gc_get_free_sblk
	.align	2
	.global	zftl_get_free_sblk
	.type	zftl_get_free_sblk, %function
zftl_get_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #5
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r7, r1
	ldr	r5, .L1147
	bne	.L1132
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	movw	r3, #2792
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	bcc	.L1133
	movw	r3, #2788
	ldrh	r3, [r5, r3]
	cmp	r2, #0
	cmpne	r3, r1
	movhi	r1, #1
	movls	r1, #0
	bls	.L1134
.L1133:
	ldr	r0, .L1147+4
	mov	r1, r2, lsr #1
	b	.L1146
.L1134:
	ldr	r0, .L1147+8
	ldr	r2, .L1147+12
	b	.L1145
.L1132:
	ldr	r3, [r5, #1092]
	movw	r8, #590
	ldrh	r4, [r3, r8]
	movw	r3, #65535
	cmp	r1, #1
	cmpne	r4, r3
	beq	.L1136
	ldr	r0, .L1147+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1092]
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L1137
.L1136:
	movw	r3, #2788
	movw	r2, #2792
	ldrh	r3, [r5, r3]
	mov	r6, r0
	ldrh	r2, [r5, r2]
	cmp	r3, r2
	bcc	.L1138
	movw	r1, #2790
	ldrh	r1, [r5, r1]
	cmp	r2, #0
	cmpne	r1, r3
	bls	.L1139
.L1138:
	bl	get_ink_scaned_blk
	movw	r3, #65535
	cmp	r0, r3
	movne	r4, r0
	bne	.L1137
	cmp	r7, #1
	ldr	r0, .L1147+4
	movweq	r3, #2792
	ldreqh	r6, [r5, r3]
	moveq	r6, r6, lsr #1
	mov	r1, r6
.L1146:
	ldr	r2, .L1147+20
	b	.L1145
.L1139:
	cmp	r7, #1
	ldr	r0, .L1147+24
	moveq	r6, r3, lsr #1
	ldr	r2, .L1147+28
	mov	r1, r6
.L1145:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1137
	movw	r3, #2788
	ldr	r0, .L1147+32
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r7
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1147+36
	ldr	r3, [r3, #-3080]
	bl	printk
.L1137:
	mov	r0, r4
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1148:
	.align	2
.L1147:
	.word	.LANCHOR0
	.word	.LANCHOR3-3072
	.word	.LANCHOR3-3076
	.word	.LANCHOR0+2790
	.word	.LC106
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3080
	.word	.LANCHOR0+2788
	.word	.LC104
	.word	.LANCHOR3
	.fnend
	.size	zftl_get_free_sblk, .-zftl_get_free_sblk
	.align	2
	.global	zftl_remove_data_node
	.type	zftl_remove_data_node, %function
zftl_remove_data_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1154
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1154+4
	ldreq	r2, .L1154+8
	beq	.L1153
.L1150:
	cmp	r3, #96
	ldreq	r0, .L1154+12
	ldreq	r2, .L1154+16
	beq	.L1153
.L1151:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1154+20
	ldr	r2, .L1154+24
.L1153:
	b	_list_remove_node
.L1155:
	.align	2
.L1154:
	.word	.LANCHOR0
	.word	.LANCHOR3-3092
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3100
	.word	.LANCHOR0+2798
	.fnend
	.size	zftl_remove_data_node, .-zftl_remove_data_node
	.align	2
	.global	zftl_remove_free_node
	.type	zftl_remove_free_node, %function
zftl_remove_free_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1160
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1160+4
	ldreq	r2, .L1160+8
	beq	.L1159
.L1157:
	cmp	r3, #16
	ldreq	r0, .L1160+12
	ldreq	r2, .L1160+16
	ldrne	r0, .L1160+20
	ldrne	r2, .L1160+24
.L1159:
	b	_list_remove_node
.L1161:
	.align	2
.L1160:
	.word	.LANCHOR0
	.word	.LANCHOR3-3080
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3076
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.fnend
	.size	zftl_remove_free_node, .-zftl_remove_free_node
	.align	2
	.global	zftl_list_update_data_list
	.type	zftl_list_update_data_list, %function
zftl_list_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1167
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1167+4
	ldreq	r2, .L1167+8
	beq	.L1166
.L1163:
	cmp	r3, #96
	ldreq	r0, .L1167+12
	ldreq	r2, .L1167+16
	beq	.L1166
.L1164:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1167+20
	ldr	r2, .L1167+24
.L1166:
	b	_list_update_data_list
.L1168:
	.align	2
.L1167:
	.word	.LANCHOR0
	.word	.LANCHOR3-3092
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3100
	.word	.LANCHOR0+2798
	.fnend
	.size	zftl_list_update_data_list, .-zftl_list_update_data_list
	.align	2
	.global	print_list_info
	.type	print_list_info, %function
print_list_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrh	r2, [r1]
	.pad #36
	sub	sp, sp, #36
	ldr	r1, [r4]
	ldr	r0, .L1174
	bl	printk
	ldr	r4, [r4]
	cmp	r4, #0
	beq	.L1169
	ldr	r6, .L1174+4
	mov	r5, #0
	ldr	r7, .L1174+8
.L1172:
	ldr	r2, [r6, #1036]
	ldrh	r0, [r4, #2]
	rsb	r2, r2, r4
	ldr	r1, [r6, #1080]
	ldrh	r3, [r4]
	mov	r2, r2, asr #1
	str	r0, [sp]
	ldrh	r0, [r4, #4]
	mul	r2, r7, r2
	str	r0, [sp, #4]
	uxth	r2, r2
	mov	ip, r2, asl #2
	add	lr, r1, ip
	ldrb	r0, [lr, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	str	r0, [sp, #8]
	ldrb	r0, [lr, #2]	@ zero_extendqisi2
	ubfx	r0, r0, #3, #2
	str	r0, [sp, #12]
	ldr	r0, [r1, r2, asl #2]
	ubfx	r0, r0, #11, #8
	str	r0, [sp, #16]
	ldrh	r1, [r1, ip]
	ldr	r0, [r6, #1088]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #20]
	mov	r1, r2, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1174+12
	str	r1, [sp, #24]
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1169
	ldr	r3, [r6, #1036]
	mov	r4, #6
	add	r5, r5, #1
	mla	r4, r4, r2, r3
	ldr	r3, .L1174+16
	uxth	r5, r5
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L1172
.L1169:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1175:
	.align	2
.L1174:
	.word	.LC107
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC108
	.word	.LANCHOR3-3068
	.fnend
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.type	dump_all_list_info, %function
dump_all_list_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L1178
	ldr	r5, .L1178+4
	add	r1, r4, #2784
	sub	r6, r5, #3072
	add	r1, r1, #4
	sub	r0, r6, #8
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r6, #4
	add	r1, r1, #6
	bl	print_list_info
	add	r1, r4, #2784
	mov	r0, r6
	add	r1, r1, #8
	bl	print_list_info
	sub	r6, r5, #3088
	add	r1, r4, #2784
	sub	r0, r6, #4
	add	r1, r1, #10
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r5, #3104
	add	r1, r1, #12
	bl	print_list_info
	add	r1, r4, #2784
	sub	r0, r6, #12
	add	r1, r1, #14
	ldmfd	sp!, {r4, r5, r6, lr}
	b	print_list_info
.L1179:
	.align	2
.L1178:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1185
	ldr	r3, [r3, #2804]
	ldr	r2, [r3, #16]
	cmp	r2, #2048
	movhi	r0, r2, lsr #11
	ldrhi	r1, [r3, #20]
	subhi	r2, r2, r0, asl #11
	strhi	r2, [r3, #16]
	ldr	r2, [r3, #24]
	addhi	r1, r1, r0
	strhi	r1, [r3, #20]
	cmp	r2, #2048
	movhi	r0, r2, lsr #11
	ldrhi	r1, [r3, #28]
	subhi	r2, r2, r0, asl #11
	strhi	r2, [r3, #24]
	ldr	r2, [r3, #32]
	addhi	r1, r1, r0
	strhi	r1, [r3, #28]
	cmp	r2, #1024
	movhi	r0, r2, lsr #10
	ldrhi	r1, [r3, #36]
	subhi	r2, r2, r0, asl #10
	strhi	r2, [r3, #32]
	ldr	r2, [r3, #40]
	addhi	r1, r1, r0
	strhi	r1, [r3, #36]
	cmp	r2, #1024
	movhi	r0, r2, lsr #10
	ldrhi	r1, [r3, #44]
	subhi	r2, r2, r0, asl #10
	strhi	r2, [r3, #40]
	addhi	r1, r1, r0
	strhi	r1, [r3, #44]
	bx	lr
.L1186:
	.align	2
.L1185:
	.word	.LANCHOR0
	.fnend
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.global	__aeabi_idiv
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, #0
	ldr	r8, .L1197
	mov	r10, r0
	ldr	r9, .L1197+4
	mov	r6, r1
	ldr	r2, .L1197+8
	mov	r7, r5
	ldr	r4, [r8, #1080]
	add	r4, r4, r0, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
.L1188:
	ldrb	r1, [r9, #-3123]	@ zero_extendqisi2
	cmp	r7, r1
	ldr	r1, .L1197+4
	bge	.L1195
	mov	r1, r3, asr r7
	tst	r1, #1
	bne	.L1189
	ldrb	r4, [r9, #-3130]	@ zero_extendqisi2
	mov	r0, r7
	str	r2, [sp, #4]
	mov	fp, r5, asl #1
	str	r3, [sp]
	add	r5, r5, #1
	mov	r1, r4
	bl	__aeabi_idiv
	smulbb	r4, r10, r4
	ldrb	r1, [r8, #1189]	@ zero_extendqisi2
	rsb	r1, r1, #24
	ldr	r2, [sp, #4]
	ldr	r3, [sp]
	ldrh	ip, [r2]
	rsb	r1, ip, r1
	add	r0, r4, r0, asl r1
	uxth	r4, r0
	strh	r4, [r6, fp]	@ movhi
	ldrb	r1, [r9, #-3130]	@ zero_extendqisi2
	cmp	r1, #1
	subhi	r1, r1, #1
	andhi	r1, r1, r7
	addhi	r4, r4, r1
	strhih	r4, [r6, fp]	@ movhi
.L1189:
	add	r7, r7, #1
	b	.L1188
.L1195:
	add	r6, r6, r5, asl #1
	mov	r0, r5
	mvn	r2, #0
.L1192:
	ldrb	r3, [r1, #-3123]	@ zero_extendqisi2
	cmp	r5, r3
	strlth	r2, [r6], #2	@ movhi
	addlt	r5, r5, #1
	blt	.L1192
.L1196:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1198:
	.align	2
.L1197:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3132
	.fnend
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_erase_phy_blk
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r6, r1
	ldr	r3, .L1205
	ldrb	r4, [r3, #1189]	@ zero_extendqisi2
	ldr	r3, .L1205+4
	rsb	r4, r4, #24
	sub	r2, r3, #3120
	sub	r7, r3, #3056
	ldrh	r2, [r2, #-12]
	rsb	r4, r2, r4
	mvn	r2, #0
	mov	r5, r0, asr r4
	bic	r4, r0, r2, asl r4
	ldrb	r2, [r3, #-3122]	@ zero_extendqisi2
	uxtb	r5, r5
	sxth	r4, r4
	cmp	r2, #0
	beq	.L1200
	ldrb	r3, [r3, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1200
	ldrh	r2, [r7, #-10]
	clz	r1, r1
	mov	r0, r5
	mov	r1, r1, lsr #5
	mul	r2, r2, r4
	bl	flash_erase_block_en
.L1200:
	ldrh	r2, [r7, #-10]
	mov	r0, r5
	uxtb	r1, r6
	mul	r2, r2, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	flash_erase_block_en
.L1206:
	.align	2
.L1205:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	ftl_erase_phy_blk, .-ftl_erase_phy_blk
	.align	2
	.global	ftl_erase_sblk
	.type	ftl_erase_sblk, %function
ftl_erase_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r0, asl #2
	ldr	r4, .L1235
	.pad #84
	sub	sp, sp, #84
	ldr	r9, .L1235+4
	mov	r8, r0
	mov	r7, r1
	mov	r6, #0
	ldr	r3, [r4, #1080]
	add	r3, r3, r10
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L1208:
	ldrb	r3, [r9, #-3064]	@ zero_extendqisi2
	cmp	r6, r3
	bge	.L1219
	ldrb	r2, [r9, #-3130]	@ zero_extendqisi2
	mov	r5, #0
	sub	fp, r2, #1
	mul	r3, r2, r8
	mul	ip, r2, r6
	str	r3, [sp, #4]
	ldr	r3, .L1235+8
	ldrh	r3, [r3]
	str	r3, [sp, #8]
	mov	r3, r5
.L1220:
	cmp	r3, r2
	bge	.L1234
	ldr	lr, [sp, #12]
	add	r1, r3, ip
	mov	r1, lr, asr r1
	tst	r1, #1
	bne	.L1209
	add	r1, sp, #80
	ldr	r0, [sp, #4]
	add	lr, r1, r5, asl #2
	and	r1, r3, fp
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	add	r5, r5, #1
	mul	r1, r0, r1
	str	r1, [lr, #-64]
.L1209:
	add	r3, r3, #1
	b	.L1220
.L1234:
	cmp	r2, #4
	bne	.L1232
	uxtb	r3, r6
	uxtb	ip, r7
	mov	fp, #0
.L1211:
	cmp	fp, r5
	beq	.L1214
	mov	r0, r3
	str	r3, [sp, #8]
	add	r3, sp, #16
	mov	r1, ip
	str	ip, [sp, #4]
	ldr	r2, [r3, fp, asl #2]
	add	fp, fp, #1
	bl	flash_erase_block_en
	ldr	ip, [sp, #4]
	ldr	r3, [sp, #8]
	b	.L1211
.L1232:
	cmp	r5, #2
	bne	.L1215
	ldr	r3, .L1235+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1216
	ldrb	r3, [r9, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1216
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
.L1216:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
	b	.L1214
.L1215:
	cmp	r5, #1
	bne	.L1214
	ldr	r3, .L1235+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1218
	ldrb	r3, [r9, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1218
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	bl	flash_erase_block_en
.L1218:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	bl	flash_erase_block_en
.L1214:
	add	r6, r6, #1
	b	.L1208
.L1219:
	cmp	r7, #0
	ldr	r1, [r4, #1080]
	bne	.L1221
	ldrh	r3, [r1, r10]
	ubfx	r2, r3, #0, #11
	add	r2, r2, #1
	bfi	r3, r2, #0, #11
	strh	r3, [r1, r10]	@ movhi
	ldr	r3, [r4, #2804]
	ldr	r2, [r3, #84]
	ldrh	r0, [r3, #96]
	add	r2, r2, #1
	str	r2, [r3, #84]
	ldr	r2, [r4, #1080]
	ldrh	r2, [r2, r10]
	ubfx	r2, r2, #0, #11
	uxth	r1, r2
	cmp	r0, r1
	strlth	r2, [r3, #96]	@ movhi
	b	.L1223
.L1221:
	ldr	r3, [r1, r8, asl #2]
	ubfx	r2, r3, #11, #8
	add	r2, r2, #1
	bfi	r3, r2, #11, #8
	str	r3, [r1, r8, asl #2]
	ldr	r3, [r4, #2804]
	ldr	r2, [r3, #80]
	ldrh	r1, [r3, #98]
	add	r2, r2, #1
	str	r2, [r3, #80]
	ldr	r2, [r4, #1080]
	ldr	r2, [r2, r8, asl #2]
	ubfx	r2, r2, #11, #8
	cmp	r1, r2
	strcch	r2, [r3, #98]	@ movhi
.L1223:
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1236:
	.align	2
.L1235:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3066
	.fnend
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1248
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2804]
	mov	r4, r3
	ldrh	r1, [r2, #136]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #136]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #0
	bne	.L1239
	ldr	r1, .L1248+4
	movw	r2, #1359
	ldr	r0, .L1248+8
	bl	printk
	bl	dump_stack
.L1239:
	ldr	r3, [r4, #2804]
	movw	lr, #65535
	mov	ip, #0
.L1243:
	ldrh	r2, [r3, #136]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1240:
	cmp	r2, #63
	bgt	.L1247
	ldrh	r0, [r1, #2]!
	cmp	r0, lr
	bne	.L1245
	add	r2, r2, #1
	b	.L1240
.L1247:
	strh	ip, [r3, #136]	@ movhi
	b	.L1243
.L1245:
	add	r1, r3, r2, asl #1
	mvn	ip, #0
	strh	ip, [r1, #160]	@ movhi
	strh	r2, [r3, #136]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, ip
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1249:
	.align	2
.L1248:
	.word	.LANCHOR0
	.word	.LANCHOR1+1640
	.word	.LC0
	.fnend
	.size	ftl_alloc_sys_blk, .-ftl_alloc_sys_blk
	.align	2
	.global	ftl_free_sys_blk
	.type	ftl_free_sys_blk, %function
ftl_free_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r3, .L1260
	ldr	r2, [r3, #2804]
	mov	r4, r3
	ldrh	r1, [r2, #138]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #138]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #63
	bls	.L1252
	ldr	r1, .L1260+4
	movw	r2, #1386
	ldr	r0, .L1260+8
	bl	printk
	bl	dump_stack
.L1252:
	ldr	r3, [r4, #2804]
	movw	ip, #65535
	mov	r0, #0
.L1256:
	ldrh	r2, [r3, #138]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1253:
	cmp	r2, #63
	bgt	.L1259
	ldrh	lr, [r1, #2]!
	cmp	lr, ip
	bne	.L1254
	add	r1, r3, r2, asl #1
	strh	r5, [r1, #160]	@ movhi
	strh	r2, [r3, #138]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, #1
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1254:
	add	r2, r2, #1
	b	.L1253
.L1259:
	strh	r0, [r3, #138]	@ movhi
	b	.L1256
.L1261:
	.align	2
.L1260:
	.word	.LANCHOR0
	.word	.LANCHOR1+1660
	.word	.LC0
	.fnend
	.size	ftl_free_sys_blk, .-ftl_free_sys_blk
	.align	2
	.global	ftl_info_data_recovery
	.type	ftl_info_data_recovery, %function
ftl_info_data_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #65535
	ldrh	r6, [r0]
	cmp	r6, r3
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r4, .L1272
	mov	r8, r6, asl #2
	ldr	r5, [r4, #1080]
	add	r7, r5, r8
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	tst	r3, #224
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldrb	r2, [r0, #4]	@ zero_extendqisi2
	mov	r0, r6
	bfi	r3, r2, #5, #3
	strb	r3, [r7, #2]
	bl	zftl_remove_free_node
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	ldr	r2, [r4, #2804]
	tst	r3, #8
	ldrneh	r3, [r2, #116]
	subne	r3, r3, #1
	strneh	r3, [r2, #116]	@ movhi
	bne	.L1267
	tst	r3, #24
	ldreqh	r3, [r2, #114]
	ldrneh	r3, [r2, #118]
	subeq	r3, r3, #1
	subne	r3, r3, #1
	streqh	r3, [r2, #114]	@ movhi
	strneh	r3, [r2, #118]	@ movhi
.L1267:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	bne	.L1269
	ldr	r3, [r5, r6, asl #2]
	ubfx	r2, r3, #11, #8
	add	r2, r2, #1
	bfi	r3, r2, #11, #8
	str	r3, [r5, r6, asl #2]
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #120]
	sub	r3, r3, #1
	strh	r3, [r2, #120]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1269:
	ldrh	r2, [r5, r8]
	cmp	r3, #64
	ubfx	r1, r2, #0, #11
	add	r1, r1, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r5, r8]	@ movhi
	bne	.L1270
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #122]
	sub	r3, r3, #1
	strh	r3, [r2, #122]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1270:
	cmp	r3, #96
	ldreq	r2, [r4, #2804]
	ldreqh	r3, [r2, #124]
	subeq	r3, r3, #1
	streqh	r3, [r2, #124]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1273:
	.align	2
.L1272:
	.word	.LANCHOR0
	.fnend
	.size	ftl_info_data_recovery, .-ftl_info_data_recovery
	.align	2
	.global	ftl_get_ppa_from_index
	.type	ftl_get_ppa_from_index, %function
ftl_get_ppa_from_index:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldr	r3, .L1279
	ldr	r2, .L1279+4
	ldr	r5, [r3, #1092]
	sub	r3, r2, #3088
	ldrb	r2, [r2, #-3123]	@ zero_extendqisi2
	ldrh	r3, [r3, #-8]
	mul	r1, r2, r3
	cmp	r0, r1
	addlt	r5, r5, #16
	addge	r5, r5, #48
	smulbbge	r3, r2, r3
	ldrb	r6, [r5, #9]	@ zero_extendqisi2
	rsbge	r4, r3, r0
	mov	r1, r6
	uxthge	r4, r4
	mov	r0, r4
	bl	__aeabi_idiv
	movw	r3, #65535
	smulbb	r6, r0, r6
	mov	r7, r0
	rsb	r4, r6, r4
	uxth	r4, r4
	add	r5, r5, r4, asl #1
	ldrh	r4, [r5, #16]
	cmp	r4, r3
	bne	.L1277
	ldr	r1, .L1279+8
	movw	r2, #1945
	ldr	r0, .L1279+12
	bl	printk
	bl	dump_stack
.L1277:
	ldr	r3, .L1279+16
	ldrh	r0, [r3, #-10]
	mla	r0, r0, r4, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1280:
	.align	2
.L1279:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1680
	.word	.LC0
	.word	.LANCHOR3-3056
	.fnend
	.size	ftl_get_ppa_from_index, .-ftl_get_ppa_from_index
	.align	2
	.global	lpa_hash_get_ppa
	.type	lpa_hash_get_ppa, %function
lpa_hash_get_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	uxtb	r3, r0
	ldr	r2, .L1287
	ldr	r1, .L1287+4
	mov	r3, r3, asl #1
	str	lr, [sp, #-4]!
	.save {lr}
	ldrh	r3, [r1, r3]
	ldr	ip, [r2, #-2544]
	ldr	r1, [r2, #-2548]
	movw	r2, #65535
.L1282:
	cmp	r3, r2
	beq	.L1286
	ldr	lr, [r1, r3, asl #2]
	cmp	lr, r0
	bne	.L1283
	mov	r0, r3
	ldr	lr, [sp], #4
	b	ftl_get_ppa_from_index
.L1283:
	mov	r3, r3, asl #1
	ldrh	r3, [ip, r3]
	b	.L1282
.L1286:
	mvn	r0, #0
	ldr	pc, [sp], #4
.L1288:
	.align	2
.L1287:
	.word	.LANCHOR3
	.word	.LANCHOR3-3060
	.fnend
	.size	lpa_hash_get_ppa, .-lpa_hash_get_ppa
	.align	2
	.global	ftl_get_new_free_page
	.type	ftl_get_new_free_page, %function
ftl_get_new_free_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	movw	r3, #65535
	ldrh	r2, [r0]
	mov	r4, r0
	cmp	r2, r3
	bne	.L1290
	ldr	r1, .L1299
	movw	r2, #2088
	ldr	r0, .L1299+4
	bl	printk
	bl	dump_stack
.L1290:
	ldr	r3, .L1299+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-8]
	cmp	r2, r3
	bne	.L1291
	ldr	r1, .L1299
	movw	r2, #2089
	ldr	r0, .L1299+4
	bl	printk
	bl	dump_stack
.L1291:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L1292
	ldr	r1, .L1299
	movw	r2, #2090
	ldr	r0, .L1299+4
	bl	printk
	bl	dump_stack
.L1292:
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	movw	r2, #65535
	ldr	r1, .L1299+12
	mov	lr, #0
	add	r3, r4, r3, asl #1
	ldrb	r0, [r1, #-3123]	@ zero_extendqisi2
	ldrh	ip, [r3, #16]
.L1293:
	cmp	ip, r2
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	bne	.L1298
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #5]
	cmp	r3, r0
	streqb	lr, [r4, #5]
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	add	r3, r4, r3, asl #1
	ldrh	ip, [r3, #16]
	b	.L1293
.L1298:
	ldr	r0, .L1299+16
	add	r3, r3, #1
	ldrb	r1, [r1, #-3123]	@ zero_extendqisi2
	uxtb	r3, r3
	ldrh	r2, [r4, #2]
	ldrh	r0, [r0, #-10]
	cmp	r1, r3
	strb	r3, [r4, #5]
	moveq	r3, #0
	streqb	r3, [r4, #5]
	mul	r0, r0, ip
	ldrh	ip, [r4, #6]
	sub	ip, ip, #1
	strh	ip, [r4, #6]	@ movhi
	ldrh	ip, [r4, #10]
	orr	r0, r0, r2
	addeq	r2, r2, #1
	streqh	r2, [r4, #2]	@ movhi
	add	ip, ip, #1
	strh	ip, [r4, #10]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1300:
	.align	2
.L1299:
	.word	.LANCHOR1+1704
	.word	.LC0
	.word	.LANCHOR3-3088
	.word	.LANCHOR3
	.word	.LANCHOR3-3056
	.fnend
	.size	ftl_get_new_free_page, .-ftl_get_new_free_page
	.align	2
	.global	ftl_ext_alloc_new_blk
	.type	ftl_ext_alloc_new_blk, %function
ftl_ext_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	bl	ftl_alloc_sys_blk
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r4, r0
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1302
	ldr	r1, .L1304
	movw	r2, #2125
	ldr	r0, .L1304+4
	bl	printk
	bl	dump_stack
.L1302:
	ldr	r5, .L1304+8
	mov	r1, #0
	mov	r0, r4
	bl	ftl_erase_phy_blk
	ldr	r3, [r5, #2804]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sys_blk
	ldr	r3, [r5, #2804]
	mov	r0, #0
	strh	r4, [r3, #130]	@ movhi
	strh	r0, [r3, #140]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1305:
	.align	2
.L1304:
	.word	.LANCHOR1+1728
	.word	.LC0
	.word	.LANCHOR0
	.fnend
	.size	ftl_ext_alloc_new_blk, .-ftl_ext_alloc_new_blk
	.align	2
	.global	ftl_total_vpn_update
	.type	ftl_total_vpn_update, %function
ftl_total_vpn_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1319
	ldrh	r3, [r2, #-12]
	cmp	r3, #4
	cmpls	r0, #0
	addeq	r3, r3, #1
	streqh	r3, [r2, #-12]	@ movhi
	bxeq	lr
	ldr	r3, .L1319+4
	movw	ip, #1076
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #0
	ldrh	lr, [r3, ip]
	movw	r6, #65535
	strh	r0, [r2, #-12]	@ movhi
	mov	ip, r0
	ldr	r1, [r3, #1088]
	ldr	r2, [r3, #1080]
	sub	r1, r1, #2
	add	lr, r2, lr, asl #2
.L1309:
	cmp	r2, lr
	beq	.L1318
	ldrh	r4, [r1, #2]!
	cmp	r4, r6
	beq	.L1310
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	and	r5, r5, #224
	cmp	r5, #160
	addeq	r0, r0, r4
	addne	ip, ip, r4
.L1310:
	add	r2, r2, #4
	b	.L1309
.L1318:
	ldr	r4, [r3, #1092]
	ldr	r3, [r3, #2804]
	str	ip, [r4, #524]
	str	r0, [r4, #528]
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	bl	__aeabi_uidiv
	str	r0, [r4, #532]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1320:
	.align	2
.L1319:
	.word	.LANCHOR3-2528
	.word	.LANCHOR0
	.fnend
	.size	ftl_total_vpn_update, .-ftl_total_vpn_update
	.align	2
	.global	ftl_debug_info_fill
	.type	ftl_debug_info_fill, %function
ftl_debug_info_fill:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1326
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L1325
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	sub	r4, r0, #2
	clz	r4, r4
	cmp	r2, #0
	mov	r4, r4, lsr #5
	moveq	r4, #0
	cmp	r4, #0
	streq	r4, [r1]
	streq	r4, [r1, #4]
	beq	.L1322
	ldr	r3, .L1326+4
	mov	r4, r1
	mov	r0, r2
	str	r3, [r1]
	mov	r1, #1024
	bl	js_hash
	str	r0, [r4, #4]
.L1322:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1325:
	mov	r0, #0
	bx	lr
.L1327:
	.align	2
.L1326:
	.word	.LANCHOR0
	.word	1212240712
	.fnend
	.size	ftl_debug_info_fill, .-ftl_debug_info_fill
	.align	2
	.global	ftl_vpn_update
	.type	ftl_vpn_update, %function
ftl_vpn_update:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	bl	zftl_list_update_data_list
	ldr	r3, .L1332
	mov	r4, r4, asl #1
	ldr	r2, [r3, #1088]
	ldrh	r2, [r2, r4]
	cmp	r2, #0
	moveq	r0, #1
	movne	r0, #0
	streq	r0, [r3, #2820]
	ldmfd	sp!, {r4, pc}
.L1333:
	.align	2
.L1332:
	.word	.LANCHOR0
	.fnend
	.size	ftl_vpn_update, .-ftl_vpn_update
	.align	2
	.global	ftl_vpn_decrement
	.type	ftl_vpn_decrement, %function
ftl_vpn_decrement:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	cmp	r0, r3
	mov	r5, r0
	beq	.L1335
	ldr	r1, .L1344
	mov	r3, r0, asl #1
	ldr	r2, [r1, #1088]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1335
	ldr	r3, [r1, #1080]
	mov	r2, r4
	mov	r1, r5
	add	r3, r3, r0, asl #2
	ldr	r0, .L1344+4
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1335:
	ldr	r3, .L1344+8
	ldrh	r0, [r3]
	mov	r4, r3
	cmp	r0, r5
	beq	.L1341
	movw	r2, #65535
	cmp	r0, r2
	streqh	r5, [r3]	@ movhi
	beq	.L1341
	bl	ftl_vpn_update
	add	r3, r4, #624
	ldrh	r1, [r4]
	ldrh	r2, [r3, #-10]
	strh	r5, [r4]	@ movhi
	add	r2, r2, #1
	uxth	r2, r2
	adds	r0, r0, #0
	movne	r0, #1
	cmp	r2, #7
	movhi	r2, #0
	strh	r2, [r3, #-10]	@ movhi
	ldrh	r3, [r3, #-10]
	ldr	r2, .L1344+12
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1341:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1345:
	.align	2
.L1344:
	.word	.LANCHOR0
	.word	.LC109
	.word	.LANCHOR3-3152
	.word	.LANCHOR3-3148
	.fnend
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	uxtb	r6, r0
	ldr	r3, .L1358
	movw	r4, #65535
	ldr	ip, .L1358+4
	mov	r6, r6, asl #1
	mov	r8, r4
	ldr	r7, [r3, #-2548]
	ldrh	lr, [ip, r6]
	ldr	r9, [r3, #-2544]
.L1347:
	cmp	lr, r8
	beq	.L1351
	ldr	r5, [r7, lr, asl #2]
	add	r10, r7, lr, asl #2
	cmp	r5, r0
	mov	r5, lr, asl #1
	bne	.L1348
	mvn	lr, #0
	str	lr, [r10]
	movw	lr, #65535
	cmp	r4, lr
	ldr	lr, [r3, #-2544]
	movne	r4, r4, asl #1
	ldreqh	lr, [lr, r5]
	ldrneh	r7, [lr, r5]
	streqh	lr, [ip, r6]	@ movhi
	strneh	r7, [lr, r4]	@ movhi
	mvn	r4, #0
	ldr	lr, [r3, #-2544]
	strh	r4, [lr, r5]	@ movhi
	b	.L1351
.L1348:
	mov	r4, lr
	ldrh	lr, [r9, r5]
	b	.L1347
.L1351:
	ldr	r4, [r3, #-2548]
	cmn	r1, #1
	ldr	lr, .L1358
	str	r0, [r4, r2, asl #2]
	ldrh	r0, [ip, r6]
	ldr	r3, [r3, #-2544]
	strh	r2, [ip, r6]	@ movhi
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	beq	.L1353
	sub	r3, lr, #3120
	ldrh	r0, [r3, #-12]
	ldr	r3, .L1358+8
	mov	r1, r1, lsr r0
	ldrb	r3, [r3, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r1, r0, asl r3
	ldrb	r1, [lr, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L1353:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1359:
	.align	2
.L1358:
	.word	.LANCHOR3
	.word	.LANCHOR3-3060
	.word	.LANCHOR0
	.fnend
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r3, #1
	ldr	r1, .L1368
	ldr	r7, .L1368+4
	sub	r1, r1, #3120
	ldrb	r5, [r1, #-10]	@ zero_extendqisi2
	ldrb	r4, [r7, #1189]	@ zero_extendqisi2
	ldrh	r1, [r1, #-12]
	rsb	r2, r4, #24
	mov	r4, r3, asl r4
	sub	r4, r4, #1
	and	r4, r4, r0, lsr r2
	rsb	r2, r1, r2
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	uxtb	r4, r4
	and	r0, r3, r0, lsr r1
	mov	r1, r5
	smulbb	r6, r4, r5
	uxth	r9, r0
	bl	__aeabi_uidiv
	cmp	r5, #1
	subhi	r3, r5, #1
	uxtb	r6, r6
	andhi	r3, r3, r9
	uxth	r8, r0
	addhi	r3, r6, r3
	uxtbhi	r6, r3
	ldr	r3, .L1368+8
	ldr	r3, [r3]
	tst	r3, #16384
	beq	.L1362
	str	r9, [sp]
	mov	r1, r4
	ldr	r0, .L1368+12
	mov	r2, r6
	mov	r3, r8
	bl	printk
.L1362:
	movw	r3, #1076
	ldrh	r3, [r7, r3]
	cmp	r3, r8
	bls	.L1360
	ldr	r3, .L1368+4
	mov	r2, #1
	ldr	r4, [r3, #1080]
	add	r4, r4, r8, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
	orr	r6, r3, r2, asl r6
	strb	r6, [r4, #3]
.L1360:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L1369:
	.align	2
.L1368:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC110
	.fnend
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
	.align	2
	.global	gc_free_bad_sblk
	.type	gc_free_bad_sblk, %function
gc_free_bad_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L1391
	str	r0, [sp]
	ldr	r3, [r4, #920]
	cmp	r3, #0
	movne	r5, #0
	ldrne	r7, .L1391+4
	beq	.L1387
.L1371:
	ldrb	r3, [r7, #-3123]	@ zero_extendqisi2
	uxth	r0, r5
	cmp	r3, r0
	bls	.L1387
	ldrb	r9, [r7, #-3130]	@ zero_extendqisi2
	mov	fp, #0
	ldr	r6, .L1391+8
	mov	r1, r9
	uxth	r8, r9
	bl	__aeabi_idiv
	ldr	r2, .L1391+12
	ldrb	r3, [r6, #1189]	@ zero_extendqisi2
	cmp	r9, #1
	subhi	r1, r8, #1
	ldrh	r2, [r2]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	ldrh	r2, [sp]
	andhi	r1, r1, r5
	smulbb	r2, r2, r8
	ldr	r8, .L1391+16
	add	r0, r2, r0, asl r3
	mov	r9, r8
	uxth	r2, r0
	addhi	r2, r2, r1
	uxthhi	r2, r2
.L1373:
	ldr	r1, [r4, #920]
	uxth	r10, fp
	cmp	r10, r1
	bcs	.L1389
	add	r1, r10, #1088
	add	r1, r1, #8
	mov	r1, r1, asl #1
	ldrh	r1, [r8, r1]
	cmp	r1, r2
	bne	.L1374
	mov	r1, r2
	ldr	r0, .L1391+20
	str	r2, [sp, #4]
	bl	printk
	ldr	r3, .L1391+4
	ldrb	r1, [r3, #-2536]	@ zero_extendqisi2
	cmp	r1, #0
	ldr	r2, [sp, #4]
	bne	.L1375
	ldrb	r1, [r7, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1376
.L1375:
	ldr	r1, [r6, #2804]
	ldr	r3, .L1391+24
	ldr	r1, [r1, #156]
	cmp	r1, r3
	beq	.L1377
.L1376:
	ldr	r1, .L1391+12
	str	r2, [sp, #4]
	ldrh	r0, [r1]
	mov	r0, r2, asl r0
	bl	ftl_mask_bad_block
	ldr	r2, [sp, #4]
.L1377:
	ldr	r0, [r4, #920]
.L1378:
	cmp	r10, r0
	bcs	.L1390
	add	r1, r10, #1088
	add	r3, r10, #1
	add	r1, r1, #9
	mov	r1, r1, asl #1
	ldrh	lr, [r9, r1]
	add	r1, r10, #1088
	add	r1, r1, #8
	uxth	r10, r3
	mov	r1, r1, asl #1
	strh	lr, [r9, r1]	@ movhi
	b	.L1378
.L1390:
	sub	r0, r0, #1
	str	r0, [r4, #920]
.L1374:
	add	fp, fp, #1
	b	.L1373
.L1389:
	add	r5, r5, #1
	b	.L1371
.L1387:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1392:
	.align	2
.L1391:
	.word	.LANCHOR0+4096
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3132
	.word	.LANCHOR0+2828
	.word	.LC111
	.word	1145785929
	.fnend
	.size	gc_free_bad_sblk, .-gc_free_bad_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r0, asl #2
	ldr	r5, .L1417
	.pad #20
	sub	sp, sp, #20
	mov	r7, r0
	ldr	r6, [r5, #1080]
	add	r8, r6, r10
	ldrb	r4, [r8, #2]	@ zero_extendqisi2
	tst	r4, #8
	mov	r3, r4, lsr #5
	str	r3, [sp]
	beq	.L1394
	ldr	r9, [r5, #2804]
	movw	r1, #1084
	ldrh	r2, [r6, r10]
	ldr	fp, [r6, r0, asl #2]
	ldrh	r3, [r9, #74]
	ubfx	r2, r2, #0, #11
	ldrh	ip, [r9, #72]
	ubfx	fp, fp, #11, #8
	add	r3, r2, r3
	ldrh	r9, [r5, r1]
	add	ip, fp, ip
	str	r2, [sp, #12]
	uxth	r3, r3
	str	r3, [sp, #4]
	mov	r1, r9
	uxth	ip, ip
	mov	r0, r3
	str	ip, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r1, .L1417+4
	ldrh	lr, [r1, #-6]
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #4]
	uxtah	r0, ip, r0
	ldr	r2, [sp, #12]
	cmp	r0, lr
	ble	.L1395
	movw	r3, #2798
	sub	r1, r1, #544
	ldrh	r0, [r5, r3]
	movw	r3, #2790
	ldrh	r3, [r5, r3]
	add	r0, r0, r3
	ldrh	r3, [r1, #-12]
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1401
	movw	r3, #2794
	movw	r0, #2788
	ldrh	r0, [r5, r0]
	ldrh	r3, [r5, r3]
	add	r3, r3, r0
	movw	r0, #2796
	ldrh	ip, [r5, r0]
	add	ip, r3, ip
	ldrh	r3, [r1, #-14]
	add	r3, r3, #8
	cmp	ip, r3
	b	.L1416
.L1395:
	mla	r3, r9, ip, r3
	ldrh	r0, [r1, #-4]
	cmp	r3, r0
	ble	.L1397
	movw	r3, #2794
	movw	r0, #2788
	ldrh	r0, [r5, r0]
	ldrh	r3, [r5, r3]
	add	r3, r3, r0
	movw	r0, #2796
	ldrh	ip, [r5, r0]
	add	ip, r3, ip
	sub	r3, r1, #544
	ldrh	r1, [r3, #-14]
	add	r0, r1, #8
	cmp	ip, r0
	blt	.L1414
	movw	r0, #2798
	movw	lr, #2790
	ldrh	r0, [r5, r0]
	ldrh	lr, [r5, lr]
	ldrh	r3, [r3, #-12]
	add	r0, r0, lr
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1401
	add	r1, r1, #24
	cmp	ip, r1
.L1416:
	bge	.L1401
.L1414:
	bfc	r4, #3, #2
	b	.L1412
.L1401:
	mov	r3, #2
	bfi	r4, r3, #3, #2
.L1412:
	strb	r4, [r8, #2]
.L1397:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	bne	.L1402
	mul	r9, r9, fp
	ldrh	r3, [r6, r10]
	mov	fp, fp, lsr #3
	add	r9, r9, r9, asl #1
	add	r2, r2, r9, asr #2
	bfi	r3, r2, #0, #11
	strh	r3, [r6, r10]	@ movhi
	ldr	r4, [r6, r7, asl #2]
	bfi	r4, fp, #11, #8
	str	r4, [r6, r7, asl #2]
	b	.L1403
.L1402:
	cmp	r3, #16
	bne	.L1403
	mov	r0, r2
	mov	r1, r9
	str	r2, [sp, #4]
	bl	__aeabi_idiv
	ldr	r3, [r6, r7, asl #2]
	add	r0, r0, r0, asl #1
	add	fp, fp, r0, asr #2
	bfi	r3, fp, #11, #8
	str	r3, [r6, r7, asl #2]
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, r10]
	mov	r2, r2, asr #5
	bfi	r3, r2, #0, #11
	b	.L1413
.L1394:
	tst	r4, #24
	bne	.L1403
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1403
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	cmp	r1, #15
	bhi	.L1403
	movw	r3, #2794
	movw	r2, #2788
	ldrh	r2, [r5, r2]
	ldrh	r3, [r5, r3]
	ldr	r0, .L1417+8
	add	r3, r3, r2
	movw	r2, #2796
	ldrh	r2, [r5, r2]
	add	r3, r3, r2
	ldrh	r2, [r0, #-14]
	add	r2, r2, #16
	cmp	r3, r2
	ble	.L1403
	movw	r3, #2798
	ldrh	r2, [r5, r3]
	ldrh	r3, [r0, #-12]
	add	r2, r2, r1
	add	r3, r3, #8
	cmp	r2, r3
	bge	.L1403
	mov	r3, #2
	bfi	r4, r3, #3, #2
	strb	r4, [r8, #2]
	movw	r3, #1084
	ldrh	r4, [r6, r10]
	ldrh	r1, [r5, r3]
	ubfx	r4, r4, #0, #11
	mov	r0, r4
	mov	r4, r4, asr #5
	bl	__aeabi_idiv
	ldr	r3, [r6, r7, asl #2]
	ubfx	r2, r3, #11, #8
	add	r0, r0, r0, asl #1
	add	r0, r2, r0, asr #2
	bfi	r3, r0, #11, #8
	str	r3, [r6, r7, asl #2]
	ldrh	r3, [r6, r10]
	bfi	r3, r4, #0, #11
.L1413:
	strh	r3, [r6, r10]	@ movhi
.L1403:
	mov	r0, r7
	bl	zftl_remove_data_node
	ldr	r3, .L1417
	mov	r0, #0
	ldr	r2, [r3, #1080]
	mov	r4, r3
	add	r10, r2, r10
	ldrb	r2, [r10, #2]	@ zero_extendqisi2
	bfc	r2, #5, #3
	strb	r2, [r10, #2]
	ldr	r1, [r3, #1088]
	mov	r2, r7, asl #1
	strh	r0, [r1, r2]	@ movhi
	ldr	r2, [sp]
	add	r2, r2, #6
	and	r2, r2, #7
	cmp	r2, #4
	bhi	.L1406
	mov	r0, r7
	bl	gc_free_bad_sblk
.L1406:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1407
	ldr	r3, [r4, #1092]
	movw	r2, #586
	ldrh	r1, [r3, r2]
	cmp	r1, r7
	bne	.L1407
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1407
	strh	r7, [r3, r2]	@ movhi
	mov	r1, r7
	ldr	r0, .L1417+12
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1407:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	zftl_insert_free_list
.L1418:
	.align	2
.L1417:
	.word	.LANCHOR0
	.word	.LANCHOR3-2528
	.word	.LANCHOR3-3072
	.word	.LC112
	.fnend
	.size	ftl_free_sblk, .-ftl_free_sblk
	.align	2
	.global	gc_free_src_blk
	.type	gc_free_src_blk, %function
gc_free_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	ldr	r6, .L1458
	ldr	r8, .L1458+4
	mov	r7, r6
.L1420:
	ldrh	r2, [r8, #52]
	uxth	r3, r5
	cmp	r2, r3
	bls	.L1457
	add	r3, r8, r3, asl #1
	ldrh	r4, [r3, #54]
	ldr	r3, [r6, #1088]
	mov	r9, r4, asl #1
	ldrh	r2, [r3, r9]
	cmp	r2, #0
	beq	.L1421
	ldr	r0, .L1458+8
	mov	r1, r4
	bl	printk
.L1421:
	ldr	r3, [r7, #1088]
	mov	r2, #0
	strh	r2, [r3, r9]	@ movhi
	ldr	r3, [r7, #1088]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	bne	.L1422
	ldr	r3, .L1458+12
	ldr	r9, [r6, #1080]
	ldr	r3, [r3]
	add	r9, r9, r4, asl #2
	tst	r3, #256
	beq	.L1423
	ldrb	r2, [r9, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L1458+16
	mov	r2, r2, lsr #5
	bl	printk
.L1423:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L1424
	ldr	r1, .L1458+20
	movw	r2, #1363
	ldr	r0, .L1458+24
	bl	printk
	bl	dump_stack
.L1424:
	mov	r0, r4
	bl	ftl_free_sblk
	ldr	r3, [r7, #1092]
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	beq	.L1425
	add	r0, r3, #388
	mov	r2, #0
	add	r0, r0, #2
.L1427:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1426
	add	r2, r2, #196
	mvn	r0, #0
	add	r1, r1, r0
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #124]	@ movhi
	b	.L1425
.L1426:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1427
.L1425:
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	beq	.L1428
	add	r0, r3, #134
	mov	r2, #0
.L1430:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1429
	add	r2, r3, r2, asl #1
	mvn	r0, #0
	add	r1, r1, r0
	strh	r0, [r2, #136]	@ movhi
	strh	r1, [r3, #120]	@ movhi
	b	.L1428
.L1429:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1430
.L1428:
	ldrh	r0, [r3, #122]
	cmp	r0, #0
	beq	.L1431
	add	r1, r3, #260
	mov	r2, #0
	add	r1, r1, #2
.L1433:
	ldrh	ip, [r1, #2]!
	cmp	ip, r4
	bne	.L1432
	add	r2, r2, #132
	mvn	r1, #0
	add	r0, r0, r1
	mov	r2, r2, asl #1
	strh	r1, [r3, r2]	@ movhi
	strh	r0, [r3, #122]	@ movhi
	b	.L1431
.L1432:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1433
	b	.L1431
.L1422:
	mov	r0, r4
	mov	r1, #1
	bl	gc_add_sblk
.L1431:
	add	r5, r5, #1
	b	.L1420
.L1457:
	ldr	r3, .L1458+4
	mov	r2, #0
	strh	r2, [r3, #52]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1459:
	.align	2
.L1458:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LC113
	.word	.LANCHOR2
	.word	.LC114
	.word	.LANCHOR1+1752
	.word	.LC0
	.fnend
	.size	gc_free_src_blk, .-gc_free_src_blk
	.align	2
	.global	print_ftl_debug_info
	.type	print_ftl_debug_info, %function
print_ftl_debug_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	movw	ip, #2790
	ldr	r4, .L1462
	.pad #32
	sub	sp, sp, #32
	ldr	r3, [r4, #2804]
	ldrh	ip, [r4, ip]
	ldr	r0, [r4, #1092]
	ldrh	r1, [r3, #148]
	ldrh	r2, [r3, #146]
	movw	r3, #2788
	str	ip, [sp]
	movw	ip, #2792
	ldrh	ip, [r4, ip]
	ldrh	r3, [r4, r3]
	str	ip, [sp, #4]
	movw	ip, #2794
	ldrh	ip, [r4, ip]
	str	ip, [sp, #8]
	movw	ip, #2796
	ldrh	ip, [r4, ip]
	str	ip, [sp, #12]
	movw	ip, #2798
	ldrh	ip, [r4, ip]
	str	ip, [sp, #16]
	ldr	ip, [r0, #524]
	str	ip, [sp, #20]
	ldr	r0, [r0, #528]
	str	r0, [sp, #24]
	ldr	r0, [r4, #2784]
	str	r0, [sp, #28]
	ldr	r0, .L1462+4
	bl	printk
	ldr	r2, [r4, #2804]
	ldrb	r3, [r4, #2801]	@ zero_extendqisi2
	ldr	r0, .L1462+8
	ldr	r1, [r2, #8]
	str	r1, [sp]
	ldr	r1, [r2, #64]
	str	r1, [sp, #4]
	ldr	r1, [r2, #20]
	ldr	r2, [r2, #28]
	bl	printk
	ldr	r2, [r4, #2804]
	ldr	r0, .L1462+12
	ldr	r3, [r2, #16]
	ldr	r1, [r2, #52]
	ldr	r2, [r2, #60]
	mov	r3, r3, lsr #11
	bl	printk
	ldr	r2, [r4, #2804]
	ldrh	r0, [r2, #92]
	ldrh	r1, [r2, #74]
	ldrh	r3, [r2, #88]
	str	r0, [sp]
	ldrh	r0, [r2, #96]
	str	r0, [sp, #4]
	ldrh	r0, [r2, #72]
	str	r0, [sp, #8]
	ldr	r0, [r2, #80]
	str	r0, [sp, #12]
	ldrh	r0, [r2, #90]
	str	r0, [sp, #16]
	ldrh	r0, [r2, #94]
	str	r0, [sp, #20]
	ldrh	r0, [r2, #98]
	str	r0, [sp, #24]
	ldr	r0, .L1462+16
	ldr	r2, [r2, #84]
	bl	printk
	add	r0, r4, #2816
	movw	r3, #2810
	ldrh	r0, [r0]
	ldrh	r1, [r4, r3]
	movw	r3, #2812
	ldrh	r2, [r4, r3]
	movw	r3, #2814
	ldrh	r3, [r4, r3]
	str	r0, [sp]
	movw	r0, #2808
	ldrh	r0, [r4, r0]
	str	r0, [sp, #4]
	ldr	r0, .L1462+20
	bl	printk
	ldr	r0, [r4, #1092]
	movw	lr, #590
	ldr	ip, [r4, #2804]
	add	r3, r0, #584
	ldrh	r1, [r3]
	movw	r3, #586
	ldrh	r2, [r0, r3]
	add	r3, r0, #588
	ldrh	r0, [r0, lr]
	ldrh	r3, [r3]
	str	r0, [sp]
	ldr	r0, [ip, #156]
	str	r0, [sp, #4]
	ldrh	r0, [ip, #150]
	str	r0, [sp, #8]
	ldr	r0, .L1462+24
	bl	printk
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L1463:
	.align	2
.L1462:
	.word	.LANCHOR0
	.word	.LC115
	.word	.LC116
	.word	.LC117
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.fnend
	.size	print_ftl_debug_info, .-print_ftl_debug_info
	.align	2
	.global	ftl_write_buf
	.type	ftl_write_buf, %function
ftl_write_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	subs	r4, r0, #0
	bne	.L1465
	ldr	r1, .L1476
	movw	r2, #810
	ldr	r0, .L1476+4
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1465:
	ldr	r5, .L1476+8
	ldrb	r2, [r4, #40]	@ zero_extendqisi2
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1470
	ldr	r1, .L1476
	movw	r2, #817
	ldr	r0, .L1476+4
	bl	printk
	bl	dump_stack
.L1470:
	ldrb	r3, [r4, #40]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1467
	ldrb	r2, [r5, #2772]	@ zero_extendqisi2
	ldr	r6, .L1476+8
	cmp	r2, r3
	bcs	.L1468
.L1467:
	mov	r0, r4
	bl	zbuf_free
	ldrb	r0, [r5, #2800]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, pc}
.L1468:
	mov	r1, r4
	ldr	r0, .L1476+12
	bl	buf_add_tail
	ldr	r3, [r6, #2804]
	ldrb	r1, [r4, #40]	@ zero_extendqisi2
	ldrb	r0, [r6, #2800]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	r0, r0, #1
	add	r2, r2, r1
	str	r2, [r3, #16]
	ldr	r2, [r3, #32]
	uxtb	r0, r0
	strb	r0, [r6, #2800]
	add	r2, r2, #1
	str	r2, [r3, #32]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1477:
	.align	2
.L1476:
	.word	.LANCHOR1+1768
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR0+2824
	.fnend
	.size	ftl_write_buf, .-ftl_write_buf
	.align	2
	.global	ftl_write_completed
	.type	ftl_write_completed, %function
ftl_write_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r8, .L1496
	ldr	r9, .L1496+4
	mov	r6, r8
.L1479:
	ldrb	r10, [r8, #2778]	@ zero_extendqisi2
	cmp	r10, #255
	beq	.L1495
	mov	r4, #48
	ldr	r7, .L1496+8
	mul	r4, r4, r10
	add	r7, r7, r4
	add	r4, r8, r4
	ldrb	r3, [r4, #1236]	@ zero_extendqisi2
	strb	r3, [r8, #2778]
	ldr	r3, [r4, #1272]
	cmn	r3, #1
	bne	.L1480
	ldr	r4, .L1496+12
	ldrb	r3, [r4, #-2536]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1481
	ldrb	r3, [r4, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1482
.L1481:
	ldr	r3, [r6, #2804]
	ldr	r3, [r3, #156]
	cmp	r3, r9
	beq	.L1483
.L1482:
	mov	r5, #48
	ldr	r3, .L1496+16
	mla	r5, r5, r10, r6
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r6, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	ldr	r2, [r5, #1260]
	rsb	r3, r0, r3
	mov	r2, r2, lsr r0
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r3, [r6, #1092]
	movw	r2, #65535
	ldr	r1, [r3, #560]
	cmp	r1, r2
	uxth	r4, r0
	streq	r4, [r3, #560]
	ldreq	r2, [r5, #1260]
	ldr	r0, .L1496+20
	mov	r1, r4
	streq	r2, [r3, #564]
	mov	r3, #48
	mla	r10, r3, r10, r6
	ldr	r2, [r10, #1256]
	ldr	r3, [r10, #1260]
	bl	printk
	ldr	r3, [r6, #1092]
	ldr	r2, [r3, #556]
	add	r2, r2, #1
	str	r2, [r3, #556]
	ldrh	r2, [r3, #16]
	cmp	r2, r4
	moveq	r2, #0
	streqh	r2, [r3, #22]	@ movhi
	beq	.L1483
	ldrh	r2, [r3, #48]
	cmp	r2, r4
	moveq	r2, #0
	streqh	r2, [r3, #54]	@ movhi
.L1483:
	mov	r0, r7
	mov	r5, #1
	bl	ftl_write_buf
	b	.L1479
.L1480:
	cmp	r5, #1
	add	r3, r4, #1248
	add	r3, r3, #12
	bne	.L1488
	ldr	r1, [r4, #1256]
	ldr	r2, [r4, #1260]
	ldr	r0, .L1496+24
	bl	printk
	mov	r0, r7
	bl	ftl_write_buf
	b	.L1479
.L1488:
	ldr	r1, [r3, #4]
	ldr	r0, [r4, #1256]
	ldrh	r2, [r7, #32]
	bl	lpa_hash_update_ppa
	ldrb	r3, [r4, #1238]	@ zero_extendqisi2
	and	r5, r3, #4
	ands	r5, r5, #255
	bicne	r3, r3, #2
	strneb	r3, [r4, #1238]
	movne	r5, #0
	bne	.L1479
	mov	r0, r7
	bl	zbuf_free
	b	.L1479
.L1495:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1497:
	.align	2
.L1496:
	.word	.LANCHOR0
	.word	1145785929
	.word	.LANCHOR0+1236
	.word	.LANCHOR3
	.word	.LANCHOR3-3132
	.word	.LC121
	.word	.LC122
	.fnend
	.size	ftl_write_completed, .-ftl_write_completed
	.align	2
	.global	zftl_add_read_buf
	.type	zftl_add_read_buf, %function
zftl_add_read_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	subs	r4, r0, #0
	bne	.L1499
	ldr	r1, .L1504
	movw	r2, #1150
	ldr	r0, .L1504+4
	bl	printk
	bl	dump_stack
	ldmfd	sp!, {r3, r4, r5, lr}
	b	print_ftl_debug_info
.L1499:
	ldr	r3, .L1504+8
	ldrb	r2, [r4, #40]	@ zero_extendqisi2
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1501
	ldr	r1, .L1504
	movw	r2, #1157
	ldr	r0, .L1504+4
	bl	printk
	bl	dump_stack
.L1501:
	ldr	r5, .L1504+12
	mov	r1, r4
	ldr	r0, .L1504+16
	bl	buf_add_tail
	ldrb	r3, [r5, #-2529]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r5, #-2529]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1505:
	.align	2
.L1504:
	.word	.LANCHOR1+1784
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-2530
	.fnend
	.size	zftl_add_read_buf, .-zftl_add_read_buf
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1507
	mvn	r2, #0
	mov	r0, #0
	strb	r2, [r3, #2775]
	strb	r2, [r3, #2778]
	strb	r2, [r3, #2776]
	strb	r2, [r3, #2777]
	bx	lr
.L1508:
	.align	2
.L1507:
	.word	.LANCHOR0
	.fnend
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L1519
	ldr	r0, .L1519+4
	ldrb	r1, [r4, #2775]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r4, #2775]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1519+8
	mov	r2, #48
	mov	r6, r2
	mla	r4, r2, r4, r3
	mov	r5, r3
.L1511:
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldrb	r2, [r4, #42]	@ zero_extendqisi2
	ldr	r3, [r4, #24]
	ldr	r0, .L1519+12
	bl	printk
	ldrb	r4, [r4]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mla	r4, r6, r4, r5
	b	.L1511
.L1520:
	.align	2
.L1519:
	.word	.LANCHOR0
	.word	.LC123
	.word	.LANCHOR0+1236
	.word	.LC124
	.fnend
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.type	queue_lun_state, %function
queue_lun_state:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	lr, .L1537
	ldrb	ip, [lr, #2775]	@ zero_extendqisi2
	cmp	ip, #255
	beq	.L1529
	ldr	r4, .L1537+4
	mov	r2, #1
	ldrb	r3, [lr, #1189]	@ zero_extendqisi2
	mov	r9, #48
	sub	r5, r4, #3120
	rsb	r7, r3, #24
	ldrb	r4, [r4, #-3130]	@ zero_extendqisi2
	ldrh	r8, [r5, #-12]
	mov	r3, r2, asl r3
	sub	r4, r4, #1
	sub	r3, r3, #1
	rsb	r5, r8, r7
	mov	r2, r2, asl r5
	uxth	r4, r4
	sub	r2, r2, #1
	uxth	r3, r3
	and	r5, r4, r0, asr r8
	and	r10, r3, r0, asr r7
	uxth	r2, r2
	and	r5, r5, r2
.L1528:
	mla	r0, r9, ip, lr
	ldr	r6, [r0, #1260]
	and	fp, r3, r6, lsr r7
	cmp	fp, r10
	bne	.L1523
	and	r6, r4, r6, lsr r8
	ldrb	r0, [r0, #1278]	@ zero_extendqisi2
	and	r6, r6, r2
	cmp	r6, r5
	bne	.L1524
	cmp	r1, #1
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1523
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1524:
	cmp	r1, #3
	ldrls	pc, [pc, r1, asl #2]
	b	.L1523
.L1525:
	.word	.L1522
	.word	.L1526
	.word	.L1527
	.word	.L1522
.L1526:
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1523
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1527:
	cmp	r0, #11
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1523:
	mla	ip, r9, ip, lr
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1528
.L1529:
	mov	r0, #0
.L1522:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1538:
	.align	2
.L1537:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1566
	mov	ip, #0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r0, #48
	ldrb	r1, [r3, #2777]	@ zero_extendqisi2
	mov	r8, r3
	ldrb	r2, [r3, #2775]	@ zero_extendqisi2
	mvn	r9, #0
	ldrb	r6, [r3, #2776]	@ zero_extendqisi2
	mov	r4, r3
	ldrb	r7, [r3, #2778]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r10, .L1566+4
	str	r1, [sp, #4]
.L1540:
	cmp	r2, #255
	beq	.L1541
	mla	fp, r0, r2, r3
	mov	r1, r2
	ldrb	lr, [fp, #1278]	@ zero_extendqisi2
	sub	lr, lr, #12
	cmp	lr, #1
	bls	.L1542
	cmp	ip, #0
	ldrne	r3, .L1566
	beq	.L1539
	b	.L1564
.L1542:
	mul	ip, r0, r1
	ldrb	r2, [fp, #1236]	@ zero_extendqisi2
	add	lr, r8, ip
	ldrb	fp, [lr, #1279]	@ zero_extendqisi2
	strb	r9, [lr, #1236]
	cmp	fp, #1
	bne	.L1545
	add	ip, r10, ip
	ldrh	ip, [ip, #34]
	cmp	ip, #0
	moveq	ip, r7
	ldreq	lr, .L1566+8
	ldrne	ip, [sp, #4]
	ldrne	lr, .L1566+12
	b	.L1546
.L1545:
	cmp	fp, #0
	beq	.L1565
.L1547:
	mov	ip, #1
	b	.L1540
.L1565:
	ldr	ip, [lr, #1256]
	cmn	ip, #1
	movne	ip, r6
	ldrne	lr, .L1566+16
	beq	.L1547
.L1546:
	cmp	ip, #255
	bne	.L1551
	strb	r2, [r3, #2775]
	mov	r2, #48
	mla	r1, r2, r1, r3
	ldrb	r3, [r1, #1237]	@ zero_extendqisi2
	strb	r3, [lr]
	b	.L1539
.L1551:
	mov	lr, ip
	mla	ip, r0, ip, r3
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1551
	mla	r1, r5, r1, r4
	mla	lr, r5, lr, r4
	ldrb	r1, [r1, #1237]	@ zero_extendqisi2
	strb	r1, [lr, #1236]
	b	.L1547
.L1541:
	cmp	ip, #0
	beq	.L1539
	mvn	r2, #0
.L1564:
	strb	r2, [r3, #2775]
.L1539:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1567:
	.align	2
.L1566:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+2778
	.word	.LANCHOR0+2777
	.word	.LANCHOR0+2776
	.fnend
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1583
	mov	r3, #0
	movw	r1, #65535
.L1570:
	mov	ip, r3, asl #3
	uxth	r0, r3
	ldrh	ip, [ip, r2]
	cmp	ip, r1
	ldr	ip, .L1583
	bxeq	lr
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1570
	mov	r2, #0
	mov	r0, r3
	mov	r1, #32768
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.L1572:
	add	r3, ip, r2, asl #3
	uxth	lr, r2
	ldrh	r3, [r3, #2]
	tst	r3, #32768
	bne	.L1571
	cmp	r3, r1
	movcc	r1, r3
	movcc	r0, lr
.L1571:
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L1572
	cmp	r0, #32
	mov	r4, r0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r2, .L1583+4
	mov	r3, #0
	mvn	r1, #0
	sub	r2, r2, #2528
	ldrb	r5, [r2, #256]	@ zero_extendqisi2
.L1574:
	mov	lr, r3, asl #3
	uxth	r6, r3
	add	r0, ip, lr
	ldrh	r0, [r0, #2]
	cmp	r0, r1
	bcs	.L1573
	ldrh	lr, [lr, r2]
	cmp	lr, r5
	movne	r1, r0
	movne	r4, r6
.L1573:
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1574
	cmp	r4, #32
	bne	.L1575
	ldr	r1, .L1583+8
	movw	r2, #377
	ldr	r0, .L1583+12
	bl	printk
	bl	dump_stack
.L1575:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1584:
	.align	2
.L1583:
	.word	.LANCHOR3-2528
	.word	.LANCHOR3
	.word	.LANCHOR1+1804
	.word	.LC0
	.fnend
	.size	pm_select_ram_region, .-pm_select_ram_region
	.align	2
	.global	ftl_memset
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
	.type	flash_lsb_page_tbl_build, %function
flash_lsb_page_tbl_build:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L1630
	mov	r0, #1024
	ldr	r3, [r4, #1096]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	__aeabi_idiv
	cmp	r6, #0
	uxth	r5, r0
	bne	.L1587
	add	r4, r4, #4
.L1588:
	mov	r3, r6, asl #1
	strh	r6, [r3, r4]	@ movhi
	add	r6, r6, #1
	cmp	r6, #512
	bne	.L1588
.L1592:
	mov	r1, #255
	ldr	r0, .L1630+4
	mov	r2, #2048
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L1630+8
	ldr	r0, .L1630+4
	b	.L1589
.L1587:
	cmp	r6, #1
	bne	.L1590
	add	r4, r4, #4
	mov	r3, #0
.L1591:
	uxth	r2, r3
	mov	ip, r3, asl #1
	cmp	r2, #3
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	add	r3, r3, #1
	movne	r1, #2
	moveq	r1, #3
	cmp	r0, #0
	rsb	r1, r1, r2, asl #1
	movne	r2, r1
	cmp	r3, #512
	strh	r2, [ip, r4]	@ movhi
	bne	.L1591
	b	.L1592
.L1590:
	cmp	r6, #2
	bne	.L1593
	add	r4, r4, #4
	mov	r3, #0
.L1594:
	uxth	r2, r3
	mov	r1, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	r0, r2, asl #1
	subhi	r2, r0, #1
	cmp	r3, #512
	strh	r2, [r1, r4]	@ movhi
	bne	.L1594
	b	.L1592
.L1593:
	cmp	r6, #3
	bne	.L1595
	add	r4, r4, #4
	mov	r3, #0
.L1596:
	uxth	r2, r3
	mov	ip, r3, asl #1
	cmp	r2, #5
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	add	r3, r3, #1
	movne	r1, #4
	moveq	r1, #5
	cmp	r0, #0
	rsb	r1, r1, r2, asl #1
	movne	r2, r1
	cmp	r3, #512
	strh	r2, [ip, r4]	@ movhi
	bne	.L1596
	b	.L1592
.L1595:
	cmp	r6, #4
	mov	r3, #0
	bne	.L1597
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #1
	strh	r6, [r4, #12]	@ movhi
	strh	r3, [r4, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r4, #8]	@ movhi
	mov	r3, #3
	strh	r3, [r4, #10]	@ movhi
	mov	r3, #5
	strh	r3, [r4, #14]	@ movhi
	mov	r3, #7
	strh	r3, [r4, #16]	@ movhi
	mov	r3, #8
	strh	r3, [r4, #18]!	@ movhi
.L1598:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r4, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1598
	b	.L1592
.L1597:
	cmp	r6, #5
	bne	.L1599
	add	r4, r4, #4
.L1600:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L1600
	ldr	r2, .L1630+12
.L1601:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L1601
	b	.L1592
.L1599:
	cmp	r6, #8
	bne	.L1602
	add	r4, r4, #4
.L1603:
	strh	r3, [r3, r4]	@ movhi
	add	r3, r3, #2
	cmp	r3, #1024
	bne	.L1603
	b	.L1592
.L1602:
	cmp	r6, #9
	bne	.L1604
	strh	r3, [r4, #4]	@ movhi
	movw	r2, #1021
	mov	r3, #1
	strh	r3, [r4, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r4, #8]!	@ movhi
	mov	r3, #3
.L1605:
	strh	r3, [r4, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r2
	bne	.L1605
	b	.L1592
.L1604:
	cmp	r6, #10
	bne	.L1606
	add	r4, r4, #4
.L1607:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L1607
	ldr	r2, .L1630+16
	movw	r1, #961
.L1608:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L1608
	b	.L1592
.L1606:
	cmp	r6, #11
	bne	.L1609
	ldr	r2, .L1630+8
	mov	r3, #0
.L1610:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L1610
	ldr	r1, .L1630+20
.L1611:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1611
	b	.L1592
.L1609:
	cmp	r6, #13
	bne	.L1592
	ldr	r2, .L1630+24
	mov	r3, #0
.L1612:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #3
	uxth	r3, r3
	cmp	r3, #1536
	bne	.L1612
	b	.L1592
.L1589:
	uxth	r2, r3
	cmp	r2, r5
	bcs	.L1629
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L1589
.L1629:
	ldmfd	sp!, {r4, r5, r6, pc}
.L1631:
	.align	2
.L1630:
	.word	.LANCHOR0
	.word	.LANCHOR3-2268
	.word	.LANCHOR0+4
	.word	.LANCHOR0+34
	.word	.LANCHOR0+128
	.word	.LANCHOR0+18
	.word	.LANCHOR0+2
	.fnend
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.type	flash_die_info_init, %function
flash_die_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L1663
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1633
	ldr	r0, .L1663+4
	ldr	r1, .L1663+8
	bl	printk
.L1633:
	ldrh	r3, [r5, #30]
	mov	r7, #0
	ldr	r4, .L1663+12
	ldrb	r1, [r5, #16]	@ zero_extendqisi2
	ldrh	r0, [r5, #14]
	add	r10, r4, #1200
	strh	r3, [r4, #2]	@ movhi
	strb	r7, [r4, #1101]
	bl	__aeabi_idiv
	ldr	r6, .L1663+16
	mov	r1, r7
	mov	r2, #8
	ldr	r9, .L1663+20
	strh	r0, [r6, #-220]	@ movhi
	ldr	r0, .L1663+24
	bl	ftl_memset
	sub	r0, r6, #216
	mov	r1, r7
	mov	r2, #32
	bl	ftl_memset
.L1638:
	ldrb	r2, [r5, #4]	@ zero_extendqisi2
	add	r1, r10, r7, asl #3
	ldr	r8, .L1663+28
	mov	r3, #2
	ldr	r0, .L1663+32
	strb	r3, [r7, r9]
	mov	r3, #0
.L1634:
	cmp	r3, r2
	bcs	.L1661
	add	ip, r0, r3
	ldrb	lr, [ip, #1]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	cmp	lr, ip
	bne	.L1635
	add	r3, r3, #1
	b	.L1634
.L1661:
	ldrb	r3, [r4, #1101]	@ zero_extendqisi2
	mov	r1, #0
	uxtb	r0, r7
	add	r2, r6, r3, asl #2
	str	r1, [r2, #-216]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #1101]
	strb	r0, [r3, #1180]
	bl	zftl_flash_enter_slc_mode
.L1635:
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1638
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldr	r3, .L1663
	cmp	r2, #2
	beq	.L1639
.L1646:
	ldrb	r3, [r5, #17]	@ zero_extendqisi2
	ldrb	r2, [r4, #1101]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #18]
	smulbb	r3, r2, r3
	strh	r3, [r6, #-184]	@ movhi
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1639:
	ldrh	ip, [r3, #18]
	mov	lr, #0
	ldrh	r2, [r4, #2]
	add	r3, r3, #4
	and	ip, ip, #65280
	ldrb	r1, [r4, #1101]	@ zero_extendqisi2
	ldrb	r7, [r3]	@ zero_extendqisi2
	mul	r2, r2, ip
	ldrb	ip, [r3, #13]	@ zero_extendqisi2
	mul	ip, ip, r2
	ldrb	r2, [r3, #23]	@ zero_extendqisi2
	str	r2, [sp]
	mov	r2, ip, asl #1
	str	r2, [sp, #4]
	mov	r2, lr
.L1644:
	add	r9, r8, r2, asl #3
	mov	r0, #0
.L1640:
	cmp	r0, r7
	bcs	.L1662
	add	r10, r3, r0
	ldrb	fp, [r10, #1]	@ zero_extendqisi2
	ldrb	r10, [r9, r0]	@ zero_extendqisi2
	cmp	fp, r10
	bne	.L1641
	add	r0, r0, #1
	b	.L1640
.L1662:
	ldmia	sp, {r9, lr}
	mov	r0, r1
	add	r1, r6, r1, asl #2
	cmp	r9, #0
	moveq	lr, ip
	str	lr, [r1, #-216]
	add	r1, r0, #1
	add	r0, r4, r0
	mov	lr, #1
	uxtb	r1, r1
	strb	r2, [r0, #1180]
.L1641:
	add	r2, r2, #1
	cmp	r2, #4
	bne	.L1644
	cmp	lr, #0
	strneb	r1, [r4, #1101]
	b	.L1646
.L1664:
	.align	2
.L1663:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+1828
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1192
	.word	.LANCHOR0+1180
	.word	.LANCHOR0+1200
	.word	.LANCHOR2+4
	.fnend
	.size	flash_die_info_init, .-flash_die_info_init
	.align	2
	.global	lpa_hash_init
	.type	lpa_hash_init, %function
lpa_hash_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r1, #255
	ldr	r4, .L1667
	mov	r2, #512
	sub	r0, r4, #3056
	sub	r0, r0, #4
	bl	ftl_memset
	sub	r3, r4, #3088
	ldr	r0, [r4, #-2544]
	mov	r1, #255
	ldrh	r2, [r3, #-8]
	ldrb	r3, [r4, #-3123]	@ zero_extendqisi2
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	ldmfd	sp!, {r4, lr}
	b	ftl_memset
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR3
	.fnend
	.size	lpa_hash_init, .-lpa_hash_init
	.align	2
	.global	lpa_rebuild_hash
	.type	lpa_rebuild_hash, %function
lpa_rebuild_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L1682
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1670
	ldr	r0, .L1682+4
	mov	r2, #239
	ldr	r1, .L1682+8
	mov	r3, #0
	bl	printk
.L1670:
	ldr	r4, .L1682+12
	mov	r1, #255
	mov	r2, #512
	ldr	r0, .L1682+16
	sub	r5, r4, #3088
	bl	ftl_memset
	ldrb	r3, [r4, #-3123]	@ zero_extendqisi2
	mov	r1, #255
	ldrh	r2, [r5, #-8]
	ldr	r0, [r4, #-2544]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	sub	lr, r5, #8
	add	r5, r5, #28
	mov	r0, #0
	mov	ip, r4
.L1671:
	ldrh	r1, [lr]
	uxth	r2, r0
	ldrb	r3, [r4, #-3123]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asl #1
	bge	.L1681
	ldr	r3, [ip, #-2548]
	ldr	r3, [r3, r2, asl #2]
	cmn	r3, #1
	beq	.L1672
	uxtb	r3, r3
	mov	r3, r3, asl #1
	ldrh	r1, [r5, r3]
	strh	r2, [r5, r3]	@ movhi
	mov	r2, r2, asl #1
	ldr	r3, [ip, #-2544]
	strh	r1, [r3, r2]	@ movhi
.L1672:
	add	r0, r0, #1
	b	.L1671
.L1681:
	ldmfd	sp!, {r3, r4, r5, pc}
.L1683:
	.align	2
.L1682:
	.word	.LANCHOR2
	.word	.LC125
	.word	.LANCHOR1+1848
	.word	.LANCHOR3
	.word	.LANCHOR3-3060
	.fnend
	.size	lpa_rebuild_hash, .-lpa_rebuild_hash
	.align	2
	.global	zftl_read_flash_info
	.type	zftl_read_flash_info, %function
zftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r1, #0
	mov	r2, #11
	mov	r4, r0
	bl	ftl_memset
	ldr	r2, .L1689
	ldr	r3, .L1689+4
	mov	ip, #1
	ldrh	r1, [r2, #2]
	ldrb	r0, [r3, #13]	@ zero_extendqisi2
	smulbb	r1, r0, r1
	ldr	r0, .L1689+8
	strh	r1, [r4, #4]	@ unaligned
	ldrb	r1, [r2, #1172]	@ zero_extendqisi2
	strb	r1, [r4, #7]
	ldr	r1, [r2, #1032]
	str	r1, [r4]	@ unaligned
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	ldrb	r3, [r3, #11]	@ zero_extendqisi2
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #1101]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L1685:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L1688
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L1685
.L1688:
	ldmfd	sp!, {r4, pc}
.L1690:
	.align	2
.L1689:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1180
	.fnend
	.size	zftl_read_flash_info, .-zftl_read_flash_info
	.align	2
	.global	gc_init
	.type	gc_init, %function
gc_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movw	r9, #2828
	ldr	r6, .L1693
	mov	r5, #0
	ldr	r4, .L1693+4
	mov	r1, r5
	add	r7, r6, r9
	movw	r2, #2204
	sub	r8, r4, #3088
	mov	r0, r7
	strb	r5, [r4, #-3115]
	strb	r5, [r4, #-182]
	str	r5, [r4, #-180]
	strh	r5, [r8]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r8, #-8]
	mvn	r3, #0
	strh	r3, [r6, r9]	@ movhi
	movw	ip, #2812
	str	r5, [r6, #2836]
	mov	r3, r2, lsr #1
	strh	r3, [r7, #34]	@ movhi
	ldrb	r3, [r4, #-3123]	@ zero_extendqisi2
	mov	r0, r2, lsr #2
	strh	r0, [r7, #32]	@ movhi
	sub	r7, r4, #3104
	smulbb	r1, r3, r2
	strh	r5, [r7, #-8]	@ movhi
	strh	r5, [r7, #-6]	@ movhi
	strh	r5, [r7, #-4]	@ movhi
	uxth	r1, r1
	sub	lr, r1, #32
	strh	lr, [r6, ip]	@ movhi
	movw	ip, #2810
	strh	r1, [r6, ip]	@ movhi
	add	r1, r6, #2816
	strh	r2, [r1]	@ movhi
	mov	r2, #4
	strh	r2, [r4, #-176]	@ movhi
	movw	r2, #2814
	strh	r0, [r6, r2]	@ movhi
	sub	r4, r4, #3072
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #2900]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r3, [r7, #-10]
	str	r0, [r4, #2904]
	ldrb	r0, [r4, #-51]	@ zero_extendqisi2
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #-48]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #-56]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	movw	r2, #2808
	str	r0, [r4, #2908]
	ldrh	r3, [r4, #-14]
	mov	r3, r3, lsr #2
	strh	r3, [r6, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1694:
	.align	2
.L1693:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_init, .-gc_init
	.align	2
	.global	gc_static_wearleveling
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L1814
	ldr	r3, [r4, #2804]
	ldr	r3, [r3, #32]
	cmp	r3, #10240
	bls	.L1696
	bl	ftl_tmp_into_update
.L1696:
	ldr	r3, [r4, #1092]
	ldr	r2, [r3, #568]
	ldr	r0, [r3, #12]
	add	r1, r2, #35840
	add	r1, r1, #160
	cmp	r0, r1
	bcs	.L1697
	ldr	r1, .L1814
	ldr	ip, [r1, #2804]
	ldr	r1, [r3, #572]
	ldr	ip, [ip, #36]
	add	r1, r1, #256
	cmp	ip, r1
	movcc	r0, #0
	bcc	.L1806
.L1697:
	add	r2, r2, #860160
	add	r2, r2, #3840
	cmp	r0, r2
	bhi	.L1699
	ldr	r1, [r4, #2804]
	ldr	r2, [r3, #572]
	ldr	r1, [r1, #36]
	add	r2, r2, #32
	cmp	r1, r2
	movls	r7, #0
	movls	r9, r7
	bls	.L1700
.L1699:
	ldr	r2, [r4, #2804]
	mov	r8, #0
	ldr	ip, .L1814
	movw	r5, #65535
	mov	fp, r8
	mov	r9, r8
	ldr	r1, [r2, #36]
	mov	r10, r8
	str	r0, [r3, #568]
	mov	r6, r5
	str	r8, [sp, #32]
	str	r1, [r3, #572]
	ldrh	r7, [r2, #134]
	str	r8, [sp, #28]
	str	r8, [sp, #24]
	str	r8, [sp, #20]
.L1701:
	ldr	r2, .L1814+4
	ldr	r3, .L1814
	ldrh	r2, [r2]
	cmp	r2, r7
	bls	.L1811
	ldr	r0, [ip, #1080]
	mov	r1, r7, asl #2
	add	lr, r0, r1
	ldrb	r3, [lr, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	cmp	r2, #224
	beq	.L1702
	tst	r3, #8
	ldrneh	r3, [r0, r1]
	ldrne	r4, [r0, r1]
	ubfxne	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1704
.L1703:
	tst	r3, #24
	ldreqh	r3, [r0, r1]
	movweq	r4, #65535
	ldrne	r4, [r0, r1]
	movwne	r3, #65535
	ubfxeq	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1706
.L1704:
	ldr	r2, [sp, #32]
	add	r9, r9, #1
	cmp	r5, r3
	add	r2, r2, r3
	uxth	r9, r9
	str	r2, [sp, #32]
	ldrhi	r2, [ip, #1092]
	movhi	r5, r3
	addhi	r2, r2, #584
	strhih	r7, [r2, #2]	@ movhi
.L1748:
	ldr	r2, [sp, #20]
	cmp	r2, r3
	movw	r2, #65535
	strcc	r3, [sp, #20]
	movcc	r8, r7
	cmp	r4, r2
	beq	.L1708
.L1706:
	ldr	r2, [sp, #24]
	cmp	r6, r4
	add	r2, r2, #1
	movhi	r6, r4
	uxth	r2, r2
	str	r2, [sp, #24]
	ldr	r2, [sp, #28]
	add	r2, r2, r4
	str	r2, [sp, #28]
	ldrhi	r2, [ip, #1092]
	addhi	r2, r2, #584
	strhih	r7, [r2]	@ movhi
	cmp	r10, r4
	movcc	fp, r7
	movcc	r10, r4
.L1708:
	cmp	r4, #9
	cmphi	r3, #9
	bhi	.L1702
	ldr	r3, .L1814+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1702
	ldrh	r2, [r0, r1]
	ldr	r3, [r0, r1]
	ldrb	r1, [lr, #2]	@ zero_extendqisi2
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	str	ip, [sp, #36]
	ubfx	r1, r1, #3, #2
	str	r1, [sp]
	ldrb	r1, [lr, #2]	@ zero_extendqisi2
	mov	r1, r1, lsr #5
	str	r1, [sp, #4]
	ldrb	r1, [lr, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	mov	r1, r7, asl #1
	ldr	r0, [ip, #1088]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1814+12
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1702:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1701
.L1811:
	ldr	r0, [r3, #1092]
	mov	r2, #0
	mov	r1, #255
	str	r3, [sp, #36]
	add	r0, r0, #264
	strh	r2, [r0, #-142]	@ movhi
	mov	r2, #128
	bl	ftl_memset
	movw	r2, #586
	ldr	r3, [sp, #36]
	ldr	r1, [r3, #1092]
	ldr	r7, [r3, #1080]
	ldrh	r1, [r1, r2]
	ldr	r2, .L1814+8
	mov	ip, r1, asl #2
	ldr	r2, [r2]
	add	r4, r7, ip
	tst	r2, #1024
	beq	.L1711
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	ldrh	r2, [r7, ip]
	ldr	lr, [r7, r1, asl #2]
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ubfx	r2, r2, #0, #11
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	str	ip, [sp, #36]
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r0, [r4, #3]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, [r3, #1088]
	mov	r3, r1, asl #1
	ldrh	r3, [r0, r3]
	ldr	r0, .L1814+16
	str	r3, [sp, #12]
	ubfx	r3, lr, #11, #8
	bl	printk
	ldr	ip, [sp, #36]
.L1711:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r1, .L1814
	and	r3, r3, #224
	cmp	r3, #32
	bne	.L1712
	ldr	r3, [r1, #2804]
	add	r3, r3, #688
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrhi	r3, .L1814+20
	movhi	r2, #1
	strhi	r2, [r3, #-160]
.L1712:
	ldrb	r2, [r4, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L1713
	ldr	r3, [r1, #2804]
	ldr	r0, .L1814+20
	ldr	r4, .L1814
	ldrh	r1, [r3, #96]
	ldrh	r0, [r0, #-156]
	ldrh	r3, [r7, ip]
	ubfx	r3, r3, #0, #11
	add	r3, r3, r0, lsr #2
	cmp	r1, r3
	ble	.L1713
	and	r3, r2, #192
	cmp	r3, #64
	bne	.L1714
	ldr	r2, [r4, #1092]
	movw	r3, #586
	mov	r1, #0
	ldrh	r0, [r2, r3]
	mov	r2, #1
	bl	gc_add_sblk
	ldr	r2, .L1814+24
	movw	r3, #2180
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1713
.L1714:
	tst	r2, #224
	bne	.L1713
	ldr	r3, [r4, #1092]
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1713
	movw	r7, #586
	str	r2, [sp, #36]
	ldrh	r0, [r3, r7]
	add	r3, r3, #588
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L1713
	bl	zftl_remove_free_node
	ldr	r3, [r4, #1092]
	ldrh	r1, [r3, r7]
	ldr	r2, [sp, #36]
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
.L1713:
	ldr	r1, .L1814
	ldr	r2, .L1814+8
	ldr	r3, [r1, #1092]
	ldr	r2, [r2]
	add	r3, r3, #584
	ldr	ip, [r1, #1080]
	tst	r2, #1024
	ldrh	r7, [r3]
	mov	r3, r7, asl #2
	add	r4, ip, r3
	beq	.L1715
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	ldrh	r2, [ip, r3]
	ubfx	r0, r0, #3, #2
	ldr	r3, [ip, r7, asl #2]
	str	r0, [sp]
	ubfx	r2, r2, #0, #11
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	ubfx	r3, r3, #11, #8
	str	ip, [sp, #36]
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r0, [r4, #3]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, [r1, #1088]
	mov	r1, r7, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1814+28
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1715:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r4, .L1814
	tst	r3, #8
	beq	.L1716
	ldr	r2, [r4, #2804]
	ldr	r0, .L1814+20
	ldrh	r1, [r2, #98]
	ldrh	r0, [r0, #-154]
	ldr	r2, [ip, r7, asl #2]
	ubfx	r2, r2, #11, #8
	add	r2, r2, r0, lsr #2
	cmp	r1, r2
	ble	.L1716
	and	r2, r3, #192
	cmp	r2, #64
	bne	.L1717
	ldr	r3, [r4, #1092]
	mov	r1, #0
	mov	r2, #1
	add	r3, r3, #584
	ldrh	r0, [r3]
	bl	gc_add_sblk
	ldr	r2, .L1814+24
	movw	r3, #2180
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1716
.L1717:
	and	r3, r3, #248
	cmp	r3, #16
	bne	.L1716
	ldr	r3, [r4, #1092]
	add	r2, r3, #588
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1716
	add	r2, r3, #584
	ldrh	r0, [r2]
	movw	r2, #590
	ldrh	r3, [r3, r2]
	cmp	r3, r0
	beq	.L1716
	bl	zftl_remove_free_node
	ldr	r3, [r4, #1092]
	add	r2, r3, #588
	add	r3, r3, #584
	ldrh	r1, [r3]
	strh	r1, [r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L1716:
	ldr	r3, .L1814+8
	ldr	r0, [r4, #1080]
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L1718
	mov	r3, r8, asl #2
	add	r1, r0, r3
	ldrh	r2, [r0, r3]
	ldr	r3, [r0, r8, asl #2]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r1, [r1, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldr	r1, .L1814
	ldr	r0, [r1, #1088]
	mov	r1, r8, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1814+32
	str	r1, [sp, #12]
	mov	r1, r8
	bl	printk
.L1718:
	ldr	r3, .L1814+8
	ldr	ip, .L1814
	ldr	r3, [r3]
	ldr	r0, [ip, #1080]
	tst	r3, #1024
	beq	.L1719
	mov	r3, fp, asl #2
	add	r1, r0, r3
	ldrh	r2, [r0, r3]
	ldr	r3, [r0, fp, asl #2]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r1, [r1, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	mov	r1, fp, asl #1
	ldr	r0, [ip, #1088]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1814+36
	str	r1, [sp, #12]
	mov	r1, fp
	bl	printk
.L1719:
	ldr	r4, [r4, #2804]
	mov	r1, r9
	ldrh	r3, [sp, #20]
	ldr	r0, [sp, #32]
	strh	r10, [r4, #98]	@ movhi
	strh	r3, [r4, #96]	@ movhi
	strh	r5, [r4, #92]	@ movhi
	strh	r6, [r4, #94]	@ movhi
	bl	__aeabi_uidiv
	strh	r0, [r4, #88]	@ movhi
	mov	r7, r0
	ldr	r1, [sp, #24]
	ldr	r0, [sp, #28]
	bl	__aeabi_uidiv
	strh	r0, [r4, #90]	@ movhi
	ldr	r4, .L1814+8
	ldr	r3, [r4]
	tst	r3, #1024
	beq	.L1720
	uxth	r0, r0
	mov	r1, r9
	str	r0, [sp]
	uxth	r3, r7
	ldr	r0, .L1814+40
	ldr	r2, [sp, #24]
	bl	printk
.L1720:
	ldr	r3, [r4]
	ldr	r4, .L1814+20
	tst	r3, #1024
	beq	.L1721
	str	r10, [sp]
	mov	r1, r5
	ldrh	r3, [r4, #-156]
	mov	r2, r6
	ldr	r0, .L1814+44
	str	r3, [sp, #4]
	ldrh	r3, [r4, #-154]
	str	r3, [sp, #8]
	ldr	r3, [sp, #20]
	bl	printk
.L1721:
	rsb	r3, r6, r10
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldrh	r3, [r4, #-154]
	cmp	r2, r3
	bgt	.L1722
	ldr	r2, .L1814+20
	ldr	r3, [sp, #20]
	ldrh	r2, [r2, #-156]
	rsb	r3, r5, r3
	cmp	r3, r2
	movle	r7, #0
	movle	r9, r7
	ble	.L1723
.L1722:
	ldr	ip, .L1814
	mov	r7, #0
	mov	r9, r7
	ldr	r3, [ip, #1092]
	mov	fp, ip
	ldr	r4, [r3, #580]
	ldr	r3, [ip, #2804]
	uxth	r4, r4
	ldrh	r10, [r3, #134]
.L1724:
	ldr	r3, .L1814+4
	ldrh	r3, [r3]
	cmp	r10, r3
	bcs	.L1733
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r3
	ldr	r3, [fp, #1080]
	movcs	r4, #0
	mov	r8, r4, asl #2
	add	r2, r3, r8
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r0, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r0, #224
	orreq	r1, r1, #1
	cmp	r1, #0
	bne	.L1726
	ubfx	r2, r2, #3, #2
	ands	r1, r2, #1
	beq	.L1727
	cmp	r0, #160
	b	.L1809
.L1727:
	cmp	r2, #2
.L1809:
	bne	.L1729
	ldr	r2, .L1814+20
	ldr	r0, [sp, #24]
	ldrh	r2, [r2, #-154]
	cmp	r0, r2
	ble	.L1730
	ldr	r2, [r3, r8]
	ubfx	r2, r2, #11, #8
	cmp	r2, r6
	bls	.L1731
	cmp	r1, #0
	beq	.L1730
	ldrh	r2, [r3, r8]
	ubfx	r2, r2, #0, #11
	cmp	r2, r5
	bgt	.L1730
.L1731:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1814+48
	mov	r1, #1
	add	r9, r9, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1814+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1730
	ldr	r0, [fp, #1088]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1080]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1814+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1814+52
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
	b	.L1810
.L1729:
	ldr	r0, .L1814+20
	ldr	r2, [sp, #20]
	ldrh	r0, [r0, #-156]
	rsb	r2, r5, r2
	cmp	r2, r0
	ble	.L1730
	ldrh	r2, [r3, r8]
	add	r0, r5, #8
	ubfx	r2, r2, #0, #11
	cmp	r2, r0
	ble	.L1732
	cmp	r1, #0
	beq	.L1730
	ldr	r2, [r3, r8]
	add	r1, r6, #4
	ubfx	r2, r2, #11, #8
	cmp	r2, r1
	bgt	.L1730
.L1732:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1814+48
	mov	r1, #1
	add	r7, r7, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1814+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1730
	ldr	r0, [fp, #1088]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1080]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1814+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1814+56
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
.L1810:
	mov	r1, r4
	mov	r2, r2, lsr #5
	mov	r3, lr
	bl	printk
.L1730:
	cmp	r9, #4
	cmpls	r7, #4
	bhi	.L1733
.L1726:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1724
.L1733:
	ldr	r3, .L1814
	ldr	r3, [r3, #1092]
	str	r4, [r3, #580]
.L1723:
	cmp	r6, #0
	beq	.L1736
	ldr	r0, .L1814
	ldr	r4, .L1814+4
	ldr	r3, [r0, #2804]
	ldrh	r3, [r3, #134]
.L1737:
	ldrh	r2, [r4]
	cmp	r2, r3
	bls	.L1812
	ldr	ip, [r0, #1080]
	mov	lr, r3, asl #2
	add	r8, ip, lr
	ldr	r2, [ip, r3, asl #2]
	ubfx	r1, r2, #11, #8
	cmp	r1, r6
	bcc	.L1738
	ldrb	r8, [r8, #2]	@ zero_extendqisi2
	tst	r8, #24
	rsbne	r1, r6, r1
	bfine	r2, r1, #11, #8
	strne	r2, [ip, lr]
.L1738:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1737
.L1812:
	ldr	r3, .L1814
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #72]
	add	r2, r6, r2
	strh	r2, [r3, #72]	@ movhi
	ldrh	r2, [r3, #98]
	cmp	r2, r6
	rsbhi	r2, r6, r2
	strhih	r2, [r3, #98]	@ movhi
.L1736:
	cmp	r5, #0
	beq	.L1742
	ldr	ip, .L1814
	ldr	r6, .L1814+4
	ldr	r3, [ip, #2804]
	ldrh	r3, [r3, #134]
.L1743:
	ldrh	r2, [r6]
	cmp	r2, r3
	bls	.L1813
	ldr	r4, [ip, #1080]
	mov	lr, r3, asl #2
	add	r0, r4, lr
	ldrh	r2, [r4, lr]
	ubfx	r1, r2, #0, #11
	cmp	r1, r5
	blt	.L1744
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	and	r0, r0, #24
	cmp	r0, #16
	rsbne	r1, r5, r1
	bfine	r2, r1, #0, #11
	strneh	r2, [r4, lr]	@ movhi
.L1744:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1743
.L1813:
	ldr	r3, .L1814
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #74]
	add	r2, r5, r2
	strh	r2, [r3, #74]	@ movhi
	ldrh	r2, [r3, #96]
	cmp	r2, r5
	rsbhi	r2, r5, r2
	strhih	r2, [r3, #96]	@ movhi
.L1742:
	ldr	r4, .L1814+60
	mov	r1, #0
	sub	r0, r4, #12
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1700
	ldr	r2, .L1814
	mov	r3, r0, asl #1
	ldr	r1, [r2, #1088]
	ldrh	r2, [r4, #-8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #1
	bhi	.L1700
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	add	r9, r9, #1
.L1700:
	add	r0, r7, r9
.L1806:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1815:
	.align	2
.L1814:
	.word	.LANCHOR0
	.word	.LANCHOR0+1076
	.word	.LANCHOR2
	.word	.LC126
	.word	.LC127
	.word	.LANCHOR3
	.word	.LANCHOR0+2828
	.word	.LC128
	.word	.LC129
	.word	.LC130
	.word	.LC131
	.word	.LC132
	.word	.LANCHOR0+5008
	.word	.LC133
	.word	.LC134
	.word	.LANCHOR3-3088
	.fnend
	.size	gc_static_wearleveling, .-gc_static_wearleveling
	.align	2
	.global	zftl_sblk_list_init
	.type	zftl_sblk_list_init, %function
zftl_sblk_list_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1076
	ldr	r5, .L1849
	mov	r2, #6
	ldr	r6, .L1849+4
	mov	r1, #0
	.pad #20
	sub	sp, sp, #20
	mov	fp, #0
	ldrh	r3, [r5, r3]
	mov	r4, fp
	ldr	r0, [r5, #1036]
	mov	r8, r5
	ldr	r10, .L1849+8
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r3, r6, #3056
	mov	r2, #32
	str	fp, [r6, #-3080]
	strh	r2, [r3, #-12]	@ movhi
	movw	r3, #2788
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2790
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2792
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2794
	strh	fp, [r5, r3]	@ movhi
	sub	r2, r6, #3104
	movw	r3, #2798
	str	fp, [r6, #-3076]
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2796
	strh	fp, [r5, r3]	@ movhi
	mov	r0, #32768
	ldrb	r3, [r6, #-3123]	@ zero_extendqisi2
	ldrh	r1, [r2, #-10]
	str	fp, [r6, #-3072]
	str	fp, [r6, #-3104]
	mul	r1, r1, r3
	str	fp, [r6, #-3092]
	str	fp, [r6, #-3100]
	bl	__aeabi_idiv
	uxth	r3, r0
	str	r3, [sp, #4]
	ldr	r3, [r5, #2804]
	str	r6, [sp, #8]
	ldrh	r7, [r3, #134]
	strh	fp, [r3, #146]	@ movhi
.L1817:
	ldr	r2, .L1849+12
	sxth	ip, r7
	ldr	r3, .L1849
	ldrh	r2, [r2]
	cmp	ip, r2
	bge	.L1847
	ldr	r6, [r8, #1080]
	add	r6, r6, ip, asl #2
	ldrb	r3, [r6, #3]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r1, [sp, #4]
	beq	.L1818
	ldr	r3, [sp, #8]
	ldrh	fp, [r10]
	ldr	r0, [r8, #2804]
	ldrb	lr, [r3, #-3123]	@ zero_extendqisi2
	mov	r3, #0
	mov	r1, r3
.L1819:
	cmp	r3, lr
	bge	.L1848
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	mov	r2, r2, asr r3
	add	r3, r3, #1
	tst	r2, #1
	addeq	r1, fp, r1
	ldrneh	r2, [r0, #146]
	uxtheq	r1, r1
	addne	r2, r2, #1
	strneh	r2, [r0, #146]	@ movhi
	b	.L1819
.L1848:
	cmp	r1, #0
	beq	.L1823
	sxth	r1, r1
	mov	r0, #32768
	str	ip, [sp, #12]
	bl	__aeabi_idiv
	add	r0, r0, #1
	ldr	ip, [sp, #12]
	uxth	r1, r0
	b	.L1818
.L1823:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	mvn	r0, #0
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	ldr	r2, [r5, #1088]
	mov	r3, ip, asl #1
	strh	r0, [r2, r3]	@ movhi
.L1818:
	mov	r9, ip, asl #1
	ldr	r0, [r5, #1036]
	add	r2, r9, ip
	mov	r2, r2, asl #1
	add	lr, r0, r2
	strh	r1, [lr, #4]	@ movhi
	mvn	r1, #0
	strh	r1, [lr, #2]	@ movhi
	strh	r1, [r0, r2]	@ movhi
	ldrb	r2, [r6, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #32
	cmpne	r2, #224
	moveq	fp, #1
	movne	fp, #0
	beq	.L1824
	ldr	r1, [r8, #1092]
	ldrh	r0, [r1, #16]
	cmp	ip, r0
	beq	.L1824
	ldrh	r0, [r1, #48]
	cmp	ip, r0
	beq	.L1824
	ldrh	r1, [r1, #80]
	cmp	ip, r1
	beq	.L1824
	cmp	r2, #64
	uxtheq	r6, r7
	ldreq	r0, .L1849+16
	ldreq	r2, .L1849+20
	moveq	r1, r6
	beq	.L1845
.L1825:
	cmp	r2, #96
	uxtheq	r6, r7
	ldreq	r0, .L1849+24
	ldreq	r2, .L1849+28
	moveq	r1, r6
	beq	.L1845
.L1826:
	cmp	r2, #160
	bne	.L1827
	uxth	r6, r7
	ldr	r0, .L1849+32
	ldr	r2, .L1849+36
	mov	r1, r6
.L1845:
	bl	_insert_data_list
	ldr	r2, [r8, #1088]
	ldrh	r3, [r2, r9]
	cmp	r3, #7
	movls	r0, r6
	movls	r1, #1
	movls	r2, fp
	bhi	.L1824
	b	.L1843
.L1827:
	cmp	r2, #0
	bne	.L1824
	ldr	r2, [r8, #1088]
	uxth	fp, r7
	ldrh	r2, [r2, r9]
	cmp	r2, #0
	beq	.L1828
	sxth	r3, r4
	cmp	r3, #2
	bgt	.L1829
	mov	r1, ip
	ldr	r0, .L1849+40
	bl	printk
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	add	r4, r4, #1
	mov	r0, fp
	tst	r3, #16
	mov	r1, #1
	uxth	r4, r4
	movne	r2, #5
	moveq	r2, #2
	bfi	r3, r2, #5, #3
	mov	r2, #0
	strb	r3, [r6, #2]
.L1843:
	bl	gc_add_sblk
	b	.L1824
.L1829:
	ldr	r1, .L1849+44
	mov	r2, #656
	ldr	r0, .L1849+48
	bl	printk
	bl	dump_stack
.L1828:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1849+52
	moveq	r1, fp
	ldreq	r2, .L1849+56
	beq	.L1841
.L1831:
	cmp	r3, #16
	ldreq	r0, .L1849+60
	moveq	r1, fp
	ldreq	r2, .L1849+64
	movne	r1, fp
	ldrne	r0, .L1849+68
	ldrne	r2, .L1849+72
.L1841:
	bl	_insert_free_list
.L1824:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1817
.L1847:
	movw	r1, #2788
	ldr	r2, [r3, #2804]
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #114]	@ movhi
	movw	r1, #2790
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #118]	@ movhi
	movw	r1, #2792
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #116]	@ movhi
	movw	r1, #2794
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #122]	@ movhi
	movw	r1, #2798
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #120]	@ movhi
	movw	r1, #2796
	ldrh	r3, [r3, r1]
	strh	r3, [r2, #124]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1850:
	.align	2
.L1849:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3114
	.word	.LANCHOR0+1076
	.word	.LANCHOR3-3092
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3100
	.word	.LANCHOR0+2798
	.word	.LC135
	.word	.LANCHOR1+1868
	.word	.LC0
	.word	.LANCHOR3-3080
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3076
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.fnend
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	pm_free_sblk
	.type	pm_free_sblk, %function
pm_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 272
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #284
	sub	sp, sp, #284
	ldr	r5, .L1881
	ldrh	r3, [r5, #-152]
	cmp	r3, #128
	bls	.L1852
	ldr	r1, .L1881+4
	mov	r2, #94
	ldr	r0, .L1881+8
	bl	printk
	bl	dump_stack
.L1852:
	ldr	r4, .L1881+12
	mov	r1, #0
	ldrh	lr, [r5, #-152]
	mov	r0, r1
	movw	r6, #65535
	ldr	r2, [r4, #2804]
	add	r3, r2, #688
	add	r2, r2, #412
	add	r2, r2, #2
	ldrh	ip, [r3]
.L1855:
	ldrh	r7, [r2, #2]!
	uxth	r3, r1
	cmp	r7, r6
	addne	r0, r0, #1
	uxthne	r0, r0
	cmp	r0, ip
	bcs	.L1854
	cmp	r0, lr
	bcs	.L1854
	add	r1, r1, #1
	cmp	r1, #128
	bne	.L1855
	mov	r3, r1
.L1854:
	add	r3, r3, #1
	add	r0, sp, #24
	mov	r1, #0
	mov	r2, #256
	uxth	r3, r3
	mvn	r8, #0
	cmp	r3, #128
	mov	r6, #0
	movcs	r3, #128
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r7, [r4, #2804]
	movw	r3, #698
	ldrb	ip, [r5, #-3130]	@ zero_extendqisi2
	add	r10, r7, #700
	ldrh	fp, [r7, r3]
	ldr	r3, .L1881+16
	ldrh	r9, [r3, #-12]
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r9, r3
	mvn	r8, r8, asl r3
.L1856:
	uxth	r3, r6
	cmp	r3, fp
	bcs	.L1878
	ldr	r0, [r10, #4]!
	mov	r1, ip
	str	ip, [sp, #16]
	and	r0, r8, r0, lsr r9
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r3, r0
	ldr	ip, [sp, #16]
	str	r3, [sp, #12]
	add	r3, r7, #412
	add	r3, r3, #2
	add	r2, sp, #24
.L1857:
	ldr	r0, [sp, #8]
	uxth	lr, r1
	cmp	lr, r0
	bcs	.L1879
	ldrh	lr, [r3, #2]!
	add	r1, r1, #1
	ldr	r0, [sp, #12]
	add	r2, r2, #2
	cmp	lr, r0
	ldreqh	lr, [r2, #-2]
	addeq	lr, lr, #1
	streqh	lr, [r2, #-2]	@ movhi
	b	.L1857
.L1879:
	add	r6, r6, #1
	b	.L1856
.L1878:
	ldr	r2, .L1881+20
	mov	r6, #0
	ldrb	fp, [r5, #-3123]	@ zero_extendqisi2
	add	r8, sp, #24
	movw	r10, #65535
	mov	r9, r6
	ldrh	r3, [r2, #-8]
	smulbb	fp, fp, r3
	add	r3, r2, #3088
	str	r3, [sp, #12]
	uxth	r7, fp
.L1861:
	ldr	r3, [sp, #8]
	uxth	fp, r6
	cmp	fp, r3
	bcs	.L1880
	add	r3, r6, #208
	ldr	ip, [r4, #2804]
	ldrb	r1, [r5, #-3130]	@ zero_extendqisi2
	mov	r3, r3, asl #1
	add	r2, ip, #692
	str	ip, [sp, #16]
	ldrh	r3, [ip, r3]
	ldrh	r0, [r2]
	str	r3, [sp, #20]
	bl	__aeabi_idiv
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #16]
	cmp	r0, r3
	movw	r0, #65535
	ldreq	r2, [sp, #12]
	ldreqb	r1, [r2, #-3123]	@ zero_extendqisi2
	ldreq	r2, .L1881+24
	ldreqh	r2, [r2]
	smulbbeq	r2, r1, r2
	streqh	r2, [r8]	@ movhi
	ldrh	r2, [r8]
	cmp	r2, #0
	cmpne	r7, r2
	movhi	r1, #1
	movls	r1, #0
	cmp	r1, #0
	ldrh	r1, [ip, #74]
	movne	r9, fp
	movne	r7, r2
	cmp	r3, r0
	cmpne	r1, #2
	bls	.L1864
	ldr	r0, [r4, #1080]
	mov	r1, r3, asl #2
	ldrh	r1, [r0, r1]
	ldrh	r0, [ip, #92]
	ubfx	r1, r1, #0, #11
	add	r0, r0, #4
	cmp	r1, r0
	bgt	.L1864
	str	r1, [sp]
	mov	r1, r6
	ldr	r0, .L1881+28
	mov	r10, fp
	bl	printk
.L1864:
	ldrh	r2, [r8]
	cmp	r2, #0
	bne	.L1865
	add	fp, r6, #208
	ldr	r1, [r4, #2804]
	movw	r0, #65535
	mov	r3, fp, asl #1
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L1865
	ldr	r0, .L1881+32
	ldr	r0, [r0]
	tst	r0, #4096
	beq	.L1866
	add	r1, r1, #688
	ldr	r0, .L1881+36
	ldrh	r1, [r1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1866:
	ldr	r3, [r4, #2804]
	mov	fp, fp, asl #1
	ldrh	r0, [r3, fp]
	bl	ftl_free_sblk
	ldr	r3, [r4, #2804]
	mvn	r2, #0
	strh	r2, [r3, fp]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	sub	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L1865:
	add	r6, r6, #1
	add	r8, r8, #2
	b	.L1861
.L1880:
	movw	r0, #65535
	cmp	r10, r0
	movne	r0, r10
	moveq	r0, r9
	add	sp, sp, #284
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1882:
	.align	2
.L1881:
	.word	.LANCHOR3
	.word	.LANCHOR1+1888
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3-3120
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3096
	.word	.LC136
	.word	.LANCHOR2
	.word	.LC137
	.fnend
	.size	pm_free_sblk, .-pm_free_sblk
	.align	2
	.global	ftl_memcpy
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
	.type	flash_info_data_init, %function
flash_info_data_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1890
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1885
	ldr	r0, .L1890+4
	mov	r1, #120
	ldr	r2, .L1890+8
	bl	printk
.L1885:
	ldr	r4, .L1890+12
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1176]
	bl	ftl_memset
	ldr	r3, [r4, #1176]
	ldr	r2, .L1890+16
	ldr	r1, .L1890+20
	str	r2, [r3]
	mov	r3, #2032
	ldr	r0, [r4, #1176]
	mov	r2, #32
	add	r0, r0, #80
	str	r3, [r0, #-72]
	mov	r3, #1
	strh	r3, [r0, #-64]	@ movhi
	bl	ftl_memcpy
	ldr	r0, [r4, #1176]
	ldr	r1, .L1890+24
	mov	r2, #32
	add	r0, r0, #48
	ldmfd	sp!, {r4, lr}
	b	ftl_memcpy
.L1891:
	.align	2
.L1890:
	.word	.LANCHOR2
	.word	.LC138
	.word	.LANCHOR1+1904
	.word	.LANCHOR0
	.word	1398362953
	.word	.LANCHOR0+1103
	.word	.LANCHOR2+4
	.fnend
	.size	flash_info_data_init, .-flash_info_data_init
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #0
.L1893:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1893
.L1895:
	bx	lr
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	2
	.global	ftl_memcmp
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
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1898
	ldr	r0, [r3]
	ldr	r3, .L1898+4
	ldr	r3, [r3, #-144]
	rsb	r0, r3, r0
	b	jiffies_to_msecs
.L1899:
	.align	2
.L1898:
	.word	jiffies
	.word	.LANCHOR3
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #512
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	mov	r1, #0
	bl	ftl_memset
	bl	rknand_device_lock
	ldr	r3, .L1902
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #12]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1903:
	.align	2
.L1902:
	.word	.LANCHOR3
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r1
	mov	r5, r0
	bl	rknand_device_lock
	ldr	r3, .L1906
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #16]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1907:
	.align	2
.L1906:
	.word	.LANCHOR3
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	ldr	r3, .L1910
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #4]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1911:
	.align	2
.L1910:
	.word	.LANCHOR3
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	ldr	r3, .L1914
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #8]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1915:
	.align	2
.L1914:
	.word	.LANCHOR3
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	flash_sram_load_store
	.type	flash_sram_load_store, %function
flash_sram_load_store:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1920
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #-132]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1919
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1919:
	b	ftl_memcpy
.L1921:
	.align	2
.L1920:
	.word	.LANCHOR3
	.fnend
	.size	flash_sram_load_store, .-flash_sram_load_store
	.align	2
	.global	FlashCs123Init
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
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r0, .L1925
	bl	printk
	ldr	r3, .L1925+4
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #40]
	ldmfd	sp!, {r4, lr}
	bx	r3	@ indirect register sibling call
.L1926:
	.align	2
.L1925:
	.word	.LC139
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1928
	mov	r0, #0
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #32]
	bx	r3	@ indirect register sibling call
.L1929:
	.align	2
.L1928:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1931
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #44]
	bx	r3	@ indirect register sibling call
.L1932:
	.align	2
.L1931:
	.word	.LANCHOR3
	.fnend
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1934
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #48]
	bx	r3	@ indirect register sibling call
.L1935:
	.align	2
.L1934:
	.word	.LANCHOR3
	.fnend
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1937
	mov	r0, #0
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #36]
	bx	r3
.L1938:
	.align	2
.L1937:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1940
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #60]
	bx	r3
.L1941:
	.align	2
.L1940:
	.word	.LANCHOR3
	.fnend
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1943
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #64]
	bx	r3
.L1944:
	.align	2
.L1943:
	.word	.LANCHOR3
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L1946
	ldr	ip, [ip, #-136]
	ldr	ip, [ip, #20]
	bx	ip
.L1947:
	.align	2
.L1946:
	.word	.LANCHOR3
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1949
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #28]
	bx	r3
.L1950:
	.align	2
.L1949:
	.word	.LANCHOR3
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1952
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #52]
	bx	r3
.L1953:
	.align	2
.L1952:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1955
	ldr	r3, [r3, #-136]
	ldr	r3, [r3, #56]
	bx	r3	@ indirect register sibling call
.L1956:
	.align	2
.L1955:
	.word	.LANCHOR3
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, lr}
	.save {lr}
	.pad #20
	mov	ip, r0
	mul	r3, r3, r2
	str	r2, [sp]
	str	r1, [sp, #4]
	mov	r2, #0
	ldr	r0, .L1959
	mov	r1, ip
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	mov	r3, #16
	bl	print_hex_dump
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1960:
	.align	2
.L1959:
	.word	.LC140
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	hynix_get_read_retry_default
	.type	hynix_get_read_retry_default, %function
hynix_get_read_retry_default:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r0, #2
	ldr	r8, .L2072
	.pad #36
	sub	sp, sp, #36
	mvn	r7, #81
	mvn	r6, #80
	mov	r5, r0
	ldr	r4, [r8, #1176]
	add	r3, r4, #128
	str	r3, [sp]
	strb	r0, [r4, #112]
	mvn	r3, #83
	strb	r7, [r4, #130]
	strb	r3, [r4, #128]
	mvn	r3, #82
	strb	r6, [r4, #131]
	strb	r3, [r4, #129]
	bne	.L1962
	mvn	r3, #88
	strb	r3, [r4, #128]
	ldr	r3, .L2072+4
	mvn	r1, #8
	mov	r6, #7
	strb	r1, [r3, #413]
	b	.L2023
.L1962:
	cmp	r0, #3
	bne	.L1964
	add	r1, r4, #127
	mov	r3, #176
.L1965:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1965
	b	.L2066
.L1964:
	cmp	r0, #4
	bne	.L1966
	mvn	r1, #51
	strb	r3, [r4, #133]
	strb	r1, [r4, #128]
	mvn	r1, #64
	strb	r7, [r4, #134]
	strb	r1, [r4, #129]
	mvn	r1, #85
	strb	r6, [r4, #135]
	strb	r1, [r4, #130]
	mvn	r1, #84
	strb	r1, [r4, #131]
	mvn	r1, #50
	strb	r1, [r4, #132]
.L2066:
	mov	r6, #8
	mov	fp, r6
	b	.L1963
.L1966:
	cmp	r0, #5
	bne	.L1967
	mov	r3, #56
	mov	r6, #8
	strb	r3, [r4, #128]
	mov	r3, #57
	strb	r3, [r4, #129]
	mov	r3, #58
	strb	r3, [r4, #130]
	mov	r3, #59
	strb	r3, [r4, #131]
	b	.L2023
.L1967:
	cmp	r0, #6
	bne	.L1968
	mov	r3, #14
	mov	r6, #12
	strb	r3, [r4, #128]
	mov	r3, #15
	strb	r3, [r4, #129]
	mov	r3, #16
	strb	r3, [r4, #130]
	mov	r3, #17
	strb	r3, [r4, #131]
	b	.L2023
.L1968:
	cmp	r0, #7
	bne	.L1969
	add	r1, r4, #127
	mov	r3, #176
.L1970:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1970
	mvn	r3, #43
	mov	r6, #12
	strb	r3, [r4, #136]
	mov	fp, #10
	mvn	r3, #42
	strb	r3, [r4, #137]
	b	.L1963
.L1969:
	cmp	r0, #8
	mov	r6, #7
	bne	.L2023
	mov	r3, #6
	strb	r6, [r4, #129]
	strb	r3, [r4, #128]
	mov	r6, #50
	mov	r3, #9
	strb	r0, [r4, #130]
	strb	r3, [r4, #131]
	mov	fp, #5
	mov	r3, #10
	strb	r3, [r4, #132]
	b	.L1963
.L2023:
	mov	fp, #4
.L1963:
	sub	r3, r5, #1
	cmp	r3, #1
	bhi	.L2062
	ldr	r1, .L2072+8
	mov	r9, #0
.L1971:
	ldrb	ip, [r8, #1101]	@ zero_extendqisi2
	uxtb	r3, r9
	ldr	r0, .L2072
	cmp	ip, r3
	bls	.L1978
	add	r3, r0, r3
	mov	r5, #160
	ldr	r0, [r0, #1040]
	sub	r10, fp, #1
	ldrb	r7, [r3, #1180]	@ zero_extendqisi2
	add	r2, r4, #127
	ldr	ip, [sp]
	mla	r5, r5, r7, r4
	add	r7, r0, r7, asl #8
	uxtab	r10, ip, r10
	mov	ip, #55
	add	r5, r5, #144
	sub	r3, r5, #1
.L1973:
	str	ip, [r7, #2056]
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r3, [sp, #12]
	str	r0, [r7, #2052]
	mov	r0, #200
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	str	r2, [sp, #20]
	bl	timer_delay_ns
	ldr	r0, [r7, #2048]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	cmp	r2, r10
	ldr	ip, [sp, #8]
	ldr	r1, [sp, #16]
	strb	r0, [r3, #1]!
	bne	.L1973
	mov	ip, r5
	mov	r0, #0
.L1974:
	add	r7, r1, r0
	mov	r3, #1
.L1975:
	ldrb	lr, [r7, r3, asl #2]	@ zero_extendqisi2
	ldrb	r10, [ip]	@ zero_extendqisi2
	add	lr, lr, r10
	strb	lr, [ip, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1975
	add	r0, r0, #1
	add	ip, ip, #1
	cmp	r0, #4
	bne	.L1974
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r5, #16]
	strb	r3, [r5, #24]
	strb	r3, [r5, #32]
	strb	r3, [r5, #40]
	strb	r3, [r5, #48]
	strb	r3, [r5, #41]
	strb	r3, [r5, #49]
	b	.L1971
.L2062:
	sub	r3, r5, #3
	cmp	r3, #5
	bhi	.L1978
	smulbb	r2, fp, r6
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #28]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L2070:
	str	r3, [sp, #12]
	ldr	r3, .L2072
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	cmp	r2, r3
	bhi	.L2022
.L1978:
	strb	fp, [r4, #113]
	strb	r6, [r4, #114]
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2022:
	ldr	r2, .L2072
	mov	r10, #160
	mov	r9, #255
	add	r3, r2, r3
	ldrb	r3, [r3, #1180]	@ zero_extendqisi2
	mov	r0, r3
	str	r3, [sp]
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [sp]
	mla	r10, r10, r3, r4
	add	r3, r10, #144
	str	r3, [sp, #24]
	ldr	r3, .L2072
	ldr	r3, [r3, #1040]
	str	r3, [sp, #4]
	ldr	r3, [sp]
	mov	r8, r3, asl #8
	ldr	r3, [sp, #4]
	add	r7, r3, r8
	str	r9, [r7, #2056]
	bl	nandc_wait_flash_ready
	cmp	r5, #8
	bne	.L1980
	add	r3, r4, #144
	mov	r2, #23
	mov	r1, #25
	str	r3, [sp, #24]
	mov	r3, #120
	str	r3, [r7, #2056]
	mov	r3, #0
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r2, [r7, #2056]
	mov	r2, #4
	str	r2, [r7, #2056]
	str	r1, [r7, #2056]
	mov	r1, #218
	str	r1, [r7, #2056]
	mov	r1, #21
	str	r3, [r7, #2056]
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r1, [r7, #2052]
	str	r2, [r7, #2052]
	str	r3, [r7, #2052]
	b	.L1981
.L1980:
	cmp	r5, #4
	mov	r3, #54
	str	r3, [r7, #2056]
	moveq	r3, #64
	streq	r9, [r7, #2052]
	streq	r3, [r7, #2048]
	moveq	r3, #204
	beq	.L2067
.L1982:
	sub	r3, r5, #5
	cmp	r3, #1
	ldrlsb	r3, [r4, #128]	@ zero_extendqisi2
	strls	r3, [r7, #2052]
	movls	r3, #82
	bls	.L2068
	cmp	r5, #7
	bne	.L1983
	mov	r3, #174
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #176
.L2067:
	str	r3, [r7, #2052]
	mov	r3, #77
.L2068:
	str	r3, [r7, #2048]
.L1983:
	ldr	r3, [sp, #4]
	cmp	r5, #6
	add	r8, r3, r8
	mov	r3, #22
	str	r3, [r8, #2056]
	mov	r3, #23
	str	r3, [r8, #2056]
	mov	r3, #4
	str	r3, [r8, #2056]
	mov	r3, #25
	str	r3, [r8, #2056]
	mov	r3, #0
	str	r3, [r8, #2056]
	str	r3, [r8, #2052]
	str	r3, [r8, #2052]
	moveq	r3, #31
	str	r3, [r8, #2052]
	mov	r3, #2
	str	r3, [r8, #2052]
	mov	r3, #0
	str	r3, [r8, #2052]
.L1981:
	ldmia	sp, {r2, r3}
	sub	r10, r5, #8
	sub	r8, r5, #5
	clz	r10, r10
	add	r3, r3, r2, asl #8
	mov	r2, #48
	mov	r10, r10, lsr #5
	str	r2, [r3, #2056]
	bl	nandc_wait_flash_ready
	cmp	r8, #1
	movhi	r3, #0
	movls	r3, #1
	str	r3, [sp, #16]
	orrs	r3, r3, r10
	movne	ip, #16
	bne	.L1987
	cmp	r5, #7
	movne	ip, #2
	moveq	ip, #32
.L1987:
	ldr	r3, .L2072+12
	ldmia	sp, {r1, r2}
	ldr	r3, [r3, #-128]
	add	r1, r2, r1, asl #8
	mov	r0, r3
.L1988:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1988
	cmp	r10, #0
	beq	.L1989
	mov	r2, #0
.L1991:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L1990
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1990
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1991
	b	.L1992
.L1990:
	cmp	r0, #7
	bne	.L1993
.L1992:
	ldr	r0, .L2072+16
	mov	r1, #0
	bl	printk
.L1994:
	b	.L1994
.L1989:
	cmp	r5, #7
	bne	.L1995
	mov	r2, r10
.L1997:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L1996
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1996
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1997
	b	.L1998
.L1996:
	cmp	r0, #7
	bne	.L1993
.L1998:
	ldr	r0, .L2072+16
	mov	r1, #0
	bl	printk
.L1999:
	b	.L1999
.L1995:
	cmp	r5, #6
	bne	.L1993
	sub	r2, r3, #1
	add	r3, r3, #7
.L2000:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L1993
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L1993
	cmp	r2, r3
	bne	.L2000
	ldr	r0, .L2072+16
	mov	r1, #0
	bl	printk
.L2002:
	b	.L2002
.L1993:
	ldr	r3, .L2072+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-128]
	add	r0, r2, r0
	mov	r3, r2
.L2003:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L2003
.L2071:
	ldr	r3, .L2072+12
	mov	r0, #8
	ldr	r1, [sp, #8]
	ldr	r3, [r3, #-128]
	add	r1, r3, r1
	str	r1, [sp, #20]
.L2006:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L2005:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L2005
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L2006
.L2007:
	mov	ip, #0
	mov	r0, ip
.L2010:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r8, #16
	mov	r7, lr
.L2008:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	subs	r8, r8, #1
	add	lr, lr, r9
	bne	.L2008
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L2010
	ldr	r1, [sp, #20]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L2007
	ldr	r3, .L2072+12
	ldr	r1, [r3, #-128]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L2012:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L2012
	cmp	r3, #7
	ble	.L2013
	ldr	r0, .L2072+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2072+16
	mov	r1, #0
	bl	printk
.L2014:
	b	.L2014
.L2013:
	cmp	r5, #6
	moveq	r0, #4
	beq	.L2015
	cmp	r5, #7
	moveq	r0, #10
	beq	.L2015
	cmp	r10, #0
	moveq	r0, #8
	movne	r0, #5
.L2015:
	sub	r9, fp, #1
	ldr	r1, [sp, #24]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L2016:
	mov	r7, r1
	mov	r3, r2
.L2017:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, fp
	bcc	.L2017
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r6
	add	r1, r1, r0
	blt	.L2016
	ldmia	sp, {r2, r3}
	mov	r8, #255
	add	r7, r3, r2, asl #8
	str	r8, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L2019
	mov	r3, #54
	str	r3, [r7, #2056]
	ldrb	r3, [r4, #128]	@ zero_extendqisi2
	mov	r2, #22
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	str	r2, [r7, #2056]
	str	r3, [r7, #2056]
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	mov	r3, #48
	str	r8, [r7, #2052]
	str	r8, [r7, #2052]
	str	r8, [r7, #2052]
	b	.L2069
.L2019:
	cmp	r10, #0
	movne	r3, #190
	moveq	r3, #56
.L2069:
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2070
.L2073:
	.align	2
.L2072:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+396
	.word	.LANCHOR3
	.word	.LC141
	.word	.LC142
	.fnend
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2076
	ldrb	r0, [r3, #23]	@ zero_extendqisi2
	sub	r3, r0, #1
	cmp	r3, #7
	bxhi	lr
	b	hynix_get_read_retry_default
.L2077:
	.align	2
.L2076:
	.word	.LANCHOR2
	.fnend
	.size	flash_get_read_retry_tbl, .-flash_get_read_retry_tbl
	.align	2
	.global	nandc_xfer_done
	.type	nandc_xfer_done, %function
nandc_xfer_done:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2127
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r5, #0
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	mov	r4, r3
	ldr	r7, .L2127+4
	cmp	r2, #9
	strb	r5, [r7, #-124]
	bne	.L2079
	ldr	r6, [r3, #1040]
	ldr	r3, [r6, #16]
	str	r3, [sp]
	ldr	r3, [r6, #48]
	ubfx	r3, r3, #1, #1
	cmp	r3, r5
	moveq	r5, r3
	moveq	r7, r4
	beq	.L2081
.L2080:
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #6
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2083
	ldr	r3, [r4, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2082
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2082
	ldr	r1, [sp, #4]
	ldr	r0, .L2127+8
	ubfx	r1, r1, #17, #1
	bl	printk
	b	.L2083
.L2082:
	ldr	r3, [sp]
	add	r5, r5, #1
	ubfx	r3, r3, #22, #6
	cmp	r5, r3, asl #12
	bne	.L2084
	ldr	r2, [r6, #64]
	mov	r1, r5
	ldr	r3, [sp]
	ldr	r0, .L2127+12
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r3, [sp, #4]
	tst	r3, #8192
	mov	r3, #1
	strb	r3, [r7, #-124]
	bne	.L2083
	ldr	r3, .L2127+16
	ldr	r0, .L2127+20
	ldr	r3, [r3, #4]
	blx	r3
	b	.L2083
.L2084:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2080
.L2083:
	ldr	r3, [r4, #1160]
	ldr	r5, .L2127
	cmp	r3, #0
	beq	.L2088
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #1152]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1156]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2126
.L2090:
	ldr	r3, [r4, #1040]
	add	r5, r5, #1
	ldr	r3, [r3, #16]
	str	r3, [sp]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2089
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #64]
	ldr	r0, .L2127+24
	ubfx	r3, r3, #16, #6
	bl	printk
	ldr	r0, .L2127+28
	ldr	r1, [r7, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2089:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2081:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2090
	ldr	r3, [r4, #1160]
	ldr	r5, .L2127
	cmp	r3, #0
	beq	.L2088
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #1152]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1156]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2125
.L2079:
	ldr	r7, [r3, #1040]
	ldr	r3, [r7, #8]
	str	r3, [sp]
	ldr	r6, [r7, #16]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	moveq	r5, r4
	beq	.L2093
.L2092:
	ldr	r2, [r7, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2095
	ldr	r3, [r4, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2094
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2094
	ldr	r1, [sp, #4]
	ldr	r0, .L2127+32
	bl	printk
	b	.L2095
.L2094:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2096
	ldr	r2, [r7, #28]
	mov	r1, r5
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L2127+12
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L2127+28
	ldr	r1, [r4, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2096:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2092
.L2095:
	ldr	r3, [r4, #1160]
	ldr	r5, .L2127
	cmp	r3, #0
	beq	.L2088
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #1152]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1156]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2126:
	mov	r2, #0
	b	.L2124
.L2099:
	ldr	r3, [r4, #1040]
	add	r6, r6, #1
	ldr	r3, [r3, #8]
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L2098
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r7, #28]
	ldr	r0, .L2127+24
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L2127+28
	ldr	r1, [r5, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2098:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2093:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2099
	ldr	r3, [r4, #1160]
	ldr	r5, .L2127
	cmp	r3, #0
	beq	.L2088
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #1152]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1156]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2125:
	mov	r2, #1
.L2124:
	bl	rknand_dma_unmap_single
.L2088:
	mov	r3, #0
	str	r3, [r4, #1160]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2128:
	.align	2
.L2127:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC143
	.word	.LC144
	.word	arm_delay_ops
	.word	644245000
	.word	.LC145
	.word	.LC146
	.word	.LC147
	.fnend
	.size	nandc_xfer_done, .-nandc_xfer_done
	.align	2
	.global	nandc_xfer
	.type	nandc_xfer, %function
nandc_xfer:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r4, r1
	ldr	r7, [sp, #32]
	mov	r0, r1
	mov	r8, r2
	mov	r1, r2
	mov	r6, r3
	mov	r2, r3
	mov	r3, r7
	bl	nandc_xfer_start
	mov	r0, r4
	bl	nandc_xfer_done
	cmp	r4, #0
	movne	r0, #0
	bne	.L2159
	ldr	r5, .L2172
	ldrb	r3, [r5, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2131
	ldr	ip, [r5, #1040]
	mov	r2, r8, lsr #2
	mov	r1, #1
	mov	lr, r4
	mov	r0, r4
.L2132:
	cmp	lr, r2
	bcs	.L2169
	add	r3, lr, #84
	ldr	r3, [ip, r3, asl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	ldr	r4, [sp, #4]
	ubfx	r4, r4, #26, #1
	and	r4, r4, r3, lsr #10
	ldr	r3, [sp, #4]
	and	r1, r1, r4
	tst	r3, #4
	bne	.L2151
	ldr	r3, [sp, #4]
	tst	r3, #262144
	bne	.L2151
	ldr	r4, [sp, #4]
	ldr	r3, [sp, #4]
	ubfx	r4, r4, #3, #7
	ubfx	r3, r3, #19, #7
	cmp	r4, r3
	ldr	r3, [sp, #4]
	ubfxgt	r3, r3, #3, #7
	ubfxle	r3, r3, #19, #7
	cmp	r0, r3
	movcc	r0, r3
	b	.L2133
.L2151:
	mvn	r0, #0
.L2133:
	add	lr, lr, #1
	b	.L2132
.L2169:
	ldr	r3, [r5, #1040]
	cmp	r1, #0
	movne	r0, #512
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2138
	mov	r1, r4
	ldr	r0, .L2172+4
	bl	printk
	ldr	r3, [r5, #1040]
	mvn	r0, #0
	orr	r4, r4, #131072
	str	r4, [r3]
.L2138:
	tst	r4, #8192
	beq	.L2139
	ldr	r3, .L2172+8
	ldrb	r3, [r3, #-124]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2139
	ldr	r0, .L2172+12
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1040]
	mov	r2, #1
	str	r2, [r3, #16]
	b	.L2140
.L2131:
	ldrb	r3, [r5, #1172]	@ zero_extendqisi2
	mov	r1, r8, lsr #1
	mov	r2, r4
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L2142:
	cmp	r2, r1
	add	r0, r4, ip
	add	r3, r3, #4
	bcs	.L2170
	ldr	lr, [r5, #1140]
	mov	r4, r4, lsr #2
	add	r2, r2, #1
	ldr	r4, [lr, r4, asl #2]
	mov	lr, r4, lsr #8
	strb	r4, [r3, #-4]
	strb	lr, [r3, #-3]
	mov	lr, r4, lsr #16
	mov	r4, r4, lsr #24
	strb	lr, [r3, #-2]
	strb	r4, [r3, #-1]
	mov	r4, r0
	b	.L2142
.L2170:
	ldr	ip, [r5, #1040]
	mov	r1, #0
	mov	r2, r8, lsr #2
	mov	r0, r1
.L2144:
	cmp	r1, r2
	bcs	.L2171
	add	r3, r1, #8
	ldr	r3, [ip, r3, asl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2154
	ldr	r3, [sp, #4]
	tst	r3, #32768
	bne	.L2154
	ldr	lr, [sp, #4]
	ldr	r8, [sp, #4]
	ldr	r3, [sp, #4]
	ubfx	lr, lr, #3, #5
	ldr	r4, [sp, #4]
	ubfx	r8, r8, #27, #1
	ubfx	r3, r3, #16, #5
	ubfx	r4, r4, #29, #1
	orr	lr, lr, r8, asl #5
	orr	r3, r3, r4, asl #5
	cmp	lr, r3
	ldr	r3, [sp, #4]
	ldrhi	r4, [sp, #4]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #4]
	ubfxls	r3, r3, #16, #5
	ubfxhi	r4, r4, #27, #1
	ubfxls	r4, r4, #29, #1
	orr	r4, r3, r4, asl #5
	cmp	r0, r4
	movcc	r0, r4
	b	.L2145
.L2154:
	mvn	r0, #0
.L2145:
	add	r1, r1, #1
	b	.L2144
.L2171:
	ldr	r3, [r5, #1040]
	mov	r2, #0
	str	r2, [r3, #16]
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2139
	mov	r1, r4
	ldr	r0, .L2172+16
	bl	printk
	ldr	r3, .L2172
	orr	r4, r4, #131072
	ldr	r3, [r3, #1040]
	str	r4, [r3]
	b	.L2140
.L2139:
	cmn	r0, #1
	beq	.L2159
	ldr	r3, [r7]
	cmn	r3, #1
	bne	.L2159
	ldr	r3, [r7, #4]
	cmn	r3, #1
	bne	.L2159
	ldr	r3, [r6]
	cmn	r3, #1
	moveq	r0, #512
	b	.L2159
.L2140:
	mvn	r0, #0
.L2159:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2173:
	.align	2
.L2172:
	.word	.LANCHOR0
	.word	.LC148
	.word	.LANCHOR3
	.word	.LC149
	.word	.LC150
	.fnend
	.size	nandc_xfer, .-nandc_xfer
	.align	2
	.global	flash_read_page
	.type	flash_read_page, %function
flash_read_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r10, .L2190
	.pad #20
	sub	sp, sp, #20
	mvn	r4, #0
	ubfx	r9, r1, #24, #2
	str	r2, [sp, #12]
	mov	r8, r3
	ldrb	r0, [r10, #1189]	@ zero_extendqisi2
	mov	r7, r10
	ldr	r5, [r10, #1040]
	mov	r10, r6, asl #8
	rsb	r0, r0, #24
	bic	r4, r1, r4, asl r0
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r9, #0
	bne	.L2175
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	b	.L2176
.L2175:
	ldr	r2, [r7, #1096]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L2177
	ldrb	r2, [r7, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2177
	ldrb	r2, [r7, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r2, r5, r10
	streq	r9, [r2, #2056]
	beq	.L2176
.L2177:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2176:
	ldr	r2, [r7, #1096]
	ldrb	r1, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2178
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	cmp	r1, #2
	addeq	r1, r5, r10
	moveq	r0, #38
	streq	r0, [r1, #2056]
.L2178:
	add	fp, r5, r10
	mov	r1, #0
	mov	r0, #48
	str	r1, [fp, #2056]
	str	r1, [fp, #2052]
	str	r1, [fp, #2052]
	uxtb	r1, r4
	str	r1, [fp, #2052]
	mov	r1, r4, lsr #8
	str	r1, [fp, #2052]
	mov	r1, r4, lsr #16
	str	r1, [fp, #2052]
	ldrb	r1, [r7, #1188]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, r4, lsr #24
	strne	r1, [fp, #2052]
	add	r1, r5, r10
	str	r0, [r1, #2056]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r9, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2180
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2180
	ldr	r3, .L2190
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r4, r4, r4, asl #1
	subeq	r0, r4, #1
	addeq	r0, r0, r9
	beq	.L2189
.L2180:
	mov	r0, r4
.L2189:
	bl	nandc_set_seed
	mov	r4, #0
	bl	nandc_wait_flash_ready
	add	r3, r5, r10
	mov	r1, r4
	mov	r2, #5
	mov	r0, r6
	str	r2, [r3, #2056]
	str	r4, [fp, #2052]
	mov	r2, #224
	str	r4, [fp, #2052]
	str	r2, [r3, #2056]
	ldrb	r2, [sp, #56]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	str	r8, [sp]
	bl	nandc_xfer
	mov	r5, r0
	mov	r0, r4
	bl	nandc_de_cs
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2191:
	.align	2
.L2190:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_page, .-flash_read_page
	.align	2
	.global	micron_read_retrial
	.type	micron_read_retrial, %function
micron_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L2220
	mov	r5, r0
	str	r3, [sp, #24]
	mov	r9, r1
	str	r2, [sp, #20]
	mov	r8, #0
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	ldr	r10, .L2220+4
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #1040]
	str	r3, [sp, #16]
.L2193:
	mov	r0, r5
	mov	r7, #0
	bl	zftl_flash_enter_slc_mode
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	mvn	r4, #0
	ldr	r3, [sp, #16]
	add	r6, r3, r5, asl #8
.L2194:
	ldrb	r3, [r10, #-116]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L2198
	mov	r3, #239
	mov	r0, #200
	str	r3, [r6, #2056]
	mov	r3, #137
	str	r3, [r6, #2052]
	add	fp, r7, #1
	bl	timer_delay_ns
	str	fp, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	mov	r0, r5
	str	r3, [r6, #2048]
	mov	r1, r9
	ldr	r3, [sp, #72]
	ldr	r2, [sp, #20]
	str	r3, [sp]
	ldr	r3, [sp, #24]
	bl	flash_read_page
	ldr	r3, .L2220+8
	ldr	r3, [r3]
	tst	r3, #4096
	mov	ip, r0
	beq	.L2195
	str	r0, [sp]
	mov	r1, r7
	str	r0, [sp, #28]
	mov	r2, r9
	ldr	r0, .L2220+12
	mov	r3, r4
	bl	printk
	ldr	ip, [sp, #28]
.L2195:
	cmn	ip, #1
	beq	.L2196
	ldr	r3, [r10, #-128]
	cmn	r4, #1
	moveq	r4, ip
	str	r3, [sp, #20]
	ldr	r3, [r10, #-120]
	str	r3, [sp, #24]
	ldr	r3, [sp, #12]
	cmp	ip, r3
	bcc	.L2205
.L2196:
	mov	r7, fp
	b	.L2194
.L2205:
	mov	r4, ip
.L2198:
	ldr	r3, [sp, #16]
	mov	r6, r5, asl #8
	mov	r0, #200
	add	fp, r3, r6
	mov	r3, #239
	str	r3, [fp, #2056]
	mov	r3, #137
	str	r3, [fp, #2052]
	bl	timer_delay_ns
	mov	r3, #0
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	ldr	r3, [sp, #12]
	cmp	r4, r3
	bcc	.L2200
	cmn	r4, #1
	movne	r4, #256
.L2200:
	cmn	r4, #1
	movne	fp, #0
	moveq	fp, #1
	cmp	r4, #256
	movne	r3, fp
	orreq	r3, fp, #1
	cmp	r3, #0
	beq	.L2201
	mov	r3, r7
	str	r4, [sp]
	ldr	r0, .L2220+16
	mov	r1, r5
	mov	r2, r9
	eor	r7, r8, #1
	bl	printk
	ands	r7, fp, r7
	beq	.L2202
	bl	nandc_wait_flash_ready
	ldr	r3, .L2220
	mov	r0, #200
	mov	r8, #1
	ldr	r3, [r3, #1040]
	add	r6, r3, r6
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #150
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	mov	r3, #3
	str	r3, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	b	.L2193
.L2202:
	cmp	r8, #0
	beq	.L2203
	bl	nandc_wait_flash_ready
	ldr	r3, .L2220
	mov	r0, #200
	ldr	r9, [r3, #1040]
	mov	r3, #239
	add	r5, r9, r5, asl #8
	add	r6, r9, r6
	str	r3, [r6, #2056]
	mov	r3, #150
	str	r3, [r5, #2052]
	bl	timer_delay_ns
	cmn	r4, #1
	str	r7, [r5, #2048]
	movne	r4, #256
	str	r7, [r5, #2048]
	str	r7, [r5, #2048]
	str	r7, [r5, #2048]
	b	.L2203
.L2201:
	cmp	r8, #0
	beq	.L2203
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r2, .L2220
	mov	r0, #200
	mov	r4, #256
	ldr	r9, [r2, #1040]
	mov	r2, #239
	add	r5, r9, r5, asl #8
	add	r6, r9, r6
	str	r2, [r6, #2056]
	mov	r2, #150
	str	r2, [r5, #2052]
	bl	timer_delay_ns
	ldr	r3, [sp, #12]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L2203:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2221:
	.align	2
.L2220:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC151
	.word	.LC152
	.fnend
	.size	micron_read_retrial, .-micron_read_retrial
	.align	2
	.global	toshiba_3d_read_retrial
	.type	toshiba_3d_read_retrial, %function
toshiba_3d_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ubfx	r4, r1, #24, #2
	mov	r8, r0
	mov	fp, r3
	mov	r10, r1
	str	r2, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r7, .L2270
	add	r6, r8, #8
	ldr	r3, [r7, #1040]
	add	r6, r3, r6, asl #8
	str	r3, [sp, #8]
	ldrb	r3, [r7, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	movne	r3, #56
	moveq	r3, #46
	movne	r5, #10
	moveq	r5, #26
	cmp	r4, #0
	str	r3, [sp, #16]
	mvn	r4, #0
	bne	.L2224
	ldr	r3, [sp, #8]
	mov	r9, #1
	add	r3, r3, r8, asl #8
	str	r3, [sp, #16]
.L2231:
	ldr	r3, .L2270
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2225
	mov	r2, #0
	mov	r1, r9
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	ldr	r2, [sp, #16]
	str	r3, [r2, #2056]
	b	.L2226
.L2225:
	uxtb	r1, r9
	bl	toshiba_3d_set_slc_rr_para
.L2226:
	ldr	r3, [sp, #64]
	mov	r0, r8
	mov	r1, r10
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2270+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2227
	mov	r3, ip
	ldr	r0, .L2270+8
	mov	r1, r9
	mov	r2, r10
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2227:
	cmn	ip, #1
	beq	.L2228
	ldr	r3, .L2270+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-128]
	ldr	fp, [r3, #-120]
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2246
.L2228:
	add	r9, r9, #1
	cmp	r9, r5
	bne	.L2231
	b	.L2230
.L2246:
	mov	r5, r9
	mov	r4, ip
.L2230:
	ldr	r3, .L2270
	mov	r1, #0
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	moveq	r2, r1
	beq	.L2269
	bl	toshiba_3d_set_slc_rr_para
	b	.L2233
.L2224:
	ldr	r3, [sp, #8]
	mov	r5, #1
	add	r9, r3, r8, asl #8
.L2240:
	ldr	r3, .L2270
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2234
	mov	r1, r5
	mov	r2, #1
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	b	.L2268
.L2234:
	uxtb	r1, r5
	bl	toshiba_3d_set_tlc_rr_para
	mov	r3, #38
.L2268:
	str	r3, [r9, #2056]
	mov	r0, r8
	ldr	r3, [sp, #64]
	mov	r1, r10
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2270+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2236
	mov	r3, ip
	ldr	r0, .L2270+16
	mov	r1, r5
	mov	r2, r10
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2236:
	cmn	ip, #1
	beq	.L2237
	ldr	r3, .L2270+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-128]
	ldr	fp, [r3, #-120]
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2247
.L2237:
	ldr	r3, [sp, #16]
	add	r5, r5, #1
	cmp	r5, r3
	bne	.L2240
	b	.L2239
.L2247:
	mov	r4, ip
.L2239:
	ldr	r3, .L2270
	mov	r0, r6
	mov	r1, #0
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2241
	mov	r2, #1
.L2269:
	bl	toshiba_tlc_set_rr_para
	b	.L2233
.L2241:
	bl	toshiba_3d_set_tlc_rr_para
.L2233:
	ldr	r3, .L2270
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2242
	ldr	r3, [sp, #8]
	mov	r2, #85
	add	r3, r3, r8, asl #8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2048]
	mov	r2, #255
	str	r2, [r3, #2056]
.L2242:
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2243
	cmn	r4, #1
	movne	r4, #256
.L2243:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2244
	str	r4, [sp]
	mov	r1, r8
	ldr	r0, .L2270+20
	mov	r2, r10
	mov	r3, r5
	bl	printk
.L2244:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2271:
	.align	2
.L2270:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC153
	.word	.LANCHOR3
	.word	.LC154
	.word	.LC155
	.fnend
	.size	toshiba_3d_read_retrial, .-toshiba_3d_read_retrial
	.align	2
	.global	toshiba_read_retrial
	.type	toshiba_read_retrial, %function
toshiba_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	r5, r0
	ldr	r4, .L2305
	str	r3, [sp, #20]
	add	r9, r5, #8
	str	r1, [sp, #12]
	str	r2, [sp, #16]
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	ldr	r7, [r4, #1040]
	sub	r3, r3, #67
	cmp	r3, #1
	add	r9, r7, r9, asl #8
	mov	r3, r5, asl #8
	str	r3, [sp, #24]
	movls	r3, #0
	strls	r3, [sp, #8]
	bls	.L2273
	ldrb	r6, [r4, #1135]	@ zero_extendqisi2
	cmp	r6, #0
	streq	r6, [sp, #8]
	beq	.L2274
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #1
	str	r3, [sp, #8]
.L2274:
	ldr	r3, [sp, #24]
	mov	r2, #92
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L2273:
	ldr	fp, .L2305+4
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r5, asl #8
	str	r3, [sp, #28]
.L2275:
	ldrb	r3, [fp, #-116]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L2304
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	mov	r0, r9
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2276
	bl	sandisk_set_rr_para
	b	.L2277
.L2276:
	bl	toshiba_set_rr_para
.L2277:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L2278
	ldr	r3, .L2305+4
	ldrb	r3, [r3, #-116]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #28]
	moveq	r2, #179
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L2278:
	ldr	r3, [sp, #24]
	mov	r2, #38
	add	r1, sp, #12
	mov	r0, r5
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	flash_read_page
	cmn	r0, #1
	mov	r6, r0
	beq	.L2281
	ldr	r3, [fp, #-128]
	cmn	r10, #1
	moveq	r10, r0
	str	r3, [sp, #16]
	ldr	r3, [fp, #-120]
	str	r3, [sp, #20]
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2283
.L2281:
	add	r8, r8, #1
	b	.L2275
.L2304:
	mov	r6, r10
.L2283:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, #0
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2285
	bl	sandisk_set_rr_para
	b	.L2286
.L2285:
	bl	toshiba_set_rr_para
.L2286:
	add	r5, r7, r5, asl #8
	mov	r3, #255
	str	r3, [r5, #2056]
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L2287
	cmn	r6, #1
	movne	r6, #256
.L2287:
	cmn	r6, #1
	cmpne	r6, #256
	bne	.L2288
	str	r6, [sp]
	mov	r1, r8
	ldr	r0, .L2305+8
	mov	r3, r8
	ldr	r2, [sp, #12]
	bl	printk
.L2288:
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L2289
	mov	r0, #4
	bl	nandc_set_if_mode
.L2289:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2306:
	.align	2
.L2305:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC155
	.fnend
	.size	toshiba_read_retrial, .-toshiba_read_retrial
	.align	2
	.global	ymtc_3d_read_retrial
	.type	ymtc_3d_read_retrial, %function
ymtc_3d_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L2337
	mov	r9, r1
	mov	fp, r3
	mov	r10, r2
	bl	nandc_wait_flash_ready
	mov	r0, r8
	add	r7, r8, #8
	bl	zftl_flash_enter_slc_mode
	mov	r0, r8
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r6, #1040]
	tst	r9, #50331648
	mvn	r4, #0
	add	r7, r3, r7, asl #8
	mov	r5, #1
	ldr	ip, .L2337+4
	bne	.L2317
.L2312:
	mov	r0, r7
	uxtb	r1, r5
	str	ip, [sp, #12]
	bl	ymtc_3d_set_slc_rr_para
	mov	r0, r8
	mov	r1, r9
	mov	r2, r10
	ldr	r3, [sp, #56]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L2309
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-128]
	moveq	r4, r0
	ldr	fp, [ip, #-120]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2320
.L2309:
	add	r5, r5, #1
	cmp	r5, #10
	bne	.L2312
	b	.L2311
.L2320:
	mov	r4, r0
.L2311:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_slc_rr_para
	b	.L2313
.L2336:
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-128]
	moveq	r4, r0
	ldr	fp, [ip, #-120]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2321
.L2314:
	add	r5, r5, #1
	cmp	r5, #51
	beq	.L2316
.L2317:
	mov	r0, r7
	uxtb	r1, r5
	str	ip, [sp, #12]
	bl	ymtc_3d_set_tlc_rr_para
	mov	r0, r8
	mov	r1, r9
	mov	r2, r10
	ldr	r3, [sp, #56]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	cmn	r0, #1
	ldr	ip, [sp, #12]
	bne	.L2336
	b	.L2314
.L2321:
	mov	r4, r0
.L2316:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_tlc_rr_para
.L2313:
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2318
	cmn	r4, #1
	movne	r4, #256
.L2318:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2319
	str	r4, [sp]
	mov	r1, r5
	ldr	r0, .L2337+8
	mov	r2, r9
	mov	r3, r5
	bl	printk
.L2319:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2338:
	.align	2
.L2337:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC156
	.fnend
	.size	ymtc_3d_read_retrial, .-ymtc_3d_read_retrial
	.align	2
	.global	samsung_read_retrial
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	mov	r8, r1
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	bl	nandc_wait_flash_ready
	mov	r0, r7
	bl	zftl_flash_enter_slc_mode
	mov	r0, r7
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L2377
	tst	r8, #50331648
	ldr	r4, [r3, #1040]
	str	r3, [sp, #16]
	bne	.L2340
	mov	r10, r7, asl #8
	mvn	r5, #0
	add	r9, r4, r10
	mov	r6, #1
	mov	fp, #239
.L2345:
	str	fp, [r9, #2056]
	mov	r3, #141
	str	r3, [r9, #2052]
	ldr	r3, .L2377+4
	ldrsb	r3, [r6, r3]
	str	r3, [r9, #2048]
	mov	r3, #0
	str	r3, [r9, #2048]
	str	r3, [r9, #2048]
	str	r3, [r9, #2048]
	bl	nandc_wait_flash_ready
	mov	r0, r7
	mov	r1, r8
	ldr	r3, [sp, #64]
	ldr	r2, [sp, #8]
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bl	flash_read_page
	ldr	r3, .L2377+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2341
	mov	r3, ip
	ldr	r0, .L2377+12
	mov	r1, r6
	mov	r2, r8
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2341:
	cmn	ip, #1
	beq	.L2342
	ldr	r3, .L2377+16
	cmn	r5, #1
	moveq	r5, ip
	ldr	r2, [r3, #-128]
	ldr	r3, [r3, #-120]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2354
.L2342:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L2345
	b	.L2344
.L2354:
	mov	r5, ip
.L2344:
	add	r10, r4, r10
	mov	r3, #239
	add	r4, r4, r7, asl #8
	str	r3, [r10, #2056]
	mov	r3, #141
	b	.L2376
.L2340:
	mov	r3, r7, asl #8
	ldr	r9, .L2377+20
	add	r10, r4, r3
	mvn	r5, #0
	mov	r6, #1
	str	r3, [sp, #20]
.L2351:
	mov	r3, #239
	str	r3, [r10, #2056]
	mov	r3, #137
	str	r3, [r10, #2052]
	ldrb	r3, [r9, #4]	@ zero_extendqisi2
	str	r3, [r10, #2048]
	ldrb	r3, [r9, #5]	@ zero_extendqisi2
	str	r3, [r10, #2048]
	ldrb	r3, [r9, #6]	@ zero_extendqisi2
	str	r3, [r10, #2048]
	ldrb	r3, [r9, #7]	@ zero_extendqisi2
	str	r3, [r10, #2048]
	bl	nandc_wait_flash_ready
	mov	r0, r7
	mov	r1, r8
	ldr	r3, [sp, #64]
	ldr	r2, [sp, #8]
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bl	flash_read_page
	ldr	r3, .L2377+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	fp, r0
	beq	.L2347
	ldr	r0, .L2377+24
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L2347:
	cmn	fp, #1
	beq	.L2348
	ldr	r3, .L2377+16
	cmn	r5, #1
	moveq	r5, fp
	ldr	r2, [r3, #-128]
	ldr	r3, [r3, #-120]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	fp, r3, asr #2
	bcc	.L2355
.L2348:
	add	r6, r6, #1
	add	r9, r9, #4
	cmp	r6, #26
	bne	.L2351
	b	.L2350
.L2355:
	mov	r5, fp
.L2350:
	ldr	r3, [sp, #20]
	mov	r2, #239
	add	r3, r4, r3
	add	r4, r4, r7, asl #8
	str	r2, [r3, #2056]
	mov	r3, #137
.L2376:
	str	r3, [r4, #2052]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, asr #2
	bcc	.L2352
	cmn	r5, #1
	movne	r5, #256
.L2352:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L2353
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L2377+28
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2353:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2378:
	.align	2
.L2377:
	.word	.LANCHOR0
	.word	.LANCHOR1+1928
	.word	.LANCHOR2
	.word	.LC157
	.word	.LANCHOR3
	.word	.LANCHOR1+1956
	.word	.LC158
	.word	.LC159
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	hynix_read_retrial
	.type	hynix_read_retrial, %function
hynix_read_retrial:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r3, .L2398
	.pad #28
	sub	sp, sp, #28
	mov	r10, r2
	mov	r6, r0
	mov	r8, r1
	mov	r7, #0
	ldr	r2, [r3, #1176]
	mvn	r4, #0
	str	r3, [sp, #16]
	add	r3, r2, r0
	str	r3, [sp, #12]
	ldrb	r9, [r2, #114]	@ zero_extendqisi2
	ldrb	r5, [r3, #120]	@ zero_extendqisi2
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	ldr	ip, .L2398+4
.L2380:
	cmp	r7, r9
	bcs	.L2384
	add	r5, r5, #1
	mov	r0, r6
	str	ip, [sp, #20]
	uxtb	r5, r5
	cmp	r5, r9
	movcs	r5, #0
	mov	r1, r5
	bl	hynix_set_rr_para
	mov	r0, r6
	mov	r1, r8
	mov	r2, r10
	ldr	r3, [sp, #64]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	cmn	r0, #1
	ldr	ip, [sp, #20]
	beq	.L2382
	ldr	r3, [sp, #16]
	cmn	r4, #1
	ldr	r10, [ip, #-128]
	moveq	r4, r0
	ldr	fp, [ip, #-120]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2388
.L2382:
	add	r7, r7, #1
	b	.L2380
.L2388:
	mov	r4, r0
.L2384:
	ldr	r3, [sp, #12]
	strb	r5, [r3, #120]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2386
	cmn	r4, #1
	movne	r4, #256
.L2386:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2387
	str	r4, [sp]
	mov	r1, r7
	ldr	r0, .L2398+8
	mov	r2, r8
	mov	r3, r7
	bl	printk
.L2387:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2399:
	.align	2
.L2398:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC160
	.fnend
	.size	hynix_read_retrial, .-hynix_read_retrial
	.align	2
	.global	flash_ddr_tuning_read
	.type	flash_ddr_tuning_read, %function
flash_ddr_tuning_read:
	.fnstart
	@ args = 4, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	r8, r0
	mov	r10, r1
	mov	fp, r2
	str	r3, [sp, #16]
	bl	nandc_get_ddr_para
	mov	r4, #0
	mov	r5, r4
	mov	r6, #1024
	mov	r7, #6
	mvn	r9, #0
	str	r0, [sp, #24]
	str	r4, [sp, #20]
	str	r4, [sp, #12]
.L2406:
	uxtb	r0, r7
	bl	nandc_set_ddr_para
	mov	r0, r8
	mov	r1, r10
	mov	r2, fp
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	ldr	r3, .L2427
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2401
	mov	r3, ip
	ldr	r0, .L2427+4
	mov	r1, r7
	mov	r2, r10
	str	ip, [sp, #28]
	bl	printk
	ldr	ip, [sp, #28]
.L2401:
	add	r3, r6, #1
	cmp	ip, r3
	bhi	.L2402
	ldr	r3, .L2427+8
	ldr	fp, [r3, #-128]
	ldr	r3, [r3, #-120]
	str	r3, [sp, #16]
	ldr	r3, .L2427+12
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	cmp	ip, r3, lsr #2
	bcs	.L2412
	add	r5, r5, #1
	cmp	r5, #7
	bls	.L2412
	rsb	r4, r5, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2404
.L2402:
	ldr	r3, [sp, #12]
	cmp	r3, r5
	bcs	.L2413
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #20]
	bhi	.L2405
	str	r5, [sp, #12]
	b	.L2413
.L2412:
	mov	r4, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2403
.L2413:
	mov	r5, #0
.L2403:
	add	r7, r7, #2
	cmp	r7, #50
	bne	.L2406
.L2404:
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r2, r5
	movhi	r4, r3
.L2405:
	cmp	r4, #0
	beq	.L2407
	ldr	r3, .L2427+12
	ldr	r2, .L2427+16
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	umull	r2, r3, r3, r2
	ubfx	r3, r3, #1, #8
	cmp	r6, r3
	bcs	.L2407
	ldr	r0, .L2427+20
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	b	.L2426
.L2407:
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
.L2426:
	bl	nandc_set_ddr_para
	cmn	r9, #1
	bne	.L2409
	ldr	r5, .L2427+12
	ldrb	r3, [r5, #1232]	@ zero_extendqisi2
	mov	r4, r5
	tst	r3, #1
	beq	.L2409
	mov	r1, r8
	mov	r2, r10
	ldr	r0, .L2427+24
	bl	printk
	mov	r0, r8
	bl	flash_reset
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	add	r3, r5, r8
	mov	r0, r8
	mov	r2, #2
	strb	r2, [r3, #1192]
	bl	zftl_flash_enter_slc_mode
	mov	r1, r10
	mov	r2, fp
	mov	r0, r8
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	mov	r1, r8
	mov	r2, r10
	mov	r6, r0
	ldr	r0, .L2427+28
	mov	r3, r6
	bl	printk
	ldrb	r3, [r5, #1172]	@ zero_extendqisi2
	cmp	r6, r3
	bhi	.L2414
	ldr	r2, .L2427+8
	ldr	r3, [r2, #-112]
	add	r3, r3, #1
	str	r3, [r2, #-112]
	cmp	r3, #100
	movhi	r3, #0
	strhib	r3, [r5, #1135]
	bls	.L2410
	b	.L2409
.L2414:
	mov	r6, r9
.L2410:
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	nandc_set_if_mode
.L2409:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2428:
	.align	2
.L2427:
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
	.type	flash_read_page_en, %function
flash_read_page_en:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r3
	ldr	r5, .L2460
	mov	r10, r0
	mov	r4, r1
	mov	r8, r2
	ldr	r9, [sp, #48]
	ldrb	r3, [r5, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2430
	ldr	r1, .L2460+4
	movw	r2, #431
	ldr	r0, .L2460+8
	bl	printk
	bl	dump_stack
.L2430:
	add	r2, r5, r10
	ldr	r3, .L2460
	ldrb	r6, [r2, #1180]	@ zero_extendqisi2
	ldrb	r2, [r5, #1101]	@ zero_extendqisi2
	cmp	r10, r2
	bcc	.L2431
	ldr	r3, .L2460+12
	ldr	r3, [r3]
	tst	r3, #64
	beq	.L2459
	str	r2, [sp]
	mov	r1, r6
	ldr	r0, .L2460+16
	mov	r2, r10
	mov	r3, r4
	bl	printk
.L2459:
	mvn	r0, #0
	b	.L2456
.L2431:
	tst	r4, #50331648
	bne	.L2434
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2435
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2434
.L2435:
	ldrh	r10, [r5, #2]
	mov	r0, r4
	mov	r1, r10
	bl	__aeabi_uidiv
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L2460
	mul	r0, r10, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L2434:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	cmn	r0, #1
	bne	.L2456
	ldrb	fp, [r5, #1168]	@ zero_extendqisi2
	ldr	r10, .L2460
	cmp	fp, #0
	bne	.L2438
.L2441:
	ldr	r3, .L2460+20
	ldr	ip, [r3, #-108]
	cmp	ip, #0
	bne	.L2439
	b	.L2440
.L2438:
	mov	r3, #0
	mov	r0, r6
	strb	r3, [r10, #1168]
	mov	r1, r4
	str	r9, [sp]
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	strb	fp, [r10, #1168]
	cmn	r0, #1
	beq	.L2441
	b	.L2456
.L2439:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	blx	ip
	cmn	r0, #1
	bne	.L2456
.L2440:
	ldrb	r3, [r5, #1168]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2460+24
	mov	r2, r4
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2459
	str	r9, [sp, #48]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	flash_ddr_tuning_read
.L2456:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2461:
	.align	2
.L2460:
	.word	.LANCHOR0
	.word	.LANCHOR1+2060
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC165
	.word	.LANCHOR3
	.word	.LC166
	.fnend
	.size	flash_read_page_en, .-flash_read_page_en
	.align	2
	.global	flash_get_last_written_page
	.type	flash_get_last_written_page, %function
flash_get_last_written_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r1
	ldr	r6, .L2473
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L2473+4
	mov	r10, r0
	mov	fp, r2
	mov	r8, r3
	ldrh	r7, [r6, #30]
	ldrh	r4, [r1, #-220]
	ldr	r1, [sp, #56]
	sub	r4, r4, #1
	mul	r7, r7, r9
	uxth	r4, r4
	str	r1, [sp]
	sxtah	r1, r7, r4
	bl	flash_read_page_en
	cmp	r0, #512
	moveq	ip, #0
	bne	.L2463
.L2464:
	sxth	r3, ip
	sxth	r2, r4
	cmp	r3, r2
	bgt	.L2463
	add	r3, r3, r2
	mov	r0, r10
	mov	r2, fp
	str	ip, [sp, #12]
	add	r3, r3, r3, lsr #31
	mov	r5, r3, asr #1
	ldr	r3, [sp, #56]
	sxtah	r1, r7, r5
	str	r3, [sp]
	mov	r3, r8
	bl	flash_read_page_en
	cmp	r0, #512
	ldr	ip, [sp, #12]
	subeq	r5, r5, #1
	addne	ip, r5, #1
	uxtheq	r4, r5
	uxthne	ip, ip
	b	.L2464
.L2463:
	ldr	r3, [r6]
	sxth	r4, r4
	tst	r3, #4096
	beq	.L2468
	ldr	r0, .L2473+8
	mov	r1, r9
	mov	r2, r4
	ldr	r3, [r8]
	bl	printk
.L2468:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2474:
	.align	2
.L2473:
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC167
	.fnend
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, lr}
	.save {r4, lr}
	.pad #8
	mov	lr, r2
	ldr	r2, .L2477
	mov	r4, r1
	ldr	r1, .L2477+4
	mov	ip, r0
	ldrb	r2, [r2, #1189]	@ zero_extendqisi2
	ldrh	r1, [r1, #-12]
	rsb	r2, r2, #24
	str	r3, [sp]
	rsb	r2, r1, r2
	mvn	r1, #0
	mov	r0, r0, asr r2
	bic	r1, ip, r1, asl r2
	mov	r3, lr
	mov	r2, r4
	uxtb	r0, r0
	bl	flash_get_last_written_page
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L2478:
	.align	2
.L2477:
	.word	.LANCHOR0
	.word	.LANCHOR3-3120
	.fnend
	.size	flash_get_last_written_page_ext, .-flash_get_last_written_page_ext
	.align	2
	.global	flash_ddr_para_scan
	.type	flash_ddr_para_scan, %function
flash_ddr_para_scan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r8, r0
	ldr	r4, .L2486
	mov	r7, r1
	mov	r6, #1
	ldr	r5, .L2486+4
	mov	r9, #4
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	strb	r6, [r4, #1135]
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	nandc_set_if_mode
	mov	r1, r7
	ldr	r2, [r5, #-104]
	mov	r0, r8
	ldr	r3, [r5, #-100]
	str	r9, [sp]
	bl	flash_ddr_tuning_read
	ldr	r3, [r5, #-100]
	mov	r0, r8
	mov	r1, r7
	ldr	r2, [r5, #-104]
	str	r9, [sp]
	bl	flash_read_page
	mov	r3, r4
	cmn	r0, #1
	bne	.L2480
	ldrb	r2, [r4, #1232]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L2480
	mov	r0, r6
	bl	flash_set_interface_mode
	mov	r0, r6
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #1135]
	b	.L2481
.L2480:
	mov	r2, #1
	strb	r2, [r3, #1135]
.L2481:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2487:
	.align	2
.L2486:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.global	__aeabi_uidivmod
	.section	.text.unlikely
	.align	2
	.type	id_block_read_data.constprop.31, %function
id_block_read_data.constprop.31:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2519
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #124
	sub	sp, sp, #124
	ldr	r4, .L2519+4
	mov	r6, r2
	str	r1, [sp, #24]
	mov	r5, #0
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r7, #4
	ldrh	r1, [r3, #30]
	mov	r10, r5
	ldrb	r3, [r4, #1168]	@ zero_extendqisi2
	str	r0, [sp, #40]
	mul	r1, r1, r2
	str	r3, [sp, #44]
	str	r2, [sp, #28]
	strb	r5, [r4, #1168]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #40]
	str	r1, [sp, #12]
	rsb	r3, r1, r3
	str	r3, [sp, #32]
	and	r3, r1, #3
	str	r3, [sp, #16]
.L2489:
	ldr	r3, [sp, #24]
	cmp	r5, r3
	bcs	.L2518
	ldr	r3, [sp, #16]
	mov	r1, r7
	rsb	r3, r3, r7
	uxth	r3, r3
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	add	r0, r5, r3
	bl	__aeabi_uidiv
	ldrb	r1, [r4, #1102]	@ zero_extendqisi2
	cmp	r1, #0
	uxth	r0, r0
	mov	r3, r0, asl #1
	add	r2, r4, r3
	ldrh	r2, [r2, #4]
	moveq	r0, r2
	beq	.L2491
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r0, r3
.L2491:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #16]
	mla	r3, r3, r0, r2
	str	r3, [sp, #36]
	ldr	r3, .L2519+4
	ldr	r2, [sp, #36]
	ldrb	r9, [r3, #1100]	@ zero_extendqisi2
	add	r0, r2, r1
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, .L2519+4
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	mov	fp, r0
	mov	r0, r9
	str	r3, [sp, #48]
	bl	nandc_bch_sel
.L2492:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_read_page
	cmn	r0, #1
	mov	r8, r0
	bne	.L2499
	ldrb	ip, [r4, #1168]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L2494
.L2497:
	ldr	r3, .L2519+8
	ldr	ip, [r3, #-108]
	cmp	ip, #0
	bne	.L2495
	b	.L2496
.L2494:
	mov	r0, #0
	str	r7, [sp]
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	str	ip, [sp, #52]
	strb	r0, [r4, #1168]
	bl	flash_read_page
	ldr	ip, [sp, #52]
	cmn	r0, #1
	strb	ip, [r4, #1168]
	beq	.L2497
	b	.L2517
.L2495:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	blx	ip
	cmn	r0, #1
	bne	.L2517
.L2496:
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2499
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_ddr_tuning_read
.L2517:
	mov	r8, r0
.L2499:
	subs	r9, r9, #16
	movne	r9, #1
	cmn	r8, #1
	movne	r9, #0
	cmp	r9, #0
	beq	.L2500
	mov	r0, #16
	mov	r9, #16
	bl	nandc_bch_sel
	b	.L2492
.L2500:
	ldr	r0, [sp, #48]
	bl	nandc_bch_sel
	cmn	r8, #1
	mvneq	r10, #0
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #40]
	cmp	r5, #0
	cmpeq	r3, r2
	moveq	r3, #1
	movne	r3, #0
	cmp	r10, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L2502
	ldr	r3, [r6]
	ldr	r2, .L2519+12
	cmp	r3, r2
	bne	.L2502
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #20]
	ldrb	r7, [r6, #17]	@ zero_extendqisi2
	add	r3, r3, r2
	str	r3, [sp, #24]
	b	.L2503
.L2502:
	ldr	r3, [sp, #20]
	mov	r2, r5
	ldr	r0, .L2519+16
	ldr	r1, [sp, #36]
	add	r6, r6, r3, asl #9
	ldr	r3, [sp, #60]
	str	r3, [sp]
	ldr	r3, [sp, #56]
	bl	printk
	mov	r3, #0
	str	r3, [sp, #16]
.L2503:
	ldr	r3, [sp, #20]
	add	r5, r3, r5
	uxth	r5, r5
	b	.L2489
.L2518:
	ldr	r3, .L2519+4
	mov	r0, r10
	ldrb	r2, [sp, #44]	@ zero_extendqisi2
	strb	r2, [r3, #1168]
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2520:
	.align	2
.L2519:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	1179535694
	.word	.LC168
	.fnend
	.size	id_block_read_data.constprop.31, .-id_block_read_data.constprop.31
	.text
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r7, .L2533
	mov	r4, r0
	.pad #20
	sub	sp, sp, #20
	mvn	r5, #0
	mov	r8, r1
	mov	r10, r2
	ldrb	r3, [r7, #1189]	@ zero_extendqisi2
	add	r9, r0, #8
	ldr	r6, [r7, #1040]
	rsb	r3, r3, #24
	bic	r5, r1, r5, asl r3
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	r9, r6, r9, asl #8
	bl	hynix_reconfig_rr_para
	mov	r0, r4
	bl	nandc_cs
	tst	r8, #50331648
	mov	r0, r4
	bne	.L2522
	bl	zftl_flash_enter_slc_mode
	b	.L2523
.L2522:
	bl	zftl_flash_exit_slc_mode
.L2523:
	mov	ip, r4, asl #8
	mov	r2, #128
	add	r3, r6, ip
	mov	r0, r5
	str	ip, [sp, #12]
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r5
	str	r2, [r3, #2052]
	mov	r2, r5, lsr #8
	str	r2, [r3, #2052]
	mov	r2, r5, lsr #16
	str	r2, [r3, #2052]
	ldrb	r2, [r7, #1188]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r5, lsr #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2533+4
	mov	r1, #1
	mov	r0, r4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	mov	r3, #16
	ldr	ip, [sp, #12]
	add	ip, r6, ip
	str	r3, [ip, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r9
	bl	flash_read_status
	mov	r5, r0
	mov	r0, r4
	bl	nandc_de_cs
	ands	r2, r5, #4
	beq	.L2525
	ldr	r0, .L2533+8
	mov	r1, r8
	bl	printk
	mvn	r2, #0
.L2525:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2534:
	.align	2
.L2533:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC169
	.fnend
	.size	flash_prog_page, .-flash_prog_page
	.align	2
	.global	flash_test_blk
	.type	flash_test_blk, %function
flash_test_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ldr	r4, .L2546
	mov	r5, r1
	mov	r2, #32
	mov	r1, #165
	ldr	r0, [r4, #-128]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r4, #-120]
	bl	ftl_memset
	ldr	r3, .L2546+4
	mov	r0, r6
	ldrh	r3, [r3, #2]
	mul	r5, r3, r5
	mov	r1, r5
	bl	flash_erase_block
	cmn	r0, #1
	bne	.L2536
.L2538:
	mvn	r4, #0
	b	.L2537
.L2536:
	ldr	r7, .L2546+8
	mov	r0, r6
	mov	r1, r5
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r2, [r4, #-128]
	ldr	r3, [r4, #-120]
	bl	flash_prog_page
	cmn	r0, #1
	beq	.L2538
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r6
	mov	r1, r5
	str	r3, [sp]
	ldr	r2, [r4, #-128]
	ldr	r3, [r4, #-120]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L2538
	ldr	r3, [r4, #-128]
	ldr	r2, [r3]
	ldr	r3, .L2546+12
	cmp	r2, r3
	bne	.L2538
	ldr	r3, [r4, #-120]
	ldr	r4, .L2546+16
	ldr	r3, [r3]
	subs	r4, r3, r4
	mvnne	r4, #0
.L2537:
	mov	r0, r6
	mov	r1, r5
	bl	flash_erase_block
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2547:
	.align	2
.L2546:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1515870811
	.word	1515870810
	.fnend
	.size	flash_test_blk, .-flash_test_blk
	.align	2
	.global	flash_start_one_pass_page_prog
	.type	flash_start_one_pass_page_prog, %function
flash_start_one_pass_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r10, r0
	ldr	r6, .L2558
	mov	r0, r3
	ldr	r5, [sp, #40]
	mov	r9, r3
	mov	r8, r2
	mov	r7, r9, asl #8
	ldr	r4, [r6, #1040]
	bl	nandc_cs
	cmp	r10, #0
	mov	r0, r5
	add	r2, r4, r7
	addne	r3, r4, r7
	add	r4, r4, r7
	strne	r10, [r3, #2056]
	mov	r3, #128
	str	r3, [r2, #2056]
	mov	r3, #0
	str	r3, [r2, #2052]
	str	r3, [r2, #2052]
	uxtb	r3, r5
	str	r3, [r2, #2052]
	mov	r3, r5, lsr #8
	str	r3, [r2, #2052]
	mov	r3, r5, lsr #16
	str	r3, [r2, #2052]
	ldrb	r3, [r6, #1188]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, r5, lsr #24
	strne	r3, [r2, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2558+4
	mov	r0, r9
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #48]
	str	r3, [sp]
	ldr	r3, [sp, #44]
	bl	nandc_xfer
	str	r8, [r4, #2056]
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2559:
	.align	2
.L2558:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_one_pass_page_prog, .-flash_start_one_pass_page_prog
	.align	2
	.global	flash_dual_page_prog
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r3
	ldr	r4, .L2573
	add	r8, r0, #8
	ldr	r6, .L2573+4
	.pad #20
	sub	sp, sp, #20
	mov	r5, r0
	mov	r7, r1
	ldr	r3, [r4, #1040]
	mov	r10, r2
	ubfx	fp, r1, #24, #2
	add	r8, r3, r8, asl #8
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	mvn	r4, #0
	rsb	r3, r3, #24
	bic	r4, r1, r4, asl r3
	ldr	r3, [r6]
	tst	r3, #16
	beq	.L2561
	ldr	r0, .L2573+8
	mov	r2, fp
	ldr	r3, [sp, #64]
	bl	printk
.L2561:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r5
	bne	.L2562
	bl	zftl_flash_enter_slc_mode
	b	.L2563
.L2562:
	bl	zftl_flash_exit_slc_mode
.L2563:
	mov	r0, #0
	mov	r2, #16
	mov	r1, r0
	mov	r3, r5
	stmia	sp, {r4, r10}
	add	r4, r4, #1
	str	r9, [sp, #8]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	r0, #0
	mov	r2, #16
	mov	r1, r0
	ldr	r3, [sp, #56]
	str	r4, [sp]
	str	r3, [sp, #4]
	ldr	r3, [sp, #60]
	str	r3, [sp, #8]
	mov	r3, r5
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r5
	bl	nandc_de_cs
	ands	r2, r4, #4
	beq	.L2564
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L2565
	ldr	r0, .L2573+12
	mov	r1, r7
	bl	printk
.L2565:
	mvn	r2, #0
.L2564:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2574:
	.align	2
.L2573:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC170
	.word	.LC169
	.fnend
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.align	2
	.global	ymtc_flash_tlc_page_prog
	.type	ymtc_flash_tlc_page_prog, %function
ymtc_flash_tlc_page_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r8, r3
	ldr	r4, .L2582
	mov	r6, #1
	add	r7, r0, #8
	mov	r5, r0
	mov	r9, r2
	mov	r10, r1
	ldr	r3, [r4, #1040]
	ldrb	r4, [r4, #1189]	@ zero_extendqisi2
	add	r7, r3, r7, asl #8
	rsb	r4, r4, #24
	mov	r4, r6, asl r4
	sub	r4, r4, #1
	and	r4, r4, r1
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	mov	r1, r6
	mov	r2, #26
	mov	r3, r5
	mov	r0, #0
	stmia	sp, {r4, r9}
	str	r8, [sp, #8]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	add	r3, r4, r6
	mov	r1, r6
	mov	r2, #26
	mov	r0, #0
	add	r4, r4, #2
	stmia	sp, {r3, r9}
	mov	r3, r5
	str	r8, [sp, #8]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	r2, #16
	mov	r1, r6
	mov	r3, r5
	mov	r0, #0
	stmia	sp, {r4, r9}
	str	r8, [sp, #8]
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r7
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r5
	bl	nandc_de_cs
	ands	r2, r4, #4
	beq	.L2576
	ldr	r3, .L2582+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2577
	ldr	r0, .L2582+8
	mov	r1, r10
	bl	printk
.L2577:
	mvn	r2, #0
.L2576:
	mov	r0, r2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2583:
	.align	2
.L2582:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC171
	.fnend
	.size	ymtc_flash_tlc_page_prog, .-ymtc_flash_tlc_page_prog
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.30, %function
fw_flash_page_prog.constprop.30:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, r1
	ldr	r4, .L2590
	.pad #20
	sub	sp, sp, #20
	mov	r5, r2
	ldr	r3, [r4, #1096]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrb	r8, [r4, #1172]	@ zero_extendqisi2
	mov	r7, r0
	ldrb	r0, [r4, #1100]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r3, .L2590+4
	ldrb	r2, [r3, #11]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L2585
	ldrb	r4, [r4, #1102]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L2585
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2586
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	ymtc_flash_tlc_page_prog
	b	.L2589
.L2586:
	ldr	r9, .L2590+8
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r9, #-128]
	bl	ftl_memset
	ldr	r3, [r9, #-128]
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	str	r3, [sp]
	str	r3, [sp, #4]
	mov	r3, #4
	str	r3, [sp, #8]
	mov	r3, r5
	bl	flash_dual_page_prog
	b	.L2589
.L2585:
	mov	r3, #4
	mov	r0, #0
	str	r3, [sp]
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	flash_prog_page
.L2589:
	mov	r4, r0
	mov	r0, r8
	bl	nandc_bch_sel
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2591:
	.align	2
.L2590:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.fnend
	.size	fw_flash_page_prog.constprop.30, .-fw_flash_page_prog.constprop.30
	.text
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r3
	ldr	r3, .L2600
	mov	r8, r2
	add	r5, sp, #48
	mov	fp, r0
	mov	r7, r1
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	mov	r6, r3
	ldmia	r5, {r5, r9, r10}
	cmp	r2, r4
	bhi	.L2593
	ldr	r1, .L2600+4
	mov	r2, #868
	ldr	r0, .L2600+8
	bl	printk
	bl	dump_stack
.L2593:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L2600
	cmp	r2, r4
	bls	.L2592
	add	r4, r3, r4
	ldrb	r6, [r4, #1180]	@ zero_extendqisi2
	ldr	r4, [r3, #1040]
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r3, r6, asl #8
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
	mov	r3, r5, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #16
	add	r5, r5, r5, asl #1
	str	r3, [r4, #2052]
	sub	r0, r5, #1
	add	r0, r0, r7
	bl	nandc_set_seed
	ldr	r3, .L2600+12
	mov	r0, r6
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r9
	str	r10, [sp]
	bl	nandc_xfer
	str	r8, [r4, #2056]
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L2592:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2601:
	.align	2
.L2600:
	.word	.LANCHOR0
	.word	.LANCHOR1+2080
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_tlc_page_prog, .-flash_start_tlc_page_prog
	.align	2
	.type	queue_tlc_prog_cmd, %function
queue_tlc_prog_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r8, #1
	ldr	r2, .L2607
	mov	r6, r0
	ldr	r3, [r0]
	mov	r7, r1
	ldrb	r4, [r2, #1189]	@ zero_extendqisi2
	ldr	r0, [r3, #24]
	rsb	r1, r4, #24
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	mov	r4, r8, asl r4
	mov	r5, r8, asl r1
	sub	r4, r4, #1
	cmp	r2, #0
	and	r4, r4, r0, lsr r1
	sub	r5, r5, #1
	and	r5, r5, r0
	uxtb	r4, r4
	beq	.L2603
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r6]
	mov	r1, r8
	mov	r0, #0
	str	r5, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #4]
	add	r2, r5, r8
	mov	r1, r8
	mov	r0, #0
	add	r5, r5, #2
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #8]
	mov	r0, #0
	mov	r1, r8
	str	r5, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #16
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	b	.L2604
.L2603:
	ldrb	r0, [r3, #44]	@ zero_extendqisi2
	mov	r1, r8
	str	r5, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldmia	r6, {r2, r3}
	mov	r1, #2
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r5, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r6, #8]
	ldr	r2, [r6]
	mov	r1, #3
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r5, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #16
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
.L2604:
	cmp	r7, #0
	beq	.L2602
	ldr	r1, [r6]
	mov	r3, #4
	ldr	r0, .L2607+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2602:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2608:
	.align	2
.L2607:
	.word	.LANCHOR0
	.word	.LANCHOR0+2775
	.fnend
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r7, #1
	ldr	r3, .L2613
	mov	r5, r0
	ldr	r1, [r0, #24]
	ldrb	r4, [r3, #1189]	@ zero_extendqisi2
	ldrb	r8, [r3, #1197]	@ zero_extendqisi2
	rsb	r2, r4, #24
	mov	r4, r7, asl r4
	sub	r4, r4, #1
	mov	r6, r7, asl r2
	and	r4, r4, r1, lsr r2
	cmp	r8, #0
	sub	r6, r6, #1
	and	r6, r6, r1
	uxtb	r4, r4
	beq	.L2610
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r5, #4]
	mov	r1, r7
	mov	r2, #26
	mov	r0, #0
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	add	r3, r6, r7
	mov	r1, r7
	mov	r2, #26
	mov	r0, #0
	add	r6, r6, #2
	str	r3, [sp]
	ldr	r3, [r5, #4]
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r0, #0
	mov	r1, r7
	mov	r2, #16
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	b	.L2611
.L2610:
	str	r6, [sp]
	mov	r1, r7
	ldr	r3, [r0, #4]
	mov	r2, #26
	str	r3, [sp, #4]
	ldr	r3, [r0, #12]
	mov	r0, r8
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r1, #2
	mov	r2, #26
	mov	r0, r8
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r0, r8
	mov	r1, #3
	mov	r2, #16
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
.L2611:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r0, [r5, #24]
	mov	r1, #64
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready
.L2614:
	.align	2
.L2613:
	.word	.LANCHOR0
	.fnend
	.size	sblk_3d_tlc_dump_prog, .-sblk_3d_tlc_dump_prog
	.align	2
	.global	flash_start_3d_mlc_page_prog
	.type	flash_start_3d_mlc_page_prog, %function
flash_start_3d_mlc_page_prog:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L2619
	mov	r5, r2
	mov	r7, r0
	mov	r4, r1
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	mov	r6, r3
	cmp	r2, r1
	bhi	.L2616
	ldr	r1, .L2619+4
	movw	r2, #903
	ldr	r0, .L2619+8
	bl	printk
	bl	dump_stack
.L2616:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L2619
	cmp	r2, r4
	bls	.L2615
	add	r4, r3, r4
	ldrb	r6, [r4, #1180]	@ zero_extendqisi2
	ldr	r4, [r3, #1040]
	add	r4, r4, r6, asl #8
	mov	r0, r6
	bl	nandc_cs
	mov	r3, #128
	mov	r0, r5
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r5
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #16
	str	r3, [r4, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2619+12
	mov	r0, r6
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	str	r3, [sp]
	mov	r3, r8
	bl	nandc_xfer
	str	r7, [r4, #2056]
.L2615:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2620:
	.align	2
.L2619:
	.word	.LANCHOR0
	.word	.LANCHOR1+2108
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_3d_mlc_page_prog, .-flash_start_3d_mlc_page_prog
	.align	2
	.global	sblk_mlc_dump_prog
	.type	sblk_mlc_dump_prog, %function
sblk_mlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2627
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	mov	r4, #1
	ldrb	r1, [r3, #1189]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r2, [r0, #24]
	rsb	r3, r1, #24
	mov	r6, r4, asl r3
	mov	r4, r4, asl r1
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, r2, lsr r3
	and	r6, r6, r2
	uxtb	r4, r4
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L2627+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2622
	ldr	r2, [r5, #24]
	mov	r1, r6
	ldr	r0, .L2627+8
	add	r3, r2, #1
	bl	printk
.L2622:
	ldr	r3, [r5, #12]
	mov	r1, r4
	mov	r2, r6
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
	mov	r5, r0
	mov	r0, r4
	bl	nandc_de_cs
	mov	r0, r5
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L2628:
	.align	2
.L2627:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC172
	.fnend
	.size	sblk_mlc_dump_prog, .-sblk_mlc_dump_prog
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r6, .L2644
	mvn	r3, #0
	mov	r10, r2
	.pad #20
	sub	sp, sp, #20
	mov	r8, r1
	ldrb	r4, [r6, #1189]	@ zero_extendqisi2
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	rsb	r7, r4, #24
	str	r0, [sp, #8]
	mvn	r4, r3, asl r4
	and	r5, r4, r1, lsr r7
	uxtb	r9, r5
	cmp	r2, r9
	bhi	.L2630
	ldr	r1, .L2644+4
	mov	r2, #956
	ldr	r0, .L2644+8
	bl	printk
	bl	dump_stack
.L2630:
	ldrb	r1, [r6, #1101]	@ zero_extendqisi2
	ldr	r2, .L2644
	cmp	r1, r9
	bls	.L2629
	add	r1, r2, r9
	mvn	r4, #0
	str	r2, [sp, #12]
	bic	r4, r8, r4, asl r7
	ldrb	r5, [r1, #1180]	@ zero_extendqisi2
	ldr	r7, [r2, #1040]
	bl	nandc_rdy_status
	cmp	r0, #0
	ldr	r2, [sp, #12]
	bne	.L2632
	ldrb	r2, [r2, #1101]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2633
	bl	nandc_wait_flash_ready
	b	.L2632
.L2633:
	mov	r0, r9
	mov	r1, r4
	mov	r2, #64
	bl	flash_wait_device_ready_raw
.L2632:
	mov	r0, r5
	bl	hynix_reconfig_rr_para
	mov	r0, r5
	bl	nandc_cs
	tst	r8, #50331648
	bne	.L2634
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r4, r0
	beq	.L2635
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L2635
.L2634:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L2635:
	mov	r8, r5, asl #8
	mov	r2, #128
	add	r3, r7, r8
	mov	r0, r4
	add	r7, r7, r8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r4
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #8
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #16
	str	r2, [r3, #2052]
	ldrb	r2, [r6, #1188]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r4, lsr #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2644+12
	mov	r0, r5
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	mov	r0, r5
	ldr	r3, [sp, #8]
	str	r3, [r7, #2056]
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L2629:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2645:
	.align	2
.L2644:
	.word	.LANCHOR0
	.word	.LANCHOR1+2140
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_page_prog, .-flash_start_page_prog
	.align	2
	.type	queue_prog_cmd, %function
queue_prog_cmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, [r4, #4]
	mov	r0, #16
	ldr	r3, [r4, #12]
	ldr	r1, [r4, #24]
	bl	flash_start_page_prog
	ldr	r2, .L2655
	ldr	r0, [r4, #24]
	ldrb	r3, [r2, #2775]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2647
	ldrb	ip, [r2, #1189]	@ zero_extendqisi2
	mvn	r1, #0
	mov	r5, #48
	rsb	r6, ip, #24
	mvn	r1, r1, asl ip
	uxth	r1, r1
	and	r0, r1, r0, asr r6
.L2649:
	mla	lr, r5, r3, r2
	ldr	ip, [lr, #1260]
	and	ip, r1, ip, lsr r6
	cmp	r0, ip
	bne	.L2648
	add	ip, lr, #1264
	ldrb	lr, [lr, #1278]	@ zero_extendqisi2
	add	ip, ip, #12
	cmp	lr, #7
	moveq	r3, #3
	streqb	r3, [ip, #2]
	beq	.L2647
.L2648:
	mla	r3, r5, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2649
.L2647:
	mov	r3, #3
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #1
	ldr	r0, .L2655+4
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	buf_add_tail
.L2656:
	.align	2
.L2655:
	.word	.LANCHOR0
	.word	.LANCHOR0+2775
	.fnend
	.size	queue_prog_cmd, .-queue_prog_cmd
	.align	2
	.global	flash_complete_plane_page_read
	.type	flash_complete_plane_page_read, %function
flash_complete_plane_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mvn	r3, #0
	ldr	r8, .L2687
	mov	r10, r0
	mov	r6, r1
	mov	r7, r2
	ldrb	r4, [r8, #1189]	@ zero_extendqisi2
	rsb	r9, r4, #24
	mvn	r4, r3, asl r4
	and	r4, r4, r0, lsr r9
	ldrb	r3, [r8, #1101]	@ zero_extendqisi2
	uxtb	r5, r4
	cmp	r3, r5
	bhi	.L2658
	ldr	r1, .L2687+4
	movw	r2, #1070
	ldr	r0, .L2687+8
	bl	printk
	bl	dump_stack
.L2658:
	ldrb	r2, [r8, #1101]	@ zero_extendqisi2
	ldr	r3, .L2687
	cmp	r2, r5
	mvnls	r0, #0
	bls	.L2659
	add	r5, r3, r5
	mvn	r4, #0
	bic	r4, r10, r4, asl r9
	ubfx	r10, r10, #24, #2
	ldrb	r9, [r5, #1180]	@ zero_extendqisi2
	ldr	r5, [r3, #1040]
	mov	r0, r9
	bl	nandc_cs
	cmp	r10, #0
	bne	.L2660
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2660:
	ldrb	r3, [r8, #1119]	@ zero_extendqisi2
	uxtb	ip, r4
	mov	r0, r4, lsr #8
	ldrb	r1, [r8, #1188]	@ zero_extendqisi2
	cmp	r3, #1
	mov	r3, r9, asl #8
	addeq	r2, r5, r3
	moveq	lr, #6
	beq	.L2686
	ldr	r2, .L2687
	ldr	r2, [r2, #1096]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	add	r2, r5, r3
	bne	.L2664
	mov	lr, #5
.L2686:
	add	r3, r5, r3
	str	lr, [r2, #2056]
	mov	lr, #0
	cmp	r1, lr
	str	lr, [r2, #2052]
	movne	r1, r4, lsr #24
	str	lr, [r2, #2052]
	str	ip, [r2, #2052]
	str	r0, [r2, #2052]
	mov	r0, r4, lsr #16
	str	r0, [r2, #2052]
	strne	r1, [r2, #2052]
	mov	r2, #224
	str	r2, [r3, #2056]
	b	.L2663
.L2664:
	add	r5, r5, r3
	mov	lr, #0
	cmp	r1, lr
	str	lr, [r2, #2056]
	mov	r3, #5
	str	lr, [r2, #2052]
	movne	r1, r4, lsr #24
	str	lr, [r2, #2052]
	str	ip, [r2, #2052]
	str	r0, [r2, #2052]
	mov	r0, r4, lsr #16
	str	r0, [r2, #2052]
	strne	r1, [r2, #2052]
	str	r3, [r5, #2056]
	mov	r3, #0
	str	r3, [r2, #2052]
	str	r3, [r2, #2052]
	mov	r3, #224
	str	r3, [r5, #2056]
.L2663:
	ldr	r2, [r8, #1096]
	ldr	r3, .L2687
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r10, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2667
	ldrb	r2, [r3, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2667
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r10, #1
	addeq	r4, r4, r4, asl #1
	addeq	r0, r0, r4
	beq	.L2683
.L2667:
	mov	r0, r4
.L2683:
	bl	nandc_set_seed
	ldr	r3, .L2687+12
	mov	r1, #0
	mov	r0, r9
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r6
	str	r7, [sp]
	bl	nandc_xfer
	mov	r4, r0
	mov	r0, r9
	bl	nandc_de_cs
	mov	r0, r4
.L2659:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2688:
	.align	2
.L2687:
	.word	.LANCHOR0
	.word	.LANCHOR1+2164
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_complete_plane_page_read, .-flash_complete_plane_page_read
	.align	2
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #1
	ldr	r5, .L2724
	.pad #20
	sub	sp, sp, #20
	mov	r9, r0
	mov	r10, r1
	mov	fp, r2
	ldrb	r7, [r5, #1189]	@ zero_extendqisi2
	rsb	r3, r7, #24
	mov	r7, r6, asl r7
	sub	r7, r7, #1
	mov	r4, r6, asl r3
	and	r7, r7, r0, lsr r3
	ldrb	r3, [r5, #1101]	@ zero_extendqisi2
	sub	r4, r4, #1
	ubfx	r6, r0, #24, #2
	uxtb	r7, r7
	and	r4, r4, r0
	cmp	r3, r7
	bhi	.L2690
	ldr	r1, .L2724+4
	mov	r2, #1232
	ldr	r0, .L2724+8
	bl	printk
	bl	dump_stack
.L2690:
	add	r7, r5, r7
	ldr	r3, [r5, #1040]
	ldrb	r8, [r7, #1180]	@ zero_extendqisi2
	str	r3, [sp, #12]
	mov	r0, r8
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2691
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2691:
	ldr	r7, .L2724+12
	ldrb	r3, [r7, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2692
	ldr	r3, [sp, #12]
	mov	r2, #5
	add	r3, r3, r8, asl #8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r4
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #8
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #16
	str	r2, [r3, #2052]
	mov	r2, #224
	str	r2, [r3, #2056]
.L2692:
	ldr	r2, [r5, #1096]
	ldr	r3, .L2724
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r6, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2693
	ldrb	r2, [r3, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2693
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r6, #1
	addeq	r3, r4, r4, asl #1
	addeq	r0, r0, r3
	beq	.L2723
.L2693:
	mov	r0, r4
.L2723:
	bl	nandc_set_seed
	ldrb	r2, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	mov	r1, #0
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	cmn	r0, #1
	bne	.L2695
	ldrb	ip, [r5, #1168]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L2696
	ldr	r2, .L2724
	mov	r3, #0
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	str	ip, [sp, #12]
	strb	r3, [r2, #1168]
	mov	r2, r10
	ldr	r3, .L2724+12
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2724
	cmp	r6, #0
	ldr	ip, [sp, #12]
	strb	ip, [r3, #1168]
	bne	.L2697
.L2704:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2697
	ldr	r3, .L2724
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	blt	.L2697
	ldrb	r3, [r7, #23]	@ zero_extendqisi2
	sub	r3, r3, #4
	cmp	r3, #4
	movls	r0, #256
	b	.L2711
.L2697:
	cmn	r0, #1
	bne	.L2711
.L2705:
	ldr	r3, .L2724+16
	ldr	ip, [r3, #-108]
	cmp	ip, #0
	bne	.L2700
.L2703:
	ldrb	r3, [r5, #1168]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2724+20
	mov	r2, r9
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	mvneq	r0, #0
	beq	.L2711
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	bl	flash_ddr_tuning_read
	b	.L2711
.L2700:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	blx	ip
	cmn	r0, #1
	bne	.L2711
	b	.L2703
.L2695:
	cmp	r6, #0
	beq	.L2704
	b	.L2711
.L2696:
	cmp	r6, #0
	beq	.L2704
	b	.L2705
.L2711:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2725:
	.align	2
.L2724:
	.word	.LANCHOR0
	.word	.LANCHOR1+2196
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC173
	.fnend
	.size	flash_complete_page_read, .-flash_complete_page_read
	.align	2
	.type	queue_wait_first_req_completed, %function
queue_wait_first_req_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L2816
	ldrb	r5, [r4, #2775]	@ zero_extendqisi2
	cmp	r5, #255
	beq	.L2760
	mov	r7, #48
	mla	r2, r7, r5, r4
	ldrb	r3, [r2, #1278]	@ zero_extendqisi2
	ldr	r6, [r2, #1260]
	sub	r2, r3, #1
	cmp	r2, #10
	ldrls	pc, [pc, r2, asl #2]
	b	.L2760
.L2730:
	.word	.L2729
	.word	.L2731
	.word	.L2732
	.word	.L2732
	.word	.L2732
	.word	.L2732
	.word	.L2733
	.word	.L2734
	.word	.L2735
	.word	.L2732
	.word	.L2735
.L2729:
	mla	r7, r7, r5, r4
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #1244]
	ldr	r1, [r7, #1240]
	cmp	r3, #0
	beq	.L2736
	ldr	r2, .L2816
	ldrb	r0, [r7, #1276]	@ zero_extendqisi2
	ldrb	r2, [r2, #2772]	@ zero_extendqisi2
	cmp	r0, r2
	moveq	r1, r3
.L2736:
	mov	r3, #48
	mov	r0, r6
	mla	r5, r3, r5, r4
	ldr	r2, [r5, #1248]
	bl	flash_complete_page_read
	mov	r3, #13
	strb	r3, [r5, #1278]
	ldrb	r3, [r5, #1238]	@ zero_extendqisi2
	orr	r3, r3, #8
	strb	r3, [r5, #1238]
	str	r0, [r5, #1272]
	b	.L2760
.L2731:
	bl	nandc_wait_flash_ready
	mla	r3, r7, r5, r4
	ldrb	r6, [r3, #1236]	@ zero_extendqisi2
	ldr	r2, [r3, #1244]
	ldr	r10, [r3, #1240]
	mla	r7, r7, r6, r4
	cmp	r2, #0
	ldr	r8, [r7, #1240]
	beq	.L2737
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2816
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r10, r2
.L2737:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #1244]
	cmp	r2, #0
	beq	.L2738
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2816
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r8, r2
.L2738:
	mov	r9, #48
	mov	r1, r10
	mla	r3, r9, r5, r4
	mla	r9, r9, r6, r4
	add	fp, r3, #1232
	add	fp, fp, #12
	ldr	r2, [r3, #1248]
	ldr	r0, [r3, #1260]
	str	r3, [sp, #12]
	bl	flash_complete_plane_page_read
	ldr	r2, [r9, #1248]
	mov	r1, r8
	mov	r7, r0
	ldr	r0, [r9, #1260]
	bl	flash_complete_plane_page_read
	cmn	r7, #1
	mov	r9, r0
	beq	.L2739
	ldr	r3, [sp, #12]
	ldr	r3, [r3, #1256]
	cmn	r3, #1
	beq	.L2740
	ldr	r2, [fp, #4]
	ldr	r2, [r2, #4]
	cmp	r3, r2
	beq	.L2740
.L2739:
	mov	fp, #48
	ldrb	r0, [r4, #1189]	@ zero_extendqisi2
	mla	fp, fp, r5, r4
	ldrb	lr, [r4, #2772]	@ zero_extendqisi2
	rsb	r1, r0, #24
	mvn	r3, #0
	mvn	r3, r3, asl r0
	add	ip, fp, #1232
	add	ip, ip, #12
	str	ip, [sp, #12]
	ldr	r2, [fp, #1260]
	str	lr, [sp]
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	mov	r2, r10
	ldr	r3, [fp, #1248]
	uxtb	r0, r0
	bl	flash_read_page_en
	ldr	r2, [fp, #1256]
	cmn	r2, #1
	mov	r7, r0
	beq	.L2741
	ldr	ip, [sp, #12]
	ldr	r3, [ip, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2741
	ldr	r0, .L2816+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2741
	str	r1, [sp]
	ldr	r0, .L2816+8
	ldr	r1, [fp, #1260]
	ldr	r3, [r3]
	bl	printk
.L2741:
	mov	r3, #48
	mla	r3, r3, r5, r4
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2740
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2740
	ldr	r1, .L2816+12
	movw	r2, #431
	ldr	r0, .L2816+16
	bl	printk
	bl	dump_stack
.L2740:
	mov	r3, #48
	mov	r2, #13
	mla	r5, r3, r5, r4
	cmn	r9, #1
	strb	r2, [r5, #1278]
	ldrb	r2, [r5, #1238]	@ zero_extendqisi2
	str	r7, [r5, #1272]
	orr	r2, r2, #8
	strb	r2, [r5, #1238]
	beq	.L2742
	ldr	r2, .L2816
	mla	r3, r3, r6, r2
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2744
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2744
.L2742:
	mov	r5, #48
	ldrb	r0, [r4, #1189]	@ zero_extendqisi2
	mla	r5, r5, r6, r4
	rsb	r1, r0, #24
	ldrb	ip, [r4, #2772]	@ zero_extendqisi2
	mvn	r3, #0
	mvn	r3, r3, asl r0
	add	r9, r5, #1232
	add	r9, r9, #12
	ldr	r2, [r5, #1260]
	str	ip, [sp]
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	mov	r2, r8
	ldr	r3, [r5, #1248]
	uxtb	r0, r0
	bl	flash_read_page_en
	ldr	r2, [r5, #1256]
	cmn	r2, #1
	beq	.L2746
	ldr	r3, [r9, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2746
	ldr	r0, .L2816+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2746
	str	r1, [sp]
	ldr	r0, .L2816+8
	ldr	r1, [r5, #1260]
	ldr	r3, [r3]
	bl	printk
.L2746:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2744
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2744
	ldr	r1, .L2816+12
	movw	r2, #450
	ldr	r0, .L2816+16
	bl	printk
	bl	dump_stack
.L2744:
	mov	r3, #48
	mla	r4, r3, r6, r4
	mov	r3, #13
	strb	r3, [r4, #1278]
	ldrb	r3, [r4, #1238]	@ zero_extendqisi2
	str	r7, [r4, #1272]
	orr	r3, r3, #8
	strb	r3, [r4, #1238]
	b	.L2760
.L2732:
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	beq	.L2760
	ands	r6, r0, #5
	mov	r3, #48
	beq	.L2748
	mla	r5, r3, r5, r4
	ldr	r0, .L2816+20
	mov	r3, #12
	mov	r2, r7
	mov	r6, r7
	strb	r3, [r5, #1278]
	ldrb	r1, [r5, #1237]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r5, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r5, #1272]
	b	.L2728
.L2748:
	mul	r3, r3, r5
	mov	r2, #13
	ldr	ip, .L2816
	add	r7, r4, r3
	strb	r2, [r7, #1278]
	ldr	r2, [r4, #2804]
	str	r6, [r7, #1272]
	ldr	r1, [r2, #156]
	ldr	r2, .L2816+24
	cmp	r1, r2
	bne	.L2760
	ldr	r2, .L2816+28
	add	r3, r3, r2
	ldrh	r3, [r3, #2]
	cmp	r3, #0
	bne	.L2760
	ldrb	r0, [ip, #1189]	@ zero_extendqisi2
	mvn	r3, #0
	ldr	r2, [r7, #1260]
	rsb	r1, r0, #24
	ldr	r8, .L2816+32
	ldrb	ip, [ip, #2772]	@ zero_extendqisi2
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	str	ip, [sp]
	uxtb	r0, r0
	ldr	r3, [r8, #-100]
	ldr	r2, [r8, #-104]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2749
	ldr	r1, [r7, #1248]
	ldr	r2, [r8, #-100]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2760
.L2749:
	mov	r2, #48
	ldr	r0, .L2816+36
	mla	r5, r2, r5, r4
	ldrb	r2, [r4, #1168]	@ zero_extendqisi2
	ldrb	r1, [r5, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r5, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r5, #1272]
	b	.L2728
.L2735:
	ldr	r2, .L2816+40
	cmp	r3, #11
	mov	r1, #48
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	mvn	r7, #0
	movne	r9, #3
	mla	ip, r1, r5, r2
	rsb	lr, r3, #24
	mvn	r3, r7, asl r3
	and	r0, r3, r6, lsr lr
	moveq	r9, #10
	mov	r10, r3
	uxth	r0, r0
.L2751:
	ldrb	r8, [ip]	@ zero_extendqisi2
	cmp	r8, #255
	beq	.L2815
	mul	r3, r1, r8
	ldr	r7, .L2816
	add	ip, r2, r3
	add	r3, r4, r3
	ldrb	fp, [r3, #1278]	@ zero_extendqisi2
	cmp	fp, r9
	bne	.L2751
	ldr	r3, [r3, #1260]
	and	r3, r10, r3, lsr lr
	cmp	r0, r3
	bne	.L2751
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r6, r0
	beq	.L2814
	ands	r2, r0, #15
	mov	r3, #48
	mul	r3, r3, r5
	beq	.L2755
	add	r7, r7, r3
	mov	r9, #12
	ldr	r0, .L2816+44
	mov	r2, r6
	ldrb	r1, [r7, #1237]	@ zero_extendqisi2
	str	r9, [sp]
	ldr	r3, [r7, #1260]
	bl	printk
	mvn	r3, #0
	strb	r9, [r7, #1278]
	str	r3, [r7, #1272]
	b	.L2754
.L2755:
	add	r6, r7, r3
	mov	r1, #13
	str	r2, [r6, #1272]
	ldr	r2, [r7, #2804]
	strb	r1, [r6, #1278]
	ldr	r1, [r2, #156]
	ldr	r2, .L2816+24
	cmp	r1, r2
	bne	.L2814
	ldr	r2, .L2816+40
	add	r3, r2, r3
	ldrh	r3, [r3, #34]
	cmp	r3, #0
	bne	.L2814
	ldr	r2, [r6, #1260]
	mvn	r3, #0
	ldrb	r0, [r7, #1189]	@ zero_extendqisi2
	ldr	r9, .L2816+32
	rsb	r1, r0, #24
	ldrb	ip, [r7, #2772]	@ zero_extendqisi2
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	str	ip, [sp]
	uxtb	r0, r0
	ldr	r3, [r9, #-100]
	ldr	r2, [r9, #-104]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2757
	ldr	r1, [r6, #1248]
	ldr	r2, [r9, #-100]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2814
.L2757:
	mov	r6, #48
	ldrb	r2, [r4, #1168]	@ zero_extendqisi2
	mla	r6, r6, r5, r4
	ldr	r0, .L2816+48
	ldrb	r1, [r6, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r6, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r6, #1272]
.L2814:
	mov	r6, #0
.L2754:
	mov	r3, #48
	mla	r5, r3, r5, r4
	mla	r4, r3, r8, r4
	ldrb	r2, [r5, #1278]	@ zero_extendqisi2
	ldr	r3, [r5, #1272]
	strb	r2, [r4, #1278]
	str	r3, [r4, #1272]
	b	.L2728
.L2815:
	mvn	r6, #0
	b	.L2728
.L2733:
	mov	r0, r6
	mov	r1, #32
	bl	flash_wait_device_ready
	tst	r0, #32
	beq	.L2760
	ands	r6, r0, #15
	mov	r3, #48
	mlaeq	r5, r3, r5, r4
	movne	r2, #12
	mlane	r3, r3, r5, r4
	moveq	r3, #13
	movne	r6, r0
	streqb	r3, [r5, #1278]
	streq	r6, [r5, #1272]
	strneb	r2, [r3, #1278]
	mvnne	r2, #0
	strne	r2, [r3, #1272]
	b	.L2728
.L2734:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	movne	r3, #48
	mlane	r5, r3, r5, r4
	movne	r3, #7
	strne	r0, [r5, #1272]
	strneb	r3, [r5, #1278]
.L2760:
	mov	r6, #0
.L2728:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2817:
	.align	2
.L2816:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC174
	.word	.LANCHOR1+2224
	.word	.LC0
	.word	.LC175
	.word	1145785929
	.word	.LANCHOR0+1268
	.word	.LANCHOR3
	.word	.LC176
	.word	.LANCHOR0+1236
	.word	.LC177
	.word	.LC178
	.fnend
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_prog_page
	.type	sblk_prog_page, %function
sblk_prog_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldrh	r3, [r0, #34]
	mov	r5, r1
	cmp	r3, #0
	beq	.L2819
	ldr	r3, .L2850
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L2819
	ldr	r0, .L2850+4
	mov	r2, r5
	ldr	r1, [r4, #24]
	bl	printk
.L2819:
	ldr	r8, .L2850+8
	mov	r6, #0
	mov	r9, r8
.L2820:
	cmp	r5, #0
	beq	.L2847
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r7, [r4, #24]
	str	r3, [sp, #4]
.L2821:
	mov	r0, r7
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2849
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2821
.L2849:
	cmp	r5, #1
	beq	.L2823
	ldrb	r3, [r8, #1233]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2823
	ldrb	r3, [r8, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2824
.L2823:
	mov	r0, r4
	bl	queue_prog_cmd
	b	.L2825
.L2824:
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	mvn	fp, #0
	rsb	ip, r3, #24
	mov	ip, r7, lsr ip
	bic	fp, ip, fp, asl r3
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	uxth	fp, fp
	bne	.L2826
	ldr	r1, .L2850+12
	movw	r2, #697
	ldr	r0, .L2850+16
	bl	printk
	bl	dump_stack
.L2826:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	mov	r10, #1
	mla	r3, r2, r3, r9
	ldrb	r2, [r9, #1189]	@ zero_extendqisi2
	ldr	ip, [r3, #1260]
	rsb	r3, r2, #24
	mov	r2, r10, asl r2
	sub	r2, r2, #1
	and	r2, r2, ip, lsr r3
	uxth	r2, r2
	cmp	fp, r2
	bne	.L2827
	ldr	r2, .L2850+20
	ldrh	fp, [r2]
	ldr	r2, .L2850+24
	rsb	r3, fp, r3
	mov	r0, r10, asl fp
	mov	r3, r10, asl r3
	sub	r0, r0, #1
	ldrb	lr, [r2, #-3130]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxth	r0, r0
	sub	lr, lr, #1
	uxth	r3, r3
	uxth	r1, lr
	and	r2, r1, r7, lsr fp
	and	r1, r1, ip, lsr fp
	and	r2, r2, r3
	and	r3, r3, r1
	subs	fp, r2, r3
	and	r7, r7, r0
	and	ip, ip, r0
	movne	fp, #1
	cmp	r7, ip
	movne	fp, #0
	cmp	fp, #0
	beq	.L2828
	cmp	r6, lr
	beq	.L2827
	mov	r0, #17
	ldr	r1, [r4, #24]
	ldr	r2, [r4, #4]
	add	r6, r6, r10
	ldr	r3, [r4, #12]
	bl	flash_start_page_prog
	mov	r3, #9
	strb	r10, [r4, #43]
	mov	r1, r4
	strb	r3, [r4, #42]
	mvn	r3, #0
	ldr	r0, .L2850+28
	strb	r3, [r4]
	bl	buf_add_tail
	b	.L2825
.L2828:
	mov	r0, r4
	mov	r6, fp
	bl	queue_prog_cmd
	b	.L2825
.L2827:
	mov	r0, r4
	mov	r6, #0
	bl	queue_prog_cmd
.L2825:
	subs	r5, r5, #1
	ldrne	r3, .L2850+32
	movne	r4, #48
	ldrne	r2, [sp, #4]
	mlane	r4, r4, r2, r3
	bne	.L2820
.L2847:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2851:
	.align	2
.L2850:
	.word	.LANCHOR2
	.word	.LC179
	.word	.LANCHOR0
	.word	.LANCHOR1+2256
	.word	.LC0
	.word	.LANCHOR3-3132
	.word	.LANCHOR3
	.word	.LANCHOR0+2775
	.word	.LANCHOR0+1236
	.fnend
	.size	sblk_prog_page, .-sblk_prog_page
	.align	2
	.global	sblk_wait_write_queue_completed
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L2857
.L2853:
	ldrb	r3, [r4, #2775]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2856
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2853
.L2856:
	ldmfd	sp!, {r4, pc}
.L2858:
	.align	2
.L2857:
	.word	.LANCHOR0
	.fnend
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
	.align	2
	.global	ftl_flush
	.type	ftl_flush, %function
ftl_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L2865
	ldr	r5, .L2865+4
	ldrb	r1, [r3, #2801]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r1, #0
	beq	.L2860
	ldrb	r2, [r5, #-96]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L2865+8
	mla	r0, r0, r2, r3
	bl	sblk_prog_page
.L2860:
	mvn	r3, #0
	strb	r3, [r5, #-96]
	mov	r3, #0
	strb	r3, [r4, #2801]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_vpn_decrement
.L2866:
	.align	2
.L2865:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.fnend
	.size	ftl_flush, .-ftl_flush
	.align	2
	.global	zftl_cache_flush
	.type	zftl_cache_flush, %function
zftl_cache_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2872
	ldrb	r3, [r3, #2801]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, pc}
	bl	timer_get_time
	ldr	r3, .L2872+4
	ldr	r3, [r3, #-92]
	add	r3, r3, #100
	cmp	r0, r3
	ldmlsfd	sp!, {r3, pc}
	ldmfd	sp!, {r3, lr}
	b	ftl_flush
.L2873:
	.align	2
.L2872:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	ftl_read_page
	.type	ftl_read_page, %function
ftl_read_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	mov	r6, r1
	mov	r5, r2
	mov	r4, r3
	ldr	r8, [sp, #24]
	bl	sblk_wait_write_queue_completed
	mov	r0, r7
	mov	r1, r6
	mov	r2, r5
	mov	r3, r4
	str	r8, [sp, #24]
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_read_page_en
	.fnend
	.size	ftl_read_page, .-ftl_read_page
	.align	2
	.global	ftl_read_ppa_page
	.type	ftl_read_ppa_page, %function
ftl_read_ppa_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L2878
	mov	r7, r1
	mov	r6, r2
	mvn	r2, #0
	mov	r5, r0
	ldrb	r1, [r3, #1189]	@ zero_extendqisi2
	rsb	r4, r1, #24
	mvn	r2, r2, asl r1
	and	r4, r2, r0, lsr r4
	bl	sblk_wait_write_queue_completed
	mov	r1, r5
	uxtb	r4, r4
	mov	r2, r7
	mov	r3, r6
	mov	r0, r4
	str	r8, [sp]
	bl	flash_read_page_en
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2879:
	.align	2
.L2878:
	.word	.LANCHOR0
	.fnend
	.size	ftl_read_ppa_page, .-ftl_read_ppa_page
	.align	2
	.global	sblk_read_page
	.type	sblk_read_page, %function
sblk_read_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r10, .L2908
	mov	r8, r0
	mov	r6, r1
	mov	r4, r0
	mov	r5, r1
.L2881:
	cmp	r5, #0
	beq	.L2904
	ldrb	fp, [r4]	@ zero_extendqisi2
	ldr	r9, [r4, #24]
.L2882:
	mov	r0, r9
	mov	r1, #0
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2906
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2882
.L2906:
	cmp	r5, #1
	beq	.L2888
	ldr	r3, .L2908+4
	ldrb	r3, [r3, #-88]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2888
	ldr	r3, .L2908
	mvn	lr, #0
	ldrb	r2, [r3, #1189]	@ zero_extendqisi2
	rsb	r3, r2, #24
	mov	r3, r9, lsr r3
	bic	lr, r3, lr, asl r2
	uxth	r3, lr
	str	r3, [sp, #4]
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2887
	ldr	r1, .L2908+8
	movw	r2, #782
	ldr	r0, .L2908+12
	bl	printk
	bl	dump_stack
.L2887:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	ldrb	r0, [r10, #1189]	@ zero_extendqisi2
	mvn	ip, #0
	mul	r3, r2, r3
	rsb	r2, r0, #24
	add	r7, r10, r3
	ldr	r1, [r7, #1260]
	mov	r2, r1, lsr r2
	bic	r2, r2, ip, asl r0
	ldr	r0, [sp, #4]
	uxth	r2, r2
	cmp	r0, r2
	bne	.L2888
	ldr	r2, .L2908+16
	ldrh	r2, [r2]
	add	r9, r2, r9
	cmp	r1, r9
	bne	.L2888
	ldr	r0, [r4, #24]
	mvn	r9, #0
	str	r3, [sp, #12]
	add	r5, r5, r9
	ldrb	fp, [r7, #1236]	@ zero_extendqisi2
	bl	flash_start_plane_read
	mov	ip, #2
	mov	r2, #0
	strb	ip, [r4, #42]
	mov	r1, r4
	strb	r2, [r4, #43]
	ldr	r0, .L2908+20
	strb	r9, [r4]
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	bl	buf_add_tail
	ldr	r1, .L2908+24
	strb	r9, [r7, #1236]
	ldr	r0, .L2908+20
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	strb	ip, [r7, #1278]
	strb	r2, [r7, #1279]
	add	r1, r1, r3
	bl	buf_add_tail
	b	.L2886
.L2888:
	mov	r0, r4
	bl	queue_read_cmd
.L2886:
	subs	r5, r5, #1
	ldrne	r3, .L2908+24
	movne	r4, #48
	mlane	r4, r4, fp, r3
	bne	.L2881
.L2904:
	ldr	r4, .L2908+24
	mov	r5, #48
.L2892:
	cmp	r6, #0
	beq	.L2907
	ldrb	r3, [r8, #42]	@ zero_extendqisi2
	cmp	r3, #13
	bne	.L2893
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r6, r6, #1
	cmp	r3, #255
	mlane	r8, r5, r3, r4
.L2893:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2892
.L2907:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2909:
	.align	2
.L2908:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2272
	.word	.LC0
	.word	.LANCHOR3-3066
	.word	.LANCHOR0+2775
	.word	.LANCHOR0+1236
	.fnend
	.size	sblk_read_page, .-sblk_read_page
	.align	2
	.global	gc_check_data_one_wl
	.type	gc_check_data_one_wl, %function
gc_check_data_one_wl:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2946
	ldr	r3, [r6, #2836]
	ldr	r9, [r6, #1092]
	cmp	r3, #0
	bne	.L2911
	mov	r0, #1
	bl	buf_alloc
	str	r0, [r6, #2836]
.L2911:
	ldr	r4, [r6, #2836]
	cmp	r4, #0
	bne	.L2912
	ldr	r1, .L2946+4
	movw	r2, #729
	ldr	r0, .L2946+8
	bl	printk
	bl	dump_stack
.L2912:
	ldr	fp, .L2946+12
	add	r10, r9, #96
	mov	r8, #0
	mov	ip, fp
.L2913:
	ldrb	r3, [r9, #89]	@ zero_extendqisi2
	ldr	r5, .L2946+16
	cmp	r8, r3
	bge	.L2944
	mov	r7, #1
.L2924:
	ldrh	r3, [r5, #16]
	cmp	r7, r3
	bgt	.L2945
	ldr	r2, .L2946+20
	ldrb	r1, [fp, #-3124]	@ zero_extendqisi2
	ldrh	r3, [r10]
	ldrh	r2, [r2]
	cmp	r1, #2
	mul	r2, r2, r3
	beq	.L2914
	ldr	r3, .L2946
	ldrb	r3, [r3, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2915
.L2914:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #1
	add	r3, r3, r2
	add	r3, r3, r7
	orr	r3, r3, r1, asl #24
	b	.L2943
.L2915:
	ldr	r3, .L2946+16
	cmp	r1, #3
	ldrh	r3, [r3, #12]
	addne	r2, r2, r3
	strne	r2, [r4, #24]
	bne	.L2916
	ldr	r1, .L2946
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	addne	r3, r3, r3, asl #1
	addeq	r3, r2, r3
	subne	r3, r3, #1
	orreq	r3, r3, r7, asl #24
	addne	r3, r3, r2
	addne	r3, r3, r7
	orrne	r3, r3, #50331648
.L2943:
	str	r3, [r4, #24]
.L2916:
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #28]
	bl	sblk_read_page
	ldr	r2, [r4, #36]
	cmn	r2, #1
	ldr	ip, [sp, #28]
	beq	.L2919
	ldr	r0, [ip, #-172]
	ldrh	r1, [r5, #18]
	ldr	r3, [r4, #12]
	ldr	lr, [r0, r1, asl #2]
	ldr	r0, [r3, #4]
	cmp	lr, r0
	bne	.L2919
	ldr	r0, [fp, #-168]
	ldr	r3, [r3, #8]
	ldr	r1, [r0, r1, asl #2]
	cmp	r1, r3
	beq	.L2920
.L2919:
	ldrh	r3, [r5, #18]
	ldr	r1, [ip, #-172]
	mov	r0, r3, asl #2
	ldr	r3, [r1, r3, asl #2]
	cmn	r3, #1
	beq	.L2920
	ldr	r1, .L2946+24
	ldr	r1, [r1]
	tst	r1, #1024
	beq	.L2921
	ldr	ip, .L2946+12
	ldr	r1, [r4, #12]
	ldr	ip, [ip, #-168]
	ldr	r0, [ip, r0]
	str	r0, [sp]
	ldr	r0, [r1]
	str	r0, [sp, #4]
	ldr	r0, [r1, #4]
	str	r0, [sp, #8]
	ldr	r0, [r1, #8]
	str	r0, [sp, #12]
	ldr	r1, [r1, #12]
	ldr	r0, .L2946+28
	str	r1, [sp, #16]
	ldr	r1, [r4, #24]
	bl	printk
.L2921:
	ldrh	r3, [r9, #80]
	mov	r1, #0
	ldr	r2, [r6, #1088]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, [r6, #1092]
	ldr	r3, [r2, #556]
	add	r3, r3, #1
	str	r3, [r2, #556]
	ldr	r3, [r6, #2804]
	ldr	r2, [r3, #156]
	ldr	r3, .L2946+32
	cmp	r2, r3
	bne	.L2927
	ldr	r3, .L2946+12
	ldrb	r3, [r3, #-2536]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2927
	ldr	r3, .L2946+12
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2927
	ldr	r0, [r4, #24]
	bl	ftl_mask_bad_block
	b	.L2927
.L2920:
	ldrh	r3, [r5, #18]
	add	r7, r7, #1
	add	r3, r3, #1
	strh	r3, [r5, #18]	@ movhi
	b	.L2924
.L2945:
	add	r8, r8, #1
	add	r10, r10, #2
	b	.L2913
.L2944:
	ldrh	r3, [r5, #12]
	add	r2, r3, #1
	strh	r2, [r5, #12]	@ movhi
	ldr	r2, .L2946+12
	ldrb	r0, [r2, #-3122]	@ zero_extendqisi2
	cmp	r0, #0
	addne	r3, r3, #2
	strneh	r3, [r5, #12]	@ movhi
	movne	r0, #0
	b	.L2922
.L2927:
	mvn	r0, #0
.L2922:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2947:
	.align	2
.L2946:
	.word	.LANCHOR0
	.word	.LANCHOR1+2288
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR0+2828
	.word	.LANCHOR3-3066
	.word	.LANCHOR2
	.word	.LC180
	.word	1145785929
	.fnend
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	sblk_tlc_prog_one_page
	.type	sblk_tlc_prog_one_page, %function
sblk_tlc_prog_one_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r3, [r0]
	ldr	r6, [r3, #24]
.L2949:
	mov	r0, r6
	mov	r1, #1
	bl	queue_lun_state
	subs	r5, r0, #0
	beq	.L2952
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2949
.L2952:
	mov	r0, r4
	mov	r1, #1
	bl	queue_tlc_prog_cmd
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	sblk_tlc_prog_one_page, .-sblk_tlc_prog_one_page
	.align	2
	.global	sblk_xlc_prog_pages
	.type	sblk_xlc_prog_pages, %function
sblk_xlc_prog_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r0]
	mov	r5, r0
	mov	r7, r1
	mov	r10, r2
	ldr	r4, [r3, #24]
.L2954:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r9, r0, #0
	beq	.L2966
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2954
.L2966:
	cmp	r10, #2
	bne	.L2956
	ldr	r2, .L2968
	ldr	lr, [r7]
	ldrb	r3, [r2, #1233]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r4, [lr, #24]
	beq	.L2961
	ldr	r3, [r5]
	mov	fp, #1
	ldrb	r4, [r2, #1189]	@ zero_extendqisi2
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	ldr	r0, [r3, #24]
	rsb	r1, r4, #24
	mov	r4, fp, asl r4
	mov	ip, fp, asl r1
	sub	r4, r4, #1
	ldr	r6, [lr, #24]
	and	r4, r4, r0, lsr r1
	cmp	r2, #0
	sub	ip, ip, #1
	and	r8, ip, r0
	and	r6, r6, ip
	uxtb	r4, r4
	beq	.L2958
	mov	r0, r4
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r5]
	mov	r1, fp
	mov	r0, r9
	str	r8, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7]
	mov	r1, fp
	mov	r0, r9
	str	r6, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	add	r2, r8, fp
	mov	r1, r10
	mov	r0, r9
	add	r8, r8, #2
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #4]
	add	r2, r6, fp
	mov	r1, r10
	mov	r0, r9
	add	r6, r6, #2
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #8]
	mov	r0, r9
	mov	r1, #3
	str	r8, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #8]
	mov	r0, r9
	mov	r1, #3
	str	r6, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #16
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_one_pass_page_prog
	b	.L2959
.L2958:
	ldrb	r0, [r3, #44]	@ zero_extendqisi2
	mov	r1, fp
	str	r8, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7]
	ldr	r2, [r5]
	mov	r1, fp
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r6, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldmia	r5, {r2, r3}
	mov	r1, r10
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r8, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #4]
	ldr	r2, [r5]
	mov	r1, r10
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r6, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #8]
	ldr	r2, [r5]
	mov	r1, #3
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r8, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #8]
	ldr	r2, [r5]
	mov	r1, #3
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r6, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #16
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
.L2959:
	ldr	r1, [r5]
	mov	r3, #5
	ldr	r0, .L2968+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2960
.L2961:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r6, r0, #0
	beq	.L2967
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2961
.L2967:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
	mov	r0, r7
	mov	r1, r6
	bl	queue_tlc_prog_cmd
.L2963:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2960
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2963
.L2956:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
.L2960:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2969:
	.align	2
.L2968:
	.word	.LANCHOR0
	.word	.LANCHOR0+2775
	.fnend
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	sblk_3d_mlc_prog_pages
	.type	sblk_3d_mlc_prog_pages, %function
sblk_3d_mlc_prog_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	add	r5, r0, #8
	ldr	r9, .L2978
	mov	r7, r1
	mov	r8, #1
.L2971:
	cmp	r7, #0
	beq	.L2976
	ldr	r3, [r5, #-8]
	ldr	r4, [r3, #24]
.L2972:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2977
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2972
.L2977:
	ldr	r3, [r5, #-8]
	sub	r7, r7, #1
	ldrb	r4, [r9, #1189]	@ zero_extendqisi2
	add	r5, r5, #8
	ldr	r2, [r3, #24]
	rsb	r3, r4, #24
	mov	r4, r8, asl r4
	mov	r6, r8, asl r3
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, r2, lsr r3
	and	r6, r6, r2
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
	mov	r0, r4
	bl	nandc_de_cs
	ldr	r1, [r5, #-16]
	mov	r3, #4
	ldr	r0, .L2978+4
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r8, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2971
.L2976:
	mov	r0, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2979:
	.align	2
.L2978:
	.word	.LANCHOR0
	.word	.LANCHOR0+2775
	.fnend
	.size	sblk_3d_mlc_prog_pages, .-sblk_3d_mlc_prog_pages
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	.fnstart
	@ args = 8, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r8, r3
	ldr	r6, .L3010
	ldrb	r3, [sp, #68]	@ zero_extendqisi2
	mov	r7, r0
	mov	r5, r1
	mov	r9, r2
	ubfx	r4, r1, #24, #2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2981
	ldr	r1, .L3010+4
	movw	r2, #642
	ldr	r0, .L3010+8
	bl	printk
	bl	dump_stack
.L2981:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L3010
	cmp	r2, r7
	mvnls	r0, #0
	bls	.L2982
	add	r2, r3, r7
	cmp	r4, #0
	ldrb	r10, [r2, #1180]	@ zero_extendqisi2
	bne	.L2994
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2984
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2994
.L2984:
	ldrh	fp, [r6, #2]
	mov	r0, r5
	mov	r1, fp
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L3010
	mul	r0, fp, r0
	rsb	r4, r0, r5
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
	b	.L2983
.L2994:
	mov	r4, r5
.L2983:
	ldr	fp, .L3010+12
	ldr	r6, .L3010+16
.L2989:
	ldr	r3, [fp]
	tst	r3, #16
	beq	.L2986
	ldr	r0, .L3010+20
	mov	r1, r10
	mov	r2, r5
	mov	r3, r4
	bl	printk
.L2986:
	ldr	r3, [sp, #64]
	mov	r0, r10
	mov	r1, r4
	mov	r2, r9
	str	r3, [sp]
	mov	r3, r8
	bl	flash_prog_page
	ldr	r3, [sp, #16]
	mov	ip, r0
	cmp	r3, #0
	beq	.L2987
	ldr	r3, [sp, #64]
	mov	r1, r5
	str	r0, [sp, #20]
	mov	r0, r7
	str	r3, [sp]
	ldr	r3, [r6, #-100]
	ldr	r2, [r6, #-104]
	bl	flash_read_page_en
	sub	r3, r0, #512
	cmp	r0, #512
	cmnne	r0, #1
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp, #12]
	beq	.L2988
	ldr	r3, [r6, #-104]
	ldr	r2, [r9]
	ldr	ip, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2988
	ldr	r3, [r6, #-100]
	ldr	r2, [r8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2987
.L2988:
	mov	r2, #4
	ldr	r0, .L3010+24
	mov	r3, r2
	mov	r1, r9
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3010+28
	mov	r1, r8
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3010+32
	ldr	r1, [r6, #-100]
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3010+36
	ldr	r1, [r6, #-104]
	bl	rknand_print_hex
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L2989
.L2991:
	mov	r1, r5
	ldr	r0, .L3010+40
	bl	printk
	ldr	r1, .L3010+4
	movw	r2, #685
	ldr	r0, .L3010+8
	bl	printk
	bl	dump_stack
	mvn	ip, #0
	b	.L2990
.L2987:
	cmn	ip, #1
	beq	.L2991
.L2990:
	mov	r0, ip
.L2982:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3011:
	.align	2
.L3010:
	.word	.LANCHOR0
	.word	.LANCHOR1+2312
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC181
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.word	.LC185
	.word	.LC186
	.fnend
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_prog_page
	.type	ftl_prog_page, %function
ftl_prog_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r4, r3
	mov	r7, r0
	mov	r5, r1
	mov	r6, r2
	bl	sblk_wait_write_queue_completed
	mov	r0, r7
	mov	r1, r5
	mov	r2, r6
	ldr	r3, [sp, #32]
	str	r3, [sp]
	mov	r3, #1
	str	r3, [sp, #4]
	mov	r3, r4
	bl	flash_prog_page_en
	cmn	r0, #1
	mov	r4, r0
	bne	.L3013
	ldr	r1, .L3015
	movw	r2, #2678
	ldr	r0, .L3015+4
	bl	printk
	bl	dump_stack
	ldr	r0, .L3015+8
	mov	r1, r5
	bl	printk
.L3013:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L3016:
	.align	2
.L3015:
	.word	.LANCHOR1+2332
	.word	.LC0
	.word	.LC186
	.fnend
	.size	ftl_prog_page, .-ftl_prog_page
	.align	2
	.global	ftl_info_flush
	.type	ftl_info_flush, %function
ftl_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r6, .L3052
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	ldr	r0, .L3052+4
	ldr	r4, .L3052+8
	mov	r8, #0
	ldrb	r2, [r6, #2772]	@ zero_extendqisi2
	ldr	r9, .L3052+12
	mov	r7, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, [r6, #2804]
	ldrh	r2, [r3, #74]
	cmp	r2, #1
	movhi	r2, #0
	strhih	r2, [r3, #150]	@ movhi
.L3029:
	ldrh	r3, [r4, #202]
	ldr	r2, [r6, #2804]
	ldrb	r1, [r6, #2772]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, [r2, #4]
	ldr	r0, [r4, #204]
	mov	r1, r1, asl #9
	add	r3, r3, #1
	str	r3, [r2, #4]
	ldrb	r5, [r4, #200]	@ zero_extendqisi2
	str	r3, [r4, #-52]
	ldrh	fp, [r9]
	str	r10, [r4, #-56]
	bl	js_hash
	ldr	r3, .L3052+16
	ldr	r3, [r3]
	tst	r3, #4096
	str	r0, [r4, #-48]
	beq	.L3019
	ldr	r3, .L3052
	ldr	r0, .L3052+20
	ldrb	r1, [r4, #200]	@ zero_extendqisi2
	ldr	r3, [r3, #2804]
	ldrh	r2, [r4, #202]
	ldr	r3, [r3, #4]
	bl	printk
.L3019:
	ldr	r3, .L3052+24
	ldrh	r0, [r7, #202]
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L3020
.L3025:
	ldrb	r3, [r7, #201]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r7, #201]
	cmp	r3, #7
	bls	.L3021
	mov	r3, #8
.L3024:
	ldr	r2, [r6, #1176]
	sub	r5, r3, #8
	add	r2, r2, r3
	uxth	r5, r5
	ldrb	r1, [r2, #32]	@ zero_extendqisi2
	add	r2, r1, #127
	uxtb	r2, r2
	cmp	r2, #125
	bhi	.L3022
	ldr	r1, .L3052+28
	movw	r2, #846
	ldr	r0, .L3052+32
	bl	printk
	bl	dump_stack
	b	.L3023
.L3022:
	cmp	r1, #255
	bne	.L3023
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L3024
	mov	r5, #8
.L3023:
	mov	r8, #1
	strb	r5, [r7, #201]
.L3021:
	ldr	r3, [r6, #1176]
	ldrb	r2, [r7, #201]	@ zero_extendqisi2
	add	r3, r3, r2
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	strb	r3, [r7, #200]
	beq	.L3025
	ldrh	r5, [r9]
	mov	r0, #0
	mul	r5, r5, r3
	mov	r1, r5
	bl	flash_erase_block
	ldr	r3, .L3052
	mov	r1, r5
	mov	r0, #0
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, .L3052+4
	ldr	r2, [r4, #204]
	bl	ftl_prog_page
	mov	r3, #1
	strh	r3, [r4, #202]	@ movhi
	add	r5, r5, r3
	b	.L3026
.L3020:
	ldr	r3, [sp, #12]
	cmp	r0, #0
	mla	r5, fp, r5, r3
	bne	.L3026
	mov	r1, r5
	bl	flash_erase_block
.L3026:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	mov	r0, #0
	ldr	fp, .L3052+8
	mov	r1, r5
	str	r3, [sp]
	sub	r3, fp, #56
	ldr	r2, [r7, #204]
	bl	ftl_prog_page
	ldrh	r3, [r7, #202]
	add	r3, r3, #1
	strh	r3, [r7, #202]	@ movhi
	cmn	r0, #1
	beq	.L3027
	ldrb	r3, [r4, #208]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3028
.L3027:
	mov	r3, #0
	strb	r3, [r7, #208]
	b	.L3029
.L3028:
	cmp	r8, #1
	beq	.L3030
.L3038:
	ldr	r3, .L3052+8
	ldrb	r3, [r3, #200]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L3032
	ldr	r1, .L3052+28
	movw	r2, #890
	ldr	r0, .L3052+32
	bl	printk
	bl	dump_stack
	b	.L3032
.L3030:
	ldrb	r4, [fp, #201]	@ zero_extendqisi2
	add	r4, r4, #1
.L3033:
	cmp	r4, #7
	bhi	.L3038
	ldr	r3, [r6, #1176]
	add	r3, r3, r4
	ldrb	r5, [r3, #40]	@ zero_extendqisi2
	add	r3, r5, #127
	uxtb	r3, r3
	cmp	r3, #125
	bhi	.L3034
	ldr	r1, .L3052+28
	movw	r2, #881
	ldr	r0, .L3052+32
	bl	printk
	bl	dump_stack
	b	.L3035
.L3034:
	cmp	r5, #255
	beq	.L3036
.L3035:
	ldr	r3, .L3052+12
	mov	r0, #0
	ldrh	r1, [r3]
	mul	r1, r1, r5
	bl	flash_erase_block
.L3036:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L3033
.L3032:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3053:
	.align	2
.L3052:
	.word	.LANCHOR0
	.word	.LANCHOR3-56
	.word	.LANCHOR3
	.word	.LANCHOR3-3066
	.word	.LANCHOR2
	.word	.LC187
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2348
	.word	.LC0
	.fnend
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #1
	ldr	r4, .L3087
	mov	r6, #0
	ldr	r5, .L3087+4
	mov	r1, r6
	mov	r2, #16384
	.pad #28
	sub	sp, sp, #28
	strb	r3, [r4, #208]
	mov	r10, r4
	strb	r3, [r4, #210]
	movw	r3, #1076
	ldr	r0, [r4, #204]
	ldrh	r3, [r5, r3]
	strb	r6, [r4, #209]
	str	r0, [r5, #1080]
	add	r3, r0, r3, asl #2
	str	r3, [r5, #2804]
	bl	ftl_memset
	mov	r1, r6
	ldr	r0, [r4, #212]
	mov	r2, #16384
	bl	ftl_memset
	ldr	r3, [r5, #1176]
	ldr	fp, .L3087+8
	strb	r6, [r4, #201]
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	strh	r6, [r4, #202]	@ movhi
	mov	r6, #7
	strb	r3, [r4, #200]
.L3059:
	ldr	r3, [r5, #1176]
	uxth	r7, r6
	ldr	ip, .L3087+4
	add	r3, r3, r6
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3055
	ldr	r2, .L3087+12
	mov	r0, #0
	str	ip, [sp, #12]
	ldrh	r9, [r2]
	ldr	r2, [r4, #204]
	mul	r9, r9, r3
	ldrb	r3, [ip, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, .L3087+16
	mov	r1, r9
	bl	ftl_read_page
	cmn	r0, #1
	mov	r8, r0
	ldr	ip, [sp, #12]
	bne	.L3056
	ldrb	r3, [ip, #2772]	@ zero_extendqisi2
	mov	r0, #0
	add	r1, r9, #1
	str	r3, [sp]
	ldr	r3, .L3087+16
	ldr	r2, [r10, #204]
	bl	ftl_read_page
	mov	r8, r0
.L3056:
	ldr	r3, .L3087+20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3057
	ldr	r3, [r5, #2804]
	mov	r1, r6
	ldr	r0, .L3087+24
	mov	r2, r8
	ldr	r3, [r3]
	str	r3, [sp]
	movw	r3, #749
	bl	printk
.L3057:
	cmn	r8, #1
	beq	.L3055
	ldr	r3, [r5, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	beq	.L3069
.L3055:
	subs	r6, r6, #1
	bcs	.L3059
	movw	r6, #65535
	mov	r7, #0
	b	.L3058
.L3069:
	mov	r6, r7
.L3058:
	ldr	r3, .L3087+20
	sxth	r6, r6
	ldr	r2, [r3]
	str	r3, [sp, #16]
	tst	r2, #4096
	beq	.L3060
	ldr	r3, [r5, #2804]
	mov	r1, r6
	ldr	r0, .L3087+28
	mov	r2, #4800
	ldr	r3, [r3]
	bl	printk
.L3060:
	cmn	r6, #1
	bne	.L3061
	mov	r2, #16384
	ldr	r0, [r4, #204]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r5, #2804]
	ldr	r2, .L3087+8
	mov	r0, r6
	str	r2, [r3]
	ldr	r2, .L3087+32
	ldr	r3, [r5, #2804]
	str	r2, [r3, #12]
	b	.L3062
.L3061:
	ldr	r3, [r5, #1176]
	mov	r0, #0
	ldr	r2, [r4, #204]
	sxtah	r3, r3, r7
	strb	r7, [r4, #201]
	ldr	r6, .L3087+36
	mov	r7, #0
	ldrb	r1, [r3, #40]	@ zero_extendqisi2
	mov	r3, #4
	str	r3, [sp]
	ldr	r3, .L3087+16
	strb	r1, [r4, #200]
	bl	flash_get_last_written_page
	ldrh	r8, [r6, #-10]
	add	r6, r6, #3056
	ldr	fp, .L3087+8
	uxth	r9, r0
	mov	r10, r0
	add	r3, r9, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	ldrb	r3, [r4, #200]	@ zero_extendqisi2
	mul	r8, r8, r3
.L3063:
	rsb	r3, r7, r9
	tst	r3, #32768
	bne	.L3067
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	add	r1, r10, r8
	ldr	ip, .L3087+4
	mov	r0, #0
	rsb	r1, r7, r1
	ldr	r2, [r4, #204]
	str	r3, [sp]
	ldr	r3, .L3087+16
	str	ip, [sp, #20]
	bl	ftl_read_page
	cmn	r0, #1
	beq	.L3064
	ldr	ip, [sp, #20]
	ldr	r3, [ip, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L3064
	ldr	r3, [r6, #-48]
	cmp	r3, #0
	bne	.L3065
.L3067:
	ldrh	r3, [sp, #12]
	strh	r3, [r4, #202]	@ movhi
	bl	ftl_tmp_into_update
	ldr	r2, [r5, #2804]
	mov	r0, #0
	ldr	r3, [r2, #64]
	add	r3, r3, #1
	str	r3, [r2, #64]
	bl	ftl_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	ldr	r3, [sp, #16]
	ldr	r0, [r3]
	ands	r0, r0, #16384
	beq	.L3062
	ldr	r3, .L3087+4
	ldr	r0, .L3087+40
	ldr	r3, [r3, #2804]
	ldr	r1, [r3, #156]
	bl	printk
	mov	r0, #0
	b	.L3062
.L3065:
	ldrb	r1, [ip, #2772]	@ zero_extendqisi2
	ldr	r0, [r6, #204]
	str	r3, [sp, #20]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	beq	.L3067
	ldr	r0, .L3087+44
	ldr	r1, [r6, #-48]
	bl	printk
.L3064:
	add	r7, r7, #1
	b	.L3063
.L3062:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3088:
	.align	2
.L3087:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	1229739078
	.word	.LANCHOR3-3066
	.word	.LANCHOR3-56
	.word	.LANCHOR2
	.word	.LC188
	.word	.LC189
	.word	393252
	.word	.LANCHOR3-3056
	.word	.LC191
	.word	.LC190
	.fnend
	.size	ftl_info_blk_init, .-ftl_info_blk_init
	.align	2
	.global	ftl_ext_info_flush
	.type	ftl_ext_info_flush, %function
ftl_ext_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	bl	timer_get_time
	ldr	r3, .L3105
	ldr	r4, .L3105+4
	umull	r0, r1, r0, r3
	ldr	r3, [r4, #1092]
	ldr	r0, [r3, #520]
	mov	r1, r1, lsr #5
	cmp	r1, r0
	ldrhi	r2, [r3, #12]
	rsbhi	r2, r0, r2
	addhi	r2, r2, r1
	strhi	r2, [r3, #12]
	bhi	.L3104
.L3090:
	bcs	.L3091
.L3104:
	str	r1, [r3, #520]
.L3091:
	mov	r0, #0
	ldr	r7, .L3105+8
	bl	ftl_total_vpn_update
	ldr	r6, .L3105+12
.L3095:
	ldr	r3, [r4, #2804]
	ldr	r2, [r3, #56]
	add	r2, r2, #1
	str	r2, [r3, #56]
	ldrh	r2, [r3, #140]
	ldrh	r3, [r7]
	cmp	r2, r3
	bcc	.L3093
	bl	ftl_ext_alloc_new_blk
.L3093:
	ldr	r3, [r4, #2804]
	ldrh	r2, [r3, #130]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3094
	ldr	r1, .L3105+16
	movw	r2, #2211
	ldr	r0, .L3105+20
	bl	printk
	bl	dump_stack
.L3094:
	ldr	r1, [r4, #2804]
	ldr	r0, .L3105+24
	ldrb	r2, [r4, #1189]	@ zero_extendqisi2
	ldrh	r3, [r1, #130]
	ldrh	r0, [r0]
	rsb	r2, r2, #24
	ldr	r8, .L3105+12
	rsb	r2, r0, r2
	mvn	r0, #0
	mov	r9, r3, asr r2
	bic	r3, r3, r0, asl r2
	ldr	r2, .L3105+28
	sub	r0, r8, #56
	sxth	r3, r3
	ldrh	r5, [r2]
	ldrh	r2, [r1, #140]
	mov	r1, #0
	mla	r5, r5, r3, r2
	ldrb	r2, [r4, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r3, #0
	str	r3, [r6, #-56]
	ldr	r3, [r4, #2804]
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, [r6, #212]
	ldr	r3, [r3, #56]
	mov	r1, r1, asl #9
	str	r3, [r6, #-52]
	bl	js_hash
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	mov	r1, r5
	ldr	r2, [r6, #212]
	str	r0, [r6, #-48]
	uxtb	r0, r9
	str	r3, [sp]
	sub	r3, r8, #56
	bl	ftl_prog_page
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #140]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #140]	@ movhi
	cmp	r3, #1
	beq	.L3095
	cmn	r0, #1
	beq	.L3096
	ldrb	r0, [r8, #210]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3097
.L3096:
.L3092:
	mov	r3, #0
	strb	r3, [r8, #210]
	b	.L3095
.L3097:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3106:
	.align	2
.L3105:
	.word	1374389535
	.word	.LANCHOR0
	.word	.LANCHOR3-3096
	.word	.LANCHOR3
	.word	.LANCHOR1+2364
	.word	.LC0
	.word	.LANCHOR3-3132
	.word	.LANCHOR3-3066
	.fnend
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L3127
	mov	r2, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	sub	r3, ip, #2528
	ldr	r4, .L3127+4
	mvn	r5, #0
	strh	r2, [r3, #-12]	@ movhi
	sub	r2, ip, #3120
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r4, #2804]
	ldrb	r9, [r4, #1189]	@ zero_extendqisi2
	ldrh	r6, [r2, #-12]
	ldrh	r3, [r3, #130]
	rsb	r8, r9, #24
	rsb	r6, r6, r8
	str	ip, [sp, #12]
	bic	r5, r3, r5, asl r6
	mov	fp, r3, asr r6
	mov	r3, #4
	mov	r1, r5
	str	r3, [sp]
	uxtb	r10, fp
	ldr	r2, [ip, #212]
	sub	r3, ip, #56
	mov	r0, r10
	bl	flash_get_last_written_page
	ldr	r3, .L3127+8
	ldr	r2, [r3]
	tst	r2, #4096
	mov	r7, r0
	str	r3, [sp, #16]
	beq	.L3108
	uxth	fp, fp
	str	fp, [sp]
	ldr	r3, [r4, #2804]
	mov	r2, #2256
	ldr	r0, .L3127+12
	ldr	r1, .L3127+16
	ldrh	r3, [r3, #130]
	str	r3, [sp, #4]
	mov	r3, r7
	bl	printk
.L3108:
	ldr	r9, .L3127
	mov	r8, #0
	ldr	fp, .L3127+20
.L3109:
	uxth	r6, r7
	rsb	r3, r8, r6
	tst	r3, #32768
	bne	.L3114
	ldr	r3, .L3127+24
	rsb	r1, r8, r7
	ldrb	r2, [r4, #2772]	@ zero_extendqisi2
	mov	r0, r10
	ldr	ip, .L3127+4
	ldrh	r3, [r3]
	str	r2, [sp]
	str	ip, [sp, #20]
	mla	r1, r3, r5, r1
	ldr	r3, [sp, #12]
	ldr	r2, [r3, #212]
	ldr	r3, .L3127+28
	bl	flash_read_page_en
	cmp	r0, #512
	cmnne	r0, #1
	ldr	ip, [sp, #20]
	beq	.L3110
	ldr	r3, [ip, #1092]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L3110
	ldr	r3, [r9, #-48]
	cmp	r3, #0
	bne	.L3111
.L3114:
	bl	zftl_sblk_list_init
	ldr	r3, [r4, #2804]
	ldrh	r2, [r3, #140]
	cmp	r2, r7
	bgt	.L3113
	add	r6, r6, #1
	strh	r6, [r3, #140]	@ movhi
	bl	ftl_ext_info_flush
	b	.L3113
.L3111:
	ldrb	r1, [ip, #2772]	@ zero_extendqisi2
	ldr	r0, [r9, #212]
	str	r3, [sp, #20]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	beq	.L3114
	ldr	r0, .L3127+32
	ldr	r1, [r9, #-48]
	bl	printk
.L3110:
	add	r8, r8, #1
	b	.L3109
.L3113:
	ldr	r5, [r4, #1092]
	bl	timer_get_time
	ldr	r3, .L3127+36
	umull	r0, r1, r0, r3
	mov	r3, r1, lsr #5
	str	r3, [r5, #520]
	ldr	r5, [r4, #1092]
	bl	timer_get_time
	ldr	r3, [r4, #1092]
	mov	r2, #0
	add	r1, r3, #584
	str	r0, [r5, #604]
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
	ldr	r2, [sp, #16]
	str	r1, [r3, #560]
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L3116
	ldr	r5, [r3, #520]
	ldr	r4, [r3, #12]
	bl	timer_get_time
	ldr	r1, .L3127+16
	mov	r2, r5
	mov	r3, r4
	str	r0, [sp]
	ldr	r0, .L3127+40
	bl	printk
.L3116:
	ldr	r3, .L3127+44
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3128:
	.align	2
.L3127:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC192
	.word	.LANCHOR1+2384
	.word	1162432070
	.word	.LANCHOR3-3066
	.word	.LANCHOR3-56
	.word	.LC193
	.word	1374389535
	.word	.LC194
	.word	.LANCHOR3-3152
	.fnend
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_prog_ppa_page
	.type	ftl_prog_ppa_page, %function
ftl_prog_ppa_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, lr}
	.save {r4, lr}
	.pad #8
	mov	lr, r2
	ldr	r2, .L3131
	mov	ip, r0
	mov	r4, r1
	ldrb	r0, [r2, #1189]	@ zero_extendqisi2
	mov	r2, #1
	str	r3, [sp]
	mov	r3, lr
	rsb	r1, r0, #24
	mov	r0, r2, asl r0
	sub	r0, r0, #1
	and	r0, r0, ip, lsr r1
	mov	r1, r2, asl r1
	sub	r1, r1, #1
	mov	r2, r4
	and	r1, r1, ip
	uxtb	r0, r0
	bl	ftl_prog_page
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L3132:
	.align	2
.L3131:
	.word	.LANCHOR0
	.fnend
	.size	ftl_prog_ppa_page, .-ftl_prog_ppa_page
	.align	2
	.global	ftl_write_last_log_page
	.type	ftl_write_last_log_page, %function
ftl_write_last_log_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrh	r3, [r0, #6]
	ldr	r5, .L3138
	cmp	r3, #1
	ldrh	r8, [r0, #12]
	ldr	r6, [r5, #-2548]
	bne	.L3135
	bl	ftl_get_new_free_page
	cmn	r0, #1
	mov	r7, r0
	beq	.L3136
	ldrh	r0, [r4]
	add	r6, r6, r8, asl #2
	bl	ftl_vpn_decrement
	ldr	r3, .L3138+4
	mov	r0, r6
	mov	r4, #0
	str	r3, [r5, #264]
	sub	r3, r5, #3088
	ldrh	r1, [r3, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	mul	r1, r3, r1
	mov	r1, r1, asl #2
	bl	js_hash
	mov	r1, r5
	str	r4, [r1, #280]!
	mov	r2, r4
	str	r4, [r5, #272]
	str	r4, [r5, #276]
	str	r0, [r5, #268]
	mov	r0, #2
	bl	ftl_debug_info_fill
	ldr	r3, .L3138+8
	mov	r0, r7
	mov	r1, r6
	add	r2, r5, #264
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	b	.L3136
.L3135:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3136:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3139:
	.align	2
.L3138:
	.word	.LANCHOR3
	.word	-178307901
	.word	.LANCHOR0
	.fnend
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3182
	movw	r2, #1076
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldrh	r2, [r3, r2]
	.pad #116
	sub	sp, sp, #116
	cmp	r2, r0
	bls	.L3140
	mov	fp, r3
	ldr	r3, .L3182+4
	ldrb	r2, [r3, #-3116]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3142
	ldrb	r2, [r3, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3140
.L3142:
	ldrb	r2, [fp, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3140
	ldr	r2, .L3182
	mov	r4, r6, asl #2
	add	r1, sp, #96
	mov	r0, r6
	strh	r6, [sp, #80]	@ movhi
	mov	r5, #0
	ldr	r2, [r2, #1080]
	mov	r7, r5
	ldr	r8, .L3182+8
	mov	r9, r5
	add	r2, r2, r4
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #160
	ldreqb	r10, [r3, #-3124]	@ zero_extendqisi2
	movne	r10, #1
	bl	ftl_get_blk_list_in_sblk
	mov	r2, r8
	ldrh	r3, [r2, #-8]!
	uxtb	r0, r0
	strb	r0, [sp, #89]
	str	r2, [sp, #44]
	smulbb	r0, r3, r0
	ldr	r2, [fp, #1080]
	strh	r5, [sp, #82]	@ movhi
	add	r3, r2, r4
	strb	r5, [sp, #85]
	strh	r5, [sp, #90]	@ movhi
	strh	r0, [sp, #86]	@ movhi
	ldrh	r1, [r2, r4]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r2, [r2, r6, asl #2]
	mov	r1, r6
	ldr	r0, .L3182+12
	ubfx	r2, r2, #11, #8
	str	r2, [sp, #4]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	add	r3, r8, #3088
	mov	r4, r0
	str	r3, [sp, #48]
.L3144:
	ldr	r3, [sp, #44]
	uxth	r8, r5
	ldrh	r3, [r3]
	cmp	r3, r8
	bls	.L3167
	mov	r3, r8, asl #1
	mov	ip, #0
	add	r2, r3, r8
	sub	r3, r3, #1
	sub	r2, r2, #1
	str	r3, [sp, #60]
	str	r2, [sp, #52]
.L3155:
	ldrb	r3, [sp, #89]	@ zero_extendqisi2
	uxth	r9, ip
	cmp	r3, r9
	bls	.L3179
	add	r3, r9, #8
	mov	r7, #1
	mov	r3, r3, asl r7
	str	r3, [sp, #64]
.L3154:
	cmp	r7, r10
	bhi	.L3180
	ldr	r2, [sp, #64]
	add	r3, sp, #112
	add	r3, r3, r2
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3146
	ldr	r3, .L3182+16
	cmp	r10, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3147
	ldr	r1, .L3182
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #52]
	orreq	r3, r3, r7, asl #24
	beq	.L3149
	b	.L3178
.L3147:
	cmp	r10, #2
	uxtahne	r3, r3, r5
	bne	.L3149
	ldr	r1, [sp, #60]
.L3178:
	add	r3, r3, r1
	ldr	r1, [sp, #48]
	add	r3, r3, r7
	ldrb	r1, [r1, #-3124]	@ zero_extendqisi2
	orr	r3, r3, r1, asl #24
.L3149:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #76]
	str	r2, [sp, #72]
	str	r3, [sp, #68]
	bl	sblk_read_page
	ldr	lr, [r4, #36]
	sub	r3, lr, #512
	cmp	lr, #512
	cmnne	lr, #1
	clz	r3, r3
	mov	r3, r3, lsr #5
	ldr	r2, [sp, #72]
	str	r3, [sp, #56]
	ldr	ip, [sp, #76]
	ldr	r3, [sp, #68]
	bne	.L3146
	ldr	r0, [r4, #4]
	ldr	r1, [r4, #12]
	str	lr, [sp]
	ldr	lr, [r0]
	str	ip, [sp, #68]
	str	lr, [sp, #4]
	ldr	lr, [r0, #4]
	str	lr, [sp, #8]
	ldr	lr, [r0, #8]
	str	lr, [sp, #12]
	ldr	r0, [r0, #12]
	str	r0, [sp, #16]
	ldr	r0, [r1]
	str	r0, [sp, #20]
	ldr	r0, [r1, #4]
	str	r0, [sp, #24]
	ldr	r0, [r1, #8]
	str	r0, [sp, #28]
	ldr	r1, [r1, #12]
	ldr	r0, .L3182+20
	str	r1, [sp, #32]
	mov	r1, r2
	mov	r2, r8
	bl	printk
	ldr	r3, [sp, #56]
	ldr	ip, [sp, #68]
	cmp	r3, #0
	bne	.L3167
.L3146:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3154
.L3180:
	add	ip, ip, #1
	b	.L3155
.L3179:
	add	r5, r5, #1
	b	.L3144
.L3167:
.L3152:
	str	r7, [sp]
	mov	r3, r9
	ldr	r7, .L3182
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L3182+24
	bl	printk
	mov	r1, #0
	ldrb	r2, [r7, #2772]	@ zero_extendqisi2
	mov	r5, r8
	ldr	r0, [r4, #4]
	ldr	r10, .L3182+28
	mov	r2, r2, asl #9
	ldr	r9, .L3182+32
	bl	ftl_memset
	ldrb	r2, [r7, #2772]	@ zero_extendqisi2
	ldr	r0, [r4, #12]
	mov	r1, #0
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3156:
	ldrh	r3, [r10]
	cmp	r3, r5
	bls	.L3162
	ldr	r8, .L3182+4
	mov	r7, #0
	mov	r3, r5, asl #1
	str	r3, [sp, #44]
.L3163:
	ldrb	r2, [sp, #89]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bls	.L3181
	ldr	r2, [r9]
	tst	r2, #4096
	beq	.L3157
	mov	r2, r3
	ldr	r0, .L3182+36
	mov	r1, r5
	str	r3, [sp, #48]
	bl	printk
	ldr	r3, [sp, #48]
.L3157:
	ldrb	ip, [r8, #-3116]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L3158
	add	r2, sp, #112
	mov	r0, r4
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3182+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	orr	r3, r3, r5
	str	r3, [r4, #24]
	bl	sblk_3d_tlc_dump_prog
	b	.L3159
.L3158:
	ldrb	r2, [r8, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	add	r2, sp, #112
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3182+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3160
	ldr	r2, [sp, #44]
	mov	r0, r4
	orr	r3, r3, r2
	orr	r3, r3, #33554432
	str	r3, [r4, #24]
	bl	sblk_mlc_dump_prog
	b	.L3159
.L3160:
	ldrb	r2, [fp, #1189]	@ zero_extendqisi2
	orr	r3, r3, r5
	mvn	r0, #0
	ldrb	lr, [fp, #2772]	@ zero_extendqisi2
	rsb	r1, r2, #24
	str	r3, [r4, #24]
	mvn	r2, r0, asl r2
	and	r0, r2, r3, lsr r1
	str	lr, [sp]
	bic	r1, r3, r2, asl r1
	str	ip, [sp, #4]
	uxtb	r0, r0
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	bl	flash_prog_page_en
.L3159:
	add	r7, r7, #1
	b	.L3163
.L3181:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L3156
.L3162:
	mov	r0, r4
	bl	zbuf_free
	ldr	r0, .L3182+40
	mov	r1, r6
	bl	printk
.L3140:
	add	sp, sp, #116
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3183:
	.align	2
.L3182:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC195
	.word	.LANCHOR3-3066
	.word	.LC196
	.word	.LC197
	.word	.LANCHOR3-3096
	.word	.LANCHOR2
	.word	.LC198
	.word	.LC199
	.fnend
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	gc_ink_check_sblk
	.type	gc_ink_check_sblk, %function
gc_ink_check_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r5, #2106
	ldr	r4, .L3213
	ldr	r7, .L3213+4
	ldrh	r3, [r4, r5]
	ldr	r6, [r7, #908]
	cmp	r3, #3
	ldrls	pc, [pc, r3, asl #2]
	b	.L3185
.L3187:
	.word	.L3186
	.word	.L3188
	.word	.L3189
	.word	.L3190
.L3186:
	ldr	r8, .L3213+8
	movw	r3, #2792
	ldrh	r3, [r8, r3]
	cmp	r3, #7
	bls	.L3184
	ldrb	r3, [r8, #2774]	@ zero_extendqisi2
	cmp	r3, #2
	bls	.L3184
	ldr	r0, .L3213+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r9, #65535
	cmp	r0, r9
	beq	.L3184
	ldr	r3, [r8, #1080]
	mov	r0, r0, asl #2
	ldrh	r3, [r3, r0]
	ubfx	r3, r3, #0, #11
	cmp	r3, #2
	bgt	.L3184
	mov	r0, #1
	add	r6, r8, #4096
	bl	buf_alloc
	cmp	r0, #0
	str	r0, [r7, #908]
	beq	.L3184
	ldr	r0, .L3213+12
	mov	r1, #0
	ldr	r2, .L3213+16
	bl	_list_pop_index_node
	uxth	r5, r0
	cmp	r5, r9
	bne	.L3194
	ldr	r0, [r6, #908]
	bl	zbuf_free
	mov	r3, #0
	str	r3, [r6, #908]
	b	.L3184
.L3194:
	ldr	r1, .L3213+20
	mov	r0, r5
	bl	ftl_get_blk_list_in_sblk
	movw	r3, #2142
	strh	r5, [r4, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3213+24
	mov	r5, r5, asl #2
	strh	r2, [r3]	@ movhi
	movw	r3, #2106
	mov	r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r8, #1080]
	ldrb	r2, [r8, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #9
	strb	r0, [r6, #883]
	ldrh	r3, [r3, r5]
	tst	r3, #1
	ldr	r3, [r6, #908]
	movne	r1, #85
	moveq	r1, #170
	ldr	r0, [r3, #4]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	ftl_memset
.L3188:
	movw	r3, #2142
	mov	r1, #0
	ldrh	r0, [r4, r3]
	bl	ftl_erase_sblk
	movw	r3, #2106
	mov	r2, #2
	strh	r2, [r4, r3]	@ movhi
	b	.L3184
.L3189:
	bl	sblk_wait_write_queue_completed
	ldr	r9, .L3213+8
	ldr	r10, .L3213+28
	mov	r8, #0
	ldr	fp, .L3213+32
.L3196:
	ldrb	r3, [r7, #883]	@ zero_extendqisi2
	uxth	r5, r8
	cmp	r3, r5
	bls	.L3211
	add	r5, r4, r5, asl #1
	movw	r3, #65535
	add	r5, r5, #2144
	ldrh	r2, [r5, #14]
	cmp	r2, r3
	beq	.L3197
	ldr	r3, .L3213+24
	mov	ip, #0
	ldrh	r1, [r10]
	ldrh	r3, [r3]
	mla	r3, r1, r2, r3
	ldr	r2, [r6, #4]
	str	r3, [r2]
	ldr	r2, [r6, #4]
	str	fp, [r2, #4]
	ldr	r2, [r6, #12]
	str	ip, [r2]
	mvn	r2, #0
	ldrb	r0, [r9, #1189]	@ zero_extendqisi2
	ldrb	lr, [r9, #2772]	@ zero_extendqisi2
	rsb	r1, r0, #24
	str	ip, [sp, #4]
	mvn	r2, r2, asl r0
	and	r0, r2, r3, lsr r1
	str	lr, [sp]
	bic	r1, r3, r2, asl r1
	uxtb	r0, r0
	ldr	r2, [r6, #4]
	ldr	r3, [r6, #12]
	bl	flash_prog_page_en
.L3197:
	add	r8, r8, #1
	b	.L3196
.L3211:
	ldr	r1, .L3213
	ldr	r0, .L3213+36
	add	r2, r1, #2144
	ldrh	r3, [r2]
	ldrh	r0, [r0, #-8]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2]	@ movhi
	cmp	r0, r3
	movls	r3, #0
	strlsh	r3, [r2]	@ movhi
	movwls	r3, #2106
	movls	r2, #3
	strlsh	r2, [r1, r3]	@ movhi
	b	.L3184
.L3190:
	bl	sblk_wait_write_queue_completed
	ldr	r9, .L3213+28
	ldr	r10, .L3213+24
	mov	fp, #0
.L3199:
	ldrb	r3, [r7, #883]	@ zero_extendqisi2
	uxth	r5, fp
	ldr	r8, .L3213+4
	cmp	r3, r5
	bls	.L3212
	add	r5, r4, r5, asl #1
	movw	r2, #65535
	add	r5, r5, #2144
	add	r5, r5, #14
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L3201
	ldrh	r2, [r10]
	mov	r0, r6
	ldrh	r8, [r9]
	mov	r1, #1
	mla	r8, r8, r3, r2
	str	r8, [r6, #24]
	bl	sblk_read_page
	ldr	r3, [r6, #4]
	ldr	r3, [r3]
	cmp	r3, r8
	beq	.L3201
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L3201:
	add	fp, fp, #1
	b	.L3199
.L3212:
	add	r2, r8, #876
	ldr	r5, .L3213
	ldrh	r3, [r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2]	@ movhi
	ldr	r2, .L3213+36
	ldrh	r2, [r2, #-8]
	cmp	r2, r3
	bhi	.L3184
	movw	r3, #2106
	ldr	r0, [r8, #908]
	mov	r6, #0
	strh	r6, [r5, r3]	@ movhi
	bl	zbuf_free
	movw	r2, #2108
	ldrh	r3, [r5, r2]
	str	r6, [r8, #908]
	cmp	r3, #15
	bhi	.L3204
	add	r1, r3, #1
	add	r3, r3, #1040
	strh	r1, [r5, r2]	@ movhi
	add	r3, r3, #12
	movw	r2, #2142
	ldrh	r2, [r5, r2]
	add	r3, r5, r3, asl #1
	strh	r2, [r3, #6]	@ movhi
	b	.L3205
.L3204:
	movw	r3, #2142
	ldrh	r0, [r5, r3]
	bl	zftl_insert_free_list
.L3205:
	movw	r3, #2142
	ldr	r0, .L3213+40
	ldrh	r1, [r4, r3]
	movw	r3, #2108
	ldrh	r2, [r4, r3]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L3185:
	mov	r3, #0
	strh	r3, [r4, r5]	@ movhi
.L3184:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3214:
	.align	2
.L3213:
	.word	.LANCHOR0+2828
	.word	.LANCHOR0+4096
	.word	.LANCHOR0
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+4986
	.word	.LANCHOR0+4972
	.word	.LANCHOR3-3066
	.word	1437269760
	.word	.LANCHOR3-3088
	.word	.LC200
	.fnend
	.size	gc_ink_check_sblk, .-gc_ink_check_sblk
	.align	2
	.global	ftl_ink_check_sblk
	.type	ftl_ink_check_sblk, %function
ftl_ink_check_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L3238
	mov	r3, r0, asl #2
	str	r3, [sp, #8]
	mov	r5, r0
	ldr	r2, [sp, #8]
	mov	r1, r5
	ldr	r3, [r6, #1080]
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, r0, asl #2]
	ubfx	r2, r2, #0, #11
	ldr	r0, .L3238+4
	ubfx	r3, r3, #11, #8
	bl	printk
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3215
	movw	r3, #1076
	ldrh	r3, [r6, r3]
	cmp	r3, r5
	bls	.L3215
	mov	r1, #0
	mov	r0, r5
	bl	ftl_erase_sblk
	mov	r0, r5
	mov	r8, #0
	add	r1, sp, #32
	strh	r5, [sp, #16]	@ movhi
	bl	ftl_get_blk_list_in_sblk
	strb	r0, [sp, #25]
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r6, #1080]
	ldr	r2, [sp, #8]
	mov	r4, r0
	ldr	r0, [r0, #4]
	ldrh	r3, [r3, r2]
	ldrb	r2, [r6, #2772]	@ zero_extendqisi2
	ubfx	r3, r3, #0, #11
	cmp	r3, #1
	mov	r2, r2, asl #9
	movle	r1, #85
	movgt	r1, #170
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
.L3219:
	ldr	r3, .L3238+8
	uxth	r10, r8
	ldr	fp, .L3238+8
	ldrh	r3, [r3]
	cmp	r3, r10
	bls	.L3234
	add	fp, fp, #30
	mov	r9, #0
.L3222:
	ldrb	r2, [sp, #25]	@ zero_extendqisi2
	uxth	r3, r9
	cmp	r2, r3
	bls	.L3235
	add	r2, sp, #48
	add	r3, r2, r3, asl #1
	movw	r2, #65535
	ldrh	r3, [r3, #-16]
	cmp	r3, r2
	beq	.L3220
	ldrh	r7, [fp]
	mvn	r0, #0
	ldr	r2, .L3238+12
	mla	r7, r7, r3, r10
	ldr	r3, [r4, #4]
	str	r7, [r3]
	ldr	r3, [r4, #4]
	str	r2, [r3, #4]
	mov	r2, #0
	ldr	r3, [r4, #12]
	str	r2, [r3]
	ldrb	r3, [r6, #1189]	@ zero_extendqisi2
	ldrb	ip, [r6, #2772]	@ zero_extendqisi2
	rsb	r1, r3, #24
	str	r2, [sp, #4]
	mvn	r3, r0, asl r3
	and	r0, r3, r7, lsr r1
	str	ip, [sp]
	bic	r1, r7, r3, asl r1
	uxtb	r0, r0
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	bl	flash_prog_page_en
.L3220:
	add	r9, r9, #1
	b	.L3222
.L3235:
	add	r8, r8, #1
	b	.L3219
.L3234:
	mov	r10, #0
.L3223:
	ldrh	r1, [fp]
	uxth	r3, r10
	str	r3, [sp, #12]
	cmp	r1, r3
	bls	.L3236
	mov	r9, #0
.L3228:
	ldrb	r1, [sp, #25]	@ zero_extendqisi2
	uxth	r7, r9
	cmp	r1, r7
	bls	.L3237
	add	r7, r7, #8
	add	r3, sp, #48
	add	r7, r3, r7, asl #1
	movw	r3, #65535
	ldrh	r1, [r7, #-32]
	cmp	r1, r3
	beq	.L3225
	ldr	r3, .L3238+16
	mov	r0, r4
	ldrh	r8, [r3]
	ldr	r3, [sp, #12]
	mla	r8, r8, r1, r3
	mov	r1, #1
	str	r8, [r4, #24]
	bl	sblk_read_page
	ldr	r1, [r4, #4]
	ldr	r1, [r1]
	cmp	r1, r8
	beq	.L3225
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r1, #0
	strh	r1, [r7, #-32]	@ movhi
.L3225:
	add	r9, r9, #1
	b	.L3228
.L3237:
	add	r10, r10, #1
	b	.L3223
.L3236:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, [r6, #1080]
	ldr	r0, .L3238+20
	mov	r1, r5
	ldr	r2, [sp, #8]
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, r5, asl #2]
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	bl	printk
.L3215:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3239:
	.align	2
.L3238:
	.word	.LANCHOR0
	.word	.LC201
	.word	.LANCHOR3-3096
	.word	1437269760
	.word	.LANCHOR3-3066
	.word	.LC202
	.fnend
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	uxth	r5, r0
	cmp	r0, #5
	mov	r9, r0
	mov	r1, r5
	mov	r0, #0
	movne	r7, #0
	moveq	r7, #2
	bl	zftl_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	beq	.L3242
	ldr	r6, .L3256
	mov	r8, r0, asl #2
	ldr	r5, [r6, #1080]
	add	r5, r5, r8
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L3243
	ldr	r1, .L3256+4
	mov	r2, #1012
	ldr	r0, .L3256+8
	bl	printk
	bl	dump_stack
.L3243:
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	bfi	r3, r9, #5, #3
	uxtb	r3, r3
	ubfx	r2, r3, #3, #2
	orr	r2, r7, r2
	bfi	r3, r2, #3, #2
	strb	r3, [r5, #2]
	uxtb	r3, r3
	and	r2, r3, #24
	cmp	r7, #0
	cmpeq	r2, #24
	moveq	r2, #1
	bfieq	r3, r2, #3, #2
	streqb	r3, [r5, #2]
	ldr	r3, [r6, #2804]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	beq	.L3247
	ldr	r3, .L3256
	mov	r0, r4
	ldr	r3, [r3, #1080]
	ldrh	r3, [r3, r8]
	ubfx	r3, r3, #0, #11
	orrs	r3, r7, r3
	bne	.L3248
	bl	ftl_ink_check_sblk
	b	.L3247
.L3242:
	bl	print_ftl_debug_info
	mov	r1, r4
	mov	r2, r9
	ldr	r0, .L3256+12
	bl	printk
	mov	r1, r5
	mov	r0, #0
	bl	zftl_get_free_sblk
	mov	r2, r9
	mov	r4, r0
	ldr	r0, .L3256+12
	mov	r1, r4
	bl	printk
	bl	dump_all_list_info
	ldr	r1, .L3256+4
	movw	r2, #1031
	ldr	r0, .L3256+8
	bl	printk
	bl	dump_stack
.L3247:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3248:
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3257:
	.align	2
.L3256:
	.word	.LANCHOR0
	.word	.LANCHOR1+2404
	.word	.LC0
	.word	.LC203
	.fnend
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L3266
	mov	r5, r1
	ldr	r8, .L3266+4
	mov	r7, r6
.L3259:
	movw	r10, #65535
.L3260:
	mov	r0, r5
	bl	ftl_alloc_sblk
	cmp	r0, r10
	mov	r9, r0
	beq	.L3260
	mov	r1, #0
	ldr	fp, .L3266+8
	bl	ftl_erase_sblk
	add	r1, r4, #16
	mov	r0, r9
	mov	r10, r9, asl #1
	bl	ftl_get_blk_list_in_sblk
	strh	r9, [r4]	@ movhi
	ldrh	r2, [r8]
	cmp	r5, #2
	strb	r5, [r4, #4]
	uxtb	r3, r0
	strb	r3, [r4, #9]
	mov	r0, #0
	strh	r0, [r4, #2]	@ movhi
	smulbb	r3, r3, r2
	strb	r0, [r4, #5]
	strh	r0, [r4, #10]	@ movhi
	ldrneb	r0, [r6, #-3123]	@ zero_extendqisi2
	ldr	r1, [r7, #-2548]
	strh	r3, [r4, #6]	@ movhi
	ldrb	r3, [r7, #-3123]	@ zero_extendqisi2
	smulbbne	r0, r0, r2
	mul	r2, r3, r2
	uxthne	r0, r0
	strh	r0, [r4, #12]	@ movhi
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [fp, #1088]
	ldrh	r2, [r4, #6]
	strh	r2, [r3, r10]	@ movhi
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	ldr	r0, .L3266+12
	mov	r1, r9
	bl	printk
	ldr	r3, [fp, #1088]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	mov	r3, #7
	strb	r3, [r4, #4]
	b	.L3259
.L3267:
	.align	2
.L3266:
	.word	.LANCHOR3
	.word	.LANCHOR3-3096
	.word	.LANCHOR0
	.word	.LC204
	.fnend
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	pm_alloc_new_blk
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	movw	r2, #690
	ldr	r5, .L3286
	ldr	r7, .L3286+4
	ldr	r4, [r5, #2804]
	ldrh	r3, [r4, r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	ldrb	r2, [r7, #-3123]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L3269
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3270
.L3269:
	ldr	r4, .L3286
	movw	r8, #65535
.L3271:
	mov	r0, #1
	bl	ftl_alloc_sblk
	cmp	r0, r8
	mov	r6, r0
	beq	.L3271
	mov	r1, #0
	bl	ftl_erase_sblk
	ldr	r1, [r5, #2804]
	mov	r0, r6
	add	r1, r1, #672
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L3272
	mov	r1, r6
	ldr	r0, .L3286+8
	bl	printk
	ldr	r3, [r4, #1080]
	add	r6, r3, r6, asl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	b	.L3271
.L3272:
	ldr	r3, .L3286
	movw	r2, #690
	mov	r4, #0
	movw	r1, #65535
	ldr	r3, [r3, #2804]
	strh	r4, [r3, r2]	@ movhi
	add	r3, r3, #412
	add	r3, r3, #2
	mov	r2, #1
	str	r2, [r7, #520]
.L3274:
	ldrh	r0, [r3, #2]!
	uxth	r2, r4
	cmp	r0, r1
	beq	.L3273
	add	r4, r4, #1
	cmp	r4, #128
	bne	.L3274
	mov	r2, #264
	ldr	r1, .L3286+12
	ldr	r0, .L3286+16
	bl	printk
	bl	dump_stack
	mov	r2, r4
.L3273:
	add	r2, r2, #208
	ldr	r3, [r5, #2804]
	mov	r2, r2, asl #1
	strh	r6, [r3, r2]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L3270:
	ldr	r2, [r5, #2804]
	movw	r3, #690
	ldrh	r3, [r2, r3]
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r4, [r2, r3]
	movw	r2, #65533
	sub	r3, r4, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L3276
	ldr	r1, .L3286+12
	movw	r2, #270
	ldr	r0, .L3286+16
	bl	printk
	bl	dump_stack
.L3276:
	ldr	r1, [r5, #2804]
	mov	r2, #0
	add	r3, r1, #696
	strh	r2, [r3]	@ movhi
	ldr	r2, .L3286+20
	ldrb	r3, [r5, #1189]	@ zero_extendqisi2
	ldrh	r5, [r2, #-12]
	rsb	r3, r3, #24
	movw	r2, #694
	rsb	r5, r5, r3
	mov	r3, r4, asr r5
	strh	r3, [r1, r2]	@ movhi
	ldr	r2, .L3286+24
	add	r1, r1, #692
	strh	r4, [r1]	@ movhi
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L3280
	mvn	r2, #0
	uxth	r3, r3
	ldr	r0, .L3286+28
	mov	r1, r4
	str	r3, [sp]
	mvn	r2, r2, asl r5
	mov	r3, r4
	bl	printk
.L3280:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3287:
	.align	2
.L3286:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC204
	.word	.LANCHOR1+2420
	.word	.LC0
	.word	.LANCHOR3-3120
	.word	.LANCHOR2
	.word	.LC205
	.fnend
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r0
	ldr	r8, .L3302
	mov	r7, r1
	ldr	r9, .L3302+4
	mov	r4, r8
.L3289:
	ldr	r3, [r8, #2804]
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	add	r2, r3, #696
	ldrh	r1, [r2]
	ldrh	r2, [r9]
	cmp	r1, r2
	bcs	.L3290
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3291
.L3290:
	bl	pm_alloc_new_blk
	mov	r0, #0
	bl	ftl_info_flush
.L3291:
	ldr	r3, [r4, #2804]
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3292
	ldr	r1, .L3302+8
	movw	r2, #303
	ldr	r0, .L3302+12
	bl	printk
	bl	dump_stack
.L3292:
	ldr	r3, [r4, #2804]
	ldr	r5, .L3302+16
	add	r2, r3, #692
	add	r3, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3302+20
	ldr	r0, [r5, #524]
	ldrh	r10, [r2]
	ldrh	r2, [r3]
	mla	r10, r10, r1, r2
	mov	r2, #64
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r5, #524]
	mov	r0, r7
	str	r6, [r3]
	ldr	r3, [r4, #2804]
	ldr	fp, [r5, #524]
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	ldr	r3, [r3, #48]
	mov	r1, r1, asl #9
	str	r3, [fp, #4]
	bl	js_hash
	ldr	r3, [r4, #2804]
	mov	r1, r10
	mov	r2, r7
	str	r0, [fp, #8]
	ldrb	r0, [r3, #694]	@ zero_extendqisi2
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r5, #524]
	bl	ftl_prog_page
	ldr	r2, [r4, #2804]
	add	r1, r2, #696
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r1]	@ movhi
	cmp	r3, #1
	beq	.L3293
	ldrb	r3, [r5, #528]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3294
.L3293:
	mov	r3, #0
	strb	r3, [r5, #528]
	b	.L3289
.L3294:
	cmn	r0, #1
	bne	.L3296
	mov	r1, r10
	ldr	r0, .L3302+24
	bl	printk
	b	.L3289
.L3296:
	movw	r3, #698
	mov	r0, #0
	ldrh	r3, [r2, r3]
	cmp	r6, r3
	addcc	r6, r6, #176
	strcc	r10, [r2, r6, asl #2]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3303:
	.align	2
.L3302:
	.word	.LANCHOR0
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2440
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR3-3066
	.word	.LC206
	.fnend
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3318
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3305
	ldr	r0, .L3318+4
	movw	r1, #365
	ldr	r2, .L3318+8
	bl	printk
.L3305:
	ldr	r5, .L3318+12
	mov	r1, #0
	ldr	r6, .L3318+16
	mov	r2, #64
	add	r8, r5, #536
	mov	r9, #0
	ldr	r0, [r5, #532]
	mov	r7, r5
	bl	ftl_memset
	ldr	r0, .L3318+20
	ldr	r1, [r6, #1176]
	mov	r2, #4
	mov	r3, #16
	mov	r10, r8
	bl	rknand_print_hex
	ldr	r4, [r6, #1176]
	add	r0, r4, #16
	ldr	r1, [r4, #8]
	bl	js_hash
	str	r0, [r4, #12]
.L3306:
	ldrb	r3, [r5, #536]	@ zero_extendqisi2
	ldrh	r4, [r8, #2]
	ldrh	ip, [r6, #2]
	mov	r1, r3
	ldr	r0, .L3318+24
	mov	r2, r4
	str	r3, [sp, #8]
	str	ip, [sp, #12]
	bl	printk
	ldrh	r2, [r5, #-220]
	ldrh	r0, [r8, #2]
	sub	r2, r2, #1
	ldr	fp, .L3318+16
	cmp	r0, r2
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #12]
	blt	.L3307
	ldr	r4, [fp, #1176]
	ldrb	r2, [r7, #537]	@ zero_extendqisi2
	ldr	r3, [r4, #4]
	mov	r0, r4
	add	r3, r3, #1
	str	r3, [r4, #4]
	ldrb	r3, [r7, #536]	@ zero_extendqisi2
	strb	r2, [r7, #536]
	ldrh	r2, [r4, #16]
	strb	r3, [r7, #537]
	mov	r3, #0
	add	r2, r2, #1
	strh	r2, [r0, #16]!	@ movhi
	ldr	r1, [r4, #8]
	strh	r3, [r8, #2]	@ movhi
	str	r3, [sp, #8]
	bl	js_hash
	ldrb	r2, [r7, #536]	@ zero_extendqisi2
	str	r0, [r4, #12]
	ldr	r3, [sp, #8]
	ldrh	r4, [fp, #2]
	mov	r0, r3
	mul	r4, r4, r2
	b	.L3316
.L3307:
	cmp	r0, #0
	mla	r4, ip, r3, r4
	bne	.L3308
.L3316:
	mov	r1, r4
	bl	flash_erase_block
.L3308:
	ldr	r2, [r6, #1176]
	mov	fp, #1
	ldr	r3, [r5, #532]
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r2, #4]
	str	r2, [r3]
	ldr	r2, .L3318+28
	ldr	r3, [r5, #532]
	str	r2, [r3, #4]
	mov	r2, #4
	stmia	sp, {r2, fp}
	ldr	r2, [r6, #1176]
	bl	flash_prog_page_en
	ldrh	r3, [r10, #2]
	add	r3, r3, fp
	strh	r3, [r10, #2]	@ movhi
	cmn	r0, #1
	bne	.L3309
	mov	r1, r4
	ldr	r0, .L3318+32
	bl	printk
	b	.L3306
.L3309:
	cmp	r9, #0
	moveq	r9, fp
	beq	.L3306
.L3317:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3319:
	.align	2
.L3318:
	.word	.LANCHOR2
	.word	.LC138
	.word	.LANCHOR1+2456
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC207
	.word	.LC208
	.word	1398362953
	.word	.LC209
	.fnend
	.size	flash_info_flush, .-flash_info_flush
	.align	2
	.global	flash_info_blk_init
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3353
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	ldr	r6, .L3353+4
	tst	r3, #4096
	beq	.L3321
	ldr	r3, [r6, #1176]
	mov	r1, #50
	ldr	r0, .L3353+8
	ldr	r2, .L3353+12
	str	r3, [sp]
	mov	r3, #2048
	bl	printk
.L3321:
	ldr	ip, .L3353+4
	mov	r7, #4
	ldr	fp, .L3353+16
.L3322:
	mov	r5, #0
.L3325:
	ldrh	r1, [r6, #2]
	mov	r8, #4
	ldr	r4, .L3353+20
	mov	r0, #0
	str	r8, [sp]
	mla	r1, r1, r7, r5
	ldr	r2, [r6, #1176]
	ldr	r3, [r4, #532]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	ldr	r9, .L3353+4
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3323
	ldr	r2, [ip, #1176]
	ldr	r10, .L3353+16
	ldr	r3, [r2]
	cmp	r3, fp
	beq	.L3324
.L3323:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L3325
	add	r7, r7, #1
	cmp	r7, #16
	bne	.L3322
	b	.L3334
.L3324:
	ldrb	r1, [r2, #37]	@ zero_extendqisi2
	mov	r0, #0
	ldrb	r3, [r2, #36]	@ zero_extendqisi2
	strb	r1, [r4, #537]
	ldrh	r1, [r9, #2]
	strb	r3, [r4, #536]
	str	r8, [sp]
	mul	r1, r1, r3
	ldr	r3, [r4, #532]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3335
	ldr	r3, [r9, #1176]
	ldr	r2, [r3]
	cmp	r2, fp
	ldreq	r5, [r3, #4]
	bne	.L3335
.L3327:
	ldrb	r3, [r4, #537]	@ zero_extendqisi2
	mov	r2, #4
	ldrh	r1, [r6, #2]
	mov	r0, #0
	str	r2, [sp]
	ldr	r2, [r6, #1176]
	mul	r1, r1, r3
	ldr	r3, [r4, #532]
	bl	flash_read_page_en
	ldr	r7, .L3353+20
	cmn	r0, #1
	bne	.L3352
.L3328:
	ldr	r2, [r6, #1176]
	mov	r8, #4
	ldr	r3, [r4, #532]
	mov	r0, #0
	ldrb	r1, [r4, #536]	@ zero_extendqisi2
	mov	r7, #0
	str	r8, [sp]
	bl	flash_get_last_written_page
	ldr	r3, .L3353+24
	ldrh	r9, [r6, #2]
	ldr	ip, .L3353+4
	uxth	fp, r0
	add	r2, fp, #1
	strh	r2, [r3, #2]	@ movhi
	ldrb	r3, [r4, #536]	@ zero_extendqisi2
	mla	r9, r9, r3, r0
.L3329:
	rsb	r5, r7, fp
	sxth	r5, r5
	cmp	r5, #0
	bge	.L3332
	cmn	r5, #1
	bne	.L3331
	ldr	r3, [r6, #1176]
	ldr	r0, .L3353+28
	ldr	r1, [r3]
	bl	printk
.L3334:
	mvn	r0, #0
	b	.L3350
.L3335:
	mov	r5, #0
	b	.L3327
.L3352:
	ldr	r3, .L3353+4
	ldr	r3, [r3, #1176]
	ldr	r2, [r3]
	cmp	r2, r10
	bne	.L3328
	ldr	r2, [r3, #4]
	cmp	r5, r2
	ldrccb	r2, [r3, #37]	@ zero_extendqisi2
	ldrccb	r3, [r3, #36]	@ zero_extendqisi2
	strccb	r2, [r7, #536]
	strccb	r3, [r7, #537]
	b	.L3328
.L3332:
	str	r8, [sp]
	mov	r0, #0
	rsb	r1, r7, r9
	ldr	r2, [r6, #1176]
	ldr	r3, [r4, #532]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3330
	ldr	r3, [ip, #1176]
	ldr	r3, [r3]
	cmp	r3, r10
	beq	.L3331
.L3330:
	add	r7, r7, #1
	b	.L3329
.L3331:
	cmp	r7, #1
	bls	.L3336
	bl	flash_info_flush
.L3336:
	mov	r0, #0
.L3350:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3354:
	.align	2
.L3353:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC210
	.word	.LANCHOR1+2476
	.word	1398362953
	.word	.LANCHOR3
	.word	.LANCHOR3+536
	.word	.LC211
	.fnend
	.size	flash_info_blk_init, .-flash_info_blk_init
	.align	2
	.global	nand_flash_init
	.type	nand_flash_init, %function
nand_flash_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L3451
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3356
	ldr	r0, .L3451+4
	movw	r1, #3450
	ldr	r2, .L3451+8
	bl	printk
.L3356:
	ldr	r5, .L3451+12
	mov	r0, r4
	mov	r7, #0
	ldr	r4, .L3451+16
	str	r7, [r5, #-112]
	bl	nandc_init
	ldr	r3, .L3451+20
	mov	r1, r7
	mov	r2, #8
	ldr	r0, .L3451+24
	add	r10, r4, #1200
	mov	r9, r4
	str	r3, [r4, #1096]
	mov	r3, #1
	strb	r3, [r4, #1101]
	mov	r3, #3
	strb	r3, [r4, #1189]
	bl	ftl_memset
	sub	r0, r5, #216
	mov	r1, r7
	mov	r2, #32
	mov	fp, r10
	bl	ftl_memset
.L3362:
	mov	r8, r7, asl #3
	uxtb	r0, r7
	add	r1, r10, r8
	bl	flash_read_id
	cmp	r7, #0
	ldr	r1, .L3451+28
	bne	.L3357
	ldrb	r3, [r9, #1200]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bls	.L3358
.L3360:
	mvn	r7, #1
	b	.L3440
.L3358:
	ldrb	r3, [r9, #1201]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3360
.L3357:
	ldrb	r3, [fp, r8]	@ zero_extendqisi2
	add	r7, r7, #1
	cmp	r3, #181
	ldreq	r3, .L3451+28
	moveq	r2, #44
	streqb	r2, [r8, r3]
	cmp	r7, #4
	bne	.L3362
	ldr	ip, .L3451+32
	mov	r3, #0
.L3400:
	mov	r0, r3, asl #5
	ldrb	r8, [ip, r3, asl #5]	@ zero_extendqisi2
	add	r7, r0, #1
	ldr	lr, .L3451+32
	add	r7, ip, r7
	mov	r2, #0
.L3363:
	cmp	r2, r8
	bcs	.L3450
	ldrb	r10, [r7, r2]	@ zero_extendqisi2
	ldrb	r9, [r1, r2]	@ zero_extendqisi2
	cmp	r10, r9
	bne	.L3364
	add	r2, r2, #1
	b	.L3363
.L3450:
	adds	r8, lr, r0
	beq	.L3360
	ldr	r3, .L3451
	ldr	r2, .L3451+36
	add	r0, r3, r0
	mov	r3, #0
	ldrb	r1, [r0, #446]	@ zero_extendqisi2
.L3368:
	ldrb	r0, [r2, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r1
	beq	.L3367
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L3368
.L3367:
	ldr	r1, .L3451+36
	mov	r2, #32
	ldr	r7, .L3451
	add	r1, r1, r3, asl #5
	ldr	r0, .L3451+40
	bl	ftl_memcpy
	add	r0, r7, #4
	mov	r1, r8
	mov	r2, #32
	bl	ftl_memcpy
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #8
	bhi	.L3369
	ldrb	r2, [r7, #24]	@ zero_extendqisi2
	cmp	r2, #60
	movhi	r2, #60
	strhib	r2, [r7, #24]
	cmp	r3, #6
	beq	.L3360
.L3369:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3371
	ldr	r0, .L3451+4
	movw	r1, #3480
	ldr	r2, .L3451+8
	bl	printk
.L3371:
	ldr	r3, [r4, #1096]
	mov	r0, #16384
	mov	r7, #0
	ldrh	r3, [r3, #10]
	cmp	r3, #1024
	ldrcs	r3, .L3451+16
	movcs	r2, #2
	strcsb	r2, [r3, #1189]
	bl	ftl_malloc
	str	r0, [r5, #-104]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-128]
	mov	r0, #2048
	bl	ftl_malloc
	str	r0, [r4, #1176]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-100]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-120]
	mov	r0, #64
	bl	ftl_malloc
	strb	r7, [r5, #540]
	str	r0, [r5, #532]
	bl	flash_die_info_init
	ldrb	r0, [r6, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrb	r0, [r6, #24]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r1, [r4, #1096]
	str	r7, [r5, #-108]
	ldrh	r3, [r1, #16]
	ubfx	r2, r3, #8, #3
	strb	r2, [r4, #1232]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #-88]
	ubfx	r2, r3, #4, #1
	strb	r2, [r4, #1233]
	ubfx	r2, r3, #12, #1
	strb	r2, [r5, #-3116]
	ubfx	r2, r3, #13, #1
	strb	r2, [r5, #-3122]
	ubfx	r2, r3, #11, #1
	strb	r2, [r4, #1196]
	ldrb	r2, [r1, #31]	@ zero_extendqisi2
	ldrb	r1, [r1, #28]	@ zero_extendqisi2
	ubfx	r0, r2, #1, #1
	strb	r0, [r5, #-2536]
	ubfx	r0, r2, #2, #1
	strb	r0, [r5, #541]
	ubfx	r0, r3, #14, #1
	mov	r3, r3, lsr #15
	strb	r3, [r5, #-3121]
	ubfx	r3, r2, #3, #1
	strb	r3, [r4, #1188]
	mov	r3, #60
	strb	r3, [r4, #1100]
	ubfx	r2, r2, #4, #1
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	strb	r2, [r4, #1197]
	cmp	r3, #9
	strb	r1, [r4]
	strb	r1, [r4, #1102]
	ldreq	r2, .L3451+16
	ldrb	r1, [r6, #35]	@ zero_extendqisi2
	strb	r0, [r4, #1]
	moveq	r0, #70
	streqb	r0, [r2, #1100]
	tst	r1, #1
	ldr	r2, .L3451+16
	beq	.L3374
	ldr	r1, .L3451
	ldrb	r1, [r1, #33]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, #2
	moveq	r1, #3
	strb	r1, [r2, #1102]
.L3374:
	cmp	r3, #8
	bne	.L3376
	ldrb	r3, [r4, #1200]	@ zero_extendqisi2
	cmp	r3, #44
	cmpne	r3, #137
	bne	.L3376
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #3
	ldreq	r3, .L3451+16
	moveq	r2, #0
	streqb	r2, [r3, #1102]
.L3376:
	ldrh	r2, [r6, #20]
	ldrb	r3, [r6, #23]	@ zero_extendqisi2
	tst	r2, #64
	ldr	r0, .L3451+16
	strb	r3, [r4, #1173]
	beq	.L3378
	sub	r2, r3, #17
	sub	r1, r3, #21
	clz	r1, r1
	cmp	r3, #21
	cmpne	r2, #2
	mov	r1, r1, lsr #5
	bhi	.L3379
	ldr	r2, .L3451+44
	cmp	r1, #0
	ldr	r3, .L3451+12
	str	r2, [r5, #-108]
	moveq	r2, #15
	movne	r2, #4
	strb	r2, [r3, #-116]
	b	.L3378
.L3379:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r3, .L3451+48
	strls	r3, [r5, #-108]
	movls	r3, #4
	strlsb	r3, [r0, #1174]
	bls	.L3449
.L3382:
	sub	r1, r3, #67
	sub	r2, r3, #34
	cmp	r1, #1
	sub	ip, r3, #35
	sub	lr, r3, #68
	clz	ip, ip
	movhi	r1, #0
	movls	r1, #1
	cmp	r2, #1
	movhi	r2, r1
	orrls	r2, r1, #1
	clz	lr, lr
	cmp	r2, #0
	mov	ip, ip, lsr #5
	mov	lr, lr, lsr #5
	beq	.L3383
	ldr	r2, .L3451+48
	ldr	r3, .L3451+12
	str	r2, [r5, #-108]
	orrs	r2, lr, ip
	moveq	r2, #7
	movne	r2, #17
	cmp	r1, #0
	strb	r2, [r3, #-116]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #1174]
	b	.L3378
.L3383:
	sub	r1, r3, #36
	cmp	r1, #1
	bhi	.L3387
	ldr	r3, .L3451+52
	str	r3, [r5, #-108]
.L3449:
	mov	r3, #7
	b	.L3447
.L3387:
	cmp	r3, #50
	bne	.L3388
	ldr	r3, .L3451+56
	str	r3, [r5, #-108]
	mov	r3, #25
.L3447:
	strb	r3, [r5, #-116]
	b	.L3378
.L3388:
	cmp	r3, #81
	streqb	r2, [r0, #1136]
	ldreq	r3, .L3451+60
	streq	r3, [r5, #-108]
	moveq	r3, #7
	streqb	r3, [r5, #-116]
.L3378:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3389
	ldr	r0, .L3451+4
	movw	r1, #3573
	ldr	r2, .L3451+8
	bl	printk
.L3389:
	mov	r3, #0
	strb	r3, [r4, #1135]
	ldrb	r3, [r4, #1232]	@ zero_extendqisi2
	tst	r3, #1
	moveq	r0, #4
	beq	.L3448
	ldr	r3, .L3451+16
	ldrb	r3, [r3, #1200]	@ zero_extendqisi2
	cmp	r3, #155
	beq	.L3391
	mov	r0, #4
	bl	flash_set_interface_mode
	mov	r0, #4
	bl	nandc_set_if_mode
.L3391:
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
.L3448:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	r0, #1
	mov	r7, r0
	bne	.L3393
	ldr	r3, [r4, #1176]
	mov	r2, #17
	mov	r0, #0
	strb	r0, [r4, #1192]
	strb	r2, [r3, #32]
	bl	zftl_flash_exit_slc_mode
	b	.L3440
.L3393:
	ldrb	r3, [r6, #11]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L3394
	ldr	r3, [r4, #1176]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	cmp	r3, #1
	ldrne	r3, .L3451+16
	movne	r2, #0
	strneb	r2, [r3, #1102]
.L3394:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	ldr	r7, .L3451+16
	sub	r3, r3, #1
	cmp	r3, #7
	ldrls	r3, .L3451+64
	strls	r3, [r5, #-108]
	ldrb	r3, [r4, #1232]	@ zero_extendqisi2
	tst	r3, #4
	beq	.L3397
	ldr	r3, [r7, #1176]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3397
	ldrb	r3, [r5, #536]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r1, [r7, #2]
	mul	r1, r1, r3
	bl	flash_ddr_para_scan
	ldrb	r3, [r7, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3397
	ldr	r2, [r7, #1176]
	strb	r3, [r2, #19]
	bl	flash_info_flush
.L3397:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3399
	ldr	r0, .L3451+4
	movw	r1, #3676
	ldr	r2, .L3451+8
	bl	printk
.L3399:
	bl	nand_flash_print_info
	mov	r7, #0
	b	.L3440
.L3364:
	add	r3, r3, #1
	cmp	r3, #49
	bne	.L3400
	b	.L3360
.L3440:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3452:
	.align	2
.L3451:
	.word	.LANCHOR2
	.word	.LC138
	.word	.LANCHOR1+2496
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2+4
	.word	.LANCHOR0+1180
	.word	.LANCHOR0+1200
	.word	.LANCHOR2+424
	.word	.LANCHOR2+1992
	.word	.LANCHOR0+1103
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
	.type	ftl_sysblk_dump, %function
ftl_sysblk_dump:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r0
	.pad #52
	sub	sp, sp, #52
	mov	r0, #1
	bl	buf_alloc
	ldr	r10, .L3465
	mov	r6, #0
	mov	r5, r6
	ldr	ip, [r0, #4]
	mov	r4, r0
.L3454:
	ldrh	r3, [r10]
	uxth	r8, r6
	cmp	r3, r8
	bls	.L3464
	ldr	r3, .L3465+4
	add	r6, r6, #1
	ldr	r1, [r4, #4]
	ldr	r2, [r4, #12]
	ldrh	r7, [r3]
	ldr	r3, .L3465+8
	str	ip, [sp, #44]
	mla	r7, r7, fp, r8
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	mov	r0, r7
	bl	ftl_read_ppa_page
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	str	r0, [sp]
	mov	r9, r0
	ldr	r1, [r2]
	ldr	r0, .L3465+12
	str	r1, [sp, #4]
	ldr	r1, [r2, #4]
	str	r1, [sp, #8]
	ldr	r1, [r2, #8]
	str	r1, [sp, #12]
	mov	r1, fp
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	ldr	r2, [r3]
	str	r2, [sp, #20]
	ldr	r2, [r3, #4]
	str	r2, [sp, #24]
	ldr	r2, [r3, #8]
	str	r2, [sp, #28]
	mov	r2, r8
	ldr	r3, [r3, #12]
	str	r3, [sp, #32]
	mov	r3, r7
	bl	printk
	cmn	r9, #1
	cmpne	r9, #512
	moveq	r5, #1
	ldr	ip, [sp, #44]
	b	.L3454
.L3464:
	ldr	r0, .L3465+16
	add	r1, ip, #704
	mov	r2, #4
	mov	r3, #32
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	cmp	r5, #0
	beq	.L3457
	ldr	r1, .L3465+20
	movw	r2, #1619
	ldr	r0, .L3465+24
	bl	printk
	bl	dump_stack
.L3457:
	mov	r0, r5
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3466:
	.align	2
.L3465:
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3066
	.word	.LANCHOR0
	.word	.LC196
	.word	.LC212
	.word	.LANCHOR1+2512
	.word	.LC0
	.fnend
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 216
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #228
	sub	sp, sp, #228
	ldr	r5, .L3606
	mov	r4, r0
	str	r1, [sp, #28]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3468
	ldr	r0, .L3606+4
	ldrh	r1, [r4, #2]
	bl	printk
.L3468:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3469
	ldr	r0, .L3606+8
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	bl	printk
.L3469:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3470
	ldr	r0, .L3606+12
	ldrh	r1, [r4]
	bl	printk
.L3470:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3471
	ldr	r0, .L3606+16
	ldrh	r1, [r4, #16]
	ldrh	r2, [r4, #18]
	bl	printk
.L3471:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3472
	ldr	r0, .L3606+20
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	bl	printk
.L3472:
	ldrh	r3, [r4, #10]
	ldr	r6, .L3606+24
	ldrh	r2, [r4]
	strh	r3, [r4, #14]	@ movhi
	movw	r3, #1076
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bcs	.L3467
	mov	r0, #1
	bl	buf_alloc
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	mov	r1, #255
	sub	r3, r3, #2
	ldr	r2, [r0, #4]
	mov	r5, r0
	add	r0, sp, #32
	add	r3, r2, r3, asl #9
	mov	r2, #64
	str	r3, [sp, #16]
	bl	ftl_memset
	mov	r1, #255
	mov	r2, #64
	add	r0, sp, #96
	bl	ftl_memset
	mov	r1, #255
	mov	r2, #64
	add	r0, sp, #160
	bl	ftl_memset
	ldrb	r6, [r4, #5]	@ zero_extendqisi2
	ldrh	r7, [r4, #2]
	mov	r3, #2
	str	r3, [sp, #12]
	mov	r3, #0
	str	r3, [sp, #24]
.L3474:
	ldr	r3, .L3606+28
	ldrh	r3, [r3]
	cmp	r3, r7
	bls	.L3477
	ldrb	r6, [r4, #5]	@ zero_extendqisi2
	ldr	r8, .L3606+24
.L3475:
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L3602
	add	r3, r4, r6, asl #1
	ldrh	r10, [r3, #16]
	movw	r3, #65535
	cmp	r10, r3
	beq	.L3476
	ldr	r3, .L3606+32
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrh	r3, [r3]
	mla	r3, r3, r10, r7
	str	r3, [sp, #20]
	mov	r0, r3
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmp	r0, #512
	mov	r9, r0
	beq	.L3477
	cmn	r0, #1
	beq	.L3478
	ldr	r3, [r5, #12]
	ldr	r2, [r3]
	cmn	r2, #1
	bne	.L3478
	ldr	r3, [r3, #4]
	cmn	r3, #1
	bne	.L3478
	ldr	r3, [r5, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3477
.L3478:
	ldr	r3, .L3606+36
	mov	r2, #1
	strb	r2, [r3, #209]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r2, [r4, #10]
	mla	r3, r3, r7, r6
	cmp	r2, r3
	beq	.L3479
	ldr	r1, .L3606+40
	movw	r2, #1694
	ldr	r0, .L3606+44
	bl	printk
	bl	dump_stack
.L3479:
	ldrh	r3, [r4, #6]
	ldrh	r2, [r4, #10]
	add	r2, r3, r2
	ldr	r3, .L3606+28
	ldrh	r1, [r3]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L3480
	ldr	r1, .L3606+40
	movw	r2, #1695
	ldr	r0, .L3606+44
	bl	printk
	bl	dump_stack
.L3480:
	ldr	r3, .L3606+24
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L3481
	ldr	r3, [r5, #12]
	ldr	r1, .L3606+48
	ldr	r2, [r3]
	cmp	r2, r1
	beq	.L3481
	cmn	r9, #1
	beq	.L3486
	ldr	r2, [r3, #4]
	cmn	r2, #1
	beq	.L3519
	ldr	r2, [r3, #16]
	ldr	r1, .L3606+52
	cmp	r2, r1
	bne	.L3519
	ldr	r0, [sp, #16]
	mov	r1, #1024
	ldr	fp, [r3, #20]
	bl	js_hash
	cmp	fp, r0
	beq	.L3519
	mov	r1, #1024
	ldr	r0, [sp, #16]
	bl	js_hash
	mov	r1, r10
	mov	r2, r7
	str	r9, [sp]
	str	r0, [sp, #4]
	ldr	r3, [sp, #20]
	ldr	r0, .L3606+56
	bl	printk
	ldr	r0, .L3606+60
	mov	r2, #4
	mov	r3, #16
	ldr	r1, [sp, #16]
	bl	rknand_print_hex
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r0, .L3606+64
	mov	r2, #4
	ldr	r1, [r5, #12]
	mov	r3, r3, lsr #1
	bl	rknand_print_hex
	b	.L3486
.L3481:
	cmn	r9, #1
	beq	.L3486
.L3519:
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #4]
	cmn	r0, #1
	beq	.L3486
	ldr	r3, [r3]
	ldr	r2, .L3606+48
	cmp	r3, r2
	beq	.L3486
	bl	lpa_hash_get_ppa
	ldr	r2, [sp, #28]
	mov	r3, r0
	cmp	r2, #0
	beq	.L3485
	ldr	r2, [r5, #12]
	ldr	r1, [r2, #8]
	str	r2, [sp, #24]
	cmp	r0, r1
	cmnne	r0, #1
	beq	.L3485
	ldr	r2, .L3606+24
	mov	r9, #1
	ldr	fp, .L3606+68
	ldr	r10, .L3606+36
	ldrb	r1, [r2, #1189]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r1, r1, #24
	str	r3, [sp, #12]
	rsb	r1, r0, r1
	mov	r1, r9, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r10, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	uxth	r0, r0
	ldr	r2, [sp, #24]
	ldrh	r1, [r3]
	ldr	r3, [sp, #12]
	cmp	r1, r0
	bne	.L3485
	ldr	ip, [r2]
	mov	r0, r3
	ldr	r1, [r5, #4]
	sub	r2, r10, #56
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	str	ip, [sp, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r10, #-56]
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bcc	.L3485
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #8]
	cmn	r0, #1
	beq	.L3486
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	ldrh	r2, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r10, #-3130]	@ zero_extendqisi2
	rsb	r3, r2, r3
	mov	r9, r9, asl r3
	sub	r9, r9, #1
	and	r0, r9, r0, lsr r2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L3486
.L3485:
	ldr	r2, [r5, #12]
	ldr	r3, [r8, #2784]
	ldr	r1, [r2, #4]
	cmp	r1, r3
	bcs	.L3486
	ldr	r3, .L3606+28
	ldrb	r0, [r4, #9]	@ zero_extendqisi2
	ldrh	r1, [r4, #10]
	ldrh	r3, [r3]
	mul	r3, r3, r0
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3486
	ldr	r3, [r8, #2804]
	ldr	r2, [r2]
	ldr	r1, [r3, #8]
	cmp	r2, r1
	strhi	r2, [r3, #8]
	ldrh	r3, [r4, #12]
	ldrh	r2, [r4, #10]
	ldr	r1, [r5, #12]
	add	r2, r3, r2
	ldmib	r1, {r0, r1}
	uxth	r2, r2
	bl	lpa_hash_update_ppa
	ldr	r3, [sp, #36]
	str	r3, [sp, #32]
	ldr	r3, [sp, #40]
	str	r3, [sp, #36]
	ldr	r3, [sp, #44]
	str	r3, [sp, #40]
	ldr	r3, [sp, #20]
	str	r3, [sp, #44]
.L3486:
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
.L3476:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3475
.L3602:
	add	r7, r7, #1
	mov	r3, #0
	strb	r3, [r4, #5]
	uxth	r7, r7
	b	.L3474
.L3477:
	ldrh	r3, [r4, #6]
	strb	r6, [r4, #5]
	ldrh	r6, [r4, #10]
	ldrb	fp, [r4, #9]	@ zero_extendqisi2
	add	r6, r3, r6
	ldr	r3, .L3606+72
	strh	r7, [r4, #2]	@ movhi
	ldrh	ip, [r3, #-8]
	str	r3, [sp, #16]
	mul	fp, ip, fp
	cmp	r6, fp
	beq	.L3490
	ldr	r1, .L3606+40
	movw	r2, #1802
	ldr	r0, .L3606+44
	bl	printk
	bl	dump_stack
.L3490:
	ldr	r9, .L3606+36
	mov	r8, #0
	ldrh	r6, [r4, #10]
	mov	r7, r8
	ldr	r10, [r9, #-2548]
.L3491:
	cmp	r7, r6
	bcs	.L3603
	ldrh	r3, [r4, #12]
	add	r3, r7, r3
	add	r7, r7, #1
	ldr	r3, [r10, r3, asl #2]
	cmn	r3, #1
	addne	r8, r8, #1
	uxthne	r8, r8
	b	.L3491
.L3603:
	ldr	r2, [sp, #16]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r10, [r2, #-8]
	smulbb	r3, r3, r10
	ldr	r10, .L3606+24
	rsb	r6, r6, r3
	add	r6, r8, r6
	ldr	r8, .L3606
	uxth	r6, r6
	ldr	r3, [r8]
	tst	r3, #4096
	beq	.L3494
	ldrh	r1, [r4]
	mov	r2, r6
	ldr	ip, [r10, #1088]
	ldr	r0, .L3606+76
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L3494:
	ldrh	r3, [r4]
	ldr	r2, [r10, #1088]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [r8]
	tst	r3, #16384
	beq	.L3495
	ldr	r3, [sp, #44]
	add	r1, sp, #32
	ldr	r0, .L3606+80
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	printk
.L3495:
	ldrb	r2, [r10, #2772]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r5, #4]
	mov	r7, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L3496:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r7, r3
	bcs	.L3604
	add	ip, sp, #32
	ldr	r0, [ip, r7, asl #2]
	cmn	r0, #1
	bne	.L3497
.L3501:
	ldr	r2, [r10, #2804]
	mvn	r6, #0
	ldr	r3, [r5, #12]
	mov	r0, #2
	ldr	r2, [r2, #8]
	str	r2, [r3]
	mov	r2, #0
	ldr	r3, [r5, #12]
	str	r6, [r3, #4]
	ldr	r3, [r5, #12]
	str	r6, [r3, #8]
	ldr	r3, [r5, #12]
	str	r2, [r3, #12]
	ldr	r3, [r5, #12]
	str	r2, [r3, #16]
	ldr	r3, [r5, #4]
	str	r2, [r3]
	ldr	r1, [r5, #12]
	add	r1, r1, #16
	bl	ftl_debug_info_fill
	b	.L3498
.L3497:
	ldr	r3, .L3606+24
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	str	ip, [sp, #20]
	bl	ftl_read_ppa_page
	ldr	r3, [r5, #12]
	mov	r6, r0
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	ldr	ip, [sp, #20]
	beq	.L3499
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3606+84
	ldr	r2, [ip, r7, asl #2]
	ldr	r3, [r3, #4]
	bl	printk
.L3499:
	add	r3, sp, #32
	mov	r2, #1
	mov	r0, #2
	ldr	r3, [r3, r7, asl #2]
	cmp	fp, r3
	ldr	r3, [r5, #12]
	mvnne	r6, #0
	str	fp, [r3, #8]
	ldr	r3, [r5, #12]
	str	r2, [r3, #12]
	mov	r2, #0
	ldr	r3, [r5, #12]
	str	r2, [r3, #16]
	ldr	r1, [r5, #12]
	add	r1, r1, #16
	bl	ftl_debug_info_fill
	cmn	r6, #1
	beq	.L3501
.L3498:
	ldr	r3, [sp, #24]
	cmp	r3, #1
	bne	.L3503
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bls	.L3503
.L3573:
	mov	r0, r4
	bl	ftl_get_new_free_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3505
	ldrh	r1, [r4, #12]
	ldr	r2, [r5, #12]
	ldrh	r3, [r4, #10]
	ldr	r0, .L3606+88
	add	r3, r3, r1
	ldr	r2, [r2, #4]
	mov	r1, fp
	sub	r3, r3, #1
	bl	printk
.L3505:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	ldrh	r2, [r4, #6]
	mul	r3, r3, r1
	add	r3, r3, #1
	rsb	r3, r7, r3
	cmp	r2, r3
	bls	.L3503
	ldr	r3, .L3606+24
	mov	r0, fp
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	str	r0, [sp, #20]
	ldrh	r0, [r4]
	bl	ftl_vpn_decrement
	adds	r3, r6, #1
	movne	r3, #1
	ldr	r2, [sp, #20]
	cmn	r2, #1
	cmnne	r6, #1
	beq	.L3506
	add	r3, sp, #96
	ldrh	r2, [r4, #12]
	str	fp, [r3, r7, asl #2]
	ldrh	r3, [r4, #10]
	add	r3, r3, r2
	add	r2, sp, #160
	sub	r3, r3, #1
	str	r3, [r2, r7, asl #2]
	b	.L3503
.L3506:
	ldrh	r2, [r4, #6]
	cmp	r2, #1
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L3573
.L3503:
	add	r7, r7, #1
	b	.L3496
.L3604:
	ldr	r10, .L3606+24
	mov	r6, #0
.L3509:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r6, r3
	bcs	.L3605
	add	fp, sp, #96
	ldr	r3, [fp, r6, asl #2]
	cmn	r3, #1
	beq	.L3511
	add	r7, sp, #32
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldr	r0, [r7, r6, asl #2]
	ldrb	r3, [r10, #2772]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #256
	bne	.L3511
	ldr	r0, [fp, r6, asl #2]
	ldrb	r3, [r10, #2772]	@ zero_extendqisi2
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3513
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3606+88
	ldr	r2, [r3, #8]
	ldr	r3, [r7, r6, asl #2]
	bl	printk
.L3513:
	cmn	fp, #1
	beq	.L3511
	ldr	r3, [r5, #12]
	ldr	r2, [r7, r6, asl #2]
	ldr	r1, [r3, #8]
	cmp	r2, r1
	bne	.L3511
	ldr	r1, [r8]
	add	r7, sp, #160
	tst	r1, #16384
	beq	.L3514
	ldr	r1, [r3, #4]
	ldr	r0, .L3606+92
	ldr	r3, [r7, r6, asl #2]
	bl	printk
.L3514:
	ldr	r3, [r5, #12]
	ldr	r2, [r7, r6, asl #2]
	ldmib	r3, {r0, r1}
	uxth	r2, r2
	bl	lpa_hash_update_ppa
.L3511:
	add	r6, r6, #1
	b	.L3509
.L3605:
	mov	r0, r5
	bl	zbuf_free
	ldrh	r3, [r4, #12]
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #16]
	ldrh	r2, [r2, #-8]
	mla	r3, r1, r2, r3
	ldr	r2, .L3606+36
	ldr	r2, [r2, #-2548]
	sub	r3, r3, #-1073741823
	ldr	r3, [r2, r3, asl #2]
	cmn	r3, #1
	beq	.L3516
	ldr	r1, .L3606+40
	movw	r2, #1917
	ldr	r0, .L3606+44
	bl	printk
	bl	dump_stack
.L3516:
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L3467
	mov	r0, r4
	bl	ftl_write_last_log_page
.L3467:
	add	sp, sp, #228
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3607:
	.align	2
.L3606:
	.word	.LANCHOR2
	.word	.LC213
	.word	.LC214
	.word	.LC215
	.word	.LC216
	.word	.LC217
	.word	.LANCHOR0
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3066
	.word	.LANCHOR3
	.word	.LANCHOR1+2528
	.word	.LC0
	.word	-178307901
	.word	1212240712
	.word	.LC218
	.word	.LC219
	.word	.LC184
	.word	.LANCHOR3-3132
	.word	.LANCHOR3-3088
	.word	.LC220
	.word	.LC221
	.word	.LC222
	.word	.LC223
	.word	.LC224
	.fnend
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	movw	r7, #1076
	ldr	r4, .L3610
	ldr	r5, .L3610+4
	ldr	r0, .L3610+8
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	sub	r6, r5, #3088
	bl	printk
	ldrb	r1, [r5, #200]	@ zero_extendqisi2
	ldrb	r2, [r5, #201]	@ zero_extendqisi2
	ldrh	r3, [r5, #202]
	ldr	r0, .L3610+12
	bl	printk
	ldr	r3, [r4, #2804]
	ldr	r0, .L3610+16
	ldrh	r1, [r3, #130]
	ldrh	r2, [r3, #140]
	bl	printk
	ldr	r0, [r4, #1092]
	ldrh	ip, [r0, #22]
	ldrh	r1, [r0, #16]
	ldrh	r2, [r0, #18]
	ldrb	r3, [r0, #21]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #26]
	str	r0, [sp, #4]
	ldr	r0, .L3610+20
	bl	printk
	ldr	r0, [r4, #1092]
	ldrh	ip, [r0, #54]
	ldrh	r1, [r0, #48]
	ldrh	r2, [r0, #50]
	ldrb	r3, [r0, #53]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #58]
	str	r0, [sp, #4]
	ldr	r0, .L3610+24
	bl	printk
	ldr	r0, [r4, #1092]
	ldrh	ip, [r0, #86]
	ldrh	r1, [r0, #80]
	ldrh	r2, [r0, #82]
	ldrb	r3, [r0, #85]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #90]
	str	r0, [sp, #4]
	ldr	r0, .L3610+28
	bl	printk
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldr	r1, [r5, #-2548]
	ldr	r0, .L3610+32
	mul	r3, r3, r2
	mov	r2, #4
	mov	r3, r3, asl #1
	bl	rknand_print_hex
	ldr	r1, [r4, #1088]
	ldrh	r3, [r4, r7]
	mov	r2, #2
	ldr	r0, .L3610+36
	bl	rknand_print_hex
	ldr	r3, [r4, #2804]
	movw	ip, #698
	ldr	r0, .L3610+40
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #1080]
	ldrh	r3, [r4, r7]
	mov	r2, #4
	ldr	r0, .L3610+44
	bl	rknand_print_hex
	sub	r1, r5, #3056
	ldr	r0, .L3610+48
	sub	r1, r1, #4
	mov	r2, #2
	mov	r3, #256
	bl	rknand_print_hex
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldr	r0, .L3610+52
	ldr	r1, [r5, #-2544]
	mul	r3, r3, r2
	mov	r2, #2
	mov	r3, r3, asl #1
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, lr}
	b	rknand_print_hex
.L3611:
	.align	2
.L3610:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC225
	.word	.LC226
	.word	.LC227
	.word	.LC228
	.word	.LC229
	.word	.LC230
	.word	.LC231
	.word	.LC232
	.word	.LC212
	.word	.LC233
	.word	.LC234
	.word	.LC235
	.fnend
	.size	dump_ftl_info, .-dump_ftl_info
	.align	2
	.global	pm_ppa_update_check
	.type	pm_ppa_update_check, %function
pm_ppa_update_check:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r2
	ldr	r3, .L3616
	mov	r6, r0
	ldr	r7, .L3616+4
	mov	r5, r1
	sub	r2, r3, #3120
	mvn	r1, #0
	ldrh	r0, [r2, #-12]
	ldrb	r2, [r7, #1189]	@ zero_extendqisi2
	rsb	r2, r2, #24
	rsb	r2, r0, r2
	mov	r0, r4, lsr r0
	bic	r0, r0, r1, asl r2
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, [r7, #1080]
	uxth	r0, r0
	add	r0, r3, r0, asl #2
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	cmp	r0, #1
	cmpne	r0, #7
	moveq	r0, #1
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r1, r6
	mov	r2, r5
	mov	r3, r4
	ldr	r0, .L3616+8
	bl	printk
	bl	dump_ftl_info
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3617:
	.align	2
.L3616:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC236
	.fnend
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #31
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r6, r0
	mov	r5, r1
	bls	.L3619
	ldr	r1, .L3633
	mov	r2, #32
	ldr	r0, .L3633+4
	bl	printk
	bl	dump_stack
.L3619:
	ldr	r9, .L3633+8
	movw	r10, #698
	ldr	r7, .L3633+12
	ldr	r3, [r9, #2804]
	mov	r8, r9
	ldrh	r2, [r3, r10]
	cmp	r6, r2
	bls	.L3620
	mov	r1, r6
	ldr	r0, .L3633+16
	bl	printk
	mov	r4, #0
	movw	r3, #542
	ldr	r0, [r4, #4]
	ldrh	r2, [r7, r3]
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [r9, #2804]
	ldrh	r3, [r3, r10]
	cmp	r3, r6
	bcs	.L3631
	ldr	r0, .L3633+4
	mov	r2, #37
	ldr	r1, .L3633
	b	.L3632
.L3620:
	add	r2, r6, #176
	mov	r5, r5, asl #3
	add	r9, r7, r5
	ldr	r4, [r3, r2, asl #2]
	sub	r3, r7, #2528
	add	r2, r3, r5
	strh	r6, [r3, r5]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strh	r3, [r2, #2]	@ movhi
	bne	.L3623
	mov	r1, r6
	mov	r2, r4
	ldr	r0, .L3633+20
	bl	printk
	movw	r3, #542
	ldr	r0, [r9, #-2524]
	mov	r1, #255
	ldrh	r2, [r7, r3]
	bl	ftl_memset
	b	.L3631
.L3623:
	ldr	r2, [r7, #524]
	mov	r0, r4
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r1, [r9, #-2524]
	bl	ftl_read_ppa_page
	ldr	r2, [r7, #524]
	ldr	r2, [r2]
	cmp	r2, r6
	mov	r3, r0
	bne	.L3624
	cmn	r0, #1
	cmpne	r0, #512
	beq	.L3624
.L3628:
	ldr	r3, [r7, #524]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3631
	ldr	r0, .L3633+4
	mov	r2, #73
	ldr	r1, .L3633
	b	.L3632
.L3624:
	mov	r1, r6
	str	r4, [sp]
	ldr	r0, .L3633+24
	add	r5, r7, r5
	bl	printk
	ldr	r3, [r8, #2804]
	movw	ip, #698
	ldr	r0, .L3633+28
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r0, .L3633+32
	mov	r2, #4
	ldr	r1, [r5, #-2524]
	mov	r3, r3, asl #7
	ldr	r9, .L3633+12
	bl	rknand_print_hex
	ldr	r0, .L3633+36
	ldr	r1, [r7, #524]
	mov	r2, #4
	mov	r3, #16
	bl	rknand_print_hex
	mov	r0, r4
	ldr	r1, [r5, #-2524]
	ldr	r2, [r7, #524]
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #512
	bne	.L3627
	movw	r3, #542
	ldr	r0, [r5, #-2524]
	mov	r1, #255
	ldrh	r2, [r9, r3]
	bl	ftl_memset
	b	.L3629
.L3627:
	ldr	r3, [r9, #524]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3628
	b	.L3629
.L3632:
	bl	printk
	bl	dump_stack
.L3631:
	mov	r0, #0
	b	.L3622
.L3629:
	mvn	r0, #0
.L3622:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3634:
	.align	2
.L3633:
	.word	.LANCHOR1+2552
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC237
	.word	.LC238
	.word	.LC239
	.word	.LC240
	.word	.LC219
	.word	.LC241
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	pm_gc
	.type	pm_gc, %function
pm_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L3654
	ldr	r4, [r5, #2804]
	add	r4, r4, #688
	ldrh	r2, [r4]
	ldr	r4, .L3654+4
	ldrh	r3, [r4, #-152]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L3636
	ldr	r3, [r4, #-160]
	cmp	r3, #0
	beq	.L3637
.L3636:
	bl	pm_free_sblk
	ldr	r2, [r5, #2804]
	add	r3, r2, #688
	ldrh	r1, [r3]
	ldrh	r3, [r4, #-152]
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3638
	ldr	r3, .L3654+4
	ldr	r3, [r3, #-160]
	cmp	r3, #0
	beq	.L3637
.L3638:
	uxth	r0, r0
	movw	r6, #65535
	add	r0, r0, #208
	mov	r3, #0
	str	r3, [r4, #-160]
	mov	r0, r0, asl #1
	ldrh	r7, [r2, r0]
	cmp	r7, r6
	bne	.L3640
	ldr	r1, .L3654+8
	mov	r2, #182
	ldr	r0, .L3654+12
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	r3, [r5, #2804]
	uxth	r0, r0
	add	r0, r0, #208
	mov	r0, r0, asl #1
	ldrh	r7, [r3, r0]
	cmp	r7, r6
	beq	.L3637
.L3640:
	bl	pm_select_ram_region
	ldr	r8, .L3654+4
	movw	r2, #65535
	sub	r3, r8, #2528
	mov	r6, r0, asl #3
	mov	r10, r0
	add	r9, r3, r6
	str	r3, [sp]
	ldrh	r0, [r3, r6]
	cmp	r0, r2
	beq	.L3641
	add	r8, r8, r6
	ldr	r1, [r8, #-2524]
	cmp	r1, #0
	beq	.L3641
	ldrsh	r3, [r9, #2]
	cmp	r3, #0
	bge	.L3641
	bl	pm_write_page
	ldrh	r3, [r9, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r9, #2]	@ movhi
.L3641:
	ldr	fp, .L3654
	mov	r8, #0
	ldr	r9, .L3654+4
	add	r3, r9, r6
	str	r3, [sp, #4]
.L3642:
	ldr	r0, [r5, #2804]
	uxth	r9, r8
	add	r3, r0, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r3, r9
	bls	.L3653
	ldr	r3, .L3654+16
	ldrh	r1, [r3]
	add	r3, r9, #176
	ldr	ip, [r0, r3, asl #2]
	mvn	r0, #0
	ldrb	r3, [fp, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r1, r3
	mvn	r0, r0, asl r3
	and	r0, r0, ip, lsr r1
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, r7
	bne	.L3643
	mov	r0, r9
	mov	r1, r10
	bl	load_l2p_region
	cmp	r0, #0
	bne	.L3644
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r3, #-2524]
	bl	pm_write_page
.L3644:
	ldr	r2, [sp]
	mvn	r3, #0
	strh	r3, [r2, r6]	@ movhi
.L3643:
	add	r8, r8, #1
	b	.L3642
.L3653:
	bl	pm_free_sblk
.L3637:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3655:
	.align	2
.L3654:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2568
	.word	.LC0
	.word	.LANCHOR3-3132
	.fnend
	.size	pm_gc, .-pm_gc
	.align	2
	.global	pm_flush_id
	.type	pm_flush_id, %function
pm_flush_id:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, r0, asl #3
	ldr	r4, .L3662
	sub	r3, r4, #2528
	add	r2, r4, r0
	add	r5, r3, r0
	ldr	r1, [r2, #-2524]
	ldrh	r0, [r3, r0]
	bl	pm_write_page
	ldrh	r3, [r5, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #520]
	cmp	r3, #0
	beq	.L3657
	bl	pm_gc
	mov	r3, #0
	str	r3, [r4, #520]
.L3657:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3663:
	.align	2
.L3662:
	.word	.LANCHOR3
	.fnend
	.size	pm_flush_id, .-pm_flush_id
	.align	2
	.global	pm_flush
	.type	pm_flush, %function
pm_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, #0
	ldr	r5, .L3669
.L3666:
	add	r3, r5, r4, asl #3
	uxth	r0, r4
	ldrsh	r3, [r3, #2]
	cmp	r3, #0
	bge	.L3665
	bl	pm_flush_id
.L3665:
	add	r4, r4, #1
	cmp	r4, #32
	bne	.L3666
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3670:
	.align	2
.L3669:
	.word	.LANCHOR3-2528
	.fnend
	.size	pm_flush, .-pm_flush
	.align	2
	.global	flt_sys_flush
	.type	flt_sys_flush, %function
flt_sys_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, #0
	ldmfd	sp!, {r3, lr}
	b	ftl_info_flush
	.fnend
	.size	flt_sys_flush, .-flt_sys_flush
	.align	2
	.global	zftl_deinit
	.type	zftl_deinit, %function
zftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	zftl_flash_de_init
	bl	flt_sys_flush
	ldmfd	sp!, {r3, lr}
	b	zftl_flash_de_init
	.fnend
	.size	zftl_deinit, .-zftl_deinit
	.align	2
	.global	pm_init
	.type	pm_init, %function
pm_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r4, .L3699
	.pad #20
	sub	sp, sp, #20
	mov	r0, #64
	mov	r5, #0
	mov	r3, #1
	ldr	r10, .L3699+4
	str	r5, [r4, #-160]
	sub	r8, r4, #2528
	str	r5, [r4, #520]
	mvn	r9, #0
	strb	r3, [r4, #528]
	bl	ftl_malloc
	str	r0, [r4, #524]
.L3677:
	mov	r3, #0
	cmp	r7, r3
	mov	r6, r8
	strh	r9, [r6, r5]!	@ movhi
	strh	r3, [r6, #2]	@ movhi
	beq	.L3676
	ldrb	r0, [r10, #2772]	@ zero_extendqisi2
	mov	r0, r0, asl #9
	bl	ftl_malloc
	str	r0, [r6, #4]
.L3676:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L3677
	ldr	r5, .L3699+4
	ldr	r10, [r4, #-2524]
	ldr	r6, [r4, #524]
	ldr	r3, [r5, #2804]
	mov	r2, r10
	add	r3, r3, #692
	ldrb	r0, [r3, #2]	@ zero_extendqisi2
	ldrh	r1, [r3]
	mov	r3, #4
	str	r3, [sp]
	mov	r3, r6
	bl	flash_get_last_written_page
	ldr	r3, [r5, #2804]
	add	r2, r3, #696
	ldrh	r2, [r2]
	cmp	r2, r0
	mov	r7, r0
	bgt	.L3678
	add	r3, r3, #692
	ldr	r0, .L3699+8
	mov	fp, r5
	ldrh	r1, [r3]
	mov	r3, r7
	bl	printk
	ldr	r3, [r5, #2804]
	add	r7, r7, #1
	add	r3, r3, #696
	ldrh	r9, [r3]
.L3679:
	sxth	r3, r9
	cmp	r3, r7
	bge	.L3698
	ldr	r2, [r5, #2804]
	ldr	r0, .L3699+12
	add	r1, r2, #692
	ldrh	r8, [r0]
	ldrh	r1, [r1]
	ldrb	r0, [r2, #694]	@ zero_extendqisi2
	mov	r2, r10
	mla	r8, r8, r1, r3
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r6
	mov	r1, r8
	bl	flash_read_page_en
	ldr	r3, [r5, #2804]
	add	r3, r3, #692
	ldr	r2, [r3, #-644]
	add	r2, r2, #1
	str	r2, [r3, #-644]
	add	r3, r3, #2
	ldr	r1, [r6]
	mov	r2, r8
	ldrh	r3, [r3]
	str	r0, [sp, #12]
	ldr	r0, .L3699+16
	bl	printk
	ldr	ip, [sp, #12]
	cmp	ip, #512
	cmnne	ip, #1
	beq	.L3680
	ldr	r3, [fp, #2804]
	ldr	r2, [r6]
	add	r3, r3, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L3680
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L3681
	ldrb	r1, [fp, #2772]	@ zero_extendqisi2
	mov	r0, r10
	str	r3, [sp, #12]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3681
	ldr	r1, [r6, #8]
	ldr	r0, .L3699+20
	bl	printk
	b	.L3680
.L3681:
	ldr	r3, [r6]
	ldr	r2, [r5, #2804]
	add	r3, r3, #176
	str	r8, [r2, r3, asl #2]
.L3680:
	add	r9, r9, #1
	uxth	r9, r9
	b	.L3679
.L3698:
	mov	r3, #1
	strb	r3, [r4, #209]
	ldr	r3, [r5, #2804]
	add	r3, r3, #696
	strh	r7, [r3]	@ movhi
	bl	pm_free_sblk
.L3678:
	movw	r3, #542
	mov	r1, #255
	ldrh	r2, [r4, r3]
	ldr	r0, [r4, #-2524]
	bl	ftl_memset
	mvn	r0, #0
	ldr	r1, [r4, #-2524]
	bl	pm_write_page
	ldrb	r3, [r4, #209]	@ zero_extendqisi2
	ldr	r5, .L3699
	cmp	r3, #0
	beq	.L3683
	ldr	r1, [r5, #-2524]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r5, #-2524]
	mvn	r0, #0
	bl	pm_write_page
	mvn	r0, #0
	ldr	r1, [r5, #-2524]
	bl	pm_write_page
.L3683:
	bl	pm_free_sblk
	bl	pm_gc
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3700:
	.align	2
.L3699:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC242
	.word	.LANCHOR3-3066
	.word	.LC243
	.word	.LC244
	.fnend
	.size	pm_init, .-pm_init
	.align	2
	.global	pm_log2phys
	.type	pm_log2phys, %function
pm_log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r1
	ldr	r9, .L3720
	mov	r10, r0
	mov	r8, r2
	ldrb	r4, [r9, #2772]	@ zero_extendqisi2
	mov	r1, r4, asl #7
	mov	r5, r4, asl #7
	bl	__aeabi_uidiv
	ldr	r2, [r9, #2784]
	cmp	r10, r2
	movcc	r3, #0
	ldrcc	r2, .L3720+4
	uxth	fp, r0
	mov	r7, r0
	smulbb	r5, r5, fp
	rsb	r5, r5, r10
	uxth	r5, r5
	bcc	.L3702
	mov	r1, r10
	ldr	r0, .L3720+8
	bl	printk
	mvn	r0, #0
	cmp	r8, #0
	streq	r0, [r6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3718:
	ldrh	r1, [r1, r2]
	cmp	r1, fp
	beq	.L3705
.L3704:
	add	r3, r3, #1
	cmp	r3, #32
	beq	.L3717
.L3702:
	mov	r1, r3, asl #3
	uxth	r4, r3
	add	r0, r2, r1
	ldr	r10, .L3720+4
	ldr	r0, [r0, #4]
	cmp	r0, #0
	bne	.L3718
	b	.L3704
.L3717:
	bl	pm_select_ram_region
	mov	r3, r0, asl #3
	mov	r4, r0
	add	r2, r10, r3
	ldrh	r1, [r10, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L3719
.L3708:
	ldr	r3, .L3720+12
	mov	r0, fp
	mov	r1, r4
	strb	r4, [r3, #544]
	bl	load_l2p_region
.L3705:
	cmp	r8, #0
	ldr	r1, .L3720+12
	mov	r4, r4, asl #3
	bne	.L3706
	add	r3, r1, r4
	mvn	r2, #0
	ldr	r3, [r3, #-2524]
	ldr	r3, [r3, r5, asl #2]
	str	r3, [r6]
	ldrb	r0, [r9, #1189]	@ zero_extendqisi2
	rsb	ip, r0, #24
	mvn	r2, r2, asl r0
	and	r3, r2, r3, lsr ip
	ldrb	r2, [r1, #-3064]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6]
	b	.L3707
.L3706:
	add	r3, r1, r4
	ldr	r2, [r6]
	ldr	r3, [r3, #-2524]
	str	r2, [r3, r5, asl #2]
	add	r2, r10, r4
	strb	r7, [r1, #-2272]
	ldrh	r3, [r2, #2]
	mvn	r3, r3, asl #17
	mvn	r3, r3, lsr #17
	strh	r3, [r2, #2]	@ movhi
.L3707:
	add	r4, r10, r4
	movw	r2, #32767
	mov	r0, #0
	ldrh	r3, [r4, #2]
	ubfx	r1, r3, #0, #15
	cmp	r1, r2
	addne	r3, r3, #1
	strneh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3719:
	ldrsh	r3, [r2, #2]
	cmp	r3, #0
	bge	.L3708
	bl	pm_flush_id
	b	.L3708
.L3721:
	.align	2
.L3720:
	.word	.LANCHOR0
	.word	.LANCHOR3-2528
	.word	.LC245
	.word	.LANCHOR3
	.fnend
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #65535
	ldr	r7, .L3822
	mov	r6, #0
	ldr	r8, .L3822+4
	.pad #68
	sub	sp, sp, #68
	ldr	r5, [r7, #1092]
	strb	r6, [r8, #-3115]
	strb	r6, [r8, #-182]
	ldrh	r4, [r5, #80]
	cmp	r4, r3
	beq	.L3723
	mvn	r3, #0
	mov	r0, #1
	strh	r3, [r5, #130]	@ movhi
	bl	buf_alloc
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	add	r3, r5, r3, asl #1
	ldrh	r1, [r3, #94]
	sub	r3, r8, #3056
	ldrh	r3, [r3, #-10]
	mul	r1, r3, r1
	ldrb	r3, [r8, #-3124]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r4, r0
	str	r1, [sp, #56]
	beq	.L3724
	ldrb	r2, [r7, #1196]	@ zero_extendqisi2
	cmp	r2, r6
	bne	.L3724
	ldrb	r2, [r7, #1197]	@ zero_extendqisi2
	cmp	r2, r6
	beq	.L3725
.L3724:
	ldr	r2, .L3822+8
	ldrh	r2, [r2, #-10]
	sub	r2, r2, #1
	add	r2, r2, r1
	orr	r3, r2, r3, asl #24
	b	.L3814
.L3725:
	cmp	r3, #3
	bne	.L3726
	sub	r3, r8, #3088
	ldrh	r3, [r3, #-8]
	sub	r3, r3, #1
	add	r3, r3, r1
	orr	r3, r3, #50331648
.L3814:
	str	r3, [r4, #24]
.L3726:
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3727
	ldr	r3, [r4, #12]
	ldr	r6, .L3822+12
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3728
.L3727:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L3822+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r1, [r2, #8]
	str	r1, [sp, #20]
	ldr	r2, [r2, #12]
	str	r2, [sp, #24]
	ldr	r1, [r4, #24]
	ldr	r2, [r4, #36]
	ldr	r3, [r3]
	bl	printk
	b	.L3818
.L3728:
	ldrb	r3, [r8, #545]	@ zero_extendqisi2
	ldr	r10, .L3822+4
	cmp	r3, #2
	sub	r9, r10, #3104
	bne	.L3730
	ldrb	r3, [r10, #-3124]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L3730
	ldrh	r2, [r9, #-10]
	movw	r3, #542
	ldrb	ip, [r10, #-3123]	@ zero_extendqisi2
	ldrh	r3, [r10, r3]
	mov	r2, r2, asl #2
	ldr	r1, [r10, #-172]
	smulbb	r2, ip, r2
	mov	r0, r3, lsr #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #4]
	rsb	r2, r3, r2
	uxth	r2, r2
	bl	ftl_memcpy
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3731
	ldr	r3, .L3822
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3732
.L3731:
	ldr	r3, [r4, #24]
	sub	r3, r3, #1
	b	.L3733
.L3732:
	sub	r10, r10, #3088
	ldr	r3, [sp, #56]
	ldrh	r2, [r10, #-8]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, #33554432
.L3733:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3734
	ldr	r3, [r4, #12]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3735
.L3734:
	mov	r0, r4
	bl	zbuf_free
.L3818:
	ldrh	r4, [r5, #80]
	mov	r6, #0
	ldr	r3, [r7, #1088]
	mov	r4, r4, asl #1
	strh	r6, [r3, r4]	@ movhi
	ldrh	r2, [r5, #80]
	ldr	r3, [r7, #1092]
	strh	r2, [r3, #130]	@ movhi
	b	.L3729
.L3735:
	movw	r3, #542
	ldr	r0, [r8, #-172]
	ldr	r1, [r4, #4]
	ldrh	r2, [r8, r3]
	b	.L3815
.L3730:
	ldrh	r2, [r9, #-10]
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	ldr	r0, [r8, #-172]
	ldr	r1, [r4, #4]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
.L3815:
	bl	ftl_memcpy
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	ldrh	r2, [r9, #-10]
	ldr	r10, .L3822+4
	mul	r2, r3, r2
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	sub	r6, r10, #3104
	cmp	r3, r2, asr #6
	mov	r2, r2, asl #2
	bge	.L3737
	ldr	r0, [r10, #-168]
	mov	r1, #0
	bl	ftl_memset
	ldrb	r9, [r10, #545]	@ zero_extendqisi2
	cmp	r9, #1
	movne	r9, #1
	movne	r6, #0
	bne	.L3738
	ldrh	r1, [r6, #-10]
	movw	r2, #542
	ldrb	r3, [r10, #-3123]	@ zero_extendqisi2
	ldrh	r6, [r10, r2]
	ldr	r0, [r10, #-168]
	mul	r3, r3, r1
	ldr	r1, [r4, #4]
	sub	r6, r6, r3, asl #2
	add	r1, r1, r3, asl #2
	mov	r2, r6
	bl	ftl_memcpy
	b	.L3738
.L3737:
	ldr	r1, [r4, #4]
	mov	r9, #0
	ldr	r0, [r10, #-168]
	mov	r6, r9
	add	r1, r1, r2
	bl	ftl_memcpy
.L3738:
	ldr	r3, .L3822+4
	sub	r2, r3, #3088
	ldrh	r2, [r2, #-8]
	str	r2, [sp, #36]
	ldrb	r2, [r8, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	str	r2, [sp, #32]
	bne	.L3739
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3740
.L3739:
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3741
.L3740:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	mul	r3, r3, r2
	str	r3, [sp, #36]
	mov	r3, #1
	str	r3, [sp, #32]
.L3741:
	ldr	r10, .L3822+4
	mov	r7, #0
	str	r7, [sp, #40]
.L3742:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bcs	.L3755
	add	ip, r5, #96
	add	r3, r7, r7, asl #1
	sub	r3, r3, #1
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #44]
.L3756:
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	ldr	r2, [sp, #44]
	cmp	r2, r3
	bge	.L3752
	ldr	r3, [sp, #40]
	mov	fp, #1
	mov	r8, r3, asl #2
.L3753:
	ldr	r3, [sp, #32]
	cmp	fp, r3
	bhi	.L3820
	ldr	r3, .L3822+20
	ldrh	r2, [ip]
	ldrb	r1, [r10, #-3124]	@ zero_extendqisi2
	ldrh	r3, [r3]
	cmp	r1, #2
	mul	r3, r3, r2
	str	r3, [sp, #56]
	beq	.L3743
	ldr	r2, .L3822
	ldrb	r2, [r2, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3744
.L3743:
	sub	r2, r7, #1
	add	r3, r2, r3
	add	r3, r3, fp
	orr	r3, r3, r1, asl #24
	b	.L3816
.L3744:
	cmp	r1, #3
	addne	r3, r3, r7
	bne	.L3816
	ldr	r2, .L3822
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r7
	ldrne	r2, [sp, #48]
	orreq	r3, r3, fp, asl #24
	addne	r3, r3, r2
	addne	r3, r3, fp
	orrne	r3, r3, #50331648
.L3816:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #52]
	bl	sblk_read_page
	cmp	r9, #0
	ldr	ip, [sp, #52]
	beq	.L3748
	ldr	r3, [r10, #-168]
	ldr	r2, [r3, r8]
	cmp	r2, #0
	ldreq	r2, [r4, #12]
	ldreq	r2, [r2, #8]
	streq	r2, [r3, r8]
.L3748:
	ldr	r3, [r10, #-172]
	ldr	r2, [r4, #12]
	ldr	r3, [r3, r8]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L3749
	ldr	r0, [r10, #-168]
	ldr	lr, [r0, r8]
	ldr	r0, [r2, #8]
	cmp	lr, r0
	beq	.L3779
.L3749:
	ldr	r0, [r10, #-168]
	str	ip, [sp, #52]
	ldr	r0, [r0, r8]
	str	r0, [sp]
	ldr	r0, [r2]
	stmib	sp, {r0, r1}
	ldr	r1, [r2, #8]
	ldr	r0, .L3822+24
	str	r1, [sp, #12]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	ldr	r1, [r4, #24]
	ldr	r2, [r4, #36]
	bl	printk
	ldr	r3, [r10, #-172]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	ldr	ip, [sp, #52]
	beq	.L3779
	mov	r0, r4
	bl	zbuf_free
	ldr	r2, .L3822
	ldrh	r3, [r5, #80]
	mov	r0, #0
	ldr	r1, [r2, #1088]
	mov	r3, r3, asl #1
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r5, #80]
	ldr	r3, [r2, #1092]
	strh	r1, [r3, #130]	@ movhi
	b	.L3729
.L3779:
	ldr	r3, [sp, #40]
	add	fp, fp, #1
	add	r8, r8, #4
	add	r3, r3, #1
	str	r3, [sp, #40]
	b	.L3753
.L3820:
	ldr	r3, [sp, #44]
	add	ip, ip, #2
	add	r3, r3, #1
	str	r3, [sp, #44]
	b	.L3756
.L3752:
	ldrb	r3, [r10, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r7, r7, #1
	add	r7, r7, #1
	b	.L3742
.L3755:
	mov	r0, r4
	mov	r6, #0
	bl	zbuf_free
	ldr	ip, .L3822+8
	ldrb	r8, [r5, #89]	@ zero_extendqisi2
	mov	r7, r6
	ldr	r3, .L3822
	add	r10, ip, #3104
	ldrh	r9, [ip, #-10]
	ldrh	r4, [r5, #80]
	ldr	r3, [r3, #1088]
	smulbb	r8, r8, r9
	mov	r4, r4, asl #1
	mov	r9, r6
	strh	r8, [r3, r4]	@ movhi
.L3757:
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bcs	.L3771
	add	r2, r9, r9, asl #1
	sub	r3, r2, #1
	str	r3, [sp, #48]
	add	r3, r5, #96
	str	r3, [sp, #44]
	mov	r3, #0
	str	r3, [sp, #40]
.L3772:
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r3, r2
	bge	.L3768
	mov	r4, #1
.L3769:
	ldr	r3, [sp, #32]
	cmp	r4, r3
	bhi	.L3821
	ldr	r2, [r10, #-172]
	ldr	r8, [r2, r7, asl #2]
	cmn	r8, #1
	beq	.L3758
	ldr	r2, [r10, #-168]
	mov	r0, r8
	ldr	fp, [r2, r7, asl #2]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #60]
	bne	.L3759
	mov	r0, r8
	add	r1, sp, #60
	mov	r2, #0
	bl	pm_log2phys
.L3759:
	ldr	r3, [sp, #44]
	ldrh	r2, [r3]
	ldr	r3, .L3822+20
	ldrh	r1, [r3]
	ldr	r3, .L3822+4
	mul	r2, r1, r2
	ldrb	r1, [r3, #-3124]	@ zero_extendqisi2
	cmp	r1, #2
	beq	.L3760
	ldr	r0, .L3822
	ldrb	lr, [r0, #1196]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L3761
.L3760:
	sub	r0, r9, #1
	add	r2, r0, r2
	add	r2, r2, r4
	orr	r2, r2, r1, asl #24
	b	.L3817
.L3761:
	cmp	r1, #3
	addne	r2, r2, r9
	bne	.L3817
	ldrb	r1, [r0, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, r4, asl #24
	addeq	r2, r2, r9
	ldrne	r3, [sp, #48]
	addne	r2, r2, r3
	addne	r2, r2, r4
	orr	r2, r1, r2
.L3817:
	str	r2, [sp, #56]
	ldr	r2, .L3822+28
	ldrh	r0, [r2]
	ldr	r2, .L3822
	ldrb	r1, [r2, #1189]	@ zero_extendqisi2
	mov	r2, #1
	str	r2, [sp, #52]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r2, asl r1
	sub	r1, r1, #1
	and	r0, r1, fp, lsr r0
	ldrb	r1, [r10, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r1, [sp, #60]
	ldr	r2, [sp, #52]
	cmp	r1, fp
	uxth	fp, r0
	bne	.L3765
	mov	r0, r8
	add	r1, sp, #56
	bl	pm_log2phys
	mov	r0, fp
	add	r6, r6, #1
	bl	ftl_vpn_decrement
	b	.L3766
.L3765:
	ldr	r2, [sp, #56]
	cmp	r1, r2
	addeq	r6, r6, #1
.L3766:
	ldr	r1, .L3822
	add	r7, r7, #1
	ldr	r2, [r1, #1080]
	add	r2, r2, fp, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r2, r2, #224
	bne	.L3758
	ldr	r8, [r1, #1088]
	mov	fp, fp, asl #1
	ldrh	r1, [r8, fp]
	cmp	r1, #0
	strneh	r2, [r8, fp]	@ movhi
.L3758:
	add	r4, r4, #1
	b	.L3769
.L3821:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	ldr	r3, [sp, #44]
	add	r3, r3, #2
	str	r3, [sp, #44]
	b	.L3772
.L3768:
	ldrb	r2, [r10, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r9, r9, #1
	add	r9, r9, #1
	b	.L3757
.L3771:
	ldr	r2, .L3822
	ldrh	r3, [r5, #80]
	ldr	r2, [r2, #1088]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
.L3729:
	ldr	r4, .L3822
	ldrh	r0, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3773
	bl	ftl_dump_write_open_sblk
.L3773:
	mov	r2, r6
	ldrh	r1, [r5, #80]
	ldr	r0, .L3822+32
	bl	printk
	mvn	r3, #0
	strh	r3, [r5, #80]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #1092]
	movw	r2, #65535
	ldrh	r3, [r3, #130]
	cmp	r3, r2
	beq	.L3774
	ldr	r1, .L3822
	movw	r2, #1076
	ldrh	r2, [r1, r2]
	cmp	r2, r3
	bhi	.L3775
	ldr	r1, .L3822+36
	movw	r2, #517
	ldr	r0, .L3822+40
	bl	printk
	bl	dump_stack
.L3775:
	ldr	r3, [r4, #1092]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
.L3774:
	ldr	r2, [r4, #2804]
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, #126]	@ movhi
	ldr	r2, [r4, #1092]
	strh	r3, [r2, #130]	@ movhi
	bl	ftl_info_flush
	b	.L3722
.L3723:
	ldrh	r3, [r5, #130]
	cmp	r3, r4
	beq	.L3722
	ldr	r2, [r7, #2804]
	ldrh	r2, [r2, #126]
	cmp	r2, r3
	bne	.L3777
	bl	pm_flush
	ldr	r3, [r7, #1092]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
	ldr	r3, [r7, #2804]
	mvn	r2, #0
	mov	r0, r6
	strh	r2, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L3777:
	ldr	r3, [r7, #1092]
	mvn	r2, #0
	strh	r2, [r3, #130]	@ movhi
.L3722:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3823:
	.align	2
.L3822:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LC246
	.word	.LANCHOR3-3066
	.word	.LC247
	.word	.LANCHOR3-3132
	.word	.LC248
	.word	.LANCHOR1+2576
	.word	.LC0
	.fnend
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	gc_update_l2p_map_new
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L3860
	ldr	r3, .L3860+4
	ldr	r9, .L3860+8
	ldr	r5, [r4, #1092]
	ldrh	r3, [r3, #-10]
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	mul	r3, r2, r3
	str	r3, [sp]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3825
	ldr	r0, .L3860+12
	ldrh	r1, [r5, #80]
	bl	printk
.L3825:
	ldrh	r3, [r5, #80]
	mov	r8, #0
	ldr	r6, .L3860+16
	mov	r7, r8
	ldr	r1, [r4, #1088]
	mov	r3, r3, asl #1
	ldr	r0, [sp]
	ldrb	r2, [r6, #545]	@ zero_extendqisi2
	rsb	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
.L3826:
	ldr	r3, [sp]
	cmp	r7, r3
	beq	.L3859
	ldr	r3, [r6, #-172]
	mov	r10, r7, asl #2
	ldr	fp, [r3, r7, asl #2]
	cmn	fp, #1
	beq	.L3827
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #12]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3828
	ldr	r0, .L3860+20
	mov	r2, fp
	ldr	r1, [sp, #12]
	mov	r3, r7
	bl	printk
.L3828:
	str	r7, [sp, #8]
.L3834:
	ldr	r3, [r6, #-172]
	ldr	fp, [r3, r10]
	cmn	fp, #1
	beq	.L3829
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	uxth	r0, r0
	cmp	r0, r3
	bne	.L3829
	ldr	r3, [r6, #-168]
	mov	r0, fp
	ldr	r3, [r3, r10]
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3830
	mov	r0, fp
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3830:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L3831
	ldr	r3, .L3860+16
	add	r1, sp, #24
	mov	r0, fp
	mov	r2, #1
	add	r8, r8, #1
	ldr	r3, [r3, #-164]
	ldr	r3, [r3, r10]
	str	r3, [r1, #-8]!
	bl	pm_log2phys
	ldr	r3, .L3860+24
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	ldr	r2, [sp, #4]
	mov	fp, r2, lsr r0
	mvn	r0, #0
	bic	r0, fp, r0, asl r3
	ldr	r3, .L3860+16
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	b	.L3858
.L3831:
	ldr	r2, [r9]
	tst	r2, #256
	beq	.L3833
	ldr	r0, .L3860+28
	mov	r1, fp
	ldr	r2, [sp, #4]
	bl	printk
.L3833:
	ldrh	r0, [r5, #80]
.L3858:
	bl	ftl_vpn_decrement
	ldr	r3, [r6, #-172]
	mvn	r2, #0
	str	r2, [r3, r10]
.L3829:
	ldr	r3, [sp, #8]
	add	r10, r10, #4
	ldr	r2, [sp]
	add	r3, r3, #1
	str	r3, [sp, #8]
	cmp	r3, r2
	bne	.L3834
.L3827:
	add	r7, r7, #1
	b	.L3826
.L3859:
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3836
	ldrh	r3, [r5, #80]
	ldr	r1, [r4, #1092]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldr	r0, .L3860+32
	ldrh	r1, [r1, #80]
	ldrh	r2, [r2, r3]
	mov	r3, r8
	bl	printk
.L3836:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, r8
	beq	.L3837
	ldr	r1, .L3860+36
	movw	r2, #898
	ldr	r0, .L3860+40
	bl	printk
	bl	dump_stack
.L3837:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r8, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3861:
	.align	2
.L3860:
	.word	.LANCHOR0
	.word	.LANCHOR3-3104
	.word	.LANCHOR2
	.word	.LC249
	.word	.LANCHOR3
	.word	.LC250
	.word	.LANCHOR3-3132
	.word	.LC251
	.word	.LC252
	.word	.LANCHOR1+2588
	.word	.LC0
	.fnend
	.size	gc_update_l2p_map_new, .-gc_update_l2p_map_new
	.align	2
	.global	gc_scan_src_blk_one_page
	.type	gc_scan_src_blk_one_page, %function
gc_scan_src_blk_one_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r6, .L3891
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3891+4
	mov	lr, r1
	ldr	r5, .L3891+8
	movw	r0, #65535
	ldrb	r3, [r6, #2832]	@ zero_extendqisi2
	ldrb	ip, [r5, #-3123]	@ zero_extendqisi2
	add	r2, r4, r3, asl #1
	ldrh	r7, [r2, #36]
	ldrh	r2, [r4, #2]
.L3863:
	cmp	r7, r0
	bne	.L3889
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, ip
	addeq	r2, r2, #1
	moveq	r3, #0
	moveq	lr, #1
	add	r1, r4, r3, asl #1
	uxtheq	r2, r2
	ldrh	r7, [r1, #36]
	mov	r1, #1
	b	.L3863
.L3889:
	mov	r0, #1
	cmp	lr, #0
	strneh	r2, [r4, #2]	@ movhi
	cmp	r1, #0
	strneb	r3, [r6, #2832]
	bl	buf_alloc
	ldr	r8, .L3891
	mov	r9, #1
	ldr	r10, .L3891+12
	mov	fp, r0
.L3868:
	ldrb	r3, [r6, #2834]	@ zero_extendqisi2
	ldr	r2, .L3891
	cmp	r9, r3
	bgt	.L3890
	ldrh	r2, [r10]
	cmp	r3, #2
	mul	r2, r2, r7
	bne	.L3869
	ldrh	r3, [r4, #2]
	add	r3, r2, r3, asl #1
	ldr	r2, .L3891+8
	sub	r3, r3, #1
	add	r3, r3, r9
	ldrb	r2, [r2, #-3124]	@ zero_extendqisi2
	b	.L3887
.L3869:
	cmp	r3, #3
	ldrneh	r1, [r4, #2]
	addne	r2, r2, r1
	bne	.L3888
	ldrb	r3, [r8, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3872
	ldrb	r3, [r8, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3873
.L3872:
	ldrh	r3, [r4, #2]
	add	r3, r3, r3, asl #1
	sub	r3, r3, #1
	add	r3, r3, r2
	ldrb	r2, [r5, #-3124]	@ zero_extendqisi2
	add	r3, r3, r9
.L3887:
	orr	r3, r3, r2, asl #24
	str	r3, [fp, #24]
	b	.L3870
.L3873:
	ldrh	r1, [r4, #2]
	add	r2, r2, r1
	orr	r2, r2, r9, asl #24
.L3888:
	str	r2, [fp, #24]
.L3870:
	mov	r0, fp
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [fp, #36]
	cmp	r3, #512
	cmnne	r3, #1
	beq	.L3875
	ldr	r3, [fp, #12]
	ldr	r3, [r3, #4]
	mov	r0, r3
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #12]
	ldr	r3, [sp, #4]
	bne	.L3876
	ldr	r2, [r6, #2784]
	cmp	r3, r2
	bcs	.L3876
	mov	r0, r3
	add	r1, sp, #12
	mov	r2, #0
	bl	pm_log2phys
.L3876:
	ldr	r3, [fp, #24]
	ldr	r2, [sp, #12]
	cmp	r3, r2
	ldreqh	r1, [r4, #20]
	ldreq	r2, [r5, #-3128]
	streq	r3, [r2, r1, asl #2]
	ldreqh	r3, [r4, #20]
	addeq	r3, r3, #1
	streqh	r3, [r4, #20]	@ movhi
.L3875:
	ldrh	r3, [r4, #22]
	add	r9, r9, #1
	add	r3, r3, #1
	strh	r3, [r4, #22]	@ movhi
	b	.L3868
.L3890:
	mov	r0, fp
	str	r2, [sp, #4]
	bl	zbuf_free
	ldrb	r1, [r5, #-3123]	@ zero_extendqisi2
	ldr	r2, [sp, #4]
	ldrb	r3, [r2, #2832]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r2, #2832]
	cmp	r1, r3
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r2, #2832]
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3892:
	.align	2
.L3891:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR3-3066
	.fnend
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2828
	ldr	r4, .L3945
	.pad #28
	sub	sp, sp, #28
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3894
	ldr	r1, .L3945+4
	movw	r2, #1505
	ldr	r0, .L3945+8
	bl	printk
	bl	dump_stack
.L3894:
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	moveq	r0, #0
	beq	.L3895
	ldr	r3, .L3945+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3896
	ldr	r3, .L3945
	ldr	r0, .L3945+16
	ldr	r2, [r3, #1088]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	bl	printk
.L3896:
	ldr	r5, .L3945
	movw	r7, #2828
	bl	timer_get_time
	ldrh	r0, [r4, r7]
	add	r1, r5, #2864
	ldr	r6, .L3945+20
	bl	ftl_get_blk_list_in_sblk
	uxtb	r0, r0
	strb	r0, [r4, #2833]
	cmp	r0, #0
	mvneq	r3, #0
	streqh	r3, [r5, r7]	@ movhi
	beq	.L3895
	ldrh	r3, [r5, r7]
	ldr	r2, [r5, #1080]
	add	r2, r2, r3, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r1, r2, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r1, #224
	orreq	r2, r2, #1
	cmp	r2, #0
	bne	.L3898
	ldr	r2, [r5, #1092]
	ldrh	ip, [r2, #16]
	cmp	ip, r3
	beq	.L3898
	ldrh	ip, [r2, #48]
	cmp	ip, r3
	beq	.L3898
	ldrh	r2, [r2, #80]
	cmp	r2, r3
	bne	.L3899
.L3898:
	cmp	r1, #0
	bne	.L3900
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3901
	ldr	r1, .L3945+4
	movw	r2, #1530
	ldr	r0, .L3945+8
	bl	printk
	bl	dump_stack
.L3901:
	movw	r3, #2828
	ldr	r2, [r4, #1088]
	ldrh	r3, [r4, r3]
	mov	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L3900:
	movw	r3, #2828
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	strh	r0, [r6, #20]	@ movhi
	b	.L3895
.L3899:
	add	r0, r6, r0, asl #1
	movw	r3, #65535
	ldrh	r10, [r0, #34]
	cmp	r10, r3
	bne	.L3902
	ldr	r1, .L3945+4
	movw	r2, #1540
	ldr	r0, .L3945+8
	bl	printk
	bl	dump_stack
.L3902:
	movw	r3, #2828
	ldr	r5, .L3945+24
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #1080]
	sub	r9, r5, #3088
	sub	fp, r5, #3056
	str	fp, [sp, #4]
	add	r3, r3, r2, asl #2
	ldrh	r8, [r9, #-8]
	ldrh	r7, [fp, #-10]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	sub	r8, r8, #1
	and	r3, r3, #224
	uxth	r8, r8
	cmp	r3, #160
	mul	r7, r7, r10
	movne	r3, #1
	bne	.L3903
	ldrb	r2, [r5, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r3, r2
	orr	r7, r7, r2, asl #24
	subeq	r2, r5, #3104
	ldreqh	r8, [r2, #-10]
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	subeq	r8, r8, #1
	uxtheq	r8, r8
	cmp	r2, #0
	bne	.L3905
	ldr	r2, .L3945
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3903
.L3905:
	ldr	r2, .L3945+28
	ldrh	r8, [r2, #-10]
	sub	r8, r8, #1
	uxth	r8, r8
.L3903:
	mov	r0, #1
	strb	r3, [r4, #2834]
	str	r3, [sp]
	orr	r7, r8, r7
	mov	r8, #0
	strh	r8, [r6, #2]	@ movhi
	strb	r8, [r4, #2832]
	strh	r8, [r6, #22]	@ movhi
	strh	r8, [r6, #24]	@ movhi
	bl	buf_alloc
	mov	r1, #1
	str	r7, [r0, #24]
	mov	fp, r0
	bl	sblk_read_page
	ldr	r2, [fp, #36]
	strh	r8, [r6, #20]	@ movhi
	cmn	r2, #1
	cmpne	r2, #512
	ldr	r3, [sp]
	beq	.L3942
	ldr	r0, [fp, #12]
	ldr	r2, .L3945+32
	ldr	r1, [r0]
	str	r2, [sp, #8]
	cmp	r1, r2
	beq	.L3907
	ldr	r0, .L3945+8
	movw	r2, #1578
	ldr	r1, .L3945+4
.L3943:
	bl	printk
	bl	dump_stack
.L3942:
	mov	r0, fp
	bl	zbuf_free
	b	.L3940
.L3907:
	ldrb	r8, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r1, [r9, #-8]
	ldr	r2, .L3945+24
	smulbb	r1, r1, r8
	smulbb	r1, r1, r3
	uxth	r1, r1
	str	r1, [sp]
	ldrb	r1, [r5, #545]	@ zero_extendqisi2
	cmp	r3, #3
	cmpeq	r1, #2
	bne	.L3908
	movw	r3, #542
	ldr	r1, [fp, #4]
	ldrh	r0, [r2, r3]
	sub	r3, r2, #3104
	ldrh	r3, [r3, #-10]
	mov	r3, r3, asl #2
	smulbb	r8, r3, r8
	ldr	r3, [r2, #-3128]
	rsb	r8, r0, r8
	mov	r0, r0, lsr #2
	uxth	r8, r8
	add	r0, r3, r0, asl #2
	mov	r2, r8
	bl	ftl_memcpy
	ldr	r3, [fp, #12]
	ldr	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3909
	ldr	r0, [fp, #4]
	mov	r1, r8
	str	r3, [sp, #12]
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3909
	mov	r0, fp
	bl	zbuf_free
	ldr	r3, [fp, #12]
	ldr	r0, .L3945+36
	ldr	r2, [fp, #24]
	ldr	r1, [r3, #4]
	mov	r3, r8
	b	.L3941
.L3909:
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3910
	ldr	r3, .L3945
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3911
.L3910:
	sub	r7, r7, #1
	b	.L3912
.L3911:
	ldr	r3, [sp, #4]
	ldrh	r7, [r9, #-8]
	ldrh	r3, [r3, #-10]
	sub	r7, r7, #1
	uxth	r7, r7
	mul	r10, r3, r10
	orr	r7, r7, #33554432
	orr	r7, r7, r10
.L3912:
	str	r7, [fp, #24]
	mov	r0, fp
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [fp, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3942
	ldr	r3, [fp, #12]
	ldr	r2, [sp, #8]
	ldr	r3, [r3]
	cmp	r3, r2
	ldrne	r0, .L3945+8
	movwne	r2, #1619
	ldrne	r1, .L3945+4
	bne	.L3943
.L3914:
	movw	r3, #542
	ldr	r0, [r5, #-3128]
	ldr	r1, [fp, #4]
	ldrh	r2, [r5, r3]
	b	.L3939
.L3908:
	ldr	r3, [sp]
	ldr	r8, [r0, #4]
	ldr	r0, [fp, #4]
	mov	r7, r3, asl #2
	str	r2, [sp, #4]
	mov	r1, r7
	bl	js_hash
	ldr	r2, [sp, #4]
	cmp	r8, r0
	ldreq	r1, [fp, #4]
	ldreq	r0, [r2, #-3128]
	moveq	r2, r7
	beq	.L3939
	mov	r0, fp
	bl	zbuf_free
	ldr	r3, [fp, #12]
	ldr	r0, .L3945+36
	ldr	r2, [fp, #24]
	ldr	r1, [r3, #4]
	mov	r3, r7
.L3941:
	bl	printk
.L3940:
	mvn	r0, #0
	b	.L3895
.L3939:
	bl	ftl_memcpy
	ldr	r7, [r5, #-3128]
	ldr	r10, .L3945+24
	mov	r8, #0
	ldr	r9, .L3945+20
	sub	r7, r7, #4
.L3917:
	ldr	r3, [sp]
	cmp	r8, r3
	bge	.L3944
	ldr	r0, [r7, #4]!
	cmn	r0, #1
	beq	.L3919
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3920
	ldr	r0, [r7]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3920:
	ldr	r3, .L3945+40
	mvn	r1, #0
	ldr	r2, [sp, #20]
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	str	r2, [sp, #4]
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mov	r0, r2, lsr r0
	bic	r0, r0, r1, asl r3
	ldrb	r1, [r5, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrh	r3, [r6]
	cmp	r0, r3
	ldr	r2, [sp, #4]
	ldreq	r3, [r10, #-3128]
	ldreqh	r1, [r9, #20]
	streq	r2, [r3, r1, asl #2]
	ldreqh	r3, [r9, #20]
	addeq	r3, r3, #1
	streqh	r3, [r9, #20]	@ movhi
.L3919:
	bl	timer_get_time
	add	r8, r8, #1
	b	.L3917
.L3944:
	mov	r0, fp
	bl	zbuf_free
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r6, #20]
	cmp	r2, r3
	beq	.L3923
	ldr	r0, .L3945+44
	bl	printk
.L3923:
	movw	r3, #2828
	ldrh	r1, [r6, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldrh	r0, [r6, #20]
	strh	r3, [r6, #24]	@ movhi
.L3895:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3946:
	.align	2
.L3945:
	.word	.LANCHOR0
	.word	.LANCHOR1+2612
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC253
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LC254
	.word	.LANCHOR3-3132
	.word	.LC255
	.fnend
	.size	gc_scan_src_blk, .-gc_scan_src_blk
	.align	2
	.global	gc_scan_static_data
	.type	gc_scan_static_data, %function
gc_scan_static_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	ldr	r4, .L3964
	ldr	r3, [r4, #1092]
	ldr	r2, [r3, #544]
	cmn	r2, #1
	beq	.L3948
	mov	r5, #0
	mov	r7, r4
.L3949:
	ldr	r3, [r4, #1092]
	add	r1, sp, #4
	mov	r2, #0
	uxth	r6, r5
	ldr	r0, [r3, #544]
	bl	pm_log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3950
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #4]
	mov	r8, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmp	r3, #256
	bne	.L3951
	ldr	r3, .L3964+4
	mov	r9, #1
	ldr	r0, [sp, #4]
	ldrh	r2, [r3]
	ldrb	r3, [r7, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r3, r9, asl r3
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	ldr	r3, .L3964+8
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r1, r9
	mov	r2, #0
	uxth	r0, r0
	bl	gc_add_sblk
.L3951:
	ldr	r2, [r8, #12]
	ldr	r3, [r4, #1092]
	ldr	r2, [r2, #4]
	ldr	r3, [r3, #544]
	cmp	r2, r3
	beq	.L3952
	ldr	r1, .L3964+12
	movw	r2, #2163
	ldr	r0, .L3964+16
	bl	printk
	bl	dump_stack
.L3952:
	mov	r0, r8
	bl	zbuf_free
.L3950:
	ldr	r3, [r4, #1092]
	ldr	r1, [r4, #2784]
	ldr	r2, [r3, #544]
	add	r2, r2, #1
	str	r2, [r3, #544]
	cmp	r2, r1
	bcc	.L3953
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
	b	.L3947
.L3953:
	ldr	r3, [sp, #4]
	cmn	r3, #1
	bne	.L3947
	ldr	r3, .L3964+20
	add	r5, r5, #1
	ldrh	r3, [r3]
	cmp	r6, r3, lsr #2
	bcc	.L3949
	b	.L3947
.L3948:
	ldr	r2, [r3, #536]
	ldr	r1, [r3, #12]
	add	r2, r2, #12910592
	add	r2, r2, #49408
	cmp	r1, r2
	bhi	.L3958
	ldr	r0, [r4, #2804]
	ldr	r2, [r3, #540]
	ldr	r0, [r0, #44]
	add	r2, r2, #4992
	add	r2, r2, #8
	cmp	r0, r2
	bls	.L3947
.L3958:
	ldr	r2, [r4, #2804]
	ldr	r2, [r2, #44]
	str	r1, [r3, #536]
	str	r2, [r3, #540]
	mov	r2, #0
	str	r2, [r3, #544]
.L3947:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3965:
	.align	2
.L3964:
	.word	.LANCHOR0
	.word	.LANCHOR3-3132
	.word	.LANCHOR3
	.word	.LANCHOR1+2628
	.word	.LC0
	.word	.LANCHOR3+542
	.fnend
	.size	gc_scan_static_data, .-gc_scan_static_data
	.align	2
	.global	gc_block_vpn_scan
	.type	gc_block_vpn_scan, %function
gc_block_vpn_scan:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r4, #1076
	ldr	r5, .L4004
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r5, #1092]
	ldrh	r3, [r5, r4]
	ldr	r2, [r2, #608]
	cmp	r2, r3
	bcs	.L3966
	bl	timer_get_time
	ldr	r6, [r5, #1092]
	ldr	r3, [r6, #604]
	add	r3, r3, #29952
	add	r3, r3, #48
	cmp	r0, r3
	bls	.L3966
	bl	timer_get_time
	ldr	r3, [r5, #1092]
	ldrh	r1, [r5, r4]
	str	r0, [r6, #604]
	ldr	r2, [r3, #600]
	cmp	r2, r1
	bcs	.L3970
	ldr	r1, [r5, #2804]
	ldrh	r1, [r1, #134]
	cmp	r2, r1
	bcs	.L3971
.L3970:
	ldr	r2, [r5, #2804]
	ldrh	r2, [r2, #134]
	str	r2, [r3, #600]
.L3971:
	ldr	r4, [r3, #600]
	movw	r3, #65535
	uxth	r4, r4
	cmp	r4, r3
	bne	.L3972
	ldr	r1, .L4004+4
	movw	r2, #2504
	ldr	r0, .L4004+8
	bl	printk
	bl	dump_stack
.L3972:
	ldr	r3, [r5, #1092]
	mov	r0, r4
	add	r1, sp, #24
	ldr	r6, .L4004
	ldr	r2, [r3, #600]
	add	r2, r2, #1
	str	r2, [r3, #600]
	ldr	r2, [r3, #608]
	add	r2, r2, #1
	str	r2, [r3, #608]
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	beq	.L3966
	ldr	r9, [r6, #1080]
	add	r9, r9, r4, asl #2
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r2, r3, #224
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, #224
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L3973
	ldr	r3, [r6, #1092]
	ldrh	r1, [r3, #16]
	cmp	r1, r4
	beq	.L3973
	ldrh	r1, [r3, #48]
	cmp	r1, r4
	beq	.L3973
	ldrh	r3, [r3, #80]
	cmp	r3, r4
	bne	.L3974
.L3973:
	cmp	r2, #0
	bne	.L3966
	ldr	r3, [r5, #1088]
	mov	r4, r4, asl #1
	ldrh	r3, [r3, r4]
	cmp	r3, #0
	beq	.L3975
	ldr	r1, .L4004+4
	movw	r2, #2521
	ldr	r0, .L4004+8
	bl	printk
	bl	dump_stack
.L3975:
	ldr	r3, [r5, #1088]
	mov	r2, #0
	strh	r2, [r3, r4]	@ movhi
	b	.L3966
.L3974:
	add	r3, sp, #40
	add	r0, r3, r0, asl #1
	movw	r3, #65535
	ldrh	r7, [r0, #-18]
	cmp	r7, r3
	bne	.L3977
	ldr	r1, .L4004+4
	movw	r2, #2529
	ldr	r0, .L4004+8
	bl	printk
	bl	dump_stack
.L3977:
	ldr	r6, .L4004+12
	ldrb	r10, [r9, #2]	@ zero_extendqisi2
	sub	fp, r6, #3088
	ldrh	r3, [fp, #-8]
	sub	r3, r3, #1
	uxth	r8, r3
	sub	r3, r6, #3056
	ldrh	r2, [r3, #-10]
	sub	r3, r6, #3104
	mul	r7, r2, r7
	and	r2, r10, #224
	cmp	r2, #160
	movne	r10, #1
	bne	.L3978
	ldrb	r2, [r6, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r10, r2
	orr	r7, r7, r2, asl #24
	ldreqh	r8, [r3, #-10]
	subeq	r8, r8, #1
	uxtheq	r8, r8
.L3978:
	mov	r0, #1
	str	r3, [sp, #12]
	bl	buf_alloc
	orr	r7, r8, r7
	mov	r1, #1
	str	r7, [r0, #24]
	mov	r8, r0
	bl	sblk_read_page
	mov	r1, #255
	ldr	r0, [r6, #-3128]
	ldr	r3, [sp, #12]
	ldrh	r2, [r3, #-10]
	ldrb	r3, [r6, #-3123]	@ zero_extendqisi2
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r8, #36]
	cmn	r2, #1
	cmpne	r2, #512
	moveq	r2, #1
	movne	r2, #0
	moveq	r0, r4
	moveq	r1, #1
	moveq	r2, #0
	beq	.L4002
	ldr	r3, [r8, #12]
	ldr	r1, [r3]
	ldr	r3, .L4004+16
	cmp	r1, r3
	beq	.L3980
	mov	r0, r4
	mov	r1, #1
.L4002:
	bl	gc_add_sblk
	mov	r0, r8
	bl	zbuf_free
	b	.L3966
.L3980:
	ldr	r3, .L4004+12
	mov	r7, r2
	ldrh	fp, [fp, #-8]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	smulbb	fp, r3, fp
	smulbb	fp, fp, r10
	ldr	r10, [r8, #4]
	uxth	r3, fp
	mov	fp, r2
	str	r3, [sp, #12]
.L3981:
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bge	.L4003
	ldr	r0, [r10, fp, asl #2]
	cmn	r0, #1
	beq	.L3982
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3983
	ldr	r0, [r10, fp, asl #2]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3983:
	ldr	r3, .L4004+20
	ldrb	r2, [r5, #1189]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	ldrh	r1, [r3]
	rsb	r2, r2, #24
	mov	r3, #1
	rsb	r2, r1, r2
	mov	r2, r3, asl r2
	sub	r2, r2, #1
	and	r0, r2, r0, lsr r1
	ldrb	r1, [r6, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	cmp	r0, r4
	addeq	r7, r7, #1
	uxtheq	r7, r7
.L3982:
	add	fp, fp, #1
	b	.L3981
.L4003:
	mov	r0, r8
	mov	r6, r4, asl #1
	bl	zbuf_free
	ldr	r3, .L4004+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3985
	ldr	r3, [r5, #1088]
	mov	r1, r4
	ldr	r0, .L4004+28
	ldrh	r2, [r3, r6]
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r7
	bl	printk
.L3985:
	ldr	r3, [r5, #1088]
	cmp	r7, #31
	strh	r7, [r3, r6]	@ movhi
	bhi	.L3966
	mov	r0, r4
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L3966:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4005:
	.align	2
.L4004:
	.word	.LANCHOR0
	.word	.LANCHOR1+2648
	.word	.LC0
	.word	.LANCHOR3
	.word	-178307901
	.word	.LANCHOR3-3132
	.word	.LANCHOR2
	.word	.LC256
	.fnend
	.size	gc_block_vpn_scan, .-gc_block_vpn_scan
	.align	2
	.global	ftl_sblk_dump
	.type	ftl_sblk_dump, %function
ftl_sblk_dump:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r0, asl #2
	ldr	r4, .L4044
	.pad #132
	sub	sp, sp, #132
	mov	r7, r0
	str	r1, [sp, #60]
	ldr	r2, [r4, #1080]
	add	r3, r2, fp
	ldrh	r1, [r2, fp]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r10, [r2, r0, asl #2]
	mov	r1, r7
	mov	r2, r3, lsr #5
	ldr	r0, .L4044+4
	ubfx	r3, r3, #3, #2
	ubfx	r10, r10, #11, #8
	str	r10, [sp, #4]
	bl	printk
	movw	r3, #65535
	cmp	r7, r3
	beq	.L4029
	movw	r3, #1076
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bls	.L4029
	ldr	r3, [r4, #1080]
	add	r1, sp, #112
	mov	r0, r7
	strh	r7, [sp, #96]	@ movhi
	add	r3, r3, fp
	mov	r5, #0
	mov	r9, r5
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	ldreq	r3, .L4044+8
	movne	r10, #1
	ldreqb	r10, [r3, #-3124]	@ zero_extendqisi2
	bl	ftl_get_blk_list_in_sblk
	ldr	r3, .L4044+12
	mov	r1, r7
	mov	r2, r3
	ldrh	r3, [r2, #-8]!
	uxtb	r0, r0
	strh	r5, [sp, #98]	@ movhi
	strb	r0, [sp, #105]
	smulbb	r3, r3, r0
	strb	r5, [sp, #101]
	str	r2, [sp, #64]
	strh	r5, [sp, #106]	@ movhi
	strh	r3, [sp, #102]	@ movhi
	ldr	r3, [r4, #1080]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	mov	r3, r10
	str	r0, [sp]
	ldr	r0, .L4044+16
	mov	r2, r2, lsr #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	r6, r0
	str	r5, [sp, #44]
	str	r5, [sp, #52]
.L4009:
	ldr	r3, [sp, #64]
	uxth	r2, r5
	str	r2, [sp, #48]
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L4026
	mov	r3, r2, asl #1
	add	r2, r3, r2
	sub	r3, r3, #1
	str	r3, [sp, #72]
	mov	r3, #0
	sub	r2, r2, #1
	str	r2, [sp, #68]
.L4042:
	str	r3, [sp, #56]
	ldrb	r2, [sp, #105]	@ zero_extendqisi2
	ldrh	r3, [sp, #56]
	cmp	r2, r3
	bls	.L4024
	mov	r8, #1
	add	r3, r3, #8
	str	r3, [sp, #76]
.L4025:
	cmp	r8, r10
	bhi	.L4043
	ldr	r2, [sp, #76]
	add	r3, sp, #128
	add	r3, r3, r2, asl #1
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L4010
	ldr	r3, .L4044+20
	cmp	r10, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L4011
	ldr	r1, .L4044
	ldrb	r0, [r1, #1196]	@ zero_extendqisi2
	cmp	r0, #0
	ldrne	r1, [sp, #68]
	bne	.L4040
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	mov	r4, r8, asl #24
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #68]
	addne	r3, r3, r1
	addne	r3, r3, r8
	orr	r4, r4, r3
	b	.L4013
.L4011:
	cmp	r10, #2
	uxtahne	r4, r3, r5
	bne	.L4013
	ldr	r1, [sp, #72]
.L4040:
	add	r3, r3, r1
	ldr	r1, .L4044+8
	add	r3, r3, r8
	ldrb	r4, [r1, #-3124]	@ zero_extendqisi2
	orr	r4, r3, r4, asl #24
.L4013:
	mov	r1, #1
	str	r4, [r6, #24]
	mov	r0, r6
	str	r2, [sp, #84]
	bl	sblk_read_page
	ldr	r1, [r6, #4]
	ldr	ip, [r6, #36]
	ldr	r3, [r6, #12]
	str	ip, [sp]
	ldr	r0, [r1]
	ldr	r2, [sp, #84]
	str	ip, [sp, #80]
	str	r0, [sp, #4]
	ldr	r0, [r1, #4]
	str	r0, [sp, #8]
	ldr	r0, [r1, #8]
	str	r0, [sp, #12]
	ldr	r1, [r1, #12]
	ldr	r0, .L4044+24
	str	r1, [sp, #16]
	ldr	r1, [r3]
	str	r1, [sp, #20]
	ldr	r1, [r3, #4]
	str	r1, [sp, #24]
	ldr	r1, [r3, #8]
	str	r1, [sp, #28]
	mov	r1, r2
	ldr	r3, [r3, #12]
	ldr	r2, [sp, #48]
	str	r3, [sp, #32]
	mov	r3, r4
	bl	printk
	ldr	r0, .L4044+28
	ldr	ip, [sp, #80]
	ldr	r3, [sp, #52]
	cmn	ip, #1
	cmpne	ip, #512
	moveq	r3, #1
	str	r3, [sp, #52]
	ldr	r3, .L4044+32
	ldr	r3, [r3, #4]
	blx	r3
	ldr	r3, .L4044
	ldr	r3, [r3, #1080]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #224
	cmpne	r2, #32
	moveq	r2, #1
	movne	r2, #0
	beq	.L4010
	ldr	r3, [r6, #12]
	str	r2, [sp, #80]
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #92]
	ldr	r2, [sp, #80]
	bne	.L4017
	ldr	r3, [r6, #12]
	add	r1, sp, #92
	ldr	r0, [r3, #4]
	bl	pm_log2phys
.L4017:
	ldr	r3, [sp, #92]
	cmp	r3, r4
	bne	.L4018
	ldr	r3, [sp, #44]
	mov	r1, r4
	ldr	r0, .L4044+36
	add	r3, r3, #1
	str	r3, [sp, #44]
	ldr	r3, [r6, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #44]
	bl	printk
.L4018:
	ldr	r3, [sp, #60]
	cmp	r3, #0
	beq	.L4020
	ldr	r2, [r3, r9, asl #2]
	mov	r4, r9, asl #2
	ldr	r3, [r6, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	beq	.L4021
	ldr	r3, .L4044+40
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4021
	ldr	r0, .L4044+44
	mov	r1, r9
	bl	printk
.L4021:
	ldr	r3, [sp, #60]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r4]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	cmnne	r3, #1
	beq	.L4020
	ldr	r1, .L4044+48
	movw	r2, #1575
	ldr	r0, .L4044+52
	bl	printk
	bl	dump_stack
.L4020:
	add	r9, r9, #1
.L4010:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L4025
.L4043:
	ldr	r3, [sp, #56]
	add	r3, r3, #1
	b	.L4042
.L4024:
	add	r5, r5, #1
	b	.L4009
.L4026:
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, .L4044
	ldr	r0, .L4044+56
	mov	r1, r7
	ldr	r2, [r3, #1088]
	mov	r3, r7, asl #1
	ldrh	r2, [r2, r3]
	ldr	r3, [sp, #44]
	bl	printk
	ldr	r0, [sp, #52]
	b	.L4007
.L4029:
	mov	r0, #0
.L4007:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4045:
	.align	2
.L4044:
	.word	.LANCHOR0
	.word	.LC257
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC258
	.word	.LANCHOR3-3066
	.word	.LC196
	.word	644245000
	.word	arm_delay_ops
	.word	.LC259
	.word	.LANCHOR2
	.word	.LC260
	.word	.LANCHOR1+2668
	.word	.LC0
	.word	.LC261
	.fnend
	.size	ftl_sblk_dump, .-ftl_sblk_dump
	.align	2
	.global	zftl_read
	.type	zftl_read, %function
zftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r3
	ldr	r3, .L4113
	.pad #76
	sub	sp, sp, #76
	mov	r8, r0
	mov	r4, r1
	mov	r5, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4047
	ldr	r0, .L4113+4
	mov	r1, r8
	mov	r2, r4
	mov	r3, r5
	bl	printk
.L4047:
	cmp	r8, #0
	ldreq	r3, .L4113+8
	moveq	ip, #24576
	ldreq	r8, [r3, #1032]
	beq	.L4049
.L4048:
	cmp	r8, #3
	bhi	.L4078
	mov	ip, r8, asl #13
	mov	r8, #8192
.L4049:
	cmp	r5, r8
	cmpls	r4, r8
	movcs	r7, #1
	movcc	r7, #0
	bcs	.L4078
	add	r3, r4, r5
	cmp	r3, r8
	bhi	.L4078
	ldr	r2, .L4113+8
	add	r3, ip, r4
	str	r3, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r2, #2804]
	ldrb	r4, [r2, #2772]	@ zero_extendqisi2
	ldr	r3, [r1, #24]
	add	r3, r3, r5
	str	r3, [r1, #24]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #28]
	str	r0, [sp, #36]
	add	r3, r3, r5
	str	r3, [sp, #44]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	str	r7, [sp, #40]
	ldr	r4, [sp, #36]
	rsb	r3, r3, #1
	ldr	r7, .L4113+12
	add	r3, r3, r0
	str	r0, [sp, #48]
	str	r3, [sp, #32]
.L4051:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	beq	.L4110
	ldr	r3, [sp, #36]
	ldr	r8, .L4113+8
	rsb	r2, r3, r4
	ldr	r3, [sp, #48]
	clz	r2, r2
	rsb	r3, r3, r4
	ldrb	r1, [r8, #2772]	@ zero_extendqisi2
	clz	r3, r3
	mov	r2, r2, lsr #5
	mov	r9, r8
	mov	r3, r3, lsr #5
	uxth	r10, r1
	orrs	fp, r3, r2
	beq	.L4052
	cmp	r2, #0
	beq	.L4053
	ldr	r0, [sp, #28]
	bl	__aeabi_uidivmod
	uxth	fp, r1
	rsb	r10, fp, r10
	uxth	r10, r10
	cmp	r10, r5
	uxthhi	r10, r5
	b	.L4052
.L4053:
	cmp	r3, #0
	ldrne	r3, [sp, #28]
	movne	fp, r2
	moveq	fp, r3
	addne	r3, r3, r5
	mlsne	r1, r4, r1, r3
	uxtbne	r10, r1
.L4052:
	ldr	r2, .L4113+16
	mov	r3, #0
.L4056:
	ldr	r1, [r2, #20]
	cmp	r1, r4
	bne	.L4054
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L4054
	ldr	r1, .L4113+8
	mov	r2, #48
	mov	r10, r10, asl #9
	mov	r0, r6
	add	r6, r6, r10
	mla	r3, r2, r3, r1
	mov	r2, r10
	ldr	r1, [r3, #1240]
	add	r1, r1, fp, asl #9
	bl	ftl_memcpy
	b	.L4055
.L4054:
	add	r3, r3, #1
	add	r2, r2, #48
	cmp	r3, #32
	bne	.L4056
	mov	r0, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #68]
	bne	.L4057
	mov	r0, r4
	add	r1, sp, #68
	mov	r2, #0
	bl	pm_log2phys
.L4057:
	ldr	r3, [sp, #68]
	cmn	r3, #1
	moveq	r10, #0
	bne	.L4111
.L4059:
	ldrb	r3, [r9, #2772]	@ zero_extendqisi2
	cmp	r10, r3
	bcs	.L4055
	mla	r3, r3, r4, r10
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #44]
	cmp	r3, r2
	movcs	r2, #1
	movcc	r2, #0
	cmp	r3, r1
	movcs	r2, #0
	cmp	r2, #0
	beq	.L4060
	mov	r0, r6
	mov	r1, #0
	mov	r2, #512
	add	r6, r6, #512
	bl	ftl_memset
.L4060:
	add	r10, r10, #1
	b	.L4059
.L4111:
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4062
	ldr	r2, .L4113+8
	ldr	r1, [r2, #2804]
	ldr	r2, [r1, #40]
	add	r2, r2, #1
	str	r2, [r1, #40]
	ldr	r2, [sp, #68]
	str	r6, [r3, #8]
	add	r6, r6, r10, asl #9
	str	r4, [r3, #20]
	str	r2, [r3, #24]
	str	r2, [r3, #28]
	strb	r10, [r3, #40]
	strb	fp, [r3, #41]
	bl	zftl_add_read_buf
.L4055:
	ldr	r3, [sp, #32]
	add	r4, r4, #1
	sub	r3, r3, #1
	str	r3, [sp, #32]
.L4062:
	ldrb	r3, [r8, #2774]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	cmp	r2, #0
	cmpne	r3, #2
	bhi	.L4051
	ldrb	r1, [r7, #-2529]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4051
	ldrb	r2, [r7, #-2530]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L4113+16
	mla	r0, r0, r2, r3
	bl	sblk_read_page
.L4064:
	ldrb	r3, [r7, #-2529]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4112
	ldrb	r3, [r7, #-2530]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L4065
	ldr	r1, .L4113+20
	movw	r2, #1283
	ldr	r0, .L4113+24
	bl	printk
	bl	dump_stack
.L4065:
	ldr	r3, .L4113+12
	mov	ip, #48
	ldr	r8, .L4113+8
	ldr	r1, .L4113+12
	ldrb	r10, [r3, #-2530]	@ zero_extendqisi2
	ldr	r3, .L4113+16
	mul	ip, ip, r10
	add	r2, r8, ip
	add	r3, r3, ip
	str	r3, [sp, #52]
	ldr	r9, [r2, #1272]
	ldrb	r3, [r2, #1236]	@ zero_extendqisi2
	cmn	r9, #1
	strb	r3, [r1, #-2530]
	bne	.L4066
	ldr	r2, [r8, #1092]
	str	r9, [sp, #40]
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	b	.L4067
.L4066:
	cmp	r9, #256
	bne	.L4067
	ldr	r1, .L4113+28
	ldr	r3, [r2, #1260]
	str	r2, [sp, #60]
	ldrh	r0, [r1]
	ldrb	r1, [r8, #1189]	@ zero_extendqisi2
	mov	r8, #1
	str	r3, [sp, #56]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r8, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r7, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r2, [sp, #60]
	uxth	fp, r0
	ldr	r3, [sp, #56]
	mov	r1, fp
	ldr	r0, .L4113+32
	ldr	r2, [r2, #1256]
	bl	printk
	mov	r0, fp
	mov	r1, r8
	mov	r2, #0
	bl	gc_add_sblk
.L4067:
	ldr	r1, .L4113+8
	mov	r0, #48
	cmn	r9, #1
	mla	r0, r0, r10, r1
	movne	r2, #0
	moveq	r2, #1
	ldr	r3, [r0, #1248]
	ldr	ip, [r0, #1256]
	ldr	lr, [r3, #4]
	cmp	lr, ip
	orrne	r2, r2, #1
	cmp	r2, #0
	beq	.L4068
	ldr	r9, [r0, #1240]
	mvn	ip, #0
	ldr	r8, [r0, #1260]
	ldr	r2, [r0, #1244]
	ldrb	fp, [r0, #1276]	@ zero_extendqisi2
	ldrb	r0, [r1, #1189]	@ zero_extendqisi2
	ldrb	lr, [r1, #2772]	@ zero_extendqisi2
	rsb	r1, r0, #24
	mvn	ip, ip, asl r0
	cmp	fp, lr
	and	r0, ip, r8, lsr r1
	str	lr, [sp]
	movcc	r2, r9
	bic	r1, r8, ip, asl r1
	uxtb	r0, r0
	bl	flash_read_page_en
	mov	r9, r0
.L4068:
	ldr	ip, .L4113+8
	mov	r8, #48
	cmn	r9, #1
	mla	r8, r8, r10, ip
	movne	fp, #0
	moveq	fp, #1
	add	r3, r8, #1232
	add	r3, r3, #12
	ldr	r2, [r8, #1248]
	ldr	r1, [r8, #1256]
	ldr	r2, [r2, #4]
	cmp	r2, r1
	moveq	r2, fp
	orrne	r2, fp, #1
	cmp	r2, #0
	beq	.L4070
	ldr	r1, [ip, #1092]
	ldr	r0, .L4113+36
	str	ip, [sp, #56]
	ldr	r2, [r1, #552]
	add	r2, r2, #1
	str	r2, [r1, #552]
	ldr	r3, [r3, #4]
	ldr	r2, [r8, #1260]
	ldrb	r1, [r8, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r3]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	mov	r2, r9
	ldr	r3, [r3, #12]
	str	r3, [sp, #16]
	ldr	r3, [r8, #1256]
	bl	printk
	ldr	r3, .L4113+28
	ldr	r2, [r8, #1260]
	ldrb	r1, [r7, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	mov	r2, r2, lsr r0
	ldr	ip, [sp, #56]
	ldrb	r3, [ip, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
.L4070:
	ldr	r8, .L4113+8
	mov	r3, #48
	mla	r3, r3, r10, r8
	mov	r9, r8
	ldr	r2, [r3, #1248]
	ldr	r3, [r3, #1256]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	orrne	fp, fp, #1
	cmp	fp, #0
	beq	.L4071
	ldr	r1, .L4113+20
	movw	r2, #1319
	ldr	r0, .L4113+24
	bl	printk
	bl	dump_stack
.L4071:
	mov	r3, #48
	mla	r10, r3, r10, r9
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldrb	r2, [r10, #1276]	@ zero_extendqisi2
	cmp	r3, r2
	ldrlsb	r3, [r10, #1238]	@ zero_extendqisi2
	bicls	r3, r3, #8
	strlsb	r3, [r10, #1238]
	bls	.L4073
	ldrb	r1, [r10, #1277]	@ zero_extendqisi2
	mov	r2, r2, asl #9
	ldr	r3, [r10, #1240]
	ldr	r0, [r10, #1244]
	add	r1, r3, r1, asl #9
	bl	ftl_memcpy
.L4073:
	ldr	r1, [sp, #52]
	ldr	r0, .L4113+40
	bl	buf_remove_buf
	ldr	r0, [sp, #52]
	bl	zbuf_free
	ldrb	r3, [r7, #-2529]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r7, #-2529]
	b	.L4064
.L4112:
	ldr	r1, .L4113+12
	mvn	r2, #0
	strb	r3, [r1, #-2529]
	strb	r2, [r1, #-2530]
	b	.L4051
.L4110:
	bl	timer_get_time
	ldr	r3, .L4113+12
	str	r0, [r3, #548]
	ldr	r0, [sp, #40]
	b	.L4050
.L4078:
	mvn	r0, #0
.L4050:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4114:
	.align	2
.L4113:
	.word	.LANCHOR2
	.word	.LC262
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2684
	.word	.LC0
	.word	.LANCHOR3-3132
	.word	.LC263
	.word	.LC264
	.word	.LANCHOR0+2776
	.fnend
	.size	zftl_read, .-zftl_read
	.align	2
	.global	zftl_vendor_read
	.type	zftl_vendor_read, %function
zftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r1
	mov	r3, r2
	add	r1, r0, #512
	mov	r2, ip
	mov	r0, #2
	b	zftl_read
	.fnend
	.size	zftl_vendor_read, .-zftl_vendor_read
	.align	2
	.global	zftl_sys_read
	.type	zftl_sys_read, %function
zftl_sys_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	str	lr, [sp, #-4]!
	.save {lr}
	mov	ip, r1
	mov	lr, r0
	mov	r3, r2
	mov	r1, lr
	mov	r0, #2
	mov	r2, ip
	ldr	lr, [sp], #4
	b	zftl_read
	.fnend
	.size	zftl_sys_read, .-zftl_sys_read
	.align	2
	.type	zftl_debug_proc_write, %function
zftl_debug_proc_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	cmp	r2, #79
	.pad #124
	sub	sp, sp, #124
	mov	r5, r2
	add	r4, sp, #40
	mvnhi	r0, #21
	str	r4, [sp, #36]
	bhi	.L4119
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	mvnne	r0, #13
	bne	.L4119
	add	r3, sp, #120
	mov	r1, r4
	add	r3, r3, r5
	strb	r0, [r3, #-80]
	ldr	r0, .L4144
	bl	printk
	ldr	r0, .L4144+4
	mov	r1, r4
	mov	r2, #1
	mov	r3, #16
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	r0, r4
	ldr	r1, .L4144+8
	mov	r2, #7
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4120
	ldr	r4, .L4144+12
	movw	ip, #698
	ldr	r0, .L4144+16
	mov	r2, #4
	ldr	r7, .L4144+20
	movw	r8, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4144+24
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r7, #-152]
	bl	rknand_print_hex
.L4121:
	ldrh	r3, [r7, #-152]
	cmp	r6, r3
	bge	.L4124
	mov	r0, #300
	bl	msleep
	add	r3, r6, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r8
	beq	.L4122
	mov	r1, #0
	bl	ftl_sblk_dump
.L4122:
	add	r6, r6, #1
	b	.L4121
.L4120:
	mov	r0, r4
	ldr	r1, .L4144+28
	mov	r2, #7
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4125
	ldr	r7, .L4144+12
	movw	ip, #698
	ldr	r0, .L4144+16
	mov	r2, #4
	ldr	r9, .L4144+20
	ldr	r3, [r7, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r7, #2804]
	ldrh	r3, [r9, #-152]
	mov	r2, #2
	add	r1, r1, #416
	ldr	r0, .L4144+24
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
	ldr	r0, .L4144+32
	sub	r9, r9, #2528
	add	r4, sp, #120
	add	r1, sp, #47
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	mov	r4, r6
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	uxth	r8, r0
	str	r0, [sp, #32]
.L4127:
	add	r6, r9, r4
	ldr	r0, .L4144+36
	ldrh	r1, [r9, r4]
	ldrh	r2, [r6, #2]
	bl	printk
	ldrh	r3, [r4, r9]
	cmp	r3, r8
	bne	.L4126
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r0, .L4144+40
	ldr	r1, [r6, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
.L4126:
	add	r4, r4, #8
	cmp	r4, #256
	bne	.L4127
	mov	r0, #300
	add	r8, r8, #176
	bl	msleep
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r7, #2804]
	mov	r1, #1
	ldr	r3, [r3, r8, asl #2]
	str	r3, [r0, #24]
	mov	r4, r0
	str	r3, [sp, #32]
	bl	sblk_read_page
	ldr	ip, [r4, #4]
	ldr	r3, [r4, #12]
	ldr	r0, .L4144+44
	ldr	r2, [ip, #4]
	str	r2, [sp]
	ldr	r2, [ip, #8]
	ldr	r1, [sp, #32]
	str	r2, [sp, #4]
	ldr	r2, [ip, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3]
	str	r2, [sp, #12]
	ldr	r2, [r3, #4]
	str	r2, [sp, #16]
	ldr	r2, [r3, #8]
	str	r2, [sp, #20]
	ldr	r3, [r3, #12]
	str	r3, [sp, #24]
	ldr	r2, [r4, #36]
	ldr	r3, [ip]
	bl	printk
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	ldr	r0, .L4144+48
	mov	r2, #4
	ldr	r1, [r4, #4]
	mov	r3, r3, asl #7
	b	.L4143
.L4125:
	mov	r0, r4
	ldr	r1, .L4144+52
	mov	r2, #7
	bl	memcmp
	cmp	r0, #0
	bne	.L4128
	bl	dump_ftl_info
	b	.L4124
.L4128:
	mov	r0, r4
	ldr	r1, .L4144+56
	mov	r2, #9
	bl	memcmp
	cmp	r0, #0
	bne	.L4129
	add	r1, sp, #120
	add	r0, sp, #49
	str	r0, [r1, #-84]!
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4144+60
	str	r0, [sp, #32]
	strh	r0, [r3, #-12]	@ movhi
	bl	dump_all_list_info
	b	.L4124
.L4129:
	mov	r0, r4
	ldr	r1, .L4144+64
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	beq	.L4124
	mov	r0, r4
	ldr	r1, .L4144+68
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	bne	.L4131
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4144+32
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r6, .L4144+12
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	str	r0, [sp, #32]
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #32]
	mov	r4, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L4144+44
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r1, [r2, #8]
	str	r1, [sp, #20]
	ldr	r2, [r2, #12]
	ldr	r1, [sp, #32]
	str	r2, [sp, #24]
	ldr	r2, [r4, #36]
	ldr	r3, [r3]
	bl	printk
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	ldr	r0, .L4144+72
	mov	r2, #4
	ldr	r1, [r4, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	ldr	r0, .L4144+76
	mov	r2, #4
	ldr	r1, [r4, #12]
	mov	r3, r3, asl #1
.L4143:
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	b	.L4124
.L4131:
	mov	r0, r4
	ldr	r1, .L4144+80
	mov	r2, #8
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4132
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4144+32
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r1, r6
	str	r0, [sp, #32]
	uxth	r0, r0
	bl	ftl_sblk_dump
	b	.L4124
.L4132:
	mov	r0, r4
	ldr	r1, .L4144+84
	mov	r2, #10
	bl	memcmp
	cmp	r0, #0
	bne	.L4133
	add	r4, sp, #120
	add	r1, sp, #50
	ldr	r0, .L4144+32
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4144+88
	str	r0, [sp, #32]
	str	r0, [r3]
	b	.L4124
.L4133:
	mov	r0, r4
	ldr	r1, .L4144+92
	mov	r2, #8
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4134
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4144+32
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r4, r0
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #32]
	bne	.L4135
	mov	r0, r4
	add	r1, sp, #32
	mov	r2, r6
	bl	pm_log2phys
.L4135:
	mov	r1, r4
	ldr	r2, [sp, #32]
	ldr	r0, .L4144+96
	bl	printk
	b	.L4124
.L4134:
	ldr	r0, .L4144+100
	bl	printk
	ldr	r0, .L4144+104
	bl	printk
	ldr	r0, .L4144+108
	bl	printk
	ldr	r0, .L4144+112
	bl	printk
	ldr	r0, .L4144+116
	bl	printk
	ldr	r0, .L4144+120
	bl	printk
	ldr	r0, .L4144+124
	bl	printk
	ldr	r0, .L4144+128
	bl	printk
	ldr	r0, .L4144+132
	bl	printk
.L4124:
	bl	rknand_device_unlock
	mov	r0, r5
.L4119:
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L4145:
	.align	2
.L4144:
	.word	.LC265
	.word	.LC266
	.word	.LC267
	.word	.LANCHOR0
	.word	.LC268
	.word	.LANCHOR3
	.word	.LC269
	.word	.LC270
	.word	.LC271
	.word	.LC272
	.word	.LC273
	.word	.LC246
	.word	.LC274
	.word	.LC275
	.word	.LC276
	.word	.LANCHOR3-3056
	.word	.LC277
	.word	.LC278
	.word	.LC219
	.word	.LC241
	.word	.LC279
	.word	.LC280
	.word	.LANCHOR2
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
	.word	.LC291
	.fnend
	.size	zftl_debug_proc_write, .-zftl_debug_proc_write
	.align	2
	.global	ftl_update_l2p_map
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r5, .L4177
	.pad #20
	sub	sp, sp, #20
	ldrb	r7, [r0, #9]	@ zero_extendqisi2
	sub	r3, r5, #3088
	ldrh	r4, [r0, #12]
	ldrh	r3, [r3, #-8]
	mul	r7, r7, r3
	ldr	r3, [r5, #-2548]
	add	r4, r3, r4, asl #2
	sub	r3, r7, #-1073741823
	ldr	r3, [r4, r3, asl #2]
	cmn	r3, #1
	beq	.L4147
	ldr	r1, .L4177+4
	movw	r2, #1998
	ldr	r0, .L4177+8
	bl	printk
	bl	dump_stack
.L4147:
	mov	r5, #0
	sub	r4, r4, #4
	mov	r9, r5
.L4148:
	cmp	r9, r7
	beq	.L4176
	ldr	r8, [r4, #4]!
	cmn	r8, #1
	beq	.L4149
	ldr	r3, .L4177+12
	mov	r0, r8
	ldrb	r1, [r3, #2772]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, .L4177+16
	ldr	r3, [r3]
	tst	r3, #4096
	uxth	fp, r0
	beq	.L4150
	ldr	r0, .L4177+20
	mov	r1, fp
	mov	r2, r8
	mov	r3, r9
	bl	printk
.L4150:
	mov	r8, r4
	mov	r10, r9
.L4153:
	ldr	r0, [r8]
	cmn	r0, #1
	beq	.L4151
	ldr	r3, .L4177+12
	ldrb	r1, [r3, #2772]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, fp
	bne	.L4151
	ldrb	r3, [r6, #9]	@ zero_extendqisi2
	mov	r0, r10
	mov	r1, r3
	str	r3, [sp, #4]
	bl	__aeabi_idiv
	ldr	r2, .L4177+24
	ldrh	r2, [r2]
	ldr	r3, [sp, #4]
	mls	r3, r3, r0, r10
	add	r3, r6, r3, asl #1
	ldrh	r3, [r3, #16]
	mla	r2, r2, r3, r0
	ldr	r0, .L4177+28
	str	r2, [sp, #12]
	ldr	r1, [r8]
	bl	pm_ppa_update_check
	cmp	r0, #0
	beq	.L4152
	ldr	r3, .L4177
	mov	r2, #4
	ldr	r0, .L4177+32
	ldr	r1, [r3, #-2548]
	mov	r3, r7
	bl	rknand_print_hex
.L4152:
	add	r5, r5, #1
	ldr	r0, [r8]
	add	r1, sp, #12
	mov	r2, #1
	uxth	r5, r5
	bl	pm_log2phys
	mvn	r3, #0
	str	r3, [r8]
.L4151:
	add	r10, r10, #1
	add	r8, r8, #4
	cmp	r10, r7
	bne	.L4153
.L4149:
	add	r9, r9, #1
	b	.L4148
.L4176:
	ldr	r3, .L4177+16
	ldr	r4, .L4177+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4155
	ldrh	r1, [r6]
	mov	r2, r5
	ldr	ip, [r4, #1088]
	ldr	r0, .L4177+36
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L4155:
	ldrh	r3, [r6]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4178:
	.align	2
.L4177:
	.word	.LANCHOR3
	.word	.LANCHOR1+2696
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC292
	.word	.LANCHOR3-3066
	.word	.LC293
	.word	.LC294
	.word	.LC295
	.fnend
	.size	ftl_update_l2p_map, .-ftl_update_l2p_map
	.align	2
	.global	ftl_alloc_new_data_sblk
	.type	ftl_alloc_new_data_sblk, %function
ftl_alloc_new_data_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r0]
	bl	ftl_update_l2p_map
	bl	pm_flush
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4180
	bl	zftl_insert_data_list
.L4180:
	ldr	r4, .L4188
	mov	r0, r6
	ldr	r1, [r4, #1092]
	add	r1, r1, #16
	cmp	r1, r6
	movw	r6, #65535
	moveq	r1, #2
	movne	r1, #3
	bl	ftl_open_sblk_init
	ldr	r3, [r4, #1092]
	cmp	r5, r6
	ldr	r3, [r3, #560]
	rsb	r3, r5, r3
	clz	r3, r3
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4182
	mov	r1, r5
	ldr	r0, .L4188+4
	bl	printk
	ldr	r3, [r4, #1092]
	ldr	r0, [r3, #564]
	bl	gc_mark_bad_ppa
	ldr	r3, [r4, #1092]
	mvn	r2, #0
	str	r6, [r3, #560]
	str	r2, [r3, #564]
.L4182:
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L4189:
	.align	2
.L4188:
	.word	.LANCHOR0
	.word	.LC296
	.fnend
	.size	ftl_alloc_new_data_sblk, .-ftl_alloc_new_data_sblk
	.align	2
	.global	ftl_write_commit
	.type	ftl_write_commit, %function
ftl_write_commit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r7, .L4258
	mov	r8, r7
.L4191:
	ldrb	r2, [r7, #2800]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4193
	ldrb	r3, [r7, #2824]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r7, #2800]
	str	r3, [sp, #16]
	mov	r3, #48
	ldr	r1, [sp, #16]
	mul	r3, r3, r1
	ldr	r1, .L4258+4
	add	r1, r1, r3
	add	r3, r7, r3
	str	r1, [sp, #20]
	ldr	r2, [r3, #1256]
	ldrb	r1, [r3, #1236]	@ zero_extendqisi2
	ldr	r3, [r7, #2784]
	cmp	r2, r3
	strb	r1, [r7, #2824]
	bcc	.L4194
	ldr	r1, .L4258+8
	movw	r2, #606
	ldr	r0, .L4258+12
	bl	printk
	bl	dump_stack
.L4194:
	ldr	r2, [sp, #16]
	mov	r3, #48
	ldr	r1, [r8, #2784]
	mla	r2, r3, r2, r8
	ldr	r9, [r2, #1256]
	cmp	r9, r1
	bcc	.L4195
	ldr	r0, [sp, #20]
	bl	zbuf_free
	mvn	r0, #0
	b	.L4254
.L4195:
	ldr	r1, [r2, #1248]
	ldr	fp, [r2, #1240]
	ldrb	r5, [r2, #1277]	@ zero_extendqisi2
	ldrb	r2, [r2, #1276]	@ zero_extendqisi2
	str	r1, [sp, #8]
	str	r2, [sp, #12]
	ldrb	r2, [r7, #2801]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4197
	ldr	r2, .L4258+16
	ldrb	r4, [r2, #-96]	@ zero_extendqisi2
	ldr	r2, .L4258+4
	mla	r4, r3, r4, r2
.L4198:
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #255
	ldrne	r1, .L4258+4
	mlane	r4, r3, r2, r1
	bne	.L4198
.L4256:
	ldr	r3, [r4, #20]
	cmp	r3, r9
	bne	.L4197
	ldr	r3, [sp, #12]
	mov	r1, r5, asl #9
	ldr	r0, [r4, #4]
	mov	r2, r3, asl #9
	add	r0, r0, r1
	add	r1, fp, r1
	bl	ftl_memcpy
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r1, [r4, #12]
	mov	r0, #2
	sub	r3, r3, #2
	ldr	r2, [r4, #4]
	add	r1, r1, #16
	add	r2, r2, r3, asl #9
	bl	ftl_debug_info_fill
	ldr	r0, [sp, #20]
	bl	zbuf_free
	b	.L4191
.L4197:
	mov	r0, r9
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4201
	mov	r0, r9
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
.L4201:
	ldr	r6, [r8, #1092]
	mov	r1, #0
	ldr	r3, .L4258+4
	mov	r4, r1
	ldr	ip, [sp, #28]
	add	r6, r6, #16
	add	r0, r3, #1536
.L4203:
	ldr	r2, [r3, #20]
	cmp	r2, r9
	bne	.L4202
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	tst	r2, #8
	bicne	r2, r2, #8
	strneb	r2, [r3, #2]
	ldrne	r4, [r3, #4]
	movne	r1, #1
	ldrne	ip, [r3, #24]
.L4202:
	add	r3, r3, #48
	cmp	r3, r0
	bne	.L4203
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	cmp	r1, #0
	ldr	r2, [sp, #12]
	strne	ip, [sp, #28]
	cmp	r2, r3
	bcs	.L4230
	cmp	r4, #0
	beq	.L4206
	cmp	r5, #0
	beq	.L4207
	mov	r0, fp
	mov	r1, r4
	mov	r2, r5, asl #9
	bl	ftl_memcpy
	ldr	r6, [r7, #1092]
	add	r6, r6, #48
.L4207:
	ldr	r3, [sp, #12]
	ldrb	r10, [r8, #2772]	@ zero_extendqisi2
	add	r2, r5, r3
	cmp	r2, r10
	ldrcs	r6, [r7, #1092]
	addcs	r6, r6, #16
	bcs	.L4230
	mov	r1, r2, asl #9
	rsb	r2, r2, r10
	add	r0, fp, r1
	add	r1, r4, r1
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4230
.L4206:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4209
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #28]
	mov	r4, r0
	str	r9, [r0, #20]
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #4]
	ldr	r10, [r3, #12]
	cmp	r2, r9
	add	r10, r10, #1
	bne	.L4210
	ldr	r2, [r4, #36]
	cmn	r2, #1
	bne	.L4211
.L4210:
	ldr	ip, [r4, #24]
	mvn	r0, #0
	ldrb	r2, [r8, #1189]	@ zero_extendqisi2
	ldrb	lr, [r8, #2772]	@ zero_extendqisi2
	rsb	r1, r2, #24
	mvn	r2, r0, asl r2
	and	r0, r2, ip, lsr r1
	str	lr, [sp]
	bic	r1, ip, r2, asl r1
	uxtb	r0, r0
	ldr	r2, [r4, #4]
	bl	flash_read_page_en
	str	r0, [r4, #36]
.L4211:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r9
	bne	.L4212
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4213
.L4212:
	ldr	r2, [r7, #1092]
	ldr	r0, .L4258+20
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r4, #36]
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	str	r3, [sp]
	mov	r3, r9
	bl	printk
	mov	r2, #4
	ldr	r0, .L4258+24
	mov	r3, r2
	ldr	r1, [r4, #12]
	bl	rknand_print_hex
.L4213:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r9
	bne	.L4214
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4215
.L4214:
	ldr	r1, .L4258+8
	movw	r2, #698
	ldr	r0, .L4258+12
	bl	printk
	bl	dump_stack
.L4215:
	cmp	r5, #0
	beq	.L4216
	ldr	r3, [sp, #28]
	mov	r2, r5, asl #9
	cmn	r3, #1
	beq	.L4217
	mov	r0, fp
	ldr	r1, [r4, #4]
	bl	ftl_memcpy
	b	.L4218
.L4209:
	cmp	r5, #0
	moveq	r4, r5
	moveq	r10, r5
	beq	.L4216
	mov	r2, r5, asl #9
	mov	r10, r4
.L4217:
	mov	r0, fp
	mov	r1, #0
	bl	ftl_memset
.L4218:
	ldr	r6, [r7, #1092]
	add	r6, r6, #48
.L4216:
	ldr	r3, [sp, #12]
	ldrb	r2, [r7, #2772]	@ zero_extendqisi2
	add	r5, r5, r3
	cmp	r5, r2
	bcc	.L4219
	ldrhi	r6, [r7, #1092]
	addhi	r6, r6, #16
	b	.L4220
.L4219:
	ldr	r3, [sp, #28]
	rsb	r2, r5, r2
	mov	r5, r5, asl #9
	cmn	r3, #1
	mov	r2, r2, asl #9
	beq	.L4221
	ldr	r1, [r4, #4]
	add	r0, fp, r5
	add	r1, r1, r5
	bl	ftl_memcpy
	b	.L4220
.L4221:
	add	r0, fp, r5
	mov	r1, #0
	bl	ftl_memset
.L4220:
	cmp	r4, #0
	beq	.L4205
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L4258+28
	bic	r3, r3, #8
	strb	r3, [r4, #2]
	bl	buf_remove_buf
	mov	r0, r4
	bl	zbuf_free
	b	.L4205
.L4230:
	mov	r10, #0
.L4205:
	ldrh	r3, [r6, #6]
	cmp	r3, #0
	bne	.L4222
	bl	ftl_flush
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4222:
	mov	r0, r6
	mov	r4, #48
	bl	ftl_get_new_free_page
	ldr	r3, [sp, #16]
	str	r0, [sp, #12]
	mov	r0, #2
	mul	r4, r4, r3
	ldr	r3, [sp, #8]
	add	r5, r8, r4
	mov	r1, r3
	ldr	r2, [r5, #1252]
	str	r10, [r3, #12]
	stmia	r3, {r2, r9}
	ldr	r2, [sp, #28]
	ldrb	ip, [r8, #2772]	@ zero_extendqisi2
	str	r2, [r3, #8]
	mov	r2, #0
	str	r2, [r1, #16]!
	sub	r2, ip, #2
	add	r2, fp, r2, asl #9
	bl	ftl_debug_info_fill
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #20]
	str	r3, [r5, #1260]
	ldr	r3, [sp, #28]
	str	r3, [r5, #1264]
	mvn	r3, #0
	strb	r3, [r5, #1236]
	ldrb	r3, [r5, #1238]	@ zero_extendqisi2
	orr	r3, r3, #10
	strb	r3, [r5, #1238]
	ldr	r3, .L4258+4
	ldrh	r10, [r6, #10]
	ldrh	r9, [r6, #12]
	add	r4, r3, r4
	add	r9, r10, r9
	sub	r9, r9, #1
	strh	r9, [r4, #32]	@ movhi
	ldr	r4, .L4258+16
	sub	r0, r4, #96
	bl	buf_add_tail
	ldrb	r3, [r8, #2801]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r8, #2801]
	bl	timer_get_time
	ldrb	r3, [r8, #2801]	@ zero_extendqisi2
	ldrh	r2, [r6, #6]
	cmp	r3, #2
	str	r0, [r4, #-92]
	bhi	.L4223
	cmp	r2, #1
	bne	.L4193
.L4223:
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	mov	lr, #48
	ldrb	r0, [r4, #-96]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, #0
	moveq	ip, #1
	movne	ip, #2
	cmp	r2, #1
	mov	r2, r0
	moveq	ip, r3
.L4227:
	cmp	r1, ip
	mlane	r2, lr, r2, r7
	addne	r1, r1, #1
	ldrneb	r2, [r2, #1236]	@ zero_extendqisi2
	bne	.L4227
.L4257:
	strb	r2, [r4, #-96]
	uxtb	r1, r1
	ldr	r2, .L4258+4
	rsb	r3, r1, r3
	strb	r3, [r8, #2801]
	mov	r3, #48
	mla	r0, r3, r0, r2
	bl	sblk_prog_page
	ldrh	r3, [r6, #6]
	cmp	r3, #1
	bne	.L4193
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	r0, r6
	bl	ftl_write_last_log_page
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4193:
	ldrb	r4, [r7, #2800]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L4191
	bl	ftl_write_completed
	mov	r0, r4
.L4254:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4259:
	.align	2
.L4258:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2716
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC297
	.word	.LC241
	.word	.LANCHOR0+2776
	.fnend
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r8, .L4408
	ldr	r5, .L4408+4
	ldrb	r0, [r8, #-3115]	@ zero_extendqisi2
	ldr	r3, [r5, #1092]
	cmp	r0, #0
	str	r3, [sp, #12]
	bne	.L4261
	bl	buf_alloc
	subs	r4, r0, #0
	beq	.L4260
	ldr	r7, .L4408+8
	ldrh	r6, [r7, #22]
	mov	r0, r6
	add	r6, r6, #1
	bl	gc_get_src_ppa_from_index
	mov	r1, #1
	strh	r6, [r7, #22]	@ movhi
	str	r0, [r4, #24]
	mov	r9, r0
	mov	r0, r4
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4263
	ldr	r1, .L4408+12
	movw	r2, #1032
	ldr	r0, .L4408+16
	bl	printk
	bl	dump_stack
.L4263:
	ldr	r3, [r4, #12]
	ldr	r6, [r3, #4]
	mov	r0, r6
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #48]
	bne	.L4264
	mov	r0, r6
	add	r1, sp, #48
	mov	r2, #0
	bl	pm_log2phys
.L4264:
	ldr	r8, [sp, #48]
	cmp	r8, r9
	bne	.L4265
	ldr	r1, .L4408+20
	mov	r3, #0
.L4268:
	add	r2, r1, r3
	ldr	r0, [r2, #20]
	cmp	r0, r6
	bne	.L4266
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #2
	beq	.L4266
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L4408+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4260
	mov	r1, r6
	mov	r2, r8
	ldrh	r3, [r7, #22]
	ldr	r0, .L4408+28
	bl	printk
	b	.L4260
.L4266:
	add	r3, r3, #48
	cmp	r3, #1536
	bne	.L4268
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	mov	r2, #0
	str	r6, [r4, #20]
	strb	r2, [r4, #41]
	strb	r3, [r4, #40]
	ldr	r3, [r4, #12]
	ldr	r1, [r3]
	str	r1, [r4, #16]
	str	r2, [r3, #16]
	ldr	r3, .L4408+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4269
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r8, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4408+32
	str	r2, [sp, #4]
	mov	r2, r8
	bl	printk
.L4269:
	mov	r0, r4
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	r2, [r5, #2804]
	ldr	r3, [r2, #60]
	add	r3, r3, #1
	str	r3, [r2, #60]
	ldrh	r3, [r7, #24]
	add	r3, r3, #1
	strh	r3, [r7, #24]	@ movhi
	b	.L4260
.L4265:
	ldr	r3, .L4408+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4271
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r9, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4408+32
	str	r2, [sp, #4]
	mov	r2, r8
	bl	printk
.L4271:
	mov	r0, r4
	bl	zbuf_free
	b	.L4260
.L4261:
	ldr	r3, [sp, #12]
	ldrb	r9, [r3, #89]	@ zero_extendqisi2
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	uxth	fp, r9
	cmp	r3, fp
	movhi	r6, #1
	bhi	.L4273
	cmp	fp, #2
	movcc	r6, fp
	movcs	r6, #2
.L4273:
	ldrb	r4, [r8, #-3124]	@ zero_extendqisi2
	ldr	r10, .L4408
	cmp	r4, #3
	ldr	r3, .L4408+8
	bne	.L4274
	ldrb	r2, [r5, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4275
	movw	r2, #2102
	mov	r1, r9
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bl	__aeabi_idiv
	ldrb	r3, [r10, #-2536]	@ zero_extendqisi2
	cmp	r3, #0
	uxth	r5, r0
	beq	.L4276
	ldr	r3, .L4408+36
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
	b	.L4277
.L4276:
	ldrb	r3, [r10, #541]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L4277
	sub	r3, r5, #62
	cmp	r3, #2160
	movcs	r4, #2
	bcs	.L4277
	mov	r1, r4
	mov	r0, r5
	bl	__aeabi_uidivmod
	uxth	r1, r1
	cmp	r1, #0
	movne	r4, #1
	moveq	r4, #2
.L4277:
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
	b	.L4280
.L4275:
	ldrb	r2, [r10, #-3116]	@ zero_extendqisi2
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	beq	.L4279
	add	r1, r9, r9, asl #1
	bl	__aeabi_idiv
	ldr	r2, .L4408+40
	mul	r9, r9, r0
	uxth	r5, r0
	sub	r9, r9, r9, asl #2
	add	r1, r7, r9
	smull	r2, r3, r1, r2
	sub	r1, r3, r1, asr #31
	uxth	r3, r1
	str	r3, [sp, #16]
	ldr	r3, .L4408+4
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r5, r5, r5, asl #1
	uxthne	r5, r5
	b	.L4280
.L4279:
	mov	r1, r9
	bl	__aeabi_idiv
	ldr	r2, .L4408+44
	smulbb	r1, r0, fp
	mov	r3, r0, asl #1
	ldrh	r2, [r2, r3]
	rsb	r1, r1, r7
	and	r3, r2, #7
	str	r3, [sp, #20]
	mov	r5, r2, lsr #3
	uxth	r3, r1
	str	r3, [sp, #16]
	mla	r9, r9, r5, r3
	ldr	r3, [sp, #20]
	cmp	r3, #1
	add	r9, r9, r9, asl #1
	uxth	r7, r9
	bne	.L4281
	b	.L4280
.L4274:
	ldrb	r2, [r10, #-3122]	@ zero_extendqisi2
	mov	r1, r9
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bne	.L4282
	bl	__aeabi_idiv
	mov	r4, #1
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
	b	.L4280
.L4282:
	bl	__aeabi_idiv
	mov	r4, #2
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
.L4280:
	mul	r3, r6, r4
	ldr	r10, .L4408+36
	mov	r2, #0
	ldr	ip, .L4408
	add	r10, r10, r5, asl #1
	ldr	r1, .L4408+8
	str	r3, [sp, #20]
	b	.L4283
.L4403:
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4341
	tst	lr, #1
	beq	.L4341
.L4284:
	ldr	fp, [r8, #-168]
	ldr	r0, [fp, r3, asl #2]
	cmn	r0, #1
	beq	.L4402
.L4287:
	add	r2, r2, #1
.L4283:
	ldr	r3, [sp, #20]
	uxth	lr, r2
	cmp	lr, r3
	bge	.L4341
	ldr	r0, .L4408+48
	add	r3, r7, lr
	ldr	r9, [sp, #12]
	ldrh	r0, [r0]
	ldrb	r9, [r9, #89]	@ zero_extendqisi2
	mul	r0, r9, r0
	ldrb	r9, [r8, #545]	@ zero_extendqisi2
	rsb	r0, r9, r0
	cmp	r3, r0
	blt	.L4284
	ldrb	r0, [ip, #-2536]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L4403
.L4341:
	mov	r3, #1
	str	r3, [sp, #20]
.L4281:
	rsb	r1, r6, r6, asl #15
	mul	r3, r6, r4
	mov	r1, r1, asl #1
	smulbb	r1, r1, r4
	str	r3, [sp, #24]
	add	r1, r7, r1
	uxth	r3, r1
	ldr	r1, .L4408+36
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #28]
	add	r3, r1, r5, asl #1
	str	r3, [sp, #32]
	b	.L4286
.L4402:
	ldrh	r9, [r1, #22]
	ldrh	r0, [r1, #20]
	cmp	r0, r9
	bls	.L4260
	ldrb	r0, [ip, #-2536]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L4288
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4288
	tst	lr, #1
	ldrne	r0, [fp, r0, asl #2]
	strne	r0, [fp, r3, asl #2]
	bne	.L4287
.L4288:
	mov	r0, r9
	str	r1, [sp, #36]
	str	ip, [sp, #32]
	add	r9, r9, #1
	str	r2, [sp, #28]
	str	r3, [sp, #24]
	bl	gc_get_src_ppa_from_index
	ldr	r3, .L4408+8
	strh	r9, [r3, #22]	@ movhi
	ldr	r3, [sp, #24]
	ldr	r1, [sp, #36]
	ldr	ip, [sp, #32]
	str	r0, [fp, r3, asl #2]
	ldr	r2, [sp, #28]
	b	.L4287
.L4312:
	ldr	r3, [fp, #-3120]
	add	r10, r7, r9
	ldrb	r3, [r3, r10]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L4404
.L4290:
	ldr	r1, .L4408
	add	r2, sp, #72
	add	r2, r2, r9, asl #2
	ldr	r0, .L4408+20
	ldr	ip, .L4408+4
	ldr	r3, [r1, #-3120]
	ldrb	r8, [r3, r10]	@ zero_extendqisi2
	mov	r3, #48
	mul	r3, r3, r8
	add	r0, r0, r3
	str	r0, [r2, #-24]
	ldrb	r2, [sp, #20]	@ zero_extendqisi2
	add	r3, ip, r3
	strb	r2, [r3, #1281]
	mov	r2, #2
	strh	r2, [r0, #34]	@ movhi
	ldrb	r2, [r1, #-3124]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L4303
	ldrb	r2, [ip, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4304
	ldrb	r1, [r1, #-2536]	@ zero_extendqisi2
	and	r2, r9, #1
	add	r3, r5, r2
	cmp	r1, #0
	beq	.L4305
	ldr	r1, [sp, #32]
	ldrh	r1, [r1]
	cmp	r1, r5
	movcs	r2, #0
	andcc	r2, r2, #1
	cmp	r2, #0
	movne	r3, r1
.L4305:
	ldr	r2, .L4408+4
	mov	r1, r4
	mov	fp, #48
	mov	r0, r9
	str	r3, [sp, #40]
	mla	fp, fp, r8, r2
	bl	__aeabi_uidiv
	ldr	r1, .L4408+52
	ldrh	r1, [r1]
	ldr	r3, [sp, #16]
	uxtah	r0, r3, r0
	ldr	r3, [sp, #12]
	add	r0, r3, r0, asl #1
	ldr	r3, [sp, #40]
	ldrh	r2, [r0, #96]
	mla	r3, r1, r2, r3
	orr	r3, r3, #50331648
	str	r3, [fp, #1260]
.L4306:
	mov	r3, #48
	mul	r8, r3, r8
	ldr	r3, .L4408+4
	add	r3, r3, r8
	ldr	r1, [r3, #1260]
	ldr	r3, .L4408+20
	add	r8, r3, r8
	ldr	r3, .L4408
	ldrh	r2, [r8, #32]
	ldr	r3, [r3, #-164]
	str	r1, [r3, r2, asl #2]
.L4331:
	ldr	r8, .L4408
	ldrb	r3, [r8, #-2536]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4405
.L4311:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
.L4286:
	ldrh	r9, [sp, #28]
	ldr	r3, [sp, #24]
	ldr	fp, .L4408
	cmp	r9, r3
	blt	.L4312
	b	.L4406
.L4404:
	mov	r0, #0
	bl	buf_alloc
	subs	r8, r0, #0
	bne	.L4291
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	r0, r8
	bl	buf_alloc
	subs	r8, r0, #0
	beq	.L4260
.L4291:
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldr	r3, [fp, #-3120]
	strb	r2, [r3, r10]
	ldr	r2, .L4408+4
	ldrb	r3, [r2, #2835]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r2, #2835]
	add	r3, r9, r7
	strh	r3, [r8, #32]	@ movhi
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [r8, #45]
	ldr	r3, .L4408+48
	ldrh	r2, [r3]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	ldrb	r2, [fp, #545]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r10, r3
	blt	.L4292
	ldr	r3, .L4408
	ldrb	r3, [r3, #-2536]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4293
	ldr	r3, [sp, #32]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4293
	tst	r9, #1
	bne	.L4292
.L4293:
	ldr	r3, .L4408
	mvn	r1, #0
	ldr	fp, .L4408+56
	ldr	r2, [r3, #-172]
	str	r1, [r2, r10, asl #2]
	ldrb	r2, [r3, #545]	@ zero_extendqisi2
	cmp	r2, #2
	bne	.L4294
	ldr	r2, .L4408+48
	ldr	ip, .L4408+60
	ldrh	r0, [r2]
	ldr	r2, [sp, #12]
	ldrb	r2, [r2, #89]	@ zero_extendqisi2
	mul	r2, r2, r0
	sub	r2, r2, #2
	cmp	r10, r2
	bne	.L4295
	ldrh	r2, [ip]
	ldr	r1, [r3, #-172]
	ldr	r0, [r8, #4]
	str	ip, [sp, #44]
	bl	ftl_memcpy
	ldr	r3, .L4408+4
	mov	r1, #0
	ldr	r0, [r8, #12]
	ldrb	r2, [r3, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	str	r3, [sp, #40]
	bl	ftl_memset
	ldr	r2, [r8, #12]
	str	fp, [r2]
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldr	ip, [sp, #44]
	ldrh	r1, [ip]
	bl	js_hash
	ldr	r3, [sp, #40]
	str	r0, [fp, #4]
	ldr	r2, [r8, #12]
	ldr	r3, [r3, #1092]
	ldr	r3, [r3, #132]
	str	r3, [r2, #8]
	mov	r2, #0
	ldr	r3, [r8, #12]
	b	.L4398
.L4295:
	ldrb	r2, [r3, #-3123]	@ zero_extendqisi2
	ldrh	r1, [ip]
	ldr	r3, [r3, #-172]
	mul	r0, r2, r0
	rsb	ip, r1, r0, asl #2
	mov	r1, r1, lsr #2
	ldr	r0, [r8, #4]
	mov	r2, ip
	add	r1, r3, r1, asl #2
	str	ip, [sp, #44]
	bl	ftl_memcpy
	ldr	r3, .L4408+4
	mov	r1, #0
	ldr	r0, [r8, #12]
	ldrb	r2, [r3, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	str	r3, [sp, #40]
	bl	ftl_memset
	ldr	r2, [r8, #12]
	str	fp, [r2]
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldr	ip, [sp, #44]
	mov	r1, ip
	bl	js_hash
	ldr	r3, [sp, #40]
	str	r0, [fp, #4]
	ldr	r2, [r8, #12]
	ldr	r3, [r3, #1092]
	ldr	r3, [r3, #132]
	str	r3, [r2, #8]
	mov	r2, #1
	ldr	r3, [r8, #12]
.L4398:
	str	r2, [r3, #12]
	b	.L4296
.L4294:
	ldr	ip, .L4408+48
	ldrb	r2, [r3, #-3123]	@ zero_extendqisi2
	ldr	r0, [r8, #4]
	ldrh	r1, [ip]
	str	ip, [sp, #44]
	str	r3, [sp, #40]
	mul	r2, r2, r1
	ldr	r1, [r3, #-172]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	ldr	r0, [r8, #4]
	ldr	ip, [sp, #44]
	ldr	r3, [sp, #40]
	ldrh	r1, [ip]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	ldr	ip, .L4408
	mul	r3, r3, r1
	ldr	r1, .L4408+4
	str	ip, [sp, #44]
	ldrb	r1, [r1, #2772]	@ zero_extendqisi2
	cmp	r1, r3, asr #6
	mov	r2, r3, asl #2
	ldrlt	r1, .L4408+60
	mov	r3, r2
	add	r0, r0, r3
	ldrlth	r1, [r1]
	rsblt	r2, r2, r1
	ldr	r1, [ip, #-168]
	bl	ftl_memcpy
	ldr	r3, .L4408+4
	mov	r1, #0
	ldr	r0, [r8, #12]
	ldrb	r2, [r3, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	str	r3, [sp, #40]
	bl	ftl_memset
	ldr	r2, [r8, #12]
	str	fp, [r2]
	ldr	r2, .L4408+48
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldrh	r1, [r2]
	ldr	ip, [sp, #44]
	ldrb	r2, [ip, #-3123]	@ zero_extendqisi2
	mul	r1, r2, r1
	mov	r1, r1, asl #2
	bl	js_hash
	ldr	r3, [sp, #40]
	str	r0, [fp, #4]
	ldr	r2, [r8, #12]
	ldr	r3, [r3, #1092]
	ldr	r3, [r3, #132]
	str	r3, [r2, #8]
	b	.L4296
.L4292:
	ldr	fp, .L4408
	mov	r0, r8
	mov	r1, #1
	ldr	r3, [fp, #-168]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r8, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4299
	ldr	r3, .L4408+64
	ldr	r2, [r8, #24]
	ldrb	r1, [fp, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldr	r3, .L4408+4
	mov	r2, r2, lsr r0
	ldrb	r3, [r3, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	ldreq	r3, [r8, #12]
	mvneq	r2, #0
	streq	r2, [r3, #4]
.L4299:
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4301
	ldr	r1, .L4408+12
	movw	r2, #1223
	ldr	r0, .L4408+16
	bl	printk
	bl	dump_stack
.L4301:
	ldr	r3, [r8, #12]
	ldr	r2, .L4408+4
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #2784]
	cmp	r1, r2
	mvncs	r2, #0
	strcs	r2, [r3, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r3, #4]
	ldr	r3, .L4408
	ldr	r3, [r3, #-172]
	str	r2, [r3, r10, asl #2]
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #24]
	str	r2, [r3, #8]
.L4296:
	ldr	r3, [r8, #12]
	mov	r2, #0
	str	r2, [r3, #16]
	b	.L4290
.L4304:
	ldrb	r2, [ip, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, .L4408+68
	umull	r0, r1, r9, r2
	beq	.L4307
	mov	r1, r1, lsr #1
	ldr	r0, [sp, #16]
	ldr	ip, [sp, #12]
	uxtah	r0, r0, r1
	add	r1, r1, r1, asl #1
	rsb	r1, r1, r9
	add	r0, ip, r0, asl #1
	ldr	ip, .L4408+52
	ldrh	r0, [r0, #96]
	ldrh	ip, [ip]
	mla	r0, ip, r0, r5
	uxtah	r0, r0, r1
	str	r0, [r3, #1260]
	b	.L4308
.L4307:
	ldr	r0, [sp, #16]
	ubfx	r1, r1, #1, #16
	add	r1, r0, r1
	ldr	r0, [sp, #12]
	add	r1, r0, r1, asl #1
	ldr	r0, .L4408+52
	ldrh	r1, [r1, #96]
	ldrh	r0, [r0]
	mla	r1, r0, r1, r5
	str	r1, [r3, #1260]
	b	.L4308
.L4303:
	cmp	r2, #2
	bne	.L4306
	ldrb	r2, [r1, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, [sp, #16]
	bne	.L4310
	ldr	r1, [sp, #12]
	add	r2, r2, r9
	add	r2, r1, r2, asl #1
	ldr	r1, .L4408+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	b	.L4399
.L4310:
	ldr	r1, [sp, #12]
	add	r2, r2, r9, lsr #1
	add	r2, r1, r2, asl #1
	ldr	r1, .L4408+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	and	r1, r9, #1
	add	r2, r2, r1
.L4399:
	orr	r2, r2, #33554432
	str	r2, [r3, #1260]
	b	.L4306
.L4405:
	ldr	r3, [sp, #32]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4311
	tst	r9, #1
	beq	.L4311
	ldr	r2, [r8, #-3120]
	mvn	r3, #0
	ldrh	r1, [sp, #36]
	ldr	r0, [sp, #36]
	strb	r3, [r2, r10]
	add	r2, sp, #72
	add	r9, r2, r9, asl #2
	ldr	r2, [r9, #-24]
	strh	r1, [r2, #32]	@ movhi
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r8, #-3120]
	strb	r1, [r2, r0]
	ldr	r2, [r8, #-168]
	str	r3, [r2, r10, asl #2]
	b	.L4311
.L4406:
	ldrb	r3, [fp, #-3124]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L4313
	ldr	r3, .L4408+4
	ldrb	r1, [r3, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [sp, #24]
	movne	r2, #0
	subne	r1, r3, #1
	beq	.L4407
.L4315:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4400
	add	ip, sp, #72
	add	r0, sp, #72
	add	ip, ip, r3, asl #2
	add	r0, r0, r3, asl #2
	ldr	r3, [ip, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [r0]
	b	.L4315
.L4407:
	ldrb	r2, [fp, #-3116]	@ zero_extendqisi2
	ldr	r3, [sp, #48]
	cmp	r2, #0
	strneb	r1, [r3, #44]
	bne	.L4401
	ldr	r1, [sp, #20]
	cmp	r1, #1
	moveq	r2, #9
	beq	.L4321
	ldr	r1, [sp, #20]
	cmp	r1, #2
	moveq	r2, #13
.L4321:
	strb	r2, [r3, #44]
.L4401:
	add	r1, sp, #60
	mov	r2, r6
	add	r0, sp, #48
	bl	sblk_xlc_prog_pages
	b	.L4317
.L4313:
	ldrb	r3, [fp, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4322
	ldrb	r3, [fp, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4323
.L4322:
	ldr	r3, [sp, #24]
	mov	r2, #0
	sub	r1, r3, #1
	b	.L4324
.L4323:
	mov	r1, r6
	add	r0, sp, #48
	bl	sblk_3d_mlc_prog_pages
	b	.L4317
.L4324:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4400
	add	ip, sp, #72
	add	r0, sp, #72
	add	ip, ip, r3, asl #2
	add	r0, r0, r3, asl #2
	ldr	r3, [ip, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [r0]
	b	.L4324
.L4400:
	add	r3, sp, #72
	mvn	r2, #0
	add	r1, r3, r1, asl #2
	ldr	r3, [r1, #-24]
	smulbb	r1, r4, r6
	strb	r2, [r3]
	ldr	r0, [sp, #48]
	uxtb	r1, r1
	bl	sblk_prog_page
.L4317:
	ldrb	r3, [fp, #-3116]	@ zero_extendqisi2
	ldr	r2, .L4408
	cmp	r3, #0
	ldr	r3, .L4408+4
	addne	r4, r6, r6, asl #1
	bne	.L4327
	ldrb	r1, [r2, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r4, r6, asl #1
	bne	.L4327
	ldrb	r1, [r3, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	moveq	r4, r6
	beq	.L4327
	ldrb	r2, [r2, #-2536]	@ zero_extendqisi2
	mul	r4, r4, r6
	cmp	r2, #0
	beq	.L4327
	ldr	r1, .L4408+36
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	movcc	r4, r6
.L4327:
	ldr	r2, [r3, #2804]
	ldr	r3, [r2, #52]
	add	r3, r3, r4
	str	r3, [r2, #52]
	ldr	r2, .L4408+8
	movw	r3, #2102
	ldrh	r1, [r2, r3]
	add	r4, r4, r1
	uxth	r4, r4
	strh	r4, [r2, r3]	@ movhi
	ldr	r3, .L4408+72
	ldrh	r2, [r3, #-10]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r4, r3
	ldrge	r3, .L4408+4
	movge	r2, #0
	ldrge	r3, [r3, #1092]
	strgeh	r2, [r3, #86]	@ movhi
	bl	gc_write_completed
	b	.L4260
.L4308:
	mov	r0, #48
	ldr	r3, .L4408+20
	mul	r0, r0, r8
	ldr	ip, .L4408+4
	add	r3, r3, r0
	add	r0, ip, r0
	ldrh	r1, [r3, #32]
	ldr	r3, .L4408
	ldr	r0, [r0, #1260]
	ldr	lr, [r3, #-164]
	umull	r2, r3, r9, r2
	mov	r2, r3, lsr #1
	add	r2, r2, r2, asl #1
	rsb	r2, r2, r9
	mov	r2, r2, asl #24
	add	r2, r2, #16777216
	orr	r2, r2, r0
	str	r2, [lr, r1, asl #2]
	b	.L4331
.L4260:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4409:
	.align	2
.L4408:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+2736
	.word	.LC0
	.word	.LANCHOR0+1236
	.word	.LANCHOR2
	.word	.LC298
	.word	.LC299
	.word	.LANCHOR2+2120
	.word	1431655766
	.word	.LANCHOR2+3656
	.word	.LANCHOR3-3114
	.word	.LANCHOR3-3066
	.word	-178307901
	.word	.LANCHOR3+542
	.word	.LANCHOR3-3132
	.word	-1431655765
	.word	.LANCHOR3-3104
	.fnend
	.size	gc_do_copy_back, .-gc_do_copy_back
	.align	2
	.global	zftl_do_gc
	.type	zftl_do_gc, %function
zftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2792
	ldr	r4, .L4635+60
	cmp	r0, #1
	.pad #44
	sub	sp, sp, #44
	mov	fp, r0
	ldr	r5, .L4635+32
	ldrh	r9, [r4, r3]
	movw	r3, #2788
	ldrh	r7, [r4, r3]
	movw	r3, #2790
	ldr	r6, [r4, #1092]
	add	r7, r9, r7
	ldrh	r8, [r4, r3]
	uxth	r7, r7
	beq	.L4411
.L4422:
	ldrb	r3, [r5, #-182]	@ zero_extendqisi2
	cmp	r3, #6
	ldrls	pc, [pc, r3, asl #2]
	b	.L4536
.L4414:
	.word	.L4413
	.word	.L4415
	.word	.L4416
	.word	.L4417
	.word	.L4418
	.word	.L4537
	.word	.L4420
.L4411:
	ldr	r10, [r5, #548]
	cmp	r10, #0
	bne	.L4421
	ldr	r3, [r5, #552]
	cmp	r3, #0
	beq	.L4422
.L4421:
	ldr	r3, .L4635+32
	sub	r2, r3, #3072
	ldrh	r2, [r2, #-14]
	cmp	r7, r2, lsr #2
	bls	.L4422
	movw	r2, #2808
	ldrh	r2, [r4, r2]
	cmp	r2, r7
	bcs	.L4422
	str	r3, [sp, #28]
	bl	timer_get_time
	add	r10, r10, #20
	ldr	r3, [sp, #28]
	cmp	r10, r0
	ldr	r10, [r5, #552]
	movcc	r2, #0
	strcc	r2, [r3, #548]
	bl	timer_get_time
	add	r10, r10, #20
	cmp	r10, r0
	ldrcc	r3, .L4635+32
	movcc	r2, #0
	strcc	r2, [r3, #552]
	ldr	r3, [r4, #1092]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4536
	b	.L4422
.L4413:
	movw	r2, #2794
	movw	r3, #2796
	ldrh	r3, [r4, r3]
	add	r8, r8, r9
	ldrh	r10, [r4, r2]
	ldrh	r2, [r6, #80]
	uxth	r8, r8
	add	r10, r3, r10
	movw	r3, #65535
	cmp	r2, r3
	ldr	r9, .L4635+60
	uxth	r10, r10
	beq	.L4425
	cmp	fp, #0
	bne	.L4426
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r7, r3, asl #1
	bge	.L4536
.L4426:
	ldr	fp, .L4635+32
	sub	r9, fp, #3088
	sub	r0, r9, #12
	ldrh	r1, [r9]
	add	r1, r1, #1
	uxth	r1, r1
	strh	r1, [r9]	@ movhi
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4428
	ldr	ip, [r4, #1088]
	mov	r2, r0, asl #1
	ldr	r3, [fp, #-180]
	ldr	r1, .L4635+60
	ldrh	r2, [ip, r2]
	add	r3, r3, #1
	ldrh	ip, [r9, #-8]
	str	r3, [fp, #-180]
	cmp	ip, r2
	bcs	.L4429
	movw	ip, #1076
	ldrh	ip, [r1, ip]
	cmp	r3, ip, lsr #4
	bls	.L4428
	movw	r3, #2810
	ldrh	r3, [r1, r3]
	cmp	r3, r2
	bls	.L4428
.L4429:
	mov	r1, #0
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	ldr	r9, .L4635+32
	cmp	r0, #0
	beq	.L4430
	mov	r3, #1
	strb	r3, [r9, #-182]
	mov	r3, #0
	str	r3, [r9, #-180]
	b	.L4536
.L4428:
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L4430:
	cmp	r8, #15
	movls	r9, #2
	bls	.L4431
	ldr	r2, .L4635
	movw	r3, #2798
	ldrh	r3, [r4, r3]
	ldrh	r2, [r2, #-12]
	cmp	r3, r2
	movls	r3, #0
	movhi	r3, #1
	cmp	r10, #0
	movne	r10, r3
	orreq	r10, r3, #1
	cmp	r10, #0
	movne	r9, #2
	moveq	r9, #1
.L4431:
	ldr	r3, .L4635+4
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4432
	ldr	r3, [r4, #1092]
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	str	r8, [sp]
	ldrh	r1, [r3, #124]
	ldr	r0, .L4635+8
	str	r1, [sp, #4]
	ldrh	r1, [r3, #120]
	str	r1, [sp, #8]
	movw	r1, #2807
	ldrh	r3, [r3, #122]
	str	r3, [sp, #12]
	ldrh	r3, [r6, #80]
	str	r3, [sp, #16]
	mov	r3, r7
	bl	printk
.L4432:
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
	mov	r1, r9
	mov	r2, #1
	bl	gc_search_src_blk
	cmp	r0, #0
	ble	.L4433
.L4434:
	mov	r3, #1
	b	.L4631
.L4433:
	ldr	r3, .L4635+32
	mov	r1, #3
	mov	r2, #1
	ldrb	r0, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	bgt	.L4434
	b	.L4536
.L4425:
	cmp	fp, #1
	bne	.L4435
	ldr	r3, [r9, #2804]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	beq	.L4436
	movw	r3, #2792
	ldrh	r3, [r9, r3]
	cmp	r3, #8
	bls	.L4436
	bl	gc_ink_check_sblk
.L4436:
	bl	gc_scan_static_data
	ldr	r3, [r4, #1092]
	ldrh	r3, [r3, #122]
	cmp	r3, #0
	beq	.L4437
.L4438:
	mov	r3, #1
	strb	r3, [r5, #-3115]
	b	.L4631
.L4437:
	bl	gc_static_wearleveling
	cmp	r0, #0
	bne	.L4438
	bl	gc_block_vpn_scan
	cmp	r8, #0
	beq	.L4439
	cmp	r7, r10
	ldr	r6, .L4635
	bcs	.L4440
	ldrh	r3, [r6, #-14]
	cmp	r3, r7
	bhi	.L4441
.L4440:
	ldrh	r2, [r6, #-14]
	add	r3, r7, r10
	cmp	r3, r2
	blt	.L4441
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldr	r3, .L4635
	ldrh	r3, [r3, #-12]
	cmp	r2, r3
	bcc	.L4439
.L4441:
	ldr	r0, .L4635+12
	mov	r1, #16
	mov	ip, #1
	strb	ip, [r5, #-3115]
	str	ip, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r9, .L4635+32
	cmp	r0, r2
	beq	.L4442
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldr	ip, [sp, #28]
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-176]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4442
	mov	r2, #0
	mov	r1, ip
	strb	r2, [r9, #-3115]
	str	r2, [r9, #-180]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4434
.L4442:
	ldr	r9, .L4635+32
	mov	r1, #0
	sub	r0, r9, #3104
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4443
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-176]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4443
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3115]
	str	r2, [r9, #-180]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4434
.L4443:
	ldr	r1, .L4635+32
	mov	r9, #0
	ldrh	r3, [r5, #-176]
	sub	r2, r1, #3088
	ldr	ip, .L4635+60
	add	r3, r3, #1
	ldrh	r2, [r2, #-8]
	uxth	r3, r3
	strh	r3, [r5, #-176]	@ movhi
	cmp	r3, r2, lsr #5
	movhi	r3, #4
	strhih	r3, [r1, #-176]	@ movhi
	ldr	r3, .L4635+32
	ldr	r1, [r5, #-180]
	sub	r0, r3, #3104
	add	r1, r1, #1
	str	r1, [r5, #-180]
	strh	r9, [r0, #-8]	@ movhi
	strh	r9, [r0, #-6]	@ movhi
	strh	r9, [r0, #-4]	@ movhi
	movw	r0, #1076
	ldrh	r0, [r4, r0]
	cmp	r1, r0, lsr #5
	bls	.L4445
	movw	r1, #2798
	ldrh	r1, [ip, r1]
	cmp	r1, r8
	bls	.L4445
	mov	r1, r9
	ldr	r0, .L4635+16
	str	ip, [sp, #36]
	str	r3, [sp, #32]
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r1, #65535
	cmp	r0, r1
	beq	.L4541
	ldr	ip, [sp, #36]
	mov	r0, r0, asl #1
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #28]
	ldr	r1, [ip, #1088]
	ldrh	r0, [r1, r0]
	ldrb	r1, [r3, #-3123]	@ zero_extendqisi2
	mul	r2, r1, r2
	cmp	r0, r2
	strle	r9, [r3, #-180]
	ldrleb	r0, [r3, #-3115]	@ zero_extendqisi2
	movle	r1, #2
	bgt	.L4541
	b	.L4624
.L4445:
	movw	r2, #2798
	ldr	r3, .L4635+60
	ldrh	r1, [r4, r2]
	ldrh	r2, [r6, #-12]
	cmp	r1, r2
	ldrcs	r3, .L4635+32
	movcs	r1, #2
	movcs	r2, #1
	ldrcsb	r0, [r3, #-3115]	@ zero_extendqisi2
	bcs	.L4623
.L4448:
	movw	r2, #2794
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L4449
	movw	r2, #2796
	ldrh	r3, [r3, r2]
	cmp	r3, #8
	bls	.L4450
.L4449:
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
	mov	r1, #1
.L4624:
	mov	r2, #4
.L4623:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4434
	b	.L4541
.L4450:
	ldr	r2, .L4635+56
	movw	r3, #2106
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	movne	r6, #16
	moveq	r6, #0
	b	.L4451
.L4439:
	ldr	r3, .L4635+32
	sub	r2, r3, #3088
	ldrh	r1, [r2, #-8]
	ldrh	r2, [r5, #-176]
	cmp	r2, r1, lsr #5
	movcs	r2, #4
	strcsh	r2, [r3, #-176]	@ movhi
	b	.L4541
.L4435:
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r3, r7
	bcs	.L4541
	ldr	r3, [r9, #1092]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4536
.L4541:
	mov	r6, #16
.L4451:
	ldr	r3, [r4, #2820]
	cmp	r3, #0
	beq	.L4453
	ldr	r3, .L4635+60
	cmp	r7, #15
	mov	r2, #0
	ldr	r0, .L4635+16
	mov	r1, #0
	str	r2, [r3, #2820]
	movhi	r3, #0
	movls	r3, #1
	cmp	r8, r2
	moveq	r3, #0
	cmp	r3, r2
	movne	r3, #1
	strb	r3, [r5, #-3115]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4456
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #8
	bhi	.L4456
	ldr	r3, .L4635+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldr	r3, .L4635+32
	ldrb	r2, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4434
.L4456:
	ldr	r9, .L4635+32
	mov	r1, #0
	sub	r0, r9, #3104
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4457
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4457
	ldr	r3, .L4635+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldrb	r2, [r9, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4434
.L4457:
	ldr	r0, .L4635+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4453
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4453
	ldr	r3, .L4635+60
	mov	r1, #1
	str	r1, [r3, #2820]
	ldr	r3, .L4635+32
	ldrb	r2, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4434
.L4453:
	ldr	r2, [r4, #1092]
	mov	r1, #1
	ldr	r9, .L4635+32
	ldr	r3, .L4635+60
	ldrh	ip, [r2, #124]
	strb	r1, [r5, #-3115]
	cmp	ip, #0
	strneb	r1, [r9, #-182]
	movne	r3, #0
	strneb	r3, [r9, #-3115]
	bne	.L4459
	movw	r2, #2808
	ldrh	r0, [r3, r2]
	cmp	r7, r0
	bcs	.L4460
	cmp	r8, #0
	beq	.L4461
	cmp	r8, #16
	bls	.L4462
	sub	r2, r9, #3072
	movw	r0, #2798
	ldrh	r0, [r3, r0]
	ldrh	r2, [r2, #-12]
	cmp	r0, r2
	bhi	.L4462
	mov	r0, r1
	mov	r2, #4
	str	ip, [sp, #32]
	str	r3, [sp, #28]
	bl	gc_search_src_blk
	uxth	r0, r0
	ldr	r3, [sp, #28]
	cmp	r0, #0
	ldr	ip, [sp, #32]
	beq	.L4628
	sub	r7, r9, #3088
	mov	r1, ip
	sub	r0, r7, #12
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4534
	ldr	r3, [sp, #28]
	mov	r1, r0, asl #1
	ldr	r2, [r9, #-180]
	ldr	ip, [r3, #1088]
	add	r2, r2, #1
	str	r2, [r9, #-180]
	ldrh	r1, [ip, r1]
	ldrh	ip, [r7, #-8]
	cmp	ip, r1
	bcs	.L4466
	movw	ip, #1076
	ldrh	ip, [r3, ip]
	cmp	r2, ip, lsr #4
	bls	.L4534
	movw	r2, #2810
	ldrh	r3, [r3, r2]
	cmp	r3, r1
	bls	.L4534
.L4466:
	mov	r1, #0
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	mov	r3, #1
	str	r3, [r4, #2820]
	mov	r3, #0
	str	r3, [r5, #-180]
	b	.L4534
.L4462:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r5, #-3115]	@ zero_extendqisi2
	bne	.L4534
	b	.L4627
.L4461:
	cmp	r7, #16
	strb	r8, [r9, #-3115]
	mov	r0, r8
	bhi	.L4627
	mov	r2, r1
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4534
.L4628:
	ldrb	r0, [r9, #-3115]	@ zero_extendqisi2
.L4627:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	b	.L4464
.L4460:
	cmp	fp, #1
	bne	.L4459
	cmp	r7, r0, asl #1
	bge	.L4469
	cmp	r10, r8, lsr #1
	bcs	.L4470
	sub	r9, r9, #3072
	movw	r1, #2798
	ldrh	r0, [r3, r1]
	ldrh	r1, [r9, #-12]
	cmp	r0, r1
	ldrcch	r1, [r9, #-14]
	movcc	r1, r1, lsr #2
	strcch	r1, [r3, r2]	@ movhi
	bcc	.L4459
.L4470:
	ldr	r0, .L4635+12
	mov	r1, #8
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r9, .L4635+32
	cmp	r0, r2
	beq	.L4471
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #3
	movhi	r3, #0
	movls	r3, #1
	cmp	r7, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4471
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3115]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4634
.L4471:
	ldr	r0, .L4635+16
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r9, .L4635+32
	cmp	r0, r3
	mov	fp, r0
	bne	.L4472
.L4477:
	cmp	r8, #1
	bhi	.L4473
	b	.L4474
.L4472:
	movw	r1, #1076
	ldr	r3, [r9, #-180]
	ldrh	r1, [r4, r1]
	mov	ip, r0, asl #1
	add	r3, r3, #1
	ldr	r2, .L4635+60
	str	r3, [r9, #-180]
	cmp	r3, r1, lsr #4
	bls	.L4475
	ldr	r3, [r2, #1088]
	mov	r1, #0
	str	r1, [r9, #-180]
	ldrh	lr, [r3, ip]
	movw	r3, #2810
	ldrh	r3, [r2, r3]
	cmp	lr, r3
	bcs	.L4475
	mov	r3, #1
	str	ip, [sp, #32]
	mov	r2, r3
	strb	r3, [r9, #-3115]
	str	r3, [sp, #28]
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	r3, [sp, #28]
	ldr	ip, [sp, #32]
	bne	.L4633
.L4475:
	ldr	r2, [r4, #1088]
	ldr	r9, .L4635+32
	ldr	r3, .L4635+60
	ldrh	r1, [r2, ip]
	sub	r2, r9, #3088
	ldrh	r2, [r2, #-8]
	cmp	r1, r2, lsr #1
	bhi	.L4476
	mov	r0, fp
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L4634:
	mov	r3, #1
.L4633:
	strb	r3, [r9, #-182]
	b	.L4459
.L4476:
	movw	r2, #2794
	movw	r0, #2796
	ldrh	r0, [r3, r0]
	sub	r9, r9, #3072
	ldrh	r2, [r3, r2]
	add	r2, r2, r0
	ldrh	r0, [r9, #-14]
	cmp	r2, r0, asl #1
	ble	.L4477
	movw	r2, #2810
	ldrh	r3, [r3, r2]
	cmp	r3, r1
	bcc	.L4474
	b	.L4477
.L4473:
	ldr	r9, .L4635+32
	cmp	r8, #16
	mov	r0, #1
	strb	r0, [r5, #-3115]
	sub	r8, r9, #3072
	bls	.L4478
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldrh	r3, [r8, #-12]
	cmp	r2, r3
	bhi	.L4478
	mov	r1, r0
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r9, #-3115]	@ zero_extendqisi2
	bne	.L4479
	b	.L4626
.L4478:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4479
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
.L4626:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
.L4479:
	ldrh	r3, [r8, #-14]
	cmp	r7, r10, lsr #1
	movw	r2, #2808
	movhi	r3, r3, lsr #2
	movls	r3, r3, lsr #1
	strh	r3, [r4, r2]	@ movhi
	b	.L4464
.L4474:
	cmp	r7, r10
	ldr	r8, .L4635
	bcs	.L4481
	sub	r0, r8, #20
	mov	r1, #4
	mov	r9, #0
	strb	r9, [r5, #-3115]
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4481
	ldr	r3, [r4, #1088]
	mov	r0, r0, asl #1
	ldrh	r1, [r8, #-24]
	ldr	r7, .L4635+60
	ldrh	r2, [r3, r0]
	ldrb	r3, [r8, #-51]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asr #1
	ldrgth	r3, [r8, #-14]
	movwgt	r2, #2808
	movgt	r3, r3, lsr #2
	strgth	r3, [r7, r2]	@ movhi
	bgt	.L4459
.L4483:
	mov	r2, #4
	mov	r0, r9
	mov	r1, #3
	bl	gc_search_src_blk
	ldrh	r3, [r8, #-14]
	movw	r2, #2808
	mov	r3, r3, lsr #1
	strh	r3, [r7, r2]	@ movhi
	uxth	r0, r0
	b	.L4464
.L4481:
	ldrh	r3, [r8, #-14]
	movw	r2, #2808
	mov	r3, r3, lsr #2
	strh	r3, [r4, r2]	@ movhi
	b	.L4459
.L4469:
	sub	r9, r9, #3072
	ldrh	r1, [r9, #-14]
	mov	r1, r1, lsr #2
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, .L4635+56
	movw	r3, #2106
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	moveq	r6, #0
	b	.L4459
.L4464:
	cmp	r0, #0
	beq	.L4459
.L4534:
	mov	r3, #1
	strb	r3, [r5, #-182]
	b	.L4459
.L4415:
	movw	r6, #2828
	movw	r3, #65535
	ldrh	r2, [r4, r6]
	cmp	r2, r3
	bne	.L4484
	bl	gc_get_src_blk
	ldr	r3, .L4635+60
	strh	r0, [r3, r6]	@ movhi
.L4484:
	movw	r3, #2828
	movw	r0, #65535
	ldrh	r1, [r4, r3]
	ldr	r2, .L4635+60
	cmp	r1, r0
	beq	.L4485
	movw	r0, #1076
	ldrh	r0, [r2, r0]
	cmp	r0, r1
	mvnls	r1, #0
	strlsh	r1, [r2, r3]	@ movhi
.L4485:
	movw	r3, #2828
	movw	r1, #65535
	ldrh	r3, [r4, r3]
	ldr	r2, .L4635+60
	cmp	r3, r1
	beq	.L4632
	ldr	r0, .L4635+56
	ldr	r1, [r2, #1080]
	ldrh	ip, [r0, #52]
	add	r1, r1, r3, asl #2
	cmp	ip, #0
	addne	r2, r2, #2880
	movne	r0, #0
	beq	.L4487
.L4488:
	uxth	lr, r0
	cmp	lr, ip
	bcs	.L4487
	ldrh	lr, [r2, #2]!
	add	r0, r0, #1
	cmp	lr, r3
	bne	.L4488
.L4492:
	movw	r3, #2828
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	b	.L4536
.L4487:
	ldrb	r2, [r1, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r2, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r2, #224
	movne	r2, r1
	orreq	r2, r1, #1
	cmp	r2, #0
	beq	.L4490
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4492
	ldr	r1, .L4635+64
	movw	r2, #3306
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
	b	.L4492
.L4490:
	mov	r3, #2
	b	.L4631
.L4416:
	bl	gc_scan_src_blk
	cmn	r0, #1
	moveq	r3, #3
	beq	.L4631
	movw	r3, #2828
	ldr	r2, .L4635+56
	ldrh	r3, [r4, r3]
	movw	r1, #65535
	cmp	r3, r1
	mov	r6, r2
	beq	.L4434
	ldrh	r1, [r2, #20]
	cmp	r1, #0
	movne	r3, #4
	strneb	r3, [r5, #-182]
	movne	r3, #0
	strneh	r3, [r2, #22]	@ movhi
	bne	.L4536
.L4494:
	mov	r2, #1
	strb	r2, [r5, #-182]
	ldr	r2, .L4635+60
	mov	r3, r3, asl #1
	ldr	r2, [r2, #1088]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4495
	ldr	r1, .L4635+64
	movw	r2, #3336
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
.L4495:
	movw	r5, #2828
	ldr	r7, .L4635+56
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #1088]
	mov	r5, #0
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r6, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strlsh	r3, [r7, #26]	@ movhi
	bls	.L4492
	strh	r5, [r7, #26]	@ movhi
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, r5
	bl	ftl_info_flush
	b	.L4492
.L4417:
	ldr	r6, .L4635+56
.L4594:
	bl	gc_scan_src_blk_one_page
	ldr	r3, .L4635+20
	ldrh	r1, [r6, #2]
	ldr	r0, .L4635+60
	ldrh	r3, [r3]
	ldr	r2, .L4635+32
	cmp	r1, r3
	ldr	r5, .L4635+56
	bcs	.L4498
	cmp	r7, #7
	bls	.L4594
	b	.L4536
.L4498:
	ldrh	r3, [r5, #20]
	ldrh	r1, [r5]
	cmp	r3, #0
	beq	.L4499
	ldr	r0, [r0, #1088]
	mov	ip, #4
	strb	ip, [r2, #-182]
	mov	r2, #0
	strh	r2, [r5, #22]	@ movhi
	mov	r2, r1, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	beq	.L4500
	ldr	r0, .L4635+4
	ldr	r0, [r0]
	tst	r0, #1024
	beq	.L4500
	ldr	r0, .L4635+24
	bl	printk
.L4500:
	movw	r3, #2828
	ldr	r2, [r4, #1088]
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L4501
	ldr	r1, .L4635+64
	movw	r2, #3379
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
.L4501:
	movw	r3, #2828
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	b	.L4536
.L4499:
	mov	r3, #1
	strb	r3, [r2, #-182]
	ldr	r3, .L4635+4
	ldr	r6, [r0, #1080]
	ldr	r3, [r3]
	add	r6, r6, r1, asl #2
	tst	r3, #256
	beq	.L4502
	ldrb	r2, [r6, #2]	@ zero_extendqisi2
	ldr	r0, .L4635+28
	mov	r2, r2, lsr #5
	bl	printk
.L4502:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4503
	ldr	r1, .L4635+64
	movw	r2, #3389
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
.L4503:
	movw	r7, #2828
	ldr	r6, .L4635+56
	ldrh	r0, [r4, r7]
	bl	ftl_free_sblk
	mvn	r3, #0
	strh	r3, [r4, r7]	@ movhi
	ldrh	r3, [r5, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strlsh	r3, [r6, #26]	@ movhi
	movhi	r3, #0
	strhih	r3, [r6, #26]	@ movhi
	bls	.L4536
	b	.L4630
.L4636:
	.align	2
.L4635:
	.word	.LANCHOR3-3072
	.word	.LANCHOR2
	.word	.LC300
	.word	.LANCHOR3-3092
	.word	.LANCHOR3-3100
	.word	.LANCHOR3-3096
	.word	.LC301
	.word	.LC302
	.word	.LANCHOR3
	.word	.LANCHOR0+5010
	.word	.LANCHOR0+4096
	.word	1145785929
	.word	.LANCHOR3-3120
	.word	.LANCHOR3-3088
	.word	.LANCHOR0+2828
	.word	.LANCHOR0
	.word	.LANCHOR1+2752
	.word	.LC0
.L4418:
	cmp	fp, #0
	bne	.L4505
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bcc	.L4536
.L4505:
	ldrh	r2, [r6, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4506
	ldrb	r8, [r5, #-3115]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L4506
	ldr	r9, .L4635+56
	bl	ftl_flush
	movw	r3, #2180
	mov	r1, #5
	ldrh	r0, [r9, r3]
	cmp	r0, #0
	movne	r0, r8
	bl	zftl_gc_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r7, r0
	beq	.L4509
	ldr	r8, [r4, #1080]
	add	r8, r8, r0, asl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L4510
	ldr	r1, .L4635+64
	movw	r2, #3423
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
.L4510:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	and	r3, r3, #15
	orr	r3, r3, #176
	strb	r3, [r8, #2]
.L4535:
	mov	r0, r7
	mov	r1, #1
	bl	ftl_erase_sblk
	mov	r3, #5
	add	r1, r6, #96
	strb	r3, [r6, #84]
	mov	r0, r7
	ldr	r10, .L4635+52
	bl	ftl_get_blk_list_in_sblk
	mov	r8, #0
	strh	r7, [r6, #80]	@ movhi
	mov	r1, #255
	ldrh	r3, [r10, #-8]
	ldrh	r2, [r10, #-26]
	strh	r8, [r6, #82]	@ movhi
	strb	r8, [r6, #85]
	strh	r8, [r6, #90]	@ movhi
	uxtb	r0, r0
	strb	r0, [r6, #89]
	smulbb	r0, r3, r0
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	mul	r2, r3, r2
	strh	r0, [r6, #86]	@ movhi
	ldr	r0, [r5, #-172]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r2, [r10, #-26]
	mov	r1, #255
	ldr	r0, [r5, #-168]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r3, [r10, #-26]
	ldrb	r2, [r5, #-3123]	@ zero_extendqisi2
	mov	r1, #255
	ldr	r0, [r5, #-3120]
	mvn	r5, #0
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, [r4, #1092]
	strh	r5, [r3, #128]	@ movhi
	strh	r5, [r3, #130]	@ movhi
	str	r7, [r3, #132]
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2804]
	mov	r0, r8
	strh	r8, [r9, #52]	@ movhi
	strh	r7, [r3, #126]	@ movhi
	movw	r3, #2102
	strh	r8, [r9, r3]	@ movhi
	movw	r3, #2104
	strh	r8, [r9, r3]	@ movhi
	movw	r3, #2182
	strh	r8, [r9, r3]	@ movhi
	ldr	r3, .L4635+40
	str	r5, [r3, #916]
	bl	ftl_info_flush
	b	.L4536
.L4506:
	cmp	fp, #1
	ldr	r10, .L4635+56
	ldr	r9, .L4635+36
	movne	r8, #1
	moveq	r8, #4
	cmp	r7, #15
	addls	r8, r8, #4
.L4513:
	sub	r8, r8, #1
	uxtb	r8, r8
	cmp	r8, #255
	beq	.L4536
	bl	gc_do_copy_back
	ldrb	r3, [r5, #-3115]	@ zero_extendqisi2
	ldr	fp, .L4635+32
	cmp	r3, #0
	ldr	r7, .L4635+56
	bne	.L4514
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #3
	bhi	.L4515
	bl	ftl_write_commit
.L4515:
	ldrh	r2, [r10, #22]
	ldrh	r3, [r10, #20]
	cmp	r2, r3
	bcc	.L4513
	mov	r3, #1
	strb	r3, [r5, #-182]
	bl	ftl_write_commit
	bl	ftl_flush
	ldr	r3, .L4635+56
	ldr	r2, .L4635+60
	ldrh	r3, [r3]
	ldr	r2, [r2, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4517
	ldr	r1, .L4635+64
	movw	r2, #3507
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
.L4517:
	movw	r3, #2828
	ldr	r1, [r4, #1088]
	ldrh	r0, [r4, r3]
	mov	r2, r0, asl #1
	ldrh	ip, [r1, r2]
	cmp	ip, #0
	bne	.L4518
	strh	ip, [r1, r2]	@ movhi
	ldr	r2, .L4635+60
	ldrh	r0, [r2, r3]
	bl	ftl_free_sblk
	b	.L4492
.L4518:
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	b	.L4492
.L4514:
	ldrh	r3, [r9]
	cmp	r3, #0
	beq	.L4519
	ldr	r3, .L4635+36
	mov	r2, #0
	ldr	r8, .L4635+40
	strh	r2, [r3]	@ movhi
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	r0, [r8, #916]
	cmn	r0, #1
	beq	.L4520
	ldrb	r3, [fp, #-2536]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4521
	ldrb	r3, [fp, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4522
.L4521:
	ldr	r3, [r4, #2804]
	ldr	r9, .L4635+60
	ldr	r2, [r3, #156]
	ldr	r3, .L4635+44
	cmp	r2, r3
	bne	.L4522
	ldr	r10, .L4635+48
	ldrb	r3, [r9, #1189]	@ zero_extendqisi2
	ldrb	r1, [r5, #-3130]	@ zero_extendqisi2
	ldrh	r2, [r10, #-12]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r2, r0, lsr r2
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r6, [r9, #1080]
	mov	ip, r0, asl #2
	add	r1, r6, ip
	ldrb	r3, [r1, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L4520
	movw	r2, #2788
	ldrh	lr, [r10, #34]
	ldrh	r2, [r9, r2]
	add	r2, r2, #8
	cmp	r2, lr
	bge	.L4520
	bfc	r3, #3, #2
	movw	r2, #1084
	strb	r3, [r1, #2]
	ldr	r3, [r6, r0, asl #2]
	ldrh	r2, [r9, r2]
	ubfx	r3, r3, #11, #8
	ldrh	r1, [r6, ip]
	mul	r2, r2, r3
	ubfx	lr, r1, #0, #11
	mov	r3, r3, lsr #3
	add	r2, r2, r2, asl #1
	add	r2, lr, r2, asr #2
	bfi	r1, r2, #0, #11
	strh	r1, [r6, ip]	@ movhi
	ldr	r2, [r6, r0, asl #2]
	bfi	r2, r3, #11, #8
	str	r2, [r6, r0, asl #2]
	b	.L4520
.L4522:
	bl	ftl_mask_bad_block
.L4520:
	ldr	r3, [r4, #1092]
	mov	r6, #0
	str	r6, [r8, #916]
	strh	r6, [r7, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r0, [r4, #2836]
	ldr	r2, [r4, #1092]
	mvn	r3, #0
	ldr	r1, [r4, #2804]
	cmp	r0, r6
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	beq	.L4523
	bl	zbuf_free
.L4523:
	str	r6, [r4, #2836]
	bl	flt_sys_flush
	ldr	r1, .L4635+64
	movw	r2, #3567
	ldr	r0, .L4635+68
	strb	r6, [r5, #-182]
	bl	printk
	bl	dump_stack
	b	.L4536
.L4519:
	ldrh	r3, [r6, #86]
	ldrh	r2, [r10, #22]
	cmp	r3, #1
	ldrh	r3, [r10, #20]
	bls	.L4524
	cmp	r2, r3
	bcc	.L4513
	mov	r3, #1
	strb	r3, [fp, #-182]
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L4536
.L4524:
	cmp	r2, r3
	mov	r1, #5
	strb	r1, [fp, #-182]
	bcc	.L4525
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L4525:
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldrh	r2, [r6, #80]
	ldr	r3, [r4, #1092]
	strh	r2, [r3, #128]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r1, .L4635+52
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	mov	r3, #0
	strh	r3, [r7, #12]	@ movhi
	ldrh	r3, [r1, #-8]
	cmp	r0, #0
	ldr	r2, .L4635+56
	ldrneh	r1, [r1, #-26]
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r5, #-3124]	@ zero_extendqisi2
	strneh	r1, [r2, #14]	@ movhi
	movne	r1, #1
	strh	r3, [r7, #16]	@ movhi
	strneh	r1, [r2, #16]	@ movhi
	cmp	r3, #2
	bne	.L4528
	ldrh	r3, [r7, #14]
	mov	r3, r3, asl #1
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r5, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L4635+56
	moveq	r2, #1
	streqh	r2, [r3, #16]	@ movhi
.L4528:
	mov	r3, #0
	strh	r3, [r7, #18]	@ movhi
	b	.L4536
.L4537:
	ldr	r8, .L4635+56
	mov	r6, #0
.L4419:
	bl	gc_check_data_one_wl
	subs	r10, r0, #0
	beq	.L4531
	ldr	r3, .L4635+56
	mov	r6, #0
	strh	r6, [r3, #52]	@ movhi
	ldr	r3, [r4, #1092]
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r2, [r4, #1092]
	ldr	r1, [r4, #2804]
	mvn	r3, #0
	ldr	r0, [r4, #2836]
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	bl	zbuf_free
	str	r6, [r4, #2836]
	strb	r6, [r5, #-182]
.L4630:
	bl	flt_sys_flush
	b	.L4536
.L4531:
	ldrh	r2, [r8, #12]
	ldrh	r3, [r8, #14]
	ldr	r9, .L4635+60
	cmp	r2, r3
	bcc	.L4532
	ldr	r0, [r9, #2836]
	mov	r3, #6
	strb	r3, [r5, #-182]
	bl	zbuf_free
	str	r10, [r9, #2836]
	b	.L4536
.L4532:
	cmp	r7, #15
	bls	.L4419
	cmp	fp, #1
	bne	.L4536
	add	r6, r6, #1
	uxtb	r6, r6
	cmp	r6, #4
	bls	.L4419
	b	.L4536
.L4420:
	bl	gc_update_l2p_map_new
	mvn	r7, #0
	bl	gc_free_src_blk
	bl	ftl_flush
	bl	pm_flush
	strh	r7, [r6, #80]	@ movhi
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2804]
	mov	r0, #0
	strh	r7, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L4632:
	mov	r3, #0
.L4631:
	strb	r3, [r5, #-182]
.L4536:
	mov	r0, #16
	b	.L4621
.L4459:
	mov	r0, r6
	b	.L4621
.L4509:
	ldr	r1, .L4635+64
	movw	r2, #3430
	ldr	r0, .L4635+68
	bl	printk
	bl	dump_stack
	b	.L4535
.L4621:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	zftl_do_gc, .-zftl_do_gc
	.align	2
	.global	zftl_init
	.type	zftl_init, %function
zftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L4721
	mvn	r3, #0
	ldr	r4, .L4721+4
	mov	r6, #0
	ldr	r1, .L4721+8
	ldr	r0, .L4721+12
	strb	r3, [r4, #2824]
	strb	r3, [r5, #-2530]
	strb	r3, [r5, #-96]
	str	r3, [r5, #556]
	strb	r6, [r4, #2800]
	strb	r6, [r5, #-2529]
	strb	r6, [r4, #2801]
	bl	printk
	ldr	r8, .L4721+16
	sub	r2, r5, #3104
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	ldrb	fp, [r8, #13]	@ zero_extendqisi2
	strb	fp, [r4, #2772]
	mov	r9, fp, asl #9
	uxth	r9, r9
	str	r3, [sp, #4]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	strb	r3, [r5, #-3124]
	ldrh	r3, [r8, #14]
	strh	r3, [r2, #-10]	@ movhi
	mov	r0, r3
	str	r3, [sp, #8]
	bl	__aeabi_idiv
	sub	r1, r5, #3088
	ldrh	r10, [r8, #18]
	ldrh	ip, [r4, #2]
	strh	r0, [r1, #-8]	@ movhi
	mov	r2, r0
	ldrb	r1, [r4, #1101]	@ zero_extendqisi2
	ldrb	r0, [r8, #17]	@ zero_extendqisi2
	ldr	r3, [sp, #8]
	strb	r1, [r5, #-3064]
	smulbb	r1, r0, r1
	strb	r0, [r5, #-3130]
	mov	r0, #1
	uxtb	r7, r1
	movw	r1, #1076
	strh	r10, [r4, r1]	@ movhi
	movw	r1, #542
	strh	r9, [r5, r1]	@ movhi
	sub	r1, r5, #3056
	strb	r7, [r5, #-3123]
	strh	ip, [r1, #-10]	@ movhi
.L4638:
	cmp	r0, ip
	uxth	r1, r6
	add	r6, r6, #1
	movls	r0, r0, asl #1
	bls	.L4638
.L4719:
	ldr	r0, .L4721+20
	sub	r1, r1, #1
	strh	r1, [r0, #-12]	@ movhi
	mov	r0, #0
	mul	r1, r3, fp
	mul	r6, r10, r1
	str	r1, [sp, #8]
	mov	r1, #1
	mov	ip, r6, lsr #21
.L4640:
	cmp	r1, ip
	uxth	lr, r0
	add	r0, r0, #1
	movls	r1, r1, asl #1
	bls	.L4640
.L4720:
	ldr	r1, .L4721+24
	sub	lr, lr, #1
	mov	r0, #1
	str	r3, [sp, #20]
	mul	r6, r7, r6
	uxth	lr, lr
	mul	r1, r1, r7
	mov	lr, r0, asl lr
	str	r2, [sp, #12]
	mov	r10, r10, lsr #4
	str	r6, [r4, #2780]
	ldr	r6, .L4721
	mul	r8, lr, r1
	mov	r1, fp
	add	fp, r6, #564
	add	ip, r8, #24576
	str	ip, [r6, #560]
	str	ip, [sp, #16]
	mov	r0, ip
	str	r8, [r4, #1032]
	bl	__aeabi_uidiv
	sub	r1, r9, #1
	str	r0, [r4, #2784]
	add	r0, r1, r0, asl #2
	mov	r1, r9
	bl	__aeabi_uidiv
	ldr	r2, [sp, #12]
	strh	r0, [fp]	@ movhi
	uxth	r0, r0
	mul	r1, r7, r2
	mov	r0, r0, asl #4
	bl	__aeabi_idiv
	sub	r2, r6, #3072
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #16]
	strh	r0, [r6, #-152]	@ movhi
	mul	r1, r7, r3
	mov	r0, ip
	str	r2, [sp, #12]
	sub	r1, r1, #1
	bl	__aeabi_uidiv
	cmp	r10, #79
	movw	ip, #1084
	movls	r1, #80
	ldr	r2, [sp, #12]
	add	r0, r0, #8
	ldr	r3, [sp, #20]
	strh	r10, [r2, #-14]	@ movhi
	strlsh	r1, [r2, #-14]	@ movhi
	ldr	r1, .L4721
	strh	r0, [r2, #-12]	@ movhi
	mov	r0, #2000
	sub	r2, r1, #2528
	strh	r0, [r2, #-4]	@ movhi
	mov	r0, #50
	strh	r0, [r2, #-6]	@ movhi
	mov	r0, #256
	strh	r0, [r6, #-156]	@ movhi
	mov	r0, #48
	strh	r0, [r6, #-154]	@ movhi
	mov	r0, #32
	strh	r0, [r4, ip]	@ movhi
	ldr	ip, [sp, #4]
	cmp	ip, #2
	mov	ip, r2
	beq	.L4643
	ldrb	lr, [r1, #-3116]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L4644
.L4643:
	mov	r1, #150
	strh	r1, [ip, #-6]	@ movhi
	mov	r1, #64
	strh	r1, [r6, #-154]	@ movhi
	movw	r1, #1084
	mov	lr, #12
	strh	lr, [r4, r1]	@ movhi
	ldrb	lr, [r4]	@ zero_extendqisi2
	ldr	r2, .L4721
	cmp	lr, #0
	sub	r0, r2, #2528
	bne	.L4645
	ldr	lr, .L4721+4
	mov	r10, #4
	strh	r10, [lr, r1]	@ movhi
	mov	r1, #600
	strh	r1, [r0, #-4]	@ movhi
	mov	r1, #128
	strh	r1, [r2, #-156]	@ movhi
.L4645:
	ldrb	r2, [r4, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, #200
	strneh	r2, [ip, #-6]	@ movhi
	movne	r2, #2000
	strneh	r2, [ip, #-4]	@ movhi
	b	.L4647
.L4644:
	ldr	ip, .L4721+4
	ldrb	ip, [ip, #1196]	@ zero_extendqisi2
	cmp	ip, #0
	strneh	r0, [r2, #-6]	@ movhi
	strneh	r0, [r1, #-154]	@ movhi
	movne	ip, #1200
	strneh	ip, [r2, #-4]	@ movhi
.L4647:
	mul	r3, r7, r3
	ldr	r7, .L4721+16
	mov	r2, #0
	str	r2, [r4, #2820]
	mov	r2, #1
	strb	r2, [r6, #545]
	cmp	r9, r3, asl #2
	ldrlt	r3, .L4721
	movlt	r2, #2
	strltb	r2, [r3, #545]
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4650
	ldr	r0, .L4721+28
	mov	r1, r8
	bl	printk
.L4650:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4651
	ldr	r0, .L4721+32
	ldr	r1, [r4, #2780]
	bl	printk
.L4651:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4652
	ldr	r0, .L4721+36
	ldr	r1, [r4, #2784]
	bl	printk
.L4652:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4653
	ldr	r0, .L4721+40
	ldr	r1, [r6, #560]
	bl	printk
.L4653:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4654
	ldr	r0, .L4721+44
	ldrh	r1, [fp]
	bl	printk
.L4654:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4655
	movw	r3, #542
	ldr	r0, .L4721+48
	ldrh	r1, [r6, r3]
	bl	printk
.L4655:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4656
	ldr	r0, .L4721+52
	ldrh	r1, [r6, #-152]
	bl	printk
.L4656:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4657
	ldr	r3, .L4721+56
	ldr	r0, .L4721+60
	ldrh	r1, [r3, #-14]
	bl	printk
.L4657:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4658
	ldr	r3, .L4721+56
	ldr	r0, .L4721+64
	ldrh	r1, [r3, #-12]
	bl	printk
.L4658:
	bl	zbuf_init
	mov	r0, #16384
	bl	ftl_malloc
	movw	r9, #1076
	ldr	r8, .L4721+68
	str	r0, [r5, #204]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #212]
	mov	r0, #16384
	bl	ftl_malloc
	ldrh	r3, [r4, r9]
	str	r0, [r5, #568]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	str	r0, [r4, #1036]
	ldrh	r0, [r8, #-8]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r1, [r8, #-8]
	ldr	r2, [r5, #212]
	mul	r1, r3, r1
	str	r2, [r5, #-2548]
	add	r3, r2, r1, asl #3
	str	r3, [r4, #1088]
	str	r0, [r5, #-2544]
	ldrh	r5, [r4, r9]
	ldr	r0, [r7]
	mov	r5, r5, lsr #1
	tst	r0, #4096
	add	r1, r5, r1, asl #1
	add	r1, r2, r1, asl #2
	str	r1, [r4, #1092]
	beq	.L4659
	ldr	r0, .L4721+72
	bl	printk
.L4659:
	movw	r3, #1076
	ldrh	r5, [r8, #-8]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r6, #-3123]	@ zero_extendqisi2
	ldrh	fp, [fp]
	mul	r5, r3, r5
	ldr	r3, [r7]
	mov	fp, fp, asl #2
	tst	r3, #4096
	add	fp, fp, r2, asl #2
	add	fp, fp, #704
	add	r5, r2, r5, asl #2
	mov	r5, r5, asl #1
	add	r5, r5, #632
	beq	.L4660
	ldr	ip, .L4721
	movw	r3, #542
	ldr	r0, .L4721+76
	mov	r1, r5
	mov	r2, fp
	ldrh	r3, [ip, r3]
	bl	printk
.L4660:
	movw	r3, #542
	ldrh	r3, [r6, r3]
	cmp	fp, r3
	cmpls	r5, r3
	movhi	fp, #1
	movls	fp, #0
	bls	.L4661
.L4717:
	b	.L4717
.L4661:
	bl	sblk_init
	bl	gc_init
	bl	ftl_info_blk_init
	cmn	r0, #1
	beq	.L4663
	bl	ftl_ext_info_init
	mov	r0, #1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	r0, [r4, #1092]
	mov	r1, fp
	add	r0, r0, #16
	bl	ftl_open_sblk_recovery
	ldr	r1, [r4, #1092]
	add	r0, r1, #48
	add	r1, r1, #16
	bl	ftl_open_sblk_recovery
	ldr	r2, [r4, #2804]
	ldr	r0, [r4, #1092]
	ldr	r3, [r2, #8]
	add	r0, r0, #16
	add	r3, r3, #16
	str	r3, [r2, #8]
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #1092]
	add	r0, r0, #48
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #1092]
	add	r0, r0, #80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	r0, #1
	bl	ftl_total_vpn_update
	ldr	r3, .L4721
	ldrb	r3, [r3, #209]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, .L4721+4
	ldrne	r2, [r3, #2804]
	ldrne	r3, [r2, #68]
	addne	r3, r3, #1
	strne	r3, [r2, #68]
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	ldr	r3, [r4, #1092]
	ldr	r2, .L4721+4
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4669
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #7
	ble	.L4669
.L4667:
	mov	r0, #0
	b	.L4663
.L4669:
	ldr	r6, .L4721+80
	mov	r5, #16384
.L4700:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	ldr	r3, [r4, #1092]
	ldrh	r2, [r3, #124]
	cmp	r2, #0
	bne	.L4666
	ldrh	r2, [r3, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4666
	ldr	r2, .L4721+84
	ldrh	r3, [r6]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4667
.L4666:
	subs	r5, r5, #1
	bne	.L4700
	b	.L4667
.L4663:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4722:
	.align	2
.L4721:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC2
	.word	.LC1
	.word	.LANCHOR2
	.word	.LANCHOR3-3120
	.word	1892352
	.word	.LC303
	.word	.LC304
	.word	.LC305
	.word	.LC306
	.word	.LC307
	.word	.LC308
	.word	.LC309
	.word	.LANCHOR3-3072
	.word	.LC310
	.word	.LC311
	.word	.LANCHOR3-3088
	.word	.LC312
	.word	.LC313
	.word	.LANCHOR0+2788
	.word	.LANCHOR0+2792
	.fnend
	.size	zftl_init, .-zftl_init
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r0, #68
	ldr	r3, .L4739
	mov	r5, #0
	ldr	r4, .L4739+4
	ldr	r3, [r3]
	str	r5, [r4, #-140]
	strb	r5, [r4, #572]
	str	r3, [r4, #-144]
	bl	ftl_malloc
	cmp	r0, r5
	str	r0, [r4, #-136]
	bne	.L4724
.L4726:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4724:
	mov	r0, #2048
	bl	ftl_malloc
	add	r1, r4, #580
	str	r5, [r4, #-132]
	str	r5, [r4, #580]
	str	r0, [r4, #576]
	sub	r0, r4, #132
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #-132]
	cmp	r3, #0
	beq	.L4726
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r4, #576]
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	r3, [r4, #-136]
	ldr	r2, .L4739+8
	ldr	r0, [r4, #-132]
	str	r2, [r3, #40]
	ldr	r2, .L4739+12
	str	r2, [r3, #32]
	ldr	r2, .L4739+16
	str	r2, [r3, #44]
	ldr	r2, .L4739+20
	str	r2, [r3, #48]
	ldr	r2, .L4739+24
	str	r2, [r3, #36]
	ldr	r2, .L4739+28
	str	r2, [r3, #56]
	ldr	r2, .L4739+32
	str	r2, [r3, #20]
	ldr	r2, .L4739+36
	str	r2, [r3, #24]
	ldr	r2, .L4739+40
	str	r2, [r3, #12]
	ldr	r2, .L4739+44
	str	r2, [r3, #16]
	ldr	r2, .L4739+48
	str	r2, [r3, #4]
	ldr	r2, .L4739+52
	str	r2, [r3, #8]
	ldr	r2, .L4739+56
	str	r2, [r3, #60]
	ldr	r2, .L4739+60
	str	r2, [r3, #64]
	ldr	r2, .L4739+64
	str	r2, [r3, #52]
	ldr	r2, .L4739+68
	str	r2, [r3, #28]
	bl	nand_flash_init
	subs	r7, r0, #0
	bne	.L4727
	bl	zftl_init
	mov	r5, r0
	bl	zftl_proc_debug_init
	mov	r3, #1
	strb	r3, [r4, #572]
	b	.L4728
.L4727:
	ldr	r3, [r4, #-136]
	mov	r6, r4
	ldr	r2, .L4739+72
	ldr	r0, [r4, #-132]
	ldr	r4, .L4739+76
	str	r2, [r3, #40]
	ldr	r2, .L4739+80
	str	r2, [r3, #32]
	ldr	r2, .L4739+84
	str	r2, [r3, #44]
	ldr	r2, .L4739+88
	str	r2, [r3, #48]
	ldr	r2, .L4739+92
	str	r2, [r3, #36]
	ldr	r2, .L4739+96
	str	r2, [r3, #56]
	ldr	r2, .L4739+100
	str	r2, [r3, #20]
	ldr	r2, .L4739+104
	str	r2, [r3, #24]
	ldr	r2, .L4739+108
	str	r2, [r3, #12]
	ldr	r2, .L4739+112
	str	r2, [r3, #16]
	ldr	r2, .L4739+116
	str	r2, [r3, #4]
	ldr	r2, .L4739+120
	str	r2, [r3, #8]
	ldr	r2, .L4739+124
	str	r2, [r3, #60]
	ldr	r2, .L4739+128
	str	r2, [r3, #64]
	ldr	r2, .L4739+132
	str	r2, [r3, #52]
	ldr	r2, .L4739+136
	str	r2, [r3, #28]
	bl	FlashInit
	cmn	r7, #2
	mov	r5, r0
	bne	.L4729
	ldr	r1, .L4739+140
	mov	r2, #32
	add	r0, r4, #4
	bl	ftl_memcpy
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrh	r3, [r4, #14]
	strh	r3, [r4, #30]	@ movhi
.L4729:
	ldr	r3, .L4739+144
	ldr	r2, [r3]
	ldr	r3, .L4739+148
	cmp	r2, r3
	ldr	r2, .L4739+152
	bne	.L4730
	ldr	r3, .L4739+156
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4731
.L4730:
	mov	r3, #0
	strb	r3, [r2, #1102]
	strb	r3, [r2]
.L4731:
	ldr	r1, .L4739+160
	mov	r0, #1
	ldrh	ip, [r4, #14]
	mov	r3, #0
	str	r3, [r6, #-108]
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1100]
	ldr	r1, .L4739+164
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1172]
.L4732:
	cmp	r0, ip
	uxth	r1, r3
	add	r3, r3, #1
	movls	r0, r0, asl #1
	bls	.L4732
.L4738:
	ldr	r3, .L4739+168
	sub	r1, r1, #1
	strh	r1, [r3, #-12]	@ movhi
	mov	r3, #0
	cmp	r5, r3
	strb	r3, [r2, #1135]
	bne	.L4728
	bl	FtlInit
	mov	r5, r0
.L4728:
	mov	r1, r5
	ldr	r0, .L4739+172
	bl	printk
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4740:
	.align	2
.L4739:
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
	.word	.LANCHOR3-3120
	.word	.LC314
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	zftl_write
	.type	zftl_write, %function
zftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r3
	ldr	r3, .L4776
	.pad #28
	sub	sp, sp, #28
	mov	r5, r0
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4742
	ldr	r3, [r9]
	mov	r1, r5
	ldr	r0, .L4776+4
	mov	r2, r4
	str	r3, [sp]
	mov	r3, r6
	bl	printk
.L4742:
	cmp	r5, #0
	ldreq	r3, .L4776+8
	moveq	r5, #24576
	ldreq	r3, [r3, #1032]
	beq	.L4744
.L4743:
	cmp	r5, #3
	bhi	.L4762
	mov	r5, r5, asl #13
	mov	r3, #8192
.L4744:
	cmp	r6, r3
	cmpls	r4, r3
	movcs	fp, #1
	movcc	fp, #0
	bcs	.L4762
	add	r1, r4, r6
	cmp	r1, r3
	bhi	.L4762
	add	r4, r5, r4
	ldr	r5, .L4776+8
	mov	r0, r4
	ldrb	r7, [r5, #2772]	@ zero_extendqisi2
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	mov	r10, r0
	sub	r0, r6, #1
	add	r0, r0, r4
	mov	r8, r10
	bl	__aeabi_uidiv
	add	r3, r4, r6
	rsb	r7, r10, r0
	str	r0, [sp, #8]
	add	r7, r7, #1
	str	fp, [sp, #12]
	str	r3, [sp, #16]
.L4746:
	cmp	r7, #0
	beq	.L4774
	ldrb	r3, [r5, #2800]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4747
	ldr	r3, .L4776+8
	ldrb	r3, [r3, #2774]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4747
	bl	ftl_write_commit
.L4747:
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	bne	.L4748
	bl	ftl_write_commit
	b	.L4746
.L4748:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	rsb	r0, r10, r8
	clz	r0, r0
	ldrb	r2, [r5, #2772]	@ zero_extendqisi2
	strb	r3, [fp, #41]
	mov	r0, r0, lsr #5
	ldr	r3, [sp, #8]
	strb	r2, [fp, #40]
	rsb	r1, r3, r8
	clz	r1, r1
	mov	r1, r1, lsr #5
	orrs	r3, r1, r0
	beq	.L4751
	cmp	r0, #0
	beq	.L4752
	mov	r1, r2
	mov	r0, r4
	str	r2, [sp, #20]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #20]
	uxtb	r1, r1
	strb	r1, [fp, #41]
	rsb	r2, r1, r2
	uxtb	r2, r2
	cmp	r2, r6
	strhib	r6, [fp, #40]
	bhi	.L4751
	b	.L4773
.L4752:
	cmp	r1, #0
	beq	.L4751
	smulbb	r2, r8, r2
	ldr	r3, [sp, #16]
	rsb	r2, r2, r3
.L4773:
	strb	r2, [fp, #40]
.L4751:
	ldrb	r0, [fp, #41]	@ zero_extendqisi2
	sub	r7, r7, #1
	ldrb	r2, [fp, #40]	@ zero_extendqisi2
	ldr	r1, [fp, #4]
	mov	r2, r2, asl #9
	add	r0, r1, r0, asl #9
	mov	r1, r9
	bl	ftl_memcpy
	ldr	r1, [r5, #2804]
	str	r8, [fp, #20]
	add	r8, r8, #1
	ldr	r2, [r1, #8]
	add	r0, r2, #1
	str	r0, [r1, #8]
	str	r2, [fp, #16]
	mov	r0, fp
	bl	ftl_write_buf
	ldrb	r3, [fp, #40]	@ zero_extendqisi2
	add	r9, r9, r3, asl #9
	b	.L4746
.L4774:
	bl	ftl_write_commit
	mov	r0, r7
	mov	r1, #1
	bl	zftl_do_gc
	ldr	r3, [r5, #1092]
	ldr	r2, .L4776+8
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4756
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #11
	bgt	.L4757
.L4756:
	mov	r0, #0
	mov	r1, #1
	bl	zftl_do_gc
.L4757:
	ldr	r4, .L4776+12
.L4758:
	ldr	r2, .L4776+16
	ldrh	r3, [r4]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4775
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	b	.L4758
.L4775:
	bl	timer_get_time
	ldr	r3, .L4776+20
	str	r0, [r3, #552]
	mov	r0, #0
	b	.L4745
.L4762:
	mvn	r0, #0
.L4745:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4777:
	.align	2
.L4776:
	.word	.LANCHOR2
	.word	.LC315
	.word	.LANCHOR0
	.word	.LANCHOR0+2788
	.word	.LANCHOR0+2792
	.word	.LANCHOR3
	.fnend
	.size	zftl_write, .-zftl_write
	.align	2
	.global	zftl_vendor_write
	.type	zftl_vendor_write, %function
zftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r1
	mov	r3, r2
	add	r1, r0, #512
	mov	r2, ip
	mov	r0, #2
	b	zftl_write
	.fnend
	.size	zftl_vendor_write, .-zftl_vendor_write
	.align	2
	.global	zftl_sys_write
	.type	zftl_sys_write, %function
zftl_sys_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	str	lr, [sp, #-4]!
	.save {lr}
	mov	ip, r1
	mov	lr, r0
	mov	r3, r2
	mov	r1, lr
	mov	r0, #2
	mov	r2, ip
	ldr	lr, [sp], #4
	b	zftl_write
	.fnend
	.size	zftl_sys_write, .-zftl_sys_write
	.align	2
	.global	zftl_discard
	.type	zftl_discard, %function
zftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r1
	ldr	r6, .L4832
	.pad #36
	sub	sp, sp, #36
	ldr	r5, [r6, #1032]
	cmp	r1, r5
	cmpls	r0, r5
	movcs	r2, #1
	movcc	r2, #0
	bcs	.L4803
	add	r3, r0, r1
	cmp	r3, r5
	bhi	.L4803
	ldr	r3, .L4832+4
	add	r7, r0, #24576
	ldr	fp, .L4832+8
	ldr	r0, [r3]
	ldr	r5, [fp, #584]
	mov	r9, fp
	tst	r0, #4096
	str	r3, [sp, #16]
	add	r1, r1, r5
	str	r1, [fp, #584]
	beq	.L4783
	str	r2, [sp]
	mov	r3, r4
	ldr	r0, .L4832+12
	mov	r2, r7
	bl	printk
.L4783:
	ldr	r3, [r6, #2804]
	ldr	r10, [r3, #8]
	add	r2, r10, #1
	str	r2, [r3, #8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	r8, [r6, #2772]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, r8
	bl	__aeabi_uidiv
	mls	fp, r8, r0, r7
	mov	r5, r0
	cmp	fp, #0
	beq	.L4784
	rsb	r8, fp, r8
	cmp	r8, r4
	movcs	r8, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4785
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4785:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	uxth	r3, r8
	str	r3, [sp, #12]
	beq	.L4787
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4787
	ldr	r0, [r3, #4]
	mov	r1, #0
	str	r5, [r3, #20]
	strb	fp, [r3, #41]
	add	r0, r0, fp, asl #9
	strb	r8, [r3, #40]
	str	r10, [r3, #16]
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #20]
	mov	r0, r3
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r6, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4787:
	ldr	r3, [sp, #12]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L4784:
	cmp	r4, #0
	beq	.L4789
	bl	ftl_flush
.L4789:
	ldr	r8, .L4832
	mvn	r3, #0
	ldr	fp, .L4832+16
	str	r3, [sp, #28]
.L4790:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L4831
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	beq	.L4791
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4793
	ldrb	r2, [r8, #2772]	@ zero_extendqisi2
	mov	r1, #0
	str	r5, [r3, #20]
	strb	r1, [r3, #41]
	strb	r2, [r3, #40]
	mov	r2, r2, asl #9
	str	r10, [r3, #16]
	ldr	r0, [r3, #4]
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r3, [sp, #12]
	mov	r0, r3
	bl	ftl_write_buf
	bl	ftl_write_commit
	b	.L4830
.L4791:
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4793
	add	r1, sp, #28
	mov	r2, #1
	mov	r0, r5
	bl	pm_log2phys
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r9, #-3130]	@ zero_extendqisi2
	rsb	r3, r0, r3
	ldr	r2, [sp, #24]
	mov	r2, r2, lsr r0
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L4830:
	ldr	r2, [r8, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4793:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	add	r5, r5, #1
	rsb	r4, r3, r4
	b	.L4790
.L4831:
	cmp	r4, #0
	beq	.L4797
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4798
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4798:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4797
	mov	r0, #0
	bl	buf_alloc
	subs	r8, r0, #0
	beq	.L4797
	mov	r3, #0
	strb	r3, [r8, #41]
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	str	r5, [r8, #20]
	cmp	r4, r3
	strb	r4, [r8, #40]
	str	r10, [r8, #16]
	bcc	.L4800
	ldr	r1, .L4832+20
	movw	r2, #1495
	ldr	r0, .L4832+24
	bl	printk
	bl	dump_stack
.L4800:
	mov	r2, r4, asl #9
	mov	r1, #0
	ldr	r0, [r8, #4]
	bl	ftl_memset
	mov	r0, r8
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r6, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4797:
	ldr	r1, [r9, #584]
	cmp	r1, #8192
	bls	.L4804
	ldr	r3, [sp, #16]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4801
	mov	r3, #0
	ldr	r0, .L4832+12
	str	r3, [sp]
	mov	r2, r7
	mov	r3, r4
	bl	printk
.L4801:
	mov	r4, #0
	str	r4, [r9, #584]
	bl	flt_sys_flush
	mov	r3, #1
	str	r3, [r6, #2820]
	b	.L4804
.L4803:
	mvn	r0, #0
	b	.L4782
.L4804:
	mov	r0, #0
.L4782:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4833:
	.align	2
.L4832:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC316
	.word	.LANCHOR3-3132
	.word	.LANCHOR1+2764
	.word	.LC0
	.fnend
	.size	zftl_discard, .-zftl_discard
	.align	2
	.global	dump_pm_blk
	.type	dump_pm_blk, %function
dump_pm_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	ip, #698
	ldr	r4, .L4840
	mov	r2, #4
	ldr	r0, .L4840+4
	mov	r6, #0
	ldr	r5, .L4840+8
	movw	r7, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4840+12
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r5, #-152]
	bl	rknand_print_hex
.L4835:
	ldrh	r2, [r5, #-152]
	uxth	r3, r6
	cmp	r2, r3
	bls	.L4839
	add	r3, r3, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r7
	beq	.L4836
	mov	r1, #0
	bl	ftl_sblk_dump
.L4836:
	add	r6, r6, #1
	b	.L4835
.L4839:
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4841:
	.align	2
.L4840:
	.word	.LANCHOR0
	.word	.LC268
	.word	.LANCHOR3
	.word	.LC269
	.fnend
	.size	dump_pm_blk, .-dump_pm_blk
	.align	2
	.global	id_block_prog_msb_ff_data
	.type	id_block_prog_msb_ff_data, %function
id_block_prog_msb_ff_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r2
	ldr	r5, .L4849
	.pad #20
	sub	sp, sp, #20
	ldrb	r2, [r5, #1102]	@ zero_extendqisi2
	ldr	r3, [r5, #1096]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	bne	.L4842
	sub	r2, r3, #5
	mov	r8, r0
	cmp	r3, #68
	cmpne	r2, #2
	mov	r9, r1
	bls	.L4844
	sub	r3, r3, #19
	tst	r3, #239
	bne	.L4842
.L4844:
	ldr	fp, .L4849
	ldr	r6, .L4849+4
	ldr	r7, .L4849+8
.L4846:
	ldr	r3, [r5, #1096]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L4842
	mov	ip, r4, asl #1
	add	r10, r4, r9
	ldr	r0, .L4849+12
	mov	r1, r4
	ldrh	r3, [r7, ip]
	mov	r2, r10
	str	ip, [sp, #12]
	bl	printk
	movw	r3, #65535
	ldr	ip, [sp, #12]
	ldrh	r2, [r7, ip]
	cmp	r2, r3
	bne	.L4842
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r6, #-128]
	add	r4, r4, #1
	bl	ftl_memset
	ldr	r3, [fp, #1096]
	ldr	r2, [r6, #-128]
	mov	r0, r8
	mov	r1, r10
	uxth	r4, r4
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r2
	bl	flash_prog_page
	b	.L4846
.L4842:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4850:
	.align	2
.L4849:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-2268
	.word	.LC317
	.fnend
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 136
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L4930
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #156
	sub	sp, sp, #156
	ldr	r3, [ip, #1096]
	ldr	r5, [r1]
	ldr	r8, .L4930+4
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	r9, [r3, #10]
	ldr	r3, .L4930+8
	ldr	fp, .L4930+12
	ldr	r10, .L4930+16
	ldrh	r6, [r3, #30]
	cmp	r5, r8
	cmpne	r5, fp
	ldrb	r3, [ip]	@ zero_extendqisi2
	movne	r7, #1
	moveq	r7, #0
	cmp	r5, r10
	moveq	r5, #0
	andne	r5, r7, #1
	str	r3, [sp, #60]
	cmp	r5, #0
	ldrh	r3, [sp, #60]
	str	r3, [sp, #44]
	ldrb	r3, [ip, #1102]	@ zero_extendqisi2
	str	r3, [sp, #68]
	bne	.L4928
	cmp	r0, #15
	bls	.L4928
	mov	r7, r0
	mov	r0, #256000
	str	ip, [sp, #20]
	mov	r5, r2
	str	r1, [sp, #36]
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #24]
	beq	.L4928
	ldr	r3, .L4930+20
	ldr	ip, [sp, #20]
	ldrb	r3, [r3, #572]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4927
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #0
	ldrb	r3, [ip, #1102]	@ zero_extendqisi2
	clzne	r3, r3
	movne	r3, r3, lsr #5
	bne	.L4927
	cmp	r3, #3
	streq	r3, [sp, #44]
	moveq	r3, #2
	beq	.L4927
	cmp	r3, #2
	ldr	r3, [sp, #44]
	moveq	r3, #2
	str	r3, [sp, #44]
	movne	r3, #0
	moveq	r3, #3
.L4927:
	str	r3, [sp, #20]
	ldr	r3, [sp, #36]
	ldr	r3, [r3]
	cmp	r3, r8
	cmpne	r3, fp
	movne	fp, #1
	moveq	fp, #0
	cmp	r3, r10
	moveq	r10, #0
	andne	r10, fp, #1
	cmp	r10, #0
	beq	.L4858
	ldr	r0, [sp, #24]
	bl	ftl_free
.L4928:
	mvn	r0, #0
	b	.L4853
.L4858:
	mov	r0, r10
	add	r7, r7, #508
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4930
	mul	r9, r9, r4
	add	r7, r7, #3
	ldrb	r2, [r3, #1102]	@ zero_extendqisi2
	sub	r0, r9, #1
	strb	r2, [r3]
	mov	r3, r7, lsr #9
	cmp	r3, #8
	mov	r1, r9
	movcs	fp, r3
	movcc	fp, #8
	add	r0, r0, fp
	bl	__aeabi_uidiv
	ldr	ip, .L4930+24
	movw	r2, #63871
	ldr	r3, [sp, #36]
	str	r0, [sp, #72]
	movw	r0, #4094
	add	r3, r3, #254976
	add	r3, r3, #512
.L4864:
	ldr	r1, [r3, #-4]!
	cmp	r1, #0
	bne	.L4859
	cmp	r10, #0
	streq	ip, [r3, #512]
	sub	r2, r2, #1
	ldrne	r1, [sp, #36]
	ldrne	r1, [r1, r10, asl #2]
	add	r10, r10, #1
	strne	r1, [r3, #512]
	cmp	r10, r0
	movhi	r10, #0
	cmp	r2, #4096
	bne	.L4864
	b	.L4863
.L4859:
	ldr	r0, .L4930+28
	bl	printk
.L4863:
	mul	r3, r6, r4
	ldr	r0, .L4930+32
	mov	r1, r5
	mov	r2, #4
	ldr	r4, .L4930
	str	r3, [sp, #56]
	mov	r3, #5
	bl	rknand_print_hex
	ldr	r0, .L4930+36
	mov	r1, fp
	mov	r2, fp
	bl	printk
	sub	r3, r5, #4
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #40]
	str	r3, [sp, #28]
.L4889:
	ldr	r3, .L4930+20
	ldrb	r3, [r3, #572]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4865
	ldr	r3, [sp, #48]
	ldr	r1, [sp, #72]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #56]
	mul	r6, r3, r2
	ldr	r3, [sp, #28]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L4866
	ldr	r3, [sp, #48]
	ldr	r3, [r3]
	add	r3, r3, #1
	cmp	r2, r3
	bne	.L4866
	b	.L4867
.L4865:
	ldr	r3, [r4, #1176]
	ldr	r2, [sp, #28]
	add	r3, r3, r2
	ldrb	r6, [r3, #32]	@ zero_extendqisi2
	cmp	r6, #255
	beq	.L4867
	ldr	r3, [sp, #56]
	mul	r6, r3, r6
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4866
	ldr	r3, [sp, #20]
	and	r5, r2, #1
	cmp	r3, #3
	orreq	r5, r5, #1
	cmp	r5, #0
	ldrneb	r3, [sp, #44]	@ zero_extendqisi2
	strneb	r3, [r4]
	strneb	r3, [r4, #1102]
	bne	.L4866
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	strb	r5, [r4]
	strb	r5, [r4, #1102]
.L4866:
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #24]
	mov	r7, #0
	bl	ftl_memset
	ldr	r3, [r4, #1096]
	mov	r0, r6
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	ldrh	r8, [r3, #10]
	ldr	r3, .L4930+8
	mov	r1, r10
	mul	r8, r8, r10
	ldrh	r5, [r3, #30]
	ldrb	r3, [r4, #1168]	@ zero_extendqisi2
	strb	r7, [r4, #1168]
	mul	r5, r5, r10
	str	r3, [sp, #76]
	bl	__aeabi_uidiv
	mov	r1, r0
	mov	r0, r7
	bl	flash_erase_block
	cmp	fp, r8
	movls	r0, #1
	bls	.L4869
	mov	r0, r7
	add	r1, r6, r5
	bl	flash_erase_block
	mov	r0, #2
.L4869:
	ldr	r3, .L4930
	ldr	r2, [r3, #1096]
	ldrh	r3, [r2, #10]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	mov	r3, r3, asl #2
	mul	r0, r0, r3
	bl	__aeabi_idiv
	mov	r1, r5
	str	r0, [sp, #80]
	mov	r0, r6
	bl	__aeabi_uidivmod
	rsb	r3, r1, r6
	str	r1, [sp, #64]
	cmp	r6, r3
	str	r3, [sp, #52]
	bne	.L4896
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L4896
	ldr	r3, .L4930+20
	mov	r1, #0
	mov	r2, #1024
	ldr	r5, [r3, #-104]
	mov	r0, r5
	bl	ftl_memset
	ldr	r2, .L4930+40
	mov	r3, #12
	stmia	r5, {r2, r3}
	mov	r3, #0
	strb	r3, [r5, #16]
	str	r3, [r5, #12]
	ldrb	r3, [r4, #1102]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r4, #1096]
	ldrneb	r3, [r3, #29]	@ zero_extendqisi2
	strneb	r3, [r5, #16]
	mov	r3, #4
	strb	r3, [r5, #17]
	ldr	r3, [r4, #1096]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	bl	__aeabi_idiv
	cmp	r10, #8
	mov	r3, #0
	mov	r1, #12
	strb	r3, [r5, #20]
	movhi	r2, #70
	movls	r2, #16
	strh	r3, [r5, #22]	@ movhi
	strh	r0, [r5, #18]	@ movhi
	add	r0, r5, #12
	strb	r2, [r5, #21]
	bl	js_hash
	sub	r3, fp, #4
	str	r0, [r5, #8]
	str	r3, [sp, #32]
	b	.L4870
.L4896:
	mov	r5, #0
	str	fp, [sp, #32]
.L4870:
	ldr	r8, [sp, #36]
	mov	r9, #0
.L4873:
	ldr	r3, [sp, #80]
	cmp	r9, r3
	bcs	.L4929
	ldr	r3, [sp, #64]
	ldrb	r2, [r4, #1102]	@ zero_extendqisi2
	add	r7, r9, r3
	ubfx	r7, r7, #2, #16
	cmp	r2, #0
	add	r0, r7, #1
	mov	r1, r0, asl #1
	add	r3, r4, r1
	ldrh	r3, [r3, #4]
	beq	.L4875
	ldrb	ip, [r4, #1]	@ zero_extendqisi2
	cmp	ip, #0
	moveq	r3, r0
	movne	r3, r1
.L4875:
	ldrb	r1, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, .L4930
	cmp	r1, #9
	movw	r1, #61424
	str	r1, [sp, #92]
	mov	r1, r7, asl #1
	add	r0, r0, r1
	subne	r3, r3, #1
	ldrh	r0, [r0, #4]
	movne	r3, r3, asl #2
	cmp	r2, #0
	str	r3, [sp, #88]
	moveq	r7, r0
	beq	.L4879
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r7, r1
.L4879:
	cmp	r5, #0
	mul	r0, r10, r7
	bne	.L4880
	ldr	r2, [sp, #52]
	str	r8, [sp]
	add	ip, r0, r2
	ldr	r2, [r8]
	ldr	r0, .L4930+44
	mov	r1, ip
	str	ip, [sp, #84]
	stmib	sp, {r2, r3}
	movw	r3, #61424
	mov	r2, r9
	str	r3, [sp, #12]
	mov	r3, fp
	bl	printk
	mov	r1, r8
	ldr	ip, [sp, #84]
	add	r2, sp, #88
	mov	r0, ip
	bl	fw_flash_page_prog.constprop.30
	ldrb	r3, [r4, #1102]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4881
	mov	r1, r10
	ldr	r0, [sp, #52]
	bl	__aeabi_uidiv
	add	r2, r7, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, r5
	bl	id_block_prog_msb_ff_data
.L4881:
	ldr	r3, [sp, #32]
	add	r8, r8, #2048
	add	r3, r3, #16
	cmp	r9, r3
	bcc	.L4882
	ldr	r3, [sp, #32]
	add	r3, r3, #20
	cmp	r9, r3
	ldrcc	r3, [sp, #36]
	addcc	r8, r3, #2048
	b	.L4882
.L4880:
	ldr	r3, [sp, #52]
	mov	r1, r5
	add	r2, sp, #88
	add	r0, r0, r3
	bl	fw_flash_page_prog.constprop.30
.L4882:
	add	r9, r9, #4
	mov	r5, #0
	uxth	r9, r9
	b	.L4873
.L4929:
	ldr	r3, .L4930
	mov	r0, r6
	ldrb	r2, [sp, #76]	@ zero_extendqisi2
	ldr	r1, [sp, #32]
	strb	r2, [r3, #1168]
	ldr	r2, [sp, #24]
	bl	id_block_read_data.constprop.31
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4884
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #1102]
.L4884:
	ldr	r3, [sp, #32]
	mov	r5, #0
	mov	r8, r3, asl #7
.L4887:
	ldr	r3, [sp, #24]
	ldr	r2, [r3, r5, asl #2]
	ldr	r3, [sp, #36]
	ldr	r3, [r3, r5, asl #2]
	cmp	r2, r3
	beq	.L4885
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	bl	ftl_memset
	mov	r0, #0
	mov	r1, r6
	bl	flash_erase_block
	cmp	r5, r8
	bcs	.L4886
	b	.L4867
.L4885:
	add	r5, r5, #1
	cmp	r5, r8
	bne	.L4887
.L4886:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	cmp	r3, #5
	bhi	.L4888
.L4867:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
	ldr	r3, [sp, #48]
	add	r3, r3, #4
	str	r3, [sp, #48]
	ldr	r3, [sp, #28]
	cmp	r3, #4
	bne	.L4889
.L4888:
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4930
	mov	r0, #0
	ldrb	r2, [sp, #60]	@ zero_extendqisi2
	strb	r2, [r3]
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldrneb	r2, [sp, #68]	@ zero_extendqisi2
	strneb	r2, [r3, #1102]
	mov	r2, #2
	strb	r2, [r3, #1192]
	bl	zftl_flash_enter_slc_mode
	ldr	r0, [sp, #24]
	bl	ftl_free
	ldr	r3, [sp, #40]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
.L4853:
	add	sp, sp, #156
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4931:
	.align	2
.L4930:
	.word	.LANCHOR0
	.word	1397640018
	.word	.LANCHOR2
	.word	-52655045
	.word	1397967698
	.word	.LANCHOR3
	.word	1314014539
	.word	.LC318
	.word	.LC319
	.word	.LC320
	.word	1179535694
	.word	.LC321
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r4, r0
	.pad #52
	sub	sp, sp, #52
	mov	r6, r1
	mov	r8, r2
	ldr	r5, .L4958
	bne	.L4933
	ldr	r3, [r2]
	ldr	r1, .L4958+4
	ldr	r2, .L4958+8
	cmp	r3, r1
	cmpne	r3, r2
	ldr	r1, .L4958+12
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, r1
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4933
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r5, #588]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r5, #592]
	bl	ftl_memset
	str	r4, [r5, #596]
.L4933:
	str	r6, [sp]
	mov	r3, r4
	ldr	r0, .L4958+16
	ldr	r1, [r5, #592]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r5, #588]	@ zero_extendqisi2
	ldr	r9, .L4958
	cmp	r3, #0
	beq	.L4932
	ldr	r7, [r9, #592]
	cmp	r7, #0
	beq	.L4932
	sub	r0, r4, #64
	cmp	r0, #500
	bcs	.L4935
	rsb	r2, r4, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4936
.L4935:
	cmp	r4, #564
	bcs	.L4944
.L4936:
	ldr	r3, [r5, #596]
	ldr	r8, .L4958
	cmp	r3, r4
	beq	.L4942
	mov	r0, r7
	mov	r9, #0
	strb	r9, [r8, #588]
	bl	ftl_free
	str	r9, [r8, #592]
.L4942:
	add	r4, r4, r6
	str	r4, [r5, #596]
	b	.L4932
.L4944:
	ldr	r3, .L4958+20
	ldr	r0, [r9, #596]
	ldr	r3, [r3, #1096]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L4945
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
	b	.L4938
.L4945:
	mov	r3, #0
.L4937:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L4937
.L4938:
	mov	r3, #61952
.L4941:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #2048
	movne	r0, r3, asl #2
	bne	.L4940
.L4939:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L4941
	mov	r0, r0, asl #9
.L4940:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r5, #592]
	mov	r7, #0
	strb	r7, [r5, #588]
	bl	ftl_free
	str	r7, [r5, #592]
	b	.L4942
.L4932:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L4959:
	.align	2
.L4958:
	.word	.LANCHOR3
	.word	-52655045
	.word	1397640018
	.word	1397967698
	.word	.LC322
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r2
	sub	r2, r1, #64
	mov	r4, r1
	cmp	r2, #1984
	mov	r5, r3
	mov	r7, r0
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	movne	r2, #0
	cmp	r2, #0
	beq	.L4961
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L4961:
	ldr	r3, .L4966
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	ldr	r3, [r3, #-136]
	ldr	ip, [r3, #24]
	mov	r3, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	bx	ip
.L4967:
	.align	2
.L4966:
	.word	.LANCHOR3
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L5025
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #524
	sub	sp, sp, #524
	mov	r4, r1
	mov	r5, r2
	beq	.L4970
	bhi	.L4971
	sub	r3, r3, #2080
	sub	r3, r3, #6
	cmp	r1, r3
	beq	.L4972
	bhi	.L4973
	sub	r3, r3, #238
	cmp	r1, r3
	beq	.L4974
	add	r3, r3, #237
	cmp	r1, r3
	beq	.L4975
	b	.L5005
.L4973:
	ldr	r3, .L5025+4
	cmp	r1, r3
	beq	.L4976
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4977
	sub	r3, r3, #124
	cmp	r1, r3
	bne	.L5005
	b	.L5023
.L4971:
	ldr	r3, .L5025+8
	cmp	r1, r3
	mov	r6, r3
	beq	.L4979
	bhi	.L4980
	sub	r3, r3, #2512
	sub	r3, r3, #14
	cmp	r1, r3
	beq	.L4970
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L4970
	b	.L5005
.L4980:
	ldr	r3, .L5025+12
	cmp	r1, r3
	beq	.L4979
	bcc	.L4981
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4981
	b	.L5005
.L4975:
	ldr	r0, .L5025+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4982
.L4988:
	ldr	r0, .L5025+20
	bl	printk
	b	.L5020
.L4982:
	ldr	r2, [sp]
	ldr	r3, .L5025+24
	cmp	r2, r3
	beq	.L4983
.L4985:
	mvn	r4, #0
	b	.L4984
.L4983:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4985
	ldr	r4, .L5025+28
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #600]
	bl	memcpy
	ldr	r2, [r4, #604]
	ldr	r3, .L5025+32
	cmp	r2, r3
	beq	.L4986
	mov	r1, #0
	add	r0, sp, #64
	mov	r2, #128
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L4986:
	mov	r1, #0
	add	r0, sp, #256
	mov	r2, #256
	str	r1, [sp, #16]
	bl	memset
.L5015:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L5020
	b	.L5019
.L4972:
	ldr	r0, .L5025+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+24
	cmp	r2, r3
	bne	.L4985
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4985
	ldr	r2, .L5025+28
	ldr	r3, .L5025+32
	ldr	r1, [r2, #604]
	cmp	r1, r3
	mvnne	r0, #1
	bne	.L4969
	ldr	r3, [sp, #12]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L4969
	ldr	r4, [r2, #600]
	add	r1, sp, #64
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L5018
.L4977:
	ldr	r0, .L5025+40
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+44
	cmp	r2, r3
	bne	.L4985
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4985
	ldr	r5, .L5025+28
	ldr	r3, [r5, #608]
	cmp	r3, #0
	bne	.L4989
.L4992:
	mov	r0, #0
	b	.L4969
.L4989:
	ldr	r3, [r5, #612]
	ldr	r2, .L5025+48
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4990
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L5025+28
	ldr	r3, [r3, #612]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4990:
	ldr	r1, [r5, #612]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #600]
	ldr	r2, .L5025+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #600]
	ldrne	r3, .L5025+28
	movne	r2, #504
	add	r0, r6, #64
	ldrne	r3, [r3, #600]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	str	r4, [r5, #608]
	str	r4, [r5, #604]
	b	.L4984
.L4976:
	ldr	r0, .L5025+52
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+56
	cmp	r2, r3
	bne	.L4985
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4985
	ldr	r5, .L5025+28
	ldr	r3, [r5, #608]
	cmp	r3, #1
	beq	.L4992
	ldr	r2, [r5, #612]
	ldr	r3, .L5025+48
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L4993
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #612]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4993:
	ldr	r1, [r5, #612]
	mov	r3, #1
	mov	r0, #0
	mov	r4, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #600]
	ldr	r2, .L5025+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #600]
	ldrne	r3, .L5025+28
	movne	r1, #504
	movne	r2, #0
	add	r0, r6, #64
	ldrne	r3, [r3, #600]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #608]
	b	.L4984
.L5023:
	ldr	r0, .L5025+60
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+64
	cmp	r2, r3
	bne	.L4985
	ldr	r2, [sp, #4]
	cmp	r2, #512
	addls	r0, sp, #8
	ldrls	r1, .L5025+68
	bls	.L5021
	b	.L4985
.L4970:
	ldr	r0, .L5025+72
	cmp	r4, r0
	mov	r7, r0
	ldreq	r0, .L5025+76
	beq	.L5017
	ldr	r3, .L5025+80
	cmp	r4, r3
	ldreq	r0, .L5025+84
	ldrne	r0, .L5025+88
.L5017:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+92
	cmp	r2, r3
	bne	.L5020
	ldr	r3, .L5025+80
	ldr	r6, .L5025+28
	cmp	r4, r3
	bne	.L4999
	ldr	r3, [r6, #600]
	mov	r0, r5
	mov	r1, sp
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4969
	b	.L5020
.L4999:
	ldr	r3, [r6, #1128]
	cmp	r3, #10
	bhi	.L5020
	ldr	r1, [r6, #600]
	ldr	r2, [sp, #4]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L5000
	ldr	r0, .L5025+96
	mov	r1, r2
	bl	printk
	ldr	r3, [r6, #1128]
	add	r3, r3, #1
	str	r3, [r6, #1128]
.L5020:
	mvn	r0, #13
	b	.L4969
.L5000:
	cmp	r4, r7
	str	r3, [r6, #1128]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L5019
	b	.L5024
.L4979:
	ldr	r0, .L5025+100
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+104
	cmp	r2, r3
	bne	.L4985
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4985
	ldr	r3, .L5025+28
	cmp	r4, r6
	add	r0, sp, #8
	ldreq	r1, [r3, #1132]
	ldrne	r1, [r3, #1136]
	add	r1, r1, #8
.L5021:
	bl	memcpy
	b	.L5015
.L4981:
	ldr	r0, .L5025+108
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4988
	ldr	r2, [sp]
	ldr	r3, .L5025+104
	cmp	r2, r3
	bne	.L4985
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4985
	ldr	r3, .L5025+112
	add	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L5025+28
	bne	.L5004
	mov	r1, sp
	ldr	r0, [r4, #1132]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #1132]
	b	.L5018
.L5004:
	mov	r1, sp
	ldr	r0, [r4, #1136]
	bl	memcpy
	ldr	r1, [r4, #1136]
	mov	r0, #3
.L5018:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L4984
.L4974:
	bl	rknand_dev_flush
.L5019:
	mov	r4, #0
	b	.L4984
.L5024:
	mvn	r4, #1
.L4984:
	ldr	r0, .L5025+116
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L4969
.L5005:
	mvn	r0, #21
.L4969:
	add	sp, sp, #524
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L5026:
	.align	2
.L5025:
	.word	1074031656
	.word	1074029694
	.word	1074034192
	.word	1074034194
	.word	.LC323
	.word	.LC324
	.word	1263358532
	.word	.LANCHOR3
	.word	-1067903959
	.word	.LC325
	.word	.LC326
	.word	1112753220
	.word	1146313043
	.word	.LC327
	.word	1112755781
	.word	.LC328
	.word	1094995539
	.word	.LANCHOR3+616
	.word	1074031666
	.word	.LC329
	.word	1074031676
	.word	.LC330
	.word	.LC331
	.word	1280262987
	.word	.LC332
	.word	.LC333
	.word	1145980246
	.word	.LC334
	.word	1074034193
	.word	.LC335
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L5036
	mov	r2, #512
	mov	r5, #0
	add	r0, r4, #616
	ldr	r1, [r4, #576]
	str	r3, [r4, #596]
	add	r3, r1, #512
	str	r3, [r4, #600]
	str	r1, [r4, #612]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #1132]
	strb	r5, [r4, #588]
	str	r5, [r4, #592]
	str	r5, [r4, #1140]
	str	r1, [r4, #1136]
	bl	ftl_memcpy
	ldr	r0, [r4, #612]
	str	r5, [r4, #604]
	str	r5, [r4, #1128]
	ldr	r6, [r0, #508]
	ldr	r3, [r0, #16]
	cmp	r6, r5
	str	r3, [r4, #608]
	beq	.L5029
	mov	r1, #508
	bl	js_hash
	cmp	r6, r0
	beq	.L5029
	ldr	r3, [r4, #612]
	ldr	r0, .L5036+4
	str	r5, [r4, #608]
	str	r5, [r3, #16]
	bl	printk
.L5029:
	ldr	r3, [r4, #608]
	mov	r0, #2
	ldr	r1, [r4, #1132]
	cmp	r3, #0
	ldrne	r2, .L5036+8
	ldrne	r3, .L5036
	strne	r2, [r3, #604]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1136]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r4, r5, r6, lr}
	b	rknand_sys_storage_init
.L5037:
	.align	2
.L5036:
	.word	.LANCHOR3
	.word	.LC336
	.word	-1067903959
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
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
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	bl	ftl_malloc
	ldr	r6, .L5050
	cmp	r0, #0
	str	r0, [r6, #1144]
	beq	.L5045
	ldr	r9, .L5050+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L5043:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #1144]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L5041
	ldr	r3, [r10, #1144]
	ldr	r0, .L5050+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #1144]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L5042
	add	r2, r5, #61440
	ldr	r3, [r5, #4]
	ldr	r1, [r2, #4092]
	cmp	r4, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r1, r3
	movne	r2, #0
	cmp	r2, #0
	movne	r8, r7
	movne	r4, r3
.L5042:
	cmp	r7, #1
	movne	r7, #1
	bne	.L5043
.L5049:
	cmp	r4, #0
	beq	.L5044
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L5041
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L5044:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L5050+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L5050+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L5041:
	ldr	r0, [r6, #1144]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #1144]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L5045:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L5051:
	.align	2
.L5050:
	.word	.LANCHOR3
	.word	1380668996
	.word	.LC337
	.word	-1032
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L5059
	ldr	ip, [r3, #1144]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L5057
.L5054:
	cmp	r3, r4
	bcs	.L5057
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L5055
	ldrh	r3, [lr, #20]
	mov	r0, r1
	ldrh	r1, [lr, #18]
	mov	r4, r2
	cmp	r2, r3
	add	r1, r1, #1024
	movcs	r4, r3
	add	r1, ip, r1
	mov	r2, r4
	bl	memcpy
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L5055:
	add	r3, r3, #1
	b	.L5054
.L5057:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L5060:
	.align	2
.L5059:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r2
	ldr	r2, .L5084
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #1144]
	cmp	r4, #0
	beq	.L5076
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L5063:
	cmp	r6, r2
	bcs	.L5082
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L5064
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L5065
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L5076
.L5066:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L5083
	ldrh	r10, [r5, #20]
	add	r0, fp, r8
	ldrh	r2, [r5, #16]
	add	r6, r6, #1
	ldrh	r1, [r5, #18]
	strh	r10, [r5, #12]	@ movhi
	add	r10, r10, #63
	bic	r10, r10, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r8, [r5, #10]	@ movhi
	add	r1, fp, r1
	mov	r2, r10
	str	r3, [sp, #20]
	str	ip, [sp, #16]
	bl	memcpy
	add	r8, r8, r10
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #16]
	b	.L5066
.L5083:
	add	r6, r4, r6, asl #3
	uxth	r8, r8
	add	r0, fp, r8
	mov	r1, r3
	strh	r8, [r6, #18]	@ movhi
	mov	r2, r9
	strh	ip, [r6, #16]	@ movhi
	uxth	r7, r7
	strh	r9, [r6, #20]	@ movhi
	add	r8, r8, r7
	bl	memcpy
	ldrh	r5, [r4, #14]
	strh	r8, [r4, #12]	@ movhi
	ldr	r3, [sp, #8]
	add	r5, r3, r5
	rsb	r7, r7, r5
	strh	r7, [r4, #14]	@ movhi
	b	.L5081
.L5065:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L5081
.L5064:
	add	r6, r6, #1
	b	.L5063
.L5082:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L5076
	add	r2, r4, r2, asl #3
	uxth	r7, r7
	rsb	r1, r7, r1
	strh	ip, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r9, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r7, r0
	strh	r1, [r4, #14]	@ movhi
	mov	r1, r3
	strh	r0, [r4, #12]	@ movhi
	ldrh	r0, [r2, #18]
	mov	r2, r9
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
.L5081:
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
	mov	r0, r3, asl #7
	bl	FlashBootVendorWrite
	mov	r0, #0
	b	.L5062
.L5076:
	mvn	r0, #0
.L5062:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L5085:
	.align	2
.L5084:
	.word	.LANCHOR3
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L5087
	ldr	r3, .L5103
	cmp	r6, r3
	beq	.L5089
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L5090
	b	.L5101
.L5089:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5101
	ldr	r2, [r4]
	ldr	r3, .L5103+4
	cmp	r2, r3
	beq	.L5092
.L5093:
	mvn	r5, #0
	b	.L5088
.L5092:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L5093
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L5088
.L5090:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5101
	ldr	r2, [r4]
	ldr	r3, .L5103+4
	cmp	r2, r3
	bne	.L5093
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L5093
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5101
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L5088
.L5101:
	mvn	r5, #13
.L5088:
	mov	r0, r4
	bl	kfree
.L5087:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L5104:
	.align	2
.L5103:
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
	.align	3
.LANCHOR1 = . + 0
	.type	__func__.39677, %object
	.size	__func__.39677, 18
__func__.39677:
	.ascii	"_list_remove_node\000"
	.space	2
	.type	__func__.39702, %object
	.size	__func__.39702, 23
__func__.39702:
	.ascii	"_list_update_data_list\000"
	.space	1
	.type	__func__.20210, %object
	.size	__func__.20210, 22
__func__.20210:
	.ascii	"nand_flash_print_info\000"
	.space	2
	.type	__func__.20296, %object
	.size	__func__.20296, 11
__func__.20296:
	.ascii	"nandc_init\000"
	.space	1
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
	.space	3
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
	.space	1
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
	.type	__func__.19683, %object
	.size	__func__.19683, 28
__func__.19683:
	.ascii	"flash_wait_device_ready_raw\000"
	.type	__func__.19747, %object
	.size	__func__.19747, 22
__func__.19747:
	.ascii	"flash_start_page_read\000"
	.space	2
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
	.space	1
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
	.space	1
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
	.space	3
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
	.space	2
	.type	__func__.19772, %object
	.size	__func__.19772, 23
__func__.19772:
	.ascii	"flash_start_plane_read\000"
	.space	1
	.type	__func__.19658, %object
	.size	__func__.19658, 26
__func__.19658:
	.ascii	"flash_erase_duplane_block\000"
	.space	2
	.type	__func__.19669, %object
	.size	__func__.19669, 21
__func__.19669:
	.ascii	"flash_erase_block_en\000"
	.space	3
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
	.type	__func__.20475, %object
	.size	__func__.20475, 13
__func__.20475:
	.ascii	"buf_add_tail\000"
	.space	3
	.type	__func__.20488, %object
	.size	__func__.20488, 10
__func__.20488:
	.ascii	"buf_alloc\000"
	.space	2
	.type	__func__.20502, %object
	.size	__func__.20502, 16
__func__.20502:
	.ascii	"buf_remove_free\000"
	.space	4
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 160
zftl_debug_proc_fops:
	.word	0
	.word	seq_lseek
	.word	seq_read
	.word	zftl_debug_proc_write
	.space	36
	.word	zftl_debug_proc_open
	.space	4
	.word	single_release
	.space	96
	.type	__func__.39210, %object
	.size	__func__.39210, 12
__func__.39210:
	.ascii	"gc_add_sblk\000"
	.type	__func__.39302, %object
	.size	__func__.39302, 19
__func__.39302:
	.ascii	"gc_write_completed\000"
	.space	1
	.type	__func__.39908, %object
	.size	__func__.39908, 18
__func__.39908:
	.ascii	"ftl_alloc_sys_blk\000"
	.space	2
	.type	__func__.39918, %object
	.size	__func__.39918, 17
__func__.39918:
	.ascii	"ftl_free_sys_blk\000"
	.space	3
	.type	__func__.40039, %object
	.size	__func__.40039, 23
__func__.40039:
	.ascii	"ftl_get_ppa_from_index\000"
	.space	1
	.type	__func__.40079, %object
	.size	__func__.40079, 22
__func__.40079:
	.ascii	"ftl_get_new_free_page\000"
	.space	2
	.type	__func__.40090, %object
	.size	__func__.40090, 22
__func__.40090:
	.ascii	"ftl_ext_alloc_new_blk\000"
	.space	2
	.type	__func__.39359, %object
	.size	__func__.39359, 16
__func__.39359:
	.ascii	"gc_free_src_blk\000"
	.type	__func__.38953, %object
	.size	__func__.38953, 14
__func__.38953:
	.ascii	"ftl_write_buf\000"
	.space	2
	.type	__func__.38998, %object
	.size	__func__.38998, 18
__func__.38998:
	.ascii	"zftl_add_read_buf\000"
	.space	2
	.type	__func__.40512, %object
	.size	__func__.40512, 21
__func__.40512:
	.ascii	"pm_select_ram_region\000"
	.space	3
	.type	__func__.20200, %object
	.size	__func__.20200, 20
__func__.20200:
	.ascii	"flash_die_info_init\000"
	.type	__func__.38885, %object
	.size	__func__.38885, 17
__func__.38885:
	.ascii	"lpa_rebuild_hash\000"
	.space	3
	.type	__func__.39777, %object
	.size	__func__.39777, 20
__func__.39777:
	.ascii	"zftl_sblk_list_init\000"
	.type	__func__.40444, %object
	.size	__func__.40444, 13
__func__.40444:
	.ascii	"pm_free_sblk\000"
	.space	3
	.type	__func__.20538, %object
	.size	__func__.20538, 21
__func__.20538:
	.ascii	"flash_info_data_init\000"
	.space	3
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
	.space	2
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
	.type	__func__.19581, %object
	.size	__func__.19581, 19
__func__.19581:
	.ascii	"flash_read_page_en\000"
	.space	1
	.type	__func__.19707, %object
	.size	__func__.19707, 26
__func__.19707:
	.ascii	"flash_start_tlc_page_prog\000"
	.space	2
	.type	__func__.19718, %object
	.size	__func__.19718, 29
__func__.19718:
	.ascii	"flash_start_3d_mlc_page_prog\000"
	.space	3
	.type	__func__.19736, %object
	.size	__func__.19736, 22
__func__.19736:
	.ascii	"flash_start_page_prog\000"
	.space	2
	.type	__func__.19760, %object
	.size	__func__.19760, 31
__func__.19760:
	.ascii	"flash_complete_plane_page_read\000"
	.space	1
	.type	__func__.19785, %object
	.size	__func__.19785, 25
__func__.19785:
	.ascii	"flash_complete_page_read\000"
	.space	3
	.type	__func__.40277, %object
	.size	__func__.40277, 31
__func__.40277:
	.ascii	"queue_wait_first_req_completed\000"
	.space	1
	.type	__func__.40331, %object
	.size	__func__.40331, 15
__func__.40331:
	.ascii	"sblk_prog_page\000"
	.space	1
	.type	__func__.40358, %object
	.size	__func__.40358, 15
__func__.40358:
	.ascii	"sblk_read_page\000"
	.space	1
	.type	__func__.39257, %object
	.size	__func__.39257, 21
__func__.39257:
	.ascii	"gc_check_data_one_wl\000"
	.space	3
	.type	__func__.19642, %object
	.size	__func__.19642, 19
__func__.19642:
	.ascii	"flash_prog_page_en\000"
	.space	1
	.type	__func__.40154, %object
	.size	__func__.40154, 14
__func__.40154:
	.ascii	"ftl_prog_page\000"
	.space	2
	.type	__func__.39811, %object
	.size	__func__.39811, 15
__func__.39811:
	.ascii	"ftl_info_flush\000"
	.space	1
	.type	__func__.40116, %object
	.size	__func__.40116, 19
__func__.40116:
	.ascii	"ftl_ext_info_flush\000"
	.space	1
	.type	__func__.40128, %object
	.size	__func__.40128, 18
__func__.40128:
	.ascii	"ftl_ext_info_init\000"
	.space	2
	.type	__func__.39866, %object
	.size	__func__.39866, 15
__func__.39866:
	.ascii	"ftl_alloc_sblk\000"
	.space	1
	.type	__func__.40484, %object
	.size	__func__.40484, 17
__func__.40484:
	.ascii	"pm_alloc_new_blk\000"
	.space	3
	.type	__func__.40494, %object
	.size	__func__.40494, 14
__func__.40494:
	.ascii	"pm_write_page\000"
	.space	2
	.type	__func__.20560, %object
	.size	__func__.20560, 17
__func__.20560:
	.ascii	"flash_info_flush\000"
	.space	3
	.type	__func__.20523, %object
	.size	__func__.20523, 20
__func__.20523:
	.ascii	"flash_info_blk_init\000"
	.type	__func__.20255, %object
	.size	__func__.20255, 16
__func__.20255:
	.ascii	"nand_flash_init\000"
	.type	__func__.39981, %object
	.size	__func__.39981, 16
__func__.39981:
	.ascii	"ftl_sysblk_dump\000"
	.type	__func__.40006, %object
	.size	__func__.40006, 23
__func__.40006:
	.ascii	"ftl_open_sblk_recovery\000"
	.space	1
	.type	__func__.40428, %object
	.size	__func__.40428, 16
__func__.40428:
	.ascii	"load_l2p_region\000"
	.type	__func__.40468, %object
	.size	__func__.40468, 6
__func__.40468:
	.ascii	"pm_gc\000"
	.space	2
	.type	__func__.39200, %object
	.size	__func__.39200, 12
__func__.39200:
	.ascii	"gc_recovery\000"
	.type	__func__.39289, %object
	.size	__func__.39289, 22
__func__.39289:
	.ascii	"gc_update_l2p_map_new\000"
	.space	2
	.type	__func__.39403, %object
	.size	__func__.39403, 16
__func__.39403:
	.ascii	"gc_scan_src_blk\000"
	.type	__func__.39464, %object
	.size	__func__.39464, 20
__func__.39464:
	.ascii	"gc_scan_static_data\000"
	.type	__func__.39527, %object
	.size	__func__.39527, 18
__func__.39527:
	.ascii	"gc_block_vpn_scan\000"
	.space	2
	.type	__func__.39957, %object
	.size	__func__.39957, 14
__func__.39957:
	.ascii	"ftl_sblk_dump\000"
	.space	2
	.type	__func__.39031, %object
	.size	__func__.39031, 10
__func__.39031:
	.ascii	"zftl_read\000"
	.space	2
	.type	__func__.40060, %object
	.size	__func__.40060, 19
__func__.40060:
	.ascii	"ftl_update_l2p_map\000"
	.space	1
	.type	__func__.38935, %object
	.size	__func__.38935, 17
__func__.38935:
	.ascii	"ftl_write_commit\000"
	.space	3
	.type	__func__.39332, %object
	.size	__func__.39332, 16
__func__.39332:
	.ascii	"gc_do_copy_back\000"
	.type	__func__.39582, %object
	.size	__func__.39582, 11
__func__.39582:
	.ascii	"zftl_do_gc\000"
	.space	1
	.type	__func__.39063, %object
	.size	__func__.39063, 13
__func__.39063:
	.ascii	"_ftl_discard\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC1:
	.ascii	"%s\012\000"
.LC2:
	.ascii	"FTL version: 6.0.24 20210315\000"
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
	.ascii	"...%s enter... %p\012\000"
.LC35:
	.ascii	"0:%x %x %x %x %x\012\000"
.LC36:
	.ascii	"g_nandc_ver...%d\012\000"
.LC37:
	.ascii	"nand sdr mode %x\012\000"
.LC38:
	.ascii	"nand ddr mode %x\012\000"
.LC39:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC40:
	.ascii	"otp:%x %x %x %x\012\000"
.LC41:
	.ascii	"bad block test:%x %x\012\000"
.LC42:
	.ascii	"flash_erase_duplane_block %x %x %x\012\000"
.LC43:
	.ascii	"flash_erase_duplane_block pageadd = %x status = %x\012"
	.ascii	"\000"
.LC44:
	.ascii	"flash_erase_block %x %x %x\012\000"
.LC45:
	.ascii	"flash_erase_block %d block = %x status = %x\012\000"
.LC46:
	.ascii	"erase done: %x\012\000"
.LC47:
	.ascii	"sblk_queue_head = %d\012\000"
.LC48:
	.ascii	"sblk_read_completed_queue_head = %d\012\000"
.LC49:
	.ascii	"sblk_gc_write_completed_queue_head = %d\012\000"
.LC50:
	.ascii	"sblk_write_completed_queue_head = %d\012\000"
.LC51:
	.ascii	"p_free_buf_head = %d\012\000"
.LC52:
	.ascii	"free_buf_count = %d\012\000"
.LC53:
	.ascii	"buf = %d, next=%d, flag=%d gc_write_flag=%d, lun_st"
	.ascii	"ate=%d, op_status = %d lpa=%x, ppa=%x\012\000"
.LC54:
	.ascii	"flash_mask_bad_block %d %d\012\000"
.LC55:
	.ascii	"zftl_debug\000"
.LC56:
	.ascii	"FLASH ID: %x %x %x %x %x %x\012\000"
.LC57:
	.ascii	"density: %d MB\012\000"
.LC58:
	.ascii	"device density: %d MB\012\000"
.LC59:
	.ascii	"FTL INFO:\012\000"
.LC60:
	.ascii	"max_lpn = 0x%x\012\000"
.LC61:
	.ascii	"density = 0x%x\012\000"
.LC62:
	.ascii	"slc vpn = 0x%x\012\000"
.LC63:
	.ascii	"xlc vpn = 0x%x\012\000"
.LC64:
	.ascii	"free slc blk = 0x%x\012\000"
.LC65:
	.ascii	"free xlc blk = 0x%x\012\000"
.LC66:
	.ascii	"free mix blk = 0x%x\012\000"
.LC67:
	.ascii	"slc data blk = 0x%x\012\000"
.LC68:
	.ascii	"slc cache blk = 0x%x\012\000"
.LC69:
	.ascii	"xlc data blk = 0x%x\012\000"
.LC70:
	.ascii	"free buf = %d, %d, %d\012\000"
.LC71:
	.ascii	"bad blk = %d %d\012\000"
.LC72:
	.ascii	"TBW = %d MB\012\000"
.LC73:
	.ascii	"TBR = %d MB\012\000"
.LC74:
	.ascii	"POC = %d\012\000"
.LC75:
	.ascii	"PLC = %d\012\000"
.LC76:
	.ascii	"sys run time = %d S\012\000"
.LC77:
	.ascii	"slc mode = %x %x %x\012\000"
.LC78:
	.ascii	"prog err = %d\012\000"
.LC79:
	.ascii	"read err = %d\012\000"
.LC80:
	.ascii	"GC XLC page = %d\012\000"
.LC81:
	.ascii	"GC SLC page = %d\012\000"
.LC82:
	.ascii	"discard page = 0x%x\012\000"
.LC83:
	.ascii	"version = %d\012\000"
.LC84:
	.ascii	"acblk = 0x%x %d %d\012\000"
.LC85:
	.ascii	"tmblk = 0x%x %d %d\012\000"
.LC86:
	.ascii	"gcblk = 0x%x %d %d\012\000"
.LC87:
	.ascii	"slc ec = %d, %d, %d, %d, %d\012\000"
.LC88:
	.ascii	"xlc ec = %d, %d, %d, %d, %d\012\000"
.LC89:
	.ascii	"gc free blk th = %d\012\000"
.LC90:
	.ascii	"gc vpn th = %d %d %d %d %d\012\000"
.LC91:
	.ascii	"swl blk = %x %x %x %x\012\000"
.LC92:
	.ascii	"rf info = %x %x %x %x %x\012\000"
.LC93:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\012\000"
.LC94:
	.ascii	"gc_add_sblk = %d, %d, %d\012\000"
.LC95:
	.ascii	"gc_add_sblk = %d, %d, %d,last update:%d, %d\012\000"
.LC96:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d\012\000"
.LC97:
	.ascii	"gc_mark_bad_ppa %d %x %x\012\000"
.LC98:
	.ascii	"status: %x, ppa: %x\012\000"
.LC99:
	.ascii	"%d gc_free_temp_buf buf id= %x\012\000"
.LC100:
	.ascii	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\012"
	.ascii	"\000"
.LC101:
	.ascii	"zftl_get_gc_node cache = %x index = %d vpn = %x\012"
	.ascii	"\000"
.LC102:
	.ascii	"gc_search_src_blk mode = %x, src mode = %x, count= "
	.ascii	"%d %d\012\000"
.LC103:
	.ascii	"swl_tlc_free_mini_ec_blk alloc sblk %x\012\000"
.LC104:
	.ascii	"zftl_get_free_sblk %x %d, %p %d %d\012\000"
.LC105:
	.ascii	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\012\000"
.LC106:
	.ascii	"swl_slc_free_mini_ec_blk alloc sblk %x\012\000"
.LC107:
	.ascii	"list count:%p %d\012\000"
.LC108:
	.ascii	"%d: node:%x %x %x %x, %d %d %d %d %d\012\000"
.LC109:
	.ascii	"ftl_vpn_decrement %x = %d, %d\012\000"
.LC110:
	.ascii	"mask bad block:cs %x %x block: %x %x\012\000"
.LC111:
	.ascii	"gc_free_bad_sblk 0x%x\012\000"
.LC112:
	.ascii	"swl_slc_free_mini_ec_blk sblk %x\012\000"
.LC113:
	.ascii	"gc_free_src_blk = %x, vpn = %d\012\000"
.LC114:
	.ascii	"gc_free_src_blk %x, %d\012\000"
.LC115:
	.ascii	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk"
	.ascii	":s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\012\000"
.LC116:
	.ascii	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\012\000"
.LC117:
	.ascii	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\012\000"
.LC118:
	.ascii	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\012\000"
.LC119:
	.ascii	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:"
	.ascii	"%d free_th: %d\012\000"
.LC120:
	.ascii	"swl : %x %x %x %x %x %x\012\000"
.LC121:
	.ascii	"ftl prog error =%x, lpa = %x, ppa= %x\012\000"
.LC122:
	.ascii	"ftl re prog: lpa = %x, ppa= %x\012\000"
.LC123:
	.ascii	"dump_sblk_queue: %d\012\000"
.LC124:
	.ascii	"buf id= %d state = %d ppa = %x\012\000"
.LC125:
	.ascii	"%s %d %d\012\000"
.LC126:
	.ascii	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d"
	.ascii	" ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC127:
	.ascii	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC128:
	.ascii	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC129:
	.ascii	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC130:
	.ascii	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC131:
	.ascii	"gc_static_wearleveling: slc blk: %x, tlc blk: %d av"
	.ascii	"g slc ec: %d, avg tlc ec: %d \012\000"
.LC132:
	.ascii	"gc_static_wearleveling: min slc ec: %x, min tlc ec:"
	.ascii	" %d max slc ec: %d, max tlc ec: %d; %d %d\012\000"
.LC133:
	.ascii	"swl add tlc gc = %x, %d, %d, %d, %d, %d\012\000"
.LC134:
	.ascii	"swl add slc gc  = %x, %d, %d, %d, %d, %d\012\000"
.LC135:
	.ascii	"free blk vpn error: %x %x\012\000"
.LC136:
	.ascii	"GC PM block %x %x %x %d\012\000"
.LC137:
	.ascii	"ftl_free_no_use_map_blk %x %x %x %d\012\000"
.LC138:
	.ascii	"...%d @ %s\012\000"
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
	.ascii	"read page: %x %x %x %x\012\000"
.LC169:
	.ascii	"flash_prog_page page_addr = %x status = %x\012\000"
.LC170:
	.ascii	"flash_prog_page %x %x %x\012\000"
.LC171:
	.ascii	"ymtc_flash_tlc_page_prog page_addr = %x status = %x"
	.ascii	"\012\000"
.LC172:
	.ascii	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\012"
	.ascii	"\000"
.LC173:
	.ascii	"flash_complete_page_read %x %x error_ecc %d %d\012\000"
.LC174:
	.ascii	"read: %x %x %x %x\012\000"
.LC175:
	.ascii	"0set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC176:
	.ascii	"prog end %x %x error_ecc %d %d\012\000"
.LC177:
	.ascii	"1set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC178:
	.ascii	"dp prog end %x %x error_ecc %d %d\012\000"
.LC179:
	.ascii	"sblk_prog_page ppa = %x, count = %d\012\000"
.LC180:
	.ascii	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %"
	.ascii	"x\012\000"
.LC181:
	.ascii	"flash_prog_page_en:%x %x %x\012\000"
.LC182:
	.ascii	"w d:\000"
.LC183:
	.ascii	"w s:\000"
.LC184:
	.ascii	"spare\000"
.LC185:
	.ascii	"data\000"
.LC186:
	.ascii	"write error: %x\012\000"
.LC187:
	.ascii	"g_ftl_info_blk blk = %x, page = %x version = %d\012"
	.ascii	"\000"
.LC188:
	.ascii	"%d %x @%d %x\012\000"
.LC189:
	.ascii	"ftl_info_blk_init %d %d %x\012\000"
.LC190:
	.ascii	"ftl info hash %x error\012\000"
.LC191:
	.ascii	"ink flag: %x\012\000"
.LC192:
	.ascii	"%s %d %d %x %x\012\000"
.LC193:
	.ascii	"ext info hash %x error\012\000"
.LC194:
	.ascii	"%s %x %x %x\012\000"
.LC195:
	.ascii	"ftl_sblk_dump_write = %x %d %d %d %d\012\000"
.LC196:
	.ascii	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x"
	.ascii	" %x %x, spare: %x %x %x %x\012\000"
.LC197:
	.ascii	"ftl_sblk_dump_write2 = %x %d %d %d\012\000"
.LC198:
	.ascii	"ftl_sblk_dump_write = %x %x\012\000"
.LC199:
	.ascii	"ftl_sblk_dump_write done = %x\012\000"
.LC200:
	.ascii	"%x: ink_scaned_blk_num %x\012\000"
.LC201:
	.ascii	"ftl_ink_check_sblk = %x %d %d\012\000"
.LC202:
	.ascii	"ftl_ink_check_sblk = %x %d %d end\012\000"
.LC203:
	.ascii	"alloc sblk %x %d\012\000"
.LC204:
	.ascii	"blk %x is bad block\012\000"
.LC205:
	.ascii	"pm_alloc_new_blk: %x %x %x %x\012\000"
.LC206:
	.ascii	"pm_write_page write error: %x\012\000"
.LC207:
	.ascii	"finfo:\000"
.LC208:
	.ascii	"flash_info_flush id = %x, page = %x\012\000"
.LC209:
	.ascii	"sys_info_flush error:%x\012\000"
.LC210:
	.ascii	"...%d @ %s %d %p\012\000"
.LC211:
	.ascii	"no sys info %x\012\000"
.LC212:
	.ascii	"l2p:\000"
.LC213:
	.ascii	"saved_active_page  = %x\012\000"
.LC214:
	.ascii	"saved_active_plane = %x\012\000"
.LC215:
	.ascii	"sblk = %x\012\000"
.LC216:
	.ascii	"phy_blk = %x %x\012\000"
.LC217:
	.ascii	"num_planes = %x\012\000"
.LC218:
	.ascii	"recovery blk=%x, page=%x, ppa = %x, status = %x, ha"
	.ascii	"sh:%x\012\000"
.LC219:
	.ascii	"data:\000"
.LC220:
	.ascii	"sblk = %x, vpn0 = %d, vpn1 = %d\012\000"
.LC221:
	.ascii	"dump_write_lpa = %x %x %x %x\012\000"
.LC222:
	.ascii	"dump write new ppa = %x, last ppa = %x lpa = %x\012"
	.ascii	"\000"
.LC223:
	.ascii	"dump write = %x %x %x\012\000"
.LC224:
	.ascii	"dump write hash update = %x %x %x\012\000"
.LC225:
	.ascii	"free_buf_count: %d\012\000"
.LC226:
	.ascii	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\012\000"
.LC227:
	.ascii	"ftl_ext_info_blk blk:0x%x, page:0x%x\012\000"
.LC228:
	.ascii	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC229:
	.ascii	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, pag"
	.ascii	"e_index:0x%x\012\000"
.LC230:
	.ascii	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC231:
	.ascii	"lpa:\000"
.LC232:
	.ascii	"vpn:\000"
.LC233:
	.ascii	"sblk:\000"
.LC234:
	.ascii	"lpa_hash:\000"
.LC235:
	.ascii	"lpa_hash_index:\000"
.LC236:
	.ascii	"%s w error lpn = %x, max ppa = %d\012\000"
.LC237:
	.ascii	"region_id = %d, pm_max_region = %d\012\000"
.LC238:
	.ascii	"load_l2p_region no ppa = %x , %x, all setting 0xff."
	.ascii	"...\012\000"
.LC239:
	.ascii	"load_l2p_region = %x,%x,%x, %x\012\000"
.LC240:
	.ascii	"pm_ppa:\000"
.LC241:
	.ascii	"spare:\000"
.LC242:
	.ascii	"pm_init posr %x %x %x\012\000"
.LC243:
	.ascii	"pm_init recovery %x %x %x\012\000"
.LC244:
	.ascii	"pm_init hash %x error\012\000"
.LC245:
	.ascii	"pm_log2phys  lpn = %d, max lpn = %d\012\000"
.LC246:
	.ascii	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x "
	.ascii	"%x %x %x\012\000"
.LC247:
	.ascii	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\012"
	.ascii	"\000"
.LC248:
	.ascii	"gc_recovery: %x vpn = %x\012\000"
.LC249:
	.ascii	"gc_update_l2p_map_new sblk %x\012\000"
.LC250:
	.ascii	"gc_update_l2p_map_new: %x %x %x\012\000"
.LC251:
	.ascii	"lpa: %x %x %x\012\000"
.LC252:
	.ascii	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\012"
	.ascii	"\000"
.LC253:
	.ascii	"gc_scan_src_blk = %x, vpn = %d\012\000"
.LC254:
	.ascii	"js hash error:%x %x %x\012\000"
.LC255:
	.ascii	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\012\000"
.LC256:
	.ascii	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:"
	.ascii	"%d\012\000"
.LC257:
	.ascii	"ftl_sblk_dump = %x %d %d %d %d\012\000"
.LC258:
	.ascii	"ftl_sblk_dump = %x %x %x %x\012\000"
.LC259:
	.ascii	"page_addr = %x, lpa=%x vpn = %d\012\000"
.LC260:
	.ascii	"index= %x, lpa=%x\012\000"
.LC261:
	.ascii	"block = %x, vpn=%x check vpn = %x\012\000"
.LC262:
	.ascii	"ftl_read %x %x %x\012\000"
.LC263:
	.ascii	"ftl_read refresh =%x, lpa = %x, ppa= %x\012\000"
.LC264:
	.ascii	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %"
	.ascii	"x %x %x\012\000"
.LC265:
	.ascii	"zftl debug cmd: %s\012\000"
.LC266:
	.ascii	"cmd:\000"
.LC267:
	.ascii	"dumpl2p\000"
.LC268:
	.ascii	"pm l2p:\000"
.LC269:
	.ascii	"pm blk:\000"
.LC270:
	.ascii	"dumppm:\000"
.LC271:
	.ascii	"p_cmd: %s\012\000"
.LC272:
	.ascii	"pm ram = %x, %x\012\000"
.LC273:
	.ascii	"ram:\000"
.LC274:
	.ascii	"pm:\000"
.LC275:
	.ascii	"dumpsys\000"
.LC276:
	.ascii	"dumplist:\000"
.LC277:
	.ascii	"vpncheck\000"
.LC278:
	.ascii	"dumpppa:\000"
.LC279:
	.ascii	"dumpblk:\000"
.LC280:
	.ascii	"setzdebug:\000"
.LC281:
	.ascii	"lpa2ppa:\000"
.LC282:
	.ascii	"lpa: %x--> ppa: %x\012\000"
.LC283:
	.ascii	"help:\012\000"
.LC284:
	.ascii	"1. echo dumpl2p > /proc/zftl_debug\012\000"
.LC285:
	.ascii	"2. echo dumppm:x > /proc/zftl_debug\012\000"
.LC286:
	.ascii	"3. echo dumpsys > /proc/zftl_debug\012\000"
.LC287:
	.ascii	"4. echo dumpppa:x > /proc/zftl_debug\012\000"
.LC288:
	.ascii	"5. echo vpncheck > /proc/zftl_debug\012\000"
.LC289:
	.ascii	"6. echo setzdebug:x > /proc/zftl_debug\012\000"
.LC290:
	.ascii	"7. echo dumplist:x > /proc/zftl_debug\012\000"
.LC291:
	.ascii	"8. echo lpa2ppa:x> /proc/zftl_debug\012\000"
.LC292:
	.ascii	"ftl_update_l2p_map: %x %x %x\012\000"
.LC293:
	.ascii	"ftl_update_l2p_map\000"
.LC294:
	.ascii	"lpa_tbl:\000"
.LC295:
	.ascii	"sblk %x vpn: %d %d\012\000"
.LC296:
	.ascii	"error gc_add_sblk: %x\012\000"
.LC297:
	.ascii	"%d read error: ppa:%x, lpa:%x, status:%x\012\000"
.LC298:
	.ascii	"gc page in buf: lpa %x ppa = %x pageindex= %x\012\000"
.LC299:
	.ascii	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x "
	.ascii	"page_index= %d\012\000"
.LC300:
	.ascii	"gc %d: %d %d %d %d %d %d %d\012\000"
.LC301:
	.ascii	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\012"
	.ascii	"\000"
.LC302:
	.ascii	"gc free %x, %d\012\000"
.LC303:
	.ascii	"_c_user_data_density := %d\012\000"
.LC304:
	.ascii	"_c_totle_phy_density := %d\012\000"
.LC305:
	.ascii	"_c_totle_log_page := %d\012\000"
.LC306:
	.ascii	"_c_totle_data_density := %d\012\000"
.LC307:
	.ascii	"_c_ftl_pm_page_num := %d\012\000"
.LC308:
	.ascii	"_c_ftl_byte_pre_page := %d\012\000"
.LC309:
	.ascii	"_c_max_pm_sblk := %d\012\000"
.LC310:
	.ascii	"_min_slc_super_block := %d\012\000"
.LC311:
	.ascii	"_max_xlc_super_block := %d\012\000"
.LC312:
	.ascii	"gp_ftl_ext_info %p %p %p\012\000"
.LC313:
	.ascii	"flash info size: %d %d %d\012\000"
.LC314:
	.ascii	"ftl_init %x\012\000"
.LC315:
	.ascii	"ftlwrite %x %x %x %x\012\000"
.LC316:
	.ascii	"ftl_discard:(%x, %x, %x, %x)\012\000"
.LC317:
	.ascii	"id_block_prog_msb_ff_data slc page = %d pageadd=%x "
	.ascii	"%x\012\000"
.LC318:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC319:
	.ascii	"idblk:\000"
.LC320:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC321:
	.ascii	"prog page: %x %x %x, %p %x %x %x\012\000"
.LC322:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC323:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC324:
	.ascii	"rk_copy_from_user error\012\000"
.LC325:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC326:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC327:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC328:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC329:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC330:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC331:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC332:
	.ascii	"LockKey not match %d\012\000"
.LC333:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC334:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC335:
	.ascii	"return ret = %lx\012\000"
.LC336:
	.ascii	"secureBootEn check error\012\000"
.LC337:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
	.data
	.align	2
.LANCHOR2 = . + 0
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
	.space	3
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
	.space	3
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
.LANCHOR0 = . + 0
.LANCHOR3 = . + 8184
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
	.type	nandc_hw_seed, %object
	.size	nandc_hw_seed, 1
nandc_hw_seed:
	.space	1
	.space	3
	.type	g_nandc_v6_master_info, %object
	.size	g_nandc_v6_master_info, 28
g_nandc_v6_master_info:
	.space	28
	.type	nandc_randomizer_en, %object
	.size	nandc_randomizer_en, 1
nandc_randomizer_en:
	.space	1
	.space	1
	.type	fill_spare_size, %object
	.size	fill_spare_size, 2
fill_spare_size:
	.space	2
	.type	g_nandc_ecc_bits, %object
	.size	g_nandc_ecc_bits, 1
g_nandc_ecc_bits:
	.space	1
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.space	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.space	1
	.space	1
	.type	gp_flash_info, %object
	.size	gp_flash_info, 4
gp_flash_info:
	.space	4
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
	.space	2
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
	.space	2
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.space	32
	.type	g_flash_interface_mode, %object
	.size	g_flash_interface_mode, 1
g_flash_interface_mode:
	.space	1
	.type	g_flash_multi_page_prog_en, %object
	.size	g_flash_multi_page_prog_en, 1
g_flash_multi_page_prog_en:
	.space	1
	.space	2
	.type	g_buf, %object
	.size	g_buf, 1536
g_buf:
	.space	1536
	.type	_c_ftl_sec_per_page, %object
	.size	_c_ftl_sec_per_page, 1
_c_ftl_sec_per_page:
	.space	1
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
	.space	1
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
	.type	gc_tlc_mode_tlc_vpn_th, %object
	.size	gc_tlc_mode_tlc_vpn_th, 2
gc_tlc_mode_tlc_vpn_th:
	.space	2
	.type	gc_tlc_mode_slc_vpn_th, %object
	.size	gc_tlc_mode_slc_vpn_th, 2
gc_tlc_mode_slc_vpn_th:
	.space	2
	.type	gc_slc_mode_vpn_th, %object
	.size	gc_slc_mode_vpn_th, 2
gc_slc_mode_vpn_th:
	.space	2
	.type	gc_slc_mode_tlc_vpn_th, %object
	.size	gc_slc_mode_tlc_vpn_th, 2
gc_slc_mode_tlc_vpn_th:
	.space	2
	.space	2
	.type	_gc_after_discard_en, %object
	.size	_gc_after_discard_en, 4
_gc_after_discard_en:
	.space	4
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
	.space	1
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
	.space	2
	.type	gp_data_slc_data_head, %object
	.size	gp_data_slc_data_head, 4
gp_data_slc_data_head:
	.space	4
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
	.space	3
	.type	lpa_hash, %object
	.size	lpa_hash, 512
lpa_hash:
	.space	512
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
	.type	gc_state, %object
	.size	gc_state, 1
gc_state:
	.space	1
	.space	1
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
	.type	g_flash_page_buffer, %object
	.size	g_flash_page_buffer, 4
g_flash_page_buffer:
	.space	4
	.type	g_flash_spare_buffer, %object
	.size	g_flash_spare_buffer, 4
g_flash_spare_buffer:
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
	.space	31
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 256
ftl_info_spare:
	.space	256
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
	.space	48
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 256
ftl_tmp_spare:
	.space	256
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
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 4
gpDrmKeyInfo:
	.space	4
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.space	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.space	4
	.type	gpBootConfig, %object
	.size	gpBootConfig, 4
gpBootConfig:
	.space	4
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.space	512
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.space	4
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 4
gpVendor0Info:
	.space	4
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 4
gpVendor1Info:
	.space	4
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 4
g_idb_buffer:
	.space	4
	.type	g_vendor, %object
	.size	g_vendor, 4
g_vendor:
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
	.space	2
	.type	nandc_ecc_sts, %object
	.size	nandc_ecc_sts, 16
nandc_ecc_sts:
	.space	16
	.type	g_slc_mode_enable, %object
	.size	g_slc_mode_enable, 1
g_slc_mode_enable:
	.space	1
