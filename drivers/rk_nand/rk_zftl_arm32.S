/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-09-29
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
	.eabi_attribute 18, 2
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
	ldrb	r3, [r3, #516]	@ zero_extendqisi2
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
	ldr	r0, [r3, #520]
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
	ldr	r2, [r3, #524]
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
	ldr	r8, [r6, #524]
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
	mov	r2, #188
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
	ldrne	r2, [r6, #524]
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
	ldrne	r2, [r6, #524]
	mulne	r1, r3, r1
	mvnne	r3, #0
	strneh	r3, [r2, r1]	@ movhi
	b	.L43
.L44:
	ldr	r0, [r6, #524]
	mov	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r4, #2]
	ldrh	r1, [r8, r5]
	ldr	r3, [r6, #524]
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
	ldr	r4, [r3, #524]
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
	ldrb	r2, [r3, #516]	@ zero_extendqisi2
	cmp	r2, #9
	ldr	r2, [r3, #528]
	ldr	r1, [r2]
	str	r1, [r3, #532]
	ldr	r1, [r2, #4]
	str	r1, [r3, #536]
	bne	.L67
	ldr	r1, [r2, #16]
	str	r1, [r3, #540]
	ldr	r1, [r2, #32]
	str	r1, [r3, #544]
	ldr	r1, [r2, #80]
	str	r1, [r3, #548]
	ldr	r1, [r2, #84]
	str	r1, [r3, #552]
	ldr	r1, [r2, #520]
	ldr	r2, [r2, #8]
	str	r1, [r3, #556]
	b	.L69
.L67:
	ldr	r1, [r2, #8]
	str	r1, [r3, #540]
	ldr	r1, [r2, #12]
	str	r1, [r3, #544]
	ldr	r1, [r2, #304]
	str	r1, [r3, #548]
	ldr	r1, [r2, #308]
	str	r1, [r3, #552]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #556]
.L69:
	str	r2, [r3, #560]
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
	add	ip, r3, #564
	ldrh	ip, [ip]
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
	ldr	r7, [r3, #524]
	mul	r6, ip, r1
	mvn	r3, #0
	add	lr, r7, r6
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L81
	ldr	r5, [r4, #568]
	add	r8, r4, #572
	mov	r2, r1, asl #2
	ldr	r9, [r4, #524]
	ldrh	r10, [r8]
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
	ldr	r0, [r4, #524]
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
	ldr	r3, .L105
	add	ip, r3, #564
	ldrh	ip, [ip]
	cmp	ip, r1
	bxls	lr
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #6
	ldrh	ip, [r2]
	mov	r4, r3
	mul	r5, r5, r1
	add	ip, ip, #1
	strh	ip, [r2]	@ movhi
	.pad #28
	sub	sp, sp, #28
	ldr	r10, [r3, #524]
	mvn	r3, #0
	add	lr, r10, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r10, r5]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L104
	ldr	r2, [r4, #576]
	add	r8, r4, #572
	ldr	r9, [r4, #568]
	ldrh	r8, [r8]
	str	r2, [sp, #4]
	mov	r2, r1, asl #1
	ldr	ip, [sp, #4]
	ldr	r7, [r9, r1, asl #2]
	ldrh	r6, [lr, #4]
	ldrh	ip, [ip, r2]
	muls	ip, r6, ip
	str	r8, [sp, #8]
	ubfx	r7, r7, #11, #8
	ldrneh	r8, [sp, #8]
	mov	r2, r1, asl #2
	ldrh	r2, [r9, r2]
	smulbbne	r7, r7, r8
	ubfx	r2, r2, #0, #11
	addne	r2, r2, r7
	uxtahne	ip, ip, r2
	ldr	r2, [r4, #524]
	cmp	r6, #0
	mov	r6, #0
	rsb	r4, r2, r3
	str	r2, [sp, #12]
	mvneq	ip, #0
	ldr	r2, .L105+4
	mov	r4, r4, asr #1
	mul	r2, r2, r4
	ldr	r4, .L105+8
	ldrh	r4, [r4]
	uxth	r2, r2
	str	r4, [sp, #16]
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
	ldr	r0, [r2, #524]
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
	.word	.LANCHOR0+564
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
	ldr	r3, [r1, #580]
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
	ldr	r10, [r5, #524]
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
	mov	r2, #255
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
	ldr	r0, [r5, #576]
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
	ldr	r3, [r5, #524]
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
	.type	rk_simple_strtoull.constprop.33, %function
rk_simple_strtoull.constprop.33:
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
	.size	rk_simple_strtoull.constprop.33, .-rk_simple_strtoull.constprop.33
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
	ldr	r4, .L258
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L136
	ldr	r0, .L258+4
	ldr	r1, .L258+8
	bl	printk
.L136:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L137
	ldr	r3, .L258+12
	ldr	r0, [r3, #584]
	ldrb	ip, [r0, #4]	@ zero_extendqisi2
	ldrb	r1, [r0, #1]	@ zero_extendqisi2
	ldrb	r2, [r0, #2]	@ zero_extendqisi2
	ldrb	r3, [r0, #3]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #5]	@ zero_extendqisi2
	str	ip, [sp, #4]
	ldrb	r0, [r0, #6]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, .L258+16
	bl	printk
.L137:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L138
	ldr	r3, .L258+12
	ldr	r0, .L258+20
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #8]	@ zero_extendqisi2
	bl	printk
.L138:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L139
	ldr	r3, .L258+12
	ldr	r0, .L258+24
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	printk
.L139:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L140
	ldr	r3, .L258+12
	ldr	r0, .L258+28
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #10]
	bl	printk
.L140:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L141
	ldr	r3, .L258+12
	ldr	r0, .L258+32
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	printk
.L141:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L142
	ldr	r3, .L258+12
	ldr	r0, .L258+36
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	bl	printk
.L142:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L143
	ldr	r3, .L258+12
	ldr	r0, .L258+40
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #14]
	bl	printk
.L143:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L144
	ldr	r3, .L258+12
	ldr	r0, .L258+44
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #23]	@ zero_extendqisi2
	bl	printk
.L144:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L145
	ldr	r3, .L258+12
	ldr	r0, .L258+48
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #18]	@ zero_extendqisi2
	bl	printk
.L145:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L146
	ldr	r3, .L258+12
	ldr	r0, .L258+52
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	bl	printk
.L146:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L147
	ldr	r3, .L258+12
	ldr	r0, .L258+56
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	bl	printk
.L147:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L148
	ldr	r3, .L258+12
	ldr	r0, .L258+60
	ldrb	r1, [r3, #588]	@ zero_extendqisi2
	bl	printk
.L148:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L149
	ldr	r3, .L258+12
	ldr	r0, .L258+64
	ldrb	r1, [r3]	@ zero_extendqisi2
	bl	printk
.L149:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L150
	ldr	r3, .L258+12
	ldr	r0, .L258+68
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #22]	@ zero_extendqisi2
	bl	printk
.L150:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L151
	ldr	r3, .L258+12
	ldr	r0, .L258+72
	ldrb	r1, [r3, #589]	@ zero_extendqisi2
	bl	printk
.L151:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L152
	ldr	r3, .L258+12
	ldr	r0, .L258+76
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	and	r1, r1, #1
	bl	printk
.L152:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L153
	ldr	r3, .L258+12
	ldr	r0, .L258+80
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #1, #1
	bl	printk
.L153:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L154
	ldr	r3, .L258+12
	ldr	r0, .L258+84
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #2, #1
	bl	printk
.L154:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L155
	ldr	r3, .L258+12
	ldr	r0, .L258+88
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #3, #1
	bl	printk
.L155:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L156
	ldr	r3, .L258+12
	ldr	r0, .L258+92
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #4, #1
	bl	printk
.L156:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L157
	ldr	r3, .L258+12
	ldr	r0, .L258+96
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #5, #1
	bl	printk
.L157:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L158
	ldr	r3, .L258+12
	ldr	r0, .L258+100
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #6, #1
	bl	printk
.L158:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L159
	ldr	r3, .L258+12
	ldr	r0, .L258+104
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #7, #1
	bl	printk
.L159:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L160
	ldr	r3, .L258+12
	ldr	r0, .L258+108
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #8, #1
	bl	printk
.L160:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L161
	ldr	r3, .L258+12
	ldr	r0, .L258+112
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #9, #1
	bl	printk
.L161:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L162
	ldr	r3, .L258+12
	ldr	r0, .L258+116
	ldr	r3, [r3, #584]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #10, #1
	bl	printk
.L162:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L163
	ldr	r3, .L258+12
	ldr	r0, .L258+120
	ldrb	r1, [r3]	@ zero_extendqisi2
	ldrb	r2, [r3, #590]	@ zero_extendqisi2
	bl	printk
.L163:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L164
	ldr	r3, .L258+12
	ldr	r0, .L258+124
	ldrb	r1, [r3, #601]	@ zero_extendqisi2
	ldrb	r2, [r3, #602]	@ zero_extendqisi2
	bl	printk
.L164:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L165
	ldr	r3, .L258+12
	ldr	r0, .L258+128
	ldrb	r1, [r3, #599]	@ zero_extendqisi2
	ldrb	r2, [r3, #600]	@ zero_extendqisi2
	bl	printk
.L165:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L135
	ldr	r3, .L258+12
	ldr	r0, .L258+132
	ldrb	r1, [r3, #623]	@ zero_extendqisi2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, lr}
	b	printk
.L135:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L259:
	.align	2
.L258:
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
	.word	.LC34
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
	ldr	r5, .L280
	mov	r3, #0
	str	r3, [sp, #12]
	mov	r7, r0
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L261
	ldr	r0, .L280+4
	mov	r2, r7
	ldr	r1, .L280+8
	bl	printk
.L261:
	ldr	r4, .L280+12
	mov	r3, #6
	ldr	r2, [r7, #352]
	ldr	r6, .L280+12
	strb	r3, [r4, #516]
	ldr	r3, .L280+16
	str	r7, [r4, #528]
	cmp	r2, r3
	ldr	r2, [r7, #128]
	moveq	r3, #8
	streqb	r3, [r4, #516]
	ldr	r3, .L280+20
	cmp	r2, r3
	ldr	r2, .L280+24
	moveq	r3, #9
	streqb	r3, [r4, #516]
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L264
	mov	r3, #1
	strb	r3, [r6, #624]
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
	ldr	r0, [r6, #528]
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
	beq	.L266
	ldr	r1, [r0]
	ldr	r2, [r0, #8]
	ldr	r3, [r0, #80]
	ldr	ip, [r0, #84]
	ldr	r0, [r0, #88]
	b	.L279
.L264:
	ldr	r3, [sp, #12]
	mov	r1, #0
	strb	r1, [r6, #624]
	mov	r0, #2048
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #117440512
	orr	r3, r3, #16777216
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r7]
	ldr	r3, [r6, #528]
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
	str	r0, [r6, #628]
	beq	.L266
	ldr	r0, [r6, #528]
	ldr	r1, [r0]
	ldr	r2, [r0, #344]
	ldr	r3, [r0, #304]
	ldr	ip, [r0, #308]
	ldr	r0, [r0, #312]
.L279:
	str	r0, [sp, #4]
	str	ip, [sp]
	ldr	r0, .L280+28
	bl	printk
.L266:
	movw	r2, #658
	mov	r3, #1
	strb	r3, [r4, #656]
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #660]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L260
	ldr	r3, .L280+12
	ldr	r0, .L280+32
	ldrb	r1, [r3, #516]	@ zero_extendqisi2
	bl	printk
.L260:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L281:
	.align	2
.L280:
	.word	.LANCHOR2
	.word	.LC35
	.word	.LANCHOR1+68
	.word	.LANCHOR0
	.word	1446522928
	.word	1446588464
	.word	1052675
	.word	.LC36
	.word	.LC37
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
.L283:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	sub	r2, r3, #1
	str	r2, [sp, #4]
	bne	.L283
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
	ldr	r9, .L298
	mov	r5, r0
	ldr	r7, .L298+4
	mov	r6, r1
	add	r10, r9, #80
	mov	r4, #0
.L289:
	ldrb	r3, [r7, #662]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L297
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	timer_delay_ns
	ldrb	r3, [r7, #661]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L296
	cmp	r3, #35
	addne	r3, r9, r6
	ldrnesb	r3, [r3, #224]
	bne	.L295
	ldr	r3, .L298+8
	add	r2, r4, r8
	add	r3, r3, r2
.L296:
	ldrsb	r3, [r3, #5]
.L295:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L289
.L297:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L299:
	.align	2
.L298:
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
	ldr	r3, .L307
	ldr	r4, [r3, #664]
	ldr	r7, [r3, #528]
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
.L303:
	cmp	r9, r8
	beq	.L306
	ldrb	r3, [r9, #1]!	@ zero_extendqisi2
	mov	r0, #120
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	ldrsb	r3, [r4, #1]!
	str	r3, [r6, #2048]
	b	.L303
.L306:
	add	r5, r7, r5
	mov	r3, #22
	str	r3, [r5, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L308:
	.align	2
.L307:
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
	ldr	r5, .L315
	ldrb	r2, [r5, #661]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	ldmhifd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r5, #664]
	add	r2, r2, r0
	ldrb	r2, [r2, #120]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r1, #0
	mov	r4, r0
	bl	hynix_set_rr_para
	ldr	r2, [r5, #664]
	mov	r3, #0
	add	r4, r2, r4
	strb	r3, [r4, #120]
	ldmfd	sp!, {r3, r4, r5, pc}
.L316:
	.align	2
.L315:
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
	ldr	r3, .L320
	ldrb	r2, [r3, #516]	@ zero_extendqisi2
	ldr	r3, [r3, #528]
	cmp	r2, #9
	mov	r2, r0, asl #16
	mov	r0, r0, asl #8
	orr	r0, r2, r0
	orr	r0, r0, #3
	streq	r0, [r3, #80]
	strne	r0, [r3, #304]
	bx	lr
.L321:
	.align	2
.L320:
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
	ldr	r3, .L326
	ldrb	r2, [r3, #516]	@ zero_extendqisi2
	ldr	r3, [r3, #528]
	cmp	r2, #9
	ldreq	r0, [r3, #80]
	ldrne	r0, [r3, #304]
	ubfx	r0, r0, #8, #8
	bx	lr
.L327:
	.align	2
.L326:
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
	ldr	r1, .L336
	ands	ip, r0, #6
	ldr	r3, [r1, #528]
	ldr	r2, [r3]
	bfieq	r2, ip, #13, #1
	beq	.L332
	orr	r2, r2, #24576
	ldrb	r1, [r1, #516]	@ zero_extendqisi2
	bfc	r2, #15, #1
	tst	r0, #4
	orr	r2, r2, #196608
	movw	r0, #8321
	orrne	r2, r2, #32768
	cmp	r1, #9
	ldr	r1, .L336+4
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
.L332:
	str	r2, [r3]
	bx	lr
.L337:
	.align	2
.L336:
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
	ldr	r3, .L339
	mov	r2, #1
	mov	r0, r2, asl r0
	ldr	r1, [r3, #528]
	ldr	r3, [r1]
	bfi	r3, r0, #0, #8
	str	r3, [r1]
	bx	lr
.L340:
	.align	2
.L339:
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
	ldr	r3, .L342
	ldr	r2, [r3, #528]
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
	.global	flash_wait_device_ready_raw
	.type	flash_wait_device_ready_raw, %function
flash_wait_device_ready_raw:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L349
	mov	r6, r1
	mov	r7, r2
	ldrb	r3, [r4, #589]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L345
	ldr	r1, .L349+4
	movw	r2, #691
	ldr	r0, .L349+8
	bl	printk
	bl	dump_stack
.L345:
	add	r5, r4, r5
	ldr	r4, [r4, #528]
	mov	r10, r6, lsr #8
	mov	r9, r6, lsr #16
	ldrb	r8, [r5, #668]	@ zero_extendqisi2
	mov	fp, #120
	mov	r0, r8
	add	r4, r4, r8, asl #8
	bl	nandc_cs
.L346:
	str	fp, [r4, #2056]
	uxtb	r3, r6
	mov	r0, #120
	str	r3, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	bl	timer_delay_ns
	ldr	r5, [r4, #2048]
	uxtb	r5, r5
	sub	r3, r5, #255
	and	r2, r7, r5
	clz	r3, r3
	mov	r3, r3, lsr #5
	cmp	r2, r7
	orrne	r3, r3, #1
	cmp	r3, #0
	bne	.L346
	mov	r0, r8
	bl	nandc_de_cs
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
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
	ldrb	r2, [r6, #676]	@ zero_extendqisi2
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
	ldr	r3, [r5, #528]
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
	ldr	r2, .L394
	ldrb	r1, [r2]	@ zero_extendqisi2
	cmp	r1, #0
	bxeq	lr
	cmp	r1, #1
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, [r2, #528]
	bne	.L376
	ldr	r2, .L394+4
	ldrb	r2, [r2, #33]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	b	.L393
.L376:
	cmp	r1, #2
	bne	.L378
	add	r2, r2, r0
	ldrb	r1, [r2, #680]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r1, #0
	strb	r1, [r2, #680]
	ldr	r2, .L394+4
	ldrb	r2, [r2, #33]	@ zero_extendqisi2
	cmp	r2, r1
	ldmeqfd	sp!, {r3, r4, r5, pc}
.L393:
	add	r0, r3, r0, asl #8
	str	r2, [r0, #2056]
	ldmfd	sp!, {r3, r4, r5, pc}
.L378:
	cmp	r1, #3
	ldmnefd	sp!, {r3, r4, r5, pc}
	add	r2, r2, r0
	ldrb	r1, [r2, #680]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	add	r4, r3, r0, asl #8
	mov	r3, #239
	mov	r0, #50
	mov	r5, #0
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	strb	r5, [r2, #680]
	bl	timer_delay_ns
	str	r5, [r4, #2048]
	mov	r3, #1
	mov	r0, #150
	str	r3, [r4, #2048]
	str	r5, [r4, #2048]
	str	r5, [r4, #2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #218
	mov	r0, #150
	str	r3, [r4, #2056]
	bl	timer_delay_ns
	ldmfd	sp!, {r3, r4, r5, lr}
	b	nandc_wait_flash_ready_no_delay
.L395:
	.align	2
.L394:
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
	ldr	r3, .L421
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	cmp	r2, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r7, [r3, #528]
	ldreq	r3, .L421+4
	ldreqb	r3, [r3, #34]	@ zero_extendqisi2
	beq	.L420
	cmp	r2, #2
	bne	.L401
	add	r3, r3, r0
	ldrb	r2, [r3, #680]	@ zero_extendqisi2
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r1, .L421+4
	ldrb	r2, [r1, #16]	@ zero_extendqisi2
	cmp	r2, #2
	movne	r2, #4
	strb	r2, [r3, #680]
	ldrb	r3, [r1, #34]	@ zero_extendqisi2
.L420:
	cmp	r3, #0
	addne	r0, r7, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L401:
	cmp	r2, #3
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	add	r6, r3, r0
	ldrb	r3, [r6, #680]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r8, .L421+4
	mov	r4, r0, asl #8
	add	r5, r7, r4
	mov	r0, #50
	add	r4, r7, r4
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	cmp	r3, #2
	movne	r3, #4
	strb	r3, [r6, #680]
	mov	r3, #239
	str	r3, [r5, #2056]
	mov	r3, #145
	str	r3, [r5, #2052]
	bl	timer_delay_ns
	ldrb	r3, [r8, #11]	@ zero_extendqisi2
	mov	r0, #150
	cmp	r3, #9
	mov	r3, #1
	streq	r3, [r5, #2048]
	ldrneb	r2, [r6, #680]	@ zero_extendqisi2
	strne	r2, [r5, #2048]
	str	r3, [r5, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #223
	mov	r0, #150
	str	r3, [r4, #2056]
	bl	timer_delay_ns
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	nandc_wait_flash_ready_no_delay
.L422:
	.align	2
.L421:
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
	ldr	r2, .L433
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mvn	r5, #0
	ldrb	r4, [r2, #676]	@ zero_extendqisi2
	mov	r6, r0
	ldrb	r3, [r2, #589]	@ zero_extendqisi2
	mov	r8, r1
	rsb	r7, r4, #24
	mvn	r4, r5, asl r4
	and	r4, r4, r1, lsr r7
	mov	r5, r2
	uxtb	r4, r4
	cmp	r3, r4
	bhi	.L424
	ldr	r1, .L433+4
	movw	r2, #866
	ldr	r0, .L433+8
	bl	printk
	bl	dump_stack
.L424:
	ldrb	r3, [r5, #589]	@ zero_extendqisi2
	ldr	r9, .L433
	cmp	r3, r4
	ldmlsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	add	r4, r9, r4
	mvn	r3, #0
	bic	r7, r8, r3, asl r7
	ubfx	r8, r8, #24, #2
	ldrb	r10, [r4, #668]	@ zero_extendqisi2
	ldr	r5, [r9, #528]
	mov	r0, r10
	mov	r4, r10, asl #8
	bl	nandc_cs
	cmp	r8, #0
	bne	.L426
	mov	r0, r7
	bl	slc_phy_page_address_calc
	ldrb	r3, [r9]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r7, r0
	beq	.L427
	mov	r0, r10
	bl	zftl_flash_enter_slc_mode
	b	.L427
.L426:
	ldr	r3, [r9, #584]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L428
	ldrb	r3, [r9, #684]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r5, r4
	streq	r8, [r3, #2056]
	beq	.L427
.L428:
	mov	r0, r10
	bl	zftl_flash_exit_slc_mode
.L427:
	add	r4, r5, r4
	mov	r0, #0
	uxtb	r3, r7
	str	r0, [r4, #2056]
	str	r0, [r4, #2052]
	str	r0, [r4, #2052]
	str	r3, [r4, #2052]
	mov	r3, r7, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r7, lsr #16
	str	r3, [r4, #2052]
	str	r6, [r4, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	nandc_de_cs
.L434:
	.align	2
.L433:
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
	ldr	r4, .L441
	ldr	r5, .L441+4
.L437:
	ldr	r3, [r5, #528]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L438
	mov	r0, #10
	bl	timer_delay_ns
	subs	r4, r4, #1
	bne	.L437
	mvn	r0, #0
	b	.L436
.L438:
	mov	r0, #0
.L436:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L442:
	.align	2
.L441:
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
	ldr	r0, .L451
	ldr	r1, .L451+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L444:
	ldrb	r3, [r1, #662]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L450
	ldrb	r3, [r1, #661]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L444
.L450:
	ldmfd	sp!, {r3, r4, r5, lr}
	b	nandc_wait_flash_ready
.L452:
	.align	2
.L451:
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
	ldr	r5, .L455
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
.L456:
	.align	2
.L455:
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
	ldr	r2, .L458
	add	r2, r2, r1
	movw	r1, #685
	ldrsb	r2, [r2, r1]
	str	r2, [r0]
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L459:
	.align	2
.L458:
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
	ldr	r3, .L464
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, #239
	mov	r4, r0
	str	r6, [r0, #8]
	beq	.L461
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
	b	.L462
.L461:
	add	r3, r3, r1
	mov	r0, #20
	str	r0, [r4, #4]
	ldrb	r3, [r3, #368]	@ zero_extendqisi2
	str	r3, [r4]
	str	r2, [r4]
	str	r2, [r4]
	str	r2, [r4]
.L462:
	ldmfd	sp!, {r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L465:
	.align	2
.L464:
	.word	.LANCHOR2
	.fnend
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
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
	ldr	r8, .L485
	ubfx	r7, r0, #24, #2
	ldrb	r2, [r8, #676]	@ zero_extendqisi2
	rsb	r5, r2, #24
	mov	r4, r3, asl r5
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	sub	r4, r4, #1
	and	r5, r3, r0, lsr r5
	ldrb	r3, [r8, #589]	@ zero_extendqisi2
	and	r6, r4, r0
	and	r4, r4, r1
	uxtb	r5, r5
	cmp	r3, r5
	bhi	.L467
	ldr	r1, .L485+4
	movw	r2, #965
	ldr	r0, .L485+8
	bl	printk
	bl	dump_stack
.L467:
	add	r5, r8, r5
	ldr	r3, .L485
	ldr	r9, [r8, #528]
	ldrb	r10, [r5, #668]	@ zero_extendqisi2
	str	r3, [sp, #4]
	mov	r0, r10
	mov	fp, r10, asl #8
	bl	nandc_cs
	cmp	r7, #0
	ldr	r3, [sp, #4]
	bne	.L468
	mov	r0, r6
	bl	slc_phy_page_address_calc
	mov	r6, r0
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldr	r3, [sp, #4]
	mov	r4, r0
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L469
	mov	r0, r10
	bl	zftl_flash_enter_slc_mode
	b	.L469
.L468:
	ldr	r2, [r3, #584]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L470
	ldrb	r3, [r3, #684]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r9, fp
	streq	r7, [r3, #2056]
	beq	.L469
.L470:
	mov	r0, r10
	bl	zftl_flash_exit_slc_mode
.L469:
	ldrb	r3, [r8, #607]	@ zero_extendqisi2
	uxtb	r0, r6
	mov	r1, r6, lsr #8
	ldr	r2, .L485
	cmp	r3, #1
	ldrb	ip, [r8, #599]	@ zero_extendqisi2
	mov	r6, r6, lsr #16
	bne	.L471
	ldrb	r3, [r2, #600]	@ zero_extendqisi2
	add	r5, r9, fp
	mov	r8, #0
	str	r2, [sp, #4]
	str	ip, [r5, #2056]
	add	fp, r9, fp
	str	r8, [r5, #2052]
	str	r8, [r5, #2052]
	str	r0, [r5, #2052]
	str	r1, [r5, #2052]
	str	r6, [r5, #2052]
	str	r3, [r5, #2056]
	bl	nandc_wait_flash_ready
	ldr	r2, [sp, #4]
	ldr	r3, [r2, #584]
	ldrb	r2, [r3, #12]	@ zero_extendqisi2
	subs	r3, r7, r8
	movne	r3, #1
	cmp	r2, #3
	movne	r3, #0
	cmp	r3, r8
	uxtb	r3, r4
	strne	r7, [r5, #2056]
	str	r8, [fp, #2056]
	str	r8, [r5, #2052]
	str	r8, [r5, #2052]
	str	r3, [r5, #2052]
	mov	r3, r4, lsr #8
	mov	r4, r4, lsr #16
	str	r3, [r5, #2052]
	str	r4, [r5, #2052]
	mov	r3, #48
	str	r3, [fp, #2056]
	b	.L473
.L471:
	ldr	r2, [r2, #584]
	add	r3, r9, fp
	add	r9, r9, fp
	str	ip, [r3, #2056]
	str	r0, [r3, #2052]
	str	r1, [r3, #2052]
	str	r6, [r3, #2052]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r7, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	ldrb	r2, [r8, #600]	@ zero_extendqisi2
	strne	r7, [r3, #2056]
	str	r2, [r9, #2056]
	uxtb	r2, r4
	str	r2, [r3, #2052]
	mov	r2, r4, lsr #8
	mov	r4, r4, lsr #16
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	mov	r3, #48
	str	r3, [r9, #2056]
.L473:
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L486:
	.align	2
.L485:
	.word	.LANCHOR0
	.word	.LANCHOR1+696
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
	ldr	r6, .L520
	mov	r9, #0
	ldr	r10, .L520+4
	mov	r7, r6
.L499:
	ldr	r3, .L520+8
	ldr	r5, [r6, #528]
	ldrb	r4, [r3, r9, asl #3]	@ zero_extendqisi2
	cmp	r4, #69
	cmpne	r4, #152
	beq	.L488
	cmp	r4, #137
	cmpne	r4, #44
	moveq	r3, #1
	movne	r3, #0
	cmp	r4, #155
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L489
.L488:
	cmp	r8, #1
	ldrb	r1, [r7, #720]	@ zero_extendqisi2
	bne	.L490
	tst	r1, #1
	beq	.L489
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L491
	ldr	r0, .L520+12
	bl	printk
.L491:
	mov	r2, r9, asl #8
	cmp	r4, #44
	cmpne	r4, #137
	add	r3, r5, r2
	mov	r1, #239
	str	r1, [r3, #2056]
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #5
	beq	.L518
	cmp	r4, #155
	moveq	r1, #1
	movne	r1, #128
	streq	r1, [r3, #2052]
	moveq	r1, #3
	strne	r1, [r3, #2052]
	movne	r1, #1
	b	.L518
.L490:
	tst	r1, #4
	beq	.L489
	ldr	r3, [r10]
	tst	r3, #4096
	beq	.L495
	ldr	r0, .L520+16
	bl	printk
.L495:
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
	beq	.L518
	cmp	r4, #155
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #37
	movne	r0, #128
	strne	r0, [r3, #2052]
.L518:
	add	r5, r5, r2
	str	r1, [r3, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L489:
	add	r9, r9, #1
	cmp	r9, #4
	bne	.L499
	bl	nandc_wait_flash_ready
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L521:
	.align	2
.L520:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+688
	.word	.LC38
	.word	.LC39
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
	ldr	r3, .L523
	mov	r2, #255
	ldr	r3, [r3, #528]
	add	r0, r3, r0, asl #8
	str	r2, [r0, #2056]
	b	nandc_wait_flash_ready
.L524:
	.align	2
.L523:
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
	ldr	r3, .L528
	mov	r4, r1
	ldr	r5, [r3, #528]
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
	bhi	.L525
	ldrb	r1, [r4, #2]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	ldr	r0, .L528+4
	str	r1, [sp]
	ldrb	r1, [r4, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r4, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r6, #1
	bl	printk
.L525:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
	.word	.LC40
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
	ldr	r3, .L532
	ldr	r2, .L532+4
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	ldr	r4, [r2, #528]
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
.L533:
	.align	2
.L532:
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
	ldr	r3, .L538
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r0
	ldr	r7, [r3, #528]
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
	ldr	r0, .L538+4
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
.L535:
	ldr	r2, [r4, #2048]
	strb	r2, [r9], #1
	cmp	r9, r3
	bne	.L535
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
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.word	.LC41
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
	mov	r0, r0, asl #8
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldr	r4, [r3, #528]
	ldr	r3, .L551+4
	ldrb	r2, [r3, #33]	@ zero_extendqisi2
	mov	r6, r3
	cmp	r2, #0
	addne	r3, r4, r0
	addeq	r2, r4, r0
	add	r4, r4, r0
	moveq	r3, #162
	strne	r2, [r3, #2056]
	streq	r3, [r2, #2056]
	mov	r2, #0
	mov	r3, #128
	str	r3, [r4, #2056]
	str	r2, [r4, #2052]
	uxtb	r3, r1
	str	r2, [r4, #2052]
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
	beq	.L543
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L543
	ldr	r0, .L551+8
	mov	r1, r5
	bl	printk
.L543:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L552:
	.align	2
.L551:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC42
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
	ldr	r3, [r3, #528]
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
	ldrb	r1, [r2, #516]	@ zero_extendqisi2
	strb	r0, [r2, #660]
	cmp	r1, #9
	bne	.L558
	ldr	r1, [r2, #528]
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
	ldr	r2, [r2, #528]
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
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	ldr	r2, [r4, #532]
	cmp	r3, #9
	ldr	r3, [r4, #528]
	str	r2, [r3]
	ldr	r2, [r4, #536]
	ldr	r3, [r4, #528]
	str	r2, [r3, #4]
	ldr	r2, [r4, #540]
	bne	.L573
	str	r2, [r3, #16]
	ldr	r2, [r4, #544]
	str	r2, [r3, #32]
	ldr	r2, [r4, #548]
	str	r2, [r3, #80]
	ldr	r2, [r4, #552]
	str	r2, [r3, #84]
	ldr	r2, [r4, #556]
	str	r2, [r3, #520]
	ldr	r2, [r4, #560]
	str	r2, [r3, #8]
	b	.L574
.L573:
	str	r2, [r3, #8]
	ldr	r2, [r4, #544]
	str	r2, [r3, #12]
	ldr	r2, [r4, #548]
	str	r2, [r3, #304]
	ldr	r2, [r4, #552]
	str	r2, [r3, #308]
	ldr	r2, [r4, #556]
	str	r2, [r3, #336]
	ldr	r2, [r4, #560]
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
	ldrb	r3, [r4, #623]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L577
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r0, r5
	bl	flash_set_interface_mode
	mov	r0, r5
	bl	nandc_set_if_mode
	ldr	r3, .L583
	ldrb	r0, [r3, #549]	@ zero_extendqisi2
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
	.word	.LANCHOR0+688
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
	ldrb	r3, [r3, #516]	@ zero_extendqisi2
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
	ldr	r0, [r4, #528]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	ldr	r0, [r4, #528]
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
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r4, .L616
	mov	r5, r0
	mov	fp, r1
	mov	r9, r2
	ldrb	r3, [r4, #589]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L600
	ldr	r1, .L616+4
	movw	r2, #598
	ldr	r0, .L616+8
	bl	printk
	bl	dump_stack
.L600:
	ldrb	r2, [r4, #589]	@ zero_extendqisi2
	ldr	r3, .L616
	cmp	r2, r5
	bls	.L607
	add	r5, r3, r5
	ldr	r7, [r3, #528]
	ldr	r3, .L616+12
	ldrb	r6, [r5, #668]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r7, r10, asl #8
	beq	.L602
	ldr	r0, .L616+16
	mov	r1, r6
	mov	r2, r9
	mov	r3, r8
	bl	printk
.L602:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r6
	bne	.L603
	bl	zftl_flash_enter_slc_mode
	b	.L604
.L603:
	bl	zftl_flash_exit_slc_mode
.L604:
	mov	fp, r6, asl #8
	mov	r3, #96
	add	r5, r7, fp
	str	r3, [r5, #2056]
	uxtb	r3, r9
	str	r3, [r5, #2052]
	mov	r3, r9, lsr #8
	str	r3, [r5, #2052]
	mov	r3, r9, lsr #16
	str	r3, [r5, #2052]
	ldrb	r3, [r4, #721]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r4, #0
	bne	.L605
	mov	r3, #208
	str	r3, [r5, #2056]
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	and	r4, r0, #5
.L605:
	add	r7, r7, fp
	mov	r3, #96
	str	r3, [r7, #2056]
	uxtb	r3, r8
	str	r3, [r5, #2052]
	mov	r3, r8, lsr #8
	mov	r8, r8, lsr #16
	str	r3, [r5, #2052]
	mov	r3, #208
	str	r8, [r5, #2052]
	str	r3, [r7, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	mov	r5, r0
	mov	r0, r6
	bl	nandc_de_cs
	and	r3, r5, #5
	orrs	r4, r3, r4
	beq	.L606
	ldr	r0, .L616+20
	mov	r1, r9
	mov	r2, r5
	bl	printk
.L606:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L607:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L617:
	.align	2
.L616:
	.word	.LANCHOR0
	.word	.LANCHOR1+720
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC43
	.word	.LC44
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
	mvn	r9, #0
	ldr	r6, .L633
	mov	r7, r0
	mov	fp, r1
	mov	r8, r2
	ldrb	r3, [r6, #676]	@ zero_extendqisi2
	rsb	r3, r3, #24
	bic	r9, r2, r9, asl r3
	ldrb	r3, [r6, #589]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L619
	ldr	r1, .L633+4
	movw	r2, #645
	ldr	r0, .L633+8
	bl	printk
	bl	dump_stack
.L619:
	ldrb	r2, [r6, #589]	@ zero_extendqisi2
	ldr	r3, .L633
	cmp	r2, r7
	bls	.L625
	add	r2, r3, r7
	ldr	r4, [r3, #528]
	ldr	r3, .L633+12
	ldrb	r5, [r2, #668]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r5, #8
	tst	r3, #16
	add	r10, r4, r10, asl #8
	beq	.L621
	ldr	r0, .L633+16
	mov	r1, r5
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L621:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r5
	bne	.L622
	bl	zftl_flash_enter_slc_mode
	b	.L623
.L622:
	bl	zftl_flash_exit_slc_mode
.L623:
	add	r4, r4, r5, asl #8
	mov	r3, #96
	str	r3, [r4, #2056]
	uxtb	r3, r9
	str	r3, [r4, #2052]
	mov	r3, r9, lsr #8
	mov	r9, r9, lsr #16
	str	r3, [r4, #2052]
	mov	r3, #208
	str	r9, [r4, #2052]
	str	r3, [r4, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r5
	bl	nandc_de_cs
	ands	r4, r4, #5
	beq	.L624
	ldrh	r1, [r6, #2]
	mov	r0, r8
	bl	__aeabi_uidiv
	mov	r1, r7
	mov	r3, r4
	mov	r2, r0
	ldr	r0, .L633+20
	bl	printk
.L624:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L625:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L634:
	.align	2
.L633:
	.word	.LANCHOR0
	.word	.LANCHOR1+748
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC45
	.word	.LC46
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
	ldr	r3, .L644
	ldr	r7, .L644+4
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	mov	r8, r7
	smulbb	r4, r4, r3
	uxth	r4, r4
.L637:
	ldrb	r2, [r7, #589]	@ zero_extendqisi2
	uxth	r3, r6
	cmp	r2, r3
	bls	.L642
	add	r3, r7, r3
	mov	r5, #0
	ldrb	r9, [r3, #668]	@ zero_extendqisi2
.L638:
	uxth	r3, r5
	cmp	r3, r4
	bcs	.L643
	ldrh	r1, [r8, #2]
	mov	r0, r9
	mul	r1, r1, r5
	bl	flash_erase_block
	add	r5, r5, #1
	b	.L638
.L643:
	add	r6, r6, #1
	b	.L637
.L642:
	ldr	r0, .L644+8
	mov	r1, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L645:
	.align	2
.L644:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC47
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
	ldr	r3, .L649
	ldrb	r3, [r3, #516]	@ zero_extendqisi2
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
.L650:
	.align	2
.L649:
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
	ldr	r4, .L669
	.pad #20
	sub	sp, sp, #20
	mov	r8, r1
	mov	r6, r3
	mov	r7, r2
	ldr	r0, [r4, #528]
	bl	rk_nandc_xfer_irq_flag_init
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	ldr	r0, [r4, #528]
	and	r1, r5, #1
	cmp	r3, #9
	add	r3, r8, #1
	bne	.L652
	ldr	r2, [r0, #292]
	ubfx	r3, r3, #1, #6
	orr	r2, r2, #1
	str	r2, [r0, #292]
	ldr	r2, [r0, #288]
	orr	r2, r2, #1
	str	r2, [r0, #288]
	mov	r2, #0
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #2
	orr	r2, r2, r1, asl #1
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
	ldr	r0, [sp, #4]
	bic	r0, r0, #264241152
	orr	r3, r0, r3, asl #22
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	orr	r3, r3, #128
	str	r3, [sp, #4]
	ldrb	r3, [r4, #624]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L654
	ldrb	r3, [r4, #656]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [sp, #4]
	orrne	r3, r3, #512
	strne	r3, [sp, #4]
	b	.L654
.L652:
	ldr	r2, [r0, #368]
	ubfx	r3, r3, #1, #6
	orr	r2, r2, #1
	str	r2, [r0, #368]
	ldr	r2, [r0, #364]
	orr	r2, r2, #1
	str	r2, [r0, #364]
	ldr	r2, [r0, #12]
	mov	r0, #0
	cmp	r5, r0
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
	str	r0, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #2
	orr	r2, r2, r1, asl #1
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
	orr	r3, ip, r3, asl #22
	str	r3, [sp, #4]
	beq	.L668
	ldrb	r3, [r4, #660]	@ zero_extendqisi2
	mov	r8, r8, lsr #1
	cmp	r3, #25
	mov	r3, r0
	movcc	r1, #64
	movcs	r1, #128
	b	.L659
.L654:
	ldr	r1, [sp, #4]
	mov	r2, r5
	mov	r0, r7
	str	r7, [r4, #632]
	ubfx	r1, r1, #22, #6
	str	r6, [r4, #636]
	mov	r1, r1, asl #10
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #640]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #636]
	mov	r1, r1, asl #2
	bl	rknand_dma_map_single
	ldr	r2, [r4, #528]
	mov	r3, #1
	str	r3, [r4, #648]
	ldr	r3, [r4, #640]
	str	r3, [r2, #52]
	str	r0, [r2, #56]
	ldr	r3, [r2, #48]
	str	r0, [r4, #644]
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
	movw	r3, #658
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
	b	.L651
.L659:
	cmp	r3, r8
	add	r6, r6, #4
	add	lr, r0, r1
	bge	.L661
	ldrh	ip, [r6, #-2]
	mov	r0, r0, lsr #2
	ldr	r9, [r4, #628]
	add	r3, r3, #1
	ldrh	r2, [r6, #-4]
	orr	ip, r2, ip, asl #16
	str	ip, [r9, r0, asl #2]
	mov	r0, lr
	b	.L659
.L668:
	ldr	r3, [r4, #628]
	mov	r2, #1
	str	r2, [r3]
.L661:
	ldr	r1, [sp, #4]
	mov	r2, r5
	ldr	r3, [r4, #628]
	mov	r0, r7
	ubfx	r1, r1, #22, #6
	str	r7, [r4, #632]
	mov	r1, r1, asl #10
	str	r3, [r4, #636]
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #640]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #636]
	mov	r1, r1, asl #7
	bl	rknand_dma_map_single
	ldr	r2, [r4, #640]
	mov	r3, #1
	str	r3, [r4, #648]
	ldr	r3, [r4, #528]
	str	r2, [r3, #20]
	mov	r2, #0
	str	r0, [r3, #24]
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
	str	r0, [r4, #644]
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
.L651:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L670:
	.align	2
.L669:
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
	ldr	r2, .L678
	and	r0, r0, #127
	ldr	r3, .L678+4
	mov	r0, r0, asl #1
	ldrb	r1, [r2, #656]	@ zero_extendqisi2
	ldrh	r3, [r3, r0]
	cmp	r1, #0
	ldr	r1, .L678
	ldrb	r2, [r2, #516]	@ zero_extendqisi2
	orrne	r3, r3, #-1073741824
	cmp	r2, #9
	ldr	r2, [r1, #528]
	streq	r3, [r2, #520]
	strne	r3, [r2, #336]
	bx	lr
.L679:
	.align	2
.L678:
	.word	.LANCHOR0
	.word	.LANCHOR1+772
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
	ldr	r4, .L700
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L681
	ldrb	r0, [r4, #590]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L681
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	cmp	r3, #9
	beq	.L681
	bl	zftl_flash_exit_slc_mode
.L681:
	mov	r0, #0
	ldr	r5, .L700
	bl	hynix_reconfig_rr_para
	ldrb	r3, [r4, #623]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L682
	ldrb	r3, [r5, #720]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L682
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r5, #623]
.L682:
	ldrb	r3, [r4, #656]	@ zero_extendqisi2
	ldr	r5, .L700
	cmp	r3, #0
	beq	.L683
	mov	r0, #0
	strb	r0, [r5, #656]
	bl	nandc_set_seed
	mov	r3, #1
	strb	r3, [r5, #656]
.L683:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L701:
	.align	2
.L700:
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
	ldr	r3, .L703
	strb	r0, [r3, #656]
	bx	lr
.L704:
	.align	2
.L703:
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
	ldr	r3, .L706
	add	r0, r0, #8
	ldr	r3, [r3, #528]
	add	r0, r3, r0, asl #8
	bx	lr
.L707:
	.align	2
.L706:
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
	ldr	r4, .L712
	mov	r7, r5
	sub	r8, r4, #724
.L709:
	ldrb	r0, [r8, #2260]	@ zero_extendqisi2
	uxtb	r3, r5
	add	r2, r3, #1
	strb	r3, [r4, #1]
	strb	r2, [r4]
	add	r5, r5, #1
	mov	r0, r0, asl #9
	strb	r7, [r4, #2]
	str	r7, [r4, #8]
	bl	ftl_malloc
	ldr	r6, .L712+4
	add	r4, r4, #48
	str	r0, [r4, #-44]
	mov	r0, #64
	bl	ftl_malloc
	cmp	r5, #32
	str	r0, [r4, #-36]
	bne	.L709
	mvn	r3, #0
	strb	r5, [r6, #2262]
	strb	r3, [r6, #2212]
	mov	r3, #0
	strb	r3, [r6, #2261]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L713:
	.align	2
.L712:
	.word	.LANCHOR0+724
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
	ldr	r2, .L717
	mov	r3, #0
	mov	r1, r3
.L715:
	uxtb	r0, r3
	add	r3, r3, #1
	cmp	r3, #32
	add	ip, r0, #1
	strb	r0, [r2, #1]
	add	r2, r2, #48
	strb	ip, [r2, #-48]
	strb	r1, [r2, #-46]
	str	r1, [r2, #-40]
	bne	.L715
	ldr	r2, .L717+4
	mvn	r1, #0
	strb	r3, [r2, #2262]
	strb	r1, [r2, #2212]
	mov	r1, #0
	strb	r1, [r2, #2261]
	bx	lr
.L718:
	.align	2
.L717:
	.word	.LANCHOR0+724
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
	ldrne	r2, .L727
	movne	r1, #48
	bne	.L725
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L721
	ldr	r1, .L727+4
	mov	r2, #74
	ldr	r0, .L727+8
	bl	printk
	bl	dump_stack
.L721:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L725:
	mov	r5, r3
	mla	r3, r1, r3, r2
	ldrb	r3, [r3, #724]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L725
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L723
	ldr	r1, .L727+4
	mov	r2, #81
	ldr	r0, .L727+8
	bl	printk
	bl	dump_stack
.L723:
	ldr	r2, .L727
	mov	r1, #48
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	mla	r5, r1, r5, r2
	strb	r3, [r5, #724]
	ldmfd	sp!, {r3, r4, r5, pc}
.L728:
	.align	2
.L727:
	.word	.LANCHOR0
	.word	.LANCHOR1+1028
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
	ldr	r0, .L731
	mov	r3, #0
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, lr}
	b	buf_add_tail
.L732:
	.align	2
.L731:
	.word	.LANCHOR0+2263
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
	ldr	r4, .L743
	and	r3, r3, #8
	uxtb	r3, r3
	strb	r3, [r0, #2]
	cmp	r3, #0
	beq	.L734
	ldr	r3, [r0, #20]
	cmn	r3, #1
	beq	.L734
	mov	r1, r0
	ldr	r0, .L743+4
	bl	buf_add_tail
	b	.L735
.L734:
	ldrb	r3, [r4, #2261]	@ zero_extendqisi2
	strb	r3, [r0]
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	strb	r3, [r4, #2261]
.L735:
	ldrb	r3, [r4, #2262]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #2262]
	ldmfd	sp!, {r4, pc}
.L744:
	.align	2
.L743:
	.word	.LANCHOR0
	.word	.LANCHOR0+2261
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
	ldr	r4, .L756
	ldrb	r3, [r4, #2262]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L746
.L749:
	ldr	r2, .L756
	cmp	r5, #0
	ldrb	r1, [r4, #2261]	@ zero_extendqisi2
	mov	r0, #48
	add	r3, r2, #724
	mla	r0, r0, r1, r3
	beq	.L747
	b	.L748
.L746:
	ldr	r1, .L756+4
	mov	r2, #121
	ldr	r0, .L756+8
	bl	printk
	bl	dump_stack
	ldrb	r3, [r4, #2262]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L749
.L751:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L747:
	ldrb	r2, [r2, #2262]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L751
.L748:
	mov	r2, #48
	mul	r1, r2, r1
	add	r2, r4, r1
	add	r3, r3, r1
	mov	r1, #0
	ldrb	ip, [r2, #724]	@ zero_extendqisi2
	strh	r1, [r3, #34]	@ movhi
	str	r1, [r2, #732]
	strb	ip, [r4, #2261]
	ldrb	ip, [r4, #2262]	@ zero_extendqisi2
	strb	r1, [r2, #764]
	sub	ip, ip, #1
	strb	r1, [r2, #765]
	strb	ip, [r4, #2262]
	mov	ip, #1
	strb	ip, [r2, #726]
	mvn	ip, #0
	strb	ip, [r2, #724]
	str	ip, [r2, #744]
	ldmfd	sp!, {r3, r4, r5, pc}
.L757:
	.align	2
.L756:
	.word	.LANCHOR0
	.word	.LANCHOR1+1044
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
	bne	.L759
	ldrb	r3, [r1]	@ zero_extendqisi2
	strb	r3, [r0]
	mov	r0, #1
	bx	lr
.L759:
	ldr	r0, .L766
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #48
.L761:
	cmp	r3, #255
	beq	.L765
	mov	r2, r3
	mla	r3, lr, r3, r0
	ldrb	r3, [r3, #724]	@ zero_extendqisi2
	cmp	r3, ip
	bne	.L761
	ldr	r0, .L766
	mov	ip, #48
	ldrb	r3, [r1]	@ zero_extendqisi2
	mla	r2, ip, r2, r0
	mov	r0, #1
	strb	r3, [r2, #724]
	mvn	r3, #0
	strb	r3, [r1]
	ldr	pc, [sp], #4
.L765:
	mov	r0, #0
	ldr	pc, [sp], #4
.L767:
	.align	2
.L766:
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
	ldr	r3, .L774
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrb	r2, [r3, #2262]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	bne	.L769
	ldr	r1, .L774+4
	mov	r2, #172
	ldr	r0, .L774+8
	bl	printk
	bl	dump_stack
.L769:
	ldrb	r3, [r4, #2262]	@ zero_extendqisi2
	ldr	r6, .L774
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L774+12
	mov	r1, r5
	bl	buf_remove_buf
	cmp	r0, #1
	ldreqb	r3, [r6, #2262]	@ zero_extendqisi2
	subeq	r3, r3, #1
	streqb	r3, [r6, #2262]
	ldreqb	r3, [r5, #2]	@ zero_extendqisi2
	orreq	r3, r3, #1
	streqb	r3, [r5, #2]
	ldmfd	sp!, {r4, r5, r6, pc}
.L775:
	.align	2
.L774:
	.word	.LANCHOR0
	.word	.LANCHOR1+1056
	.word	.LC0
	.word	.LANCHOR0+2261
	.fnend
	.size	buf_remove_free, .-buf_remove_free
	.align	2
	.global	flash_check_bad_block
	.type	flash_check_bad_block, %function
flash_check_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L777
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L777+4
	ldr	r2, [r2, #664]
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
.L778:
	.align	2
.L777:
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
	ldr	r3, .L781
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	mov	r6, r0
	ldrh	r4, [r3, #18]
	mov	r1, r6
	ldr	r0, .L781+4
	smulbb	r4, r2, r4
	mov	r2, r5
	bl	printk
	ldr	r3, .L781+8
	mov	r2, r5, lsr #5
	mov	r1, #1
	and	r5, r5, #31
	ldr	r3, [r3, #664]
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
.L782:
	.align	2
.L781:
	.word	.LANCHOR2
	.word	.LC48
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
	bne	.L784
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #2
.L784:
	ldrb	r3, [r0]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #1
	sub	r3, r0, #1
	mov	r0, #0
.L786:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L795
	sub	r1, r2, #48
	uxtb	ip, r1
	cmp	ip, #9
	addls	r0, r1, r0, asl #4
	bls	.L786
	sub	r1, r2, #97
	cmp	r1, #5
	subls	r2, r2, #87
	bls	.L794
	sub	r1, r2, #65
	cmp	r1, #5
	bxhi	lr
	sub	r2, r2, #55
.L794:
	add	r0, r2, r0, asl #4
	b	.L786
.L795:
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
	ldr	r3, .L798
	ldr	r0, .L798+4
	bl	proc_create_data
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L799:
	.align	2
.L798:
	.word	.LANCHOR1+1072
	.word	.LC49
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
	ldr	r1, .L802
	ldr	r2, .L802+4
	bl	sprintf
	ldr	r1, .L802+8
	ldr	r5, .L802+12
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
	ldr	r1, .L802+16
	bl	sprintf
	ldr	r2, [r5, #520]
	ldr	r1, .L802+20
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #2264]
	ldr	r1, .L802+24
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L802+28
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r2, [r5, #2268]
	mov	r0, r4
	ldr	r1, .L802+32
	bl	sprintf
	ldr	r2, [r5, #520]
	ldr	r1, .L802+36
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r1, .L802+40
	ldr	r2, [r3, #524]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r1, .L802+44
	ldr	r2, [r3, #528]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r3, r5, #2272
	ldr	r1, .L802+48
	ldrh	r2, [r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2274
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+52
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2276
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+56
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2278
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+60
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2280
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+64
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2282
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+68
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r1, [r5, #2285]	@ zero_extendqisi2
	ldrb	r2, [r5, #2262]	@ zero_extendqisi2
	ldrb	r3, [r5, #2284]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L802+72
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+76
	ldrh	r2, [r3, #148]
	ldrh	r3, [r3, #146]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+80
	ldr	r2, [r3, #16]
	ldr	r3, [r3, #20]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+84
	ldr	r2, [r3, #24]
	ldr	r3, [r3, #28]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+88
	ldr	r2, [r3, #64]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+92
	ldr	r2, [r3, #68]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r2, .L802+96
	ldr	r1, .L802+100
	ldr	r3, [r3, #12]
	umull	r2, r3, r3, r2
	mov	r2, r3, lsr #3
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r2, [r5]	@ zero_extendqisi2
	ldr	r1, .L802+104
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r1, .L802+108
	ldr	r2, [r3, #556]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r1, .L802+112
	ldr	r2, [r3, #552]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+116
	ldr	r2, [r3, #52]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+120
	ldr	r2, [r3, #60]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+124
	ldr	r2, [r3, #76]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldr	r1, .L802+128
	ldr	r2, [r3, #8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #580]
	ldrh	r2, [r1, #16]
	ldrh	r3, [r1, #22]
	ldrb	r1, [r1, #25]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L802+132
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #580]
	ldrh	r2, [r1, #48]
	ldrh	r3, [r1, #54]
	ldrb	r1, [r1, #57]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L802+136
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #580]
	ldrh	r2, [r1, #80]
	ldrh	r3, [r1, #86]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L802+140
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldrh	r1, [r3, #88]
	ldrh	r2, [r3, #74]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #92]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #96]
	str	r1, [sp, #8]
	ldr	r1, .L802+144
	ldr	r3, [r3, #84]
	bl	sprintf
	ldr	r3, [r5, #2288]
	ldrh	r1, [r3, #90]
	ldrh	r2, [r3, #72]
	str	r1, [sp]
	add	r4, r4, r0
	ldrh	r1, [r3, #94]
	mov	r0, r4
	str	r1, [sp, #4]
	ldrh	r1, [r3, #98]
	str	r1, [sp, #8]
	ldr	r1, .L802+148
	ldr	r3, [r3, #80]
	bl	sprintf
	movw	r3, #2292
	ldrh	r2, [r5, r3]
	ldr	r1, .L802+152
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r1, #2298
	ldrh	r1, [r5, r1]
	movw	r3, #2294
	ldrh	r2, [r5, r3]
	movw	r3, #2296
	ldrh	r3, [r5, r3]
	str	r1, [sp]
	movw	r1, #2300
	ldrh	r1, [r5, r1]
	add	r4, r4, r0
	mov	r0, r4
	str	r1, [sp, #4]
	ldr	r1, [r5, #2304]
	str	r1, [sp, #8]
	ldr	r1, .L802+156
	bl	sprintf
	ldr	r1, [r5, #580]
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
	ldr	r1, .L802+160
	bl	sprintf
	ldr	r3, [r5, #580]
	ldr	r1, .L802+164
	ldr	r2, [r3, #544]
	str	r2, [sp]
	add	r4, r4, r0
	ldr	r2, [r5, #2288]
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
.L803:
	.align	2
.L802:
	.word	.LC1
	.word	.LC2
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	.word	-858993459
	.word	.LC70
	.word	.LC71
	.word	.LC72
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
	ldr	r4, .L807
	orr	r3, r3, #2
	strb	r3, [r0, #2]
	add	r0, r4, #2304
	add	r0, r0, #4
	bl	buf_add_tail
	ldrb	r0, [r4, #2284]	@ zero_extendqisi2
	add	r0, r0, #1
	uxtb	r0, r0
	strb	r0, [r4, #2284]
	ldmfd	sp!, {r4, pc}
.L808:
	.align	2
.L807:
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
	ldr	r8, .L852
	mov	r4, r1
	mov	r9, r2
	ldr	r7, .L852+4
	ldr	r3, [r8]
	tst	r3, #256
	beq	.L812
	ldr	r3, [r7, #568]
	mov	r1, r5
	ldr	r2, [r7, #576]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r0, asl #1
	ldr	r0, .L852+8
	ldrh	r3, [r2, r3]
	mov	r2, r4
	str	r3, [sp, #4]
	ldr	r3, .L852+12
	ldrh	r3, [r3, #52]
	str	r3, [sp, #8]
	movw	r3, #2298
	ldrh	r3, [r7, r3]
	str	r3, [sp, #12]
	mov	r3, r9
	bl	printk
.L812:
	ldr	r3, .L852+16
	ldrh	r2, [r3]
	mov	r6, r3
	cmp	r2, r5
	bhi	.L813
	ldr	r1, .L852+20
	movw	r2, #454
	ldr	r0, .L852+24
	bl	printk
	bl	dump_stack
.L813:
	ldrh	r3, [r6]
	ldr	r2, .L852+4
	cmp	r3, r5
	bhi	.L814
	ldr	r3, [r8]
	tst	r3, #1024
	beq	.L850
	mov	r1, r5
	mov	r2, r4
	mov	r3, r9
	ldr	r0, .L852+28
	bl	printk
	b	.L850
.L814:
	ldr	r1, [r2, #576]
	mov	r3, r5, asl #1
	ldrh	r3, [r1, r3]
	ldr	r1, [r2, #568]
	add	r1, r1, r5, asl #2
	ldrb	ip, [r1, #2]	@ zero_extendqisi2
	ands	r6, ip, #224
	bne	.L817
	cmp	r3, #0
	beq	.L834
	ldr	r0, .L852+24
	movw	r2, #462
	ldr	r1, .L852+20
	bl	printk
.L850:
	bl	dump_stack
	b	.L834
.L817:
	movw	r1, #2312
	ldrh	r1, [r2, r1]
	cmp	r1, r5
	beq	.L834
	ldr	r2, [r2, #580]
	ldrh	r1, [r2, #48]
	cmp	r1, r5
	beq	.L834
	ldrh	r1, [r2, #16]
	cmp	r1, r5
	beq	.L834
	ldrh	r1, [r2, #80]
	cmp	r1, r5
	beq	.L834
	ldr	r1, .L852+12
	add	lr, r1, #52
	ldrh	r0, [r1, #52]
	mov	r1, #0
.L818:
	cmp	r1, r0
	bcs	.L851
	ldrh	r6, [lr, #2]!
	cmp	r6, r5
	beq	.L834
	add	r1, r1, #1
	b	.L818
.L851:
	cmp	r4, #0
	bne	.L823
	movw	r1, #2908
	ldrh	lr, [r7, r1]
	cmp	lr, r5
	moveq	r0, r4
	beq	.L816
	ldr	r6, .L852+32
	mov	r1, r4
.L822:
	ldrh	r10, [r1, r6]
	cmp	r5, r10
	bne	.L821
	ldr	r0, [r8]
	ands	r0, r0, #256
	beq	.L816
	stmia	sp, {r5, lr}
	mov	r1, r5
	mov	r2, ip, lsr #5
	ldr	r0, .L852+36
	bl	printk
	b	.L834
.L821:
	add	r1, r1, #2
	cmp	r1, #16
	bne	.L822
	add	r6, r2, r9, asl #7
	add	r6, r6, #136
	b	.L824
.L823:
	add	r6, r2, #392
.L824:
	ldr	r2, [r8]
	tst	r2, #256
	beq	.L825
	str	r3, [sp]
	movw	r3, #2298
	ldrh	r3, [r7, r3]
	mov	r1, r5
	str	r0, [sp, #4]
	mov	r2, r4
	ldr	r0, .L852+40
	str	r3, [sp, #8]
	mov	r3, ip, lsr #5
	bl	printk
.L825:
	add	r1, r6, #128
	mov	r3, r6
	movw	r0, #65535
.L828:
	mov	r2, r3
	add	r3, r3, #2
	ldrh	ip, [r2]
	cmp	ip, r0
	bne	.L826
	cmp	r4, #0
	strh	r5, [r2]	@ movhi
	ldreq	r3, [r7, #580]
	ldrne	r2, [r7, #580]
	addeq	r9, r3, r9, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r3, [r9, #120]
	addne	r3, r3, #1
	addeq	r3, r3, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r3, [r9, #120]	@ movhi
	b	.L849
.L826:
	cmp	r3, r1
	bne	.L828
.L849:
	mov	r0, #1
	b	.L816
.L834:
	mov	r0, #0
.L816:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L853:
	.align	2
.L852:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC87
	.word	.LANCHOR0+2312
	.word	.LANCHOR0+564
	.word	.LANCHOR1+1200
	.word	.LC0
	.word	.LC88
	.word	.LANCHOR0+2912
	.word	.LC89
	.word	.LC90
	.fnend
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, #1
	ldr	r5, .L860
	mov	r7, r0
	add	r3, r5, #2928
	ldrb	r2, [r5, #676]	@ zero_extendqisi2
	ldrh	r3, [r3]
	rsb	r2, r2, #24
	ldrb	r1, [r5, #2930]	@ zero_extendqisi2
	rsb	r2, r3, r2
	mov	r0, r0, lsr r3
	mov	r2, r6, asl r2
	sub	r2, r2, #1
	uxth	r4, r0
	and	r0, r0, r2
	bl	__aeabi_uidiv
	ldr	r1, [r5, #2892]
	mov	r2, r4
	mov	r3, r7
	uxth	r8, r0
	ldr	r0, .L860+4
	bl	printk
	mov	r1, r6
	mov	r2, #0
	mov	r0, r8
	bl	gc_add_sblk
	ldr	r2, [r5, #2892]
	ldr	r0, .L860+8
	mov	r1, #0
.L855:
	uxth	r3, r1
	cmp	r3, r2
	bcs	.L859
	add	r3, r3, #292
	add	r1, r1, #1
	mov	r3, r3, asl #1
	ldrh	r3, [r0, r3]
	cmp	r3, r4
	bne	.L855
	b	.L856
.L859:
	cmp	r2, #5
	addls	r3, r2, #1
	addls	r2, r2, #292
	strls	r3, [r5, #2892]
	ldrls	r3, .L860+8
	movls	r2, r2, asl #1
	strlsh	r4, [r3, r2]	@ movhi
.L856:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L861:
	.align	2
.L860:
	.word	.LANCHOR0
	.word	.LC91
	.word	.LANCHOR0+2312
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
	ldr	r3, .L863
	ldr	r3, [r3, #2932]
	ldr	r0, [r3, r0, asl #2]
	bx	lr
.L864:
	.align	2
.L863:
	.word	.LANCHOR0
	.fnend
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.type	gc_write_completed, %function
gc_write_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L896
	add	r9, r4, #724
	mov	r5, r4
.L866:
	ldrb	r10, [r4, #2936]	@ zero_extendqisi2
	cmp	r10, #255
	beq	.L895
	mov	r3, #48
	ldr	r8, .L896+4
	mul	r3, r3, r10
	add	r2, r4, r3
	add	r3, r9, r3
	ldrb	r1, [r2, #724]	@ zero_extendqisi2
	ldrh	r7, [r3, #32]
	strb	r1, [r4, #2936]
	ldr	r1, [r2, #760]
	cmp	r1, #0
	beq	.L867
	add	r3, r8, #2160
	ldr	r2, [r2, #748]
	mov	r0, #1
	strh	r0, [r3]	@ movhi
	ldr	r0, .L896+8
	str	r2, [r4, #2888]
	bl	printk
	ldr	r1, .L896+12
	movw	r2, #805
	ldr	r0, .L896+16
	bl	printk
	bl	dump_stack
.L867:
	ldrb	r3, [r5, #2937]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L868
	ldrb	r3, [r4, #684]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [r4, #580]
	ldreqb	r6, [r3, #89]	@ zero_extendqisi2
	addeq	r6, r6, r6, asl #1
	beq	.L869
.L868:
	ldrb	r3, [r5, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r6, #1
	beq	.L869
	ldrb	r3, [r4, #2939]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r6, #1
	movne	r6, #2
.L869:
	ldr	r3, .L896+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L870
	mov	r3, #48
	ldr	r1, [r5, #2940]
	mul	r3, r3, r10
	ldr	r0, .L896+24
	ldrb	r1, [r1, r7]	@ zero_extendqisi2
	add	r2, r5, r3
	add	r3, r8, r3
	ldrb	r2, [r2, #725]	@ zero_extendqisi2
	ldrh	r3, [r3, #32]
	str	r1, [sp]
	mov	r1, r7
	bl	printk
.L870:
	mov	r3, #48
	ldr	r2, [r5, #2940]
	mla	r3, r3, r10, r5
	mov	fp, r7
	ldrb	r2, [r2, r7]	@ zero_extendqisi2
	ldrb	r3, [r3, #725]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L871
	ldr	r1, .L896+12
	movw	r2, #814
	ldr	r0, .L896+16
	bl	printk
	bl	dump_stack
.L871:
	mov	r3, #48
	mla	r10, r3, r10, r5
	ldrb	r3, [r10, #769]	@ zero_extendqisi2
	cmp	r3, #3
	beq	.L872
	ldrb	r3, [r4, #2937]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L872
	ldrb	r3, [r4, #2944]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L872
	ldrb	r3, [r4, #684]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L872
	ldrb	r3, [r4, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L873
	ldrb	r3, [r4, #2939]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L872
.L873:
	ldr	r2, [r5, #2940]
	mov	r3, #0
	mov	r1, #48
	mov	r0, r3
	add	r7, r2, r7
	b	.L874
.L872:
	ldr	r3, .L896+28
	mvn	r10, #0
	mov	r2, #0
	strh	r7, [r3]	@ movhi
	mov	r3, #48
.L875:
	rsb	r1, r7, fp
	uxth	r1, r1
	cmp	r1, r6
	bcs	.L866
	ldr	r1, [r5, #2940]
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	ldrb	r0, [r1, fp]	@ zero_extendqisi2
	strb	r10, [r1, fp]
	add	fp, fp, #1
	mul	r0, r3, r0
	add	r1, r5, r0
	add	r0, r8, r0
	strb	r2, [r1, #769]
	bl	zbuf_free
	ldrb	r1, [r5, #2319]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r5, #2319]
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #8]
	b	.L875
.L874:
	uxth	r2, r3
	cmp	r2, r6
	bcs	.L866
	ldrb	r2, [r7, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	mla	r2, r1, r2, r5
	strb	r0, [r2, #769]
	b	.L874
.L895:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L897:
	.align	2
.L896:
	.word	.LANCHOR0
	.word	.LANCHOR0+724
	.word	.LC92
	.word	.LANCHOR1+1212
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC93
	.word	.LANCHOR0+2880
	.fnend
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L908
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #580]
	ldrb	r0, [r2, #2945]	@ zero_extendqisi2
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	addne	r3, r3, #392
	movne	r1, #1
	bne	.L900
	add	ip, r3, r0, asl #1
	ldrh	ip, [ip, #120]
	cmp	ip, #0
	beq	.L905
	add	r3, r3, r0, asl #7
	add	r3, r3, #136
.L900:
	add	ip, r3, #128
	movw	lr, #65535
.L904:
	mov	r4, r3
	ldrh	r0, [r3], #2
	cmp	r0, lr
	beq	.L902
	cmp	r1, #0
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	ldreqb	r1, [r2, #2945]	@ zero_extendqisi2
	ldreq	r3, [r2, #580]
	ldrne	r2, [r2, #580]
	addeq	r3, r3, r1, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r2, [r3, #120]
	subne	r3, r3, #1
	subeq	r2, r2, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r2, [r3, #120]	@ movhi
	ldmfd	sp!, {r4, pc}
.L902:
	cmp	r3, ip
	bne	.L904
	ldmfd	sp!, {r4, pc}
.L905:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L909:
	.align	2
.L908:
	.word	.LANCHOR0
	.fnend
	.size	gc_get_src_blk, .-gc_get_src_blk
	.align	2
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r5, .L1012
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r5, #580]
	add	r3, r3, r0, asl #1
	ldrh	r4, [r3, #120]
	cmp	r4, #0
	bne	.L957
	cmp	r0, #0
	mov	r9, r1
	mov	r7, r2
	bne	.L913
	movw	r3, #2946
	mov	fp, r0
	mov	r4, r0
	strh	r0, [r5, r3]	@ movhi
.L914:
	uxth	r8, r7
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L919
	ldr	ip, .L1012+4
	add	r0, ip, #2
	ldrh	r2, [ip]
	str	ip, [sp, #12]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	_list_get_gc_head_node
	ldr	r3, .L1012+4
	ldr	r2, [sp, #8]
	mov	r10, r0
	ldr	ip, [sp, #12]
	add	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r3]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L915
	ldr	r3, .L1012+8
	mov	ip, r0, asl #1
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L916
	ldr	r3, .L1012
	mov	r1, r10
	ldr	r0, .L1012+12
	str	ip, [sp, #8]
	ldr	r3, [r3, #576]
	ldrh	r3, [r3, ip]
	bl	printk
	ldr	ip, [sp, #8]
.L916:
	ldr	r3, [r5, #576]
	ldrh	r2, [r3, ip]
	ldr	r3, .L1012+16
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L917
	mov	r1, #0
	mov	r0, r10
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L918
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r8
	bls	.L918
	b	.L919
.L917:
	ldr	r2, .L1012
	movw	r3, #2946
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	b	.L919
.L915:
	mov	r3, #0
	strh	r3, [ip]	@ movhi
	b	.L919
.L918:
	add	fp, fp, #1
	b	.L914
.L919:
	tst	r9, #2
	movne	r10, #0
	ldrne	fp, .L1012
	beq	.L921
.L922:
	uxth	r3, r10
	cmp	r3, r8
	bcs	.L926
	ldr	r3, .L1012+20
	add	r0, r3, #4
	ldrh	r2, [r3]
	str	r3, [sp, #12]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	_list_get_gc_head_node
	ldr	r3, .L1012+20
	ldr	r2, [sp, #8]
	mov	lr, r0
	add	r1, r2, #1
	movw	r2, #65535
	cmp	r0, r2
	strh	r1, [r3]	@ movhi
	ldr	r3, [sp, #12]
	beq	.L923
	ldr	r1, [fp, #576]
	mov	r2, r0, asl #1
	sub	lr, r3, #652
	ldrh	r1, [r1, r2]
	ldrh	r2, [lr]
	cmp	r1, r2
	bcs	.L923
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L925
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r8
	bls	.L925
	b	.L928
.L923:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L926
.L925:
	add	r10, r10, #1
	b	.L922
.L926:
	cmp	r4, r8
	bcs	.L928
	ldr	r0, .L1012
	movw	r1, #2300
	ldrh	r2, [r5, r1]
	add	r3, r0, #2960
	ldrh	ip, [r3]
	movw	r3, #2962
	ldrh	lr, [r5, r3]
	ldrb	r3, [r5, #2964]	@ zero_extendqisi2
	mul	r3, r3, lr
	sub	r3, r3, ip, lsr #2
	cmp	r2, r3
	addlt	r3, r2, ip, lsr #3
	strlth	r3, [r0, r1]	@ movhi
	b	.L921
.L928:
	ldr	r1, .L1012
	movw	r2, #2300
	ldrh	r3, [r5, r2]
	add	r0, r1, #2960
	ldrh	r0, [r0]
	cmp	r3, r0
	subhi	r3, r3, r0, lsr #3
	strhih	r3, [r1, r2]	@ movhi
.L921:
	tst	r9, #1
	beq	.L929
	cmp	r4, r8
	movcc	fp, #0
	ldrcc	r10, .L1012+24
	bcs	.L929
.L930:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L933
	ldrh	r3, [r10]
	ldr	r2, .L1012+24
	mov	r1, r3
	add	r0, r2, #2
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	_list_get_gc_head_node
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L931
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L932
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r8
	bcc	.L932
	b	.L935
.L931:
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L933
.L932:
	add	fp, fp, #1
	b	.L930
.L933:
	cmp	r4, r8
	bcs	.L935
	ldr	r1, .L1012
	movw	r2, #2298
	ldrh	r3, [r5, r2]
	add	r0, r1, #2960
	ldrh	r0, [r0]
	cmp	r3, r0
	addcc	r3, r3, r0, lsr #3
	bcc	.L1011
	b	.L929
.L935:
	ldr	r1, .L1012
	movw	r2, #2298
	ldrh	r3, [r5, r2]
	add	r0, r1, #2960
	ldrh	r0, [r0]
	cmp	r3, r0, lsr #1
	bls	.L929
	sub	r3, r3, r0, lsr #3
.L1011:
	strh	r3, [r1, r2]	@ movhi
	b	.L929
.L913:
	ldr	r2, .L1012+28
	movw	r3, #570
	strh	r4, [r2, r3]	@ movhi
	movw	r3, #2972
	strh	r4, [r5, r3]	@ movhi
	ands	r3, r1, #1
	str	r3, [sp, #8]
	ldreq	r4, [sp, #8]
	beq	.L936
	movw	r2, #2974
	movw	r3, #2280
	ldrh	r2, [r5, r2]
	ldrh	r3, [r5, r3]
	cmp	r3, r2, lsr #2
	bcc	.L937
	movw	r2, #2278
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	bls	.L938
.L937:
	movw	r3, #2278
	movw	r10, #2966
	ldrh	r3, [r5, r3]
	mov	r1, #0
	ldr	r0, .L1012+32
	ldr	r4, .L1012
	mov	r3, r3, lsr #2
	strh	r3, [r5, r10]	@ movhi
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	moveq	r4, #0
	beq	.L939
	ldr	r1, [r4, #576]
	add	r2, r4, #2960
	mov	r3, r0, asl #1
	ldrh	r2, [r2]
	ldrh	r3, [r1, r3]
	mov	r1, #0
	cmp	r3, r2, lsr #2
	movcs	r4, r1
	bcs	.L939
	mov	r2, r6
	strh	r1, [r4, r10]	@ movhi
	bl	gc_add_sblk
	adds	r4, r0, #0
	movne	r4, #1
.L939:
	ldr	r10, .L1012+24
	mov	r8, #64
	movw	fp, #65535
.L941:
	ldrh	r2, [r10]
	ldr	r0, .L1012+32
	mov	r1, r2
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	ldr	r2, [sp, #12]
	cmp	r0, fp
	add	r2, r2, #1
	strh	r2, [r10]	@ movhi
	beq	.L938
	mov	r1, #0
	mov	r2, r6
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L940
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r7
	bcs	.L938
.L940:
	sub	r3, r8, #1
	uxth	r8, r3
	cmp	r8, #0
	bne	.L941
.L938:
	movw	r3, #2280
	ldrh	r2, [r5, r3]
	movw	r3, #2974
	ldrh	r3, [r5, r3]
	cmp	r2, r3, lsr #3
	bhi	.L958
	ldr	r1, .L1012
	movw	r3, #2278
	ldrh	r3, [r1, r3]
	add	r3, r3, #8
	cmp	r2, r3
	bgt	.L958
.L936:
	tst	r9, #2
	beq	.L944
	ldr	r10, .L1012+20
	mov	r3, #0
	movw	r2, #2952
	mov	r8, #64
	mov	fp, r3
	strh	r3, [r5, r2]	@ movhi
.L949:
	ldrh	r2, [r10]
	ldr	r0, .L1012+36
	mov	r1, r2
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	ldr	r2, [sp, #12]
	add	r2, r2, #1
	strh	r2, [r10]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L945
	cmp	r7, #1
	ldr	r1, .L1012+40
	bne	.L946
	ldr	r3, .L1012
	add	r2, r1, #668
	ldrh	r2, [r2]
	ldrb	lr, [r3, #2964]	@ zero_extendqisi2
	smulbb	r2, lr, r2
	add	lr, r3, #2960
	ldrh	lr, [lr]
	sub	r2, r2, lr, lsr #3
	strh	r2, [r1]	@ movhi
.L946:
	ldr	lr, [r5, #576]
	mov	r2, r0, asl #1
	ldrh	lr, [lr, r2]
	ldrh	r2, [r1]
	cmp	lr, r2
	bcs	.L947
	mov	r1, #0
	mov	r2, r6
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L948
	add	r4, r4, #1
	add	r3, fp, #1
	uxth	r4, r4
	uxth	fp, r3
	cmp	r4, r7
	bcc	.L948
	b	.L945
.L947:
	ldr	r1, .L1012
	movw	r2, #2952
	mov	r0, #0
	strh	r0, [r1, r2]	@ movhi
	b	.L945
.L948:
	sub	ip, r8, #1
	uxth	r8, ip
	cmp	r8, #0
	bne	.L949
.L945:
	cmp	r4, r7
	ldr	r2, .L1012+44
	bcc	.L950
	cmp	fp, #0
	bne	.L951
	movw	r3, #2282
	ldrh	r1, [r5, r3]
	ldrh	r3, [r2, #16]
	cmp	r1, r3
	bls	.L951
.L950:
	ldrh	r3, [r2]
	movw	r2, #2962
	ldrh	ip, [r5, r2]
	movw	r1, #2294
	ldrb	r2, [r5, #2964]	@ zero_extendqisi2
	ldrh	r0, [r5, r1]
	mov	r3, r3, lsr #3
	mul	r2, r2, ip
	rsb	r2, r3, r2
	cmp	r0, r2
	addlt	r3, r3, r0
	blt	.L1010
	b	.L944
.L951:
	ldrh	r0, [r2]
	movw	r1, #2294
	ldrb	r2, [r5, #2964]	@ zero_extendqisi2
	ldrh	r3, [r5, r1]
	mul	r2, r2, r0
	cmp	r3, r2
	ble	.L944
	sub	r3, r3, r0, lsr #3
.L1010:
	ldr	r2, .L1012
	strh	r3, [r2, r1]	@ movhi
.L944:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L929
	ldr	fp, .L1012
	movw	r3, #2946
	mov	r10, #64
	mov	r2, #0
	strh	r2, [r5, r3]	@ movhi
.L955:
	ldr	r3, .L1012+4
	add	r0, r3, #2
	ldrh	r8, [r3]
	mov	r1, r8
	add	r8, r8, #1
	bl	_list_get_gc_head_node
	ldr	r3, .L1012+4
	movw	r2, #65535
	strh	r8, [r3]	@ movhi
	cmp	r0, r2
	beq	.L952
	ldr	r1, [fp, #576]
	mov	r2, r0, asl #1
	ldrh	r1, [r1, r2]
	ldr	r2, .L1012+48
	ldrh	ip, [r2]
	cmp	ip, r1
	cmphi	r1, #2
	bls	.L953
	ldrh	r1, [r3, #28]
	ldrh	r2, [r2, #-16]
	cmp	r2, r1, lsr #1
	bls	.L952
.L953:
	mov	r1, #0
	mov	r2, r6
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L954
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r7
	bcs	.L952
.L954:
	sub	r3, r10, #1
	uxth	r10, r3
	cmp	r10, #0
	bne	.L955
.L952:
	cmp	r4, r7
	ldr	r3, .L1012+44
	movw	r1, #2296
	bcs	.L956
	sub	r0, r3, #2960
	ldrh	r3, [r3]
	ldrh	r2, [r5, r1]
	cmp	r2, r3, lsr #1
	subhi	r2, r2, r3, lsr #3
	strhih	r2, [r0, r1]	@ movhi
	b	.L929
.L956:
	ldrh	r0, [r3]
	sub	ip, r3, #2960
	ldrb	r3, [r5, #2964]	@ zero_extendqisi2
	ldrh	r2, [r5, r1]
	mul	r3, r3, r0
	sub	r3, r3, #32
	cmp	r2, r3
	addlt	r2, r2, r0, lsr #3
	strlth	r2, [ip, r1]	@ movhi
.L929:
	ldr	r3, .L1012+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L957
	str	r7, [sp]
	mov	r1, r6
	ldr	r0, .L1012+52
	mov	r2, r9
	mov	r3, r4
	bl	printk
.L957:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L958:
	ldr	r10, .L1012+4
	movw	r3, #2946
	mov	r8, #64
	movw	fp, #65535
	mov	r2, #0
	strh	r2, [r5, r3]	@ movhi
.L943:
	ldrh	r2, [r10]
	ldr	r0, .L1012+56
	mov	r1, r2
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	ldr	r2, [sp, #12]
	cmp	r0, fp
	add	r2, r2, #1
	strh	r2, [r10]	@ movhi
	beq	.L936
	mov	r1, #0
	mov	r2, r6
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L942
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r7
	bcs	.L936
.L942:
	sub	r3, r8, #1
	uxth	r8, r3
	cmp	r8, #0
	bne	.L943
	b	.L936
.L1013:
	.align	2
.L1012:
	.word	.LANCHOR0
	.word	.LANCHOR0+2946
	.word	.LANCHOR2
	.word	.LC94
	.word	.LANCHOR0+2298
	.word	.LANCHOR0+2952
	.word	.LANCHOR0+2966
	.word	.LANCHOR0+2312
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2294
	.word	.LANCHOR0+2960
	.word	.LANCHOR0+2296
	.word	.LC95
	.word	.LANCHOR0+2948
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
	ldr	r4, .L1029
	ldrb	r0, [r4, #2319]	@ zero_extendqisi2
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r4, #2262]	@ zero_extendqisi2
	cmp	r3, #1
	bhi	.L1021
	add	r3, r4, #2880
	mov	r0, #48
	ldrh	r5, [r3]
	movw	r3, #2962
	ldrh	r1, [r4, r3]
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	add	r2, r5, #24
	mul	r3, r3, r1
	ldr	r1, [r4, #2940]
	cmp	r3, r2
	movcs	r3, r2
.L1016:
	cmp	r5, r3
	bcs	.L1021
	ldrb	r2, [r1, r5]	@ zero_extendqisi2
	cmp	r2, #255
	beq	.L1017
	mul	r2, r0, r2
	ldr	r6, .L1029
	add	ip, r4, r2
	ldrb	ip, [ip, #769]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L1017
	add	r0, r6, #724
	add	r0, r0, r2
	bl	zbuf_free
	ldr	r3, .L1029+4
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1018
	ldr	r3, [r6, #2940]
	mov	r1, r5
	ldr	r0, .L1029+8
	ldrb	r2, [r3, r5]	@ zero_extendqisi2
	bl	printk
.L1018:
	ldr	r3, [r4, #2940]
	mvn	r2, #0
	mov	r0, #1
	strb	r2, [r3, r5]
	ldrb	r3, [r4, #2319]	@ zero_extendqisi2
	add	r3, r3, r2
	strb	r3, [r4, #2319]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1017:
	add	r5, r5, #1
	b	.L1016
.L1021:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1030:
	.align	2
.L1029:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC96
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
	movw	r2, #2312
	ldr	r0, .L1033
	movw	ip, #566
	add	r3, r0, r2
	ldrh	r1, [r0, r2]
	ldrh	r2, [r3, #2]
	ldrh	r3, [r3, ip]
	ldrb	ip, [r0, #2262]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #2319]	@ zero_extendqisi2
	str	ip, [sp, #4]
	movw	ip, #2292
	ldrh	r0, [r0, ip]
	str	r0, [sp, #8]
	ldr	r0, .L1033+4
	bl	printk
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1034:
	.align	2
.L1033:
	.word	.LANCHOR0
	.word	.LC97
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
	ldreq	r0, .L1039
	beq	.L1038
	cmp	r1, #2
	ldreq	r0, .L1039+4
	ldrne	r0, .L1039+8
.L1038:
	mov	r1, r3
	b	_list_get_gc_head_node
.L1040:
	.align	2
.L1039:
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2948
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
	ldr	r2, .L1045
	mov	r1, r0
	ldr	r3, [r2, #568]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	addeq	r2, r2, #2272
	ldreq	r0, .L1045+4
	beq	.L1044
	cmp	r3, #16
	ldrne	r0, .L1045+8
	ldreq	r0, .L1045+12
	ldreq	r2, .L1045+16
	subne	r2, r0, #712
.L1044:
	b	_insert_free_list
.L1046:
	.align	2
.L1045:
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+2988
	.word	.LANCHOR0+2984
	.word	.LANCHOR0+2274
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
	ldr	r3, .L1052
	mov	r1, r0
	ldr	r3, [r3, #568]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1052+4
	ldreq	r2, .L1052+8
	beq	.L1051
.L1048:
	cmp	r3, #96
	ldreq	r0, .L1052+12
	subeq	r2, r0, #668
	beq	.L1051
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1052+16
	ldr	r2, .L1052+20
.L1051:
	b	_insert_data_list
.L1053:
	.align	2
.L1052:
	.word	.LANCHOR0
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2278
	.word	.LANCHOR0+2948
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2282
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
	ldr	r8, .L1068
	mov	r7, r0
	ldr	r3, [r8, #580]
	add	r3, r3, #588
	ldrh	r4, [r3]
	clz	r3, r0
	cmp	r4, r2
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L1055
	ldr	r0, .L1068+4
	mov	r1, r4
	bl	printk
	ldr	r3, [r8, #580]
	mvn	r2, #0
	add	r3, r3, #588
	strh	r2, [r3]	@ movhi
	b	.L1056
.L1055:
	movw	r3, #2274
	mov	r6, r1
	ldrh	r1, [r8, r3]
	movw	r3, #2276
	ldrh	r3, [r8, r3]
	mov	r5, r8
	cmp	r1, r3
	bls	.L1057
	cmp	r0, #0
	ldr	r2, .L1068+8
	movne	r1, r1, lsr #3
	moveq	r1, r0
	ldr	r0, .L1068+12
	b	.L1067
.L1057:
	cmp	r0, #0
	moveq	r1, r3, lsr #2
	beq	.L1061
	rsb	r1, r1, r1, asl #3
	add	r3, r1, #7
	cmp	r1, #0
	movlt	r1, r3
	ubfx	r1, r1, #3, #16
.L1061:
	ldr	r0, .L1068+16
	sub	r2, r0, #712
.L1067:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1062
	ldr	r3, .L1068+20
	mov	r1, r4
	ldr	r0, .L1068+24
	mov	r2, r6
	ldrh	r3, [r3]
	str	r3, [sp]
	movw	r3, #2276
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, [r5, #2980]
	bl	printk
.L1062:
	cmp	r7, #0
	beq	.L1056
	ldr	r3, .L1068+28
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1056
	ldr	r2, [r5, #568]
	mov	r0, r4, asl #2
	add	r3, r2, r0
	ldr	r1, [r2, r4, asl #2]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #11, #8
	str	r1, [sp]
	ldrh	r2, [r2, r0]
	ldr	r0, .L1068+32
	ubfx	r2, r2, #0, #11
	str	r2, [sp, #4]
	ldr	r1, [r5, #576]
	mov	r2, r4, asl #1
	ldrh	r2, [r1, r2]
	mov	r1, r4
	str	r2, [sp, #8]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
.L1056:
	mov	r0, r4
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1069:
	.align	2
.L1068:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LANCHOR0+2274
	.word	.LANCHOR0+2984
	.word	.LANCHOR0+2988
	.word	.LANCHOR0+2272
	.word	.LC99
	.word	.LANCHOR2
	.word	.LC100
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
	ldr	r5, .L1085
	bne	.L1071
	movw	r3, #2274
	ldrh	r1, [r5, r3]
	movw	r3, #2276
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	bcc	.L1072
	add	r3, r5, #2272
	ldrh	r3, [r3]
	cmp	r2, #0
	cmpne	r3, r1
	movhi	r1, #1
	movls	r1, #0
	bls	.L1073
.L1072:
	ldr	r0, .L1085+4
	mov	r1, r2, lsr #1
	b	.L1084
.L1073:
	ldr	r0, .L1085+8
	ldr	r2, .L1085+12
	b	.L1083
.L1071:
	ldr	r2, [r5, #580]
	movw	r7, #590
	ldrh	r4, [r2, r7]
	movw	r2, #65535
	cmp	r1, #1
	cmpne	r4, r2
	beq	.L1075
	ldr	r0, .L1085+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #580]
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1076
.L1075:
	add	r2, r5, #2272
	movw	r1, #2276
	ldrh	r1, [r5, r1]
	mov	r3, r0
	ldrh	r2, [r2]
	cmp	r2, r1
	bcc	.L1077
	movw	r0, #2274
	ldrh	r0, [r5, r0]
	cmp	r1, #0
	cmpne	r0, r2
	bls	.L1078
.L1077:
	cmp	r6, #1
	ldr	r0, .L1085+4
	moveq	r3, r1, lsr #1
	mov	r1, r3
.L1084:
	sub	r2, r0, #712
	b	.L1083
.L1078:
	cmp	r6, #1
	ldr	r0, .L1085+20
	moveq	r3, r2, lsr #1
	sub	r2, r0, #708
	mov	r1, r3
.L1083:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1076
	ldr	r3, .L1085+24
	mov	r1, r4
	ldr	r0, .L1085+28
	mov	r2, r6
	ldrh	r3, [r3]
	str	r3, [sp]
	movw	r3, #2276
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, [r5, #2980]
	bl	printk
.L1076:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1086:
	.align	2
.L1085:
	.word	.LANCHOR0
	.word	.LANCHOR0+2988
	.word	.LANCHOR0+2984
	.word	.LANCHOR0+2274
	.word	.LC101
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+2272
	.word	.LC99
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
	ldr	r3, .L1092
	mov	r1, r0
	ldr	r3, [r3, #568]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1092+4
	ldreq	r2, .L1092+8
	beq	.L1091
.L1088:
	cmp	r3, #96
	ldreq	r0, .L1092+12
	subeq	r2, r0, #668
	beq	.L1091
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1092+16
	ldr	r2, .L1092+20
.L1091:
	b	_list_remove_node
.L1093:
	.align	2
.L1092:
	.word	.LANCHOR0
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2278
	.word	.LANCHOR0+2948
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2282
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
	ldr	r2, .L1098
	mov	r1, r0
	ldr	r3, [r2, #568]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	addeq	r2, r2, #2272
	ldreq	r0, .L1098+4
	beq	.L1097
	cmp	r3, #16
	ldrne	r0, .L1098+8
	ldreq	r0, .L1098+12
	ldreq	r2, .L1098+16
	subne	r2, r0, #712
.L1097:
	b	_list_remove_node
.L1099:
	.align	2
.L1098:
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+2988
	.word	.LANCHOR0+2984
	.word	.LANCHOR0+2274
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
	ldr	r3, .L1105
	mov	r1, r0
	ldr	r3, [r3, #568]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1105+4
	ldreq	r2, .L1105+8
	beq	.L1104
.L1101:
	cmp	r3, #96
	ldreq	r0, .L1105+12
	subeq	r2, r0, #668
	beq	.L1104
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1105+16
	ldr	r2, .L1105+20
.L1104:
	b	_list_update_data_list
.L1106:
	.align	2
.L1105:
	.word	.LANCHOR0
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2278
	.word	.LANCHOR0+2948
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2282
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
	ldr	r0, .L1112
	bl	printk
	ldr	r4, [r4]
	cmp	r4, #0
	beq	.L1107
	ldr	r6, .L1112+4
	mov	r5, #0
	ldr	r7, .L1112+8
.L1110:
	ldr	r2, [r6, #524]
	ldrh	r0, [r4, #2]
	rsb	r2, r2, r4
	ldr	r1, [r6, #568]
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
	ldr	r0, [r6, #576]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #20]
	mov	r1, r2, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1112+12
	str	r1, [sp, #24]
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1107
	ldr	r3, [r6, #524]
	mov	r4, #6
	add	r5, r5, #1
	mla	r4, r4, r2, r3
	ldr	r3, .L1112+16
	uxth	r5, r5
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L1110
.L1107:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1113:
	.align	2
.L1112:
	.word	.LC102
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC103
	.word	.LANCHOR0+2992
	.fnend
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.type	dump_all_list_info, %function
dump_all_list_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L1116
	add	r0, r4, #2976
	add	r1, r4, #2272
	add	r0, r0, #4
	bl	print_list_info
	add	r0, r4, #2976
	add	r1, r4, #2272
	add	r0, r0, #8
	add	r1, r1, #2
	bl	print_list_info
	add	r0, r4, #2976
	add	r1, r4, #2272
	add	r0, r0, #12
	add	r1, r1, #4
	bl	print_list_info
	add	r0, r4, #2960
	add	r1, r4, #2272
	add	r0, r0, #8
	add	r1, r1, #6
	bl	print_list_info
	add	r0, r4, #2944
	add	r1, r4, #2272
	add	r0, r0, #4
	add	r1, r1, #8
	bl	print_list_info
	add	r0, r4, #2944
	add	r1, r4, #2272
	add	r0, r0, #12
	add	r1, r1, #10
	ldmfd	sp!, {r4, lr}
	b	print_list_info
.L1117:
	.align	2
.L1116:
	.word	.LANCHOR0
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
	ldr	r3, .L1123
	ldr	r3, [r3, #2288]
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
.L1124:
	.align	2
.L1123:
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
	ldr	r6, .L1135
	mov	r10, r0
	mov	r7, r1
	mov	r8, r5
	add	r2, r6, #2928
	ldr	r4, [r6, #568]
	mov	r9, r6
	add	r4, r4, r0, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
.L1126:
	ldrb	r1, [r6, #2964]	@ zero_extendqisi2
	cmp	r8, r1
	bge	.L1133
	mov	r1, r3, asr r8
	tst	r1, #1
	bne	.L1127
	ldrb	r4, [r9, #2930]	@ zero_extendqisi2
	mov	r0, r8
	str	r2, [sp, #4]
	mov	fp, r5, asl #1
	str	r3, [sp]
	add	r5, r5, #1
	mov	r1, r4
	bl	__aeabi_idiv
	smulbb	r4, r10, r4
	ldrb	r1, [r9, #676]	@ zero_extendqisi2
	rsb	r1, r1, #24
	ldr	r2, [sp, #4]
	ldr	r3, [sp]
	ldrh	ip, [r2]
	rsb	r1, ip, r1
	add	r0, r4, r0, asl r1
	uxth	r4, r0
	strh	r4, [r7, fp]	@ movhi
	ldrb	r1, [r9, #2930]	@ zero_extendqisi2
	cmp	r1, #1
	subhi	r1, r1, #1
	andhi	r1, r1, r8
	addhi	r4, r4, r1
	strhih	r4, [r7, fp]	@ movhi
.L1127:
	add	r8, r8, #1
	b	.L1126
.L1133:
	add	r7, r7, r5, asl #1
	mov	r0, r5
	mvn	r2, #0
.L1130:
	ldrb	r3, [r6, #2964]	@ zero_extendqisi2
	cmp	r5, r3
	strlth	r2, [r7], #2	@ movhi
	addlt	r5, r5, #1
	blt	.L1130
.L1134:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1136:
	.align	2
.L1135:
	.word	.LANCHOR0
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
	mov	r7, r1
	ldr	r3, .L1143
	add	r2, r3, #2928
	ldrb	r5, [r3, #676]	@ zero_extendqisi2
	mov	r4, r3
	ldrh	r2, [r2]
	rsb	r5, r5, #24
	rsb	r5, r2, r5
	mvn	r2, #0
	mov	r6, r0, asr r5
	bic	r5, r0, r2, asl r5
	ldrb	r2, [r3, #2938]	@ zero_extendqisi2
	uxtb	r6, r6
	sxth	r5, r5
	cmp	r2, #0
	beq	.L1138
	ldrb	r3, [r3, #2939]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1138
	movw	r3, #2994
	clz	r1, r1
	ldrh	r2, [r4, r3]
	mov	r0, r6
	mov	r1, r1, lsr #5
	mul	r2, r2, r5
	bl	flash_erase_block_en
.L1138:
	movw	r3, #2994
	mov	r0, r6
	ldrh	r2, [r4, r3]
	uxtb	r1, r7
	mul	r2, r2, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	flash_erase_block_en
.L1144:
	.align	2
.L1143:
	.word	.LANCHOR0
	.fnend
	.size	ftl_erase_phy_blk, .-ftl_erase_phy_blk
	.align	2
	.global	ftl_erase_sblk
	.type	ftl_erase_sblk, %function
ftl_erase_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1174
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	ip, .L1174+4
	mov	r9, r0, asl #2
	ldr	r3, [r2]
	mov	r7, r1
	mov	r8, r0
	mov	r5, #0
	mov	r6, ip
	mov	r10, ip
	str	r3, [sp, #84]
	ldr	r3, [ip, #568]
	str	r2, [sp, #8]
	add	r1, r3, r9
	ldrb	r3, [r1, #3]	@ zero_extendqisi2
	str	r3, [sp, #4]
.L1146:
	ldrb	r2, [r6, #2996]	@ zero_extendqisi2
	ldr	r3, .L1174+4
	cmp	r5, r2
	bge	.L1157
	ldrb	r2, [r10, #2930]	@ zero_extendqisi2
	mov	r4, #0
	sub	r3, r2, #1
	str	r3, [sp]
	ldr	r3, .L1174+8
	mul	lr, r2, r5
	mul	ip, r2, r8
	ldrh	r3, [r3]
	mov	r0, r3
	mov	r3, r4
.L1158:
	cmp	r3, r2
	bge	.L1173
	ldr	fp, [sp, #4]
	add	r1, r3, lr
	mov	r1, fp, asr r1
	tst	r1, #1
	bne	.L1147
	add	r1, sp, #88
	add	fp, r1, r4, asl #2
	ldr	r1, [sp]
	add	r4, r4, #1
	and	r1, r3, r1
	add	r1, ip, r1
	mul	r1, r0, r1
	str	r1, [fp, #-68]
.L1147:
	add	r3, r3, #1
	b	.L1158
.L1173:
	cmp	r2, #4
	bne	.L1171
	uxtb	r3, r5
	uxtb	ip, r7
	mov	fp, #0
.L1149:
	cmp	fp, r4
	beq	.L1152
	add	r2, sp, #20
	mov	r0, r3
	mov	r1, ip
	str	r3, [sp, #12]
	ldr	r2, [r2, fp, asl #2]
	add	fp, fp, #1
	str	ip, [sp]
	bl	flash_erase_block_en
	ldr	ip, [sp]
	ldr	r3, [sp, #12]
	b	.L1149
.L1171:
	cmp	r4, #2
	bne	.L1153
	ldrb	r3, [r6, #2938]	@ zero_extendqisi2
	uxtb	r4, r5
	cmp	r3, #0
	beq	.L1154
	ldrb	r3, [r10, #2939]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1154
	clz	r1, r7
	mov	r0, r4
	ldr	r2, [sp, #20]
	mov	r1, r1, lsr #5
	ldr	r3, [sp, #24]
	bl	flash_erase_duplane_block
.L1154:
	mov	r0, r4
	uxtb	r1, r7
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #24]
	bl	flash_erase_duplane_block
	b	.L1152
.L1153:
	cmp	r4, #1
	bne	.L1152
	ldrb	r3, [r6, #2938]	@ zero_extendqisi2
	uxtb	r4, r5
	cmp	r3, #0
	beq	.L1156
	ldrb	r3, [r10, #2939]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1156
	clz	r1, r7
	mov	r0, r4
	ldr	r2, [sp, #20]
	mov	r1, r1, lsr #5
	bl	flash_erase_block_en
.L1156:
	mov	r0, r4
	uxtb	r1, r7
	ldr	r2, [sp, #20]
	bl	flash_erase_block_en
.L1152:
	add	r5, r5, #1
	b	.L1146
.L1157:
	cmp	r7, #0
	ldr	r0, [r3, #568]
	bne	.L1159
	ldrh	r2, [r0, r9]
	ubfx	r1, r2, #0, #11
	add	r1, r1, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r0, r9]	@ movhi
	ldr	r2, [r3, #2288]
	ldr	r3, [r3, #568]
	ldr	r1, [r2, #84]
	ldrh	r0, [r2, #96]
	add	r1, r1, #1
	str	r1, [r2, #84]
	ldrh	r3, [r3, r9]
	ubfx	r3, r3, #0, #11
	uxth	r1, r3
	cmp	r0, r1
	strlth	r3, [r2, #96]	@ movhi
	b	.L1161
.L1159:
	ldr	r2, [r0, r8, asl #2]
	ubfx	r1, r2, #11, #8
	add	r1, r1, #1
	bfi	r2, r1, #11, #8
	str	r2, [r0, r8, asl #2]
	ldr	r2, [r3, #2288]
	ldr	r3, [r3, #568]
	ldr	r1, [r2, #80]
	add	r1, r1, #1
	str	r1, [r2, #80]
	ldr	r3, [r3, r8, asl #2]
	ldrh	r1, [r2, #98]
	ubfx	r3, r3, #11, #8
	cmp	r1, r3
	strcch	r3, [r2, #98]	@ movhi
.L1161:
	ldr	r3, [sp, #8]
	mov	r0, #0
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1163
	bl	__stack_chk_fail
.L1163:
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1175:
	.align	2
.L1174:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+2994
	.fnend
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1187
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2288]
	mov	r4, r3
	ldrh	r1, [r2, #136]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #136]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #0
	bne	.L1178
	ldr	r1, .L1187+4
	movw	r2, #1140
	ldr	r0, .L1187+8
	bl	printk
	bl	dump_stack
.L1178:
	ldr	r3, [r4, #2288]
	movw	lr, #65535
	mov	ip, #0
.L1182:
	ldrh	r2, [r3, #136]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1179:
	cmp	r2, #63
	bgt	.L1186
	ldrh	r0, [r1, #2]!
	cmp	r0, lr
	bne	.L1184
	add	r2, r2, #1
	b	.L1179
.L1186:
	strh	ip, [r3, #136]	@ movhi
	b	.L1182
.L1184:
	add	r1, r3, r2, asl #1
	mvn	ip, #0
	strh	ip, [r1, #160]	@ movhi
	strh	r2, [r3, #136]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, ip
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1188:
	.align	2
.L1187:
	.word	.LANCHOR0
	.word	.LANCHOR1+1232
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
	ldr	r3, .L1199
	ldr	r2, [r3, #2288]
	mov	r4, r3
	ldrh	r1, [r2, #138]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #138]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #63
	bls	.L1191
	ldr	r1, .L1199+4
	movw	r2, #1164
	ldr	r0, .L1199+8
	bl	printk
	bl	dump_stack
.L1191:
	ldr	r3, [r4, #2288]
	movw	ip, #65535
	mov	r0, #0
.L1195:
	ldrh	r2, [r3, #138]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1192:
	cmp	r2, #63
	bgt	.L1198
	ldrh	lr, [r1, #2]!
	cmp	lr, ip
	bne	.L1193
	add	r1, r3, r2, asl #1
	strh	r5, [r1, #160]	@ movhi
	strh	r2, [r3, #138]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, #1
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1193:
	add	r2, r2, #1
	b	.L1192
.L1198:
	strh	r0, [r3, #138]	@ movhi
	b	.L1195
.L1200:
	.align	2
.L1199:
	.word	.LANCHOR0
	.word	.LANCHOR1+1252
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
	ldr	r4, .L1211
	mov	r8, r6, asl #2
	ldr	r5, [r4, #568]
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
	ldr	r2, [r4, #2288]
	tst	r3, #8
	ldrneh	r3, [r2, #116]
	subne	r3, r3, #1
	strneh	r3, [r2, #116]	@ movhi
	bne	.L1206
	tst	r3, #24
	ldreqh	r3, [r2, #114]
	ldrneh	r3, [r2, #118]
	subeq	r3, r3, #1
	subne	r3, r3, #1
	streqh	r3, [r2, #114]	@ movhi
	strneh	r3, [r2, #118]	@ movhi
.L1206:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	bne	.L1208
	ldr	r3, [r5, r6, asl #2]
	ubfx	r2, r3, #11, #8
	add	r2, r2, #1
	bfi	r3, r2, #11, #8
	str	r3, [r5, r6, asl #2]
	ldr	r2, [r4, #2288]
	ldrh	r3, [r2, #120]
	sub	r3, r3, #1
	strh	r3, [r2, #120]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1208:
	ldrh	r2, [r5, r8]
	cmp	r3, #64
	ubfx	r1, r2, #0, #11
	add	r1, r1, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r5, r8]	@ movhi
	bne	.L1209
	ldr	r2, [r4, #2288]
	ldrh	r3, [r2, #122]
	sub	r3, r3, #1
	strh	r3, [r2, #122]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1209:
	cmp	r3, #96
	ldreq	r2, [r4, #2288]
	ldreqh	r3, [r2, #124]
	subeq	r3, r3, #1
	streqh	r3, [r2, #124]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1212:
	.align	2
.L1211:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r6, .L1218
	add	r3, r6, #2960
	ldrb	r2, [r6, #2964]	@ zero_extendqisi2
	ldrh	r3, [r3]
	ldr	r5, [r6, #580]
	mul	r1, r2, r3
	cmp	r0, r1
	addlt	r5, r5, #16
	addge	r5, r5, #48
	smulbbge	r3, r2, r3
	ldrb	r7, [r5, #9]	@ zero_extendqisi2
	rsbge	r4, r3, r0
	mov	r1, r7
	uxthge	r4, r4
	mov	r0, r4
	bl	__aeabi_idiv
	movw	r3, #65535
	smulbb	r7, r0, r7
	mov	r8, r0
	rsb	r4, r7, r4
	uxth	r4, r4
	add	r5, r5, r4, asl #1
	ldrh	r4, [r5, #16]
	cmp	r4, r3
	bne	.L1216
	ldr	r1, .L1218+4
	movw	r2, #1686
	ldr	r0, .L1218+8
	bl	printk
	bl	dump_stack
.L1216:
	movw	r3, #2994
	ldrh	r0, [r6, r3]
	mla	r0, r0, r4, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1219:
	.align	2
.L1218:
	.word	.LANCHOR0
	.word	.LANCHOR1+1272
	.word	.LC0
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
	ldr	r2, .L1226
	ldr	r1, .L1226+4
	mov	r3, r3, asl #1
	str	lr, [sp, #-4]!
	.save {lr}
	ldrh	r3, [r1, r3]
	ldr	ip, [r2, #3516]
	ldr	r1, [r2, #3512]
	movw	r2, #65535
.L1221:
	cmp	r3, r2
	beq	.L1225
	ldr	lr, [r1, r3, asl #2]
	cmp	lr, r0
	bne	.L1222
	mov	r0, r3
	ldr	lr, [sp], #4
	b	ftl_get_ppa_from_index
.L1222:
	mov	r3, r3, asl #1
	ldrh	r3, [ip, r3]
	b	.L1221
.L1225:
	mvn	r0, #0
	ldr	pc, [sp], #4
.L1227:
	.align	2
.L1226:
	.word	.LANCHOR0
	.word	.LANCHOR0+3000
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
	bne	.L1229
	ldr	r1, .L1238
	movw	r2, #1809
	ldr	r0, .L1238+4
	bl	printk
	bl	dump_stack
.L1229:
	ldr	r3, .L1238+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1230
	ldr	r1, .L1238
	movw	r2, #1810
	ldr	r0, .L1238+4
	bl	printk
	bl	dump_stack
.L1230:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L1231
	ldr	r1, .L1238
	movw	r2, #1811
	ldr	r0, .L1238+4
	bl	printk
	bl	dump_stack
.L1231:
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	movw	r2, #65535
	ldr	r1, .L1238+12
	mov	lr, #0
	add	r3, r4, r3, asl #1
	ldrb	r0, [r1, #2964]	@ zero_extendqisi2
	ldrh	ip, [r3, #16]
.L1232:
	cmp	ip, r2
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	bne	.L1237
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
	b	.L1232
.L1237:
	movw	r0, #2994
	add	r3, r3, #1
	ldrh	r0, [r1, r0]
	ldrb	r1, [r1, #2964]	@ zero_extendqisi2
	uxtb	r3, r3
	ldrh	r2, [r4, #2]
	mul	r0, r0, ip
	ldrh	ip, [r4, #6]
	cmp	r1, r3
	strb	r3, [r4, #5]
	sub	ip, ip, #1
	strh	ip, [r4, #6]	@ movhi
	ldrh	ip, [r4, #10]
	orr	r0, r0, r2
	moveq	r3, #0
	addeq	r2, r2, #1
	add	ip, ip, #1
	strh	ip, [r4, #10]	@ movhi
	streqh	r2, [r4, #2]	@ movhi
	streqb	r3, [r4, #5]
	ldmfd	sp!, {r4, pc}
.L1239:
	.align	2
.L1238:
	.word	.LANCHOR1+1296
	.word	.LC0
	.word	.LANCHOR0+2960
	.word	.LANCHOR0
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
	bls	.L1241
	ldr	r1, .L1243
	movw	r2, #1842
	ldr	r0, .L1243+4
	bl	printk
	bl	dump_stack
.L1241:
	ldr	r5, .L1243+8
	mov	r1, #0
	mov	r0, r4
	bl	ftl_erase_phy_blk
	ldr	r3, [r5, #2288]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sys_blk
	ldr	r3, [r5, #2288]
	mov	r0, #0
	strh	r4, [r3, #130]	@ movhi
	strh	r0, [r3, #140]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1244:
	.align	2
.L1243:
	.word	.LANCHOR1+1320
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
	ldr	r3, .L1258
	add	r1, r3, #3520
	ldrh	r2, [r1]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1]	@ movhi
	bxeq	lr
	add	ip, r3, #564
	mov	r0, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r6, #65535
	ldrh	lr, [ip]
	mov	ip, r0
	strh	r0, [r1]	@ movhi
	ldr	r2, [r3, #568]
	ldr	r1, [r3, #576]
	add	lr, r2, lr, asl #2
	sub	r1, r1, #2
.L1248:
	cmp	r2, lr
	beq	.L1257
	ldrh	r4, [r1, #2]!
	cmp	r4, r6
	beq	.L1249
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	and	r5, r5, #224
	cmp	r5, #160
	addeq	r0, r0, r4
	addne	ip, ip, r4
.L1249:
	add	r2, r2, #4
	b	.L1248
.L1257:
	ldr	r4, [r3, #580]
	ldr	r3, [r3, #2288]
	str	ip, [r4, #524]
	str	r0, [r4, #528]
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	bl	__aeabi_uidiv
	str	r0, [r4, #532]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1259:
	.align	2
.L1258:
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
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
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
	ldr	r3, .L1265
	mov	r4, r4, asl #1
	ldr	r2, [r3, #576]
	ldrh	r2, [r2, r4]
	cmp	r2, #0
	moveq	r0, #1
	movne	r0, #0
	streq	r0, [r3, #2304]
	ldmfd	sp!, {r4, pc}
.L1266:
	.align	2
.L1265:
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
	movw	r3, #65535
	cmp	r0, r3
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r6, .L1277
	beq	.L1268
	ldr	r2, [r6, #576]
	mov	r3, r0, asl #1
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1268
	ldr	r3, [r6, #568]
	mov	r1, r5
	mov	r2, r4
	add	r3, r3, r0, asl #2
	ldr	r0, .L1277+4
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1268:
	movw	r3, #2908
	ldr	r4, .L1277
	ldrh	r0, [r6, r3]
	cmp	r0, r5
	beq	.L1274
	movw	r2, #65535
	cmp	r0, r2
	streqh	r5, [r4, r3]	@ movhi
	beq	.L1274
	bl	ftl_vpn_update
	movw	r2, #3522
	ldrh	r3, [r4, r2]
	ldr	r1, .L1277+8
	add	r3, r3, #1
	uxth	r3, r3
	adds	r0, r0, #0
	movne	r0, #1
	cmp	r3, #7
	movhi	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #3522
	movw	r2, #2908
	ldrh	r3, [r6, r3]
	ldrh	ip, [r6, r2]
	strh	r5, [r6, r2]	@ movhi
	mov	r3, r3, asl #1
	strh	ip, [r1, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L1274:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1278:
	.align	2
.L1277:
	.word	.LANCHOR0
	.word	.LC104
	.word	.LANCHOR0+2912
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
	ldr	r3, .L1291
	movw	r4, #65535
	ldr	ip, .L1291+4
	mov	r6, r6, asl #1
	mov	r8, r4
	ldr	r7, [r3, #3512]
	ldrh	lr, [ip, r6]
	ldr	r9, [r3, #3516]
.L1280:
	cmp	lr, r8
	beq	.L1284
	ldr	r5, [r7, lr, asl #2]
	add	r10, r7, lr, asl #2
	cmp	r5, r0
	mov	r5, lr, asl #1
	bne	.L1281
	mvn	lr, #0
	str	lr, [r10]
	movw	lr, #65535
	cmp	r4, lr
	ldr	lr, [r3, #3516]
	movne	r4, r4, asl #1
	ldreqh	lr, [lr, r5]
	ldrneh	r7, [lr, r5]
	streqh	lr, [ip, r6]	@ movhi
	strneh	r7, [lr, r4]	@ movhi
	mvn	r4, #0
	ldr	lr, [r3, #3516]
	strh	r4, [lr, r5]	@ movhi
	b	.L1284
.L1281:
	mov	r4, lr
	ldrh	lr, [r9, r5]
	b	.L1280
.L1284:
	ldr	r4, [r3, #3512]
	cmn	r1, #1
	ldr	lr, .L1291
	str	r0, [r4, r2, asl #2]
	ldrh	r0, [ip, r6]
	ldr	r3, [r3, #3516]
	strh	r2, [ip, r6]	@ movhi
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	beq	.L1286
	add	r3, lr, #2928
	ldrh	r0, [r3]
	ldrb	r3, [lr, #676]	@ zero_extendqisi2
	mov	r1, r1, lsr r0
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r1, r0, asl r3
	ldrb	r1, [lr, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L1286:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1292:
	.align	2
.L1291:
	.word	.LANCHOR0
	.word	.LANCHOR0+3000
	.fnend
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1301
	mov	r3, #1
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	add	r2, r2, #2928
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	ldrb	r4, [r2, #-2252]	@ zero_extendqisi2
	ldrh	r2, [r2]
	rsb	r1, r4, #24
	mov	r4, r3, asl r4
	sub	r4, r4, #1
	and	r4, r4, r0, lsr r1
	rsb	r1, r2, r1
	mov	r3, r3, asl r1
	mov	r1, r5
	sub	r3, r3, #1
	uxtb	r4, r4
	and	r0, r3, r0, lsr r2
	smulbb	r6, r4, r5
	uxth	r8, r0
	bl	__aeabi_uidiv
	cmp	r5, #1
	subhi	r3, r5, #1
	uxtb	r6, r6
	andhi	r3, r3, r8
	uxth	r7, r0
	addhi	r3, r6, r3
	uxtbhi	r6, r3
	ldr	r3, .L1301+4
	ldr	r3, [r3]
	tst	r3, #16384
	beq	.L1295
	str	r8, [sp]
	mov	r1, r4
	ldr	r0, .L1301+8
	mov	r2, r6
	mov	r3, r7
	bl	printk
.L1295:
	ldr	r3, .L1301
	add	r2, r3, #564
	ldrh	r2, [r2]
	cmp	r2, r7
	ldrhi	r4, [r3, #568]
	movhi	r2, #1
	addhi	r4, r4, r7, asl #2
	ldrhib	r3, [r4, #3]	@ zero_extendqisi2
	orrhi	r6, r3, r2, asl r6
	strhib	r6, [r4, #3]
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1302:
	.align	2
.L1301:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC105
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
	ldr	r10, .L1318
	ldr	r3, [r10, #2892]
	cmp	r3, #0
	beq	.L1314
	ldr	r7, .L1318+4
	mov	r9, r0
	mov	fp, #0
	mov	r8, r7
.L1304:
	ldrb	r3, [r10, #2964]	@ zero_extendqisi2
	uxth	r0, fp
	ldr	r6, .L1318
	cmp	r3, r0
	bls	.L1314
	ldrb	r2, [r6, #2930]	@ zero_extendqisi2
	add	r6, r6, #2928
	mov	r1, r2
	uxth	r4, r2
	str	r2, [sp]
	bl	__aeabi_idiv
	ldrb	r3, [r6, #-2252]	@ zero_extendqisi2
	smulbb	r5, r9, r4
	ldrh	r1, [r6]
	rsb	r3, r3, #24
	ldr	ip, .L1318
	rsb	r3, r1, r3
	ldr	r2, [sp]
	add	r0, r5, r0, asl r3
	cmp	r2, #1
	uxth	r5, r0
	subhi	r4, r4, #1
	mov	r2, #0
	andhi	r4, r4, fp
	addhi	r5, r5, r4
	uxthhi	r5, r5
.L1306:
	ldr	r3, [r10, #2892]
	uxth	r4, r2
	cmp	r4, r3
	bcs	.L1316
	add	r3, r4, #292
	mov	r3, r3, asl #1
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bne	.L1307
	mov	r1, r5
	ldr	r0, .L1318+8
	stmia	sp, {r2, ip}
	bl	printk
	ldrh	r0, [r6]
	mov	r0, r5, asl r0
	bl	ftl_mask_bad_block
	ldr	ip, [sp, #4]
	ldr	r2, [sp]
	ldr	r1, [ip, #2892]
.L1308:
	cmp	r4, r1
	bcs	.L1317
	add	r3, r4, #292
	add	r3, r3, #1
	mov	r3, r3, asl #1
	ldrh	r0, [r8, r3]
	add	r3, r4, #292
	add	r4, r4, #1
	mov	r3, r3, asl #1
	uxth	r4, r4
	strh	r0, [r8, r3]	@ movhi
	b	.L1308
.L1317:
	sub	r1, r1, #1
	str	r1, [r10, #2892]
.L1307:
	add	r2, r2, #1
	b	.L1306
.L1316:
	add	fp, fp, #1
	b	.L1304
.L1314:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1319:
	.align	2
.L1318:
	.word	.LANCHOR0
	.word	.LANCHOR0+2312
	.word	.LC106
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
	mov	r2, r0, asl #2
	ldr	r5, .L1342
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	ldr	r7, [r5, #568]
	add	r9, r7, r2
	ldrb	r4, [r9, #2]	@ zero_extendqisi2
	tst	r4, #8
	mov	r3, r4, lsr #5
	str	r3, [sp]
	beq	.L1322
	ldr	r8, [r5, #2288]
	add	r1, r5, #572
	ldrh	fp, [r7, r2]
	ldr	r10, [r7, r0, asl #2]
	ldrh	ip, [r8, #74]
	ubfx	fp, fp, #0, #11
	ldrh	r3, [r8, #72]
	ubfx	r10, r10, #11, #8
	add	ip, fp, ip
	ldrh	r8, [r1]
	add	r3, r10, r3
	str	r2, [sp, #12]
	uxth	ip, ip
	str	ip, [sp, #4]
	mov	r1, r8
	uxth	r3, r3
	mov	r0, ip
	str	r3, [sp, #8]
	bl	__aeabi_uidiv
	movw	r1, #3524
	ldrh	r1, [r5, r1]
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #4]
	uxtah	r0, r3, r0
	ldr	r2, [sp, #12]
	cmp	r0, r1
	ble	.L1323
	movw	r3, #2282
	movw	r1, #2274
	ldrh	r3, [r5, r3]
	ldrh	r1, [r5, r1]
	add	r1, r3, r1
	add	r3, r5, #2976
	ldrh	r3, [r3]
	add	r3, r3, #8
	cmp	r1, r3
	blt	.L1329
	add	r1, r5, #2272
	movw	r3, #2278
	ldrh	r3, [r5, r3]
	ldrh	r1, [r1]
	add	r3, r3, r1
	movw	r1, #2280
	ldrh	r1, [r5, r1]
	add	r3, r3, r1
	movw	r1, #3526
	ldrh	r1, [r5, r1]
	add	r1, r1, #8
	b	.L1341
.L1323:
	mla	r3, r8, r3, ip
	movw	r1, #3528
	ldrh	r1, [r5, r1]
	cmp	r3, r1
	ble	.L1325
	add	r1, r5, #2272
	movw	r3, #2278
	ldrh	r3, [r5, r3]
	ldrh	r1, [r1]
	add	r3, r3, r1
	movw	r1, #2280
	ldrh	r1, [r5, r1]
	add	r3, r3, r1
	movw	r1, #3526
	ldrh	r1, [r5, r1]
	add	r0, r1, #8
	cmp	r3, r0
	blt	.L1339
	movw	r0, #2282
	ldrh	ip, [r5, r0]
	movw	r0, #2274
	ldrh	r0, [r5, r0]
	add	r5, r5, #2976
	add	r0, ip, r0
	ldrh	ip, [r5]
	add	ip, ip, #8
	cmp	r0, ip
	blt	.L1329
	add	r1, r1, #24
.L1341:
	cmp	r3, r1
	bge	.L1329
.L1339:
	bfc	r4, #3, #2
	b	.L1338
.L1329:
	mov	r3, #2
	bfi	r4, r3, #3, #2
.L1338:
	strb	r4, [r9, #2]
.L1325:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	bne	.L1330
	mul	r8, r8, r10
	ldrh	r3, [r7, r2]
	mov	r10, r10, lsr #3
	add	r8, r8, r8, asl #1
	add	fp, fp, r8, asr #2
	bfi	r3, fp, #0, #11
	strh	r3, [r7, r2]	@ movhi
	ldr	r4, [r7, r6, asl #2]
	bfi	r4, r10, #11, #8
	str	r4, [r7, r6, asl #2]
	b	.L1322
.L1330:
	cmp	r3, #16
	bne	.L1322
	mov	r0, fp
	mov	r1, r8
	str	r2, [sp, #4]
	bl	__aeabi_idiv
	ldr	r3, [r7, r6, asl #2]
	mov	fp, fp, asr #5
	add	r0, r0, r0, asl #1
	add	r10, r10, r0, asr #2
	bfi	r3, r10, #11, #8
	str	r3, [r7, r6, asl #2]
	ldr	r2, [sp, #4]
	ldrh	r3, [r7, r2]
	bfi	r3, fp, #0, #11
	strh	r3, [r7, r2]	@ movhi
.L1322:
	mov	r0, r6
	str	r2, [sp, #4]
	bl	zftl_remove_data_node
	ldr	r3, .L1342
	mov	r0, #0
	ldr	r1, [r3, #568]
	mov	r4, r3
	ldr	r2, [sp, #4]
	add	r2, r1, r2
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	bfc	r1, #5, #3
	strb	r1, [r2, #2]
	ldr	r1, [r3, #576]
	mov	r2, r6, asl #1
	strh	r0, [r1, r2]	@ movhi
	ldr	r2, [sp]
	add	r1, r2, #6
	and	r1, r1, #7
	cmp	r1, #4
	bhi	.L1332
	mov	r0, r6
	bl	gc_free_bad_sblk
.L1332:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1333
	ldr	r3, [r4, #580]
	movw	r2, #586
	ldrh	r1, [r3, r2]
	cmp	r1, r6
	bne	.L1333
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1333
	strh	r6, [r3, r2]	@ movhi
	mov	r1, r6
	ldr	r0, .L1342+4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1333:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	zftl_insert_free_list
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR0
	.word	.LC107
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
	ldr	r6, .L1383
	ldr	r8, .L1383+4
	mov	r7, r6
.L1345:
	ldrh	r2, [r8, #52]
	uxth	r3, r5
	cmp	r2, r3
	bls	.L1382
	add	r3, r8, r3, asl #1
	ldrh	r4, [r3, #54]
	ldr	r3, [r6, #576]
	mov	r9, r4, asl #1
	ldrh	r2, [r3, r9]
	cmp	r2, #0
	beq	.L1346
	ldr	r0, .L1383+8
	mov	r1, r4
	bl	printk
.L1346:
	ldr	r3, [r7, #576]
	mov	r2, #0
	strh	r2, [r3, r9]	@ movhi
	ldr	r3, [r7, #576]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	bne	.L1347
	ldr	r3, .L1383+12
	ldr	r9, [r6, #568]
	ldr	r3, [r3]
	add	r9, r9, r4, asl #2
	tst	r3, #256
	beq	.L1348
	ldrb	r2, [r9, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L1383+16
	mov	r2, r2, lsr #5
	bl	printk
.L1348:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L1349
	ldr	r1, .L1383+20
	movw	r2, #1115
	ldr	r0, .L1383+24
	bl	printk
	bl	dump_stack
.L1349:
	mov	r0, r4
	bl	ftl_free_sblk
	ldr	r3, [r7, #580]
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	beq	.L1350
	add	r0, r3, #388
	mov	r2, #0
	add	r0, r0, #2
.L1352:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1351
	add	r2, r2, #196
	mvn	r0, #0
	add	r1, r1, r0
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #124]	@ movhi
	b	.L1350
.L1351:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1352
.L1350:
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	beq	.L1353
	add	r0, r3, #134
	mov	r2, #0
.L1355:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1354
	add	r2, r3, r2, asl #1
	mvn	r0, #0
	add	r1, r1, r0
	strh	r0, [r2, #136]	@ movhi
	strh	r1, [r3, #120]	@ movhi
	b	.L1353
.L1354:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1355
.L1353:
	ldrh	r0, [r3, #122]
	cmp	r0, #0
	beq	.L1356
	add	r1, r3, #260
	mov	r2, #0
	add	r1, r1, #2
.L1358:
	ldrh	ip, [r1, #2]!
	cmp	ip, r4
	bne	.L1357
	add	r2, r2, #132
	mvn	r1, #0
	add	r0, r0, r1
	mov	r2, r2, asl #1
	strh	r1, [r3, r2]	@ movhi
	strh	r0, [r3, #122]	@ movhi
	b	.L1356
.L1357:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1358
	b	.L1356
.L1347:
	mov	r0, r4
	mov	r1, #1
	bl	gc_add_sblk
.L1356:
	add	r5, r5, #1
	b	.L1345
.L1382:
	ldr	r3, .L1383+4
	mov	r2, #0
	strh	r2, [r3, #52]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1384:
	.align	2
.L1383:
	.word	.LANCHOR0
	.word	.LANCHOR0+2312
	.word	.LC108
	.word	.LANCHOR2
	.word	.LC109
	.word	.LANCHOR1+1344
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
	movw	ip, #2274
	ldr	r4, .L1387
	.pad #32
	sub	sp, sp, #32
	ldr	r3, [r4, #2288]
	ldrh	ip, [r4, ip]
	ldr	r0, [r4, #580]
	ldrh	r1, [r3, #148]
	ldrh	r2, [r3, #146]
	add	r3, r4, #2272
	ldrh	r3, [r3]
	str	ip, [sp]
	movw	ip, #2276
	ldrh	ip, [r4, ip]
	str	ip, [sp, #4]
	movw	ip, #2278
	ldrh	ip, [r4, ip]
	str	ip, [sp, #8]
	movw	ip, #2280
	ldrh	ip, [r4, ip]
	str	ip, [sp, #12]
	movw	ip, #2282
	ldrh	ip, [r4, ip]
	str	ip, [sp, #16]
	ldr	ip, [r0, #524]
	str	ip, [sp, #20]
	ldr	r0, [r0, #528]
	str	r0, [sp, #24]
	ldr	r0, [r4, #2268]
	str	r0, [sp, #28]
	ldr	r0, .L1387+4
	bl	printk
	ldr	r2, [r4, #2288]
	ldrb	r3, [r4, #2285]	@ zero_extendqisi2
	ldr	r0, .L1387+8
	ldr	r1, [r2, #8]
	str	r1, [sp]
	ldr	r1, [r2, #64]
	str	r1, [sp, #4]
	ldr	r1, [r2, #20]
	ldr	r2, [r2, #28]
	bl	printk
	ldr	r2, [r4, #2288]
	ldr	r0, .L1387+12
	ldr	r3, [r2, #16]
	ldr	r1, [r2, #52]
	ldr	r2, [r2, #60]
	mov	r3, r3, lsr #11
	bl	printk
	ldr	r2, [r4, #2288]
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
	ldr	r0, .L1387+16
	ldr	r2, [r2, #84]
	bl	printk
	movw	r0, #2300
	ldrh	r0, [r4, r0]
	movw	r3, #2294
	ldrh	r1, [r4, r3]
	movw	r3, #2296
	ldrh	r2, [r4, r3]
	movw	r3, #2298
	ldrh	r3, [r4, r3]
	str	r0, [sp]
	movw	r0, #2292
	ldrh	r0, [r4, r0]
	str	r0, [sp, #4]
	ldr	r0, .L1387+20
	bl	printk
	ldr	r0, [r4, #580]
	movw	lr, #590
	ldr	ip, [r4, #2288]
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
	ldr	r0, .L1387+24
	bl	printk
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L1388:
	.align	2
.L1387:
	.word	.LANCHOR0
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
	.word	.LC114
	.word	.LC115
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
	bne	.L1390
	ldr	r1, .L1401
	mov	r2, #676
	ldr	r0, .L1401+4
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1390:
	ldr	r5, .L1401+8
	ldrb	r2, [r4, #40]	@ zero_extendqisi2
	ldrb	r3, [r5, #2260]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1395
	ldr	r1, .L1401
	movw	r2, #681
	ldr	r0, .L1401+4
	bl	printk
	bl	dump_stack
.L1395:
	ldrb	r3, [r4, #40]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1392
	ldrb	r2, [r5, #2260]	@ zero_extendqisi2
	ldr	r6, .L1401+8
	cmp	r2, r3
	bcs	.L1393
.L1392:
	mov	r0, r4
	bl	zbuf_free
	ldrb	r0, [r5, #2284]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, pc}
.L1393:
	mov	r1, r4
	ldr	r0, .L1401+12
	bl	buf_add_tail
	ldr	r3, [r6, #2288]
	ldrb	r1, [r4, #40]	@ zero_extendqisi2
	ldrb	r0, [r6, #2284]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	r0, r0, #1
	add	r2, r2, r1
	str	r2, [r3, #16]
	ldr	r2, [r3, #32]
	uxtb	r0, r0
	strb	r0, [r6, #2284]
	add	r2, r2, #1
	str	r2, [r3, #32]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1402:
	.align	2
.L1401:
	.word	.LANCHOR1+1360
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR0+2308
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
	ldr	r6, .L1415
	add	r9, r6, #724
	mov	r8, r6
.L1404:
	ldrb	r10, [r6, #3530]	@ zero_extendqisi2
	cmp	r10, #255
	beq	.L1414
	mov	r4, #48
	mul	r4, r4, r10
	add	r7, r9, r4
	add	r4, r6, r4
	add	r2, r4, #748
	ldrb	r3, [r4, #724]	@ zero_extendqisi2
	strb	r3, [r6, #3530]
	ldr	r3, [r4, #760]
	cmn	r3, #1
	bne	.L1405
	ldr	r3, .L1415+4
	ldr	r2, [r4, #748]
	ldrb	r1, [r6, #2930]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r6, #676]	@ zero_extendqisi2
	mov	r2, r2, lsr r0
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r3, [r6, #580]
	movw	r2, #65535
	ldr	r1, [r3, #560]
	cmp	r1, r2
	uxth	r5, r0
	streq	r5, [r3, #560]
	ldreq	r2, [r4, #748]
	ldr	r0, .L1415+8
	mov	r1, r5
	streq	r2, [r3, #564]
	mov	r3, #48
	mla	r3, r3, r10, r8
	ldr	r2, [r3, #744]
	ldr	r3, [r3, #748]
	bl	printk
	ldr	r3, [r8, #580]
	ldr	r2, [r3, #556]
	add	r2, r2, #1
	str	r2, [r3, #556]
	ldrh	r2, [r3, #16]
	cmp	r2, r5
	moveq	r2, #0
	streqh	r2, [r3, #22]	@ movhi
	beq	.L1408
	ldrh	r2, [r3, #48]
	cmp	r2, r5
	moveq	r2, #0
	streqh	r2, [r3, #54]	@ movhi
.L1408:
	mov	r0, r7
	mov	r5, #1
	bl	ftl_write_buf
	b	.L1404
.L1405:
	cmp	r5, #1
	add	r3, r4, #740
	bne	.L1410
	ldr	r1, [r3, #4]
	ldr	r2, [r4, #748]
	ldr	r0, .L1415+12
	bl	printk
	mov	r0, r7
	bl	ftl_write_buf
	b	.L1404
.L1410:
	ldr	r0, [r3, #4]
	ldr	r1, [r2, #4]
	ldrh	r2, [r7, #32]
	bl	lpa_hash_update_ppa
	ldrb	r3, [r4, #726]	@ zero_extendqisi2
	and	r5, r3, #4
	ands	r5, r5, #255
	bicne	r3, r3, #2
	strneb	r3, [r4, #726]
	movne	r5, #0
	bne	.L1404
	mov	r0, r7
	bl	zbuf_free
	b	.L1404
.L1414:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1416:
	.align	2
.L1415:
	.word	.LANCHOR0
	.word	.LANCHOR0+2928
	.word	.LC116
	.word	.LC117
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
	subs	r5, r0, #0
	bne	.L1418
	ldr	r1, .L1423
	mov	r2, #972
	ldr	r0, .L1423+4
	bl	printk
	bl	dump_stack
	ldmfd	sp!, {r3, r4, r5, lr}
	b	print_ftl_debug_info
.L1418:
	ldr	r4, .L1423+8
	ldrb	r2, [r5, #40]	@ zero_extendqisi2
	ldrb	r3, [r4, #2260]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1420
	ldr	r1, .L1423
	movw	r2, #977
	ldr	r0, .L1423+4
	bl	printk
	bl	dump_stack
.L1420:
	ldr	r0, .L1423+12
	mov	r1, r5
	bl	buf_add_tail
	ldrb	r3, [r4, #3532]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #3532]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1424:
	.align	2
.L1423:
	.word	.LANCHOR1+1376
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR0+3531
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
	uxth	r6, r0
	cmp	r0, #5
	mov	r5, r0
	mov	r1, r6
	mov	r0, #0
	movne	r7, #0
	moveq	r7, #2
	bl	zftl_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	beq	.L1427
	ldr	r3, .L1436
	ldr	r6, [r3, #568]
	add	r6, r6, r0, asl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L1428
	ldr	r1, .L1436+4
	mov	r2, #836
	ldr	r0, .L1436+8
	bl	printk
	bl	dump_stack
.L1428:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	bfi	r3, r5, #5, #3
	uxtb	r3, r3
	ubfx	r2, r3, #3, #2
	orr	r2, r7, r2
	bfi	r3, r2, #3, #2
	strb	r3, [r6, #2]
	b	.L1435
.L1427:
	bl	print_ftl_debug_info
	mov	r1, r4
	mov	r2, r5
	ldr	r0, .L1436+12
	bl	printk
	mov	r1, r6
	mov	r0, #0
	bl	zftl_get_free_sblk
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1436+12
	mov	r1, r4
	bl	printk
	bl	dump_all_list_info
	ldr	r1, .L1436+4
	movw	r2, #846
	ldr	r0, .L1436+8
	bl	printk
	bl	dump_stack
.L1435:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1437:
	.align	2
.L1436:
	.word	.LANCHOR0
	.word	.LANCHOR1+1396
	.word	.LC0
	.word	.LC118
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
	ldr	r3, .L1439
	mvn	r2, #0
	mov	r0, #0
	strb	r2, [r3, #2263]
	strb	r2, [r3, #3530]
	strb	r2, [r3, #3533]
	strb	r2, [r3, #2936]
	bx	lr
.L1440:
	.align	2
.L1439:
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
	ldr	r5, .L1451
	ldr	r0, .L1451+4
	ldrb	r1, [r5, #2263]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r5, #2263]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	add	r5, r5, #724
	mov	r3, #48
	mov	r6, r3
	mla	r4, r3, r4, r5
.L1443:
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldrb	r2, [r4, #42]	@ zero_extendqisi2
	ldr	r3, [r4, #24]
	ldr	r0, .L1451+8
	bl	printk
	ldrb	r4, [r4]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mla	r4, r6, r4, r5
	b	.L1443
.L1452:
	.align	2
.L1451:
	.word	.LANCHOR0
	.word	.LC119
	.word	.LC120
	.fnend
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.type	queue_lun_state, %function
queue_lun_state:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1469
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldrb	lr, [ip, #2263]	@ zero_extendqisi2
	cmp	lr, #255
	beq	.L1461
	add	r4, ip, #2928
	ldrb	r3, [ip, #676]	@ zero_extendqisi2
	mov	r2, #1
	mov	r9, #48
	ldrh	r8, [r4]
	rsb	r7, r3, #24
	mov	r3, r2, asl r3
	rsb	r4, r8, r7
	sub	r3, r3, #1
	mov	r2, r2, asl r4
	ldrb	r4, [ip, #2930]	@ zero_extendqisi2
	sub	r2, r2, #1
	uxth	r3, r3
	sub	r4, r4, #1
	and	r10, r3, r0, asr r7
	uxth	r2, r2
	uxth	r4, r4
	and	r5, r4, r0, asr r8
	and	r5, r5, r2
.L1460:
	mla	r0, r9, lr, ip
	ldr	r6, [r0, #748]
	and	fp, r3, r6, lsr r7
	cmp	fp, r10
	bne	.L1455
	and	r6, r4, r6, lsr r8
	ldrb	r0, [r0, #766]	@ zero_extendqisi2
	and	r6, r6, r2
	cmp	r6, r5
	bne	.L1456
	cmp	r1, #1
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1455
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1456:
	cmp	r1, #3
	ldrls	pc, [pc, r1, asl #2]
	b	.L1455
.L1457:
	.word	.L1454
	.word	.L1458
	.word	.L1459
	.word	.L1454
.L1458:
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1455
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1459:
	cmp	r0, #11
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1455:
	mla	lr, r9, lr, ip
	ldrb	lr, [lr, #724]	@ zero_extendqisi2
	cmp	lr, #255
	bne	.L1460
.L1461:
	mov	r0, #0
.L1454:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1470:
	.align	2
.L1469:
	.word	.LANCHOR0
	.fnend
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1498
	mov	ip, #0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r0, #48
	ldrb	r1, [r3, #2936]	@ zero_extendqisi2
	add	r10, r3, #724
	ldrb	r2, [r3, #2263]	@ zero_extendqisi2
	mov	r8, r3
	ldrb	r6, [r3, #3533]	@ zero_extendqisi2
	mvn	r9, #0
	ldrb	r7, [r3, #3530]	@ zero_extendqisi2
	mov	r4, r3
	mov	r5, r0
	str	r1, [sp, #4]
.L1472:
	cmp	r2, #255
	beq	.L1473
	mla	fp, r0, r2, r3
	mov	r1, r2
	ldrb	lr, [fp, #766]	@ zero_extendqisi2
	sub	lr, lr, #12
	cmp	lr, #1
	bls	.L1474
	cmp	ip, #0
	ldrne	r3, .L1498
	beq	.L1471
	b	.L1496
.L1474:
	mul	ip, r0, r1
	ldrb	r2, [fp, #724]	@ zero_extendqisi2
	add	lr, r8, ip
	ldrb	fp, [lr, #767]	@ zero_extendqisi2
	strb	r9, [lr, #724]
	cmp	fp, #1
	bne	.L1477
	add	ip, r10, ip
	ldrh	ip, [ip, #34]
	cmp	ip, #0
	moveq	ip, r7
	ldreq	lr, .L1498+4
	ldrne	ip, [sp, #4]
	ldrne	lr, .L1498+8
	b	.L1478
.L1477:
	cmp	fp, #0
	beq	.L1497
.L1479:
	mov	ip, #1
	b	.L1472
.L1497:
	ldr	ip, [lr, #744]
	cmn	ip, #1
	movne	ip, r6
	ldrne	lr, .L1498+12
	beq	.L1479
.L1478:
	cmp	ip, #255
	bne	.L1483
	strb	r2, [r3, #2263]
	mov	r2, #48
	mla	r1, r2, r1, r3
	ldrb	r3, [r1, #725]	@ zero_extendqisi2
	strb	r3, [lr]
	b	.L1471
.L1483:
	mov	lr, ip
	mla	ip, r0, ip, r3
	ldrb	ip, [ip, #724]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1483
	mla	r1, r5, r1, r4
	mla	lr, r5, lr, r4
	ldrb	r1, [r1, #725]	@ zero_extendqisi2
	strb	r1, [lr, #724]
	b	.L1479
.L1473:
	cmp	ip, #0
	beq	.L1471
	mvn	r2, #0
.L1496:
	strb	r2, [r3, #2263]
.L1471:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1499:
	.align	2
.L1498:
	.word	.LANCHOR0
	.word	.LANCHOR0+3530
	.word	.LANCHOR0+2936
	.word	.LANCHOR0+3533
	.fnend
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_alloc_new_blk
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	movw	r1, #690
	ldr	r4, .L1517
	ldr	r2, [r4, #2288]
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrb	r1, [r4, #2964]	@ zero_extendqisi2
	cmp	r1, r3
	bls	.L1501
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1502
.L1501:
	ldr	r5, .L1517
.L1514:
	mov	r0, #1
	ldr	r7, .L1517
	bl	ftl_alloc_sblk
	mov	r1, #0
	mov	r6, r0
	bl	ftl_erase_sblk
	ldr	r1, [r4, #2288]
	mov	r0, r6
	add	r1, r1, #672
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L1503
	mov	r1, r6
	ldr	r0, .L1517+4
	bl	printk
	ldr	r3, [r5, #568]
	add	r6, r3, r6, asl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	b	.L1514
.L1503:
	ldr	r3, [r7, #2288]
	movw	r2, #690
	mov	r5, #0
	movw	r1, #65535
	strh	r5, [r3, r2]	@ movhi
	add	r3, r3, #412
	add	r3, r3, #2
	mov	r2, #1
	str	r2, [r7, #3536]
.L1505:
	ldrh	r0, [r3, #2]!
	uxth	r2, r5
	cmp	r0, r1
	beq	.L1504
	add	r5, r5, #1
	cmp	r5, #128
	bne	.L1505
	mov	r2, #227
	ldr	r1, .L1517+8
	ldr	r0, .L1517+12
	bl	printk
	bl	dump_stack
	mov	r2, r5
.L1504:
	add	r2, r2, #208
	ldr	r3, [r4, #2288]
	mov	r2, r2, asl #1
	strh	r6, [r3, r2]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L1502:
	ldr	r2, [r4, #2288]
	movw	r3, #690
	ldrh	r3, [r2, r3]
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r5, [r2, r3]
	movw	r2, #65533
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1507
	ldr	r1, .L1517+8
	mov	r2, #232
	ldr	r0, .L1517+12
	bl	printk
	bl	dump_stack
.L1507:
	ldr	r1, [r4, #2288]
	mov	r2, #0
	add	r3, r1, #696
	strh	r2, [r3]	@ movhi
	ldr	r2, .L1517+16
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	ldrh	r4, [r2]
	rsb	r3, r3, #24
	movw	r2, #694
	rsb	r4, r4, r3
	mov	r3, r5, asr r4
	strh	r3, [r1, r2]	@ movhi
	ldr	r2, .L1517+20
	add	r1, r1, #692
	strh	r5, [r1]	@ movhi
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L1511
	mvn	r2, #0
	uxth	r3, r3
	ldr	r0, .L1517+24
	mov	r1, r5
	str	r3, [sp]
	mvn	r2, r2, asl r4
	mov	r3, r5
	bl	printk
.L1511:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1518:
	.align	2
.L1517:
	.word	.LANCHOR0
	.word	.LC121
	.word	.LANCHOR1+1412
	.word	.LC0
	.word	.LANCHOR0+2928
	.word	.LANCHOR2
	.word	.LC122
	.fnend
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1534
	mov	r3, #0
	movw	ip, #65535
.L1521:
	mov	r1, r3, asl #3
	uxth	r0, r3
	ldrh	r1, [r1, r2]
	cmp	r1, ip
	ldr	r1, .L1534
	bxeq	lr
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1521
	mov	r2, #0
	mov	r0, r3
	mov	ip, #32768
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.L1523:
	add	r3, r1, r2, asl #3
	uxth	lr, r2
	ldrh	r3, [r3, #2]
	tst	r3, #32768
	bne	.L1522
	cmp	r3, ip
	movcc	ip, r3
	movcc	r0, lr
.L1522:
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L1523
	cmp	r0, #32
	mov	r4, r0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1534+4
	mvn	r2, #0
	ldr	r5, .L1534
	ldrb	lr, [r3, #3796]	@ zero_extendqisi2
	mov	r3, #0
.L1525:
	mov	ip, r3, asl #3
	uxth	r6, r3
	add	r0, r1, ip
	ldrh	r0, [r0, #2]
	cmp	r0, r2
	bcs	.L1524
	ldrh	ip, [ip, r5]
	cmp	ip, lr
	movne	r2, r0
	movne	r4, r6
.L1524:
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1525
	cmp	r4, #32
	bne	.L1526
	ldr	r1, .L1534+8
	movw	r2, #331
	ldr	r0, .L1534+12
	bl	printk
	bl	dump_stack
.L1526:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1535:
	.align	2
.L1534:
	.word	.LANCHOR0+3540
	.word	.LANCHOR0
	.word	.LANCHOR1+1432
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
	cmp	r0, #0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bne	.L1538
	ldr	r3, .L1567
.L1539:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L1539
.L1543:
	ldr	r0, .L1567+4
	mov	r1, #255
	mov	r2, #1024
	bl	ftl_memset
	mov	r3, #0
	ldr	ip, .L1567
	ldr	r0, .L1567+4
	b	.L1540
.L1538:
	cmp	r0, #1
	bne	.L1541
	ldr	ip, .L1567
	mov	r3, #0
.L1542:
	uxth	r2, r3
	mov	lr, r3, asl #1
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
	cmp	r3, #256
	strh	r2, [lr, ip]	@ movhi
	bne	.L1542
	b	.L1543
.L1541:
	cmp	r0, #2
	bne	.L1544
	ldr	r1, .L1567
	mov	r3, #0
.L1545:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L1545
	b	.L1543
.L1544:
	cmp	r0, #3
	bne	.L1546
	ldr	ip, .L1567
	mov	r3, #0
.L1547:
	uxth	r2, r3
	mov	lr, r3, asl #1
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
	cmp	r3, #256
	strh	r2, [lr, ip]	@ movhi
	bne	.L1547
	b	.L1543
.L1546:
	cmp	r0, #4
	mov	r3, #0
	bne	.L1548
	ldr	r2, .L1567+8
	strh	r3, [r2, #4]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #12]	@ movhi
	strh	r3, [r2, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #8]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #10]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #14]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #16]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #18]!	@ movhi
.L1549:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L1549
	b	.L1543
.L1548:
	cmp	r0, #5
	bne	.L1550
	ldr	r2, .L1567
.L1551:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L1551
	ldr	r2, .L1567+12
.L1552:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L1552
	b	.L1543
.L1550:
	cmp	r0, #8
	bne	.L1553
	ldr	r2, .L1567
.L1554:
	strh	r3, [r3, r2]	@ movhi
	add	r3, r3, #2
	cmp	r3, #512
	bne	.L1554
	b	.L1543
.L1553:
	cmp	r0, #9
	bne	.L1543
	ldr	r2, .L1567+8
	movw	r1, #509
	strh	r3, [r2, #4]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #8]!	@ movhi
	mov	r3, #3
.L1555:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L1555
	b	.L1543
.L1540:
	ldrh	r2, [r3, ip]
	add	r3, r3, #2
	cmp	r3, #512
	mov	r1, r2, asl #1
	strh	r2, [r0, r1]	@ movhi
	bne	.L1540
	ldmfd	sp!, {r3, pc}
.L1568:
	.align	2
.L1567:
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3800
	.word	.LANCHOR0
	.word	.LANCHOR0+34
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
	ldr	r5, .L1600
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1570
	ldr	r0, .L1600+4
	ldr	r1, .L1600+8
	bl	printk
.L1570:
	ldr	r4, .L1600+12
	mov	r6, #0
	ldrh	r3, [r5, #30]
	ldrb	r1, [r5, #16]	@ zero_extendqisi2
	add	r9, r4, #688
	ldrh	r0, [r5, #14]
	strh	r3, [r4, #2]	@ movhi
	strb	r6, [r4, #589]
	bl	__aeabi_idiv
	ldr	r7, .L1600+16
	mov	r1, r6
	mov	r2, #8
	strh	r0, [r7]	@ movhi
	add	r0, r4, #668
	bl	ftl_memset
	add	r0, r7, #4
	mov	r1, r6
	mov	r2, #32
	bl	ftl_memset
	add	r7, r4, #680
.L1575:
	ldrb	r2, [r5, #4]	@ zero_extendqisi2
	add	r1, r9, r6, asl #3
	ldr	r8, .L1600+20
	mov	r3, #2
	ldr	r0, .L1600+24
	strb	r3, [r6, r7]
	mov	r3, #0
.L1571:
	cmp	r3, r2
	bcs	.L1598
	add	ip, r0, r3
	ldrb	lr, [ip, #1]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	cmp	lr, ip
	bne	.L1572
	add	r3, r3, #1
	b	.L1571
.L1598:
	ldrb	r3, [r4, #589]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r2, .L1600+28
	uxtb	r0, r6
	add	r2, r2, r3, asl #2
	str	r1, [r2, #-3356]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #589]
	strb	r0, [r3, #668]
	bl	zftl_flash_enter_slc_mode
.L1572:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1575
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldr	r3, .L1600
	cmp	r2, #2
	beq	.L1576
.L1583:
	ldrb	r3, [r5, #17]	@ zero_extendqisi2
	ldrb	r2, [r4, #589]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L1600+32
	strh	r3, [r2, #-12]	@ movhi
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1576:
	ldrh	ip, [r3, #18]
	mov	lr, #0
	ldrh	r2, [r4, #2]
	add	r3, r3, #4
	and	ip, ip, #65280
	ldrb	r1, [r4, #589]	@ zero_extendqisi2
	ldr	r6, .L1600+28
	mul	r2, r2, ip
	ldrb	ip, [r3, #13]	@ zero_extendqisi2
	ldrb	r7, [r3]	@ zero_extendqisi2
	mul	ip, ip, r2
	ldrb	r2, [r3, #23]	@ zero_extendqisi2
	str	r2, [sp]
	mov	r2, ip, asl #1
	str	r2, [sp, #4]
	mov	r2, lr
.L1581:
	add	r9, r8, r2, asl #3
	mov	r0, #0
.L1577:
	cmp	r0, r7
	bcs	.L1599
	add	r10, r3, r0
	ldrb	fp, [r10, #1]	@ zero_extendqisi2
	ldrb	r10, [r9, r0]	@ zero_extendqisi2
	cmp	fp, r10
	bne	.L1578
	add	r0, r0, #1
	b	.L1577
.L1599:
	ldmia	sp, {r9, lr}
	mov	r0, r1
	add	r1, r6, r1, asl #2
	cmp	r9, #0
	moveq	lr, ip
	str	lr, [r1, #-3356]
	add	r1, r0, #1
	add	r0, r4, r0
	mov	lr, #1
	uxtb	r1, r1
	strb	r2, [r0, #668]
.L1578:
	add	r2, r2, #1
	cmp	r2, #4
	bne	.L1581
	cmp	lr, #0
	strneb	r1, [r4, #589]
	b	.L1583
.L1601:
	.align	2
.L1600:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+1456
	.word	.LANCHOR0
	.word	.LANCHOR3-3360
	.word	.LANCHOR0+688
	.word	.LANCHOR2+4
	.word	.LANCHOR3
	.word	.LANCHOR3-3312
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
	ldr	r4, .L1604
	mov	r2, #512
	add	r0, r4, #2992
	add	r0, r0, #8
	bl	ftl_memset
	add	r3, r4, #2960
	ldr	r0, [r4, #3516]
	mov	r1, #255
	ldrh	r2, [r3]
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	ldmfd	sp!, {r4, lr}
	b	ftl_memset
.L1605:
	.align	2
.L1604:
	.word	.LANCHOR0
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
	ldr	r3, .L1619
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1607
	ldr	r0, .L1619+4
	mov	r2, #225
	ldr	r1, .L1619+8
	mov	r3, #0
	bl	printk
.L1607:
	ldr	r4, .L1619+12
	mov	r1, #255
	mov	r2, #512
	ldr	r0, .L1619+16
	add	r5, r4, #2960
	bl	ftl_memset
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	mov	r1, #255
	ldrh	r2, [r5]
	ldr	r0, [r4, #3516]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	lr, r5, #40
	mov	r0, #0
	mov	ip, r4
.L1608:
	ldrh	r1, [r5]
	uxth	r2, r0
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asl #1
	bge	.L1618
	ldr	r3, [ip, #3512]
	ldr	r3, [r3, r2, asl #2]
	cmn	r3, #1
	beq	.L1609
	uxtb	r3, r3
	mov	r3, r3, asl #1
	ldrh	r1, [lr, r3]
	strh	r2, [lr, r3]	@ movhi
	mov	r2, r2, asl #1
	ldr	r3, [ip, #3516]
	strh	r1, [r3, r2]	@ movhi
.L1609:
	add	r0, r0, #1
	b	.L1608
.L1618:
	ldmfd	sp!, {r3, r4, r5, pc}
.L1620:
	.align	2
.L1619:
	.word	.LANCHOR2
	.word	.LC123
	.word	.LANCHOR1+1476
	.word	.LANCHOR0
	.word	.LANCHOR0+3000
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
	ldr	r2, .L1626
	ldr	r3, .L1626+4
	mov	ip, #1
	ldrh	r1, [r2, #2]
	add	r2, r2, #668
	ldrb	r0, [r3, #13]	@ zero_extendqisi2
	smulbb	r1, r0, r1
	strh	r1, [r4, #4]	@ unaligned
	ldrb	r1, [r2, #-8]	@ zero_extendqisi2
	strb	r1, [r4, #7]
	ldr	r1, [r2, #-148]
	str	r1, [r4]	@ unaligned
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	ldrb	r0, [r2, #-79]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	ldrb	r3, [r3, #11]	@ zero_extendqisi2
	strb	r1, [r4, #8]
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L1622:
	uxtb	r1, r3
	cmp	r1, r0
	bcs	.L1625
	ldrb	lr, [r3, r2]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orr	r1, r1, ip, asl lr
	strb	r1, [r4, #10]
	b	.L1622
.L1625:
	ldmfd	sp!, {r4, pc}
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	zftl_read_flash_info, .-zftl_read_flash_info
	.align	2
	.global	gc_init
	.type	gc_init, %function
gc_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r8, #2312
	ldr	r4, .L1630
	mov	r5, #0
	ldr	r6, .L1630+4
	movw	r3, #2972
	add	r7, r4, r8
	mov	r1, r5
	mov	r2, #596
	strh	r5, [r4, r3]	@ movhi
	mov	r0, r7
	strb	r5, [r4, #2945]
	strb	r5, [r6, #-3322]
	str	r5, [r6, #-3320]
	bl	ftl_memset
	mvn	r3, #0
	strh	r3, [r4, r8]	@ movhi
	add	r3, r4, #2960
	movw	ip, #2296
	str	r5, [r4, #2320]
	ldrh	r2, [r3]
	mov	r3, r2, lsr #1
	strh	r3, [r7, #34]	@ movhi
	movw	r3, #2966
	mov	r0, r2, lsr #2
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2946
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2952
	strh	r5, [r4, r3]	@ movhi
	movw	r5, #2962
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	strh	r0, [r7, #32]	@ movhi
	smulbb	r1, r3, r2
	uxth	r1, r1
	sub	lr, r1, #32
	strh	lr, [r4, ip]	@ movhi
	movw	ip, #2294
	strh	r1, [r4, ip]	@ movhi
	movw	r1, #2300
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #2298
	strh	r0, [r4, r2]	@ movhi
	ldrh	r0, [r4, r5]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	str	r0, [r6, #-3316]
	ldrh	r0, [r4, r5]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r3, [r4, r5]
	str	r0, [r6, #-3312]
	ldrb	r0, [r4, #2964]	@ zero_extendqisi2
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	str	r0, [r4, #2940]
	ldrh	r0, [r4, r5]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	str	r0, [r4, #2932]
	ldrh	r0, [r4, r5]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	movw	r3, #2974
	ldrh	r3, [r4, r3]
	movw	r2, #2292
	mov	r3, r3, lsr #2
	strh	r3, [r4, r2]	@ movhi
	str	r0, [r6, #-3308]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1631:
	.align	2
.L1630:
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
	ldr	r4, .L1748
	ldr	r3, [r4, #2288]
	ldr	r3, [r3, #32]
	cmp	r3, #10240
	bls	.L1633
	bl	ftl_tmp_into_update
.L1633:
	ldr	r3, [r4, #580]
	ldr	r2, [r3, #568]
	ldr	r1, [r3, #12]
	add	r2, r2, #860160
	add	r2, r2, #3840
	cmp	r1, r2
	bhi	.L1634
	ldr	r2, .L1748
	ldr	r0, [r2, #2288]
	ldr	r2, [r3, #572]
	ldr	r0, [r0, #36]
	add	r2, r2, #32
	cmp	r0, r2
	movls	r7, #0
	movls	r10, r7
	bls	.L1635
.L1634:
	ldr	r2, [r4, #2288]
	mov	r8, #0
	ldr	ip, .L1748
	movw	r5, #65535
	mov	fp, r8
	mov	r10, r8
	ldr	r0, [r2, #36]
	mov	r9, r8
	str	r1, [r3, #568]
	mov	r6, r5
	str	r8, [sp, #32]
	str	r0, [r3, #572]
	ldrh	r7, [r2, #134]
	str	r8, [sp, #28]
	str	r8, [sp, #24]
	str	r8, [sp, #20]
.L1636:
	ldr	r3, .L1748
	add	r2, r3, #564
	ldrh	r2, [r2]
	cmp	r2, r7
	bls	.L1745
	ldr	r0, [ip, #568]
	mov	r1, r7, asl #2
	add	lr, r0, r1
	ldrb	r3, [lr, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	cmp	r2, #224
	beq	.L1637
	tst	r3, #8
	ldrneh	r3, [r0, r1]
	ldrne	r4, [r0, r1]
	ubfxne	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1639
.L1638:
	tst	r3, #24
	ldreqh	r3, [r0, r1]
	movweq	r4, #65535
	ldrne	r4, [r0, r1]
	movwne	r3, #65535
	ubfxeq	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1641
.L1639:
	ldr	r2, [sp, #32]
	add	r10, r10, #1
	cmp	r5, r3
	add	r2, r2, r3
	uxth	r10, r10
	str	r2, [sp, #32]
	ldrhi	r2, [ip, #580]
	movhi	r5, r3
	addhi	r2, r2, #584
	strhih	r7, [r2, #2]	@ movhi
.L1683:
	ldr	r2, [sp, #20]
	cmp	r2, r3
	movw	r2, #65535
	strcc	r3, [sp, #20]
	movcc	r8, r7
	cmp	r4, r2
	beq	.L1643
.L1641:
	ldr	r2, [sp, #24]
	cmp	r6, r4
	add	r2, r2, #1
	movhi	r6, r4
	uxth	r2, r2
	str	r2, [sp, #24]
	ldr	r2, [sp, #28]
	add	r2, r2, r4
	str	r2, [sp, #28]
	ldrhi	r2, [ip, #580]
	addhi	r2, r2, #584
	strhih	r7, [r2]	@ movhi
	cmp	r9, r4
	movcc	fp, r7
	movcc	r9, r4
.L1643:
	cmp	r4, #9
	cmphi	r3, #9
	bhi	.L1637
	ldr	r3, .L1748+4
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1637
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
	ldr	r0, [ip, #576]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1748+8
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1637:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1636
.L1745:
	ldr	r0, [r3, #580]
	mov	r2, #0
	mov	r1, #255
	str	r3, [sp, #36]
	add	r0, r0, #264
	strh	r2, [r0, #-142]	@ movhi
	mov	r2, #128
	bl	ftl_memset
	ldr	ip, .L1748+4
	movw	r2, #586
	ldr	ip, [ip]
	tst	ip, #1024
	ldr	r3, [sp, #36]
	ldr	r1, [r3, #580]
	ldr	r0, [r3, #568]
	ldrh	r1, [r1, r2]
	mov	r2, r1, asl #2
	add	r4, r0, r2
	beq	.L1646
	ldrh	r2, [r0, r2]
	ldr	ip, [r0, r1, asl #2]
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	ubfx	r2, r2, #0, #11
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r0, [r4, #3]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, [r3, #576]
	mov	r3, r1, asl #1
	ldrh	r3, [r0, r3]
	ldr	r0, .L1748+12
	str	r3, [sp, #12]
	ubfx	r3, ip, #11, #8
	bl	printk
.L1646:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #32
	ldr	r3, .L1748
	bne	.L1647
	ldr	r2, [r3, #2288]
	add	r2, r2, #688
	ldrh	r2, [r2]
	cmp	r2, #2
	ldrhi	r2, .L1748+16
	movhi	r1, #1
	strhi	r1, [r2, #-3304]
.L1647:
	ldrb	r2, [r4, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L1648
	and	r1, r2, #192
	cmp	r1, #64
	bne	.L1649
	ldr	r2, [r3, #580]
	movw	r3, #586
	mov	r1, #0
	ldrh	r0, [r2, r3]
	mov	r2, #1
	bl	gc_add_sblk
	ldr	r2, .L1748+20
	movw	r3, #570
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1648
.L1649:
	tst	r2, #224
	bne	.L1648
	ldr	r3, [r3, #580]
	movw	r7, #590
	movw	r2, #65535
	ldrh	r1, [r3, r7]
	cmp	r1, r2
	bne	.L1648
	movw	r4, #586
	ldrh	r0, [r3, r4]
	add	r3, r3, #588
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L1648
	bl	zftl_remove_free_node
	ldr	r3, .L1748
	ldr	r3, [r3, #580]
	ldrh	r2, [r3, r4]
	strh	r2, [r3, r7]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, r4]	@ movhi
.L1648:
	ldr	r4, .L1748
	ldr	r0, .L1748+4
	ldr	r3, [r4, #580]
	ldr	r0, [r0]
	add	r3, r3, #584
	tst	r0, #1024
	ldrh	r1, [r3]
	ldr	r3, [r4, #568]
	mov	r2, r1, asl #2
	add	r7, r3, r2
	beq	.L1650
	ldrb	r0, [r7, #2]	@ zero_extendqisi2
	ldrh	r2, [r3, r2]
	ubfx	r0, r0, #3, #2
	ldr	r3, [r3, r1, asl #2]
	str	r0, [sp]
	ubfx	r2, r2, #0, #11
	ldrb	r0, [r7, #2]	@ zero_extendqisi2
	ubfx	r3, r3, #11, #8
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r0, [r7, #3]	@ zero_extendqisi2
	str	r0, [sp, #8]
	mov	r0, r1, asl #1
	ldr	ip, [r4, #576]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #12]
	ldr	r0, .L1748+24
	bl	printk
.L1650:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1651
	and	r2, r3, #192
	ldr	r7, .L1748
	cmp	r2, #64
	bne	.L1652
	ldr	r3, [r7, #580]
	mov	r1, #0
	mov	r2, #1
	add	r3, r3, #584
	ldrh	r0, [r3]
	bl	gc_add_sblk
	ldr	r2, .L1748+20
	movw	r3, #570
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1651
.L1652:
	and	r3, r3, #248
	cmp	r3, #16
	bne	.L1651
	ldr	r3, [r7, #580]
	add	r2, r3, #588
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1651
	add	r2, r3, #584
	ldrh	r0, [r2]
	movw	r2, #590
	ldrh	r3, [r3, r2]
	cmp	r3, r0
	beq	.L1651
	bl	zftl_remove_free_node
	ldr	r3, [r7, #580]
	add	r2, r3, #588
	add	r3, r3, #584
	ldrh	r1, [r3]
	strh	r1, [r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L1651:
	ldr	r2, .L1748+4
	ldr	ip, .L1748
	ldr	r2, [r2]
	ldr	r3, [ip, #568]
	tst	r2, #1024
	beq	.L1653
	mov	r2, r8, asl #2
	add	r1, r3, r2
	ldrh	r2, [r3, r2]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ldr	r3, [r3, r8, asl #2]
	ubfx	r2, r2, #0, #11
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ubfx	r3, r3, #11, #8
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r1, [r1, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	mov	r1, r8, asl #1
	ldr	r0, [ip, #576]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1748+28
	str	r1, [sp, #12]
	mov	r1, r8
	bl	printk
.L1653:
	ldr	r2, .L1748+4
	ldr	ip, .L1748
	ldr	r2, [r2]
	ldr	r3, [ip, #568]
	tst	r2, #1024
	beq	.L1654
	mov	r2, fp, asl #2
	add	r1, r3, r2
	ldrh	r2, [r3, r2]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ldr	r3, [r3, fp, asl #2]
	ubfx	r2, r2, #0, #11
	ubfx	r0, r0, #3, #2
	str	r0, [sp]
	ldrb	r0, [r1, #2]	@ zero_extendqisi2
	ubfx	r3, r3, #11, #8
	mov	r0, r0, lsr #5
	str	r0, [sp, #4]
	ldrb	r1, [r1, #3]	@ zero_extendqisi2
	str	r1, [sp, #8]
	mov	r1, fp, asl #1
	ldr	r0, [ip, #576]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1748+32
	str	r1, [sp, #12]
	mov	r1, fp
	bl	printk
.L1654:
	ldr	r4, [r4, #2288]
	mov	r1, r10
	ldrh	r3, [sp, #20]
	ldr	r0, [sp, #32]
	strh	r9, [r4, #98]	@ movhi
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
	ldr	r4, .L1748+4
	ldr	r3, [r4]
	tst	r3, #1024
	beq	.L1655
	uxth	r0, r0
	mov	r1, r10
	str	r0, [sp]
	uxth	r3, r7
	ldr	r0, .L1748+36
	ldr	r2, [sp, #24]
	bl	printk
.L1655:
	ldr	r3, [r4]
	ldr	r4, .L1748+40
	tst	r3, #1024
	beq	.L1656
	str	r9, [sp]
	mov	r1, r5
	ldrh	r3, [r4, #-4]
	mov	r2, r6
	ldr	r0, .L1748+44
	str	r3, [sp, #4]
	ldrh	r3, [r4, #-2]
	str	r3, [sp, #8]
	ldr	r3, [sp, #20]
	bl	printk
.L1656:
	rsb	r3, r6, r9
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldrh	r3, [r4, #-2]
	cmp	r2, r3
	bgt	.L1657
	ldr	r2, .L1748+40
	ldr	r3, [sp, #20]
	ldrh	r2, [r2, #-4]
	rsb	r3, r5, r3
	cmp	r3, r2
	movle	r7, #0
	movle	r10, r7
	ble	.L1658
.L1657:
	ldr	ip, .L1748
	mov	r7, #0
	mov	r10, r7
	ldr	r3, [ip, #580]
	mov	r9, ip
	ldr	r4, [r3, #580]
	ldr	r3, [ip, #2288]
	uxth	r4, r4
	ldrh	fp, [r3, #134]
.L1659:
	ldr	r3, .L1748+48
	ldrh	r3, [r3]
	cmp	fp, r3
	bcs	.L1668
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r3
	ldr	r3, [r9, #568]
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
	bne	.L1661
	ubfx	r2, r2, #3, #2
	ands	r1, r2, #1
	beq	.L1662
	cmp	r0, #160
	b	.L1743
.L1662:
	cmp	r2, #2
.L1743:
	bne	.L1664
	ldr	r2, .L1748+52
	ldr	r0, [sp, #24]
	ldrh	r2, [r2]
	cmp	r0, r2
	ble	.L1665
	ldr	r2, [r3, r8]
	ubfx	r2, r2, #11, #8
	cmp	r2, r6
	bls	.L1666
	cmp	r1, #0
	beq	.L1665
	ldrh	r2, [r3, r8]
	ubfx	r2, r2, #0, #11
	cmp	r2, r5
	bgt	.L1665
.L1666:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1748+56
	mov	r1, #1
	add	r10, r10, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1748+4
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1665
	ldr	r0, [r9, #576]
	mov	r1, r4, asl r1
	ldr	r2, [r9, #568]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1748+20
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1748+60
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
	b	.L1744
.L1664:
	ldr	r0, .L1748+64
	ldr	r2, [sp, #20]
	ldrh	r0, [r0]
	rsb	r2, r5, r2
	cmp	r2, r0
	ble	.L1665
	ldrh	r2, [r3, r8]
	add	r0, r5, #8
	ubfx	r2, r2, #0, #11
	cmp	r2, r0
	ble	.L1667
	cmp	r1, #0
	beq	.L1665
	ldr	r2, [r3, r8]
	add	r1, r6, #4
	ubfx	r2, r2, #11, #8
	cmp	r2, r1
	bgt	.L1665
.L1667:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1748+56
	mov	r1, #1
	add	r7, r7, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1748+4
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1665
	ldr	r0, [r9, #576]
	mov	r1, r4, asl r1
	ldr	r2, [r9, #568]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1748+20
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1748+68
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
.L1744:
	mov	r1, r4
	mov	r2, r2, lsr #5
	mov	r3, lr
	bl	printk
.L1665:
	cmp	r10, #4
	cmpls	r7, #4
	bhi	.L1668
.L1661:
	add	fp, fp, #1
	uxth	fp, fp
	b	.L1659
.L1668:
	ldr	r3, .L1748
	ldr	r3, [r3, #580]
	str	r4, [r3, #580]
.L1658:
	ldr	r0, .L1748
	cmp	r6, #0
	ldrne	r3, [r0, #2288]
	movne	r4, r0
	ldrneh	r3, [r3, #134]
	beq	.L1671
.L1672:
	ldr	r2, .L1748
	add	r1, r2, #564
	ldrh	r1, [r1]
	cmp	r1, r3
	bls	.L1746
	ldr	ip, [r4, #568]
	mov	lr, r3, asl #2
	add	r8, ip, lr
	ldr	r2, [ip, r3, asl #2]
	ubfx	r1, r2, #11, #8
	cmp	r1, r6
	bcc	.L1673
	ldrb	r8, [r8, #2]	@ zero_extendqisi2
	tst	r8, #24
	rsbne	r1, r6, r1
	bfine	r2, r1, #11, #8
	strne	r2, [ip, lr]
.L1673:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1672
.L1746:
	ldr	r3, [r2, #2288]
	ldrh	r2, [r3, #72]
	add	r2, r6, r2
	strh	r2, [r3, #72]	@ movhi
	ldrh	r2, [r3, #98]
	cmp	r2, r6
	rsbhi	r2, r6, r2
	strhih	r2, [r3, #98]	@ movhi
.L1671:
	cmp	r5, #0
	beq	.L1677
	ldr	r3, [r0, #2288]
	ldr	r4, .L1748
	ldrh	r3, [r3, #134]
.L1678:
	ldr	r2, .L1748
	add	r1, r2, #564
	ldrh	r1, [r1]
	cmp	r1, r3
	bls	.L1747
	ldr	ip, [r4, #568]
	mov	r0, r3, asl #2
	add	lr, ip, r0
	ldrh	r2, [ip, r0]
	ubfx	r1, r2, #0, #11
	cmp	r1, r5
	blt	.L1679
	ldrb	lr, [lr, #2]	@ zero_extendqisi2
	and	lr, lr, #24
	cmp	lr, #16
	rsbne	r1, r5, r1
	bfine	r2, r1, #0, #11
	strneh	r2, [ip, r0]	@ movhi
.L1679:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1678
.L1747:
	ldr	r3, [r2, #2288]
	ldrh	r2, [r3, #74]
	add	r2, r5, r2
	strh	r2, [r3, #74]	@ movhi
	ldrh	r2, [r3, #96]
	cmp	r2, r5
	rsbhi	r2, r5, r2
	strhih	r2, [r3, #96]	@ movhi
.L1677:
	ldr	r0, .L1748+72
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r4, .L1748
	cmp	r0, r2
	beq	.L1635
	ldr	r2, [r4, #576]
	mov	r3, r0, asl #1
	add	r4, r4, #2960
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	cmp	r2, r3
	bhi	.L1635
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	add	r10, r10, #1
.L1635:
	add	r0, r7, r10
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1749:
	.align	2
.L1748:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
	.word	.LC125
	.word	.LANCHOR3
	.word	.LANCHOR0+2312
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LC129
	.word	.LANCHOR3-3296
	.word	.LC130
	.word	.LANCHOR0+564
	.word	.LANCHOR3-3298
	.word	.LANCHOR0+2882
	.word	.LC131
	.word	.LANCHOR3-3300
	.word	.LC132
	.word	.LANCHOR0+2956
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
	mov	r2, #6
	ldr	r6, .L1781
	mov	r1, #0
	.pad #20
	sub	sp, sp, #20
	mov	r9, #0
	add	r8, r6, #564
	add	r7, r6, #2272
	ldr	r0, [r6, #524]
	mov	r4, r6
	ldrh	r3, [r8]
	ldr	fp, .L1781+4
	mul	r2, r2, r3
	bl	ftl_memset
	add	r3, r6, #2992
	mov	r2, #16
	strh	r9, [r7]	@ movhi
	strh	r2, [r3]	@ movhi
	movw	r3, #2274
	strh	r9, [r6, r3]	@ movhi
	movw	r3, #2276
	strh	r9, [r6, r3]	@ movhi
	movw	r3, #2278
	strh	r9, [r6, r3]	@ movhi
	movw	r3, #2282
	strh	r9, [r6, r3]	@ movhi
	movw	r2, #2962
	movw	r3, #2280
	ldrh	r1, [r6, r2]
	strh	r9, [r6, r3]	@ movhi
	mov	r0, #32768
	ldrb	r3, [r6, #2964]	@ zero_extendqisi2
	str	r9, [r6, #2980]
	str	r9, [r6, #2984]
	mul	r1, r1, r3
	str	r9, [r6, #2988]
	str	r9, [r6, #2948]
	str	r9, [r6, #2968]
	str	r9, [r6, #2956]
	bl	__aeabi_idiv
	uxth	r3, r0
	str	r3, [sp]
	ldr	r3, [r6, #2288]
	str	r7, [sp, #8]
	mov	r7, r6
	str	r8, [sp, #4]
	ldrh	r5, [r3, #134]
	strh	r9, [r3, #146]	@ movhi
.L1751:
	ldr	r3, [sp, #4]
	sxth	r2, r5
	ldr	r1, .L1781
	ldrh	r3, [r3]
	cmp	r2, r3
	bge	.L1779
	ldr	r3, [r7, #568]
	add	r8, r3, r2, asl #2
	ldrb	r1, [r8, #3]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp]
	beq	.L1752
	ldrb	r6, [r7, #2964]	@ zero_extendqisi2
	mov	ip, #0
	ldrh	r9, [fp]
	mov	r1, ip
	ldr	lr, [r7, #2288]
.L1753:
	cmp	ip, r6
	bge	.L1780
	ldrb	r0, [r8, #3]	@ zero_extendqisi2
	mov	r0, r0, asr ip
	add	ip, ip, #1
	tst	r0, #1
	addeq	r1, r9, r1
	ldrneh	r0, [lr, #146]
	uxtheq	r1, r1
	addne	r0, r0, #1
	strneh	r0, [lr, #146]	@ movhi
	b	.L1753
.L1780:
	cmp	r1, #0
	beq	.L1757
	sxth	r1, r1
	mov	r0, #32768
	str	r2, [sp, #12]
	bl	__aeabi_idiv
	add	r0, r0, #1
	ldr	r2, [sp, #12]
	uxth	r1, r0
	b	.L1752
.L1757:
	ldrb	r0, [r8, #2]	@ zero_extendqisi2
	mvn	lr, #0
	orr	r0, r0, #224
	strb	r0, [r8, #2]
	ldr	ip, [r4, #576]
	mov	r0, r2, asl #1
	strh	lr, [ip, r0]	@ movhi
.L1752:
	mov	r10, r2, asl #1
	ldr	lr, [r4, #524]
	add	r0, r10, r2
	mov	r0, r0, asl #1
	add	r6, lr, r0
	strh	r1, [r6, #4]	@ movhi
	mvn	r1, #0
	strh	r1, [r6, #2]	@ movhi
	strh	r1, [lr, r0]	@ movhi
	ldrb	r1, [r8, #2]	@ zero_extendqisi2
	and	r6, r1, #224
	cmp	r6, #32
	cmpne	r6, #224
	moveq	r9, #1
	movne	r9, #0
	beq	.L1759
	ldr	r0, [r7, #580]
	ldrh	lr, [r0, #16]
	cmp	r2, lr
	beq	.L1759
	ldrh	lr, [r0, #48]
	cmp	r2, lr
	beq	.L1759
	ldrh	r0, [r0, #80]
	cmp	r2, r0
	beq	.L1759
	cmp	r6, #64
	uxtheq	r6, r5
	ldreq	r0, .L1781+8
	ldreq	r2, .L1781+12
	moveq	r1, r6
	beq	.L1777
.L1761:
	cmp	r6, #96
	uxtheq	r6, r5
	ldreq	r0, .L1781+16
	moveq	r1, r6
	subeq	r2, r0, #668
	beq	.L1777
.L1762:
	cmp	r6, #160
	bne	.L1763
	uxth	r6, r5
	ldr	r0, .L1781+20
	ldr	r2, .L1781+24
	mov	r1, r6
.L1777:
	bl	_insert_data_list
	ldr	r3, [r7, #576]
	ldrh	r3, [r3, r10]
	cmp	r3, #7
	movls	r0, r6
	movls	r1, #1
	movls	r2, r9
	bhi	.L1759
	b	.L1775
.L1763:
	cmp	r6, #0
	bne	.L1759
	ldr	r0, [r7, #576]
	uxth	r9, r5
	ldrh	ip, [r0, r10]
	cmp	ip, #0
	beq	.L1764
	mov	r1, r2
	ldr	r0, .L1781+28
	mov	r2, ip
	bl	printk
	ldrb	r2, [r8, #2]	@ zero_extendqisi2
	mov	r0, r9
	tst	r2, #16
	movne	r1, #5
	moveq	r1, #2
	bfi	r2, r1, #5, #3
	mov	r1, #1
	strb	r2, [r8, #2]
	mov	r2, r6
.L1775:
	bl	gc_add_sblk
	b	.L1759
.L1764:
	ands	r1, r1, #24
	ldreq	r0, .L1781+32
	moveq	r1, r9
	subeq	r2, r0, #708
	beq	.L1773
.L1766:
	cmp	r1, #16
	ldrne	r0, .L1781+36
	moveq	r1, r9
	ldreq	r0, .L1781+40
	movne	r1, r9
	ldreq	r2, .L1781+44
	subne	r2, r0, #712
.L1773:
	bl	_insert_free_list
.L1759:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1751
.L1779:
	ldr	r2, [sp, #8]
	ldr	r3, [r1, #2288]
	ldrh	r2, [r2]
	strh	r2, [r3, #114]	@ movhi
	movw	r2, #2274
	ldrh	r2, [r1, r2]
	strh	r2, [r3, #118]	@ movhi
	movw	r2, #2276
	ldrh	r2, [r1, r2]
	strh	r2, [r3, #116]	@ movhi
	movw	r2, #2278
	ldrh	r2, [r1, r2]
	strh	r2, [r3, #122]	@ movhi
	movw	r2, #2282
	ldrh	r2, [r1, r2]
	strh	r2, [r3, #120]	@ movhi
	movw	r2, #2280
	ldrh	r2, [r1, r2]
	strh	r2, [r3, #124]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1782:
	.align	2
.L1781:
	.word	.LANCHOR0
	.word	.LANCHOR0+2962
	.word	.LANCHOR0+2968
	.word	.LANCHOR0+2278
	.word	.LANCHOR0+2948
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2282
	.word	.LC133
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+2988
	.word	.LANCHOR0+2984
	.word	.LANCHOR0+2274
	.fnend
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	pm_free_sblk
	.type	pm_free_sblk, %function
pm_free_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 280
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1814
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #292
	sub	sp, sp, #292
	ldr	r2, [r3]
	ldr	lr, .L1814+4
	str	r3, [sp, #8]
	str	r2, [sp, #284]
	ldrh	r2, [lr]
	mov	r5, lr
	cmp	r2, #128
	bls	.L1784
	ldr	r1, .L1814+8
	mov	r2, #84
	ldr	r0, .L1814+12
	bl	printk
	bl	dump_stack
.L1784:
	ldr	r4, .L1814+16
	movw	lr, #65535
	ldrh	ip, [r5]
	ldr	r3, [r4, #2288]
	add	r2, r3, #688
	add	r3, r3, #412
	add	r3, r3, #2
	ldrh	r0, [r2]
	mov	r2, #0
	mov	r1, r2
.L1787:
	ldrh	r6, [r3, #2]!
	uxth	r5, r2
	cmp	r6, lr
	addne	r1, r1, #1
	uxthne	r1, r1
	cmp	r1, r0
	bcs	.L1786
	cmp	r1, ip
	bcs	.L1786
	add	r2, r2, #1
	cmp	r2, #128
	bne	.L1787
	mov	r5, r2
.L1786:
	add	r5, r5, #1
	add	r0, sp, #28
	mov	r1, #0
	mov	r2, #256
	uxth	r5, r5
	mvn	r8, #0
	cmp	r5, #128
	mov	r6, #0
	movcs	r5, #128
	bl	ftl_memset
	ldr	r7, [r4, #2288]
	movw	r3, #698
	ldrb	ip, [r4, #2930]	@ zero_extendqisi2
	add	r10, r7, #700
	ldrh	fp, [r7, r3]
	ldr	r3, .L1814+20
	ldrh	r9, [r3]
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r9, r3
	mvn	r8, r8, asl r3
.L1788:
	uxth	r3, r6
	cmp	r3, fp
	bcs	.L1811
	ldr	r0, [r10, #4]!
	mov	r1, ip
	str	ip, [sp, #12]
	and	r0, r8, r0, lsr r9
	bl	__aeabi_uidiv
	add	r3, r7, #412
	add	r3, r3, #2
	mov	r1, #0
	ldr	ip, [sp, #12]
	uxth	r0, r0
	add	r2, sp, #28
.L1789:
	uxth	lr, r1
	cmp	lr, r5
	bcs	.L1812
	ldrh	lr, [r3, #2]!
	add	r1, r1, #1
	add	r2, r2, #2
	cmp	lr, r0
	ldreqh	lr, [r2, #-2]
	addeq	lr, lr, #1
	streqh	lr, [r2, #-2]	@ movhi
	b	.L1789
.L1812:
	add	r6, r6, #1
	b	.L1788
.L1811:
	ldr	r9, .L1814+24
	mov	r6, #0
	ldrb	r7, [r4, #2964]	@ zero_extendqisi2
	add	r8, sp, #28
	movw	fp, #65535
	mov	r10, r6
	ldrh	r3, [r9]
	str	r9, [sp, #12]
	smulbb	r7, r7, r3
	uxth	r7, r7
.L1793:
	uxth	r9, r6
	cmp	r9, r5
	bcs	.L1813
	add	r3, r6, #208
	ldr	r2, [r4, #2288]
	mov	r3, r3, asl #1
	add	r1, r2, #692
	str	r2, [sp, #16]
	ldrh	r3, [r2, r3]
	ldrh	r0, [r1]
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	str	r3, [sp, #20]
	bl	__aeabi_idiv
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #16]
	cmp	r0, r3
	ldreq	r1, .L1814+16
	ldreqb	r0, [r1, #2964]	@ zero_extendqisi2
	ldreq	r1, [sp, #12]
	ldreqh	r1, [r1]
	smulbbeq	r1, r0, r1
	movw	r0, #65535
	streqh	r1, [r8]	@ movhi
	ldrh	lr, [r8]
	cmp	lr, #0
	cmpne	r7, lr
	movhi	r1, #1
	movls	r1, #0
	cmp	r1, #0
	ldrh	r1, [r2, #74]
	movne	r10, r9
	movne	r7, lr
	cmp	r3, r0
	cmpne	r1, #2
	bls	.L1796
	ldr	r0, [r4, #568]
	mov	r1, r3, asl #2
	ldrh	r2, [r2, #92]
	ldrh	r1, [r0, r1]
	add	r2, r2, #4
	ubfx	r1, r1, #0, #11
	cmp	r1, r2
	bgt	.L1796
	str	r1, [sp]
	mov	r2, lr
	ldr	r0, .L1814+28
	mov	r1, r6
	bl	printk
	mov	fp, r9
.L1796:
	ldrh	r2, [r8]
	cmp	r2, #0
	bne	.L1797
	add	r9, r6, #208
	ldr	r1, [r4, #2288]
	movw	r0, #65535
	mov	r3, r9, asl #1
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L1797
	ldr	r0, .L1814+32
	ldr	r0, [r0]
	tst	r0, #4096
	beq	.L1798
	add	r1, r1, #688
	ldr	r0, .L1814+36
	ldrh	r1, [r1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1798:
	ldr	r3, [r4, #2288]
	mov	r9, r9, asl #1
	ldrh	r0, [r3, r9]
	bl	ftl_free_sblk
	ldr	r3, [r4, #2288]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	sub	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L1797:
	add	r6, r6, #1
	add	r8, r8, #2
	b	.L1793
.L1813:
	ldr	r3, [sp, #8]
	movw	r0, #65535
	ldr	r2, [sp, #284]
	cmp	fp, r0
	ldr	r3, [r3]
	movne	r0, fp
	moveq	r0, r10
	cmp	r2, r3
	beq	.L1801
	bl	__stack_chk_fail
.L1801:
	add	sp, sp, #292
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1815:
	.align	2
.L1814:
	.word	__stack_chk_guard
	.word	.LANCHOR3-3296
	.word	.LANCHOR1+1496
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR0+2928
	.word	.LANCHOR0+2960
	.word	.LC134
	.word	.LANCHOR2
	.word	.LC135
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
	ldr	r3, .L1823
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1818
	ldr	r0, .L1823+4
	mov	r1, #105
	ldr	r2, .L1823+8
	bl	printk
.L1818:
	ldr	r4, .L1823+12
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #664]
	bl	ftl_memset
	ldr	r3, [r4, #664]
	ldr	r2, .L1823+16
	ldr	r1, .L1823+20
	str	r2, [r3]
	mov	r3, #2032
	ldr	r0, [r4, #664]
	mov	r2, #32
	add	r0, r0, #80
	str	r3, [r0, #-72]
	mov	r3, #1
	strh	r3, [r0, #-64]	@ movhi
	bl	ftl_memcpy
	ldr	r0, [r4, #664]
	ldr	r1, .L1823+24
	mov	r2, #32
	add	r0, r0, #48
	ldmfd	sp!, {r4, lr}
	b	ftl_memcpy
.L1824:
	.align	2
.L1823:
	.word	.LANCHOR2
	.word	.LC136
	.word	.LANCHOR1+1512
	.word	.LANCHOR0
	.word	1398362953
	.word	.LANCHOR0+591
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
.L1826:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1826
.L1828:
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
	ldr	r3, .L1831
	ldr	r0, [r3]
	ldr	r3, .L1831+4
	ldr	r3, [r3, #-3288]
	rsb	r0, r3, r0
	b	jiffies_to_msecs
.L1832:
	.align	2
.L1831:
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
	ldr	r3, .L1835
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #12]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1836:
	.align	2
.L1835:
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
	ldr	r3, .L1839
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #16]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1840:
	.align	2
.L1839:
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
	ldr	r3, .L1843
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #4]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1844:
	.align	2
.L1843:
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
	ldr	r3, .L1847
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #8]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1848:
	.align	2
.L1847:
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
	ldr	ip, .L1853
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #-3276]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1852
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1852:
	b	ftl_memcpy
.L1854:
	.align	2
.L1853:
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
	ldr	r0, .L1858
	bl	printk
	ldr	r3, .L1858+4
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #40]
	ldmfd	sp!, {r4, lr}
	bx	r3	@ indirect register sibling call
.L1859:
	.align	2
.L1858:
	.word	.LC137
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
	ldr	r3, .L1861
	mov	r0, #0
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #32]
	bx	r3	@ indirect register sibling call
.L1862:
	.align	2
.L1861:
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
	ldr	r3, .L1864
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #44]
	bx	r3	@ indirect register sibling call
.L1865:
	.align	2
.L1864:
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
	ldr	r3, .L1867
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #48]
	bx	r3	@ indirect register sibling call
.L1868:
	.align	2
.L1867:
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
	ldr	r3, .L1870
	mov	r0, #0
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #36]
	bx	r3
.L1871:
	.align	2
.L1870:
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
	ldr	r3, .L1873
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #60]
	bx	r3
.L1874:
	.align	2
.L1873:
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
	ldr	r3, .L1876
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #64]
	bx	r3
.L1877:
	.align	2
.L1876:
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
	ldr	ip, .L1879
	ldr	ip, [ip, #-3280]
	ldr	ip, [ip, #20]
	bx	ip
.L1880:
	.align	2
.L1879:
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
	ldr	r3, .L1882
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #28]
	bx	r3
.L1883:
	.align	2
.L1882:
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
	ldr	r3, .L1885
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #52]
	bx	r3
.L1886:
	.align	2
.L1885:
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
	ldr	r3, .L1888
	ldr	r3, [r3, #-3280]
	ldr	r3, [r3, #56]
	bx	r3	@ indirect register sibling call
.L1889:
	.align	2
.L1888:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	mov	r9, r0
	mov	r6, r1
	mov	r7, r2
	mov	r8, r3
	mov	r4, r5
.L1891:
	cmp	r4, r8
	beq	.L1900
	cmp	r5, #0
	bne	.L1892
	ldr	r0, .L1901
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1892:
	cmp	r7, #4
	ldreq	r0, .L1901+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1899
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1901+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1901+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1899:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1896
	ldr	r0, .L1901+8
	mov	r5, #0
	ldr	r1, .L1901+12
	bl	printk
.L1896:
	add	r4, r4, #1
	b	.L1891
.L1900:
	ldr	r0, .L1901+8
	ldr	r1, .L1901+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1902:
	.align	2
.L1901:
	.word	.LC138
	.word	.LC139
	.word	.LC1
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
	ldr	r8, .L2014
	.pad #36
	sub	sp, sp, #36
	mvn	r7, #81
	mvn	r6, #80
	mov	r5, r0
	ldr	r4, [r8, #664]
	add	r3, r4, #128
	str	r3, [sp]
	strb	r0, [r4, #112]
	mvn	r3, #83
	strb	r7, [r4, #130]
	strb	r3, [r4, #128]
	mvn	r3, #82
	strb	r6, [r4, #131]
	strb	r3, [r4, #129]
	bne	.L1904
	mvn	r3, #88
	strb	r3, [r4, #128]
	ldr	r3, .L2014+4
	mvn	r1, #8
	mov	r6, #7
	strb	r1, [r3, #413]
	b	.L1965
.L1904:
	cmp	r0, #3
	bne	.L1906
	add	r1, r4, #127
	mov	r3, #176
.L1907:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1907
	b	.L2008
.L1906:
	cmp	r0, #4
	bne	.L1908
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
.L2008:
	mov	r6, #8
	mov	fp, r6
	b	.L1905
.L1908:
	cmp	r0, #5
	bne	.L1909
	mov	r3, #56
	mov	r6, #8
	strb	r3, [r4, #128]
	mov	r3, #57
	strb	r3, [r4, #129]
	mov	r3, #58
	strb	r3, [r4, #130]
	mov	r3, #59
	strb	r3, [r4, #131]
	b	.L1965
.L1909:
	cmp	r0, #6
	bne	.L1910
	mov	r3, #14
	mov	r6, #12
	strb	r3, [r4, #128]
	mov	r3, #15
	strb	r3, [r4, #129]
	mov	r3, #16
	strb	r3, [r4, #130]
	mov	r3, #17
	strb	r3, [r4, #131]
	b	.L1965
.L1910:
	cmp	r0, #7
	bne	.L1911
	add	r1, r4, #127
	mov	r3, #176
.L1912:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1912
	mvn	r3, #43
	mov	r6, #12
	strb	r3, [r4, #136]
	mov	fp, #10
	mvn	r3, #42
	strb	r3, [r4, #137]
	b	.L1905
.L1911:
	cmp	r0, #8
	mov	r6, #7
	bne	.L1965
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
	b	.L1905
.L1965:
	mov	fp, #4
.L1905:
	sub	r3, r5, #1
	cmp	r3, #1
	bhi	.L2004
	ldr	r1, .L2014+8
	mov	r9, #0
.L1913:
	ldrb	ip, [r8, #589]	@ zero_extendqisi2
	uxtb	r3, r9
	ldr	r0, .L2014
	cmp	ip, r3
	bls	.L1920
	add	r3, r0, r3
	mov	r5, #160
	ldr	r0, [r0, #528]
	sub	r10, fp, #1
	ldrb	r7, [r3, #668]	@ zero_extendqisi2
	add	r2, r4, #127
	ldr	ip, [sp]
	mla	r5, r5, r7, r4
	add	r7, r0, r7, asl #8
	uxtab	r10, ip, r10
	mov	ip, #55
	add	r5, r5, #144
	sub	r3, r5, #1
.L1915:
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
	bne	.L1915
	mov	ip, r5
	mov	r0, #0
.L1916:
	add	r7, r1, r0
	mov	r3, #1
.L1917:
	ldrb	lr, [r7, r3, asl #2]	@ zero_extendqisi2
	ldrb	r10, [ip]	@ zero_extendqisi2
	add	lr, lr, r10
	strb	lr, [ip, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1917
	add	r0, r0, #1
	add	ip, ip, #1
	cmp	r0, #4
	bne	.L1916
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r5, #16]
	strb	r3, [r5, #24]
	strb	r3, [r5, #32]
	strb	r3, [r5, #40]
	strb	r3, [r5, #48]
	strb	r3, [r5, #41]
	strb	r3, [r5, #49]
	b	.L1913
.L2004:
	sub	r3, r5, #3
	cmp	r3, #5
	bhi	.L1920
	smulbb	r2, fp, r6
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #28]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L2012:
	str	r3, [sp, #12]
	ldr	r3, .L2014
	ldrb	r2, [r3, #589]	@ zero_extendqisi2
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	cmp	r2, r3
	bhi	.L1964
.L1920:
	strb	fp, [r4, #113]
	strb	r6, [r4, #114]
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1964:
	ldr	r2, .L2014
	mov	r10, #160
	mov	r9, #255
	add	r3, r2, r3
	ldrb	r3, [r3, #668]	@ zero_extendqisi2
	mov	r0, r3
	str	r3, [sp]
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [sp]
	mla	r10, r10, r3, r4
	add	r3, r10, #144
	str	r3, [sp, #24]
	ldr	r3, .L2014
	ldr	r3, [r3, #528]
	str	r3, [sp, #4]
	ldr	r3, [sp]
	mov	r8, r3, asl #8
	ldr	r3, [sp, #4]
	add	r7, r3, r8
	str	r9, [r7, #2056]
	bl	nandc_wait_flash_ready
	cmp	r5, #8
	bne	.L1922
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
	b	.L1923
.L1922:
	cmp	r5, #4
	mov	r3, #54
	str	r3, [r7, #2056]
	moveq	r3, #64
	streq	r9, [r7, #2052]
	streq	r3, [r7, #2048]
	moveq	r3, #204
	beq	.L2009
.L1924:
	sub	r3, r5, #5
	cmp	r3, #1
	ldrlsb	r3, [r4, #128]	@ zero_extendqisi2
	strls	r3, [r7, #2052]
	movls	r3, #82
	bls	.L2010
	cmp	r5, #7
	bne	.L1925
	mov	r3, #174
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #176
.L2009:
	str	r3, [r7, #2052]
	mov	r3, #77
.L2010:
	str	r3, [r7, #2048]
.L1925:
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
.L1923:
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
	bne	.L1929
	cmp	r5, #7
	movne	ip, #2
	moveq	ip, #32
.L1929:
	ldr	r3, .L2014+12
	ldmia	sp, {r1, r2}
	ldr	r3, [r3, #-3272]
	add	r1, r2, r1, asl #8
	mov	r0, r3
.L1930:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1930
	cmp	r10, #0
	beq	.L1931
	mov	r2, #0
.L1933:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L1932
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1932
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1933
	b	.L1934
.L1932:
	cmp	r0, #7
	bne	.L1935
.L1934:
	ldr	r0, .L2014+16
	mov	r1, #0
	bl	printk
.L1936:
	b	.L1936
.L1931:
	cmp	r5, #7
	bne	.L1937
	mov	r2, r10
.L1939:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L1938
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1938
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1939
	b	.L1940
.L1938:
	cmp	r0, #7
	bne	.L1935
.L1940:
	ldr	r0, .L2014+16
	mov	r1, #0
	bl	printk
.L1941:
	b	.L1941
.L1937:
	cmp	r5, #6
	bne	.L1935
	sub	r2, r3, #1
	add	r3, r3, #7
.L1942:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L1935
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L1935
	cmp	r2, r3
	bne	.L1942
	ldr	r0, .L2014+16
	mov	r1, #0
	bl	printk
.L1944:
	b	.L1944
.L1935:
	ldr	r3, .L2014+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-3272]
	add	r0, r2, r0
	mov	r3, r2
.L1945:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L1945
.L2013:
	ldr	r3, .L2014+12
	mov	r0, #8
	ldr	r1, [sp, #8]
	ldr	r3, [r3, #-3272]
	add	r1, r3, r1
	str	r1, [sp, #20]
.L1948:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1947:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1947
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1948
.L1949:
	mov	ip, #0
	mov	r0, ip
.L1952:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r8, #16
	mov	r7, lr
.L1950:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	subs	r8, r8, #1
	add	lr, lr, r9
	bne	.L1950
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1952
	ldr	r1, [sp, #20]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1949
	ldr	r3, .L2014+12
	ldr	r1, [r3, #-3272]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1954:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1954
	cmp	r3, #7
	ble	.L1955
	ldr	r0, .L2014+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2014+16
	mov	r1, #0
	bl	printk
.L1956:
	b	.L1956
.L1955:
	cmp	r5, #6
	moveq	r0, #4
	beq	.L1957
	cmp	r5, #7
	moveq	r0, #10
	beq	.L1957
	cmp	r10, #0
	moveq	r0, #8
	movne	r0, #5
.L1957:
	sub	r9, fp, #1
	ldr	r1, [sp, #24]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1958:
	mov	r7, r1
	mov	r3, r2
.L1959:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, fp
	bcc	.L1959
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r6
	add	r1, r1, r0
	blt	.L1958
	ldmia	sp, {r2, r3}
	mov	r8, #255
	add	r7, r3, r2, asl #8
	str	r8, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L1961
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
	b	.L2011
.L1961:
	cmp	r10, #0
	movne	r3, #190
	moveq	r3, #56
.L2011:
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2012
.L2015:
	.align	2
.L2014:
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
	ldr	r3, .L2018
	ldrb	r0, [r3, #23]	@ zero_extendqisi2
	sub	r3, r0, #1
	cmp	r3, #7
	bxhi	lr
	b	hynix_get_read_retry_default
.L2019:
	.align	2
.L2018:
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
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	ldr	r5, .L2069
	ldr	r7, .L2069+4
	ldr	r0, [r5, #528]
	mov	r4, r5
	bl	wait_for_nandc_xfer_completed
	ldrb	r3, [r5, #516]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r6, [r5, #528]
	cmp	r3, #9
	strb	r1, [r7, #-3268]
	bne	.L2021
	ldr	r3, [r6, #16]
	str	r3, [sp]
	ldr	r5, [r6, #48]
	ubfx	r5, r5, #1, #1
	cmp	r5, r1
	moveq	r7, r4
	beq	.L2023
.L2022:
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #6
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2025
	ldr	r3, [r6]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2024
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2024
	ldr	r1, [sp, #4]
	ldr	r0, .L2069+8
	ubfx	r1, r1, #17, #1
	bl	printk
	b	.L2025
.L2024:
	ldr	r3, [sp]
	add	r1, r1, #1
	ubfx	r3, r3, #22, #6
	cmp	r1, r3, asl #12
	bne	.L2022
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L2069+12
	ubfx	r3, r3, #22, #6
	bl	printk
	mov	r3, #64
	ldr	r0, .L2069+16
	mov	r2, #4
	ldr	r1, [r4, #528]
	bl	rknand_print_hex
	ldr	r3, [sp, #4]
	tst	r3, #8192
	mov	r3, #1
	strb	r3, [r7, #-3268]
	bne	.L2025
	ldr	r3, .L2069+20
	ldr	r0, .L2069+24
	ldr	r3, [r3, #4]
	blx	r3
.L2025:
	ldr	r3, [r4, #648]
	ldr	r5, .L2069
	cmp	r3, #0
	beq	.L2029
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #640]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #644]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2068
.L2031:
	ldr	r3, [r4, #528]
	add	r5, r5, #1
	ldr	r3, [r3, #16]
	str	r3, [sp]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2023
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #64]
	ldr	r0, .L2069+28
	ubfx	r3, r3, #16, #6
	bl	printk
	ldr	r0, .L2069+16
	ldr	r1, [r7, #528]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2023:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2031
	ldr	r3, [r4, #648]
	ldr	r5, .L2069
	cmp	r3, #0
	beq	.L2029
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #640]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #644]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2067
.L2021:
	ldr	r3, [r6, #8]
	str	r3, [sp]
	ldr	r5, [r6, #16]
	ubfx	r5, r5, #1, #1
	cmp	r5, #0
	moveq	r7, r4
	beq	.L2034
	mov	r5, r1
.L2033:
	ldr	r2, [r6, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2036
	ldr	r3, [r4, #528]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2035
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2035
	ldr	r1, [sp, #4]
	ldr	r0, .L2069+32
	bl	printk
	b	.L2036
.L2035:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2033
	ldr	r2, [r6, #28]
	mov	r1, r5
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L2069+12
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L2069+16
	ldr	r1, [r4, #528]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
	b	.L2033
.L2036:
	ldr	r3, [r4, #648]
	ldr	r5, .L2069
	cmp	r3, #0
	beq	.L2029
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #640]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #644]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2068:
	mov	r2, #0
	b	.L2066
.L2040:
	ldr	r3, [r4, #528]
	add	r5, r5, #1
	ldr	r3, [r3, #8]
	str	r3, [sp]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2034
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #28]
	ldr	r0, .L2069+28
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L2069+16
	ldr	r1, [r7, #528]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2034:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2040
	ldr	r3, [r4, #648]
	ldr	r5, .L2069
	cmp	r3, #0
	beq	.L2029
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #640]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #644]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2067:
	mov	r2, #1
.L2066:
	bl	rknand_dma_unmap_single
.L2029:
	mov	r3, #0
	str	r3, [r4, #648]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2070:
	.align	2
.L2069:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC143
	.word	.LC144
	.word	.LC145
	.word	arm_delay_ops
	.word	644245000
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
	mov	r0, r1
	ldr	r7, [sp, #32]
	mov	r4, r1
	mov	r8, r2
	mov	r1, r2
	mov	r6, r3
	mov	r2, r3
	mov	r3, r7
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	cmp	r4, #0
	movne	r0, #0
	bne	.L2101
	ldr	r5, .L2114
	ldrb	r3, [r5, #516]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2073
	ldr	lr, [r5, #528]
	mov	r1, r8, lsr #2
	mov	ip, #1
	mov	r3, r4
	mov	r0, r4
.L2074:
	cmp	r3, r1
	bcs	.L2111
	add	r2, r3, #84
	ldr	r2, [lr, r2, asl #2]
	str	r2, [sp, #4]
	ldr	r4, [sp, #4]
	ldr	r2, [sp, #4]
	ubfx	r2, r2, #26, #1
	and	r2, r2, r4, lsr #10
	and	ip, ip, r2
	ldr	r2, [sp, #4]
	tst	r2, #4
	bne	.L2093
	ldr	r2, [sp, #4]
	tst	r2, #262144
	bne	.L2093
	ldr	r4, [sp, #4]
	ldr	r2, [sp, #4]
	ubfx	r4, r4, #3, #7
	ubfx	r2, r2, #19, #7
	cmp	r4, r2
	ldr	r2, [sp, #4]
	ubfxgt	r2, r2, #3, #7
	ubfxle	r2, r2, #19, #7
	cmp	r0, r2
	movcc	r0, r2
	b	.L2075
.L2093:
	mvn	r0, #0
.L2075:
	add	r3, r3, #1
	b	.L2074
.L2111:
	ldr	r3, [r5, #528]
	cmp	ip, #0
	movne	r0, #512
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2080
	mov	r1, r4
	ldr	r0, .L2114+4
	bl	printk
	ldr	r3, [r5, #528]
	mvn	r0, #0
	orr	r4, r4, #131072
	str	r4, [r3]
.L2080:
	tst	r4, #8192
	beq	.L2081
	ldr	r3, .L2114+8
	ldrb	r3, [r3, #-3268]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2081
	ldr	r0, .L2114+12
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #528]
	mov	r2, #1
	str	r2, [r3, #16]
	b	.L2082
.L2073:
	ldrb	r3, [r5, #660]	@ zero_extendqisi2
	mov	r0, r8, lsr #1
	mov	r2, r7
	mov	r1, r4
	cmp	r3, #25
	mov	r3, r4
	movcc	lr, #64
	movcs	lr, #128
.L2084:
	cmp	r1, r0
	add	ip, r3, lr
	add	r2, r2, #4
	bcs	.L2112
	ldr	r4, [r5, #628]
	mov	r3, r3, lsr #2
	add	r1, r1, #1
	ldr	r3, [r4, r3, asl #2]
	mov	r4, r3, lsr #8
	strb	r3, [r2, #-4]
	strb	r4, [r2, #-3]
	mov	r4, r3, lsr #16
	mov	r3, r3, lsr #24
	strb	r4, [r2, #-2]
	strb	r3, [r2, #-1]
	mov	r3, ip
	b	.L2084
.L2112:
	ldr	lr, [r5, #528]
	mov	ip, #0
	mov	r1, r8, lsr #2
	mov	r0, ip
.L2086:
	cmp	ip, r1
	bcs	.L2113
	add	r3, ip, #8
	ldr	r3, [lr, r3, asl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2096
	ldr	r3, [sp, #4]
	tst	r3, #32768
	bne	.L2096
	ldr	r2, [sp, #4]
	ldr	r8, [sp, #4]
	ldr	r3, [sp, #4]
	ubfx	r2, r2, #3, #5
	ldr	r4, [sp, #4]
	ubfx	r8, r8, #27, #1
	ubfx	r3, r3, #16, #5
	ubfx	r4, r4, #29, #1
	orr	r2, r2, r8, asl #5
	orr	r3, r3, r4, asl #5
	cmp	r2, r3
	ldr	r2, [sp, #4]
	ldrhi	r3, [sp, #4]
	ubfxhi	r2, r2, #3, #5
	ldrls	r3, [sp, #4]
	ubfxls	r2, r2, #16, #5
	ubfxhi	r3, r3, #27, #1
	ubfxls	r3, r3, #29, #1
	orr	r3, r2, r3, asl #5
	cmp	r0, r3
	movcc	r0, r3
	b	.L2087
.L2096:
	mvn	r0, #0
.L2087:
	add	ip, ip, #1
	b	.L2086
.L2113:
	ldr	r3, [r5, #528]
	mov	r2, #0
	str	r2, [r3, #16]
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2081
	mov	r1, r4
	ldr	r0, .L2114+16
	bl	printk
	ldr	r3, .L2114
	orr	r4, r4, #131072
	ldr	r3, [r3, #528]
	str	r4, [r3]
	b	.L2082
.L2081:
	cmn	r0, #1
	beq	.L2101
	ldr	r3, [r7]
	cmn	r3, #1
	bne	.L2101
	ldr	r3, [r7, #4]
	cmn	r3, #1
	bne	.L2101
	ldr	r3, [r6]
	cmn	r3, #1
	moveq	r0, #512
	b	.L2101
.L2082:
	mvn	r0, #0
.L2101:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2115:
	.align	2
.L2114:
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
	mov	fp, r3
	ldr	r7, .L2131
	mvn	r4, #0
	.pad #20
	sub	sp, sp, #20
	mov	r5, r0
	mov	ip, r1
	ubfx	r8, r1, #24, #2
	ldrb	r3, [r7, #676]	@ zero_extendqisi2
	str	r2, [sp, #12]
	rsb	r3, r3, #24
	ldr	r6, [r7, #528]
	bic	r4, r1, r4, asl r3
	ldr	r3, .L2131+4
	ldr	r3, [r3]
	tst	r3, #16
	beq	.L2117
	ldr	r0, .L2131+8
	mov	r1, r5
	mov	r2, r8
	mov	r3, ip
	bl	printk
.L2117:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	cmp	r8, #0
	mov	r9, r5, asl #8
	bne	.L2118
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L2119
.L2118:
	ldr	r3, [r7, #584]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2120
	ldr	r3, .L2131
	ldrb	r3, [r3, #684]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r6, r9
	streq	r8, [r3, #2056]
	beq	.L2119
.L2120:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L2119:
	add	r10, r6, r9
	mov	r2, #0
	str	r2, [r10, #2056]
	str	r2, [r10, #2052]
	str	r2, [r10, #2052]
	uxtb	r2, r4
	str	r2, [r10, #2052]
	mov	r2, r4, lsr #8
	str	r2, [r10, #2052]
	mov	r2, r4, lsr #16
	str	r2, [r10, #2052]
	mov	r2, #48
	str	r2, [r10, #2056]
	ldr	r2, [r7, #584]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r8, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2121
	ldr	r2, .L2131
	ldrb	r2, [r2, #684]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r4, r4, r4, asl #1
	subeq	r0, r4, #1
	addeq	r0, r0, r8
	beq	.L2130
.L2121:
	mov	r0, r4
.L2130:
	bl	nandc_set_seed
	add	r6, r6, r9
	bl	nandc_wait_flash_ready
	mov	r4, #0
	mov	r1, r4
	mov	r2, #5
	mov	r3, #224
	str	r2, [r6, #2056]
	mov	r0, r5
	str	r4, [r10, #2052]
	str	r4, [r10, #2052]
	str	r3, [r6, #2056]
	ldrb	r2, [sp, #56]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	str	fp, [sp]
	bl	nandc_xfer
	mov	r5, r0
	mov	r0, r4
	bl	nandc_de_cs
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2132:
	.align	2
.L2131:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC151
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
	ldr	r4, .L2161
	mov	r6, r0
	str	r3, [sp, #24]
	mov	r9, r1
	str	r2, [sp, #20]
	mov	r8, #0
	ldrb	r3, [r4, #660]	@ zero_extendqisi2
	ldr	r10, .L2161+4
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #528]
	str	r3, [sp, #16]
.L2134:
	ldr	r3, [sp, #16]
	mov	r7, #0
	mvn	r4, #0
	add	r5, r3, r6, asl #8
.L2135:
	ldrb	r3, [r10, #-3260]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L2139
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #137
	str	r3, [r5, #2052]
	add	fp, r7, #1
	bl	timer_delay_ns
	str	fp, [r5, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	mov	r0, r6
	str	r3, [r5, #2048]
	mov	r1, r9
	ldr	r3, [sp, #72]
	ldr	r2, [sp, #20]
	str	r3, [sp]
	ldr	r3, [sp, #24]
	bl	flash_read_page
	ldr	r3, .L2161+8
	ldr	r3, [r3]
	tst	r3, #4096
	mov	ip, r0
	beq	.L2136
	str	r0, [sp]
	mov	r1, r7
	str	r0, [sp, #28]
	mov	r2, r9
	ldr	r0, .L2161+12
	mov	r3, r4
	bl	printk
	ldr	ip, [sp, #28]
.L2136:
	cmn	ip, #1
	beq	.L2137
	ldr	r3, [r10, #-3272]
	cmn	r4, #1
	moveq	r4, ip
	str	r3, [sp, #20]
	ldr	r3, [r10, #-3264]
	str	r3, [sp, #24]
	ldr	r3, [sp, #12]
	cmp	ip, r3
	bcc	.L2146
.L2137:
	mov	r7, fp
	b	.L2135
.L2146:
	mov	r4, ip
.L2139:
	ldr	r3, [sp, #16]
	mov	r5, r6, asl #8
	mov	r0, #200
	add	fp, r3, r5
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
	bcc	.L2141
	cmn	r4, #1
	movne	r4, #256
.L2141:
	cmn	r4, #1
	movne	fp, #0
	moveq	fp, #1
	cmp	r4, #256
	movne	r3, fp
	orreq	r3, fp, #1
	cmp	r3, #0
	beq	.L2142
	mov	r1, r7
	mov	r3, r7
	str	r4, [sp]
	mov	r2, r9
	ldr	r0, .L2161+16
	eor	r7, r8, #1
	bl	printk
	ands	r7, fp, r7
	beq	.L2143
	bl	nandc_wait_flash_ready
	ldr	r3, .L2161
	mov	r0, #200
	mov	r8, #1
	ldr	r3, [r3, #528]
	add	r5, r3, r5
	mov	r3, #239
	str	r3, [r5, #2056]
	mov	r3, #150
	str	r3, [r5, #2052]
	bl	timer_delay_ns
	mov	r3, #3
	str	r3, [r5, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	b	.L2134
.L2143:
	cmp	r8, #0
	beq	.L2144
	bl	nandc_wait_flash_ready
	ldr	r3, .L2161
	mov	r0, #200
	ldr	r9, [r3, #528]
	mov	r3, #239
	add	r6, r9, r6, asl #8
	add	r5, r9, r5
	str	r3, [r5, #2056]
	mov	r3, #150
	str	r3, [r6, #2052]
	bl	timer_delay_ns
	cmn	r4, #1
	str	r7, [r6, #2048]
	movne	r4, #256
	str	r7, [r6, #2048]
	str	r7, [r6, #2048]
	str	r7, [r6, #2048]
	b	.L2144
.L2142:
	cmp	r8, #0
	beq	.L2144
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r2, .L2161
	mov	r0, #200
	mov	r4, #256
	ldr	r9, [r2, #528]
	mov	r2, #239
	add	r6, r9, r6, asl #8
	add	r5, r9, r5
	str	r2, [r5, #2056]
	mov	r2, #150
	str	r2, [r6, #2052]
	bl	timer_delay_ns
	ldr	r3, [sp, #12]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
.L2144:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2162:
	.align	2
.L2161:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC152
	.word	.LC153
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
	ldr	r7, .L2211
	add	r6, r10, #8
	ldr	r3, [r7, #528]
	add	r6, r3, r6, asl #8
	str	r3, [sp, #8]
	ldrb	r3, [r7, #661]	@ zero_extendqisi2
	cmp	r3, #36
	movne	r3, #56
	moveq	r3, #46
	movne	r5, #10
	moveq	r5, #26
	cmp	r4, #0
	str	r3, [sp, #16]
	mvn	r4, #0
	bne	.L2165
	ldr	r3, [sp, #8]
	mov	r8, #1
	add	r3, r3, r10, asl #8
	str	r3, [sp, #16]
.L2172:
	ldr	r3, .L2211
	mov	r0, r6
	ldrb	r3, [r3, #661]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2166
	mov	r2, #0
	mov	r1, r8
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	ldr	r2, [sp, #16]
	str	r3, [r2, #2056]
	b	.L2167
.L2166:
	uxtb	r1, r8
	bl	toshiba_3d_set_slc_rr_para
.L2167:
	ldr	r3, [sp, #64]
	mov	r0, r10
	mov	r1, r9
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2211+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2168
	mov	r3, ip
	ldr	r0, .L2211+8
	mov	r1, r8
	mov	r2, r9
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2168:
	cmn	ip, #1
	beq	.L2169
	ldr	r3, .L2211+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-3272]
	ldr	fp, [r3, #-3264]
	ldrb	r3, [r7, #660]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2187
.L2169:
	add	r8, r8, #1
	cmp	r8, r5
	bne	.L2172
	b	.L2171
.L2187:
	mov	r5, r8
	mov	r4, ip
.L2171:
	ldr	r3, .L2211
	mov	r1, #0
	mov	r0, r6
	ldrb	r3, [r3, #661]	@ zero_extendqisi2
	cmp	r3, #36
	moveq	r2, r1
	beq	.L2210
	bl	toshiba_3d_set_slc_rr_para
	b	.L2174
.L2165:
	ldr	r3, [sp, #8]
	mov	r5, #1
	add	r8, r3, r10, asl #8
.L2181:
	ldr	r3, .L2211
	mov	r0, r6
	ldrb	r3, [r3, #661]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2175
	mov	r1, r5
	mov	r2, #1
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	b	.L2209
.L2175:
	uxtb	r1, r5
	bl	toshiba_3d_set_tlc_rr_para
	mov	r3, #38
.L2209:
	str	r3, [r8, #2056]
	mov	r0, r10
	ldr	r3, [sp, #64]
	mov	r1, r9
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2211+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2177
	mov	r3, ip
	ldr	r0, .L2211+16
	mov	r1, r5
	mov	r2, r9
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2177:
	cmn	ip, #1
	beq	.L2178
	ldr	r3, .L2211+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-3272]
	ldr	fp, [r3, #-3264]
	ldrb	r3, [r7, #660]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2188
.L2178:
	ldr	r3, [sp, #16]
	add	r5, r5, #1
	cmp	r5, r3
	bne	.L2181
	b	.L2180
.L2188:
	mov	r4, ip
.L2180:
	ldr	r3, .L2211
	mov	r0, r6
	mov	r1, #0
	ldrb	r3, [r3, #661]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2182
	mov	r2, #1
.L2210:
	bl	toshiba_tlc_set_rr_para
	b	.L2174
.L2182:
	bl	toshiba_3d_set_tlc_rr_para
.L2174:
	ldr	r3, .L2211
	ldrb	r3, [r3, #661]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2183
	ldr	r3, [sp, #8]
	add	r10, r3, r10, asl #8
	mov	r3, #85
	str	r3, [r10, #2056]
	mov	r3, #0
	str	r3, [r10, #2052]
	str	r3, [r10, #2048]
	mov	r3, #255
	str	r3, [r10, #2056]
.L2183:
	ldrb	r3, [r7, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2184
	cmn	r4, #1
	movne	r4, #256
.L2184:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2185
	str	r4, [sp]
	mov	r1, r5
	ldr	r0, .L2211+20
	mov	r2, r9
	mov	r3, r5
	bl	printk
.L2185:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2212:
	.align	2
.L2211:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC154
	.word	.LANCHOR3
	.word	.LC155
	.word	.LC156
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
	mov	r8, r0
	ldr	r4, .L2246
	str	r3, [sp, #20]
	add	r9, r8, #8
	str	r1, [sp, #12]
	str	r2, [sp, #16]
	bl	nandc_wait_flash_ready
	ldrb	r3, [r4, #661]	@ zero_extendqisi2
	ldr	r6, [r4, #528]
	sub	r3, r3, #67
	cmp	r3, #1
	add	r9, r6, r9, asl #8
	mov	r3, r8, asl #8
	str	r3, [sp, #24]
	movls	r3, #0
	strls	r3, [sp, #8]
	bls	.L2214
	ldrb	r5, [r4, #623]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #8]
	beq	.L2215
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #1
	str	r3, [sp, #8]
.L2215:
	ldr	r3, [sp, #24]
	mov	r2, #92
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L2214:
	ldr	fp, .L2246+4
	mov	r7, #1
	mvn	r10, #0
	mov	r3, r8, asl #8
	str	r3, [sp, #28]
.L2216:
	ldrb	r3, [fp, #-3260]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L2245
	ldrb	r3, [r4, #661]	@ zero_extendqisi2
	mov	r0, r9
	uxtb	r1, r7
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2217
	bl	sandisk_set_rr_para
	b	.L2218
.L2217:
	bl	toshiba_set_rr_para
.L2218:
	ldrb	r3, [r4, #661]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L2219
	ldr	r3, .L2246+4
	ldrb	r3, [r3, #-3260]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r7, r3
	ldreq	r3, [sp, #28]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L2219:
	ldr	r3, [sp, #24]
	mov	r2, #38
	add	r1, sp, #12
	mov	r0, r8
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	flash_read_page
	cmn	r0, #1
	mov	r5, r0
	beq	.L2222
	ldr	r3, [fp, #-3272]
	cmn	r10, #1
	moveq	r10, r0
	str	r3, [sp, #16]
	ldr	r3, [fp, #-3264]
	str	r3, [sp, #20]
	ldrb	r3, [r4, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2224
.L2222:
	add	r7, r7, #1
	b	.L2216
.L2245:
	mov	r5, r10
.L2224:
	ldrb	r3, [r4, #661]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, #0
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2226
	bl	sandisk_set_rr_para
	b	.L2227
.L2226:
	bl	toshiba_set_rr_para
.L2227:
	add	r6, r6, r8, asl #8
	mov	r3, #255
	str	r3, [r6, #2056]
	ldrb	r3, [r4, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, asr #2
	bcc	.L2228
	cmn	r5, #1
	movne	r5, #256
.L2228:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L2229
	str	r5, [sp]
	mov	r1, r7
	ldr	r0, .L2246+8
	mov	r3, r7
	ldr	r2, [sp, #12]
	bl	printk
.L2229:
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L2230
	mov	r0, #4
	bl	nandc_set_if_mode
.L2230:
	mov	r0, r5
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2247:
	.align	2
.L2246:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC156
	.fnend
	.size	toshiba_read_retrial, .-toshiba_read_retrial
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
	ldr	r3, .L2267
	.pad #28
	sub	sp, sp, #28
	mov	r10, r2
	mov	r7, r0
	mov	r8, r1
	mov	r6, #0
	ldr	r2, [r3, #664]
	mvn	r4, #0
	str	r3, [sp, #16]
	add	r3, r2, r0
	str	r3, [sp, #12]
	ldrb	r9, [r2, #114]	@ zero_extendqisi2
	ldrb	r5, [r3, #120]	@ zero_extendqisi2
	bl	nandc_wait_flash_ready
	ldr	ip, .L2267+4
.L2249:
	cmp	r6, r9
	bcs	.L2253
	add	r5, r5, #1
	mov	r0, r7
	str	ip, [sp, #20]
	uxtb	r5, r5
	cmp	r5, r9
	movcs	r5, #0
	mov	r1, r5
	bl	hynix_set_rr_para
	mov	r0, r7
	mov	r1, r8
	mov	r2, r10
	ldr	r3, [sp, #64]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	cmn	r0, #1
	ldr	ip, [sp, #20]
	beq	.L2251
	ldr	r3, [sp, #16]
	cmn	r4, #1
	ldr	r10, [ip, #-3272]
	moveq	r4, r0
	ldr	fp, [ip, #-3264]
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2257
.L2251:
	add	r6, r6, #1
	b	.L2249
.L2257:
	mov	r4, r0
.L2253:
	ldr	r3, [sp, #12]
	strb	r5, [r3, #120]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2255
	cmn	r4, #1
	movne	r4, #256
.L2255:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2256
	str	r4, [sp]
	mov	r1, r6
	ldr	r0, .L2267+8
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2256:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2268:
	.align	2
.L2267:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC157
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
.L2275:
	uxtb	r0, r7
	bl	nandc_set_ddr_para
	mov	r0, r8
	mov	r1, r10
	mov	r2, fp
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	ldr	r3, .L2293
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2270
	mov	r3, ip
	ldr	r0, .L2293+4
	mov	r1, r7
	mov	r2, r10
	str	ip, [sp, #28]
	bl	printk
	ldr	ip, [sp, #28]
.L2270:
	add	r3, r6, #1
	cmp	ip, r3
	bhi	.L2271
	ldr	r3, .L2293+8
	ldr	fp, [r3, #-3272]
	ldr	r3, [r3, #-3264]
	str	r3, [sp, #16]
	ldr	r3, .L2293+12
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	cmp	ip, r3, lsr #2
	bcs	.L2281
	add	r5, r5, #1
	cmp	r5, #7
	bls	.L2281
	rsb	r4, r5, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2273
.L2271:
	ldr	r3, [sp, #12]
	cmp	r3, r5
	bcs	.L2282
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #20]
	bhi	.L2274
	str	r5, [sp, #12]
	b	.L2282
.L2281:
	mov	r4, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2272
.L2282:
	mov	r5, #0
.L2272:
	add	r7, r7, #2
	cmp	r7, #50
	bne	.L2275
.L2273:
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r2, r5
	movhi	r4, r3
.L2274:
	cmp	r4, #0
	beq	.L2276
	ldr	r3, .L2293+12
	ldr	r2, .L2293+16
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	umull	r2, r3, r3, r2
	ubfx	r3, r3, #1, #8
	cmp	r6, r3
	bcs	.L2276
	ldr	r0, .L2293+20
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	b	.L2292
.L2276:
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
.L2292:
	bl	nandc_set_ddr_para
	cmn	r9, #1
	bne	.L2278
	mov	r1, r8
	mov	r2, r10
	ldr	r0, .L2293+24
	bl	printk
	mov	r0, r8
	bl	flash_reset
	ldr	r4, .L2293+12
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	add	r3, r4, r8
	mov	r0, r8
	mov	r2, #2
	strb	r2, [r3, #680]
	bl	zftl_flash_enter_slc_mode
	mov	r1, r10
	mov	r2, fp
	mov	r0, r8
	mov	r5, r4
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	mov	r1, r8
	mov	r2, r10
	mov	r6, r0
	ldr	r0, .L2293+28
	mov	r3, r6
	bl	printk
	ldrb	r3, [r4, #660]	@ zero_extendqisi2
	cmp	r6, r3
	bhi	.L2283
	ldr	r2, .L2293+8
	ldr	r3, [r2, #-3256]
	add	r3, r3, #1
	str	r3, [r2, #-3256]
	cmp	r3, #100
	movhi	r3, #0
	strhib	r3, [r4, #623]
	bls	.L2279
	b	.L2278
.L2283:
	mov	r6, r9
.L2279:
	ldrb	r0, [r5, #720]	@ zero_extendqisi2
	bl	flash_set_interface_mode
	ldrb	r0, [r5, #720]	@ zero_extendqisi2
	bl	nandc_set_if_mode
.L2278:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2294:
	.align	2
.L2293:
	.word	.LANCHOR2
	.word	.LC158
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC159
	.word	.LC160
	.word	.LC161
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
	ldr	r5, .L2326
	mov	r10, r0
	mov	r4, r1
	mov	r8, r2
	ldr	r9, [sp, #48]
	ldrb	r3, [r5, #589]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2296
	ldr	r1, .L2326+4
	mov	r2, #404
	ldr	r0, .L2326+8
	bl	printk
	bl	dump_stack
.L2296:
	add	r2, r5, r10
	ldr	r3, .L2326
	ldrb	r6, [r2, #668]	@ zero_extendqisi2
	ldrb	r2, [r5, #589]	@ zero_extendqisi2
	cmp	r2, r10
	bcs	.L2297
	ldr	r3, .L2326+12
	ldr	r3, [r3]
	tst	r3, #64
	beq	.L2325
	ldr	r0, .L2326+16
	mov	r1, r6
	mov	r2, r10
	mov	r3, r4
	bl	printk
.L2325:
	mvn	r0, #0
	b	.L2322
.L2297:
	tst	r4, #50331648
	bne	.L2300
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2301
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2300
.L2301:
	ldrh	r10, [r5, #2]
	mov	r0, r4
	mov	r1, r10
	bl	__aeabi_uidiv
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L2326
	mul	r0, r10, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L2300:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	cmn	r0, #1
	bne	.L2322
	ldrb	fp, [r5, #656]	@ zero_extendqisi2
	ldr	r10, .L2326
	cmp	fp, #0
	bne	.L2304
.L2307:
	ldr	r3, .L2326+20
	ldr	ip, [r3, #-3252]
	cmp	ip, #0
	bne	.L2305
	b	.L2306
.L2304:
	mov	r3, #0
	mov	r0, r6
	strb	r3, [r10, #656]
	mov	r1, r4
	str	r9, [sp]
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	strb	fp, [r10, #656]
	cmn	r0, #1
	beq	.L2307
	b	.L2322
.L2305:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	blx	ip
	cmn	r0, #1
	bne	.L2322
.L2306:
	ldrb	r3, [r5, #656]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2326+24
	mov	r2, r4
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #623]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2325
	str	r9, [sp, #48]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	flash_ddr_tuning_read
.L2322:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2327:
	.align	2
.L2326:
	.word	.LANCHOR0
	.word	.LANCHOR1+1536
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC162
	.word	.LANCHOR3
	.word	.LC163
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
	ldr	r6, .L2339
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L2339+4
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
	bne	.L2329
.L2330:
	sxth	r3, ip
	sxth	r2, r4
	cmp	r3, r2
	bgt	.L2329
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
	b	.L2330
.L2329:
	ldr	r3, [r6]
	sxth	r4, r4
	tst	r3, #4096
	beq	.L2334
	ldr	r0, .L2339+8
	mov	r1, r9
	mov	r2, r4
	ldr	r3, [r8]
	bl	printk
.L2334:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2340:
	.align	2
.L2339:
	.word	.LANCHOR2
	.word	.LANCHOR3-3360
	.word	.LC164
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
	mov	r4, r1
	ldr	r1, .L2343
	mov	lr, r2
	mov	ip, r0
	add	r1, r1, #2928
	ldrb	r2, [r1, #-2252]	@ zero_extendqisi2
	ldrh	r1, [r1]
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
.L2344:
	.align	2
.L2343:
	.word	.LANCHOR0
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
	ldr	r4, .L2352
	mov	r7, r1
	mov	r6, #1
	ldr	r5, .L2352+4
	mov	r9, #4
	ldrb	r0, [r4, #720]	@ zero_extendqisi2
	strb	r6, [r4, #623]
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #720]	@ zero_extendqisi2
	bl	nandc_set_if_mode
	mov	r1, r7
	ldr	r2, [r5, #-3248]
	mov	r0, r8
	ldr	r3, [r5, #-3244]
	str	r9, [sp]
	bl	flash_ddr_tuning_read
	ldr	r3, [r5, #-3244]
	mov	r0, r8
	mov	r1, r7
	ldr	r2, [r5, #-3248]
	str	r9, [sp]
	bl	flash_read_page
	mov	r3, r4
	cmn	r0, #1
	bne	.L2346
	ldrb	r2, [r4, #720]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L2346
	mov	r0, r6
	bl	flash_set_interface_mode
	mov	r0, r6
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #623]
	b	.L2347
.L2346:
	mov	r2, #1
	strb	r2, [r3, #623]
.L2347:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2353:
	.align	2
.L2352:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.global	__aeabi_uidivmod
	.align	2
	.type	id_block_read_data.constprop.30, %function
id_block_read_data.constprop.30:
	.fnstart
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2386
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #132
	sub	sp, sp, #132
	ldr	r3, [r3]
	mov	r6, r2
	str	r1, [sp, #24]
	mov	r5, #0
	ldr	r4, .L2386+4
	mov	r7, #4
	str	r3, [sp, #124]
	mov	r10, r5
	ldr	r3, .L2386+8
	str	r0, [sp, #40]
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r1, [r3, #30]
	str	r2, [sp, #28]
	ldrh	r3, [sp, #28]
	smulbb	r1, r1, r3
	ldrb	r3, [r4, #656]	@ zero_extendqisi2
	strb	r5, [r4, #656]
	str	r3, [sp, #44]
	uxth	r1, r1
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #40]
	str	r1, [sp, #12]
	rsb	r3, r1, r3
	str	r3, [sp, #32]
	and	r3, r1, #3
	str	r3, [sp, #16]
.L2355:
	ldr	r3, [sp, #24]
	cmp	r5, r3
	bcs	.L2385
	ldr	r3, [sp, #16]
	mov	r1, r7
	rsb	r3, r3, r7
	uxth	r3, r3
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	add	r0, r5, r3
	bl	__aeabi_uidiv
	ldrb	r1, [r4, #590]	@ zero_extendqisi2
	cmp	r1, #0
	uxth	r0, r0
	mov	r3, r0, asl #1
	add	r2, r4, r3
	ldrh	r2, [r2, #4]
	moveq	r0, r2
	beq	.L2357
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r0, r3
.L2357:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #16]
	mla	r3, r3, r0, r2
	str	r3, [sp, #36]
	ldr	r3, .L2386+4
	ldr	r2, [sp, #36]
	ldrb	r9, [r3, #588]	@ zero_extendqisi2
	add	r0, r2, r1
	ldr	r3, [r3, #584]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, .L2386+4
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	mov	fp, r0
	mov	r0, r9
	str	r3, [sp, #48]
	bl	nandc_bch_sel
.L2358:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #60
	bl	flash_read_page
	cmn	r0, #1
	mov	r8, r0
	bne	.L2365
	ldrb	ip, [r4, #656]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L2360
.L2363:
	ldr	r3, .L2386+12
	ldr	ip, [r3, #-3252]
	cmp	ip, #0
	bne	.L2361
	b	.L2362
.L2360:
	mov	r0, #0
	str	r7, [sp]
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #60
	str	ip, [sp, #52]
	strb	r0, [r4, #656]
	bl	flash_read_page
	ldr	ip, [sp, #52]
	cmn	r0, #1
	strb	ip, [r4, #656]
	beq	.L2363
	b	.L2384
.L2361:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #60
	blx	ip
	cmn	r0, #1
	bne	.L2384
.L2362:
	ldrb	r3, [r4, #623]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2365
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #60
	bl	flash_ddr_tuning_read
.L2384:
	mov	r8, r0
.L2365:
	subs	r9, r9, #16
	movne	r9, #1
	cmn	r8, #1
	movne	r9, #0
	cmp	r9, #0
	beq	.L2366
	mov	r0, #16
	mov	r9, #16
	bl	nandc_bch_sel
	b	.L2358
.L2366:
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
	beq	.L2368
	ldr	r3, [r6]
	ldr	r2, .L2386+16
	cmp	r3, r2
	bne	.L2368
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #20]
	ldrb	r7, [r6, #17]	@ zero_extendqisi2
	add	r3, r3, r2
	str	r3, [sp, #24]
	b	.L2369
.L2368:
	ldr	r3, [sp, #20]
	mov	r2, r5
	ldr	r0, .L2386+20
	ldr	r1, [sp, #36]
	add	r6, r6, r3, asl #9
	ldr	r3, [sp, #64]
	str	r3, [sp]
	ldr	r3, [sp, #60]
	bl	printk
	mov	r3, #0
	str	r3, [sp, #16]
.L2369:
	ldr	r3, [sp, #20]
	add	r5, r3, r5
	uxth	r5, r5
	b	.L2355
.L2385:
	ldr	r3, .L2386+4
	mov	r0, r10
	ldrb	r2, [sp, #44]	@ zero_extendqisi2
	strb	r2, [r3, #656]
	ldr	r3, .L2386
	ldr	r2, [sp, #124]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2371
	bl	__stack_chk_fail
.L2371:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2387:
	.align	2
.L2386:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	1179535694
	.word	.LC165
	.fnend
	.size	id_block_read_data.constprop.30, .-id_block_read_data.constprop.30
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
	ldr	r4, .L2405
	mov	r10, r0
	mov	r6, r1
	mov	r7, r2
	ldrb	r8, [r4, #676]	@ zero_extendqisi2
	rsb	r9, r8, #24
	mvn	r8, r3, asl r8
	and	r3, r8, r0, lsr r9
	uxtb	r5, r3
	ldrb	r3, [r4, #589]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L2389
	ldr	r1, .L2405+4
	movw	r2, #909
	ldr	r0, .L2405+8
	bl	printk
	bl	dump_stack
.L2389:
	ldrb	r2, [r4, #589]	@ zero_extendqisi2
	ldr	r3, .L2405
	cmp	r2, r5
	mvnls	r0, #0
	bls	.L2390
	add	r5, r3, r5
	mvn	r2, #0
	bic	r9, r10, r2, asl r9
	ubfx	r10, r10, #24, #2
	ldrb	r5, [r5, #668]	@ zero_extendqisi2
	ldr	r8, [r3, #528]
	mov	r0, r5
	bl	nandc_cs
	cmp	r10, #0
	bne	.L2391
	mov	r0, r9
	bl	slc_phy_page_address_calc
	mov	r9, r0
.L2391:
	ldrb	r3, [r4, #607]	@ zero_extendqisi2
	uxtb	r0, r9
	mov	r1, r9, lsr #8
	mov	r2, r9, lsr #16
	cmp	r3, #1
	mov	r3, r5, asl #8
	addeq	r3, r8, r3
	moveq	ip, #6
	beq	.L2404
	ldr	ip, .L2405
	add	r3, r8, r3
	ldr	ip, [ip, #584]
	ldrb	ip, [ip, #12]	@ zero_extendqisi2
	cmp	ip, #3
	bne	.L2394
	mov	ip, #5
.L2404:
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	str	r0, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2052]
	b	.L2402
.L2394:
	mov	ip, #0
	str	ip, [r3, #2056]
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	str	r0, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2052]
	mov	r2, #5
	str	r2, [r3, #2056]
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
.L2402:
	mov	r2, #224
	str	r2, [r3, #2056]
	ldr	r3, [r4, #584]
	ldrb	r2, [r3, #12]	@ zero_extendqisi2
	adds	r3, r10, #0
	movne	r3, #1
	cmp	r2, #3
	movne	r3, #0
	cmp	r3, #0
	beq	.L2395
	ldr	r3, .L2405
	ldrb	r3, [r3, #684]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r3, r10, #1
	addeq	r0, r9, r9, asl #1
	addeq	r0, r3, r0
	beq	.L2403
.L2395:
	mov	r0, r9
.L2403:
	bl	nandc_set_seed
	ldr	r3, .L2405+12
	mov	r1, #0
	mov	r0, r5
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r3, r6
	str	r7, [sp]
	bl	nandc_xfer
	mov	r4, r0
	mov	r0, r5
	bl	nandc_de_cs
	mov	r0, r4
.L2390:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2406:
	.align	2
.L2405:
	.word	.LANCHOR0
	.word	.LANCHOR1+1556
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
	ldr	r5, .L2441
	.pad #20
	sub	sp, sp, #20
	mov	r9, r0
	mov	r10, r1
	mov	fp, r2
	ldrb	r7, [r5, #676]	@ zero_extendqisi2
	rsb	r3, r7, #24
	mov	r7, r6, asl r7
	sub	r7, r7, #1
	mov	r4, r6, asl r3
	and	r7, r7, r0, lsr r3
	ldrb	r3, [r5, #589]	@ zero_extendqisi2
	sub	r4, r4, #1
	ubfx	r6, r0, #24, #2
	uxtb	r7, r7
	and	r4, r4, r0
	cmp	r3, r7
	bhi	.L2408
	ldr	r1, .L2441+4
	movw	r2, #1033
	ldr	r0, .L2441+8
	bl	printk
	bl	dump_stack
.L2408:
	add	r7, r5, r7
	ldrb	r8, [r7, #668]	@ zero_extendqisi2
	ldr	r7, [r5, #528]
	mov	r0, r8
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2409
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2409:
	add	r3, r7, r8, asl #8
	mov	r2, #5
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
	ldr	r3, [r5, #584]
	ldrb	r2, [r3, #12]	@ zero_extendqisi2
	adds	r3, r6, #0
	movne	r3, #1
	cmp	r2, #3
	movne	r3, #0
	cmp	r3, #0
	beq	.L2410
	ldr	r3, .L2441
	ldrb	r3, [r3, #684]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r6, #1
	addeq	r3, r4, r4, asl #1
	addeq	r0, r0, r3
	beq	.L2440
.L2410:
	mov	r0, r4
.L2440:
	bl	nandc_set_seed
	ldr	r7, .L2441+12
	mov	r0, r8
	mov	r1, #0
	mov	r3, r10
	ldrb	r2, [r7, #13]	@ zero_extendqisi2
	str	fp, [sp]
	bl	nandc_xfer
	cmn	r0, #1
	bne	.L2412
	ldrb	ip, [r5, #656]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L2413
	ldr	r2, .L2441
	mov	r3, #0
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	str	ip, [sp, #12]
	strb	r3, [r2, #656]
	mov	r2, r10
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2441
	cmp	r6, #0
	ldr	ip, [sp, #12]
	strb	ip, [r3, #656]
	bne	.L2414
.L2421:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2414
	ldr	r3, .L2441
	ldrb	r3, [r3, #660]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	blt	.L2414
	ldrb	r3, [r7, #23]	@ zero_extendqisi2
	sub	r3, r3, #4
	cmp	r3, #4
	movls	r0, #256
	b	.L2428
.L2414:
	cmn	r0, #1
	bne	.L2428
.L2422:
	ldr	r3, .L2441+16
	ldr	ip, [r3, #-3252]
	cmp	ip, #0
	bne	.L2417
.L2420:
	ldrb	r3, [r5, #656]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2441+20
	mov	r2, r9
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #623]	@ zero_extendqisi2
	cmp	r3, #0
	mvneq	r0, #0
	beq	.L2428
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	bl	flash_ddr_tuning_read
	b	.L2428
.L2417:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	blx	ip
	cmn	r0, #1
	bne	.L2428
	b	.L2420
.L2412:
	cmp	r6, #0
	beq	.L2421
	b	.L2428
.L2413:
	cmp	r6, #0
	beq	.L2421
	b	.L2422
.L2428:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2442:
	.align	2
.L2441:
	.word	.LANCHOR0
	.word	.LANCHOR1+1588
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC166
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
	ldr	r4, .L2539
	ldrb	r5, [r4, #2263]	@ zero_extendqisi2
	cmp	r5, #255
	beq	.L2477
	mov	r3, #48
	mla	r3, r3, r5, r4
	ldrb	r2, [r3, #766]	@ zero_extendqisi2
	ldr	r6, [r3, #748]
	sub	r3, r2, #1
	cmp	r3, #10
	ldrls	pc, [pc, r3, asl #2]
	b	.L2477
.L2447:
	.word	.L2446
	.word	.L2448
	.word	.L2449
	.word	.L2449
	.word	.L2449
	.word	.L2449
	.word	.L2450
	.word	.L2451
	.word	.L2452
	.word	.L2449
	.word	.L2452
.L2446:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	beq	.L2477
	mov	r3, #48
	mla	r3, r3, r5, r4
	ldr	r2, [r3, #732]
	ldr	r1, [r3, #728]
	cmp	r2, #0
	beq	.L2454
	ldrb	r0, [r3, #764]	@ zero_extendqisi2
	ldr	r3, .L2539
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	cmp	r0, r3
	moveq	r1, r2
.L2454:
	mov	r3, #48
	mov	r0, r6
	mla	r5, r3, r5, r4
	ldr	r2, [r5, #736]
	bl	flash_complete_page_read
	mov	r3, #13
	strb	r3, [r5, #766]
	ldrb	r3, [r5, #726]	@ zero_extendqisi2
	orr	r3, r3, #8
	strb	r3, [r5, #726]
	str	r0, [r5, #760]
	b	.L2477
.L2448:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	mov	r2, #48
	mla	r3, r2, r5, r4
	ldr	r1, .L2539
	ldrb	r6, [r3, #724]	@ zero_extendqisi2
	tst	r0, #64
	beq	.L2477
	mla	r2, r2, r6, r1
	ldr	fp, [r3, #728]
	ldr	r9, [r2, #728]
	ldr	r2, [r3, #732]
	cmp	r2, #0
	beq	.L2455
	ldrb	r0, [r3, #764]	@ zero_extendqisi2
	ldrb	r3, [r1, #2260]	@ zero_extendqisi2
	cmp	r0, r3
	moveq	fp, r2
.L2455:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #732]
	cmp	r2, #0
	beq	.L2456
	ldrb	r1, [r3, #764]	@ zero_extendqisi2
	ldr	r3, .L2539
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r9, r2
.L2456:
	mov	r7, #48
	mov	r1, fp
	mla	r3, r7, r5, r4
	mla	r7, r7, r6, r4
	add	ip, r3, #732
	str	ip, [sp, #8]
	ldr	r2, [r3, #736]
	ldr	r0, [r3, #748]
	str	r3, [sp, #12]
	bl	flash_complete_plane_page_read
	mov	r1, r9
	ldr	r2, [r7, #736]
	mov	r8, r0
	ldr	r0, [r7, #748]
	bl	flash_complete_plane_page_read
	cmn	r8, #1
	mov	r10, r0
	beq	.L2457
	ldr	r3, [sp, #12]
	ldr	r3, [r3, #744]
	cmn	r3, #1
	beq	.L2458
	ldr	ip, [sp, #8]
	ldr	r2, [ip, #4]
	ldr	r2, [r2, #4]
	cmp	r3, r2
	beq	.L2458
.L2457:
	mov	r7, #48
	ldrb	r0, [r4, #676]	@ zero_extendqisi2
	mla	r7, r7, r5, r4
	rsb	r1, r0, #24
	ldrb	lr, [r4, #2260]	@ zero_extendqisi2
	mvn	r3, #0
	mvn	r3, r3, asl r0
	add	ip, r7, #732
	str	ip, [sp, #8]
	ldr	r2, [r7, #748]
	str	lr, [sp]
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	mov	r2, fp
	ldr	r3, [r7, #736]
	uxtb	r0, r0
	bl	flash_read_page_en
	ldr	r2, [r7, #744]
	cmn	r2, #1
	mov	r8, r0
	beq	.L2459
	ldr	ip, [sp, #8]
	ldr	r3, [ip, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2459
	ldr	r0, .L2539+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2459
	str	r1, [sp]
	ldr	r0, .L2539+8
	ldr	r1, [r7, #748]
	ldr	r3, [r3]
	bl	printk
.L2459:
	mov	r3, #48
	mla	r3, r3, r5, r4
	ldr	r2, [r3, #744]
	cmn	r2, #1
	beq	.L2458
	ldr	r3, [r3, #736]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2458
	ldr	r1, .L2539+12
	mov	r2, #360
	ldr	r0, .L2539+16
	bl	printk
	bl	dump_stack
.L2458:
	mov	r3, #48
	mov	r2, #13
	mla	r5, r3, r5, r4
	cmn	r10, #1
	strb	r2, [r5, #766]
	ldrb	r2, [r5, #726]	@ zero_extendqisi2
	str	r8, [r5, #760]
	orr	r2, r2, #8
	strb	r2, [r5, #726]
	beq	.L2460
	ldr	r2, .L2539
	mla	r3, r3, r6, r2
	ldr	r2, [r3, #744]
	cmn	r2, #1
	beq	.L2462
	ldr	r3, [r3, #736]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2462
.L2460:
	mov	r5, #48
	ldrb	r0, [r4, #676]	@ zero_extendqisi2
	mla	r5, r5, r6, r4
	rsb	r1, r0, #24
	ldrb	ip, [r4, #2260]	@ zero_extendqisi2
	mvn	r3, #0
	mvn	r3, r3, asl r0
	ldr	r2, [r5, #748]
	str	ip, [sp]
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	mov	r2, r9
	ldr	r3, [r5, #736]
	uxtb	r0, r0
	bl	flash_read_page_en
	ldr	r2, [r5, #744]
	cmn	r2, #1
	beq	.L2464
	ldr	r3, [r5, #736]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2464
	ldr	r0, .L2539+4
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2464
	str	r1, [sp]
	ldr	r0, .L2539+8
	ldr	r1, [r5, #748]
	ldr	r3, [r3]
	bl	printk
.L2464:
	mov	r3, #48
	mla	r3, r3, r6, r4
	ldr	r2, [r3, #744]
	cmn	r2, #1
	beq	.L2462
	ldr	r3, [r3, #736]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2462
	ldr	r1, .L2539+12
	movw	r2, #375
	ldr	r0, .L2539+16
	bl	printk
	bl	dump_stack
.L2462:
	mov	r3, #48
	mla	r4, r3, r6, r4
	mov	r3, #13
	strb	r3, [r4, #766]
	ldrb	r3, [r4, #726]	@ zero_extendqisi2
	str	r8, [r4, #760]
	orr	r3, r3, #8
	strb	r3, [r4, #726]
	b	.L2477
.L2449:
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	beq	.L2477
	ands	r6, r0, #5
	mov	r8, #48
	beq	.L2465
	mla	r5, r8, r5, r4
	mov	r3, #12
	ldr	r0, .L2539+20
	mov	r2, r7
	mov	r6, r7
	strb	r3, [r5, #766]
	ldrb	r1, [r5, #725]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r5, #748]
	bl	printk
	mvn	r3, #0
	str	r3, [r5, #760]
	b	.L2445
.L2465:
	mul	r7, r8, r5
	mov	r3, #13
	ldr	r2, .L2539
	add	r8, r4, r7
	strb	r3, [r8, #766]
	ldr	r3, [r4, #2288]
	str	r6, [r8, #760]
	ldr	r1, [r3, #156]
	ldr	r3, .L2539+24
	cmp	r1, r3
	bne	.L2477
	add	r9, r2, r7
	add	r9, r9, #756
	ldrh	r3, [r9, #2]
	cmp	r3, #0
	bne	.L2477
	ldrb	r0, [r2, #676]	@ zero_extendqisi2
	mvn	r3, #0
	ldr	ip, [r8, #748]
	ldrb	r2, [r2, #2260]	@ zero_extendqisi2
	rsb	r1, r0, #24
	ldr	r7, .L2539+28
	mvn	r3, r3, asl r0
	and	r0, r3, ip, lsr r1
	bic	r1, ip, r3, asl r1
	str	r2, [sp]
	uxtb	r0, r0
	ldr	r3, [r7, #-3244]
	ldr	r2, [r7, #-3248]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2466
	ldr	r1, [r8, #736]
	ldr	r2, [r7, #-3244]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2477
.L2466:
	mov	r2, #48
	ldr	r0, .L2539+32
	mla	r5, r2, r5, r4
	ldrb	r2, [r4, #656]	@ zero_extendqisi2
	ldrb	r1, [r5, #725]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r5, #748]
	bl	printk
	mvn	r3, #0
	str	r3, [r5, #760]
	b	.L2445
.L2452:
	cmp	r2, #11
	ldr	r1, .L2539+36
	mov	r0, #48
	mvn	r2, #0
	movne	r3, #3
	moveq	r3, #10
	str	r3, [sp, #8]
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	mla	lr, r0, r5, r1
	rsb	r10, r3, #24
	mvn	r3, r2, asl r3
	and	ip, r3, r6, lsr r10
	str	r1, [sp, #12]
	uxth	ip, ip
.L2468:
	ldrb	r8, [lr]	@ zero_extendqisi2
	cmp	r8, #255
	beq	.L2538
	mul	r2, r0, r8
	ldr	r1, [sp, #12]
	ldr	r7, .L2539
	add	lr, r1, r2
	add	r2, r4, r2
	ldr	r1, [sp, #8]
	add	r9, r7, #724
	ldrb	fp, [r2, #766]	@ zero_extendqisi2
	cmp	fp, r1
	bne	.L2468
	ldr	r2, [r2, #748]
	and	r2, r3, r2, lsr r10
	cmp	ip, r2
	bne	.L2468
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r6, r0
	beq	.L2537
	ands	r2, r0, #15
	mov	r3, #48
	mul	r3, r3, r5
	beq	.L2472
	add	r7, r7, r3
	mov	r9, #12
	ldr	r0, .L2539+40
	mov	r2, r6
	ldrb	r1, [r7, #725]	@ zero_extendqisi2
	str	r9, [sp]
	ldr	r3, [r7, #748]
	bl	printk
	mvn	r3, #0
	strb	r9, [r7, #766]
	str	r3, [r7, #760]
	b	.L2471
.L2472:
	add	r6, r7, r3
	mov	r1, #13
	str	r2, [r6, #760]
	ldr	r2, [r7, #2288]
	strb	r1, [r6, #766]
	ldr	r1, [r2, #156]
	ldr	r2, .L2539+24
	cmp	r1, r2
	bne	.L2537
	add	r3, r9, r3
	ldrh	r3, [r3, #34]
	cmp	r3, #0
	bne	.L2537
	ldr	r2, [r6, #748]
	mvn	r3, #0
	ldrb	r0, [r7, #676]	@ zero_extendqisi2
	ldr	r9, .L2539+28
	rsb	r1, r0, #24
	ldrb	ip, [r7, #2260]	@ zero_extendqisi2
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	bic	r1, r2, r3, asl r1
	str	ip, [sp]
	uxtb	r0, r0
	ldr	r3, [r9, #-3244]
	ldr	r2, [r9, #-3248]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2474
	ldr	r1, [r6, #736]
	ldr	r2, [r9, #-3244]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2537
.L2474:
	mov	r6, #48
	ldrb	r2, [r4, #656]	@ zero_extendqisi2
	mla	r6, r6, r5, r4
	ldr	r0, .L2539+44
	ldrb	r1, [r6, #725]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r6, #748]
	bl	printk
	mvn	r3, #0
	str	r3, [r6, #760]
.L2537:
	mov	r6, #0
.L2471:
	mov	r3, #48
	mla	r5, r3, r5, r4
	mla	r4, r3, r8, r4
	ldrb	r2, [r5, #766]	@ zero_extendqisi2
	ldr	r3, [r5, #760]
	strb	r2, [r4, #766]
	str	r3, [r4, #760]
	b	.L2445
.L2538:
	mvn	r6, #0
	b	.L2445
.L2450:
	mov	r0, r6
	mov	r1, #32
	bl	flash_wait_device_ready
	tst	r0, #32
	beq	.L2477
	ands	r6, r0, #15
	mov	r3, #48
	mlaeq	r5, r3, r5, r4
	movne	r2, #12
	mlane	r3, r3, r5, r4
	moveq	r3, #13
	movne	r6, r0
	streqb	r3, [r5, #766]
	streq	r6, [r5, #760]
	strneb	r2, [r3, #766]
	mvnne	r2, #0
	strne	r2, [r3, #760]
	b	.L2445
.L2451:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	movne	r3, #48
	mlane	r5, r3, r5, r4
	movne	r3, #7
	strne	r0, [r5, #760]
	strneb	r3, [r5, #766]
.L2477:
	mov	r6, #0
.L2445:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2540:
	.align	2
.L2539:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC167
	.word	.LANCHOR1+1616
	.word	.LC0
	.word	.LC168
	.word	1145785929
	.word	.LANCHOR3
	.word	.LC169
	.word	.LANCHOR0+724
	.word	.LC170
	.word	.LC171
	.fnend
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_wait_write_queue_completed
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L2546
.L2542:
	ldrb	r3, [r4, #2263]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2545
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2542
.L2545:
	ldmfd	sp!, {r4, pc}
.L2547:
	.align	2
.L2546:
	.word	.LANCHOR0
	.fnend
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
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
	ldr	r3, .L2552
	mov	r7, r1
	mov	r6, r2
	mvn	r2, #0
	mov	r5, r0
	ldrb	r1, [r3, #676]	@ zero_extendqisi2
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
.L2553:
	.align	2
.L2552:
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
	ldr	r10, .L2582
	mov	r8, r0
	mov	r6, r1
	mov	r4, r0
	mov	r5, r1
.L2555:
	cmp	r5, #0
	beq	.L2578
	ldrb	fp, [r4]	@ zero_extendqisi2
	ldr	r9, [r4, #24]
.L2556:
	mov	r0, r9
	mov	r1, #0
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2580
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2556
.L2580:
	cmp	r5, #1
	beq	.L2562
	ldr	r3, .L2582+4
	ldrb	r3, [r3, #-3240]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2562
	ldr	r3, .L2582
	mvn	lr, #0
	ldrb	r2, [r3, #676]	@ zero_extendqisi2
	rsb	r3, r2, #24
	mov	r3, r9, lsr r3
	bic	lr, r3, lr, asl r2
	uxth	r3, lr
	str	r3, [sp, #4]
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2561
	ldr	r1, .L2582+8
	movw	r2, #661
	ldr	r0, .L2582+12
	bl	printk
	bl	dump_stack
.L2561:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	ldrb	r0, [r10, #676]	@ zero_extendqisi2
	mvn	ip, #0
	mul	r3, r2, r3
	rsb	r2, r0, #24
	add	r7, r10, r3
	ldr	r1, [r7, #748]
	mov	r2, r1, lsr r2
	bic	r2, r2, ip, asl r0
	ldr	r0, [sp, #4]
	uxth	r2, r2
	cmp	r0, r2
	bne	.L2562
	ldr	r2, .L2582+16
	ldrh	r2, [r2]
	add	r9, r2, r9
	cmp	r1, r9
	bne	.L2562
	ldr	r0, [r4, #24]
	mvn	r9, #0
	str	r3, [sp, #12]
	add	r5, r5, r9
	ldrb	fp, [r7, #724]	@ zero_extendqisi2
	bl	flash_start_plane_read
	mov	ip, #2
	mov	r2, #0
	strb	ip, [r4, #42]
	mov	r1, r4
	strb	r2, [r4, #43]
	ldr	r0, .L2582+20
	strb	r9, [r4]
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	bl	buf_add_tail
	ldr	r1, .L2582+24
	strb	r9, [r7, #724]
	ldr	r0, .L2582+20
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	strb	ip, [r7, #766]
	strb	r2, [r7, #767]
	add	r1, r1, r3
	bl	buf_add_tail
	b	.L2560
.L2562:
	mov	r0, r4
	bl	queue_read_cmd
.L2560:
	subs	r5, r5, #1
	ldrne	r3, .L2582+24
	movne	r4, #48
	mlane	r4, r4, fp, r3
	bne	.L2555
.L2578:
	ldr	r4, .L2582+24
	mov	r5, #48
.L2566:
	cmp	r6, #0
	beq	.L2581
	ldrb	r3, [r8, #42]	@ zero_extendqisi2
	cmp	r3, #13
	bne	.L2567
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r6, r6, #1
	cmp	r3, #255
	mlane	r8, r5, r3, r4
.L2567:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2566
.L2581:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2583:
	.align	2
.L2582:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1648
	.word	.LC0
	.word	.LANCHOR0+2994
	.word	.LANCHOR0+2263
	.word	.LANCHOR0+724
	.fnend
	.size	sblk_read_page, .-sblk_read_page
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r10, r3
	ldr	r6, .L2596
	mov	fp, r2
	add	r8, r0, #8
	mov	r5, r0
	mov	r7, r1
	ubfx	r9, r1, #24, #2
	ldrb	r3, [r6, #676]	@ zero_extendqisi2
	ldr	r4, [r6, #528]
	mvn	r6, #0
	rsb	r3, r3, #24
	bic	r6, r1, r6, asl r3
	ldr	r3, .L2596+4
	add	r8, r4, r8, asl #8
	ldr	r2, [r3]
	str	r3, [sp, #4]
	tst	r2, #16
	beq	.L2585
	ldr	r0, .L2596+8
	mov	r2, r9
	ldr	r3, [sp, #48]
	bl	printk
.L2585:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	hynix_reconfig_rr_para
	mov	r0, r5
	bl	nandc_cs
	cmp	r9, #0
	mov	r0, r5
	bne	.L2586
	bl	zftl_flash_enter_slc_mode
	b	.L2587
.L2586:
	bl	zftl_flash_exit_slc_mode
.L2587:
	add	r4, r4, r5, asl #8
	mov	r3, #128
	mov	r0, r6
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r6
	str	r3, [r4, #2052]
	mov	r3, r6, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r6, lsr #16
	str	r3, [r4, #2052]
	bl	nandc_set_seed
	mov	r2, fp
	mov	r0, #1
	ldr	r3, [sp, #4]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	ldr	r3, .L2596+12
	ldr	r0, .L2596+16
	ldr	r3, [r3, #4]
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	r0, r8
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r5
	bl	nandc_de_cs
	ands	r4, r4, #4
	beq	.L2588
	ldr	r0, .L2596+20
	mov	r1, r7
	mov	r2, r4
	bl	printk
.L2588:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2597:
	.align	2
.L2596:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC172
	.word	arm_delay_ops
	.word	128849000
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
	ldr	r4, .L2609
	mov	r5, r1
	mov	r2, #32
	mov	r1, #165
	ldr	r0, [r4, #-3272]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r4, #-3264]
	bl	ftl_memset
	ldr	r3, .L2609+4
	mov	r0, r6
	ldrh	r3, [r3, #2]
	mul	r5, r3, r5
	mov	r1, r5
	bl	flash_erase_block
	cmn	r0, #1
	bne	.L2599
.L2601:
	mvn	r4, #0
	b	.L2600
.L2599:
	ldr	r7, .L2609+8
	mov	r0, r6
	mov	r1, r5
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r2, [r4, #-3272]
	ldr	r3, [r4, #-3264]
	bl	flash_prog_page
	cmn	r0, #1
	beq	.L2601
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r6
	mov	r1, r5
	str	r3, [sp]
	ldr	r2, [r4, #-3272]
	ldr	r3, [r4, #-3264]
	bl	flash_read_page
	cmn	r0, #1
	beq	.L2601
	ldr	r3, [r4, #-3272]
	ldr	r2, [r3]
	ldr	r3, .L2609+12
	cmp	r2, r3
	bne	.L2601
	ldr	r3, [r4, #-3264]
	ldr	r4, .L2609+16
	ldr	r3, [r3]
	subs	r4, r3, r4
	mvnne	r4, #0
.L2600:
	mov	r0, r6
	mov	r1, r5
	bl	flash_erase_block
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2610:
	.align	2
.L2609:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1515870811
	.word	1515870810
	.fnend
	.size	flash_test_blk, .-flash_test_blk
	.align	2
	.global	flash_dual_page_prog
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r3
	ldr	fp, .L2626
	add	r3, r0, #8
	ldr	r7, .L2626+4
	mvn	r5, #0
	mov	r6, r0
	mov	r8, r1
	ldr	r4, [fp, #528]
	mov	r10, r2
	add	r3, r4, r3, asl #8
	str	r3, [sp]
	ldrb	r3, [fp, #676]	@ zero_extendqisi2
	ubfx	fp, r1, #24, #2
	rsb	r3, r3, #24
	bic	r5, r1, r5, asl r3
	ldr	r3, [r7]
	tst	r3, #16
	beq	.L2612
	ldr	r0, .L2626+8
	mov	r2, fp
	ldr	r3, [sp, #56]
	bl	printk
.L2612:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r6
	bne	.L2613
	bl	zftl_flash_enter_slc_mode
	b	.L2614
.L2613:
	bl	zftl_flash_exit_slc_mode
.L2614:
	add	r4, r4, r6, asl #8
	mov	ip, #128
	mov	fp, #0
	uxtb	r3, r5
	str	ip, [r4, #2056]
	mov	r0, r5
	str	fp, [r4, #2052]
	str	fp, [r4, #2052]
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r5, lsr #16
	str	r3, [r4, #2052]
	str	ip, [sp, #4]
	bl	nandc_set_seed
	ldrb	r1, [r7, #13]	@ zero_extendqisi2
	mov	r2, r10
	mov	r3, r9
	mov	r0, #1
	bl	nandc_xfer_start
	mov	r9, #16
	bl	nandc_xfer_done
	str	r9, [r4, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	add	r0, r5, #1
	uxtb	r3, r0
	ldr	ip, [sp, #4]
	str	ip, [r4, #2056]
	str	fp, [r4, #2052]
	str	fp, [r4, #2052]
	str	r3, [r4, #2052]
	mov	r3, r0, lsr #8
	str	r3, [r4, #2052]
	mov	r3, r0, lsr r9
	str	r3, [r4, #2052]
	bl	nandc_set_seed
	ldrb	r1, [r7, #13]	@ zero_extendqisi2
	mov	r0, #1
	ldr	r2, [sp, #48]
	ldr	r3, [sp, #52]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	r9, [r4, #2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r0, [sp]
	bl	flash_read_status
	mov	r4, r0
	mov	r0, r6
	bl	nandc_de_cs
	ands	r4, r4, #4
	beq	.L2615
	ldr	r3, .L2626+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2615
	ldr	r0, .L2626+12
	mov	r1, r8
	mov	r2, r4
	bl	printk
.L2615:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2627:
	.align	2
.L2626:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC172
	.word	.LC173
	.fnend
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.29, %function
fw_flash_page_prog.constprop.29:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #16
	mov	r6, r1
	ldr	r4, .L2633
	mov	r5, r2
	ldr	r3, [r4, #584]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrb	r8, [r4, #660]	@ zero_extendqisi2
	mov	r7, r0
	ldrb	r0, [r4, #588]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r3, .L2633+4
	ldrb	r3, [r3, #11]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2629
	ldr	r4, .L2633+8
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r4, #-3272]
	bl	ftl_memset
	ldr	r3, [r4, #-3272]
	mov	r1, r7
	mov	r2, r6
	mov	r0, #0
	str	r3, [sp]
	str	r3, [sp, #4]
	mov	r3, #4
	str	r3, [sp, #8]
	mov	r3, r5
	bl	flash_dual_page_prog
	b	.L2632
.L2629:
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
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2634:
	.align	2
.L2633:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.fnend
	.size	fw_flash_page_prog.constprop.29, .-fw_flash_page_prog.constprop.29
	.text
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mvn	r5, #0
	ldr	r7, .L2646
	mov	fp, r2
	mov	r9, r0
	mov	r8, r1
	mov	r10, r3
	ldrb	r6, [r7, #676]	@ zero_extendqisi2
	ldrb	r2, [r7, #589]	@ zero_extendqisi2
	rsb	ip, r6, #24
	mvn	r6, r5, asl r6
	and	r4, r6, r1, lsr ip
	uxtb	r4, r4
	cmp	r2, r4
	bhi	.L2636
	ldr	r1, .L2646+4
	movw	r2, #821
	ldr	r0, .L2646+8
	str	ip, [sp]
	bl	printk
	bl	dump_stack
	ldr	ip, [sp]
.L2636:
	ldrb	r1, [r7, #589]	@ zero_extendqisi2
	ldr	r2, .L2646
	cmp	r1, r4
	bls	.L2635
	ldr	r3, [r2, #528]
	add	r1, r2, r4
	mvn	r5, #0
	str	r2, [sp, #4]
	bic	r5, r8, r5, asl ip
	ldrb	r6, [r1, #668]	@ zero_extendqisi2
	str	r3, [sp]
	bl	nandc_rdy_status
	cmp	r0, #0
	ldr	r2, [sp, #4]
	bne	.L2638
	ldrb	r2, [r2, #589]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2639
	bl	nandc_wait_flash_ready
	b	.L2638
.L2639:
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	bl	flash_wait_device_ready_raw
.L2638:
	mov	r0, r6
	bl	hynix_reconfig_rr_para
	mov	r0, r6
	bl	nandc_cs
	tst	r8, #50331648
	bne	.L2640
	mov	r0, r5
	bl	slc_phy_page_address_calc
	ldrb	r3, [r7]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r5, r0
	beq	.L2641
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	b	.L2641
.L2640:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2641:
	ldr	r3, [sp]
	mov	r0, r5
	add	r4, r3, r6, asl #8
	mov	r3, #128
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
	ldr	r3, .L2646+12
	mov	r2, fp
	mov	r0, #1
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	mov	r3, r10
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	ldr	r3, .L2646+16
	ldr	r0, .L2646+20
	ldr	r3, [r3, #4]
	blx	r3
	str	r9, [r4, #2056]
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L2635:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2647:
	.align	2
.L2646:
	.word	.LANCHOR0
	.word	.LANCHOR1+1664
	.word	.LC0
	.word	.LANCHOR2
	.word	arm_delay_ops
	.word	128849000
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
	ldr	r2, .L2657
	ldr	r0, [r4, #24]
	ldrb	r3, [r2, #2263]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2649
	ldrb	ip, [r2, #676]	@ zero_extendqisi2
	mvn	r1, #0
	mov	r5, #48
	rsb	r6, ip, #24
	mvn	r1, r1, asl ip
	uxth	r1, r1
	and	r0, r1, r0, asr r6
.L2651:
	mla	ip, r5, r3, r2
	ldr	lr, [ip, #748]
	and	lr, r1, lr, lsr r6
	cmp	r0, lr
	bne	.L2650
	add	lr, ip, #764
	ldrb	ip, [ip, #766]	@ zero_extendqisi2
	cmp	ip, #7
	moveq	r3, #3
	streqb	r3, [lr, #2]
	beq	.L2649
.L2650:
	mla	r3, r5, r3, r2
	ldrb	r3, [r3, #724]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2651
.L2649:
	mov	r3, #3
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #1
	ldr	r0, .L2657+4
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	buf_add_tail
.L2658:
	.align	2
.L2657:
	.word	.LANCHOR0
	.word	.LANCHOR0+2263
	.fnend
	.size	queue_prog_cmd, .-queue_prog_cmd
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
	beq	.L2660
	ldr	r3, .L2688
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L2660
	ldr	r0, .L2688+4
	mov	r2, r5
	ldr	r1, [r4, #24]
	bl	printk
.L2660:
	ldr	r8, .L2688+8
	mov	r6, #0
	mov	r9, r8
.L2661:
	cmp	r5, #0
	beq	.L2685
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r7, [r4, #24]
	str	r3, [sp, #4]
.L2662:
	mov	r0, r7
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2687
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2662
.L2687:
	cmp	r5, #1
	beq	.L2664
	ldrb	r3, [r8, #721]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2665
.L2664:
	mov	r0, r4
	bl	queue_prog_cmd
	b	.L2666
.L2665:
	ldrb	r3, [r8, #676]	@ zero_extendqisi2
	mvn	fp, #0
	rsb	ip, r3, #24
	mov	ip, r7, lsr ip
	bic	fp, ip, fp, asl r3
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	uxth	fp, fp
	bne	.L2667
	ldr	r1, .L2688+12
	movw	r2, #583
	ldr	r0, .L2688+16
	bl	printk
	bl	dump_stack
.L2667:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	mov	r10, #1
	mla	r3, r2, r3, r9
	ldrb	r2, [r9, #676]	@ zero_extendqisi2
	ldr	ip, [r3, #748]
	rsb	r3, r2, #24
	mov	r2, r10, asl r2
	sub	r2, r2, #1
	and	r2, r2, ip, lsr r3
	uxth	r2, r2
	cmp	fp, r2
	bne	.L2668
	ldr	r2, .L2688+20
	ldrb	lr, [r8, #2930]	@ zero_extendqisi2
	ldrh	fp, [r2]
	sub	lr, lr, #1
	rsb	r3, fp, r3
	uxth	r1, lr
	mov	r3, r10, asl r3
	mov	r0, r10, asl fp
	sub	r3, r3, #1
	and	r2, r1, r7, lsr fp
	sub	r0, r0, #1
	and	r1, r1, ip, lsr fp
	uxth	r3, r3
	and	r2, r2, r3
	and	r3, r3, r1
	subs	fp, r2, r3
	uxth	r0, r0
	and	r7, r7, r0
	and	ip, ip, r0
	movne	fp, #1
	cmp	r7, ip
	movne	fp, #0
	cmp	fp, #0
	beq	.L2669
	cmp	r6, lr
	beq	.L2668
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
	ldr	r0, .L2688+24
	strb	r3, [r4]
	bl	buf_add_tail
	b	.L2666
.L2669:
	mov	r0, r4
	mov	r6, fp
	bl	queue_prog_cmd
	b	.L2666
.L2668:
	mov	r0, r4
	mov	r6, #0
	bl	queue_prog_cmd
.L2666:
	subs	r5, r5, #1
	ldrne	r3, .L2688+28
	movne	r4, #48
	ldrne	r2, [sp, #4]
	mlane	r4, r4, r2, r3
	bne	.L2661
.L2685:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2689:
	.align	2
.L2688:
	.word	.LANCHOR2
	.word	.LC174
	.word	.LANCHOR0
	.word	.LANCHOR1+1688
	.word	.LC0
	.word	.LANCHOR0+2928
	.word	.LANCHOR0+2263
	.word	.LANCHOR0+724
	.fnend
	.size	sblk_prog_page, .-sblk_prog_page
	.align	2
	.global	ftl_flush
	.type	ftl_flush, %function
ftl_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r4, .L2696
	ldr	r5, .L2696+4
	ldrb	r1, [r4, #2285]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2691
	ldrb	r2, [r5, #-3239]	@ zero_extendqisi2
	add	r3, r4, #724
	mov	r0, #48
	mla	r0, r0, r2, r3
	bl	sblk_prog_page
.L2691:
	mvn	r3, #0
	strb	r3, [r5, #-3239]
	mov	r3, #0
	strb	r3, [r4, #2285]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_vpn_decrement
.L2697:
	.align	2
.L2696:
	.word	.LANCHOR0
	.word	.LANCHOR3
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
	ldr	r3, .L2703
	ldrb	r3, [r3, #2285]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, pc}
	bl	timer_get_time
	ldr	r3, .L2703+4
	ldr	r3, [r3, #-3236]
	add	r3, r3, #100
	cmp	r0, r3
	ldmlsfd	sp!, {r3, pc}
	ldmfd	sp!, {r3, lr}
	b	ftl_flush
.L2704:
	.align	2
.L2703:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	.fnstart
	@ args = 12, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r3
	ldr	r3, .L2713
	mov	r7, r2
	mov	fp, r0
	mov	r6, r1
	ldr	r5, [sp, #40]
	ldrb	r2, [r3, #589]	@ zero_extendqisi2
	mov	r10, r3
	ldr	r9, [sp, #44]
	cmp	r2, r4
	ldr	r8, [sp, #48]
	bhi	.L2706
	ldr	r1, .L2713+4
	mov	r2, #740
	ldr	r0, .L2713+8
	bl	printk
	bl	dump_stack
.L2706:
	ldrb	r2, [r10, #589]	@ zero_extendqisi2
	ldr	r3, .L2713
	cmp	r2, r4
	ldmlsfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	add	r4, r3, r4
	ldrb	r10, [r4, #668]	@ zero_extendqisi2
	ldr	r4, [r3, #528]
	mov	r0, r10
	bl	nandc_cs
	cmp	fp, #0
	mov	r3, r10, asl #8
	addne	r2, r4, r3
	add	r4, r4, r3
	mov	r3, #128
	strne	fp, [r2, #2056]
	str	r6, [r4, #2056]
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
	add	r0, r0, r6
	bl	nandc_set_seed
	ldr	r3, .L2713+12
	mov	r0, #1
	mov	r2, r9
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	mov	r3, r8
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	r7, [r4, #2056]
	mov	r0, r10
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	nandc_de_cs
.L2714:
	.align	2
.L2713:
	.word	.LANCHOR0
	.word	.LANCHOR1+1704
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
	mov	r7, #1
	ldr	r8, .L2717
	mov	r6, r0
	ldr	r3, [r0]
	ldrb	r4, [r8, #676]	@ zero_extendqisi2
	ldr	r1, [r3, #24]
	rsb	r2, r4, #24
	mov	r4, r7, asl r4
	mov	r5, r7, asl r2
	sub	r4, r4, #1
	sub	r5, r5, #1
	ldrb	r0, [r3, #44]	@ zero_extendqisi2
	and	r5, r5, r1
	and	r4, r4, r1, lsr r2
	str	r5, [sp]
	mov	r1, r7
	ldr	r2, [r3, #4]
	uxtb	r4, r4
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
	ldr	r1, [r6]
	add	r0, r8, #2256
	mov	r3, #4
	add	r0, r0, #7
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r7, [r1, #43]
	strb	r3, [r1]
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2718:
	.align	2
.L2717:
	.word	.LANCHOR0
	.fnend
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
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
.L2720:
	mov	r0, r6
	mov	r1, #1
	bl	queue_lun_state
	subs	r5, r0, #0
	beq	.L2723
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2720
.L2723:
	mov	r0, r4
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
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r5, r0
	ldr	r3, [r0]
	mov	r8, r1
	mov	r9, r2
	ldr	r4, [r3, #24]
.L2725:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2730
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2725
.L2730:
	cmp	r9, #2
	bne	.L2727
	ldr	r2, .L2731
	mov	r6, #1
	ldr	r3, [r5]
	ldr	ip, [r8]
	ldrb	r4, [r2, #676]	@ zero_extendqisi2
	ldr	r0, [r3, #24]
	rsb	r1, r4, #24
	ldr	r7, [ip, #24]
	mov	r2, r6, asl r1
	mov	r4, r6, asl r4
	sub	r2, r2, #1
	sub	r4, r4, #1
	and	r10, r2, r0
	and	r7, r7, r2
	and	r4, r4, r0, lsr r1
	ldrb	r0, [r3, #44]	@ zero_extendqisi2
	str	r10, [sp]
	mov	r1, r6
	ldr	r2, [r3, #4]
	uxtb	r4, r4
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r8]
	ldr	r2, [r5]
	mov	r1, r6
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r7, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldmia	r5, {r2, r3}
	mov	r1, r9
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r10, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r8, #4]
	ldr	r2, [r5]
	mov	r1, r9
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r7, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #26
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #8]
	ldr	r2, [r5]
	mov	r1, #3
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r10, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #17
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	r3, [r8, #8]
	ldr	r2, [r5]
	mov	r1, #3
	ldrb	r0, [r2, #44]	@ zero_extendqisi2
	str	r7, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	mov	r2, #16
	ldr	r3, [r3, #12]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	ldr	r1, [r5]
	mov	r3, #5
	ldr	r0, .L2731+4
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r6, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2728
.L2727:
	mov	r0, r5
	bl	queue_tlc_prog_cmd
.L2728:
	mov	r0, #0
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2732:
	.align	2
.L2731:
	.word	.LANCHOR0
	.word	.LANCHOR0+2263
	.fnend
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2735
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r7, #1
	ldrb	r4, [r3, #676]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r2, [r0, #24]
	.pad #20
	sub	sp, sp, #20
	rsb	r3, r4, #24
	mov	r4, r7, asl r4
	mov	r6, r7, asl r3
	sub	r4, r4, #1
	sub	r6, r6, #1
	and	r4, r4, r2, lsr r3
	and	r6, r6, r2
	mov	r3, r2
	ldr	r0, .L2735+4
	uxtb	r4, r4
	mov	r1, r6
	bl	printk
	ldr	r3, [r5, #4]
	mov	r1, r7
	mov	r2, #26
	mov	r0, #0
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r1, #2
	mov	r2, #26
	mov	r0, #0
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r3, [r5, #4]
	mov	r1, #3
	mov	r2, #16
	mov	r0, #0
	str	r3, [sp, #4]
	ldr	r3, [r5, #12]
	str	r6, [sp]
	str	r3, [sp, #8]
	mov	r3, r4
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r0, [r5, #24]
	mov	r1, #64
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, lr}
	b	flash_wait_device_ready
.L2736:
	.align	2
.L2735:
	.word	.LANCHOR0
	.word	.LC175
	.fnend
	.size	sblk_3d_tlc_dump_prog, .-sblk_3d_tlc_dump_prog
	.align	2
	.global	flash_start_3d_mlc_page_prog
	.type	flash_start_3d_mlc_page_prog, %function
flash_start_3d_mlc_page_prog:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r8, r3
	ldr	r3, .L2741
	mov	r5, r2
	mov	r7, r0
	mov	r4, r1
	ldrb	r2, [r3, #589]	@ zero_extendqisi2
	mov	r6, r3
	cmp	r2, r1
	bhi	.L2738
	ldr	r1, .L2741+4
	mov	r2, #772
	ldr	r0, .L2741+8
	bl	printk
	bl	dump_stack
.L2738:
	ldrb	r2, [r6, #589]	@ zero_extendqisi2
	ldr	r3, .L2741
	cmp	r2, r4
	ldmlsfd	sp!, {r4, r5, r6, r7, r8, pc}
	add	r4, r3, r4
	ldr	r6, [r3, #528]
	ldrb	r4, [r4, #668]	@ zero_extendqisi2
	mov	r0, r4
	add	r4, r6, r4, asl #8
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
	ldr	r3, .L2741+12
	mov	r0, #1
	mov	r2, r8
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	r7, [r4, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2742:
	.align	2
.L2741:
	.word	.LANCHOR0
	.word	.LANCHOR1+1732
	.word	.LC0
	.word	.LANCHOR2
	.fnend
	.size	flash_start_3d_mlc_page_prog, .-flash_start_3d_mlc_page_prog
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
	ldr	r9, .L2751
	mov	r7, r1
	mov	r8, #1
.L2744:
	cmp	r7, #0
	beq	.L2749
	ldr	r3, [r5, #-8]
	ldr	r4, [r3, #24]
.L2745:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2750
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2745
.L2750:
	ldr	r3, [r5, #-8]
	sub	r7, r7, #1
	ldrb	r4, [r9, #676]	@ zero_extendqisi2
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
	ldr	r0, .L2751+4
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r8, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2744
.L2749:
	mov	r0, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2752:
	.align	2
.L2751:
	.word	.LANCHOR0
	.word	.LANCHOR0+2263
	.fnend
	.size	sblk_3d_mlc_prog_pages, .-sblk_3d_mlc_prog_pages
	.align	2
	.global	sblk_mlc_dump_prog
	.type	sblk_mlc_dump_prog, %function
sblk_mlc_dump_prog:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2755
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	mov	r4, #1
	ldrb	r1, [r3, #676]	@ zero_extendqisi2
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
	ldr	r2, [r5, #24]
	mov	r1, r6
	ldr	r0, .L2755+4
	add	r3, r2, #1
	bl	printk
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
.L2756:
	.align	2
.L2755:
	.word	.LANCHOR0
	.word	.LC176
	.fnend
	.size	sblk_mlc_dump_prog, .-sblk_mlc_dump_prog
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
	ldr	r6, .L2787
	ldrb	r3, [sp, #68]	@ zero_extendqisi2
	mov	r7, r0
	mov	r5, r1
	mov	r9, r2
	ubfx	r4, r1, #24, #2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #589]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2758
	ldr	r1, .L2787+4
	movw	r2, #554
	ldr	r0, .L2787+8
	bl	printk
	bl	dump_stack
.L2758:
	ldrb	r2, [r6, #589]	@ zero_extendqisi2
	ldr	r3, .L2787
	cmp	r2, r7
	mvnls	r0, #0
	bls	.L2759
	add	r2, r3, r7
	cmp	r4, #0
	ldrb	r10, [r2, #668]	@ zero_extendqisi2
	bne	.L2771
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2761
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2771
.L2761:
	ldrh	fp, [r6, #2]
	mov	r0, r5
	mov	r1, fp
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L2787
	mul	r0, fp, r0
	rsb	r4, r0, r5
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
	b	.L2760
.L2771:
	mov	r4, r5
.L2760:
	ldr	fp, .L2787+12
	ldr	r6, .L2787+16
.L2766:
	ldr	r3, [fp]
	tst	r3, #16
	beq	.L2763
	ldr	r0, .L2787+20
	mov	r1, r10
	mov	r2, r5
	mov	r3, r4
	bl	printk
.L2763:
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
	beq	.L2764
	ldr	r3, [sp, #64]
	mov	r1, r5
	str	r0, [sp, #20]
	mov	r0, r7
	str	r3, [sp]
	ldr	r3, [r6, #-3244]
	ldr	r2, [r6, #-3248]
	bl	flash_read_page_en
	sub	r3, r0, #512
	cmp	r0, #512
	cmnne	r0, #1
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp, #12]
	beq	.L2765
	ldr	r3, [r6, #-3248]
	ldr	r2, [r9]
	ldr	ip, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2765
	ldr	r3, [r6, #-3244]
	ldr	r2, [r8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2764
.L2765:
	mov	r2, #4
	ldr	r0, .L2787+24
	mov	r3, r2
	mov	r1, r9
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2787+28
	mov	r1, r8
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2787+32
	ldr	r1, [r6, #-3244]
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2787+36
	ldr	r1, [r6, #-3248]
	bl	rknand_print_hex
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L2766
.L2768:
	mov	r1, r5
	ldr	r0, .L2787+40
	bl	printk
	ldr	r1, .L2787+4
	movw	r2, #587
	ldr	r0, .L2787+8
	bl	printk
	bl	dump_stack
	mvn	ip, #0
	b	.L2767
.L2764:
	cmn	ip, #1
	beq	.L2768
.L2767:
	mov	r0, ip
.L2759:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2788:
	.align	2
.L2787:
	.word	.LANCHOR0
	.word	.LANCHOR1+1764
	.word	.LC0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC177
	.word	.LC178
	.word	.LC179
	.word	.LC180
	.word	.LC181
	.word	.LC182
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
	bne	.L2790
	ldr	r1, .L2792
	movw	r2, #2324
	ldr	r0, .L2792+4
	bl	printk
	bl	dump_stack
	ldr	r0, .L2792+8
	mov	r1, r5
	bl	printk
.L2790:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2793:
	.align	2
.L2792:
	.word	.LANCHOR1+1784
	.word	.LC0
	.word	.LC182
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
	ldr	r5, .L2832
	.pad #20
	sub	sp, sp, #20
	mov	fp, r0
	ldr	r0, .L2832+4
	ldr	r7, .L2832+8
	mov	r10, #0
	ldrb	r2, [r5, #2260]	@ zero_extendqisi2
	mov	r6, r7
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, [r5, #2288]
	ldrh	r2, [r3, #74]
	cmp	r2, #0
	movne	r2, #0
	strneh	r2, [r3, #150]	@ movhi
.L2806:
	ldr	r3, .L2832+12
	ldr	lr, [r5, #2288]
	ldr	r8, .L2832+16
	ldrh	r3, [r3]
	ldrb	r4, [r7, #-2936]	@ zero_extendqisi2
	ldrh	ip, [r8, #2]
	str	r3, [sp, #8]
	ldr	r3, [lr, #4]
	ldr	r9, .L2832+12
	add	r3, r3, #1
	str	r3, [lr, #4]
	str	r3, [r7, #-3188]
	ldr	r3, .L2832+20
	str	fp, [r7, #-3192]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2796
	mov	r2, ip
	ldr	r0, .L2832+24
	mov	r1, r4
	ldr	r3, [lr, #4]
	str	ip, [sp, #12]
	bl	printk
	ldr	ip, [sp, #12]
.L2796:
	ldr	r3, .L2832+28
	ldrh	r0, [r8, #2]
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L2797
.L2802:
	ldrb	r3, [r6, #-2935]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r6, #-2935]
	cmp	r3, #7
	bls	.L2798
	mov	r3, #8
.L2801:
	ldr	r2, [r5, #664]
	sub	r4, r3, #8
	add	r2, r2, r3
	uxth	r4, r4
	ldrb	r1, [r2, #32]	@ zero_extendqisi2
	add	r2, r1, #127
	uxtb	r2, r2
	cmp	r2, #125
	bhi	.L2799
	ldr	r1, .L2832+32
	movw	r2, #751
	ldr	r0, .L2832+36
	bl	printk
	bl	dump_stack
	b	.L2800
.L2799:
	cmp	r1, #255
	bne	.L2800
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L2801
	mov	r4, #8
.L2800:
	mov	r10, #1
	strb	r4, [r6, #-2935]
.L2798:
	ldr	r3, [r5, #664]
	ldrb	r2, [r6, #-2935]	@ zero_extendqisi2
	add	r3, r3, r2
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	strb	r3, [r6, #-2936]
	beq	.L2802
	ldrh	r4, [r9]
	mov	r0, #0
	mul	r4, r4, r3
	mov	r1, r4
	bl	flash_erase_block
	ldr	r3, .L2832
	ldr	r2, .L2832+40
	mov	r1, r4
	mov	r0, #0
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	str	r3, [sp]
	sub	r3, r2, #320
	bl	ftl_prog_page
	ldr	r3, .L2832+16
	mov	r2, #1
	add	r4, r4, r2
	strh	r2, [r3, #2]	@ movhi
	b	.L2803
.L2797:
	ldr	r3, [sp, #8]
	cmp	r0, #0
	mla	r4, r3, r4, ip
	bne	.L2803
	mov	r1, r4
	bl	flash_erase_block
.L2803:
	ldrb	r3, [r5, #2260]	@ zero_extendqisi2
	mov	r0, #0
	ldr	r2, .L2832+40
	mov	r1, r4
	str	r3, [sp]
	sub	r3, r2, #320
	bl	ftl_prog_page
	ldrh	r3, [r8, #2]
	add	r3, r3, #1
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, .L2832+44
	cmn	r0, #1
	beq	.L2804
	ldrb	r2, [r3, #-2856]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2805
.L2804:
	mov	r2, #0
	strb	r2, [r3, #-2856]
	b	.L2806
.L2805:
	cmp	r10, #1
	beq	.L2807
.L2815:
	ldr	r3, .L2832+8
	ldrb	r3, [r3, #-2936]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2809
	ldr	r1, .L2832+32
	movw	r2, #787
	ldr	r0, .L2832+36
	bl	printk
	bl	dump_stack
	b	.L2809
.L2807:
	ldr	r3, .L2832+8
	ldrb	r4, [r3, #-2935]	@ zero_extendqisi2
	add	r4, r4, #1
.L2810:
	cmp	r4, #7
	bhi	.L2815
	ldr	r3, [r5, #664]
	add	r3, r3, r4
	ldrb	r6, [r3, #40]	@ zero_extendqisi2
	add	r3, r6, #127
	uxtb	r3, r3
	cmp	r3, #125
	bhi	.L2811
	ldr	r1, .L2832+32
	mov	r2, #780
	ldr	r0, .L2832+36
	bl	printk
	bl	dump_stack
	b	.L2812
.L2811:
	cmp	r6, #255
	beq	.L2813
.L2812:
	ldrh	r1, [r9]
	mov	r0, #0
	mul	r1, r1, r6
	bl	flash_erase_block
.L2813:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2810
.L2809:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2833:
	.align	2
.L2832:
	.word	.LANCHOR0
	.word	.LANCHOR3-3192
	.word	.LANCHOR3
	.word	.LANCHOR0+2994
	.word	.LANCHOR3-2936
	.word	.LANCHOR2
	.word	.LC183
	.word	.LANCHOR0+2960
	.word	.LANCHOR1+1800
	.word	.LC0
	.word	.LANCHOR3-2872
	.word	.LANCHOR4
	.fnend
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2866
	mov	r2, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, #0
	ldr	r4, .L2866+4
	mov	r1, r7
	strb	r2, [r3, #-2856]
	.pad #20
	sub	sp, sp, #20
	strb	r2, [r3, #-2854]
	mov	r2, #16384
	strb	r7, [r3, #-2855]
	add	r3, r4, #564
	ldr	r0, .L2866+8
	mov	r6, #7
	ldrh	r3, [r3]
	mov	r9, r4
	ldr	r5, .L2866+12
	str	r0, [r4, #568]
	add	r3, r0, r3, asl #2
	str	r3, [r4, #2288]
	bl	ftl_memset
	ldr	r0, .L2866+16
	mov	r1, r7
	mov	r2, #16384
	bl	ftl_memset
	ldr	r3, [r4, #664]
	ldr	r10, .L2866+20
	ldr	fp, .L2866+24
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	strb	r7, [r5, #-2935]
	strb	r3, [r5, #-2936]
	ldr	r3, .L2866+28
	strh	r7, [r3, #2]	@ movhi
	str	r3, [sp, #8]
.L2839:
	ldr	r3, [r4, #664]
	uxth	r7, r6
	add	r3, r3, r6
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2835
	ldrh	ip, [r10]
	mov	r0, #0
	ldr	r2, .L2866+8
	mul	ip, ip, r3
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	str	r3, [sp]
	sub	r3, r2, #320
	mov	r1, ip
	str	ip, [sp, #12]
	bl	ftl_read_page
	cmn	r0, #1
	mov	r8, r0
	ldr	ip, [sp, #12]
	bne	.L2836
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	mov	r0, #0
	ldr	r2, .L2866+8
	add	r1, ip, #1
	str	r3, [sp]
	sub	r3, r2, #320
	bl	ftl_read_page
	mov	r8, r0
.L2836:
	ldr	r3, .L2866+32
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2837
	ldr	r3, [r4, #2288]
	mov	r1, r6
	ldr	r0, .L2866+36
	mov	r2, r8
	ldr	r3, [r3]
	str	r3, [sp]
	mov	r3, #672
	bl	printk
.L2837:
	cmn	r8, #1
	beq	.L2835
	ldr	r3, [r4, #2288]
	ldr	r3, [r3]
	cmp	r3, fp
	beq	.L2848
.L2835:
	subs	r6, r6, #1
	bcs	.L2839
	movw	r8, #65535
	mov	r7, #0
	b	.L2838
.L2848:
	mov	r8, r7
.L2838:
	ldr	r6, .L2866+32
	sxth	r8, r8
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L2840
	ldr	r3, [r4, #2288]
	mov	r1, r8
	ldr	r0, .L2866+40
	mov	r2, #4800
	ldr	r3, [r3]
	bl	printk
.L2840:
	cmn	r8, #1
	bne	.L2841
	mov	r2, #16384
	ldr	r0, .L2866+8
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r4, #2288]
	ldr	r2, .L2866+24
	mov	r0, r8
	str	r2, [r3]
	ldr	r2, .L2866+44
	ldr	r3, [r4, #2288]
	str	r2, [r3, #12]
	b	.L2842
.L2841:
	ldr	r3, [r4, #664]
	mov	r0, #0
	ldr	r2, .L2866+8
	sxtah	r3, r3, r7
	strb	r7, [r5, #-2935]
	ldr	r10, .L2866+4
	ldrb	r1, [r3, #40]	@ zero_extendqisi2
	mov	r3, #4
	ldr	fp, .L2866+24
	strb	r1, [r5, #-2936]
	str	r3, [sp]
	sub	r3, r2, #320
	bl	flash_get_last_written_page
	movw	r2, #2994
	ldrb	r3, [r5, #-2936]	@ zero_extendqisi2
	mov	r5, #0
	ldrh	r7, [r4, r2]
	mul	r7, r7, r3
	uxth	r8, r0
	mov	r9, r0
	add	ip, r8, #1
	uxth	ip, ip
.L2843:
	rsb	r3, r5, r8
	tst	r3, #32768
	bne	.L2847
	ldrb	r3, [r4, #2260]	@ zero_extendqisi2
	add	r1, r9, r7
	ldr	r2, .L2866+8
	mov	r0, #0
	rsb	r1, r5, r1
	str	ip, [sp, #12]
	str	r3, [sp]
	sub	r3, r2, #320
	bl	ftl_read_page
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L2844
	ldr	r3, [r10, #2288]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L2844
.L2847:
	ldr	r3, [sp, #8]
	strh	ip, [r3, #2]	@ movhi
	bl	ftl_tmp_into_update
	ldr	r2, [r4, #2288]
	mov	r0, #0
	ldr	r3, [r2, #64]
	add	r3, r3, #1
	str	r3, [r2, #64]
	bl	ftl_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	ldr	r0, [r6]
	ands	r0, r0, #16384
	beq	.L2842
	ldr	r3, .L2866+4
	ldr	r0, .L2866+48
	ldr	r3, [r3, #2288]
	ldr	r1, [r3, #156]
	bl	printk
	mov	r0, #0
	b	.L2842
.L2844:
	add	r5, r5, #1
	b	.L2843
.L2842:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2867:
	.align	2
.L2866:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR3-2872
	.word	.LANCHOR3
	.word	.LANCHOR4-2792
	.word	.LANCHOR0+2994
	.word	1229739078
	.word	.LANCHOR3-2936
	.word	.LANCHOR2
	.word	.LC184
	.word	.LC185
	.word	393240
	.word	.LC186
	.fnend
	.size	ftl_info_blk_init, .-ftl_info_blk_init
	.align	2
	.global	ftl_ext_info_flush
	.type	ftl_ext_info_flush, %function
ftl_ext_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	bl	timer_get_time
	ldr	r3, .L2884
	ldr	r4, .L2884+4
	umull	r0, r1, r0, r3
	ldr	r3, [r4, #580]
	ldr	r0, [r3, #520]
	mov	r1, r1, lsr #5
	cmp	r1, r0
	ldrhi	r2, [r3, #12]
	rsbhi	r2, r0, r2
	addhi	r2, r2, r1
	strhi	r2, [r3, #12]
	bhi	.L2883
.L2869:
	bcs	.L2870
.L2883:
	str	r1, [r3, #520]
.L2870:
	mov	r0, #0
	ldr	r5, .L2884+8
	bl	ftl_total_vpn_update
	ldr	r7, .L2884+12
	mov	r6, r5
.L2874:
	ldr	r3, [r4, #2288]
	ldr	r2, [r3, #56]
	add	r2, r2, #1
	str	r2, [r3, #56]
	ldrh	r2, [r3, #140]
	ldrh	r3, [r7]
	cmp	r2, r3
	bcc	.L2872
	bl	ftl_ext_alloc_new_blk
.L2872:
	ldr	r3, [r4, #2288]
	ldrh	r2, [r3, #130]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2873
	ldr	r1, .L2884+16
	movw	r2, #1912
	ldr	r0, .L2884+20
	bl	printk
	bl	dump_stack
.L2873:
	ldr	r1, [r4, #2288]
	ldr	r8, .L2884+24
	ldrb	r2, [r4, #676]	@ zero_extendqisi2
	ldrh	r3, [r1, #130]
	ldrh	r0, [r8]
	rsb	r2, r2, #24
	ldrh	r8, [r8, #66]
	rsb	r2, r0, r2
	mvn	r0, #0
	mov	r9, r3, asr r2
	bic	r3, r3, r0, asl r2
	ldrh	r2, [r1, #140]
	mov	r1, #0
	sxth	r3, r3
	ldr	r0, .L2884+28
	ldr	r10, .L2884+32
	mla	r8, r8, r3, r2
	ldrb	r2, [r4, #2260]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r3, #0
	str	r3, [r10, #-3192]
	uxtb	r0, r9
	ldr	r3, [r4, #2288]
	mov	r1, r8
	ldr	r2, .L2884+36
	ldr	r3, [r3, #56]
	str	r3, [r10, #-3188]
	ldrb	r3, [r4, #2260]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, .L2884+28
	bl	ftl_prog_page
	ldr	r2, [r4, #2288]
	ldrh	r3, [r2, #140]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #140]	@ movhi
	cmp	r3, #1
	beq	.L2874
	cmn	r0, #1
	beq	.L2875
	ldrb	r0, [r5, #-2854]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L2876
.L2875:
.L2871:
	mov	r3, #0
	strb	r3, [r6, #-2854]
	b	.L2874
.L2876:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2885:
	.align	2
.L2884:
	.word	1374389535
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+2960
	.word	.LANCHOR1+1816
	.word	.LC0
	.word	.LANCHOR0+2928
	.word	.LANCHOR3-3192
	.word	.LANCHOR3
	.word	.LANCHOR4-2792
	.fnend
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r2, #0
	ldr	r4, .L2905
	mvn	r5, #0
	.pad #20
	sub	sp, sp, #20
	add	r3, r4, #3520
	add	r1, r4, #2928
	strh	r2, [r3]	@ movhi
	ldr	r3, [r4, #2288]
	ldrh	r1, [r1]
	ldrh	r2, [r3, #130]
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r1, r3
	mov	r6, r2, asr r3
	bic	r5, r2, r5, asl r3
	mov	r3, #4
	ldr	r2, .L2905+4
	uxtb	r9, r6
	str	r3, [sp]
	mov	r1, r5
	ldr	r3, .L2905+8
	mov	r0, r9
	bl	flash_get_last_written_page
	ldr	r3, .L2905+12
	ldr	r2, [r3]
	tst	r2, #4096
	mov	r7, r0
	str	r3, [sp, #8]
	beq	.L2887
	ldr	r3, [r4, #2288]
	uxth	r6, r6
	str	r6, [sp]
	movw	r2, #1956
	ldr	r0, .L2905+16
	ldrh	r3, [r3, #130]
	ldr	r1, .L2905+20
	str	r3, [sp, #4]
	mov	r3, r7
	bl	printk
.L2887:
	ldr	r10, .L2905
	mov	r8, #0
	ldr	fp, .L2905+24
	ldr	ip, .L2905+28
.L2888:
	uxth	r6, r7
	rsb	r3, r8, r6
	tst	r3, #32768
	bne	.L2893
	ldrh	r3, [fp]
	rsb	r1, r8, r7
	ldrb	r2, [r4, #2260]	@ zero_extendqisi2
	mov	r0, r9
	str	ip, [sp, #12]
	mla	r1, r3, r5, r1
	str	r2, [sp]
	ldr	r3, .L2905+8
	ldr	r2, .L2905+4
	bl	flash_read_page_en
	cmp	r0, #512
	cmnne	r0, #1
	ldr	ip, [sp, #12]
	beq	.L2889
	ldr	r3, [r10, #580]
	ldr	r3, [r3]
	cmp	r3, ip
	bne	.L2889
.L2893:
	bl	zftl_sblk_list_init
	ldr	r3, [r4, #2288]
	ldrh	r2, [r3, #140]
	cmp	r2, r7
	bgt	.L2891
	add	r6, r6, #1
	strh	r6, [r3, #140]	@ movhi
	bl	ftl_ext_info_flush
	b	.L2891
.L2889:
	add	r8, r8, #1
	b	.L2888
.L2891:
	ldr	r5, [r4, #580]
	bl	timer_get_time
	ldr	r3, .L2905+32
	umull	r0, r1, r0, r3
	mov	r3, r1, lsr #5
	str	r3, [r5, #520]
	ldr	r5, [r4, #580]
	bl	timer_get_time
	ldr	r3, [r4, #580]
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
	ldr	r2, [sp, #8]
	str	r1, [r3, #560]
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L2894
	ldr	r6, [r3, #520]
	ldr	r5, [r3, #12]
	bl	timer_get_time
	ldr	r1, .L2905+20
	mov	r2, r6
	mov	r3, r5
	str	r0, [sp]
	ldr	r0, .L2905+36
	bl	printk
.L2894:
	movw	r3, #2908
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2906:
	.align	2
.L2905:
	.word	.LANCHOR0
	.word	.LANCHOR4-2792
	.word	.LANCHOR3-3192
	.word	.LANCHOR2
	.word	.LC187
	.word	.LANCHOR1+1836
	.word	.LANCHOR0+2994
	.word	1162432070
	.word	1374389535
	.word	.LC188
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
	ldr	r2, .L2909
	mov	ip, r0
	mov	r4, r1
	ldrb	r0, [r2, #676]	@ zero_extendqisi2
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
.L2910:
	.align	2
.L2909:
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
	ldr	r5, .L2916
	cmp	r3, #1
	ldrh	r7, [r0, #12]
	ldr	r6, [r5, #3512]
	bne	.L2913
	bl	ftl_get_new_free_page
	cmn	r0, #1
	mov	r8, r0
	beq	.L2914
	ldrh	r0, [r4]
	add	r6, r6, r7, asl #2
	bl	ftl_vpn_decrement
	ldr	r7, .L2916+4
	ldr	r3, .L2916+8
	mov	r0, r6
	mov	r4, #0
	str	r3, [r7, #-2776]
	add	r3, r5, #2960
	ldrh	r1, [r3]
	ldrb	r3, [r5, #2964]	@ zero_extendqisi2
	mul	r1, r3, r1
	mov	r1, r1, asl #2
	bl	js_hash
	mov	r1, r6
	ldr	r2, .L2916+12
	ldrb	r3, [r5, #2260]	@ zero_extendqisi2
	str	r4, [r7, #-2768]
	str	r4, [r7, #-2764]
	str	r0, [r7, #-2772]
	mov	r0, r8
	bl	ftl_prog_ppa_page
	b	.L2914
.L2913:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2914:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2917:
	.align	2
.L2916:
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	-178307901
	.word	.LANCHOR5-2776
	.fnend
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 224
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3049
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #236
	sub	sp, sp, #236
	ldr	r2, [r3]
	mov	r4, r0
	ldr	ip, .L3049+4
	mov	r9, r1
	str	r3, [sp, #24]
	str	r2, [sp, #228]
	ldr	r2, [ip]
	mov	r5, ip
	tst	r2, #4096
	beq	.L2919
	ldr	r0, .L3049+8
	ldrh	r1, [r4, #2]
	bl	printk
.L2919:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L2920
	ldr	r0, .L3049+12
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	bl	printk
.L2920:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L2921
	ldr	r0, .L3049+16
	ldrh	r1, [r4]
	bl	printk
.L2921:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L2922
	ldr	r0, .L3049+20
	ldrh	r1, [r4, #16]
	ldrh	r2, [r4, #18]
	bl	printk
.L2922:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L2923
	ldr	r0, .L3049+24
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	bl	printk
.L2923:
	ldrh	r3, [r4, #10]
	ldr	r6, .L3049+28
	ldrh	r2, [r4]
	strh	r3, [r4, #14]	@ movhi
	ldrh	r3, [r6]
	cmp	r2, r3
	bcs	.L2918
	mov	r0, #1
	sub	r6, r6, #564
	bl	buf_alloc
	mov	r1, #255
	mov	r2, #64
	mov	r5, r0
	add	r0, sp, #36
	bl	ftl_memset
	mov	r1, #255
	mov	r2, #64
	add	r0, sp, #100
	bl	ftl_memset
	mov	r1, #255
	mov	r2, #64
	add	r0, sp, #164
	bl	ftl_memset
	ldrb	r7, [r4, #5]	@ zero_extendqisi2
	ldrh	r8, [r4, #2]
	mov	r3, #2
	str	r3, [sp, #12]
	mov	r3, #0
	str	r3, [sp, #16]
.L2925:
	ldr	r3, .L3049+32
	ldrh	r2, [r3]
	cmp	r2, r8
	bls	.L2928
	ldrb	r7, [r4, #5]	@ zero_extendqisi2
	add	r3, r3, #34
	str	r3, [sp, #28]
.L2926:
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L3045
	add	r3, r4, r7, asl #1
	movw	r2, #65535
	ldrh	r3, [r3, #16]
	cmp	r3, r2
	beq	.L2927
	ldr	r2, [sp, #28]
	ldr	r1, [r5, #4]
	ldrh	r2, [r2]
	mla	r3, r2, r3, r8
	ldr	r2, [r5, #12]
	str	r3, [sp, #20]
	mov	r0, r3
	ldrb	r3, [r6, #2260]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmp	r0, #512
	mov	r10, r0
	beq	.L2928
	cmn	r0, #1
	beq	.L2929
	ldr	r3, [r5, #12]
	ldr	r2, [r3]
	cmn	r2, #1
	bne	.L2929
	ldr	r3, [r3, #4]
	cmn	r3, #1
	bne	.L2929
	ldr	r3, [r5, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2928
.L2929:
	ldr	r3, .L3049+36
	mov	r2, #1
	strb	r2, [r3, #-2855]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r2, [r4, #10]
	mla	r3, r3, r8, r7
	cmp	r2, r3
	beq	.L2930
	ldr	r1, .L3049+40
	mov	r2, #1488
	ldr	r0, .L3049+44
	bl	printk
	bl	dump_stack
.L2930:
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #6]
	add	r2, r2, r3
	ldr	r3, .L3049+32
	ldrh	r1, [r3]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L2931
	ldr	r1, .L3049+40
	movw	r2, #1489
	ldr	r0, .L3049+44
	bl	printk
	bl	dump_stack
.L2931:
	cmn	r10, #1
	beq	.L2935
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #4]
	cmn	r0, #1
	beq	.L2935
	bl	lpa_hash_get_ppa
	cmp	r9, #0
	mov	r3, r0
	beq	.L2933
	ldr	r2, [r5, #12]
	ldr	r1, [r2, #8]
	str	r2, [sp, #16]
	cmp	r0, r1
	cmnne	r0, #1
	beq	.L2933
	ldr	fp, .L3049+48
	mov	r10, #1
	str	r3, [sp, #12]
	sub	r2, fp, #2928
	ldrb	r1, [fp, #-2252]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r10, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r2, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrh	r1, [r9]
	uxth	r0, r0
	ldr	r3, [sp, #12]
	cmp	r1, r0
	ldr	r2, [sp, #16]
	bne	.L2933
	ldr	ip, [r2]
	mov	r0, r3
	ldr	r1, [r5, #4]
	ldrb	r3, [r6, #2260]	@ zero_extendqisi2
	ldr	r2, .L3049+52
	str	ip, [sp, #12]
	bl	ftl_read_ppa_page
	ldr	r3, .L3049+56
	ldr	r3, [r3, #-3192]
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bcc	.L2933
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #8]
	cmn	r0, #1
	beq	.L2935
	ldrb	r3, [r6, #676]	@ zero_extendqisi2
	ldrh	r2, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r6, #2930]	@ zero_extendqisi2
	rsb	r3, r2, r3
	mov	r10, r10, asl r3
	sub	r10, r10, #1
	and	r0, r10, r0, lsr r2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L2935
.L2933:
	ldr	r2, [r5, #12]
	ldr	r3, [r6, #2268]
	ldr	r1, [r2, #4]
	cmp	r1, r3
	bcs	.L2935
	ldr	r3, .L3049+32
	ldrb	r0, [r4, #9]	@ zero_extendqisi2
	ldrh	r1, [r4, #10]
	ldrh	r3, [r3]
	mul	r3, r3, r0
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L2935
	ldr	r3, [r6, #2288]
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
	ldr	r3, [sp, #40]
	str	r3, [sp, #36]
	ldr	r3, [sp, #44]
	str	r3, [sp, #40]
	ldr	r3, [sp, #48]
	str	r3, [sp, #44]
	ldr	r3, [sp, #20]
	str	r3, [sp, #48]
.L2935:
	ldrh	r3, [r4, #6]
	sub	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	mov	r3, #4
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #16]
.L2927:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L2926
.L3045:
	add	r8, r8, #1
	mov	r3, #0
	strb	r3, [r4, #5]
	uxth	r8, r8
	b	.L2925
.L2928:
	ldrh	r3, [r4, #6]
	ldrh	r9, [r4, #10]
	strh	r8, [r4, #2]	@ movhi
	add	r9, r3, r9
	ldr	r3, .L3049+32
	ldrb	ip, [r4, #9]	@ zero_extendqisi2
	strb	r7, [r4, #5]
	ldrh	r8, [r3]
	str	r3, [sp, #20]
	mul	ip, r8, ip
	cmp	r9, ip
	beq	.L2939
	ldr	r1, .L3049+40
	movw	r2, #1569
	ldr	r0, .L3049+44
	bl	printk
	bl	dump_stack
.L2939:
	ldr	r7, .L3049+60
	mov	r6, #0
	ldrh	r1, [r4, #10]
	mov	r3, r6
	ldr	r0, [r7, #3512]
.L2940:
	cmp	r3, r1
	bcs	.L3046
	ldrh	r2, [r4, #12]
	add	r2, r3, r2
	add	r3, r3, #1
	ldr	r2, [r0, r2, asl #2]
	cmn	r2, #1
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L2940
.L3046:
	ldr	r3, [sp, #20]
	ldrb	r2, [r4, #9]	@ zero_extendqisi2
	ldr	r8, .L3049+4
	ldrh	r3, [r3]
	smulbb	r3, r2, r3
	rsb	r3, r1, r3
	add	r6, r6, r3
	ldr	r3, [r8]
	tst	r3, #4096
	uxth	r6, r6
	beq	.L2943
	ldr	r3, .L3049+60
	mov	r2, r6
	ldrh	r1, [r4]
	ldr	r0, .L3049+64
	ldr	ip, [r3, #576]
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L2943:
	ldrh	r3, [r4]
	ldr	r2, [r7, #576]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [r8]
	tst	r3, #16384
	beq	.L2944
	ldr	r3, [sp, #48]
	add	r1, sp, #36
	ldr	r0, .L3049+68
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	printk
.L2944:
	ldrb	r2, [r7, #2260]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r5, #4]
	mov	r8, #0
	ldr	fp, .L3049+4
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2945:
	ldrb	r3, [r7, #2964]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r8, r3
	bcs	.L3047
	add	r10, sp, #36
	ldr	r0, [r10, r8, asl #2]
	cmn	r0, #1
	bne	.L2946
.L2950:
	ldr	r2, [r7, #2288]
	mvn	r6, #0
	ldr	r3, [r5, #12]
	ldr	r2, [r2, #8]
	str	r2, [r3]
	ldr	r3, [r5, #12]
	str	r6, [r3, #4]
	ldr	r3, [r5, #12]
	str	r6, [r3, #8]
	mov	r3, #0
	ldr	r2, [r5, #12]
	str	r3, [r2, #12]
	ldr	r2, [r5, #4]
	str	r3, [r2]
	b	.L2947
.L2946:
	ldr	r3, .L3049+60
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	ldr	r3, [r5, #12]
	mov	r6, r0
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	ldr	r3, [fp]
	tst	r3, #16384
	mov	r9, r0
	beq	.L2948
	ldr	r3, [r5, #12]
	mov	r1, r9
	ldr	r0, .L3049+72
	ldr	r2, [r10, r8, asl #2]
	ldr	r3, [r3, #4]
	bl	printk
.L2948:
	ldr	r3, [r10, r8, asl #2]
	mov	r2, #1
	cmp	r9, r3
	ldr	r3, [r5, #12]
	mvnne	r6, #0
	cmn	r6, #1
	str	r9, [r3, #8]
	ldr	r3, [r5, #12]
	str	r2, [r3, #12]
	beq	.L2950
.L2947:
	ldr	r3, [sp, #16]
	cmp	r3, #1
	bne	.L2952
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bls	.L2952
.L3018:
	mov	r0, r4
	bl	ftl_get_new_free_page
	ldr	r3, [fp]
	tst	r3, #16384
	mov	r9, r0
	beq	.L2954
	ldrh	r1, [r4, #12]
	ldr	r2, [r5, #12]
	ldrh	r3, [r4, #10]
	ldr	r0, .L3049+76
	add	r3, r3, r1
	ldr	r2, [r2, #4]
	mov	r1, r9
	sub	r3, r3, #1
	bl	printk
.L2954:
	ldrb	r3, [r7, #2964]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	ldrh	r2, [r4, #6]
	mul	r3, r3, r1
	add	r3, r3, #1
	rsb	r3, r8, r3
	cmp	r2, r3
	bls	.L2952
	ldr	r3, .L3049+60
	mov	r0, r9
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	str	r0, [sp, #28]
	ldrh	r0, [r4]
	bl	ftl_vpn_decrement
	adds	r3, r6, #1
	movne	r3, #1
	ldr	r2, [sp, #28]
	cmn	r2, #1
	cmnne	r6, #1
	beq	.L2955
	add	r3, sp, #100
	ldrh	r2, [r4, #12]
	str	r9, [r3, r8, asl #2]
	ldrh	r3, [r4, #10]
	add	r3, r3, r2
	add	r2, sp, #164
	sub	r3, r3, #1
	str	r3, [r2, r8, asl #2]
	b	.L2952
.L2955:
	ldrh	r2, [r4, #6]
	cmp	r2, #1
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L3018
.L2952:
	add	r8, r8, #1
	b	.L2945
.L3047:
	ldr	r10, .L3049+4
	mov	r6, #0
	ldr	r9, .L3049+60
	mov	fp, r10
.L2958:
	ldrb	r3, [r7, #2964]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r6, r3
	bcs	.L3048
	add	ip, sp, #100
	str	ip, [sp, #16]
	ldr	r3, [ip, r6, asl #2]
	cmn	r3, #1
	beq	.L2960
	add	r8, sp, #36
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldr	r0, [r8, r6, asl #2]
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #256
	ldr	ip, [sp, #16]
	bne	.L2960
	ldr	r0, [ip, r6, asl #2]
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r10]
	tst	r3, #16384
	mov	ip, r0
	beq	.L2962
	ldr	r3, [r5, #12]
	mov	r1, ip
	ldr	r0, .L3049+76
	str	ip, [sp, #16]
	ldr	r2, [r3, #8]
	ldr	r3, [r8, r6, asl #2]
	bl	printk
	ldr	ip, [sp, #16]
.L2962:
	cmn	ip, #1
	beq	.L2960
	ldr	r3, [r5, #12]
	ldr	r2, [r8, r6, asl #2]
	ldr	r1, [r3, #8]
	cmp	r2, r1
	bne	.L2960
	ldr	r1, [fp]
	add	r8, sp, #164
	tst	r1, #16384
	beq	.L2963
	ldr	r1, [r3, #4]
	ldr	r0, .L3049+80
	ldr	r3, [r8, r6, asl #2]
	bl	printk
.L2963:
	ldr	r3, [r5, #12]
	ldr	r2, [r8, r6, asl #2]
	ldmib	r3, {r0, r1}
	uxth	r2, r2
	bl	lpa_hash_update_ppa
.L2960:
	add	r6, r6, #1
	b	.L2958
.L3048:
	mov	r0, r5
	bl	zbuf_free
	ldrh	r3, [r4, #12]
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #20]
	ldrh	r2, [r2]
	mla	r3, r1, r2, r3
	ldr	r2, .L3049+60
	ldr	r2, [r2, #3512]
	sub	r3, r3, #-1073741823
	ldr	r3, [r2, r3, asl #2]
	cmn	r3, #1
	beq	.L2965
	ldr	r1, .L3049+40
	movw	r2, #1660
	ldr	r0, .L3049+44
	bl	printk
	bl	dump_stack
.L2965:
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L2918
	mov	r0, r4
	bl	ftl_write_last_log_page
.L2918:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #228]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2969
	bl	__stack_chk_fail
.L2969:
	add	sp, sp, #236
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3050:
	.align	2
.L3049:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LC189
	.word	.LC190
	.word	.LC191
	.word	.LC192
	.word	.LC193
	.word	.LANCHOR0+564
	.word	.LANCHOR0+2960
	.word	.LANCHOR4
	.word	.LANCHOR1+1856
	.word	.LC0
	.word	.LANCHOR0+2928
	.word	.LANCHOR3-3192
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC194
	.word	.LC195
	.word	.LC196
	.word	.LC197
	.word	.LC198
	.fnend
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r4, r0
	ldr	r6, .L3065
	mov	r7, r1
	mov	r5, r6
.L3052:
	ldr	r3, [r6, #2288]
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	add	r2, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3065+4
	ldrh	r2, [r2]
	cmp	r1, r2
	bcs	.L3053
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3054
.L3053:
	bl	pm_alloc_new_blk
	mov	r0, #0
	bl	ftl_info_flush
.L3054:
	ldr	r3, [r5, #2288]
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3055
	ldr	r1, .L3065+8
	movw	r2, #263
	ldr	r0, .L3065+12
	bl	printk
	bl	dump_stack
.L3055:
	ldr	r3, [r5, #2288]
	ldr	r9, .L3065+16
	add	r2, r3, #692
	add	r3, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3065+20
	ldr	r0, [r9, #-2520]
	ldrh	r8, [r2]
	ldrh	r2, [r3]
	mla	r8, r8, r1, r2
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	ldr	r3, [r9, #-2520]
	str	r4, [r3]
	ldr	r2, [r5, #2288]
	ldr	r3, [r9, #-2520]
	ldr	r1, [r2, #48]
	str	r1, [r3, #4]
	mov	r1, r8
	ldrb	r0, [r2, #694]	@ zero_extendqisi2
	ldrb	r2, [r5, #2260]	@ zero_extendqisi2
	str	r2, [sp]
	mov	r2, r7
	bl	ftl_prog_page
	ldr	r2, [r5, #2288]
	add	r1, r2, #696
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r1]	@ movhi
	cmp	r3, #1
	beq	.L3056
	ldrb	r3, [r9, #-2516]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3057
.L3056:
	mov	r3, #0
	strb	r3, [r9, #-2516]
	b	.L3052
.L3057:
	cmn	r0, #1
	bne	.L3059
	mov	r1, r8
	ldr	r0, .L3065+24
	bl	printk
	b	.L3052
.L3059:
	movw	r3, #698
	mov	r0, #0
	ldrh	r3, [r2, r3]
	cmp	r4, r3
	addcc	r4, r4, #176
	strcc	r8, [r2, r4, asl #2]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3066:
	.align	2
.L3065:
	.word	.LANCHOR0
	.word	.LANCHOR0+2960
	.word	.LANCHOR1+1880
	.word	.LC0
	.word	.LANCHOR5
	.word	.LANCHOR0+2994
	.word	.LC199
	.fnend
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3101
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r3]
	mov	fp, r0
	ldr	r4, .L3101+4
	str	r3, [sp, #100]
	add	r3, r4, #564
	ldrh	r3, [r3]
	cmp	r3, r0
	bls	.L3067
	ldrb	r3, [r4, #2944]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3069
	ldrb	r3, [r4, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3067
.L3069:
	ldr	r3, [r4, #568]
	mov	r6, fp, asl #2
	add	r1, sp, #84
	mov	r0, fp
	add	r3, r3, r6
	strh	fp, [sp, #68]	@ movhi
	ldr	r7, .L3101+8
	mov	r5, #0
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r8, r5
	and	r3, r3, #224
	cmp	r3, #160
	ldreq	r3, .L3101+4
	movne	r9, #1
	ldreqb	r9, [r3, #2937]	@ zero_extendqisi2
	bl	ftl_get_blk_list_in_sblk
	ldrh	r10, [r7]
	ldr	r2, [r4, #568]
	add	r3, r2, r6
	uxtb	r0, r0
	strh	r5, [sp, #70]	@ movhi
	strb	r0, [sp, #77]
	smulbb	r10, r10, r0
	strb	r5, [sp, #73]
	strh	r5, [sp, #78]	@ movhi
	ldr	r0, .L3101+12
	strh	r10, [sp, #74]	@ movhi
	ldrh	r1, [r2, r6]
	mov	r6, r5
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r2, [r2, fp, asl #2]
	mov	r1, fp
	ubfx	r2, r2, #11, #8
	str	r2, [sp, #4]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	ip, r7
	mov	r4, r0
.L3071:
	ldrh	r3, [ip]
	uxth	r7, r5
	cmp	r3, r7
	bls	.L3093
	mov	r3, r7, asl #1
	mov	r10, #0
	sub	r3, r3, #1
	str	r3, [sp, #44]
.L3081:
	ldrb	r3, [sp, #77]	@ zero_extendqisi2
	uxth	r8, r10
	cmp	r3, r8
	bls	.L3098
	add	r3, r8, #8
	mov	r6, #1
	mov	r3, r3, asl r6
	str	r3, [sp, #48]
.L3080:
	cmp	r6, r9
	bhi	.L3099
	ldr	r2, [sp, #48]
	add	r3, sp, #104
	add	r3, r3, r2
	ldrh	r2, [r3, #-36]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3073
	ldr	r3, .L3101+16
	cmp	r9, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	uxtaheq	r3, r3, r5
	orreq	r3, r3, r6, asl #24
	beq	.L3075
	cmp	r9, #2
	uxtahne	r3, r3, r5
	ldreq	r1, [sp, #44]
	addeq	r3, r3, r1
	ldreq	r1, .L3101+4
	addeq	r3, r3, r6
	ldreqb	r1, [r1, #2937]	@ zero_extendqisi2
	orreq	r3, r3, r1, asl #24
.L3075:
	str	r3, [r4, #24]
	mov	r1, #1
	mov	r0, r4
	str	ip, [sp, #60]
	str	r2, [sp, #56]
	str	r3, [sp, #52]
	bl	sblk_read_page
	ldr	r0, [r4, #4]
	ldr	r3, [r4, #36]
	ldr	r1, [r4, #12]
	str	r3, [sp]
	ldr	lr, [r0]
	ldr	r2, [sp, #56]
	str	r3, [sp, #40]
	str	lr, [sp, #4]
	ldr	lr, [r0, #4]
	ldr	r3, [sp, #52]
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
	ldr	r0, .L3101+20
	str	r1, [sp, #32]
	mov	r1, r2
	mov	r2, r7
	bl	printk
	ldr	r3, [sp, #40]
	ldr	ip, [sp, #60]
	cmp	r3, #512
	beq	.L3093
.L3073:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3080
.L3099:
	add	r10, r10, #1
	b	.L3081
.L3098:
	add	r5, r5, #1
	b	.L3071
.L3093:
.L3077:
	str	r6, [sp]
	mov	r3, r8
	ldr	r6, .L3101+4
	mov	r1, fp
	mov	r2, r7
	ldr	r0, .L3101+24
	bl	printk
	mov	r1, #0
	ldrb	r2, [r6, #2260]	@ zero_extendqisi2
	mov	r5, r7
	ldr	r0, [r4, #4]
	mov	r9, r6
	mov	r2, r2, asl #9
	bl	ftl_memset
	ldrb	r2, [r6, #2260]	@ zero_extendqisi2
	ldr	r0, [r4, #12]
	mov	r1, #0
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3082:
	ldr	r3, .L3101+8
	ldrh	r3, [r3]
	cmp	r3, r5
	bls	.L3087
	ldr	r7, .L3101+16
	mov	r10, r5, asl #1
	mov	r8, #0
.L3088:
	ldrb	r2, [sp, #77]	@ zero_extendqisi2
	uxth	r3, r8
	cmp	r2, r3
	bls	.L3100
	mov	r2, r3
	ldr	r0, .L3101+28
	mov	r1, r5
	str	r3, [sp, #40]
	bl	printk
	ldrb	ip, [r6, #2944]	@ zero_extendqisi2
	cmp	ip, #0
	ldr	r3, [sp, #40]
	add	r3, r3, #8
	beq	.L3083
	add	r2, sp, #104
	mov	r0, r4
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-36]
	ldrh	r3, [r7]
	mul	r3, r3, r2
	orr	r3, r3, r5
	str	r3, [r4, #24]
	bl	sblk_3d_tlc_dump_prog
	b	.L3084
.L3083:
	ldrb	r2, [r9, #2937]	@ zero_extendqisi2
	cmp	r2, #2
	add	r2, sp, #104
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-36]
	ldrh	r3, [r7]
	mul	r3, r3, r2
	bne	.L3085
	orr	r3, r3, r10
	mov	r0, r4
	orr	r3, r3, #33554432
	str	r3, [r4, #24]
	bl	sblk_mlc_dump_prog
	b	.L3084
.L3085:
	ldrb	r2, [r9, #676]	@ zero_extendqisi2
	orr	r3, r3, r5
	mvn	r0, #0
	ldrb	lr, [r9, #2260]	@ zero_extendqisi2
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
.L3084:
	add	r8, r8, #1
	b	.L3088
.L3100:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L3082
.L3087:
	mov	r0, r4
	bl	zbuf_free
	ldr	r0, .L3101+32
	mov	r1, fp
	bl	printk
.L3067:
	ldr	r3, .L3101
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3089
	bl	__stack_chk_fail
.L3089:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3102:
	.align	2
.L3101:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+2960
	.word	.LC200
	.word	.LANCHOR0+2994
	.word	.LC201
	.word	.LC202
	.word	.LC203
	.word	.LC204
	.fnend
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	ftl_ink_check_sblk
	.type	ftl_ink_check_sblk, %function
ftl_ink_check_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r4, .L3127
	mov	r8, r0, asl #2
	ldr	r6, .L3127+4
	mov	r5, r0
	mov	r1, r5
	ldr	r3, [r4]
	str	r3, [sp, #52]
	ldr	r3, [r6, #568]
	ldrh	r2, [r3, r8]
	ldr	r3, [r3, r0, asl #2]
	ubfx	r2, r2, #0, #11
	ldr	r0, .L3127+8
	ubfx	r3, r3, #11, #8
	bl	printk
	movw	r3, #65535
	cmp	r5, r3
	str	r4, [sp, #8]
	beq	.L3103
	add	r3, r6, #564
	ldrh	r3, [r3]
	cmp	r3, r5
	bls	.L3103
	mov	r1, #0
	mov	r0, r5
	bl	ftl_erase_sblk
	mov	r0, r5
	mov	r9, #0
	add	r1, sp, #36
	strh	r5, [sp, #20]	@ movhi
	bl	ftl_get_blk_list_in_sblk
	strb	r0, [sp, #29]
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r6, #568]
	ldrb	r2, [r6, #2260]	@ zero_extendqisi2
	ldrh	r3, [r3, r8]
	mov	r2, r2, asl #9
	ubfx	r3, r3, #0, #11
	cmp	r3, #1
	movle	r1, #85
	movgt	r1, #170
	mov	r4, r0
	ldr	r0, [r0, #4]
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
.L3107:
	ldr	r3, .L3127+12
	uxth	fp, r9
	ldr	r2, .L3127+12
	ldrh	r3, [r3]
	cmp	r3, fp
	bls	.L3123
	add	r7, r2, #34
	mov	r10, #0
.L3110:
	ldrb	r2, [sp, #29]	@ zero_extendqisi2
	uxth	r3, r10
	cmp	r2, r3
	bls	.L3124
	add	r2, sp, #56
	add	r3, r2, r3, asl #1
	movw	r2, #65535
	ldrh	r3, [r3, #-20]
	cmp	r3, r2
	beq	.L3108
	ldrh	r1, [r7]
	mov	ip, #0
	ldr	r2, [r4, #4]
	mvn	r0, #0
	mla	r3, r1, r3, fp
	ldr	r1, .L3127+16
	str	r3, [r2]
	ldr	r2, [r4, #4]
	str	r1, [r2, #4]
	ldr	r2, [r4, #12]
	str	ip, [r2]
	ldrb	r2, [r6, #676]	@ zero_extendqisi2
	ldrb	lr, [r6, #2260]	@ zero_extendqisi2
	rsb	r1, r2, #24
	str	ip, [sp, #4]
	mvn	r2, r0, asl r2
	and	r0, r2, r3, lsr r1
	str	lr, [sp]
	bic	r1, r3, r2, asl r1
	uxtb	r0, r0
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	bl	flash_prog_page_en
.L3108:
	add	r10, r10, #1
	b	.L3110
.L3124:
	add	r9, r9, #1
	b	.L3107
.L3123:
	mov	r7, #0
.L3111:
	ldrh	r1, [r2]
	uxth	r10, r7
	cmp	r1, r10
	bls	.L3125
	mov	fp, #0
.L3116:
	ldrb	r1, [sp, #29]	@ zero_extendqisi2
	uxth	r6, fp
	cmp	r1, r6
	bls	.L3126
	add	r6, r6, #8
	add	r3, sp, #56
	add	r6, r3, r6, asl #1
	movw	r3, #65535
	ldrh	r1, [r6, #-36]
	cmp	r1, r3
	beq	.L3113
	ldr	r3, .L3127+20
	mov	r0, r4
	str	r2, [sp, #12]
	ldrh	r9, [r3]
	mla	r9, r9, r1, r10
	mov	r1, #1
	str	r9, [r4, #24]
	bl	sblk_read_page
	ldr	r1, [r4, #4]
	ldr	r1, [r1]
	cmp	r1, r9
	ldr	r2, [sp, #12]
	beq	.L3113
	mov	r0, r9
	bl	ftl_mask_bad_block
	mvn	r1, #0
	strh	r1, [r6, #-36]	@ movhi
	ldr	r2, [sp, #12]
.L3113:
	add	fp, fp, #1
	b	.L3116
.L3126:
	add	r7, r7, #1
	b	.L3111
.L3125:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L3127+4
	ldr	r0, .L3127+24
	mov	r1, r5
	ldr	r3, [r3, #568]
	ldrh	r2, [r3, r8]
	ldr	r3, [r3, r5, asl #2]
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	bl	printk
.L3103:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3118
	bl	__stack_chk_fail
.L3118:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3128:
	.align	2
.L3127:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC205
	.word	.LANCHOR0+2960
	.word	1437269760
	.word	.LANCHOR0+2994
	.word	.LC206
	.fnend
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r4, r0
	ldr	r6, .L3140
	mov	r7, r1
	mov	r8, r6
.L3130:
.L3134:
	mov	r0, r7
	bl	ftl_alloc_sblk
	ldr	r3, [r6, #2288]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	mov	r5, r0
	beq	.L3131
	ldr	r2, [r6, #568]
	mov	r3, r0, asl #2
	ldrh	r3, [r2, r3]
	ubfx	r3, r3, #0, #11
	cmp	r3, #2
	bgt	.L3131
	bl	ftl_ink_check_sblk
.L3131:
	mov	r1, #0
	mov	r0, r5
	bl	ftl_erase_sblk
	add	r1, r4, #16
	mov	r0, r5
	mov	r9, r5, asl #1
	bl	ftl_get_blk_list_in_sblk
	ldr	r2, .L3140+4
	strh	r5, [r4]	@ movhi
	cmp	r7, #2
	strb	r7, [r4, #4]
	ldrh	r2, [r2]
	uxtb	r3, r0
	strb	r3, [r4, #9]
	mov	r0, #0
	strh	r0, [r4, #2]	@ movhi
	smulbb	r3, r3, r2
	strb	r0, [r4, #5]
	strh	r0, [r4, #10]	@ movhi
	ldrneb	r0, [r6, #2964]	@ zero_extendqisi2
	ldr	r1, [r8, #3512]
	strh	r3, [r4, #6]	@ movhi
	ldrb	r3, [r8, #2964]	@ zero_extendqisi2
	smulbbne	r0, r0, r2
	mul	r2, r3, r2
	uxthne	r0, r0
	strh	r0, [r4, #12]	@ movhi
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [r8, #576]
	ldrh	r2, [r4, #6]
	strh	r2, [r3, r9]	@ movhi
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
	ldr	r0, .L3140+8
	mov	r1, r5
	bl	printk
	ldr	r3, [r6, #576]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
	mov	r3, #7
	strb	r3, [r4, #4]
	b	.L3134
.L3141:
	.align	2
.L3140:
	.word	.LANCHOR0
	.word	.LANCHOR0+2960
	.word	.LC121
	.fnend
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3156
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3143
	ldr	r0, .L3156+4
	mov	r1, #324
	ldr	r2, .L3156+8
	bl	printk
.L3143:
	ldr	r6, .L3156+12
	mov	r1, #0
	ldr	r5, .L3156+16
	mov	r2, #64
	ldr	r8, .L3156+20
	mov	r9, #0
	ldr	r0, [r6, #-2512]
	bl	ftl_memset
	ldr	r0, .L3156+24
	ldr	r1, [r5, #664]
	mov	r2, #4
	mov	r3, #16
	mov	r10, r8
	bl	rknand_print_hex
	ldr	r4, [r5, #664]
	add	r0, r4, #16
	ldr	r1, [r4, #8]
	bl	js_hash
	str	r0, [r4, #12]
.L3144:
	ldrb	r3, [r6, #-2508]	@ zero_extendqisi2
	ldrh	r4, [r8, #2]
	ldrh	ip, [r5, #2]
	mov	r1, r3
	ldr	r0, .L3156+28
	mov	r2, r4
	str	r3, [sp, #8]
	str	ip, [sp, #12]
	bl	printk
	ldr	r2, .L3156+32
	ldrh	r0, [r8, #2]
	ldr	r7, .L3156+12
	ldrh	r2, [r2]
	ldr	fp, .L3156+16
	sub	r2, r2, #1
	cmp	r0, r2
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #12]
	blt	.L3145
	ldr	r4, [fp, #664]
	ldrb	r2, [r7, #-2507]	@ zero_extendqisi2
	ldr	r3, [r4, #4]
	mov	r0, r4
	add	r3, r3, #1
	str	r3, [r4, #4]
	ldrb	r3, [r7, #-2508]	@ zero_extendqisi2
	strb	r2, [r7, #-2508]
	ldrh	r2, [r4, #16]
	strb	r3, [r7, #-2507]
	mov	r3, #0
	add	r2, r2, #1
	strh	r2, [r0, #16]!	@ movhi
	ldr	r1, [r4, #8]
	strh	r3, [r8, #2]	@ movhi
	str	r3, [sp, #8]
	bl	js_hash
	ldrb	r2, [r7, #-2508]	@ zero_extendqisi2
	str	r0, [r4, #12]
	ldr	r3, [sp, #8]
	ldrh	r4, [fp, #2]
	mov	r0, r3
	mul	r4, r4, r2
	b	.L3154
.L3145:
	cmp	r0, #0
	mla	r4, ip, r3, r4
	bne	.L3146
.L3154:
	mov	r1, r4
	bl	flash_erase_block
.L3146:
	ldr	r2, [r5, #664]
	mov	r7, #1
	ldr	r3, [r6, #-2512]
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r2, #4]
	str	r2, [r3]
	ldr	r2, .L3156+36
	ldr	r3, [r6, #-2512]
	str	r2, [r3, #4]
	mov	r2, #4
	stmia	sp, {r2, r7}
	ldr	r2, [r5, #664]
	bl	flash_prog_page_en
	ldrh	r3, [r10, #2]
	add	r3, r3, r7
	strh	r3, [r10, #2]	@ movhi
	cmn	r0, #1
	bne	.L3147
	mov	r1, r4
	ldr	r0, .L3156+40
	bl	printk
	b	.L3144
.L3147:
	cmp	r9, #0
	moveq	r9, r7
	beq	.L3144
.L3155:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3157:
	.align	2
.L3156:
	.word	.LANCHOR2
	.word	.LC136
	.word	.LANCHOR1+1896
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LANCHOR5-2508
	.word	.LC207
	.word	.LC208
	.word	.LANCHOR3-3360
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
	ldr	r3, .L3190
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	ldr	r6, .L3190+4
	tst	r3, #4096
	beq	.L3159
	ldr	r3, [r6, #664]
	mov	r1, #50
	ldr	r0, .L3190+8
	ldr	r2, .L3190+12
	str	r3, [sp]
	mov	r3, #2048
	bl	printk
.L3159:
	ldr	fp, .L3190+4
	mov	r5, #4
	ldr	r8, .L3190+16
.L3162:
	ldrh	r1, [r6, #2]
	mov	r9, #4
	ldr	r4, .L3190+20
	mov	r0, #0
	str	r9, [sp]
	mul	r1, r1, r5
	ldr	r2, [r6, #664]
	ldr	r3, [r4, #-2512]
	bl	flash_read_page_en
	ldr	r7, .L3190+4
	cmn	r0, #1
	beq	.L3160
	ldr	r2, [fp, #664]
	ldr	r10, .L3190+16
	ldr	r3, [r2]
	cmp	r3, r8
	beq	.L3161
.L3160:
	add	r5, r5, #1
	cmp	r5, #16
	bne	.L3162
.L3188:
	mvn	r0, #0
	b	.L3186
.L3161:
	ldrb	r1, [r2, #37]	@ zero_extendqisi2
	mov	r0, #0
	ldrb	r3, [r2, #36]	@ zero_extendqisi2
	strb	r1, [r4, #-2507]
	ldrh	r1, [r7, #2]
	strb	r3, [r4, #-2508]
	str	r9, [sp]
	mul	r1, r1, r3
	ldr	r3, [r4, #-2512]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3171
	ldr	r3, [r7, #664]
	ldr	r2, [r3]
	cmp	r2, r8
	ldreq	r5, [r3, #4]
	beq	.L3164
.L3171:
	mov	r5, #0
.L3164:
	ldrb	r3, [r4, #-2507]	@ zero_extendqisi2
	mov	r2, #4
	ldrh	r1, [r6, #2]
	mov	r0, #0
	str	r2, [sp]
	ldr	r2, [r6, #664]
	mul	r1, r1, r3
	ldr	r3, [r4, #-2512]
	bl	flash_read_page_en
	ldr	r7, .L3190+20
	cmn	r0, #1
	beq	.L3165
	ldr	r3, .L3190+4
	ldr	r3, [r3, #664]
	ldr	r2, [r3]
	cmp	r2, r10
	bne	.L3165
	ldr	r2, [r3, #4]
	cmp	r5, r2
	ldrccb	r2, [r3, #37]	@ zero_extendqisi2
	ldrccb	r3, [r3, #36]	@ zero_extendqisi2
	strccb	r2, [r7, #-2508]
	strccb	r3, [r7, #-2507]
.L3165:
	ldr	r2, [r6, #664]
	mov	r8, #4
	ldr	r3, [r4, #-2512]
	mov	r0, #0
	ldrb	r1, [r4, #-2508]	@ zero_extendqisi2
	mov	r7, #0
	str	r8, [sp]
	bl	flash_get_last_written_page
	ldr	r3, .L3190+24
	ldrh	r9, [r6, #2]
	ldr	ip, .L3190+4
	uxth	fp, r0
	add	r2, fp, #1
	strh	r2, [r3, #2]	@ movhi
	ldrb	r3, [r4, #-2508]	@ zero_extendqisi2
	mla	r9, r9, r3, r0
.L3166:
	rsb	r5, r7, fp
	sxth	r5, r5
	cmp	r5, #0
	blt	.L3189
	str	r8, [sp]
	mov	r0, #0
	rsb	r1, r7, r9
	ldr	r2, [r6, #664]
	ldr	r3, [r4, #-2512]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3167
	ldr	r3, [ip, #664]
	ldr	r3, [r3]
	cmp	r3, r10
	beq	.L3168
.L3167:
	add	r7, r7, #1
	b	.L3166
.L3189:
	cmn	r5, #1
	bne	.L3168
	ldr	r3, [r6, #664]
	ldr	r0, .L3190+28
	ldr	r1, [r3]
	bl	printk
	b	.L3188
.L3168:
	cmp	r7, #1
	bls	.L3172
	bl	flash_info_flush
.L3172:
	mov	r0, #0
.L3186:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3191:
	.align	2
.L3190:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC210
	.word	.LANCHOR1+1916
	.word	1398362953
	.word	.LANCHOR5
	.word	.LANCHOR5-2508
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
	ldr	r6, .L3273
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3193
	ldr	r0, .L3273+4
	movw	r1, #2578
	ldr	r2, .L3273+8
	bl	printk
.L3193:
	ldr	r5, .L3273+12
	mov	r0, r4
	mov	r7, #0
	ldr	r4, .L3273+16
	str	r7, [r5, #-3256]
	bl	nandc_init
	ldr	r3, .L3273+20
	mov	r1, r7
	mov	r2, #8
	add	r0, r4, #668
	add	r10, r4, #688
	mov	r9, r4
	str	r3, [r4, #584]
	mov	r3, #1
	strb	r3, [r4, #589]
	mov	r3, #3
	strb	r3, [r4, #676]
	bl	ftl_memset
	ldr	r0, .L3273+24
	mov	r1, r7
	mov	r2, #32
	mov	fp, r10
	bl	ftl_memset
.L3199:
	mov	r8, r7, asl #3
	uxtb	r0, r7
	add	r1, r10, r8
	bl	flash_read_id
	cmp	r7, #0
	ldr	r1, .L3273+28
	bne	.L3194
	ldrb	r3, [r9, #688]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bls	.L3195
.L3197:
	mvn	r8, #1
	b	.L3265
.L3195:
	ldrb	r3, [r9, #689]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3197
.L3194:
	ldrb	r3, [fp, r8]	@ zero_extendqisi2
	add	r7, r7, #1
	cmp	r3, #181
	ldreq	r3, .L3273+28
	moveq	r2, #44
	streqb	r2, [r8, r3]
	cmp	r7, #4
	bne	.L3199
	ldr	ip, .L3273+32
	mov	r3, #0
.L3232:
	mov	r0, r3, asl #5
	ldrb	r8, [ip, r3, asl #5]	@ zero_extendqisi2
	add	r7, r0, #1
	ldr	lr, .L3273+32
	add	r7, ip, r7
	mov	r2, #0
.L3200:
	cmp	r2, r8
	bcs	.L3272
	ldrb	r10, [r7, r2]	@ zero_extendqisi2
	ldrb	r9, [r1, r2]	@ zero_extendqisi2
	cmp	r10, r9
	bne	.L3201
	add	r2, r2, #1
	b	.L3200
.L3272:
	adds	r8, lr, r0
	beq	.L3197
	ldr	r3, .L3273
	ldr	r2, .L3273+36
	add	r0, r3, r0
	mov	r3, #0
	ldrb	r1, [r0, #446]	@ zero_extendqisi2
.L3205:
	ldrb	r0, [r2, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r1
	beq	.L3204
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L3205
.L3204:
	ldr	r1, .L3273+36
	mov	r2, #32
	ldr	r7, .L3273
	add	r1, r1, r3, asl #5
	ldr	r0, .L3273+40
	bl	ftl_memcpy
	add	r0, r7, #4
	mov	r1, r8
	mov	r2, #32
	bl	ftl_memcpy
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	cmp	r3, #8
	bhi	.L3206
	ldrb	r3, [r7, #24]	@ zero_extendqisi2
	cmp	r3, #60
	movhi	r3, #60
	strhib	r3, [r7, #24]
	b	.L3206
.L3201:
	add	r3, r3, #1
	cmp	r3, #44
	bne	.L3232
	b	.L3197
.L3206:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3234
	ldr	r0, .L3273+4
	movw	r1, #2600
	ldr	r2, .L3273+8
	bl	printk
.L3234:
	ldr	r3, [r4, #584]
	mov	r0, #16384
	ldr	r7, .L3273+44
	mov	r8, #0
	ldrh	r3, [r3, #10]
	cmp	r3, #2304
	ldreq	r3, .L3273+16
	moveq	r2, #2
	streqb	r2, [r3, #676]
	bl	ftl_malloc
	str	r0, [r5, #-3248]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-3272]
	mov	r0, #2048
	bl	ftl_malloc
	str	r0, [r4, #664]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-3244]
	mov	r0, #64
	bl	ftl_malloc
	str	r0, [r5, #-3264]
	mov	r0, #64
	bl	ftl_malloc
	strb	r8, [r7, #-2504]
	str	r0, [r7, #-2512]
	bl	flash_die_info_init
	ldrb	r0, [r6, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrb	r0, [r6, #24]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r1, [r4, #584]
	str	r8, [r5, #-3252]
	ldrh	r3, [r1, #16]
	ubfx	r2, r3, #8, #3
	strb	r2, [r4, #720]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #-3240]
	ubfx	r2, r3, #4, #1
	strb	r2, [r4, #721]
	ubfx	r2, r3, #12, #1
	strb	r2, [r4, #2944]
	ubfx	r2, r3, #13, #1
	strb	r2, [r4, #2938]
	ubfx	r2, r3, #11, #1
	strb	r2, [r4, #684]
	ldrb	r2, [r1, #31]	@ zero_extendqisi2
	ubfx	r0, r2, #1, #1
	ubfx	r2, r2, #2, #1
	strb	r2, [r7, #-2502]
	ubfx	r2, r3, #14, #1
	mov	r3, r3, lsr #15
	strb	r3, [r4, #2939]
	mov	r3, #60
	strb	r3, [r4, #588]
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	strb	r2, [r4, #1]
	cmp	r3, #9
	ldrb	r2, [r1, #28]	@ zero_extendqisi2
	strb	r0, [r7, #-2503]
	ldreq	r1, .L3273+16
	moveq	r0, #70
	strb	r2, [r4, #590]
	strb	r2, [r4]
	ldrb	r2, [r6, #11]	@ zero_extendqisi2
	streqb	r0, [r1, #588]
	cmp	r2, #9
	ldreq	r2, .L3273+16
	moveq	r1, #0
	streqb	r1, [r2, #590]
	ldrb	r2, [r6, #35]	@ zero_extendqisi2
	tst	r2, #1
	movne	r2, #3
	strneb	r2, [r4, #590]
	cmp	r3, #8
	bne	.L3211
	ldrb	r3, [r4, #688]	@ zero_extendqisi2
	cmp	r3, #44
	cmpne	r3, #137
	bne	.L3211
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #3
	ldreq	r3, .L3273+16
	moveq	r2, #0
	streqb	r2, [r3, #590]
.L3211:
	ldrh	r2, [r6, #20]
	ldrb	r3, [r6, #23]	@ zero_extendqisi2
	tst	r2, #64
	strb	r3, [r4, #661]
	beq	.L3213
	sub	r2, r3, #17
	sub	r1, r3, #21
	clz	r1, r1
	cmp	r3, #21
	cmpne	r2, #2
	mov	r1, r1, lsr #5
	bhi	.L3214
	ldr	r2, .L3273+48
	cmp	r1, #0
	ldr	r3, .L3273+12
	str	r2, [r5, #-3252]
	moveq	r2, #15
	movne	r2, #4
	strb	r2, [r3, #-3260]
	b	.L3213
.L3214:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	bhi	.L3217
	ldr	r3, .L3273+52
	mov	r2, #4
	str	r3, [r5, #-3252]
	ldr	r3, .L3273+16
	strb	r2, [r3, #662]
	b	.L3271
.L3217:
	sub	r2, r3, #67
	sub	ip, r3, #35
	cmp	r2, #1
	sub	lr, r3, #68
	sub	r1, r3, #34
	clz	ip, ip
	clz	lr, lr
	movhi	r0, #0
	movls	r0, #1
	cmp	r2, #1
	cmphi	r1, #1
	mov	ip, ip, lsr #5
	mov	lr, lr, lsr #5
	bhi	.L3218
	ldr	r2, .L3273+52
	ldr	r3, .L3273+12
	str	r2, [r5, #-3252]
	orrs	r2, lr, ip
	moveq	r2, #7
	movne	r2, #17
	cmp	r0, #0
	strb	r2, [r3, #-3260]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #662]
	b	.L3213
.L3218:
	sub	r3, r3, #36
	cmp	r3, #1
	bhi	.L3213
	ldr	r3, .L3273+56
	str	r3, [r5, #-3252]
.L3271:
	mov	r3, #7
	strb	r3, [r5, #-3260]
.L3213:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3222
	ldr	r0, .L3273+4
	movw	r1, #2671
	ldr	r2, .L3273+8
	bl	printk
.L3222:
	mov	r3, #0
	strb	r3, [r4, #623]
	ldrb	r3, [r4, #720]	@ zero_extendqisi2
	mov	r0, #4
	tst	r3, #1
	beq	.L3223
	bl	nandc_set_if_mode
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
.L3223:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	r0, #1
	mov	r8, r0
	bne	.L3225
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3226
	ldr	r0, .L3273+4
	mov	r1, #2688
	ldr	r2, .L3273+8
	bl	printk
.L3226:
	ldr	r3, [r4, #664]
	mov	r2, #17
	mov	r0, #0
	strb	r0, [r4, #680]
	strb	r2, [r3, #32]
	bl	zftl_flash_exit_slc_mode
	b	.L3265
.L3225:
	ldrb	r3, [r4, #661]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	ldrls	r3, .L3273+60
	strls	r3, [r5, #-3252]
	ldrb	r3, [r4, #720]	@ zero_extendqisi2
	ldr	r5, .L3273+16
	tst	r3, #4
	beq	.L3229
	ldr	r3, [r5, #664]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3229
	ldrb	r3, [r7, #-2508]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r1, [r5, #2]
	mul	r1, r1, r3
	bl	flash_ddr_para_scan
	ldrb	r3, [r5, #623]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3229
	ldr	r2, [r5, #664]
	strb	r3, [r2, #19]
	bl	flash_info_flush
.L3229:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3231
	ldr	r0, .L3273+4
	movw	r1, #2740
	ldr	r2, .L3273+8
	bl	printk
.L3231:
	bl	nand_flash_print_info
	mov	r8, #0
.L3265:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3274:
	.align	2
.L3273:
	.word	.LANCHOR2
	.word	.LC136
	.word	.LANCHOR1+1936
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2+4
	.word	.LANCHOR3-3356
	.word	.LANCHOR0+688
	.word	.LANCHOR2+424
	.word	.LANCHOR2+1832
	.word	.LANCHOR0+591
	.word	.LANCHOR5
	.word	micron_read_retrial
	.word	toshiba_read_retrial
	.word	toshiba_3d_read_retrial
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
	mov	r10, r0
	.pad #52
	sub	sp, sp, #52
	mov	r0, #1
	bl	buf_alloc
	ldr	ip, .L3287
	mov	r6, #0
	mov	r5, r6
	ldr	fp, [r0, #4]
	mov	r4, r0
.L3276:
	ldr	r3, .L3287+4
	uxth	r8, r6
	ldrh	r3, [r3]
	cmp	r3, r8
	bls	.L3286
	ldrh	r7, [ip]
	add	r6, r6, #1
	ldr	r3, .L3287+8
	ldr	r1, [r4, #4]
	mla	r7, r7, r10, r8
	ldr	r2, [r4, #12]
	ldrb	r3, [r3, #2260]	@ zero_extendqisi2
	str	ip, [sp, #44]
	mov	r0, r7
	bl	ftl_read_ppa_page
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	str	r0, [sp]
	mov	r9, r0
	ldr	r1, [r2]
	ldr	r0, .L3287+12
	str	r1, [sp, #4]
	ldr	r1, [r2, #4]
	str	r1, [sp, #8]
	ldr	r1, [r2, #8]
	str	r1, [sp, #12]
	mov	r1, r10
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
	b	.L3276
.L3286:
	ldr	r0, .L3287+16
	add	r1, fp, #704
	mov	r2, #4
	mov	r3, #32
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	cmp	r5, #0
	beq	.L3279
	ldr	r1, .L3287+20
	movw	r2, #1422
	ldr	r0, .L3287+24
	bl	printk
	bl	dump_stack
.L3279:
	mov	r0, r5
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3288:
	.align	2
.L3287:
	.word	.LANCHOR0+2994
	.word	.LANCHOR0+2960
	.word	.LANCHOR0
	.word	.LC201
	.word	.LC212
	.word	.LANCHOR1+1952
	.word	.LC0
	.fnend
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	ldr	r4, .L3291
	ldr	r0, .L3291+4
	add	r5, r4, #2960
	add	r6, r4, #564
	ldrb	r1, [r4, #2262]	@ zero_extendqisi2
	bl	printk
	ldr	r2, .L3291+8
	ldr	r0, .L3291+12
	sub	r3, r2, #2928
	sub	r3, r3, #8
	ldrb	r1, [r2, #-2936]	@ zero_extendqisi2
	ldrb	r2, [r2, #-2935]	@ zero_extendqisi2
	ldrh	r3, [r3, #2]
	bl	printk
	ldr	r3, [r4, #2288]
	ldr	r0, .L3291+16
	ldrh	r1, [r3, #130]
	ldrh	r2, [r3, #140]
	bl	printk
	ldr	r0, [r4, #580]
	ldrh	ip, [r0, #22]
	ldrh	r1, [r0, #16]
	ldrh	r2, [r0, #18]
	ldrb	r3, [r0, #21]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #26]
	str	r0, [sp, #4]
	ldr	r0, .L3291+20
	bl	printk
	ldr	r0, [r4, #580]
	ldrh	ip, [r0, #54]
	ldrh	r1, [r0, #48]
	ldrh	r2, [r0, #50]
	ldrb	r3, [r0, #53]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #58]
	str	r0, [sp, #4]
	ldr	r0, .L3291+24
	bl	printk
	ldr	r0, [r4, #580]
	ldrh	ip, [r0, #86]
	ldrh	r1, [r0, #80]
	ldrh	r2, [r0, #82]
	ldrb	r3, [r0, #85]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #90]
	str	r0, [sp, #4]
	ldr	r0, .L3291+28
	bl	printk
	ldrh	r2, [r5]
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldr	r1, [r4, #3512]
	ldr	r0, .L3291+32
	mul	r3, r3, r2
	mov	r2, #4
	mov	r3, r3, asl #1
	bl	rknand_print_hex
	ldr	r1, [r4, #576]
	ldrh	r3, [r6]
	mov	r2, #2
	ldr	r0, .L3291+36
	bl	rknand_print_hex
	ldr	r3, [r4, #2288]
	movw	ip, #698
	ldr	r0, .L3291+40
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #568]
	ldrh	r3, [r6]
	mov	r2, #4
	ldr	r0, .L3291+44
	bl	rknand_print_hex
	add	r1, r4, #2992
	ldr	r0, .L3291+48
	add	r1, r1, #8
	mov	r2, #2
	mov	r3, #256
	bl	rknand_print_hex
	ldrh	r2, [r5]
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldr	r0, .L3291+52
	ldr	r1, [r4, #3516]
	mul	r3, r3, r2
	mov	r2, #2
	mov	r3, r3, asl #1
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, lr}
	b	rknand_print_hex
.L3292:
	.align	2
.L3291:
	.word	.LANCHOR0
	.word	.LC213
	.word	.LANCHOR3
	.word	.LC214
	.word	.LC215
	.word	.LC216
	.word	.LC217
	.word	.LC218
	.word	.LC219
	.word	.LC220
	.word	.LC212
	.word	.LC221
	.word	.LC222
	.word	.LC223
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
	mov	r5, r2
	ldr	r4, .L3297
	mov	r7, r0
	mov	r6, r1
	add	r3, r4, #2928
	ldrb	r2, [r4, #676]	@ zero_extendqisi2
	ldrh	r0, [r3]
	mvn	r3, #0
	rsb	r2, r2, #24
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	rsb	r2, r0, r2
	mov	r0, r5, lsr r0
	bic	r0, r0, r3, asl r2
	bl	__aeabi_uidiv
	ldr	r3, [r4, #568]
	uxth	r0, r0
	add	r0, r3, r0, asl #2
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	mov	r0, r0, lsr #5
	cmp	r0, #1
	cmpne	r0, #7
	moveq	r0, #1
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	ldr	r0, .L3297+4
	bl	printk
	bl	dump_ftl_info
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3298:
	.align	2
.L3297:
	.word	.LANCHOR0
	.word	.LC224
	.fnend
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #31
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r5, r0
	mov	r6, r1
	bls	.L3300
	ldr	r1, .L3314
	mov	r2, #32
	ldr	r0, .L3314+4
	bl	printk
	bl	dump_stack
.L3300:
	ldr	r7, .L3314+8
	movw	r8, #698
	ldr	r3, [r7, #2288]
	mov	r9, r7
	ldrh	r2, [r3, r8]
	cmp	r5, r2
	bls	.L3301
	mov	r1, r5
	ldr	r0, .L3314+12
	bl	printk
	ldr	r3, .L3314+16
	mov	r4, #0
	mov	r1, #255
	ldr	r0, [r4, #4]
	ldrh	r2, [r3, #-4]
	bl	ftl_memset
	ldr	r3, [r7, #2288]
	ldrh	r3, [r3, r8]
	cmp	r3, r5
	bcs	.L3312
	ldr	r0, .L3314+4
	mov	r2, #36
	ldr	r1, .L3314
	b	.L3313
.L3301:
	add	r2, r5, #176
	mov	r6, r6, asl #3
	add	r8, r7, r6
	ldr	r4, [r3, r2, asl #2]
	ldr	r3, .L3314+20
	add	r2, r3, r6
	strh	r5, [r3, r6]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strh	r3, [r2, #2]	@ movhi
	bne	.L3304
	mov	r1, r5
	mov	r2, r4
	ldr	r0, .L3314+24
	bl	printk
	ldr	r3, .L3314+16
	ldr	r0, [r8, #3544]
	mov	r1, #255
	ldrh	r2, [r3, #-4]
	bl	ftl_memset
	b	.L3312
.L3304:
	ldr	r7, .L3314+28
	mov	r0, r4
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	ldr	r1, [r8, #3544]
	ldr	r2, [r7, #-2520]
	bl	ftl_read_ppa_page
	ldr	r2, [r7, #-2520]
	ldr	r2, [r2]
	cmp	r2, r5
	mov	r3, r0
	bne	.L3305
	cmn	r0, #1
	cmpne	r0, #512
	beq	.L3305
.L3308:
	ldr	r3, [r7, #-2520]
	ldr	r3, [r3]
	cmp	r3, r5
	beq	.L3312
	ldr	r0, .L3314+4
	mov	r2, #63
	ldr	r1, .L3314
	b	.L3313
.L3305:
	mov	r1, r5
	str	r4, [sp]
	ldr	r0, .L3314+32
	add	r6, r9, r6
	bl	printk
	ldr	r3, [r9, #2288]
	movw	ip, #698
	ldr	r0, .L3314+36
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	ldr	r0, .L3314+40
	mov	r2, #4
	ldr	r1, [r6, #3544]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	ldr	r0, .L3314+44
	ldr	r1, [r7, #-2520]
	mov	r2, #4
	mov	r3, #16
	bl	rknand_print_hex
	ldrb	r3, [r9, #2260]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r1, [r6, #3544]
	ldr	r2, [r7, #-2520]
	bl	ftl_read_ppa_page
	ldr	r3, [r7, #-2520]
	ldr	r3, [r3]
	cmp	r3, r5
	bne	.L3310
	cmn	r0, #1
	cmpne	r0, #512
	bne	.L3308
	b	.L3310
.L3313:
	bl	printk
	bl	dump_stack
.L3312:
	mov	r0, #0
	b	.L3303
.L3310:
	mvn	r0, #0
.L3303:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3315:
	.align	2
.L3314:
	.word	.LANCHOR1+1968
	.word	.LC0
	.word	.LANCHOR0
	.word	.LC225
	.word	.LANCHOR5-2496
	.word	.LANCHOR0+3540
	.word	.LC226
	.word	.LANCHOR5
	.word	.LC227
	.word	.LC228
	.word	.LC229
	.word	.LC230
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
	ldr	r4, .L3335
	ldr	r5, .L3335+4
	ldr	r3, [r4, #2288]
	add	r3, r3, #688
	ldrh	r2, [r3]
	sub	r3, r5, #3296
	ldrh	r0, [r3]
	mov	r6, r3
	sub	r0, r0, #1
	cmp	r2, r0
	bge	.L3317
	ldr	r3, [r5, #-3304]
	cmp	r3, #0
	beq	.L3318
.L3317:
	bl	pm_free_sblk
	ldr	r2, [r4, #2288]
	add	r3, r2, #688
	ldrh	r1, [r3]
	ldrh	r3, [r6]
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3319
	ldr	r3, .L3335+4
	ldr	r3, [r3, #-3304]
	cmp	r3, #0
	beq	.L3318
.L3319:
	uxth	r0, r0
	mov	r3, #0
	add	r0, r0, #208
	str	r3, [r5, #-3304]
	movw	r5, #65535
	mov	r0, r0, asl #1
	ldrh	r6, [r2, r0]
	cmp	r6, r5
	bne	.L3321
	ldr	r1, .L3335+8
	mov	r2, #162
	ldr	r0, .L3335+12
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	r3, [r4, #2288]
	uxth	r0, r0
	add	r0, r0, #208
	mov	r0, r0, asl #1
	ldrh	r6, [r3, r0]
	cmp	r6, r5
	beq	.L3318
.L3321:
	bl	pm_select_ram_region
	ldr	r3, .L3335+16
	movw	r2, #65535
	mov	r5, r0, asl #3
	mov	r9, r0
	add	r7, r3, r5
	str	r3, [sp, #4]
	ldrh	r0, [r3, r5]
	cmp	r0, r2
	beq	.L3322
	ldr	r3, .L3335
	add	r3, r3, r5
	ldr	r1, [r3, #3544]
	cmp	r1, #0
	beq	.L3322
	ldrsh	r3, [r7, #2]
	cmp	r3, #0
	bge	.L3322
	bl	pm_write_page
	ldrh	r3, [r7, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r7, #2]	@ movhi
.L3322:
	ldr	r7, .L3335
	mov	r8, #0
	add	fp, r7, r5
.L3323:
	ldr	r0, [r4, #2288]
	uxth	r10, r8
	add	r3, r0, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r3, r10
	bls	.L3334
	ldr	r3, .L3335+20
	ldrh	r1, [r3]
	add	r3, r10, #176
	ldr	ip, [r0, r3, asl #2]
	mvn	r0, #0
	ldrb	r3, [r7, #676]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r1, r3
	mvn	r0, r0, asl r3
	and	r0, r0, ip, lsr r1
	ldrb	r1, [r7, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, r6
	bne	.L3324
	mov	r0, r10
	mov	r1, r9
	bl	load_l2p_region
	cmp	r0, #0
	bne	.L3325
	mov	r0, r10
	ldr	r1, [fp, #3544]
	bl	pm_write_page
.L3325:
	ldr	r2, [sp, #4]
	mvn	r3, #0
	strh	r3, [r2, r5]	@ movhi
.L3324:
	add	r8, r8, #1
	b	.L3323
.L3334:
	bl	pm_free_sblk
.L3318:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3336:
	.align	2
.L3335:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1984
	.word	.LC0
	.word	.LANCHOR0+3540
	.word	.LANCHOR0+2928
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
	ldr	r4, .L3343
	ldr	r3, .L3343+4
	add	r2, r4, r0
	add	r5, r3, r0
	ldr	r1, [r2, #3544]
	ldrh	r0, [r3, r0]
	bl	pm_write_page
	ldrh	r3, [r5, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #3536]
	cmp	r3, #0
	beq	.L3338
	bl	pm_gc
	mov	r3, #0
	str	r3, [r4, #3536]
.L3338:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3344:
	.align	2
.L3343:
	.word	.LANCHOR0
	.word	.LANCHOR0+3540
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
	ldr	r5, .L3350
.L3347:
	add	r3, r5, r4, asl #3
	uxth	r0, r4
	ldrsh	r3, [r3, #2]
	cmp	r3, #0
	bge	.L3346
	bl	pm_flush_id
.L3346:
	add	r4, r4, #1
	cmp	r4, #32
	bne	.L3347
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3351:
	.align	2
.L3350:
	.word	.LANCHOR0+3540
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r0
	ldr	r3, .L3376
	mov	r0, #64
	ldr	r4, .L3376+4
	mov	r10, #0
	ldr	r5, .L3376+8
	mvn	fp, #0
	str	r10, [r3, #-3304]
	mov	r3, #1
	str	r10, [r4, #3536]
	mov	r8, r4
	strb	r3, [r5, #-2516]
	bl	ftl_malloc
	ldr	r9, .L3376+12
	str	r0, [r5, #-2520]
.L3358:
	mov	r3, #0
	cmp	r7, r3
	mov	r6, r9
	strh	fp, [r6, r10]!	@ movhi
	strh	r3, [r6, #2]	@ movhi
	beq	.L3357
	ldrb	r0, [r8, #2260]	@ zero_extendqisi2
	mov	r0, r0, asl #9
	bl	ftl_malloc
	str	r0, [r6, #4]
.L3357:
	add	r10, r10, #8
	cmp	r10, #256
	bne	.L3358
	ldr	r3, [r4, #2288]
	ldr	r8, [r5, #-2520]
	ldr	r10, [r4, #3544]
	add	r3, r3, #692
	ldrb	r0, [r3, #2]	@ zero_extendqisi2
	ldrh	r1, [r3]
	mov	r3, #4
	mov	r2, r10
	str	r3, [sp]
	mov	r3, r8
	ldr	r9, .L3376+4
	bl	flash_get_last_written_page
	ldr	r6, [r4, #2288]
	add	r3, r6, #696
	ldrh	r2, [r3]
	cmp	r2, r0
	mov	r5, r0
	bgt	.L3359
	add	r6, r6, #692
	mov	r3, r5
	ldr	r0, .L3376+16
	add	r5, r5, #1
	ldrh	r1, [r6]
	bl	printk
	ldr	r3, [r9, #2288]
	add	r3, r3, #696
	ldrh	r7, [r3]
.L3360:
	sxth	r3, r7
	cmp	r3, r5
	bge	.L3375
	ldr	r2, [r4, #2288]
	ldr	r0, .L3376+20
	add	r1, r2, #692
	ldrh	r6, [r0]
	ldrh	r1, [r1]
	ldrb	r0, [r2, #694]	@ zero_extendqisi2
	mov	r2, r10
	mla	r6, r6, r1, r3
	ldrb	r3, [r4, #2260]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r8
	mov	r1, r6
	bl	flash_read_page_en
	ldr	r3, [r4, #2288]
	add	r3, r3, #692
	ldr	r2, [r3, #-644]
	add	r2, r2, #1
	str	r2, [r3, #-644]
	add	r3, r3, #2
	ldr	r1, [r8]
	mov	r2, r6
	ldrh	r3, [r3]
	mov	fp, r0
	ldr	r0, .L3376+24
	bl	printk
	cmp	fp, #512
	cmnne	fp, #1
	beq	.L3361
	ldr	r1, [r9, #2288]
	ldr	r3, [r8]
	add	r2, r1, #696
	add	r2, r2, #2
	ldrh	r2, [r2]
	cmp	r3, r2
	addcc	r3, r3, #176
	strcc	r6, [r1, r3, asl #2]
.L3361:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3360
.L3375:
	ldr	r3, .L3376+28
	mov	r2, #1
	strb	r2, [r3, #-2855]
	ldr	r3, [r4, #2288]
	add	r3, r3, #696
	strh	r5, [r3]	@ movhi
	bl	pm_free_sblk
.L3359:
	ldr	r3, .L3376+32
	mov	r1, #255
	ldr	r0, [r4, #3544]
	ldr	r5, .L3376+4
	ldrh	r2, [r3, #-4]
	bl	ftl_memset
	mvn	r0, #0
	ldr	r1, [r4, #3544]
	bl	pm_write_page
	ldr	r3, .L3376+28
	ldrb	r3, [r3, #-2855]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3363
	ldr	r1, [r5, #3544]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r5, #3544]
	mvn	r0, #0
	bl	pm_write_page
	mvn	r0, #0
	ldr	r1, [r5, #3544]
	bl	pm_write_page
.L3363:
	bl	pm_free_sblk
	bl	pm_gc
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3377:
	.align	2
.L3376:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR0+3540
	.word	.LC231
	.word	.LANCHOR0+2994
	.word	.LC232
	.word	.LANCHOR4
	.word	.LANCHOR5-2496
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
	mov	r5, r1
	ldr	fp, .L3397
	mov	r9, r0
	mov	r7, r2
	ldrb	r4, [fp, #2260]	@ zero_extendqisi2
	mov	r1, r4, asl #7
	bl	__aeabi_uidiv
	mov	r3, r4, asl #7
	ldr	r2, [fp, #2268]
	cmp	r9, r2
	ldrcc	r2, .L3397+4
	uxth	r10, r0
	mov	r6, r0
	smulbb	r3, r3, r10
	rsb	r3, r3, r9
	uxth	r8, r3
	movcc	r3, #0
	bcc	.L3379
	mov	r1, r9
	ldr	r0, .L3397+8
	bl	printk
	mvn	r0, #0
	cmp	r7, #0
	streq	r0, [r5]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3395:
	ldrh	r1, [r1, r2]
	cmp	r1, r10
	beq	.L3382
.L3381:
	add	r3, r3, #1
	cmp	r3, #32
	beq	.L3394
.L3379:
	mov	r1, r3, asl #3
	uxth	r4, r3
	add	r0, r2, r1
	ldr	r9, .L3397+4
	ldr	r0, [r0, #4]
	cmp	r0, #0
	bne	.L3395
	b	.L3381
.L3394:
	bl	pm_select_ram_region
	mov	r3, r0, asl #3
	mov	r4, r0
	add	r2, r9, r3
	ldrh	r1, [r9, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L3396
.L3385:
	ldr	r3, .L3397+12
	mov	r0, r10
	mov	r1, r4
	strb	r4, [r3, #-2498]
	bl	load_l2p_region
.L3382:
	cmp	r7, #0
	mov	r3, r4, asl #3
	bne	.L3383
	add	r2, fp, r3
	mvn	r1, #0
	ldr	r2, [r2, #3544]
	ldr	r2, [r2, r8, asl #2]
	str	r2, [r5]
	ldrb	r0, [fp, #676]	@ zero_extendqisi2
	rsb	ip, r0, #24
	mvn	r1, r1, asl r0
	and	r2, r1, r2, lsr ip
	ldrb	r1, [fp, #2996]	@ zero_extendqisi2
	cmp	r2, r1
	mvncs	r2, #0
	strcs	r2, [r5]
	b	.L3384
.L3383:
	add	r2, fp, r3
	ldr	r1, [r5]
	ldr	r2, [r2, #3544]
	str	r1, [r2, r8, asl #2]
	add	r1, r9, r3
	strb	r6, [fp, #3796]
	ldrh	r2, [r1, #2]
	mvn	r2, r2, asl #17
	mvn	r2, r2, lsr #17
	strh	r2, [r1, #2]	@ movhi
.L3384:
	add	r3, r9, r3
	movw	r1, #32767
	ldrh	r2, [r3, #2]
	ubfx	r0, r2, #0, #15
	cmp	r0, r1
	mov	r0, #0
	addne	r2, r2, #1
	strneh	r2, [r3, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3396:
	ldrsh	r3, [r2, #2]
	cmp	r3, #0
	bge	.L3385
	bl	pm_flush_id
	b	.L3385
.L3398:
	.align	2
.L3397:
	.word	.LANCHOR0
	.word	.LANCHOR0+3540
	.word	.LC233
	.word	.LANCHOR5
	.fnend
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r8, .L3484
	mov	r6, #0
	ldr	r3, .L3484+4
	ldr	r4, .L3484+8
	ldr	r5, [r8, #580]
	ldr	r3, [r3]
	strb	r6, [r8, #2945]
	ldrh	r7, [r5, #80]
	str	r3, [sp, #68]
	movw	r3, #65535
	cmp	r7, r3
	strb	r6, [r4, #-3322]
	beq	.L3400
	mvn	r3, #0
	mov	r0, #1
	strh	r3, [r5, #130]	@ movhi
	bl	buf_alloc
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	add	r3, r5, r3, asl #1
	ldrh	r1, [r3, #94]
	movw	r3, #2994
	ldrh	r3, [r8, r3]
	mul	r1, r3, r1
	ldrb	r3, [r8, #2937]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r6, r0
	str	r1, [sp, #60]
	beq	.L3401
	ldrb	r2, [r8, #684]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3402
.L3401:
	movw	r2, #2962
	ldrh	r2, [r8, r2]
	sub	r2, r2, #1
	add	r2, r2, r1
	orr	r3, r2, r3, asl #24
	b	.L3481
.L3402:
	cmp	r3, #3
	bne	.L3403
	add	r3, r8, #2960
	ldrh	r3, [r3]
	sub	r3, r3, #1
	add	r3, r3, r1
	orr	r3, r3, #50331648
.L3481:
	str	r3, [r6, #24]
.L3403:
	mov	r0, r6
	mov	r1, #1
	bl	sblk_read_page
	ldr	r7, [r6, #36]
	cmn	r7, #1
	cmpne	r7, #512
	moveq	r7, #1
	movne	r7, #0
	beq	.L3404
	ldr	r3, [r6, #12]
	ldr	r2, [r3]
	ldr	r3, .L3484+12
	cmp	r2, r3
	beq	.L3405
.L3404:
	mov	r0, r6
	mov	r4, #0
	bl	zbuf_free
	ldr	r3, [r6, #4]
	ldr	r2, [r6, #12]
	ldr	r0, .L3484+16
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
	ldr	r2, [r6, #36]
	ldr	r3, [r3]
	ldr	r1, [r6, #24]
	bl	printk
	ldrh	r3, [r5, #80]
	ldr	r2, [r8, #576]
	mov	r3, r3, asl #1
	strh	r4, [r2, r3]	@ movhi
	ldrh	r2, [r5, #80]
	ldr	r3, [r8, #580]
	strh	r2, [r3, #130]	@ movhi
	b	.L3406
.L3405:
	movw	fp, #2962
	ldrb	r3, [r8, #2964]	@ zero_extendqisi2
	ldrh	r2, [r8, fp]
	ldr	r1, [r6, #4]
	ldr	r0, [r4, #-3316]
	mul	r2, r3, r2
	ldr	r10, .L3484+8
	ldr	r9, .L3484
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	ldrh	r2, [r8, fp]
	ldrb	r3, [r8, #2964]	@ zero_extendqisi2
	ldrb	r1, [r8, #2260]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r1, r3, asr #6
	mov	r2, r3, asl #2
	bge	.L3407
	rsb	r3, r3, r1, asl #7
	ldr	r0, [r10, #-3312]
	mov	r1, r7
	mov	r7, #1
	mov	r4, r3, asl #2
	bl	ftl_memset
	ldrh	r1, [r9, fp]
	ldrb	r3, [r9, #2964]	@ zero_extendqisi2
	mov	r2, r4
	ldr	r0, [r10, #-3312]
	mul	r3, r3, r1
	ldr	r1, [r6, #4]
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L3408
.L3407:
	ldr	r1, [r6, #4]
	mov	r4, r7
	ldr	r0, [r10, #-3312]
	add	r1, r1, r2
	bl	ftl_memcpy
.L3408:
	ldr	r3, .L3484
	add	r2, r3, #2960
	ldrh	r2, [r2]
	str	r2, [sp, #36]
	ldrb	r2, [r8, #2937]	@ zero_extendqisi2
	cmp	r2, #2
	str	r2, [sp, #32]
	bne	.L3409
	ldrb	r3, [r3, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3410
.L3409:
	ldrb	r3, [r8, #684]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3411
.L3410:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	mul	r3, r3, r2
	str	r3, [sp, #36]
	mov	r3, #1
	str	r3, [sp, #32]
.L3411:
	ldr	ip, .L3484
	mov	r8, #0
	str	r8, [sp, #40]
.L3412:
	ldr	r3, [sp, #36]
	cmp	r8, r3
	bcs	.L3424
	add	r3, r5, #96
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #44]
.L3425:
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	ldr	r2, [sp, #44]
	cmp	r2, r3
	bge	.L3421
	ldr	r3, [sp, #40]
	mov	fp, #1
	ldr	r10, .L3484+8
	mov	r9, r3, asl #2
.L3422:
	ldr	r3, [sp, #32]
	cmp	fp, r3
	bhi	.L3482
	ldr	r3, [sp, #48]
	ldrb	r1, [ip, #2937]	@ zero_extendqisi2
	ldrh	r2, [r3]
	cmp	r1, #2
	ldr	r3, .L3484+20
	ldrh	r3, [r3]
	mul	r3, r3, r2
	str	r3, [sp, #60]
	beq	.L3413
	ldrb	r2, [ip, #684]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3414
.L3413:
	sub	r2, r8, #1
	add	r3, r2, r3
	add	r3, r3, fp
	orr	r3, r3, r1, asl #24
	b	.L3416
.L3414:
	cmp	r1, #3
	add	r3, r3, r8
	orreq	r3, r3, fp, asl #24
.L3416:
	str	r3, [r6, #24]
	mov	r0, r6
	mov	r1, #1
	str	ip, [sp, #52]
	bl	sblk_read_page
	cmp	r7, #0
	ldr	ip, [sp, #52]
	beq	.L3417
	ldr	r3, [r10, #-3312]
	ldr	r2, [r3, r9]
	cmp	r2, #0
	ldreq	r2, [r6, #12]
	ldreq	r2, [r2, #8]
	streq	r2, [r3, r9]
.L3417:
	ldr	r3, [r10, #-3316]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L3418
	ldr	r0, [r10, #-3312]
	ldr	lr, [r0, r9]
	ldr	r0, [r2, #8]
	cmp	lr, r0
	beq	.L3446
.L3418:
	ldr	r0, [r10, #-3312]
	str	ip, [sp, #52]
	ldr	r0, [r0, r9]
	str	r0, [sp]
	ldr	r0, [r2]
	stmib	sp, {r0, r1}
	ldr	r1, [r2, #8]
	ldr	r0, .L3484+24
	str	r1, [sp, #12]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	ldr	r1, [r6, #24]
	ldr	r2, [r6, #36]
	bl	printk
	ldr	r3, [r10, #-3316]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	ldr	ip, [sp, #52]
	beq	.L3446
	mov	r0, r6
	bl	zbuf_free
	ldr	r2, .L3484
	ldrh	r3, [r5, #80]
	mov	r0, #0
	ldr	r1, [r2, #576]
	mov	r3, r3, asl #1
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r5, #80]
	ldr	r3, [r2, #580]
	strh	r1, [r3, #130]	@ movhi
	b	.L3406
.L3446:
	ldr	r3, [sp, #40]
	add	fp, fp, #1
	add	r9, r9, #4
	add	r3, r3, #1
	str	r3, [sp, #40]
	b	.L3422
.L3482:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #48]
	add	r3, r3, #2
	str	r3, [sp, #48]
	b	.L3425
.L3421:
	ldrb	r3, [ip, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r8, r8, #1
	add	r8, r8, #1
	b	.L3412
.L3424:
	mov	r0, r6
	ldr	r6, .L3484
	bl	zbuf_free
	movw	r2, #2962
	ldrb	r0, [r5, #89]	@ zero_extendqisi2
	mov	r4, #0
	ldrh	r2, [r6, r2]
	mov	r10, r4
	ldrh	r3, [r5, #80]
	mov	r8, r4
	ldr	r1, [r6, #576]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	strh	r2, [r1, r3]	@ movhi
.L3426:
	ldr	r3, [sp, #36]
	cmp	r10, r3
	bcs	.L3439
	add	r3, r5, #96
	str	r3, [sp, #44]
	mov	r3, #0
	str	r3, [sp, #40]
.L3440:
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r3, r2
	bge	.L3436
	ldr	fp, .L3484+8
	mov	r7, #1
.L3437:
	ldr	r3, [sp, #32]
	cmp	r7, r3
	bhi	.L3483
	ldr	r2, [fp, #-3316]
	ldr	r9, [r2, r8, asl #2]
	cmn	r9, #1
	beq	.L3427
	ldr	r2, [fp, #-3312]
	mov	r0, r9
	ldr	r3, [r2, r8, asl #2]
	str	r3, [sp, #48]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #64]
	bne	.L3428
	mov	r0, r9
	add	r1, sp, #64
	mov	r2, #0
	bl	pm_log2phys
.L3428:
	ldr	r3, [sp, #44]
	ldr	r2, .L3484+20
	ldrh	r1, [r3]
	ldrh	r2, [r2]
	mul	r2, r2, r1
	ldrb	r1, [r6, #2937]	@ zero_extendqisi2
	cmp	r1, #2
	beq	.L3429
	ldrb	r0, [r6, #684]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3430
.L3429:
	sub	r0, r10, #1
	add	r2, r0, r2
	add	r2, r2, r7
	orr	r2, r2, r1, asl #24
	b	.L3432
.L3430:
	cmp	r1, #3
	add	r2, r2, r10
	orreq	r2, r2, r7, asl #24
.L3432:
	str	r2, [sp, #60]
	ldr	r2, .L3484+28
	ldrb	r1, [r6, #676]	@ zero_extendqisi2
	ldr	r3, [sp, #48]
	ldrh	r0, [r2]
	rsb	r1, r1, #24
	mov	r2, #1
	str	r2, [sp, #52]
	rsb	r1, r0, r1
	mov	r1, r2, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r6, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r1, [sp, #64]
	uxth	ip, r0
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r1, r3
	bne	.L3433
	add	r1, sp, #60
	mov	r0, r9
	str	ip, [sp, #48]
	bl	pm_log2phys
	add	r4, r4, #1
	ldr	ip, [sp, #48]
	mov	r0, ip
	bl	ftl_vpn_decrement
	ldr	ip, [sp, #48]
	b	.L3434
.L3433:
	ldr	r2, [sp, #60]
	cmp	r1, r2
	addeq	r4, r4, #1
.L3434:
	ldr	r3, .L3484
	add	r8, r8, #1
	ldr	r2, [r3, #568]
	add	r2, r2, ip, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r2, r2, #224
	bne	.L3427
	ldr	r1, [r6, #576]
	mov	ip, ip, asl #1
	ldrh	r0, [r1, ip]
	cmp	r0, #0
	strneh	r2, [r1, ip]	@ movhi
.L3427:
	add	r7, r7, #1
	b	.L3437
.L3483:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	ldr	r3, [sp, #44]
	add	r3, r3, #2
	str	r3, [sp, #44]
	b	.L3440
.L3436:
	ldrb	r2, [r6, #2938]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r10, r10, #1
	add	r10, r10, #1
	b	.L3426
.L3439:
	ldr	r2, .L3484
	ldrh	r3, [r5, #80]
	ldr	r2, [r2, #576]
	mov	r3, r3, asl #1
	strh	r4, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
.L3406:
	ldr	r6, .L3484
	ldrh	r0, [r5, #80]
	ldr	r2, [r6, #576]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3441
	bl	ftl_dump_write_open_sblk
.L3441:
	ldrh	r1, [r5, #80]
	mov	r2, r4
	ldr	r0, .L3484+32
	bl	printk
	mvn	r3, #0
	strh	r3, [r5, #80]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r6, #580]
	ldrh	r0, [r3, #130]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3442
	bl	ftl_free_sblk
.L3442:
	ldr	r2, [r6, #2288]
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, #126]	@ movhi
	ldr	r2, [r6, #580]
	strh	r3, [r2, #130]	@ movhi
	bl	ftl_info_flush
	b	.L3399
.L3400:
	ldrh	r3, [r5, #130]
	cmp	r3, r7
	beq	.L3399
	ldr	r2, [r8, #2288]
	ldrh	r2, [r2, #126]
	cmp	r2, r3
	bne	.L3444
	bl	pm_flush
	ldr	r3, [r8, #580]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
	ldr	r3, [r8, #2288]
	mvn	r2, #0
	mov	r0, r6
	strh	r2, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L3444:
	ldr	r3, [r8, #580]
	mvn	r2, #0
	strh	r2, [r3, #130]	@ movhi
.L3399:
	ldr	r3, .L3484+4
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3445
	bl	__stack_chk_fail
.L3445:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3485:
	.align	2
.L3484:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR3
	.word	-178307901
	.word	.LC234
	.word	.LANCHOR0+2994
	.word	.LC235
	.word	.LANCHOR0+2928
	.word	.LC236
	.fnend
	.size	gc_recovery, .-gc_recovery
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
	ldr	fp, .L3561
	mvn	r3, #0
	ldr	r4, .L3561+4
	mov	r8, #0
	ldr	r2, .L3561+8
	mov	ip, fp
	ldr	r1, .L3561+12
	ldr	r0, .L3561+16
	str	r3, [ip, #-2496]!
	strb	r3, [r2, #-3239]
	str	ip, [sp, #16]
	str	ip, [sp, #4]
	strb	r3, [r4, #2308]
	strb	r3, [r4, #3531]
	strb	r8, [r4, #2284]
	strb	r8, [r4, #3532]
	strb	r8, [r4, #2285]
	bl	printk
	ldr	r5, .L3561+20
	movw	r1, #2962
	ldrb	r3, [r5, #16]	@ zero_extendqisi2
	ldrh	r9, [r5, #14]
	ldrb	r2, [r5, #13]	@ zero_extendqisi2
	strh	r9, [r4, r1]	@ movhi
	mov	r0, r9
	strb	r2, [r4, #2260]
	str	r3, [sp, #4]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	str	r2, [sp, #12]
	strb	r3, [r4, #2937]
	bl	__aeabi_idiv
	add	r1, r4, #2960
	ldrh	r7, [r5, #18]
	str	r0, [sp, #8]
	ldrh	r3, [sp, #8]
	ldrb	r0, [r5, #17]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	strh	r3, [r1]	@ movhi
	ldrb	r1, [r4, #589]	@ zero_extendqisi2
	ldr	ip, [sp, #16]
	mov	r10, r2, asl #9
	strb	r0, [r4, #2930]
	strb	r1, [r4, #2996]
	uxth	r10, r10
	smulbb	r1, r0, r1
	strh	r10, [ip, #-4]	@ movhi
	ldrh	ip, [r4, #2]
	mov	r0, #1
	str	fp, [sp, #12]
	uxtb	r6, r1
	add	r1, r4, #564
	strb	r6, [r4, #2964]
	strh	r7, [r1]	@ movhi
	movw	r1, #2994
	strh	ip, [r4, r1]	@ movhi
.L3487:
	cmp	r0, ip
	uxth	r1, r8
	add	r8, r8, #1
	movls	r0, r0, asl #1
	bls	.L3487
.L3559:
	mul	r3, r9, r2
	ldr	r0, .L3561+24
	sub	r1, r1, #1
	strh	r1, [r0]	@ movhi
	mov	r0, #0
	mov	r1, #1
	mul	r8, r7, r3
	mov	ip, r8, lsr #21
.L3489:
	cmp	r1, ip
	uxth	lr, r0
	add	r0, r0, #1
	movls	r1, r1, asl #1
	bls	.L3489
.L3560:
	ldr	r1, .L3561+28
	sub	lr, lr, #1
	mov	r0, #1
	str	r3, [sp, #20]
	uxth	lr, lr
	ldr	r3, [sp, #12]
	mul	r1, r1, r6
	mov	lr, r0, asl lr
	mul	r8, r6, r8
	ldr	fp, .L3561+4
	mul	r9, lr, r1
	mov	r1, r2
	str	r8, [r4, #2264]
	ldr	r8, .L3561+32
	add	ip, r9, #24576
	str	ip, [r3, #-2492]
	str	ip, [sp, #16]
	mov	r0, ip
	str	r9, [r4, #520]
	bl	__aeabi_uidiv
	sub	r2, r10, #1
	mov	r1, r10
	ldr	r10, .L3561+36
	str	r0, [r4, #2268]
	add	r0, r2, r0, asl #2
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	strh	r0, [r10, #-8]	@ movhi
	uxth	r0, r0
	mul	r1, r6, r3
	mov	r0, r0, asl #4
	bl	__aeabi_idiv
	add	r2, fp, #2976
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #16]
	strh	r0, [r8]	@ movhi
	mul	r1, r6, r3
	mov	r0, ip
	str	r2, [sp, #8]
	sub	r1, r1, #1
	bl	__aeabi_uidiv
	add	r1, r7, r7, asl #1
	mov	r7, r7, lsr #4
	movw	lr, #3528
	cmp	r7, #79
	mov	r1, r1, asr #2
	movw	ip, #3524
	ldr	r2, [sp, #8]
	add	r0, r0, #8
	ldr	r3, [sp, #4]
	strh	r0, [r2]	@ movhi
	movw	r0, #3526
	strh	r1, [r4, r0]	@ movhi
	movw	r1, #2974
	strh	r7, [r4, r1]	@ movhi
	mov	r6, r2
	movls	r2, #80
	strlsh	r2, [fp, r1]	@ movhi
	ldr	r2, .L3561+4
	mov	r1, #2000
	cmp	r3, #2
	strh	r1, [r4, lr]	@ movhi
	mov	r1, #50
	strh	r1, [r4, ip]	@ movhi
	mov	r1, #256
	strh	r1, [r8, #-4]	@ movhi
	mov	r1, #48
	strh	r1, [r8, #-2]	@ movhi
	add	r1, r2, #572
	mov	r0, #32
	strh	r0, [r1]	@ movhi
	beq	.L3492
	ldrb	r3, [r2, #2944]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3493
.L3492:
	movw	r2, #3524
	mov	r0, #150
	strh	r0, [r4, r2]	@ movhi
	mov	r0, #12
	strh	r0, [r1]	@ movhi
	mov	r2, #64
	ldrb	r1, [r4]	@ zero_extendqisi2
	ldr	r3, .L3561+4
	cmp	r1, #0
	strh	r2, [r8, #-2]	@ movhi
	add	r2, r3, #572
	bne	.L3495
	mov	r1, #4
	strh	r1, [r2]	@ movhi
	movw	r2, #3528
	mov	r1, #600
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #128
	ldr	r3, .L3561+32
	strh	r2, [r3, #-4]	@ movhi
	b	.L3495
.L3493:
	ldrb	r3, [r2, #684]	@ zero_extendqisi2
	cmp	r3, #0
	strneh	r0, [r2, ip]	@ movhi
	movne	r3, #1200
	strneh	r3, [r2, lr]	@ movhi
	ldrne	r3, .L3561+32
	strneh	r0, [r3, #-2]	@ movhi
.L3495:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3497
	ldr	r0, .L3561+40
	mov	r1, r9
	bl	printk
.L3497:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3498
	ldr	r0, .L3561+44
	ldr	r1, [r4, #2264]
	bl	printk
.L3498:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3499
	ldr	r0, .L3561+48
	ldr	r1, [r4, #2268]
	bl	printk
.L3499:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3500
	ldr	r3, [sp, #12]
	ldr	r0, .L3561+52
	ldr	r1, [r3, #-2492]
	bl	printk
.L3500:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3501
	ldr	r0, .L3561+56
	ldrh	r1, [r10, #-8]
	bl	printk
.L3501:
	ldr	r3, [r5]
	ldr	r7, .L3561+60
	tst	r3, #4096
	beq	.L3502
	ldr	r0, .L3561+64
	ldrh	r1, [r7, #-4]
	bl	printk
.L3502:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3503
	ldr	r0, .L3561+68
	ldrh	r1, [r8]
	bl	printk
.L3503:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3504
	movw	r3, #2974
	ldr	r0, .L3561+72
	ldrh	r1, [r4, r3]
	bl	printk
.L3504:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3505
	ldr	r0, .L3561+76
	ldrh	r1, [r6]
	bl	printk
.L3505:
	ldr	r8, .L3561+80
	bl	zbuf_init
	mov	r0, #6
	ldr	r6, .L3561+84
	ldrh	r3, [r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	str	r0, [r4, #524]
	ldrh	r0, [r6]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldrh	r1, [r6]
	ldr	r2, .L3561+88
	mul	r1, r3, r1
	str	r2, [r4, #3512]
	add	r3, r2, r1, asl #3
	str	r3, [r4, #576]
	str	r0, [r4, #3516]
	ldrh	r0, [r8]
	mov	r0, r0, lsr #1
	add	r1, r0, r1, asl #1
	ldr	r0, [r5]
	tst	r0, #4096
	add	r1, r2, r1, asl #2
	str	r1, [r4, #580]
	beq	.L3506
	ldr	r0, .L3561+92
	bl	printk
.L3506:
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldrh	r6, [r6]
	ldrh	r2, [r8]
	ldrh	r10, [r10, #-8]
	mul	r6, r3, r6
	ldr	r3, [r5]
	mov	r10, r10, asl #2
	tst	r3, #4096
	add	r10, r10, r2, asl #2
	add	r10, r10, #704
	add	r6, r2, r6, asl #2
	mov	r6, r6, asl #1
	add	r6, r6, #632
	beq	.L3507
	ldr	r0, .L3561+96
	mov	r1, r6
	mov	r2, r10
	ldrh	r3, [r7, #-4]
	bl	printk
.L3507:
	ldrh	r3, [r7, #-4]
	cmp	r10, r3
	cmpls	r6, r3
	movhi	r10, #1
	movls	r10, #0
	bls	.L3508
.L3557:
	b	.L3557
.L3508:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	r0, #1
	beq	.L3510
	bl	ftl_ext_info_init
	bl	gc_init
	mov	r0, #1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	r0, [r4, #580]
	mov	r1, r10
	add	r0, r0, #16
	bl	ftl_open_sblk_recovery
	ldr	r1, [r4, #580]
	add	r0, r1, #48
	add	r1, r1, #16
	bl	ftl_open_sblk_recovery
	ldr	r2, [r4, #2288]
	ldr	r0, [r4, #580]
	ldr	r3, [r2, #8]
	add	r0, r0, #16
	add	r3, r3, #16
	str	r3, [r2, #8]
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #580]
	add	r0, r0, #48
	bl	ftl_info_data_recovery
	ldr	r0, [r4, #580]
	add	r0, r0, #80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	r0, #1
	bl	ftl_total_vpn_update
	ldr	r3, .L3561+100
	ldrb	r3, [r3, #-2855]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, .L3561+4
	ldrne	r2, [r3, #2288]
	ldrne	r3, [r2, #68]
	addne	r3, r3, #1
	strne	r3, [r2, #68]
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	r0, #0
.L3510:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3562:
	.align	2
.L3561:
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC2
	.word	.LC1
	.word	.LANCHOR2
	.word	.LANCHOR0+2928
	.word	1892352
	.word	.LANCHOR3-3296
	.word	.LANCHOR5-2480
	.word	.LC237
	.word	.LC238
	.word	.LC239
	.word	.LC240
	.word	.LC241
	.word	.LANCHOR5-2496
	.word	.LC242
	.word	.LC243
	.word	.LC244
	.word	.LC245
	.word	.LANCHOR0+564
	.word	.LANCHOR0+2960
	.word	.LANCHOR4-2792
	.word	.LC246
	.word	.LC247
	.word	.LANCHOR4
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
	ldr	r3, .L3579
	mov	r7, #0
	ldr	r4, .L3579+4
	ldr	r5, .L3579+8
	ldr	r3, [r3]
	str	r7, [r4, #-3284]
	strb	r7, [r5, #-2486]
	str	r3, [r4, #-3288]
	bl	ftl_malloc
	cmp	r0, r7
	str	r0, [r4, #-3280]
	bne	.L3564
.L3566:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3564:
	mov	r0, #2048
	bl	ftl_malloc
	mov	r1, r5
	str	r7, [r1, #-2480]!
	str	r7, [r4, #-3276]
	str	r0, [r5, #-2484]
	ldr	r0, .L3579+12
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #-3276]
	cmp	r3, #0
	beq	.L3566
	bl	rk_nandc_irq_init
	mov	r1, r7
	mov	r2, r7
	mov	r3, #2048
	ldr	r0, [r5, #-2484]
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	r3, [r4, #-3280]
	ldr	r2, .L3579+16
	ldr	r0, [r4, #-3276]
	str	r2, [r3, #40]
	ldr	r2, .L3579+20
	str	r2, [r3, #32]
	ldr	r2, .L3579+24
	str	r2, [r3, #44]
	ldr	r2, .L3579+28
	str	r2, [r3, #48]
	ldr	r2, .L3579+32
	str	r2, [r3, #36]
	ldr	r2, .L3579+36
	str	r2, [r3, #56]
	ldr	r2, .L3579+40
	str	r2, [r3, #20]
	ldr	r2, .L3579+44
	str	r2, [r3, #24]
	ldr	r2, .L3579+48
	str	r2, [r3, #12]
	ldr	r2, .L3579+52
	str	r2, [r3, #16]
	ldr	r2, .L3579+56
	str	r2, [r3, #4]
	ldr	r2, .L3579+60
	str	r2, [r3, #8]
	ldr	r2, .L3579+64
	str	r2, [r3, #60]
	ldr	r2, .L3579+68
	str	r2, [r3, #64]
	ldr	r2, .L3579+72
	str	r2, [r3, #52]
	ldr	r2, .L3579+76
	str	r2, [r3, #28]
	bl	nand_flash_init
	subs	r7, r0, #0
	bne	.L3567
	bl	zftl_init
	mov	r4, r0
	bl	zftl_proc_debug_init
	mov	r3, #1
	strb	r3, [r5, #-2486]
	b	.L3568
.L3567:
	ldr	r3, [r4, #-3280]
	mov	r6, r4
	ldr	r2, .L3579+80
	ldr	r0, [r4, #-3276]
	ldr	r5, .L3579+84
	str	r2, [r3, #40]
	ldr	r2, .L3579+88
	str	r2, [r3, #32]
	ldr	r2, .L3579+92
	str	r2, [r3, #44]
	ldr	r2, .L3579+96
	str	r2, [r3, #48]
	ldr	r2, .L3579+100
	str	r2, [r3, #36]
	ldr	r2, .L3579+104
	str	r2, [r3, #56]
	ldr	r2, .L3579+108
	str	r2, [r3, #20]
	ldr	r2, .L3579+112
	str	r2, [r3, #24]
	ldr	r2, .L3579+116
	str	r2, [r3, #12]
	ldr	r2, .L3579+120
	str	r2, [r3, #16]
	ldr	r2, .L3579+124
	str	r2, [r3, #4]
	ldr	r2, .L3579+128
	str	r2, [r3, #8]
	ldr	r2, .L3579+132
	str	r2, [r3, #60]
	ldr	r2, .L3579+136
	str	r2, [r3, #64]
	ldr	r2, .L3579+140
	str	r2, [r3, #52]
	ldr	r2, .L3579+144
	str	r2, [r3, #28]
	bl	FlashInit
	cmn	r7, #2
	mov	r4, r0
	bne	.L3569
	ldr	r1, .L3579+148
	mov	r2, #32
	add	r0, r5, #4
	bl	ftl_memcpy
	ldrb	r0, [r5, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrh	r3, [r5, #14]
	strh	r3, [r5, #30]	@ movhi
.L3569:
	ldr	r3, .L3579+152
	ldr	r2, [r3]
	ldr	r3, .L3579+156
	cmp	r2, r3
	ldr	r2, .L3579+160
	bne	.L3570
	ldr	r3, .L3579+164
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3571
.L3570:
	mov	r3, #0
	strb	r3, [r2, #590]
	strb	r3, [r2]
.L3571:
	ldr	r1, .L3579+168
	mov	r0, #1
	ldrh	ip, [r5, #14]
	mov	r3, #0
	str	r3, [r6, #-3252]
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #588]
	ldr	r1, .L3579+172
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #660]
.L3572:
	cmp	r0, ip
	uxth	r1, r3
	add	r3, r3, #1
	movls	r0, r0, asl #1
	bls	.L3572
.L3578:
	ldr	r3, .L3579+176
	sub	r1, r1, #1
	strh	r1, [r3]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strb	r3, [r2, #623]
	bne	.L3568
	bl	FtlInit
	mov	r4, r0
.L3568:
	mov	r1, r4
	ldr	r0, .L3579+180
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3580:
	.align	2
.L3579:
	.word	jiffies
	.word	.LANCHOR3
	.word	.LANCHOR5
	.word	.LANCHOR3-3276
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
	.word	.LANCHOR0+2928
	.word	.LC248
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	gc_update_l2p_map_new
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L3618
	ldr	r5, .L3618+4
	ldr	r3, [r6]
	ldr	r4, [r5, #580]
	str	r3, [sp, #28]
	movw	r3, #2962
	ldrh	r3, [r5, r3]
	ldrb	r2, [r4, #89]	@ zero_extendqisi2
	mul	r3, r2, r3
	str	r3, [sp]
	bl	timer_get_time
	ldr	r3, .L3618+8
	ldr	r3, [r3]
	tst	r3, #256
	str	r6, [sp, #12]
	beq	.L3582
	ldr	r0, .L3618+12
	ldrh	r1, [r4, #80]
	bl	printk
.L3582:
	ldrh	r3, [r4, #80]
	mov	r6, #0
	ldr	r2, [r5, #576]
	mov	r5, r6
	ldr	r8, .L3618+16
	mov	r3, r3, asl #1
	ldr	r1, [sp]
	sub	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L3583:
	ldr	r3, [sp]
	cmp	r5, r3
	beq	.L3617
	ldr	r3, [r8, #-3316]
	mov	r7, r5, asl #2
	ldr	r9, [r3, r5, asl #2]
	cmn	r9, #1
	beq	.L3584
	ldr	r3, .L3618+4
	mov	r0, r9
	ldrb	r1, [r3, #2260]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #8]
	ldr	r3, .L3618+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3585
	ldr	r0, .L3618+20
	mov	r2, r9
	ldr	r1, [sp, #8]
	mov	r3, r5
	bl	printk
.L3585:
	ldr	r10, .L3618+4
	str	r5, [sp, #4]
.L3591:
	ldr	r3, [r8, #-3316]
	ldr	r9, [r3, r7]
	cmn	r9, #1
	beq	.L3586
	ldrb	r1, [r10, #2260]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	uxth	r0, r0
	cmp	r0, r3
	bne	.L3586
	ldr	r3, [r8, #-3312]
	mov	r0, r9
	ldr	fp, [r3, r7]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L3587
	mov	r0, r9
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L3587:
	ldr	r3, [sp, #24]
	cmp	r3, fp
	bne	.L3588
	ldr	r3, .L3618+16
	add	r1, sp, #32
	mov	r2, #1
	mov	r0, r9
	add	r6, r6, #1
	ldr	r3, [r3, #-3308]
	ldr	r3, [r3, r7]
	str	r3, [r1, #-12]!
	bl	pm_log2phys
	ldr	r3, .L3618+24
	ldrb	r1, [r10, #2930]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r10, #676]	@ zero_extendqisi2
	mov	fp, fp, lsr r0
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, fp, r0, asl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
	b	.L3616
.L3588:
	ldr	r2, .L3618+8
	ldr	r2, [r2]
	tst	r2, #256
	beq	.L3590
	ldr	r0, .L3618+28
	mov	r1, r9
	mov	r2, fp
	bl	printk
.L3590:
	ldrh	r0, [r4, #80]
.L3616:
	bl	ftl_vpn_decrement
	ldr	r3, [r8, #-3316]
	mvn	r2, #0
	str	r2, [r3, r7]
	bl	timer_get_time
.L3586:
	ldr	r3, [sp, #4]
	add	r7, r7, #4
	ldr	r2, [sp]
	add	r3, r3, #1
	str	r3, [sp, #4]
	cmp	r3, r2
	bne	.L3591
.L3584:
	add	r5, r5, #1
	b	.L3583
.L3617:
	ldr	r3, .L3618+8
	ldr	r5, .L3618+4
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3593
	ldrh	r3, [r4, #80]
	ldr	r1, [r5, #580]
	ldr	r2, [r5, #576]
	mov	r3, r3, asl #1
	ldr	r0, .L3618+32
	ldrh	r1, [r1, #80]
	ldrh	r2, [r2, r3]
	mov	r3, r6
	bl	printk
.L3593:
	ldrh	r3, [r4, #80]
	ldr	r2, [r5, #576]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, r6
	beq	.L3594
	ldr	r1, .L3618+36
	movw	r2, #749
	ldr	r0, .L3618+40
	bl	printk
	bl	dump_stack
.L3594:
	ldrh	r3, [r4, #80]
	ldr	r2, [r5, #576]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r4, #80]
	bl	zftl_insert_data_list
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3595
	bl	__stack_chk_fail
.L3595:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3619:
	.align	2
.L3618:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC249
	.word	.LANCHOR3
	.word	.LC250
	.word	.LANCHOR0+2928
	.word	.LC251
	.word	.LC252
	.word	.LANCHOR1+1992
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
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3651
	mov	r1, #0
	ldr	r5, .L3651+4
	mov	lr, r1
	ldr	r4, .L3651+8
	movw	r0, #65535
	ldr	r3, [r6]
	ldrb	ip, [r5, #2964]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	add	r2, r4, r3, asl #1
	ldrh	r7, [r2, #36]
	ldrh	r2, [r4, #2]
.L3621:
	cmp	r7, r0
	bne	.L3649
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
	b	.L3621
.L3649:
	mov	r0, #1
	cmp	lr, #0
	strneh	r2, [r4, #2]	@ movhi
	cmp	r1, #0
	strneb	r3, [r5, #2316]
	bl	buf_alloc
	ldr	r8, .L3651+4
	mov	fp, #1
	ldr	r9, .L3651+12
	mov	r10, r0
.L3626:
	ldrb	r1, [r5, #2318]	@ zero_extendqisi2
	ldr	r2, .L3651+4
	cmp	fp, r1
	bgt	.L3650
	ldrh	r3, [r9]
	cmp	r1, #2
	mul	r3, r3, r7
	beq	.L3627
	cmp	r1, #3
	ldrneh	r2, [r4, #2]
	addne	r3, r3, r2
	bne	.L3648
	ldrb	r2, [r8, #684]	@ zero_extendqisi2
	cmp	r2, #0
	ldreqh	r2, [r4, #2]
	addeq	r3, r3, r2
	orreq	r3, r3, fp, asl #24
	beq	.L3648
.L3627:
	ldrh	r2, [r4, #2]
	add	r3, r3, r2, asl #1
	ldrb	r2, [r5, #2937]	@ zero_extendqisi2
	sub	r3, r3, #1
	add	r3, r3, fp
	orr	r3, r3, r2, asl #24
.L3648:
	str	r3, [r10, #24]
	mov	r0, r10
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r10, #36]
	cmp	r3, #512
	cmnne	r3, #1
	beq	.L3632
	ldr	r3, [r10, #12]
	ldr	r3, [r3, #4]
	mov	r0, r3
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	ldr	r3, [sp, #4]
	bne	.L3633
	ldr	r2, [r5, #2268]
	cmp	r3, r2
	bcs	.L3633
	mov	r0, r3
	add	r1, sp, #8
	mov	r2, #0
	bl	pm_log2phys
.L3633:
	ldr	r3, [r10, #24]
	ldr	r2, [sp, #8]
	cmp	r3, r2
	ldreqh	r1, [r4, #20]
	ldreq	r2, [r5, #2932]
	streq	r3, [r2, r1, asl #2]
	ldreqh	r3, [r4, #20]
	addeq	r3, r3, #1
	streqh	r3, [r4, #20]	@ movhi
.L3632:
	ldrh	r3, [r4, #22]
	add	fp, fp, #1
	add	r3, r3, #1
	strh	r3, [r4, #22]	@ movhi
	b	.L3626
.L3650:
	mov	r0, r10
	str	r2, [sp, #4]
	bl	zbuf_free
	ldr	r2, [sp, #4]
	ldrb	r3, [r2, #2316]	@ zero_extendqisi2
	ldrb	r1, [r2, #2964]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r2, #2316]
	cmp	r1, r3
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r2, #2316]
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3637
	bl	__stack_chk_fail
.L3637:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3652:
	.align	2
.L3651:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+2312
	.word	.LANCHOR0+2994
	.fnend
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3689
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r3]
	ldr	r4, .L3689+4
	str	r3, [sp, #8]
	str	r2, [sp, #20]
	movw	r2, #2312
	ldrh	r1, [r4, r2]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3654
	ldr	r1, .L3689+8
	movw	r2, #1236
	ldr	r0, .L3689+12
	bl	printk
	bl	dump_stack
.L3654:
	movw	r3, #2312
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	moveq	r0, #0
	beq	.L3655
	ldr	r3, .L3689+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3656
	ldr	r3, .L3689+4
	ldr	r0, .L3689+20
	ldr	r2, [r3, #576]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	bl	printk
.L3656:
	movw	r7, #2312
	bl	timer_get_time
	ldrh	r0, [r4, r7]
	ldr	r1, .L3689+24
	bl	ftl_get_blk_list_in_sblk
	ldr	r6, .L3689+4
	ldr	r5, .L3689+28
	uxtb	r0, r0
	strb	r0, [r4, #2317]
	cmp	r0, #0
	mvneq	r3, #0
	streqh	r3, [r6, r7]	@ movhi
	beq	.L3655
	ldrh	r3, [r6, r7]
	ldr	r2, [r6, #568]
	add	r2, r2, r3, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r1, r2, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r1, #224
	orreq	r2, r2, #1
	cmp	r2, #0
	bne	.L3658
	ldr	r2, [r6, #580]
	ldrh	ip, [r2, #16]
	cmp	ip, r3
	beq	.L3658
	ldrh	ip, [r2, #48]
	cmp	ip, r3
	beq	.L3658
	ldrh	r2, [r2, #80]
	cmp	r2, r3
	bne	.L3659
.L3658:
	cmp	r1, #0
	bne	.L3660
	ldr	r2, [r4, #576]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3661
	ldr	r1, .L3689+8
	movw	r2, #1256
	ldr	r0, .L3689+12
	bl	printk
	bl	dump_stack
.L3661:
	movw	r3, #2312
	ldr	r2, [r4, #576]
	ldrh	r3, [r4, r3]
	mov	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L3660:
	movw	r3, #2312
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	strh	r0, [r5, #20]	@ movhi
	b	.L3655
.L3659:
	add	r0, r5, r0, asl #1
	movw	r3, #65535
	ldrh	r6, [r0, #34]
	cmp	r6, r3
	bne	.L3662
	ldr	r1, .L3689+8
	mov	r2, #1264
	ldr	r0, .L3689+12
	bl	printk
	bl	dump_stack
.L3662:
	movw	r3, #2994
	ldr	r2, .L3689+4
	ldrh	r3, [r4, r3]
	add	r8, r2, #2960
	mul	r6, r3, r6
	movw	r3, #2312
	ldrh	r1, [r4, r3]
	ldrh	fp, [r8]
	ldr	r3, [r4, #568]
	sub	fp, fp, #1
	add	r3, r3, r1, asl #2
	uxth	r7, fp
	ldrb	fp, [r3, #2]	@ zero_extendqisi2
	and	r9, fp, #224
	cmp	r9, #160
	movne	fp, #1
	bne	.L3663
	ldrb	r3, [r2, #2937]	@ zero_extendqisi2
	cmp	r3, #2
	uxth	fp, r3
	orr	r6, r6, r3, asl #24
	movweq	r3, #2962
	ldreqh	r7, [r2, r3]
	ldrb	r3, [r4, #684]	@ zero_extendqisi2
	subeq	r7, r7, #1
	uxtheq	r7, r7
	cmp	r3, #0
	ldrne	r2, .L3689+4
	movwne	r3, #2962
	ldrneh	r7, [r2, r3]
	subne	r7, r7, #1
	uxthne	r7, r7
.L3663:
	mov	r0, #1
	mov	r10, #0
	strb	fp, [r4, #2318]
	orr	r6, r7, r6
	strh	r10, [r5, #2]	@ movhi
	strb	r10, [r4, #2316]
	strh	r10, [r5, #22]	@ movhi
	strh	r10, [r5, #24]	@ movhi
	bl	buf_alloc
	mov	r1, #1
	ldr	r9, .L3689+4
	str	r6, [r0, #24]
	mov	r7, r0
	bl	sblk_read_page
	movw	r3, #2962
	ldrh	r2, [r4, r3]
	mov	r1, #255
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldr	r0, [r4, #2932]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r6, [r7, #36]
	strh	r10, [r5, #20]	@ movhi
	cmn	r6, #1
	cmpne	r6, #512
	moveq	r6, #1
	movne	r6, #0
	beq	.L3687
	ldr	r3, [r7, #12]
	ldr	r2, [r3]
	ldr	r3, .L3689+32
	cmp	r2, r3
	beq	.L3666
	ldr	r1, .L3689+8
	movw	r2, #1299
	ldr	r0, .L3689+12
	bl	printk
	bl	dump_stack
.L3687:
	mov	r0, r7
	bl	zbuf_free
	mvn	r0, #0
	b	.L3655
.L3666:
	ldrh	r3, [r8]
	ldrb	r2, [r9, #2964]	@ zero_extendqisi2
	ldr	r8, [r7, #4]
	ldr	r10, .L3689+28
	smulbb	r3, r2, r3
	sub	r8, r8, #4
	smulbb	r3, r3, fp
	mov	fp, #1
	uxth	r3, r3
	str	r3, [sp, #4]
.L3667:
	ldr	r3, [sp, #4]
	cmp	r6, r3
	bge	.L3688
	ldr	r0, [r8, #4]!
	cmn	r0, #1
	beq	.L3669
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #16]
	bne	.L3670
	ldr	r0, [r8]
	add	r1, sp, #16
	mov	r2, #0
	bl	pm_log2phys
.L3670:
	ldr	r3, .L3689+36
	ldr	r2, [sp, #16]
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	str	r2, [sp, #12]
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mov	r3, fp, asl r3
	sub	r3, r3, #1
	and	r0, r3, r2, lsr r0
	bl	__aeabi_uidiv
	ldrh	r3, [r5]
	cmp	r0, r3
	ldr	r2, [sp, #12]
	ldreq	r3, [r9, #2932]
	ldreqh	r1, [r10, #20]
	streq	r2, [r3, r1, asl #2]
	ldreqh	r3, [r10, #20]
	addeq	r3, r3, #1
	streqh	r3, [r10, #20]	@ movhi
.L3669:
	bl	timer_get_time
	add	r6, r6, #1
	b	.L3667
.L3688:
	mov	r0, r7
	bl	zbuf_free
	movw	r3, #2312
	ldrh	r1, [r4, r3]
	ldr	r2, [r4, #576]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L3673
	ldr	r0, .L3689+40
	bl	printk
.L3673:
	movw	r3, #2312
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #576]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldrh	r0, [r5, #20]
	strh	r3, [r5, #24]	@ movhi
.L3655:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3674
	bl	__stack_chk_fail
.L3674:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3690:
	.align	2
.L3689:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+2016
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC253
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2312
	.word	-178307901
	.word	.LANCHOR0+2928
	.word	.LC254
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
	ldr	r6, .L3709
	ldr	r4, .L3709+4
	ldr	r3, [r6]
	str	r3, [sp, #4]
	ldr	r3, [r4, #580]
	ldr	r2, [r3, #544]
	cmn	r2, #1
	beq	.L3692
	mov	r5, #11
	mov	r7, r4
.L3693:
	ldr	r3, [r4, #580]
	mov	r1, sp
	mov	r2, #0
	ldr	r0, [r3, #544]
	bl	pm_log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3694
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp]
	mov	r8, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmp	r3, #256
	bne	.L3695
	ldr	r3, .L3709+8
	mov	r9, #1
	ldr	r0, [sp]
	ldrb	r1, [r7, #2930]	@ zero_extendqisi2
	ldrh	r2, [r3]
	ldrb	r3, [r7, #676]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r3, r9, asl r3
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	bl	__aeabi_uidiv
	mov	r1, r9
	mov	r2, #0
	uxth	r0, r0
	bl	gc_add_sblk
.L3695:
	ldr	r2, [r8, #12]
	ldr	r3, [r4, #580]
	ldr	r2, [r2, #4]
	ldr	r3, [r3, #544]
	cmp	r2, r3
	beq	.L3696
	ldr	r1, .L3709+12
	movw	r2, #1738
	ldr	r0, .L3709+16
	bl	printk
	bl	dump_stack
.L3696:
	mov	r0, r8
	bl	zbuf_free
.L3694:
	ldr	r3, [r4, #580]
	ldr	r1, [r4, #2268]
	ldr	r2, [r3, #544]
	add	r2, r2, #1
	str	r2, [r3, #544]
	cmp	r2, r1
	bcc	.L3697
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
	b	.L3691
.L3697:
	ldr	r3, [sp]
	cmn	r3, #1
	bne	.L3691
	sub	r5, r5, #1
	uxth	r5, r5
	cmp	r5, #0
	bne	.L3693
	b	.L3691
.L3692:
	ldr	r2, [r3, #536]
	ldr	r1, [r3, #12]
	add	r2, r2, #12910592
	add	r2, r2, #49408
	cmp	r1, r2
	bhi	.L3702
	ldr	r0, [r4, #2288]
	ldr	r2, [r3, #540]
	ldr	r0, [r0, #44]
	add	r2, r2, #4992
	add	r2, r2, #8
	cmp	r0, r2
	bls	.L3691
.L3702:
	ldr	r2, [r4, #2288]
	ldr	r2, [r2, #44]
	str	r1, [r3, #536]
	str	r2, [r3, #540]
	mov	r2, #0
	str	r2, [r3, #544]
.L3691:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3703
	bl	__stack_chk_fail
.L3703:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3710:
	.align	2
.L3709:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+2928
	.word	.LANCHOR1+2032
	.word	.LC0
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
	.pad #44
	sub	sp, sp, #44
	ldr	r3, .L3750
	ldr	r4, .L3750+4
	ldr	r2, [r3]
	add	r5, r4, #564
	ldr	r1, [r4, #580]
	str	r3, [sp, #12]
	str	r2, [sp, #36]
	ldr	r1, [r1, #608]
	ldrh	r2, [r5]
	cmp	r1, r2
	bcs	.L3711
	bl	timer_get_time
	ldr	r6, [r4, #580]
	ldr	r3, [r6, #604]
	add	r3, r3, #29952
	add	r3, r3, #48
	cmp	r0, r3
	bls	.L3711
	bl	timer_get_time
	ldr	r3, [r4, #580]
	ldrh	r1, [r5]
	str	r0, [r6, #604]
	ldr	r2, [r3, #600]
	cmp	r2, r1
	bcs	.L3715
	ldr	r1, [r4, #2288]
	ldrh	r1, [r1, #134]
	cmp	r2, r1
	bcs	.L3716
.L3715:
	ldr	r2, [r4, #2288]
	ldrh	r2, [r2, #134]
	str	r2, [r3, #600]
.L3716:
	ldr	r5, [r3, #600]
	movw	r3, #65535
	uxth	r5, r5
	cmp	r5, r3
	bne	.L3717
	ldr	r1, .L3750+8
	movw	r2, #2035
	ldr	r0, .L3750+12
	bl	printk
	bl	dump_stack
.L3717:
	ldr	r3, [r4, #580]
	mov	r0, r5
	add	r1, sp, #20
	ldr	r6, .L3750+4
	ldr	r2, [r3, #600]
	add	r2, r2, #1
	str	r2, [r3, #600]
	ldr	r2, [r3, #608]
	add	r2, r2, #1
	str	r2, [r3, #608]
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	beq	.L3711
	ldr	r8, [r6, #568]
	add	r8, r8, r5, asl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r2, r3, #224
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, #224
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L3718
	ldr	r3, [r6, #580]
	ldrh	r1, [r3, #16]
	cmp	r1, r5
	beq	.L3718
	ldrh	r1, [r3, #48]
	cmp	r1, r5
	beq	.L3718
	ldrh	r3, [r3, #80]
	cmp	r3, r5
	bne	.L3719
.L3718:
	cmp	r2, #0
	bne	.L3711
	ldr	r3, [r4, #576]
	mov	r5, r5, asl #1
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L3720
	ldr	r1, .L3750+8
	movw	r2, #2051
	ldr	r0, .L3750+12
	bl	printk
	bl	dump_stack
.L3720:
	ldr	r3, [r4, #576]
	mov	r2, #0
	strh	r2, [r3, r5]	@ movhi
	b	.L3711
.L3719:
	add	r3, sp, #40
	add	r0, r3, r0, asl #1
	movw	r3, #65535
	ldrh	r9, [r0, #-22]
	cmp	r9, r3
	bne	.L3722
	ldr	r1, .L3750+8
	movw	r2, #2057
	ldr	r0, .L3750+12
	bl	printk
	bl	dump_stack
.L3722:
	ldr	r2, .L3750+4
	movw	r3, #2994
	add	r10, r2, #2960
	ldrh	fp, [r10]
	sub	r6, fp, #1
	uxth	r7, r6
	ldrh	r6, [r4, r3]
	mul	r6, r6, r9
	ldrb	r9, [r8, #2]	@ zero_extendqisi2
	and	fp, r9, #224
	cmp	fp, #160
	movne	fp, #1
	bne	.L3723
	ldrb	r3, [r2, #2937]	@ zero_extendqisi2
	cmp	r3, #2
	uxth	fp, r3
	orr	r6, r6, r3, asl #24
	movweq	r3, #2962
	ldreqh	r7, [r2, r3]
	subeq	r7, r7, #1
	uxtheq	r7, r7
.L3723:
	mov	r0, #1
	orr	r6, r7, r6
	bl	buf_alloc
	mov	r1, #1
	ldr	r9, .L3750+4
	str	r6, [r0, #24]
	mov	r7, r0
	bl	sblk_read_page
	movw	r3, #2962
	ldrh	r2, [r4, r3]
	mov	r1, #255
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	ldr	r0, [r4, #2932]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r7, #36]
	cmn	r2, #1
	cmpne	r2, #512
	moveq	r2, #1
	movne	r2, #0
	moveq	r0, r5
	moveq	r1, #1
	moveq	r2, #0
	beq	.L3748
	ldr	r3, [r7, #12]
	ldr	r1, [r3]
	ldr	r3, .L3750+16
	cmp	r1, r3
	beq	.L3725
	mov	r0, r5
	mov	r1, #1
.L3748:
	bl	gc_add_sblk
	mov	r0, r7
	bl	zbuf_free
	b	.L3711
.L3725:
	ldrh	r3, [r10]
	add	r9, r9, #2928
	ldrb	r6, [r9, #36]	@ zero_extendqisi2
	ldr	r10, [r7, #4]
	smulbb	r6, r6, r3
	smulbb	r6, r6, fp
	mov	fp, r2
	uxth	r3, r6
	mov	r6, r2
	str	r3, [sp, #8]
.L3726:
	ldr	r3, [sp, #8]
	cmp	fp, r3
	bge	.L3749
	ldr	r0, [r10, fp, asl #2]
	cmn	r0, #1
	beq	.L3727
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #16]
	bne	.L3728
	ldr	r0, [r10, fp, asl #2]
	add	r1, sp, #16
	mov	r2, #0
	bl	pm_log2phys
.L3728:
	ldrb	r2, [r4, #676]	@ zero_extendqisi2
	mov	r3, #1
	ldrh	r1, [r9]
	rsb	r2, r2, #24
	ldr	r0, [sp, #16]
	rsb	r2, r1, r2
	mov	r2, r3, asl r2
	sub	r2, r2, #1
	and	r0, r2, r0, lsr r1
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	cmp	r0, r5
	addeq	r6, r6, #1
	uxtheq	r6, r6
.L3727:
	add	fp, fp, #1
	b	.L3726
.L3749:
	mov	r0, r7
	mov	r7, r5, asl #1
	bl	zbuf_free
	ldr	r3, .L3750+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3730
	ldr	r3, [r4, #576]
	mov	r1, r5
	ldr	r0, .L3750+24
	ldrh	r2, [r3, r7]
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r6
	bl	printk
.L3730:
	ldr	r3, [r4, #576]
	cmp	r6, #31
	strh	r6, [r3, r7]	@ movhi
	bhi	.L3711
	mov	r0, r5
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L3711:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3733
	bl	__stack_chk_fail
.L3733:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3751:
	.align	2
.L3750:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+2052
	.word	.LC0
	.word	-178307901
	.word	.LANCHOR2
	.word	.LC255
	.fnend
	.size	gc_block_vpn_scan, .-gc_block_vpn_scan
	.align	2
	.global	ftl_sblk_dump
	.type	ftl_sblk_dump, %function
ftl_sblk_dump:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3789
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #132
	sub	sp, sp, #132
	ldr	r6, .L3789+4
	mov	fp, r0, asl #2
	ldr	r3, [r3]
	mov	r7, r0
	str	r1, [sp, #64]
	ldr	r2, [r6, #568]
	str	r3, [sp, #124]
	add	r3, r2, fp
	ldrh	r1, [r2, fp]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r10, [r2, r0, asl #2]
	mov	r1, r7
	mov	r2, r3, lsr #5
	ldr	r0, .L3789+8
	ubfx	r3, r3, #3, #2
	ubfx	r4, r10, #11, #8
	str	r4, [sp, #4]
	bl	printk
	movw	r3, #65535
	cmp	r7, r3
	beq	.L3775
	add	r3, r6, #564
	ldrh	r3, [r3]
	cmp	r3, r7
	bls	.L3775
	ldr	r3, [r6, #568]
	add	r1, sp, #108
	mov	r0, r7
	strh	r7, [sp, #92]	@ movhi
	add	r3, r3, fp
	ldr	r4, .L3789+12
	mov	r5, #0
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	sub	r9, r4, #2960
	and	r3, r3, #224
	cmp	r3, #160
	ldreqb	r10, [r6, #2937]	@ zero_extendqisi2
	movne	r10, #1
	bl	ftl_get_blk_list_in_sblk
	ldrh	r3, [r4]
	mov	r1, r7
	uxtb	r0, r0
	strh	r5, [sp, #94]	@ movhi
	strb	r0, [sp, #101]
	smulbb	r3, r3, r0
	strb	r5, [sp, #97]
	strh	r5, [sp, #102]	@ movhi
	strh	r3, [sp, #98]	@ movhi
	ldr	r3, [r6, #568]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	mov	r3, r10
	str	r0, [sp]
	ldr	r0, .L3789+16
	mov	r2, r2, lsr #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	r6, r0
	str	r5, [sp, #48]
	str	r5, [sp, #56]
	str	r5, [sp, #44]
.L3755:
	ldr	r4, .L3789+4
	uxth	r2, r5
	str	r2, [sp, #52]
	add	r3, r4, #2960
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L3771
	mov	r3, r2, asl #1
	add	r2, r3, r2
	sub	r3, r3, #1
	str	r3, [sp, #72]
	mov	r3, #0
	sub	r2, r2, #1
	str	r2, [sp, #68]
.L3787:
	str	r3, [sp, #60]
	ldrb	r2, [sp, #101]	@ zero_extendqisi2
	ldrh	r3, [sp, #60]
	cmp	r2, r3
	bls	.L3769
	add	r3, r3, #8
	mov	r8, #1
	mov	r3, r3, asl r8
	str	r3, [sp, #76]
.L3770:
	cmp	r8, r10
	bhi	.L3788
	ldr	r2, [sp, #76]
	add	r3, sp, #128
	add	r3, r3, r2
	movw	r2, #65535
	ldrh	r3, [r3, #-36]
	cmp	r3, r2
	beq	.L3756
	ldr	r2, .L3789+20
	cmp	r10, #3
	ldrh	r4, [r2]
	mul	r4, r4, r3
	bne	.L3757
	ldrb	r2, [r9, #684]	@ zero_extendqisi2
	cmp	r2, #0
	uxtaheq	r4, r4, r5
	ldrne	r2, [sp, #68]
	orreq	r4, r4, r8, asl #24
	beq	.L3759
	b	.L3786
.L3757:
	cmp	r10, #2
	uxtahne	r4, r4, r5
	bne	.L3759
	ldr	r2, [sp, #72]
.L3786:
	add	r4, r4, r2
	ldrb	r2, [r9, #2937]	@ zero_extendqisi2
	add	r4, r4, r8
	orr	r4, r4, r2, asl #24
.L3759:
	mov	r1, #1
	str	r4, [r6, #24]
	mov	r0, r6
	str	r3, [sp, #84]
	bl	sblk_read_page
	ldr	r1, [r6, #4]
	ldr	ip, [r6, #36]
	ldr	r2, [r6, #12]
	str	ip, [sp]
	ldr	r0, [r1]
	ldr	r3, [sp, #84]
	str	ip, [sp, #80]
	str	r0, [sp, #4]
	ldr	r0, [r1, #4]
	str	r0, [sp, #8]
	ldr	r0, [r1, #8]
	str	r0, [sp, #12]
	ldr	r1, [r1, #12]
	ldr	r0, .L3789+24
	str	r1, [sp, #16]
	ldr	r1, [r2]
	str	r1, [sp, #20]
	ldr	r1, [r2, #4]
	str	r1, [sp, #24]
	ldr	r1, [r2, #8]
	str	r1, [sp, #28]
	mov	r1, r3
	ldr	r2, [r2, #12]
	mov	r3, r4
	str	r2, [sp, #32]
	ldr	r2, [sp, #52]
	bl	printk
	ldr	ip, [sp, #80]
	ldr	r3, [sp, #56]
	cmn	ip, #1
	cmpne	ip, #512
	moveq	r3, #1
	str	r3, [sp, #56]
	ldr	r3, [r9, #568]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #224
	cmpne	r2, #32
	moveq	r2, #1
	movne	r2, #0
	beq	.L3756
	ldr	r3, [r6, #12]
	str	r2, [sp, #80]
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #88]
	ldr	r2, [sp, #80]
	bne	.L3762
	ldr	r3, [r6, #12]
	add	r1, sp, #88
	ldr	r0, [r3, #4]
	bl	pm_log2phys
.L3762:
	ldr	r3, [sp, #88]
	cmp	r3, r4
	bne	.L3763
	ldr	r3, [sp, #48]
	mov	r1, r4
	ldr	r0, .L3789+28
	add	r3, r3, #1
	str	r3, [sp, #48]
	ldr	r3, [r6, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #48]
	bl	printk
.L3763:
	ldr	r3, [sp, #64]
	cmp	r3, #0
	beq	.L3765
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #44]
	mov	r4, r3, asl #2
	ldr	r3, [sp, #64]
	ldr	r2, [r3, r2, asl #2]
	ldr	r3, [r6, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	beq	.L3766
	ldr	r3, .L3789+32
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3766
	ldr	r0, .L3789+36
	ldr	r1, [sp, #44]
	bl	printk
.L3766:
	ldr	r3, [sp, #64]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r4]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	cmnne	r3, #1
	beq	.L3765
	ldr	r1, .L3789+40
	movw	r2, #1383
	ldr	r0, .L3789+44
	bl	printk
	bl	dump_stack
.L3765:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
.L3756:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L3770
.L3788:
	ldr	r3, [sp, #60]
	add	r3, r3, #1
	b	.L3787
.L3769:
	add	r5, r5, #1
	b	.L3755
.L3771:
	mov	r0, r6
	bl	zbuf_free
	ldr	r2, [r4, #576]
	mov	r3, r7, asl #1
	ldr	r0, .L3789+48
	mov	r1, r7
	ldrh	r2, [r2, r3]
	ldr	r3, [sp, #48]
	bl	printk
	ldr	r0, [sp, #56]
	b	.L3753
.L3775:
	mov	r0, #0
.L3753:
	ldr	r3, .L3789
	ldr	r2, [sp, #124]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3773
	bl	__stack_chk_fail
.L3773:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3790:
	.align	2
.L3789:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC256
	.word	.LANCHOR0+2960
	.word	.LC257
	.word	.LANCHOR0+2994
	.word	.LC201
	.word	.LC258
	.word	.LANCHOR2
	.word	.LC259
	.word	.LANCHOR1+2072
	.word	.LC0
	.word	.LC260
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
	ldr	r3, .L3853
	.pad #76
	sub	sp, sp, #76
	mov	r9, r0
	mov	r4, r1
	mov	r8, r2
	ldr	r3, [r3]
	str	r3, [sp, #68]
	ldr	r3, .L3853+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3792
	ldr	r0, .L3853+8
	mov	r1, r9
	mov	r2, r4
	mov	r3, r8
	bl	printk
.L3792:
	cmp	r9, #0
	ldreq	r3, .L3853+12
	ldreq	r2, [r3, #520]
	moveq	r3, #24576
	beq	.L3794
.L3793:
	cmp	r9, #3
	bhi	.L3821
	mov	r3, r9, asl #13
	mov	r2, #8192
.L3794:
	add	r1, r4, r8
	cmp	r1, r2
	bhi	.L3821
	add	r3, r3, r4
	ldr	r4, .L3853+12
	str	r3, [sp, #24]
	ldr	r0, [sp, #24]
	ldr	r2, [r4, #2288]
	mov	r7, r4
	ldrb	r5, [r4, #2260]	@ zero_extendqisi2
	ldr	r3, [r2, #24]
	mov	r1, r5
	add	r3, r3, r8
	str	r3, [r2, #24]
	bl	__aeabi_uidiv
	mov	r1, r5
	ldr	r3, [sp, #24]
	str	r0, [sp, #32]
	add	r3, r3, r8
	str	r3, [sp, #40]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #32]
	ldr	r5, [sp, #32]
	rsb	r3, r3, #1
	str	r0, [sp, #44]
	add	r3, r3, r0
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #36]
.L3796:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3850
	ldr	r3, [sp, #32]
	ldrb	r1, [r4, #2260]	@ zero_extendqisi2
	rsb	r2, r3, r5
	ldr	r3, [sp, #44]
	clz	r2, r2
	rsb	r3, r3, r5
	uxth	r9, r1
	clz	r3, r3
	mov	r2, r2, lsr #5
	mov	r3, r3, lsr #5
	orrs	r10, r3, r2
	beq	.L3797
	cmp	r2, #0
	beq	.L3798
	ldr	r0, [sp, #24]
	bl	__aeabi_uidivmod
	uxth	r10, r1
	rsb	r9, r10, r9
	uxth	r9, r9
	cmp	r9, r8
	uxthhi	r9, r8
	b	.L3797
.L3798:
	cmp	r3, #0
	ldrne	r3, [sp, #24]
	movne	r10, r2
	moveq	r10, r3
	addne	r3, r3, r8
	mlsne	r1, r5, r1, r3
	uxtbne	r9, r1
.L3797:
	ldr	r2, .L3853+16
	mov	r3, #0
.L3801:
	ldr	r1, [r2, #20]
	cmp	r1, r5
	bne	.L3799
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L3799
	mov	r2, #48
	mov	r9, r9, asl #9
	mla	r3, r2, r3, r4
	mov	r0, r6
	mov	r2, r9
	add	r6, r6, r9
	ldr	r1, [r3, #728]
	add	r1, r1, r10, asl #9
	bl	ftl_memcpy
	b	.L3800
.L3799:
	add	r3, r3, #1
	add	r2, r2, #48
	cmp	r3, #32
	bne	.L3801
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #64]
	bne	.L3802
	mov	r0, r5
	add	r1, sp, #64
	mov	r2, #0
	bl	pm_log2phys
.L3802:
	ldr	r3, [sp, #64]
	cmn	r3, #1
	moveq	r9, #0
	bne	.L3851
.L3804:
	ldrb	r3, [r7, #2260]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L3800
	mla	r3, r3, r5, r9
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #40]
	cmp	r3, r2
	movcs	r2, #1
	movcc	r2, #0
	cmp	r3, r1
	movcs	r2, #0
	cmp	r2, #0
	beq	.L3805
	mov	r0, r6
	mov	r1, #0
	mov	r2, #512
	add	r6, r6, #512
	bl	ftl_memset
.L3805:
	add	r9, r9, #1
	b	.L3804
.L3851:
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L3807
	ldr	r1, [r4, #2288]
	ldr	r2, [r1, #40]
	add	r2, r2, #1
	str	r2, [r1, #40]
	ldr	r2, [sp, #64]
	str	r6, [r3, #8]
	add	r6, r6, r9, asl #9
	str	r5, [r3, #20]
	str	r2, [r3, #24]
	str	r2, [r3, #28]
	strb	r9, [r3, #40]
	strb	r10, [r3, #41]
	bl	zftl_add_read_buf
.L3800:
	ldr	r3, [sp, #28]
	add	r5, r5, #1
	sub	r3, r3, #1
	str	r3, [sp, #28]
.L3807:
	ldrb	r3, [r7, #2262]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	cmp	r2, #0
	cmpne	r3, #2
	bhi	.L3796
	ldrb	r1, [r4, #3532]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3796
	ldrb	r2, [r4, #3531]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L3853+16
	mla	r0, r0, r2, r3
	bl	sblk_read_page
.L3809:
	ldrb	r3, [r4, #3532]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3852
	ldrb	r3, [r4, #3531]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L3810
	ldr	r1, .L3853+20
	mov	r2, #1088
	ldr	r0, .L3853+24
	bl	printk
	bl	dump_stack
.L3810:
	ldrb	fp, [r7, #3531]	@ zero_extendqisi2
	mov	r2, #48
	ldr	r3, .L3853+16
	mul	r2, r2, fp
	add	r3, r3, r2
	add	r2, r7, r2
	str	r3, [sp, #48]
	ldr	r10, [r2, #760]
	ldrb	r3, [r2, #724]	@ zero_extendqisi2
	cmn	r10, #1
	strb	r3, [r7, #3531]
	bne	.L3811
	ldr	r2, [r4, #580]
	str	r10, [sp, #36]
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	b	.L3812
.L3811:
	cmp	r10, #256
	bne	.L3812
	ldr	r1, .L3853+28
	mov	r9, #1
	ldr	r3, [r2, #748]
	str	r2, [sp, #60]
	ldrh	r0, [r1]
	ldrb	r1, [r4, #676]	@ zero_extendqisi2
	str	r3, [sp, #56]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r9, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r2, [sp, #60]
	uxth	ip, r0
	ldr	r3, [sp, #56]
	mov	r1, ip
	ldr	r0, .L3853+32
	ldr	r2, [r2, #744]
	str	ip, [sp, #52]
	bl	printk
	mov	r1, r9
	mov	r2, #0
	ldr	ip, [sp, #52]
	mov	r0, ip
	bl	gc_add_sblk
.L3812:
	mov	r9, #48
	cmn	r10, #1
	mla	r9, r9, fp, r4
	movne	ip, #0
	moveq	ip, #1
	ldr	r3, [r9, #736]
	ldr	r1, [r9, #744]
	ldr	r3, [r3, #4]
	cmp	r3, r1
	moveq	r3, ip
	orrne	r3, ip, #1
	cmp	r3, #0
	beq	.L3813
	ldr	r1, [r4, #580]
	ldr	r0, .L3853+36
	str	ip, [sp, #52]
	ldr	r3, [r1, #552]
	add	r3, r3, #1
	str	r3, [r1, #552]
	ldr	r3, [r9, #736]
	ldr	r2, [r9, #748]
	ldrb	r1, [r9, #725]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r3]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	mov	r2, r10
	ldr	r3, [r3, #12]
	str	r3, [sp, #16]
	ldr	r3, [r9, #744]
	bl	printk
	ldr	r3, .L3853+28
	ldr	r2, [r9, #748]
	ldrb	r1, [r4, #2930]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r4, #676]	@ zero_extendqisi2
	mov	r2, r2, lsr r0
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
	ldr	ip, [sp, #52]
.L3813:
	mov	r3, #48
	mla	r3, r3, fp, r4
	ldr	r2, [r3, #736]
	ldr	r3, [r3, #744]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	orrne	ip, ip, #1
	cmp	ip, #0
	beq	.L3814
	ldr	r1, .L3853+20
	movw	r2, #1109
	ldr	r0, .L3853+24
	bl	printk
	bl	dump_stack
.L3814:
	mov	r3, #48
	mla	fp, r3, fp, r7
	ldrb	r3, [r7, #2260]	@ zero_extendqisi2
	ldrb	r2, [fp, #764]	@ zero_extendqisi2
	cmp	r3, r2
	ldrlsb	r3, [fp, #726]	@ zero_extendqisi2
	bicls	r3, r3, #8
	strlsb	r3, [fp, #726]
	bls	.L3816
	ldrb	r1, [fp, #765]	@ zero_extendqisi2
	mov	r2, r2, asl #9
	ldr	r3, [fp, #728]
	ldr	r0, [fp, #732]
	add	r1, r3, r1, asl #9
	bl	ftl_memcpy
.L3816:
	ldr	r1, [sp, #48]
	ldr	r0, .L3853+40
	bl	buf_remove_buf
	ldr	r0, [sp, #48]
	bl	zbuf_free
	ldrb	r3, [r4, #3532]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r4, #3532]
	b	.L3809
.L3852:
	mvn	r2, #0
	strb	r3, [r7, #3532]
	strb	r2, [r7, #3531]
	b	.L3796
.L3850:
	bl	timer_get_time
	ldr	r3, .L3853+44
	str	r0, [r3, #-2476]
	ldr	r0, [sp, #36]
	b	.L3795
.L3821:
	mvn	r0, #0
.L3795:
	ldr	r3, .L3853
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3819
	bl	__stack_chk_fail
.L3819:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3854:
	.align	2
.L3853:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LC261
	.word	.LANCHOR0
	.word	.LANCHOR0+724
	.word	.LANCHOR1+2088
	.word	.LC0
	.word	.LANCHOR0+2928
	.word	.LC262
	.word	.LC263
	.word	.LANCHOR0+3533
	.word	.LANCHOR5
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r2, #79
	ldr	r7, .L3885
	.pad #132
	sub	sp, sp, #132
	add	r4, sp, #44
	mov	r6, r2
	str	r4, [sp, #40]
	mvnhi	r0, #21
	ldr	r3, [r7]
	str	r3, [sp, #124]
	bhi	.L3859
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	mvnne	r0, #13
	bne	.L3859
	add	r3, sp, #128
	mov	r1, r4
	add	r3, r3, r6
	strb	r0, [r3, #-84]
	ldr	r0, .L3885+4
	bl	printk
	ldr	r0, .L3885+8
	mov	r1, r4
	mov	r2, #1
	mov	r3, #16
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	r0, r4
	ldr	r1, .L3885+12
	mov	r2, #7
	bl	memcmp
	subs	r5, r0, #0
	bne	.L3860
	ldr	r8, .L3885+16
	movw	ip, #698
	ldr	r0, .L3885+20
	mov	r2, #4
	ldr	r4, .L3885+24
	movw	r9, #65535
	ldr	r3, [r8, #2288]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r8, #2288]
	ldr	r0, .L3885+28
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r4]
	bl	rknand_print_hex
.L3861:
	ldrh	r3, [r4]
	cmp	r5, r3
	bge	.L3864
	mov	r0, #300
	bl	msleep
	add	r3, r5, #208
	ldr	r2, [r8, #2288]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r9
	beq	.L3862
	mov	r1, #0
	bl	ftl_sblk_dump
.L3862:
	add	r5, r5, #1
	b	.L3861
.L3860:
	mov	r0, r4
	ldr	r1, .L3885+32
	mov	r2, #7
	bl	memcmp
	subs	r5, r0, #0
	bne	.L3865
	ldr	r8, .L3885+16
	movw	ip, #698
	ldr	r0, .L3885+20
	mov	r2, #4
	ldr	r3, [r8, #2288]
	mov	fp, r8
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r3, .L3885+24
	ldr	r1, [r8, #2288]
	mov	r2, #2
	ldr	r0, .L3885+28
	ldrh	r3, [r3]
	add	r1, r1, #416
	bl	rknand_print_hex
	ldr	r0, .L3885+36
	add	r4, sp, #128
	add	r1, sp, #51
	str	r1, [r4, #-88]!
	bl	printk
	mov	r1, r4
	mov	r4, r5
	ldr	r5, .L3885+40
	ldr	r0, [sp, #40]
	bl	rk_simple_strtoull.constprop.33
	uxth	r9, r0
	str	r0, [sp, #36]
.L3867:
	add	r10, r5, r4
	ldr	r0, .L3885+44
	ldrh	r1, [r5, r4]
	ldrh	r2, [r10, #2]
	bl	printk
	ldrh	r3, [r4, r5]
	cmp	r3, r9
	bne	.L3866
	ldrb	r3, [fp, #2260]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r0, .L3885+48
	ldr	r1, [r10, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
.L3866:
	add	r4, r4, #8
	cmp	r4, #256
	bne	.L3867
	mov	r0, #300
	add	r9, r9, #176
	bl	msleep
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r8, #2288]
	mov	r1, #1
	ldr	r3, [r3, r9, asl #2]
	str	r3, [r0, #24]
	mov	r4, r0
	str	r3, [sp, #36]
	bl	sblk_read_page
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L3885+52
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
	ldr	r1, [sp, #36]
	str	r2, [sp, #24]
	ldr	r2, [r4, #36]
	ldr	r3, [r3]
	bl	printk
	ldrb	r3, [r8, #2260]	@ zero_extendqisi2
	ldr	r0, .L3885+56
	mov	r2, #4
	ldr	r1, [r4, #4]
	mov	r3, r3, asl #7
	b	.L3884
.L3865:
	mov	r0, r4
	ldr	r1, .L3885+60
	mov	r2, #7
	bl	memcmp
	cmp	r0, #0
	bne	.L3868
	bl	dump_ftl_info
	b	.L3864
.L3868:
	mov	r0, r4
	ldr	r1, .L3885+64
	mov	r2, #9
	bl	memcmp
	cmp	r0, #0
	bne	.L3869
	add	r1, sp, #128
	add	r0, sp, #53
	str	r0, [r1, #-88]!
	bl	rk_simple_strtoull.constprop.33
	ldr	r3, .L3885+68
	str	r0, [sp, #36]
	strh	r0, [r3]	@ movhi
	bl	dump_all_list_info
	b	.L3864
.L3869:
	mov	r0, r4
	ldr	r1, .L3885+72
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	beq	.L3864
	mov	r0, r4
	ldr	r1, .L3885+76
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	bne	.L3871
	add	r4, sp, #128
	add	r1, sp, #52
	ldr	r0, .L3885+36
	str	r1, [r4, #-88]!
	bl	printk
	mov	r1, r4
	ldr	r5, .L3885+16
	ldr	r0, [sp, #40]
	bl	rk_simple_strtoull.constprop.33
	str	r0, [sp, #36]
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #36]
	mov	r4, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L3885+52
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
	ldr	r1, [sp, #36]
	str	r2, [sp, #24]
	ldr	r2, [r4, #36]
	ldr	r3, [r3]
	bl	printk
	ldrb	r3, [r5, #2260]	@ zero_extendqisi2
	ldr	r0, .L3885+80
	mov	r2, #4
	ldr	r1, [r4, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	ldrb	r3, [r5, #2260]	@ zero_extendqisi2
	ldr	r0, .L3885+84
	mov	r2, #4
	ldr	r1, [r4, #12]
	mov	r3, r3, asl #1
.L3884:
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	b	.L3864
.L3871:
	mov	r0, r4
	ldr	r1, .L3885+88
	mov	r2, #8
	bl	memcmp
	subs	r5, r0, #0
	bne	.L3872
	add	r4, sp, #128
	add	r1, sp, #52
	ldr	r0, .L3885+36
	str	r1, [r4, #-88]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #40]
	bl	rk_simple_strtoull.constprop.33
	mov	r1, r5
	str	r0, [sp, #36]
	uxth	r0, r0
	bl	ftl_sblk_dump
	b	.L3864
.L3872:
	mov	r0, r4
	ldr	r1, .L3885+92
	mov	r2, #10
	bl	memcmp
	cmp	r0, #0
	bne	.L3873
	add	r4, sp, #128
	add	r1, sp, #54
	ldr	r0, .L3885+36
	str	r1, [r4, #-88]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #40]
	bl	rk_simple_strtoull.constprop.33
	ldr	r3, .L3885+96
	str	r0, [sp, #36]
	str	r0, [r3]
	b	.L3864
.L3873:
	mov	r0, r4
	ldr	r1, .L3885+100
	mov	r2, #8
	bl	memcmp
	subs	r5, r0, #0
	bne	.L3874
	add	r4, sp, #128
	add	r1, sp, #52
	ldr	r0, .L3885+36
	str	r1, [r4, #-88]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #40]
	bl	rk_simple_strtoull.constprop.33
	mov	r4, r0
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #36]
	bne	.L3875
	mov	r0, r4
	add	r1, sp, #36
	mov	r2, r5
	bl	pm_log2phys
.L3875:
	mov	r1, r4
	ldr	r2, [sp, #36]
	ldr	r0, .L3885+104
	bl	printk
	b	.L3864
.L3874:
	ldr	r0, .L3885+108
	bl	printk
	ldr	r0, .L3885+112
	bl	printk
	ldr	r0, .L3885+116
	bl	printk
	ldr	r0, .L3885+120
	bl	printk
	ldr	r0, .L3885+124
	bl	printk
	ldr	r0, .L3885+128
	bl	printk
	ldr	r0, .L3885+132
	bl	printk
	ldr	r0, .L3885+136
	bl	printk
	ldr	r0, .L3885+140
	bl	printk
.L3864:
	bl	rknand_device_unlock
	mov	r0, r6
.L3859:
	ldr	r2, [sp, #124]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3876
	bl	__stack_chk_fail
.L3876:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3886:
	.align	2
.L3885:
	.word	__stack_chk_guard
	.word	.LC264
	.word	.LC265
	.word	.LC266
	.word	.LANCHOR0
	.word	.LC267
	.word	.LANCHOR3-3296
	.word	.LC268
	.word	.LC269
	.word	.LC270
	.word	.LANCHOR0+3540
	.word	.LC271
	.word	.LC272
	.word	.LC234
	.word	.LC273
	.word	.LC274
	.word	.LC275
	.word	.LANCHOR0+2992
	.word	.LC276
	.word	.LC277
	.word	.LC229
	.word	.LC230
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
	ldr	r4, .L3920
	ldr	r3, [r4, #2320]
	ldr	r7, [r4, #580]
	cmp	r3, #0
	bne	.L3888
	mov	r0, #1
	bl	buf_alloc
	str	r0, [r4, #2320]
.L3888:
	ldr	r5, [r4, #2320]
	cmp	r5, #0
	bne	.L3889
	ldr	r1, .L3920+4
	movw	r2, #613
	ldr	r0, .L3920+8
	bl	printk
	bl	dump_stack
.L3889:
	ldr	r6, .L3920+12
	add	r10, r7, #96
	ldr	r3, .L3920
	mov	r9, #0
.L3890:
	ldrb	r2, [r7, #89]	@ zero_extendqisi2
	cmp	r9, r2
	ldr	r2, .L3920+12
	bge	.L3918
	ldr	r2, .L3920+16
	mov	r8, #1
.L3900:
	ldrh	r1, [r6, #16]
	cmp	r8, r1
	bgt	.L3919
	ldrb	r0, [r3, #2937]	@ zero_extendqisi2
	ldrh	r1, [r10]
	ldrh	ip, [r2]
	cmp	r0, #2
	mul	ip, ip, r1
	beq	.L3891
	ldrb	r1, [r3, #684]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3892
.L3891:
	ldrh	r1, [r6, #12]
	sub	r1, r1, #1
	add	r1, r1, ip
	add	r1, r1, r8
	orr	r1, r1, r0, asl #24
	b	.L3917
.L3892:
	cmp	r0, #3
	ldrh	r1, [r6, #12]
	addne	ip, ip, r1
	strne	ip, [r5, #24]
	bne	.L3893
	add	r1, ip, r1
	orr	r1, r1, r8, asl #24
.L3917:
	str	r1, [r5, #24]
.L3893:
	mov	r1, #1
	mov	r0, r5
	str	r3, [sp, #28]
	str	r2, [sp, #24]
	bl	sblk_read_page
	ldr	r1, [r5, #36]
	cmn	r1, #1
	ldr	r1, .L3920+20
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #28]
	beq	.L3895
	ldr	lr, [r1, #-3316]
	ldrh	ip, [r6, #18]
	ldr	r0, [r5, #12]
	ldr	fp, [lr, ip, asl #2]
	ldr	lr, [r0, #4]
	cmp	fp, lr
	bne	.L3895
	ldr	lr, [r1, #-3312]
	ldr	r0, [r0, #8]
	ldr	ip, [lr, ip, asl #2]
	cmp	ip, r0
	beq	.L3896
.L3895:
	ldr	r1, [r1, #-3316]
	ldrh	r0, [r6, #18]
	ldr	fp, .L3920+20
	ldr	ip, .L3920
	ldr	r0, [r1, r0, asl #2]
	cmn	r0, #1
	beq	.L3896
	movw	r6, #2962
	ldrb	r3, [r7, #89]	@ zero_extendqisi2
	ldrh	lr, [ip, r6]
	mov	r2, #4
	ldr	r0, .L3920+24
	str	ip, [sp, #24]
	mul	r3, lr, r3
	bl	rknand_print_hex
	ldrb	r3, [r7, #89]	@ zero_extendqisi2
	ldr	r0, .L3920+28
	mov	r2, #4
	ldr	r1, [fp, #-3312]
	ldr	ip, [sp, #24]
	ldrh	ip, [ip, r6]
	mul	r3, ip, r3
	bl	rknand_print_hex
	ldr	r3, .L3920+32
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L3897
	ldr	r3, .L3920+12
	ldr	r2, [fp, #-3312]
	ldr	lr, [fp, #-3316]
	ldrh	ip, [r3, #18]
	ldr	r3, [r5, #12]
	ldr	r0, .L3920+36
	ldr	r2, [r2, ip, asl #2]
	str	r2, [sp]
	ldr	r2, [r3]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #12]
	ldr	r3, [r3, #12]
	str	r3, [sp, #16]
	ldr	r1, [r5, #24]
	ldr	r2, [r5, #36]
	ldr	r3, [lr, ip, asl #2]
	bl	printk
.L3897:
	ldr	r3, [r4, #580]
	mov	r1, #0
	ldrh	r0, [r3, #80]
	bl	ftl_sblk_dump
	ldrh	r3, [r7, #80]
	ldr	r2, [r4, #576]
	mov	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, [r4, #580]
	ldr	r3, [r2, #556]
	add	r3, r3, #1
	str	r3, [r2, #556]
	ldr	r3, [r4, #2288]
	ldr	r2, [r3, #156]
	ldr	r3, .L3920+40
	cmp	r2, r3
	bne	.L3901
	ldr	r0, [r5, #24]
	bl	ftl_mask_bad_block
	b	.L3901
.L3896:
	ldrh	r1, [r6, #18]
	add	r8, r8, #1
	add	r1, r1, #1
	strh	r1, [r6, #18]	@ movhi
	b	.L3900
.L3919:
	add	r9, r9, #1
	add	r10, r10, #2
	b	.L3890
.L3918:
	ldrb	r0, [r4, #2938]	@ zero_extendqisi2
	ldrh	r3, [r2, #12]
	cmp	r0, #0
	add	r1, r3, #1
	addne	r3, r3, #2
	strh	r1, [r2, #12]	@ movhi
	movne	r0, #0
	strneh	r3, [r2, #12]	@ movhi
	b	.L3898
.L3901:
	mvn	r0, #0
.L3898:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3921:
	.align	2
.L3920:
	.word	.LANCHOR0
	.word	.LANCHOR1+2100
	.word	.LC0
	.word	.LANCHOR0+2312
	.word	.LANCHOR0+2994
	.word	.LANCHOR3
	.word	.LC291
	.word	.LC292
	.word	.LANCHOR2
	.word	.LC293
	.word	1145785929
	.fnend
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	ftl_update_l2p_map
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3954
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r3]
	mov	r7, r0
	ldr	r4, .L3954+4
	ldrb	r8, [r0, #9]	@ zero_extendqisi2
	str	r2, [sp, #20]
	add	r2, r4, #2960
	ldrh	r5, [r0, #12]
	ldrh	r2, [r2]
	ldr	r6, [r4, #3512]
	str	r3, [sp, #8]
	mul	r8, r8, r2
	add	r5, r6, r5, asl #2
	sub	r2, r8, #-1073741823
	ldr	r2, [r5, r2, asl #2]
	cmn	r2, #1
	beq	.L3923
	ldr	r1, .L3954+8
	movw	r2, #1733
	ldr	r0, .L3954+12
	bl	printk
	bl	dump_stack
.L3923:
	mov	r6, #0
	sub	r5, r5, #4
	mov	r10, r6
.L3924:
	cmp	r10, r8
	beq	.L3953
	ldr	r9, [r5, #4]!
	cmn	r9, #1
	beq	.L3925
	ldrb	r1, [r4, #2260]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #4]
	ldr	r3, .L3954+16
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3926
	ldr	r0, .L3954+20
	mov	r2, r9
	ldr	r1, [sp, #4]
	mov	r3, r10
	bl	printk
.L3926:
	mov	r9, r5
	mov	fp, r10
.L3929:
	ldr	r0, [r9]
	cmn	r0, #1
	beq	.L3927
	ldrb	r1, [r4, #2260]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxth	r0, r0
	cmp	r0, r3
	bne	.L3927
	ldrb	r3, [r7, #9]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r3
	str	r3, [sp, #12]
	bl	__aeabi_idiv
	ldr	r2, .L3954+24
	ldrh	r2, [r2]
	ldr	r3, [sp, #12]
	mls	r3, r3, r0, fp
	add	r3, r7, r3, asl #1
	ldrh	r3, [r3, #16]
	mla	r2, r2, r3, r0
	ldr	r0, .L3954+28
	str	r2, [sp, #16]
	ldr	r1, [r9]
	bl	pm_ppa_update_check
	cmp	r0, #0
	beq	.L3928
	ldr	r3, .L3954+4
	mov	r2, #4
	ldr	r0, .L3954+32
	ldr	r1, [r3, #3512]
	mov	r3, r8
	bl	rknand_print_hex
.L3928:
	add	r6, r6, #1
	ldr	r0, [r9]
	add	r1, sp, #16
	mov	r2, #1
	uxth	r6, r6
	bl	pm_log2phys
	mvn	r3, #0
	str	r3, [r9]
.L3927:
	add	fp, fp, #1
	add	r9, r9, #4
	cmp	fp, r8
	bne	.L3929
.L3925:
	add	r10, r10, #1
	b	.L3924
.L3953:
	ldr	r3, .L3954+16
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3931
	ldrh	r1, [r7]
	mov	r2, r6
	ldr	ip, [r4, #576]
	ldr	r0, .L3954+36
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L3931:
	ldrh	r3, [r7]
	ldr	r2, [r4, #576]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3932
	bl	__stack_chk_fail
.L3932:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3955:
	.align	2
.L3954:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+2124
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC294
	.word	.LANCHOR0+2994
	.word	.LC295
	.word	.LC296
	.word	.LC297
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
	beq	.L3957
	bl	zftl_insert_data_list
.L3957:
	ldr	r4, .L3965
	mov	r0, r6
	ldr	r1, [r4, #580]
	add	r1, r1, #16
	cmp	r1, r6
	movw	r6, #65535
	moveq	r1, #2
	movne	r1, #3
	bl	ftl_open_sblk_init
	ldr	r3, [r4, #580]
	cmp	r5, r6
	ldr	r3, [r3, #560]
	rsb	r3, r5, r3
	clz	r3, r3
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L3959
	mov	r1, r5
	ldr	r0, .L3965+4
	bl	printk
	ldr	r3, [r4, #580]
	ldr	r0, [r3, #564]
	bl	gc_mark_bad_ppa
	ldr	r3, [r4, #580]
	mvn	r2, #0
	str	r6, [r3, #560]
	str	r2, [r3, #564]
.L3959:
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3966:
	.align	2
.L3965:
	.word	.LANCHOR0
	.word	.LC298
	.fnend
	.size	ftl_alloc_new_data_sblk, .-ftl_alloc_new_data_sblk
	.align	2
	.global	ftl_write_commit
	.type	ftl_write_commit, %function
ftl_write_commit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4031
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L4031+4
	ldr	r3, [r3]
	mov	r8, r6
	str	r3, [sp, #28]
.L3968:
	ldrb	r2, [r6, #2284]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3970
	ldrb	r3, [r6, #2308]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r6, #2284]
	str	r3, [sp, #16]
	mov	r3, #48
	ldr	r1, [sp, #16]
	mul	r3, r3, r1
	ldr	r1, .L4031+8
	add	r1, r1, r3
	add	r3, r6, r3
	str	r1, [sp, #20]
	ldr	r2, [r3, #744]
	ldrb	r1, [r3, #724]	@ zero_extendqisi2
	ldr	r3, [r6, #2268]
	cmp	r2, r3
	strb	r1, [r6, #2308]
	bcc	.L3971
	ldr	r1, .L4031+12
	movw	r2, #510
	ldr	r0, .L4031+16
	bl	printk
	bl	dump_stack
.L3971:
	ldr	r2, [sp, #16]
	mov	r3, #48
	ldr	r1, [r8, #2268]
	mla	r2, r3, r2, r8
	ldr	r9, [r2, #744]
	cmp	r9, r1
	bcc	.L3972
	ldr	r0, [sp, #20]
	bl	zbuf_free
	mvn	r0, #0
	b	.L3973
.L3972:
	ldr	r1, [r2, #736]
	ldr	fp, [r2, #728]
	ldrb	r7, [r2, #765]	@ zero_extendqisi2
	ldrb	r2, [r2, #764]	@ zero_extendqisi2
	str	r1, [sp, #8]
	str	r2, [sp, #12]
	ldrb	r2, [r6, #2285]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3974
	ldr	r2, .L4031+20
	ldrb	r4, [r2, #-3239]	@ zero_extendqisi2
	ldr	r2, .L4031+8
	mla	r4, r3, r4, r2
.L3975:
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #255
	ldrne	r1, .L4031+8
	mlane	r4, r3, r2, r1
	bne	.L3975
.L4029:
	ldr	r3, [r4, #20]
	cmp	r3, r9
	bne	.L3974
	ldr	r3, [sp, #12]
	mov	r1, r7, asl #9
	ldr	r0, [r4, #4]
	mov	r2, r3, asl #9
	add	r0, r0, r1
	add	r1, fp, r1
	bl	ftl_memcpy
	ldr	r0, [sp, #20]
	bl	zbuf_free
	b	.L3968
.L3974:
	mov	r0, r9
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L3978
	mov	r0, r9
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L3978:
	ldr	r4, [r8, #580]
	mov	r3, #0
	ldr	r2, .L4031+8
	add	r4, r4, #16
.L3981:
	ldr	r1, [r2, #20]
	cmp	r1, r9
	bne	.L3979
	ldrb	r1, [r2, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L3979
	mov	r10, #48
	bic	r1, r1, #8
	mla	r10, r10, r3, r8
	ldr	r3, [r10, #748]
	strb	r1, [r10, #726]
	ldr	r5, [r10, #728]
	str	r3, [sp, #24]
	b	.L3980
.L3979:
	add	r3, r3, #1
	add	r2, r2, #48
	cmp	r3, #32
	bne	.L3981
	mov	r5, #0
.L3980:
	ldrb	r3, [r8, #2260]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	cmp	r2, r3
	bcs	.L4006
	cmp	r5, #0
	beq	.L3983
	cmp	r7, #0
	beq	.L3984
	mov	r0, fp
	mov	r1, r5
	mov	r2, r7, asl #9
	bl	ftl_memcpy
	ldr	r4, [r6, #580]
	add	r4, r4, #48
.L3984:
	ldr	r3, [sp, #12]
	ldrb	r2, [r8, #2260]	@ zero_extendqisi2
	add	r7, r7, r3
	cmp	r7, r2
	ldrcs	r4, [r6, #580]
	addcs	r4, r4, #16
	bcs	.L4006
	rsb	r2, r7, r2
	mov	r1, r7, asl #9
	add	r0, fp, r1
	add	r1, r5, r1
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4006
.L3983:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L3986
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #24]
	mov	r5, r0
	str	r9, [r0, #20]
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r5, #12]
	ldr	r10, [r3, #12]
	ldr	r3, [r3, #4]
	add	r10, r10, #1
	cmp	r3, r9
	bne	.L3987
	ldr	r3, [r5, #36]
	cmn	r3, #1
	bne	.L3988
.L3987:
	ldr	r2, [r8, #580]
	ldr	r0, .L4031+24
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r5, #36]
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, [sp, #24]
	str	r3, [sp]
	mov	r3, r9
	bl	printk
	mov	r2, #4
	ldr	r0, .L4031+28
	mov	r3, r2
	ldr	r1, [r5, #12]
	bl	rknand_print_hex
.L3988:
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r9
	bne	.L3989
	ldr	r3, [r5, #36]
	cmn	r3, #1
	bne	.L3990
.L3989:
	ldr	r1, .L4031+12
	movw	r2, #581
	ldr	r0, .L4031+16
	bl	printk
	bl	dump_stack
.L3990:
	cmp	r7, #0
	beq	.L3991
	ldr	r3, [sp, #24]
	mov	r2, r7, asl #9
	cmn	r3, #1
	beq	.L3992
	mov	r0, fp
	ldr	r1, [r5, #4]
	bl	ftl_memcpy
	b	.L3993
.L3986:
	cmp	r7, #0
	moveq	r5, r7
	moveq	r10, r7
	beq	.L3991
	mov	r2, r7, asl #9
	mov	r10, r5
.L3992:
	mov	r0, fp
	mov	r1, #0
	bl	ftl_memset
.L3993:
	ldr	r4, [r6, #580]
	add	r4, r4, #48
.L3991:
	ldr	r3, [sp, #12]
	ldrb	r2, [r6, #2260]	@ zero_extendqisi2
	add	r7, r7, r3
	cmp	r7, r2
	bcc	.L3994
	ldrhi	r4, [r6, #580]
	addhi	r4, r4, #16
	b	.L3995
.L3994:
	ldr	r3, [sp, #24]
	rsb	r2, r7, r2
	mov	r7, r7, asl #9
	cmn	r3, #1
	mov	r2, r2, asl #9
	beq	.L3996
	ldr	r1, [r5, #4]
	add	r0, fp, r7
	add	r1, r1, r7
	bl	ftl_memcpy
	b	.L3995
.L3996:
	add	r0, fp, r7
	mov	r1, #0
	bl	ftl_memset
.L3995:
	cmp	r5, #0
	beq	.L3982
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	mov	r1, r5
	ldr	r0, .L4031+32
	bic	r3, r3, #8
	strb	r3, [r5, #2]
	bl	buf_remove_buf
	mov	r0, r5
	bl	zbuf_free
	b	.L3982
.L4006:
	mov	r10, #0
.L3982:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L3997
	bl	ftl_flush
	mov	r0, r4
	bl	ftl_alloc_new_data_sblk
.L3997:
	mov	r0, r4
	ldr	r5, .L4031+20
	bl	ftl_get_new_free_page
	mov	r3, #48
	ldr	r2, [sp, #16]
	ldr	ip, [sp, #8]
	mul	r3, r3, r2
	add	r2, r8, r3
	ldr	r1, [r2, #740]
	str	r9, [ip, #4]
	str	r10, [ip, #12]
	str	r1, [ip]
	ldr	r1, [sp, #24]
	str	r1, [ip, #8]
	str	r1, [r2, #752]
	mvn	r1, #0
	strb	r1, [r2, #724]
	ldrb	r1, [r2, #726]	@ zero_extendqisi2
	str	r0, [r2, #748]
	orr	r1, r1, #10
	strb	r1, [r2, #726]
	ldr	r2, .L4031+8
	ldrh	r10, [r4, #10]
	ldrh	r9, [r4, #12]
	add	r3, r2, r3
	ldr	r0, .L4031+36
	add	r9, r10, r9
	ldr	r1, [sp, #20]
	sub	r9, r9, #1
	strh	r9, [r3, #32]	@ movhi
	bl	buf_add_tail
	ldrb	r3, [r8, #2285]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r8, #2285]
	bl	timer_get_time
	ldrb	r3, [r8, #2285]	@ zero_extendqisi2
	ldrh	r2, [r4, #6]
	cmp	r3, #2
	str	r0, [r5, #-3236]
	bhi	.L3998
	cmp	r2, #1
	bne	.L3970
.L3998:
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	mov	lr, #48
	ldrb	r0, [r5, #-3239]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, #0
	moveq	ip, #1
	movne	ip, #2
	cmp	r2, #1
	mov	r2, r0
	moveq	ip, r3
.L4002:
	cmp	r1, ip
	mlane	r2, lr, r2, r6
	addne	r1, r1, #1
	ldrneb	r2, [r2, #724]	@ zero_extendqisi2
	bne	.L4002
.L4030:
	strb	r2, [r5, #-3239]
	uxtb	r1, r1
	ldr	r2, .L4031+8
	rsb	r3, r1, r3
	strb	r3, [r8, #2285]
	mov	r3, #48
	mla	r0, r3, r0, r2
	bl	sblk_prog_page
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L3970
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	r0, r4
	bl	ftl_write_last_log_page
	mov	r0, r4
	bl	ftl_alloc_new_data_sblk
.L3970:
	ldrb	r4, [r6, #2284]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L3968
	bl	ftl_write_completed
	mov	r0, r4
.L3973:
	ldr	r3, .L4031
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L4005
	bl	__stack_chk_fail
.L4005:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4032:
	.align	2
.L4031:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+724
	.word	.LANCHOR1+2144
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC299
	.word	.LC230
	.word	.LANCHOR0+3533
	.word	.LANCHOR3-3239
	.fnend
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4181
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r3, [r3]
	str	r3, [sp, #84]
	ldr	r3, .L4181+4
	ldrb	r0, [r3, #2945]	@ zero_extendqisi2
	ldr	r9, [r3, #580]
	cmp	r0, #0
	bne	.L4034
	mov	r7, r3
	bl	buf_alloc
	subs	r4, r0, #0
	beq	.L4033
	ldr	r6, .L4181+8
	ldrh	r5, [r6, #22]
	mov	r0, r5
	add	r5, r5, #1
	bl	gc_get_src_ppa_from_index
	mov	r1, #1
	strh	r5, [r6, #22]	@ movhi
	str	r0, [r4, #24]
	mov	r9, r0
	mov	r0, r4
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4036
	ldr	r1, .L4181+12
	mov	r2, #864
	ldr	r0, .L4181+16
	bl	printk
	bl	dump_stack
.L4036:
	ldr	r3, [r4, #12]
	ldr	r5, [r3, #4]
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #56]
	bne	.L4037
	mov	r0, r5
	add	r1, sp, #56
	mov	r2, #0
	bl	pm_log2phys
.L4037:
	ldr	r8, [sp, #56]
	cmp	r8, r9
	bne	.L4038
	ldr	r1, .L4181+20
	mov	r3, #0
.L4041:
	add	r2, r1, r3
	ldr	r0, [r2, #20]
	cmp	r0, r5
	bne	.L4039
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #2
	beq	.L4039
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L4181+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4033
	mov	r1, r5
	mov	r2, r8
	ldrh	r3, [r6, #22]
	ldr	r0, .L4181+28
	bl	printk
	b	.L4033
.L4039:
	add	r3, r3, #48
	cmp	r3, #1536
	bne	.L4041
	mov	r3, #0
	strb	r3, [r4, #41]
	ldrb	r3, [r7, #2260]	@ zero_extendqisi2
	str	r5, [r4, #20]
	strb	r3, [r4, #40]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	orr	r3, r3, #10
	strb	r3, [r4, #2]
	ldr	r3, [r4, #12]
	ldr	r3, [r3]
	str	r3, [r4, #16]
	ldr	r3, .L4181+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4042
	mov	r0, r5
	ldr	r9, [sp, #56]
	bl	lpa_hash_get_ppa
	mov	r1, r5
	str	r8, [sp]
	mov	r3, r0
	ldrh	r2, [r6, #22]
	ldr	r0, .L4181+32
	str	r2, [sp, #4]
	mov	r2, r9
	bl	printk
.L4042:
	mov	r0, r4
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	r2, [r7, #2288]
	ldr	r3, [r2, #60]
	add	r3, r3, #1
	str	r3, [r2, #60]
	ldrh	r3, [r6, #24]
	add	r3, r3, #1
	strh	r3, [r6, #24]	@ movhi
	b	.L4033
.L4038:
	ldr	r3, .L4181+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4044
	mov	r0, r5
	bl	lpa_hash_get_ppa
	mov	r1, r5
	str	r9, [sp]
	mov	r3, r0
	ldrh	r2, [r6, #22]
	ldr	r0, .L4181+32
	str	r2, [sp, #4]
	mov	r2, r8
	bl	printk
.L4044:
	mov	r0, r4
	bl	zbuf_free
	b	.L4033
.L4034:
	ldrb	r4, [r3, #2937]	@ zero_extendqisi2
	ldr	r7, .L4181+8
	cmp	r4, #3
	ldrb	fp, [r9, #89]	@ zero_extendqisi2
	bne	.L4045
	ldrb	r2, [r3, #684]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4046
	movw	r3, #566
	mov	r1, fp
	ldrh	r8, [r7, r3]
	mov	r0, r8
	bl	__aeabi_idiv
	ldr	r3, .L4181+36
	ldrb	r2, [r3, #-2503]	@ zero_extendqisi2
	cmp	r2, #0
	uxth	r5, r0
	beq	.L4047
	ldr	r3, .L4181+40
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
	b	.L4048
.L4047:
	ldrb	r3, [r3, #-2502]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L4048
	sub	r3, r5, #62
	cmp	r3, #2160
	movcs	r4, #2
	bcs	.L4048
	mov	r1, r4
	mov	r0, r5
	bl	__aeabi_uidivmod
	uxth	r1, r1
	cmp	r1, #0
	movne	r4, #1
	moveq	r4, #2
.L4048:
	smulbb	r1, r5, fp
	rsb	r1, r1, r8
	b	.L4173
.L4046:
	ldrb	r3, [r3, #2944]	@ zero_extendqisi2
	cmp	r3, #0
	movw	r3, #566
	ldrh	r8, [r7, r3]
	mov	r0, r8
	beq	.L4050
	add	r1, fp, fp, asl #1
	sub	fp, fp, fp, asl #2
	bl	__aeabi_idiv
	uxth	r5, r0
	smulbb	r1, fp, r5
	add	r1, r8, r1
.L4173:
	uxth	r3, r1
	str	r3, [sp, #36]
	b	.L4051
.L4050:
	mov	r1, fp
	bl	__aeabi_idiv
	ldr	r2, .L4181+44
	mov	r3, r0, asl #1
	smulbb	r0, r0, fp
	ldrh	r10, [r2, r3]
	and	r3, r10, #7
	str	r3, [sp, #32]
	rsb	r0, r0, r8
	mov	r5, r10, lsr #3
	uxth	r3, r0
	str	r3, [sp, #36]
	mla	fp, fp, r5, r3
	ldr	r3, [sp, #32]
	cmp	r3, #1
	add	fp, fp, fp, asl #1
	uxth	r8, fp
	bne	.L4052
	b	.L4051
.L4045:
	ldrb	r3, [r3, #2938]	@ zero_extendqisi2
	mov	r1, fp
	cmp	r3, #0
	movw	r3, #566
	ldrh	r8, [r7, r3]
	mov	r0, r8
	bne	.L4053
	bl	__aeabi_idiv
	mov	r4, #1
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r8
	uxth	r3, r1
	str	r3, [sp, #36]
	b	.L4051
.L4053:
	bl	__aeabi_idiv
	mov	r4, #2
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r8
	uxth	r3, r1
	str	r3, [sp, #36]
.L4051:
	ldr	fp, .L4181+40
	mov	r1, #0
	ldr	ip, .L4181+36
	add	fp, fp, r5, asl #1
.L4054:
	ldrb	lr, [r9, #89]	@ zero_extendqisi2
	uxth	r0, r1
	mul	r3, lr, r4
	cmp	r0, r3
	bge	.L4106
	ldr	r3, .L4181+48
	add	r2, r8, r0
	ldrh	r3, [r3]
	mul	r3, lr, r3
	sub	r3, r3, #1
	cmp	r2, r3
	bne	.L4055
	ldrb	r3, [ip, #-2503]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4106
	ldrh	r3, [fp]
	cmp	r3, r5
	bcs	.L4106
	tst	r0, #1
	beq	.L4106
.L4055:
	ldr	r3, .L4181+52
	ldr	r3, [r3, #-3312]
	ldr	lr, [r3, r2, asl #2]
	cmn	lr, #1
	bne	.L4056
	ldrh	r10, [r7, #22]
	ldrh	lr, [r7, #20]
	cmp	lr, r10
	bls	.L4033
	ldrb	lr, [ip, #-2503]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L4057
	ldrh	lr, [fp]
	cmp	lr, r5
	bcs	.L4057
	tst	r0, #1
	ldrne	r0, [r3, lr, asl #2]
	strne	r0, [r3, r2, asl #2]
	bne	.L4056
.L4057:
	mov	r0, r10
	str	ip, [sp, #48]
	str	r1, [sp, #44]
	add	r10, r10, #1
	str	r3, [sp, #40]
	str	r2, [sp, #32]
	bl	gc_get_src_ppa_from_index
	strh	r10, [r7, #22]	@ movhi
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #40]
	ldr	ip, [sp, #48]
	ldr	r1, [sp, #44]
	str	r0, [r3, r2, asl #2]
.L4056:
	add	r1, r1, #1
	b	.L4054
.L4106:
	mov	r3, #1
	str	r3, [sp, #32]
.L4052:
	ldr	r3, .L4181+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4059
	ldr	r3, .L4181+52
	movw	r2, #566
	ldrh	r2, [r7, r2]
	mov	r1, r5
	ldr	r0, .L4181+56
	ldr	r3, [r3, #-3312]
	str	r2, [sp]
	ldr	r2, [r3, r8, asl #2]
	add	r3, r3, r8, asl #2
	str	r2, [sp, #4]
	ldr	r3, [r3, #4]
	ldr	r2, .L4181+40
	str	r8, [sp, #12]
	str	r3, [sp, #8]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #32]
	str	r3, [sp, #16]
	ldr	r3, [sp, #36]
	bl	printk
.L4059:
	ldr	r1, .L4181+40
	mov	r3, #0
	str	r3, [sp, #40]
	add	r3, r1, r5, asl #1
	str	r3, [sp, #44]
.L4060:
	ldrb	r1, [r9, #89]	@ zero_extendqisi2
	ldrh	r7, [sp, #40]
	ldr	r10, .L4181+4
	mul	r3, r1, r4
	cmp	r7, r3
	bge	.L4177
	ldr	r2, [r10, #2940]
	add	fp, r8, r7
	ldrb	r2, [r2, fp]	@ zero_extendqisi2
	cmp	r2, #255
	bne	.L4061
	mov	r0, #0
	bl	buf_alloc
	subs	r6, r0, #0
	bne	.L4062
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	r0, r6
	bl	buf_alloc
	subs	r6, r0, #0
	beq	.L4033
.L4062:
	ldrb	r1, [r6, #1]	@ zero_extendqisi2
	ldr	r2, [r10, #2940]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	ldr	ip, .L4181+48
	strb	r1, [r2, fp]
	ldrb	r2, [r10, #2319]	@ zero_extendqisi2
	ldrh	r1, [ip]
	add	r2, r2, #1
	strb	r3, [r6, #45]
	strb	r2, [r10, #2319]
	add	r2, r7, r8
	strh	r2, [r6, #32]	@ movhi
	ldrb	r2, [r9, #89]	@ zero_extendqisi2
	mul	r2, r2, r1
	ldr	r1, .L4181+52
	sub	r2, r2, #1
	cmp	fp, r2
	bne	.L4063
	ldr	r2, .L4181+36
	ldrb	r2, [r2, #-2503]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4064
	ldr	r3, [sp, #44]
	ldrh	r2, [r3]
	cmp	r2, r5
	bcs	.L4064
	tst	r7, #1
	bne	.L4063
.L4064:
	ldr	r2, [r1, #-3316]
	mvn	r0, #0
	str	ip, [sp, #48]
	str	r0, [r2, fp, asl #2]
	ldrh	r0, [ip]
	ldrb	r2, [r10, #2964]	@ zero_extendqisi2
	ldr	r1, [r1, #-3316]
	mul	r2, r2, r0
	ldr	r0, [r6, #4]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	ldrb	r3, [r10, #2964]	@ zero_extendqisi2
	ldr	r0, [r6, #4]
	ldr	r10, .L4181+4
	ldr	ip, [sp, #48]
	ldrh	r2, [ip]
	mul	r2, r3, r2
	ldr	r3, .L4181+36
	ldrb	r3, [r3, #-2502]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r2, r2, asl #2
	ldrne	r1, .L4181+4
	mov	r3, r2
	add	r0, r0, r3
	ldr	r3, .L4181+52
	ldrneb	r1, [r1, #2260]	@ zero_extendqisi2
	rsbne	r2, r2, r1, asl #9
	ldr	r1, [r3, #-3312]
	bl	ftl_memcpy
	ldrb	r2, [r10, #2260]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r6, #12]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r2, [r6, #12]
	ldr	r1, .L4181+60
	str	r1, [r2]
	ldr	r1, .L4181+48
	ldr	r2, [r6, #12]
	ldrh	r0, [r1]
	ldrb	r1, [r10, #2964]	@ zero_extendqisi2
	mul	r1, r1, r0
	ldr	r0, [r6, #4]
	mov	r1, r1, asl #2
	str	r2, [sp, #48]
	bl	js_hash
	ldr	r3, [r10, #580]
	ldr	r2, [sp, #48]
	str	r0, [r2, #4]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, #132]
	str	r3, [r2, #8]
	b	.L4061
.L4063:
	ldr	r2, [r1, #-3312]
	mov	r0, r6
	mov	r1, #1
	ldr	r2, [r2, fp, asl #2]
	str	r2, [r6, #24]
	bl	sblk_read_page
	ldr	r2, [r6, #36]
	cmn	r2, #1
	cmpne	r2, #512
	bne	.L4067
	ldr	r2, .L4181+64
	ldrb	r0, [r10, #676]	@ zero_extendqisi2
	ldrh	r1, [r2]
	rsb	r0, r0, #24
	ldr	r2, [r6, #24]
	rsb	r0, r1, r0
	mov	r2, r2, lsr r1
	mvn	r1, #0
	bic	r0, r2, r1, asl r0
	ldrb	r1, [r10, #2930]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
	ldr	r3, [r6, #36]
	cmn	r3, #1
	cmpne	r3, #512
	ldreq	r3, [r6, #12]
	mvneq	r2, #0
	streq	r2, [r3, #4]
.L4067:
	ldr	r3, [r6, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4069
	ldr	r1, .L4181+12
	movw	r2, #1003
	ldr	r0, .L4181+16
	bl	printk
	bl	dump_stack
.L4069:
	ldr	r3, [r6, #12]
	ldr	r2, .L4181+4
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #2268]
	cmp	r1, r2
	mvncs	r2, #0
	strcs	r2, [r3, #4]
	ldr	r3, [r6, #12]
	ldr	r2, [r3, #4]
	ldr	r3, .L4181+52
	ldr	r3, [r3, #-3316]
	str	r2, [r3, fp, asl #2]
	ldr	r3, [r6, #12]
	ldr	r2, [r6, #24]
	str	r2, [r3, #8]
.L4061:
	ldr	r0, .L4181+4
	mov	r2, #48
	add	ip, r0, #724
	ldr	r3, [r0, #2940]
	ldrb	r10, [r3, fp]	@ zero_extendqisi2
	add	r3, sp, #88
	add	r1, r3, r7, asl #2
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	mul	r2, r2, r10
	add	ip, ip, r2
	str	ip, [r1, #-28]
	mov	r1, #2
	add	r2, r0, r2
	strh	r1, [ip, #34]	@ movhi
	ldrb	r1, [r0, #2937]	@ zero_extendqisi2
	strb	r3, [r2, #769]
	cmp	r1, #3
	bne	.L4071
	ldrb	r1, [r0, #684]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4072
	ldr	r1, .L4181+36
	and	r2, r7, #1
	add	ip, r5, r2
	ldrb	r1, [r1, #-2503]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4073
	ldr	r3, [sp, #44]
	ldrh	r1, [r3]
	cmp	r1, r5
	movcs	r2, #0
	andcc	r2, r2, #1
	cmp	r2, #0
	movne	ip, r1
.L4073:
	ldr	r2, .L4181+4
	mov	r1, #48
	mov	r0, r7
	str	ip, [sp, #52]
	mla	r2, r1, r10, r2
	mov	r1, r4
	str	r2, [sp, #48]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	ldr	ip, [sp, #52]
	uxtah	r0, r3, r0
	ldr	r2, [sp, #48]
	add	r0, r9, r0, asl #1
	ldrh	r1, [r0, #96]
	ldr	r0, .L4181+68
	ldrh	r0, [r0]
	mla	ip, r0, r1, ip
	orr	ip, ip, #50331648
	str	ip, [r2, #748]
	b	.L4074
.L4072:
	ldr	r0, .L4181+72
	ldrh	ip, [ip, #32]
	umull	r0, r1, r7, r0
	mov	r3, r1, lsr #1
	ldr	r1, [sp, #36]
	uxtah	r1, r1, r3
	add	r3, r3, r3, asl #1
	rsb	r3, r3, r7
	add	r1, r9, r1, asl #1
	mov	r3, r3, asl #24
	ldrh	r0, [r1, #96]
	add	r3, r3, #16777216
	ldr	r1, .L4181+68
	ldrh	r1, [r1]
	mul	r1, r1, r0
	orr	r1, r1, r5
	str	r1, [r2, #748]
	ldr	r2, .L4181+52
	orr	r1, r3, r1
	ldr	r2, [r2, #-3308]
	str	r1, [r2, ip, asl #2]
	b	.L4075
.L4071:
	cmp	r1, #2
	bne	.L4074
	ldrb	r1, [r0, #2938]	@ zero_extendqisi2
	ldr	r3, [sp, #36]
	cmp	r1, #0
	bne	.L4077
	add	r1, r7, r3
	ldr	r0, .L4181+68
	add	r1, r9, r1, asl #1
	ldrh	r0, [r0]
	ldrh	r1, [r1, #96]
	mla	r1, r0, r1, r5
	b	.L4174
.L4077:
	add	r1, r3, r7, lsr #1
	ldr	r0, .L4181+68
	add	r1, r9, r1, asl #1
	ldrh	r0, [r0]
	ldrh	r1, [r1, #96]
	mla	r1, r0, r1, r5
	and	r0, r7, #1
	add	r1, r1, r0
.L4174:
	orr	r1, r1, #33554432
	str	r1, [r2, #748]
.L4074:
	mov	r1, #48
	ldr	r2, .L4181+4
	mul	r3, r1, r10
	add	r1, r2, r3
	add	r2, r2, #724
	add	r3, r2, r3
	ldr	r1, [r1, #748]
	ldrh	r2, [r3, #32]
	ldr	r3, .L4181+52
	ldr	r3, [r3, #-3308]
	str	r1, [r3, r2, asl #2]
.L4075:
	ldr	r3, .L4181+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4078
	ldr	r1, .L4181+4
	add	r3, sp, #88
	add	r3, r3, r7, asl #2
	ldr	r0, .L4181+72
	ldr	r2, [r3, #-28]
	ldrb	r3, [r1, #2318]	@ zero_extendqisi2
	ldr	r1, [r1, #2940]
	ldrb	r1, [r1, fp]	@ zero_extendqisi2
	str	r1, [sp]
	ldr	r1, [r2, #24]
	str	r1, [sp, #4]
	ldrh	r1, [r2, #32]
	str	r1, [sp, #8]
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	str	r1, [sp, #12]
	umull	r0, r1, r7, r0
	ldr	r0, [sp, #36]
	ubfx	r1, r1, #1, #16
	add	r1, r0, r1
	str	r1, [sp, #16]
	ldr	r2, [r2, #12]
	mov	r1, r7
	ldr	r0, .L4181+76
	ldr	r2, [r2, #4]
	str	r2, [sp, #20]
	ldr	r2, [r6, #12]
	ldr	r2, [r2]
	str	r2, [sp, #24]
	ldr	r2, [sp, #32]
	bl	printk
.L4078:
	ldr	r3, .L4181+36
	ldrb	r3, [r3, #-2503]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4079
	ldr	r3, [sp, #44]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4079
	tst	r7, #1
	beq	.L4079
	ldr	r1, .L4181+4
	mvn	r2, #0
	ldr	r3, [r1, #2940]
	strb	r2, [r3, fp]
	add	r3, sp, #88
	add	r7, r3, r7, asl #2
	ldrb	r3, [r9, #89]	@ zero_extendqisi2
	ldr	r1, [r1, #2940]
	ldr	r0, [r7, #-28]
	rsb	r3, r3, r3, asl #15
	mov	r3, r3, asl #1
	smulbb	r3, r3, r4
	add	r3, r8, r3
	uxth	r3, r3
	strh	r3, [r0, #32]	@ movhi
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	strb	r0, [r1, r3]
	ldr	r3, .L4181+52
	ldr	r3, [r3, #-3312]
	str	r2, [r3, fp, asl #2]
.L4079:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	b	.L4060
.L4177:
	ldrb	r2, [r10, #2937]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L4081
	ldrb	ip, [r10, #684]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L4178
	mov	r0, #0
.L4082:
	ldrb	r2, [r9, #89]	@ zero_extendqisi2
	uxth	r1, r0
	add	r3, sp, #88
	add	r0, r0, #1
	mul	r2, r4, r2
	sub	r2, r2, #1
	cmp	r1, r2
	bge	.L4179
	add	r2, r3, r1, asl #2
	add	r1, r3, r1, asl #2
	ldr	r1, [r1, #-24]
	ldr	r2, [r2, #-28]
	ldrb	r1, [r1, #1]	@ zero_extendqisi2
	strb	r1, [r2]
	b	.L4082
.L4179:
	add	r2, r3, r2, asl #2
	ldr	r2, [r2, #-28]
	b	.L4175
.L4178:
	add	r0, sp, #88
	ldrb	r1, [r10, #2944]	@ zero_extendqisi2
	ldr	r2, [r0, #-28]!
	cmp	r1, #0
	strneb	ip, [r2, #44]
	bne	.L4176
	ldr	r3, [sp, #32]
	cmp	r3, #1
	moveq	r1, #9
	beq	.L4089
	ldr	r3, [sp, #32]
	cmp	r3, #2
	moveq	r1, #13
.L4089:
	strb	r1, [r2, #44]
.L4176:
	add	r1, sp, #72
	ldrb	r2, [r9, #89]	@ zero_extendqisi2
	bl	sblk_xlc_prog_pages
	b	.L4085
.L4081:
	ldrb	r2, [r10, #2938]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4171
	ldrb	r2, [r10, #2939]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4171
	add	r0, sp, #60
	bl	sblk_3d_mlc_prog_pages
	b	.L4085
.L4171:
	ldrb	r1, [r9, #89]	@ zero_extendqisi2
	uxth	r0, r2
	add	r3, sp, #88
	add	r2, r2, #1
	mul	r1, r4, r1
	sub	r1, r1, #1
	cmp	r0, r1
	bge	.L4180
	add	r1, r3, r0, asl #2
	add	r0, r3, r0, asl #2
	ldr	r0, [r0, #-24]
	ldr	r1, [r1, #-28]
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	strb	r0, [r1]
	b	.L4171
.L4180:
	add	r1, r3, r1, asl #2
	ldr	r2, [r1, #-28]
.L4175:
	mvn	r1, #0
	strb	r1, [r2]
	ldrb	r1, [r9, #89]	@ zero_extendqisi2
	ldr	r0, [sp, #60]
	smulbb	r1, r4, r1
	uxtb	r1, r1
	bl	sblk_prog_page
.L4085:
	ldrb	r2, [r10, #2944]	@ zero_extendqisi2
	ldr	r1, .L4181+4
	cmp	r2, #0
	ldrb	r2, [r9, #89]	@ zero_extendqisi2
	addne	r2, r2, r2, asl #1
	bne	.L4093
	ldrb	r0, [r1, #2938]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r2, r2, asl #1
	bne	.L4093
	ldrb	r1, [r1, #684]	@ zero_extendqisi2
	cmp	r1, #0
	uxtheq	r2, r2
	beq	.L4093
	ldr	r0, .L4181+36
	uxth	r1, r2
	mul	r2, r4, r1
	ldrb	r0, [r0, #-2503]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L4093
	ldr	ip, .L4181+40
	mov	r0, r5, asl #1
	ldrh	r0, [ip, r0]
	cmp	r0, r5
	movcc	r2, r1
.L4093:
	ldr	r0, [r10, #2288]
	ldr	r1, [r0, #52]
	add	r1, r1, r2
	str	r1, [r0, #52]
	ldr	r0, .L4181+8
	movw	r1, #566
	ldrh	r4, [r0, r1]
	add	r4, r2, r4
	movw	r2, #2962
	ldrh	r2, [r10, r2]
	uxth	r4, r4
	strh	r4, [r0, r1]	@ movhi
	ldrb	r3, [r9, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r4, r3
	ldrge	r3, .L4181+4
	movge	r2, #0
	ldrge	r3, [r3, #580]
	strgeh	r2, [r3, #86]	@ movhi
	bl	gc_write_completed
.L4033:
	ldr	r3, .L4181
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L4098
	bl	__stack_chk_fail
.L4098:
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4182:
	.align	2
.L4181:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+2312
	.word	.LANCHOR1+2164
	.word	.LC0
	.word	.LANCHOR0+724
	.word	.LANCHOR2
	.word	.LC300
	.word	.LC301
	.word	.LANCHOR5
	.word	.LANCHOR2+1960
	.word	.LANCHOR2+3496
	.word	.LANCHOR0+2962
	.word	.LANCHOR3
	.word	.LC302
	.word	-178307901
	.word	.LANCHOR0+2928
	.word	.LANCHOR0+2994
	.word	-1431655765
	.word	.LC303
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
	movw	r3, #2276
	ldr	r4, .L4379
	cmp	r0, #1
	.pad #44
	sub	sp, sp, #44
	mov	r10, r0
	ldrh	r2, [r4, r3]
	add	r3, r4, #2272
	ldr	r5, [r4, #580]
	ldrh	r7, [r3]
	movw	r3, #2274
	ldrh	r8, [r4, r3]
	add	r7, r2, r7
	uxth	r7, r7
	bne	.L4184
	ldr	r6, .L4379+4
	ldr	r9, [r6, #-2476]
	cmp	r9, #0
	bne	.L4185
	ldr	r3, [r6, #-2472]
	cmp	r3, #0
	beq	.L4184
.L4185:
	movw	r3, #2974
	ldrh	r3, [r4, r3]
	cmp	r7, r3, lsr #2
	bls	.L4184
	ldr	r1, .L4379
	movw	r3, #2292
	ldrh	r3, [r1, r3]
	cmp	r3, r7
	bcs	.L4184
	bl	timer_get_time
	add	r9, r9, #20
	ldr	r4, [r6, #-2472]
	add	r4, r4, #20
	cmp	r9, r0
	movcc	r3, #0
	strcc	r3, [r6, #-2476]
	bl	timer_get_time
	cmp	r4, r0
	ldrcc	r3, .L4379+4
	movcc	r2, #0
	strcc	r2, [r3, #-2472]
	b	.L4300
.L4184:
	ldr	r6, .L4379+8
	ldrb	r3, [r6, #-3322]	@ zero_extendqisi2
	cmp	r3, #6
	ldrls	pc, [pc, r3, asl #2]
	b	.L4300
.L4190:
	.word	.L4189
	.word	.L4191
	.word	.L4192
	.word	.L4193
	.word	.L4194
	.word	.L4301
	.word	.L4196
.L4189:
	movw	r1, #2278
	movw	r3, #2280
	ldrh	r3, [r4, r3]
	add	r8, r8, r2
	ldrh	r9, [r4, r1]
	ldrh	r1, [r5, #80]
	uxth	r8, r8
	add	r9, r3, r9
	movw	r3, #65535
	cmp	r1, r3
	ldr	r2, .L4379
	uxth	r9, r9
	beq	.L4197
	cmp	r10, #0
	bne	.L4198
	movw	r3, #2292
	ldrh	r3, [r2, r3]
	cmp	r7, r3, asl #1
	bge	.L4300
.L4198:
	movw	r3, #2972
	ldr	r0, .L4379+12
	ldrh	r1, [r4, r3]
	ldr	r10, .L4379
	add	r1, r1, #1
	uxth	r1, r1
	strh	r1, [r4, r3]	@ movhi
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4200
	ldr	r1, [r10, #576]
	mov	r2, r0, asl #1
	ldr	r3, [r6, #-3320]
	ldrh	r2, [r1, r2]
	add	r1, r10, #2960
	add	r3, r3, #1
	str	r3, [r6, #-3320]
	ldrh	r1, [r1]
	cmp	r1, r2
	bcs	.L4201
	add	r1, r10, #564
	ldrh	r1, [r1]
	cmp	r3, r1, lsr #4
	bls	.L4200
	movw	r3, #2294
	ldrh	r3, [r10, r3]
	cmp	r3, r2
	bls	.L4200
.L4201:
	mov	r1, #0
	ldrb	r2, [r4, #2945]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L4202
	mov	r3, #1
	strb	r3, [r6, #-3322]
	mov	r3, #0
	str	r3, [r6, #-3320]
	b	.L4300
.L4200:
	movw	r3, #2972
	mov	r2, #0
	strh	r2, [r4, r3]	@ movhi
.L4202:
	cmp	r8, #15
	movls	r9, #2
	bls	.L4203
	ldr	r2, .L4379+16
	movw	r3, #2282
	ldrh	r3, [r4, r3]
	ldrh	r2, [r2]
	cmp	r3, r2
	movls	r3, #0
	movhi	r3, #1
	cmp	r9, #0
	movne	r9, r3
	orreq	r9, r3, #1
	cmp	r9, #0
	movne	r9, #2
	moveq	r9, #1
.L4203:
	ldr	r3, .L4379+20
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4204
	ldr	r3, [r4, #580]
	ldrb	r2, [r4, #2945]	@ zero_extendqisi2
	str	r8, [sp]
	ldrh	r1, [r3, #124]
	ldr	r0, .L4379+24
	str	r1, [sp, #4]
	ldrh	r1, [r3, #120]
	str	r1, [sp, #8]
	movw	r1, #2171
	ldrh	r3, [r3, #122]
	str	r3, [sp, #12]
	ldrh	r3, [r5, #80]
	str	r3, [sp, #16]
	mov	r3, r7
	bl	printk
.L4204:
	ldrb	r0, [r4, #2945]	@ zero_extendqisi2
	mov	r1, r9
	mov	r2, #1
	bl	gc_search_src_blk
	cmp	r0, #0
	ble	.L4205
.L4206:
	mov	r3, #1
	b	.L4377
.L4205:
	ldr	r3, .L4379
	mov	r1, #3
	mov	r2, #1
	ldrb	r0, [r3, #2945]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	bgt	.L4206
	b	.L4300
.L4197:
	ldr	fp, .L4379+20
	ldr	r3, [fp]
	str	fp, [sp, #28]
	tst	r3, #256
	beq	.L4207
	ldrb	r2, [r2, #2945]	@ zero_extendqisi2
	movw	r1, #2183
	str	r8, [sp]
	ldrh	r3, [r5, #124]
	ldr	r0, .L4379+24
	str	r3, [sp, #4]
	ldrh	r3, [r5, #120]
	str	r3, [sp, #8]
	ldrh	r3, [r5, #122]
	str	r10, [sp, #16]
	str	r3, [sp, #12]
	mov	r3, r7
	bl	printk
.L4207:
	cmp	r10, #1
	bne	.L4208
	bl	gc_scan_static_data
	ldr	r3, [r4, #580]
	ldrh	r3, [r3, #122]
	cmp	r3, #0
	beq	.L4209
.L4210:
	mov	r3, #1
	strb	r3, [r4, #2945]
	b	.L4377
.L4209:
	bl	gc_static_wearleveling
	cmp	r0, #0
	bne	.L4210
	bl	gc_block_vpn_scan
	cmp	r8, #0
	beq	.L4305
	cmp	r7, r9
	bcs	.L4213
	ldr	r2, .L4379
	movw	r3, #2974
	ldrh	r3, [r2, r3]
	cmp	r3, r7
	bhi	.L4214
.L4213:
	movw	r1, #2974
	add	r3, r7, r9
	ldrh	r1, [r4, r1]
	ldr	r2, .L4379
	cmp	r3, r1
	blt	.L4214
	add	r3, r2, #2976
	movw	r1, #2282
	ldrh	r2, [r2, r1]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L4305
.L4214:
	ldr	r5, .L4379
	mov	fp, #1
	ldr	r2, [r6, #-3320]
	add	r1, r5, #564
	strb	fp, [r4, #2945]
	add	r2, r2, fp
	str	r2, [r6, #-3320]
	ldrh	r1, [r1]
	cmp	r2, r1, lsr #5
	bls	.L4216
	movw	r2, #2282
	ldrh	r2, [r5, r2]
	cmp	r2, r8
	bls	.L4216
	ldr	r0, .L4379+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4305
	ldr	r2, [r5, #576]
	mov	r0, r0, asl fp
	ldrh	r1, [r2, r0]
	add	r2, r5, #2960
	ldrh	r0, [r2]
	ldrb	r2, [r5, #2964]	@ zero_extendqisi2
	mul	r2, r2, r0
	cmp	r1, r2
	bgt	.L4305
	ldr	r2, .L4379+8
	mov	r1, #0
	mov	r0, fp
	str	r1, [r2, #-3320]
	mov	r1, #2
	b	.L4369
.L4216:
	ldr	r3, .L4379
	movw	r1, #2282
	ldrh	r1, [r4, r1]
	add	r2, r3, #2976
	ldrh	r2, [r2]
	cmp	r1, r2
	movcs	r0, #1
	movcs	r1, #2
	movcs	r2, r0
	bcs	.L4370
	movw	r2, #2278
	ldrh	r5, [r3, r2]
	cmp	r5, #0
	bne	.L4220
	movw	r2, #2280
	ldrh	r3, [r3, r2]
	cmp	r3, #8
	bls	.L4212
.L4220:
	mov	r0, #1
	mov	r1, r0
.L4369:
	mov	r2, #4
.L4370:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4206
	b	.L4305
.L4208:
	movw	r3, #2292
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bcc	.L4300
.L4305:
	mov	r5, #16
.L4212:
	ldr	r2, [r4, #2304]
	ldr	r3, .L4379
	cmp	r2, #0
	beq	.L4221
	mov	ip, #0
	ldr	r0, .L4379+12
	mov	r1, ip
	str	ip, [r3, #2304]
	mov	fp, #1
	str	r3, [sp, #36]
	strb	fp, [r3, #2945]
	str	ip, [sp, #32]
	bl	_list_get_gc_head_node
	movw	lr, #65535
	cmp	r0, lr
	beq	.L4222
	ldr	r3, [sp, #36]
	mov	r1, r0, asl fp
	ldr	ip, [sp, #32]
	ldr	lr, [r3, #576]
	ldrh	r1, [lr, r1]
	cmp	r1, #8
	bhi	.L4222
	mov	r1, ip
	mov	r2, fp
	str	fp, [r3, #2304]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4206
.L4222:
	ldr	r0, .L4379+28
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	fp, .L4379
	cmp	r0, r2
	beq	.L4223
	ldr	r2, [fp, #576]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4223
	mov	r2, #1
	mov	r1, #0
	str	r2, [fp, #2304]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4206
.L4223:
	mov	r1, #0
	ldr	r0, .L4379+32
	bl	_list_get_gc_head_node
	movw	r1, #65535
	ldr	fp, .L4379
	cmp	r0, r1
	beq	.L4221
	ldr	r1, [fp, #576]
	mov	r3, r0, asl #1
	ldrh	r3, [r1, r3]
	cmp	r3, #4
	bhi	.L4221
	mov	r1, #1
	mov	r2, #0
	str	r1, [fp, #2304]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4206
.L4221:
	ldr	lr, [r4, #580]
	mov	r0, #1
	ldr	fp, .L4379
	strb	r0, [r4, #2945]
	ldrh	ip, [lr, #124]
	cmp	ip, #0
	beq	.L4224
	ldr	r3, [sp, #28]
	mov	r2, #0
	strb	r0, [r6, #-3322]
	strb	r2, [fp, #2945]
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4225
	stmia	sp, {r8, ip}
	movw	r1, #2273
	ldrh	r3, [lr, #120]
	ldr	r0, .L4379+36
	str	r3, [sp, #8]
	ldrh	r3, [lr, #122]
	str	r3, [sp, #12]
	mov	r3, r7
	bl	printk
	b	.L4225
.L4224:
	movw	r2, #2292
	ldrh	r1, [fp, r2]
	cmp	r7, r1
	bcs	.L4226
	cmp	r8, #0
	beq	.L4227
	cmp	r8, #16
	bls	.L4228
	add	r2, fp, #2976
	movw	r1, #2282
	ldrh	r1, [fp, r1]
	ldrh	r2, [r2]
	cmp	r1, r2
	bhi	.L4228
	mov	r1, r0
	mov	r2, #4
	str	ip, [sp, #28]
	bl	gc_search_src_blk
	uxth	r0, r0
	ldr	ip, [sp, #28]
	cmp	r0, #0
	ldreqb	r0, [fp, #2945]	@ zero_extendqisi2
	beq	.L4375
	ldr	r0, .L4379+12
	mov	r1, ip
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4298
	ldr	ip, [fp, #576]
	mov	r1, r0, asl #1
	ldr	r2, [r6, #-3320]
	ldrh	r1, [ip, r1]
	add	ip, fp, #2960
	add	r2, r2, #1
	str	r2, [r6, #-3320]
	ldrh	ip, [ip]
	cmp	ip, r1
	bcs	.L4232
	add	ip, fp, #564
	ldrh	ip, [ip]
	cmp	r2, ip, lsr #4
	bls	.L4298
	movw	r2, #2294
	ldrh	r3, [fp, r2]
	cmp	r3, r1
	bls	.L4298
.L4232:
	mov	r1, #0
	ldrb	r2, [r4, #2945]	@ zero_extendqisi2
	bl	gc_add_sblk
	mov	r3, #1
	str	r3, [r4, #2304]
	mov	r3, #0
	str	r3, [r6, #-3320]
	b	.L4298
.L4228:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r4, #2945]	@ zero_extendqisi2
	bne	.L4298
	b	.L4373
.L4227:
	ldr	r3, [sp, #28]
	strb	r8, [fp, #2945]
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4234
	str	r8, [sp]
	movw	r1, #2303
	str	r8, [sp, #4]
	mov	r2, r8
	ldrh	r3, [lr, #120]
	ldr	r0, .L4379+36
	str	r3, [sp, #8]
	ldrh	r3, [lr, #122]
	str	r3, [sp, #12]
	mov	r3, r7
	bl	printk
.L4234:
	cmp	r7, #16
	ldrb	r0, [r4, #2945]	@ zero_extendqisi2
	bls	.L4235
.L4375:
	mov	r1, #3
	mov	r2, #4
	b	.L4372
.L4235:
	mov	r1, #1
	mov	r2, r1
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4298
	ldr	r3, .L4379
	ldrb	r0, [r3, #2945]	@ zero_extendqisi2
.L4373:
	mov	r1, #3
	mov	r2, #2
.L4372:
	bl	gc_search_src_blk
	uxth	r0, r0
	b	.L4230
.L4226:
	cmp	r10, #1
	bne	.L4225
	cmp	r7, r1, asl #1
	bge	.L4236
	cmp	r9, r8, lsr #1
	bcs	.L4237
	add	r1, fp, #2976
	movw	r0, #2282
	ldrh	r0, [fp, r0]
	ldrh	r1, [r1]
	cmp	r0, r1
	movwcc	r1, #2974
	ldrcch	r1, [fp, r1]
	movcc	r1, r1, lsr #2
	strcch	r1, [fp, r2]	@ movhi
	bcc	.L4225
.L4237:
	ldr	r2, .L4379
	mov	r1, #0
	ldr	r0, .L4379+12
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	mov	r10, r0
	ldr	r2, [sp, #28]
	bne	.L4238
.L4243:
	cmp	r8, #1
	bhi	.L4239
	b	.L4240
.L4238:
	add	r1, r2, #564
	ldr	r3, [r6, #-3320]
	ldr	fp, .L4379+8
	ldrh	r1, [r1]
	add	r3, r3, #1
	str	r3, [r6, #-3320]
	cmp	r3, r1, lsr #4
	mov	r3, r0, asl #1
	bls	.L4241
	ldr	ip, [r2, #576]
	movw	lr, #2294
	ldrh	r2, [r2, lr]
	mov	r1, #0
	str	r1, [fp, #-3320]
	ldrh	ip, [ip, r3]
	cmp	ip, r2
	bcs	.L4241
	mov	r2, #1
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r3, [sp, #28]
	cmp	r0, #0
	movne	r3, #1
	strneb	r3, [fp, #-3322]
	bne	.L4225
.L4241:
	ldr	r1, [r4, #576]
	ldr	r2, .L4379
	ldrh	r1, [r1, r3]
	add	r3, r2, #2960
	ldrh	r3, [r3]
	cmp	r1, r3, lsr #1
	bhi	.L4242
	mov	r0, r10
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
	b	.L4298
.L4242:
	movw	r3, #2278
	movw	r0, #2280
	ldrh	r0, [r2, r0]
	ldrh	r3, [r2, r3]
	add	r3, r3, r0
	movw	r0, #2974
	ldrh	r0, [r2, r0]
	cmp	r3, r0, asl #1
	ble	.L4243
	movw	r3, #2294
	ldrh	r3, [r2, r3]
	cmp	r3, r1
	bcc	.L4240
	b	.L4243
.L4239:
	cmp	r8, #16
	mov	r0, #1
	ldr	r10, .L4379
	strb	r0, [r4, #2945]
	bls	.L4244
	add	r3, r10, #2976
	movw	r2, #2282
	ldrh	r2, [r10, r2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bhi	.L4244
	mov	r1, r0
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r10, #2945]	@ zero_extendqisi2
	moveq	r1, #3
	moveq	r2, #4
	bne	.L4245
	b	.L4371
.L4244:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4245
	ldrb	r0, [r4, #2945]	@ zero_extendqisi2
	mov	r1, #3
	mov	r2, #2
.L4371:
	bl	gc_search_src_blk
	uxth	r0, r0
.L4245:
	movw	r3, #2974
	cmp	r7, r9, lsr #1
	ldrh	r3, [r4, r3]
	movw	r2, #2292
	movhi	r3, r3, lsr #2
	movls	r3, r3, lsr #1
	strh	r3, [r4, r2]	@ movhi
	b	.L4230
.L4240:
	cmp	r7, r9
	bcs	.L4247
	ldr	r0, .L4379+32
	mov	r1, #4
	mov	r8, #0
	strb	r8, [r4, #2945]
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r7, .L4379
	cmp	r0, r3
	beq	.L4248
	ldr	r3, [r7, #576]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r3, r7, #2960
	ldrh	r1, [r3]
	ldrb	r3, [r7, #2964]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asr #1
	ble	.L4249
.L4248:
	movw	r3, #2974
	movw	r2, #2292
	ldrh	r3, [r7, r3]
	mov	r3, r3, lsr #2
	strh	r3, [r7, r2]	@ movhi
	b	.L4225
.L4249:
	mov	r2, #4
	mov	r0, r8
	mov	r1, #3
	bl	gc_search_src_blk
	movw	r3, #2974
	ldrh	r3, [r7, r3]
	movw	r2, #2292
	mov	r3, r3, lsr #1
	strh	r3, [r7, r2]	@ movhi
	uxth	r0, r0
	b	.L4230
.L4247:
	movw	r3, #2974
	movw	r2, #2292
	ldrh	r3, [r4, r3]
	mov	r3, r3, lsr #2
	strh	r3, [r4, r2]	@ movhi
	b	.L4225
.L4236:
	movw	r1, #2974
	mov	r5, ip
	ldrh	r1, [fp, r1]
	mov	r1, r1, lsr #2
	strh	r1, [fp, r2]	@ movhi
	b	.L4225
.L4230:
	cmp	r0, #0
	beq	.L4225
.L4298:
	mov	r3, #1
	strb	r3, [r6, #-3322]
	b	.L4225
.L4191:
	movw	r5, #2312
	movw	r3, #65535
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	bne	.L4250
	bl	gc_get_src_blk
	ldr	r3, .L4379
	strh	r0, [r3, r5]	@ movhi
.L4250:
	movw	r3, #2312
	movw	r0, #65535
	ldrh	r1, [r4, r3]
	ldr	r2, .L4379
	cmp	r1, r0
	beq	.L4251
	add	r0, r2, #564
	ldrh	r0, [r0]
	cmp	r0, r1
	mvnls	r1, #0
	strlsh	r1, [r2, r3]	@ movhi
.L4251:
	movw	r3, #2312
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	beq	.L4378
	ldr	r1, .L4379+56
	ldr	r2, .L4379
	ldrh	r0, [r1, #52]
	ldr	r2, [r2, #568]
	cmp	r0, #0
	addne	ip, r1, #52
	add	r2, r2, r3, asl #2
	movne	r1, #0
	beq	.L4253
.L4254:
	uxth	lr, r1
	cmp	lr, r0
	bcs	.L4253
	ldrh	lr, [ip, #2]!
	add	r1, r1, #1
	cmp	lr, r3
	bne	.L4254
.L4258:
	movw	r3, #2312
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	b	.L4300
.L4253:
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r2, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r2, #224
	movne	r2, r1
	orreq	r2, r1, #1
	cmp	r2, #0
	beq	.L4256
	ldr	r2, [r4, #576]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4258
	ldr	r1, .L4379+64
	movw	r2, #2538
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
	b	.L4258
.L4256:
	mov	r3, #2
	b	.L4377
.L4192:
	bl	gc_scan_src_blk
	cmn	r0, #1
	moveq	r3, #3
	beq	.L4377
	movw	r3, #2312
	ldr	r2, .L4379+56
	ldrh	r3, [r4, r3]
	movw	r1, #65535
	cmp	r3, r1
	mov	r7, r2
	beq	.L4206
	ldrh	r1, [r2, #20]
	cmp	r1, #0
	movne	r3, #4
	strneb	r3, [r6, #-3322]
	movne	r3, #0
	strneh	r3, [r2, #22]	@ movhi
	bne	.L4300
.L4260:
	mov	r2, #1
	strb	r2, [r6, #-3322]
	ldr	r2, .L4379
	mov	r3, r3, asl #1
	ldr	r2, [r2, #576]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4261
	ldr	r1, .L4379+64
	movw	r2, #2566
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
.L4261:
	movw	r5, #2312
	ldr	r6, .L4379+56
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #576]
	mov	r5, #0
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r7, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strlsh	r3, [r6, #26]	@ movhi
	bls	.L4258
	strh	r5, [r6, #26]	@ movhi
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, r5
	bl	ftl_info_flush
	b	.L4258
.L4193:
	ldr	r8, .L4379+56
.L4346:
	bl	gc_scan_src_blk_one_page
	ldr	r0, .L4379
	ldrh	r2, [r8, #2]
	add	r3, r0, #2960
	ldr	r5, .L4379+56
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L4264
	cmp	r7, #7
	bls	.L4346
	b	.L4300
.L4264:
	ldrh	r3, [r5, #20]
	ldr	r2, .L4379+20
	cmp	r3, #0
	beq	.L4265
	ldr	r2, [r2]
	mov	r1, #4
	strb	r1, [r6, #-3322]
	mov	r1, #0
	tst	r2, #256
	strh	r1, [r5, #22]	@ movhi
	beq	.L4266
	ldrh	r1, [r5]
	ldr	ip, [r0, #576]
	ldr	r0, .L4379+40
	mov	r2, r1, asl #1
	ldrh	r2, [ip, r2]
	bl	printk
.L4266:
	movw	r3, #2312
	ldr	r2, [r4, #576]
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L4267
	ldr	r1, .L4379+64
	movw	r2, #2600
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
.L4267:
	movw	r3, #2312
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #576]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	b	.L4300
.L4265:
	mov	r3, #1
	strb	r3, [r6, #-3322]
	ldr	r3, [r2]
	ldrh	r1, [r5]
	ldr	r7, [r0, #568]
	tst	r3, #256
	add	r7, r7, r1, asl #2
	beq	.L4268
	ldrb	r2, [r7, #2]	@ zero_extendqisi2
	ldr	r0, .L4379+44
	mov	r2, r2, lsr #5
	bl	printk
.L4268:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4269
	ldr	r1, .L4379+64
	movw	r2, #2610
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
.L4269:
	movw	r7, #2312
	ldr	r6, .L4379+56
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
	bls	.L4300
	b	.L4376
.L4194:
	cmp	r10, #0
	bne	.L4271
	movw	r3, #2292
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bcc	.L4300
.L4271:
	ldrh	r2, [r5, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4272
	ldrb	r8, [r4, #2945]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L4272
	ldr	r9, .L4379+56
	bl	ftl_flush
	movw	r3, #570
	mov	r1, #5
	ldrh	r0, [r9, r3]
	cmp	r0, #0
	movne	r0, r8
	bl	zftl_gc_get_free_sblk
	movw	r3, #65535
	cmp	r0, r3
	mov	r7, r0
	beq	.L4275
	ldr	r8, [r4, #568]
	add	r8, r8, r0, asl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L4276
	ldr	r1, .L4379+64
	movw	r2, #2639
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
.L4276:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	and	r3, r3, #15
	orr	r3, r3, #176
	strb	r3, [r8, #2]
.L4299:
	mov	r0, r7
	mov	r1, #1
	bl	ftl_erase_sblk
	mov	r3, #5
	add	r1, r5, #96
	strb	r3, [r5, #84]
	mov	r0, r7
	ldr	r10, .L4379+48
	bl	ftl_get_blk_list_in_sblk
	mov	r8, #0
	strh	r7, [r5, #80]	@ movhi
	mov	r1, #255
	mov	fp, r10
	strh	r8, [r5, #82]	@ movhi
	ldrh	r3, [fp], #-80
	strb	r8, [r5, #85]
	strh	r8, [r5, #90]	@ movhi
	uxtb	r0, r0
	strb	r0, [r5, #89]
	smulbb	r0, r3, r0
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	strh	r0, [r5, #86]	@ movhi
	movw	r5, #2962
	ldrh	r2, [r4, r5]
	ldr	r0, [r6, #-3316]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r4, r5]
	ldrb	r3, [r4, #2964]	@ zero_extendqisi2
	mov	r1, #255
	ldr	r0, [r6, #-3312]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r3, [r4, r5]
	ldrb	r2, [r4, #2964]	@ zero_extendqisi2
	mov	r1, #255
	ldr	r0, [r4, #2940]
	mvn	r5, #0
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, [r4, #580]
	strh	r5, [r3, #128]	@ movhi
	strh	r5, [r3, #130]	@ movhi
	str	r7, [r3, #132]
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2288]
	mov	r0, r8
	strh	r8, [r9, #52]	@ movhi
	strh	r7, [r3, #126]	@ movhi
	movw	r3, #566
	strh	r8, [r9, r3]	@ movhi
	strh	r8, [fp]	@ movhi
	strh	r8, [r10, #-76]	@ movhi
	str	r5, [r4, #2888]
	bl	ftl_info_flush
	b	.L4300
.L4272:
	cmp	r10, #1
	ldr	r9, .L4379
	ldr	r10, .L4379+56
	movne	r8, #1
	moveq	r8, #4
	cmp	r7, #15
	addls	r8, r8, #4
.L4279:
	sub	r8, r8, #1
	uxtb	r8, r8
	cmp	r8, #255
	beq	.L4300
	bl	gc_do_copy_back
	ldrb	r3, [r4, #2945]	@ zero_extendqisi2
	ldr	r7, .L4379+56
	cmp	r3, #0
	bne	.L4280
	ldrb	r3, [r9, #2262]	@ zero_extendqisi2
	cmp	r3, #3
	bhi	.L4281
	bl	ftl_write_commit
.L4281:
	ldrh	r2, [r10, #22]
	ldrh	r3, [r10, #20]
	cmp	r2, r3
	bcc	.L4279
	mov	r3, #1
	strb	r3, [r6, #-3322]
	bl	ftl_write_commit
	bl	ftl_flush
	ldr	r3, .L4379+56
	ldr	r2, .L4379
	ldrh	r3, [r3]
	ldr	r2, [r2, #576]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4283
	ldr	r1, .L4379+64
	movw	r2, #2714
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
.L4283:
	movw	r3, #2312
	ldr	r1, [r4, #576]
	ldrh	r0, [r4, r3]
	mov	r2, r0, asl #1
	ldrh	ip, [r1, r2]
	cmp	ip, #0
	bne	.L4284
	strh	ip, [r1, r2]	@ movhi
	ldr	r2, .L4379
	ldrh	r0, [r2, r3]
	bl	ftl_free_sblk
	b	.L4258
.L4284:
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	b	.L4258
.L4380:
	.align	2
.L4379:
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR3
	.word	.LANCHOR0+2956
	.word	.LANCHOR0+2976
	.word	.LANCHOR2
	.word	.LC304
	.word	.LANCHOR0+2948
	.word	.LANCHOR0+2968
	.word	.LC305
	.word	.LC306
	.word	.LC307
	.word	.LANCHOR0+2960
	.word	.LANCHOR0+2884
	.word	.LANCHOR0+2312
	.word	.LANCHOR0
	.word	.LANCHOR1+2180
	.word	.LC0
.L4280:
	ldr	r3, .L4379+52
	ldrh	r2, [r3]
	cmp	r2, #0
	beq	.L4285
	mov	r5, #0
	strh	r5, [r3]	@ movhi
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	r3, .L4379+60
	ldr	r0, [r3, #2888]
	cmn	r0, #1
	beq	.L4286
	bl	ftl_mask_bad_block
.L4286:
	ldr	r3, [r4, #580]
	str	r5, [r4, #2888]
	strh	r5, [r7, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r0, [r4, #2320]
	ldr	r2, [r4, #580]
	mvn	r3, #0
	ldr	r1, [r4, #2288]
	cmp	r0, #0
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	beq	.L4287
	bl	zbuf_free
.L4287:
	mov	r5, #0
	str	r5, [r4, #2320]
	bl	flt_sys_flush
	ldr	r1, .L4379+64
	mov	r2, #2752
	ldr	r0, .L4379+68
	strb	r5, [r6, #-3322]
	bl	printk
	bl	dump_stack
	b	.L4300
.L4285:
	ldrh	r3, [r5, #86]
	ldrh	r2, [r10, #22]
	cmp	r3, #1
	ldrh	r3, [r10, #20]
	bls	.L4288
	cmp	r2, r3
	bcc	.L4279
	mov	r3, #1
	strb	r3, [r6, #-3322]
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L4300
.L4288:
	cmp	r2, r3
	mov	r1, #5
	strb	r1, [r6, #-3322]
	bcc	.L4289
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L4289:
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldrh	r2, [r5, #80]
	ldr	r3, [r4, #580]
	ldr	r5, .L4379+60
	strh	r2, [r3, #128]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldrb	r1, [r4, #684]	@ zero_extendqisi2
	mov	r3, #0
	strh	r3, [r7, #12]	@ movhi
	add	r3, r5, #2960
	cmp	r1, #0
	ldr	r2, .L4379+56
	ldrh	r3, [r3]
	movwne	r1, #2962
	ldrneh	r1, [r5, r1]
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r4, #2937]	@ zero_extendqisi2
	strneh	r1, [r2, #14]	@ movhi
	movne	r1, #1
	strh	r3, [r7, #16]	@ movhi
	strneh	r1, [r2, #16]	@ movhi
	cmp	r3, #2
	bne	.L4292
	ldrh	r3, [r7, #14]
	mov	r3, r3, asl #1
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r4, #2938]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L4379+56
	moveq	r2, #1
	streqh	r2, [r3, #16]	@ movhi
.L4292:
	mov	r3, #0
	strh	r3, [r7, #18]	@ movhi
	b	.L4300
.L4301:
	ldr	r8, .L4379+56
	mov	r5, #0
.L4195:
	bl	gc_check_data_one_wl
	subs	fp, r0, #0
	beq	.L4295
	ldr	r3, .L4379+56
	mov	r5, #0
	strh	r5, [r3, #52]	@ movhi
	ldr	r3, [r4, #580]
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r2, [r4, #580]
	ldr	r1, [r4, #2288]
	mvn	r3, #0
	ldr	r0, [r4, #2320]
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	bl	zbuf_free
	str	r5, [r4, #2320]
	strb	r5, [r6, #-3322]
.L4376:
	bl	flt_sys_flush
	b	.L4300
.L4295:
	ldrh	r2, [r8, #12]
	ldrh	r3, [r8, #14]
	ldr	r9, .L4379+60
	cmp	r2, r3
	bcc	.L4296
	ldr	r0, [r9, #2320]
	mov	r3, #6
	strb	r3, [r6, #-3322]
	bl	zbuf_free
	str	fp, [r9, #2320]
	b	.L4300
.L4296:
	cmp	r7, #15
	bls	.L4195
	cmp	r10, #1
	bne	.L4300
	add	r5, r5, #1
	uxtb	r5, r5
	cmp	r5, #4
	bls	.L4195
	b	.L4300
.L4196:
	bl	gc_update_l2p_map_new
	mvn	r7, #0
	bl	gc_free_src_blk
	bl	ftl_flush
	bl	pm_flush
	strh	r7, [r5, #80]	@ movhi
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #2288]
	mov	r0, #0
	strh	r7, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L4378:
	mov	r3, #0
.L4377:
	strb	r3, [r6, #-3322]
.L4300:
	mov	r0, #16
	b	.L4367
.L4225:
	mov	r0, r5
	b	.L4367
.L4275:
	ldr	r1, .L4379+64
	movw	r2, #2645
	ldr	r0, .L4379+68
	bl	printk
	bl	dump_stack
	b	.L4299
.L4367:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	zftl_do_gc, .-zftl_do_gc
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
	ldr	r3, .L4409
	.pad #28
	sub	sp, sp, #28
	mov	r5, r0
	mov	r4, r1
	mov	r7, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4382
	ldr	r3, [r9]
	mov	r1, r5
	ldr	r0, .L4409+4
	mov	r2, r4
	str	r3, [sp]
	mov	r3, r7
	bl	printk
.L4382:
	cmp	r5, #0
	ldreq	r3, .L4409+8
	moveq	r5, #24576
	ldreq	r3, [r3, #520]
	beq	.L4384
.L4383:
	cmp	r5, #3
	bhi	.L4398
	mov	r5, r5, asl #13
	mov	r3, #8192
.L4384:
	add	r2, r4, r7
	cmp	r2, r3
	bhi	.L4398
	ldr	r8, .L4409+8
	add	r4, r5, r4
	mov	r0, r4
	ldrb	r5, [r8, #2260]	@ zero_extendqisi2
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	mov	r10, r0
	sub	r0, r7, #1
	add	r0, r0, r4
	mov	r6, r10
	bl	__aeabi_uidiv
	add	r3, r4, r7
	rsb	r5, r10, r0
	str	r0, [sp, #12]
	add	r5, r5, #1
	str	r3, [sp, #16]
.L4386:
	cmp	r5, #0
	beq	.L4407
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	bne	.L4387
	bl	ftl_write_commit
	b	.L4386
.L4387:
	mov	r3, #0
	strb	r3, [fp, #41]
	ldr	r3, [sp, #12]
	rsb	r0, r10, r6
	clz	r0, r0
	ldrb	r2, [r8, #2260]	@ zero_extendqisi2
	rsb	r1, r3, r6
	clz	r1, r1
	mov	r0, r0, lsr #5
	strb	r2, [fp, #40]
	mov	r1, r1, lsr #5
	orrs	r3, r1, r0
	beq	.L4390
	cmp	r0, #0
	beq	.L4391
	mov	r1, r2
	mov	r0, r4
	str	r2, [sp, #20]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #20]
	uxtb	r1, r1
	strb	r1, [fp, #41]
	rsb	r2, r1, r2
	uxtb	r2, r2
	cmp	r2, r7
	strhib	r7, [fp, #40]
	bhi	.L4390
	b	.L4406
.L4391:
	cmp	r1, #0
	beq	.L4390
	smulbb	r2, r6, r2
	ldr	r3, [sp, #16]
	rsb	r2, r2, r3
.L4406:
	strb	r2, [fp, #40]
.L4390:
	ldrb	r0, [fp, #41]	@ zero_extendqisi2
	sub	r5, r5, #1
	ldrb	r2, [fp, #40]	@ zero_extendqisi2
	ldr	r1, [fp, #4]
	mov	r2, r2, asl #9
	add	r0, r1, r0, asl #9
	mov	r1, r9
	bl	ftl_memcpy
	ldr	r1, [r8, #2288]
	str	r6, [fp, #20]
	add	r6, r6, #1
	ldr	r2, [r1, #8]
	add	r0, r2, #1
	str	r0, [r1, #8]
	str	r2, [fp, #16]
	mov	r0, fp
	bl	ftl_write_buf
	ldrb	r3, [fp, #40]	@ zero_extendqisi2
	add	r9, r9, r3, asl #9
	b	.L4386
.L4407:
	bl	ftl_write_commit
	mov	r0, r5
	mov	r1, #1
	ldr	r4, .L4409+12
	bl	zftl_do_gc
.L4395:
	ldr	r2, .L4409+16
	ldrh	r3, [r4]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4408
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	b	.L4395
.L4408:
	bl	timer_get_time
	ldr	r3, .L4409+20
	str	r0, [r3, #-2472]
	mov	r0, #0
	b	.L4385
.L4398:
	mvn	r0, #0
.L4385:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4410:
	.align	2
.L4409:
	.word	.LANCHOR2
	.word	.LC308
	.word	.LANCHOR0
	.word	.LANCHOR0+2272
	.word	.LANCHOR0+2276
	.word	.LANCHOR5
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
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4465
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r3]
	mov	r4, r1
	ldr	r6, .L4465+4
	add	r1, r0, r1
	str	r3, [sp, #12]
	str	r2, [sp, #36]
	ldr	r2, [r6, #520]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L4415
	ldr	r3, .L4465+8
	add	r8, r0, #24576
	ldr	r10, .L4465+12
	ldr	r2, [r3]
	ldr	r5, [r10, #-2468]
	tst	r2, #4096
	str	r10, [sp, #8]
	add	r1, r4, r5
	str	r3, [sp, #16]
	str	r1, [r10, #-2468]
	beq	.L4416
	mov	r3, #0
	ldr	r0, .L4465+16
	str	r3, [sp]
	mov	r2, r8
	mov	r3, r4
	bl	printk
.L4416:
	ldr	r3, [r6, #2288]
	ldr	r9, [r3, #8]
	add	r2, r9, #1
	str	r2, [r3, #8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	r7, [r6, #2260]	@ zero_extendqisi2
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	mls	r10, r7, r0, r8
	mov	r5, r0
	cmp	r10, #0
	beq	.L4417
	rsb	r7, r10, r7
	cmp	r7, r4
	movcs	r7, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4418
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
.L4418:
	ldr	r3, [sp, #28]
	uxth	fp, r7
	cmn	r3, #1
	beq	.L4420
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4420
	ldr	r0, [r3, #4]
	mov	r2, fp, asl #9
	mov	r1, #0
	str	r5, [r3, #20]
	strb	r10, [r3, #41]
	add	r0, r0, r10, asl #9
	strb	r7, [r3, #40]
	str	r9, [r3, #16]
	str	r3, [sp, #20]
	bl	ftl_memset
	ldr	r3, [sp, #20]
	mov	r0, r3
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r6, #2288]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4420:
	add	r5, r5, #1
	rsb	r4, fp, r4
.L4417:
	cmp	r4, #0
	beq	.L4422
	bl	ftl_flush
.L4422:
	ldr	r7, .L4465+4
	mvn	r3, #0
	str	r3, [sp, #32]
	add	r10, r7, #2928
.L4423:
	ldrb	r3, [r6, #2260]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L4464
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	beq	.L4424
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	beq	.L4426
	ldrb	r2, [r7, #2260]	@ zero_extendqisi2
	mov	r1, #0
	str	r5, [fp, #20]
	strb	r1, [fp, #41]
	strb	r2, [fp, #40]
	mov	r2, r2, asl #9
	str	r9, [fp, #16]
	ldr	r0, [fp, #4]
	bl	ftl_memset
	mov	r0, fp
	bl	ftl_write_buf
	bl	ftl_write_commit
	b	.L4463
.L4424:
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4426
	add	r1, sp, #32
	mov	r2, #1
	mov	r0, r5
	bl	pm_log2phys
	ldrb	r3, [r7, #676]	@ zero_extendqisi2
	ldrh	r0, [r10]
	rsb	r3, r3, #24
	ldrb	r1, [r7, #2930]	@ zero_extendqisi2
	rsb	r3, r0, r3
	ldr	r2, [sp, #28]
	mov	r2, r2, lsr r0
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L4463:
	ldr	r2, [r7, #2288]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4426:
	ldrb	r3, [r6, #2260]	@ zero_extendqisi2
	add	r5, r5, #1
	rsb	r4, r3, r4
	b	.L4423
.L4464:
	cmp	r4, #0
	beq	.L4430
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4431
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
.L4431:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4430
	mov	r0, #0
	bl	buf_alloc
	subs	r7, r0, #0
	beq	.L4430
	mov	r3, #0
	strb	r3, [r7, #41]
	ldrb	r3, [r6, #2260]	@ zero_extendqisi2
	str	r5, [r7, #20]
	cmp	r4, r3
	strb	r4, [r7, #40]
	str	r9, [r7, #16]
	bcc	.L4433
	ldr	r1, .L4465+20
	movw	r2, #1259
	ldr	r0, .L4465+24
	bl	printk
	bl	dump_stack
.L4433:
	mov	r2, r4, asl #9
	mov	r1, #0
	ldr	r0, [r7, #4]
	bl	ftl_memset
	mov	r0, r7
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r6, #2288]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4430:
	ldr	r3, [sp, #8]
	ldr	r1, [r3, #-2468]
	cmp	r1, #8192
	bls	.L4437
	ldr	r3, [sp, #16]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4434
	mov	r3, #0
	ldr	r0, .L4465+16
	str	r3, [sp]
	mov	r2, r8
	mov	r3, r4
	bl	printk
.L4434:
	ldr	r3, [sp, #8]
	mov	r4, #0
	str	r4, [r3, #-2468]
	bl	flt_sys_flush
	mov	r3, #1
	str	r3, [r6, #2304]
.L4437:
	mov	r0, #0
.L4415:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L4435
	bl	__stack_chk_fail
.L4435:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4466:
	.align	2
.L4465:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LC309
	.word	.LANCHOR1+2192
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
	ldr	r4, .L4473
	mov	r2, #4
	ldr	r0, .L4473+4
	mov	r6, #0
	ldr	r5, .L4473+8
	movw	r7, #65535
	ldr	r3, [r4, #2288]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2288]
	ldr	r0, .L4473+12
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r5]
	bl	rknand_print_hex
.L4468:
	ldrh	r2, [r5]
	uxth	r3, r6
	cmp	r2, r3
	bls	.L4472
	add	r3, r3, #208
	ldr	r2, [r4, #2288]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r7
	beq	.L4469
	mov	r1, #0
	bl	ftl_sblk_dump
.L4469:
	add	r6, r6, #1
	b	.L4468
.L4472:
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4474:
	.align	2
.L4473:
	.word	.LANCHOR0
	.word	.LC267
	.word	.LANCHOR3-3296
	.word	.LC268
	.fnend
	.size	dump_pm_blk, .-dump_pm_blk
	.align	2
	.global	id_block_prog_msb_ff_data
	.type	id_block_prog_msb_ff_data, %function
id_block_prog_msb_ff_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r2
	ldr	r5, .L4482
	ldrb	r2, [r5, #590]	@ zero_extendqisi2
	ldr	r3, [r5, #584]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	bne	.L4475
	sub	r2, r3, #5
	mov	r8, r0
	cmp	r3, #68
	cmpne	r2, #2
	mov	r9, r1
	bls	.L4477
	sub	r3, r3, #19
	tst	r3, #239
	bne	.L4475
.L4477:
	ldr	fp, .L4482
	ldr	r7, .L4482+4
.L4479:
	ldr	r3, [r5, #584]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L4475
	mov	r6, r4, asl #1
	add	r10, r4, r9
	ldr	r0, .L4482+8
	mov	r1, r4
	mov	r2, r10
	ldrh	r3, [r7, r6]
	bl	printk
	ldrh	r2, [r7, r6]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4475
	ldr	r6, .L4482+12
	mov	r1, #255
	mov	r2, #16384
	add	r4, r4, #1
	ldr	r0, [r6, #-3272]
	uxth	r4, r4
	bl	ftl_memset
	ldr	r3, [fp, #584]
	ldr	r2, [r6, #-3272]
	mov	r0, r8
	mov	r1, r10
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r2
	bl	flash_prog_page
	b	.L4479
.L4475:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4483:
	.align	2
.L4482:
	.word	.LANCHOR0
	.word	.LANCHOR0+3800
	.word	.LC310
	.word	.LANCHOR3
	.fnend
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 144
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4557
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #164
	sub	sp, sp, #164
	ldr	r3, [r3]
	mov	r6, r0
	ldr	r9, .L4557+4
	mov	r0, #256000
	mov	r10, r1
	mov	r7, r2
	str	r3, [sp, #156]
	ldr	r3, [r9, #584]
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	r5, [r3, #10]
	ldr	r3, .L4557+8
	ldrh	r8, [r3, #30]
	ldrb	r3, [r9]	@ zero_extendqisi2
	str	r3, [sp, #60]
	ldrh	r3, [sp, #60]
	str	r3, [sp, #52]
	ldrb	r3, [r9, #590]	@ zero_extendqisi2
	str	r3, [sp, #68]
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #24]
	beq	.L4522
	ldr	r3, .L4557+12
	ldrb	r3, [r3, #-2486]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4523
	ldrb	r3, [r9]	@ zero_extendqisi2
	cmp	r3, #0
	ldrb	r3, [r9, #590]	@ zero_extendqisi2
	clzne	r3, r3
	movne	r3, r3, lsr #5
	bne	.L4523
	cmp	r3, #3
	ldr	r3, [sp, #52]
	moveq	r3, #3
	str	r3, [sp, #52]
	movne	r3, #0
	moveq	r3, #2
.L4523:
	add	r6, r6, #508
	mov	r0, #0
	add	r6, r6, #3
	str	r3, [sp, #20]
	bl	zftl_flash_exit_slc_mode
	ldrb	r3, [r9, #590]	@ zero_extendqisi2
	mov	r6, r6, lsr #9
	cmp	r6, #8
	strb	r3, [r9]
	bls	.L4525
	cmp	r6, #500
	bhi	.L4489
	b	.L4488
.L4525:
	mov	r6, #8
.L4488:
	ldr	r2, [r10]
	ldr	r3, .L4557+16
	cmp	r2, r3
	beq	.L4490
.L4489:
	ldr	r0, [sp, #24]
	bl	ftl_free
	b	.L4522
.L4490:
	uxth	r4, r4
	smulbb	r1, r4, r5
	uxth	r1, r1
	sub	r0, r1, #1
	add	r0, r0, r6
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63871
	str	r0, [sp, #72]
	add	r0, r10, #254976
	add	r0, r0, #512
.L4494:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L4491
	ldr	r1, [r10, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L4494
	b	.L4493
.L4491:
	ldr	r0, .L4557+20
	bl	printk
.L4493:
	smulbb	r3, r4, r8
	ldr	r0, .L4557+24
	mov	r1, r7
	mov	r2, #4
	ldr	r4, .L4557+4
	uxth	r3, r3
	str	r3, [sp, #56]
	mov	r3, #5
	bl	rknand_print_hex
	ldr	r0, .L4557+28
	mov	r1, r6
	mov	r2, r6
	bl	printk
	sub	r3, r7, #4
	str	r3, [sp, #44]
	mov	r3, #0
	str	r3, [sp, #36]
	str	r3, [sp, #28]
.L4519:
	ldr	r3, .L4557+12
	ldrb	r3, [r3, #-2486]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4495
	ldr	r3, [sp, #44]
	ldr	r1, [sp, #72]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #56]
	mul	fp, r3, r2
	ldr	r3, [sp, #28]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L4496
	ldr	r3, [sp, #44]
	ldr	r3, [r3]
	add	r3, r3, #1
	cmp	r2, r3
	bne	.L4496
	b	.L4497
.L4495:
	ldr	r3, [r4, #664]
	ldr	r2, [sp, #28]
	add	r3, r3, r2
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L4497
	ldr	r2, [sp, #56]
	mul	fp, r2, r3
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4496
	ldr	r3, [sp, #28]
	bic	r3, r3, #2
	cmp	r3, #1
	ldreqb	r3, [sp, #52]	@ zero_extendqisi2
	beq	.L4555
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
.L4555:
	strb	r3, [r4]
	strb	r3, [r4, #590]
.L4496:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	mov	r8, #0
	bl	ftl_memset
	ldr	r3, [r4, #584]
	mov	r0, fp
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	ldrh	r7, [r3, #10]
	ldr	r3, .L4557+8
	mov	r1, r2
	ldrh	r3, [r3, #30]
	str	r2, [sp, #40]
	ldrh	r5, [sp, #40]
	smulbb	r7, r7, r5
	smulbb	r5, r3, r5
	ldrb	r3, [r4, #656]	@ zero_extendqisi2
	strb	r8, [r4, #656]
	str	r3, [sp, #76]
	bl	__aeabi_uidiv
	uxth	r7, r7
	uxth	r5, r5
	mov	r1, r0
	mov	r0, r8
	bl	flash_erase_block
	cmp	r6, r7
	movls	r0, #1
	bls	.L4499
	mov	r0, r8
	add	r1, fp, r5
	bl	flash_erase_block
	mov	r0, #2
.L4499:
	ldr	r3, .L4557+4
	ldr	r2, [r3, #584]
	ldrh	r3, [r2, #10]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	mov	r3, r3, asl #2
	mul	r0, r0, r3
	bl	__aeabi_idiv
	mov	r1, r5
	str	r0, [sp, #80]
	mov	r0, fp
	bl	__aeabi_uidivmod
	rsb	r3, r1, fp
	str	r1, [sp, #64]
	cmp	fp, r3
	str	r3, [sp, #48]
	bne	.L4528
	ldrb	r3, [r4, #516]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L4528
	ldr	r3, .L4557+32
	mov	r1, #0
	mov	r2, #1024
	ldr	r5, [r3, #-3248]
	mov	r0, r5
	bl	ftl_memset
	ldr	r2, .L4557+36
	mov	r3, #12
	stmia	r5, {r2, r3}
	mov	r3, #0
	strb	r3, [r5, #16]
	str	r3, [r5, #12]
	ldrb	r3, [r4, #590]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r4, #584]
	ldrneb	r3, [r3, #29]	@ zero_extendqisi2
	strneb	r3, [r5, #16]
	mov	r3, #4
	strb	r3, [r5, #17]
	ldr	r3, [r4, #584]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	bl	__aeabi_idiv
	mov	r3, #0
	mov	r1, #12
	strb	r3, [r5, #20]
	strh	r3, [r5, #22]	@ movhi
	ldr	r2, [sp, #40]
	strh	r0, [r5, #18]	@ movhi
	add	r0, r5, #12
	cmp	r2, #8
	movhi	r2, #70
	movls	r2, #16
	strb	r2, [r5, #21]
	bl	js_hash
	sub	r3, r6, #4
	str	r0, [r5, #8]
	str	r3, [sp, #32]
	b	.L4500
.L4528:
	mov	r5, #0
	str	r6, [sp, #32]
.L4500:
	mov	r8, r10
	mov	r9, #0
.L4503:
	ldr	r3, [sp, #80]
	cmp	r9, r3
	bcs	.L4556
	ldr	r3, [sp, #64]
	ldrb	r2, [r4, #590]	@ zero_extendqisi2
	add	r7, r9, r3
	ubfx	r7, r7, #2, #16
	cmp	r2, #0
	add	r0, r7, #1
	mov	r1, r0, asl #1
	add	r3, r4, r1
	ldrh	r3, [r3, #4]
	beq	.L4505
	ldrb	ip, [r4, #1]	@ zero_extendqisi2
	cmp	ip, #0
	moveq	r3, r0
	movne	r3, r1
.L4505:
	ldrb	r1, [r4, #516]	@ zero_extendqisi2
	ldr	r0, .L4557+4
	cmp	r1, #9
	movw	r1, #61424
	str	r1, [sp, #96]
	mov	r1, r7, asl #1
	add	r0, r0, r1
	subne	r3, r3, #1
	ldrh	r0, [r0, #4]
	movne	r3, r3, asl #2
	cmp	r2, #0
	str	r3, [sp, #92]
	moveq	r7, r0
	beq	.L4509
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r7, r1
.L4509:
	ldr	r2, [sp, #40]
	cmp	r5, #0
	mul	r0, r2, r7
	bne	.L4510
	ldr	r2, [sp, #48]
	str	r8, [sp]
	add	ip, r0, r2
	ldr	r2, [r8]
	ldr	r0, .L4557+40
	mov	r1, ip
	str	ip, [sp, #84]
	stmib	sp, {r2, r3}
	movw	r3, #61424
	mov	r2, r9
	str	r3, [sp, #12]
	mov	r3, r6
	bl	printk
	mov	r1, r8
	ldr	ip, [sp, #84]
	add	r2, sp, #92
	mov	r0, ip
	bl	fw_flash_page_prog.constprop.29
	ldrb	r3, [r4, #590]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4511
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #48]
	bl	__aeabi_uidiv
	add	r2, r7, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, r5
	bl	id_block_prog_msb_ff_data
.L4511:
	ldr	r3, [sp, #32]
	add	r8, r8, #2048
	add	r3, r3, #16
	cmp	r9, r3
	bcc	.L4512
	ldr	r3, [sp, #32]
	add	r3, r3, #20
	cmp	r9, r3
	addcc	r8, r10, #2048
	b	.L4512
.L4510:
	ldr	r3, [sp, #48]
	mov	r1, r5
	add	r2, sp, #92
	add	r0, r0, r3
	bl	fw_flash_page_prog.constprop.29
.L4512:
	add	r9, r9, #4
	mov	r5, #0
	uxth	r9, r9
	b	.L4503
.L4556:
	ldr	r3, .L4557+4
	mov	r0, fp
	ldrb	r2, [sp, #76]	@ zero_extendqisi2
	ldr	r1, [sp, #32]
	strb	r2, [r3, #656]
	ldr	r2, [sp, #24]
	bl	id_block_read_data.constprop.30
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4514
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #590]
.L4514:
	ldr	r3, [sp, #32]
	mov	r5, #0
	mov	r8, r3, asl #7
.L4517:
	ldr	r3, [sp, #24]
	ldr	r2, [r3, r5, asl #2]
	ldr	r3, [r10, r5, asl #2]
	cmp	r2, r3
	beq	.L4515
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	bl	ftl_memset
	mov	r0, #0
	mov	r1, fp
	bl	flash_erase_block
	cmp	r5, r8
	bcc	.L4497
	b	.L4516
.L4515:
	add	r5, r5, #1
	cmp	r5, r8
	bne	.L4517
.L4516:
	ldr	r3, [sp, #36]
	add	r3, r3, #1
	str	r3, [sp, #36]
	cmp	r3, #5
	bhi	.L4518
.L4497:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
	ldr	r3, [sp, #44]
	add	r3, r3, #4
	str	r3, [sp, #44]
	ldr	r3, [sp, #28]
	cmp	r3, #4
	bne	.L4519
.L4518:
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4557+4
	mov	r0, #0
	ldrb	r2, [sp, #60]	@ zero_extendqisi2
	strb	r2, [r3]
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldrneb	r2, [sp, #68]	@ zero_extendqisi2
	strneb	r2, [r3, #590]
	mov	r2, #2
	strb	r2, [r3, #680]
	bl	zftl_flash_enter_slc_mode
	ldr	r0, [sp, #24]
	bl	ftl_free
	ldr	r3, [sp, #36]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L4485
.L4522:
	mvn	r0, #0
.L4485:
	ldr	r3, .L4557
	ldr	r2, [sp, #156]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L4521
	bl	__stack_chk_fail
.L4521:
	add	sp, sp, #164
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4558:
	.align	2
.L4557:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	-52655045
	.word	.LC311
	.word	.LC312
	.word	.LC313
	.word	.LANCHOR3
	.word	1179535694
	.word	.LC314
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L4563
	add	r1, r0, r1
	mov	r2, #0
.L4560:
	cmp	r0, r1
	beq	.L4562
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, ip, r3, asl #2
	ldr	r3, [r3, #-3920]
	eor	r2, r3, r2, asl #8
	b	.L4560
.L4562:
	mov	r0, r2
	bx	lr
.L4564:
	.align	2
.L4563:
	.word	.LANCHOR6
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	cmp	r0, #64
	ldr	r6, .L4589
	.pad #56
	sub	sp, sp, #56
	mov	r4, r0
	mov	r7, r1
	mov	r9, r2
	ldr	r5, .L4589+4
	ldr	r3, [r6]
	str	r3, [sp, #52]
	bne	.L4566
	ldr	r2, [r2]
	ldr	r3, .L4589+8
	cmp	r2, r3
	bne	.L4566
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r5, #-2464]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r5, #-2460]
	bl	ftl_memset
	str	r4, [r5, #-2456]
.L4566:
	str	r7, [sp]
	mov	r3, r4
	ldr	r0, .L4589+12
	ldr	r1, [r5, #-2460]
	ldr	r2, [r9]
	bl	printk
	ldrb	r3, [r5, #-2464]	@ zero_extendqisi2
	ldr	r10, .L4589+4
	cmp	r3, #0
	beq	.L4565
	ldr	r8, [r10, #-2460]
	cmp	r8, #0
	beq	.L4565
	sub	r0, r4, #64
	cmp	r0, #500
	bcs	.L4568
	rsb	r2, r4, #564
	add	r0, r8, r0, asl #9
	cmp	r2, r7
	mov	r1, r9
	movcs	r2, r7
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4569
.L4568:
	cmp	r4, #564
	bcs	.L4577
.L4569:
	ldr	r3, [r5, #-2456]
	ldr	r9, .L4589+4
	cmp	r3, r4
	beq	.L4575
	mov	r0, r8
	mov	r10, #0
	strb	r10, [r9, #-2464]
	bl	ftl_free
	str	r10, [r9, #-2460]
.L4575:
	add	r4, r4, r7
	str	r4, [r5, #-2456]
	b	.L4565
.L4577:
	ldr	r3, .L4589+16
	ldr	r0, [r10, #-2456]
	ldr	r3, [r3, #584]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L4579
	mov	r3, #2
	str	r3, [sp, #12]
	mov	r3, #3
	str	r3, [sp, #16]
	mov	r3, #4
	str	r3, [sp, #20]
	mov	r3, #5
	str	r3, [sp, #24]
	mov	r3, #6
	str	r3, [sp, #28]
	b	.L4571
.L4579:
	mov	r3, #0
.L4570:
	cmp	r0, #256
	add	r1, sp, #12
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L4570
.L4571:
	mov	r3, #61952
.L4574:
	ldr	r2, [r8, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #2048
	movne	r0, r3, asl #2
	bne	.L4573
.L4572:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L4574
	mov	r0, r0, asl #9
.L4573:
	mov	r1, r8
	add	r2, sp, #12
	bl	write_idblock
	ldr	r0, [r5, #-2460]
	mov	r8, #0
	strb	r8, [r5, #-2464]
	bl	ftl_free
	str	r8, [r5, #-2460]
	b	.L4575
.L4565:
	ldr	r2, [sp, #52]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L4578
	bl	__stack_chk_fail
.L4578:
	add	sp, sp, #56
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4590:
	.align	2
.L4589:
	.word	__stack_chk_guard
	.word	.LANCHOR5
	.word	-52655045
	.word	.LC315
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
	beq	.L4592
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L4592:
	ldr	r3, .L4597
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	ldr	r3, [r3, #-3280]
	ldr	ip, [r3, #24]
	mov	r3, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	bx	ip
.L4598:
	.align	2
.L4597:
	.word	.LANCHOR3
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #528
	sub	sp, sp, #528
	ldr	r5, .L4706
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r5]
	str	r3, [sp, #524]
	ldr	r3, .L4706+4
	cmp	r1, r3
	beq	.L4601
	bhi	.L4602
	sub	r3, r3, #3456
	sub	r3, r3, #5
	cmp	r1, r3
	beq	.L4603
	bhi	.L4604
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L4605
	bhi	.L4606
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L4663
	bl	rknand_dev_flush
	b	.L4694
.L4606:
	ldr	r3, .L4706+8
	cmp	r1, r3
	beq	.L4608
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4609
	b	.L4663
.L4604:
	ldr	r3, .L4706+12
	cmp	r1, r3
	beq	.L4610
	bhi	.L4611
	sub	r3, r3, #1952
	sub	r3, r3, #9
	cmp	r1, r3
	beq	.L4612
	b	.L4663
.L4611:
	ldr	r3, .L4706+16
	cmp	r1, r3
	beq	.L4610
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L4610
	b	.L4663
.L4602:
	ldr	r3, .L4706+20
	cmp	r1, r3
	beq	.L4613
	bhi	.L4614
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L4615
	bcc	.L4616
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L4617
	add	r3, r3, #56
	cmp	r1, r3
	beq	.L4618
	b	.L4663
.L4614:
	ldr	r3, .L4706+24
	cmp	r1, r3
	mov	r7, r3
	beq	.L4619
	bhi	.L4620
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L4621
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L4622
	b	.L4663
.L4620:
	ldr	r3, .L4706+28
	cmp	r1, r3
	beq	.L4622
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4619
	b	.L4663
.L4616:
	ldr	r0, .L4706+32
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	bne	.L4623
.L4628:
	mvn	r0, #11
	b	.L4600
.L4623:
	mov	r1, r6
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4700
	ldr	r0, .L4706+36
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp]
	bhi	.L4698
	bl	rknand_device_lock
	mov	r2, r4
	ldr	r0, [r4]
	ldr	r1, [sp]
	bl	id_block_read_data.constprop.30
	bl	rknand_device_unlock
	mov	r0, r6
	mov	r1, r4
	ldr	r2, [sp]
	mov	r2, r2, asl #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4693
	ldr	r0, .L4706+40
.L4696:
	bl	printk
.L4698:
	mov	r0, r4
.L4699:
	bl	ftl_free
.L4697:
	mvn	r0, #13
	b	.L4600
.L4615:
	ldr	r0, .L4706+44
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	beq	.L4628
	mov	r1, r6
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4629
.L4700:
	ldr	r0, .L4706+48
	b	.L4696
.L4629:
	ldr	r6, .L4706+52
	ldr	r0, .L4706+56
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r7, [r6, #-2452]
	mov	r8, r6
	cmp	r7, #0
	bne	.L4630
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r6, #-2452]
	beq	.L4698
	mov	r1, r7
	mov	r2, #260096
	bl	memset
.L4630:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L4698
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L4698
	ldr	r0, [r8, #-2452]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L4693:
	mov	r0, r4
	bl	ftl_free
.L4694:
	mov	r4, #0
	b	.L4627
.L4618:
	ldr	r0, .L4706+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L4628
	mov	r1, r6
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4633
	ldr	r0, .L4706+48
	bl	printk
	b	.L4634
.L4633:
	ldmia	r7, {r1, r2}
	ldr	r0, .L4706+64
	bl	printk
	ldr	r1, [r7]
	cmp	r1, #256000
	bhi	.L4634
	ldr	r6, .L4706+52
	ldr	r0, [r6, #-2452]
	cmp	r0, #0
	beq	.L4634
	bl	CRC_32
	ldr	r3, [r7, #4]
	cmp	r3, r0
	beq	.L4636
	mov	r0, r7
	bl	ftl_free
	b	.L4664
.L4636:
	bl	rknand_device_lock
	ldr	r1, [r6, #-2452]
	add	r2, r7, #8
	ldr	r0, [r7]
	bl	write_idblock
	mov	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r6, #-2452]
	bl	ftl_free
	str	r4, [r6, #-2452]
	mov	r0, r7
	b	.L4640
.L4634:
	mov	r0, r7
	b	.L4699
.L4617:
	ldr	r0, .L4706+68
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L4628
	bl	ReadFlashInfo
	mov	r0, r6
	mov	r1, r7
	mov	r2, #11
	b	.L4704
.L4601:
	ldr	r0, .L4706+72
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L4628
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	mov	r0, r6
	mov	r1, r7
	mov	r2, #64
	b	.L4704
.L4613:
	ldr	r0, .L4706+76
	bl	printk
	ldr	r3, .L4706+52
	mov	r0, r6
	mov	r2, #4
	ldr	r3, [r3, #-2448]
	ldr	r3, [r3, #20]
	add	r1, sp, #528
	str	r3, [r1, #-528]!
.L4688:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4697
	b	.L4694
.L4621:
	ldr	r0, .L4706+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L4628
	bl	rknand_device_lock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r7
	mov	r0, #16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	r0, r6
	mov	r1, r7
	mov	r2, #1024
.L4704:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r7
	bne	.L4699
.L4640:
	bl	ftl_free
	b	.L4627
.L4605:
	ldr	r0, .L4706+84
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	subs	r4, r0, #0
	beq	.L4641
.L4646:
	ldr	r0, .L4706+48
	bl	printk
	b	.L4697
.L4641:
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+88
	cmp	r2, r3
	beq	.L4642
.L4643:
	mvn	r4, #0
	b	.L4627
.L4642:
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L4643
	ldr	r7, .L4706+52
	mov	r2, #512
	add	r0, sp, #4
	ldr	r1, [r7, #-2448]
	bl	memcpy
	ldr	r2, [r7, #-2444]
	ldr	r3, .L4706+92
	cmp	r2, r3
	beq	.L4644
	add	r0, sp, #68
	mov	r1, r4
	mov	r2, #128
	str	r4, [sp, #12]
	str	r4, [sp, #16]
	bl	memset
.L4644:
	mov	r1, #0
	mov	r2, #256
	add	r0, sp, #260
	str	r1, [sp, #20]
	bl	memset
	b	.L4702
.L4608:
	ldr	r0, .L4706+96
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+88
	cmp	r2, r3
	bne	.L4643
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L4643
	ldr	r2, .L4706+52
	ldr	r3, .L4706+92
	ldr	r1, [r2, #-2444]
	cmp	r1, r3
	bne	.L4664
	ldr	r3, [sp, #16]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L4600
	ldr	r4, [r2, #-2448]
	add	r1, sp, #68
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #16]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4695
.L4612:
	ldr	r0, .L4706+100
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+104
	cmp	r2, r3
	bne	.L4643
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L4643
	ldr	r6, .L4706+52
	ldr	r3, [r6, #-2440]
	cmp	r3, #0
	bne	.L4647
.L4650:
	mov	r0, #0
	b	.L4600
.L4647:
	ldr	r3, [r6, #-2436]
	ldr	r2, .L4706+108
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4648
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r6, #-2436]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4648:
	ldr	r1, [r6, #-2436]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #-2448]
	ldr	r2, .L4706+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #-2448]
	ldrne	r3, .L4706+52
	movne	r2, #504
	add	r0, r7, #64
	ldrne	r3, [r3, #-2448]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r7, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	str	r4, [r6, #-2440]
	str	r4, [r6, #-2444]
	b	.L4627
.L4603:
	ldr	r0, .L4706+112
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+116
	cmp	r2, r3
	bne	.L4643
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L4643
	ldr	r6, .L4706+52
	ldr	r3, [r6, #-2440]
	cmp	r3, #1
	beq	.L4650
	ldr	r3, [r6, #-2436]
	mov	r4, #0
	ldr	r2, .L4706+108
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r1, [r6, #-2436]
	ldrne	r3, [r6, #-2436]
	movne	r2, #504
	strne	r0, [r3, #8]
	strne	r2, [r3, #4]
	strne	r0, [r3, #12]
	mov	r3, #1
	mov	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #-2448]
	ldr	r2, .L4706+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #-2448]
	ldrne	r3, .L4706+52
	movne	r1, #504
	movne	r2, #0
	add	r0, r7, #64
	ldrne	r3, [r3, #-2448]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r7, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r6, #-2440]
	b	.L4627
.L4609:
	ldr	r0, .L4706+120
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+124
	cmp	r2, r3
	bne	.L4643
	ldr	r2, [sp, #8]
	cmp	r2, #512
	addls	r0, sp, #12
	ldrls	r1, .L4706+128
	bls	.L4701
	b	.L4643
.L4610:
	ldr	r8, .L4706+16
	cmp	r4, r8
	ldreq	r0, .L4706+132
	beq	.L4691
	ldr	r3, .L4706+136
	cmp	r4, r3
	ldreq	r0, .L4706+140
	ldrne	r0, .L4706+144
.L4691:
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+148
	cmp	r2, r3
	bne	.L4697
	ldr	r3, .L4706+136
	ldr	r7, .L4706+52
	cmp	r4, r3
	bne	.L4656
	ldr	r3, [r7, #-2448]
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #8]
	strb	r3, [sp, #12]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4600
	b	.L4697
.L4656:
	ldr	r3, [r7, #-1920]
	cmp	r3, #10
	bhi	.L4697
	ldr	r1, [r7, #-2448]
	ldr	r2, [sp, #8]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4657
	ldr	r0, .L4706+152
	mov	r1, r2
	bl	printk
	ldr	r3, [r7, #-1920]
	add	r3, r3, #1
	str	r3, [r7, #-1920]
	b	.L4697
.L4657:
	cmp	r4, r8
	str	r3, [r7, #-1920]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L4694
	mvn	r4, #1
	b	.L4627
.L4622:
	ldr	r0, .L4706+156
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+160
	cmp	r2, r3
	bne	.L4643
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L4643
	ldr	r3, .L4706+164
	add	r0, sp, #12
	cmp	r4, r3
	ldr	r3, .L4706+52
	ldreq	r1, [r3, #-1916]
	ldrne	r1, [r3, #-1912]
	add	r1, r1, #8
.L4701:
	bl	memcpy
.L4702:
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #520
	b	.L4688
.L4619:
	ldr	r0, .L4706+168
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4646
	ldr	r2, [sp, #4]
	ldr	r3, .L4706+160
	cmp	r2, r3
	bne	.L4643
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L4643
	cmp	r4, r7
	add	r2, r2, #8
	ldr	r4, .L4706+52
	bne	.L4661
	add	r1, sp, #4
	ldr	r0, [r4, #-1916]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #-1916]
	b	.L4695
.L4661:
	add	r1, sp, #4
	ldr	r0, [r4, #-1912]
	bl	memcpy
	ldr	r1, [r4, #-1912]
	mov	r0, #3
.L4695:
	bl	StorageSysDataStore
	mov	r4, r0
.L4627:
	ldr	r0, .L4706+172
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L4600
.L4663:
	mvn	r0, #21
	b	.L4600
.L4664:
	mvn	r0, #1
.L4600:
	ldr	r2, [sp, #524]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L4662
	bl	__stack_chk_fail
.L4662:
	add	sp, sp, #528
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L4707:
	.align	2
.L4706:
	.word	__stack_chk_guard
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC316
	.word	.LC318
	.word	.LC319
	.word	.LC320
	.word	.LC317
	.word	.LANCHOR5
	.word	.LC321
	.word	.LC322
	.word	.LC323
	.word	.LC324
	.word	.LC325
	.word	.LC326
	.word	.LC327
	.word	.LC328
	.word	1263358532
	.word	-1067903959
	.word	.LC329
	.word	.LC330
	.word	1112753220
	.word	1146313043
	.word	.LC331
	.word	1112755781
	.word	.LC332
	.word	1094995539
	.word	.LANCHOR5-2432
	.word	.LC333
	.word	1074031676
	.word	.LC334
	.word	.LC335
	.word	1280262987
	.word	.LC336
	.word	.LC337
	.word	1145980246
	.word	1074034192
	.word	.LC338
	.word	.LC339
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
	ldr	r4, .L4717
	mov	r2, #512
	mov	r5, #0
	sub	r0, r4, #2432
	ldr	r1, [r4, #-2484]
	str	r3, [r4, #-2456]
	add	r3, r1, #512
	str	r3, [r4, #-2448]
	str	r1, [r4, #-2436]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #-1916]
	strb	r5, [r4, #-2464]
	str	r5, [r4, #-2460]
	str	r5, [r4, #-2452]
	str	r1, [r4, #-1912]
	bl	ftl_memcpy
	ldr	r0, [r4, #-2436]
	str	r5, [r4, #-2444]
	str	r5, [r4, #-1920]
	ldr	r6, [r0, #508]
	ldr	r3, [r0, #16]
	cmp	r6, r5
	str	r3, [r4, #-2440]
	beq	.L4710
	mov	r1, #508
	bl	js_hash
	cmp	r6, r0
	beq	.L4710
	ldr	r3, [r4, #-2436]
	ldr	r0, .L4717+4
	str	r5, [r4, #-2440]
	str	r5, [r3, #16]
	bl	printk
.L4710:
	ldr	r3, [r4, #-2440]
	mov	r0, #2
	ldr	r1, [r4, #-1916]
	cmp	r3, #0
	ldrne	r2, .L4717+8
	ldrne	r3, .L4717
	strne	r2, [r3, #-2444]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #-1912]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r4, r5, r6, lr}
	b	rknand_sys_storage_init
.L4718:
	.align	2
.L4717:
	.word	.LANCHOR5
	.word	.LC340
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
	ldr	r6, .L4731
	cmp	r0, #0
	str	r0, [r6, #-1908]
	beq	.L4726
	ldr	r9, .L4731+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4724:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #-1908]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4722
	ldr	r3, [r10, #-1908]
	ldr	r0, .L4731+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #-1908]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4723
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
.L4723:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4724
.L4730:
	cmp	r4, #0
	beq	.L4725
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4722
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4725:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4731+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4731+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4722:
	ldr	r0, [r6, #-1908]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #-1908]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4726:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4732:
	.align	2
.L4731:
	.word	.LANCHOR5
	.word	1380668996
	.word	.LC341
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
	ldr	r3, .L4740
	ldr	ip, [r3, #-1908]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4738
.L4735:
	cmp	r3, r4
	bcs	.L4738
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4736
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
.L4736:
	add	r3, r3, #1
	b	.L4735
.L4738:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4741:
	.align	2
.L4740:
	.word	.LANCHOR5
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
	ldr	r2, .L4765
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #-1908]
	cmp	r4, #0
	beq	.L4757
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4744:
	cmp	r6, r2
	bcs	.L4763
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4745
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4746
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L4757
.L4747:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L4764
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
	b	.L4747
.L4764:
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
	b	.L4762
.L4746:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4762
.L4745:
	add	r6, r6, #1
	b	.L4744
.L4763:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4757
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
.L4762:
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
	b	.L4743
.L4757:
	mvn	r0, #0
.L4743:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4766:
	.align	2
.L4765:
	.word	.LANCHOR5
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
	beq	.L4768
	ldr	r3, .L4784
	cmp	r6, r3
	beq	.L4770
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4771
	b	.L4782
.L4770:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4782
	ldr	r2, [r4]
	ldr	r3, .L4784+4
	cmp	r2, r3
	beq	.L4773
.L4774:
	mvn	r5, #0
	b	.L4769
.L4773:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4774
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4769
.L4771:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4782
	ldr	r2, [r4]
	ldr	r3, .L4784+4
	cmp	r2, r3
	bne	.L4774
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4774
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4782
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4769
.L4782:
	mvn	r5, #13
.L4769:
	mov	r0, r4
	bl	kfree
.L4768:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L4785:
	.align	2
.L4784:
	.word	1074034177
	.word	1448232273
	.fnend
	.size	rk_ftl_vendor_storage_ioctl, .-rk_ftl_vendor_storage_ioctl
	.global	gTable_Crc32
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
	.global	ftl_tmp_spare
	.global	ftl_info_spare
	.global	ftl_ext_info_data_buffer
	.global	ftl_info_data_buffer
	.global	ftl_tmp_buffer
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
	.global	gc_slc_mode_tlc_vpn_th
	.global	gc_tlc_mode_tlc_vpn_th
	.global	gc_tlc_mode_slc_vpn_th
	.global	gc_state
	.global	gc_mode
	.global	p_read_ahead_ext_buf
	.global	discard_sector_count
	.global	read_ahead_lpa
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
	.global	_max_slc_super_block
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
.LANCHOR1 = . + 0
	.type	__func__.38616, %object
	.size	__func__.38616, 18
__func__.38616:
	.ascii	"_list_remove_node\000"
	.space	2
	.type	__func__.38641, %object
	.size	__func__.38641, 23
__func__.38641:
	.ascii	"_list_update_data_list\000"
	.space	1
	.type	__func__.19719, %object
	.size	__func__.19719, 22
__func__.19719:
	.ascii	"nand_flash_print_info\000"
	.space	2
	.type	__func__.19805, %object
	.size	__func__.19805, 11
__func__.19805:
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
	.type	__func__.19268, %object
	.size	__func__.19268, 28
__func__.19268:
	.ascii	"flash_wait_device_ready_raw\000"
	.type	__func__.19332, %object
	.size	__func__.19332, 22
__func__.19332:
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
	.type	__func__.19357, %object
	.size	__func__.19357, 23
__func__.19357:
	.ascii	"flash_start_plane_read\000"
	.space	1
	.type	__func__.19243, %object
	.size	__func__.19243, 26
__func__.19243:
	.ascii	"flash_erase_duplane_block\000"
	.space	2
	.type	__func__.19254, %object
	.size	__func__.19254, 21
__func__.19254:
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
	.type	__func__.19988, %object
	.size	__func__.19988, 13
__func__.19988:
	.ascii	"buf_add_tail\000"
	.space	3
	.type	__func__.20001, %object
	.size	__func__.20001, 10
__func__.20001:
	.ascii	"buf_alloc\000"
	.space	2
	.type	__func__.20015, %object
	.size	__func__.20015, 16
__func__.20015:
	.ascii	"buf_remove_free\000"
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 128
zftl_debug_proc_fops:
	.word	0
	.word	seq_lseek
	.word	seq_read
	.word	zftl_debug_proc_write
	.space	36
	.word	zftl_debug_proc_open
	.space	4
	.word	single_release
	.space	64
	.type	__func__.38180, %object
	.size	__func__.38180, 12
__func__.38180:
	.ascii	"gc_add_sblk\000"
	.type	__func__.38274, %object
	.size	__func__.38274, 19
__func__.38274:
	.ascii	"gc_write_completed\000"
	.space	1
	.type	__func__.38823, %object
	.size	__func__.38823, 18
__func__.38823:
	.ascii	"ftl_alloc_sys_blk\000"
	.space	2
	.type	__func__.38833, %object
	.size	__func__.38833, 17
__func__.38833:
	.ascii	"ftl_free_sys_blk\000"
	.space	3
	.type	__func__.38972, %object
	.size	__func__.38972, 23
__func__.38972:
	.ascii	"ftl_get_ppa_from_index\000"
	.space	1
	.type	__func__.39012, %object
	.size	__func__.39012, 22
__func__.39012:
	.ascii	"ftl_get_new_free_page\000"
	.space	2
	.type	__func__.39023, %object
	.size	__func__.39023, 22
__func__.39023:
	.ascii	"ftl_ext_alloc_new_blk\000"
	.space	2
	.type	__func__.38330, %object
	.size	__func__.38330, 16
__func__.38330:
	.ascii	"gc_free_src_blk\000"
	.type	__func__.37931, %object
	.size	__func__.37931, 14
__func__.37931:
	.ascii	"ftl_write_buf\000"
	.space	2
	.type	__func__.37976, %object
	.size	__func__.37976, 18
__func__.37976:
	.ascii	"zftl_add_read_buf\000"
	.space	2
	.type	__func__.38781, %object
	.size	__func__.38781, 15
__func__.38781:
	.ascii	"ftl_alloc_sblk\000"
	.space	1
	.type	__func__.39409, %object
	.size	__func__.39409, 17
__func__.39409:
	.ascii	"pm_alloc_new_blk\000"
	.space	3
	.type	__func__.39437, %object
	.size	__func__.39437, 21
__func__.39437:
	.ascii	"pm_select_ram_region\000"
	.space	3
	.type	__func__.19709, %object
	.size	__func__.19709, 20
__func__.19709:
	.ascii	"flash_die_info_init\000"
	.type	__func__.37866, %object
	.size	__func__.37866, 17
__func__.37866:
	.ascii	"lpa_rebuild_hash\000"
	.space	3
	.type	__func__.39369, %object
	.size	__func__.39369, 13
__func__.39369:
	.ascii	"pm_free_sblk\000"
	.space	3
	.type	__func__.20039, %object
	.size	__func__.20039, 21
__func__.20039:
	.ascii	"flash_info_data_init\000"
	.space	3
	.type	__func__.19185, %object
	.size	__func__.19185, 19
__func__.19185:
	.ascii	"flash_read_page_en\000"
	.space	1
	.type	__func__.19345, %object
	.size	__func__.19345, 31
__func__.19345:
	.ascii	"flash_complete_plane_page_read\000"
	.space	1
	.type	__func__.19370, %object
	.size	__func__.19370, 25
__func__.19370:
	.ascii	"flash_complete_page_read\000"
	.space	3
	.type	__func__.39208, %object
	.size	__func__.39208, 31
__func__.39208:
	.ascii	"queue_wait_first_req_completed\000"
	.space	1
	.type	__func__.39289, %object
	.size	__func__.39289, 15
__func__.39289:
	.ascii	"sblk_read_page\000"
	.space	1
	.type	__func__.19321, %object
	.size	__func__.19321, 22
__func__.19321:
	.ascii	"flash_start_page_prog\000"
	.space	2
	.type	__func__.39262, %object
	.size	__func__.39262, 15
__func__.39262:
	.ascii	"sblk_prog_page\000"
	.space	1
	.type	__func__.19292, %object
	.size	__func__.19292, 26
__func__.19292:
	.ascii	"flash_start_tlc_page_prog\000"
	.space	2
	.type	__func__.19303, %object
	.size	__func__.19303, 29
__func__.19303:
	.ascii	"flash_start_3d_mlc_page_prog\000"
	.space	3
	.type	__func__.19227, %object
	.size	__func__.19227, 19
__func__.19227:
	.ascii	"flash_prog_page_en\000"
	.space	1
	.type	__func__.39086, %object
	.size	__func__.39086, 14
__func__.39086:
	.ascii	"ftl_prog_page\000"
	.space	2
	.type	__func__.38749, %object
	.size	__func__.38749, 15
__func__.38749:
	.ascii	"ftl_info_flush\000"
	.space	1
	.type	__func__.39048, %object
	.size	__func__.39048, 19
__func__.39048:
	.ascii	"ftl_ext_info_flush\000"
	.space	1
	.type	__func__.39060, %object
	.size	__func__.39060, 18
__func__.39060:
	.ascii	"ftl_ext_info_init\000"
	.space	2
	.type	__func__.38939, %object
	.size	__func__.38939, 23
__func__.38939:
	.ascii	"ftl_open_sblk_recovery\000"
	.space	1
	.type	__func__.39419, %object
	.size	__func__.39419, 14
__func__.39419:
	.ascii	"pm_write_page\000"
	.space	2
	.type	__func__.20061, %object
	.size	__func__.20061, 17
__func__.20061:
	.ascii	"flash_info_flush\000"
	.space	3
	.type	__func__.20028, %object
	.size	__func__.20028, 20
__func__.20028:
	.ascii	"flash_info_blk_init\000"
	.type	__func__.19764, %object
	.size	__func__.19764, 16
__func__.19764:
	.ascii	"nand_flash_init\000"
	.type	__func__.38915, %object
	.size	__func__.38915, 16
__func__.38915:
	.ascii	"ftl_sysblk_dump\000"
	.type	__func__.39353, %object
	.size	__func__.39353, 16
__func__.39353:
	.ascii	"load_l2p_region\000"
	.type	__func__.39393, %object
	.size	__func__.39393, 6
__func__.39393:
	.ascii	"pm_gc\000"
	.space	2
	.type	__func__.38261, %object
	.size	__func__.38261, 22
__func__.38261:
	.ascii	"gc_update_l2p_map_new\000"
	.space	2
	.type	__func__.38374, %object
	.size	__func__.38374, 16
__func__.38374:
	.ascii	"gc_scan_src_blk\000"
	.type	__func__.38432, %object
	.size	__func__.38432, 20
__func__.38432:
	.ascii	"gc_scan_static_data\000"
	.type	__func__.38495, %object
	.size	__func__.38495, 18
__func__.38495:
	.ascii	"gc_block_vpn_scan\000"
	.space	2
	.type	__func__.38891, %object
	.size	__func__.38891, 14
__func__.38891:
	.ascii	"ftl_sblk_dump\000"
	.space	2
	.type	__func__.38009, %object
	.size	__func__.38009, 10
__func__.38009:
	.ascii	"zftl_read\000"
	.space	2
	.type	__func__.38227, %object
	.size	__func__.38227, 21
__func__.38227:
	.ascii	"gc_check_data_one_wl\000"
	.space	3
	.type	__func__.38993, %object
	.size	__func__.38993, 19
__func__.38993:
	.ascii	"ftl_update_l2p_map\000"
	.space	1
	.type	__func__.37913, %object
	.size	__func__.37913, 17
__func__.37913:
	.ascii	"ftl_write_commit\000"
	.space	3
	.type	__func__.38303, %object
	.size	__func__.38303, 16
__func__.38303:
	.ascii	"gc_do_copy_back\000"
	.type	__func__.38523, %object
	.size	__func__.38523, 11
__func__.38523:
	.ascii	"zftl_do_gc\000"
	.space	1
	.type	__func__.38040, %object
	.size	__func__.38040, 13
__func__.38040:
	.ascii	"_ftl_discard\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC1:
	.ascii	"%s\012\000"
.LC2:
	.ascii	"FTL version: 6.0.18 20190918\000"
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
	.ascii	"g_flash_slc_mode: %x\012\000"
.LC18:
	.ascii	"OptMode: %x\012\000"
.LC19:
	.ascii	"g_nand_max_die: %x\012\000"
.LC20:
	.ascii	"Cache read enable: %x\012\000"
.LC21:
	.ascii	"Cache random read enable: %x\012\000"
.LC22:
	.ascii	"Cache prog enable: %x\012\000"
.LC23:
	.ascii	"multi read enable: %x\012\000"
.LC24:
	.ascii	"multi prog enable: %x\012\000"
.LC25:
	.ascii	"interleave enable: %x\012\000"
.LC26:
	.ascii	"read retry enable: %x\012\000"
.LC27:
	.ascii	"randomizer enable: %x\012\000"
.LC28:
	.ascii	"SDR enable: %x\012\000"
.LC29:
	.ascii	"ONFI enable: %x\012\000"
.LC30:
	.ascii	"TOGGLE enable: %x\012\000"
.LC31:
	.ascii	"g_flash_slc_mode: %x %x\012\000"
.LC32:
	.ascii	"MultiPlaneProgCmd: %x %x\012\000"
.LC33:
	.ascii	"MultiPlaneReadCmd: %x %x\012\000"
.LC34:
	.ascii	"g_flash_toggle_mode_en: %x\012\000"
.LC35:
	.ascii	"...%s enter... %p\012\000"
.LC36:
	.ascii	"0:%x %x %x %x %x\012\000"
.LC37:
	.ascii	"g_nandc_ver...%d\012\000"
.LC38:
	.ascii	"nand sdr mode %x\012\000"
.LC39:
	.ascii	"nand ddr mode %x\012\000"
.LC40:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC41:
	.ascii	"otp:%x %x %x %x\012\000"
.LC42:
	.ascii	"bad block test:%x %x\012\000"
.LC43:
	.ascii	"flash_erase_duplane_block %x %x %x\012\000"
.LC44:
	.ascii	"flash_erase_duplane_block pageadd = %x status = %x\012"
	.ascii	"\000"
.LC45:
	.ascii	"flash_erase_block %x %x %x\012\000"
.LC46:
	.ascii	"flash_erase_block %d block = %x status = %x\012\000"
.LC47:
	.ascii	"erase done: %x\012\000"
.LC48:
	.ascii	"flash_mask_bad_block %d %d\012\000"
.LC49:
	.ascii	"zftl_debug\000"
.LC50:
	.ascii	"FLASH ID: %x %x %x %x %x %x\012\000"
.LC51:
	.ascii	"density: %d MB\012\000"
.LC52:
	.ascii	"device density: %d MB\012\000"
.LC53:
	.ascii	"FTL INFO:\012\000"
.LC54:
	.ascii	"max_lpn = 0x%x\012\000"
.LC55:
	.ascii	"density = 0x%x\012\000"
.LC56:
	.ascii	"slc vpn = 0x%x\012\000"
.LC57:
	.ascii	"xlc vpn = 0x%x\012\000"
.LC58:
	.ascii	"free slc blk = 0x%x\012\000"
.LC59:
	.ascii	"free xlc blk = 0x%x\012\000"
.LC60:
	.ascii	"free mix blk = 0x%x\012\000"
.LC61:
	.ascii	"slc data blk = 0x%x\012\000"
.LC62:
	.ascii	"slc cache blk = 0x%x\012\000"
.LC63:
	.ascii	"xlc data blk = 0x%x\012\000"
.LC64:
	.ascii	"free buf = %d, %d, %d\012\000"
.LC65:
	.ascii	"bad blk = %d %d\012\000"
.LC66:
	.ascii	"TBW = %d MB\012\000"
.LC67:
	.ascii	"TBR = %d MB\012\000"
.LC68:
	.ascii	"POC = %d\012\000"
.LC69:
	.ascii	"PLC = %d\012\000"
.LC70:
	.ascii	"sys run time = %d S\012\000"
.LC71:
	.ascii	"slc mode = %d\012\000"
.LC72:
	.ascii	"prog err = %d\012\000"
.LC73:
	.ascii	"read err = %d\012\000"
.LC74:
	.ascii	"GC XLC page = %d\012\000"
.LC75:
	.ascii	"GC SLC page = %d\012\000"
.LC76:
	.ascii	"discard page = 0x%x\012\000"
.LC77:
	.ascii	"version = %d\012\000"
.LC78:
	.ascii	"acblk = 0x%x %d %d\012\000"
.LC79:
	.ascii	"tmblk = 0x%x %d %d\012\000"
.LC80:
	.ascii	"gcblk = 0x%x %d %d\012\000"
.LC81:
	.ascii	"slc ec = %d, %d, %d, %d, %d\012\000"
.LC82:
	.ascii	"xlc ec = %d, %d, %d, %d, %d\012\000"
.LC83:
	.ascii	"gc free blk th = %d\012\000"
.LC84:
	.ascii	"gc vpn th = %d %d %d %d %d\012\000"
.LC85:
	.ascii	"swl blk = %x %x %x %x\012\000"
.LC86:
	.ascii	"rf info = %x %x %x %x %x\012\000"
.LC87:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\012\000"
.LC88:
	.ascii	"gc_add_sblk = %d, %d, %d\012\000"
.LC89:
	.ascii	"gc_add_sblk = %d, %d, %d,last update:%d, %d\012\000"
.LC90:
	.ascii	"gc_add_sblk = %d, %d, %d, %d, %d, %d\012\000"
.LC91:
	.ascii	"gc_mark_bad_ppa %d %x %x\012\000"
.LC92:
	.ascii	"status: %x, ppa: %x\012\000"
.LC93:
	.ascii	"gc_write_completed: %x  %x  %d %x\012\000"
.LC94:
	.ascii	"zftl_get_gc_node cache = %x index = %d vpn = %x\012"
	.ascii	"\000"
.LC95:
	.ascii	"gc_search_src_blk mode = %x, src mode = %x, count= "
	.ascii	"%d %d\012\000"
.LC96:
	.ascii	"%d gc_free_temp_buf buf id= %x\012\000"
.LC97:
	.ascii	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\012"
	.ascii	"\000"
.LC98:
	.ascii	"swl_tlc_free_mini_ec_blk alloc sblk %x\012\000"
.LC99:
	.ascii	"zftl_get_free_sblk %x %d, %p %d %d\012\000"
.LC100:
	.ascii	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\012\000"
.LC101:
	.ascii	"swl_slc_free_mini_ec_blk alloc sblk %x\012\000"
.LC102:
	.ascii	"list count:%p %d\012\000"
.LC103:
	.ascii	"%d: node:%x %x %x %x, %d %d %d %d %d\012\000"
.LC104:
	.ascii	"ftl_vpn_decrement %x = %d, %d\012\000"
.LC105:
	.ascii	"mask bad block:cs %x %x block: %x %x\012\000"
.LC106:
	.ascii	"gc_free_bad_sblk 0x%x\012\000"
.LC107:
	.ascii	"swl_slc_free_mini_ec_blk sblk %x\012\000"
.LC108:
	.ascii	"gc_free_src_blk = %x, vpn = %d\012\000"
.LC109:
	.ascii	"gc_free_src_blk %x, %d\012\000"
.LC110:
	.ascii	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk"
	.ascii	":s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\012\000"
.LC111:
	.ascii	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\012\000"
.LC112:
	.ascii	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\012\000"
.LC113:
	.ascii	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\012\000"
.LC114:
	.ascii	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:"
	.ascii	"%d free_th: %d\012\000"
.LC115:
	.ascii	"swl : %x %x %x %x %x %x\012\000"
.LC116:
	.ascii	"ftl prog error =%x, lpa = %x, ppa= %x\012\000"
.LC117:
	.ascii	"ftl re prog: lpa = %x, ppa= %x\012\000"
.LC118:
	.ascii	"alloc sblk %x %d\012\000"
.LC119:
	.ascii	"dump_sblk_queue: %d\012\000"
.LC120:
	.ascii	"buf id= %d state = %d ppa = %x\012\000"
.LC121:
	.ascii	"blk %x is bad block\012\000"
.LC122:
	.ascii	"pm_alloc_new_blk: %x %x %x %x\012\000"
.LC123:
	.ascii	"%s %d %d\012\000"
.LC124:
	.ascii	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d"
	.ascii	" ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC125:
	.ascii	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC126:
	.ascii	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC127:
	.ascii	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC128:
	.ascii	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec "
	.ascii	"= %d ,mode=%d, func=%x, bbt=%x vpn = %d\012\000"
.LC129:
	.ascii	"gc_static_wearleveling: slc blk: %x, tlc blk: %d av"
	.ascii	"g slc ec: %d, avg tlc ec: %d \012\000"
.LC130:
	.ascii	"gc_static_wearleveling: min slc ec: %x, min tlc ec:"
	.ascii	" %d max slc ec: %d, max tlc ec: %d; %d %d\012\000"
.LC131:
	.ascii	"swl add tlc gc = %x, %d, %d, %d, %d, %d\012\000"
.LC132:
	.ascii	"swl add slc gc  = %x, %d, %d, %d, %d, %d\012\000"
.LC133:
	.ascii	"free blk vpn error: %x %x\012\000"
.LC134:
	.ascii	"GC PM block %x %x %x %d\012\000"
.LC135:
	.ascii	"ftl_free_no_use_map_blk %x %x %x %d\012\000"
.LC136:
	.ascii	"...%d @ %s\012\000"
.LC137:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC138:
	.ascii	"%s %p:0x%x:\000"
.LC139:
	.ascii	"%x \000"
.LC140:
	.ascii	"\000"
.LC141:
	.ascii	"otp error! %d\000"
.LC142:
	.ascii	"rr\000"
.LC143:
	.ascii	"flash_abort_clear = %d\012\000"
.LC144:
	.ascii	"%d mtrans_cnt = %d page_num = %d\012\000"
.LC145:
	.ascii	"nandc:\000"
.LC146:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC147:
	.ascii	"nandc_xfer_done read error %x\012\000"
.LC148:
	.ascii	"dqs data abort %x\012\000"
.LC149:
	.ascii	"dqs data timeout %x\012\000"
.LC150:
	.ascii	"xfer error %x\012\000"
.LC151:
	.ascii	"flash_read_page %x %x %x\012\000"
.LC152:
	.ascii	"MT %d row=%x,last status %d,status = %d\012\000"
.LC153:
	.ascii	"MT RR %d row=%x,count %d,status=%d\012\000"
.LC154:
	.ascii	"toshiba SRR %d row=%x, status=%d\012\000"
.LC155:
	.ascii	"toshiba TRR %d row=%x, status=%d\012\000"
.LC156:
	.ascii	"toshiba RR %d row=%x,count %d,status=%d\012\000"
.LC157:
	.ascii	"hynix RR %d row=%x, count %d, status=%d\012\000"
.LC158:
	.ascii	"%d flash_ddr_tuning_read %x ecc=%d\012\000"
.LC159:
	.ascii	"sync para %d\012\000"
.LC160:
	.ascii	"DDR mode Read error %x %x\012\000"
.LC161:
	.ascii	"SDR mode Read %x %x ecc:%x\012\000"
.LC162:
	.ascii	"flash_read_page_en %x %x %x\012\000"
.LC163:
	.ascii	"flash_read_page_en %x %x error_ecc %d %d\012\000"
.LC164:
	.ascii	"flash_get_last_written_page: %x %x %x\012\000"
.LC165:
	.ascii	"read page: %x %x %x %x\012\000"
.LC166:
	.ascii	"flash_complete_page_read %x %x error_ecc %d %d\012\000"
.LC167:
	.ascii	"read: %x %x %x %x\012\000"
.LC168:
	.ascii	"0set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC169:
	.ascii	"prog end %x %x error_ecc %d %d\012\000"
.LC170:
	.ascii	"1set buf %d,status = %x, ppa = %x lun state = %d\012"
	.ascii	"\000"
.LC171:
	.ascii	"dp prog end %x %x error_ecc %d %d\012\000"
.LC172:
	.ascii	"flash_prog_page %x %x %x\012\000"
.LC173:
	.ascii	"flash_prog_page page_addr = %x status = %x\012\000"
.LC174:
	.ascii	"sblk_prog_page ppa = %x, count = %d\012\000"
.LC175:
	.ascii	"sblk_3d_tlc_dump_prog wl_addr= %x ppa = %x ppa = %x"
	.ascii	"\012\000"
.LC176:
	.ascii	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\012"
	.ascii	"\000"
.LC177:
	.ascii	"flash_prog_page_en:%x %x %x\012\000"
.LC178:
	.ascii	"w d:\000"
.LC179:
	.ascii	"w s:\000"
.LC180:
	.ascii	"spare\000"
.LC181:
	.ascii	"data\000"
.LC182:
	.ascii	"write error: %x\012\000"
.LC183:
	.ascii	"g_ftl_info_blk blk = %x, page = %x version = %d\012"
	.ascii	"\000"
.LC184:
	.ascii	"%d %x @%d %x\012\000"
.LC185:
	.ascii	"ftl_info_blk_init %d %d %x\012\000"
.LC186:
	.ascii	"ink flag: %x\012\000"
.LC187:
	.ascii	"%s %d %d %x %x\012\000"
.LC188:
	.ascii	"%s %x %x %x\012\000"
.LC189:
	.ascii	"saved_active_page  = %x\012\000"
.LC190:
	.ascii	"saved_active_plane = %x\012\000"
.LC191:
	.ascii	"sblk = %x\012\000"
.LC192:
	.ascii	"phy_blk = %x %x\012\000"
.LC193:
	.ascii	"num_planes = %x\012\000"
.LC194:
	.ascii	"sblk = %x, vpn0 = %d, vpn1 = %d\012\000"
.LC195:
	.ascii	"dump_write_lpa = %x %x %x %x\012\000"
.LC196:
	.ascii	"dump write new ppa = %x, last ppa = %x lpa = %x\012"
	.ascii	"\000"
.LC197:
	.ascii	"dump write = %x %x %x\012\000"
.LC198:
	.ascii	"dump write hash update = %x %x %x\012\000"
.LC199:
	.ascii	"pm_write_page write error: %x\012\000"
.LC200:
	.ascii	"ftl_sblk_dump_write = %x %d %d %d %d\012\000"
.LC201:
	.ascii	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x"
	.ascii	" %x %x, spare: %x %x %x %x\012\000"
.LC202:
	.ascii	"ftl_sblk_dump_write2 = %x %d %d %d\012\000"
.LC203:
	.ascii	"ftl_sblk_dump_write = %x %x\012\000"
.LC204:
	.ascii	"ftl_sblk_dump_write done = %x\012\000"
.LC205:
	.ascii	"ftl_ink_check_sblk = %x %d %d\012\000"
.LC206:
	.ascii	"ftl_ink_check_sblk = %x %d %d end\012\000"
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
	.ascii	"free_buf_count: %d\012\000"
.LC214:
	.ascii	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\012\000"
.LC215:
	.ascii	"ftl_ext_info_blk blk:0x%x, page:0x%x\012\000"
.LC216:
	.ascii	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC217:
	.ascii	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, pag"
	.ascii	"e_index:0x%x\012\000"
.LC218:
	.ascii	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page"
	.ascii	"_index:0x%x\012\000"
.LC219:
	.ascii	"lpa:\000"
.LC220:
	.ascii	"vpn:\000"
.LC221:
	.ascii	"sblk:\000"
.LC222:
	.ascii	"lpa_hash:\000"
.LC223:
	.ascii	"lpa_hash_index:\000"
.LC224:
	.ascii	"%s w error lpn = %x, max ppa = %d\012\000"
.LC225:
	.ascii	"region_id = %d, pm_max_region = %d\012\000"
.LC226:
	.ascii	"load_l2p_region no ppa = %x , %x, all setting 0xff."
	.ascii	"...\012\000"
.LC227:
	.ascii	"load_l2p_region = %x,%x,%x, %x\012\000"
.LC228:
	.ascii	"pm_ppa:\000"
.LC229:
	.ascii	"data:\000"
.LC230:
	.ascii	"spare:\000"
.LC231:
	.ascii	"pm_init posr %x %x %x\012\000"
.LC232:
	.ascii	"pm_init recovery %x %x %x\012\000"
.LC233:
	.ascii	"pm_log2phys  lpn = %d, max lpn = %d\012\000"
.LC234:
	.ascii	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x "
	.ascii	"%x %x %x\012\000"
.LC235:
	.ascii	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\012"
	.ascii	"\000"
.LC236:
	.ascii	"gc_recovery: %x vpn = %x\012\000"
.LC237:
	.ascii	"_c_user_data_density := %d\012\000"
.LC238:
	.ascii	"_c_totle_phy_density := %d\012\000"
.LC239:
	.ascii	"_c_totle_log_page := %d\012\000"
.LC240:
	.ascii	"_c_totle_data_density := %d\012\000"
.LC241:
	.ascii	"_c_ftl_pm_page_num := %d\012\000"
.LC242:
	.ascii	"_c_ftl_byte_pre_page := %d\012\000"
.LC243:
	.ascii	"_c_max_pm_sblk := %d\012\000"
.LC244:
	.ascii	"_min_slc_super_block := %d\012\000"
.LC245:
	.ascii	"_max_xlc_super_block := %d\012\000"
.LC246:
	.ascii	"gp_ftl_ext_info %p %p %p\012\000"
.LC247:
	.ascii	"flash info size: %d %d %d\012\000"
.LC248:
	.ascii	"ftl_init %x\012\000"
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
	.ascii	"block = %x, vpn=%x check vpn = %d\012\000"
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
	.ascii	"7. echo dumplist > /proc/zftl_debug\012\000"
.LC290:
	.ascii	"8. echo lpa2ppa:x> /proc/zftl_debug\012\000"
.LC291:
	.ascii	"gc_lpa:\000"
.LC292:
	.ascii	"gc_ppa:\000"
.LC293:
	.ascii	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %"
	.ascii	"x\012\000"
.LC294:
	.ascii	"ftl_update_l2p_map: %x %x %x\012\000"
.LC295:
	.ascii	"ftl_update_l2p_map\000"
.LC296:
	.ascii	"lpa_tbl:\000"
.LC297:
	.ascii	"sblk %x vpn: %d %d\012\000"
.LC298:
	.ascii	"error gc_add_sblk: %x\012\000"
.LC299:
	.ascii	"%d read error: ppa:%x, lpa:%x, status:%x\012\000"
.LC300:
	.ascii	"gc page in buf: lpa %x ppa = %x pageindex= %x\012\000"
.LC301:
	.ascii	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x "
	.ascii	"page_index= %d\012\000"
.LC302:
	.ascii	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x %x %d\012"
	.ascii	"\000"
.LC303:
	.ascii	"%d prog_step: %x %x buf id= %x ppa = %x hash=%d id "
	.ascii	"= %x plane = %x lpa=%x %x\012\000"
.LC304:
	.ascii	"gc %d: %d %d %d %d %d %d %d\012\000"
.LC305:
	.ascii	"gc %d: %d %d %d %d %d %d\012\000"
.LC306:
	.ascii	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\012"
	.ascii	"\000"
.LC307:
	.ascii	"gc free %x, %d\012\000"
.LC308:
	.ascii	"ftlwrite %x %x %x %x\012\000"
.LC309:
	.ascii	"ftl_discard:(%x, %x, %x, %x)\012\000"
.LC310:
	.ascii	"id_block_prog_msb_ff_data slc page = %d pageadd=%x "
	.ascii	"%x\012\000"
.LC311:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC312:
	.ascii	"idblk:\000"
.LC313:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC314:
	.ascii	"prog page: %x %x %x, %p %x %x %x\012\000"
.LC315:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC316:
	.ascii	"READ_SECTOR_IO\012\000"
.LC317:
	.ascii	"rk_copy_from_user error\012\000"
.LC318:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC319:
	.ascii	"rk_copy_to_user error\012\000"
.LC320:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC321:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC322:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC323:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC324:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC325:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC326:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC327:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC328:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC329:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC330:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC331:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC332:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC333:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC334:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC335:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC336:
	.ascii	"LockKey not match %d\012\000"
.LC337:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC338:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC339:
	.ascii	"return ret = %lx\012\000"
.LC340:
	.ascii	"secureBootEn check error\012\000"
.LC341:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
	.data
	.align	2
.LANCHOR2 = . + 0
.LANCHOR6 = . + 8184
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
	.size	zftl_nand_flash_para_tbl, 1408
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
	.byte	0
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.byte	4
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
	.byte	4
	.byte	0
	.short	512
	.byte	2
	.byte	-65
	.byte	-66
	.byte	0
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
	.type	gTable_Crc32, %object
	.size	gTable_Crc32, 1024
gTable_Crc32:
	.word	0
	.word	79760823
	.word	159521646
	.word	222500569
	.word	319043292
	.word	398801771
	.word	445001138
	.word	507977733
	.word	638086584
	.word	583622671
	.word	797603542
	.word	726367073
	.word	890002276
	.word	835540691
	.word	1015955466
	.word	944721341
	.word	1276173168
	.word	1221711559
	.word	1167245342
	.word	1096011177
	.word	1595207084
	.word	1540743195
	.word	1452734146
	.word	1381497717
	.word	1780004552
	.word	1859763071
	.word	1671081382
	.word	1734058001
	.word	2031910932
	.word	2111671715
	.word	1889442682
	.word	1952421581
	.word	-1742620960
	.word	-1662993577
	.word	-1851544178
	.word	-1788698567
	.word	-1960476612
	.word	-1880846965
	.word	-2102944942
	.word	-2040097051
	.word	-1104553128
	.word	-1159145745
	.word	-1213480906
	.word	-1284846207
	.word	-1389499004
	.word	-1444093901
	.word	-1531971862
	.word	-1603339427
	.word	-734958192
	.word	-789553113
	.word	-575441154
	.word	-646808759
	.word	-952804532
	.word	-1007397125
	.word	-826851294
	.word	-898216555
	.word	-231145432
	.word	-151515745
	.word	-71623866
	.word	-8775951
	.word	-516081932
	.word	-436454589
	.word	-390124134
	.word	-327278547
	.word	880959607
	.word	809725376
	.word	1023441689
	.word	968980142
	.word	663115435
	.word	591878940
	.word	772033989
	.word	717570162
	.word	311037391
	.word	374014072
	.word	453514913
	.word	533273366
	.word	26098451
	.word	89077412
	.word	135012477
	.word	214773194
	.word	2022882055
	.word	2085861040
	.word	1896915049
	.word	1976675806
	.word	1805028827
	.word	1868005484
	.word	1645516469
	.word	1725274882
	.word	1587205823
	.word	1515969288
	.word	1461243345
	.word	1406779494
	.word	1302257763
	.word	1231023572
	.word	1142749965
	.word	1088288442
	.word	-1398548841
	.word	-1469916384
	.word	-1524511239
	.word	-1579106226
	.word	-1079517109
	.word	-1150882308
	.word	-1239024859
	.word	-1293617518
	.word	-1968456913
	.word	-1905609064
	.word	-2094423999
	.word	-2014794250
	.word	-1716548109
	.word	-1653702588
	.word	-1876060515
	.word	-1796433110
	.word	-525136409
	.word	-462290864
	.word	-382658935
	.word	-303031490
	.word	-206095557
	.word	-143247732
	.word	-97181611
	.word	-17551902
	.word	-960798625
	.word	-1032163864
	.word	-818316495
	.word	-872909178
	.word	-708880765
	.word	-780248268
	.word	-599962131
	.word	-654557094
	.word	1761919214
	.word	1841679705
	.word	1619450752
	.word	1682429495
	.word	2046883378
	.word	2126642053
	.word	1937960284
	.word	2000937195
	.word	1326230870
	.word	1271767265
	.word	1183757880
	.word	1112521615
	.word	1544067978
	.word	1489606205
	.word	1435140324
	.word	1363905875
	.word	622074782
	.word	567612969
	.word	748028144
	.word	676793671
	.word	907029826
	.word	852566261
	.word	1066546732
	.word	995310491
	.word	52196902
	.word	131955601
	.word	178154824
	.word	241131775
	.word	270024954
	.word	349785421
	.word	429546388
	.word	492525091
	.word	-249203186
	.word	-169575495
	.word	-123245216
	.word	-60399401
	.word	-501137198
	.word	-421507739
	.word	-341615684
	.word	-278768117
	.word	-684909642
	.word	-739502591
	.word	-558956328
	.word	-630321809
	.word	-1003934358
	.word	-1058529059
	.word	-844417532
	.word	-915784781
	.word	-1120555650
	.word	-1175150391
	.word	-1263028720
	.word	-1334395993
	.word	-1372480606
	.word	-1427073515
	.word	-1481408308
	.word	-1552773765
	.word	-1690451770
	.word	-1610822287
	.word	-1832920152
	.word	-1770072545
	.word	-2009467366
	.word	-1929839699
	.word	-2118390412
	.word	-2055544637
	.word	1569104025
	.word	1497869614
	.word	1409596407
	.word	1355134528
	.word	1317180997
	.word	1245944818
	.word	1191218475
	.word	1136754844
	.word	2072956193
	.word	2135933078
	.word	1913443919
	.word	1993202680
	.word	1753938941
	.word	1816917578
	.word	1627971731
	.word	1707732260
	.word	295074793
	.word	358053470
	.word	403988615
	.word	483749168
	.word	43142453
	.word	106119298
	.word	185620059
	.word	265378796
	.word	933107281
	.word	861871078
	.word	1042025791
	.word	987562120
	.word	614080653
	.word	542846266
	.word	756562915
	.word	702101076
	.word	-978905479
	.word	-1050272818
	.word	-869987049
	.word	-924581728
	.word	-693952347
	.word	-765317870
	.word	-551470133
	.word	-606062980
	.word	-475038783
	.word	-412191114
	.word	-366124881
	.word	-286495464
	.word	-257209059
	.word	-194363222
	.word	-114731405
	.word	-35103804
	.word	-1984443127
	.word	-1921597250
	.word	-2143955353
	.word	-2064327728
	.word	-1699480619
	.word	-1636632990
	.word	-1825447749
	.word	-1745818356
	.word	-1346395983
	.word	-1417761530
	.word	-1505903649
	.word	-1560496536
	.word	-1128556947
	.word	-1199924262
	.word	-1254519549
	.word	-1309114188
	.bss
	.align	6
.LANCHOR0 = . + 0
.LANCHOR3 = . + 8184
.LANCHOR4 = . + 24552
.LANCHOR5 = . + 40920
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
	.size	g_lsb_page_tbl, 512
g_lsb_page_tbl:
	.space	512
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
	.type	_c_ftl_cs_bits, %object
	.size	_c_ftl_cs_bits, 1
_c_ftl_cs_bits:
	.space	1
	.space	3
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 4
g_flash_cur_mode:
	.space	4
	.type	g_flash_micron_3d_tlc_flag, %object
	.size	g_flash_micron_3d_tlc_flag, 1
g_flash_micron_3d_tlc_flag:
	.space	1
	.space	3
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
	.size	g_gc_info, 596
g_gc_info:
	.space	596
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
	.type	sblk_gc_write_completed_queue_head, %object
	.size	sblk_gc_write_completed_queue_head, 1
sblk_gc_write_completed_queue_head:
	.space	1
	.type	_c_ftl_nand_type, %object
	.size	_c_ftl_nand_type, 1
_c_ftl_nand_type:
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
	.type	gc_slc_cache_index, %object
	.size	gc_slc_cache_index, 2
gc_slc_cache_index:
	.space	2
	.type	gp_data_slc_cache_head, %object
	.size	gp_data_slc_cache_head, 4
gp_data_slc_cache_head:
	.space	4
	.type	gc_xlc_data_index, %object
	.size	gc_xlc_data_index, 2
gc_xlc_data_index:
	.space	2
	.space	2
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
	.type	_c_ftl_nand_planes_num, %object
	.size	_c_ftl_nand_planes_num, 1
_c_ftl_nand_planes_num:
	.space	1
	.space	1
	.type	gc_slc_data_index, %object
	.size	gc_slc_data_index, 2
gc_slc_data_index:
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
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.space	2
	.type	_max_slc_super_block, %object
	.size	_max_slc_super_block, 2
_max_slc_super_block:
	.space	2
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.space	2
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.space	1
	.type	read_buf_head, %object
	.size	read_buf_head, 1
read_buf_head:
	.space	1
	.type	read_buf_count, %object
	.size	read_buf_count, 1
read_buf_count:
	.space	1
	.type	sblk_read_completed_queue_head, %object
	.size	sblk_read_completed_queue_head, 1
sblk_read_completed_queue_head:
	.space	1
	.space	2
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
	.size	g_msb_page_tbl, 1024
g_msb_page_tbl:
	.space	1024
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
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
	.space	1
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.space	1
	.space	2
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.space	4
	.space	40
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 256
ftl_info_spare:
	.space	256
	.type	g_ftl_info_blk, %object
	.size	g_ftl_info_blk, 4
g_ftl_info_blk:
	.space	4
	.space	60
	.type	ftl_info_data_buffer, %object
	.size	ftl_info_data_buffer, 16384
ftl_info_data_buffer:
	.space	16384
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
	.space	61
	.type	ftl_ext_info_data_buffer, %object
	.size	ftl_ext_info_data_buffer, 16384
ftl_ext_info_data_buffer:
	.space	16384
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
	.type	g_flash_micron_3d_tlc_b05a, %object
	.size	g_flash_micron_3d_tlc_b05a, 1
g_flash_micron_3d_tlc_b05a:
	.space	1
	.type	g_flash_micron_3d_tlc_b16a, %object
	.size	g_flash_micron_3d_tlc_b16a, 1
g_flash_micron_3d_tlc_b16a:
	.space	1
	.space	1
	.type	_c_ftl_byte_pre_page, %object
	.size	_c_ftl_byte_pre_page, 2
_c_ftl_byte_pre_page:
	.space	2
	.type	pm_last_load_ram_id, %object
	.size	pm_last_load_ram_id, 1
pm_last_load_ram_id:
	.space	1
	.space	1
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
	.type	rk_zftl_enable, %object
	.size	rk_zftl_enable, 1
rk_zftl_enable:
	.space	1
	.space	1
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 4
gLoaderBootInfo:
	.space	4
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 4
RK29_NANDC1_REG_BASE:
	.space	4
	.type	_last_read_time, %object
	.size	_last_read_time, 4
_last_read_time:
	.space	4
	.type	_last_write_time, %object
	.size	_last_write_time, 4
_last_write_time:
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
	.type	g_vendor, %object
	.size	g_vendor, 4
g_vendor:
	.space	4
	.type	ftl_low_format_cur_blk, %object
	.size	ftl_low_format_cur_blk, 2
ftl_low_format_cur_blk:
	.space	2
	.space	22
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 16384
ftl_tmp_buffer:
	.space	16384
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
