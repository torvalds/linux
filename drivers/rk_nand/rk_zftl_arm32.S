/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-11-30
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
	.type	_list_get_gc_head_node, %function
_list_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, [r0]
	cmp	r0, #0
	beq	.L32
	ldr	r3, .L35
	mov	ip, #6
	ldr	r2, [r3, #1036]
	movw	r3, #65535
.L29:
	cmp	r1, #0
	beq	.L30
	ldrh	r0, [r0]
	cmp	r0, r3
	subne	r1, r1, #1
	mlane	r0, ip, r0, r2
	uxthne	r1, r1
	bne	.L29
.L34:
	bx	lr
.L32:
	movw	r0, #65535
	bx	lr
.L30:
	rsb	r0, r2, r0
	ldr	r3, .L35+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	uxth	r0, r0
	bx	lr
.L36:
	.align	2
.L35:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_get_gc_head_node, .-_list_get_gc_head_node
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
	ldr	r6, .L46
	mov	r7, r0
	movw	r3, #65535
	ldr	r8, [r6, #1036]
	add	r4, r8, r5
	ldrh	r0, [r8, r5]
	ldrh	r1, [r4, #2]
	cmp	r0, r3
	ldr	r3, [r7]
	bne	.L38
	cmp	r1, r0
	bne	.L38
	cmp	r4, r3
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L38:
	mov	r9, r2
	movw	r2, #65535
	cmp	r1, r2
	bne	.L40
	cmp	r4, r3
	beq	.L40
	ldr	r1, .L46+4
	mov	r2, #202
	ldr	r0, .L46+8
	bl	printk
	bl	dump_stack
.L40:
	ldr	r3, [r7]
	movw	r2, #65535
	cmp	r4, r3
	ldrh	r3, [r8, r5]
	bne	.L41
	cmp	r3, r2
	ldrne	r2, [r6, #1036]
	movne	r1, #6
	moveq	r3, #0
	streq	r3, [r7]
	mlane	r3, r1, r3, r2
	mvnne	r2, #0
	strne	r3, [r7]
	strneh	r2, [r3, #2]	@ movhi
	b	.L43
.L41:
	cmp	r3, r2
	ldrh	r1, [r4, #2]
	bne	.L44
	cmp	r1, r3
	movne	r3, #6
	ldrne	r2, [r6, #1036]
	mulne	r1, r3, r1
	mvnne	r3, #0
	strneh	r3, [r2, r1]	@ movhi
	b	.L43
.L44:
	ldr	r0, [r6, #1036]
	mov	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r4, #2]
	ldrh	r1, [r8, r5]
	ldr	r3, [r6, #1036]
	mul	r2, r2, r0
	strh	r1, [r3, r2]	@ movhi
.L43:
	mvn	r3, #0
	strh	r3, [r8, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	ldrh	r3, [r9]
	sub	r3, r3, #1
	strh	r3, [r9]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L47:
	.align	2
.L46:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC0
	.fnend
	.size	_list_remove_node, .-_list_remove_node
	.align	2
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, [r0]
	cmp	r5, #0
	beq	.L54
	ldr	r3, .L56
	movw	ip, #65535
	mov	lr, #6
	ldr	r4, [r3, #1036]
.L50:
	cmp	r1, #0
	bne	.L51
.L53:
	rsb	r4, r4, r5
	ldr	r3, .L56+4
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
.L51:
	ldrh	r3, [r5]
	cmp	r3, ip
	beq	.L53
	sub	r1, r1, #1
	mla	r5, lr, r3, r4
	uxth	r1, r1
	b	.L50
.L54:
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, pc}
.L57:
	.align	2
.L56:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	_list_pop_index_node, .-_list_pop_index_node
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
	ldr	r1, .L60
	mov	r2, r0
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	single_open
.L61:
	.align	2
.L60:
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
	ldr	r1, .L64
	ldr	r2, .L64+4
	bl	seq_printf
	ldr	r3, .L64+8
	mov	r0, r4
	ldr	r1, .L64+12
	ldr	r2, [r3]
	bl	seq_printf
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L65:
	.align	2
.L64:
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
	ldr	r3, .L70
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	ldr	r2, [r3, #1040]
	ldr	r1, [r2]
	str	r1, [r3, #1044]
	ldr	r1, [r2, #4]
	str	r1, [r3, #1048]
	bne	.L67
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
	b	.L69
.L67:
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
.L69:
	str	r2, [r3, #1072]
	bx	lr
.L71:
	.align	2
.L70:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L82
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
	beq	.L81
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
	ldr	r2, .L82+4
	mov	r8, r8, asr #1
	mul	r2, r2, r8
	uxth	r2, r2
.L77:
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
	bcs	.L75
	ldrh	ip, [r3]
	cmp	ip, fp
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L72
.L76:
	ldr	r3, [sp, #4]
	mov	r2, ip
	mla	r3, r3, ip, r9
	b	.L77
.L75:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L78
	strh	r1, [r3, #2]	@ movhi
.L81:
	str	lr, [r0]
	b	.L72
.L78:
	ldrh	ip, [r3, #2]
	mov	r2, #6
	ldr	r0, [r4, #1036]
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L72:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L83:
	.align	2
.L82:
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
	ldr	ip, .L105
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
	beq	.L104
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
	ldr	r4, .L105
	mvneq	ip, #0
	ldr	r2, [r4, #1036]
	rsb	r6, r2, r3
	str	r2, [sp, #12]
	ldr	r2, .L105+4
	mov	r6, r6, asr #1
	mul	r2, r2, r6
	movw	r6, #1076
	ldrh	r4, [r4, r6]
	mov	r6, #0
	str	r4, [sp, #16]
	uxth	r2, r2
.L94:
	add	r6, r6, #1
	ldr	r4, [sp, #16]
	uxth	r6, r6
	cmp	r6, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r1, r2
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L84
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
	bcs	.L92
	ldrh	r4, [r3]
	movw	r7, #65535
	cmp	r4, r7
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L84
.L93:
	ldr	r2, [sp, #12]
	mov	r3, #6
	mla	r3, r3, r4, r2
	mov	r2, r4
	b	.L94
.L92:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r10, r5]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L95
	strh	r1, [r3, #2]	@ movhi
.L104:
	str	lr, [r0]
	b	.L84
.L95:
	ldr	r2, .L105
	ldrh	ip, [r3, #2]
	ldr	r0, [r2, #1036]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L84:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L106:
	.align	2
.L105:
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
	ldr	r1, .L118
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
	bne	.L111
	ldrh	r2, [r10, r8]
	cmp	r2, r3
	bne	.L111
	ldr	r1, .L118+4
	movw	r2, #273
	ldr	r0, .L118+8
	bl	printk
	bl	dump_stack
.L111:
	ldrh	r2, [r9, #2]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L112
	ldrh	r3, [r10, r8]
	cmp	r3, r2
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L112:
	ldr	r0, [r5, #1088]
	mov	r3, r4, asl #1
	ldrh	r1, [r0, r3]
	ldrh	r3, [r9, #4]
	cmp	r3, #0
	mulne	r1, r3, r1
	mov	r3, #6
	mul	r2, r3, r2
	ldr	r3, .L118+12
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
.L119:
	.align	2
.L118:
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
	bne	.L121
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	add	r2, r0, #1
	orr	r3, r3, #32
	cmp	r3, #120
	bne	.L132
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	bic	ip, r3, #32
	sub	ip, ip, #65
	cmp	ip, #25
	bls	.L122
	sub	r3, r3, #48
	cmp	r3, #9
	bls	.L122
.L132:
	mov	lr, #8
.L121:
	mov	r0, #0
.L123:
	mov	r4, r2
	ldrb	ip, [r2], #1	@ zero_extendqisi2
	bic	r3, ip, #32
	sub	r3, r3, #65
	cmp	r3, #25
	sub	r3, ip, #48
	bls	.L124
	cmp	r3, #9
	bls	.L124
.L128:
	cmp	r1, #0
	bne	.L125
	ldmfd	sp!, {r4, pc}
.L125:
	str	r4, [r1]
	ldmfd	sp!, {r4, pc}
.L122:
	add	r2, r0, #2
	mov	lr, #16
	b	.L121
.L124:
	cmp	r3, #9
	orrhi	ip, ip, #32
	subhi	r3, ip, #87
	cmp	r3, lr
	bcs	.L128
	mla	r0, lr, r0, r3
	b	.L123
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
	ldr	r4, .L254
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L136
	ldr	r0, .L254+4
	ldr	r1, .L254+8
	bl	printk
.L136:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L137
	ldr	r3, .L254+12
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
	ldr	r0, .L254+16
	bl	printk
.L137:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L138
	ldr	r3, .L254+12
	ldr	r0, .L254+20
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #8]	@ zero_extendqisi2
	bl	printk
.L138:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L139
	ldr	r3, .L254+12
	ldr	r0, .L254+24
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	printk
.L139:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L140
	ldr	r3, .L254+12
	ldr	r0, .L254+28
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #10]
	bl	printk
.L140:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L141
	ldr	r3, .L254+12
	ldr	r0, .L254+32
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	printk
.L141:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L142
	ldr	r3, .L254+12
	ldr	r0, .L254+36
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	bl	printk
.L142:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L143
	ldr	r3, .L254+12
	ldr	r0, .L254+40
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #14]
	bl	printk
.L143:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L144
	ldr	r3, .L254+12
	ldr	r0, .L254+44
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #23]	@ zero_extendqisi2
	bl	printk
.L144:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L145
	ldr	r3, .L254+12
	ldr	r0, .L254+48
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #18]	@ zero_extendqisi2
	bl	printk
.L145:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L146
	ldr	r3, .L254+12
	ldr	r0, .L254+52
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	bl	printk
.L146:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L147
	ldr	r3, .L254+12
	ldr	r0, .L254+56
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	bl	printk
.L147:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L148
	ldr	r3, .L254+12
	ldr	r0, .L254+60
	ldrb	r1, [r3, #1100]	@ zero_extendqisi2
	bl	printk
.L148:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L149
	ldr	r3, .L254+12
	ldr	r0, .L254+64
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #22]	@ zero_extendqisi2
	bl	printk
.L149:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L150
	ldr	r3, .L254+12
	ldr	r0, .L254+68
	ldrb	r1, [r3, #1101]	@ zero_extendqisi2
	bl	printk
.L150:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L151
	ldr	r3, .L254+12
	ldr	r0, .L254+72
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	and	r1, r1, #1
	bl	printk
.L151:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L152
	ldr	r3, .L254+12
	ldr	r0, .L254+76
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #1, #1
	bl	printk
.L152:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L153
	ldr	r3, .L254+12
	ldr	r0, .L254+80
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #2, #1
	bl	printk
.L153:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L154
	ldr	r3, .L254+12
	ldr	r0, .L254+84
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #3, #1
	bl	printk
.L154:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L155
	ldr	r3, .L254+12
	ldr	r0, .L254+88
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #4, #1
	bl	printk
.L155:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L156
	ldr	r3, .L254+12
	ldr	r0, .L254+92
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #5, #1
	bl	printk
.L156:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L157
	ldr	r3, .L254+12
	ldr	r0, .L254+96
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #6, #1
	bl	printk
.L157:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L158
	ldr	r3, .L254+12
	ldr	r0, .L254+100
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #7, #1
	bl	printk
.L158:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L159
	ldr	r3, .L254+12
	ldr	r0, .L254+104
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #8, #1
	bl	printk
.L159:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L160
	ldr	r3, .L254+12
	ldr	r0, .L254+108
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #9, #1
	bl	printk
.L160:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L161
	ldr	r3, .L254+12
	ldr	r0, .L254+112
	ldr	r3, [r3, #1096]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #10, #1
	bl	printk
.L161:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L162
	ldr	r3, .L254+12
	ldr	r0, .L254+116
	ldrb	r1, [r3]	@ zero_extendqisi2
	ldrb	r2, [r3, #1102]	@ zero_extendqisi2
	bl	printk
.L162:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L163
	ldr	r3, .L254+12
	ldr	r0, .L254+120
	ldrb	r1, [r3, #1113]	@ zero_extendqisi2
	ldrb	r2, [r3, #1114]	@ zero_extendqisi2
	bl	printk
.L163:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L164
	ldr	r3, .L254+12
	ldr	r0, .L254+124
	ldrb	r1, [r3, #1111]	@ zero_extendqisi2
	ldrb	r2, [r3, #1112]	@ zero_extendqisi2
	bl	printk
.L164:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L135
	ldr	r3, .L254+12
	ldr	r0, .L254+128
	ldrb	r1, [r3, #1135]	@ zero_extendqisi2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, lr}
	b	printk
.L135:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L255:
	.align	2
.L254:
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
	ldr	r5, .L276
	mov	r3, #0
	str	r3, [sp, #12]
	mov	r7, r0
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L257
	ldr	r0, .L276+4
	mov	r2, r7
	ldr	r1, .L276+8
	bl	printk
.L257:
	ldr	r4, .L276+12
	mov	r3, #6
	ldr	r2, [r7, #352]
	ldr	r6, .L276+12
	strb	r3, [r4, #1028]
	ldr	r3, .L276+16
	str	r7, [r4, #1040]
	cmp	r2, r3
	ldr	r2, [r7, #128]
	moveq	r3, #8
	streqb	r3, [r4, #1028]
	ldr	r3, .L276+20
	cmp	r2, r3
	ldr	r2, .L276+24
	moveq	r3, #9
	streqb	r3, [r4, #1028]
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L260
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
	beq	.L262
	ldr	r1, [r0]
	ldr	r2, [r0, #8]
	ldr	r3, [r0, #80]
	ldr	ip, [r0, #84]
	ldr	r0, [r0, #88]
	b	.L275
.L260:
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
	beq	.L262
	ldr	r0, [r6, #1040]
	ldr	r1, [r0]
	ldr	r2, [r0, #344]
	ldr	r3, [r0, #304]
	ldr	ip, [r0, #308]
	ldr	r0, [r0, #312]
.L275:
	str	r0, [sp, #4]
	str	ip, [sp]
	ldr	r0, .L276+28
	bl	printk
.L262:
	movw	r2, #1170
	mov	r3, #1
	strb	r3, [r4, #1168]
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #1172]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L256
	ldr	r3, .L276+12
	ldr	r0, .L276+32
	ldrb	r1, [r3, #1028]	@ zero_extendqisi2
	bl	printk
.L256:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L277:
	.align	2
.L276:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
	mov	r0, r0, lsr #4
	str	r0, [sp, #4]
.L279:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	sub	r2, r3, #1
	str	r2, [sp, #4]
	bne	.L279
	add	sp, sp, #8
	@ sp needed
	bx	lr
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
	ldr	r9, .L294
	mov	r5, r0
	ldr	r7, .L294+4
	mov	r6, r1
	add	r10, r9, #80
	mov	r4, #0
.L285:
	ldrb	r3, [r7, #1174]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L293
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
	beq	.L292
	cmp	r3, #35
	addne	r3, r9, r6
	ldrnesb	r3, [r3, #224]
	bne	.L291
	ldr	r3, .L294+8
	add	r2, r4, r8
	add	r3, r3, r2
.L292:
	ldrsb	r3, [r3, #5]
.L291:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L285
.L293:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L295:
	.align	2
.L294:
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
	ldr	r3, .L303
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
.L299:
	cmp	r9, r8
	beq	.L302
	ldrb	r3, [r9, #1]!	@ zero_extendqisi2
	mov	r0, #120
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	ldrsb	r3, [r4, #1]!
	str	r3, [r6, #2048]
	b	.L299
.L302:
	add	r5, r7, r5
	mov	r3, #22
	str	r3, [r5, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L304:
	.align	2
.L303:
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
	ldr	r5, .L311
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
.L312:
	.align	2
.L311:
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
	ldr	r3, .L316
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
.L317:
	.align	2
.L316:
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
	ldr	r3, .L322
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1040]
	cmp	r2, #9
	ldreq	r0, [r3, #80]
	ldrne	r0, [r3, #304]
	ubfx	r0, r0, #8, #8
	bx	lr
.L323:
	.align	2
.L322:
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
	ldr	r1, .L332
	ands	ip, r0, #6
	ldr	r3, [r1, #1040]
	ldr	r2, [r3]
	bfieq	r2, ip, #13, #1
	beq	.L328
	orr	r2, r2, #24576
	ldrb	r1, [r1, #1028]	@ zero_extendqisi2
	bfc	r2, #15, #1
	tst	r0, #4
	orr	r2, r2, #196608
	movw	r0, #8321
	orrne	r2, r2, #32768
	cmp	r1, #9
	ldr	r1, .L332+4
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
.L328:
	str	r2, [r3]
	bx	lr
.L333:
	.align	2
.L332:
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
	ldr	r3, .L335
	mov	r2, #1
	mov	r0, r2, asl r0
	ldr	r1, [r3, #1040]
	ldr	r3, [r1]
	bfi	r3, r0, #0, #8
	str	r3, [r1]
	bx	lr
.L336:
	.align	2
.L335:
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
	ldr	r3, .L338
	ldr	r2, [r3, #1040]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L339:
	.align	2
.L338:
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
	ldr	r4, .L349
	mov	r5, r1
	mov	r7, r2
	ldrb	r3, [r4, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L341
	ldr	r1, .L349+4
	movw	r2, #805
	ldr	r0, .L349+8
	bl	printk
	bl	dump_stack
.L341:
	add	r6, r4, r6
	ldr	r3, [r4, #1040]
	mov	r9, r5, lsr #16
	mov	r10, r5, lsr #24
	ldrb	r6, [r6, #1180]	@ zero_extendqisi2
	mov	fp, #120
	mov	r2, r5, lsr #8
	str	r2, [sp]
	add	r8, r3, r6, asl #8
.L343:
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
	bne	.L343
	mov	r0, r2
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L350:
	.align	2
.L349:
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
	ldr	r6, .L363
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
	bne	.L352
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L353
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L352
.L353:
	ldrh	r7, [r6, #2]
	mov	r0, r4
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L363
	mul	r0, r7, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L352:
	mov	r0, r5
	mov	r1, r4
	mov	r2, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready_raw
.L364:
	.align	2
.L363:
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
	ldr	r4, .L371
	ldr	r5, .L371+4
.L367:
	ldr	r3, [r5, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L368
	mov	r0, #10
	bl	timer_delay_ns
	subs	r4, r4, #1
	bne	.L367
	mvn	r0, #0
	b	.L366
.L368:
	mov	r0, #0
.L366:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L372:
	.align	2
.L371:
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
	ldr	r3, .L393
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldr	r5, [r3, #1040]
	bne	.L376
	ldr	r3, .L393+4
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r5, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L376:
	cmp	r2, #2
	mov	r4, r0
	bne	.L378
	add	r3, r3, r0
	ldrb	r2, [r3, #1192]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r6, .L393+4
	mov	r2, #0
	strb	r2, [r3, #1192]
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	cmp	r3, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	bl	nandc_wait_flash_ready_no_delay
	add	r4, r5, r4, asl #8
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	b	.L392
.L378:
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
.L392:
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L394:
	.align	2
.L393:
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
	ldr	r3, .L417
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r4, [r3]	@ zero_extendqisi2
	cmp	r4, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	cmp	r4, #1
	ldr	r7, [r3, #1040]
	bne	.L398
	ldr	r3, .L417+4
	ldrb	r3, [r3, #34]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r7, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L398:
	cmp	r4, #2
	mov	r5, r0
	bne	.L400
	add	r3, r3, r0
	ldrb	r2, [r3, #1192]	@ zero_extendqisi2
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r4, .L417+4
	ldrb	r2, [r4, #16]	@ zero_extendqisi2
	cmp	r2, #2
	movne	r2, #4
	strb	r2, [r3, #1192]
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	bl	nandc_wait_flash_ready_no_delay
	add	r5, r7, r5, asl #8
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	b	.L416
.L400:
	cmp	r4, #3
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	add	r4, r3, r0
	ldrb	r3, [r4, #1192]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r8, .L417+4
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
.L416:
	str	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L418:
	.align	2
.L417:
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
	ldr	r7, .L434
	mov	r5, r0
	mov	r8, r1
	ldrb	r3, [r7, #1189]	@ zero_extendqisi2
	rsb	r6, r3, #24
	mvn	r2, r2, asl r3
	and	r2, r2, r1, lsr r6
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	uxtb	r4, r2
	cmp	r3, r4
	bhi	.L420
	ldr	r1, .L434+4
	movw	r2, #1001
	ldr	r0, .L434+8
	bl	printk
	bl	dump_stack
.L420:
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	ldr	r9, .L434
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
	bne	.L422
	mov	r0, r6
	bl	slc_phy_page_address_calc
	ldrb	r3, [r9]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L423
	mov	r0, fp
	bl	zftl_flash_enter_slc_mode
	b	.L423
.L422:
	ldr	r3, [r9, #1096]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L424
	ldrb	r3, [r9, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L424
	ldrb	r3, [r9, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r4, r10
	streq	r8, [r3, #2056]
	beq	.L423
.L424:
	mov	r0, fp
	bl	zftl_flash_exit_slc_mode
.L423:
	ldr	r3, [r7, #1096]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L425
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r4, r10
	moveq	r2, #38
	streq	r2, [r3, #2056]
.L425:
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
.L435:
	.align	2
.L434:
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
	ldr	r4, .L442
	ldr	r5, .L442+4
.L438:
	ldr	r3, [r5, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L439
	mov	r0, #10
	bl	timer_delay_ns
	subs	r4, r4, #1
	bne	.L438
	mvn	r0, #0
	b	.L437
.L439:
	mov	r0, #0
.L437:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L443:
	.align	2
.L442:
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
	ldr	r0, .L452
	ldr	r1, .L452+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L445:
	ldrb	r3, [r1, #1174]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L451
	ldrb	r3, [r1, #1173]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L445
.L451:
	ldmfd	sp!, {r3, r4, r5, lr}
	b	nandc_wait_flash_ready
.L453:
	.align	2
.L452:
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
	ldr	r5, .L456
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
.L457:
	.align	2
.L456:
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
	ldr	r2, .L459
	add	r2, r2, r1
	movw	r1, #685
	ldrsb	r2, [r2, r1]
	str	r2, [r0]
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L460:
	.align	2
.L459:
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
	ldr	r3, .L465
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, #239
	mov	r4, r0
	str	r6, [r0, #8]
	beq	.L462
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
	b	.L463
.L462:
	add	r3, r3, r1
	mov	r0, #20
	str	r0, [r4, #4]
	ldrb	r3, [r3, #368]	@ zero_extendqisi2
	str	r3, [r4]
	str	r2, [r4]
	str	r2, [r4]
	str	r2, [r4]
.L463:
	ldmfd	sp!, {r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L466:
	.align	2
.L465:
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
	ldr	r5, .L469
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
.L470:
	.align	2
.L469:
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
	ldr	r3, .L472
	ldrsb	r3, [r3, r1]
	str	r3, [r0]
	mov	r3, #0
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L473:
	.align	2
.L472:
	.word	.LANCHOR1+1056
	.fnend
	.size	ymtc_3d_set_slc_rr_para, .-ymtc_3d_set_slc_rr_para
	.align	2
	.global	flash_start_plane_read
	.type	flash_start_plane_read, %function
flash_start_plane_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r3, #1
	ldr	r7, .L502
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
	bhi	.L475
	ldr	r1, .L502+4
	movw	r2, #1128
	ldr	r0, .L502+8
	bl	printk
	bl	dump_stack
.L475:
	add	r5, r7, r5
	ldr	r3, .L502
	ldr	r8, [r7, #1040]
	ldrb	r10, [r5, #1180]	@ zero_extendqisi2
	str	r3, [sp, #4]
	mov	r0, r10
	mov	fp, r10, asl #8
	bl	nandc_cs
	cmp	r9, #0
	ldr	r3, [sp, #4]
	bne	.L476
	mov	r0, r6
	bl	slc_phy_page_address_calc
	mov	r6, r0
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldr	r3, [sp, #4]
	mov	r4, r0
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L477
	mov	r0, r10
	bl	zftl_flash_enter_slc_mode
	b	.L477
.L476:
	ldr	r2, [r3, #1096]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L478
	ldrb	r2, [r3, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L478
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r8, fp
	streq	r9, [r3, #2056]
	beq	.L477
.L478:
	mov	r0, r10
	bl	zftl_flash_exit_slc_mode
.L477:
	ldrb	r2, [r7, #1119]	@ zero_extendqisi2
	uxtb	ip, r6
	ldr	r3, .L502
	mov	r0, r6, lsr #8
	cmp	r2, #1
	mov	r1, r6, lsr #16
	bne	.L479
	ldrb	r2, [r3, #1111]	@ zero_extendqisi2
	add	r5, r8, fp
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
	add	r6, r8, fp
	str	r3, [r6, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #1096]
	ldrb	r2, [r3, #12]	@ zero_extendqisi2
	adds	r3, r9, #0
	movne	r3, #1
	cmp	r2, #3
	add	r2, r8, fp
	add	fp, r8, fp
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
	str	r3, [fp, #2056]
	b	.L483
.L479:
	ldr	r2, [r3, #1096]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L484
	ldrb	r3, [r2, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r8, fp
	moveq	lr, #38
	streq	lr, [r3, #2056]
.L484:
	ldrb	lr, [r7, #1111]	@ zero_extendqisi2
	add	r3, r8, fp
	add	r8, r8, fp
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
.L483:
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L503:
	.align	2
.L502:
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
	ldr	r6, .L538
	mov	r9, #0
	ldr	r10, .L538+4
	mov	r7, r6
.L516:
	ldr	r3, .L538+8
	ldr	r5, [r6, #1040]
	ldrb	r4, [r3, r9, asl #3]	@ zero_extendqisi2
	cmp	r4, #69
	cmpne	r4, #152
	beq	.L505
	cmp	r4, #137
	cmpne	r4, #44
	moveq	r3, #1
	movne	r3, #0
	cmp	r4, #155
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L506
.L505:
	cmp	r8, #1
	ldrb	r1, [r7, #1232]	@ zero_extendqisi2
	bne	.L507
	tst	r1, #1
	beq	.L506
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L508
	ldr	r0, .L538+12
	bl	printk
.L508:
	mov	r2, r9, asl #8
	cmp	r4, #44
	cmpne	r4, #137
	add	r3, r5, r2
	mov	r1, #239
	str	r1, [r3, #2056]
	beq	.L537
	cmp	r4, #155
	movne	r1, #128
	strne	r1, [r3, #2052]
	movne	r1, #1
	bne	.L535
.L537:
	mov	r1, #1
	str	r1, [r3, #2052]
	mov	r1, #5
	b	.L535
.L507:
	tst	r1, #4
	beq	.L506
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L512
	ldr	r0, .L538+16
	bl	printk
.L512:
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
	beq	.L535
	cmp	r4, #155
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #37
	movne	r0, #128
	strne	r0, [r3, #2052]
.L535:
	add	r5, r5, r2
	str	r1, [r3, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L506:
	add	r9, r9, #1
	cmp	r9, #4
	bne	.L516
	bl	nandc_wait_flash_ready
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L539:
	.align	2
.L538:
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
	ldr	r3, .L541
	mov	r2, #255
	ldr	r3, [r3, #1040]
	add	r0, r3, r0, asl #8
	str	r2, [r0, #2056]
	b	nandc_wait_flash_ready
.L542:
	.align	2
.L541:
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
	ldr	r3, .L546
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
	bhi	.L543
	ldrb	r1, [r4, #2]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	ldr	r0, .L546+4
	str	r1, [sp]
	ldrb	r1, [r4, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r4, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r6, #1
	bl	printk
.L543:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L547:
	.align	2
.L546:
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
	ldr	r3, .L550
	ldr	r2, .L550+4
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
.L551:
	.align	2
.L550:
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
	ldr	r3, .L556
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
	ldr	r0, .L556+4
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
.L553:
	ldr	r2, [r4, #2048]
	strb	r2, [r9], #1
	cmp	r9, r3
	bne	.L553
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
.L557:
	.align	2
.L556:
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
	ldr	r3, .L573
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0, asl #8
	ldr	r6, .L573+4
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
	mov	r0, #80
	str	r3, [r4, #2056]
	bl	timer_delay_ns
	ldr	r2, [r4, #2048]
	ands	r4, r2, #5
	beq	.L561
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L561
	ldr	r0, .L573+8
	mov	r1, r8
	bl	printk
.L561:
	ldrb	r3, [r6, #34]	@ zero_extendqisi2
	mov	r0, r4
	cmp	r3, #0
	addne	r5, r5, r7
	strne	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L574:
	.align	2
.L573:
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
	ldr	r3, .L577
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
.L578:
	.align	2
.L577:
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
	ldr	r2, .L592
	ldrb	r1, [r2, #1028]	@ zero_extendqisi2
	strb	r0, [r2, #1172]
	cmp	r1, #9
	bne	.L580
	ldr	r1, [r2, #1040]
	cmp	r0, #70
	ldr	r2, [sp, #4]
	str	r2, [r1, #16]
	beq	.L581
	cmp	r0, #60
	moveq	r3, #3
	beq	.L581
	cmp	r0, #40
	moveq	r3, #2
	movne	r3, #1
.L581:
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
	b	.L579
.L580:
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
	beq	.L590
	ldr	r3, [sp]
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L591
	orr	r3, r3, #262144
	str	r3, [sp]
	ldr	r3, [sp]
	cmp	r0, #40
	orr	r3, r3, #16
	str	r3, [sp]
	bne	.L584
.L590:
	ldr	r3, [sp]
	bic	r3, r3, #16
.L591:
	str	r3, [sp]
.L584:
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r2, #12]
.L579:
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L593:
	.align	2
.L592:
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
	ldr	r4, .L605
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r2, [r4, #1044]
	cmp	r3, #9
	ldr	r3, [r4, #1040]
	str	r2, [r3]
	ldr	r2, [r4, #1048]
	ldr	r3, [r4, #1040]
	str	r2, [r3, #4]
	ldr	r2, [r4, #1052]
	bne	.L595
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
	b	.L596
.L595:
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
.L596:
	ldr	r6, .L605+4
	mov	r5, #0
	mov	r8, #2
	sub	r7, r6, #8
.L598:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L597
	uxtb	r9, r5
	mov	r0, r9
	bl	flash_reset
	strb	r8, [r5, r7]
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
.L597:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L598
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L599
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r0, r5
	bl	flash_set_interface_mode
	mov	r0, r5
	bl	nandc_set_if_mode
	ldr	r3, .L605
	ldrb	r0, [r3, #1061]	@ zero_extendqisi2
	bl	nandc_set_ddr_para
.L599:
	ldr	r3, .L605+8
	ldrb	r0, [r3, #24]	@ zero_extendqisi2
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	nandc_bch_sel
.L606:
	.align	2
.L605:
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
	ldr	r3, .L610
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
.L611:
	.align	2
.L610:
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
	ldr	r4, .L619
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
	bne	.L615
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L615:
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
.L620:
	.align	2
.L619:
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
	ldr	fp, .L646
	mov	r4, r0
	mov	r5, r1
	mov	r9, r2
	ldrb	r3, [fp, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L622
	ldr	r1, .L646+4
	mov	r2, #688
	ldr	r0, .L646+8
	bl	printk
	bl	dump_stack
.L622:
	ldrb	r2, [fp, #1101]	@ zero_extendqisi2
	ldr	r3, .L646
	cmp	r2, r4
	mvnls	r0, #0
	bls	.L623
	add	r4, r3, r4
	ldr	r7, [r3, #1040]
	ldr	r3, .L646+12
	ldrb	r6, [r4, #1180]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r7, r10, asl #8
	beq	.L624
	ldr	r0, .L646+16
	mov	r1, r6
	mov	r2, r9
	mov	r3, r8
	bl	printk
.L624:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r5, #0
	mov	r0, r6
	bne	.L625
	bl	zftl_flash_enter_slc_mode
	b	.L626
.L625:
	bl	zftl_flash_exit_slc_mode
.L626:
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
	bne	.L628
	add	r2, r7, r3
	mov	r1, #208
	str	r3, [sp, #4]
	str	r1, [r2, #2056]
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	ldr	r3, [sp, #4]
	and	r5, r0, #5
.L628:
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
	beq	.L630
	ldr	r0, .L646+20
	mov	r1, r9
	mov	r2, r4
	bl	printk
.L630:
	mov	r0, r5
.L623:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L647:
	.align	2
.L646:
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
	ldr	r5, .L667
	mov	r7, r0
	mov	fp, r1
	mov	r8, r2
	ldrb	r3, [r5, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	bic	r4, r2, r4, asl r3
	ldrb	r3, [r5, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L649
	ldr	r1, .L667+4
	movw	r2, #750
	ldr	r0, .L667+8
	bl	printk
	bl	dump_stack
.L649:
	ldrb	r2, [r5, #1101]	@ zero_extendqisi2
	ldr	r3, .L667
	cmp	r2, r7
	bls	.L656
	add	r2, r3, r7
	ldr	r9, [r3, #1040]
	ldr	r3, .L667+12
	ldrb	r6, [r2, #1180]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r9, r10, asl #8
	beq	.L651
	ldr	r0, .L667+16
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L651:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r6
	bne	.L652
	bl	zftl_flash_enter_slc_mode
	b	.L653
.L652:
	bl	zftl_flash_exit_slc_mode
.L653:
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
	beq	.L655
	ldrh	r1, [r5, #2]
	mov	r0, r8
	bl	__aeabi_uidiv
	mov	r3, r4
	mov	r1, r7
	mvn	r4, #0
	mov	r2, r0
	ldr	r0, .L667+20
	bl	printk
.L655:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L656:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L668:
	.align	2
.L667:
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
	ldr	r3, .L678
	ldr	r7, .L678+4
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	mov	r8, r7
	smulbb	r4, r4, r3
	uxth	r4, r4
.L671:
	ldrb	r2, [r7, #1101]	@ zero_extendqisi2
	uxth	r3, r6
	cmp	r2, r3
	bls	.L676
	add	r3, r7, r3
	mov	r5, #0
	ldrb	r9, [r3, #1180]	@ zero_extendqisi2
.L672:
	uxth	r3, r5
	cmp	r3, r4
	bcs	.L677
	ldrh	r1, [r8, #2]
	mov	r0, r9
	mul	r1, r1, r5
	bl	flash_erase_block
	add	r5, r5, #1
	b	.L672
.L677:
	add	r6, r6, #1
	b	.L671
.L676:
	ldr	r0, .L678+8
	mov	r1, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L679:
	.align	2
.L678:
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
	ldr	r3, .L683
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
.L684:
	.align	2
.L683:
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
	ldr	r4, .L702
	mov	r0, r2
	.pad #20
	sub	sp, sp, #20
	and	r6, r5, #1
	add	lr, r1, #1
	ldrb	r2, [r4, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L686
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
	beq	.L687
	ldrb	r2, [r4, #1168]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [sp, #4]
	orrne	r2, r2, #512
	strne	r2, [sp, #4]
.L687:
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
	b	.L685
.L686:
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
	beq	.L693
	ldrb	r2, [r4, #1172]	@ zero_extendqisi2
	mov	r1, r1, lsr #1
	mov	r6, ip
	cmp	r2, #25
	movcc	r7, #64
	movcs	r7, #128
.L691:
	cmp	r6, r1
	add	r3, r3, #4
	add	r8, ip, r7
	bge	.L693
	ldrh	lr, [r3, #-2]
	mov	ip, ip, lsr #2
	ldr	r9, [r4, #1140]
	add	r6, r6, #1
	ldrh	r2, [r3, #-4]
	orr	lr, r2, lr, asl #16
	str	lr, [r9, ip, asl #2]
	mov	ip, r8
	b	.L691
.L693:
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
.L685:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L703:
	.align	2
.L702:
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
	ldr	r2, .L711
	and	r0, r0, #127
	ldr	r3, .L711+4
	mov	r0, r0, asl #1
	ldrb	r1, [r2, #1168]	@ zero_extendqisi2
	ldrh	r3, [r3, r0]
	cmp	r1, #0
	ldr	r1, .L711
	ldrb	r2, [r2, #1028]	@ zero_extendqisi2
	orrne	r3, r3, #-1073741824
	cmp	r2, #9
	ldr	r2, [r1, #1040]
	streq	r3, [r2, #520]
	strne	r3, [r2, #336]
	bx	lr
.L712:
	.align	2
.L711:
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
	ldr	r4, .L733
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L714
	ldrb	r0, [r4, #1102]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L714
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	beq	.L714
	bl	zftl_flash_exit_slc_mode
.L714:
	mov	r0, #0
	ldr	r5, .L733
	bl	hynix_reconfig_rr_para
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L715
	ldrb	r3, [r5, #1232]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L715
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r5, #1135]
.L715:
	ldrb	r3, [r4, #1168]	@ zero_extendqisi2
	ldr	r5, .L733
	cmp	r3, #0
	beq	.L716
	mov	r0, #0
	strb	r0, [r5, #1168]
	bl	nandc_set_seed
	mov	r3, #1
	strb	r3, [r5, #1168]
.L716:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L734:
	.align	2
.L733:
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
	ldr	r3, .L736
	strb	r0, [r3, #1168]
	bx	lr
.L737:
	.align	2
.L736:
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
	ldr	r3, .L739
	add	r0, r0, #8
	ldr	r3, [r3, #1040]
	add	r0, r3, r0, asl #8
	bx	lr
.L740:
	.align	2
.L739:
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
	ldr	r4, .L745
	mov	r7, r5
	ldr	r8, .L745+4
.L742:
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
	ldr	r6, .L745+4
	add	r4, r4, #48
	str	r0, [r4, #-44]
	mov	r0, #64
	bl	ftl_malloc
	cmp	r5, #32
	str	r0, [r4, #-36]
	bne	.L742
	mvn	r3, #0
	strb	r5, [r6, #2774]
	strb	r3, [r6, #2724]
	mov	r3, #0
	strb	r3, [r6, #2773]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L746:
	.align	2
.L745:
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
	ldr	r2, .L750
	mov	r3, #0
	mov	r1, r3
.L748:
	uxtb	r0, r3
	add	r3, r3, #1
	cmp	r3, #32
	add	ip, r0, #1
	strb	r0, [r2, #1]
	add	r2, r2, #48
	strb	ip, [r2, #-48]
	strb	r1, [r2, #-46]
	str	r1, [r2, #-40]
	bne	.L748
	ldr	r2, .L750+4
	mvn	r1, #0
	strb	r3, [r2, #2774]
	strb	r1, [r2, #2724]
	mov	r1, #0
	strb	r1, [r2, #2773]
	bx	lr
.L751:
	.align	2
.L750:
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
	ldrne	r2, .L760
	movne	r1, #48
	bne	.L758
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L754
	ldr	r1, .L760+4
	mov	r2, #74
	ldr	r0, .L760+8
	bl	printk
	bl	dump_stack
.L754:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L758:
	mov	r5, r3
	mla	r3, r1, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L758
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L756
	ldr	r1, .L760+4
	mov	r2, #81
	ldr	r0, .L760+8
	bl	printk
	bl	dump_stack
.L756:
	ldr	r2, .L760
	mov	r1, #48
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	mla	r5, r1, r5, r2
	strb	r3, [r5, #1236]
	ldmfd	sp!, {r3, r4, r5, pc}
.L761:
	.align	2
.L760:
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
	ldr	r0, .L764
	mov	r3, #0
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, lr}
	b	buf_add_tail
.L765:
	.align	2
.L764:
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
	ldr	r4, .L776
	and	r3, r3, #8
	uxtb	r3, r3
	strb	r3, [r0, #2]
	cmp	r3, #0
	beq	.L767
	ldr	r3, [r0, #20]
	cmn	r3, #1
	beq	.L767
	mov	r1, r0
	ldr	r0, .L776+4
	bl	buf_add_tail
	b	.L768
.L767:
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	strb	r3, [r0]
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	strb	r3, [r4, #2773]
.L768:
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #2774]
	ldmfd	sp!, {r4, pc}
.L777:
	.align	2
.L776:
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
	ldr	r4, .L789
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L779
.L782:
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	cmp	r5, #0
	ldr	r3, .L789+4
	mov	r0, #48
	mla	r0, r0, r1, r3
	beq	.L780
	b	.L781
.L779:
	ldr	r1, .L789+8
	mov	r2, #121
	ldr	r0, .L789+12
	bl	printk
	bl	dump_stack
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L782
.L784:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L780:
	ldr	r2, .L789
	ldrb	r2, [r2, #2774]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L784
.L781:
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
.L790:
	.align	2
.L789:
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
	bne	.L792
	ldrb	r3, [r1]	@ zero_extendqisi2
	strb	r3, [r0]
	mov	r0, #1
	bx	lr
.L792:
	ldr	r0, .L799
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #48
.L794:
	cmp	r3, #255
	beq	.L798
	mov	r2, r3
	mla	r3, lr, r3, r0
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, ip
	bne	.L794
	ldr	r0, .L799
	mov	ip, #48
	ldrb	r3, [r1]	@ zero_extendqisi2
	mla	r2, ip, r2, r0
	mov	r0, #1
	strb	r3, [r2, #1236]
	mvn	r3, #0
	strb	r3, [r1]
	ldr	pc, [sp], #4
.L798:
	mov	r0, #0
	ldr	pc, [sp], #4
.L800:
	.align	2
.L799:
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
	ldr	r3, .L807
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrb	r2, [r3, #2774]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	bne	.L802
	ldr	r1, .L807+4
	mov	r2, #172
	ldr	r0, .L807+8
	bl	printk
	bl	dump_stack
.L802:
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	ldr	r6, .L807
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L807+12
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
.L808:
	.align	2
.L807:
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
	ldr	r4, .L813
	ldr	r0, .L813+4
	ldrb	r1, [r4, #2775]	@ zero_extendqisi2
	bl	printk
	ldrb	r1, [r4, #2776]	@ zero_extendqisi2
	ldr	r0, .L813+8
	bl	printk
	ldrb	r1, [r4, #2777]	@ zero_extendqisi2
	ldr	r0, .L813+12
	bl	printk
	ldrb	r1, [r4, #2778]	@ zero_extendqisi2
	ldr	r0, .L813+16
	bl	printk
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	ldr	r0, .L813+20
	bl	printk
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	ldr	r0, .L813+24
	bl	printk
	ldr	r4, .L813+28
	add	r5, r4, #1536
.L810:
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
	ldr	r0, .L813+32
	bl	printk
	cmp	r4, r5
	bne	.L810
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L814:
	.align	2
.L813:
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
	ldr	r3, .L816
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L816+4
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
.L817:
	.align	2
.L816:
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
	ldr	r3, .L820
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	mov	r6, r0
	ldrh	r4, [r3, #18]
	mov	r1, r6
	ldr	r0, .L820+4
	smulbb	r4, r2, r4
	mov	r2, r5
	bl	printk
	ldr	r3, .L820+8
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
.L821:
	.align	2
.L820:
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
	bne	.L823
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #2
.L823:
	ldrb	r3, [r0]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #1
	sub	r3, r0, #1
	mov	r0, #0
.L825:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L834
	sub	r1, r2, #48
	uxtb	ip, r1
	cmp	ip, #9
	addls	r0, r1, r0, asl #4
	bls	.L825
	sub	r1, r2, #97
	cmp	r1, #5
	subls	r2, r2, #87
	bls	.L833
	sub	r1, r2, #65
	cmp	r1, #5
	bxhi	lr
	sub	r2, r2, #55
.L833:
	add	r0, r2, r0, asl #4
	b	.L825
.L834:
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
	ldr	r3, .L837
	ldr	r0, .L837+4
	bl	proc_create_data
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L838:
	.align	2
.L837:
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
	mov	r6, r0
	ldr	r1, .L841
	ldr	r2, .L841+4
	bl	sprintf
	ldr	r1, .L841+8
	ldr	r5, .L841+12
	ldrb	r3, [r1, #6]	@ zero_extendqisi2
	ldrb	r2, [r1, #5]	@ zero_extendqisi2
	add	r4, r6, r0
	ldrb	r0, [r1, #7]	@ zero_extendqisi2
	str	r0, [sp]
	ldrb	r0, [r1, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldrb	r0, [r1, #9]	@ zero_extendqisi2
	str	r0, [sp, #8]
	mov	r0, r4
	ldrb	r1, [r1, #10]	@ zero_extendqisi2
	str	r1, [sp, #12]
	ldr	r1, .L841+16
	bl	sprintf
	ldr	r2, [r5, #1032]
	ldr	r1, .L841+20
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #2780]
	ldr	r1, .L841+24
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L841+28
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r2, [r5, #2784]
	mov	r0, r4
	ldr	r1, .L841+32
	bl	sprintf
	ldr	r2, [r5, #1032]
	ldr	r1, .L841+36
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r1, .L841+40
	ldr	r2, [r3, #524]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r1, .L841+44
	ldr	r2, [r3, #528]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2788
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+48
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2790
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+52
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2792
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+56
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2794
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+60
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2796
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+64
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2798
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+68
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r1, [r5, #2801]	@ zero_extendqisi2
	ldrb	r2, [r5, #2774]	@ zero_extendqisi2
	ldrb	r3, [r5, #2800]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L841+72
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+76
	ldrh	r2, [r3, #148]
	ldrh	r3, [r3, #146]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+80
	ldr	r2, [r3, #16]
	ldr	r3, [r3, #20]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+84
	ldr	r2, [r3, #24]
	ldr	r3, [r3, #28]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+88
	ldr	r2, [r3, #64]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+92
	ldr	r2, [r3, #68]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r2, .L841+96
	ldr	r1, .L841+100
	ldr	r3, [r3, #12]
	umull	r2, r3, r3, r2
	mov	r2, r3, lsr #3
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r2, [r5]	@ zero_extendqisi2
	ldr	r1, .L841+104
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r1, .L841+108
	ldr	r2, [r3, #556]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r1, .L841+112
	ldr	r2, [r3, #552]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+116
	ldr	r2, [r3, #52]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+120
	ldr	r2, [r3, #60]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+124
	ldr	r2, [r3, #76]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldr	r1, .L841+128
	ldr	r2, [r3, #8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #1092]
	ldrh	r2, [r1, #16]
	ldrh	r3, [r1, #22]
	ldrb	r1, [r1, #25]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L841+132
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #1092]
	ldrh	r2, [r1, #48]
	ldrh	r3, [r1, #54]
	ldrb	r1, [r1, #57]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L841+136
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #1092]
	ldrh	r2, [r1, #80]
	ldrh	r3, [r1, #86]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L841+140
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldrh	r1, [r3, #88]
	ldrh	r2, [r3, #74]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #92]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #96]
	str	r1, [sp, #8]
	ldr	r1, .L841+144
	ldr	r3, [r3, #84]
	bl	sprintf
	ldr	r3, [r5, #2804]
	ldrh	r1, [r3, #90]
	ldrh	r2, [r3, #72]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #94]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #98]
	str	r1, [sp, #8]
	ldr	r1, .L841+148
	ldr	r3, [r3, #80]
	bl	sprintf
	movw	r3, #2808
	ldrh	r2, [r5, r3]
	ldr	r1, .L841+152
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r1, #2814
	ldrh	r1, [r5, r1]
	movw	r3, #2810
	ldrh	r2, [r5, r3]
	movw	r3, #2812
	ldrh	r3, [r5, r3]
	str	r1, [sp]
	add	r1, r5, #2816
	add	r4, r4, r0
	ldrh	r1, [r1]
	mov	r0, r4
	str	r1, [sp, #4]
	ldr	r1, [r5, #2820]
	str	r1, [sp, #8]
	ldr	r1, .L841+156
	bl	sprintf
	ldr	r1, [r5, #1092]
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
	ldr	r1, .L841+160
	bl	sprintf
	ldr	r3, [r5, #1092]
	ldr	r1, .L841+164
	ldr	r2, [r3, #544]
	str	r2, [sp]
	add	r4, r4, r0
	ldr	r2, [r5, #2804]
	mov	r0, r4
	ldr	r2, [r2, #44]
	str	r2, [sp, #4]
	ldr	r2, [r3, #548]
	str	r2, [sp, #8]
	ldr	r2, [r3, #536]
	ldr	r3, [r3, #540]
	bl	sprintf
	add	r0, r4, r0
	rsb	r0, r6, r0
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L842:
	.align	2
.L841:
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
	ldr	r4, .L846
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
.L847:
	.align	2
.L846:
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
	ldr	r8, .L888
	mov	r4, r1
	mov	r9, r2
	ldr	r7, .L888+4
	ldr	r3, [r8]
	tst	r3, #256
	beq	.L852
	ldr	r3, [r7, #1080]
	mov	r1, r5
	ldr	r2, [r7, #1088]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r0, asl #1
	ldr	r0, .L888+8
	ldrh	r3, [r2, r3]
	mov	r2, r4
	str	r3, [sp, #4]
	ldr	r3, .L888+12
	ldrh	r3, [r3, #52]
	str	r3, [sp, #8]
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	str	r3, [sp, #12]
	mov	r3, r9
	bl	printk
.L852:
	movw	r3, #1076
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L853
	ldr	r1, .L888+16
	movw	r2, #543
	ldr	r0, .L888+20
	bl	printk
	bl	dump_stack
.L853:
	movw	r3, #1076
	ldr	r1, .L888+4
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L854
	ldr	r0, [r8]
	ands	r0, r0, #1024
	beq	.L855
	mov	r1, r5
	mov	r2, r4
	mov	r3, r9
	ldr	r0, .L888+24
	bl	printk
	b	.L874
.L854:
	ldr	r2, [r1, #1088]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	ldr	r2, [r1, #1080]
	add	r2, r2, r5, asl #2
	ldrb	ip, [r2, #2]	@ zero_extendqisi2
	ands	r6, ip, #224
	bne	.L856
	cmp	r3, #0
	beq	.L874
	ldr	r1, .L888+16
	movw	r2, #553
	ldr	r0, .L888+20
	bl	printk
	bl	dump_stack
	b	.L874
.L856:
	movw	r2, #2828
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	beq	.L874
	ldr	r2, [r1, #1092]
	ldrh	r0, [r2, #48]
	cmp	r0, r5
	beq	.L874
	ldrh	r0, [r2, #16]
	cmp	r0, r5
	beq	.L874
	ldrh	r0, [r2, #80]
	cmp	r0, r5
	beq	.L874
	ldr	r0, .L888+12
	add	r1, r1, #2880
	ldrh	lr, [r0, #52]
	mov	r0, #0
.L857:
	cmp	r0, lr
	bcs	.L887
	ldrh	r6, [r1, #2]!
	cmp	r6, r5
	beq	.L874
	add	r0, r0, #1
	b	.L857
.L887:
	cmp	r4, #0
	bne	.L862
	ldr	r0, .L888+28
	ldrh	r6, [r0, #-8]
	cmp	r6, r5
	moveq	r0, r4
	beq	.L855
	sub	r0, r0, #4
	mov	r1, r4
.L861:
	ldrh	r10, [r1, r0]
	cmp	r5, r10
	bne	.L860
	ldr	r0, [r8]
	ands	r0, r0, #256
	beq	.L855
	stmia	sp, {r5, r6}
	mov	r1, r5
	mov	r2, ip, lsr #5
	ldr	r0, .L888+32
	bl	printk
	b	.L874
.L860:
	add	r1, r1, #2
	cmp	r1, #16
	bne	.L861
	add	r6, r2, r9, asl #7
	add	r6, r6, #136
	b	.L863
.L862:
	add	r6, r2, #392
.L863:
	ldr	r2, [r8]
	tst	r2, #256
	beq	.L864
	stmia	sp, {r3, lr}
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	mov	r1, r5
	ldr	r0, .L888+36
	mov	r2, r4
	str	r3, [sp, #8]
	mov	r3, ip, lsr #5
	bl	printk
.L864:
	add	r1, r6, #128
	mov	r3, r6
	movw	r0, #65535
.L867:
	mov	r2, r3
	add	r3, r3, #2
	ldrh	ip, [r2]
	cmp	ip, r0
	bne	.L865
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
	b	.L886
.L865:
	cmp	r3, r1
	bne	.L867
.L886:
	mov	r0, #1
	b	.L855
.L874:
	mov	r0, #0
.L855:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L889:
	.align	2
.L888:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC93
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+1608
	.word	.LC0
	.word	.LC94
	.word	.LANCHOR3-3216
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
	ldr	r3, .L896
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	sub	r2, r3, #3200
	ldr	r5, .L896+4
	mov	r6, #1
	ldrh	r1, [r2, #-4]
	mov	r7, r0
	add	r5, r5, #4096
	ldrb	r2, [r5, #-2907]	@ zero_extendqisi2
	mov	r0, r0, lsr r1
	rsb	r2, r2, #24
	uxth	r4, r0
	rsb	r2, r1, r2
	ldrb	r1, [r3, #-3202]	@ zero_extendqisi2
	mov	r2, r6, asl r2
	sub	r2, r2, #1
	and	r0, r0, r2
	bl	__aeabi_uidiv
	mov	r3, r7
	ldr	r1, [r5, #848]
	mov	r2, r4
	uxth	r8, r0
	ldr	r0, .L896+8
	bl	printk
	mov	r1, r6
	mov	r0, r8
	mov	r2, #0
	bl	gc_add_sblk
	ldr	r3, [r5, #848]
	ldr	r0, .L896+12
	mov	r1, #0
.L891:
	uxth	r2, r1
	cmp	r2, r3
	bcs	.L895
	add	r2, r2, #1056
	add	r1, r1, #1
	add	r2, r2, #4
	mov	r2, r2, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r4
	bne	.L891
	b	.L892
.L895:
	cmp	r3, #5
	bhi	.L892
	add	r2, r3, #1
	add	r3, r3, #1056
	add	r3, r3, #4
	str	r2, [r5, #848]
	ldr	r2, .L896+12
	mov	r3, r3, asl #1
	strh	r4, [r2, r3]	@ movhi
.L892:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L897:
	.align	2
.L896:
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
	ldr	r3, .L899
	ldr	r3, [r3, #-3200]
	ldr	r0, [r3, r0, asl #2]
	bx	lr
.L900:
	.align	2
.L899:
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
	ldr	r5, .L932
	mov	r6, r5
.L902:
	ldrb	fp, [r5, #2777]	@ zero_extendqisi2
	cmp	fp, #255
	beq	.L931
	mov	r3, #48
	mul	r3, r3, fp
	add	r2, r5, r3
	ldrb	r1, [r2, #1236]	@ zero_extendqisi2
	strb	r1, [r5, #2777]
	ldr	r1, .L932+4
	add	r3, r1, r3
	ldr	r1, [r2, #1272]
	cmp	r1, #0
	ldrh	r8, [r3, #32]
	beq	.L903
	ldr	r3, .L932+8
	mov	r0, #1
	ldr	r2, [r2, #1260]
	strh	r0, [r3]	@ movhi
	ldr	r0, .L932+12
	str	r2, [r3, #4]
	bl	printk
	ldr	r1, .L932+16
	mov	r2, #956
	ldr	r0, .L932+20
	bl	printk
	bl	dump_stack
.L903:
	ldr	r7, .L932+24
	ldrb	r3, [r7, #-3196]	@ zero_extendqisi2
	mov	r10, r7
	cmp	r3, #3
	bne	.L904
	ldrb	r3, [r6, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L904
	ldr	r3, [r5, #1092]
	ldrb	r2, [r7, #-3195]	@ zero_extendqisi2
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	cmp	r2, r3
	movhi	r3, #1
	bhi	.L906
	cmp	r3, #3
	movcs	r3, #2
.L906:
	add	r4, r3, r3, asl #1
	b	.L907
.L904:
	ldrb	r3, [r7, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L907
	ldr	r3, .L932+24
	ldrb	r3, [r3, #-3193]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
.L907:
	mov	r2, #48
	ldr	r1, [r7, #-3192]
	mla	r2, r2, fp, r6
	mov	r9, r8
	ldrb	r1, [r1, r8]	@ zero_extendqisi2
	ldrb	r2, [r2, #1237]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L908
	ldr	r1, .L932+16
	mov	r2, #976
	ldr	r0, .L932+20
	bl	printk
	bl	dump_stack
.L908:
	mov	r2, #48
	mla	fp, r2, fp, r6
	ldrb	r2, [fp, #1281]	@ zero_extendqisi2
	cmp	r2, #3
	beq	.L909
	ldrb	r1, [r7, #-3196]	@ zero_extendqisi2
	ldr	r2, .L932+24
	cmp	r1, #3
	bne	.L909
	ldrb	r1, [r2, #-3188]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L909
	ldrb	r1, [r5, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L909
	ldrb	r1, [r5, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L909
	ldrb	r1, [r2, #-3194]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L910
	ldrb	r2, [r2, #-3193]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L909
.L910:
	ldr	r2, [r7, #-3192]
	mov	r3, #0
	mov	r1, #48
	mov	r0, r3
	add	ip, r2, r8
	b	.L911
.L909:
	ldr	r2, .L932+28
	mvn	r7, #0
	mov	fp, #48
	strh	r8, [r2]	@ movhi
	mov	r2, #0
.L912:
	rsb	r1, r8, r9
	uxth	r1, r1
	cmp	r1, r4
	bcs	.L902
	ldr	r1, [r10, #-3192]
	ldr	r3, .L932+4
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
	b	.L912
.L911:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L902
	ldrb	r2, [ip, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	mla	r2, r1, r2, r6
	strb	r0, [r2, #1281]
	b	.L911
.L931:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L933:
	.align	2
.L932:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+4936
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
	ldr	r1, .L944
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r1, #1092]
	ldr	ip, .L944+4
	ldrh	r2, [r3, #124]
	ldrb	r0, [ip, #-3187]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r3, r3, #392
	movne	r2, #1
	bne	.L936
	add	lr, r3, r0, asl #1
	ldrh	lr, [lr, #120]
	cmp	lr, #0
	beq	.L941
	add	r3, r3, r0, asl #7
	add	r3, r3, #136
.L936:
	add	lr, r3, #128
	movw	r4, #65535
.L940:
	mov	r5, r3
	ldrh	r0, [r3], #2
	cmp	r0, r4
	beq	.L938
	cmp	r2, #0
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	ldreqb	r2, [ip, #-3187]	@ zero_extendqisi2
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
.L938:
	cmp	r3, lr
	bne	.L940
	ldmfd	sp!, {r4, r5, pc}
.L941:
	movw	r0, #65535
	ldmfd	sp!, {r4, r5, pc}
.L945:
	.align	2
.L944:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_get_src_blk, .-gc_get_src_blk
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
	ldr	r5, .L1069
	str	r0, [sp, #12]
	str	r1, [sp, #20]
	ldr	r9, [r5, #1092]
	str	r2, [sp, #8]
	add	r3, r9, r0, asl #1
	ldrh	r4, [r3, #120]
	cmp	r4, #0
	bne	.L1001
	ldr	r7, .L1069+4
	ldrh	r8, [r7, #52]
	cmp	r8, #1
	ldrls	r3, .L1069+8
	strlsh	r4, [r3, #-2]	@ movhi
	strlsh	r4, [r3], #16	@ movhi
	strlsh	r4, [r3, #-14]	@ movhi
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L1068
	ldr	r7, [sp, #12]
	ldr	r9, .L1069+8
	ldr	fp, .L1069+12
	mov	r4, r7
.L950:
	ldr	r3, [sp, #8]
	add	r8, r3, #1
	uxth	r3, r7
	cmp	r3, r8
	bge	.L956
	ldrh	r10, [r9]
	ldr	r3, .L1069+8
	mov	r1, r10
	add	r0, r3, #4
	str	r3, [sp, #16]
	bl	_list_get_gc_head_node
	add	r2, r10, #1
	movw	r1, #65535
	uxth	r2, r2
	strh	r2, [r9]	@ movhi
	ldr	r3, [sp, #16]
	cmp	r0, r1
	mov	r6, r0
	mov	ip, r3
	beq	.L952
	ldr	r3, [fp]
	mov	r10, r0, asl #1
	tst	r3, #256
	beq	.L953
	ldr	r3, [r5, #1088]
	mov	r1, r6
	ldr	r0, .L1069+16
	ldrh	r3, [r3, r10]
	bl	printk
	ldr	ip, [sp, #16]
.L953:
	ldr	r3, [r5, #1088]
	ldrh	r2, [r3, r10]
	ldr	r3, .L1069+20
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L954
	mov	r1, #0
	mov	r0, r6
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L955
	add	r4, r4, #1
	ldr	r3, [sp, #8]
	uxth	r4, r4
	cmp	r4, r3
	bcc	.L955
	b	.L956
.L954:
	mov	r3, #0
	strh	r3, [ip]	@ movhi
	b	.L956
.L952:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L956
.L955:
	add	r7, r7, #1
	b	.L950
.L956:
	ldr	r3, [sp, #20]
	tst	r3, #2
	beq	.L958
	movw	r3, #2798
	ldrh	r3, [r5, r3]
	cmp	r3, #32
	bls	.L958
	ldr	r9, .L1069+24
	mov	fp, #0
	ldr	r7, .L1069+28
.L959:
	uxth	r3, fp
	ldr	r6, .L1069+24
	cmp	r3, r8
	bge	.L963
	ldrh	r10, [r7]
	ldr	r3, .L1069+28
	mov	r1, r10
	add	r0, r3, #6
	str	r3, [sp, #16]
	bl	_list_get_gc_head_node
	movw	r1, #65535
	add	r10, r10, #1
	strh	r10, [r7]	@ movhi
	cmp	r0, r1
	ldr	r3, [sp, #16]
	beq	.L960
	ldr	r10, [r5, #1088]
	mov	r2, r0, asl #1
	ldrh	r1, [r10, r2]
	ldrh	r2, [r9]
	cmp	r1, r2
	bcs	.L960
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L962
	add	r4, r4, #1
	ldr	r3, [sp, #8]
	uxth	r4, r4
	cmp	r4, r3
	bcc	.L962
	b	.L963
.L960:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L963
.L962:
	add	fp, fp, #1
	b	.L959
.L963:
	ldr	r3, [sp, #8]
	ldr	r2, .L1069+32
	cmp	r4, r3
	bcs	.L965
	ldrh	ip, [r2, #-2]
	ldrb	r3, [r2, #-27]	@ zero_extendqisi2
	ldrh	r0, [r2, #-4]
	ldrh	r1, [r6]
	mul	r3, r3, ip
	sub	r3, r3, r0, lsr #2
	cmp	r1, r3
	ldrlt	r3, .L1069+24
	addlt	r1, r1, r0, lsr #3
	strlth	r1, [r3]	@ movhi
	b	.L958
.L965:
	ldrh	r2, [r2, #-4]
	ldrh	r3, [r6]
	cmp	r3, r2
	subhi	r3, r3, r2, lsr #3
	ldrhi	r2, .L1069+24
	strhih	r3, [r2]	@ movhi
.L958:
	ldr	r3, [sp, #20]
	tst	r3, #1
	beq	.L966
	ldrh	r6, [sp, #8]
	cmp	r4, r6
	bcs	.L966
	ldr	r7, .L1069+36
	mov	r10, #0
	movw	r8, #65535
.L971:
	ldrh	r9, [r7]
	ldr	fp, .L1069+36
	mov	r1, r9
	add	r0, fp, #18
	bl	_list_get_gc_head_node
	add	r9, r9, #1
	strh	r9, [r7]	@ movhi
	cmp	r0, r8
	beq	.L967
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L968
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r6
	bcc	.L968
	b	.L969
.L967:
	mov	r3, #0
	strh	r3, [fp]	@ movhi
	b	.L970
.L968:
	add	r10, r10, #1
	uxth	r3, r10
	cmp	r3, r6
	bcc	.L971
.L970:
	cmp	r4, r6
	bcs	.L969
	ldr	r1, .L1069+32
	movw	r2, #2814
	ldrh	r3, [r5, r2]
	ldrh	r1, [r1, #-4]
	cmp	r3, r1
	addcc	r3, r3, r1, lsr #3
	bcc	.L1067
	b	.L966
.L969:
	ldr	r1, .L1069+32
	movw	r2, #2814
	ldrh	r3, [r5, r2]
	ldrh	r1, [r1, #-4]
	cmp	r3, r1, lsr #1
	bls	.L966
	sub	r3, r3, r1, lsr #3
.L1067:
	ldr	r1, .L1069
	b	.L1066
.L1068:
	ldr	r3, .L1069+40
	sub	r3, r3, #3168
	ldrb	r1, [r3, #-27]	@ zero_extendqisi2
	ldrh	r2, [r3, #-2]
	smulbb	r2, r1, r2
	uxth	r3, r2
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	cmp	r3, #1
	bne	.L972
	cmp	r8, #0
	beq	.L972
	ldrh	r2, [r9, #80]
	movw	r3, #65535
	cmp	r2, r3
	movwne	r3, #2102
	ldrneh	r2, [r7, r3]
	ldrne	r3, [sp, #16]
	rsbne	r2, r2, r3
	uxthne	r3, r2
	strne	r3, [sp, #16]
	mov	r3, #8
	str	r3, [sp, #8]
.L972:
	movw	r3, #2106
	mov	r6, #0
	strh	r6, [r7, r3]	@ movhi
	ldr	r3, [sp, #20]
	ldr	r7, .L1069+44
	ands	r3, r3, #1
	str	r3, [sp, #24]
	ldr	r4, .L1069
	strh	r6, [r7, #-12]	@ movhi
	ldreq	r6, [sp, #24]
	moveq	r4, r6
	beq	.L973
	movw	r3, #2794
	ldrh	r2, [r7, #-10]
	ldrh	r3, [r4, r3]
	cmp	r3, r2, lsr #2
	bhi	.L974
	movw	r2, #2796
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcs	r4, r6
	bcs	.L975
.L974:
	cmp	r8, #1
	bls	.L976
.L978:
	mov	r4, #0
	b	.L977
.L976:
	ldr	r4, .L1069+8
	mov	r1, #0
	mov	r3, r3, lsr #2
	add	r6, r4, #16
	strh	r3, [r4, #-2]	@ movhi
	mov	r0, r6
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L978
	ldr	r1, [r5, #1088]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, #12]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #2
	bcs	.L978
	mov	r1, #0
	ldr	r2, [sp, #12]
	mov	r3, #1
	strh	r3, [r4, #-2]	@ movhi
	bl	gc_add_sblk
	adds	r4, r0, #0
	movne	r4, #1
.L977:
	ldr	r9, .L1069+36
	mov	r8, #64
	mov	r6, #0
.L981:
	ldrh	fp, [r9]
	ldr	r3, .L1069+36
	mov	r1, fp
	add	r0, r3, #18
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	mov	r10, r0
	ldr	r3, [sp, #28]
	beq	.L979
	mov	r1, #0
	ldr	r2, [sp, #12]
	add	fp, fp, #1
	strh	fp, [r9]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L980
	ldr	r3, [r5, #1088]
	mov	r10, r10, asl #1
	add	r4, r4, #1
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, r10]
	uxth	r4, r4
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r6, r3
	cmpls	r4, r2
	bcs	.L975
	ldr	r2, .L1069+48
	ldr	r3, .L1069+52
	ldrh	r2, [r2]
	ldrh	r3, [r3]
	cmp	r3, r2, asl #1
	ble	.L980
	b	.L975
.L979:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L975
.L980:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L981
.L975:
	movw	r3, #2796
	ldrh	r2, [r5, r3]
	ldrh	r3, [r7, #-10]
	cmp	r2, r3, lsr #2
	bhi	.L1002
	ldr	r1, .L1069
	movw	r3, #2794
	ldrh	r3, [r1, r3]
	add	r3, r3, #8
	cmp	r2, r3
	bgt	.L1002
.L973:
	ldr	r3, [sp, #20]
	tst	r3, #2
	beq	.L985
	movw	r3, #2798
	ldrh	r2, [r5, r3]
	ldr	r3, [sp, #16]
	cmp	r6, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r2, #32
	movls	r3, #0
	cmp	r3, #0
	beq	.L985
	ldr	r8, .L1069+28
	mov	r7, #64
	mov	r5, #0
.L991:
	ldrh	fp, [r8]
	ldr	r9, .L1069+28
	ldr	r10, .L1069
	add	r0, r9, #6
	mov	r1, fp
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L986
	ldr	r3, [sp, #8]
	add	fp, fp, #1
	strh	fp, [r8]	@ movhi
	cmp	r3, #1
	bne	.L987
	ldr	r3, .L1069+40
	ldrb	r2, [r3, #-3195]	@ zero_extendqisi2
	ldrh	r3, [r9, #12]
	smulbb	r3, r2, r3
	ldrh	r2, [r9, #10]
	sub	r3, r3, r2, lsr #3
	ldr	r2, .L1069+56
	strh	r3, [r2]	@ movhi
.L987:
	ldr	r3, [r10, #1088]
	mov	r9, r0, asl #1
	ldrh	r2, [r3, r9]
	ldr	r3, .L1069+56
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L988
	mov	r1, #0
	ldr	r2, [sp, #12]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L989
	ldr	r3, .L1069
	add	r4, r4, #1
	ldr	r2, [sp, #8]
	add	r5, r5, #1
	uxth	r4, r4
	ldr	r3, [r3, #1088]
	uxth	r5, r5
	ldrh	r3, [r3, r9]
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r6, r3
	cmpls	r4, r2
	bcc	.L989
	b	.L990
.L988:
	ldr	r3, .L1069+32
	mov	r2, #0
	strh	r2, [r3, #-14]	@ movhi
	b	.L990
.L986:
	mov	r3, #0
	strh	r3, [r9]	@ movhi
	b	.L990
.L989:
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L991
.L990:
	ldr	r3, [sp, #8]
	ldr	r2, .L1069+32
	cmp	r4, r3
	add	ip, r2, #3168
	bcc	.L992
	cmp	r5, #0
	bne	.L993
	ldr	r0, .L1069
	sub	r3, ip, #3152
	movw	r1, #2798
	ldrh	r3, [r3, #-8]
	ldrh	r1, [r0, r1]
	cmp	r1, r3
	bls	.L993
.L992:
	ldrh	r3, [r2, #-4]
	movw	r1, #2810
	ldrh	lr, [r2, #-2]
	ldrb	r2, [ip, #-3195]	@ zero_extendqisi2
	ldrh	r0, [r10, r1]
	mov	r3, r3, lsr #3
	mul	r2, r2, lr
	rsb	r2, r3, r2
	cmp	r0, r2
	ldrlt	r2, .L1069
	addlt	r3, r3, r0
	strlth	r3, [r2, r1]	@ movhi
	b	.L985
.L993:
	ldrh	r0, [r2, #-4]
	movw	r1, #2810
	ldrb	r2, [ip, #-3195]	@ zero_extendqisi2
	ldrh	r3, [r10, r1]
	mul	r2, r2, r0
	cmp	r3, r2
	subgt	r3, r3, r0, lsr #3
	bgt	.L1065
	b	.L966
.L985:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L966
	ldrh	r8, [sp, #8]
	ldr	r3, [sp, #16]
	cmp	r4, r8
	cmpcc	r6, r3
	bcs	.L966
	ldr	r9, .L1069+8
	mov	r5, #64
	ldr	fp, .L1069
.L999:
	ldrh	r7, [r9]
	ldr	r3, .L1069+8
	ldr	r10, .L1069
	add	r0, r3, #4
	mov	r1, r7
	str	r3, [sp, #24]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r3, [sp, #24]
	cmp	r0, r2
	add	ip, r3, #3184
	beq	.L995
	ldr	r3, [fp, #1088]
	add	r7, r7, #1
	strh	r7, [r9]	@ movhi
	mov	r7, r0, asl #1
	ldrh	r2, [r3, r7]
	ldr	r3, .L1069+60
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L996
	ldr	r3, .L1069+64
	ldrh	r2, [r3]
	ldr	r3, .L1069+52
	ldrh	r3, [r3]
	cmp	r3, r2, lsr #1
	bls	.L997
.L996:
	mov	r1, #0
	ldr	r2, [sp, #12]
	str	ip, [sp, #24]
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	ip, [sp, #24]
	beq	.L998
	ldr	r3, [fp, #1088]
	add	r4, r4, #1
	uxth	r4, r4
	ldrh	r3, [r3, r7]
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r6, r3
	cmpls	r4, r8
	bcc	.L998
	b	.L997
.L995:
	cmp	r7, #64
	movhi	r2, #0
	strhih	r2, [r3]	@ movhi
	b	.L997
.L998:
	sub	r5, r5, #1
	uxth	r5, r5
	cmp	r5, #0
	bne	.L999
.L997:
	cmp	r4, r8
	ldr	r0, .L1069+32
	movw	r1, #2812
	bcs	.L1000
	ldrh	r2, [r0, #-4]
	ldrh	r3, [r10, r1]
	cmp	r3, r2, lsr #1
	bls	.L966
	sub	r3, r3, r2, lsr #3
.L1065:
	ldr	r2, .L1069
.L1066:
	strh	r3, [r2, r1]	@ movhi
	b	.L966
.L1000:
	ldrh	r0, [r0, #-4]
	ldrb	r3, [ip, #-3195]	@ zero_extendqisi2
	ldrh	r2, [r10, r1]
	mul	r3, r3, r0
	sub	r3, r3, #32
	cmp	r2, r3
	addlt	r2, r2, r0, lsr #3
	ldrlt	r3, .L1069
	strlth	r2, [r3, r1]	@ movhi
.L966:
	ldr	r3, .L1069+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1001
	ldr	r3, [sp, #8]
	ldr	r0, .L1069+68
	ldr	r1, [sp, #12]
	str	r3, [sp]
	mov	r3, r4
	ldr	r2, [sp, #20]
	bl	printk
.L1001:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1002:
	ldr	r3, [sp, #16]
	cmp	r6, r3
	bcs	.L973
	ldr	r8, .L1069+8
	mov	r7, #64
	movw	r9, #65535
.L984:
	ldrh	r10, [r8]
	ldr	r3, .L1069+8
	mov	r1, r10
	add	r0, r3, #4
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	cmp	r0, r9
	mov	fp, r0
	ldr	r3, [sp, #28]
	beq	.L982
	mov	r1, #0
	ldr	r2, [sp, #12]
	add	r10, r10, #1
	strh	r10, [r8]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L983
	ldr	r3, [r5, #1088]
	mov	fp, fp, asl #1
	add	r4, r4, #1
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, fp]
	uxth	r4, r4
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r6, r3
	cmpls	r4, r2
	bcc	.L983
	b	.L973
.L982:
	cmp	r10, #64
	movhi	r2, #0
	strhih	r2, [r3]	@ movhi
	b	.L973
.L983:
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L984
	b	.L973
.L1070:
	.align	2
.L1069:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3-3184
	.word	.LANCHOR2
	.word	.LC99
	.word	.LANCHOR0+2814
	.word	.LANCHOR0+2816
	.word	.LANCHOR3-3182
	.word	.LANCHOR3-3168
	.word	.LANCHOR3-3186
	.word	.LANCHOR3
	.word	.LANCHOR3-3152
	.word	.LANCHOR0+2794
	.word	.LANCHOR0+2796
	.word	.LANCHOR0+2810
	.word	.LANCHOR0+2812
	.word	.LANCHOR3-3162
	.word	.LC100
	.fnend
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	gc_free_temp_buf
	.type	gc_free_temp_buf, %function
gc_free_temp_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L1086
	ldrb	r0, [r5, #2835]	@ zero_extendqisi2
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r5, #2774]	@ zero_extendqisi2
	cmp	r3, #1
	bhi	.L1078
	ldr	r2, .L1086+4
	movw	r3, #2104
	ldr	r1, .L1086+8
	ldrh	r4, [r2, r3]
	sub	r3, r1, #3168
	mov	r6, r1
	ldrh	r0, [r3, #-2]
	add	r2, r4, #24
	ldrb	r3, [r1, #-3195]	@ zero_extendqisi2
	mul	r3, r3, r0
	ldr	r0, [r1, #-3192]
	mov	r1, #48
	cmp	r3, r2
	movcs	r3, r2
.L1073:
	cmp	r4, r3
	bcs	.L1078
	ldrb	r2, [r0, r4]	@ zero_extendqisi2
	cmp	r2, #255
	beq	.L1074
	mul	r2, r1, r2
	add	ip, r5, r2
	ldrb	ip, [ip, #1281]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L1074
	ldr	r0, .L1086+12
	add	r0, r0, r2
	bl	zbuf_free
	ldr	r3, .L1086+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1075
	ldr	r3, [r6, #-3192]
	mov	r1, r4
	ldr	r0, .L1086+20
	ldrb	r2, [r3, r4]	@ zero_extendqisi2
	bl	printk
.L1075:
	ldr	r3, [r6, #-3192]
	mvn	r2, #0
	mov	r0, #1
	strb	r2, [r3, r4]
	ldrb	r3, [r5, #2835]	@ zero_extendqisi2
	add	r3, r3, r2
	strb	r3, [r5, #2835]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1074:
	add	r4, r4, #1
	b	.L1073
.L1078:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1087:
	.align	2
.L1086:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR2
	.word	.LC101
	.fnend
	.size	gc_free_temp_buf, .-gc_free_temp_buf
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
	ldr	r0, .L1090
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
	ldr	r0, .L1090+4
	bl	printk
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1091:
	.align	2
.L1090:
	.word	.LANCHOR0
	.word	.LC102
	.fnend
	.size	print_gc_debug_info, .-print_gc_debug_info
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
	ldreq	r0, .L1096
	beq	.L1095
	cmp	r1, #2
	ldreq	r0, .L1096+4
	ldrne	r0, .L1096+8
.L1095:
	mov	r1, r3
	b	_list_get_gc_head_node
.L1097:
	.align	2
.L1096:
	.word	.LANCHOR3-3176
	.word	.LANCHOR3-3168
	.word	.LANCHOR3-3180
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
	ldr	r3, .L1102
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1102+4
	ldreq	r2, .L1102+8
	beq	.L1101
.L1099:
	cmp	r3, #16
	ldreq	r0, .L1102+12
	ldreq	r2, .L1102+16
	ldrne	r0, .L1102+20
	ldrne	r2, .L1102+24
.L1101:
	b	_insert_free_list
.L1103:
	.align	2
.L1102:
	.word	.LANCHOR0
	.word	.LANCHOR3-3156
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3152
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3148
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
	ldr	r3, .L1109
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1109+4
	ldreq	r2, .L1109+8
	beq	.L1108
.L1105:
	cmp	r3, #96
	ldreq	r0, .L1109+12
	ldreq	r2, .L1109+16
	beq	.L1108
.L1106:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1109+20
	ldr	r2, .L1109+24
.L1108:
	b	_insert_data_list
.L1110:
	.align	2
.L1109:
	.word	.LANCHOR0
	.word	.LANCHOR3-3168
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3180
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3176
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
	ldr	r8, .L1126
	mov	r7, r0
	ldr	r3, [r8, #1092]
	add	r3, r3, #588
	ldrh	r4, [r3]
	clz	r3, r0
	cmp	r4, r2
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L1112
	ldr	r0, .L1126+4
	mov	r1, r4
	bl	printk
	ldr	r3, [r8, #1092]
	mvn	r2, #0
	add	r3, r3, #588
	strh	r2, [r3]	@ movhi
	b	.L1113
.L1112:
	movw	r3, #2790
	mov	r6, r1
	ldrh	r1, [r8, r3]
	movw	r3, #2792
	ldrh	r3, [r8, r3]
	mov	r5, r8
	cmp	r1, r3
	bcc	.L1114
	movw	r2, #2788
	ldrh	r2, [r8, r2]
	cmp	r3, #0
	cmpne	r2, r1
	bls	.L1115
.L1114:
	cmp	r7, #0
	ldr	r0, .L1126+8
	rsbne	r1, r1, r1, asl #3
	moveq	r1, r3, lsr #2
	ubfxne	r1, r1, #3, #16
	ldr	r2, .L1126+12
	b	.L1125
.L1115:
	cmp	r0, #0
	ldr	r2, .L1126+16
	movne	r1, r1, lsr #3
	moveq	r1, r0
	ldr	r0, .L1126+20
.L1125:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1120
	movw	r3, #2788
	ldr	r0, .L1126+24
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r6
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1126+28
	ldr	r3, [r3, #-3156]
	bl	printk
.L1120:
	cmp	r7, #0
	beq	.L1113
	ldr	r3, .L1126+32
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1113
	ldr	r2, [r5, #1080]
	mov	r0, r4, asl #2
	add	r3, r2, r0
	ldr	r1, [r2, r4, asl #2]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #11, #8
	str	r1, [sp]
	ldrh	r2, [r2, r0]
	ldr	r0, .L1126+36
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
.L1113:
	mov	r0, r4
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1127:
	.align	2
.L1126:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR3-3148
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3152
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
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r1
	ldr	r5, .L1143
	bne	.L1129
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	movw	r3, #2792
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	bcc	.L1130
	movw	r3, #2788
	ldrh	r3, [r5, r3]
	cmp	r2, #0
	cmpne	r3, r1
	movhi	r1, #1
	movls	r1, #0
	bls	.L1131
.L1130:
	ldr	r0, .L1143+4
	mov	r1, r2, lsr #1
	b	.L1142
.L1131:
	ldr	r0, .L1143+8
	ldr	r2, .L1143+12
	b	.L1141
.L1129:
	ldr	r2, [r5, #1092]
	movw	r7, #590
	ldrh	r4, [r2, r7]
	movw	r2, #65535
	cmp	r1, #1
	cmpne	r4, r2
	beq	.L1133
	ldr	r0, .L1143+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1092]
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1134
.L1133:
	movw	r2, #2788
	movw	r1, #2792
	ldrh	r2, [r5, r2]
	mov	r3, r0
	ldrh	r1, [r5, r1]
	cmp	r2, r1
	bcc	.L1135
	movw	r0, #2790
	ldrh	r0, [r5, r0]
	cmp	r1, #0
	cmpne	r0, r2
	bls	.L1136
.L1135:
	cmp	r6, #1
	ldr	r0, .L1143+4
	moveq	r3, r1, lsr #1
	mov	r1, r3
.L1142:
	ldr	r2, .L1143+20
	b	.L1141
.L1136:
	cmp	r6, #1
	ldr	r0, .L1143+24
	moveq	r3, r2, lsr #1
	ldr	r2, .L1143+28
	mov	r1, r3
.L1141:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1134
	movw	r3, #2788
	ldr	r0, .L1143+32
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r6
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1143+36
	ldr	r3, [r3, #-3156]
	bl	printk
.L1134:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1144:
	.align	2
.L1143:
	.word	.LANCHOR0
	.word	.LANCHOR3-3148
	.word	.LANCHOR3-3152
	.word	.LANCHOR0+2790
	.word	.LC106
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3156
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
	ldr	r3, .L1150
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1150+4
	ldreq	r2, .L1150+8
	beq	.L1149
.L1146:
	cmp	r3, #96
	ldreq	r0, .L1150+12
	ldreq	r2, .L1150+16
	beq	.L1149
.L1147:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1150+20
	ldr	r2, .L1150+24
.L1149:
	b	_list_remove_node
.L1151:
	.align	2
.L1150:
	.word	.LANCHOR0
	.word	.LANCHOR3-3168
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3180
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3176
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
	ldr	r3, .L1156
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1156+4
	ldreq	r2, .L1156+8
	beq	.L1155
.L1153:
	cmp	r3, #16
	ldreq	r0, .L1156+12
	ldreq	r2, .L1156+16
	ldrne	r0, .L1156+20
	ldrne	r2, .L1156+24
.L1155:
	b	_list_remove_node
.L1157:
	.align	2
.L1156:
	.word	.LANCHOR0
	.word	.LANCHOR3-3156
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3152
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3148
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
	ldr	r3, .L1163
	mov	r1, r0
	ldr	r3, [r3, #1080]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1163+4
	ldreq	r2, .L1163+8
	beq	.L1162
.L1159:
	cmp	r3, #96
	ldreq	r0, .L1163+12
	ldreq	r2, .L1163+16
	beq	.L1162
.L1160:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1163+20
	ldr	r2, .L1163+24
.L1162:
	b	_list_update_data_list
.L1164:
	.align	2
.L1163:
	.word	.LANCHOR0
	.word	.LANCHOR3-3168
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3180
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3176
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
	ldr	r0, .L1170
	bl	printk
	ldr	r4, [r4]
	cmp	r4, #0
	beq	.L1165
	ldr	r6, .L1170+4
	mov	r5, #0
	ldr	r7, .L1170+8
.L1168:
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
	ldr	r0, .L1170+12
	str	r1, [sp, #24]
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1165
	ldr	r3, [r6, #1036]
	mov	r4, #6
	add	r5, r5, #1
	mla	r4, r4, r2, r3
	ldr	r3, .L1170+16
	uxth	r5, r5
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L1168
.L1165:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1171:
	.align	2
.L1170:
	.word	.LC107
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC108
	.word	.LANCHOR3-3144
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
	ldr	r5, .L1174
	ldr	r4, .L1174+4
	add	r1, r5, #2784
	sub	r6, r4, #3152
	add	r1, r1, #4
	sub	r0, r6, #4
	bl	print_list_info
	add	r1, r5, #2784
	mov	r0, r6
	add	r1, r1, #6
	bl	print_list_info
	sub	r0, r4, #3136
	add	r1, r5, #2784
	sub	r4, r4, #3168
	sub	r0, r0, #12
	add	r1, r1, #8
	bl	print_list_info
	add	r1, r5, #2784
	mov	r0, r4
	add	r1, r1, #10
	bl	print_list_info
	add	r1, r5, #2784
	sub	r0, r4, #12
	add	r1, r1, #12
	bl	print_list_info
	add	r1, r5, #2784
	sub	r0, r4, #8
	add	r1, r1, #14
	ldmfd	sp!, {r4, r5, r6, lr}
	b	print_list_info
.L1175:
	.align	2
.L1174:
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
	ldr	r3, .L1181
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
.L1182:
	.align	2
.L1181:
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
	ldr	r8, .L1193
	mov	r10, r0
	ldr	r9, .L1193+4
	mov	r6, r1
	ldr	r2, .L1193+8
	mov	r7, r5
	ldr	r4, [r8, #1080]
	add	r4, r4, r0, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
.L1184:
	ldrb	r1, [r9, #-3195]	@ zero_extendqisi2
	cmp	r7, r1
	ldr	r1, .L1193+4
	bge	.L1191
	mov	r1, r3, asr r7
	tst	r1, #1
	bne	.L1185
	ldrb	r4, [r9, #-3202]	@ zero_extendqisi2
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
	ldrb	r1, [r9, #-3202]	@ zero_extendqisi2
	cmp	r1, #1
	subhi	r1, r1, #1
	andhi	r1, r1, r7
	addhi	r4, r4, r1
	strhih	r4, [r6, fp]	@ movhi
.L1185:
	add	r7, r7, #1
	b	.L1184
.L1191:
	add	r6, r6, r5, asl #1
	mov	r0, r5
	mvn	r2, #0
.L1188:
	ldrb	r3, [r1, #-3195]	@ zero_extendqisi2
	cmp	r5, r3
	strlth	r2, [r6], #2	@ movhi
	addlt	r5, r5, #1
	blt	.L1188
.L1192:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1194:
	.align	2
.L1193:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3204
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
	ldr	r3, .L1201
	ldrb	r4, [r3, #1189]	@ zero_extendqisi2
	ldr	r3, .L1201+4
	rsb	r4, r4, #24
	sub	r2, r3, #3200
	sub	r7, r3, #3136
	ldrh	r2, [r2, #-4]
	rsb	r4, r2, r4
	mvn	r2, #0
	mov	r5, r0, asr r4
	bic	r4, r0, r2, asl r4
	ldrb	r2, [r3, #-3194]	@ zero_extendqisi2
	uxtb	r5, r5
	sxth	r4, r4
	cmp	r2, #0
	beq	.L1196
	ldrb	r3, [r3, #-3193]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1196
	ldrh	r2, [r7, #-6]
	clz	r1, r1
	mov	r0, r5
	mov	r1, r1, lsr #5
	mul	r2, r2, r4
	bl	flash_erase_block_en
.L1196:
	ldrh	r2, [r7, #-6]
	mov	r0, r5
	uxtb	r1, r6
	mul	r2, r2, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	flash_erase_block_en
.L1202:
	.align	2
.L1201:
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
	ldr	r4, .L1231
	.pad #84
	sub	sp, sp, #84
	ldr	r9, .L1231+4
	mov	r8, r0
	mov	r7, r1
	mov	r6, #0
	ldr	r3, [r4, #1080]
	add	r3, r3, r10
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L1204:
	ldrb	r3, [r9, #-3140]	@ zero_extendqisi2
	cmp	r6, r3
	bge	.L1215
	ldrb	r2, [r9, #-3202]	@ zero_extendqisi2
	mov	r5, #0
	sub	fp, r2, #1
	mul	r3, r2, r8
	mul	ip, r2, r6
	str	r3, [sp, #4]
	ldr	r3, .L1231+8
	ldrh	r3, [r3]
	str	r3, [sp, #8]
	mov	r3, r5
.L1216:
	cmp	r3, r2
	bge	.L1230
	ldr	lr, [sp, #12]
	add	r1, r3, ip
	mov	r1, lr, asr r1
	tst	r1, #1
	bne	.L1205
	add	r1, sp, #80
	ldr	r0, [sp, #4]
	add	lr, r1, r5, asl #2
	and	r1, r3, fp
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	add	r5, r5, #1
	mul	r1, r0, r1
	str	r1, [lr, #-64]
.L1205:
	add	r3, r3, #1
	b	.L1216
.L1230:
	cmp	r2, #4
	bne	.L1228
	uxtb	r3, r6
	uxtb	ip, r7
	mov	fp, #0
.L1207:
	cmp	fp, r5
	beq	.L1210
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
	b	.L1207
.L1228:
	cmp	r5, #2
	bne	.L1211
	ldr	r3, .L1231+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1212
	ldrb	r3, [r9, #-3193]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1212
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
.L1212:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
	b	.L1210
.L1211:
	cmp	r5, #1
	bne	.L1210
	ldr	r3, .L1231+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1214
	ldrb	r3, [r9, #-3193]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1214
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	bl	flash_erase_block_en
.L1214:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	bl	flash_erase_block_en
.L1210:
	add	r6, r6, #1
	b	.L1204
.L1215:
	cmp	r7, #0
	ldr	r1, [r4, #1080]
	bne	.L1217
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
	b	.L1219
.L1217:
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
.L1219:
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1232:
	.align	2
.L1231:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3142
	.fnend
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1244
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
	bne	.L1235
	ldr	r1, .L1244+4
	movw	r2, #1283
	ldr	r0, .L1244+8
	bl	printk
	bl	dump_stack
.L1235:
	ldr	r3, [r4, #2804]
	movw	lr, #65535
	mov	ip, #0
.L1239:
	ldrh	r2, [r3, #136]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1236:
	cmp	r2, #63
	bgt	.L1243
	ldrh	r0, [r1, #2]!
	cmp	r0, lr
	bne	.L1241
	add	r2, r2, #1
	b	.L1236
.L1243:
	strh	ip, [r3, #136]	@ movhi
	b	.L1239
.L1241:
	add	r1, r3, r2, asl #1
	mvn	ip, #0
	strh	ip, [r1, #160]	@ movhi
	strh	r2, [r3, #136]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, ip
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1245:
	.align	2
.L1244:
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
	ldr	r3, .L1256
	ldr	r2, [r3, #2804]
	mov	r4, r3
	ldrh	r1, [r2, #138]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #138]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #63
	bls	.L1248
	ldr	r1, .L1256+4
	movw	r2, #1310
	ldr	r0, .L1256+8
	bl	printk
	bl	dump_stack
.L1248:
	ldr	r3, [r4, #2804]
	movw	ip, #65535
	mov	r0, #0
.L1252:
	ldrh	r2, [r3, #138]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1249:
	cmp	r2, #63
	bgt	.L1255
	ldrh	lr, [r1, #2]!
	cmp	lr, ip
	bne	.L1250
	add	r1, r3, r2, asl #1
	strh	r5, [r1, #160]	@ movhi
	strh	r2, [r3, #138]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, #1
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1250:
	add	r2, r2, #1
	b	.L1249
.L1255:
	strh	r0, [r3, #138]	@ movhi
	b	.L1252
.L1257:
	.align	2
.L1256:
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
	ldr	r4, .L1268
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
	bne	.L1263
	tst	r3, #24
	ldreqh	r3, [r2, #114]
	ldrneh	r3, [r2, #118]
	subeq	r3, r3, #1
	subne	r3, r3, #1
	streqh	r3, [r2, #114]	@ movhi
	strneh	r3, [r2, #118]	@ movhi
.L1263:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	bne	.L1265
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
.L1265:
	ldrh	r2, [r5, r8]
	cmp	r3, #64
	ubfx	r1, r2, #0, #11
	add	r1, r1, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r5, r8]	@ movhi
	bne	.L1266
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #122]
	sub	r3, r3, #1
	strh	r3, [r2, #122]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1266:
	cmp	r3, #96
	ldreq	r2, [r4, #2804]
	ldreqh	r3, [r2, #124]
	subeq	r3, r3, #1
	streqh	r3, [r2, #124]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1269:
	.align	2
.L1268:
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
	ldr	r3, .L1275
	ldr	r2, .L1275+4
	ldr	r5, [r3, #1092]
	sub	r3, r2, #3168
	ldrb	r2, [r2, #-3195]	@ zero_extendqisi2
	ldrh	r3, [r3, #-4]
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
	bne	.L1273
	ldr	r1, .L1275+8
	movw	r2, #1937
	ldr	r0, .L1275+12
	bl	printk
	bl	dump_stack
.L1273:
	ldr	r3, .L1275+16
	ldrh	r0, [r3, #-6]
	mla	r0, r0, r4, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1276:
	.align	2
.L1275:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1680
	.word	.LC0
	.word	.LANCHOR3-3136
	.fnend
	.size	ftl_get_ppa_from_index, .-ftl_get_ppa_from_index
	.align	2
	.global	lpa_hash_get_ppa
	.type	lpa_hash_get_ppa, %function
lpa_hash_get_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1283
	uxtb	r3, r0
	str	lr, [sp, #-4]!
	.save {lr}
	sub	r1, r2, #3136
	mov	r3, r3, asl #1
	ldr	ip, [r2, #-2620]
	ldrh	r3, [r1, r3]
	ldr	r1, [r2, #-2624]
	movw	r2, #65535
.L1278:
	cmp	r3, r2
	beq	.L1282
	ldr	lr, [r1, r3, asl #2]
	cmp	lr, r0
	bne	.L1279
	mov	r0, r3
	ldr	lr, [sp], #4
	b	ftl_get_ppa_from_index
.L1279:
	mov	r3, r3, asl #1
	ldrh	r3, [ip, r3]
	b	.L1278
.L1282:
	mvn	r0, #0
	ldr	pc, [sp], #4
.L1284:
	.align	2
.L1283:
	.word	.LANCHOR3
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
	bne	.L1286
	ldr	r1, .L1295
	mov	r2, #2080
	ldr	r0, .L1295+4
	bl	printk
	bl	dump_stack
.L1286:
	ldr	r3, .L1295+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-4]
	cmp	r2, r3
	bne	.L1287
	ldr	r1, .L1295
	movw	r2, #2081
	ldr	r0, .L1295+4
	bl	printk
	bl	dump_stack
.L1287:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L1288
	ldr	r1, .L1295
	movw	r2, #2082
	ldr	r0, .L1295+4
	bl	printk
	bl	dump_stack
.L1288:
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	movw	r2, #65535
	ldr	r1, .L1295+12
	mov	lr, #0
	add	r3, r4, r3, asl #1
	ldrb	r0, [r1, #-3195]	@ zero_extendqisi2
	ldrh	ip, [r3, #16]
.L1289:
	cmp	ip, r2
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	bne	.L1294
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
	b	.L1289
.L1294:
	ldr	r0, .L1295+16
	add	r3, r3, #1
	ldrb	r1, [r1, #-3195]	@ zero_extendqisi2
	uxtb	r3, r3
	ldrh	r2, [r4, #2]
	ldrh	r0, [r0, #-6]
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
.L1296:
	.align	2
.L1295:
	.word	.LANCHOR1+1704
	.word	.LC0
	.word	.LANCHOR3-3168
	.word	.LANCHOR3
	.word	.LANCHOR3-3136
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
	bls	.L1298
	ldr	r1, .L1300
	movw	r2, #2117
	ldr	r0, .L1300+4
	bl	printk
	bl	dump_stack
.L1298:
	ldr	r5, .L1300+8
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
.L1301:
	.align	2
.L1300:
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
	ldr	r2, .L1315
	ldrh	r3, [r2, #-8]
	cmp	r3, #4
	cmpls	r0, #0
	addeq	r3, r3, #1
	streqh	r3, [r2, #-8]	@ movhi
	bxeq	lr
	ldr	r3, .L1315+4
	movw	ip, #1076
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #0
	ldrh	lr, [r3, ip]
	movw	r6, #65535
	strh	r0, [r2, #-8]	@ movhi
	mov	ip, r0
	ldr	r1, [r3, #1088]
	ldr	r2, [r3, #1080]
	sub	r1, r1, #2
	add	lr, r2, lr, asl #2
.L1305:
	cmp	r2, lr
	beq	.L1314
	ldrh	r4, [r1, #2]!
	cmp	r4, r6
	beq	.L1306
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	and	r5, r5, #224
	cmp	r5, #160
	addeq	r0, r0, r4
	addne	ip, ip, r4
.L1306:
	add	r2, r2, #4
	b	.L1305
.L1314:
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
.L1316:
	.align	2
.L1315:
	.word	.LANCHOR3-2608
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
	ldr	r3, .L1322
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L1321
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
	beq	.L1318
	ldr	r3, .L1322+4
	mov	r4, r1
	mov	r0, r2
	str	r3, [r1]
	mov	r1, #1024
	bl	js_hash
	str	r0, [r4, #4]
.L1318:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1321:
	mov	r0, #0
	bx	lr
.L1323:
	.align	2
.L1322:
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
	ldr	r3, .L1328
	mov	r4, r4, asl #1
	ldr	r2, [r3, #1088]
	ldrh	r2, [r2, r4]
	cmp	r2, #0
	moveq	r0, #1
	movne	r0, #0
	streq	r0, [r3, #2820]
	ldmfd	sp!, {r4, pc}
.L1329:
	.align	2
.L1328:
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
	beq	.L1331
	ldr	r1, .L1340
	mov	r3, r0, asl #1
	ldr	r2, [r1, #1088]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1331
	ldr	r3, [r1, #1080]
	mov	r2, r4
	mov	r1, r5
	add	r3, r3, r0, asl #2
	ldr	r0, .L1340+4
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1331:
	ldr	r3, .L1340+8
	ldrh	r0, [r3, #-8]
	mov	r4, r3
	cmp	r0, r5
	beq	.L1337
	movw	r2, #65535
	cmp	r0, r2
	streqh	r5, [r3, #-8]	@ movhi
	beq	.L1337
	bl	ftl_vpn_update
	add	r3, r4, #608
	ldrh	r1, [r4, #-8]
	ldrh	r2, [r3, #-6]
	strh	r5, [r4, #-8]	@ movhi
	add	r2, r2, #1
	uxth	r2, r2
	adds	r0, r0, #0
	movne	r0, #1
	cmp	r2, #7
	movhi	r2, #0
	strh	r2, [r3, #-6]	@ movhi
	ldrh	r3, [r3, #-6]
	ldr	r2, .L1340+12
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1337:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1341:
	.align	2
.L1340:
	.word	.LANCHOR0
	.word	.LC109
	.word	.LANCHOR3-3216
	.word	.LANCHOR3-3220
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
	ldr	r3, .L1354
	movw	r4, #65535
	mov	r6, r6, asl #1
	mov	r8, r4
	sub	ip, r3, #3136
	ldr	r7, [r3, #-2624]
	ldrh	lr, [ip, r6]
	ldr	r9, [r3, #-2620]
.L1343:
	cmp	lr, r8
	beq	.L1347
	ldr	r5, [r7, lr, asl #2]
	add	r10, r7, lr, asl #2
	cmp	r5, r0
	mov	r5, lr, asl #1
	bne	.L1344
	mvn	lr, #0
	str	lr, [r10]
	movw	lr, #65535
	cmp	r4, lr
	ldr	lr, [r3, #-2620]
	movne	r4, r4, asl #1
	ldreqh	lr, [lr, r5]
	ldrneh	r7, [lr, r5]
	streqh	lr, [ip, r6]	@ movhi
	strneh	r7, [lr, r4]	@ movhi
	mvn	r4, #0
	ldr	lr, [r3, #-2620]
	strh	r4, [lr, r5]	@ movhi
	b	.L1347
.L1344:
	mov	r4, lr
	ldrh	lr, [r9, r5]
	b	.L1343
.L1347:
	ldr	r4, [r3, #-2624]
	cmn	r1, #1
	ldr	lr, .L1354
	str	r0, [r4, r2, asl #2]
	ldrh	r0, [ip, r6]
	ldr	r3, [r3, #-2620]
	strh	r2, [ip, r6]	@ movhi
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	beq	.L1349
	sub	r3, lr, #3200
	ldrh	r0, [r3, #-4]
	ldr	r3, .L1354+4
	mov	r1, r1, lsr r0
	ldrb	r3, [r3, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r1, r0, asl r3
	ldrb	r1, [lr, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L1349:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1355:
	.align	2
.L1354:
	.word	.LANCHOR3
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
	ldr	r1, .L1364
	ldr	r7, .L1364+4
	sub	r1, r1, #3200
	ldrb	r5, [r1, #-2]	@ zero_extendqisi2
	ldrb	r4, [r7, #1189]	@ zero_extendqisi2
	ldrh	r1, [r1, #-4]
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
	ldr	r3, .L1364+8
	ldr	r3, [r3]
	tst	r3, #16384
	beq	.L1358
	str	r9, [sp]
	mov	r1, r4
	ldr	r0, .L1364+12
	mov	r2, r6
	mov	r3, r8
	bl	printk
.L1358:
	movw	r3, #1076
	ldrh	r3, [r7, r3]
	cmp	r3, r8
	bls	.L1356
	ldr	r3, .L1364+4
	mov	r2, #1
	ldr	r4, [r3, #1080]
	add	r4, r4, r8, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
	orr	r6, r3, r2, asl r6
	strb	r6, [r4, #3]
.L1356:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L1365:
	.align	2
.L1364:
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
	ldr	r4, .L1387
	str	r0, [sp]
	ldr	r3, [r4, #848]
	cmp	r3, #0
	movne	r5, #0
	ldrne	r7, .L1387+4
	beq	.L1383
.L1367:
	ldrb	r3, [r7, #-3195]	@ zero_extendqisi2
	uxth	r0, r5
	cmp	r3, r0
	bls	.L1383
	ldrb	r9, [r7, #-3202]	@ zero_extendqisi2
	mov	fp, #0
	ldr	r6, .L1387+8
	mov	r1, r9
	uxth	r8, r9
	bl	__aeabi_idiv
	ldr	r2, .L1387+12
	ldrb	r3, [r6, #1189]	@ zero_extendqisi2
	cmp	r9, #1
	subhi	r1, r8, #1
	ldrh	r2, [r2]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	ldrh	r2, [sp]
	andhi	r1, r1, r5
	smulbb	r2, r2, r8
	ldr	r8, .L1387+16
	add	r0, r2, r0, asl r3
	mov	r9, r8
	uxth	r2, r0
	addhi	r2, r2, r1
	uxthhi	r2, r2
.L1369:
	ldr	r1, [r4, #848]
	uxth	r10, fp
	cmp	r10, r1
	bcs	.L1385
	add	r1, r10, #1056
	add	r1, r1, #4
	mov	r1, r1, asl #1
	ldrh	r1, [r8, r1]
	cmp	r1, r2
	bne	.L1370
	mov	r1, r2
	ldr	r0, .L1387+20
	str	r2, [sp, #4]
	bl	printk
	ldr	r3, .L1387+4
	ldrb	r1, [r3, #-2612]	@ zero_extendqisi2
	cmp	r1, #0
	ldr	r2, [sp, #4]
	bne	.L1371
	ldrb	r1, [r7, #-3194]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1372
.L1371:
	ldr	r1, [r6, #2804]
	ldr	r3, .L1387+24
	ldr	r1, [r1, #156]
	cmp	r1, r3
	beq	.L1373
.L1372:
	ldr	r1, .L1387+12
	str	r2, [sp, #4]
	ldrh	r0, [r1]
	mov	r0, r2, asl r0
	bl	ftl_mask_bad_block
	ldr	r2, [sp, #4]
.L1373:
	ldr	r0, [r4, #848]
.L1374:
	cmp	r10, r0
	bcs	.L1386
	add	r1, r10, #1056
	add	r3, r10, #1
	add	r1, r1, #5
	mov	r1, r1, asl #1
	ldrh	lr, [r9, r1]
	add	r1, r10, #1056
	add	r1, r1, #4
	uxth	r10, r3
	mov	r1, r1, asl #1
	strh	lr, [r9, r1]	@ movhi
	b	.L1374
.L1386:
	sub	r0, r0, #1
	str	r0, [r4, #848]
.L1370:
	add	fp, fp, #1
	b	.L1369
.L1385:
	add	r5, r5, #1
	b	.L1367
.L1383:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1388:
	.align	2
.L1387:
	.word	.LANCHOR0+4096
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3204
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
	ldr	r5, .L1413
	.pad #20
	sub	sp, sp, #20
	mov	r7, r0
	ldr	r6, [r5, #1080]
	add	r8, r6, r10
	ldrb	r4, [r8, #2]	@ zero_extendqisi2
	tst	r4, #8
	mov	r3, r4, lsr #5
	str	r3, [sp]
	beq	.L1390
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
	ldr	r1, .L1413+4
	ldrh	lr, [r1, #-2]
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #4]
	uxtah	r0, ip, r0
	ldr	r2, [sp, #12]
	cmp	r0, lr
	ble	.L1391
	movw	r3, #2798
	sub	r1, r1, #544
	ldrh	r0, [r5, r3]
	movw	r3, #2790
	ldrh	r3, [r5, r3]
	add	r0, r0, r3
	ldrh	r3, [r1, #-8]
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1397
	movw	r3, #2794
	movw	r0, #2788
	ldrh	r0, [r5, r0]
	ldrh	r3, [r5, r3]
	add	r3, r3, r0
	movw	r0, #2796
	ldrh	ip, [r5, r0]
	add	ip, r3, ip
	ldrh	r3, [r1, #-10]
	add	r3, r3, #8
	cmp	ip, r3
	b	.L1412
.L1391:
	mla	r3, r9, ip, r3
	ldrh	r0, [r1]
	cmp	r3, r0
	ble	.L1393
	movw	r3, #2794
	movw	r0, #2788
	ldrh	r0, [r5, r0]
	ldrh	r3, [r5, r3]
	add	r3, r3, r0
	movw	r0, #2796
	ldrh	ip, [r5, r0]
	add	ip, r3, ip
	sub	r3, r1, #544
	ldrh	r1, [r3, #-10]
	add	r0, r1, #8
	cmp	ip, r0
	blt	.L1410
	movw	r0, #2798
	movw	lr, #2790
	ldrh	r0, [r5, r0]
	ldrh	lr, [r5, lr]
	ldrh	r3, [r3, #-8]
	add	r0, r0, lr
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1397
	add	r1, r1, #24
	cmp	ip, r1
.L1412:
	bge	.L1397
.L1410:
	bfc	r4, #3, #2
	b	.L1408
.L1397:
	mov	r3, #2
	bfi	r4, r3, #3, #2
.L1408:
	strb	r4, [r8, #2]
.L1393:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	bne	.L1398
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
	b	.L1399
.L1398:
	cmp	r3, #16
	bne	.L1399
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
	b	.L1409
.L1390:
	tst	r4, #24
	bne	.L1399
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1399
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	cmp	r1, #15
	bhi	.L1399
	movw	r3, #2794
	movw	r2, #2788
	ldrh	r2, [r5, r2]
	ldrh	r3, [r5, r3]
	ldr	r0, .L1413+8
	add	r3, r3, r2
	movw	r2, #2796
	ldrh	r2, [r5, r2]
	add	r3, r3, r2
	ldrh	r2, [r0, #-10]
	add	r2, r2, #16
	cmp	r3, r2
	ble	.L1399
	movw	r3, #2798
	ldrh	r2, [r5, r3]
	ldrh	r3, [r0, #-8]
	add	r2, r2, r1
	add	r3, r3, #8
	cmp	r2, r3
	bge	.L1399
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
.L1409:
	strh	r3, [r6, r10]	@ movhi
.L1399:
	mov	r0, r7
	bl	zftl_remove_data_node
	ldr	r3, .L1413
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
	bhi	.L1402
	mov	r0, r7
	bl	gc_free_bad_sblk
.L1402:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1403
	ldr	r3, [r4, #1092]
	movw	r2, #586
	ldrh	r1, [r3, r2]
	cmp	r1, r7
	bne	.L1403
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1403
	strh	r7, [r3, r2]	@ movhi
	mov	r1, r7
	ldr	r0, .L1413+12
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1403:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	zftl_insert_free_list
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR0
	.word	.LANCHOR3-2608
	.word	.LANCHOR3-3152
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
	ldr	r6, .L1454
	ldr	r8, .L1454+4
	mov	r7, r6
.L1416:
	ldrh	r2, [r8, #52]
	uxth	r3, r5
	cmp	r2, r3
	bls	.L1453
	add	r3, r8, r3, asl #1
	ldrh	r4, [r3, #54]
	ldr	r3, [r6, #1088]
	mov	r9, r4, asl #1
	ldrh	r2, [r3, r9]
	cmp	r2, #0
	beq	.L1417
	ldr	r0, .L1454+8
	mov	r1, r4
	bl	printk
.L1417:
	ldr	r3, [r7, #1088]
	mov	r2, #0
	strh	r2, [r3, r9]	@ movhi
	ldr	r3, [r7, #1088]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	bne	.L1418
	ldr	r3, .L1454+12
	ldr	r9, [r6, #1080]
	ldr	r3, [r3]
	add	r9, r9, r4, asl #2
	tst	r3, #256
	beq	.L1419
	ldrb	r2, [r9, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L1454+16
	mov	r2, r2, lsr #5
	bl	printk
.L1419:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L1420
	ldr	r1, .L1454+20
	movw	r2, #1362
	ldr	r0, .L1454+24
	bl	printk
	bl	dump_stack
.L1420:
	mov	r0, r4
	bl	ftl_free_sblk
	ldr	r3, [r7, #1092]
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	beq	.L1421
	add	r0, r3, #388
	mov	r2, #0
	add	r0, r0, #2
.L1423:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1422
	add	r2, r2, #196
	mvn	r0, #0
	add	r1, r1, r0
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #124]	@ movhi
	b	.L1421
.L1422:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1423
.L1421:
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	beq	.L1424
	add	r0, r3, #134
	mov	r2, #0
.L1426:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1425
	add	r2, r3, r2, asl #1
	mvn	r0, #0
	add	r1, r1, r0
	strh	r0, [r2, #136]	@ movhi
	strh	r1, [r3, #120]	@ movhi
	b	.L1424
.L1425:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1426
.L1424:
	ldrh	r0, [r3, #122]
	cmp	r0, #0
	beq	.L1427
	add	r1, r3, #260
	mov	r2, #0
	add	r1, r1, #2
.L1429:
	ldrh	ip, [r1, #2]!
	cmp	ip, r4
	bne	.L1428
	add	r2, r2, #132
	mvn	r1, #0
	add	r0, r0, r1
	mov	r2, r2, asl #1
	strh	r1, [r3, r2]	@ movhi
	strh	r0, [r3, #122]	@ movhi
	b	.L1427
.L1428:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1429
	b	.L1427
.L1418:
	mov	r0, r4
	mov	r1, #1
	bl	gc_add_sblk
.L1427:
	add	r5, r5, #1
	b	.L1416
.L1453:
	ldr	r3, .L1454+4
	mov	r2, #0
	strh	r2, [r3, #52]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1455:
	.align	2
.L1454:
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
	ldr	r4, .L1458
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
	ldr	r0, .L1458+4
	bl	printk
	ldr	r2, [r4, #2804]
	ldrb	r3, [r4, #2801]	@ zero_extendqisi2
	ldr	r0, .L1458+8
	ldr	r1, [r2, #8]
	str	r1, [sp]
	ldr	r1, [r2, #64]
	str	r1, [sp, #4]
	ldr	r1, [r2, #20]
	ldr	r2, [r2, #28]
	bl	printk
	ldr	r2, [r4, #2804]
	ldr	r0, .L1458+12
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
	ldr	r0, .L1458+16
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
	ldr	r0, .L1458+20
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
	ldr	r0, .L1458+24
	bl	printk
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L1459:
	.align	2
.L1458:
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
	bne	.L1461
	ldr	r1, .L1472
	movw	r2, #803
	ldr	r0, .L1472+4
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1461:
	ldr	r5, .L1472+8
	ldrb	r2, [r4, #40]	@ zero_extendqisi2
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1466
	ldr	r1, .L1472
	movw	r2, #810
	ldr	r0, .L1472+4
	bl	printk
	bl	dump_stack
.L1466:
	ldrb	r3, [r4, #40]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1463
	ldrb	r2, [r5, #2772]	@ zero_extendqisi2
	ldr	r6, .L1472+8
	cmp	r2, r3
	bcs	.L1464
.L1463:
	mov	r0, r4
	bl	zbuf_free
	ldrb	r0, [r5, #2800]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, pc}
.L1464:
	mov	r1, r4
	ldr	r0, .L1472+12
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
.L1473:
	.align	2
.L1472:
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
	ldr	r8, .L1492
	ldr	r9, .L1492+4
	mov	r6, r8
.L1475:
	ldrb	r10, [r8, #2778]	@ zero_extendqisi2
	cmp	r10, #255
	beq	.L1491
	mov	r4, #48
	ldr	r7, .L1492+8
	mul	r4, r4, r10
	add	r7, r7, r4
	add	r4, r8, r4
	ldrb	r3, [r4, #1236]	@ zero_extendqisi2
	strb	r3, [r8, #2778]
	ldr	r3, [r4, #1272]
	cmn	r3, #1
	bne	.L1476
	ldr	r4, .L1492+12
	ldrb	r3, [r4, #-2612]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1477
	ldrb	r3, [r4, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1478
.L1477:
	ldr	r3, [r6, #2804]
	ldr	r3, [r3, #156]
	cmp	r3, r9
	beq	.L1479
.L1478:
	mov	r5, #48
	ldr	r3, .L1492+16
	mla	r5, r5, r10, r6
	ldrb	r1, [r4, #-3202]	@ zero_extendqisi2
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
	ldr	r0, .L1492+20
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
	beq	.L1479
	ldrh	r2, [r3, #48]
	cmp	r2, r4
	moveq	r2, #0
	streqh	r2, [r3, #54]	@ movhi
.L1479:
	mov	r0, r7
	mov	r5, #1
	bl	ftl_write_buf
	b	.L1475
.L1476:
	cmp	r5, #1
	add	r3, r4, #1248
	add	r3, r3, #12
	bne	.L1484
	ldr	r1, [r4, #1256]
	ldr	r2, [r4, #1260]
	ldr	r0, .L1492+24
	bl	printk
	mov	r0, r7
	bl	ftl_write_buf
	b	.L1475
.L1484:
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
	bne	.L1475
	mov	r0, r7
	bl	zbuf_free
	b	.L1475
.L1491:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1493:
	.align	2
.L1492:
	.word	.LANCHOR0
	.word	1145785929
	.word	.LANCHOR0+1236
	.word	.LANCHOR3
	.word	.LANCHOR3-3204
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
	bne	.L1495
	ldr	r1, .L1500
	movw	r2, #1143
	ldr	r0, .L1500+4
	bl	printk
	bl	dump_stack
	ldmfd	sp!, {r3, r4, r5, lr}
	b	print_ftl_debug_info
.L1495:
	ldr	r3, .L1500+8
	ldrb	r2, [r4, #40]	@ zero_extendqisi2
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1497
	ldr	r1, .L1500
	movw	r2, #1150
	ldr	r0, .L1500+4
	bl	printk
	bl	dump_stack
.L1497:
	ldr	r5, .L1500+12
	mov	r1, r4
	ldr	r0, .L1500+16
	bl	buf_add_tail
	ldrb	r3, [r5, #-2605]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r5, #-2605]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1501:
	.align	2
.L1500:
	.word	.LANCHOR1+1784
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-2606
	.fnend
	.size	zftl_add_read_buf, .-zftl_add_read_buf
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	uxth	r5, r0
	cmp	r0, #5
	mov	r6, r0
	mov	r1, r5
	mov	r0, #0
	movne	r7, #0
	moveq	r7, #2
	bl	zftl_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	beq	.L1504
	ldr	r3, .L1513
	ldr	r5, [r3, #1080]
	add	r5, r5, r0, asl #2
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L1505
	ldr	r1, .L1513+4
	movw	r2, #939
	ldr	r0, .L1513+8
	bl	printk
	bl	dump_stack
.L1505:
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	bfi	r3, r6, #5, #3
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
	b	.L1508
.L1504:
	bl	print_ftl_debug_info
	mov	r1, r4
	mov	r2, r6
	ldr	r0, .L1513+12
	bl	printk
	mov	r1, r5
	mov	r0, #0
	bl	zftl_get_free_sblk
	mov	r2, r6
	mov	r4, r0
	ldr	r0, .L1513+12
	mov	r1, r4
	bl	printk
	bl	dump_all_list_info
	ldr	r1, .L1513+4
	movw	r2, #955
	ldr	r0, .L1513+8
	bl	printk
	bl	dump_stack
.L1508:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1514:
	.align	2
.L1513:
	.word	.LANCHOR0
	.word	.LANCHOR1+1804
	.word	.LC0
	.word	.LC123
	.fnend
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1516
	mvn	r2, #0
	mov	r0, #0
	strb	r2, [r3, #2775]
	strb	r2, [r3, #2778]
	strb	r2, [r3, #2776]
	strb	r2, [r3, #2777]
	bx	lr
.L1517:
	.align	2
.L1516:
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
	ldr	r4, .L1528
	ldr	r0, .L1528+4
	ldrb	r1, [r4, #2775]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r4, #2775]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1528+8
	mov	r2, #48
	mov	r6, r2
	mla	r4, r2, r4, r3
	mov	r5, r3
.L1520:
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldrb	r2, [r4, #42]	@ zero_extendqisi2
	ldr	r3, [r4, #24]
	ldr	r0, .L1528+12
	bl	printk
	ldrb	r4, [r4]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mla	r4, r6, r4, r5
	b	.L1520
.L1529:
	.align	2
.L1528:
	.word	.LANCHOR0
	.word	.LC124
	.word	.LANCHOR0+1236
	.word	.LC125
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
	ldr	lr, .L1546
	ldrb	ip, [lr, #2775]	@ zero_extendqisi2
	cmp	ip, #255
	beq	.L1538
	ldr	r4, .L1546+4
	mov	r2, #1
	ldrb	r3, [lr, #1189]	@ zero_extendqisi2
	mov	r9, #48
	sub	r5, r4, #3200
	rsb	r7, r3, #24
	ldrb	r4, [r4, #-3202]	@ zero_extendqisi2
	ldrh	r8, [r5, #-4]
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
.L1537:
	mla	r0, r9, ip, lr
	ldr	r6, [r0, #1260]
	and	fp, r3, r6, lsr r7
	cmp	fp, r10
	bne	.L1532
	and	r6, r4, r6, lsr r8
	ldrb	r0, [r0, #1278]	@ zero_extendqisi2
	and	r6, r6, r2
	cmp	r6, r5
	bne	.L1533
	cmp	r1, #1
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1532
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1533:
	cmp	r1, #3
	ldrls	pc, [pc, r1, asl #2]
	b	.L1532
.L1534:
	.word	.L1531
	.word	.L1535
	.word	.L1536
	.word	.L1531
.L1535:
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1532
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1536:
	cmp	r0, #11
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1532:
	mla	ip, r9, ip, lr
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1537
.L1538:
	mov	r0, #0
.L1531:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1547:
	.align	2
.L1546:
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
	ldr	r3, .L1575
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
	ldr	r10, .L1575+4
	str	r1, [sp, #4]
.L1549:
	cmp	r2, #255
	beq	.L1550
	mla	fp, r0, r2, r3
	mov	r1, r2
	ldrb	lr, [fp, #1278]	@ zero_extendqisi2
	sub	lr, lr, #12
	cmp	lr, #1
	bls	.L1551
	cmp	ip, #0
	ldrne	r3, .L1575
	beq	.L1548
	b	.L1573
.L1551:
	mul	ip, r0, r1
	ldrb	r2, [fp, #1236]	@ zero_extendqisi2
	add	lr, r8, ip
	ldrb	fp, [lr, #1279]	@ zero_extendqisi2
	strb	r9, [lr, #1236]
	cmp	fp, #1
	bne	.L1554
	add	ip, r10, ip
	ldrh	ip, [ip, #34]
	cmp	ip, #0
	moveq	ip, r7
	ldreq	lr, .L1575+8
	ldrne	ip, [sp, #4]
	ldrne	lr, .L1575+12
	b	.L1555
.L1554:
	cmp	fp, #0
	beq	.L1574
.L1556:
	mov	ip, #1
	b	.L1549
.L1574:
	ldr	ip, [lr, #1256]
	cmn	ip, #1
	movne	ip, r6
	ldrne	lr, .L1575+16
	beq	.L1556
.L1555:
	cmp	ip, #255
	bne	.L1560
	strb	r2, [r3, #2775]
	mov	r2, #48
	mla	r1, r2, r1, r3
	ldrb	r3, [r1, #1237]	@ zero_extendqisi2
	strb	r3, [lr]
	b	.L1548
.L1560:
	mov	lr, ip
	mla	ip, r0, ip, r3
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1560
	mla	r1, r5, r1, r4
	mla	lr, r5, lr, r4
	ldrb	r1, [r1, #1237]	@ zero_extendqisi2
	strb	r1, [lr, #1236]
	b	.L1556
.L1550:
	cmp	ip, #0
	beq	.L1548
	mvn	r2, #0
.L1573:
	strb	r2, [r3, #2775]
.L1548:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1576:
	.align	2
.L1575:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+2778
	.word	.LANCHOR0+2777
	.word	.LANCHOR0+2776
	.fnend
	.size	queue_remove_completed_req, .-queue_remove_completed_req
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
	ldr	r5, .L1595
	ldr	r7, .L1595+4
	ldr	r4, [r5, #2804]
	ldrh	r3, [r4, r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	ldrb	r2, [r7, #-3195]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1578
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1579
.L1578:
	ldr	r4, .L1595
	movw	r8, #65535
.L1580:
	mov	r0, #1
	bl	ftl_alloc_sblk
	cmp	r0, r8
	mov	r6, r0
	beq	.L1580
	mov	r1, #0
	bl	ftl_erase_sblk
	ldr	r1, [r5, #2804]
	mov	r0, r6
	add	r1, r1, #672
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L1581
	mov	r1, r6
	ldr	r0, .L1595+8
	bl	printk
	ldr	r3, [r4, #1080]
	add	r6, r3, r6, asl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	b	.L1580
.L1581:
	ldr	r3, .L1595
	movw	r2, #690
	mov	r4, #0
	movw	r1, #65535
	ldr	r3, [r3, #2804]
	strh	r4, [r3, r2]	@ movhi
	add	r3, r3, #412
	add	r3, r3, #2
	mov	r2, #1
	str	r2, [r7, #-2604]
.L1583:
	ldrh	r0, [r3, #2]!
	uxth	r2, r4
	cmp	r0, r1
	beq	.L1582
	add	r4, r4, #1
	cmp	r4, #128
	bne	.L1583
	mov	r2, #264
	ldr	r1, .L1595+12
	ldr	r0, .L1595+16
	bl	printk
	bl	dump_stack
	mov	r2, r4
.L1582:
	add	r2, r2, #208
	ldr	r3, [r5, #2804]
	mov	r2, r2, asl #1
	strh	r6, [r3, r2]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L1579:
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
	bls	.L1585
	ldr	r1, .L1595+12
	movw	r2, #270
	ldr	r0, .L1595+16
	bl	printk
	bl	dump_stack
.L1585:
	ldr	r1, [r5, #2804]
	mov	r2, #0
	add	r3, r1, #696
	strh	r2, [r3]	@ movhi
	ldr	r2, .L1595+20
	ldrb	r3, [r5, #1189]	@ zero_extendqisi2
	ldrh	r5, [r2, #-4]
	rsb	r3, r3, #24
	movw	r2, #694
	rsb	r5, r5, r3
	mov	r3, r4, asr r5
	strh	r3, [r1, r2]	@ movhi
	ldr	r2, .L1595+24
	add	r1, r1, #692
	strh	r4, [r1]	@ movhi
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L1589
	mvn	r2, #0
	uxth	r3, r3
	ldr	r0, .L1595+28
	mov	r1, r4
	str	r3, [sp]
	mvn	r2, r2, asl r5
	mov	r3, r4
	bl	printk
.L1589:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1596:
	.align	2
.L1595:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC126
	.word	.LANCHOR1+1820
	.word	.LC0
	.word	.LANCHOR3-3200
	.word	.LANCHOR2
	.word	.LC127
	.fnend
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1612
	mov	r3, #0
	movw	ip, #65535
.L1599:
	mov	r1, r3, asl #3
	uxth	r0, r3
	ldrh	r1, [r1, r2]
	cmp	r1, ip
	ldr	r1, .L1612
	bxeq	lr
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1599
	mov	r2, #0
	mov	r0, r3
	mov	ip, #32768
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.L1601:
	add	r3, r1, r2, asl #3
	uxth	lr, r2
	ldrh	r3, [r3, #2]
	tst	r3, #32768
	bne	.L1600
	cmp	r3, ip
	movcc	ip, r3
	movcc	r0, lr
.L1600:
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L1601
	cmp	r0, #32
	mov	r4, r0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1612+4
	mvn	r2, #0
	ldr	r5, .L1612
	ldrb	lr, [r3, #-2344]	@ zero_extendqisi2
	mov	r3, #0
.L1603:
	mov	ip, r3, asl #3
	uxth	r6, r3
	add	r0, r1, ip
	ldrh	r0, [r0, #2]
	cmp	r0, r2
	bcs	.L1602
	ldrh	ip, [ip, r5]
	cmp	ip, lr
	movne	r2, r0
	movne	r4, r6
.L1602:
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1603
	cmp	r4, #32
	bne	.L1604
	ldr	r1, .L1612+8
	movw	r2, #377
	ldr	r0, .L1612+12
	bl	printk
	bl	dump_stack
.L1604:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1613:
	.align	2
.L1612:
	.word	.LANCHOR3-2600
	.word	.LANCHOR3
	.word	.LANCHOR1+1840
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
	ldr	r4, .L1659
	mov	r0, #1024
	ldr	r3, [r4, #1096]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	__aeabi_idiv
	cmp	r6, #0
	uxth	r5, r0
	bne	.L1616
	add	r4, r4, #4
.L1617:
	mov	r3, r6, asl #1
	strh	r6, [r3, r4]	@ movhi
	add	r6, r6, #1
	cmp	r6, #512
	bne	.L1617
.L1621:
	mov	r1, #255
	ldr	r0, .L1659+4
	mov	r2, #2048
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L1659+8
	ldr	r0, .L1659+4
	b	.L1618
.L1616:
	cmp	r6, #1
	bne	.L1619
	add	r4, r4, #4
	mov	r3, #0
.L1620:
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
	bne	.L1620
	b	.L1621
.L1619:
	cmp	r6, #2
	bne	.L1622
	add	r4, r4, #4
	mov	r3, #0
.L1623:
	uxth	r2, r3
	mov	r1, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	r0, r2, asl #1
	subhi	r2, r0, #1
	cmp	r3, #512
	strh	r2, [r1, r4]	@ movhi
	bne	.L1623
	b	.L1621
.L1622:
	cmp	r6, #3
	bne	.L1624
	add	r4, r4, #4
	mov	r3, #0
.L1625:
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
	bne	.L1625
	b	.L1621
.L1624:
	cmp	r6, #4
	mov	r3, #0
	bne	.L1626
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
.L1627:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r4, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1627
	b	.L1621
.L1626:
	cmp	r6, #5
	bne	.L1628
	add	r4, r4, #4
.L1629:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L1629
	ldr	r2, .L1659+12
.L1630:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L1630
	b	.L1621
.L1628:
	cmp	r6, #8
	bne	.L1631
	add	r4, r4, #4
.L1632:
	strh	r3, [r3, r4]	@ movhi
	add	r3, r3, #2
	cmp	r3, #1024
	bne	.L1632
	b	.L1621
.L1631:
	cmp	r6, #9
	bne	.L1633
	strh	r3, [r4, #4]	@ movhi
	movw	r2, #1021
	mov	r3, #1
	strh	r3, [r4, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r4, #8]!	@ movhi
	mov	r3, #3
.L1634:
	strh	r3, [r4, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r2
	bne	.L1634
	b	.L1621
.L1633:
	cmp	r6, #10
	bne	.L1635
	add	r4, r4, #4
.L1636:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L1636
	ldr	r2, .L1659+16
	movw	r1, #961
.L1637:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L1637
	b	.L1621
.L1635:
	cmp	r6, #11
	bne	.L1638
	ldr	r2, .L1659+8
	mov	r3, #0
.L1639:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L1639
	ldr	r1, .L1659+20
.L1640:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1640
	b	.L1621
.L1638:
	cmp	r6, #13
	bne	.L1621
	ldr	r2, .L1659+24
	mov	r3, #0
.L1641:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #3
	uxth	r3, r3
	cmp	r3, #1536
	bne	.L1641
	b	.L1621
.L1618:
	uxth	r2, r3
	cmp	r2, r5
	bcs	.L1658
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L1618
.L1658:
	ldmfd	sp!, {r4, r5, r6, pc}
.L1660:
	.align	2
.L1659:
	.word	.LANCHOR0
	.word	.LANCHOR3-2340
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
	ldr	r5, .L1692
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1662
	ldr	r0, .L1692+4
	ldr	r1, .L1692+8
	bl	printk
.L1662:
	ldrh	r3, [r5, #30]
	mov	r6, #0
	ldr	r4, .L1692+12
	ldrb	r1, [r5, #16]	@ zero_extendqisi2
	ldrh	r0, [r5, #14]
	add	r9, r4, #1200
	strh	r3, [r4, #2]	@ movhi
	strb	r6, [r4, #1101]
	bl	__aeabi_idiv
	ldr	r7, .L1692+16
	mov	r1, r6
	mov	r2, #8
	strh	r0, [r7]	@ movhi
	ldr	r0, .L1692+20
	bl	ftl_memset
	add	r0, r7, #4
	mov	r1, r6
	mov	r2, #32
	bl	ftl_memset
	ldr	r7, .L1692+24
.L1667:
	ldrb	r2, [r5, #4]	@ zero_extendqisi2
	add	r1, r9, r6, asl #3
	ldr	r8, .L1692+28
	mov	r3, #2
	ldr	r0, .L1692+32
	strb	r3, [r6, r7]
	mov	r3, #0
.L1663:
	cmp	r3, r2
	bcs	.L1690
	add	ip, r0, r3
	ldrb	lr, [ip, #1]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	cmp	lr, ip
	bne	.L1664
	add	r3, r3, #1
	b	.L1663
.L1690:
	ldrb	r3, [r4, #1101]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r2, .L1692+36
	uxtb	r0, r6
	add	r2, r2, r3, asl #2
	str	r1, [r2, #-288]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #1101]
	strb	r0, [r3, #1180]
	bl	zftl_flash_enter_slc_mode
.L1664:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1667
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldr	r3, .L1692
	cmp	r2, #2
	beq	.L1668
.L1675:
	ldrb	r3, [r5, #17]	@ zero_extendqisi2
	ldrb	r2, [r4, #1101]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L1692+40
	strh	r3, [r2]	@ movhi
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1668:
	ldrh	ip, [r3, #18]
	mov	lr, #0
	ldrh	r2, [r4, #2]
	add	r3, r3, #4
	and	ip, ip, #65280
	ldrb	r1, [r4, #1101]	@ zero_extendqisi2
	ldr	r6, .L1692+36
	mul	r2, r2, ip
	ldrb	ip, [r3, #13]	@ zero_extendqisi2
	ldrb	r7, [r3]	@ zero_extendqisi2
	mul	ip, ip, r2
	ldrb	r2, [r3, #23]	@ zero_extendqisi2
	str	r2, [sp]
	mov	r2, ip, asl #1
	str	r2, [sp, #4]
	mov	r2, lr
.L1673:
	add	r9, r8, r2, asl #3
	mov	r0, #0
.L1669:
	cmp	r0, r7
	bcs	.L1691
	add	r10, r3, r0
	ldrb	fp, [r10, #1]	@ zero_extendqisi2
	ldrb	r10, [r9, r0]	@ zero_extendqisi2
	cmp	fp, r10
	bne	.L1670
	add	r0, r0, #1
	b	.L1669
.L1691:
	ldmia	sp, {r9, lr}
	mov	r0, r1
	add	r1, r6, r1, asl #2
	cmp	r9, #0
	moveq	lr, ip
	str	lr, [r1, #-288]
	add	r1, r0, #1
	add	r0, r4, r0
	mov	lr, #1
	uxtb	r1, r1
	strb	r2, [r0, #1180]
.L1670:
	add	r2, r2, #1
	cmp	r2, #4
	bne	.L1673
	cmp	lr, #0
	strneb	r1, [r4, #1101]
	b	.L1675
.L1693:
	.align	2
.L1692:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+1864
	.word	.LANCHOR0
	.word	.LANCHOR3-292
	.word	.LANCHOR0+1180
	.word	.LANCHOR0+1192
	.word	.LANCHOR0+1200
	.word	.LANCHOR2+4
	.word	.LANCHOR3
	.word	.LANCHOR3-256
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
	ldr	r4, .L1696
	mov	r2, #512
	sub	r0, r4, #3136
	bl	ftl_memset
	sub	r3, r4, #3168
	ldr	r0, [r4, #-2620]
	mov	r1, #255
	ldrh	r2, [r3, #-4]
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	ldmfd	sp!, {r4, lr}
	b	ftl_memset
.L1697:
	.align	2
.L1696:
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
	ldr	r3, .L1711
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1699
	ldr	r0, .L1711+4
	mov	r2, #239
	ldr	r1, .L1711+8
	mov	r3, #0
	bl	printk
.L1699:
	ldr	r4, .L1711+12
	mov	r1, #255
	mov	r2, #512
	sub	r5, r4, #3168
	sub	r0, r4, #3136
	bl	ftl_memset
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	ldrh	r2, [r5, #-4]
	mov	r1, #255
	ldr	r0, [r4, #-2620]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	sub	lr, r5, #4
	sub	r5, r4, #3136
	mov	r0, #0
	mov	ip, r4
.L1700:
	ldrh	r1, [lr]
	uxth	r2, r0
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asl #1
	bge	.L1710
	ldr	r3, [ip, #-2624]
	ldr	r3, [r3, r2, asl #2]
	cmn	r3, #1
	beq	.L1701
	uxtb	r3, r3
	mov	r3, r3, asl #1
	ldrh	r1, [r5, r3]
	strh	r2, [r5, r3]	@ movhi
	mov	r2, r2, asl #1
	ldr	r3, [ip, #-2620]
	strh	r1, [r3, r2]	@ movhi
.L1701:
	add	r0, r0, #1
	b	.L1700
.L1710:
	ldmfd	sp!, {r3, r4, r5, pc}
.L1712:
	.align	2
.L1711:
	.word	.LANCHOR2
	.word	.LC128
	.word	.LANCHOR1+1884
	.word	.LANCHOR3
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
	ldr	r2, .L1718
	ldr	r3, .L1718+4
	mov	ip, #1
	ldrh	r1, [r2, #2]
	ldrb	r0, [r3, #13]	@ zero_extendqisi2
	smulbb	r1, r0, r1
	ldr	r0, .L1718+8
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
.L1714:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L1717
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L1714
.L1717:
	ldmfd	sp!, {r4, pc}
.L1719:
	.align	2
.L1718:
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
	movw	r7, #2828
	ldr	r6, .L1722
	mov	r5, #0
	ldr	r4, .L1722+4
	mov	r1, r5
	add	r9, r6, r7
	movw	r2, #2132
	sub	r8, r4, #3152
	mov	r0, r9
	strb	r5, [r4, #-3187]
	strb	r5, [r4, #-254]
	str	r5, [r4, #-252]
	strh	r5, [r8, #-12]	@ movhi
	bl	ftl_memset
	mvn	r3, #0
	strh	r3, [r6, r7]	@ movhi
	sub	r7, r4, #3168
	movw	ip, #2812
	str	r5, [r6, #2836]
	ldrh	r2, [r7, #-4]
	strh	r5, [r7, #-14]	@ movhi
	mov	r3, r2, lsr #1
	strh	r3, [r9, #34]	@ movhi
	sub	r3, r4, #3184
	mov	r0, r2, lsr #2
	strh	r0, [r9, #32]	@ movhi
	strh	r5, [r3, #-2]	@ movhi
	strh	r5, [r3]	@ movhi
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	smulbb	r1, r3, r2
	uxth	r1, r1
	sub	lr, r1, #32
	strh	lr, [r6, ip]	@ movhi
	movw	ip, #2810
	strh	r1, [r6, ip]	@ movhi
	add	r1, r6, #2816
	strh	r2, [r1]	@ movhi
	mov	r2, #4
	strh	r2, [r4, #-248]	@ movhi
	movw	r2, #2814
	strh	r0, [r6, r2]	@ movhi
	ldrh	r0, [r7, #-2]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	str	r0, [r4, #-244]
	ldrh	r0, [r7, #-2]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r3, [r7, #-2]
	str	r0, [r4, #-240]
	ldrb	r0, [r4, #-3195]	@ zero_extendqisi2
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	str	r0, [r4, #-3192]
	ldrh	r0, [r7, #-2]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #-3195]	@ zero_extendqisi2
	str	r0, [r4, #-3200]
	ldrh	r0, [r7, #-2]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r3, [r8, #-10]
	movw	r2, #2808
	mov	r3, r3, lsr #2
	strh	r3, [r6, r2]	@ movhi
	str	r0, [r4, #-236]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1723:
	.align	2
.L1722:
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
	ldr	r4, .L1843
	ldr	r3, [r4, #2804]
	ldr	r3, [r3, #32]
	cmp	r3, #10240
	bls	.L1725
	bl	ftl_tmp_into_update
.L1725:
	ldr	r3, [r4, #1092]
	ldr	r2, [r3, #568]
	ldr	r0, [r3, #12]
	add	r1, r2, #35840
	add	r1, r1, #160
	cmp	r0, r1
	bcs	.L1726
	ldr	r1, .L1843
	ldr	ip, [r1, #2804]
	ldr	r1, [r3, #572]
	ldr	ip, [ip, #36]
	add	r1, r1, #256
	cmp	ip, r1
	movcc	r0, #0
	bcc	.L1835
.L1726:
	add	r2, r2, #860160
	add	r2, r2, #3840
	cmp	r0, r2
	bhi	.L1728
	ldr	r1, [r4, #2804]
	ldr	r2, [r3, #572]
	ldr	r1, [r1, #36]
	add	r2, r2, #32
	cmp	r1, r2
	movls	r7, #0
	movls	r9, r7
	bls	.L1729
.L1728:
	ldr	r2, [r4, #2804]
	mov	r8, #0
	ldr	ip, .L1843
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
.L1730:
	ldr	r2, .L1843+4
	ldr	r3, .L1843
	ldrh	r2, [r2]
	cmp	r2, r7
	bls	.L1840
	ldr	r0, [ip, #1080]
	mov	r1, r7, asl #2
	add	lr, r0, r1
	ldrb	r3, [lr, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	cmp	r2, #224
	beq	.L1731
	tst	r3, #8
	ldrneh	r3, [r0, r1]
	ldrne	r4, [r0, r1]
	ubfxne	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1733
.L1732:
	tst	r3, #24
	ldreqh	r3, [r0, r1]
	movweq	r4, #65535
	ldrne	r4, [r0, r1]
	movwne	r3, #65535
	ubfxeq	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1735
.L1733:
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
.L1777:
	ldr	r2, [sp, #20]
	cmp	r2, r3
	movw	r2, #65535
	strcc	r3, [sp, #20]
	movcc	r8, r7
	cmp	r4, r2
	beq	.L1737
.L1735:
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
.L1737:
	cmp	r4, #9
	cmphi	r3, #9
	bhi	.L1731
	ldr	r3, .L1843+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1731
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
	ldr	r0, .L1843+12
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1731:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1730
.L1840:
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
	ldr	r2, .L1843+8
	mov	ip, r1, asl #2
	ldr	r2, [r2]
	add	r4, r7, ip
	tst	r2, #1024
	beq	.L1740
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
	ldr	r0, .L1843+16
	str	r3, [sp, #12]
	ubfx	r3, lr, #11, #8
	bl	printk
	ldr	ip, [sp, #36]
.L1740:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r1, .L1843
	and	r3, r3, #224
	cmp	r3, #32
	bne	.L1741
	ldr	r3, [r1, #2804]
	add	r3, r3, #688
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrhi	r3, .L1843+20
	movhi	r2, #1
	strhi	r2, [r3, #-232]
.L1741:
	ldrb	r2, [r4, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L1742
	ldr	r3, [r1, #2804]
	ldr	r0, .L1843+20
	ldr	r4, .L1843
	ldrh	r1, [r3, #96]
	ldrh	r0, [r0, #-228]
	ldrh	r3, [r7, ip]
	ubfx	r3, r3, #0, #11
	add	r3, r3, r0, lsr #2
	cmp	r1, r3
	ble	.L1742
	and	r3, r2, #192
	cmp	r3, #64
	bne	.L1743
	ldr	r2, [r4, #1092]
	movw	r3, #586
	mov	r1, #0
	ldrh	r0, [r2, r3]
	mov	r2, #1
	bl	gc_add_sblk
	ldr	r2, .L1843+24
	movw	r3, #2106
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1742
.L1743:
	tst	r2, #224
	bne	.L1742
	ldr	r3, [r4, #1092]
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1742
	movw	r7, #586
	str	r2, [sp, #36]
	ldrh	r0, [r3, r7]
	add	r3, r3, #588
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L1742
	bl	zftl_remove_free_node
	ldr	r3, [r4, #1092]
	ldrh	r1, [r3, r7]
	ldr	r2, [sp, #36]
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
.L1742:
	ldr	r1, .L1843
	ldr	r2, .L1843+8
	ldr	r3, [r1, #1092]
	ldr	r2, [r2]
	add	r3, r3, #584
	ldr	ip, [r1, #1080]
	tst	r2, #1024
	ldrh	r7, [r3]
	mov	r3, r7, asl #2
	add	r4, ip, r3
	beq	.L1744
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
	ldr	r0, .L1843+28
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1744:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r4, .L1843
	tst	r3, #8
	beq	.L1745
	ldr	r2, [r4, #2804]
	ldr	r0, .L1843+20
	ldrh	r1, [r2, #98]
	ldrh	r0, [r0, #-226]
	ldr	r2, [ip, r7, asl #2]
	ubfx	r2, r2, #11, #8
	add	r2, r2, r0, lsr #2
	cmp	r1, r2
	ble	.L1745
	and	r2, r3, #192
	cmp	r2, #64
	bne	.L1746
	ldr	r3, [r4, #1092]
	mov	r1, #0
	mov	r2, #1
	add	r3, r3, #584
	ldrh	r0, [r3]
	bl	gc_add_sblk
	ldr	r2, .L1843+24
	movw	r3, #2106
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1745
.L1746:
	and	r3, r3, #248
	cmp	r3, #16
	bne	.L1745
	ldr	r3, [r4, #1092]
	add	r2, r3, #588
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1745
	add	r2, r3, #584
	ldrh	r0, [r2]
	movw	r2, #590
	ldrh	r3, [r3, r2]
	cmp	r3, r0
	beq	.L1745
	bl	zftl_remove_free_node
	ldr	r3, [r4, #1092]
	add	r2, r3, #588
	add	r3, r3, #584
	ldrh	r1, [r3]
	strh	r1, [r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L1745:
	ldr	r3, .L1843+8
	ldr	r0, [r4, #1080]
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L1747
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
	ldr	r1, .L1843
	ldr	r0, [r1, #1088]
	mov	r1, r8, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1843+32
	str	r1, [sp, #12]
	mov	r1, r8
	bl	printk
.L1747:
	ldr	r3, .L1843+8
	ldr	ip, .L1843
	ldr	r3, [r3]
	ldr	r0, [ip, #1080]
	tst	r3, #1024
	beq	.L1748
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
	ldr	r0, .L1843+36
	str	r1, [sp, #12]
	mov	r1, fp
	bl	printk
.L1748:
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
	ldr	r4, .L1843+8
	ldr	r3, [r4]
	tst	r3, #1024
	beq	.L1749
	uxth	r0, r0
	mov	r1, r9
	str	r0, [sp]
	uxth	r3, r7
	ldr	r0, .L1843+40
	ldr	r2, [sp, #24]
	bl	printk
.L1749:
	ldr	r3, [r4]
	ldr	r4, .L1843+20
	tst	r3, #1024
	beq	.L1750
	str	r10, [sp]
	mov	r1, r5
	ldrh	r3, [r4, #-228]
	mov	r2, r6
	ldr	r0, .L1843+44
	str	r3, [sp, #4]
	ldrh	r3, [r4, #-226]
	str	r3, [sp, #8]
	ldr	r3, [sp, #20]
	bl	printk
.L1750:
	rsb	r3, r6, r10
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldrh	r3, [r4, #-226]
	cmp	r2, r3
	bgt	.L1751
	ldr	r2, .L1843+20
	ldr	r3, [sp, #20]
	ldrh	r2, [r2, #-228]
	rsb	r3, r5, r3
	cmp	r3, r2
	movle	r7, #0
	movle	r9, r7
	ble	.L1752
.L1751:
	ldr	ip, .L1843
	mov	r7, #0
	mov	r9, r7
	ldr	r3, [ip, #1092]
	mov	fp, ip
	ldr	r4, [r3, #580]
	ldr	r3, [ip, #2804]
	uxth	r4, r4
	ldrh	r10, [r3, #134]
.L1753:
	ldr	r3, .L1843+4
	ldrh	r3, [r3]
	cmp	r10, r3
	bcs	.L1762
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
	bne	.L1755
	ubfx	r2, r2, #3, #2
	ands	r1, r2, #1
	beq	.L1756
	cmp	r0, #160
	b	.L1838
.L1756:
	cmp	r2, #2
.L1838:
	bne	.L1758
	ldr	r2, .L1843+20
	ldr	r0, [sp, #24]
	ldrh	r2, [r2, #-226]
	cmp	r0, r2
	ble	.L1759
	ldr	r2, [r3, r8]
	ubfx	r2, r2, #11, #8
	cmp	r2, r6
	bls	.L1760
	cmp	r1, #0
	beq	.L1759
	ldrh	r2, [r3, r8]
	ubfx	r2, r2, #0, #11
	cmp	r2, r5
	bgt	.L1759
.L1760:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1843+48
	mov	r1, #1
	add	r9, r9, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1843+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1759
	ldr	r0, [fp, #1088]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1080]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1843+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1843+52
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
	b	.L1839
.L1758:
	ldr	r0, .L1843+20
	ldr	r2, [sp, #20]
	ldrh	r0, [r0, #-228]
	rsb	r2, r5, r2
	cmp	r2, r0
	ble	.L1759
	ldrh	r2, [r3, r8]
	add	r0, r5, #8
	ubfx	r2, r2, #0, #11
	cmp	r2, r0
	ble	.L1761
	cmp	r1, #0
	beq	.L1759
	ldr	r2, [r3, r8]
	add	r1, r6, #4
	ubfx	r2, r2, #11, #8
	cmp	r2, r1
	bgt	.L1759
.L1761:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1843+48
	mov	r1, #1
	add	r7, r7, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1843+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1759
	ldr	r0, [fp, #1088]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1080]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1843+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1843+56
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
.L1839:
	mov	r1, r4
	mov	r2, r2, lsr #5
	mov	r3, lr
	bl	printk
.L1759:
	cmp	r9, #4
	cmpls	r7, #4
	bhi	.L1762
.L1755:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1753
.L1762:
	ldr	r3, .L1843
	ldr	r3, [r3, #1092]
	str	r4, [r3, #580]
.L1752:
	cmp	r6, #0
	beq	.L1765
	ldr	r0, .L1843
	ldr	r4, .L1843+4
	ldr	r3, [r0, #2804]
	ldrh	r3, [r3, #134]
.L1766:
	ldrh	r2, [r4]
	cmp	r2, r3
	bls	.L1841
	ldr	ip, [r0, #1080]
	mov	lr, r3, asl #2
	add	r8, ip, lr
	ldr	r2, [ip, r3, asl #2]
	ubfx	r1, r2, #11, #8
	cmp	r1, r6
	bcc	.L1767
	ldrb	r8, [r8, #2]	@ zero_extendqisi2
	tst	r8, #24
	rsbne	r1, r6, r1
	bfine	r2, r1, #11, #8
	strne	r2, [ip, lr]
.L1767:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1766
.L1841:
	ldr	r3, .L1843
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #72]
	add	r2, r6, r2
	strh	r2, [r3, #72]	@ movhi
	ldrh	r2, [r3, #98]
	cmp	r2, r6
	rsbhi	r2, r6, r2
	strhih	r2, [r3, #98]	@ movhi
.L1765:
	cmp	r5, #0
	beq	.L1771
	ldr	ip, .L1843
	ldr	r6, .L1843+4
	ldr	r3, [ip, #2804]
	ldrh	r3, [r3, #134]
.L1772:
	ldrh	r2, [r6]
	cmp	r2, r3
	bls	.L1842
	ldr	r4, [ip, #1080]
	mov	lr, r3, asl #2
	add	r0, r4, lr
	ldrh	r2, [r4, lr]
	ubfx	r1, r2, #0, #11
	cmp	r1, r5
	blt	.L1773
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	and	r0, r0, #24
	cmp	r0, #16
	rsbne	r1, r5, r1
	bfine	r2, r1, #0, #11
	strneh	r2, [r4, lr]	@ movhi
.L1773:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1772
.L1842:
	ldr	r3, .L1843
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #74]
	add	r2, r5, r2
	strh	r2, [r3, #74]	@ movhi
	ldrh	r2, [r3, #96]
	cmp	r2, r5
	rsbhi	r2, r5, r2
	strhih	r2, [r3, #96]	@ movhi
.L1771:
	ldr	r4, .L1843+60
	mov	r1, #0
	sub	r0, r4, #8
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1729
	ldr	r2, .L1843
	mov	r3, r0, asl #1
	ldr	r1, [r2, #1088]
	ldrh	r2, [r4, #-4]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #1
	bhi	.L1729
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	add	r9, r9, #1
.L1729:
	add	r0, r7, r9
.L1835:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1844:
	.align	2
.L1843:
	.word	.LANCHOR0
	.word	.LANCHOR0+1076
	.word	.LANCHOR2
	.word	.LC129
	.word	.LC130
	.word	.LANCHOR3
	.word	.LANCHOR0+2828
	.word	.LC131
	.word	.LC132
	.word	.LC133
	.word	.LC134
	.word	.LC135
	.word	.LANCHOR0+4934
	.word	.LC136
	.word	.LC137
	.word	.LANCHOR3-3168
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
	ldr	r5, .L1878
	mov	r2, #6
	ldr	r6, .L1878+4
	mov	r1, #0
	.pad #20
	sub	sp, sp, #20
	mov	fp, #0
	ldrh	r3, [r5, r3]
	mov	r4, fp
	ldr	r0, [r5, #1036]
	mov	r8, r5
	ldr	r10, .L1878+8
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r3, r6, #3136
	mov	r2, #32
	str	fp, [r6, #-3156]
	strh	r2, [r3, #-8]	@ movhi
	movw	r3, #2788
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2790
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2792
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2794
	strh	fp, [r5, r3]	@ movhi
	sub	r2, r6, #3168
	movw	r3, #2798
	str	fp, [r6, #-3152]
	strh	fp, [r5, r3]	@ movhi
	movw	r3, #2796
	strh	fp, [r5, r3]	@ movhi
	mov	r0, #32768
	ldrb	r3, [r6, #-3195]	@ zero_extendqisi2
	ldrh	r1, [r2, #-2]
	str	fp, [r6, #-3148]
	str	fp, [r6, #-3180]
	mul	r1, r1, r3
	str	fp, [r6, #-3168]
	str	fp, [r6, #-3176]
	bl	__aeabi_idiv
	uxth	r3, r0
	str	r3, [sp, #4]
	ldr	r3, [r5, #2804]
	str	r6, [sp, #8]
	ldrh	r7, [r3, #134]
	strh	fp, [r3, #146]	@ movhi
.L1846:
	ldr	r2, .L1878+12
	sxth	ip, r7
	ldr	r3, .L1878
	ldrh	r2, [r2]
	cmp	ip, r2
	bge	.L1876
	ldr	r6, [r8, #1080]
	add	r6, r6, ip, asl #2
	ldrb	r3, [r6, #3]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r1, [sp, #4]
	beq	.L1847
	ldr	r3, [sp, #8]
	ldrh	fp, [r10]
	ldr	r0, [r8, #2804]
	ldrb	lr, [r3, #-3195]	@ zero_extendqisi2
	mov	r3, #0
	mov	r1, r3
.L1848:
	cmp	r3, lr
	bge	.L1877
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	mov	r2, r2, asr r3
	add	r3, r3, #1
	tst	r2, #1
	addeq	r1, fp, r1
	ldrneh	r2, [r0, #146]
	uxtheq	r1, r1
	addne	r2, r2, #1
	strneh	r2, [r0, #146]	@ movhi
	b	.L1848
.L1877:
	cmp	r1, #0
	beq	.L1852
	sxth	r1, r1
	mov	r0, #32768
	str	ip, [sp, #12]
	bl	__aeabi_idiv
	add	r0, r0, #1
	ldr	ip, [sp, #12]
	uxth	r1, r0
	b	.L1847
.L1852:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	mvn	r0, #0
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	ldr	r2, [r5, #1088]
	mov	r3, ip, asl #1
	strh	r0, [r2, r3]	@ movhi
.L1847:
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
	beq	.L1853
	ldr	r1, [r8, #1092]
	ldrh	r0, [r1, #16]
	cmp	ip, r0
	beq	.L1853
	ldrh	r0, [r1, #48]
	cmp	ip, r0
	beq	.L1853
	ldrh	r1, [r1, #80]
	cmp	ip, r1
	beq	.L1853
	cmp	r2, #64
	uxtheq	r6, r7
	ldreq	r0, .L1878+16
	ldreq	r2, .L1878+20
	moveq	r1, r6
	beq	.L1874
.L1854:
	cmp	r2, #96
	uxtheq	r6, r7
	ldreq	r0, .L1878+24
	ldreq	r2, .L1878+28
	moveq	r1, r6
	beq	.L1874
.L1855:
	cmp	r2, #160
	bne	.L1856
	uxth	r6, r7
	ldr	r0, .L1878+32
	ldr	r2, .L1878+36
	mov	r1, r6
.L1874:
	bl	_insert_data_list
	ldr	r2, [r8, #1088]
	ldrh	r3, [r2, r9]
	cmp	r3, #7
	movls	r0, r6
	movls	r1, #1
	movls	r2, fp
	bhi	.L1853
	b	.L1872
.L1856:
	cmp	r2, #0
	bne	.L1853
	ldr	r2, [r8, #1088]
	uxth	fp, r7
	ldrh	r2, [r2, r9]
	cmp	r2, #0
	beq	.L1857
	sxth	r3, r4
	cmp	r3, #2
	bgt	.L1858
	mov	r1, ip
	ldr	r0, .L1878+40
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
.L1872:
	bl	gc_add_sblk
	b	.L1853
.L1858:
	ldr	r1, .L1878+44
	movw	r2, #651
	ldr	r0, .L1878+48
	bl	printk
	bl	dump_stack
.L1857:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1878+52
	moveq	r1, fp
	ldreq	r2, .L1878+56
	beq	.L1870
.L1860:
	cmp	r3, #16
	ldreq	r0, .L1878+60
	moveq	r1, fp
	ldreq	r2, .L1878+64
	movne	r1, fp
	ldrne	r0, .L1878+68
	ldrne	r2, .L1878+72
.L1870:
	bl	_insert_free_list
.L1853:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1846
.L1876:
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
.L1879:
	.align	2
.L1878:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3170
	.word	.LANCHOR0+1076
	.word	.LANCHOR3-3168
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3180
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3176
	.word	.LANCHOR0+2798
	.word	.LC138
	.word	.LANCHOR1+1904
	.word	.LC0
	.word	.LANCHOR3-3156
	.word	.LANCHOR0+2788
	.word	.LANCHOR3-3152
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3148
	.word	.LANCHOR0+2792
	.fnend
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
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
	ldr	r6, .L1888
	mov	r5, r1
	ldr	r8, .L1888+4
	mov	r7, r6
.L1881:
	movw	r10, #65535
.L1882:
	mov	r0, r5
	bl	ftl_alloc_sblk
	cmp	r0, r10
	mov	r9, r0
	beq	.L1882
	mov	r1, #0
	ldr	fp, .L1888+8
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
	ldrneb	r0, [r6, #-3195]	@ zero_extendqisi2
	ldr	r1, [r7, #-2624]
	strh	r3, [r4, #6]	@ movhi
	ldrb	r3, [r7, #-3195]	@ zero_extendqisi2
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
	ldr	r0, .L1888+12
	mov	r1, r9
	bl	printk
	ldr	r3, [fp, #1088]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	mov	r3, #7
	strb	r3, [r4, #4]
	b	.L1881
.L1889:
	.align	2
.L1888:
	.word	.LANCHOR3
	.word	.LANCHOR3-3172
	.word	.LANCHOR0
	.word	.LC126
	.fnend
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
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
	ldr	r5, .L1920
	ldrh	r3, [r5, #-224]
	cmp	r3, #128
	bls	.L1891
	ldr	r1, .L1920+4
	mov	r2, #94
	ldr	r0, .L1920+8
	bl	printk
	bl	dump_stack
.L1891:
	ldr	r4, .L1920+12
	mov	r1, #0
	ldrh	lr, [r5, #-224]
	mov	r0, r1
	movw	r6, #65535
	ldr	r2, [r4, #2804]
	add	r3, r2, #688
	add	r2, r2, #412
	add	r2, r2, #2
	ldrh	ip, [r3]
.L1894:
	ldrh	r7, [r2, #2]!
	uxth	r3, r1
	cmp	r7, r6
	addne	r0, r0, #1
	uxthne	r0, r0
	cmp	r0, ip
	bcs	.L1893
	cmp	r0, lr
	bcs	.L1893
	add	r1, r1, #1
	cmp	r1, #128
	bne	.L1894
	mov	r3, r1
.L1893:
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
	ldrb	ip, [r5, #-3202]	@ zero_extendqisi2
	add	r10, r7, #700
	ldrh	fp, [r7, r3]
	ldr	r3, .L1920+16
	ldrh	r9, [r3, #-4]
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r9, r3
	mvn	r8, r8, asl r3
.L1895:
	uxth	r3, r6
	cmp	r3, fp
	bcs	.L1917
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
.L1896:
	ldr	r0, [sp, #8]
	uxth	lr, r1
	cmp	lr, r0
	bcs	.L1918
	ldrh	lr, [r3, #2]!
	add	r1, r1, #1
	ldr	r0, [sp, #12]
	add	r2, r2, #2
	cmp	lr, r0
	ldreqh	lr, [r2, #-2]
	addeq	lr, lr, #1
	streqh	lr, [r2, #-2]	@ movhi
	b	.L1896
.L1918:
	add	r6, r6, #1
	b	.L1895
.L1917:
	ldr	r2, .L1920+20
	mov	r6, #0
	ldrb	fp, [r5, #-3195]	@ zero_extendqisi2
	add	r8, sp, #24
	movw	r10, #65535
	mov	r9, r6
	ldrh	r3, [r2, #-4]
	smulbb	fp, fp, r3
	add	r3, r2, #3168
	str	r3, [sp, #12]
	uxth	r7, fp
.L1900:
	ldr	r3, [sp, #8]
	uxth	fp, r6
	cmp	fp, r3
	bcs	.L1919
	add	r3, r6, #208
	ldr	ip, [r4, #2804]
	ldrb	r1, [r5, #-3202]	@ zero_extendqisi2
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
	ldreqb	r1, [r2, #-3195]	@ zero_extendqisi2
	ldreq	r2, .L1920+24
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
	bls	.L1903
	ldr	r0, [r4, #1080]
	mov	r1, r3, asl #2
	ldrh	r1, [r0, r1]
	ldrh	r0, [ip, #92]
	ubfx	r1, r1, #0, #11
	add	r0, r0, #4
	cmp	r1, r0
	bgt	.L1903
	str	r1, [sp]
	mov	r1, r6
	ldr	r0, .L1920+28
	mov	r10, fp
	bl	printk
.L1903:
	ldrh	r2, [r8]
	cmp	r2, #0
	bne	.L1904
	add	fp, r6, #208
	ldr	r1, [r4, #2804]
	movw	r0, #65535
	mov	r3, fp, asl #1
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L1904
	ldr	r0, .L1920+32
	ldr	r0, [r0]
	tst	r0, #4096
	beq	.L1905
	add	r1, r1, #688
	ldr	r0, .L1920+36
	ldrh	r1, [r1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1905:
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
.L1904:
	add	r6, r6, #1
	add	r8, r8, #2
	b	.L1900
.L1919:
	movw	r0, #65535
	cmp	r10, r0
	movne	r0, r10
	moveq	r0, r9
	add	sp, sp, #284
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1921:
	.align	2
.L1920:
	.word	.LANCHOR3
	.word	.LANCHOR1+1924
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3-3200
	.word	.LANCHOR3-3168
	.word	.LANCHOR3-3172
	.word	.LC139
	.word	.LANCHOR2
	.word	.LC140
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
	ldr	r3, .L1929
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1924
	ldr	r0, .L1929+4
	mov	r1, #119
	ldr	r2, .L1929+8
	bl	printk
.L1924:
	ldr	r4, .L1929+12
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1176]
	bl	ftl_memset
	ldr	r3, [r4, #1176]
	ldr	r2, .L1929+16
	ldr	r1, .L1929+20
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
	ldr	r1, .L1929+24
	mov	r2, #32
	add	r0, r0, #48
	ldmfd	sp!, {r4, lr}
	b	ftl_memcpy
.L1930:
	.align	2
.L1929:
	.word	.LANCHOR2
	.word	.LC141
	.word	.LANCHOR1+1940
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
.L1932:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1932
.L1934:
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
	ldr	r3, .L1937
	ldr	r0, [r3]
	ldr	r3, .L1937+4
	ldr	r3, [r3, #-216]
	rsb	r0, r3, r0
	b	jiffies_to_msecs
.L1938:
	.align	2
.L1937:
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
	ldr	r3, .L1941
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #12]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1942:
	.align	2
.L1941:
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
	ldr	r3, .L1945
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #16]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1946:
	.align	2
.L1945:
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
	ldr	r3, .L1949
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #4]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1950:
	.align	2
.L1949:
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
	ldr	r3, .L1953
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #8]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1954:
	.align	2
.L1953:
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
	ldr	ip, .L1959
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #-204]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1958
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1958:
	b	ftl_memcpy
.L1960:
	.align	2
.L1959:
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
	ldr	r0, .L1964
	bl	printk
	ldr	r3, .L1964+4
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #40]
	ldmfd	sp!, {r4, lr}
	bx	r3	@ indirect register sibling call
.L1965:
	.align	2
.L1964:
	.word	.LC142
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
	ldr	r3, .L1967
	mov	r0, #0
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #32]
	bx	r3	@ indirect register sibling call
.L1968:
	.align	2
.L1967:
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
	ldr	r3, .L1970
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #44]
	bx	r3	@ indirect register sibling call
.L1971:
	.align	2
.L1970:
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
	ldr	r3, .L1973
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #48]
	bx	r3	@ indirect register sibling call
.L1974:
	.align	2
.L1973:
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
	ldr	r3, .L1976
	mov	r0, #0
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #36]
	bx	r3
.L1977:
	.align	2
.L1976:
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
	ldr	r3, .L1979
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #60]
	bx	r3
.L1980:
	.align	2
.L1979:
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
	ldr	r3, .L1982
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #64]
	bx	r3
.L1983:
	.align	2
.L1982:
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
	ldr	ip, .L1985
	ldr	ip, [ip, #-208]
	ldr	ip, [ip, #20]
	bx	ip
.L1986:
	.align	2
.L1985:
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
	ldr	r3, .L1988
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #28]
	bx	r3
.L1989:
	.align	2
.L1988:
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
	ldr	r3, .L1991
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #52]
	bx	r3
.L1992:
	.align	2
.L1991:
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
	ldr	r3, .L1994
	ldr	r3, [r3, #-208]
	ldr	r3, [r3, #56]
	bx	r3	@ indirect register sibling call
.L1995:
	.align	2
.L1994:
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
	ldr	r0, .L1998
	mov	r1, ip
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	mov	r3, #16
	bl	print_hex_dump
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1999:
	.align	2
.L1998:
	.word	.LC143
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
	ldr	r8, .L2111
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
	bne	.L2001
	mvn	r3, #88
	strb	r3, [r4, #128]
	ldr	r3, .L2111+4
	mvn	r1, #8
	mov	r6, #7
	strb	r1, [r3, #413]
	b	.L2062
.L2001:
	cmp	r0, #3
	bne	.L2003
	add	r1, r4, #127
	mov	r3, #176
.L2004:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L2004
	b	.L2105
.L2003:
	cmp	r0, #4
	bne	.L2005
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
.L2105:
	mov	r6, #8
	mov	fp, r6
	b	.L2002
.L2005:
	cmp	r0, #5
	bne	.L2006
	mov	r3, #56
	mov	r6, #8
	strb	r3, [r4, #128]
	mov	r3, #57
	strb	r3, [r4, #129]
	mov	r3, #58
	strb	r3, [r4, #130]
	mov	r3, #59
	strb	r3, [r4, #131]
	b	.L2062
.L2006:
	cmp	r0, #6
	bne	.L2007
	mov	r3, #14
	mov	r6, #12
	strb	r3, [r4, #128]
	mov	r3, #15
	strb	r3, [r4, #129]
	mov	r3, #16
	strb	r3, [r4, #130]
	mov	r3, #17
	strb	r3, [r4, #131]
	b	.L2062
.L2007:
	cmp	r0, #7
	bne	.L2008
	add	r1, r4, #127
	mov	r3, #176
.L2009:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L2009
	mvn	r3, #43
	mov	r6, #12
	strb	r3, [r4, #136]
	mov	fp, #10
	mvn	r3, #42
	strb	r3, [r4, #137]
	b	.L2002
.L2008:
	cmp	r0, #8
	mov	r6, #7
	bne	.L2062
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
	b	.L2002
.L2062:
	mov	fp, #4
.L2002:
	sub	r3, r5, #1
	cmp	r3, #1
	bhi	.L2101
	ldr	r1, .L2111+8
	mov	r9, #0
.L2010:
	ldrb	ip, [r8, #1101]	@ zero_extendqisi2
	uxtb	r3, r9
	ldr	r0, .L2111
	cmp	ip, r3
	bls	.L2017
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
.L2012:
	str	ip, [r7, #2056]
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r3, [sp, #12]
	str	r0, [r7, #2052]
	mov	r0, #80
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
	bne	.L2012
	mov	ip, r5
	mov	r0, #0
.L2013:
	add	r7, r1, r0
	mov	r3, #1
.L2014:
	ldrb	lr, [r7, r3, asl #2]	@ zero_extendqisi2
	ldrb	r10, [ip]	@ zero_extendqisi2
	add	lr, lr, r10
	strb	lr, [ip, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L2014
	add	r0, r0, #1
	add	ip, ip, #1
	cmp	r0, #4
	bne	.L2013
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r5, #16]
	strb	r3, [r5, #24]
	strb	r3, [r5, #32]
	strb	r3, [r5, #40]
	strb	r3, [r5, #48]
	strb	r3, [r5, #41]
	strb	r3, [r5, #49]
	b	.L2010
.L2101:
	sub	r3, r5, #3
	cmp	r3, #5
	bhi	.L2017
	smulbb	r2, fp, r6
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #28]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L2109:
	str	r3, [sp, #12]
	ldr	r3, .L2111
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	cmp	r2, r3
	bhi	.L2061
.L2017:
	strb	fp, [r4, #113]
	strb	r6, [r4, #114]
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2061:
	ldr	r2, .L2111
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
	ldr	r3, .L2111
	ldr	r3, [r3, #1040]
	str	r3, [sp, #4]
	ldr	r3, [sp]
	mov	r8, r3, asl #8
	ldr	r3, [sp, #4]
	add	r7, r3, r8
	str	r9, [r7, #2056]
	bl	nandc_wait_flash_ready
	cmp	r5, #8
	bne	.L2019
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
	b	.L2020
.L2019:
	cmp	r5, #4
	mov	r3, #54
	str	r3, [r7, #2056]
	moveq	r3, #64
	streq	r9, [r7, #2052]
	streq	r3, [r7, #2048]
	moveq	r3, #204
	beq	.L2106
.L2021:
	sub	r3, r5, #5
	cmp	r3, #1
	ldrlsb	r3, [r4, #128]	@ zero_extendqisi2
	strls	r3, [r7, #2052]
	movls	r3, #82
	bls	.L2107
	cmp	r5, #7
	bne	.L2022
	mov	r3, #174
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #176
.L2106:
	str	r3, [r7, #2052]
	mov	r3, #77
.L2107:
	str	r3, [r7, #2048]
.L2022:
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
.L2020:
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
	bne	.L2026
	cmp	r5, #7
	movne	ip, #2
	moveq	ip, #32
.L2026:
	ldr	r3, .L2111+12
	ldmia	sp, {r1, r2}
	ldr	r3, [r3, #-200]
	add	r1, r2, r1, asl #8
	mov	r0, r3
.L2027:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L2027
	cmp	r10, #0
	beq	.L2028
	mov	r2, #0
.L2030:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L2029
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L2029
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L2030
	b	.L2031
.L2029:
	cmp	r0, #7
	bne	.L2032
.L2031:
	ldr	r0, .L2111+16
	mov	r1, #0
	bl	printk
.L2033:
	b	.L2033
.L2028:
	cmp	r5, #7
	bne	.L2034
	mov	r2, r10
.L2036:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L2035
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L2035
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L2036
	b	.L2037
.L2035:
	cmp	r0, #7
	bne	.L2032
.L2037:
	ldr	r0, .L2111+16
	mov	r1, #0
	bl	printk
.L2038:
	b	.L2038
.L2034:
	cmp	r5, #6
	bne	.L2032
	sub	r2, r3, #1
	add	r3, r3, #7
.L2039:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L2032
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L2032
	cmp	r2, r3
	bne	.L2039
	ldr	r0, .L2111+16
	mov	r1, #0
	bl	printk
.L2041:
	b	.L2041
.L2032:
	ldr	r3, .L2111+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-200]
	add	r0, r2, r0
	mov	r3, r2
.L2042:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L2042
.L2110:
	ldr	r3, .L2111+12
	mov	r0, #8
	ldr	r1, [sp, #8]
	ldr	r3, [r3, #-200]
	add	r1, r3, r1
	str	r1, [sp, #20]
.L2045:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L2044:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L2044
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L2045
.L2046:
	mov	ip, #0
	mov	r0, ip
.L2049:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r8, #16
	mov	r7, lr
.L2047:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	subs	r8, r8, #1
	add	lr, lr, r9
	bne	.L2047
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L2049
	ldr	r1, [sp, #20]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L2046
	ldr	r3, .L2111+12
	ldr	r1, [r3, #-200]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L2051:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L2051
	cmp	r3, #7
	ble	.L2052
	ldr	r0, .L2111+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2111+16
	mov	r1, #0
	bl	printk
.L2053:
	b	.L2053
.L2052:
	cmp	r5, #6
	moveq	r0, #4
	beq	.L2054
	cmp	r5, #7
	moveq	r0, #10
	beq	.L2054
	cmp	r10, #0
	moveq	r0, #8
	movne	r0, #5
.L2054:
	sub	r9, fp, #1
	ldr	r1, [sp, #24]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L2055:
	mov	r7, r1
	mov	r3, r2
.L2056:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, fp
	bcc	.L2056
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r6
	add	r1, r1, r0
	blt	.L2055
	ldmia	sp, {r2, r3}
	mov	r8, #255
	add	r7, r3, r2, asl #8
	str	r8, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L2058
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
	b	.L2108
.L2058:
	cmp	r10, #0
	movne	r3, #190
	moveq	r3, #56
.L2108:
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2109
.L2112:
	.align	2
.L2111:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+396
	.word	.LANCHOR3
	.word	.LC144
	.word	.LC145
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
	ldr	r3, .L2115
	ldrb	r0, [r3, #23]	@ zero_extendqisi2
	sub	r3, r0, #1
	cmp	r3, #7
	bxhi	lr
	b	hynix_get_read_retry_default
.L2116:
	.align	2
.L2115:
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
	ldr	r3, .L2166
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r5, #0
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	mov	r4, r3
	ldr	r7, .L2166+4
	cmp	r2, #9
	strb	r5, [r7, #-196]
	bne	.L2118
	ldr	r6, [r3, #1040]
	ldr	r3, [r6, #16]
	str	r3, [sp]
	ldr	r3, [r6, #48]
	ubfx	r3, r3, #1, #1
	cmp	r3, r5
	moveq	r5, r3
	moveq	r7, r4
	beq	.L2120
.L2119:
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #6
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2122
	ldr	r3, [r4, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2121
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2121
	ldr	r1, [sp, #4]
	ldr	r0, .L2166+8
	ubfx	r1, r1, #17, #1
	bl	printk
	b	.L2122
.L2121:
	ldr	r3, [sp]
	add	r5, r5, #1
	ubfx	r3, r3, #22, #6
	cmp	r5, r3, asl #12
	bne	.L2123
	ldr	r2, [r6, #64]
	mov	r1, r5
	ldr	r3, [sp]
	ldr	r0, .L2166+12
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r3, [sp, #4]
	tst	r3, #8192
	mov	r3, #1
	strb	r3, [r7, #-196]
	bne	.L2122
	ldr	r3, .L2166+16
	ldr	r0, .L2166+20
	ldr	r3, [r3, #4]
	blx	r3
	b	.L2122
.L2123:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2119
.L2122:
	ldr	r3, [r4, #1160]
	ldr	r5, .L2166
	cmp	r3, #0
	beq	.L2127
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
	b	.L2165
.L2129:
	ldr	r3, [r4, #1040]
	add	r5, r5, #1
	ldr	r3, [r3, #16]
	str	r3, [sp]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2128
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #64]
	ldr	r0, .L2166+24
	ubfx	r3, r3, #16, #6
	bl	printk
	ldr	r0, .L2166+28
	ldr	r1, [r7, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2128:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2120:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2129
	ldr	r3, [r4, #1160]
	ldr	r5, .L2166
	cmp	r3, #0
	beq	.L2127
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
	b	.L2164
.L2118:
	ldr	r7, [r3, #1040]
	ldr	r3, [r7, #8]
	str	r3, [sp]
	ldr	r6, [r7, #16]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	moveq	r5, r4
	beq	.L2132
.L2131:
	ldr	r2, [r7, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2134
	ldr	r3, [r4, #1040]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2133
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2133
	ldr	r1, [sp, #4]
	ldr	r0, .L2166+32
	bl	printk
	b	.L2134
.L2133:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2135
	ldr	r2, [r7, #28]
	mov	r1, r5
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L2166+12
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L2166+28
	ldr	r1, [r4, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2135:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2131
.L2134:
	ldr	r3, [r4, #1160]
	ldr	r5, .L2166
	cmp	r3, #0
	beq	.L2127
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
.L2165:
	mov	r2, #0
	b	.L2163
.L2138:
	ldr	r3, [r4, #1040]
	add	r6, r6, #1
	ldr	r3, [r3, #8]
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L2137
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r7, #28]
	ldr	r0, .L2166+24
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L2166+28
	ldr	r1, [r5, #1040]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2137:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2132:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2138
	ldr	r3, [r4, #1160]
	ldr	r5, .L2166
	cmp	r3, #0
	beq	.L2127
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
.L2164:
	mov	r2, #1
.L2163:
	bl	rknand_dma_unmap_single
.L2127:
	mov	r3, #0
	str	r3, [r4, #1160]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2167:
	.align	2
.L2166:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC146
	.word	.LC147
	.word	arm_delay_ops
	.word	2147483000
	.word	.LC148
	.word	.LC149
	.word	.LC150
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
	bne	.L2198
	ldr	r5, .L2211
	ldrb	r3, [r5, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2170
	ldr	ip, [r5, #1040]
	mov	r2, r8, lsr #2
	mov	r1, #1
	mov	lr, r4
	mov	r0, r4
.L2171:
	cmp	lr, r2
	bcs	.L2208
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
	bne	.L2190
	ldr	r3, [sp, #4]
	tst	r3, #262144
	bne	.L2190
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
	b	.L2172
.L2190:
	mvn	r0, #0
.L2172:
	add	lr, lr, #1
	b	.L2171
.L2208:
	ldr	r3, [r5, #1040]
	cmp	r1, #0
	movne	r0, #512
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2177
	mov	r1, r4
	ldr	r0, .L2211+4
	bl	printk
	ldr	r3, [r5, #1040]
	mvn	r0, #0
	orr	r4, r4, #131072
	str	r4, [r3]
.L2177:
	tst	r4, #8192
	beq	.L2178
	ldr	r3, .L2211+8
	ldrb	r3, [r3, #-196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2178
	ldr	r0, .L2211+12
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1040]
	mov	r2, #1
	str	r2, [r3, #16]
	b	.L2179
.L2170:
	ldrb	r3, [r5, #1172]	@ zero_extendqisi2
	mov	r1, r8, lsr #1
	mov	r2, r4
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L2181:
	cmp	r2, r1
	add	r0, r4, ip
	add	r3, r3, #4
	bcs	.L2209
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
	b	.L2181
.L2209:
	ldr	ip, [r5, #1040]
	mov	r1, #0
	mov	r2, r8, lsr #2
	mov	r0, r1
.L2183:
	cmp	r1, r2
	bcs	.L2210
	add	r3, r1, #8
	ldr	r3, [ip, r3, asl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2193
	ldr	r3, [sp, #4]
	tst	r3, #32768
	bne	.L2193
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
	b	.L2184
.L2193:
	mvn	r0, #0
.L2184:
	add	r1, r1, #1
	b	.L2183
.L2210:
	ldr	r3, [r5, #1040]
	mov	r2, #0
	str	r2, [r3, #16]
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2178
	mov	r1, r4
	ldr	r0, .L2211+16
	bl	printk
	ldr	r3, .L2211
	orr	r4, r4, #131072
	ldr	r3, [r3, #1040]
	str	r4, [r3]
	b	.L2179
.L2178:
	cmn	r0, #1
	beq	.L2198
	ldr	r3, [r7]
	cmn	r3, #1
	bne	.L2198
	ldr	r3, [r7, #4]
	cmn	r3, #1
	bne	.L2198
	ldr	r3, [r6]
	cmn	r3, #1
	moveq	r0, #512
	b	.L2198
.L2179:
	mvn	r0, #0
.L2198:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2212:
	.align	2
.L2211:
	.word	.LANCHOR0
	.word	.LC151
	.word	.LANCHOR3
	.word	.LC152
	.word	.LC153
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
	ldr	r10, .L2229
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
	bne	.L2214
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	b	.L2215
.L2214:
	ldr	r2, [r7, #1096]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L2216
	ldrb	r2, [r7, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2216
	ldrb	r2, [r7, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r2, r5, r10
	streq	r9, [r2, #2056]
	beq	.L2215
.L2216:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2215:
	ldr	r2, [r7, #1096]
	ldrb	r1, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2217
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	cmp	r1, #2
	addeq	r1, r5, r10
	moveq	r0, #38
	streq	r0, [r1, #2056]
.L2217:
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
	beq	.L2219
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2219
	ldr	r3, .L2229
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r4, r4, r4, asl #1
	subeq	r0, r4, #1
	addeq	r0, r0, r9
	beq	.L2228
.L2219:
	mov	r0, r4
.L2228:
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
.L2230:
	.align	2
.L2229:
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
	ldr	r4, .L2259
	mov	r5, r0
	str	r3, [sp, #24]
	mov	r9, r1
	str	r2, [sp, #20]
	mov	r8, #0
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	ldr	r10, .L2259+4
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r4, #1040]
	str	r3, [sp, #16]
.L2232:
	ldr	r3, [sp, #16]
	mov	r7, #0
	mvn	r4, #0
	add	r6, r3, r5, asl #8
.L2233:
	ldrb	r3, [r10, #-188]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L2237
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
	ldr	r3, .L2259+8
	ldr	r3, [r3]
	tst	r3, #4096
	mov	ip, r0
	beq	.L2234
	str	r0, [sp]
	mov	r1, r7
	str	r0, [sp, #28]
	mov	r2, r9
	ldr	r0, .L2259+12
	mov	r3, r4
	bl	printk
	ldr	ip, [sp, #28]
.L2234:
	cmn	ip, #1
	beq	.L2235
	ldr	r3, [r10, #-200]
	cmn	r4, #1
	moveq	r4, ip
	str	r3, [sp, #20]
	ldr	r3, [r10, #-192]
	str	r3, [sp, #24]
	ldr	r3, [sp, #12]
	cmp	ip, r3
	bcc	.L2244
.L2235:
	mov	r7, fp
	b	.L2233
.L2244:
	mov	r4, ip
.L2237:
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
	bcc	.L2239
	cmn	r4, #1
	movne	r4, #256
.L2239:
	cmn	r4, #1
	movne	fp, #0
	moveq	fp, #1
	cmp	r4, #256
	movne	r3, fp
	orreq	r3, fp, #1
	cmp	r3, #0
	beq	.L2240
	mov	r1, r7
	mov	r3, r7
	str	r4, [sp]
	mov	r2, r9
	ldr	r0, .L2259+16
	eor	r7, r8, #1
	bl	printk
	ands	r7, fp, r7
	beq	.L2241
	bl	nandc_wait_flash_ready
	ldr	r3, .L2259
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
	b	.L2232
.L2241:
	cmp	r8, #0
	beq	.L2242
	bl	nandc_wait_flash_ready
	ldr	r3, .L2259
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
	b	.L2242
.L2240:
	cmp	r8, #0
	beq	.L2242
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r2, .L2259
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
.L2242:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2260:
	.align	2
.L2259:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC154
	.word	.LC155
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
	mov	r10, r0
	mov	fp, r3
	mov	r9, r1
	str	r2, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r7, .L2309
	add	r6, r10, #8
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
	bne	.L2263
	ldr	r3, [sp, #8]
	mov	r8, #1
	add	r3, r3, r10, asl #8
	str	r3, [sp, #16]
.L2270:
	ldr	r3, .L2309
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2264
	mov	r2, #0
	mov	r1, r8
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	ldr	r2, [sp, #16]
	str	r3, [r2, #2056]
	b	.L2265
.L2264:
	uxtb	r1, r8
	bl	toshiba_3d_set_slc_rr_para
.L2265:
	ldr	r3, [sp, #64]
	mov	r0, r10
	mov	r1, r9
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2309+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2266
	mov	r3, ip
	ldr	r0, .L2309+8
	mov	r1, r8
	mov	r2, r9
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2266:
	cmn	ip, #1
	beq	.L2267
	ldr	r3, .L2309+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-200]
	ldr	fp, [r3, #-192]
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2285
.L2267:
	add	r8, r8, #1
	cmp	r8, r5
	bne	.L2270
	b	.L2269
.L2285:
	mov	r5, r8
	mov	r4, ip
.L2269:
	ldr	r3, .L2309
	mov	r1, #0
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	moveq	r2, r1
	beq	.L2308
	bl	toshiba_3d_set_slc_rr_para
	b	.L2272
.L2263:
	ldr	r3, [sp, #8]
	mov	r5, #1
	add	r8, r3, r10, asl #8
.L2279:
	ldr	r3, .L2309
	mov	r0, r6
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2273
	mov	r1, r5
	mov	r2, #1
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	b	.L2307
.L2273:
	uxtb	r1, r5
	bl	toshiba_3d_set_tlc_rr_para
	mov	r3, #38
.L2307:
	str	r3, [r8, #2056]
	mov	r0, r10
	ldr	r3, [sp, #64]
	mov	r1, r9
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2309+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2275
	mov	r3, ip
	ldr	r0, .L2309+16
	mov	r1, r5
	mov	r2, r9
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2275:
	cmn	ip, #1
	beq	.L2276
	ldr	r3, .L2309+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-200]
	ldr	fp, [r3, #-192]
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2286
.L2276:
	ldr	r3, [sp, #16]
	add	r5, r5, #1
	cmp	r5, r3
	bne	.L2279
	b	.L2278
.L2286:
	mov	r4, ip
.L2278:
	ldr	r3, .L2309
	mov	r0, r6
	mov	r1, #0
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2280
	mov	r2, #1
.L2308:
	bl	toshiba_tlc_set_rr_para
	b	.L2272
.L2280:
	bl	toshiba_3d_set_tlc_rr_para
.L2272:
	ldr	r3, .L2309
	ldrb	r3, [r3, #1173]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2281
	ldr	r3, [sp, #8]
	add	r10, r3, r10, asl #8
	mov	r3, #85
	str	r3, [r10, #2056]
	mov	r3, #0
	str	r3, [r10, #2052]
	str	r3, [r10, #2048]
	mov	r3, #255
	str	r3, [r10, #2056]
.L2281:
	ldrb	r3, [r7, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2282
	cmn	r4, #1
	movne	r4, #256
.L2282:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2283
	str	r4, [sp]
	mov	r1, r5
	ldr	r0, .L2309+20
	mov	r2, r9
	mov	r3, r5
	bl	printk
.L2283:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2310:
	.align	2
.L2309:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC156
	.word	.LANCHOR3
	.word	.LC157
	.word	.LC158
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
	ldr	r4, .L2344
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
	bls	.L2312
	ldrb	r6, [r4, #1135]	@ zero_extendqisi2
	cmp	r6, #0
	streq	r6, [sp, #8]
	beq	.L2313
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #1
	str	r3, [sp, #8]
.L2313:
	ldr	r3, [sp, #24]
	mov	r2, #92
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L2312:
	ldr	fp, .L2344+4
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r5, asl #8
	str	r3, [sp, #28]
.L2314:
	ldrb	r3, [fp, #-188]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L2343
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	mov	r0, r9
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2315
	bl	sandisk_set_rr_para
	b	.L2316
.L2315:
	bl	toshiba_set_rr_para
.L2316:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L2317
	ldr	r3, .L2344+4
	ldrb	r3, [r3, #-188]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #28]
	moveq	r2, #179
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L2317:
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
	beq	.L2320
	ldr	r3, [fp, #-200]
	cmn	r10, #1
	moveq	r10, r0
	str	r3, [sp, #16]
	ldr	r3, [fp, #-192]
	str	r3, [sp, #20]
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2322
.L2320:
	add	r8, r8, #1
	b	.L2314
.L2343:
	mov	r6, r10
.L2322:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, #0
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2324
	bl	sandisk_set_rr_para
	b	.L2325
.L2324:
	bl	toshiba_set_rr_para
.L2325:
	add	r5, r7, r5, asl #8
	mov	r3, #255
	str	r3, [r5, #2056]
	ldrb	r3, [r4, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L2326
	cmn	r6, #1
	movne	r6, #256
.L2326:
	cmn	r6, #1
	cmpne	r6, #256
	bne	.L2327
	str	r6, [sp]
	mov	r1, r8
	ldr	r0, .L2344+8
	mov	r3, r8
	ldr	r2, [sp, #12]
	bl	printk
.L2327:
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L2328
	mov	r0, #4
	bl	nandc_set_if_mode
.L2328:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2345:
	.align	2
.L2344:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC158
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
	ldr	r6, .L2376
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
	ldr	ip, .L2376+4
	bne	.L2356
.L2351:
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
	beq	.L2348
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-200]
	moveq	r4, r0
	ldr	fp, [ip, #-192]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2359
.L2348:
	add	r5, r5, #1
	cmp	r5, #10
	bne	.L2351
	b	.L2350
.L2359:
	mov	r4, r0
.L2350:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_slc_rr_para
	b	.L2352
.L2375:
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-200]
	moveq	r4, r0
	ldr	fp, [ip, #-192]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2360
.L2353:
	add	r5, r5, #1
	cmp	r5, #51
	beq	.L2355
.L2356:
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
	bne	.L2375
	b	.L2353
.L2360:
	mov	r4, r0
.L2355:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_tlc_rr_para
.L2352:
	ldrb	r3, [r6, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2357
	cmn	r4, #1
	movne	r4, #256
.L2357:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2358
	str	r4, [sp]
	mov	r1, r5
	ldr	r0, .L2376+8
	mov	r2, r9
	mov	r3, r5
	bl	printk
.L2358:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2377:
	.align	2
.L2376:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC159
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
	ldr	r3, .L2416
	tst	r8, #50331648
	ldr	r4, [r3, #1040]
	str	r3, [sp, #16]
	bne	.L2379
	mov	r10, r7, asl #8
	mvn	r5, #0
	add	r9, r4, r10
	mov	r6, #1
	mov	fp, #239
.L2384:
	str	fp, [r9, #2056]
	mov	r3, #141
	str	r3, [r9, #2052]
	ldr	r3, .L2416+4
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
	ldr	r3, .L2416+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2380
	mov	r3, ip
	ldr	r0, .L2416+12
	mov	r1, r6
	mov	r2, r8
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2380:
	cmn	ip, #1
	beq	.L2381
	ldr	r3, .L2416+16
	cmn	r5, #1
	moveq	r5, ip
	ldr	r2, [r3, #-200]
	ldr	r3, [r3, #-192]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2393
.L2381:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L2384
	b	.L2383
.L2393:
	mov	r5, ip
.L2383:
	add	r10, r4, r10
	mov	r3, #239
	add	r4, r4, r7, asl #8
	str	r3, [r10, #2056]
	mov	r3, #141
	b	.L2415
.L2379:
	mov	r3, r7, asl #8
	ldr	r9, .L2416+20
	add	r10, r4, r3
	mvn	r5, #0
	mov	r6, #1
	str	r3, [sp, #20]
.L2390:
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
	ldr	r3, .L2416+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	fp, r0
	beq	.L2386
	ldr	r0, .L2416+24
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L2386:
	cmn	fp, #1
	beq	.L2387
	ldr	r3, .L2416+16
	cmn	r5, #1
	moveq	r5, fp
	ldr	r2, [r3, #-200]
	ldr	r3, [r3, #-192]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	fp, r3, asr #2
	bcc	.L2394
.L2387:
	add	r6, r6, #1
	add	r9, r9, #4
	cmp	r6, #26
	bne	.L2390
	b	.L2389
.L2394:
	mov	r5, fp
.L2389:
	ldr	r3, [sp, #20]
	mov	r2, #239
	add	r3, r4, r3
	add	r4, r4, r7, asl #8
	str	r2, [r3, #2056]
	mov	r3, #137
.L2415:
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
	bcc	.L2391
	cmn	r5, #1
	movne	r5, #256
.L2391:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L2392
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L2416+28
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2392:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2417:
	.align	2
.L2416:
	.word	.LANCHOR0
	.word	.LANCHOR1+1964
	.word	.LANCHOR2
	.word	.LC160
	.word	.LANCHOR3
	.word	.LANCHOR1+1992
	.word	.LC161
	.word	.LC162
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
	ldr	r3, .L2437
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
	ldr	ip, .L2437+4
.L2419:
	cmp	r7, r9
	bcs	.L2423
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
	beq	.L2421
	ldr	r3, [sp, #16]
	cmn	r4, #1
	ldr	r10, [ip, #-200]
	moveq	r4, r0
	ldr	fp, [ip, #-192]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2427
.L2421:
	add	r7, r7, #1
	b	.L2419
.L2427:
	mov	r4, r0
.L2423:
	ldr	r3, [sp, #12]
	strb	r5, [r3, #120]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2425
	cmn	r4, #1
	movne	r4, #256
.L2425:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2426
	str	r4, [sp]
	mov	r1, r7
	ldr	r0, .L2437+8
	mov	r2, r8
	mov	r3, r7
	bl	printk
.L2426:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2438:
	.align	2
.L2437:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC163
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
.L2445:
	uxtb	r0, r7
	bl	nandc_set_ddr_para
	mov	r0, r8
	mov	r1, r10
	mov	r2, fp
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	ldr	r3, .L2466
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2440
	mov	r3, ip
	ldr	r0, .L2466+4
	mov	r1, r7
	mov	r2, r10
	str	ip, [sp, #28]
	bl	printk
	ldr	ip, [sp, #28]
.L2440:
	add	r3, r6, #1
	cmp	ip, r3
	bhi	.L2441
	ldr	r3, .L2466+8
	ldr	fp, [r3, #-200]
	ldr	r3, [r3, #-192]
	str	r3, [sp, #16]
	ldr	r3, .L2466+12
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	cmp	ip, r3, lsr #2
	bcs	.L2451
	add	r5, r5, #1
	cmp	r5, #7
	bls	.L2451
	rsb	r4, r5, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2443
.L2441:
	ldr	r3, [sp, #12]
	cmp	r3, r5
	bcs	.L2452
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #20]
	bhi	.L2444
	str	r5, [sp, #12]
	b	.L2452
.L2451:
	mov	r4, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2442
.L2452:
	mov	r5, #0
.L2442:
	add	r7, r7, #2
	cmp	r7, #50
	bne	.L2445
.L2443:
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r2, r5
	movhi	r4, r3
.L2444:
	cmp	r4, #0
	beq	.L2446
	ldr	r3, .L2466+12
	ldr	r2, .L2466+16
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	umull	r2, r3, r3, r2
	ubfx	r3, r3, #1, #8
	cmp	r6, r3
	bcs	.L2446
	ldr	r0, .L2466+20
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	b	.L2465
.L2446:
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
.L2465:
	bl	nandc_set_ddr_para
	cmn	r9, #1
	bne	.L2448
	ldr	r5, .L2466+12
	ldrb	r3, [r5, #1232]	@ zero_extendqisi2
	mov	r4, r5
	tst	r3, #1
	beq	.L2448
	mov	r1, r8
	mov	r2, r10
	ldr	r0, .L2466+24
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
	ldr	r0, .L2466+28
	mov	r3, r6
	bl	printk
	ldrb	r3, [r5, #1172]	@ zero_extendqisi2
	cmp	r6, r3
	bhi	.L2453
	ldr	r2, .L2466+8
	ldr	r3, [r2, #-184]
	add	r3, r3, #1
	str	r3, [r2, #-184]
	cmp	r3, #100
	movhi	r3, #0
	strhib	r3, [r5, #1135]
	bls	.L2449
	b	.L2448
.L2453:
	mov	r6, r9
.L2449:
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	nandc_set_if_mode
.L2448:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2467:
	.align	2
.L2466:
	.word	.LANCHOR2
	.word	.LC164
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC165
	.word	.LC166
	.word	.LC167
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
	ldr	r5, .L2499
	mov	r10, r0
	mov	r4, r1
	mov	r8, r2
	ldr	r9, [sp, #48]
	ldrb	r3, [r5, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2469
	ldr	r1, .L2499+4
	mov	r2, #428
	ldr	r0, .L2499+8
	bl	printk
	bl	dump_stack
.L2469:
	add	r2, r5, r10
	ldr	r3, .L2499
	ldrb	r6, [r2, #1180]	@ zero_extendqisi2
	ldrb	r2, [r5, #1101]	@ zero_extendqisi2
	cmp	r10, r2
	bcc	.L2470
	ldr	r3, .L2499+12
	ldr	r3, [r3]
	tst	r3, #64
	beq	.L2498
	str	r2, [sp]
	mov	r1, r6
	ldr	r0, .L2499+16
	mov	r2, r10
	mov	r3, r4
	bl	printk
.L2498:
	mvn	r0, #0
	b	.L2495
.L2470:
	tst	r4, #50331648
	bne	.L2473
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2474
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2473
.L2474:
	ldrh	r10, [r5, #2]
	mov	r0, r4
	mov	r1, r10
	bl	__aeabi_uidiv
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L2499
	mul	r0, r10, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L2473:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	cmn	r0, #1
	bne	.L2495
	ldrb	fp, [r5, #1168]	@ zero_extendqisi2
	ldr	r10, .L2499
	cmp	fp, #0
	bne	.L2477
.L2480:
	ldr	r3, .L2499+20
	ldr	ip, [r3, #-180]
	cmp	ip, #0
	bne	.L2478
	b	.L2479
.L2477:
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
	beq	.L2480
	b	.L2495
.L2478:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	blx	ip
	cmn	r0, #1
	bne	.L2495
.L2479:
	ldrb	r3, [r5, #1168]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2499+24
	mov	r2, r4
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2498
	str	r9, [sp, #48]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	flash_ddr_tuning_read
.L2495:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2500:
	.align	2
.L2499:
	.word	.LANCHOR0
	.word	.LANCHOR1+2096
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC168
	.word	.LANCHOR3
	.word	.LC169
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
	ldr	r6, .L2512
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L2512+4
	mov	r10, r0
	mov	fp, r2
	mov	r8, r3
	ldrh	r7, [r6, #30]
	ldrh	r4, [r1]
	ldr	r1, [sp, #56]
	sub	r4, r4, #1
	mul	r7, r7, r9
	uxth	r4, r4
	str	r1, [sp]
	sxtah	r1, r7, r4
	bl	flash_read_page_en
	cmp	r0, #512
	moveq	ip, #0
	bne	.L2502
.L2503:
	sxth	r3, ip
	sxth	r2, r4
	cmp	r3, r2
	bgt	.L2502
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
	b	.L2503
.L2502:
	ldr	r3, [r6]
	sxth	r4, r4
	tst	r3, #4096
	beq	.L2507
	ldr	r0, .L2512+8
	mov	r1, r9
	mov	r2, r4
	ldr	r3, [r8]
	bl	printk
.L2507:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2513:
	.align	2
.L2512:
	.word	.LANCHOR2
	.word	.LANCHOR3-292
	.word	.LC170
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
	ldr	r2, .L2516
	mov	r4, r1
	ldr	r1, .L2516+4
	mov	ip, r0
	ldrb	r2, [r2, #1189]	@ zero_extendqisi2
	ldrh	r1, [r1, #-4]
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
.L2517:
	.align	2
.L2516:
	.word	.LANCHOR0
	.word	.LANCHOR3-3200
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
	ldr	r4, .L2525
	mov	r7, r1
	mov	r6, #1
	ldr	r5, .L2525+4
	mov	r9, #4
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	strb	r6, [r4, #1135]
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1232]	@ zero_extendqisi2
	bl	nandc_set_if_mode
	mov	r1, r7
	ldr	r2, [r5, #-176]
	mov	r0, r8
	ldr	r3, [r5, #-172]
	str	r9, [sp]
	bl	flash_ddr_tuning_read
	ldr	r3, [r5, #-172]
	mov	r0, r8
	mov	r1, r7
	ldr	r2, [r5, #-176]
	str	r9, [sp]
	bl	flash_read_page
	mov	r3, r4
	cmn	r0, #1
	bne	.L2519
	ldrb	r2, [r4, #1232]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L2519
	mov	r0, r6
	bl	flash_set_interface_mode
	mov	r0, r6
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #1135]
	b	.L2520
.L2519:
	mov	r2, #1
	strb	r2, [r3, #1135]
.L2520:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2526:
	.align	2
.L2525:
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
	ldr	r3, .L2558
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #124
	sub	sp, sp, #124
	ldr	r4, .L2558+4
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
.L2528:
	ldr	r3, [sp, #24]
	cmp	r5, r3
	bcs	.L2557
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
	beq	.L2530
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r0, r3
.L2530:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #16]
	mla	r3, r3, r0, r2
	str	r3, [sp, #36]
	ldr	r3, .L2558+4
	ldr	r2, [sp, #36]
	ldrb	r9, [r3, #1100]	@ zero_extendqisi2
	add	r0, r2, r1
	ldr	r3, [r3, #1096]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, .L2558+4
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	mov	fp, r0
	mov	r0, r9
	str	r3, [sp, #48]
	bl	nandc_bch_sel
.L2531:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_read_page
	cmn	r0, #1
	mov	r8, r0
	bne	.L2538
	ldrb	ip, [r4, #1168]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L2533
.L2536:
	ldr	r3, .L2558+8
	ldr	ip, [r3, #-180]
	cmp	ip, #0
	bne	.L2534
	b	.L2535
.L2533:
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
	beq	.L2536
	b	.L2556
.L2534:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	blx	ip
	cmn	r0, #1
	bne	.L2556
.L2535:
	ldrb	r3, [r4, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2538
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_ddr_tuning_read
.L2556:
	mov	r8, r0
.L2538:
	subs	r9, r9, #16
	movne	r9, #1
	cmn	r8, #1
	movne	r9, #0
	cmp	r9, #0
	beq	.L2539
	mov	r0, #16
	mov	r9, #16
	bl	nandc_bch_sel
	b	.L2531
.L2539:
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
	beq	.L2541
	ldr	r3, [r6]
	ldr	r2, .L2558+12
	cmp	r3, r2
	bne	.L2541
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #20]
	ldrb	r7, [r6, #17]	@ zero_extendqisi2
	add	r3, r3, r2
	str	r3, [sp, #24]
	b	.L2542
.L2541:
	ldr	r3, [sp, #20]
	mov	r2, r5
	ldr	r0, .L2558+16
	ldr	r1, [sp, #36]
	add	r6, r6, r3, asl #9
	ldr	r3, [sp, #60]
	str	r3, [sp]
	ldr	r3, [sp, #56]
	bl	printk
	mov	r3, #0
	str	r3, [sp, #16]
.L2542:
	ldr	r3, [sp, #20]
	add	r5, r3, r5
	uxth	r5, r5
	b	.L2528
.L2557:
	ldr	r3, .L2558+4
	mov	r0, r10
	ldrb	r2, [sp, #44]	@ zero_extendqisi2
	strb	r2, [r3, #1168]
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2559:
	.align	2
.L2558:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	1179535694
	.word	.LC171
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
	ldr	r3, .L2576
	mov	r10, r2
	mvn	r5, #0
	add	r8, r0, #8
	mov	r4, r0
	.pad #20
	sub	sp, sp, #20
	ldrb	r2, [r3, #1189]	@ zero_extendqisi2
	mov	r7, r1
	ldr	r6, [r3, #1040]
	ubfx	r9, r1, #24, #2
	rsb	r2, r2, #24
	str	r3, [sp, #8]
	bic	r5, r1, r5, asl r2
	ldr	r2, .L2576+4
	add	r8, r6, r8, asl #8
	ldr	r0, [r2]
	str	r2, [sp, #12]
	tst	r0, #16
	beq	.L2561
	ldr	r0, .L2576+8
	mov	r2, r9
	ldr	r3, [sp, #56]
	bl	printk
.L2561:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	bl	hynix_reconfig_rr_para
	mov	r0, r4
	bl	nandc_cs
	cmp	r9, #0
	mov	r0, r4
	bne	.L2562
	bl	zftl_flash_enter_slc_mode
	b	.L2563
.L2562:
	bl	zftl_flash_exit_slc_mode
.L2563:
	mov	r9, r4, asl #8
	mov	r2, #128
	add	r3, r6, r9
	mov	r0, r5
	add	r6, r6, r9
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
	ldr	r2, [sp, #8]
	ldrb	r2, [r2, #1188]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r5, lsr #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	mov	r1, #1
	mov	r0, r4
	ldr	r3, [sp, #12]
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	mov	r3, #16
	str	r3, [r6, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	mov	r5, r0
	mov	r0, r4
	bl	nandc_de_cs
	ands	r2, r5, #4
	beq	.L2565
	ldr	r0, .L2576+12
	mov	r1, r7
	bl	printk
	mvn	r2, #0
.L2565:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2577:
	.align	2
.L2576:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC172
	.word	.LC173
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
	ldr	r4, .L2589
	mov	r5, r1
	mov	r2, #32
	mov	r1, #165
	ldr	r0, [r4, #-200]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r4, #-192]
	bl	ftl_memset
	ldr	r3, .L2589+4
	mov	r0, r6
	ldrh	r3, [r3, #2]
	mul	r5, r3, r5
	mov	r1, r5
	bl	flash_erase_block
	cmn	r0, #1
	bne	.L2579
.L2581:
	mvn	r4, #0
	b	.L2580
.L2579:
	ldr	r7, .L2589+8
	mov	r0, r6
	mov	r1, r5
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r2, [r4, #-200]
	ldr	r3, [r4, #-192]
	bl	flash_prog_page
	cmn	r0, #1
	beq	.L2581
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r6
	mov	r1, r5
	str	r3, [sp]
	ldr	r2, [r4, #-200]
	ldr	r3, [r4, #-192]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L2581
	ldr	r3, [r4, #-200]
	ldr	r2, [r3]
	ldr	r3, .L2589+12
	cmp	r2, r3
	bne	.L2581
	ldr	r3, [r4, #-192]
	ldr	r4, .L2589+16
	ldr	r3, [r3]
	subs	r4, r3, r4
	mvnne	r4, #0
.L2580:
	mov	r0, r6
	mov	r1, r5
	bl	flash_erase_block
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2590:
	.align	2
.L2589:
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
	ldr	r6, .L2601
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
	ldr	r3, .L2601+4
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
.L2602:
	.align	2
.L2601:
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
	ldr	r4, .L2616
	add	r8, r0, #8
	ldr	r6, .L2616+4
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
	beq	.L2604
	ldr	r0, .L2616+8
	mov	r2, fp
	ldr	r3, [sp, #64]
	bl	printk
.L2604:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r5
	bne	.L2605
	bl	zftl_flash_enter_slc_mode
	b	.L2606
.L2605:
	bl	zftl_flash_exit_slc_mode
.L2606:
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
	beq	.L2607
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L2608
	ldr	r0, .L2616+12
	mov	r1, r7
	bl	printk
.L2608:
	mvn	r2, #0
.L2607:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2617:
	.align	2
.L2616:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC172
	.word	.LC173
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
	ldr	r4, .L2625
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
	beq	.L2619
	ldr	r3, .L2625+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2620
	ldr	r0, .L2625+8
	mov	r1, r10
	bl	printk
.L2620:
	mvn	r2, #0
.L2619:
	mov	r0, r2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2626:
	.align	2
.L2625:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC174
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
	ldr	r4, .L2633
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
	ldr	r3, .L2633+4
	ldrb	r2, [r3, #11]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L2628
	ldrb	r4, [r4, #1102]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L2628
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2629
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	ymtc_flash_tlc_page_prog
	b	.L2632
.L2629:
	ldr	r9, .L2633+8
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r9, #-200]
	bl	ftl_memset
	ldr	r3, [r9, #-200]
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	str	r3, [sp]
	str	r3, [sp, #4]
	mov	r3, #4
	str	r3, [sp, #8]
	mov	r3, r5
	bl	flash_dual_page_prog
	b	.L2632
.L2628:
	mov	r3, #4
	mov	r0, #0
	str	r3, [sp]
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	flash_prog_page
.L2632:
	mov	r4, r0
	mov	r0, r8
	bl	nandc_bch_sel
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2634:
	.align	2
.L2633:
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
	ldr	r3, .L2643
	mov	r8, r2
	add	r5, sp, #48
	mov	fp, r0
	mov	r7, r1
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	mov	r6, r3
	ldmia	r5, {r5, r9, r10}
	cmp	r2, r4
	bhi	.L2636
	ldr	r1, .L2643+4
	movw	r2, #859
	ldr	r0, .L2643+8
	bl	printk
	bl	dump_stack
.L2636:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L2643
	cmp	r2, r4
	bls	.L2635
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
	ldr	r3, .L2643+12
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
.L2635:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2644:
	.align	2
.L2643:
	.word	.LANCHOR0
	.word	.LANCHOR1+2116
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
	ldr	r2, .L2650
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
	beq	.L2646
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
	b	.L2647
.L2646:
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
.L2647:
	cmp	r7, #0
	beq	.L2645
	ldr	r1, [r6]
	mov	r3, #4
	ldr	r0, .L2650+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2645:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2651:
	.align	2
.L2650:
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
	ldr	r3, .L2656
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
	beq	.L2653
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
	b	.L2654
.L2653:
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
.L2654:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r0, [r5, #24]
	mov	r1, #64
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready
.L2657:
	.align	2
.L2656:
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
	ldr	r3, .L2662
	mov	r5, r2
	mov	r7, r0
	mov	r4, r1
	ldrb	r2, [r3, #1101]	@ zero_extendqisi2
	mov	r6, r3
	cmp	r2, r1
	bhi	.L2659
	ldr	r1, .L2662+4
	mov	r2, #892
	ldr	r0, .L2662+8
	bl	printk
	bl	dump_stack
.L2659:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L2662
	cmp	r2, r4
	bls	.L2658
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
	ldr	r3, .L2662+12
	mov	r0, r6
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	str	r3, [sp]
	mov	r3, r8
	bl	nandc_xfer
	str	r7, [r4, #2056]
.L2658:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2663:
	.align	2
.L2662:
	.word	.LANCHOR0
	.word	.LANCHOR1+2144
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
	ldr	r3, .L2670
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
	ldr	r3, .L2670+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2665
	ldr	r2, [r5, #24]
	mov	r1, r6
	ldr	r0, .L2670+8
	add	r3, r2, #1
	bl	printk
.L2665:
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
.L2671:
	.align	2
.L2670:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC175
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
	ldr	r6, .L2687
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
	bhi	.L2673
	ldr	r1, .L2687+4
	movw	r2, #945
	ldr	r0, .L2687+8
	bl	printk
	bl	dump_stack
.L2673:
	ldrb	r1, [r6, #1101]	@ zero_extendqisi2
	ldr	r2, .L2687
	cmp	r1, r9
	bls	.L2672
	add	r1, r2, r9
	mvn	r4, #0
	str	r2, [sp, #12]
	bic	r4, r8, r4, asl r7
	ldrb	r5, [r1, #1180]	@ zero_extendqisi2
	ldr	r7, [r2, #1040]
	bl	nandc_rdy_status
	cmp	r0, #0
	ldr	r2, [sp, #12]
	bne	.L2675
	ldrb	r2, [r2, #1101]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2676
	bl	nandc_wait_flash_ready
	b	.L2675
.L2676:
	mov	r0, r9
	mov	r1, r4
	mov	r2, #64
	bl	flash_wait_device_ready_raw
.L2675:
	mov	r0, r5
	bl	hynix_reconfig_rr_para
	mov	r0, r5
	bl	nandc_cs
	tst	r8, #50331648
	bne	.L2677
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r4, r0
	beq	.L2678
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L2678
.L2677:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L2678:
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
	ldr	r3, .L2687+12
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
.L2672:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2688:
	.align	2
.L2687:
	.word	.LANCHOR0
	.word	.LANCHOR1+2176
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
	ldr	r2, .L2698
	ldr	r0, [r4, #24]
	ldrb	r3, [r2, #2775]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2690
	ldrb	ip, [r2, #1189]	@ zero_extendqisi2
	mvn	r1, #0
	mov	r5, #48
	rsb	r6, ip, #24
	mvn	r1, r1, asl ip
	uxth	r1, r1
	and	r0, r1, r0, asr r6
.L2692:
	mla	lr, r5, r3, r2
	ldr	ip, [lr, #1260]
	and	ip, r1, ip, lsr r6
	cmp	r0, ip
	bne	.L2691
	add	ip, lr, #1264
	ldrb	lr, [lr, #1278]	@ zero_extendqisi2
	add	ip, ip, #12
	cmp	lr, #7
	moveq	r3, #3
	streqb	r3, [ip, #2]
	beq	.L2690
.L2691:
	mla	r3, r5, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2692
.L2690:
	mov	r3, #3
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #1
	ldr	r0, .L2698+4
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	buf_add_tail
.L2699:
	.align	2
.L2698:
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
	ldr	r8, .L2730
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
	bhi	.L2701
	ldr	r1, .L2730+4
	movw	r2, #1055
	ldr	r0, .L2730+8
	bl	printk
	bl	dump_stack
.L2701:
	ldrb	r2, [r8, #1101]	@ zero_extendqisi2
	ldr	r3, .L2730
	cmp	r2, r5
	mvnls	r0, #0
	bls	.L2702
	add	r5, r3, r5
	mvn	r4, #0
	bic	r4, r10, r4, asl r9
	ubfx	r10, r10, #24, #2
	ldrb	r9, [r5, #1180]	@ zero_extendqisi2
	ldr	r5, [r3, #1040]
	mov	r0, r9
	bl	nandc_cs
	cmp	r10, #0
	bne	.L2703
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2703:
	ldrb	r3, [r8, #1119]	@ zero_extendqisi2
	uxtb	ip, r4
	mov	r0, r4, lsr #8
	ldrb	r1, [r8, #1188]	@ zero_extendqisi2
	cmp	r3, #1
	mov	r3, r9, asl #8
	addeq	r2, r5, r3
	moveq	lr, #6
	beq	.L2729
	ldr	r2, .L2730
	ldr	r2, [r2, #1096]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	add	r2, r5, r3
	bne	.L2707
	mov	lr, #5
.L2729:
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
	b	.L2706
.L2707:
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
.L2706:
	ldr	r2, [r8, #1096]
	ldr	r3, .L2730
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r10, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2710
	ldrb	r2, [r3, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2710
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r10, #1
	addeq	r4, r4, r4, asl #1
	addeq	r0, r0, r4
	beq	.L2726
.L2710:
	mov	r0, r4
.L2726:
	bl	nandc_set_seed
	ldr	r3, .L2730+12
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
.L2702:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2731:
	.align	2
.L2730:
	.word	.LANCHOR0
	.word	.LANCHOR1+2200
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
	ldr	r5, .L2767
	.pad #20
	sub	sp, sp, #20
	mov	r8, r0
	mov	r9, r1
	mov	r10, r2
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
	bhi	.L2733
	ldr	r1, .L2767+4
	movw	r2, #1212
	ldr	r0, .L2767+8
	bl	printk
	bl	dump_stack
.L2733:
	add	r7, r5, r7
	ldr	fp, [r5, #1040]
	ldrb	r7, [r7, #1180]	@ zero_extendqisi2
	mov	r0, r7
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2734
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2734:
	mov	r2, r7, asl #8
	mov	r1, #5
	add	r3, fp, r2
	add	fp, fp, r2
	ldr	r2, [r5, #1096]
	str	r1, [r3, #2056]
	mov	r1, #0
	str	r1, [r3, #2052]
	str	r1, [r3, #2052]
	ldr	r1, .L2767+12
	ldrb	r0, [r1, #16]	@ zero_extendqisi2
	str	r1, [sp, #8]
	cmp	r0, #3
	uxtbeq	r1, r4
	streq	r1, [r3, #2052]
	moveq	r1, r4, lsr #8
	streq	r1, [r3, #2052]
	moveq	r1, r4, lsr #16
	streq	r1, [r3, #2052]
	mov	r3, #224
	str	r3, [fp, #2056]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r6, #0
	ldr	r3, .L2767
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2736
	ldrb	r2, [r3, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2736
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r6, #1
	addeq	r3, r4, r4, asl #1
	addeq	r0, r0, r3
	beq	.L2766
.L2736:
	mov	r0, r4
.L2766:
	bl	nandc_set_seed
	mov	r0, r7
	mov	r1, #0
	ldr	r3, [sp, #8]
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r9
	str	r10, [sp]
	bl	nandc_xfer
	cmn	r0, #1
	bne	.L2738
	ldrb	fp, [r5, #1168]	@ zero_extendqisi2
	ldr	ip, .L2767
	cmp	fp, #0
	beq	.L2739
	mov	r3, #0
	strb	r3, [ip, #1168]
	ldr	r3, .L2767+12
	mov	r0, r7
	orr	r1, r4, r6, asl #24
	mov	r2, r9
	str	ip, [sp, #12]
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r10
	bl	flash_read_page
	cmp	r6, #0
	ldr	ip, [sp, #12]
	strb	fp, [ip, #1168]
	bne	.L2740
.L2747:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2740
	ldr	r3, .L2767
	ldrb	r3, [r3, #1172]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	blt	.L2740
	ldr	r3, [sp, #8]
	ldrb	r3, [r3, #23]	@ zero_extendqisi2
	sub	r3, r3, #4
	cmp	r3, #4
	movls	r0, #256
	b	.L2754
.L2740:
	cmn	r0, #1
	bne	.L2754
.L2748:
	ldr	r3, .L2767+16
	ldr	ip, [r3, #-180]
	cmp	ip, #0
	bne	.L2743
.L2746:
	ldrb	r3, [r5, #1168]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2767+20
	mov	r2, r8
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	mvneq	r0, #0
	beq	.L2754
	ldr	r3, [sp, #8]
	mov	r0, r7
	orr	r1, r4, r6, asl #24
	mov	r2, r9
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r10
	bl	flash_ddr_tuning_read
	b	.L2754
.L2743:
	ldr	r3, [sp, #8]
	mov	r0, r7
	orr	r1, r4, r6, asl #24
	mov	r2, r9
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r10
	blx	ip
	cmn	r0, #1
	bne	.L2754
	b	.L2746
.L2738:
	cmp	r6, #0
	beq	.L2747
	b	.L2754
.L2739:
	cmp	r6, #0
	beq	.L2747
	b	.L2748
.L2754:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2768:
	.align	2
.L2767:
	.word	.LANCHOR0
	.word	.LANCHOR1+2232
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC176
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
	ldr	r5, .L2866
	ldrb	r4, [r5, #2775]	@ zero_extendqisi2
	cmp	r4, #255
	beq	.L2803
	mov	r3, #48
	mla	r3, r3, r4, r5
	ldrb	r2, [r3, #1278]	@ zero_extendqisi2
	ldr	r6, [r3, #1260]
	sub	r3, r2, #1
	cmp	r3, #10
	ldrls	pc, [pc, r3, asl #2]
	b	.L2803
.L2773:
	.word	.L2772
	.word	.L2774
	.word	.L2775
	.word	.L2775
	.word	.L2775
	.word	.L2775
	.word	.L2776
	.word	.L2777
	.word	.L2778
	.word	.L2775
	.word	.L2778
.L2772:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	beq	.L2803
	mov	r3, #48
	mla	r3, r3, r4, r5
	ldr	r2, [r3, #1244]
	ldr	r1, [r3, #1240]
	cmp	r2, #0
	beq	.L2780
	ldrb	r0, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2866
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r0, r3
	moveq	r1, r2
.L2780:
	mov	r3, #48
	mov	r0, r6
	mla	r5, r3, r4, r5
	ldr	r2, [r5, #1248]
	bl	flash_complete_page_read
	str	r0, [r5, #1272]
	b	.L2864
.L2774:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	mov	r2, #48
	mla	r3, r2, r4, r5
	ldr	ip, .L2866
	add	r1, r3, #1232
	add	r1, r1, #4
	ldrb	r6, [r3, #1236]	@ zero_extendqisi2
	tst	r0, #64
	beq	.L2803
	mla	r2, r2, r6, ip
	ldr	r10, [r1, #4]
	ldr	r8, [r2, #1240]
	ldr	r2, [r3, #1244]
	cmp	r2, #0
	beq	.L2781
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldrb	r3, [ip, #2772]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r10, r2
.L2781:
	mov	r3, #48
	mla	r3, r3, r6, r5
	ldr	r2, [r3, #1244]
	cmp	r2, #0
	beq	.L2782
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2866
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r8, r2
.L2782:
	mov	r9, #48
	mov	r1, r10
	mla	r3, r9, r4, r5
	mla	r9, r9, r6, r5
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
	beq	.L2783
	ldr	r3, [sp, #12]
	ldr	r3, [r3, #1256]
	cmn	r3, #1
	beq	.L2784
	ldr	r2, [fp, #4]
	ldr	r2, [r2, #4]
	cmp	r3, r2
	beq	.L2784
.L2783:
	mov	fp, #48
	ldrb	r0, [r5, #1189]	@ zero_extendqisi2
	mla	fp, fp, r4, r5
	ldrb	lr, [r5, #2772]	@ zero_extendqisi2
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
	beq	.L2785
	ldr	ip, [sp, #12]
	ldr	r3, [ip, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2785
	ldr	r0, .L2866+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2785
	str	r1, [sp]
	ldr	r0, .L2866+8
	ldr	r1, [fp, #1260]
	ldr	r3, [r3]
	bl	printk
.L2785:
	mov	r3, #48
	mla	r3, r3, r4, r5
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2784
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2784
	ldr	r1, .L2866+12
	movw	r2, #430
	ldr	r0, .L2866+16
	bl	printk
	bl	dump_stack
.L2784:
	mov	r3, #48
	mov	r2, #13
	mla	r4, r3, r4, r5
	cmn	r9, #1
	strb	r2, [r4, #1278]
	ldrb	r2, [r4, #1238]	@ zero_extendqisi2
	str	r7, [r4, #1272]
	orr	r2, r2, #8
	strb	r2, [r4, #1238]
	beq	.L2786
	ldr	r2, .L2866
	mla	r3, r3, r6, r2
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2788
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2788
.L2786:
	mov	r4, #48
	ldrb	r0, [r5, #1189]	@ zero_extendqisi2
	mla	r4, r4, r6, r5
	rsb	r1, r0, #24
	ldrb	ip, [r5, #2772]	@ zero_extendqisi2
	mvn	r3, #0
	mvn	r3, r3, asl r0
	add	r9, r4, #1232
	add	r9, r9, #12
	ldr	r2, [r4, #1260]
	str	ip, [sp]
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	mov	r2, r8
	ldr	r3, [r4, #1248]
	uxtb	r0, r0
	bl	flash_read_page_en
	ldr	r2, [r4, #1256]
	cmn	r2, #1
	beq	.L2790
	ldr	r3, [r9, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2790
	ldr	r0, .L2866+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2790
	str	r1, [sp]
	ldr	r0, .L2866+8
	ldr	r1, [r4, #1260]
	ldr	r3, [r3]
	bl	printk
.L2790:
	mov	r3, #48
	mla	r3, r3, r6, r5
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2788
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2788
	ldr	r1, .L2866+12
	movw	r2, #449
	ldr	r0, .L2866+16
	bl	printk
	bl	dump_stack
.L2788:
	mov	r3, #48
	mla	r5, r3, r6, r5
	str	r7, [r5, #1272]
.L2864:
	mov	r3, #13
	strb	r3, [r5, #1278]
	ldrb	r3, [r5, #1238]	@ zero_extendqisi2
	orr	r3, r3, #8
	strb	r3, [r5, #1238]
	b	.L2803
.L2775:
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	beq	.L2803
	ands	r6, r0, #5
	mov	r3, #48
	beq	.L2791
	mla	r5, r3, r4, r5
	ldr	r0, .L2866+20
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
	b	.L2771
.L2791:
	mul	r3, r3, r4
	mov	r2, #13
	ldr	ip, .L2866
	add	r7, r5, r3
	strb	r2, [r7, #1278]
	ldr	r2, [r5, #2804]
	str	r6, [r7, #1272]
	ldr	r1, [r2, #156]
	ldr	r2, .L2866+24
	cmp	r1, r2
	bne	.L2803
	ldr	r2, .L2866+28
	add	r3, r3, r2
	ldrh	r3, [r3, #2]
	cmp	r3, #0
	bne	.L2803
	ldrb	r0, [ip, #1189]	@ zero_extendqisi2
	mvn	r3, #0
	ldr	r2, [r7, #1260]
	rsb	r1, r0, #24
	ldr	r8, .L2866+32
	ldrb	ip, [ip, #2772]	@ zero_extendqisi2
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	str	ip, [sp]
	uxtb	r0, r0
	ldr	r3, [r8, #-172]
	ldr	r2, [r8, #-176]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2792
	ldr	r1, [r7, #1248]
	ldr	r2, [r8, #-172]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2803
.L2792:
	mov	r2, #48
	ldr	r0, .L2866+36
	mla	r4, r2, r4, r5
	ldrb	r2, [r5, #1168]	@ zero_extendqisi2
	ldrb	r1, [r4, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r4, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #1272]
	b	.L2771
.L2778:
	cmp	r2, #11
	ldr	r2, .L2866+40
	ldrb	r3, [r5, #1189]	@ zero_extendqisi2
	mov	r1, #48
	mvn	r7, #0
	movne	r9, #3
	mla	ip, r1, r4, r2
	rsb	lr, r3, #24
	mvn	r3, r7, asl r3
	and	r0, r3, r6, lsr lr
	moveq	r9, #10
	mov	r10, r3
	uxth	r0, r0
.L2794:
	ldrb	r8, [ip]	@ zero_extendqisi2
	cmp	r8, #255
	beq	.L2865
	mul	r3, r1, r8
	ldr	r7, .L2866
	add	ip, r2, r3
	add	r3, r5, r3
	ldrb	fp, [r3, #1278]	@ zero_extendqisi2
	cmp	fp, r9
	bne	.L2794
	ldr	r3, [r3, #1260]
	and	r3, r10, r3, lsr lr
	cmp	r0, r3
	bne	.L2794
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r6, r0
	beq	.L2863
	ands	r2, r0, #15
	mov	r3, #48
	mul	r3, r3, r4
	beq	.L2798
	add	r7, r7, r3
	mov	r9, #12
	ldr	r0, .L2866+44
	mov	r2, r6
	ldrb	r1, [r7, #1237]	@ zero_extendqisi2
	str	r9, [sp]
	ldr	r3, [r7, #1260]
	bl	printk
	mvn	r3, #0
	strb	r9, [r7, #1278]
	str	r3, [r7, #1272]
	b	.L2797
.L2798:
	add	r6, r7, r3
	mov	r1, #13
	str	r2, [r6, #1272]
	ldr	r2, [r7, #2804]
	strb	r1, [r6, #1278]
	ldr	r1, [r2, #156]
	ldr	r2, .L2866+24
	cmp	r1, r2
	bne	.L2863
	ldr	r2, .L2866+40
	add	r3, r2, r3
	ldrh	r3, [r3, #34]
	cmp	r3, #0
	bne	.L2863
	ldr	r2, [r6, #1260]
	mvn	r3, #0
	ldrb	r0, [r7, #1189]	@ zero_extendqisi2
	ldr	r9, .L2866+32
	rsb	r1, r0, #24
	ldrb	ip, [r7, #2772]	@ zero_extendqisi2
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	str	ip, [sp]
	uxtb	r0, r0
	ldr	r3, [r9, #-172]
	ldr	r2, [r9, #-176]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2800
	ldr	r1, [r6, #1248]
	ldr	r2, [r9, #-172]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2863
.L2800:
	mov	r6, #48
	ldrb	r2, [r5, #1168]	@ zero_extendqisi2
	mla	r6, r6, r4, r5
	ldr	r0, .L2866+48
	ldrb	r1, [r6, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r6, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r6, #1272]
.L2863:
	mov	r6, #0
.L2797:
	mov	r3, #48
	mla	r4, r3, r4, r5
	mla	r5, r3, r8, r5
	ldrb	r2, [r4, #1278]	@ zero_extendqisi2
	ldr	r3, [r4, #1272]
	strb	r2, [r5, #1278]
	str	r3, [r5, #1272]
	b	.L2771
.L2865:
	mvn	r6, #0
	b	.L2771
.L2776:
	mov	r0, r6
	mov	r1, #32
	bl	flash_wait_device_ready
	tst	r0, #32
	beq	.L2803
	ands	r6, r0, #15
	mov	r3, #48
	mlaeq	r5, r3, r4, r5
	movne	r2, #12
	mlane	r3, r3, r4, r5
	moveq	r3, #13
	movne	r6, r0
	streqb	r3, [r5, #1278]
	streq	r6, [r5, #1272]
	strneb	r2, [r3, #1278]
	mvnne	r2, #0
	strne	r2, [r3, #1272]
	b	.L2771
.L2777:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	movne	r3, #48
	mlane	r5, r3, r4, r5
	movne	r3, #7
	strne	r0, [r5, #1272]
	strneb	r3, [r5, #1278]
.L2803:
	mov	r6, #0
.L2771:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2867:
	.align	2
.L2866:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC177
	.word	.LANCHOR1+2260
	.word	.LC0
	.word	.LC178
	.word	1145785929
	.word	.LANCHOR0+1268
	.word	.LANCHOR3
	.word	.LC179
	.word	.LANCHOR0+1236
	.word	.LC180
	.word	.LC181
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
	beq	.L2869
	ldr	r3, .L2900
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L2869
	ldr	r0, .L2900+4
	mov	r2, r5
	ldr	r1, [r4, #24]
	bl	printk
.L2869:
	ldr	r8, .L2900+8
	mov	r6, #0
	mov	r9, r8
.L2870:
	cmp	r5, #0
	beq	.L2897
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r7, [r4, #24]
	str	r3, [sp, #4]
.L2871:
	mov	r0, r7
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2899
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2871
.L2899:
	cmp	r5, #1
	beq	.L2873
	ldrb	r3, [r8, #1233]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2873
	ldrb	r3, [r8, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2874
.L2873:
	mov	r0, r4
	bl	queue_prog_cmd
	b	.L2875
.L2874:
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	mvn	fp, #0
	rsb	ip, r3, #24
	mov	ip, r7, lsr ip
	bic	fp, ip, fp, asl r3
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	uxth	fp, fp
	bne	.L2876
	ldr	r1, .L2900+12
	mov	r2, #696
	ldr	r0, .L2900+16
	bl	printk
	bl	dump_stack
.L2876:
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
	bne	.L2877
	ldr	r2, .L2900+20
	ldrh	fp, [r2]
	ldr	r2, .L2900+24
	rsb	r3, fp, r3
	mov	r0, r10, asl fp
	mov	r3, r10, asl r3
	sub	r0, r0, #1
	ldrb	lr, [r2, #-3202]	@ zero_extendqisi2
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
	beq	.L2878
	cmp	r6, lr
	beq	.L2877
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
	ldr	r0, .L2900+28
	strb	r3, [r4]
	bl	buf_add_tail
	b	.L2875
.L2878:
	mov	r0, r4
	mov	r6, fp
	bl	queue_prog_cmd
	b	.L2875
.L2877:
	mov	r0, r4
	mov	r6, #0
	bl	queue_prog_cmd
.L2875:
	subs	r5, r5, #1
	ldrne	r3, .L2900+32
	movne	r4, #48
	ldrne	r2, [sp, #4]
	mlane	r4, r4, r2, r3
	bne	.L2870
.L2897:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2901:
	.align	2
.L2900:
	.word	.LANCHOR2
	.word	.LC182
	.word	.LANCHOR0
	.word	.LANCHOR1+2292
	.word	.LC0
	.word	.LANCHOR3-3204
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
	ldr	r4, .L2907
.L2903:
	ldrb	r3, [r4, #2775]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2906
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2903
.L2906:
	ldmfd	sp!, {r4, pc}
.L2908:
	.align	2
.L2907:
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
	ldr	r3, .L2915
	ldr	r5, .L2915+4
	ldrb	r1, [r3, #2801]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r1, #0
	beq	.L2910
	ldrb	r2, [r5, #-168]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L2915+8
	mla	r0, r0, r2, r3
	bl	sblk_prog_page
.L2910:
	mvn	r3, #0
	strb	r3, [r5, #-168]
	mov	r3, #0
	strb	r3, [r4, #2801]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_vpn_decrement
.L2916:
	.align	2
.L2915:
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
	ldr	r3, .L2922
	ldrb	r3, [r3, #2801]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, pc}
	bl	timer_get_time
	ldr	r3, .L2922+4
	ldr	r3, [r3, #-164]
	add	r3, r3, #100
	cmp	r0, r3
	ldmlsfd	sp!, {r3, pc}
	ldmfd	sp!, {r3, lr}
	b	ftl_flush
.L2923:
	.align	2
.L2922:
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
	ldr	r3, .L2928
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
.L2929:
	.align	2
.L2928:
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
	ldr	r10, .L2958
	mov	r8, r0
	mov	r6, r1
	mov	r4, r0
	mov	r5, r1
.L2931:
	cmp	r5, #0
	beq	.L2954
	ldrb	fp, [r4]	@ zero_extendqisi2
	ldr	r9, [r4, #24]
.L2932:
	mov	r0, r9
	mov	r1, #0
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2956
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2932
.L2956:
	cmp	r5, #1
	beq	.L2938
	ldr	r3, .L2958+4
	ldrb	r3, [r3, #-160]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2938
	ldr	r3, .L2958
	mvn	lr, #0
	ldrb	r2, [r3, #1189]	@ zero_extendqisi2
	rsb	r3, r2, #24
	mov	r3, r9, lsr r3
	bic	lr, r3, lr, asl r2
	uxth	r3, lr
	str	r3, [sp, #4]
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2937
	ldr	r1, .L2958+8
	movw	r2, #781
	ldr	r0, .L2958+12
	bl	printk
	bl	dump_stack
.L2937:
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
	bne	.L2938
	ldr	r2, .L2958+16
	ldrh	r2, [r2]
	add	r9, r2, r9
	cmp	r1, r9
	bne	.L2938
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
	ldr	r0, .L2958+20
	strb	r9, [r4]
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	bl	buf_add_tail
	ldr	r1, .L2958+24
	strb	r9, [r7, #1236]
	ldr	r0, .L2958+20
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	strb	ip, [r7, #1278]
	strb	r2, [r7, #1279]
	add	r1, r1, r3
	bl	buf_add_tail
	b	.L2936
.L2938:
	mov	r0, r4
	bl	queue_read_cmd
.L2936:
	subs	r5, r5, #1
	ldrne	r3, .L2958+24
	movne	r4, #48
	mlane	r4, r4, fp, r3
	bne	.L2931
.L2954:
	ldr	r4, .L2958+24
	mov	r5, #48
.L2942:
	cmp	r6, #0
	beq	.L2957
	ldrb	r3, [r8, #42]	@ zero_extendqisi2
	cmp	r3, #13
	bne	.L2943
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r6, r6, #1
	cmp	r3, #255
	mlane	r8, r5, r3, r4
.L2943:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2942
.L2957:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2959:
	.align	2
.L2958:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2308
	.word	.LC0
	.word	.LANCHOR3-3142
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
	ldr	r6, .L2996
	ldr	r3, [r6, #2836]
	ldr	r9, [r6, #1092]
	cmp	r3, #0
	bne	.L2961
	mov	r0, #1
	bl	buf_alloc
	str	r0, [r6, #2836]
.L2961:
	ldr	r4, [r6, #2836]
	cmp	r4, #0
	bne	.L2962
	ldr	r1, .L2996+4
	movw	r2, #729
	ldr	r0, .L2996+8
	bl	printk
	bl	dump_stack
.L2962:
	ldr	fp, .L2996+12
	add	r10, r9, #96
	mov	r8, #0
	mov	ip, fp
.L2963:
	ldrb	r3, [r9, #89]	@ zero_extendqisi2
	ldr	r5, .L2996+16
	cmp	r8, r3
	bge	.L2994
	mov	r7, #1
.L2974:
	ldrh	r3, [r5, #16]
	cmp	r7, r3
	bgt	.L2995
	ldr	r2, .L2996+20
	ldrb	r1, [fp, #-3196]	@ zero_extendqisi2
	ldrh	r3, [r10]
	ldrh	r2, [r2]
	cmp	r1, #2
	mul	r2, r2, r3
	beq	.L2964
	ldr	r3, .L2996
	ldrb	r3, [r3, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2965
.L2964:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #1
	add	r3, r3, r2
	add	r3, r3, r7
	orr	r3, r3, r1, asl #24
	b	.L2993
.L2965:
	ldr	r3, .L2996+16
	cmp	r1, #3
	ldrh	r3, [r3, #12]
	addne	r2, r2, r3
	strne	r2, [r4, #24]
	bne	.L2966
	ldr	r1, .L2996
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	addne	r3, r3, r3, asl #1
	addeq	r3, r2, r3
	subne	r3, r3, #1
	orreq	r3, r3, r7, asl #24
	addne	r3, r3, r2
	addne	r3, r3, r7
	orrne	r3, r3, #50331648
.L2993:
	str	r3, [r4, #24]
.L2966:
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #28]
	bl	sblk_read_page
	ldr	r2, [r4, #36]
	cmn	r2, #1
	ldr	ip, [sp, #28]
	beq	.L2969
	ldr	r0, [ip, #-244]
	ldrh	r1, [r5, #18]
	ldr	r3, [r4, #12]
	ldr	lr, [r0, r1, asl #2]
	ldr	r0, [r3, #4]
	cmp	lr, r0
	bne	.L2969
	ldr	r0, [fp, #-240]
	ldr	r3, [r3, #8]
	ldr	r1, [r0, r1, asl #2]
	cmp	r1, r3
	beq	.L2970
.L2969:
	ldrh	r3, [r5, #18]
	ldr	r1, [ip, #-244]
	mov	r0, r3, asl #2
	ldr	r3, [r1, r3, asl #2]
	cmn	r3, #1
	beq	.L2970
	ldr	r1, .L2996+24
	ldr	r1, [r1]
	tst	r1, #1024
	beq	.L2971
	ldr	ip, .L2996+12
	ldr	r1, [r4, #12]
	ldr	ip, [ip, #-240]
	ldr	r0, [ip, r0]
	str	r0, [sp]
	ldr	r0, [r1]
	str	r0, [sp, #4]
	ldr	r0, [r1, #4]
	str	r0, [sp, #8]
	ldr	r0, [r1, #8]
	str	r0, [sp, #12]
	ldr	r1, [r1, #12]
	ldr	r0, .L2996+28
	str	r1, [sp, #16]
	ldr	r1, [r4, #24]
	bl	printk
.L2971:
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
	ldr	r3, .L2996+32
	cmp	r2, r3
	bne	.L2977
	ldr	r3, .L2996+12
	ldrb	r3, [r3, #-2612]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2977
	ldr	r3, .L2996+12
	ldrb	r3, [r3, #-3194]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2977
	ldr	r0, [r4, #24]
	bl	ftl_mask_bad_block
	b	.L2977
.L2970:
	ldrh	r3, [r5, #18]
	add	r7, r7, #1
	add	r3, r3, #1
	strh	r3, [r5, #18]	@ movhi
	b	.L2974
.L2995:
	add	r8, r8, #1
	add	r10, r10, #2
	b	.L2963
.L2994:
	ldrh	r3, [r5, #12]
	add	r2, r3, #1
	strh	r2, [r5, #12]	@ movhi
	ldr	r2, .L2996+12
	ldrb	r0, [r2, #-3194]	@ zero_extendqisi2
	cmp	r0, #0
	addne	r3, r3, #2
	strneh	r3, [r5, #12]	@ movhi
	movne	r0, #0
	b	.L2972
.L2977:
	mvn	r0, #0
.L2972:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2997:
	.align	2
.L2996:
	.word	.LANCHOR0
	.word	.LANCHOR1+2324
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR0+2828
	.word	.LANCHOR3-3142
	.word	.LANCHOR2
	.word	.LC183
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
.L2999:
	mov	r0, r6
	mov	r1, #1
	bl	queue_lun_state
	subs	r5, r0, #0
	beq	.L3002
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2999
.L3002:
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
.L3004:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r9, r0, #0
	beq	.L3016
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L3004
.L3016:
	cmp	r10, #2
	bne	.L3006
	ldr	r2, .L3018
	ldr	lr, [r7]
	ldrb	r3, [r2, #1233]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r4, [lr, #24]
	beq	.L3011
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
	beq	.L3008
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
	b	.L3009
.L3008:
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
.L3009:
	ldr	r1, [r5]
	mov	r3, #5
	ldr	r0, .L3018+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L3010
.L3011:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r6, r0, #0
	beq	.L3017
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L3011
.L3017:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
	mov	r0, r7
	mov	r1, r6
	bl	queue_tlc_prog_cmd
.L3013:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L3010
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L3013
.L3006:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
.L3010:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3019:
	.align	2
.L3018:
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
	ldr	r9, .L3028
	mov	r7, r1
	mov	r8, #1
.L3021:
	cmp	r7, #0
	beq	.L3026
	ldr	r3, [r5, #-8]
	ldr	r4, [r3, #24]
.L3022:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L3027
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L3022
.L3027:
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
	ldr	r0, .L3028+4
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r8, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L3021
.L3026:
	mov	r0, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3029:
	.align	2
.L3028:
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
	ldr	r6, .L3060
	ldrb	r3, [sp, #68]	@ zero_extendqisi2
	mov	r7, r0
	mov	r5, r1
	mov	r9, r2
	ubfx	r4, r1, #24, #2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #1101]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L3031
	ldr	r1, .L3060+4
	movw	r2, #635
	ldr	r0, .L3060+8
	bl	printk
	bl	dump_stack
.L3031:
	ldrb	r2, [r6, #1101]	@ zero_extendqisi2
	ldr	r3, .L3060
	cmp	r2, r7
	mvnls	r0, #0
	bls	.L3032
	add	r2, r3, r7
	cmp	r4, #0
	ldrb	r10, [r2, #1180]	@ zero_extendqisi2
	bne	.L3044
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3034
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3044
.L3034:
	ldrh	fp, [r6, #2]
	mov	r0, r5
	mov	r1, fp
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L3060
	mul	r0, fp, r0
	rsb	r4, r0, r5
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
	b	.L3033
.L3044:
	mov	r4, r5
.L3033:
	ldr	fp, .L3060+12
	ldr	r6, .L3060+16
.L3039:
	ldr	r3, [fp]
	tst	r3, #16
	beq	.L3036
	ldr	r0, .L3060+20
	mov	r1, r10
	mov	r2, r5
	mov	r3, r4
	bl	printk
.L3036:
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
	beq	.L3037
	ldr	r3, [sp, #64]
	mov	r1, r5
	str	r0, [sp, #20]
	mov	r0, r7
	str	r3, [sp]
	ldr	r3, [r6, #-172]
	ldr	r2, [r6, #-176]
	bl	flash_read_page_en
	sub	r3, r0, #512
	cmp	r0, #512
	cmnne	r0, #1
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp, #12]
	beq	.L3038
	ldr	r3, [r6, #-176]
	ldr	r2, [r9]
	ldr	ip, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L3038
	ldr	r3, [r6, #-172]
	ldr	r2, [r8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3037
.L3038:
	mov	r2, #4
	ldr	r0, .L3060+24
	mov	r3, r2
	mov	r1, r9
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3060+28
	mov	r1, r8
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3060+32
	ldr	r1, [r6, #-172]
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3060+36
	ldr	r1, [r6, #-176]
	bl	rknand_print_hex
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L3039
.L3041:
	mov	r1, r5
	ldr	r0, .L3060+40
	bl	printk
	ldr	r1, .L3060+4
	movw	r2, #678
	ldr	r0, .L3060+8
	bl	printk
	bl	dump_stack
	mvn	ip, #0
	b	.L3040
.L3037:
	cmn	ip, #1
	beq	.L3041
.L3040:
	mov	r0, ip
.L3032:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3061:
	.align	2
.L3060:
	.word	.LANCHOR0
	.word	.LANCHOR1+2348
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC184
	.word	.LC185
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
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
	bne	.L3063
	ldr	r1, .L3065
	movw	r2, #2670
	ldr	r0, .L3065+4
	bl	printk
	bl	dump_stack
	ldr	r0, .L3065+8
	mov	r1, r5
	bl	printk
.L3063:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L3066:
	.align	2
.L3065:
	.word	.LANCHOR1+2368
	.word	.LC0
	.word	.LC189
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
	ldr	r6, .L3105
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	ldr	r0, .L3105+4
	ldr	r4, .L3105+8
	mov	r8, #0
	ldrb	r2, [r6, #2772]	@ zero_extendqisi2
	ldr	r9, .L3105+12
	mov	r7, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, [r6, #2804]
	ldrh	r2, [r3, #74]
	cmp	r2, #0
	movne	r2, #0
	strneh	r2, [r3, #150]	@ movhi
.L3079:
	ldrh	r3, [r4, #138]
	ldr	r2, [r6, #2804]
	ldrb	r1, [r6, #2772]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, [r2, #4]
	ldr	r0, [r4, #140]
	mov	r1, r1, asl #9
	add	r3, r3, #1
	str	r3, [r2, #4]
	ldrb	r5, [r4, #136]	@ zero_extendqisi2
	str	r3, [r4, #-116]
	ldrh	fp, [r9]
	str	r10, [r4, #-120]
	bl	js_hash
	ldr	r3, .L3105+16
	ldr	r3, [r3]
	tst	r3, #4096
	str	r0, [r4, #-112]
	beq	.L3069
	ldr	r3, .L3105
	ldr	r0, .L3105+20
	ldrb	r1, [r4, #136]	@ zero_extendqisi2
	ldr	r3, [r3, #2804]
	ldrh	r2, [r4, #138]
	ldr	r3, [r3, #4]
	bl	printk
.L3069:
	ldr	r3, .L3105+24
	ldrh	r0, [r7, #138]
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L3070
.L3075:
	ldrb	r3, [r7, #137]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r7, #137]
	cmp	r3, #7
	bls	.L3071
	mov	r3, #8
.L3074:
	ldr	r2, [r6, #1176]
	sub	r5, r3, #8
	add	r2, r2, r3
	uxth	r5, r5
	ldrb	r1, [r2, #32]	@ zero_extendqisi2
	add	r2, r1, #127
	uxtb	r2, r2
	cmp	r2, #125
	bhi	.L3072
	ldr	r1, .L3105+28
	movw	r2, #841
	ldr	r0, .L3105+32
	bl	printk
	bl	dump_stack
	b	.L3073
.L3072:
	cmp	r1, #255
	bne	.L3073
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L3074
	mov	r5, #8
.L3073:
	mov	r8, #1
	strb	r5, [r7, #137]
.L3071:
	ldr	r3, [r6, #1176]
	ldrb	r2, [r7, #137]	@ zero_extendqisi2
	add	r3, r3, r2
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	strb	r3, [r7, #136]
	beq	.L3075
	ldrh	r5, [r9]
	mov	r0, #0
	mul	r5, r5, r3
	mov	r1, r5
	bl	flash_erase_block
	ldr	r3, .L3105
	mov	r1, r5
	mov	r0, #0
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, .L3105+4
	ldr	r2, [r4, #140]
	bl	ftl_prog_page
	mov	r3, #1
	strh	r3, [r4, #138]	@ movhi
	add	r5, r5, r3
	b	.L3076
.L3070:
	ldr	r3, [sp, #12]
	cmp	r0, #0
	mla	r5, fp, r5, r3
	bne	.L3076
	mov	r1, r5
	bl	flash_erase_block
.L3076:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	mov	r0, #0
	ldr	fp, .L3105+8
	mov	r1, r5
	str	r3, [sp]
	sub	r3, fp, #120
	ldr	r2, [r7, #140]
	bl	ftl_prog_page
	ldrh	r3, [r7, #138]
	add	r3, r3, #1
	strh	r3, [r7, #138]	@ movhi
	cmn	r0, #1
	beq	.L3077
	ldrb	r3, [r4, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3078
.L3077:
	mov	r3, #0
	strb	r3, [r7, #144]
	b	.L3079
.L3078:
	cmp	r8, #1
	beq	.L3080
.L3088:
	ldr	r3, .L3105+8
	ldrb	r3, [r3, #136]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L3082
	ldr	r1, .L3105+28
	movw	r2, #885
	ldr	r0, .L3105+32
	bl	printk
	bl	dump_stack
	b	.L3082
.L3080:
	ldrb	r4, [fp, #137]	@ zero_extendqisi2
	add	r4, r4, #1
.L3083:
	cmp	r4, #7
	bhi	.L3088
	ldr	r3, [r6, #1176]
	add	r3, r3, r4
	ldrb	r5, [r3, #40]	@ zero_extendqisi2
	add	r3, r5, #127
	uxtb	r3, r3
	cmp	r3, #125
	bhi	.L3084
	ldr	r1, .L3105+28
	mov	r2, #876
	ldr	r0, .L3105+32
	bl	printk
	bl	dump_stack
	b	.L3085
.L3084:
	cmp	r5, #255
	beq	.L3086
.L3085:
	ldr	r3, .L3105+12
	mov	r0, #0
	ldrh	r1, [r3]
	mul	r1, r1, r5
	bl	flash_erase_block
.L3086:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L3083
.L3082:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3106:
	.align	2
.L3105:
	.word	.LANCHOR0
	.word	.LANCHOR3-120
	.word	.LANCHOR3
	.word	.LANCHOR3-3142
	.word	.LANCHOR2
	.word	.LC190
	.word	.LANCHOR3-3172
	.word	.LANCHOR1+2384
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
	ldr	r4, .L3140
	mov	r6, #0
	ldr	r5, .L3140+4
	mov	r1, r6
	mov	r2, #16384
	.pad #28
	sub	sp, sp, #28
	strb	r3, [r4, #144]
	mov	r10, r4
	strb	r3, [r4, #146]
	movw	r3, #1076
	ldr	r0, [r4, #140]
	ldrh	r3, [r5, r3]
	strb	r6, [r4, #145]
	str	r0, [r5, #1080]
	add	r3, r0, r3, asl #2
	str	r3, [r5, #2804]
	bl	ftl_memset
	mov	r1, r6
	ldr	r0, [r4, #148]
	mov	r2, #16384
	bl	ftl_memset
	ldr	r3, [r5, #1176]
	ldr	fp, .L3140+8
	strb	r6, [r4, #137]
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	strh	r6, [r4, #138]	@ movhi
	mov	r6, #7
	strb	r3, [r4, #136]
.L3112:
	ldr	r3, [r5, #1176]
	uxth	r7, r6
	ldr	ip, .L3140+4
	add	r3, r3, r6
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3108
	ldr	r2, .L3140+12
	mov	r0, #0
	str	ip, [sp, #12]
	ldrh	r9, [r2]
	ldr	r2, [r4, #140]
	mul	r9, r9, r3
	ldrb	r3, [ip, #2772]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, .L3140+16
	mov	r1, r9
	bl	ftl_read_page
	cmn	r0, #1
	mov	r8, r0
	ldr	ip, [sp, #12]
	bne	.L3109
	ldrb	r3, [ip, #2772]	@ zero_extendqisi2
	mov	r0, #0
	add	r1, r9, #1
	str	r3, [sp]
	ldr	r3, .L3140+16
	ldr	r2, [r10, #140]
	bl	ftl_read_page
	mov	r8, r0
.L3109:
	ldr	r3, .L3140+20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3110
	ldr	r3, [r5, #2804]
	mov	r1, r6
	ldr	r0, .L3140+24
	mov	r2, r8
	ldr	r3, [r3]
	str	r3, [sp]
	mov	r3, #744
	bl	printk
.L3110:
	cmn	r8, #1
	beq	.L3108
	ldr	r3, [r5, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	beq	.L3122
.L3108:
	subs	r6, r6, #1
	bcs	.L3112
	movw	r6, #65535
	mov	r7, #0
	b	.L3111
.L3122:
	mov	r6, r7
.L3111:
	ldr	r3, .L3140+20
	sxth	r6, r6
	ldr	r2, [r3]
	str	r3, [sp, #16]
	tst	r2, #4096
	beq	.L3113
	ldr	r3, [r5, #2804]
	mov	r1, r6
	ldr	r0, .L3140+28
	mov	r2, #4800
	ldr	r3, [r3]
	bl	printk
.L3113:
	cmn	r6, #1
	bne	.L3114
	mov	r2, #16384
	ldr	r0, [r4, #140]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r5, #2804]
	ldr	r2, .L3140+8
	mov	r0, r6
	str	r2, [r3]
	ldr	r2, .L3140+32
	ldr	r3, [r5, #2804]
	str	r2, [r3, #12]
	b	.L3115
.L3114:
	ldr	r3, [r5, #1176]
	mov	r0, #0
	ldr	r2, [r4, #140]
	sxtah	r3, r3, r7
	strb	r7, [r4, #137]
	ldr	r6, .L3140+36
	mov	r7, #0
	ldrb	r1, [r3, #40]	@ zero_extendqisi2
	mov	r3, #4
	str	r3, [sp]
	ldr	r3, .L3140+16
	strb	r1, [r4, #136]
	bl	flash_get_last_written_page
	ldrh	r8, [r6, #-6]
	add	r6, r6, #3136
	ldr	fp, .L3140+8
	uxth	r9, r0
	mov	r10, r0
	add	r3, r9, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	ldrb	r3, [r4, #136]	@ zero_extendqisi2
	mul	r8, r8, r3
.L3116:
	rsb	r3, r7, r9
	tst	r3, #32768
	bne	.L3120
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	add	r1, r10, r8
	ldr	ip, .L3140+4
	mov	r0, #0
	rsb	r1, r7, r1
	ldr	r2, [r4, #140]
	str	r3, [sp]
	ldr	r3, .L3140+16
	str	ip, [sp, #20]
	bl	ftl_read_page
	cmn	r0, #1
	beq	.L3117
	ldr	ip, [sp, #20]
	ldr	r3, [ip, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L3117
	ldr	r3, [r6, #-112]
	cmp	r3, #0
	bne	.L3118
.L3120:
	ldrh	r3, [sp, #12]
	strh	r3, [r4, #138]	@ movhi
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
	beq	.L3115
	ldr	r3, .L3140+4
	ldr	r0, .L3140+40
	ldr	r3, [r3, #2804]
	ldr	r1, [r3, #156]
	bl	printk
	mov	r0, #0
	b	.L3115
.L3118:
	ldrb	r1, [ip, #2772]	@ zero_extendqisi2
	ldr	r0, [r6, #140]
	str	r3, [sp, #20]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	beq	.L3120
	ldr	r0, .L3140+44
	ldr	r1, [r6, #-112]
	bl	printk
.L3117:
	add	r7, r7, #1
	b	.L3116
.L3115:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3141:
	.align	2
.L3140:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	1229739078
	.word	.LANCHOR3-3142
	.word	.LANCHOR3-120
	.word	.LANCHOR2
	.word	.LC191
	.word	.LC192
	.word	393251
	.word	.LANCHOR3-3136
	.word	.LC194
	.word	.LC193
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
	ldr	r3, .L3158
	ldr	r4, .L3158+4
	umull	r0, r1, r0, r3
	ldr	r3, [r4, #1092]
	ldr	r0, [r3, #520]
	mov	r1, r1, lsr #5
	cmp	r1, r0
	ldrhi	r2, [r3, #12]
	rsbhi	r2, r0, r2
	addhi	r2, r2, r1
	strhi	r2, [r3, #12]
	bhi	.L3157
.L3143:
	bcs	.L3144
.L3157:
	str	r1, [r3, #520]
.L3144:
	mov	r0, #0
	ldr	r7, .L3158+8
	bl	ftl_total_vpn_update
	ldr	r6, .L3158+12
.L3148:
	ldr	r3, [r4, #2804]
	ldr	r2, [r3, #56]
	add	r2, r2, #1
	str	r2, [r3, #56]
	ldrh	r2, [r3, #140]
	ldrh	r3, [r7]
	cmp	r2, r3
	bcc	.L3146
	bl	ftl_ext_alloc_new_blk
.L3146:
	ldr	r3, [r4, #2804]
	ldrh	r2, [r3, #130]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3147
	ldr	r1, .L3158+16
	movw	r2, #2203
	ldr	r0, .L3158+20
	bl	printk
	bl	dump_stack
.L3147:
	ldr	r1, [r4, #2804]
	ldr	r0, .L3158+24
	ldrb	r2, [r4, #1189]	@ zero_extendqisi2
	ldrh	r3, [r1, #130]
	ldrh	r0, [r0]
	rsb	r2, r2, #24
	ldr	r8, .L3158+12
	rsb	r2, r0, r2
	mvn	r0, #0
	mov	r9, r3, asr r2
	bic	r3, r3, r0, asl r2
	ldr	r2, .L3158+28
	sub	r0, r8, #120
	sxth	r3, r3
	ldrh	r5, [r2]
	ldrh	r2, [r1, #140]
	mov	r1, #0
	mla	r5, r5, r3, r2
	ldrb	r2, [r4, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r3, #0
	str	r3, [r6, #-120]
	ldr	r3, [r4, #2804]
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, [r6, #148]
	ldr	r3, [r3, #56]
	mov	r1, r1, asl #9
	str	r3, [r6, #-116]
	bl	js_hash
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	mov	r1, r5
	ldr	r2, [r6, #148]
	str	r0, [r6, #-112]
	uxtb	r0, r9
	str	r3, [sp]
	sub	r3, r8, #120
	bl	ftl_prog_page
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #140]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #140]	@ movhi
	cmp	r3, #1
	beq	.L3148
	cmn	r0, #1
	beq	.L3149
	ldrb	r0, [r8, #146]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3150
.L3149:
.L3145:
	mov	r3, #0
	strb	r3, [r8, #146]
	b	.L3148
.L3150:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3159:
	.align	2
.L3158:
	.word	1374389535
	.word	.LANCHOR0
	.word	.LANCHOR3-3172
	.word	.LANCHOR3
	.word	.LANCHOR1+2400
	.word	.LC0
	.word	.LANCHOR3-3204
	.word	.LANCHOR3-3142
	.fnend
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L3180
	mov	r2, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	sub	r3, ip, #2608
	ldr	r4, .L3180+4
	mvn	r5, #0
	strh	r2, [r3, #-8]	@ movhi
	sub	r2, ip, #3200
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r4, #2804]
	ldrb	r9, [r4, #1189]	@ zero_extendqisi2
	ldrh	r6, [r2, #-4]
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
	ldr	r2, [ip, #148]
	sub	r3, ip, #120
	mov	r0, r10
	bl	flash_get_last_written_page
	ldr	r3, .L3180+8
	ldr	r2, [r3]
	tst	r2, #4096
	mov	r7, r0
	str	r3, [sp, #16]
	beq	.L3161
	uxth	fp, fp
	str	fp, [sp]
	ldr	r3, [r4, #2804]
	movw	r2, #2248
	ldr	r0, .L3180+12
	ldr	r1, .L3180+16
	ldrh	r3, [r3, #130]
	str	r3, [sp, #4]
	mov	r3, r7
	bl	printk
.L3161:
	ldr	r9, .L3180
	mov	r8, #0
	ldr	fp, .L3180+20
.L3162:
	uxth	r6, r7
	rsb	r3, r8, r6
	tst	r3, #32768
	bne	.L3167
	ldr	r3, .L3180+24
	rsb	r1, r8, r7
	ldrb	r2, [r4, #2772]	@ zero_extendqisi2
	mov	r0, r10
	ldr	ip, .L3180+4
	ldrh	r3, [r3]
	str	r2, [sp]
	str	ip, [sp, #20]
	mla	r1, r3, r5, r1
	ldr	r3, [sp, #12]
	ldr	r2, [r3, #148]
	ldr	r3, .L3180+28
	bl	flash_read_page_en
	cmp	r0, #512
	cmnne	r0, #1
	ldr	ip, [sp, #20]
	beq	.L3163
	ldr	r3, [ip, #1092]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L3163
	ldr	r3, [r9, #-112]
	cmp	r3, #0
	bne	.L3164
.L3167:
	bl	zftl_sblk_list_init
	ldr	r3, [r4, #2804]
	ldrh	r2, [r3, #140]
	cmp	r2, r7
	bgt	.L3166
	add	r6, r6, #1
	strh	r6, [r3, #140]	@ movhi
	bl	ftl_ext_info_flush
	b	.L3166
.L3164:
	ldrb	r1, [ip, #2772]	@ zero_extendqisi2
	ldr	r0, [r9, #148]
	str	r3, [sp, #20]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	beq	.L3167
	ldr	r0, .L3180+32
	ldr	r1, [r9, #-112]
	bl	printk
.L3163:
	add	r8, r8, #1
	b	.L3162
.L3166:
	ldr	r5, [r4, #1092]
	bl	timer_get_time
	ldr	r3, .L3180+36
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
	beq	.L3169
	ldr	r5, [r3, #520]
	ldr	r4, [r3, #12]
	bl	timer_get_time
	ldr	r1, .L3180+16
	mov	r2, r5
	mov	r3, r4
	str	r0, [sp]
	ldr	r0, .L3180+40
	bl	printk
.L3169:
	ldr	r3, .L3180+44
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, #-8]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3181:
	.align	2
.L3180:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC195
	.word	.LANCHOR1+2420
	.word	1162432070
	.word	.LANCHOR3-3142
	.word	.LANCHOR3-120
	.word	.LC196
	.word	1374389535
	.word	.LC197
	.word	.LANCHOR3-3216
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
	ldr	r2, .L3184
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
.L3185:
	.align	2
.L3184:
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
	ldr	r5, .L3191
	cmp	r3, #1
	ldrh	r8, [r0, #12]
	ldr	r6, [r5, #-2624]
	bne	.L3188
	bl	ftl_get_new_free_page
	cmn	r0, #1
	mov	r7, r0
	beq	.L3189
	ldrh	r0, [r4]
	add	r6, r6, r8, asl #2
	bl	ftl_vpn_decrement
	ldr	r3, .L3191+4
	mov	r0, r6
	mov	r4, #0
	str	r3, [r5, #200]
	sub	r3, r5, #3168
	ldrh	r1, [r3, #-4]
	ldrb	r3, [r5, #-3195]	@ zero_extendqisi2
	mul	r1, r3, r1
	mov	r1, r1, asl #2
	bl	js_hash
	mov	r1, r5
	str	r4, [r1, #216]!
	mov	r2, r4
	str	r4, [r5, #208]
	str	r4, [r5, #212]
	str	r0, [r5, #204]
	mov	r0, #2
	bl	ftl_debug_info_fill
	ldr	r3, .L3191+8
	mov	r0, r7
	mov	r1, r6
	add	r2, r5, #200
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	b	.L3189
.L3188:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3189:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3192:
	.align	2
.L3191:
	.word	.LANCHOR3
	.word	-178307901
	.word	.LANCHOR0
	.fnend
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
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
	ldr	r8, .L3207
	mov	r7, r1
	ldr	r9, .L3207+4
	mov	r4, r8
.L3194:
	ldr	r3, [r8, #2804]
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	add	r2, r3, #696
	ldrh	r1, [r2]
	ldrh	r2, [r9]
	cmp	r1, r2
	bcs	.L3195
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3196
.L3195:
	bl	pm_alloc_new_blk
	mov	r0, #0
	bl	ftl_info_flush
.L3196:
	ldr	r3, [r4, #2804]
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3197
	ldr	r1, .L3207+8
	movw	r2, #303
	ldr	r0, .L3207+12
	bl	printk
	bl	dump_stack
.L3197:
	ldr	r3, [r4, #2804]
	ldr	r5, .L3207+16
	add	r2, r3, #692
	add	r3, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3207+20
	ldr	r0, [r5, #456]
	ldrh	r10, [r2]
	ldrh	r2, [r3]
	mla	r10, r10, r1, r2
	mov	r2, #64
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r5, #456]
	mov	r0, r7
	str	r6, [r3]
	ldr	r3, [r4, #2804]
	ldr	fp, [r5, #456]
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
	ldr	r3, [r5, #456]
	bl	ftl_prog_page
	ldr	r2, [r4, #2804]
	add	r1, r2, #696
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r1]	@ movhi
	cmp	r3, #1
	beq	.L3198
	ldrb	r3, [r5, #460]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3199
.L3198:
	mov	r3, #0
	strb	r3, [r5, #460]
	b	.L3194
.L3199:
	cmn	r0, #1
	bne	.L3201
	mov	r1, r10
	ldr	r0, .L3207+24
	bl	printk
	b	.L3194
.L3201:
	movw	r3, #698
	mov	r0, #0
	ldrh	r3, [r2, r3]
	cmp	r6, r3
	addcc	r6, r6, #176
	strcc	r10, [r2, r6, asl #2]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3208:
	.align	2
.L3207:
	.word	.LANCHOR0
	.word	.LANCHOR3-3172
	.word	.LANCHOR1+2440
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR3-3142
	.word	.LC198
	.fnend
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3251
	movw	r2, #1076
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldrh	r2, [r3, r2]
	.pad #116
	sub	sp, sp, #116
	cmp	r2, r0
	bls	.L3209
	mov	fp, r3
	ldr	r3, .L3251+4
	ldrb	r2, [r3, #-3188]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3211
	ldrb	r2, [r3, #-3194]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3209
.L3211:
	ldrb	r2, [fp, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3209
	ldr	r2, .L3251
	mov	r4, r6, asl #2
	add	r1, sp, #96
	mov	r0, r6
	strh	r6, [sp, #80]	@ movhi
	mov	r5, #0
	ldr	r2, [r2, #1080]
	mov	r7, r5
	ldr	r8, .L3251+8
	mov	r9, r5
	add	r2, r2, r4
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #160
	ldreqb	r10, [r3, #-3196]	@ zero_extendqisi2
	movne	r10, #1
	bl	ftl_get_blk_list_in_sblk
	mov	r2, r8
	ldrh	r3, [r2, #-4]!
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
	ldr	r0, .L3251+12
	ubfx	r2, r2, #11, #8
	str	r2, [sp, #4]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	add	r3, r8, #3168
	mov	r4, r0
	str	r3, [sp, #48]
.L3213:
	ldr	r3, [sp, #44]
	uxth	r8, r5
	ldrh	r3, [r3]
	cmp	r3, r8
	bls	.L3236
	mov	r3, r8, asl #1
	mov	ip, #0
	add	r2, r3, r8
	sub	r3, r3, #1
	sub	r2, r2, #1
	str	r3, [sp, #60]
	str	r2, [sp, #52]
.L3224:
	ldrb	r3, [sp, #89]	@ zero_extendqisi2
	uxth	r9, ip
	cmp	r3, r9
	bls	.L3248
	add	r3, r9, #8
	mov	r7, #1
	mov	r3, r3, asl r7
	str	r3, [sp, #64]
.L3223:
	cmp	r7, r10
	bhi	.L3249
	ldr	r2, [sp, #64]
	add	r3, sp, #112
	add	r3, r3, r2
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3215
	ldr	r3, .L3251+16
	cmp	r10, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3216
	ldr	r1, .L3251
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #52]
	orreq	r3, r3, r7, asl #24
	beq	.L3218
	b	.L3247
.L3216:
	cmp	r10, #2
	uxtahne	r3, r3, r5
	bne	.L3218
	ldr	r1, [sp, #60]
.L3247:
	add	r3, r3, r1
	ldr	r1, [sp, #48]
	add	r3, r3, r7
	ldrb	r1, [r1, #-3196]	@ zero_extendqisi2
	orr	r3, r3, r1, asl #24
.L3218:
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
	bne	.L3215
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
	ldr	r0, .L3251+20
	str	r1, [sp, #32]
	mov	r1, r2
	mov	r2, r8
	bl	printk
	ldr	r3, [sp, #56]
	ldr	ip, [sp, #68]
	cmp	r3, #0
	bne	.L3236
.L3215:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3223
.L3249:
	add	ip, ip, #1
	b	.L3224
.L3248:
	add	r5, r5, #1
	b	.L3213
.L3236:
.L3221:
	str	r7, [sp]
	mov	r3, r9
	ldr	r7, .L3251
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L3251+24
	bl	printk
	mov	r1, #0
	ldrb	r2, [r7, #2772]	@ zero_extendqisi2
	mov	r5, r8
	ldr	r0, [r4, #4]
	ldr	r10, .L3251+28
	mov	r2, r2, asl #9
	ldr	r9, .L3251+32
	bl	ftl_memset
	ldrb	r2, [r7, #2772]	@ zero_extendqisi2
	ldr	r0, [r4, #12]
	mov	r1, #0
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3225:
	ldrh	r3, [r10]
	cmp	r3, r5
	bls	.L3231
	ldr	r8, .L3251+4
	mov	r7, #0
	mov	r3, r5, asl #1
	str	r3, [sp, #44]
.L3232:
	ldrb	r2, [sp, #89]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bls	.L3250
	ldr	r2, [r9]
	tst	r2, #4096
	beq	.L3226
	mov	r2, r3
	ldr	r0, .L3251+36
	mov	r1, r5
	str	r3, [sp, #48]
	bl	printk
	ldr	r3, [sp, #48]
.L3226:
	ldrb	ip, [r8, #-3188]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L3227
	add	r2, sp, #112
	mov	r0, r4
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3251+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	orr	r3, r3, r5
	str	r3, [r4, #24]
	bl	sblk_3d_tlc_dump_prog
	b	.L3228
.L3227:
	ldrb	r2, [r8, #-3196]	@ zero_extendqisi2
	cmp	r2, #2
	add	r2, sp, #112
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3251+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3229
	ldr	r2, [sp, #44]
	mov	r0, r4
	orr	r3, r3, r2
	orr	r3, r3, #33554432
	str	r3, [r4, #24]
	bl	sblk_mlc_dump_prog
	b	.L3228
.L3229:
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
.L3228:
	add	r7, r7, #1
	b	.L3232
.L3250:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L3225
.L3231:
	mov	r0, r4
	bl	zbuf_free
	ldr	r0, .L3251+40
	mov	r1, r6
	bl	printk
.L3209:
	add	sp, sp, #116
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3252:
	.align	2
.L3251:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3168
	.word	.LC199
	.word	.LANCHOR3-3142
	.word	.LC200
	.word	.LC201
	.word	.LANCHOR3-3172
	.word	.LANCHOR2
	.word	.LC202
	.word	.LC203
	.fnend
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3267
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3254
	ldr	r0, .L3267+4
	mov	r1, #364
	ldr	r2, .L3267+8
	bl	printk
.L3254:
	ldr	r5, .L3267+12
	mov	r1, #0
	ldr	r6, .L3267+16
	mov	r2, #64
	add	r8, r5, #468
	mov	r9, #0
	ldr	r0, [r5, #464]
	mov	r7, r5
	bl	ftl_memset
	ldr	r0, .L3267+20
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
.L3255:
	ldrb	r3, [r5, #468]	@ zero_extendqisi2
	ldrh	r4, [r8, #2]
	ldrh	ip, [r6, #2]
	mov	r1, r3
	ldr	r0, .L3267+24
	mov	r2, r4
	str	r3, [sp, #8]
	str	ip, [sp, #12]
	bl	printk
	ldr	r2, .L3267+28
	ldrh	r0, [r8, #2]
	ldr	fp, .L3267+16
	ldrh	r2, [r2]
	sub	r2, r2, #1
	cmp	r0, r2
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #12]
	blt	.L3256
	ldr	r4, [fp, #1176]
	ldrb	r2, [r7, #469]	@ zero_extendqisi2
	ldr	r3, [r4, #4]
	mov	r0, r4
	add	r3, r3, #1
	str	r3, [r4, #4]
	ldrb	r3, [r7, #468]	@ zero_extendqisi2
	strb	r2, [r7, #468]
	ldrh	r2, [r4, #16]
	strb	r3, [r7, #469]
	mov	r3, #0
	add	r2, r2, #1
	strh	r2, [r0, #16]!	@ movhi
	ldr	r1, [r4, #8]
	strh	r3, [r8, #2]	@ movhi
	str	r3, [sp, #8]
	bl	js_hash
	ldrb	r2, [r7, #468]	@ zero_extendqisi2
	str	r0, [r4, #12]
	ldr	r3, [sp, #8]
	ldrh	r4, [fp, #2]
	mov	r0, r3
	mul	r4, r4, r2
	b	.L3265
.L3256:
	cmp	r0, #0
	mla	r4, ip, r3, r4
	bne	.L3257
.L3265:
	mov	r1, r4
	bl	flash_erase_block
.L3257:
	ldr	r2, [r6, #1176]
	mov	fp, #1
	ldr	r3, [r5, #464]
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r2, #4]
	str	r2, [r3]
	ldr	r2, .L3267+32
	ldr	r3, [r5, #464]
	str	r2, [r3, #4]
	mov	r2, #4
	stmia	sp, {r2, fp}
	ldr	r2, [r6, #1176]
	bl	flash_prog_page_en
	ldrh	r3, [r10, #2]
	add	r3, r3, fp
	strh	r3, [r10, #2]	@ movhi
	cmn	r0, #1
	bne	.L3258
	mov	r1, r4
	ldr	r0, .L3267+36
	bl	printk
	b	.L3255
.L3258:
	cmp	r9, #0
	moveq	r9, fp
	beq	.L3255
.L3266:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3268:
	.align	2
.L3267:
	.word	.LANCHOR2
	.word	.LC141
	.word	.LANCHOR1+2456
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC204
	.word	.LC205
	.word	.LANCHOR3-292
	.word	1398362953
	.word	.LC206
	.fnend
	.size	flash_info_flush, .-flash_info_flush
	.align	2
	.global	flash_info_blk_init
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3302
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	ldr	r6, .L3302+4
	tst	r3, #4096
	beq	.L3270
	ldr	r3, [r6, #1176]
	mov	r1, #49
	ldr	r0, .L3302+8
	ldr	r2, .L3302+12
	str	r3, [sp]
	mov	r3, #2048
	bl	printk
.L3270:
	ldr	ip, .L3302+4
	mov	r7, #4
	ldr	fp, .L3302+16
.L3271:
	mov	r5, #0
.L3274:
	ldrh	r1, [r6, #2]
	mov	r8, #4
	ldr	r4, .L3302+20
	mov	r0, #0
	str	r8, [sp]
	mla	r1, r1, r7, r5
	ldr	r2, [r6, #1176]
	ldr	r3, [r4, #464]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	ldr	r9, .L3302+4
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3272
	ldr	r2, [ip, #1176]
	ldr	r10, .L3302+16
	ldr	r3, [r2]
	cmp	r3, fp
	beq	.L3273
.L3272:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L3274
	add	r7, r7, #1
	cmp	r7, #16
	bne	.L3271
	b	.L3283
.L3273:
	ldrb	r1, [r2, #37]	@ zero_extendqisi2
	mov	r0, #0
	ldrb	r3, [r2, #36]	@ zero_extendqisi2
	strb	r1, [r4, #469]
	ldrh	r1, [r9, #2]
	strb	r3, [r4, #468]
	str	r8, [sp]
	mul	r1, r1, r3
	ldr	r3, [r4, #464]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3284
	ldr	r3, [r9, #1176]
	ldr	r2, [r3]
	cmp	r2, fp
	ldreq	r5, [r3, #4]
	bne	.L3284
.L3276:
	ldrb	r3, [r4, #469]	@ zero_extendqisi2
	mov	r2, #4
	ldrh	r1, [r6, #2]
	mov	r0, #0
	str	r2, [sp]
	ldr	r2, [r6, #1176]
	mul	r1, r1, r3
	ldr	r3, [r4, #464]
	bl	flash_read_page_en
	ldr	r7, .L3302+20
	cmn	r0, #1
	bne	.L3301
.L3277:
	ldr	r2, [r6, #1176]
	mov	r8, #4
	ldr	r3, [r4, #464]
	mov	r0, #0
	ldrb	r1, [r4, #468]	@ zero_extendqisi2
	mov	r7, #0
	str	r8, [sp]
	bl	flash_get_last_written_page
	ldr	r3, .L3302+24
	ldrh	r9, [r6, #2]
	ldr	ip, .L3302+4
	uxth	fp, r0
	add	r2, fp, #1
	strh	r2, [r3, #2]	@ movhi
	ldrb	r3, [r4, #468]	@ zero_extendqisi2
	mla	r9, r9, r3, r0
.L3278:
	rsb	r5, r7, fp
	sxth	r5, r5
	cmp	r5, #0
	bge	.L3281
	cmn	r5, #1
	bne	.L3280
	ldr	r3, [r6, #1176]
	ldr	r0, .L3302+28
	ldr	r1, [r3]
	bl	printk
.L3283:
	mvn	r0, #0
	b	.L3299
.L3284:
	mov	r5, #0
	b	.L3276
.L3301:
	ldr	r3, .L3302+4
	ldr	r3, [r3, #1176]
	ldr	r2, [r3]
	cmp	r2, r10
	bne	.L3277
	ldr	r2, [r3, #4]
	cmp	r5, r2
	ldrccb	r2, [r3, #37]	@ zero_extendqisi2
	ldrccb	r3, [r3, #36]	@ zero_extendqisi2
	strccb	r2, [r7, #468]
	strccb	r3, [r7, #469]
	b	.L3277
.L3281:
	str	r8, [sp]
	mov	r0, #0
	rsb	r1, r7, r9
	ldr	r2, [r6, #1176]
	ldr	r3, [r4, #464]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3279
	ldr	r3, [ip, #1176]
	ldr	r3, [r3]
	cmp	r3, r10
	beq	.L3280
.L3279:
	add	r7, r7, #1
	b	.L3278
.L3280:
	cmp	r7, #1
	bls	.L3285
	bl	flash_info_flush
.L3285:
	mov	r0, #0
.L3299:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3303:
	.align	2
.L3302:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC207
	.word	.LANCHOR1+2476
	.word	1398362953
	.word	.LANCHOR3
	.word	.LANCHOR3+468
	.word	.LC208
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
	ldr	r6, .L3394
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3305
	ldr	r0, .L3394+4
	movw	r1, #3429
	ldr	r2, .L3394+8
	bl	printk
.L3305:
	ldr	r5, .L3394+12
	mov	r0, r4
	mov	r7, #0
	ldr	r4, .L3394+16
	str	r7, [r5, #-184]
	bl	nandc_init
	ldr	r3, .L3394+20
	mov	r1, r7
	mov	r2, #8
	ldr	r0, .L3394+24
	add	r10, r4, #1200
	mov	r9, r4
	str	r3, [r4, #1096]
	mov	r3, #1
	strb	r3, [r4, #1101]
	mov	r3, #3
	strb	r3, [r4, #1189]
	bl	ftl_memset
	sub	r0, r5, #288
	mov	r1, r7
	mov	r2, #32
	mov	fp, r10
	bl	ftl_memset
.L3311:
	mov	r8, r7, asl #3
	uxtb	r0, r7
	add	r1, r10, r8
	bl	flash_read_id
	cmp	r7, #0
	ldr	r1, .L3394+28
	bne	.L3306
	ldrb	r3, [r9, #1200]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bls	.L3307
.L3309:
	mvn	r7, #1
	b	.L3383
.L3307:
	ldrb	r3, [r9, #1201]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3309
.L3306:
	ldrb	r3, [fp, r8]	@ zero_extendqisi2
	add	r7, r7, #1
	cmp	r3, #181
	ldreq	r3, .L3394+28
	moveq	r2, #44
	streqb	r2, [r8, r3]
	cmp	r7, #4
	bne	.L3311
	ldr	ip, .L3394+32
	mov	r3, #0
.L3347:
	mov	r0, r3, asl #5
	ldrb	r8, [ip, r3, asl #5]	@ zero_extendqisi2
	add	r7, r0, #1
	ldr	lr, .L3394+32
	add	r7, ip, r7
	mov	r2, #0
.L3312:
	cmp	r2, r8
	bcs	.L3393
	ldrb	r10, [r7, r2]	@ zero_extendqisi2
	ldrb	r9, [r1, r2]	@ zero_extendqisi2
	cmp	r10, r9
	bne	.L3313
	add	r2, r2, #1
	b	.L3312
.L3393:
	adds	r8, lr, r0
	beq	.L3309
	ldr	r3, .L3394
	ldr	r2, .L3394+36
	add	r0, r3, r0
	mov	r3, #0
	ldrb	r1, [r0, #446]	@ zero_extendqisi2
.L3317:
	ldrb	r0, [r2, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r1
	beq	.L3316
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L3317
.L3316:
	ldr	r1, .L3394+36
	mov	r2, #32
	ldr	r7, .L3394
	add	r1, r1, r3, asl #5
	ldr	r0, .L3394+40
	bl	ftl_memcpy
	add	r0, r7, #4
	mov	r1, r8
	mov	r2, #32
	bl	ftl_memcpy
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #8
	bhi	.L3318
	ldrb	r3, [r7, #24]	@ zero_extendqisi2
	cmp	r3, #60
	movhi	r3, #60
	strhib	r3, [r7, #24]
	b	.L3318
.L3313:
	add	r3, r3, #1
	cmp	r3, #49
	bne	.L3347
	b	.L3309
.L3318:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3349
	ldr	r0, .L3394+4
	mov	r1, #3456
	ldr	r2, .L3394+8
	bl	printk
.L3349:
	ldr	r3, [r4, #1096]
	mov	r0, #16384
	mov	r7, #0
	ldrh	r3, [r3, #10]
	cmp	r3, #1024
	ldrcs	r3, .L3394+16
	movcs	r2, #2
	strcsb	r2, [r3, #1189]
	bl	ftl_malloc
	str	r0, [r5, #-176]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-200]
	mov	r0, #2048
	bl	ftl_malloc
	str	r0, [r4, #1176]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-172]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-192]
	mov	r0, #64
	bl	ftl_malloc
	strb	r7, [r5, #472]
	str	r0, [r5, #464]
	bl	flash_die_info_init
	ldrb	r0, [r6, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrb	r0, [r6, #24]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r1, [r4, #1096]
	str	r7, [r5, #-180]
	ldrh	r3, [r1, #16]
	ubfx	r2, r3, #8, #3
	strb	r2, [r4, #1232]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #-160]
	ubfx	r2, r3, #4, #1
	strb	r2, [r4, #1233]
	ubfx	r2, r3, #12, #1
	strb	r2, [r5, #-3188]
	ubfx	r2, r3, #13, #1
	strb	r2, [r5, #-3194]
	ubfx	r2, r3, #11, #1
	strb	r2, [r4, #1196]
	ldrb	r2, [r1, #31]	@ zero_extendqisi2
	ldrb	r1, [r1, #28]	@ zero_extendqisi2
	ubfx	r0, r2, #1, #1
	strb	r0, [r5, #-2612]
	ubfx	r0, r2, #2, #1
	strb	r0, [r5, #473]
	ubfx	r0, r3, #14, #1
	mov	r3, r3, lsr #15
	strb	r3, [r5, #-3193]
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
	ldreq	r2, .L3394+16
	ldrb	r1, [r6, #35]	@ zero_extendqisi2
	strb	r0, [r4, #1]
	moveq	r0, #70
	streqb	r0, [r2, #1100]
	tst	r1, #1
	ldr	r2, .L3394+16
	beq	.L3321
	ldr	r1, .L3394
	ldrb	r1, [r1, #33]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, #2
	moveq	r1, #3
	strb	r1, [r2, #1102]
.L3321:
	cmp	r3, #8
	bne	.L3323
	ldrb	r3, [r4, #1200]	@ zero_extendqisi2
	cmp	r3, #44
	cmpne	r3, #137
	bne	.L3323
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #3
	ldreq	r3, .L3394+16
	moveq	r2, #0
	streqb	r2, [r3, #1102]
.L3323:
	ldrh	r2, [r6, #20]
	ldrb	r3, [r6, #23]	@ zero_extendqisi2
	tst	r2, #64
	ldr	r0, .L3394+16
	strb	r3, [r4, #1173]
	beq	.L3325
	sub	r2, r3, #17
	sub	r1, r3, #21
	clz	r1, r1
	cmp	r3, #21
	cmpne	r2, #2
	mov	r1, r1, lsr #5
	bhi	.L3326
	ldr	r2, .L3394+44
	cmp	r1, #0
	ldr	r3, .L3394+12
	str	r2, [r5, #-180]
	moveq	r2, #15
	movne	r2, #4
	strb	r2, [r3, #-188]
	b	.L3325
.L3326:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r3, .L3394+48
	strls	r3, [r5, #-180]
	movls	r3, #4
	strlsb	r3, [r0, #1174]
	bls	.L3392
.L3329:
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
	beq	.L3330
	ldr	r2, .L3394+48
	ldr	r3, .L3394+12
	str	r2, [r5, #-180]
	orrs	r2, lr, ip
	moveq	r2, #7
	movne	r2, #17
	cmp	r1, #0
	strb	r2, [r3, #-188]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #1174]
	b	.L3325
.L3330:
	sub	r1, r3, #36
	cmp	r1, #1
	bhi	.L3334
	ldr	r3, .L3394+52
	str	r3, [r5, #-180]
.L3392:
	mov	r3, #7
	b	.L3390
.L3334:
	cmp	r3, #50
	bne	.L3335
	ldr	r3, .L3394+56
	str	r3, [r5, #-180]
	mov	r3, #25
.L3390:
	strb	r3, [r5, #-188]
	b	.L3325
.L3335:
	cmp	r3, #81
	streqb	r2, [r0, #1136]
	ldreq	r3, .L3394+60
	streq	r3, [r5, #-180]
	moveq	r3, #7
	streqb	r3, [r5, #-188]
.L3325:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3336
	ldr	r0, .L3394+4
	movw	r1, #3549
	ldr	r2, .L3394+8
	bl	printk
.L3336:
	mov	r3, #0
	strb	r3, [r4, #1135]
	ldrb	r3, [r4, #1232]	@ zero_extendqisi2
	tst	r3, #1
	moveq	r0, #4
	beq	.L3391
	ldr	r3, .L3394+16
	ldrb	r3, [r3, #1200]	@ zero_extendqisi2
	cmp	r3, #155
	beq	.L3338
	mov	r0, #4
	bl	flash_set_interface_mode
	mov	r0, #4
	bl	nandc_set_if_mode
.L3338:
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
.L3391:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	r0, #1
	mov	r7, r0
	bne	.L3340
	ldr	r3, [r4, #1176]
	mov	r2, #17
	mov	r0, #0
	strb	r0, [r4, #1192]
	strb	r2, [r3, #32]
	bl	zftl_flash_exit_slc_mode
	b	.L3383
.L3340:
	ldrb	r3, [r6, #11]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L3341
	ldr	r3, [r4, #1176]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	cmp	r3, #1
	ldrne	r3, .L3394+16
	movne	r2, #0
	strneb	r2, [r3, #1102]
.L3341:
	ldrb	r3, [r4, #1173]	@ zero_extendqisi2
	ldr	r7, .L3394+16
	sub	r3, r3, #1
	cmp	r3, #7
	ldrls	r3, .L3394+64
	strls	r3, [r5, #-180]
	ldrb	r3, [r4, #1232]	@ zero_extendqisi2
	tst	r3, #4
	beq	.L3344
	ldr	r3, [r7, #1176]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3344
	ldrb	r3, [r5, #468]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r1, [r7, #2]
	mul	r1, r1, r3
	bl	flash_ddr_para_scan
	ldrb	r3, [r7, #1135]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3344
	ldr	r2, [r7, #1176]
	strb	r3, [r2, #19]
	bl	flash_info_flush
.L3344:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3346
	ldr	r0, .L3394+4
	movw	r1, #3647
	ldr	r2, .L3394+8
	bl	printk
.L3346:
	bl	nand_flash_print_info
	mov	r7, #0
.L3383:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3395:
	.align	2
.L3394:
	.word	.LANCHOR2
	.word	.LC141
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
	ldr	r10, .L3408
	mov	r6, #0
	mov	r5, r6
	ldr	ip, [r0, #4]
	mov	r4, r0
.L3397:
	ldrh	r3, [r10]
	uxth	r8, r6
	cmp	r3, r8
	bls	.L3407
	ldr	r3, .L3408+4
	add	r6, r6, #1
	ldr	r1, [r4, #4]
	ldr	r2, [r4, #12]
	ldrh	r7, [r3]
	ldr	r3, .L3408+8
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
	ldr	r0, .L3408+12
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
	b	.L3397
.L3407:
	ldr	r0, .L3408+16
	add	r1, ip, #704
	mov	r2, #4
	mov	r3, #32
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	cmp	r5, #0
	beq	.L3400
	ldr	r1, .L3408+20
	movw	r2, #1611
	ldr	r0, .L3408+24
	bl	printk
	bl	dump_stack
.L3400:
	mov	r0, r5
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3409:
	.align	2
.L3408:
	.word	.LANCHOR3-3172
	.word	.LANCHOR3-3142
	.word	.LANCHOR0
	.word	.LC200
	.word	.LC209
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
	ldr	r5, .L3549
	mov	r4, r0
	str	r1, [sp, #28]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3411
	ldr	r0, .L3549+4
	ldrh	r1, [r4, #2]
	bl	printk
.L3411:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3412
	ldr	r0, .L3549+8
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	bl	printk
.L3412:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3413
	ldr	r0, .L3549+12
	ldrh	r1, [r4]
	bl	printk
.L3413:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3414
	ldr	r0, .L3549+16
	ldrh	r1, [r4, #16]
	ldrh	r2, [r4, #18]
	bl	printk
.L3414:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3415
	ldr	r0, .L3549+20
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	bl	printk
.L3415:
	ldrh	r3, [r4, #10]
	ldr	r6, .L3549+24
	ldrh	r2, [r4]
	strh	r3, [r4, #14]	@ movhi
	movw	r3, #1076
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bcs	.L3410
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
.L3417:
	ldr	r3, .L3549+28
	ldrh	r3, [r3]
	cmp	r3, r7
	bls	.L3420
	ldrb	r6, [r4, #5]	@ zero_extendqisi2
	ldr	r8, .L3549+24
.L3418:
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L3545
	add	r3, r4, r6, asl #1
	ldrh	r10, [r3, #16]
	movw	r3, #65535
	cmp	r10, r3
	beq	.L3419
	ldr	r3, .L3549+32
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
	ldr	r3, .L3549+36
	mov	r2, #1
	strb	r2, [r3, #145]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r2, [r4, #10]
	mla	r3, r3, r7, r6
	cmp	r2, r3
	beq	.L3422
	ldr	r1, .L3549+40
	movw	r2, #1686
	ldr	r0, .L3549+44
	bl	printk
	bl	dump_stack
.L3422:
	ldrh	r3, [r4, #6]
	ldrh	r2, [r4, #10]
	add	r2, r3, r2
	ldr	r3, .L3549+28
	ldrh	r1, [r3]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L3423
	ldr	r1, .L3549+40
	movw	r2, #1687
	ldr	r0, .L3549+44
	bl	printk
	bl	dump_stack
.L3423:
	ldr	r3, .L3549+24
	ldrb	r3, [r3, #2772]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L3424
	ldr	r3, [r5, #12]
	ldr	r1, .L3549+48
	ldr	r2, [r3]
	cmp	r2, r1
	beq	.L3424
	cmn	r9, #1
	beq	.L3429
	ldr	r2, [r3, #4]
	cmn	r2, #1
	beq	.L3462
	ldr	r2, [r3, #16]
	ldr	r1, .L3549+52
	cmp	r2, r1
	bne	.L3462
	ldr	r0, [sp, #16]
	mov	r1, #1024
	ldr	fp, [r3, #20]
	bl	js_hash
	cmp	fp, r0
	beq	.L3462
	mov	r1, #1024
	ldr	r0, [sp, #16]
	bl	js_hash
	mov	r1, r10
	mov	r2, r7
	str	r9, [sp]
	str	r0, [sp, #4]
	ldr	r3, [sp, #20]
	ldr	r0, .L3549+56
	bl	printk
	ldr	r0, .L3549+60
	mov	r2, #4
	mov	r3, #16
	ldr	r1, [sp, #16]
	bl	rknand_print_hex
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r0, .L3549+64
	mov	r2, #4
	ldr	r1, [r5, #12]
	mov	r3, r3, lsr #1
	bl	rknand_print_hex
	b	.L3429
.L3424:
	cmn	r9, #1
	beq	.L3429
.L3462:
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #4]
	cmn	r0, #1
	beq	.L3429
	ldr	r3, [r3]
	ldr	r2, .L3549+48
	cmp	r3, r2
	beq	.L3429
	bl	lpa_hash_get_ppa
	ldr	r2, [sp, #28]
	mov	r3, r0
	cmp	r2, #0
	beq	.L3428
	ldr	r2, [r5, #12]
	ldr	r1, [r2, #8]
	str	r2, [sp, #24]
	cmp	r0, r1
	cmnne	r0, #1
	beq	.L3428
	ldr	r2, .L3549+24
	mov	r9, #1
	ldr	fp, .L3549+68
	ldr	r10, .L3549+36
	ldrb	r1, [r2, #1189]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r1, r1, #24
	str	r3, [sp, #12]
	rsb	r1, r0, r1
	mov	r1, r9, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r10, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	uxth	r0, r0
	ldr	r2, [sp, #24]
	ldrh	r1, [r3]
	ldr	r3, [sp, #12]
	cmp	r1, r0
	bne	.L3428
	ldr	ip, [r2]
	mov	r0, r3
	ldr	r1, [r5, #4]
	sub	r2, r10, #120
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	str	ip, [sp, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r10, #-120]
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bcc	.L3428
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #8]
	cmn	r0, #1
	beq	.L3429
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	ldrh	r2, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r10, #-3202]	@ zero_extendqisi2
	rsb	r3, r2, r3
	mov	r9, r9, asl r3
	sub	r9, r9, #1
	and	r0, r9, r0, lsr r2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L3429
.L3428:
	ldr	r2, [r5, #12]
	ldr	r3, [r8, #2784]
	ldr	r1, [r2, #4]
	cmp	r1, r3
	bcs	.L3429
	ldr	r3, .L3549+28
	ldrb	r0, [r4, #9]	@ zero_extendqisi2
	ldrh	r1, [r4, #10]
	ldrh	r3, [r3]
	mul	r3, r3, r0
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3429
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
.L3429:
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
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3418
.L3545:
	add	r7, r7, #1
	mov	r3, #0
	strb	r3, [r4, #5]
	uxth	r7, r7
	b	.L3417
.L3420:
	ldrh	r3, [r4, #6]
	strb	r6, [r4, #5]
	ldrh	r6, [r4, #10]
	ldrb	fp, [r4, #9]	@ zero_extendqisi2
	add	r6, r3, r6
	ldr	r3, .L3549+72
	strh	r7, [r4, #2]	@ movhi
	ldrh	ip, [r3, #-4]
	str	r3, [sp, #16]
	mul	fp, ip, fp
	cmp	r6, fp
	beq	.L3433
	ldr	r1, .L3549+40
	movw	r2, #1794
	ldr	r0, .L3549+44
	bl	printk
	bl	dump_stack
.L3433:
	ldr	r9, .L3549+36
	mov	r8, #0
	ldrh	r6, [r4, #10]
	mov	r7, r8
	ldr	r10, [r9, #-2624]
.L3434:
	cmp	r7, r6
	bcs	.L3546
	ldrh	r3, [r4, #12]
	add	r3, r7, r3
	add	r7, r7, #1
	ldr	r3, [r10, r3, asl #2]
	cmn	r3, #1
	addne	r8, r8, #1
	uxthne	r8, r8
	b	.L3434
.L3546:
	ldr	r2, [sp, #16]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r10, [r2, #-4]
	smulbb	r3, r3, r10
	ldr	r10, .L3549+24
	rsb	r6, r6, r3
	add	r6, r8, r6
	ldr	r8, .L3549
	uxth	r6, r6
	ldr	r3, [r8]
	tst	r3, #4096
	beq	.L3437
	ldrh	r1, [r4]
	mov	r2, r6
	ldr	ip, [r10, #1088]
	ldr	r0, .L3549+76
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L3437:
	ldrh	r3, [r4]
	ldr	r2, [r10, #1088]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [r8]
	tst	r3, #16384
	beq	.L3438
	ldr	r3, [sp, #44]
	add	r1, sp, #32
	ldr	r0, .L3549+80
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	printk
.L3438:
	ldrb	r2, [r10, #2772]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r5, #4]
	mov	r7, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L3439:
	ldrb	r3, [r9, #-3195]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r7, r3
	bcs	.L3547
	add	ip, sp, #32
	ldr	r0, [ip, r7, asl #2]
	cmn	r0, #1
	bne	.L3440
.L3444:
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
	b	.L3441
.L3440:
	ldr	r3, .L3549+24
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
	beq	.L3442
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3549+84
	ldr	r2, [ip, r7, asl #2]
	ldr	r3, [r3, #4]
	bl	printk
.L3442:
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
	beq	.L3444
.L3441:
	ldr	r3, [sp, #24]
	cmp	r3, #1
	bne	.L3446
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bls	.L3446
.L3516:
	mov	r0, r4
	bl	ftl_get_new_free_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3448
	ldrh	r1, [r4, #12]
	ldr	r2, [r5, #12]
	ldrh	r3, [r4, #10]
	ldr	r0, .L3549+88
	add	r3, r3, r1
	ldr	r2, [r2, #4]
	mov	r1, fp
	sub	r3, r3, #1
	bl	printk
.L3448:
	ldrb	r3, [r9, #-3195]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	ldrh	r2, [r4, #6]
	mul	r3, r3, r1
	add	r3, r3, #1
	rsb	r3, r7, r3
	cmp	r2, r3
	bls	.L3446
	ldr	r3, .L3549+24
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
	beq	.L3449
	add	r3, sp, #96
	ldrh	r2, [r4, #12]
	str	fp, [r3, r7, asl #2]
	ldrh	r3, [r4, #10]
	add	r3, r3, r2
	add	r2, sp, #160
	sub	r3, r3, #1
	str	r3, [r2, r7, asl #2]
	b	.L3446
.L3449:
	ldrh	r2, [r4, #6]
	cmp	r2, #1
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L3516
.L3446:
	add	r7, r7, #1
	b	.L3439
.L3547:
	ldr	r10, .L3549+24
	mov	r6, #0
.L3452:
	ldrb	r3, [r9, #-3195]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r6, r3
	bcs	.L3548
	add	fp, sp, #96
	ldr	r3, [fp, r6, asl #2]
	cmn	r3, #1
	beq	.L3454
	add	r7, sp, #32
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldr	r0, [r7, r6, asl #2]
	ldrb	r3, [r10, #2772]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #256
	bne	.L3454
	ldr	r0, [fp, r6, asl #2]
	ldrb	r3, [r10, #2772]	@ zero_extendqisi2
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3456
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3549+88
	ldr	r2, [r3, #8]
	ldr	r3, [r7, r6, asl #2]
	bl	printk
.L3456:
	cmn	fp, #1
	beq	.L3454
	ldr	r3, [r5, #12]
	ldr	r2, [r7, r6, asl #2]
	ldr	r1, [r3, #8]
	cmp	r2, r1
	bne	.L3454
	ldr	r1, [r8]
	add	r7, sp, #160
	tst	r1, #16384
	beq	.L3457
	ldr	r1, [r3, #4]
	ldr	r0, .L3549+92
	ldr	r3, [r7, r6, asl #2]
	bl	printk
.L3457:
	ldr	r3, [r5, #12]
	ldr	r2, [r7, r6, asl #2]
	ldmib	r3, {r0, r1}
	uxth	r2, r2
	bl	lpa_hash_update_ppa
.L3454:
	add	r6, r6, #1
	b	.L3452
.L3548:
	mov	r0, r5
	bl	zbuf_free
	ldrh	r3, [r4, #12]
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #16]
	ldrh	r2, [r2, #-4]
	mla	r3, r1, r2, r3
	ldr	r2, .L3549+36
	ldr	r2, [r2, #-2624]
	sub	r3, r3, #-1073741823
	ldr	r3, [r2, r3, asl #2]
	cmn	r3, #1
	beq	.L3459
	ldr	r1, .L3549+40
	movw	r2, #1909
	ldr	r0, .L3549+44
	bl	printk
	bl	dump_stack
.L3459:
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L3410
	mov	r0, r4
	bl	ftl_write_last_log_page
.L3410:
	add	sp, sp, #228
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3550:
	.align	2
.L3549:
	.word	.LANCHOR2
	.word	.LC210
	.word	.LC211
	.word	.LC212
	.word	.LC213
	.word	.LC214
	.word	.LANCHOR0
	.word	.LANCHOR3-3172
	.word	.LANCHOR3-3142
	.word	.LANCHOR3
	.word	.LANCHOR1+2528
	.word	.LC0
	.word	-178307901
	.word	1212240712
	.word	.LC215
	.word	.LC216
	.word	.LC187
	.word	.LANCHOR3-3204
	.word	.LANCHOR3-3168
	.word	.LC217
	.word	.LC218
	.word	.LC219
	.word	.LC220
	.word	.LC221
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
	ldr	r4, .L3553
	ldr	r5, .L3553+4
	ldr	r0, .L3553+8
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	sub	r6, r5, #3168
	bl	printk
	ldrb	r1, [r5, #136]	@ zero_extendqisi2
	ldrb	r2, [r5, #137]	@ zero_extendqisi2
	ldrh	r3, [r5, #138]
	ldr	r0, .L3553+12
	bl	printk
	ldr	r3, [r4, #2804]
	ldr	r0, .L3553+16
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
	ldr	r0, .L3553+20
	bl	printk
	ldr	r0, [r4, #1092]
	ldrh	ip, [r0, #54]
	ldrh	r1, [r0, #48]
	ldrh	r2, [r0, #50]
	ldrb	r3, [r0, #53]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #58]
	str	r0, [sp, #4]
	ldr	r0, .L3553+24
	bl	printk
	ldr	r0, [r4, #1092]
	ldrh	ip, [r0, #86]
	ldrh	r1, [r0, #80]
	ldrh	r2, [r0, #82]
	ldrb	r3, [r0, #85]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #90]
	str	r0, [sp, #4]
	ldr	r0, .L3553+28
	bl	printk
	ldrh	r2, [r6, #-4]
	ldrb	r3, [r5, #-3195]	@ zero_extendqisi2
	ldr	r1, [r5, #-2624]
	ldr	r0, .L3553+32
	mul	r3, r3, r2
	mov	r2, #4
	mov	r3, r3, asl #1
	bl	rknand_print_hex
	ldr	r1, [r4, #1088]
	ldrh	r3, [r4, r7]
	mov	r2, #2
	ldr	r0, .L3553+36
	bl	rknand_print_hex
	ldr	r3, [r4, #2804]
	movw	ip, #698
	ldr	r0, .L3553+40
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #1080]
	ldrh	r3, [r4, r7]
	mov	r2, #4
	ldr	r0, .L3553+44
	bl	rknand_print_hex
	sub	r1, r5, #3136
	ldr	r0, .L3553+48
	mov	r2, #2
	mov	r3, #256
	bl	rknand_print_hex
	ldrh	r2, [r6, #-4]
	ldrb	r3, [r5, #-3195]	@ zero_extendqisi2
	ldr	r0, .L3553+52
	ldr	r1, [r5, #-2620]
	mul	r3, r3, r2
	mov	r2, #2
	mov	r3, r3, asl #1
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, lr}
	b	rknand_print_hex
.L3554:
	.align	2
.L3553:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC222
	.word	.LC223
	.word	.LC224
	.word	.LC225
	.word	.LC226
	.word	.LC227
	.word	.LC228
	.word	.LC229
	.word	.LC209
	.word	.LC230
	.word	.LC231
	.word	.LC232
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
	ldr	r3, .L3559
	mov	r6, r0
	ldr	r7, .L3559+4
	mov	r5, r1
	sub	r2, r3, #3200
	mvn	r1, #0
	ldrh	r0, [r2, #-4]
	ldrb	r2, [r7, #1189]	@ zero_extendqisi2
	rsb	r2, r2, #24
	rsb	r2, r0, r2
	mov	r0, r4, lsr r0
	bic	r0, r0, r1, asl r2
	ldrb	r1, [r3, #-3202]	@ zero_extendqisi2
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
	ldr	r0, .L3559+8
	bl	printk
	bl	dump_ftl_info
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3560:
	.align	2
.L3559:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC233
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
	bls	.L3562
	ldr	r1, .L3576
	mov	r2, #32
	ldr	r0, .L3576+4
	bl	printk
	bl	dump_stack
.L3562:
	ldr	r9, .L3576+8
	movw	r10, #698
	ldr	r7, .L3576+12
	ldr	r3, [r9, #2804]
	mov	r8, r9
	ldrh	r2, [r3, r10]
	cmp	r6, r2
	bls	.L3563
	mov	r1, r6
	ldr	r0, .L3576+16
	bl	printk
	mov	r4, #0
	movw	r3, #474
	ldr	r0, [r4, #4]
	ldrh	r2, [r7, r3]
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [r9, #2804]
	ldrh	r3, [r3, r10]
	cmp	r3, r6
	bcs	.L3574
	ldr	r0, .L3576+4
	mov	r2, #37
	ldr	r1, .L3576
	b	.L3575
.L3563:
	add	r2, r6, #176
	mov	r5, r5, asl #3
	add	r9, r7, r5
	ldr	r4, [r3, r2, asl #2]
	ldr	r3, .L3576+20
	add	r2, r3, r5
	strh	r6, [r3, r5]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strh	r3, [r2, #2]	@ movhi
	bne	.L3566
	mov	r1, r6
	mov	r2, r4
	ldr	r0, .L3576+24
	bl	printk
	movw	r3, #474
	ldr	r0, [r9, #-2596]
	mov	r1, #255
	ldrh	r2, [r7, r3]
	bl	ftl_memset
	b	.L3574
.L3566:
	ldr	r2, [r7, #456]
	mov	r0, r4
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r1, [r9, #-2596]
	bl	ftl_read_ppa_page
	ldr	r2, [r7, #456]
	ldr	r2, [r2]
	cmp	r2, r6
	mov	r3, r0
	bne	.L3567
	cmn	r0, #1
	cmpne	r0, #512
	beq	.L3567
.L3571:
	ldr	r3, [r7, #456]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3574
	ldr	r0, .L3576+4
	mov	r2, #73
	ldr	r1, .L3576
	b	.L3575
.L3567:
	mov	r1, r6
	str	r4, [sp]
	ldr	r0, .L3576+28
	add	r5, r7, r5
	bl	printk
	ldr	r3, [r8, #2804]
	movw	ip, #698
	ldr	r0, .L3576+32
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldr	r0, .L3576+36
	mov	r2, #4
	ldr	r1, [r5, #-2596]
	mov	r3, r3, asl #7
	ldr	r9, .L3576+12
	bl	rknand_print_hex
	ldr	r0, .L3576+40
	ldr	r1, [r7, #456]
	mov	r2, #4
	mov	r3, #16
	bl	rknand_print_hex
	mov	r0, r4
	ldr	r1, [r5, #-2596]
	ldr	r2, [r7, #456]
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #512
	bne	.L3570
	movw	r3, #474
	ldr	r0, [r5, #-2596]
	mov	r1, #255
	ldrh	r2, [r9, r3]
	bl	ftl_memset
	b	.L3572
.L3570:
	ldr	r3, [r9, #456]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3571
	b	.L3572
.L3575:
	bl	printk
	bl	dump_stack
.L3574:
	mov	r0, #0
	b	.L3565
.L3572:
	mvn	r0, #0
.L3565:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3577:
	.align	2
.L3576:
	.word	.LANCHOR1+2552
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC234
	.word	.LANCHOR3-2600
	.word	.LC235
	.word	.LC236
	.word	.LC237
	.word	.LC216
	.word	.LC238
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
	ldr	r5, .L3597
	ldr	r4, [r5, #2804]
	add	r4, r4, #688
	ldrh	r2, [r4]
	ldr	r4, .L3597+4
	ldrh	r3, [r4, #-224]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L3579
	ldr	r3, [r4, #-232]
	cmp	r3, #0
	beq	.L3580
.L3579:
	bl	pm_free_sblk
	ldr	r2, [r5, #2804]
	add	r3, r2, #688
	ldrh	r1, [r3]
	ldrh	r3, [r4, #-224]
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3581
	ldr	r3, .L3597+4
	ldr	r3, [r3, #-232]
	cmp	r3, #0
	beq	.L3580
.L3581:
	uxth	r0, r0
	movw	r6, #65535
	add	r0, r0, #208
	mov	r3, #0
	str	r3, [r4, #-232]
	mov	r0, r0, asl #1
	ldrh	r7, [r2, r0]
	cmp	r7, r6
	bne	.L3583
	ldr	r1, .L3597+8
	mov	r2, #182
	ldr	r0, .L3597+12
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	r3, [r5, #2804]
	uxth	r0, r0
	add	r0, r0, #208
	mov	r0, r0, asl #1
	ldrh	r7, [r3, r0]
	cmp	r7, r6
	beq	.L3580
.L3583:
	bl	pm_select_ram_region
	ldr	r3, .L3597+16
	movw	r2, #65535
	mov	r6, r0, asl #3
	mov	r10, r0
	add	r8, r3, r6
	str	r3, [sp]
	ldrh	r0, [r3, r6]
	cmp	r0, r2
	beq	.L3584
	ldr	r3, .L3597+4
	add	r3, r3, r6
	ldr	r1, [r3, #-2596]
	cmp	r1, #0
	beq	.L3584
	ldrsh	r3, [r8, #2]
	cmp	r3, #0
	bge	.L3584
	bl	pm_write_page
	ldrh	r3, [r8, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r8, #2]	@ movhi
.L3584:
	ldr	fp, .L3597
	mov	r8, #0
	ldr	r9, .L3597+4
	add	r3, r9, r6
	str	r3, [sp, #4]
.L3585:
	ldr	r0, [r5, #2804]
	uxth	r9, r8
	add	r3, r0, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r3, r9
	bls	.L3596
	ldr	r3, .L3597+20
	ldrh	r1, [r3]
	add	r3, r9, #176
	ldr	ip, [r0, r3, asl #2]
	mvn	r0, #0
	ldrb	r3, [fp, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r1, r3
	mvn	r0, r0, asl r3
	and	r0, r0, ip, lsr r1
	ldrb	r1, [r4, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, r7
	bne	.L3586
	mov	r0, r9
	mov	r1, r10
	bl	load_l2p_region
	cmp	r0, #0
	bne	.L3587
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r3, #-2596]
	bl	pm_write_page
.L3587:
	ldr	r2, [sp]
	mvn	r3, #0
	strh	r3, [r2, r6]	@ movhi
.L3586:
	add	r8, r8, #1
	b	.L3585
.L3596:
	bl	pm_free_sblk
.L3580:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3598:
	.align	2
.L3597:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2568
	.word	.LC0
	.word	.LANCHOR3-2600
	.word	.LANCHOR3-3204
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
	ldr	r4, .L3605
	ldr	r3, .L3605+4
	add	r2, r4, r0
	add	r5, r3, r0
	ldr	r1, [r2, #-2596]
	ldrh	r0, [r3, r0]
	bl	pm_write_page
	ldrh	r3, [r5, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #-2604]
	cmp	r3, #0
	beq	.L3600
	bl	pm_gc
	mov	r3, #0
	str	r3, [r4, #-2604]
.L3600:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3606:
	.align	2
.L3605:
	.word	.LANCHOR3
	.word	.LANCHOR3-2600
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
	ldr	r5, .L3612
.L3609:
	add	r3, r5, r4, asl #3
	uxth	r0, r4
	ldrsh	r3, [r3, #2]
	cmp	r3, #0
	bge	.L3608
	bl	pm_flush_id
.L3608:
	add	r4, r4, #1
	cmp	r4, #32
	bne	.L3609
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3613:
	.align	2
.L3612:
	.word	.LANCHOR3-2600
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
	ldr	r4, .L3642
	.pad #20
	sub	sp, sp, #20
	mov	r0, #64
	mov	r5, #0
	mov	r3, #1
	ldr	r8, .L3642+4
	str	r5, [r4, #-232]
	mvn	r9, #0
	str	r5, [r4, #-2604]
	strb	r3, [r4, #460]
	bl	ftl_malloc
	ldr	r10, .L3642+8
	str	r0, [r4, #456]
.L3620:
	mov	r3, #0
	cmp	r7, r3
	mov	r6, r8
	strh	r9, [r6, r5]!	@ movhi
	strh	r3, [r6, #2]	@ movhi
	beq	.L3619
	ldrb	r0, [r10, #2772]	@ zero_extendqisi2
	mov	r0, r0, asl #9
	bl	ftl_malloc
	str	r0, [r6, #4]
.L3619:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L3620
	ldr	r5, .L3642+8
	ldr	r10, [r4, #-2596]
	ldr	r6, [r4, #456]
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
	bgt	.L3621
	add	r3, r3, #692
	ldr	r0, .L3642+12
	mov	fp, r5
	ldrh	r1, [r3]
	mov	r3, r7
	bl	printk
	ldr	r3, [r5, #2804]
	add	r7, r7, #1
	add	r3, r3, #696
	ldrh	r9, [r3]
.L3622:
	sxth	r3, r9
	cmp	r3, r7
	bge	.L3641
	ldr	r2, [r5, #2804]
	ldr	r0, .L3642+16
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
	ldr	r0, .L3642+20
	bl	printk
	ldr	ip, [sp, #12]
	cmp	ip, #512
	cmnne	ip, #1
	beq	.L3623
	ldr	r3, [fp, #2804]
	ldr	r2, [r6]
	add	r3, r3, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L3623
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L3624
	ldrb	r1, [fp, #2772]	@ zero_extendqisi2
	mov	r0, r10
	str	r3, [sp, #12]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3624
	ldr	r1, [r6, #8]
	ldr	r0, .L3642+24
	bl	printk
	b	.L3623
.L3624:
	ldr	r3, [r6]
	ldr	r2, [r5, #2804]
	add	r3, r3, #176
	str	r8, [r2, r3, asl #2]
.L3623:
	add	r9, r9, #1
	uxth	r9, r9
	b	.L3622
.L3641:
	mov	r3, #1
	strb	r3, [r4, #145]
	ldr	r3, [r5, #2804]
	add	r3, r3, #696
	strh	r7, [r3]	@ movhi
	bl	pm_free_sblk
.L3621:
	movw	r3, #474
	mov	r1, #255
	ldrh	r2, [r4, r3]
	ldr	r0, [r4, #-2596]
	bl	ftl_memset
	mvn	r0, #0
	ldr	r1, [r4, #-2596]
	bl	pm_write_page
	ldrb	r3, [r4, #145]	@ zero_extendqisi2
	ldr	r5, .L3642
	cmp	r3, #0
	beq	.L3626
	ldr	r1, [r5, #-2596]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r5, #-2596]
	mvn	r0, #0
	bl	pm_write_page
	mvn	r0, #0
	ldr	r1, [r5, #-2596]
	bl	pm_write_page
.L3626:
	bl	pm_free_sblk
	bl	pm_gc
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3643:
	.align	2
.L3642:
	.word	.LANCHOR3
	.word	.LANCHOR3-2600
	.word	.LANCHOR0
	.word	.LC239
	.word	.LANCHOR3-3142
	.word	.LC240
	.word	.LC241
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
	ldr	r9, .L3663
	mov	r10, r0
	mov	r8, r2
	ldrb	r4, [r9, #2772]	@ zero_extendqisi2
	mov	r1, r4, asl #7
	mov	r5, r4, asl #7
	bl	__aeabi_uidiv
	ldr	r2, [r9, #2784]
	cmp	r10, r2
	movcc	r3, #0
	ldrcc	r2, .L3663+4
	uxth	fp, r0
	mov	r7, r0
	smulbb	r5, r5, fp
	rsb	r5, r5, r10
	uxth	r5, r5
	bcc	.L3645
	mov	r1, r10
	ldr	r0, .L3663+8
	bl	printk
	mvn	r0, #0
	cmp	r8, #0
	streq	r0, [r6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3661:
	ldrh	r1, [r1, r2]
	cmp	r1, fp
	beq	.L3648
.L3647:
	add	r3, r3, #1
	cmp	r3, #32
	beq	.L3660
.L3645:
	mov	r1, r3, asl #3
	uxth	r4, r3
	add	r0, r2, r1
	ldr	r10, .L3663+4
	ldr	r0, [r0, #4]
	cmp	r0, #0
	bne	.L3661
	b	.L3647
.L3660:
	bl	pm_select_ram_region
	mov	r3, r0, asl #3
	mov	r4, r0
	add	r2, r10, r3
	ldrh	r1, [r10, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L3662
.L3651:
	ldr	r3, .L3663+12
	mov	r0, fp
	mov	r1, r4
	strb	r4, [r3, #476]
	bl	load_l2p_region
.L3648:
	cmp	r8, #0
	ldr	r1, .L3663+12
	mov	r4, r4, asl #3
	bne	.L3649
	add	r3, r1, r4
	mvn	r2, #0
	ldr	r3, [r3, #-2596]
	ldr	r3, [r3, r5, asl #2]
	str	r3, [r6]
	ldrb	r0, [r9, #1189]	@ zero_extendqisi2
	rsb	ip, r0, #24
	mvn	r2, r2, asl r0
	and	r3, r2, r3, lsr ip
	ldrb	r2, [r1, #-3140]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6]
	b	.L3650
.L3649:
	add	r3, r1, r4
	ldr	r2, [r6]
	ldr	r3, [r3, #-2596]
	str	r2, [r3, r5, asl #2]
	add	r2, r10, r4
	strb	r7, [r1, #-2344]
	ldrh	r3, [r2, #2]
	mvn	r3, r3, asl #17
	mvn	r3, r3, lsr #17
	strh	r3, [r2, #2]	@ movhi
.L3650:
	add	r4, r10, r4
	movw	r2, #32767
	mov	r0, #0
	ldrh	r3, [r4, #2]
	ubfx	r1, r3, #0, #15
	cmp	r1, r2
	addne	r3, r3, #1
	strneh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3662:
	ldrsh	r3, [r2, #2]
	cmp	r3, #0
	bge	.L3651
	bl	pm_flush_id
	b	.L3651
.L3664:
	.align	2
.L3663:
	.word	.LANCHOR0
	.word	.LANCHOR3-2600
	.word	.LC242
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
	ldr	r7, .L3765
	mov	r6, #0
	ldr	r8, .L3765+4
	.pad #68
	sub	sp, sp, #68
	ldr	r5, [r7, #1092]
	strb	r6, [r8, #-3187]
	strb	r6, [r8, #-254]
	ldrh	r4, [r5, #80]
	cmp	r4, r3
	beq	.L3666
	mvn	r3, #0
	mov	r0, #1
	strh	r3, [r5, #130]	@ movhi
	bl	buf_alloc
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	add	r3, r5, r3, asl #1
	ldrh	r1, [r3, #94]
	sub	r3, r8, #3136
	ldrh	r3, [r3, #-6]
	mul	r1, r3, r1
	ldrb	r3, [r8, #-3196]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r4, r0
	str	r1, [sp, #56]
	beq	.L3667
	ldrb	r2, [r7, #1196]	@ zero_extendqisi2
	cmp	r2, r6
	bne	.L3667
	ldrb	r2, [r7, #1197]	@ zero_extendqisi2
	cmp	r2, r6
	beq	.L3668
.L3667:
	ldr	r2, .L3765+8
	ldrh	r2, [r2, #-2]
	sub	r2, r2, #1
	add	r2, r2, r1
	orr	r3, r2, r3, asl #24
	b	.L3757
.L3668:
	cmp	r3, #3
	bne	.L3669
	sub	r3, r8, #3168
	ldrh	r3, [r3, #-4]
	sub	r3, r3, #1
	add	r3, r3, r1
	orr	r3, r3, #50331648
.L3757:
	str	r3, [r4, #24]
.L3669:
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3670
	ldr	r3, [r4, #12]
	ldr	r6, .L3765+12
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3671
.L3670:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L3765+16
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
	b	.L3761
.L3671:
	ldrb	r2, [r8, #477]	@ zero_extendqisi2
	ldr	r3, .L3765+4
	cmp	r2, #2
	sub	r10, r3, #3168
	bne	.L3673
	ldrb	r2, [r3, #-3196]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L3673
	movw	r2, #474
	ldr	r0, [r3, #-244]
	ldrh	r1, [r3, r2]
	ldrh	r2, [r10, #-2]
	ldrb	r3, [r3, #-3195]	@ zero_extendqisi2
	mov	ip, r1, lsr #2
	mov	r2, r2, asl #2
	add	r0, r0, ip, asl #2
	smulbb	r2, r3, r2
	rsb	r2, r1, r2
	ldr	r1, [r4, #4]
	uxth	r2, r2
	bl	ftl_memcpy
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3674
	ldr	r3, .L3765
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3675
.L3674:
	ldr	r3, [r4, #24]
	sub	r3, r3, #1
	b	.L3676
.L3675:
	ldr	r3, [sp, #56]
	ldrh	r2, [r10, #-4]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, #33554432
.L3676:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3677
	ldr	r3, [r4, #12]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3678
.L3677:
	mov	r0, r4
	bl	zbuf_free
.L3761:
	ldrh	r4, [r5, #80]
	mov	r6, #0
	ldr	r3, [r7, #1088]
	mov	r4, r4, asl #1
	strh	r6, [r3, r4]	@ movhi
	ldrh	r2, [r5, #80]
	ldr	r3, [r7, #1092]
	strh	r2, [r3, #130]	@ movhi
	b	.L3672
.L3678:
	movw	r3, #474
	ldr	r0, [r8, #-244]
	ldr	r1, [r4, #4]
	ldrh	r2, [r8, r3]
	b	.L3758
.L3673:
	ldrh	r2, [r10, #-2]
	ldrb	r3, [r8, #-3195]	@ zero_extendqisi2
	ldr	r0, [r8, #-244]
	ldr	r1, [r4, #4]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
.L3758:
	bl	ftl_memcpy
	ldrb	r3, [r8, #-3195]	@ zero_extendqisi2
	ldrh	r2, [r10, #-2]
	ldr	fp, .L3765+4
	mul	r2, r3, r2
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	sub	r6, fp, #3168
	cmp	r3, r2, asr #6
	mov	r2, r2, asl #2
	bge	.L3680
	ldr	r0, [fp, #-240]
	mov	r1, #0
	bl	ftl_memset
	ldrb	r9, [fp, #477]	@ zero_extendqisi2
	cmp	r9, #1
	movne	r9, #1
	movne	r6, #0
	bne	.L3681
	ldrh	r1, [r6, #-2]
	movw	r2, #474
	ldrb	r3, [fp, #-3195]	@ zero_extendqisi2
	ldrh	r6, [fp, r2]
	ldr	r0, [fp, #-240]
	mul	r3, r3, r1
	ldr	r1, [r4, #4]
	sub	r6, r6, r3, asl #2
	add	r1, r1, r3, asl #2
	mov	r2, r6
	bl	ftl_memcpy
	b	.L3681
.L3680:
	ldr	r1, [r4, #4]
	mov	r9, #0
	ldr	r0, [fp, #-240]
	mov	r6, r9
	add	r1, r1, r2
	bl	ftl_memcpy
.L3681:
	ldrh	r3, [r10, #-4]
	str	r3, [sp, #36]
	ldrb	r3, [r8, #-3196]	@ zero_extendqisi2
	cmp	r3, #2
	str	r3, [sp, #32]
	bne	.L3682
	ldr	r3, .L3765+4
	ldrb	r3, [r3, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3683
.L3682:
	ldrb	r3, [r7, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3684
.L3683:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	mul	r3, r3, r2
	str	r3, [sp, #36]
	mov	r3, #1
	str	r3, [sp, #32]
.L3684:
	ldr	r10, .L3765+4
	mov	r7, #0
	str	r7, [sp, #40]
.L3685:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bcs	.L3698
	add	ip, r5, #96
	add	r3, r7, r7, asl #1
	sub	r3, r3, #1
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #44]
.L3699:
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	ldr	r2, [sp, #44]
	cmp	r2, r3
	bge	.L3695
	ldr	r3, [sp, #40]
	mov	fp, #1
	mov	r8, r3, asl #2
.L3696:
	ldr	r3, [sp, #32]
	cmp	fp, r3
	bhi	.L3763
	ldr	r3, .L3765+20
	ldrh	r2, [ip]
	ldrb	r1, [r10, #-3196]	@ zero_extendqisi2
	ldrh	r3, [r3]
	cmp	r1, #2
	mul	r3, r3, r2
	str	r3, [sp, #56]
	beq	.L3686
	ldr	r2, .L3765
	ldrb	r2, [r2, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3687
.L3686:
	sub	r2, r7, #1
	add	r3, r2, r3
	add	r3, r3, fp
	orr	r3, r3, r1, asl #24
	b	.L3759
.L3687:
	cmp	r1, #3
	addne	r3, r3, r7
	bne	.L3759
	ldr	r2, .L3765
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r7
	ldrne	r2, [sp, #48]
	orreq	r3, r3, fp, asl #24
	addne	r3, r3, r2
	addne	r3, r3, fp
	orrne	r3, r3, #50331648
.L3759:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #52]
	bl	sblk_read_page
	cmp	r9, #0
	ldr	ip, [sp, #52]
	beq	.L3691
	ldr	r3, [r10, #-240]
	ldr	r2, [r3, r8]
	cmp	r2, #0
	ldreq	r2, [r4, #12]
	ldreq	r2, [r2, #8]
	streq	r2, [r3, r8]
.L3691:
	ldr	r3, [r10, #-244]
	ldr	r2, [r4, #12]
	ldr	r3, [r3, r8]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L3692
	ldr	r0, [r10, #-240]
	ldr	lr, [r0, r8]
	ldr	r0, [r2, #8]
	cmp	lr, r0
	beq	.L3722
.L3692:
	ldr	r0, [r10, #-240]
	str	ip, [sp, #52]
	ldr	r0, [r0, r8]
	str	r0, [sp]
	ldr	r0, [r2]
	stmib	sp, {r0, r1}
	ldr	r1, [r2, #8]
	ldr	r0, .L3765+24
	str	r1, [sp, #12]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	ldr	r1, [r4, #24]
	ldr	r2, [r4, #36]
	bl	printk
	ldr	r3, [r10, #-244]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	ldr	ip, [sp, #52]
	beq	.L3722
	mov	r0, r4
	bl	zbuf_free
	ldr	r2, .L3765
	ldrh	r3, [r5, #80]
	mov	r0, #0
	ldr	r1, [r2, #1088]
	mov	r3, r3, asl #1
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r5, #80]
	ldr	r3, [r2, #1092]
	strh	r1, [r3, #130]	@ movhi
	b	.L3672
.L3722:
	ldr	r3, [sp, #40]
	add	fp, fp, #1
	add	r8, r8, #4
	add	r3, r3, #1
	str	r3, [sp, #40]
	b	.L3696
.L3763:
	ldr	r3, [sp, #44]
	add	ip, ip, #2
	add	r3, r3, #1
	str	r3, [sp, #44]
	b	.L3699
.L3695:
	ldrb	r3, [r10, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r7, r7, #1
	add	r7, r7, #1
	b	.L3685
.L3698:
	mov	r0, r4
	mov	r6, #0
	bl	zbuf_free
	ldr	ip, .L3765+8
	ldrb	r8, [r5, #89]	@ zero_extendqisi2
	mov	r7, r6
	ldr	r3, .L3765
	add	r10, ip, #3168
	ldrh	r9, [ip, #-2]
	ldrh	r4, [r5, #80]
	ldr	r3, [r3, #1088]
	smulbb	r8, r8, r9
	mov	r4, r4, asl #1
	mov	r9, r6
	strh	r8, [r3, r4]	@ movhi
.L3700:
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bcs	.L3714
	add	r2, r9, r9, asl #1
	sub	r3, r2, #1
	str	r3, [sp, #48]
	add	r3, r5, #96
	str	r3, [sp, #44]
	mov	r3, #0
	str	r3, [sp, #40]
.L3715:
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r3, r2
	bge	.L3711
	mov	r4, #1
.L3712:
	ldr	r3, [sp, #32]
	cmp	r4, r3
	bhi	.L3764
	ldr	r2, [r10, #-244]
	ldr	r8, [r2, r7, asl #2]
	cmn	r8, #1
	beq	.L3701
	ldr	r2, [r10, #-240]
	mov	r0, r8
	ldr	fp, [r2, r7, asl #2]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #60]
	bne	.L3702
	mov	r0, r8
	add	r1, sp, #60
	mov	r2, #0
	bl	pm_log2phys
.L3702:
	ldr	r3, [sp, #44]
	ldrh	r2, [r3]
	ldr	r3, .L3765+20
	ldrh	r1, [r3]
	ldr	r3, .L3765+4
	mul	r2, r1, r2
	ldrb	r1, [r3, #-3196]	@ zero_extendqisi2
	cmp	r1, #2
	beq	.L3703
	ldr	r0, .L3765
	ldrb	lr, [r0, #1196]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L3704
.L3703:
	sub	r0, r9, #1
	add	r2, r0, r2
	add	r2, r2, r4
	orr	r2, r2, r1, asl #24
	b	.L3760
.L3704:
	cmp	r1, #3
	addne	r2, r2, r9
	bne	.L3760
	ldrb	r1, [r0, #1197]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, r4, asl #24
	addeq	r2, r2, r9
	ldrne	r3, [sp, #48]
	addne	r2, r2, r3
	addne	r2, r2, r4
	orr	r2, r1, r2
.L3760:
	str	r2, [sp, #56]
	ldr	r2, .L3765+28
	ldrh	r0, [r2]
	ldr	r2, .L3765
	ldrb	r1, [r2, #1189]	@ zero_extendqisi2
	mov	r2, #1
	str	r2, [sp, #52]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r2, asl r1
	sub	r1, r1, #1
	and	r0, r1, fp, lsr r0
	ldrb	r1, [r10, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r1, [sp, #60]
	ldr	r2, [sp, #52]
	cmp	r1, fp
	uxth	fp, r0
	bne	.L3708
	mov	r0, r8
	add	r1, sp, #56
	bl	pm_log2phys
	mov	r0, fp
	add	r6, r6, #1
	bl	ftl_vpn_decrement
	b	.L3709
.L3708:
	ldr	r2, [sp, #56]
	cmp	r1, r2
	addeq	r6, r6, #1
.L3709:
	ldr	r1, .L3765
	add	r7, r7, #1
	ldr	r2, [r1, #1080]
	add	r2, r2, fp, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r2, r2, #224
	bne	.L3701
	ldr	r8, [r1, #1088]
	mov	fp, fp, asl #1
	ldrh	r1, [r8, fp]
	cmp	r1, #0
	strneh	r2, [r8, fp]	@ movhi
.L3701:
	add	r4, r4, #1
	b	.L3712
.L3764:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	ldr	r3, [sp, #44]
	add	r3, r3, #2
	str	r3, [sp, #44]
	b	.L3715
.L3711:
	ldrb	r2, [r10, #-3194]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r9, r9, #1
	add	r9, r9, #1
	b	.L3700
.L3714:
	ldr	r2, .L3765
	ldrh	r3, [r5, #80]
	ldr	r2, [r2, #1088]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
.L3672:
	ldr	r4, .L3765
	ldrh	r0, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3716
	bl	ftl_dump_write_open_sblk
.L3716:
	mov	r2, r6
	ldrh	r1, [r5, #80]
	ldr	r0, .L3765+32
	bl	printk
	mvn	r3, #0
	strh	r3, [r5, #80]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #1092]
	movw	r2, #65535
	ldrh	r3, [r3, #130]
	cmp	r3, r2
	beq	.L3717
	ldr	r1, .L3765
	movw	r2, #1076
	ldrh	r2, [r1, r2]
	cmp	r2, r3
	bhi	.L3718
	ldr	r1, .L3765+36
	movw	r2, #517
	ldr	r0, .L3765+40
	bl	printk
	bl	dump_stack
.L3718:
	ldr	r3, [r4, #1092]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
.L3717:
	ldr	r2, [r4, #2804]
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, #126]	@ movhi
	ldr	r2, [r4, #1092]
	strh	r3, [r2, #130]	@ movhi
	bl	ftl_info_flush
	b	.L3665
.L3666:
	ldrh	r3, [r5, #130]
	cmp	r3, r4
	beq	.L3665
	ldr	r2, [r7, #2804]
	ldrh	r2, [r2, #126]
	cmp	r2, r3
	bne	.L3720
	bl	pm_flush
	ldr	r3, [r7, #1092]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
	ldr	r3, [r7, #2804]
	mvn	r2, #0
	mov	r0, r6
	strh	r2, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L3720:
	ldr	r3, [r7, #1092]
	mvn	r2, #0
	strh	r2, [r3, #130]	@ movhi
.L3665:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3766:
	.align	2
.L3765:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3168
	.word	-178307901
	.word	.LC243
	.word	.LANCHOR3-3142
	.word	.LC244
	.word	.LANCHOR3-3204
	.word	.LC245
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
	ldr	r4, .L3803
	ldr	r3, .L3803+4
	ldr	r9, .L3803+8
	ldr	r5, [r4, #1092]
	ldrh	r3, [r3, #-2]
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	mul	r3, r2, r3
	str	r3, [sp]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3768
	ldr	r0, .L3803+12
	ldrh	r1, [r5, #80]
	bl	printk
.L3768:
	ldrh	r3, [r5, #80]
	mov	r8, #0
	ldr	r6, .L3803+16
	mov	r7, r8
	ldr	r1, [r4, #1088]
	mov	r3, r3, asl #1
	ldr	r0, [sp]
	ldrb	r2, [r6, #477]	@ zero_extendqisi2
	rsb	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
.L3769:
	ldr	r3, [sp]
	cmp	r7, r3
	beq	.L3802
	ldr	r3, [r6, #-244]
	mov	r10, r7, asl #2
	ldr	fp, [r3, r7, asl #2]
	cmn	fp, #1
	beq	.L3770
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #12]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3771
	ldr	r0, .L3803+20
	mov	r2, fp
	ldr	r1, [sp, #12]
	mov	r3, r7
	bl	printk
.L3771:
	str	r7, [sp, #8]
.L3777:
	ldr	r3, [r6, #-244]
	ldr	fp, [r3, r10]
	cmn	fp, #1
	beq	.L3772
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	uxth	r0, r0
	cmp	r0, r3
	bne	.L3772
	ldr	r3, [r6, #-240]
	mov	r0, fp
	ldr	r3, [r3, r10]
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3773
	mov	r0, fp
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3773:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L3774
	ldr	r3, .L3803+16
	add	r1, sp, #24
	mov	r0, fp
	mov	r2, #1
	add	r8, r8, #1
	ldr	r3, [r3, #-236]
	ldr	r3, [r3, r10]
	str	r3, [r1, #-8]!
	bl	pm_log2phys
	ldr	r3, .L3803+24
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	ldr	r2, [sp, #4]
	mov	fp, r2, lsr r0
	mvn	r0, #0
	bic	r0, fp, r0, asl r3
	ldr	r3, .L3803+16
	ldrb	r1, [r3, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	b	.L3801
.L3774:
	ldr	r2, [r9]
	tst	r2, #256
	beq	.L3776
	ldr	r0, .L3803+28
	mov	r1, fp
	ldr	r2, [sp, #4]
	bl	printk
.L3776:
	ldrh	r0, [r5, #80]
.L3801:
	bl	ftl_vpn_decrement
	ldr	r3, [r6, #-244]
	mvn	r2, #0
	str	r2, [r3, r10]
.L3772:
	ldr	r3, [sp, #8]
	add	r10, r10, #4
	ldr	r2, [sp]
	add	r3, r3, #1
	str	r3, [sp, #8]
	cmp	r3, r2
	bne	.L3777
.L3770:
	add	r7, r7, #1
	b	.L3769
.L3802:
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3779
	ldrh	r3, [r5, #80]
	ldr	r1, [r4, #1092]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldr	r0, .L3803+32
	ldrh	r1, [r1, #80]
	ldrh	r2, [r2, r3]
	mov	r3, r8
	bl	printk
.L3779:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, r8
	beq	.L3780
	ldr	r1, .L3803+36
	movw	r2, #898
	ldr	r0, .L3803+40
	bl	printk
	bl	dump_stack
.L3780:
	ldrh	r3, [r5, #80]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r8, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3804:
	.align	2
.L3803:
	.word	.LANCHOR0
	.word	.LANCHOR3-3168
	.word	.LANCHOR2
	.word	.LC246
	.word	.LANCHOR3
	.word	.LC247
	.word	.LANCHOR3-3204
	.word	.LC248
	.word	.LC249
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
	ldr	r6, .L3834
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3834+4
	mov	lr, r1
	ldr	r5, .L3834+8
	movw	r0, #65535
	ldrb	r3, [r6, #2832]	@ zero_extendqisi2
	ldrb	ip, [r5, #-3195]	@ zero_extendqisi2
	add	r2, r4, r3, asl #1
	ldrh	r7, [r2, #36]
	ldrh	r2, [r4, #2]
.L3806:
	cmp	r7, r0
	bne	.L3832
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
	b	.L3806
.L3832:
	mov	r0, #1
	cmp	lr, #0
	strneh	r2, [r4, #2]	@ movhi
	cmp	r1, #0
	strneb	r3, [r6, #2832]
	bl	buf_alloc
	ldr	r8, .L3834
	mov	r9, #1
	ldr	r10, .L3834+12
	mov	fp, r0
.L3811:
	ldrb	r3, [r6, #2834]	@ zero_extendqisi2
	ldr	r2, .L3834
	cmp	r9, r3
	bgt	.L3833
	ldrh	r2, [r10]
	cmp	r3, #2
	mul	r2, r2, r7
	bne	.L3812
	ldrh	r3, [r4, #2]
	add	r3, r2, r3, asl #1
	ldr	r2, .L3834+8
	sub	r3, r3, #1
	add	r3, r3, r9
	ldrb	r2, [r2, #-3196]	@ zero_extendqisi2
	b	.L3830
.L3812:
	cmp	r3, #3
	ldrneh	r1, [r4, #2]
	addne	r2, r2, r1
	bne	.L3831
	ldrb	r3, [r8, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3815
	ldrb	r3, [r8, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3816
.L3815:
	ldrh	r3, [r4, #2]
	add	r3, r3, r3, asl #1
	sub	r3, r3, #1
	add	r3, r3, r2
	ldrb	r2, [r5, #-3196]	@ zero_extendqisi2
	add	r3, r3, r9
.L3830:
	orr	r3, r3, r2, asl #24
	str	r3, [fp, #24]
	b	.L3813
.L3816:
	ldrh	r1, [r4, #2]
	add	r2, r2, r1
	orr	r2, r2, r9, asl #24
.L3831:
	str	r2, [fp, #24]
.L3813:
	mov	r0, fp
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [fp, #36]
	cmp	r3, #512
	cmnne	r3, #1
	beq	.L3818
	ldr	r3, [fp, #12]
	ldr	r3, [r3, #4]
	mov	r0, r3
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #12]
	ldr	r3, [sp, #4]
	bne	.L3819
	ldr	r2, [r6, #2784]
	cmp	r3, r2
	bcs	.L3819
	mov	r0, r3
	add	r1, sp, #12
	mov	r2, #0
	bl	pm_log2phys
.L3819:
	ldr	r3, [fp, #24]
	ldr	r2, [sp, #12]
	cmp	r3, r2
	ldreqh	r1, [r4, #20]
	ldreq	r2, [r5, #-3200]
	streq	r3, [r2, r1, asl #2]
	ldreqh	r3, [r4, #20]
	addeq	r3, r3, #1
	streqh	r3, [r4, #20]	@ movhi
.L3818:
	ldrh	r3, [r4, #22]
	add	r9, r9, #1
	add	r3, r3, #1
	strh	r3, [r4, #22]	@ movhi
	b	.L3811
.L3833:
	mov	r0, fp
	str	r2, [sp, #4]
	bl	zbuf_free
	ldrb	r1, [r5, #-3195]	@ zero_extendqisi2
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
.L3835:
	.align	2
.L3834:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR3-3142
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
	ldr	r4, .L3888
	.pad #28
	sub	sp, sp, #28
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3837
	ldr	r1, .L3888+4
	mov	r2, #1504
	ldr	r0, .L3888+8
	bl	printk
	bl	dump_stack
.L3837:
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	moveq	r0, #0
	beq	.L3838
	ldr	r3, .L3888+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3839
	ldr	r3, .L3888
	ldr	r0, .L3888+16
	ldr	r2, [r3, #1088]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	bl	printk
.L3839:
	ldr	r6, .L3888
	movw	r7, #2828
	bl	timer_get_time
	ldrh	r0, [r4, r7]
	add	r1, r6, #2864
	ldr	r5, .L3888+20
	bl	ftl_get_blk_list_in_sblk
	uxtb	r0, r0
	strb	r0, [r4, #2833]
	cmp	r0, #0
	mvneq	r3, #0
	streqh	r3, [r6, r7]	@ movhi
	beq	.L3838
	ldrh	r3, [r6, r7]
	ldr	r2, [r6, #1080]
	add	r2, r2, r3, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r1, r2, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r1, #224
	orreq	r2, r2, #1
	cmp	r2, #0
	bne	.L3841
	ldr	r2, [r6, #1092]
	ldrh	ip, [r2, #16]
	cmp	ip, r3
	beq	.L3841
	ldrh	ip, [r2, #48]
	cmp	ip, r3
	beq	.L3841
	ldrh	r2, [r2, #80]
	cmp	r2, r3
	bne	.L3842
.L3841:
	cmp	r1, #0
	bne	.L3843
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3844
	ldr	r1, .L3888+4
	movw	r2, #1529
	ldr	r0, .L3888+8
	bl	printk
	bl	dump_stack
.L3844:
	movw	r3, #2828
	ldr	r2, [r4, #1088]
	ldrh	r3, [r4, r3]
	mov	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L3843:
	movw	r3, #2828
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	strh	r0, [r5, #20]	@ movhi
	b	.L3838
.L3842:
	add	r0, r5, r0, asl #1
	movw	r3, #65535
	ldrh	r10, [r0, #34]
	cmp	r10, r3
	bne	.L3845
	ldr	r1, .L3888+4
	movw	r2, #1539
	ldr	r0, .L3888+8
	bl	printk
	bl	dump_stack
.L3845:
	movw	r2, #2828
	ldr	r6, .L3888+24
	ldrh	r1, [r4, r2]
	ldr	r2, [r4, #1080]
	sub	r9, r6, #3168
	sub	r3, r6, #3136
	str	r6, [sp, #4]
	add	r2, r2, r1, asl #2
	ldrh	r7, [r9, #-4]
	ldrh	r3, [r3, #-6]
	ldrb	r8, [r2, #2]	@ zero_extendqisi2
	sub	r7, r7, #1
	and	r8, r8, #224
	uxth	r7, r7
	cmp	r8, #160
	mul	r3, r3, r10
	movne	r8, #1
	bne	.L3846
	ldrb	r2, [r6, #-3196]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r8, r2
	orr	r3, r3, r2, asl #24
	ldreqh	r7, [r9, #-2]
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	subeq	r7, r7, #1
	uxtheq	r7, r7
	cmp	r2, #0
	bne	.L3848
	ldr	r2, .L3888
	ldrb	r2, [r2, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3846
.L3848:
	ldrh	r7, [r9, #-2]
	sub	r7, r7, #1
	uxth	r7, r7
.L3846:
	mov	r0, #1
	mov	fp, #0
	orr	r7, r7, r3
	strb	r8, [r4, #2834]
	strh	fp, [r5, #2]	@ movhi
	strb	fp, [r4, #2832]
	strh	fp, [r5, #22]	@ movhi
	strh	fp, [r5, #24]	@ movhi
	bl	buf_alloc
	mov	r1, #1
	mov	r6, r0
	str	r7, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r6, #36]
	strh	fp, [r5, #20]	@ movhi
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3885
	ldr	r3, [r6, #12]
	ldr	lr, .L3888+28
	ldr	r2, [r3]
	str	lr, [sp, #12]
	cmp	r2, lr
	beq	.L3850
	ldr	r0, .L3888+8
	movw	r2, #1577
	ldr	r1, .L3888+4
.L3886:
	bl	printk
	bl	dump_stack
.L3885:
	mov	r0, r6
	bl	zbuf_free
	b	.L3883
.L3850:
	ldr	r1, [sp, #4]
	ldrh	r0, [r9, #-4]
	ldr	r2, .L3888+24
	ldrb	fp, [r1, #-3195]	@ zero_extendqisi2
	sub	r1, r2, #3168
	smulbb	r0, r0, fp
	smulbb	r0, r0, r8
	uxth	r0, r0
	str	r0, [sp, #8]
	ldr	r0, [sp, #4]
	ldrb	r0, [r0, #477]	@ zero_extendqisi2
	cmp	r8, #3
	cmpeq	r0, #2
	bne	.L3851
	ldrh	r8, [r1, #-2]
	movw	r3, #474
	ldrh	r0, [r2, r3]
	ldr	r3, [r2, #-3200]
	mov	r8, r8, asl #2
	ldr	r1, [r6, #4]
	smulbb	r8, r8, fp
	rsb	r8, r0, r8
	mov	r0, r0, lsr #2
	uxth	r8, r8
	add	r0, r3, r0, asl #2
	mov	r2, r8
	bl	ftl_memcpy
	ldr	r3, [r6, #12]
	ldr	fp, [r3, #4]
	cmp	fp, #0
	beq	.L3852
	ldr	r0, [r6, #4]
	mov	r1, r8
	bl	js_hash
	cmp	fp, r0
	beq	.L3852
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, [r6, #12]
	ldr	r0, .L3888+32
	ldr	r2, [r6, #24]
	ldr	r1, [r3, #4]
	mov	r3, r8
	b	.L3884
.L3852:
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3853
	ldr	r3, .L3888
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3854
.L3853:
	sub	r7, r7, #1
	b	.L3855
.L3854:
	ldr	r3, .L3888+36
	ldrh	r7, [r9, #-4]
	ldrh	r3, [r3, #-6]
	sub	r7, r7, #1
	uxth	r7, r7
	mul	r10, r3, r10
	orr	r7, r7, #33554432
	orr	r7, r7, r10
.L3855:
	str	r7, [r6, #24]
	mov	r0, r6
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r6, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3885
	ldr	r3, [r6, #12]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r3, r2
	ldrne	r0, .L3888+8
	movwne	r2, #1618
	ldrne	r1, .L3888+4
	bne	.L3886
.L3857:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r1, [r6, #4]
	ldr	r0, [r3, #-3200]
	movw	r3, #474
	ldrh	r2, [r2, r3]
	b	.L3882
.L3851:
	ldr	r8, [r3, #4]
	ldr	r3, [sp, #8]
	ldr	r0, [r6, #4]
	str	r2, [sp, #12]
	mov	r7, r3, asl #2
	mov	r1, r7
	bl	js_hash
	ldr	r2, [sp, #12]
	cmp	r8, r0
	ldreq	r1, [r6, #4]
	ldreq	r0, [r2, #-3200]
	moveq	r2, r7
	beq	.L3882
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, [r6, #12]
	ldr	r0, .L3888+32
	ldr	r2, [r6, #24]
	ldr	r1, [r3, #4]
	mov	r3, r7
.L3884:
	bl	printk
.L3883:
	mvn	r0, #0
	b	.L3838
.L3882:
	bl	ftl_memcpy
	ldr	r10, .L3888+24
	ldr	fp, .L3888+40
	mov	r8, #0
	ldr	r9, .L3888+20
	ldr	r3, [sp, #4]
	ldr	r7, [r3, #-3200]
	sub	r7, r7, #4
.L3860:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	bge	.L3887
	ldr	r0, [r7, #4]!
	cmn	r0, #1
	beq	.L3862
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3863
	ldr	r0, [r7]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3863:
	ldr	r2, [sp, #20]
	mvn	r1, #0
	ldrb	r3, [r4, #1189]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r3, r3, #24
	str	r2, [sp, #12]
	rsb	r3, r0, r3
	mov	r0, r2, lsr r0
	bic	r0, r0, r1, asl r3
	ldr	r3, [sp, #4]
	ldrb	r1, [r3, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrh	r3, [r5]
	cmp	r0, r3
	ldr	r2, [sp, #12]
	ldreq	r3, [r10, #-3200]
	ldreqh	r1, [r9, #20]
	streq	r2, [r3, r1, asl #2]
	ldreqh	r3, [r9, #20]
	addeq	r3, r3, #1
	streqh	r3, [r9, #20]	@ movhi
.L3862:
	bl	timer_get_time
	add	r8, r8, #1
	b	.L3860
.L3887:
	mov	r0, r6
	bl	zbuf_free
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L3866
	ldr	r0, .L3888+44
	bl	printk
.L3866:
	movw	r3, #2828
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldrh	r0, [r5, #20]
	strh	r3, [r5, #24]	@ movhi
.L3838:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3889:
	.align	2
.L3888:
	.word	.LANCHOR0
	.word	.LANCHOR1+2612
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC250
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	-178307901
	.word	.LC251
	.word	.LANCHOR3-3136
	.word	.LANCHOR3-3204
	.word	.LC252
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
	ldr	r4, .L3907
	ldr	r3, [r4, #1092]
	ldr	r2, [r3, #544]
	cmn	r2, #1
	beq	.L3891
	mov	r5, #0
	mov	r7, r4
.L3892:
	ldr	r3, [r4, #1092]
	add	r1, sp, #4
	mov	r2, #0
	uxth	r6, r5
	ldr	r0, [r3, #544]
	bl	pm_log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3893
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #4]
	mov	r8, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmp	r3, #256
	bne	.L3894
	ldr	r3, .L3907+4
	mov	r9, #1
	ldr	r0, [sp, #4]
	ldrh	r2, [r3]
	ldrb	r3, [r7, #1189]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r3, r9, asl r3
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	ldr	r3, .L3907+8
	ldrb	r1, [r3, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r1, r9
	mov	r2, #0
	uxth	r0, r0
	bl	gc_add_sblk
.L3894:
	ldr	r2, [r8, #12]
	ldr	r3, [r4, #1092]
	ldr	r2, [r2, #4]
	ldr	r3, [r3, #544]
	cmp	r2, r3
	beq	.L3895
	ldr	r1, .L3907+12
	movw	r2, #2162
	ldr	r0, .L3907+16
	bl	printk
	bl	dump_stack
.L3895:
	mov	r0, r8
	bl	zbuf_free
.L3893:
	ldr	r3, [r4, #1092]
	ldr	r1, [r4, #2784]
	ldr	r2, [r3, #544]
	add	r2, r2, #1
	str	r2, [r3, #544]
	cmp	r2, r1
	bcc	.L3896
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
	b	.L3890
.L3896:
	ldr	r3, [sp, #4]
	cmn	r3, #1
	bne	.L3890
	ldr	r3, .L3907+20
	add	r5, r5, #1
	ldrh	r3, [r3]
	cmp	r6, r3, lsr #2
	bcc	.L3892
	b	.L3890
.L3891:
	ldr	r2, [r3, #536]
	ldr	r1, [r3, #12]
	add	r2, r2, #12910592
	add	r2, r2, #49408
	cmp	r1, r2
	bhi	.L3901
	ldr	r0, [r4, #2804]
	ldr	r2, [r3, #540]
	ldr	r0, [r0, #44]
	add	r2, r2, #4992
	add	r2, r2, #8
	cmp	r0, r2
	bls	.L3890
.L3901:
	ldr	r2, [r4, #2804]
	ldr	r2, [r2, #44]
	str	r1, [r3, #536]
	str	r2, [r3, #540]
	mov	r2, #0
	str	r2, [r3, #544]
.L3890:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3908:
	.align	2
.L3907:
	.word	.LANCHOR0
	.word	.LANCHOR3-3204
	.word	.LANCHOR3
	.word	.LANCHOR1+2628
	.word	.LC0
	.word	.LANCHOR3+474
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
	ldr	r5, .L3947
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r5, #1092]
	ldrh	r3, [r5, r4]
	ldr	r2, [r2, #608]
	cmp	r2, r3
	bcs	.L3909
	bl	timer_get_time
	ldr	r6, [r5, #1092]
	ldr	r3, [r6, #604]
	add	r3, r3, #29952
	add	r3, r3, #48
	cmp	r0, r3
	bls	.L3909
	bl	timer_get_time
	ldr	r3, [r5, #1092]
	ldrh	r1, [r5, r4]
	str	r0, [r6, #604]
	ldr	r2, [r3, #600]
	cmp	r2, r1
	bcs	.L3913
	ldr	r1, [r5, #2804]
	ldrh	r1, [r1, #134]
	cmp	r2, r1
	bcs	.L3914
.L3913:
	ldr	r2, [r5, #2804]
	ldrh	r2, [r2, #134]
	str	r2, [r3, #600]
.L3914:
	ldr	r4, [r3, #600]
	movw	r3, #65535
	uxth	r4, r4
	cmp	r4, r3
	bne	.L3915
	ldr	r1, .L3947+4
	movw	r2, #2503
	ldr	r0, .L3947+8
	bl	printk
	bl	dump_stack
.L3915:
	ldr	r3, [r5, #1092]
	mov	r0, r4
	add	r1, sp, #24
	ldr	r6, .L3947
	ldr	r2, [r3, #600]
	add	r2, r2, #1
	str	r2, [r3, #600]
	ldr	r2, [r3, #608]
	add	r2, r2, #1
	str	r2, [r3, #608]
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	beq	.L3909
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
	bne	.L3916
	ldr	r3, [r6, #1092]
	ldrh	r1, [r3, #16]
	cmp	r1, r4
	beq	.L3916
	ldrh	r1, [r3, #48]
	cmp	r1, r4
	beq	.L3916
	ldrh	r3, [r3, #80]
	cmp	r3, r4
	bne	.L3917
.L3916:
	cmp	r2, #0
	bne	.L3909
	ldr	r3, [r5, #1088]
	mov	r4, r4, asl #1
	ldrh	r3, [r3, r4]
	cmp	r3, #0
	beq	.L3918
	ldr	r1, .L3947+4
	movw	r2, #2520
	ldr	r0, .L3947+8
	bl	printk
	bl	dump_stack
.L3918:
	ldr	r3, [r5, #1088]
	mov	r2, #0
	strh	r2, [r3, r4]	@ movhi
	b	.L3909
.L3917:
	add	r3, sp, #40
	add	r0, r3, r0, asl #1
	movw	r3, #65535
	ldrh	fp, [r0, #-18]
	cmp	fp, r3
	bne	.L3920
	ldr	r1, .L3947+4
	mov	r2, #2528
	ldr	r0, .L3947+8
	bl	printk
	bl	dump_stack
.L3920:
	ldr	r6, .L3947+12
	sub	r10, r6, #3168
	ldrh	r3, [r10, #-4]
	sub	r7, r3, #1
	sub	r3, r6, #3136
	uxth	r8, r7
	ldrb	r7, [r9, #2]	@ zero_extendqisi2
	ldrh	r2, [r3, #-6]
	and	r7, r7, #224
	cmp	r7, #160
	mul	r2, r2, fp
	movne	fp, #1
	bne	.L3921
	ldrb	r3, [r6, #-3196]	@ zero_extendqisi2
	cmp	r3, #2
	uxth	fp, r3
	orr	r2, r2, r3, asl #24
	ldreqh	r8, [r10, #-2]
	subeq	r8, r8, #1
	uxtheq	r8, r8
.L3921:
	mov	r0, #1
	orr	r7, r8, r2
	bl	buf_alloc
	mov	r1, #1
	str	r7, [r0, #24]
	mov	r8, r0
	bl	sblk_read_page
	ldrh	r2, [r10, #-2]
	ldrb	r3, [r6, #-3195]	@ zero_extendqisi2
	mov	r1, #255
	ldr	r7, .L3947+12
	ldr	r0, [r6, #-3200]
	mul	r2, r3, r2
	sub	ip, r7, #3168
	mov	r2, r2, asl #2
	str	ip, [sp, #12]
	bl	ftl_memset
	ldr	r2, [r8, #36]
	cmn	r2, #1
	cmpne	r2, #512
	moveq	r2, #1
	movne	r2, #0
	moveq	r0, r4
	moveq	r1, #1
	moveq	r2, #0
	ldr	ip, [sp, #12]
	beq	.L3945
	ldr	r3, [r8, #12]
	ldr	r1, [r3]
	ldr	r3, .L3947+16
	cmp	r1, r3
	beq	.L3923
	mov	r0, r4
	mov	r1, #1
.L3945:
	bl	gc_add_sblk
	mov	r0, r8
	bl	zbuf_free
	b	.L3909
.L3923:
	ldrb	r1, [r7, #-3195]	@ zero_extendqisi2
	mov	r7, r2
	ldrh	r3, [ip, #-4]
	ldr	r10, [r8, #4]
	smulbb	r3, r1, r3
	smulbb	fp, r3, fp
	uxth	r3, fp
	mov	fp, r2
	str	r3, [sp, #12]
.L3924:
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bge	.L3946
	ldr	r0, [r10, fp, asl #2]
	cmn	r0, #1
	beq	.L3925
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3926
	ldr	r0, [r10, fp, asl #2]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3926:
	ldr	r3, .L3947+20
	ldrb	r2, [r5, #1189]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	ldrh	r1, [r3]
	rsb	r2, r2, #24
	mov	r3, #1
	rsb	r2, r1, r2
	mov	r2, r3, asl r2
	sub	r2, r2, #1
	and	r0, r2, r0, lsr r1
	ldrb	r1, [r6, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	cmp	r0, r4
	addeq	r7, r7, #1
	uxtheq	r7, r7
.L3925:
	add	fp, fp, #1
	b	.L3924
.L3946:
	mov	r0, r8
	mov	r6, r4, asl #1
	bl	zbuf_free
	ldr	r3, .L3947+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3928
	ldr	r3, [r5, #1088]
	mov	r1, r4
	ldr	r0, .L3947+28
	ldrh	r2, [r3, r6]
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r7
	bl	printk
.L3928:
	ldr	r3, [r5, #1088]
	cmp	r7, #31
	strh	r7, [r3, r6]	@ movhi
	bhi	.L3909
	mov	r0, r4
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L3909:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3948:
	.align	2
.L3947:
	.word	.LANCHOR0
	.word	.LANCHOR1+2648
	.word	.LC0
	.word	.LANCHOR3
	.word	-178307901
	.word	.LANCHOR3-3204
	.word	.LANCHOR2
	.word	.LC253
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
	ldr	r6, .L3987
	.pad #132
	sub	sp, sp, #132
	mov	r7, r0
	str	r1, [sp, #56]
	ldr	r2, [r6, #1080]
	add	r3, r2, fp
	ldrh	r1, [r2, fp]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r10, [r2, r0, asl #2]
	mov	r1, r7
	mov	r2, r3, lsr #5
	ldr	r0, .L3987+4
	ubfx	r3, r3, #3, #2
	ubfx	r4, r10, #11, #8
	str	r4, [sp, #4]
	bl	printk
	movw	r3, #65535
	cmp	r7, r3
	beq	.L3972
	movw	r3, #1076
	ldrh	r3, [r6, r3]
	cmp	r3, r7
	bls	.L3972
	ldr	r3, [r6, #1080]
	add	r1, sp, #112
	mov	r0, r7
	strh	r7, [sp, #96]	@ movhi
	add	r3, r3, fp
	ldr	r4, .L3987+8
	mov	r5, #0
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r9, r5
	and	r3, r3, #224
	cmp	r3, #160
	ldreq	r3, .L3987+12
	movne	r10, #1
	ldreqb	r10, [r3, #-3196]	@ zero_extendqisi2
	bl	ftl_get_blk_list_in_sblk
	mov	r2, r4
	mov	r1, r7
	ldrh	r3, [r2, #-4]!
	uxtb	r0, r0
	strh	r5, [sp, #98]	@ movhi
	strb	r0, [sp, #105]
	smulbb	r3, r3, r0
	strb	r5, [sp, #101]
	str	r2, [sp, #60]
	strh	r5, [sp, #106]	@ movhi
	strh	r3, [sp, #102]	@ movhi
	ldr	r3, [r6, #1080]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	mov	r3, r10
	str	r0, [sp]
	ldr	r0, .L3987+16
	mov	r2, r2, lsr #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	add	r3, r4, #3168
	mov	r6, r0
	str	r5, [sp, #40]
	str	r5, [sp, #48]
	str	r3, [sp, #68]
.L3952:
	ldr	r3, [sp, #60]
	uxth	r2, r5
	str	r2, [sp, #44]
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L3969
	mov	r3, r2, asl #1
	add	r2, r3, r2
	sub	r3, r3, #1
	str	r3, [sp, #72]
	mov	r3, #0
	sub	r2, r2, #1
	str	r2, [sp, #64]
.L3985:
	str	r3, [sp, #52]
	ldrb	r2, [sp, #105]	@ zero_extendqisi2
	ldrh	r3, [sp, #52]
	cmp	r2, r3
	bls	.L3967
	add	r3, r3, #8
	mov	r8, #1
	mov	r3, r3, asl r8
	str	r3, [sp, #76]
.L3968:
	cmp	r8, r10
	bhi	.L3986
	ldr	r2, [sp, #76]
	add	r3, sp, #128
	add	r3, r3, r2
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3953
	ldr	r3, .L3987+20
	cmp	r10, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3954
	ldr	r1, .L3987
	ldrb	r0, [r1, #1196]	@ zero_extendqisi2
	cmp	r0, #0
	ldrne	r1, [sp, #64]
	bne	.L3983
	ldrb	r1, [r1, #1197]	@ zero_extendqisi2
	mov	r4, r8, asl #24
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #64]
	addne	r3, r3, r1
	addne	r3, r3, r8
	orr	r4, r4, r3
	b	.L3956
.L3954:
	cmp	r10, #2
	uxtahne	r4, r3, r5
	bne	.L3956
	ldr	r1, [sp, #72]
.L3983:
	add	r3, r3, r1
	ldr	r1, [sp, #68]
	add	r3, r3, r8
	ldrb	r4, [r1, #-3196]	@ zero_extendqisi2
	orr	r4, r3, r4, asl #24
.L3956:
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
	ldr	r0, .L3987+24
	str	r1, [sp, #16]
	ldr	r1, [r3]
	str	r1, [sp, #20]
	ldr	r1, [r3, #4]
	str	r1, [sp, #24]
	ldr	r1, [r3, #8]
	str	r1, [sp, #28]
	mov	r1, r2
	ldr	r3, [r3, #12]
	ldr	r2, [sp, #44]
	str	r3, [sp, #32]
	mov	r3, r4
	bl	printk
	ldr	r0, .L3987+28
	ldr	ip, [sp, #80]
	ldr	r3, [sp, #48]
	cmn	ip, #1
	cmpne	ip, #512
	moveq	r3, #1
	str	r3, [sp, #48]
	ldr	r3, .L3987+32
	ldr	r3, [r3, #4]
	blx	r3
	ldr	r3, .L3987
	ldr	r3, [r3, #1080]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #224
	cmpne	r2, #32
	moveq	r2, #1
	movne	r2, #0
	beq	.L3953
	ldr	r3, [r6, #12]
	str	r2, [sp, #80]
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #92]
	ldr	r2, [sp, #80]
	bne	.L3960
	ldr	r3, [r6, #12]
	add	r1, sp, #92
	ldr	r0, [r3, #4]
	bl	pm_log2phys
.L3960:
	ldr	r3, [sp, #92]
	cmp	r3, r4
	bne	.L3961
	ldr	r3, [sp, #40]
	mov	r1, r4
	ldr	r0, .L3987+36
	add	r3, r3, #1
	str	r3, [sp, #40]
	ldr	r3, [r6, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #40]
	bl	printk
.L3961:
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L3963
	ldr	r2, [r3, r9, asl #2]
	mov	r4, r9, asl #2
	ldr	r3, [r6, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	beq	.L3964
	ldr	r3, .L3987+40
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3964
	ldr	r0, .L3987+44
	mov	r1, r9
	bl	printk
.L3964:
	ldr	r3, [sp, #56]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r4]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	cmnne	r3, #1
	beq	.L3963
	ldr	r1, .L3987+48
	movw	r2, #1567
	ldr	r0, .L3987+52
	bl	printk
	bl	dump_stack
.L3963:
	add	r9, r9, #1
.L3953:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L3968
.L3986:
	ldr	r3, [sp, #52]
	add	r3, r3, #1
	b	.L3985
.L3967:
	add	r5, r5, #1
	b	.L3952
.L3969:
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, .L3987
	ldr	r0, .L3987+56
	mov	r1, r7
	ldr	r2, [r3, #1088]
	mov	r3, r7, asl #1
	ldrh	r2, [r2, r3]
	ldr	r3, [sp, #40]
	bl	printk
	ldr	r0, [sp, #48]
	b	.L3950
.L3972:
	mov	r0, #0
.L3950:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3988:
	.align	2
.L3987:
	.word	.LANCHOR0
	.word	.LC254
	.word	.LANCHOR3-3168
	.word	.LANCHOR3
	.word	.LC255
	.word	.LANCHOR3-3142
	.word	.LC200
	.word	2147483000
	.word	arm_delay_ops
	.word	.LC256
	.word	.LANCHOR2
	.word	.LC257
	.word	.LANCHOR1+2668
	.word	.LC0
	.word	.LC258
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
	ldr	r3, .L4051
	.pad #76
	sub	sp, sp, #76
	mov	r8, r0
	mov	r4, r1
	mov	r5, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3990
	ldr	r0, .L4051+4
	mov	r1, r8
	mov	r2, r4
	mov	r3, r5
	bl	printk
.L3990:
	cmp	r8, #0
	ldreq	r3, .L4051+8
	moveq	r8, #24576
	ldreq	fp, [r3, #1032]
	beq	.L3992
.L3991:
	cmp	r8, #3
	bhi	.L4019
	mov	r8, r8, asl #13
	mov	fp, #8192
.L3992:
	cmp	r5, fp
	cmpls	r4, fp
	movcs	r7, #1
	movcc	r7, #0
	bcs	.L4019
	add	r3, r4, r5
	cmp	r3, fp
	bhi	.L4019
	ldr	r2, .L4051+8
	add	r3, r8, r4
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
	ldr	r7, .L4051+12
	add	r3, r3, r0
	str	r0, [sp, #48]
	str	r3, [sp, #32]
.L3994:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	beq	.L4048
	ldr	r3, [sp, #36]
	ldr	r8, .L4051+8
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
	beq	.L3995
	cmp	r2, #0
	beq	.L3996
	ldr	r0, [sp, #28]
	bl	__aeabi_uidivmod
	uxth	fp, r1
	rsb	r10, fp, r10
	uxth	r10, r10
	cmp	r10, r5
	uxthhi	r10, r5
	b	.L3995
.L3996:
	cmp	r3, #0
	ldrne	r3, [sp, #28]
	movne	fp, r2
	moveq	fp, r3
	addne	r3, r3, r5
	mlsne	r1, r4, r1, r3
	uxtbne	r10, r1
.L3995:
	ldr	r2, .L4051+16
	mov	r3, #0
.L3999:
	ldr	r1, [r2, #20]
	cmp	r1, r4
	bne	.L3997
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L3997
	ldr	r1, .L4051+8
	mov	r2, #48
	mov	r10, r10, asl #9
	mov	r0, r6
	add	r6, r6, r10
	mla	r3, r2, r3, r1
	mov	r2, r10
	ldr	r1, [r3, #1240]
	add	r1, r1, fp, asl #9
	bl	ftl_memcpy
	b	.L3998
.L3997:
	add	r3, r3, #1
	add	r2, r2, #48
	cmp	r3, #32
	bne	.L3999
	mov	r0, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #68]
	bne	.L4000
	mov	r0, r4
	add	r1, sp, #68
	mov	r2, #0
	bl	pm_log2phys
.L4000:
	ldr	r3, [sp, #68]
	cmn	r3, #1
	moveq	r10, #0
	bne	.L4049
.L4002:
	ldrb	r3, [r9, #2772]	@ zero_extendqisi2
	cmp	r10, r3
	bcs	.L3998
	mla	r3, r3, r4, r10
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #44]
	cmp	r3, r2
	movcs	r2, #1
	movcc	r2, #0
	cmp	r3, r1
	movcs	r2, #0
	cmp	r2, #0
	beq	.L4003
	mov	r0, r6
	mov	r1, #0
	mov	r2, #512
	add	r6, r6, #512
	bl	ftl_memset
.L4003:
	add	r10, r10, #1
	b	.L4002
.L4049:
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4005
	ldr	r2, .L4051+8
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
.L3998:
	ldr	r3, [sp, #32]
	add	r4, r4, #1
	sub	r3, r3, #1
	str	r3, [sp, #32]
.L4005:
	ldrb	r3, [r8, #2774]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	cmp	r2, #0
	cmpne	r3, #2
	bhi	.L3994
	ldrb	r1, [r7, #-2605]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3994
	ldrb	r2, [r7, #-2606]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L4051+16
	mla	r0, r0, r2, r3
	bl	sblk_read_page
.L4007:
	ldrb	r3, [r7, #-2605]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4050
	ldrb	r3, [r7, #-2606]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L4008
	ldr	r1, .L4051+20
	movw	r2, #1276
	ldr	r0, .L4051+24
	bl	printk
	bl	dump_stack
.L4008:
	ldr	r3, .L4051+12
	mov	r2, #48
	ldr	r8, .L4051+8
	ldr	r1, .L4051+12
	ldrb	r10, [r3, #-2606]	@ zero_extendqisi2
	ldr	r3, .L4051+16
	mul	r2, r2, r10
	add	r3, r3, r2
	add	r2, r8, r2
	str	r3, [sp, #52]
	ldr	r9, [r2, #1272]
	ldrb	r3, [r2, #1236]	@ zero_extendqisi2
	cmn	r9, #1
	strb	r3, [r1, #-2606]
	bne	.L4009
	ldr	r2, [r8, #1092]
	str	r9, [sp, #40]
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	b	.L4010
.L4009:
	cmp	r9, #256
	bne	.L4010
	ldr	r1, .L4051+28
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
	ldrb	r1, [r7, #-3202]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r2, [sp, #60]
	uxth	fp, r0
	ldr	r3, [sp, #56]
	mov	r1, fp
	ldr	r0, .L4051+32
	ldr	r2, [r2, #1256]
	bl	printk
	mov	r0, fp
	mov	r1, r8
	mov	r2, #0
	bl	gc_add_sblk
.L4010:
	ldr	ip, .L4051+8
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
	beq	.L4011
	ldr	r1, [ip, #1092]
	ldr	r0, .L4051+36
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
	ldr	r3, .L4051+28
	ldr	r2, [r8, #1260]
	ldrb	r1, [r7, #-3202]	@ zero_extendqisi2
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
.L4011:
	ldr	r8, .L4051+8
	mov	r3, #48
	mla	r3, r3, r10, r8
	mov	r9, r8
	ldr	r2, [r3, #1248]
	ldr	r3, [r3, #1256]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	orrne	fp, fp, #1
	cmp	fp, #0
	beq	.L4012
	ldr	r1, .L4051+20
	movw	r2, #1301
	ldr	r0, .L4051+24
	bl	printk
	bl	dump_stack
.L4012:
	mov	r3, #48
	mla	r10, r3, r10, r9
	ldrb	r3, [r8, #2772]	@ zero_extendqisi2
	ldrb	r2, [r10, #1276]	@ zero_extendqisi2
	cmp	r3, r2
	ldrlsb	r3, [r10, #1238]	@ zero_extendqisi2
	bicls	r3, r3, #8
	strlsb	r3, [r10, #1238]
	bls	.L4014
	ldrb	r1, [r10, #1277]	@ zero_extendqisi2
	mov	r2, r2, asl #9
	ldr	r3, [r10, #1240]
	ldr	r0, [r10, #1244]
	add	r1, r3, r1, asl #9
	bl	ftl_memcpy
.L4014:
	ldr	r1, [sp, #52]
	ldr	r0, .L4051+40
	bl	buf_remove_buf
	ldr	r0, [sp, #52]
	bl	zbuf_free
	ldrb	r3, [r7, #-2605]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r7, #-2605]
	b	.L4007
.L4050:
	ldr	r1, .L4051+12
	mvn	r2, #0
	strb	r3, [r1, #-2605]
	strb	r2, [r1, #-2606]
	b	.L3994
.L4048:
	bl	timer_get_time
	ldr	r3, .L4051+12
	str	r0, [r3, #480]
	ldr	r0, [sp, #40]
	b	.L3993
.L4019:
	mvn	r0, #0
.L3993:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4052:
	.align	2
.L4051:
	.word	.LANCHOR2
	.word	.LC259
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2684
	.word	.LC0
	.word	.LANCHOR3-3204
	.word	.LC260
	.word	.LC261
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
	mov	r7, r2
	add	r4, sp, #40
	mvnhi	r0, #21
	str	r4, [sp, #36]
	bhi	.L4057
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	mvnne	r0, #13
	bne	.L4057
	add	r3, sp, #120
	mov	r1, r4
	add	r3, r3, r7
	strb	r0, [r3, #-80]
	ldr	r0, .L4082
	bl	printk
	ldr	r0, .L4082+4
	mov	r1, r4
	mov	r2, #1
	mov	r3, #16
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	r0, r4
	ldr	r1, .L4082+8
	mov	r2, #7
	bl	memcmp
	subs	r5, r0, #0
	bne	.L4058
	ldr	r4, .L4082+12
	movw	ip, #698
	ldr	r0, .L4082+16
	mov	r2, #4
	ldr	r6, .L4082+20
	movw	r8, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4082+24
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r6, #-224]
	bl	rknand_print_hex
.L4059:
	ldrh	r3, [r6, #-224]
	cmp	r5, r3
	bge	.L4062
	mov	r0, #300
	bl	msleep
	add	r3, r5, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r8
	beq	.L4060
	mov	r1, #0
	bl	ftl_sblk_dump
.L4060:
	add	r5, r5, #1
	b	.L4059
.L4058:
	mov	r0, r4
	ldr	r1, .L4082+28
	mov	r2, #7
	bl	memcmp
	subs	r5, r0, #0
	bne	.L4063
	ldr	r4, .L4082+12
	movw	ip, #698
	ldr	r0, .L4082+16
	mov	r2, #4
	ldr	r8, .L4082+32
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r3, .L4082+20
	ldr	r1, [r4, #2804]
	mov	r2, #2
	ldr	r0, .L4082+24
	ldrh	r3, [r3, #-224]
	add	r1, r1, #416
	bl	rknand_print_hex
	ldr	r0, .L4082+36
	add	r6, sp, #120
	add	r1, sp, #47
	str	r1, [r6, #-84]!
	bl	printk
	mov	r1, r6
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	uxth	r6, r0
	str	r0, [sp, #32]
.L4065:
	add	r9, r8, r5
	ldr	r0, .L4082+40
	ldrh	r1, [r8, r5]
	ldrh	r2, [r9, #2]
	bl	printk
	ldrh	r3, [r5, r8]
	cmp	r3, r6
	bne	.L4064
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r0, .L4082+44
	ldr	r1, [r9, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
.L4064:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L4065
	mov	r0, #300
	add	r6, r6, #176
	bl	msleep
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r4, #2804]
	mov	r1, #1
	ldr	r3, [r3, r6, asl #2]
	str	r3, [r0, #24]
	mov	r5, r0
	str	r3, [sp, #32]
	bl	sblk_read_page
	ldr	ip, [r5, #4]
	ldr	r3, [r5, #12]
	ldr	r0, .L4082+48
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
	ldr	r2, [r5, #36]
	ldr	r3, [ip]
	bl	printk
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, .L4082+52
	mov	r2, #4
	ldr	r1, [r5, #4]
	mov	r3, r3, asl #7
	b	.L4081
.L4063:
	mov	r0, r4
	ldr	r1, .L4082+56
	mov	r2, #7
	bl	memcmp
	cmp	r0, #0
	bne	.L4066
	bl	dump_ftl_info
	b	.L4062
.L4066:
	mov	r0, r4
	ldr	r1, .L4082+60
	mov	r2, #9
	bl	memcmp
	cmp	r0, #0
	bne	.L4067
	add	r1, sp, #120
	add	r0, sp, #49
	str	r0, [r1, #-84]!
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4082+64
	str	r0, [sp, #32]
	strh	r0, [r3, #-8]	@ movhi
	bl	dump_all_list_info
	b	.L4062
.L4067:
	mov	r0, r4
	ldr	r1, .L4082+68
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	beq	.L4062
	mov	r0, r4
	ldr	r1, .L4082+72
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	bne	.L4069
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4082+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r4, .L4082+12
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	str	r0, [sp, #32]
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #32]
	mov	r5, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r5, #4]
	ldr	r2, [r5, #12]
	ldr	r0, .L4082+48
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
	ldr	r2, [r5, #36]
	ldr	r3, [r3]
	bl	printk
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, .L4082+76
	mov	r2, #4
	ldr	r1, [r5, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, .L4082+80
	mov	r2, #4
	ldr	r1, [r5, #12]
	mov	r3, r3, asl #1
.L4081:
	bl	rknand_print_hex
	mov	r0, r5
	bl	zbuf_free
	b	.L4062
.L4069:
	mov	r0, r4
	ldr	r1, .L4082+84
	mov	r2, #8
	bl	memcmp
	subs	r5, r0, #0
	bne	.L4070
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4082+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r1, r5
	str	r0, [sp, #32]
	uxth	r0, r0
	bl	ftl_sblk_dump
	b	.L4062
.L4070:
	mov	r0, r4
	ldr	r1, .L4082+88
	mov	r2, #10
	bl	memcmp
	cmp	r0, #0
	bne	.L4071
	add	r4, sp, #120
	add	r1, sp, #50
	ldr	r0, .L4082+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4082+92
	str	r0, [sp, #32]
	str	r0, [r3]
	b	.L4062
.L4071:
	mov	r0, r4
	ldr	r1, .L4082+96
	mov	r2, #8
	bl	memcmp
	subs	r5, r0, #0
	bne	.L4072
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4082+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r4, r0
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #32]
	bne	.L4073
	mov	r0, r4
	add	r1, sp, #32
	mov	r2, r5
	bl	pm_log2phys
.L4073:
	mov	r1, r4
	ldr	r2, [sp, #32]
	ldr	r0, .L4082+100
	bl	printk
	b	.L4062
.L4072:
	ldr	r0, .L4082+104
	bl	printk
	ldr	r0, .L4082+108
	bl	printk
	ldr	r0, .L4082+112
	bl	printk
	ldr	r0, .L4082+116
	bl	printk
	ldr	r0, .L4082+120
	bl	printk
	ldr	r0, .L4082+124
	bl	printk
	ldr	r0, .L4082+128
	bl	printk
	ldr	r0, .L4082+132
	bl	printk
	ldr	r0, .L4082+136
	bl	printk
.L4062:
	bl	rknand_device_unlock
	mov	r0, r7
.L4057:
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L4083:
	.align	2
.L4082:
	.word	.LC262
	.word	.LC263
	.word	.LC264
	.word	.LANCHOR0
	.word	.LC265
	.word	.LANCHOR3
	.word	.LC266
	.word	.LC267
	.word	.LANCHOR3-2600
	.word	.LC268
	.word	.LC269
	.word	.LC270
	.word	.LC243
	.word	.LC271
	.word	.LC272
	.word	.LC273
	.word	.LANCHOR3-3136
	.word	.LC274
	.word	.LC275
	.word	.LC216
	.word	.LC238
	.word	.LC276
	.word	.LC277
	.word	.LANCHOR2
	.word	.LC278
	.word	.LC279
	.word	.LC280
	.word	.LC281
	.word	.LC282
	.word	.LC283
	.word	.LC284
	.word	.LC285
	.word	.LC286
	.word	.LC287
	.word	.LC288
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
	ldr	r5, .L4115
	.pad #20
	sub	sp, sp, #20
	ldrb	r7, [r0, #9]	@ zero_extendqisi2
	sub	r3, r5, #3168
	ldrh	r4, [r0, #12]
	ldrh	r3, [r3, #-4]
	mul	r7, r7, r3
	ldr	r3, [r5, #-2624]
	add	r4, r3, r4, asl #2
	sub	r3, r7, #-1073741823
	ldr	r3, [r4, r3, asl #2]
	cmn	r3, #1
	beq	.L4085
	ldr	r1, .L4115+4
	movw	r2, #1990
	ldr	r0, .L4115+8
	bl	printk
	bl	dump_stack
.L4085:
	mov	r5, #0
	sub	r4, r4, #4
	mov	r9, r5
.L4086:
	cmp	r9, r7
	beq	.L4114
	ldr	r8, [r4, #4]!
	cmn	r8, #1
	beq	.L4087
	ldr	r3, .L4115+12
	mov	r0, r8
	ldrb	r1, [r3, #2772]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, .L4115+16
	ldr	r3, [r3]
	tst	r3, #4096
	uxth	fp, r0
	beq	.L4088
	ldr	r0, .L4115+20
	mov	r1, fp
	mov	r2, r8
	mov	r3, r9
	bl	printk
.L4088:
	mov	r8, r4
	mov	r10, r9
.L4091:
	ldr	r0, [r8]
	cmn	r0, #1
	beq	.L4089
	ldr	r3, .L4115+12
	ldrb	r1, [r3, #2772]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, fp
	bne	.L4089
	ldrb	r3, [r6, #9]	@ zero_extendqisi2
	mov	r0, r10
	mov	r1, r3
	str	r3, [sp, #4]
	bl	__aeabi_idiv
	ldr	r2, .L4115+24
	ldrh	r2, [r2]
	ldr	r3, [sp, #4]
	mls	r3, r3, r0, r10
	add	r3, r6, r3, asl #1
	ldrh	r3, [r3, #16]
	mla	r2, r2, r3, r0
	ldr	r0, .L4115+28
	str	r2, [sp, #12]
	ldr	r1, [r8]
	bl	pm_ppa_update_check
	cmp	r0, #0
	beq	.L4090
	ldr	r3, .L4115
	mov	r2, #4
	ldr	r0, .L4115+32
	ldr	r1, [r3, #-2624]
	mov	r3, r7
	bl	rknand_print_hex
.L4090:
	add	r5, r5, #1
	ldr	r0, [r8]
	add	r1, sp, #12
	mov	r2, #1
	uxth	r5, r5
	bl	pm_log2phys
	mvn	r3, #0
	str	r3, [r8]
.L4089:
	add	r10, r10, #1
	add	r8, r8, #4
	cmp	r10, r7
	bne	.L4091
.L4087:
	add	r9, r9, #1
	b	.L4086
.L4114:
	ldr	r3, .L4115+16
	ldr	r4, .L4115+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4093
	ldrh	r1, [r6]
	mov	r2, r5
	ldr	ip, [r4, #1088]
	ldr	r0, .L4115+36
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L4093:
	ldrh	r3, [r6]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4116:
	.align	2
.L4115:
	.word	.LANCHOR3
	.word	.LANCHOR1+2696
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC289
	.word	.LANCHOR3-3142
	.word	.LC290
	.word	.LC291
	.word	.LC292
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
	beq	.L4118
	bl	zftl_insert_data_list
.L4118:
	ldr	r4, .L4126
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
	beq	.L4120
	mov	r1, r5
	ldr	r0, .L4126+4
	bl	printk
	ldr	r3, [r4, #1092]
	ldr	r0, [r3, #564]
	bl	gc_mark_bad_ppa
	ldr	r3, [r4, #1092]
	mvn	r2, #0
	str	r6, [r3, #560]
	str	r2, [r3, #564]
.L4120:
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L4127:
	.align	2
.L4126:
	.word	.LANCHOR0
	.word	.LC293
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
	ldr	r6, .L4194
	mov	r7, r6
.L4129:
	ldrb	r2, [r6, #2800]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4131
	ldrb	r3, [r6, #2824]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r6, #2800]
	str	r3, [sp, #16]
	mov	r3, #48
	ldr	r1, [sp, #16]
	mul	r3, r3, r1
	ldr	r1, .L4194+4
	add	r1, r1, r3
	add	r3, r6, r3
	str	r1, [sp, #20]
	ldr	r2, [r3, #1256]
	ldrb	r1, [r3, #1236]	@ zero_extendqisi2
	ldr	r3, [r6, #2784]
	cmp	r2, r3
	strb	r1, [r6, #2824]
	bcc	.L4132
	ldr	r1, .L4194+8
	mov	r2, #604
	ldr	r0, .L4194+12
	bl	printk
	bl	dump_stack
.L4132:
	ldr	r2, [sp, #16]
	mov	r3, #48
	ldr	r1, [r7, #2784]
	mla	r2, r3, r2, r7
	ldr	r8, [r2, #1256]
	cmp	r8, r1
	bcc	.L4133
	ldr	r0, [sp, #20]
	bl	zbuf_free
	mvn	r0, #0
	b	.L4190
.L4133:
	ldr	r1, [r2, #1248]
	ldr	fp, [r2, #1240]
	ldrb	r4, [r2, #1277]	@ zero_extendqisi2
	ldrb	r2, [r2, #1276]	@ zero_extendqisi2
	str	r1, [sp, #8]
	str	r2, [sp, #12]
	ldrb	r2, [r6, #2801]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4135
	ldr	r2, .L4194+16
	ldrb	r5, [r2, #-168]	@ zero_extendqisi2
	ldr	r2, .L4194+4
	mla	r5, r3, r5, r2
.L4136:
	ldrb	r2, [r5]	@ zero_extendqisi2
	cmp	r2, #255
	ldrne	r1, .L4194+4
	mlane	r5, r3, r2, r1
	bne	.L4136
.L4192:
	ldr	r3, [r5, #20]
	cmp	r3, r8
	bne	.L4135
	ldr	r3, [sp, #12]
	mov	r1, r4, asl #9
	ldr	r0, [r5, #4]
	mov	r2, r3, asl #9
	add	r0, r0, r1
	add	r1, fp, r1
	bl	ftl_memcpy
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	ldr	r1, [r5, #12]
	mov	r0, #2
	sub	r3, r3, #2
	ldr	r2, [r5, #4]
	add	r1, r1, #16
	add	r2, r2, r3, asl #9
	bl	ftl_debug_info_fill
	ldr	r0, [sp, #20]
	bl	zbuf_free
	b	.L4129
.L4135:
	mov	r0, r8
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4139
	mov	r0, r8
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
.L4139:
	ldr	r5, [r7, #1092]
	mov	r2, #0
	ldr	r10, .L4194+4
	mov	r9, r2
	ldr	r0, [sp, #28]
	add	r5, r5, #16
	add	r1, r10, #1536
.L4141:
	ldr	r3, [r10, #20]
	cmp	r3, r8
	bne	.L4140
	ldrb	r3, [r10, #2]	@ zero_extendqisi2
	tst	r3, #8
	bicne	r3, r3, #8
	strneb	r3, [r10, #2]
	ldrne	r9, [r10, #4]
	movne	r2, #1
	ldrne	r0, [r10, #24]
.L4140:
	add	r10, r10, #48
	cmp	r10, r1
	bne	.L4141
	cmp	r2, #0
	ldrb	r3, [r7, #2772]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	strne	r0, [sp, #28]
	cmp	r2, r3
	bcs	.L4166
	cmp	r9, #0
	beq	.L4144
	cmp	r4, #0
	beq	.L4145
	mov	r0, fp
	mov	r1, r9
	mov	r2, r4, asl #9
	bl	ftl_memcpy
	ldr	r5, [r6, #1092]
	add	r5, r5, #48
.L4145:
	ldr	r3, [sp, #12]
	ldrb	r10, [r7, #2772]	@ zero_extendqisi2
	add	r2, r4, r3
	cmp	r2, r10
	ldrcs	r5, [r6, #1092]
	addcs	r5, r5, #16
	bcs	.L4166
	mov	r1, r2, asl #9
	rsb	r2, r2, r10
	add	r0, fp, r1
	add	r1, r9, r1
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4166
.L4144:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4147
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #28]
	mov	r9, r0
	str	r8, [r0, #20]
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r9, #12]
	ldr	r10, [r3, #12]
	ldr	r3, [r3, #4]
	add	r10, r10, #1
	cmp	r3, r8
	bne	.L4148
	ldr	r3, [r9, #36]
	cmn	r3, #1
	bne	.L4149
.L4148:
	ldr	r2, [r7, #1092]
	ldr	r0, .L4194+20
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r9, #36]
	ldrb	r1, [r9, #1]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	str	r3, [sp]
	mov	r3, r8
	bl	printk
	mov	r2, #4
	ldr	r0, .L4194+24
	mov	r3, r2
	ldr	r1, [r9, #12]
	bl	rknand_print_hex
.L4149:
	ldr	r3, [r9, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r8
	bne	.L4150
	ldr	r3, [r9, #36]
	cmn	r3, #1
	bne	.L4151
.L4150:
	ldr	r1, .L4194+8
	movw	r2, #691
	ldr	r0, .L4194+12
	bl	printk
	bl	dump_stack
.L4151:
	cmp	r4, #0
	beq	.L4152
	ldr	r3, [sp, #28]
	mov	r2, r4, asl #9
	cmn	r3, #1
	beq	.L4153
	mov	r0, fp
	ldr	r1, [r9, #4]
	bl	ftl_memcpy
	b	.L4154
.L4147:
	cmp	r4, #0
	moveq	r9, r4
	moveq	r10, r4
	beq	.L4152
	mov	r2, r4, asl #9
	mov	r10, r9
.L4153:
	mov	r0, fp
	mov	r1, #0
	bl	ftl_memset
.L4154:
	ldr	r5, [r6, #1092]
	add	r5, r5, #48
.L4152:
	ldr	r3, [sp, #12]
	ldrb	r2, [r6, #2772]	@ zero_extendqisi2
	add	r4, r4, r3
	cmp	r4, r2
	bcc	.L4155
	ldrhi	r5, [r6, #1092]
	addhi	r5, r5, #16
	b	.L4156
.L4155:
	ldr	r3, [sp, #28]
	rsb	r2, r4, r2
	mov	r4, r4, asl #9
	cmn	r3, #1
	mov	r2, r2, asl #9
	beq	.L4157
	ldr	r1, [r9, #4]
	add	r0, fp, r4
	add	r1, r1, r4
	bl	ftl_memcpy
	b	.L4156
.L4157:
	add	r0, fp, r4
	mov	r1, #0
	bl	ftl_memset
.L4156:
	cmp	r9, #0
	beq	.L4143
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	mov	r1, r9
	ldr	r0, .L4194+28
	bic	r3, r3, #8
	strb	r3, [r9, #2]
	bl	buf_remove_buf
	mov	r0, r9
	bl	zbuf_free
	b	.L4143
.L4166:
	mov	r10, #0
.L4143:
	ldrh	r3, [r5, #6]
	cmp	r3, #0
	bne	.L4158
	bl	ftl_flush
	mov	r0, r5
	bl	ftl_alloc_new_data_sblk
.L4158:
	mov	r0, r5
	mov	r4, #48
	bl	ftl_get_new_free_page
	ldr	r3, [sp, #16]
	str	r0, [sp, #12]
	mov	r0, #2
	mul	r4, r4, r3
	ldr	r3, [sp, #8]
	add	r9, r7, r4
	mov	r1, r3
	ldr	r2, [r9, #1252]
	str	r10, [r3, #12]
	stmia	r3, {r2, r8}
	ldr	r2, [sp, #28]
	ldrb	r8, [r7, #2772]	@ zero_extendqisi2
	str	r2, [r3, #8]
	mov	r2, #0
	str	r2, [r1, #16]!
	sub	r2, r8, #2
	add	r2, fp, r2, asl #9
	bl	ftl_debug_info_fill
	ldr	r3, .L4194+4
	add	r4, r3, r4
	ldr	r2, [sp, #28]
	ldr	ip, [sp, #12]
	ldr	r1, [sp, #20]
	str	r2, [r9, #1264]
	mvn	r2, #0
	strb	r2, [r9, #1236]
	ldrb	r2, [r9, #1238]	@ zero_extendqisi2
	str	ip, [r9, #1260]
	orr	r2, r2, #10
	strb	r2, [r9, #1238]
	ldrh	r9, [r5, #10]
	ldrh	r8, [r5, #12]
	add	r8, r9, r8
	sub	r8, r8, #1
	strh	r8, [r4, #32]	@ movhi
	ldr	r4, .L4194+16
	sub	r0, r4, #168
	bl	buf_add_tail
	ldrb	r3, [r7, #2801]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r7, #2801]
	bl	timer_get_time
	ldrb	r3, [r7, #2801]	@ zero_extendqisi2
	ldrh	r2, [r5, #6]
	cmp	r3, #2
	str	r0, [r4, #-164]
	bhi	.L4159
	cmp	r2, #1
	bne	.L4131
.L4159:
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	mov	lr, #48
	ldrb	r0, [r4, #-168]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, #0
	moveq	ip, #1
	movne	ip, #2
	cmp	r2, #1
	mov	r2, r0
	moveq	ip, r3
.L4163:
	cmp	r1, ip
	mlane	r2, lr, r2, r6
	addne	r1, r1, #1
	ldrneb	r2, [r2, #1236]	@ zero_extendqisi2
	bne	.L4163
.L4193:
	strb	r2, [r4, #-168]
	uxtb	r1, r1
	ldr	r2, .L4194+4
	rsb	r3, r1, r3
	strb	r3, [r7, #2801]
	mov	r3, #48
	mla	r0, r3, r0, r2
	bl	sblk_prog_page
	ldrh	r3, [r5, #6]
	cmp	r3, #1
	bne	.L4131
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	r0, r5
	bl	ftl_write_last_log_page
	mov	r0, r5
	bl	ftl_alloc_new_data_sblk
.L4131:
	ldrb	r4, [r6, #2800]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L4129
	bl	ftl_write_completed
	mov	r0, r4
.L4190:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4195:
	.align	2
.L4194:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2716
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC294
	.word	.LC238
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
	ldr	r8, .L4344
	ldr	r5, .L4344+4
	ldrb	r0, [r8, #-3187]	@ zero_extendqisi2
	ldr	r3, [r5, #1092]
	cmp	r0, #0
	str	r3, [sp, #12]
	bne	.L4197
	bl	buf_alloc
	subs	r4, r0, #0
	beq	.L4196
	ldr	r7, .L4344+8
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
	bne	.L4199
	ldr	r1, .L4344+12
	movw	r2, #1032
	ldr	r0, .L4344+16
	bl	printk
	bl	dump_stack
.L4199:
	ldr	r3, [r4, #12]
	ldr	r6, [r3, #4]
	mov	r0, r6
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #48]
	bne	.L4200
	mov	r0, r6
	add	r1, sp, #48
	mov	r2, #0
	bl	pm_log2phys
.L4200:
	ldr	r8, [sp, #48]
	cmp	r8, r9
	bne	.L4201
	ldr	r1, .L4344+20
	mov	r3, #0
.L4204:
	add	r2, r1, r3
	ldr	r0, [r2, #20]
	cmp	r0, r6
	bne	.L4202
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #2
	beq	.L4202
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L4344+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4196
	mov	r1, r6
	mov	r2, r8
	ldrh	r3, [r7, #22]
	ldr	r0, .L4344+28
	bl	printk
	b	.L4196
.L4202:
	add	r3, r3, #48
	cmp	r3, #1536
	bne	.L4204
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	mov	r2, #0
	str	r6, [r4, #20]
	strb	r2, [r4, #41]
	strb	r3, [r4, #40]
	ldr	r3, [r4, #12]
	ldr	r1, [r3]
	str	r1, [r4, #16]
	str	r2, [r3, #16]
	ldr	r3, .L4344+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4205
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r8, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4344+32
	str	r2, [sp, #4]
	mov	r2, r8
	bl	printk
.L4205:
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
	b	.L4196
.L4201:
	ldr	r3, .L4344+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4207
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r9, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4344+32
	str	r2, [sp, #4]
	mov	r2, r8
	bl	printk
.L4207:
	mov	r0, r4
	bl	zbuf_free
	b	.L4196
.L4197:
	ldr	r3, [sp, #12]
	ldrb	r9, [r3, #89]	@ zero_extendqisi2
	ldrb	r3, [r8, #-3195]	@ zero_extendqisi2
	uxth	fp, r9
	cmp	r3, fp
	movhi	r6, #1
	bhi	.L4209
	cmp	fp, #2
	movcc	r6, fp
	movcs	r6, #2
.L4209:
	ldrb	r4, [r8, #-3196]	@ zero_extendqisi2
	ldr	r10, .L4344
	cmp	r4, #3
	ldr	r3, .L4344+8
	bne	.L4210
	ldrb	r2, [r5, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4211
	movw	r2, #2102
	mov	r1, r9
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bl	__aeabi_idiv
	ldrb	r3, [r10, #-2612]	@ zero_extendqisi2
	cmp	r3, #0
	uxth	r5, r0
	beq	.L4212
	ldr	r3, .L4344+36
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
	b	.L4213
.L4212:
	ldrb	r3, [r10, #473]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L4213
	sub	r3, r5, #62
	cmp	r3, #2160
	movcs	r4, #2
	bcs	.L4213
	mov	r1, r4
	mov	r0, r5
	bl	__aeabi_uidivmod
	uxth	r1, r1
	cmp	r1, #0
	movne	r4, #1
	moveq	r4, #2
.L4213:
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
	b	.L4216
.L4211:
	ldrb	r2, [r10, #-3188]	@ zero_extendqisi2
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	beq	.L4215
	add	r1, r9, r9, asl #1
	bl	__aeabi_idiv
	ldr	r2, .L4344+40
	mul	r9, r9, r0
	uxth	r5, r0
	sub	r9, r9, r9, asl #2
	add	r1, r7, r9
	smull	r2, r3, r1, r2
	sub	r1, r3, r1, asr #31
	uxth	r3, r1
	str	r3, [sp, #16]
	ldr	r3, .L4344+4
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r5, r5, r5, asl #1
	uxthne	r5, r5
	b	.L4216
.L4215:
	mov	r1, r9
	bl	__aeabi_idiv
	ldr	r2, .L4344+44
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
	bne	.L4217
	b	.L4216
.L4210:
	ldrb	r2, [r10, #-3194]	@ zero_extendqisi2
	mov	r1, r9
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bne	.L4218
	bl	__aeabi_idiv
	mov	r4, #1
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
	b	.L4216
.L4218:
	bl	__aeabi_idiv
	mov	r4, #2
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #16]
.L4216:
	mul	r3, r6, r4
	ldr	r10, .L4344+36
	mov	r2, #0
	ldr	ip, .L4344
	add	r10, r10, r5, asl #1
	ldr	r1, .L4344+8
	str	r3, [sp, #20]
	b	.L4219
.L4339:
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4277
	tst	lr, #1
	beq	.L4277
.L4220:
	ldr	fp, [r8, #-240]
	ldr	r0, [fp, r3, asl #2]
	cmn	r0, #1
	beq	.L4338
.L4223:
	add	r2, r2, #1
.L4219:
	ldr	r3, [sp, #20]
	uxth	lr, r2
	cmp	lr, r3
	bge	.L4277
	ldr	r0, .L4344+48
	add	r3, r7, lr
	ldr	r9, [sp, #12]
	ldrh	r0, [r0]
	ldrb	r9, [r9, #89]	@ zero_extendqisi2
	mul	r0, r9, r0
	ldrb	r9, [r8, #477]	@ zero_extendqisi2
	rsb	r0, r9, r0
	cmp	r3, r0
	blt	.L4220
	ldrb	r0, [ip, #-2612]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L4339
.L4277:
	mov	r3, #1
	str	r3, [sp, #20]
.L4217:
	rsb	r1, r6, r6, asl #15
	mul	r3, r6, r4
	mov	r1, r1, asl #1
	smulbb	r1, r1, r4
	str	r3, [sp, #24]
	add	r1, r7, r1
	uxth	r3, r1
	ldr	r1, .L4344+36
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #28]
	add	r3, r1, r5, asl #1
	str	r3, [sp, #32]
	b	.L4222
.L4338:
	ldrh	r9, [r1, #22]
	ldrh	r0, [r1, #20]
	cmp	r0, r9
	bls	.L4196
	ldrb	r0, [ip, #-2612]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L4224
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4224
	tst	lr, #1
	ldrne	r0, [fp, r0, asl #2]
	strne	r0, [fp, r3, asl #2]
	bne	.L4223
.L4224:
	mov	r0, r9
	str	r1, [sp, #36]
	str	ip, [sp, #32]
	add	r9, r9, #1
	str	r2, [sp, #28]
	str	r3, [sp, #24]
	bl	gc_get_src_ppa_from_index
	ldr	r3, .L4344+8
	strh	r9, [r3, #22]	@ movhi
	ldr	r3, [sp, #24]
	ldr	r1, [sp, #36]
	ldr	ip, [sp, #32]
	str	r0, [fp, r3, asl #2]
	ldr	r2, [sp, #28]
	b	.L4223
.L4248:
	ldr	r3, [fp, #-3192]
	add	r10, r7, r9
	ldrb	r3, [r3, r10]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L4340
.L4226:
	ldr	r1, .L4344
	add	r2, sp, #72
	add	r2, r2, r9, asl #2
	ldr	r0, .L4344+20
	ldr	ip, .L4344+4
	ldr	r3, [r1, #-3192]
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
	ldrb	r2, [r1, #-3196]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L4239
	ldrb	r2, [ip, #1196]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4240
	ldrb	r1, [r1, #-2612]	@ zero_extendqisi2
	and	r2, r9, #1
	add	r3, r5, r2
	cmp	r1, #0
	beq	.L4241
	ldr	r1, [sp, #32]
	ldrh	r1, [r1]
	cmp	r1, r5
	movcs	r2, #0
	andcc	r2, r2, #1
	cmp	r2, #0
	movne	r3, r1
.L4241:
	ldr	r2, .L4344+4
	mov	r1, r4
	mov	fp, #48
	mov	r0, r9
	str	r3, [sp, #40]
	mla	fp, fp, r8, r2
	bl	__aeabi_uidiv
	ldr	r1, .L4344+52
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
.L4242:
	mov	r3, #48
	mul	r8, r3, r8
	ldr	r3, .L4344+4
	add	r3, r3, r8
	ldr	r1, [r3, #1260]
	ldr	r3, .L4344+20
	add	r8, r3, r8
	ldr	r3, .L4344
	ldrh	r2, [r8, #32]
	ldr	r3, [r3, #-236]
	str	r1, [r3, r2, asl #2]
.L4267:
	ldr	r8, .L4344
	ldrb	r3, [r8, #-2612]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4341
.L4247:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
.L4222:
	ldrh	r9, [sp, #28]
	ldr	r3, [sp, #24]
	ldr	fp, .L4344
	cmp	r9, r3
	blt	.L4248
	b	.L4342
.L4340:
	mov	r0, #0
	bl	buf_alloc
	subs	r8, r0, #0
	bne	.L4227
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	r0, r8
	bl	buf_alloc
	subs	r8, r0, #0
	beq	.L4196
.L4227:
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldr	r3, [fp, #-3192]
	strb	r2, [r3, r10]
	ldr	r2, .L4344+4
	ldrb	r3, [r2, #2835]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r2, #2835]
	add	r3, r9, r7
	strh	r3, [r8, #32]	@ movhi
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [r8, #45]
	ldr	r3, .L4344+48
	ldrh	r2, [r3]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	ldrb	r2, [fp, #477]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r10, r3
	blt	.L4228
	ldr	r3, .L4344
	ldrb	r3, [r3, #-2612]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4229
	ldr	r3, [sp, #32]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4229
	tst	r9, #1
	bne	.L4228
.L4229:
	ldr	r3, .L4344
	mvn	r1, #0
	ldr	fp, .L4344+56
	ldr	r2, [r3, #-244]
	str	r1, [r2, r10, asl #2]
	ldrb	r2, [r3, #477]	@ zero_extendqisi2
	cmp	r2, #2
	bne	.L4230
	ldr	r2, .L4344+48
	ldr	ip, .L4344+60
	ldrh	r0, [r2]
	ldr	r2, [sp, #12]
	ldrb	r2, [r2, #89]	@ zero_extendqisi2
	mul	r2, r2, r0
	sub	r2, r2, #2
	cmp	r10, r2
	bne	.L4231
	ldrh	r2, [ip]
	ldr	r1, [r3, #-244]
	ldr	r0, [r8, #4]
	str	ip, [sp, #44]
	bl	ftl_memcpy
	ldr	r3, .L4344+4
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
	b	.L4334
.L4231:
	ldrb	r2, [r3, #-3195]	@ zero_extendqisi2
	ldrh	r1, [ip]
	ldr	r3, [r3, #-244]
	mul	r0, r2, r0
	rsb	ip, r1, r0, asl #2
	mov	r1, r1, lsr #2
	ldr	r0, [r8, #4]
	mov	r2, ip
	add	r1, r3, r1, asl #2
	str	ip, [sp, #44]
	bl	ftl_memcpy
	ldr	r3, .L4344+4
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
.L4334:
	str	r2, [r3, #12]
	b	.L4232
.L4230:
	ldr	ip, .L4344+48
	ldrb	r2, [r3, #-3195]	@ zero_extendqisi2
	ldr	r0, [r8, #4]
	ldrh	r1, [ip]
	str	ip, [sp, #44]
	str	r3, [sp, #40]
	mul	r2, r2, r1
	ldr	r1, [r3, #-244]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	ldr	r0, [r8, #4]
	ldr	ip, [sp, #44]
	ldr	r3, [sp, #40]
	ldrh	r1, [ip]
	ldrb	r3, [r3, #-3195]	@ zero_extendqisi2
	ldr	ip, .L4344
	mul	r3, r3, r1
	ldr	r1, .L4344+4
	str	ip, [sp, #44]
	ldrb	r1, [r1, #2772]	@ zero_extendqisi2
	cmp	r1, r3, asr #6
	mov	r2, r3, asl #2
	ldrlt	r1, .L4344+60
	mov	r3, r2
	add	r0, r0, r3
	ldrlth	r1, [r1]
	rsblt	r2, r2, r1
	ldr	r1, [ip, #-240]
	bl	ftl_memcpy
	ldr	r3, .L4344+4
	mov	r1, #0
	ldr	r0, [r8, #12]
	ldrb	r2, [r3, #2772]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	str	r3, [sp, #40]
	bl	ftl_memset
	ldr	r2, [r8, #12]
	str	fp, [r2]
	ldr	r2, .L4344+48
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldrh	r1, [r2]
	ldr	ip, [sp, #44]
	ldrb	r2, [ip, #-3195]	@ zero_extendqisi2
	mul	r1, r2, r1
	mov	r1, r1, asl #2
	bl	js_hash
	ldr	r3, [sp, #40]
	str	r0, [fp, #4]
	ldr	r2, [r8, #12]
	ldr	r3, [r3, #1092]
	ldr	r3, [r3, #132]
	str	r3, [r2, #8]
	b	.L4232
.L4228:
	ldr	fp, .L4344
	mov	r0, r8
	mov	r1, #1
	ldr	r3, [fp, #-240]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r8, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4235
	ldr	r3, .L4344+64
	ldr	r2, [r8, #24]
	ldrb	r1, [fp, #-3202]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldr	r3, .L4344+4
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
.L4235:
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4237
	ldr	r1, .L4344+12
	movw	r2, #1223
	ldr	r0, .L4344+16
	bl	printk
	bl	dump_stack
.L4237:
	ldr	r3, [r8, #12]
	ldr	r2, .L4344+4
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #2784]
	cmp	r1, r2
	mvncs	r2, #0
	strcs	r2, [r3, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r3, #4]
	ldr	r3, .L4344
	ldr	r3, [r3, #-244]
	str	r2, [r3, r10, asl #2]
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #24]
	str	r2, [r3, #8]
.L4232:
	ldr	r3, [r8, #12]
	mov	r2, #0
	str	r2, [r3, #16]
	b	.L4226
.L4240:
	ldrb	r2, [ip, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, .L4344+68
	umull	r0, r1, r9, r2
	beq	.L4243
	mov	r1, r1, lsr #1
	ldr	r0, [sp, #16]
	ldr	ip, [sp, #12]
	uxtah	r0, r0, r1
	add	r1, r1, r1, asl #1
	rsb	r1, r1, r9
	add	r0, ip, r0, asl #1
	ldr	ip, .L4344+52
	ldrh	r0, [r0, #96]
	ldrh	ip, [ip]
	mla	r0, ip, r0, r5
	uxtah	r0, r0, r1
	str	r0, [r3, #1260]
	b	.L4244
.L4243:
	ldr	r0, [sp, #16]
	ubfx	r1, r1, #1, #16
	add	r1, r0, r1
	ldr	r0, [sp, #12]
	add	r1, r0, r1, asl #1
	ldr	r0, .L4344+52
	ldrh	r1, [r1, #96]
	ldrh	r0, [r0]
	mla	r1, r0, r1, r5
	str	r1, [r3, #1260]
	b	.L4244
.L4239:
	cmp	r2, #2
	bne	.L4242
	ldrb	r2, [r1, #-3194]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, [sp, #16]
	bne	.L4246
	ldr	r1, [sp, #12]
	add	r2, r2, r9
	add	r2, r1, r2, asl #1
	ldr	r1, .L4344+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	b	.L4335
.L4246:
	ldr	r1, [sp, #12]
	add	r2, r2, r9, lsr #1
	add	r2, r1, r2, asl #1
	ldr	r1, .L4344+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	and	r1, r9, #1
	add	r2, r2, r1
.L4335:
	orr	r2, r2, #33554432
	str	r2, [r3, #1260]
	b	.L4242
.L4341:
	ldr	r3, [sp, #32]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4247
	tst	r9, #1
	beq	.L4247
	ldr	r2, [r8, #-3192]
	mvn	r3, #0
	ldrh	r1, [sp, #36]
	ldr	r0, [sp, #36]
	strb	r3, [r2, r10]
	add	r2, sp, #72
	add	r9, r2, r9, asl #2
	ldr	r2, [r9, #-24]
	strh	r1, [r2, #32]	@ movhi
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r8, #-3192]
	strb	r1, [r2, r0]
	ldr	r2, [r8, #-240]
	str	r3, [r2, r10, asl #2]
	b	.L4247
.L4342:
	ldrb	r3, [fp, #-3196]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L4249
	ldr	r3, .L4344+4
	ldrb	r1, [r3, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [sp, #24]
	movne	r2, #0
	subne	r1, r3, #1
	beq	.L4343
.L4251:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4336
	add	ip, sp, #72
	add	r0, sp, #72
	add	ip, ip, r3, asl #2
	add	r0, r0, r3, asl #2
	ldr	r3, [ip, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [r0]
	b	.L4251
.L4343:
	ldrb	r2, [fp, #-3188]	@ zero_extendqisi2
	ldr	r3, [sp, #48]
	cmp	r2, #0
	strneb	r1, [r3, #44]
	bne	.L4337
	ldr	r1, [sp, #20]
	cmp	r1, #1
	moveq	r2, #9
	beq	.L4257
	ldr	r1, [sp, #20]
	cmp	r1, #2
	moveq	r2, #13
.L4257:
	strb	r2, [r3, #44]
.L4337:
	add	r1, sp, #60
	mov	r2, r6
	add	r0, sp, #48
	bl	sblk_xlc_prog_pages
	b	.L4253
.L4249:
	ldrb	r3, [fp, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4258
	ldrb	r3, [fp, #-3193]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4259
.L4258:
	ldr	r3, [sp, #24]
	mov	r2, #0
	sub	r1, r3, #1
	b	.L4260
.L4259:
	mov	r1, r6
	add	r0, sp, #48
	bl	sblk_3d_mlc_prog_pages
	b	.L4253
.L4260:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4336
	add	ip, sp, #72
	add	r0, sp, #72
	add	ip, ip, r3, asl #2
	add	r0, r0, r3, asl #2
	ldr	r3, [ip, #-20]
	ldr	r0, [r0, #-24]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [r0]
	b	.L4260
.L4336:
	add	r3, sp, #72
	mvn	r2, #0
	add	r1, r3, r1, asl #2
	ldr	r3, [r1, #-24]
	smulbb	r1, r4, r6
	strb	r2, [r3]
	ldr	r0, [sp, #48]
	uxtb	r1, r1
	bl	sblk_prog_page
.L4253:
	ldrb	r3, [fp, #-3188]	@ zero_extendqisi2
	ldr	r2, .L4344
	cmp	r3, #0
	ldr	r3, .L4344+4
	addne	r4, r6, r6, asl #1
	bne	.L4263
	ldrb	r1, [r2, #-3194]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r4, r6, asl #1
	bne	.L4263
	ldrb	r1, [r3, #1196]	@ zero_extendqisi2
	cmp	r1, #0
	moveq	r4, r6
	beq	.L4263
	ldrb	r2, [r2, #-2612]	@ zero_extendqisi2
	mul	r4, r4, r6
	cmp	r2, #0
	beq	.L4263
	ldr	r1, .L4344+36
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	movcc	r4, r6
.L4263:
	ldr	r2, [r3, #2804]
	ldr	r3, [r2, #52]
	add	r3, r3, r4
	str	r3, [r2, #52]
	ldr	r2, .L4344+8
	movw	r3, #2102
	ldrh	r1, [r2, r3]
	add	r4, r4, r1
	uxth	r4, r4
	strh	r4, [r2, r3]	@ movhi
	ldr	r3, .L4344+72
	ldrh	r2, [r3, #-2]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r4, r3
	ldrge	r3, .L4344+4
	movge	r2, #0
	ldrge	r3, [r3, #1092]
	strgeh	r2, [r3, #86]	@ movhi
	bl	gc_write_completed
	b	.L4196
.L4244:
	mov	r0, #48
	ldr	r3, .L4344+20
	mul	r0, r0, r8
	ldr	ip, .L4344+4
	add	r3, r3, r0
	add	r0, ip, r0
	ldrh	r1, [r3, #32]
	ldr	r3, .L4344
	ldr	r0, [r0, #1260]
	ldr	lr, [r3, #-236]
	umull	r2, r3, r9, r2
	mov	r2, r3, lsr #1
	add	r2, r2, r2, asl #1
	rsb	r2, r2, r9
	mov	r2, r2, asl #24
	add	r2, r2, #16777216
	orr	r2, r2, r0
	str	r2, [lr, r1, asl #2]
	b	.L4267
.L4196:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4345:
	.align	2
.L4344:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+2736
	.word	.LC0
	.word	.LANCHOR0+1236
	.word	.LANCHOR2
	.word	.LC295
	.word	.LC296
	.word	.LANCHOR2+2120
	.word	1431655766
	.word	.LANCHOR2+3656
	.word	.LANCHOR3-3170
	.word	.LANCHOR3-3142
	.word	-178307901
	.word	.LANCHOR3+474
	.word	.LANCHOR3-3204
	.word	-1431655765
	.word	.LANCHOR3-3168
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
	ldr	r4, .L4565+60
	cmp	r0, #1
	.pad #44
	sub	sp, sp, #44
	mov	fp, r0
	ldr	r6, .L4565+32
	ldrh	r9, [r4, r3]
	movw	r3, #2788
	ldrh	r7, [r4, r3]
	movw	r3, #2790
	ldr	r5, [r4, #1092]
	add	r7, r9, r7
	ldrh	r8, [r4, r3]
	uxth	r7, r7
	beq	.L4347
.L4358:
	ldrb	r3, [r6, #-254]	@ zero_extendqisi2
	cmp	r3, #6
	ldrls	pc, [pc, r3, asl #2]
	b	.L4470
.L4350:
	.word	.L4349
	.word	.L4351
	.word	.L4352
	.word	.L4353
	.word	.L4354
	.word	.L4471
	.word	.L4356
.L4347:
	ldr	r10, [r6, #480]
	cmp	r10, #0
	bne	.L4357
	ldr	r3, [r6, #484]
	cmp	r3, #0
	beq	.L4358
.L4357:
	ldr	r3, .L4565+32
	sub	r2, r3, #3152
	ldrh	r2, [r2, #-10]
	cmp	r7, r2, lsr #2
	bls	.L4358
	movw	r2, #2808
	ldrh	r2, [r4, r2]
	cmp	r2, r7
	bcs	.L4358
	str	r3, [sp, #28]
	bl	timer_get_time
	add	r10, r10, #20
	ldr	r3, [sp, #28]
	cmp	r10, r0
	ldr	r10, [r6, #484]
	movcc	r2, #0
	strcc	r2, [r3, #480]
	bl	timer_get_time
	add	r10, r10, #20
	cmp	r10, r0
	ldrcc	r3, .L4565+32
	movcc	r2, #0
	strcc	r2, [r3, #484]
	ldr	r3, [r4, #1092]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4470
	b	.L4358
.L4349:
	movw	r2, #2794
	movw	r3, #2796
	ldrh	r3, [r4, r3]
	add	r8, r8, r9
	ldrh	r10, [r4, r2]
	ldrh	r2, [r5, #80]
	uxth	r8, r8
	add	r10, r3, r10
	movw	r3, #65535
	cmp	r2, r3
	ldr	r9, .L4565+60
	uxth	r10, r10
	beq	.L4361
	cmp	fp, #0
	bne	.L4362
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r7, r3, asl #1
	bge	.L4470
.L4362:
	ldr	fp, .L4565+32
	sub	r9, fp, #3152
	sub	r3, fp, #3168
	sub	r0, r3, #8
	str	r3, [sp, #28]
	ldrh	r1, [r9, #-12]
	add	r1, r1, #1
	uxth	r1, r1
	strh	r1, [r9, #-12]	@ movhi
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4364
	ldr	lr, [r4, #1088]
	mov	r1, r0, asl #1
	ldr	r3, [sp, #28]
	ldr	r2, [fp, #-252]
	ldrh	r1, [lr, r1]
	ldrh	r3, [r3, #-4]
	add	r2, r2, #1
	ldr	ip, .L4565+60
	cmp	r3, r1
	str	r2, [fp, #-252]
	bcs	.L4365
	movw	r3, #1076
	ldrh	r3, [ip, r3]
	cmp	r2, r3, lsr #4
	bls	.L4364
	movw	r3, #2810
	ldrh	r3, [ip, r3]
	cmp	r3, r1
	bls	.L4364
.L4365:
	mov	r1, #0
	ldrb	r2, [r6, #-3187]	@ zero_extendqisi2
	bl	gc_add_sblk
	ldr	fp, .L4565+32
	cmp	r0, #0
	beq	.L4366
	mov	r3, #1
	strb	r3, [fp, #-254]
	mov	r3, #0
	str	r3, [fp, #-252]
	b	.L4470
.L4364:
	mov	r3, #0
	strh	r3, [r9, #-12]	@ movhi
.L4366:
	cmp	r8, #15
	movls	r9, #2
	bls	.L4367
	movw	r3, #2798
	ldrh	r2, [r9, #-8]
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	movls	r3, #0
	movhi	r3, #1
	cmp	r10, #0
	movne	r10, r3
	orreq	r10, r3, #1
	cmp	r10, #0
	movne	r9, #2
	moveq	r9, #1
.L4367:
	ldr	r3, .L4565
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4368
	ldr	r3, [r4, #1092]
	ldrb	r2, [r6, #-3187]	@ zero_extendqisi2
	str	r8, [sp]
	ldrh	r1, [r3, #124]
	ldr	r0, .L4565+4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #120]
	str	r1, [sp, #8]
	movw	r1, #2662
	ldrh	r3, [r3, #122]
	str	r3, [sp, #12]
	ldrh	r3, [r5, #80]
	str	r3, [sp, #16]
	mov	r3, r7
	bl	printk
.L4368:
	ldrb	r0, [r6, #-3187]	@ zero_extendqisi2
	mov	r1, r9
	mov	r2, #1
	bl	gc_search_src_blk
	cmp	r0, #0
	ble	.L4369
.L4370:
	mov	r3, #1
	b	.L4562
.L4369:
	ldr	r3, .L4565+32
	mov	r1, #3
	mov	r2, #1
	ldrb	r0, [r3, #-3187]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	bgt	.L4370
	b	.L4470
.L4361:
	cmp	fp, #1
	bne	.L4371
	bl	gc_scan_static_data
	ldr	r3, [r9, #1092]
	ldrh	r3, [r3, #122]
	cmp	r3, #0
	beq	.L4372
.L4373:
	mov	r3, #1
	strb	r3, [r6, #-3187]
	b	.L4562
.L4372:
	bl	gc_static_wearleveling
	cmp	r0, #0
	bne	.L4373
	bl	gc_block_vpn_scan
	cmp	r8, #0
	beq	.L4374
	cmp	r7, r10
	ldr	r5, .L4565+8
	bcs	.L4375
	ldrh	r3, [r5, #-10]
	cmp	r3, r7
	bhi	.L4376
.L4375:
	ldrh	r2, [r5, #-10]
	add	r3, r7, r10
	cmp	r3, r2
	blt	.L4376
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldr	r3, .L4565+8
	ldrh	r3, [r3, #-8]
	cmp	r2, r3
	bcc	.L4374
.L4376:
	ldr	r9, .L4565+32
	mov	r1, #16
	mov	ip, #1
	strb	ip, [r6, #-3187]
	sub	r0, r9, #3168
	str	ip, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4377
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldr	ip, [sp, #28]
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-248]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4377
	mov	r2, #0
	mov	r1, ip
	strb	r2, [r9, #-3187]
	str	r2, [r9, #-252]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4370
.L4377:
	ldr	r0, .L4565+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r9, .L4565+32
	cmp	r0, r2
	beq	.L4378
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-248]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4378
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3187]
	str	r2, [r9, #-252]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4370
.L4378:
	ldr	r1, .L4565+32
	mov	r9, #0
	ldrh	r3, [r6, #-248]
	sub	ip, r1, #3168
	add	r3, r3, #1
	ldrh	r2, [ip, #-4]
	uxth	r3, r3
	strh	r3, [r6, #-248]	@ movhi
	strh	r9, [ip, #-14]	@ movhi
	cmp	r3, r2, lsr #5
	ldr	ip, .L4565+60
	movhi	r3, #4
	strhih	r3, [r1, #-248]	@ movhi
	ldr	r3, .L4565+32
	ldr	r1, [r6, #-252]
	sub	r0, r3, #3184
	add	r1, r1, #1
	str	r1, [r6, #-252]
	strh	r9, [r0, #-2]	@ movhi
	strh	r9, [r0]	@ movhi
	movw	r0, #1076
	ldrh	r0, [r4, r0]
	cmp	r1, r0, lsr #5
	bls	.L4380
	movw	r1, #2798
	ldrh	r1, [ip, r1]
	cmp	r1, r8
	bls	.L4380
	mov	r1, r9
	ldr	r0, .L4565+16
	str	ip, [sp, #36]
	str	r3, [sp, #32]
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r1, #65535
	cmp	r0, r1
	beq	.L4475
	ldr	ip, [sp, #36]
	mov	r0, r0, asl #1
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #28]
	ldr	r1, [ip, #1088]
	ldrh	r0, [r1, r0]
	ldrb	r1, [r3, #-3195]	@ zero_extendqisi2
	mul	r2, r1, r2
	cmp	r0, r2
	strle	r9, [r3, #-252]
	ldrleb	r0, [r3, #-3187]	@ zero_extendqisi2
	movle	r1, #2
	bgt	.L4475
	b	.L4554
.L4380:
	movw	r2, #2798
	ldr	r3, .L4565+60
	ldrh	r1, [r4, r2]
	ldrh	r2, [r5, #-8]
	cmp	r1, r2
	ldrcs	r3, .L4565+32
	movcs	r1, #2
	movcs	r2, #1
	ldrcsb	r0, [r3, #-3187]	@ zero_extendqisi2
	bcs	.L4555
.L4383:
	movw	r2, #2794
	ldrh	r5, [r3, r2]
	cmp	r5, #0
	bne	.L4384
	movw	r2, #2796
	ldrh	r3, [r3, r2]
	cmp	r3, #8
	bls	.L4385
.L4384:
	ldrb	r0, [r6, #-3187]	@ zero_extendqisi2
	mov	r1, #1
.L4554:
	mov	r2, #4
.L4555:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4370
	b	.L4475
.L4374:
	ldr	r3, .L4565+32
	sub	r2, r3, #3168
	ldrh	r1, [r2, #-4]
	ldrh	r2, [r6, #-248]
	cmp	r2, r1, lsr #5
	movcs	r2, #4
	strcsh	r2, [r3, #-248]	@ movhi
	b	.L4475
.L4371:
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r3, r7
	bcs	.L4475
	ldr	r3, [r9, #1092]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4470
.L4475:
	mov	r5, #16
.L4385:
	ldr	r3, [r4, #2820]
	cmp	r3, #0
	beq	.L4387
	ldr	r3, .L4565+60
	cmp	r7, #15
	mov	r2, #0
	ldr	r0, .L4565+16
	mov	r1, #0
	str	r2, [r3, #2820]
	movhi	r3, #0
	movls	r3, #1
	cmp	r8, r2
	moveq	r3, #0
	cmp	r3, r2
	movne	r3, #1
	strb	r3, [r6, #-3187]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4390
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #8
	bhi	.L4390
	ldr	r3, .L4565+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldr	r3, .L4565+32
	ldrb	r2, [r3, #-3187]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4370
.L4390:
	ldr	r0, .L4565+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4391
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4391
	ldr	r3, .L4565+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldr	r3, .L4565+32
	ldrb	r2, [r3, #-3187]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4370
.L4391:
	ldr	r9, .L4565+32
	mov	r1, #0
	sub	r0, r9, #3168
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4387
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4387
	ldr	r3, .L4565+60
	mov	r1, #1
	ldrb	r2, [r9, #-3187]	@ zero_extendqisi2
	str	r1, [r3, #2820]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4370
.L4387:
	ldr	r2, [r4, #1092]
	mov	r1, #1
	ldr	r9, .L4565+32
	ldr	r3, .L4565+60
	ldrh	ip, [r2, #124]
	strb	r1, [r6, #-3187]
	cmp	ip, #0
	strneb	r1, [r9, #-254]
	movne	r3, #0
	strneb	r3, [r9, #-3187]
	bne	.L4393
	movw	r2, #2808
	ldrh	r0, [r3, r2]
	cmp	r7, r0
	bcs	.L4394
	cmp	r8, #0
	beq	.L4395
	cmp	r8, #16
	bls	.L4396
	sub	r2, r9, #3152
	movw	r0, #2798
	ldrh	r0, [r3, r0]
	ldrh	r2, [r2, #-8]
	cmp	r0, r2
	bhi	.L4396
	mov	r0, r1
	mov	r2, #4
	str	ip, [sp, #32]
	str	r3, [sp, #28]
	bl	gc_search_src_blk
	uxth	r0, r0
	ldr	r3, [sp, #28]
	cmp	r0, #0
	ldr	ip, [sp, #32]
	beq	.L4559
	sub	r7, r9, #3168
	mov	r1, ip
	sub	r0, r7, #8
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4468
	ldr	r3, [sp, #28]
	mov	r1, r0, asl #1
	ldr	r2, [r9, #-252]
	ldr	ip, [r3, #1088]
	add	r2, r2, #1
	str	r2, [r9, #-252]
	ldrh	r1, [ip, r1]
	ldrh	ip, [r7, #-4]
	cmp	ip, r1
	bcs	.L4400
	movw	ip, #1076
	ldrh	ip, [r3, ip]
	cmp	r2, ip, lsr #4
	bls	.L4468
	movw	r2, #2810
	ldrh	r3, [r3, r2]
	cmp	r3, r1
	bls	.L4468
.L4400:
	mov	r1, #0
	ldrb	r2, [r6, #-3187]	@ zero_extendqisi2
	bl	gc_add_sblk
	mov	r3, #1
	str	r3, [r4, #2820]
	mov	r3, #0
	str	r3, [r6, #-252]
	b	.L4468
.L4396:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r6, #-3187]	@ zero_extendqisi2
	bne	.L4468
	b	.L4558
.L4395:
	cmp	r7, #16
	strb	r8, [r9, #-3187]
	mov	r0, r8
	bhi	.L4558
	mov	r2, r1
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4468
.L4559:
	ldrb	r0, [r9, #-3187]	@ zero_extendqisi2
.L4558:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	b	.L4398
.L4394:
	cmp	fp, #1
	bne	.L4393
	cmp	r7, r0, asl #1
	bge	.L4403
	cmp	r10, r8, lsr #1
	bcs	.L4404
	sub	r9, r9, #3152
	movw	r1, #2798
	ldrh	r0, [r3, r1]
	ldrh	r1, [r9, #-8]
	cmp	r0, r1
	ldrcch	r1, [r9, #-10]
	movcc	r1, r1, lsr #2
	strcch	r1, [r3, r2]	@ movhi
	bcc	.L4393
.L4404:
	ldr	r9, .L4565+32
	mov	r1, #8
	sub	r0, r9, #3168
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4405
	ldr	r2, [r4, #1088]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #3
	movhi	r3, #0
	movls	r3, #1
	cmp	r7, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4405
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3187]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4564
.L4405:
	ldr	r0, .L4565+16
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r9, .L4565+32
	cmp	r0, r3
	mov	fp, r0
	bne	.L4406
.L4411:
	cmp	r8, #1
	bhi	.L4407
	b	.L4408
.L4406:
	movw	r1, #1076
	ldr	r3, [r9, #-252]
	ldrh	r1, [r4, r1]
	add	r3, r3, #1
	ldr	r2, .L4565+60
	str	r3, [r9, #-252]
	cmp	r3, r1, lsr #4
	mov	r3, r0, asl #1
	bls	.L4409
	ldr	ip, [r2, #1088]
	movw	lr, #2810
	ldrh	r2, [r2, lr]
	mov	r1, #0
	str	r1, [r9, #-252]
	ldrh	ip, [ip, r3]
	cmp	ip, r2
	bcs	.L4409
	mov	ip, #1
	str	r3, [sp, #32]
	mov	r2, ip
	strb	ip, [r9, #-3187]
	str	ip, [sp, #28]
	bl	gc_add_sblk
	ldr	ip, [sp, #28]
	cmp	r0, #0
	ldr	r3, [sp, #32]
	strneb	ip, [r9, #-254]
	bne	.L4393
.L4409:
	ldr	r1, [r4, #1088]
	ldr	r9, .L4565+32
	ldr	r2, .L4565+60
	ldrh	r1, [r1, r3]
	sub	r3, r9, #3168
	ldrh	r3, [r3, #-4]
	cmp	r1, r3, lsr #1
	bhi	.L4410
	mov	r0, fp
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L4564:
	mov	r3, #1
	strb	r3, [r9, #-254]
	b	.L4393
.L4410:
	movw	r3, #2794
	movw	r0, #2796
	ldrh	r0, [r2, r0]
	sub	r9, r9, #3152
	ldrh	r3, [r2, r3]
	add	r3, r3, r0
	ldrh	r0, [r9, #-10]
	cmp	r3, r0, asl #1
	ble	.L4411
	movw	r3, #2810
	ldrh	r3, [r2, r3]
	cmp	r3, r1
	bcc	.L4408
	b	.L4411
.L4407:
	ldr	r9, .L4565+32
	cmp	r8, #16
	mov	r0, #1
	strb	r0, [r6, #-3187]
	sub	r8, r9, #3152
	bls	.L4412
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldrh	r3, [r8, #-8]
	cmp	r2, r3
	bhi	.L4412
	mov	r1, r0
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r9, #-3187]	@ zero_extendqisi2
	bne	.L4413
	b	.L4557
.L4412:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4413
	ldrb	r0, [r6, #-3187]	@ zero_extendqisi2
.L4557:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
.L4413:
	ldrh	r3, [r8, #-10]
	cmp	r7, r10, lsr #1
	movw	r2, #2808
	movhi	r3, r3, lsr #2
	movls	r3, r3, lsr #1
	strh	r3, [r4, r2]	@ movhi
	b	.L4398
.L4408:
	cmp	r7, r10
	ldr	r7, .L4565+8
	bcs	.L4415
	sub	r10, r7, #16
	mov	r1, #4
	mov	r9, #0
	strb	r9, [r6, #-3187]
	mov	r0, r10
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4415
	ldr	r3, [r4, #1088]
	mov	r0, r0, asl #1
	ldrh	r1, [r7, #-20]
	ldr	r8, .L4565+60
	ldrh	r2, [r3, r0]
	ldrb	r3, [r7, #-43]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asr #1
	ldrgth	r3, [r7, #-10]
	movwgt	r2, #2808
	movgt	r3, r3, lsr #2
	strgth	r3, [r8, r2]	@ movhi
	bgt	.L4393
.L4417:
	mov	r2, #4
	mov	r0, r9
	mov	r1, #3
	bl	gc_search_src_blk
	ldrh	r3, [r7, #-10]
	movw	r2, #2808
	mov	r3, r3, lsr #1
	strh	r3, [r8, r2]	@ movhi
	uxth	r0, r0
	b	.L4398
.L4415:
	ldrh	r3, [r7, #-10]
	movw	r2, #2808
	mov	r3, r3, lsr #2
	strh	r3, [r4, r2]	@ movhi
	b	.L4393
.L4403:
	sub	r9, r9, #3152
	mov	r5, ip
	ldrh	r1, [r9, #-10]
	mov	r1, r1, lsr #2
	strh	r1, [r3, r2]	@ movhi
	b	.L4393
.L4398:
	cmp	r0, #0
	beq	.L4393
.L4468:
	mov	r3, #1
	strb	r3, [r6, #-254]
	b	.L4393
.L4351:
	movw	r5, #2828
	movw	r3, #65535
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	bne	.L4418
	bl	gc_get_src_blk
	ldr	r3, .L4565+60
	strh	r0, [r3, r5]	@ movhi
.L4418:
	movw	r3, #2828
	movw	r0, #65535
	ldrh	r1, [r4, r3]
	ldr	r2, .L4565+60
	cmp	r1, r0
	beq	.L4419
	movw	r0, #1076
	ldrh	r0, [r2, r0]
	cmp	r0, r1
	mvnls	r1, #0
	strlsh	r1, [r2, r3]	@ movhi
.L4419:
	movw	r3, #2828
	movw	r1, #65535
	ldrh	r3, [r4, r3]
	ldr	r2, .L4565+60
	cmp	r3, r1
	beq	.L4563
	ldr	r0, .L4565+56
	ldr	r1, [r2, #1080]
	ldrh	ip, [r0, #52]
	add	r1, r1, r3, asl #2
	cmp	ip, #0
	addne	r2, r2, #2880
	movne	r0, #0
	beq	.L4421
.L4422:
	uxth	lr, r0
	cmp	lr, ip
	bcs	.L4421
	ldrh	lr, [r2, #2]!
	add	r0, r0, #1
	cmp	lr, r3
	bne	.L4422
.L4426:
	movw	r3, #2828
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	b	.L4470
.L4421:
	ldrb	r2, [r1, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r2, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r2, #224
	movne	r2, r1
	orreq	r2, r1, #1
	cmp	r2, #0
	beq	.L4424
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4426
	ldr	r1, .L4565+64
	movw	r2, #3153
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
	b	.L4426
.L4424:
	mov	r3, #2
	b	.L4562
.L4352:
	bl	gc_scan_src_blk
	cmn	r0, #1
	moveq	r3, #3
	beq	.L4562
	movw	r3, #2828
	ldr	r2, .L4565+56
	ldrh	r3, [r4, r3]
	movw	r1, #65535
	cmp	r3, r1
	mov	r7, r2
	beq	.L4370
	ldrh	r1, [r2, #20]
	cmp	r1, #0
	movne	r3, #4
	strneb	r3, [r6, #-254]
	movne	r3, #0
	strneh	r3, [r2, #22]	@ movhi
	bne	.L4470
.L4428:
	mov	r2, #1
	strb	r2, [r6, #-254]
	ldr	r2, .L4565+60
	mov	r3, r3, asl #1
	ldr	r2, [r2, #1088]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4429
	ldr	r1, .L4565+64
	movw	r2, #3183
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
.L4429:
	movw	r5, #2828
	ldr	r6, .L4565+56
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #1088]
	mov	r5, #0
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r7, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strlsh	r3, [r6, #26]	@ movhi
	bls	.L4426
	strh	r5, [r6, #26]	@ movhi
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, r5
	bl	ftl_info_flush
	b	.L4426
.L4353:
	ldr	r6, .L4565+56
.L4526:
	bl	gc_scan_src_blk_one_page
	ldr	r3, .L4565+20
	ldrh	r1, [r6, #2]
	ldr	r2, .L4565+60
	ldrh	r3, [r3]
	ldr	r0, .L4565+32
	cmp	r1, r3
	ldr	r5, .L4565+56
	bcs	.L4432
	cmp	r7, #7
	bls	.L4526
	b	.L4470
.L4432:
	ldrh	r3, [r5, #20]
	ldrh	r1, [r5]
	cmp	r3, #0
	beq	.L4433
	mov	ip, #4
	strb	ip, [r0, #-254]
	mov	r0, #0
	strh	r0, [r5, #22]	@ movhi
	ldr	r0, [r2, #1088]
	mov	r2, r1, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	beq	.L4434
	ldr	r0, .L4565
	ldr	r0, [r0]
	tst	r0, #1024
	beq	.L4434
	ldr	r0, .L4565+24
	bl	printk
.L4434:
	movw	r3, #2828
	ldr	r2, [r4, #1088]
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L4435
	ldr	r1, .L4565+64
	movw	r2, #3226
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
.L4435:
	movw	r3, #2828
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1088]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	b	.L4470
.L4433:
	mov	r3, #1
	strb	r3, [r0, #-254]
	ldr	r3, .L4565
	ldr	r6, [r2, #1080]
	ldr	r3, [r3]
	add	r6, r6, r1, asl #2
	tst	r3, #256
	beq	.L4436
	ldrb	r2, [r6, #2]	@ zero_extendqisi2
	ldr	r0, .L4565+28
	mov	r2, r2, lsr #5
	bl	printk
.L4436:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4437
	ldr	r1, .L4565+64
	movw	r2, #3236
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
.L4437:
	movw	r7, #2828
	ldr	r6, .L4565+56
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
	bls	.L4470
	b	.L4561
.L4566:
	.align	2
.L4565:
	.word	.LANCHOR2
	.word	.LC297
	.word	.LANCHOR3-3152
	.word	.LANCHOR3-3180
	.word	.LANCHOR3-3176
	.word	.LANCHOR3-3172
	.word	.LC298
	.word	.LC299
	.word	.LANCHOR3
	.word	.LANCHOR0+4936
	.word	.LANCHOR0+4096
	.word	1145785929
	.word	.LANCHOR3-3200
	.word	.LANCHOR3-3168
	.word	.LANCHOR0+2828
	.word	.LANCHOR0
	.word	.LANCHOR1+2752
	.word	.LC0
.L4354:
	cmp	fp, #0
	bne	.L4439
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bcc	.L4470
.L4439:
	ldrh	r2, [r5, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4440
	ldrb	r8, [r6, #-3187]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L4440
	ldr	r9, .L4565+56
	bl	ftl_flush
	movw	r3, #2106
	mov	r1, #5
	ldrh	r0, [r9, r3]
	cmp	r0, #0
	movne	r0, r8
	bl	zftl_gc_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r7, r0
	beq	.L4443
	ldr	r8, [r4, #1080]
	add	r8, r8, r0, asl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L4444
	ldr	r1, .L4565+64
	movw	r2, #3270
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
.L4444:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	and	r3, r3, #15
	orr	r3, r3, #176
	strb	r3, [r8, #2]
.L4469:
	mov	r0, r7
	mov	r1, #1
	bl	ftl_erase_sblk
	mov	r3, #5
	add	r1, r5, #96
	strb	r3, [r5, #84]
	mov	r0, r7
	ldr	r10, .L4565+52
	bl	ftl_get_blk_list_in_sblk
	mov	r8, #0
	strh	r7, [r5, #80]	@ movhi
	mov	r1, #255
	ldrh	r3, [r10, #-4]
	ldrh	r2, [r10, #-2]
	strh	r8, [r5, #82]	@ movhi
	strb	r8, [r5, #85]
	strh	r8, [r5, #90]	@ movhi
	uxtb	r0, r0
	strb	r0, [r5, #89]
	smulbb	r0, r3, r0
	ldrb	r3, [r6, #-3195]	@ zero_extendqisi2
	mul	r2, r3, r2
	strh	r0, [r5, #86]	@ movhi
	mvn	r5, #0
	ldr	r0, [r6, #-244]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrb	r3, [r6, #-3195]	@ zero_extendqisi2
	ldrh	r2, [r10, #-2]
	mov	r1, #255
	ldr	r0, [r6, #-240]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r3, [r10, #-2]
	ldrb	r2, [r6, #-3195]	@ zero_extendqisi2
	mov	r1, #255
	ldr	r0, [r6, #-3192]
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
	movw	r3, #2108
	strh	r8, [r9, r3]	@ movhi
	ldr	r3, .L4565+40
	str	r5, [r3, #844]
	bl	ftl_info_flush
	b	.L4470
.L4440:
	cmp	fp, #1
	ldr	r9, .L4565+56
	ldr	r10, .L4565+36
	movne	r8, #1
	moveq	r8, #4
	cmp	r7, #15
	addls	r8, r8, #4
.L4447:
	sub	r8, r8, #1
	uxtb	r8, r8
	cmp	r8, #255
	beq	.L4470
	bl	gc_do_copy_back
	ldrb	r3, [r6, #-3187]	@ zero_extendqisi2
	ldr	fp, .L4565+32
	cmp	r3, #0
	ldr	r7, .L4565+56
	bne	.L4448
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #3
	bhi	.L4449
	bl	ftl_write_commit
.L4449:
	ldrh	r2, [r9, #22]
	ldrh	r3, [r9, #20]
	cmp	r2, r3
	bcc	.L4447
	mov	r3, #1
	strb	r3, [r6, #-254]
	bl	ftl_write_commit
	bl	ftl_flush
	ldr	r3, .L4565+56
	ldr	r2, .L4565+60
	ldrh	r3, [r3]
	ldr	r2, [r2, #1088]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4451
	ldr	r1, .L4565+64
	movw	r2, #3354
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
.L4451:
	movw	r3, #2828
	ldr	r1, [r4, #1088]
	ldrh	r0, [r4, r3]
	mov	r2, r0, asl #1
	ldrh	ip, [r1, r2]
	cmp	ip, #0
	bne	.L4452
	strh	ip, [r1, r2]	@ movhi
	ldr	r2, .L4565+60
	ldrh	r0, [r2, r3]
	bl	ftl_free_sblk
	b	.L4426
.L4452:
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	b	.L4426
.L4448:
	ldrh	r3, [r10]
	cmp	r3, #0
	beq	.L4453
	ldr	r3, .L4565+36
	mov	r2, #0
	ldr	r8, .L4565+40
	strh	r2, [r3]	@ movhi
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	r0, [r8, #844]
	cmn	r0, #1
	beq	.L4454
	ldrb	r3, [fp, #-2612]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4455
	ldrb	r3, [fp, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4456
.L4455:
	ldr	r3, [r4, #2804]
	ldr	r9, .L4565+60
	ldr	r2, [r3, #156]
	ldr	r3, .L4565+44
	cmp	r2, r3
	bne	.L4456
	ldr	r10, .L4565+48
	ldrb	r3, [r9, #1189]	@ zero_extendqisi2
	ldrb	r1, [r6, #-3202]	@ zero_extendqisi2
	ldrh	r2, [r10, #-4]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r2, r0, lsr r2
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r5, [r9, #1080]
	mov	ip, r0, asl #2
	add	r1, r5, ip
	ldrb	r3, [r1, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L4454
	movw	r2, #2788
	ldrh	lr, [r10, #38]
	ldrh	r2, [r9, r2]
	add	r2, r2, #8
	cmp	r2, lr
	bge	.L4454
	bfc	r3, #3, #2
	movw	r2, #1084
	strb	r3, [r1, #2]
	ldr	r3, [r5, r0, asl #2]
	ldrh	r2, [r9, r2]
	ubfx	r3, r3, #11, #8
	ldrh	r1, [r5, ip]
	mul	r2, r2, r3
	ubfx	lr, r1, #0, #11
	mov	r3, r3, lsr #3
	add	r2, r2, r2, asl #1
	add	r2, lr, r2, asr #2
	bfi	r1, r2, #0, #11
	strh	r1, [r5, ip]	@ movhi
	ldr	r2, [r5, r0, asl #2]
	bfi	r2, r3, #11, #8
	str	r2, [r5, r0, asl #2]
	b	.L4454
.L4456:
	bl	ftl_mask_bad_block
.L4454:
	ldr	r3, [r4, #1092]
	mov	r5, #0
	str	r5, [r8, #844]
	strh	r5, [r7, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r0, [r4, #2836]
	ldr	r2, [r4, #1092]
	mvn	r3, #0
	ldr	r1, [r4, #2804]
	cmp	r0, r5
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	beq	.L4457
	bl	zbuf_free
.L4457:
	str	r5, [r4, #2836]
	bl	flt_sys_flush
	ldr	r1, .L4565+64
	movw	r2, #3413
	ldr	r0, .L4565+68
	strb	r5, [r6, #-254]
	bl	printk
	bl	dump_stack
	b	.L4470
.L4453:
	ldrh	r3, [r5, #86]
	ldrh	r2, [r9, #22]
	cmp	r3, #1
	ldrh	r3, [r9, #20]
	bls	.L4458
	cmp	r2, r3
	bcc	.L4447
	mov	r3, #1
	strb	r3, [fp, #-254]
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L4470
.L4458:
	cmp	r2, r3
	mov	r1, #5
	strb	r1, [fp, #-254]
	bcc	.L4459
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L4459:
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldrh	r2, [r5, #80]
	ldr	r3, [r4, #1092]
	strh	r2, [r3, #128]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r1, .L4565+52
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	mov	r3, #0
	strh	r3, [r7, #12]	@ movhi
	ldrh	r3, [r1, #-4]
	cmp	r0, #0
	ldr	r2, .L4565+56
	ldrneh	r1, [r1, #-2]
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r6, #-3196]	@ zero_extendqisi2
	strneh	r1, [r2, #14]	@ movhi
	movne	r1, #1
	strh	r3, [r7, #16]	@ movhi
	strneh	r1, [r2, #16]	@ movhi
	cmp	r3, #2
	bne	.L4462
	ldrh	r3, [r7, #14]
	mov	r3, r3, asl #1
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r6, #-3194]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L4565+56
	moveq	r2, #1
	streqh	r2, [r3, #16]	@ movhi
.L4462:
	mov	r3, #0
	strh	r3, [r7, #18]	@ movhi
	b	.L4470
.L4471:
	ldr	r8, .L4565+56
	mov	r5, #0
.L4355:
	bl	gc_check_data_one_wl
	subs	r10, r0, #0
	beq	.L4465
	ldr	r3, .L4565+56
	mov	r5, #0
	strh	r5, [r3, #52]	@ movhi
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
	str	r5, [r4, #2836]
	strb	r5, [r6, #-254]
.L4561:
	bl	flt_sys_flush
	b	.L4470
.L4465:
	ldrh	r2, [r8, #12]
	ldrh	r3, [r8, #14]
	ldr	r9, .L4565+60
	cmp	r2, r3
	bcc	.L4466
	ldr	r0, [r9, #2836]
	mov	r3, #6
	strb	r3, [r6, #-254]
	bl	zbuf_free
	str	r10, [r9, #2836]
	b	.L4470
.L4466:
	cmp	r7, #15
	bls	.L4355
	cmp	fp, #1
	bne	.L4470
	add	r5, r5, #1
	uxtb	r5, r5
	cmp	r5, #4
	bls	.L4355
	b	.L4470
.L4356:
	bl	gc_update_l2p_map_new
	mvn	r7, #0
	bl	gc_free_src_blk
	bl	ftl_flush
	bl	pm_flush
	strh	r7, [r5, #80]	@ movhi
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2804]
	mov	r0, #0
	strh	r7, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L4563:
	mov	r3, #0
.L4562:
	strb	r3, [r6, #-254]
.L4470:
	mov	r0, #16
	b	.L4552
.L4393:
	mov	r0, r5
	b	.L4552
.L4443:
	ldr	r1, .L4565+64
	movw	r2, #3277
	ldr	r0, .L4565+68
	bl	printk
	bl	dump_stack
	b	.L4469
.L4552:
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
	ldr	r5, .L4651
	mvn	r3, #0
	ldr	r4, .L4651+4
	mov	r6, #0
	ldr	r1, .L4651+8
	sub	r7, r5, #3168
	ldr	r0, .L4651+12
	strb	r3, [r4, #2824]
	strb	r3, [r5, #-2606]
	strb	r3, [r5, #-168]
	str	r3, [r5, #488]
	strb	r6, [r4, #2800]
	strb	r6, [r5, #-2605]
	strb	r6, [r4, #2801]
	bl	printk
	ldr	r8, .L4651+16
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	ldrb	fp, [r8, #13]	@ zero_extendqisi2
	strb	fp, [r4, #2772]
	mov	r9, fp, asl #9
	uxth	r9, r9
	str	r3, [sp, #4]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	strb	r3, [r5, #-3196]
	ldrh	r3, [r8, #14]
	strh	r3, [r7, #-2]	@ movhi
	mov	r0, r3
	str	r3, [sp, #8]
	bl	__aeabi_idiv
	ldrb	r1, [r4, #1101]	@ zero_extendqisi2
	ldrh	r10, [r8, #18]
	ldrh	ip, [r4, #2]
	strb	r1, [r5, #-3140]
	strh	r0, [r7, #-4]	@ movhi
	mov	r2, r0
	ldrb	r0, [r8, #17]	@ zero_extendqisi2
	ldr	r3, [sp, #8]
	smulbb	r1, r0, r1
	strb	r0, [r5, #-3202]
	mov	r0, #1
	uxtb	r7, r1
	movw	r1, #1076
	strh	r10, [r4, r1]	@ movhi
	movw	r1, #474
	strh	r9, [r5, r1]	@ movhi
	sub	r1, r5, #3136
	strb	r7, [r5, #-3195]
	strh	ip, [r1, #-6]	@ movhi
.L4568:
	cmp	r0, ip
	uxth	r1, r6
	add	r6, r6, #1
	movls	r0, r0, asl #1
	bls	.L4568
.L4649:
	ldr	r0, .L4651+20
	sub	r1, r1, #1
	strh	r1, [r0, #-4]	@ movhi
	mov	r0, #0
	mul	r1, r3, fp
	mul	r6, r10, r1
	str	r1, [sp, #8]
	mov	r1, #1
	mov	ip, r6, lsr #21
.L4570:
	cmp	r1, ip
	uxth	lr, r0
	add	r0, r0, #1
	movls	r1, r1, asl #1
	bls	.L4570
.L4650:
	ldr	r1, .L4651+24
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
	ldr	r6, .L4651
	mul	r8, lr, r1
	mov	r1, fp
	add	fp, r6, #496
	add	ip, r8, #24576
	str	ip, [r6, #492]
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
	sub	r2, r6, #3152
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #16]
	strh	r0, [r6, #-224]	@ movhi
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
	strh	r10, [r2, #-10]	@ movhi
	strlsh	r1, [r2, #-10]	@ movhi
	ldr	r1, .L4651
	strh	r0, [r2, #-8]	@ movhi
	mov	r0, #2000
	sub	r2, r1, #2608
	strh	r0, [r2]	@ movhi
	mov	r0, #50
	strh	r0, [r2, #-2]	@ movhi
	mov	r0, #256
	strh	r0, [r6, #-228]	@ movhi
	mov	r0, #48
	strh	r0, [r6, #-226]	@ movhi
	mov	r0, #32
	strh	r0, [r4, ip]	@ movhi
	ldr	ip, [sp, #4]
	cmp	ip, #2
	mov	ip, r2
	beq	.L4573
	ldrb	lr, [r1, #-3188]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L4574
.L4573:
	mov	r1, #150
	strh	r1, [ip, #-2]	@ movhi
	mov	r1, #64
	strh	r1, [r6, #-226]	@ movhi
	movw	r1, #1084
	mov	lr, #12
	strh	lr, [r4, r1]	@ movhi
	ldrb	lr, [r4]	@ zero_extendqisi2
	ldr	r2, .L4651
	cmp	lr, #0
	sub	r0, r2, #2608
	bne	.L4575
	ldr	lr, .L4651+4
	mov	r10, #4
	strh	r10, [lr, r1]	@ movhi
	mov	r1, #600
	strh	r1, [r0]	@ movhi
	mov	r1, #128
	strh	r1, [r2, #-228]	@ movhi
.L4575:
	ldrb	r2, [r4, #1197]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, #200
	strneh	r2, [ip, #-2]	@ movhi
	movne	r2, #2000
	strneh	r2, [ip]	@ movhi
	b	.L4577
.L4574:
	ldr	ip, .L4651+4
	ldrb	ip, [ip, #1196]	@ zero_extendqisi2
	cmp	ip, #0
	strneh	r0, [r2, #-2]	@ movhi
	strneh	r0, [r1, #-226]	@ movhi
	movne	ip, #1200
	strneh	ip, [r2]	@ movhi
.L4577:
	mul	r3, r7, r3
	ldr	r7, .L4651+16
	mov	r2, #0
	str	r2, [r4, #2820]
	mov	r2, #1
	strb	r2, [r6, #477]
	cmp	r9, r3, asl #2
	ldrlt	r3, .L4651
	movlt	r2, #2
	strltb	r2, [r3, #477]
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4580
	ldr	r0, .L4651+28
	mov	r1, r8
	bl	printk
.L4580:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4581
	ldr	r0, .L4651+32
	ldr	r1, [r4, #2780]
	bl	printk
.L4581:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4582
	ldr	r0, .L4651+36
	ldr	r1, [r4, #2784]
	bl	printk
.L4582:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4583
	ldr	r0, .L4651+40
	ldr	r1, [r6, #492]
	bl	printk
.L4583:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4584
	ldr	r0, .L4651+44
	ldrh	r1, [fp]
	bl	printk
.L4584:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4585
	movw	r3, #474
	ldr	r0, .L4651+48
	ldrh	r1, [r6, r3]
	bl	printk
.L4585:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4586
	ldr	r0, .L4651+52
	ldrh	r1, [r6, #-224]
	bl	printk
.L4586:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4587
	ldr	r3, .L4651+56
	ldr	r0, .L4651+60
	ldrh	r1, [r3, #-10]
	bl	printk
.L4587:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4588
	ldr	r3, .L4651+56
	ldr	r0, .L4651+64
	ldrh	r1, [r3, #-8]
	bl	printk
.L4588:
	bl	zbuf_init
	mov	r0, #16384
	bl	ftl_malloc
	movw	r9, #1076
	ldr	r8, .L4651+68
	str	r0, [r5, #140]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #148]
	mov	r0, #16384
	bl	ftl_malloc
	ldrh	r3, [r4, r9]
	str	r0, [r5, #500]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r5, #-3195]	@ zero_extendqisi2
	str	r0, [r4, #1036]
	ldrh	r0, [r8, #-4]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r5, #-3195]	@ zero_extendqisi2
	ldrh	r1, [r8, #-4]
	ldr	r2, [r5, #148]
	mul	r1, r3, r1
	str	r2, [r5, #-2624]
	add	r3, r2, r1, asl #3
	str	r3, [r4, #1088]
	str	r0, [r5, #-2620]
	ldrh	r5, [r4, r9]
	ldr	r0, [r7]
	mov	r5, r5, lsr #1
	tst	r0, #4096
	add	r1, r5, r1, asl #1
	add	r1, r2, r1, asl #2
	str	r1, [r4, #1092]
	beq	.L4589
	ldr	r0, .L4651+72
	bl	printk
.L4589:
	movw	r3, #1076
	ldrh	r5, [r8, #-4]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r6, #-3195]	@ zero_extendqisi2
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
	beq	.L4590
	ldr	ip, .L4651
	movw	r3, #474
	ldr	r0, .L4651+76
	mov	r1, r5
	mov	r2, fp
	ldrh	r3, [ip, r3]
	bl	printk
.L4590:
	movw	r3, #474
	ldrh	r3, [r6, r3]
	cmp	fp, r3
	cmpls	r5, r3
	movhi	fp, #1
	movls	fp, #0
	bls	.L4591
.L4647:
	b	.L4647
.L4591:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	r0, #1
	beq	.L4593
	bl	ftl_ext_info_init
	bl	gc_init
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
	ldr	r3, .L4651
	ldrb	r3, [r3, #145]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, .L4651+4
	ldrne	r2, [r3, #2804]
	ldrne	r3, [r2, #68]
	addne	r3, r3, #1
	strne	r3, [r2, #68]
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	ldr	r3, [r4, #1092]
	ldr	r2, .L4651+4
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4599
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #7
	ble	.L4599
.L4597:
	mov	r0, #0
	b	.L4593
.L4599:
	ldr	r6, .L4651+80
	mov	r5, #16384
.L4630:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	ldr	r3, [r4, #1092]
	ldrh	r2, [r3, #124]
	cmp	r2, #0
	bne	.L4596
	ldrh	r2, [r3, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4596
	ldr	r2, .L4651+84
	ldrh	r3, [r6]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4597
.L4596:
	subs	r5, r5, #1
	bne	.L4630
	b	.L4597
.L4593:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4652:
	.align	2
.L4651:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC2
	.word	.LC1
	.word	.LANCHOR2
	.word	.LANCHOR3-3200
	.word	1892352
	.word	.LC300
	.word	.LC301
	.word	.LC302
	.word	.LC303
	.word	.LC304
	.word	.LC305
	.word	.LC306
	.word	.LANCHOR3-3152
	.word	.LC307
	.word	.LC308
	.word	.LANCHOR3-3168
	.word	.LC309
	.word	.LC310
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
	ldr	r3, .L4669
	mov	r5, #0
	ldr	r4, .L4669+4
	ldr	r3, [r3]
	str	r5, [r4, #-212]
	strb	r5, [r4, #504]
	str	r3, [r4, #-216]
	bl	ftl_malloc
	cmp	r0, r5
	str	r0, [r4, #-208]
	bne	.L4654
.L4656:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4654:
	mov	r0, #2048
	bl	ftl_malloc
	add	r1, r4, #512
	str	r5, [r4, #-204]
	str	r5, [r4, #512]
	str	r0, [r4, #508]
	sub	r0, r4, #204
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #-204]
	cmp	r3, #0
	beq	.L4656
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r4, #508]
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	r3, [r4, #-208]
	ldr	r2, .L4669+8
	ldr	r0, [r4, #-204]
	str	r2, [r3, #40]
	ldr	r2, .L4669+12
	str	r2, [r3, #32]
	ldr	r2, .L4669+16
	str	r2, [r3, #44]
	ldr	r2, .L4669+20
	str	r2, [r3, #48]
	ldr	r2, .L4669+24
	str	r2, [r3, #36]
	ldr	r2, .L4669+28
	str	r2, [r3, #56]
	ldr	r2, .L4669+32
	str	r2, [r3, #20]
	ldr	r2, .L4669+36
	str	r2, [r3, #24]
	ldr	r2, .L4669+40
	str	r2, [r3, #12]
	ldr	r2, .L4669+44
	str	r2, [r3, #16]
	ldr	r2, .L4669+48
	str	r2, [r3, #4]
	ldr	r2, .L4669+52
	str	r2, [r3, #8]
	ldr	r2, .L4669+56
	str	r2, [r3, #60]
	ldr	r2, .L4669+60
	str	r2, [r3, #64]
	ldr	r2, .L4669+64
	str	r2, [r3, #52]
	ldr	r2, .L4669+68
	str	r2, [r3, #28]
	bl	nand_flash_init
	subs	r7, r0, #0
	bne	.L4657
	bl	zftl_init
	mov	r5, r0
	bl	zftl_proc_debug_init
	mov	r3, #1
	strb	r3, [r4, #504]
	b	.L4658
.L4657:
	ldr	r3, [r4, #-208]
	mov	r6, r4
	ldr	r2, .L4669+72
	ldr	r0, [r4, #-204]
	ldr	r4, .L4669+76
	str	r2, [r3, #40]
	ldr	r2, .L4669+80
	str	r2, [r3, #32]
	ldr	r2, .L4669+84
	str	r2, [r3, #44]
	ldr	r2, .L4669+88
	str	r2, [r3, #48]
	ldr	r2, .L4669+92
	str	r2, [r3, #36]
	ldr	r2, .L4669+96
	str	r2, [r3, #56]
	ldr	r2, .L4669+100
	str	r2, [r3, #20]
	ldr	r2, .L4669+104
	str	r2, [r3, #24]
	ldr	r2, .L4669+108
	str	r2, [r3, #12]
	ldr	r2, .L4669+112
	str	r2, [r3, #16]
	ldr	r2, .L4669+116
	str	r2, [r3, #4]
	ldr	r2, .L4669+120
	str	r2, [r3, #8]
	ldr	r2, .L4669+124
	str	r2, [r3, #60]
	ldr	r2, .L4669+128
	str	r2, [r3, #64]
	ldr	r2, .L4669+132
	str	r2, [r3, #52]
	ldr	r2, .L4669+136
	str	r2, [r3, #28]
	bl	FlashInit
	cmn	r7, #2
	mov	r5, r0
	bne	.L4659
	ldr	r1, .L4669+140
	mov	r2, #32
	add	r0, r4, #4
	bl	ftl_memcpy
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrh	r3, [r4, #14]
	strh	r3, [r4, #30]	@ movhi
.L4659:
	ldr	r3, .L4669+144
	ldr	r2, [r3]
	ldr	r3, .L4669+148
	cmp	r2, r3
	ldr	r2, .L4669+152
	bne	.L4660
	ldr	r3, .L4669+156
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4661
.L4660:
	mov	r3, #0
	strb	r3, [r2, #1102]
	strb	r3, [r2]
.L4661:
	ldr	r1, .L4669+160
	mov	r0, #1
	ldrh	ip, [r4, #14]
	mov	r3, #0
	str	r3, [r6, #-180]
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1100]
	ldr	r1, .L4669+164
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1172]
.L4662:
	cmp	r0, ip
	uxth	r1, r3
	add	r3, r3, #1
	movls	r0, r0, asl #1
	bls	.L4662
.L4668:
	ldr	r3, .L4669+168
	sub	r1, r1, #1
	strh	r1, [r3, #-4]	@ movhi
	mov	r3, #0
	cmp	r5, r3
	strb	r3, [r2, #1135]
	bne	.L4658
	bl	FtlInit
	mov	r5, r0
.L4658:
	mov	r1, r5
	ldr	r0, .L4669+172
	bl	printk
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4670:
	.align	2
.L4669:
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
	.word	.LANCHOR3-3200
	.word	.LC311
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
	ldr	r3, .L4706
	.pad #28
	sub	sp, sp, #28
	mov	r5, r0
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4672
	ldr	r3, [r9]
	mov	r1, r5
	ldr	r0, .L4706+4
	mov	r2, r4
	str	r3, [sp]
	mov	r3, r6
	bl	printk
.L4672:
	cmp	r5, #0
	ldreq	r3, .L4706+8
	moveq	r5, #24576
	ldreq	r3, [r3, #1032]
	beq	.L4674
.L4673:
	cmp	r5, #3
	bhi	.L4692
	mov	r5, r5, asl #13
	mov	r3, #8192
.L4674:
	cmp	r6, r3
	cmpls	r4, r3
	movcs	fp, #1
	movcc	fp, #0
	bcs	.L4692
	add	r1, r4, r6
	cmp	r1, r3
	bhi	.L4692
	add	r4, r5, r4
	ldr	r5, .L4706+8
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
.L4676:
	cmp	r7, #0
	beq	.L4704
	ldrb	r3, [r5, #2800]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4677
	ldr	r3, .L4706+8
	ldrb	r3, [r3, #2774]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4677
	bl	ftl_write_commit
.L4677:
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	bne	.L4678
	bl	ftl_write_commit
	b	.L4676
.L4678:
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
	beq	.L4681
	cmp	r0, #0
	beq	.L4682
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
	bhi	.L4681
	b	.L4703
.L4682:
	cmp	r1, #0
	beq	.L4681
	smulbb	r2, r8, r2
	ldr	r3, [sp, #16]
	rsb	r2, r2, r3
.L4703:
	strb	r2, [fp, #40]
.L4681:
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
	b	.L4676
.L4704:
	bl	ftl_write_commit
	mov	r0, r7
	mov	r1, #1
	bl	zftl_do_gc
	ldr	r3, [r5, #1092]
	ldr	r2, .L4706+8
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4686
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #11
	bgt	.L4687
.L4686:
	mov	r0, #0
	mov	r1, #1
	bl	zftl_do_gc
.L4687:
	ldr	r4, .L4706+12
.L4688:
	ldr	r2, .L4706+16
	ldrh	r3, [r4]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4705
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	b	.L4688
.L4705:
	bl	timer_get_time
	ldr	r3, .L4706+20
	str	r0, [r3, #484]
	mov	r0, #0
	b	.L4675
.L4692:
	mvn	r0, #0
.L4675:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4707:
	.align	2
.L4706:
	.word	.LANCHOR2
	.word	.LC312
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
	ldr	r6, .L4762
	.pad #36
	sub	sp, sp, #36
	ldr	r5, [r6, #1032]
	cmp	r1, r5
	cmpls	r0, r5
	movcs	r2, #1
	movcc	r2, #0
	bcs	.L4733
	add	r3, r0, r1
	cmp	r3, r5
	bhi	.L4733
	ldr	r3, .L4762+4
	add	r7, r0, #24576
	ldr	fp, .L4762+8
	ldr	r0, [r3]
	ldr	r5, [fp, #516]
	mov	r9, fp
	tst	r0, #4096
	str	r3, [sp, #16]
	add	r1, r1, r5
	str	r1, [fp, #516]
	beq	.L4713
	str	r2, [sp]
	mov	r3, r4
	ldr	r0, .L4762+12
	mov	r2, r7
	bl	printk
.L4713:
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
	beq	.L4714
	rsb	r8, fp, r8
	cmp	r8, r4
	movcs	r8, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4715
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4715:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	uxth	r3, r8
	str	r3, [sp, #12]
	beq	.L4717
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4717
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
.L4717:
	ldr	r3, [sp, #12]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L4714:
	cmp	r4, #0
	beq	.L4719
	bl	ftl_flush
.L4719:
	ldr	r8, .L4762
	mvn	r3, #0
	ldr	fp, .L4762+16
	str	r3, [sp, #28]
.L4720:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L4761
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	beq	.L4721
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4723
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
	b	.L4760
.L4721:
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4723
	add	r1, sp, #28
	mov	r2, #1
	mov	r0, r5
	bl	pm_log2phys
	ldrb	r3, [r8, #1189]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r9, #-3202]	@ zero_extendqisi2
	rsb	r3, r0, r3
	ldr	r2, [sp, #24]
	mov	r2, r2, lsr r0
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L4760:
	ldr	r2, [r8, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4723:
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	add	r5, r5, #1
	rsb	r4, r3, r4
	b	.L4720
.L4761:
	cmp	r4, #0
	beq	.L4727
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4728
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4728:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4727
	mov	r0, #0
	bl	buf_alloc
	subs	r8, r0, #0
	beq	.L4727
	mov	r3, #0
	strb	r3, [r8, #41]
	ldrb	r3, [r6, #2772]	@ zero_extendqisi2
	str	r5, [r8, #20]
	cmp	r4, r3
	strb	r4, [r8, #40]
	str	r10, [r8, #16]
	bcc	.L4730
	ldr	r1, .L4762+20
	movw	r2, #1477
	ldr	r0, .L4762+24
	bl	printk
	bl	dump_stack
.L4730:
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
.L4727:
	ldr	r1, [r9, #516]
	cmp	r1, #8192
	bls	.L4734
	ldr	r3, [sp, #16]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4731
	mov	r3, #0
	ldr	r0, .L4762+12
	str	r3, [sp]
	mov	r2, r7
	mov	r3, r4
	bl	printk
.L4731:
	mov	r4, #0
	str	r4, [r9, #516]
	bl	flt_sys_flush
	mov	r3, #1
	str	r3, [r6, #2820]
	b	.L4734
.L4733:
	mvn	r0, #0
	b	.L4712
.L4734:
	mov	r0, #0
.L4712:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4763:
	.align	2
.L4762:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC313
	.word	.LANCHOR3-3204
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
	ldr	r4, .L4770
	mov	r2, #4
	ldr	r0, .L4770+4
	mov	r6, #0
	ldr	r5, .L4770+8
	movw	r7, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4770+12
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r5, #-224]
	bl	rknand_print_hex
.L4765:
	ldrh	r2, [r5, #-224]
	uxth	r3, r6
	cmp	r2, r3
	bls	.L4769
	add	r3, r3, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r7
	beq	.L4766
	mov	r1, #0
	bl	ftl_sblk_dump
.L4766:
	add	r6, r6, #1
	b	.L4765
.L4769:
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4771:
	.align	2
.L4770:
	.word	.LANCHOR0
	.word	.LC265
	.word	.LANCHOR3
	.word	.LC266
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
	ldr	r5, .L4779
	.pad #20
	sub	sp, sp, #20
	ldrb	r2, [r5, #1102]	@ zero_extendqisi2
	ldr	r3, [r5, #1096]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	bne	.L4772
	sub	r2, r3, #5
	mov	r8, r0
	cmp	r3, #68
	cmpne	r2, #2
	mov	r9, r1
	bls	.L4774
	sub	r3, r3, #19
	tst	r3, #239
	bne	.L4772
.L4774:
	ldr	fp, .L4779
	ldr	r6, .L4779+4
	ldr	r7, .L4779+8
.L4776:
	ldr	r3, [r5, #1096]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L4772
	mov	ip, r4, asl #1
	add	r10, r4, r9
	ldr	r0, .L4779+12
	mov	r1, r4
	ldrh	r3, [r7, ip]
	mov	r2, r10
	str	ip, [sp, #12]
	bl	printk
	movw	r3, #65535
	ldr	ip, [sp, #12]
	ldrh	r2, [r7, ip]
	cmp	r2, r3
	bne	.L4772
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r6, #-200]
	add	r4, r4, #1
	bl	ftl_memset
	ldr	r3, [fp, #1096]
	ldr	r2, [r6, #-200]
	mov	r0, r8
	mov	r1, r10
	uxth	r4, r4
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r2
	bl	flash_prog_page
	b	.L4776
.L4772:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4780:
	.align	2
.L4779:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-2340
	.word	.LC314
	.fnend
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 136
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L4860
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #156
	sub	sp, sp, #156
	ldr	r3, [ip, #1096]
	ldr	r5, [r1]
	ldr	r8, .L4860+4
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	r9, [r3, #10]
	ldr	r3, .L4860+8
	ldr	fp, .L4860+12
	ldr	r10, .L4860+16
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
	bne	.L4858
	cmp	r0, #15
	bls	.L4858
	mov	r7, r0
	mov	r0, #256000
	str	ip, [sp, #20]
	mov	r5, r2
	str	r1, [sp, #36]
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #24]
	beq	.L4858
	ldr	r3, .L4860+20
	ldr	ip, [sp, #20]
	ldrb	r3, [r3, #504]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4857
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #0
	ldrb	r3, [ip, #1102]	@ zero_extendqisi2
	clzne	r3, r3
	movne	r3, r3, lsr #5
	bne	.L4857
	cmp	r3, #3
	streq	r3, [sp, #44]
	moveq	r3, #2
	beq	.L4857
	cmp	r3, #2
	ldr	r3, [sp, #44]
	moveq	r3, #2
	str	r3, [sp, #44]
	movne	r3, #0
	moveq	r3, #3
.L4857:
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
	beq	.L4788
	ldr	r0, [sp, #24]
	bl	ftl_free
.L4858:
	mvn	r0, #0
	b	.L4783
.L4788:
	mov	r0, r10
	add	r7, r7, #508
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4860
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
	ldr	ip, .L4860+24
	movw	r2, #63871
	ldr	r3, [sp, #36]
	str	r0, [sp, #72]
	movw	r0, #4094
	add	r3, r3, #254976
	add	r3, r3, #512
.L4794:
	ldr	r1, [r3, #-4]!
	cmp	r1, #0
	bne	.L4789
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
	bne	.L4794
	b	.L4793
.L4789:
	ldr	r0, .L4860+28
	bl	printk
.L4793:
	mul	r3, r6, r4
	ldr	r0, .L4860+32
	mov	r1, r5
	mov	r2, #4
	ldr	r4, .L4860
	str	r3, [sp, #56]
	mov	r3, #5
	bl	rknand_print_hex
	ldr	r0, .L4860+36
	mov	r1, fp
	mov	r2, fp
	bl	printk
	sub	r3, r5, #4
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #40]
	str	r3, [sp, #28]
.L4819:
	ldr	r3, .L4860+20
	ldrb	r3, [r3, #504]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4795
	ldr	r3, [sp, #48]
	ldr	r1, [sp, #72]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #56]
	mul	r6, r3, r2
	ldr	r3, [sp, #28]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L4796
	ldr	r3, [sp, #48]
	ldr	r3, [r3]
	add	r3, r3, #1
	cmp	r2, r3
	bne	.L4796
	b	.L4797
.L4795:
	ldr	r3, [r4, #1176]
	ldr	r2, [sp, #28]
	add	r3, r3, r2
	ldrb	r6, [r3, #32]	@ zero_extendqisi2
	cmp	r6, #255
	beq	.L4797
	ldr	r3, [sp, #56]
	mul	r6, r3, r6
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4796
	ldr	r3, [sp, #20]
	and	r5, r2, #1
	cmp	r3, #3
	orreq	r5, r5, #1
	cmp	r5, #0
	ldrneb	r3, [sp, #44]	@ zero_extendqisi2
	strneb	r3, [r4]
	strneb	r3, [r4, #1102]
	bne	.L4796
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	strb	r5, [r4]
	strb	r5, [r4, #1102]
.L4796:
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #24]
	mov	r7, #0
	bl	ftl_memset
	ldr	r3, [r4, #1096]
	mov	r0, r6
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	ldrh	r8, [r3, #10]
	ldr	r3, .L4860+8
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
	bls	.L4799
	mov	r0, r7
	add	r1, r6, r5
	bl	flash_erase_block
	mov	r0, #2
.L4799:
	ldr	r3, .L4860
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
	bne	.L4826
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L4826
	ldr	r3, .L4860+20
	mov	r1, #0
	mov	r2, #1024
	ldr	r5, [r3, #-176]
	mov	r0, r5
	bl	ftl_memset
	ldr	r2, .L4860+40
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
	b	.L4800
.L4826:
	mov	r5, #0
	str	fp, [sp, #32]
.L4800:
	ldr	r8, [sp, #36]
	mov	r9, #0
.L4803:
	ldr	r3, [sp, #80]
	cmp	r9, r3
	bcs	.L4859
	ldr	r3, [sp, #64]
	ldrb	r2, [r4, #1102]	@ zero_extendqisi2
	add	r7, r9, r3
	ubfx	r7, r7, #2, #16
	cmp	r2, #0
	add	r0, r7, #1
	mov	r1, r0, asl #1
	add	r3, r4, r1
	ldrh	r3, [r3, #4]
	beq	.L4805
	ldrb	ip, [r4, #1]	@ zero_extendqisi2
	cmp	ip, #0
	moveq	r3, r0
	movne	r3, r1
.L4805:
	ldrb	r1, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, .L4860
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
	beq	.L4809
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r7, r1
.L4809:
	cmp	r5, #0
	mul	r0, r10, r7
	bne	.L4810
	ldr	r2, [sp, #52]
	str	r8, [sp]
	add	ip, r0, r2
	ldr	r2, [r8]
	ldr	r0, .L4860+44
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
	bne	.L4811
	mov	r1, r10
	ldr	r0, [sp, #52]
	bl	__aeabi_uidiv
	add	r2, r7, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, r5
	bl	id_block_prog_msb_ff_data
.L4811:
	ldr	r3, [sp, #32]
	add	r8, r8, #2048
	add	r3, r3, #16
	cmp	r9, r3
	bcc	.L4812
	ldr	r3, [sp, #32]
	add	r3, r3, #20
	cmp	r9, r3
	ldrcc	r3, [sp, #36]
	addcc	r8, r3, #2048
	b	.L4812
.L4810:
	ldr	r3, [sp, #52]
	mov	r1, r5
	add	r2, sp, #88
	add	r0, r0, r3
	bl	fw_flash_page_prog.constprop.30
.L4812:
	add	r9, r9, #4
	mov	r5, #0
	uxth	r9, r9
	b	.L4803
.L4859:
	ldr	r3, .L4860
	mov	r0, r6
	ldrb	r2, [sp, #76]	@ zero_extendqisi2
	ldr	r1, [sp, #32]
	strb	r2, [r3, #1168]
	ldr	r2, [sp, #24]
	bl	id_block_read_data.constprop.31
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4814
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #1102]
.L4814:
	ldr	r3, [sp, #32]
	mov	r5, #0
	mov	r8, r3, asl #7
.L4817:
	ldr	r3, [sp, #24]
	ldr	r2, [r3, r5, asl #2]
	ldr	r3, [sp, #36]
	ldr	r3, [r3, r5, asl #2]
	cmp	r2, r3
	beq	.L4815
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	bl	ftl_memset
	mov	r0, #0
	mov	r1, r6
	bl	flash_erase_block
	cmp	r5, r8
	bcs	.L4816
	b	.L4797
.L4815:
	add	r5, r5, #1
	cmp	r5, r8
	bne	.L4817
.L4816:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	cmp	r3, #5
	bhi	.L4818
.L4797:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
	ldr	r3, [sp, #48]
	add	r3, r3, #4
	str	r3, [sp, #48]
	ldr	r3, [sp, #28]
	cmp	r3, #4
	bne	.L4819
.L4818:
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4860
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
.L4783:
	add	sp, sp, #156
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4861:
	.align	2
.L4860:
	.word	.LANCHOR0
	.word	1397640018
	.word	.LANCHOR2
	.word	-52655045
	.word	1397967698
	.word	.LANCHOR3
	.word	1314014539
	.word	.LC315
	.word	.LC316
	.word	.LC317
	.word	1179535694
	.word	.LC318
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
	ldr	r5, .L4888
	bne	.L4863
	ldr	r3, [r2]
	ldr	r1, .L4888+4
	ldr	r2, .L4888+8
	cmp	r3, r1
	cmpne	r3, r2
	ldr	r1, .L4888+12
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, r1
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4863
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r5, #520]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r5, #524]
	bl	ftl_memset
	str	r4, [r5, #528]
.L4863:
	str	r6, [sp]
	mov	r3, r4
	ldr	r0, .L4888+16
	ldr	r1, [r5, #524]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r5, #520]	@ zero_extendqisi2
	ldr	r9, .L4888
	cmp	r3, #0
	beq	.L4862
	ldr	r7, [r9, #524]
	cmp	r7, #0
	beq	.L4862
	sub	r0, r4, #64
	cmp	r0, #500
	bcs	.L4865
	rsb	r2, r4, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4866
.L4865:
	cmp	r4, #564
	bcs	.L4874
.L4866:
	ldr	r3, [r5, #528]
	ldr	r8, .L4888
	cmp	r3, r4
	beq	.L4872
	mov	r0, r7
	mov	r9, #0
	strb	r9, [r8, #520]
	bl	ftl_free
	str	r9, [r8, #524]
.L4872:
	add	r4, r4, r6
	str	r4, [r5, #528]
	b	.L4862
.L4874:
	ldr	r3, .L4888+20
	ldr	r0, [r9, #528]
	ldr	r3, [r3, #1096]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L4875
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
	b	.L4868
.L4875:
	mov	r3, #0
.L4867:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L4867
.L4868:
	mov	r3, #61952
.L4871:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #2048
	movne	r0, r3, asl #2
	bne	.L4870
.L4869:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L4871
	mov	r0, r0, asl #9
.L4870:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r5, #524]
	mov	r7, #0
	strb	r7, [r5, #520]
	bl	ftl_free
	str	r7, [r5, #524]
	b	.L4872
.L4862:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L4889:
	.align	2
.L4888:
	.word	.LANCHOR3
	.word	-52655045
	.word	1397640018
	.word	1397967698
	.word	.LC319
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
	beq	.L4891
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L4891:
	ldr	r3, .L4896
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	ldr	r3, [r3, #-208]
	ldr	ip, [r3, #24]
	mov	r3, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	bx	ip
.L4897:
	.align	2
.L4896:
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
	ldr	r3, .L4955
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #524
	sub	sp, sp, #524
	mov	r4, r1
	mov	r5, r2
	beq	.L4900
	bhi	.L4901
	sub	r3, r3, #2080
	sub	r3, r3, #6
	cmp	r1, r3
	beq	.L4902
	bhi	.L4903
	sub	r3, r3, #238
	cmp	r1, r3
	beq	.L4904
	add	r3, r3, #237
	cmp	r1, r3
	beq	.L4905
	b	.L4935
.L4903:
	ldr	r3, .L4955+4
	cmp	r1, r3
	beq	.L4906
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4907
	sub	r3, r3, #124
	cmp	r1, r3
	bne	.L4935
	b	.L4953
.L4901:
	ldr	r3, .L4955+8
	cmp	r1, r3
	mov	r6, r3
	beq	.L4909
	bhi	.L4910
	sub	r3, r3, #2512
	sub	r3, r3, #14
	cmp	r1, r3
	beq	.L4900
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L4900
	b	.L4935
.L4910:
	ldr	r3, .L4955+12
	cmp	r1, r3
	beq	.L4909
	bcc	.L4911
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4911
	b	.L4935
.L4905:
	ldr	r0, .L4955+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4912
.L4918:
	ldr	r0, .L4955+20
	bl	printk
	b	.L4950
.L4912:
	ldr	r2, [sp]
	ldr	r3, .L4955+24
	cmp	r2, r3
	beq	.L4913
.L4915:
	mvn	r4, #0
	b	.L4914
.L4913:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4915
	ldr	r4, .L4955+28
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #532]
	bl	memcpy
	ldr	r2, [r4, #536]
	ldr	r3, .L4955+32
	cmp	r2, r3
	beq	.L4916
	mov	r1, #0
	add	r0, sp, #64
	mov	r2, #128
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L4916:
	mov	r1, #0
	add	r0, sp, #256
	mov	r2, #256
	str	r1, [sp, #16]
	bl	memset
.L4945:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4950
	b	.L4949
.L4902:
	ldr	r0, .L4955+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+24
	cmp	r2, r3
	bne	.L4915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4915
	ldr	r2, .L4955+28
	ldr	r3, .L4955+32
	ldr	r1, [r2, #536]
	cmp	r1, r3
	mvnne	r0, #1
	bne	.L4899
	ldr	r3, [sp, #12]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L4899
	ldr	r4, [r2, #532]
	add	r1, sp, #64
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4948
.L4907:
	ldr	r0, .L4955+40
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+44
	cmp	r2, r3
	bne	.L4915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4915
	ldr	r5, .L4955+28
	ldr	r3, [r5, #540]
	cmp	r3, #0
	bne	.L4919
.L4922:
	mov	r0, #0
	b	.L4899
.L4919:
	ldr	r3, [r5, #544]
	ldr	r2, .L4955+48
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4920
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L4955+28
	ldr	r3, [r3, #544]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4920:
	ldr	r1, [r5, #544]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #532]
	ldr	r2, .L4955+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #532]
	ldrne	r3, .L4955+28
	movne	r2, #504
	add	r0, r6, #64
	ldrne	r3, [r3, #532]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	str	r4, [r5, #540]
	str	r4, [r5, #536]
	b	.L4914
.L4906:
	ldr	r0, .L4955+52
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+56
	cmp	r2, r3
	bne	.L4915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4915
	ldr	r5, .L4955+28
	ldr	r3, [r5, #540]
	cmp	r3, #1
	beq	.L4922
	ldr	r2, [r5, #544]
	ldr	r3, .L4955+48
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L4923
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #544]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4923:
	ldr	r1, [r5, #544]
	mov	r3, #1
	mov	r0, #0
	mov	r4, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #532]
	ldr	r2, .L4955+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #532]
	ldrne	r3, .L4955+28
	movne	r1, #504
	movne	r2, #0
	add	r0, r6, #64
	ldrne	r3, [r3, #532]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #540]
	b	.L4914
.L4953:
	ldr	r0, .L4955+60
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+64
	cmp	r2, r3
	bne	.L4915
	ldr	r2, [sp, #4]
	cmp	r2, #512
	addls	r0, sp, #8
	ldrls	r1, .L4955+68
	bls	.L4951
	b	.L4915
.L4900:
	ldr	r0, .L4955+72
	cmp	r4, r0
	mov	r7, r0
	ldreq	r0, .L4955+76
	beq	.L4947
	ldr	r3, .L4955+80
	cmp	r4, r3
	ldreq	r0, .L4955+84
	ldrne	r0, .L4955+88
.L4947:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+92
	cmp	r2, r3
	bne	.L4950
	ldr	r3, .L4955+80
	ldr	r6, .L4955+28
	cmp	r4, r3
	bne	.L4929
	ldr	r3, [r6, #532]
	mov	r0, r5
	mov	r1, sp
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4899
	b	.L4950
.L4929:
	ldr	r3, [r6, #1060]
	cmp	r3, #10
	bhi	.L4950
	ldr	r1, [r6, #532]
	ldr	r2, [sp, #4]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4930
	ldr	r0, .L4955+96
	mov	r1, r2
	bl	printk
	ldr	r3, [r6, #1060]
	add	r3, r3, #1
	str	r3, [r6, #1060]
.L4950:
	mvn	r0, #13
	b	.L4899
.L4930:
	cmp	r4, r7
	str	r3, [r6, #1060]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L4949
	b	.L4954
.L4909:
	ldr	r0, .L4955+100
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+104
	cmp	r2, r3
	bne	.L4915
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4915
	ldr	r3, .L4955+28
	cmp	r4, r6
	add	r0, sp, #8
	ldreq	r1, [r3, #1064]
	ldrne	r1, [r3, #1068]
	add	r1, r1, #8
.L4951:
	bl	memcpy
	b	.L4945
.L4911:
	ldr	r0, .L4955+108
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4918
	ldr	r2, [sp]
	ldr	r3, .L4955+104
	cmp	r2, r3
	bne	.L4915
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4915
	ldr	r3, .L4955+112
	add	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L4955+28
	bne	.L4934
	mov	r1, sp
	ldr	r0, [r4, #1064]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #1064]
	b	.L4948
.L4934:
	mov	r1, sp
	ldr	r0, [r4, #1068]
	bl	memcpy
	ldr	r1, [r4, #1068]
	mov	r0, #3
.L4948:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L4914
.L4904:
	bl	rknand_dev_flush
.L4949:
	mov	r4, #0
	b	.L4914
.L4954:
	mvn	r4, #1
.L4914:
	ldr	r0, .L4955+116
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L4899
.L4935:
	mvn	r0, #21
.L4899:
	add	sp, sp, #524
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L4956:
	.align	2
.L4955:
	.word	1074031656
	.word	1074029694
	.word	1074034192
	.word	1074034194
	.word	.LC320
	.word	.LC321
	.word	1263358532
	.word	.LANCHOR3
	.word	-1067903959
	.word	.LC322
	.word	.LC323
	.word	1112753220
	.word	1146313043
	.word	.LC324
	.word	1112755781
	.word	.LC325
	.word	1094995539
	.word	.LANCHOR3+548
	.word	1074031666
	.word	.LC326
	.word	1074031676
	.word	.LC327
	.word	.LC328
	.word	1280262987
	.word	.LC329
	.word	.LC330
	.word	1145980246
	.word	.LC331
	.word	1074034193
	.word	.LC332
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
	ldr	r4, .L4966
	mov	r2, #512
	mov	r5, #0
	add	r0, r4, #548
	ldr	r1, [r4, #508]
	str	r3, [r4, #528]
	add	r3, r1, #512
	str	r3, [r4, #532]
	str	r1, [r4, #544]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #1064]
	strb	r5, [r4, #520]
	str	r5, [r4, #524]
	str	r5, [r4, #1072]
	str	r1, [r4, #1068]
	bl	ftl_memcpy
	ldr	r0, [r4, #544]
	str	r5, [r4, #536]
	str	r5, [r4, #1060]
	ldr	r6, [r0, #508]
	ldr	r3, [r0, #16]
	cmp	r6, r5
	str	r3, [r4, #540]
	beq	.L4959
	mov	r1, #508
	bl	js_hash
	cmp	r6, r0
	beq	.L4959
	ldr	r3, [r4, #544]
	ldr	r0, .L4966+4
	str	r5, [r4, #540]
	str	r5, [r3, #16]
	bl	printk
.L4959:
	ldr	r3, [r4, #540]
	mov	r0, #2
	ldr	r1, [r4, #1064]
	cmp	r3, #0
	ldrne	r2, .L4966+8
	ldrne	r3, .L4966
	strne	r2, [r3, #536]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1068]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r4, r5, r6, lr}
	b	rknand_sys_storage_init
.L4967:
	.align	2
.L4966:
	.word	.LANCHOR3
	.word	.LC333
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
	ldr	r6, .L4980
	cmp	r0, #0
	str	r0, [r6, #1076]
	beq	.L4975
	ldr	r9, .L4980+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4973:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #1076]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4971
	ldr	r3, [r10, #1076]
	ldr	r0, .L4980+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #1076]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4972
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
.L4972:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4973
.L4979:
	cmp	r4, #0
	beq	.L4974
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4971
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4974:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4980+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4980+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4971:
	ldr	r0, [r6, #1076]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #1076]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4975:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4981:
	.align	2
.L4980:
	.word	.LANCHOR3
	.word	1380668996
	.word	.LC334
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
	ldr	r3, .L4989
	ldr	ip, [r3, #1076]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4987
.L4984:
	cmp	r3, r4
	bcs	.L4987
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4985
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
.L4985:
	add	r3, r3, #1
	b	.L4984
.L4987:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4990:
	.align	2
.L4989:
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
	ldr	r2, .L5014
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #1076]
	cmp	r4, #0
	beq	.L5006
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4993:
	cmp	r6, r2
	bcs	.L5012
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4994
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4995
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L5006
.L4996:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L5013
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
	b	.L4996
.L5013:
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
	b	.L5011
.L4995:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L5011
.L4994:
	add	r6, r6, #1
	b	.L4993
.L5012:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L5006
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
.L5011:
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
	b	.L4992
.L5006:
	mvn	r0, #0
.L4992:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L5015:
	.align	2
.L5014:
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
	beq	.L5017
	ldr	r3, .L5033
	cmp	r6, r3
	beq	.L5019
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L5020
	b	.L5031
.L5019:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5031
	ldr	r2, [r4]
	ldr	r3, .L5033+4
	cmp	r2, r3
	beq	.L5022
.L5023:
	mvn	r5, #0
	b	.L5018
.L5022:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L5023
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L5018
.L5020:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5031
	ldr	r2, [r4]
	ldr	r3, .L5033+4
	cmp	r2, r3
	bne	.L5023
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L5023
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5031
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L5018
.L5031:
	mvn	r5, #13
.L5018:
	mov	r0, r4
	bl	kfree
.L5017:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L5034:
	.align	2
.L5033:
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
	.type	__func__.39435, %object
	.size	__func__.39435, 18
__func__.39435:
	.ascii	"_list_remove_node\000"
	.space	2
	.type	__func__.39460, %object
	.size	__func__.39460, 23
__func__.39460:
	.ascii	"_list_update_data_list\000"
	.space	1
	.type	__func__.20182, %object
	.size	__func__.20182, 22
__func__.20182:
	.ascii	"nand_flash_print_info\000"
	.space	2
	.type	__func__.20268, %object
	.size	__func__.20268, 11
__func__.20268:
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
	.type	__func__.19655, %object
	.size	__func__.19655, 28
__func__.19655:
	.ascii	"flash_wait_device_ready_raw\000"
	.type	__func__.19719, %object
	.size	__func__.19719, 22
__func__.19719:
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
	.type	__func__.19744, %object
	.size	__func__.19744, 23
__func__.19744:
	.ascii	"flash_start_plane_read\000"
	.space	1
	.type	__func__.19630, %object
	.size	__func__.19630, 26
__func__.19630:
	.ascii	"flash_erase_duplane_block\000"
	.space	2
	.type	__func__.19641, %object
	.size	__func__.19641, 21
__func__.19641:
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
	.type	__func__.20451, %object
	.size	__func__.20451, 13
__func__.20451:
	.ascii	"buf_add_tail\000"
	.space	3
	.type	__func__.20464, %object
	.size	__func__.20464, 10
__func__.20464:
	.ascii	"buf_alloc\000"
	.space	2
	.type	__func__.20478, %object
	.size	__func__.20478, 16
__func__.20478:
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
	.type	__func__.38995, %object
	.size	__func__.38995, 12
__func__.38995:
	.ascii	"gc_add_sblk\000"
	.type	__func__.39087, %object
	.size	__func__.39087, 19
__func__.39087:
	.ascii	"gc_write_completed\000"
	.space	1
	.type	__func__.39643, %object
	.size	__func__.39643, 18
__func__.39643:
	.ascii	"ftl_alloc_sys_blk\000"
	.space	2
	.type	__func__.39653, %object
	.size	__func__.39653, 17
__func__.39653:
	.ascii	"ftl_free_sys_blk\000"
	.space	3
	.type	__func__.39774, %object
	.size	__func__.39774, 23
__func__.39774:
	.ascii	"ftl_get_ppa_from_index\000"
	.space	1
	.type	__func__.39814, %object
	.size	__func__.39814, 22
__func__.39814:
	.ascii	"ftl_get_new_free_page\000"
	.space	2
	.type	__func__.39825, %object
	.size	__func__.39825, 22
__func__.39825:
	.ascii	"ftl_ext_alloc_new_blk\000"
	.space	2
	.type	__func__.39144, %object
	.size	__func__.39144, 16
__func__.39144:
	.ascii	"gc_free_src_blk\000"
	.type	__func__.38739, %object
	.size	__func__.38739, 14
__func__.38739:
	.ascii	"ftl_write_buf\000"
	.space	2
	.type	__func__.38784, %object
	.size	__func__.38784, 18
__func__.38784:
	.ascii	"zftl_add_read_buf\000"
	.space	2
	.type	__func__.39601, %object
	.size	__func__.39601, 15
__func__.39601:
	.ascii	"ftl_alloc_sblk\000"
	.space	1
	.type	__func__.40219, %object
	.size	__func__.40219, 17
__func__.40219:
	.ascii	"pm_alloc_new_blk\000"
	.space	3
	.type	__func__.40247, %object
	.size	__func__.40247, 21
__func__.40247:
	.ascii	"pm_select_ram_region\000"
	.space	3
	.type	__func__.20172, %object
	.size	__func__.20172, 20
__func__.20172:
	.ascii	"flash_die_info_init\000"
	.type	__func__.38671, %object
	.size	__func__.38671, 17
__func__.38671:
	.ascii	"lpa_rebuild_hash\000"
	.space	3
	.type	__func__.39535, %object
	.size	__func__.39535, 20
__func__.39535:
	.ascii	"zftl_sblk_list_init\000"
	.type	__func__.40179, %object
	.size	__func__.40179, 13
__func__.40179:
	.ascii	"pm_free_sblk\000"
	.space	3
	.type	__func__.20514, %object
	.size	__func__.20514, 21
__func__.20514:
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
	.type	__func__.19553, %object
	.size	__func__.19553, 19
__func__.19553:
	.ascii	"flash_read_page_en\000"
	.space	1
	.type	__func__.19679, %object
	.size	__func__.19679, 26
__func__.19679:
	.ascii	"flash_start_tlc_page_prog\000"
	.space	2
	.type	__func__.19690, %object
	.size	__func__.19690, 29
__func__.19690:
	.ascii	"flash_start_3d_mlc_page_prog\000"
	.space	3
	.type	__func__.19708, %object
	.size	__func__.19708, 22
__func__.19708:
	.ascii	"flash_start_page_prog\000"
	.space	2
	.type	__func__.19732, %object
	.size	__func__.19732, 31
__func__.19732:
	.ascii	"flash_complete_plane_page_read\000"
	.space	1
	.type	__func__.19757, %object
	.size	__func__.19757, 25
__func__.19757:
	.ascii	"flash_complete_page_read\000"
	.space	3
	.type	__func__.40012, %object
	.size	__func__.40012, 31
__func__.40012:
	.ascii	"queue_wait_first_req_completed\000"
	.space	1
	.type	__func__.40066, %object
	.size	__func__.40066, 15
__func__.40066:
	.ascii	"sblk_prog_page\000"
	.space	1
	.type	__func__.40093, %object
	.size	__func__.40093, 15
__func__.40093:
	.ascii	"sblk_read_page\000"
	.space	1
	.type	__func__.39042, %object
	.size	__func__.39042, 21
__func__.39042:
	.ascii	"gc_check_data_one_wl\000"
	.space	3
	.type	__func__.19614, %object
	.size	__func__.19614, 19
__func__.19614:
	.ascii	"flash_prog_page_en\000"
	.space	1
	.type	__func__.39889, %object
	.size	__func__.39889, 14
__func__.39889:
	.ascii	"ftl_prog_page\000"
	.space	2
	.type	__func__.39569, %object
	.size	__func__.39569, 15
__func__.39569:
	.ascii	"ftl_info_flush\000"
	.space	1
	.type	__func__.39851, %object
	.size	__func__.39851, 19
__func__.39851:
	.ascii	"ftl_ext_info_flush\000"
	.space	1
	.type	__func__.39863, %object
	.size	__func__.39863, 18
__func__.39863:
	.ascii	"ftl_ext_info_init\000"
	.space	2
	.type	__func__.40229, %object
	.size	__func__.40229, 14
__func__.40229:
	.ascii	"pm_write_page\000"
	.space	2
	.type	__func__.20536, %object
	.size	__func__.20536, 17
__func__.20536:
	.ascii	"flash_info_flush\000"
	.space	3
	.type	__func__.20499, %object
	.size	__func__.20499, 20
__func__.20499:
	.ascii	"flash_info_blk_init\000"
	.type	__func__.20227, %object
	.size	__func__.20227, 16
__func__.20227:
	.ascii	"nand_flash_init\000"
	.type	__func__.39716, %object
	.size	__func__.39716, 16
__func__.39716:
	.ascii	"ftl_sysblk_dump\000"
	.type	__func__.39741, %object
	.size	__func__.39741, 23
__func__.39741:
	.ascii	"ftl_open_sblk_recovery\000"
	.space	1
	.type	__func__.40163, %object
	.size	__func__.40163, 16
__func__.40163:
	.ascii	"load_l2p_region\000"
	.type	__func__.40203, %object
	.size	__func__.40203, 6
__func__.40203:
	.ascii	"pm_gc\000"
	.space	2
	.type	__func__.38985, %object
	.size	__func__.38985, 12
__func__.38985:
	.ascii	"gc_recovery\000"
	.type	__func__.39074, %object
	.size	__func__.39074, 22
__func__.39074:
	.ascii	"gc_update_l2p_map_new\000"
	.space	2
	.type	__func__.39188, %object
	.size	__func__.39188, 16
__func__.39188:
	.ascii	"gc_scan_src_blk\000"
	.type	__func__.39249, %object
	.size	__func__.39249, 20
__func__.39249:
	.ascii	"gc_scan_static_data\000"
	.type	__func__.39312, %object
	.size	__func__.39312, 18
__func__.39312:
	.ascii	"gc_block_vpn_scan\000"
	.space	2
	.type	__func__.39692, %object
	.size	__func__.39692, 14
__func__.39692:
	.ascii	"ftl_sblk_dump\000"
	.space	2
	.type	__func__.38817, %object
	.size	__func__.38817, 10
__func__.38817:
	.ascii	"zftl_read\000"
	.space	2
	.type	__func__.39795, %object
	.size	__func__.39795, 19
__func__.39795:
	.ascii	"ftl_update_l2p_map\000"
	.space	1
	.type	__func__.38721, %object
	.size	__func__.38721, 17
__func__.38721:
	.ascii	"ftl_write_commit\000"
	.space	3
	.type	__func__.39117, %object
	.size	__func__.39117, 16
__func__.39117:
	.ascii	"gc_do_copy_back\000"
	.type	__func__.39340, %object
	.size	__func__.39340, 11
__func__.39340:
	.ascii	"zftl_do_gc\000"
	.space	1
	.type	__func__.38848, %object
	.size	__func__.38848, 13
__func__.38848:
	.ascii	"_ftl_discard\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC1:
	.ascii	"%s\012\000"
.LC2:
	.ascii	"FTL version: 6.0.23 20201130\000"
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
	.ascii	"slc mode = %d\012\000"
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
	.ascii	"zftl_get_gc_node cache = %x index = %d vpn = %x\012"
	.ascii	"\000"
.LC100:
	.ascii	"gc_search_src_blk mode = %x, src mode = %x, count= "
	.ascii	"%d %d\012\000"
.LC101:
	.ascii	"%d gc_free_temp_buf buf id= %x\012\000"
.LC102:
	.ascii	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\012"
	.ascii	"\000"
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
	.ascii	"alloc sblk %x %d\012\000"
.LC124:
	.ascii	"dump_sblk_queue: %d\012\000"
.LC125:
	.ascii	"buf id= %d state = %d ppa = %x\012\000"
.LC126:
	.ascii	"blk %x is bad block\012\000"
.LC127:
	.ascii	"pm_alloc_new_blk: %x %x %x %x\012\000"
.LC128:
	.ascii	"%s %d %d\012\000"
.LC129:
	.ascii	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d"
	.ascii	" ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC130:
	.ascii	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC131:
	.ascii	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC132:
	.ascii	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC133:
	.ascii	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC134:
	.ascii	"gc_static_wearleveling: slc blk: %x, tlc blk: %d av"
	.ascii	"g slc ec: %d, avg tlc ec: %d \012\000"
.LC135:
	.ascii	"gc_static_wearleveling: min slc ec: %x, min tlc ec:"
	.ascii	" %d max slc ec: %d, max tlc ec: %d; %d %d\012\000"
.LC136:
	.ascii	"swl add tlc gc = %x, %d, %d, %d, %d, %d\012\000"
.LC137:
	.ascii	"swl add slc gc  = %x, %d, %d, %d, %d, %d\012\000"
.LC138:
	.ascii	"free blk vpn error: %x %x\012\000"
.LC139:
	.ascii	"GC PM block %x %x %x %d\012\000"
.LC140:
	.ascii	"ftl_free_no_use_map_blk %x %x %x %d\012\000"
.LC141:
	.ascii	"...%d @ %s\012\000"
.LC142:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC143:
	.ascii	"\0013\000"
.LC144:
	.ascii	"otp error! %d\000"
.LC145:
	.ascii	"rr\000"
.LC146:
	.ascii	"flash_abort_clear = %d\012\000"
.LC147:
	.ascii	"%d mtrans_cnt = %d page_num = %d\012\000"
.LC148:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC149:
	.ascii	"nandc:\000"
.LC150:
	.ascii	"nandc_xfer_done read error %x\012\000"
.LC151:
	.ascii	"dqs data abort %x\012\000"
.LC152:
	.ascii	"dqs data timeout %x\012\000"
.LC153:
	.ascii	"xfer error %x\012\000"
.LC154:
	.ascii	"MT %d row=%x,last status %d,status = %d\012\000"
.LC155:
	.ascii	"MT RR %d row=%x,count %d,status=%d\012\000"
.LC156:
	.ascii	"toshiba SRR %d row=%x, status=%d\012\000"
.LC157:
	.ascii	"toshiba TRR %d row=%x, status=%d\012\000"
.LC158:
	.ascii	"toshiba RR %d row=%x,count %d,status=%d\012\000"
.LC159:
	.ascii	"YMTC RR %d row=%x,count %d,status=%d\012\000"
.LC160:
	.ascii	"samsung SRR %d row=%x, status=%d\012\000"
.LC161:
	.ascii	"samsung TRR %d row=%x, status=%d\012\000"
.LC162:
	.ascii	"samsung RR %d row=%x,count %d,status=%d\012\000"
.LC163:
	.ascii	"hynix RR %d row=%x, count %d, status=%d\012\000"
.LC164:
	.ascii	"%d flash_ddr_tuning_read %x ecc=%d\012\000"
.LC165:
	.ascii	"sync para %d\012\000"
.LC166:
	.ascii	"DDR mode Read error %x %x\012\000"
.LC167:
	.ascii	"SDR mode Read %x %x ecc:%x\012\000"
.LC168:
	.ascii	"flash_read_page_en %x %x %x %x\012\000"
.LC169:
	.ascii	"flash_read_page_en %x %x error_ecc %d %d\012\000"
.LC170:
	.ascii	"flash_get_last_written_page: %x %x %x\012\000"
.LC171:
	.ascii	"read page: %x %x %x %x\012\000"
.LC172:
	.ascii	"flash_prog_page %x %x %x\012\000"
.LC173:
	.ascii	"flash_prog_page page_addr = %x status = %x\012\000"
.LC174:
	.ascii	"ymtc_flash_tlc_page_prog page_addr = %x status = %x"
	.ascii	"\012\000"
.LC175:
	.ascii	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\012"
	.ascii	"\000"
.LC176:
	.ascii	"flash_complete_page_read %x %x error_ecc %d %d\012\000"
.LC177:
	.ascii	"read: %x %x %x %x\012\000"
.LC178:
	.ascii	"0set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC179:
	.ascii	"prog end %x %x error_ecc %d %d\012\000"
.LC180:
	.ascii	"1set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC181:
	.ascii	"dp prog end %x %x error_ecc %d %d\012\000"
.LC182:
	.ascii	"sblk_prog_page ppa = %x, count = %d\012\000"
.LC183:
	.ascii	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %"
	.ascii	"x\012\000"
.LC184:
	.ascii	"flash_prog_page_en:%x %x %x\012\000"
.LC185:
	.ascii	"w d:\000"
.LC186:
	.ascii	"w s:\000"
.LC187:
	.ascii	"spare\000"
.LC188:
	.ascii	"data\000"
.LC189:
	.ascii	"write error: %x\012\000"
.LC190:
	.ascii	"g_ftl_info_blk blk = %x, page = %x version = %d\012"
	.ascii	"\000"
.LC191:
	.ascii	"%d %x @%d %x\012\000"
.LC192:
	.ascii	"ftl_info_blk_init %d %d %x\012\000"
.LC193:
	.ascii	"ftl info hash %x error\012\000"
.LC194:
	.ascii	"ink flag: %x\012\000"
.LC195:
	.ascii	"%s %d %d %x %x\012\000"
.LC196:
	.ascii	"ext info hash %x error\012\000"
.LC197:
	.ascii	"%s %x %x %x\012\000"
.LC198:
	.ascii	"pm_write_page write error: %x\012\000"
.LC199:
	.ascii	"ftl_sblk_dump_write = %x %d %d %d %d\012\000"
.LC200:
	.ascii	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x"
	.ascii	" %x %x, spare: %x %x %x %x\012\000"
.LC201:
	.ascii	"ftl_sblk_dump_write2 = %x %d %d %d\012\000"
.LC202:
	.ascii	"ftl_sblk_dump_write = %x %x\012\000"
.LC203:
	.ascii	"ftl_sblk_dump_write done = %x\012\000"
.LC204:
	.ascii	"finfo:\000"
.LC205:
	.ascii	"flash_info_flush id = %x, page = %x\012\000"
.LC206:
	.ascii	"sys_info_flush error:%x\012\000"
.LC207:
	.ascii	"...%d @ %s %d %p\012\000"
.LC208:
	.ascii	"no sys info %x\012\000"
.LC209:
	.ascii	"l2p:\000"
.LC210:
	.ascii	"saved_active_page  = %x\012\000"
.LC211:
	.ascii	"saved_active_plane = %x\012\000"
.LC212:
	.ascii	"sblk = %x\012\000"
.LC213:
	.ascii	"phy_blk = %x %x\012\000"
.LC214:
	.ascii	"num_planes = %x\012\000"
.LC215:
	.ascii	"recovery blk=%x, page=%x, ppa = %x, status = %x, ha"
	.ascii	"sh:%x\012\000"
.LC216:
	.ascii	"data:\000"
.LC217:
	.ascii	"sblk = %x, vpn0 = %d, vpn1 = %d\012\000"
.LC218:
	.ascii	"dump_write_lpa = %x %x %x %x\012\000"
.LC219:
	.ascii	"dump write new ppa = %x, last ppa = %x lpa = %x\012"
	.ascii	"\000"
.LC220:
	.ascii	"dump write = %x %x %x\012\000"
.LC221:
	.ascii	"dump write hash update = %x %x %x\012\000"
.LC222:
	.ascii	"free_buf_count: %d\012\000"
.LC223:
	.ascii	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\012\000"
.LC224:
	.ascii	"ftl_ext_info_blk blk:0x%x, page:0x%x\012\000"
.LC225:
	.ascii	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC226:
	.ascii	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, pag"
	.ascii	"e_index:0x%x\012\000"
.LC227:
	.ascii	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC228:
	.ascii	"lpa:\000"
.LC229:
	.ascii	"vpn:\000"
.LC230:
	.ascii	"sblk:\000"
.LC231:
	.ascii	"lpa_hash:\000"
.LC232:
	.ascii	"lpa_hash_index:\000"
.LC233:
	.ascii	"%s w error lpn = %x, max ppa = %d\012\000"
.LC234:
	.ascii	"region_id = %d, pm_max_region = %d\012\000"
.LC235:
	.ascii	"load_l2p_region no ppa = %x , %x, all setting 0xff."
	.ascii	"...\012\000"
.LC236:
	.ascii	"load_l2p_region = %x,%x,%x, %x\012\000"
.LC237:
	.ascii	"pm_ppa:\000"
.LC238:
	.ascii	"spare:\000"
.LC239:
	.ascii	"pm_init posr %x %x %x\012\000"
.LC240:
	.ascii	"pm_init recovery %x %x %x\012\000"
.LC241:
	.ascii	"pm_init hash %x error\012\000"
.LC242:
	.ascii	"pm_log2phys  lpn = %d, max lpn = %d\012\000"
.LC243:
	.ascii	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x "
	.ascii	"%x %x %x\012\000"
.LC244:
	.ascii	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\012"
	.ascii	"\000"
.LC245:
	.ascii	"gc_recovery: %x vpn = %x\012\000"
.LC246:
	.ascii	"gc_update_l2p_map_new sblk %x\012\000"
.LC247:
	.ascii	"gc_update_l2p_map_new: %x %x %x\012\000"
.LC248:
	.ascii	"lpa: %x %x %x\012\000"
.LC249:
	.ascii	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\012"
	.ascii	"\000"
.LC250:
	.ascii	"gc_scan_src_blk = %x, vpn = %d\012\000"
.LC251:
	.ascii	"js hash error:%x %x %x\012\000"
.LC252:
	.ascii	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\012\000"
.LC253:
	.ascii	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:"
	.ascii	"%d\012\000"
.LC254:
	.ascii	"ftl_sblk_dump = %x %d %d %d %d\012\000"
.LC255:
	.ascii	"ftl_sblk_dump = %x %x %x %x\012\000"
.LC256:
	.ascii	"page_addr = %x, lpa=%x vpn = %d\012\000"
.LC257:
	.ascii	"index= %x, lpa=%x\012\000"
.LC258:
	.ascii	"block = %x, vpn=%x check vpn = %x\012\000"
.LC259:
	.ascii	"ftl_read %x %x %x\012\000"
.LC260:
	.ascii	"ftl_read refresh =%x, lpa = %x, ppa= %x\012\000"
.LC261:
	.ascii	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %"
	.ascii	"x %x %x\012\000"
.LC262:
	.ascii	"zftl debug cmd: %s\012\000"
.LC263:
	.ascii	"cmd:\000"
.LC264:
	.ascii	"dumpl2p\000"
.LC265:
	.ascii	"pm l2p:\000"
.LC266:
	.ascii	"pm blk:\000"
.LC267:
	.ascii	"dumppm:\000"
.LC268:
	.ascii	"p_cmd: %s\012\000"
.LC269:
	.ascii	"pm ram = %x, %x\012\000"
.LC270:
	.ascii	"ram:\000"
.LC271:
	.ascii	"pm:\000"
.LC272:
	.ascii	"dumpsys\000"
.LC273:
	.ascii	"dumplist:\000"
.LC274:
	.ascii	"vpncheck\000"
.LC275:
	.ascii	"dumpppa:\000"
.LC276:
	.ascii	"dumpblk:\000"
.LC277:
	.ascii	"setzdebug:\000"
.LC278:
	.ascii	"lpa2ppa:\000"
.LC279:
	.ascii	"lpa: %x--> ppa: %x\012\000"
.LC280:
	.ascii	"help:\012\000"
.LC281:
	.ascii	"1. echo dumpl2p > /proc/zftl_debug\012\000"
.LC282:
	.ascii	"2. echo dumppm:x > /proc/zftl_debug\012\000"
.LC283:
	.ascii	"3. echo dumpsys > /proc/zftl_debug\012\000"
.LC284:
	.ascii	"4. echo dumpppa:x > /proc/zftl_debug\012\000"
.LC285:
	.ascii	"5. echo vpncheck > /proc/zftl_debug\012\000"
.LC286:
	.ascii	"6. echo setzdebug:x > /proc/zftl_debug\012\000"
.LC287:
	.ascii	"7. echo dumplist > /proc/zftl_debug\012\000"
.LC288:
	.ascii	"8. echo lpa2ppa:x> /proc/zftl_debug\012\000"
.LC289:
	.ascii	"ftl_update_l2p_map: %x %x %x\012\000"
.LC290:
	.ascii	"ftl_update_l2p_map\000"
.LC291:
	.ascii	"lpa_tbl:\000"
.LC292:
	.ascii	"sblk %x vpn: %d %d\012\000"
.LC293:
	.ascii	"error gc_add_sblk: %x\012\000"
.LC294:
	.ascii	"%d read error: ppa:%x, lpa:%x, status:%x\012\000"
.LC295:
	.ascii	"gc page in buf: lpa %x ppa = %x pageindex= %x\012\000"
.LC296:
	.ascii	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x "
	.ascii	"page_index= %d\012\000"
.LC297:
	.ascii	"gc %d: %d %d %d %d %d %d %d\012\000"
.LC298:
	.ascii	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\012"
	.ascii	"\000"
.LC299:
	.ascii	"gc free %x, %d\012\000"
.LC300:
	.ascii	"_c_user_data_density := %d\012\000"
.LC301:
	.ascii	"_c_totle_phy_density := %d\012\000"
.LC302:
	.ascii	"_c_totle_log_page := %d\012\000"
.LC303:
	.ascii	"_c_totle_data_density := %d\012\000"
.LC304:
	.ascii	"_c_ftl_pm_page_num := %d\012\000"
.LC305:
	.ascii	"_c_ftl_byte_pre_page := %d\012\000"
.LC306:
	.ascii	"_c_max_pm_sblk := %d\012\000"
.LC307:
	.ascii	"_min_slc_super_block := %d\012\000"
.LC308:
	.ascii	"_max_xlc_super_block := %d\012\000"
.LC309:
	.ascii	"gp_ftl_ext_info %p %p %p\012\000"
.LC310:
	.ascii	"flash info size: %d %d %d\012\000"
.LC311:
	.ascii	"ftl_init %x\012\000"
.LC312:
	.ascii	"ftlwrite %x %x %x %x\012\000"
.LC313:
	.ascii	"ftl_discard:(%x, %x, %x, %x)\012\000"
.LC314:
	.ascii	"id_block_prog_msb_ff_data slc page = %d pageadd=%x "
	.ascii	"%x\012\000"
.LC315:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC316:
	.ascii	"idblk:\000"
.LC317:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC318:
	.ascii	"prog page: %x %x %x, %p %x %x %x\012\000"
.LC319:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC320:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC321:
	.ascii	"rk_copy_from_user error\012\000"
.LC322:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC323:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC324:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC325:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC326:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC327:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC328:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC329:
	.ascii	"LockKey not match %d\012\000"
.LC330:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC331:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC332:
	.ascii	"return ret = %lx\012\000"
.LC333:
	.ascii	"secureBootEn check error\012\000"
.LC334:
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
	.size	g_gc_info, 2132
g_gc_info:
	.space	2132
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
	.type	_c_ftl_page_pre_blk, %object
	.size	_c_ftl_page_pre_blk, 2
_c_ftl_page_pre_blk:
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
	.type	pm_gc_enable, %object
	.size	pm_gc_enable, 4
pm_gc_enable:
	.space	4
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
	.space	39
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
