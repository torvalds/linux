/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-11-30
 * function: rk ftl v6 for rockchip soc base on arm v8 to support 3D/2D
 *	     TLC and MLC.
 */
	.cpu generic+fp+simd
	.file	"rk_zftl_arm64.S"
	.text
	.align	2
	.type	flash_mem_cmp8, %function
flash_mem_cmp8:
	mov	x3, 0
.L2:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L7
	ldrb	w5, [x0,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w5
	beq	.L2
	add	w0, w4, 1
	b	.L3
.L7:
	mov	w0, 0
.L3:
	ret
	.size	flash_mem_cmp8, .-flash_mem_cmp8
	.align	2
	.type	slc_phy_page_address_calc, %function
slc_phy_page_address_calc:
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w3, [x1,#:lo12:.LANCHOR0]
	cbz	w3, .L9
	ldrb	w2, [x2,1]
	cbz	w2, .L10
.L9:
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w3, [x1,2]
	udiv	w2, w0, w3
	mul	w2, w2, w3
	ldrb	w3, [x1,1]
	sub	w0, w0, w2
	cbz	w3, .L11
	add	w0, w2, w0, lsl 1
	b	.L10
.L11:
	add	x1, x1, 4
	ldrh	w0, [x1,w0,uxtw 1]
	add	w0, w0, w2
.L10:
	ret
	.size	slc_phy_page_address_calc, .-slc_phy_page_address_calc
	.align	2
	.global	zftl_nandc_get_irq_status
	.type	zftl_nandc_get_irq_status, %function
zftl_nandc_get_irq_status:
	adrp	x1, .LANCHOR0+1028
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1028]
	cmp	w1, 9
	bne	.L19
	ldr	w0, [x0,296]
	b	.L20
.L19:
	ldr	w0, [x0,372]
.L20:
	ret
	.size	zftl_nandc_get_irq_status, .-zftl_nandc_get_irq_status
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	isxdigit, %function
isxdigit:
	and	w1, w0, -33
	mov	w2, 1
	sub	w1, w1, #65
	cmp	w1, 25
	bls	.L22
	sub	w0, w0, #48
	cmp	w0, 9
	cset	w2, ls
.L22:
	mov	w0, w2
	ret
	.size	isxdigit, .-isxdigit
	.text
	.align	2
	.global	zftl_get_density
	.type	zftl_get_density, %function
zftl_get_density:
	cbnz	w0, .L25
	adrp	x0, .LANCHOR0+1032
	ldr	w0, [x0,#:lo12:.LANCHOR0+1032]
	b	.L26
.L25:
	cmp	w0, 4
	mov	w0, 8192
	csel	w0, w0, wzr, cc
.L26:
	ret
	.size	zftl_get_density, .-zftl_get_density
	.align	2
	.type	_list_remove_node, %function
_list_remove_node:
	stp	x29, x30, [sp, -64]!
	uxth	w1, w1
	mov	w4, 6
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x3, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	umull	x21, w1, w4
	ldr	x23, [x3,1040]
	mov	w3, 65535
	add	x20, x23, x21
	ldrh	w1, [x23,x21]
	cmp	w1, w3
	bne	.L29
	ldrh	w3, [x20,2]
	cmp	w3, w1
	bne	.L29
	ldr	x1, [x0]
	cmp	x20, x1
	bne	.L28
.L29:
	ldrh	w1, [x20,2]
	mov	x22, x0
	mov	w0, 65535
	mov	x24, x2
	cmp	w1, w0
	bne	.L31
	ldr	x0, [x22]
	cmp	x20, x0
	beq	.L31
	adrp	x0, .LC0
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 202
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L31:
	ldr	x0, [x22]
	mov	w2, 65535
	cmp	x20, x0
	bne	.L32
	ldrh	w1, [x23,x21]
	cmp	w1, w2
	bne	.L33
	str	xzr, [x22]
	b	.L34
.L33:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	umull	x1, w1, w0
	ldr	x0, [x19,1040]
	add	x1, x0, x1
	str	x1, [x22]
	mov	w0, -1
	strh	w0, [x1,2]
	b	.L34
.L32:
	ldrh	w0, [x23,x21]
	cmp	w0, w2
	bne	.L35
	ldrh	w1, [x20,2]
	cmp	w1, w0
	beq	.L34
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19,1040]
	b	.L36
.L35:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	ldrh	w2, [x20,2]
	umull	x0, w0, w1
	ldr	x3, [x19,1040]
	add	x0, x3, x0
	strh	w2, [x0,2]
	ldrh	w0, [x20,2]
	ldrh	w2, [x23,x21]
	umull	x0, w0, w1
	ldr	x1, [x19,1040]
.L36:
	strh	w2, [x1,x0]
.L34:
	mov	w0, -1
	strh	w0, [x23,x21]
	strh	w0, [x20,2]
	ldrh	w0, [x24]
	sub	w0, w0, #1
	strh	w0, [x24]
.L28:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	_list_remove_node, .-_list_remove_node
	.align	2
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	stp	x29, x30, [sp, -32]!
	uxth	w1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 65535
	ldr	x20, [x0]
	cbz	x20, .L38
	adrp	x3, .LANCHOR0+1040
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3,#:lo12:.LANCHOR0+1040]
.L39:
	cbnz	w1, .L40
.L42:
	sub	x19, x20, x19
	mov	x1, -6148914691236517206
	asr	x19, x19, 1
	madd	x19, x1, x19, x19
	uxth	w19, w19
	mov	w1, w19
	bl	_list_remove_node
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x20,2]
	b	.L38
.L40:
	ldrh	w3, [x20]
	cmp	w3, w4
	beq	.L42
	umull	x20, w3, w5
	sub	w1, w1, #1
	add	x20, x19, x20
	uxth	w1, w1
	b	.L39
.L38:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	_list_pop_index_node, .-_list_pop_index_node
	.align	2
	.type	zftl_debug_proc_open, %function
zftl_debug_proc_open:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x1, [x29,24]
	bl	PDE_DATA
	ldr	x1, [x29,24]
	mov	x2, x0
	mov	x0, x1
	adrp	x1, zftl_debug_proc_show
	add	x1, x1, :lo12:zftl_debug_proc_show
	bl	single_open
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_debug_proc_open, .-zftl_debug_proc_open
	.align	2
	.type	zftl_debug_proc_show, %function
zftl_debug_proc_show:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC1
	adrp	x2, .LC2
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC1
	add	x2, x2, :lo12:.LC2
	str	x19, [sp,16]
	mov	x19, x0
	bl	seq_printf
	adrp	x2, .LANCHOR2
	adrp	x1, .LC3
	mov	x0, x19
	add	x1, x1, :lo12:.LC3
	ldr	w2, [x2,#:lo12:.LANCHOR2]
	bl	seq_printf
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_debug_proc_show, .-zftl_debug_proc_show
	.align	2
	.global	zftl_flash_suspend
	.type	zftl_flash_suspend, %function
zftl_flash_suspend:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,1028]
	cmp	w1, 9
	ldr	x1, [x0,1048]
	ldr	w2, [x1]
	str	w2, [x0,1056]
	ldr	w2, [x1,4]
	str	w2, [x0,1060]
	bne	.L47
	ldr	w2, [x1,16]
	str	w2, [x0,1064]
	ldr	w2, [x1,32]
	str	w2, [x0,1068]
	ldr	w2, [x1,80]
	str	w2, [x0,1072]
	ldr	w2, [x1,84]
	str	w2, [x0,1076]
	ldr	w2, [x1,520]
	str	w2, [x0,1080]
	ldr	w1, [x1,8]
	b	.L49
.L47:
	ldr	w2, [x1,8]
	str	w2, [x0,1064]
	ldr	w2, [x1,12]
	str	w2, [x0,1068]
	ldr	w2, [x1,304]
	str	w2, [x0,1072]
	ldr	w2, [x1,308]
	str	w2, [x0,1076]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,1080]
.L49:
	str	w1, [x0,1084]
	ret
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	adrp	x2, .LANCHOR0+1028
	ldrb	w2, [x2,#:lo12:.LANCHOR0+1028]
	cmp	w2, 9
	mov	x2, 1
	bne	.L51
	ldr	w3, [x0,292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,292]
	ldr	w2, [x0,288]
	bic	w1, w2, w1
	str	w1, [x0,288]
	b	.L50
.L51:
	ldr	w3, [x0,368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
.L50:
	ret
	.size	nandc_irq_disable, .-nandc_irq_disable
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	adrp	x3, .LANCHOR0
	uxth	w1, w1
	add	x6, x3, :lo12:.LANCHOR0
	mov	x10, x3
	ldrh	w4, [x6,1088]
	cmp	w4, w1
	bls	.L53
	ldrh	w3, [x2]
	mov	w7, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x11, w1, w7
	ldr	x12, [x6,1040]
	mov	w2, -1
	add	x5, x12, x11
	strh	w2, [x5,2]
	strh	w2, [x12,x11]
	ldr	x2, [x0]
	cbz	x2, .L60
	ldr	x9, [x6,1096]
	ubfiz	x8, x1, 2, 16
	ldrh	w13, [x6,1104]
	mov	w16, 65535
	ldr	x14, [x6,1040]
	mov	x6, -6148914691236517206
	ldr	w4, [x9,x8]
	ubfx	x3, x4, 11, 8
	ldrh	w4, [x9,x8]
	mov	w8, w7
	ubfx	x4, x4, 0, 11
	madd	w4, w13, w3, w4
	sub	x3, x2, x14
	asr	x3, x3, 1
	uxth	w4, w4
	madd	x3, x6, x3, x3
	uxth	w3, w3
.L58:
	ubfiz	x15, x3, 2, 16
	ldr	w6, [x9,x15]
	ubfx	x7, x6, 11, 8
	ldrh	w6, [x9,x15]
	ubfx	x6, x6, 0, 11
	madd	w6, w13, w7, w6
	cmp	w4, w6, uxth
	bls	.L56
	ldrh	w6, [x2]
	cmp	w6, w16
	bne	.L57
	strh	w3, [x5,2]
	strh	w1, [x2]
	b	.L53
.L57:
	umull	x2, w6, w8
	mov	w3, w6
	add	x2, x14, x2
	b	.L58
.L56:
	ldrh	w4, [x2,2]
	strh	w4, [x5,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L59
	strh	w1, [x2,2]
.L60:
	str	x5, [x0]
	b	.L53
.L59:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,1040]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L53:
	ret
	.size	_insert_free_list, .-_insert_free_list
	.align	2
	.type	_insert_data_list, %function
_insert_data_list:
	sub	sp, sp, #32
	adrp	x6, .LANCHOR0
	add	x10, x6, :lo12:.LANCHOR0
	uxth	w1, w1
	stp	x19, x20, [sp]
	str	x21, [sp,16]
	ldrh	w3, [x10,1088]
	cmp	w3, w1
	bls	.L61
	ldrh	w3, [x2]
	mov	w8, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x8, w1, w8
	ldr	x14, [x10,1040]
	mov	w2, -1
	add	x7, x14, x8
	strh	w2, [x7,2]
	strh	w2, [x14,x8]
	ldr	x2, [x0]
	cbz	x2, .L79
	uxtw	x9, w1
	ldr	x15, [x10,1112]
	ldr	x13, [x10,1096]
	ldrh	w11, [x7,4]
	ldrh	w5, [x15,x9,lsl 1]
	lsl	x9, x9, 2
	ldrh	w16, [x10,1104]
	ldr	w3, [x13,x9]
	mul	w5, w5, w11
	ubfx	x4, x3, 11, 8
	ldrh	w3, [x13,x9]
	and	w3, w3, 2047
	cbz	w5, .L65
	madd	w3, w16, w4, w3
	add	w5, w5, w3, uxth
.L65:
	add	x4, x6, :lo12:.LANCHOR0
	mov	x9, -6148914691236517206
	cmp	w11, wzr
	mov	w21, 65535
	csinv	w5, w5, wzr, ne
	mov	w19, 6
	ldr	x17, [x4,1040]
	ldrh	w20, [x4,1088]
	sub	x3, x2, x17
	asr	x3, x3, 1
	madd	x3, x9, x3, x3
	mov	w9, 0
	uxth	w3, w3
.L71:
	add	w9, w9, 1
	cmp	w1, w3
	uxth	w9, w9
	beq	.L61
	cmp	w9, w20
	bhi	.L61
	uxtw	x12, w3
	ldrh	w18, [x2,4]
	ldrh	w4, [x15,x12,lsl 1]
	lsl	x12, x12, 2
	ldr	w10, [x13,x12]
	mul	w4, w4, w18
	ubfx	x11, x10, 11, 8
	ldrh	w10, [x13,x12]
	and	w10, w10, 2047
	cbz	w4, .L68
	madd	w10, w16, w11, w10
	add	w4, w4, w10, uxth
.L68:
	cmp	w4, w5
	bcs	.L69
	cbz	w18, .L69
	ldrh	w4, [x2]
	cmp	w4, w21
	bne	.L70
	strh	w3, [x7,2]
	strh	w1, [x2]
	b	.L61
.L70:
	umull	x2, w4, w19
	mov	w3, w4
	add	x2, x17, x2
	b	.L71
.L69:
	ldrh	w4, [x2,2]
	strh	w4, [x7,2]
	strh	w3, [x14,x8]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L72
	strh	w1, [x2,2]
.L79:
	str	x7, [x0]
	b	.L61
.L72:
	ldrh	w0, [x2,2]
	add	x6, x6, :lo12:.LANCHOR0
	mov	w3, 6
	umull	x0, w0, w3
	ldr	x3, [x6,1040]
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L61:
	ldp	x19, x20, [sp]
	ldr	x21, [sp,16]
	add	sp, sp, 32
	ret
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_get_gc_head_node.isra.4, %function
_list_get_gc_head_node.isra.4:
	uxth	w1, w1
	mov	w2, 65535
	cbz	x0, .L86
	adrp	x2, .LANCHOR0+1040
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x2,#:lo12:.LANCHOR0+1040]
.L82:
	cbz	w1, .L83
	ldrh	w2, [x0]
	cmp	w2, w4
	beq	.L86
	umull	x2, w2, w5
	sub	w1, w1, #1
	add	x0, x3, x2
	uxth	w1, w1
	b	.L82
.L83:
	sub	x0, x0, x3
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	madd	x0, x1, x0, x0
	uxth	w2, w0
.L86:
	mov	w0, w2
	ret
	.size	_list_get_gc_head_node.isra.4, .-_list_get_gc_head_node.isra.4
	.align	2
	.type	_list_update_data_list, %function
_list_update_data_list:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w1
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x20, x1
	ldr	x3, [x4,1120]
	ldrh	w5, [x3,16]
	cmp	w5, w19
	beq	.L87
	ldrh	w1, [x3,48]
	cmp	w1, w19
	beq	.L87
	ldrh	w1, [x3,80]
	cmp	w1, w19
	beq	.L87
	mov	w23, 6
	ldr	x25, [x4,1040]
	ldr	x1, [x0]
	umull	x23, w19, w23
	add	x24, x25, x23
	cmp	x24, x1
	beq	.L87
	mov	x21, x0
	ldrh	w0, [x24,2]
	mov	w1, 65535
	mov	x22, x2
	cmp	w0, w1
	bne	.L91
	ldrh	w1, [x25,x23]
	cmp	w1, w0
	bne	.L91
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 273
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L91:
	ldrh	w3, [x24,2]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L92
	ldrh	w0, [x25,x23]
	cmp	w0, w3
	beq	.L87
.L92:
	add	x1, x20, :lo12:.LANCHOR0
	ubfiz	x0, x19, 1, 16
	mov	x5, -6148914691236517206
	ldr	x4, [x1,1112]
	ldr	x1, [x1,1040]
	ldrh	w2, [x4,x0]
	ldrh	w0, [x24,4]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 6
	csinv	w2, w2, wzr, ne
	umull	x3, w3, w0
	asr	x0, x3, 1
	add	x3, x1, x3
	madd	x0, x5, x0, x0
	ldrh	w1, [x3,4]
	ldrh	w0, [x4,x0,lsl 1]
	cmp	w1, wzr
	mul	w0, w0, w1
	csinv	w0, w0, wzr, ne
	cmp	w2, w0
	bcs	.L87
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_list_remove_node
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_insert_data_list
.L87:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	_list_update_data_list, .-_list_update_data_list
	.align	2
	.type	zftl_get_gc_node.part.11, %function
zftl_get_gc_node.part.11:
	uxth	w1, w0
	adrp	x0, .LANCHOR0+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR0+1128]
	bl	_list_get_gc_head_node.isra.4
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node.part.11, .-zftl_get_gc_node.part.11
	.section	.text.unlikely
	.align	2
	.type	rk_simple_strtoull.constprop.35, %function
rk_simple_strtoull.constprop.35:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	mov	x20, x0
	stp	x21, x22, [sp,32]
	mov	x23, x1
	ldrb	w0, [x0]
	cmp	w0, 48
	bne	.L109
	ldrb	w0, [x20,1]
	add	x19, x20, 1
	mov	w21, 8
	orr	w0, w0, 32
	uxtb	w0, w0
	cmp	w0, 120
	bne	.L99
	ldrb	w0, [x20,2]
	bl	isxdigit
	cbz	w0, .L99
	add	x19, x20, 2
	mov	w21, 16
	b	.L99
.L109:
	mov	x19, x20
	mov	w21, 10
.L99:
	mov	w22, 0
.L100:
	ldrb	w20, [x19]
	mov	w0, w20
	bl	isxdigit
	cbnz	w0, .L101
.L107:
	cbz	x23, .L113
	str	x19, [x23]
	b	.L113
.L101:
	sub	w2, w20, #48
	cmp	w2, 9
	bhi	.L114
.L108:
	cmp	w2, w21
	bcs	.L107
	madd	w22, w22, w21, w2
	add	x19, x19, 1
	b	.L100
.L114:
	orr	w20, w20, 32
	sub	w2, w20, #87
	b	.L108
.L113:
	mov	w0, w22
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	rk_simple_strtoull.constprop.35, .-rk_simple_strtoull.constprop.35
	.text
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L116
	adrp	x1, .LANCHOR1
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L116:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L117
	adrp	x0, .LANCHOR0+1136
	ldr	x6, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	ldrb	w1, [x6,1]
	ldrb	w2, [x6,2]
	ldrb	w3, [x6,3]
	ldrb	w4, [x6,4]
	ldrb	w5, [x6,5]
	ldrb	w6, [x6,6]
	bl	printk
.L117:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L118
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,8]
	bl	printk
.L118:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L119
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrb	w1, [x1,9]
	bl	printk
.L119:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L120
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrh	w1, [x1,10]
	bl	printk
.L120:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L121
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,12]
	bl	printk
.L121:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L122
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,13]
	bl	printk
.L122:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L123
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrh	w1, [x1,14]
	bl	printk
.L123:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L124
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	ldrb	w1, [x1,23]
	bl	printk
.L124:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L125
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,18]
	bl	printk
.L125:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L126
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,19]
	bl	printk
.L126:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L127
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrb	w1, [x1,20]
	bl	printk
.L127:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L128
	adrp	x1, .LANCHOR0+1144
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1144]
	bl	printk
.L128:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x0, .LANCHOR0+1136
	ldr	x1, [x0,#:lo12:.LANCHOR0+1136]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,22]
	bl	printk
.L129:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x1, .LANCHOR0+1145
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1145]
	bl	printk
.L130:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	and	w1, w1, 1
	bl	printk
.L131:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L132
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	ubfx	x1, x1, 1, 1
	bl	printk
.L132:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L133
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 2, 1
	bl	printk
.L133:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L134
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 3, 1
	bl	printk
.L134:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L135
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 4, 1
	bl	printk
.L135:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L136
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 5, 1
	bl	printk
.L136:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L137
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 6, 1
	bl	printk
.L137:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L138
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 7, 1
	bl	printk
.L138:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L139
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrb	w1, [x0,17]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	and	w1, w1, 1
	bl	printk
.L139:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L140
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	ubfx	x1, x1, 9, 1
	bl	printk
.L140:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L141
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrh	w1, [x0,16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 10, 1
	bl	printk
.L141:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L142
	adrp	x1, .LANCHOR0
	adrp	x0, .LC30
	add	x2, x1, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC30
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldrb	w2, [x2,1146]
	bl	printk
.L142:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L143
	adrp	x2, .LANCHOR0
	adrp	x0, .LC31
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC31
	add	x2, x2, 1152
	ldrb	w1, [x2,10]
	ldrb	w2, [x2,11]
	bl	printk
.L143:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L144
	adrp	x2, .LANCHOR0
	adrp	x0, .LC32
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC32
	add	x2, x2, 1152
	ldrb	w1, [x2,8]
	ldrb	w2, [x2,9]
	bl	printk
.L144:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L115
	adrp	x1, .LANCHOR0+1184
	adrp	x0, .LC33
	add	x0, x0, :lo12:.LC33
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1184]
	bl	printk
.L115:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nand_flash_print_info, .-nand_flash_print_info
	.align	2
	.global	nandc_init
	.type	nandc_init, %function
nandc_init:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	mov	x22, x0
	stp	x19, x20, [sp,16]
	str	wzr, [x29,56]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L237
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 72
	mov	x2, x22
	bl	printk
.L237:
	adrp	x1, .LANCHOR0
	mov	w2, 6
	add	x0, x1, :lo12:.LANCHOR0
	ldr	w3, [x22,352]
	mov	x20, x1
	strb	w2, [x0,1028]
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	str	x22, [x0,1048]
	cmp	w3, w2
	bne	.L238
	mov	w1, 8
	strb	w1, [x0,1028]
.L238:
	ldr	w1, [x22,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L239
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 9
	strb	w1, [x0,1028]
.L239:
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1028]
	cmp	w0, 9
	bne	.L240
	mov	w0, 1
	strb	w0, [x19,1185]
	ldr	w0, [x29,56]
	mov	w1, 2
	orr	w0, w0, 256
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	bfi	w0, w1, 18, 3
	str	w0, [x29,56]
	mov	w1, 4161
	ldr	w0, [x29,56]
	str	w0, [x22]
	ldr	x0, [x19,1048]
	str	wzr, [x0,520]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x19,1048]
	str	w1, [x0,8]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,80]
	mov	w1, 38
	str	w1, [x0,84]
	mov	w1, 39
	str	w1, [x0,84]
	ldr	w1, [x21,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L242
	ldr	w1, [x0]
	ldr	w2, [x0,8]
	ldr	w3, [x0,80]
	ldr	w4, [x0,84]
	ldr	w5, [x0,88]
	b	.L254
.L240:
	ldr	w0, [x29,56]
	mov	w1, 1
	strb	wzr, [x19,1185]
	orr	w0, w0, 256
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	bfi	w0, w1, 24, 3
	str	w0, [x29,56]
	mov	w1, 4193
	ldr	w0, [x29,56]
	str	w0, [x22]
	ldr	x0, [x19,1048]
	str	wzr, [x0,336]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x19,1048]
	str	w1, [x0,344]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,304]
	mov	w1, 38
	str	w1, [x0,308]
	mov	w1, 39
	str	w1, [x0,308]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x19,1192]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L242
	ldr	x0, [x19,1048]
	ldr	w1, [x0]
	ldr	w2, [x0,344]
	ldr	w3, [x0,304]
	ldr	w4, [x0,308]
	ldr	w5, [x0,312]
.L254:
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L242:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x1,1232]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	strh	wzr, [x1,1234]
	strb	wzr, [x1,1236]
	tbz	x0, 12, .L236
	adrp	x0, .LC36
	ldrb	w1, [x1,1028]
	add	x0, x0, :lo12:.LC36
	bl	printk
.L236:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	nandc_init, .-nandc_init
	.align	2
	.global	timer_delay_ns
	.type	timer_delay_ns, %function
timer_delay_ns:
	sub	sp, sp, #16
	lsr	w0, w0, 4
	str	w0, [sp,12]
.L256:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L256
	add	sp, sp, 16
	ret
	.size	timer_delay_ns, .-timer_delay_ns
	.align	2
	.global	flash_read_status
	.type	flash_read_status, %function
flash_read_status:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	mov	w0, 112
	str	w0, [x19,8]
	mov	w0, 120
	bl	timer_delay_ns
	ldr	w0, [x19]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	uxtb	w0, w0
	ret
	.size	flash_read_status, .-flash_read_status
	.align	2
	.global	toshiba_set_rr_para
	.type	toshiba_set_rr_para, %function
toshiba_set_rr_para:
	uxtb	w1, w1
	mov	w3, 5
	add	w2, w1, 1
	stp	x29, x30, [sp, -96]!
	smull	x3, w2, w3
	adrp	x2, .LANCHOR1
	add	x29, sp, 0
	add	x2, x2, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	add	x23, x2, x1, sxtw
	add	x24, x2, 88
	add	x21, x2, 136
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x0
	add	x24, x24, x3
	add	x21, x21, x3
	mov	x20, 0
	adrp	x26, .LANCHOR0
	mov	w27, 85
	mov	x19, x2
.L260:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,1238]
	cmp	w0, w20
	bls	.L266
	add	x0, x19, 88
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,1237]
	cmp	w0, 34
	bne	.L261
	ldrsb	w0, [x24,x20]
	b	.L265
.L261:
	cmp	w0, 35
	bne	.L263
	ldrsb	w0, [x21,x20]
	b	.L265
.L263:
	ldrsb	w0, [x23,232]
.L265:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L260
.L266:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
	.align	2
	.type	hynix_set_rr_para, %function
hynix_set_rr_para:
	stp	x29, x30, [sp, -64]!
	adrp	x3, .LANCHOR0
	uxtb	w1, w1
	add	x3, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	uxtb	w0, w0
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	ldr	x2, [x3,1240]
	ldr	x19, [x3,1048]
	add	x23, x2, 128
	add	x21, x2, 112
	ldrb	w22, [x2,113]
	ldrb	w2, [x2,112]
	mul	w1, w1, w22
	cmp	w2, 8
	sxtw	x1, w1
	beq	.L268
	sxtw	x2, w0
	mov	x3, 160
	madd	x2, x2, x3, x1
	add	x2, x2, 32
	add	x21, x21, x2
	b	.L269
.L268:
	add	x1, x1, 32
	add	x21, x21, x1
.L269:
	ubfiz	x0, x0, 8, 8
	mov	x20, 0
	add	x19, x19, x0
	mov	w0, 54
	str	w0, [x19,2056]
.L270:
	cmp	w22, w20, uxtb
	bls	.L272
	ldrb	w0, [x23,x20]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x21,x20]
	add	x20, x20, 1
	str	w0, [x19,2048]
	b	.L270
.L272:
	mov	w0, 22
	str	w0, [x19,2056]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	hynix_set_rr_para, .-hynix_set_rr_para
	.align	2
	.global	hynix_reconfig_rr_para
	.type	hynix_reconfig_rr_para, %function
hynix_reconfig_rr_para:
	stp	x29, x30, [sp, -32]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w1, [x19,1237]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 7
	bhi	.L273
	ldr	x1, [x19,1240]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L273
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,1240]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L273:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
	.align	2
	.global	nandc_set_ddr_para
	.type	nandc_set_ddr_para, %function
nandc_set_ddr_para:
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,1028]
	ldr	x1, [x1,1048]
	cmp	w2, 9
	lsl	w2, w0, 16
	lsl	w0, w0, 8
	orr	w0, w2, w0
	orr	w0, w0, 3
	bne	.L279
	str	w0, [x1,80]
	b	.L278
.L279:
	str	w0, [x1,304]
.L278:
	ret
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,1028]
	ldr	x0, [x0,1048]
	cmp	w1, 9
	bne	.L282
	ldr	w0, [x0,80]
	b	.L284
.L282:
	ldr	w0, [x0,304]
.L284:
	ubfx	x0, x0, 8, 8
	ret
	.size	nandc_get_ddr_para, .-nandc_get_ddr_para
	.align	2
	.global	nandc_set_if_mode
	.type	nandc_set_if_mode, %function
nandc_set_if_mode:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	tst	w0, 6
	ldr	x2, [x3,1048]
	ldr	w1, [x2]
	beq	.L286
	orr	w1, w1, 24576
	and	w0, w0, 4
	and	w1, w1, -32769
	cmp	w0, wzr
	orr	w1, w1, 196608
	ldrb	w0, [x3,1028]
	orr	w4, w1, 32768
	csel	w1, w4, w1, ne
	cmp	w0, 9
	mov	w0, 8321
	bne	.L288
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L289
.L288:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L289
.L286:
	and	w1, w1, -8193
.L289:
	str	w1, [x2]
	ret
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	adrp	x1, .LANCHOR0+1048
	mov	w2, 1
	lsl	w0, w2, w0
	ldr	x3, [x1,#:lo12:.LANCHOR0+1048]
	ldr	w1, [x3]
	bfi	w1, w0, 0, 8
	str	w1, [x3]
	ret
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	adrp	x0, .LANCHOR0+1048
	ldr	x1, [x0,#:lo12:.LANCHOR0+1048]
	ldr	w0, [x1]
	and	w0, w0, -256
	and	w0, w0, -131073
	str	w0, [x1]
	ret
	.size	nandc_de_cs, .-nandc_de_cs
	.align	2
	.global	flash_wait_device_ready_raw
	.type	flash_wait_device_ready_raw, %function
flash_wait_device_ready_raw:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	w19, w1
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w22, w2
	ldrb	w0, [x0,1145]
	cmp	w0, w21
	bhi	.L295
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 805
	add	x1, x1, 240
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L295:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x0, x21, sxtw
	ldrb	w21, [x21,1248]
	lsr	w23, w19, 8
	lsr	w24, w19, 16
	lsr	w25, w19, 24
	ldr	x26, [x0,1048]
	ubfiz	x27, x21, 8, 8
	mov	w28, 120
	and	w19, w19, 255
.L300:
	mov	w0, w21
	bl	nandc_cs
	add	x1, x26, x27
	add	x0, x20, :lo12:.LANCHOR0
	str	w28, [x1,2056]
	str	w19, [x1,2052]
	str	w23, [x1,2052]
	ldrb	w0, [x0,1256]
	str	w24, [x1,2052]
	cbz	w0, .L296
	str	w25, [x1,2052]
.L296:
	mov	w0, 120
	str	x1, [x29,104]
	bl	timer_delay_ns
	ldr	x1, [x29,104]
	mov	w0, w21
	ldr	w1, [x1,2048]
	and	w1, w1, 255
	str	x1, [x29,104]
	bl	nandc_de_cs
	ldr	x1, [x29,104]
	and	w0, w22, w1
	cmp	w0, w22
	bne	.L300
	cmp	w1, 255
	beq	.L300
	mov	w0, w1
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	flash_wait_device_ready_raw, .-flash_wait_device_ready_raw
	.align	2
	.global	flash_wait_device_ready
	.type	flash_wait_device_ready, %function
flash_wait_device_ready:
	adrp	x4, .LANCHOR0
	mov	w5, 24
	add	x7, x4, :lo12:.LANCHOR0
	mov	w6, 1
	stp	x29, x30, [sp, -16]!
	mov	w2, w1
	add	x29, sp, 0
	ldrb	w8, [x7,1257]
	sub	w5, w5, w8
	lsl	w3, w6, w5
	lsl	w6, w6, w8
	asr	w5, w0, w5
	sub	w3, w3, #1
	sub	w6, w6, #1
	and	w1, w3, w0
	and	w5, w6, w5
	ubfx	x0, x0, 24, 2
	uxtb	w5, w5
	cbnz	w0, .L303
	ldrb	w0, [x4,#:lo12:.LANCHOR0]
	cbz	w0, .L304
	ldrb	w0, [x7,1]
	cbz	w0, .L303
.L304:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w3, [x4,2]
	udiv	w0, w1, w3
	mul	w0, w0, w3
	sub	w3, w1, w0
	ldrb	w1, [x4,1]
	cbz	w1, .L305
	add	w1, w0, w3, lsl 1
	b	.L303
.L305:
	add	x4, x4, 4
	ldrh	w3, [x4,w3,uxtw 1]
	add	w1, w3, w0
.L303:
	mov	w0, w5
	bl	flash_wait_device_ready_raw
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_wait_device_ready, .-flash_wait_device_ready
	.align	2
	.global	nandc_wait_flash_ready_no_delay
	.type	nandc_wait_flash_ready_no_delay, %function
nandc_wait_flash_ready_no_delay:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	movk	w19, 0x1, lsl 16
.L314:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L315
	mov	w0, 10
	bl	timer_delay_ns
	subs	w19, w19, #1
	bne	.L314
	mov	w0, -1
	b	.L313
.L315:
	mov	w0, 0
.L313:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	nandc_wait_flash_ready_no_delay, .-nandc_wait_flash_ready_no_delay
	.align	2
	.global	zftl_flash_enter_slc_mode
	.type	zftl_flash_enter_slc_mode, %function
zftl_flash_enter_slc_mode:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	x20, w0
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	str	x21, [sp,32]
	cbz	w1, .L317
	cmp	w1, 1
	ldr	x19, [x0,1048]
	bne	.L320
	adrp	x0, .LANCHOR2+37
	ldrb	w1, [x0,#:lo12:.LANCHOR2+37]
	cbz	w1, .L317
	sxtw	x0, w20
	add	x0, x0, 8
	add	x20, x19, x0, lsl 8
	str	w1, [x20,8]
	b	.L317
.L320:
	cmp	w1, 2
	bne	.L322
	add	x0, x0, x20
	ldrb	w1, [x0,1264]
	cbz	w1, .L317
	adrp	x21, .LANCHOR2
	strb	wzr, [x0,1264]
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 8
	ldrb	w0, [x21,29]
	cbz	w0, .L317
	add	x20, x20, 8
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x21,29]
	add	x20, x19, x20, lsl 8
	str	w0, [x20,8]
	b	.L317
.L322:
	cmp	w1, 3
	bne	.L317
	add	x21, x0, x20, sxtw
	ldrb	w0, [x21,1264]
	cbz	w0, .L317
	bl	nandc_wait_flash_ready_no_delay
	strb	wzr, [x21,1264]
	ubfiz	x0, x20, 8, 8
	add	x19, x19, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	w0, 100
	bl	timer_delay_ns
	str	wzr, [x19,2048]
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	mov	w0, 150
	str	wzr, [x19,2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 218
	str	w0, [x19,2056]
.L317:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	x20, w0
	adrp	x0, .LANCHOR0
	stp	x21, x22, [sp,32]
	add	x21, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L335
	cmp	w0, 1
	ldr	x19, [x21,1048]
	bne	.L338
	adrp	x0, .LANCHOR2+38
	ldrb	w1, [x0,#:lo12:.LANCHOR2+38]
	cbz	w1, .L335
	sxtw	x0, w20
	add	x0, x0, 8
	add	x20, x19, x0, lsl 8
	str	w1, [x20,8]
	b	.L335
.L338:
	cmp	w0, 2
	bne	.L340
	add	x21, x21, x20
	ldrb	w0, [x21,1264]
	cbnz	w0, .L335
	adrp	x22, .LANCHOR2
	mov	w1, 4
	add	x22, x22, :lo12:.LANCHOR2
	add	x22, x22, 8
	ldrb	w0, [x22,12]
	cmp	w0, 2
	csel	w0, w0, w1, eq
	strb	w0, [x21,1264]
	ldrb	w0, [x22,30]
	cbz	w0, .L335
	add	x20, x20, 8
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x22,30]
	add	x20, x19, x20, lsl 8
	str	w0, [x20,8]
	b	.L335
.L340:
	cmp	w0, 3
	bne	.L335
	add	x21, x21, x20, sxtw
	ldrb	w0, [x21,1264]
	cbnz	w0, .L335
	adrp	x22, .LANCHOR2
	bl	nandc_wait_flash_ready_no_delay
	add	x22, x22, :lo12:.LANCHOR2
	mov	w1, 4
	ldrb	w0, [x22,20]
	cmp	w0, 2
	csel	w0, w0, w1, eq
	strb	w0, [x21,1264]
	ubfiz	x0, x20, 8, 8
	add	x19, x19, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	w0, 100
	bl	timer_delay_ns
	ldrb	w0, [x22,15]
	cmp	w0, 9
	mov	w0, 1
	bne	.L343
	str	w0, [x19,2048]
	b	.L344
.L343:
	ldrb	w1, [x21,1264]
	str	w1, [x19,2048]
.L344:
	str	w0, [x19,2048]
	mov	w0, 150
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 223
	str	w0, [x19,2056]
.L335:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	stp	x29, x30, [sp, -80]!
	mov	w2, 1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	uxtb	w21, w0
	add	x0, x22, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	w20, 24
	mov	w23, w1
	ldrb	w19, [x0,1257]
	ldrb	w0, [x0,1145]
	sub	w20, w20, w19
	lsl	w19, w2, w19
	lsr	w1, w1, w20
	sub	w19, w19, #1
	and	w19, w1, w19
	uxtb	w19, w19
	cmp	w0, w19
	bhi	.L356
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1001
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L356:
	add	x24, x22, :lo12:.LANCHOR0
	ldrb	w0, [x24,1145]
	cmp	w0, w19
	bls	.L355
	mov	w0, 1
	add	x19, x24, x19, sxtw
	lsl	w20, w0, w20
	ldrb	w19, [x19,1248]
	sub	w20, w20, #1
	ldr	x25, [x24,1048]
	and	w20, w20, w23
	mov	w0, w19
	ubfx	x23, x23, 24, 2
	bl	nandc_cs
	cbnz	w23, .L358
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L359
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	b	.L359
.L358:
	ldr	x0, [x24,1136]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L360
	ldrb	w0, [x24,1268]
	cbnz	w0, .L360
	ldrb	w0, [x24,1269]
	cbnz	w0, .L360
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w23, [x0,8]
	b	.L359
.L360:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L359:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,1136]
	ldrb	w1, [x0,7]
	cmp	w1, 1
	bne	.L361
	ldrb	w0, [x0,12]
	cmp	w0, 2
	bne	.L361
	sxtw	x0, w19
	mov	w1, 38
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w1, [x0,8]
.L361:
	ubfiz	x19, x19, 8, 8
	and	w0, w20, 255
	add	x1, x25, x19
	add	x22, x22, :lo12:.LANCHOR0
	str	wzr, [x1,2056]
	str	wzr, [x1,2052]
	str	wzr, [x1,2052]
	str	w0, [x1,2052]
	lsr	w0, w20, 8
	str	w0, [x1,2052]
	lsr	w0, w20, 16
	str	w0, [x1,2052]
	ldrb	w0, [x22,1256]
	cbz	w0, .L362
	lsr	w20, w20, 24
	str	w20, [x1,2052]
.L362:
	str	w21, [x1,2056]
.L355:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_page_read, .-flash_start_page_read
	.align	2
	.global	nandc_wait_flash_ready
	.type	nandc_wait_flash_ready, %function
nandc_wait_flash_ready:
	stp	x29, x30, [sp, -48]!
	mov	w0, 150
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	movk	w19, 0x1, lsl 16
	bl	timer_delay_ns
.L371:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L372
	mov	w0, 10
	bl	timer_delay_ns
	subs	w19, w19, #1
	bne	.L371
	mov	w0, -1
	b	.L370
.L372:
	mov	w0, 0
.L370:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	nandc_wait_flash_ready, .-nandc_wait_flash_ready
	.align	2
	.global	sandisk_set_rr_para
	.type	sandisk_set_rr_para, %function
sandisk_set_rr_para:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	mov	w0, 239
	uxtb	w19, w1
	add	w19, w19, 1
	str	w0, [x20,8]
	mov	w0, 17
	str	w0, [x20,4]
	mov	w0, 200
	bl	timer_delay_ns
	mov	w1, 5
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	adrp	x5, .LANCHOR0
	smull	x1, w19, w1
	add	x0, x2, 88
	add	x2, x2, 136
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L375:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,1238]
	cmp	w4, w2
	bls	.L380
	ldrb	w3, [x3,1237]
	cmp	w3, 67
	bne	.L376
	ldrsb	w3, [x0,x2]
	b	.L379
.L376:
	ldrsb	w3, [x1,x2]
.L379:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L375
.L380:
	bl	nandc_wait_flash_ready
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	sandisk_set_rr_para, .-sandisk_set_rr_para
	.align	2
	.global	toshiba_3d_set_tlc_rr_para
	.type	toshiba_3d_set_tlc_rr_para, %function
toshiba_3d_set_tlc_rr_para:
	stp	x29, x30, [sp, -48]!
	uxtb	x2, w1
	mov	x1, 7
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, x0
	mov	w21, 213
	str	w21, [x0,8]
	str	wzr, [x0,4]
	mov	w0, -119
	str	w0, [x19,4]
	adrp	x0, .LANCHOR1
	madd	x1, x2, x1, x1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 296
	add	x20, x0, x1
	ldrsb	w0, [x0,x1]
	str	w0, [x19]
	ldrsb	w0, [x20,1]
	str	w0, [x19]
	ldrsb	w0, [x20,2]
	str	w0, [x19]
	ldrsb	w0, [x20,3]
	str	w0, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19,8]
	str	wzr, [x19,4]
	mov	w0, -118
	str	w0, [x19,4]
	ldrsb	w0, [x20,4]
	str	w0, [x19]
	ldrsb	w0, [x20,5]
	str	w0, [x19]
	ldrsb	w0, [x20,6]
	str	w0, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	toshiba_3d_set_tlc_rr_para, .-toshiba_3d_set_tlc_rr_para
	.align	2
	.global	toshiba_3d_set_slc_rr_para
	.type	toshiba_3d_set_slc_rr_para, %function
toshiba_3d_set_slc_rr_para:
	stp	x29, x30, [sp, -16]!
	mov	w2, 213
	add	x29, sp, 0
	str	w2, [x0,8]
	mov	w2, -117
	str	wzr, [x0,4]
	str	w2, [x0,4]
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	add	x1, x2, x1, uxtb
	ldrsb	w1, [x1,697]
	str	w1, [x0]
	str	wzr, [x0]
	str	wzr, [x0]
	str	wzr, [x0]
	bl	nandc_wait_flash_ready
	ldp	x29, x30, [sp], 16
	ret
	.size	toshiba_3d_set_slc_rr_para, .-toshiba_3d_set_slc_rr_para
	.align	2
	.global	toshiba_tlc_set_rr_para
	.type	toshiba_tlc_set_rr_para, %function
toshiba_tlc_set_rr_para:
	stp	x29, x30, [sp, -48]!
	uxtw	x1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	str	x21, [sp,32]
	mov	w21, 239
	adrp	x0, .LANCHOR2
	str	w21, [x19,8]
	cbz	w2, .L384
	mov	w2, 18
	str	w2, [x19,4]
	mov	x2, 7
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 40
	mul	x1, x1, x2
	add	x20, x0, x1
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	ldrb	w0, [x20,1]
	str	w0, [x19]
	ldrb	w0, [x20,2]
	str	w0, [x19]
	ldrb	w0, [x20,3]
	str	w0, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19,8]
	mov	w0, 19
	str	w0, [x19,4]
	ldrb	w0, [x20,4]
	str	w0, [x19]
	ldrb	w0, [x20,5]
	str	w0, [x19]
	ldrb	w0, [x20,6]
	str	w0, [x19]
	str	wzr, [x19]
	b	.L385
.L384:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 376
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L385:
	bl	nandc_wait_flash_ready
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	ymtc_3d_set_tlc_rr_para
	.type	ymtc_3d_set_tlc_rr_para, %function
ymtc_3d_set_tlc_rr_para:
	stp	x29, x30, [sp, -48]!
	mov	x2, 7
	uxtb	x1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	str	x21, [sp,32]
	mov	w21, 239
	str	w21, [x0,8]
	mov	w0, 160
	str	w0, [x19,4]
	adrp	x0, .LANCHOR1
	mul	x1, x1, x2
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 712
	add	x20, x0, x1
	ldrsb	w0, [x0,x1]
	str	w0, [x19]
	ldrsb	w0, [x20,4]
	str	w0, [x19]
	str	wzr, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19,8]
	mov	w0, 161
	str	w0, [x19,4]
	ldrsb	w0, [x20,1]
	str	w0, [x19]
	ldrsb	w0, [x20,3]
	str	w0, [x19]
	ldrsb	w0, [x20,5]
	str	w0, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19,8]
	mov	w0, 162
	str	w0, [x19,4]
	ldrsb	w0, [x20,2]
	str	w0, [x19]
	ldrsb	w0, [x20,6]
	str	w0, [x19]
	str	wzr, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ymtc_3d_set_tlc_rr_para, .-ymtc_3d_set_tlc_rr_para
	.align	2
	.global	ymtc_3d_set_slc_rr_para
	.type	ymtc_3d_set_slc_rr_para, %function
ymtc_3d_set_slc_rr_para:
	stp	x29, x30, [sp, -16]!
	mov	w2, 239
	add	x29, sp, 0
	str	w2, [x0,8]
	mov	w2, 163
	str	w2, [x0,4]
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	add	x1, x2, x1, uxtb
	ldrsb	w1, [x1,1072]
	str	w1, [x0]
	str	wzr, [x0]
	str	wzr, [x0]
	str	wzr, [x0]
	bl	nandc_wait_flash_ready
	ldp	x29, x30, [sp], 16
	ret
	.size	ymtc_3d_set_slc_rr_para, .-ymtc_3d_set_slc_rr_para
	.align	2
	.global	flash_start_plane_read
	.type	flash_start_plane_read, %function
flash_start_plane_read:
	stp	x29, x30, [sp, -96]!
	mov	w3, 24
	mov	w2, 1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x4, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ubfx	x23, x0, 24, 2
	ldrb	w5, [x4,1257]
	sub	w3, w3, w5
	lsl	w21, w2, w3
	lsl	w2, w2, w5
	lsr	w19, w0, w3
	sub	w2, w2, #1
	sub	w21, w21, #1
	and	w19, w19, w2
	and	w22, w21, w0
	ldrb	w0, [x4,1145]
	uxtb	w19, w19
	and	w21, w21, w1
	cmp	w0, w19
	bhi	.L389
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1128
	add	x1, x1, 1088
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L389:
	add	x26, x20, :lo12:.LANCHOR0
	add	x19, x26, x19, sxtw
	ldrb	w24, [x19,1248]
	mov	w0, w24
	ldr	x25, [x26,1048]
	bl	nandc_cs
	cbnz	w23, .L390
	mov	w0, w22
	bl	slc_phy_page_address_calc
	mov	w22, w0
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L391
	mov	w0, w24
	bl	zftl_flash_enter_slc_mode
	b	.L391
.L390:
	ldr	x0, [x26,1136]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L392
	ldrb	w0, [x26,1268]
	cbnz	w0, .L392
	ldrb	w0, [x26,1269]
	cbnz	w0, .L392
	sxtw	x0, w24
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w23, [x0,8]
	b	.L391
.L392:
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
.L391:
	add	x0, x20, :lo12:.LANCHOR0
	uxtb	x19, w24
	and	w4, w22, 255
	lsr	w3, w22, 8
	and	w28, w21, 255
	lsr	w27, w21, 8
	ldrb	w2, [x0,1168]
	lsr	w26, w21, 16
	cmp	w2, 1
	lsr	w2, w22, 16
	bne	.L393
	add	x19, x25, x19, lsl 8
	ldrb	w1, [x0,1160]
	ldrb	w0, [x0,1256]
	str	w1, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w4, [x19,2052]
	str	w3, [x19,2052]
	str	w2, [x19,2052]
	cbz	w0, .L394
	lsr	w22, w22, 24
	str	w22, [x19,2052]
.L394:
	add	x22, x20, :lo12:.LANCHOR0
	ldrb	w0, [x22,1161]
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	cbz	w23, .L395
	ldr	x0, [x22,1136]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L395
	str	w23, [x19,2056]
.L395:
	str	wzr, [x19,2056]
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w28, [x19,2052]
	str	w27, [x19,2052]
	ldrb	w0, [x20,1256]
	str	w26, [x19,2052]
	cbz	w0, .L421
	lsr	w21, w21, 24
	str	w21, [x19,2052]
	b	.L421
.L393:
	ldr	x1, [x0,1136]
	ldrb	w0, [x1,7]
	cmp	w0, 1
	bne	.L398
	ldrb	w0, [x1,12]
	cmp	w0, 2
	bne	.L398
	sxtw	x0, w24
	mov	w5, 38
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w5, [x0,8]
.L398:
	add	x0, x20, :lo12:.LANCHOR0
	add	x19, x25, x19, lsl 8
	ldrb	w0, [x0,1160]
	str	w0, [x19,2056]
	str	w4, [x19,2052]
	str	w3, [x19,2052]
	str	w2, [x19,2052]
	cbz	w23, .L399
	ldrb	w0, [x1,12]
	cmp	w0, 3
	bne	.L399
	str	w23, [x19,2056]
.L399:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1161]
	str	w0, [x19,2056]
	str	w28, [x19,2052]
	str	w27, [x19,2052]
	str	w26, [x19,2052]
.L421:
	mov	w0, 48
	str	w0, [x19,2056]
	mov	w0, w24
	bl	nandc_de_cs
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_start_plane_read, .-flash_start_plane_read
	.align	2
	.global	flash_set_interface_mode
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	adrp	x23, .LC38
	adrp	x24, .LC37
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w25, w0
	mov	x21, 0
	add	x22, x22, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC38
	add	x24, x24, :lo12:.LC37
.L437:
	add	x0, x22, x21, lsl 3
	ldr	x20, [x22,1048]
	adrp	x1, .LANCHOR0
	ldrb	w19, [x0,1272]
	cmp	w19, 152
	beq	.L423
	cmp	w19, 69
	beq	.L423
	cmp	w19, 155
	beq	.L423
	cmp	w19, 44
	cset	w2, eq
	cmp	w19, 137
	cset	w0, eq
	orr	w0, w2, w0
	cbz	w0, .L424
.L423:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w25, 1
	ldrb	w1, [x1,1304]
	bne	.L426
	tbz	x1, 0, .L424
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L427
	mov	x0, x24
	bl	printk
.L427:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L438
	cmp	w19, 44
	bne	.L428
.L438:
	mov	w0, 1
	b	.L459
.L428:
	cmp	w19, 155
	mov	w0, 1
	bne	.L436
.L459:
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L457
.L426:
	tbz	x1, 2, .L424
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L432
	mov	x0, x23
	bl	printk
.L432:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L439
	cmp	w19, 44
	cset	w0, eq
	cbz	w0, .L433
.L439:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L457
.L433:
	cmp	w19, 155
	bne	.L436
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 37
	b	.L457
.L436:
	mov	w2, 128
	str	w2, [x1,2052]
.L457:
	str	w0, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L424:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L437
	bl	nandc_wait_flash_ready
	ldr	x25, [sp,64]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_set_interface_mode, .-flash_set_interface_mode
	.align	2
	.type	mt_auto_read_calibration_config, %function
mt_auto_read_calibration_config:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	ubfiz	x19, x19, 8, 8
	str	x1, [x29,40]
	bl	nandc_wait_flash_ready
	adrp	x0, .LANCHOR0+1048
	ldr	x0, [x0,#:lo12:.LANCHOR0+1048]
	add	x19, x0, x19
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 150
	str	w0, [x19,2052]
	mov	w0, 200
	bl	timer_delay_ns
	ldr	x1, [x29,40]
	str	w1, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	mt_auto_read_calibration_config, .-mt_auto_read_calibration_config
	.align	2
	.global	flash_reset
	.type	flash_reset, %function
flash_reset:
	adrp	x1, .LANCHOR0+1048
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 8
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR0+1048]
	add	x0, x1, x0, lsl 8
	mov	w1, 255
	str	w1, [x0,8]
	bl	nandc_wait_flash_ready
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_reset, .-flash_reset
	.align	2
	.global	flash_read_id
	.type	flash_read_id, %function
flash_read_id:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR0+1048
	stp	x19, x20, [sp,16]
	mov	x19, x1
	ldr	x20, [x0,#:lo12:.LANCHOR0+1048]
	mov	w0, w21
	bl	flash_reset
	mov	w0, w21
	bl	nandc_cs
	ubfiz	x2, x21, 8, 8
	mov	w0, 144
	add	x20, x20, x2
	str	w0, [x20,2056]
	mov	w0, 200
	str	wzr, [x20,2052]
	bl	timer_delay_ns
	ldr	w0, [x20,2048]
	strb	w0, [x19]
	ldr	w0, [x20,2048]
	strb	w0, [x19,1]
	ldr	w0, [x20,2048]
	strb	w0, [x19,2]
	ldr	w0, [x20,2048]
	strb	w0, [x19,3]
	ldr	w0, [x20,2048]
	strb	w0, [x19,4]
	ldr	w0, [x20,2048]
	strb	w0, [x19,5]
	ldr	w0, [x20,2048]
	strb	w0, [x19,6]
	ldr	w0, [x20,2048]
	strb	w0, [x19,7]
	mov	w0, w21
	bl	nandc_de_cs
	ldrb	w2, [x19]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L462
	adrp	x0, .LC39
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC39
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L462:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_read_id, .-flash_read_id
	.align	2
	.global	flash_read_spare
	.type	flash_read_spare, %function
flash_read_spare:
	stp	x29, x30, [sp, -48]!
	adrp	x4, .LANCHOR0+1048
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x3, .LANCHOR2+17
	ldr	x19, [x4,#:lo12:.LANCHOR0+1048]
	str	x2, [x29,40]
	add	x19, x19, x0
	ldrb	w3, [x3,#:lo12:.LANCHOR2+17]
	and	w0, w1, 255
	lsl	w3, w3, 9
	str	wzr, [x19,2056]
	str	w3, [x19,2052]
	lsr	w3, w3, 8
	str	w3, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w1, 8
	str	w0, [x19,2052]
	lsr	w1, w1, 16
	str	w1, [x19,2052]
	mov	w0, 48
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	ldr	x2, [x29,40]
	ldr	w0, [x19,2048]
	strb	w0, [x2]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_read_spare, .-flash_read_spare
	.align	2
	.global	flash_read_otp_data
	.type	flash_read_otp_data, %function
flash_read_otp_data:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR0+1048
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	mov	w20, w1
	ldr	x19, [x0,#:lo12:.LANCHOR0+1048]
	mov	w0, w21
	mov	x23, x2
	mov	w22, 144
	bl	nandc_cs
	ubfiz	x3, x21, 8, 8
	mov	w0, 239
	add	x19, x19, x3
	str	w0, [x19,2056]
	mov	w0, 50
	str	w22, [x19,2052]
	bl	timer_delay_ns
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	nandc_wait_flash_ready
	mov	w0, 238
	str	w0, [x19,2056]
	str	w22, [x19,2052]
	adrp	x0, .LC40
	mov	w1, w20
	add	x0, x0, :lo12:.LC40
	ldr	w2, [x19,2048]
	ldr	w3, [x19,2048]
	ldr	w4, [x19,2048]
	bl	printk
	bl	nandc_wait_flash_ready
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	and	w0, w20, 255
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w20, 8
	str	w0, [x19,2052]
	lsr	w20, w20, 16
	str	w20, [x19,2052]
	mov	w0, 48
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	mov	x1, 0
.L466:
	ldr	w0, [x19,2048]
	strb	w0, [x23,x1]
	add	x1, x1, 1
	cmp	x1, 16384
	bne	.L466
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 144
	str	w0, [x19,2052]
	mov	w0, 50
	bl	timer_delay_ns
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	mov	w0, w21
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	nandc_de_cs
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_read_otp_data, .-flash_read_otp_data
	.align	2
	.global	sandisk_prog_test_bad_block
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR0+1048
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x2,#:lo12:.LANCHOR0+1048]
	add	x2, x20, :lo12:.LANCHOR2
	str	x21, [sp,32]
	ldrb	w3, [x2,37]
	sxtw	x2, w0
	add	x2, x2, 8
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L482
	mov	w3, 162
.L482:
	ubfiz	x0, x0, 8, 8
	str	w3, [x2,8]
	add	x19, x19, x0
	mov	w0, 128
	str	x1, [x29,56]
	str	w0, [x19,2056]
	and	w0, w1, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w1, 8
	str	w0, [x19,2052]
	lsr	w0, w1, 16
	str	w0, [x19,2052]
	mov	w0, 16
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	mov	w0, 112
	str	w0, [x19,2056]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w2, [x19,2048]
	mov	w0, 5
	ldr	x1, [x29,56]
	ands	w21, w2, w0
	beq	.L471
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L471
	adrp	x0, .LC41
	add	x0, x0, :lo12:.LC41
	bl	printk
.L471:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,38]
	cbz	w0, .L472
	str	w0, [x19,2056]
.L472:
	mov	w0, w21
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	sandisk_prog_test_bad_block, .-sandisk_prog_test_bad_block
	.align	2
	.global	nandc_rdy_status
	.type	nandc_rdy_status, %function
nandc_rdy_status:
	adrp	x0, .LANCHOR0+1048
	sub	sp, sp, #16
	ldr	x0, [x0,#:lo12:.LANCHOR0+1048]
	ldr	w0, [x0]
	str	w0, [sp,8]
	ldr	w0, [sp,8]
	add	sp, sp, 16
	ubfx	x0, x0, 9, 1
	ret
	.size	nandc_rdy_status, .-nandc_rdy_status
	.align	2
	.global	nandc_bch_sel
	.type	nandc_bch_sel, %function
nandc_bch_sel:
	sub	sp, sp, #16
	str	wzr, [sp,8]
	ldr	w1, [sp,8]
	orr	w1, w1, 1
	str	w1, [sp,8]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,1028]
	strb	w0, [x1,1236]
	cmp	w2, 9
	ldr	x2, [x1,1048]
	ldr	w1, [sp,8]
	bne	.L485
	str	w1, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L486
	cmp	w0, 60
	mov	w1, 3
	beq	.L486
	cmp	w0, 40
	mov	w1, 2
	csinc	w1, w1, wzr, eq
.L486:
	str	wzr, [sp]
	ldr	w0, [sp]
	bfi	w0, w1, 25, 3
	str	w0, [sp]
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,32]
	b	.L484
.L485:
	str	w1, [x2,8]
	mov	w3, 16
	str	wzr, [sp]
	cmp	w0, w3
	ldr	w1, [sp]
	bfi	w1, w3, 8, 8
	str	w1, [sp]
	ldr	w1, [sp]
	and	w1, w1, -262145
	str	w1, [sp]
	beq	.L494
	cmp	w0, 24
	bne	.L490
	ldr	w0, [sp]
	orr	w0, w0, 16
	b	.L495
.L490:
	ldr	w1, [sp]
	cmp	w0, 40
	orr	w1, w1, 262144
	str	w1, [sp]
	ldr	w1, [sp]
	orr	w1, w1, 16
	str	w1, [sp]
	bne	.L489
.L494:
	ldr	w0, [sp]
	and	w0, w0, -17
.L495:
	str	w0, [sp]
.L489:
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,12]
.L484:
	add	sp, sp, 16
	ret
	.size	nandc_bch_sel, .-nandc_bch_sel
	.align	2
	.global	zftl_flash_resume
	.type	zftl_flash_resume, %function
zftl_flash_resume:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x23, [sp,48]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	ldrb	w1, [x0,1028]
	ldr	w2, [x0,1056]
	cmp	w1, 9
	ldr	x1, [x0,1048]
	str	w2, [x1]
	ldr	x1, [x0,1048]
	ldr	w2, [x0,1060]
	str	w2, [x1,4]
	ldr	x1, [x0,1048]
	ldr	w2, [x0,1064]
	bne	.L497
	str	w2, [x1,16]
	ldr	w2, [x0,1068]
	str	w2, [x1,32]
	ldr	w2, [x0,1072]
	str	w2, [x1,80]
	ldr	w2, [x0,1076]
	str	w2, [x1,84]
	ldr	w2, [x0,1080]
	str	w2, [x1,520]
	ldr	w0, [x0,1084]
	str	w0, [x1,8]
	b	.L498
.L497:
	str	w2, [x1,8]
	ldr	w2, [x0,1068]
	str	w2, [x1,12]
	ldr	w2, [x0,1072]
	str	w2, [x1,304]
	ldr	w2, [x0,1076]
	str	w2, [x1,308]
	ldr	w2, [x0,1080]
	str	w2, [x1,336]
	ldr	w0, [x0,1084]
	str	w0, [x1,344]
.L498:
	mov	x19, 0
	add	x21, x20, :lo12:.LANCHOR0
	mov	w23, 2
.L500:
	add	x0, x21, x19, lsl 3
	ldrb	w0, [x0,1272]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L499
	uxtb	w22, w19
	mov	w0, w22
	bl	flash_reset
	add	x0, x21, 1264
	strb	w23, [x19,x0]
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L499:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L500
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1184]
	cbz	w0, .L501
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,1072]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L501:
	adrp	x0, .LANCHOR2+28
	ldrb	w0, [x0,#:lo12:.LANCHOR2+28]
	bl	nandc_bch_sel
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	zftl_flash_resume, .-zftl_flash_resume
	.align	2
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	stp	x29, x30, [sp, -16]!
	mov	w1, 1
	add	x29, sp, 0
	bl	nandc_irq_disable
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_flash_ready, .-rk_nandc_flash_ready
	.align	2
	.global	nandc_iqr_wait_flash_ready
	.type	nandc_iqr_wait_flash_ready, %function
nandc_iqr_wait_flash_ready:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,1048]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	w1, [x19,1028]
	ldr	x0, [x19,1048]
	cmp	w1, 9
	bne	.L508
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L509
.L508:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L509:
	ldr	w1, [x0]
	tbnz	x1, 9, .L510
	bl	wait_for_nand_flash_ready
	b	.L507
.L510:
	mov	w1, 1
	bl	nandc_irq_disable
.L507:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w20, w1
	mov	w24, w2
	ldrb	w0, [x0,1145]
	mov	w23, w3
	cmp	w0, w19
	bhi	.L513
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 688
	add	x1, x1, 1112
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L513:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1145]
	cmp	w2, w19
	bls	.L514
	adrp	x0, .LANCHOR2
	add	x19, x1, x19, sxtw
	ldrb	w25, [x19,1248]
	ldr	x19, [x1,1048]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	x22, x25
	add	x25, x25, 8
	add	x25, x19, x25, lsl 8
	tbz	x0, 4, .L515
	adrp	x0, .LC42
	mov	w1, w22
	add	x0, x0, :lo12:.LC42
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L515:
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	nandc_cs
	mov	w0, w22
	cbnz	w20, .L516
	bl	zftl_flash_enter_slc_mode
	b	.L517
.L516:
	bl	zftl_flash_exit_slc_mode
.L517:
	ubfiz	x4, x22, 8, 8
	mov	w0, 96
	add	x19, x19, x4
	str	w0, [x19,2056]
	and	w0, w24, 255
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w0, w24, 16
	str	w0, [x19,2052]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1256]
	cbz	w0, .L518
	lsr	w0, w24, 24
	str	w0, [x19,2052]
.L518:
	add	x0, x21, :lo12:.LANCHOR0
	mov	w20, 0
	ldrb	w0, [x0,1305]
	cbnz	w0, .L519
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x25
	bl	flash_read_status
	and	w20, w0, w20
.L519:
	mov	w0, 96
	add	x21, x21, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w23, 255
	str	w0, [x19,2052]
	lsr	w0, w23, 8
	str	w0, [x19,2052]
	lsr	w0, w23, 16
	str	w0, [x19,2052]
	ldrb	w0, [x21,1256]
	cbz	w0, .L520
	lsr	w23, w23, 24
	str	w23, [x19,2052]
.L520:
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w19, 5
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x25
	bl	flash_read_status
	mov	w2, w0
	mov	w0, w22
	str	x2, [x29,88]
	bl	nandc_de_cs
	ldr	x2, [x29,88]
	and	w19, w2, w19
	orr	w19, w19, w20
	cbz	w19, .L521
	adrp	x0, .LC43
	mov	w1, w24
	add	x0, x0, :lo12:.LC43
	bl	printk
.L521:
	mov	w0, w19
.L514:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_erase_duplane_block, .-flash_erase_duplane_block
	.align	2
	.global	flash_erase_block_en
	.type	flash_erase_block_en, %function
flash_erase_block_en:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp,64]
	uxtb	w25, w1
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	w22, w0
	mov	w0, 24
	ldrb	w19, [x1,1257]
	mov	w23, w2
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ldrb	w0, [x1,1145]
	sub	w19, w19, #1
	cmp	w0, w22
	and	w19, w19, w2
	bhi	.L537
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 750
	add	x1, x1, 1144
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L537:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1145]
	cmp	w2, w22
	bls	.L538
	add	x0, x1, x22, sxtw
	ldrb	w24, [x0,1248]
	adrp	x0, .LANCHOR2
	ldr	x26, [x1,1048]
	mov	x21, x24
	add	x24, x24, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x24, x26, x24, lsl 8
	tbz	x0, 4, .L539
	adrp	x0, .LC44
	mov	w1, w21
	add	x0, x0, :lo12:.LC44
	mov	w2, w23
	mov	w3, w25
	bl	printk
.L539:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L540
	bl	zftl_flash_enter_slc_mode
	b	.L541
.L540:
	bl	zftl_flash_exit_slc_mode
.L541:
	ubfiz	x3, x21, 8, 8
	mov	w0, 96
	add	x3, x26, x3
	str	w0, [x3,2056]
	and	w0, w19, 255
	str	w0, [x3,2052]
	lsr	w0, w19, 8
	str	w0, [x3,2052]
	lsr	w0, w19, 16
	str	w0, [x3,2052]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1256]
	cbz	w0, .L542
	lsr	w19, w19, 24
	str	w19, [x3,2052]
.L542:
	mov	w0, 208
	str	w0, [x3,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x24
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w21
	bl	nandc_de_cs
	mov	w3, 5
	ands	w3, w19, w3
	beq	.L543
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	mov	w1, w22
	ldrh	w2, [x20,2]
	udiv	w2, w23, w2
	bl	printk
	mov	w3, -1
.L543:
	mov	w0, w3
.L538:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_erase_block_en, .-flash_erase_block_en
	.align	2
	.global	flash_erase_block
	.type	flash_erase_block, %function
flash_erase_block:
	stp	x29, x30, [sp, -16]!
	mov	w2, w1
	mov	w1, 0
	add	x29, sp, 0
	bl	flash_erase_block_en
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_erase_block, .-flash_erase_block
	.align	2
	.global	flash_erase_all
	.type	flash_erase_all, %function
flash_erase_all:
	stp	x29, x30, [sp, -64]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w19, 0
	ldrb	w1, [x0,21]
	ldrh	w20, [x0,22]
	adrp	x0, .LANCHOR0
	add	x23, x0, :lo12:.LANCHOR0
	mov	x21, x0
	mul	w20, w1, w20
	uxth	w20, w20
.L556:
	ldrb	w0, [x23,1145]
	cmp	w0, w19
	bls	.L560
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,1248]
	mov	w22, 0
.L557:
	cmp	w20, w22, uxth
	bls	.L561
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L557
.L561:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L556
.L560:
	adrp	x0, .LC46
	mov	w1, 0
	add	x0, x0, :lo12:.LC46
	bl	printk
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_erase_all, .-flash_erase_all
	.align	2
	.global	rk_nandc_flash_xfer_completed
	.type	rk_nandc_flash_xfer_completed, %function
rk_nandc_flash_xfer_completed:
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	add	x29, sp, 0
	bl	nandc_irq_disable
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
	.align	2
	.global	nandc_xfer_start
	.type	nandc_xfer_start, %function
nandc_xfer_start:
	stp	x29, x30, [sp, -80]!
	uxtb	w1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x4, x20, :lo12:.LANCHOR0
	uxtb	w21, w0
	mov	x0, x2
	and	w6, w21, 1
	add	w5, w1, 1
	ldrb	w2, [x4,1028]
	cmp	w2, 9
	bne	.L564
	str	wzr, [x29,56]
	mov	w2, 1
	ubfx	x5, x5, 1, 6
	ldr	w1, [x29,56]
	bfi	w1, w6, 1, 1
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	orr	w1, w1, 8
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	bfi	w1, w2, 5, 2
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	orr	w1, w1, 536870912
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	orr	w1, w1, 1024
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	and	w1, w1, -17
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	bfi	w1, w5, 22, 6
	str	w1, [x29,56]
	ldr	w1, [x29,56]
	orr	w1, w1, 128
	str	w1, [x29,56]
	ldrb	w1, [x4,1185]
	cbz	w1, .L565
	ldrb	w1, [x4,1232]
	cbz	w1, .L565
	ldr	w1, [x29,56]
	orr	w1, w1, 512
	str	w1, [x29,56]
.L565:
	ldr	w1, [x29,56]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	str	x3, [x20,1208]
	str	x0, [x20,1200]
	bl	rknand_dma_map_single
	str	w0, [x20,1216]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,1208]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	bl	rknand_dma_map_single
	str	w0, [x20,1220]
	mov	w1, 1
	str	w1, [x20,1224]
	ldr	x1, [x20,1048]
	cmp	w21, wzr
	ldr	w2, [x20,1216]
	str	w2, [x1,52]
	mov	w2, 16
	str	w0, [x1,56]
	ldr	w0, [x1,48]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 9, 5
	str	w0, [x29,64]
	mov	w2, 2
	ldr	w0, [x29,64]
	orr	w0, w0, 448
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 3, 3
	str	w0, [x29,64]
	cset	w2, eq
	ldr	w0, [x29,64]
	orr	w0, w0, 4
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 1, 1
	str	w0, [x29,64]
	ldrh	w2, [x20,1234]
	ldr	w0, [x29,64]
	orr	w0, w0, 1
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 16, 11
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	str	w0, [x1,48]
	ldr	w0, [x29,56]
	str	w0, [x1,16]
	ldr	w0, [x29,56]
	orr	w0, w0, 4
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	str	w0, [x1,16]
	b	.L563
.L564:
	ldr	x2, [x4,1048]
	mov	w7, 16
	ubfx	x5, x5, 1, 6
	ldr	w2, [x2,12]
	str	w2, [x29,72]
	ldr	w2, [x29,72]
	bfi	w2, w7, 8, 8
	str	w2, [x29,72]
	ldr	w2, [x29,72]
	and	w2, w2, -9
	str	w2, [x29,72]
	ldr	w2, [x29,72]
	and	w2, w2, -225
	str	w2, [x29,72]
	str	wzr, [x29,56]
	ldr	w2, [x29,56]
	bfi	w2, w6, 1, 1
	str	w2, [x29,56]
	mov	w6, 1
	ldr	w2, [x29,56]
	orr	w2, w2, 8
	str	w2, [x29,56]
	ldr	w2, [x29,56]
	bfi	w2, w6, 5, 2
	str	w2, [x29,56]
	ldr	w2, [x29,56]
	orr	w2, w2, 536870912
	str	w2, [x29,56]
	ldr	w2, [x29,56]
	orr	w2, w2, 1024
	str	w2, [x29,56]
	ldr	w2, [x29,56]
	and	w2, w2, -17
	str	w2, [x29,56]
	ldr	w2, [x29,56]
	bfi	w2, w5, 22, 6
	str	w2, [x29,56]
	cbz	w21, .L567
	ldrb	w2, [x4,1236]
	ubfx	x1, x1, 1, 7
	mov	w5, 128
	cmp	w2, 25
	mov	w2, 64
	csel	w2, w2, w5, cc
	add	x1, x3, x1, lsl 2
	mov	w5, 0
.L569:
	cmp	x3, x1
	add	w7, w5, w2
	beq	.L571
	ldrh	w6, [x3,2]
	and	x5, x5, 4294967292
	ldr	x8, [x4,1192]
	ldrh	w9, [x3],4
	orr	w6, w9, w6, lsl 16
	str	w6, [x8,x5]
	mov	w5, w7
	b	.L569
.L567:
	ldr	x1, [x4,1192]
	str	w6, [x1]
.L571:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ldr	x1, [x20,1192]
	str	x1, [x20,1208]
	ldr	w1, [x29,56]
	str	x0, [x20,1200]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_map_single
	str	w0, [x20,1216]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,1208]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
	bl	rknand_dma_map_single
	str	w0, [x20,1220]
	ldr	x1, [x20,1048]
	mov	w0, 1
	str	w0, [x20,1224]
	mov	w2, 16
	ldr	w0, [x20,1216]
	cmp	w21, wzr
	str	w0, [x1,20]
	ldr	w0, [x20,1220]
	str	w0, [x1,24]
	str	wzr, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 9, 5
	str	w0, [x29,64]
	mov	w2, 2
	ldr	w0, [x29,64]
	orr	w0, w0, 448
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 3, 3
	str	w0, [x29,64]
	cset	w2, eq
	ldr	w0, [x29,64]
	orr	w0, w0, 4
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	bfi	w0, w2, 1, 1
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	orr	w0, w0, 1
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	str	w0, [x1,16]
	ldr	w0, [x29,72]
	str	w0, [x1,12]
	ldr	w0, [x29,56]
	str	w0, [x1,8]
	ldr	w0, [x29,56]
	orr	w0, w0, 4
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	str	w0, [x1,8]
.L563:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	nandc_xfer_start, .-nandc_xfer_start
	.align	2
	.global	nandc_set_seed
	.type	nandc_set_seed, %function
nandc_set_seed:
	adrp	x1, .LANCHOR1
	and	w0, w0, 127
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1168
	ldrh	w1, [x1,w0,uxtw 1]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	orr	w2, w1, -1073741824
	ldrb	w3, [x0,1232]
	cmp	w3, wzr
	csel	w1, w2, w1, ne
	ldrb	w2, [x0,1028]
	ldr	x0, [x0,1048]
	cmp	w2, 9
	bne	.L581
	str	w1, [x0,520]
	b	.L579
.L581:
	str	w1, [x0,336]
.L579:
	ret
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	zftl_flash_de_init
	.type	zftl_flash_de_init, %function
zftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	bl	nandc_wait_flash_ready
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cbz	w0, .L586
	ldrb	w0, [x1,1146]
	cbnz	w0, .L586
	ldrb	w1, [x1,1028]
	cmp	w1, 9
	beq	.L586
	bl	zftl_flash_exit_slc_mode
.L586:
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	hynix_reconfig_rr_para
	ldrb	w0, [x20,1184]
	cbz	w0, .L587
	ldrb	w0, [x20,1304]
	tbz	x0, 0, .L587
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,1184]
.L587:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,1232]
	cbz	w0, .L588
	mov	w0, 0
	strb	wzr, [x19,1232]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,1232]
.L588:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+1232
	strb	w0, [x1,#:lo12:.LANCHOR0+1232]
	ret
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	adrp	x1, .LANCHOR0+1048
	uxtb	x0, w0
	add	x0, x0, 8
	ldr	x1, [x1,#:lo12:.LANCHOR0+1048]
	add	x0, x1, x0, lsl 8
	ret
	.size	nandc_get_chip_if, .-nandc_get_chip_if
	.align	2
	.global	zbuf_init
	.type	zbuf_init, %function
zbuf_init:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x19, x0, :lo12:.LANCHOR0
	mov	w20, 0
	stp	x21, x22, [sp,32]
	add	x19, x19, 1312
	mov	x22, x0
.L607:
	add	x21, x22, :lo12:.LANCHOR0
	uxtb	w0, w20
	add	w1, w0, 1
	strb	w0, [x19,1]
	strb	w1, [x19]
	add	w20, w20, 1
	ldrb	w0, [x21,3360]
	add	x19, x19, 64
	strb	wzr, [x19,-62]
	str	xzr, [x19,-48]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x19,-56]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x19,-40]
	cmp	w20, 32
	bne	.L607
	strb	w20, [x21,3362]
	mov	w0, -1
	strb	wzr, [x21,3361]
	strb	w0, [x21,3296]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zbuf_init, .-zbuf_init
	.align	2
	.global	buf_reinit
	.type	buf_reinit, %function
buf_reinit:
	adrp	x0, .LANCHOR0
	mov	w2, 0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 1312
.L610:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L610
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0,3361]
	strb	w1, [x0,3296]
	strb	w2, [x0,3362]
	ret
	.size	buf_reinit, .-buf_reinit
	.align	2
	.global	buf_add_tail
	.type	buf_add_tail, %function
buf_add_tail:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	mov	w0, -1
	strb	w0, [x1]
	mov	x19, x1
	ldrb	w1, [x20]
	cmp	w1, 255
	bne	.L618
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L614
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 74
	add	x1, x1, 1424
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L614:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L612
.L618:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1312
.L619:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L619
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L616
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 81
	add	x1, x1, 1424
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L616:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 1312
	strb	w1, [x0,x20]
.L612:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_add_tail, .-buf_add_tail
	.align	2
	.type	queue_read_cmd, %function
queue_read_cmd:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	mov	w0, 48
	ldr	w1, [x19,40]
	bl	flash_start_page_read
	strb	wzr, [x19,59]
	mov	w0, 1
	strb	w0, [x19,58]
	mov	w0, -1
	strb	w0, [x19]
	adrp	x0, .LANCHOR0
	mov	x1, x19
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3363
	bl	buf_add_tail
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	queue_read_cmd, .-queue_read_cmd
	.align	2
	.global	zbuf_free
	.type	zbuf_free, %function
zbuf_free:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	ldrb	w1, [x0,2]
	and	w1, w1, 8
	uxtb	w1, w1
	strb	w1, [x0,2]
	cbz	w1, .L622
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L622
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 3361
	bl	buf_add_tail
	b	.L623
.L622:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,3361]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,3361]
.L623:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,3362]
	add	w0, w0, 1
	strb	w0, [x19,3362]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zbuf_free, .-zbuf_free
	.align	2
	.global	buf_alloc
	.type	buf_alloc, %function
buf_alloc:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x20, x1, :lo12:.LANCHOR0
	str	x21, [sp,32]
	uxtb	w21, w0
	mov	x19, x1
	ldrb	w0, [x20,3362]
	cbz	w0, .L631
.L634:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1312
	ldrb	w2, [x1,3361]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L632
	b	.L633
.L631:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 121
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w1, [x20,3362]
	cbnz	w1, .L634
.L636:
	mov	x0, 0
	b	.L635
.L632:
	ldrb	w1, [x1,3362]
	cmp	w1, 1
	beq	.L636
.L633:
	add	x1, x19, :lo12:.LANCHOR0
	lsl	x2, x2, 6
	add	x4, x1, 1312
	add	x3, x4, x2
	ldrb	w5, [x4,x2]
	strb	w5, [x1,3361]
	ldrb	w5, [x1,3362]
	strh	wzr, [x3,50]
	sub	w5, w5, #1
	strb	w5, [x1,3362]
	mov	w1, 1
	strb	w1, [x3,2]
	mov	w1, -1
	str	xzr, [x3,16]
	strb	w1, [x4,x2]
	mov	w1, -1
	strb	wzr, [x3,56]
	str	w1, [x3,36]
	strb	wzr, [x3,57]
.L635:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	buf_alloc, .-buf_alloc
	.align	2
	.global	buf_remove_buf
	.type	buf_remove_buf, %function
buf_remove_buf:
	ldrb	w4, [x1,1]
	ldrb	w2, [x0]
	cmp	w4, w2
	bne	.L643
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L647
.L643:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1312
.L645:
	cmp	w2, 255
	beq	.L648
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L645
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L647:
	mov	w0, 1
	b	.L642
.L648:
	mov	w0, 0
.L642:
	ret
	.size	buf_remove_buf, .-buf_remove_buf
	.align	2
	.global	buf_remove_free
	.type	buf_remove_free, %function
buf_remove_free:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	x20, x0
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3362]
	cbnz	w0, .L650
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 172
	add	x1, x1, 1456
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L650:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,3362]
	cbz	w0, .L649
	add	x0, x19, 3361
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L649
	ldrb	w0, [x19,3362]
	sub	w0, w0, #1
	strb	w0, [x19,3362]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L649:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_remove_free, .-buf_remove_free
	.align	2
	.global	dump_buf_info
	.type	dump_buf_info, %function
dump_buf_info:
	sub	sp, sp, #64
	adrp	x0, .LC47
	add	x0, x0, :lo12:.LC47
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	str	x21, [sp,48]
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x21, .LC53
	add	x20, x19, 1312
	add	x21, x21, :lo12:.LC53
	add	x19, x19, 3360
	ldrb	w1, [x19,3]
	bl	printk
	ldrb	w1, [x19,4]
	adrp	x0, .LC48
	add	x0, x0, :lo12:.LC48
	bl	printk
	ldrb	w1, [x19,5]
	adrp	x0, .LC49
	add	x0, x0, :lo12:.LC49
	bl	printk
	ldrb	w1, [x19,6]
	adrp	x0, .LC50
	add	x0, x0, :lo12:.LC50
	bl	printk
	ldrb	w1, [x19,1]
	adrp	x0, .LC51
	add	x0, x0, :lo12:.LC51
	bl	printk
	ldrb	w1, [x19,2]
	adrp	x0, .LC52
	add	x0, x0, :lo12:.LC52
	bl	printk
.L655:
	ldr	w0, [x20,40]
	add	x20, x20, 64
	ldrb	w1, [x20,-63]
	ldrb	w2, [x20,-64]
	ldrb	w3, [x20,-62]
	ldrh	w4, [x20,-14]
	ldrb	w5, [x20,-6]
	str	w0, [sp]
	mov	x0, x21
	ldr	w6, [x20,-12]
	ldr	w7, [x20,-28]
	bl	printk
	cmp	x20, x19
	bne	.L655
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldr	x21, [sp,48]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 64
	ret
	.size	dump_buf_info, .-dump_buf_info
	.align	2
	.global	flash_check_bad_block
	.type	flash_check_bad_block, %function
flash_check_bad_block:
	adrp	x2, .LANCHOR2
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR2
	add	x2, x2, 8
	ldrb	w3, [x2,13]
	ldrh	w2, [x2,14]
	mul	w2, w3, w2
	ubfx	x3, x1, 5, 27
	uxth	w2, w2
	add	w2, w2, 31
	asr	w2, w2, 5
	lsl	w2, w2, 2
	mul	w2, w0, w2
	adrp	x0, .LANCHOR0+1240
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+1240]
	add	x2, x2, 912
	add	x2, x0, x2
	ldr	w0, [x2,x3,lsl 2]
	lsr	w0, w0, w1
	and	w0, w0, 1
	ret
	.size	flash_check_bad_block, .-flash_check_bad_block
	.align	2
	.global	flash_mask_bad_block
	.type	flash_mask_bad_block, %function
flash_mask_bad_block:
	stp	x29, x30, [sp, -48]!
	adrp	x3, .LANCHOR2
	add	x29, sp, 0
	add	x3, x3, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	uxtb	w19, w0
	mov	w21, w1
	mov	w1, w19
	ldrb	w0, [x3,21]
	mov	w2, w21
	ldrh	w20, [x3,22]
	mul	w20, w0, w20
	adrp	x0, .LC54
	add	x0, x0, :lo12:.LC54
	uxth	w20, w20
	add	w20, w20, 31
	bl	printk
	asr	w20, w20, 5
	adrp	x0, .LANCHOR0+1240
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+1240]
	sxtw	x19, w19
	add	x19, x19, 912
	add	x19, x0, x19
	mov	w0, 1
	lsl	w1, w0, w21
	ldr	w0, [x19,x3]
	orr	w1, w0, w1
	str	w1, [x19,x3]
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_mask_bad_block, .-flash_mask_bad_block
	.align	2
	.global	str2hex
	.type	str2hex, %function
str2hex:
	ldrb	w1, [x0]
	cmp	w1, 48
	bne	.L660
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L660:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L662:
	ldrb	w2, [x0]
	cbz	w2, .L669
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L663
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L664
.L663:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L665
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L664
.L665:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L669
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L664:
	add	x0, x0, 1
	b	.L662
.L669:
	mov	w0, w1
	ret
	.size	str2hex, .-str2hex
	.align	2
	.global	zftl_proc_debug_init
	.type	zftl_proc_debug_init, %function
zftl_proc_debug_init:
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR1
	mov	x2, 0
	adrp	x0, .LC55
	add	x3, x3, :lo12:.LANCHOR1
	add	x29, sp, 0
	mov	w1, 292
	add	x3, x3, 1472
	mov	x4, x2
	add	x0, x0, :lo12:.LC55
	bl	proc_create_data
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_proc_debug_init, .-zftl_proc_debug_init
	.align	2
	.global	ftl_print_info_to_buf
	.type	ftl_print_info_to_buf, %function
ftl_print_info_to_buf:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LC1
	adrp	x2, .LC2
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC1
	add	x2, x2, :lo12:.LC2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x20, x0
	bl	sprintf
	adrp	x7, .LANCHOR2
	adrp	x22, .LANCHOR0
	add	x7, x7, :lo12:.LANCHOR2
	adrp	x1, .LC56
	add	x7, x7, 8
	add	x21, x22, :lo12:.LANCHOR0
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC56
	ldrb	w5, [x7,4]
	mov	x0, x19
	ldrb	w6, [x7,5]
	ldrb	w3, [x7,2]
	ldrb	w4, [x7,3]
	ldrb	w2, [x7,1]
	ldrb	w7, [x7,6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,1032]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,3368]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x1, .LC59
	mov	x0, x19
	add	x1, x1, :lo12:.LC59
	add	x19, x19, 10
	bl	strcpy
	ldr	w2, [x21,3372]
	adrp	x1, .LC60
	mov	x0, x19
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,1032]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3376]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3378]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3380]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3382]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3384]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3386]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w4, [x21,3389]
	adrp	x1, .LC70
	ldrb	w2, [x21,3362]
	mov	x0, x19
	ldrb	w3, [x21,3388]
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x3, [x21,3392]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,3392]
	adrp	x1, .LC72
	add	x1, x1, :lo12:.LC72
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,3392]
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,1120]
	mov	w2, 10
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w2, [x22,#:lo12:.LANCHOR0]
	adrp	x1, .LC77
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC78
	mov	x0, x19
	add	x1, x1, :lo12:.LC78
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC79
	mov	x0, x19
	add	x1, x1, :lo12:.LC79
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC80
	mov	x0, x19
	add	x1, x1, :lo12:.LC80
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC81
	mov	x0, x19
	add	x1, x1, :lo12:.LC81
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC82
	mov	x0, x19
	add	x1, x1, :lo12:.LC82
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC83
	mov	x0, x19
	add	x1, x1, :lo12:.LC83
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC84
	mov	x0, x19
	add	x1, x1, :lo12:.LC84
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC85
	mov	x0, x19
	add	x1, x1, :lo12:.LC85
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC86
	mov	x0, x19
	add	x1, x1, :lo12:.LC86
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,3392]
	adrp	x1, .LC87
	mov	x0, x19
	add	x1, x1, :lo12:.LC87
	ldrh	w2, [x6,74]
	ldr	w3, [x6,84]
	ldrh	w4, [x6,88]
	ldrh	w5, [x6,92]
	ldrh	w6, [x6,96]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,3392]
	adrp	x1, .LC88
	mov	x0, x19
	add	x1, x1, :lo12:.LC88
	ldr	w3, [x6,80]
	ldrh	w4, [x6,90]
	ldrh	w5, [x6,94]
	ldrh	w2, [x6,72]
	ldrh	w6, [x6,98]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,3400]
	adrp	x1, .LC89
	mov	x0, x19
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w6, [x21,3412]
	adrp	x1, .LC90
	ldrh	w2, [x21,3402]
	mov	x0, x19
	ldrh	w3, [x21,3404]
	add	x1, x1, :lo12:.LC90
	ldrh	w4, [x21,3406]
	ldrh	w5, [x21,3408]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x5, [x21,1120]
	adrp	x1, .LC91
	mov	x0, x19
	add	x1, x1, :lo12:.LC91
	ldrh	w2, [x5,584]
	ldrh	w3, [x5,586]
	ldrh	w4, [x5,588]
	ldrh	w5, [x5,590]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,1120]
	adrp	x1, .LC92
	ldr	x5, [x21,3392]
	mov	x0, x19
	add	x1, x1, :lo12:.LC92
	ldr	w2, [x6,536]
	ldr	w3, [x6,540]
	ldr	w4, [x6,544]
	ldr	w5, [x5,44]
	ldr	w6, [x6,548]
	bl	sprintf
	add	x0, x19, x0, sxtw
	ldp	x21, x22, [sp,32]
	sub	w0, w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_print_info_to_buf, .-ftl_print_info_to_buf
	.align	2
	.global	zftl_proc_ftl_read
	.type	zftl_proc_ftl_read, %function
zftl_proc_ftl_read:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_print_info_to_buf
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_proc_ftl_read, .-zftl_proc_ftl_read
	.align	2
	.global	ftl_gc_write_buf
	.type	ftl_gc_write_buf, %function
ftl_gc_write_buf:
	stp	x29, x30, [sp, -32]!
	mov	x1, x0
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2]
	orr	w0, w0, 2
	strb	w0, [x1,2]
	add	x0, x19, 3416
	bl	buf_add_tail
	ldrb	w0, [x19,3388]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,3388]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_gc_write_buf, .-ftl_gc_write_buf
	.align	2
	.global	gc_hook
	.type	gc_hook, %function
gc_hook:
	ret
	.size	gc_hook, .-gc_hook
	.align	2
	.global	vpn_check
	.type	vpn_check, %function
vpn_check:
	ret
	.size	vpn_check, .-vpn_check
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	ret
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	gc_add_sblk
	.type	gc_add_sblk, %function
gc_add_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	uxth	w23, w1
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	adrp	x22, .LANCHOR0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	uxth	w20, w2
	tbz	x0, 8, .L678
	add	x7, x22, :lo12:.LANCHOR0
	uxtw	x5, w21
	mov	w1, w21
	mov	w2, w23
	mov	w3, w20
	ldr	x0, [x7,1096]
	ldr	x6, [x7,1112]
	add	x0, x0, x5, lsl 2
	ldrh	w5, [x6,x5,lsl 1]
	ldrb	w4, [x0,2]
	adrp	x0, .LC93
	ldrh	w6, [x7,3480]
	add	x0, x0, :lo12:.LC93
	ldrh	w7, [x7,3406]
	ubfx	x4, x4, 5, 3
	bl	printk
.L678:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,1088]
	cmp	w0, w21
	bhi	.L679
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 543
	add	x1, x1, 1760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L679:
	add	x5, x22, :lo12:.LANCHOR0
	ldrh	w0, [x5,1088]
	cmp	w0, w21
	bhi	.L680
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x0, 10, .L681
	adrp	x0, .LC94
	mov	w1, w21
	mov	w2, w23
	mov	w3, w20
	add	x0, x0, :lo12:.LC94
	bl	printk
	b	.L681
.L680:
	uxtw	x0, w21
	ldr	x1, [x5,1112]
	ldrh	w4, [x1,x0,lsl 1]
	ldr	x1, [x5,1096]
	add	x0, x1, x0, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L682
	cbz	w4, .L681
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 553
	add	x1, x1, 1760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L681
.L682:
	ldrh	w1, [x5,3424]
	add	x8, x5, 3424
	mov	w19, 0
	cmp	w1, w21
	beq	.L681
	ldr	x7, [x5,1120]
	ldrh	w1, [x7,48]
	cmp	w1, w21
	beq	.L681
	ldrh	w1, [x7,16]
	cmp	w1, w21
	beq	.L681
	ldrh	w1, [x7,80]
	cmp	w1, w21
	beq	.L681
	ldrh	w5, [x8,56]
	mov	x6, 0
.L683:
	cmp	w5, w6
	bls	.L716
	add	x6, x6, 1
	add	x1, x8, x6, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L683
	mov	w19, 0
	b	.L681
.L716:
	cbnz	w23, .L685
	adrp	x6, .LANCHOR3
	mov	w19, w23
	add	x6, x6, :lo12:.LANCHOR3
	ldrh	w9, [x6,1208]
	cmp	w9, w21
	beq	.L681
	mov	x8, 0
.L687:
	add	x1, x6, 1216
	ldrh	w1, [x8,x1]
	cmp	w21, w1
	bne	.L686
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x1, 8, .L681
	ldrb	w2, [x0,2]
	adrp	x0, .LC95
	mov	w3, w4
	mov	w1, w21
	ubfx	x2, x2, 5, 3
	mov	w4, w21
	mov	w5, w9
	add	x0, x0, :lo12:.LC95
	bl	printk
	b	.L681
.L686:
	add	x8, x8, 2
	cmp	x8, 16
	bne	.L687
	sxtw	x6, w20
	add	x19, x7, x6, lsl 7
	add	x19, x19, 136
	b	.L689
.L685:
	add	x19, x7, 392
.L689:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L690
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w3, [x0,2]
	adrp	x0, .LC96
	mov	w1, w21
	add	x0, x0, :lo12:.LC96
	mov	w2, w23
	ldrh	w6, [x6,3406]
	ubfx	x3, x3, 5, 3
	bl	printk
.L690:
	mov	x4, x19
	add	x0, x19, 128
	mov	w1, 65535
.L693:
	ldrh	w2, [x4]
	cmp	w2, w1
	bne	.L691
	strh	w21, [x4]
	add	x22, x22, :lo12:.LANCHOR0
	cbz	w23, .L692
	ldr	x1, [x22,1120]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L715
.L692:
	ldr	x0, [x22,1120]
	add	x20, x0, x20, uxth 1
	ldrh	w0, [x20,120]
	add	w0, w0, 1
	strh	w0, [x20,120]
.L715:
	mov	w19, 1
	b	.L681
.L691:
	add	x4, x4, 2
	cmp	x4, x0
	bne	.L693
	b	.L715
.L681:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR3
	mov	w3, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	add	x21, x20, :lo12:.LANCHOR0
	add	x4, x4, :lo12:.LANCHOR3
	mov	w1, 24
	mov	w23, 1
	ldrb	w19, [x21,1257]
	add	x21, x21, 3424
	ldrh	w0, [x4,1232]
	sub	w19, w1, w19
	ldr	w1, [x21,2120]
	sub	w19, w19, w0
	lsr	w2, w3, w0
	lsl	w19, w23, w19
	ldrb	w0, [x4,1234]
	sub	w19, w19, #1
	uxth	w22, w2
	and	w19, w19, w2
	mov	w2, w22
	udiv	w19, w19, w0
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	printk
	mov	w1, w23
	mov	w2, 0
	uxth	w19, w19
	mov	w0, w19
	bl	gc_add_sblk
	ldr	w0, [x21,2120]
	mov	w1, 0
.L718:
	cmp	w1, w0
	bcs	.L721
	add	x2, x21, x1, sxtw 1
	ldrh	w2, [x2,2124]
	cmp	w2, w22
	beq	.L719
	add	w1, w1, 1
	uxth	w1, w1
	b	.L718
.L721:
	cmp	w0, 5
	bhi	.L719
	add	x20, x20, :lo12:.LANCHOR0
	add	w1, w0, 1
	str	w1, [x20,5544]
	add	x20, x20, x0, uxtw 1
	strh	w22, [x20,5548]
.L719:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_mark_bad_ppa, .-gc_mark_bad_ppa
	.align	2
	.global	gc_get_src_ppa_from_index
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	adrp	x1, .LANCHOR3+1240
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR3+1240]
	ldr	w0, [x1,x0]
	ret
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.type	gc_write_completed, %function
gc_write_completed:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x21, .LANCHOR1
	adrp	x26, .LANCHOR0
	add	x26, x26, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR1
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	add	x22, x26, 1312
	add	x21, x21, 1776
.L724:
	ldrb	w20, [x26,3365]
	adrp	x19, .LANCHOR0
	cmp	w20, 255
	beq	.L753
	lsl	x1, x20, 6
	add	x0, x22, x1
	ldrb	w1, [x22,x1]
	strb	w1, [x26,3365]
	ldr	w1, [x0,52]
	ldrh	w23, [x0,48]
	cbz	w1, .L725
	mov	w2, 1
	strh	w2, [x26,5536]
	ldr	w2, [x0,40]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	str	w2, [x26,5540]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 956
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L725:
	adrp	x25, .LANCHOR3
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w0, [x1,1248]
	cmp	w0, 3
	bne	.L726
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,1268]
	cbnz	w2, .L726
	ldr	x0, [x0,1120]
	ldrb	w1, [x1,1249]
	ldrb	w0, [x0,89]
	cmp	w1, w0
	bhi	.L739
	cmp	w0, 3
	mov	w1, 2
	csel	w0, w0, w1, cc
	b	.L728
.L739:
	mov	w0, 1
.L728:
	add	w24, w0, w0, lsl 1
	b	.L729
.L726:
	add	x0, x25, :lo12:.LANCHOR3
	mov	w24, 1
	ldrb	w1, [x0,1250]
	cbz	w1, .L729
	ldrb	w0, [x0,1251]
	cmp	w0, wzr
	mov	w0, 2
	csel	w24, w24, w0, eq
.L729:
	add	x0, x25, :lo12:.LANCHOR3
	uxth	x1, w23
	ldr	x2, [x0,1256]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, x20, lsl 6
	ldrb	w1, [x2,x1]
	ldrb	w0, [x0,1313]
	cmp	w1, w0
	beq	.L730
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 976
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L730:
	add	x0, x19, :lo12:.LANCHOR0
	add	x20, x0, x20, lsl 6
	ldrb	w1, [x20,1373]
	cmp	w1, 3
	beq	.L731
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w2, [x1,1248]
	cmp	w2, 3
	bne	.L731
	ldrb	w2, [x1,1264]
	cbnz	w2, .L731
	ldrb	w2, [x0,1268]
	cbnz	w2, .L731
	ldrb	w0, [x0,1269]
	cbnz	w0, .L731
	ldrb	w0, [x1,1250]
	cbz	w0, .L732
	ldrb	w0, [x1,1251]
	cbnz	w0, .L731
.L732:
	add	x25, x25, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x19, x19, 1312
	ldr	x2, [x25,1256]
	b	.L733
.L731:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 0
	mov	w27, -1
	add	x28, x19, 1312
	strh	w23, [x19,5532]
.L734:
	cmp	w24, w20, uxth
	bls	.L724
	add	x0, x25, :lo12:.LANCHOR3
	add	w2, w20, w23
	add	w20, w20, 1
	ldr	x0, [x0,1256]
	ldrb	w1, [x0,x2]
	strb	w27, [x0,x2]
	sbfiz	x1, x1, 6, 32
	add	x0, x28, x1
	strb	wzr, [x0,61]
	bl	zbuf_free
	ldrb	w0, [x19,3431]
	sub	w0, w0, #1
	strb	w0, [x19,3431]
	b	.L734
.L733:
	cmp	w24, w1, uxth
	bls	.L724
	add	w0, w1, w23
	add	w1, w1, 1
	ldrb	w0, [x2,x0]
	add	x0, x19, x0, lsl 6
	strb	wzr, [x0,61]
	b	.L733
.L753:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	adrp	x3, .LANCHOR0
	adrp	x4, .LANCHOR3
	add	x0, x3, :lo12:.LANCHOR0
	ldr	x1, [x0,1120]
	add	x0, x4, :lo12:.LANCHOR3
	ldrh	w5, [x1,124]
	ldrb	w0, [x0,1265]
	cbz	w5, .L755
	add	x1, x1, 392
	mov	w2, 1
	b	.L756
.L755:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w6, [x0,120]
	mov	w0, 65535
	cbz	w6, .L757
	add	x1, x1, x2, lsl 7
	mov	w2, w5
	add	x1, x1, 136
.L756:
	add	x5, x1, 128
	mov	w6, 65535
.L760:
	ldrh	w0, [x1]
	cmp	w0, w6
	beq	.L758
	mov	w5, -1
	strh	w5, [x1]
	cbz	w2, .L759
	add	x3, x3, :lo12:.LANCHOR0
	ldr	x2, [x3,1120]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L757
.L759:
	add	x4, x4, :lo12:.LANCHOR3
	add	x3, x3, :lo12:.LANCHOR0
	ldrb	w1, [x4,1265]
	ldr	x2, [x3,1120]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L757
.L758:
	add	x1, x1, 2
	cmp	x1, x5
	bne	.L760
.L757:
	ret
	.size	gc_get_src_blk, .-gc_get_src_blk
	.align	2
	.global	gc_free_temp_buf
	.type	gc_free_temp_buf, %function
gc_free_temp_buf:
	stp	x29, x30, [sp, -64]!
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x21, [sp,32]
	add	x4, x19, :lo12:.LANCHOR0
	ldrb	w1, [x4,3431]
	cbz	w1, .L765
	ldrb	w1, [x4,3362]
	cmp	w1, 1
	bhi	.L765
	adrp	x20, .LANCHOR3
	ldrh	w1, [x4,5532]
	add	x5, x20, :lo12:.LANCHOR3
	add	x4, x4, 1312
	add	w2, w1, 24
	ldrh	w3, [x5,1266]
	ldrb	w0, [x5,1249]
	mul	w0, w3, w0
	ldr	x3, [x5,1256]
	cmp	w0, w2
	csel	w2, w0, w2, ls
.L766:
	cmp	w1, w2
	bcs	.L779
	uxtw	x21, w1
	ldrb	w0, [x3,x21]
	cmp	w0, 255
	beq	.L767
	sbfiz	x0, x0, 6, 32
	add	x0, x4, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L767
	str	x1, [x29,56]
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L768
	add	x0, x20, :lo12:.LANCHOR3
	ldr	x2, [x0,1256]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	ldrb	w2, [x2,x21]
	bl	printk
.L768:
	add	x20, x20, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x20,1256]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,3431]
	sub	w0, w0, #1
	strb	w0, [x19,3431]
	mov	w0, 1
	b	.L765
.L767:
	add	w1, w1, 1
	b	.L766
.L779:
	mov	w0, 0
.L765:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	print_gc_debug_info
	.type	print_gc_debug_info, %function
print_gc_debug_info:
	adrp	x6, .LANCHOR0
	adrp	x0, .LC100
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC100
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,3424]
	ldrh	w2, [x6,3426]
	ldrh	w3, [x6,5530]
	ldrb	w4, [x6,3362]
	ldrb	w5, [x6,3431]
	ldrh	w6, [x6,3400]
	bl	printk
	ldp	x29, x30, [sp], 16
	ret
	.size	print_gc_debug_info, .-print_gc_debug_info
	.align	2
	.global	zftl_get_gc_node
	.type	zftl_get_gc_node, %function
zftl_get_gc_node:
	stp	x29, x30, [sp, -16]!
	uxth	w1, w1
	uxth	w2, w0
	cmp	w1, 5
	add	x29, sp, 0
	bne	.L782
	adrp	x0, .LANCHOR3+1272
	ldr	x0, [x0,#:lo12:.LANCHOR3+1272]
	b	.L785
.L782:
	cmp	w1, 2
	bne	.L784
	mov	w0, w2
	bl	zftl_get_gc_node.part.11
	b	.L786
.L784:
	adrp	x0, .LANCHOR3+1280
	ldr	x0, [x0,#:lo12:.LANCHOR3+1280]
.L785:
	mov	w1, w2
	bl	_list_get_gc_head_node.isra.4
.L786:
	uxth	w0, w0
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node, .-zftl_get_gc_node
	.align	2
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	uxtb	w20, w2
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	uxtb	w0, w1
	add	x1, x19, :lo12:.LANCHOR0
	str	w0, [x29,128]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x3, [x1,1120]
	add	x0, x3, x21, sxtw 1
	ldrh	w0, [x0,120]
	cbnz	w0, .L908
	ldrh	w2, [x1,3480]
	cmp	w2, 1
	bhi	.L790
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	strh	w0, [x1,1288]
	strh	w0, [x1,1290]
	strh	w0, [x1,1292]
.L790:
	cbnz	w21, .L912
	adrp	x24, .LC101
	mov	w22, w21
	mov	w23, w21
	add	w25, w20, 1
	adrp	x26, .LANCHOR3
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x24, x24, :lo12:.LC101
.L791:
	cmp	w23, w25
	bge	.L797
	add	x3, x26, :lo12:.LANCHOR3
	mov	w1, 3
	str	x3, [x29,112]
	ldrh	w2, [x3,1290]
	str	x2, [x29,120]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,120]
	cmp	w1, w27
	ldr	x3, [x29,112]
	mov	w4, w1
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x3,1290]
	beq	.L793
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	uxtw	x6, w1
	tbz	x0, 8, .L794
	add	x0, x19, :lo12:.LANCHOR0
	str	x4, [x29,112]
	str	x6, [x29,120]
	ldr	x3, [x0,1112]
	mov	x0, x24
	ldrh	w3, [x3,x6,lsl 1]
	bl	printk
	ldr	x4, [x29,112]
	ldr	x6, [x29,120]
.L794:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1112]
	ldrh	w0, [x0,3406]
	ldrh	w1, [x1,x6,lsl 1]
	cmp	w1, w0
	bcs	.L795
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L796
	add	w5, w22, 1
	uxth	w22, w5
	cmp	w22, w20
	bcc	.L796
	b	.L797
.L795:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	strh	wzr, [x0,1290]
	b	.L797
.L793:
	strh	wzr, [x3,1290]
	b	.L797
.L796:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L791
.L797:
	ldr	x0, [x29,128]
	tbz	x0, 1, .L799
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3386]
	cmp	w0, 32
	bls	.L799
	mov	w28, 0
	adrp	x24, .LANCHOR3
	mov	w26, 65535
	uxth	w27, w20
.L800:
	cmp	w28, w25
	adrp	x23, .LANCHOR3
	bge	.L804
	add	x2, x24, :lo12:.LANCHOR3
	mov	w1, 5
	str	x2, [x29,112]
	ldrh	w3, [x2,1292]
	str	x3, [x29,120]
	mov	w0, w3
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,112]
	cmp	w1, w26
	ldr	x3, [x29,120]
	add	w3, w3, 1
	strh	w3, [x2,1292]
	beq	.L801
	add	x3, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x4, [x3,1112]
	ldrh	w4, [x4,x1]
	ldrh	w1, [x3,3408]
	cmp	w4, w1
	bcs	.L801
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L803
	add	w5, w22, 1
	uxth	w22, w5
	cmp	w22, w27
	bcc	.L803
	b	.L804
.L801:
	strh	wzr, [x2,1292]
	b	.L804
.L803:
	add	w28, w28, 1
	uxth	w28, w28
	b	.L800
.L804:
	cmp	w22, w20
	bcs	.L806
	add	x23, x23, :lo12:.LANCHOR3
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x23,1266]
	ldrb	w4, [x23,1249]
	ldrh	w1, [x23,1294]
	ldrh	w3, [x2,3408]
	mul	w0, w0, w4
	sub	w0, w0, w1, lsr 2
	cmp	w3, w0
	bge	.L799
	add	w1, w3, w1, lsr 3
	strh	w1, [x2,3408]
	b	.L799
.L806:
	add	x1, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	ldrh	w2, [x1,3408]
	ldrh	w0, [x23,1294]
	cmp	w2, w0
	bls	.L799
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,3408]
.L799:
	ldr	x0, [x29,128]
	tbz	x0, 0, .L807
	uxth	w27, w20
	cmp	w22, w27
	bcs	.L807
	mov	w24, 0
	adrp	x25, .LANCHOR3
	mov	w26, 65535
.L812:
	add	x28, x25, :lo12:.LANCHOR3
	ldrh	w23, [x28,1288]
	mov	w0, w23
	add	w23, w23, 1
	bl	zftl_get_gc_node.part.11
	strh	w23, [x28,1288]
	cmp	w26, w0, uxth
	adrp	x23, .LANCHOR3
	beq	.L808
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L809
	add	w5, w22, 1
	uxth	w22, w5
	cmp	w22, w27
	bcc	.L809
	b	.L810
.L808:
	strh	wzr, [x28,1288]
	b	.L811
.L809:
	add	w24, w24, 1
	uxth	w24, w24
	cmp	w24, w27
	bcc	.L812
.L811:
	cmp	w22, w27
	bcs	.L810
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	ldrh	w1, [x19,3406]
	ldrh	w0, [x23,1294]
	cmp	w1, w0
	bcs	.L807
	add	w0, w1, w0, lsr 3
	b	.L911
.L810:
	add	x23, x23, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x23,1294]
	ldrh	w1, [x19,3406]
	cmp	w1, w0, lsr 1
	bls	.L807
	sub	w0, w1, w0, lsr 3
.L911:
	strh	w0, [x19,3406]
	b	.L807
.L912:
	adrp	x23, .LANCHOR3
	cmp	w20, 1
	add	x1, x23, :lo12:.LANCHOR3
	ldrb	w0, [x1,1249]
	ldrh	w24, [x1,1266]
	mul	w24, w0, w24
	uxth	w24, w24
	bne	.L813
	cbz	w2, .L813
	ldrh	w1, [x3,80]
	mov	w0, 65535
	mov	w20, 8
	cmp	w1, w0
	beq	.L813
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,5530]
	sub	w24, w24, w0
	uxth	w24, w24
.L813:
	ldr	w0, [x29,128]
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR3
	and	w25, w0, 1
	ldr	x0, [x29,128]
	strh	wzr, [x1,5534]
	strh	wzr, [x3,1296]
	tbz	x0, 0, .L847
	ldrh	w3, [x3,1298]
	ldrh	w0, [x1,3382]
	cmp	w0, w3, lsr 2
	bhi	.L815
	ldrh	w1, [x1,3384]
	cmp	w1, w0
	bcs	.L848
.L815:
	cmp	w2, 1
	bls	.L817
.L819:
	mov	w22, 0
	b	.L818
.L817:
	add	x26, x23, :lo12:.LANCHOR3
	lsr	w0, w0, 2
	strh	w0, [x26,1288]
	mov	w0, 0
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L819
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w2, [x26,1294]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 2
	bcs	.L819
	mov	w1, 1
	mov	w2, w21
	strh	w1, [x26,1288]
	mov	w1, 0
	bl	gc_add_sblk
	cmp	w0, wzr
	cset	w22, ne
.L818:
	mov	w26, 64
	mov	w28, 0
	uxth	w27, w20
.L822:
	add	x2, x23, :lo12:.LANCHOR3
	str	x2, [x29,104]
	ldrh	w1, [x2,1288]
	str	x1, [x29,112]
	mov	w0, w1
	bl	zftl_get_gc_node.part.11
	uxth	w4, w0
	mov	w1, 65535
	str	x4, [x29,120]
	cmp	w4, w1
	ldr	x2, [x29,104]
	beq	.L820
	ldr	x1, [x29,112]
	add	w1, w1, 1
	strh	w1, [x2,1288]
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	ldr	x4, [x29,120]
	cbz	w0, .L821
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x4, x4, 1, 16
	add	w5, w22, 1
	ldr	x1, [x0,1112]
	uxth	w22, w5
	ldrh	w1, [x1,x4]
	add	w28, w28, w1
	uxth	w28, w28
	cmp	w28, w24
	bhi	.L816
	cmp	w22, w27
	bcs	.L816
	ldrh	w1, [x0,3384]
	ldrh	w0, [x0,3382]
	cmp	w1, w0, lsl 1
	ble	.L821
	b	.L816
.L820:
	strh	wzr, [x2,1288]
	b	.L816
.L821:
	sub	w3, w26, #1
	uxth	w26, w3
	cbnz	w26, .L822
	b	.L816
.L847:
	mov	w28, w25
	mov	w22, w25
.L814:
	ldr	x0, [x29,128]
	tbz	x0, 1, .L826
	cmp	w28, w24
	bcs	.L826
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3386]
	cmp	w0, 32
	bls	.L826
	uxth	w0, w20
	mov	w27, 64
	mov	w26, 0
	str	w0, [x29,120]
.L832:
	add	x2, x23, :lo12:.LANCHOR3
	mov	w1, 5
	str	x2, [x29,104]
	ldrh	w7, [x2,1292]
	str	x7, [x29,112]
	mov	w0, w7
	bl	zftl_get_gc_node
	uxth	w6, w0
	mov	w1, 65535
	ldr	x2, [x29,104]
	cmp	w6, w1
	beq	.L827
	ldr	x7, [x29,112]
	cmp	w20, 1
	add	w7, w7, 1
	strh	w7, [x2,1292]
	bne	.L828
	ldrb	w7, [x2,1249]
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w8, [x2,1266]
	mul	w8, w7, w8
	ldrh	w7, [x2,1294]
	sub	w2, w8, w7, lsr 3
	strh	w2, [x1,3402]
.L828:
	add	x7, x19, :lo12:.LANCHOR0
	ubfiz	x6, x6, 1, 16
	str	x7, [x29,112]
	str	x6, [x29,104]
	ldr	x1, [x7,1112]
	ldrh	w2, [x1,x6]
	ldrh	w1, [x7,3402]
	cmp	w2, w1
	bcs	.L829
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	ldr	x7, [x29,112]
	ldr	x6, [x29,104]
	cbz	w0, .L830
	ldr	x0, [x7,1112]
	add	w3, w26, 1
	add	w5, w22, 1
	uxth	w26, w3
	ldrh	w0, [x0,x6]
	uxth	w22, w5
	add	w28, w28, w0
	uxth	w28, w28
	cmp	w28, w24
	bhi	.L831
	ldr	w0, [x29,120]
	cmp	w22, w0
	bcc	.L830
	b	.L831
.L829:
	add	x0, x23, :lo12:.LANCHOR3
	strh	wzr, [x0,1292]
	b	.L831
.L827:
	strh	wzr, [x2,1292]
	b	.L831
.L830:
	sub	w4, w27, #1
	uxth	w27, w4
	cbnz	w27, .L832
.L831:
	cmp	w22, w20
	bcc	.L833
	cbnz	w26, .L834
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w1, [x1,3386]
	ldrh	w0, [x0,1300]
	cmp	w1, w0
	bls	.L834
.L833:
	add	x3, x23, :lo12:.LANCHOR3
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w2, [x3,1294]
	ldrh	w0, [x3,1266]
	ldrb	w3, [x3,1249]
	lsr	w2, w2, 3
	ldrh	w1, [x4,3402]
	mul	w0, w0, w3
	sub	w0, w0, w2
	cmp	w1, w0
	bge	.L826
	add	w1, w2, w1
	strh	w1, [x4,3402]
	b	.L826
.L834:
	add	x23, x23, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x23,1294]
	ldrb	w1, [x23,1249]
	ldrh	w2, [x19,3402]
	mul	w1, w0, w1
	cmp	w2, w1
	ble	.L807
	sub	w0, w2, w0, lsr 3
	strh	w0, [x19,3402]
	b	.L807
.L826:
	cbz	w25, .L807
	uxth	w26, w20
	cmp	w22, w26
	bcs	.L807
	cmp	w28, w24
	bcs	.L807
	mov	w25, 64
.L840:
	add	x2, x23, :lo12:.LANCHOR3
	mov	w1, 3
	str	x2, [x29,120]
	ldrh	w27, [x2,1290]
	mov	w0, w27
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	ldr	x2, [x29,120]
	beq	.L836
	add	w27, w27, 1
	strh	w27, [x2,1290]
	ubfiz	x27, x1, 1, 16
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x4, [x1,1112]
	ldrh	w6, [x4,x27]
	ldrh	w4, [x1,3404]
	cmp	w6, w4
	bcs	.L837
	ldrh	w2, [x2,1298]
	ldrh	w1, [x1,3384]
	cmp	w1, w2, lsr 1
	bls	.L838
.L837:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L839
	add	x0, x19, :lo12:.LANCHOR0
	add	w5, w22, 1
	uxth	w22, w5
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x27]
	add	w28, w28, w0
	uxth	w28, w28
	cmp	w28, w24
	bhi	.L838
	cmp	w22, w26
	bcc	.L839
	b	.L838
.L836:
	cmp	w27, 64
	bls	.L838
	strh	wzr, [x2,1290]
	b	.L838
.L839:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L840
.L838:
	cmp	w22, w26
	add	x19, x19, :lo12:.LANCHOR0
	bcs	.L841
	add	x23, x23, :lo12:.LANCHOR3
	ldrh	w1, [x19,3404]
	ldrh	w0, [x23,1294]
	cmp	w1, w0, lsr 1
	bls	.L807
	sub	w0, w1, w0, lsr 3
	strh	w0, [x19,3404]
	b	.L807
.L841:
	add	x23, x23, :lo12:.LANCHOR3
	ldrh	w2, [x19,3404]
	ldrh	w1, [x23,1294]
	ldrb	w0, [x23,1249]
	mul	w0, w1, w0
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L807
	add	w1, w2, w1, lsr 3
	strh	w1, [x19,3404]
.L807:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L842
	adrp	x0, .LC102
	ldr	w2, [x29,128]
	add	x0, x0, :lo12:.LC102
	mov	w1, w21
	mov	w3, w22
	mov	w4, w20
	bl	printk
.L842:
	mov	w0, w22
	b	.L908
.L848:
	mov	w28, 0
	mov	w22, w28
.L816:
	add	x2, x23, :lo12:.LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x2,1298]
	ldrh	w1, [x0,3384]
	cmp	w1, w2, lsr 2
	bhi	.L843
	ldrh	w0, [x0,3382]
	add	w0, w0, 8
	cmp	w1, w0
	ble	.L814
.L843:
	cmp	w28, w24
	bcs	.L814
	mov	w26, 64
	uxth	w27, w20
.L825:
	add	x6, x23, :lo12:.LANCHOR3
	mov	w1, 3
	str	x6, [x29,104]
	ldrh	w2, [x6,1290]
	str	x2, [x29,112]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w4, w0
	mov	w1, 65535
	str	x4, [x29,120]
	cmp	w4, w1
	ldr	x2, [x29,112]
	ldr	x6, [x29,104]
	beq	.L823
	add	w2, w2, 1
	mov	w1, 0
	strh	w2, [x6,1290]
	mov	w2, w21
	bl	gc_add_sblk
	ldr	x4, [x29,120]
	cbz	w0, .L824
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x4, x4, 1, 16
	add	w5, w22, 1
	ldr	x0, [x0,1112]
	uxth	w22, w5
	ldrh	w0, [x0,x4]
	add	w28, w28, w0
	uxth	w28, w28
	cmp	w28, w24
	bhi	.L814
	cmp	w22, w27
	bcc	.L824
	b	.L814
.L823:
	cmp	w2, 64
	bls	.L814
	strh	wzr, [x6,1290]
	b	.L814
.L824:
	sub	w3, w26, #1
	uxth	w26, w3
	cbnz	w26, .L825
	b	.L814
.L908:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_insert_free_list
	.type	zftl_insert_free_list, %function
zftl_insert_free_list:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,1096]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L914
	add	x0, x3, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1304
	b	.L917
.L914:
	cmp	w0, 16
	add	x0, x3, :lo12:.LANCHOR3
	bne	.L916
	add	x0, x0, 1312
	add	x2, x2, 3378
	b	.L917
.L916:
	add	x0, x0, 1320
	add	x2, x2, 3380
.L917:
	bl	_insert_free_list
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_insert_free_list, .-zftl_insert_free_list
	.align	2
	.global	zftl_insert_data_list
	.type	zftl_insert_data_list, %function
zftl_insert_data_list:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,1096]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L919
	add	x0, x2, 1128
	add	x2, x2, 3382
	b	.L922
.L919:
	cmp	w3, 96
	bne	.L921
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1280
	b	.L922
.L921:
	cmp	w3, 160
	bne	.L918
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1272
.L922:
	bl	_insert_data_list
.L918:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_insert_data_list, .-zftl_insert_data_list
	.align	2
	.global	zftl_gc_get_free_sblk
	.type	zftl_gc_get_free_sblk, %function
zftl_gc_get_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	add	x21, x20, :lo12:.LANCHOR0
	uxth	w22, w0
	uxth	w23, w1
	ldr	x0, [x21,1120]
	ldrh	w19, [x0,588]
	cbnz	w22, .L924
	mov	w0, 65535
	cmp	w19, w0
	beq	.L924
	adrp	x0, .LC103
	mov	w1, w19
	add	x0, x0, :lo12:.LC103
	bl	printk
	ldr	x0, [x21,1120]
	mov	w1, -1
	strh	w1, [x0,588]
	b	.L925
.L924:
	add	x0, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldrh	w3, [x0,3378]
	ldrh	w1, [x0,3380]
	cmp	w3, w1
	bcc	.L926
	ldrh	w0, [x0,3376]
	cmp	w0, w3
	bls	.L927
	cbz	w1, .L927
.L926:
	lsr	w1, w1, 2
	cbz	w22, .L929
	mov	w1, 7
	mul	w3, w3, w1
	lsr	w1, w3, 3
.L929:
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1320
	add	x2, x2, 3380
	b	.L951
.L927:
	mov	w1, 0
	lsr	w3, w3, 3
	cmp	w22, w1
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	csel	w1, w3, w1, ne
	add	x0, x0, 1312
	add	x2, x2, 3378
.L951:
	bl	_list_pop_index_node
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L932
	add	x5, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	adrp	x0, .LC104
	mov	w1, w19
	add	x0, x0, :lo12:.LC104
	mov	w2, w23
	ldrh	w4, [x5,3376]
	ldr	x3, [x21,1304]
	ldrh	w5, [x5,3380]
	bl	printk
.L932:
	cbz	w22, .L925
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L925
	add	x20, x20, :lo12:.LANCHOR0
	uxtw	x6, w19
	lsl	x0, x6, 2
	ldr	x1, [x20,1096]
	ldr	x7, [x20,1112]
	add	x2, x1, x0
	ldr	w4, [x1,x0]
	ldrh	w5, [x1,x0]
	adrp	x0, .LC105
	ldrb	w3, [x2,2]
	add	x0, x0, :lo12:.LC105
	ldrh	w6, [x7,x6,lsl 1]
	mov	w1, w19
	ubfx	x2, x3, 5, 3
	ubfx	x4, x4, 11, 8
	ubfx	x3, x3, 3, 2
	and	w5, w5, 2047
	bl	printk
.L925:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	zftl_gc_get_free_sblk, .-zftl_gc_get_free_sblk
	.align	2
	.global	zftl_get_free_sblk
	.type	zftl_get_free_sblk, %function
zftl_get_free_sblk:
	stp	x29, x30, [sp, -48]!
	uxth	w3, w0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w21, w1
	stp	x19, x20, [sp,16]
	cmp	w21, 5
	adrp	x20, .LANCHOR0
	bne	.L953
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	ldrh	w3, [x2,3378]
	ldrh	w1, [x2,3380]
	cmp	w3, w1
	bcc	.L954
	ldrh	w2, [x2,3376]
	cmp	w2, w3
	bls	.L955
	cbz	w1, .L955
.L954:
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1320
	lsr	w1, w1, 1
	b	.L983
.L955:
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1312
	mov	w1, 0
	add	x2, x2, 3378
	b	.L982
.L953:
	add	x22, x20, :lo12:.LANCHOR0
	cmp	w21, 1
	ldr	x0, [x22,1120]
	ldrh	w19, [x0,590]
	beq	.L957
	mov	w0, 65535
	cmp	w19, w0
	beq	.L957
	adrp	x0, .LC106
	mov	w1, w19
	add	x0, x0, :lo12:.LC106
	bl	printk
	ldr	x0, [x22,1120]
	mov	w1, -1
	strh	w1, [x0,590]
	b	.L958
.L957:
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	ldrh	w1, [x4,3376]
	ldrh	w2, [x4,3380]
	cmp	w1, w2
	bcc	.L959
	ldrh	w4, [x4,3378]
	cmp	w4, w1
	bls	.L960
	cbz	w2, .L960
.L959:
	lsr	w2, w2, 1
	cmp	w21, 1
	csel	w3, w2, w3, eq
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1320
	mov	w1, w3
.L983:
	add	x2, x2, 3380
	b	.L982
.L960:
	lsr	w1, w1, 1
	cmp	w21, 1
	csel	w3, w1, w3, eq
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1304
	mov	w1, w3
	add	x2, x2, 3376
.L982:
	bl	_list_pop_index_node
	uxth	w0, w0
	mov	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L958
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3+1304
	adrp	x0, .LC104
	mov	w1, w19
	add	x0, x0, :lo12:.LC104
	mov	w2, w21
	ldr	x3, [x3,#:lo12:.LANCHOR3+1304]
	ldrh	w4, [x20,3376]
	ldrh	w5, [x20,3380]
	bl	printk
.L958:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zftl_get_free_sblk, .-zftl_get_free_sblk
	.align	2
	.global	zftl_remove_data_node
	.type	zftl_remove_data_node, %function
zftl_remove_data_node:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,1096]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L985
	add	x0, x2, 1128
	add	x2, x2, 3382
	b	.L988
.L985:
	cmp	w3, 96
	bne	.L987
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1280
	b	.L988
.L987:
	cmp	w3, 160
	bne	.L984
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1272
.L988:
	bl	_list_remove_node
.L984:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_remove_data_node, .-zftl_remove_data_node
	.align	2
	.global	zftl_remove_free_node
	.type	zftl_remove_free_node, %function
zftl_remove_free_node:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,1096]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L990
	add	x0, x3, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1304
	b	.L993
.L990:
	cmp	w0, 16
	add	x0, x3, :lo12:.LANCHOR3
	bne	.L992
	add	x0, x0, 1312
	add	x2, x2, 3378
	b	.L993
.L992:
	add	x0, x0, 1320
	add	x2, x2, 3380
.L993:
	bl	_list_remove_node
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_remove_free_node, .-zftl_remove_free_node
	.align	2
	.global	zftl_list_update_data_list
	.type	zftl_list_update_data_list, %function
zftl_list_update_data_list:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,1096]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L995
	add	x0, x2, 1128
	add	x2, x2, 3382
	b	.L998
.L995:
	cmp	w3, 96
	bne	.L997
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1280
	b	.L998
.L997:
	cmp	w3, 160
	bne	.L994
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1272
.L998:
	bl	_list_update_data_list
.L994:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_list_update_data_list, .-zftl_list_update_data_list
	.align	2
	.global	print_list_info
	.type	print_list_info, %function
print_list_info:
	sub	sp, sp, #128
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	mov	x19, x0
	str	x27, [sp,112]
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	adrp	x0, .LC107
	ldrh	w2, [x1]
	add	x0, x0, :lo12:.LC107
	ldr	x1, [x19]
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L999
	mov	x22, -6148914691236517206
	adrp	x23, .LC108
	mov	w20, 0
	adrp	x26, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC108
	mov	w27, 65535
	mov	w25, 6
	adrp	x24, .LANCHOR3
.L1002:
	add	x21, x26, :lo12:.LANCHOR0
	ldrh	w3, [x19]
	ldrh	w4, [x19,2]
	ldrh	w5, [x19,4]
	ldr	x2, [x21,1040]
	ldr	x1, [x21,1096]
	sub	x2, x19, x2
	asr	x2, x2, 1
	mul	x2, x2, x22
	and	x8, x2, 65535
	lsl	x0, x8, 2
	uxth	w2, w2
	add	x6, x1, x0
	ldrb	w7, [x6,2]
	ldr	w6, [x1,x0]
	ubfx	x6, x6, 11, 8
	str	w6, [sp]
	ubfx	x6, x7, 5, 3
	ubfx	x7, x7, 3, 2
	ldrh	w0, [x1,x0]
	mov	w1, w20
	and	w0, w0, 2047
	str	w0, [sp,8]
	ldr	x0, [x21,1112]
	ldrh	w0, [x0,x8,lsl 1]
	str	w0, [sp,16]
	mov	x0, x23
	bl	printk
	ldrh	w19, [x19]
	cmp	w19, w27
	beq	.L999
	ldr	x0, [x21,1040]
	add	w20, w20, 1
	umull	x19, w19, w25
	uxth	w20, w20
	add	x19, x0, x19
	add	x0, x24, :lo12:.LANCHOR3
	ldrh	w0, [x0,1328]
	cmp	w0, w20
	bcs	.L1002
.L999:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldr	x27, [sp,112]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 128
	ret
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.type	dump_all_list_info, %function
dump_all_list_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR3
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	add	x0, x20, 1304
	add	x1, x19, 3376
	bl	print_list_info
	add	x0, x20, 1312
	add	x1, x19, 3378
	bl	print_list_info
	add	x0, x20, 1320
	add	x1, x19, 3380
	bl	print_list_info
	add	x0, x19, 1128
	add	x1, x19, 3382
	bl	print_list_info
	add	x0, x20, 1280
	add	x1, x19, 3384
	bl	print_list_info
	add	x0, x20, 1272
	add	x1, x19, 3386
	bl	print_list_info
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	adrp	x0, .LANCHOR0+3392
	ldr	x0, [x0,#:lo12:.LANCHOR0+3392]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L1005
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
.L1005:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L1006
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L1006:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L1007
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L1007:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L1004
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L1004:
	ret
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x3, .LANCHOR0
	uxth	w0, w0
	add	x2, x3, :lo12:.LANCHOR0
	mov	x9, x3
	adrp	x11, .LANCHOR3
	mov	w13, 24
	ldr	x2, [x2,1096]
	add	x2, x2, x0, uxth 2
	ldrb	w12, [x2,3]
	mov	w2, 0
	mov	w6, w2
.L1010:
	add	x8, x11, :lo12:.LANCHOR3
	adrp	x4, .LANCHOR3
	ldrb	w3, [x8,1249]
	cmp	w6, w3
	bge	.L1016
	asr	w3, w12, w6
	tbnz	x3, 0, .L1011
	add	x3, x9, :lo12:.LANCHOR0
	ldrb	w5, [x8,1234]
	sbfiz	x10, x2, 1, 32
	ldrb	w7, [x3,1257]
	ldrh	w3, [x8,1232]
	sdiv	w4, w6, w5
	sub	w7, w13, w7
	sub	w3, w7, w3
	lsl	w3, w4, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,1234]
	cmp	w3, 1
	bls	.L1012
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L1012:
	add	w2, w2, 1
.L1011:
	add	w6, w6, 1
	b	.L1010
.L1016:
	mov	w0, w2
	mov	w5, -1
.L1014:
	add	x3, x4, :lo12:.LANCHOR3
	ldrb	w3, [x3,1249]
	cmp	w2, w3
	bge	.L1017
	strh	w5, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L1014
.L1017:
	ret
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_erase_phy_blk
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	stp	x29, x30, [sp, -48]!
	uxth	w2, w0
	adrp	x3, .LANCHOR3
	adrp	x0, .LANCHOR0+1257
	add	x5, x3, :lo12:.LANCHOR3
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	ldrb	w4, [x0,#:lo12:.LANCHOR0+1257]
	mov	w0, 24
	ldrh	w19, [x5,1232]
	mov	w21, w1
	sub	w4, w0, w4
	ldrb	w0, [x5,1250]
	sub	w4, w4, w19
	mov	w19, 1
	lsl	w19, w19, w4
	asr	w22, w2, w4
	sub	w19, w19, #1
	mov	x20, x3
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L1019
	ldrb	w0, [x5,1251]
	cbnz	w0, .L1019
	ldrh	w2, [x5,1330]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L1019:
	add	x3, x20, :lo12:.LANCHOR3
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,1330]
	mul	w2, w19, w2
	bl	flash_erase_block_en
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_erase_phy_blk, .-ftl_erase_phy_blk
	.align	2
	.global	ftl_erase_sblk
	.type	ftl_erase_sblk, %function
ftl_erase_sblk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxth	w24, w0
	adrp	x0, .LANCHOR0
	mov	w23, w1
	add	x1, x0, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	ubfiz	x22, x24, 2, 16
	adrp	x25, .LANCHOR3
	ldr	x1, [x1,1096]
	mov	w19, 0
	mov	x21, x0
	mov	x26, x25
	add	x1, x1, x22
	ldrb	w27, [x1,3]
.L1024:
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w0, [x1,1332]
	cmp	w19, w0
	bge	.L1035
	ldrb	w0, [x1,1234]
	mov	w20, 0
	ldrh	w3, [x1,1330]
	mov	w1, w20
	sub	w4, w0, #1
	mul	w6, w19, w0
	mul	w5, w24, w0
.L1036:
	cmp	w1, w0
	bge	.L1049
	add	w2, w1, w6
	asr	w2, w27, w2
	tbnz	x2, 0, .L1025
	and	w2, w1, w4
	add	x7, x29, 96
	add	w2, w5, w2
	mul	w2, w2, w3
	str	w2, [x7,w20,sxtw 2]
	add	w20, w20, 1
.L1025:
	add	w1, w1, 1
	b	.L1036
.L1049:
	cmp	w0, 4
	bne	.L1048
	mov	x28, 0
.L1027:
	cmp	w20, w28
	ble	.L1030
	add	x2, x29, 96
	mov	w0, w19
	mov	w1, w23
	ldr	w2, [x2,x28,lsl 2]
	add	x28, x28, 1
	bl	flash_erase_block_en
	b	.L1027
.L1048:
	cmp	w20, 2
	bne	.L1031
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w1, [x0,1250]
	cbz	w1, .L1032
	ldrb	w0, [x0,1251]
	cbnz	w0, .L1032
	cmp	w23, wzr
	ldr	w2, [x29,96]
	ldr	w3, [x29,100]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L1032:
	ldr	w2, [x29,96]
	mov	w0, w19
	ldr	w3, [x29,100]
	mov	w1, w23
	bl	flash_erase_duplane_block
	b	.L1030
.L1031:
	cmp	w20, 1
	bne	.L1030
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w1, [x0,1250]
	cbz	w1, .L1034
	ldrb	w0, [x0,1251]
	cbnz	w0, .L1034
	cmp	w23, wzr
	ldr	w2, [x29,96]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L1034:
	ldr	w2, [x29,96]
	mov	w0, w19
	mov	w1, w23
	bl	flash_erase_block_en
.L1030:
	add	w19, w19, 1
	b	.L1024
.L1035:
	add	x0, x21, :lo12:.LANCHOR0
	cbnz	w23, .L1037
	ldr	x2, [x0,1096]
	ldrh	w1, [x2,x22]
	add	w3, w1, 1
	bfi	w1, w3, 0, 11
	strh	w1, [x2,x22]
	ldr	x1, [x0,3392]
	ldr	x0, [x0,1096]
	ldr	w2, [x1,84]
	ldrh	w3, [x1,96]
	add	w2, w2, 1
	str	w2, [x1,84]
	ldrh	w0, [x0,x22]
	ubfx	x0, x0, 0, 11
	uxth	w2, w0
	cmp	w3, w2
	bge	.L1039
	strh	w0, [x1,96]
	b	.L1039
.L1037:
	ldr	x3, [x0,1096]
	ldr	w1, [x3,x22]
	ubfx	x2, x1, 11, 8
	add	w2, w2, 1
	bfi	w1, w2, 11, 8
	str	w1, [x3,x22]
	ldr	x1, [x0,3392]
	ldr	x0, [x0,1096]
	ldr	w2, [x1,80]
	add	w2, w2, 1
	str	w2, [x1,80]
	ldrh	w2, [x1,98]
	ldr	w0, [x0,x22]
	ubfx	x0, x0, 11, 8
	cmp	w2, w0, uxtb
	bcs	.L1039
	strh	w0, [x1,98]
.L1039:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	add	x1, x0, :lo12:.LANCHOR0
	str	x19, [sp,16]
	mov	x19, x0
	ldr	x1, [x1,3392]
	ldrh	w2, [x1,136]
	cmp	w2, 63
	bls	.L1051
	strh	wzr, [x1,136]
.L1051:
	ldrh	w0, [x1,112]
	cbnz	w0, .L1052
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1283
	add	x1, x1, 1800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1052:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,3392]
.L1056:
	ldrh	w2, [x1,136]
.L1053:
	cmp	w2, 63
	bgt	.L1059
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L1058
	add	w2, w2, 1
	b	.L1053
.L1059:
	strh	wzr, [x1,136]
	b	.L1056
.L1058:
	mov	w4, -1
	strh	w4, [x1,x3,lsl 1]
	strh	w2, [x1,136]
	ldrh	w2, [x1,112]
	sub	w2, w2, #1
	strh	w2, [x1,112]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_alloc_sys_blk, .-ftl_alloc_sys_blk
	.align	2
	.global	ftl_free_sys_blk
	.type	ftl_free_sys_blk, %function
ftl_free_sys_blk:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	add	x0, x1, :lo12:.LANCHOR0
	mov	x19, x1
	ldr	x2, [x0,3392]
	ldrh	w0, [x2,138]
	cmp	w0, 63
	bls	.L1061
	strh	wzr, [x2,138]
.L1061:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L1062
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1310
	add	x1, x1, 1824
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1062:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,3392]
.L1066:
	ldrh	w2, [x1,138]
.L1063:
	cmp	w2, 63
	bgt	.L1068
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L1064
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L1064:
	add	w2, w2, 1
	b	.L1063
.L1068:
	strh	wzr, [x1,138]
	b	.L1066
	.size	ftl_free_sys_blk, .-ftl_free_sys_blk
	.align	2
	.global	ftl_info_data_recovery
	.type	ftl_info_data_recovery, %function
ftl_info_data_recovery:
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L1069
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,1096]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L1069
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L1073
	ldr	x1, [x23,3392]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L1074
.L1073:
	tst	w0, 24
	ldr	x1, [x23,3392]
	bne	.L1075
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L1074
.L1075:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L1074:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1076
	ldr	w0, [x21,x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21,x20]
	ldr	x1, [x19,3392]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L1069
.L1076:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L1077
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L1069
.L1077:
	cmp	w0, 96
	bne	.L1069
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L1069:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_info_data_recovery, .-ftl_info_data_recovery
	.align	2
	.global	ftl_get_ppa_from_index
	.type	ftl_get_ppa_from_index, %function
ftl_get_ppa_from_index:
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LANCHOR3
	uxth	w1, w0
	add	x4, x2, :lo12:.LANCHOR3
	adrp	x0, .LANCHOR0+1120
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, x2
	ldr	x3, [x0,#:lo12:.LANCHOR0+1120]
	ldrh	w0, [x4,1294]
	ldrb	w4, [x4,1249]
	mul	w0, w0, w4
	cmp	w1, w0
	bge	.L1079
	add	x3, x3, 16
	b	.L1080
.L1079:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L1080:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, x1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L1081
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1937
	add	x1, x1, 1848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1081:
	add	x2, x19, :lo12:.LANCHOR3
	ldrh	w0, [x2,1330]
	madd	w0, w21, w0, w20
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_get_ppa_from_index, .-ftl_get_ppa_from_index
	.align	2
	.global	lpa_hash_get_ppa
	.type	lpa_hash_get_ppa, %function
lpa_hash_get_ppa:
	adrp	x1, .LANCHOR3
	mov	w3, 65535
	add	x1, x1, :lo12:.LANCHOR3
	add	x2, x1, x0, uxtb 1
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w2, [x2,1336]
	ldr	x4, [x1,1848]
	ldr	x5, [x1,1856]
.L1083:
	cmp	w2, w3
	beq	.L1087
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L1084
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L1085
.L1084:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L1083
.L1087:
	mov	w0, -1
.L1085:
	ldp	x29, x30, [sp], 16
	ret
	.size	lpa_hash_get_ppa, .-lpa_hash_get_ppa
	.align	2
	.global	ftl_get_new_free_page
	.type	ftl_get_new_free_page, %function
ftl_get_new_free_page:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1089
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2080
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1089:
	adrp	x20, .LANCHOR3
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	cmp	w1, w0
	bne	.L1090
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2081
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1090:
	ldrh	w0, [x19,6]
	cbnz	w0, .L1091
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2082
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1091:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR3
	add	x0, x0, 8
	ldrb	w2, [x1,1249]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L1092:
	cmp	w0, w1
	bne	.L1096
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L1093
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1093:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1092
.L1096:
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w2, [x19,2]
	ldrh	w1, [x20,1330]
	ldrb	w3, [x20,1249]
	mul	w0, w0, w1
	ldrh	w1, [x19,6]
	orr	w0, w0, w2
	sub	w1, w1, #1
	strh	w1, [x19,6]
	ldrh	w1, [x19,10]
	add	w1, w1, 1
	strh	w1, [x19,10]
	ldrb	w1, [x19,5]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x19,5]
	cmp	w3, w1
	bne	.L1095
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L1095:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_get_new_free_page, .-ftl_get_new_free_page
	.align	2
	.global	ftl_ext_alloc_new_blk
	.type	ftl_ext_alloc_new_blk, %function
ftl_ext_alloc_new_blk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	bl	ftl_alloc_sys_blk
	uxth	w0, w0
	mov	w20, w0
	mov	w1, 65533
	sub	w0, w0, #1
	cmp	w1, w0, uxth
	bcs	.L1098
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2117
	add	x1, x1, 1896
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1098:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w20
	bl	ftl_erase_phy_blk
	ldr	x0, [x19,3392]
	ldrh	w0, [x0,130]
	bl	ftl_free_sys_blk
	ldr	x0, [x19,3392]
	strh	w20, [x0,130]
	strh	wzr, [x0,140]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_ext_alloc_new_blk, .-ftl_ext_alloc_new_blk
	.align	2
	.global	ftl_total_vpn_update
	.type	ftl_total_vpn_update, %function
ftl_total_vpn_update:
	adrp	x1, .LANCHOR3
	add	x3, x1, :lo12:.LANCHOR3
	ldrh	w2, [x3,1864]
	cbnz	w0, .L1100
	cmp	w2, 4
	bhi	.L1100
	add	w2, w2, 1
	strh	w2, [x3,1864]
	b	.L1099
.L1100:
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LANCHOR0
	mov	w9, 65535
	strh	wzr, [x1,1864]
	add	x1, x0, :lo12:.LANCHOR0
	ldrh	w6, [x1,1088]
	ldr	x7, [x1,1112]
	ldr	x8, [x1,1096]
	mov	x1, 0
	mov	w2, w1
	mov	w3, w1
.L1102:
	cmp	w6, w1, uxth
	bls	.L1110
	ldrh	w4, [x7,x1,lsl 1]
	cmp	w4, w9
	beq	.L1103
	add	x5, x8, x1, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1104
	add	w2, w2, w4
	b	.L1103
.L1104:
	add	w3, w3, w4
.L1103:
	add	x1, x1, 1
	b	.L1102
.L1110:
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,1120]
	ldr	x0, [x0,3392]
	str	w3, [x1,524]
	str	w2, [x1,528]
	ldrh	w0, [x0,120]
	cbz	w0, .L1099
	udiv	w2, w2, w0
	str	w2, [x1,532]
.L1099:
	ret
	.size	ftl_total_vpn_update, .-ftl_total_vpn_update
	.align	2
	.global	ftl_debug_info_fill
	.type	ftl_debug_info_fill, %function
ftl_debug_info_fill:
	adrp	x3, .LANCHOR0+3360
	uxtb	w0, w0
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldrb	w3, [x3,#:lo12:.LANCHOR0+3360]
	str	x19, [sp,16]
	cmp	w3, 8
	bls	.L1112
	cmp	w0, 2
	bne	.L1113
	cbz	x2, .L1113
	mov	x19, x1
	mov	w1, 21320
	movk	w1, 0x4841, lsl 16
	mov	x0, x2
	str	w1, [x19]
	mov	w1, 1024
	bl	js_hash
	str	w0, [x19,4]
	b	.L1112
.L1113:
	str	wzr, [x1]
	str	wzr, [x1,4]
.L1112:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_debug_info_fill, .-ftl_debug_info_fill
	.align	2
	.global	ftl_vpn_update
	.type	ftl_vpn_update, %function
ftl_vpn_update:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	mov	w0, w19
	ubfiz	x19, x19, 1, 16
	bl	zftl_list_update_data_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,1112]
	ldrh	w2, [x0,x19]
	mov	w0, 0
	cbnz	w2, .L1121
	mov	w0, 1
	str	w0, [x1,3412]
.L1121:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_vpn_update, .-ftl_vpn_update
	.align	2
	.global	ftl_vpn_decrement
	.type	ftl_vpn_decrement, %function
ftl_vpn_decrement:
	stp	x29, x30, [sp, -48]!
	uxth	w1, w0
	mov	w0, 65535
	add	x29, sp, 0
	str	x19, [sp,16]
	cmp	w1, w0
	beq	.L1124
	adrp	x2, .LANCHOR0
	uxtw	x0, w1
	add	x2, x2, :lo12:.LANCHOR0
	lsl	x3, x0, 1
	ldr	x4, [x2,1112]
	ldrh	w19, [x4,x3]
	cbnz	w19, .L1125
	ldr	x2, [x2,1096]
	add	x0, x2, x0, lsl 2
	mov	w2, w19
	ldrb	w3, [x0,2]
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	ubfx	x3, x3, 5, 3
	bl	printk
	b	.L1131
.L1125:
	sub	w19, w19, #1
	strh	w19, [x4,x3]
.L1124:
	adrp	x19, .LANCHOR3
	mov	w0, 0
	add	x19, x19, :lo12:.LANCHOR3
	ldrh	w2, [x19,1208]
	cmp	w2, w1
	beq	.L1126
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1127
	strh	w1, [x19,1208]
.L1131:
	mov	w0, 0
	b	.L1126
.L1127:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldrh	w3, [x19,1866]
	cset	w0, ne
	ldrh	w2, [x19,1208]
	add	w3, w3, 1
	ldr	x1, [x29,40]
	strh	w1, [x19,1208]
	uxth	w3, w3
	cmp	w3, 7
	csel	w3, w3, wzr, ls
	strh	w3, [x19,1866]
	add	x3, x19, x3, uxth 1
	strh	w2, [x3,1216]
.L1126:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	adrp	x3, .LANCHOR3
	uxtb	w5, w0
	add	x6, x3, :lo12:.LANCHOR3
	uxth	w2, w2
	add	x4, x6, 1336
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,1848]
	ldr	x12, [x6,1856]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L1133:
	cmp	w4, w11
	beq	.L1137
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L1134
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR3
	bne	.L1135
	ldr	x6, [x4,1856]
	add	x4, x4, 1336
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L1136
.L1135:
	ldr	x4, [x4,1856]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L1136:
	add	x4, x3, :lo12:.LANCHOR3
	mov	w6, -1
	ldr	x4, [x4,1856]
	strh	w6, [x4,x7,lsl 1]
	b	.L1137
.L1134:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L1133
.L1137:
	add	x3, x3, :lo12:.LANCHOR3
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,1848]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 1336
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,1856]
	strh	w6, [x0,x4,lsl 1]
	beq	.L1139
	adrp	x2, .LANCHOR0+1257
	ldrh	w0, [x3,1232]
	lsr	w1, w1, w0
	ldrb	w4, [x2,#:lo12:.LANCHOR0+1257]
	mov	w2, 24
	sub	w2, w2, w4
	mov	w4, 1
	sub	w2, w2, w0
	ldrb	w0, [x3,1234]
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L1139:
	mov	w0, -1
	ldp	x29, x30, [sp], 16
	ret
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	stp	x29, x30, [sp, -48]!
	adrp	x6, .LANCHOR0
	mov	w4, 1
	add	x29, sp, 0
	add	x1, x6, :lo12:.LANCHOR0
	mov	w3, 24
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	adrp	x7, .LANCHOR3
	add	x7, x7, :lo12:.LANCHOR3
	mov	x19, x6
	ldrb	w1, [x1,1257]
	sub	w3, w3, w1
	lsl	w1, w4, w1
	lsr	w2, w0, w3
	sub	w1, w1, #1
	and	w1, w1, w2
	ldrb	w5, [x7,1234]
	uxtb	w1, w1
	cmp	w5, 1
	mul	w2, w1, w5
	uxtb	w20, w2
	ldrh	w2, [x7,1232]
	sub	w3, w3, w2
	lsr	w0, w0, w2
	lsl	w3, w4, w3
	sub	w3, w3, #1
	and	w3, w3, w0
	uxth	w4, w3
	udiv	w3, w3, w5
	uxth	w21, w3
	bls	.L1144
	sub	w5, w5, #1
	and	w5, w4, w5
	add	w2, w20, w5
	uxtb	w20, w2
.L1144:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L1145
	adrp	x0, .LC110
	mov	w2, w20
	add	x0, x0, :lo12:.LC110
	uxth	w3, w3
	bl	printk
.L1145:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,1088]
	cmp	w1, w21
	bls	.L1143
	ldr	x0, [x0,1096]
	mov	w2, 1
	add	x21, x0, x21, uxth 2
	ldrb	w0, [x21,3]
	lsl	w2, w2, w20
	orr	w2, w2, w0
	strb	w2, [x21,3]
.L1143:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
	.align	2
	.global	gc_free_bad_sblk
	.type	gc_free_bad_sblk, %function
gc_free_bad_sblk:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxth	w24, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w27, 0
	ldr	w0, [x0,5544]
	cbz	w0, .L1167
	adrp	x22, .LANCHOR3
	adrp	x21, .LC111
	add	x0, x21, :lo12:.LC111
	mov	x23, x22
	mov	w25, 24
	str	x0, [x29,104]
.L1168:
	add	x2, x22, :lo12:.LANCHOR3
	ldrb	w0, [x2,1249]
	cmp	w0, w27
	bls	.L1167
	add	x4, x20, :lo12:.LANCHOR0
	ldrb	w3, [x2,1234]
	ldrh	w2, [x2,1232]
	cmp	w3, 1
	ldrb	w19, [x4,1257]
	uxth	w1, w3
	sdiv	w0, w27, w3
	sub	w19, w25, w19
	sub	w19, w19, w2
	lsl	w19, w0, w19
	madd	w19, w24, w1, w19
	uxth	w19, w19
	bls	.L1152
	sub	w1, w1, #1
	and	w1, w27, w1
	add	w19, w19, w1
	uxth	w19, w19
.L1152:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w26, 0
	add	x28, x21, 3424
.L1153:
	ldr	w0, [x28,2120]
	cmp	w26, w0
	bcs	.L1169
	add	x0, x28, x26, sxtw 1
	ldrh	w0, [x0,2124]
	cmp	w0, w19
	bne	.L1154
	ldr	x0, [x29,104]
	mov	w1, w19
	bl	printk
	add	x0, x23, :lo12:.LANCHOR3
	ldrb	w1, [x0,1868]
	cbnz	w1, .L1155
	ldrb	w0, [x0,1250]
	cbz	w0, .L1156
.L1155:
	ldr	x0, [x21,3392]
	ldr	w1, [x0,156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	beq	.L1157
.L1156:
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1232]
	lsl	w0, w19, w0
	bl	ftl_mask_bad_block
.L1157:
	ldr	w1, [x28,2120]
	mov	w0, w26
.L1158:
	cmp	w0, w1
	bcs	.L1170
	add	w2, w0, 1
	add	x0, x28, x0, sxtw 1
	add	x4, x28, x2, sxtw 1
	ldrh	w4, [x4,2124]
	strh	w4, [x0,2124]
	uxth	w0, w2
	b	.L1158
.L1170:
	sub	w1, w1, #1
	str	w1, [x28,2120]
.L1154:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L1153
.L1169:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1168
.L1167:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	gc_free_bad_sblk, .-gc_free_bad_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x4, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxth	x24, w0
	ldr	x5, [x4,1096]
	lsl	x19, x24, 2
	mov	x22, x24
	add	x21, x5, x19
	ldrb	w2, [x21,2]
	ubfx	x23, x2, 5, 3
	tbz	x2, 3, .L1172
	ldr	x6, [x4,3392]
	ldrh	w0, [x5,x19]
	ldr	w3, [x5,x19]
	ldrh	w7, [x6,74]
	and	w1, w0, 2047
	ldrh	w6, [x6,72]
	ubfx	x8, x3, 11, 8
	add	w7, w1, w7
	ldrh	w3, [x4,1104]
	add	w6, w8, w6
	uxth	w0, w7
	uxth	w7, w6
	adrp	x6, .LANCHOR3
	add	x6, x6, :lo12:.LANCHOR3
	udiv	w9, w0, w3
	ldrh	w10, [x6,1870]
	add	w9, w7, w9
	cmp	w9, w10
	ble	.L1173
	ldrh	w0, [x4,3378]
	ldrh	w7, [x4,3386]
	add	w7, w7, w0
	ldrh	w0, [x6,1300]
	add	w0, w0, 8
	cmp	w7, w0
	blt	.L1179
	ldrh	w0, [x4,3376]
	ldrh	w7, [x4,3382]
	add	w7, w7, w0
	ldrh	w0, [x4,3384]
	add	w4, w7, w0
	ldrh	w0, [x6,1298]
	add	w0, w0, 8
	cmp	w4, w0
	b	.L1193
.L1173:
	madd	w7, w7, w3, w0
	ldrh	w0, [x6,1872]
	cmp	w7, w0
	ble	.L1175
	ldrh	w0, [x4,3376]
	ldrh	w7, [x4,3382]
	add	w7, w7, w0
	ldrh	w0, [x4,3384]
	add	w7, w7, w0
	ldrh	w0, [x6,1298]
	add	w9, w0, 8
	cmp	w7, w9
	blt	.L1191
	ldrh	w9, [x4,3386]
	ldrh	w4, [x4,3378]
	add	w9, w9, w4
	ldrh	w4, [x6,1300]
	add	w4, w4, 8
	cmp	w9, w4
	blt	.L1179
	add	w0, w0, 24
	cmp	w7, w0
.L1193:
	bge	.L1179
.L1191:
	and	w2, w2, -25
	b	.L1190
.L1179:
	mov	w0, 2
	bfi	w2, w0, 3, 2
.L1190:
	strb	w2, [x21,2]
.L1175:
	ldrb	w0, [x21,2]
	ands	w0, w0, 24
	bne	.L1180
	mul	w0, w8, w3
	lsr	w3, w8, 3
	add	w0, w0, w0, lsl 1
	add	w0, w1, w0, lsr 2
	ldrh	w1, [x5,x19]
	bfi	w1, w0, 0, 11
	strh	w1, [x5,x19]
	ldr	w0, [x5,x19]
	bfi	w0, w3, 11, 8
	str	w0, [x5,x19]
	b	.L1181
.L1180:
	cmp	w0, 16
	bne	.L1181
	sdiv	w3, w1, w3
	ldr	w0, [x5,x19]
	add	w3, w3, w3, lsl 1
	add	w3, w8, w3, lsr 2
	bfi	w0, w3, 11, 8
	asr	w3, w1, 5
	str	w0, [x5,x19]
	bfi	w0, w3, 0, 11
	strh	w0, [x5,x19]
	b	.L1181
.L1172:
	tst	w2, 24
	bne	.L1181
	ldrh	w0, [x4,3380]
	cbnz	w0, .L1181
	ldrh	w0, [x4,3378]
	cmp	w0, 15
	bhi	.L1181
	ldrh	w1, [x4,3376]
	ldrh	w6, [x4,3382]
	add	w6, w6, w1
	ldrh	w1, [x4,3384]
	add	w6, w6, w1
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldrh	w3, [x1,1298]
	add	w3, w3, 16
	cmp	w6, w3
	ble	.L1181
	ldrh	w3, [x4,3386]
	ldrh	w1, [x1,1300]
	add	w0, w3, w0
	add	w1, w1, 8
	cmp	w0, w1
	bge	.L1181
	mov	w0, 2
	bfi	w2, w0, 3, 2
	strb	w2, [x21,2]
	ldrh	w2, [x4,1104]
	ldrh	w0, [x5,x19]
	and	w0, w0, 2047
	sdiv	w2, w0, w2
	asr	w0, w0, 5
	add	w3, w2, w2, lsl 1
	ldr	w2, [x5,x19]
	ubfx	x1, x2, 11, 8
	add	w3, w1, w3, lsr 2
	bfi	w2, w3, 11, 8
	str	w2, [x5,x19]
	bfi	w2, w0, 0, 11
	strh	w2, [x5,x19]
.L1181:
	mov	w0, w22
	add	w23, w23, 6
	and	w23, w23, 7
	bl	zftl_remove_data_node
	add	x1, x20, :lo12:.LANCHOR0
	cmp	w23, 4
	ldr	x2, [x1,1096]
	add	x2, x2, x19
	ldrb	w0, [x2,2]
	and	w0, w0, 31
	strb	w0, [x2,2]
	ldr	x0, [x1,1112]
	strh	wzr, [x0,x24,lsl 1]
	bhi	.L1184
	mov	w0, w22
	bl	gc_free_bad_sblk
.L1184:
	ldrb	w0, [x21,2]
	tbz	x0, 3, .L1185
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,1120]
	ldrh	w1, [x0,586]
	cmp	w1, w22
	bne	.L1185
	mov	w1, -1
	ldrh	w2, [x0,590]
	strh	w1, [x0,586]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L1185
	strh	w22, [x0,590]
	adrp	x0, .LC112
	mov	w1, w22
	add	x0, x0, :lo12:.LC112
	bl	printk
	b	.L1171
.L1185:
	mov	w0, w22
	bl	zftl_insert_free_list
.L1171:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_free_sblk, .-ftl_free_sblk
	.align	2
	.global	gc_free_src_blk
	.type	gc_free_src_blk, %function
gc_free_src_blk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	adrp	x23, .LANCHOR0
	adrp	x21, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR1
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	w20, 0
	add	x24, x23, 3424
	add	x21, x21, 1920
.L1195:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L1233
	add	x0, x24, x20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,1112]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L1196
	adrp	x0, .LC113
	mov	w1, w22
	add	x0, x0, :lo12:.LC113
	bl	printk
.L1196:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x26]
	ldr	x1, [x0,1112]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L1197
	ldr	x0, [x0,1096]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L1198
	ldrb	w2, [x22,2]
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L1198:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L1211
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L1199
.L1211:
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 1362
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1199:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,1120]
	ldrh	w1, [x2,124]
	cbz	w1, .L1201
	mov	x0, 0
.L1203:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L1202
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L1201
.L1202:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1203
.L1201:
	ldrh	w1, [x2,120]
	cbz	w1, .L1204
	mov	x0, 0
.L1206:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L1205
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L1204
.L1205:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1206
.L1204:
	ldrh	w1, [x2,122]
	cbz	w1, .L1207
	mov	x0, 0
.L1209:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L1208
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L1207
.L1208:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1209
	b	.L1207
.L1197:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L1207:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1195
.L1233:
	strh	wzr, [x24,56]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_free_src_blk, .-gc_free_src_blk
	.align	2
	.global	print_ftl_debug_info
	.type	print_ftl_debug_info, %function
print_ftl_debug_info:
	sub	sp, sp, #64
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	str	x19, [sp,48]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x2, [x19,3392]
	ldr	x0, [x19,1120]
	ldrh	w8, [x19,3386]
	ldrh	w1, [x2,148]
	ldrh	w2, [x2,146]
	str	w8, [sp]
	ldrh	w6, [x19,3382]
	ldr	w8, [x0,524]
	str	w8, [sp,8]
	ldrh	w7, [x19,3384]
	ldr	w0, [x0,528]
	str	w0, [sp,16]
	ldr	w0, [x19,3372]
	ldrh	w3, [x19,3376]
	ldrh	w4, [x19,3378]
	ldrh	w5, [x19,3380]
	str	w0, [sp,24]
	adrp	x0, .LC115
	add	x0, x0, :lo12:.LC115
	bl	printk
	ldr	x5, [x19,3392]
	adrp	x0, .LC116
	ldrb	w3, [x19,3389]
	add	x0, x0, :lo12:.LC116
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,3392]
	adrp	x0, .LC117
	add	x0, x0, :lo12:.LC117
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,3392]
	ldrh	w0, [x7,90]
	ldrh	w6, [x7,72]
	ldrh	w1, [x7,74]
	ldrh	w3, [x7,88]
	ldrh	w4, [x7,92]
	ldrh	w5, [x7,96]
	str	w0, [sp]
	ldrh	w0, [x7,94]
	str	w0, [sp,8]
	ldrh	w0, [x7,98]
	str	w0, [sp,16]
	adrp	x0, .LC118
	add	x0, x0, :lo12:.LC118
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
	bl	printk
	ldrh	w1, [x19,3402]
	adrp	x0, .LC119
	ldrh	w2, [x19,3404]
	add	x0, x0, :lo12:.LC119
	ldrh	w3, [x19,3406]
	ldrh	w4, [x19,3408]
	ldrh	w5, [x19,3400]
	bl	printk
	ldr	x4, [x19,1120]
	adrp	x0, .LC120
	ldr	x6, [x19,3392]
	add	x0, x0, :lo12:.LC120
	ldrh	w1, [x4,584]
	ldrh	w2, [x4,586]
	ldrh	w3, [x4,588]
	ldr	w5, [x6,156]
	ldrh	w4, [x4,590]
	ldrh	w6, [x6,150]
	bl	printk
	sub	sp, x29, #32
	ldr	x19, [sp,48]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 64
	ret
	.size	print_ftl_debug_info, .-print_ftl_debug_info
	.align	2
	.global	ftl_write_buf
	.type	ftl_write_buf, %function
ftl_write_buf:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	cbnz	x0, .L1236
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 803
	add	x1, x1, 1936
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L1245
.L1236:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w1, w0
	bls	.L1241
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 810
	add	x1, x1, 1936
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1241:
	ldrb	w0, [x20,56]
	cbz	w0, .L1238
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,3360]
	cmp	w1, w0
	bcs	.L1239
.L1238:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	zbuf_free
	ldrb	w0, [x19,3388]
	b	.L1245
.L1239:
	add	x0, x21, 3416
	mov	x1, x20
	bl	buf_add_tail
	ldr	x0, [x21,3392]
	ldrb	w1, [x20,56]
	ldrb	w2, [x21,3388]
	ldr	w3, [x0,16]
	add	w2, w2, 1
	add	w1, w3, w1
	str	w1, [x0,16]
	ldr	w1, [x0,32]
	uxtb	w2, w2
	strb	w2, [x21,3388]
	add	w1, w1, 1
	str	w1, [x0,32]
	mov	w0, w2
.L1245:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_write_buf, .-ftl_write_buf
	.align	2
	.global	ftl_write_completed
	.type	ftl_write_completed, %function
ftl_write_completed:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	add	x21, x19, 1312
	mov	x20, x19
	stp	x23, x24, [sp,48]
	mov	x22, x21
	mov	w23, 0
.L1247:
	ldrb	w2, [x19,3366]
	cmp	w2, 255
	beq	.L1262
	lsl	x0, x2, 6
	add	x24, x21, x0
	ldrb	w0, [x21,x0]
	strb	w0, [x19,3366]
	ldr	w0, [x24,52]
	cmn	w0, #1
	bne	.L1248
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	ldrb	w3, [x1,1868]
	cbnz	w3, .L1249
	ldrb	w1, [x1,1250]
	cbz	w1, .L1250
.L1249:
	ldr	x1, [x20,3392]
	ldr	w3, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w3, w1
	beq	.L1251
.L1250:
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w4, [x20,1257]
	mov	w3, 24
	add	x5, x22, x2, lsl 6
	sub	w4, w3, w4
	mov	w3, 1
	ldrh	w1, [x0,1232]
	add	x5, x5, 32
	ldrb	w0, [x0,1234]
	sub	w4, w4, w1
	lsl	w4, w3, w4
	ldr	w3, [x5,8]
	sub	w4, w4, #1
	lsr	w1, w3, w1
	mov	w3, 65535
	and	w1, w4, w1
	udiv	w1, w1, w0
	ldr	x0, [x20,1120]
	ldr	w4, [x0,560]
	cmp	w4, w3
	uxth	w23, w1
	and	w1, w1, w3
	bne	.L1252
	str	w1, [x0,560]
	ldr	w3, [x5,8]
	str	w3, [x0,564]
.L1252:
	add	x2, x22, x2, lsl 6
	adrp	x0, .LC121
	add	x3, x2, 32
	add	x0, x0, :lo12:.LC121
	ldr	w2, [x2,36]
	ldr	w3, [x3,8]
	bl	printk
	ldr	x0, [x20,1120]
	ldr	w1, [x0,556]
	add	w1, w1, 1
	str	w1, [x0,556]
	ldrh	w1, [x0,16]
	cmp	w1, w23
	bne	.L1253
	strh	wzr, [x0,22]
	b	.L1251
.L1253:
	ldrh	w1, [x0,48]
	cmp	w1, w23
	bne	.L1251
	strh	wzr, [x0,54]
.L1251:
	mov	x0, x24
	mov	w23, 1
	bl	ftl_write_buf
	b	.L1247
.L1248:
	cmp	w23, 1
	add	x2, x24, 32
	bne	.L1256
	ldr	w1, [x2,4]
	adrp	x0, .LC122
	ldr	w2, [x2,8]
	add	x0, x0, :lo12:.LC122
	bl	printk
	mov	x0, x24
	bl	ftl_write_buf
	b	.L1247
.L1256:
	ldr	w0, [x2,4]
	ldr	w1, [x2,12]
	ldrh	w2, [x24,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x24,2]
	and	w23, w0, 4
	uxtb	w23, w23
	cbz	w23, .L1257
	and	w0, w0, -3
	mov	w23, 0
	strb	w0, [x24,2]
	b	.L1247
.L1257:
	mov	x0, x24
	bl	zbuf_free
	b	.L1247
.L1262:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_write_completed, .-ftl_write_completed
	.align	2
	.global	zftl_add_read_buf
	.type	zftl_add_read_buf, %function
zftl_add_read_buf:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	cbnz	x0, .L1264
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1143
	add	x1, x1, 1952
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	b	.L1263
.L1264:
	ldrb	w1, [x0,56]
	adrp	x0, .LANCHOR0+3360
	ldrb	w0, [x0,#:lo12:.LANCHOR0+3360]
	cmp	w1, w0
	bls	.L1267
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1150
	add	x1, x1, 1952
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1267:
	adrp	x19, .LANCHOR3
	mov	x1, x20
	add	x19, x19, :lo12:.LANCHOR3
	add	x0, x19, 1874
	bl	buf_add_tail
	ldrb	w0, [x19,1875]
	add	w0, w0, 1
	strb	w0, [x19,1875]
.L1263:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_add_read_buf, .-zftl_add_read_buf
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	stp	x29, x30, [sp, -64]!
	cmp	w0, 5
	add	x29, sp, 0
	str	x23, [sp,48]
	uxth	w23, w0
	stp	x21, x22, [sp,32]
	mov	w1, w23
	mov	w21, w0
	mov	w0, 0
	mov	w22, 2
	stp	x19, x20, [sp,16]
	csel	w22, wzr, w22, ne
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1270
	adrp	x0, .LANCHOR0+1096
	ldr	x20, [x0,#:lo12:.LANCHOR0+1096]
	add	x20, x20, x19, uxth 2
	ldrb	w0, [x20,2]
	tst	w0, 224
	beq	.L1271
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 939
	add	x1, x1, 1976
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1271:
	ldrb	w1, [x20,2]
	bfi	w1, w21, 5, 3
	ubfx	x2, x1, 3, 2
	mov	w0, w1
	orr	w2, w22, w2
	bfi	w0, w2, 3, 2
	strb	w0, [x20,2]
	cbnz	w22, .L1274
	and	w1, w0, 24
	cmp	w1, 24
	bne	.L1274
	mov	w1, 1
	bfi	w0, w1, 3, 2
	strb	w0, [x20,2]
	b	.L1274
.L1270:
	adrp	x20, .LC123
	bl	print_ftl_debug_info
	add	x20, x20, :lo12:.LC123
	mov	w1, w19
	mov	w2, w21
	mov	x0, x20
	bl	printk
	mov	w1, w23
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w1, w19
	mov	w2, w21
	mov	x0, x20
	bl	printk
	bl	dump_all_list_info
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 955
	add	x1, x1, 1976
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1274:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	adrp	x0, .LANCHOR0
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,3363]
	strb	w1, [x0,3366]
	strb	w1, [x0,3364]
	strb	w1, [x0,3365]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC124
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC124
	ldrb	w1, [x20,3363]
	bl	printk
	ldrb	w19, [x20,3363]
	cmp	w19, 255
	beq	.L1285
	add	x20, x20, 1312
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC125
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC125
.L1287:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1285
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1287
.L1285:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.type	queue_lun_state, %function
queue_lun_state:
	adrp	x3, .LANCHOR0
	mov	w4, w0
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, 0
	ldrb	w2, [x3,3363]
	cmp	w2, 255
	beq	.L1295
	ldrb	w5, [x3,1257]
	mov	w0, 1
	mov	w8, 24
	adrp	x13, .L1298
	sub	w8, w8, w5
	lsl	w5, w0, w5
	sub	w5, w5, #1
	asr	w10, w4, w8
	add	x3, x3, 1312
	add	x13, x13, :lo12:.L1298
	uxth	w7, w5
	adrp	x5, .LANCHOR3
	add	x5, x5, :lo12:.LANCHOR3
	and	w10, w7, w10
	ldrh	w11, [x5,1232]
	ldrb	w12, [x5,1234]
	sub	w6, w8, w11
	asr	w4, w4, w11
	lsl	w0, w0, w6
	sub	w12, w12, #1
	sub	w0, w0, #1
	uxth	w12, w12
	uxth	w9, w0
	and	w4, w4, w12
	and	w4, w4, w9
.L1301:
	add	x6, x3, x2, lsl 6
	ldr	w5, [x6,40]
	lsr	w0, w5, w8
	and	w0, w7, w0
	cmp	w0, w10
	bne	.L1296
	lsr	w5, w5, w11
	ldrb	w0, [x6,58]
	and	w5, w12, w5
	and	w5, w9, w5
	cmp	w5, w4
	bne	.L1297
	cmp	w1, 1
	beq	.L1299
	b	.L1295
.L1297:
	cmp	w1, 3
	bhi	.L1296
	ldrb	w5, [x13,w1,uxtw]
	adr	x6, .Lrtx1298
	add	x5, x6, w5, sxtb #2
	br	x5
.Lrtx1298:
	.section	.rodata
	.align	0
	.align	2
.L1298:
	.byte	(.L1295 - .Lrtx1298) / 4
	.byte	(.L1299 - .Lrtx1298) / 4
	.byte	(.L1300 - .Lrtx1298) / 4
	.byte	(.L1295 - .Lrtx1298) / 4
	.text
.L1299:
	sub	w6, w0, #7
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L1296
	b	.L1295
.L1300:
	cmp	w0, 11
	bne	.L1295
.L1296:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L1301
	mov	w0, 0
.L1295:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x3, .LANCHOR0
	mov	w5, 0
	add	x0, x3, :lo12:.LANCHOR0
	mov	w11, -1
	add	x4, x0, 1312
	add	x12, x0, 3364
	add	x13, x0, 3365
	add	x14, x0, 3366
	ldrb	w7, [x0,3363]
	ldrb	w9, [x0,3364]
	ldrb	w8, [x0,3365]
	ldrb	w10, [x0,3366]
.L1311:
	cmp	w7, 255
	beq	.L1312
	sbfiz	x6, x7, 6, 32
	sxtw	x1, w7
	add	x2, x4, x6
	ldrb	w2, [x2,58]
	sub	w2, w2, #12
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1313
	cbz	w5, .L1310
	strb	w7, [x0,3363]
	b	.L1310
.L1313:
	lsl	x5, x1, 6
	ldrb	w7, [x4,x6]
	add	x2, x4, x5
	strb	w11, [x4,x5]
	ldrb	w5, [x2,59]
	cmp	w5, 1
	bne	.L1316
	ldrh	w2, [x2,50]
	cbnz	w2, .L1324
	mov	w2, w10
	mov	x5, x14
	b	.L1317
.L1316:
	cbz	w5, .L1334
.L1318:
	mov	w5, 1
	b	.L1311
.L1334:
	ldr	w2, [x2,36]
	cmn	w2, #1
	beq	.L1318
	mov	w2, w9
	mov	x5, x12
	b	.L1317
.L1324:
	mov	w2, w8
	mov	x5, x13
.L1317:
	cmp	w2, 255
	bne	.L1322
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x3, x1, lsl 6
	strb	w7, [x3,3363]
	ldrb	w0, [x1,1313]
	strb	w0, [x5]
	b	.L1310
.L1322:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x4,x2]
	cmp	w2, 255
	bne	.L1322
	add	x1, x4, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1,1]
	strb	w1, [x4,x5]
	b	.L1318
.L1312:
	cbz	w5, .L1310
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, -1
	strb	w0, [x3,3363]
.L1310:
	ret
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_alloc_new_blk
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x2, [x0,3392]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	add	x1, x20, :lo12:.LANCHOR3
	strh	w0, [x2,690]
	ldrb	w1, [x1,1249]
	cmp	w1, w0
	bls	.L1336
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1337
.L1336:
	adrp	x23, .LC126
	mov	w24, 65535
	add	x23, x23, :lo12:.LC126
.L1338:
	mov	w0, 1
	bl	ftl_alloc_sblk
	uxth	w22, w0
	cmp	w22, w24
	beq	.L1338
	mov	w1, 0
	mov	w0, w22
	add	x21, x19, :lo12:.LANCHOR0
	bl	ftl_erase_sblk
	mov	w0, w22
	ldr	x1, [x21,3392]
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L1339
	mov	w1, w22
	mov	x0, x23
	bl	printk
	ldr	x1, [x21,1096]
	add	x22, x1, x22, uxth 2
	ldrb	w1, [x22,2]
	orr	w1, w1, -32
	strb	w1, [x22,2]
	b	.L1338
.L1339:
	ldr	x1, [x21,3392]
	add	x0, x20, :lo12:.LANCHOR3
	mov	w2, 1
	mov	x21, 0
	str	w2, [x0,1876]
	mov	w2, 65535
	strh	wzr, [x1,690]
.L1341:
	add	x3, x1, x21, lsl 1
	uxth	w0, w21
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L1340
	add	x21, x21, 1
	cmp	x21, 128
	bne	.L1341
	adrp	x0, .LC0
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 264
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, w21
.L1340:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	add	x0, x1, x0, sxtw 1
	strh	w22, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L1337:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w21, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w21, #1
	cmp	w0, w1, uxth
	bcs	.L1343
	adrp	x0, .LC0
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 270
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1343:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	mov	w0, 24
	ldrb	w2, [x19,1257]
	ldr	x1, [x19,3392]
	sub	w0, w0, w2
	ldrh	w2, [x20,1232]
	sub	w0, w0, w2
	strh	wzr, [x1,696]
	asr	w4, w21, w0
	strh	w21, [x1,692]
	strh	w4, [x1,694]
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L1347
	mov	w2, 1
	mov	w1, w21
	lsl	w2, w2, w0
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	sub	w2, w2, #1
	mov	w3, w21
	uxth	w4, w4
	bl	printk
.L1347:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR3
	mov	w3, 65535
	add	x29, sp, 0
	str	x19, [sp,16]
	add	x2, x2, :lo12:.LANCHOR3
	mov	x19, 0
.L1354:
	add	x1, x2, 1880
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1353
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1354
	mov	x0, 0
	mov	w3, 32768
.L1356:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1355
	cmp	w2, w3
	bcs	.L1355
	mov	w3, w2
	mov	w19, w4
.L1355:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1356
	cmp	w19, 32
	mov	w0, w19
	bne	.L1353
	adrp	x0, .LANCHOR3
	mov	w2, -1
	add	x0, x0, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x0, 1880
	ldrb	w3, [x0,512]
.L1358:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1357
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1357:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1358
	cmp	w19, 32
	bne	.L1359
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 377
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1359:
	mov	w0, w19
.L1353:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	pm_select_ram_region, .-pm_select_ram_region
	.align	2
	.global	ftl_memset
	.type	ftl_memset, %function
ftl_memset:
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	add	x29, sp, 0
	bl	memset
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	flash_lsb_page_tbl_build
	.type	flash_lsb_page_tbl_build, %function
flash_lsb_page_tbl_build:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x21, [sp,32]
	add	x1, x19, :lo12:.LANCHOR0
	mov	w21, 1024
	ldr	x2, [x1,1136]
	ldrb	w2, [x2,12]
	sdiv	w21, w21, w2
	cbnz	w0, .L1367
	mov	x0, 0
.L1368:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1368
.L1374:
	adrp	x20, .LANCHOR3
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR3
	mov	w2, 2048
	add	x20, x20, 2396
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_memset
	mov	x0, 0
	b	.L1369
.L1367:
	cmp	w0, 1
	bne	.L1370
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
.L1373:
	cmp	x0, 3
	uxth	w3, w0
	mov	w2, w3
	bls	.L1371
	ubfiz	w2, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w3, w4, w5, ne
	sub	w2, w2, w3
	uxth	w2, w2
.L1371:
	add	x3, x1, 4
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1373
	b	.L1374
.L1370:
	cmp	w0, 2
	bne	.L1375
	mov	w2, 65535
	mov	x0, 0
.L1377:
	add	x4, x1, 4
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w2, w3, hi
	add	w2, w2, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	uxth	w2, w2
	bne	.L1377
	b	.L1374
.L1375:
	cmp	w0, 3
	bne	.L1378
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
.L1381:
	cmp	x0, 5
	uxth	w3, w0
	mov	w2, w3
	bls	.L1379
	ubfiz	w2, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w3, w4, w5, ne
	sub	w2, w2, w3
	uxth	w2, w2
.L1379:
	add	x3, x1, 4
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1381
	b	.L1374
.L1378:
	cmp	w0, 4
	bne	.L1382
	add	x2, x1, 4
	strh	w0, [x2,8]
	mov	w0, 5
	strh	wzr, [x1,4]
	mov	w1, 1
	mov	w5, 7
	strh	w0, [x2,10]
	mov	w0, 7
	strh	w1, [x2,2]
	mov	w1, 2
	strh	w0, [x2,12]
	mov	w0, 8
	strh	w1, [x2,4]
	mov	w4, 6
	mov	w1, 3
	strh	w0, [x2,14]
	strh	w1, [x2,6]
	mov	w0, 8
.L1384:
	and	w3, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x2, x2, 2
	uxth	w0, w0
	sub	w1, w1, w3
	cmp	w0, 512
	strh	w1, [x2,14]
	bne	.L1384
	b	.L1374
.L1382:
	cmp	w0, 5
	bne	.L1385
	mov	x0, 0
.L1386:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1386
	mov	x0, 0
.L1387:
	add	x1, x2, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 992
	strh	w3, [x1,32]
	bne	.L1387
	b	.L1374
.L1385:
	cmp	w0, 8
	bne	.L1388
	mov	x0, 0
.L1389:
	add	x2, x1, 4
	strh	w0, [x0,x2]
	add	x0, x0, 2
	cmp	x0, 1024
	bne	.L1389
	b	.L1374
.L1388:
	cmp	w0, 9
	bne	.L1390
	add	x2, x1, 4
	mov	w0, 1
	strh	wzr, [x1,4]
	strh	w0, [x2,2]
	mov	w0, 2
	strh	w0, [x2,4]
	mov	x0, 0
.L1391:
	add	x1, x2, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 1018
	strh	w3, [x1,6]
	bne	.L1391
	b	.L1374
.L1390:
	cmp	w0, 10
	bne	.L1392
	mov	x0, 0
.L1393:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L1393
	mov	x0, 0
.L1394:
	add	x1, x2, x0
	add	w3, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w3, [x1,126]
	bne	.L1394
	b	.L1374
.L1392:
	cmp	w0, 11
	bne	.L1395
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1396:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1396
	mov	w5, 7
	mov	w4, 6
.L1398:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x1, x1, 2
	uxth	w0, w0
	sub	w2, w2, w3
	cmp	w0, 512
	strh	w2, [x1,14]
	bne	.L1398
	b	.L1374
.L1395:
	cmp	w0, 13
	bne	.L1374
	mov	x0, 0
	add	x3, x19, :lo12:.LANCHOR0
.L1399:
	add	x1, x3, 4
	add	w2, w0, w0, lsl 1
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1399
	b	.L1374
.L1369:
	cmp	w21, w0, uxth
	bls	.L1422
	add	x1, x19, 4
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x20,w1,sxtw 1]
	b	.L1369
.L1422:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.type	flash_die_info_init, %function
flash_die_info_init:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	tbz	x0, 12, .L1424
	adrp	x1, .LANCHOR4
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L1424:
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR5
	add	x25, x22, :lo12:.LANCHOR5
	mov	w2, 8
	ldrh	w0, [x24,34]
	mov	x23, 0
	ldrb	w1, [x24,20]
	mov	w26, 2
	strh	w0, [x20,2]
	add	x27, x24, 9
	ldrh	w0, [x24,18]
	add	x28, x20, 1272
	strb	wzr, [x20,1145]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x25,92]
	add	x0, x20, 1248
	bl	ftl_memset
	add	x0, x25, 96
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
.L1426:
	add	x0, x20, 1264
	ldrb	w2, [x24,8]
	add	x1, x28, x23, lsl 3
	strb	w26, [x23,x0]
	mov	x0, x27
	bl	flash_mem_cmp8
	cbnz	w0, .L1425
	ldrb	w1, [x20,1145]
	add	x2, x25, 96
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	strb	w0, [x20,1145]
	uxtb	w0, w23
	add	x1, x20, x1, sxtw
	strb	w0, [x1,1248]
	bl	zftl_flash_enter_slc_mode
.L1425:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L1426
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 8
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1427
.L1431:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR5
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,21]
	ldrb	w0, [x21,1145]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,22]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,128]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1427:
	add	x23, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w26, [x0,8]
	mov	x24, 0
	and	w20, w20, 65280
	ldrb	w27, [x1,23]
	ldrh	w2, [x23,2]
	add	x25, x0, 9
	add	x3, x23, 1272
	add	x4, x22, :lo12:.LANCHOR5
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L1430:
	add	x1, x3, x24, lsl 3
	mov	x0, x25
	mov	w2, w26
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1428
	ldrb	w0, [x23,1145]
	add	x1, x4, 96
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x1,w0,sxtw 2]
	add	w1, w0, 1
	add	x0, x23, x0, sxtw
	strb	w1, [x23,1145]
	strb	w24, [x0,1248]
.L1428:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1430
	b	.L1431
	.size	flash_die_info_init, .-flash_die_info_init
	.align	2
	.global	lpa_hash_init
	.type	lpa_hash_init, %function
lpa_hash_init:
	stp	x29, x30, [sp, -32]!
	mov	w1, 255
	mov	w2, 512
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	add	x0, x19, 1336
	bl	ftl_memset
	ldrh	w0, [x19,1294]
	mov	w1, 255
	ldrb	w2, [x19,1249]
	mul	w2, w0, w2
	ldr	x0, [x19,1856]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	lpa_hash_init, .-lpa_hash_init
	.align	2
	.global	lpa_rebuild_hash
	.type	lpa_rebuild_hash, %function
lpa_rebuild_hash:
	adrp	x0, .LANCHOR2
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	str	x19, [sp,16]
	tbz	x0, 12, .L1442
	adrp	x1, .LANCHOR4
	adrp	x0, .LC128
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC128
	add	x1, x1, 72
	mov	w2, 239
	mov	w3, 0
	bl	printk
.L1442:
	adrp	x19, .LANCHOR3
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR3
	mov	w2, 512
	add	x0, x19, 1336
	bl	ftl_memset
	ldrh	w0, [x19,1294]
	mov	w1, 255
	ldrb	w2, [x19,1249]
	mul	w2, w0, w2
	ldr	x0, [x19,1856]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L1443:
	ldrh	w1, [x19,1294]
	ldrb	w2, [x19,1249]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1452
	uxtw	x2, w0
	ldr	x1, [x19,1848]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1444
	uxtb	w1, w1
	add	x3, x19, 1336
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,1856]
	strh	w4, [x1,x2,lsl 1]
.L1444:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1443
.L1452:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	lpa_rebuild_hash, .-lpa_rebuild_hash
	.align	2
	.global	zftl_read_flash_info
	.type	zftl_read_flash_info, %function
zftl_read_flash_info:
	stp	x29, x30, [sp, -32]!
	mov	w1, 0
	mov	w2, 11
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	bl	ftl_memset
	adrp	x1, .LANCHOR2
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 8
	strb	wzr, [x19,10]
	mov	w5, 1
	ldrb	w2, [x1,9]
	ldrh	w3, [x0,2]
	mul	w2, w2, w3
	strh	w2, [x19,4]
	ldrb	w2, [x0,1236]
	strb	w2, [x19,7]
	ldr	w2, [x0,1032]
	str	w2, [x19]
	ldrb	w2, [x1,9]
	strb	w2, [x19,6]
	mov	w2, 32
	ldrb	w4, [x0,1145]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L1454:
	cmp	w4, w1, uxtb
	bls	.L1456
	add	x2, x0, 1248
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1454
.L1456:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_read_flash_info, .-zftl_read_flash_info
	.align	2
	.global	gc_init
	.type	gc_init, %function
gc_init:
	stp	x29, x30, [sp, -48]!
	mov	w1, 0
	mov	w2, 2136
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR3
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR5
	add	x22, x20, 3424
	add	x21, x21, :lo12:.LANCHOR5
	mov	x0, x22
	strb	wzr, [x19,1265]
	strh	wzr, [x19,1296]
	strb	wzr, [x21,130]
	str	wzr, [x21,132]
	bl	ftl_memset
	ldrh	w1, [x19,1294]
	mov	w0, -1
	ldrb	w3, [x19,1249]
	strh	w0, [x20,3424]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x20,3460]
	strh	w0, [x20,3406]
	ldrh	w0, [x19,1266]
	strh	w2, [x20,3462]
	mul	w2, w1, w3
	mul	w0, w0, w3
	strh	w1, [x20,3408]
	mov	w1, 4
	uxth	w2, w2
	lsl	w0, w0, 2
	sub	w4, w2, #32
	strh	w2, [x20,3402]
	strh	w4, [x20,3404]
	strh	w1, [x21,136]
	strh	wzr, [x19,1288]
	strh	wzr, [x19,1290]
	strh	wzr, [x19,1292]
	str	xzr, [x20,3432]
	bl	ftl_malloc
	ldrb	w1, [x19,1249]
	str	x0, [x21,144]
	ldrh	w0, [x19,1266]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x21,152]
	ldrh	w1, [x19,1266]
	ldrb	w0, [x19,1249]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1256]
	ldrb	w1, [x19,1249]
	ldrh	w0, [x19,1266]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1240]
	ldrb	w1, [x19,1249]
	ldrh	w0, [x19,1266]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x21,160]
	ldrh	w0, [x19,1298]
	ldp	x21, x22, [sp,32]
	lsr	w0, w0, 2
	strh	w0, [x20,3400]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	gc_init, .-gc_init
	.align	2
	.global	gc_static_wearleveling
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x0, [x0,3392]
	ldr	w1, [x0,32]
	mov	w0, 10240
	cmp	w1, w0
	bls	.L1459
	bl	ftl_tmp_into_update
.L1459:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	x1, [x4,1120]
	ldr	w2, [x1,568]
	ldr	w3, [x1,12]
	add	w0, w2, 32768
	add	w0, w0, 3232
	cmp	w3, w0
	bcs	.L1460
	ldr	x0, [x4,3392]
	ldr	w4, [x1,572]
	ldr	w5, [x0,36]
	add	w4, w4, 256
	mov	w0, 0
	cmp	w5, w4
	bcc	.L1553
.L1460:
	add	w2, w2, 860160
	add	w2, w2, 3840
	cmp	w3, w2
	bhi	.L1462
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x2, [x0,3392]
	ldr	w0, [x1,572]
	ldr	w2, [x2,36]
	add	w0, w0, 32
	cmp	w2, w0
	bls	.L1514
.L1462:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w20, 65535
	adrp	x25, .LC129
	str	w22, [x29,128]
	mov	w27, w22
	ldr	x0, [x0,3392]
	mov	w26, w22
	str	w22, [x29,136]
	mov	w24, w22
	str	w22, [x29,140]
	mov	w28, w22
	ldr	w2, [x0,36]
	mov	w21, w20
	str	w2, [x1,572]
	mov	w8, w20
	str	w3, [x1,568]
	adrp	x9, .LANCHOR2
	add	x25, x25, :lo12:.LC129
	ldrh	w23, [x0,134]
.L1464:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,1088]
	cmp	w0, w23
	bls	.L1575
	uxtw	x7, w23
	ldr	x0, [x4,1096]
	lsl	x1, x7, 2
	add	x6, x0, x1
	ldrb	w2, [x6,2]
	and	w3, w2, 224
	cmp	w3, 224
	beq	.L1465
	tbz	x2, 3, .L1466
	ldrh	w3, [x0,x1]
	ldr	w2, [x0,x1]
	and	w3, w3, 2047
	ubfx	x2, x2, 11, 8
	b	.L1467
.L1466:
	tst	w2, 24
	bne	.L1468
	ldrh	w3, [x0,x1]
	mov	w2, 65535
	and	w3, w3, 2047
	b	.L1467
.L1468:
	ldr	w2, [x0,x1]
	mov	w3, 65535
	ubfx	x2, x2, 11, 8
	b	.L1469
.L1467:
	ldr	w4, [x29,136]
	add	w26, w26, 1
	cmp	w20, w3
	add	w4, w4, w3
	str	w4, [x29,136]
	uxth	w26, w26
	bls	.L1512
	add	x4, x19, :lo12:.LANCHOR0
	mov	w20, w3
	ldr	x4, [x4,1120]
	strh	w23, [x4,586]
.L1512:
	cmp	w24, w3
	bcs	.L1470
	mov	w22, w23
	mov	w24, w3
.L1470:
	cmp	w2, w8
	beq	.L1471
.L1469:
	ldr	w4, [x29,140]
	add	w27, w27, 1
	cmp	w21, w2
	add	w4, w4, w2
	str	w4, [x29,140]
	uxth	w27, w27
	bls	.L1472
	add	x4, x19, :lo12:.LANCHOR0
	mov	w21, w2
	ldr	x4, [x4,1120]
	strh	w23, [x4,584]
.L1472:
	cmp	w28, w2
	bcs	.L1471
	str	w23, [x29,128]
	mov	w28, w2
.L1471:
	cmp	w3, 9
	bls	.L1516
	cmp	w2, 9
	bhi	.L1465
.L1516:
	ldr	w2, [x9,#:lo12:.LANCHOR2]
	tbz	x2, 8, .L1465
	ldrh	w2, [x0,x1]
	ldr	w3, [x0,x1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w5, [x6,2]
	mov	w1, w23
	ldrb	w6, [x6,3]
	and	w2, w2, 2047
	ldr	x10, [x0,1112]
	ubfx	x4, x5, 3, 2
	mov	x0, x25
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x9, [x29,112]
	ldrh	w7, [x10,x7,lsl 1]
	str	x8, [x29,120]
	bl	printk
	ldr	x9, [x29,112]
	ldr	x8, [x29,120]
.L1465:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1464
.L1575:
	ldr	x0, [x4,1120]
	mov	w1, 255
	mov	w2, 128
	str	x4, [x29,120]
	add	x0, x0, 264
	adrp	x23, .LANCHOR2
	strh	wzr, [x0,-142]
	bl	ftl_memset
	ldr	x4, [x29,120]
	ldr	x0, [x4,1120]
	ldr	x10, [x4,1096]
	ldrh	w7, [x0,586]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	lsl	x9, x7, 2
	mov	x1, x7
	add	x8, x10, x9
	tbz	x0, 10, .L1475
	ldr	x11, [x4,1112]
	adrp	x0, .LC130
	ldrh	w2, [x10,x9]
	add	x0, x0, :lo12:.LC130
	ldr	w3, [x10,x9]
	ldrb	w5, [x8,2]
	and	w2, w2, 2047
	ldrb	w6, [x8,3]
	ubfx	x3, x3, 11, 8
	ldrh	w7, [x11,x7,lsl 1]
	ubfx	x4, x5, 3, 2
	ubfx	x5, x5, 5, 3
	str	x9, [x29,104]
	str	x10, [x29,112]
	str	x8, [x29,120]
	bl	printk
	ldr	x9, [x29,104]
	ldr	x10, [x29,112]
	ldr	x8, [x29,120]
.L1475:
	ldrb	w0, [x8,2]
	adrp	x25, .LANCHOR5
	and	w0, w0, 224
	cmp	w0, 32
	bne	.L1476
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,688]
	cmp	w0, 2
	bls	.L1476
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, 1
	str	w1, [x0,168]
.L1476:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1477
	add	x3, x19, :lo12:.LANCHOR0
	add	x1, x25, :lo12:.LANCHOR5
	ldr	x0, [x3,3392]
	ldrh	w1, [x1,172]
	ldrh	w2, [x0,96]
	ldrh	w0, [x10,x9]
	and	w0, w0, 2047
	add	w0, w0, w1, lsr 2
	cmp	w2, w0
	ble	.L1477
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1478
	ldr	x0, [x3,1120]
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldrh	w0, [x0,586]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,5534]
	b	.L1477
.L1478:
	tst	w0, 224
	bne	.L1477
	ldr	x1, [x3,1120]
	mov	w0, 65535
	ldrh	w2, [x1,590]
	cmp	w2, w0
	bne	.L1477
	ldrh	w0, [x1,586]
	ldrh	w1, [x1,588]
	str	x3, [x29,120]
	cmp	w1, w0
	beq	.L1477
	bl	zftl_remove_free_node
	ldr	x3, [x29,120]
	ldr	x0, [x3,1120]
	ldrh	w1, [x0,586]
	strh	w1, [x0,590]
	mov	w1, -1
	strh	w1, [x0,586]
.L1477:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x0,1120]
	ldr	x10, [x0,1096]
	ldrh	w7, [x1,584]
	lsl	x9, x7, 2
	mov	x1, x7
	add	x8, x10, x9
	tbz	x2, 10, .L1479
	ldr	x11, [x0,1112]
	adrp	x0, .LC131
	ldrh	w2, [x10,x9]
	add	x0, x0, :lo12:.LC131
	ldr	w3, [x10,x9]
	ldrb	w5, [x8,2]
	and	w2, w2, 2047
	ldrb	w6, [x8,3]
	ubfx	x3, x3, 11, 8
	ldrh	w7, [x11,x7,lsl 1]
	ubfx	x4, x5, 3, 2
	ubfx	x5, x5, 5, 3
	str	x9, [x29,104]
	str	x10, [x29,112]
	str	x8, [x29,120]
	bl	printk
	ldr	x9, [x29,104]
	ldr	x10, [x29,112]
	ldr	x8, [x29,120]
.L1479:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1480
	add	x3, x19, :lo12:.LANCHOR0
	add	x1, x25, :lo12:.LANCHOR5
	ldr	x0, [x3,3392]
	ldrh	w1, [x1,174]
	ldrh	w2, [x0,98]
	ldr	w0, [x10,x9]
	ubfx	x0, x0, 11, 8
	add	w0, w0, w1, lsr 2
	cmp	w2, w0
	ble	.L1480
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1481
	ldr	x0, [x3,1120]
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldrh	w0, [x0,584]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,5534]
	b	.L1480
.L1481:
	and	w0, w0, 248
	cmp	w0, 16
	bne	.L1480
	ldr	x1, [x3,1120]
	mov	w0, 65535
	ldrh	w2, [x1,588]
	cmp	w2, w0
	bne	.L1480
	ldrh	w0, [x1,584]
	ldrh	w1, [x1,590]
	str	x3, [x29,120]
	cmp	w1, w0
	beq	.L1480
	bl	zftl_remove_free_node
	ldr	x3, [x29,120]
	ldr	x0, [x3,1120]
	ldrh	w1, [x0,584]
	strh	w1, [x0,588]
	mov	w1, -1
	strh	w1, [x0,584]
.L1480:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,1096]
	tbz	x0, 10, .L1482
	uxtw	x7, w22
	ldr	x8, [x4,1112]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w7, [x8,x7,lsl 1]
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC132
	ldrb	w5, [x6,2]
	add	x0, x0, :lo12:.LC132
	ldrb	w6, [x6,3]
	mov	w1, w22
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1482:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,1096]
	tbz	x0, 10, .L1483
	ldr	w7, [x29,128]
	ldr	x8, [x4,1112]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC133
	ldrb	w5, [x6,2]
	mov	x1, x7
	ldrb	w6, [x6,3]
	add	x0, x0, :lo12:.LC133
	ldrh	w7, [x8,x7,lsl 1]
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1483:
	ldr	w1, [x29,136]
	add	x0, x19, :lo12:.LANCHOR0
	udiv	w3, w1, w26
	ldr	w1, [x29,140]
	ldr	x0, [x0,3392]
	strh	w24, [x0,96]
	strh	w28, [x0,98]
	strh	w20, [x0,92]
	strh	w21, [x0,94]
	udiv	w4, w1, w27
	strh	w3, [x0,88]
	strh	w4, [x0,90]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1484
	adrp	x0, .LC134
	mov	w1, w26
	add	x0, x0, :lo12:.LC134
	mov	w2, w27
	uxth	w3, w3
	uxth	w4, w4
	bl	printk
.L1484:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1485
	add	x6, x25, :lo12:.LANCHOR5
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	mov	w1, w20
	mov	w2, w21
	mov	w3, w24
	ldrh	w5, [x6,172]
	mov	w4, w28
	ldrh	w6, [x6,174]
	bl	printk
.L1485:
	add	x1, x25, :lo12:.LANCHOR5
	sub	w0, w28, w21
	str	w0, [x29,140]
	ldrh	w0, [x1,174]
	ldr	w2, [x29,140]
	cmp	w2, w0
	bgt	.L1486
	ldrh	w1, [x1,172]
	sub	w0, w24, w20
	cmp	w0, w1
	ble	.L1515
.L1486:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w26, 0
	mov	w22, w26
	ldr	x1, [x0,1120]
	ldr	x0, [x0,3392]
	ldrh	w28, [x1,580]
	ldrh	w27, [x0,134]
	sub	w0, w24, w20
	str	w0, [x29,136]
	adrp	x0, .LC137
	add	x0, x0, :lo12:.LC137
	adrp	x24, .LC136
	str	x0, [x29,128]
	add	x0, x24, :lo12:.LC136
	str	x0, [x29,120]
.L1488:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1088]
	cmp	w27, w0
	bcs	.L1497
	add	w7, w28, 1
	ldr	x4, [x1,1096]
	uxth	w28, w7
	cmp	w28, w0
	csel	w28, w28, wzr, cc
	uxtw	x24, w28
	lsl	x3, x24, 2
	add	x0, x4, x3
	ldrb	w0, [x0,2]
	and	w2, w0, 224
	cmp	w2, 224
	beq	.L1490
	tst	w0, 192
	beq	.L1490
	ubfx	x0, x0, 3, 2
	cmp	w2, 160
	and	w1, w0, 1
	tbnz	x0, 0, .L1573
	cmp	w0, 2
.L1573:
	add	x0, x25, :lo12:.LANCHOR5
	bne	.L1493
	ldrh	w0, [x0,174]
	ldr	w2, [x29,140]
	cmp	w2, w0
	ble	.L1494
	ldr	w0, [x4,x3]
	ubfx	x0, x0, 11, 8
	cmp	w0, w21
	bls	.L1495
	cbz	w1, .L1494
	ldrh	w0, [x4,x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1494
.L1495:
	mov	w1, 0
	mov	w0, w28
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w22, w22, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 3424
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,5534]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1494
	ldr	x1, [x0,1096]
	ldr	x10, [x0,1112]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,120]
	ldrb	w2, [x1,2]
	b	.L1574
.L1493:
	ldrh	w0, [x0,172]
	ldr	w2, [x29,136]
	cmp	w2, w0
	ble	.L1494
	ldrh	w0, [x4,x3]
	add	w2, w20, 8
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1496
	cbz	w1, .L1494
	ldr	w0, [x4,x3]
	add	w1, w21, 4
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1494
.L1496:
	mov	w1, 0
	mov	w0, w28
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w26, w26, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 3424
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,5534]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1494
	ldr	x1, [x0,1096]
	ldr	x10, [x0,1112]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,128]
	ldrb	w2, [x1,2]
.L1574:
	ldrh	w3, [x10,x24,lsl 1]
	mov	w1, w28
	ldrh	w4, [x9,56]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1494:
	cmp	w26, 4
	bhi	.L1497
	cmp	w22, 4
	bhi	.L1497
.L1490:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1488
.L1497:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	str	w28, [x0,580]
	b	.L1487
.L1515:
	mov	w26, 0
	mov	w22, w26
.L1487:
	cbz	w21, .L1500
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,134]
.L1501:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,1088]
	cmp	w2, w0
	bls	.L1576
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1096]
	add	x5, x4, x3
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w21
	bcc	.L1502
	ldrb	w5, [x5,2]
	tst	w5, 24
	beq	.L1502
	sub	w2, w2, w21
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1502:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1501
.L1576:
	ldr	x0, [x1,3392]
	ldrh	w1, [x0,72]
	add	w1, w21, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w21
	bls	.L1500
	sub	w21, w1, w21
	strh	w21, [x0,98]
.L1500:
	cbz	w20, .L1506
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,134]
.L1507:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,1088]
	cmp	w2, w0
	bls	.L1577
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1096]
	add	x5, x4, x3
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L1508
	ldrb	w5, [x5,2]
	and	w5, w5, 24
	cmp	w5, 16
	beq	.L1508
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1508:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1507
.L1577:
	ldr	x0, [x1,3392]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L1506
	sub	w20, w1, w20
	strh	w20, [x0,96]
.L1506:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1463
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR3+1294
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x19,1112]
	ldrh	w2, [x2,#:lo12:.LANCHOR3+1294]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 1
	bhi	.L1463
	mov	w1, 0
	mov	w2, 1
	add	w22, w22, 1
	bl	gc_add_sblk
	b	.L1463
.L1514:
	mov	w26, 0
	mov	w22, w26
.L1463:
	add	w0, w26, w22
.L1553:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	gc_static_wearleveling, .-gc_static_wearleveling
	.align	2
	.global	zftl_sblk_list_init
	.type	zftl_sblk_list_init, %function
zftl_sblk_list_init:
	stp	x29, x30, [sp, -112]!
	mov	w2, 6
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR3
	adrp	x26, .LANCHOR4
	ldrh	w3, [x19,1088]
	adrp	x27, .LC0
	ldr	x0, [x19,1040]
	add	x26, x26, :lo12:.LANCHOR4
	mov	w24, 0
	add	x27, x27, :lo12:.LC0
	mul	w2, w3, w2
	add	x26, x26, 96
	bl	ftl_memset
	str	xzr, [x19,1128]
	add	x0, x23, :lo12:.LANCHOR3
	mov	w1, 32
	strh	wzr, [x19,3376]
	strh	wzr, [x19,3378]
	strh	w1, [x0,1328]
	ldrh	w25, [x0,1266]
	ldrb	w1, [x0,1249]
	str	xzr, [x0,1304]
	str	xzr, [x0,1312]
	mul	w25, w1, w25
	str	xzr, [x0,1320]
	str	xzr, [x0,1280]
	str	xzr, [x0,1272]
	mov	w0, 32768
	strh	wzr, [x19,3380]
	sdiv	w25, w0, w25
	strh	wzr, [x19,3382]
	strh	wzr, [x19,3386]
	strh	wzr, [x19,3384]
	sxth	w0, w25
	str	w0, [x29,108]
	ldr	x0, [x19,3392]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1579:
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w0, [x2,1088]
	cmp	w19, w0
	bge	.L1610
	sxtw	x25, w19
	ldr	x20, [x2,1096]
	ldr	w0, [x29,108]
	add	x20, x20, x25, lsl 2
	ldrb	w1, [x20,3]
	cbz	w1, .L1580
	add	x0, x23, :lo12:.LANCHOR3
	mov	w1, 0
	ldr	x4, [x2,3392]
	ldrb	w5, [x0,1249]
	ldrh	w6, [x0,1266]
	mov	w0, w1
.L1581:
	cmp	w1, w5
	bge	.L1611
	ldrb	w2, [x20,3]
	asr	w2, w2, w1
	tbnz	x2, 0, .L1582
	add	w0, w6, w0
	sxth	w0, w0
	b	.L1583
.L1582:
	ldrh	w2, [x4,146]
	add	w2, w2, 1
	strh	w2, [x4,146]
.L1583:
	add	w1, w1, 1
	b	.L1581
.L1611:
	cbz	w0, .L1585
	mov	w1, 32768
	sdiv	w0, w1, w0
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1580
.L1585:
	ldrb	w1, [x20,2]
	mov	w2, -1
	orr	w1, w1, -32
	strb	w1, [x20,2]
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x1, [x1,1112]
	strh	w2, [x1,x25,lsl 1]
.L1580:
	add	x21, x22, :lo12:.LANCHOR0
	mov	w1, 6
	smull	x1, w19, w1
	ldr	x2, [x21,1040]
	add	x2, x2, x1
	strh	w0, [x2,4]
	mov	w2, -1
	ldr	x0, [x21,1040]
	add	x4, x0, x1
	strh	w2, [x4,2]
	strh	w2, [x0,x1]
	ldrb	w0, [x20,2]
	and	w0, w0, 224
	cmp	w0, 224
	beq	.L1586
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1586
	ldr	x1, [x21,1120]
	ldrh	w2, [x1,16]
	cmp	w19, w2
	beq	.L1586
	ldrh	w2, [x1,48]
	cmp	w19, w2
	beq	.L1586
	ldrh	w1, [x1,80]
	cmp	w19, w1
	beq	.L1586
	cmp	w0, 64
	bne	.L1587
	uxth	w20, w19
	add	x0, x21, 1128
	mov	w1, w20
	add	x2, x21, 3382
	b	.L1608
.L1587:
	cmp	w0, 96
	bne	.L1588
	uxth	w20, w19
	add	x0, x23, :lo12:.LANCHOR3
	add	x0, x0, 1280
	mov	w1, w20
	add	x2, x21, 3384
	b	.L1608
.L1588:
	cmp	w0, 160
	bne	.L1589
	uxth	w20, w19
	add	x0, x23, :lo12:.LANCHOR3
	add	x0, x0, 1272
	mov	w1, w20
	add	x2, x21, 3386
.L1608:
	bl	_insert_data_list
	ldr	x0, [x21,1112]
	ldrh	w0, [x0,x25,lsl 1]
	cmp	w0, 7
	bhi	.L1586
	mov	w0, w20
	mov	w1, 1
	mov	w2, w28
	b	.L1606
.L1589:
	cbnz	w0, .L1586
	ldr	x0, [x21,1112]
	ldrh	w2, [x0,x25,lsl 1]
	cbz	w2, .L1590
	cmp	w24, 2
	bgt	.L1591
	adrp	x0, .LC138
	mov	w1, w19
	add	x0, x0, :lo12:.LC138
	add	w24, w24, 1
	bl	printk
	sxth	w24, w24
	ldrb	w0, [x20,2]
	tbz	x0, 4, .L1592
	mov	w1, 5
	b	.L1602
.L1592:
	mov	w1, 2
.L1602:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x20,2]
	mov	w1, 1
	mov	w0, w19
.L1606:
	bl	gc_add_sblk
	b	.L1586
.L1591:
	mov	x1, x26
	mov	w2, 651
	mov	x0, x27
	bl	printk
	bl	dump_stack
.L1590:
	ldrb	w0, [x20,2]
	ands	w0, w0, 24
	bne	.L1594
	add	x0, x23, :lo12:.LANCHOR3
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x0, 1304
	mov	w1, w19
	add	x2, x2, 3376
	b	.L1604
.L1594:
	cmp	w0, 16
	add	x2, x22, :lo12:.LANCHOR0
	add	x0, x23, :lo12:.LANCHOR3
	bne	.L1595
	add	x0, x0, 1312
	mov	w1, w19
	add	x2, x2, 3378
	b	.L1604
.L1595:
	add	x0, x0, 1320
	mov	w1, w19
	add	x2, x2, 3380
.L1604:
	bl	_insert_free_list
.L1586:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1579
.L1610:
	ldr	x0, [x2,3392]
	ldrh	w1, [x2,3376]
	strh	w1, [x0,114]
	ldrh	w1, [x2,3378]
	strh	w1, [x0,118]
	ldrh	w1, [x2,3380]
	strh	w1, [x0,116]
	ldrh	w1, [x2,3382]
	strh	w1, [x0,122]
	ldrh	w1, [x2,3386]
	strh	w1, [x0,120]
	ldrh	w1, [x2,3384]
	strh	w1, [x0,124]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LC126
	adrp	x22, .LANCHOR3
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	mov	w23, 65535
	add	x24, x0, 16
	mov	x25, x22
	add	x20, x20, :lo12:.LC126
.L1614:
	mov	w0, w21
	bl	ftl_alloc_sblk
	uxth	w26, w0
	cmp	w26, w23
	beq	.L1614
	mov	w1, 0
	mov	w0, w26
	bl	ftl_erase_sblk
	mov	x1, x24
	mov	w0, w26
	bl	ftl_get_blk_list_in_sblk
	strh	w26, [x19]
	add	x2, x22, :lo12:.LANCHOR3
	uxtb	w0, w0
	strb	w0, [x19,9]
	cmp	w21, 2
	strh	wzr, [x19,2]
	mov	w3, 0
	ldrh	w1, [x2,1294]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w0, w1, w0
	strh	w0, [x19,6]
	strb	w21, [x19,4]
	beq	.L1615
	ldrb	w3, [x2,1249]
	mul	w3, w1, w3
	uxth	w3, w3
.L1615:
	add	x2, x25, :lo12:.LANCHOR3
	adrp	x27, .LANCHOR0
	strh	w3, [x19,12]
	add	x27, x27, :lo12:.LANCHOR0
	ubfiz	x28, x26, 1, 16
	ldr	x0, [x2,1848]
	add	x0, x0, x3, uxth 2
	ldrb	w2, [x2,1249]
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,1112]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L1612
.L1613:
	mov	w1, w26
	mov	x0, x20
	bl	printk
	ldr	x0, [x27,1112]
	mov	w1, -1
	strh	w1, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L1614
.L1612:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	pm_free_sblk
	.type	pm_free_sblk, %function
pm_free_sblk:
	stp	x29, x30, [sp, -384]!
	adrp	x0, .LANCHOR5
	add	x29, sp, 0
	add	x1, x0, :lo12:.LANCHOR5
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	ldrh	w1, [x1,176]
	cmp	w1, 128
	bls	.L1620
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 94
	add	x1, x1, 120
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1620:
	adrp	x20, .LANCHOR0
	mov	x1, 0
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, w1
	mov	w6, 65535
	ldr	x3, [x0,3392]
	add	x0, x19, :lo12:.LANCHOR5
	ldrh	w4, [x3,688]
	ldrh	w5, [x0,176]
.L1623:
	add	x7, x3, x1, lsl 1
	uxth	w0, w1
	ldrh	w7, [x7,416]
	cmp	w7, w6
	beq	.L1621
	add	w2, w2, 1
	uxth	w2, w2
.L1621:
	cmp	w2, w4
	bcs	.L1622
	cmp	w2, w5
	bcs	.L1622
	add	x1, x1, 1
	cmp	x1, 128
	bne	.L1623
	mov	w0, w1
.L1622:
	add	w0, w0, 1
	mov	w19, 128
	add	x22, x29, 128
	mov	w1, 0
	uxth	w0, w0
	mov	w2, 256
	cmp	w0, 128
	adrp	x23, .LANCHOR3
	csel	w19, w0, w19, ls
	uxth	w0, w19
	str	w0, [x29,124]
	mov	x0, x22
	bl	ftl_memset
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x23, :lo12:.LANCHOR3
	mov	x3, 0
	ldrb	w2, [x0,1257]
	ldr	x4, [x0,3392]
	mov	w0, 24
	ldrh	w6, [x1,1232]
	sub	w2, w0, w2
	mov	w0, 1
	ldrb	w8, [x1,1234]
	sub	w2, w2, w6
	ldrh	w7, [x4,698]
	lsl	w2, w0, w2
	sub	w2, w2, #1
.L1624:
	cmp	w7, w3, uxth
	bls	.L1652
	add	x0, x4, x3, lsl 2
	mov	x1, 0
	ldr	w0, [x0,704]
	lsr	w0, w0, w6
	and	w0, w0, w2
	udiv	w0, w0, w8
	uxth	w0, w0
.L1625:
	ldr	w5, [x29,124]
	cmp	w5, w1, uxth
	bls	.L1653
	add	x5, x4, x1, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w0
	bne	.L1626
	ldrh	w5, [x22,x1,lsl 1]
	add	w5, w5, 1
	strh	w5, [x22,x1,lsl 1]
.L1626:
	add	x1, x1, 1
	b	.L1625
.L1653:
	add	x3, x3, 1
	b	.L1624
.L1652:
	add	x1, x23, :lo12:.LANCHOR3
	mov	x19, 0
	mov	w28, 65535
	adrp	x25, .LC139
	adrp	x26, .LC140
	mov	w24, w19
	ldrb	w0, [x1,1249]
	mov	w27, w28
	ldrh	w21, [x1,1294]
	add	x25, x25, :lo12:.LC139
	add	x26, x26, :lo12:.LC140
	mul	w21, w0, w21
	uxth	w21, w21
.L1629:
	ldr	w0, [x29,124]
	uxth	w6, w19
	cmp	w6, w0
	bcs	.L1654
	add	x0, x20, :lo12:.LANCHOR0
	add	x2, x23, :lo12:.LANCHOR3
	mov	w8, w19
	ldr	x0, [x0,3392]
	add	x1, x0, x19, sxtw 1
	ldrb	w4, [x2,1234]
	ldrh	w3, [x1,416]
	ldrh	w1, [x0,692]
	sdiv	w1, w1, w4
	cmp	w1, w3
	bne	.L1630
	ldrb	w1, [x2,1249]
	ldrh	w2, [x2,1294]
	mul	w1, w1, w2
	strh	w1, [x22,x19,lsl 1]
.L1630:
	ldrh	w2, [x22,x19,lsl 1]
	cmp	w21, w2
	bls	.L1631
	cmp	w2, wzr
	csel	w24, w24, w6, eq
	csel	w21, w21, w2, eq
.L1631:
	cmp	w3, w27
	beq	.L1633
	ldrh	w1, [x0,74]
	cmp	w1, 2
	bls	.L1633
	add	x4, x20, :lo12:.LANCHOR0
	ubfiz	x1, x3, 2, 16
	ldrh	w0, [x0,92]
	ldr	x4, [x4,1096]
	add	w0, w0, 4
	ldrh	w4, [x4,x1]
	and	w4, w4, 2047
	cmp	w4, w0
	bgt	.L1633
	mov	w1, w8
	mov	x0, x25
	str	x6, [x29,104]
	str	x8, [x29,112]
	bl	printk
	ldr	x6, [x29,104]
	ldr	x8, [x29,112]
	mov	w28, w6
.L1633:
	ldrh	w2, [x22,x19,lsl 1]
	cbnz	w2, .L1634
	add	x0, x20, :lo12:.LANCHOR0
	sxtw	x6, w8
	ldr	x4, [x0,3392]
	add	x0, x6, 208
	ldrh	w3, [x4,x0,lsl 1]
	cmp	w3, w27
	beq	.L1634
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1635
	ldrh	w4, [x4,688]
	mov	x0, x26
	mov	w1, w8
	str	x6, [x29,112]
	bl	printk
	ldr	x6, [x29,112]
.L1635:
	add	x1, x20, :lo12:.LANCHOR0
	add	x6, x6, 208
	str	x1, [x29,104]
	str	x6, [x29,112]
	ldr	x0, [x1,3392]
	ldrh	w0, [x0,x6,lsl 1]
	bl	ftl_free_sblk
	ldr	x1, [x29,104]
	ldr	x6, [x29,112]
	ldr	x0, [x1,3392]
	mov	w1, -1
	strh	w1, [x0,x6,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L1634:
	add	x19, x19, 1
	b	.L1629
.L1654:
	mov	w0, 65535
	cmp	w28, w0
	csel	w0, w24, w28, eq
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 384
	ret
	.size	pm_free_sblk, .-pm_free_sblk
	.align	2
	.global	ftl_memcpy
	.type	ftl_memcpy, %function
ftl_memcpy:
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	add	x29, sp, 0
	bl	memcpy
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_memcpy, .-ftl_memcpy
	.align	2
	.global	flash_info_data_init
	.type	flash_info_data_init, %function
flash_info_data_init:
	adrp	x1, .LANCHOR2
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x1,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	mov	x20, x1
	tbz	x0, 12, .L1657
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 119
	add	x2, x2, 136
	bl	printk
.L1657:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	ldr	x0, [x19,1240]
	bl	ftl_memset
	ldr	x1, [x19,1240]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,1240]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	add	x1, x19, 1152
	bl	ftl_memcpy
	ldr	x0, [x19,1240]
	add	x1, x20, :lo12:.LANCHOR2
	add	x1, x1, 8
	mov	w2, 32
	add	x0, x0, 48
	bl	ftl_memcpy
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_info_data_init, .-flash_info_data_init
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L1662:
	cmp	w2, w3
	bls	.L1664
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1662
.L1664:
	ret
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	2
	.global	ftl_memcmp
	.type	ftl_memcmp, %function
ftl_memcmp:
	stp	x29, x30, [sp, -16]!
	uxtw	x2, w2
	add	x29, sp, 0
	bl	memcmp
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_memcmp, .-ftl_memcmp
	.align	2
	.global	timer_get_time
	.type	timer_get_time, %function
timer_get_time:
	adrp	x0, jiffies
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0,#:lo12:jiffies]
	adrp	x0, .LANCHOR5+184
	ldr	x0, [x0,#:lo12:.LANCHOR5+184]
	sub	x0, x1, x0
	bl	jiffies_to_msecs
	ldp	x29, x30, [sp], 16
	ret
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	stp	x29, x30, [sp, -48]!
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	mov	x19, x1
	mov	x0, x1
	mov	w1, 0
	bl	ftl_memset
	bl	rknand_device_lock
	adrp	x0, .LANCHOR5+192
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x3, [x0,24]
	mov	w0, w20
	blr	x3
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x0, [x29,16]
	str	x1, [x29,24]
	bl	rknand_device_lock
	adrp	x1, .LANCHOR5+192
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR5+192]
	ldr	x3, [x1,32]
	mov	w1, 1
	blr	x3
	str	x0, [x29,24]
	bl	rknand_device_unlock
	ldr	x0, [x29,24]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x0, [x29,24]
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	rknand_device_lock
	adrp	x3, .LANCHOR5+192
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR5+192]
	ldr	x3, [x3,8]
	blr	x3
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x0, [x29,24]
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	rknand_device_lock
	adrp	x3, .LANCHOR5+192
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR5+192]
	ldr	x3, [x3,16]
	blr	x3
	str	x0, [x29,40]
	bl	rknand_device_unlock
	ldr	x0, [x29,40]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	flash_sram_load_store
	.type	flash_sram_load_store, %function
flash_sram_load_store:
	adrp	x4, .LANCHOR5+200
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR5+200]
	add	x4, x4, 4096
	cbnz	w2, .L1672
	add	x1, x4, x1
	b	.L1674
.L1672:
	add	x0, x4, x1
	mov	x1, x6
.L1674:
	mov	w2, w3
	bl	ftl_memcpy
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_sram_load_store, .-flash_sram_load_store
	.align	2
	.global	FlashCs123Init
	.type	FlashCs123Init, %function
FlashCs123Init:
	ret
	.size	FlashCs123Init, .-FlashCs123Init
	.align	2
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LC142
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC142
	bl	printk
	adrp	x0, .LANCHOR5+192
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x1, [x0,64]
	mov	w0, 0
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	adrp	x0, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x1, [x0,72]
	mov	w0, 0
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	adrp	x1, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+192]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+192]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR5+192
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR5+192]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR5+192]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR5+192]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR5+192
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+192]
	ldr	x1, [x1,112]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	stp	x29, x30, [sp, -16]!
	mul	w6, w2, w3
	mov	x7, x0
	mov	w4, w2
	adrp	x0, .LC143
	mov	w2, 0
	add	x29, sp, 0
	mov	x5, x1
	add	x0, x0, :lo12:.LC143
	mov	x1, x7
	mov	w3, 16
	mov	w7, w2
	bl	print_hex_dump
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	hynix_get_read_retry_default
	.type	hynix_get_read_retry_default, %function
hynix_get_read_retry_default:
	stp	x29, x30, [sp, -144]!
	adrp	x3, .LANCHOR0
	mov	w2, -83
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	add	x0, x3, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	w1, -82
	cmp	w19, 2
	ldr	x25, [x0,1240]
	mov	w0, -84
	str	x3, [x29,136]
	add	x26, x25, 128
	add	x22, x25, 112
	strb	w0, [x25,128]
	mov	w0, -81
	strb	w19, [x25,112]
	strb	w2, [x25,129]
	strb	w1, [x25,130]
	strb	w0, [x25,131]
	bne	.L1689
	mov	w0, -89
	strb	w0, [x22,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1752
.L1689:
	cmp	w19, 3
	bne	.L1691
	mov	x5, 0
.L1692:
	sub	w0, w5, #80
	strb	w0, [x26,x5]
	add	x5, x5, 1
	cmp	x5, 8
	bne	.L1692
	mov	w27, w5
	mov	w28, w5
	b	.L1690
.L1691:
	cmp	w19, 4
	bne	.L1693
	mov	w5, -52
	strb	w5, [x25,128]
	mov	w5, -65
	strb	w5, [x25,129]
	mov	w5, -86
	mov	w27, 8
	strb	w5, [x25,130]
	mov	w5, -85
	strb	w2, [x25,133]
	mov	w28, w27
	strb	w5, [x25,131]
	mov	w5, -51
	strb	w1, [x25,134]
	strb	w5, [x25,132]
	strb	w0, [x25,135]
	b	.L1690
.L1693:
	cmp	w19, 5
	bne	.L1694
	mov	w0, 56
	strb	w0, [x25,128]
	mov	w0, 57
	strb	w0, [x25,129]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x25,130]
	mov	w0, 59
	strb	w0, [x25,131]
	b	.L1794
.L1694:
	cmp	w19, 6
	bne	.L1695
	mov	w0, 14
	strb	w0, [x25,128]
	mov	w0, 15
	strb	w0, [x25,129]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x25,130]
	mov	w0, 17
	strb	w0, [x25,131]
	b	.L1794
.L1695:
	cmp	w19, 7
	bne	.L1696
	mov	x0, 0
.L1697:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1697
	mov	w0, -44
	mov	w27, 12
	strb	w0, [x25,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x25,137]
	b	.L1690
.L1696:
	cmp	w19, 8
	bne	.L1752
	mov	w0, 6
	strb	w0, [x25,128]
	mov	w0, 7
	strb	w0, [x25,129]
	mov	w0, 9
	strb	w19, [x25,130]
	strb	w0, [x25,131]
	mov	w27, 50
	mov	w0, 10
	mov	w28, 5
	strb	w0, [x25,132]
	b	.L1690
.L1752:
	mov	w27, 7
.L1794:
	mov	w28, 4
.L1690:
	sub	w0, w19, #1
	cmp	w0, 1
	bhi	.L1791
	ldr	x0, [x29,136]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w5, 55
	add	x24, x24, 408
.L1698:
	ldrb	w0, [x19,1145]
	cmp	w0, w23
	bls	.L1705
	add	x0, x19, x23, sxtw
	ldrb	w0, [x0,1248]
	mov	x20, 160
	ldr	x21, [x19,1048]
	ubfiz	x1, x0, 8, 8
	mov	x25, 0
	madd	x20, x0, x20, x22
	add	x21, x21, x1
	add	x20, x20, 32
.L1700:
	str	w5, [x21,2056]
	str	x5, [x29,136]
	ldrb	w0, [x26,x25]
	str	w0, [x21,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x21,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	ldr	x5, [x29,136]
	cmp	w28, w25, uxtb
	bhi	.L1700
	mov	x0, 0
.L1701:
	add	w2, w0, 8
	mov	x1, 0
.L1702:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w4, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w4, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1702
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1701
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1698
.L1791:
	sub	w0, w19, #3
	cmp	w0, 5
	bhi	.L1705
	mul	w0, w28, w27
	sub	w24, w28, #1
	mov	w20, 0
	asr	w23, w0, 1
	lsl	w0, w0, 4
	str	w0, [x29,128]
	lsl	w0, w23, 1
	uxtb	x24, w24
	str	w0, [x29,124]
	sub	w0, w19, #5
	adrp	x26, .LANCHOR5
	str	w0, [x29,132]
	add	x0, x24, 1
	str	x0, [x29,112]
.L1706:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w0, [x1,1145]
	cmp	w0, w20
	bhi	.L1751
.L1705:
	strb	w28, [x22,1]
	strb	w27, [x22,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L1751:
	add	x0, x1, x20, sxtw
	ldrb	w24, [x0,1248]
	str	x1, [x29,104]
	mov	x21, 160
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
	ldr	x1, [x29,104]
	sxtw	x0, w24
	ubfiz	x2, x24, 8, 8
	madd	x21, x0, x21, x22
	ldr	x0, [x1,1048]
	mov	w1, 255
	str	x1, [x29,104]
	add	x21, x21, 32
	add	x24, x0, x2
	str	w1, [x24,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 8
	ldr	x1, [x29,104]
	bne	.L1707
	mov	w0, 120
	str	w0, [x24,2056]
	str	wzr, [x24,2052]
	mov	w0, 23
	str	wzr, [x24,2052]
	mov	w1, 25
	str	wzr, [x24,2052]
	add	x21, x25, 144
	str	w0, [x24,2056]
	mov	w0, 4
	str	w0, [x24,2056]
	str	w1, [x24,2056]
	mov	w1, 218
	str	w1, [x24,2056]
	mov	w1, 21
	str	wzr, [x24,2056]
	str	wzr, [x24,2052]
	str	wzr, [x24,2052]
	str	w1, [x24,2052]
	b	.L1797
.L1707:
	mov	w0, 54
	cmp	w19, 4
	str	w0, [x24,2056]
	bne	.L1709
	mov	w0, 64
	str	w1, [x24,2052]
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1795
.L1709:
	ldr	w0, [x29,132]
	cmp	w0, 1
	bhi	.L1711
	ldrb	w0, [x25,128]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1796
.L1711:
	cmp	w19, 7
	bne	.L1710
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1795:
	str	w0, [x24,2052]
	mov	w0, 77
.L1796:
	str	w0, [x24,2048]
.L1710:
	mov	w0, 22
	str	w0, [x24,2056]
	mov	w0, 23
	str	w0, [x24,2056]
	mov	w0, 4
	str	w0, [x24,2056]
	mov	w0, 25
	str	w0, [x24,2056]
	str	wzr, [x24,2056]
	cmp	w19, 6
	str	wzr, [x24,2052]
	str	wzr, [x24,2052]
	bne	.L1712
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L1713
.L1712:
	str	wzr, [x24,2052]
.L1713:
	mov	w0, 2
.L1797:
	str	w0, [x24,2052]
	mov	w0, 48
	str	wzr, [x24,2052]
	str	w0, [x24,2056]
	bl	nandc_wait_flash_ready
	ldr	w0, [x29,132]
	cmp	w0, 1
	cset	w8, ls
	cmp	w19, 8
	cset	w7, eq
	cbnz	w8, .L1754
	cbnz	w7, .L1754
	cmp	w19, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L1714
.L1754:
	mov	w1, 16
.L1714:
	adrp	x0, .LANCHOR5
	mov	x9, 0
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,208]
.L1715:
	ldr	w10, [x24,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L1715
	cbz	w7, .L1716
	mov	w1, 0
.L1718:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L1717
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L1717
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1718
	b	.L1719
.L1717:
	cmp	w1, 7
	bne	.L1720
.L1719:
	adrp	x0, .LC144
	mov	w1, 0
	add	x0, x0, :lo12:.LC144
	bl	printk
.L1721:
	b	.L1721
.L1716:
	cmp	w19, 7
	bne	.L1722
	mov	w1, w7
.L1724:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L1723
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L1723
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1724
	b	.L1725
.L1723:
	cmp	w1, 7
	bne	.L1720
.L1725:
	adrp	x0, .LC144
	mov	w1, 0
	add	x0, x0, :lo12:.LC144
	bl	printk
.L1726:
	b	.L1726
.L1722:
	cmp	w19, 6
	bne	.L1720
	mov	x1, 0
.L1727:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L1720
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L1720
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1727
	adrp	x0, .LC144
	mov	w1, 0
	add	x0, x0, :lo12:.LC144
	bl	printk
.L1729:
	b	.L1729
.L1720:
	add	x0, x26, :lo12:.LANCHOR5
	ldr	x10, [x0,208]
	mov	x0, 0
.L1730:
	ldr	w1, [x29,128]
	cmp	w1, w0
	ble	.L1799
	ldr	w1, [x24,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L1730
.L1799:
	add	x0, x26, :lo12:.LANCHOR5
	mov	w11, w23
	mov	w9, 8
	ldr	x12, [x0,208]
.L1733:
	mov	w0, 0
.L1732:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L1732
	ldr	w0, [x29,124]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L1733
	mov	x9, 0
	mov	w17, 1
.L1734:
	mov	w1, 0
	mov	w11, w1
.L1738:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L1736:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L1736
	cmp	w14, 8
	bls	.L1737
	orr	w1, w1, w13
	uxth	w1, w1
.L1737:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L1738
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L1734
	add	x1, x26, :lo12:.LANCHOR5
	mov	x9, 0
	mov	w11, w9
	ldr	x1, [x1,208]
.L1741:
	ldr	w12, [x1,x9]
	add	x9, x9, 4
	cmp	w12, wzr
	csinc	w11, w11, w11, ne
	cmp	x9, 32
	bne	.L1741
	cmp	w11, 7
	ble	.L1742
	adrp	x0, .LC145
	mov	w2, 1
	add	x0, x0, :lo12:.LC145
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC144
	mov	w1, 0
	add	x0, x0, :lo12:.LC144
	bl	printk
.L1743:
	b	.L1743
.L1742:
	cmp	w19, 6
	mov	w9, 4
	beq	.L1744
	cmp	w19, 7
	mov	w9, 10
	beq	.L1744
	cmp	w7, wzr
	mov	w9, 5
	mov	w1, 8
	csel	w9, w1, w9, eq
.L1744:
	mov	w11, 0
.L1745:
	mov	x1, 0
.L1746:
	add	w12, w0, w1
	ldrb	w13, [x10,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x21,w12,sxtw]
	bhi	.L1746
	add	w11, w11, 1
	ldr	x1, [x29,112]
	cmp	w11, w27
	add	w0, w0, w9
	add	x10, x10, x1
	blt	.L1745
	mov	w21, 255
	str	w21, [x24,2056]
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	nandc_wait_flash_ready
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L1748
	mov	w0, 54
	str	w0, [x24,2056]
	ldrb	w0, [x25,128]
	str	w0, [x24,2052]
	mov	w0, 22
	str	wzr, [x24,2048]
	str	w0, [x24,2056]
	mov	w0, 48
	str	wzr, [x24,2056]
	str	wzr, [x24,2052]
	str	wzr, [x24,2052]
	str	w21, [x24,2052]
	str	w21, [x24,2052]
	str	w21, [x24,2052]
	b	.L1798
.L1748:
	mov	w0, 190
	cbnz	w7, .L1798
	mov	w0, 56
.L1798:
	str	w0, [x24,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1706
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	adrp	x0, .LANCHOR2+27
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR2+27]
	sub	w1, w0, #1
	uxtb	w1, w1
	cmp	w1, 7
	bhi	.L1800
	bl	hynix_get_read_retry_default
.L1800:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_get_read_retry_tbl, .-flash_get_read_retry_tbl
	.align	2
	.global	nandc_xfer_done
	.type	nandc_xfer_done, %function
nandc_xfer_done:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR5
	add	x0, x21, :lo12:.LANCHOR5
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x19, .LANCHOR0
	strb	wzr, [x0,216]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1028]
	cmp	w1, 9
	bne	.L1803
	ldr	x22, [x0,1048]
	ldr	w0, [x22,16]
	str	w0, [x29,64]
	ldr	w20, [x22,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1824
	adrp	x21, .LC148
	adrp	x23, .LC149
	add	x21, x21, :lo12:.LC148
	add	x23, x23, :lo12:.LC149
	b	.L1805
.L1824:
	mov	w20, 0
.L1804:
	ldr	w1, [x22,64]
	ldr	w0, [x29,64]
	ubfx	x1, x1, 16, 6
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1807
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1806
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1806
	ldr	w1, [x29,72]
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1807
.L1806:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1808
	ldr	w2, [x22,64]
	adrp	x0, .LC147
	ldr	w3, [x29,64]
	mov	w1, w20
	add	x0, x0, :lo12:.LC147
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	add	x21, x21, :lo12:.LANCHOR5
	bl	printk
	ldr	w1, [x29,72]
	mov	w0, 1
	strb	w0, [x21,216]
	tbnz	x1, 13, .L1807
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	b	.L1807
.L1808:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1804
.L1807:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1192
	ldr	w0, [x20,32]
	cbz	w0, .L1812
	ldr	w1, [x29,64]
	mov	w2, 0
	ldr	w0, [x20,24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	ldr	w0, [x20,28]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	b	.L1849
.L1814:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,1048]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1813
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x22,64]
	mov	x0, x21
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x24,1048]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1813:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1805:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1814
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1192
	ldr	w0, [x20,32]
	cbz	w0, .L1812
	ldr	w1, [x29,64]
	mov	w2, 1
	ldr	w0, [x20,24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	ldr	w0, [x20,28]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	b	.L1848
.L1803:
	ldr	x21, [x0,1048]
	ldr	w0, [x21,8]
	str	w0, [x29,64]
	ldr	w20, [x21,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1825
	adrp	x22, .LC148
	adrp	x23, .LC149
	add	x22, x22, :lo12:.LC148
	add	x23, x23, :lo12:.LC149
	b	.L1817
.L1825:
	adrp	x22, .LC147
	adrp	x23, .LC149
	mov	w20, 0
	add	x22, x22, :lo12:.LC147
	add	x23, x23, :lo12:.LC149
.L1816:
	ldr	w2, [x21,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1819
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1818
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1818
	ldr	w1, [x29,72]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	bl	printk
	b	.L1819
.L1818:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1820
	ldr	w2, [x21,28]
	mov	w1, w20
	ldr	w3, [x29,64]
	mov	x0, x22
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	ldr	x1, [x1,1048]
	bl	rknand_print_hex
.L1820:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1816
.L1819:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1192
	ldr	w0, [x20,32]
	cbz	w0, .L1812
	ldr	w1, [x29,64]
	mov	w2, 0
	ldr	w0, [x20,24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	ldr	w0, [x20,28]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
.L1849:
	mov	w2, 0
	b	.L1847
.L1823:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,1048]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1822
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x24,1048]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1822:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1817:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1823
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1192
	ldr	w0, [x20,32]
	cbz	w0, .L1812
	ldr	w1, [x29,64]
	mov	w2, 1
	ldr	w0, [x20,24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	ldr	w0, [x20,28]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
.L1848:
	mov	w2, 1
.L1847:
	bl	rknand_dma_unmap_single
.L1812:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,1224]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 80
	ret
	.size	nandc_xfer_done, .-nandc_xfer_done
	.align	2
	.global	nandc_xfer
	.type	nandc_xfer, %function
nandc_xfer:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	uxtb	w22, w2
	uxtb	w20, w1
	mov	x2, x3
	mov	w0, w20
	mov	w1, w22
	mov	x21, x3
	mov	x3, x4
	str	x23, [sp,48]
	mov	x23, x4
	bl	nandc_xfer_start
	mov	w0, w20
	bl	nandc_xfer_done
	mov	w0, 0
	cbnz	w20, .L1880
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,1028]
	cmp	w2, 9
	bne	.L1852
	ldr	x5, [x1,1048]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1853:
	cmp	w3, w22
	bcs	.L1889
	uxtw	x1, w3
	add	x1, x1, 84
	ldr	w1, [x5,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x1, 10, 1
	ldr	w1, [x29,72]
	ubfx	x1, x1, 26, 1
	and	w1, w1, w2
	and	w4, w4, w1
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1872
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1872
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1855
	ubfx	x1, x1, 3, 7
	b	.L1856
.L1855:
	ubfx	x1, x1, 19, 7
.L1856:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1854
.L1872:
	mov	w0, -1
.L1854:
	add	w3, w3, 1
	b	.L1853
.L1889:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w4, wzr
	ldr	x1, [x22,1048]
	ldr	w20, [x1]
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1859
	adrp	x0, .LC151
	mov	w1, w20
	add	x0, x0, :lo12:.LC151
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,1048]
	str	w20, [x0]
	mov	w0, -1
.L1859:
	tbz	x20, 13, .L1860
	adrp	x1, .LANCHOR5+216
	ldrb	w1, [x1,#:lo12:.LANCHOR5+216]
	cbz	w1, .L1860
	adrp	x0, .LC152
	mov	w1, w20
	add	x0, x0, :lo12:.LC152
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,1048]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1861
.L1852:
	ldrb	w0, [x1,1236]
	mov	w4, 128
	lsr	w5, w22, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
	mov	w3, w20
.L1863:
	cmp	w3, w5
	add	w6, w20, w4
	bcs	.L1890
	ldr	x0, [x1,1192]
	and	x20, x20, 4294967292
	ubfiz	x7, x3, 2, 30
	add	w3, w3, 1
	ldr	w0, [x0,x20]
	mov	w20, w6
	strb	w0, [x23,x7]
	lsr	w7, w0, 8
	strb	w7, [x23,w2,uxtw]
	add	w7, w2, 1
	lsr	w8, w0, 16
	lsr	w0, w0, 24
	strb	w8, [x23,x7]
	add	w7, w2, 2
	add	w2, w2, 4
	strb	w0, [x23,x7]
	b	.L1863
.L1890:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,1048]
	mov	w0, w2
.L1865:
	cmp	w2, w22
	bcs	.L1891
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1875
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1875
	ldr	w3, [x29,72]
	ubfx	x5, x3, 3, 5
	ldr	w3, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x3, x3, 27, 1
	ubfx	x4, x1, 16, 5
	ldr	w1, [x29,72]
	orr	w3, w5, w3, lsl 5
	ubfx	x1, x1, 29, 1
	orr	w1, w4, w1, lsl 5
	cmp	w3, w1
	ldr	w1, [x29,72]
	bls	.L1867
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1888
.L1867:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1888:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1866
.L1875:
	mov	w0, -1
.L1866:
	add	w2, w2, 1
	b	.L1865
.L1891:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,1048]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1860
	adrp	x0, .LC153
	mov	w1, w20
	add	x0, x0, :lo12:.LC153
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,1048]
	str	w20, [x0]
	b	.L1861
.L1860:
	cmn	w0, #1
	beq	.L1880
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1880
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1880
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1880
.L1861:
	mov	w0, -1
.L1880:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	nandc_xfer, .-nandc_xfer
	.align	2
	.global	flash_read_page
	.type	flash_read_page, %function
flash_read_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x27, [sp,80]
	add	x27, x21, :lo12:.LANCHOR0
	uxtb	w22, w0
	stp	x19, x20, [sp,16]
	mov	w0, 24
	ldrb	w19, [x27,1257]
	stp	x23, x24, [sp,48]
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ubfx	x23, x1, 24, 2
	sub	w19, w19, #1
	mov	x24, x3
	stp	x25, x26, [sp,64]
	ldr	x20, [x27,1048]
	mov	x25, x2
	mov	w26, w4
	and	w19, w1, w19
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	nandc_cs
	cbnz	w23, .L1893
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	b	.L1894
.L1893:
	ldr	x0, [x27,1136]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1895
	ldrb	w0, [x27,1268]
	cbnz	w0, .L1895
	ldrb	w0, [x27,1269]
	cbnz	w0, .L1895
	sxtw	x0, w22
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w23, [x0,8]
	b	.L1894
.L1895:
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
.L1894:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,1136]
	ldrb	w0, [x1,7]
	cmp	w0, 1
	bne	.L1896
	ldrb	w0, [x1,12]
	cmp	w0, 2
	bne	.L1896
	sxtw	x0, w22
	mov	w2, 38
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
.L1896:
	ubfiz	x5, x22, 8, 8
	and	w0, w19, 255
	add	x20, x20, x5
	str	wzr, [x20,2056]
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1256]
	cbz	w0, .L1897
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L1897:
	mov	w0, 48
	str	w0, [x20,2056]
	cbz	w23, .L1898
	ldrb	w0, [x1,12]
	cmp	w0, 3
	bne	.L1898
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1268]
	cbnz	w0, .L1898
	ldrb	w0, [x21,1269]
	cbnz	w0, .L1898
	add	w19, w19, w19, lsl 1
	sub	w0, w19, #1
	add	w0, w0, w23
	b	.L1909
.L1898:
	mov	w0, w19
.L1909:
	bl	nandc_set_seed
	bl	nandc_wait_flash_ready
	mov	w0, 5
	str	w0, [x20,2056]
	str	wzr, [x20,2052]
	mov	w0, 224
	str	wzr, [x20,2052]
	mov	w2, w26
	mov	x3, x25
	mov	x4, x24
	str	w0, [x20,2056]
	mov	w1, 0
	mov	w0, w22
	bl	nandc_xfer
	mov	w19, w0
	mov	w0, 0
	bl	nandc_de_cs
	ldr	x27, [sp,80]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_read_page, .-flash_read_page
	.align	2
	.global	micron_read_retrial
	.type	micron_read_retrial, %function
micron_read_retrial:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	uxtb	w19, w0
	add	x20, x20, :lo12:.LANCHOR0
	str	w4, [x29,152]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w22, w1
	mov	x23, x2
	ldrb	w0, [x20,1236]
	mov	x24, x3
	uxtb	x25, w19
	mov	w21, 0
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	str	w0, [x29,156]
	bl	nandc_wait_flash_ready
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x0, [x20,1048]
	str	x0, [x29,144]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	str	x0, [x29,128]
	lsl	x0, x25, 8
	str	x0, [x29,120]
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	str	x0, [x29,112]
.L1911:
	lsl	x0, x25, 8
	mov	w20, 0
	mov	w28, -1
	adrp	x26, .LANCHOR5
	str	x0, [x29,136]
.L1912:
	add	x0, x26, :lo12:.LANCHOR5
	ldrb	w0, [x0,232]
	cmp	w20, w0
	bcs	.L1916
	ldr	x0, [x29,144]
	add	w27, w20, 1
	ldr	x1, [x29,136]
	add	x1, x0, x1
	mov	w0, 239
	str	x1, [x29,104]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	w0, 200
	bl	timer_delay_ns
	ldr	x1, [x29,104]
	mov	w0, w19
	ldr	w4, [x29,152]
	mov	x2, x23
	mov	x3, x24
	str	w27, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	mov	w1, w22
	bl	flash_read_page
	mov	w6, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1913
	ldr	x0, [x29,128]
	mov	w4, w6
	mov	w1, w20
	mov	w2, w22
	mov	w3, w28
	str	x6, [x29,104]
	bl	printk
	ldr	x6, [x29,104]
.L1913:
	cmn	w6, #1
	beq	.L1914
	adrp	x0, .LANCHOR5
	cmn	w28, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w28, w28, w6, ne
	ldr	x23, [x0,208]
	ldr	x24, [x0,224]
	ldr	w0, [x29,156]
	cmp	w6, w0
	bcc	.L1924
.L1914:
	mov	w20, w27
	b	.L1912
.L1924:
	mov	w28, w6
.L1916:
	ldr	x0, [x29,144]
	ldr	x1, [x29,120]
	add	x26, x0, x1
	mov	w0, 239
	str	w0, [x26,2056]
	mov	w0, 137
	str	w0, [x26,2052]
	mov	w0, 200
	bl	timer_delay_ns
	str	wzr, [x26,2048]
	str	wzr, [x26,2048]
	ldr	w0, [x29,156]
	str	wzr, [x26,2048]
	str	wzr, [x26,2048]
	cmp	w28, w0
	bcc	.L1918
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1918:
	cmn	w28, #1
	cset	w26, eq
	cbnz	w26, .L1926
	cmp	w28, 256
	cset	w1, eq
	cbz	w1, .L1919
.L1926:
	ldr	x0, [x29,112]
	mov	w1, w20
	mov	w2, w22
	mov	w3, w20
	mov	w4, w28
	bl	printk
	cbz	w26, .L1921
	cbnz	w21, .L1921
	mov	w0, w19
	mov	w1, 3
	mov	w21, 1
	bl	mt_auto_read_calibration_config
	b	.L1911
.L1921:
	cbz	w21, .L1922
	mov	w0, w19
	mov	w1, 0
	bl	mt_auto_read_calibration_config
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
	b	.L1922
.L1919:
	cbz	w21, .L1922
	mov	w0, w19
	mov	w28, 256
	bl	mt_auto_read_calibration_config
.L1922:
	bl	nandc_wait_flash_ready
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	micron_read_retrial, .-micron_read_retrial
	.align	2
	.global	toshiba_3d_read_retrial
	.type	toshiba_3d_read_retrial, %function
toshiba_3d_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w24, w1
	uxtb	w22, w0
	stp	x27, x28, [sp,80]
	mov	x26, x2
	mov	x27, x3
	str	w4, [x29,124]
	adrp	x20, .LANCHOR0
	bl	nandc_wait_flash_ready
	ubfx	x19, x24, 24, 2
	add	x1, x20, :lo12:.LANCHOR0
	sxtw	x0, w22
	mov	w2, 46
	mov	w3, 56
	add	x0, x0, 8
	mov	w21, 10
	ldr	x25, [x1,1048]
	lsl	x0, x0, 8
	ldrb	w1, [x1,1237]
	add	x23, x25, x0
	cmp	w1, 36
	csel	w2, w3, w2, ne
	str	w2, [x29,112]
	cmp	w1, 36
	mov	w1, 26
	csel	w21, w21, w1, ne
	cbnz	w19, .L1947
	str	x0, [x29,112]
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	mov	w19, -1
	mov	w28, 1
	str	x0, [x29,104]
.L1954:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1948
	mov	w1, w28
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x25, x0
	str	w1, [x0,8]
	b	.L1949
.L1948:
	mov	w1, w28
	bl	toshiba_3d_set_slc_rr_para
.L1949:
	ldr	w4, [x29,124]
	mov	w0, w22
	mov	w1, w24
	mov	x2, x26
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1950
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w28
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1950:
	cmn	w4, #1
	beq	.L1951
	adrp	x0, .LANCHOR5
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w19, w19, w4, ne
	ldr	x26, [x0,208]
	ldr	x27, [x0,224]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1970
.L1951:
	add	w28, w28, 1
	cmp	w28, w21
	bne	.L1954
	b	.L1953
.L1970:
	mov	w21, w28
	mov	w19, w4
.L1953:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1955
	mov	w1, 0
	mov	w2, w1
	b	.L1993
.L1955:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1956
.L1947:
	mov	x28, x0
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,104]
.L1963:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1957
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x25, x28
	mov	w1, 93
	b	.L1992
.L1957:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x25, x28
	mov	w1, 38
.L1992:
	str	w1, [x0,8]
	mov	x2, x26
	mov	w0, w22
	mov	w1, w24
	ldr	w4, [x29,124]
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1959
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1959:
	cmn	w4, #1
	beq	.L1960
	adrp	x0, .LANCHOR5
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w19, w19, w4, ne
	ldr	x26, [x0,208]
	ldr	x27, [x0,224]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1971
.L1960:
	ldr	w0, [x29,112]
	add	w21, w21, 1
	cmp	w21, w0
	bne	.L1963
	b	.L1962
.L1971:
	mov	w19, w4
.L1962:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1964
	mov	w1, 0
	mov	w2, 1
.L1993:
	bl	toshiba_tlc_set_rr_para
	b	.L1956
.L1964:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1956:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 36
	bne	.L1965
	ubfiz	x22, x22, 8, 8
	mov	w0, 85
	add	x25, x25, x22
	str	w0, [x25,2056]
	mov	w0, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2048]
	str	w0, [x25,2056]
.L1965:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1236]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1966
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1966:
	cmn	w19, #1
	beq	.L1972
	cmp	w19, 256
	bne	.L1967
.L1972:
	adrp	x0, .LC158
	mov	w1, w21
	add	x0, x0, :lo12:.LC158
	mov	w2, w24
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1967:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	toshiba_3d_read_retrial, .-toshiba_3d_read_retrial
	.align	2
	.global	toshiba_read_retrial
	.type	toshiba_read_retrial, %function
toshiba_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	uxtb	w21, w0
	str	w1, [x29,124]
	mov	x26, x2
	str	w4, [x29,120]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	x27, x3
	bl	nandc_wait_flash_ready
	mov	w0, w21
	adrp	x19, .LANCHOR0
	mov	w25, 0
	bl	zftl_flash_enter_slc_mode
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
	add	x1, x19, :lo12:.LANCHOR0
	sxtw	x0, w21
	add	x22, x0, 8
	str	x0, [x29,112]
	ldrb	w0, [x1,1237]
	ldr	x23, [x1,1048]
	sub	w0, w0, #67
	add	x22, x23, x22, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1995
	ldrb	w0, [x1,1184]
	cbz	w0, .L1996
	mov	w0, 1
	mov	w25, 1
	bl	nandc_set_if_mode
.L1996:
	ubfiz	x0, x21, 8, 8
	mov	w1, 92
	add	x0, x23, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1995:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x21, 8, 8
	str	x0, [x29,96]
.L1997:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0,232]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L2028
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,1237]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x22
	bhi	.L1998
	bl	sandisk_set_rr_para
	b	.L1999
.L1998:
	bl	toshiba_set_rr_para
.L1999:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	cmp	w0, 34
	bne	.L2000
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0,232]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L2000
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x23, x0
	str	w1, [x0,8]
.L2000:
	ldr	x0, [x29,96]
	mov	w1, 38
	ldr	w4, [x29,120]
	mov	x2, x26
	add	x0, x23, x0
	mov	x3, x27
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	mov	w0, w21
	ldr	w1, [x29,124]
	bl	flash_read_page
	cmn	w0, #1
	mov	w28, w0
	beq	.L2003
	cmn	w24, #1
	csel	w24, w24, w0, ne
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x26, [x0,208]
	ldr	x27, [x0,224]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L2005
.L2003:
	add	w20, w20, 1
	b	.L1997
.L2028:
	mov	w28, w24
.L2005:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,1237]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x22
	bhi	.L2007
	bl	sandisk_set_rr_para
	b	.L2008
.L2007:
	bl	toshiba_set_rr_para
.L2008:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x23, x23, x0, lsl 8
	mov	w0, 255
	str	w0, [x23,8]
	ldrb	w0, [x19,1236]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L2009
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L2009:
	cmn	w28, #1
	beq	.L2015
	cmp	w28, 256
	bne	.L2010
.L2015:
	adrp	x0, .LC158
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC158
	mov	w1, w20
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L2010:
	bl	nandc_wait_flash_ready
	cbz	w25, .L2012
	mov	w0, 4
	bl	nandc_set_if_mode
.L2012:
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	toshiba_read_retrial, .-toshiba_read_retrial
	.align	2
	.global	ymtc_3d_read_retrial
	.type	ymtc_3d_read_retrial, %function
ymtc_3d_read_retrial:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	x22, w0
	mov	w24, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x25, x2
	mov	x26, x3
	mov	w27, w4
	bl	nandc_wait_flash_ready
	mov	w0, w22
	adrp	x21, .LANCHOR0
	mov	x23, x22
	mov	w19, -1
	bl	zftl_flash_enter_slc_mode
	mov	w20, 1
	mov	w0, w22
	add	x22, x22, 8
	adrp	x28, .LANCHOR5
	bl	zftl_flash_exit_slc_mode
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	add	x22, x0, x22, lsl 8
	ubfx	x0, x24, 24, 2
	cbnz	w0, .L2039
.L2034:
	mov	x0, x22
	mov	w1, w20
	bl	ymtc_3d_set_slc_rr_para
	mov	w0, w23
	mov	w1, w24
	mov	x2, x25
	mov	x3, x26
	mov	w4, w27
	bl	flash_read_page
	cmn	w0, #1
	beq	.L2031
	add	x1, x28, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,208]
	ldr	x26, [x1,224]
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1,1236]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2043
.L2031:
	add	w20, w20, 1
	cmp	w20, 10
	bne	.L2034
	b	.L2033
.L2043:
	mov	w19, w0
.L2033:
	mov	x0, x22
	mov	w1, 0
	bl	ymtc_3d_set_slc_rr_para
	b	.L2035
.L2059:
	add	x1, x28, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,208]
	ldr	x26, [x1,224]
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1,1236]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2044
.L2036:
	add	w20, w20, 1
	cmp	w20, 51
	beq	.L2038
.L2039:
	mov	x0, x22
	mov	w1, w20
	bl	ymtc_3d_set_tlc_rr_para
	mov	w0, w23
	mov	w1, w24
	mov	x2, x25
	mov	x3, x26
	mov	w4, w27
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2059
	b	.L2036
.L2044:
	mov	w19, w0
.L2038:
	mov	x0, x22
	mov	w1, 0
	bl	ymtc_3d_set_tlc_rr_para
.L2035:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1236]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2040
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2040:
	cmn	w19, #1
	beq	.L2045
	cmp	w19, 256
	bne	.L2041
.L2045:
	adrp	x0, .LC159
	mov	w1, w20
	add	x0, x0, :lo12:.LC159
	mov	w2, w24
	mov	w3, w20
	mov	w4, w19
	bl	printk
.L2041:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ymtc_3d_read_retrial, .-ymtc_3d_read_retrial
	.align	2
	.global	samsung_read_retrial
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w22, w1
	uxtb	w23, w0
	stp	x25, x26, [sp,64]
	str	w4, [x29,140]
	mov	x25, x2
	mov	x26, x3
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	bl	nandc_wait_flash_ready
	uxtb	x19, w23
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	add	x0, x21, :lo12:.LANCHOR0
	adrp	x5, .LANCHOR4
	add	x5, x5, :lo12:.LANCHOR4
	ldr	x24, [x0,1048]
	ubfx	x0, x22, 24, 2
	cbnz	w0, .L2061
	add	x0, x5, 185
	add	x5, x5, 160
	str	x0, [x29,120]
	mov	w0, 1
	sub	w0, w0, w5
	str	w0, [x29,112]
	lsl	x0, x19, 8
	str	x0, [x29,128]
	add	x19, x24, x0
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	mov	w27, -1
	str	x0, [x29,104]
.L2066:
	ldr	w0, [x29,112]
	str	x5, [x29,96]
	add	w20, w0, w5
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 141
	str	w0, [x19,2052]
	ldrsb	w0, [x5,1]
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	nandc_wait_flash_ready
	ldr	w4, [x29,140]
	mov	w0, w23
	mov	w1, w22
	mov	x2, x25
	mov	x3, x26
	bl	flash_read_page
	mov	w28, w0
	adrp	x0, .LANCHOR2
	ldr	x5, [x29,96]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2062
	ldr	x0, [x29,104]
	mov	w1, w20
	mov	w2, w22
	mov	w3, w28
	bl	printk
	ldr	x5, [x29,96]
.L2062:
	cmn	w28, #1
	beq	.L2063
	adrp	x0, .LANCHOR5
	cmn	w27, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w27, w27, w28, ne
	ldr	x25, [x0,208]
	ldr	x26, [x0,224]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L2076
.L2063:
	ldr	x0, [x29,120]
	add	x5, x5, 1
	cmp	x5, x0
	bne	.L2066
	mov	w20, 26
	b	.L2065
.L2076:
	mov	w27, w28
.L2065:
	ldr	x0, [x29,128]
	add	x19, x24, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 141
	b	.L2098
.L2061:
	lsl	x0, x19, 8
	str	x0, [x29,128]
	add	x19, x24, x0
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	add	x28, x5, 192
	mov	w27, -1
	mov	w20, 1
	str	x0, [x29,120]
.L2072:
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 137
	str	w0, [x19,2052]
	ldrb	w0, [x28,4]
	str	w0, [x19,2048]
	ldrb	w0, [x28,5]
	str	w0, [x19,2048]
	ldrb	w0, [x28,6]
	str	w0, [x19,2048]
	ldrb	w0, [x28,7]
	str	w0, [x19,2048]
	bl	nandc_wait_flash_ready
	ldr	w4, [x29,140]
	mov	w0, w23
	mov	w1, w22
	mov	x2, x25
	mov	x3, x26
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2068
	ldr	x0, [x29,120]
	mov	w3, w4
	mov	w1, w20
	mov	w2, w22
	str	x4, [x29,112]
	bl	printk
	ldr	x4, [x29,112]
.L2068:
	cmn	w4, #1
	beq	.L2069
	adrp	x0, .LANCHOR5
	cmn	w27, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w27, w27, w4, ne
	ldr	x25, [x0,208]
	ldr	x26, [x0,224]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L2077
.L2069:
	add	w20, w20, 1
	add	x28, x28, 4
	cmp	w20, 26
	bne	.L2072
	b	.L2071
.L2077:
	mov	w27, w4
.L2071:
	ldr	x0, [x29,128]
	add	x19, x24, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 137
.L2098:
	str	w0, [x19,2052]
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	nandc_wait_flash_ready
	ldrb	w0, [x21,1236]
	add	w0, w0, w0, lsl 1
	cmp	w27, w0, lsr 2
	bcc	.L2073
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L2073:
	cmn	w27, #1
	beq	.L2078
	cmp	w27, 256
	bne	.L2074
.L2078:
	adrp	x0, .LC162
	mov	w1, w20
	add	x0, x0, :lo12:.LC162
	mov	w2, w22
	mov	w3, w20
	mov	w4, w27
	bl	printk
.L2074:
	bl	nandc_wait_flash_ready
	mov	w0, w27
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	hynix_read_retrial
	.type	hynix_read_retrial, %function
hynix_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x27, x28, [sp,80]
	adrp	x28, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	x22, w0
	add	x0, x28, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	x25, x2
	mov	w23, w1
	ldr	x5, [x0,1240]
	mov	x26, x3
	str	w4, [x29,124]
	mov	x21, x22
	add	x27, x5, 112
	mov	w19, -1
	add	x0, x27, x22
	ldrb	w24, [x27,2]
	ldrb	w20, [x0,8]
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
	mov	w6, 0
	adrp	x7, .LANCHOR5
.L2100:
	cmp	w6, w24
	bcs	.L2104
	add	w20, w20, 1
	mov	w0, w21
	str	x7, [x29,104]
	uxtb	w20, w20
	str	x6, [x29,112]
	cmp	w20, w24
	csel	w20, w20, wzr, cc
	mov	w1, w20
	bl	hynix_set_rr_para
	ldr	w4, [x29,124]
	mov	w0, w21
	mov	w1, w23
	mov	x2, x25
	mov	x3, x26
	bl	flash_read_page
	cmn	w0, #1
	ldr	x6, [x29,112]
	ldr	x7, [x29,104]
	beq	.L2102
	add	x1, x7, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,208]
	ldr	x26, [x1,224]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,1236]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2109
.L2102:
	add	w6, w6, 1
	b	.L2100
.L2109:
	mov	w19, w0
.L2104:
	add	x28, x28, :lo12:.LANCHOR0
	add	x22, x27, x22
	ldrb	w0, [x28,1236]
	strb	w20, [x22,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2106
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2106:
	cmn	w19, #1
	beq	.L2110
	cmp	w19, 256
	bne	.L2107
.L2110:
	adrp	x0, .LC163
	mov	w1, w6
	add	x0, x0, :lo12:.LC163
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L2107:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	hynix_read_retrial, .-hynix_read_retrial
	.align	2
	.global	flash_ddr_tuning_read
	.type	flash_ddr_tuning_read, %function
flash_ddr_tuning_read:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w20, w0
	mov	w22, w1
	mov	x24, x2
	mov	x23, x3
	str	w4, [x29,120]
	bl	nandc_get_ddr_para
	mov	w27, 0
	str	w0, [x29,116]
	adrp	x0, .LC164
	str	w27, [x29,124]
	add	x0, x0, :lo12:.LC164
	mov	w25, w27
	mov	w28, w27
	mov	w19, 1024
	mov	w26, 6
	mov	w21, -1
	str	x0, [x29,104]
.L2125:
	mov	w0, w26
	bl	nandc_set_ddr_para
	ldr	w4, [x29,120]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2120
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w26
	mov	w2, w22
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L2120:
	add	w0, w19, 1
	cmp	w4, w0
	bhi	.L2121
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x24, [x0,208]
	ldr	x23, [x0,224]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,1236]
	cmp	w4, w0, lsr 2
	bcs	.L2131
	add	w28, w28, 1
	cmp	w28, 7
	bls	.L2131
	sub	w27, w26, w28
	mov	w19, w4
	mov	w21, 0
	b	.L2123
.L2121:
	cmp	w25, w28
	bcs	.L2132
	sub	w0, w27, w28
	cmp	w28, 7
	str	w0, [x29,124]
	bhi	.L2124
	mov	w25, w28
	b	.L2132
.L2131:
	mov	w27, w26
	mov	w19, w4
	mov	w21, 0
	b	.L2122
.L2132:
	mov	w28, 0
.L2122:
	add	w26, w26, 2
	cmp	w26, 50
	bne	.L2125
.L2123:
	ldr	w0, [x29,124]
	cmp	w25, w28
	csel	w27, w27, w0, ls
.L2124:
	cbz	w27, .L2126
	adrp	x0, .LANCHOR0+1236
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+1236]
	udiv	w0, w0, w1
	cmp	w19, w0
	bcs	.L2126
	adrp	x0, .LC165
	mov	w1, w27
	add	x0, x0, :lo12:.LC165
	bl	printk
	mov	w0, w27
	b	.L2144
.L2126:
	ldrb	w0, [x29,116]
.L2144:
	bl	nandc_set_ddr_para
	cbz	w21, .L2128
	adrp	x0, .LANCHOR0
	add	x27, x0, :lo12:.LANCHOR0
	mov	x26, x0
	ldrb	w1, [x27,1304]
	tbz	x1, 0, .L2128
	adrp	x0, .LC166
	mov	w2, w22
	mov	w1, w20
	add	x0, x0, :lo12:.LC166
	bl	printk
	mov	w0, w20
	bl	flash_reset
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	add	x0, x27, x20, sxtw
	mov	w1, 2
	strb	w1, [x0,1264]
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	ldr	w4, [x29,120]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	bl	flash_read_page
	mov	w19, w0
	adrp	x0, .LC167
	mov	w1, w20
	add	x0, x0, :lo12:.LC167
	mov	w2, w22
	mov	w3, w19
	bl	printk
	ldrb	w0, [x27,1236]
	cmp	w19, w0
	bhi	.L2133
	adrp	x1, .LANCHOR5
	add	x1, x1, :lo12:.LANCHOR5
	ldr	w0, [x1,236]
	add	w0, w0, 1
	str	w0, [x1,236]
	cmp	w0, 100
	bls	.L2129
	strb	wzr, [x27,1184]
	b	.L2128
.L2133:
	mov	w19, w21
.L2129:
	add	x20, x26, :lo12:.LANCHOR0
	ldrb	w0, [x20,1304]
	bl	flash_set_interface_mode
	ldrb	w0, [x20,1304]
	bl	nandc_set_if_mode
.L2128:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	flash_ddr_tuning_read, .-flash_ddr_tuning_read
	.align	2
	.global	flash_read_page_en
	.type	flash_read_page_en, %function
flash_read_page_en:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	w19, w1
	stp	x25, x26, [sp,64]
	uxtb	w25, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	mov	x24, x2
	mov	x23, x3
	ldrb	w0, [x0,1145]
	mov	w22, w4
	cmp	w0, w25
	bhi	.L2146
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 428
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2146:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, x25, sxtw
	ldrb	w21, [x1,1248]
	ldrb	w4, [x0,1145]
	cmp	w25, w4
	bcc	.L2147
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2173
	adrp	x0, .LC168
	mov	w1, w21
	add	x0, x0, :lo12:.LC168
	mov	w2, w25
	mov	w3, w19
	bl	printk
.L2173:
	mov	w0, -1
	b	.L2172
.L2147:
	ubfx	x1, x19, 24, 2
	cbnz	w1, .L2150
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L2151
	ldrb	w0, [x0,1]
	cbz	w0, .L2150
.L2151:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,1]
	sub	w19, w19, w0
	cbz	w2, .L2152
	add	w19, w0, w19, lsl 1
	b	.L2150
.L2152:
	add	x1, x1, 4
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L2150:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2172
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,1232]
	cbnz	w26, .L2154
.L2157:
	adrp	x0, .LANCHOR5+240
	ldr	x5, [x0,#:lo12:.LANCHOR5+240]
	cbnz	x5, .L2155
	b	.L2156
.L2154:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	strb	wzr, [x25,1232]
	bl	flash_read_page
	strb	w26, [x25,1232]
	cmn	w0, #1
	beq	.L2157
	b	.L2172
.L2155:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	blr	x5
	cmn	w0, #1
	bne	.L2172
.L2156:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC169
	add	x0, x0, :lo12:.LC169
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,1232]
	bl	printk
	ldrb	w0, [x20,1184]
	cbz	w0, .L2173
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_ddr_tuning_read
.L2172:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_read_page_en, .-flash_read_page_en
	.align	2
	.global	flash_get_last_written_page
	.type	flash_get_last_written_page, %function
flash_get_last_written_page:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	adrp	x0, .LANCHOR5+92
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x24, x2
	ldrh	w19, [x0,#:lo12:.LANCHOR5+92]
	add	x0, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxth	w22, w1
	sub	w19, w19, #1
	ldrh	w26, [x0,34]
	mov	w0, w23
	sxth	w19, w19
	mov	x21, x3
	mov	w25, w4
	mov	x27, x20
	mul	w26, w22, w26
	add	w1, w26, w19
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L2175
	mov	w28, 0
	mov	w5, 2
.L2176:
	cmp	w28, w19
	bgt	.L2175
	add	w0, w28, w19
	mov	x2, x24
	mov	x3, x21
	mov	w4, w25
	sdiv	w20, w0, w5
	mov	w0, w23
	str	x5, [x29,104]
	add	w1, w26, w20, sxth
	bl	flash_read_page_en
	cmp	w0, 512
	ldr	x5, [x29,104]
	bne	.L2177
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L2176
.L2177:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L2176
.L2175:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2180
	adrp	x0, .LC170
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC170
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L2180:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	uxth	w6, w0
	adrp	x0, .LANCHOR0+1257
	stp	x29, x30, [sp, -16]!
	mov	w5, 24
	mov	x8, x1
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+1257]
	mov	x7, x2
	mov	w4, w3
	mov	x2, x8
	sub	w5, w5, w0
	adrp	x0, .LANCHOR3+1232
	mov	x3, x7
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1232]
	sub	w0, w5, w0
	mov	w5, 1
	lsl	w5, w5, w0
	asr	w0, w6, w0
	sub	w1, w5, #1
	and	w1, w6, w1
	bl	flash_get_last_written_page
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_get_last_written_page_ext, .-flash_get_last_written_page_ext
	.align	2
	.global	flash_ddr_para_scan
	.type	flash_ddr_para_scan, %function
flash_ddr_para_scan:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR5
	stp	x21, x22, [sp,32]
	add	x21, x19, :lo12:.LANCHOR0
	mov	w22, 1
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	mov	w23, w1
	ldrb	w0, [x21,1304]
	add	x20, x20, :lo12:.LANCHOR5
	strb	w22, [x21,1184]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,1304]
	bl	nandc_set_if_mode
	ldr	x2, [x20,248]
	mov	w1, w23
	ldr	x3, [x20,256]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tuning_read
	ldr	x2, [x20,248]
	mov	w0, w24
	ldr	x3, [x20,256]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2186
	ldrb	w0, [x21,1304]
	tbz	x0, 0, .L2186
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,1184]
	b	.L2187
.L2186:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1184]
.L2187:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	uxtb	x24, w0
	add	x0, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	adrp	x22, .LANCHOR2
	mov	x21, x24
	ldrb	w19, [x0,1257]
	add	x24, x24, 8
	ldr	x20, [x0,1048]
	mov	w0, 24
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	mov	w25, w1
	sub	w19, w19, #1
	mov	x26, x3
	mov	x27, x2
	mov	w3, w4
	add	x24, x20, x24, lsl 8
	and	w19, w1, w19
	ubfx	x28, x25, 24, 2
	tbz	x0, 4, .L2192
	adrp	x0, .LC171
	mov	w2, w28
	add	x0, x0, :lo12:.LC171
	bl	printk
.L2192:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w28, .L2193
	bl	zftl_flash_enter_slc_mode
	b	.L2194
.L2193:
	bl	zftl_flash_exit_slc_mode
.L2194:
	ubfiz	x5, x21, 8, 8
	mov	w0, 128
	add	x20, x20, x5
	add	x23, x23, :lo12:.LANCHOR0
	str	w0, [x20,2056]
	and	w0, w19, 255
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	ldrb	w0, [x23,1256]
	cbz	w0, .L2195
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L2195:
	mov	w0, w19
	add	x22, x22, :lo12:.LANCHOR2
	bl	nandc_set_seed
	ldrb	w2, [x22,17]
	mov	w1, 1
	mov	x3, x27
	mov	x4, x26
	mov	w0, w21
	bl	nandc_xfer
	mov	w0, 16
	str	w0, [x20,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x24
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w21
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2196
	adrp	x0, .LC172
	mov	w1, w25
	add	x0, x0, :lo12:.LC172
	bl	printk
	mov	w2, -1
.L2196:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_prog_page, .-flash_prog_page
	.align	2
	.global	flash_test_blk
	.type	flash_test_blk, %function
flash_test_blk:
	stp	x29, x30, [sp, -48]!
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR5
	uxth	w20, w1
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 165
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	ldr	x0, [x19,208]
	bl	ftl_memset
	ldr	x0, [x19,224]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	adrp	x0, .LANCHOR0+2
	ldrh	w0, [x0,#:lo12:.LANCHOR0+2]
	mul	w20, w20, w0
	mov	w0, w22
	mov	w1, w20
	bl	flash_erase_block
	cmn	w0, #1
	bne	.L2207
.L2209:
	mov	w19, -1
	b	.L2208
.L2207:
	adrp	x21, .LANCHOR2
	ldr	x2, [x19,208]
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x3, [x19,224]
	mov	w0, w22
	mov	w1, w20
	add	x21, x21, 8
	ldrb	w4, [x21,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2209
	ldr	x2, [x19,208]
	mov	w0, w22
	ldr	x3, [x19,224]
	mov	w1, w20
	ldrb	w4, [x21,9]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2209
	ldr	x0, [x19,208]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L2209
	ldr	x0, [x19,224]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csetm	w19, ne
.L2208:
	mov	w1, w20
	mov	w0, w22
	bl	flash_erase_block
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.align	2
	.global	flash_start_one_pass_page_prog
	.type	flash_start_one_pass_page_prog, %function
flash_start_one_pass_page_prog:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp,64]
	uxtb	w26, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	w25, w3
	uxtb	w22, w2
	ldr	x19, [x0,1048]
	mov	w0, w25
	mov	w21, w4
	mov	x24, x5
	mov	x23, x6
	bl	nandc_cs
	cbz	w26, .L2217
	sxtw	x1, w25
	add	x1, x1, 8
	add	x1, x19, x1, lsl 8
	str	w26, [x1,8]
.L2217:
	ubfiz	x1, x25, 8, 8
	mov	w0, 128
	add	x19, x19, x1
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w21, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w21, 8
	str	w0, [x19,2052]
	lsr	w0, w21, 16
	str	w0, [x19,2052]
	ldrb	w0, [x20,1256]
	cbz	w0, .L2218
	lsr	w0, w21, 24
	str	w0, [x19,2052]
.L2218:
	mov	w0, w21
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	x3, x24
	mov	x4, x23
	mov	w0, w25
	mov	w1, 1
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	str	w22, [x19,2056]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_one_pass_page_prog, .-flash_start_one_pass_page_prog
	.align	2
	.global	flash_dual_page_prog
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x21, w0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w22, w1
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	adrp	x27, .LANCHOR2
	mov	x26, x2
	ldrb	w19, [x0,1257]
	mov	x20, x21
	ldr	x2, [x0,1048]
	mov	w0, 24
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	add	x21, x21, 8
	sub	w19, w19, #1
	mov	x25, x3
	mov	x24, x4
	mov	x23, x5
	mov	w3, w6
	add	x21, x2, x21, lsl 8
	and	w19, w1, w19
	ubfx	x28, x22, 24, 2
	tbz	x0, 4, .L2226
	adrp	x0, .LC171
	mov	w2, w28
	add	x0, x0, :lo12:.LC171
	bl	printk
.L2226:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w28, .L2227
	bl	zftl_flash_enter_slc_mode
	b	.L2228
.L2227:
	bl	zftl_flash_exit_slc_mode
.L2228:
	mov	w0, 0
	mov	w4, w19
	mov	w1, w0
	mov	w2, 16
	mov	w3, w20
	mov	x5, x26
	mov	x6, x25
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	w0, 0
	mov	w2, 16
	add	w4, w19, 1
	mov	w1, w0
	mov	w3, w20
	mov	x5, x24
	mov	x6, x23
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	x0, x21
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w20
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2229
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2230
	adrp	x0, .LC172
	mov	w1, w22
	add	x0, x0, :lo12:.LC172
	bl	printk
.L2230:
	mov	w2, -1
.L2229:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.align	2
	.global	ymtc_flash_tlc_page_prog
	.type	ymtc_flash_tlc_page_prog, %function
ymtc_flash_tlc_page_prog:
	stp	x29, x30, [sp, -80]!
	mov	w4, 24
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x21, w0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	w25, w1
	mov	x20, x21
	ldr	x1, [x0,1048]
	add	x21, x21, 8
	ldrb	w19, [x0,1257]
	mov	x23, x2
	mov	x22, x3
	add	x21, x1, x21, lsl 8
	sub	w19, w4, w19
	bl	nandc_wait_flash_ready
	mov	w0, w20
	mov	w24, 1
	lsl	w19, w24, w19
	bl	nandc_cs
	sub	w19, w19, #1
	mov	w0, w20
	and	w19, w19, w25
	bl	zftl_flash_exit_slc_mode
	mov	w4, w19
	mov	w1, w24
	mov	w2, 26
	mov	w3, w20
	mov	x5, x23
	mov	x6, x22
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	add	w4, w19, w24
	mov	w1, w24
	mov	w2, 26
	mov	w3, w20
	mov	x5, x23
	mov	x6, x22
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	w2, 16
	add	w4, w19, 2
	mov	w1, w24
	mov	w3, w20
	mov	x5, x23
	mov	x6, x22
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x21
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w20
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2238
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2239
	adrp	x0, .LC173
	mov	w1, w25
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2239:
	mov	w2, -1
.L2238:
	mov	w0, w2
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ymtc_flash_tlc_page_prog, .-ymtc_flash_tlc_page_prog
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.31, %function
fw_flash_page_prog.constprop.31:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x19, x19, :lo12:.LANCHOR0
	str	x2, [x29,72]
	stp	x21, x22, [sp,32]
	mov	x22, x1
	ldr	x1, [x19,1136]
	ldrb	w21, [x19,1236]
	ldrb	w20, [x1,9]
	udiv	w20, w0, w20
	ldrb	w0, [x19,1144]
	bl	nandc_bch_sel
	adrp	x4, .LANCHOR2
	ldr	x3, [x29,72]
	add	x4, x4, :lo12:.LANCHOR2
	add	x4, x4, 8
	ldrb	w0, [x4,7]
	cmp	w0, 9
	bne	.L2244
	ldrb	w23, [x19,1146]
	cbnz	w23, .L2244
	ldrb	w0, [x4,12]
	cmp	w0, 3
	bne	.L2245
	mov	w0, w23
	mov	w1, w20
	mov	x2, x22
	bl	ymtc_flash_tlc_page_prog
	b	.L2247
.L2245:
	adrp	x19, .LANCHOR5
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR5
	mov	w2, 16384
	str	x3, [x29,72]
	ldr	x0, [x19,208]
	bl	ftl_memset
	ldr	x4, [x19,208]
	mov	w0, w23
	ldr	x3, [x29,72]
	mov	w1, w20
	mov	x2, x22
	mov	x5, x4
	mov	w6, 4
	bl	flash_dual_page_prog
	b	.L2247
.L2244:
	mov	w0, 0
	mov	w1, w20
	mov	x2, x22
	mov	w4, 4
	bl	flash_prog_page
.L2247:
	mov	w19, w0
	mov	w0, w21
	bl	nandc_bch_sel
	ldr	x23, [sp,48]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	fw_flash_page_prog.constprop.31, .-fw_flash_page_prog.constprop.31
	.text
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	stp	x29, x30, [sp, -80]!
	adrp	x7, .LANCHOR0
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	uxtb	w26, w0
	add	x0, x7, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	w25, w3
	uxtb	w21, w1
	ldrb	w0, [x0,1145]
	uxtb	w22, w2
	mov	w20, w4
	mov	x24, x5
	cmp	w0, w25
	mov	x23, x6
	mov	x19, x7
	bhi	.L2249
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 859
	add	x1, x1, 320
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2249:
	add	x7, x19, :lo12:.LANCHOR0
	ldrb	w0, [x7,1145]
	cmp	w0, w25
	bls	.L2248
	add	x25, x7, x25, sxtw
	ldrb	w25, [x25,1248]
	ldr	x19, [x7,1048]
	mov	w0, w25
	bl	nandc_cs
	cbz	w26, .L2251
	sxtw	x0, w25
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w26, [x0,8]
.L2251:
	ubfiz	x7, x25, 8, 8
	mov	w0, 128
	add	x19, x19, x7
	str	w21, [x19,2056]
	str	w0, [x19,2056]
	and	w0, w20, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w20, 8
	str	w0, [x19,2052]
	lsr	w0, w20, 16
	add	w20, w20, w20, lsl 1
	str	w0, [x19,2052]
	sub	w0, w20, #1
	add	w0, w0, w21
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	w0, w25
	mov	w1, 1
	mov	x3, x24
	mov	x4, x23
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	str	w22, [x19,2056]
	mov	w0, w25
	bl	nandc_de_cs
.L2248:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_tlc_page_prog, .-flash_start_tlc_page_prog
	.align	2
	.type	queue_tlc_prog_cmd, %function
queue_tlc_prog_cmd:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	x20, x0
	ldr	x6, [x0]
	add	x0, x23, :lo12:.LANCHOR0
	mov	w24, w1
	mov	w1, 24
	mov	w21, 1
	ldrb	w3, [x0,1257]
	ldr	w2, [x6,40]
	sub	w1, w1, w3
	lsl	w3, w21, w3
	lsl	w22, w21, w1
	sub	w19, w3, #1
	sub	w22, w22, #1
	ldrb	w0, [x0,1269]
	and	w22, w22, w2
	lsr	w2, w2, w1
	and	w19, w19, w2
	uxtb	w19, w19
	cbz	w0, .L2256
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x6, [x20]
	mov	w1, w21
	mov	w3, w19
	mov	w4, w22
	mov	w2, 26
	mov	w0, 0
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,8]
	mov	w1, w21
	mov	w3, w19
	add	w4, w22, w21
	mov	w2, 26
	mov	w0, 0
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,16]
	mov	w0, 0
	mov	w1, w21
	mov	w2, 16
	mov	w3, w19
	add	w4, w22, 2
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	b	.L2257
.L2256:
	ldr	x5, [x6,8]
	mov	w1, w21
	ldrb	w0, [x6,60]
	mov	w2, 26
	ldr	x6, [x6,24]
	mov	w3, w19
	mov	w4, w22
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,8]
	mov	w1, 2
	ldr	x0, [x20]
	mov	w2, 26
	mov	w3, w19
	mov	w4, w22
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,16]
	mov	w1, 3
	ldr	x0, [x20]
	mov	w2, 16
	mov	w3, w19
	mov	w4, w22
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
.L2257:
	cbz	w24, .L2255
	ldr	x1, [x20]
	mov	w0, 4
	strb	w0, [x1,58]
	mov	w0, 1
	strb	w0, [x1,59]
	mov	w0, -1
	strb	w0, [x1]
	add	x0, x23, :lo12:.LANCHOR0
	add	x0, x0, 3363
	bl	buf_add_tail
.L2255:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR0
	mov	w3, 24
	add	x2, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	w22, 1
	mov	x19, x0
	ldrb	w1, [x2,1257]
	ldr	w0, [x0,40]
	sub	w3, w3, w1
	lsl	w1, w22, w1
	lsl	w21, w22, w3
	sub	w20, w1, #1
	sub	w21, w21, #1
	ldrb	w23, [x2,1269]
	and	w21, w21, w0
	lsr	w0, w0, w3
	and	w20, w20, w0
	uxtb	w20, w20
	cbz	w23, .L2263
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
	ldr	x5, [x19,8]
	mov	w1, w22
	ldr	x6, [x19,24]
	mov	w3, w20
	mov	w4, w21
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x19,8]
	mov	w1, w22
	ldr	x6, [x19,24]
	mov	w3, w20
	add	w4, w21, w22
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x19,8]
	mov	w0, 0
	ldr	x6, [x19,24]
	mov	w1, w22
	mov	w2, 16
	mov	w3, w20
	add	w4, w21, 2
	bl	flash_start_one_pass_page_prog
	b	.L2264
.L2263:
	ldr	x5, [x19,8]
	mov	w1, w22
	ldr	x6, [x19,24]
	mov	w2, 26
	mov	w3, w20
	mov	w4, w21
	mov	w0, w23
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x19,8]
	mov	w1, 2
	ldr	x6, [x19,24]
	mov	w2, 26
	mov	w3, w20
	mov	w4, w21
	mov	w0, w23
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x19,8]
	mov	w0, w23
	ldr	x6, [x19,24]
	mov	w1, 3
	mov	w2, 16
	mov	w3, w20
	mov	w4, w21
	bl	flash_start_tlc_page_prog
.L2264:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	w0, [x19,40]
	mov	w1, 64
	bl	flash_wait_device_ready
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	sblk_3d_tlc_dump_prog, .-sblk_3d_tlc_dump_prog
	.align	2
	.global	flash_start_3d_mlc_page_prog
	.type	flash_start_3d_mlc_page_prog, %function
flash_start_3d_mlc_page_prog:
	stp	x29, x30, [sp, -64]!
	adrp	x5, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	add	x0, x5, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	uxtb	w20, w1
	mov	w21, w2
	ldrb	w0, [x0,1145]
	mov	x19, x5
	cmp	w0, w20
	bhi	.L2266
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 892
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC0
	str	x4, [x29,48]
	str	x3, [x29,56]
	bl	printk
	bl	dump_stack
	ldr	x4, [x29,48]
	ldr	x3, [x29,56]
.L2266:
	add	x5, x19, :lo12:.LANCHOR0
	ldrb	w0, [x5,1145]
	cmp	w0, w20
	bls	.L2265
	add	x20, x5, x20, sxtw
	ldrb	w20, [x20,1248]
	ldr	x19, [x5,1048]
	mov	w0, w20
	str	x4, [x29,48]
	str	x3, [x29,56]
	bl	nandc_cs
	ubfiz	x1, x20, 8, 8
	mov	w0, 128
	add	x19, x19, x1
	str	w0, [x19,2056]
	and	w0, w21, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w21, 8
	str	w0, [x19,2052]
	lsr	w0, w21, 16
	str	w0, [x19,2052]
	mov	w0, w21
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	ldr	x3, [x29,56]
	ldr	x4, [x29,48]
	mov	w0, w20
	mov	w1, 1
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	str	w22, [x19,2056]
.L2265:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_start_3d_mlc_page_prog, .-flash_start_3d_mlc_page_prog
	.align	2
	.global	sblk_mlc_dump_prog
	.type	sblk_mlc_dump_prog, %function
sblk_mlc_dump_prog:
	stp	x29, x30, [sp, -48]!
	mov	w1, 24
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	ldr	w3, [x0,40]
	adrp	x0, .LANCHOR0+1257
	ldrb	w19, [x0,#:lo12:.LANCHOR0+1257]
	mov	w0, 1
	sub	w1, w1, w19
	lsl	w21, w0, w1
	lsl	w0, w0, w19
	sub	w21, w21, #1
	sub	w19, w0, #1
	and	w21, w21, w3
	lsr	w3, w3, w1
	and	w19, w19, w3
	uxtb	w19, w19
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2269
	ldr	w2, [x20,40]
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	mov	w1, w21
	add	w3, w2, 1
	bl	printk
.L2269:
	ldr	x3, [x20,8]
	mov	w1, w19
	ldr	x4, [x20,24]
	mov	w2, w21
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x3, [x20,8]
	add	w2, w21, 1
	ldr	x4, [x20,24]
	mov	w1, w19
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	w0, [x20,40]
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	mov	w0, w19
	bl	nandc_de_cs
	ldr	x21, [sp,32]
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	sblk_mlc_dump_prog, .-sblk_mlc_dump_prog
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	stp	x29, x30, [sp, -96]!
	mov	w5, 1
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x25, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	w19, 24
	mov	w24, w1
	ldrb	w20, [x0,1257]
	mov	x23, x2
	ldrb	w0, [x0,1145]
	mov	x22, x3
	sub	w19, w19, w20
	lsl	w20, w5, w20
	lsr	w1, w1, w19
	sub	w20, w20, #1
	and	w20, w1, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2274
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 945
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2274:
	add	x28, x25, :lo12:.LANCHOR0
	ldrb	w0, [x28,1145]
	cmp	w0, w20
	bls	.L2273
	mov	w0, 1
	ldr	x27, [x28,1048]
	lsl	w19, w0, w19
	sub	w19, w19, #1
	add	x0, x28, x20, sxtw
	and	w19, w24, w19
	ldrb	w26, [x0,1248]
	bl	nandc_rdy_status
	cbnz	w0, .L2276
	ldrb	w0, [x28,1145]
	cmp	w0, 1
	bne	.L2277
	bl	nandc_wait_flash_ready
	b	.L2276
.L2277:
	mov	w0, w20
	mov	w1, w19
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L2276:
	mov	w0, w26
	ubfx	x24, x24, 24, 2
	bl	hynix_reconfig_rr_para
	mov	w0, w26
	bl	nandc_cs
	cbnz	w24, .L2278
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
	ldrb	w0, [x25,#:lo12:.LANCHOR0]
	cbz	w0, .L2279
	mov	w0, w26
	bl	zftl_flash_enter_slc_mode
	b	.L2279
.L2278:
	mov	w0, w26
	bl	zftl_flash_exit_slc_mode
.L2279:
	ubfiz	x1, x26, 8, 8
	mov	w0, 128
	add	x20, x27, x1
	add	x25, x25, :lo12:.LANCHOR0
	str	w0, [x20,2056]
	and	w0, w19, 255
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	ldrb	w0, [x25,1256]
	cbz	w0, .L2280
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L2280:
	mov	w0, w19
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	w0, w26
	mov	w1, 1
	mov	x3, x23
	mov	x4, x22
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	str	w21, [x20,2056]
	mov	w0, w26
	bl	nandc_de_cs
.L2273:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_start_page_prog, .-flash_start_page_prog
	.align	2
	.type	queue_prog_cmd, %function
queue_prog_cmd:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	mov	w0, 16
	ldr	x2, [x19,8]
	ldr	x3, [x19,24]
	ldr	w1, [x19,40]
	bl	flash_start_page_prog
	adrp	x0, .LANCHOR0
	ldr	w4, [x19,40]
	add	x3, x0, :lo12:.LANCHOR0
	ldrb	w2, [x3,3363]
	cmp	w2, 255
	beq	.L2288
	ldrb	w5, [x3,1257]
	mov	w1, 1
	mov	w7, 24
	add	x3, x3, 1312
	lsl	w1, w1, w5
	sub	w7, w7, w5
	sub	w1, w1, #1
	asr	w4, w4, w7
	uxth	w1, w1
	and	w4, w1, w4
.L2290:
	add	x5, x3, x2, lsl 6
	ldr	w6, [x5,40]
	lsr	w6, w6, w7
	and	w6, w1, w6
	cmp	w4, w6
	bne	.L2289
	ldrb	w6, [x5,58]
	add	x5, x5, 48
	cmp	w6, 7
	bne	.L2289
	mov	w1, 3
	strb	w1, [x5,10]
	b	.L2288
.L2289:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L2290
.L2288:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 3363
	strb	w1, [x19]
	mov	x1, x19
	bl	buf_add_tail
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	queue_prog_cmd, .-queue_prog_cmd
	.align	2
	.global	flash_complete_plane_page_read
	.type	flash_complete_plane_page_read, %function
flash_complete_plane_page_read:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	w24, w0
	add	x0, x23, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	x22, x1
	mov	w19, 24
	ldrb	w20, [x0,1257]
	mov	w1, 1
	mov	x21, x2
	ldrb	w0, [x0,1145]
	sub	w19, w19, w20
	lsl	w20, w1, w20
	lsr	w2, w24, w19
	sub	w20, w20, #1
	and	w20, w2, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2296
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1055
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2296:
	add	x3, x23, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w1, [x3,1145]
	cmp	w1, w20
	bls	.L2297
	mov	w2, 1
	add	x20, x3, x20, sxtw
	ldrb	w20, [x20,1248]
	lsl	w19, w2, w19
	add	w19, w19, w0
	ubfx	x25, x24, 24, 2
	mov	w0, w20
	and	w19, w19, w24
	ldr	x24, [x3,1048]
	bl	nandc_cs
	cbnz	w25, .L2298
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2298:
	add	x7, x23, :lo12:.LANCHOR0
	uxtb	x5, w20
	and	w6, w19, 255
	lsr	w4, w19, 8
	lsr	w3, w19, 16
	ldrb	w0, [x7,1168]
	ldrb	w2, [x7,1256]
	cmp	w0, 1
	bne	.L2299
	add	x5, x24, x5, lsl 8
	mov	w0, 6
	b	.L2327
.L2299:
	ldr	x0, [x7,1136]
	add	x5, x24, x5, lsl 8
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2302
	mov	w0, 5
.L2327:
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w6, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	cbz	w2, .L2323
	lsr	w0, w19, 24
	str	w0, [x5,2052]
	b	.L2323
.L2302:
	str	wzr, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w6, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	cbz	w2, .L2304
	lsr	w0, w19, 24
	str	w0, [x5,2052]
.L2304:
	mov	w0, 5
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
.L2323:
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w25, .L2305
	add	x23, x23, :lo12:.LANCHOR0
	ldr	x0, [x23,1136]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2305
	ldrb	w0, [x23,1268]
	cbnz	w0, .L2305
	ldrb	w0, [x23,1269]
	cbnz	w0, .L2305
	sub	w1, w25, #1
	add	w0, w19, w19, lsl 1
	add	w0, w1, w0
	b	.L2324
.L2305:
	mov	w0, w19
.L2324:
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	w1, 0
	mov	x3, x22
	mov	x4, x21
	mov	w0, w20
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	mov	w19, w0
	mov	w0, w20
	bl	nandc_de_cs
	mov	w0, w19
.L2297:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_complete_plane_page_read, .-flash_complete_plane_page_read
	.align	2
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	stp	x29, x30, [sp, -112]!
	mov	w5, 1
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	mov	x23, x2
	stp	x21, x22, [sp,32]
	mov	x22, x1
	add	x1, x24, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w21, w0
	mov	w0, 24
	ldrb	w2, [x1,1257]
	ubfx	x25, x21, 24, 2
	sub	w0, w0, w2
	lsl	w19, w5, w0
	lsl	w5, w5, w2
	lsr	w20, w21, w0
	sub	w5, w5, #1
	and	w20, w20, w5
	ldrb	w0, [x1,1145]
	sub	w19, w19, #1
	uxtb	w20, w20
	and	w19, w19, w21
	cmp	w0, w20
	bhi	.L2329
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1212
	add	x1, x1, 440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2329:
	add	x0, x24, :lo12:.LANCHOR0
	add	x20, x0, x20, sxtw
	ldrb	w26, [x20,1248]
	ldr	x20, [x0,1048]
	mov	w0, w26
	bl	nandc_cs
	cbnz	w25, .L2330
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2330:
	ubfiz	x0, x26, 8, 8
	add	x1, x20, x0
	mov	w0, 5
	adrp	x20, .LANCHOR2
	str	w0, [x1,2056]
	add	x0, x20, :lo12:.LANCHOR2
	str	wzr, [x1,2052]
	str	wzr, [x1,2052]
	ldrb	w0, [x0,20]
	cmp	w0, 3
	bne	.L2331
	and	w0, w19, 255
	str	w0, [x1,2052]
	lsr	w0, w19, 8
	str	w0, [x1,2052]
	lsr	w0, w19, 16
	str	w0, [x1,2052]
.L2331:
	mov	w0, 224
	str	w0, [x1,2056]
	cbz	w25, .L2332
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0,1136]
	ldrb	w1, [x1,12]
	cmp	w1, 3
	bne	.L2332
	ldrb	w1, [x0,1268]
	cbnz	w1, .L2332
	ldrb	w0, [x0,1269]
	cbnz	w0, .L2332
	sub	w0, w25, #1
	add	w1, w19, w19, lsl 1
	add	w0, w0, w1
	b	.L2364
.L2332:
	mov	w0, w19
.L2364:
	add	x27, x20, :lo12:.LANCHOR2
	bl	nandc_set_seed
	mov	w0, w26
	mov	w1, 0
	mov	x3, x22
	mov	x4, x23
	ldrb	w2, [x27,17]
	add	x27, x27, 8
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L2334
	add	x28, x24, :lo12:.LANCHOR0
	ldrb	w5, [x28,1232]
	cbz	w5, .L2335
	ldrb	w4, [x27,9]
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	str	x5, [x29,104]
	strb	wzr, [x28,1232]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,1232]
	cbnz	w25, .L2336
.L2343:
	ldrb	w2, [x24,#:lo12:.LANCHOR0]
	add	x1, x24, :lo12:.LANCHOR0
	cbz	w2, .L2336
	ldrb	w1, [x1,1236]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2336
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w1, [x20,27]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 4
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L2351
.L2336:
	cmn	w0, #1
	bne	.L2351
.L2344:
	adrp	x0, .LANCHOR5+240
	ldr	x5, [x0,#:lo12:.LANCHOR5+240]
	cbnz	x5, .L2339
.L2342:
	add	x24, x24, :lo12:.LANCHOR0
	adrp	x0, .LC175
	add	x0, x0, :lo12:.LC175
	mov	w1, 0
	mov	w2, w21
	mov	w3, -1
	ldrb	w4, [x24,1232]
	bl	printk
	ldrb	w0, [x24,1184]
	cbnz	w0, .L2340
	mov	w0, -1
	b	.L2351
.L2339:
	add	x4, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x4,17]
	blr	x5
	cmn	w0, #1
	bne	.L2351
	b	.L2342
.L2340:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x20,17]
	bl	flash_ddr_tuning_read
	b	.L2351
.L2334:
	cbz	w25, .L2343
	b	.L2351
.L2335:
	cbz	w25, .L2343
	b	.L2344
.L2351:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	flash_complete_page_read, .-flash_complete_page_read
	.align	2
	.type	queue_wait_first_req_completed, %function
queue_wait_first_req_completed:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrb	w22, [x0,3363]
	cmp	w22, 255
	beq	.L2459
	add	x0, x0, 1312
	mov	w20, 0
	add	x0, x0, x22, lsl 6
	ldrb	w1, [x0,58]
	ldr	w24, [x0,40]
	sub	w2, w1, #1
	cmp	w2, 10
	bhi	.L2367
	adrp	x0, .L2369
	add	x0, x0, :lo12:.L2369
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x2, .Lrtx2369
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx2369:
	.section	.rodata
	.align	0
	.align	2
.L2369:
	.2byte	(.L2368 - .Lrtx2369) / 4
	.2byte	(.L2370 - .Lrtx2369) / 4
	.2byte	(.L2371 - .Lrtx2369) / 4
	.2byte	(.L2371 - .Lrtx2369) / 4
	.2byte	(.L2371 - .Lrtx2369) / 4
	.2byte	(.L2371 - .Lrtx2369) / 4
	.2byte	(.L2372 - .Lrtx2369) / 4
	.2byte	(.L2373 - .Lrtx2369) / 4
	.2byte	(.L2374 - .Lrtx2369) / 4
	.2byte	(.L2371 - .Lrtx2369) / 4
	.2byte	(.L2374 - .Lrtx2369) / 4
	.text
.L2368:
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2459
	add	x3, x19, :lo12:.LANCHOR0
	add	x0, x3, 1312
	add	x0, x0, x22, lsl 6
	ldr	x2, [x0,16]
	ldr	x1, [x0,8]
	cbz	x2, .L2376
	ldrb	w4, [x0,56]
	ldrb	w0, [x3,3360]
	cmp	w4, w0
	csel	x1, x1, x2, ne
.L2376:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w24
	add	x19, x19, 1312
	add	x19, x19, x22, lsl 6
	ldr	x2, [x19,24]
	bl	flash_complete_page_read
	str	w0, [x19,52]
	mov	w0, 13
	strb	w0, [x19,58]
	b	.L2460
.L2370:
	mov	w1, 64
	mov	w0, w24
	bl	flash_wait_device_ready
	add	x2, x19, :lo12:.LANCHOR0
	lsl	x4, x22, 6
	add	x3, x2, 1312
	add	x1, x3, x4
	ldrb	w20, [x3,x4]
	tbz	x0, 6, .L2459
	add	x3, x3, x20, lsl 6
	ldr	x0, [x1,16]
	ldr	x27, [x1,8]
	ldr	x24, [x3,8]
	cbz	x0, .L2377
	ldrb	w3, [x1,56]
	ldrb	w1, [x2,3360]
	cmp	w3, w1
	csel	x27, x27, x0, ne
.L2377:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 1312
	add	x0, x0, x20, lsl 6
	ldr	x1, [x0,16]
	cbz	x1, .L2378
	ldrb	w3, [x0,56]
	ldrb	w0, [x2,3360]
	cmp	w3, w0
	csel	x24, x24, x1, ne
.L2378:
	add	x21, x19, :lo12:.LANCHOR0
	mov	x1, x27
	add	x21, x21, 1312
	add	x26, x21, x22, lsl 6
	add	x21, x21, x20, lsl 6
	add	x28, x26, 32
	add	x26, x26, 16
	ldr	x2, [x26,8]
	ldr	w0, [x26,24]
	bl	flash_complete_plane_page_read
	mov	w23, w0
	ldr	x2, [x21,24]
	mov	x1, x24
	ldr	w0, [x21,40]
	bl	flash_complete_plane_page_read
	mov	w25, w0
	cmn	w23, #1
	beq	.L2379
	ldr	w0, [x28,4]
	cmn	w0, #1
	beq	.L2380
	ldr	x1, [x26,8]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2380
.L2379:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x21, x4, 1312
	mov	w3, 24
	add	x21, x21, x22, lsl 6
	ldrb	w2, [x4,1257]
	add	x26, x21, 32
	ldrb	w4, [x4,3360]
	add	x21, x21, 16
	ldr	w1, [x21,24]
	lsl	w0, w0, w2
	sub	w3, w3, w2
	sub	w0, w0, #1
	lsr	w2, w1, w3
	lsl	w3, w0, w3
	bic	w1, w1, w3
	ldr	x3, [x21,8]
	and	w0, w0, w2
	mov	x2, x27
	bl	flash_read_page_en
	mov	w23, w0
	ldr	w2, [x26,4]
	cmn	w2, #1
	beq	.L2381
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2381
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2381
	adrp	x0, .LC176
	ldr	w1, [x26,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC176
	bl	printk
.L2381:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x0, x0, x22, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2380
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2380
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 430
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2380:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x3, x3, 1312
	cmn	w25, #1
	add	x22, x3, x22, lsl 6
	ldrb	w0, [x22,2]
	str	w23, [x22,52]
	orr	w0, w0, 8
	strb	w1, [x22,58]
	strb	w0, [x22,2]
	beq	.L2382
	add	x3, x3, x20, lsl 6
	ldr	w0, [x3,36]
	cmn	w0, #1
	beq	.L2384
	ldr	x1, [x3,24]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2384
.L2382:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w2, 24
	add	x21, x4, 1312
	mov	w0, 1
	add	x21, x21, x20, lsl 6
	ldrb	w3, [x4,1257]
	add	x22, x21, 32
	ldrb	w4, [x4,3360]
	add	x21, x21, 16
	ldr	w1, [x21,24]
	sub	w2, w2, w3
	lsl	w0, w0, w3
	sub	w0, w0, #1
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	and	w0, w0, w3
	ldr	x3, [x21,8]
	bic	w1, w1, w2
	mov	x2, x24
	bl	flash_read_page_en
	ldr	w2, [x22,4]
	cmn	w2, #1
	beq	.L2386
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2386
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2386
	adrp	x0, .LC176
	ldr	w1, [x22,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC176
	bl	printk
.L2386:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2384
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2384
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 449
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2384:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x19, x19, 1312
	add	x19, x19, x20, lsl 6
	str	w23, [x19,52]
	strb	w1, [x19,58]
.L2460:
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2459
.L2371:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L2459
	mov	w20, 5
	ands	w20, w21, w20
	beq	.L2387
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 12
	add	x19, x19, 1312
	mov	w2, w21
	add	x19, x19, x22, lsl 6
	mov	w4, 12
	mov	w20, w21
	strb	w0, [x19,58]
	adrp	x0, .LC177
	ldrb	w1, [x19,1]
	add	x0, x0, :lo12:.LC177
	ldr	w3, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2367
.L2387:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x23, x4, 1312
	add	x23, x23, x22, lsl 6
	strb	w1, [x23,58]
	ldr	x1, [x4,3392]
	str	w20, [x23,52]
	ldr	w2, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2459
	ldrh	w0, [x23,50]
	cbnz	w0, .L2459
	ldrb	w3, [x4,1257]
	mov	w0, 1
	adrp	x21, .LANCHOR5
	ldr	w1, [x23,40]
	lsl	w0, w0, w3
	add	x21, x21, :lo12:.LANCHOR5
	mov	w2, 24
	sub	w0, w0, #1
	sub	w2, w2, w3
	ldrb	w4, [x4,3360]
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	bic	w1, w1, w2
	and	w0, w0, w3
	ldr	x2, [x21,248]
	ldr	x3, [x21,256]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2388
	ldr	x1, [x23,24]
	ldr	x0, [x21,256]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2459
.L2388:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC178
	add	x19, x4, 1312
	add	x0, x0, :lo12:.LC178
	add	x19, x19, x22, lsl 6
	ldrb	w4, [x4,1232]
	ldrb	w1, [x19,1]
	ldr	w2, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2367
.L2374:
	add	x25, x19, :lo12:.LANCHOR0
	cmp	w1, 11
	mov	w0, 3
	mov	w5, 10
	csel	w5, w0, w5, ne
	mov	w4, 24
	ldrb	w1, [x25,1257]
	mov	w0, 1
	add	x21, x25, 1312
	sub	w4, w4, w1
	lsl	w0, w0, w1
	sub	w0, w0, #1
	lsr	w2, w24, w4
	and	w2, w0, w2
	add	x3, x21, x22, lsl 6
	uxth	w2, w2
.L2390:
	ldrb	w1, [x3]
	cmp	w1, 255
	beq	.L2461
	sxtw	x23, w1
	add	x3, x21, x23, lsl 6
	ldrb	w1, [x3,58]
	cmp	w1, w5
	bne	.L2390
	ldr	w1, [x3,40]
	lsr	w1, w1, w4
	and	w1, w0, w1
	cmp	w2, w1
	bne	.L2390
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	tbz	x20, 6, .L2458
	ands	w1, w20, 15
	beq	.L2394
	add	x21, x21, x22, lsl 6
	adrp	x0, .LC179
	add	x0, x0, :lo12:.LC179
	mov	w2, w20
	mov	w4, 12
	ldrb	w1, [x21,1]
	ldr	w3, [x21,40]
	bl	printk
	mov	w0, 12
	strb	w0, [x21,58]
	mov	w0, -1
	str	w0, [x21,52]
	b	.L2393
.L2394:
	add	x20, x21, x22, lsl 6
	mov	w2, 13
	str	w1, [x20,52]
	ldr	x1, [x25,3392]
	strb	w2, [x20,58]
	ldr	w2, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2458
	ldrh	w0, [x20,50]
	cbnz	w0, .L2458
	ldrb	w3, [x25,1257]
	mov	w0, 1
	adrp	x21, .LANCHOR5
	ldr	w1, [x20,40]
	lsl	w0, w0, w3
	add	x21, x21, :lo12:.LANCHOR5
	mov	w2, 24
	sub	w0, w0, #1
	sub	w2, w2, w3
	ldrb	w4, [x25,3360]
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	bic	w1, w1, w2
	and	w0, w0, w3
	ldr	x2, [x21,248]
	ldr	x3, [x21,256]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2396
	ldr	x1, [x20,24]
	ldr	x0, [x21,256]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2458
.L2396:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC180
	add	x20, x4, 1312
	add	x0, x0, :lo12:.LC180
	add	x20, x20, x22, lsl 6
	ldrb	w4, [x4,1232]
	ldrb	w1, [x20,1]
	ldr	w2, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
.L2458:
	mov	w20, 0
.L2393:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 1312
	add	x19, x1, x22, lsl 6
	add	x1, x1, x23, lsl 6
	ldrb	w0, [x19,58]
	strb	w0, [x1,58]
	ldr	w0, [x19,52]
	str	w0, [x1,52]
	b	.L2367
.L2461:
	mov	w20, -1
	b	.L2367
.L2372:
	mov	w1, 32
	mov	w0, w24
	bl	flash_wait_device_ready
	tbz	x0, 5, .L2459
	ands	w20, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	beq	.L2398
	mov	w1, 12
	mov	w20, w0
	strb	w1, [x19,1370]
	mov	w1, -1
	str	w1, [x19,1364]
	b	.L2367
.L2398:
	mov	w0, 13
	str	w20, [x19,1364]
	strb	w0, [x19,1370]
	b	.L2367
.L2373:
	mov	w1, 64
	mov	w0, w24
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2459
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	str	w0, [x19,1364]
	mov	w0, 7
	strb	w0, [x19,1370]
.L2459:
	mov	w20, 0
.L2367:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_prog_page
	.type	sblk_prog_page, %function
sblk_prog_page:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	uxtb	w20, w1
	ldrh	w0, [x0,50]
	cbz	w0, .L2463
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2463
	adrp	x0, .LC181
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC181
	mov	w2, w20
	bl	printk
.L2463:
	adrp	x23, .LANCHOR0
	adrp	x25, .LC0
	add	x24, x23, :lo12:.LANCHOR0
	mov	w21, 0
	add	x27, x24, 1312
	add	x25, x25, :lo12:.LC0
.L2464:
	cbz	w20, .L2497
	ldrb	w26, [x19]
	ldr	w22, [x19,40]
.L2465:
	mov	w0, w22
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2498
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2465
.L2498:
	cmp	w20, 1
	beq	.L2467
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,1305]
	cbz	w1, .L2467
	ldrb	w1, [x0,1269]
	cbz	w1, .L2468
.L2467:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L2469
.L2468:
	ldrb	w1, [x0,1257]
	mov	w28, 1
	mov	w0, 24
	lsl	w28, w28, w1
	sub	w0, w0, w1
	lsr	w0, w22, w0
	sub	w28, w28, #1
	and	w28, w28, w0
	ldrb	w0, [x19]
	cmp	w0, 255
	uxth	w28, w28
	bne	.L2470
	adrp	x1, .LANCHOR4
	mov	w2, 696
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x25
	add	x1, x1, 504
	bl	printk
	bl	dump_stack
.L2470:
	ldrb	w0, [x19]
	mov	w4, 1
	ldrb	w1, [x24,1257]
	add	x0, x27, x0, lsl 6
	ldr	w2, [x0,40]
	mov	w0, 24
	sub	w0, w0, w1
	lsl	w1, w4, w1
	sub	w1, w1, #1
	lsr	w3, w2, w0
	and	w1, w1, w3
	cmp	w28, w1, uxth
	bne	.L2471
	adrp	x5, .LANCHOR3
	str	x4, [x29,104]
	add	x5, x5, :lo12:.LANCHOR3
	ldrh	w1, [x5,1232]
	ldrb	w6, [x5,1234]
	sub	w3, w0, w1
	lsr	w5, w22, w1
	lsl	w3, w4, w3
	sub	w6, w6, #1
	sub	w3, w3, #1
	lsl	w0, w4, w1
	uxth	w7, w6
	lsr	w1, w2, w1
	uxth	w3, w3
	and	w5, w7, w5
	and	w1, w7, w1
	and	w5, w3, w5
	and	w3, w3, w1
	sub	w0, w0, #1
	cmp	w5, w3
	uxth	w0, w0
	beq	.L2471
	and	w22, w0, w22
	and	w0, w0, w2
	cmp	w22, w0
	bne	.L2471
	cmp	w21, w6
	beq	.L2471
	ldr	w1, [x19,40]
	mov	w0, 17
	ldr	x2, [x19,8]
	add	w21, w21, 1
	ldr	x3, [x19,24]
	bl	flash_start_page_prog
	mov	w0, 9
	ldr	x4, [x29,104]
	strb	w0, [x19,58]
	mov	w0, -1
	strb	w4, [x19,59]
	mov	x1, x19
	strb	w0, [x19]
	add	x0, x24, 3363
	bl	buf_add_tail
	b	.L2469
.L2471:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
.L2469:
	subs	w20, w20, #1
	beq	.L2497
	add	x19, x23, :lo12:.LANCHOR0
	sbfiz	x26, x26, 6, 32
	add	x19, x19, 1312
	add	x19, x19, x26
	b	.L2464
.L2497:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	sblk_prog_page, .-sblk_prog_page
	.align	2
	.global	sblk_wait_write_queue_completed
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
.L2500:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3363]
	cmp	w0, 255
	beq	.L2502
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2500
.L2502:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
	.align	2
	.global	ftl_flush
	.type	ftl_flush, %function
ftl_flush:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0
	add	x29, sp, 0
	add	x0, x2, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	mov	x19, x2
	adrp	x20, .LANCHOR5
	ldrb	w1, [x0,3389]
	cbz	w1, .L2504
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 1312
	ldrb	w2, [x2,264]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2504:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2,3389]
	strb	w0, [x20,264]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	w0, -1
	bl	ftl_vpn_decrement
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_flush, .-ftl_flush
	.align	2
	.global	zftl_cache_flush
	.type	zftl_cache_flush, %function
zftl_cache_flush:
	adrp	x0, .LANCHOR0+3389
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+3389]
	cbz	w0, .L2508
	bl	timer_get_time
	adrp	x1, .LANCHOR5+268
	ldr	w1, [x1,#:lo12:.LANCHOR5+268]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2508
	bl	ftl_flush
.L2508:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	ftl_read_page
	.type	ftl_read_page, %function
ftl_read_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	str	x1, [x29,32]
	str	x2, [x29,40]
	str	x3, [x29,48]
	str	x4, [x29,56]
	bl	sblk_wait_write_queue_completed
	ldr	x1, [x29,32]
	mov	w0, w19
	ldr	x2, [x29,40]
	ldr	x3, [x29,48]
	ldr	x4, [x29,56]
	bl	flash_read_page_en
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_read_page, .-ftl_read_page
	.align	2
	.global	ftl_read_ppa_page
	.type	ftl_read_ppa_page, %function
ftl_read_ppa_page:
	stp	x29, x30, [sp, -64]!
	mov	w5, 1
	mov	w4, w3
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	adrp	x0, .LANCHOR0+1257
	mov	w19, 24
	str	x4, [x29,56]
	stp	x21, x22, [sp,32]
	ldrb	w0, [x0,#:lo12:.LANCHOR0+1257]
	mov	x22, x1
	mov	x21, x2
	sub	w19, w19, w0
	lsl	w5, w5, w0
	sub	w5, w5, #1
	lsr	w19, w20, w19
	and	w19, w19, w5
	bl	sblk_wait_write_queue_completed
	ldr	x4, [x29,56]
	uxtb	w19, w19
	mov	w1, w20
	mov	x2, x22
	mov	x3, x21
	mov	w0, w19
	bl	flash_read_page_en
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_read_ppa_page, .-ftl_read_ppa_page
	.align	2
	.global	sblk_read_page
	.type	sblk_read_page, %function
sblk_read_page:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	add	x26, x25, :lo12:.LANCHOR0
	uxtb	w21, w1
	adrp	x28, .LC0
	add	x27, x26, 1312
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	x19, x0
	mov	x23, x0
	mov	w20, w21
	add	x28, x28, :lo12:.LC0
.L2515:
	cbz	w20, .L2538
	ldrb	w24, [x19]
	ldr	w22, [x19,40]
.L2516:
	mov	w0, w22
	mov	w1, 0
	bl	queue_lun_state
	cbz	w0, .L2539
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2516
.L2539:
	cmp	w20, 1
	beq	.L2522
	adrp	x0, .LANCHOR5+272
	ldrb	w0, [x0,#:lo12:.LANCHOR5+272]
	cbz	w0, .L2522
	add	x0, x25, :lo12:.LANCHOR0
	mov	w3, 1
	ldrb	w1, [x0,1257]
	mov	w0, 24
	lsl	w3, w3, w1
	sub	w0, w0, w1
	lsr	w0, w22, w0
	sub	w3, w3, #1
	and	w3, w3, w0
	ldrb	w0, [x19]
	cmp	w0, 255
	uxth	w3, w3
	bne	.L2521
	adrp	x1, .LANCHOR4
	mov	w2, 781
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x28
	add	x1, x1, 520
	str	x3, [x29,120]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,120]
.L2521:
	ldrb	w4, [x19]
	mov	w5, 1
	ldrb	w0, [x26,1257]
	mov	w2, 24
	lsl	x4, x4, 6
	add	x6, x27, x4
	lsl	w5, w5, w0
	sub	w0, w2, w0
	sub	w5, w5, #1
	ldr	w1, [x6,40]
	lsr	w0, w1, w0
	and	w0, w5, w0
	cmp	w3, w0, uxth
	bne	.L2522
	adrp	x0, .LANCHOR3+1330
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1330]
	add	w22, w0, w22
	cmp	w1, w22
	bne	.L2522
	ldr	w0, [x19,40]
	add	x22, x26, 3363
	str	x6, [x29,96]
	sub	w20, w20, #1
	ldrb	w24, [x27,x4]
	str	x4, [x29,104]
	bl	flash_start_plane_read
	mov	w3, 2
	mov	w2, -1
	strb	w3, [x19,58]
	mov	x0, x22
	strb	w2, [x19]
	mov	x1, x19
	strb	wzr, [x19,59]
	str	x3, [x29,112]
	str	x2, [x29,120]
	bl	buf_add_tail
	ldr	x6, [x29,96]
	mov	x0, x22
	ldr	x4, [x29,104]
	ldr	x3, [x29,112]
	mov	x1, x6
	ldr	x2, [x29,120]
	strb	w3, [x6,58]
	strb	wzr, [x6,59]
	strb	w2, [x27,x4]
	bl	buf_add_tail
	b	.L2520
.L2522:
	mov	x0, x19
	bl	queue_read_cmd
.L2520:
	subs	w20, w20, #1
	beq	.L2538
	add	x19, x25, :lo12:.LANCHOR0
	sbfiz	x24, x24, 6, 32
	add	x19, x19, 1312
	add	x19, x19, x24
	b	.L2515
.L2538:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 1312
.L2526:
	cbz	w21, .L2540
	ldrb	w0, [x23,58]
	cmp	w0, 13
	bne	.L2527
	ldrb	w0, [x23]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2527
	sbfiz	x23, x0, 6, 32
	add	x23, x19, x23
.L2527:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2526
.L2540:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	sblk_read_page, .-sblk_read_page
	.align	2
	.global	gc_check_data_one_wl
	.type	gc_check_data_one_wl, %function
gc_check_data_one_wl:
	sub	sp, sp, #112
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x20,3432]
	ldr	x21, [x20,1120]
	add	x20, x20, 3424
	cbnz	x0, .L2542
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L2542:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,3432]
	cbnz	x20, .L2543
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 729
	add	x1, x1, 536
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2543:
	mov	w24, 0
	add	x25, x21, 80
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x27, .LANCHOR3
	adrp	x26, .LANCHOR5
.L2544:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L2575
	mov	w23, 1
	sxtw	x28, w24
.L2555:
	add	x2, x22, 3424
	ldrh	w0, [x2,20]
	cmp	w23, w0
	bgt	.L2576
	add	x0, x28, 8
	ldrh	w1, [x25,x0,lsl 1]
	add	x0, x27, :lo12:.LANCHOR3
	ldrh	w3, [x0,1330]
	ldrb	w0, [x0,1248]
	cmp	w0, 2
	mul	w1, w1, w3
	beq	.L2545
	ldrb	w3, [x22,1268]
	cbz	w3, .L2546
.L2545:
	ldrh	w2, [x22,3440]
	sub	w2, w2, #1
	add	w1, w2, w1
	add	w1, w1, w23
	orr	w1, w1, w0, lsl 24
	b	.L2574
.L2546:
	cmp	w0, 3
	bne	.L2548
	ldrb	w0, [x22,1269]
	cbz	w0, .L2549
	ldrh	w0, [x2,16]
	add	w0, w0, w0, lsl 1
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w23
	orr	w1, w1, 50331648
	b	.L2574
.L2549:
	ldrh	w0, [x2,16]
	add	w1, w1, w0
	orr	w1, w1, w23, lsl 24
	b	.L2574
.L2548:
	ldrh	w0, [x2,16]
	add	w1, w1, w0
.L2574:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L2550
	add	x3, x26, :lo12:.LANCHOR5
	ldrh	w0, [x22,3446]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,144]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L2550
	ldr	x3, [x3,152]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L2551
.L2550:
	add	x0, x26, :lo12:.LANCHOR5
	ldrh	w4, [x22,3446]
	lsl	x4, x4, 2
	ldr	x1, [x0,144]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L2551
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L2552
	ldr	x7, [x20,24]
	ldr	x5, [x0,152]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC182
	add	x0, x0, :lo12:.LC182
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L2552:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x21,80]
	mov	w21, -1
	ldr	x0, [x19,1112]
	strh	wzr, [x0,x1,lsl 1]
	ldr	x1, [x19,1120]
	ldr	w0, [x1,556]
	add	w0, w0, 1
	str	w0, [x1,556]
	ldr	x0, [x19,3392]
	ldr	w1, [x0,156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	bne	.L2553
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w1, [x0,1868]
	cbnz	w1, .L2553
	ldrb	w0, [x0,1250]
	cbnz	w0, .L2553
	ldr	w0, [x20,40]
	bl	ftl_mask_bad_block
	b	.L2553
.L2551:
	ldrh	w0, [x22,3446]
	add	w23, w23, 1
	add	w0, w0, 1
	strh	w0, [x22,3446]
	b	.L2555
.L2576:
	add	w24, w24, 1
	b	.L2544
.L2575:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w21, 0
	add	x19, x19, 3424
	ldrh	w0, [x19,16]
	add	w1, w0, 1
	strh	w1, [x19,16]
	adrp	x1, .LANCHOR3+1250
	ldrb	w1, [x1,#:lo12:.LANCHOR3+1250]
	cbz	w1, .L2553
	add	w0, w0, 2
	strh	w0, [x19,16]
.L2553:
	sub	sp, x29, #16
	mov	w0, w21
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 112
	ret
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	sblk_tlc_prog_one_page
	.type	sblk_tlc_prog_one_page, %function
sblk_tlc_prog_one_page:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	ldr	x0, [x0]
	ldr	w21, [x0,40]
.L2578:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2580
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2578
.L2580:
	mov	x0, x20
	mov	w1, 1
	bl	queue_tlc_prog_cmd
	ldr	x21, [sp,32]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	sblk_tlc_prog_one_page, .-sblk_tlc_prog_one_page
	.align	2
	.global	sblk_xlc_prog_pages
	.type	sblk_xlc_prog_pages, %function
sblk_xlc_prog_pages:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	str	x27, [sp,80]
	mov	x21, x0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x22, x1
	mov	w27, w2
	ldr	x0, [x0]
	ldr	w19, [x0,40]
.L2582:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	mov	w24, w0
	cbz	w0, .L2593
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2582
.L2593:
	cmp	w27, 2
	bne	.L2584
	adrp	x25, .LANCHOR0
	add	x2, x25, :lo12:.LANCHOR0
	ldrb	w0, [x2,1305]
	cbz	w0, .L2585
	ldrb	w0, [x2,1257]
	mov	w1, 24
	ldr	x6, [x21]
	mov	w20, 1
	sub	w1, w1, w0
	ldr	x4, [x22]
	lsl	w23, w20, w1
	lsl	w0, w20, w0
	ldr	w3, [x6,40]
	sub	w23, w23, #1
	sub	w19, w0, #1
	ldrb	w0, [x2,1269]
	and	w26, w23, w3
	lsr	w3, w3, w1
	ldr	w4, [x4,40]
	and	w19, w19, w3
	and	w23, w23, w4
	uxtb	w19, w19
	cbz	w0, .L2586
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x6, [x21]
	mov	w1, w20
	mov	w3, w19
	mov	w4, w26
	mov	w0, w24
	mov	w2, 17
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22]
	mov	w1, w20
	mov	w3, w19
	mov	w4, w23
	mov	w0, w24
	mov	w2, 26
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,8]
	mov	w1, w27
	mov	w3, w19
	add	w4, w26, w20
	mov	w0, w24
	mov	w2, 17
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,8]
	mov	w1, w27
	mov	w3, w19
	add	w4, w23, w20
	mov	w0, w24
	mov	w2, 26
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,16]
	mov	w3, w19
	add	w4, w26, 2
	mov	w0, w24
	mov	w1, 3
	mov	w2, 17
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,16]
	mov	w0, w24
	mov	w1, 3
	mov	w2, 16
	mov	w3, w19
	add	w4, w23, 2
	ldr	x5, [x6,8]
	ldr	x6, [x6,24]
	bl	flash_start_one_pass_page_prog
	b	.L2587
.L2586:
	ldr	x5, [x6,8]
	mov	w1, w20
	ldrb	w0, [x6,60]
	mov	w2, 17
	ldr	x6, [x6,24]
	mov	w3, w19
	mov	w4, w26
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22]
	mov	w1, w20
	ldr	x0, [x21]
	mov	w2, 26
	mov	w3, w19
	mov	w4, w23
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,8]
	mov	w1, w27
	ldr	x0, [x21]
	mov	w2, 17
	mov	w3, w19
	mov	w4, w26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,8]
	mov	w1, w27
	ldr	x0, [x21]
	mov	w2, 26
	mov	w3, w19
	mov	w4, w23
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,16]
	mov	w1, 3
	ldr	x0, [x21]
	mov	w2, 17
	mov	w3, w19
	mov	w4, w26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,16]
	mov	w1, 3
	ldr	x0, [x21]
	mov	w2, 16
	mov	w3, w19
	mov	w4, w23
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
.L2587:
	ldr	x1, [x21]
	mov	w0, 5
	strb	w0, [x1,58]
	mov	w0, 1
	strb	w0, [x1,59]
	mov	w0, -1
	strb	w0, [x1]
	add	x0, x25, :lo12:.LANCHOR0
	add	x0, x0, 3363
	bl	buf_add_tail
	b	.L2588
.L2585:
	ldr	x0, [x22]
	ldr	w19, [x0,40]
.L2589:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	mov	w20, w0
	cbz	w0, .L2594
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2589
.L2594:
	mov	x0, x21
	mov	w1, 1
	bl	queue_tlc_prog_cmd
	mov	x0, x22
	mov	w1, w20
	bl	queue_tlc_prog_cmd
.L2591:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2588
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2591
.L2584:
	mov	x0, x21
	mov	w1, 1
	bl	queue_tlc_prog_cmd
.L2588:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	sblk_3d_mlc_prog_pages
	.type	sblk_3d_mlc_prog_pages, %function
sblk_3d_mlc_prog_pages:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	str	x27, [sp,80]
	add	x23, x23, :lo12:.LANCHOR0
	mov	w24, 1
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x20, x0
	mov	w22, w1
	mov	w25, 24
	mov	w26, 4
	add	x27, x23, 3363
.L2596:
	cbz	w22, .L2600
	ldr	x0, [x20]
	ldr	w19, [x0,40]
.L2597:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2601
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2597
.L2601:
	ldr	x0, [x20]
	sub	w22, w22, #1
	ldr	w3, [x0,40]
	ldrb	w0, [x23,1257]
	sub	w1, w25, w0
	lsl	w0, w24, w0
	lsl	w21, w24, w1
	sub	w19, w0, #1
	sub	w21, w21, #1
	and	w21, w21, w3
	lsr	w3, w3, w1
	and	w19, w19, w3
	uxtb	w19, w19
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x4, [x20]
	mov	w1, w19
	mov	w2, w21
	mov	w0, 16
	ldr	x3, [x4,8]
	ldr	x4, [x4,24]
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x4, [x20,8]
	mov	w1, w19
	add	w2, w21, 1
	mov	w0, 16
	ldr	x3, [x4,8]
	ldr	x4, [x4,24]
	bl	flash_start_3d_mlc_page_prog
	mov	w0, w19
	bl	nandc_de_cs
	ldr	x1, [x20],16
	mov	w0, -1
	strb	w26, [x1,58]
	strb	w0, [x1]
	mov	x0, x27
	strb	w24, [x1,59]
	bl	buf_add_tail
	b	.L2596
.L2600:
	mov	w0, w22
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	sblk_3d_mlc_prog_pages, .-sblk_3d_mlc_prog_pages
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w20, w1
	add	x1, x0, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	mov	x23, x2
	mov	x22, x3
	ldrb	w1, [x1,1145]
	mov	w27, w4
	uxtb	w28, w5
	ubfx	x25, x20, 24, 2
	cmp	w1, w21
	mov	x24, x0
	bhi	.L2603
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 635
	add	x1, x1, 560
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2603:
	add	x1, x24, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1145]
	cmp	w2, w21
	bls	.L2604
	add	x0, x1, x21, sxtw
	mov	w19, w20
	ldrb	w26, [x0,1248]
	cbnz	w25, .L2605
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L2606
	ldrb	w0, [x1,1]
	cbz	w0, .L2605
.L2606:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w19, [x0,2]
	ldrb	w2, [x0,1]
	udiv	w1, w20, w19
	mul	w19, w1, w19
	sub	w1, w20, w19
	cbz	w2, .L2607
	add	w19, w19, w1, lsl 1
	b	.L2605
.L2607:
	add	x0, x0, 4
	ldrh	w0, [x0,w1,uxtw 1]
	add	w19, w0, w19
.L2605:
	adrp	x25, .LC183
	adrp	x24, .LC184
	adrp	x0, .LC185
	add	x25, x25, :lo12:.LC183
	add	x0, x0, :lo12:.LC185
	add	x24, x24, :lo12:.LC184
	str	x0, [x29,120]
.L2611:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2608
	mov	x0, x25
	mov	w1, w26
	mov	w2, w20
	mov	w3, w19
	bl	printk
.L2608:
	mov	w0, w26
	mov	w1, w19
	mov	x2, x23
	mov	x3, x22
	mov	w4, w27
	bl	flash_prog_page
	mov	w5, w0
	cbz	w28, .L2609
	adrp	x6, .LANCHOR5
	mov	w4, w27
	add	x7, x6, :lo12:.LANCHOR5
	mov	w0, w21
	mov	w1, w20
	str	x6, [x29,104]
	str	x5, [x29,96]
	ldr	x2, [x7,248]
	ldr	x3, [x7,256]
	str	x7, [x29,112]
	bl	flash_read_page_en
	cmp	w0, 512
	cset	w4, eq
	ldr	x6, [x29,104]
	cbnz	w4, .L2610
	cmn	w0, #1
	beq	.L2610
	ldr	x7, [x29,112]
	ldr	w1, [x23]
	ldr	x5, [x29,96]
	ldr	x0, [x7,248]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2610
	ldr	x0, [x7,256]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2609
.L2610:
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	mov	x1, x23
	str	x4, [x29,104]
	str	x6, [x29,112]
	bl	rknand_print_hex
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	mov	x1, x22
	bl	rknand_print_hex
	ldr	x6, [x29,112]
	mov	w2, 4
	adrp	x0, .LC186
	mov	w3, w2
	add	x6, x6, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC186
	str	x6, [x29,112]
	ldr	x1, [x6,256]
	bl	rknand_print_hex
	ldr	x6, [x29,112]
	mov	w2, 4
	adrp	x0, .LC187
	mov	w3, w2
	add	x0, x0, :lo12:.LC187
	ldr	x1, [x6,248]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
	cbnz	w4, .L2611
.L2613:
	adrp	x0, .LC188
	mov	w1, w20
	add	x0, x0, :lo12:.LC188
	bl	printk
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 678
	add	x1, x1, 560
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, -1
	b	.L2604
.L2609:
	cmn	w5, #1
	mov	w0, w5
	beq	.L2613
.L2604:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_prog_page
	.type	ftl_prog_page, %function
ftl_prog_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	mov	w20, w1
	str	x2, [x29,40]
	str	x3, [x29,48]
	str	x4, [x29,56]
	bl	sblk_wait_write_queue_completed
	ldr	x2, [x29,40]
	mov	w0, w19
	ldr	x3, [x29,48]
	mov	w1, w20
	ldr	x4, [x29,56]
	mov	w5, 1
	bl	flash_prog_page_en
	cmn	w0, #1
	mov	w19, w0
	bne	.L2632
	adrp	x2, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 584
	mov	w2, 2670
	bl	printk
	bl	dump_stack
	adrp	x0, .LC188
	mov	w1, w20
	add	x0, x0, :lo12:.LC188
	bl	printk
.L2632:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_prog_page, .-ftl_prog_page
	.align	2
	.global	ftl_info_flush
	.type	ftl_info_flush, %function
ftl_info_flush:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	str	w0, [x29,124]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR5
	ldrb	w2, [x19,3360]
	add	x0, x21, :lo12:.LANCHOR5
	add	x0, x0, 320
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19,3392]
	ldrh	w1, [x0,74]
	cbz	w1, .L2634
	strh	wzr, [x0,150]
.L2634:
	adrp	x23, .LANCHOR4
	adrp	x24, .LC189
	add	x23, x23, :lo12:.LANCHOR4
	mov	w25, 0
	add	x26, x21, :lo12:.LANCHOR5
	add	x24, x24, :lo12:.LC189
	add	x23, x23, 600
.L2645:
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x0, x22, :lo12:.LANCHOR3
	add	x3, x26, 320
	str	x2, [x29,112]
	ldr	x1, [x2,3392]
	ldrh	w28, [x0,1330]
	str	x3, [x29,104]
	ldr	w0, [x1,4]
	ldrb	w19, [x26,576]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldr	w0, [x29,124]
	str	w0, [x26,320]
	ldr	x0, [x2,3392]
	ldrb	w1, [x2,3360]
	ldrh	w27, [x26,578]
	ldr	w0, [x0,4]
	lsl	w1, w1, 9
	str	w0, [x26,324]
	ldr	x0, [x26,584]
	bl	js_hash
	ldr	x3, [x29,104]
	ldr	x2, [x29,112]
	str	w0, [x3,8]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2635
	ldr	x3, [x2,3392]
	mov	x0, x24
	ldrb	w1, [x26,576]
	ldrh	w2, [x26,578]
	ldr	w3, [x3,4]
	bl	printk
.L2635:
	add	x1, x22, :lo12:.LANCHOR3
	ldrh	w0, [x26,578]
	ldrh	w1, [x1,1294]
	cmp	w1, w0
	bhi	.L2636
	adrp	x19, .LC0
	add	x19, x19, :lo12:.LC0
.L2641:
	ldrb	w1, [x26,577]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x26,577]
	cmp	w1, 7
	bls	.L2637
	mov	x1, 0
.L2640:
	add	x0, x20, :lo12:.LANCHOR0
	uxth	w25, w1
	ldr	x2, [x0,1240]
	add	w0, w1, 8
	add	x0, x2, x0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2638
	mov	x1, x23
	mov	w2, 841
	mov	x0, x19
	bl	printk
	bl	dump_stack
	b	.L2639
.L2638:
	cmp	w2, 255
	bne	.L2639
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2640
	mov	w25, w1
.L2639:
	strb	w25, [x26,577]
	mov	w25, 1
.L2637:
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w1, [x26,577]
	ldr	x0, [x28,1240]
	add	x1, x0, x1
	ldrb	w1, [x1,40]
	strb	w1, [x26,576]
	cmp	w1, 255
	beq	.L2641
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w19, [x0,1330]
	mov	w0, 0
	mul	w19, w1, w19
	mov	w1, w19
	bl	flash_erase_block
	ldr	x2, [x26,584]
	mov	w1, w19
	ldrb	w4, [x28,3360]
	mov	w0, 0
	add	x3, x26, 320
	add	w19, w19, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x26,578]
	b	.L2642
.L2636:
	madd	w19, w19, w28, w27
	cbnz	w0, .L2642
	mov	w1, w19
	bl	flash_erase_block
.L2642:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	x2, [x26,584]
	mov	w1, w19
	mov	w0, 0
	add	x3, x26, 320
	ldrb	w4, [x4,3360]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x26,578]
	add	w1, w1, 1
	strh	w1, [x26,578]
	beq	.L2643
	ldrb	w0, [x26,592]
	cbz	w0, .L2644
.L2643:
	strb	wzr, [x26,592]
	b	.L2645
.L2644:
	cbnz	w25, .L2646
.L2654:
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w0, [x21,576]
	cmp	w0, 255
	bne	.L2648
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 885
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2648
.L2646:
	ldrb	w19, [x26,577]
	adrp	x23, .LANCHOR4
	adrp	x24, .LC0
	add	x23, x23, :lo12:.LANCHOR4
	add	w19, w19, 1
	add	x24, x24, :lo12:.LC0
	add	x23, x23, 600
.L2649:
	cmp	w19, 7
	bhi	.L2654
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0,1240]
	add	w0, w19, 8
	add	x0, x1, x0, sxtw
	ldrb	w25, [x0,32]
	add	w0, w25, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2650
	mov	x1, x23
	mov	w2, 876
	mov	x0, x24
	bl	printk
	bl	dump_stack
	b	.L2651
.L2650:
	cmp	w25, 255
	beq	.L2652
.L2651:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w1, [x0,1330]
	mov	w0, 0
	mul	w1, w25, w1
	bl	flash_erase_block
.L2652:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2649
.L2648:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	stp	x29, x30, [sp, -112]!
	mov	w0, 1
	mov	w2, 16384
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp,32]
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR5
	add	x21, x22, :lo12:.LANCHOR5
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	strb	w0, [x21,592]
	adrp	x25, .LC190
	ldrh	w1, [x19,1088]
	adrp	x27, .LANCHOR3
	strb	w0, [x21,594]
	add	x26, x21, 320
	ldr	x0, [x21,584]
	adrp	x28, .LANCHOR2
	str	x0, [x19,1096]
	add	x25, x25, :lo12:.LC190
	add	x1, x0, x1, lsl 2
	str	x1, [x19,3392]
	mov	w1, 0
	strb	wzr, [x21,593]
	bl	ftl_memset
	ldr	x0, [x21,600]
	mov	w1, 0
	mov	w2, 16384
	bl	ftl_memset
	strb	wzr, [x21,577]
	ldr	x0, [x19,1240]
	mov	w19, 7
	strh	wzr, [x21,578]
	ldrb	w0, [x0,40]
	strb	w0, [x21,576]
.L2675:
	add	x6, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w24, w19
	ldr	x1, [x6,1240]
	add	x0, x1, x0, sxtw
	ldrb	w0, [x0,32]
	cmp	w0, 255
	beq	.L2671
	add	x1, x27, :lo12:.LANCHOR3
	ldrb	w4, [x6,3360]
	ldr	x2, [x21,584]
	mov	x3, x26
	str	x6, [x29,104]
	ldrh	w23, [x1,1330]
	mul	w23, w0, w23
	mov	w0, 0
	mov	w1, w23
	bl	ftl_read_page
	cmn	w0, #1
	mov	w5, w0
	ldr	x6, [x29,104]
	bne	.L2672
	ldr	x2, [x21,584]
	mov	w0, 0
	ldrb	w4, [x6,3360]
	add	w1, w23, 1
	mov	x3, x26
	bl	ftl_read_page
	mov	w5, w0
.L2672:
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2673
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, w5
	mov	w1, w19
	mov	w3, 744
	str	x5, [x29,104]
	ldr	x4, [x0,3392]
	mov	x0, x25
	ldr	w4, [x4]
	bl	printk
	ldr	x5, [x29,104]
.L2673:
	cmn	w5, #1
	beq	.L2671
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2685
.L2671:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L2675
	mov	w24, 0
	b	.L2674
.L2685:
	mov	w19, w24
.L2674:
	adrp	x23, .LANCHOR2
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2676
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,3392]
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	ldr	w3, [x3]
	bl	printk
.L2676:
	cmn	w19, #1
	bne	.L2677
	add	x22, x22, :lo12:.LANCHOR5
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, 16384
	ldr	x0, [x22,584]
	bl	ftl_memset
	ldr	x1, [x20,3392]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, 35
	movk	w0, 0x6, lsl 16
	ldr	x1, [x20,3392]
	str	w0, [x1,12]
	b	.L2702
.L2677:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x22, :lo12:.LANCHOR5
	add	x19, x21, 320
	mov	w4, 4
	mov	x3, x19
	adrp	x27, .LC192
	ldr	x1, [x0,1240]
	add	w0, w24, 8
	ldr	x2, [x21,584]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	mov	w0, 0
	strb	w24, [x21,577]
	add	x27, x27, :lo12:.LC192
	strb	w1, [x21,576]
	bl	flash_get_last_written_page
	sxth	w25, w0
	add	w0, w0, 1
	ldrb	w26, [x21,576]
	uxth	w24, w0
	adrp	x0, .LANCHOR3+1330
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1330]
	mul	w26, w26, w0
	b	.L2679
.L2702:
	mov	w0, w19
	b	.L2678
.L2684:
	add	x28, x20, :lo12:.LANCHOR0
	ldr	x2, [x21,584]
	mov	w0, 0
	add	w1, w25, w26
	mov	x3, x19
	ldrb	w4, [x28,3360]
	bl	ftl_read_page
	cmn	w0, #1
	bne	.L2703
.L2680:
	sub	w25, w25, #1
	sxth	w25, w25
.L2679:
	tbz	w25, #31, .L2684
	b	.L2683
.L2703:
	ldr	x0, [x28,3392]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2680
	ldr	w2, [x19,8]
	cbnz	w2, .L2681
.L2683:
	add	x22, x22, :lo12:.LANCHOR5
	add	x20, x20, :lo12:.LANCHOR0
	strh	w24, [x22,578]
	bl	ftl_tmp_into_update
	ldr	x1, [x20,3392]
	ldr	w0, [x1,64]
	add	w0, w0, 1
	str	w0, [x1,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	and	w19, w0, 16384
	tbz	x0, 14, .L2702
	ldr	x1, [x20,3392]
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	ldr	w1, [x1,156]
	bl	printk
	mov	w0, 0
	b	.L2678
.L2681:
	ldrb	w1, [x28,3360]
	ldr	x0, [x21,584]
	lsl	w1, w1, 9
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2683
	ldr	w1, [x19,8]
	mov	x0, x27
	bl	printk
	b	.L2680
.L2678:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	ftl_info_blk_init, .-ftl_info_blk_init
	.align	2
	.global	ftl_ext_info_flush
	.type	ftl_ext_info_flush, %function
ftl_ext_info_flush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	bl	timer_get_time
	mov	w1, 100
	adrp	x21, .LANCHOR0
	udiv	w0, w0, w1
	add	x1, x21, :lo12:.LANCHOR0
	ldr	x1, [x1,1120]
	ldr	w3, [x1,520]
	cmp	w0, w3
	bls	.L2705
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2718
.L2705:
	bcs	.L2706
.L2718:
	str	w0, [x1,520]
.L2706:
	adrp	x23, .LANCHOR4
	adrp	x22, .LANCHOR5
	add	x23, x23, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR5
	mov	w0, 0
	add	x23, x23, 616
	add	x24, x22, 320
	bl	ftl_total_vpn_update
.L2710:
	add	x0, x21, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3
	add	x2, x3, :lo12:.LANCHOR3
	mov	x19, x3
	ldr	x0, [x0,3392]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,1294]
	cmp	w1, w0
	bcc	.L2708
	bl	ftl_ext_alloc_new_blk
.L2708:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2709
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 2203
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2709:
	add	x20, x21, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR3
	mov	w0, 24
	ldr	x4, [x20,3392]
	ldrb	w1, [x20,1257]
	ldrh	w19, [x3,1232]
	ldrh	w2, [x4,130]
	sub	w1, w0, w1
	sub	w1, w1, w19
	mov	w19, 1
	asr	w25, w2, w1
	lsl	w1, w19, w1
	sub	w19, w1, #1
	ldrh	w0, [x4,140]
	and	w2, w19, w2
	ldrh	w19, [x3,1330]
	mov	w1, 0
	sxth	w2, w2
	madd	w19, w2, w19, w0
	ldrb	w2, [x20,3360]
	mov	x0, x24
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x24]
	ldr	x0, [x20,3392]
	ldrb	w1, [x20,3360]
	ldr	w0, [x0,56]
	lsl	w1, w1, 9
	str	w0, [x24,4]
	ldr	x0, [x22,600]
	bl	js_hash
	str	w0, [x24,8]
	ldr	x2, [x22,600]
	mov	w1, w19
	ldrb	w4, [x20,3360]
	mov	w0, w25
	mov	x3, x24
	bl	ftl_prog_page
	ldr	x1, [x20,3392]
	ldrh	w2, [x1,140]
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x1,140]
	cmp	w2, 1
	beq	.L2710
	cmn	w0, #1
	beq	.L2711
	ldrb	w0, [x22,594]
	cbz	w0, .L2712
.L2711:
.L2707:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,594]
	b	.L2710
.L2712:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	stp	x29, x30, [sp, -112]!
	mov	w4, 4
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR5
	stp	x21, x22, [sp,32]
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	add	x1, x21, :lo12:.LANCHOR3
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x0, [x22,3392]
	add	x3, x3, 320
	ldrb	w2, [x22,1257]
	strh	wzr, [x1,1864]
	ldrh	w23, [x0,130]
	mov	w0, 24
	ldrh	w1, [x1,1232]
	sub	w0, w0, w2
	ldr	x2, [x3,280]
	sub	w0, w0, w1
	mov	w1, 1
	asr	w26, w23, w0
	lsl	w0, w1, w0
	sub	w0, w0, #1
	and	w23, w0, w23
	uxtb	w28, w26
	mov	w1, w23
	mov	w0, w28
	bl	flash_get_last_written_page
	sxth	w25, w0
	adrp	x0, .LANCHOR2
	str	x0, [x29,104]
	mov	w24, w25
	ldr	w1, [x0,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L2720
	ldr	x5, [x22,3392]
	adrp	x1, .LANCHOR4
	adrp	x0, .LC194
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC194
	add	x1, x1, 640
	ldrh	w5, [x5,130]
	mov	w2, 2248
	mov	w3, w25
	uxth	w4, w26
	bl	printk
.L2720:
	add	x20, x20, :lo12:.LANCHOR5
	adrp	x27, .LC195
	mov	w22, w25
	add	x26, x20, 320
	add	x27, x27, :lo12:.LC195
.L2721:
	tbnz	w22, #31, .L2726
	add	x0, x21, :lo12:.LANCHOR3
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x2, [x20,600]
	mov	x3, x26
	str	x5, [x29,96]
	ldrh	w1, [x0,1330]
	mov	w0, w28
	ldrb	w4, [x5,3360]
	madd	w1, w23, w1, w22
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2722
	cmn	w0, #1
	beq	.L2722
	ldr	x5, [x29,96]
	ldr	x0, [x5,1120]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2722
	ldr	w2, [x26,8]
	cbnz	w2, .L2723
.L2726:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,140]
	cmp	w1, w25
	bgt	.L2725
	add	w24, w24, 1
	strh	w24, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2725
.L2723:
	ldrb	w1, [x5,3360]
	ldr	x0, [x20,600]
	lsl	w1, w1, 9
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	cmp	w2, w0
	beq	.L2726
	ldr	w1, [x26,8]
	mov	x0, x27
	bl	printk
.L2722:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L2721
.L2725:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x20, [x19,1120]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x20,520]
	ldr	x20, [x19,1120]
	bl	timer_get_time
	str	w0, [x20,604]
	ldr	x0, [x19,1120]
	mov	w1, -1
	strh	w1, [x0,584]
	strh	w1, [x0,586]
	strh	w1, [x0,588]
	strh	w1, [x0,590]
	mov	w1, 65535
	str	w1, [x0,560]
	mov	w1, -1
	str	w1, [x0,564]
	ldr	x1, [x29,104]
	str	wzr, [x0,608]
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L2728
	ldr	w2, [x0,520]
	ldr	w3, [x0,12]
	str	x2, [x29,96]
	str	x3, [x29,104]
	bl	timer_get_time
	mov	w4, w0
	adrp	x1, .LANCHOR4
	adrp	x0, .LC196
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x2, [x29,96]
	ldr	x3, [x29,104]
	add	x0, x0, :lo12:.LC196
	add	x1, x1, 640
	bl	printk
.L2728:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, -1
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w0, [x21,1208]
	mov	w0, 0
	ldp	x21, x22, [sp,32]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_prog_ppa_page
	.type	ftl_prog_ppa_page, %function
ftl_prog_ppa_page:
	mov	w8, w0
	adrp	x0, .LANCHOR0+1257
	stp	x29, x30, [sp, -16]!
	mov	w5, 24
	mov	x10, x1
	add	x29, sp, 0
	ldrb	w6, [x0,#:lo12:.LANCHOR0+1257]
	mov	x9, x2
	mov	w4, w3
	mov	x2, x10
	sub	w7, w5, w6
	mov	w5, 1
	lsl	w6, w5, w6
	lsl	w5, w5, w7
	lsr	w0, w8, w7
	sub	w6, w6, #1
	sub	w1, w5, #1
	and	w0, w0, w6
	and	w1, w1, w8
	mov	x3, x9
	bl	ftl_prog_page
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_prog_ppa_page, .-ftl_prog_ppa_page
	.align	2
	.global	ftl_write_last_log_page
	.type	ftl_write_last_log_page, %function
ftl_write_last_log_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR3
	mov	w19, -1
	ldrh	w1, [x0,6]
	add	x22, x22, :lo12:.LANCHOR3
	ldrh	w23, [x0,12]
	cmp	w1, 1
	ldr	x21, [x22,1848]
	bne	.L2743
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w24, w0
	mov	w19, 0
	beq	.L2743
	ldrh	w0, [x20]
	add	x23, x21, x23, uxth 2
	adrp	x21, .LANCHOR5
	add	x21, x21, :lo12:.LANCHOR5
	bl	ftl_vpn_decrement
	add	x20, x21, 640
	mov	w0, 15555
	ldrh	w1, [x22,1294]
	movk	w0, 0xf55f, lsl 16
	str	w0, [x21,640]
	ldrb	w0, [x22,1249]
	mul	w1, w1, w0
	mov	x0, x23
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x21,644]
	add	x1, x21, 656
	mov	x2, 0
	mov	w0, 2
	str	w19, [x21,648]
	str	w19, [x21,652]
	str	w19, [x21,656]
	bl	ftl_debug_info_fill
	adrp	x3, .LANCHOR0+3360
	mov	w0, w24
	mov	x1, x23
	mov	x2, x20
	ldrb	w3, [x3,#:lo12:.LANCHOR0+3360]
	bl	ftl_prog_ppa_page
.L2743:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR4
	mov	w21, w0
	stp	x23, x24, [sp,48]
	add	x22, x22, :lo12:.LANCHOR4
	adrp	x23, .LC0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x24, x1
	adrp	x25, .LANCHOR0
	add	x23, x23, :lo12:.LC0
	add	x22, x22, 664
.L2748:
	add	x0, x25, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x0,3392]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	adrp	x1, .LANCHOR3
	add	x2, x1, :lo12:.LANCHOR3
	mov	x20, x1
	ldrh	w2, [x2,1294]
	cmp	w3, w2
	bcs	.L2749
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2750
.L2749:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2750:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2751
	mov	x1, x22
	mov	w2, 303
	mov	x0, x23
	bl	printk
	bl	dump_stack
.L2751:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x20, :lo12:.LANCHOR3
	adrp	x20, .LANCHOR5
	add	x26, x20, :lo12:.LANCHOR5
	ldr	x2, [x19,3392]
	ldrh	w1, [x0,1330]
	ldrh	w27, [x2,692]
	ldrh	w0, [x2,696]
	mov	w2, 64
	madd	w27, w27, w1, w0
	ldr	x0, [x26,896]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x26,896]
	str	w21, [x0]
	ldr	x1, [x19,3392]
	ldr	x0, [x26,896]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	x0, x24
	ldrb	w1, [x19,3360]
	ldr	x28, [x26,896]
	lsl	w1, w1, 9
	bl	js_hash
	str	w0, [x28,8]
	ldr	x0, [x19,3392]
	mov	x2, x24
	ldr	x3, [x26,896]
	mov	w1, w27
	ldrb	w4, [x19,3360]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x3, [x19,3392]
	ldrh	w2, [x3,696]
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x3,696]
	cmp	w2, 1
	beq	.L2752
	ldrb	w1, [x26,904]
	cbz	w1, .L2753
.L2752:
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x20,904]
	b	.L2748
.L2753:
	cmn	w0, #1
	bne	.L2755
	adrp	x0, .LC197
	mov	w1, w27
	add	x0, x0, :lo12:.LC197
	bl	printk
	b	.L2748
.L2755:
	ldrh	w0, [x3,698]
	cmp	w21, w0
	bcs	.L2756
	add	x21, x3, x21, uxtw 2
	str	w27, [x21,704]
.L2756:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	sub	sp, sp, #208
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x21, x22, [sp,80]
	adrp	x21, .LANCHOR0
	uxth	w22, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,64]
	stp	x23, x24, [sp,96]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	ldrh	w0, [x0,1088]
	cmp	w0, w22
	bls	.L2760
	adrp	x20, .LANCHOR3
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w1, [x0,1264]
	cbnz	w1, .L2762
	ldrb	w0, [x0,1250]
	cbz	w0, .L2760
.L2762:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L2760
	ldr	x0, [x0,1096]
	ubfiz	x19, x22, 2, 16
	mov	w24, 1
	add	x0, x0, x19
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2763
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w24, [x0,1248]
.L2763:
	add	x23, x29, 128
	mov	w0, w22
	add	x1, x23, 16
	strh	w22, [x29,128]
	mov	w27, 0
	adrp	x26, .LC199
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	add	x1, x20, :lo12:.LANCHOR3
	strb	w0, [x29,137]
	strh	wzr, [x29,130]
	mov	w28, w27
	strb	wzr, [x29,133]
	mov	w25, w27
	ldrh	w1, [x1,1294]
	add	x26, x26, :lo12:.LC199
	strh	wzr, [x29,138]
	mul	w0, w0, w1
	strh	w0, [x29,134]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,1096]
	add	x1, x0, x19
	ldrh	w4, [x0,x19]
	ldr	w5, [x0,x19]
	adrp	x0, .LC198
	ldrb	w3, [x1,2]
	and	w4, w4, 2047
	mov	w1, w22
	ubfx	x5, x5, 11, 8
	ubfx	x2, x3, 5, 3
	add	x0, x0, :lo12:.LC198
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
.L2764:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	cmp	w0, w28
	bls	.L2788
	lsl	w0, w28, 1
	add	w25, w0, w28
	sub	w0, w0, #1
	sub	w1, w25, #1
	str	w0, [x29,120]
	str	w1, [x29,124]
	mov	w25, 0
.L2776:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2800
	sxtw	x0, w25
	mov	w27, 1
	str	x0, [x29,112]
.L2775:
	cmp	w27, w24
	bhi	.L2801
	ldr	x0, [x29,112]
	add	x0, x0, 8
	ldrh	w2, [x23,x0,lsl 1]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2766
	add	x1, x20, :lo12:.LANCHOR3
	cmp	w24, 3
	ldrh	w0, [x1,1330]
	mul	w0, w2, w0
	bne	.L2767
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w3, [x3,1269]
	cbz	w3, .L2768
	ldr	w3, [x29,124]
	b	.L2799
.L2768:
	add	w0, w0, w28
	orr	w3, w0, w27, lsl 24
	b	.L2769
.L2767:
	cmp	w24, 2
	add	w3, w0, w28
	bne	.L2769
	ldr	w3, [x29,120]
.L2799:
	add	w0, w0, w3
	ldrb	w3, [x1,1248]
	add	w0, w0, w27
	orr	w3, w0, w3, lsl 24
.L2769:
	str	w3, [x19,40]
	mov	x0, x19
	mov	w1, 1
	str	x2, [x29,96]
	str	x3, [x29,104]
	bl	sblk_read_page
	ldr	w4, [x19,52]
	ldr	x3, [x29,104]
	cmp	w4, 512
	ldr	x2, [x29,96]
	cset	w11, eq
	cmn	w4, #1
	beq	.L2789
	cbz	w11, .L2766
.L2789:
	ldr	x7, [x19,8]
	ldr	x0, [x19,24]
	str	x11, [x29,104]
	ldr	w1, [x7,12]
	str	w1, [sp]
	ldr	w1, [x0]
	str	w1, [sp,8]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w2
	mov	w2, w28
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	mov	x0, x26
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x11, [x29,104]
	cbnz	w11, .L2788
.L2766:
	add	w9, w27, 1
	uxth	w27, w9
	b	.L2775
.L2801:
	add	w10, w25, 1
	uxth	w25, w10
	b	.L2776
.L2800:
	add	w8, w28, 1
	uxth	w28, w8
	b	.L2764
.L2788:
.L2773:
	adrp	x0, .LC200
	mov	w3, w25
	mov	w4, w27
	mov	w2, w28
	mov	w1, w22
	add	x25, x21, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC200
	adrp	x26, .LC201
	mov	w24, w28
	add	x26, x26, :lo12:.LC201
	bl	printk
	mov	w27, 24
	ldrb	w2, [x25,3360]
	mov	w1, 0
	ldr	x0, [x19,8]
	mov	w28, 1
	lsl	w2, w2, 9
	bl	ftl_memset
	ldrb	w2, [x25,3360]
	mov	w1, 0
	ldr	x0, [x19,24]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2777:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	cmp	w0, w24
	bls	.L2783
	lsl	w0, w24, 1
	mov	w25, 0
	str	w0, [x29,124]
.L2784:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2802
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2778
	mov	x0, x26
	mov	w1, w24
	mov	w2, w25
	bl	printk
.L2778:
	add	x1, x20, :lo12:.LANCHOR3
	sxtw	x0, w25
	ldrb	w5, [x1,1264]
	ldrh	w2, [x1,1330]
	cbz	w5, .L2779
	add	x0, x0, 8
	ldrh	w0, [x23,x0,lsl 1]
	mul	w2, w0, w2
	mov	x0, x19
	orr	w2, w2, w24
	str	w2, [x19,40]
	bl	sblk_3d_tlc_dump_prog
	b	.L2780
.L2779:
	add	x0, x0, 8
	ldrb	w1, [x1,1248]
	cmp	w1, 2
	ldrh	w0, [x23,x0,lsl 1]
	mul	w2, w0, w2
	bne	.L2781
	ldr	w0, [x29,124]
	orr	w2, w2, w0
	mov	x0, x19
	orr	w2, w2, 33554432
	str	w2, [x19,40]
	bl	sblk_mlc_dump_prog
	b	.L2780
.L2781:
	add	x4, x21, :lo12:.LANCHOR0
	orr	w2, w2, w24
	str	w2, [x19,40]
	ldrb	w0, [x4,1257]
	ldrb	w4, [x4,3360]
	sub	w1, w27, w0
	lsl	w0, w28, w0
	sub	w0, w0, #1
	lsr	w3, w2, w1
	lsl	w1, w0, w1
	and	w0, w3, w0
	bic	w1, w2, w1
	ldr	x3, [x19,24]
	ldr	x2, [x19,8]
	bl	flash_prog_page_en
.L2780:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2784
.L2802:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2777
.L2783:
	mov	x0, x19
	bl	zbuf_free
	adrp	x0, .LC202
	mov	w1, w22
	add	x0, x0, :lo12:.LC202
	bl	printk
.L2760:
	sub	sp, x29, #48
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 208
	ret
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	adrp	x0, .LANCHOR2
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	tbz	x0, 12, .L2804
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 364
	add	x2, x2, 680
	bl	printk
.L2804:
	adrp	x22, .LANCHOR5
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR5
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC204
	adrp	x25, .LC205
	ldr	x0, [x0,912]
	mov	w26, 0
	add	x24, x24, :lo12:.LC204
	add	x25, x25, :lo12:.LC205
	bl	ftl_memset
	ldr	x1, [x19,1240]
	adrp	x0, .LC203
	add	x0, x0, :lo12:.LC203
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,1240]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2805:
	add	x21, x22, :lo12:.LANCHOR5
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,920]
	ldrh	w19, [x21,922]
	mov	w1, w20
	ldrh	w28, [x27,2]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,92]
	ldrh	w0, [x21,922]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2806
	ldr	x1, [x27,1240]
	strh	wzr, [x21,922]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,920]
	ldr	x19, [x27,1240]
	ldrb	w1, [x21,921]
	strb	w0, [x21,921]
	mov	x0, x19
	strb	w1, [x21,920]
	ldrh	w1, [x19,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
	ldrb	w1, [x21,920]
	mov	w0, 0
	ldrh	w19, [x27,2]
	mul	w19, w1, w19
	b	.L2814
.L2806:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2807
.L2814:
	mov	w1, w19
	bl	flash_erase_block
.L2807:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR5
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,1240]
	ldr	x0, [x20,912]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,912]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,1240]
	ldr	x3, [x20,912]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,922]
	add	w1, w1, 1
	strh	w1, [x20,922]
	bne	.L2808
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2805
.L2808:
	cbnz	w26, .L2815
	mov	w26, 1
	b	.L2805
.L2815:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_info_flush, .-flash_info_flush
	.align	2
	.global	flash_info_blk_init
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	adrp	x0, .LANCHOR2
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	adrp	x20, .LANCHOR0
	tbz	x0, 12, .L2817
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR4
	adrp	x0, .LC206
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC206
	mov	w1, 49
	ldr	x4, [x4,1240]
	add	x2, x2, 704
	mov	w3, 2048
	bl	printk
.L2817:
	mov	w24, 4
	adrp	x25, .LANCHOR5
.L2818:
	mov	w23, 0
.L2821:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x25, :lo12:.LANCHOR5
	mov	w0, 0
	mov	w4, 4
	adrp	x22, .LANCHOR5
	ldrh	w1, [x19,2]
	ldr	x2, [x19,1240]
	ldr	x3, [x21,912]
	madd	w1, w24, w1, w23
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2819
	ldr	x2, [x19,1240]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L2820
.L2819:
	add	w23, w23, 1
	cmp	w23, 4
	bne	.L2821
	add	w24, w24, 1
	cmp	w24, 16
	bne	.L2818
	b	.L2830
.L2820:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,2]
	strb	w1, [x21,920]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,912]
	strb	w0, [x21,921]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2823
	ldr	x0, [x19,1240]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2823
	ldr	w21, [x0,4]
.L2823:
	add	x19, x22, :lo12:.LANCHOR5
	add	x23, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,921]
	ldrh	w1, [x23,2]
	ldr	x3, [x19,912]
	mul	w1, w2, w1
	ldr	x2, [x23,1240]
	bl	flash_read_page_en
	cmn	w0, #1
	bne	.L2847
.L2824:
	add	x19, x22, :lo12:.LANCHOR5
	add	x21, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x19,920]
	ldr	x2, [x21,1240]
	ldr	x3, [x19,912]
	bl	flash_get_last_written_page
	ldrh	w23, [x21,2]
	ldrb	w1, [x19,920]
	uxth	w24, w0
	add	w0, w24, 1
	mov	w21, 0
	strh	w0, [x19,922]
	mul	w23, w1, w23
.L2825:
	sub	w19, w24, w21
	sxth	w19, w19
	tbz	w19, #31, .L2828
	cmn	w19, #1
	bne	.L2827
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC207
	add	x0, x0, :lo12:.LC207
	ldr	x1, [x20,1240]
	ldr	w1, [x1]
	bl	printk
.L2830:
	mov	w0, -1
	b	.L2846
.L2847:
	ldr	x0, [x23,1240]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2824
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L2824
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,920]
	strb	w0, [x19,921]
	b	.L2824
.L2828:
	add	x25, x20, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR5
	mov	w0, 0
	add	w1, w19, w23
	mov	w4, 4
	ldr	x2, [x25,1240]
	ldr	x3, [x3,912]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2826
	ldr	x0, [x25,1240]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	beq	.L2827
.L2826:
	add	w21, w21, 1
	b	.L2825
.L2827:
	cmp	w21, 1
	mov	w0, 0
	bls	.L2846
	bl	flash_info_flush
	mov	w0, 0
.L2846:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_info_blk_init, .-flash_info_blk_init
	.align	2
	.global	nand_flash_init
	.type	nand_flash_init, %function
nand_flash_init:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x19, x0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	str	x25, [sp,64]
	tbz	x0, 12, .L2849
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 3429
	add	x2, x2, 728
	bl	printk
.L2849:
	adrp	x21, .LANCHOR5
	mov	x0, x19
	add	x23, x21, :lo12:.LANCHOR5
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w25, 44
	add	x24, x22, 1272
	str	wzr, [x23,236]
	bl	nandc_init
	mov	w0, 1
	add	x1, x20, :lo12:.LANCHOR2
	add	x1, x1, 8
	strb	w0, [x22,1145]
	mov	w0, 3
	str	x1, [x22,1136]
	strb	w0, [x22,1257]
	mov	w1, 0
	mov	w2, 8
	add	x0, x22, 1248
	bl	ftl_memset
	add	x0, x23, 96
	mov	w1, 0
	mov	w2, 32
	mov	w23, 0
	bl	ftl_memset
.L2855:
	mov	w0, w23
	mov	x1, x24
	bl	flash_read_id
	cbnz	w23, .L2850
	ldrb	w0, [x22,1272]
	add	x1, x22, 1272
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2851
.L2853:
	mov	w22, -2
	b	.L2933
.L2851:
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L2853
.L2850:
	ldrb	w0, [x24]
	cmp	w0, 181
	bne	.L2854
	strb	w25, [x24]
.L2854:
	add	w23, w23, 1
	add	x24, x24, 8
	uxtb	w23, w23
	cmp	w23, 4
	bne	.L2855
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x24, x24, 1272
.L2858:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x24
	bl	flash_mem_cmp8
	cbnz	w0, .L2856
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L2853
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2860
.L2856:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 49
	bne	.L2858
	b	.L2853
.L2860:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,2008]
	cmp	w4, w3
	beq	.L2859
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2860
	mov	w2, w0
.L2859:
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x23, 2008
	ubfiz	x2, x2, 5, 32
	add	x1, x1, x2
	add	x0, x24, 1152
	mov	w2, 32
	add	x23, x23, 8
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x24,1028]
	cmp	w0, 8
	bhi	.L2861
	ldrb	w0, [x23,20]
	cmp	w0, 60
	bls	.L2861
	mov	w0, 60
	strb	w0, [x23,20]
.L2861:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2895
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 3456
	add	x2, x2, 728
	bl	printk
.L2895:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1136]
	ldrh	w1, [x1,10]
	cmp	w1, 1023
	bls	.L2862
	mov	w1, 2
	strb	w1, [x0,1257]
.L2862:
	add	x23, x21, :lo12:.LANCHOR5
	mov	w0, 16384
	add	x22, x19, :lo12:.LANCHOR0
	add	x24, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x23,248]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x23,208]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x22,1240]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,256]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,224]
	mov	w0, 64
	bl	ftl_malloc
	strb	wzr, [x23,924]
	str	x0, [x23,912]
	bl	flash_die_info_init
	ldrb	w0, [x24,26]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,28]
	bl	nandc_bch_sel
	str	xzr, [x23,240]
	ldr	x3, [x22,1136]
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldrh	w0, [x3,16]
	ubfx	x1, x0, 8, 3
	strb	w1, [x22,1304]
	ubfx	x1, x0, 3, 1
	strb	w1, [x23,272]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,1305]
	ubfx	x1, x0, 12, 1
	strb	w1, [x2,1264]
	ubfx	x1, x0, 13, 1
	strb	w1, [x2,1250]
	ubfx	x1, x0, 11, 1
	strb	w1, [x22,1268]
	ldrb	w1, [x3,31]
	ubfx	x4, x1, 1, 1
	strb	w4, [x2,1868]
	ubfx	x4, x1, 2, 1
	strb	w4, [x23,925]
	ubfx	x4, x0, 14, 1
	lsr	w0, w0, 15
	strb	w0, [x2,1251]
	ubfx	x2, x1, 3, 1
	strb	w2, [x22,1256]
	ubfx	x1, x1, 4, 1
	ldrb	w2, [x22,1028]
	ldrb	w0, [x3,28]
	strb	w1, [x22,1269]
	cmp	w2, 9
	mov	w1, 60
	strb	w4, [x22,1]
	strb	w0, [x19,#:lo12:.LANCHOR0]
	strb	w1, [x22,1144]
	bne	.L2863
	mov	w1, 70
	strb	w1, [x22,1144]
.L2863:
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1146]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrb	w3, [x0,31]
	tbz	x3, 0, .L2864
	ldrb	w0, [x0,29]
	cbz	w0, .L2865
	mov	w0, 2
	b	.L2935
.L2865:
	mov	w0, 3
.L2935:
	strb	w0, [x1,1146]
.L2864:
	cmp	w2, 8
	bne	.L2866
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1272]
	cmp	w0, 137
	beq	.L2896
	cmp	w0, 44
	bne	.L2866
.L2896:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,36]
	cmp	w0, 3
	bne	.L2866
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1146]
.L2866:
	add	x1, x20, :lo12:.LANCHOR2
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x1, 8
	ldrb	w0, [x1,19]
	ldrh	w1, [x1,16]
	strb	w0, [x2,1237]
	tbz	x1, 6, .L2869
	sub	w1, w0, #17
	cmp	w0, 21
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 2
	bls	.L2897
	cbz	w2, .L2870
.L2897:
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, micron_read_retrial
	add	x1, x1, :lo12:micron_read_retrial
	str	x1, [x0,240]
	mov	w1, 15
	cbz	w2, .L2936
	mov	w1, 4
.L2936:
	strb	w1, [x0,232]
	b	.L2869
.L2870:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2898
	cmp	w0, 33
	bne	.L2874
.L2898:
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,240]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,1238]
	b	.L2939
.L2874:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2899
	cbz	w2, .L2876
.L2899:
	add	x3, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x3,240]
	beq	.L2878
	cmp	w0, 68
	beq	.L2878
	mov	w0, 7
	strb	w0, [x3,232]
	b	.L2879
.L2878:
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, 17
	strb	w1, [x0,232]
.L2879:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L2937
	mov	w1, 5
.L2937:
	strb	w1, [x0,1238]
	b	.L2869
.L2876:
	sub	w1, w0, #36
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L2881
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_3d_read_retrial
	add	x1, x1, :lo12:toshiba_3d_read_retrial
	str	x1, [x0,240]
.L2939:
	mov	w1, 7
	b	.L2936
.L2881:
	cmp	w0, 50
	bne	.L2882
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, samsung_read_retrial
	add	x1, x1, :lo12:samsung_read_retrial
	str	x1, [x0,240]
	mov	w1, 25
	b	.L2936
.L2882:
	cmp	w0, 81
	bne	.L2869
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, ymtc_3d_read_retrial
	add	x1, x1, :lo12:ymtc_3d_read_retrial
	str	x1, [x0,240]
	mov	w1, 7
	strb	w1, [x0,232]
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1185]
.L2869:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2883
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 3549
	add	x2, x2, 728
	bl	printk
.L2883:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1304]
	strb	wzr, [x0,1184]
	tbz	x1, 0, .L2884
	ldrb	w0, [x0,1272]
	cmp	w0, 155
	beq	.L2885
	mov	w0, 4
	bl	flash_set_interface_mode
	mov	w0, 4
	bl	nandc_set_if_mode
.L2885:
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2938
.L2884:
	mov	w0, 4
.L2938:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2887
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19,1240]
	strb	wzr, [x19,1264]
	strb	w1, [x0,32]
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	b	.L2933
.L2887:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,15]
	cmp	w0, 9
	bne	.L2888
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1240]
	ldrb	w1, [x1,20]
	cmp	w1, 1
	beq	.L2888
	strb	wzr, [x0,1146]
.L2888:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1237]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L2889
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,240]
.L2889:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,1304]
	tbz	x0, 2, .L2891
	ldr	x0, [x19,1240]
	ldrb	w0, [x0,19]
	cbz	w0, .L2891
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w1, [x19,2]
	mov	w0, 0
	ldrb	w2, [x21,920]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,1184]
	cbnz	w0, .L2891
	ldr	x1, [x19,1240]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2891:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2893
	adrp	x2, .LANCHOR4
	adrp	x0, .LC141
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC141
	mov	w1, 3647
	add	x2, x2, 728
	bl	printk
.L2893:
	mov	w22, 0
	bl	nand_flash_print_info
.L2933:
	mov	w0, w22
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	nand_flash_init, .-nand_flash_init
	.align	2
	.global	ftl_sysblk_dump
	.type	ftl_sysblk_dump, %function
ftl_sysblk_dump:
	sub	sp, sp, #144
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x23, x24, [sp,96]
	uxth	w24, w0
	mov	w0, 1
	stp	x19, x20, [sp,64]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	stp	x21, x22, [sp,80]
	mov	w20, 0
	bl	buf_alloc
	adrp	x22, .LC199
	ldr	x26, [x0,8]
	mov	x25, x0
	mov	w19, w20
	adrp	x27, .LANCHOR3
	adrp	x28, .LANCHOR0
	add	x22, x22, :lo12:.LC199
.L2941:
	add	x2, x27, :lo12:.LANCHOR3
	ldrh	w3, [x2,1294]
	cmp	w3, w19
	bls	.L2953
	ldrh	w21, [x2,1330]
	add	x3, x28, :lo12:.LANCHOR0
	ldr	x1, [x25,8]
	ldr	x2, [x25,24]
	madd	w21, w24, w21, w19
	ldrb	w3, [x3,3360]
	mov	w0, w21
	bl	ftl_read_ppa_page
	mov	w23, w0
	ldr	x7, [x25,8]
	mov	w1, w24
	ldr	x2, [x25,24]
	mov	w3, w21
	mov	w4, w23
	ldr	w0, [x7,12]
	str	w0, [sp]
	ldr	w0, [x2]
	str	w0, [sp,8]
	ldr	w0, [x2,4]
	str	w0, [sp,16]
	ldr	w0, [x2,8]
	str	w0, [sp,24]
	ldr	w0, [x2,12]
	mov	w2, w19
	str	w0, [sp,32]
	mov	x0, x22
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	cmp	w23, 512
	beq	.L2946
	cmn	w23, #1
	bne	.L2942
.L2946:
	mov	w20, 1
.L2942:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2941
.L2953:
	adrp	x0, .LC208
	add	x1, x26, 704
	add	x0, x0, :lo12:.LC208
	mov	w2, 4
	mov	w3, 32
	bl	rknand_print_hex
	mov	x0, x25
	bl	zbuf_free
	cbz	w20, .L2945
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1611
	add	x1, x1, 744
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2945:
	sub	sp, x29, #48
	mov	w0, w20
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 144
	ret
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	stp	x29, x30, [sp, -368]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	mov	x19, x0
	str	x1, [x29,136]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	tbz	x0, 12, .L2955
	adrp	x0, .LC209
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC209
	bl	printk
.L2955:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2956
	adrp	x0, .LC210
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC210
	bl	printk
.L2956:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2957
	adrp	x0, .LC211
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC211
	bl	printk
.L2957:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2958
	adrp	x0, .LC212
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC212
	bl	printk
.L2958:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2959
	adrp	x0, .LC213
	ldrb	w1, [x19,9]
	add	x0, x0, :lo12:.LC213
	bl	printk
.L2959:
	adrp	x20, .LANCHOR0
	ldrh	w0, [x19,10]
	add	x21, x20, :lo12:.LANCHOR0
	strh	w0, [x19,14]
	ldrh	w1, [x19]
	ldrh	w0, [x21,1088]
	cmp	w1, w0
	bcs	.L2954
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	ldrb	w0, [x21,3360]
	add	x21, x29, 176
	mov	w2, 64
	ldr	x1, [x27,8]
	sub	w0, w0, #2
	sbfiz	x0, x0, 9, 32
	add	x0, x1, x0
	mov	w1, 255
	str	x0, [x29,160]
	mov	x0, x21
	bl	ftl_memset
	mov	w1, 255
	mov	w2, 64
	add	x0, x29, 240
	bl	ftl_memset
	add	x0, x29, 304
	mov	w1, 255
	mov	w2, 64
	bl	ftl_memset
	ldrb	w24, [x19,5]
	mov	w0, 2
	str	w0, [x29,168]
	adrp	x0, .LANCHOR5
	ldrh	w25, [x19,2]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x29,144]
	add	x0, x0, 320
	str	x0, [x29,128]
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	str	x0, [x29,120]
.L2961:
	adrp	x23, .LANCHOR3
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	cmp	w0, w25
	bls	.L2964
	ldrb	w24, [x19,5]
.L2962:
	ldrb	w0, [x19,9]
	cmp	w0, w24
	bls	.L3101
	add	x0, x19, x24, sxtw 1
	ldrh	w28, [x0,16]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L2963
	add	x0, x23, :lo12:.LANCHOR3
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrh	w26, [x0,1330]
	ldrb	w3, [x3,3360]
	madd	w0, w28, w26, w25
	str	w0, [x29,152]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w26, w0
	beq	.L2964
	cmn	w0, #1
	beq	.L2965
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2965
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2965
	ldr	x0, [x27,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2964
.L2965:
	adrp	x0, .LANCHOR5
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR5
	strb	w1, [x0,593]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w25, w0, w24
	cmp	w1, w0
	beq	.L2966
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1686
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2966:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2967
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1687
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2967:
	add	x0, x20, :lo12:.LANCHOR0
	str	x0, [x29,168]
	ldrb	w0, [x0,3360]
	cmp	w0, 8
	bls	.L2968
	ldr	x0, [x27,24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L2968
	cmn	w26, #1
	beq	.L2973
	ldr	w1, [x0,4]
	cmn	w1, #1
	beq	.L3008
	ldr	w2, [x0,16]
	mov	w1, 21320
	movk	w1, 0x4841, lsl 16
	cmp	w2, w1
	bne	.L3008
	ldr	w2, [x0,20]
	mov	w1, 1024
	ldr	x0, [x29,160]
	str	x2, [x29,144]
	bl	js_hash
	ldr	x2, [x29,144]
	cmp	w2, w0
	beq	.L3008
	ldr	x0, [x29,160]
	mov	w1, 1024
	bl	js_hash
	mov	w5, w0
	ldr	w3, [x29,152]
	mov	w1, w28
	ldr	x0, [x29,120]
	mov	w2, w25
	mov	w4, w26
	bl	printk
	ldr	x1, [x29,160]
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x0, [x29,168]
	mov	w2, 4
	ldr	x1, [x27,24]
	ldrb	w3, [x0,3360]
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	lsr	w3, w3, 1
	bl	rknand_print_hex
	b	.L2973
.L2968:
	cmn	w26, #1
	beq	.L2973
.L3008:
	ldr	x1, [x27,24]
	ldr	w0, [x1,4]
	cmn	w0, #1
	beq	.L2973
	ldr	w2, [x1]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	cmp	w2, w1
	beq	.L2973
	bl	lpa_hash_get_ppa
	ldr	x1, [x29,136]
	cbz	x1, .L2972
	ldr	x8, [x27,24]
	ldr	w1, [x8,8]
	cmp	w0, w1
	beq	.L2972
	cmn	w0, #1
	beq	.L2972
	add	x7, x20, :lo12:.LANCHOR0
	add	x4, x23, :lo12:.LANCHOR3
	mov	w5, 24
	mov	w28, 1
	ldrb	w2, [x7,1257]
	ldrh	w1, [x4,1232]
	sub	w2, w5, w2
	lsr	w3, w0, w1
	sub	w1, w2, w1
	lsl	w1, w28, w1
	ldrb	w2, [x4,1234]
	sub	w1, w1, #1
	and	w1, w3, w1
	udiv	w1, w1, w2
	ldr	x2, [x29,136]
	ldrh	w2, [x2]
	cmp	w2, w1, uxth
	bne	.L2972
	ldr	w8, [x8]
	ldrb	w3, [x7,3360]
	ldr	x1, [x27,8]
	ldr	x2, [x29,128]
	str	x5, [x29,104]
	str	x4, [x29,112]
	str	x8, [x29,144]
	str	x7, [x29,168]
	bl	ftl_read_ppa_page
	ldr	x0, [x29,128]
	ldr	x8, [x29,144]
	ldr	x7, [x29,168]
	ldr	w0, [x0]
	ldr	x4, [x29,112]
	cmp	w0, w8
	ldr	x5, [x29,104]
	bcc	.L2972
	ldr	x0, [x27,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2973
	ldrb	w2, [x7,1257]
	ldrh	w0, [x4,1232]
	sub	w5, w5, w2
	sub	w5, w5, w0
	lsr	w1, w1, w0
	lsl	w28, w28, w5
	ldrb	w0, [x4,1234]
	sub	w28, w28, #1
	and	w1, w28, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2973
.L2972:
	ldr	x2, [x27,24]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,3372]
	cmp	w3, w0
	bcs	.L2973
	add	x4, x23, :lo12:.LANCHOR3
	ldrb	w0, [x19,9]
	ldrh	w3, [x19,10]
	ldrh	w4, [x4,1294]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L3102
.L2973:
	ldrh	w0, [x19,6]
	sub	w0, w0, #1
	strh	w0, [x19,6]
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
	mov	w0, 4
	str	w0, [x29,168]
	mov	w0, 1
	str	w0, [x29,144]
.L2963:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2962
.L3101:
	add	w25, w25, 1
	strb	wzr, [x19,5]
	uxth	w25, w25
	b	.L2961
.L2964:
	add	x2, x23, :lo12:.LANCHOR3
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	strh	w25, [x19,2]
	add	w1, w1, w0
	ldrh	w2, [x2,1294]
	ldrb	w0, [x19,9]
	strb	w24, [x19,5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2977
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1794
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2977:
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w3, [x19,10]
	ldr	x4, [x0,1848]
	mov	w0, 0
	mov	w2, w0
.L2978:
	cmp	w2, w3
	bcs	.L3103
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L2979
	add	w1, w0, 1
	uxth	w0, w1
.L2979:
	add	w2, w2, 1
	b	.L2978
.L3103:
	add	x2, x23, :lo12:.LANCHOR3
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w24, [x2,1294]
	madd	w0, w1, w24, w0
	uxth	w24, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2981
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	mov	w2, w24
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,1112]
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	ldrh	w3, [x4,x3]
	bl	printk
.L2981:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldr	x0, [x0,1112]
	strh	w24, [x0,x1,lsl 1]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2982
	adrp	x0, .LC217
	ldr	w1, [x29,176]
	ldr	w2, [x29,180]
	add	x0, x0, :lo12:.LC217
	ldr	w3, [x29,184]
	ldr	w4, [x29,188]
	bl	printk
.L2982:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 0
	adrp	x28, .LC219
	mov	x24, 0
	mov	w26, -1
	ldrb	w2, [x0,3360]
	ldr	x0, [x27,8]
	lsl	w2, w2, 9
	bl	ftl_memset
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	str	x0, [x29,152]
	add	x0, x28, :lo12:.LC219
	str	x0, [x29,136]
.L2983:
	add	x0, x23, :lo12:.LANCHOR3
	ldr	w1, [x29,168]
	str	w24, [x29,160]
	ldrb	w0, [x0,1249]
	mul	w0, w1, w0
	cmp	w24, w0
	bcs	.L3104
	ldr	w0, [x21,x24,lsl 2]
	cmn	w0, #1
	bne	.L2984
.L2988:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x27,24]
	mov	x2, 0
	mov	w25, -1
	ldr	x1, [x1,3392]
	ldr	w1, [x1,8]
	str	w1, [x0]
	ldr	x0, [x27,24]
	str	w26, [x0,4]
	ldr	x0, [x27,24]
	str	w26, [x0,8]
	ldr	x0, [x27,24]
	str	wzr, [x0,12]
	ldr	x0, [x27,24]
	str	wzr, [x0,16]
	ldr	x0, [x27,8]
	str	wzr, [x0]
	mov	w0, 2
	ldr	x1, [x27,24]
	add	x1, x1, 16
	bl	ftl_debug_info_fill
	b	.L2985
.L2984:
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrb	w3, [x3,3360]
	bl	ftl_read_ppa_page
	mov	w25, w0
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2986
	ldr	x3, [x27,24]
	mov	w1, w28
	ldr	x0, [x29,152]
	ldr	w2, [x21,x24,lsl 2]
	ldr	w3, [x3,4]
	bl	printk
.L2986:
	ldr	w0, [x21,x24,lsl 2]
	mov	w1, 1
	mov	x2, 0
	cmp	w28, w0
	ldr	x0, [x27,24]
	csinv	w25, w25, wzr, eq
	str	w28, [x0,8]
	ldr	x0, [x27,24]
	str	w1, [x0,12]
	ldr	x0, [x27,24]
	str	wzr, [x0,16]
	mov	w0, 2
	ldr	x1, [x27,24]
	add	x1, x1, 16
	bl	ftl_debug_info_fill
	cmn	w25, #1
	beq	.L2988
.L2985:
	ldr	w0, [x29,144]
	cbz	w0, .L2990
.L3100:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L2990
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2992
	ldr	x2, [x27,24]
	mov	w1, w28
	ldrh	w0, [x19,12]
	ldrh	w3, [x19,10]
	ldr	w2, [x2,4]
	add	w3, w3, w0
	ldr	x0, [x29,136]
	sub	w3, w3, #1
	bl	printk
.L2992:
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w0, [x19,6]
	ldrb	w2, [x1,1249]
	ldr	w1, [x29,168]
	mul	w2, w1, w2
	ldr	w1, [x29,160]
	add	w2, w2, 1
	sub	w2, w2, w1
	cmp	w0, w2
	bls	.L2990
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	mov	w0, w28
	ldrb	w3, [x3,3360]
	bl	ftl_prog_ppa_page
	mov	w2, w0
	ldrh	w0, [x19]
	str	x2, [x29,128]
	bl	ftl_vpn_decrement
	ldr	x2, [x29,128]
	cmn	w25, #1
	cset	w1, ne
	cmn	w2, #1
	beq	.L2993
	cbz	w1, .L2990
	add	x0, x29, 240
	ldrh	w1, [x19,10]
	str	w28, [x0,x24,lsl 2]
	ldrh	w0, [x19,12]
	add	w0, w1, w0
	add	x1, x29, 304
	sub	w0, w0, #1
	str	w0, [x1,x24,lsl 2]
	b	.L2990
.L2993:
	cbnz	w1, .L3100
.L2990:
	add	x24, x24, 1
	b	.L2983
.L3104:
	adrp	x24, .LC220
	adrp	x25, .LC219
	mov	x26, 0
	add	x24, x24, :lo12:.LC220
	add	x25, x25, :lo12:.LC219
.L2997:
	add	x28, x23, :lo12:.LANCHOR3
	ldr	w1, [x29,168]
	ldrb	w0, [x28,1249]
	mul	w0, w1, w0
	cmp	w0, w26
	bls	.L3105
	add	x0, x29, 240
	ldr	w0, [x0,x26,lsl 2]
	cmn	w0, #1
	beq	.L2999
	add	x3, x20, :lo12:.LANCHOR0
	ldr	w0, [x21,x26,lsl 2]
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrb	w3, [x3,3360]
	bl	ftl_read_ppa_page
	cmp	w0, 256
	beq	.L3010
	cmn	w0, #1
	bne	.L2999
.L3010:
	add	x3, x20, :lo12:.LANCHOR0
	add	x0, x29, 240
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldr	w0, [x0,x26,lsl 2]
	ldrb	w3, [x3,3360]
	bl	ftl_read_ppa_page
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3002
	ldr	x2, [x27,24]
	mov	x0, x25
	ldr	w3, [x21,x26,lsl 2]
	mov	w1, w28
	ldr	w2, [x2,8]
	bl	printk
.L3002:
	cmn	w28, #1
	beq	.L2999
	ldr	x1, [x27,24]
	ldr	w2, [x21,x26,lsl 2]
	ldr	w0, [x1,8]
	cmp	w2, w0
	bne	.L2999
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3003
	add	x3, x29, 304
	ldr	w1, [x1,4]
	mov	x0, x24
	ldr	w3, [x3,x26,lsl 2]
	bl	printk
.L3003:
	ldr	x1, [x27,24]
	add	x0, x29, 304
	ldr	w2, [x0,x26,lsl 2]
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
.L2999:
	add	x26, x26, 1
	b	.L2997
.L3105:
	mov	x0, x27
	bl	zbuf_free
	ldrh	w1, [x28,1294]
	ldrh	w0, [x19,12]
	ldrb	w2, [x19,9]
	madd	w0, w1, w2, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x28,1848]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L3005
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1909
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3005:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2954
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2954
.L3102:
	ldr	x0, [x1,3392]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2974
	str	w2, [x0,8]
.L2974:
	ldr	x1, [x27,24]
	ldrh	w0, [x19,10]
	ldrh	w2, [x19,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	ldr	w0, [x21,4]
	str	w0, [x21]
	ldr	w0, [x21,8]
	str	w0, [x21,4]
	ldr	w0, [x21,12]
	str	w0, [x21,8]
	ldr	w0, [x29,152]
	str	w0, [x21,12]
	b	.L2973
.L2954:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 368
	ret
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LC221
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x0, :lo12:.LC221
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	ldrb	w1, [x19,3362]
	bl	printk
	adrp	x3, .LANCHOR5
	adrp	x0, .LC222
	add	x3, x3, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC222
	ldrb	w1, [x3,576]
	ldrb	w2, [x3,577]
	ldrh	w3, [x3,578]
	bl	printk
	ldr	x2, [x19,3392]
	adrp	x0, .LC223
	add	x0, x0, :lo12:.LC223
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x20,1294]
	mov	w2, 4
	ldrb	w3, [x20,1249]
	ldr	x1, [x20,1848]
	mul	w3, w0, w3
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,1112]
	adrp	x0, .LC228
	ldrh	w3, [x19,1088]
	add	x0, x0, :lo12:.LC228
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,3392]
	adrp	x0, .LC208
	add	x0, x0, :lo12:.LC208
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,1096]
	adrp	x0, .LC229
	ldrh	w3, [x19,1088]
	add	x0, x0, :lo12:.LC229
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC230
	add	x1, x20, 1336
	add	x0, x0, :lo12:.LC230
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x20,1294]
	mov	w2, 2
	ldrb	w3, [x20,1249]
	ldr	x1, [x20,1856]
	mul	w3, w0, w3
	adrp	x0, .LC231
	add	x0, x0, :lo12:.LC231
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_ftl_info, .-dump_ftl_info
	.align	2
	.global	pm_ppa_update_check
	.type	pm_ppa_update_check, %function
pm_ppa_update_check:
	adrp	x3, .LANCHOR0
	adrp	x7, .LANCHOR3
	add	x3, x3, :lo12:.LANCHOR0
	add	x7, x7, :lo12:.LANCHOR3
	stp	x29, x30, [sp, -16]!
	mov	w5, 24
	add	x29, sp, 0
	ldrb	w6, [x3,1257]
	ldrh	w4, [x7,1232]
	sub	w5, w5, w6
	mov	w6, 1
	sub	w5, w5, w4
	lsr	w4, w2, w4
	lsl	w5, w6, w5
	ldr	x3, [x3,1096]
	sub	w5, w5, #1
	and	w4, w5, w4
	ldrb	w5, [x7,1234]
	udiv	w4, w4, w5
	add	x4, x3, x4, uxth 2
	ldrb	w3, [x4,2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 7
	cset	w4, eq
	cbnz	w4, .L3111
	cmp	w3, w6
	bne	.L3108
.L3111:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC232
	add	x0, x0, :lo12:.LC232
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L3108:
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	x22, w1
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	cmp	w22, 31
	uxth	w21, w0
	bls	.L3114
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 32
	add	x1, x1, 784
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3114:
	adrp	x24, .LANCHOR0
	adrp	x20, .LANCHOR5
	add	x26, x24, :lo12:.LANCHOR0
	ldr	x0, [x26,3392]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L3115
	adrp	x0, .LC233
	mov	w1, w21
	add	x0, x0, :lo12:.LC233
	mov	x19, 0
	add	x20, x20, :lo12:.LANCHOR5
	bl	printk
	ldr	x0, [x19,8]
	mov	w1, 255
	ldrh	w2, [x20,926]
	bl	ftl_memset
	ldr	x0, [x26,3392]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L3127
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 784
	mov	w2, 37
	b	.L3128
.L3115:
	add	x0, x0, x21, sxtw 2
	adrp	x25, .LANCHOR3
	ldr	w19, [x0,704]
	add	x0, x25, :lo12:.LANCHOR3
	add	x0, x0, 1880
	lsl	x1, x22, 4
	add	x27, x0, x1
	strh	w21, [x0,x1]
	strh	wzr, [x27,2]
	cbnz	w19, .L3118
	adrp	x0, .LC234
	mov	w1, w21
	mov	w2, w19
	add	x20, x20, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC234
	bl	printk
	ldr	x0, [x27,8]
	mov	w1, 255
	ldrh	w2, [x20,926]
	bl	ftl_memset
	b	.L3127
.L3118:
	add	x23, x20, :lo12:.LANCHOR5
	ldrb	w3, [x26,3360]
	ldr	x1, [x27,8]
	mov	w0, w19
	ldr	x2, [x23,896]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x23,896]
	ldr	w2, [x0]
	cmp	w2, w21
	bne	.L3119
	cmn	w3, #1
	beq	.L3119
	cmp	w3, 512
	beq	.L3119
.L3124:
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x20,896]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L3127
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 784
	mov	w2, 73
	b	.L3128
.L3119:
	adrp	x0, .LC235
	mov	w4, w19
	add	x24, x24, :lo12:.LANCHOR0
	mov	w1, w21
	add	x0, x0, :lo12:.LC235
	add	x23, x25, :lo12:.LANCHOR3
	add	x23, x23, 1880
	add	x26, x20, :lo12:.LANCHOR5
	bl	printk
	add	x23, x23, x22, lsl 4
	ldr	x3, [x24,3392]
	adrp	x0, .LC236
	add	x0, x0, :lo12:.LC236
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x24,3360]
	adrp	x0, .LC215
	ldr	x1, [x23,8]
	add	x0, x0, :lo12:.LC215
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldr	x1, [x26,896]
	adrp	x0, .LC237
	add	x0, x0, :lo12:.LC237
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x1, [x23,8]
	mov	w0, w19
	ldr	x2, [x26,896]
	ldrb	w3, [x24,3360]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	beq	.L3126
	cmn	w0, #1
	bne	.L3122
.L3126:
	add	x25, x25, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	add	x22, x25, x22, lsl 4
	mov	w1, 255
	ldrh	w2, [x20,926]
	ldr	x0, [x22,1888]
	bl	ftl_memset
	mov	w0, -1
	b	.L3117
.L3122:
	ldr	x0, [x26,896]
	ldr	w1, [x0]
	mov	w0, -1
	cmp	w1, w21
	beq	.L3124
	b	.L3117
.L3128:
	bl	printk
	bl	dump_stack
.L3127:
	mov	w0, 0
.L3117:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	pm_gc
	.type	pm_gc, %function
pm_gc:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	x0, [x0,3392]
	ldrh	w2, [x0,688]
	ldrh	w0, [x1,176]
	sub	w0, w0, #1
	cmp	w2, w0
	bge	.L3130
	ldr	w0, [x1,168]
	cbz	w0, .L3131
.L3130:
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x2, [x1,3392]
	ldrh	w1, [x3,176]
	ldrh	w4, [x2,688]
	sub	w1, w1, #1
	cmp	w4, w1
	bge	.L3132
	ldr	w1, [x3,168]
	cbz	w1, .L3131
.L3132:
	add	x20, x20, :lo12:.LANCHOR5
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	str	wzr, [x20,168]
	mov	w20, 65535
	cmp	w21, w20
	bne	.L3134
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 182
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	add	x0, x1, x0, uxth 1
	ldrh	w21, [x0,416]
	cmp	w21, w20
	beq	.L3131
.L3134:
	bl	pm_select_ram_region
	adrp	x26, .LANCHOR3
	uxth	x20, w0
	add	x0, x26, :lo12:.LANCHOR3
	lsl	x1, x20, 4
	add	x0, x0, 1880
	add	x23, x0, x1
	mov	x22, x20
	ldrh	w0, [x0,x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3135
	ldr	x1, [x23,8]
	cbz	x1, .L3135
	ldrsh	w2, [x23,2]
	tbz	w2, #31, .L3135
	bl	pm_write_page
	ldrh	w0, [x23,2]
	and	w0, w0, 32767
	strh	w0, [x23,2]
.L3135:
	add	x26, x26, :lo12:.LANCHOR3
	mov	w25, 0
	add	x0, x26, 1880
	mov	w23, 24
	mov	w24, 1
	add	x20, x0, x20, lsl 4
.L3136:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,3392]
	ldrh	w1, [x0,698]
	cmp	w1, w25
	bls	.L3146
	ldrb	w2, [x2,1257]
	add	x0, x0, x25, sxtw 2
	ldrh	w1, [x26,1232]
	ldr	w0, [x0,704]
	sub	w2, w23, w2
	lsr	w0, w0, w1
	sub	w1, w2, w1
	lsl	w1, w24, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x26,1234]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L3137
	mov	w0, w25
	mov	w1, w22
	bl	load_l2p_region
	cbnz	w0, .L3138
	ldr	x1, [x20,8]
	mov	w0, w25
	bl	pm_write_page
.L3138:
	mov	w0, -1
	strh	w0, [x20]
.L3137:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L3136
.L3146:
	bl	pm_free_sblk
.L3131:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	pm_gc, .-pm_gc
	.align	2
	.global	pm_flush_id
	.type	pm_flush_id, %function
pm_flush_id:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 4, 16
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	add	x1, x19, 1880
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,1876]
	cbz	w0, .L3148
	bl	pm_gc
	str	wzr, [x19,1876]
.L3148:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	pm_flush_id, .-pm_flush_id
	.align	2
	.global	pm_flush
	.type	pm_flush, %function
pm_flush:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR3
	mov	x19, 0
	add	x20, x20, :lo12:.LANCHOR3
	add	x20, x20, 1880
.L3154:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L3153
	bl	pm_flush_id
.L3153:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L3154
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	pm_flush, .-pm_flush
	.align	2
	.global	flt_sys_flush
	.type	flt_sys_flush, %function
flt_sys_flush:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	ldp	x29, x30, [sp], 16
	ret
	.size	flt_sys_flush, .-flt_sys_flush
	.align	2
	.global	zftl_deinit
	.type	zftl_deinit, %function
zftl_deinit:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	zftl_flash_de_init
	bl	flt_sys_flush
	bl	zftl_flash_de_init
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_deinit, .-zftl_deinit
	.align	2
	.global	pm_init
	.type	pm_init, %function
pm_init:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR5
	adrp	x19, .LANCHOR3
	stp	x21, x22, [sp,32]
	add	x21, x20, :lo12:.LANCHOR5
	add	x22, x19, :lo12:.LANCHOR3
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w23, w0
	mov	w0, 1
	strb	w0, [x21,904]
	mov	w0, 64
	str	wzr, [x21,168]
	mov	w24, -1
	str	wzr, [x22,1876]
	adrp	x25, .LANCHOR0
	bl	ftl_malloc
	str	x0, [x21,896]
	add	x21, x22, 1880
	add	x22, x22, 2392
.L3160:
	strh	w24, [x21]
	strh	wzr, [x21,2]
	cbz	w23, .L3159
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x21,8]
.L3159:
	add	x21, x21, 16
	cmp	x21, x22
	bne	.L3160
	add	x0, x19, :lo12:.LANCHOR3
	adrp	x21, .LANCHOR0
	add	x23, x21, :lo12:.LANCHOR0
	mov	w4, 4
	ldr	x26, [x0,1888]
	add	x0, x20, :lo12:.LANCHOR5
	ldr	x1, [x23,3392]
	mov	x2, x26
	ldr	x22, [x0,896]
	ldrb	w0, [x1,694]
	ldrh	w1, [x1,692]
	mov	x3, x22
	bl	flash_get_last_written_page
	sxth	w24, w0
	ldr	x1, [x23,3392]
	mov	w25, w24
	ldrh	w2, [x1,696]
	cmp	w2, w24
	bgt	.L3161
	adrp	x0, .LC238
	ldrh	w1, [x1,692]
	add	x0, x0, :lo12:.LC238
	mov	w3, w24
	adrp	x27, .LC239
	adrp	x28, .LC240
	bl	printk
	add	x28, x28, :lo12:.LC240
	ldr	x0, [x23,3392]
	ldrsh	w23, [x0,696]
	add	w0, w24, 1
	str	w0, [x29,124]
	add	x0, x27, :lo12:.LC239
	str	x0, [x29,112]
.L3162:
	ldr	w0, [x29,124]
	cmp	w23, w0
	bge	.L3183
	add	x27, x21, :lo12:.LANCHOR0
	add	x1, x19, :lo12:.LANCHOR3
	mov	x2, x26
	mov	x3, x22
	ldr	x0, [x27,3392]
	ldrh	w1, [x1,1330]
	ldrb	w4, [x27,3360]
	ldrh	w24, [x0,692]
	ldrb	w0, [x0,694]
	madd	w24, w24, w1, w23
	mov	w1, w24
	bl	flash_read_page_en
	mov	w4, w0
	ldr	x3, [x27,3392]
	mov	w2, w24
	ldr	x0, [x29,112]
	str	x4, [x29,104]
	ldr	w1, [x3,48]
	add	w1, w1, 1
	str	w1, [x3,48]
	ldrh	w3, [x3,694]
	ldr	w1, [x22]
	bl	printk
	ldr	x4, [x29,104]
	cmp	w4, 512
	beq	.L3163
	cmn	w4, #1
	beq	.L3163
	ldr	x0, [x27,3392]
	ldr	w1, [x22]
	ldrh	w0, [x0,698]
	cmp	w1, w0
	bcs	.L3163
	ldr	w2, [x22,8]
	cbz	w2, .L3164
	ldrb	w1, [x27,3360]
	mov	x0, x26
	str	x2, [x29,104]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L3164
	ldr	w1, [x22,8]
	mov	x0, x28
	bl	printk
	b	.L3163
.L3164:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldr	w0, [x22]
	add	x0, x0, 176
	str	w24, [x1,x0,lsl 2]
.L3163:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L3162
.L3183:
	add	x0, x20, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	mov	w1, 1
	add	w25, w25, 1
	strb	w1, [x0,593]
	ldr	x0, [x21,3392]
	strh	w25, [x0,696]
	bl	pm_free_sblk
.L3161:
	add	x19, x19, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, 255
	add	x19, x19, 1880
	ldr	x0, [x19,8]
	ldrh	w2, [x20,926]
	bl	ftl_memset
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldrb	w0, [x20,593]
	cbz	w0, .L3166
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
.L3166:
	bl	pm_free_sblk
	bl	pm_gc
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	pm_init, .-pm_init
	.align	2
	.global	pm_log2phys
	.type	pm_log2phys, %function
pm_log2phys:
	stp	x29, x30, [sp, -96]!
	mov	w3, w0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	add	x0, x23, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	w25, w2
	mov	x22, x1
	ldrb	w20, [x0,3360]
	ldr	w2, [x0,3372]
	lsl	w24, w20, 7
	lsl	w20, w20, 7
	cmp	w3, w2
	udiv	w24, w3, w24
	uxth	w26, w24
	msub	w20, w26, w20, w3
	uxth	x20, w20
	bcc	.L3185
	adrp	x0, .LC241
	mov	w1, w3
	add	x0, x0, :lo12:.LC241
	bl	printk
	mov	w0, -1
	cbnz	w25, .L3186
	str	w0, [x22]
	b	.L3186
.L3185:
	adrp	x21, .LANCHOR3
	mov	x19, 0
	add	x1, x21, :lo12:.LANCHOR3
	add	x1, x1, 1880
.L3191:
	ldr	x0, [x1,8]
	cbz	x0, .L3187
	ldrh	w0, [x1]
	cmp	w0, w26
	bne	.L3187
.L3188:
	cbnz	w25, .L3189
	add	x3, x21, :lo12:.LANCHOR3
	add	x23, x23, :lo12:.LANCHOR0
	add	x0, x3, x19, lsl 4
	mov	w2, 1
	ldr	x0, [x0,1888]
	ldr	w1, [x0,x20,lsl 2]
	mov	w0, 24
	str	w1, [x22]
	ldrb	w4, [x23,1257]
	sub	w0, w0, w4
	lsl	w2, w2, w4
	lsr	w1, w1, w0
	sub	w2, w2, #1
	and	w0, w1, w2
	ldrb	w1, [x3,1332]
	cmp	w0, w1
	bcc	.L3190
	mov	w0, -1
	str	w0, [x22]
	b	.L3190
.L3189:
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w3, [x22]
	add	x0, x2, 1880
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w3, [x1,x20,lsl 2]
	strb	w24, [x2,2392]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
.L3190:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 32767
	add	x21, x21, 1880
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L3186
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L3186
.L3187:
	add	w19, w19, 1
	add	x1, x1, 16
	uxth	x19, w19
	cmp	w19, 32
	bne	.L3191
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR3
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 1880
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	beq	.L3192
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L3192
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
.L3192:
	adrp	x0, .LANCHOR5+928
	strb	w19, [x0,#:lo12:.LANCHOR5+928]
	mov	w0, w26
	bl	load_l2p_region
	b	.L3188
.L3186:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	sub	sp, sp, #224
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp,64]
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	stp	x27, x28, [sp,112]
	adrp	x23, .LANCHOR5
	add	x25, x22, :lo12:.LANCHOR3
	ldr	x20, [x21,1120]
	add	x0, x23, :lo12:.LANCHOR5
	strb	wzr, [x25,1265]
	add	x24, x20, 80
	ldrh	w1, [x20,80]
	strb	wzr, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L3200
	mov	w0, -1
	strh	w0, [x20,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	ldrb	w0, [x24,9]
	ldrh	w1, [x25,1330]
	sub	w0, w0, #1
	add	x0, x24, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x25,1248]
	str	w0, [x29,184]
	cmp	w1, 2
	beq	.L3201
	ldrb	w2, [x21,1268]
	cbnz	w2, .L3201
	ldrb	w2, [x21,1269]
	cbz	w2, .L3202
.L3201:
	add	x2, x22, :lo12:.LANCHOR3
	ldrh	w2, [x2,1266]
	sub	w2, w2, #1
	add	w0, w2, w0
	orr	w0, w0, w1, lsl 24
	b	.L3288
.L3202:
	cmp	w1, 3
	bne	.L3203
	ldrh	w1, [x25,1294]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
.L3288:
	str	w0, [x28,40]
.L3203:
	mov	x0, x28
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3204
	cmn	w0, #1
	beq	.L3204
	ldr	x0, [x28,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3205
.L3204:
	mov	x0, x28
	bl	zbuf_free
	ldr	x7, [x28,24]
	ldr	x6, [x28,8]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC242
	add	x0, x0, :lo12:.LC242
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x28,40]
	ldr	w2, [x28,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L3293
.L3205:
	add	x1, x23, :lo12:.LANCHOR5
	ldrb	w0, [x1,929]
	cmp	w0, 2
	bne	.L3207
	add	x21, x22, :lo12:.LANCHOR3
	ldrb	w0, [x21,1248]
	cmp	w0, 3
	bne	.L3207
	ldrh	w3, [x1,926]
	ldr	x4, [x1,144]
	ldrh	w1, [x21,1266]
	and	x0, x3, 65532
	ldrb	w2, [x21,1249]
	add	x0, x4, x0
	lsl	w1, w1, 2
	mul	w2, w1, w2
	ldr	x1, [x28,8]
	sub	w2, w2, w3
	uxth	w2, w2
	bl	ftl_memcpy
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L3208
	ldrb	w0, [x0,1269]
	cbz	w0, .L3209
.L3208:
	ldr	w0, [x28,40]
	sub	w0, w0, #1
	b	.L3289
.L3209:
	ldr	w0, [x29,184]
	ldrh	w1, [x21,1294]
	sub	w0, w0, #1
	add	w0, w0, w1
	orr	w0, w0, 33554432
.L3289:
	str	w0, [x28,40]
	mov	w1, 1
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3211
	cmn	w0, #1
	beq	.L3211
	ldr	x0, [x28,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3212
.L3211:
	mov	x0, x28
	bl	zbuf_free
.L3293:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	mov	w21, 0
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,1120]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L3206
.L3212:
	add	x2, x23, :lo12:.LANCHOR5
	ldr	x1, [x28,8]
	ldr	x0, [x2,144]
	ldrh	w2, [x2,926]
	b	.L3290
.L3207:
	add	x2, x22, :lo12:.LANCHOR3
	add	x1, x23, :lo12:.LANCHOR5
	ldrh	w0, [x2,1266]
	ldrb	w2, [x2,1249]
	mul	w2, w0, w2
	ldr	x0, [x1,144]
	ldr	x1, [x28,8]
	lsl	w2, w2, 2
.L3290:
	add	x21, x22, :lo12:.LANCHOR3
	bl	ftl_memcpy
	ldrh	w0, [x21,1266]
	ldrb	w1, [x21,1249]
	mul	w1, w0, w1
	add	x0, x19, :lo12:.LANCHOR0
	lsl	w2, w1, 2
	ldrb	w0, [x0,3360]
	cmp	w0, w1, lsr 6
	bge	.L3214
	add	x25, x23, :lo12:.LANCHOR5
	mov	w1, 0
	ldr	x0, [x25,152]
	bl	ftl_memset
	ldrb	w26, [x25,929]
	cmp	w26, 1
	bne	.L3254
	ldrh	w0, [x21,1266]
	ldrb	w1, [x21,1249]
	ldrh	w21, [x25,926]
	ldr	x2, [x28,8]
	mul	w1, w0, w1
	ldr	x0, [x25,152]
	sub	w21, w21, w1, lsl 2
	add	x1, x2, x1, sxtw 2
	mov	w2, w21
	bl	ftl_memcpy
	mov	w10, w26
	b	.L3215
.L3214:
	add	x0, x23, :lo12:.LANCHOR5
	ldr	x3, [x28,8]
	add	x1, x3, x1, sxtw 2
	ldr	x0, [x0,152]
	bl	ftl_memcpy
	mov	w10, 0
	mov	w21, w10
	b	.L3215
.L3254:
	mov	w10, 1
	mov	w21, 0
.L3215:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w25, [x0,1248]
	ldrh	w27, [x0,1294]
	cmp	w25, 2
	bne	.L3216
	ldrb	w0, [x0,1250]
	cbz	w0, .L3217
.L3216:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1268]
	cbz	w0, .L3218
.L3217:
	mul	w27, w27, w25
	mov	w25, 1
.L3218:
	adrp	x0, .LC243
	mov	w26, 0
	add	x0, x0, :lo12:.LC243
	str	w26, [x29,172]
	str	x0, [x29,144]
.L3219:
	cmp	w26, w27
	bcs	.L3231
	add	w0, w26, w26, lsl 1
	mov	w11, 0
	sub	w0, w0, #1
	str	w0, [x29,168]
	sub	w0, w26, #1
	str	w0, [x29,152]
.L3232:
	ldrb	w0, [x24,9]
	cmp	w11, w0
	bge	.L3294
	sxtw	x0, w11
	mov	x12, 0
	str	x0, [x29,160]
.L3229:
	add	w2, w12, 1
	ldr	w0, [x29,172]
	cmp	w2, w25
	add	w13, w0, w12
	bhi	.L3295
	ldr	x0, [x29,160]
	add	x3, x22, :lo12:.LANCHOR3
	add	x0, x0, 8
	ldrh	w1, [x24,x0,lsl 1]
	ldrh	w0, [x3,1330]
	mul	w0, w1, w0
	ldrb	w1, [x3,1248]
	str	w0, [x29,184]
	cmp	w1, 2
	beq	.L3220
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w4, [x3,1268]
	cbz	w4, .L3221
.L3220:
	ldr	w3, [x29,152]
	add	w0, w3, w0
	add	w0, w0, w2
	orr	w0, w0, w1, lsl 24
	b	.L3291
.L3221:
	cmp	w1, 3
	bne	.L3223
	ldrb	w1, [x3,1269]
	cbz	w1, .L3224
	ldr	w1, [x29,168]
	add	w0, w0, w1
	add	w0, w0, w2
	orr	w0, w0, 50331648
	b	.L3291
.L3224:
	add	w0, w0, w26
	orr	w0, w0, w2, lsl 24
	b	.L3291
.L3223:
	add	w0, w0, w26
.L3291:
	str	w0, [x28,40]
	mov	w1, 1
	mov	x0, x28
	str	x13, [x29,112]
	str	x12, [x29,120]
	str	x10, [x29,128]
	str	x11, [x29,136]
	bl	sblk_read_page
	ldr	x13, [x29,112]
	ldr	x10, [x29,128]
	ldr	x11, [x29,136]
	sxtw	x13, w13
	ldr	x12, [x29,120]
	cbz	w10, .L3225
	add	x0, x23, :lo12:.LANCHOR5
	lsl	x1, x13, 2
	ldr	x0, [x0,152]
	ldr	w2, [x0,x1]
	cbnz	w2, .L3225
	ldr	x2, [x28,24]
	ldr	w2, [x2,8]
	str	w2, [x0,x1]
.L3225:
	add	x0, x23, :lo12:.LANCHOR5
	lsl	x13, x13, 2
	ldr	x7, [x28,24]
	ldr	x1, [x0,144]
	ldr	w6, [x7,4]
	ldr	w3, [x1,x13]
	cmp	w3, w6
	bne	.L3226
	ldr	x0, [x0,152]
	ldr	w1, [x0,x13]
	ldr	w0, [x7,8]
	cmp	w1, w0
	beq	.L3256
.L3226:
	add	x14, x23, :lo12:.LANCHOR5
	ldr	w0, [x7,12]
	str	x12, [x29,104]
	str	x10, [x29,112]
	ldr	x4, [x14,152]
	str	w0, [sp]
	ldr	x0, [x29,144]
	ldr	w4, [x4,x13]
	ldr	w5, [x7]
	ldr	w1, [x28,40]
	ldr	w2, [x28,52]
	ldr	w7, [x7,8]
	str	x11, [x29,120]
	str	x14, [x29,128]
	str	x13, [x29,136]
	bl	printk
	ldr	x14, [x29,128]
	ldr	x13, [x29,136]
	ldr	x11, [x29,120]
	ldr	x0, [x14,144]
	ldr	x10, [x29,112]
	ldr	x12, [x29,104]
	ldr	w0, [x0,x13]
	cmn	w0, #1
	beq	.L3256
	mov	x0, x28
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,1120]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L3206
.L3256:
	add	x12, x12, 1
	b	.L3229
.L3295:
	str	w13, [x29,172]
	add	w11, w11, 1
	b	.L3232
.L3294:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0,1250]
	cmp	w0, wzr
	csinc	w26, w26, w26, eq
	add	w26, w26, 1
	b	.L3219
.L3231:
	mov	x0, x28
	mov	w21, 0
	mov	w28, w21
	bl	zbuf_free
	str	w21, [x29,168]
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR3
	ldrh	w2, [x20,80]
	ldr	x1, [x0,1112]
	ldrh	w3, [x3,1266]
	ldrb	w0, [x24,9]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
.L3233:
	cmp	w28, w27
	bcs	.L3247
	add	w0, w28, w28, lsl 1
	str	wzr, [x29,172]
	sub	w0, w0, #1
	str	w0, [x29,144]
	sub	w0, w28, #1
	str	w0, [x29,128]
.L3248:
	ldrb	w0, [x24,9]
	ldr	w1, [x29,172]
	cmp	w1, w0
	bge	.L3244
	ldrsw	x0, [x29,172]
	mov	w26, 1
	str	x0, [x29,136]
.L3245:
	cmp	w26, w25
	bhi	.L3296
	add	x1, x23, :lo12:.LANCHOR5
	ldr	w0, [x29,168]
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,144]
	ldr	w5, [x2,x0]
	cmn	w5, #1
	beq	.L3234
	ldr	x1, [x1,152]
	str	x5, [x29,152]
	ldr	w0, [x1,x0]
	str	w0, [x29,160]
	mov	w0, w5
	bl	lpa_hash_get_ppa
	str	w0, [x29,188]
	cmn	w0, #1
	ldr	x5, [x29,152]
	bne	.L3235
	mov	w0, w5
	add	x1, x29, 188
	mov	w2, 0
	bl	pm_log2phys
	ldr	x5, [x29,152]
.L3235:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	ldrh	w1, [x24,x0,lsl 1]
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w2, [x0,1330]
	ldrb	w0, [x0,1248]
	cmp	w0, 2
	mul	w1, w1, w2
	beq	.L3236
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w6, [x2,1268]
	cbz	w6, .L3237
.L3236:
	ldr	w2, [x29,128]
	add	w1, w2, w1
	add	w1, w1, w26
	orr	w1, w1, w0, lsl 24
	b	.L3292
.L3237:
	cmp	w0, 3
	bne	.L3239
	ldrb	w2, [x2,1269]
	lsl	w0, w26, 24
	cbz	w2, .L3240
	ldr	w2, [x29,144]
	add	w2, w1, w2
	add	w2, w2, w26
	orr	w1, w0, w2
	b	.L3292
.L3240:
	add	w1, w1, w28
	orr	w1, w0, w1
	b	.L3292
.L3239:
	add	w1, w1, w28
.L3292:
	add	x2, x22, :lo12:.LANCHOR3
	ldr	w0, [x29,160]
	str	w1, [x29,184]
	mov	w3, 24
	ldrh	w1, [x2,1232]
	lsr	w6, w0, w1
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1257]
	sub	w0, w3, w0
	sub	w1, w0, w1
	mov	w0, 1
	lsl	w1, w0, w1
	ldrb	w0, [x2,1234]
	sub	w1, w1, #1
	and	w1, w6, w1
	udiv	w1, w1, w0
	uxth	w0, w1
	ldr	w1, [x29,160]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w0, w1
	bne	.L3241
	mov	w0, w5
	add	x1, x29, 184
	mov	w2, 1
	add	w21, w21, 1
	bl	pm_log2phys
	ldrh	w0, [x29,152]
	bl	ftl_vpn_decrement
	b	.L3242
.L3241:
	ldr	w1, [x29,184]
	cmp	w0, w1
	csinc	w21, w21, w21, ne
.L3242:
	ldr	w0, [x29,168]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,168]
	ldr	w0, [x29,152]
	ldr	x1, [x2,1096]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L3234
	lsl	x0, x0, 1
	ldr	x2, [x2,1112]
	ldrh	w5, [x2,x0]
	cbz	w5, .L3234
	strh	w1, [x2,x0]
.L3234:
	add	w26, w26, 1
	b	.L3245
.L3296:
	ldr	w0, [x29,172]
	add	w0, w0, 1
	str	w0, [x29,172]
	b	.L3248
.L3244:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0,1250]
	cmp	w0, wzr
	csinc	w28, w28, w28, eq
	add	w28, w28, 1
	b	.L3233
.L3247:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,1112]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20,80]
	bl	zftl_insert_data_list
.L3206:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,80]
	ubfiz	x2, x0, 1, 16
	ldr	x1, [x1,1112]
	ldrh	w1, [x1,x2]
	cbnz	w1, .L3249
	bl	ftl_dump_write_open_sblk
.L3249:
	ldrh	w1, [x20,80]
	adrp	x0, .LC244
	mov	w2, w21
	add	x0, x0, :lo12:.LC244
	bl	printk
	mov	w0, -1
	strh	w0, [x20,80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldr	x0, [x1,1120]
	ldrh	w0, [x0,130]
	cmp	w0, w2
	beq	.L3250
	ldrh	w1, [x1,1088]
	cmp	w1, w0
	bhi	.L3251
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 517
	add	x1, x1, 808
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3251:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
.L3250:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,3392]
	strh	w0, [x1,126]
	ldr	x1, [x19,1120]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3199
.L3200:
	ldrh	w0, [x20,130]
	cmp	w0, w1
	beq	.L3199
	ldr	x1, [x21,3392]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L3253
	bl	pm_flush
	ldr	x0, [x21,1120]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x21,3392]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L3253:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,1120]
	strh	w1, [x0,130]
.L3199:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldp	x27, x28, [sp,112]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 224
	ret
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	gc_update_l2p_map_new
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR2
	ldr	x20, [x0,1120]
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	str	x0, [x29,120]
	ldrh	w25, [x1,1266]
	add	x1, x20, 80
	str	x1, [x29,128]
	ldrb	w1, [x1,9]
	mul	w25, w25, w1
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3298
	adrp	x0, .LC245
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC245
	bl	printk
.L3298:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR5
	ldrh	w2, [x20,80]
	mov	w22, 0
	mov	w24, w22
	ldr	x1, [x0,1112]
	add	x0, x21, :lo12:.LANCHOR5
	ldrb	w0, [x0,929]
	sub	w0, w25, w0
	strh	w0, [x1,x2,lsl 1]
	adrp	x0, .LC247
	add	x0, x0, :lo12:.LC247
	str	x0, [x29,112]
	adrp	x0, .LC246
	add	x0, x0, :lo12:.LC246
	str	x0, [x29,104]
.L3299:
	cmp	w24, w25
	beq	.L3331
	add	x0, x21, :lo12:.LANCHOR5
	ldr	x0, [x0,144]
	ldr	w2, [x0,w24,sxtw 2]
	cmn	w2, #1
	beq	.L3300
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,140]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3301
	ldr	x0, [x29,104]
	mov	w3, w24
	ldr	w1, [x29,140]
	bl	printk
.L3301:
	mov	w26, w24
.L3307:
	add	x1, x21, :lo12:.LANCHOR5
	sbfiz	x27, x26, 2, 32
	ldr	x0, [x1,144]
	ldr	w4, [x0,x27]
	cmn	w4, #1
	beq	.L3302
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,140]
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w2, w0, uxth
	bne	.L3302
	ldr	x0, [x1,152]
	str	x4, [x29,96]
	ldr	w28, [x0,x27]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x4, [x29,96]
	bne	.L3303
	mov	w0, w4
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x4, [x29,96]
.L3303:
	ldr	w3, [x29,156]
	cmp	w3, w28
	bne	.L3304
	add	x0, x21, :lo12:.LANCHOR5
	add	x1, x29, 152
	mov	w2, 1
	add	w22, w22, 1
	ldr	x0, [x0,160]
	ldr	w0, [x0,x27]
	str	w0, [x29,152]
	mov	w0, w4
	bl	pm_log2phys
	ldr	x0, [x29,120]
	mov	w2, 24
	add	x3, x0, :lo12:.LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1257]
	ldrh	w1, [x3,1232]
	sub	w0, w2, w0
	mov	w2, 1
	sub	w0, w0, w1
	lsr	w28, w28, w1
	lsl	w0, w2, w0
	sub	w0, w0, #1
	and	w28, w0, w28
	ldrb	w0, [x3,1234]
	udiv	w0, w28, w0
	b	.L3330
.L3304:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3306
	ldr	x0, [x29,112]
	mov	w1, w4
	mov	w2, w28
	bl	printk
.L3306:
	ldr	x0, [x29,128]
	ldrh	w0, [x0]
.L3330:
	bl	ftl_vpn_decrement
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, -1
	ldr	x0, [x0,144]
	str	w1, [x0,x27]
.L3302:
	add	w26, w26, 1
	cmp	w26, w25
	bne	.L3307
.L3300:
	add	w24, w24, 1
	b	.L3299
.L3331:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3309
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,1120]
	ldr	x2, [x0,1112]
	adrp	x0, .LC248
	add	x0, x0, :lo12:.LC248
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
.L3309:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w22
	beq	.L3310
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 898
	add	x1, x1, 824
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3310:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x19,1112]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20,80]
	bl	zftl_insert_data_list
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	gc_update_l2p_map_new, .-gc_update_l2p_map_new
	.align	2
	.global	gc_scan_src_blk_one_page
	.type	gc_scan_src_blk_one_page, %function
gc_scan_src_blk_one_page:
	stp	x29, x30, [sp, -80]!
	mov	w3, 0
	mov	w5, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	w4, w3
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 3424
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR3
	ldrb	w0, [x1,4]
	add	x2, x1, x0, sxtw 1
	ldrh	w22, [x2,40]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w6, [x2,1249]
	ldrh	w2, [x1,2]
.L3333:
	cmp	w22, w5
	bne	.L3362
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w6
	bne	.L3334
	add	w2, w2, 1
	mov	w0, 0
	mov	w4, 1
	uxth	w2, w2
.L3334:
	add	x3, x1, x0, sxtw 1
	ldrh	w22, [x3,40]
	mov	w3, 1
	b	.L3333
.L3362:
	cbz	w4, .L3336
	add	x1, x19, :lo12:.LANCHOR0
	strh	w2, [x1,3426]
.L3336:
	cbz	w3, .L3337
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,3428]
.L3337:
	mov	w0, 1
	mov	w24, 1
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x23, x0
.L3338:
	add	x20, x19, 3424
	ldrb	w0, [x20,6]
	cmp	w24, w0
	bgt	.L3363
	add	x3, x21, :lo12:.LANCHOR3
	cmp	w0, 2
	ldrh	w1, [x3,1330]
	mul	w1, w22, w1
	bne	.L3339
	ldrh	w2, [x20,2]
	ldrb	w0, [x3,1248]
	add	w1, w1, w2, lsl 1
	sub	w1, w1, #1
	add	w1, w1, w24
	b	.L3361
.L3339:
	cmp	w0, 3
	bne	.L3341
	ldrb	w0, [x19,1268]
	cbnz	w0, .L3342
	ldrb	w0, [x19,1269]
	cbz	w0, .L3343
.L3342:
	ldrh	w2, [x19,3426]
	add	x0, x21, :lo12:.LANCHOR3
	add	w2, w2, w2, lsl 1
	ldrb	w0, [x0,1248]
	sub	w2, w2, #1
	add	w1, w2, w1
	add	w1, w1, w24
.L3361:
	orr	w1, w1, w0, lsl 24
	b	.L3360
.L3343:
	ldrh	w0, [x20,2]
	add	w1, w1, w0
	orr	w1, w1, w24, lsl 24
	b	.L3360
.L3341:
	ldrh	w0, [x20,2]
	add	w1, w1, w0
.L3360:
	str	w1, [x23,40]
	mov	x0, x23
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L3346
	cmn	w0, #1
	beq	.L3346
	ldr	x0, [x23,24]
	ldr	w20, [x0,4]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L3347
	ldr	w0, [x19,3372]
	cmp	w20, w0
	bcs	.L3347
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L3347:
	ldr	w2, [x23,40]
	ldr	w0, [x29,76]
	cmp	w2, w0
	bne	.L3346
	add	x1, x21, :lo12:.LANCHOR3
	ldrh	w3, [x19,3448]
	ldr	x1, [x1,1240]
	str	w2, [x1,x3,lsl 2]
	ldrh	w1, [x19,3448]
	add	w1, w1, 1
	strh	w1, [x19,3448]
.L3346:
	ldrh	w0, [x19,3450]
	add	w24, w24, 1
	add	w0, w0, 1
	strh	w0, [x19,3450]
	b	.L3338
.L3363:
	mov	x0, x23
	add	x21, x21, :lo12:.LANCHOR3
	bl	zbuf_free
	ldrb	w0, [x20,4]
	ldrb	w1, [x21,1249]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x20,4]
	cmp	w1, w0
	bne	.L3332
	ldrh	w0, [x20,2]
	strb	wzr, [x20,4]
	add	w0, w0, 1
	strh	w0, [x20,2]
.L3332:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x27, [sp,80]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w1, [x0,3424]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3365
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1504
	add	x1, x1, 848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3365:
	add	x2, x20, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,3424]
	cmp	w1, w3
	beq	.L3366
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3367
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,1112]
	adrp	x0, .LC249
	add	x0, x0, :lo12:.LC249
	ldrh	w2, [x2,x3]
	bl	printk
.L3367:
	add	x19, x20, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x19, 3424
	add	x1, x19, 3464
	ldrh	w0, [x19,3424]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L3368
	mov	w0, -1
	strh	w0, [x19,3424]
	b	.L3420
.L3368:
	ldrh	w3, [x19,3424]
	ldr	x2, [x19,1096]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L3369
	cmp	w4, 224
	beq	.L3369
	ldr	x2, [x19,1120]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L3369
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L3369
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L3370
.L3369:
	cbnz	w4, .L3371
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L3372
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1529
	add	x1, x1, 848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3372:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3424]
	ldr	x0, [x0,1112]
	strh	wzr, [x0,x1,lsl 1]
.L3371:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x20,3448]
	strh	w0, [x20,3424]
.L3420:
	mov	w0, 0
	b	.L3366
.L3370:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w21, [x0,40]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L3373
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1539
	add	x1, x1, 848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3373:
	add	x3, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x1, x22, :lo12:.LANCHOR3
	mov	w26, 1
	ldrh	w2, [x3,3424]
	ldr	x3, [x3,1096]
	ldrh	w0, [x1,1294]
	add	x2, x3, x2, lsl 2
	ldrh	w23, [x1,1330]
	sub	w0, w0, #1
	ldrb	w2, [x2,2]
	uxth	w0, w0
	mul	w23, w21, w23
	and	w2, w2, 224
	cmp	w2, 160
	bne	.L3374
	ldrb	w2, [x1,1248]
	cmp	w2, 2
	uxth	w26, w2
	orr	w23, w23, w2, lsl 24
	bne	.L3375
	ldrh	w0, [x1,1266]
	sub	w0, w0, #1
	uxth	w0, w0
.L3375:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,1268]
	cbnz	w2, .L3376
	ldrb	w1, [x1,1269]
	cbz	w1, .L3374
.L3376:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w0, [x0,1266]
	sub	w0, w0, #1
	uxth	w0, w0
.L3374:
	add	x24, x20, :lo12:.LANCHOR0
	orr	w23, w0, w23
	mov	w0, 1
	strb	w26, [x24,3430]
	strh	wzr, [x24,3426]
	strb	wzr, [x24,3428]
	strh	wzr, [x24,3450]
	strh	wzr, [x24,3452]
	bl	buf_alloc
	mov	w1, 1
	mov	x19, x0
	str	w23, [x0,40]
	bl	sblk_read_page
	strh	wzr, [x24,3448]
	ldr	w0, [x19,52]
	cmp	w0, 512
	beq	.L3423
	cmn	w0, #1
	beq	.L3423
	ldr	x0, [x19,24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3379
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 848
	mov	w2, 1577
	b	.L3424
.L3379:
	add	x2, x22, :lo12:.LANCHOR3
	cmp	w26, 3
	ldrb	w1, [x2,1249]
	ldrh	w25, [x2,1294]
	mul	w25, w1, w25
	mul	w25, w26, w25
	uxth	w25, w25
	bne	.L3380
	adrp	x26, .LANCHOR5
	add	x3, x26, :lo12:.LANCHOR5
	ldrb	w4, [x3,929]
	cmp	w4, 2
	bne	.L3380
	ldrh	w24, [x2,1266]
	ldrh	w0, [x3,926]
	lsl	w24, w24, 2
	mul	w24, w1, w24
	ldr	x1, [x2,1240]
	sub	w24, w24, w0
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,8]
	uxth	w24, w24
	mov	w2, w24
	bl	ftl_memcpy
	ldr	x0, [x19,24]
	ldr	w27, [x0,4]
	cbz	w27, .L3381
	ldr	x0, [x19,8]
	mov	w1, w24
	bl	js_hash
	cmp	w27, w0
	beq	.L3381
	mov	x0, x19
	bl	zbuf_free
	ldr	x1, [x19,24]
	adrp	x0, .LC250
	add	x0, x0, :lo12:.LC250
	ldr	w2, [x19,40]
	mov	w3, w24
	ldr	w1, [x1,4]
	b	.L3422
.L3381:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L3382
	ldrb	w0, [x0,1269]
	cbz	w0, .L3383
.L3382:
	sub	w0, w23, #1
	str	w0, [x19,40]
	b	.L3384
.L3383:
	add	x1, x22, :lo12:.LANCHOR3
	ldrh	w0, [x1,1294]
	ldrh	w1, [x1,1330]
	sub	w0, w0, #1
	mul	w21, w21, w1
	uxth	w0, w0
	orr	w0, w0, 33554432
	orr	w21, w0, w21
	str	w21, [x19,40]
.L3384:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x19,52]
	cmp	w0, 512
	beq	.L3423
	cmn	w0, #1
	beq	.L3423
	ldr	x0, [x19,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3387
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 848
	mov	w2, 1618
.L3424:
	bl	printk
	bl	dump_stack
.L3423:
	mov	x0, x19
	bl	zbuf_free
	b	.L3421
.L3387:
	add	x0, x22, :lo12:.LANCHOR3
	add	x26, x26, :lo12:.LANCHOR5
	ldr	x1, [x19,8]
	ldr	x0, [x0,1240]
	ldrh	w2, [x26,926]
	b	.L3419
.L3380:
	ldr	w23, [x0,4]
	lsl	w21, w25, 2
	ldr	x0, [x19,8]
	mov	w1, w21
	bl	js_hash
	cmp	w23, w0
	beq	.L3389
	mov	x0, x19
	bl	zbuf_free
	ldr	x1, [x19,24]
	adrp	x0, .LC250
	ldr	w2, [x19,40]
	add	x0, x0, :lo12:.LC250
	mov	w3, w21
	ldr	w1, [x1,4]
.L3422:
	bl	printk
.L3421:
	mov	w0, -1
	b	.L3366
.L3389:
	add	x0, x22, :lo12:.LANCHOR3
	ldr	x1, [x19,8]
	mov	w2, w21
	ldr	x0, [x0,1240]
.L3419:
	bl	ftl_memcpy
	mov	x27, 0
	add	x0, x22, :lo12:.LANCHOR3
	add	x21, x20, :lo12:.LANCHOR0
	mov	w24, 24
	mov	w26, 1
	ldr	x23, [x0,1240]
.L3390:
	cmp	w25, w27
	ble	.L3425
	ldr	w0, [x23,x27,lsl 2]
	cmn	w0, #1
	beq	.L3392
	bl	lpa_hash_get_ppa
	str	w0, [x29,108]
	cmn	w0, #1
	bne	.L3393
	ldr	w0, [x23,x27,lsl 2]
	add	x1, x29, 108
	mov	w2, 0
	bl	pm_log2phys
.L3393:
	add	x2, x22, :lo12:.LANCHOR3
	ldrb	w0, [x21,1257]
	ldr	w3, [x29,108]
	sub	w0, w24, w0
	ldrh	w4, [x21,3424]
	ldrh	w1, [x2,1232]
	sub	w0, w0, w1
	lsr	w1, w3, w1
	lsl	w0, w26, w0
	sub	w0, w0, #1
	and	w0, w0, w1
	ldrb	w1, [x2,1234]
	udiv	w0, w0, w1
	cmp	w0, w4
	bne	.L3392
	ldrh	w4, [x21,3448]
	ldr	x0, [x2,1240]
	str	w3, [x0,x4,lsl 2]
	ldrh	w0, [x21,3448]
	add	w0, w0, 1
	strh	w0, [x21,3448]
.L3392:
	bl	timer_get_time
	add	x27, x27, 1
	b	.L3390
.L3425:
	mov	x0, x19
	bl	zbuf_free
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3424]
	ldr	x3, [x0,1112]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,3448]
	cmp	w2, w3
	beq	.L3396
	adrp	x0, .LC251
	add	x0, x0, :lo12:.LC251
	bl	printk
.L3396:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x20,3424]
	ldr	x1, [x20,1112]
	ldrh	w3, [x20,3448]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x20,3452]
	ldrh	w0, [x20,3448]
.L3366:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	gc_scan_src_blk, .-gc_scan_src_blk
	.align	2
	.global	gc_scan_static_data
	.type	gc_scan_static_data, %function
gc_scan_static_data:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x3, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x3,1120]
	ldr	w1, [x0,544]
	cmn	w1, #1
	beq	.L3427
	adrp	x20, .LANCHOR4
	adrp	x22, .LC0
	add	x20, x20, :lo12:.LANCHOR4
	mov	w21, 0
	add	x22, x22, :lo12:.LC0
	add	x20, x20, 864
.L3428:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,1120]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L3429
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L3430
	adrp	x4, .LANCHOR3
	ldrb	w2, [x24,1257]
	add	x4, x4, :lo12:.LANCHOR3
	mov	w3, 24
	sub	w2, w3, w2
	mov	w1, 1
	ldr	w3, [x29,76]
	ldrh	w0, [x4,1232]
	sub	w2, w2, w0
	lsr	w3, w3, w0
	lsl	w2, w1, w2
	ldrb	w0, [x4,1234]
	sub	w2, w2, #1
	and	w2, w2, w3
	udiv	w0, w2, w0
	mov	w2, 0
	bl	gc_add_sblk
.L3430:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,1120]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L3431
	mov	x1, x20
	mov	w2, 2162
	mov	x0, x22
	bl	printk
	bl	dump_stack
.L3431:
	mov	x0, x23
	bl	zbuf_free
.L3429:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,1120]
	ldr	w2, [x2,3372]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L3432
	mov	w0, -1
	str	w0, [x1,544]
	ldr	w0, [x1,548]
	add	w0, w0, 1
	str	w0, [x1,548]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3426
.L3432:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L3426
	adrp	x1, .LANCHOR5+926
	add	w0, w21, 1
	uxth	w0, w0
	ldrh	w1, [x1,#:lo12:.LANCHOR5+926]
	cmp	w21, w1, lsr 2
	bcs	.L3426
	mov	w21, w0
	b	.L3428
.L3427:
	ldr	w1, [x0,536]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L3437
	ldr	x3, [x3,3392]
	ldr	w1, [x0,540]
	ldr	w3, [x3,44]
	add	w1, w1, 4096
	add	w1, w1, 904
	cmp	w3, w1
	bls	.L3426
.L3437:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w2, [x0,536]
	str	wzr, [x0,544]
.L3426:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_scan_static_data, .-gc_scan_static_data
	.align	2
	.global	gc_block_vpn_scan
	.type	gc_block_vpn_scan, %function
gc_block_vpn_scan:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x1, [x20,1120]
	ldrh	w0, [x20,1088]
	ldr	w1, [x1,608]
	cmp	w1, w0
	bcs	.L3442
	bl	timer_get_time
	ldr	x21, [x20,1120]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L3442
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,1120]
	ldrh	w2, [x20,1088]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L3446
	ldr	x2, [x20,3392]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L3447
.L3446:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L3447:
	ldr	w26, [x0,600]
	mov	w0, 65535
	uxth	w20, w26
	cmp	w20, w0
	bne	.L3448
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2503
	add	x1, x1, 888
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3448:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,1120]
	ldr	w1, [x0,600]
	add	w1, w1, 1
	str	w1, [x0,600]
	ldr	w1, [x0,608]
	add	w1, w1, 1
	str	w1, [x0,608]
	add	x1, x29, 128
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	uxth	w1, w0
	cbz	w1, .L3442
	uxtw	x22, w20
	ldr	x24, [x21,1096]
	add	x24, x24, x22, lsl 2
	ldrb	w1, [x24,2]
	tst	w1, 192
	and	w2, w1, 224
	beq	.L3449
	cmp	w2, 224
	beq	.L3449
	ldr	x1, [x21,1120]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L3449
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L3449
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L3450
.L3449:
	cbnz	w2, .L3442
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x22]
	cbz	w0, .L3451
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2520
	add	x1, x1, 888
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3451:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,1112]
	strh	wzr, [x0,x22]
	b	.L3442
.L3450:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L3453
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2528
	add	x1, x1, 888
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3453:
	adrp	x25, .LANCHOR3
	ldrb	w3, [x24,2]
	add	x2, x25, :lo12:.LANCHOR3
	mov	w27, 1
	and	w3, w3, 224
	cmp	w3, 160
	ldrh	w0, [x2,1294]
	ldrh	w1, [x2,1330]
	sub	w0, w0, #1
	mul	w1, w21, w1
	uxth	w0, w0
	bne	.L3454
	ldrb	w3, [x2,1248]
	cmp	w3, 2
	uxth	w27, w3
	orr	w1, w1, w3, lsl 24
	bne	.L3454
	ldrh	w0, [x2,1266]
	sub	w0, w0, #1
	uxth	w0, w0
.L3454:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x25, :lo12:.LANCHOR3
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,1266]
	mov	w1, 255
	ldrb	w2, [x21,1249]
	mul	w2, w0, w2
	ldr	x0, [x21,1240]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L3466
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L3455
.L3466:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L3479
.L3455:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3457
	mov	w0, w20
	mov	w1, 1
.L3479:
	bl	gc_add_sblk
	mov	x0, x23
	bl	zbuf_free
	b	.L3442
.L3457:
	ldrb	w0, [x21,1249]
	mov	x28, 0
	ldrh	w1, [x21,1294]
	mov	w21, w28
	mul	w0, w0, w1
	mul	w27, w27, w0
	uxth	w0, w27
	ldr	x27, [x23,8]
	str	w0, [x29,108]
	and	w0, w26, 65535
	str	w0, [x29,104]
.L3458:
	ldr	w0, [x29,108]
	cmp	w0, w28
	ble	.L3480
	ldr	w0, [x27,x28,lsl 2]
	cmn	w0, #1
	beq	.L3459
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L3460
	ldr	w0, [x27,x28,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L3460:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x25, :lo12:.LANCHOR3
	mov	w1, 24
	ldrb	w0, [x0,1257]
	ldrh	w4, [x2,1232]
	sub	w0, w1, w0
	mov	w1, 1
	sub	w0, w0, w4
	lsl	w0, w1, w0
	ldr	w1, [x29,124]
	sub	w0, w0, #1
	lsr	w1, w1, w4
	and	w0, w0, w1
	ldrb	w1, [x2,1234]
	udiv	w0, w0, w1
	ldr	w1, [x29,104]
	cmp	w0, w1
	bne	.L3459
	add	w21, w21, 1
	uxth	w21, w21
.L3459:
	add	x28, x28, 1
	b	.L3458
.L3480:
	mov	x0, x23
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3462
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w26
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,1112]
	adrp	x0, .LC252
	add	x0, x0, :lo12:.LC252
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L3462:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,1112]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L3442
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L3442:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	gc_block_vpn_scan, .-gc_block_vpn_scan
	.align	2
	.global	ftl_sblk_dump
	.type	ftl_sblk_dump, %function
ftl_sblk_dump:
	sub	sp, sp, #272
	uxth	x0, w0
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x19, x20, [sp,64]
	adrp	x20, .LANCHOR0
	str	x0, [x29,128]
	add	x19, x20, :lo12:.LANCHOR0
	str	x1, [x29,160]
	stp	x21, x22, [sp,80]
	stp	x23, x24, [sp,96]
	stp	x27, x28, [sp,128]
	stp	x25, x26, [sp,112]
	lsl	x24, x0, 2
	mov	x22, x0
	ldr	x0, [x19,1096]
	mov	x28, x20
	add	x1, x0, x24
	ldrh	w4, [x0,x24]
	ldr	w5, [x0,x24]
	adrp	x0, .LC253
	ldrb	w3, [x1,2]
	add	x0, x0, :lo12:.LC253
	mov	w1, w22
	and	w4, w4, 2047
	ubfx	x2, x3, 5, 3
	ubfx	x5, x5, 11, 8
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w1, 65535
	mov	w0, 0
	cmp	w22, w1
	beq	.L3482
	ldrh	w1, [x19,1088]
	cmp	w1, w22
	bls	.L3482
	ldr	x0, [x19,1096]
	mov	w25, 1
	adrp	x27, .LANCHOR3
	add	x0, x0, x24
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L3483
	add	x0, x27, :lo12:.LANCHOR3
	ldrb	w25, [x0,1248]
.L3483:
	add	x1, x29, 208
	mov	w0, w22
	strh	w22, [x29,192]
	mov	w23, 0
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	add	x0, x27, :lo12:.LANCHOR3
	strb	w1, [x29,201]
	mov	w3, w25
	strh	wzr, [x29,194]
	uxtb	w4, w4
	ldrh	w0, [x0,1294]
	mov	w20, w23
	strb	wzr, [x29,197]
	strh	wzr, [x29,202]
	mul	w1, w1, w0
	add	x0, x28, :lo12:.LANCHOR0
	strh	w1, [x29,198]
	ldr	x1, [x0,1096]
	adrp	x0, .LC254
	add	x0, x0, :lo12:.LC254
	add	x1, x1, x24
	ldrb	w2, [x1,2]
	mov	w1, w22
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x26, x0
	adrp	x0, .LC199
	str	w23, [x29,168]
	add	x0, x0, :lo12:.LC199
	str	x0, [x29,120]
	adrp	x0, .LANCHOR4
	str	w23, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 912
	str	x0, [x29,112]
.L3484:
	add	x0, x27, :lo12:.LANCHOR3
	ldrh	w0, [x0,1294]
	cmp	w0, w20
	bls	.L3502
	lsl	w0, w20, 1
	str	wzr, [x29,172]
	add	w1, w0, w20
	sub	w0, w0, #1
	sub	w1, w1, #1
	str	w0, [x29,136]
	str	w1, [x29,140]
.L3503:
	ldrb	w0, [x29,201]
	ldr	w1, [x29,172]
	cmp	w0, w1
	bls	.L3500
	mov	w21, 1
.L3501:
	cmp	w21, w25
	ldr	w0, [x29,172]
	bhi	.L3522
	add	x1, x29, 192
	add	x0, x1, x0, sxtw 1
	ldrh	w3, [x0,16]
	mov	w0, 65535
	cmp	w3, w0
	beq	.L3485
	add	x0, x27, :lo12:.LANCHOR3
	cmp	w25, 3
	ldrh	w2, [x0,1330]
	mul	w2, w3, w2
	bne	.L3486
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w4, [x1,1268]
	cbz	w4, .L3487
	ldr	w1, [x29,140]
	b	.L3520
.L3487:
	ldrb	w0, [x1,1269]
	lsl	w19, w21, 24
	cbz	w0, .L3489
	ldr	w0, [x29,140]
	add	w2, w2, w0
	add	w2, w2, w21
	b	.L3521
.L3489:
	add	w2, w2, w20
.L3521:
	orr	w19, w19, w2
	b	.L3488
.L3486:
	cmp	w25, 2
	add	w19, w2, w20
	bne	.L3488
	ldr	w1, [x29,136]
.L3520:
	ldrb	w19, [x0,1248]
	add	w2, w2, w1
	add	w2, w2, w21
	orr	w19, w2, w19, lsl 24
.L3488:
	mov	w1, 1
	str	w19, [x26,40]
	mov	x0, x26
	str	x3, [x29,104]
	bl	sblk_read_page
	ldr	x7, [x26,8]
	mov	w2, w20
	ldr	x0, [x26,24]
	ldr	w9, [x26,52]
	ldr	w1, [x7,12]
	str	w1, [sp]
	mov	w4, w9
	ldr	x3, [x29,104]
	ldr	w1, [x0]
	str	w1, [sp,8]
	str	x9, [x29,104]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w3
	mov	w3, w19
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	ldr	x0, [x29,120]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x9, [x29,104]
	cmp	w9, 512
	beq	.L3507
	cmn	w9, #1
	bne	.L3491
.L3507:
	mov	w0, 1
	str	w0, [x29,168]
.L3491:
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	add	x0, x28, :lo12:.LANCHOR0
	ldr	x0, [x0,1096]
	add	x0, x0, x24
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L3485
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L3485
	ldr	x0, [x26,24]
	str	x2, [x29,104]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,188]
	cmn	w0, #1
	ldr	x2, [x29,104]
	bne	.L3493
	ldr	x0, [x26,24]
	add	x1, x29, 188
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L3493:
	ldr	w0, [x29,188]
	cmp	w0, w19
	bne	.L3494
	ldr	x2, [x26,24]
	add	w23, w23, 1
	adrp	x0, .LC255
	mov	w1, w19
	add	x0, x0, :lo12:.LC255
	mov	w3, w23
	ldr	w2, [x2,4]
	bl	printk
.L3494:
	ldr	x0, [x29,160]
	cbz	x0, .L3496
	ldr	x0, [x29,144]
	ubfiz	x19, x0, 2, 32
	ldr	x0, [x29,160]
	ldr	w2, [x0,x19]
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L3497
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3497
	adrp	x0, .LC256
	ldr	w1, [x29,144]
	add	x0, x0, :lo12:.LC256
	bl	printk
.L3497:
	ldr	x0, [x29,160]
	ldr	x1, [x26,24]
	ldr	w0, [x0,x19]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3496
	cmn	w0, #1
	beq	.L3496
	adrp	x0, .LC0
	ldr	x1, [x29,112]
	mov	w2, 1567
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3496:
	ldr	w0, [x29,144]
	add	w0, w0, 1
	str	w0, [x29,144]
.L3485:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3501
.L3522:
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,172]
	b	.L3503
.L3500:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L3484
.L3502:
	mov	x0, x26
	bl	zbuf_free
	add	x0, x28, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	mov	w1, w22
	ldr	x2, [x0,1112]
	adrp	x0, .LC257
	add	x0, x0, :lo12:.LC257
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w23
	bl	printk
	ldr	w0, [x29,168]
.L3482:
	sub	sp, x29, #48
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 272
	ret
	.size	ftl_sblk_dump, .-ftl_sblk_dump
	.align	2
	.global	zftl_read
	.type	zftl_read, %function
zftl_read:
	sub	sp, sp, #192
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	mov	w19, w0
	adrp	x0, .LANCHOR2
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w20, w1
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	mov	w23, w2
	mov	x22, x3
	tbz	x0, 12, .L3524
	adrp	x0, .LC258
	mov	w1, w19
	add	x0, x0, :lo12:.LC258
	mov	w2, w20
	mov	w3, w23
	bl	printk
.L3524:
	cbnz	w19, .L3525
	adrp	x0, .LANCHOR0+1032
	mov	w19, 24576
	ldr	w1, [x0,#:lo12:.LANCHOR0+1032]
	b	.L3526
.L3525:
	cmp	w19, 3
	mov	w3, -1
	bhi	.L3527
	lsl	w19, w19, 13
	mov	w1, 8192
.L3526:
	cmp	w20, w1
	bcs	.L3556
	cmp	w23, w1
	cset	w0, hi
	cbnz	w0, .L3556
	add	w2, w20, w23
	mov	w3, -1
	cmp	w2, w1
	bhi	.L3527
	adrp	x21, .LANCHOR0
	add	w20, w19, w20
	add	x2, x21, :lo12:.LANCHOR0
	str	w0, [x29,148]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,136]
	ldr	x3, [x2,3392]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
	str	x0, [x29,128]
	ldr	w1, [x3,24]
	add	w1, w1, w23
	str	w1, [x3,24]
	ldrb	w1, [x2,3360]
	udiv	w2, w20, w1
	str	w2, [x29,156]
	add	w2, w20, w23
	str	w2, [x29,152]
	sub	w2, w2, #1
	udiv	w1, w2, w1
	ldr	w2, [x29,156]
	str	w1, [x29,144]
	sub	w25, w1, w2
	mov	x19, x2
	add	w25, w25, 1
.L3528:
	cbz	w25, .L3594
	ldr	w1, [x29,156]
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w19, w1
	ldr	w1, [x29,144]
	ldrb	w0, [x0,3360]
	cset	w2, eq
	cmp	w19, w1
	cset	w26, eq
	uxth	w24, w0
	cbnz	w26, .L3560
	cbz	w2, .L3529
	b	.L3593
.L3560:
	cbz	w2, .L3531
.L3593:
	udiv	w26, w20, w0
	msub	w26, w26, w0, w20
	uxth	w0, w23
	uxth	w26, w26
	sub	w24, w24, w26
	uxth	w24, w24
	cmp	w24, w23
	csel	w24, w0, w24, hi
	b	.L3529
.L3531:
	ldr	w1, [x29,152]
	mov	w26, w2
	msub	w24, w0, w19, w1
	uxtb	w24, w24
.L3529:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 1312
.L3534:
	ldr	w2, [x1,36]
	cmp	w2, w19
	bne	.L3532
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L3532
	add	x2, x21, :lo12:.LANCHOR0
	ubfiz	x1, x0, 6, 32
	add	x0, x2, x1
	ubfiz	x26, x26, 9, 16
	lsl	w2, w24, 9
	ubfiz	x24, x24, 9, 16
	ldr	x1, [x0,1320]
	mov	x0, x22
	add	x22, x22, x24
	add	x1, x1, x26
	bl	ftl_memcpy
	b	.L3533
.L3532:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3534
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L3535
	mov	w0, w19
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L3535:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L3536
	mov	w24, 0
.L3537:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w24, w0
	bcs	.L3533
	madd	w0, w19, w0, w24
	cmp	w0, w20
	bcc	.L3538
	ldr	w1, [x29,152]
	cmp	w0, w1
	bcs	.L3538
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	add	x22, x22, 512
	bl	ftl_memset
.L3538:
	add	w24, w24, 1
	b	.L3537
.L3536:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3540
	add	x2, x21, :lo12:.LANCHOR0
	ldr	x3, [x2,3392]
	ldr	w2, [x3,40]
	add	w2, w2, 1
	str	w2, [x3,40]
	ldr	w2, [x29,172]
	strb	w24, [x0,56]
	ubfiz	x24, x24, 9, 16
	str	x22, [x0,16]
	add	x22, x22, x24
	str	w2, [x0,40]
	str	w2, [x0,44]
	str	w19, [x0,36]
	strb	w26, [x0,57]
	bl	zftl_add_read_buf
.L3533:
	add	w19, w19, 1
	sub	w25, w25, #1
.L3540:
	cbz	w25, .L3561
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3362]
	cmp	w0, 2
	bhi	.L3528
.L3561:
	adrp	x24, .LANCHOR3
	add	x2, x24, :lo12:.LANCHOR3
	ldrb	w1, [x2,1875]
	cbz	w1, .L3528
	ldrb	w2, [x2,1874]
	add	x26, x21, :lo12:.LANCHOR0
	add	x0, x26, 1312
	mov	x27, x26
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
.L3543:
	add	x0, x24, :lo12:.LANCHOR3
	ldrb	w1, [x0,1875]
	cbz	w1, .L3595
	ldrb	w0, [x0,1874]
	cmp	w0, 255
	bne	.L3544
	ldr	x1, [x29,128]
	mov	w2, 1276
	ldr	x0, [x29,136]
	bl	printk
	bl	dump_stack
.L3544:
	add	x2, x24, :lo12:.LANCHOR3
	add	x0, x27, 1312
	ldrb	w28, [x2,1874]
	lsl	x1, x28, 6
	add	x26, x0, x1
	ldrb	w0, [x0,x1]
	ldr	w5, [x26,52]
	strb	w0, [x2,1874]
	cmn	w5, #1
	bne	.L3545
	ldr	x1, [x27,1120]
	str	w5, [x29,148]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L3546
.L3545:
	cmp	w5, 256
	bne	.L3546
	ldrb	w4, [x27,1257]
	mov	w1, 24
	ldrh	w0, [x2,1232]
	mov	w6, 1
	sub	w4, w1, w4
	ldr	w3, [x26,40]
	sub	w4, w4, w0
	str	x5, [x29,104]
	lsl	w4, w6, w4
	lsr	w0, w3, w0
	sub	w4, w4, #1
	str	x6, [x29,112]
	and	w0, w4, w0
	ldrb	w4, [x2,1234]
	ldr	w2, [x26,36]
	udiv	w4, w0, w4
	adrp	x0, .LC259
	add	x0, x0, :lo12:.LC259
	str	x4, [x29,120]
	uxth	w1, w4
	bl	printk
	ldr	x4, [x29,120]
	mov	w2, 0
	ldr	x6, [x29,112]
	mov	w0, w4
	mov	w1, w6
	bl	gc_add_sblk
	ldr	x5, [x29,104]
.L3546:
	add	x0, x27, 1312
	cmn	w5, #1
	add	x0, x0, x28, lsl 6
	cset	w11, eq
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3562
	cbz	w11, .L3547
.L3562:
	ldr	x1, [x27,1120]
	add	x10, x27, 1312
	add	x10, x10, x28, lsl 6
	mov	w2, w5
	add	x10, x10, 32
	str	x11, [x29,112]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	ldr	x7, [x10,-8]
	ldrb	w1, [x10,-31]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC260
	add	x0, x0, :lo12:.LC260
	ldr	w3, [x10,4]
	ldr	w4, [x10,8]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	str	x10, [x29,120]
	bl	printk
	add	x3, x24, :lo12:.LANCHOR3
	ldrb	w2, [x27,1257]
	mov	w1, 24
	ldr	x10, [x29,120]
	sub	w2, w1, w2
	mov	w1, 1
	ldrh	w0, [x3,1232]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x10,8]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,1234]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x11, [x29,112]
.L3547:
	add	x0, x27, 1312
	add	x0, x0, x28, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3563
	cbz	w11, .L3549
.L3563:
	ldr	x1, [x29,128]
	mov	w2, 1301
	ldr	x0, [x29,136]
	bl	printk
	bl	dump_stack
.L3549:
	add	x0, x27, 1312
	ldrb	w1, [x27,3360]
	add	x9, x0, x28, lsl 6
	ldrb	w2, [x9,56]
	cmp	w1, w2
	bls	.L3551
	ldrb	w3, [x9,57]
	lsl	w2, w2, 9
	ldr	x1, [x9,8]
	ldr	x0, [x9,16]
	add	x1, x1, x3, lsl 9
	bl	ftl_memcpy
	b	.L3552
.L3551:
	ldrb	w0, [x9,2]
	and	w0, w0, -9
	strb	w0, [x9,2]
.L3552:
	mov	x1, x26
	add	x0, x27, 3364
	bl	buf_remove_buf
	mov	x0, x26
	bl	zbuf_free
	add	x1, x24, :lo12:.LANCHOR3
	ldrb	w0, [x1,1875]
	sub	w0, w0, #1
	strb	w0, [x1,1875]
	b	.L3543
.L3595:
	mov	w2, -1
	strb	w1, [x0,1875]
	strb	w2, [x0,1874]
	b	.L3528
.L3594:
	bl	timer_get_time
	adrp	x1, .LANCHOR5+932
	ldr	w3, [x29,148]
	str	w0, [x1,#:lo12:.LANCHOR5+932]
	b	.L3527
.L3556:
	mov	w3, -1
.L3527:
	sub	sp, x29, #16
	mov	w0, w3
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 192
	ret
	.size	zftl_read, .-zftl_read
	.align	2
	.global	zftl_vendor_read
	.type	zftl_vendor_read, %function
zftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	mov	w4, w1
	mov	x3, x2
	add	x29, sp, 0
	add	w1, w0, 512
	mov	w2, w4
	mov	w0, 2
	bl	zftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_vendor_read, .-zftl_vendor_read
	.align	2
	.global	zftl_sys_read
	.type	zftl_sys_read, %function
zftl_sys_read:
	stp	x29, x30, [sp, -16]!
	mov	w5, w0
	mov	w4, w1
	add	x29, sp, 0
	mov	x3, x2
	mov	w1, w5
	mov	w2, w4
	mov	w0, 2
	bl	zftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_sys_read, .-zftl_sys_read
	.align	2
	.type	zftl_debug_proc_write, %function
zftl_debug_proc_write:
	sub	sp, sp, #224
	cmp	x2, 79
	mov	x0, -22
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	stp	x21, x22, [sp,64]
	add	x19, x29, 112
	str	x19, [x29,104]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	mov	x21, x2
	bhi	.L3599
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x22, x0
	mov	x0, -14
	cbnz	x22, .L3599
	adrp	x0, .LC261
	mov	x1, x19
	add	x0, x0, :lo12:.LC261
	strb	w22, [x19,x21]
	bl	printk
	adrp	x0, .LC262
	mov	x1, x19
	add	x0, x0, :lo12:.LC262
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC263
	mov	x0, x19
	add	x1, x1, :lo12:.LC263
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3600
	adrp	x23, .LANCHOR0
	adrp	x0, .LC264
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC264
	mov	w2, 4
	adrp	x20, .LANCHOR5
	ldr	x3, [x19,3392]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x1, [x19,3392]
	adrp	x0, .LC265
	mov	w2, 2
	add	x0, x0, :lo12:.LC265
	add	x1, x1, 416
	ldrh	w3, [x3,176]
	mov	w19, w22
	mov	w22, 65535
	bl	rknand_print_hex
.L3601:
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w0, [x0,176]
	cmp	w19, w0
	bge	.L3604
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3602
	mov	x1, 0
	bl	ftl_sblk_dump
.L3602:
	add	w19, w19, 1
	b	.L3601
.L3600:
	adrp	x1, .LC266
	mov	x0, x19
	add	x1, x1, :lo12:.LC266
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3605
	adrp	x22, .LANCHOR0
	adrp	x0, .LC264
	add	x20, x22, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC264
	mov	w2, 4
	adrp	x26, .LC269
	add	x26, x26, :lo12:.LC269
	ldr	x3, [x20,3392]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	adrp	x3, .LANCHOR5+176
	ldr	x1, [x20,3392]
	adrp	x0, .LC265
	mov	w2, 2
	add	x0, x0, :lo12:.LC265
	add	x1, x1, 416
	ldrh	w3, [x3,#:lo12:.LANCHOR5+176]
	add	x20, x29, 192
	bl	rknand_print_hex
	adrp	x0, .LC267
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC267
	str	x1, [x20,-88]!
	mov	x19, x22
	adrp	x22, .LC268
	bl	printk
	add	x22, x22, :lo12:.LC268
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.35
	mov	w24, w0
	adrp	x2, .LANCHOR3
	str	w0, [x29,100]
	add	x2, x2, :lo12:.LANCHOR3
	uxth	w25, w0
	add	x20, x2, 1880
	add	x23, x2, 2392
.L3607:
	ldrh	w1, [x20]
	mov	x0, x22
	ldrh	w2, [x20,2]
	bl	printk
	ldrh	w0, [x20]
	cmp	w0, w25
	bne	.L3606
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x20,8]
	mov	w2, 4
	ldrb	w3, [x0,3360]
	mov	x0, x26
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L3606:
	add	x20, x20, 16
	cmp	x20, x23
	bne	.L3607
	mov	w0, 300
	add	x19, x19, :lo12:.LANCHOR0
	bl	msleep
	mov	w0, 1
	bl	buf_alloc
	mov	x20, x0
	ldr	x1, [x19,3392]
	add	x1, x1, x24, uxth 2
	ldr	w1, [x1,704]
	str	w1, [x29,100]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	x7, [x20,24]
	ldr	x6, [x20,8]
	ldr	w1, [x29,100]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC242
	add	x0, x0, :lo12:.LC242
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x20,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x19,3360]
	adrp	x0, .LC270
	ldr	x1, [x20,8]
	add	x0, x0, :lo12:.LC270
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	mov	x0, x20
	b	.L3622
.L3605:
	adrp	x1, .LC271
	mov	x0, x19
	add	x1, x1, :lo12:.LC271
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3608
	bl	dump_ftl_info
	b	.L3604
.L3608:
	adrp	x1, .LC272
	mov	x0, x19
	add	x1, x1, :lo12:.LC272
	mov	x2, 9
	bl	memcmp
	cbnz	w0, .L3609
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1,-88]!
	bl	rk_simple_strtoull.constprop.35
	str	w0, [x29,100]
	adrp	x1, .LANCHOR3+1328
	strh	w0, [x1,#:lo12:.LANCHOR3+1328]
	bl	dump_all_list_info
	b	.L3604
.L3609:
	adrp	x1, .LC273
	mov	x0, x19
	add	x1, x1, :lo12:.LC273
	mov	x2, 8
	bl	memcmp
	cbz	w0, .L3604
	adrp	x1, .LC274
	mov	x0, x19
	add	x1, x1, :lo12:.LC274
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3611
	add	x20, x29, 192
	adrp	x0, .LC267
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC267
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	bl	rk_simple_strtoull.constprop.35
	str	w0, [x29,100]
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldr	w1, [x29,100]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	x7, [x19,24]
	ldr	x6, [x19,8]
	ldr	w1, [x29,100]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC242
	add	x0, x0, :lo12:.LC242
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w3, [x6]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	ldr	w2, [x19,52]
	bl	printk
	ldrb	w3, [x20,3360]
	adrp	x0, .LC215
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC215
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,3360]
	adrp	x0, .LC237
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC237
	mov	w2, 4
	lsl	w3, w3, 1
	bl	rknand_print_hex
	mov	x0, x19
.L3622:
	bl	zbuf_free
	b	.L3604
.L3611:
	adrp	x1, .LC275
	mov	x0, x19
	add	x1, x1, :lo12:.LC275
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3612
	add	x20, x29, 192
	adrp	x0, .LC267
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC267
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.35
	str	w0, [x29,100]
	mov	x1, x22
	bl	ftl_sblk_dump
	b	.L3604
.L3612:
	adrp	x1, .LC276
	mov	x0, x19
	add	x1, x1, :lo12:.LC276
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3613
	add	x20, x29, 192
	adrp	x0, .LC267
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC267
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.35
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3604
.L3613:
	adrp	x1, .LC277
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC277
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3614
	add	x1, x19, 8
	adrp	x0, .LC267
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC267
	str	x1, [x19,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.35
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29,100]
	cmn	w0, #1
	ldr	x2, [x29,88]
	bne	.L3615
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3615:
	adrp	x0, .LC278
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC278
	bl	printk
	b	.L3604
.L3614:
	adrp	x0, .LC279
	add	x0, x0, :lo12:.LC279
	bl	printk
	adrp	x0, .LC280
	add	x0, x0, :lo12:.LC280
	bl	printk
	adrp	x0, .LC281
	add	x0, x0, :lo12:.LC281
	bl	printk
	adrp	x0, .LC282
	add	x0, x0, :lo12:.LC282
	bl	printk
	adrp	x0, .LC283
	add	x0, x0, :lo12:.LC283
	bl	printk
	adrp	x0, .LC284
	add	x0, x0, :lo12:.LC284
	bl	printk
	adrp	x0, .LC285
	add	x0, x0, :lo12:.LC285
	bl	printk
	adrp	x0, .LC286
	add	x0, x0, :lo12:.LC286
	bl	printk
	adrp	x0, .LC287
	add	x0, x0, :lo12:.LC287
	bl	printk
.L3604:
	bl	rknand_device_unlock
	mov	x0, x21
.L3599:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 224
	ret
	.size	zftl_debug_proc_write, .-zftl_debug_proc_write
	.align	2
	.global	ftl_update_l2p_map
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x22, x0
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	str	x0, [x29,120]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrb	w2, [x22,9]
	ldrh	w23, [x1,1294]
	ldrh	w19, [x22,12]
	ldr	x1, [x1,1848]
	mul	w23, w23, w2
	add	x19, x1, x19, lsl 2
	add	x1, x19, x23, sxtw 2
	ldr	w1, [x1,-4]
	cmn	w1, #1
	beq	.L3624
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1990
	add	x1, x1, 944
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3624:
	adrp	x0, .LC289
	mov	x28, 0
	add	x0, x0, :lo12:.LC289
	adrp	x27, .LC288
	str	x0, [x29,112]
	adrp	x0, .LC290
	add	x0, x0, :lo12:.LC290
	mov	w21, w28
	str	x0, [x29,104]
	add	x27, x27, :lo12:.LC288
.L3625:
	cmp	w28, w23
	mov	w24, w28
	bge	.L3652
	ldr	w2, [x19,x28,lsl 2]
	cmn	w2, #1
	beq	.L3626
	adrp	x25, .LANCHOR0
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w20, [x0,3360]
	adrp	x0, .LANCHOR2
	lsl	w20, w20, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w20, w2, w20
	and	w20, w20, 65535
	tbz	x0, 12, .L3630
	mov	x0, x27
	mov	w1, w20
	mov	w3, w28
	bl	printk
.L3630:
	sbfiz	x26, x24, 2, 32
	ldr	w0, [x19,x26]
	cmn	w0, #1
	beq	.L3628
	add	x1, x25, :lo12:.LANCHOR0
	ldrb	w1, [x1,3360]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w20, w0, uxth
	bne	.L3628
	ldrb	w0, [x22,9]
	ldr	x2, [x29,120]
	add	x3, x2, :lo12:.LANCHOR3
	str	x3, [x29,96]
	sdiv	w1, w24, w0
	ldrh	w2, [x3,1330]
	msub	w0, w1, w0, w24
	add	x0, x22, x0, sxtw 1
	ldrh	w0, [x0,16]
	madd	w2, w0, w2, w1
	str	w2, [x29,140]
	ldr	x0, [x29,112]
	ldr	w1, [x19,x26]
	bl	pm_ppa_update_check
	ldr	x3, [x29,96]
	cbz	w0, .L3629
	ldr	x1, [x3,1848]
	mov	w2, 4
	ldr	x0, [x29,104]
	mov	w3, w23
	bl	rknand_print_hex
.L3629:
	ldr	w0, [x19,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w21, w21, 1
	bl	pm_log2phys
	uxth	w21, w21
	mov	w0, -1
	str	w0, [x19,x26]
.L3628:
	add	w24, w24, 1
	cmp	w24, w23
	bne	.L3630
.L3626:
	add	x28, x28, 1
	b	.L3625
.L3652:
	adrp	x0, .LANCHOR2
	adrp	x19, .LANCHOR0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3632
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22]
	mov	w2, w21
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,1112]
	adrp	x0, .LC291
	add	x0, x0, :lo12:.LC291
	ldrh	w3, [x4,x3]
	bl	printk
.L3632:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22]
	ldr	x0, [x19,1112]
	strh	w21, [x0,x1,lsl 1]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ftl_update_l2p_map, .-ftl_update_l2p_map
	.align	2
	.global	ftl_alloc_new_data_sblk
	.type	ftl_alloc_new_data_sblk, %function
ftl_alloc_new_data_sblk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	ldrh	w21, [x0]
	bl	ftl_update_l2p_map
	bl	pm_flush
	ldrh	w0, [x20]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3654
	bl	zftl_insert_data_list
.L3654:
	adrp	x19, .LANCHOR0
	mov	w2, 2
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 3
	ldr	x0, [x19,1120]
	add	x0, x0, 16
	cmp	x20, x0
	mov	x0, x20
	csel	w1, w2, w1, eq
	bl	ftl_open_sblk_init
	ldr	x0, [x19,1120]
	ldr	w0, [x0,560]
	cmp	w0, w21
	bne	.L3656
	mov	w20, 65535
	cmp	w21, w20
	beq	.L3656
	adrp	x0, .LC292
	mov	w1, w21
	add	x0, x0, :lo12:.LC292
	bl	printk
	ldr	x0, [x19,1120]
	ldr	w0, [x0,564]
	bl	gc_mark_bad_ppa
	ldr	x0, [x19,1120]
	mov	w1, -1
	str	w20, [x0,560]
	str	w1, [x0,564]
.L3656:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	ldr	x21, [sp,32]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_alloc_new_data_sblk, .-ftl_alloc_new_data_sblk
	.align	2
	.global	ftl_write_commit
	.type	ftl_write_commit, %function
ftl_write_commit:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x26, .LANCHOR0
	add	x26, x26, :lo12:.LANCHOR0
	add	x0, x26, 1312
	str	x0, [x29,128]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	x0, [x29,136]
	stp	x19, x20, [sp,16]
	add	x0, x0, 264
	str	x0, [x29,120]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
.L3665:
	ldrb	w0, [x26,3388]
	adrp	x20, .LANCHOR0
	cbz	w0, .L3667
	ldrb	w1, [x26,3416]
	sub	w0, w0, #1
	ldr	x2, [x29,128]
	str	x1, [x29,144]
	lsl	x1, x1, 6
	add	x28, x2, x1
	strb	w0, [x26,3388]
	ldr	w0, [x26,3372]
	ldrb	w1, [x2,x1]
	strb	w1, [x26,3416]
	ldr	w1, [x28,36]
	cmp	w1, w0
	bcc	.L3668
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 604
	add	x1, x1, 968
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3668:
	ldr	x0, [x29,144]
	add	x1, x20, :lo12:.LANCHOR0
	add	x3, x1, 1312
	add	x0, x3, x0, lsl 6
	ldr	w2, [x1,3372]
	ldr	w23, [x0,36]
	cmp	w23, w2
	bcc	.L3669
	mov	x0, x28
	bl	zbuf_free
	mov	w0, -1
	b	.L3726
.L3669:
	ldr	x25, [x0,8]
	ldr	x24, [x0,24]
	ldrb	w22, [x0,57]
	ldrb	w21, [x0,56]
	ldrb	w0, [x1,3389]
	cbz	w0, .L3671
	ldr	x0, [x29,136]
	ldrb	w19, [x0,264]
	add	x19, x3, x19, lsl 6
.L3672:
	ldrb	w4, [x19]
	cmp	w4, 255
	beq	.L3728
	sbfiz	x19, x4, 6, 32
	add	x19, x3, x19
	b	.L3672
.L3728:
	ldr	w0, [x19,36]
	cmp	w0, w23
	bne	.L3671
	ldr	x0, [x19,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w21, 9
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, x1
	add	x1, x25, x1
	bl	ftl_memcpy
	ldrb	w2, [x20,3360]
	mov	w0, 2
	ldr	x1, [x19,24]
	ldr	x3, [x19,8]
	sub	w2, w2, #2
	sbfiz	x2, x2, 9, 32
	add	x1, x1, 16
	add	x2, x3, x2
	bl	ftl_debug_info_fill
	mov	x0, x28
	bl	zbuf_free
	b	.L3665
.L3671:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L3675
	mov	w0, w23
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L3675:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	w0, [x29,172]
	add	x3, x4, 1312
	mov	w1, 0
	mov	x27, 0
	ldr	x19, [x4,1120]
	add	x4, x4, 3360
	add	x19, x19, 16
.L3677:
	ldr	w2, [x3,36]
	cmp	w2, w23
	bne	.L3676
	ldrb	w6, [x3,2]
	tbz	x6, 3, .L3676
	ldr	x27, [x3,8]
	and	w6, w6, -9
	ldr	w0, [x3,40]
	mov	w1, 1
	strb	w6, [x3,2]
.L3676:
	add	x3, x3, 64
	cmp	x3, x4
	bne	.L3677
	cbz	w1, .L3678
	str	w0, [x29,172]
.L3678:
	add	x3, x20, :lo12:.LANCHOR0
	str	wzr, [x29,152]
	ldrb	w0, [x3,3360]
	cmp	w21, w0
	bcs	.L3679
	add	w21, w22, w21
	cbz	x27, .L3680
	cbz	w22, .L3681
	mov	x0, x25
	mov	x1, x27
	lsl	w2, w22, 9
	str	x3, [x29,152]
	bl	ftl_memcpy
	ldr	x3, [x29,152]
	ldr	x19, [x3,1120]
	add	x19, x19, 48
.L3681:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w2, [x0,3360]
	cmp	w21, w2
	bcc	.L3682
	ldr	x19, [x0,1120]
	add	x19, x19, 16
	b	.L3727
.L3682:
	ubfiz	x1, x21, 9, 9
	sub	w2, w2, w21
	add	x0, x25, x1
	lsl	w2, w2, 9
	add	x1, x27, x1
	bl	ftl_memcpy
.L3727:
	str	wzr, [x29,152]
	b	.L3679
.L3680:
	ldr	w0, [x29,172]
	cmn	w0, #1
	beq	.L3683
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	ldr	w1, [x29,172]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x0, [x27,24]
	ldr	w1, [x0,12]
	ldr	w0, [x0,4]
	add	w1, w1, 1
	str	w1, [x29,152]
	cmp	w0, w23
	bne	.L3684
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3685
.L3684:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,172]
	mov	w3, w23
	ldr	x1, [x0,1120]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	adrp	x0, .LC293
	ldrb	w1, [x27,1]
	add	x0, x0, :lo12:.LC293
	ldr	w4, [x27,52]
	bl	printk
	adrp	x0, .LC237
	mov	w2, 4
	ldr	x1, [x27,24]
	add	x0, x0, :lo12:.LC237
	mov	w3, w2
	bl	rknand_print_hex
.L3685:
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3686
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3687
.L3686:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 691
	add	x1, x1, 968
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3687:
	cbz	w22, .L3688
	ldr	w0, [x29,172]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3689
	ldr	x1, [x27,8]
	mov	x0, x25
	bl	ftl_memcpy
	b	.L3690
.L3683:
	cbz	w22, .L3688
	lsl	w2, w22, 9
.L3689:
	mov	x0, x25
	mov	w1, 0
	bl	ftl_memset
.L3690:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x19, [x0,1120]
	add	x19, x19, 48
.L3688:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w2, [x0,3360]
	cmp	w21, w2
	bcc	.L3691
	bls	.L3692
	ldr	x19, [x0,1120]
	add	x19, x19, 16
	b	.L3692
.L3691:
	ldr	w0, [x29,172]
	sub	w2, w2, w21
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x21, 7, 9
	beq	.L3693
	ldr	x1, [x27,8]
	lsl	x21, x0, 2
	add	x0, x25, x21
	add	x1, x1, x21
	bl	ftl_memcpy
	b	.L3692
.L3693:
	add	x0, x25, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3692:
	cbz	x27, .L3679
	ldrb	w0, [x27,2]
	mov	x1, x27
	and	w0, w0, -9
	strb	w0, [x27,2]
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 3364
	bl	buf_remove_buf
	mov	x0, x27
	bl	zbuf_free
.L3679:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3694
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3694:
	mov	x0, x19
	add	x21, x20, :lo12:.LANCHOR0
	bl	ftl_get_new_free_page
	mov	w6, w0
	ldr	x0, [x29,144]
	add	x4, x21, 1312
	mov	x1, x24
	str	x6, [x29,104]
	lsl	x27, x0, 6
	str	x4, [x29,112]
	add	x22, x4, x27
	add	x5, x22, 32
	str	x5, [x29,144]
	ldr	w0, [x22,32]
	str	w0, [x24]
	str	w23, [x24,4]
	ldrb	w2, [x21,3360]
	ldr	w0, [x29,172]
	str	w0, [x24,8]
	sub	w2, w2, #2
	ldr	w0, [x29,152]
	sbfiz	x2, x2, 9, 32
	str	w0, [x24,12]
	add	x2, x25, x2
	str	wzr, [x1,16]!
	mov	w0, 2
	bl	ftl_debug_info_fill
	ldr	x5, [x29,144]
	ldr	x4, [x29,112]
	ldr	w0, [x29,172]
	str	w0, [x5,12]
	mov	w0, -1
	ldrb	w1, [x22,2]
	strb	w0, [x4,x27]
	mov	w0, 10
	ldr	x6, [x29,104]
	orr	w0, w1, w0
	str	w6, [x5,8]
	strb	w0, [x22,2]
	ldrh	w1, [x19,10]
	ldrh	w0, [x19,12]
	add	w0, w1, w0
	mov	x1, x28
	sub	w0, w0, #1
	strh	w0, [x22,48]
	ldr	x0, [x29,120]
	bl	buf_add_tail
	ldrb	w0, [x21,3389]
	add	w0, w0, 1
	strb	w0, [x21,3389]
	bl	timer_get_time
	ldr	x1, [x29,136]
	ldrb	w2, [x21,3389]
	str	w0, [x1,268]
	cmp	w2, 2
	adrp	x0, .LANCHOR5
	bhi	.L3695
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L3667
.L3695:
	ldrb	w1, [x19,5]
	mov	w4, 2
	add	x6, x20, :lo12:.LANCHOR0
	cmp	w1, wzr
	ldrh	w1, [x19,6]
	csinc	w4, w4, wzr, ne
	add	x6, x6, 1312
	cmp	w1, 1
	add	x1, x0, :lo12:.LANCHOR5
	csel	w4, w4, w2, ne
	ldrb	w5, [x1,264]
	mov	w1, 0
	mov	w3, w5
.L3699:
	cmp	w1, w4
	beq	.L3729
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L3699
.L3729:
	add	x0, x0, :lo12:.LANCHOR5
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,264]
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 1312
	strb	w2, [x0,2077]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3667
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3667:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w19, [x20,3388]
	cbnz	w19, .L3665
	bl	ftl_write_completed
	mov	w0, w19
.L3726:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR3
	add	x1, x21, :lo12:.LANCHOR3
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w0, [x1,1265]
	ldr	x26, [x22,1120]
	cbnz	w0, .L3731
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3730
	add	x21, x22, 3424
	ldrh	w22, [x22,3450]
	mov	w0, w22
	add	w22, w22, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w23, w0
	mov	w1, 1
	mov	x0, x20
	strh	w22, [x21,26]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3816
	cmn	w0, #1
	bne	.L3733
.L3816:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1032
	add	x1, x1, 992
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3733:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,192]
	cmn	w0, #1
	bne	.L3735
	mov	w0, w21
	add	x1, x29, 192
	mov	w2, 0
	bl	pm_log2phys
.L3735:
	ldr	w2, [x29,192]
	cmp	w2, w23
	bne	.L3736
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1312
	add	x1, x1, 3360
.L3739:
	ldr	w3, [x0,36]
	cmp	w3, w21
	bne	.L3737
	ldrb	w3, [x0,2]
	tbz	x3, 1, .L3737
	mov	x0, x20
	str	x2, [x29,184]
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	x2, [x29,184]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3730
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC294
	mov	w1, w21
	add	x0, x0, :lo12:.LC294
	ldrh	w3, [x19,3450]
	bl	printk
	b	.L3730
.L3737:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3739
	add	x22, x19, :lo12:.LANCHOR0
	str	w21, [x20,36]
	strb	wzr, [x20,57]
	ldrb	w0, [x22,3360]
	strb	w0, [x20,56]
	ldr	x0, [x20,24]
	ldr	w1, [x0]
	str	w1, [x20,32]
	str	wzr, [x0,16]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3740
	mov	w0, w21
	str	x2, [x29,184]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,184]
	adrp	x0, .LC295
	ldrh	w5, [x22,3450]
	add	x0, x0, :lo12:.LC295
	mov	w1, w21
	mov	w4, w2
	bl	printk
.L3740:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,3392]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,3452]
	add	w0, w0, 1
	strh	w0, [x19,3452]
	b	.L3730
.L3736:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3742
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	str	x2, [x29,184]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,184]
	adrp	x0, .LC295
	ldrh	w5, [x19,3450]
	add	x0, x0, :lo12:.LC295
	mov	w1, w21
	mov	w4, w23
	bl	printk
.L3742:
	mov	x0, x20
	bl	zbuf_free
	b	.L3730
.L3731:
	add	x0, x26, 80
	str	x0, [x29,184]
	ldrb	w1, [x1,1249]
	mov	w24, 1
	ldrb	w0, [x0,9]
	uxth	w20, w0
	cmp	w1, w20
	bhi	.L3744
	cmp	w20, 2
	mov	w24, 2
	csel	w24, w20, w24, ls
.L3744:
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w3, [x2,1248]
	cmp	w3, 3
	bne	.L3745
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w4, [x1,1268]
	cbz	w4, .L3746
	ldrh	w22, [x1,5530]
	ldrb	w1, [x2,1868]
	sdiv	w0, w22, w0
	uxth	w26, w0
	cbz	w1, .L3747
	adrp	x1, .LANCHOR2
	mov	w23, 2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w0, [x1,w0,sxtw 1]
	cmp	w0, wzr
	csinc	w23, w23, wzr, ne
	b	.L3748
.L3747:
	adrp	x0, .LANCHOR5+925
	mov	w23, 1
	ldrb	w0, [x0,#:lo12:.LANCHOR5+925]
	cbz	w0, .L3748
	sub	w0, w26, #62
	mov	w23, 2
	uxth	w0, w0
	cmp	w0, 2159
	bhi	.L3748
	udiv	w0, w26, w3
	add	w0, w0, w0, lsl 1
	sub	w0, w26, w0
	uxth	w0, w0
	cmp	w0, wzr
	csinc	w23, w23, wzr, eq
.L3748:
	msub	w20, w26, w20, w22
	uxth	w20, w20
	b	.L3751
.L3746:
	ldrb	w2, [x2,1264]
	cbz	w2, .L3750
	ldrh	w22, [x1,5530]
	add	w20, w0, w0, lsl 1
	mov	w23, w3
	sdiv	w20, w22, w20
	mul	w0, w20, w0
	uxth	w26, w20
	sub	w0, w0, w0, lsl 2
	add	w20, w22, w0
	ldrb	w0, [x1,1269]
	sdiv	w20, w20, w3
	uxth	w20, w20
	cbz	w0, .L3751
	add	w4, w26, w26, lsl 1
	uxth	w26, w4
	b	.L3751
.L3750:
	ldrh	w5, [x1,5530]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mov	w23, w3
	add	x1, x1, 3672
	sdiv	w2, w5, w0
	msub	w20, w20, w2, w5
	ldrh	w25, [x1,w2,sxtw 1]
	uxth	w20, w20
	lsr	w26, w25, 3
	and	w25, w25, 7
	madd	w22, w26, w0, w20
	cmp	w25, 1
	add	w22, w22, w22, lsl 1
	uxth	w22, w22
	bne	.L3752
	b	.L3751
.L3745:
	ldrb	w1, [x2,1250]
	cbnz	w1, .L3753
	add	x1, x19, :lo12:.LANCHOR0
	mov	w23, 1
	ldrh	w22, [x1,5530]
	sdiv	w26, w22, w0
	msub	w20, w26, w20, w22
	uxth	w20, w20
	b	.L3751
.L3753:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w23, 2
	ldrh	w22, [x1,5530]
	sdiv	w26, w22, w0
	msub	w20, w26, w20, w22
	uxth	w20, w20
.L3751:
	adrp	x3, .LANCHOR5
	adrp	x2, .LANCHOR2
	mul	w7, w23, w24
	mov	w28, 0
	mov	x8, x3
	add	x2, x2, :lo12:.LANCHOR2
	sxtw	x5, w26
	add	x6, x19, :lo12:.LANCHOR0
	b	.L3754
.L3885:
	add	x0, x2, 2136
	ldrh	w0, [x0,x5,lsl 1]
	cmp	w0, w26
	bcs	.L3814
	tbz	x28, 0, .L3814
.L3755:
	add	x0, x8, :lo12:.LANCHOR5
	sbfiz	x25, x25, 2, 32
	ldr	x1, [x0,152]
	ldr	w0, [x1,x25]
	cmn	w0, #1
	beq	.L3884
.L3758:
	add	w28, w28, 1
	uxth	w28, w28
.L3754:
	cmp	w28, w7
	bge	.L3814
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x4, [x29,184]
	add	w25, w22, w28
	ldrb	w9, [x4,9]
	ldrh	w0, [x1,1266]
	mul	w0, w0, w9
	add	x9, x3, :lo12:.LANCHOR5
	ldrb	w9, [x9,929]
	sub	w0, w0, w9
	cmp	w25, w0
	blt	.L3755
	ldrb	w0, [x1,1868]
	cbnz	w0, .L3885
.L3814:
	mov	w25, 1
.L3752:
	mul	w0, w23, w24
	str	w0, [x29,168]
	str	wzr, [x29,152]
	sub	w0, w22, w0, lsl 1
	uxth	w0, w0
	str	w0, [x29,128]
	ldrh	w0, [x29,128]
	str	x0, [x29,120]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,112]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 992
	str	x0, [x29,104]
	b	.L3757
.L3884:
	ldrh	w27, [x6,3450]
	ldrh	w0, [x6,3448]
	cmp	w0, w27
	bls	.L3730
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w0, [x0,1868]
	cbz	w0, .L3759
	add	x0, x2, 2136
	ldrh	w0, [x0,x5,lsl 1]
	cmp	w0, w26
	bcs	.L3759
	tbz	x28, 0, .L3759
	ubfiz	x0, x0, 2, 16
	ldr	w0, [x1,x0]
	str	w0, [x1,x25]
	b	.L3758
.L3759:
	mov	w0, w27
	str	x6, [x29,128]
	str	x5, [x29,136]
	add	w27, w27, 1
	str	x2, [x29,144]
	str	x8, [x29,152]
	str	x3, [x29,160]
	str	x1, [x29,168]
	str	x7, [x29,176]
	bl	gc_get_src_ppa_from_index
	ldr	x6, [x29,128]
	ldr	x1, [x29,168]
	ldr	x5, [x29,136]
	strh	w27, [x6,3450]
	str	w0, [x1,x25]
	ldr	x2, [x29,144]
	ldr	x8, [x29,152]
	ldr	x3, [x29,160]
	ldr	x7, [x29,176]
	b	.L3758
.L3785:
	ldr	w0, [x29,160]
	add	w2, w22, w0
	sxtw	x0, w2
	str	x0, [x29,176]
	add	x0, x21, :lo12:.LANCHOR3
	ldr	x1, [x29,176]
	ldr	x0, [x0,1256]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	beq	.L3886
.L3761:
	add	x5, x21, :lo12:.LANCHOR3
	ldr	x2, [x29,176]
	add	x3, x29, 192
	add	x6, x19, :lo12:.LANCHOR0
	add	x1, x6, 1312
	ldr	x0, [x5,1256]
	ldrb	w2, [x0,x2]
	ldr	w0, [x29,160]
	add	x1, x1, x2, lsl 6
	str	x1, [x3,w0,sxtw 3]
	mov	w3, 2
	ldrb	w0, [x5,1248]
	strh	w3, [x1,50]
	adrp	x3, .LANCHOR5
	strb	w25, [x1,61]
	cmp	w0, 3
	bne	.L3776
	ldrb	w7, [x6,1268]
	cbnz	w7, .L3887
	ldrb	w6, [x6,1269]
	add	x1, x1, 32
	udiv	w0, w27, w0
	ldrh	w7, [x5,1330]
	cbz	w6, .L3780
	add	w6, w0, w20
	ldr	x4, [x29,184]
	add	x6, x4, x6, sxtw 1
	ldrh	w5, [x6,16]
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	madd	w5, w5, w7, w26
	add	w0, w5, w0, uxth
.L3883:
	str	w0, [x1,8]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x2, x0, x2, lsl 6
	add	x0, x3, :lo12:.LANCHOR5
	ldr	x1, [x0,160]
	mov	w0, 3
	ldrh	w5, [x2,48]
	udiv	w0, w27, w0
	ldr	w2, [x2,40]
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	uxth	w0, w0
	add	w0, w0, 1
	orr	w0, w2, w0, lsl 24
	str	w0, [x1,x5,lsl 2]
.L3804:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1868]
	cbnz	w0, .L3888
.L3784:
	ldr	w0, [x29,152]
	add	w0, w0, 1
	str	w0, [x29,152]
.L3757:
	ldr	w0, [x29,152]
	ldr	w1, [x29,168]
	and	w0, w0, 65535
	str	w0, [x29,160]
	cmp	w0, w1
	ldrh	w27, [x29,152]
	blt	.L3785
	b	.L3889
.L3886:
	mov	w0, 0
	str	x2, [x29,144]
	bl	buf_alloc
	mov	x28, x0
	ldr	x2, [x29,144]
	cbnz	x0, .L3762
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w28
	bl	buf_alloc
	mov	x28, x0
	ldr	x2, [x29,144]
	cbz	x0, .L3730
.L3762:
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x3, [x29,176]
	ldrb	w5, [x28,1]
	ldr	x0, [x1,1256]
	strb	w5, [x0,x3]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,184]
	strb	w25, [x28,61]
	ldrb	w5, [x0,3431]
	add	w5, w5, 1
	strb	w5, [x0,3431]
	add	w0, w27, w22
	strh	w0, [x28,48]
	ldrh	w0, [x1,1266]
	ldrb	w5, [x3,9]
	mul	w0, w0, w5
	adrp	x5, .LANCHOR5
	str	x5, [x29,144]
	add	x6, x5, :lo12:.LANCHOR5
	ldrb	w6, [x6,929]
	sub	w0, w0, w6
	cmp	w2, w0
	blt	.L3763
	ldrb	w0, [x1,1868]
	cbz	w0, .L3764
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0,w26,sxtw 1]
	cmp	w0, w26
	bcs	.L3764
	tbnz	x27, 0, .L3763
.L3764:
	ldr	x0, [x29,144]
	mov	w1, -1
	ldr	x3, [x29,176]
	add	x5, x21, :lo12:.LANCHOR3
	add	x6, x0, :lo12:.LANCHOR5
	ldr	x0, [x6,144]
	str	w1, [x0,x3,lsl 2]
	ldrb	w0, [x6,929]
	cmp	w0, 2
	ldrh	w0, [x5,1266]
	bne	.L3765
	ldr	x1, [x29,184]
	ldrb	w1, [x1,9]
	mul	w1, w0, w1
	sub	w1, w1, #2
	cmp	w2, w1
	ldrh	w2, [x6,926]
	bne	.L3766
	ldr	x1, [x6,144]
	ldr	x0, [x28,8]
	str	x6, [x29,96]
	bl	ftl_memcpy
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x0, [x28,24]
	mov	w1, 0
	str	x5, [x29,136]
	ldrb	w2, [x5,3360]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x28,24]
	mov	w0, 15555
	ldr	x6, [x29,96]
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	ldr	x2, [x28,24]
	ldrh	w1, [x6,926]
	ldr	x0, [x28,8]
	str	x2, [x29,144]
	bl	js_hash
	ldr	x2, [x29,144]
	ldr	x5, [x29,136]
	str	w0, [x2,4]
	ldr	x1, [x5,1120]
	ldr	x0, [x28,24]
	ldr	w1, [x1,132]
	str	w1, [x0,8]
	ldr	x0, [x28,24]
	str	wzr, [x0,12]
	b	.L3767
.L3766:
	ldrb	w5, [x5,1249]
	ldr	x1, [x6,144]
	mul	w5, w0, w5
	ldr	x0, [x28,8]
	lsl	w5, w5, 2
	sub	w5, w5, w2
	and	x2, x2, 65532
	add	x1, x1, x2
	mov	w2, w5
	str	x5, [x29,96]
	bl	ftl_memcpy
	add	x6, x19, :lo12:.LANCHOR0
	ldr	x0, [x28,24]
	mov	w1, 0
	str	x6, [x29,136]
	ldrb	w2, [x6,3360]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x28,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	ldr	x5, [x29,96]
	str	w0, [x1]
	mov	w1, w5
	ldr	x2, [x28,24]
	ldr	x0, [x28,8]
	str	x2, [x29,144]
	bl	js_hash
	ldr	x2, [x29,144]
	ldr	x6, [x29,136]
	str	w0, [x2,4]
	ldr	x1, [x6,1120]
	ldr	x0, [x28,24]
	ldr	w1, [x1,132]
	str	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x28,24]
	str	w1, [x0,12]
	b	.L3767
.L3765:
	ldrb	w2, [x5,1249]
	ldr	x1, [x6,144]
	str	x5, [x29,96]
	mul	w2, w0, w2
	ldr	x0, [x28,8]
	lsl	w2, w2, 2
	str	x6, [x29,136]
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	ldr	x6, [x29,136]
	ldrb	w1, [x5,1249]
	ldrh	w0, [x5,1266]
	mul	w0, w0, w1
	add	x1, x19, :lo12:.LANCHOR0
	lsl	w2, w0, 2
	ldrb	w1, [x1,3360]
	cmp	w1, w0, lsr 6
	bge	.L3768
	ldrh	w1, [x6,926]
	sub	w2, w1, w2
.L3768:
	ldr	x1, [x29,144]
	ldr	x5, [x28,8]
	add	x0, x5, x0, sxtw 2
	add	x1, x1, :lo12:.LANCHOR5
	ldr	x1, [x1,152]
	bl	ftl_memcpy
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x0, [x28,24]
	mov	w1, 0
	str	x5, [x29,136]
	ldrb	w2, [x5,3360]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x28,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x2, [x28,24]
	ldrh	w0, [x1,1266]
	ldrb	w1, [x1,1249]
	str	x2, [x29,144]
	mul	w1, w0, w1
	ldr	x0, [x28,8]
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,144]
	ldr	x5, [x29,136]
	str	w0, [x2,4]
	ldr	x1, [x5,1120]
	ldr	x0, [x28,24]
	ldr	w1, [x1,132]
	b	.L3879
.L3763:
	ldr	x0, [x29,176]
	lsl	x0, x0, 2
	str	x0, [x29,136]
	ldr	x0, [x29,144]
	ldr	x1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,152]
	ldr	w0, [x0,x1]
	mov	w1, 1
	str	w0, [x28,40]
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3769
	cmn	w0, #1
	bne	.L3770
.L3769:
	add	x1, x19, :lo12:.LANCHOR0
	add	x5, x21, :lo12:.LANCHOR3
	ldrb	w2, [x1,1257]
	mov	w1, 24
	ldrh	w0, [x5,1232]
	sub	w2, w1, w2
	mov	w1, 1
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x28,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x5,1234]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3817
	cmn	w0, #1
	bne	.L3770
.L3817:
	ldr	x0, [x28,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3770:
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3818
	cmn	w0, #1
	bne	.L3773
.L3818:
	ldr	x1, [x29,104]
	mov	w2, 1223
	ldr	x0, [x29,112]
	bl	printk
	bl	dump_stack
.L3773:
	ldr	x1, [x28,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,3372]
	cmp	w2, w0
	bcc	.L3775
	mov	w0, -1
	str	w0, [x1,4]
.L3775:
	ldr	x0, [x28,24]
	ldr	x2, [x29,136]
	ldr	w1, [x0,4]
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,144]
	str	w1, [x0,x2]
	ldr	x0, [x28,24]
	ldr	w1, [x28,40]
.L3879:
	str	w1, [x0,8]
.L3767:
	ldr	x0, [x28,24]
	str	wzr, [x0,16]
	b	.L3761
.L3887:
	ldrb	w1, [x5,1868]
	and	w6, w27, 1
	add	w0, w26, w6
	cbz	w1, .L3778
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w1, [x1,w26,sxtw 1]
	cbz	w6, .L3778
	cmp	w1, w26
	csel	w0, w0, w1, cs
.L3778:
	add	x6, x21, :lo12:.LANCHOR3
	ldr	x4, [x29,184]
	udiv	w1, w27, w23
	add	x5, x19, :lo12:.LANCHOR0
	add	x5, x5, x2, lsl 6
	add	w1, w1, w20
	ldrh	w6, [x6,1330]
	add	x1, x4, x1, sxtw 1
	ldrh	w1, [x1,16]
	madd	w0, w1, w6, w0
	orr	w0, w0, 50331648
	str	w0, [x5,1352]
	b	.L3779
.L3780:
	add	w0, w0, w20
	ldr	x4, [x29,184]
	add	x0, x4, x0, sxtw 1
	ldrh	w5, [x0,16]
	madd	w0, w5, w7, w26
	b	.L3883
.L3776:
	cmp	w0, 2
	bne	.L3779
	ldrb	w6, [x5,1250]
	add	x1, x1, 32
	ldrh	w0, [x5,1330]
	cbnz	w6, .L3783
	ldr	w4, [x29,160]
	add	w5, w4, w20
	ldr	x4, [x29,184]
	add	x5, x4, x5, sxtw 1
	ldrh	w5, [x5,16]
	madd	w0, w5, w0, w26
	b	.L3880
.L3783:
	add	w5, w20, w27, lsr 1
	ldr	x4, [x29,184]
	add	x5, x4, x5, sxtw 1
	ldrh	w5, [x5,16]
	madd	w0, w5, w0, w26
	and	w5, w27, 1
	add	w0, w0, w5
.L3880:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L3779:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x2, x0, x2, lsl 6
	add	x0, x3, :lo12:.LANCHOR5
	ldrh	w1, [x2,48]
	ldr	x0, [x0,160]
	ldr	w5, [x2,40]
	str	w5, [x0,x1,lsl 2]
	b	.L3804
.L3888:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0,w26,sxtw 1]
	cmp	w0, w26
	bcs	.L3784
	tbz	x27, 0, .L3784
	ldr	x0, [x1,1256]
	mov	w2, -1
	ldr	x4, [x29,176]
	add	x3, x3, :lo12:.LANCHOR5
	strb	w2, [x0,x4]
	add	x2, x29, 192
	ldr	w0, [x29,160]
	ldr	x0, [x2,w0,sxtw 3]
	ldrh	w2, [x29,128]
	strh	w2, [x0,48]
	ldrb	w2, [x0,1]
	ldr	x0, [x1,1256]
	ldr	x1, [x29,120]
	strb	w2, [x0,x1]
	mov	w1, -1
	ldr	x0, [x3,152]
	str	w1, [x0,x4,lsl 2]
	b	.L3784
.L3889:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1248]
	cmp	w0, 3
	add	x0, x29, 192
	bne	.L3786
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w2, [x2,1268]
	cbz	w2, .L3787
	ldr	w1, [x29,168]
	sub	w2, w1, #1
	mov	w1, 0
.L3788:
	cmp	w1, w2
	bge	.L3882
	ldr	x3, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w5, [x5,1]
	strb	w5, [x3]
	b	.L3788
.L3787:
	ldrb	w1, [x1,1264]
	cbnz	w1, .L3881
	cmp	w25, 1
	mov	w2, 9
	beq	.L3881
	cmp	w25, 2
	bne	.L3794
	mov	w2, 13
.L3881:
	ldr	x1, [x29,192]
	strb	w2, [x1,60]
	b	.L3793
.L3794:
	ldr	x2, [x29,192]
	strb	w1, [x2,60]
.L3793:
	add	x1, x0, 24
	mov	w2, w24
	bl	sblk_xlc_prog_pages
	b	.L3790
.L3786:
	ldrb	w2, [x1,1250]
	cbz	w2, .L3795
	ldrb	w1, [x1,1251]
	cbnz	w1, .L3796
.L3795:
	ldr	w1, [x29,168]
	sub	w2, w1, #1
	mov	w1, 0
	b	.L3797
.L3796:
	mov	w1, w24
	bl	sblk_3d_mlc_prog_pages
	b	.L3790
.L3797:
	cmp	w1, w2
	bge	.L3882
	ldr	x3, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w5, [x5,1]
	strb	w5, [x3]
	b	.L3797
.L3882:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldr	x0, [x29,192]
	ldrb	w1, [x29,168]
	bl	sblk_prog_page
.L3790:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1264]
	cbz	w0, .L3799
	add	w0, w24, w24, lsl 1
	b	.L3800
.L3799:
	ldrb	w0, [x1,1250]
	cbz	w0, .L3801
	lsl	w0, w24, 1
	b	.L3800
.L3801:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,1268]
	mov	w0, w24
	cbz	w2, .L3800
	ldrb	w1, [x1,1868]
	ldrh	w0, [x29,168]
	cbz	w1, .L3800
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w1, [x1,w26,sxtw 1]
	cmp	w1, w26
	csel	w0, w0, w24, cs
.L3800:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	ldr	x2, [x19,3392]
	ldr	w1, [x2,52]
	add	w1, w1, w0
	str	w1, [x2,52]
	ldr	x2, [x29,184]
	ldrh	w1, [x19,5530]
	add	w0, w0, w1
	ldrh	w1, [x21,1266]
	uxth	w0, w0
	strh	w0, [x19,5530]
	ldrb	w2, [x2,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L3802
	ldr	x0, [x19,1120]
	strh	wzr, [x0,86]
.L3802:
	bl	gc_write_completed
.L3730:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	gc_do_copy_back, .-gc_do_copy_back
	.align	2
	.global	zftl_do_gc
	.type	zftl_do_gc, %function
zftl_do_gc:
	sub	sp, sp, #128
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR5
	stp	x25, x26, [sp,80]
	mov	w26, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x27, x28, [sp,96]
	cmp	w26, 1
	ldrh	w23, [x0,3380]
	ldrh	w22, [x0,3376]
	ldr	x24, [x0,1120]
	add	w22, w23, w22
	ldrh	w25, [x0,3378]
	uxth	w22, w22
	beq	.L3891
.L3902:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, 16
	ldrb	w2, [x0,130]
	cmp	w2, 6
	bhi	.L4083
	adrp	x0, .L3894
	add	x0, x0, :lo12:.L3894
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x1, .Lrtx3894
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx3894:
	.section	.rodata
	.align	0
	.align	2
.L3894:
	.2byte	(.L3893 - .Lrtx3894) / 4
	.2byte	(.L3895 - .Lrtx3894) / 4
	.2byte	(.L3896 - .Lrtx3894) / 4
	.2byte	(.L3897 - .Lrtx3894) / 4
	.2byte	(.L3898 - .Lrtx3894) / 4
	.2byte	(.L4018 - .Lrtx3894) / 4
	.2byte	(.L3900 - .Lrtx3894) / 4
	.text
.L3891:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w21, [x0,932]
	cbnz	w21, .L3901
	ldr	w0, [x0,936]
	cbz	w0, .L3902
.L3901:
	adrp	x0, .LANCHOR3+1298
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1298]
	cmp	w22, w0, lsr 2
	bls	.L3902
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3400]
	cmp	w0, w22
	bcs	.L3902
	add	w21, w21, 20
	bl	timer_get_time
	cmp	w21, w0
	bcs	.L3903
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0,932]
.L3903:
	add	x27, x20, :lo12:.LANCHOR5
	ldr	w21, [x27,936]
	bl	timer_get_time
	add	w21, w21, 20
	cmp	w21, w0
	bcs	.L3904
	str	wzr, [x27,936]
.L3904:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,124]
	cbz	w0, .L4121
	b	.L3902
.L3893:
	add	x21, x19, :lo12:.LANCHOR0
	add	w23, w25, w23
	ldrh	w1, [x24,80]
	uxth	w23, w23
	ldrh	w0, [x21,3382]
	ldrh	w25, [x21,3384]
	add	w25, w25, w0
	mov	w0, 65535
	cmp	w1, w0
	uxth	w25, w25
	beq	.L3905
	cbnz	w26, .L3906
	ldrh	w0, [x21,3400]
	cmp	w22, w0, lsl 1
	bge	.L4121
.L3906:
	adrp	x21, .LANCHOR3
	mov	w1, 5
	add	x26, x21, :lo12:.LANCHOR3
	ldrh	w0, [x26,1296]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x26,1296]
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3908
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,132]
	add	w1, w1, 1
	str	w1, [x3,132]
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x4, [x3,1112]
	ldrh	w2, [x4,x2]
	ldrh	w4, [x26,1294]
	cmp	w4, w2
	bcs	.L3909
	ldrh	w4, [x3,1088]
	cmp	w1, w4, lsr 4
	bls	.L3908
	ldrh	w1, [x3,3402]
	cmp	w1, w2
	bls	.L3908
.L3909:
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 0
	ldrb	w2, [x2,1265]
	bl	gc_add_sblk
	cbz	w0, .L3910
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	str	wzr, [x20,132]
	strb	w0, [x20,130]
	b	.L4121
.L3908:
	add	x0, x21, :lo12:.LANCHOR3
	strh	wzr, [x0,1296]
.L3910:
	cmp	w23, 15
	mov	w26, 2
	bls	.L3911
	cbz	w25, .L4020
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	mov	w26, 1
	ldrh	w1, [x1,3386]
	ldrh	w0, [x0,1300]
	cmp	w1, w0
	bls	.L3911
.L4020:
	mov	w26, 2
.L3911:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3912
	add	x19, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR3
	mov	w3, w22
	mov	w4, w23
	ldr	x0, [x19,1120]
	ldrb	w2, [x1,1265]
	mov	w1, 2662
	ldrh	w5, [x0,124]
	ldrh	w6, [x0,120]
	ldrh	w7, [x0,122]
	ldrh	w0, [x24,80]
	str	w0, [sp]
	adrp	x0, .LC296
	add	x0, x0, :lo12:.LC296
	bl	printk
.L3912:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, w26
	mov	w2, 1
	ldrb	w0, [x21,1265]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3913
.L3914:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	b	.L4123
.L3913:
	ldrb	w0, [x21,1265]
	mov	w1, 3
	mov	w2, 1
	bl	gc_search_src_blk
	cmp	w0, wzr
	bgt	.L3914
	b	.L4121
.L3905:
	cmp	w26, 1
	bne	.L3915
	bl	gc_scan_static_data
	ldr	x0, [x21,1120]
	adrp	x21, .LANCHOR3
	ldrh	w0, [x0,122]
	cbz	w0, .L3916
.L3917:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	add	x20, x20, :lo12:.LANCHOR5
	strb	w0, [x21,1265]
.L4123:
	strb	w0, [x20,130]
	b	.L4121
.L3916:
	bl	gc_static_wearleveling
	cbnz	w0, .L3917
	bl	gc_block_vpn_scan
	cbz	w23, .L3918
	cmp	w22, w25
	bcs	.L3919
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0,1298]
	cmp	w0, w22
	bhi	.L3920
.L3919:
	add	x1, x21, :lo12:.LANCHOR3
	add	w0, w22, w25
	ldrh	w2, [x1,1298]
	cmp	w0, w2
	blt	.L3920
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,3386]
	ldrh	w0, [x1,1300]
	cmp	w2, w0
	bcc	.L3918
.L3920:
	add	x24, x21, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x24,1265]
	mov	w0, 16
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3921
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,136]
	cmp	w3, w1
	bcs	.L3921
	cmp	w22, 2
	bls	.L3921
	str	wzr, [x2,132]
	mov	w1, 1
	mov	w2, 0
	strb	wzr, [x24,1265]
	bl	gc_add_sblk
	cbnz	w0, .L3914
.L3921:
	mov	w1, 3
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3922
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,136]
	cmp	w3, w1
	bcs	.L3922
	cmp	w22, 2
	bls	.L3922
	add	x1, x21, :lo12:.LANCHOR3
	str	wzr, [x2,132]
	mov	w2, 0
	strb	wzr, [x1,1265]
	mov	w1, 1
	bl	gc_add_sblk
	cbnz	w0, .L3914
.L3922:
	add	x1, x20, :lo12:.LANCHOR5
	add	x2, x21, :lo12:.LANCHOR3
	ldrh	w0, [x1,136]
	ldrh	w24, [x2,1294]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x1,136]
	cmp	w0, w24, lsr 5
	bls	.L3923
	mov	w0, 4
	strh	w0, [x1,136]
.L3923:
	add	x28, x20, :lo12:.LANCHOR5
	add	x2, x19, :lo12:.LANCHOR0
	add	x27, x21, :lo12:.LANCHOR3
	ldr	w0, [x28,132]
	ldrh	w1, [x2,1088]
	add	w0, w0, 1
	strh	wzr, [x27,1288]
	str	w0, [x28,132]
	strh	wzr, [x27,1290]
	cmp	w0, w1, lsr 5
	strh	wzr, [x27,1292]
	bls	.L3924
	ldrh	w0, [x2,3386]
	cmp	w0, w23
	bls	.L3924
	mov	w1, 5
	mov	w0, 0
	str	x2, [x29,104]
	bl	zftl_get_gc_node
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L4021
	ldr	x2, [x29,104]
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x2,1112]
	ldrh	w1, [x1,x0]
	ldrb	w0, [x27,1249]
	mul	w0, w24, w0
	mov	w24, 16
	cmp	w1, w0
	bgt	.L3929
	str	wzr, [x28,132]
	mov	w1, 2
	ldrb	w0, [x27,1265]
	b	.L4114
.L3924:
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR3
	ldrh	w3, [x0,3386]
	ldrh	w2, [x1,1300]
	cmp	w3, w2
	bcc	.L3927
	ldrb	w0, [x1,1265]
	mov	w2, 1
	mov	w1, 2
	b	.L4115
.L3927:
	ldrh	w24, [x0,3382]
	cbnz	w24, .L3928
	ldrh	w0, [x0,3384]
	cmp	w0, 8
	bls	.L3929
.L3928:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, 1
	ldrb	w0, [x21,1265]
.L4114:
	mov	w2, 4
.L4115:
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3914
	b	.L4021
.L3918:
	add	x21, x21, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x21,1294]
	ldrh	w1, [x0,136]
	cmp	w1, w2, lsr 5
	bcc	.L4021
	mov	w1, 4
	strh	w1, [x0,136]
	b	.L4021
.L3915:
	ldrh	w0, [x21,3400]
	cmp	w0, w22
	bcs	.L4021
	ldr	x0, [x21,1120]
	ldrh	w0, [x0,124]
	cbz	w0, .L4121
.L4021:
	mov	w24, 16
.L3929:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldr	w1, [x0,3412]
	cbz	w1, .L3931
	str	wzr, [x0,3412]
	cmp	w22, 15
	add	x0, x21, :lo12:.LANCHOR3
	bhi	.L4028
	cbnz	w23, .L3932
.L4028:
	strb	wzr, [x0,1265]
	b	.L3934
.L3932:
	mov	w1, 1
	strb	w1, [x0,1265]
.L3934:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3935
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 8
	bhi	.L3935
	mov	w1, 1
	str	w1, [x2,3412]
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 0
	ldrb	w2, [x2,1265]
	bl	gc_add_sblk
	cbnz	w0, .L3914
.L3935:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3936
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3936
	mov	w1, 1
	str	w1, [x2,3412]
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 0
	ldrb	w2, [x2,1265]
	bl	gc_add_sblk
	cbnz	w0, .L3914
.L3936:
	mov	w0, 0
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3931
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3931
	mov	w1, 1
	str	w1, [x2,3412]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w2, [x2,1265]
	bl	gc_add_sblk
	cbnz	w0, .L3914
.L3931:
	add	x28, x19, :lo12:.LANCHOR0
	add	x27, x21, :lo12:.LANCHOR3
	mov	w1, 1
	ldr	x0, [x28,1120]
	strb	w1, [x27,1265]
	ldrh	w3, [x0,124]
	cbz	w3, .L3937
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x27,1265]
	strb	w1, [x20,130]
	b	.L3938
.L3937:
	ldrh	w0, [x28,3400]
	cmp	w22, w0
	bcs	.L3939
	cbz	w23, .L3940
	cmp	w23, 16
	bls	.L3941
	ldrh	w2, [x28,3386]
	ldrh	w0, [x27,1300]
	cmp	w2, w0
	bhi	.L3941
	mov	w0, w1
	mov	w2, 4
	str	x3, [x29,104]
	bl	gc_search_src_blk
	uxth	w0, w0
	ldr	x3, [x29,104]
	cbz	w0, .L4118
	mov	w1, 5
	mov	w0, w3
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L4015
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,132]
	add	w1, w1, 1
	str	w1, [x3,132]
	ldr	x3, [x28,1112]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x27,1294]
	cmp	w3, w2
	bcs	.L3945
	ldrh	w3, [x28,1088]
	cmp	w1, w3, lsr 4
	bls	.L4015
	ldrh	w1, [x28,3402]
	cmp	w1, w2
	bls	.L4015
.L3945:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w2, [x21,1265]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19,3412]
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0,132]
	b	.L4015
.L3941:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4015
	add	x21, x21, :lo12:.LANCHOR3
	ldrb	w0, [x21,1265]
	b	.L4117
.L3940:
	strb	w23, [x27,1265]
	cmp	w22, 16
	mov	w0, w23
	bhi	.L4117
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4015
.L4118:
	ldrb	w0, [x27,1265]
.L4117:
	mov	w1, 3
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3943
.L3939:
	cmp	w26, 1
	mov	w1, w24
	bne	.L4083
	cmp	w22, w0, lsl 1
	bge	.L3948
	cmp	w25, w23, lsr 1
	bcs	.L3949
	ldrh	w2, [x28,3386]
	ldrh	w0, [x27,1300]
	cmp	w2, w0
	bcs	.L3949
	ldrh	w0, [x27,1298]
	lsr	w0, w0, 2
	strh	w0, [x28,3400]
	b	.L4083
.L3949:
	mov	w0, 8
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3950
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,1112]
	ldrh	w1, [x2,x1]
	cmp	w1, 3
	bhi	.L3950
	cbz	w22, .L3950
	add	x1, x21, :lo12:.LANCHOR3
	mov	w2, 0
	strb	wzr, [x1,1265]
	mov	w1, 1
	bl	gc_add_sblk
	cbnz	w0, .L4015
.L3950:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w26, w0
	mov	w1, 65535
	mov	w28, w26
	cmp	w26, w1
	bne	.L3951
.L3956:
	cmp	w23, 1
	bhi	.L3952
	b	.L3953
.L3951:
	add	x27, x20, :lo12:.LANCHOR5
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x26, w26
	ldr	w1, [x27,132]
	ldrh	w3, [x2,1088]
	add	w1, w1, 1
	str	w1, [x27,132]
	cmp	w1, w3, lsr 4
	bls	.L3954
	ldr	x1, [x2,1112]
	str	wzr, [x27,132]
	ldrh	w3, [x1,x26,lsl 1]
	ldrh	w1, [x2,3402]
	cmp	w3, w1
	bcs	.L3954
	add	x1, x21, :lo12:.LANCHOR3
	mov	w3, 1
	mov	w2, 1
	str	x3, [x29,104]
	strb	w3, [x1,1265]
	mov	w1, 0
	bl	gc_add_sblk
	ldr	x3, [x29,104]
	cbz	w0, .L3954
	strb	w3, [x27,130]
	b	.L3938
.L3954:
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR3
	ldr	x0, [x1,1112]
	ldrh	w2, [x0,x26,lsl 1]
	ldrh	w0, [x3,1294]
	cmp	w2, w0, lsr 1
	bhi	.L3955
	mov	w0, w28
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L4015
.L3955:
	ldrh	w4, [x1,3382]
	ldrh	w0, [x1,3384]
	ldrh	w3, [x3,1298]
	add	w0, w4, w0
	cmp	w0, w3, lsl 1
	ble	.L3956
	ldrh	w0, [x1,3402]
	cmp	w0, w2
	bcc	.L3953
	b	.L3956
.L3952:
	add	x26, x21, :lo12:.LANCHOR3
	mov	w0, 1
	cmp	w23, 16
	strb	w0, [x26,1265]
	bls	.L3957
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,3386]
	ldrh	w1, [x26,1300]
	cmp	w2, w1
	bhi	.L3957
	mov	w1, w0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3958
	ldrb	w0, [x26,1265]
	b	.L4116
.L3957:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3958
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w0, [x0,1265]
.L4116:
	mov	w1, 3
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
.L3958:
	add	x21, x21, :lo12:.LANCHOR3
	cmp	w22, w25, lsr 1
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x21,1298]
	bls	.L4119
	lsr	w1, w1, 2
	b	.L4120
.L3953:
	cmp	w22, w25
	bcs	.L3960
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 4
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x21,1265]
	bl	zftl_get_gc_node.part.11
	mov	w1, 65535
	uxth	w0, w0
	cmp	w0, w1
	beq	.L4125
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,1112]
	ldrb	w2, [x21,1249]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x21,1294]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	bgt	.L4125
	mov	w1, 3
	mov	w0, 0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	ldrh	w1, [x21,1298]
.L4119:
	lsr	w1, w1, 1
.L4120:
	strh	w1, [x19,3400]
	b	.L3943
.L3960:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
.L4125:
	ldrh	w0, [x21,1298]
	lsr	w0, w0, 2
	strh	w0, [x19,3400]
	b	.L3938
.L3948:
	ldrh	w0, [x27,1298]
	mov	w24, w3
	lsr	w0, w0, 2
	strh	w0, [x28,3400]
	b	.L3938
.L3943:
	mov	w1, w24
	cbz	w0, .L4083
.L4015:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,130]
	b	.L3938
.L3895:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21,3424]
	cmp	w1, w0
	bne	.L3963
	bl	gc_get_src_blk
	strh	w0, [x21,3424]
.L3963:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0,3424]
	cmp	w1, w2
	beq	.L3964
	ldrh	w2, [x0,1088]
	cmp	w2, w1
	bhi	.L3964
	mov	w1, -1
	strh	w1, [x0,3424]
.L3964:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 3424
	ldrh	w3, [x0,3424]
	cmp	w3, w2
	beq	.L4122
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,1096]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3967
	mov	x0, 0
.L3966:
	cmp	w5, w0, uxth
	bls	.L3967
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3966
.L3972:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,3424]
	b	.L4121
.L3967:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L4029
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3969
.L4029:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3972
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3153
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3972
.L3969:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	b	.L4123
.L3896:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3973
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 3
	b	.L4123
.L3973:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,3424]
	cmp	w0, w3
	beq	.L3914
	ldrh	w3, [x1,3448]
	add	x20, x20, :lo12:.LANCHOR5
	cbz	w3, .L3974
	mov	w0, 4
	strh	wzr, [x1,3450]
	strb	w0, [x20,130]
	b	.L4121
.L3974:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,1112]
	mov	w2, 1
	strb	w2, [x20,130]
	ldrh	w0, [x1,x0]
	cbz	w0, .L3975
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3183
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3975:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x20, 3424
	ldrh	w0, [x20,3424]
	bl	ftl_free_sblk
	ldr	x0, [x20,1112]
	ldrh	w1, [x20,3424]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x21,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3976
	strh	w0, [x21,30]
	b	.L3972
.L3976:
	strh	wzr, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3972
.L3897:
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR3
.L4084:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,3426]
	add	x0, x21, 3424
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w1, [x1,1294]
	cmp	w2, w1
	bcs	.L3978
	cmp	w22, 7
	bls	.L4084
	b	.L4121
.L3978:
	ldrh	w3, [x0,24]
	cbz	w3, .L3979
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, 4
	strh	wzr, [x0,26]
	ldr	x2, [x21,1112]
	strb	w1, [x20,130]
	ldrh	w1, [x0]
	ubfiz	x0, x1, 1, 16
	ldrh	w2, [x2,x0]
	cmp	w2, w3
	beq	.L3980
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L3980
	adrp	x0, .LC297
	add	x0, x0, :lo12:.LC297
	bl	printk
.L3980:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,3424]
	ldr	x1, [x0,1112]
	ldrh	w0, [x0,3448]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L3981
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3226
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3981:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,3424]
	ldr	x0, [x19,1112]
	ldrh	w2, [x19,3448]
	strh	w2, [x0,x1,lsl 1]
	b	.L4121
.L3979:
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x21, [x21,1096]
	add	x21, x21, x1, uxth 2
	strb	w0, [x20,130]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3982
	ldrb	w2, [x21,2]
	adrp	x0, .LC298
	add	x0, x0, :lo12:.LC298
	ubfx	x2, x2, 5, 3
	bl	printk
.L3982:
	ldrb	w0, [x21,2]
	tst	w0, 192
	beq	.L4030
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3983
.L4030:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3236
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3983:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 3424
	ldrh	w0, [x19,3424]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,3424]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3985
	strh	w0, [x20,30]
	b	.L4121
.L3985:
	strh	wzr, [x20,30]
	b	.L4124
.L3898:
	cbnz	w26, .L3986
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3400]
	cmp	w0, w22
	bcc	.L4121
.L3986:
	ldrh	w1, [x24,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3987
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w23, [x0,1265]
	cmp	w23, 1
	bne	.L3987
	bl	ftl_flush
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,5534]
	cbz	w0, .L3988
	mov	w0, w23
.L3988:
	mov	w1, 5
	bl	zftl_gc_get_free_sblk
	uxth	w23, w0
	mov	w0, 65535
	cmp	w23, w0
	beq	.L3990
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x22, [x0,1096]
	add	x22, x22, x23, uxth 2
	ldrb	w0, [x22,2]
	tst	w0, 224
	beq	.L3991
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3270
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3991:
	ldrb	w0, [x22,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x22,2]
.L4016:
	mov	w0, w23
	mov	w1, 1
	add	x21, x21, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	bl	ftl_erase_sblk
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 5
	add	x1, x24, 96
	strb	w0, [x24,84]
	mov	w0, w23
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x21,1294]
	strb	w0, [x24,89]
	ldrb	w2, [x21,1249]
	mul	w0, w0, w1
	strh	w0, [x24,86]
	mov	w1, 255
	ldrh	w0, [x21,1266]
	strh	w23, [x24,80]
	strh	wzr, [x24,82]
	mul	w2, w0, w2
	ldr	x0, [x20,144]
	strb	wzr, [x24,85]
	lsl	w2, w2, 2
	strh	wzr, [x24,90]
	bl	ftl_memset
	ldrh	w0, [x21,1266]
	mov	w1, 255
	ldrb	w2, [x21,1249]
	mul	w2, w0, w2
	ldr	x0, [x20,152]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x21,1266]
	mov	w1, 255
	ldrb	w2, [x21,1249]
	ldr	x0, [x21,1256]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x19,1120]
	mov	w1, -1
	str	w23, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x19,3480]
	ldr	x0, [x19,3392]
	strh	w23, [x0,126]
	mov	w0, -1
	str	w0, [x19,5540]
	mov	w0, 0
	strh	wzr, [x19,5530]
	strh	wzr, [x19,5532]
	strh	wzr, [x19,5536]
	bl	ftl_info_flush
	b	.L4121
.L3987:
	cmp	w26, 1
	mov	w23, 4
	csinc	w23, w23, wzr, eq
	cmp	w22, 15
	add	w0, w23, 4
	adrp	x27, .LANCHOR3
	csel	w23, w0, w23, ls
	add	x25, x19, :lo12:.LANCHOR0
.L3994:
	sub	w23, w23, #1
	uxtb	w23, w23
	cmp	w23, 255
	beq	.L4121
	add	x26, x27, :lo12:.LANCHOR3
	bl	gc_do_copy_back
	adrp	x22, .LANCHOR3
	ldrb	w0, [x26,1265]
	cbnz	w0, .L3995
	ldrb	w0, [x25,3362]
	cmp	w0, 3
	bhi	.L3996
	bl	ftl_write_commit
.L3996:
	ldrh	w1, [x25,3450]
	ldrh	w0, [x25,3448]
	cmp	w1, w0
	bcc	.L3994
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,130]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x25,3424]
	ldr	x0, [x25,1112]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3998
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3354
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3998:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,3424]
	ldr	x3, [x1,1112]
	ubfiz	x2, x0, 1, 16
	ldrh	w4, [x3,x2]
	cbnz	w4, .L3999
	strh	w4, [x3,x2]
	ldrh	w0, [x1,3424]
	bl	ftl_free_sblk
	b	.L3972
.L3999:
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	b	.L3972
.L3995:
	ldrh	w0, [x25,5536]
	add	x21, x25, 3424
	cbz	w0, .L4000
	strh	wzr, [x21,2112]
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	w0, [x21,2116]
	cmn	w0, #1
	beq	.L4001
	ldrb	w1, [x26,1868]
	cbnz	w1, .L4002
	ldrb	w1, [x26,1250]
	cbz	w1, .L4003
.L4002:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,3392]
	ldr	w3, [x2,156]
	mov	w2, 20041
	movk	w2, 0x444b, lsl 16
	cmp	w3, w2
	bne	.L4003
	add	x22, x22, :lo12:.LANCHOR3
	ldrb	w3, [x1,1257]
	mov	w2, 24
	sub	w2, w2, w3
	mov	w3, 1
	ldrh	w4, [x22,1232]
	sub	w2, w2, w4
	lsr	w0, w0, w4
	lsl	w2, w3, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x22,1234]
	udiv	w0, w0, w2
	ldr	x2, [x1,1096]
	lsl	x0, x0, 2
	add	x5, x2, x0
	ldrb	w3, [x5,2]
	tbz	x3, 3, .L4001
	ldrh	w4, [x1,3376]
	ldrh	w6, [x22,1298]
	add	w4, w4, 8
	cmp	w4, w6
	bge	.L4001
	and	w3, w3, -25
	strb	w3, [x5,2]
	ldrh	w1, [x1,1104]
	ldr	w3, [x2,x0]
	ldrh	w4, [x2,x0]
	ubfx	x3, x3, 11, 8
	ubfx	x5, x4, 0, 11
	mul	w1, w3, w1
	lsr	w3, w3, 3
	add	w1, w1, w1, lsl 1
	add	w1, w5, w1, lsr 2
	bfi	w4, w1, 0, 11
	strh	w4, [x2,x0]
	ldr	w1, [x2,x0]
	bfi	w1, w3, 11, 8
	str	w1, [x2,x0]
	b	.L4001
.L4003:
	bl	ftl_mask_bad_block
.L4001:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,1120]
	str	wzr, [x21,5540]
	strh	wzr, [x21,3480]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x21,1120]
	mov	w0, -1
	ldr	x2, [x21,3392]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x21,3432]
	cbz	x0, .L4004
	bl	zbuf_free
.L4004:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	str	xzr, [x19,3432]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3413
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x20,130]
	bl	printk
	bl	dump_stack
	b	.L4121
.L4000:
	ldrh	w0, [x24,86]
	cmp	w0, 1
	bls	.L4005
	ldrh	w1, [x21,26]
	ldrh	w0, [x21,24]
	cmp	w1, w0
	bcc	.L3994
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,130]
	ldrh	w0, [x21,56]
	add	w1, w0, 1
	add	x0, x21, x0, sxtw 1
	strh	w1, [x21,56]
	ldrh	w1, [x21]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x21]
	b	.L4121
.L4005:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 5
	ldrh	w1, [x21,26]
	strb	w0, [x20,130]
	ldrh	w0, [x21,24]
	cmp	w1, w0
	bcc	.L4006
	ldrh	w0, [x21,56]
	add	w1, w0, 1
	add	x0, x21, x0, sxtw 1
	strh	w1, [x21,56]
	ldrh	w1, [x21]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x21]
.L4006:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,1120]
	ldrh	w1, [x24,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,3440]
	add	x1, x22, :lo12:.LANCHOR3
	ldrb	w3, [x20,1268]
	ldrh	w2, [x1,1294]
	strh	w2, [x20,3442]
	ldrb	w2, [x1,1248]
	strh	w2, [x20,3444]
	cbz	w3, .L4007
	ldrh	w1, [x1,1266]
	strh	w1, [x20,3442]
	mov	w1, 1
	strh	w1, [x20,3444]
.L4007:
	cmp	w2, 2
	bne	.L4009
	add	x0, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	add	x0, x0, 3424
	ldrh	w1, [x0,18]
	lsl	w1, w1, 1
	strh	w1, [x0,18]
	ldrb	w1, [x22,1250]
	cbnz	w1, .L4009
	mov	w1, 1
	strh	w1, [x0,20]
.L4009:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,3446]
	b	.L4121
.L4018:
	mov	w21, 0
	add	x24, x19, :lo12:.LANCHOR0
.L3899:
	bl	gc_check_data_one_wl
	cbz	w0, .L4012
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x19,1120]
	strh	wzr, [x19,3480]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,1120]
	mov	w0, -1
	ldr	x2, [x19,3392]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,3432]
	bl	zbuf_free
	str	xzr, [x19,3432]
	strb	wzr, [x20,130]
.L4124:
	bl	flt_sys_flush
	b	.L4121
.L4012:
	ldrh	w1, [x24,3440]
	ldrh	w0, [x24,3442]
	cmp	w1, w0
	bcc	.L4013
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 6
	strb	w0, [x20,130]
	ldr	x0, [x24,3432]
	bl	zbuf_free
	str	xzr, [x24,3432]
	b	.L4121
.L4013:
	cmp	w22, 15
	bls	.L3899
	cmp	w26, 1
	bne	.L4121
	add	w21, w21, 1
	uxtb	w21, w21
	cmp	w21, 4
	bls	.L3899
	b	.L4121
.L3900:
	bl	gc_update_l2p_map_new
	mov	w21, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	pm_flush
	strh	w21, [x24,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,3392]
	strh	w21, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L4122:
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x20,130]
.L4121:
	mov	w1, 16
	b	.L4083
.L3938:
	mov	w1, w24
	b	.L4083
.L3990:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3277
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L4016
.L4083:
	sub	sp, x29, #16
	mov	w0, w1
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 128
	ret
	.size	zftl_do_gc, .-zftl_do_gc
	.align	2
	.global	zftl_init
	.type	zftl_init, %function
zftl_init:
	stp	x29, x30, [sp, -80]!
	mov	w0, -1
	adrp	x1, .LC2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	adrp	x21, .LANCHOR3
	adrp	x19, .LANCHOR5
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	add	x25, x19, :lo12:.LANCHOR5
	add	x23, x21, :lo12:.LANCHOR3
	add	x24, x20, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LC2
	adrp	x22, .LANCHOR2
	strb	w0, [x23,1874]
	strb	w0, [x24,3416]
	strb	w0, [x25,264]
	mov	w0, -1
	str	w0, [x25,940]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	strb	wzr, [x24,3388]
	strb	wzr, [x23,1875]
	strb	wzr, [x24,3389]
	bl	printk
	add	x1, x22, :lo12:.LANCHOR2
	ldrb	w3, [x24,1145]
	ldrh	w4, [x24,2]
	strb	w3, [x23,1332]
	ldrb	w2, [x1,21]
	ldrb	w9, [x1,20]
	ldrh	w5, [x1,18]
	strb	w2, [x23,1234]
	mul	w2, w2, w3
	ldrb	w0, [x1,17]
	mov	w3, 1
	ldrh	w6, [x1,22]
	mov	w1, 0
	sdiv	w10, w5, w9
	uxtb	w2, w2
	ubfiz	w8, w0, 9, 7
	strb	w9, [x23,1248]
	strb	w0, [x24,3360]
	strh	w5, [x23,1266]
	strh	w10, [x23,1294]
	strb	w2, [x23,1249]
	strh	w6, [x24,1088]
	strh	w8, [x25,926]
	strh	w4, [x23,1330]
.L4127:
	cmp	w3, w4
	bhi	.L4207
	add	w1, w1, 1
	lsl	w3, w3, 1
	uxth	w1, w1
	b	.L4127
.L4207:
	add	x3, x21, :lo12:.LANCHOR3
	sub	w1, w1, #1
	mul	w7, w0, w5
	mov	w4, 0
	strh	w1, [x3,1232]
	mov	w1, 1
	mul	w3, w7, w6
	lsr	w11, w3, 21
.L4129:
	cmp	w1, w11
	bhi	.L4208
	add	w4, w4, 1
	lsl	w1, w1, 1
	uxth	w4, w4
	b	.L4129
.L4208:
	mov	w1, 57344
	mov	w11, 1
	movk	w1, 0x1c, lsl 16
	sub	w4, w4, #1
	lsl	w4, w11, w4
	add	x11, x20, :lo12:.LANCHOR0
	mul	w1, w2, w1
	mul	w3, w3, w2
	str	w3, [x11,3368]
	lsr	w6, w6, 4
	mul	w1, w1, w4
	str	w1, [x11,1032]
	add	x4, x19, :lo12:.LANCHOR5
	add	w3, w1, 24576
	cmp	w6, 79
	mul	w10, w10, w2
	udiv	w0, w3, w0
	str	w3, [x4,944]
	str	w0, [x11,3372]
	sub	w11, w8, #1
	mul	w7, w7, w2
	sub	w7, w7, #1
	add	w0, w11, w0, lsl 2
	udiv	w3, w3, w7
	add	w3, w3, 8
	udiv	w0, w0, w8
	strh	w0, [x4,948]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w10
	strh	w0, [x4,176]
	add	x0, x21, :lo12:.LANCHOR3
	strh	w3, [x0,1300]
	strh	w6, [x0,1298]
	bhi	.L4131
	mov	w3, 80
	strh	w3, [x0,1298]
.L4131:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 2000
	add	x6, x20, :lo12:.LANCHOR0
	mov	w4, 256
	cmp	w9, 2
	strh	w3, [x0,1872]
	mov	w3, 50
	strh	w3, [x0,1870]
	add	x3, x19, :lo12:.LANCHOR5
	strh	w4, [x3,172]
	mov	w4, 48
	strh	w4, [x3,174]
	mov	w4, 32
	strh	w4, [x6,1104]
	beq	.L4132
	ldrb	w7, [x0,1264]
	cbz	w7, .L4133
.L4132:
	add	x3, x21, :lo12:.LANCHOR3
	mov	w0, 150
	mov	w4, 64
	mov	w6, 12
	strh	w0, [x3,1870]
	add	x0, x19, :lo12:.LANCHOR5
	strh	w4, [x0,174]
	add	x4, x20, :lo12:.LANCHOR0
	strh	w6, [x4,1104]
	ldrb	w6, [x20,#:lo12:.LANCHOR0]
	cbnz	w6, .L4134
	mov	w6, 4
	strh	w6, [x4,1104]
	mov	w4, 600
	strh	w4, [x3,1872]
	mov	w3, 128
	strh	w3, [x0,172]
.L4134:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1269]
	cbz	w0, .L4136
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 200
	strh	w3, [x0,1870]
	mov	w3, 2000
	strh	w3, [x0,1872]
	b	.L4136
.L4133:
	ldrb	w6, [x6,1268]
	cbz	w6, .L4136
	mov	w6, 1200
	strh	w4, [x0,1870]
	strh	w6, [x0,1872]
	strh	w4, [x3,174]
.L4136:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w3, 1
	mul	w2, w5, w2
	str	wzr, [x0,3412]
	add	x0, x19, :lo12:.LANCHOR5
	cmp	w8, w2, lsl 2
	strb	w3, [x0,929]
	bge	.L4138
	mov	w2, 2
	strb	w2, [x0,929]
.L4138:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4139
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	bl	printk
.L4139:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4140
	add	x1, x20, :lo12:.LANCHOR0
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	ldr	w1, [x1,3368]
	bl	printk
.L4140:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4141
	add	x1, x20, :lo12:.LANCHOR0
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
	ldr	w1, [x1,3372]
	bl	printk
.L4141:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4142
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	ldr	w1, [x1,944]
	bl	printk
.L4142:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4143
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	ldrh	w1, [x1,948]
	bl	printk
.L4143:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4144
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	ldrh	w1, [x1,926]
	bl	printk
.L4144:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4145
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC305
	add	x0, x0, :lo12:.LC305
	ldrh	w1, [x1,176]
	bl	printk
.L4145:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4146
	add	x1, x21, :lo12:.LANCHOR3
	adrp	x0, .LC306
	add	x0, x0, :lo12:.LC306
	ldrh	w1, [x1,1298]
	bl	printk
.L4146:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4147
	add	x1, x21, :lo12:.LANCHOR3
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
	ldrh	w1, [x1,1300]
	bl	printk
.L4147:
	bl	zbuf_init
	add	x25, x19, :lo12:.LANCHOR5
	mov	w0, 16384
	add	x24, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR3
	bl	ftl_malloc
	str	x0, [x25,584]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x25,600]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x25,952]
	ldrh	w1, [x24,1088]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x24,1040]
	ldrb	w1, [x23,1249]
	ldrh	w0, [x23,1294]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x23,1856]
	ldrb	w1, [x23,1249]
	ldrh	w0, [x23,1294]
	ldr	x2, [x25,600]
	str	x2, [x23,1848]
	mul	w0, w0, w1
	add	x3, x2, x0, sxtw 3
	ldrh	w1, [x24,1088]
	str	x3, [x24,1112]
	lsr	w1, w1, 1
	add	w1, w1, w0, lsl 1
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	add	x1, x2, x1, sxtw 2
	str	x1, [x24,1120]
	tbz	x0, 12, .L4148
	adrp	x0, .LC308
	add	x0, x0, :lo12:.LC308
	bl	printk
.L4148:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	add	x3, x19, :lo12:.LANCHOR5
	ldrh	w1, [x0,1088]
	ldrh	w0, [x21,1294]
	ldrb	w21, [x21,1249]
	ldrh	w23, [x3,948]
	mul	w21, w0, w21
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	lsl	w23, w23, 2
	add	w23, w23, w1, lsl 2
	add	w21, w1, w21, lsl 2
	add	w23, w23, 704
	lsl	w21, w21, 1
	add	w21, w21, 632
	tbz	x0, 12, .L4149
	adrp	x0, .LC309
	ldrh	w3, [x3,926]
	add	x0, x0, :lo12:.LC309
	mov	w1, w21
	mov	w2, w23
	bl	printk
.L4149:
	add	x19, x19, :lo12:.LANCHOR5
	ldrh	w0, [x19,926]
	cmp	w21, w0
	bhi	.L4151
	cmp	w23, w0
	bls	.L4150
.L4151:
.L4152:
	b	.L4152
.L4150:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L4153
	bl	ftl_ext_info_init
	add	x21, x20, :lo12:.LANCHOR0
	bl	gc_init
	mov	w0, 1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	x0, [x21,1120]
	mov	x1, 0
	add	x0, x0, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x21,1120]
	add	x0, x1, 48
	add	x1, x1, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x21,3392]
	ldr	w0, [x1,8]
	add	w0, w0, 16
	str	w0, [x1,8]
	ldr	x0, [x21,1120]
	add	x0, x0, 16
	bl	ftl_info_data_recovery
	ldr	x0, [x21,1120]
	add	x0, x0, 48
	bl	ftl_info_data_recovery
	ldr	x0, [x21,1120]
	add	x0, x0, 80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	w0, 1
	bl	ftl_total_vpn_update
	ldrb	w0, [x19,593]
	cbz	w0, .L4154
	ldr	x1, [x21,3392]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L4154:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w0, [x0,124]
	cbnz	w0, .L4158
	ldrh	w2, [x1,3376]
	mov	w19, 16384
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	ble	.L4155
.L4157:
	mov	w0, 0
	b	.L4153
.L4158:
	mov	w19, 16384
.L4155:
	mov	w21, 65535
.L4190:
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w0, 1
	mov	w1, w0
	bl	zftl_do_gc
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w2, [x0,124]
	cbnz	w2, .L4156
	ldrh	w0, [x0,80]
	cmp	w0, w21
	bne	.L4156
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L4157
.L4156:
	subs	w19, w19, #1
	bne	.L4190
	b	.L4157
.L4153:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	zftl_init, .-zftl_init
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	stp	x29, x30, [sp, -48]!
	adrp	x0, jiffies
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR5
	ldr	x0, [x0,#:lo12:jiffies]
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR5
	str	x0, [x19,184]
	mov	w0, 136
	strb	wzr, [x19,960]
	bl	ftl_malloc
	str	x0, [x19,192]
	cbnz	x0, .L4210
.L4212:
	mov	w0, -1
	b	.L4211
.L4210:
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x19,968]
	add	x0, x19, 200
	str	xzr, [x19,200]
	add	x1, x0, 776
	str	xzr, [x19,976]
	bl	rknand_get_reg_addr
	ldr	x0, [x19,200]
	cbz	x0, .L4212
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x19,968]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,192]
	adrp	x1, zftl_deinit
	add	x1, x1, :lo12:zftl_deinit
	str	x1, [x0,80]
	adrp	x1, zftl_cache_flush
	add	x1, x1, :lo12:zftl_cache_flush
	str	x1, [x0,64]
	adrp	x1, zftl_flash_suspend
	add	x1, x1, :lo12:zftl_flash_suspend
	str	x1, [x0,88]
	adrp	x1, zftl_flash_resume
	add	x1, x1, :lo12:zftl_flash_resume
	str	x1, [x0,96]
	adrp	x1, zftl_get_density
	add	x1, x1, :lo12:zftl_get_density
	str	x1, [x0,72]
	adrp	x1, zftl_read_flash_info
	add	x1, x1, :lo12:zftl_read_flash_info
	str	x1, [x0,112]
	adrp	x1, zftl_read
	add	x1, x1, :lo12:zftl_read
	str	x1, [x0,40]
	adrp	x1, zftl_write
	add	x1, x1, :lo12:zftl_write
	str	x1, [x0,48]
	adrp	x1, zftl_sys_read
	add	x1, x1, :lo12:zftl_sys_read
	str	x1, [x0,24]
	adrp	x1, zftl_sys_write
	add	x1, x1, :lo12:zftl_sys_write
	str	x1, [x0,32]
	adrp	x1, zftl_vendor_read
	add	x1, x1, :lo12:zftl_vendor_read
	str	x1, [x0,8]
	adrp	x1, zftl_vendor_write
	add	x1, x1, :lo12:zftl_vendor_write
	str	x1, [x0,16]
	adrp	x1, zftl_nandc_get_irq_status
	add	x1, x1, :lo12:zftl_nandc_get_irq_status
	str	x1, [x0,120]
	adrp	x1, zftl_proc_ftl_read
	add	x1, x1, :lo12:zftl_proc_ftl_read
	str	x1, [x0,128]
	adrp	x1, zftl_do_gc
	add	x1, x1, :lo12:zftl_do_gc
	str	x1, [x0,104]
	adrp	x1, zftl_discard
	add	x1, x1, :lo12:zftl_discard
	str	x1, [x0,56]
	ldr	x0, [x19,200]
	bl	nand_flash_init
	mov	w22, w0
	cbnz	w0, .L4213
	bl	zftl_init
	mov	w20, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x19,960]
	b	.L4214
.L4213:
	ldr	x1, [x19,192]
	adrp	x2, ftl_get_density
	add	x2, x2, :lo12:ftl_get_density
	adrp	x0, ftl_deinit
	add	x0, x0, :lo12:ftl_deinit
	str	x2, [x1,72]
	adrp	x2, ftl_read_flash_info
	add	x2, x2, :lo12:ftl_read_flash_info
	str	x2, [x1,112]
	adrp	x2, ftl_read
	str	x0, [x1,80]
	add	x2, x2, :lo12:ftl_read
	str	x2, [x1,40]
	adrp	x2, ftl_write
	adrp	x0, ftl_cache_flush
	add	x2, x2, :lo12:ftl_write
	str	x2, [x1,48]
	adrp	x2, ftl_sys_read
	add	x0, x0, :lo12:ftl_cache_flush
	add	x2, x2, :lo12:ftl_sys_read
	str	x2, [x1,24]
	adrp	x2, ftl_sys_write
	str	x0, [x1,64]
	add	x2, x2, :lo12:ftl_sys_write
	str	x2, [x1,32]
	adrp	x2, ftl_vendor_read
	adrp	x0, ftl_flash_suspend
	add	x2, x2, :lo12:ftl_vendor_read
	str	x2, [x1,8]
	adrp	x2, ftl_vendor_write
	add	x0, x0, :lo12:ftl_flash_suspend
	add	x2, x2, :lo12:ftl_vendor_write
	str	x2, [x1,16]
	adrp	x2, ftl_nandc_get_irq_status
	str	x0, [x1,88]
	add	x2, x2, :lo12:ftl_nandc_get_irq_status
	str	x2, [x1,120]
	adrp	x2, ftl_proc_ftl_read
	adrp	x0, ftl_flash_resume
	add	x2, x2, :lo12:ftl_proc_ftl_read
	str	x2, [x1,128]
	adrp	x2, ftl_do_gc
	add	x0, x0, :lo12:ftl_flash_resume
	add	x2, x2, :lo12:ftl_do_gc
	str	x0, [x1,96]
	str	x2, [x1,104]
	adrp	x2, ftl_discard
	ldr	x0, [x19,200]
	add	x2, x2, :lo12:ftl_discard
	str	x2, [x1,56]
	bl	FlashInit
	mov	w20, w0
	cmn	w22, #2
	adrp	x22, .LANCHOR2
	bne	.L4215
	add	x19, x22, :lo12:.LANCHOR2
	adrp	x1, gNandParaInfo
	add	x19, x19, 8
	add	x1, x1, :lo12:gNandParaInfo
	mov	w2, 32
	mov	x0, x19
	bl	ftl_memcpy
	ldrb	w0, [x19,18]
	bl	flash_lsb_page_tbl_build
	ldrh	w0, [x19,10]
	strh	w0, [x19,26]
.L4215:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x1, .LANCHOR0
	bne	.L4216
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L4217
.L4216:
	add	x0, x1, :lo12:.LANCHOR0
	strb	wzr, [x1,#:lo12:.LANCHOR0]
	strb	wzr, [x0,1146]
.L4217:
	adrp	x2, gNandFlashIDBEccBits
	add	x0, x1, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w2, [x2,#:lo12:gNandFlashIDBEccBits]
	strb	w2, [x0,1144]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x22,18]
	str	xzr, [x21,240]
	ldrb	w2, [x2,#:lo12:gNandFlashEccBits]
	strb	w2, [x0,1236]
	mov	w2, 1
	mov	w0, 0
.L4218:
	cmp	w2, w3
	bhi	.L4223
	add	w0, w0, 1
	lsl	w2, w2, 1
	uxth	w0, w0
	b	.L4218
.L4223:
	adrp	x2, .LANCHOR3+1232
	add	x1, x1, :lo12:.LANCHOR0
	sub	w0, w0, #1
	strb	wzr, [x1,1184]
	strh	w0, [x2,#:lo12:.LANCHOR3+1232]
	cbnz	w20, .L4214
	bl	FtlInit
	mov	w20, w0
.L4214:
	adrp	x0, .LC310
	mov	w1, w20
	add	x0, x0, :lo12:.LC310
	bl	printk
	mov	w0, w20
.L4211:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	zftl_write
	.type	zftl_write, %function
zftl_write:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	adrp	x0, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w19, w1
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	w21, w2
	mov	x25, x3
	tbz	x0, 12, .L4225
	adrp	x0, .LC311
	ldr	w4, [x25]
	add	x0, x0, :lo12:.LC311
	mov	w1, w20
	mov	w2, w19
	mov	w3, w21
	bl	printk
.L4225:
	cbnz	w20, .L4226
	adrp	x0, .LANCHOR0+1032
	mov	w20, 24576
	ldr	w1, [x0,#:lo12:.LANCHOR0+1032]
	b	.L4227
.L4226:
	cmp	w20, 3
	mov	w0, -1
	bhi	.L4228
	lsl	w20, w20, 13
	mov	w1, 8192
.L4227:
	cmp	w19, w1
	bcs	.L4244
	cmp	w21, w1
	bhi	.L4244
	add	w2, w19, w21
	mov	w0, -1
	cmp	w2, w1
	bhi	.L4228
	add	w19, w20, w19
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	sub	w24, w21, #1
	add	w24, w24, w19
	add	w27, w19, w21
	ldrb	w0, [x0,3360]
	udiv	w26, w19, w0
	mov	w23, w26
	udiv	w24, w24, w0
	sub	w22, w24, w26
	add	w22, w22, 1
.L4229:
	cbz	w22, .L4255
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,3388]
	cbz	w1, .L4230
	ldrb	w0, [x0,3362]
	cmp	w0, 2
	bhi	.L4230
	bl	ftl_write_commit
.L4230:
	mov	w0, 0
	bl	buf_alloc
	mov	x28, x0
	cbnz	x0, .L4231
	bl	ftl_write_commit
	b	.L4229
.L4231:
	strb	wzr, [x0,57]
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w23, w26
	ldrb	w4, [x0,3360]
	cset	w0, eq
	strb	w4, [x28,56]
	cmp	w23, w24
	beq	.L4233
	cbz	w0, .L4234
	b	.L4246
.L4233:
	cbz	w0, .L4235
.L4246:
	udiv	w0, w19, w4
	msub	w0, w0, w4, w19
	uxtb	w0, w0
	strb	w0, [x28,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w21
	csel	w4, w21, w4, hi
	b	.L4253
.L4235:
	msub	w4, w4, w23, w27
.L4253:
	strb	w4, [x28,56]
.L4234:
	ldrb	w1, [x28,57]
	sub	w22, w22, #1
	ldr	x0, [x28,8]
	ldrb	w2, [x28,56]
	add	x0, x0, x1, lsl 9
	mov	x1, x25
	lsl	w2, w2, 9
	bl	ftl_memcpy
	str	w23, [x28,36]
	add	x0, x20, :lo12:.LANCHOR0
	add	w23, w23, 1
	ldr	x1, [x0,3392]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x28,32]
	mov	x0, x28
	bl	ftl_write_buf
	ldrb	w0, [x28,56]
	add	x25, x25, x0, lsl 9
	b	.L4229
.L4255:
	bl	ftl_write_commit
	mov	w1, 1
	mov	w0, w22
	bl	zftl_do_gc
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w0, [x0,124]
	cbnz	w0, .L4239
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 11
	bgt	.L4241
.L4239:
	mov	w0, 0
	mov	w1, 1
.L4254:
	bl	zftl_do_gc
.L4241:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L4256
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w0, 1
	mov	w1, w0
	b	.L4254
.L4256:
	bl	timer_get_time
	adrp	x1, .LANCHOR5+936
	str	w0, [x1,#:lo12:.LANCHOR5+936]
	mov	w0, 0
	b	.L4228
.L4244:
	mov	w0, -1
.L4228:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	zftl_write, .-zftl_write
	.align	2
	.global	zftl_vendor_write
	.type	zftl_vendor_write, %function
zftl_vendor_write:
	stp	x29, x30, [sp, -16]!
	mov	w4, w1
	mov	x3, x2
	add	x29, sp, 0
	add	w1, w0, 512
	mov	w2, w4
	mov	w0, 2
	bl	zftl_write
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_vendor_write, .-zftl_vendor_write
	.align	2
	.global	zftl_sys_write
	.type	zftl_sys_write, %function
zftl_sys_write:
	stp	x29, x30, [sp, -16]!
	mov	w5, w0
	mov	w4, w1
	add	x29, sp, 0
	mov	x3, x2
	mov	w1, w5
	mov	w2, w4
	mov	w0, 2
	bl	zftl_write
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_sys_write, .-zftl_sys_write
	.align	2
	.global	zftl_discard
	.type	zftl_discard, %function
zftl_discard:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w1, [x1,1032]
	cmp	w0, w1
	bcs	.L4280
	cmp	w19, w1
	cset	w5, hi
	cbnz	w5, .L4280
	add	w2, w0, w19
	mov	w4, -1
	cmp	w2, w1
	bhi	.L4260
	adrp	x23, .LANCHOR5
	add	w22, w0, 24576
	add	x0, x23, :lo12:.LANCHOR5
	ldr	w1, [x0,984]
	add	w1, w19, w1
	str	w1, [x0,984]
	adrp	x0, .LANCHOR2
	str	x0, [x29,104]
	ldr	w2, [x0,#:lo12:.LANCHOR2]
	tbz	x2, 12, .L4261
	adrp	x0, .LC312
	mov	w2, w22
	add	x0, x0, :lo12:.LC312
	mov	w3, w19
	mov	w4, w5
	bl	printk
.L4261:
	add	x20, x21, :lo12:.LANCHOR0
	ldr	x0, [x20,3392]
	ldr	w25, [x0,8]
	add	w1, w25, 1
	str	w1, [x0,8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	w24, [x20,3360]
	udiv	w20, w22, w24
	msub	w26, w20, w24, w22
	cbz	w26, .L4262
	sub	w24, w24, w26
	mov	w0, w20
	cmp	w24, w19
	csel	w24, w24, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L4263
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L4263:
	ldr	w0, [x29,120]
	and	w27, w24, 65535
	cmn	w0, #1
	beq	.L4265
	mov	w0, 0
	bl	buf_alloc
	mov	x28, x0
	cbz	x0, .L4265
	strb	w26, [x0,57]
	ubfiz	x26, x26, 9, 25
	str	w20, [x0,36]
	mov	w1, 0
	strb	w24, [x0,56]
	lsl	w2, w27, 9
	str	w25, [x0,32]
	ldr	x0, [x0,8]
	add	x0, x0, x26
	bl	ftl_memset
	mov	x0, x28
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L4265:
	add	w20, w20, 1
	sub	w19, w19, w27
.L4262:
	cbz	w19, .L4267
	bl	ftl_flush
.L4267:
	mov	w0, -1
	mov	w26, 1
	str	w0, [x29,124]
	adrp	x27, .LANCHOR3
	mov	w28, 24
.L4268:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,3360]
	cmp	w19, w0
	bcc	.L4308
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L4269
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L4271
	ldrb	w2, [x24,3360]
	mov	w1, 0
	str	w20, [x0,36]
	strb	w2, [x0,56]
	lsl	w2, w2, 9
	str	w25, [x0,32]
	strb	wzr, [x0,57]
	ldr	x0, [x0,8]
	str	x3, [x29,96]
	bl	ftl_memset
	ldr	x3, [x29,96]
	mov	x0, x3
	bl	ftl_write_buf
	bl	ftl_write_commit
	b	.L4307
.L4269:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L4271
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	add	x2, x27, :lo12:.LANCHOR3
	ldrb	w1, [x24,1257]
	ldr	w0, [x29,120]
	sub	w1, w28, w1
	ldrh	w3, [x2,1232]
	sub	w1, w1, w3
	lsr	w0, w0, w3
	lsl	w1, w26, w1
	sub	w1, w1, #1
	and	w1, w1, w0
	ldrb	w0, [x2,1234]
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L4307:
	ldr	x1, [x24,3392]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L4271:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,3360]
	sub	w19, w19, w0
	b	.L4268
.L4308:
	cbz	w19, .L4275
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L4276
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L4276:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L4275
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L4275
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w25, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w19, w0
	bcc	.L4278
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1477
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4278:
	ldr	x0, [x24,8]
	mov	w1, 0
	lsl	w2, w19, 9
	bl	ftl_memset
	mov	x0, x24
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L4275:
	add	x0, x23, :lo12:.LANCHOR5
	mov	w4, 0
	ldr	w1, [x0,984]
	cmp	w1, 8192
	bls	.L4260
	ldr	x0, [x29,104]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4279
	adrp	x0, .LC312
	mov	w2, w22
	add	x0, x0, :lo12:.LC312
	mov	w3, w19
	bl	printk
.L4279:
	add	x23, x23, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x23,984]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x21,3412]
	b	.L4260
.L4280:
	mov	w4, -1
.L4260:
	mov	w0, w4
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	zftl_discard, .-zftl_discard
	.align	2
	.global	dump_pm_blk
	.type	dump_pm_blk, %function
dump_pm_blk:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC264
	mov	w2, 4
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	add	x0, x0, :lo12:.LC264
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR5
	mov	w22, 65535
	ldr	x3, [x19,3392]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x1, [x19,3392]
	adrp	x0, .LC265
	mov	w2, 2
	add	x0, x0, :lo12:.LC265
	add	x1, x1, 416
	ldrh	w3, [x3,176]
	mov	w19, 0
	bl	rknand_print_hex
.L4310:
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w0, [x0,176]
	cmp	w0, w19
	bls	.L4313
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L4311
	mov	x1, 0
	bl	ftl_sblk_dump
.L4311:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L4310
.L4313:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	dump_pm_blk, .-dump_pm_blk
	.align	2
	.global	id_block_prog_msb_ff_data
	.type	id_block_prog_msb_ff_data, %function
id_block_prog_msb_ff_data:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxth	w19, w2
	ldr	x2, [x0,1136]
	ldrb	w0, [x0,1146]
	ldrb	w2, [x2,19]
	cbnz	w0, .L4314
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L4316
	cmp	w2, 68
	beq	.L4316
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L4314
.L4316:
	adrp	x20, .LANCHOR3
	adrp	x24, .LC313
	add	x20, x20, :lo12:.LANCHOR3
	mov	w25, w1
	add	x24, x24, :lo12:.LC313
	add	x20, x20, 2396
	mov	w27, 65535
.L4318:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,1136]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L4314
	add	w26, w19, w25
	ldrh	w3, [x20,w19,sxtw 1]
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	bl	printk
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w27
	bne	.L4314
	adrp	x22, .LANCHOR5
	mov	w1, 255
	add	x22, x22, :lo12:.LANCHOR5
	mov	w2, 16384
	add	w19, w19, 1
	ldr	x0, [x22,208]
	uxth	w19, w19
	bl	ftl_memset
	ldr	x4, [x28,1136]
	mov	w0, w23
	ldr	x2, [x22,208]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L4318
.L4314:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -304]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	add	x3, x23, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x4, [x3,1136]
	ldrb	w3, [x3,1146]
	str	w3, [x29,140]
	ldrb	w20, [x4,9]
	ldrh	w24, [x4,10]
	adrp	x4, .LANCHOR2+34
	ldr	w3, [x1]
	ldrh	w21, [x4,#:lo12:.LANCHOR2+34]
	ldrb	w4, [x23,#:lo12:.LANCHOR0]
	str	w4, [x29,156]
	ldrh	w4, [x29,156]
	str	w4, [x29,212]
	mov	w4, 19282
	movk	w4, 0x534e, lsl 16
	cmp	w3, w4
	mov	w4, 35899
	movk	w4, 0xfcdc, lsl 16
	cset	w5, ne
	cmp	w3, w4
	cset	w4, ne
	tst	w5, w4
	beq	.L4321
	mov	w4, 19282
	movk	w4, 0x5353, lsl 16
	cmp	w3, w4
	bne	.L4455
.L4321:
	cmp	w0, 15
	bls	.L4455
	mov	w19, w0
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	str	x2, [x29,160]
	str	x1, [x29,216]
	bl	ftl_malloc
	mov	x22, x0
	cbz	x0, .L4455
	adrp	x0, .LANCHOR5+960
	str	wzr, [x29,236]
	ldrb	w0, [x0,#:lo12:.LANCHOR5+960]
	cbz	w0, .L4448
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x23,#:lo12:.LANCHOR0]
	ldrb	w0, [x0,1146]
	cbz	w1, .L4325
	cmp	w0, wzr
	cset	w0, eq
	b	.L4451
.L4325:
	cmp	w0, 3
	beq	.L4379
	ldr	w2, [x29,212]
	cmp	w0, 2
	mov	w1, 2
	csel	w1, w2, w1, ne
	cmp	w0, 2
	mov	w0, 3
	str	w1, [x29,212]
	csel	w0, wzr, w0, ne
	b	.L4451
.L4379:
	str	w0, [x29,212]
	mov	w0, 2
.L4451:
	str	w0, [x29,236]
.L4448:
	ldr	x0, [x29,216]
	mov	w1, 19282
	movk	w1, 0x534e, lsl 16
	ldr	w0, [x0]
	cmp	w0, w1
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	cset	w2, ne
	cmp	w0, w1
	cset	w1, ne
	tst	w2, w1
	beq	.L4327
	mov	w1, 19282
	movk	w1, 0x5353, lsl 16
	cmp	w0, w1
	beq	.L4327
	mov	x0, x22
	bl	ftl_free
.L4455:
	mov	w0, -1
	b	.L4449
.L4327:
	mov	w0, 0
	add	w19, w19, 511
	mul	w24, w20, w24
	bl	zftl_flash_exit_slc_mode
	lsr	w19, w19, 9
	add	x0, x23, :lo12:.LANCHOR0
	cmp	w19, 8
	mov	w3, 0
	mov	w2, 63871
	ldrb	w0, [x0,1146]
	strb	w0, [x23,#:lo12:.LANCHOR0]
	mov	w0, 8
	csel	w19, w19, w0, cs
	sub	w0, w24, #1
	add	w0, w0, w19
	udiv	w0, w0, w24
	str	w0, [x29,136]
	ldr	x0, [x29,216]
	add	x0, x0, 253952
	add	x0, x0, 2044
.L4333:
	sub	x1, x0, #512
	ldr	w1, [x1]
	cbnz	w1, .L4328
	cbnz	w3, .L4329
	mov	w1, 17739
	movk	w1, 0x4e52, lsl 16
	b	.L4452
.L4329:
	ldr	x1, [x29,216]
	ldr	w1, [x1,w3,uxtw 2]
.L4452:
	add	w3, w3, 1
	sub	w2, w2, #1
	cmp	w3, 4095
	str	w1, [x0]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L4333
	b	.L4332
.L4328:
	adrp	x0, .LC314
	add	x0, x0, :lo12:.LC314
	bl	printk
.L4332:
	mul	w0, w20, w21
	str	w0, [x29,168]
	adrp	x0, .LC315
	ldr	x1, [x29,160]
	add	x0, x0, :lo12:.LC315
	mov	w2, 4
	mov	w3, 5
	mov	x21, 0
	bl	rknand_print_hex
	adrp	x0, .LC316
	mov	w1, w19
	add	x0, x0, :lo12:.LC316
	mov	w2, w19
	bl	printk
	str	w21, [x29,232]
	adrp	x0, .LC317
	add	x0, x0, :lo12:.LC317
	str	x0, [x29,112]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	x0, [x29,104]
.L4375:
	adrp	x26, .LANCHOR5
	mov	w0, w21
	add	x1, x26, :lo12:.LANCHOR5
	ldrb	w1, [x1,960]
	cbnz	w1, .L4334
	ldr	x1, [x29,160]
	ldr	w2, [x29,168]
	ldr	w1, [x1,x21,lsl 2]
	mul	w20, w1, w2
	cbz	w21, .L4335
	ldr	w2, [x29,136]
	cmp	w2, 1
	bls	.L4335
	sub	w0, w21, #1
	ldr	x2, [x29,160]
	ldr	w0, [x2,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L4335
	b	.L4336
.L4334:
	adrp	x24, .LANCHOR0
	add	x25, x24, :lo12:.LANCHOR0
	ldr	x1, [x25,1240]
	add	x1, x1, x21, uxtw
	ldrb	w20, [x1,32]
	mov	x1, x24
	cmp	w20, 255
	beq	.L4336
	ldr	w2, [x29,168]
	mul	w20, w20, w2
	ldr	w2, [x29,236]
	cbz	w2, .L4335
	tbnz	x0, 0, .L4385
	cmp	w2, 3
	cset	w23, eq
	cbz	w23, .L4337
.L4385:
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w0, [x29,212]
	strb	w0, [x1,#:lo12:.LANCHOR0]
	strb	w0, [x2,1146]
	b	.L4335
.L4337:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	strb	w23, [x24,#:lo12:.LANCHOR0]
	strb	w23, [x25,1146]
.L4335:
	mov	w1, 0
	mov	w2, 512
	mov	x0, x22
	adrp	x24, .LANCHOR0
	bl	ftl_memset
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0,1136]
	ldrb	w25, [x1,9]
	ldrh	w28, [x1,10]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mul	w28, w25, w28
	ldrh	w27, [x1,34]
	ldrb	w1, [x0,1232]
	str	w1, [x29,132]
	udiv	w1, w20, w25
	strb	wzr, [x0,1232]
	mov	w0, 0
	mul	w27, w25, w27
	bl	flash_erase_block
	cmp	w19, w28
	mov	w1, 1
	bls	.L4339
	add	w1, w20, w27
	mov	w0, 0
	bl	flash_erase_block
	mov	w1, 2
.L4339:
	add	x3, x24, :lo12:.LANCHOR0
	ldr	x0, [x3,1136]
	ldrh	w23, [x0,10]
	ldrb	w0, [x0,12]
	lsl	w23, w23, 2
	mul	w23, w23, w1
	sdiv	w0, w23, w0
	str	w0, [x29,204]
	udiv	w0, w20, w27
	msub	w0, w0, w27, w20
	str	w0, [x29,208]
	sub	w28, w20, w0
	cmp	w20, w28
	bne	.L4383
	ldrb	w0, [x3,1028]
	cmp	w0, 9
	bne	.L4383
	add	x26, x26, :lo12:.LANCHOR5
	mov	w1, 0
	mov	w2, 1024
	str	x3, [x29,224]
	ldr	x26, [x26,248]
	mov	x0, x26
	bl	ftl_memset
	str	wzr, [x26,12]
	mov	w0, 18766
	ldr	x3, [x29,224]
	movk	w0, 0x464e, lsl 16
	str	w0, [x26]
	mov	w0, 12
	str	w0, [x26,4]
	ldrb	w0, [x3,1146]
	strb	wzr, [x26,16]
	cbz	w0, .L4341
	ldr	x0, [x3,1136]
	ldrb	w0, [x0,29]
	strb	w0, [x26,16]
.L4341:
	add	x24, x24, :lo12:.LANCHOR0
	mov	w0, 4
	strb	w0, [x26,17]
	cmp	w25, 8
	ldr	x1, [x24,1136]
	ldrh	w0, [x1,10]
	ldrb	w1, [x1,12]
	strb	wzr, [x26,20]
	strh	wzr, [x26,22]
	sdiv	w0, w0, w1
	mov	w1, 16
	strh	w0, [x26,18]
	mov	w0, 70
	csel	w0, w0, w1, hi
	mov	w1, 12
	strb	w0, [x26,21]
	add	x0, x26, 12
	bl	js_hash
	str	w0, [x26,8]
	sub	w0, w19, #4
	str	w0, [x29,224]
	b	.L4340
.L4383:
	str	w19, [x29,224]
	mov	x26, 0
.L4340:
	adrp	x10, .LANCHOR0
	ldr	x24, [x29,216]
	mov	w23, 0
	add	x27, x10, :lo12:.LANCHOR0
.L4343:
	ldr	w0, [x29,204]
	cmp	w23, w0
	bcs	.L4456
	ldr	w0, [x29,208]
	ldrb	w2, [x27,1146]
	add	w8, w23, w0
	add	x0, x27, 4
	ubfx	x8, x8, 2, 16
	add	w1, w8, 1
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L4345
	ldrb	w3, [x27,1]
	lsl	w0, w1, 1
	cmp	w3, wzr
	csel	w0, w0, w1, ne
.L4345:
	ldrb	w1, [x27,1028]
	cmp	w1, 9
	beq	.L4453
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L4453:
	str	w0, [x29,240]
	mov	w0, 61424
	str	w0, [x29,244]
	add	x0, x27, 4
	ldrh	w0, [x0,w8,sxtw 1]
	cbnz	w2, .L4349
	mov	w8, w0
	b	.L4350
.L4349:
	ldrb	w1, [x27,1]
	lsl	w0, w8, 1
	cmp	w1, wzr
	csel	w8, w0, w8, ne
.L4350:
	mul	w0, w8, w25
	cbnz	x26, .L4351
	add	w26, w0, w28
	ldr	w5, [x24]
	ldr	w6, [x29,240]
	mov	w1, w26
	ldr	x0, [x29,112]
	mov	w2, w23
	mov	w3, w19
	mov	x4, x24
	mov	w7, 61424
	str	x8, [x29,192]
	bl	printk
	mov	w0, w26
	mov	x1, x24
	add	x2, x29, 240
	bl	fw_flash_page_prog.constprop.31
	ldrb	w0, [x27,1146]
	ldr	x8, [x29,192]
	cbnz	w0, .L4352
	udiv	w1, w28, w25
	add	w2, w8, 1
	bl	id_block_prog_msb_ff_data
.L4352:
	ldr	w0, [x29,224]
	add	x24, x24, 2048
	add	w0, w0, 16
	cmp	w23, w0
	bcc	.L4353
	ldr	w0, [x29,224]
	add	w1, w0, 20
	ldr	x0, [x29,216]
	cmp	w23, w1
	add	x0, x0, 2048
	csel	x24, x24, x0, cs
	b	.L4353
.L4351:
	add	w0, w0, w28
	mov	x1, x26
	add	x2, x29, 240
	bl	fw_flash_page_prog.constprop.31
.L4353:
	add	w9, w23, 4
	mov	x26, 0
	uxth	w23, w9
	b	.L4343
.L4456:
	ldr	x0, [x29,104]
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	mov	w25, 0
	add	x0, x0, 8
	mov	x24, x22
	ldrb	w1, [x0,9]
	mov	w26, 4
	str	w1, [x29,152]
	ldrh	w0, [x0,26]
	strb	wzr, [x23,1232]
	str	w25, [x29,200]
	mul	w0, w1, w0
	udiv	w1, w20, w0
	msub	w0, w1, w0, w20
	str	w0, [x29,204]
	sub	w0, w20, w0
	str	w0, [x29,148]
	ldr	w0, [x29,204]
	and	w0, w0, 3
	str	w0, [x29,192]
	ldr	w0, [x29,224]
	str	w0, [x29,172]
	adrp	x0, .LC318
	add	x0, x0, :lo12:.LC318
	str	x0, [x29,120]
.L4355:
	ldr	w0, [x29,172]
	adrp	x27, .LANCHOR0
	cmp	w25, w0
	bcs	.L4457
	ldr	w0, [x29,192]
	add	x1, x23, 4
	ldrb	w2, [x23,1146]
	sub	w0, w26, w0
	uxth	w0, w0
	str	w0, [x29,176]
	ldr	w0, [x29,204]
	add	w0, w25, w0
	udiv	w0, w0, w26
	and	w0, w0, 65535
	ldrh	w1, [x1,w0,sxtw 1]
	cbnz	w2, .L4356
	mov	w0, w1
	b	.L4357
.L4356:
	ldrb	w2, [x23,1]
	lsl	w1, w0, 1
	cmp	w2, wzr
	csel	w0, w1, w0, ne
.L4357:
	ldr	w1, [x29,152]
	ldr	w2, [x29,148]
	madd	w0, w0, w1, w2
	str	w0, [x29,144]
	ldrb	w0, [x23,1144]
	str	w0, [x29,208]
	ldr	w1, [x29,192]
	ldr	w0, [x29,144]
	add	w27, w0, w1
	ldr	x0, [x23,1136]
	ldrb	w0, [x0,9]
	udiv	w27, w27, w0
	ldrb	w0, [x23,1236]
	str	w0, [x29,128]
	ldr	w0, [x29,208]
	bl	nandc_bch_sel
.L4358:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	bl	flash_read_page
	cmn	w0, #1
	mov	w28, w0
	bne	.L4365
	ldrb	w6, [x23,1232]
	cbnz	w6, .L4360
.L4363:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x6, [x0,240]
	cbnz	x6, .L4361
	b	.L4362
.L4360:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	str	x6, [x29,96]
	strb	wzr, [x23,1232]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x6, [x29,96]
	strb	w6, [x23,1232]
	beq	.L4363
	b	.L4454
.L4361:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	blr	x6
	cmn	w0, #1
	bne	.L4454
.L4362:
	ldrb	w0, [x23,1184]
	cbz	w0, .L4365
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	bl	flash_ddr_tuning_read
.L4454:
	mov	w28, w0
.L4365:
	cmn	w28, #1
	bne	.L4366
	ldr	w0, [x29,208]
	cmp	w0, 16
	beq	.L4366
	mov	w0, 16
	bl	nandc_bch_sel
	mov	w0, 16
	str	w0, [x29,208]
	b	.L4358
.L4366:
	ldr	w0, [x29,128]
	bl	nandc_bch_sel
	ldr	w0, [x29,200]
	cmn	w28, #1
	csinv	w0, w0, wzr, ne
	str	w0, [x29,200]
	ldr	w0, [x29,148]
	cmp	w25, wzr
	cset	w1, eq
	cmp	w0, w20
	cset	w0, eq
	tst	w1, w0
	beq	.L4368
	ldr	w0, [x29,200]
	cbnz	w0, .L4368
	ldr	w1, [x24]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L4368
	ldr	w0, [x29,172]
	ldr	w1, [x29,176]
	ldrb	w26, [x24,17]
	add	w0, w0, w1
	str	w0, [x29,172]
	b	.L4369
.L4368:
	ldr	x0, [x29,176]
	mov	w2, w25
	ldr	w1, [x29,144]
	ubfiz	x0, x0, 9, 16
	ldr	w3, [x29,240]
	add	x24, x24, x0
	ldr	w4, [x29,244]
	ldr	x0, [x29,120]
	bl	printk
	str	wzr, [x29,192]
.L4369:
	ldr	w0, [x29,176]
	add	w25, w0, w25
	uxth	w25, w25
	b	.L4355
.L4457:
	add	x23, x27, :lo12:.LANCHOR0
	ldrb	w0, [x29,132]
	strb	w0, [x23,1232]
	ldr	w0, [x29,236]
	cbz	w0, .L4371
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	strb	wzr, [x27,#:lo12:.LANCHOR0]
	strb	wzr, [x23,1146]
.L4371:
	ldr	w0, [x29,224]
	mov	x1, 0
	lsl	w0, w0, 7
.L4372:
	cmp	w0, w1
	bls	.L4458
	ldr	w3, [x22,x1,lsl 2]
	add	x1, x1, 1
	ldr	x2, [x29,216]
	add	x2, x2, x1, lsl 2
	ldr	w2, [x2,-4]
	cmp	w3, w2
	beq	.L4372
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L4336
.L4458:
	ldr	w0, [x29,232]
	add	w0, w0, 1
	str	w0, [x29,232]
	cmp	w0, 5
	bhi	.L4377
.L4336:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L4375
.L4377:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR0
	ldrb	w2, [x29,156]
	add	x1, x0, :lo12:.LANCHOR0
	strb	w2, [x0,#:lo12:.LANCHOR0]
	ldr	w2, [x29,236]
	cbz	w2, .L4376
	ldrb	w2, [x29,140]
	strb	w2, [x1,1146]
.L4376:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 2
	strb	w1, [x0,1264]
	mov	w0, 0
	bl	zftl_flash_enter_slc_mode
	mov	x0, x22
	bl	ftl_free
	ldr	w0, [x29,232]
	cmp	w0, wzr
	csetm	w0, eq
.L4449:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 304
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	stp	x29, x30, [sp, -112]!
	cmp	w0, 64
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w19, w0
	mov	w21, w1
	mov	x24, x2
	adrp	x20, .LANCHOR5
	bne	.L4460
	ldr	w0, [x2]
	mov	w1, 19282
	movk	w1, 0x534e, lsl 16
	mov	w2, 35899
	cmp	w0, w1
	movk	w2, 0xfcdc, lsl 16
	cset	w3, eq
	cmp	w0, w2
	cset	w1, eq
	orr	w1, w3, w1
	cbnz	w1, .L4476
	mov	w1, 19282
	movk	w1, 0x5353, lsl 16
	cmp	w0, w1
	bne	.L4460
.L4476:
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22,988]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,992]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	mov	w0, 64
	str	w0, [x22,1000]
.L4460:
	add	x23, x20, :lo12:.LANCHOR5
	adrp	x0, .LC319
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC319
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,992]
	bl	printk
	ldrb	w0, [x23,988]
	cbz	w0, .L4459
	ldr	x22, [x23,992]
	cbz	x22, .L4459
	sub	w0, w19, #64
	cmp	w0, 499
	bhi	.L4463
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L4464
.L4463:
	cmp	w19, 563
	bhi	.L4474
.L4464:
	add	x23, x20, :lo12:.LANCHOR5
	ldr	w0, [x23,1000]
	cmp	w0, w19
	beq	.L4472
	mov	x0, x22
	strb	wzr, [x23,988]
	bl	ftl_free
	str	xzr, [x23,992]
.L4472:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,1000]
	b	.L4459
.L4474:
	ldr	w2, [x23,1000]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+1136
	ldr	x0, [x0,#:lo12:.LANCHOR0+1136]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L4475
	mov	w0, 2
	str	w0, [x29,72]
	mov	w0, 3
	str	w0, [x29,76]
	mov	w0, 4
	str	w0, [x29,80]
	mov	w0, 5
	str	w0, [x29,84]
	mov	w0, 6
	str	w0, [x29,88]
	b	.L4466
.L4475:
	mov	x3, 0
.L4465:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L4467
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L4468
.L4467:
	str	w3, [x0,x3,lsl 2]
.L4468:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L4465
.L4466:
	add	x0, x22, 245760
	mov	w3, 61952
	add	x0, x0, 2048
.L4471:
	ldr	w1, [x0]
	cbz	w1, .L4469
	add	w2, w3, 2048
	lsl	w0, w2, 2
	b	.L4470
.L4469:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L4471
	lsl	w0, w2, 9
.L4470:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,992]
	strb	wzr, [x22,988]
	bl	ftl_free
	str	xzr, [x22,992]
	b	.L4472
.L4459:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 112
	ret
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	str	x21, [sp,32]
	uxtb	w21, w0
	sub	w0, w1, #64
	stp	x19, x20, [sp,16]
	cmp	w0, 1983
	mov	w19, w1
	mov	w20, w2
	bhi	.L4489
	cbnz	w21, .L4489
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L4489:
	adrp	x0, .LANCHOR5+192
	mov	w1, w19
	mov	w2, w20
	ldr	x0, [x0,#:lo12:.LANCHOR5+192]
	ldr	x4, [x0,48]
	mov	w0, w21
	blr	x4
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	sub	sp, sp, #528
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	mov	x20, x2
	beq	.L4498
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4499
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4500
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4501
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4502
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4503
	b	.L4534
.L4501:
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4504
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4505
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L4534
	b	.L4556
.L4499:
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4507
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4508
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4498
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4498
	b	.L4534
.L4508:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4507
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L4509
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4509
	b	.L4534
.L4503:
	adrp	x0, .LC320
	add	x0, x0, :lo12:.LC320
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbz	x0, .L4510
.L4516:
	adrp	x0, .LC321
	add	x0, x0, :lo12:.LC321
	bl	printk
	b	.L4553
.L4510:
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L4511
.L4513:
	mov	x19, -1
	b	.L4512
.L4511:
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4513
	adrp	x19, .LANCHOR5
	add	x0, x29, 40
	add	x19, x19, :lo12:.LANCHOR5
	mov	x2, 512
	ldr	x1, [x19,1008]
	bl	memcpy
	ldr	w1, [x19,1016]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L4514
	add	x0, x29, 104
	mov	w1, 0
	mov	x2, 128
	str	wzr, [x29,48]
	str	wzr, [x29,52]
	bl	memset
.L4514:
	add	x0, x29, 296
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,56]
	bl	memset
.L4548:
	mov	x0, x20
	add	x1, x29, 40
	mov	x2, 520
	bl	rk_copy_to_user
	cbnz	x0, .L4553
	b	.L4551
.L4500:
	adrp	x0, .LC322
	add	x0, x0, :lo12:.LC322
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4513
	adrp	x1, .LANCHOR5
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR5
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,1016]
	cmp	w3, w2
	bne	.L4497
	ldr	w2, [x29,52]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L4497
	ldr	x19, [x1,1008]
	add	x1, x29, 104
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,52]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L4552
.L4505:
	adrp	x0, .LC323
	add	x0, x0, :lo12:.LC323
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4513
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w0, [x0,1020]
	cbnz	w0, .L4517
.L4520:
	mov	x0, 0
	b	.L4497
.L4517:
	add	x0, x19, :lo12:.LANCHOR5
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	x1, [x0,1024]
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L4518
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,1024]
	str	w2, [x1,4]
	ldr	x0, [x0,1024]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L4518:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 0
	ldr	x1, [x20,1024]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,1008]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4519
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1008]
	str	w1, [x0,4]
	ldr	x0, [x20,1008]
	str	wzr, [x0,8]
.L4519:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1008]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,1020]
	str	wzr, [x19,1016]
	b	.L4551
.L4504:
	adrp	x0, .LC324
	add	x0, x0, :lo12:.LC324
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 20037
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4513
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,1020]
	cmp	w1, 1
	beq	.L4520
	ldr	x0, [x0,1024]
	mov	w1, 22867
	movk	w1, 0x4453, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4521
	mov	w1, 22867
	mov	w2, 504
	movk	w1, 0x4453, lsl 16
	str	w1, [x0]
	add	x0, x19, :lo12:.LANCHOR5
	ldr	x1, [x0,1024]
	str	w2, [x1,4]
	ldr	x0, [x0,1024]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L4521:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 1
	ldr	x1, [x20,1024]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,1008]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4522
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1008]
	str	w1, [x0,4]
	ldr	x0, [x20,1008]
	str	wzr, [x0,8]
.L4522:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1008]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,1020]
	b	.L4551
.L4556:
	adrp	x0, .LC325
	add	x0, x0, :lo12:.LC325
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w2, [x29,44]
	cmp	w2, 512
	bhi	.L4513
	adrp	x1, .LANCHOR5
	add	x0, x29, 48
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 1032
	b	.L4554
.L4498:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4524
	adrp	x0, .LC326
	add	x0, x0, :lo12:.LC326
	b	.L4550
.L4524:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4526
	adrp	x0, .LC327
	add	x0, x0, :lo12:.LC327
	b	.L4550
.L4526:
	adrp	x0, .LC328
	add	x0, x0, :lo12:.LC328
.L4550:
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L4553
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L4527
	add	x0, x0, :lo12:.LANCHOR5
	add	x1, x29, 40
	mov	x2, 16
	ldr	x0, [x0,1008]
	ldr	w0, [x0,20]
	str	w0, [x29,44]
	strb	w0, [x29,48]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L4497
	b	.L4553
.L4527:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,1544]
	cmp	w1, 10
	bhi	.L4553
	ldr	x1, [x20,1008]
	ldr	w2, [x29,44]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L4528
	cbz	w3, .L4528
	adrp	x0, .LC329
	mov	w1, w2
	add	x0, x0, :lo12:.LC329
	bl	printk
	ldr	w0, [x20,1544]
	add	w0, w0, 1
	str	w0, [x20,1544]
.L4553:
	mov	x0, -14
	b	.L4497
.L4528:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1544]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4529
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L4530
.L4529:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L4530:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L4551
	b	.L4512
.L4507:
	adrp	x0, .LC330
	add	x0, x0, :lo12:.LC330
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L4513
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 48
	bne	.L4531
	ldr	x1, [x1,1552]
	b	.L4555
.L4531:
	ldr	x1, [x1,1560]
.L4555:
	add	x1, x1, 8
.L4554:
	bl	memcpy
	b	.L4548
.L4509:
	adrp	x0, .LC331
	add	x0, x0, :lo12:.LC331
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4516
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4513
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L4513
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L4533
	ldr	x0, [x19,1552]
	add	x1, x29, 40
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1552]
	b	.L4552
.L4533:
	ldr	x0, [x19,1560]
	add	x1, x29, 40
	bl	memcpy
	ldr	x1, [x19,1560]
	mov	w0, 3
.L4552:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L4512
.L4502:
	bl	rknand_dev_flush
.L4551:
	mov	x19, 0
.L4512:
	adrp	x0, .LC332
	mov	x1, x19
	add	x0, x0, :lo12:.LC332
	bl	printk
	mov	x0, x19
	b	.L4497
.L4534:
	mov	x0, -22
.L4497:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	add	sp, sp, 528
	ret
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -48]!
	mov	w0, -1
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR5
	str	x21, [sp,32]
	add	x19, x20, :lo12:.LANCHOR5
	ldr	x1, [x19,968]
	str	w0, [x19,1000]
	add	x0, x1, 512
	str	x0, [x19,1008]
	add	x0, x1, 1024
	str	x1, [x19,1024]
	str	x0, [x19,1552]
	add	x1, x1, 1536
	add	x0, x19, 1032
	str	x1, [x19,1560]
	strb	wzr, [x19,988]
	str	xzr, [x19,992]
	str	xzr, [x19,1568]
	bl	ftl_memcpy
	ldr	x0, [x19,1024]
	str	wzr, [x19,1016]
	str	wzr, [x19,1544]
	ldr	w1, [x0,16]
	ldr	w21, [x0,508]
	str	w1, [x19,1020]
	cbz	w21, .L4559
	mov	w1, 508
	bl	js_hash
	cmp	w21, w0
	beq	.L4559
	ldr	x0, [x19,1024]
	str	wzr, [x19,1020]
	str	wzr, [x0,16]
	adrp	x0, .LC333
	add	x0, x0, :lo12:.LC333
	bl	printk
.L4559:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1020]
	cbz	w1, .L4561
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,1016]
.L4561:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	ldr	x1, [x20,1552]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1560]
	mov	w0, 3
	bl	StorageSysDataLoad
	bl	rknand_sys_storage_init
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	mov	w0, 0
	ret
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.align	2
	.global	rk_ftl_vendor_storage_init
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	stp	x29, x30, [sp, -64]!
	mov	w0, 65536
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR5
	bl	ftl_malloc
	add	x1, x21, :lo12:.LANCHOR5
	mov	w19, -12
	str	x0, [x1,1576]
	cbz	x0, .L4567
	mov	w23, 0
	adrp	x24, .LC334
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC334
.L4571:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,1576]
	bl	FlashBootVendorRead
	cbnz	w0, .L4568
	ldr	x3, [x20,1576]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,1576]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L4569
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L4569
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L4569:
	cbnz	w22, .L4579
	mov	w22, 1
	b	.L4571
.L4579:
	cbz	w19, .L4572
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L4567
	b	.L4568
.L4572:
	mov	x0, x20
	mov	w1, w19
	mov	x2, 65536
	bl	memset
	str	w22, [x20,4]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	str	w0, [x20]
	add	x0, x20, 61440
	str	w22, [x0,4092]
	mov	w0, -1032
	strh	w19, [x20,12]
	strh	w0, [x20,14]
	b	.L4567
.L4568:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,1576]
	bl	kfree
	str	xzr, [x21,1576]
.L4567:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	adrp	x3, .LANCHOR5+1576
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR5+1576]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L4581
	ldrh	w3, [x5,10]
	mov	x4, 0
.L4582:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L4586
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L4582
	add	x6, x5, x6, uxtw 3
	ldrh	w4, [x6,20]
	mov	x0, x1
	ldrh	w1, [x6,18]
	cmp	w2, w4
	csel	w19, w2, w4, ls
	add	x1, x1, 1024
	add	x1, x5, x1
	uxtw	x2, w19
	bl	memcpy
	mov	w3, w19
	b	.L4581
.L4586:
	mov	w3, -1
.L4581:
	mov	w0, w3
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	stp	x29, x30, [sp, -112]!
	mov	w6, w0
	adrp	x0, .LANCHOR5+1576
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR5+1576]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L4588
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L4589:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L4604
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L4589
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L4590
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L4588
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L4591:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L4605
	add	x3, x19, x3, lsl 3
	add	w25, w4, 1
	add	x1, x19, x25, uxtw 3
	ldrh	w0, [x1,16]
	str	x6, [x29,96]
	strh	w0, [x3,16]
	str	x5, [x29,104]
	ldrh	w0, [x1,20]
	strh	w0, [x3,20]
	strh	w21, [x3,18]
	add	x0, x5, x21, uxtw
	ldrh	w27, [x1,20]
	ldrh	w1, [x1,18]
	add	w27, w27, 63
	and	w27, w27, -64
	add	x1, x5, x1
	and	x2, x27, 131008
	add	w21, w21, w27
	bl	memcpy
	mov	w4, w25
	ldr	x6, [x29,96]
	ldr	x5, [x29,104]
	b	.L4591
.L4605:
	add	x3, x19, x3, lsl 3
	uxth	w21, w21
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x21, uxth
	uxth	w20, w20
	strh	w21, [x3,18]
	add	w21, w21, w20
	strh	w6, [x3,16]
	strh	w26, [x3,20]
	bl	memcpy
	strh	w21, [x19,12]
	ldrh	w0, [x19,14]
	add	w24, w24, w0
	sub	w24, w24, w20
	strh	w24, [x19,14]
	b	.L4603
.L4590:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L4603
.L4604:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L4588
	add	x3, x19, x3, uxth 3
	strh	w6, [x3,16]
	uxth	w20, w20
	mov	x1, x28
	ldrh	w0, [x19,12]
	uxtw	x2, w26
	strh	w0, [x3,18]
	strh	w26, [x3,20]
	add	w0, w20, w0
	sub	w20, w4, w20
	strh	w0, [x19,12]
	strh	w20, [x19,14]
	ldrh	w0, [x3,18]
	add	x0, x0, 1024
	add	x0, x19, x0
	bl	memcpy
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
.L4603:
	ldr	w0, [x19,4]
	add	x1, x19, 61440
	mov	x2, x19
	add	w0, w0, 1
	str	w0, [x19,4]
	str	w0, [x1,4092]
	mov	w1, 128
	ldrh	w0, [x19,8]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19,8]
	lsl	w0, w23, 7
	bl	FlashBootVendorWrite
	mov	w2, 0
.L4588:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	stp	x29, x30, [sp, -64]!
	mov	w0, 4096
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, -1
	str	x1, [x29,56]
	mov	x21, x2
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L4607
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4609
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4610
	b	.L4621
.L4609:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4621
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L4612
.L4613:
	mov	x20, -1
	b	.L4608
.L4612:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L4613
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L4608
.L4610:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4621
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L4613
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L4613
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L4621
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L4608
.L4621:
	mov	x20, -14
.L4608:
	mov	x0, x19
	bl	kfree
.L4607:
	mov	x0, x20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
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
	.type	__func__.39475, %object
	.size	__func__.39475, 18
__func__.39475:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.39500, %object
	.size	__func__.39500, 23
__func__.39500:
	.string	"_list_update_data_list"
	.zero	1
	.type	__func__.20801, %object
	.size	__func__.20801, 22
__func__.20801:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.20887, %object
	.size	__func__.20887, 11
__func__.20887:
	.string	"nandc_init"
	.zero	5
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
	.zero	3
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
	.zero	1
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
	.type	__func__.20274, %object
	.size	__func__.20274, 28
__func__.20274:
	.string	"flash_wait_device_ready_raw"
	.zero	4
	.type	__func__.20338, %object
	.size	__func__.20338, 22
__func__.20338:
	.string	"flash_start_page_read"
	.zero	2
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
	.zero	1
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
	.zero	5
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
	.zero	3
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
	.zero	6
	.type	__func__.20363, %object
	.size	__func__.20363, 23
__func__.20363:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.20249, %object
	.size	__func__.20249, 26
__func__.20249:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.20260, %object
	.size	__func__.20260, 21
__func__.20260:
	.string	"flash_erase_block_en"
	.zero	3
	.type	random_seed, %object
	.size	random_seed, 256
random_seed:
	.hword	22378
	.hword	1512
	.hword	25245
	.hword	17827
	.hword	25756
	.hword	19440
	.hword	9026
	.hword	10030
	.hword	29528
	.hword	20467
	.hword	29676
	.hword	24432
	.hword	31328
	.hword	6872
	.hword	13426
	.hword	13842
	.hword	8783
	.hword	1108
	.hword	782
	.hword	28837
	.hword	30729
	.hword	9505
	.hword	18676
	.hword	23085
	.hword	18730
	.hword	1085
	.hword	32609
	.hword	14697
	.hword	20858
	.hword	15170
	.hword	30365
	.hword	1607
	.hword	32298
	.hword	4995
	.hword	18905
	.hword	1976
	.hword	9592
	.hword	20204
	.hword	17443
	.hword	13615
	.hword	23330
	.hword	29369
	.hword	13947
	.hword	9398
	.hword	32398
	.hword	8984
	.hword	27600
	.hword	21785
	.hword	6019
	.hword	6311
	.hword	31598
	.hword	30210
	.hword	19327
	.hword	13896
	.hword	11347
	.hword	27545
	.hword	3107
	.hword	26575
	.hword	32270
	.hword	19852
	.hword	20601
	.hword	8349
	.hword	9290
	.hword	29819
	.hword	13579
	.hword	3661
	.hword	28676
	.hword	27331
	.hword	32574
	.hword	8693
	.hword	31253
	.hword	9081
	.hword	5399
	.hword	6842
	.hword	20087
	.hword	5537
	.hword	1274
	.hword	11617
	.hword	9530
	.hword	4866
	.hword	8035
	.hword	23219
	.hword	1178
	.hword	23272
	.hword	7383
	.hword	18944
	.hword	12488
	.hword	12871
	.hword	29340
	.hword	20532
	.hword	11022
	.hword	22514
	.hword	228
	.hword	22363
	.hword	24978
	.hword	14584
	.hword	12138
	.hword	3092
	.hword	17916
	.hword	16863
	.hword	14554
	.hword	31457
	.hword	29474
	.hword	25311
	.hword	24121
	.hword	3684
	.hword	28037
	.hword	22865
	.hword	22839
	.hword	25217
	.hword	13217
	.hword	27186
	.hword	14938
	.hword	11180
	.hword	29754
	.hword	24180
	.hword	15150
	.hword	32455
	.hword	20434
	.hword	23848
	.hword	29983
	.hword	16120
	.hword	14769
	.hword	20041
	.hword	29803
	.hword	28406
	.hword	17598
	.hword	28087
	.type	__func__.21070, %object
	.size	__func__.21070, 13
__func__.21070:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.21083, %object
	.size	__func__.21083, 10
__func__.21083:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.21097, %object
	.size	__func__.21097, 16
__func__.21097:
	.string	"buf_remove_free"
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 288
zftl_debug_proc_fops:
	.xword	0
	.xword	seq_lseek
	.xword	seq_read
	.xword	zftl_debug_proc_write
	.zero	72
	.xword	zftl_debug_proc_open
	.zero	8
	.xword	single_release
	.zero	160
	.type	__func__.39035, %object
	.size	__func__.39035, 12
__func__.39035:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.39127, %object
	.size	__func__.39127, 19
__func__.39127:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.39683, %object
	.size	__func__.39683, 18
__func__.39683:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.39693, %object
	.size	__func__.39693, 17
__func__.39693:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.39814, %object
	.size	__func__.39814, 23
__func__.39814:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.39854, %object
	.size	__func__.39854, 22
__func__.39854:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.39865, %object
	.size	__func__.39865, 22
__func__.39865:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.39184, %object
	.size	__func__.39184, 16
__func__.39184:
	.string	"gc_free_src_blk"
	.type	__func__.38779, %object
	.size	__func__.38779, 14
__func__.38779:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.38824, %object
	.size	__func__.38824, 18
__func__.38824:
	.string	"zftl_add_read_buf"
	.zero	6
	.type	__func__.39641, %object
	.size	__func__.39641, 15
__func__.39641:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR4 = . + 0
	.type	__func__.40259, %object
	.size	__func__.40259, 17
__func__.40259:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.40287, %object
	.size	__func__.40287, 21
__func__.40287:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.20791, %object
	.size	__func__.20791, 20
__func__.20791:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.38711, %object
	.size	__func__.38711, 17
__func__.38711:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.39575, %object
	.size	__func__.39575, 20
__func__.39575:
	.string	"zftl_sblk_list_init"
	.zero	4
	.type	__func__.40219, %object
	.size	__func__.40219, 13
__func__.40219:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.21133, %object
	.size	__func__.21133, 21
__func__.21133:
	.string	"flash_info_data_init"
	.zero	3
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
	.zero	6
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
	.type	__func__.20172, %object
	.size	__func__.20172, 19
__func__.20172:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.20298, %object
	.size	__func__.20298, 26
__func__.20298:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.20309, %object
	.size	__func__.20309, 29
__func__.20309:
	.string	"flash_start_3d_mlc_page_prog"
	.zero	3
	.type	__func__.20327, %object
	.size	__func__.20327, 22
__func__.20327:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.20351, %object
	.size	__func__.20351, 31
__func__.20351:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.20376, %object
	.size	__func__.20376, 25
__func__.20376:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.40052, %object
	.size	__func__.40052, 31
__func__.40052:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.40106, %object
	.size	__func__.40106, 15
__func__.40106:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.40133, %object
	.size	__func__.40133, 15
__func__.40133:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.39082, %object
	.size	__func__.39082, 21
__func__.39082:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.20233, %object
	.size	__func__.20233, 19
__func__.20233:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.39929, %object
	.size	__func__.39929, 14
__func__.39929:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.39609, %object
	.size	__func__.39609, 15
__func__.39609:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.39891, %object
	.size	__func__.39891, 19
__func__.39891:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.39903, %object
	.size	__func__.39903, 18
__func__.39903:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.40269, %object
	.size	__func__.40269, 14
__func__.40269:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.21155, %object
	.size	__func__.21155, 17
__func__.21155:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.21118, %object
	.size	__func__.21118, 20
__func__.21118:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.20846, %object
	.size	__func__.20846, 16
__func__.20846:
	.string	"nand_flash_init"
	.type	__func__.39756, %object
	.size	__func__.39756, 16
__func__.39756:
	.string	"ftl_sysblk_dump"
	.type	__func__.39781, %object
	.size	__func__.39781, 23
__func__.39781:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.40203, %object
	.size	__func__.40203, 16
__func__.40203:
	.string	"load_l2p_region"
	.type	__func__.40243, %object
	.size	__func__.40243, 6
__func__.40243:
	.string	"pm_gc"
	.zero	2
	.type	__func__.39025, %object
	.size	__func__.39025, 12
__func__.39025:
	.string	"gc_recovery"
	.zero	4
	.type	__func__.39114, %object
	.size	__func__.39114, 22
__func__.39114:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.39228, %object
	.size	__func__.39228, 16
__func__.39228:
	.string	"gc_scan_src_blk"
	.type	__func__.39289, %object
	.size	__func__.39289, 20
__func__.39289:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.39352, %object
	.size	__func__.39352, 18
__func__.39352:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.39732, %object
	.size	__func__.39732, 14
__func__.39732:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.38857, %object
	.size	__func__.38857, 10
__func__.38857:
	.string	"zftl_read"
	.zero	6
	.type	__func__.39835, %object
	.size	__func__.39835, 19
__func__.39835:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.38761, %object
	.size	__func__.38761, 17
__func__.38761:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.39157, %object
	.size	__func__.39157, 16
__func__.39157:
	.string	"gc_do_copy_back"
	.type	__func__.39380, %object
	.size	__func__.39380, 11
__func__.39380:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.38888, %object
	.size	__func__.38888, 13
__func__.38888:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.23 20201130"
.LC3:
	.string	"zftl_debug:0x%x\n"
.LC4:
	.string	"...%s enter...\n"
.LC5:
	.string	"No.0 FLASH ID: %x %x %x %x %x %x\n"
.LC6:
	.string	"DiePerChip: %x\n"
.LC7:
	.string	"SectPerPage: %x\n"
.LC8:
	.string	"PagePerBlk: %x\n"
.LC9:
	.string	"Cell: %x\n"
.LC10:
	.string	"PlanePerDie: %x\n"
.LC11:
	.string	"BlkPerPlane: %x\n"
.LC12:
	.string	"die gap: %x\n"
.LC13:
	.string	"lsbMode: %x\n"
.LC14:
	.string	"ReadRetryMode: %x\n"
.LC15:
	.string	"ecc: %x\n"
.LC16:
	.string	"idb ecc: %x\n"
.LC17:
	.string	"OptMode: %x\n"
.LC18:
	.string	"g_nand_max_die: %x\n"
.LC19:
	.string	"Cache read enable: %x\n"
.LC20:
	.string	"Cache random read enable: %x\n"
.LC21:
	.string	"Cache prog enable: %x\n"
.LC22:
	.string	"multi read enable: %x\n"
.LC23:
	.string	"multi prog enable: %x\n"
.LC24:
	.string	"interleave enable: %x\n"
.LC25:
	.string	"read retry enable: %x\n"
.LC26:
	.string	"randomizer enable: %x\n"
.LC27:
	.string	"SDR enable: %x\n"
.LC28:
	.string	"ONFI enable: %x\n"
.LC29:
	.string	"TOGGLE enable: %x\n"
.LC30:
	.string	"g_flash_slc_mode: %x %x\n"
.LC31:
	.string	"MultiPlaneProgCmd: %x %x\n"
.LC32:
	.string	"MultiPlaneReadCmd: %x %x\n"
.LC33:
	.string	"g_flash_toggle_mode_en: %x\n"
.LC34:
	.string	"...%s enter... %p\n"
.LC35:
	.string	"0:%x %x %x %x %x\n"
.LC36:
	.string	"g_nandc_ver...%d\n"
.LC37:
	.string	"nand sdr mode %x\n"
.LC38:
	.string	"nand ddr mode %x\n"
.LC39:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC40:
	.string	"otp:%x %x %x %x\n"
.LC41:
	.string	"bad block test:%x %x\n"
.LC42:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC43:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC44:
	.string	"flash_erase_block %x %x %x\n"
.LC45:
	.string	"flash_erase_block %d block = %x status = %x\n"
.LC46:
	.string	"erase done: %x\n"
.LC47:
	.string	"sblk_queue_head = %d\n"
.LC48:
	.string	"sblk_read_completed_queue_head = %d\n"
.LC49:
	.string	"sblk_gc_write_completed_queue_head = %d\n"
.LC50:
	.string	"sblk_write_completed_queue_head = %d\n"
.LC51:
	.string	"p_free_buf_head = %d\n"
.LC52:
	.string	"free_buf_count = %d\n"
.LC53:
	.string	"buf = %d, next=%d, flag=%d gc_write_flag=%d, lun_state=%d, op_status = %d lpa=%x, ppa=%x\n"
.LC54:
	.string	"flash_mask_bad_block %d %d\n"
.LC55:
	.string	"zftl_debug"
.LC56:
	.string	"FLASH ID: %x %x %x %x %x %x\n"
.LC57:
	.string	"density: %d MB\n"
.LC58:
	.string	"device density: %d MB\n"
.LC59:
	.string	"FTL INFO:\n"
.LC60:
	.string	"max_lpn = 0x%x\n"
.LC61:
	.string	"density = 0x%x\n"
.LC62:
	.string	"slc vpn = 0x%x\n"
.LC63:
	.string	"xlc vpn = 0x%x\n"
.LC64:
	.string	"free slc blk = 0x%x\n"
.LC65:
	.string	"free xlc blk = 0x%x\n"
.LC66:
	.string	"free mix blk = 0x%x\n"
.LC67:
	.string	"slc data blk = 0x%x\n"
.LC68:
	.string	"slc cache blk = 0x%x\n"
.LC69:
	.string	"xlc data blk = 0x%x\n"
.LC70:
	.string	"free buf = %d, %d, %d\n"
.LC71:
	.string	"bad blk = %d %d\n"
.LC72:
	.string	"TBW = %d MB\n"
.LC73:
	.string	"TBR = %d MB\n"
.LC74:
	.string	"POC = %d\n"
.LC75:
	.string	"PLC = %d\n"
.LC76:
	.string	"sys run time = %d S\n"
.LC77:
	.string	"slc mode = %d\n"
.LC78:
	.string	"prog err = %d\n"
.LC79:
	.string	"read err = %d\n"
.LC80:
	.string	"GC XLC page = %d\n"
.LC81:
	.string	"GC SLC page = %d\n"
.LC82:
	.string	"discard page = 0x%x\n"
.LC83:
	.string	"version = %d\n"
.LC84:
	.string	"acblk = 0x%x %d %d\n"
.LC85:
	.string	"tmblk = 0x%x %d %d\n"
.LC86:
	.string	"gcblk = 0x%x %d %d\n"
.LC87:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC88:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC89:
	.string	"gc free blk th = %d\n"
.LC90:
	.string	"gc vpn th = %d %d %d %d %d\n"
.LC91:
	.string	"swl blk = %x %x %x %x\n"
.LC92:
	.string	"rf info = %x %x %x %x %x\n"
.LC93:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\n"
.LC94:
	.string	"gc_add_sblk = %d, %d, %d\n"
.LC95:
	.string	"gc_add_sblk = %d, %d, %d,last update:%d, %d\n"
.LC96:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC97:
	.string	"gc_mark_bad_ppa %d %x %x\n"
.LC98:
	.string	"status: %x, ppa: %x\n"
.LC99:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC100:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC101:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC102:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC103:
	.string	"swl_tlc_free_mini_ec_blk alloc sblk %x\n"
.LC104:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC105:
	.string	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\n"
.LC106:
	.string	"swl_slc_free_mini_ec_blk alloc sblk %x\n"
.LC107:
	.string	"list count:%p %d\n"
.LC108:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC109:
	.string	"ftl_vpn_decrement %x = %d, %d\n"
.LC110:
	.string	"mask bad block:cs %x %x block: %x %x\n"
.LC111:
	.string	"gc_free_bad_sblk 0x%x\n"
.LC112:
	.string	"swl_slc_free_mini_ec_blk sblk %x\n"
.LC113:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC114:
	.string	"gc_free_src_blk %x, %d\n"
.LC115:
	.string	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC116:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC117:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC118:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC119:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC120:
	.string	"swl : %x %x %x %x %x %x\n"
.LC121:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC122:
	.string	"ftl re prog: lpa = %x, ppa= %x\n"
.LC123:
	.string	"alloc sblk %x %d\n"
.LC124:
	.string	"dump_sblk_queue: %d\n"
.LC125:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC126:
	.string	"blk %x is bad block\n"
.LC127:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC128:
	.string	"%s %d %d\n"
.LC129:
	.string	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC130:
	.string	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC131:
	.string	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC132:
	.string	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC133:
	.string	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC134:
	.string	"gc_static_wearleveling: slc blk: %x, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC135:
	.string	"gc_static_wearleveling: min slc ec: %x, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC136:
	.string	"swl add tlc gc = %x, %d, %d, %d, %d, %d\n"
.LC137:
	.string	"swl add slc gc  = %x, %d, %d, %d, %d, %d\n"
.LC138:
	.string	"free blk vpn error: %x %x\n"
.LC139:
	.string	"GC PM block %x %x %x %d\n"
.LC140:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC141:
	.string	"...%d @ %s\n"
.LC142:
	.string	"rk_ftl_de_init %x\n"
.LC143:
	.string	"\0013"
.LC144:
	.string	"otp error! %d"
.LC145:
	.string	"rr"
.LC146:
	.string	"flash_abort_clear = %d\n"
.LC147:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC148:
	.string	"%d flReg.d32=%x %x\n"
.LC149:
	.string	"nandc:"
.LC150:
	.string	"nandc_xfer_done read error %x\n"
.LC151:
	.string	"dqs data abort %x\n"
.LC152:
	.string	"dqs data timeout %x\n"
.LC153:
	.string	"xfer error %x\n"
.LC154:
	.string	"MT %d row=%x,last status %d,status = %d\n"
.LC155:
	.string	"MT RR %d row=%x,count %d,status=%d\n"
.LC156:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC157:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC158:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC159:
	.string	"YMTC RR %d row=%x,count %d,status=%d\n"
.LC160:
	.string	"samsung SRR %d row=%x, status=%d\n"
.LC161:
	.string	"samsung TRR %d row=%x, status=%d\n"
.LC162:
	.string	"samsung RR %d row=%x,count %d,status=%d\n"
.LC163:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC164:
	.string	"%d flash_ddr_tuning_read %x ecc=%d\n"
.LC165:
	.string	"sync para %d\n"
.LC166:
	.string	"DDR mode Read error %x %x\n"
.LC167:
	.string	"SDR mode Read %x %x ecc:%x\n"
.LC168:
	.string	"flash_read_page_en %x %x %x %x\n"
.LC169:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC170:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC171:
	.string	"flash_prog_page %x %x %x\n"
.LC172:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC173:
	.string	"ymtc_flash_tlc_page_prog page_addr = %x status = %x\n"
.LC174:
	.string	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\n"
.LC175:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC176:
	.string	"read: %x %x %x %x\n"
.LC177:
	.string	"0set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC178:
	.string	"prog end %x %x error_ecc %d %d\n"
.LC179:
	.string	"1set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC180:
	.string	"dp prog end %x %x error_ecc %d %d\n"
.LC181:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC182:
	.string	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC183:
	.string	"flash_prog_page_en:%x %x %x\n"
.LC184:
	.string	"w d:"
.LC185:
	.string	"w s:"
.LC186:
	.string	"spare"
.LC187:
	.string	"data"
.LC188:
	.string	"write error: %x\n"
.LC189:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC190:
	.string	"%d %x @%d %x\n"
.LC191:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC192:
	.string	"ftl info hash %x error\n"
.LC193:
	.string	"ink flag: %x\n"
.LC194:
	.string	"%s %d %d %x %x\n"
.LC195:
	.string	"ext info hash %x error\n"
.LC196:
	.string	"%s %x %x %x\n"
.LC197:
	.string	"pm_write_page write error: %x\n"
.LC198:
	.string	"ftl_sblk_dump_write = %x %d %d %d %d\n"
.LC199:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC200:
	.string	"ftl_sblk_dump_write2 = %x %d %d %d\n"
.LC201:
	.string	"ftl_sblk_dump_write = %x %x\n"
.LC202:
	.string	"ftl_sblk_dump_write done = %x\n"
.LC203:
	.string	"finfo:"
.LC204:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC205:
	.string	"sys_info_flush error:%x\n"
.LC206:
	.string	"...%d @ %s %d %p\n"
.LC207:
	.string	"no sys info %x\n"
.LC208:
	.string	"l2p:"
.LC209:
	.string	"saved_active_page  = %x\n"
.LC210:
	.string	"saved_active_plane = %x\n"
.LC211:
	.string	"sblk = %x\n"
.LC212:
	.string	"phy_blk = %x %x\n"
.LC213:
	.string	"num_planes = %x\n"
.LC214:
	.string	"recovery blk=%x, page=%x, ppa = %x, status = %x, hash:%x\n"
.LC215:
	.string	"data:"
.LC216:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC217:
	.string	"dump_write_lpa = %x %x %x %x\n"
.LC218:
	.string	"dump write new ppa = %x, last ppa = %x lpa = %x\n"
.LC219:
	.string	"dump write = %x %x %x\n"
.LC220:
	.string	"dump write hash update = %x %x %x\n"
.LC221:
	.string	"free_buf_count: %d\n"
.LC222:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC223:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC224:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC225:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC226:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC227:
	.string	"lpa:"
.LC228:
	.string	"vpn:"
.LC229:
	.string	"sblk:"
.LC230:
	.string	"lpa_hash:"
.LC231:
	.string	"lpa_hash_index:"
.LC232:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC233:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC234:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC235:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC236:
	.string	"pm_ppa:"
.LC237:
	.string	"spare:"
.LC238:
	.string	"pm_init posr %x %x %x\n"
.LC239:
	.string	"pm_init recovery %x %x %x\n"
.LC240:
	.string	"pm_init hash %x error\n"
.LC241:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC242:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC243:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC244:
	.string	"gc_recovery: %x vpn = %x\n"
.LC245:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC246:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC247:
	.string	"lpa: %x %x %x\n"
.LC248:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC249:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC250:
	.string	"js hash error:%x %x %x\n"
.LC251:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC252:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC253:
	.string	"ftl_sblk_dump = %x %d %d %d %d\n"
.LC254:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC255:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC256:
	.string	"index= %x, lpa=%x\n"
.LC257:
	.string	"block = %x, vpn=%x check vpn = %x\n"
.LC258:
	.string	"ftl_read %x %x %x\n"
.LC259:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC260:
	.string	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC261:
	.string	"zftl debug cmd: %s\n"
.LC262:
	.string	"cmd:"
.LC263:
	.string	"dumpl2p"
.LC264:
	.string	"pm l2p:"
.LC265:
	.string	"pm blk:"
.LC266:
	.string	"dumppm:"
.LC267:
	.string	"p_cmd: %s\n"
.LC268:
	.string	"pm ram = %x, %x\n"
.LC269:
	.string	"ram:"
.LC270:
	.string	"pm:"
.LC271:
	.string	"dumpsys"
.LC272:
	.string	"dumplist:"
.LC273:
	.string	"vpncheck"
.LC274:
	.string	"dumpppa:"
.LC275:
	.string	"dumpblk:"
.LC276:
	.string	"setzdebug:"
.LC277:
	.string	"lpa2ppa:"
.LC278:
	.string	"lpa: %x--> ppa: %x\n"
.LC279:
	.string	"help:\n"
.LC280:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC281:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC282:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC283:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC284:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC285:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC286:
	.string	"7. echo dumplist > /proc/zftl_debug\n"
.LC287:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC288:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC289:
	.string	"ftl_update_l2p_map"
.LC290:
	.string	"lpa_tbl:"
.LC291:
	.string	"sblk %x vpn: %d %d\n"
.LC292:
	.string	"error gc_add_sblk: %x\n"
.LC293:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC294:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC295:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC296:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC297:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC298:
	.string	"gc free %x, %d\n"
.LC299:
	.string	"_c_user_data_density := %d\n"
.LC300:
	.string	"_c_totle_phy_density := %d\n"
.LC301:
	.string	"_c_totle_log_page := %d\n"
.LC302:
	.string	"_c_totle_data_density := %d\n"
.LC303:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC304:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC305:
	.string	"_c_max_pm_sblk := %d\n"
.LC306:
	.string	"_min_slc_super_block := %d\n"
.LC307:
	.string	"_max_xlc_super_block := %d\n"
.LC308:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC309:
	.string	"flash info size: %d %d %d\n"
.LC310:
	.string	"ftl_init %x\n"
.LC311:
	.string	"ftlwrite %x %x %x %x\n"
.LC312:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC313:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC314:
	.string	"write_idblock fix data %x %x\n"
.LC315:
	.string	"idblk:"
.LC316:
	.string	"write_idblock totle_sec %x %x\n"
.LC317:
	.string	"prog page: %x %x %x, %p %x %x %x\n"
.LC318:
	.string	"read page: %x %x %x %x\n"
.LC319:
	.string	"wl_lba %p %x %x %x\n"
.LC320:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC321:
	.string	"rk_copy_from_user error\n"
.LC322:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC323:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC324:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC325:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC326:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC327:
	.string	"RKNAND_LOADER_STATUS\n"
.LC328:
	.string	"RKNAND_LOADER_LOCK\n"
.LC329:
	.string	"LockKey not match %d\n"
.LC330:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC331:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC332:
	.string	"return ret = %lx\n"
.LC333:
	.string	"secureBootEn check error\n"
.LC334:
	.string	"\0013vendor storage %x,%x,%x\n"
	.data
	.align	3
.LANCHOR2 = . + 0
	.type	zftl_debug, %object
	.size	zftl_debug, 4
zftl_debug:
	.word	17476
	.zero	4
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
	.hword	768
	.byte	3
	.byte	2
	.hword	758
	.hword	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	2
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.zero	7
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
	.zero	7
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
	.zero	4
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
	.hword	768
	.byte	3
	.byte	2
	.hword	758
	.hword	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	768
	.byte	3
	.byte	2
	.hword	1478
	.hword	5593
	.byte	0
	.byte	37
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	384
	.byte	3
	.byte	2
	.hword	1446
	.hword	1497
	.byte	0
	.byte	36
	.byte	60
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1074
	.hword	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	1
	.hword	2092
	.hword	17857
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	2106
	.hword	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	1
	.hword	1056
	.hword	17857
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	1
	.hword	1060
	.hword	17857
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1066
	.hword	17881
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	1
	.hword	1060
	.hword	17857
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1058
	.hword	17881
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1074
	.hword	17881
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	2106
	.hword	17881
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1074
	.hword	17881
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1058
	.hword	17881
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1066
	.hword	17881
	.byte	2
	.byte	67
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	1
	.hword	1060
	.hword	17857
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.hword	256
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
	.hword	512
	.byte	2
	.byte	1
	.hword	1048
	.hword	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	2
	.hword	700
	.hword	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1064
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	256
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
	.hword	512
	.byte	2
	.byte	2
	.hword	1024
	.hword	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	1024
	.hword	455
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	256
	.byte	2
	.byte	4
	.hword	1024
	.hword	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	256
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
	.hword	512
	.byte	2
	.byte	2
	.hword	1024
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	2184
	.hword	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	1044
	.hword	471
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	2184
	.hword	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	2184
	.hword	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	2184
	.hword	9671
	.byte	5
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	512
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
	.hword	256
	.byte	2
	.byte	1
	.hword	2144
	.hword	-23097
	.byte	8
	.byte	21
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	8
	.byte	0
	.hword	256
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
	.hword	512
	.byte	2
	.byte	2
	.hword	1024
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	768
	.byte	3
	.byte	1
	.hword	1440
	.hword	3527
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	1024
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
	.hword	2304
	.byte	3
	.byte	1
	.hword	1008
	.hword	3521
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	4096
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
	.hword	1024
	.byte	2
	.byte	1
	.hword	2192
	.hword	9671
	.byte	10
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	1024
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
	.hword	1152
	.byte	3
	.byte	2
	.hword	1006
	.hword	-27169
	.byte	13
	.byte	81
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.hword	2048
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1056
	.hword	455
	.byte	2
	.byte	6
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1066
	.hword	473
	.byte	1
	.byte	7
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1060
	.hword	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1046
	.hword	473
	.byte	1
	.byte	3
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	2092
	.hword	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	2
	.hword	1056
	.hword	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.hword	256
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
	.hword	388
	.byte	2
	.byte	2
	.hword	1362
	.hword	473
	.byte	9
	.byte	8
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	1
	.hword	1024
	.hword	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	512
	.byte	2
	.byte	2
	.hword	1024
	.hword	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	512
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
	.hword	256
	.byte	2
	.byte	4
	.hword	1024
	.hword	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	4
	.hword	1024
	.hword	449
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	256
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
	.hword	256
	.byte	2
	.byte	4
	.hword	1024
	.hword	449
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	256
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
	.hword	2304
	.byte	3
	.byte	1
	.hword	1008
	.hword	3521
	.byte	0
	.byte	19
	.byte	70
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.hword	4096
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
	.hword	792
	.byte	2
	.byte	1
	.hword	688
	.hword	1217
	.byte	11
	.byte	50
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.hword	1024
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
	.zero	14
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
	.zero	14
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
	.zero	14
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
	.zero	14
	.type	tlc_b05a_prog_tbl, %object
	.size	tlc_b05a_prog_tbl, 1536
tlc_b05a_prog_tbl:
	.hword	0
	.hword	0
	.hword	0
	.hword	0
	.hword	0
	.hword	0
	.hword	0
	.hword	0
	.hword	9
	.hword	0
	.hword	11
	.hword	0
	.hword	13
	.hword	0
	.hword	15
	.hword	0
	.hword	17
	.hword	0
	.hword	19
	.hword	0
	.hword	21
	.hword	0
	.hword	23
	.hword	0
	.hword	25
	.hword	0
	.hword	27
	.hword	0
	.hword	29
	.hword	0
	.hword	31
	.hword	0
	.hword	33
	.hword	0
	.hword	35
	.hword	0
	.hword	37
	.hword	0
	.hword	39
	.hword	0
	.hword	41
	.hword	0
	.hword	43
	.hword	0
	.hword	45
	.hword	0
	.hword	47
	.hword	0
	.hword	49
	.hword	0
	.hword	51
	.hword	0
	.hword	53
	.hword	0
	.hword	55
	.hword	0
	.hword	25
	.hword	58
	.hword	0
	.hword	27
	.hword	61
	.hword	0
	.hword	29
	.hword	64
	.hword	0
	.hword	31
	.hword	67
	.hword	0
	.hword	33
	.hword	70
	.hword	0
	.hword	35
	.hword	73
	.hword	0
	.hword	37
	.hword	76
	.hword	0
	.hword	39
	.hword	79
	.hword	0
	.hword	41
	.hword	82
	.hword	0
	.hword	43
	.hword	85
	.hword	0
	.hword	45
	.hword	88
	.hword	0
	.hword	47
	.hword	91
	.hword	0
	.hword	49
	.hword	94
	.hword	0
	.hword	51
	.hword	97
	.hword	0
	.hword	53
	.hword	100
	.hword	0
	.hword	55
	.hword	103
	.hword	0
	.hword	58
	.hword	106
	.hword	0
	.hword	61
	.hword	109
	.hword	0
	.hword	64
	.hword	112
	.hword	0
	.hword	67
	.hword	115
	.hword	0
	.hword	70
	.hword	118
	.hword	0
	.hword	73
	.hword	121
	.hword	0
	.hword	76
	.hword	124
	.hword	0
	.hword	79
	.hword	127
	.hword	0
	.hword	82
	.hword	130
	.hword	0
	.hword	85
	.hword	133
	.hword	0
	.hword	88
	.hword	136
	.hword	0
	.hword	91
	.hword	139
	.hword	0
	.hword	94
	.hword	142
	.hword	0
	.hword	97
	.hword	145
	.hword	0
	.hword	100
	.hword	148
	.hword	0
	.hword	103
	.hword	151
	.hword	0
	.hword	106
	.hword	154
	.hword	0
	.hword	109
	.hword	157
	.hword	0
	.hword	112
	.hword	160
	.hword	0
	.hword	115
	.hword	163
	.hword	0
	.hword	118
	.hword	166
	.hword	0
	.hword	121
	.hword	169
	.hword	0
	.hword	124
	.hword	172
	.hword	0
	.hword	127
	.hword	175
	.hword	0
	.hword	130
	.hword	178
	.hword	0
	.hword	133
	.hword	181
	.hword	0
	.hword	136
	.hword	184
	.hword	0
	.hword	139
	.hword	187
	.hword	0
	.hword	142
	.hword	190
	.hword	0
	.hword	145
	.hword	193
	.hword	0
	.hword	148
	.hword	196
	.hword	0
	.hword	151
	.hword	199
	.hword	0
	.hword	154
	.hword	202
	.hword	0
	.hword	157
	.hword	205
	.hword	0
	.hword	160
	.hword	208
	.hword	0
	.hword	163
	.hword	211
	.hword	0
	.hword	166
	.hword	214
	.hword	0
	.hword	169
	.hword	217
	.hword	0
	.hword	172
	.hword	220
	.hword	0
	.hword	175
	.hword	223
	.hword	0
	.hword	178
	.hword	226
	.hword	0
	.hword	181
	.hword	229
	.hword	0
	.hword	184
	.hword	232
	.hword	0
	.hword	187
	.hword	235
	.hword	0
	.hword	190
	.hword	238
	.hword	0
	.hword	193
	.hword	241
	.hword	0
	.hword	196
	.hword	244
	.hword	0
	.hword	199
	.hword	247
	.hword	0
	.hword	202
	.hword	250
	.hword	0
	.hword	205
	.hword	253
	.hword	0
	.hword	208
	.hword	256
	.hword	0
	.hword	211
	.hword	259
	.hword	0
	.hword	214
	.hword	262
	.hword	0
	.hword	217
	.hword	265
	.hword	0
	.hword	220
	.hword	268
	.hword	0
	.hword	223
	.hword	271
	.hword	0
	.hword	226
	.hword	274
	.hword	0
	.hword	229
	.hword	277
	.hword	0
	.hword	232
	.hword	280
	.hword	0
	.hword	235
	.hword	283
	.hword	0
	.hword	238
	.hword	286
	.hword	0
	.hword	241
	.hword	289
	.hword	0
	.hword	244
	.hword	292
	.hword	0
	.hword	247
	.hword	295
	.hword	0
	.hword	250
	.hword	298
	.hword	0
	.hword	253
	.hword	301
	.hword	0
	.hword	256
	.hword	304
	.hword	0
	.hword	259
	.hword	307
	.hword	0
	.hword	262
	.hword	310
	.hword	0
	.hword	265
	.hword	313
	.hword	0
	.hword	268
	.hword	316
	.hword	0
	.hword	271
	.hword	319
	.hword	0
	.hword	274
	.hword	322
	.hword	0
	.hword	277
	.hword	325
	.hword	0
	.hword	280
	.hword	328
	.hword	0
	.hword	283
	.hword	331
	.hword	0
	.hword	286
	.hword	334
	.hword	0
	.hword	289
	.hword	337
	.hword	0
	.hword	292
	.hword	340
	.hword	0
	.hword	295
	.hword	343
	.hword	0
	.hword	298
	.hword	346
	.hword	0
	.hword	301
	.hword	349
	.hword	0
	.hword	304
	.hword	352
	.hword	0
	.hword	307
	.hword	355
	.hword	0
	.hword	310
	.hword	358
	.hword	0
	.hword	313
	.hword	361
	.hword	0
	.hword	316
	.hword	364
	.hword	0
	.hword	319
	.hword	367
	.hword	0
	.hword	322
	.hword	370
	.hword	0
	.hword	325
	.hword	373
	.hword	0
	.hword	328
	.hword	376
	.hword	0
	.hword	331
	.hword	379
	.hword	0
	.hword	334
	.hword	382
	.hword	0
	.hword	337
	.hword	385
	.hword	0
	.hword	340
	.hword	388
	.hword	0
	.hword	343
	.hword	391
	.hword	0
	.hword	346
	.hword	394
	.hword	0
	.hword	349
	.hword	397
	.hword	0
	.hword	352
	.hword	400
	.hword	0
	.hword	355
	.hword	403
	.hword	0
	.hword	358
	.hword	406
	.hword	0
	.hword	361
	.hword	409
	.hword	0
	.hword	364
	.hword	412
	.hword	0
	.hword	367
	.hword	415
	.hword	0
	.hword	370
	.hword	418
	.hword	0
	.hword	373
	.hword	421
	.hword	0
	.hword	376
	.hword	424
	.hword	0
	.hword	379
	.hword	427
	.hword	0
	.hword	382
	.hword	430
	.hword	0
	.hword	385
	.hword	433
	.hword	0
	.hword	388
	.hword	436
	.hword	0
	.hword	391
	.hword	439
	.hword	0
	.hword	394
	.hword	442
	.hword	0
	.hword	397
	.hword	445
	.hword	0
	.hword	400
	.hword	448
	.hword	0
	.hword	403
	.hword	451
	.hword	0
	.hword	406
	.hword	454
	.hword	0
	.hword	409
	.hword	457
	.hword	0
	.hword	412
	.hword	460
	.hword	0
	.hword	415
	.hword	463
	.hword	0
	.hword	418
	.hword	466
	.hword	0
	.hword	421
	.hword	469
	.hword	0
	.hword	424
	.hword	472
	.hword	0
	.hword	427
	.hword	475
	.hword	0
	.hword	430
	.hword	478
	.hword	0
	.hword	433
	.hword	481
	.hword	0
	.hword	436
	.hword	484
	.hword	0
	.hword	439
	.hword	487
	.hword	0
	.hword	442
	.hword	490
	.hword	0
	.hword	445
	.hword	493
	.hword	0
	.hword	448
	.hword	496
	.hword	0
	.hword	451
	.hword	499
	.hword	0
	.hword	454
	.hword	502
	.hword	0
	.hword	457
	.hword	505
	.hword	0
	.hword	460
	.hword	508
	.hword	0
	.hword	463
	.hword	511
	.hword	0
	.hword	466
	.hword	514
	.hword	0
	.hword	469
	.hword	517
	.hword	0
	.hword	472
	.hword	520
	.hword	0
	.hword	475
	.hword	523
	.hword	0
	.hword	478
	.hword	526
	.hword	0
	.hword	481
	.hword	529
	.hword	0
	.hword	484
	.hword	532
	.hword	0
	.hword	487
	.hword	535
	.hword	0
	.hword	490
	.hword	538
	.hword	0
	.hword	493
	.hword	541
	.hword	0
	.hword	496
	.hword	544
	.hword	0
	.hword	499
	.hword	547
	.hword	0
	.hword	502
	.hword	550
	.hword	0
	.hword	505
	.hword	553
	.hword	0
	.hword	508
	.hword	556
	.hword	0
	.hword	511
	.hword	559
	.hword	0
	.hword	514
	.hword	562
	.hword	0
	.hword	517
	.hword	565
	.hword	0
	.hword	520
	.hword	568
	.hword	0
	.hword	523
	.hword	571
	.hword	0
	.hword	526
	.hword	574
	.hword	0
	.hword	529
	.hword	577
	.hword	0
	.hword	532
	.hword	580
	.hword	0
	.hword	535
	.hword	583
	.hword	0
	.hword	538
	.hword	586
	.hword	0
	.hword	541
	.hword	589
	.hword	0
	.hword	544
	.hword	592
	.hword	0
	.hword	547
	.hword	595
	.hword	0
	.hword	550
	.hword	598
	.hword	0
	.hword	553
	.hword	601
	.hword	0
	.hword	556
	.hword	604
	.hword	0
	.hword	559
	.hword	607
	.hword	0
	.hword	562
	.hword	610
	.hword	0
	.hword	565
	.hword	613
	.hword	0
	.hword	568
	.hword	616
	.hword	0
	.hword	571
	.hword	619
	.hword	0
	.hword	574
	.hword	622
	.hword	0
	.hword	577
	.hword	625
	.hword	0
	.hword	580
	.hword	628
	.hword	0
	.hword	583
	.hword	631
	.hword	0
	.hword	586
	.hword	634
	.hword	0
	.hword	589
	.hword	637
	.hword	0
	.hword	592
	.hword	640
	.hword	0
	.hword	595
	.hword	643
	.hword	0
	.hword	598
	.hword	646
	.hword	0
	.hword	601
	.hword	649
	.hword	0
	.hword	604
	.hword	652
	.hword	0
	.hword	607
	.hword	655
	.hword	0
	.hword	610
	.hword	658
	.hword	0
	.hword	613
	.hword	661
	.hword	0
	.hword	616
	.hword	664
	.hword	0
	.hword	619
	.hword	667
	.hword	0
	.hword	622
	.hword	670
	.hword	0
	.hword	625
	.hword	673
	.hword	0
	.hword	628
	.hword	676
	.hword	0
	.hword	631
	.hword	679
	.hword	0
	.hword	634
	.hword	682
	.hword	0
	.hword	637
	.hword	685
	.hword	0
	.hword	640
	.hword	688
	.hword	0
	.hword	643
	.hword	691
	.hword	0
	.hword	646
	.hword	694
	.hword	0
	.hword	649
	.hword	697
	.hword	0
	.hword	652
	.hword	700
	.hword	0
	.hword	655
	.hword	703
	.hword	0
	.hword	658
	.hword	706
	.hword	0
	.hword	661
	.hword	709
	.hword	0
	.hword	664
	.hword	712
	.hword	0
	.hword	667
	.hword	715
	.hword	0
	.hword	670
	.hword	718
	.hword	0
	.hword	673
	.hword	721
	.hword	0
	.hword	676
	.hword	724
	.hword	0
	.hword	679
	.hword	727
	.hword	0
	.hword	682
	.hword	730
	.hword	0
	.hword	685
	.hword	733
	.hword	0
	.hword	688
	.hword	736
	.hword	0
	.hword	691
	.hword	739
	.hword	0
	.hword	694
	.hword	742
	.hword	0
	.hword	697
	.hword	745
	.hword	0
	.hword	700
	.hword	748
	.hword	0
	.hword	703
	.hword	751
	.hword	0
	.hword	706
	.hword	0
	.hword	709
	.hword	0
	.hword	712
	.hword	0
	.hword	715
	.hword	0
	.hword	718
	.hword	0
	.hword	721
	.hword	0
	.hword	724
	.hword	0
	.hword	727
	.hword	0
	.type	tlc_prog_order, %object
	.size	tlc_prog_order, 768
tlc_prog_order:
	.hword	1
	.hword	9
	.hword	2
	.hword	17
	.hword	10
	.hword	3
	.hword	25
	.hword	18
	.hword	11
	.hword	33
	.hword	26
	.hword	19
	.hword	41
	.hword	34
	.hword	27
	.hword	49
	.hword	42
	.hword	35
	.hword	57
	.hword	50
	.hword	43
	.hword	65
	.hword	58
	.hword	51
	.hword	73
	.hword	66
	.hword	59
	.hword	81
	.hword	74
	.hword	67
	.hword	89
	.hword	82
	.hword	75
	.hword	97
	.hword	90
	.hword	83
	.hword	105
	.hword	98
	.hword	91
	.hword	113
	.hword	106
	.hword	99
	.hword	121
	.hword	114
	.hword	107
	.hword	129
	.hword	122
	.hword	115
	.hword	137
	.hword	130
	.hword	123
	.hword	145
	.hword	138
	.hword	131
	.hword	153
	.hword	146
	.hword	139
	.hword	161
	.hword	154
	.hword	147
	.hword	169
	.hword	162
	.hword	155
	.hword	177
	.hword	170
	.hword	163
	.hword	185
	.hword	178
	.hword	171
	.hword	193
	.hword	186
	.hword	179
	.hword	201
	.hword	194
	.hword	187
	.hword	209
	.hword	202
	.hword	195
	.hword	217
	.hword	210
	.hword	203
	.hword	225
	.hword	218
	.hword	211
	.hword	233
	.hword	226
	.hword	219
	.hword	241
	.hword	234
	.hword	227
	.hword	249
	.hword	242
	.hword	235
	.hword	257
	.hword	250
	.hword	243
	.hword	265
	.hword	258
	.hword	251
	.hword	273
	.hword	266
	.hword	259
	.hword	281
	.hword	274
	.hword	267
	.hword	289
	.hword	282
	.hword	275
	.hword	297
	.hword	290
	.hword	283
	.hword	305
	.hword	298
	.hword	291
	.hword	313
	.hword	306
	.hword	299
	.hword	321
	.hword	314
	.hword	307
	.hword	329
	.hword	322
	.hword	315
	.hword	337
	.hword	330
	.hword	323
	.hword	345
	.hword	338
	.hword	331
	.hword	353
	.hword	346
	.hword	339
	.hword	361
	.hword	354
	.hword	347
	.hword	369
	.hword	362
	.hword	355
	.hword	377
	.hword	370
	.hword	363
	.hword	385
	.hword	378
	.hword	371
	.hword	393
	.hword	386
	.hword	379
	.hword	401
	.hword	394
	.hword	387
	.hword	409
	.hword	402
	.hword	395
	.hword	417
	.hword	410
	.hword	403
	.hword	425
	.hword	418
	.hword	411
	.hword	433
	.hword	426
	.hword	419
	.hword	441
	.hword	434
	.hword	427
	.hword	449
	.hword	442
	.hword	435
	.hword	457
	.hword	450
	.hword	443
	.hword	465
	.hword	458
	.hword	451
	.hword	473
	.hword	466
	.hword	459
	.hword	481
	.hword	474
	.hword	467
	.hword	489
	.hword	482
	.hword	475
	.hword	497
	.hword	490
	.hword	483
	.hword	505
	.hword	498
	.hword	491
	.hword	513
	.hword	506
	.hword	499
	.hword	521
	.hword	514
	.hword	507
	.hword	529
	.hword	522
	.hword	515
	.hword	537
	.hword	530
	.hword	523
	.hword	545
	.hword	538
	.hword	531
	.hword	553
	.hword	546
	.hword	539
	.hword	561
	.hword	554
	.hword	547
	.hword	569
	.hword	562
	.hword	555
	.hword	577
	.hword	570
	.hword	563
	.hword	585
	.hword	578
	.hword	571
	.hword	593
	.hword	586
	.hword	579
	.hword	601
	.hword	594
	.hword	587
	.hword	609
	.hword	602
	.hword	595
	.hword	617
	.hword	610
	.hword	603
	.hword	625
	.hword	618
	.hword	611
	.hword	633
	.hword	626
	.hword	619
	.hword	641
	.hword	634
	.hword	627
	.hword	649
	.hword	642
	.hword	635
	.hword	657
	.hword	650
	.hword	643
	.hword	665
	.hword	658
	.hword	651
	.hword	673
	.hword	666
	.hword	659
	.hword	681
	.hword	674
	.hword	667
	.hword	689
	.hword	682
	.hword	675
	.hword	697
	.hword	690
	.hword	683
	.hword	705
	.hword	698
	.hword	691
	.hword	713
	.hword	706
	.hword	699
	.hword	721
	.hword	714
	.hword	707
	.hword	729
	.hword	722
	.hword	715
	.hword	737
	.hword	730
	.hword	723
	.hword	745
	.hword	738
	.hword	731
	.hword	753
	.hword	746
	.hword	739
	.hword	761
	.hword	754
	.hword	747
	.hword	769
	.hword	762
	.hword	755
	.hword	777
	.hword	770
	.hword	763
	.hword	785
	.hword	778
	.hword	771
	.hword	793
	.hword	786
	.hword	779
	.hword	801
	.hword	794
	.hword	787
	.hword	809
	.hword	802
	.hword	795
	.hword	817
	.hword	810
	.hword	803
	.hword	825
	.hword	818
	.hword	811
	.hword	833
	.hword	826
	.hword	819
	.hword	841
	.hword	834
	.hword	827
	.hword	849
	.hword	842
	.hword	835
	.hword	857
	.hword	850
	.hword	843
	.hword	865
	.hword	858
	.hword	851
	.hword	873
	.hword	866
	.hword	859
	.hword	881
	.hword	874
	.hword	867
	.hword	889
	.hword	882
	.hword	875
	.hword	897
	.hword	890
	.hword	883
	.hword	905
	.hword	898
	.hword	891
	.hword	913
	.hword	906
	.hword	899
	.hword	921
	.hword	914
	.hword	907
	.hword	929
	.hword	922
	.hword	915
	.hword	937
	.hword	930
	.hword	923
	.hword	945
	.hword	938
	.hword	931
	.hword	953
	.hword	946
	.hword	939
	.hword	961
	.hword	954
	.hword	947
	.hword	969
	.hword	962
	.hword	955
	.hword	977
	.hword	970
	.hword	963
	.hword	985
	.hword	978
	.hword	971
	.hword	993
	.hword	986
	.hword	979
	.hword	1001
	.hword	994
	.hword	987
	.hword	1009
	.hword	1002
	.hword	995
	.hword	1017
	.hword	1010
	.hword	1003
	.hword	1018
	.hword	1011
	.hword	1019
	.bss
	.align	6
.LANCHOR0 = . + 0
.LANCHOR3 = . + 4352
.LANCHOR5 = . + 8704
	.type	g_flash_slc_mode, %object
	.size	g_flash_slc_mode, 1
g_flash_slc_mode:
	.zero	1
	.type	g_slc_mode_addr2, %object
	.size	g_slc_mode_addr2, 1
g_slc_mode_addr2:
	.zero	1
	.type	g_block_align_addr, %object
	.size	g_block_align_addr, 2
g_block_align_addr:
	.zero	2
	.type	g_lsb_page_tbl, %object
	.size	g_lsb_page_tbl, 1024
g_lsb_page_tbl:
	.zero	1024
	.type	g_nandc_ver, %object
	.size	g_nandc_ver, 1
g_nandc_ver:
	.zero	1
	.zero	3
	.type	_c_user_data_density, %object
	.size	_c_user_data_density, 4
_c_user_data_density:
	.zero	4
	.zero	4
	.type	gp_sblk_list_tbl, %object
	.size	gp_sblk_list_tbl, 8
gp_sblk_list_tbl:
	.zero	8
	.type	gp_nandc, %object
	.size	gp_nandc, 8
gp_nandc:
	.zero	8
	.type	NANDC_FMCTL, %object
	.size	NANDC_FMCTL, 4
NANDC_FMCTL:
	.zero	4
	.type	NANDC_FMWAIT, %object
	.size	NANDC_FMWAIT, 4
NANDC_FMWAIT:
	.zero	4
	.type	NANDC_FLCTL, %object
	.size	NANDC_FLCTL, 4
NANDC_FLCTL:
	.zero	4
	.type	NANDC_BCHCTL, %object
	.size	NANDC_BCHCTL, 4
NANDC_BCHCTL:
	.zero	4
	.type	NANDC_DLL_CTL_REG0, %object
	.size	NANDC_DLL_CTL_REG0, 4
NANDC_DLL_CTL_REG0:
	.zero	4
	.type	NANDC_DLL_CTL_REG1, %object
	.size	NANDC_DLL_CTL_REG1, 4
NANDC_DLL_CTL_REG1:
	.zero	4
	.type	NANDC_RANDMZ_CFG, %object
	.size	NANDC_RANDMZ_CFG, 4
NANDC_RANDMZ_CFG:
	.zero	4
	.type	NANDC_FMWAIT_SYN, %object
	.size	NANDC_FMWAIT_SYN, 4
NANDC_FMWAIT_SYN:
	.zero	4
	.type	_c_ftl_blk_pre_plane, %object
	.size	_c_ftl_blk_pre_plane, 2
_c_ftl_blk_pre_plane:
	.zero	2
	.zero	6
	.type	gp_blk_info, %object
	.size	gp_blk_info, 8
gp_blk_info:
	.zero	8
	.type	_c_slc_to_xlc_ec_ratio, %object
	.size	_c_slc_to_xlc_ec_ratio, 2
_c_slc_to_xlc_ec_ratio:
	.zero	2
	.zero	6
	.type	ftl_sblk_vpn, %object
	.size	ftl_sblk_vpn, 8
ftl_sblk_vpn:
	.zero	8
	.type	gp_ftl_ext_info, %object
	.size	gp_ftl_ext_info, 8
gp_ftl_ext_info:
	.zero	8
	.type	gp_data_slc_data_head, %object
	.size	gp_data_slc_data_head, 8
gp_data_slc_data_head:
	.zero	8
	.type	gp_nand_para_info, %object
	.size	gp_nand_para_info, 8
gp_nand_para_info:
	.zero	8
	.type	g_idb_ecc_bits, %object
	.size	g_idb_ecc_bits, 1
g_idb_ecc_bits:
	.zero	1
	.type	g_nand_max_die, %object
	.size	g_nand_max_die, 1
g_nand_max_die:
	.zero	1
	.type	g_idb_slc_mode_enable, %object
	.size	g_idb_slc_mode_enable, 1
g_idb_slc_mode_enable:
	.zero	1
	.zero	5
	.type	g_nand_opt_para, %object
	.size	g_nand_opt_para, 32
g_nand_opt_para:
	.zero	32
	.type	g_flash_toggle_mode_en, %object
	.size	g_flash_toggle_mode_en, 1
g_flash_toggle_mode_en:
	.zero	1
	.type	nandc_hw_seed, %object
	.size	nandc_hw_seed, 1
nandc_hw_seed:
	.zero	1
	.zero	6
	.type	g_nandc_v6_master_info, %object
	.size	g_nandc_v6_master_info, 40
g_nandc_v6_master_info:
	.zero	40
	.type	nandc_randomizer_en, %object
	.size	nandc_randomizer_en, 1
nandc_randomizer_en:
	.zero	1
	.zero	1
	.type	fill_spare_size, %object
	.size	fill_spare_size, 2
fill_spare_size:
	.zero	2
	.type	g_nandc_ecc_bits, %object
	.size	g_nandc_ecc_bits, 1
g_nandc_ecc_bits:
	.zero	1
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.zero	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.zero	1
	.zero	1
	.type	gp_flash_info, %object
	.size	gp_flash_info, 8
gp_flash_info:
	.zero	8
	.type	g_die_cs_idx, %object
	.size	g_die_cs_idx, 8
g_die_cs_idx:
	.zero	8
	.type	g_flash_six_addr, %object
	.size	g_flash_six_addr, 1
g_flash_six_addr:
	.zero	1
	.type	_c_ftl_cs_bits, %object
	.size	_c_ftl_cs_bits, 1
_c_ftl_cs_bits:
	.zero	1
	.zero	6
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 4
g_flash_cur_mode:
	.zero	4
	.type	g_flash_micron_3d_tlc_flag, %object
	.size	g_flash_micron_3d_tlc_flag, 1
g_flash_micron_3d_tlc_flag:
	.zero	1
	.type	g_flash_ymtc_3d_tlc_flag, %object
	.size	g_flash_ymtc_3d_tlc_flag, 1
g_flash_ymtc_3d_tlc_flag:
	.zero	1
	.zero	2
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.zero	32
	.type	g_flash_interface_mode, %object
	.size	g_flash_interface_mode, 1
g_flash_interface_mode:
	.zero	1
	.type	g_flash_multi_page_prog_en, %object
	.size	g_flash_multi_page_prog_en, 1
g_flash_multi_page_prog_en:
	.zero	1
	.zero	6
	.type	g_buf, %object
	.size	g_buf, 2048
g_buf:
	.zero	2048
	.type	_c_ftl_sec_per_page, %object
	.size	_c_ftl_sec_per_page, 1
_c_ftl_sec_per_page:
	.zero	1
	.type	p_free_buf_head, %object
	.size	p_free_buf_head, 1
p_free_buf_head:
	.zero	1
	.type	free_buf_count, %object
	.size	free_buf_count, 1
free_buf_count:
	.zero	1
	.type	sblk_queue_head, %object
	.size	sblk_queue_head, 1
sblk_queue_head:
	.zero	1
	.type	sblk_read_completed_queue_head, %object
	.size	sblk_read_completed_queue_head, 1
sblk_read_completed_queue_head:
	.zero	1
	.type	sblk_gc_write_completed_queue_head, %object
	.size	sblk_gc_write_completed_queue_head, 1
sblk_gc_write_completed_queue_head:
	.zero	1
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.zero	1
	.zero	1
	.type	_c_totle_phy_density, %object
	.size	_c_totle_phy_density, 4
_c_totle_phy_density:
	.zero	4
	.type	_c_totle_log_page, %object
	.size	_c_totle_log_page, 4
_c_totle_log_page:
	.zero	4
	.type	free_slc_sblk, %object
	.size	free_slc_sblk, 2
free_slc_sblk:
	.zero	2
	.type	free_xlc_sblk, %object
	.size	free_xlc_sblk, 2
free_xlc_sblk:
	.zero	2
	.type	free_mix_sblk, %object
	.size	free_mix_sblk, 2
free_mix_sblk:
	.zero	2
	.type	slc_data_sblk, %object
	.size	slc_data_sblk, 2
slc_data_sblk:
	.zero	2
	.type	slc_cache_sblk, %object
	.size	slc_cache_sblk, 2
slc_cache_sblk:
	.zero	2
	.type	xlc_data_sblk, %object
	.size	xlc_data_sblk, 2
xlc_data_sblk:
	.zero	2
	.type	write_buf_count, %object
	.size	write_buf_count, 1
write_buf_count:
	.zero	1
	.type	write_commit_count, %object
	.size	write_commit_count, 1
write_commit_count:
	.zero	1
	.zero	2
	.type	gp_ftl_info, %object
	.size	gp_ftl_info, 8
gp_ftl_info:
	.zero	8
	.type	gc_free_slc_sblk_th, %object
	.size	gc_free_slc_sblk_th, 2
gc_free_slc_sblk_th:
	.zero	2
	.type	gc_tlc_mode_tlc_vpn_th, %object
	.size	gc_tlc_mode_tlc_vpn_th, 2
gc_tlc_mode_tlc_vpn_th:
	.zero	2
	.type	gc_tlc_mode_slc_vpn_th, %object
	.size	gc_tlc_mode_slc_vpn_th, 2
gc_tlc_mode_slc_vpn_th:
	.zero	2
	.type	gc_slc_mode_vpn_th, %object
	.size	gc_slc_mode_vpn_th, 2
gc_slc_mode_vpn_th:
	.zero	2
	.type	gc_slc_mode_tlc_vpn_th, %object
	.size	gc_slc_mode_tlc_vpn_th, 2
gc_slc_mode_tlc_vpn_th:
	.zero	2
	.zero	2
	.type	_gc_after_discard_en, %object
	.size	_gc_after_discard_en, 4
_gc_after_discard_en:
	.zero	4
	.type	write_buf_head, %object
	.size	write_buf_head, 1
write_buf_head:
	.zero	1
	.zero	7
	.type	g_gc_info, %object
	.size	g_gc_info, 2136
g_gc_info:
	.zero	2136
	.type	ftl_sblk_vpn_update_id, %object
	.size	ftl_sblk_vpn_update_id, 2
ftl_sblk_vpn_update_id:
	.zero	2
	.zero	6
	.type	ftl_sblk_update_list, %object
	.size	ftl_sblk_update_list, 16
ftl_sblk_update_list:
	.zero	16
	.type	_c_ftl_block_addr_log2, %object
	.size	_c_ftl_block_addr_log2, 2
_c_ftl_block_addr_log2:
	.zero	2
	.type	_c_ftl_planes_per_die, %object
	.size	_c_ftl_planes_per_die, 1
_c_ftl_planes_per_die:
	.zero	1
	.zero	5
	.type	gc_valid_page_ppa, %object
	.size	gc_valid_page_ppa, 8
gc_valid_page_ppa:
	.zero	8
	.type	_c_ftl_nand_type, %object
	.size	_c_ftl_nand_type, 1
_c_ftl_nand_type:
	.zero	1
	.type	_c_ftl_nand_planes_num, %object
	.size	_c_ftl_nand_planes_num, 1
_c_ftl_nand_planes_num:
	.zero	1
	.type	g_flash_3d_mlc_flag, %object
	.size	g_flash_3d_mlc_flag, 1
g_flash_3d_mlc_flag:
	.zero	1
	.type	g_one_pass_program, %object
	.size	g_one_pass_program, 1
g_one_pass_program:
	.zero	1
	.zero	4
	.type	gc_page_buf_id, %object
	.size	gc_page_buf_id, 8
gc_page_buf_id:
	.zero	8
	.type	g_flash_3d_tlc_flag, %object
	.size	g_flash_3d_tlc_flag, 1
g_flash_3d_tlc_flag:
	.zero	1
	.type	gc_mode, %object
	.size	gc_mode, 1
gc_mode:
	.zero	1
	.type	_c_ftl_page_pre_blk, %object
	.size	_c_ftl_page_pre_blk, 2
_c_ftl_page_pre_blk:
	.zero	2
	.zero	4
	.type	gp_data_xlc_data_head, %object
	.size	gp_data_xlc_data_head, 8
gp_data_xlc_data_head:
	.zero	8
	.type	gp_data_slc_cache_head, %object
	.size	gp_data_slc_cache_head, 8
gp_data_slc_cache_head:
	.zero	8
	.type	gc_slc_data_index, %object
	.size	gc_slc_data_index, 2
gc_slc_data_index:
	.zero	2
	.type	gc_slc_cache_index, %object
	.size	gc_slc_cache_index, 2
gc_slc_cache_index:
	.zero	2
	.type	gc_xlc_data_index, %object
	.size	gc_xlc_data_index, 2
gc_xlc_data_index:
	.zero	2
	.type	_c_ftl_page_pre_slc_blk, %object
	.size	_c_ftl_page_pre_slc_blk, 2
_c_ftl_page_pre_slc_blk:
	.zero	2
	.type	gc_xlc_search_index, %object
	.size	gc_xlc_search_index, 2
gc_xlc_search_index:
	.zero	2
	.type	_min_slc_super_block, %object
	.size	_min_slc_super_block, 2
_min_slc_super_block:
	.zero	2
	.type	_max_xlc_super_block, %object
	.size	_max_xlc_super_block, 2
_max_xlc_super_block:
	.zero	2
	.zero	2
	.type	gp_free_slc_head, %object
	.size	gp_free_slc_head, 8
gp_free_slc_head:
	.zero	8
	.type	gp_free_xlc_head, %object
	.size	gp_free_xlc_head, 8
gp_free_xlc_head:
	.zero	8
	.type	gp_free_mix_head, %object
	.size	gp_free_mix_head, 8
gp_free_mix_head:
	.zero	8
	.type	zftl_print_list_count, %object
	.size	zftl_print_list_count, 2
zftl_print_list_count:
	.zero	2
	.type	_c_ftl_block_align_addr, %object
	.size	_c_ftl_block_align_addr, 2
_c_ftl_block_align_addr:
	.zero	2
	.type	_c_ftl_nand_die_num, %object
	.size	_c_ftl_nand_die_num, 1
_c_ftl_nand_die_num:
	.zero	1
	.zero	3
	.type	lpa_hash, %object
	.size	lpa_hash, 512
lpa_hash:
	.zero	512
	.type	ftl_sblk_lpa_tbl, %object
	.size	ftl_sblk_lpa_tbl, 8
ftl_sblk_lpa_tbl:
	.zero	8
	.type	lpa_hash_index, %object
	.size	lpa_hash_index, 8
lpa_hash_index:
	.zero	8
	.type	ftl_vpn_update_count, %object
	.size	ftl_vpn_update_count, 2
ftl_vpn_update_count:
	.zero	2
	.type	ftl_sblk_update_list_offset, %object
	.size	ftl_sblk_update_list_offset, 2
ftl_sblk_update_list_offset:
	.zero	2
	.type	g_flash_micron_3d_tlc_b05a, %object
	.size	g_flash_micron_3d_tlc_b05a, 1
g_flash_micron_3d_tlc_b05a:
	.zero	1
	.zero	1
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.zero	2
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.zero	2
	.type	read_buf_head, %object
	.size	read_buf_head, 1
read_buf_head:
	.zero	1
	.type	read_buf_count, %object
	.size	read_buf_count, 1
read_buf_count:
	.zero	1
	.type	pm_gc_enable, %object
	.size	pm_gc_enable, 4
pm_gc_enable:
	.zero	4
	.type	pm_ram_info, %object
	.size	pm_ram_info, 512
pm_ram_info:
	.zero	512
	.type	pm_last_update_ram_id, %object
	.size	pm_last_update_ram_id, 1
pm_last_update_ram_id:
	.zero	1
	.zero	3
	.type	g_msb_page_tbl, %object
	.size	g_msb_page_tbl, 2048
g_msb_page_tbl:
	.zero	2048
	.type	g_slc_page_num, %object
	.size	g_slc_page_num, 2
g_slc_page_num:
	.zero	2
	.zero	2
	.type	g_die_addr, %object
	.size	g_die_addr, 32
g_die_addr:
	.zero	32
	.type	g_totle_phy_block, %object
	.size	g_totle_phy_block, 2
g_totle_phy_block:
	.zero	2
	.type	gc_state, %object
	.size	gc_state, 1
gc_state:
	.zero	1
	.zero	1
	.type	gc_search_count, %object
	.size	gc_search_count, 4
gc_search_count:
	.zero	4
	.type	gc_slc_mode_slc_vpn_th, %object
	.size	gc_slc_mode_slc_vpn_th, 2
gc_slc_mode_slc_vpn_th:
	.zero	2
	.zero	6
	.type	gc_lpa_tbl, %object
	.size	gc_lpa_tbl, 8
gc_lpa_tbl:
	.zero	8
	.type	gc_pre_ppa_tbl, %object
	.size	gc_pre_ppa_tbl, 8
gc_pre_ppa_tbl:
	.zero	8
	.type	gc_des_ppa_tbl, %object
	.size	gc_des_ppa_tbl, 8
gc_des_ppa_tbl:
	.zero	8
	.type	pm_force_gc, %object
	.size	pm_force_gc, 4
pm_force_gc:
	.zero	4
	.type	_c_swl_slc_gc_th, %object
	.size	_c_swl_slc_gc_th, 2
_c_swl_slc_gc_th:
	.zero	2
	.type	_c_swl_xlc_gc_th, %object
	.size	_c_swl_xlc_gc_th, 2
_c_swl_xlc_gc_th:
	.zero	2
	.type	_c_max_pm_sblk, %object
	.size	_c_max_pm_sblk, 2
_c_max_pm_sblk:
	.zero	2
	.zero	6
	.type	power_on_init_jiffies, %object
	.size	power_on_init_jiffies, 8
power_on_init_jiffies:
	.zero	8
	.type	gp_ftl_api, %object
	.size	gp_ftl_api, 8
gp_ftl_api:
	.zero	8
	.type	RK29_NANDC_REG_BASE, %object
	.size	RK29_NANDC_REG_BASE, 8
RK29_NANDC_REG_BASE:
	.zero	8
	.type	g_flash_tmp_page_buffer, %object
	.size	g_flash_tmp_page_buffer, 8
g_flash_tmp_page_buffer:
	.zero	8
	.type	g_nandc_tran_timeout, %object
	.size	g_nandc_tran_timeout, 1
g_nandc_tran_timeout:
	.zero	1
	.zero	7
	.type	g_flash_tmp_spare_buffer, %object
	.size	g_flash_tmp_spare_buffer, 8
g_flash_tmp_spare_buffer:
	.zero	8
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.zero	1
	.zero	3
	.type	flash_ddr_tuning_sdr_read_count, %object
	.size	flash_ddr_tuning_sdr_read_count, 4
flash_ddr_tuning_sdr_read_count:
	.zero	4
	.type	flash_read_retry, %object
	.size	flash_read_retry, 8
flash_read_retry:
	.zero	8
	.type	g_flash_page_buffer, %object
	.size	g_flash_page_buffer, 8
g_flash_page_buffer:
	.zero	8
	.type	g_flash_spare_buffer, %object
	.size	g_flash_spare_buffer, 8
g_flash_spare_buffer:
	.zero	8
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.zero	1
	.zero	3
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.zero	4
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
	.zero	1
	.zero	47
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 256
ftl_info_spare:
	.zero	256
	.type	g_ftl_info_blk, %object
	.size	g_ftl_info_blk, 4
g_ftl_info_blk:
	.zero	4
	.zero	4
	.type	ftl_info_data_buffer, %object
	.size	ftl_info_data_buffer, 8
ftl_info_data_buffer:
	.zero	8
	.type	ftl_sys_info_first_write, %object
	.size	ftl_sys_info_first_write, 1
ftl_sys_info_first_write:
	.zero	1
	.type	ftl_power_lost_flag, %object
	.size	ftl_power_lost_flag, 1
ftl_power_lost_flag:
	.zero	1
	.type	ftl_ext_info_first_write, %object
	.size	ftl_ext_info_first_write, 1
ftl_ext_info_first_write:
	.zero	1
	.zero	5
	.type	ftl_ext_info_data_buffer, %object
	.size	ftl_ext_info_data_buffer, 8
ftl_ext_info_data_buffer:
	.zero	8
	.zero	32
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 256
ftl_tmp_spare:
	.zero	256
	.type	g_pm_spare, %object
	.size	g_pm_spare, 8
g_pm_spare:
	.zero	8
	.type	pm_first_write, %object
	.size	pm_first_write, 1
pm_first_write:
	.zero	1
	.zero	7
	.type	g_flash_sys_spare_buffer, %object
	.size	g_flash_sys_spare_buffer, 8
g_flash_sys_spare_buffer:
	.zero	8
	.type	g_flash_blk_info, %object
	.size	g_flash_blk_info, 4
g_flash_blk_info:
	.zero	4
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.zero	1
	.type	g_flash_micron_3d_tlc_b16a, %object
	.size	g_flash_micron_3d_tlc_b16a, 1
g_flash_micron_3d_tlc_b16a:
	.zero	1
	.type	_c_ftl_byte_pre_page, %object
	.size	_c_ftl_byte_pre_page, 2
_c_ftl_byte_pre_page:
	.zero	2
	.type	pm_last_load_ram_id, %object
	.size	pm_last_load_ram_id, 1
pm_last_load_ram_id:
	.zero	1
	.type	_ftl_gc_tag_page_num, %object
	.size	_ftl_gc_tag_page_num, 1
_ftl_gc_tag_page_num:
	.zero	1
	.zero	2
	.type	_last_read_time, %object
	.size	_last_read_time, 4
_last_read_time:
	.zero	4
	.type	_last_write_time, %object
	.size	_last_write_time, 4
_last_write_time:
	.zero	4
	.type	read_ahead_lpa, %object
	.size	read_ahead_lpa, 4
read_ahead_lpa:
	.zero	4
	.type	_c_totle_data_density, %object
	.size	_c_totle_data_density, 4
_c_totle_data_density:
	.zero	4
	.type	_c_ftl_pm_page_num, %object
	.size	_c_ftl_pm_page_num, 2
_c_ftl_pm_page_num:
	.zero	2
	.zero	2
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 8
ftl_tmp_buffer:
	.zero	8
	.type	rk_zftl_enable, %object
	.size	rk_zftl_enable, 1
rk_zftl_enable:
	.zero	1
	.zero	7
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 8
gLoaderBootInfo:
	.zero	8
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 8
RK29_NANDC1_REG_BASE:
	.zero	8
	.type	discard_sector_count, %object
	.size	discard_sector_count, 4
discard_sector_count:
	.zero	4
	.type	idb_write_enable, %object
	.size	idb_write_enable, 1
idb_write_enable:
	.zero	1
	.zero	3
	.type	idb_buf, %object
	.size	idb_buf, 8
idb_buf:
	.zero	8
	.type	idb_last_lba, %object
	.size	idb_last_lba, 4
idb_last_lba:
	.zero	4
	.zero	4
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 8
gpDrmKeyInfo:
	.zero	8
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.zero	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.zero	4
	.type	gpBootConfig, %object
	.size	gpBootConfig, 8
gpBootConfig:
	.zero	8
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.zero	512
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.zero	4
	.zero	4
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 8
gpVendor0Info:
	.zero	8
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 8
gpVendor1Info:
	.zero	8
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 8
g_idb_buffer:
	.zero	8
	.type	g_vendor, %object
	.size	g_vendor, 8
g_vendor:
	.zero	8
	.type	ftl_low_format_cur_blk, %object
	.size	ftl_low_format_cur_blk, 2
ftl_low_format_cur_blk:
	.zero	2
	.zero	6
	.type	p_read_ahead_ext_buf, %object
	.size	p_read_ahead_ext_buf, 8
p_read_ahead_ext_buf:
	.zero	8
	.type	_c_ftl_nand_blks_per_die, %object
	.size	_c_ftl_nand_blks_per_die, 2
_c_ftl_nand_blks_per_die:
	.zero	2
	.zero	6
	.type	nandc_ecc_sts, %object
	.size	nandc_ecc_sts, 16
nandc_ecc_sts:
	.zero	16
	.type	g_slc_mode_enable, %object
	.size	g_slc_mode_enable, 1
g_slc_mode_enable:
	.zero	1
