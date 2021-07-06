/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
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
	.type	ndelay, %function
ndelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L39
	add	r0, r0, #996
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L39+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	bx	r3	@ indirect register sibling call
.L40:
	.align	2
.L39:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	ndelay, .-ndelay
	.align	2
	.type	hynix_set_rr_para, %function
hynix_set_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r6, r0, asl #8
	ldr	r3, .L48
	ldr	r2, [r3, #1040]
	ldr	r8, [r3, #1044]
	ldrb	ip, [r2, #113]	@ zero_extendqisi2
	mov	r3, r2
	add	r7, r8, r6
	mul	r5, r1, ip
	ldrb	r1, [r3, #112]!	@ zero_extendqisi2
	sub	ip, ip, #1
	cmp	r1, #8
	mov	r1, #54
	str	r1, [r7, #2056]
	movne	r4, #160
	mlane	r5, r4, r0, r5
	add	r5, r5, #32
	add	r3, r3, r5
	add	r5, r2, #127
	sub	r9, r3, #1
	add	r4, r3, ip
.L44:
	cmp	r9, r4
	beq	.L47
	ldrb	r3, [r5, #1]!	@ zero_extendqisi2
	mov	r0, #120
	str	r3, [r7, #2052]
	bl	ndelay
	ldrsb	r3, [r9, #1]!
	str	r3, [r7, #2048]
	b	.L44
.L47:
	add	r6, r8, r6
	mov	r3, #22
	str	r3, [r6, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L49:
	.align	2
.L48:
	.word	.LANCHOR0
	.fnend
	.size	hynix_set_rr_para, .-hynix_set_rr_para
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
	ldr	r1, .L52
	mov	r2, r0
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	single_open
.L53:
	.align	2
.L52:
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
	ldr	r1, .L56
	ldr	r2, .L56+4
	bl	seq_printf
	ldr	r3, .L56+8
	mov	r0, r4
	ldr	r1, .L56+12
	ldr	r2, [r3]
	bl	seq_printf
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L57:
	.align	2
.L56:
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
	ldr	r3, .L62
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	ldr	r2, [r3, #1044]
	ldr	r1, [r2]
	str	r1, [r3, #1048]
	ldr	r1, [r2, #4]
	str	r1, [r3, #1052]
	bne	.L59
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
	b	.L61
.L59:
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
.L61:
	str	r2, [r3, #1076]
	bx	lr
.L63:
	.align	2
.L62:
	.word	.LANCHOR0
	.fnend
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L74
	movw	ip, #1080
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
	beq	.L73
	ldr	r5, [r4, #1084]
	add	r8, r4, #1088
	mov	r2, r1, asl #2
	ldr	r9, [r4, #1036]
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
	ldr	r2, .L74+4
	mov	r8, r8, asr #1
	mul	r2, r2, r8
	uxth	r2, r2
.L69:
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
	bcs	.L67
	ldrh	ip, [r3]
	cmp	ip, fp
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L64
.L68:
	ldr	r3, [sp, #4]
	mov	r2, ip
	mla	r3, r3, ip, r9
	b	.L69
.L67:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L70
	strh	r1, [r3, #2]	@ movhi
.L73:
	str	lr, [r0]
	b	.L64
.L70:
	ldrh	ip, [r3, #2]
	mov	r2, #6
	ldr	r0, [r4, #1036]
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L64:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L75:
	.align	2
.L74:
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
	ldr	r3, .L97
	movw	ip, #1080
	ldrh	ip, [r3, ip]
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
	ldr	r10, [r3, #1036]
	mvn	r3, #0
	add	lr, r10, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r10, r5]	@ movhi
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L96
	ldr	r2, [r4, #1092]
	add	r8, r4, #1088
	ldr	r9, [r4, #1084]
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
	ldr	r2, [r4, #1036]
	cmp	r6, #0
	rsb	r6, r2, r3
	str	r2, [sp, #12]
	mvneq	ip, #0
	ldr	r2, .L97+4
	mov	r6, r6, asr #1
	mul	r2, r2, r6
	movw	r6, #1080
	ldrh	r4, [r4, r6]
	mov	r6, #0
	str	r4, [sp, #16]
	uxth	r2, r2
.L86:
	add	r6, r6, #1
	ldr	r4, [sp, #16]
	uxth	r6, r6
	cmp	r6, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r1, r2
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L76
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
	bcs	.L84
	ldrh	r4, [r3]
	movw	r7, #65535
	cmp	r4, r7
	streqh	r2, [lr, #2]	@ movhi
	streqh	r1, [r3]	@ movhi
	beq	.L76
.L85:
	ldr	r2, [sp, #12]
	mov	r3, #6
	mla	r3, r3, r4, r2
	mov	r2, r4
	b	.L86
.L84:
	ldrh	ip, [r3, #2]
	strh	ip, [lr, #2]	@ movhi
	strh	r2, [r10, r5]	@ movhi
	ldr	r2, [r0]
	cmp	r3, r2
	bne	.L87
	strh	r1, [r3, #2]	@ movhi
.L96:
	str	lr, [r0]
	b	.L76
.L87:
	ldr	r2, .L97
	ldrh	ip, [r3, #2]
	ldr	r0, [r2, #1036]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r1, [r0, r2]	@ movhi
	strh	r1, [r3, #2]	@ movhi
.L76:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L98:
	.align	2
.L97:
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
	ldr	r1, .L110
	mov	r6, r0
	ldr	r3, [r1, #1096]
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
	bne	.L103
	ldrh	r2, [r10, r8]
	cmp	r2, r3
	bne	.L103
	ldr	r1, .L110+4
	movw	r2, #273
	ldr	r0, .L110+8
	bl	printk
	bl	dump_stack
.L103:
	ldrh	r2, [r9, #2]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L104
	ldrh	r3, [r10, r8]
	cmp	r3, r2
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L104:
	ldr	r0, [r5, #1092]
	mov	r3, r4, asl #1
	ldrh	r1, [r0, r3]
	ldrh	r3, [r9, #4]
	cmp	r3, #0
	mulne	r1, r3, r1
	mov	r3, #6
	mul	r2, r3, r2
	ldr	r3, .L110+12
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
.L111:
	.align	2
.L110:
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
	bne	.L113
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	add	r2, r0, #1
	orr	r3, r3, #32
	cmp	r3, #120
	bne	.L124
	ldrb	r3, [r0, #2]	@ zero_extendqisi2
	bic	ip, r3, #32
	sub	ip, ip, #65
	cmp	ip, #25
	bls	.L114
	sub	r3, r3, #48
	cmp	r3, #9
	bls	.L114
.L124:
	mov	lr, #8
.L113:
	mov	r0, #0
.L115:
	mov	r4, r2
	ldrb	ip, [r2], #1	@ zero_extendqisi2
	bic	r3, ip, #32
	sub	r3, r3, #65
	cmp	r3, #25
	sub	r3, ip, #48
	bls	.L116
	cmp	r3, #9
	bls	.L116
.L120:
	cmp	r1, #0
	bne	.L117
	ldmfd	sp!, {r4, pc}
.L117:
	str	r4, [r1]
	ldmfd	sp!, {r4, pc}
.L114:
	add	r2, r0, #2
	mov	lr, #16
	b	.L113
.L116:
	cmp	r3, #9
	orrhi	ip, ip, #32
	subhi	r3, ip, #87
	cmp	r3, lr
	bcs	.L120
	mla	r0, lr, r0, r3
	b	.L115
	.fnend
	.size	rk_simple_strtoull.constprop.34, .-rk_simple_strtoull.constprop.34
	.text
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
	bl	ndelay
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
	ldr	r9, .L139
	mov	r5, r0
	ldr	r7, .L139+4
	mov	r6, r1
	add	r10, r9, #44
	mov	r4, #0
.L130:
	ldrb	r3, [r7, #1101]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L138
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	ndelay
	ldrb	r3, [r7, #1100]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L137
	cmp	r3, #35
	addne	r3, r9, r6
	ldrnesb	r3, [r3, #188]
	bne	.L136
	ldr	r3, .L139+8
	add	r2, r4, r8
	add	r3, r3, r2
.L137:
	ldrsb	r3, [r3, #5]
.L136:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L130
.L138:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L140:
	.align	2
.L139:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR1+92
	.fnend
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
	.align	2
	.global	hynix_reconfig_rr_para
	.type	hynix_reconfig_rr_para, %function
hynix_reconfig_rr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, .L147
	ldrb	r2, [r5, #1100]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	ldmhifd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r5, #1040]
	add	r2, r2, r0
	ldrb	r2, [r2, #120]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r1, #0
	mov	r4, r0
	bl	hynix_set_rr_para
	ldr	r2, [r5, #1040]
	mov	r3, #0
	add	r4, r2, r4
	strb	r3, [r4, #120]
	ldmfd	sp!, {r3, r4, r5, pc}
.L148:
	.align	2
.L147:
	.word	.LANCHOR0
	.fnend
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
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
	ldr	r4, .L268
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L150
	ldr	r0, .L268+4
	ldr	r1, .L268+8
	bl	printk
.L150:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L151
	ldr	r3, .L268+12
	ldr	r0, [r3, #1104]
	ldrb	ip, [r0, #4]	@ zero_extendqisi2
	ldrb	r1, [r0, #1]	@ zero_extendqisi2
	ldrb	r2, [r0, #2]	@ zero_extendqisi2
	ldrb	r3, [r0, #3]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #5]	@ zero_extendqisi2
	str	ip, [sp, #4]
	ldrb	r0, [r0, #6]	@ zero_extendqisi2
	str	r0, [sp, #8]
	ldr	r0, .L268+16
	bl	printk
.L151:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L152
	ldr	r3, .L268+12
	ldr	r0, .L268+20
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #8]	@ zero_extendqisi2
	bl	printk
.L152:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L153
	ldr	r3, .L268+12
	ldr	r0, .L268+24
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	printk
.L153:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L154
	ldr	r3, .L268+12
	ldr	r0, .L268+28
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #10]
	bl	printk
.L154:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L155
	ldr	r3, .L268+12
	ldr	r0, .L268+32
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	printk
.L155:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L156
	ldr	r3, .L268+12
	ldr	r0, .L268+36
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #13]	@ zero_extendqisi2
	bl	printk
.L156:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L157
	ldr	r3, .L268+12
	ldr	r0, .L268+40
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #14]
	bl	printk
.L157:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L158
	ldr	r3, .L268+12
	ldr	r0, .L268+44
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #23]	@ zero_extendqisi2
	bl	printk
.L158:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L159
	ldr	r3, .L268+12
	ldr	r0, .L268+48
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #18]	@ zero_extendqisi2
	bl	printk
.L159:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L160
	ldr	r3, .L268+12
	ldr	r0, .L268+52
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	bl	printk
.L160:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L161
	ldr	r3, .L268+12
	ldr	r0, .L268+56
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	bl	printk
.L161:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L162
	ldr	r3, .L268+12
	ldr	r0, .L268+60
	ldrb	r1, [r3, #1108]	@ zero_extendqisi2
	bl	printk
.L162:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L163
	ldr	r3, .L268+12
	ldr	r0, .L268+64
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #22]	@ zero_extendqisi2
	bl	printk
.L163:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L164
	ldr	r3, .L268+12
	ldr	r0, .L268+68
	ldrb	r1, [r3, #1109]	@ zero_extendqisi2
	bl	printk
.L164:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L165
	ldr	r3, .L268+12
	ldr	r0, .L268+72
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	and	r1, r1, #1
	bl	printk
.L165:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L166
	ldr	r3, .L268+12
	ldr	r0, .L268+76
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #1, #1
	bl	printk
.L166:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L167
	ldr	r3, .L268+12
	ldr	r0, .L268+80
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #2, #1
	bl	printk
.L167:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L168
	ldr	r3, .L268+12
	ldr	r0, .L268+84
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #3, #1
	bl	printk
.L168:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L169
	ldr	r3, .L268+12
	ldr	r0, .L268+88
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #4, #1
	bl	printk
.L169:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L170
	ldr	r3, .L268+12
	ldr	r0, .L268+92
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #5, #1
	bl	printk
.L170:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L171
	ldr	r3, .L268+12
	ldr	r0, .L268+96
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #6, #1
	bl	printk
.L171:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L172
	ldr	r3, .L268+12
	ldr	r0, .L268+100
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #7, #1
	bl	printk
.L172:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L173
	ldr	r3, .L268+12
	ldr	r0, .L268+104
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #8, #1
	bl	printk
.L173:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L174
	ldr	r3, .L268+12
	ldr	r0, .L268+108
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #9, #1
	bl	printk
.L174:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L175
	ldr	r3, .L268+12
	ldr	r0, .L268+112
	ldr	r3, [r3, #1104]
	ldrh	r1, [r3, #16]
	ubfx	r1, r1, #10, #1
	bl	printk
.L175:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L176
	ldr	r3, .L268+12
	ldr	r0, .L268+116
	ldrb	r1, [r3]	@ zero_extendqisi2
	ldrb	r2, [r3, #1110]	@ zero_extendqisi2
	bl	printk
.L176:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L177
	ldr	r3, .L268+12
	ldr	r0, .L268+120
	ldrb	r1, [r3, #1121]	@ zero_extendqisi2
	ldrb	r2, [r3, #1122]	@ zero_extendqisi2
	bl	printk
.L177:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L178
	ldr	r3, .L268+12
	ldr	r0, .L268+124
	ldrb	r1, [r3, #1119]	@ zero_extendqisi2
	ldrb	r2, [r3, #1120]	@ zero_extendqisi2
	bl	printk
.L178:
	ldr	r3, [r4]
	tst	r3, #4096
	beq	.L149
	ldr	r3, .L268+12
	ldr	r0, .L268+128
	ldrb	r1, [r3, #1143]	@ zero_extendqisi2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, lr}
	b	printk
.L149:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L269:
	.align	2
.L268:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+196
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
	.type	nandc_set_ddr_para, %function
nandc_set_ddr_para:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L274
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1044]
	cmp	r2, #9
	mov	r2, r0, asl #16
	mov	r0, r0, asl #8
	orr	r0, r2, r0
	orr	r0, r0, #3
	streq	r0, [r3, #80]
	strne	r0, [r3, #304]
	bx	lr
.L275:
	.align	2
.L274:
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
	ldr	r3, .L280
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	ldr	r3, [r3, #1044]
	cmp	r2, #9
	ldreq	r0, [r3, #80]
	ldrne	r0, [r3, #304]
	ubfx	r0, r0, #8, #8
	bx	lr
.L281:
	.align	2
.L280:
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
	ldr	r1, .L290
	ands	ip, r0, #6
	ldr	r3, [r1, #1044]
	ldr	r2, [r3]
	bfieq	r2, ip, #13, #1
	beq	.L286
	orr	r2, r2, #24576
	ldrb	r1, [r1, #1028]	@ zero_extendqisi2
	bfc	r2, #15, #1
	tst	r0, #4
	orr	r2, r2, #196608
	movw	r0, #8321
	orrne	r2, r2, #32768
	cmp	r1, #9
	ldr	r1, .L290+4
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
.L286:
	str	r2, [r3]
	bx	lr
.L291:
	.align	2
.L290:
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
	ldr	r3, .L293
	mov	r2, #1
	mov	r0, r2, asl r0
	ldr	r1, [r3, #1044]
	ldr	r3, [r1]
	bfi	r3, r0, #0, #8
	str	r3, [r1]
	bx	lr
.L294:
	.align	2
.L293:
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
	ldr	r3, .L296
	ldr	r2, [r3, #1044]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L297:
	.align	2
.L296:
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
	ldr	r4, .L307
	mov	r5, r1
	mov	r7, r2
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L299
	ldr	r1, .L307+4
	mov	r2, #812
	ldr	r0, .L307+8
	bl	printk
	bl	dump_stack
.L299:
	add	r6, r4, r6
	ldr	r3, [r4, #1044]
	mov	r9, r5, lsr #16
	mov	r10, r5, lsr #24
	ldrb	r6, [r6, #1144]	@ zero_extendqisi2
	mov	fp, #120
	mov	r2, r5, lsr #8
	str	r2, [sp]
	add	r8, r3, r6, asl #8
.L301:
	mov	r0, r6
	bl	nandc_cs
	str	fp, [r8, #2056]
	uxtb	r2, r5
	str	r2, [r8, #2052]
	ldrb	r2, [r4, #1152]	@ zero_extendqisi2
	mov	r0, #120
	cmp	r2, #0
	ldr	r3, [sp]
	str	r3, [r8, #2052]
	str	r9, [r8, #2052]
	strne	r10, [r8, #2052]
	bl	ndelay
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
	bne	.L301
	mov	r0, r2
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L308:
	.align	2
.L307:
	.word	.LANCHOR0
	.word	.LANCHOR1+220
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
	ldr	r6, .L321
	tst	r0, #50331648
	mov	r8, r1
	ldrb	r2, [r6, #1153]	@ zero_extendqisi2
	rsb	r5, r2, #24
	mov	r4, r3, asl r5
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	sub	r4, r4, #1
	and	r5, r3, r0, asr r5
	and	r4, r4, r0
	uxtb	r5, r5
	bne	.L310
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L311
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L310
.L311:
	ldrh	r7, [r6, #2]
	mov	r0, r4
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L321
	mul	r0, r7, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L310:
	mov	r0, r5
	mov	r1, r4
	mov	r2, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready_raw
.L322:
	.align	2
.L321:
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
	ldr	r4, .L329
	ldr	r5, .L329+4
.L325:
	ldr	r3, [r5, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L326
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L325
	mvn	r0, #0
	b	.L324
.L326:
	mov	r0, #0
.L324:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L330:
	.align	2
.L329:
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
	ldr	r3, .L350
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldr	r5, [r3, #1044]
	bne	.L334
	ldr	r3, .L350+4
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r5, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L334:
	cmp	r2, #2
	mov	r4, r0
	bne	.L336
	add	r3, r3, r0
	ldrb	r2, [r3, #1156]	@ zero_extendqisi2
	cmp	r2, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r6, .L350+4
	mov	r2, #0
	strb	r2, [r3, #1156]
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	cmp	r3, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r6, #33]	@ zero_extendqisi2
	add	r4, r5, r4, asl #8
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L336:
	cmp	r2, #3
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	add	r6, r3, r0
	ldrb	r3, [r6, #1156]	@ zero_extendqisi2
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
	strb	r7, [r6, #1156]
	bl	ndelay
	str	r7, [r4, #2048]
	mov	r3, #1
	mov	r0, #150
	str	r3, [r4, #2048]
	str	r7, [r4, #2048]
	str	r7, [r4, #2048]
	bl	ndelay
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #218
	mov	r0, #50
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	ndelay
.L351:
	.align	2
.L350:
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
	ldr	r3, .L373
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r4, [r3]	@ zero_extendqisi2
	cmp	r4, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	cmp	r4, #1
	ldr	r7, [r3, #1044]
	bne	.L355
	ldr	r3, .L373+4
	ldrb	r3, [r3, #34]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r0, r7, r0, asl #8
	strne	r3, [r0, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L355:
	cmp	r4, #2
	mov	r5, r0
	bne	.L357
	add	r3, r3, r0
	ldrb	r2, [r3, #1156]	@ zero_extendqisi2
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r4, .L373+4
	ldrb	r2, [r4, #16]	@ zero_extendqisi2
	cmp	r2, #2
	movne	r2, #4
	strb	r2, [r3, #1156]
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	bl	nandc_wait_flash_ready_no_delay
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	add	r5, r7, r5, asl #8
	str	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L357:
	cmp	r4, #3
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	add	r4, r3, r0
	ldrb	r3, [r4, #1156]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	ldr	r8, .L373+4
	bl	nandc_wait_flash_ready_no_delay
	mov	r5, r5, asl #8
	mov	r0, #100
	add	r6, r7, r5
	add	r5, r7, r5
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	cmp	r3, #2
	movne	r3, #4
	strb	r3, [r4, #1156]
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #145
	str	r3, [r6, #2052]
	bl	ndelay
	ldrb	r3, [r8, #11]	@ zero_extendqisi2
	mov	r0, #150
	cmp	r3, #9
	mov	r3, #1
	streq	r3, [r6, #2048]
	ldrneb	r2, [r4, #1156]	@ zero_extendqisi2
	strne	r2, [r6, #2048]
	str	r3, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	bl	ndelay
	bl	nandc_wait_flash_ready_no_delay
	mov	r3, #223
	mov	r0, #50
	str	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	ndelay
.L374:
	.align	2
.L373:
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
	ldr	r7, .L390
	mov	r5, r0
	mov	r8, r1
	ldrb	r3, [r7, #1153]	@ zero_extendqisi2
	rsb	r6, r3, #24
	mvn	r2, r2, asl r3
	and	r2, r2, r1, lsr r6
	ldrb	r3, [r7, #1109]	@ zero_extendqisi2
	uxtb	r4, r2
	cmp	r3, r4
	bhi	.L376
	ldr	r1, .L390+4
	movw	r2, #1013
	ldr	r0, .L390+8
	bl	printk
	bl	dump_stack
.L376:
	ldrb	r3, [r7, #1109]	@ zero_extendqisi2
	ldr	r9, .L390
	cmp	r3, r4
	ldmlsfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	add	r4, r9, r4
	mvn	r3, #0
	bic	r6, r8, r3, asl r6
	ubfx	r8, r8, #24, #2
	ldrb	fp, [r4, #1144]	@ zero_extendqisi2
	ldr	r4, [r9, #1044]
	mov	r0, fp
	mov	r10, fp, asl #8
	bl	nandc_cs
	cmp	r8, #0
	bne	.L378
	mov	r0, r6
	bl	slc_phy_page_address_calc
	ldrb	r3, [r9]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L379
	mov	r0, fp
	bl	zftl_flash_enter_slc_mode
	b	.L379
.L378:
	ldr	r3, [r9, #1104]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L380
	ldrb	r3, [r9, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L380
	ldrb	r3, [r9, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r4, r10
	streq	r8, [r3, #2056]
	beq	.L379
.L380:
	mov	r0, fp
	bl	zftl_flash_exit_slc_mode
.L379:
	ldr	r3, [r7, #1104]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L381
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r4, r10
	moveq	r2, #38
	streq	r2, [r3, #2056]
.L381:
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
	ldrb	r3, [r7, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, r6, lsr #24
	strne	r3, [r2, #2052]
	add	r3, r4, r10
	str	r5, [r3, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L391:
	.align	2
.L390:
	.word	.LANCHOR0
	.word	.LANCHOR1+248
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
	bl	ndelay
	ldr	r4, .L398
	ldr	r5, .L398+4
.L394:
	ldr	r3, [r5, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L395
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L394
	mvn	r0, #0
	b	.L393
.L395:
	mov	r0, #0
.L393:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L399:
	.align	2
.L398:
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
	bl	ndelay
	ldr	r0, .L408
	ldr	r1, .L408+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L401:
	ldrb	r3, [r1, #1101]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L407
	ldrb	r3, [r1, #1100]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L401
.L407:
	ldmfd	sp!, {r3, r4, r5, lr}
	b	nandc_wait_flash_ready
.L409:
	.align	2
.L408:
	.word	.LANCHOR1+92
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
	ldr	r5, .L412
	mov	r6, #0
	mov	r7, #213
	mvn	r3, #118
	add	r5, r5, r1
	str	r7, [r0, #8]
	str	r6, [r0, #4]
	mov	r4, r0
	str	r3, [r0, #4]
	add	r3, r5, #272
	ldrsb	r3, [r3]
	str	r3, [r0]
	movw	r3, #273
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #274
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	movw	r3, #275
	ldrsb	r3, [r5, r3]
	str	r3, [r0]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mvn	r3, #117
	str	r6, [r4, #4]
	str	r3, [r4, #4]
	add	r3, r5, #276
	ldrsb	r3, [r3]
	str	r3, [r4]
	movw	r3, #277
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #278
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	nandc_wait_flash_ready
.L413:
	.align	2
.L412:
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
	ldr	r2, .L415
	add	r2, r2, r1
	movw	r1, #673
	ldrsb	r2, [r2, r1]
	str	r2, [r0]
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L416:
	.align	2
.L415:
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
	ldr	r3, .L421
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, #239
	mov	r4, r0
	str	r6, [r0, #8]
	beq	.L418
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
	b	.L419
.L418:
	add	r3, r3, r1
	mov	r0, #20
	str	r0, [r4, #4]
	ldrb	r3, [r3, #368]	@ zero_extendqisi2
	str	r3, [r4]
	str	r2, [r4]
	str	r2, [r4]
	str	r2, [r4]
.L419:
	ldmfd	sp!, {r4, r5, r6, lr}
	b	nandc_wait_flash_ready
.L422:
	.align	2
.L421:
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
	ldr	r5, .L425
	mov	r7, #239
	mov	r3, #160
	str	r7, [r0, #8]
	add	r5, r5, r1
	str	r3, [r0, #4]
	add	r3, r5, #684
	mov	r4, r0
	mov	r6, #0
	ldrsb	r3, [r3]
	str	r3, [r0]
	add	r3, r5, #688
	ldrsb	r3, [r3]
	str	r3, [r0]
	str	r6, [r0]
	str	r6, [r0]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mov	r3, #161
	str	r3, [r4, #4]
	movw	r3, #685
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #687
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #689
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	bl	nandc_wait_flash_ready
	str	r7, [r4, #8]
	mov	r3, #162
	str	r3, [r4, #4]
	movw	r3, #686
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	movw	r3, #690
	ldrsb	r3, [r5, r3]
	str	r3, [r4]
	str	r6, [r4]
	str	r6, [r4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	nandc_wait_flash_ready
.L426:
	.align	2
.L425:
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
	ldr	r3, .L428
	movw	r2, #1044
	add	r1, r3, r1
	ldrsb	r3, [r1, r2]
	str	r3, [r0]
	mov	r3, #0
	str	r3, [r0]
	str	r3, [r0]
	str	r3, [r0]
	b	nandc_wait_flash_ready
.L429:
	.align	2
.L428:
	.word	.LANCHOR1
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
	ldr	r7, .L458
	ubfx	r9, r0, #24, #2
	ldrb	r2, [r7, #1153]	@ zero_extendqisi2
	rsb	r5, r2, #24
	mov	r4, r3, asl r5
	mov	r3, r3, asl r2
	sub	r3, r3, #1
	sub	r4, r4, #1
	and	r5, r3, r0, lsr r5
	ldrb	r3, [r7, #1109]	@ zero_extendqisi2
	and	r6, r4, r0
	and	r4, r4, r1
	uxtb	r5, r5
	cmp	r3, r5
	bhi	.L431
	ldr	r1, .L458+4
	movw	r2, #1148
	ldr	r0, .L458+8
	bl	printk
	bl	dump_stack
.L431:
	add	r5, r7, r5
	ldr	r8, [r7, #1044]
	ldr	fp, .L458
	ldrb	r5, [r5, #1144]	@ zero_extendqisi2
	mov	r0, r5
	mov	r10, r5, asl #8
	bl	nandc_cs
	cmp	r9, #0
	bne	.L432
	mov	r0, r6
	bl	slc_phy_page_address_calc
	mov	r6, r0
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [fp]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r4, r0
	beq	.L433
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L433
.L432:
	ldr	r3, [fp, #1104]
	ldrb	r3, [r3, #12]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L434
	ldrb	r3, [fp, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L434
	ldrb	r3, [fp, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r8, r10
	streq	r9, [r3, #2056]
	beq	.L433
.L434:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L433:
	ldrb	r2, [r7, #1127]	@ zero_extendqisi2
	uxtb	ip, r6
	ldr	r3, .L458
	mov	r0, r6, lsr #8
	cmp	r2, #1
	mov	r1, r6, lsr #16
	bne	.L435
	ldrb	r2, [r3, #1119]	@ zero_extendqisi2
	add	r5, r8, r10
	ldrb	r3, [r3, #1152]	@ zero_extendqisi2
	str	r2, [r5, #2056]
	mov	r2, #0
	cmp	r3, r2
	ldrb	r3, [r7, #1120]	@ zero_extendqisi2
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
	ldr	r3, [r7, #1104]
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
	ldrb	r3, [r7, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r3, #48
	movne	r4, r4, lsr #24
	strne	r4, [r5, #2052]
	str	r3, [r10, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L435:
	ldr	r2, [r3, #1104]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L440
	ldrb	r3, [r2, #12]	@ zero_extendqisi2
	cmp	r3, #2
	addeq	r3, r8, r10
	moveq	lr, #38
	streq	lr, [r3, #2056]
.L440:
	ldrb	lr, [r7, #1119]	@ zero_extendqisi2
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
	ldrb	r2, [r7, #1120]	@ zero_extendqisi2
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
.L459:
	.align	2
.L458:
	.word	.LANCHOR0
	.word	.LANCHOR1+1056
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
	ldr	r6, .L494
	mov	r10, #0
	ldr	r9, .L494+4
	mov	r7, r6
.L472:
	ldrb	r4, [r9, r10, asl #3]	@ zero_extendqisi2
	ldr	r5, [r6, #1044]
	cmp	r4, #69
	cmpne	r4, #152
	beq	.L461
	cmp	r4, #137
	cmpne	r4, #44
	moveq	r3, #1
	movne	r3, #0
	cmp	r4, #155
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L462
.L461:
	cmp	r8, #1
	ldrb	r1, [r7, #1196]	@ zero_extendqisi2
	bne	.L463
	tst	r1, #1
	beq	.L462
	ldr	r3, .L494+8
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L464
	ldr	r0, .L494+12
	bl	printk
.L464:
	mov	r2, r10, asl #8
	cmp	r4, #44
	cmpne	r4, #137
	add	r3, r5, r2
	mov	r1, #239
	str	r1, [r3, #2056]
	beq	.L493
	cmp	r4, #155
	movne	r1, #128
	strne	r1, [r3, #2052]
	movne	r1, #1
	bne	.L491
.L493:
	mov	r1, #1
	str	r1, [r3, #2052]
	mov	r1, #5
	b	.L491
.L463:
	tst	r1, #4
	beq	.L462
	ldr	r3, .L494+8
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L468
	ldr	r0, .L494+16
	bl	printk
.L468:
	mov	r2, r10, asl #8
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
	beq	.L491
	cmp	r4, #155
	moveq	r1, #1
	streq	r1, [r3, #2052]
	moveq	r1, #37
	movne	r0, #128
	strne	r0, [r3, #2052]
.L491:
	add	r5, r5, r2
	str	r1, [r3, #2048]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L462:
	add	r10, r10, #1
	cmp	r10, #4
	bne	.L472
	bl	nandc_wait_flash_ready
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L495:
	.align	2
.L494:
	.word	.LANCHOR0
	.word	.LANCHOR0+1164
	.word	.LANCHOR2
	.word	.LC34
	.word	.LC35
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
	ldr	r3, .L497
	mov	r2, #255
	ldr	r3, [r3, #1044]
	add	r0, r3, r0, asl #8
	str	r2, [r0, #2056]
	b	nandc_wait_flash_ready
.L498:
	.align	2
.L497:
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
	ldr	r3, .L502
	mov	r4, r1
	ldr	r5, [r3, #1044]
	bl	flash_reset
	mov	r0, r6
	add	r5, r5, r6, asl #8
	bl	nandc_cs
	mov	r3, #144
	mov	r0, #200
	str	r3, [r5, #2056]
	mov	r3, #0
	str	r3, [r5, #2052]
	bl	ndelay
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
	bhi	.L499
	ldrb	r1, [r4, #2]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	ldr	r0, .L502+4
	str	r1, [sp]
	ldrb	r1, [r4, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r4, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r6, #1
	bl	printk
.L499:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L503:
	.align	2
.L502:
	.word	.LANCHOR0
	.word	.LC36
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
	ldr	r3, .L506
	ldr	r2, .L506+4
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	ldr	r4, [r2, #1044]
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
.L507:
	.align	2
.L506:
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
	ldr	r3, .L512
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r0
	ldr	r7, [r3, #1044]
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
	bl	ndelay
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
	ldr	r0, .L512+4
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
.L509:
	ldr	r2, [r4, #2048]
	strb	r2, [r9], #1
	cmp	r9, r3
	bne	.L509
	add	r7, r7, r10
	mov	r3, #239
	mov	r0, #50
	str	r3, [r7, #2056]
	mov	r3, #144
	str	r3, [r4, #2052]
	bl	ndelay
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
.L513:
	.align	2
.L512:
	.word	.LANCHOR0
	.word	.LC37
	.fnend
	.size	flash_read_otp_data, .-flash_read_otp_data
	.align	2
	.global	sandisk_prog_test_bad_block
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L530
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0, asl #8
	ldr	r6, .L530+4
	mov	r8, r1
	ldr	r5, [r3, #1044]
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
	bl	ndelay
	ldr	r2, [r4, #2048]
	cmp	r2, #255
	ldreq	r2, [r4, #2048]
	ands	r4, r2, #5
	beq	.L518
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L518
	ldr	r0, .L530+8
	mov	r1, r8
	bl	printk
.L518:
	ldrb	r3, [r6, #34]	@ zero_extendqisi2
	mov	r0, r4
	cmp	r3, #0
	addne	r5, r5, r7
	strne	r3, [r5, #2056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L531:
	.align	2
.L530:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC38
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
	ldr	r3, .L534
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
.L535:
	.align	2
.L534:
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
	ldr	r2, .L549
	ldrb	r1, [r2, #1028]	@ zero_extendqisi2
	strb	r0, [r2, #1197]
	cmp	r1, #9
	bne	.L537
	ldr	r1, [r2, #1044]
	cmp	r0, #70
	ldr	r2, [sp, #4]
	str	r2, [r1, #16]
	beq	.L538
	cmp	r0, #60
	moveq	r3, #3
	beq	.L538
	cmp	r0, #40
	moveq	r3, #2
	movne	r3, #1
.L538:
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
	b	.L536
.L537:
	ldr	r1, [sp, #4]
	cmp	r0, #16
	ldr	r2, [r2, #1044]
	str	r1, [r2, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	bic	r3, r3, #65280
	orr	r3, r3, #4096
	str	r3, [sp]
	ldr	r3, [sp]
	bic	r3, r3, #262144
	str	r3, [sp]
	beq	.L547
	ldr	r3, [sp]
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L548
	orr	r3, r3, #262144
	str	r3, [sp]
	ldr	r3, [sp]
	cmp	r0, #40
	orr	r3, r3, #16
	str	r3, [sp]
	bne	.L541
.L547:
	ldr	r3, [sp]
	bic	r3, r3, #16
.L548:
	str	r3, [sp]
.L541:
	ldr	r3, [sp]
	orr	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [sp]
	str	r3, [r2, #12]
.L536:
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L550:
	.align	2
.L549:
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
	ldr	r4, .L562
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	ldr	r2, [r4, #1048]
	cmp	r3, #9
	ldr	r3, [r4, #1044]
	str	r2, [r3]
	ldr	r2, [r4, #1052]
	ldr	r3, [r4, #1044]
	str	r2, [r3, #4]
	ldr	r2, [r4, #1056]
	bne	.L552
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
	b	.L553
.L552:
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
.L553:
	ldr	r6, .L562+4
	mov	r5, #0
	mov	r8, #2
	sub	r7, r6, #8
.L555:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L554
	uxtb	r9, r5
	mov	r0, r9
	bl	flash_reset
	strb	r8, [r5, r7]
	mov	r0, r9
	bl	zftl_flash_enter_slc_mode
.L554:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L555
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L556
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r0, r5
	bl	flash_set_interface_mode
	mov	r0, r5
	bl	nandc_set_if_mode
	ldr	r3, .L562
	ldrb	r0, [r3, #1065]	@ zero_extendqisi2
	bl	nandc_set_ddr_para
.L556:
	ldr	r3, .L562+8
	ldrb	r0, [r3, #24]	@ zero_extendqisi2
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	nandc_bch_sel
.L563:
	.align	2
.L562:
	.word	.LANCHOR0
	.word	.LANCHOR0+1164
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
	ldr	r3, .L567
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
.L568:
	.align	2
.L567:
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
	ldr	r4, .L576
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
	bne	.L572
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L572:
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
.L577:
	.align	2
.L576:
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
	ldr	fp, .L603
	mov	r4, r0
	mov	r5, r1
	mov	r9, r2
	ldrb	r3, [fp, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L579
	ldr	r1, .L603+4
	movw	r2, #695
	ldr	r0, .L603+8
	bl	printk
	bl	dump_stack
.L579:
	ldrb	r2, [fp, #1109]	@ zero_extendqisi2
	ldr	r3, .L603
	cmp	r2, r4
	mvnls	r0, #0
	bls	.L580
	add	r4, r3, r4
	ldr	r7, [r3, #1044]
	ldr	r3, .L603+12
	ldrb	r6, [r4, #1144]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r7, r10, asl #8
	beq	.L581
	ldr	r0, .L603+16
	mov	r1, r6
	mov	r2, r9
	mov	r3, r8
	bl	printk
.L581:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r5, #0
	mov	r0, r6
	bne	.L582
	bl	zftl_flash_enter_slc_mode
	b	.L583
.L582:
	bl	zftl_flash_exit_slc_mode
.L583:
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
	ldrb	r2, [fp, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r9, lsr #24
	strne	r2, [r4, #2052]
	ldrb	r2, [fp, #1198]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r5, #0
	bne	.L585
	add	r2, r7, r3
	mov	r1, #208
	str	r3, [sp, #4]
	str	r1, [r2, #2056]
	bl	nandc_wait_flash_ready
	mov	r0, r10
	bl	flash_read_status
	ldr	r3, [sp, #4]
	and	r5, r0, #5
.L585:
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
	ldrb	r2, [fp, #1152]	@ zero_extendqisi2
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
	beq	.L587
	ldr	r0, .L603+20
	mov	r1, r9
	mov	r2, r4
	bl	printk
.L587:
	mov	r0, r5
.L580:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L604:
	.align	2
.L603:
	.word	.LANCHOR0
	.word	.LANCHOR1+1080
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC39
	.word	.LC40
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
	ldr	r5, .L624
	mov	r7, r0
	mov	fp, r1
	mov	r8, r2
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	bic	r4, r2, r4, asl r3
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L606
	ldr	r1, .L624+4
	movw	r2, #757
	ldr	r0, .L624+8
	bl	printk
	bl	dump_stack
.L606:
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	ldr	r3, .L624
	cmp	r2, r7
	bls	.L613
	add	r2, r3, r7
	ldr	r9, [r3, #1044]
	ldr	r3, .L624+12
	ldrb	r6, [r2, #1144]	@ zero_extendqisi2
	ldr	r3, [r3]
	add	r10, r6, #8
	tst	r3, #16
	add	r10, r9, r10, asl #8
	beq	.L608
	ldr	r0, .L624+16
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L608:
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r6
	bne	.L609
	bl	zftl_flash_enter_slc_mode
	b	.L610
.L609:
	bl	zftl_flash_exit_slc_mode
.L610:
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
	ldrb	r1, [r5, #1152]	@ zero_extendqisi2
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
	beq	.L612
	ldrh	r1, [r5, #2]
	mov	r0, r8
	bl	__aeabi_uidiv
	mov	r3, r4
	mov	r1, r7
	mvn	r4, #0
	mov	r2, r0
	ldr	r0, .L624+20
	bl	printk
.L612:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L613:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L625:
	.align	2
.L624:
	.word	.LANCHOR0
	.word	.LANCHOR1+1108
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC41
	.word	.LC42
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
	ldr	r3, .L635
	ldr	r7, .L635+4
	ldrb	r4, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	mov	r8, r7
	smulbb	r4, r4, r3
	uxth	r4, r4
.L628:
	ldrb	r2, [r7, #1109]	@ zero_extendqisi2
	uxth	r3, r6
	cmp	r2, r3
	bls	.L633
	add	r3, r7, r3
	mov	r5, #0
	ldrb	r9, [r3, #1144]	@ zero_extendqisi2
.L629:
	uxth	r3, r5
	cmp	r3, r4
	bcs	.L634
	ldrh	r1, [r8, #2]
	mov	r0, r9
	mul	r1, r1, r5
	bl	flash_erase_block
	add	r5, r5, #1
	b	.L629
.L634:
	add	r6, r6, #1
	b	.L628
.L633:
	ldr	r0, .L635+8
	mov	r1, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L636:
	.align	2
.L635:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC43
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
	ldr	r3, .L640
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
.L641:
	.align	2
.L640:
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
	ldr	r4, .L659
	mov	r0, r2
	.pad #20
	sub	sp, sp, #20
	and	r6, r5, #1
	add	lr, r1, #1
	ldrb	r2, [r4, #1028]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L643
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
	ldrb	r2, [r4, #1199]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L644
	ldrb	r2, [r4, #1200]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [sp, #4]
	orrne	r2, r2, #512
	strne	r2, [sp, #4]
.L644:
	ldr	r1, [sp, #4]
	mov	r2, r5
	str	r3, [r4, #1212]
	ubfx	r1, r1, #22, #6
	str	r0, [r4, #1208]
	mov	r1, r1, asl #10
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #1216]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #1212]
	mov	r1, r1, asl #2
	bl	rknand_dma_map_single
	ldr	r2, [r4, #1044]
	mov	r3, #1
	str	r3, [r4, #1224]
	ldr	r3, [r4, #1216]
	str	r3, [r2, #52]
	str	r0, [r2, #56]
	ldr	r3, [r2, #48]
	str	r0, [r4, #1220]
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
	ldr	r4, [sp, #8]
	bic	r4, r4, #2
	orr	r5, r4, r5, asl #1
	str	r5, [sp, #8]
	ldr	r3, [sp, #8]
	orr	r3, r3, #1
	str	r3, [sp, #8]
	ldr	r3, .L659+4
	ldrh	r1, [r3]
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
	b	.L642
.L643:
	ldr	r2, [r4, #1044]
	mov	ip, #0
	ubfx	lr, lr, #1, #6
	cmp	r5, ip
	ldr	r2, [r2, #12]
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
	ldreq	r3, [r4, #1204]
	streq	r2, [r3]
	beq	.L650
	ldrb	r2, [r4, #1197]	@ zero_extendqisi2
	mov	r1, r1, lsr #1
	mov	r6, ip
	cmp	r2, #25
	movcc	r7, #64
	movcs	r7, #128
.L648:
	cmp	r6, r1
	add	r3, r3, #4
	add	r8, ip, r7
	bge	.L650
	ldrh	lr, [r3, #-2]
	mov	ip, ip, lsr #2
	ldr	r9, [r4, #1204]
	add	r6, r6, #1
	ldrh	r2, [r3, #-4]
	orr	lr, r2, lr, asl #16
	str	lr, [r9, ip, asl #2]
	mov	ip, r8
	b	.L648
.L650:
	ldr	r1, [sp, #4]
	mov	r2, r5
	ldr	r3, [r4, #1204]
	ubfx	r1, r1, #22, #6
	str	r0, [r4, #1208]
	mov	r1, r1, asl #10
	str	r3, [r4, #1212]
	bl	rknand_dma_map_single
	mov	r2, r5
	clz	r5, r5
	mov	r5, r5, lsr #5
	ldr	r1, [sp, #4]
	str	r0, [r4, #1216]
	ubfx	r1, r1, #22, #6
	ldr	r0, [r4, #1212]
	mov	r1, r1, asl #7
	bl	rknand_dma_map_single
	ldr	r2, [r4, #1216]
	mov	r3, #1
	str	r3, [r4, #1224]
	ldr	r3, [r4, #1044]
	str	r2, [r3, #20]
	mov	r2, #0
	str	r0, [r3, #24]
	str	r0, [r4, #1220]
	str	r2, [sp, #8]
	ldr	r2, [sp, #8]
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
.L642:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L660:
	.align	2
.L659:
	.word	.LANCHOR0
	.word	.LANCHOR0+1232
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
	ldr	r2, .L668
	and	r0, r0, #127
	ldr	r3, .L668+4
	mov	r0, r0, asl #1
	ldrb	r1, [r2, #1200]	@ zero_extendqisi2
	ldrh	r3, [r3, r0]
	cmp	r1, #0
	ldr	r1, .L668
	ldrb	r2, [r2, #1028]	@ zero_extendqisi2
	orrne	r3, r3, #-1073741824
	cmp	r2, #9
	ldr	r2, [r1, #1044]
	streq	r3, [r2, #520]
	strne	r3, [r2, #336]
	bx	lr
.L669:
	.align	2
.L668:
	.word	.LANCHOR0
	.word	.LANCHOR1+1132
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
	ldr	r4, .L690
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L671
	ldrb	r0, [r4, #1110]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L671
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	beq	.L671
	bl	zftl_flash_exit_slc_mode
.L671:
	mov	r0, #0
	ldr	r5, .L690
	bl	hynix_reconfig_rr_para
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L672
	ldrb	r3, [r5, #1196]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L672
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r5, #1143]
.L672:
	ldrb	r3, [r4, #1200]	@ zero_extendqisi2
	ldr	r5, .L690
	cmp	r3, #0
	beq	.L673
	mov	r0, #0
	strb	r0, [r5, #1200]
	bl	nandc_set_seed
	mov	r3, #1
	strb	r3, [r5, #1200]
.L673:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L691:
	.align	2
.L690:
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
	ldr	r3, .L693
	strb	r0, [r3, #1200]
	bx	lr
.L694:
	.align	2
.L693:
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
	ldr	r3, .L696
	add	r0, r0, #8
	ldr	r3, [r3, #1044]
	add	r0, r3, r0, asl #8
	bx	lr
.L697:
	.align	2
.L696:
	.word	.LANCHOR0
	.fnend
	.size	nandc_get_chip_if, .-nandc_get_chip_if
	.align	2
	.global	buf_reinit
	.type	buf_reinit, %function
buf_reinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L701
	mov	r3, #0
	mov	r1, r3
.L699:
	uxtb	r0, r3
	add	r3, r3, #1
	cmp	r3, #32
	add	ip, r0, #1
	strb	r0, [r2, #1]
	add	r2, r2, #48
	strb	ip, [r2, #-48]
	strb	r1, [r2, #-46]
	str	r1, [r2, #-40]
	bne	.L699
	ldr	r2, .L701+4
	mvn	r1, #0
	strb	r3, [r2, #2773]
	strb	r1, [r2, #2724]
	mov	r1, #0
	strb	r1, [r2, #2772]
	bx	lr
.L702:
	.align	2
.L701:
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
	ldrne	r2, .L711
	movne	r1, #48
	bne	.L709
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L705
	ldr	r1, .L711+4
	mov	r2, #74
	ldr	r0, .L711+8
	bl	printk
	bl	dump_stack
.L705:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L709:
	mov	r5, r3
	mla	r3, r1, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L709
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L707
	ldr	r1, .L711+4
	mov	r2, #81
	ldr	r0, .L711+8
	bl	printk
	bl	dump_stack
.L707:
	ldr	r2, .L711
	mov	r1, #48
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	mla	r5, r1, r5, r2
	strb	r3, [r5, #1236]
	ldmfd	sp!, {r3, r4, r5, pc}
.L712:
	.align	2
.L711:
	.word	.LANCHOR0
	.word	.LANCHOR1+1388
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
	ldr	r0, .L715
	mov	r3, #0
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, lr}
	b	buf_add_tail
.L716:
	.align	2
.L715:
	.word	.LANCHOR0+2774
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
	ldr	r4, .L727
	and	r3, r3, #8
	uxtb	r3, r3
	strb	r3, [r0, #2]
	cmp	r3, #0
	beq	.L718
	ldr	r3, [r0, #20]
	cmn	r3, #1
	beq	.L718
	mov	r1, r0
	ldr	r0, .L727+4
	bl	buf_add_tail
	b	.L719
.L718:
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	strb	r3, [r0]
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	strb	r3, [r4, #2772]
.L719:
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #2773]
	ldmfd	sp!, {r4, pc}
.L728:
	.align	2
.L727:
	.word	.LANCHOR0
	.word	.LANCHOR0+2772
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
	ldr	r4, .L740
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L730
.L733:
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	cmp	r5, #0
	ldr	r3, .L740+4
	mov	r0, #48
	mla	r0, r0, r1, r3
	beq	.L731
	b	.L732
.L730:
	ldr	r1, .L740+8
	mov	r2, #121
	ldr	r0, .L740+12
	bl	printk
	bl	dump_stack
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L733
.L735:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L731:
	ldr	r2, .L740
	ldrb	r2, [r2, #2773]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L735
.L732:
	mov	r2, #48
	mul	r1, r2, r1
	add	r2, r4, r1
	add	r3, r3, r1
	mov	r1, #0
	ldrb	ip, [r2, #1236]	@ zero_extendqisi2
	strh	r1, [r3, #34]	@ movhi
	str	r1, [r2, #1244]
	strb	ip, [r4, #2772]
	ldrb	ip, [r4, #2773]	@ zero_extendqisi2
	strb	r1, [r2, #1276]
	sub	ip, ip, #1
	strb	r1, [r2, #1277]
	strb	ip, [r4, #2773]
	mov	ip, #1
	strb	ip, [r2, #1238]
	mvn	ip, #0
	strb	ip, [r2, #1236]
	str	ip, [r2, #1256]
	ldmfd	sp!, {r3, r4, r5, pc}
.L741:
	.align	2
.L740:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+1404
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
	bne	.L743
	ldrb	r3, [r1]	@ zero_extendqisi2
	strb	r3, [r0]
	mov	r0, #1
	bx	lr
.L743:
	ldr	r0, .L750
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #48
.L745:
	cmp	r3, #255
	beq	.L749
	mov	r2, r3
	mla	r3, lr, r3, r0
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, ip
	bne	.L745
	ldr	r0, .L750
	mov	ip, #48
	ldrb	r3, [r1]	@ zero_extendqisi2
	mla	r2, ip, r2, r0
	mov	r0, #1
	strb	r3, [r2, #1236]
	mvn	r3, #0
	strb	r3, [r1]
	ldr	pc, [sp], #4
.L749:
	mov	r0, #0
	ldr	pc, [sp], #4
.L751:
	.align	2
.L750:
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
	ldr	r3, .L758
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrb	r2, [r3, #2773]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	bne	.L753
	ldr	r1, .L758+4
	mov	r2, #172
	ldr	r0, .L758+8
	bl	printk
	bl	dump_stack
.L753:
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	ldr	r6, .L758
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L758+12
	mov	r1, r5
	bl	buf_remove_buf
	cmp	r0, #1
	ldreqb	r3, [r6, #2773]	@ zero_extendqisi2
	subeq	r3, r3, #1
	streqb	r3, [r6, #2773]
	ldreqb	r3, [r5, #2]	@ zero_extendqisi2
	orreq	r3, r3, #1
	streqb	r3, [r5, #2]
	ldmfd	sp!, {r4, r5, r6, pc}
.L759:
	.align	2
.L758:
	.word	.LANCHOR0
	.word	.LANCHOR1+1416
	.word	.LC0
	.word	.LANCHOR0+2772
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
	ldr	r4, .L764
	ldr	r0, .L764+4
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	bl	printk
	ldrb	r1, [r4, #2775]	@ zero_extendqisi2
	ldr	r0, .L764+8
	bl	printk
	ldrb	r1, [r4, #2776]	@ zero_extendqisi2
	ldr	r0, .L764+12
	bl	printk
	ldrb	r1, [r4, #2777]	@ zero_extendqisi2
	ldr	r0, .L764+16
	bl	printk
	ldrb	r1, [r4, #2772]	@ zero_extendqisi2
	ldr	r0, .L764+20
	bl	printk
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	ldr	r0, .L764+24
	bl	printk
	ldr	r4, .L764+28
	add	r5, r4, #1536
.L761:
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
	ldr	r0, .L764+32
	bl	printk
	cmp	r4, r5
	bne	.L761
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L765:
	.align	2
.L764:
	.word	.LANCHOR0
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	.LANCHOR0+1236
	.word	.LC50
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
	ldr	r3, .L767
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	ldrh	r3, [r3, #18]
	smulbb	r3, r2, r3
	ldr	r2, .L767+4
	ldr	r2, [r2, #1040]
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
.L768:
	.align	2
.L767:
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
	ldr	r3, .L771
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldrb	r2, [r3, #17]	@ zero_extendqisi2
	mov	r6, r0
	ldrh	r4, [r3, #18]
	mov	r1, r6
	ldr	r0, .L771+4
	smulbb	r4, r2, r4
	mov	r2, r5
	bl	printk
	ldr	r3, .L771+8
	mov	r2, r5, lsr #5
	mov	r1, #1
	and	r5, r5, #31
	ldr	r3, [r3, #1040]
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
.L772:
	.align	2
.L771:
	.word	.LANCHOR2
	.word	.LC51
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
	bne	.L774
	ldrb	r3, [r0, #1]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #2
.L774:
	ldrb	r3, [r0]	@ zero_extendqisi2
	and	r3, r3, #223
	cmp	r3, #88
	addeq	r0, r0, #1
	sub	r3, r0, #1
	mov	r0, #0
.L776:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L785
	sub	r1, r2, #48
	uxtb	ip, r1
	cmp	ip, #9
	addls	r0, r1, r0, asl #4
	bls	.L776
	sub	r1, r2, #97
	cmp	r1, #5
	subls	r2, r2, #87
	bls	.L784
	sub	r1, r2, #65
	cmp	r1, #5
	bxhi	lr
	sub	r2, r2, #55
.L784:
	add	r0, r2, r0, asl #4
	b	.L776
.L785:
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
	ldr	r3, .L788
	ldr	r0, .L788+4
	bl	proc_create_data
	add	sp, sp, #12
	@ sp needed
	ldr	pc, [sp], #4
.L789:
	.align	2
.L788:
	.word	.LANCHOR1+1432
	.word	.LC52
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
	ldr	r1, .L792
	ldr	r2, .L792+4
	bl	sprintf
	ldr	r1, .L792+8
	ldr	r6, .L792+12
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
	ldr	r1, .L792+16
	bl	sprintf
	ldr	r2, [r6, #1032]
	ldr	r1, .L792+20
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r6, #2780]
	ldr	r1, .L792+24
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L792+28
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r2, [r6, #2784]
	mov	r0, r4
	ldr	r1, .L792+32
	bl	sprintf
	ldr	r2, [r6, #1032]
	ldr	r1, .L792+36
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r1, .L792+40
	ldr	r2, [r3, #524]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r1, .L792+44
	ldr	r2, [r3, #528]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2788
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+48
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2790
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+52
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2792
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+56
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2794
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+60
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2796
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+64
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #2798
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+68
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrb	r1, [r6, #2801]	@ zero_extendqisi2
	ldrb	r2, [r6, #2773]	@ zero_extendqisi2
	ldrb	r3, [r6, #2800]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L792+72
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+76
	ldrh	r2, [r3, #148]
	ldrh	r3, [r3, #146]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+80
	ldr	r2, [r3, #16]
	ldr	r3, [r3, #20]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+84
	ldr	r2, [r3, #24]
	ldr	r3, [r3, #28]
	add	r2, r3, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+88
	ldr	r2, [r3, #64]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+92
	ldr	r2, [r3, #68]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r2, .L792+96
	ldr	r1, .L792+100
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
	ldr	r1, .L792+104
	mov	r0, r4
	ldr	r3, [r3, #156]
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r1, .L792+108
	ldr	r2, [r3, #556]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r1, .L792+112
	ldr	r2, [r3, #552]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+116
	ldr	r2, [r3, #52]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+120
	ldr	r2, [r3, #60]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+124
	ldr	r2, [r3, #76]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r6, #2804]
	ldr	r1, .L792+128
	ldr	r2, [r3, #8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1096]
	ldrh	r2, [r1, #16]
	ldrh	r3, [r1, #22]
	ldrb	r1, [r1, #25]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L792+132
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1096]
	ldrh	r2, [r1, #48]
	ldrh	r3, [r1, #54]
	ldrb	r1, [r1, #57]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L792+136
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r6, #1096]
	ldrh	r2, [r1, #80]
	ldrh	r3, [r1, #86]
	ldrb	r1, [r1, #89]	@ zero_extendqisi2
	add	r4, r4, r0
	str	r1, [sp]
	ldr	r1, .L792+140
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
	ldr	r1, .L792+144
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
	ldr	r1, .L792+148
	ldr	r3, [r3, #80]
	bl	sprintf
	movw	r3, #2808
	ldrh	r2, [r6, r3]
	ldr	r1, .L792+152
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
	ldr	r1, .L792+156
	bl	sprintf
	ldr	r1, [r6, #1096]
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
	ldr	r1, .L792+160
	bl	sprintf
	ldr	r3, [r6, #1096]
	ldr	r1, .L792+164
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
.L793:
	.align	2
.L792:
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
	ldr	r4, .L797
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
.L798:
	.align	2
.L797:
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
	ldr	r8, .L839
	mov	r4, r1
	mov	r9, r2
	ldr	r7, .L839+4
	ldr	r3, [r8]
	tst	r3, #256
	beq	.L803
	ldr	r3, [r7, #1084]
	mov	r1, r5
	ldr	r2, [r7, #1092]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r0, asl #1
	ldr	r0, .L839+8
	ldrh	r3, [r2, r3]
	mov	r2, r4
	str	r3, [sp, #4]
	ldr	r3, .L839+12
	ldrh	r3, [r3, #52]
	str	r3, [sp, #8]
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	str	r3, [sp, #12]
	mov	r3, r9
	bl	printk
.L803:
	movw	r3, #1080
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L804
	ldr	r1, .L839+16
	movw	r2, #543
	ldr	r0, .L839+20
	bl	printk
	bl	dump_stack
.L804:
	movw	r3, #1080
	ldr	r1, .L839+4
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L805
	ldr	r0, [r8]
	ands	r0, r0, #1024
	beq	.L806
	mov	r1, r5
	mov	r2, r4
	mov	r3, r9
	ldr	r0, .L839+24
	bl	printk
	b	.L825
.L805:
	ldr	r2, [r1, #1092]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	ldr	r2, [r1, #1084]
	add	r2, r2, r5, asl #2
	ldrb	ip, [r2, #2]	@ zero_extendqisi2
	ands	r6, ip, #224
	bne	.L807
	cmp	r3, #0
	beq	.L825
	ldr	r1, .L839+16
	movw	r2, #553
	ldr	r0, .L839+20
	bl	printk
	bl	dump_stack
	b	.L825
.L807:
	movw	r2, #2828
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	beq	.L825
	ldr	r2, [r1, #1096]
	ldrh	r0, [r2, #48]
	cmp	r0, r5
	beq	.L825
	ldrh	r0, [r2, #16]
	cmp	r0, r5
	beq	.L825
	ldrh	r0, [r2, #80]
	cmp	r0, r5
	beq	.L825
	ldr	r0, .L839+12
	add	r1, r1, #2880
	ldrh	lr, [r0, #52]
	mov	r0, #0
.L808:
	cmp	r0, lr
	bcs	.L838
	ldrh	r6, [r1, #2]!
	cmp	r6, r5
	beq	.L825
	add	r0, r0, #1
	b	.L808
.L838:
	cmp	r4, #0
	bne	.L813
	ldr	r0, .L839+28
	ldrh	r6, [r0]
	cmp	r6, r5
	moveq	r0, r4
	beq	.L806
	add	r0, r0, #4
	mov	r1, r4
.L812:
	ldrh	r10, [r1, r0]
	cmp	r5, r10
	bne	.L811
	ldr	r0, [r8]
	ands	r0, r0, #256
	beq	.L806
	stmia	sp, {r5, r6}
	mov	r1, r5
	mov	r2, ip, lsr #5
	ldr	r0, .L839+32
	bl	printk
	b	.L825
.L811:
	add	r1, r1, #2
	cmp	r1, #16
	bne	.L812
	add	r6, r2, r9, asl #7
	add	r6, r6, #136
	b	.L814
.L813:
	add	r6, r2, #392
.L814:
	ldr	r2, [r8]
	tst	r2, #256
	beq	.L815
	stmia	sp, {r3, lr}
	movw	r3, #2814
	ldrh	r3, [r7, r3]
	mov	r1, r5
	ldr	r0, .L839+36
	mov	r2, r4
	str	r3, [sp, #8]
	mov	r3, ip, lsr #5
	bl	printk
.L815:
	add	r1, r6, #128
	mov	r3, r6
	movw	r0, #65535
.L818:
	mov	r2, r3
	add	r3, r3, #2
	ldrh	ip, [r2]
	cmp	ip, r0
	bne	.L816
	cmp	r4, #0
	strh	r5, [r2]	@ movhi
	ldreq	r3, [r7, #1096]
	ldrne	r2, [r7, #1096]
	addeq	r9, r3, r9, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r3, [r9, #120]
	addne	r3, r3, #1
	addeq	r3, r3, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r3, [r9, #120]	@ movhi
	b	.L837
.L816:
	cmp	r3, r1
	bne	.L818
.L837:
	mov	r0, #1
	b	.L806
.L825:
	mov	r0, #0
.L806:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L840:
	.align	2
.L839:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+1476
	.word	.LC0
	.word	.LC91
	.word	.LANCHOR3-3152
	.word	.LC92
	.word	.LC93
	.fnend
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L847
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	sub	r2, r3, #3120
	ldr	r5, .L847+4
	mov	r6, #1
	ldrh	r1, [r2, #-12]
	mov	r7, r0
	add	r5, r5, #4096
	ldrb	r2, [r5, #-2943]	@ zero_extendqisi2
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
	ldr	r0, .L847+8
	bl	printk
	mov	r1, r6
	mov	r0, r8
	mov	r2, #0
	bl	gc_add_sblk
	ldr	r3, [r5, #920]
	ldr	r0, .L847+12
	mov	r1, #0
.L842:
	uxth	r2, r1
	cmp	r2, r3
	bcs	.L846
	add	r2, r2, #1088
	add	r1, r1, #1
	add	r2, r2, #8
	mov	r2, r2, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r4
	bne	.L842
	b	.L843
.L846:
	cmp	r3, #5
	bhi	.L843
	add	r2, r3, #1
	add	r3, r3, #1088
	add	r3, r3, #8
	str	r2, [r5, #920]
	ldr	r2, .L847+12
	mov	r3, r3, asl #1
	strh	r4, [r2, r3]	@ movhi
.L843:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L848:
	.align	2
.L847:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC94
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
	ldr	r3, .L850
	ldr	r3, [r3, #-3128]
	ldr	r0, [r3, r0, asl #2]
	bx	lr
.L851:
	.align	2
.L850:
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
	ldr	r5, .L883
	mov	r6, r5
.L853:
	ldrb	fp, [r5, #2776]	@ zero_extendqisi2
	cmp	fp, #255
	beq	.L882
	mov	r3, #48
	mul	r3, r3, fp
	add	r2, r5, r3
	ldrb	r1, [r2, #1236]	@ zero_extendqisi2
	strb	r1, [r5, #2776]
	ldr	r1, .L883+4
	add	r3, r1, r3
	ldr	r1, [r2, #1272]
	cmp	r1, #0
	ldrh	r8, [r3, #32]
	beq	.L854
	ldr	r3, .L883+8
	mov	r0, #1
	ldr	r2, [r2, #1260]
	strh	r0, [r3]	@ movhi
	ldr	r3, .L883+12
	ldr	r0, .L883+16
	str	r2, [r3, #916]
	bl	printk
	ldr	r1, .L883+20
	mov	r2, #956
	ldr	r0, .L883+24
	bl	printk
	bl	dump_stack
.L854:
	ldr	r7, .L883+28
	ldrb	r3, [r7, #-3124]	@ zero_extendqisi2
	mov	r10, r7
	cmp	r3, #3
	bne	.L855
	ldrb	r3, [r6, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L855
	ldr	r3, [r5, #1096]
	ldrb	r2, [r7, #-3123]	@ zero_extendqisi2
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	cmp	r2, r3
	movhi	r3, #1
	bhi	.L857
	cmp	r3, #3
	movcs	r3, #2
.L857:
	add	r4, r3, r3, asl #1
	b	.L858
.L855:
	ldrb	r3, [r7, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L858
	ldr	r3, .L883+28
	ldrb	r3, [r3, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
.L858:
	mov	r2, #48
	ldr	r1, [r7, #-3120]
	mla	r2, r2, fp, r6
	mov	r9, r8
	ldrb	r1, [r1, r8]	@ zero_extendqisi2
	ldrb	r2, [r2, #1237]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L859
	ldr	r1, .L883+20
	mov	r2, #976
	ldr	r0, .L883+24
	bl	printk
	bl	dump_stack
.L859:
	mov	r2, #48
	mla	fp, r2, fp, r6
	ldrb	r2, [fp, #1281]	@ zero_extendqisi2
	cmp	r2, #3
	beq	.L860
	ldrb	r1, [r7, #-3124]	@ zero_extendqisi2
	ldr	r2, .L883+28
	cmp	r1, #3
	bne	.L860
	ldrb	r1, [r2, #-3116]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L860
	ldrb	r1, [r5, #1160]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L860
	ldrb	r1, [r5, #1161]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L860
	ldrb	r1, [r2, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L861
	ldrb	r2, [r2, #-3121]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L860
.L861:
	ldr	r2, [r7, #-3120]
	mov	r3, #0
	mov	r1, #48
	mov	r0, r3
	add	ip, r2, r8
	b	.L862
.L860:
	ldr	r2, .L883+32
	mvn	r7, #0
	mov	fp, #48
	strh	r8, [r2]	@ movhi
	mov	r2, #0
.L863:
	rsb	r1, r8, r9
	uxth	r1, r1
	cmp	r1, r4
	bcs	.L853
	ldr	r1, [r10, #-3120]
	ldr	r3, .L883+4
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
	b	.L863
.L862:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L853
	ldrb	r2, [ip, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	mla	r2, r1, r2, r6
	strb	r0, [r2, #1281]
	b	.L862
.L882:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L884:
	.align	2
.L883:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+5010
	.word	.LANCHOR0+4096
	.word	.LC95
	.word	.LANCHOR1+1488
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
	ldr	r1, .L895
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r1, #1096]
	ldr	ip, .L895+4
	ldrh	r2, [r3, #124]
	ldrb	r0, [ip, #-3115]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r3, r3, #392
	movne	r2, #1
	bne	.L887
	add	lr, r3, r0, asl #1
	ldrh	lr, [lr, #120]
	cmp	lr, #0
	beq	.L892
	add	r3, r3, r0, asl #7
	add	r3, r3, #136
.L887:
	add	lr, r3, #128
	movw	r4, #65535
.L891:
	mov	r5, r3
	ldrh	r0, [r3], #2
	cmp	r0, r4
	beq	.L889
	cmp	r2, #0
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	ldreqb	r2, [ip, #-3115]	@ zero_extendqisi2
	ldreq	r3, [r1, #1096]
	ldrne	r2, [r1, #1096]
	addeq	r3, r3, r2, asl #1
	ldrneh	r3, [r2, #124]
	ldreqh	r2, [r3, #120]
	subne	r3, r3, #1
	subeq	r2, r2, #1
	strneh	r3, [r2, #124]	@ movhi
	streqh	r2, [r3, #120]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L889:
	cmp	r3, lr
	bne	.L891
	ldmfd	sp!, {r4, r5, pc}
.L892:
	movw	r0, #65535
	ldmfd	sp!, {r4, r5, pc}
.L896:
	.align	2
.L895:
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
	ldr	r5, .L912
	ldrb	r0, [r5, #2835]	@ zero_extendqisi2
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r5, #2773]	@ zero_extendqisi2
	cmp	r3, #1
	bhi	.L904
	ldr	r2, .L912+4
	movw	r3, #2104
	ldr	r1, .L912+8
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
.L899:
	cmp	r4, r3
	bcs	.L904
	ldrb	r2, [r0, r4]	@ zero_extendqisi2
	cmp	r2, #255
	beq	.L900
	mul	r2, r1, r2
	add	ip, r5, r2
	ldrb	ip, [ip, #1281]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L900
	ldr	r0, .L912+12
	add	r0, r0, r2
	bl	zbuf_free
	ldr	r3, .L912+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L901
	ldr	r3, [r6, #-3120]
	mov	r1, r4
	ldr	r0, .L912+20
	ldrb	r2, [r3, r4]	@ zero_extendqisi2
	bl	printk
.L901:
	ldr	r3, [r6, #-3120]
	mvn	r2, #0
	mov	r0, #1
	strb	r2, [r3, r4]
	ldrb	r3, [r5, #2835]	@ zero_extendqisi2
	add	r3, r3, r2
	strb	r3, [r5, #2835]
	ldmfd	sp!, {r4, r5, r6, pc}
.L900:
	add	r4, r4, #1
	b	.L899
.L904:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L913:
	.align	2
.L912:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR2
	.word	.LC96
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
	ldr	r1, .L917
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
.L918:
	.align	2
.L917:
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
	ldr	r0, .L921
	movw	ip, #2102
	add	r3, r0, r2
	ldrh	r1, [r0, r2]
	ldrh	r2, [r3, #2]
	ldrh	r3, [r3, ip]
	ldrb	ip, [r0, #2773]	@ zero_extendqisi2
	str	ip, [sp]
	ldrb	ip, [r0, #2835]	@ zero_extendqisi2
	str	ip, [sp, #4]
	movw	ip, #2808
	ldrh	r0, [r0, ip]
	str	r0, [sp, #8]
	ldr	r0, .L921+4
	bl	printk
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L922:
	.align	2
.L921:
	.word	.LANCHOR0
	.word	.LC97
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
	beq	.L929
	ldr	r3, .L931
	movw	ip, #65535
	mov	lr, #6
	ldr	r4, [r3, #1036]
.L925:
	cmp	r1, #0
	bne	.L926
.L928:
	rsb	r4, r4, r5
	ldr	r3, .L931+4
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
.L926:
	ldrh	r3, [r5]
	cmp	r3, ip
	beq	.L928
	sub	r1, r1, #1
	mla	r5, lr, r3, r4
	uxth	r1, r1
	b	.L925
.L929:
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, pc}
.L932:
	.align	2
.L931:
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
	beq	.L938
	ldr	r3, .L941
	mov	ip, #6
	ldr	r2, [r3, #1036]
	movw	r3, #65535
.L935:
	cmp	r1, #0
	beq	.L936
	ldrh	r0, [r0]
	cmp	r0, r3
	subne	r1, r1, #1
	mlane	r0, ip, r0, r2
	uxthne	r1, r1
	bne	.L935
.L940:
	bx	lr
.L938:
	movw	r0, #65535
	bx	lr
.L936:
	rsb	r0, r2, r0
	ldr	r3, .L941+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	uxth	r0, r0
	bx	lr
.L942:
	.align	2
.L941:
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
	ldr	r6, .L1066
	str	r0, [sp, #8]
	str	r1, [sp, #16]
	ldr	r7, [r6, #1096]
	add	r3, r7, r0, asl #1
	ldrh	r4, [r3, #120]
	cmp	r4, #0
	bne	.L998
	ldr	r3, .L1066+4
	mov	r5, r2
	ldrh	r2, [r3, #52]
	cmp	r2, #1
	ldrls	r1, .L1066+8
	strlsh	r4, [r1, #-8]	@ movhi
	strlsh	r4, [r1, #-6]	@ movhi
	strlsh	r4, [r1, #-4]	@ movhi
	ldr	r1, [sp, #8]
	cmp	r1, #0
	bne	.L1065
	ldr	r8, [sp, #8]
	ldr	r10, .L1066+12
	ldr	fp, .L1066+16
	mov	r4, r8
.L947:
	add	r9, r5, #1
	uxth	r3, r8
	cmp	r3, r9
	bge	.L953
	ldrh	r2, [r10]
	ldr	ip, .L1066+12
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
	beq	.L949
	ldr	r3, [fp]
	mov	ip, r0, asl #1
	tst	r3, #256
	beq	.L950
	ldr	r3, [r6, #1092]
	mov	r1, r7
	ldr	r0, .L1066+20
	str	ip, [sp, #12]
	ldrh	r3, [r3, ip]
	bl	printk
	ldr	ip, [sp, #12]
.L950:
	ldr	r3, [r6, #1092]
	ldrh	r2, [r3, ip]
	ldr	r3, .L1066+24
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L951
	mov	r1, #0
	mov	r0, r7
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L952
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	bcc	.L952
	b	.L953
.L951:
	ldr	r3, .L1066+8
	mov	r2, #0
	strh	r2, [r3, #-6]	@ movhi
	b	.L953
.L949:
	mov	r3, #0
	strh	r3, [ip]	@ movhi
	b	.L953
.L952:
	add	r8, r8, #1
	b	.L947
.L953:
	ldr	r3, [sp, #16]
	tst	r3, #2
	beq	.L955
	movw	r3, #2798
	ldrh	r3, [r6, r3]
	cmp	r3, #32
	movhi	r10, #0
	ldrhi	r8, .L1066+28
	bls	.L955
.L956:
	uxth	r2, r10
	ldr	r7, .L1066+32
	cmp	r2, r9
	bge	.L960
	ldrh	fp, [r8]
	ldr	r2, .L1066+28
	mov	r1, fp
	add	r0, r2, #8
	str	r2, [sp, #12]
	bl	_list_get_gc_head_node
	movw	ip, #65535
	add	fp, fp, #1
	strh	fp, [r8]	@ movhi
	cmp	r0, ip
	ldr	r2, [sp, #12]
	beq	.L957
	ldr	fp, [r6, #1092]
	mov	r1, r0, asl #1
	ldr	r3, .L1066+32
	ldrh	ip, [fp, r1]
	ldrh	r1, [r3]
	cmp	ip, r1
	bcs	.L957
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L959
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	bcc	.L959
	b	.L960
.L957:
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L960
.L959:
	add	r10, r10, #1
	b	.L956
.L960:
	cmp	r4, r5
	ldr	r2, .L1066+36
	bcs	.L962
	ldrh	ip, [r2, #-26]
	ldrb	r3, [r2, #-35]	@ zero_extendqisi2
	ldrh	r0, [r2, #-8]
	ldrh	r1, [r7]
	mul	r3, r3, ip
	sub	r3, r3, r0, lsr #2
	cmp	r1, r3
	ldrlt	r3, .L1066+32
	addlt	r1, r1, r0, lsr #3
	strlth	r1, [r3]	@ movhi
	b	.L955
.L962:
	ldrh	r2, [r2, #-8]
	ldrh	r3, [r7]
	cmp	r3, r2
	subhi	r3, r3, r2, lsr #3
	ldrhi	r2, .L1066+32
	strhih	r3, [r2]	@ movhi
.L955:
	ldr	r3, [sp, #16]
	tst	r3, #1
	beq	.L963
	uxth	r7, r5
	cmp	r4, r7
	bcs	.L963
	ldr	r8, .L1066+40
	mov	fp, #0
	movw	r9, #65535
.L968:
	ldrh	r10, [r8]
	ldr	r3, .L1066+40
	mov	r1, r10
	add	r0, r3, #20
	str	r3, [sp, #12]
	bl	_list_get_gc_head_node
	add	r10, r10, #1
	strh	r10, [r8]	@ movhi
	cmp	r0, r9
	ldr	r3, [sp, #12]
	beq	.L964
	mov	r1, #0
	mov	r2, r1
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L965
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r7
	bcc	.L965
	b	.L966
.L964:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L967
.L965:
	add	fp, fp, #1
	uxth	r3, fp
	cmp	r3, r7
	bcc	.L968
.L967:
	cmp	r4, r7
	bcs	.L966
	ldr	r1, .L1066+36
	movw	r2, #2814
	ldrh	r3, [r6, r2]
	ldrh	r1, [r1, #-8]
	cmp	r3, r1
	addcc	r3, r3, r1, lsr #3
	bcc	.L1064
	b	.L963
.L966:
	ldr	r1, .L1066+36
	movw	r2, #2814
	ldrh	r3, [r6, r2]
	ldrh	r1, [r1, #-8]
	cmp	r3, r1, lsr #1
	bls	.L963
	sub	r3, r3, r1, lsr #3
.L1064:
	ldr	r1, .L1066
	b	.L1063
.L1065:
	ldr	r8, .L1066+44
	cmp	r5, #1
	sub	r8, r8, #3104
	ldrb	r0, [r8, #-19]	@ zero_extendqisi2
	ldrh	r1, [r8, #-10]
	smulbb	r1, r0, r1
	uxth	r1, r1
	str	r1, [sp, #12]
	bne	.L969
	cmp	r2, #0
	beq	.L969
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
.L969:
	movw	r0, #2180
	mov	r7, #0
	strh	r7, [r3, r0]	@ movhi
	ldr	r3, [sp, #16]
	ldr	r4, .L1066+36
	ands	r3, r3, #1
	str	r3, [sp, #20]
	ldr	r1, .L1066
	strh	r7, [r4]	@ movhi
	ldreq	r7, [sp, #20]
	moveq	r4, r7
	beq	.L970
	ldrh	r0, [r4, #2]
	movw	r3, #2794
	ldrh	r3, [r1, r3]
	add	r4, r4, #16
	str	r4, [sp, #24]
	cmp	r3, r0, lsr #2
	bhi	.L971
	movw	r0, #2796
	ldrh	r1, [r1, r0]
	cmp	r1, r3
	movcs	r4, r7
	bcs	.L972
.L971:
	cmp	r2, #1
	bls	.L973
.L975:
	mov	r4, #0
	b	.L974
.L973:
	ldr	r4, .L1066+8
	mov	r1, #0
	mov	r3, r3, lsr #2
	strh	r3, [r8, #-8]	@ movhi
	add	r0, r4, #12
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L975
	ldr	r1, [r6, #1092]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, #8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #2
	bcs	.L975
	mov	r1, #0
	ldr	r2, [sp, #8]
	mov	r3, #1
	strh	r3, [r4, #-8]	@ movhi
	bl	gc_add_sblk
	adds	r4, r0, #0
	movne	r4, #1
.L974:
	ldr	r9, .L1066+40
	mov	r8, #64
	mov	r7, #0
.L978:
	ldrh	fp, [r9]
	ldr	r3, .L1066+40
	mov	r1, fp
	add	r0, r3, #20
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	mov	r10, r0
	ldr	r3, [sp, #28]
	beq	.L976
	mov	r1, #0
	ldr	r2, [sp, #8]
	add	fp, fp, #1
	strh	fp, [r9]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L977
	ldr	r3, [r6, #1092]
	mov	r10, r10, asl #1
	add	r4, r4, #1
	ldrh	r3, [r3, r10]
	uxth	r4, r4
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcs	.L972
	ldr	r2, .L1066+48
	ldr	r3, .L1066+52
	ldrh	r2, [r2]
	ldrh	r3, [r3]
	cmp	r3, r2, asl #1
	ble	.L977
	b	.L972
.L976:
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L972
.L977:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L978
.L972:
	movw	r3, #2796
	ldrh	r2, [r6, r3]
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #-14]
	cmp	r2, r3, lsr #2
	bhi	.L999
	ldr	r1, .L1066
	movw	r3, #2794
	ldrh	r3, [r1, r3]
	add	r3, r3, #8
	cmp	r2, r3
	bgt	.L999
.L970:
	ldr	r3, [sp, #16]
	tst	r3, #2
	beq	.L982
	movw	r3, #2798
	ldrh	r2, [r6, r3]
	ldr	r3, [sp, #12]
	cmp	r7, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r2, #32
	movls	r3, #0
	cmp	r3, #0
	beq	.L982
	ldr	r9, .L1066+28
	mov	r8, #64
	mov	r6, #0
.L988:
	ldrh	r3, [r9]
	ldr	r10, .L1066+28
	ldr	fp, .L1066
	add	r0, r10, #8
	mov	r1, r3
	str	r3, [sp, #24]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L983
	ldr	r3, [sp, #24]
	cmp	r5, #1
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	bne	.L984
	ldr	r3, .L1066+44
	ldrb	r2, [r3, #-3123]	@ zero_extendqisi2
	ldrh	r3, [r10, #-6]
	smulbb	r3, r2, r3
	ldrh	r2, [r10, #12]
	sub	r3, r3, r2, lsr #3
	ldr	r2, .L1066+56
	strh	r3, [r2]	@ movhi
.L984:
	ldr	r3, [fp, #1092]
	mov	r10, r0, asl #1
	ldrh	r2, [r3, r10]
	ldr	r3, .L1066+56
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L985
	mov	r1, #0
	ldr	r2, [sp, #8]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L986
	ldr	r3, .L1066
	add	r4, r4, #1
	add	r6, r6, #1
	uxth	r4, r4
	ldr	r3, [r3, #1092]
	uxth	r6, r6
	ldrh	r3, [r3, r10]
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcc	.L986
	b	.L987
.L985:
	ldr	r3, .L1066+8
	mov	r2, #0
	strh	r2, [r3, #-4]	@ movhi
	b	.L987
.L983:
	mov	r3, #0
	strh	r3, [r10]	@ movhi
	b	.L987
.L986:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L988
.L987:
	ldr	r0, .L1066+44
	cmp	r4, r5
	sub	r2, r0, #3088
	bcc	.L989
	cmp	r6, #0
	bne	.L990
	ldr	ip, .L1066
	sub	r3, r0, #3072
	movw	r1, #2798
	ldrh	r3, [r3, #-12]
	ldrh	r1, [ip, r1]
	cmp	r1, r3
	bls	.L990
.L989:
	ldr	r3, .L1066+8
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
	ldrlt	r2, .L1066
	strlth	r3, [r2, r1]	@ movhi
	b	.L982
.L990:
	ldrh	ip, [r2, #-8]
	movw	r1, #2810
	ldrb	r2, [r0, #-3123]	@ zero_extendqisi2
	ldrh	r3, [fp, r1]
	mul	r2, r2, ip
	cmp	r3, r2
	subgt	r3, r3, ip, lsr #3
	bgt	.L1062
	b	.L963
.L982:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L963
	ldr	r3, [sp, #12]
	uxth	r9, r5
	cmp	r4, r9
	cmpcc	r7, r3
	bcs	.L963
	ldr	r10, .L1066
	mov	r6, #64
.L996:
	ldr	r2, .L1066+12
	ldr	fp, .L1066
	add	r0, r2, #6
	ldrh	r8, [r2]
	str	r2, [sp, #20]
	mov	r1, r8
	bl	_list_get_gc_head_node
	movw	r1, #65535
	cmp	r0, r1
	ldr	r2, [sp, #20]
	beq	.L992
	ldr	r3, .L1066+12
	add	r8, r8, #1
	ldr	r2, [r10, #1092]
	strh	r8, [r3]	@ movhi
	mov	r8, r0, asl #1
	ldrh	r1, [r2, r8]
	ldr	r2, .L1066+60
	ldrh	r2, [r2]
	cmp	r1, r2
	bcs	.L993
	ldr	r2, .L1066+52
	ldrh	r1, [r3, #24]
	ldrh	r2, [r2]
	cmp	r2, r1, lsr #1
	bls	.L994
.L993:
	mov	r1, #0
	ldr	r2, [sp, #8]
	bl	gc_add_sblk
	cmp	r0, #0
	beq	.L995
	ldr	r2, [r10, #1092]
	add	r4, r4, #1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	ldrh	r2, [r2, r8]
	add	r7, r7, r2
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r9
	bcc	.L995
	b	.L994
.L992:
	cmp	r8, #64
	movhi	r3, #0
	strhih	r3, [r2]	@ movhi
	b	.L994
.L995:
	sub	r6, r6, #1
	uxth	r6, r6
	cmp	r6, #0
	bne	.L996
.L994:
	cmp	r4, r9
	ldr	r0, .L1066+36
	movw	r1, #2812
	bcs	.L997
	ldrh	r2, [r0, #-8]
	ldrh	r3, [fp, r1]
	cmp	r3, r2, lsr #1
	bls	.L963
	sub	r3, r3, r2, lsr #3
.L1062:
	ldr	r2, .L1066
.L1063:
	strh	r3, [r2, r1]	@ movhi
	b	.L963
.L997:
	ldr	r3, .L1066+44
	ldrh	r0, [r0, #-8]
	ldrh	r2, [fp, r1]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	mul	r3, r3, r0
	sub	r3, r3, #32
	cmp	r2, r3
	addlt	r2, r2, r0, lsr #3
	ldrlt	r3, .L1066
	strlth	r2, [r3, r1]	@ movhi
.L963:
	ldr	r3, .L1066+16
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L998
	str	r5, [sp]
	mov	r3, r4
	ldr	r0, .L1066+64
	ldr	r1, [sp, #8]
	ldr	r2, [sp, #16]
	bl	printk
.L998:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L999:
	ldr	r3, [sp, #12]
	cmp	r7, r3
	bcs	.L970
	ldr	r9, .L1066+12
	mov	r8, #64
	movw	r10, #65535
.L981:
	ldrh	fp, [r9]
	ldr	r2, .L1066+12
	mov	r1, fp
	add	r0, r2, #6
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	cmp	r0, r10
	mov	r3, r0
	str	r0, [sp, #24]
	ldr	r2, [sp, #28]
	beq	.L979
	mov	r1, #0
	ldr	r2, [sp, #8]
	add	fp, fp, #1
	strh	fp, [r9]	@ movhi
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	r3, [sp, #24]
	beq	.L980
	ldr	r2, [r6, #1092]
	mov	r3, r3, asl #1
	add	r4, r4, #1
	ldrh	r3, [r2, r3]
	uxth	r4, r4
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	cmpls	r4, r5
	bcc	.L980
	b	.L970
.L979:
	cmp	fp, #64
	movhi	r3, #0
	strhih	r3, [r2]	@ movhi
	b	.L970
.L980:
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L981
	b	.L970
.L1067:
	.align	2
.L1066:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR3-3104
	.word	.LANCHOR3-3110
	.word	.LANCHOR2
	.word	.LC98
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
	.word	.LC99
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
	ldreq	r0, .L1072
	beq	.L1071
	cmp	r1, #2
	ldreq	r0, .L1072+4
	ldrne	r0, .L1072+8
.L1071:
	mov	r1, r3
	b	_list_get_gc_head_node
.L1073:
	.align	2
.L1072:
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
	ldr	r3, .L1078
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1078+4
	ldreq	r2, .L1078+8
	beq	.L1077
.L1075:
	cmp	r3, #16
	ldreq	r0, .L1078+12
	ldreq	r2, .L1078+16
	ldrne	r0, .L1078+20
	ldrne	r2, .L1078+24
.L1077:
	b	_insert_free_list
.L1079:
	.align	2
.L1078:
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
	ldr	r3, .L1085
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1085+4
	ldreq	r2, .L1085+8
	beq	.L1084
.L1081:
	cmp	r3, #96
	ldreq	r0, .L1085+12
	ldreq	r2, .L1085+16
	beq	.L1084
.L1082:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1085+20
	ldr	r2, .L1085+24
.L1084:
	b	_insert_data_list
.L1086:
	.align	2
.L1085:
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
	ldr	r8, .L1102
	mov	r7, r0
	ldr	r3, [r8, #1096]
	add	r3, r3, #588
	ldrh	r4, [r3]
	clz	r3, r0
	cmp	r4, r2
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L1088
	ldr	r0, .L1102+4
	mov	r1, r4
	bl	printk
	ldr	r3, [r8, #1096]
	mvn	r2, #0
	add	r3, r3, #588
	strh	r2, [r3]	@ movhi
	b	.L1089
.L1088:
	movw	r3, #2790
	mov	r6, r1
	ldrh	r1, [r8, r3]
	movw	r3, #2792
	ldrh	r3, [r8, r3]
	mov	r5, r8
	cmp	r1, r3
	bcc	.L1090
	movw	r2, #2788
	ldrh	r2, [r8, r2]
	cmp	r3, #0
	cmpne	r2, r1
	bls	.L1091
.L1090:
	cmp	r7, #0
	ldr	r0, .L1102+8
	rsbne	r1, r1, r1, asl #3
	moveq	r1, r3, lsr #2
	ubfxne	r1, r1, #3, #16
	ldr	r2, .L1102+12
	b	.L1101
.L1091:
	cmp	r0, #0
	ldr	r2, .L1102+16
	movne	r1, r1, lsr #3
	moveq	r1, r0
	ldr	r0, .L1102+20
.L1101:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1096
	movw	r3, #2788
	ldr	r0, .L1102+24
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r6
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1102+28
	ldr	r3, [r3, #-3080]
	bl	printk
.L1096:
	cmp	r7, #0
	beq	.L1089
	ldr	r3, .L1102+32
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1089
	ldr	r2, [r5, #1084]
	mov	r0, r4, asl #2
	add	r3, r2, r0
	ldr	r1, [r2, r4, asl #2]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #11, #8
	str	r1, [sp]
	ldrh	r2, [r2, r0]
	ldr	r0, .L1102+36
	ubfx	r2, r2, #0, #11
	str	r2, [sp, #4]
	ldr	r1, [r5, #1092]
	mov	r2, r4, asl #1
	ldrh	r2, [r1, r2]
	mov	r1, r4
	str	r2, [sp, #8]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
.L1089:
	mov	r0, r4
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1103:
	.align	2
.L1102:
	.word	.LANCHOR0
	.word	.LC100
	.word	.LANCHOR3-3072
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+2790
	.word	.LANCHOR3-3076
	.word	.LC101
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC102
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
	ldr	r5, .L1120
	bne	.L1105
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	movw	r3, #2792
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	bcc	.L1106
	movw	r3, #2788
	ldrh	r3, [r5, r3]
	cmp	r2, #0
	cmpne	r3, r1
	movhi	r1, #1
	movls	r1, #0
	bls	.L1107
.L1106:
	ldr	r0, .L1120+4
	mov	r1, r2, lsr #1
	b	.L1119
.L1107:
	ldr	r0, .L1120+8
	ldr	r2, .L1120+12
	b	.L1118
.L1105:
	ldr	r3, [r5, #1096]
	movw	r8, #590
	ldrh	r4, [r3, r8]
	movw	r3, #65535
	cmp	r1, #1
	cmpne	r4, r3
	beq	.L1109
	ldr	r0, .L1120+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1096]
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L1110
.L1109:
	movw	r3, #2788
	movw	r2, #2792
	ldrh	r3, [r5, r3]
	mov	r6, r0
	ldrh	r2, [r5, r2]
	cmp	r3, r2
	bcc	.L1111
	movw	r1, #2790
	ldrh	r1, [r5, r1]
	cmp	r2, #0
	cmpne	r1, r3
	bls	.L1112
.L1111:
	bl	get_ink_scaned_blk
	movw	r3, #65535
	cmp	r0, r3
	movne	r4, r0
	bne	.L1110
	cmp	r7, #1
	ldr	r0, .L1120+4
	movweq	r3, #2792
	ldreqh	r6, [r5, r3]
	moveq	r6, r6, lsr #1
	mov	r1, r6
.L1119:
	ldr	r2, .L1120+20
	b	.L1118
.L1112:
	cmp	r7, #1
	ldr	r0, .L1120+24
	moveq	r6, r3, lsr #1
	ldr	r2, .L1120+28
	mov	r1, r6
.L1118:
	bl	_list_pop_index_node
	movw	r3, #65535
	uxth	r4, r0
	cmp	r4, r3
	bne	.L1110
	movw	r3, #2788
	ldr	r0, .L1120+32
	ldrh	r3, [r5, r3]
	mov	r1, r4
	mov	r2, r7
	str	r3, [sp]
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	str	r3, [sp, #4]
	ldr	r3, .L1120+36
	ldr	r3, [r3, #-3080]
	bl	printk
.L1110:
	mov	r0, r4
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1121:
	.align	2
.L1120:
	.word	.LANCHOR0
	.word	.LANCHOR3-3072
	.word	.LANCHOR3-3076
	.word	.LANCHOR0+2790
	.word	.LC103
	.word	.LANCHOR0+2792
	.word	.LANCHOR3-3080
	.word	.LANCHOR0+2788
	.word	.LC101
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
	ldr	r3, .L1127
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1127+4
	ldreq	r2, .L1127+8
	beq	.L1126
.L1123:
	cmp	r3, #96
	ldreq	r0, .L1127+12
	ldreq	r2, .L1127+16
	beq	.L1126
.L1124:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1127+20
	ldr	r2, .L1127+24
.L1126:
	b	_list_remove_node
.L1128:
	.align	2
.L1127:
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
	ldr	r3, .L1133
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1133+4
	ldreq	r2, .L1133+8
	beq	.L1132
.L1130:
	cmp	r3, #16
	ldreq	r0, .L1133+12
	ldreq	r2, .L1133+16
	ldrne	r0, .L1133+20
	ldrne	r2, .L1133+24
.L1132:
	b	_list_remove_node
.L1134:
	.align	2
.L1133:
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
	ldr	r3, .L1140
	mov	r1, r0
	ldr	r3, [r3, #1084]
	add	r3, r3, r0, asl #2
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #64
	ldreq	r0, .L1140+4
	ldreq	r2, .L1140+8
	beq	.L1139
.L1136:
	cmp	r3, #96
	ldreq	r0, .L1140+12
	ldreq	r2, .L1140+16
	beq	.L1139
.L1137:
	cmp	r3, #160
	bxne	lr
	ldr	r0, .L1140+20
	ldr	r2, .L1140+24
.L1139:
	b	_list_update_data_list
.L1141:
	.align	2
.L1140:
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
	ldr	r0, .L1147
	bl	printk
	ldr	r4, [r4]
	cmp	r4, #0
	beq	.L1142
	ldr	r6, .L1147+4
	mov	r5, #0
	ldr	r7, .L1147+8
.L1145:
	ldr	r2, [r6, #1036]
	ldrh	r0, [r4, #2]
	rsb	r2, r2, r4
	ldr	r1, [r6, #1084]
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
	ldr	r0, [r6, #1092]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #20]
	mov	r1, r2, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1147+12
	str	r1, [sp, #24]
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1142
	ldr	r3, [r6, #1036]
	mov	r4, #6
	add	r5, r5, #1
	mla	r4, r4, r2, r3
	ldr	r3, .L1147+16
	uxth	r5, r5
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L1145
.L1142:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1148:
	.align	2
.L1147:
	.word	.LC104
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC105
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
	ldr	r4, .L1151
	ldr	r5, .L1151+4
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
.L1152:
	.align	2
.L1151:
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
	ldr	r3, .L1158
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
.L1159:
	.align	2
.L1158:
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
	ldr	r8, .L1170
	mov	r10, r0
	ldr	r9, .L1170+4
	mov	r6, r1
	ldr	r2, .L1170+8
	mov	r7, r5
	ldr	r4, [r8, #1084]
	add	r4, r4, r0, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
.L1161:
	ldrb	r1, [r9, #-3123]	@ zero_extendqisi2
	cmp	r7, r1
	ldr	r1, .L1170+4
	bge	.L1168
	mov	r1, r3, asr r7
	tst	r1, #1
	bne	.L1162
	ldrb	r4, [r9, #-3130]	@ zero_extendqisi2
	mov	r0, r7
	str	r2, [sp, #4]
	mov	fp, r5, asl #1
	str	r3, [sp]
	add	r5, r5, #1
	mov	r1, r4
	bl	__aeabi_idiv
	smulbb	r4, r10, r4
	ldrb	r1, [r8, #1153]	@ zero_extendqisi2
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
.L1162:
	add	r7, r7, #1
	b	.L1161
.L1168:
	add	r6, r6, r5, asl #1
	mov	r0, r5
	mvn	r2, #0
.L1165:
	ldrb	r3, [r1, #-3123]	@ zero_extendqisi2
	cmp	r5, r3
	strlth	r2, [r6], #2	@ movhi
	addlt	r5, r5, #1
	blt	.L1165
.L1169:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1171:
	.align	2
.L1170:
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
	ldr	r3, .L1178
	ldrb	r4, [r3, #1153]	@ zero_extendqisi2
	ldr	r3, .L1178+4
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
	beq	.L1173
	ldrb	r3, [r3, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1173
	ldrh	r2, [r7, #-10]
	clz	r1, r1
	mov	r0, r5
	mov	r1, r1, lsr #5
	mul	r2, r2, r4
	bl	flash_erase_block_en
.L1173:
	ldrh	r2, [r7, #-10]
	mov	r0, r5
	uxtb	r1, r6
	mul	r2, r2, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	flash_erase_block_en
.L1179:
	.align	2
.L1178:
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
	ldr	r4, .L1208
	.pad #84
	sub	sp, sp, #84
	ldr	r9, .L1208+4
	mov	r8, r0
	mov	r7, r1
	mov	r6, #0
	ldr	r3, [r4, #1084]
	add	r3, r3, r10
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L1181:
	ldrb	r3, [r9, #-3064]	@ zero_extendqisi2
	cmp	r6, r3
	bge	.L1192
	ldrb	r2, [r9, #-3130]	@ zero_extendqisi2
	mov	r5, #0
	sub	fp, r2, #1
	mul	r3, r2, r8
	mul	ip, r2, r6
	str	r3, [sp, #4]
	ldr	r3, .L1208+8
	ldrh	r3, [r3]
	str	r3, [sp, #8]
	mov	r3, r5
.L1193:
	cmp	r3, r2
	bge	.L1207
	ldr	lr, [sp, #12]
	add	r1, r3, ip
	mov	r1, lr, asr r1
	tst	r1, #1
	bne	.L1182
	add	r1, sp, #80
	ldr	r0, [sp, #4]
	add	lr, r1, r5, asl #2
	and	r1, r3, fp
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	add	r5, r5, #1
	mul	r1, r0, r1
	str	r1, [lr, #-64]
.L1182:
	add	r3, r3, #1
	b	.L1193
.L1207:
	cmp	r2, #4
	bne	.L1205
	uxtb	r3, r6
	uxtb	ip, r7
	mov	fp, #0
.L1184:
	cmp	fp, r5
	beq	.L1187
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
	b	.L1184
.L1205:
	cmp	r5, #2
	bne	.L1188
	ldr	r3, .L1208+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1189
	ldrb	r3, [r9, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1189
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
.L1189:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bl	flash_erase_duplane_block
	b	.L1187
.L1188:
	cmp	r5, #1
	bne	.L1187
	ldr	r3, .L1208+4
	uxtb	r5, r6
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1191
	ldrb	r3, [r9, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1191
	clz	r1, r7
	mov	r0, r5
	ldr	r2, [sp, #16]
	mov	r1, r1, lsr #5
	bl	flash_erase_block_en
.L1191:
	mov	r0, r5
	uxtb	r1, r7
	ldr	r2, [sp, #16]
	bl	flash_erase_block_en
.L1187:
	add	r6, r6, #1
	b	.L1181
.L1192:
	cmp	r7, #0
	ldr	r1, [r4, #1084]
	bne	.L1194
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
	ldr	r2, [r4, #1084]
	ldrh	r2, [r2, r10]
	ubfx	r2, r2, #0, #11
	uxth	r1, r2
	cmp	r0, r1
	strlth	r2, [r3, #96]	@ movhi
	b	.L1196
.L1194:
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
	ldr	r2, [r4, #1084]
	ldr	r2, [r2, r8, asl #2]
	ubfx	r2, r2, #11, #8
	cmp	r1, r2
	strcch	r2, [r3, #98]	@ movhi
.L1196:
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1209:
	.align	2
.L1208:
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
	ldr	r3, .L1221
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
	bne	.L1212
	ldr	r1, .L1221+4
	movw	r2, #1359
	ldr	r0, .L1221+8
	bl	printk
	bl	dump_stack
.L1212:
	ldr	r3, [r4, #2804]
	movw	lr, #65535
	mov	ip, #0
.L1216:
	ldrh	r2, [r3, #136]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1213:
	cmp	r2, #63
	bgt	.L1220
	ldrh	r0, [r1, #2]!
	cmp	r0, lr
	bne	.L1218
	add	r2, r2, #1
	b	.L1213
.L1220:
	strh	ip, [r3, #136]	@ movhi
	b	.L1216
.L1218:
	add	r1, r3, r2, asl #1
	mvn	ip, #0
	strh	ip, [r1, #160]	@ movhi
	strh	r2, [r3, #136]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, ip
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1222:
	.align	2
.L1221:
	.word	.LANCHOR0
	.word	.LANCHOR1+1508
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
	ldr	r3, .L1233
	ldr	r2, [r3, #2804]
	mov	r4, r3
	ldrh	r1, [r2, #138]
	cmp	r1, #63
	movhi	r3, #0
	strhih	r3, [r2, #138]	@ movhi
	ldrh	r3, [r2, #112]
	cmp	r3, #63
	bls	.L1225
	ldr	r1, .L1233+4
	movw	r2, #1386
	ldr	r0, .L1233+8
	bl	printk
	bl	dump_stack
.L1225:
	ldr	r3, [r4, #2804]
	movw	ip, #65535
	mov	r0, #0
.L1229:
	ldrh	r2, [r3, #138]
	add	r1, r2, #79
	add	r1, r3, r1, asl #1
.L1226:
	cmp	r2, #63
	bgt	.L1232
	ldrh	lr, [r1, #2]!
	cmp	lr, ip
	bne	.L1227
	add	r1, r3, r2, asl #1
	strh	r5, [r1, #160]	@ movhi
	strh	r2, [r3, #138]	@ movhi
	ldrh	r2, [r3, #112]
	add	r2, r2, #1
	strh	r2, [r3, #112]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1227:
	add	r2, r2, #1
	b	.L1226
.L1232:
	strh	r0, [r3, #138]	@ movhi
	b	.L1229
.L1234:
	.align	2
.L1233:
	.word	.LANCHOR0
	.word	.LANCHOR1+1528
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
	ldr	r4, .L1245
	mov	r8, r6, asl #2
	ldr	r5, [r4, #1084]
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
	bne	.L1240
	tst	r3, #24
	ldreqh	r3, [r2, #114]
	ldrneh	r3, [r2, #118]
	subeq	r3, r3, #1
	subne	r3, r3, #1
	streqh	r3, [r2, #114]	@ movhi
	strneh	r3, [r2, #118]	@ movhi
.L1240:
	ldrb	r3, [r7, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	bne	.L1242
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
.L1242:
	ldrh	r2, [r5, r8]
	cmp	r3, #64
	ubfx	r1, r2, #0, #11
	add	r1, r1, #1
	bfi	r2, r1, #0, #11
	strh	r2, [r5, r8]	@ movhi
	bne	.L1243
	ldr	r2, [r4, #2804]
	ldrh	r3, [r2, #122]
	sub	r3, r3, #1
	strh	r3, [r2, #122]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1243:
	cmp	r3, #96
	ldreq	r2, [r4, #2804]
	ldreqh	r3, [r2, #124]
	subeq	r3, r3, #1
	streqh	r3, [r2, #124]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1246:
	.align	2
.L1245:
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
	ldr	r3, .L1252
	ldr	r2, .L1252+4
	ldr	r5, [r3, #1096]
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
	bne	.L1250
	ldr	r1, .L1252+8
	movw	r2, #1945
	ldr	r0, .L1252+12
	bl	printk
	bl	dump_stack
.L1250:
	ldr	r3, .L1252+16
	ldrh	r0, [r3, #-10]
	mla	r0, r0, r4, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1253:
	.align	2
.L1252:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+1548
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
	ldr	r2, .L1260
	ldr	r1, .L1260+4
	mov	r3, r3, asl #1
	str	lr, [sp, #-4]!
	.save {lr}
	ldrh	r3, [r1, r3]
	ldr	ip, [r2, #-2544]
	ldr	r1, [r2, #-2548]
	movw	r2, #65535
.L1255:
	cmp	r3, r2
	beq	.L1259
	ldr	lr, [r1, r3, asl #2]
	cmp	lr, r0
	bne	.L1256
	mov	r0, r3
	ldr	lr, [sp], #4
	b	ftl_get_ppa_from_index
.L1256:
	mov	r3, r3, asl #1
	ldrh	r3, [ip, r3]
	b	.L1255
.L1259:
	mvn	r0, #0
	ldr	pc, [sp], #4
.L1261:
	.align	2
.L1260:
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
	bne	.L1263
	ldr	r1, .L1272
	movw	r2, #2088
	ldr	r0, .L1272+4
	bl	printk
	bl	dump_stack
.L1263:
	ldr	r3, .L1272+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-8]
	cmp	r2, r3
	bne	.L1264
	ldr	r1, .L1272
	movw	r2, #2089
	ldr	r0, .L1272+4
	bl	printk
	bl	dump_stack
.L1264:
	ldrh	r3, [r4, #6]
	cmp	r3, #0
	bne	.L1265
	ldr	r1, .L1272
	movw	r2, #2090
	ldr	r0, .L1272+4
	bl	printk
	bl	dump_stack
.L1265:
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	movw	r2, #65535
	ldr	r1, .L1272+12
	mov	lr, #0
	add	r3, r4, r3, asl #1
	ldrb	r0, [r1, #-3123]	@ zero_extendqisi2
	ldrh	ip, [r3, #16]
.L1266:
	cmp	ip, r2
	ldrb	r3, [r4, #5]	@ zero_extendqisi2
	bne	.L1271
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
	b	.L1266
.L1271:
	ldr	r0, .L1272+16
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
.L1273:
	.align	2
.L1272:
	.word	.LANCHOR1+1572
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
	bls	.L1275
	ldr	r1, .L1277
	movw	r2, #2125
	ldr	r0, .L1277+4
	bl	printk
	bl	dump_stack
.L1275:
	ldr	r5, .L1277+8
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
.L1278:
	.align	2
.L1277:
	.word	.LANCHOR1+1596
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
	ldr	r2, .L1292
	ldrh	r3, [r2, #-12]
	cmp	r3, #4
	cmpls	r0, #0
	addeq	r3, r3, #1
	streqh	r3, [r2, #-12]	@ movhi
	bxeq	lr
	ldr	r3, .L1292+4
	movw	ip, #1080
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #0
	ldrh	lr, [r3, ip]
	movw	r6, #65535
	strh	r0, [r2, #-12]	@ movhi
	mov	ip, r0
	ldr	r1, [r3, #1092]
	ldr	r2, [r3, #1084]
	sub	r1, r1, #2
	add	lr, r2, lr, asl #2
.L1282:
	cmp	r2, lr
	beq	.L1291
	ldrh	r4, [r1, #2]!
	cmp	r4, r6
	beq	.L1283
	ldrb	r5, [r2, #2]	@ zero_extendqisi2
	and	r5, r5, #224
	cmp	r5, #160
	addeq	r0, r0, r4
	addne	ip, ip, r4
.L1283:
	add	r2, r2, #4
	b	.L1282
.L1291:
	ldr	r4, [r3, #1096]
	ldr	r3, [r3, #2804]
	str	ip, [r4, #524]
	str	r0, [r4, #528]
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	bl	__aeabi_uidiv
	str	r0, [r4, #532]
	ldmfd	sp!, {r4, r5, r6, pc}
.L1293:
	.align	2
.L1292:
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
	ldr	r3, .L1299
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L1298
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
	beq	.L1295
	ldr	r3, .L1299+4
	mov	r4, r1
	mov	r0, r2
	str	r3, [r1]
	mov	r1, #1024
	bl	js_hash
	str	r0, [r4, #4]
.L1295:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1298:
	mov	r0, #0
	bx	lr
.L1300:
	.align	2
.L1299:
	.word	.LANCHOR3
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
	ldr	r3, .L1305
	mov	r4, r4, asl #1
	ldr	r2, [r3, #1092]
	ldrh	r2, [r2, r4]
	cmp	r2, #0
	moveq	r0, #1
	movne	r0, #0
	streq	r0, [r3, #2820]
	ldmfd	sp!, {r4, pc}
.L1306:
	.align	2
.L1305:
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
	beq	.L1308
	ldr	r1, .L1317
	mov	r3, r0, asl #1
	ldr	r2, [r1, #1092]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1308
	ldr	r3, [r1, #1084]
	mov	r2, r4
	mov	r1, r5
	add	r3, r3, r0, asl #2
	ldr	r0, .L1317+4
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1308:
	ldr	r3, .L1317+8
	ldrh	r0, [r3]
	mov	r4, r3
	cmp	r0, r5
	beq	.L1314
	movw	r2, #65535
	cmp	r0, r2
	streqh	r5, [r3]	@ movhi
	beq	.L1314
	bl	ftl_vpn_update
	add	r3, r4, #624
	ldrh	r1, [r4]
	ldrh	r2, [r3, #-8]
	strh	r5, [r4]	@ movhi
	add	r2, r2, #1
	uxth	r2, r2
	adds	r0, r0, #0
	movne	r0, #1
	cmp	r2, #7
	movhi	r2, #0
	strh	r2, [r3, #-8]	@ movhi
	ldrh	r3, [r3, #-8]
	ldr	r2, .L1317+12
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, pc}
.L1314:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1318:
	.align	2
.L1317:
	.word	.LANCHOR0
	.word	.LC106
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
	ldr	r3, .L1331
	movw	r4, #65535
	ldr	ip, .L1331+4
	mov	r6, r6, asl #1
	mov	r8, r4
	ldr	r7, [r3, #-2548]
	ldrh	lr, [ip, r6]
	ldr	r9, [r3, #-2544]
.L1320:
	cmp	lr, r8
	beq	.L1324
	ldr	r5, [r7, lr, asl #2]
	add	r10, r7, lr, asl #2
	cmp	r5, r0
	mov	r5, lr, asl #1
	bne	.L1321
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
	b	.L1324
.L1321:
	mov	r4, lr
	ldrh	lr, [r9, r5]
	b	.L1320
.L1324:
	ldr	r4, [r3, #-2548]
	cmn	r1, #1
	ldr	lr, .L1331
	str	r0, [r4, r2, asl #2]
	ldrh	r0, [ip, r6]
	ldr	r3, [r3, #-2544]
	strh	r2, [ip, r6]	@ movhi
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	beq	.L1326
	sub	r3, lr, #3120
	ldrh	r0, [r3, #-12]
	ldr	r3, .L1331+8
	mov	r1, r1, lsr r0
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r1, r0, asl r3
	ldrb	r1, [lr, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
.L1326:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1332:
	.align	2
.L1331:
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
	ldr	r1, .L1341
	ldr	r7, .L1341+4
	sub	r1, r1, #3120
	ldrb	r5, [r1, #-10]	@ zero_extendqisi2
	ldrb	r4, [r7, #1153]	@ zero_extendqisi2
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
	ldr	r3, .L1341+8
	ldr	r3, [r3]
	tst	r3, #16384
	beq	.L1335
	str	r9, [sp]
	mov	r1, r4
	ldr	r0, .L1341+12
	mov	r2, r6
	mov	r3, r8
	bl	printk
.L1335:
	movw	r3, #1080
	ldrh	r3, [r7, r3]
	cmp	r3, r8
	bls	.L1333
	ldr	r3, .L1341+4
	mov	r2, #1
	ldr	r4, [r3, #1084]
	add	r4, r4, r8, asl #2
	ldrb	r3, [r4, #3]	@ zero_extendqisi2
	orr	r6, r3, r2, asl r6
	strb	r6, [r4, #3]
.L1333:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L1342:
	.align	2
.L1341:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC107
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
	ldr	r4, .L1364
	str	r0, [sp]
	ldr	r3, [r4, #920]
	cmp	r3, #0
	movne	r5, #0
	ldrne	r7, .L1364+4
	beq	.L1360
.L1344:
	ldrb	r3, [r7, #-3123]	@ zero_extendqisi2
	uxth	r0, r5
	cmp	r3, r0
	bls	.L1360
	ldrb	r9, [r7, #-3130]	@ zero_extendqisi2
	mov	fp, #0
	ldr	r6, .L1364+8
	mov	r1, r9
	uxth	r8, r9
	bl	__aeabi_idiv
	ldr	r2, .L1364+12
	ldrb	r3, [r6, #1153]	@ zero_extendqisi2
	cmp	r9, #1
	subhi	r1, r8, #1
	ldrh	r2, [r2]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	ldrh	r2, [sp]
	andhi	r1, r1, r5
	smulbb	r2, r2, r8
	ldr	r8, .L1364+16
	add	r0, r2, r0, asl r3
	mov	r9, r8
	uxth	r2, r0
	addhi	r2, r2, r1
	uxthhi	r2, r2
.L1346:
	ldr	r1, [r4, #920]
	uxth	r10, fp
	cmp	r10, r1
	bcs	.L1362
	add	r1, r10, #1088
	add	r1, r1, #8
	mov	r1, r1, asl #1
	ldrh	r1, [r8, r1]
	cmp	r1, r2
	bne	.L1347
	mov	r1, r2
	ldr	r0, .L1364+20
	str	r2, [sp, #4]
	bl	printk
	ldr	r3, .L1364+4
	ldrb	r1, [r3, #-2534]	@ zero_extendqisi2
	cmp	r1, #0
	ldr	r2, [sp, #4]
	bne	.L1348
	ldrb	r1, [r7, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1349
.L1348:
	ldr	r1, [r6, #2804]
	ldr	r3, .L1364+24
	ldr	r1, [r1, #156]
	cmp	r1, r3
	beq	.L1350
.L1349:
	ldr	r1, .L1364+12
	str	r2, [sp, #4]
	ldrh	r0, [r1]
	mov	r0, r2, asl r0
	bl	ftl_mask_bad_block
	ldr	r2, [sp, #4]
.L1350:
	ldr	r0, [r4, #920]
.L1351:
	cmp	r10, r0
	bcs	.L1363
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
	b	.L1351
.L1363:
	sub	r0, r0, #1
	str	r0, [r4, #920]
.L1347:
	add	fp, fp, #1
	b	.L1346
.L1362:
	add	r5, r5, #1
	b	.L1344
.L1360:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1365:
	.align	2
.L1364:
	.word	.LANCHOR0+4096
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3132
	.word	.LANCHOR0+2828
	.word	.LC108
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
	ldr	r5, .L1390
	.pad #20
	sub	sp, sp, #20
	mov	r7, r0
	ldr	r6, [r5, #1084]
	add	r8, r6, r10
	ldrb	r4, [r8, #2]	@ zero_extendqisi2
	tst	r4, #8
	mov	r3, r4, lsr #5
	str	r3, [sp]
	beq	.L1367
	ldr	r9, [r5, #2804]
	add	r1, r5, #1088
	ldrh	r2, [r6, r10]
	ldr	fp, [r6, r0, asl #2]
	ldrh	r3, [r9, #74]
	ubfx	r2, r2, #0, #11
	ldrh	ip, [r9, #72]
	ubfx	fp, fp, #11, #8
	add	r3, r2, r3
	ldrh	r9, [r1]
	add	ip, fp, ip
	str	r2, [sp, #12]
	uxth	r3, r3
	str	r3, [sp, #4]
	mov	r1, r9
	uxth	ip, ip
	mov	r0, r3
	str	ip, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r1, .L1390+4
	ldrh	lr, [r1, #-4]
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #4]
	uxtah	r0, ip, r0
	ldr	r2, [sp, #12]
	cmp	r0, lr
	ble	.L1368
	movw	r3, #2798
	sub	r1, r1, #544
	ldrh	r0, [r5, r3]
	movw	r3, #2790
	ldrh	r3, [r5, r3]
	add	r0, r0, r3
	ldrh	r3, [r1, #-12]
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1374
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
	b	.L1389
.L1368:
	mla	r3, r9, ip, r3
	ldrh	r0, [r1, #-2]
	cmp	r3, r0
	ble	.L1370
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
	blt	.L1387
	movw	r0, #2798
	movw	lr, #2790
	ldrh	r0, [r5, r0]
	ldrh	lr, [r5, lr]
	ldrh	r3, [r3, #-12]
	add	r0, r0, lr
	add	r3, r3, #8
	cmp	r0, r3
	blt	.L1374
	add	r1, r1, #24
	cmp	ip, r1
.L1389:
	bge	.L1374
.L1387:
	bfc	r4, #3, #2
	b	.L1385
.L1374:
	mov	r3, #2
	bfi	r4, r3, #3, #2
.L1385:
	strb	r4, [r8, #2]
.L1370:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	bne	.L1375
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
	b	.L1376
.L1375:
	cmp	r3, #16
	bne	.L1376
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
	b	.L1386
.L1367:
	tst	r4, #24
	bne	.L1376
	movw	r3, #2792
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1376
	movw	r3, #2790
	ldrh	r1, [r5, r3]
	cmp	r1, #15
	bhi	.L1376
	movw	r3, #2794
	movw	r2, #2788
	ldrh	r2, [r5, r2]
	ldrh	r3, [r5, r3]
	ldr	r0, .L1390+8
	add	r3, r3, r2
	movw	r2, #2796
	ldrh	r2, [r5, r2]
	add	r3, r3, r2
	ldrh	r2, [r0, #-14]
	add	r2, r2, #16
	cmp	r3, r2
	ble	.L1376
	movw	r3, #2798
	ldrh	r2, [r5, r3]
	ldrh	r3, [r0, #-12]
	add	r2, r2, r1
	add	r3, r3, #8
	cmp	r2, r3
	bge	.L1376
	mov	r3, #2
	bfi	r4, r3, #3, #2
	strb	r4, [r8, #2]
	add	r5, r5, #1088
	ldrh	r4, [r6, r10]
	ldrh	r1, [r5]
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
.L1386:
	strh	r3, [r6, r10]	@ movhi
.L1376:
	mov	r0, r7
	bl	zftl_remove_data_node
	ldr	r3, .L1390
	mov	r0, #0
	ldr	r2, [r3, #1084]
	mov	r4, r3
	add	r10, r2, r10
	ldrb	r2, [r10, #2]	@ zero_extendqisi2
	bfc	r2, #5, #3
	strb	r2, [r10, #2]
	ldr	r1, [r3, #1092]
	mov	r2, r7, asl #1
	strh	r0, [r1, r2]	@ movhi
	ldr	r2, [sp]
	add	r2, r2, #6
	and	r2, r2, #7
	cmp	r2, #4
	bhi	.L1379
	mov	r0, r7
	bl	gc_free_bad_sblk
.L1379:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L1380
	ldr	r3, [r4, #1096]
	movw	r2, #586
	ldrh	r1, [r3, r2]
	cmp	r1, r7
	bne	.L1380
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1380
	strh	r7, [r3, r2]	@ movhi
	mov	r1, r7
	ldr	r0, .L1390+12
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1380:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	zftl_insert_free_list
.L1391:
	.align	2
.L1390:
	.word	.LANCHOR0
	.word	.LANCHOR3-2528
	.word	.LANCHOR3-3072
	.word	.LC109
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
	ldr	r6, .L1431
	ldr	r8, .L1431+4
	mov	r7, r6
.L1393:
	ldrh	r2, [r8, #52]
	uxth	r3, r5
	cmp	r2, r3
	bls	.L1430
	add	r3, r8, r3, asl #1
	ldrh	r4, [r3, #54]
	ldr	r3, [r6, #1092]
	mov	r9, r4, asl #1
	ldrh	r2, [r3, r9]
	cmp	r2, #0
	beq	.L1394
	ldr	r0, .L1431+8
	mov	r1, r4
	bl	printk
.L1394:
	ldr	r3, [r7, #1092]
	mov	r2, #0
	strh	r2, [r3, r9]	@ movhi
	ldr	r3, [r7, #1092]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	bne	.L1395
	ldr	r3, .L1431+12
	ldr	r9, [r6, #1084]
	ldr	r3, [r3]
	add	r9, r9, r4, asl #2
	tst	r3, #256
	beq	.L1396
	ldrb	r2, [r9, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L1431+16
	mov	r2, r2, lsr #5
	bl	printk
.L1396:
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L1397
	ldr	r1, .L1431+20
	movw	r2, #1363
	ldr	r0, .L1431+24
	bl	printk
	bl	dump_stack
.L1397:
	mov	r0, r4
	bl	ftl_free_sblk
	ldr	r3, [r7, #1096]
	ldrh	r1, [r3, #124]
	cmp	r1, #0
	beq	.L1398
	add	r0, r3, #388
	mov	r2, #0
	add	r0, r0, #2
.L1400:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1399
	add	r2, r2, #196
	mvn	r0, #0
	add	r1, r1, r0
	mov	r2, r2, asl #1
	strh	r0, [r3, r2]	@ movhi
	strh	r1, [r3, #124]	@ movhi
	b	.L1398
.L1399:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1400
.L1398:
	ldrh	r1, [r3, #120]
	cmp	r1, #0
	beq	.L1401
	add	r0, r3, #134
	mov	r2, #0
.L1403:
	ldrh	ip, [r0, #2]!
	cmp	ip, r4
	bne	.L1402
	add	r2, r3, r2, asl #1
	mvn	r0, #0
	add	r1, r1, r0
	strh	r0, [r2, #136]	@ movhi
	strh	r1, [r3, #120]	@ movhi
	b	.L1401
.L1402:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1403
.L1401:
	ldrh	r0, [r3, #122]
	cmp	r0, #0
	beq	.L1404
	add	r1, r3, #260
	mov	r2, #0
	add	r1, r1, #2
.L1406:
	ldrh	ip, [r1, #2]!
	cmp	ip, r4
	bne	.L1405
	add	r2, r2, #132
	mvn	r1, #0
	add	r0, r0, r1
	mov	r2, r2, asl #1
	strh	r1, [r3, r2]	@ movhi
	strh	r0, [r3, #122]	@ movhi
	b	.L1404
.L1405:
	add	r2, r2, #1
	cmp	r2, #64
	bne	.L1406
	b	.L1404
.L1395:
	mov	r0, r4
	mov	r1, #1
	bl	gc_add_sblk
.L1404:
	add	r5, r5, #1
	b	.L1393
.L1430:
	ldr	r3, .L1431+4
	mov	r2, #0
	strh	r2, [r3, #52]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1432:
	.align	2
.L1431:
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LC110
	.word	.LANCHOR2
	.word	.LC111
	.word	.LANCHOR1+1620
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
	ldr	r4, .L1435
	.pad #32
	sub	sp, sp, #32
	ldr	r3, [r4, #2804]
	ldrh	ip, [r4, ip]
	ldr	r0, [r4, #1096]
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
	ldr	r0, .L1435+4
	bl	printk
	ldr	r2, [r4, #2804]
	ldrb	r3, [r4, #2801]	@ zero_extendqisi2
	ldr	r0, .L1435+8
	ldr	r1, [r2, #8]
	str	r1, [sp]
	ldr	r1, [r2, #64]
	str	r1, [sp, #4]
	ldr	r1, [r2, #20]
	ldr	r2, [r2, #28]
	bl	printk
	ldr	r2, [r4, #2804]
	ldr	r0, .L1435+12
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
	ldr	r0, .L1435+16
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
	ldr	r0, .L1435+20
	bl	printk
	ldr	r0, [r4, #1096]
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
	ldr	r0, .L1435+24
	bl	printk
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L1436:
	.align	2
.L1435:
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
	.type	ftl_write_buf, %function
ftl_write_buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	subs	r4, r0, #0
	bne	.L1438
	ldr	r1, .L1449
	movw	r2, #811
	ldr	r0, .L1449+4
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1438:
	ldr	r3, .L1449+8
	ldrb	r1, [r4, #40]	@ zero_extendqisi2
	ldrb	r2, [r3, #-2538]	@ zero_extendqisi2
	mov	r5, r3
	cmp	r1, r2
	bls	.L1443
	ldr	r1, .L1449
	movw	r2, #818
	ldr	r0, .L1449+4
	bl	printk
	bl	dump_stack
.L1443:
	ldrb	r3, [r4, #40]	@ zero_extendqisi2
	ldr	r6, .L1449+12
	cmp	r3, #0
	beq	.L1440
	ldrb	r2, [r5, #-2538]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L1441
.L1440:
	mov	r0, r4
	bl	zbuf_free
	ldrb	r0, [r6, #2800]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, pc}
.L1441:
	mov	r1, r4
	ldr	r0, .L1449+16
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
.L1450:
	.align	2
.L1449:
	.word	.LANCHOR1+1636
	.word	.LC0
	.word	.LANCHOR3
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
	ldr	r8, .L1469
	ldr	r9, .L1469+4
	mov	r6, r8
.L1452:
	ldrb	r10, [r8, #2777]	@ zero_extendqisi2
	cmp	r10, #255
	beq	.L1468
	mov	r4, #48
	ldr	r7, .L1469+8
	mul	r4, r4, r10
	add	r7, r7, r4
	add	r4, r8, r4
	ldrb	r3, [r4, #1236]	@ zero_extendqisi2
	strb	r3, [r8, #2777]
	ldr	r3, [r4, #1272]
	cmn	r3, #1
	bne	.L1453
	ldr	r4, .L1469+12
	ldrb	r3, [r4, #-2534]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1454
	ldrb	r3, [r4, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1455
.L1454:
	ldr	r3, [r6, #2804]
	ldr	r3, [r3, #156]
	cmp	r3, r9
	beq	.L1456
.L1455:
	mov	r5, #48
	ldr	r3, .L1469+16
	mla	r5, r5, r10, r6
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldrb	r3, [r6, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	ldr	r2, [r5, #1260]
	rsb	r3, r0, r3
	mov	r2, r2, lsr r0
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r3, [r6, #1096]
	movw	r2, #65535
	ldr	r1, [r3, #560]
	cmp	r1, r2
	uxth	r4, r0
	streq	r4, [r3, #560]
	ldreq	r2, [r5, #1260]
	ldr	r0, .L1469+20
	mov	r1, r4
	streq	r2, [r3, #564]
	mov	r3, #48
	mla	r10, r3, r10, r6
	ldr	r2, [r10, #1256]
	ldr	r3, [r10, #1260]
	bl	printk
	ldr	r3, [r6, #1096]
	ldr	r2, [r3, #556]
	add	r2, r2, #1
	str	r2, [r3, #556]
	ldrh	r2, [r3, #16]
	cmp	r2, r4
	moveq	r2, #0
	streqh	r2, [r3, #22]	@ movhi
	beq	.L1456
	ldrh	r2, [r3, #48]
	cmp	r2, r4
	moveq	r2, #0
	streqh	r2, [r3, #54]	@ movhi
.L1456:
	mov	r0, r7
	mov	r5, #1
	bl	ftl_write_buf
	b	.L1452
.L1453:
	cmp	r5, #1
	add	r3, r4, #1248
	add	r3, r3, #12
	bne	.L1461
	ldr	r1, [r4, #1256]
	ldr	r2, [r4, #1260]
	ldr	r0, .L1469+24
	bl	printk
	mov	r0, r7
	bl	ftl_write_buf
	b	.L1452
.L1461:
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
	bne	.L1452
	mov	r0, r7
	bl	zbuf_free
	b	.L1452
.L1468:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1470:
	.align	2
.L1469:
	.word	.LANCHOR0
	.word	1145785929
	.word	.LANCHOR0+1236
	.word	.LANCHOR3
	.word	.LANCHOR3-3132
	.word	.LC118
	.word	.LC119
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
	bne	.L1472
	ldr	r1, .L1477
	movw	r2, #1151
	ldr	r0, .L1477+4
	bl	printk
	bl	dump_stack
	ldmfd	sp!, {r3, r4, r5, lr}
	b	print_ftl_debug_info
.L1472:
	ldr	r4, .L1477+8
	ldrb	r2, [r5, #40]	@ zero_extendqisi2
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L1474
	ldr	r1, .L1477
	movw	r2, #1158
	ldr	r0, .L1477+4
	bl	printk
	bl	dump_stack
.L1474:
	ldr	r0, .L1477+12
	mov	r1, r5
	bl	buf_add_tail
	ldrb	r3, [r4, #-2527]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r4, #-2527]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1478:
	.align	2
.L1477:
	.word	.LANCHOR1+1652
	.word	.LC0
	.word	.LANCHOR3
	.word	.LANCHOR3-2528
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
	ldr	r3, .L1480
	mvn	r2, #0
	mov	r0, #0
	strb	r2, [r3, #2774]
	strb	r2, [r3, #2777]
	strb	r2, [r3, #2775]
	strb	r2, [r3, #2776]
	bx	lr
.L1481:
	.align	2
.L1480:
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
	ldr	r4, .L1492
	ldr	r0, .L1492+4
	ldrb	r1, [r4, #2774]	@ zero_extendqisi2
	bl	printk
	ldrb	r4, [r4, #2774]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1492+8
	mov	r2, #48
	mov	r6, r2
	mla	r4, r2, r4, r3
	mov	r5, r3
.L1484:
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldrb	r2, [r4, #42]	@ zero_extendqisi2
	ldr	r3, [r4, #24]
	ldr	r0, .L1492+12
	bl	printk
	ldrb	r4, [r4]	@ zero_extendqisi2
	cmp	r4, #255
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mla	r4, r6, r4, r5
	b	.L1484
.L1493:
	.align	2
.L1492:
	.word	.LANCHOR0
	.word	.LC120
	.word	.LANCHOR0+1236
	.word	.LC121
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
	ldr	lr, .L1510
	ldrb	ip, [lr, #2774]	@ zero_extendqisi2
	cmp	ip, #255
	beq	.L1502
	ldr	r4, .L1510+4
	mov	r2, #1
	ldrb	r3, [lr, #1153]	@ zero_extendqisi2
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
.L1501:
	mla	r0, r9, ip, lr
	ldr	r6, [r0, #1260]
	and	fp, r3, r6, lsr r7
	cmp	fp, r10
	bne	.L1496
	and	r6, r4, r6, lsr r8
	ldrb	r0, [r0, #1278]	@ zero_extendqisi2
	and	r6, r6, r2
	cmp	r6, r5
	bne	.L1497
	cmp	r1, #1
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1496
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1497:
	cmp	r1, #3
	ldrls	pc, [pc, r1, asl #2]
	b	.L1496
.L1498:
	.word	.L1495
	.word	.L1499
	.word	.L1500
	.word	.L1495
.L1499:
	sub	r6, r0, #7
	tst	r6, #253
	beq	.L1496
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1500:
	cmp	r0, #11
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1496:
	mla	ip, r9, ip, lr
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1501
.L1502:
	mov	r0, #0
.L1495:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1511:
	.align	2
.L1510:
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
	ldr	r3, .L1539
	mov	ip, #0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r0, #48
	ldrb	r1, [r3, #2776]	@ zero_extendqisi2
	mov	r8, r3
	ldrb	r2, [r3, #2774]	@ zero_extendqisi2
	mvn	r9, #0
	ldrb	r6, [r3, #2775]	@ zero_extendqisi2
	mov	r4, r3
	ldrb	r7, [r3, #2777]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r10, .L1539+4
	str	r1, [sp, #4]
.L1513:
	cmp	r2, #255
	beq	.L1514
	mla	fp, r0, r2, r3
	mov	r1, r2
	ldrb	lr, [fp, #1278]	@ zero_extendqisi2
	sub	lr, lr, #12
	cmp	lr, #1
	bls	.L1515
	cmp	ip, #0
	ldrne	r3, .L1539
	beq	.L1512
	b	.L1537
.L1515:
	mul	ip, r0, r1
	ldrb	r2, [fp, #1236]	@ zero_extendqisi2
	add	lr, r8, ip
	ldrb	fp, [lr, #1279]	@ zero_extendqisi2
	strb	r9, [lr, #1236]
	cmp	fp, #1
	bne	.L1518
	add	ip, r10, ip
	ldrh	ip, [ip, #34]
	cmp	ip, #0
	moveq	ip, r7
	ldreq	lr, .L1539+8
	ldrne	ip, [sp, #4]
	ldrne	lr, .L1539+12
	b	.L1519
.L1518:
	cmp	fp, #0
	beq	.L1538
.L1520:
	mov	ip, #1
	b	.L1513
.L1538:
	ldr	ip, [lr, #1256]
	cmn	ip, #1
	movne	ip, r6
	ldrne	lr, .L1539+16
	beq	.L1520
.L1519:
	cmp	ip, #255
	bne	.L1524
	strb	r2, [r3, #2774]
	mov	r2, #48
	mla	r1, r2, r1, r3
	ldrb	r3, [r1, #1237]	@ zero_extendqisi2
	strb	r3, [lr]
	b	.L1512
.L1524:
	mov	lr, ip
	mla	ip, r0, ip, r3
	ldrb	ip, [ip, #1236]	@ zero_extendqisi2
	cmp	ip, #255
	bne	.L1524
	mla	r1, r5, r1, r4
	mla	lr, r5, lr, r4
	ldrb	r1, [r1, #1237]	@ zero_extendqisi2
	strb	r1, [lr, #1236]
	b	.L1520
.L1514:
	cmp	ip, #0
	beq	.L1512
	mvn	r2, #0
.L1537:
	strb	r2, [r3, #2774]
.L1512:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1540:
	.align	2
.L1539:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR0+2777
	.word	.LANCHOR0+2776
	.word	.LANCHOR0+2775
	.fnend
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1556
	mov	r3, #0
	movw	ip, #65535
.L1543:
	mov	r1, r3, asl #3
	uxth	r0, r3
	ldrh	r1, [r1, r2]
	cmp	r1, ip
	ldr	r1, .L1556
	bxeq	lr
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1543
	mov	r2, #0
	mov	r0, r3
	mov	ip, #32768
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.L1545:
	add	r3, r1, r2, asl #3
	uxth	lr, r2
	ldrh	r3, [r3, #2]
	tst	r3, #32768
	bne	.L1544
	cmp	r3, ip
	movcc	ip, r3
	movcc	r0, lr
.L1544:
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L1545
	cmp	r0, #32
	mov	r4, r0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L1556+4
	mvn	r2, #0
	ldr	r5, .L1556
	ldrb	lr, [r3, #-2268]	@ zero_extendqisi2
	mov	r3, #0
.L1547:
	mov	ip, r3, asl #3
	uxth	r6, r3
	add	r0, r1, ip
	ldrh	r0, [r0, #2]
	cmp	r0, r2
	bcs	.L1546
	ldrh	ip, [ip, r5]
	cmp	ip, lr
	movne	r2, r0
	movne	r4, r6
.L1546:
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L1547
	cmp	r4, #32
	bne	.L1548
	ldr	r1, .L1556+8
	movw	r2, #377
	ldr	r0, .L1556+12
	bl	printk
	bl	dump_stack
.L1548:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1557:
	.align	2
.L1556:
	.word	.LANCHOR3-2524
	.word	.LANCHOR3
	.word	.LANCHOR1+1672
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
	ldr	r4, .L1603
	mov	r0, #1024
	ldr	r3, [r4, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	bl	__aeabi_idiv
	cmp	r6, #0
	uxth	r5, r0
	bne	.L1560
	add	r4, r4, #4
.L1561:
	mov	r3, r6, asl #1
	strh	r6, [r3, r4]	@ movhi
	add	r6, r6, #1
	cmp	r6, #512
	bne	.L1561
.L1565:
	mov	r1, #255
	ldr	r0, .L1603+4
	mov	r2, #2048
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L1603+8
	ldr	r0, .L1603+4
	b	.L1562
.L1560:
	cmp	r6, #1
	bne	.L1563
	add	r4, r4, #4
	mov	r3, #0
.L1564:
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
	bne	.L1564
	b	.L1565
.L1563:
	cmp	r6, #2
	bne	.L1566
	add	r4, r4, #4
	mov	r3, #0
.L1567:
	uxth	r2, r3
	mov	r1, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	r0, r2, asl #1
	subhi	r2, r0, #1
	cmp	r3, #512
	strh	r2, [r1, r4]	@ movhi
	bne	.L1567
	b	.L1565
.L1566:
	cmp	r6, #3
	bne	.L1568
	add	r4, r4, #4
	mov	r3, #0
.L1569:
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
	bne	.L1569
	b	.L1565
.L1568:
	cmp	r6, #4
	mov	r3, #0
	bne	.L1570
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
.L1571:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r4, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1571
	b	.L1565
.L1570:
	cmp	r6, #5
	bne	.L1572
	add	r4, r4, #4
.L1573:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L1573
	ldr	r2, .L1603+12
.L1574:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L1574
	b	.L1565
.L1572:
	cmp	r6, #8
	bne	.L1575
	add	r4, r4, #4
.L1576:
	strh	r3, [r3, r4]	@ movhi
	add	r3, r3, #2
	cmp	r3, #1024
	bne	.L1576
	b	.L1565
.L1575:
	cmp	r6, #9
	bne	.L1577
	strh	r3, [r4, #4]	@ movhi
	movw	r2, #1021
	mov	r3, #1
	strh	r3, [r4, #6]	@ movhi
	mov	r3, #2
	strh	r3, [r4, #8]!	@ movhi
	mov	r3, #3
.L1578:
	strh	r3, [r4, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r2
	bne	.L1578
	b	.L1565
.L1577:
	cmp	r6, #10
	bne	.L1579
	add	r4, r4, #4
.L1580:
	mov	r2, r3, asl #1
	strh	r3, [r2, r4]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L1580
	ldr	r2, .L1603+16
	movw	r1, #961
.L1581:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L1581
	b	.L1565
.L1579:
	cmp	r6, #11
	bne	.L1582
	ldr	r2, .L1603+8
	mov	r3, #0
.L1583:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L1583
	ldr	r1, .L1603+20
.L1584:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L1584
	b	.L1565
.L1582:
	cmp	r6, #13
	bne	.L1565
	ldr	r2, .L1603+24
	mov	r3, #0
.L1585:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #3
	uxth	r3, r3
	cmp	r3, #1536
	bne	.L1585
	b	.L1565
.L1562:
	uxth	r2, r3
	cmp	r2, r5
	bcs	.L1602
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L1562
.L1602:
	ldmfd	sp!, {r4, r5, r6, pc}
.L1604:
	.align	2
.L1603:
	.word	.LANCHOR0
	.word	.LANCHOR3-2264
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
	ldr	r5, .L1636
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1606
	ldr	r0, .L1636+4
	ldr	r1, .L1636+8
	bl	printk
.L1606:
	ldrh	r3, [r5, #30]
	mov	r7, #0
	ldr	r4, .L1636+12
	ldrb	r1, [r5, #16]	@ zero_extendqisi2
	ldrh	r0, [r5, #14]
	strh	r3, [r4, #2]	@ movhi
	strb	r7, [r4, #1109]
	bl	__aeabi_idiv
	ldr	r6, .L1636+16
	mov	r1, r7
	mov	r2, #8
	ldr	r9, .L1636+20
	add	r10, r9, #8
	strh	r0, [r6, #-216]	@ movhi
	ldr	r0, .L1636+24
	bl	ftl_memset
	sub	r0, r6, #212
	mov	r1, r7
	mov	r2, #32
	bl	ftl_memset
.L1611:
	ldrb	r2, [r5, #4]	@ zero_extendqisi2
	add	r1, r10, r7, asl #3
	ldr	r8, .L1636+28
	mov	r3, #2
	ldr	r0, .L1636+32
	strb	r3, [r7, r9]
	mov	r3, #0
.L1607:
	cmp	r3, r2
	bcs	.L1634
	add	ip, r0, r3
	ldrb	lr, [ip, #1]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	cmp	lr, ip
	bne	.L1608
	add	r3, r3, #1
	b	.L1607
.L1634:
	ldrb	r3, [r4, #1109]	@ zero_extendqisi2
	mov	r1, #0
	uxtb	r0, r7
	add	r2, r6, r3, asl #2
	str	r1, [r2, #-212]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #1109]
	strb	r0, [r3, #1144]
	bl	zftl_flash_enter_slc_mode
.L1608:
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1611
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldr	r3, .L1636
	cmp	r2, #2
	beq	.L1612
.L1619:
	ldrb	r3, [r5, #17]	@ zero_extendqisi2
	ldrb	r2, [r4, #1109]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #18]
	smulbb	r3, r2, r3
	strh	r3, [r6, #-180]	@ movhi
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1612:
	ldrh	ip, [r3, #18]
	mov	lr, #0
	ldrh	r2, [r4, #2]
	add	r3, r3, #4
	and	ip, ip, #65280
	ldrb	r1, [r4, #1109]	@ zero_extendqisi2
	ldrb	r7, [r3]	@ zero_extendqisi2
	mul	r2, r2, ip
	ldrb	ip, [r3, #13]	@ zero_extendqisi2
	mul	ip, ip, r2
	ldrb	r2, [r3, #23]	@ zero_extendqisi2
	str	r2, [sp]
	mov	r2, ip, asl #1
	str	r2, [sp, #4]
	mov	r2, lr
.L1617:
	add	r9, r8, r2, asl #3
	mov	r0, #0
.L1613:
	cmp	r0, r7
	bcs	.L1635
	add	r10, r3, r0
	ldrb	fp, [r10, #1]	@ zero_extendqisi2
	ldrb	r10, [r9, r0]	@ zero_extendqisi2
	cmp	fp, r10
	bne	.L1614
	add	r0, r0, #1
	b	.L1613
.L1635:
	ldmia	sp, {r9, lr}
	mov	r0, r1
	add	r1, r6, r1, asl #2
	cmp	r9, #0
	moveq	lr, ip
	str	lr, [r1, #-212]
	add	r1, r0, #1
	add	r0, r4, r0
	mov	lr, #1
	uxtb	r1, r1
	strb	r2, [r0, #1144]
.L1614:
	add	r2, r2, #1
	cmp	r2, #4
	bne	.L1617
	cmp	lr, #0
	strneb	r1, [r4, #1109]
	b	.L1619
.L1637:
	.align	2
.L1636:
	.word	.LANCHOR2
	.word	.LC4
	.word	.LANCHOR1+1696
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1156
	.word	.LANCHOR0+1144
	.word	.LANCHOR0+1164
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
	ldr	r4, .L1640
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
.L1641:
	.align	2
.L1640:
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
	ldr	r3, .L1655
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1643
	ldr	r0, .L1655+4
	mov	r2, #239
	ldr	r1, .L1655+8
	mov	r3, #0
	bl	printk
.L1643:
	ldr	r4, .L1655+12
	mov	r1, #255
	mov	r2, #512
	ldr	r0, .L1655+16
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
.L1644:
	ldrh	r1, [lr]
	uxth	r2, r0
	ldrb	r3, [r4, #-3123]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asl #1
	bge	.L1654
	ldr	r3, [ip, #-2548]
	ldr	r3, [r3, r2, asl #2]
	cmn	r3, #1
	beq	.L1645
	uxtb	r3, r3
	mov	r3, r3, asl #1
	ldrh	r1, [r5, r3]
	strh	r2, [r5, r3]	@ movhi
	mov	r2, r2, asl #1
	ldr	r3, [ip, #-2544]
	strh	r1, [r3, r2]	@ movhi
.L1645:
	add	r0, r0, #1
	b	.L1644
.L1654:
	ldmfd	sp!, {r3, r4, r5, pc}
.L1656:
	.align	2
.L1655:
	.word	.LANCHOR2
	.word	.LC122
	.word	.LANCHOR1+1716
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
	ldr	r2, .L1662
	ldr	r3, .L1662+4
	mov	ip, #1
	ldrh	r1, [r2, #2]
	ldrb	r0, [r3, #13]	@ zero_extendqisi2
	smulbb	r1, r0, r1
	ldr	r0, .L1662+8
	strh	r1, [r4, #4]	@ unaligned
	ldrb	r1, [r2, #1197]	@ zero_extendqisi2
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
.L1658:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L1661
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L1658
.L1661:
	ldmfd	sp!, {r4, pc}
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
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L1783
	ldr	r3, [r4, #2804]
	ldr	r3, [r3, #32]
	cmp	r3, #10240
	bls	.L1665
	bl	ftl_tmp_into_update
.L1665:
	ldr	r3, [r4, #1096]
	ldr	r2, [r3, #568]
	ldr	r0, [r3, #12]
	add	r1, r2, #35840
	add	r1, r1, #160
	cmp	r0, r1
	bcs	.L1666
	ldr	r1, .L1783
	ldr	ip, [r1, #2804]
	ldr	r1, [r3, #572]
	ldr	ip, [ip, #36]
	add	r1, r1, #256
	cmp	ip, r1
	movcc	r0, #0
	bcc	.L1775
.L1666:
	add	r2, r2, #860160
	add	r2, r2, #3840
	cmp	r0, r2
	bhi	.L1668
	ldr	r1, [r4, #2804]
	ldr	r2, [r3, #572]
	ldr	r1, [r1, #36]
	add	r2, r2, #32
	cmp	r1, r2
	movls	r7, #0
	movls	r9, r7
	bls	.L1669
.L1668:
	ldr	r2, [r4, #2804]
	mov	r8, #0
	ldr	ip, .L1783
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
.L1670:
	ldr	r2, .L1783+4
	ldr	r3, .L1783
	ldrh	r2, [r2]
	cmp	r2, r7
	bls	.L1780
	ldr	r0, [ip, #1084]
	mov	r1, r7, asl #2
	add	lr, r0, r1
	ldrb	r3, [lr, #2]	@ zero_extendqisi2
	and	r2, r3, #224
	cmp	r2, #224
	beq	.L1671
	tst	r3, #8
	ldrneh	r3, [r0, r1]
	ldrne	r4, [r0, r1]
	ubfxne	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1673
.L1672:
	tst	r3, #24
	ldreqh	r3, [r0, r1]
	movweq	r4, #65535
	ldrne	r4, [r0, r1]
	movwne	r3, #65535
	ubfxeq	r3, r3, #0, #11
	ubfxne	r4, r4, #11, #8
	bne	.L1675
.L1673:
	ldr	r2, [sp, #32]
	add	r9, r9, #1
	cmp	r5, r3
	add	r2, r2, r3
	uxth	r9, r9
	str	r2, [sp, #32]
	ldrhi	r2, [ip, #1096]
	movhi	r5, r3
	addhi	r2, r2, #584
	strhih	r7, [r2, #2]	@ movhi
.L1717:
	ldr	r2, [sp, #20]
	cmp	r2, r3
	movw	r2, #65535
	strcc	r3, [sp, #20]
	movcc	r8, r7
	cmp	r4, r2
	beq	.L1677
.L1675:
	ldr	r2, [sp, #24]
	cmp	r6, r4
	add	r2, r2, #1
	movhi	r6, r4
	uxth	r2, r2
	str	r2, [sp, #24]
	ldr	r2, [sp, #28]
	add	r2, r2, r4
	str	r2, [sp, #28]
	ldrhi	r2, [ip, #1096]
	addhi	r2, r2, #584
	strhih	r7, [r2]	@ movhi
	cmp	r10, r4
	movcc	fp, r7
	movcc	r10, r4
.L1677:
	cmp	r4, #9
	cmphi	r3, #9
	bhi	.L1671
	ldr	r3, .L1783+8
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L1671
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
	ldr	r0, [ip, #1092]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1783+12
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1671:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1670
.L1780:
	ldr	r0, [r3, #1096]
	mov	r2, #0
	mov	r1, #255
	str	r3, [sp, #36]
	add	r0, r0, #264
	strh	r2, [r0, #-142]	@ movhi
	mov	r2, #128
	bl	ftl_memset
	movw	r2, #586
	ldr	r3, [sp, #36]
	ldr	r1, [r3, #1096]
	ldr	r7, [r3, #1084]
	ldrh	r1, [r1, r2]
	ldr	r2, .L1783+8
	mov	ip, r1, asl #2
	ldr	r2, [r2]
	add	r4, r7, ip
	tst	r2, #1024
	beq	.L1680
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
	ldr	r0, [r3, #1092]
	mov	r3, r1, asl #1
	ldrh	r3, [r0, r3]
	ldr	r0, .L1783+16
	str	r3, [sp, #12]
	ubfx	r3, lr, #11, #8
	bl	printk
	ldr	ip, [sp, #36]
.L1680:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r1, .L1783
	and	r3, r3, #224
	cmp	r3, #32
	bne	.L1681
	ldr	r3, [r1, #2804]
	add	r3, r3, #688
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrhi	r3, .L1783+20
	movhi	r2, #1
	strhi	r2, [r3, #-176]
.L1681:
	ldrb	r2, [r4, #2]	@ zero_extendqisi2
	tst	r2, #8
	beq	.L1682
	ldr	r3, [r1, #2804]
	ldr	r0, .L1783+20
	ldr	r4, .L1783
	ldrh	r1, [r3, #96]
	ldrh	r0, [r0, #-172]
	ldrh	r3, [r7, ip]
	ubfx	r3, r3, #0, #11
	add	r3, r3, r0, lsr #2
	cmp	r1, r3
	ble	.L1682
	and	r3, r2, #192
	cmp	r3, #64
	bne	.L1683
	ldr	r2, [r4, #1096]
	movw	r3, #586
	mov	r1, #0
	ldrh	r0, [r2, r3]
	mov	r2, #1
	bl	gc_add_sblk
	ldr	r2, .L1783+24
	movw	r3, #2180
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1682
.L1683:
	tst	r2, #224
	bne	.L1682
	ldr	r3, [r4, #1096]
	movw	r2, #590
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L1682
	movw	r7, #586
	str	r2, [sp, #36]
	ldrh	r0, [r3, r7]
	add	r3, r3, #588
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L1682
	bl	zftl_remove_free_node
	ldr	r3, [r4, #1096]
	ldrh	r1, [r3, r7]
	ldr	r2, [sp, #36]
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
.L1682:
	ldr	r1, .L1783
	ldr	r2, .L1783+8
	ldr	r3, [r1, #1096]
	ldr	r2, [r2]
	add	r3, r3, #584
	ldr	ip, [r1, #1084]
	tst	r2, #1024
	ldrh	r7, [r3]
	mov	r3, r7, asl #2
	add	r4, ip, r3
	beq	.L1684
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
	ldr	r0, [r1, #1092]
	mov	r1, r7, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1783+28
	str	r1, [sp, #12]
	mov	r1, r7
	bl	printk
	ldr	ip, [sp, #36]
.L1684:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	ldr	r4, .L1783
	tst	r3, #8
	beq	.L1685
	ldr	r2, [r4, #2804]
	ldr	r0, .L1783+20
	ldrh	r1, [r2, #98]
	ldrh	r0, [r0, #-170]
	ldr	r2, [ip, r7, asl #2]
	ubfx	r2, r2, #11, #8
	add	r2, r2, r0, lsr #2
	cmp	r1, r2
	ble	.L1685
	and	r2, r3, #192
	cmp	r2, #64
	bne	.L1686
	ldr	r3, [r4, #1096]
	mov	r1, #0
	mov	r2, #1
	add	r3, r3, #584
	ldrh	r0, [r3]
	bl	gc_add_sblk
	ldr	r2, .L1783+24
	movw	r3, #2180
	mov	r1, #1
	strh	r1, [r2, r3]	@ movhi
	b	.L1685
.L1686:
	and	r3, r3, #248
	cmp	r3, #16
	bne	.L1685
	ldr	r3, [r4, #1096]
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
	ldr	r3, [r4, #1096]
	add	r2, r3, #588
	add	r3, r3, #584
	ldrh	r1, [r3]
	strh	r1, [r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L1685:
	ldr	r3, .L1783+8
	ldr	r0, [r4, #1084]
	ldr	r3, [r3]
	tst	r3, #1024
	beq	.L1687
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
	ldr	r1, .L1783
	ldr	r0, [r1, #1092]
	mov	r1, r8, asl #1
	ldrh	r1, [r0, r1]
	ldr	r0, .L1783+32
	str	r1, [sp, #12]
	mov	r1, r8
	bl	printk
.L1687:
	ldr	r3, .L1783+8
	ldr	ip, .L1783
	ldr	r3, [r3]
	ldr	r0, [ip, #1084]
	tst	r3, #1024
	beq	.L1688
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
	ldr	r0, [ip, #1092]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1783+36
	str	r1, [sp, #12]
	mov	r1, fp
	bl	printk
.L1688:
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
	ldr	r4, .L1783+8
	ldr	r3, [r4]
	tst	r3, #1024
	beq	.L1689
	uxth	r0, r0
	mov	r1, r9
	str	r0, [sp]
	uxth	r3, r7
	ldr	r0, .L1783+40
	ldr	r2, [sp, #24]
	bl	printk
.L1689:
	ldr	r3, [r4]
	ldr	r4, .L1783+20
	tst	r3, #1024
	beq	.L1690
	str	r10, [sp]
	mov	r1, r5
	ldrh	r3, [r4, #-172]
	mov	r2, r6
	ldr	r0, .L1783+44
	str	r3, [sp, #4]
	ldrh	r3, [r4, #-170]
	str	r3, [sp, #8]
	ldr	r3, [sp, #20]
	bl	printk
.L1690:
	rsb	r3, r6, r10
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldrh	r3, [r4, #-170]
	cmp	r2, r3
	bgt	.L1691
	ldr	r2, .L1783+20
	ldr	r3, [sp, #20]
	ldrh	r2, [r2, #-172]
	rsb	r3, r5, r3
	cmp	r3, r2
	movle	r7, #0
	movle	r9, r7
	ble	.L1692
.L1691:
	ldr	ip, .L1783
	mov	r7, #0
	mov	r9, r7
	ldr	r3, [ip, #1096]
	mov	fp, ip
	ldr	r4, [r3, #580]
	ldr	r3, [ip, #2804]
	uxth	r4, r4
	ldrh	r10, [r3, #134]
.L1693:
	ldr	r3, .L1783+4
	ldrh	r3, [r3]
	cmp	r10, r3
	bcs	.L1702
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r3
	ldr	r3, [fp, #1084]
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
	bne	.L1695
	ubfx	r2, r2, #3, #2
	ands	r1, r2, #1
	beq	.L1696
	cmp	r0, #160
	b	.L1778
.L1696:
	cmp	r2, #2
.L1778:
	bne	.L1698
	ldr	r2, .L1783+20
	ldr	r0, [sp, #24]
	ldrh	r2, [r2, #-170]
	cmp	r0, r2
	ble	.L1699
	ldr	r2, [r3, r8]
	ubfx	r2, r2, #11, #8
	cmp	r2, r6
	bls	.L1700
	cmp	r1, #0
	beq	.L1699
	ldrh	r2, [r3, r8]
	ubfx	r2, r2, #0, #11
	cmp	r2, r5
	bgt	.L1699
.L1700:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1783+48
	mov	r1, #1
	add	r9, r9, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1783+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1699
	ldr	r0, [fp, #1092]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1084]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1783+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1783+52
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
	b	.L1779
.L1698:
	ldr	r0, .L1783+20
	ldr	r2, [sp, #20]
	ldrh	r0, [r0, #-172]
	rsb	r2, r5, r2
	cmp	r2, r0
	ble	.L1699
	ldrh	r2, [r3, r8]
	add	r0, r5, #8
	ubfx	r2, r2, #0, #11
	cmp	r2, r0
	ble	.L1701
	cmp	r1, #0
	beq	.L1699
	ldr	r2, [r3, r8]
	add	r1, r6, #4
	ubfx	r2, r2, #11, #8
	cmp	r2, r1
	bgt	.L1699
.L1701:
	mov	r1, #0
	mov	r2, #1
	mov	r0, r4
	str	r3, [sp, #28]
	bl	gc_add_sblk
	ldr	r2, .L1783+48
	mov	r1, #1
	add	r7, r7, r1
	strh	r1, [r2]	@ movhi
	ldr	r2, .L1783+8
	ldr	r2, [r2]
	tst	r2, #1024
	ldr	r3, [sp, #28]
	beq	.L1699
	ldr	r0, [fp, #1092]
	mov	r1, r4, asl r1
	ldr	r2, [fp, #1084]
	ldrh	lr, [r0, r1]
	add	r2, r2, r8
	ldr	r1, .L1783+24
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ldr	r0, .L1783+56
	ldrh	r1, [r1, #52]
	str	r1, [sp]
	ldrh	r1, [r3, r8]
	ubfx	r1, r1, #0, #11
	str	r1, [sp, #4]
	ldr	r3, [r3, r8]
	ubfx	r3, r3, #11, #8
	str	r3, [sp, #8]
.L1779:
	mov	r1, r4
	mov	r2, r2, lsr #5
	mov	r3, lr
	bl	printk
.L1699:
	cmp	r9, #4
	cmpls	r7, #4
	bhi	.L1702
.L1695:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1693
.L1702:
	ldr	r3, .L1783
	ldr	r3, [r3, #1096]
	str	r4, [r3, #580]
.L1692:
	cmp	r6, #0
	beq	.L1705
	ldr	r0, .L1783
	ldr	r4, .L1783+4
	ldr	r3, [r0, #2804]
	ldrh	r3, [r3, #134]
.L1706:
	ldrh	r2, [r4]
	cmp	r2, r3
	bls	.L1781
	ldr	ip, [r0, #1084]
	mov	lr, r3, asl #2
	add	r8, ip, lr
	ldr	r2, [ip, r3, asl #2]
	ubfx	r1, r2, #11, #8
	cmp	r1, r6
	bcc	.L1707
	ldrb	r8, [r8, #2]	@ zero_extendqisi2
	tst	r8, #24
	rsbne	r1, r6, r1
	bfine	r2, r1, #11, #8
	strne	r2, [ip, lr]
.L1707:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1706
.L1781:
	ldr	r3, .L1783
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #72]
	add	r2, r6, r2
	strh	r2, [r3, #72]	@ movhi
	ldrh	r2, [r3, #98]
	cmp	r2, r6
	rsbhi	r2, r6, r2
	strhih	r2, [r3, #98]	@ movhi
.L1705:
	cmp	r5, #0
	beq	.L1711
	ldr	ip, .L1783
	ldr	r6, .L1783+4
	ldr	r3, [ip, #2804]
	ldrh	r3, [r3, #134]
.L1712:
	ldrh	r2, [r6]
	cmp	r2, r3
	bls	.L1782
	ldr	r4, [ip, #1084]
	mov	lr, r3, asl #2
	add	r0, r4, lr
	ldrh	r2, [r4, lr]
	ubfx	r1, r2, #0, #11
	cmp	r1, r5
	blt	.L1713
	ldrb	r0, [r0, #2]	@ zero_extendqisi2
	and	r0, r0, #24
	cmp	r0, #16
	rsbne	r1, r5, r1
	bfine	r2, r1, #0, #11
	strneh	r2, [r4, lr]	@ movhi
.L1713:
	add	r3, r3, #1
	uxth	r3, r3
	b	.L1712
.L1782:
	ldr	r3, .L1783
	ldr	r3, [r3, #2804]
	ldrh	r2, [r3, #74]
	add	r2, r5, r2
	strh	r2, [r3, #74]	@ movhi
	ldrh	r2, [r3, #96]
	cmp	r2, r5
	rsbhi	r2, r5, r2
	strhih	r2, [r3, #96]	@ movhi
.L1711:
	ldr	r4, .L1783+60
	mov	r1, #0
	sub	r0, r4, #12
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1669
	ldr	r2, .L1783
	mov	r3, r0, asl #1
	ldr	r1, [r2, #1092]
	ldrh	r2, [r4, #-8]
	ldrh	r3, [r1, r3]
	cmp	r3, r2, lsr #1
	bhi	.L1669
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	add	r9, r9, #1
.L1669:
	add	r0, r7, r9
.L1775:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1784:
	.align	2
.L1783:
	.word	.LANCHOR0
	.word	.LANCHOR0+1080
	.word	.LANCHOR2
	.word	.LC123
	.word	.LC124
	.word	.LANCHOR3
	.word	.LANCHOR0+2828
	.word	.LC125
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LC129
	.word	.LANCHOR0+5008
	.word	.LC130
	.word	.LC131
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
	movw	r3, #1080
	ldr	r5, .L1818
	mov	r2, #6
	ldr	r6, .L1818+4
	mov	r1, #0
	.pad #20
	sub	sp, sp, #20
	mov	fp, #0
	ldrh	r3, [r5, r3]
	mov	r4, fp
	ldr	r0, [r5, #1036]
	mov	r8, r5
	ldr	r10, .L1818+8
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
.L1786:
	ldr	r2, .L1818+12
	sxth	ip, r7
	ldr	r3, .L1818
	ldrh	r2, [r2]
	cmp	ip, r2
	bge	.L1816
	ldr	r6, [r8, #1084]
	add	r6, r6, ip, asl #2
	ldrb	r3, [r6, #3]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r1, [sp, #4]
	beq	.L1787
	ldr	r3, [sp, #8]
	ldrh	fp, [r10]
	ldr	r0, [r8, #2804]
	ldrb	lr, [r3, #-3123]	@ zero_extendqisi2
	mov	r3, #0
	mov	r1, r3
.L1788:
	cmp	r3, lr
	bge	.L1817
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	mov	r2, r2, asr r3
	add	r3, r3, #1
	tst	r2, #1
	addeq	r1, fp, r1
	ldrneh	r2, [r0, #146]
	uxtheq	r1, r1
	addne	r2, r2, #1
	strneh	r2, [r0, #146]	@ movhi
	b	.L1788
.L1817:
	cmp	r1, #0
	beq	.L1792
	sxth	r1, r1
	mov	r0, #32768
	str	ip, [sp, #12]
	bl	__aeabi_idiv
	add	r0, r0, #1
	ldr	ip, [sp, #12]
	uxth	r1, r0
	b	.L1787
.L1792:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	mvn	r0, #0
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	ldr	r2, [r5, #1092]
	mov	r3, ip, asl #1
	strh	r0, [r2, r3]	@ movhi
.L1787:
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
	beq	.L1793
	ldr	r1, [r8, #1096]
	ldrh	r0, [r1, #16]
	cmp	ip, r0
	beq	.L1793
	ldrh	r0, [r1, #48]
	cmp	ip, r0
	beq	.L1793
	ldrh	r1, [r1, #80]
	cmp	ip, r1
	beq	.L1793
	cmp	r2, #64
	uxtheq	r6, r7
	ldreq	r0, .L1818+16
	ldreq	r2, .L1818+20
	moveq	r1, r6
	beq	.L1814
.L1794:
	cmp	r2, #96
	uxtheq	r6, r7
	ldreq	r0, .L1818+24
	ldreq	r2, .L1818+28
	moveq	r1, r6
	beq	.L1814
.L1795:
	cmp	r2, #160
	bne	.L1796
	uxth	r6, r7
	ldr	r0, .L1818+32
	ldr	r2, .L1818+36
	mov	r1, r6
.L1814:
	bl	_insert_data_list
	ldr	r2, [r8, #1092]
	ldrh	r3, [r2, r9]
	cmp	r3, #7
	movls	r0, r6
	movls	r1, #1
	movls	r2, fp
	bhi	.L1793
	b	.L1812
.L1796:
	cmp	r2, #0
	bne	.L1793
	ldr	r2, [r8, #1092]
	uxth	fp, r7
	ldrh	r2, [r2, r9]
	cmp	r2, #0
	beq	.L1797
	sxth	r3, r4
	cmp	r3, #2
	bgt	.L1798
	mov	r1, ip
	ldr	r0, .L1818+40
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
.L1812:
	bl	gc_add_sblk
	b	.L1793
.L1798:
	ldr	r1, .L1818+44
	mov	r2, #656
	ldr	r0, .L1818+48
	bl	printk
	bl	dump_stack
.L1797:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	ands	r3, r3, #24
	ldreq	r0, .L1818+52
	moveq	r1, fp
	ldreq	r2, .L1818+56
	beq	.L1810
.L1800:
	cmp	r3, #16
	ldreq	r0, .L1818+60
	moveq	r1, fp
	ldreq	r2, .L1818+64
	movne	r1, fp
	ldrne	r0, .L1818+68
	ldrne	r2, .L1818+72
.L1810:
	bl	_insert_free_list
.L1793:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1786
.L1816:
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
.L1819:
	.align	2
.L1818:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3114
	.word	.LANCHOR0+1080
	.word	.LANCHOR3-3092
	.word	.LANCHOR0+2794
	.word	.LANCHOR3-3104
	.word	.LANCHOR0+2796
	.word	.LANCHOR3-3100
	.word	.LANCHOR0+2798
	.word	.LC132
	.word	.LANCHOR1+1736
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
	ldr	r5, .L1850
	ldrh	r3, [r5, #-168]
	cmp	r3, #128
	bls	.L1821
	ldr	r1, .L1850+4
	mov	r2, #94
	ldr	r0, .L1850+8
	bl	printk
	bl	dump_stack
.L1821:
	ldr	r4, .L1850+12
	mov	r1, #0
	ldrh	lr, [r5, #-168]
	mov	r0, r1
	movw	r6, #65535
	ldr	r2, [r4, #2804]
	add	r3, r2, #688
	add	r2, r2, #412
	add	r2, r2, #2
	ldrh	ip, [r3]
.L1824:
	ldrh	r7, [r2, #2]!
	uxth	r3, r1
	cmp	r7, r6
	addne	r0, r0, #1
	uxthne	r0, r0
	cmp	r0, ip
	bcs	.L1823
	cmp	r0, lr
	bcs	.L1823
	add	r1, r1, #1
	cmp	r1, #128
	bne	.L1824
	mov	r3, r1
.L1823:
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
	ldr	r3, .L1850+16
	ldrh	r9, [r3, #-12]
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r9, r3
	mvn	r8, r8, asl r3
.L1825:
	uxth	r3, r6
	cmp	r3, fp
	bcs	.L1847
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
.L1826:
	ldr	r0, [sp, #8]
	uxth	lr, r1
	cmp	lr, r0
	bcs	.L1848
	ldrh	lr, [r3, #2]!
	add	r1, r1, #1
	ldr	r0, [sp, #12]
	add	r2, r2, #2
	cmp	lr, r0
	ldreqh	lr, [r2, #-2]
	addeq	lr, lr, #1
	streqh	lr, [r2, #-2]	@ movhi
	b	.L1826
.L1848:
	add	r6, r6, #1
	b	.L1825
.L1847:
	ldr	r2, .L1850+20
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
.L1830:
	ldr	r3, [sp, #8]
	uxth	fp, r6
	cmp	fp, r3
	bcs	.L1849
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
	ldreq	r2, .L1850+24
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
	bls	.L1833
	ldr	r0, [r4, #1084]
	mov	r1, r3, asl #2
	ldrh	r1, [r0, r1]
	ldrh	r0, [ip, #92]
	ubfx	r1, r1, #0, #11
	add	r0, r0, #4
	cmp	r1, r0
	bgt	.L1833
	str	r1, [sp]
	mov	r1, r6
	ldr	r0, .L1850+28
	mov	r10, fp
	bl	printk
.L1833:
	ldrh	r2, [r8]
	cmp	r2, #0
	bne	.L1834
	add	fp, r6, #208
	ldr	r1, [r4, #2804]
	movw	r0, #65535
	mov	r3, fp, asl #1
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L1834
	ldr	r0, .L1850+32
	ldr	r0, [r0]
	tst	r0, #4096
	beq	.L1835
	add	r1, r1, #688
	ldr	r0, .L1850+36
	ldrh	r1, [r1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.L1835:
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
.L1834:
	add	r6, r6, #1
	add	r8, r8, #2
	b	.L1830
.L1849:
	movw	r0, #65535
	cmp	r10, r0
	movne	r0, r10
	moveq	r0, r9
	add	sp, sp, #284
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1851:
	.align	2
.L1850:
	.word	.LANCHOR3
	.word	.LANCHOR1+1756
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3-3120
	.word	.LANCHOR3-3088
	.word	.LANCHOR3-3096
	.word	.LC133
	.word	.LANCHOR2
	.word	.LC134
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
	ldr	r3, .L1859
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L1854
	ldr	r0, .L1859+4
	mov	r1, #120
	ldr	r2, .L1859+8
	bl	printk
.L1854:
	ldr	r4, .L1859+12
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1040]
	bl	ftl_memset
	ldr	r3, [r4, #1040]
	ldr	r2, .L1859+16
	ldr	r1, .L1859+20
	str	r2, [r3]
	mov	r3, #2032
	ldr	r0, [r4, #1040]
	mov	r2, #32
	add	r0, r0, #80
	str	r3, [r0, #-72]
	mov	r3, #1
	strh	r3, [r0, #-64]	@ movhi
	bl	ftl_memcpy
	ldr	r0, [r4, #1040]
	ldr	r1, .L1859+24
	mov	r2, #32
	add	r0, r0, #48
	ldmfd	sp!, {r4, lr}
	b	ftl_memcpy
.L1860:
	.align	2
.L1859:
	.word	.LANCHOR2
	.word	.LC135
	.word	.LANCHOR1+1772
	.word	.LANCHOR0
	.word	1398362953
	.word	.LANCHOR0+1111
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
.L1862:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1862
.L1864:
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
	ldr	r3, .L1867
	ldr	r0, [r3]
	ldr	r3, .L1867+4
	ldr	r3, [r3, #-160]
	rsb	r0, r3, r0
	b	jiffies_to_msecs
.L1868:
	.align	2
.L1867:
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
	ldr	r3, .L1871
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #12]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1872:
	.align	2
.L1871:
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
	ldr	r3, .L1875
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #16]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1876:
	.align	2
.L1875:
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
	ldr	r3, .L1879
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #4]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1880:
	.align	2
.L1879:
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
	ldr	r3, .L1883
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #8]
	blx	r3
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1884:
	.align	2
.L1883:
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
	ldr	ip, .L1889
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #-148]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1888
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1888:
	b	ftl_memcpy
.L1890:
	.align	2
.L1889:
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
	.global	ftl_dma32_malloc
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #8192
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ble	.L1893
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_malloc
.L1893:
	ldr	r5, .L1896
	add	r4, r0, #63
	bic	r4, r4, #63
	ldr	r3, [r5, #-144]
	cmp	r3, r4
	bge	.L1894
	mov	r0, #16384
	bl	ftl_malloc
	mov	r3, #16384
	str	r3, [r5, #-144]
	str	r0, [r5, #-140]
.L1894:
	ldr	r3, [r5, #-144]
	ldr	r0, [r5, #-140]
	rsb	r3, r4, r3
	str	r3, [r5, #-144]
	add	r4, r0, r4
	str	r4, [r5, #-140]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1897:
	.align	2
.L1896:
	.word	.LANCHOR3
	.fnend
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
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
	ldr	r5, .L1918
	mov	r3, #0
	str	r3, [sp, #12]
	mov	r7, r0
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L1899
	ldr	r0, .L1918+4
	mov	r2, r7
	ldr	r1, .L1918+8
	bl	printk
.L1899:
	ldr	r4, .L1918+12
	mov	r3, #6
	ldr	r2, [r7, #352]
	ldr	r6, .L1918+12
	strb	r3, [r4, #1028]
	ldr	r3, .L1918+16
	str	r7, [r4, #1044]
	cmp	r2, r3
	ldr	r2, [r7, #128]
	moveq	r3, #8
	streqb	r3, [r4, #1028]
	ldr	r3, .L1918+20
	cmp	r2, r3
	ldr	r2, .L1918+24
	moveq	r3, #9
	streqb	r3, [r4, #1028]
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L1902
	mov	r3, #1
	strb	r3, [r6, #1199]
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
	ldr	r0, [r6, #1044]
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
	beq	.L1904
	ldr	r1, [r0]
	ldr	r2, [r0, #8]
	ldr	r3, [r0, #80]
	ldr	ip, [r0, #84]
	ldr	r0, [r0, #88]
	b	.L1917
.L1902:
	ldr	r3, [sp, #12]
	mov	r1, #0
	strb	r1, [r6, #1199]
	mov	r0, #2048
	orr	r3, r3, #256
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #117440512
	orr	r3, r3, #16777216
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	str	r3, [r7]
	ldr	r3, [r6, #1044]
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
	tst	r3, #4096
	str	r0, [r6, #1204]
	beq	.L1904
	ldr	r0, [r6, #1044]
	ldr	r1, [r0]
	ldr	r2, [r0, #344]
	ldr	r3, [r0, #304]
	ldr	ip, [r0, #308]
	ldr	r0, [r0, #312]
.L1917:
	str	r0, [sp, #4]
	str	ip, [sp]
	ldr	r0, .L1918+28
	bl	printk
.L1904:
	ldr	r3, .L1918+12
	mov	r2, #1
	strb	r2, [r4, #1200]
	mov	r2, #0
	add	r1, r3, #1232
	strb	r2, [r4, #1197]
	strh	r2, [r1]	@ movhi
	ldr	r2, [r5]
	tst	r2, #4096
	beq	.L1898
	ldr	r0, .L1918+32
	ldrb	r1, [r3, #1028]	@ zero_extendqisi2
	bl	printk
.L1898:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L1919:
	.align	2
.L1918:
	.word	.LANCHOR2
	.word	.LC136
	.word	.LANCHOR1+1796
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
	.type	zbuf_init, %function
zbuf_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, #0
	ldr	r4, .L1924
	mov	r6, r5
	ldr	r7, .L1924+4
.L1921:
	ldrb	r0, [r7, #-2538]	@ zero_extendqisi2
	uxtb	r3, r5
	add	r2, r3, #1
	strb	r3, [r4, #1]
	strb	r2, [r4]
	add	r5, r5, #1
	mov	r0, r0, asl #9
	strb	r6, [r4, #2]
	str	r6, [r4, #8]
	bl	ftl_dma32_malloc
	add	r4, r4, #48
	str	r0, [r4, #-44]
	mov	r0, #64
	bl	ftl_dma32_malloc
	cmp	r5, #32
	str	r0, [r4, #-36]
	bne	.L1921
	ldr	r3, .L1924+8
	mvn	r2, #0
	strb	r5, [r3, #2773]
	strb	r2, [r3, #2724]
	mov	r2, #0
	strb	r2, [r3, #2772]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1925:
	.align	2
.L1924:
	.word	.LANCHOR0+1236
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	zbuf_init, .-zbuf_init
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
	ldr	r6, .L1928
	mov	r5, #0
	ldr	r4, .L1928+4
	mov	r1, r5
	add	r7, r6, r9
	movw	r2, #2204
	sub	r8, r4, #3088
	mov	r0, r7
	strb	r5, [r4, #-3115]
	strb	r5, [r4, #-136]
	str	r5, [r4, #-132]
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
	strh	r2, [r4, #-128]	@ movhi
	movw	r2, #2814
	strh	r0, [r6, r2]	@ movhi
	sub	r4, r4, #3072
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_dma32_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #2948]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_dma32_malloc
	ldrh	r3, [r7, #-10]
	str	r0, [r4, #2952]
	ldrb	r0, [r4, #-51]	@ zero_extendqisi2
	mul	r0, r0, r3
	bl	ftl_dma32_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #-48]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_dma32_malloc
	ldrb	r3, [r4, #-51]	@ zero_extendqisi2
	str	r0, [r4, #-56]
	ldrh	r0, [r7, #-10]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_dma32_malloc
	movw	r2, #2808
	str	r0, [r4, #2956]
	ldrh	r3, [r4, #-14]
	mov	r3, r3, lsr #2
	strh	r3, [r6, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1929:
	.align	2
.L1928:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	gc_init, .-gc_init
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
	ldr	r0, .L1932
	bl	printk
	ldr	r3, .L1932+4
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #40]
	ldmfd	sp!, {r4, lr}
	bx	r3	@ indirect register sibling call
.L1933:
	.align	2
.L1932:
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
	ldr	r3, .L1935
	mov	r0, #0
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #32]
	bx	r3	@ indirect register sibling call
.L1936:
	.align	2
.L1935:
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
	ldr	r3, .L1938
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #44]
	bx	r3	@ indirect register sibling call
.L1939:
	.align	2
.L1938:
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
	ldr	r3, .L1941
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #48]
	bx	r3	@ indirect register sibling call
.L1942:
	.align	2
.L1941:
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
	ldr	r3, .L1944
	mov	r0, #0
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #36]
	bx	r3
.L1945:
	.align	2
.L1944:
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
	ldr	r3, .L1947
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #60]
	bx	r3
.L1948:
	.align	2
.L1947:
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
	ldr	r3, .L1950
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #64]
	bx	r3
.L1951:
	.align	2
.L1950:
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
	ldr	ip, .L1953
	ldr	ip, [ip, #-152]
	ldr	ip, [ip, #20]
	bx	ip
.L1954:
	.align	2
.L1953:
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
	ldr	r3, .L1956
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #28]
	bx	r3
.L1957:
	.align	2
.L1956:
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
	ldr	r3, .L1959
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #52]
	bx	r3
.L1960:
	.align	2
.L1959:
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
	ldr	r3, .L1962
	ldr	r3, [r3, #-152]
	ldr	r3, [r3, #56]
	bx	r3	@ indirect register sibling call
.L1963:
	.align	2
.L1962:
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
	ldr	r0, .L1966
	mov	r1, ip
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	mov	r3, #16
	bl	print_hex_dump
	add	sp, sp, #20
	@ sp needed
	ldr	pc, [sp], #4
.L1967:
	.align	2
.L1966:
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
	ldr	r8, .L2079
	.pad #36
	sub	sp, sp, #36
	mvn	r7, #81
	mvn	r6, #80
	mov	r5, r0
	ldr	r4, [r8, #1040]
	add	r3, r4, #128
	str	r3, [sp]
	strb	r0, [r4, #112]
	mvn	r3, #83
	strb	r7, [r4, #130]
	strb	r3, [r4, #128]
	mvn	r3, #82
	strb	r6, [r4, #131]
	strb	r3, [r4, #129]
	bne	.L1969
	mvn	r3, #88
	strb	r3, [r4, #128]
	ldr	r3, .L2079+4
	mvn	r1, #8
	mov	r6, #7
	strb	r1, [r3, #413]
	b	.L2030
.L1969:
	cmp	r0, #3
	bne	.L1971
	add	r1, r4, #127
	mov	r3, #176
.L1972:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1972
	b	.L2073
.L1971:
	cmp	r0, #4
	bne	.L1973
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
.L2073:
	mov	r6, #8
	mov	fp, r6
	b	.L1970
.L1973:
	cmp	r0, #5
	bne	.L1974
	mov	r3, #56
	mov	r6, #8
	strb	r3, [r4, #128]
	mov	r3, #57
	strb	r3, [r4, #129]
	mov	r3, #58
	strb	r3, [r4, #130]
	mov	r3, #59
	strb	r3, [r4, #131]
	b	.L2030
.L1974:
	cmp	r0, #6
	bne	.L1975
	mov	r3, #14
	mov	r6, #12
	strb	r3, [r4, #128]
	mov	r3, #15
	strb	r3, [r4, #129]
	mov	r3, #16
	strb	r3, [r4, #130]
	mov	r3, #17
	strb	r3, [r4, #131]
	b	.L2030
.L1975:
	cmp	r0, #7
	bne	.L1976
	add	r1, r4, #127
	mov	r3, #176
.L1977:
	strb	r3, [r1, #1]!
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #184
	bne	.L1977
	mvn	r3, #43
	mov	r6, #12
	strb	r3, [r4, #136]
	mov	fp, #10
	mvn	r3, #42
	strb	r3, [r4, #137]
	b	.L1970
.L1976:
	cmp	r0, #8
	mov	r6, #7
	bne	.L2030
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
	b	.L1970
.L2030:
	mov	fp, #4
.L1970:
	sub	r3, r5, #1
	cmp	r3, #1
	bhi	.L2069
	ldr	r1, .L2079+8
	mov	r9, #0
.L1978:
	ldrb	ip, [r8, #1109]	@ zero_extendqisi2
	uxtb	r3, r9
	ldr	r0, .L2079
	cmp	ip, r3
	bls	.L1985
	add	r3, r0, r3
	mov	r5, #160
	ldr	r0, [r0, #1044]
	sub	r10, fp, #1
	ldrb	r7, [r3, #1144]	@ zero_extendqisi2
	add	r2, r4, #127
	ldr	ip, [sp]
	mla	r5, r5, r7, r4
	add	r7, r0, r7, asl #8
	uxtab	r10, ip, r10
	mov	ip, #55
	add	r5, r5, #144
	sub	r3, r5, #1
.L1980:
	str	ip, [r7, #2056]
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r3, [sp, #12]
	str	r0, [r7, #2052]
	mov	r0, #200
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	str	r2, [sp, #20]
	bl	ndelay
	ldr	r0, [r7, #2048]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	cmp	r2, r10
	ldr	ip, [sp, #8]
	ldr	r1, [sp, #16]
	strb	r0, [r3, #1]!
	bne	.L1980
	mov	ip, r5
	mov	r0, #0
.L1981:
	add	r7, r1, r0
	mov	r3, #1
.L1982:
	ldrb	lr, [r7, r3, asl #2]	@ zero_extendqisi2
	ldrb	r10, [ip]	@ zero_extendqisi2
	add	lr, lr, r10
	strb	lr, [ip, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1982
	add	r0, r0, #1
	add	ip, ip, #1
	cmp	r0, #4
	bne	.L1981
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r5, #16]
	strb	r3, [r5, #24]
	strb	r3, [r5, #32]
	strb	r3, [r5, #40]
	strb	r3, [r5, #48]
	strb	r3, [r5, #41]
	strb	r3, [r5, #49]
	b	.L1978
.L2069:
	sub	r3, r5, #3
	cmp	r3, #5
	bhi	.L1985
	smulbb	r2, fp, r6
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #28]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L2077:
	str	r3, [sp, #12]
	ldr	r3, .L2079
	ldrb	r2, [r3, #1109]	@ zero_extendqisi2
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	cmp	r2, r3
	bhi	.L2029
.L1985:
	strb	fp, [r4, #113]
	strb	r6, [r4, #114]
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2029:
	ldr	r2, .L2079
	mov	r10, #160
	mov	r9, #255
	add	r3, r2, r3
	ldrb	r3, [r3, #1144]	@ zero_extendqisi2
	mov	r0, r3
	str	r3, [sp]
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [sp]
	mla	r10, r10, r3, r4
	add	r3, r10, #144
	str	r3, [sp, #24]
	ldr	r3, .L2079
	ldr	r3, [r3, #1044]
	str	r3, [sp, #4]
	ldr	r3, [sp]
	mov	r8, r3, asl #8
	ldr	r3, [sp, #4]
	add	r7, r3, r8
	str	r9, [r7, #2056]
	bl	nandc_wait_flash_ready
	cmp	r5, #8
	bne	.L1987
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
	b	.L1988
.L1987:
	cmp	r5, #4
	mov	r3, #54
	str	r3, [r7, #2056]
	moveq	r3, #64
	streq	r9, [r7, #2052]
	streq	r3, [r7, #2048]
	moveq	r3, #204
	beq	.L2074
.L1989:
	sub	r3, r5, #5
	cmp	r3, #1
	ldrlsb	r3, [r4, #128]	@ zero_extendqisi2
	strls	r3, [r7, #2052]
	movls	r3, #82
	bls	.L2075
	cmp	r5, #7
	bne	.L1990
	mov	r3, #174
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #176
.L2074:
	str	r3, [r7, #2052]
	mov	r3, #77
.L2075:
	str	r3, [r7, #2048]
.L1990:
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
.L1988:
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
	bne	.L1994
	cmp	r5, #7
	movne	ip, #2
	moveq	ip, #32
.L1994:
	ldr	r3, .L2079+12
	ldmia	sp, {r1, r2}
	ldr	r3, [r3, #-112]
	add	r1, r2, r1, asl #8
	mov	r0, r3
.L1995:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1995
	cmp	r10, #0
	beq	.L1996
	mov	r2, #0
.L1998:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L1997
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1997
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1998
	b	.L1999
.L1997:
	cmp	r0, #7
	bne	.L2000
.L1999:
	ldr	r0, .L2079+16
	mov	r1, #0
	bl	printk
.L2001:
	b	.L2001
.L1996:
	cmp	r5, #7
	bne	.L2002
	mov	r2, r10
.L2004:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L2003
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L2003
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L2004
	b	.L2005
.L2003:
	cmp	r0, #7
	bne	.L2000
.L2005:
	ldr	r0, .L2079+16
	mov	r1, #0
	bl	printk
.L2006:
	b	.L2006
.L2002:
	cmp	r5, #6
	bne	.L2000
	sub	r2, r3, #1
	add	r3, r3, #7
.L2007:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L2000
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L2000
	cmp	r2, r3
	bne	.L2007
	ldr	r0, .L2079+16
	mov	r1, #0
	bl	printk
.L2009:
	b	.L2009
.L2000:
	ldr	r3, .L2079+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-112]
	add	r0, r2, r0
	mov	r3, r2
.L2010:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L2010
.L2078:
	ldr	r3, .L2079+12
	mov	r0, #8
	ldr	r1, [sp, #8]
	ldr	r3, [r3, #-112]
	add	r1, r3, r1
	str	r1, [sp, #20]
.L2013:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L2012:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L2012
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L2013
.L2014:
	mov	ip, #0
	mov	r0, ip
.L2017:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r8, #16
	mov	r7, lr
.L2015:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	subs	r8, r8, #1
	add	lr, lr, r9
	bne	.L2015
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L2017
	ldr	r1, [sp, #20]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L2014
	ldr	r3, .L2079+12
	ldr	r1, [r3, #-112]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L2019:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L2019
	cmp	r3, #7
	ble	.L2020
	ldr	r0, .L2079+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2079+16
	mov	r1, #0
	bl	printk
.L2021:
	b	.L2021
.L2020:
	cmp	r5, #6
	moveq	r0, #4
	beq	.L2022
	cmp	r5, #7
	moveq	r0, #10
	beq	.L2022
	cmp	r10, #0
	moveq	r0, #8
	movne	r0, #5
.L2022:
	sub	r9, fp, #1
	ldr	r1, [sp, #24]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L2023:
	mov	r7, r1
	mov	r3, r2
.L2024:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, fp
	bcc	.L2024
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r6
	add	r1, r1, r0
	blt	.L2023
	ldmia	sp, {r2, r3}
	mov	r8, #255
	add	r7, r3, r2, asl #8
	str	r8, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L2026
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
	b	.L2076
.L2026:
	cmp	r10, #0
	movne	r3, #190
	moveq	r3, #56
.L2076:
	str	r3, [r7, #2056]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2077
.L2080:
	.align	2
.L2079:
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
	ldr	r3, .L2083
	ldrb	r0, [r3, #23]	@ zero_extendqisi2
	sub	r3, r0, #1
	cmp	r3, #7
	bxhi	lr
	b	hynix_get_read_retry_default
.L2084:
	.align	2
.L2083:
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
	ldr	r3, .L2134
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r5, #0
	ldrb	r2, [r3, #1028]	@ zero_extendqisi2
	mov	r4, r3
	ldr	r7, .L2134+4
	cmp	r2, #9
	strb	r5, [r7, #-108]
	bne	.L2086
	ldr	r6, [r3, #1044]
	ldr	r3, [r6, #16]
	str	r3, [sp]
	ldr	r3, [r6, #48]
	ubfx	r3, r3, #1, #1
	cmp	r3, r5
	moveq	r5, r3
	moveq	r7, r4
	beq	.L2088
.L2087:
	ldr	r2, [r6, #64]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #6
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2090
	ldr	r3, [r4, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2089
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2089
	ldr	r1, [sp, #4]
	ldr	r0, .L2134+8
	ubfx	r1, r1, #17, #1
	bl	printk
	b	.L2090
.L2089:
	ldr	r3, [sp]
	add	r5, r5, #1
	ubfx	r3, r3, #22, #6
	cmp	r5, r3, asl #12
	bne	.L2091
	ldr	r2, [r6, #64]
	mov	r1, r5
	ldr	r3, [sp]
	ldr	r0, .L2134+12
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r3, [sp, #4]
	tst	r3, #8192
	mov	r3, #1
	strb	r3, [r7, #-108]
	bne	.L2090
	ldr	r3, .L2134+16
	ldr	r0, .L2134+20
	ldr	r3, [r3, #4]
	blx	r3
	b	.L2090
.L2091:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2087
.L2090:
	ldr	r3, [r4, #1224]
	ldr	r5, .L2134
	cmp	r3, #0
	beq	.L2095
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #1216]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1220]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2133
.L2097:
	ldr	r3, [r4, #1044]
	add	r5, r5, #1
	ldr	r3, [r3, #16]
	str	r3, [sp]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2096
	ldr	r2, [sp]
	mov	r1, r5
	ldr	r3, [r6, #64]
	ldr	r0, .L2134+24
	ubfx	r3, r3, #16, #6
	bl	printk
	ldr	r0, .L2134+28
	ldr	r1, [r7, #1044]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2096:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2088:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2097
	ldr	r3, [r4, #1224]
	ldr	r5, .L2134
	cmp	r3, #0
	beq	.L2095
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #1216]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1220]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #2
	b	.L2132
.L2086:
	ldr	r7, [r3, #1044]
	ldr	r3, [r7, #8]
	str	r3, [sp]
	ldr	r6, [r7, #16]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	moveq	r5, r4
	beq	.L2100
.L2099:
	ldr	r2, [r7, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L2102
	ldr	r3, [r4, #1044]
	ldr	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L2101
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L2101
	ldr	r1, [sp, #4]
	ldr	r0, .L2134+32
	bl	printk
	b	.L2102
.L2101:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L2103
	ldr	r2, [r7, #28]
	mov	r1, r5
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L2134+12
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L2134+28
	ldr	r1, [r4, #1044]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2103:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L2099
.L2102:
	ldr	r3, [r4, #1224]
	ldr	r5, .L2134
	cmp	r3, #0
	beq	.L2095
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #1216]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1220]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2133:
	mov	r2, #0
	b	.L2131
.L2106:
	ldr	r3, [r4, #1044]
	add	r6, r6, #1
	ldr	r3, [r3, #8]
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L2105
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r7, #28]
	ldr	r0, .L2134+24
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L2134+28
	ldr	r1, [r5, #1044]
	mov	r2, #4
	mov	r3, #64
	bl	rknand_print_hex
.L2105:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L2100:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L2106
	ldr	r3, [r4, #1224]
	ldr	r5, .L2134
	cmp	r3, #0
	beq	.L2095
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #1216]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r5, #1220]
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #6
	mov	r1, r1, asl #7
.L2132:
	mov	r2, #1
.L2131:
	bl	rknand_dma_unmap_single
.L2095:
	mov	r3, #0
	str	r3, [r4, #1224]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2135:
	.align	2
.L2134:
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
	bne	.L2166
	ldr	r5, .L2179
	ldrb	r3, [r5, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L2138
	ldr	ip, [r5, #1044]
	mov	r2, r8, lsr #2
	mov	r1, #1
	mov	lr, r4
	mov	r0, r4
.L2139:
	cmp	lr, r2
	bcs	.L2176
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
	bne	.L2158
	ldr	r3, [sp, #4]
	tst	r3, #262144
	bne	.L2158
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
	b	.L2140
.L2158:
	mvn	r0, #0
.L2140:
	add	lr, lr, #1
	b	.L2139
.L2176:
	ldr	r3, [r5, #1044]
	cmp	r1, #0
	movne	r0, #512
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2145
	mov	r1, r4
	ldr	r0, .L2179+4
	bl	printk
	ldr	r3, [r5, #1044]
	mvn	r0, #0
	orr	r4, r4, #131072
	str	r4, [r3]
.L2145:
	tst	r4, #8192
	beq	.L2146
	ldr	r3, .L2179+8
	ldrb	r3, [r3, #-108]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2146
	ldr	r0, .L2179+12
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #1044]
	mov	r2, #1
	str	r2, [r3, #16]
	b	.L2147
.L2138:
	ldrb	r3, [r5, #1197]	@ zero_extendqisi2
	mov	r1, r8, lsr #1
	mov	r2, r4
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L2149:
	cmp	r2, r1
	add	r0, r4, ip
	add	r3, r3, #4
	bcs	.L2177
	ldr	lr, [r5, #1204]
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
	b	.L2149
.L2177:
	ldr	ip, [r5, #1044]
	mov	r1, #0
	mov	r2, r8, lsr #2
	mov	r0, r1
.L2151:
	cmp	r1, r2
	bcs	.L2178
	add	r3, r1, #8
	ldr	r3, [ip, r3, asl #2]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #4
	bne	.L2161
	ldr	r3, [sp, #4]
	tst	r3, #32768
	bne	.L2161
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
	b	.L2152
.L2161:
	mvn	r0, #0
.L2152:
	add	r1, r1, #1
	b	.L2151
.L2178:
	ldr	r3, [r5, #1044]
	mov	r2, #0
	str	r2, [r3, #16]
	ldr	r4, [r3]
	and	r3, r4, #139264
	cmp	r3, #139264
	bne	.L2146
	mov	r1, r4
	ldr	r0, .L2179+16
	bl	printk
	ldr	r3, .L2179
	orr	r4, r4, #131072
	ldr	r3, [r3, #1044]
	str	r4, [r3]
	b	.L2147
.L2146:
	cmn	r0, #1
	beq	.L2166
	ldr	r3, [r7]
	cmn	r3, #1
	bne	.L2166
	ldr	r3, [r7, #4]
	cmn	r3, #1
	bne	.L2166
	ldr	r3, [r6]
	cmn	r3, #1
	moveq	r0, #512
	b	.L2166
.L2147:
	mvn	r0, #0
.L2166:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2180:
	.align	2
.L2179:
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
	ldr	r10, .L2197
	.pad #20
	sub	sp, sp, #20
	mvn	r4, #0
	ubfx	r9, r1, #24, #2
	str	r2, [sp, #12]
	mov	r8, r3
	ldrb	r0, [r10, #1153]	@ zero_extendqisi2
	mov	r7, r10
	ldr	r5, [r10, #1044]
	mov	r10, r6, asl #8
	rsb	r0, r0, #24
	bic	r4, r1, r4, asl r0
	bl	nandc_wait_flash_ready
	mov	r0, r6
	bl	nandc_cs
	cmp	r9, #0
	bne	.L2182
	mov	r0, r6
	bl	zftl_flash_enter_slc_mode
	b	.L2183
.L2182:
	ldr	r2, [r7, #1104]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L2184
	ldrb	r2, [r7, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2184
	ldrb	r2, [r7, #1161]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r2, r5, r10
	streq	r9, [r2, #2056]
	beq	.L2183
.L2184:
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
.L2183:
	ldr	r2, [r7, #1104]
	ldrb	r1, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2185
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	cmp	r1, #2
	addeq	r1, r5, r10
	moveq	r0, #38
	streq	r0, [r1, #2056]
.L2185:
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
	ldrb	r1, [r7, #1152]	@ zero_extendqisi2
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
	beq	.L2187
	ldrb	r3, [r7, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2187
	ldr	r3, .L2197
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r4, r4, r4, asl #1
	subeq	r0, r4, #1
	addeq	r0, r0, r9
	beq	.L2196
.L2187:
	mov	r0, r4
.L2196:
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
.L2198:
	.align	2
.L2197:
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
	ldr	r4, .L2227
	mov	r5, r0
	str	r3, [sp, #24]
	mov	r9, r1
	str	r2, [sp, #20]
	mov	r8, #0
	ldrb	r3, [r4, #1197]	@ zero_extendqisi2
	ldr	r10, .L2227+4
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r3, [r4, #1044]
	str	r3, [sp, #16]
.L2200:
	mov	r0, r5
	mov	r7, #0
	bl	zftl_flash_enter_slc_mode
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	mvn	r4, #0
	ldr	r3, [sp, #16]
	add	r6, r3, r5, asl #8
.L2201:
	ldrb	r3, [r10, #-100]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L2205
	mov	r3, #239
	mov	r0, #200
	str	r3, [r6, #2056]
	mov	r3, #137
	str	r3, [r6, #2052]
	add	fp, r7, #1
	bl	ndelay
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
	ldr	r3, .L2227+8
	ldr	r3, [r3]
	tst	r3, #4096
	mov	ip, r0
	beq	.L2202
	str	r0, [sp]
	mov	r1, r7
	str	r0, [sp, #28]
	mov	r2, r9
	ldr	r0, .L2227+12
	mov	r3, r4
	bl	printk
	ldr	ip, [sp, #28]
.L2202:
	cmn	ip, #1
	beq	.L2203
	ldr	r3, [r10, #-112]
	cmn	r4, #1
	moveq	r4, ip
	str	r3, [sp, #20]
	ldr	r3, [r10, #-104]
	str	r3, [sp, #24]
	ldr	r3, [sp, #12]
	cmp	ip, r3
	bcc	.L2212
.L2203:
	mov	r7, fp
	b	.L2201
.L2212:
	mov	r4, ip
.L2205:
	ldr	r3, [sp, #16]
	mov	r6, r5, asl #8
	mov	r0, #200
	add	fp, r3, r6
	mov	r3, #239
	str	r3, [fp, #2056]
	mov	r3, #137
	str	r3, [fp, #2052]
	bl	ndelay
	mov	r3, #0
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	str	r3, [fp, #2048]
	ldr	r3, [sp, #12]
	cmp	r4, r3
	bcc	.L2207
	cmn	r4, #1
	movne	r4, #256
.L2207:
	cmn	r4, #1
	movne	fp, #0
	moveq	fp, #1
	cmp	r4, #256
	movne	r3, fp
	orreq	r3, fp, #1
	cmp	r3, #0
	beq	.L2208
	mov	r3, r7
	str	r4, [sp]
	ldr	r0, .L2227+16
	mov	r1, r5
	mov	r2, r9
	eor	r7, r8, #1
	bl	printk
	ands	r7, fp, r7
	beq	.L2209
	bl	nandc_wait_flash_ready
	ldr	r3, .L2227
	mov	r0, #200
	mov	r8, #1
	ldr	r3, [r3, #1044]
	add	r6, r3, r6
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #150
	str	r3, [r6, #2052]
	bl	ndelay
	mov	r3, #3
	str	r3, [r6, #2048]
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	b	.L2200
.L2209:
	cmp	r8, #0
	beq	.L2210
	bl	nandc_wait_flash_ready
	ldr	r3, .L2227
	mov	r0, #200
	ldr	r9, [r3, #1044]
	mov	r3, #239
	add	r5, r9, r5, asl #8
	add	r6, r9, r6
	str	r3, [r6, #2056]
	mov	r3, #150
	str	r3, [r5, #2052]
	bl	ndelay
	cmn	r4, #1
	str	r7, [r5, #2048]
	movne	r4, #256
	str	r7, [r5, #2048]
	str	r7, [r5, #2048]
	str	r7, [r5, #2048]
	b	.L2210
.L2208:
	cmp	r8, #0
	beq	.L2210
	str	r3, [sp, #12]
	bl	nandc_wait_flash_ready
	ldr	r2, .L2227
	mov	r0, #200
	mov	r4, #256
	ldr	r9, [r2, #1044]
	mov	r2, #239
	add	r5, r9, r5, asl #8
	add	r6, r9, r6
	str	r2, [r6, #2056]
	mov	r2, #150
	str	r2, [r5, #2052]
	bl	ndelay
	ldr	r3, [sp, #12]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
.L2210:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2228:
	.align	2
.L2227:
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
	ldr	r7, .L2277
	add	r6, r8, #8
	ldr	r3, [r7, #1044]
	add	r6, r3, r6, asl #8
	str	r3, [sp, #8]
	ldrb	r3, [r7, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	movne	r3, #56
	moveq	r3, #46
	movne	r5, #10
	moveq	r5, #26
	cmp	r4, #0
	str	r3, [sp, #16]
	mvn	r4, #0
	bne	.L2231
	ldr	r3, [sp, #8]
	mov	r9, #1
	add	r3, r3, r8, asl #8
	str	r3, [sp, #16]
.L2238:
	ldr	r3, .L2277
	mov	r0, r6
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2232
	mov	r2, #0
	mov	r1, r9
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	ldr	r2, [sp, #16]
	str	r3, [r2, #2056]
	b	.L2233
.L2232:
	uxtb	r1, r9
	bl	toshiba_3d_set_slc_rr_para
.L2233:
	ldr	r3, [sp, #64]
	mov	r0, r8
	mov	r1, r10
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2277+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2234
	mov	r3, ip
	ldr	r0, .L2277+8
	mov	r1, r9
	mov	r2, r10
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2234:
	cmn	ip, #1
	beq	.L2235
	ldr	r3, .L2277+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-112]
	ldr	fp, [r3, #-104]
	ldrb	r3, [r7, #1197]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2253
.L2235:
	add	r9, r9, #1
	cmp	r9, r5
	bne	.L2238
	b	.L2237
.L2253:
	mov	r5, r9
	mov	r4, ip
.L2237:
	ldr	r3, .L2277
	mov	r1, #0
	mov	r0, r6
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	moveq	r2, r1
	beq	.L2276
	bl	toshiba_3d_set_slc_rr_para
	b	.L2240
.L2231:
	ldr	r3, [sp, #8]
	mov	r5, #1
	add	r9, r3, r8, asl #8
.L2247:
	ldr	r3, .L2277
	mov	r0, r6
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2241
	mov	r1, r5
	mov	r2, #1
	bl	toshiba_tlc_set_rr_para
	mov	r3, #93
	b	.L2275
.L2241:
	uxtb	r1, r5
	bl	toshiba_3d_set_tlc_rr_para
	mov	r3, #38
.L2275:
	str	r3, [r9, #2056]
	mov	r0, r8
	ldr	r3, [sp, #64]
	mov	r1, r10
	ldr	r2, [sp, #12]
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2277+4
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2243
	mov	r3, ip
	ldr	r0, .L2277+16
	mov	r1, r5
	mov	r2, r10
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2243:
	cmn	ip, #1
	beq	.L2244
	ldr	r3, .L2277+12
	cmn	r4, #1
	moveq	r4, ip
	ldr	r2, [r3, #-112]
	ldr	fp, [r3, #-104]
	ldrb	r3, [r7, #1197]	@ zero_extendqisi2
	str	r2, [sp, #12]
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2254
.L2244:
	ldr	r3, [sp, #16]
	add	r5, r5, #1
	cmp	r5, r3
	bne	.L2247
	b	.L2246
.L2254:
	mov	r4, ip
.L2246:
	ldr	r3, .L2277
	mov	r0, r6
	mov	r1, #0
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2248
	mov	r2, #1
.L2276:
	bl	toshiba_tlc_set_rr_para
	b	.L2240
.L2248:
	bl	toshiba_3d_set_tlc_rr_para
.L2240:
	ldr	r3, .L2277
	ldrb	r3, [r3, #1100]	@ zero_extendqisi2
	cmp	r3, #36
	bne	.L2249
	ldr	r3, [sp, #8]
	mov	r2, #85
	add	r3, r3, r8, asl #8
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2048]
	mov	r2, #255
	str	r2, [r3, #2056]
.L2249:
	ldrb	r3, [r7, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2250
	cmn	r4, #1
	movne	r4, #256
.L2250:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2251
	str	r4, [sp]
	mov	r1, r8
	ldr	r0, .L2277+20
	mov	r2, r10
	mov	r3, r5
	bl	printk
.L2251:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2278:
	.align	2
.L2277:
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
	ldr	r4, .L2312
	str	r3, [sp, #20]
	add	r9, r5, #8
	str	r1, [sp, #12]
	str	r2, [sp, #16]
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	ldr	r7, [r4, #1044]
	sub	r3, r3, #67
	cmp	r3, #1
	add	r9, r7, r9, asl #8
	mov	r3, r5, asl #8
	str	r3, [sp, #24]
	movls	r3, #0
	strls	r3, [sp, #8]
	bls	.L2280
	ldrb	r6, [r4, #1143]	@ zero_extendqisi2
	cmp	r6, #0
	streq	r6, [sp, #8]
	beq	.L2281
	mov	r0, #1
	bl	nandc_set_if_mode
	mov	r3, #1
	str	r3, [sp, #8]
.L2281:
	ldr	r3, [sp, #24]
	mov	r2, #92
	add	r3, r7, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L2280:
	ldr	fp, .L2312+4
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r5, asl #8
	str	r3, [sp, #28]
.L2282:
	ldrb	r3, [fp, #-100]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L2311
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	mov	r0, r9
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2283
	bl	sandisk_set_rr_para
	b	.L2284
.L2283:
	bl	toshiba_set_rr_para
.L2284:
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L2285
	ldr	r3, .L2312+4
	ldrb	r3, [r3, #-100]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #28]
	moveq	r2, #179
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L2285:
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
	beq	.L2288
	ldr	r3, [fp, #-112]
	cmn	r10, #1
	moveq	r10, r0
	str	r3, [sp, #16]
	ldr	r3, [fp, #-104]
	str	r3, [sp, #20]
	ldrb	r3, [r4, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2290
.L2288:
	add	r8, r8, #1
	b	.L2282
.L2311:
	mov	r6, r10
.L2290:
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	mov	r0, r9
	mov	r1, #0
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L2292
	bl	sandisk_set_rr_para
	b	.L2293
.L2292:
	bl	toshiba_set_rr_para
.L2293:
	add	r5, r7, r5, asl #8
	mov	r3, #255
	str	r3, [r5, #2056]
	ldrb	r3, [r4, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L2294
	cmn	r6, #1
	movne	r6, #256
.L2294:
	cmn	r6, #1
	cmpne	r6, #256
	bne	.L2295
	str	r6, [sp]
	mov	r1, r8
	ldr	r0, .L2312+8
	mov	r3, r8
	ldr	r2, [sp, #12]
	bl	printk
.L2295:
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L2296
	mov	r0, #4
	bl	nandc_set_if_mode
.L2296:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2313:
	.align	2
.L2312:
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
	ldr	r6, .L2344
	mov	r9, r1
	mov	fp, r3
	mov	r10, r2
	bl	nandc_wait_flash_ready
	mov	r0, r8
	add	r7, r8, #8
	bl	zftl_flash_enter_slc_mode
	mov	r0, r8
	bl	zftl_flash_exit_slc_mode
	ldr	r3, [r6, #1044]
	tst	r9, #50331648
	mvn	r4, #0
	add	r7, r3, r7, asl #8
	mov	r5, #1
	ldr	ip, .L2344+4
	bne	.L2324
.L2319:
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
	beq	.L2316
	ldrb	r3, [r6, #1197]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-112]
	moveq	r4, r0
	ldr	fp, [ip, #-104]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2327
.L2316:
	add	r5, r5, #1
	cmp	r5, #10
	bne	.L2319
	b	.L2318
.L2327:
	mov	r4, r0
.L2318:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_slc_rr_para
	b	.L2320
.L2343:
	ldrb	r3, [r6, #1197]	@ zero_extendqisi2
	cmn	r4, #1
	ldr	r10, [ip, #-112]
	moveq	r4, r0
	ldr	fp, [ip, #-104]
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2328
.L2321:
	add	r5, r5, #1
	cmp	r5, #51
	beq	.L2323
.L2324:
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
	bne	.L2343
	b	.L2321
.L2328:
	mov	r4, r0
.L2323:
	mov	r0, r7
	mov	r1, #0
	bl	ymtc_3d_set_tlc_rr_para
.L2320:
	ldrb	r3, [r6, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2325
	cmn	r4, #1
	movne	r4, #256
.L2325:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2326
	str	r4, [sp]
	mov	r1, r5
	ldr	r0, .L2344+8
	mov	r2, r9
	mov	r3, r5
	bl	printk
.L2326:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2345:
	.align	2
.L2344:
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
	ldr	r3, .L2384
	tst	r8, #50331648
	ldr	r4, [r3, #1044]
	str	r3, [sp, #16]
	bne	.L2347
	mov	r10, r7, asl #8
	mvn	r5, #0
	add	r9, r4, r10
	mov	r6, #1
	mov	fp, #239
.L2352:
	str	fp, [r9, #2056]
	mov	r3, #141
	str	r3, [r9, #2052]
	ldr	r3, .L2384+4
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
	ldr	r3, .L2384+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2348
	mov	r3, ip
	ldr	r0, .L2384+12
	mov	r1, r6
	mov	r2, r8
	str	ip, [sp, #20]
	bl	printk
	ldr	ip, [sp, #20]
.L2348:
	cmn	ip, #1
	beq	.L2349
	ldr	r3, .L2384+16
	cmn	r5, #1
	moveq	r5, ip
	ldr	r2, [r3, #-112]
	ldr	r3, [r3, #-104]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bcc	.L2361
.L2349:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L2352
	b	.L2351
.L2361:
	mov	r5, ip
.L2351:
	add	r10, r4, r10
	mov	r3, #239
	add	r4, r4, r7, asl #8
	str	r3, [r10, #2056]
	mov	r3, #141
	b	.L2383
.L2347:
	mov	r3, r7, asl #8
	ldr	r9, .L2384+20
	add	r10, r4, r3
	mvn	r5, #0
	mov	r6, #1
	str	r3, [sp, #20]
.L2358:
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
	ldr	r3, .L2384+8
	ldr	r3, [r3]
	tst	r3, #16
	mov	fp, r0
	beq	.L2354
	ldr	r0, .L2384+24
	mov	r1, r6
	mov	r2, r8
	mov	r3, fp
	bl	printk
.L2354:
	cmn	fp, #1
	beq	.L2355
	ldr	r3, .L2384+16
	cmn	r5, #1
	moveq	r5, fp
	ldr	r2, [r3, #-112]
	ldr	r3, [r3, #-104]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	fp, r3, asr #2
	bcc	.L2362
.L2355:
	add	r6, r6, #1
	add	r9, r9, #4
	cmp	r6, #26
	bne	.L2358
	b	.L2357
.L2362:
	mov	r5, fp
.L2357:
	ldr	r3, [sp, #20]
	mov	r2, #239
	add	r3, r4, r3
	add	r4, r4, r7, asl #8
	str	r2, [r3, #2056]
	mov	r3, #137
.L2383:
	str	r3, [r4, #2052]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	nandc_wait_flash_ready
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, asr #2
	bcc	.L2359
	cmn	r5, #1
	movne	r5, #256
.L2359:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L2360
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L2384+28
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2360:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2385:
	.align	2
.L2384:
	.word	.LANCHOR0
	.word	.LANCHOR1+1808
	.word	.LANCHOR2
	.word	.LC157
	.word	.LANCHOR3
	.word	.LANCHOR1+1836
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
	ldr	r3, .L2405
	.pad #28
	sub	sp, sp, #28
	mov	r10, r2
	mov	r6, r0
	mov	r8, r1
	mov	r7, #0
	ldr	r2, [r3, #1040]
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
	ldr	ip, .L2405+4
.L2387:
	cmp	r7, r9
	bcs	.L2391
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
	beq	.L2389
	ldr	r3, [sp, #16]
	cmn	r4, #1
	ldr	r10, [ip, #-112]
	moveq	r4, r0
	ldr	fp, [ip, #-104]
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L2395
.L2389:
	add	r7, r7, #1
	b	.L2387
.L2395:
	mov	r4, r0
.L2391:
	ldr	r3, [sp, #12]
	strb	r5, [r3, #120]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L2393
	cmn	r4, #1
	movne	r4, #256
.L2393:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L2394
	str	r4, [sp]
	mov	r1, r7
	ldr	r0, .L2405+8
	mov	r2, r8
	mov	r3, r7
	bl	printk
.L2394:
	bl	nandc_wait_flash_ready
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2406:
	.align	2
.L2405:
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
.L2413:
	uxtb	r0, r7
	bl	nandc_set_ddr_para
	mov	r0, r8
	mov	r1, r10
	mov	r2, fp
	ldr	r3, [sp, #72]
	str	r3, [sp]
	ldr	r3, [sp, #16]
	bl	flash_read_page
	ldr	r3, .L2434
	ldr	r3, [r3]
	tst	r3, #16
	mov	ip, r0
	beq	.L2408
	mov	r3, ip
	ldr	r0, .L2434+4
	mov	r1, r7
	mov	r2, r10
	str	ip, [sp, #28]
	bl	printk
	ldr	ip, [sp, #28]
.L2408:
	add	r3, r6, #1
	cmp	ip, r3
	bhi	.L2409
	ldr	r3, .L2434+8
	ldr	fp, [r3, #-112]
	ldr	r3, [r3, #-104]
	str	r3, [sp, #16]
	ldr	r3, .L2434+12
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	cmp	ip, r3, lsr #2
	bcs	.L2419
	add	r5, r5, #1
	cmp	r5, #7
	bls	.L2419
	rsb	r4, r5, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2411
.L2409:
	ldr	r3, [sp, #12]
	cmp	r3, r5
	bcs	.L2420
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #20]
	bhi	.L2412
	str	r5, [sp, #12]
	b	.L2420
.L2419:
	mov	r4, r7
	mov	r6, ip
	mov	r9, #0
	b	.L2410
.L2420:
	mov	r5, #0
.L2410:
	add	r7, r7, #2
	cmp	r7, #50
	bne	.L2413
.L2411:
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r2, r5
	movhi	r4, r3
.L2412:
	cmp	r4, #0
	beq	.L2414
	ldr	r3, .L2434+12
	ldr	r2, .L2434+16
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	umull	r2, r3, r3, r2
	ubfx	r3, r3, #1, #8
	cmp	r6, r3
	bcs	.L2414
	ldr	r0, .L2434+20
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	b	.L2433
.L2414:
	ldrb	r0, [sp, #24]	@ zero_extendqisi2
.L2433:
	bl	nandc_set_ddr_para
	cmn	r9, #1
	bne	.L2416
	ldr	r5, .L2434+12
	ldrb	r3, [r5, #1196]	@ zero_extendqisi2
	mov	r4, r5
	tst	r3, #1
	beq	.L2416
	mov	r1, r8
	mov	r2, r10
	ldr	r0, .L2434+24
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
	strb	r2, [r3, #1156]
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
	ldr	r0, .L2434+28
	mov	r3, r6
	bl	printk
	ldrb	r3, [r5, #1197]	@ zero_extendqisi2
	cmp	r6, r3
	bhi	.L2421
	ldr	r2, .L2434+8
	ldr	r3, [r2, #-96]
	add	r3, r3, #1
	str	r3, [r2, #-96]
	cmp	r3, #100
	movhi	r3, #0
	strhib	r3, [r5, #1143]
	bls	.L2417
	b	.L2416
.L2421:
	mov	r6, r9
.L2417:
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	bl	nandc_set_if_mode
.L2416:
	mov	r0, r6
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2435:
	.align	2
.L2434:
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
	ldr	r5, .L2467
	mov	r10, r0
	mov	r4, r1
	mov	r8, r2
	ldr	r9, [sp, #48]
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2437
	ldr	r1, .L2467+4
	movw	r2, #431
	ldr	r0, .L2467+8
	bl	printk
	bl	dump_stack
.L2437:
	add	r2, r5, r10
	ldr	r3, .L2467
	ldrb	r6, [r2, #1144]	@ zero_extendqisi2
	ldrb	r2, [r5, #1109]	@ zero_extendqisi2
	cmp	r10, r2
	bcc	.L2438
	ldr	r3, .L2467+12
	ldr	r3, [r3]
	tst	r3, #64
	beq	.L2466
	str	r2, [sp]
	mov	r1, r6
	ldr	r0, .L2467+16
	mov	r2, r10
	mov	r3, r4
	bl	printk
.L2466:
	mvn	r0, #0
	b	.L2463
.L2438:
	tst	r4, #50331648
	bne	.L2441
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2442
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2441
.L2442:
	ldrh	r10, [r5, #2]
	mov	r0, r4
	mov	r1, r10
	bl	__aeabi_uidiv
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L2467
	mul	r0, r10, r0
	rsb	r4, r0, r4
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
.L2441:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	cmn	r0, #1
	bne	.L2463
	ldrb	fp, [r5, #1200]	@ zero_extendqisi2
	ldr	r10, .L2467
	cmp	fp, #0
	bne	.L2445
.L2448:
	ldr	r3, .L2467+20
	ldr	ip, [r3, #-92]
	cmp	ip, #0
	bne	.L2446
	b	.L2447
.L2445:
	mov	r3, #0
	mov	r0, r6
	strb	r3, [r10, #1200]
	mov	r1, r4
	str	r9, [sp]
	mov	r2, r8
	mov	r3, r7
	bl	flash_read_page
	strb	fp, [r10, #1200]
	cmn	r0, #1
	beq	.L2448
	b	.L2463
.L2446:
	str	r9, [sp]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	blx	ip
	cmn	r0, #1
	bne	.L2463
.L2447:
	ldrb	r3, [r5, #1200]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2467+24
	mov	r2, r4
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2466
	str	r9, [sp, #48]
	mov	r0, r6
	mov	r1, r4
	mov	r2, r8
	mov	r3, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	flash_ddr_tuning_read
.L2463:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2468:
	.align	2
.L2467:
	.word	.LANCHOR0
	.word	.LANCHOR1+1940
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
	ldr	r6, .L2480
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L2480+4
	mov	r10, r0
	mov	fp, r2
	mov	r8, r3
	ldrh	r7, [r6, #30]
	ldrh	r4, [r1, #-216]
	ldr	r1, [sp, #56]
	sub	r4, r4, #1
	mul	r7, r7, r9
	uxth	r4, r4
	str	r1, [sp]
	sxtah	r1, r7, r4
	bl	flash_read_page_en
	cmp	r0, #512
	moveq	ip, #0
	bne	.L2470
.L2471:
	sxth	r3, ip
	sxth	r2, r4
	cmp	r3, r2
	bgt	.L2470
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
	b	.L2471
.L2470:
	ldr	r3, [r6]
	sxth	r4, r4
	tst	r3, #4096
	beq	.L2475
	ldr	r0, .L2480+8
	mov	r1, r9
	mov	r2, r4
	ldr	r3, [r8]
	bl	printk
.L2475:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2481:
	.align	2
.L2480:
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
	ldr	r2, .L2484
	mov	r4, r1
	ldr	r1, .L2484+4
	mov	ip, r0
	ldrb	r2, [r2, #1153]	@ zero_extendqisi2
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
.L2485:
	.align	2
.L2484:
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
	ldr	r4, .L2493
	mov	r7, r1
	mov	r6, #1
	ldr	r5, .L2493+4
	mov	r9, #4
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	strb	r6, [r4, #1143]
	bl	flash_set_interface_mode
	ldrb	r0, [r4, #1196]	@ zero_extendqisi2
	bl	nandc_set_if_mode
	mov	r1, r7
	ldr	r2, [r5, #-88]
	mov	r0, r8
	ldr	r3, [r5, #-84]
	str	r9, [sp]
	bl	flash_ddr_tuning_read
	ldr	r3, [r5, #-84]
	mov	r0, r8
	mov	r1, r7
	ldr	r2, [r5, #-88]
	str	r9, [sp]
	bl	flash_read_page
	mov	r3, r4
	cmn	r0, #1
	bne	.L2487
	ldrb	r2, [r4, #1196]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L2487
	mov	r0, r6
	bl	flash_set_interface_mode
	mov	r0, r6
	bl	nandc_set_if_mode
	mov	r3, #0
	strb	r3, [r4, #1143]
	b	.L2488
.L2487:
	mov	r2, #1
	strb	r2, [r3, #1143]
.L2488:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2494:
	.align	2
.L2493:
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
	ldr	r3, .L2526
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #124
	sub	sp, sp, #124
	ldr	r4, .L2526+4
	mov	r6, r2
	str	r1, [sp, #24]
	mov	r5, #0
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	mov	r7, #4
	ldrh	r1, [r3, #30]
	mov	r10, r5
	ldrb	r3, [r4, #1200]	@ zero_extendqisi2
	str	r0, [sp, #40]
	mul	r1, r1, r2
	str	r3, [sp, #44]
	str	r2, [sp, #28]
	strb	r5, [r4, #1200]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #40]
	str	r1, [sp, #12]
	rsb	r3, r1, r3
	str	r3, [sp, #32]
	and	r3, r1, #3
	str	r3, [sp, #16]
.L2496:
	ldr	r3, [sp, #24]
	cmp	r5, r3
	bcs	.L2525
	ldr	r3, [sp, #16]
	mov	r1, r7
	rsb	r3, r3, r7
	uxth	r3, r3
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	add	r0, r5, r3
	bl	__aeabi_uidiv
	ldrb	r1, [r4, #1110]	@ zero_extendqisi2
	cmp	r1, #0
	uxth	r0, r0
	mov	r3, r0, asl #1
	add	r2, r4, r3
	ldrh	r2, [r2, #4]
	moveq	r0, r2
	beq	.L2498
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r0, r3
.L2498:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #16]
	mla	r3, r3, r0, r2
	str	r3, [sp, #36]
	ldr	r3, .L2526+4
	ldr	r2, [sp, #36]
	ldrb	r9, [r3, #1108]	@ zero_extendqisi2
	add	r0, r2, r1
	ldr	r3, [r3, #1104]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, .L2526+4
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	mov	fp, r0
	mov	r0, r9
	str	r3, [sp, #48]
	bl	nandc_bch_sel
.L2499:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_read_page
	cmn	r0, #1
	mov	r8, r0
	bne	.L2506
	ldrb	ip, [r4, #1200]	@ zero_extendqisi2
	cmp	ip, #0
	bne	.L2501
.L2504:
	ldr	r3, .L2526+8
	ldr	ip, [r3, #-92]
	cmp	ip, #0
	bne	.L2502
	b	.L2503
.L2501:
	mov	r0, #0
	str	r7, [sp]
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	str	ip, [sp, #52]
	strb	r0, [r4, #1200]
	bl	flash_read_page
	ldr	ip, [sp, #52]
	cmn	r0, #1
	strb	ip, [r4, #1200]
	beq	.L2504
	b	.L2524
.L2502:
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	blx	ip
	cmn	r0, #1
	bne	.L2524
.L2503:
	ldrb	r3, [r4, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2506
	str	r7, [sp]
	mov	r0, #0
	mov	r1, fp
	mov	r2, r6
	add	r3, sp, #56
	bl	flash_ddr_tuning_read
.L2524:
	mov	r8, r0
.L2506:
	subs	r9, r9, #16
	movne	r9, #1
	cmn	r8, #1
	movne	r9, #0
	cmp	r9, #0
	beq	.L2507
	mov	r0, #16
	mov	r9, #16
	bl	nandc_bch_sel
	b	.L2499
.L2507:
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
	beq	.L2509
	ldr	r3, [r6]
	ldr	r2, .L2526+12
	cmp	r3, r2
	bne	.L2509
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #20]
	ldrb	r7, [r6, #17]	@ zero_extendqisi2
	add	r3, r3, r2
	str	r3, [sp, #24]
	b	.L2510
.L2509:
	ldr	r3, [sp, #20]
	mov	r2, r5
	ldr	r0, .L2526+16
	ldr	r1, [sp, #36]
	add	r6, r6, r3, asl #9
	ldr	r3, [sp, #60]
	str	r3, [sp]
	ldr	r3, [sp, #56]
	bl	printk
	mov	r3, #0
	str	r3, [sp, #16]
.L2510:
	ldr	r3, [sp, #20]
	add	r5, r3, r5
	uxth	r5, r5
	b	.L2496
.L2525:
	ldr	r3, .L2526+4
	mov	r0, r10
	ldrb	r2, [sp, #44]	@ zero_extendqisi2
	strb	r2, [r3, #1200]
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2527:
	.align	2
.L2526:
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
	ldr	r7, .L2540
	mov	r4, r0
	.pad #20
	sub	sp, sp, #20
	mvn	r5, #0
	mov	r8, r1
	mov	r10, r2
	ldrb	r3, [r7, #1153]	@ zero_extendqisi2
	add	r9, r0, #8
	ldr	r6, [r7, #1044]
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
	bne	.L2529
	bl	zftl_flash_enter_slc_mode
	b	.L2530
.L2529:
	bl	zftl_flash_exit_slc_mode
.L2530:
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
	ldrb	r2, [r7, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r5, lsr #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2540+4
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
	beq	.L2532
	ldr	r0, .L2540+8
	mov	r1, r8
	bl	printk
	mvn	r2, #0
.L2532:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2541:
	.align	2
.L2540:
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
	ldr	r4, .L2553
	mov	r5, r1
	mov	r2, #32
	mov	r1, #165
	ldr	r0, [r4, #-112]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r4, #-104]
	bl	ftl_memset
	ldr	r3, .L2553+4
	mov	r0, r6
	ldrh	r3, [r3, #2]
	mul	r5, r3, r5
	mov	r1, r5
	bl	flash_erase_block
	cmn	r0, #1
	bne	.L2543
.L2545:
	mvn	r4, #0
	b	.L2544
.L2543:
	ldr	r7, .L2553+8
	mov	r0, r6
	mov	r1, r5
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r2, [r4, #-112]
	ldr	r3, [r4, #-104]
	bl	flash_prog_page
	cmn	r0, #1
	beq	.L2545
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r6
	mov	r1, r5
	str	r3, [sp]
	ldr	r2, [r4, #-112]
	ldr	r3, [r4, #-104]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L2545
	ldr	r3, [r4, #-112]
	ldr	r2, [r3]
	ldr	r3, .L2553+12
	cmp	r2, r3
	bne	.L2545
	ldr	r3, [r4, #-104]
	ldr	r4, .L2553+16
	ldr	r3, [r3]
	subs	r4, r3, r4
	mvnne	r4, #0
.L2544:
	mov	r0, r6
	mov	r1, r5
	bl	flash_erase_block
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2554:
	.align	2
.L2553:
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
	ldr	r6, .L2565
	mov	r0, r3
	ldr	r5, [sp, #40]
	mov	r9, r3
	mov	r8, r2
	mov	r7, r9, asl #8
	ldr	r4, [r6, #1044]
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
	ldrb	r3, [r6, #1152]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, r5, lsr #24
	strne	r3, [r2, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2565+4
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
.L2566:
	.align	2
.L2565:
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
	ldr	r4, .L2580
	add	r8, r0, #8
	ldr	r6, .L2580+4
	.pad #20
	sub	sp, sp, #20
	mov	r5, r0
	mov	r7, r1
	ldr	r3, [r4, #1044]
	mov	r10, r2
	ubfx	fp, r1, #24, #2
	add	r8, r3, r8, asl #8
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
	mvn	r4, #0
	rsb	r3, r3, #24
	bic	r4, r1, r4, asl r3
	ldr	r3, [r6]
	tst	r3, #16
	beq	.L2568
	ldr	r0, .L2580+8
	mov	r2, fp
	ldr	r3, [sp, #64]
	bl	printk
.L2568:
	bl	nandc_wait_flash_ready
	mov	r0, r5
	bl	nandc_cs
	cmp	fp, #0
	mov	r0, r5
	bne	.L2569
	bl	zftl_flash_enter_slc_mode
	b	.L2570
.L2569:
	bl	zftl_flash_exit_slc_mode
.L2570:
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
	beq	.L2571
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L2572
	ldr	r0, .L2580+12
	mov	r1, r7
	bl	printk
.L2572:
	mvn	r2, #0
.L2571:
	mov	r0, r2
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2581:
	.align	2
.L2580:
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
	ldr	r4, .L2589
	mov	r6, #1
	add	r7, r0, #8
	mov	r5, r0
	mov	r9, r2
	mov	r10, r1
	ldr	r3, [r4, #1044]
	ldrb	r4, [r4, #1153]	@ zero_extendqisi2
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
	beq	.L2583
	ldr	r3, .L2589+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2584
	ldr	r0, .L2589+8
	mov	r1, r10
	bl	printk
.L2584:
	mvn	r2, #0
.L2583:
	mov	r0, r2
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2590:
	.align	2
.L2589:
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
	ldr	r4, .L2597
	.pad #20
	sub	sp, sp, #20
	mov	r5, r2
	ldr	r3, [r4, #1104]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldrb	r8, [r4, #1197]	@ zero_extendqisi2
	mov	r7, r0
	ldrb	r0, [r4, #1108]	@ zero_extendqisi2
	bl	nandc_bch_sel
	ldr	r3, .L2597+4
	ldrb	r2, [r3, #11]	@ zero_extendqisi2
	cmp	r2, #9
	bne	.L2592
	ldrb	r4, [r4, #1110]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L2592
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2593
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	ymtc_flash_tlc_page_prog
	b	.L2596
.L2593:
	ldr	r9, .L2597+8
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r9, #-112]
	bl	ftl_memset
	ldr	r3, [r9, #-112]
	mov	r0, r4
	mov	r1, r7
	mov	r2, r6
	str	r3, [sp]
	str	r3, [sp, #4]
	mov	r3, #4
	str	r3, [sp, #8]
	mov	r3, r5
	bl	flash_dual_page_prog
	b	.L2596
.L2592:
	mov	r3, #4
	mov	r0, #0
	str	r3, [sp]
	mov	r1, r7
	mov	r2, r6
	mov	r3, r5
	bl	flash_prog_page
.L2596:
	mov	r4, r0
	mov	r0, r8
	bl	nandc_bch_sel
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2598:
	.align	2
.L2597:
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
	ldr	r3, .L2607
	mov	r8, r2
	add	r5, sp, #48
	mov	fp, r0
	mov	r7, r1
	ldrb	r2, [r3, #1109]	@ zero_extendqisi2
	mov	r6, r3
	ldmia	r5, {r5, r9, r10}
	cmp	r2, r4
	bhi	.L2600
	ldr	r1, .L2607+4
	mov	r2, #868
	ldr	r0, .L2607+8
	bl	printk
	bl	dump_stack
.L2600:
	ldrb	r2, [r6, #1109]	@ zero_extendqisi2
	ldr	r3, .L2607
	cmp	r2, r4
	bls	.L2599
	add	r4, r3, r4
	ldrb	r6, [r4, #1144]	@ zero_extendqisi2
	ldr	r4, [r3, #1044]
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
	ldr	r3, .L2607+12
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
.L2599:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2608:
	.align	2
.L2607:
	.word	.LANCHOR0
	.word	.LANCHOR1+1960
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
	ldr	r2, .L2614
	mov	r6, r0
	ldr	r3, [r0]
	mov	r7, r1
	ldrb	r4, [r2, #1153]	@ zero_extendqisi2
	ldr	r0, [r3, #24]
	rsb	r1, r4, #24
	ldrb	r2, [r2, #1161]	@ zero_extendqisi2
	mov	r4, r8, asl r4
	mov	r5, r8, asl r1
	sub	r4, r4, #1
	cmp	r2, #0
	and	r4, r4, r0, lsr r1
	sub	r5, r5, #1
	and	r5, r5, r0
	uxtb	r4, r4
	beq	.L2610
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
	b	.L2611
.L2610:
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
.L2611:
	cmp	r7, #0
	beq	.L2609
	ldr	r1, [r6]
	mov	r3, #4
	ldr	r0, .L2614+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	buf_add_tail
.L2609:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2615:
	.align	2
.L2614:
	.word	.LANCHOR0
	.word	.LANCHOR0+2774
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
	ldr	r3, .L2620
	mov	r5, r0
	ldr	r1, [r0, #24]
	ldrb	r4, [r3, #1153]	@ zero_extendqisi2
	ldrb	r8, [r3, #1161]	@ zero_extendqisi2
	rsb	r2, r4, #24
	mov	r4, r7, asl r4
	sub	r4, r4, #1
	mov	r6, r7, asl r2
	and	r4, r4, r1, lsr r2
	cmp	r8, #0
	sub	r6, r6, #1
	and	r6, r6, r1
	uxtb	r4, r4
	beq	.L2617
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
	b	.L2618
.L2617:
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
.L2618:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	r0, [r5, #24]
	mov	r1, #64
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	flash_wait_device_ready
.L2621:
	.align	2
.L2620:
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
	ldr	r3, .L2626
	mov	r5, r2
	mov	r7, r0
	mov	r4, r1
	ldrb	r2, [r3, #1109]	@ zero_extendqisi2
	mov	r6, r3
	cmp	r2, r1
	bhi	.L2623
	ldr	r1, .L2626+4
	movw	r2, #903
	ldr	r0, .L2626+8
	bl	printk
	bl	dump_stack
.L2623:
	ldrb	r2, [r6, #1109]	@ zero_extendqisi2
	ldr	r3, .L2626
	cmp	r2, r4
	bls	.L2622
	add	r4, r3, r4
	ldrb	r6, [r4, #1144]	@ zero_extendqisi2
	ldr	r4, [r3, #1044]
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
	ldr	r3, .L2626+12
	mov	r0, r6
	mov	r1, #1
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	str	r3, [sp]
	mov	r3, r8
	bl	nandc_xfer
	str	r7, [r4, #2056]
.L2622:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2627:
	.align	2
.L2626:
	.word	.LANCHOR0
	.word	.LANCHOR1+1988
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
	ldr	r3, .L2634
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	mov	r4, #1
	ldrb	r1, [r3, #1153]	@ zero_extendqisi2
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
	ldr	r3, .L2634+4
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L2629
	ldr	r2, [r5, #24]
	mov	r1, r6
	ldr	r0, .L2634+8
	add	r3, r2, #1
	bl	printk
.L2629:
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
.L2635:
	.align	2
.L2634:
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
	ldr	r6, .L2651
	mvn	r3, #0
	mov	r10, r2
	.pad #20
	sub	sp, sp, #20
	mov	r8, r1
	ldrb	r4, [r6, #1153]	@ zero_extendqisi2
	ldrb	r2, [r6, #1109]	@ zero_extendqisi2
	rsb	r7, r4, #24
	str	r0, [sp, #8]
	mvn	r4, r3, asl r4
	and	r5, r4, r1, lsr r7
	uxtb	r9, r5
	cmp	r2, r9
	bhi	.L2637
	ldr	r1, .L2651+4
	mov	r2, #956
	ldr	r0, .L2651+8
	bl	printk
	bl	dump_stack
.L2637:
	ldrb	r1, [r6, #1109]	@ zero_extendqisi2
	ldr	r2, .L2651
	cmp	r1, r9
	bls	.L2636
	add	r1, r2, r9
	mvn	r4, #0
	str	r2, [sp, #12]
	bic	r4, r8, r4, asl r7
	ldrb	r5, [r1, #1144]	@ zero_extendqisi2
	ldr	r7, [r2, #1044]
	bl	nandc_rdy_status
	cmp	r0, #0
	ldr	r2, [sp, #12]
	bne	.L2639
	ldrb	r2, [r2, #1109]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2640
	bl	nandc_wait_flash_ready
	b	.L2639
.L2640:
	mov	r0, r9
	mov	r1, r4
	mov	r2, #64
	bl	flash_wait_device_ready_raw
.L2639:
	mov	r0, r5
	bl	hynix_reconfig_rr_para
	mov	r0, r5
	bl	nandc_cs
	tst	r8, #50331648
	bne	.L2641
	mov	r0, r4
	bl	slc_phy_page_address_calc
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r4, r0
	beq	.L2642
	mov	r0, r5
	bl	zftl_flash_enter_slc_mode
	b	.L2642
.L2641:
	mov	r0, r5
	bl	zftl_flash_exit_slc_mode
.L2642:
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
	ldrb	r2, [r6, #1152]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r2, r4, lsr #24
	strne	r2, [r3, #2052]
	bl	nandc_set_seed
	ldr	r3, .L2651+12
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
.L2636:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2652:
	.align	2
.L2651:
	.word	.LANCHOR0
	.word	.LANCHOR1+2020
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
	ldr	r2, .L2662
	ldr	r0, [r4, #24]
	ldrb	r3, [r2, #2774]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2654
	ldrb	ip, [r2, #1153]	@ zero_extendqisi2
	mvn	r1, #0
	mov	r5, #48
	rsb	r6, ip, #24
	mvn	r1, r1, asl ip
	uxth	r1, r1
	and	r0, r1, r0, asr r6
.L2656:
	mla	lr, r5, r3, r2
	ldr	ip, [lr, #1260]
	and	ip, r1, ip, lsr r6
	cmp	r0, ip
	bne	.L2655
	add	ip, lr, #1264
	ldrb	lr, [lr, #1278]	@ zero_extendqisi2
	add	ip, ip, #12
	cmp	lr, #7
	moveq	r3, #3
	streqb	r3, [ip, #2]
	beq	.L2654
.L2655:
	mla	r3, r5, r3, r2
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2656
.L2654:
	mov	r3, #3
	mov	r1, r4
	strb	r3, [r4, #42]
	mov	r3, #1
	ldr	r0, .L2662+4
	strb	r3, [r4, #43]
	mvn	r3, #0
	strb	r3, [r4]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	buf_add_tail
.L2663:
	.align	2
.L2662:
	.word	.LANCHOR0
	.word	.LANCHOR0+2774
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
	ldr	r8, .L2694
	mov	r10, r0
	mov	r6, r1
	mov	r7, r2
	ldrb	r4, [r8, #1153]	@ zero_extendqisi2
	rsb	r9, r4, #24
	mvn	r4, r3, asl r4
	and	r4, r4, r0, lsr r9
	ldrb	r3, [r8, #1109]	@ zero_extendqisi2
	uxtb	r5, r4
	cmp	r3, r5
	bhi	.L2665
	ldr	r1, .L2694+4
	movw	r2, #1070
	ldr	r0, .L2694+8
	bl	printk
	bl	dump_stack
.L2665:
	ldrb	r2, [r8, #1109]	@ zero_extendqisi2
	ldr	r3, .L2694
	cmp	r2, r5
	mvnls	r0, #0
	bls	.L2666
	add	r5, r3, r5
	mvn	r4, #0
	bic	r4, r10, r4, asl r9
	ubfx	r10, r10, #24, #2
	ldrb	r9, [r5, #1144]	@ zero_extendqisi2
	ldr	r5, [r3, #1044]
	mov	r0, r9
	bl	nandc_cs
	cmp	r10, #0
	bne	.L2667
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2667:
	ldrb	r3, [r8, #1127]	@ zero_extendqisi2
	uxtb	ip, r4
	mov	r0, r4, lsr #8
	ldrb	r1, [r8, #1152]	@ zero_extendqisi2
	cmp	r3, #1
	mov	r3, r9, asl #8
	addeq	r2, r5, r3
	moveq	lr, #6
	beq	.L2693
	ldr	r2, .L2694
	ldr	r2, [r2, #1104]
	ldrb	r2, [r2, #12]	@ zero_extendqisi2
	cmp	r2, #3
	add	r2, r5, r3
	bne	.L2671
	mov	lr, #5
.L2693:
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
	b	.L2670
.L2671:
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
.L2670:
	ldr	r2, [r8, #1104]
	ldr	r3, .L2694
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r10, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2674
	ldrb	r2, [r3, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2674
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r10, #1
	addeq	r4, r4, r4, asl #1
	addeq	r0, r0, r4
	beq	.L2690
.L2674:
	mov	r0, r4
.L2690:
	bl	nandc_set_seed
	ldr	r3, .L2694+12
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
.L2666:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2695:
	.align	2
.L2694:
	.word	.LANCHOR0
	.word	.LANCHOR1+2044
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
	ldr	r5, .L2731
	.pad #20
	sub	sp, sp, #20
	mov	r9, r0
	mov	r10, r1
	mov	fp, r2
	ldrb	r7, [r5, #1153]	@ zero_extendqisi2
	rsb	r3, r7, #24
	mov	r7, r6, asl r7
	sub	r7, r7, #1
	mov	r4, r6, asl r3
	and	r7, r7, r0, lsr r3
	ldrb	r3, [r5, #1109]	@ zero_extendqisi2
	sub	r4, r4, #1
	ubfx	r6, r0, #24, #2
	uxtb	r7, r7
	and	r4, r4, r0
	cmp	r3, r7
	bhi	.L2697
	ldr	r1, .L2731+4
	mov	r2, #1232
	ldr	r0, .L2731+8
	bl	printk
	bl	dump_stack
.L2697:
	add	r7, r5, r7
	ldr	r3, [r5, #1044]
	ldrb	r8, [r7, #1144]	@ zero_extendqisi2
	str	r3, [sp, #12]
	mov	r0, r8
	bl	nandc_cs
	cmp	r6, #0
	bne	.L2698
	mov	r0, r4
	bl	slc_phy_page_address_calc
	mov	r4, r0
.L2698:
	ldr	r7, .L2731+12
	ldrb	r3, [r7, #16]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L2699
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
.L2699:
	ldr	r2, [r5, #1104]
	ldr	r3, .L2731
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	adds	r2, r6, #0
	movne	r2, #1
	cmp	r1, #3
	movne	r2, #0
	cmp	r2, #0
	beq	.L2700
	ldrb	r2, [r3, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2700
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r0, r6, #1
	addeq	r3, r4, r4, asl #1
	addeq	r0, r0, r3
	beq	.L2730
.L2700:
	mov	r0, r4
.L2730:
	bl	nandc_set_seed
	ldrb	r2, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	mov	r1, #0
	mov	r3, r10
	str	fp, [sp]
	bl	nandc_xfer
	cmn	r0, #1
	bne	.L2702
	ldrb	ip, [r5, #1200]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L2703
	ldr	r2, .L2731
	mov	r3, #0
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	str	ip, [sp, #12]
	strb	r3, [r2, #1200]
	mov	r2, r10
	ldr	r3, .L2731+12
	ldrb	r3, [r3, #13]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, fp
	bl	flash_read_page
	ldr	r3, .L2731
	cmp	r6, #0
	ldr	ip, [sp, #12]
	strb	ip, [r3, #1200]
	bne	.L2704
.L2711:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2704
	ldr	r3, .L2731
	ldrb	r3, [r3, #1197]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	blt	.L2704
	ldrb	r3, [r7, #23]	@ zero_extendqisi2
	sub	r3, r3, #4
	cmp	r3, #4
	movls	r0, #256
	b	.L2718
.L2704:
	cmn	r0, #1
	bne	.L2718
.L2712:
	ldr	r3, .L2731+16
	ldr	ip, [r3, #-92]
	cmp	ip, #0
	bne	.L2707
.L2710:
	ldrb	r3, [r5, #1200]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, .L2731+20
	mov	r2, r9
	str	r3, [sp]
	mvn	r3, #0
	bl	printk
	ldrb	r3, [r5, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	mvneq	r0, #0
	beq	.L2718
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	bl	flash_ddr_tuning_read
	b	.L2718
.L2707:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	mov	r0, r8
	orr	r1, r4, r6, asl #24
	mov	r2, r10
	str	r3, [sp]
	mov	r3, fp
	blx	ip
	cmn	r0, #1
	bne	.L2718
	b	.L2710
.L2702:
	cmp	r6, #0
	beq	.L2711
	b	.L2718
.L2703:
	cmp	r6, #0
	beq	.L2711
	b	.L2712
.L2718:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2732:
	.align	2
.L2731:
	.word	.LANCHOR0
	.word	.LANCHOR1+2076
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
	ldr	r5, .L2824
	ldrb	r4, [r5, #2774]	@ zero_extendqisi2
	cmp	r4, #255
	beq	.L2767
	mov	r7, #48
	mla	r2, r7, r4, r5
	ldrb	r3, [r2, #1278]	@ zero_extendqisi2
	ldr	r6, [r2, #1260]
	sub	r2, r3, #1
	cmp	r2, #10
	ldrls	pc, [pc, r2, asl #2]
	b	.L2767
.L2737:
	.word	.L2736
	.word	.L2738
	.word	.L2739
	.word	.L2739
	.word	.L2739
	.word	.L2739
	.word	.L2740
	.word	.L2741
	.word	.L2742
	.word	.L2739
	.word	.L2742
.L2736:
	mla	r7, r7, r4, r5
	bl	nandc_wait_flash_ready
	ldr	r3, [r7, #1244]
	ldr	r1, [r7, #1240]
	cmp	r3, #0
	beq	.L2743
	ldr	r2, .L2824+4
	ldrb	r0, [r7, #1276]	@ zero_extendqisi2
	ldrb	r2, [r2, #-2538]	@ zero_extendqisi2
	cmp	r0, r2
	moveq	r1, r3
.L2743:
	mov	r3, #48
	mov	r0, r6
	mla	r5, r3, r4, r5
	ldr	r2, [r5, #1248]
	bl	flash_complete_page_read
	str	r0, [r5, #1272]
	b	.L2822
.L2738:
	bl	nandc_wait_flash_ready
	mla	r3, r7, r4, r5
	ldrb	r6, [r3, #1236]	@ zero_extendqisi2
	ldr	r2, [r3, #1244]
	ldr	r10, [r3, #1240]
	mla	r7, r7, r6, r5
	cmp	r2, #0
	ldr	r8, [r7, #1240]
	beq	.L2744
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2824+4
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r10, r2
.L2744:
	mov	r3, #48
	mla	r3, r3, r6, r5
	ldr	r2, [r3, #1244]
	cmp	r2, #0
	beq	.L2745
	ldrb	r1, [r3, #1276]	@ zero_extendqisi2
	ldr	r3, .L2824+4
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	cmp	r1, r3
	moveq	r8, r2
.L2745:
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
	beq	.L2746
	ldr	r3, [sp, #12]
	ldr	r3, [r3, #1256]
	cmn	r3, #1
	beq	.L2747
	ldr	r2, [fp, #4]
	ldr	r2, [r2, #4]
	cmp	r3, r2
	beq	.L2747
.L2746:
	mov	fp, #48
	ldr	lr, .L2824+4
	mla	fp, fp, r4, r5
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	ldrb	lr, [lr, #-2538]	@ zero_extendqisi2
	mvn	r0, #0
	rsb	r1, r3, #24
	mvn	r3, r0, asl r3
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
	beq	.L2748
	ldr	ip, [sp, #12]
	ldr	r3, [ip, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2748
	ldr	r0, .L2824+8
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2748
	str	r1, [sp]
	ldr	r0, .L2824+12
	ldr	r1, [fp, #1260]
	ldr	r3, [r3]
	bl	printk
.L2748:
	mov	r3, #48
	mla	r3, r3, r4, r5
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2747
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2747
	ldr	r1, .L2824+16
	movw	r2, #431
	ldr	r0, .L2824+20
	bl	printk
	bl	dump_stack
.L2747:
	mov	r3, #48
	mov	r2, #13
	mla	r4, r3, r4, r5
	cmn	r9, #1
	strb	r2, [r4, #1278]
	ldrb	r2, [r4, #1238]	@ zero_extendqisi2
	str	r7, [r4, #1272]
	orr	r2, r2, #8
	strb	r2, [r4, #1238]
	beq	.L2749
	ldr	r2, .L2824
	mla	r3, r3, r6, r2
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2751
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2751
.L2749:
	mov	r4, #48
	ldr	ip, .L2824+4
	mla	r4, r4, r6, r5
	ldrb	r0, [r5, #1153]	@ zero_extendqisi2
	ldrb	ip, [ip, #-2538]	@ zero_extendqisi2
	mvn	r3, #0
	rsb	r1, r0, #24
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
	beq	.L2753
	ldr	r3, [r9, #4]
	ldr	r1, [r3, #4]
	cmp	r2, r1
	beq	.L2753
	ldr	r0, .L2824+8
	ldr	r0, [r0]
	tst	r0, #64
	beq	.L2753
	str	r1, [sp]
	ldr	r0, .L2824+12
	ldr	r1, [r4, #1260]
	ldr	r3, [r3]
	bl	printk
.L2753:
	mov	r3, #48
	mla	r3, r3, r6, r5
	ldr	r2, [r3, #1256]
	cmn	r2, #1
	beq	.L2751
	ldr	r3, [r3, #1248]
	ldr	r3, [r3, #4]
	cmp	r2, r3
	beq	.L2751
	ldr	r1, .L2824+16
	movw	r2, #450
	ldr	r0, .L2824+20
	bl	printk
	bl	dump_stack
.L2751:
	mov	r3, #48
	mla	r5, r3, r6, r5
	str	r7, [r5, #1272]
.L2822:
	mov	r3, #13
	strb	r3, [r5, #1278]
	ldrb	r3, [r5, #1238]	@ zero_extendqisi2
	orr	r3, r3, #8
	strb	r3, [r5, #1238]
	b	.L2767
.L2739:
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r7, r0
	beq	.L2767
	ands	r6, r0, #5
	mov	r3, #48
	beq	.L2755
	mla	r5, r3, r4, r5
	ldr	r0, .L2824+24
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
	b	.L2735
.L2755:
	mul	r3, r3, r4
	mov	r2, #13
	add	r8, r5, r3
	strb	r2, [r8, #1278]
	ldr	r2, [r5, #2804]
	str	r6, [r8, #1272]
	ldr	r1, [r2, #156]
	ldr	r2, .L2824+28
	cmp	r1, r2
	bne	.L2767
	ldr	r2, .L2824+32
	add	r3, r3, r2
	ldrh	r3, [r3, #2]
	cmp	r3, #0
	bne	.L2767
	ldr	r3, .L2824
	ldr	r7, .L2824+4
	ldr	r2, [r8, #1260]
	ldrb	r0, [r3, #1153]	@ zero_extendqisi2
	mvn	r3, #0
	ldrb	ip, [r7, #-2538]	@ zero_extendqisi2
	rsb	r1, r0, #24
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	str	ip, [sp]
	bic	r1, r2, r3, asl r1
	uxtb	r0, r0
	ldr	r3, [r7, #-84]
	ldr	r2, [r7, #-88]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2756
	ldr	r1, [r8, #1248]
	ldr	r2, [r7, #-84]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2767
.L2756:
	mov	r2, #48
	ldr	r0, .L2824+36
	mla	r4, r2, r4, r5
	ldrb	r2, [r5, #1200]	@ zero_extendqisi2
	ldrb	r1, [r4, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r4, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #1272]
	b	.L2735
.L2742:
	ldr	r2, .L2824+40
	cmp	r3, #11
	mov	r1, #48
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	mvn	r8, #0
	movne	r9, #3
	mla	ip, r1, r4, r2
	rsb	lr, r3, #24
	mvn	r3, r8, asl r3
	and	r0, r3, r6, lsr lr
	moveq	r9, #10
	mov	r10, r3
	uxth	r0, r0
.L2758:
	ldrb	r8, [ip]	@ zero_extendqisi2
	cmp	r8, #255
	beq	.L2823
	mul	r3, r1, r8
	ldr	r7, .L2824
	add	ip, r2, r3
	add	r3, r5, r3
	ldrb	fp, [r3, #1278]	@ zero_extendqisi2
	cmp	fp, r9
	bne	.L2758
	ldr	r3, [r3, #1260]
	and	r3, r10, r3, lsr lr
	cmp	r0, r3
	bne	.L2758
	bl	nandc_iqr_wait_flash_ready
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	mov	r6, r0
	beq	.L2821
	ands	r2, r0, #15
	mov	r3, #48
	mul	r3, r3, r4
	beq	.L2762
	add	r7, r7, r3
	mov	r9, #12
	ldr	r0, .L2824+44
	mov	r2, r6
	ldrb	r1, [r7, #1237]	@ zero_extendqisi2
	str	r9, [sp]
	ldr	r3, [r7, #1260]
	bl	printk
	mvn	r3, #0
	strb	r9, [r7, #1278]
	str	r3, [r7, #1272]
	b	.L2761
.L2762:
	add	r9, r7, r3
	mov	r1, #13
	str	r2, [r9, #1272]
	ldr	r2, [r7, #2804]
	strb	r1, [r9, #1278]
	ldr	r1, [r2, #156]
	ldr	r2, .L2824+28
	cmp	r1, r2
	bne	.L2821
	ldr	r2, .L2824+40
	add	r3, r2, r3
	ldrh	r3, [r3, #34]
	cmp	r3, #0
	bne	.L2821
	ldr	r6, .L2824+4
	mvn	r3, #0
	ldr	r2, [r9, #1260]
	ldrb	r0, [r7, #1153]	@ zero_extendqisi2
	ldrb	ip, [r6, #-2538]	@ zero_extendqisi2
	rsb	r1, r0, #24
	mvn	r3, r3, asl r0
	and	r0, r3, r2, lsr r1
	str	ip, [sp]
	bic	r1, r2, r3, asl r1
	uxtb	r0, r0
	ldr	r3, [r6, #-84]
	ldr	r2, [r6, #-88]
	bl	flash_read_page_en
	cmn	r0, #1
	mov	r3, r0
	beq	.L2764
	ldr	r1, [r9, #1248]
	ldr	r2, [r6, #-84]
	ldr	r1, [r1]
	ldr	r2, [r2]
	cmp	r1, r2
	beq	.L2821
.L2764:
	mov	r6, #48
	ldrb	r2, [r5, #1200]	@ zero_extendqisi2
	mla	r6, r6, r4, r5
	ldr	r0, .L2824+48
	ldrb	r1, [r6, #1237]	@ zero_extendqisi2
	str	r2, [sp]
	ldr	r2, [r6, #1260]
	bl	printk
	mvn	r3, #0
	str	r3, [r6, #1272]
.L2821:
	mov	r6, #0
.L2761:
	mov	r3, #48
	mla	r4, r3, r4, r5
	mla	r5, r3, r8, r5
	ldrb	r2, [r4, #1278]	@ zero_extendqisi2
	ldr	r3, [r4, #1272]
	strb	r2, [r5, #1278]
	str	r3, [r5, #1272]
	b	.L2735
.L2823:
	mvn	r6, #0
	b	.L2735
.L2740:
	mov	r0, r6
	mov	r1, #32
	bl	flash_wait_device_ready
	tst	r0, #32
	beq	.L2767
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
	b	.L2735
.L2741:
	mov	r0, r6
	mov	r1, #64
	bl	flash_wait_device_ready
	tst	r0, #64
	movne	r3, #48
	mlane	r5, r3, r4, r5
	movne	r3, #7
	strne	r0, [r5, #1272]
	strneb	r3, [r5, #1278]
.L2767:
	mov	r6, #0
.L2735:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2825:
	.align	2
.L2824:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LC174
	.word	.LANCHOR1+2104
	.word	.LC0
	.word	.LC175
	.word	1145785929
	.word	.LANCHOR0+1268
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
	beq	.L2827
	ldr	r3, .L2858
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L2827
	ldr	r0, .L2858+4
	mov	r2, r5
	ldr	r1, [r4, #24]
	bl	printk
.L2827:
	ldr	r8, .L2858+8
	mov	r6, #0
	mov	r9, r8
.L2828:
	cmp	r5, #0
	beq	.L2855
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r7, [r4, #24]
	str	r3, [sp, #4]
.L2829:
	mov	r0, r7
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2857
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2829
.L2857:
	cmp	r5, #1
	beq	.L2831
	ldrb	r3, [r8, #1198]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2831
	ldrb	r3, [r8, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2832
.L2831:
	mov	r0, r4
	bl	queue_prog_cmd
	b	.L2833
.L2832:
	ldrb	r3, [r8, #1153]	@ zero_extendqisi2
	mvn	fp, #0
	rsb	ip, r3, #24
	mov	ip, r7, lsr ip
	bic	fp, ip, fp, asl r3
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	uxth	fp, fp
	bne	.L2834
	ldr	r1, .L2858+12
	movw	r2, #697
	ldr	r0, .L2858+16
	bl	printk
	bl	dump_stack
.L2834:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	mov	r10, #1
	mla	r3, r2, r3, r9
	ldrb	r2, [r9, #1153]	@ zero_extendqisi2
	ldr	ip, [r3, #1260]
	rsb	r3, r2, #24
	mov	r2, r10, asl r2
	sub	r2, r2, #1
	and	r2, r2, ip, lsr r3
	uxth	r2, r2
	cmp	fp, r2
	bne	.L2835
	ldr	r2, .L2858+20
	ldrh	fp, [r2]
	ldr	r2, .L2858+24
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
	beq	.L2836
	cmp	r6, lr
	beq	.L2835
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
	ldr	r0, .L2858+28
	strb	r3, [r4]
	bl	buf_add_tail
	b	.L2833
.L2836:
	mov	r0, r4
	mov	r6, fp
	bl	queue_prog_cmd
	b	.L2833
.L2835:
	mov	r0, r4
	mov	r6, #0
	bl	queue_prog_cmd
.L2833:
	subs	r5, r5, #1
	ldrne	r3, .L2858+32
	movne	r4, #48
	ldrne	r2, [sp, #4]
	mlane	r4, r4, r2, r3
	bne	.L2828
.L2855:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2859:
	.align	2
.L2858:
	.word	.LANCHOR2
	.word	.LC179
	.word	.LANCHOR0
	.word	.LANCHOR1+2136
	.word	.LC0
	.word	.LANCHOR3-3132
	.word	.LANCHOR3
	.word	.LANCHOR0+2774
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
	ldr	r4, .L2865
.L2861:
	ldrb	r3, [r4, #2774]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2864
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2861
.L2864:
	ldmfd	sp!, {r4, pc}
.L2866:
	.align	2
.L2865:
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
	ldr	r3, .L2873
	ldr	r5, .L2873+4
	ldrb	r1, [r3, #2801]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r1, #0
	beq	.L2868
	ldrb	r2, [r5, #-80]	@ zero_extendqisi2
	mov	r0, #48
	ldr	r3, .L2873+8
	mla	r0, r0, r2, r3
	bl	sblk_prog_page
.L2868:
	mvn	r3, #0
	strb	r3, [r5, #-80]
	mov	r3, #0
	strb	r3, [r4, #2801]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	movw	r0, #65535
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_vpn_decrement
.L2874:
	.align	2
.L2873:
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
	ldr	r3, .L2880
	ldrb	r3, [r3, #2801]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, pc}
	bl	timer_get_time
	ldr	r3, .L2880+4
	ldr	r3, [r3, #-76]
	add	r3, r3, #100
	cmp	r0, r3
	ldmlsfd	sp!, {r3, pc}
	ldmfd	sp!, {r3, lr}
	b	ftl_flush
.L2881:
	.align	2
.L2880:
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
	ldr	r3, .L2886
	mov	r7, r1
	mov	r6, r2
	mvn	r2, #0
	mov	r5, r0
	ldrb	r1, [r3, #1153]	@ zero_extendqisi2
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
.L2887:
	.align	2
.L2886:
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
	ldr	r10, .L2916
	mov	r8, r0
	mov	r6, r1
	mov	r4, r0
	mov	r5, r1
.L2889:
	cmp	r5, #0
	beq	.L2912
	ldrb	fp, [r4]	@ zero_extendqisi2
	ldr	r9, [r4, #24]
.L2890:
	mov	r0, r9
	mov	r1, #0
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2914
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2890
.L2914:
	cmp	r5, #1
	beq	.L2896
	ldr	r3, .L2916+4
	ldrb	r3, [r3, #-72]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2896
	ldr	r3, .L2916
	mvn	lr, #0
	ldrb	r2, [r3, #1153]	@ zero_extendqisi2
	rsb	r3, r2, #24
	mov	r3, r9, lsr r3
	bic	lr, r3, lr, asl r2
	uxth	r3, lr
	str	r3, [sp, #4]
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2895
	ldr	r1, .L2916+8
	movw	r2, #782
	ldr	r0, .L2916+12
	bl	printk
	bl	dump_stack
.L2895:
	ldrb	r3, [r4]	@ zero_extendqisi2
	mov	r2, #48
	ldrb	r0, [r10, #1153]	@ zero_extendqisi2
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
	bne	.L2896
	ldr	r2, .L2916+16
	ldrh	r2, [r2]
	add	r9, r2, r9
	cmp	r1, r9
	bne	.L2896
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
	ldr	r0, .L2916+20
	strb	r9, [r4]
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	bl	buf_add_tail
	ldr	r1, .L2916+24
	strb	r9, [r7, #1236]
	ldr	r0, .L2916+20
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	strb	ip, [r7, #1278]
	strb	r2, [r7, #1279]
	add	r1, r1, r3
	bl	buf_add_tail
	b	.L2894
.L2896:
	mov	r0, r4
	bl	queue_read_cmd
.L2894:
	subs	r5, r5, #1
	ldrne	r3, .L2916+24
	movne	r4, #48
	mlane	r4, r4, fp, r3
	bne	.L2889
.L2912:
	ldr	r4, .L2916+24
	mov	r5, #48
.L2900:
	cmp	r6, #0
	beq	.L2915
	ldrb	r3, [r8, #42]	@ zero_extendqisi2
	cmp	r3, #13
	bne	.L2901
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r6, r6, #1
	cmp	r3, #255
	mlane	r8, r5, r3, r4
.L2901:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2900
.L2915:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2917:
	.align	2
.L2916:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2152
	.word	.LC0
	.word	.LANCHOR3-3066
	.word	.LANCHOR0+2774
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
	ldr	r6, .L2954
	ldr	r3, [r6, #2836]
	ldr	r9, [r6, #1096]
	cmp	r3, #0
	bne	.L2919
	mov	r0, #1
	bl	buf_alloc
	str	r0, [r6, #2836]
.L2919:
	ldr	r4, [r6, #2836]
	cmp	r4, #0
	bne	.L2920
	ldr	r1, .L2954+4
	movw	r2, #729
	ldr	r0, .L2954+8
	bl	printk
	bl	dump_stack
.L2920:
	ldr	fp, .L2954+12
	add	r10, r9, #96
	mov	r8, #0
	mov	ip, fp
.L2921:
	ldrb	r3, [r9, #89]	@ zero_extendqisi2
	ldr	r5, .L2954+16
	cmp	r8, r3
	bge	.L2952
	mov	r7, #1
.L2932:
	ldrh	r3, [r5, #16]
	cmp	r7, r3
	bgt	.L2953
	ldr	r2, .L2954+20
	ldrb	r1, [fp, #-3124]	@ zero_extendqisi2
	ldrh	r3, [r10]
	ldrh	r2, [r2]
	cmp	r1, #2
	mul	r2, r2, r3
	beq	.L2922
	ldr	r3, .L2954
	ldrb	r3, [r3, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2923
.L2922:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #1
	add	r3, r3, r2
	add	r3, r3, r7
	orr	r3, r3, r1, asl #24
	b	.L2951
.L2923:
	ldr	r3, .L2954+16
	cmp	r1, #3
	ldrh	r3, [r3, #12]
	addne	r2, r2, r3
	strne	r2, [r4, #24]
	bne	.L2924
	ldr	r1, .L2954
	ldrb	r1, [r1, #1161]	@ zero_extendqisi2
	cmp	r1, #0
	addne	r3, r3, r3, asl #1
	addeq	r3, r2, r3
	subne	r3, r3, #1
	orreq	r3, r3, r7, asl #24
	addne	r3, r3, r2
	addne	r3, r3, r7
	orrne	r3, r3, #50331648
.L2951:
	str	r3, [r4, #24]
.L2924:
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #28]
	bl	sblk_read_page
	ldr	r2, [r4, #36]
	cmn	r2, #1
	ldr	ip, [sp, #28]
	beq	.L2927
	ldr	r0, [ip, #-124]
	ldrh	r1, [r5, #18]
	ldr	r3, [r4, #12]
	ldr	lr, [r0, r1, asl #2]
	ldr	r0, [r3, #4]
	cmp	lr, r0
	bne	.L2927
	ldr	r0, [fp, #-120]
	ldr	r3, [r3, #8]
	ldr	r1, [r0, r1, asl #2]
	cmp	r1, r3
	beq	.L2928
.L2927:
	ldrh	r3, [r5, #18]
	ldr	r1, [ip, #-124]
	mov	r0, r3, asl #2
	ldr	r3, [r1, r3, asl #2]
	cmn	r3, #1
	beq	.L2928
	ldr	r1, .L2954+24
	ldr	r1, [r1]
	tst	r1, #1024
	beq	.L2929
	ldr	ip, .L2954+12
	ldr	r1, [r4, #12]
	ldr	ip, [ip, #-120]
	ldr	r0, [ip, r0]
	str	r0, [sp]
	ldr	r0, [r1]
	str	r0, [sp, #4]
	ldr	r0, [r1, #4]
	str	r0, [sp, #8]
	ldr	r0, [r1, #8]
	str	r0, [sp, #12]
	ldr	r1, [r1, #12]
	ldr	r0, .L2954+28
	str	r1, [sp, #16]
	ldr	r1, [r4, #24]
	bl	printk
.L2929:
	ldrh	r3, [r9, #80]
	mov	r1, #0
	ldr	r2, [r6, #1092]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, [r6, #1096]
	ldr	r3, [r2, #556]
	add	r3, r3, #1
	str	r3, [r2, #556]
	ldr	r3, [r6, #2804]
	ldr	r2, [r3, #156]
	ldr	r3, .L2954+32
	cmp	r2, r3
	bne	.L2935
	ldr	r3, .L2954+12
	ldrb	r3, [r3, #-2534]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2935
	ldr	r3, .L2954+12
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, r1
	bne	.L2935
	ldr	r0, [r4, #24]
	bl	ftl_mask_bad_block
	b	.L2935
.L2928:
	ldrh	r3, [r5, #18]
	add	r7, r7, #1
	add	r3, r3, #1
	strh	r3, [r5, #18]	@ movhi
	b	.L2932
.L2953:
	add	r8, r8, #1
	add	r10, r10, #2
	b	.L2921
.L2952:
	ldrh	r3, [r5, #12]
	add	r2, r3, #1
	strh	r2, [r5, #12]	@ movhi
	ldr	r2, .L2954+12
	ldrb	r0, [r2, #-3122]	@ zero_extendqisi2
	cmp	r0, #0
	addne	r3, r3, #2
	strneh	r3, [r5, #12]	@ movhi
	movne	r0, #0
	b	.L2930
.L2935:
	mvn	r0, #0
.L2930:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2955:
	.align	2
.L2954:
	.word	.LANCHOR0
	.word	.LANCHOR1+2168
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
.L2957:
	mov	r0, r6
	mov	r1, #1
	bl	queue_lun_state
	subs	r5, r0, #0
	beq	.L2960
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2957
.L2960:
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
.L2962:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r9, r0, #0
	beq	.L2974
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2962
.L2974:
	cmp	r10, #2
	bne	.L2964
	ldr	r2, .L2976
	ldr	lr, [r7]
	ldrb	r3, [r2, #1198]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r4, [lr, #24]
	beq	.L2969
	ldr	r3, [r5]
	mov	fp, #1
	ldrb	r4, [r2, #1153]	@ zero_extendqisi2
	ldrb	r2, [r2, #1161]	@ zero_extendqisi2
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
	beq	.L2966
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
	b	.L2967
.L2966:
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
.L2967:
	ldr	r1, [r5]
	mov	r3, #5
	ldr	r0, .L2976+4
	strb	r3, [r1, #42]
	mov	r3, #1
	strb	r3, [r1, #43]
	mvn	r3, #0
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2968
.L2969:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	subs	r6, r0, #0
	beq	.L2975
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2969
.L2975:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
	mov	r0, r7
	mov	r1, r6
	bl	queue_tlc_prog_cmd
.L2971:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2968
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2971
.L2964:
	mov	r0, r5
	mov	r1, #1
	bl	queue_tlc_prog_cmd
.L2968:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2977:
	.align	2
.L2976:
	.word	.LANCHOR0
	.word	.LANCHOR0+2774
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
	ldr	r9, .L2986
	mov	r7, r1
	mov	r8, #1
.L2979:
	cmp	r7, #0
	beq	.L2984
	ldr	r3, [r5, #-8]
	ldr	r4, [r3, #24]
.L2980:
	mov	r0, r4
	mov	r1, #1
	bl	queue_lun_state
	cmp	r0, #0
	beq	.L2985
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2980
.L2985:
	ldr	r3, [r5, #-8]
	sub	r7, r7, #1
	ldrb	r4, [r9, #1153]	@ zero_extendqisi2
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
	ldr	r0, .L2986+4
	strb	r3, [r1, #42]
	mvn	r3, #0
	strb	r8, [r1, #43]
	strb	r3, [r1]
	bl	buf_add_tail
	b	.L2979
.L2984:
	mov	r0, r7
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2987:
	.align	2
.L2986:
	.word	.LANCHOR0
	.word	.LANCHOR0+2774
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
	ldr	r6, .L3018
	ldrb	r3, [sp, #68]	@ zero_extendqisi2
	mov	r7, r0
	mov	r5, r1
	mov	r9, r2
	ubfx	r4, r1, #24, #2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #1109]	@ zero_extendqisi2
	cmp	r3, r0
	bhi	.L2989
	ldr	r1, .L3018+4
	movw	r2, #642
	ldr	r0, .L3018+8
	bl	printk
	bl	dump_stack
.L2989:
	ldrb	r2, [r6, #1109]	@ zero_extendqisi2
	ldr	r3, .L3018
	cmp	r2, r7
	mvnls	r0, #0
	bls	.L2990
	add	r2, r3, r7
	cmp	r4, #0
	ldrb	r10, [r2, #1144]	@ zero_extendqisi2
	bne	.L3002
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2992
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3002
.L2992:
	ldrh	fp, [r6, #2]
	mov	r0, r5
	mov	r1, fp
	bl	__aeabi_uidiv
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L3018
	mul	r0, fp, r0
	rsb	r4, r0, r5
	mov	r4, r4, asl #1
	addeq	r4, r3, r4
	ldreqh	r4, [r4, #4]
	add	r4, r4, r0
	b	.L2991
.L3002:
	mov	r4, r5
.L2991:
	ldr	fp, .L3018+12
	ldr	r6, .L3018+16
.L2997:
	ldr	r3, [fp]
	tst	r3, #16
	beq	.L2994
	ldr	r0, .L3018+20
	mov	r1, r10
	mov	r2, r5
	mov	r3, r4
	bl	printk
.L2994:
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
	beq	.L2995
	ldr	r3, [sp, #64]
	mov	r1, r5
	str	r0, [sp, #20]
	mov	r0, r7
	str	r3, [sp]
	ldr	r3, [r6, #-84]
	ldr	r2, [r6, #-88]
	bl	flash_read_page_en
	sub	r3, r0, #512
	cmp	r0, #512
	cmnne	r0, #1
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp, #12]
	beq	.L2996
	ldr	r3, [r6, #-88]
	ldr	r2, [r9]
	ldr	ip, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	bne	.L2996
	ldr	r3, [r6, #-84]
	ldr	r2, [r8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2995
.L2996:
	mov	r2, #4
	ldr	r0, .L3018+24
	mov	r3, r2
	mov	r1, r9
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3018+28
	mov	r1, r8
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3018+32
	ldr	r1, [r6, #-84]
	bl	rknand_print_hex
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L3018+36
	ldr	r1, [r6, #-88]
	bl	rknand_print_hex
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L2997
.L2999:
	mov	r1, r5
	ldr	r0, .L3018+40
	bl	printk
	ldr	r1, .L3018+4
	movw	r2, #685
	ldr	r0, .L3018+8
	bl	printk
	bl	dump_stack
	mvn	ip, #0
	b	.L2998
.L2995:
	cmn	ip, #1
	beq	.L2999
.L2998:
	mov	r0, ip
.L2990:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3019:
	.align	2
.L3018:
	.word	.LANCHOR0
	.word	.LANCHOR1+2192
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
	bne	.L3021
	ldr	r1, .L3023
	movw	r2, #2678
	ldr	r0, .L3023+4
	bl	printk
	bl	dump_stack
	ldr	r0, .L3023+8
	mov	r1, r5
	bl	printk
.L3021:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L3024:
	.align	2
.L3023:
	.word	.LANCHOR1+2212
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
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3060
	mov	r1, #0
	ldr	r5, .L3060+4
	mov	r7, #0
	str	r0, [sp, #8]
	ldrb	r2, [r4, #-2538]	@ zero_extendqisi2
	mov	r9, r4
	ldr	r0, [r4, #-68]
	ldr	r6, .L3060
	mov	r2, r2, asl #1
	ldr	r8, .L3060+8
	bl	ftl_memset
	ldr	r3, [r5, #2804]
	ldrh	r2, [r3, #74]
	cmp	r2, #1
	movhi	r2, #0
	strhih	r2, [r3, #150]	@ movhi
.L3037:
	ldr	r2, [r5, #2804]
	ldrb	r4, [r9, #-56]	@ zero_extendqisi2
	ldrh	fp, [r8]
	ldr	r3, [r2, #4]
	ldrh	r10, [r9, #-54]
	add	r3, r3, #1
	str	r3, [r2, #4]
	ldr	r3, [r9, #-68]
	ldr	r2, [sp, #8]
	str	r2, [r3]
	ldr	r2, [r5, #2804]
	ldr	r3, [r9, #-68]
	ldrb	r1, [r9, #-2538]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	ldr	r0, [r9, #-52]
	mov	r1, r1, asl #9
	str	r3, [sp, #12]
	str	r2, [r3, #4]
	bl	js_hash
	ldr	r3, [sp, #12]
	str	r0, [r3, #8]
	ldr	r3, .L3060+12
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3027
	ldr	r3, .L3060+4
	ldr	r0, .L3060+16
	ldrb	r1, [r6, #-56]	@ zero_extendqisi2
	ldr	r3, [r3, #2804]
	ldrh	r2, [r6, #-54]
	ldr	r3, [r3, #4]
	bl	printk
.L3027:
	ldr	r3, .L3060+20
	ldrh	r0, [r9, #-54]
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L3028
.L3033:
	ldrb	r3, [r9, #-55]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r9, #-55]
	cmp	r3, #7
	bls	.L3029
	mov	r3, #8
.L3032:
	ldr	r2, [r5, #1040]
	sub	r4, r3, #8
	add	r2, r2, r3
	uxth	r4, r4
	ldrb	r1, [r2, #32]	@ zero_extendqisi2
	add	r2, r1, #127
	uxtb	r2, r2
	cmp	r2, #125
	bhi	.L3030
	ldr	r1, .L3060+24
	movw	r2, #846
	ldr	r0, .L3060+28
	bl	printk
	bl	dump_stack
	b	.L3031
.L3030:
	cmp	r1, #255
	bne	.L3031
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L3032
	mov	r4, #8
.L3031:
	mov	r7, #1
	strb	r4, [r9, #-55]
.L3029:
	ldr	r3, [r5, #1040]
	ldrb	r2, [r9, #-55]	@ zero_extendqisi2
	ldr	r10, .L3060
	add	r3, r3, r2
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	strb	r3, [r9, #-56]
	beq	.L3033
	ldrh	r4, [r8]
	mov	r0, #0
	mul	r4, r4, r3
	mov	r1, r4
	bl	flash_erase_block
	ldrb	r3, [r10, #-2538]	@ zero_extendqisi2
	mov	r1, r4
	mov	r0, #0
	str	r3, [sp]
	ldr	r3, [r10, #-68]
	ldr	r2, [r10, #-52]
	bl	ftl_prog_page
	mov	r3, #1
	strh	r3, [r10, #-54]	@ movhi
	add	r4, r4, r3
	b	.L3034
.L3028:
	cmp	r0, #0
	mla	r4, fp, r4, r10
	bne	.L3034
	mov	r1, r4
	bl	flash_erase_block
.L3034:
	ldrb	r3, [r9, #-2538]	@ zero_extendqisi2
	mov	r0, #0
	mov	r1, r4
	str	r3, [sp]
	ldr	r3, [r9, #-68]
	ldr	r2, [r9, #-52]
	bl	ftl_prog_page
	ldrh	r3, [r9, #-54]
	add	r3, r3, #1
	strh	r3, [r9, #-54]	@ movhi
	cmn	r0, #1
	beq	.L3035
	ldrb	r3, [r6, #-48]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3036
.L3035:
	mov	r3, #0
	strb	r3, [r9, #-48]
	b	.L3037
.L3036:
	cmp	r7, #1
	beq	.L3038
.L3046:
	ldrb	r3, [r9, #-56]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L3040
	ldr	r1, .L3060+24
	movw	r2, #890
	ldr	r0, .L3060+28
	bl	printk
	bl	dump_stack
	b	.L3040
.L3038:
	ldr	r3, .L3060
	ldrb	r4, [r3, #-55]	@ zero_extendqisi2
	add	r4, r4, #1
.L3041:
	cmp	r4, #7
	bhi	.L3046
	ldr	r3, [r5, #1040]
	add	r3, r3, r4
	ldrb	r6, [r3, #40]	@ zero_extendqisi2
	add	r3, r6, #127
	uxtb	r3, r3
	cmp	r3, #125
	bhi	.L3042
	ldr	r1, .L3060+24
	movw	r2, #881
	ldr	r0, .L3060+28
	bl	printk
	bl	dump_stack
	b	.L3043
.L3042:
	cmp	r6, #255
	beq	.L3044
.L3043:
	ldr	r3, .L3060+8
	mov	r0, #0
	ldrh	r1, [r3]
	mul	r1, r1, r6
	bl	flash_erase_block
.L3044:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L3041
.L3040:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3061:
	.align	2
.L3060:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3066
	.word	.LANCHOR2
	.word	.LC187
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2228
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
	ldr	r4, .L3095
	mov	r6, #0
	ldr	r5, .L3095+4
	mov	r1, r6
	mov	r2, #16384
	.pad #28
	sub	sp, sp, #28
	strb	r3, [r4, #-48]
	mov	r9, r4
	strb	r3, [r4, #-46]
	movw	r3, #1080
	ldr	r0, [r4, #-52]
	ldrh	r3, [r5, r3]
	strb	r6, [r4, #-47]
	str	r0, [r5, #1084]
	add	r3, r0, r3, asl #2
	str	r3, [r5, #2804]
	bl	ftl_memset
	mov	r1, r6
	ldr	r0, [r4, #-44]
	mov	r2, #16384
	bl	ftl_memset
	ldr	r3, [r5, #1040]
	ldr	r10, .L3095+8
	ldr	fp, .L3095+12
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	strb	r6, [r4, #-55]
	strh	r6, [r4, #-54]	@ movhi
	mov	r6, #7
	strb	r3, [r4, #-56]
.L3067:
	ldr	r3, [r5, #1040]
	uxth	r8, r6
	add	r3, r3, r6
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3063
	ldrh	ip, [r10]
	mov	r0, #0
	ldr	r2, [r4, #-52]
	mul	ip, ip, r3
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r4, #-68]
	mov	r1, ip
	str	ip, [sp, #12]
	bl	ftl_read_page
	cmn	r0, #1
	mov	r7, r0
	ldr	ip, [sp, #12]
	bne	.L3064
	ldrb	r3, [r9, #-2538]	@ zero_extendqisi2
	mov	r0, #0
	add	r1, ip, #1
	str	r3, [sp]
	ldr	r2, [r9, #-52]
	ldr	r3, [r9, #-68]
	bl	ftl_read_page
	mov	r7, r0
.L3064:
	ldr	r3, .L3095+16
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3065
	ldr	r3, [r5, #2804]
	mov	r1, r6
	ldr	r0, .L3095+20
	mov	r2, r7
	ldr	r3, [r3]
	str	r3, [sp]
	movw	r3, #749
	bl	printk
.L3065:
	cmn	r7, #1
	beq	.L3063
	ldr	r3, [r5, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	beq	.L3077
.L3063:
	subs	r6, r6, #1
	bcs	.L3067
	movw	r7, #65535
	mov	r8, #0
	b	.L3066
.L3077:
	mov	r7, r8
.L3066:
	ldr	r6, .L3095+16
	sxth	r7, r7
	ldr	r3, [r6]
	str	r6, [sp, #16]
	tst	r3, #4096
	beq	.L3068
	ldr	r3, [r5, #2804]
	mov	r1, r7
	ldr	r0, .L3095+24
	mov	r2, #4800
	ldr	r3, [r3]
	bl	printk
.L3068:
	cmn	r7, #1
	bne	.L3069
	mov	r2, #16384
	ldr	r0, [r4, #-52]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r5, #2804]
	ldr	r2, .L3095+12
	mov	r0, r7
	str	r2, [r3]
	ldr	r2, .L3095+28
	ldr	r3, [r5, #2804]
	str	r2, [r3, #12]
	b	.L3070
.L3069:
	ldr	r3, [r5, #1040]
	mov	r0, #0
	ldr	r2, [r4, #-52]
	mov	r7, #0
	sxtah	r3, r3, r8
	strb	r8, [r4, #-55]
	ldr	fp, .L3095+12
	ldrb	r1, [r3, #40]	@ zero_extendqisi2
	mov	r3, #4
	str	r3, [sp]
	ldr	r3, [r4, #-68]
	strb	r1, [r4, #-56]
	bl	flash_get_last_written_page
	ldr	ip, .L3095+32
	add	r6, ip, #3056
	ldrh	r8, [ip, #-10]
	uxth	r9, r0
	mov	r10, r0
	add	r3, r9, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	ldrb	r3, [r4, #-56]	@ zero_extendqisi2
	mul	r8, r8, r3
.L3071:
	rsb	r3, r7, r9
	tst	r3, #32768
	bne	.L3075
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	add	r1, r10, r8
	mov	r0, #0
	rsb	r1, r7, r1
	ldr	r2, [r4, #-52]
	str	r3, [sp]
	ldr	r3, [r4, #-68]
	bl	ftl_read_page
	cmn	r0, #1
	beq	.L3072
	ldr	r3, [r5, #2804]
	ldr	r3, [r3]
	cmp	r3, fp
	bne	.L3072
	ldr	r3, [r6, #-68]
	ldr	r3, [r3, #8]
	cmp	r3, #0
	bne	.L3073
.L3075:
	ldrh	r3, [sp, #12]
	strh	r3, [r4, #-54]	@ movhi
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
	beq	.L3070
	ldr	r3, .L3095+4
	ldr	r0, .L3095+36
	ldr	r3, [r3, #2804]
	ldr	r1, [r3, #156]
	bl	printk
	mov	r0, #0
	b	.L3070
.L3073:
	ldrb	r1, [r6, #-2538]	@ zero_extendqisi2
	ldr	r0, [r6, #-52]
	str	r3, [sp, #20]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	beq	.L3075
	ldr	r3, [r6, #-68]
	ldr	r0, .L3095+40
	ldr	r1, [r3, #8]
	bl	printk
.L3072:
	add	r7, r7, #1
	b	.L3071
.L3070:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3096:
	.align	2
.L3095:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-3066
	.word	1229739078
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
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	bl	timer_get_time
	ldr	r3, .L3113
	ldr	r5, .L3113+4
	ldr	r4, [r5, #1096]
	umull	r0, r1, r0, r3
	mov	r2, r1, lsr #5
	ldr	r1, [r4, #520]
	cmp	r2, r1
	ldrhi	r3, [r4, #12]
	rsbhi	r3, r1, r3
	addhi	r3, r3, r2
	strhi	r3, [r4, #12]
	bhi	.L3112
.L3098:
	bcs	.L3099
.L3112:
	str	r2, [r4, #520]
.L3099:
	mov	r0, #0
	ldr	r4, .L3113+8
	bl	ftl_total_vpn_update
	ldr	r7, .L3113+12
	mov	r8, r4
	mov	r6, r4
.L3103:
	ldr	r3, [r5, #2804]
	ldr	r2, [r3, #56]
	add	r2, r2, #1
	str	r2, [r3, #56]
	ldrh	r2, [r3, #140]
	ldrh	r3, [r7]
	cmp	r2, r3
	bcc	.L3101
	bl	ftl_ext_alloc_new_blk
.L3101:
	ldr	r3, [r5, #2804]
	ldrh	r2, [r3, #130]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3102
	ldr	r1, .L3113+16
	movw	r2, #2211
	ldr	r0, .L3113+20
	bl	printk
	bl	dump_stack
.L3102:
	ldr	r1, [r5, #2804]
	ldr	r9, .L3113+24
	ldrb	r2, [r5, #1153]	@ zero_extendqisi2
	ldrh	r3, [r1, #130]
	ldrh	r0, [r9]
	rsb	r2, r2, #24
	ldrh	r9, [r9, #66]
	rsb	r2, r0, r2
	mvn	r0, #0
	mov	r10, r3, asr r2
	bic	r3, r3, r0, asl r2
	ldrh	r2, [r1, #140]
	mov	r1, #0
	sxth	r3, r3
	ldr	r0, [r4, #-68]
	mla	r9, r9, r3, r2
	ldrb	r2, [r4, #-2538]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, [r4, #-68]
	mov	r2, #0
	str	r2, [r3]
	ldr	r3, [r5, #2804]
	ldr	fp, [r4, #-68]
	ldrb	r1, [r4, #-2538]	@ zero_extendqisi2
	ldr	r3, [r3, #56]
	ldr	r0, [r4, #-44]
	mov	r1, r1, asl #9
	str	r3, [fp, #4]
	bl	js_hash
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	mov	r1, r9
	str	r0, [fp, #8]
	uxtb	r0, r10
	str	r3, [sp]
	ldr	r2, [r4, #-44]
	ldr	r3, [r4, #-68]
	bl	ftl_prog_page
	ldr	r2, [r5, #2804]
	ldrh	r3, [r2, #140]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #140]	@ movhi
	cmp	r3, #1
	beq	.L3103
	cmn	r0, #1
	beq	.L3104
	ldrb	r0, [r8, #-46]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3105
.L3104:
.L3100:
	mov	r3, #0
	strb	r3, [r6, #-46]
	b	.L3103
.L3105:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3114:
	.align	2
.L3113:
	.word	1374389535
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2244
	.word	.LC0
	.word	.LANCHOR3-3132
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
	ldr	r5, .L3135
	mvn	r6, #0
	ldr	r9, .L3135+4
	.pad #20
	sub	sp, sp, #20
	sub	r3, r5, #2528
	sub	r1, r5, #3120
	strh	r2, [r3, #-12]	@ movhi
	ldr	r3, [r9, #2804]
	ldrh	r4, [r1, #-12]
	ldrh	r2, [r3, #130]
	ldrb	r3, [r9, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r4, r4, r3
	mov	r3, #4
	mov	r7, r2, asr r4
	bic	r6, r2, r6, asl r4
	str	r3, [sp]
	mov	r4, r9
	uxtb	r10, r7
	ldr	r2, [r5, #-44]
	ldr	r3, [r5, #-68]
	mov	r1, r6
	mov	r0, r10
	bl	flash_get_last_written_page
	ldr	r3, .L3135+8
	ldr	r2, [r3]
	tst	r2, #4096
	mov	r8, r0
	str	r3, [sp, #8]
	beq	.L3116
	uxth	r7, r7
	str	r7, [sp]
	ldr	r3, [r9, #2804]
	mov	r2, #2256
	ldr	r0, .L3135+12
	ldr	r1, .L3135+16
	ldrh	r3, [r3, #130]
	str	r3, [sp, #4]
	mov	r3, r8
	bl	printk
.L3116:
	ldr	fp, .L3135
	mov	r9, #0
.L3117:
	uxth	r7, r8
	rsb	r3, r9, r7
	tst	r3, #32768
	bne	.L3122
	ldr	r3, .L3135+20
	rsb	r1, r9, r8
	ldrb	r2, [r5, #-2538]	@ zero_extendqisi2
	mov	r0, r10
	ldrh	r3, [r3]
	str	r2, [sp]
	ldr	r2, [r5, #-44]
	mla	r1, r3, r6, r1
	ldr	r3, [r5, #-68]
	bl	flash_read_page_en
	cmp	r0, #512
	cmnne	r0, #1
	beq	.L3118
	ldr	r3, [r4, #1096]
	ldr	r2, .L3135+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L3118
	ldr	r3, [fp, #-68]
	ldr	r3, [r3, #8]
	cmp	r3, #0
	bne	.L3119
.L3122:
	bl	zftl_sblk_list_init
	ldr	r3, [r4, #2804]
	ldrh	r2, [r3, #140]
	cmp	r2, r8
	bgt	.L3121
	add	r7, r7, #1
	strh	r7, [r3, #140]	@ movhi
	bl	ftl_ext_info_flush
	b	.L3121
.L3119:
	ldrb	r1, [fp, #-2538]	@ zero_extendqisi2
	ldr	r0, [fp, #-44]
	str	r3, [sp, #12]
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3122
	ldr	r3, [fp, #-68]
	ldr	r0, .L3135+28
	ldr	r1, [r3, #8]
	bl	printk
.L3118:
	add	r9, r9, #1
	b	.L3117
.L3121:
	ldr	r5, [r4, #1096]
	bl	timer_get_time
	ldr	r3, .L3135+32
	umull	r0, r1, r0, r3
	mov	r3, r1, lsr #5
	str	r3, [r5, #520]
	ldr	r5, [r4, #1096]
	bl	timer_get_time
	ldr	r3, [r4, #1096]
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
	beq	.L3124
	ldr	r5, [r3, #520]
	ldr	r4, [r3, #12]
	bl	timer_get_time
	ldr	r1, .L3135+16
	mov	r2, r5
	mov	r3, r4
	str	r0, [sp]
	ldr	r0, .L3135+36
	bl	printk
.L3124:
	ldr	r3, .L3135+40
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3136:
	.align	2
.L3135:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC192
	.word	.LANCHOR1+2264
	.word	.LANCHOR3-3066
	.word	1162432070
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
	ldr	r2, .L3139
	mov	ip, r0
	mov	r4, r1
	ldrb	r0, [r2, #1153]	@ zero_extendqisi2
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
.L3140:
	.align	2
.L3139:
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
	ldrh	r3, [r0, #6]
	ldr	r5, .L3146
	cmp	r3, #1
	ldrh	r8, [r0, #12]
	ldr	r6, [r5, #-2548]
	bne	.L3143
	mov	r4, r0
	bl	ftl_get_new_free_page
	cmn	r0, #1
	mov	r7, r0
	beq	.L3144
	ldrh	r0, [r4]
	add	r6, r6, r8, asl #2
	bl	ftl_vpn_decrement
	ldr	r3, [r5, #-40]
	ldr	r2, .L3146+4
	mov	r0, r6
	str	r2, [r3]
	sub	r3, r5, #3088
	ldr	r4, [r5, #-40]
	ldrh	r1, [r3, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	mul	r1, r3, r1
	mov	r1, r1, asl #2
	bl	js_hash
	ldr	r1, [r5, #-40]
	str	r0, [r4, #4]
	mov	r4, #0
	mov	r2, r4
	str	r4, [r1, #8]
	str	r4, [r1, #12]
	mov	r0, #2
	str	r4, [r1, #16]!
	bl	ftl_debug_info_fill
	mov	r0, r7
	mov	r1, r6
	ldr	r2, [r5, #-40]
	ldrb	r3, [r5, #-2538]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	b	.L3144
.L3143:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3144:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3147:
	.align	2
.L3146:
	.word	.LANCHOR3
	.word	-178307901
	.fnend
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3190
	movw	r2, #1080
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #116
	sub	sp, sp, #116
	ldrh	r2, [r3, r2]
	mov	r10, r0
	str	r3, [sp, #44]
	cmp	r2, r0
	bls	.L3148
	ldr	r3, .L3190+4
	ldrb	r2, [r3, #-3116]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3150
	ldrb	r2, [r3, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3148
.L3150:
	ldr	r2, [sp, #44]
	ldrb	r2, [r2, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3148
	ldr	r2, .L3190
	mov	r4, r10, asl #2
	add	r1, sp, #96
	mov	r0, r10
	strh	r10, [sp, #80]	@ movhi
	mov	r5, #0
	ldr	r2, [r2, #1084]
	mov	r6, r5
	ldr	r7, .L3190+8
	mov	r8, r5
	add	r2, r2, r4
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	mov	fp, r7
	and	r2, r2, #224
	cmp	r2, #160
	ldreqb	r9, [r3, #-3124]	@ zero_extendqisi2
	movne	r9, #1
	bl	ftl_get_blk_list_in_sblk
	ldrh	r3, [fp, #-8]!
	uxtb	r0, r0
	strb	r0, [sp, #89]
	strh	r5, [sp, #82]	@ movhi
	smulbb	r0, r3, r0
	ldr	r3, [sp, #44]
	strb	r5, [sp, #85]
	strh	r5, [sp, #90]	@ movhi
	ldr	r2, [r3, #1084]
	add	r3, r2, r4
	strh	r0, [sp, #86]	@ movhi
	ldrh	r1, [r2, r4]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r2, [r2, r10, asl #2]
	mov	r1, r10
	ldr	r0, .L3190+12
	ubfx	r2, r2, #11, #8
	str	r2, [sp, #4]
	mov	r2, r3, lsr #5
	ubfx	r3, r3, #3, #2
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	add	r3, r7, #3088
	mov	r4, r0
	str	r3, [sp, #48]
.L3152:
	ldrh	r3, [fp]
	uxth	r7, r5
	cmp	r3, r7
	bls	.L3175
	mov	r3, r7, asl #1
	mov	ip, #0
	add	r2, r3, r7
	sub	r3, r3, #1
	sub	r2, r2, #1
	str	r3, [sp, #60]
	str	r2, [sp, #52]
.L3163:
	ldrb	r3, [sp, #89]	@ zero_extendqisi2
	uxth	r8, ip
	cmp	r3, r8
	bls	.L3187
	add	r3, r8, #8
	mov	r6, #1
	mov	r3, r3, asl r6
	str	r3, [sp, #64]
.L3162:
	cmp	r6, r9
	bhi	.L3188
	ldr	r2, [sp, #64]
	add	r3, sp, #112
	add	r3, r3, r2
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3154
	ldr	r3, .L3190+16
	cmp	r9, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3155
	ldr	r1, .L3190
	ldrb	r1, [r1, #1161]	@ zero_extendqisi2
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #52]
	orreq	r3, r3, r6, asl #24
	beq	.L3157
	b	.L3186
.L3155:
	cmp	r9, #2
	uxtahne	r3, r3, r5
	bne	.L3157
	ldr	r1, [sp, #60]
.L3186:
	add	r3, r3, r1
	ldr	r1, [sp, #48]
	add	r3, r3, r6
	ldrb	r1, [r1, #-3124]	@ zero_extendqisi2
	orr	r3, r3, r1, asl #24
.L3157:
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
	bne	.L3154
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
	ldr	r0, .L3190+20
	str	r1, [sp, #32]
	mov	r1, r2
	mov	r2, r7
	bl	printk
	ldr	r3, [sp, #56]
	ldr	ip, [sp, #68]
	cmp	r3, #0
	bne	.L3175
.L3154:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3162
.L3188:
	add	ip, ip, #1
	b	.L3163
.L3187:
	add	r5, r5, #1
	b	.L3152
.L3175:
.L3160:
	str	r6, [sp]
	mov	r3, r8
	ldr	r6, .L3190+4
	mov	r1, r10
	mov	r2, r7
	ldr	r0, .L3190+24
	bl	printk
	mov	r1, #0
	ldrb	r2, [r6, #-2538]	@ zero_extendqisi2
	mov	r5, r7
	ldr	r0, [r4, #4]
	ldr	fp, .L3190+28
	mov	r2, r2, asl #9
	bl	ftl_memset
	ldrb	r2, [r6, #-2538]	@ zero_extendqisi2
	ldr	r0, [r4, #12]
	mov	r1, #0
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3164:
	ldr	r3, .L3190+32
	ldrh	r3, [r3]
	cmp	r3, r5
	bls	.L3170
	ldr	r8, .L3190+4
	mov	r9, r5, asl #1
	mov	r7, #0
.L3171:
	ldrb	r2, [sp, #89]	@ zero_extendqisi2
	uxth	r3, r7
	cmp	r2, r3
	bls	.L3189
	ldr	r2, [fp]
	tst	r2, #4096
	beq	.L3165
	mov	r2, r3
	ldr	r0, .L3190+36
	mov	r1, r5
	str	r3, [sp, #48]
	bl	printk
	ldr	r3, [sp, #48]
.L3165:
	ldrb	ip, [r6, #-3116]	@ zero_extendqisi2
	cmp	ip, #0
	beq	.L3166
	add	r2, sp, #112
	mov	r0, r4
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3190+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	orr	r3, r3, r5
	str	r3, [r4, #24]
	bl	sblk_3d_tlc_dump_prog
	b	.L3167
.L3166:
	ldrb	r2, [r8, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	add	r2, sp, #112
	add	r3, r2, r3, asl #1
	ldrh	r2, [r3, #-16]
	ldr	r3, .L3190+16
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L3168
	orr	r3, r3, r9
	mov	r0, r4
	orr	r3, r3, #33554432
	str	r3, [r4, #24]
	bl	sblk_mlc_dump_prog
	b	.L3167
.L3168:
	ldr	r2, [sp, #44]
	orr	r3, r3, r5
	mvn	r0, #0
	ldrb	lr, [r8, #-2538]	@ zero_extendqisi2
	str	r3, [r4, #24]
	ldrb	r2, [r2, #1153]	@ zero_extendqisi2
	str	lr, [sp]
	rsb	r1, r2, #24
	str	ip, [sp, #4]
	mvn	r2, r0, asl r2
	and	r0, r2, r3, lsr r1
	bic	r1, r3, r2, asl r1
	ldr	r2, [r4, #4]
	uxtb	r0, r0
	ldr	r3, [r4, #12]
	bl	flash_prog_page_en
.L3167:
	add	r7, r7, #1
	b	.L3171
.L3189:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L3164
.L3170:
	mov	r0, r4
	bl	zbuf_free
	ldr	r0, .L3190+40
	mov	r1, r10
	bl	printk
.L3148:
	add	sp, sp, #116
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3191:
	.align	2
.L3190:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3088
	.word	.LC195
	.word	.LANCHOR3-3066
	.word	.LC196
	.word	.LC197
	.word	.LANCHOR2
	.word	.LANCHOR3-3096
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
	ldr	r4, .L3221
	ldr	r7, .L3221+4
	ldrh	r3, [r4, r5]
	ldr	r6, [r7, #908]
	cmp	r3, #3
	ldrls	pc, [pc, r3, asl #2]
	b	.L3193
.L3195:
	.word	.L3194
	.word	.L3196
	.word	.L3197
	.word	.L3198
.L3194:
	ldr	r8, .L3221+8
	movw	r3, #2792
	ldrh	r3, [r8, r3]
	cmp	r3, #7
	bls	.L3192
	ldrb	r3, [r8, #2773]	@ zero_extendqisi2
	cmp	r3, #2
	bls	.L3192
	ldr	r9, .L3221+12
	mov	r1, #0
	movw	r10, #65535
	sub	r0, r9, #3072
	bl	_list_get_gc_head_node
	cmp	r0, r10
	beq	.L3192
	ldr	r3, [r8, #1084]
	mov	r0, r0, asl #2
	ldrh	r3, [r3, r0]
	ubfx	r3, r3, #0, #11
	cmp	r3, #2
	bgt	.L3192
	mov	r0, #1
	add	r6, r8, #4096
	bl	buf_alloc
	cmp	r0, #0
	str	r0, [r7, #908]
	beq	.L3192
	sub	r0, r9, #3072
	mov	r1, #0
	ldr	r2, .L3221+16
	bl	_list_pop_index_node
	uxth	r5, r0
	cmp	r5, r10
	bne	.L3202
	ldr	r0, [r6, #908]
	bl	zbuf_free
	mov	r3, #0
	str	r3, [r6, #908]
	b	.L3192
.L3202:
	ldr	r1, .L3221+20
	mov	r0, r5
	bl	ftl_get_blk_list_in_sblk
	movw	r3, #2142
	strh	r5, [r4, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3221+24
	mov	r5, r5, asl #2
	strh	r2, [r3]	@ movhi
	movw	r3, #2106
	mov	r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r8, #1084]
	ldrb	r2, [r9, #-2538]	@ zero_extendqisi2
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
.L3196:
	movw	r3, #2142
	mov	r1, #0
	ldrh	r0, [r4, r3]
	bl	ftl_erase_sblk
	movw	r3, #2106
	mov	r2, #2
	strh	r2, [r4, r3]	@ movhi
	b	.L3192
.L3197:
	bl	sblk_wait_write_queue_completed
	ldr	r9, .L3221+8
	ldr	r10, .L3221+12
	mov	r8, #0
	ldr	fp, .L3221+28
.L3204:
	ldrb	r3, [r7, #883]	@ zero_extendqisi2
	uxth	r5, r8
	cmp	r3, r5
	bls	.L3219
	add	r5, r4, r5, asl #1
	movw	r3, #65535
	add	r5, r5, #2144
	ldrh	r2, [r5, #14]
	cmp	r2, r3
	beq	.L3205
	ldr	r3, .L3221+32
	mov	ip, #0
	ldrh	r1, [r3]
	ldr	r3, .L3221+24
	ldrh	r3, [r3]
	mla	r3, r1, r2, r3
	ldr	r2, [r6, #4]
	str	r3, [r2]
	ldr	r2, [r6, #4]
	str	fp, [r2, #4]
	ldr	r2, [r6, #12]
	str	ip, [r2]
	mvn	r2, #0
	ldrb	r0, [r9, #1153]	@ zero_extendqisi2
	ldrb	lr, [r10, #-2538]	@ zero_extendqisi2
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
.L3205:
	add	r8, r8, #1
	b	.L3204
.L3219:
	ldr	r1, .L3221
	ldr	r0, .L3221+36
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
	b	.L3192
.L3198:
	bl	sblk_wait_write_queue_completed
	ldr	r9, .L3221+32
	ldr	r10, .L3221+24
	mov	fp, #0
.L3207:
	ldrb	r3, [r7, #883]	@ zero_extendqisi2
	uxth	r5, fp
	ldr	r8, .L3221+4
	cmp	r3, r5
	bls	.L3220
	add	r5, r4, r5, asl #1
	movw	r2, #65535
	add	r5, r5, #2144
	add	r5, r5, #14
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L3209
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
	beq	.L3209
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L3209:
	add	fp, fp, #1
	b	.L3207
.L3220:
	add	r2, r8, #876
	ldr	r5, .L3221
	ldrh	r3, [r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2]	@ movhi
	ldr	r2, .L3221+36
	ldrh	r2, [r2, #-8]
	cmp	r2, r3
	bhi	.L3192
	movw	r3, #2106
	ldr	r0, [r8, #908]
	mov	r6, #0
	strh	r6, [r5, r3]	@ movhi
	bl	zbuf_free
	movw	r2, #2108
	ldrh	r3, [r5, r2]
	str	r6, [r8, #908]
	cmp	r3, #15
	bhi	.L3212
	add	r1, r3, #1
	add	r3, r3, #1040
	strh	r1, [r5, r2]	@ movhi
	add	r3, r3, #12
	movw	r2, #2142
	ldrh	r2, [r5, r2]
	add	r3, r5, r3, asl #1
	strh	r2, [r3, #6]	@ movhi
	b	.L3213
.L3212:
	movw	r3, #2142
	ldrh	r0, [r5, r3]
	bl	zftl_insert_free_list
.L3213:
	movw	r3, #2142
	ldr	r0, .L3221+40
	ldrh	r1, [r4, r3]
	movw	r3, #2108
	ldrh	r2, [r4, r3]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L3193:
	mov	r3, #0
	strh	r3, [r4, r5]	@ movhi
.L3192:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3222:
	.align	2
.L3221:
	.word	.LANCHOR0+2828
	.word	.LANCHOR0+4096
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+2792
	.word	.LANCHOR0+4986
	.word	.LANCHOR0+4972
	.word	1437269760
	.word	.LANCHOR3-3066
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
	ldr	r6, .L3246
	mov	r3, r0, asl #2
	str	r3, [sp, #8]
	mov	r5, r0
	ldr	r2, [sp, #8]
	mov	r1, r5
	ldr	r3, [r6, #1084]
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, r0, asl #2]
	ubfx	r2, r2, #0, #11
	ldr	r0, .L3246+4
	ubfx	r3, r3, #11, #8
	bl	printk
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3223
	movw	r3, #1080
	ldrh	r3, [r6, r3]
	cmp	r3, r5
	bls	.L3223
	mov	r1, #0
	mov	r0, r5
	bl	ftl_erase_sblk
	mov	r0, r5
	ldr	r8, .L3246+8
	mov	r7, #0
	add	r1, sp, #32
	strh	r5, [sp, #16]	@ movhi
	bl	ftl_get_blk_list_in_sblk
	strb	r0, [sp, #25]
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r6, #1084]
	ldr	r2, [sp, #8]
	mov	r4, r0
	ldr	r0, [r0, #4]
	ldrh	r3, [r3, r2]
	ldrb	r2, [r8, #-2538]	@ zero_extendqisi2
	ubfx	r3, r3, #0, #11
	cmp	r3, #1
	mov	r2, r2, asl #9
	movle	r1, #85
	movgt	r1, #170
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
.L3227:
	ldr	r3, .L3246+12
	uxth	r10, r7
	ldr	fp, .L3246+12
	ldrh	r3, [r3]
	cmp	r3, r10
	bls	.L3242
	add	fp, fp, #30
	mov	r9, #0
.L3230:
	ldrb	r2, [sp, #25]	@ zero_extendqisi2
	uxth	r3, r9
	cmp	r2, r3
	bls	.L3243
	add	r2, sp, #48
	add	r3, r2, r3, asl #1
	movw	r2, #65535
	ldrh	r3, [r3, #-16]
	cmp	r3, r2
	beq	.L3228
	ldrh	r1, [fp]
	mov	ip, #0
	ldr	r2, [r4, #4]
	mvn	r0, #0
	mla	r3, r1, r3, r10
	ldr	r1, .L3246+16
	str	r3, [r2]
	ldr	r2, [r4, #4]
	str	r1, [r2, #4]
	ldr	r2, [r4, #12]
	str	ip, [r2]
	ldrb	r2, [r6, #1153]	@ zero_extendqisi2
	ldrb	lr, [r8, #-2538]	@ zero_extendqisi2
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
.L3228:
	add	r9, r9, #1
	b	.L3230
.L3243:
	add	r7, r7, #1
	b	.L3227
.L3242:
	mov	r10, #0
.L3231:
	ldrh	r1, [fp]
	uxth	r3, r10
	str	r3, [sp, #12]
	cmp	r1, r3
	bls	.L3244
	mov	r9, #0
.L3236:
	ldrb	r1, [sp, #25]	@ zero_extendqisi2
	uxth	r7, r9
	cmp	r1, r7
	bls	.L3245
	add	r7, r7, #8
	add	r3, sp, #48
	add	r7, r3, r7, asl #1
	movw	r3, #65535
	ldrh	r1, [r7, #-32]
	cmp	r1, r3
	beq	.L3233
	ldr	r3, .L3246+20
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
	beq	.L3233
	mov	r0, r8
	bl	ftl_mask_bad_block
	mvn	r1, #0
	strh	r1, [r7, #-32]	@ movhi
.L3233:
	add	r9, r9, #1
	b	.L3236
.L3245:
	add	r10, r10, #1
	b	.L3231
.L3244:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, [r6, #1084]
	ldr	r0, .L3246+24
	mov	r1, r5
	ldr	r2, [sp, #8]
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, r5, asl #2]
	ubfx	r2, r2, #0, #11
	ubfx	r3, r3, #11, #8
	bl	printk
.L3223:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3247:
	.align	2
.L3246:
	.word	.LANCHOR0
	.word	.LC201
	.word	.LANCHOR3
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
	beq	.L3250
	ldr	r6, .L3264
	mov	r8, r0, asl #2
	ldr	r5, [r6, #1084]
	add	r5, r5, r8
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L3251
	ldr	r1, .L3264+4
	mov	r2, #1012
	ldr	r0, .L3264+8
	bl	printk
	bl	dump_stack
.L3251:
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
	beq	.L3255
	ldr	r3, .L3264
	mov	r0, r4
	ldr	r3, [r3, #1084]
	ldrh	r3, [r3, r8]
	ubfx	r3, r3, #0, #11
	orrs	r3, r7, r3
	bne	.L3256
	bl	ftl_ink_check_sblk
	b	.L3255
.L3250:
	bl	print_ftl_debug_info
	mov	r1, r4
	mov	r2, r9
	ldr	r0, .L3264+12
	bl	printk
	mov	r1, r5
	mov	r0, #0
	bl	zftl_get_free_sblk
	mov	r2, r9
	mov	r4, r0
	ldr	r0, .L3264+12
	mov	r1, r4
	bl	printk
	bl	dump_all_list_info
	ldr	r1, .L3264+4
	movw	r2, #1031
	ldr	r0, .L3264+8
	bl	printk
	bl	dump_stack
.L3255:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3256:
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3265:
	.align	2
.L3264:
	.word	.LANCHOR0
	.word	.LANCHOR1+2284
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
	ldr	r6, .L3274
	mov	r5, r1
	ldr	r8, .L3274+4
	mov	r7, r6
.L3267:
	movw	r10, #65535
.L3268:
	mov	r0, r5
	bl	ftl_alloc_sblk
	cmp	r0, r10
	mov	r9, r0
	beq	.L3268
	mov	r1, #0
	ldr	fp, .L3274+8
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
	ldr	r3, [fp, #1092]
	ldrh	r2, [r4, #6]
	strh	r2, [r3, r10]	@ movhi
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	ldr	r0, .L3274+12
	mov	r1, r9
	bl	printk
	ldr	r3, [fp, #1092]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	mov	r3, #7
	strb	r3, [r4, #4]
	b	.L3267
.L3275:
	.align	2
.L3274:
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
	ldr	r5, .L3294
	ldr	r7, .L3294+4
	ldr	r4, [r5, #2804]
	ldrh	r3, [r4, r2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	ldrb	r2, [r7, #-3123]	@ zero_extendqisi2
	cmp	r2, r3
	bls	.L3277
	add	r3, r3, #336
	mov	r3, r3, asl #1
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3278
.L3277:
	ldr	r4, .L3294
	movw	r8, #65535
.L3279:
	mov	r0, #1
	bl	ftl_alloc_sblk
	cmp	r0, r8
	mov	r6, r0
	beq	.L3279
	mov	r1, #0
	bl	ftl_erase_sblk
	ldr	r1, [r5, #2804]
	mov	r0, r6
	add	r1, r1, #672
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L3280
	mov	r1, r6
	ldr	r0, .L3294+8
	bl	printk
	ldr	r3, [r4, #1084]
	add	r6, r3, r6, asl #2
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	orr	r3, r3, #224
	strb	r3, [r6, #2]
	b	.L3279
.L3280:
	ldr	r3, .L3294
	movw	r2, #690
	mov	r4, #0
	movw	r1, #65535
	ldr	r3, [r3, #2804]
	strh	r4, [r3, r2]	@ movhi
	add	r3, r3, #412
	add	r3, r3, #2
	mov	r2, #1
	str	r2, [r7, #-36]
.L3282:
	ldrh	r0, [r3, #2]!
	uxth	r2, r4
	cmp	r0, r1
	beq	.L3281
	add	r4, r4, #1
	cmp	r4, #128
	bne	.L3282
	mov	r2, #264
	ldr	r1, .L3294+12
	ldr	r0, .L3294+16
	bl	printk
	bl	dump_stack
	mov	r2, r4
.L3281:
	add	r2, r2, #208
	ldr	r3, [r5, #2804]
	mov	r2, r2, asl #1
	strh	r6, [r3, r2]	@ movhi
	add	r3, r3, #688
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
.L3278:
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
	bls	.L3284
	ldr	r1, .L3294+12
	movw	r2, #270
	ldr	r0, .L3294+16
	bl	printk
	bl	dump_stack
.L3284:
	ldr	r1, [r5, #2804]
	mov	r2, #0
	add	r3, r1, #696
	strh	r2, [r3]	@ movhi
	ldr	r2, .L3294+20
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	ldrh	r5, [r2, #-12]
	rsb	r3, r3, #24
	movw	r2, #694
	rsb	r5, r5, r3
	mov	r3, r4, asr r5
	strh	r3, [r1, r2]	@ movhi
	ldr	r2, .L3294+24
	add	r1, r1, #692
	strh	r4, [r1]	@ movhi
	ldr	r2, [r2]
	tst	r2, #4096
	beq	.L3288
	mvn	r2, #0
	uxth	r3, r3
	ldr	r0, .L3294+28
	mov	r1, r4
	str	r3, [sp]
	mvn	r2, r2, asl r5
	mov	r3, r4
	bl	printk
.L3288:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3295:
	.align	2
.L3294:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC204
	.word	.LANCHOR1+2300
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
	mov	r5, r0
	ldr	r8, .L3310
	mov	r7, r1
	ldr	r9, .L3310+4
	mov	r6, r8
.L3297:
	ldr	r3, [r8, #2804]
	ldr	r2, [r3, #48]
	add	r2, r2, #1
	str	r2, [r3, #48]
	add	r2, r3, #696
	ldrh	r1, [r2]
	ldrh	r2, [r9]
	cmp	r1, r2
	bcs	.L3298
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3299
.L3298:
	bl	pm_alloc_new_blk
	mov	r0, #0
	bl	ftl_info_flush
.L3299:
	ldr	r3, [r6, #2804]
	add	r3, r3, #692
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3300
	ldr	r1, .L3310+8
	movw	r2, #303
	ldr	r0, .L3310+12
	bl	printk
	bl	dump_stack
.L3300:
	ldr	r3, [r6, #2804]
	ldr	r4, .L3310+16
	add	r2, r3, #692
	add	r3, r3, #696
	ldrh	r1, [r2]
	ldr	r2, .L3310+20
	ldr	r0, [r4, #-32]
	ldrh	r10, [r2]
	ldrh	r2, [r3]
	mla	r10, r10, r1, r2
	mov	r2, #64
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [r4, #-32]
	mov	r0, r7
	str	r5, [r3]
	ldr	r3, [r6, #2804]
	ldr	fp, [r4, #-32]
	ldrb	r1, [r4, #-2538]	@ zero_extendqisi2
	ldr	r3, [r3, #48]
	mov	r1, r1, asl #9
	str	r3, [fp, #4]
	bl	js_hash
	ldr	r3, [r6, #2804]
	mov	r1, r10
	mov	r2, r7
	str	r0, [fp, #8]
	ldrb	r0, [r3, #694]	@ zero_extendqisi2
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	str	r3, [sp]
	ldr	r3, [r4, #-32]
	bl	ftl_prog_page
	ldr	r2, [r6, #2804]
	add	r1, r2, #696
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r1]	@ movhi
	cmp	r3, #1
	beq	.L3301
	ldrb	r3, [r4, #-28]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3302
.L3301:
	mov	r3, #0
	strb	r3, [r4, #-28]
	b	.L3297
.L3302:
	cmn	r0, #1
	bne	.L3304
	mov	r1, r10
	ldr	r0, .L3310+24
	bl	printk
	b	.L3297
.L3304:
	movw	r3, #698
	mov	r0, #0
	ldrh	r3, [r2, r3]
	cmp	r5, r3
	addcc	r5, r5, #176
	strcc	r10, [r2, r5, asl #2]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3311:
	.align	2
.L3310:
	.word	.LANCHOR0
	.word	.LANCHOR3-3096
	.word	.LANCHOR1+2320
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
	ldr	r3, .L3326
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L3313
	ldr	r0, .L3326+4
	movw	r1, #365
	ldr	r2, .L3326+8
	bl	printk
.L3313:
	ldr	r4, .L3326+12
	mov	r1, #0
	ldr	r6, .L3326+16
	mov	r2, #64
	ldr	r10, .L3326+20
	mov	r8, #0
	ldr	r0, [r4, #-24]
	mov	r7, r4
	bl	ftl_memset
	ldr	r0, .L3326+24
	ldr	r1, [r6, #1040]
	mov	r2, #4
	mov	r3, #16
	mov	r9, r6
	bl	rknand_print_hex
	ldr	r5, [r6, #1040]
	add	r0, r5, #16
	ldr	r1, [r5, #8]
	bl	js_hash
	str	r0, [r5, #12]
.L3314:
	ldrb	r5, [r4, #-20]	@ zero_extendqisi2
	ldrh	fp, [r4, #-18]
	ldrh	r3, [r6, #2]
	ldr	r0, .L3326+28
	mov	r1, r5
	mov	r2, fp
	str	r3, [sp, #12]
	bl	printk
	ldrh	r2, [r4, #-216]
	ldrh	r0, [r4, #-18]
	sub	r2, r2, #1
	cmp	r0, r2
	ldr	r3, [sp, #12]
	blt	.L3315
	ldr	r5, [r9, #1040]
	mov	fp, #0
	ldrb	r2, [r7, #-19]	@ zero_extendqisi2
	strh	fp, [r7, #-18]	@ movhi
	ldr	r3, [r5, #4]
	mov	r0, r5
	add	r3, r3, #1
	str	r3, [r5, #4]
	ldrb	r3, [r7, #-20]	@ zero_extendqisi2
	strb	r2, [r7, #-20]
	strb	r3, [r7, #-19]
	ldrh	r3, [r5, #16]
	add	r3, r3, #1
	strh	r3, [r0, #16]!	@ movhi
	ldr	r1, [r5, #8]
	bl	js_hash
	ldrb	r3, [r7, #-20]	@ zero_extendqisi2
	str	r0, [r5, #12]
	mov	r0, fp
	ldrh	r5, [r9, #2]
	mul	r5, r5, r3
	b	.L3324
.L3315:
	cmp	r0, #0
	mla	r5, r3, r5, fp
	bne	.L3316
.L3324:
	mov	r1, r5
	bl	flash_erase_block
.L3316:
	ldr	r2, [r6, #1040]
	mov	fp, #1
	ldr	r3, [r4, #-24]
	mov	r0, #0
	mov	r1, r5
	ldr	r2, [r2, #4]
	str	r2, [r3]
	mov	r2, #4
	ldr	r3, [r4, #-24]
	str	r10, [r3, #4]
	stmia	sp, {r2, fp}
	ldr	r2, [r6, #1040]
	bl	flash_prog_page_en
	ldrh	r3, [r4, #-18]
	add	r3, r3, fp
	strh	r3, [r4, #-18]	@ movhi
	cmn	r0, #1
	bne	.L3317
	mov	r1, r5
	ldr	r0, .L3326+32
	bl	printk
	b	.L3314
.L3317:
	cmp	r8, #0
	moveq	r8, fp
	beq	.L3314
.L3325:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3327:
	.align	2
.L3326:
	.word	.LANCHOR2
	.word	.LC135
	.word	.LANCHOR1+2336
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	1398362953
	.word	.LC207
	.word	.LC208
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
	ldr	r3, .L3361
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3]
	ldr	r6, .L3361+4
	tst	r3, #4096
	beq	.L3329
	ldr	r3, [r6, #1040]
	mov	r1, #50
	ldr	r0, .L3361+8
	ldr	r2, .L3361+12
	str	r3, [sp]
	mov	r3, #2048
	bl	printk
.L3329:
	ldr	ip, .L3361+4
	mov	r7, #4
	ldr	fp, .L3361+16
.L3330:
	mov	r5, #0
.L3333:
	ldrh	r1, [r6, #2]
	mov	r8, #4
	ldr	r4, .L3361+20
	mov	r0, #0
	str	r8, [sp]
	mla	r1, r1, r7, r5
	ldr	r2, [r6, #1040]
	ldr	r3, [r4, #-24]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	ldr	r9, .L3361+4
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3331
	ldr	r2, [ip, #1040]
	ldr	r10, .L3361+16
	ldr	r3, [r2]
	cmp	r3, fp
	beq	.L3332
.L3331:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L3333
	add	r7, r7, #1
	cmp	r7, #16
	bne	.L3330
	b	.L3342
.L3332:
	ldrb	r1, [r2, #37]	@ zero_extendqisi2
	mov	r0, #0
	ldrb	r3, [r2, #36]	@ zero_extendqisi2
	strb	r1, [r4, #-19]
	ldrh	r1, [r9, #2]
	strb	r3, [r4, #-20]
	str	r8, [sp]
	mul	r1, r1, r3
	ldr	r3, [r4, #-24]
	bl	flash_read_page_en
	cmn	r0, #1
	beq	.L3343
	ldr	r3, [r9, #1040]
	ldr	r2, [r3]
	cmp	r2, fp
	ldreq	r5, [r3, #4]
	bne	.L3343
.L3335:
	ldrb	r3, [r4, #-19]	@ zero_extendqisi2
	mov	r2, #4
	ldrh	r1, [r6, #2]
	mov	r0, #0
	str	r2, [sp]
	ldr	r2, [r6, #1040]
	mul	r1, r1, r3
	ldr	r3, [r4, #-24]
	bl	flash_read_page_en
	ldr	r7, .L3361+20
	cmn	r0, #1
	bne	.L3360
.L3336:
	ldr	r3, [r4, #-24]
	mov	r8, #4
	ldrb	r1, [r4, #-20]	@ zero_extendqisi2
	mov	r0, #0
	str	r8, [sp]
	mov	r7, #0
	ldr	r2, [r6, #1040]
	bl	flash_get_last_written_page
	ldrh	r9, [r6, #2]
	ldr	ip, .L3361+4
	uxth	fp, r0
	add	r3, fp, #1
	strh	r3, [r4, #-18]	@ movhi
	ldrb	r3, [r4, #-20]	@ zero_extendqisi2
	mla	r9, r9, r3, r0
.L3337:
	rsb	r5, r7, fp
	sxth	r5, r5
	cmp	r5, #0
	bge	.L3340
	cmn	r5, #1
	bne	.L3339
	ldr	r3, [r6, #1040]
	ldr	r0, .L3361+24
	ldr	r1, [r3]
	bl	printk
.L3342:
	mvn	r0, #0
	b	.L3358
.L3343:
	mov	r5, #0
	b	.L3335
.L3360:
	ldr	r3, .L3361+4
	ldr	r3, [r3, #1040]
	ldr	r2, [r3]
	cmp	r2, r10
	bne	.L3336
	ldr	r2, [r3, #4]
	cmp	r5, r2
	ldrccb	r2, [r3, #37]	@ zero_extendqisi2
	ldrccb	r3, [r3, #36]	@ zero_extendqisi2
	strccb	r2, [r7, #-20]
	strccb	r3, [r7, #-19]
	b	.L3336
.L3340:
	str	r8, [sp]
	mov	r0, #0
	rsb	r1, r7, r9
	ldr	r2, [r6, #1040]
	ldr	r3, [r4, #-24]
	str	ip, [sp, #12]
	bl	flash_read_page_en
	cmn	r0, #1
	ldr	ip, [sp, #12]
	beq	.L3338
	ldr	r3, [ip, #1040]
	ldr	r3, [r3]
	cmp	r3, r10
	beq	.L3339
.L3338:
	add	r7, r7, #1
	b	.L3337
.L3339:
	cmp	r7, #1
	bls	.L3344
	bl	flash_info_flush
.L3344:
	mov	r0, #0
.L3358:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3362:
	.align	2
.L3361:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC210
	.word	.LANCHOR1+2356
	.word	1398362953
	.word	.LANCHOR3
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
	ldr	r6, .L3459
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3364
	ldr	r0, .L3459+4
	movw	r1, #3450
	ldr	r2, .L3459+8
	bl	printk
.L3364:
	ldr	r5, .L3459+12
	mov	r0, r4
	mov	r7, #0
	ldr	r4, .L3459+16
	ldr	r8, .L3459+20
	str	r7, [r5, #-96]
	bl	nandc_init
	ldr	r3, .L3459+24
	mov	r1, r7
	mov	r2, #8
	ldr	r0, .L3459+28
	mov	r10, r4
	mov	fp, r8
	str	r3, [r4, #1104]
	mov	r3, #1
	strb	r3, [r4, #1109]
	mov	r3, #3
	strb	r3, [r4, #1153]
	bl	ftl_memset
	sub	r0, r5, #212
	mov	r1, r7
	mov	r2, #32
	bl	ftl_memset
.L3370:
	mov	r9, r7, asl #3
	uxtb	r0, r7
	add	r1, r8, r9
	bl	flash_read_id
	cmp	r7, #0
	ldr	r1, .L3459+20
	bne	.L3365
	ldrb	r3, [r10, #1164]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bls	.L3366
.L3368:
	mvn	r7, #1
	b	.L3448
.L3366:
	ldrb	r3, [r10, #1165]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L3368
.L3365:
	ldrb	r3, [r9, fp]	@ zero_extendqisi2
	add	r7, r7, #1
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r9, r8]
	cmp	r7, #4
	bne	.L3370
	ldr	ip, .L3459+32
	mov	r3, #0
.L3408:
	mov	r0, r3, asl #5
	ldrb	r8, [ip, r3, asl #5]	@ zero_extendqisi2
	add	r7, r0, #1
	ldr	lr, .L3459+32
	add	r7, ip, r7
	mov	r2, #0
.L3371:
	cmp	r2, r8
	bcs	.L3458
	ldrb	r10, [r7, r2]	@ zero_extendqisi2
	ldrb	r9, [r2, r1]	@ zero_extendqisi2
	cmp	r10, r9
	bne	.L3372
	add	r2, r2, #1
	b	.L3371
.L3458:
	adds	r8, lr, r0
	beq	.L3368
	ldr	r3, .L3459
	ldr	r2, .L3459+36
	add	r0, r3, r0
	mov	r3, #0
	ldrb	r1, [r0, #446]	@ zero_extendqisi2
.L3376:
	ldrb	r0, [r2, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r1
	beq	.L3375
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L3376
.L3375:
	ldr	r1, .L3459+36
	mov	r2, #32
	ldr	r7, .L3459
	add	r1, r1, r3, asl #5
	ldr	r0, .L3459+40
	bl	ftl_memcpy
	add	r0, r7, #4
	mov	r1, r8
	mov	r2, #32
	bl	ftl_memcpy
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #8
	bhi	.L3377
	ldrb	r2, [r7, #24]	@ zero_extendqisi2
	cmp	r2, #60
	movhi	r2, #60
	strhib	r2, [r7, #24]
	cmp	r3, #6
	beq	.L3368
.L3377:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3379
	ldr	r0, .L3459+4
	movw	r1, #3480
	ldr	r2, .L3459+8
	bl	printk
.L3379:
	ldr	r3, [r4, #1104]
	mov	r0, #16384
	mov	r7, #0
	ldrh	r3, [r3, #10]
	cmp	r3, #1024
	ldrcs	r3, .L3459+16
	movcs	r2, #2
	strcsb	r2, [r3, #1153]
	bl	ftl_malloc
	str	r0, [r5, #-88]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-112]
	mov	r0, #2048
	bl	ftl_dma32_malloc
	str	r0, [r4, #1040]
	mov	r0, #64
	bl	ftl_dma32_malloc
	str	r0, [r5, #-84]
	mov	r0, #64
	bl	ftl_dma32_malloc
	str	r0, [r5, #-104]
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
	str	r7, [r5, #-92]
	ldrh	r3, [r1, #16]
	ubfx	r2, r3, #8, #3
	strb	r2, [r4, #1196]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #-72]
	ubfx	r2, r3, #4, #1
	strb	r2, [r4, #1198]
	ubfx	r2, r3, #12, #1
	strb	r2, [r5, #-3116]
	ubfx	r2, r3, #13, #1
	strb	r2, [r5, #-3122]
	ubfx	r2, r3, #11, #1
	strb	r2, [r4, #1160]
	ldrb	r2, [r1, #31]	@ zero_extendqisi2
	ldrb	r1, [r1, #28]	@ zero_extendqisi2
	ubfx	r0, r2, #1, #1
	strb	r0, [r5, #-2534]
	ubfx	r0, r2, #2, #1
	strb	r0, [r5, #-15]
	ubfx	r0, r3, #14, #1
	mov	r3, r3, lsr #15
	strb	r3, [r5, #-3121]
	ubfx	r3, r2, #3, #1
	strb	r3, [r4, #1152]
	mov	r3, #60
	strb	r3, [r4, #1108]
	ubfx	r2, r2, #4, #1
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	strb	r2, [r4, #1161]
	cmp	r3, #9
	strb	r1, [r4]
	strb	r1, [r4, #1110]
	ldreq	r2, .L3459+16
	ldrb	r1, [r6, #35]	@ zero_extendqisi2
	strb	r0, [r4, #1]
	moveq	r0, #70
	streqb	r0, [r2, #1108]
	tst	r1, #1
	ldr	r2, .L3459+16
	beq	.L3382
	ldr	r1, .L3459
	ldrb	r1, [r1, #33]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, #2
	moveq	r1, #3
	strb	r1, [r2, #1110]
.L3382:
	cmp	r3, #8
	bne	.L3384
	ldrb	r3, [r4, #1164]	@ zero_extendqisi2
	cmp	r3, #44
	cmpne	r3, #137
	bne	.L3384
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #3
	ldreq	r3, .L3459+16
	moveq	r2, #0
	streqb	r2, [r3, #1110]
.L3384:
	ldrh	r2, [r6, #20]
	ldrb	r3, [r6, #23]	@ zero_extendqisi2
	tst	r2, #64
	ldr	r0, .L3459+16
	strb	r3, [r4, #1100]
	beq	.L3386
	sub	r2, r3, #17
	sub	r1, r3, #21
	clz	r1, r1
	cmp	r3, #21
	cmpne	r2, #2
	mov	r1, r1, lsr #5
	bhi	.L3387
	ldr	r2, .L3459+44
	cmp	r1, #0
	ldr	r3, .L3459+12
	str	r2, [r5, #-92]
	moveq	r2, #15
	movne	r2, #4
	strb	r2, [r3, #-100]
	b	.L3386
.L3387:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r3, .L3459+48
	strls	r3, [r5, #-92]
	movls	r3, #4
	strlsb	r3, [r0, #1101]
	bls	.L3457
.L3390:
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
	beq	.L3391
	ldr	r2, .L3459+48
	ldr	r3, .L3459+12
	str	r2, [r5, #-92]
	orrs	r2, lr, ip
	moveq	r2, #7
	movne	r2, #17
	cmp	r1, #0
	strb	r2, [r3, #-100]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #1101]
	b	.L3386
.L3391:
	sub	r1, r3, #36
	cmp	r1, #1
	bhi	.L3395
	ldr	r3, .L3459+52
	str	r3, [r5, #-92]
.L3457:
	mov	r3, #7
	b	.L3455
.L3395:
	cmp	r3, #50
	bne	.L3396
	ldr	r3, .L3459+56
	str	r3, [r5, #-92]
	mov	r3, #25
.L3455:
	strb	r3, [r5, #-100]
	b	.L3386
.L3396:
	cmp	r3, #81
	streqb	r2, [r0, #1199]
	ldreq	r3, .L3459+60
	streq	r3, [r5, #-92]
	moveq	r3, #7
	streqb	r3, [r5, #-100]
.L3386:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3397
	ldr	r0, .L3459+4
	movw	r1, #3573
	ldr	r2, .L3459+8
	bl	printk
.L3397:
	mov	r3, #0
	strb	r3, [r4, #1143]
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	tst	r3, #1
	moveq	r0, #4
	beq	.L3456
	ldr	r3, .L3459+16
	ldrb	r3, [r3, #1164]	@ zero_extendqisi2
	cmp	r3, #155
	beq	.L3399
	mov	r0, #4
	bl	flash_set_interface_mode
	mov	r0, #4
	bl	nandc_set_if_mode
.L3399:
	mov	r0, #1
	bl	flash_set_interface_mode
	mov	r0, #1
.L3456:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	r0, #1
	mov	r7, r0
	bne	.L3401
	ldr	r3, [r4, #1040]
	mov	r2, #17
	mov	r0, #0
	strb	r0, [r4, #1156]
	strb	r2, [r3, #32]
	bl	zftl_flash_exit_slc_mode
	b	.L3448
.L3401:
	ldrb	r3, [r6, #11]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L3402
	ldr	r3, [r4, #1040]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	cmp	r3, #1
	ldrne	r3, .L3459+16
	movne	r2, #0
	strneb	r2, [r3, #1110]
.L3402:
	ldrb	r3, [r4, #1100]	@ zero_extendqisi2
	ldr	r7, .L3459+16
	sub	r3, r3, #1
	cmp	r3, #7
	ldrls	r3, .L3459+64
	strls	r3, [r5, #-92]
	ldrb	r3, [r4, #1196]	@ zero_extendqisi2
	tst	r3, #4
	beq	.L3405
	ldr	r3, [r7, #1040]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3405
	ldrb	r3, [r5, #-20]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r1, [r7, #2]
	mul	r1, r1, r3
	bl	flash_ddr_para_scan
	ldrb	r3, [r7, #1143]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3405
	ldr	r2, [r7, #1040]
	strb	r3, [r2, #19]
	bl	flash_info_flush
.L3405:
	ldr	r3, [r6]
	tst	r3, #4096
	beq	.L3407
	ldr	r0, .L3459+4
	movw	r1, #3676
	ldr	r2, .L3459+8
	bl	printk
.L3407:
	bl	nand_flash_print_info
	mov	r7, #0
	b	.L3448
.L3372:
	add	r3, r3, #1
	cmp	r3, #49
	bne	.L3408
	b	.L3368
.L3448:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3460:
	.align	2
.L3459:
	.word	.LANCHOR2
	.word	.LC135
	.word	.LANCHOR1+2376
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR0+1164
	.word	.LANCHOR2+4
	.word	.LANCHOR0+1144
	.word	.LANCHOR2+424
	.word	.LANCHOR2+1992
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
	ldr	ip, .L3473
	mov	r6, #0
	mov	r5, r6
	ldr	fp, [r0, #4]
	mov	r4, r0
.L3462:
	ldr	r3, .L3473+4
	uxth	r8, r6
	ldrh	r3, [r3]
	cmp	r3, r8
	bls	.L3472
	ldrh	r7, [ip]
	add	r6, r6, #1
	ldr	r3, .L3473+8
	ldr	r1, [r4, #4]
	mla	r7, r7, r10, r8
	ldr	r2, [r4, #12]
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	str	ip, [sp, #44]
	mov	r0, r7
	bl	ftl_read_ppa_page
	ldr	r2, [r4, #4]
	ldr	r3, [r4, #12]
	str	r0, [sp]
	mov	r9, r0
	ldr	r1, [r2]
	ldr	r0, .L3473+12
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
	b	.L3462
.L3472:
	ldr	r0, .L3473+16
	add	r1, fp, #704
	mov	r2, #4
	mov	r3, #32
	bl	rknand_print_hex
	mov	r0, r4
	bl	zbuf_free
	cmp	r5, #0
	beq	.L3465
	ldr	r1, .L3473+20
	movw	r2, #1619
	ldr	r0, .L3473+24
	bl	printk
	bl	dump_stack
.L3465:
	mov	r0, r5
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3474:
	.align	2
.L3473:
	.word	.LANCHOR3-3066
	.word	.LANCHOR3-3096
	.word	.LANCHOR3
	.word	.LC196
	.word	.LC212
	.word	.LANCHOR1+2392
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
	ldr	r5, .L3614
	mov	r4, r0
	str	r1, [sp, #28]
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3476
	ldr	r0, .L3614+4
	ldrh	r1, [r4, #2]
	bl	printk
.L3476:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3477
	ldr	r0, .L3614+8
	ldrb	r1, [r4, #5]	@ zero_extendqisi2
	bl	printk
.L3477:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3478
	ldr	r0, .L3614+12
	ldrh	r1, [r4]
	bl	printk
.L3478:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3479
	ldr	r0, .L3614+16
	ldrh	r1, [r4, #16]
	ldrh	r2, [r4, #18]
	bl	printk
.L3479:
	ldr	r3, [r5]
	tst	r3, #4096
	beq	.L3480
	ldr	r0, .L3614+20
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	bl	printk
.L3480:
	ldrh	r3, [r4, #10]
	ldr	r1, .L3614+24
	ldrh	r2, [r4]
	strh	r3, [r4, #14]	@ movhi
	movw	r3, #1080
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcs	.L3475
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, .L3614+28
	mov	r1, #255
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
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
.L3482:
	ldr	r3, .L3614+32
	ldrh	r3, [r3]
	cmp	r3, r7
	bls	.L3485
	ldrb	r6, [r4, #5]	@ zero_extendqisi2
	ldr	r8, .L3614+28
.L3483:
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L3610
	add	r3, r4, r6, asl #1
	ldrh	r10, [r3, #16]
	movw	r3, #65535
	cmp	r10, r3
	beq	.L3484
	ldr	r3, .L3614+36
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrh	r3, [r3]
	mla	r3, r3, r10, r7
	str	r3, [sp, #20]
	mov	r0, r3
	ldrb	r3, [r8, #-2538]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmp	r0, #512
	mov	r9, r0
	beq	.L3485
	cmn	r0, #1
	beq	.L3486
	ldr	r3, [r5, #12]
	ldr	r2, [r3]
	cmn	r2, #1
	bne	.L3486
	ldr	r3, [r3, #4]
	cmn	r3, #1
	bne	.L3486
	ldr	r3, [r5, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3485
.L3486:
	ldr	r2, .L3614+28
	mov	r3, #1
	strb	r3, [r2, #-47]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r2, [r4, #10]
	mla	r3, r3, r7, r6
	cmp	r2, r3
	beq	.L3487
	ldr	r1, .L3614+40
	movw	r2, #1694
	ldr	r0, .L3614+44
	bl	printk
	bl	dump_stack
.L3487:
	ldrh	r3, [r4, #6]
	ldrh	r2, [r4, #10]
	add	r2, r3, r2
	ldr	r3, .L3614+32
	ldrh	r1, [r3]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L3488
	ldr	r1, .L3614+40
	movw	r2, #1695
	ldr	r0, .L3614+44
	bl	printk
	bl	dump_stack
.L3488:
	ldr	r3, .L3614+28
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	cmp	r3, #8
	bls	.L3489
	ldr	r3, [r5, #12]
	ldr	r1, .L3614+48
	ldr	r2, [r3]
	cmp	r2, r1
	beq	.L3489
	cmn	r9, #1
	beq	.L3494
	ldr	r2, [r3, #4]
	cmn	r2, #1
	beq	.L3527
	ldr	r2, [r3, #16]
	ldr	r1, .L3614+52
	cmp	r2, r1
	bne	.L3527
	ldr	r0, [sp, #16]
	mov	r1, #1024
	ldr	fp, [r3, #20]
	bl	js_hash
	cmp	fp, r0
	beq	.L3527
	mov	r1, #1024
	ldr	r0, [sp, #16]
	bl	js_hash
	mov	r1, r10
	mov	r2, r7
	str	r9, [sp]
	str	r0, [sp, #4]
	ldr	r3, [sp, #20]
	ldr	r0, .L3614+56
	bl	printk
	ldr	r0, .L3614+60
	mov	r2, #4
	mov	r3, #16
	ldr	r1, [sp, #16]
	bl	rknand_print_hex
	ldrb	r3, [r8, #-2538]	@ zero_extendqisi2
	ldr	r0, .L3614+64
	mov	r2, #4
	ldr	r1, [r5, #12]
	mov	r3, r3, lsr #1
	bl	rknand_print_hex
	b	.L3494
.L3489:
	cmn	r9, #1
	beq	.L3494
.L3527:
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #4]
	cmn	r0, #1
	beq	.L3494
	ldr	r3, [r3]
	ldr	r2, .L3614+48
	cmp	r3, r2
	beq	.L3494
	bl	lpa_hash_get_ppa
	ldr	r2, [sp, #28]
	mov	r3, r0
	cmp	r2, #0
	beq	.L3493
	ldr	r2, [r5, #12]
	ldr	r1, [r2, #8]
	str	r2, [sp, #24]
	cmp	r0, r1
	cmnne	r0, #1
	beq	.L3493
	ldr	r10, .L3614+24
	mov	r9, #1
	ldr	fp, .L3614+68
	str	r3, [sp, #12]
	ldrb	r1, [r10, #1153]	@ zero_extendqisi2
	ldrh	r0, [fp]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r9, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldr	r3, .L3614+28
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	uxth	r0, r0
	ldr	r2, [sp, #24]
	ldrh	r1, [r3]
	ldr	r3, [sp, #12]
	cmp	r1, r0
	bne	.L3493
	ldr	ip, [r2]
	mov	r0, r3
	ldr	r1, [r5, #4]
	ldrb	r3, [r8, #-2538]	@ zero_extendqisi2
	ldr	r2, [r8, #-68]
	str	ip, [sp, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r8, #-68]
	ldr	r3, [r3]
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bcc	.L3493
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #8]
	cmn	r0, #1
	beq	.L3494
	ldrb	r3, [r10, #1153]	@ zero_extendqisi2
	ldrh	r2, [fp]
	rsb	r3, r3, #24
	ldrb	r1, [r8, #-3130]	@ zero_extendqisi2
	rsb	r3, r2, r3
	mov	r9, r9, asl r3
	sub	r9, r9, #1
	and	r0, r9, r0, lsr r2
	bl	__aeabi_uidiv
	uxth	r0, r0
	bl	ftl_vpn_decrement
	b	.L3494
.L3493:
	ldr	r1, [r5, #12]
	ldr	r2, .L3614+24
	ldr	r0, [r1, #4]
	ldr	r3, [r2, #2784]
	cmp	r0, r3
	bcs	.L3494
	ldr	r3, .L3614+32
	ldrb	ip, [r4, #9]	@ zero_extendqisi2
	ldrh	r0, [r4, #10]
	ldrh	r3, [r3]
	mul	r3, r3, ip
	sub	r3, r3, #1
	cmp	r0, r3
	bge	.L3494
	ldr	r3, [r2, #2804]
	ldr	r1, [r1]
	ldr	r2, [r3, #8]
	cmp	r1, r2
	strhi	r1, [r3, #8]
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
.L3494:
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
.L3484:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3483
.L3610:
	add	r7, r7, #1
	mov	r3, #0
	strb	r3, [r4, #5]
	uxth	r7, r7
	b	.L3482
.L3485:
	ldrh	r3, [r4, #6]
	strb	r6, [r4, #5]
	ldrh	r6, [r4, #10]
	ldrb	fp, [r4, #9]	@ zero_extendqisi2
	add	r6, r3, r6
	ldr	r3, .L3614+72
	strh	r7, [r4, #2]	@ movhi
	ldrh	ip, [r3, #-8]
	str	r3, [sp, #16]
	mul	fp, ip, fp
	cmp	r6, fp
	beq	.L3498
	ldr	r1, .L3614+40
	movw	r2, #1802
	ldr	r0, .L3614+44
	bl	printk
	bl	dump_stack
.L3498:
	ldr	r9, .L3614+28
	mov	r8, #0
	ldrh	r6, [r4, #10]
	mov	r7, r8
	ldr	r10, [r9, #-2548]
.L3499:
	cmp	r7, r6
	bcs	.L3611
	ldrh	r3, [r4, #12]
	add	r3, r7, r3
	add	r7, r7, #1
	ldr	r3, [r10, r3, asl #2]
	cmn	r3, #1
	addne	r8, r8, #1
	uxthne	r8, r8
	b	.L3499
.L3611:
	ldr	r2, [sp, #16]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	ldrh	r10, [r2, #-8]
	smulbb	r3, r3, r10
	ldr	r10, .L3614+24
	rsb	r6, r6, r3
	add	r6, r8, r6
	ldr	r8, .L3614
	uxth	r6, r6
	ldr	r3, [r8]
	tst	r3, #4096
	beq	.L3502
	ldrh	r1, [r4]
	mov	r2, r6
	ldr	ip, [r10, #1092]
	ldr	r0, .L3614+76
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L3502:
	ldrh	r3, [r4]
	ldr	r2, [r10, #1092]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [r8]
	tst	r3, #16384
	beq	.L3503
	ldr	r3, [sp, #44]
	add	r1, sp, #32
	ldr	r0, .L3614+80
	str	r3, [sp]
	ldmia	r1, {r1, r2, r3}
	bl	printk
.L3503:
	ldrb	r2, [r9, #-2538]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r5, #4]
	mov	r7, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L3504:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r7, r3
	bcs	.L3612
	add	ip, sp, #32
	ldr	r0, [ip, r7, asl #2]
	cmn	r0, #1
	bne	.L3505
.L3509:
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
	b	.L3506
.L3505:
	ldr	r3, .L3614+28
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
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
	beq	.L3507
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3614+84
	ldr	r2, [ip, r7, asl #2]
	ldr	r3, [r3, #4]
	bl	printk
.L3507:
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
	beq	.L3509
.L3506:
	ldr	r3, [sp, #24]
	cmp	r3, #1
	bne	.L3511
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bls	.L3511
.L3581:
	mov	r0, r4
	bl	ftl_get_new_free_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3513
	ldrh	r1, [r4, #12]
	ldr	r2, [r5, #12]
	ldrh	r3, [r4, #10]
	ldr	r0, .L3614+88
	add	r3, r3, r1
	ldr	r2, [r2, #4]
	mov	r1, fp
	sub	r3, r3, #1
	bl	printk
.L3513:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	ldrh	r2, [r4, #6]
	mul	r3, r3, r1
	add	r3, r3, #1
	rsb	r3, r7, r3
	cmp	r2, r3
	bls	.L3511
	ldr	r3, .L3614+28
	mov	r0, fp
	ldr	r2, [r5, #12]
	ldr	r1, [r5, #4]
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	bl	ftl_prog_ppa_page
	str	r0, [sp, #20]
	ldrh	r0, [r4]
	bl	ftl_vpn_decrement
	adds	r3, r6, #1
	movne	r3, #1
	ldr	r2, [sp, #20]
	cmn	r2, #1
	cmnne	r6, #1
	beq	.L3514
	add	r3, sp, #96
	ldrh	r2, [r4, #12]
	str	fp, [r3, r7, asl #2]
	ldrh	r3, [r4, #10]
	add	r3, r3, r2
	add	r2, sp, #160
	sub	r3, r3, #1
	str	r3, [r2, r7, asl #2]
	b	.L3511
.L3514:
	ldrh	r2, [r4, #6]
	cmp	r2, #1
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L3581
.L3511:
	add	r7, r7, #1
	b	.L3504
.L3612:
	ldr	r10, .L3614+28
	add	r7, sp, #32
	mov	r6, #0
.L3517:
	ldrb	r3, [r9, #-3123]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	mul	r3, r3, r2
	cmp	r6, r3
	bcs	.L3613
	add	fp, sp, #96
	ldr	r3, [fp, r6, asl #2]
	cmn	r3, #1
	beq	.L3519
	ldr	r0, [r7, r6, asl #2]
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	ldrb	r3, [r10, #-2538]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #256
	bne	.L3519
	ldr	r0, [fp, r6, asl #2]
	ldrb	r3, [r10, #-2538]	@ zero_extendqisi2
	ldr	r1, [r5, #4]
	ldr	r2, [r5, #12]
	bl	ftl_read_ppa_page
	ldr	r3, [r8]
	tst	r3, #16384
	mov	fp, r0
	beq	.L3521
	ldr	r3, [r5, #12]
	mov	r1, fp
	ldr	r0, .L3614+88
	ldr	r2, [r3, #8]
	ldr	r3, [r7, r6, asl #2]
	bl	printk
.L3521:
	cmn	fp, #1
	beq	.L3519
	ldr	r3, [r5, #12]
	ldr	r2, [r7, r6, asl #2]
	ldr	r1, [r3, #8]
	cmp	r2, r1
	bne	.L3519
	ldr	r1, [r8]
	add	fp, sp, #160
	tst	r1, #16384
	beq	.L3522
	ldr	r1, [r3, #4]
	ldr	r0, .L3614+92
	ldr	r3, [fp, r6, asl #2]
	bl	printk
.L3522:
	ldr	r3, [r5, #12]
	ldr	r2, [fp, r6, asl #2]
	ldmib	r3, {r0, r1}
	uxth	r2, r2
	bl	lpa_hash_update_ppa
.L3519:
	add	r6, r6, #1
	b	.L3517
.L3613:
	mov	r0, r5
	bl	zbuf_free
	ldrh	r3, [r4, #12]
	ldrb	r1, [r4, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #16]
	ldrh	r2, [r2, #-8]
	mla	r3, r1, r2, r3
	ldr	r2, .L3614+28
	ldr	r2, [r2, #-2548]
	sub	r3, r3, #-1073741823
	ldr	r3, [r2, r3, asl #2]
	cmn	r3, #1
	beq	.L3524
	ldr	r1, .L3614+40
	movw	r2, #1917
	ldr	r0, .L3614+44
	bl	printk
	bl	dump_stack
.L3524:
	ldrh	r3, [r4, #6]
	cmp	r3, #1
	bne	.L3475
	mov	r0, r4
	bl	ftl_write_last_log_page
.L3475:
	add	sp, sp, #228
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3615:
	.align	2
.L3614:
	.word	.LANCHOR2
	.word	.LC213
	.word	.LC214
	.word	.LC215
	.word	.LC216
	.word	.LC217
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3096
	.word	.LANCHOR3-3066
	.word	.LANCHOR1+2408
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
	movw	r7, #1080
	ldr	r4, .L3618
	ldr	r5, .L3618+4
	ldr	r0, .L3618+8
	ldrb	r1, [r4, #2773]	@ zero_extendqisi2
	sub	r6, r5, #3088
	bl	printk
	ldrb	r1, [r5, #-56]	@ zero_extendqisi2
	ldrb	r2, [r5, #-55]	@ zero_extendqisi2
	ldrh	r3, [r5, #-54]
	ldr	r0, .L3618+12
	bl	printk
	ldr	r3, [r4, #2804]
	ldr	r0, .L3618+16
	ldrh	r1, [r3, #130]
	ldrh	r2, [r3, #140]
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #22]
	ldrh	r1, [r0, #16]
	ldrh	r2, [r0, #18]
	ldrb	r3, [r0, #21]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #26]
	str	r0, [sp, #4]
	ldr	r0, .L3618+20
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #54]
	ldrh	r1, [r0, #48]
	ldrh	r2, [r0, #50]
	ldrb	r3, [r0, #53]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #58]
	str	r0, [sp, #4]
	ldr	r0, .L3618+24
	bl	printk
	ldr	r0, [r4, #1096]
	ldrh	ip, [r0, #86]
	ldrh	r1, [r0, #80]
	ldrh	r2, [r0, #82]
	ldrb	r3, [r0, #85]	@ zero_extendqisi2
	str	ip, [sp]
	ldrh	r0, [r0, #90]
	str	r0, [sp, #4]
	ldr	r0, .L3618+28
	bl	printk
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldr	r1, [r5, #-2548]
	ldr	r0, .L3618+32
	mul	r3, r3, r2
	mov	r2, #4
	mov	r3, r3, asl #1
	bl	rknand_print_hex
	ldr	r1, [r4, #1092]
	ldrh	r3, [r4, r7]
	mov	r2, #2
	ldr	r0, .L3618+36
	bl	rknand_print_hex
	ldr	r3, [r4, #2804]
	movw	ip, #698
	ldr	r0, .L3618+40
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #1084]
	ldrh	r3, [r4, r7]
	mov	r2, #4
	ldr	r0, .L3618+44
	bl	rknand_print_hex
	sub	r1, r5, #3056
	ldr	r0, .L3618+48
	sub	r1, r1, #4
	mov	r2, #2
	mov	r3, #256
	bl	rknand_print_hex
	ldrh	r2, [r6, #-8]
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldr	r0, .L3618+52
	ldr	r1, [r5, #-2544]
	mul	r3, r3, r2
	mov	r2, #2
	mov	r3, r3, asl #1
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, lr}
	b	rknand_print_hex
.L3619:
	.align	2
.L3618:
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
	ldr	r3, .L3624
	mov	r6, r0
	ldr	r7, .L3624+4
	mov	r5, r1
	sub	r2, r3, #3120
	mvn	r1, #0
	ldrh	r0, [r2, #-12]
	ldrb	r2, [r7, #1153]	@ zero_extendqisi2
	rsb	r2, r2, #24
	rsb	r2, r0, r2
	mov	r0, r4, lsr r0
	bic	r0, r0, r1, asl r2
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r3, [r7, #1084]
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
	ldr	r0, .L3624+8
	bl	printk
	bl	dump_ftl_info
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3625:
	.align	2
.L3624:
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
	mov	r7, r0
	mov	r6, r1
	bls	.L3627
	ldr	r1, .L3641
	mov	r2, #32
	ldr	r0, .L3641+4
	bl	printk
	bl	dump_stack
.L3627:
	ldr	r8, .L3641+8
	movw	r10, #698
	ldr	r5, .L3641+12
	ldr	r3, [r8, #2804]
	mov	r9, r8
	ldrh	r2, [r3, r10]
	cmp	r7, r2
	bls	.L3628
	mov	r1, r7
	ldr	r0, .L3641+16
	mov	r4, #0
	bl	printk
	ldr	r0, [r4, #4]
	mov	r1, #255
	ldrh	r2, [r5, #-14]
	bl	ftl_memset
	ldr	r3, [r8, #2804]
	ldrh	r3, [r3, r10]
	cmp	r3, r7
	bcs	.L3639
	ldr	r0, .L3641+4
	mov	r2, #37
	ldr	r1, .L3641
	b	.L3640
.L3628:
	add	r2, r7, #176
	mov	r6, r6, asl #3
	add	r8, r5, r6
	ldr	r4, [r3, r2, asl #2]
	ldr	r3, .L3641+20
	add	r2, r3, r6
	strh	r7, [r3, r6]	@ movhi
	mov	r3, #0
	cmp	r4, r3
	strh	r3, [r2, #2]	@ movhi
	bne	.L3631
	mov	r1, r7
	mov	r2, r4
	ldr	r0, .L3641+24
	bl	printk
	ldr	r0, [r8, #-2520]
	mov	r1, #255
	ldrh	r2, [r5, #-14]
	bl	ftl_memset
	b	.L3639
.L3631:
	ldr	r2, [r5, #-32]
	mov	r0, r4
	ldrb	r3, [r5, #-2538]	@ zero_extendqisi2
	ldr	r1, [r8, #-2520]
	bl	ftl_read_ppa_page
	ldr	r2, [r5, #-32]
	ldr	r2, [r2]
	cmp	r2, r7
	mov	r3, r0
	bne	.L3632
	cmn	r0, #1
	cmpne	r0, #512
	beq	.L3632
.L3636:
	ldr	r3, [r5, #-32]
	ldr	r3, [r3]
	cmp	r3, r7
	beq	.L3639
	ldr	r0, .L3641+4
	mov	r2, #73
	ldr	r1, .L3641
	b	.L3640
.L3632:
	mov	r1, r7
	str	r4, [sp]
	ldr	r0, .L3641+28
	add	r6, r5, r6
	bl	printk
	ldr	r3, [r9, #2804]
	movw	ip, #698
	ldr	r0, .L3641+32
	add	r1, r3, #704
	mov	r2, #4
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldrb	r3, [r5, #-2538]	@ zero_extendqisi2
	ldr	r0, .L3641+36
	mov	r2, #4
	ldr	r1, [r6, #-2520]
	mov	r3, r3, asl #7
	ldr	r8, .L3641+12
	bl	rknand_print_hex
	ldr	r0, .L3641+40
	ldr	r1, [r5, #-32]
	mov	r2, #4
	mov	r3, #16
	bl	rknand_print_hex
	mov	r0, r4
	ldr	r1, [r6, #-2520]
	ldr	r2, [r5, #-32]
	ldrb	r3, [r5, #-2538]	@ zero_extendqisi2
	bl	ftl_read_ppa_page
	cmn	r0, #1
	cmpne	r0, #512
	bne	.L3635
	ldr	r0, [r6, #-2520]
	mov	r1, #255
	ldrh	r2, [r8, #-14]
	bl	ftl_memset
	b	.L3637
.L3635:
	ldr	r3, [r8, #-32]
	ldr	r3, [r3]
	cmp	r3, r7
	beq	.L3636
	b	.L3637
.L3640:
	bl	printk
	bl	dump_stack
.L3639:
	mov	r0, #0
	b	.L3630
.L3637:
	mvn	r0, #0
.L3630:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3642:
	.align	2
.L3641:
	.word	.LANCHOR1+2432
	.word	.LC0
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC237
	.word	.LANCHOR3-2524
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
	ldr	r5, .L3662
	ldr	r4, [r5, #2804]
	add	r4, r4, #688
	ldrh	r2, [r4]
	ldr	r4, .L3662+4
	ldrh	r3, [r4, #-168]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L3644
	ldr	r3, [r4, #-176]
	cmp	r3, #0
	beq	.L3645
.L3644:
	bl	pm_free_sblk
	ldr	r2, [r5, #2804]
	add	r3, r2, #688
	ldrh	r1, [r3]
	ldrh	r3, [r4, #-168]
	sub	r3, r3, #1
	cmp	r1, r3
	bge	.L3646
	ldr	r3, .L3662+4
	ldr	r3, [r3, #-176]
	cmp	r3, #0
	beq	.L3645
.L3646:
	uxth	r0, r0
	movw	r6, #65535
	add	r0, r0, #208
	mov	r3, #0
	str	r3, [r4, #-176]
	mov	r0, r0, asl #1
	ldrh	r7, [r2, r0]
	cmp	r7, r6
	bne	.L3648
	ldr	r1, .L3662+8
	mov	r2, #182
	ldr	r0, .L3662+12
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	r3, [r5, #2804]
	uxth	r0, r0
	add	r0, r0, #208
	mov	r0, r0, asl #1
	ldrh	r7, [r3, r0]
	cmp	r7, r6
	beq	.L3645
.L3648:
	bl	pm_select_ram_region
	ldr	r3, .L3662+16
	movw	r2, #65535
	mov	r6, r0, asl #3
	mov	r10, r0
	add	r8, r3, r6
	str	r3, [sp]
	ldrh	r0, [r3, r6]
	cmp	r0, r2
	beq	.L3649
	ldr	r3, .L3662+4
	add	r3, r3, r6
	ldr	r1, [r3, #-2520]
	cmp	r1, #0
	beq	.L3649
	ldrsh	r3, [r8, #2]
	cmp	r3, #0
	bge	.L3649
	bl	pm_write_page
	ldrh	r3, [r8, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r8, #2]	@ movhi
.L3649:
	ldr	fp, .L3662
	mov	r8, #0
	ldr	r9, .L3662+4
	add	r3, r9, r6
	str	r3, [sp, #4]
.L3650:
	ldr	r0, [r5, #2804]
	uxth	r9, r8
	add	r3, r0, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r3, r9
	bls	.L3661
	ldr	r3, .L3662+20
	ldrh	r1, [r3]
	add	r3, r9, #176
	ldr	ip, [r0, r3, asl #2]
	mvn	r0, #0
	ldrb	r3, [fp, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r1, r3
	mvn	r0, r0, asl r3
	and	r0, r0, ip, lsr r1
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, r7
	bne	.L3651
	mov	r0, r9
	mov	r1, r10
	bl	load_l2p_region
	cmp	r0, #0
	bne	.L3652
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r3, #-2520]
	bl	pm_write_page
.L3652:
	ldr	r2, [sp]
	mvn	r3, #0
	strh	r3, [r2, r6]	@ movhi
.L3651:
	add	r8, r8, #1
	b	.L3650
.L3661:
	bl	pm_free_sblk
.L3645:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3663:
	.align	2
.L3662:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR1+2448
	.word	.LC0
	.word	.LANCHOR3-2524
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
	ldr	r4, .L3670
	ldr	r3, .L3670+4
	add	r2, r4, r0
	add	r5, r3, r0
	ldr	r1, [r2, #-2520]
	ldrh	r0, [r3, r0]
	bl	pm_write_page
	ldrh	r3, [r5, #2]
	ubfx	r3, r3, #0, #15
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #-36]
	cmp	r3, #0
	beq	.L3665
	bl	pm_gc
	mov	r3, #0
	str	r3, [r4, #-36]
.L3665:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3671:
	.align	2
.L3670:
	.word	.LANCHOR3
	.word	.LANCHOR3-2524
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
	ldr	r5, .L3677
.L3674:
	add	r3, r5, r4, asl #3
	uxth	r0, r4
	ldrsh	r3, [r3, #2]
	cmp	r3, #0
	bge	.L3673
	bl	pm_flush_id
.L3673:
	add	r4, r4, #1
	cmp	r4, #32
	bne	.L3674
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L3678:
	.align	2
.L3677:
	.word	.LANCHOR3-2524
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
	ldr	r4, .L3707
	.pad #20
	sub	sp, sp, #20
	mov	r0, #64
	mov	r5, #0
	mov	r3, #1
	ldr	r9, .L3707+4
	str	r5, [r4, #-176]
	mov	r8, r4
	str	r5, [r4, #-36]
	mvn	r10, #0
	strb	r3, [r4, #-28]
	bl	ftl_dma32_malloc
	str	r0, [r4, #-32]
.L3685:
	mov	r3, #0
	cmp	r7, r3
	mov	r6, r9
	strh	r10, [r6, r5]!	@ movhi
	strh	r3, [r6, #2]	@ movhi
	beq	.L3684
	ldrb	r0, [r8, #-2538]	@ zero_extendqisi2
	mov	r0, r0, asl #9
	bl	ftl_dma32_malloc
	str	r0, [r6, #4]
.L3684:
	add	r5, r5, #8
	cmp	r5, #256
	bne	.L3685
	ldr	r5, .L3707+8
	ldr	r10, [r4, #-2520]
	ldr	r6, [r4, #-32]
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
	bgt	.L3686
	add	r3, r3, #692
	ldr	r0, .L3707+12
	mov	fp, r5
	ldrh	r1, [r3]
	mov	r3, r7
	bl	printk
	ldr	r3, [r5, #2804]
	add	r7, r7, #1
	add	r3, r3, #696
	ldrh	r9, [r3]
.L3687:
	sxth	r3, r9
	cmp	r3, r7
	bge	.L3706
	ldr	r2, [r5, #2804]
	ldr	r0, .L3707+16
	add	r1, r2, #692
	ldrh	r8, [r0]
	ldrh	r1, [r1]
	ldrb	r0, [r2, #694]	@ zero_extendqisi2
	mov	r2, r10
	mla	r8, r8, r1, r3
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
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
	ldr	r0, .L3707+20
	bl	printk
	ldr	ip, [sp, #12]
	cmp	ip, #512
	cmnne	ip, #1
	beq	.L3688
	ldr	r3, [fp, #2804]
	ldr	r2, [r6]
	add	r3, r3, #696
	add	r3, r3, #2
	ldrh	r3, [r3]
	cmp	r2, r3
	bcs	.L3688
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L3689
	ldr	r2, .L3707
	mov	r0, r10
	str	r3, [sp, #12]
	ldrb	r1, [r2, #-2538]	@ zero_extendqisi2
	mov	r1, r1, asl #9
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3689
	ldr	r1, [r6, #8]
	ldr	r0, .L3707+24
	bl	printk
	b	.L3688
.L3689:
	ldr	r3, [r6]
	ldr	r2, [r5, #2804]
	add	r3, r3, #176
	str	r8, [r2, r3, asl #2]
.L3688:
	add	r9, r9, #1
	uxth	r9, r9
	b	.L3687
.L3706:
	mov	r3, #1
	strb	r3, [r4, #-47]
	ldr	r3, [r5, #2804]
	add	r3, r3, #696
	strh	r7, [r3]	@ movhi
	bl	pm_free_sblk
.L3686:
	mov	r1, #255
	ldrh	r2, [r4, #-14]
	ldr	r0, [r4, #-2520]
	bl	ftl_memset
	mvn	r0, #0
	ldr	r1, [r4, #-2520]
	bl	pm_write_page
	ldrb	r3, [r4, #-47]	@ zero_extendqisi2
	ldr	r5, .L3707
	cmp	r3, #0
	beq	.L3691
	ldr	r1, [r5, #-2520]
	mvn	r0, #0
	bl	pm_write_page
	ldr	r1, [r5, #-2520]
	mvn	r0, #0
	bl	pm_write_page
	mvn	r0, #0
	ldr	r1, [r5, #-2520]
	bl	pm_write_page
.L3691:
	bl	pm_free_sblk
	bl	pm_gc
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3708:
	.align	2
.L3707:
	.word	.LANCHOR3
	.word	.LANCHOR3-2524
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r1
	ldr	r9, .L3728
	mov	r10, r0
	mov	r8, r2
	ldrb	r4, [r9, #-2538]	@ zero_extendqisi2
	mov	r1, r4, asl #7
	mov	r5, r4, asl #7
	bl	__aeabi_uidiv
	ldr	r1, .L3728+4
	ldr	r2, [r1, #2784]
	cmp	r10, r2
	movcc	r2, #0
	uxth	fp, r0
	str	r1, [sp, #4]
	mov	r7, r0
	ldrcc	r1, .L3728+8
	smulbb	r5, r5, fp
	rsb	r5, r5, r10
	uxth	r5, r5
	bcc	.L3710
	ldr	r0, .L3728+12
	mov	r1, r10
	bl	printk
	cmp	r8, #0
	mvn	r0, #0
	streq	r0, [r6]
	b	.L3711
.L3726:
	ldrh	r0, [r0, r1]
	cmp	r0, fp
	beq	.L3713
.L3712:
	add	r2, r2, #1
	cmp	r2, #32
	beq	.L3725
.L3710:
	mov	r0, r2, asl #3
	uxth	r4, r2
	add	ip, r1, r0
	ldr	r10, .L3728+8
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bne	.L3726
	b	.L3712
.L3725:
	bl	pm_select_ram_region
	mov	r2, r0, asl #3
	mov	r4, r0
	add	r1, r10, r2
	ldrh	ip, [r10, r2]
	movw	r2, #65535
	cmp	ip, r2
	bne	.L3727
.L3716:
	mov	r0, fp
	mov	r1, r4
	strb	r4, [r9, #-12]
	bl	load_l2p_region
.L3713:
	cmp	r8, #0
	mov	r4, r4, asl #3
	bne	.L3714
	add	r2, r9, r4
	ldr	r3, [sp, #4]
	mvn	r1, #0
	ldr	r2, [r2, #-2520]
	ldr	r2, [r2, r5, asl #2]
	str	r2, [r6]
	ldrb	r0, [r3, #1153]	@ zero_extendqisi2
	ldrb	r3, [r9, #-3064]	@ zero_extendqisi2
	rsb	ip, r0, #24
	mvn	r1, r1, asl r0
	and	r2, r1, r2, lsr ip
	cmp	r2, r3
	mvncs	r3, #0
	strcs	r3, [r6]
	b	.L3715
.L3714:
	add	r2, r9, r4
	ldr	r1, [r6]
	ldr	r2, [r2, #-2520]
	str	r1, [r2, r5, asl #2]
	add	r1, r10, r4
	strb	r7, [r9, #-2268]
	ldrh	r2, [r1, #2]
	mvn	r2, r2, asl #17
	mvn	r2, r2, lsr #17
	strh	r2, [r1, #2]	@ movhi
.L3715:
	add	r4, r10, r4
	movw	r2, #32767
	mov	r0, #0
	ldrh	r3, [r4, #2]
	ubfx	r1, r3, #0, #15
	cmp	r1, r2
	addne	r3, r3, #1
	strneh	r3, [r4, #2]	@ movhi
	b	.L3711
.L3727:
	ldrsh	r2, [r1, #2]
	cmp	r2, #0
	bge	.L3716
	bl	pm_flush_id
	b	.L3716
.L3711:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3729:
	.align	2
.L3728:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-2524
	.word	.LC245
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
	ldr	r7, .L3830
	mov	r6, #0
	ldr	r8, .L3830+4
	.pad #68
	sub	sp, sp, #68
	ldr	r5, [r7, #1096]
	strb	r6, [r8, #-3115]
	strb	r6, [r8, #-136]
	ldrh	r4, [r5, #80]
	cmp	r4, r3
	beq	.L3731
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
	beq	.L3732
	ldrb	r2, [r7, #1160]	@ zero_extendqisi2
	cmp	r2, r6
	bne	.L3732
	ldrb	r2, [r7, #1161]	@ zero_extendqisi2
	cmp	r2, r6
	beq	.L3733
.L3732:
	ldr	r2, .L3830+8
	ldrh	r2, [r2, #-10]
	sub	r2, r2, #1
	add	r2, r2, r1
	orr	r3, r2, r3, asl #24
	b	.L3822
.L3733:
	cmp	r3, #3
	bne	.L3734
	sub	r3, r8, #3088
	ldrh	r3, [r3, #-8]
	sub	r3, r3, #1
	add	r3, r3, r1
	orr	r3, r3, #50331648
.L3822:
	str	r3, [r4, #24]
.L3734:
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3735
	ldr	r3, [r4, #12]
	ldr	r6, .L3830+12
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3736
.L3735:
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, [r4, #4]
	ldr	r2, [r4, #12]
	ldr	r0, .L3830+16
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
	b	.L3826
.L3736:
	ldrb	r3, [r8, #-11]	@ zero_extendqisi2
	ldr	r10, .L3830+4
	cmp	r3, #2
	sub	r9, r10, #3104
	bne	.L3738
	ldrb	r3, [r10, #-3124]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L3738
	ldrh	r2, [r9, #-10]
	ldrb	ip, [r10, #-3123]	@ zero_extendqisi2
	ldrh	r3, [r10, #-14]
	mov	r2, r2, asl #2
	ldr	r1, [r10, #-124]
	smulbb	r2, ip, r2
	mov	r0, r3, lsr #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #4]
	rsb	r2, r3, r2
	uxth	r2, r2
	bl	ftl_memcpy
	ldrb	r3, [r7, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3739
	ldr	r3, .L3830
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3740
.L3739:
	ldr	r3, [r4, #24]
	sub	r3, r3, #1
	b	.L3741
.L3740:
	sub	r10, r10, #3088
	ldr	r3, [sp, #56]
	ldrh	r2, [r10, #-8]
	sub	r3, r3, #1
	add	r3, r3, r2
	orr	r3, r3, #33554432
.L3741:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3742
	ldr	r3, [r4, #12]
	ldr	r3, [r3]
	cmp	r3, r6
	beq	.L3743
.L3742:
	mov	r0, r4
	bl	zbuf_free
.L3826:
	ldrh	r4, [r5, #80]
	mov	r6, #0
	ldr	r3, [r7, #1092]
	mov	r4, r4, asl #1
	strh	r6, [r3, r4]	@ movhi
	ldrh	r2, [r5, #80]
	ldr	r3, [r7, #1096]
	strh	r2, [r3, #130]	@ movhi
	b	.L3737
.L3743:
	ldr	r0, [r8, #-124]
	ldr	r1, [r4, #4]
	ldrh	r2, [r8, #-14]
	b	.L3823
.L3738:
	ldrh	r2, [r9, #-10]
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	ldr	r0, [r8, #-124]
	ldr	r1, [r4, #4]
	mul	r2, r3, r2
	mov	r2, r2, asl #2
.L3823:
	bl	ftl_memcpy
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	ldrh	r2, [r9, #-10]
	ldr	r10, .L3830+4
	mul	r2, r3, r2
	ldrb	r3, [r8, #-2538]	@ zero_extendqisi2
	sub	r6, r10, #3104
	cmp	r3, r2, asr #6
	mov	r2, r2, asl #2
	bge	.L3745
	ldr	r0, [r10, #-120]
	mov	r1, #0
	bl	ftl_memset
	ldrb	r9, [r10, #-11]	@ zero_extendqisi2
	cmp	r9, #1
	movne	r9, #1
	movne	r6, #0
	bne	.L3746
	ldrh	r1, [r6, #-10]
	ldrb	r3, [r10, #-3123]	@ zero_extendqisi2
	ldrh	r6, [r10, #-14]
	ldr	r0, [r10, #-120]
	mul	r3, r3, r1
	ldr	r1, [r4, #4]
	sub	r6, r6, r3, asl #2
	add	r1, r1, r3, asl #2
	mov	r2, r6
	bl	ftl_memcpy
	b	.L3746
.L3745:
	ldr	r1, [r4, #4]
	mov	r9, #0
	ldr	r0, [r10, #-120]
	mov	r6, r9
	add	r1, r1, r2
	bl	ftl_memcpy
.L3746:
	ldr	r3, .L3830+4
	sub	r2, r3, #3088
	ldrh	r2, [r2, #-8]
	str	r2, [sp, #36]
	ldrb	r2, [r8, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	str	r2, [sp, #32]
	bne	.L3747
	ldrb	r3, [r3, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3748
.L3747:
	ldrb	r3, [r7, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3749
.L3748:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	mul	r3, r3, r2
	str	r3, [sp, #36]
	mov	r3, #1
	str	r3, [sp, #32]
.L3749:
	ldr	r10, .L3830+4
	mov	r7, #0
	str	r7, [sp, #40]
.L3750:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bcs	.L3763
	add	ip, r5, #96
	add	r3, r7, r7, asl #1
	sub	r3, r3, #1
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #44]
.L3764:
	ldrb	r3, [r5, #89]	@ zero_extendqisi2
	ldr	r2, [sp, #44]
	cmp	r2, r3
	bge	.L3760
	ldr	r3, [sp, #40]
	mov	fp, #1
	mov	r8, r3, asl #2
.L3761:
	ldr	r3, [sp, #32]
	cmp	fp, r3
	bhi	.L3828
	ldr	r3, .L3830+20
	ldrh	r2, [ip]
	ldrb	r1, [r10, #-3124]	@ zero_extendqisi2
	ldrh	r3, [r3]
	cmp	r1, #2
	mul	r3, r3, r2
	str	r3, [sp, #56]
	beq	.L3751
	ldr	r2, .L3830
	ldrb	r2, [r2, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3752
.L3751:
	sub	r2, r7, #1
	add	r3, r2, r3
	add	r3, r3, fp
	orr	r3, r3, r1, asl #24
	b	.L3824
.L3752:
	cmp	r1, #3
	addne	r3, r3, r7
	bne	.L3824
	ldr	r2, .L3830
	ldrb	r2, [r2, #1161]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r7
	ldrne	r2, [sp, #48]
	orreq	r3, r3, fp, asl #24
	addne	r3, r3, r2
	addne	r3, r3, fp
	orrne	r3, r3, #50331648
.L3824:
	str	r3, [r4, #24]
	mov	r0, r4
	mov	r1, #1
	str	ip, [sp, #52]
	bl	sblk_read_page
	cmp	r9, #0
	ldr	ip, [sp, #52]
	beq	.L3756
	ldr	r3, [r10, #-120]
	ldr	r2, [r3, r8]
	cmp	r2, #0
	ldreq	r2, [r4, #12]
	ldreq	r2, [r2, #8]
	streq	r2, [r3, r8]
.L3756:
	ldr	r3, [r10, #-124]
	ldr	r2, [r4, #12]
	ldr	r3, [r3, r8]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L3757
	ldr	r0, [r10, #-120]
	ldr	lr, [r0, r8]
	ldr	r0, [r2, #8]
	cmp	lr, r0
	beq	.L3787
.L3757:
	ldr	r0, [r10, #-120]
	str	ip, [sp, #52]
	ldr	r0, [r0, r8]
	str	r0, [sp]
	ldr	r0, [r2]
	stmib	sp, {r0, r1}
	ldr	r1, [r2, #8]
	ldr	r0, .L3830+24
	str	r1, [sp, #12]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	ldr	r1, [r4, #24]
	ldr	r2, [r4, #36]
	bl	printk
	ldr	r3, [r10, #-124]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	ldr	ip, [sp, #52]
	beq	.L3787
	mov	r0, r4
	bl	zbuf_free
	ldr	r2, .L3830
	ldrh	r3, [r5, #80]
	mov	r0, #0
	ldr	r1, [r2, #1092]
	mov	r3, r3, asl #1
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r5, #80]
	ldr	r3, [r2, #1096]
	strh	r1, [r3, #130]	@ movhi
	b	.L3737
.L3787:
	ldr	r3, [sp, #40]
	add	fp, fp, #1
	add	r8, r8, #4
	add	r3, r3, #1
	str	r3, [sp, #40]
	b	.L3761
.L3828:
	ldr	r3, [sp, #44]
	add	ip, ip, #2
	add	r3, r3, #1
	str	r3, [sp, #44]
	b	.L3764
.L3760:
	ldrb	r3, [r10, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r7, r7, #1
	add	r7, r7, #1
	b	.L3750
.L3763:
	mov	r0, r4
	mov	r6, #0
	bl	zbuf_free
	ldr	ip, .L3830+8
	ldrb	r8, [r5, #89]	@ zero_extendqisi2
	mov	r7, r6
	ldr	r3, .L3830
	add	r10, ip, #3104
	ldrh	r9, [ip, #-10]
	ldrh	r4, [r5, #80]
	ldr	r3, [r3, #1092]
	smulbb	r8, r8, r9
	mov	r4, r4, asl #1
	mov	r9, r6
	strh	r8, [r3, r4]	@ movhi
.L3765:
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bcs	.L3779
	add	r2, r9, r9, asl #1
	sub	r3, r2, #1
	str	r3, [sp, #48]
	add	r3, r5, #96
	str	r3, [sp, #44]
	mov	r3, #0
	str	r3, [sp, #40]
.L3780:
	ldrb	r2, [r5, #89]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r3, r2
	bge	.L3776
	mov	r4, #1
.L3777:
	ldr	r3, [sp, #32]
	cmp	r4, r3
	bhi	.L3829
	ldr	r2, [r10, #-124]
	ldr	r8, [r2, r7, asl #2]
	cmn	r8, #1
	beq	.L3766
	ldr	r2, [r10, #-120]
	mov	r0, r8
	ldr	fp, [r2, r7, asl #2]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #60]
	bne	.L3767
	mov	r0, r8
	add	r1, sp, #60
	mov	r2, #0
	bl	pm_log2phys
.L3767:
	ldr	r3, [sp, #44]
	ldrh	r2, [r3]
	ldr	r3, .L3830+20
	ldrh	r1, [r3]
	ldr	r3, .L3830+4
	mul	r2, r1, r2
	ldrb	r1, [r3, #-3124]	@ zero_extendqisi2
	cmp	r1, #2
	beq	.L3768
	ldr	r0, .L3830
	ldrb	lr, [r0, #1160]	@ zero_extendqisi2
	cmp	lr, #0
	beq	.L3769
.L3768:
	sub	r0, r9, #1
	add	r2, r0, r2
	add	r2, r2, r4
	orr	r2, r2, r1, asl #24
	b	.L3825
.L3769:
	cmp	r1, #3
	addne	r2, r2, r9
	bne	.L3825
	ldrb	r1, [r0, #1161]	@ zero_extendqisi2
	cmp	r1, #0
	mov	r1, r4, asl #24
	addeq	r2, r2, r9
	ldrne	r3, [sp, #48]
	addne	r2, r2, r3
	addne	r2, r2, r4
	orr	r2, r1, r2
.L3825:
	str	r2, [sp, #56]
	ldr	r2, .L3830+28
	ldrh	r0, [r2]
	ldr	r2, .L3830
	ldrb	r1, [r2, #1153]	@ zero_extendqisi2
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
	bne	.L3773
	mov	r0, r8
	add	r1, sp, #56
	bl	pm_log2phys
	mov	r0, fp
	add	r6, r6, #1
	bl	ftl_vpn_decrement
	b	.L3774
.L3773:
	ldr	r2, [sp, #56]
	cmp	r1, r2
	addeq	r6, r6, #1
.L3774:
	ldr	r1, .L3830
	add	r7, r7, #1
	ldr	r2, [r1, #1084]
	add	r2, r2, fp, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	ands	r2, r2, #224
	bne	.L3766
	ldr	r8, [r1, #1092]
	mov	fp, fp, asl #1
	ldrh	r1, [r8, fp]
	cmp	r1, #0
	strneh	r2, [r8, fp]	@ movhi
.L3766:
	add	r4, r4, #1
	b	.L3777
.L3829:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	ldr	r3, [sp, #44]
	add	r3, r3, #2
	str	r3, [sp, #44]
	b	.L3780
.L3776:
	ldrb	r2, [r10, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	addne	r9, r9, #1
	add	r9, r9, #1
	b	.L3765
.L3779:
	ldr	r2, .L3830
	ldrh	r3, [r5, #80]
	ldr	r2, [r2, #1092]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r5, #80]
	bl	zftl_insert_data_list
.L3737:
	ldr	r4, .L3830
	ldrh	r0, [r5, #80]
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3781
	bl	ftl_dump_write_open_sblk
.L3781:
	mov	r2, r6
	ldrh	r1, [r5, #80]
	ldr	r0, .L3830+32
	bl	printk
	mvn	r3, #0
	strh	r3, [r5, #80]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r3, [r4, #1096]
	movw	r2, #65535
	ldrh	r3, [r3, #130]
	cmp	r3, r2
	beq	.L3782
	ldr	r1, .L3830
	movw	r2, #1080
	ldrh	r2, [r1, r2]
	cmp	r2, r3
	bhi	.L3783
	ldr	r1, .L3830+36
	movw	r2, #517
	ldr	r0, .L3830+40
	bl	printk
	bl	dump_stack
.L3783:
	ldr	r3, [r4, #1096]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
.L3782:
	ldr	r2, [r4, #2804]
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, #126]	@ movhi
	ldr	r2, [r4, #1096]
	strh	r3, [r2, #130]	@ movhi
	bl	ftl_info_flush
	b	.L3730
.L3731:
	ldrh	r3, [r5, #130]
	cmp	r3, r4
	beq	.L3730
	ldr	r2, [r7, #2804]
	ldrh	r2, [r2, #126]
	cmp	r2, r3
	bne	.L3785
	bl	pm_flush
	ldr	r3, [r7, #1096]
	ldrh	r0, [r3, #130]
	bl	ftl_free_sblk
	ldr	r3, [r7, #2804]
	mvn	r2, #0
	mov	r0, r6
	strh	r2, [r3, #126]	@ movhi
	bl	ftl_info_flush
.L3785:
	ldr	r3, [r7, #1096]
	mvn	r2, #0
	strh	r2, [r3, #130]	@ movhi
.L3730:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3831:
	.align	2
.L3830:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-3104
	.word	-178307901
	.word	.LC246
	.word	.LANCHOR3-3066
	.word	.LC247
	.word	.LANCHOR3-3132
	.word	.LC248
	.word	.LANCHOR1+2456
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
	ldr	r5, .L3868
	ldr	r3, .L3868+4
	ldr	r9, .L3868+8
	ldr	r6, [r5, #1096]
	ldrh	r3, [r3, #-10]
	ldrb	r2, [r6, #89]	@ zero_extendqisi2
	mul	r3, r2, r3
	str	r3, [sp]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3833
	ldr	r0, .L3868+12
	ldrh	r1, [r6, #80]
	bl	printk
.L3833:
	ldrh	r3, [r6, #80]
	mov	r8, #0
	ldr	r4, .L3868+16
	mov	r7, r8
	ldr	r1, [r5, #1092]
	mov	r3, r3, asl #1
	ldr	r0, [sp]
	ldrb	r2, [r4, #-11]	@ zero_extendqisi2
	rsb	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
.L3834:
	ldr	r3, [sp]
	cmp	r7, r3
	beq	.L3867
	ldr	r3, [r4, #-124]
	mov	r10, r7, asl #2
	ldr	fp, [r3, r7, asl #2]
	cmn	fp, #1
	beq	.L3835
	ldrb	r1, [r4, #-2538]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r3, r0
	str	r3, [sp, #12]
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3836
	ldr	r0, .L3868+20
	mov	r2, fp
	ldr	r1, [sp, #12]
	mov	r3, r7
	bl	printk
.L3836:
	str	r7, [sp, #8]
.L3842:
	ldr	r3, [r4, #-124]
	ldr	fp, [r3, r10]
	cmn	fp, #1
	beq	.L3837
	ldrb	r1, [r4, #-2538]	@ zero_extendqisi2
	mov	r0, fp
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	uxth	r0, r0
	cmp	r0, r3
	bne	.L3837
	ldr	r3, [r4, #-120]
	mov	r0, fp
	ldr	r3, [r3, r10]
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3838
	mov	r0, fp
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3838:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L3839
	ldr	r3, .L3868+16
	add	r1, sp, #24
	mov	r0, fp
	mov	r2, #1
	add	r8, r8, #1
	ldr	r3, [r3, #-116]
	ldr	r3, [r3, r10]
	str	r3, [r1, #-8]!
	bl	pm_log2phys
	ldr	r3, .L3868+24
	ldrh	r0, [r3]
	ldrb	r3, [r5, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	ldr	r2, [sp, #4]
	mov	fp, r2, lsr r0
	mvn	r0, #0
	bic	r0, fp, r0, asl r3
	ldr	r3, .L3868+16
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	uxth	r0, r0
	b	.L3866
.L3839:
	ldr	r2, [r9]
	tst	r2, #256
	beq	.L3841
	ldr	r0, .L3868+28
	mov	r1, fp
	ldr	r2, [sp, #4]
	bl	printk
.L3841:
	ldrh	r0, [r6, #80]
.L3866:
	bl	ftl_vpn_decrement
	ldr	r3, [r4, #-124]
	mvn	r2, #0
	str	r2, [r3, r10]
.L3837:
	ldr	r3, [sp, #8]
	add	r10, r10, #4
	ldr	r2, [sp]
	add	r3, r3, #1
	str	r3, [sp, #8]
	cmp	r3, r2
	bne	.L3842
.L3835:
	add	r7, r7, #1
	b	.L3834
.L3867:
	ldr	r3, [r9]
	tst	r3, #256
	beq	.L3844
	ldrh	r3, [r6, #80]
	ldr	r1, [r5, #1096]
	ldr	r2, [r5, #1092]
	mov	r3, r3, asl #1
	ldr	r0, .L3868+32
	ldrh	r1, [r1, #80]
	ldrh	r2, [r2, r3]
	mov	r3, r8
	bl	printk
.L3844:
	ldrh	r3, [r6, #80]
	ldr	r2, [r5, #1092]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, r8
	beq	.L3845
	ldr	r1, .L3868+36
	movw	r2, #898
	ldr	r0, .L3868+40
	bl	printk
	bl	dump_stack
.L3845:
	ldrh	r3, [r6, #80]
	ldr	r2, [r5, #1092]
	mov	r3, r3, asl #1
	strh	r8, [r2, r3]	@ movhi
	ldrh	r0, [r6, #80]
	bl	zftl_insert_data_list
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3869:
	.align	2
.L3868:
	.word	.LANCHOR0
	.word	.LANCHOR3-3104
	.word	.LANCHOR2
	.word	.LC249
	.word	.LANCHOR3
	.word	.LC250
	.word	.LANCHOR3-3132
	.word	.LC251
	.word	.LC252
	.word	.LANCHOR1+2468
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
	ldr	r6, .L3899
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3899+4
	mov	lr, r1
	ldr	r5, .L3899+8
	movw	r0, #65535
	ldrb	r3, [r6, #2832]	@ zero_extendqisi2
	ldrb	ip, [r5, #-3123]	@ zero_extendqisi2
	add	r2, r4, r3, asl #1
	ldrh	r7, [r2, #36]
	ldrh	r2, [r4, #2]
.L3871:
	cmp	r7, r0
	bne	.L3897
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
	b	.L3871
.L3897:
	mov	r0, #1
	cmp	lr, #0
	strneh	r2, [r4, #2]	@ movhi
	cmp	r1, #0
	strneb	r3, [r6, #2832]
	bl	buf_alloc
	ldr	r8, .L3899
	mov	r9, #1
	ldr	r10, .L3899+12
	mov	fp, r0
.L3876:
	ldrb	r3, [r6, #2834]	@ zero_extendqisi2
	ldr	r2, .L3899
	cmp	r9, r3
	bgt	.L3898
	ldrh	r2, [r10]
	cmp	r3, #2
	mul	r2, r2, r7
	bne	.L3877
	ldrh	r3, [r4, #2]
	add	r3, r2, r3, asl #1
	ldr	r2, .L3899+8
	sub	r3, r3, #1
	add	r3, r3, r9
	ldrb	r2, [r2, #-3124]	@ zero_extendqisi2
	b	.L3895
.L3877:
	cmp	r3, #3
	ldrneh	r1, [r4, #2]
	addne	r2, r2, r1
	bne	.L3896
	ldrb	r3, [r8, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3880
	ldrb	r3, [r8, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3881
.L3880:
	ldrh	r3, [r4, #2]
	add	r3, r3, r3, asl #1
	sub	r3, r3, #1
	add	r3, r3, r2
	ldrb	r2, [r5, #-3124]	@ zero_extendqisi2
	add	r3, r3, r9
.L3895:
	orr	r3, r3, r2, asl #24
	str	r3, [fp, #24]
	b	.L3878
.L3881:
	ldrh	r1, [r4, #2]
	add	r2, r2, r1
	orr	r2, r2, r9, asl #24
.L3896:
	str	r2, [fp, #24]
.L3878:
	mov	r0, fp
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [fp, #36]
	cmp	r3, #512
	cmnne	r3, #1
	beq	.L3883
	ldr	r3, [fp, #12]
	ldr	r3, [r3, #4]
	mov	r0, r3
	str	r3, [sp, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #12]
	ldr	r3, [sp, #4]
	bne	.L3884
	ldr	r2, [r6, #2784]
	cmp	r3, r2
	bcs	.L3884
	mov	r0, r3
	add	r1, sp, #12
	mov	r2, #0
	bl	pm_log2phys
.L3884:
	ldr	r3, [fp, #24]
	ldr	r2, [sp, #12]
	cmp	r3, r2
	ldreqh	r1, [r4, #20]
	ldreq	r2, [r5, #-3128]
	streq	r3, [r2, r1, asl #2]
	ldreqh	r3, [r4, #20]
	addeq	r3, r3, #1
	streqh	r3, [r4, #20]	@ movhi
.L3883:
	ldrh	r3, [r4, #22]
	add	r9, r9, #1
	add	r3, r3, #1
	strh	r3, [r4, #22]	@ movhi
	b	.L3876
.L3898:
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
.L3900:
	.align	2
.L3899:
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
	ldr	r4, .L3953
	.pad #28
	sub	sp, sp, #28
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3902
	ldr	r1, .L3953+4
	movw	r2, #1505
	ldr	r0, .L3953+8
	bl	printk
	bl	dump_stack
.L3902:
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	moveq	r0, #0
	beq	.L3903
	ldr	r3, .L3953+12
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3904
	ldr	r3, .L3953
	ldr	r0, .L3953+16
	ldr	r2, [r3, #1092]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	bl	printk
.L3904:
	ldr	r5, .L3953
	movw	r7, #2828
	bl	timer_get_time
	ldrh	r0, [r4, r7]
	add	r1, r5, #2864
	ldr	r6, .L3953+20
	bl	ftl_get_blk_list_in_sblk
	uxtb	r0, r0
	strb	r0, [r4, #2833]
	cmp	r0, #0
	mvneq	r3, #0
	streqh	r3, [r5, r7]	@ movhi
	beq	.L3903
	ldrh	r3, [r5, r7]
	ldr	r2, [r5, #1084]
	add	r2, r2, r3, asl #2
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r1, r2, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r1, #224
	orreq	r2, r2, #1
	cmp	r2, #0
	bne	.L3906
	ldr	r2, [r5, #1096]
	ldrh	ip, [r2, #16]
	cmp	ip, r3
	beq	.L3906
	ldrh	ip, [r2, #48]
	cmp	ip, r3
	beq	.L3906
	ldrh	r2, [r2, #80]
	cmp	r2, r3
	bne	.L3907
.L3906:
	cmp	r1, #0
	bne	.L3908
	ldr	r2, [r4, #1092]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3909
	ldr	r1, .L3953+4
	movw	r2, #1530
	ldr	r0, .L3953+8
	bl	printk
	bl	dump_stack
.L3909:
	movw	r3, #2828
	ldr	r2, [r4, #1092]
	ldrh	r3, [r4, r3]
	mov	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L3908:
	movw	r3, #2828
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r4, r3]	@ movhi
	strh	r0, [r6, #20]	@ movhi
	b	.L3903
.L3907:
	add	r0, r6, r0, asl #1
	movw	r3, #65535
	ldrh	r10, [r0, #34]
	cmp	r10, r3
	bne	.L3910
	ldr	r1, .L3953+4
	movw	r2, #1540
	ldr	r0, .L3953+8
	bl	printk
	bl	dump_stack
.L3910:
	movw	r3, #2828
	ldr	r5, .L3953+24
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #1084]
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
	bne	.L3911
	ldrb	r2, [r5, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r3, r2
	orr	r7, r7, r2, asl #24
	subeq	r2, r5, #3104
	ldreqh	r8, [r2, #-10]
	ldrb	r2, [r4, #1160]	@ zero_extendqisi2
	subeq	r8, r8, #1
	uxtheq	r8, r8
	cmp	r2, #0
	bne	.L3913
	ldr	r2, .L3953
	ldrb	r2, [r2, #1161]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3911
.L3913:
	ldr	r2, .L3953+28
	ldrh	r8, [r2, #-10]
	sub	r8, r8, #1
	uxth	r8, r8
.L3911:
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
	beq	.L3950
	ldr	r0, [fp, #12]
	ldr	r2, .L3953+32
	ldr	r1, [r0]
	str	r2, [sp, #8]
	cmp	r1, r2
	beq	.L3915
	ldr	r0, .L3953+8
	movw	r2, #1578
	ldr	r1, .L3953+4
.L3951:
	bl	printk
	bl	dump_stack
.L3950:
	mov	r0, fp
	bl	zbuf_free
	b	.L3948
.L3915:
	ldrb	r8, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r1, [r9, #-8]
	ldr	r2, .L3953+24
	smulbb	r1, r1, r8
	smulbb	r1, r1, r3
	uxth	r1, r1
	str	r1, [sp]
	ldrb	r1, [r5, #-11]	@ zero_extendqisi2
	cmp	r3, #3
	cmpeq	r1, #2
	bne	.L3916
	sub	r3, r2, #3104
	ldrh	r0, [r2, #-14]
	ldr	r1, [fp, #4]
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
	beq	.L3917
	ldr	r0, [fp, #4]
	mov	r1, r8
	str	r3, [sp, #12]
	bl	js_hash
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L3917
	mov	r0, fp
	bl	zbuf_free
	ldr	r3, [fp, #12]
	ldr	r0, .L3953+36
	ldr	r2, [fp, #24]
	ldr	r1, [r3, #4]
	mov	r3, r8
	b	.L3949
.L3917:
	ldrb	r3, [r4, #1160]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3918
	ldr	r3, .L3953
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3919
.L3918:
	sub	r7, r7, #1
	b	.L3920
.L3919:
	ldr	r3, [sp, #4]
	ldrh	r7, [r9, #-8]
	ldrh	r3, [r3, #-10]
	sub	r7, r7, #1
	uxth	r7, r7
	mul	r10, r3, r10
	orr	r7, r7, #33554432
	orr	r7, r7, r10
.L3920:
	str	r7, [fp, #24]
	mov	r0, fp
	mov	r1, #1
	bl	sblk_read_page
	ldr	r3, [fp, #36]
	cmn	r3, #1
	cmpne	r3, #512
	beq	.L3950
	ldr	r3, [fp, #12]
	ldr	r2, [sp, #8]
	ldr	r3, [r3]
	cmp	r3, r2
	ldreq	r0, [r5, #-3128]
	ldreq	r1, [fp, #4]
	ldreqh	r2, [r5, #-14]
	beq	.L3947
	ldr	r0, .L3953+8
	movw	r2, #1619
	ldr	r1, .L3953+4
	b	.L3951
.L3916:
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
	beq	.L3947
	mov	r0, fp
	bl	zbuf_free
	ldr	r3, [fp, #12]
	ldr	r0, .L3953+36
	ldr	r2, [fp, #24]
	ldr	r1, [r3, #4]
	mov	r3, r7
.L3949:
	bl	printk
.L3948:
	mvn	r0, #0
	b	.L3903
.L3947:
	bl	ftl_memcpy
	ldr	r7, [r5, #-3128]
	ldr	r10, .L3953+24
	mov	r8, #0
	ldr	r9, .L3953+20
	sub	r7, r7, #4
.L3925:
	ldr	r3, [sp]
	cmp	r8, r3
	bge	.L3952
	ldr	r0, [r7, #4]!
	cmn	r0, #1
	beq	.L3927
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3928
	ldr	r0, [r7]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3928:
	ldr	r3, .L3953+40
	mvn	r1, #0
	ldr	r2, [sp, #20]
	ldrh	r0, [r3]
	ldrb	r3, [r4, #1153]	@ zero_extendqisi2
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
.L3927:
	bl	timer_get_time
	add	r8, r8, #1
	b	.L3925
.L3952:
	mov	r0, fp
	bl	zbuf_free
	movw	r3, #2828
	ldrh	r1, [r4, r3]
	ldr	r2, [r4, #1092]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r6, #20]
	cmp	r2, r3
	beq	.L3931
	ldr	r0, .L3953+44
	bl	printk
.L3931:
	movw	r3, #2828
	ldrh	r1, [r6, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1092]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldrh	r0, [r6, #20]
	strh	r3, [r6, #24]	@ movhi
.L3903:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3954:
	.align	2
.L3953:
	.word	.LANCHOR0
	.word	.LANCHOR1+2492
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
	ldr	r4, .L3972
	ldr	r3, [r4, #1096]
	ldr	r2, [r3, #544]
	cmn	r2, #1
	beq	.L3956
	mov	r5, #0
	mov	r7, r4
.L3957:
	ldr	r3, [r4, #1096]
	add	r1, sp, #4
	mov	r2, #0
	uxth	r6, r5
	ldr	r0, [r3, #544]
	bl	pm_log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3958
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #4]
	mov	r8, r0
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmp	r3, #256
	bne	.L3959
	ldr	r3, .L3972+4
	mov	r9, #1
	ldr	r0, [sp, #4]
	ldrh	r2, [r3]
	ldrb	r3, [r7, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r3, r9, asl r3
	sub	r3, r3, #1
	and	r0, r3, r0, lsr r2
	ldr	r3, .L3972+8
	ldrb	r1, [r3, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r1, r9
	mov	r2, #0
	uxth	r0, r0
	bl	gc_add_sblk
.L3959:
	ldr	r2, [r8, #12]
	ldr	r3, [r4, #1096]
	ldr	r2, [r2, #4]
	ldr	r3, [r3, #544]
	cmp	r2, r3
	beq	.L3960
	ldr	r1, .L3972+12
	movw	r2, #2163
	ldr	r0, .L3972+16
	bl	printk
	bl	dump_stack
.L3960:
	mov	r0, r8
	bl	zbuf_free
.L3958:
	ldr	r3, [r4, #1096]
	ldr	r1, [r4, #2784]
	ldr	r2, [r3, #544]
	add	r2, r2, #1
	str	r2, [r3, #544]
	cmp	r2, r1
	bcc	.L3961
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
	b	.L3955
.L3961:
	ldr	r3, [sp, #4]
	cmn	r3, #1
	bne	.L3955
	ldr	r3, .L3972+8
	add	r5, r5, #1
	ldrh	r3, [r3, #-14]
	cmp	r6, r3, lsr #2
	bcc	.L3957
	b	.L3955
.L3956:
	ldr	r2, [r3, #536]
	ldr	r1, [r3, #12]
	add	r2, r2, #12910592
	add	r2, r2, #49408
	cmp	r1, r2
	bhi	.L3966
	ldr	r0, [r4, #2804]
	ldr	r2, [r3, #540]
	ldr	r0, [r0, #44]
	add	r2, r2, #4992
	add	r2, r2, #8
	cmp	r0, r2
	bls	.L3955
.L3966:
	ldr	r2, [r4, #2804]
	ldr	r2, [r2, #44]
	str	r1, [r3, #536]
	str	r2, [r3, #540]
	mov	r2, #0
	str	r2, [r3, #544]
.L3955:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3973:
	.align	2
.L3972:
	.word	.LANCHOR0
	.word	.LANCHOR3-3132
	.word	.LANCHOR3
	.word	.LANCHOR1+2508
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
	movw	r4, #1080
	ldr	r5, .L4012
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r5, #1096]
	ldrh	r3, [r5, r4]
	ldr	r2, [r2, #608]
	cmp	r2, r3
	bcs	.L3974
	bl	timer_get_time
	ldr	r6, [r5, #1096]
	ldr	r3, [r6, #604]
	add	r3, r3, #29952
	add	r3, r3, #48
	cmp	r0, r3
	bls	.L3974
	bl	timer_get_time
	ldr	r3, [r5, #1096]
	ldrh	r1, [r5, r4]
	str	r0, [r6, #604]
	ldr	r2, [r3, #600]
	cmp	r2, r1
	bcs	.L3978
	ldr	r1, [r5, #2804]
	ldrh	r1, [r1, #134]
	cmp	r2, r1
	bcs	.L3979
.L3978:
	ldr	r2, [r5, #2804]
	ldrh	r2, [r2, #134]
	str	r2, [r3, #600]
.L3979:
	ldr	r4, [r3, #600]
	movw	r3, #65535
	uxth	r4, r4
	cmp	r4, r3
	bne	.L3980
	ldr	r1, .L4012+4
	movw	r2, #2504
	ldr	r0, .L4012+8
	bl	printk
	bl	dump_stack
.L3980:
	ldr	r3, [r5, #1096]
	mov	r0, r4
	add	r1, sp, #24
	ldr	r6, .L4012
	ldr	r2, [r3, #600]
	add	r2, r2, #1
	str	r2, [r3, #600]
	ldr	r2, [r3, #608]
	add	r2, r2, #1
	str	r2, [r3, #608]
	bl	ftl_get_blk_list_in_sblk
	uxth	r0, r0
	cmp	r0, #0
	beq	.L3974
	ldr	r9, [r6, #1084]
	add	r9, r9, r4, asl #2
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r2, r3, #224
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, #224
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L3981
	ldr	r3, [r6, #1096]
	ldrh	r1, [r3, #16]
	cmp	r1, r4
	beq	.L3981
	ldrh	r1, [r3, #48]
	cmp	r1, r4
	beq	.L3981
	ldrh	r3, [r3, #80]
	cmp	r3, r4
	bne	.L3982
.L3981:
	cmp	r2, #0
	bne	.L3974
	ldr	r3, [r5, #1092]
	mov	r4, r4, asl #1
	ldrh	r3, [r3, r4]
	cmp	r3, #0
	beq	.L3983
	ldr	r1, .L4012+4
	movw	r2, #2521
	ldr	r0, .L4012+8
	bl	printk
	bl	dump_stack
.L3983:
	ldr	r3, [r5, #1092]
	mov	r2, #0
	strh	r2, [r3, r4]	@ movhi
	b	.L3974
.L3982:
	add	r3, sp, #40
	add	r0, r3, r0, asl #1
	movw	r3, #65535
	ldrh	r7, [r0, #-18]
	cmp	r7, r3
	bne	.L3985
	ldr	r1, .L4012+4
	movw	r2, #2529
	ldr	r0, .L4012+8
	bl	printk
	bl	dump_stack
.L3985:
	ldr	r6, .L4012+12
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
	bne	.L3986
	ldrb	r2, [r6, #-3124]	@ zero_extendqisi2
	cmp	r2, #2
	uxth	r10, r2
	orr	r7, r7, r2, asl #24
	ldreqh	r8, [r3, #-10]
	subeq	r8, r8, #1
	uxtheq	r8, r8
.L3986:
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
	beq	.L4010
	ldr	r3, [r8, #12]
	ldr	r1, [r3]
	ldr	r3, .L4012+16
	cmp	r1, r3
	beq	.L3988
	mov	r0, r4
	mov	r1, #1
.L4010:
	bl	gc_add_sblk
	mov	r0, r8
	bl	zbuf_free
	b	.L3974
.L3988:
	ldr	r3, .L4012+12
	mov	r7, r2
	ldrh	fp, [fp, #-8]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	smulbb	fp, r3, fp
	smulbb	fp, fp, r10
	ldr	r10, [r8, #4]
	uxth	r3, fp
	mov	fp, r2
	str	r3, [sp, #12]
.L3989:
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bge	.L4011
	ldr	r0, [r10, fp, asl #2]
	cmn	r0, #1
	beq	.L3990
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #20]
	bne	.L3991
	ldr	r0, [r10, fp, asl #2]
	add	r1, sp, #20
	mov	r2, #0
	bl	pm_log2phys
.L3991:
	ldr	r3, .L4012+20
	ldrb	r2, [r5, #1153]	@ zero_extendqisi2
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
.L3990:
	add	fp, fp, #1
	b	.L3989
.L4011:
	mov	r0, r8
	mov	r6, r4, asl #1
	bl	zbuf_free
	ldr	r3, .L4012+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L3993
	ldr	r3, [r5, #1092]
	mov	r1, r4
	ldr	r0, .L4012+28
	ldrh	r2, [r3, r6]
	ldrb	r3, [r9, #2]	@ zero_extendqisi2
	mov	r3, r3, lsr #5
	str	r3, [sp]
	mov	r3, r7
	bl	printk
.L3993:
	ldr	r3, [r5, #1092]
	cmp	r7, #31
	strh	r7, [r3, r6]	@ movhi
	bhi	.L3974
	mov	r0, r4
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L3974:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4013:
	.align	2
.L4012:
	.word	.LANCHOR0
	.word	.LANCHOR1+2528
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
	ldr	r4, .L4052
	.pad #132
	sub	sp, sp, #132
	mov	r7, r0
	str	r1, [sp, #60]
	ldr	r2, [r4, #1084]
	add	r3, r2, fp
	ldrh	r1, [r2, fp]
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	ubfx	r1, r1, #0, #11
	str	r1, [sp]
	ldr	r10, [r2, r0, asl #2]
	mov	r1, r7
	mov	r2, r3, lsr #5
	ldr	r0, .L4052+4
	ubfx	r3, r3, #3, #2
	ubfx	r10, r10, #11, #8
	str	r10, [sp, #4]
	bl	printk
	movw	r3, #65535
	cmp	r7, r3
	beq	.L4037
	movw	r3, #1080
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bls	.L4037
	ldr	r3, [r4, #1084]
	add	r1, sp, #112
	mov	r0, r7
	strh	r7, [sp, #96]	@ movhi
	add	r3, r3, fp
	mov	r5, #0
	mov	r9, r5
	ldrb	r3, [r3, #2]	@ zero_extendqisi2
	and	r3, r3, #224
	cmp	r3, #160
	ldreq	r3, .L4052+8
	movne	r10, #1
	ldreqb	r10, [r3, #-3124]	@ zero_extendqisi2
	bl	ftl_get_blk_list_in_sblk
	ldr	r3, .L4052+12
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
	ldr	r3, [r4, #1084]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	mov	r3, r10
	str	r0, [sp]
	ldr	r0, .L4052+16
	mov	r2, r2, lsr #5
	bl	printk
	mov	r0, #1
	bl	buf_alloc
	mov	r6, r0
	str	r5, [sp, #44]
	str	r5, [sp, #52]
.L4017:
	ldr	r3, [sp, #64]
	uxth	r2, r5
	str	r2, [sp, #48]
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L4034
	mov	r3, r2, asl #1
	add	r2, r3, r2
	sub	r3, r3, #1
	str	r3, [sp, #72]
	mov	r3, #0
	sub	r2, r2, #1
	str	r2, [sp, #68]
.L4050:
	str	r3, [sp, #56]
	ldrb	r2, [sp, #105]	@ zero_extendqisi2
	ldrh	r3, [sp, #56]
	cmp	r2, r3
	bls	.L4032
	mov	r8, #1
	add	r3, r3, #8
	str	r3, [sp, #76]
.L4033:
	cmp	r8, r10
	bhi	.L4051
	ldr	r2, [sp, #76]
	add	r3, sp, #128
	add	r3, r3, r2, asl #1
	ldrh	r2, [r3, #-32]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L4018
	ldr	r3, .L4052+20
	cmp	r10, #3
	ldrh	r3, [r3]
	mul	r3, r3, r2
	bne	.L4019
	ldr	r1, .L4052
	ldrb	r0, [r1, #1160]	@ zero_extendqisi2
	cmp	r0, #0
	ldrne	r1, [sp, #68]
	bne	.L4048
	ldrb	r1, [r1, #1161]	@ zero_extendqisi2
	mov	r4, r8, asl #24
	cmp	r1, #0
	uxtaheq	r3, r3, r5
	ldrne	r1, [sp, #68]
	addne	r3, r3, r1
	addne	r3, r3, r8
	orr	r4, r4, r3
	b	.L4021
.L4019:
	cmp	r10, #2
	uxtahne	r4, r3, r5
	bne	.L4021
	ldr	r1, [sp, #72]
.L4048:
	add	r3, r3, r1
	ldr	r1, .L4052+8
	add	r3, r3, r8
	ldrb	r4, [r1, #-3124]	@ zero_extendqisi2
	orr	r4, r3, r4, asl #24
.L4021:
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
	ldr	r0, .L4052+24
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
	ldr	r0, .L4052+28
	ldr	ip, [sp, #80]
	ldr	r3, [sp, #52]
	cmn	ip, #1
	cmpne	ip, #512
	moveq	r3, #1
	str	r3, [sp, #52]
	ldr	r3, .L4052+32
	ldr	r3, [r3, #4]
	blx	r3
	ldr	r3, .L4052
	ldr	r3, [r3, #1084]
	add	r3, r3, fp
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	and	r2, r2, #224
	cmp	r2, #224
	cmpne	r2, #32
	moveq	r2, #1
	movne	r2, #0
	beq	.L4018
	ldr	r3, [r6, #12]
	str	r2, [sp, #80]
	ldr	r0, [r3, #4]
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #92]
	ldr	r2, [sp, #80]
	bne	.L4025
	ldr	r3, [r6, #12]
	add	r1, sp, #92
	ldr	r0, [r3, #4]
	bl	pm_log2phys
.L4025:
	ldr	r3, [sp, #92]
	cmp	r3, r4
	bne	.L4026
	ldr	r3, [sp, #44]
	mov	r1, r4
	ldr	r0, .L4052+36
	add	r3, r3, #1
	str	r3, [sp, #44]
	ldr	r3, [r6, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #44]
	bl	printk
.L4026:
	ldr	r3, [sp, #60]
	cmp	r3, #0
	beq	.L4028
	ldr	r2, [r3, r9, asl #2]
	mov	r4, r9, asl #2
	ldr	r3, [r6, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r2
	beq	.L4029
	ldr	r3, .L4052+40
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4029
	ldr	r0, .L4052+44
	mov	r1, r9
	bl	printk
.L4029:
	ldr	r3, [sp, #60]
	ldr	r2, [r6, #12]
	ldr	r3, [r3, r4]
	ldr	r2, [r2, #4]
	cmp	r2, r3
	cmnne	r3, #1
	beq	.L4028
	ldr	r1, .L4052+48
	movw	r2, #1575
	ldr	r0, .L4052+52
	bl	printk
	bl	dump_stack
.L4028:
	add	r9, r9, #1
.L4018:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L4033
.L4051:
	ldr	r3, [sp, #56]
	add	r3, r3, #1
	b	.L4050
.L4032:
	add	r5, r5, #1
	b	.L4017
.L4034:
	mov	r0, r6
	bl	zbuf_free
	ldr	r3, .L4052
	ldr	r0, .L4052+56
	mov	r1, r7
	ldr	r2, [r3, #1092]
	mov	r3, r7, asl #1
	ldrh	r2, [r2, r3]
	ldr	r3, [sp, #44]
	bl	printk
	ldr	r0, [sp, #52]
	b	.L4015
.L4037:
	mov	r0, #0
.L4015:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4053:
	.align	2
.L4052:
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
	.word	.LANCHOR1+2548
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
	mov	r7, r3
	ldr	r3, .L4121
	.pad #76
	sub	sp, sp, #76
	mov	r8, r0
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4055
	ldr	r0, .L4121+4
	mov	r1, r8
	mov	r2, r4
	mov	r3, r6
	bl	printk
.L4055:
	cmp	r8, #0
	ldreq	r3, .L4121+8
	moveq	ip, #24576
	ldreq	r8, [r3, #1032]
	beq	.L4057
.L4056:
	cmp	r8, #3
	bhi	.L4086
	mov	ip, r8, asl #13
	mov	r8, #8192
.L4057:
	cmp	r6, r8
	cmpls	r4, r8
	movcs	r10, #1
	movcc	r10, #0
	bcs	.L4086
	add	r3, r4, r6
	cmp	r3, r8
	bhi	.L4086
	add	r3, ip, r4
	str	r3, [sp, #28]
	ldr	r3, .L4121+8
	ldr	r4, .L4121+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #2804]
	ldrb	r5, [r4, #-2538]	@ zero_extendqisi2
	ldr	r3, [r2, #24]
	mov	r1, r5
	add	r3, r3, r6
	str	r3, [r2, #24]
	bl	__aeabi_uidiv
	mov	r1, r5
	ldr	r3, [sp, #28]
	str	r0, [sp, #36]
	add	r3, r3, r6
	str	r3, [sp, #44]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	ldr	r5, [sp, #36]
	rsb	r3, r3, #1
	str	r0, [sp, #48]
	add	r3, r3, r0
	str	r10, [sp, #40]
	str	r3, [sp, #32]
.L4059:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	beq	.L4118
	ldr	r3, [sp, #36]
	ldrb	r8, [r4, #-2538]	@ zero_extendqisi2
	rsb	r1, r3, r5
	ldr	r3, [sp, #48]
	clz	r1, r1
	rsb	r3, r3, r5
	uxth	r9, r8
	clz	r3, r3
	mov	r1, r1, lsr #5
	mov	r3, r3, lsr #5
	orrs	r10, r3, r1
	beq	.L4060
	cmp	r1, #0
	beq	.L4061
	ldr	r0, [sp, #28]
	mov	r1, r8
	bl	__aeabi_uidivmod
	uxth	r10, r1
	rsb	r9, r10, r9
	uxth	r9, r9
	cmp	r9, r6
	uxthhi	r9, r6
	b	.L4060
.L4061:
	cmp	r3, #0
	ldrne	r3, [sp, #28]
	movne	r10, r1
	moveq	r10, r3
	addne	r9, r3, r6
	mlsne	r9, r5, r8, r9
	uxtbne	r9, r9
.L4060:
	ldr	r3, .L4121+16
	mov	r2, #0
	mov	r8, r3
.L4064:
	ldr	r1, [r3, #20]
	cmp	r1, r5
	bne	.L4062
	ldrb	r1, [r3, #2]	@ zero_extendqisi2
	tst	r1, #8
	beq	.L4062
	ldr	r1, .L4121+8
	mov	r3, #48
	mov	r9, r9, asl #9
	mov	r0, r7
	add	r7, r7, r9
	mla	r2, r3, r2, r1
	ldr	r1, [r2, #1240]
	mov	r2, r9
	add	r1, r1, r10, asl #9
	bl	ftl_memcpy
	b	.L4063
.L4062:
	add	r2, r2, #1
	add	r3, r3, #48
	cmp	r2, #32
	bne	.L4064
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #68]
	bne	.L4065
	mov	r0, r5
	add	r1, sp, #68
	mov	r2, #0
	bl	pm_log2phys
.L4065:
	ldr	r3, [sp, #68]
	cmn	r3, #1
	moveq	r9, #0
	bne	.L4119
.L4067:
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L4063
	mla	r3, r3, r5, r9
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #44]
	cmp	r3, r2
	movcs	r2, #1
	movcc	r2, #0
	cmp	r3, r1
	movcs	r2, #0
	cmp	r2, #0
	beq	.L4068
	mov	r0, r7
	mov	r1, #0
	mov	r2, #512
	add	r7, r7, #512
	bl	ftl_memset
.L4068:
	add	r9, r9, #1
	b	.L4067
.L4119:
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4070
	ldr	r2, .L4121+8
	ldr	r1, [r2, #2804]
	ldr	r2, [r1, #40]
	add	r2, r2, #1
	str	r2, [r1, #40]
	ldr	r2, [sp, #68]
	str	r7, [r3, #8]
	add	r7, r7, r9, asl #9
	str	r5, [r3, #20]
	str	r2, [r3, #24]
	str	r2, [r3, #28]
	strb	r9, [r3, #40]
	strb	r10, [r3, #41]
	bl	zftl_add_read_buf
.L4063:
	ldr	r3, [sp, #32]
	add	r5, r5, #1
	sub	r3, r3, #1
	str	r3, [sp, #32]
.L4070:
	ldr	r3, .L4121+8
	ldr	r2, [sp, #32]
	ldrb	r3, [r3, #2773]	@ zero_extendqisi2
	cmp	r2, #0
	cmpne	r3, #2
	bhi	.L4059
	ldrb	r1, [r4, #-2527]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L4059
	ldrb	r3, [r4, #-2528]	@ zero_extendqisi2
	mov	r0, #48
	mla	r0, r0, r3, r8
	bl	sblk_read_page
.L4072:
	ldrb	r3, [r4, #-2527]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4120
	ldrb	r3, [r4, #-2528]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L4073
	ldr	r1, .L4121+20
	movw	r2, #1284
	ldr	r0, .L4121+24
	bl	printk
	bl	dump_stack
.L4073:
	ldr	r3, .L4121+12
	mov	ip, #48
	ldr	r8, .L4121+8
	ldr	r1, .L4121+12
	ldrb	r10, [r3, #-2528]	@ zero_extendqisi2
	ldr	r3, .L4121+16
	mul	ip, ip, r10
	add	r2, r8, ip
	add	r3, r3, ip
	str	r3, [sp, #52]
	ldr	r9, [r2, #1272]
	ldrb	r3, [r2, #1236]	@ zero_extendqisi2
	cmn	r9, #1
	strb	r3, [r1, #-2528]
	bne	.L4074
	ldr	r2, [r8, #1096]
	str	r9, [sp, #40]
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	b	.L4075
.L4074:
	cmp	r9, #256
	bne	.L4075
	ldr	r1, .L4121+28
	ldr	r3, [r2, #1260]
	str	r2, [sp, #60]
	ldrh	r0, [r1]
	ldrb	r1, [r8, #1153]	@ zero_extendqisi2
	mov	r8, #1
	str	r3, [sp, #56]
	rsb	r1, r1, #24
	rsb	r1, r0, r1
	mov	r1, r8, asl r1
	sub	r1, r1, #1
	and	r0, r1, r3, lsr r0
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r2, [sp, #60]
	uxth	fp, r0
	ldr	r3, [sp, #56]
	mov	r1, fp
	ldr	r0, .L4121+32
	ldr	r2, [r2, #1256]
	bl	printk
	mov	r0, fp
	mov	r1, r8
	mov	r2, #0
	bl	gc_add_sblk
.L4075:
	ldr	r1, .L4121+8
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
	beq	.L4076
	ldr	r9, [r0, #1240]
	mvn	ip, #0
	ldr	r8, [r0, #1260]
	ldr	r2, [r0, #1244]
	ldrb	fp, [r0, #1276]	@ zero_extendqisi2
	ldrb	r0, [r1, #1153]	@ zero_extendqisi2
	ldrb	lr, [r4, #-2538]	@ zero_extendqisi2
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
.L4076:
	ldr	ip, .L4121+8
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
	beq	.L4078
	ldr	r1, [ip, #1096]
	ldr	r0, .L4121+36
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
	ldr	r3, .L4121+28
	ldr	r2, [r8, #1260]
	ldrb	r1, [r4, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	mov	r2, r2, lsr r0
	ldr	ip, [sp, #56]
	ldrb	r3, [ip, #1153]	@ zero_extendqisi2
	rsb	r3, r3, #24
	rsb	r3, r0, r3
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	mov	r1, #0
	uxth	r0, r0
	bl	ftl_sblk_dump
.L4078:
	ldr	r3, .L4121+8
	mov	r2, #48
	mla	r2, r2, r10, r3
	mov	r8, r3
	ldr	r1, [r2, #1248]
	ldr	r2, [r2, #1256]
	ldr	r1, [r1, #4]
	cmp	r1, r2
	orrne	fp, fp, #1
	cmp	fp, #0
	beq	.L4079
	ldr	r1, .L4121+20
	movw	r2, #1320
	ldr	r0, .L4121+24
	bl	printk
	bl	dump_stack
.L4079:
	mov	r3, #48
	mla	r10, r3, r10, r8
	ldrb	r3, [r4, #-2538]	@ zero_extendqisi2
	ldrb	r2, [r10, #1276]	@ zero_extendqisi2
	cmp	r3, r2
	ldrlsb	r3, [r10, #1238]	@ zero_extendqisi2
	bicls	r3, r3, #8
	strlsb	r3, [r10, #1238]
	bls	.L4081
	ldrb	r1, [r10, #1277]	@ zero_extendqisi2
	mov	r2, r2, asl #9
	ldr	r3, [r10, #1240]
	ldr	r0, [r10, #1244]
	add	r1, r3, r1, asl #9
	bl	ftl_memcpy
.L4081:
	ldr	r1, [sp, #52]
	ldr	r0, .L4121+40
	bl	buf_remove_buf
	ldr	r0, [sp, #52]
	bl	zbuf_free
	ldr	r3, .L4121+12
	ldr	r2, .L4121+12
	ldrb	r3, [r3, #-2527]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r2, #-2527]
	b	.L4072
.L4120:
	ldr	r1, .L4121+12
	mvn	r2, #0
	strb	r3, [r1, #-2527]
	strb	r2, [r1, #-2528]
	b	.L4059
.L4118:
	bl	timer_get_time
	ldr	r3, .L4121+12
	str	r0, [r3, #-8]
	ldr	r0, [sp, #40]
	b	.L4058
.L4086:
	mvn	r0, #0
.L4058:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4122:
	.align	2
.L4121:
	.word	.LANCHOR2
	.word	.LC262
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2564
	.word	.LC0
	.word	.LANCHOR3-3132
	.word	.LC263
	.word	.LC264
	.word	.LANCHOR0+2775
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r2, #79
	.pad #124
	sub	sp, sp, #124
	mov	r5, r2
	add	r4, sp, #40
	mvnhi	r0, #21
	str	r4, [sp, #36]
	bhi	.L4127
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	mvnne	r0, #13
	bne	.L4127
	add	r3, sp, #120
	mov	r1, r4
	add	r3, r3, r5
	strb	r0, [r3, #-80]
	ldr	r0, .L4152
	bl	printk
	ldr	r0, .L4152+4
	mov	r1, r4
	mov	r2, #1
	mov	r3, #16
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	r0, r4
	ldr	r1, .L4152+8
	mov	r2, #7
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4128
	ldr	r4, .L4152+12
	movw	ip, #698
	ldr	r0, .L4152+16
	mov	r2, #4
	ldr	r7, .L4152+20
	movw	r8, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4152+24
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r7, #-168]
	bl	rknand_print_hex
.L4129:
	ldrh	r3, [r7, #-168]
	cmp	r6, r3
	bge	.L4132
	mov	r0, #300
	bl	msleep
	add	r3, r6, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r8
	beq	.L4130
	mov	r1, #0
	bl	ftl_sblk_dump
.L4130:
	add	r6, r6, #1
	b	.L4129
.L4128:
	mov	r0, r4
	ldr	r1, .L4152+28
	mov	r2, #7
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4133
	ldr	r9, .L4152+12
	movw	ip, #698
	ldr	r0, .L4152+16
	mov	r2, #4
	ldr	r7, .L4152+20
	ldr	r3, [r9, #2804]
	ldr	r10, .L4152+32
	add	r1, r3, #704
	mov	fp, r7
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r9, #2804]
	mov	r2, #2
	ldrh	r3, [r7, #-168]
	add	r1, r1, #416
	ldr	r0, .L4152+24
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
	ldr	r0, .L4152+36
	add	r4, sp, #120
	add	r1, sp, #47
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	uxth	r4, r0
	str	r0, [sp, #32]
.L4135:
	add	r8, r10, r6
	ldr	r0, .L4152+40
	ldrh	r1, [r10, r6]
	ldrh	r2, [r8, #2]
	bl	printk
	ldrh	r3, [r6, r10]
	cmp	r3, r4
	bne	.L4134
	ldrb	r3, [fp, #-2538]	@ zero_extendqisi2
	mov	r2, #4
	ldr	r0, .L4152+44
	ldr	r1, [r8, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	mov	r0, #50
	bl	msleep
.L4134:
	add	r6, r6, #8
	cmp	r6, #256
	bne	.L4135
	mov	r0, #300
	add	r4, r4, #176
	bl	msleep
	mov	r0, #1
	bl	buf_alloc
	ldr	r3, [r9, #2804]
	mov	r1, #1
	ldr	r3, [r3, r4, asl #2]
	str	r3, [r0, #24]
	mov	r6, r0
	str	r3, [sp, #32]
	bl	sblk_read_page
	ldr	ip, [r6, #4]
	ldr	r3, [r6, #12]
	ldr	r0, .L4152+48
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
	ldr	r3, [ip]
	ldr	r2, [r6, #36]
	bl	printk
	ldrb	r3, [r7, #-2538]	@ zero_extendqisi2
	ldr	r0, .L4152+52
	mov	r2, #4
	ldr	r1, [r6, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	mov	r0, r6
	b	.L4151
.L4133:
	mov	r0, r4
	ldr	r1, .L4152+56
	mov	r2, #7
	bl	memcmp
	cmp	r0, #0
	bne	.L4136
	bl	dump_ftl_info
	b	.L4132
.L4136:
	mov	r0, r4
	ldr	r1, .L4152+60
	mov	r2, #9
	bl	memcmp
	cmp	r0, #0
	bne	.L4137
	add	r1, sp, #120
	add	r0, sp, #49
	str	r0, [r1, #-84]!
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4152+64
	str	r0, [sp, #32]
	strh	r0, [r3, #-12]	@ movhi
	bl	dump_all_list_info
	b	.L4132
.L4137:
	mov	r0, r4
	ldr	r1, .L4152+68
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	beq	.L4132
	mov	r0, r4
	ldr	r1, .L4152+72
	mov	r2, #8
	bl	memcmp
	cmp	r0, #0
	bne	.L4139
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4152+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r6, .L4152+20
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
	ldr	r0, .L4152+48
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
	ldrb	r3, [r6, #-2538]	@ zero_extendqisi2
	ldr	r0, .L4152+76
	mov	r2, #4
	ldr	r1, [r4, #4]
	mov	r3, r3, asl #7
	bl	rknand_print_hex
	ldrb	r3, [r6, #-2538]	@ zero_extendqisi2
	ldr	r0, .L4152+80
	mov	r2, #4
	ldr	r1, [r4, #12]
	mov	r3, r3, asl #1
	bl	rknand_print_hex
	mov	r0, r4
.L4151:
	bl	zbuf_free
	b	.L4132
.L4139:
	mov	r0, r4
	ldr	r1, .L4152+84
	mov	r2, #8
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4140
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4152+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r1, r6
	str	r0, [sp, #32]
	uxth	r0, r0
	bl	ftl_sblk_dump
	b	.L4132
.L4140:
	mov	r0, r4
	ldr	r1, .L4152+88
	mov	r2, #10
	bl	memcmp
	cmp	r0, #0
	bne	.L4141
	add	r4, sp, #120
	add	r1, sp, #50
	ldr	r0, .L4152+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	ldr	r3, .L4152+92
	str	r0, [sp, #32]
	str	r0, [r3]
	b	.L4132
.L4141:
	mov	r0, r4
	ldr	r1, .L4152+96
	mov	r2, #8
	bl	memcmp
	subs	r6, r0, #0
	bne	.L4142
	add	r4, sp, #120
	add	r1, sp, #48
	ldr	r0, .L4152+36
	str	r1, [r4, #-84]!
	bl	printk
	mov	r1, r4
	ldr	r0, [sp, #36]
	bl	rk_simple_strtoull.constprop.34
	mov	r4, r0
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #32]
	bne	.L4143
	mov	r0, r4
	add	r1, sp, #32
	mov	r2, r6
	bl	pm_log2phys
.L4143:
	mov	r1, r4
	ldr	r2, [sp, #32]
	ldr	r0, .L4152+100
	bl	printk
	b	.L4132
.L4142:
	ldr	r0, .L4152+104
	bl	printk
	ldr	r0, .L4152+108
	bl	printk
	ldr	r0, .L4152+112
	bl	printk
	ldr	r0, .L4152+116
	bl	printk
	ldr	r0, .L4152+120
	bl	printk
	ldr	r0, .L4152+124
	bl	printk
	ldr	r0, .L4152+128
	bl	printk
	ldr	r0, .L4152+132
	bl	printk
	ldr	r0, .L4152+136
	bl	printk
.L4132:
	bl	rknand_device_unlock
	mov	r0, r5
.L4127:
	add	sp, sp, #124
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4153:
	.align	2
.L4152:
	.word	.LC265
	.word	.LC266
	.word	.LC267
	.word	.LANCHOR0
	.word	.LC268
	.word	.LANCHOR3
	.word	.LC269
	.word	.LC270
	.word	.LANCHOR3-2524
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
	ldr	r3, .L4185
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	sub	r2, r3, #3088
	ldrb	r7, [r0, #9]	@ zero_extendqisi2
	.pad #20
	sub	sp, sp, #20
	ldrh	r2, [r2, #-8]
	mov	r6, r0
	ldrh	r4, [r0, #12]
	ldr	r5, [r3, #-2548]
	mul	r7, r7, r2
	add	r4, r5, r4, asl #2
	str	r3, [sp]
	sub	r2, r7, #-1073741823
	ldr	r2, [r4, r2, asl #2]
	cmn	r2, #1
	beq	.L4155
	ldr	r1, .L4185+4
	movw	r2, #1998
	ldr	r0, .L4185+8
	bl	printk
	bl	dump_stack
.L4155:
	mov	r5, #0
	sub	r4, r4, #4
	mov	r9, r5
.L4156:
	cmp	r9, r7
	beq	.L4184
	ldr	r8, [r4, #4]!
	cmn	r8, #1
	beq	.L4157
	ldr	r3, [sp]
	mov	r0, r8
	ldrb	r1, [r3, #-2538]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	ldr	r3, .L4185+12
	ldr	r3, [r3]
	tst	r3, #4096
	uxth	fp, r0
	beq	.L4158
	ldr	r0, .L4185+16
	mov	r1, fp
	mov	r2, r8
	mov	r3, r9
	bl	printk
.L4158:
	mov	r8, r4
	mov	r10, r9
.L4161:
	ldr	r0, [r8]
	cmn	r0, #1
	beq	.L4159
	ldr	r3, [sp]
	ldrb	r1, [r3, #-2538]	@ zero_extendqisi2
	mov	r1, r1, asl #7
	bl	__aeabi_uidiv
	uxth	r0, r0
	cmp	r0, fp
	bne	.L4159
	ldrb	r3, [r6, #9]	@ zero_extendqisi2
	mov	r0, r10
	mov	r1, r3
	str	r3, [sp, #4]
	bl	__aeabi_idiv
	ldr	r2, .L4185+20
	ldrh	r2, [r2]
	ldr	r3, [sp, #4]
	mls	r3, r3, r0, r10
	add	r3, r6, r3, asl #1
	ldrh	r3, [r3, #16]
	mla	r2, r2, r3, r0
	ldr	r0, .L4185+24
	str	r2, [sp, #12]
	ldr	r1, [r8]
	bl	pm_ppa_update_check
	cmp	r0, #0
	beq	.L4160
	ldr	r3, .L4185
	mov	r2, #4
	ldr	r0, .L4185+28
	ldr	r1, [r3, #-2548]
	mov	r3, r7
	bl	rknand_print_hex
.L4160:
	add	r5, r5, #1
	ldr	r0, [r8]
	add	r1, sp, #12
	mov	r2, #1
	uxth	r5, r5
	bl	pm_log2phys
	mvn	r3, #0
	str	r3, [r8]
.L4159:
	add	r10, r10, #1
	add	r8, r8, #4
	cmp	r10, r7
	bne	.L4161
.L4157:
	add	r9, r9, #1
	b	.L4156
.L4184:
	ldr	r3, .L4185+12
	ldr	r4, .L4185+32
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4163
	ldrh	r1, [r6]
	mov	r2, r5
	ldr	ip, [r4, #1092]
	ldr	r0, .L4185+36
	mov	r3, r1, asl #1
	ldrh	r3, [ip, r3]
	bl	printk
.L4163:
	ldrh	r3, [r6]
	ldr	r2, [r4, #1092]
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4186:
	.align	2
.L4185:
	.word	.LANCHOR3
	.word	.LANCHOR1+2576
	.word	.LC0
	.word	.LANCHOR2
	.word	.LC292
	.word	.LANCHOR3-3066
	.word	.LC293
	.word	.LC294
	.word	.LANCHOR0
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
	beq	.L4188
	bl	zftl_insert_data_list
.L4188:
	ldr	r4, .L4196
	mov	r0, r6
	ldr	r1, [r4, #1096]
	add	r1, r1, #16
	cmp	r1, r6
	movw	r6, #65535
	moveq	r1, #2
	movne	r1, #3
	bl	ftl_open_sblk_init
	ldr	r3, [r4, #1096]
	cmp	r5, r6
	ldr	r3, [r3, #560]
	rsb	r3, r5, r3
	clz	r3, r3
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4190
	mov	r1, r5
	ldr	r0, .L4196+4
	bl	printk
	ldr	r3, [r4, #1096]
	ldr	r0, [r3, #564]
	bl	gc_mark_bad_ppa
	ldr	r3, [r4, #1096]
	mvn	r2, #0
	str	r6, [r3, #560]
	str	r2, [r3, #564]
.L4190:
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L4197:
	.align	2
.L4196:
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
	ldr	r7, .L4266
	mov	r9, r7
.L4199:
	ldrb	r2, [r7, #2800]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4201
	ldrb	r3, [r7, #2824]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r7, #2800]
	ldr	r10, .L4266+4
	str	r3, [sp, #16]
	mov	r3, #48
	ldr	r1, [sp, #16]
	mul	r3, r3, r1
	ldr	r1, .L4266+4
	add	r1, r1, r3
	add	r3, r7, r3
	str	r1, [sp, #20]
	ldr	r2, [r3, #1256]
	ldrb	r1, [r3, #1236]	@ zero_extendqisi2
	ldr	r3, [r7, #2784]
	cmp	r2, r3
	strb	r1, [r7, #2824]
	bcc	.L4202
	ldr	r1, .L4266+8
	movw	r2, #607
	ldr	r0, .L4266+12
	bl	printk
	bl	dump_stack
.L4202:
	ldr	r2, [sp, #16]
	mov	r3, #48
	ldr	r1, [r9, #2784]
	mla	r2, r3, r2, r9
	ldr	r8, [r2, #1256]
	cmp	r8, r1
	bcc	.L4203
	ldr	r0, [sp, #20]
	bl	zbuf_free
	mvn	r0, #0
	b	.L4262
.L4203:
	ldr	r1, [r2, #1248]
	ldr	fp, [r2, #1240]
	ldrb	r5, [r2, #1277]	@ zero_extendqisi2
	ldrb	r2, [r2, #1276]	@ zero_extendqisi2
	str	r1, [sp, #8]
	str	r2, [sp, #12]
	ldrb	r2, [r7, #2801]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4205
	ldr	r6, .L4266+16
	ldrb	r4, [r6, #-80]	@ zero_extendqisi2
	mla	r4, r3, r4, r10
.L4206:
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #255
	mlane	r4, r3, r2, r10
	bne	.L4206
.L4264:
	ldr	r3, [r4, #20]
	cmp	r3, r8
	bne	.L4205
	ldr	r3, [sp, #12]
	mov	r1, r5, asl #9
	ldr	r0, [r4, #4]
	mov	r2, r3, asl #9
	add	r0, r0, r1
	add	r1, fp, r1
	bl	ftl_memcpy
	ldrb	r3, [r6, #-2538]	@ zero_extendqisi2
	ldr	r1, [r4, #12]
	mov	r0, #2
	sub	r3, r3, #2
	ldr	r2, [r4, #4]
	add	r1, r1, #16
	add	r2, r2, r3, asl #9
	bl	ftl_debug_info_fill
	ldr	r0, [sp, #20]
	bl	zbuf_free
	b	.L4199
.L4205:
	mov	r0, r8
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #28]
	bne	.L4209
	mov	r0, r8
	add	r1, sp, #28
	mov	r2, #0
	bl	pm_log2phys
.L4209:
	ldr	r6, [r9, #1096]
	mov	r1, #0
	ldr	r3, .L4266+4
	mov	r4, r1
	ldr	ip, [sp, #28]
	add	r6, r6, #16
	add	r0, r3, #1536
.L4211:
	ldr	r2, [r3, #20]
	cmp	r2, r8
	bne	.L4210
	ldrb	r2, [r3, #2]	@ zero_extendqisi2
	tst	r2, #8
	bicne	r2, r2, #8
	strneb	r2, [r3, #2]
	ldrne	r4, [r3, #4]
	movne	r1, #1
	ldrne	ip, [r3, #24]
.L4210:
	add	r3, r3, #48
	cmp	r3, r0
	bne	.L4211
	ldr	r3, .L4266+16
	cmp	r1, #0
	ldr	r2, [sp, #12]
	strne	ip, [sp, #28]
	ldrb	r3, [r3, #-2538]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L4238
	cmp	r4, #0
	beq	.L4214
	cmp	r5, #0
	beq	.L4215
	mov	r0, fp
	mov	r1, r4
	mov	r2, r5, asl #9
	bl	ftl_memcpy
	ldr	r6, [r9, #1096]
	add	r6, r6, #48
.L4215:
	ldr	r3, [sp, #12]
	add	r2, r5, r3
	ldr	r3, .L4266+16
	ldrb	r10, [r3, #-2538]	@ zero_extendqisi2
	cmp	r2, r10
	ldrcs	r6, [r9, #1096]
	addcs	r6, r6, #16
	bcs	.L4238
	mov	r1, r2, asl #9
	rsb	r2, r2, r10
	add	r0, fp, r1
	add	r1, r4, r1
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4238
.L4214:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L4217
	mov	r0, #1
	bl	buf_alloc
	mov	r1, #1
	ldr	r3, [sp, #28]
	mov	r4, r0
	str	r8, [r0, #20]
	str	r3, [r0, #24]
	bl	sblk_read_page
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #4]
	ldr	r10, [r3, #12]
	cmp	r2, r8
	add	r10, r10, #1
	bne	.L4218
	ldr	r2, [r4, #36]
	cmn	r2, #1
	bne	.L4219
.L4218:
	ldr	lr, .L4266+16
	mvn	r0, #0
	ldr	ip, [r4, #24]
	ldrb	r2, [r9, #1153]	@ zero_extendqisi2
	ldrb	lr, [lr, #-2538]	@ zero_extendqisi2
	rsb	r1, r2, #24
	mvn	r2, r0, asl r2
	and	r0, r2, ip, lsr r1
	str	lr, [sp]
	bic	r1, ip, r2, asl r1
	uxtb	r0, r0
	ldr	r2, [r4, #4]
	bl	flash_read_page_en
	str	r0, [r4, #36]
.L4219:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r8
	bne	.L4220
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4221
.L4220:
	ldr	r2, [r7, #1096]
	ldr	r0, .L4266+20
	ldr	r3, [r2, #552]
	add	r3, r3, #1
	str	r3, [r2, #552]
	ldr	r3, [r4, #36]
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	str	r3, [sp]
	mov	r3, r8
	bl	printk
	mov	r2, #4
	ldr	r0, .L4266+24
	mov	r3, r2
	ldr	r1, [r4, #12]
	bl	rknand_print_hex
.L4221:
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #4]
	cmp	r3, r8
	bne	.L4222
	ldr	r3, [r4, #36]
	cmn	r3, #1
	bne	.L4223
.L4222:
	ldr	r1, .L4266+8
	movw	r2, #699
	ldr	r0, .L4266+12
	bl	printk
	bl	dump_stack
.L4223:
	cmp	r5, #0
	beq	.L4224
	ldr	r3, [sp, #28]
	mov	r2, r5, asl #9
	cmn	r3, #1
	beq	.L4225
	mov	r0, fp
	ldr	r1, [r4, #4]
	bl	ftl_memcpy
	b	.L4226
.L4217:
	cmp	r5, #0
	moveq	r4, r5
	moveq	r10, r5
	beq	.L4224
	mov	r2, r5, asl #9
	mov	r10, r4
.L4225:
	mov	r0, fp
	mov	r1, #0
	bl	ftl_memset
.L4226:
	ldr	r6, [r7, #1096]
	add	r6, r6, #48
.L4224:
	ldr	r3, [sp, #12]
	add	r5, r5, r3
	ldr	r3, .L4266+16
	ldrb	r2, [r3, #-2538]	@ zero_extendqisi2
	cmp	r5, r2
	bcc	.L4227
	ldrhi	r6, [r7, #1096]
	addhi	r6, r6, #16
	b	.L4228
.L4227:
	ldr	r3, [sp, #28]
	rsb	r2, r5, r2
	mov	r5, r5, asl #9
	cmn	r3, #1
	mov	r2, r2, asl #9
	beq	.L4229
	ldr	r1, [r4, #4]
	add	r0, fp, r5
	add	r1, r1, r5
	bl	ftl_memcpy
	b	.L4228
.L4229:
	add	r0, fp, r5
	mov	r1, #0
	bl	ftl_memset
.L4228:
	cmp	r4, #0
	beq	.L4213
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	mov	r1, r4
	ldr	r0, .L4266+28
	bic	r3, r3, #8
	strb	r3, [r4, #2]
	bl	buf_remove_buf
	mov	r0, r4
	bl	zbuf_free
	b	.L4213
.L4238:
	mov	r10, #0
.L4213:
	ldrh	r3, [r6, #6]
	cmp	r3, #0
	bne	.L4230
	bl	ftl_flush
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4230:
	mov	r0, r6
	mov	r4, #48
	bl	ftl_get_new_free_page
	ldr	r3, [sp, #16]
	str	r0, [sp, #12]
	mov	r0, #2
	mul	r4, r4, r3
	ldr	r3, [sp, #8]
	add	r5, r9, r4
	mov	r1, r3
	ldr	r2, [r5, #1252]
	str	r10, [r3, #12]
	stmia	r3, {r2, r8}
	ldr	r2, [sp, #28]
	str	r2, [r3, #8]
	mov	r2, #0
	ldr	r3, .L4266+16
	str	r2, [r1, #16]!
	ldrb	r2, [r3, #-2538]	@ zero_extendqisi2
	sub	r2, r2, #2
	add	r2, fp, r2, asl #9
	bl	ftl_debug_info_fill
	ldr	r0, .L4266+32
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
	ldr	r3, .L4266+4
	ldrh	fp, [r6, #10]
	ldrh	r10, [r6, #12]
	add	r4, r3, r4
	add	r10, fp, r10
	sub	r8, r10, #1
	strh	r8, [r4, #32]	@ movhi
	bl	buf_add_tail
	ldrb	r3, [r9, #2801]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r9, #2801]
	bl	timer_get_time
	ldr	r3, .L4266+16
	ldrh	r2, [r6, #6]
	str	r0, [r3, #-76]
	ldrb	r3, [r9, #2801]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4231
	cmp	r2, #1
	bne	.L4201
.L4231:
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	mov	r4, #48
	ldr	r0, .L4266+16
	cmp	r1, #0
	mov	r1, #0
	ldrb	lr, [r0, #-80]	@ zero_extendqisi2
	moveq	ip, #1
	movne	ip, #2
	cmp	r2, #1
	mov	r2, lr
	moveq	ip, r3
.L4235:
	cmp	r1, ip
	mlane	r2, r4, r2, r7
	addne	r1, r1, #1
	ldrneb	r2, [r2, #1236]	@ zero_extendqisi2
	bne	.L4235
.L4265:
	uxtb	r1, r1
	strb	r2, [r0, #-80]
	rsb	r3, r1, r3
	strb	r3, [r9, #2801]
	ldr	r3, .L4266+4
	mov	r0, #48
	mla	r0, r0, lr, r3
	bl	sblk_prog_page
	ldrh	r3, [r6, #6]
	cmp	r3, #1
	bne	.L4201
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	r0, r6
	bl	ftl_write_last_log_page
	mov	r0, r6
	bl	ftl_alloc_new_data_sblk
.L4201:
	ldrb	r4, [r7, #2800]	@ zero_extendqisi2
	cmp	r4, #0
	bne	.L4199
	bl	ftl_write_completed
	mov	r0, r4
.L4262:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4267:
	.align	2
.L4266:
	.word	.LANCHOR0
	.word	.LANCHOR0+1236
	.word	.LANCHOR1+2596
	.word	.LC0
	.word	.LANCHOR3
	.word	.LC297
	.word	.LC241
	.word	.LANCHOR0+2775
	.word	.LANCHOR3-80
	.fnend
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L4417
	ldr	r5, .L4417+4
	ldrb	r0, [r8, #-3115]	@ zero_extendqisi2
	ldr	r3, [r5, #1096]
	cmp	r0, #0
	str	r3, [sp, #8]
	bne	.L4269
	bl	buf_alloc
	subs	r4, r0, #0
	beq	.L4268
	ldr	r7, .L4417+8
	ldrh	r6, [r7, #22]
	mov	r0, r6
	add	r6, r6, #1
	bl	gc_get_src_ppa_from_index
	mov	r1, #1
	strh	r6, [r7, #22]	@ movhi
	str	r0, [r4, #24]
	mov	r10, r0
	mov	r0, r4
	bl	sblk_read_page
	ldr	r3, [r4, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4271
	ldr	r1, .L4417+12
	movw	r2, #1032
	ldr	r0, .L4417+16
	bl	printk
	bl	dump_stack
.L4271:
	ldr	r3, [r4, #12]
	ldr	r6, [r3, #4]
	mov	r0, r6
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #40]
	bne	.L4272
	mov	r0, r6
	add	r1, sp, #40
	mov	r2, #0
	bl	pm_log2phys
.L4272:
	ldr	r9, [sp, #40]
	cmp	r9, r10
	bne	.L4273
	ldr	r1, .L4417+20
	mov	r3, #0
.L4276:
	add	r2, r1, r3
	ldr	r0, [r2, #20]
	cmp	r0, r6
	bne	.L4274
	ldrb	r2, [r2, #2]	@ zero_extendqisi2
	tst	r2, #2
	beq	.L4274
	mov	r0, r4
	bl	zbuf_free
	ldr	r3, .L4417+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4268
	mov	r1, r6
	mov	r2, r9
	ldrh	r3, [r7, #22]
	ldr	r0, .L4417+28
	bl	printk
	b	.L4268
.L4274:
	add	r3, r3, #48
	cmp	r3, #1536
	bne	.L4276
	ldrb	r3, [r8, #-2538]	@ zero_extendqisi2
	mov	r2, #0
	str	r6, [r4, #20]
	strb	r2, [r4, #41]
	strb	r3, [r4, #40]
	ldr	r3, [r4, #12]
	ldr	r1, [r3]
	str	r1, [r4, #16]
	str	r2, [r3, #16]
	ldr	r3, .L4417+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4277
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r9, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4417+32
	str	r2, [sp, #4]
	mov	r2, r9
	bl	printk
.L4277:
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
	b	.L4268
.L4273:
	ldr	r3, .L4417+24
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4279
	mov	r0, r6
	bl	lpa_hash_get_ppa
	mov	r1, r6
	str	r10, [sp]
	mov	r3, r0
	ldrh	r2, [r7, #22]
	ldr	r0, .L4417+32
	str	r2, [sp, #4]
	mov	r2, r9
	bl	printk
.L4279:
	mov	r0, r4
	bl	zbuf_free
	b	.L4268
.L4269:
	ldr	r3, [sp, #8]
	ldrb	r9, [r3, #89]	@ zero_extendqisi2
	ldrb	r3, [r8, #-3123]	@ zero_extendqisi2
	uxth	fp, r9
	cmp	r3, fp
	movhi	r6, #1
	bhi	.L4281
	cmp	fp, #2
	movcc	r6, fp
	movcs	r6, #2
.L4281:
	ldrb	r4, [r8, #-3124]	@ zero_extendqisi2
	ldr	r10, .L4417
	cmp	r4, #3
	ldr	r3, .L4417+8
	bne	.L4282
	ldrb	r2, [r5, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4283
	movw	r2, #2102
	mov	r1, r9
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bl	__aeabi_idiv
	ldrb	r3, [r10, #-2534]	@ zero_extendqisi2
	cmp	r3, #0
	uxth	r5, r0
	beq	.L4284
	ldr	r3, .L4417+36
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #0
	moveq	r4, #1
	movne	r4, #2
	b	.L4285
.L4284:
	ldrb	r3, [r10, #-15]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r4, #1
	beq	.L4285
	sub	r3, r5, #62
	cmp	r3, #2160
	movcs	r4, #2
	bcs	.L4285
	mov	r1, r4
	mov	r0, r5
	bl	__aeabi_uidivmod
	uxth	r1, r1
	cmp	r1, #0
	movne	r4, #1
	moveq	r4, #2
.L4285:
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #12]
	b	.L4288
.L4283:
	ldrb	r2, [r10, #-3116]	@ zero_extendqisi2
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	beq	.L4287
	add	r1, r9, r9, asl #1
	bl	__aeabi_idiv
	ldr	r2, .L4417+40
	mul	r9, r9, r0
	uxth	r5, r0
	sub	r9, r9, r9, asl #2
	add	r1, r7, r9
	smull	r2, r3, r1, r2
	sub	r1, r3, r1, asr #31
	uxth	r3, r1
	str	r3, [sp, #12]
	ldr	r3, .L4417+4
	ldrb	r3, [r3, #1161]	@ zero_extendqisi2
	cmp	r3, #0
	addne	r5, r5, r5, asl #1
	uxthne	r5, r5
	b	.L4288
.L4287:
	mov	r1, r9
	bl	__aeabi_idiv
	ldr	r2, .L4417+44
	smulbb	r1, r0, fp
	mov	r3, r0, asl #1
	ldrh	r2, [r2, r3]
	rsb	r1, r1, r7
	and	r3, r2, #7
	str	r3, [sp, #16]
	mov	r5, r2, lsr #3
	uxth	r3, r1
	str	r3, [sp, #12]
	mla	r9, r9, r5, r3
	ldr	r3, [sp, #16]
	cmp	r3, #1
	add	r9, r9, r9, asl #1
	uxth	r7, r9
	bne	.L4289
	b	.L4288
.L4282:
	ldrb	r2, [r10, #-3122]	@ zero_extendqisi2
	mov	r1, r9
	cmp	r2, #0
	movw	r2, #2102
	ldrh	r7, [r3, r2]
	mov	r0, r7
	bne	.L4290
	bl	__aeabi_idiv
	mov	r4, #1
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #12]
	b	.L4288
.L4290:
	bl	__aeabi_idiv
	mov	r4, #2
	uxth	r5, r0
	smulbb	r1, r5, fp
	rsb	r1, r1, r7
	uxth	r3, r1
	str	r3, [sp, #12]
.L4288:
	mul	r3, r6, r4
	ldr	r10, .L4417+36
	mov	r2, #0
	ldr	ip, .L4417
	add	r10, r10, r5, asl #1
	ldr	r1, .L4417+8
	str	r3, [sp, #16]
	b	.L4291
.L4412:
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4349
	tst	lr, #1
	beq	.L4349
.L4292:
	ldr	fp, [r8, #-120]
	ldr	r0, [fp, r3, asl #2]
	cmn	r0, #1
	beq	.L4411
.L4295:
	add	r2, r2, #1
.L4291:
	ldr	r3, [sp, #16]
	uxth	lr, r2
	cmp	lr, r3
	bge	.L4349
	ldr	r0, .L4417+48
	add	r3, r7, lr
	ldr	r9, [sp, #8]
	ldrh	r0, [r0]
	ldrb	r9, [r9, #89]	@ zero_extendqisi2
	mul	r0, r9, r0
	ldrb	r9, [r8, #-11]	@ zero_extendqisi2
	rsb	r0, r9, r0
	cmp	r3, r0
	blt	.L4292
	ldrb	r0, [ip, #-2534]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L4412
.L4349:
	mov	r3, #1
	str	r3, [sp, #16]
.L4289:
	rsb	r1, r6, r6, asl #15
	mul	r3, r6, r4
	mov	r1, r1, asl #1
	smulbb	r1, r1, r4
	str	r3, [sp, #20]
	add	r1, r7, r1
	uxth	r3, r1
	ldr	r1, .L4417+36
	str	r3, [sp, #32]
	mov	r3, #0
	str	r3, [sp, #24]
	add	r3, r1, r5, asl #1
	str	r3, [sp, #28]
	b	.L4294
.L4411:
	ldrh	r9, [r1, #22]
	ldrh	r0, [r1, #20]
	cmp	r0, r9
	bls	.L4268
	ldrb	r0, [ip, #-2534]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L4296
	ldrh	r0, [r10]
	cmp	r0, r5
	bcs	.L4296
	tst	lr, #1
	ldrne	r0, [fp, r0, asl #2]
	strne	r0, [fp, r3, asl #2]
	bne	.L4295
.L4296:
	mov	r0, r9
	str	r1, [sp, #32]
	str	ip, [sp, #28]
	add	r9, r9, #1
	str	r2, [sp, #24]
	str	r3, [sp, #20]
	bl	gc_get_src_ppa_from_index
	ldr	r3, .L4417+8
	strh	r9, [r3, #22]	@ movhi
	ldr	r3, [sp, #20]
	ldr	r1, [sp, #32]
	ldr	ip, [sp, #28]
	str	r0, [fp, r3, asl #2]
	ldr	r2, [sp, #24]
	b	.L4295
.L4320:
	ldr	r3, [fp, #-3120]
	add	r10, r7, r9
	ldrb	r3, [r3, r10]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L4413
.L4298:
	ldr	r1, .L4417
	add	r2, sp, #64
	add	r2, r2, r9, asl #2
	ldr	r0, .L4417+20
	ldr	ip, .L4417+4
	ldr	r3, [r1, #-3120]
	ldrb	r8, [r3, r10]	@ zero_extendqisi2
	mov	r3, #48
	mul	r3, r3, r8
	add	r0, r0, r3
	str	r0, [r2, #-24]
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	add	r3, ip, r3
	strb	r2, [r3, #1281]
	mov	r2, #2
	strh	r2, [r0, #34]	@ movhi
	ldrb	r2, [r1, #-3124]	@ zero_extendqisi2
	cmp	r2, #3
	bne	.L4311
	ldrb	r2, [ip, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L4312
	ldrb	r1, [r1, #-2534]	@ zero_extendqisi2
	and	r2, r9, #1
	add	r3, r5, r2
	cmp	r1, #0
	beq	.L4313
	ldr	r1, [sp, #28]
	ldrh	r1, [r1]
	cmp	r1, r5
	movcs	r2, #0
	andcc	r2, r2, #1
	cmp	r2, #0
	movne	r3, r1
.L4313:
	ldr	r2, .L4417+4
	mov	r1, r4
	mov	fp, #48
	mov	r0, r9
	str	r3, [sp, #36]
	mla	fp, fp, r8, r2
	bl	__aeabi_uidiv
	ldr	r1, .L4417+52
	ldrh	r1, [r1]
	ldr	r3, [sp, #12]
	uxtah	r0, r3, r0
	ldr	r3, [sp, #8]
	add	r0, r3, r0, asl #1
	ldr	r3, [sp, #36]
	ldrh	r2, [r0, #96]
	mla	r3, r1, r2, r3
	orr	r3, r3, #50331648
	str	r3, [fp, #1260]
.L4314:
	mov	r3, #48
	mul	r8, r3, r8
	ldr	r3, .L4417+4
	add	r3, r3, r8
	ldr	r1, [r3, #1260]
	ldr	r3, .L4417+20
	add	r8, r3, r8
	ldr	r3, .L4417
	ldrh	r2, [r8, #32]
	ldr	r3, [r3, #-116]
	str	r1, [r3, r2, asl #2]
.L4339:
	ldr	r8, .L4417
	ldrb	r3, [r8, #-2534]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4414
.L4319:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
.L4294:
	ldrh	r9, [sp, #24]
	ldr	r3, [sp, #20]
	ldr	fp, .L4417
	cmp	r9, r3
	blt	.L4320
	b	.L4415
.L4413:
	mov	r0, #0
	bl	buf_alloc
	subs	r8, r0, #0
	bne	.L4299
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	r0, r8
	bl	buf_alloc
	subs	r8, r0, #0
	beq	.L4268
.L4299:
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldr	r3, [fp, #-3120]
	strb	r2, [r3, r10]
	ldr	r2, .L4417+4
	ldrb	r3, [r2, #2835]	@ zero_extendqisi2
	add	r3, r3, #1
	strb	r3, [r2, #2835]
	add	r3, r9, r7
	strh	r3, [r8, #32]	@ movhi
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r8, #45]
	ldr	r3, .L4417+48
	ldrh	r2, [r3]
	ldr	r3, [sp, #8]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	ldrb	r2, [fp, #-11]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r10, r3
	blt	.L4300
	ldr	r3, .L4417
	ldrb	r3, [r3, #-2534]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4301
	ldr	r3, [sp, #28]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4301
	tst	r9, #1
	bne	.L4300
.L4301:
	ldr	fp, .L4417
	mvn	r1, #0
	ldr	r2, [fp, #-124]
	str	r1, [r2, r10, asl #2]
	ldrb	r2, [fp, #-11]	@ zero_extendqisi2
	cmp	r2, #2
	bne	.L4302
	ldr	r2, .L4417+48
	ldr	r3, [sp, #8]
	ldrh	r1, [r2]
	ldrb	r2, [r3, #89]	@ zero_extendqisi2
	mul	r2, r2, r1
	sub	r2, r2, #2
	cmp	r10, r2
	ldrh	r2, [fp, #-14]
	bne	.L4303
	ldr	r1, [fp, #-124]
	ldr	r0, [r8, #4]
	bl	ftl_memcpy
	ldrb	r2, [fp, #-2538]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r8, #12]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r2, [r8, #12]
	ldr	r3, .L4417+56
	str	r3, [r2]
	ldr	r3, [r8, #12]
	ldr	r0, [r8, #4]
	ldrh	r1, [fp, #-14]
	str	r3, [sp, #36]
	bl	js_hash
	ldr	r2, .L4417+4
	ldr	r2, [r2, #1096]
	ldr	r3, [sp, #36]
	str	r0, [r3, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r2, #132]
	str	r2, [r3, #8]
	mov	r2, #0
	ldr	r3, [r8, #12]
	b	.L4406
.L4303:
	ldrb	r0, [fp, #-3123]	@ zero_extendqisi2
	mul	r1, r0, r1
	ldr	r0, [r8, #4]
	rsb	ip, r2, r1, asl #2
	ldr	r1, [fp, #-124]
	mov	r2, r2, lsr #2
	str	ip, [sp, #36]
	add	r1, r1, r2, asl #2
	mov	r2, ip
	bl	ftl_memcpy
	ldrb	r2, [fp, #-2538]	@ zero_extendqisi2
	mov	r1, #0
	ldr	r0, [r8, #12]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, [r8, #12]
	ldr	r2, .L4417+56
	str	r2, [r3]
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldr	ip, [sp, #36]
	mov	r1, ip
	bl	js_hash
	ldr	r2, .L4417+4
	ldr	r2, [r2, #1096]
	str	r0, [fp, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r2, #132]
	str	r2, [r3, #8]
	mov	r2, #1
	ldr	r3, [r8, #12]
.L4406:
	str	r2, [r3, #12]
	b	.L4304
.L4302:
	ldr	ip, .L4417+48
	ldrb	r2, [fp, #-3123]	@ zero_extendqisi2
	ldr	r0, [r8, #4]
	ldrh	r1, [ip]
	str	ip, [sp, #36]
	mul	r2, r2, r1
	ldr	r1, [fp, #-124]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	ldrb	r2, [fp, #-3123]	@ zero_extendqisi2
	ldrb	r0, [fp, #-2538]	@ zero_extendqisi2
	ldr	ip, [sp, #36]
	ldrh	r1, [ip]
	mul	r1, r2, r1
	cmp	r0, r1, asr #6
	mov	r2, r1, asl #2
	ldr	r0, [r8, #4]
	ldrlth	r3, [fp, #-14]
	mov	r1, r2
	add	r0, r0, r1
	rsblt	r2, r2, r3
	ldr	r3, .L4417
	ldr	r1, [r3, #-120]
	str	r3, [sp, #36]
	bl	ftl_memcpy
	mov	r1, #0
	ldr	r0, [r8, #12]
	ldr	r3, [sp, #36]
	ldrb	r2, [r3, #-2538]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r2, [r8, #12]
	ldr	r3, .L4417+56
	str	r3, [r2]
	ldr	r2, .L4417+48
	ldr	r0, [r8, #4]
	ldr	fp, [r8, #12]
	ldrh	r1, [r2]
	ldr	r3, [sp, #36]
	ldrb	r3, [r3, #-3123]	@ zero_extendqisi2
	mul	r1, r3, r1
	mov	r1, r1, asl #2
	bl	js_hash
	ldr	r2, .L4417+4
	ldr	r2, [r2, #1096]
	str	r0, [fp, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r2, #132]
	b	.L4407
.L4300:
	ldr	fp, .L4417
	mov	r0, r8
	mov	r1, #1
	ldr	r3, [fp, #-120]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r8, #24]
	bl	sblk_read_page
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4307
	ldr	r3, .L4417+60
	ldr	r2, [r8, #24]
	ldrb	r1, [fp, #-3130]	@ zero_extendqisi2
	ldrh	r0, [r3]
	ldr	r3, .L4417+4
	mov	r2, r2, lsr r0
	ldrb	r3, [r3, #1153]	@ zero_extendqisi2
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
.L4307:
	ldr	r3, [r8, #36]
	cmn	r3, #1
	cmpne	r3, #512
	bne	.L4309
	ldr	r1, .L4417+12
	movw	r2, #1223
	ldr	r0, .L4417+16
	bl	printk
	bl	dump_stack
.L4309:
	ldr	r3, [r8, #12]
	ldr	r2, .L4417+4
	ldr	r1, [r3, #4]
	ldr	r2, [r2, #2784]
	cmp	r1, r2
	mvncs	r2, #0
	strcs	r2, [r3, #4]
	ldr	r3, [r8, #12]
	ldr	r2, [r3, #4]
	ldr	r3, .L4417
	ldr	r3, [r3, #-124]
	str	r2, [r3, r10, asl #2]
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #24]
.L4407:
	str	r2, [r3, #8]
.L4304:
	ldr	r3, [r8, #12]
	mov	r2, #0
	str	r2, [r3, #16]
	b	.L4298
.L4312:
	ldrb	r2, [ip, #1161]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, .L4417+64
	umull	r0, r1, r9, r2
	beq	.L4315
	mov	r1, r1, lsr #1
	ldr	r0, [sp, #12]
	ldr	ip, [sp, #8]
	uxtah	r0, r0, r1
	add	r1, r1, r1, asl #1
	rsb	r1, r1, r9
	add	r0, ip, r0, asl #1
	ldr	ip, .L4417+52
	ldrh	r0, [r0, #96]
	ldrh	ip, [ip]
	mla	r0, ip, r0, r5
	uxtah	r0, r0, r1
	str	r0, [r3, #1260]
	b	.L4316
.L4315:
	ldr	r0, [sp, #12]
	ubfx	r1, r1, #1, #16
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	add	r1, r0, r1, asl #1
	ldr	r0, .L4417+52
	ldrh	r1, [r1, #96]
	ldrh	r0, [r0]
	mla	r1, r0, r1, r5
	str	r1, [r3, #1260]
	b	.L4316
.L4311:
	cmp	r2, #2
	bne	.L4314
	ldrb	r2, [r1, #-3122]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, [sp, #12]
	bne	.L4318
	ldr	r1, [sp, #8]
	add	r2, r2, r9
	add	r2, r1, r2, asl #1
	ldr	r1, .L4417+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	b	.L4408
.L4318:
	ldr	r1, [sp, #8]
	add	r2, r2, r9, lsr #1
	add	r2, r1, r2, asl #1
	ldr	r1, .L4417+52
	ldrh	r2, [r2, #96]
	ldrh	r1, [r1]
	mla	r2, r1, r2, r5
	and	r1, r9, #1
	add	r2, r2, r1
.L4408:
	orr	r2, r2, #33554432
	str	r2, [r3, #1260]
	b	.L4314
.L4414:
	ldr	r3, [sp, #28]
	ldrh	r3, [r3]
	cmp	r3, r5
	bcs	.L4319
	tst	r9, #1
	beq	.L4319
	ldr	r2, [r8, #-3120]
	mvn	r3, #0
	ldrh	r1, [sp, #32]
	ldr	r0, [sp, #32]
	strb	r3, [r2, r10]
	add	r2, sp, #64
	add	r9, r2, r9, asl #2
	ldr	r2, [r9, #-24]
	strh	r1, [r2, #32]	@ movhi
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r8, #-3120]
	strb	r1, [r2, r0]
	ldr	r2, [r8, #-120]
	str	r3, [r2, r10, asl #2]
	b	.L4319
.L4415:
	ldrb	r3, [fp, #-3124]	@ zero_extendqisi2
	cmp	r3, #3
	bne	.L4321
	ldr	r3, .L4417+4
	ldrb	r1, [r3, #1160]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [sp, #20]
	movne	r2, #0
	subne	r1, r3, #1
	beq	.L4416
.L4323:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4409
	add	r0, sp, #64
	add	r0, r0, r3, asl #2
	ldr	ip, [r0, #-24]
	add	r0, sp, #64
	add	r0, r0, r3, asl #2
	ldr	r3, [r0, #-20]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [ip]
	b	.L4323
.L4416:
	ldrb	r2, [fp, #-3116]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	cmp	r2, #0
	strneb	r1, [r3, #44]
	bne	.L4410
	ldr	r1, [sp, #16]
	cmp	r1, #1
	moveq	r2, #9
	beq	.L4329
	ldr	r1, [sp, #16]
	cmp	r1, #2
	moveq	r2, #13
.L4329:
	strb	r2, [r3, #44]
.L4410:
	add	r1, sp, #52
	mov	r2, r6
	add	r0, sp, #40
	bl	sblk_xlc_prog_pages
	b	.L4325
.L4321:
	ldrb	r3, [fp, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4330
	ldrb	r3, [fp, #-3121]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4331
.L4330:
	ldr	r3, [sp, #20]
	mov	r2, #0
	sub	r1, r3, #1
	b	.L4332
.L4331:
	mov	r1, r6
	add	r0, sp, #40
	bl	sblk_3d_mlc_prog_pages
	b	.L4325
.L4332:
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1
	bge	.L4409
	add	r0, sp, #64
	add	r0, r0, r3, asl #2
	ldr	ip, [r0, #-24]
	add	r0, sp, #64
	add	r0, r0, r3, asl #2
	ldr	r3, [r0, #-20]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	strb	r3, [ip]
	b	.L4332
.L4409:
	add	r3, sp, #64
	mvn	r2, #0
	add	r1, r3, r1, asl #2
	ldr	r3, [r1, #-24]
	smulbb	r1, r4, r6
	strb	r2, [r3]
	ldr	r0, [sp, #40]
	uxtb	r1, r1
	bl	sblk_prog_page
.L4325:
	ldrb	r3, [fp, #-3116]	@ zero_extendqisi2
	ldr	r2, .L4417
	cmp	r3, #0
	ldr	r3, .L4417+4
	addne	r4, r6, r6, asl #1
	bne	.L4335
	ldrb	r1, [r2, #-3122]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r4, r6, asl #1
	bne	.L4335
	ldrb	r1, [r3, #1160]	@ zero_extendqisi2
	cmp	r1, #0
	moveq	r4, r6
	beq	.L4335
	ldrb	r2, [r2, #-2534]	@ zero_extendqisi2
	mul	r4, r4, r6
	cmp	r2, #0
	beq	.L4335
	ldr	r1, .L4417+36
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, r5
	movcc	r4, r6
.L4335:
	ldr	r2, [r3, #2804]
	ldr	r3, [r2, #52]
	add	r3, r3, r4
	str	r3, [r2, #52]
	ldr	r2, .L4417+8
	movw	r3, #2102
	ldrh	r1, [r2, r3]
	add	r4, r4, r1
	uxth	r4, r4
	strh	r4, [r2, r3]	@ movhi
	ldr	r3, .L4417+68
	ldrh	r2, [r3, #-10]
	ldr	r3, [sp, #8]
	ldrb	r3, [r3, #89]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r4, r3
	ldrge	r3, .L4417+4
	movge	r2, #0
	ldrge	r3, [r3, #1096]
	strgeh	r2, [r3, #86]	@ movhi
	bl	gc_write_completed
	b	.L4268
.L4316:
	mov	r0, #48
	ldr	r3, .L4417+20
	mul	r0, r0, r8
	ldr	ip, .L4417+4
	add	r3, r3, r0
	add	r0, ip, r0
	ldrh	r1, [r3, #32]
	ldr	r3, .L4417
	ldr	r0, [r0, #1260]
	ldr	lr, [r3, #-116]
	umull	r2, r3, r9, r2
	mov	r2, r3, lsr #1
	add	r2, r2, r2, asl #1
	rsb	r2, r2, r9
	mov	r2, r2, asl #24
	add	r2, r2, #16777216
	orr	r2, r2, r0
	str	r2, [lr, r1, asl #2]
	b	.L4339
.L4268:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4418:
	.align	2
.L4417:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR0+2828
	.word	.LANCHOR1+2616
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
	ldr	r4, .L4644+60
	cmp	r0, #1
	.pad #44
	sub	sp, sp, #44
	mov	fp, r0
	ldr	r5, .L4644+32
	ldrh	r9, [r4, r3]
	movw	r3, #2788
	ldrh	r7, [r4, r3]
	movw	r3, #2790
	ldr	r6, [r4, #1096]
	add	r7, r9, r7
	ldrh	r8, [r4, r3]
	uxth	r7, r7
	beq	.L4420
.L4431:
	ldrb	r3, [r5, #-136]	@ zero_extendqisi2
	cmp	r3, #6
	ldrls	pc, [pc, r3, asl #2]
	b	.L4545
.L4423:
	.word	.L4422
	.word	.L4424
	.word	.L4425
	.word	.L4426
	.word	.L4427
	.word	.L4546
	.word	.L4429
.L4420:
	ldr	r10, [r5, #-8]
	cmp	r10, #0
	bne	.L4430
	ldr	r3, [r5, #-4]
	cmp	r3, #0
	beq	.L4431
.L4430:
	ldr	r3, .L4644+32
	sub	r2, r3, #3072
	ldrh	r2, [r2, #-14]
	cmp	r7, r2, lsr #2
	bls	.L4431
	movw	r2, #2808
	ldrh	r2, [r4, r2]
	cmp	r2, r7
	bcs	.L4431
	str	r3, [sp, #28]
	bl	timer_get_time
	add	r10, r10, #20
	ldr	r3, [sp, #28]
	cmp	r10, r0
	ldr	r10, [r5, #-4]
	movcc	r2, #0
	strcc	r2, [r3, #-8]
	bl	timer_get_time
	add	r10, r10, #20
	cmp	r10, r0
	ldrcc	r3, .L4644+32
	movcc	r2, #0
	strcc	r2, [r3, #-4]
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4545
	b	.L4431
.L4422:
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
	ldr	r9, .L4644+60
	uxth	r10, r10
	beq	.L4434
	cmp	fp, #0
	bne	.L4435
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r7, r3, asl #1
	bge	.L4545
.L4435:
	ldr	fp, .L4644+32
	sub	r9, fp, #3088
	sub	r0, r9, #12
	ldrh	r1, [r9]
	add	r1, r1, #1
	uxth	r1, r1
	strh	r1, [r9]	@ movhi
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4437
	ldr	ip, [r4, #1092]
	mov	r2, r0, asl #1
	ldr	r3, [fp, #-132]
	ldr	r1, .L4644+60
	ldrh	r2, [ip, r2]
	add	r3, r3, #1
	ldrh	ip, [r9, #-8]
	str	r3, [fp, #-132]
	cmp	ip, r2
	bcs	.L4438
	movw	ip, #1080
	ldrh	ip, [r1, ip]
	cmp	r3, ip, lsr #4
	bls	.L4437
	movw	r3, #2810
	ldrh	r3, [r1, r3]
	cmp	r3, r2
	bls	.L4437
.L4438:
	mov	r1, #0
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	ldr	r9, .L4644+32
	cmp	r0, #0
	beq	.L4439
	mov	r3, #1
	strb	r3, [r9, #-136]
	mov	r3, #0
	str	r3, [r9, #-132]
	b	.L4545
.L4437:
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L4439:
	cmp	r8, #15
	movls	r9, #2
	bls	.L4440
	ldr	r2, .L4644
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
.L4440:
	ldr	r3, .L4644+4
	ldr	r3, [r3]
	tst	r3, #256
	beq	.L4441
	ldr	r3, [r4, #1096]
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	str	r8, [sp]
	ldrh	r1, [r3, #124]
	ldr	r0, .L4644+8
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
.L4441:
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
	mov	r1, r9
	mov	r2, #1
	bl	gc_search_src_blk
	cmp	r0, #0
	ble	.L4442
.L4443:
	mov	r3, #1
	b	.L4640
.L4442:
	ldr	r3, .L4644+32
	mov	r1, #3
	mov	r2, #1
	ldrb	r0, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_search_src_blk
	cmp	r0, #0
	bgt	.L4443
	b	.L4545
.L4434:
	cmp	fp, #1
	bne	.L4444
	ldr	r3, [r9, #2804]
	ldrh	r3, [r3, #150]
	cmp	r3, #0
	beq	.L4445
	movw	r3, #2792
	ldrh	r3, [r9, r3]
	cmp	r3, #8
	bls	.L4445
	bl	gc_ink_check_sblk
.L4445:
	bl	gc_scan_static_data
	ldr	r3, [r4, #1096]
	ldrh	r3, [r3, #122]
	cmp	r3, #0
	beq	.L4446
.L4447:
	mov	r3, #1
	strb	r3, [r5, #-3115]
	b	.L4640
.L4446:
	bl	gc_static_wearleveling
	cmp	r0, #0
	bne	.L4447
	bl	gc_block_vpn_scan
	cmp	r8, #0
	beq	.L4448
	cmp	r7, r10
	ldr	r6, .L4644
	bcs	.L4449
	ldrh	r3, [r6, #-14]
	cmp	r3, r7
	bhi	.L4450
.L4449:
	ldrh	r2, [r6, #-14]
	add	r3, r7, r10
	cmp	r3, r2
	blt	.L4450
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldr	r3, .L4644
	ldrh	r3, [r3, #-12]
	cmp	r2, r3
	bcc	.L4448
.L4450:
	ldr	r0, .L4644+12
	mov	r1, #16
	mov	ip, #1
	strb	ip, [r5, #-3115]
	str	ip, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r9, .L4644+32
	cmp	r0, r2
	beq	.L4451
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldr	ip, [sp, #28]
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-128]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4451
	mov	r2, #0
	mov	r1, ip
	strb	r2, [r9, #-3115]
	str	r2, [r9, #-132]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4443
.L4451:
	ldr	r9, .L4644+32
	mov	r1, #0
	sub	r0, r9, #3104
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4452
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r9, #-128]
	cmp	r3, r2
	movcs	r3, #0
	movcc	r3, #1
	cmp	r7, #2
	movls	r3, #0
	cmp	r3, #0
	beq	.L4452
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3115]
	str	r2, [r9, #-132]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4443
.L4452:
	ldr	r1, .L4644+32
	mov	r9, #0
	ldrh	r3, [r5, #-128]
	sub	r2, r1, #3088
	ldr	ip, .L4644+60
	add	r3, r3, #1
	ldrh	r2, [r2, #-8]
	uxth	r3, r3
	strh	r3, [r5, #-128]	@ movhi
	cmp	r3, r2, lsr #5
	movhi	r3, #4
	strhih	r3, [r1, #-128]	@ movhi
	ldr	r3, .L4644+32
	ldr	r1, [r5, #-132]
	sub	r0, r3, #3104
	add	r1, r1, #1
	str	r1, [r5, #-132]
	strh	r9, [r0, #-8]	@ movhi
	strh	r9, [r0, #-6]	@ movhi
	strh	r9, [r0, #-4]	@ movhi
	movw	r0, #1080
	ldrh	r0, [r4, r0]
	cmp	r1, r0, lsr #5
	bls	.L4454
	movw	r1, #2798
	ldrh	r1, [ip, r1]
	cmp	r1, r8
	bls	.L4454
	mov	r1, r9
	ldr	r0, .L4644+16
	str	ip, [sp, #36]
	str	r3, [sp, #32]
	str	r2, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r1, #65535
	cmp	r0, r1
	beq	.L4550
	ldr	ip, [sp, #36]
	mov	r0, r0, asl #1
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #28]
	ldr	r1, [ip, #1092]
	ldrh	r0, [r1, r0]
	ldrb	r1, [r3, #-3123]	@ zero_extendqisi2
	mul	r2, r1, r2
	cmp	r0, r2
	strle	r9, [r3, #-132]
	ldrleb	r0, [r3, #-3115]	@ zero_extendqisi2
	movle	r1, #2
	bgt	.L4550
	b	.L4633
.L4454:
	movw	r2, #2798
	ldr	r3, .L4644+60
	ldrh	r1, [r4, r2]
	ldrh	r2, [r6, #-12]
	cmp	r1, r2
	ldrcs	r3, .L4644+32
	movcs	r1, #2
	movcs	r2, #1
	ldrcsb	r0, [r3, #-3115]	@ zero_extendqisi2
	bcs	.L4632
.L4457:
	movw	r2, #2794
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L4458
	movw	r2, #2796
	ldrh	r3, [r3, r2]
	cmp	r3, #8
	bls	.L4459
.L4458:
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
	mov	r1, #1
.L4633:
	mov	r2, #4
.L4632:
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4443
	b	.L4550
.L4459:
	ldr	r2, .L4644+56
	movw	r3, #2106
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	movne	r6, #16
	moveq	r6, #0
	b	.L4460
.L4448:
	ldr	r3, .L4644+32
	sub	r2, r3, #3088
	ldrh	r1, [r2, #-8]
	ldrh	r2, [r5, #-128]
	cmp	r2, r1, lsr #5
	movcs	r2, #4
	strcsh	r2, [r3, #-128]	@ movhi
	b	.L4550
.L4444:
	movw	r3, #2808
	ldrh	r3, [r9, r3]
	cmp	r3, r7
	bcs	.L4550
	ldr	r3, [r9, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	beq	.L4545
.L4550:
	mov	r6, #16
.L4460:
	ldr	r3, [r4, #2820]
	cmp	r3, #0
	beq	.L4462
	ldr	r3, .L4644+60
	cmp	r7, #15
	mov	r2, #0
	ldr	r0, .L4644+16
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
	beq	.L4465
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #8
	bhi	.L4465
	ldr	r3, .L4644+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldr	r3, .L4644+32
	ldrb	r2, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4443
.L4465:
	ldr	r9, .L4644+32
	mov	r1, #0
	sub	r0, r9, #3104
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4466
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4466
	ldr	r3, .L4644+60
	mov	r2, #1
	mov	r1, #0
	str	r2, [r3, #2820]
	ldrb	r2, [r9, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4443
.L4466:
	ldr	r0, .L4644+12
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4462
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #4
	bhi	.L4462
	ldr	r3, .L4644+60
	mov	r1, #1
	str	r1, [r3, #2820]
	ldr	r3, .L4644+32
	ldrb	r2, [r3, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4443
.L4462:
	ldr	r2, [r4, #1096]
	mov	r1, #1
	ldr	r9, .L4644+32
	ldr	r3, .L4644+60
	ldrh	ip, [r2, #124]
	strb	r1, [r5, #-3115]
	cmp	ip, #0
	strneb	r1, [r9, #-136]
	movne	r3, #0
	strneb	r3, [r9, #-3115]
	bne	.L4468
	movw	r2, #2808
	ldrh	r0, [r3, r2]
	cmp	r7, r0
	bcs	.L4469
	cmp	r8, #0
	beq	.L4470
	cmp	r8, #16
	bls	.L4471
	sub	r2, r9, #3072
	movw	r0, #2798
	ldrh	r0, [r3, r0]
	ldrh	r2, [r2, #-12]
	cmp	r0, r2
	bhi	.L4471
	mov	r0, r1
	mov	r2, #4
	str	ip, [sp, #32]
	str	r3, [sp, #28]
	bl	gc_search_src_blk
	uxth	r0, r0
	ldr	r3, [sp, #28]
	cmp	r0, #0
	ldr	ip, [sp, #32]
	beq	.L4637
	sub	r7, r9, #3088
	mov	r1, ip
	sub	r0, r7, #12
	str	r3, [sp, #28]
	bl	_list_get_gc_head_node
	movw	r2, #65535
	cmp	r0, r2
	beq	.L4543
	ldr	r3, [sp, #28]
	mov	r1, r0, asl #1
	ldr	r2, [r9, #-132]
	ldr	ip, [r3, #1092]
	add	r2, r2, #1
	str	r2, [r9, #-132]
	ldrh	r1, [ip, r1]
	ldrh	ip, [r7, #-8]
	cmp	ip, r1
	bcs	.L4475
	movw	ip, #1080
	ldrh	ip, [r3, ip]
	cmp	r2, ip, lsr #4
	bls	.L4543
	movw	r2, #2810
	ldrh	r3, [r3, r2]
	cmp	r3, r1
	bls	.L4543
.L4475:
	mov	r1, #0
	ldrb	r2, [r5, #-3115]	@ zero_extendqisi2
	bl	gc_add_sblk
	mov	r3, #1
	str	r3, [r4, #2820]
	mov	r3, #0
	str	r3, [r5, #-132]
	b	.L4543
.L4471:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r5, #-3115]	@ zero_extendqisi2
	bne	.L4543
	b	.L4636
.L4470:
	cmp	r7, #16
	strb	r8, [r9, #-3115]
	mov	r0, r8
	bhi	.L4636
	mov	r2, r1
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4543
.L4637:
	ldrb	r0, [r9, #-3115]	@ zero_extendqisi2
.L4636:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	b	.L4473
.L4469:
	cmp	fp, #1
	bne	.L4468
	cmp	r7, r0, asl #1
	bge	.L4478
	cmp	r10, r8, lsr #1
	bcs	.L4479
	sub	r9, r9, #3072
	movw	r1, #2798
	ldrh	r0, [r3, r1]
	ldrh	r1, [r9, #-12]
	cmp	r0, r1
	ldrcch	r1, [r9, #-14]
	movcc	r1, r1, lsr #2
	strcch	r1, [r3, r2]	@ movhi
	bcc	.L4468
.L4479:
	ldr	r0, .L4644+12
	mov	r1, #8
	bl	_list_get_gc_head_node
	movw	r2, #65535
	ldr	r9, .L4644+32
	cmp	r0, r2
	beq	.L4480
	ldr	r2, [r4, #1092]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #3
	movhi	r3, #0
	movls	r3, #1
	cmp	r7, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L4480
	mov	r2, #0
	mov	r1, #1
	strb	r2, [r9, #-3115]
	bl	gc_add_sblk
	cmp	r0, #0
	bne	.L4643
.L4480:
	ldr	r0, .L4644+16
	mov	r1, #0
	bl	_list_get_gc_head_node
	movw	r3, #65535
	ldr	r9, .L4644+32
	cmp	r0, r3
	mov	fp, r0
	bne	.L4481
.L4486:
	cmp	r8, #1
	bhi	.L4482
	b	.L4483
.L4481:
	movw	r1, #1080
	ldr	r3, [r9, #-132]
	ldrh	r1, [r4, r1]
	mov	ip, r0, asl #1
	add	r3, r3, #1
	ldr	r2, .L4644+60
	str	r3, [r9, #-132]
	cmp	r3, r1, lsr #4
	bls	.L4484
	ldr	r3, [r2, #1092]
	mov	r1, #0
	str	r1, [r9, #-132]
	ldrh	lr, [r3, ip]
	movw	r3, #2810
	ldrh	r3, [r2, r3]
	cmp	lr, r3
	bcs	.L4484
	mov	r3, #1
	str	ip, [sp, #32]
	mov	r2, r3
	strb	r3, [r9, #-3115]
	str	r3, [sp, #28]
	bl	gc_add_sblk
	cmp	r0, #0
	ldr	r3, [sp, #28]
	ldr	ip, [sp, #32]
	bne	.L4642
.L4484:
	ldr	r2, [r4, #1092]
	ldr	r9, .L4644+32
	ldr	r3, .L4644+60
	ldrh	r1, [r2, ip]
	sub	r2, r9, #3088
	ldrh	r2, [r2, #-8]
	cmp	r1, r2, lsr #1
	bhi	.L4485
	mov	r0, fp
	mov	r1, #1
	mov	r2, #0
	bl	gc_add_sblk
.L4643:
	mov	r3, #1
.L4642:
	strb	r3, [r9, #-136]
	b	.L4468
.L4485:
	movw	r2, #2794
	movw	r0, #2796
	ldrh	r0, [r3, r0]
	sub	r9, r9, #3072
	ldrh	r2, [r3, r2]
	add	r2, r2, r0
	ldrh	r0, [r9, #-14]
	cmp	r2, r0, asl #1
	ble	.L4486
	movw	r2, #2810
	ldrh	r3, [r3, r2]
	cmp	r3, r1
	bcc	.L4483
	b	.L4486
.L4482:
	ldr	r9, .L4644+32
	cmp	r8, #16
	mov	r0, #1
	strb	r0, [r5, #-3115]
	sub	r8, r9, #3072
	bls	.L4487
	movw	r3, #2798
	ldrh	r2, [r4, r3]
	ldrh	r3, [r8, #-12]
	cmp	r2, r3
	bhi	.L4487
	mov	r1, r0
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	ldreqb	r0, [r9, #-3115]	@ zero_extendqisi2
	bne	.L4488
	b	.L4635
.L4487:
	mov	r0, #1
	mov	r1, #2
	mov	r2, r0
	bl	gc_search_src_blk
	uxth	r0, r0
	cmp	r0, #0
	bne	.L4488
	ldrb	r0, [r5, #-3115]	@ zero_extendqisi2
.L4635:
	mov	r1, #3
	mov	r2, #4
	bl	gc_search_src_blk
	uxth	r0, r0
.L4488:
	ldrh	r3, [r8, #-14]
	cmp	r7, r10, lsr #1
	movw	r2, #2808
	movhi	r3, r3, lsr #2
	movls	r3, r3, lsr #1
	strh	r3, [r4, r2]	@ movhi
	b	.L4473
.L4483:
	cmp	r7, r10
	ldr	r8, .L4644
	bcs	.L4490
	sub	r0, r8, #20
	mov	r1, #4
	mov	r9, #0
	strb	r9, [r5, #-3115]
	bl	_list_get_gc_head_node
	movw	r3, #65535
	cmp	r0, r3
	beq	.L4490
	ldr	r3, [r4, #1092]
	mov	r0, r0, asl #1
	ldrh	r1, [r8, #-24]
	ldr	r7, .L4644+60
	ldrh	r2, [r3, r0]
	ldrb	r3, [r8, #-51]	@ zero_extendqisi2
	mul	r3, r3, r1
	cmp	r2, r3, asr #1
	ldrgth	r3, [r8, #-14]
	movwgt	r2, #2808
	movgt	r3, r3, lsr #2
	strgth	r3, [r7, r2]	@ movhi
	bgt	.L4468
.L4492:
	mov	r2, #4
	mov	r0, r9
	mov	r1, #3
	bl	gc_search_src_blk
	ldrh	r3, [r8, #-14]
	movw	r2, #2808
	mov	r3, r3, lsr #1
	strh	r3, [r7, r2]	@ movhi
	uxth	r0, r0
	b	.L4473
.L4490:
	ldrh	r3, [r8, #-14]
	movw	r2, #2808
	mov	r3, r3, lsr #2
	strh	r3, [r4, r2]	@ movhi
	b	.L4468
.L4478:
	sub	r9, r9, #3072
	ldrh	r1, [r9, #-14]
	mov	r1, r1, lsr #2
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, .L4644+56
	movw	r3, #2106
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	moveq	r6, #0
	b	.L4468
.L4473:
	cmp	r0, #0
	beq	.L4468
.L4543:
	mov	r3, #1
	strb	r3, [r5, #-136]
	b	.L4468
.L4424:
	movw	r6, #2828
	movw	r3, #65535
	ldrh	r2, [r4, r6]
	cmp	r2, r3
	bne	.L4493
	bl	gc_get_src_blk
	ldr	r3, .L4644+60
	strh	r0, [r3, r6]	@ movhi
.L4493:
	movw	r3, #2828
	movw	r0, #65535
	ldrh	r1, [r4, r3]
	ldr	r2, .L4644+60
	cmp	r1, r0
	beq	.L4494
	movw	r0, #1080
	ldrh	r0, [r2, r0]
	cmp	r0, r1
	mvnls	r1, #0
	strlsh	r1, [r2, r3]	@ movhi
.L4494:
	movw	r3, #2828
	movw	r1, #65535
	ldrh	r3, [r4, r3]
	ldr	r2, .L4644+60
	cmp	r3, r1
	beq	.L4641
	ldr	r0, .L4644+56
	ldr	r1, [r2, #1084]
	ldrh	ip, [r0, #52]
	add	r1, r1, r3, asl #2
	cmp	ip, #0
	addne	r2, r2, #2880
	movne	r0, #0
	beq	.L4496
.L4497:
	uxth	lr, r0
	cmp	lr, ip
	bcs	.L4496
	ldrh	lr, [r2, #2]!
	add	r0, r0, #1
	cmp	lr, r3
	bne	.L4497
.L4501:
	movw	r3, #2828
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	b	.L4545
.L4496:
	ldrb	r2, [r1, #2]	@ zero_extendqisi2
	tst	r2, #192
	and	r2, r2, #224
	moveq	r1, #1
	movne	r1, #0
	cmp	r2, #224
	movne	r2, r1
	orreq	r2, r1, #1
	cmp	r2, #0
	beq	.L4499
	ldr	r2, [r4, #1092]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4501
	ldr	r1, .L4644+64
	movw	r2, #3306
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
	b	.L4501
.L4499:
	mov	r3, #2
	b	.L4640
.L4425:
	bl	gc_scan_src_blk
	cmn	r0, #1
	moveq	r3, #3
	beq	.L4640
	movw	r3, #2828
	ldr	r2, .L4644+56
	ldrh	r3, [r4, r3]
	movw	r1, #65535
	cmp	r3, r1
	mov	r6, r2
	beq	.L4443
	ldrh	r1, [r2, #20]
	cmp	r1, #0
	movne	r3, #4
	strneb	r3, [r5, #-136]
	movne	r3, #0
	strneh	r3, [r2, #22]	@ movhi
	bne	.L4545
.L4503:
	mov	r2, #1
	strb	r2, [r5, #-136]
	ldr	r2, .L4644+60
	mov	r3, r3, asl #1
	ldr	r2, [r2, #1092]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4504
	ldr	r1, .L4644+64
	movw	r2, #3336
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
.L4504:
	movw	r5, #2828
	ldr	r7, .L4644+56
	ldrh	r0, [r4, r5]
	bl	ftl_free_sblk
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #1092]
	mov	r5, #0
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r6, #26]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #8
	strlsh	r3, [r7, #26]	@ movhi
	bls	.L4501
	strh	r5, [r7, #26]	@ movhi
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	r0, r5
	bl	ftl_info_flush
	b	.L4501
.L4426:
	ldr	r6, .L4644+56
.L4603:
	bl	gc_scan_src_blk_one_page
	ldr	r3, .L4644+20
	ldrh	r1, [r6, #2]
	ldr	r0, .L4644+60
	ldrh	r3, [r3]
	ldr	r2, .L4644+32
	cmp	r1, r3
	ldr	r5, .L4644+56
	bcs	.L4507
	cmp	r7, #7
	bls	.L4603
	b	.L4545
.L4507:
	ldrh	r3, [r5, #20]
	ldrh	r1, [r5]
	cmp	r3, #0
	beq	.L4508
	ldr	r0, [r0, #1092]
	mov	ip, #4
	strb	ip, [r2, #-136]
	mov	r2, #0
	strh	r2, [r5, #22]	@ movhi
	mov	r2, r1, asl #1
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	beq	.L4509
	ldr	r0, .L4644+4
	ldr	r0, [r0]
	tst	r0, #1024
	beq	.L4509
	ldr	r0, .L4644+24
	bl	printk
.L4509:
	movw	r3, #2828
	ldr	r2, [r4, #1092]
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r5, #20]
	cmp	r2, r3
	beq	.L4510
	ldr	r1, .L4644+64
	movw	r2, #3379
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
.L4510:
	movw	r3, #2828
	ldrh	r1, [r5, #20]
	ldrh	r3, [r4, r3]
	ldr	r2, [r4, #1092]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	b	.L4545
.L4508:
	mov	r3, #1
	strb	r3, [r2, #-136]
	ldr	r3, .L4644+4
	ldr	r6, [r0, #1084]
	ldr	r3, [r3]
	add	r6, r6, r1, asl #2
	tst	r3, #256
	beq	.L4511
	ldrb	r2, [r6, #2]	@ zero_extendqisi2
	ldr	r0, .L4644+28
	mov	r2, r2, lsr #5
	bl	printk
.L4511:
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	tst	r3, #192
	and	r3, r3, #224
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #224
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4512
	ldr	r1, .L4644+64
	movw	r2, #3389
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
.L4512:
	movw	r7, #2828
	ldr	r6, .L4644+56
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
	bls	.L4545
	b	.L4639
.L4645:
	.align	2
.L4644:
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
	.word	.LANCHOR1+2632
	.word	.LC0
.L4427:
	cmp	fp, #0
	bne	.L4514
	movw	r3, #2808
	ldrh	r3, [r4, r3]
	cmp	r3, r7
	bcc	.L4545
.L4514:
	ldrh	r2, [r6, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4515
	ldrb	r8, [r5, #-3115]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L4515
	ldr	r9, .L4644+56
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
	beq	.L4518
	ldr	r8, [r4, #1084]
	add	r8, r8, r0, asl #2
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	tst	r3, #224
	beq	.L4519
	ldr	r1, .L4644+64
	movw	r2, #3423
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
.L4519:
	ldrb	r3, [r8, #2]	@ zero_extendqisi2
	and	r3, r3, #15
	orr	r3, r3, #176
	strb	r3, [r8, #2]
.L4544:
	mov	r0, r7
	mov	r1, #1
	bl	ftl_erase_sblk
	mov	r3, #5
	add	r1, r6, #96
	strb	r3, [r6, #84]
	mov	r0, r7
	ldr	r10, .L4644+52
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
	ldr	r0, [r5, #-124]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r2, [r10, #-26]
	mov	r1, #255
	ldr	r0, [r5, #-120]
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
	ldr	r3, [r4, #1096]
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
	ldr	r3, .L4644+40
	str	r5, [r3, #916]
	bl	ftl_info_flush
	b	.L4545
.L4515:
	cmp	fp, #1
	ldr	r10, .L4644+56
	ldr	r9, .L4644+36
	movne	r8, #1
	moveq	r8, #4
	cmp	r7, #15
	addls	r8, r8, #4
.L4522:
	sub	r8, r8, #1
	uxtb	r8, r8
	cmp	r8, #255
	beq	.L4545
	bl	gc_do_copy_back
	ldrb	r3, [r5, #-3115]	@ zero_extendqisi2
	ldr	fp, .L4644+32
	cmp	r3, #0
	ldr	r7, .L4644+56
	bne	.L4523
	ldrb	r3, [r4, #2773]	@ zero_extendqisi2
	cmp	r3, #3
	bhi	.L4524
	bl	ftl_write_commit
.L4524:
	ldrh	r2, [r10, #22]
	ldrh	r3, [r10, #20]
	cmp	r2, r3
	bcc	.L4522
	mov	r3, #1
	strb	r3, [r5, #-136]
	bl	ftl_write_commit
	bl	ftl_flush
	ldr	r3, .L4644+56
	ldr	r2, .L4644+60
	ldrh	r3, [r3]
	ldr	r2, [r2, #1092]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L4526
	ldr	r1, .L4644+64
	movw	r2, #3507
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
.L4526:
	movw	r3, #2828
	ldr	r1, [r4, #1092]
	ldrh	r0, [r4, r3]
	mov	r2, r0, asl #1
	ldrh	ip, [r1, r2]
	cmp	ip, #0
	bne	.L4527
	strh	ip, [r1, r2]	@ movhi
	ldr	r2, .L4644+60
	ldrh	r0, [r2, r3]
	bl	ftl_free_sblk
	b	.L4501
.L4527:
	mov	r1, #0
	mov	r2, #1
	bl	gc_add_sblk
	b	.L4501
.L4523:
	ldrh	r3, [r9]
	cmp	r3, #0
	beq	.L4528
	ldr	r3, .L4644+36
	mov	r2, #0
	ldr	r8, .L4644+40
	strh	r2, [r3]	@ movhi
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	r0, [r8, #916]
	cmn	r0, #1
	beq	.L4529
	ldrb	r3, [fp, #-2534]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4530
	ldrb	r3, [fp, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4531
.L4530:
	ldr	r3, [r4, #2804]
	ldr	r9, .L4644+60
	ldr	r2, [r3, #156]
	ldr	r3, .L4644+44
	cmp	r2, r3
	bne	.L4531
	ldr	r10, .L4644+48
	ldrb	r3, [r9, #1153]	@ zero_extendqisi2
	ldrb	r1, [r5, #-3130]	@ zero_extendqisi2
	ldrh	r2, [r10, #-12]
	rsb	r3, r3, #24
	rsb	r3, r2, r3
	mov	r2, r0, lsr r2
	mvn	r0, #0
	bic	r0, r2, r0, asl r3
	bl	__aeabi_uidiv
	ldr	r6, [r9, #1084]
	mov	ip, r0, asl #2
	add	r1, r6, ip
	ldrb	r3, [r1, #2]	@ zero_extendqisi2
	tst	r3, #8
	beq	.L4529
	movw	r2, #2788
	ldrh	lr, [r10, #34]
	ldrh	r2, [r9, r2]
	add	r2, r2, #8
	cmp	r2, lr
	bge	.L4529
	add	r9, r9, #1088
	bfc	r3, #3, #2
	strb	r3, [r1, #2]
	ldr	r3, [r6, r0, asl #2]
	ldrh	r2, [r9]
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
	b	.L4529
.L4531:
	bl	ftl_mask_bad_block
.L4529:
	ldr	r3, [r4, #1096]
	mov	r6, #0
	str	r6, [r8, #916]
	strh	r6, [r7, #52]	@ movhi
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r0, [r4, #2836]
	ldr	r2, [r4, #1096]
	mvn	r3, #0
	ldr	r1, [r4, #2804]
	cmp	r0, r6
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	beq	.L4532
	bl	zbuf_free
.L4532:
	str	r6, [r4, #2836]
	bl	flt_sys_flush
	ldr	r1, .L4644+64
	movw	r2, #3567
	ldr	r0, .L4644+68
	strb	r6, [r5, #-136]
	bl	printk
	bl	dump_stack
	b	.L4545
.L4528:
	ldrh	r3, [r6, #86]
	ldrh	r2, [r10, #22]
	cmp	r3, #1
	ldrh	r3, [r10, #20]
	bls	.L4533
	cmp	r2, r3
	bcc	.L4522
	mov	r3, #1
	strb	r3, [fp, #-136]
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
	b	.L4545
.L4533:
	cmp	r2, r3
	mov	r1, #5
	strb	r1, [fp, #-136]
	bcc	.L4534
	ldrh	r3, [r7, #52]
	add	r2, r3, #1
	strh	r2, [r7, #52]	@ movhi
	ldrh	r2, [r7]
	add	r3, r7, r3, asl #1
	strh	r2, [r3, #54]	@ movhi
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L4534:
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldrh	r2, [r6, #80]
	ldr	r3, [r4, #1096]
	strh	r2, [r3, #128]	@ movhi
	bl	pm_flush
	bl	ftl_ext_info_flush
	ldr	r1, .L4644+52
	ldrb	r0, [r4, #1160]	@ zero_extendqisi2
	mov	r3, #0
	strh	r3, [r7, #12]	@ movhi
	ldrh	r3, [r1, #-8]
	cmp	r0, #0
	ldr	r2, .L4644+56
	ldrneh	r1, [r1, #-26]
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r5, #-3124]	@ zero_extendqisi2
	strneh	r1, [r2, #14]	@ movhi
	movne	r1, #1
	strh	r3, [r7, #16]	@ movhi
	strneh	r1, [r2, #16]	@ movhi
	cmp	r3, #2
	bne	.L4537
	ldrh	r3, [r7, #14]
	mov	r3, r3, asl #1
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r5, #-3122]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L4644+56
	moveq	r2, #1
	streqh	r2, [r3, #16]	@ movhi
.L4537:
	mov	r3, #0
	strh	r3, [r7, #18]	@ movhi
	b	.L4545
.L4546:
	ldr	r8, .L4644+56
	mov	r6, #0
.L4428:
	bl	gc_check_data_one_wl
	subs	r10, r0, #0
	beq	.L4540
	ldr	r3, .L4644+56
	mov	r6, #0
	strh	r6, [r3, #52]	@ movhi
	ldr	r3, [r4, #1096]
	ldrh	r0, [r3, #80]
	bl	ftl_free_sblk
	ldr	r2, [r4, #1096]
	ldr	r1, [r4, #2804]
	mvn	r3, #0
	ldr	r0, [r4, #2836]
	strh	r3, [r2, #80]	@ movhi
	strh	r3, [r1, #126]	@ movhi
	strh	r3, [r2, #130]	@ movhi
	bl	zbuf_free
	str	r6, [r4, #2836]
	strb	r6, [r5, #-136]
.L4639:
	bl	flt_sys_flush
	b	.L4545
.L4540:
	ldrh	r2, [r8, #12]
	ldrh	r3, [r8, #14]
	ldr	r9, .L4644+60
	cmp	r2, r3
	bcc	.L4541
	ldr	r0, [r9, #2836]
	mov	r3, #6
	strb	r3, [r5, #-136]
	bl	zbuf_free
	str	r10, [r9, #2836]
	b	.L4545
.L4541:
	cmp	r7, #15
	bls	.L4428
	cmp	fp, #1
	bne	.L4545
	add	r6, r6, #1
	uxtb	r6, r6
	cmp	r6, #4
	bls	.L4428
	b	.L4545
.L4429:
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
.L4641:
	mov	r3, #0
.L4640:
	strb	r3, [r5, #-136]
.L4545:
	mov	r0, #16
	b	.L4630
.L4468:
	mov	r0, r6
	b	.L4630
.L4518:
	ldr	r1, .L4644+64
	movw	r2, #3430
	ldr	r0, .L4644+68
	bl	printk
	bl	dump_stack
	b	.L4544
.L4630:
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
	ldr	r5, .L4730
	mvn	r3, #0
	ldr	r4, .L4730+4
	mov	r6, #0
	ldr	r1, .L4730+8
	ldr	r0, .L4730+12
	strb	r3, [r4, #2824]
	strb	r3, [r5, #-2528]
	strb	r3, [r5, #-80]
	str	r3, [r5]
	strb	r6, [r4, #2800]
	strb	r6, [r5, #-2527]
	strb	r6, [r4, #2801]
	bl	printk
	ldr	r8, .L4730+16
	sub	r1, r5, #3104
	ldrb	r3, [r8, #16]	@ zero_extendqisi2
	ldrb	fp, [r8, #13]	@ zero_extendqisi2
	strb	fp, [r5, #-2538]
	mov	r9, fp, asl #9
	uxth	r9, r9
	str	r3, [sp, #4]
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	strb	r3, [r5, #-3124]
	ldrh	r3, [r8, #14]
	strh	r3, [r1, #-10]	@ movhi
	mov	r0, r3
	ldr	r1, [sp, #4]
	str	r3, [sp, #12]
	bl	__aeabi_idiv
	sub	r1, r5, #3088
	ldrh	r10, [r8, #18]
	ldrh	ip, [r4, #2]
	strh	r9, [r5, #-14]	@ movhi
	str	r0, [sp, #8]
	ldrh	r3, [sp, #8]
	ldrb	r0, [r8, #17]	@ zero_extendqisi2
	strh	r3, [r1, #-8]	@ movhi
	ldrb	r1, [r4, #1109]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	strb	r0, [r5, #-3130]
	strb	r1, [r5, #-3064]
	smulbb	r1, r0, r1
	mov	r0, #1
	uxtb	r7, r1
	movw	r1, #1080
	strh	r10, [r4, r1]	@ movhi
	sub	r1, r5, #3056
	strb	r7, [r5, #-3123]
	strh	ip, [r1, #-10]	@ movhi
.L4647:
	cmp	r0, ip
	uxth	r1, r6
	add	r6, r6, #1
	movls	r0, r0, asl #1
	bls	.L4647
.L4728:
	mul	r2, r3, fp
	ldr	r0, .L4730+20
	sub	r1, r1, #1
	strh	r1, [r0, #-12]	@ movhi
	mov	r0, #0
	mov	r1, #1
	mul	r6, r10, r2
	mov	ip, r6, lsr #21
.L4649:
	cmp	r1, ip
	uxth	lr, r0
	add	r0, r0, #1
	movls	r1, r1, asl #1
	bls	.L4649
.L4729:
	ldr	r1, .L4730+24
	sub	lr, lr, #1
	mov	r0, #1
	str	r2, [sp, #20]
	mul	r6, r7, r6
	uxth	lr, lr
	mul	r1, r1, r7
	mov	lr, r0, asl lr
	str	r3, [sp, #16]
	mov	r10, r10, lsr #4
	str	r6, [r4, #2780]
	ldr	r6, .L4730
	mul	r8, lr, r1
	mov	r1, fp
	sub	fp, r6, #3072
	add	ip, r8, #24576
	str	ip, [r6, #4]
	str	ip, [sp, #12]
	mov	r0, ip
	str	r8, [r4, #1032]
	bl	__aeabi_uidiv
	sub	r1, r9, #1
	str	r0, [r4, #2784]
	add	r0, r1, r0, asl #2
	mov	r1, r9
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	strh	r0, [r6, #8]	@ movhi
	uxth	r0, r0
	mul	r1, r7, r3
	mov	r0, r0, asl #4
	bl	__aeabi_idiv
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #12]
	strh	r0, [r6, #-168]	@ movhi
	mul	r1, r7, r2
	mov	r0, ip
	sub	r1, r1, #1
	bl	__aeabi_uidiv
	cmp	r10, #79
	ldr	lr, .L4730+4
	mov	ip, #2000
	movls	r1, #80
	strh	r10, [fp, #-14]	@ movhi
	add	r10, lr, #1088
	add	r0, r0, #8
	ldr	r2, [sp, #4]
	strh	r0, [fp, #-12]	@ movhi
	ldr	r0, .L4730
	strlsh	r1, [fp, #-14]	@ movhi
	cmp	r2, #2
	sub	r1, r0, #2528
	ldr	r3, [sp, #16]
	strh	ip, [r1, #-2]	@ movhi
	mov	r2, r1
	mov	ip, #50
	strh	ip, [r1, #-4]	@ movhi
	mov	ip, #256
	strh	ip, [r6, #-172]	@ movhi
	mov	ip, #48
	strh	ip, [r6, #-170]	@ movhi
	mov	ip, #32
	strh	ip, [r10]	@ movhi
	beq	.L4652
	ldrb	r10, [r0, #-3116]	@ zero_extendqisi2
	cmp	r10, #0
	beq	.L4653
.L4652:
	mov	r0, #150
	strh	r0, [r2, #-4]	@ movhi
	mov	r0, #64
	strh	r0, [r6, #-170]	@ movhi
	ldr	r0, .L4730+28
	mov	lr, #12
	ldr	r1, .L4730
	strh	lr, [r0]	@ movhi
	sub	ip, r1, #2528
	ldrb	lr, [r4]	@ zero_extendqisi2
	cmp	lr, #0
	moveq	lr, #4
	streqh	lr, [r0]	@ movhi
	moveq	r0, #600
	streqh	r0, [ip, #-2]	@ movhi
	moveq	r0, #128
	streqh	r0, [r1, #-172]	@ movhi
	ldrb	r1, [r4, #1161]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r1, #200
	strneh	r1, [r2, #-4]	@ movhi
	movne	r1, #2000
	strneh	r1, [r2, #-2]	@ movhi
	b	.L4656
.L4653:
	ldrb	r2, [lr, #1160]	@ zero_extendqisi2
	cmp	r2, #0
	strneh	ip, [r1, #-4]	@ movhi
	strneh	ip, [r0, #-170]	@ movhi
	movne	r2, #1200
	strneh	r2, [r1, #-2]	@ movhi
.L4656:
	mul	r3, r7, r3
	ldr	r7, .L4730+16
	mov	r2, #0
	str	r2, [r4, #2820]
	mov	r2, #1
	strb	r2, [r6, #-11]
	cmp	r9, r3, asl #2
	ldrlt	r3, .L4730
	movlt	r2, #2
	strltb	r2, [r3, #-11]
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4659
	ldr	r0, .L4730+32
	mov	r1, r8
	bl	printk
.L4659:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4660
	ldr	r0, .L4730+36
	ldr	r1, [r4, #2780]
	bl	printk
.L4660:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4661
	ldr	r0, .L4730+40
	ldr	r1, [r4, #2784]
	bl	printk
.L4661:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4662
	ldr	r0, .L4730+44
	ldr	r1, [r6, #4]
	bl	printk
.L4662:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4663
	ldr	r0, .L4730+48
	ldrh	r1, [r6, #8]
	bl	printk
.L4663:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4664
	ldr	r0, .L4730+52
	ldrh	r1, [r6, #-14]
	bl	printk
.L4664:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4665
	ldr	r0, .L4730+56
	ldrh	r1, [r6, #-168]
	bl	printk
.L4665:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4666
	ldr	r0, .L4730+60
	ldrh	r1, [fp, #-14]
	bl	printk
.L4666:
	ldr	r3, [r7]
	tst	r3, #4096
	beq	.L4667
	ldr	r0, .L4730+64
	ldrh	r1, [fp, #-12]
	bl	printk
.L4667:
	bl	zbuf_init
	mov	r0, #16384
	bl	ftl_malloc
	movw	r9, #1080
	ldr	r8, .L4730+68
	str	r0, [r5, #-52]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #-44]
	mov	r0, #16384
	bl	ftl_malloc
	str	r0, [r5, #12]
	mov	r0, #256
	bl	ftl_dma32_malloc
	str	r0, [r5, #-68]
	mov	r0, #256
	bl	ftl_dma32_malloc
	ldrh	r3, [r4, r9]
	str	r0, [r5, #-40]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_dma32_malloc
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	str	r0, [r4, #1036]
	ldrh	r0, [r8, #-8]
	mul	r0, r3, r0
	mov	r0, r0, asl #2
	bl	ftl_dma32_malloc
	ldrb	r3, [r5, #-3123]	@ zero_extendqisi2
	ldrh	r1, [r8, #-8]
	ldr	r2, [r5, #-44]
	mul	r1, r3, r1
	str	r2, [r5, #-2548]
	add	r3, r2, r1, asl #3
	str	r3, [r4, #1092]
	str	r0, [r5, #-2544]
	ldrh	r5, [r4, r9]
	ldr	r0, [r7]
	mov	r5, r5, lsr #1
	tst	r0, #4096
	add	r1, r5, r1, asl #1
	add	r1, r2, r1, asl #2
	str	r1, [r4, #1096]
	beq	.L4668
	ldr	r0, .L4730+72
	bl	printk
.L4668:
	movw	r3, #1080
	ldrh	r5, [r8, #-8]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r6, #-3123]	@ zero_extendqisi2
	mul	r5, r3, r5
	ldrh	r3, [r6, #8]
	mov	r3, r3, asl #2
	add	r8, r3, r2, asl #2
	ldr	r3, [r7]
	add	r5, r2, r5, asl #2
	add	r8, r8, #704
	tst	r3, #4096
	mov	r5, r5, asl #1
	add	r5, r5, #632
	beq	.L4669
	ldr	r3, .L4730
	mov	r1, r5
	ldr	r0, .L4730+76
	mov	r2, r8
	ldrh	r3, [r3, #-14]
	bl	printk
.L4669:
	ldrh	r3, [r6, #-14]
	cmp	r8, r3
	cmpls	r5, r3
	movhi	r8, #1
	movls	r8, #0
	bls	.L4670
.L4726:
	b	.L4726
.L4670:
	bl	sblk_init
	bl	gc_init
	bl	ftl_info_blk_init
	cmn	r0, #1
	beq	.L4672
	bl	ftl_ext_info_init
	mov	r0, #1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	r0, [r4, #1096]
	mov	r1, r8
	add	r0, r0, #16
	bl	ftl_open_sblk_recovery
	ldr	r1, [r4, #1096]
	add	r0, r1, #48
	add	r1, r1, #16
	bl	ftl_open_sblk_recovery
	ldr	r2, [r4, #2804]
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
	ldr	r3, .L4730
	ldrb	r3, [r3, #-47]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, .L4730+4
	ldrne	r2, [r3, #2804]
	ldrne	r3, [r2, #68]
	addne	r3, r3, #1
	strne	r3, [r2, #68]
	bl	ftl_ext_info_flush
	mov	r0, #0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	ldr	r3, [r4, #1096]
	ldr	r2, .L4730+4
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4678
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #7
	ble	.L4678
.L4676:
	mov	r0, #0
	b	.L4672
.L4678:
	ldr	r6, .L4730+80
	mov	r5, #16384
.L4709:
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	ldr	r3, [r4, #1096]
	ldrh	r2, [r3, #124]
	cmp	r2, #0
	bne	.L4675
	ldrh	r2, [r3, #80]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L4675
	ldr	r2, .L4730+84
	ldrh	r3, [r6]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4676
.L4675:
	subs	r5, r5, #1
	bne	.L4709
	b	.L4676
.L4672:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4731:
	.align	2
.L4730:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LC2
	.word	.LC1
	.word	.LANCHOR2
	.word	.LANCHOR3-3120
	.word	1892352
	.word	.LANCHOR0+1088
	.word	.LC303
	.word	.LC304
	.word	.LC305
	.word	.LC306
	.word	.LC307
	.word	.LC308
	.word	.LC309
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
	ldr	r3, .L4748
	mov	r5, #0
	ldr	r4, .L4748+4
	ldr	r3, [r3]
	str	r5, [r4, #-156]
	strb	r5, [r4, #16]
	str	r3, [r4, #-160]
	str	r5, [r4, #-144]
	bl	ftl_dma32_malloc
	cmp	r0, r5
	str	r0, [r4, #-152]
	bne	.L4733
.L4735:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4733:
	mov	r0, #2048
	bl	ftl_dma32_malloc
	add	r1, r4, #24
	str	r5, [r4, #-148]
	str	r5, [r4, #24]
	str	r0, [r4, #20]
	sub	r0, r4, #148
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #-148]
	cmp	r3, #0
	beq	.L4735
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r4, #20]
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	r3, [r4, #-152]
	ldr	r2, .L4748+8
	ldr	r0, [r4, #-148]
	str	r2, [r3, #40]
	ldr	r2, .L4748+12
	str	r2, [r3, #32]
	ldr	r2, .L4748+16
	str	r2, [r3, #44]
	ldr	r2, .L4748+20
	str	r2, [r3, #48]
	ldr	r2, .L4748+24
	str	r2, [r3, #36]
	ldr	r2, .L4748+28
	str	r2, [r3, #56]
	ldr	r2, .L4748+32
	str	r2, [r3, #20]
	ldr	r2, .L4748+36
	str	r2, [r3, #24]
	ldr	r2, .L4748+40
	str	r2, [r3, #12]
	ldr	r2, .L4748+44
	str	r2, [r3, #16]
	ldr	r2, .L4748+48
	str	r2, [r3, #4]
	ldr	r2, .L4748+52
	str	r2, [r3, #8]
	ldr	r2, .L4748+56
	str	r2, [r3, #60]
	ldr	r2, .L4748+60
	str	r2, [r3, #64]
	ldr	r2, .L4748+64
	str	r2, [r3, #52]
	ldr	r2, .L4748+68
	str	r2, [r3, #28]
	bl	nand_flash_init
	subs	r7, r0, #0
	bne	.L4736
	bl	zftl_init
	mov	r5, r0
	bl	zftl_proc_debug_init
	mov	r3, #1
	strb	r3, [r4, #16]
	b	.L4737
.L4736:
	ldr	r3, [r4, #-152]
	mov	r6, r4
	ldr	r2, .L4748+72
	ldr	r0, [r4, #-148]
	ldr	r4, .L4748+76
	str	r2, [r3, #40]
	ldr	r2, .L4748+80
	str	r2, [r3, #32]
	ldr	r2, .L4748+84
	str	r2, [r3, #44]
	ldr	r2, .L4748+88
	str	r2, [r3, #48]
	ldr	r2, .L4748+92
	str	r2, [r3, #36]
	ldr	r2, .L4748+96
	str	r2, [r3, #56]
	ldr	r2, .L4748+100
	str	r2, [r3, #20]
	ldr	r2, .L4748+104
	str	r2, [r3, #24]
	ldr	r2, .L4748+108
	str	r2, [r3, #12]
	ldr	r2, .L4748+112
	str	r2, [r3, #16]
	ldr	r2, .L4748+116
	str	r2, [r3, #4]
	ldr	r2, .L4748+120
	str	r2, [r3, #8]
	ldr	r2, .L4748+124
	str	r2, [r3, #60]
	ldr	r2, .L4748+128
	str	r2, [r3, #64]
	ldr	r2, .L4748+132
	str	r2, [r3, #52]
	ldr	r2, .L4748+136
	str	r2, [r3, #28]
	bl	FlashInit
	cmn	r7, #2
	mov	r5, r0
	bne	.L4738
	ldr	r1, .L4748+140
	mov	r2, #32
	add	r0, r4, #4
	bl	ftl_memcpy
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	bl	flash_lsb_page_tbl_build
	ldrh	r3, [r4, #14]
	strh	r3, [r4, #30]	@ movhi
.L4738:
	ldr	r3, .L4748+144
	ldr	r2, [r3]
	ldr	r3, .L4748+148
	cmp	r2, r3
	ldr	r2, .L4748+152
	bne	.L4739
	ldr	r3, .L4748+156
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4740
.L4739:
	mov	r3, #0
	strb	r3, [r2, #1110]
	strb	r3, [r2]
.L4740:
	ldr	r1, .L4748+160
	mov	r0, #1
	ldrh	ip, [r4, #14]
	mov	r3, #0
	str	r3, [r6, #-92]
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1108]
	ldr	r1, .L4748+164
	ldrb	r1, [r1]	@ zero_extendqisi2
	strb	r1, [r2, #1197]
.L4741:
	cmp	r0, ip
	uxth	r1, r3
	add	r3, r3, #1
	movls	r0, r0, asl #1
	bls	.L4741
.L4747:
	ldr	r3, .L4748+168
	sub	r1, r1, #1
	strh	r1, [r3, #-12]	@ movhi
	mov	r3, #0
	cmp	r5, r3
	strb	r3, [r2, #1143]
	bne	.L4737
	bl	FtlInit
	mov	r5, r0
.L4737:
	mov	r1, r5
	ldr	r0, .L4748+172
	bl	printk
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4749:
	.align	2
.L4748:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L4785
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r4, r1
	mov	r5, r2
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4751
	ldr	r3, [r8]
	mov	r1, r6
	ldr	r0, .L4785+4
	mov	r2, r4
	str	r3, [sp]
	mov	r3, r5
	bl	printk
.L4751:
	cmp	r6, #0
	ldreq	r3, .L4785+8
	moveq	r6, #24576
	ldreq	r3, [r3, #1032]
	beq	.L4753
.L4752:
	cmp	r6, #3
	bhi	.L4771
	mov	r6, r6, asl #13
	mov	r3, #8192
.L4753:
	cmp	r5, r3
	cmpls	r4, r3
	movcs	fp, #1
	movcc	fp, #0
	bcs	.L4771
	add	r1, r4, r5
	cmp	r1, r3
	bhi	.L4771
	ldr	r10, .L4785+12
	add	r4, r6, r4
	mov	r0, r4
	ldrb	r6, [r10, #-2538]	@ zero_extendqisi2
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	mov	r9, r0
	sub	r0, r5, #1
	add	r0, r0, r4
	mov	r7, r9
	bl	__aeabi_uidiv
	add	r3, r4, r5
	rsb	r6, r9, r0
	str	r10, [sp, #16]
	ldr	r10, .L4785+8
	add	r6, r6, #1
	str	r0, [sp, #12]
	str	fp, [sp, #20]
	str	r3, [sp, #24]
.L4755:
	cmp	r6, #0
	ldr	r2, .L4785+8
	beq	.L4783
	ldrb	r3, [r10, #2800]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4756
	ldrb	r3, [r10, #2773]	@ zero_extendqisi2
	cmp	r3, #2
	bhi	.L4756
	bl	ftl_write_commit
.L4756:
	mov	r0, #0
	bl	buf_alloc
	subs	fp, r0, #0
	bne	.L4757
	bl	ftl_write_commit
	b	.L4755
.L4757:
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	rsb	r0, r9, r7
	clz	r0, r0
	strb	r3, [fp, #41]
	mov	r0, r0, lsr #5
	ldr	r3, [sp, #16]
	ldrb	r2, [r3, #-2538]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	rsb	r1, r3, r7
	strb	r2, [fp, #40]
	clz	r1, r1
	mov	r1, r1, lsr #5
	orrs	r3, r1, r0
	beq	.L4760
	cmp	r0, #0
	beq	.L4761
	mov	r1, r2
	mov	r0, r4
	str	r2, [sp, #28]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #28]
	uxtb	r1, r1
	strb	r1, [fp, #41]
	rsb	r2, r1, r2
	uxtb	r2, r2
	cmp	r2, r5
	strhib	r5, [fp, #40]
	bhi	.L4760
	b	.L4782
.L4761:
	cmp	r1, #0
	beq	.L4760
	smulbb	r2, r7, r2
	ldr	r3, [sp, #24]
	rsb	r2, r2, r3
.L4782:
	strb	r2, [fp, #40]
.L4760:
	ldrb	r0, [fp, #41]	@ zero_extendqisi2
	sub	r6, r6, #1
	ldrb	r2, [fp, #40]	@ zero_extendqisi2
	ldr	r1, [fp, #4]
	mov	r2, r2, asl #9
	add	r0, r1, r0, asl #9
	mov	r1, r8
	bl	ftl_memcpy
	ldr	r1, [r10, #2804]
	str	r7, [fp, #20]
	add	r7, r7, #1
	ldr	r2, [r1, #8]
	add	r0, r2, #1
	str	r0, [r1, #8]
	str	r2, [fp, #16]
	mov	r0, fp
	bl	ftl_write_buf
	ldrb	r3, [fp, #40]	@ zero_extendqisi2
	add	r8, r8, r3, asl #9
	b	.L4755
.L4783:
	str	r2, [sp, #12]
	bl	ftl_write_commit
	mov	r0, r6
	mov	r1, #1
	bl	zftl_do_gc
	ldr	r2, [sp, #12]
	ldr	r3, [r2, #1096]
	ldrh	r3, [r3, #124]
	cmp	r3, #0
	bne	.L4765
	movw	r3, #2788
	movw	r1, #2792
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r1]
	add	r3, r3, r2
	cmp	r3, #11
	bgt	.L4766
.L4765:
	mov	r0, #0
	mov	r1, #1
	bl	zftl_do_gc
.L4766:
	ldr	r4, .L4785+16
.L4767:
	ldr	r2, .L4785+20
	ldrh	r3, [r4]
	ldrh	r2, [r2]
	add	r3, r3, r2
	cmp	r3, #7
	bgt	.L4784
	mov	r1, #1
	mov	r0, #0
	bl	zftl_do_gc
	mov	r0, #1
	mov	r1, r0
	bl	zftl_do_gc
	b	.L4767
.L4784:
	bl	timer_get_time
	ldr	r3, [sp, #16]
	str	r0, [r3, #-4]
	mov	r0, #0
	b	.L4754
.L4771:
	mvn	r0, #0
.L4754:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4786:
	.align	2
.L4785:
	.word	.LANCHOR2
	.word	.LC315
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+2788
	.word	.LANCHOR0+2792
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
	ldr	r6, .L4841
	.pad #36
	sub	sp, sp, #36
	ldr	r5, [r6, #1032]
	cmp	r1, r5
	cmpls	r0, r5
	movcs	r2, #1
	movcc	r2, #0
	bcs	.L4812
	add	r3, r0, r1
	cmp	r3, r5
	bhi	.L4812
	ldr	r3, .L4841+4
	add	r8, r0, #24576
	ldr	r9, .L4841+8
	ldr	r0, [r3]
	ldr	r5, [r9, #28]
	mov	r7, r9
	tst	r0, #4096
	str	r3, [sp, #16]
	add	r1, r1, r5
	str	r1, [r9, #28]
	beq	.L4792
	str	r2, [sp]
	mov	r3, r4
	ldr	r0, .L4841+12
	mov	r2, r8
	bl	printk
.L4792:
	ldr	r3, [r6, #2804]
	ldr	r10, [r3, #8]
	add	r2, r10, #1
	str	r2, [r3, #8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	r9, [r7, #-2538]	@ zero_extendqisi2
	mov	r0, r8
	mov	r1, r9
	bl	__aeabi_uidiv
	mls	fp, r9, r0, r8
	mov	r5, r0
	cmp	fp, #0
	beq	.L4793
	rsb	r9, fp, r9
	cmp	r9, r4
	movcs	r9, r4
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4794
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4794:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	uxth	r3, r9
	str	r3, [sp, #12]
	beq	.L4796
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4796
	ldr	r0, [r3, #4]
	mov	r1, #0
	str	r5, [r3, #20]
	strb	fp, [r3, #41]
	add	r0, r0, fp, asl #9
	strb	r9, [r3, #40]
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
.L4796:
	ldr	r3, [sp, #12]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L4793:
	cmp	r4, #0
	beq	.L4798
	bl	ftl_flush
.L4798:
	ldr	r9, .L4841+8
	mvn	r3, #0
	ldr	fp, .L4841+16
	str	r3, [sp, #28]
.L4799:
	ldrb	r3, [r7, #-2538]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L4840
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	beq	.L4800
	mov	r0, #0
	bl	buf_alloc
	subs	r3, r0, #0
	beq	.L4802
	ldrb	r2, [r9, #-2538]	@ zero_extendqisi2
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
	b	.L4839
.L4800:
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4802
	add	r1, sp, #28
	mov	r2, #1
	mov	r0, r5
	bl	pm_log2phys
	ldrb	r3, [r6, #1153]	@ zero_extendqisi2
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
.L4839:
	ldr	r2, [r6, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4802:
	ldrb	r3, [r7, #-2538]	@ zero_extendqisi2
	add	r5, r5, #1
	rsb	r4, r3, r4
	b	.L4799
.L4840:
	cmp	r4, #0
	beq	.L4806
	mov	r0, r5
	bl	lpa_hash_get_ppa
	cmn	r0, #1
	str	r0, [sp, #24]
	bne	.L4807
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	pm_log2phys
.L4807:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L4806
	mov	r0, #0
	bl	buf_alloc
	subs	r9, r0, #0
	beq	.L4806
	mov	r3, #0
	strb	r3, [r9, #41]
	ldrb	r3, [r7, #-2538]	@ zero_extendqisi2
	str	r5, [r9, #20]
	cmp	r4, r3
	strb	r4, [r9, #40]
	str	r10, [r9, #16]
	bcc	.L4809
	ldr	r1, .L4841+20
	movw	r2, #1496
	ldr	r0, .L4841+24
	bl	printk
	bl	dump_stack
.L4809:
	mov	r2, r4, asl #9
	mov	r1, #0
	ldr	r0, [r9, #4]
	bl	ftl_memset
	mov	r0, r9
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	r2, [r6, #2804]
	ldr	r3, [r2, #76]
	add	r3, r3, #1
	str	r3, [r2, #76]
.L4806:
	ldr	r1, [r7, #28]
	cmp	r1, #8192
	bls	.L4813
	ldr	r3, [sp, #16]
	ldr	r3, [r3]
	tst	r3, #4096
	beq	.L4810
	mov	r3, #0
	ldr	r0, .L4841+12
	str	r3, [sp]
	mov	r2, r8
	mov	r3, r4
	bl	printk
.L4810:
	mov	r4, #0
	str	r4, [r7, #28]
	bl	flt_sys_flush
	mov	r3, #1
	str	r3, [r6, #2820]
	b	.L4813
.L4812:
	mvn	r0, #0
	b	.L4791
.L4813:
	mov	r0, #0
.L4791:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4842:
	.align	2
.L4841:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC316
	.word	.LANCHOR3-3132
	.word	.LANCHOR1+2644
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
	ldr	r4, .L4849
	mov	r2, #4
	ldr	r0, .L4849+4
	mov	r6, #0
	ldr	r5, .L4849+8
	movw	r7, #65535
	ldr	r3, [r4, #2804]
	add	r1, r3, #704
	ldrh	r3, [r3, ip]
	bl	rknand_print_hex
	ldr	r1, [r4, #2804]
	ldr	r0, .L4849+12
	mov	r2, #2
	add	r1, r1, #416
	ldrh	r3, [r5, #-168]
	bl	rknand_print_hex
.L4844:
	ldrh	r2, [r5, #-168]
	uxth	r3, r6
	cmp	r2, r3
	bls	.L4848
	add	r3, r3, #208
	ldr	r2, [r4, #2804]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	cmp	r0, r7
	beq	.L4845
	mov	r1, #0
	bl	ftl_sblk_dump
.L4845:
	add	r6, r6, #1
	b	.L4844
.L4848:
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L4850:
	.align	2
.L4849:
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
	ldr	r5, .L4858
	.pad #20
	sub	sp, sp, #20
	ldrb	r2, [r5, #1110]	@ zero_extendqisi2
	ldr	r3, [r5, #1104]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	bne	.L4851
	sub	r2, r3, #5
	mov	r8, r0
	cmp	r3, #68
	cmpne	r2, #2
	mov	r9, r1
	bls	.L4853
	sub	r3, r3, #19
	tst	r3, #239
	bne	.L4851
.L4853:
	ldr	fp, .L4858
	ldr	r6, .L4858+4
	ldr	r7, .L4858+8
.L4855:
	ldr	r3, [r5, #1104]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L4851
	mov	ip, r4, asl #1
	add	r10, r4, r9
	ldr	r0, .L4858+12
	mov	r1, r4
	ldrh	r3, [r7, ip]
	mov	r2, r10
	str	ip, [sp, #12]
	bl	printk
	movw	r3, #65535
	ldr	ip, [sp, #12]
	ldrh	r2, [r7, ip]
	cmp	r2, r3
	bne	.L4851
	mov	r1, #255
	mov	r2, #16384
	ldr	r0, [r6, #-112]
	add	r4, r4, #1
	bl	ftl_memset
	ldr	r3, [fp, #1104]
	ldr	r2, [r6, #-112]
	mov	r0, r8
	mov	r1, r10
	uxth	r4, r4
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	mov	r3, r2
	bl	flash_prog_page
	b	.L4855
.L4851:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4859:
	.align	2
.L4858:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-2264
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
	ldr	ip, .L4940
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #156
	sub	sp, sp, #156
	ldr	r3, [ip, #1104]
	ldr	r6, [r1]
	ldr	r8, .L4940+4
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	r9, [r3, #10]
	ldr	r3, .L4940+8
	ldr	fp, .L4940+12
	ldr	r10, .L4940+16
	ldrh	r7, [r3, #30]
	cmp	r6, r8
	cmpne	r6, fp
	ldrb	r3, [ip]	@ zero_extendqisi2
	movne	r5, #1
	moveq	r5, #0
	cmp	r6, r10
	moveq	r5, #0
	andne	r5, r5, #1
	str	r3, [sp, #60]
	cmp	r5, #0
	ldrh	r3, [sp, #60]
	str	r3, [sp, #44]
	ldrb	r3, [ip, #1110]	@ zero_extendqisi2
	str	r3, [sp, #68]
	bne	.L4938
	cmp	r0, #15
	bls	.L4938
	mov	r5, r0
	mov	r0, #256000
	str	ip, [sp, #20]
	mov	r6, r2
	str	r1, [sp, #36]
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #24]
	beq	.L4938
	ldr	r3, .L4940+20
	ldr	ip, [sp, #20]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4937
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #0
	ldrb	r3, [ip, #1110]	@ zero_extendqisi2
	clzne	r3, r3
	movne	r3, r3, lsr #5
	bne	.L4937
	cmp	r3, #3
	streq	r3, [sp, #44]
	moveq	r3, #2
	beq	.L4937
	cmp	r3, #2
	ldr	r3, [sp, #44]
	moveq	r3, #2
	str	r3, [sp, #44]
	movne	r3, #0
	moveq	r3, #3
.L4937:
	add	r5, r5, #508
	str	r3, [sp, #20]
	add	r5, r5, #3
	mov	r5, r5, lsr #9
	cmp	r5, #8
	bls	.L4904
	cmp	r5, #500
	bhi	.L4867
	b	.L4866
.L4904:
	mov	r5, #8
.L4866:
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
	beq	.L4868
.L4867:
	ldr	r0, [sp, #24]
	bl	ftl_free
.L4938:
	mvn	r0, #0
	b	.L4862
.L4868:
	mov	r0, r10
	bl	zftl_flash_exit_slc_mode
	mul	r9, r9, r4
	ldr	r3, .L4940
	ldrb	r2, [r3, #1110]	@ zero_extendqisi2
	sub	r0, r9, #1
	add	r0, r0, r5
	mov	r1, r9
	strb	r2, [r3]
	bl	__aeabi_uidiv
	ldr	ip, .L4940+24
	movw	r2, #63871
	ldr	r3, [sp, #36]
	str	r0, [sp, #72]
	movw	r0, #4094
	add	r3, r3, #254976
	add	r3, r3, #512
.L4874:
	ldr	r1, [r3, #-4]!
	cmp	r1, #0
	bne	.L4869
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
	bne	.L4874
	b	.L4873
.L4869:
	ldr	r0, .L4940+28
	bl	printk
.L4873:
	mul	r3, r7, r4
	ldr	r0, .L4940+32
	mov	r1, r6
	mov	r2, #4
	ldr	r4, .L4940
	str	r3, [sp, #56]
	mov	r3, #5
	bl	rknand_print_hex
	ldr	r0, .L4940+36
	mov	r1, r5
	mov	r2, r5
	bl	printk
	sub	r3, r6, #4
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #40]
	str	r3, [sp, #28]
.L4899:
	ldr	r3, .L4940+20
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4875
	ldr	r3, [sp, #48]
	ldr	r1, [sp, #72]
	ldr	r2, [r3, #4]
	ldr	r3, [sp, #56]
	mul	r7, r3, r2
	ldr	r3, [sp, #28]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L4876
	ldr	r3, [sp, #48]
	ldr	r3, [r3]
	add	r3, r3, #1
	cmp	r2, r3
	bne	.L4876
	b	.L4877
.L4875:
	ldr	r3, [r4, #1040]
	ldr	r2, [sp, #28]
	add	r3, r3, r2
	ldrb	r7, [r3, #32]	@ zero_extendqisi2
	cmp	r7, #255
	beq	.L4877
	ldr	r3, [sp, #56]
	mul	r7, r3, r7
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4876
	ldr	r3, [sp, #20]
	and	r6, r2, #1
	cmp	r3, #3
	orreq	r6, r6, #1
	cmp	r6, #0
	ldrneb	r3, [sp, #44]	@ zero_extendqisi2
	strneb	r3, [r4]
	strneb	r3, [r4, #1110]
	bne	.L4876
	mov	r0, r6
	bl	zftl_flash_exit_slc_mode
	strb	r6, [r4]
	strb	r6, [r4, #1110]
.L4876:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	mov	r8, #0
	bl	ftl_memset
	ldr	r3, [r4, #1104]
	mov	r0, r7
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	ldrh	r9, [r3, #10]
	ldr	r3, .L4940+8
	mov	r1, r2
	mul	r9, r9, r2
	ldrh	r6, [r3, #30]
	ldrb	r3, [r4, #1200]	@ zero_extendqisi2
	strb	r8, [r4, #1200]
	mul	r6, r6, r2
	str	r2, [sp, #32]
	str	r3, [sp, #76]
	bl	__aeabi_uidiv
	mov	r1, r0
	mov	r0, r8
	bl	flash_erase_block
	cmp	r5, r9
	movls	r0, #1
	bls	.L4879
	mov	r0, r8
	add	r1, r7, r6
	bl	flash_erase_block
	mov	r0, #2
.L4879:
	ldr	r3, .L4940
	ldr	r2, [r3, #1104]
	ldrh	r3, [r2, #10]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	mov	r3, r3, asl #2
	mul	r0, r0, r3
	bl	__aeabi_idiv
	mov	r1, r6
	str	r0, [sp, #80]
	mov	r0, r7
	bl	__aeabi_uidivmod
	rsb	r3, r1, r7
	str	r1, [sp, #64]
	cmp	r7, r3
	str	r3, [sp, #52]
	bne	.L4907
	ldrb	r3, [r4, #1028]	@ zero_extendqisi2
	cmp	r3, #9
	bne	.L4907
	ldr	r3, .L4940+20
	mov	r1, #0
	mov	r2, #1024
	sub	r9, r5, #4
	ldr	r6, [r3, #-88]
	mov	r0, r6
	bl	ftl_memset
	ldr	r2, .L4940+40
	mov	r3, #12
	stmia	r6, {r2, r3}
	mov	r3, #0
	strb	r3, [r6, #16]
	str	r3, [r6, #12]
	ldrb	r3, [r4, #1110]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r4, #1104]
	ldrneb	r3, [r3, #29]	@ zero_extendqisi2
	strneb	r3, [r6, #16]
	mov	r3, #4
	strb	r3, [r6, #17]
	ldr	r3, [r4, #1104]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	bl	__aeabi_idiv
	mov	r3, #0
	mov	r1, #12
	strb	r3, [r6, #20]
	strh	r3, [r6, #22]	@ movhi
	ldr	r2, [sp, #32]
	strh	r0, [r6, #18]	@ movhi
	add	r0, r6, #12
	cmp	r2, #8
	movhi	r2, #70
	movls	r2, #16
	strb	r2, [r6, #21]
	bl	js_hash
	str	r0, [r6, #8]
	b	.L4880
.L4907:
	mov	r9, r5
	mov	r6, #0
.L4880:
	ldr	fp, [sp, #36]
	mov	r10, #0
.L4883:
	ldr	r3, [sp, #80]
	cmp	r10, r3
	bcs	.L4939
	ldr	r3, [sp, #64]
	ldrb	r2, [r4, #1110]	@ zero_extendqisi2
	add	r8, r10, r3
	ubfx	r8, r8, #2, #16
	cmp	r2, #0
	add	r0, r8, #1
	mov	r1, r0, asl #1
	add	r3, r4, r1
	ldrh	r3, [r3, #4]
	beq	.L4885
	ldrb	ip, [r4, #1]	@ zero_extendqisi2
	cmp	ip, #0
	moveq	r3, r0
	movne	r3, r1
.L4885:
	ldrb	r1, [r4, #1028]	@ zero_extendqisi2
	ldr	r0, .L4940
	cmp	r1, #9
	movw	r1, #61424
	str	r1, [sp, #92]
	mov	r1, r8, asl #1
	add	r0, r0, r1
	subne	r3, r3, #1
	ldrh	r0, [r0, #4]
	movne	r3, r3, asl #2
	cmp	r2, #0
	str	r3, [sp, #88]
	moveq	r8, r0
	beq	.L4889
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	cmp	r2, #0
	movne	r8, r1
.L4889:
	ldr	r2, [sp, #32]
	cmp	r6, #0
	mul	r0, r2, r8
	bne	.L4890
	ldr	r2, [sp, #52]
	str	fp, [sp]
	add	ip, r0, r2
	ldr	r2, [fp]
	ldr	r0, .L4940+44
	mov	r1, ip
	str	ip, [sp, #84]
	stmib	sp, {r2, r3}
	movw	r3, #61424
	mov	r2, r10
	str	r3, [sp, #12]
	mov	r3, r5
	bl	printk
	mov	r1, fp
	ldr	ip, [sp, #84]
	add	r2, sp, #88
	mov	r0, ip
	bl	fw_flash_page_prog.constprop.30
	ldrb	r3, [r4, #1110]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L4891
	ldr	r1, [sp, #32]
	ldr	r0, [sp, #52]
	bl	__aeabi_uidiv
	add	r2, r8, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, r6
	bl	id_block_prog_msb_ff_data
.L4891:
	add	r3, r9, #16
	add	fp, fp, #2048
	cmp	r10, r3
	bcc	.L4892
	add	r3, r9, #20
	cmp	r10, r3
	ldrcc	r3, [sp, #36]
	addcc	fp, r3, #2048
	b	.L4892
.L4890:
	ldr	r3, [sp, #52]
	mov	r1, r6
	add	r2, sp, #88
	add	r0, r0, r3
	bl	fw_flash_page_prog.constprop.30
.L4892:
	add	r10, r10, #4
	mov	r6, #0
	uxth	r10, r10
	b	.L4883
.L4939:
	ldr	r3, .L4940
	mov	r0, r7
	ldrb	r2, [sp, #76]	@ zero_extendqisi2
	mov	r1, r9
	strb	r2, [r3, #1200]
	ldr	r2, [sp, #24]
	bl	id_block_read_data.constprop.31
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L4894
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	mov	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #1110]
.L4894:
	mov	r9, r9, asl #7
	mov	r6, #0
.L4897:
	ldr	r3, [sp, #24]
	ldr	r2, [r3, r6, asl #2]
	ldr	r3, [sp, #36]
	ldr	r3, [r3, r6, asl #2]
	cmp	r2, r3
	beq	.L4895
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #24]
	bl	ftl_memset
	mov	r0, #0
	mov	r1, r7
	bl	flash_erase_block
	cmp	r6, r9
	bcs	.L4896
	b	.L4877
.L4895:
	add	r6, r6, #1
	cmp	r6, r9
	bne	.L4897
.L4896:
	ldr	r3, [sp, #40]
	add	r3, r3, #1
	str	r3, [sp, #40]
	cmp	r3, #5
	bhi	.L4898
.L4877:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
	ldr	r3, [sp, #48]
	add	r3, r3, #4
	str	r3, [sp, #48]
	ldr	r3, [sp, #28]
	cmp	r3, #4
	bne	.L4899
.L4898:
	mov	r0, #0
	bl	zftl_flash_exit_slc_mode
	ldr	r3, .L4940
	mov	r0, #0
	ldrb	r2, [sp, #60]	@ zero_extendqisi2
	strb	r2, [r3]
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldrneb	r2, [sp, #68]	@ zero_extendqisi2
	strneb	r2, [r3, #1110]
	mov	r2, #2
	strb	r2, [r3, #1156]
	bl	zftl_flash_enter_slc_mode
	ldr	r0, [sp, #24]
	bl	ftl_free
	ldr	r3, [sp, #40]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
.L4862:
	add	sp, sp, #156
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4941:
	.align	2
.L4940:
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
	ldr	r5, .L4968
	bne	.L4943
	ldr	r3, [r2]
	ldr	r1, .L4968+4
	ldr	r2, .L4968+8
	cmp	r3, r1
	cmpne	r3, r2
	ldr	r1, .L4968+12
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, r1
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	beq	.L4943
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r5, #32]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r5, #36]
	bl	ftl_memset
	str	r4, [r5, #40]
.L4943:
	str	r6, [sp]
	mov	r3, r4
	ldr	r0, .L4968+16
	ldr	r1, [r5, #36]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r5, #32]	@ zero_extendqisi2
	ldr	r9, .L4968
	cmp	r3, #0
	beq	.L4942
	ldr	r7, [r9, #36]
	cmp	r7, #0
	beq	.L4942
	sub	r0, r4, #64
	cmp	r0, #500
	bcs	.L4945
	rsb	r2, r4, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L4946
.L4945:
	cmp	r4, #564
	bcs	.L4954
.L4946:
	ldr	r3, [r5, #40]
	ldr	r8, .L4968
	cmp	r3, r4
	beq	.L4952
	mov	r0, r7
	mov	r9, #0
	strb	r9, [r8, #32]
	bl	ftl_free
	str	r9, [r8, #36]
.L4952:
	add	r4, r4, r6
	str	r4, [r5, #40]
	b	.L4942
.L4954:
	ldr	r3, .L4968+20
	ldr	r0, [r9, #40]
	ldr	r3, [r3, #1104]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L4955
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
	b	.L4948
.L4955:
	mov	r3, #0
.L4947:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L4947
.L4948:
	mov	r3, #61952
.L4951:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #2048
	movne	r0, r3, asl #2
	bne	.L4950
.L4949:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L4951
	mov	r0, r0, asl #9
.L4950:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r5, #36]
	mov	r7, #0
	strb	r7, [r5, #32]
	bl	ftl_free
	str	r7, [r5, #36]
	b	.L4952
.L4942:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L4969:
	.align	2
.L4968:
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
	beq	.L4971
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L4971:
	ldr	r3, .L4976
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	ldr	r3, [r3, #-152]
	ldr	ip, [r3, #24]
	mov	r3, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	bx	ip
.L4977:
	.align	2
.L4976:
	.word	.LANCHOR3
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L4984
	cmp	r1, r3
	bne	.L4982
	bl	rknand_dev_flush
	mov	r1, #0
	ldr	r0, .L4984+4
	bl	printk
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L4982:
	mvn	r0, #21
	ldmfd	sp!, {r3, pc}
.L4985:
	.align	2
.L4984:
	.word	1074029332
	.word	.LC323
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L4987
	mov	r2, #0
	mvn	r1, #0
	strb	r2, [r3, #32]
	str	r1, [r3, #40]
	str	r2, [r3, #36]
	str	r2, [r3, #44]
	b	rknand_sys_storage_init
.L4988:
	.align	2
.L4987:
	.word	.LANCHOR3
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
	ldr	r7, .L5002
	ldr	r3, [r7, #48]
	cmp	r3, #0
	bne	.L4991
	mov	r0, #65536
	bl	ftl_malloc
	str	r0, [r7, #48]
.L4991:
	ldr	r3, [r7, #48]
	cmp	r3, #0
	beq	.L4996
	ldr	r10, .L5002
	mov	r8, #0
	ldr	r9, .L5002+4
	mov	r4, r8
	mov	r6, r8
.L4994:
	mov	r0, r6, asl #7
	mov	r1, #128
	ldr	r2, [r7, #48]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4997
	ldr	r3, [r10, #48]
	ldr	r0, .L5002+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #48]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4993
	add	r2, r5, #61440
	ldr	r3, [r5, #4]
	ldr	r1, [r2, #4092]
	cmp	r4, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r1, r3
	movne	r2, #0
	cmp	r2, #0
	movne	r8, r6
	movne	r4, r3
.L4993:
	cmp	r6, #1
	movne	r6, #1
	bne	.L4994
.L5001:
	cmp	r4, #0
	beq	.L4995
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	adds	r0, r0, #0
	movne	r0, #1
	rsb	r0, r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4995:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L5002+4
	str	r6, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r6, [r3, #4092]
	ldr	r3, .L5002+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4996:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4997:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L5003:
	.align	2
.L5002:
	.word	.LANCHOR3
	.word	1380668996
	.word	.LC324
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
	ldr	r3, .L5011
	ldr	ip, [r3, #48]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L5009
.L5006:
	cmp	r3, r4
	bcs	.L5009
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L5007
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
.L5007:
	add	r3, r3, #1
	b	.L5006
.L5009:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L5012:
	.align	2
.L5011:
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
	ldr	r2, .L5036
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #48]
	cmp	r4, #0
	beq	.L5028
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L5015:
	cmp	r6, r2
	bcs	.L5034
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L5016
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L5017
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L5028
.L5018:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L5035
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
	b	.L5018
.L5035:
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
	b	.L5033
.L5017:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L5033
.L5016:
	add	r6, r6, #1
	b	.L5015
.L5034:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L5028
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
.L5033:
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
	b	.L5014
.L5028:
	mvn	r0, #0
.L5014:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L5037:
	.align	2
.L5036:
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
	mov	r0, #9216
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L5039
	ldr	r3, .L5055
	cmp	r6, r3
	beq	.L5041
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L5042
	b	.L5053
.L5041:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5053
	ldr	r2, [r4]
	ldr	r3, .L5055+4
	cmp	r2, r3
	beq	.L5044
.L5045:
	mvn	r5, #0
	b	.L5040
.L5044:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L5045
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L5040
.L5042:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5053
	ldr	r2, [r4]
	ldr	r3, .L5055+4
	cmp	r2, r3
	bne	.L5045
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L5045
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L5053
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L5040
.L5053:
	mvn	r5, #13
.L5040:
	mov	r0, r4
	bl	kfree
.L5039:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L5056:
	.align	2
.L5055:
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
.LANCHOR1 = . + 0
	.type	__func__.42986, %object
	.size	__func__.42986, 18
__func__.42986:
	.ascii	"_list_remove_node\000"
	.space	2
	.type	__func__.43011, %object
	.size	__func__.43011, 23
__func__.43011:
	.ascii	"_list_update_data_list\000"
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
	.type	__func__.23492, %object
	.size	__func__.23492, 22
__func__.23492:
	.ascii	"nand_flash_print_info\000"
	.space	2
	.type	__func__.22965, %object
	.size	__func__.22965, 28
__func__.22965:
	.ascii	"flash_wait_device_ready_raw\000"
	.type	__func__.23029, %object
	.size	__func__.23029, 22
__func__.23029:
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
	.type	__func__.23054, %object
	.size	__func__.23054, 23
__func__.23054:
	.ascii	"flash_start_plane_read\000"
	.space	1
	.type	__func__.22940, %object
	.size	__func__.22940, 26
__func__.22940:
	.ascii	"flash_erase_duplane_block\000"
	.space	2
	.type	__func__.22951, %object
	.size	__func__.22951, 21
__func__.22951:
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
	.type	__func__.23757, %object
	.size	__func__.23757, 13
__func__.23757:
	.ascii	"buf_add_tail\000"
	.space	3
	.type	__func__.23770, %object
	.size	__func__.23770, 10
__func__.23770:
	.ascii	"buf_alloc\000"
	.space	2
	.type	__func__.23784, %object
	.size	__func__.23784, 16
__func__.23784:
	.ascii	"buf_remove_free\000"
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
	.type	__func__.42519, %object
	.size	__func__.42519, 12
__func__.42519:
	.ascii	"gc_add_sblk\000"
	.type	__func__.42611, %object
	.size	__func__.42611, 19
__func__.42611:
	.ascii	"gc_write_completed\000"
	.space	1
	.type	__func__.43217, %object
	.size	__func__.43217, 18
__func__.43217:
	.ascii	"ftl_alloc_sys_blk\000"
	.space	2
	.type	__func__.43227, %object
	.size	__func__.43227, 17
__func__.43227:
	.ascii	"ftl_free_sys_blk\000"
	.space	3
	.type	__func__.43348, %object
	.size	__func__.43348, 23
__func__.43348:
	.ascii	"ftl_get_ppa_from_index\000"
	.space	1
	.type	__func__.43388, %object
	.size	__func__.43388, 22
__func__.43388:
	.ascii	"ftl_get_new_free_page\000"
	.space	2
	.type	__func__.43399, %object
	.size	__func__.43399, 22
__func__.43399:
	.ascii	"ftl_ext_alloc_new_blk\000"
	.space	2
	.type	__func__.42668, %object
	.size	__func__.42668, 16
__func__.42668:
	.ascii	"gc_free_src_blk\000"
	.type	__func__.42262, %object
	.size	__func__.42262, 14
__func__.42262:
	.ascii	"ftl_write_buf\000"
	.space	2
	.type	__func__.42307, %object
	.size	__func__.42307, 18
__func__.42307:
	.ascii	"zftl_add_read_buf\000"
	.space	2
	.type	__func__.43821, %object
	.size	__func__.43821, 21
__func__.43821:
	.ascii	"pm_select_ram_region\000"
	.space	3
	.type	__func__.23482, %object
	.size	__func__.23482, 20
__func__.23482:
	.ascii	"flash_die_info_init\000"
	.type	__func__.42194, %object
	.size	__func__.42194, 17
__func__.42194:
	.ascii	"lpa_rebuild_hash\000"
	.space	3
	.type	__func__.43086, %object
	.size	__func__.43086, 20
__func__.43086:
	.ascii	"zftl_sblk_list_init\000"
	.type	__func__.43753, %object
	.size	__func__.43753, 13
__func__.43753:
	.ascii	"pm_free_sblk\000"
	.space	3
	.type	__func__.23820, %object
	.size	__func__.23820, 21
__func__.23820:
	.ascii	"flash_info_data_init\000"
	.space	3
	.type	__func__.23578, %object
	.size	__func__.23578, 11
__func__.23578:
	.ascii	"nandc_init\000"
	.space	1
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
	.type	__func__.22863, %object
	.size	__func__.22863, 19
__func__.22863:
	.ascii	"flash_read_page_en\000"
	.space	1
	.type	__func__.22989, %object
	.size	__func__.22989, 26
__func__.22989:
	.ascii	"flash_start_tlc_page_prog\000"
	.space	2
	.type	__func__.23000, %object
	.size	__func__.23000, 29
__func__.23000:
	.ascii	"flash_start_3d_mlc_page_prog\000"
	.space	3
	.type	__func__.23018, %object
	.size	__func__.23018, 22
__func__.23018:
	.ascii	"flash_start_page_prog\000"
	.space	2
	.type	__func__.23042, %object
	.size	__func__.23042, 31
__func__.23042:
	.ascii	"flash_complete_plane_page_read\000"
	.space	1
	.type	__func__.23067, %object
	.size	__func__.23067, 25
__func__.23067:
	.ascii	"flash_complete_page_read\000"
	.space	3
	.type	__func__.43586, %object
	.size	__func__.43586, 31
__func__.43586:
	.ascii	"queue_wait_first_req_completed\000"
	.space	1
	.type	__func__.43640, %object
	.size	__func__.43640, 15
__func__.43640:
	.ascii	"sblk_prog_page\000"
	.space	1
	.type	__func__.43667, %object
	.size	__func__.43667, 15
__func__.43667:
	.ascii	"sblk_read_page\000"
	.space	1
	.type	__func__.42566, %object
	.size	__func__.42566, 21
__func__.42566:
	.ascii	"gc_check_data_one_wl\000"
	.space	3
	.type	__func__.22924, %object
	.size	__func__.22924, 19
__func__.22924:
	.ascii	"flash_prog_page_en\000"
	.space	1
	.type	__func__.43463, %object
	.size	__func__.43463, 14
__func__.43463:
	.ascii	"ftl_prog_page\000"
	.space	2
	.type	__func__.43120, %object
	.size	__func__.43120, 15
__func__.43120:
	.ascii	"ftl_info_flush\000"
	.space	1
	.type	__func__.43425, %object
	.size	__func__.43425, 19
__func__.43425:
	.ascii	"ftl_ext_info_flush\000"
	.space	1
	.type	__func__.43437, %object
	.size	__func__.43437, 18
__func__.43437:
	.ascii	"ftl_ext_info_init\000"
	.space	2
	.type	__func__.43175, %object
	.size	__func__.43175, 15
__func__.43175:
	.ascii	"ftl_alloc_sblk\000"
	.space	1
	.type	__func__.43793, %object
	.size	__func__.43793, 17
__func__.43793:
	.ascii	"pm_alloc_new_blk\000"
	.space	3
	.type	__func__.43803, %object
	.size	__func__.43803, 14
__func__.43803:
	.ascii	"pm_write_page\000"
	.space	2
	.type	__func__.23842, %object
	.size	__func__.23842, 17
__func__.23842:
	.ascii	"flash_info_flush\000"
	.space	3
	.type	__func__.23805, %object
	.size	__func__.23805, 20
__func__.23805:
	.ascii	"flash_info_blk_init\000"
	.type	__func__.23537, %object
	.size	__func__.23537, 16
__func__.23537:
	.ascii	"nand_flash_init\000"
	.type	__func__.43290, %object
	.size	__func__.43290, 16
__func__.43290:
	.ascii	"ftl_sysblk_dump\000"
	.type	__func__.43315, %object
	.size	__func__.43315, 23
__func__.43315:
	.ascii	"ftl_open_sblk_recovery\000"
	.space	1
	.type	__func__.43737, %object
	.size	__func__.43737, 16
__func__.43737:
	.ascii	"load_l2p_region\000"
	.type	__func__.43777, %object
	.size	__func__.43777, 6
__func__.43777:
	.ascii	"pm_gc\000"
	.space	2
	.type	__func__.42509, %object
	.size	__func__.42509, 12
__func__.42509:
	.ascii	"gc_recovery\000"
	.type	__func__.42598, %object
	.size	__func__.42598, 22
__func__.42598:
	.ascii	"gc_update_l2p_map_new\000"
	.space	2
	.type	__func__.42712, %object
	.size	__func__.42712, 16
__func__.42712:
	.ascii	"gc_scan_src_blk\000"
	.type	__func__.42773, %object
	.size	__func__.42773, 20
__func__.42773:
	.ascii	"gc_scan_static_data\000"
	.type	__func__.42836, %object
	.size	__func__.42836, 18
__func__.42836:
	.ascii	"gc_block_vpn_scan\000"
	.space	2
	.type	__func__.43266, %object
	.size	__func__.43266, 14
__func__.43266:
	.ascii	"ftl_sblk_dump\000"
	.space	2
	.type	__func__.42340, %object
	.size	__func__.42340, 10
__func__.42340:
	.ascii	"zftl_read\000"
	.space	2
	.type	__func__.43369, %object
	.size	__func__.43369, 19
__func__.43369:
	.ascii	"ftl_update_l2p_map\000"
	.space	1
	.type	__func__.42244, %object
	.size	__func__.42244, 17
__func__.42244:
	.ascii	"ftl_write_commit\000"
	.space	3
	.type	__func__.42641, %object
	.size	__func__.42641, 16
__func__.42641:
	.ascii	"gc_do_copy_back\000"
	.type	__func__.42891, %object
	.size	__func__.42891, 11
__func__.42891:
	.ascii	"zftl_do_gc\000"
	.space	1
	.type	__func__.42372, %object
	.size	__func__.42372, 13
__func__.42372:
	.ascii	"_ftl_discard\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC1:
	.ascii	"%s\012\000"
.LC2:
	.ascii	"FTL version: 6.0.24 20210716\000"
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
	.ascii	"return ret = %lx\012\000"
.LC324:
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
	.space	3
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 4
ftl_info_spare:
	.space	4
	.space	8
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
	.space	2
	.type	nandc_ecc_sts, %object
	.size	nandc_ecc_sts, 16
nandc_ecc_sts:
	.space	16
	.type	g_slc_mode_enable, %object
	.size	g_slc_mode_enable, 1
g_slc_mode_enable:
	.space	1
