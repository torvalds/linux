/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
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
	adrp	x1, .LANCHOR0+516
	ldrb	w1, [x1,#:lo12:.LANCHOR0+516]
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
	adrp	x0, .LANCHOR0+520
	ldr	w0, [x0,#:lo12:.LANCHOR0+520]
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
	ldr	x23, [x3,528]
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
	mov	w2, 188
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
	ldr	x0, [x19,528]
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
	ldr	x0, [x19,528]
	b	.L36
.L35:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	ldrh	w2, [x20,2]
	umull	x0, w0, w1
	ldr	x3, [x19,528]
	add	x0, x3, x0
	strh	w2, [x0,2]
	ldrh	w0, [x20,2]
	ldrh	w2, [x23,x21]
	umull	x0, w0, w1
	ldr	x1, [x19,528]
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
	adrp	x3, .LANCHOR0+528
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3,#:lo12:.LANCHOR0+528]
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
	ldrb	w1, [x0,516]
	cmp	w1, 9
	ldr	x1, [x0,536]
	ldr	w2, [x1]
	str	w2, [x0,544]
	ldr	w2, [x1,4]
	str	w2, [x0,548]
	bne	.L47
	ldr	w2, [x1,16]
	str	w2, [x0,552]
	ldr	w2, [x1,32]
	str	w2, [x0,556]
	ldr	w2, [x1,80]
	str	w2, [x0,560]
	ldr	w2, [x1,84]
	str	w2, [x0,564]
	ldr	w2, [x1,520]
	str	w2, [x0,568]
	ldr	w1, [x1,8]
	b	.L49
.L47:
	ldr	w2, [x1,8]
	str	w2, [x0,552]
	ldr	w2, [x1,12]
	str	w2, [x0,556]
	ldr	w2, [x1,304]
	str	w2, [x0,560]
	ldr	w2, [x1,308]
	str	w2, [x0,564]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,568]
.L49:
	str	w1, [x0,572]
	ret
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	adrp	x2, .LANCHOR0+516
	ldrb	w2, [x2,#:lo12:.LANCHOR0+516]
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
	ldrh	w4, [x6,576]
	cmp	w4, w1
	bls	.L53
	ldrh	w3, [x2]
	mov	w7, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x11, w1, w7
	ldr	x12, [x6,528]
	mov	w2, -1
	add	x5, x12, x11
	strh	w2, [x5,2]
	strh	w2, [x12,x11]
	ldr	x2, [x0]
	cbz	x2, .L60
	ldr	x9, [x6,584]
	ubfiz	x8, x1, 2, 16
	ldrh	w13, [x6,592]
	mov	w16, 65535
	ldr	x14, [x6,528]
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
	ldr	x3, [x3,528]
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
	ldrh	w3, [x10,576]
	cmp	w3, w1
	bls	.L61
	ldrh	w3, [x2]
	mov	w8, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x8, w1, w8
	ldr	x14, [x10,528]
	mov	w2, -1
	add	x7, x14, x8
	strh	w2, [x7,2]
	strh	w2, [x14,x8]
	ldr	x2, [x0]
	cbz	x2, .L79
	uxtw	x9, w1
	ldr	x15, [x10,600]
	ldr	x13, [x10,584]
	ldrh	w11, [x7,4]
	ldrh	w5, [x15,x9,lsl 1]
	lsl	x9, x9, 2
	ldrh	w16, [x10,592]
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
	ldr	x17, [x4,528]
	ldrh	w20, [x4,576]
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
	ldr	x3, [x6,528]
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
	adrp	x2, .LANCHOR0+528
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x2,#:lo12:.LANCHOR0+528]
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
	ldr	x3, [x4,608]
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
	ldr	x25, [x4,528]
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
	mov	w2, 255
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
	ldr	x4, [x1,600]
	ldr	x1, [x1,528]
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
	adrp	x0, .LANCHOR0+616
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR0+616]
	bl	_list_get_gc_head_node.isra.4
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node.part.11, .-zftl_get_gc_node.part.11
	.section	.text.unlikely
	.align	2
	.type	rk_simple_strtoull.constprop.33, %function
rk_simple_strtoull.constprop.33:
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
	.size	rk_simple_strtoull.constprop.33, .-rk_simple_strtoull.constprop.33
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
	adrp	x0, .LANCHOR0+624
	ldr	x6, [x0,#:lo12:.LANCHOR0+624]
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
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,8]
	bl	printk
.L118:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L119
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrb	w1, [x1,9]
	bl	printk
.L119:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L120
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrh	w1, [x1,10]
	bl	printk
.L120:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L121
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,12]
	bl	printk
.L121:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L122
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,13]
	bl	printk
.L122:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L123
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrh	w1, [x1,14]
	bl	printk
.L123:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L124
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	ldrb	w1, [x1,23]
	bl	printk
.L124:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L125
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,18]
	bl	printk
.L125:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L126
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,19]
	bl	printk
.L126:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L127
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrb	w1, [x1,20]
	bl	printk
.L127:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L128
	adrp	x1, .LANCHOR0+632
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w1, [x1,#:lo12:.LANCHOR0+632]
	bl	printk
.L128:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x1, .LANCHOR0
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	bl	printk
.L129:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ldrb	w1, [x1,22]
	bl	printk
.L130:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x1, .LANCHOR0+633
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	ldrb	w1, [x1,#:lo12:.LANCHOR0+633]
	bl	printk
.L131:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L132
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	and	w1, w1, 1
	bl	printk
.L132:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L133
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 1, 1
	bl	printk
.L133:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L134
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 2, 1
	bl	printk
.L134:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L135
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 3, 1
	bl	printk
.L135:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L136
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 4, 1
	bl	printk
.L136:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L137
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 5, 1
	bl	printk
.L137:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L138
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 6, 1
	bl	printk
.L138:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L139
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	ubfx	x1, x1, 7, 1
	bl	printk
.L139:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L140
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrb	w1, [x0,17]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	and	w1, w1, 1
	bl	printk
.L140:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L141
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 9, 1
	bl	printk
.L141:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L142
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	ubfx	x1, x1, 10, 1
	bl	printk
.L142:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L143
	adrp	x1, .LANCHOR0
	adrp	x0, .LC31
	add	x2, x1, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC31
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldrb	w2, [x2,634]
	bl	printk
.L143:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L144
	adrp	x2, .LANCHOR0
	adrp	x0, .LC32
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC32
	add	x2, x2, 640
	ldrb	w1, [x2,10]
	ldrb	w2, [x2,11]
	bl	printk
.L144:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L145
	adrp	x2, .LANCHOR0
	adrp	x0, .LC33
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC33
	add	x2, x2, 640
	ldrb	w1, [x2,8]
	ldrb	w2, [x2,9]
	bl	printk
.L145:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L115
	adrp	x1, .LANCHOR0+672
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	ldrb	w1, [x1,#:lo12:.LANCHOR0+672]
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
	tbz	x0, 12, .L241
	adrp	x1, .LANCHOR1
	adrp	x0, .LC35
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC35
	add	x1, x1, 72
	mov	x2, x22
	bl	printk
.L241:
	adrp	x1, .LANCHOR0
	mov	w2, 6
	add	x0, x1, :lo12:.LANCHOR0
	ldr	w3, [x22,352]
	mov	x20, x1
	strb	w2, [x0,516]
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	str	x22, [x0,536]
	cmp	w3, w2
	bne	.L242
	mov	w1, 8
	strb	w1, [x0,516]
.L242:
	ldr	w1, [x22,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L243
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 9
	strb	w1, [x0,516]
.L243:
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,516]
	cmp	w0, 9
	bne	.L244
	mov	w0, 1
	strb	w0, [x19,673]
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
	ldr	x0, [x19,536]
	str	wzr, [x0,520]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x19,536]
	str	w1, [x0,8]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,80]
	mov	w1, 38
	str	w1, [x0,84]
	mov	w1, 39
	str	w1, [x0,84]
	ldr	w1, [x21,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L246
	ldr	w1, [x0]
	ldr	w2, [x0,8]
	ldr	w3, [x0,80]
	ldr	w4, [x0,84]
	ldr	w5, [x0,88]
	b	.L258
.L244:
	ldr	w0, [x29,56]
	mov	w1, 1
	strb	wzr, [x19,673]
	orr	w0, w0, 256
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	bfi	w0, w1, 24, 3
	str	w0, [x29,56]
	mov	w1, 4193
	ldr	w0, [x29,56]
	str	w0, [x22]
	ldr	x0, [x19,536]
	str	wzr, [x0,336]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x19,536]
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
	str	x0, [x19,680]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L246
	ldr	x0, [x19,536]
	ldr	w1, [x0]
	ldr	w2, [x0,344]
	ldr	w3, [x0,304]
	ldr	w4, [x0,308]
	ldr	w5, [x0,312]
.L258:
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	bl	printk
.L246:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x1,720]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	strh	wzr, [x1,722]
	strb	wzr, [x1,724]
	tbz	x0, 12, .L240
	adrp	x0, .LC37
	ldrb	w1, [x1,516]
	add	x0, x0, :lo12:.LC37
	bl	printk
.L240:
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
.L260:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L260
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
.L264:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,726]
	cmp	w0, w20
	bls	.L270
	add	x0, x19, 88
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,725]
	cmp	w0, 34
	bne	.L265
	ldrsb	w0, [x24,x20]
	b	.L269
.L265:
	cmp	w0, 35
	bne	.L267
	ldrsb	w0, [x21,x20]
	b	.L269
.L267:
	ldrsb	w0, [x23,232]
.L269:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L264
.L270:
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
	ldr	x2, [x3,728]
	ldr	x19, [x3,536]
	add	x23, x2, 128
	add	x21, x2, 112
	ldrb	w22, [x2,113]
	ldrb	w2, [x2,112]
	mul	w1, w1, w22
	cmp	w2, 8
	sxtw	x1, w1
	beq	.L272
	sxtw	x2, w0
	mov	x3, 160
	madd	x2, x2, x3, x1
	add	x2, x2, 32
	add	x21, x21, x2
	b	.L273
.L272:
	add	x1, x1, 32
	add	x21, x21, x1
.L273:
	ubfiz	x0, x0, 8, 8
	mov	x20, 0
	add	x19, x19, x0
	mov	w0, 54
	str	w0, [x19,2056]
.L274:
	cmp	w22, w20, uxtb
	bls	.L276
	ldrb	w0, [x23,x20]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x21,x20]
	add	x20, x20, 1
	str	w0, [x19,2048]
	b	.L274
.L276:
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
	ldrb	w1, [x19,725]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 7
	bhi	.L277
	ldr	x1, [x19,728]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L277
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,728]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L277:
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
	ldrb	w2, [x1,516]
	ldr	x1, [x1,536]
	cmp	w2, 9
	lsl	w2, w0, 16
	lsl	w0, w0, 8
	orr	w0, w2, w0
	orr	w0, w0, 3
	bne	.L283
	str	w0, [x1,80]
	b	.L282
.L283:
	str	w0, [x1,304]
.L282:
	ret
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,516]
	ldr	x0, [x0,536]
	cmp	w1, 9
	bne	.L286
	ldr	w0, [x0,80]
	b	.L288
.L286:
	ldr	w0, [x0,304]
.L288:
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
	ldr	x2, [x3,536]
	ldr	w1, [x2]
	beq	.L290
	orr	w1, w1, 24576
	and	w0, w0, 4
	and	w1, w1, -32769
	cmp	w0, wzr
	orr	w1, w1, 196608
	ldrb	w0, [x3,516]
	orr	w4, w1, 32768
	csel	w1, w4, w1, ne
	cmp	w0, 9
	mov	w0, 8321
	bne	.L292
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L293
.L292:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L293
.L290:
	and	w1, w1, -8193
.L293:
	str	w1, [x2]
	ret
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	adrp	x1, .LANCHOR0+536
	mov	w2, 1
	lsl	w0, w2, w0
	ldr	x3, [x1,#:lo12:.LANCHOR0+536]
	ldr	w1, [x3]
	bfi	w1, w0, 0, 8
	str	w1, [x3]
	ret
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	adrp	x0, .LANCHOR0+536
	ldr	x1, [x0,#:lo12:.LANCHOR0+536]
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
	stp	x29, x30, [sp, -80]!
	adrp	x3, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x3, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w20, w1
	mov	w22, w2
	ldrb	w0, [x0,633]
	mov	x19, x3
	cmp	w0, w21
	bhi	.L299
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 700
	add	x1, x1, 240
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L299:
	add	x3, x19, :lo12:.LANCHOR0
	add	x21, x3, x21, sxtw
	ldrb	w23, [x21,736]
	lsr	w24, w20, 8
	lsr	w25, w20, 16
	mov	w26, 120
	mov	w0, w23
	ldr	x19, [x3,536]
	and	w21, w20, 255
	bl	nandc_cs
	ubfiz	x3, x23, 8, 8
	add	x19, x19, x3
.L301:
	str	w26, [x19,2056]
	mov	w0, 120
	str	w21, [x19,2052]
	str	w24, [x19,2052]
	str	w25, [x19,2052]
	bl	timer_delay_ns
	ldr	w20, [x19,2048]
	and	w20, w20, 255
	and	w0, w22, w20
	cmp	w0, w22
	bne	.L301
	cmp	w20, 255
	beq	.L301
	mov	w0, w23
	bl	nandc_de_cs
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
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
	ldrb	w8, [x7,744]
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
	ldr	x0, [x0,536]
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
	adrp	x2, .LANCHOR0
	uxtb	x0, w0
	stp	x29, x30, [sp, -32]!
	add	x1, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	str	x19, [sp,16]
	cbz	w2, .L317
	cmp	w2, 1
	ldr	x19, [x1,536]
	bne	.L320
	adrp	x1, .LANCHOR2+37
	ldrb	w2, [x1,#:lo12:.LANCHOR2+37]
	cbz	w2, .L317
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x19, x1, lsl 8
	str	w2, [x0,8]
	b	.L317
.L320:
	cmp	w2, 2
	bne	.L322
	add	x1, x1, x0
	ldrb	w2, [x1,752]
	cbz	w2, .L317
	strb	wzr, [x1,752]
	adrp	x1, .LANCHOR2+37
	ldrb	w1, [x1,#:lo12:.LANCHOR2+37]
	cbz	w1, .L317
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w1, [x0,8]
	b	.L317
.L322:
	cmp	w2, 3
	bne	.L317
	add	x1, x1, x0, sxtw
	ldrb	w2, [x1,752]
	cbz	w2, .L317
	strb	wzr, [x1,752]
	ubfiz	x1, x0, 8, 8
	add	x19, x19, x1
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	w0, 50
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
	mov	w0, 150
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
.L317:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	uxtb	x0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x20, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	str	x21, [sp,32]
	cbz	w1, .L335
	cmp	w1, 1
	ldr	x19, [x20,536]
	bne	.L338
	adrp	x1, .LANCHOR2+38
	ldrb	w2, [x1,#:lo12:.LANCHOR2+38]
	cbz	w2, .L335
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x19, x1, lsl 8
	str	w2, [x0,8]
	b	.L335
.L338:
	cmp	w1, 2
	bne	.L340
	add	x20, x20, x0
	ldrb	w1, [x20,752]
	cbnz	w1, .L335
	adrp	x1, .LANCHOR2
	mov	w3, 4
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 8
	ldrb	w2, [x1,12]
	ldrb	w1, [x1,30]
	cmp	w2, 2
	csel	w2, w2, w3, eq
	strb	w2, [x20,752]
	cbz	w1, .L335
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w1, [x0,8]
	b	.L335
.L340:
	cmp	w1, 3
	bne	.L335
	add	x20, x20, x0, sxtw
	ldrb	w1, [x20,752]
	cbnz	w1, .L335
	adrp	x21, .LANCHOR2
	mov	w2, 4
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w1, [x21,20]
	cmp	w1, 2
	csel	w1, w1, w2, eq
	strb	w1, [x20,752]
	ubfiz	x1, x0, 8, 8
	mov	w0, 239
	add	x19, x19, x1
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	w0, 50
	bl	timer_delay_ns
	ldrb	w0, [x21,15]
	cmp	w0, 9
	mov	w0, 1
	bne	.L343
	str	w0, [x19,2048]
	b	.L344
.L343:
	ldrb	w1, [x20,752]
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
	mov	w0, 150
	bl	timer_delay_ns
	bl	nandc_wait_flash_ready_no_delay
.L335:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	uxtb	w21, w0
	stp	x23, x24, [sp,48]
	mov	w23, w1
	add	x1, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w0, 1
	mov	w20, 24
	ldrb	w19, [x1,744]
	sub	w20, w20, w19
	lsl	w19, w0, w19
	lsr	w2, w23, w20
	sub	w19, w19, #1
	and	w19, w2, w19
	ldrb	w0, [x1,633]
	uxtb	w19, w19
	cmp	w0, w19
	bhi	.L356
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 878
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L356:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,633]
	cmp	w0, w19
	bls	.L355
	mov	w2, 1
	add	x19, x22, x19, sxtw
	lsl	w20, w2, w20
	ldrb	w19, [x19,736]
	sub	w20, w20, #1
	ldr	x24, [x22,536]
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
	ldr	x0, [x22,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L360
	ldrb	w0, [x22,756]
	cbnz	w0, .L360
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x24, x0, lsl 8
	str	w23, [x0,8]
	b	.L359
.L360:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L359:
	ubfiz	x19, x19, 8, 8
	and	w0, w20, 255
	add	x1, x24, x19
	str	wzr, [x1,2056]
	str	wzr, [x1,2052]
	str	wzr, [x1,2052]
	str	w0, [x1,2052]
	lsr	w0, w20, 8
	str	w0, [x1,2052]
	lsr	w20, w20, 16
	str	w20, [x1,2052]
	mov	w0, 0
	str	w21, [x1,2056]
	bl	nandc_de_cs
.L355:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
.L366:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L367
	mov	w0, 10
	bl	timer_delay_ns
	subs	w19, w19, #1
	bne	.L366
	mov	w0, -1
	b	.L365
.L367:
	mov	w0, 0
.L365:
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
.L370:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,726]
	cmp	w4, w2
	bls	.L375
	ldrb	w3, [x3,725]
	cmp	w3, 67
	bne	.L371
	ldrsb	w3, [x0,x2]
	b	.L374
.L371:
	ldrsb	w3, [x1,x2]
.L374:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L370
.L375:
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
	cbz	w2, .L379
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
	b	.L380
.L379:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 376
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L380:
	bl	nandc_wait_flash_ready
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	flash_start_plane_read
	.type	flash_start_plane_read, %function
flash_start_plane_read:
	stp	x29, x30, [sp, -96]!
	mov	w3, 24
	mov	w2, 1
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	add	x4, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ubfx	x21, x0, 24, 2
	ldrb	w5, [x4,744]
	sub	w3, w3, w5
	lsl	w20, w2, w3
	lsl	w2, w2, w5
	lsr	w19, w0, w3
	sub	w2, w2, #1
	sub	w20, w20, #1
	and	w19, w19, w2
	and	w22, w20, w0
	ldrb	w0, [x4,633]
	uxtb	w19, w19
	and	w20, w20, w1
	cmp	w0, w19
	bhi	.L382
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 977
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L382:
	add	x26, x23, :lo12:.LANCHOR0
	add	x19, x26, x19, sxtw
	ldrb	w24, [x19,736]
	mov	w0, w24
	ldr	x25, [x26,536]
	bl	nandc_cs
	cbnz	w21, .L383
	mov	w0, w22
	bl	slc_phy_page_address_calc
	mov	w22, w0
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L384
	mov	w0, w24
	bl	zftl_flash_enter_slc_mode
	b	.L384
.L383:
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L385
	ldrb	w0, [x26,756]
	cbnz	w0, .L385
	sxtw	x0, w24
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w21, [x0,8]
	b	.L384
.L385:
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
.L384:
	add	x26, x23, :lo12:.LANCHOR0
	and	w2, w22, 255
	add	x0, x26, 640
	and	w28, w20, 255
	lsr	w27, w20, 8
	uxtb	x19, w24
	ldrb	w1, [x0,16]
	lsr	w20, w20, 16
	cmp	w1, 1
	lsr	w1, w22, 8
	lsr	w22, w22, 16
	bne	.L386
	add	x19, x25, x19, lsl 8
	ldrb	w3, [x0,8]
	ldrb	w0, [x0,9]
	str	w3, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	str	w22, [x19,2052]
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	cbz	w21, .L387
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L387
	str	w21, [x19,2056]
.L387:
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	b	.L405
.L386:
	add	x19, x25, x19, lsl 8
	ldrb	w0, [x0,8]
	str	w0, [x19,2056]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	str	w22, [x19,2052]
	cbz	w21, .L389
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L389
	str	w21, [x19,2056]
.L389:
	add	x23, x23, :lo12:.LANCHOR0
	ldrb	w0, [x23,649]
	str	w0, [x19,2056]
.L405:
	str	w28, [x19,2052]
	mov	w0, 48
	str	w27, [x19,2052]
	str	w20, [x19,2052]
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
	adrp	x23, .LC39
	adrp	x24, .LC38
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w25, w0
	mov	x21, 0
	add	x22, x22, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC39
	add	x24, x24, :lo12:.LC38
.L421:
	add	x0, x22, x21, lsl 3
	ldr	x20, [x22,536]
	adrp	x1, .LANCHOR0
	ldrb	w19, [x0,760]
	cmp	w19, 152
	beq	.L407
	cmp	w19, 69
	beq	.L407
	cmp	w19, 155
	beq	.L407
	cmp	w19, 44
	cset	w2, eq
	cmp	w19, 137
	cset	w0, eq
	orr	w0, w2, w0
	cbz	w0, .L408
.L407:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w25, 1
	ldrb	w1, [x1,792]
	bne	.L410
	tbz	x1, 0, .L408
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L411
	mov	x0, x24
	bl	printk
.L411:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L422
	cmp	w19, 44
	bne	.L412
.L422:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L441
.L412:
	cmp	w19, 155
	mov	w0, 1
	bne	.L420
	str	w0, [x1,2052]
	mov	w0, 3
	b	.L441
.L410:
	tbz	x1, 2, .L408
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L416
	mov	x0, x23
	bl	printk
.L416:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L423
	cmp	w19, 44
	cset	w0, eq
	cbz	w0, .L417
.L423:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L441
.L417:
	cmp	w19, 155
	bne	.L420
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 37
	b	.L441
.L420:
	mov	w2, 128
	str	w2, [x1,2052]
.L441:
	str	w0, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L408:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L421
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
	adrp	x0, .LANCHOR0+536
	ldr	x0, [x0,#:lo12:.LANCHOR0+536]
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
	adrp	x1, .LANCHOR0+536
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 8
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR0+536]
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
	adrp	x0, .LANCHOR0+536
	stp	x19, x20, [sp,16]
	mov	x19, x1
	ldr	x20, [x0,#:lo12:.LANCHOR0+536]
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
	bhi	.L445
	adrp	x0, .LC40
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC40
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L445:
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
	adrp	x4, .LANCHOR0+536
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x3, .LANCHOR2+17
	ldr	x19, [x4,#:lo12:.LANCHOR0+536]
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
	adrp	x0, .LANCHOR0+536
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	mov	w20, w1
	ldr	x19, [x0,#:lo12:.LANCHOR0+536]
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
	adrp	x0, .LC41
	mov	w1, w20
	add	x0, x0, :lo12:.LC41
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
.L449:
	ldr	w0, [x19,2048]
	strb	w0, [x23,x1]
	add	x1, x1, 1
	cmp	x1, 16384
	bne	.L449
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
	adrp	x2, .LANCHOR0+536
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x2,#:lo12:.LANCHOR0+536]
	add	x2, x20, :lo12:.LANCHOR2
	str	x21, [sp,32]
	ldrb	w3, [x2,37]
	sxtw	x2, w0
	add	x2, x2, 8
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L465
	mov	w3, 162
.L465:
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
	beq	.L454
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L454
	adrp	x0, .LC42
	add	x0, x0, :lo12:.LC42
	bl	printk
.L454:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,38]
	cbz	w0, .L455
	str	w0, [x19,2056]
.L455:
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
	adrp	x0, .LANCHOR0+536
	sub	sp, sp, #16
	ldr	x0, [x0,#:lo12:.LANCHOR0+536]
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
	ldrb	w2, [x1,516]
	strb	w0, [x1,724]
	cmp	w2, 9
	ldr	x2, [x1,536]
	ldr	w1, [sp,8]
	bne	.L468
	str	w1, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L469
	cmp	w0, 60
	mov	w1, 3
	beq	.L469
	cmp	w0, 40
	mov	w1, 2
	csinc	w1, w1, wzr, eq
.L469:
	str	wzr, [sp]
	ldr	w0, [sp]
	bfi	w0, w1, 25, 3
	str	w0, [sp]
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,32]
	b	.L467
.L468:
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
	beq	.L477
	cmp	w0, 24
	bne	.L473
	ldr	w0, [sp]
	orr	w0, w0, 16
	b	.L478
.L473:
	ldr	w1, [sp]
	cmp	w0, 40
	orr	w1, w1, 262144
	str	w1, [sp]
	ldr	w1, [sp]
	orr	w1, w1, 16
	str	w1, [sp]
	bne	.L472
.L477:
	ldr	w0, [sp]
	and	w0, w0, -17
.L478:
	str	w0, [sp]
.L472:
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,12]
.L467:
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
	ldrb	w1, [x0,516]
	ldr	w2, [x0,544]
	cmp	w1, 9
	ldr	x1, [x0,536]
	str	w2, [x1]
	ldr	x1, [x0,536]
	ldr	w2, [x0,548]
	str	w2, [x1,4]
	ldr	x1, [x0,536]
	ldr	w2, [x0,552]
	bne	.L480
	str	w2, [x1,16]
	ldr	w2, [x0,556]
	str	w2, [x1,32]
	ldr	w2, [x0,560]
	str	w2, [x1,80]
	ldr	w2, [x0,564]
	str	w2, [x1,84]
	ldr	w2, [x0,568]
	str	w2, [x1,520]
	ldr	w0, [x0,572]
	str	w0, [x1,8]
	b	.L481
.L480:
	str	w2, [x1,8]
	ldr	w2, [x0,556]
	str	w2, [x1,12]
	ldr	w2, [x0,560]
	str	w2, [x1,304]
	ldr	w2, [x0,564]
	str	w2, [x1,308]
	ldr	w2, [x0,568]
	str	w2, [x1,336]
	ldr	w0, [x0,572]
	str	w0, [x1,344]
.L481:
	mov	x19, 0
	add	x21, x20, :lo12:.LANCHOR0
	mov	w23, 2
.L483:
	add	x0, x21, x19, lsl 3
	ldrb	w0, [x0,760]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L482
	uxtb	w22, w19
	mov	w0, w22
	bl	flash_reset
	add	x0, x21, 752
	strb	w23, [x19,x0]
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L482:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L483
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,672]
	cbz	w0, .L484
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,560]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L484:
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
	ldr	x0, [x19,536]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	w1, [x19,516]
	ldr	x0, [x19,536]
	cmp	w1, 9
	bne	.L491
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L492
.L491:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L492:
	ldr	w1, [x0]
	tbnz	x1, 9, .L493
	bl	wait_for_nand_flash_ready
	b	.L490
.L493:
	mov	w1, 1
	bl	nandc_irq_disable
.L490:
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
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	uxtb	w19, w0
	add	x0, x20, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	uxtb	w25, w1
	mov	w23, w2
	ldrb	w0, [x0,633]
	mov	w24, w3
	cmp	w0, w19
	bhi	.L496
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 604
	add	x1, x1, 736
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L496:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,633]
	cmp	w2, w19
	bls	.L497
	adrp	x0, .LANCHOR2
	add	x19, x1, x19, sxtw
	ldrb	w22, [x19,736]
	ldr	x19, [x1,536]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	x21, x22
	add	x22, x22, 8
	add	x22, x19, x22, lsl 8
	tbz	x0, 4, .L498
	adrp	x0, .LC43
	mov	w1, w21
	add	x0, x0, :lo12:.LC43
	mov	w2, w23
	mov	w3, w24
	bl	printk
.L498:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L499
	bl	zftl_flash_enter_slc_mode
	b	.L500
.L499:
	bl	zftl_flash_exit_slc_mode
.L500:
	ubfiz	x4, x21, 8, 8
	mov	w0, 96
	add	x19, x19, x4
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w23, 255
	str	w0, [x19,2052]
	lsr	w0, w23, 8
	str	w0, [x19,2052]
	lsr	w0, w23, 16
	str	w0, [x19,2052]
	ldrb	w0, [x20,793]
	mov	w20, 0
	cbnz	w0, .L501
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	and	w20, w0, w20
.L501:
	mov	w0, 96
	str	w0, [x19,2056]
	and	w0, w24, 255
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w24, w24, 16
	str	w24, [x19,2052]
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w19, 5
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w2, w0
	mov	w0, w21
	str	x2, [x29,88]
	bl	nandc_de_cs
	ldr	x2, [x29,88]
	and	w19, w2, w19
	orr	w19, w19, w20
	cbz	w19, .L502
	adrp	x0, .LC44
	mov	w1, w23
	add	x0, x0, :lo12:.LC44
	bl	printk
.L502:
	mov	w0, w19
.L497:
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
	ldrb	w19, [x1,744]
	mov	w23, w2
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ldrb	w0, [x1,633]
	sub	w19, w19, #1
	cmp	w0, w22
	and	w19, w19, w2
	bhi	.L512
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 653
	add	x1, x1, 768
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L512:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,633]
	cmp	w2, w22
	bls	.L513
	add	x0, x1, x22, sxtw
	ldrb	w24, [x0,736]
	adrp	x0, .LANCHOR2
	ldr	x26, [x1,536]
	mov	x21, x24
	add	x24, x24, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x24, x26, x24, lsl 8
	tbz	x0, 4, .L514
	adrp	x0, .LC45
	mov	w1, w21
	add	x0, x0, :lo12:.LC45
	mov	w2, w23
	mov	w3, w25
	bl	printk
.L514:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L515
	bl	zftl_flash_enter_slc_mode
	b	.L516
.L515:
	bl	zftl_flash_exit_slc_mode
.L516:
	ubfiz	x3, x21, 8, 8
	mov	w0, 96
	add	x3, x26, x3
	str	w0, [x3,2056]
	and	w0, w19, 255
	str	w0, [x3,2052]
	lsr	w0, w19, 8
	str	w0, [x3,2052]
	lsr	w19, w19, 16
	str	w19, [x3,2052]
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
	beq	.L517
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC46
	add	x0, x0, :lo12:.LC46
	mov	w1, w22
	ldrh	w2, [x20,2]
	udiv	w2, w23, w2
	bl	printk
	mov	w3, -1
.L517:
	mov	w0, w3
.L513:
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
.L527:
	ldrb	w0, [x23,633]
	cmp	w0, w19
	bls	.L531
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,736]
	mov	w22, 0
.L528:
	cmp	w20, w22, uxth
	bls	.L532
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L528
.L532:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L527
.L531:
	adrp	x0, .LC47
	mov	w1, 0
	add	x0, x0, :lo12:.LC47
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
	ldrb	w2, [x4,516]
	cmp	w2, 9
	bne	.L535
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
	ldrb	w1, [x4,673]
	cbz	w1, .L536
	ldrb	w1, [x4,720]
	cbz	w1, .L536
	ldr	w1, [x29,56]
	orr	w1, w1, 512
	str	w1, [x29,56]
.L536:
	ldr	w1, [x29,56]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	str	x3, [x20,696]
	str	x0, [x20,688]
	bl	rknand_dma_map_single
	str	w0, [x20,704]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,696]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	bl	rknand_dma_map_single
	str	w0, [x20,708]
	mov	w1, 1
	str	w1, [x20,712]
	ldr	x1, [x20,536]
	cmp	w21, wzr
	ldr	w2, [x20,704]
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
	ldrh	w2, [x20,722]
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
	b	.L534
.L535:
	ldr	x2, [x4,536]
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
	cbz	w21, .L538
	ldrb	w2, [x4,724]
	ubfx	x1, x1, 1, 7
	mov	w5, 128
	cmp	w2, 25
	mov	w2, 64
	csel	w2, w2, w5, cc
	add	x1, x3, x1, lsl 2
	mov	w5, 0
.L540:
	cmp	x3, x1
	add	w7, w5, w2
	beq	.L542
	ldrh	w6, [x3,2]
	and	x5, x5, 4294967292
	ldr	x8, [x4,680]
	ldrh	w9, [x3],4
	orr	w6, w9, w6, lsl 16
	str	w6, [x8,x5]
	mov	w5, w7
	b	.L540
.L538:
	ldr	x1, [x4,680]
	str	w6, [x1]
.L542:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ldr	x1, [x20,680]
	str	x1, [x20,696]
	ldr	w1, [x29,56]
	str	x0, [x20,688]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_map_single
	str	w0, [x20,704]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,696]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
	bl	rknand_dma_map_single
	str	w0, [x20,708]
	ldr	x1, [x20,536]
	mov	w0, 1
	str	w0, [x20,712]
	mov	w2, 16
	ldr	w0, [x20,704]
	cmp	w21, wzr
	str	w0, [x1,20]
	ldr	w0, [x20,708]
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
.L534:
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
	add	x1, x1, 792
	ldrh	w1, [x1,w0,uxtw 1]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	orr	w2, w1, -1073741824
	ldrb	w3, [x0,720]
	cmp	w3, wzr
	csel	w1, w2, w1, ne
	ldrb	w2, [x0,516]
	ldr	x0, [x0,536]
	cmp	w2, 9
	bne	.L552
	str	w1, [x0,520]
	b	.L550
.L552:
	str	w1, [x0,336]
.L550:
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
	cbz	w0, .L557
	ldrb	w0, [x1,634]
	cbnz	w0, .L557
	ldrb	w1, [x1,516]
	cmp	w1, 9
	beq	.L557
	bl	zftl_flash_exit_slc_mode
.L557:
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	hynix_reconfig_rr_para
	ldrb	w0, [x20,672]
	cbz	w0, .L558
	ldrb	w0, [x20,792]
	tbz	x0, 0, .L558
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,672]
.L558:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,720]
	cbz	w0, .L559
	mov	w0, 0
	strb	wzr, [x19,720]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,720]
.L559:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+720
	strb	w0, [x1,#:lo12:.LANCHOR0+720]
	ret
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	adrp	x1, .LANCHOR0+536
	uxtb	x0, w0
	add	x0, x0, 8
	ldr	x1, [x1,#:lo12:.LANCHOR0+536]
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
	add	x19, x19, 800
	mov	x22, x0
.L578:
	add	x21, x22, :lo12:.LANCHOR0
	uxtb	w0, w20
	add	w1, w0, 1
	strb	w0, [x19,1]
	strb	w1, [x19]
	add	w20, w20, 1
	ldrb	w0, [x21,2848]
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
	bne	.L578
	strb	w20, [x21,2850]
	mov	w0, -1
	strb	wzr, [x21,2849]
	strb	w0, [x21,2784]
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
	add	x1, x1, 800
.L581:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L581
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0,2849]
	strb	w1, [x0,2784]
	strb	w2, [x0,2850]
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
	bne	.L589
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L585
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 74
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L585:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L583
.L589:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 800
.L590:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L590
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L587
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 81
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L587:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 800
	strb	w1, [x0,x20]
.L583:
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
	add	x0, x0, 2851
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
	cbz	w1, .L593
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L593
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2849
	bl	buf_add_tail
	b	.L594
.L593:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2849]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2849]
.L594:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2850]
	add	w0, w0, 1
	strb	w0, [x19,2850]
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
	ldrb	w0, [x20,2850]
	cbz	w0, .L602
.L605:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 800
	ldrb	w2, [x1,2849]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L603
	b	.L604
.L602:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 121
	add	x1, x1, 1064
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w1, [x20,2850]
	cbnz	w1, .L605
.L607:
	mov	x0, 0
	b	.L606
.L603:
	ldrb	w1, [x1,2850]
	cmp	w1, 1
	beq	.L607
.L604:
	add	x1, x19, :lo12:.LANCHOR0
	lsl	x2, x2, 6
	add	x4, x1, 800
	add	x3, x4, x2
	ldrb	w5, [x4,x2]
	strb	w5, [x1,2849]
	ldrb	w5, [x1,2850]
	strh	wzr, [x3,50]
	sub	w5, w5, #1
	strb	w5, [x1,2850]
	mov	w1, 1
	strb	w1, [x3,2]
	mov	w1, -1
	str	xzr, [x3,16]
	strb	w1, [x4,x2]
	mov	w1, -1
	strb	wzr, [x3,56]
	str	w1, [x3,36]
	strb	wzr, [x3,57]
.L606:
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
	bne	.L614
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L618
.L614:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 800
.L616:
	cmp	w2, 255
	beq	.L619
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L616
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L618:
	mov	w0, 1
	b	.L613
.L619:
	mov	w0, 0
.L613:
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
	ldrb	w0, [x0,2850]
	cbnz	w0, .L621
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 172
	add	x1, x1, 1080
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L621:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2850]
	cbz	w0, .L620
	add	x0, x19, 2849
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L620
	ldrb	w0, [x19,2850]
	sub	w0, w0, #1
	strb	w0, [x19,2850]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L620:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_remove_free, .-buf_remove_free
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
	adrp	x0, .LANCHOR0+728
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+728]
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
	adrp	x0, .LC48
	add	x0, x0, :lo12:.LC48
	uxth	w20, w20
	add	w20, w20, 31
	bl	printk
	asr	w20, w20, 5
	adrp	x0, .LANCHOR0+728
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+728]
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
	bne	.L628
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L628:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L630:
	ldrb	w2, [x0]
	cbz	w2, .L637
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L631
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L632
.L631:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L633
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L632
.L633:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L637
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L632:
	add	x0, x0, 1
	b	.L630
.L637:
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
	adrp	x0, .LC49
	add	x3, x3, :lo12:.LANCHOR1
	add	x29, sp, 0
	mov	w1, 292
	add	x3, x3, 1096
	mov	x4, x2
	add	x0, x0, :lo12:.LC49
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
	adrp	x1, .LC50
	add	x7, x7, 8
	add	x21, x22, :lo12:.LANCHOR0
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC50
	ldrb	w5, [x7,4]
	mov	x0, x19
	ldrb	w6, [x7,5]
	ldrb	w3, [x7,2]
	ldrb	w4, [x7,3]
	ldrb	w2, [x7,1]
	ldrb	w7, [x7,6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,520]
	adrp	x1, .LC51
	mov	x0, x19
	add	x1, x1, :lo12:.LC51
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,2852]
	adrp	x1, .LC52
	mov	x0, x19
	add	x1, x1, :lo12:.LC52
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x1, .LC53
	mov	x0, x19
	add	x1, x1, :lo12:.LC53
	add	x19, x19, 10
	bl	strcpy
	ldr	w2, [x21,2856]
	adrp	x1, .LC54
	mov	x0, x19
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,520]
	adrp	x1, .LC55
	mov	x0, x19
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC56
	mov	x0, x19
	add	x1, x1, :lo12:.LC56
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2860]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2862]
	adrp	x1, .LC59
	mov	x0, x19
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2864]
	adrp	x1, .LC60
	mov	x0, x19
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2866]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2868]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2870]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w4, [x21,2873]
	adrp	x1, .LC64
	ldrb	w2, [x21,2850]
	mov	x0, x19
	ldrb	w3, [x21,2872]
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x3, [x21,2880]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2880]
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2880]
	adrp	x1, .LC67
	add	x1, x1, :lo12:.LC67
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,608]
	mov	w2, 10
	adrp	x1, .LC70
	add	x1, x1, :lo12:.LC70
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w2, [x22,#:lo12:.LANCHOR0]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC73
	mov	x0, x19
	add	x1, x1, :lo12:.LC73
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC76
	mov	x0, x19
	add	x1, x1, :lo12:.LC76
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2880]
	adrp	x1, .LC77
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC78
	mov	x0, x19
	add	x1, x1, :lo12:.LC78
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC79
	mov	x0, x19
	add	x1, x1, :lo12:.LC79
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC80
	mov	x0, x19
	add	x1, x1, :lo12:.LC80
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2880]
	adrp	x1, .LC81
	mov	x0, x19
	add	x1, x1, :lo12:.LC81
	ldrh	w2, [x6,74]
	ldr	w3, [x6,84]
	ldrh	w4, [x6,88]
	ldrh	w5, [x6,92]
	ldrh	w6, [x6,96]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2880]
	adrp	x1, .LC82
	mov	x0, x19
	add	x1, x1, :lo12:.LC82
	ldr	w3, [x6,80]
	ldrh	w4, [x6,90]
	ldrh	w5, [x6,94]
	ldrh	w2, [x6,72]
	ldrh	w6, [x6,98]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2888]
	adrp	x1, .LC83
	mov	x0, x19
	add	x1, x1, :lo12:.LC83
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w6, [x21,2900]
	adrp	x1, .LC84
	ldrh	w2, [x21,2890]
	mov	x0, x19
	ldrh	w3, [x21,2892]
	add	x1, x1, :lo12:.LC84
	ldrh	w4, [x21,2894]
	ldrh	w5, [x21,2896]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x5, [x21,608]
	adrp	x1, .LC85
	mov	x0, x19
	add	x1, x1, :lo12:.LC85
	ldrh	w2, [x5,584]
	ldrh	w3, [x5,586]
	ldrh	w4, [x5,588]
	ldrh	w5, [x5,590]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,608]
	adrp	x1, .LC86
	ldr	x5, [x21,2880]
	mov	x0, x19
	add	x1, x1, :lo12:.LC86
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
	add	x0, x19, 2904
	bl	buf_add_tail
	ldrb	w0, [x19,2872]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,2872]
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
	tbz	x0, 8, .L645
	add	x7, x22, :lo12:.LANCHOR0
	uxtw	x5, w21
	mov	w1, w21
	mov	w2, w23
	mov	w3, w20
	ldr	x0, [x7,584]
	ldr	x6, [x7,600]
	add	x0, x0, x5, lsl 2
	ldrh	w5, [x6,x5,lsl 1]
	ldrb	w4, [x0,2]
	adrp	x0, .LC87
	ldrh	w6, [x7,2968]
	add	x0, x0, :lo12:.LC87
	ldrh	w7, [x7,2894]
	ubfx	x4, x4, 5, 3
	bl	printk
.L645:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,576]
	cmp	w0, w21
	bhi	.L646
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 454
	add	x1, x1, 1352
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L646:
	add	x5, x22, :lo12:.LANCHOR0
	ldrh	w0, [x5,576]
	cmp	w0, w21
	bhi	.L647
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L648
	adrp	x0, .LC88
	mov	w1, w21
	add	x0, x0, :lo12:.LC88
	mov	w2, w23
	mov	w3, w20
	bl	printk
.L648:
	bl	dump_stack
	b	.L667
.L647:
	uxtw	x0, w21
	ldr	x1, [x5,600]
	ldrh	w4, [x1,x0,lsl 1]
	ldr	x1, [x5,584]
	add	x0, x1, x0, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L650
	cbz	w4, .L649
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 462
	add	x1, x1, 1352
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L649
.L650:
	ldrh	w1, [x5,2912]
	add	x8, x5, 2912
	mov	w19, 0
	cmp	w1, w21
	beq	.L649
	ldr	x7, [x5,608]
	ldrh	w1, [x7,48]
	cmp	w1, w21
	beq	.L649
	ldrh	w1, [x7,16]
	cmp	w1, w21
	beq	.L649
	ldrh	w1, [x7,80]
	cmp	w1, w21
	beq	.L649
	ldrh	w5, [x8,56]
	mov	x6, 0
.L651:
	cmp	w5, w6
	bls	.L685
	add	x6, x6, 1
	add	x1, x8, x6, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L651
	b	.L667
.L685:
	cbnz	w23, .L653
	add	x8, x22, :lo12:.LANCHOR0
	mov	w19, w23
	ldrh	w9, [x8,3512]
	cmp	w9, w21
	beq	.L649
	mov	x6, 0
.L655:
	add	x1, x8, 3520
	ldrh	w1, [x6,x1]
	cmp	w21, w1
	bne	.L654
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x1, 8, .L649
	ldrb	w2, [x0,2]
	adrp	x0, .LC89
	mov	w3, w4
	mov	w1, w21
	ubfx	x2, x2, 5, 3
	mov	w4, w21
	mov	w5, w9
	add	x0, x0, :lo12:.LC89
	bl	printk
	b	.L649
.L654:
	add	x6, x6, 2
	cmp	x6, 16
	bne	.L655
	sxtw	x6, w20
	add	x19, x7, x6, lsl 7
	add	x19, x19, 136
	b	.L657
.L653:
	add	x19, x7, 392
.L657:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L658
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w3, [x0,2]
	adrp	x0, .LC90
	mov	w1, w21
	add	x0, x0, :lo12:.LC90
	mov	w2, w23
	ldrh	w6, [x6,2894]
	ubfx	x3, x3, 5, 3
	bl	printk
.L658:
	mov	x4, x19
	add	x0, x19, 128
	mov	w1, 65535
.L661:
	ldrh	w2, [x4]
	cmp	w2, w1
	bne	.L659
	strh	w21, [x4]
	add	x22, x22, :lo12:.LANCHOR0
	cbz	w23, .L660
	ldr	x1, [x22,608]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L684
.L660:
	ldr	x0, [x22,608]
	add	x20, x0, x20, uxth 1
	ldrh	w0, [x20,120]
	add	w0, w0, 1
	strh	w0, [x20,120]
	b	.L684
.L659:
	add	x4, x4, 2
	cmp	x4, x0
	bne	.L661
.L684:
	mov	w19, 1
	b	.L649
.L667:
	mov	w19, 0
.L649:
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
	mov	w3, w0
	mov	w1, 24
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x23, [sp,48]
	add	x4, x21, :lo12:.LANCHOR0
	mov	w23, 1
	stp	x19, x20, [sp,16]
	add	x20, x4, 2912
	ldrb	w19, [x4,744]
	ldrh	w0, [x4,3536]
	sub	w19, w1, w19
	ldr	w1, [x4,3496]
	sub	w19, w19, w0
	lsr	w2, w3, w0
	lsl	w19, w23, w19
	ldrb	w0, [x4,3538]
	sub	w19, w19, #1
	uxth	w22, w2
	and	w19, w19, w2
	mov	w2, w22
	udiv	w19, w19, w0
	adrp	x0, .LC91
	add	x0, x0, :lo12:.LC91
	bl	printk
	mov	w1, w23
	mov	w2, 0
	uxth	w19, w19
	mov	w0, w19
	bl	gc_add_sblk
	ldr	w0, [x20,584]
	mov	w1, 0
	mov	x4, x20
.L687:
	cmp	w1, w0
	bcs	.L690
	add	x2, x4, x1, sxtw 1
	ldrh	w2, [x2,588]
	cmp	w2, w22
	beq	.L688
	add	w1, w1, 1
	uxth	w1, w1
	b	.L687
.L690:
	cmp	w0, 5
	bhi	.L688
	add	x21, x21, :lo12:.LANCHOR0
	add	w1, w0, 1
	add	x21, x21, 2912
	str	w1, [x21,584]
	add	x21, x21, x0, uxtw 1
	strh	w22, [x21,588]
.L688:
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
	adrp	x1, .LANCHOR0+3544
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3544]
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
	adrp	x21, .LANCHOR0
	adrp	x22, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	add	x23, x21, 800
	add	x22, x22, 1368
.L693:
	ldrb	w0, [x21,3552]
	adrp	x19, .LANCHOR0
	cmp	w0, 255
	beq	.L722
	sxtw	x25, w0
	lsl	x0, x25, 6
	add	x2, x23, x0
	ldrb	w0, [x23,x0]
	ldr	w1, [x2,52]
	strb	w0, [x21,3552]
	ldrh	w24, [x2,48]
	cbz	w1, .L694
	ldr	w2, [x2,40]
	adrp	x0, .LC92
	mov	w3, 1
	add	x0, x0, :lo12:.LC92
	strh	w3, [x21,3488]
	str	w2, [x21,3492]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 805
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L694:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,3553]
	cmp	w1, 3
	bne	.L695
	ldrb	w1, [x0,756]
	cbnz	w1, .L695
	ldr	x0, [x0,608]
	ldrb	w20, [x0,89]
	add	w20, w20, w20, lsl 1
	and	w20, w20, 1023
	b	.L696
.L695:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w20, 1
	ldrb	w1, [x0,3554]
	cbz	w1, .L696
	ldrb	w0, [x0,3555]
	mov	w20, 2
	cmp	w0, wzr
	csinc	w20, w20, wzr, ne
.L696:
	adrp	x0, .LANCHOR2
	uxth	x26, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L697
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w24
	add	x3, x0, 800
	add	x3, x3, x25, lsl 6
	ldr	x4, [x0,3560]
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	ldrb	w2, [x3,1]
	ldrb	w4, [x4,x26]
	ldrh	w3, [x3,48]
	bl	printk
.L697:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3560]
	add	x0, x0, x25, lsl 6
	ldrb	w1, [x1,x26]
	ldrb	w0, [x0,801]
	cmp	w1, w0
	beq	.L698
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 814
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L698:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, x25, lsl 6
	ldrb	w0, [x0,861]
	cmp	w0, 3
	beq	.L699
	ldrb	w0, [x1,3553]
	cmp	w0, 3
	bne	.L699
	ldrb	w0, [x1,3568]
	cbnz	w0, .L699
	ldrb	w0, [x1,756]
	cbnz	w0, .L699
	ldrb	w0, [x1,3554]
	cbz	w0, .L700
	ldrb	w0, [x1,3555]
	cbnz	w0, .L699
.L700:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	x19, x19, 800
	ldr	x2, [x19,2760]
	b	.L701
.L699:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w26, -1
	add	x27, x19, 800
	strh	w24, [x19,3484]
.L702:
	cmp	w20, w25, uxth
	bls	.L693
	add	w1, w25, w24
	ldr	x2, [x19,3560]
	add	w25, w25, 1
	ldrb	w0, [x2,x1]
	strb	w26, [x2,x1]
	sbfiz	x0, x0, 6, 32
	add	x0, x27, x0
	strb	wzr, [x0,61]
	bl	zbuf_free
	ldrb	w0, [x19,2919]
	sub	w0, w0, #1
	strb	w0, [x19,2919]
	b	.L702
.L701:
	cmp	w20, w0, uxth
	bls	.L693
	add	w1, w0, w24
	add	w0, w0, 1
	ldrb	w1, [x2,x1]
	add	x1, x19, x1, lsl 6
	strb	wzr, [x1,61]
	b	.L701
.L722:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	adrp	x3, .LANCHOR0
	add	x0, x3, :lo12:.LANCHOR0
	ldr	x1, [x0,608]
	ldrb	w0, [x0,3569]
	ldrh	w4, [x1,124]
	cbz	w4, .L724
	add	x1, x1, 392
	mov	w2, 1
	b	.L725
.L724:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L726
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L725:
	add	x4, x1, 128
	mov	w5, 65535
.L729:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L727
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L728
	ldr	x2, [x3,608]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L726
.L728:
	ldrb	w1, [x3,3569]
	ldr	x2, [x3,608]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L726
.L727:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L729
.L726:
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
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w3, [x2,2919]
	cbz	w3, .L734
	ldrb	w3, [x2,2850]
	cmp	w3, 1
	bhi	.L734
	ldrh	w4, [x2,3570]
	mov	x20, x2
	ldrb	w0, [x2,3572]
	add	x2, x2, 800
	ldrh	w1, [x2,2684]
	mul	w0, w4, w0
	add	w3, w1, 24
	ldr	x4, [x2,2760]
	cmp	w0, w3
	csel	w3, w0, w3, ls
.L735:
	cmp	w1, w3
	bcs	.L748
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L736
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L736
	str	x1, [x29,56]
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L737
	ldr	x2, [x20,3560]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	ldrb	w2, [x2,x21]
	bl	printk
.L737:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3560]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2919]
	sub	w0, w0, #1
	strb	w0, [x19,2919]
	mov	w0, 1
	b	.L734
.L736:
	add	w1, w1, 1
	b	.L735
.L748:
	mov	w0, 0
.L734:
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
	adrp	x0, .LC95
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC95
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,2912]
	ldrh	w2, [x6,2914]
	ldrh	w3, [x6,3482]
	ldrb	w4, [x6,2850]
	ldrb	w5, [x6,2919]
	ldrh	w6, [x6,2888]
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
	bne	.L751
	adrp	x0, .LANCHOR0+3576
	ldr	x0, [x0,#:lo12:.LANCHOR0+3576]
	b	.L754
.L751:
	cmp	w1, 2
	bne	.L753
	mov	w0, w2
	bl	zftl_get_gc_node.part.11
	b	.L755
.L753:
	adrp	x0, .LANCHOR0+3584
	ldr	x0, [x0,#:lo12:.LANCHOR0+3584]
.L754:
	mov	w1, w2
	bl	_list_get_gc_head_node.isra.4
.L755:
	uxth	w0, w0
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node, .-zftl_get_gc_node
	.align	2
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w24, w1
	uxtb	w22, w2
	ldr	x1, [x0,608]
	add	x1, x1, x21, sxtw 1
	ldrh	w19, [x1,120]
	cbnz	w19, .L803
	cbnz	w21, .L759
	adrp	x26, .LC96
	strh	w21, [x0,3592]
	mov	w19, w21
	mov	w25, w21
	uxth	w23, w22
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x26, x26, :lo12:.LC96
.L760:
	cmp	w25, w23
	bcs	.L765
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, 3
	str	x3, [x29,96]
	ldrh	w2, [x3,3592]
	str	x2, [x29,104]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,104]
	cmp	w1, w27
	ldr	x3, [x29,96]
	mov	w4, w1
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x3,3592]
	beq	.L761
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	uxtw	x5, w1
	tbz	x0, 8, .L762
	ldr	x3, [x3,600]
	mov	x0, x26
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w3, [x3,x5,lsl 1]
	bl	printk
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
.L762:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x5,lsl 1]
	ldrh	w0, [x1,2894]
	cmp	w2, w0
	bcs	.L763
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L764
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L764
	b	.L765
.L763:
	strh	wzr, [x1,3592]
	b	.L765
.L761:
	strh	wzr, [x3,3592]
	b	.L765
.L764:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L760
.L765:
	tbz	x24, 1, .L767
	mov	w26, 0
	mov	w27, 65535
.L768:
	cmp	w26, w23
	beq	.L772
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w28, [x25,3594]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x25,3594]
	cmp	w1, w27
	beq	.L769
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x25,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x25,2896]
	cmp	w2, w1
	bcs	.L769
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L771
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L771
	b	.L774
.L769:
	strh	wzr, [x25,3594]
	b	.L772
.L771:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L768
.L772:
	cmp	w19, w23
	bcs	.L774
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3570]
	ldrb	w4, [x0,3572]
	ldrh	w2, [x0,3596]
	ldrh	w3, [x0,2896]
	mul	w1, w1, w4
	sub	w1, w1, w2, lsr 2
	cmp	w3, w1
	bge	.L767
	add	w2, w3, w2, lsr 3
	strh	w2, [x0,2896]
	b	.L767
.L774:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2896]
	ldrh	w0, [x1,3596]
	cmp	w2, w0
	bls	.L767
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,2896]
.L767:
	tbz	x24, 0, .L775
	cmp	w19, w23
	bcs	.L775
	mov	w25, 0
	mov	w27, 65535
.L776:
	cmp	w25, w23
	beq	.L779
	add	x28, x20, :lo12:.LANCHOR0
	ldrh	w26, [x28,3598]
	mov	w0, w26
	add	w26, w26, 1
	bl	zftl_get_gc_node.part.11
	strh	w26, [x28,3598]
	cmp	w27, w0, uxth
	beq	.L777
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L778
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bcc	.L778
	b	.L781
.L777:
	strh	wzr, [x28,3598]
	b	.L779
.L778:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L776
.L779:
	cmp	w19, w23
	bcs	.L781
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,2894]
	ldrh	w0, [x20,3596]
	cmp	w1, w0
	bcs	.L775
	add	w0, w1, w0, lsr 3
	b	.L856
.L781:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,3596]
	ldrh	w1, [x20,2894]
	cmp	w1, w0, lsr 1
	bls	.L775
	sub	w0, w1, w0, lsr 3
.L856:
	strh	w0, [x20,2894]
	b	.L775
.L759:
	and	w1, w24, 1
	strh	w19, [x0,3486]
	strh	w19, [x0,3600]
	str	w1, [x29,104]
	tbz	x24, 0, .L782
	ldrh	w2, [x0,3602]
	ldrh	w1, [x0,2868]
	cmp	w1, w2, lsr 2
	bcc	.L783
	ldrh	w0, [x0,2866]
	cmp	w0, w1
	bls	.L784
.L783:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w19, 0
	ldrh	w0, [x23,2866]
	lsr	w0, w0, 2
	strh	w0, [x23,3598]
	mov	w0, 0
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L785
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x23,600]
	ldrh	w2, [x23,3596]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 2
	bcs	.L785
	mov	w1, w19
	mov	w2, w21
	strh	w19, [x23,3598]
	bl	gc_add_sblk
	cmp	w0, w19
	cset	w19, ne
.L785:
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L787:
	add	x25, x20, :lo12:.LANCHOR0
	ldrh	w28, [x25,3598]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node.part.11
	strh	w28, [x25,3598]
	cmp	w26, w0, uxth
	beq	.L784
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L786
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L784
.L786:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L787
.L784:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3602]
	ldrh	w1, [x0,2868]
	cmp	w1, w2, lsr 3
	bhi	.L804
	ldrh	w0, [x0,2866]
	add	w0, w0, 8
	cmp	w1, w0
	bgt	.L804
.L782:
	tbz	x24, 1, .L790
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 0
	mov	w27, 65535
	uxth	w28, w22
	strh	wzr, [x0,3594]
.L795:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w2, [x23,3594]
	str	x2, [x29,96]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,96]
	cmp	w1, w27
	add	w2, w2, 1
	strh	w2, [x23,3594]
	beq	.L791
	cmp	w22, 1
	bne	.L792
	ldrh	w3, [x23,3570]
	ldrb	w2, [x23,3572]
	mul	w2, w2, w3
	ldrh	w3, [x23,3596]
	sub	w2, w2, w3, lsr 3
	strh	w2, [x23,2890]
.L792:
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,2890]
	cmp	w3, w1
	bcs	.L793
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L794
	add	w19, w19, 1
	add	w26, w26, 1
	uxth	w19, w19
	uxth	w26, w26
	cmp	w19, w28
	bcc	.L794
	b	.L791
.L793:
	strh	wzr, [x2,3594]
	b	.L791
.L794:
	sub	w0, w25, #1
	uxth	w25, w0
	cbnz	w25, .L795
.L791:
	cmp	w19, w22
	bcc	.L796
	cbnz	w26, .L797
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,2870]
	ldrh	w0, [x0,3604]
	cmp	w1, w0
	bls	.L797
.L796:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3570]
	ldrb	w4, [x0,3572]
	ldrh	w3, [x0,3596]
	ldrh	w2, [x0,2890]
	mul	w1, w1, w4
	lsr	w3, w3, 3
	sub	w1, w1, w3
	cmp	w2, w1
	bge	.L790
	add	w2, w3, w2
	strh	w2, [x0,2890]
	b	.L790
.L797:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3596]
	ldrb	w2, [x0,3572]
	ldrh	w3, [x0,2890]
	mul	w2, w1, w2
	cmp	w3, w2
	ble	.L790
	sub	w1, w3, w1, lsr 3
	strh	w1, [x0,2890]
.L790:
	ldr	w0, [x29,104]
	cbz	w0, .L775
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3592]
.L801:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x23,3592]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x23,3592]
	cmp	w1, w26
	beq	.L798
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x23,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 2
	bls	.L799
	ldrh	w2, [x23,2892]
	cmp	w2, w1
	bls	.L799
	ldrh	w2, [x23,3602]
	ldrh	w1, [x23,2868]
	cmp	w1, w2, lsr 1
	bls	.L798
.L799:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L800
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L798
.L800:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L801
.L798:
	cmp	w19, w22
	add	x20, x20, :lo12:.LANCHOR0
	bcs	.L802
	ldrh	w0, [x20,3596]
	ldrh	w1, [x20,2892]
	cmp	w1, w0, lsr 1
	bls	.L775
	sub	w0, w1, w0, lsr 3
	strh	w0, [x20,2892]
	b	.L775
.L802:
	ldrh	w1, [x20,3596]
	ldrb	w0, [x20,3572]
	ldrh	w2, [x20,2892]
	mul	w0, w1, w0
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L775
	add	w1, w2, w1, lsr 3
	strh	w1, [x20,2892]
.L775:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L803
	adrp	x0, .LC97
	mov	w1, w21
	add	x0, x0, :lo12:.LC97
	mov	w2, w24
	mov	w3, w19
	mov	w4, w22
	bl	printk
.L803:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L804:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3592]
.L789:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x25,3592]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	strh	w28, [x25,3592]
	cmp	w26, w0, uxth
	beq	.L782
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L788
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L782
.L788:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L789
	b	.L782
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_insert_free_list
	.type	zftl_insert_free_list, %function
zftl_insert_free_list:
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,584]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L858
	add	x0, x2, 3608
	add	x2, x2, 2860
	b	.L861
.L858:
	cmp	w0, 16
	bne	.L860
	add	x0, x2, 3616
	add	x2, x2, 2862
	b	.L861
.L860:
	add	x0, x2, 3624
	add	x2, x2, 2864
.L861:
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
	ldr	x0, [x2,584]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L863
	add	x0, x2, 616
	add	x2, x2, 2866
	b	.L866
.L863:
	cmp	w3, 96
	bne	.L865
	add	x0, x2, 3584
	add	x2, x2, 2868
	b	.L866
.L865:
	cmp	w3, 160
	bne	.L862
	add	x0, x2, 3576
	add	x2, x2, 2870
.L866:
	bl	_insert_data_list
.L862:
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
	add	x23, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	uxth	w22, w1
	ldr	x0, [x23,608]
	ldrh	w19, [x0,588]
	cbnz	w21, .L868
	mov	w0, 65535
	cmp	w19, w0
	beq	.L868
	adrp	x0, .LC98
	mov	w1, w19
	add	x0, x0, :lo12:.LC98
	bl	printk
	ldr	x0, [x23,608]
	mov	w1, -1
	strh	w1, [x0,588]
	b	.L869
.L868:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w3, [x0,2862]
	ldrh	w1, [x0,2864]
	cmp	w3, w1
	bls	.L870
	mov	w1, 0
	mov	x2, x0
	lsr	w3, w3, 3
	cmp	w21, w1
	csel	w1, w3, w1, ne
	add	x0, x0, 3616
	add	x2, x2, 2862
	b	.L888
.L870:
	lsr	w1, w1, 2
	cbz	w21, .L874
	mov	w1, 7
	mul	w3, w3, w1
	mov	w1, 8
	sdiv	w3, w3, w1
	uxth	w1, w3
.L874:
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x2, 3624
	add	x2, x2, 2864
.L888:
	bl	_list_pop_index_node
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L875
	add	x5, x20, :lo12:.LANCHOR0
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	mov	w1, w19
	mov	w2, w22
	ldr	x3, [x5,3608]
	ldrh	w4, [x5,2860]
	ldrh	w5, [x5,2864]
	bl	printk
.L875:
	cbz	w21, .L869
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L869
	add	x20, x20, :lo12:.LANCHOR0
	uxtw	x6, w19
	lsl	x0, x6, 2
	ldr	x1, [x20,584]
	ldr	x7, [x20,600]
	add	x2, x1, x0
	ldr	w4, [x1,x0]
	ldrh	w5, [x1,x0]
	adrp	x0, .LC100
	ldrb	w3, [x2,2]
	add	x0, x0, :lo12:.LC100
	ldrh	w6, [x7,x6,lsl 1]
	mov	w1, w19
	ubfx	x2, x3, 5, 3
	ubfx	x4, x4, 11, 8
	ubfx	x3, x3, 3, 2
	and	w5, w5, 2047
	bl	printk
.L869:
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
	adrp	x19, .LANCHOR0
	bne	.L890
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2862]
	ldrh	w1, [x0,2864]
	cmp	w2, w1
	bcc	.L891
	ldrh	w0, [x0,2860]
	cmp	w0, w2
	bls	.L892
	cbz	w1, .L892
.L891:
	add	x2, x19, :lo12:.LANCHOR0
	lsr	w1, w1, 1
	add	x0, x2, 3624
	b	.L920
.L892:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x2, 3616
	add	x2, x2, 2862
	b	.L919
.L890:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w21, 1
	ldr	x0, [x22,608]
	ldrh	w20, [x0,590]
	beq	.L894
	mov	w0, 65535
	cmp	w20, w0
	beq	.L894
	adrp	x0, .LC101
	mov	w1, w20
	add	x0, x0, :lo12:.LC101
	bl	printk
	ldr	x0, [x22,608]
	mov	w1, -1
	strh	w1, [x0,590]
	b	.L895
.L894:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2860]
	ldrh	w1, [x2,2864]
	cmp	w0, w1
	bcc	.L896
	ldrh	w2, [x2,2862]
	cmp	w2, w0
	bls	.L897
	cbz	w1, .L897
.L896:
	lsr	w1, w1, 1
	cmp	w21, 1
	csel	w3, w1, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3624
	mov	w1, w3
.L920:
	add	x2, x2, 2864
	b	.L919
.L897:
	lsr	w0, w0, 1
	cmp	w21, 1
	csel	w3, w0, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3608
	mov	w1, w3
	add	x2, x2, 2860
.L919:
	bl	_list_pop_index_node
	uxth	w0, w0
	mov	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L895
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	mov	w1, w20
	mov	w2, w21
	ldr	x3, [x19,3608]
	ldrh	w4, [x19,2860]
	ldrh	w5, [x19,2864]
	bl	printk
.L895:
	mov	w0, w20
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
	ldr	x0, [x2,584]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L922
	add	x0, x2, 616
	add	x2, x2, 2866
	b	.L925
.L922:
	cmp	w3, 96
	bne	.L924
	add	x0, x2, 3584
	add	x2, x2, 2868
	b	.L925
.L924:
	cmp	w3, 160
	bne	.L921
	add	x0, x2, 3576
	add	x2, x2, 2870
.L925:
	bl	_list_remove_node
.L921:
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
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x2,584]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L927
	add	x0, x2, 3608
	add	x2, x2, 2860
	b	.L930
.L927:
	cmp	w0, 16
	bne	.L929
	add	x0, x2, 3616
	add	x2, x2, 2862
	b	.L930
.L929:
	add	x0, x2, 3624
	add	x2, x2, 2864
.L930:
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
	ldr	x0, [x2,584]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L932
	add	x0, x2, 616
	add	x2, x2, 2866
	b	.L935
.L932:
	cmp	w3, 96
	bne	.L934
	add	x0, x2, 3584
	add	x2, x2, 2868
	b	.L935
.L934:
	cmp	w3, 160
	bne	.L931
	add	x0, x2, 3576
	add	x2, x2, 2870
.L935:
	bl	_list_update_data_list
.L931:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_list_update_data_list, .-zftl_list_update_data_list
	.align	2
	.global	print_list_info
	.type	print_list_info, %function
print_list_info:
	sub	sp, sp, #112
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	mov	x19, x0
	adrp	x0, .LC102
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	add	x0, x0, :lo12:.LC102
	ldrh	w2, [x1]
	ldr	x1, [x19]
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L936
	mov	x22, -6148914691236517206
	adrp	x23, .LC103
	mov	w20, 0
	adrp	x25, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC103
	mov	w26, 65535
	mov	w24, 6
.L939:
	add	x21, x25, :lo12:.LANCHOR0
	ldrh	w3, [x19]
	ldrh	w4, [x19,2]
	ldrh	w5, [x19,4]
	ldr	x2, [x21,528]
	ldr	x1, [x21,584]
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
	ldr	x0, [x21,600]
	ldrh	w0, [x0,x8,lsl 1]
	str	w0, [sp,16]
	mov	x0, x23
	bl	printk
	ldrh	w19, [x19]
	cmp	w19, w26
	beq	.L936
	ldr	x0, [x21,528]
	add	w20, w20, 1
	umull	x19, w19, w24
	uxth	w20, w20
	add	x19, x0, x19
	ldrh	w0, [x21,3632]
	cmp	w0, w20
	bcs	.L939
.L936:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 112
	ret
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.type	dump_all_list_info, %function
dump_all_list_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3608
	add	x1, x19, 2860
	bl	print_list_info
	add	x0, x19, 3616
	add	x1, x19, 2862
	bl	print_list_info
	add	x0, x19, 3624
	add	x1, x19, 2864
	bl	print_list_info
	add	x0, x19, 616
	add	x1, x19, 2866
	bl	print_list_info
	add	x0, x19, 3584
	add	x1, x19, 2868
	bl	print_list_info
	add	x0, x19, 3576
	add	x1, x19, 2870
	bl	print_list_info
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	adrp	x0, .LANCHOR0+2880
	ldr	x0, [x0,#:lo12:.LANCHOR0+2880]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L942
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
.L942:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L943
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L943:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L944
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L944:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L941
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L941:
	ret
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x7, .LANCHOR0
	uxth	w0, w0
	add	x2, x7, :lo12:.LANCHOR0
	mov	w12, 24
	ldr	x2, [x2,584]
	add	x2, x2, x0, uxth 2
	ldrb	w11, [x2,3]
	mov	w2, 0
	mov	w6, w2
.L947:
	add	x8, x7, :lo12:.LANCHOR0
	ldrb	w3, [x8,3572]
	cmp	w6, w3
	bge	.L953
	asr	w3, w11, w6
	tbnz	x3, 0, .L948
	ldrb	w5, [x8,3538]
	sbfiz	x10, x2, 1, 32
	ldrb	w9, [x8,744]
	ldrh	w3, [x8,3536]
	sdiv	w4, w6, w5
	sub	w9, w12, w9
	sub	w3, w9, w3
	lsl	w3, w4, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,3538]
	cmp	w3, 1
	bls	.L949
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L949:
	add	w2, w2, 1
.L948:
	add	w6, w6, 1
	b	.L947
.L953:
	mov	w0, w2
	mov	w4, -1
.L951:
	add	x3, x7, :lo12:.LANCHOR0
	ldrb	w3, [x3,3572]
	cmp	w2, w3
	bge	.L954
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L951
.L954:
	ret
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_erase_phy_blk
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	stp	x29, x30, [sp, -48]!
	adrp	x3, .LANCHOR0
	uxth	w2, w0
	add	x5, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	mov	w0, 24
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	w21, w1
	mov	x20, x3
	ldrb	w4, [x5,744]
	ldrh	w19, [x5,3536]
	sub	w4, w0, w4
	ldrb	w0, [x5,3554]
	sub	w4, w4, w19
	mov	w19, 1
	lsl	w19, w19, w4
	asr	w22, w2, w4
	sub	w19, w19, #1
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L956
	ldrb	w0, [x5,3555]
	cbnz	w0, .L956
	ldrh	w2, [x5,3634]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L956:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,3634]
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
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	uxth	w24, w0
	add	x0, x23, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	ubfiz	x20, x24, 2, 16
	mov	w22, w1
	ldr	x0, [x0,584]
	mov	w19, 0
	add	x0, x0, x20
	ldrb	w25, [x0,3]
.L961:
	add	x2, x23, :lo12:.LANCHOR0
	ldrb	w0, [x2,3636]
	cmp	w19, w0
	bge	.L972
	ldrb	w3, [x2,3538]
	mov	w21, 0
	ldrh	w2, [x2,3634]
	mov	w1, w21
	sub	w4, w3, #1
	mul	w6, w19, w3
	mul	w5, w24, w3
.L973:
	cmp	w1, w3
	bge	.L986
	add	w0, w1, w6
	asr	w0, w25, w0
	tbnz	x0, 0, .L962
	and	w0, w1, w4
	add	x7, x29, 80
	add	w0, w5, w0
	mul	w0, w0, w2
	str	w0, [x7,w21,sxtw 2]
	add	w21, w21, 1
.L962:
	add	w1, w1, 1
	b	.L973
.L986:
	cmp	w3, 4
	bne	.L985
	mov	x26, 0
.L964:
	cmp	w21, w26
	ble	.L967
	add	x2, x29, 80
	mov	w0, w19
	mov	w1, w22
	ldr	w2, [x2,x26,lsl 2]
	add	x26, x26, 1
	bl	flash_erase_block_en
	b	.L964
.L985:
	cmp	w21, 2
	bne	.L968
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,3554]
	cbz	w1, .L969
	ldrb	w0, [x0,3555]
	cbnz	w0, .L969
	cmp	w22, wzr
	ldr	w2, [x29,80]
	ldr	w3, [x29,84]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L969:
	ldr	w2, [x29,80]
	mov	w0, w19
	ldr	w3, [x29,84]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L967
.L968:
	cmp	w21, 1
	bne	.L967
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,3554]
	cbz	w1, .L971
	ldrb	w0, [x0,3555]
	cbnz	w0, .L971
	cmp	w22, wzr
	ldr	w2, [x29,80]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L971:
	ldr	w2, [x29,80]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L967:
	add	w19, w19, 1
	b	.L961
.L972:
	cbnz	w22, .L974
	ldr	x1, [x2,584]
	ldrh	w0, [x1,x20]
	add	w3, w0, 1
	bfi	w0, w3, 0, 11
	strh	w0, [x1,x20]
	ldr	x0, [x2,2880]
	ldr	w1, [x0,84]
	ldrh	w3, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,84]
	ldr	x1, [x2,584]
	ldrh	w1, [x1,x20]
	ubfx	x1, x1, 0, 11
	uxth	w2, w1
	cmp	w3, w2
	bge	.L976
	strh	w1, [x0,96]
	b	.L976
.L974:
	ldr	x3, [x2,584]
	ldr	w0, [x3,x20]
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x3,x20]
	ldr	x0, [x2,2880]
	ldr	w1, [x0,80]
	add	w1, w1, 1
	str	w1, [x0,80]
	ldr	x1, [x2,584]
	ldrh	w2, [x0,98]
	ldr	w1, [x1,x20]
	ubfx	x1, x1, 11, 8
	cmp	w2, w1, uxtb
	bcs	.L976
	strh	w1, [x0,98]
.L976:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 144
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
	ldr	x1, [x1,2880]
	ldrh	w2, [x1,136]
	cmp	w2, 63
	bls	.L988
	strh	wzr, [x1,136]
.L988:
	ldrh	w0, [x1,112]
	cbnz	w0, .L989
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1150
	add	x1, x1, 1392
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L989:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,2880]
.L993:
	ldrh	w2, [x1,136]
.L990:
	cmp	w2, 63
	bgt	.L996
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L995
	add	w2, w2, 1
	b	.L990
.L996:
	strh	wzr, [x1,136]
	b	.L993
.L995:
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
	ldr	x2, [x0,2880]
	ldrh	w0, [x2,138]
	cmp	w0, 63
	bls	.L998
	strh	wzr, [x2,138]
.L998:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L999
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1174
	add	x1, x1, 1416
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L999:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,2880]
.L1003:
	ldrh	w2, [x1,138]
.L1000:
	cmp	w2, 63
	bgt	.L1005
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L1001
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L1001:
	add	w2, w2, 1
	b	.L1000
.L1005:
	strh	wzr, [x1,138]
	b	.L1003
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
	beq	.L1006
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,584]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L1006
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L1010
	ldr	x1, [x23,2880]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L1011
.L1010:
	tst	w0, 24
	ldr	x1, [x23,2880]
	bne	.L1012
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L1011
.L1012:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L1011:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1013
	ldr	w0, [x21,x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21,x20]
	ldr	x1, [x19,2880]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L1006
.L1013:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L1014
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2880]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L1006
.L1014:
	cmp	w0, 96
	bne	.L1006
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2880]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L1006:
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
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x4, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, x2
	ldrh	w0, [x4,3596]
	ldr	x3, [x4,608]
	ldrb	w4, [x4,3572]
	mul	w0, w0, w4
	cmp	w1, w0
	bge	.L1016
	add	x3, x3, 16
	b	.L1017
.L1016:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L1017:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, x1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L1018
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1696
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1018:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,3634]
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
	adrp	x2, .LANCHOR3
	adrp	x1, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR3
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x1, x1, x0, uxtb 1
	mov	w3, 65535
	add	x29, sp, 0
	ldrh	w1, [x1,3640]
	ldr	x4, [x2,-200]
	ldr	x5, [x2,-192]
.L1020:
	cmp	w1, w3
	beq	.L1024
	uxtw	x2, w1
	ldr	w6, [x4,x2,lsl 2]
	cmp	w6, w0
	bne	.L1021
	mov	w0, w1
	bl	ftl_get_ppa_from_index
	b	.L1022
.L1021:
	ldrh	w1, [x5,x2,lsl 1]
	b	.L1020
.L1024:
	mov	w0, -1
.L1022:
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
	bne	.L1026
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1819
	add	x1, x1, 1464
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1026:
	adrp	x20, .LANCHOR0
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	cmp	w1, w0
	bne	.L1027
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1820
	add	x1, x1, 1464
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1027:
	ldrh	w0, [x19,6]
	cbnz	w0, .L1028
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1821
	add	x1, x1, 1464
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1028:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrb	w2, [x1,3572]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L1029:
	cmp	w0, w1
	bne	.L1033
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L1030
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1030:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1029
.L1033:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x19,2]
	ldrh	w1, [x20,3634]
	ldrb	w3, [x20,3572]
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
	bne	.L1032
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L1032:
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
	bcs	.L1035
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1852
	add	x1, x1, 1488
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1035:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w20
	bl	ftl_erase_phy_blk
	ldr	x0, [x19,2880]
	ldrh	w0, [x0,130]
	bl	ftl_free_sys_blk
	ldr	x0, [x19,2880]
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
	ldrh	w2, [x3,-184]
	cbnz	w0, .L1037
	cmp	w2, 4
	bhi	.L1037
	add	w2, w2, 1
	strh	w2, [x3,-184]
	b	.L1036
.L1037:
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LANCHOR0
	mov	w9, 65535
	strh	wzr, [x1,-184]
	add	x1, x0, :lo12:.LANCHOR0
	ldrh	w6, [x1,576]
	ldr	x7, [x1,600]
	ldr	x8, [x1,584]
	mov	x1, 0
	mov	w2, w1
	mov	w3, w1
.L1039:
	cmp	w6, w1, uxth
	bls	.L1047
	ldrh	w4, [x7,x1,lsl 1]
	cmp	w4, w9
	beq	.L1040
	add	x5, x8, x1, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1041
	add	w2, w2, w4
	b	.L1040
.L1041:
	add	w3, w3, w4
.L1040:
	add	x1, x1, 1
	b	.L1039
.L1047:
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,608]
	ldr	x0, [x0,2880]
	str	w3, [x1,524]
	str	w2, [x1,528]
	ldrh	w0, [x0,120]
	cbz	w0, .L1036
	udiv	w2, w2, w0
	str	w2, [x1,532]
.L1036:
	ret
	.size	ftl_total_vpn_update, .-ftl_total_vpn_update
	.align	2
	.global	ftl_debug_info_fill
	.type	ftl_debug_info_fill, %function
ftl_debug_info_fill:
	mov	w0, 0
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
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x19]
	mov	w0, 0
	cbnz	w2, .L1050
	mov	w0, 1
	str	w0, [x1,2900]
.L1050:
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
	adrp	x2, .LANCHOR0
	beq	.L1053
	add	x5, x2, :lo12:.LANCHOR0
	uxtw	x0, w1
	lsl	x3, x0, 1
	ldr	x4, [x5,600]
	ldrh	w19, [x4,x3]
	cbnz	w19, .L1054
	ldr	x2, [x5,584]
	add	x0, x2, x0, lsl 2
	mov	w2, w19
	ldrb	w3, [x0,2]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	ubfx	x3, x3, 5, 3
	bl	printk
	b	.L1060
.L1054:
	sub	w19, w19, #1
	strh	w19, [x4,x3]
.L1053:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3512]
	cmp	w2, w1
	beq	.L1055
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1056
	strh	w1, [x19,3512]
.L1060:
	mov	w0, 0
	b	.L1055
.L1056:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	adrp	x2, .LANCHOR3
	cset	w0, ne
	add	x2, x2, :lo12:.LANCHOR3
	ldr	x1, [x29,40]
	ldrh	w3, [x2,-182]
	add	w3, w3, 1
	uxth	w3, w3
	cmp	w3, 7
	csel	w3, w3, wzr, ls
	strh	w3, [x2,-182]
	ldrh	w2, [x19,3512]
	add	x3, x19, x3, uxth 1
	strh	w1, [x19,3512]
	strh	w2, [x3,3520]
.L1055:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	adrp	x4, .LANCHOR0
	uxtb	w11, w0
	add	x3, x4, :lo12:.LANCHOR0
	uxth	w2, w2
	add	x3, x3, 3640
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w5, [x3,w11,sxtw 1]
	adrp	x3, .LANCHOR3
	add	x6, x3, :lo12:.LANCHOR3
	ldr	x8, [x6,-200]
	ldr	x13, [x6,-192]
	mov	w6, 65535
	mov	w12, w6
.L1062:
	cmp	w5, w12
	beq	.L1066
	uxtw	x9, w5
	uxth	x7, w5
	lsl	x10, x9, 2
	add	x14, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L1063
	mov	w5, -1
	str	w5, [x14]
	mov	w5, 65535
	cmp	w6, w5
	add	x5, x3, :lo12:.LANCHOR3
	ldr	x5, [x5,-192]
	bne	.L1064
	ldrh	w6, [x5,x7,lsl 1]
	add	x5, x4, :lo12:.LANCHOR0
	add	x5, x5, 3640
	strh	w6, [x5,w11,sxtw 1]
	b	.L1065
.L1064:
	ldrh	w8, [x5,x7,lsl 1]
	strh	w8, [x5,w6,uxtw 1]
.L1065:
	add	x5, x3, :lo12:.LANCHOR3
	mov	w6, -1
	ldr	x5, [x5,-192]
	strh	w6, [x5,x7,lsl 1]
	b	.L1066
.L1063:
	mov	w6, w5
	ldrh	w5, [x13,x9,lsl 1]
	b	.L1062
.L1066:
	add	x3, x3, :lo12:.LANCHOR3
	uxtw	x5, w2
	add	x4, x4, :lo12:.LANCHOR0
	cmn	w1, #1
	ldr	x6, [x3,-200]
	str	w0, [x6,x5,lsl 2]
	add	x6, x4, 3640
	ldr	x0, [x3,-192]
	ldrh	w7, [x6,w11,sxtw 1]
	strh	w2, [x6,w11,sxtw 1]
	strh	w7, [x0,x5,lsl 1]
	beq	.L1068
	ldrb	w0, [x4,744]
	mov	w2, 24
	ldrh	w5, [x4,3536]
	mov	w3, 1
	sub	w2, w2, w0
	ldrb	w0, [x4,3538]
	sub	w2, w2, w5
	lsr	w1, w1, w5
	lsl	w2, w3, w2
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L1068:
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
	add	x7, x6, :lo12:.LANCHOR0
	add	x29, sp, 0
	mov	w3, 24
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, x6
	ldrb	w1, [x7,744]
	ldrb	w5, [x7,3538]
	sub	w3, w3, w1
	lsl	w1, w4, w1
	lsr	w2, w0, w3
	sub	w1, w1, #1
	and	w1, w1, w2
	cmp	w5, 1
	uxtb	w1, w1
	mul	w2, w1, w5
	uxtb	w20, w2
	ldrh	w2, [x7,3536]
	sub	w3, w3, w2
	lsr	w0, w0, w2
	lsl	w3, w4, w3
	sub	w3, w3, #1
	and	w3, w3, w0
	uxth	w4, w3
	udiv	w3, w3, w5
	uxth	w21, w3
	bls	.L1073
	sub	w5, w5, #1
	and	w5, w4, w5
	add	w2, w20, w5
	uxtb	w20, w2
.L1073:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L1074
	adrp	x0, .LC105
	mov	w2, w20
	add	x0, x0, :lo12:.LC105
	uxth	w3, w3
	bl	printk
.L1074:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w1, w21
	bls	.L1072
	ldr	x0, [x0,584]
	mov	w2, 1
	add	x21, x0, x21, uxth 2
	ldrb	w0, [x21,3]
	lsl	w2, w2, w20
	orr	w2, w2, w0
	strb	w2, [x21,3]
.L1072:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
	.align	2
	.global	gc_free_bad_sblk
	.type	gc_free_bad_sblk, %function
gc_free_bad_sblk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	add	x21, x20, 2912
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	uxth	w25, w0
	mov	w24, 0
	ldr	w0, [x21,584]
	cbz	w0, .L1090
	adrp	x23, .LC106
	mov	w26, 24
	add	x23, x23, :lo12:.LC106
.L1091:
	ldrb	w0, [x20,3572]
	cmp	w0, w24
	bls	.L1090
	ldrb	w3, [x20,3538]
	ldrb	w19, [x20,744]
	ldrh	w0, [x20,3536]
	cmp	w3, 1
	sdiv	w1, w24, w3
	sub	w19, w26, w19
	sub	w19, w19, w0
	uxth	w2, w3
	lsl	w19, w1, w19
	madd	w19, w25, w2, w19
	uxth	w19, w19
	bls	.L1081
	sub	w2, w2, #1
	and	w2, w24, w2
	add	w19, w19, w2
	uxth	w19, w19
.L1081:
	mov	w22, 0
.L1082:
	ldr	w0, [x21,584]
	cmp	w22, w0
	bcs	.L1092
	add	x0, x21, x22, sxtw 1
	ldrh	w0, [x0,588]
	cmp	w0, w19
	bne	.L1083
	mov	w1, w19
	mov	x0, x23
	bl	printk
	ldrh	w0, [x20,3536]
	lsl	w0, w19, w0
	bl	ftl_mask_bad_block
	ldr	w2, [x21,584]
	mov	w1, w22
.L1084:
	cmp	w1, w2
	bcs	.L1093
	add	w3, w1, 1
	add	x1, x21, x1, sxtw 1
	add	x0, x21, x3, sxtw 1
	ldrh	w0, [x0,588]
	strh	w0, [x1,588]
	uxth	w1, w3
	b	.L1084
.L1093:
	sub	w2, w2, #1
	str	w2, [x21,584]
.L1083:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L1082
.L1092:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L1091
.L1090:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_free_bad_sblk, .-gc_free_bad_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x4, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxth	x24, w0
	ldr	x7, [x4,584]
	lsl	x20, x24, 2
	mov	x22, x24
	add	x21, x7, x20
	ldrb	w2, [x21,2]
	ubfx	x23, x2, 5, 3
	tbz	x2, 3, .L1096
	ldr	x0, [x4,2880]
	ldrh	w3, [x7,x20]
	ldrh	w5, [x0,74]
	and	w8, w3, 2047
	ldr	w3, [x7,x20]
	add	w5, w8, w5
	ldrh	w6, [x0,72]
	ubfx	x9, x3, 11, 8
	adrp	x0, .LANCHOR3
	ldrh	w3, [x4,592]
	uxth	w1, w5
	add	w6, w9, w6
	add	x0, x0, :lo12:.LANCHOR3
	uxth	w5, w6
	udiv	w6, w1, w3
	ldrh	w10, [x0,-180]
	add	w6, w5, w6
	cmp	w6, w10
	ble	.L1097
	ldrh	w1, [x4,2862]
	ldrh	w5, [x4,2870]
	add	w5, w5, w1
	ldrh	w1, [x4,3604]
	add	w1, w1, 8
	cmp	w5, w1
	blt	.L1103
	ldrh	w5, [x4,2866]
	ldrh	w1, [x4,2860]
	ldrh	w0, [x0,-178]
	ldrh	w4, [x4,2868]
	add	w1, w5, w1
	add	w0, w0, 8
	add	w1, w1, w4
	b	.L1115
.L1097:
	madd	w5, w5, w3, w1
	ldrh	w1, [x0,-176]
	cmp	w5, w1
	ble	.L1099
	ldrh	w5, [x4,2866]
	ldrh	w1, [x4,2860]
	ldrh	w0, [x0,-178]
	add	w1, w5, w1
	ldrh	w5, [x4,2868]
	add	w1, w1, w5
	add	w5, w0, 8
	cmp	w1, w5
	blt	.L1113
	ldrh	w6, [x4,2870]
	ldrh	w5, [x4,2862]
	ldrh	w4, [x4,3604]
	add	w5, w6, w5
	add	w4, w4, 8
	cmp	w5, w4
	blt	.L1103
	add	w0, w0, 24
.L1115:
	cmp	w1, w0
	bge	.L1103
.L1113:
	and	w2, w2, -25
	b	.L1112
.L1103:
	mov	w0, 2
	bfi	w2, w0, 3, 2
.L1112:
	strb	w2, [x21,2]
.L1099:
	ldrb	w0, [x21,2]
	ands	w0, w0, 24
	bne	.L1104
	mul	w4, w9, w3
	ldrh	w0, [x7,x20]
	lsr	w3, w9, 3
	add	w4, w4, w4, lsl 1
	add	w4, w8, w4, lsr 2
	bfi	w0, w4, 0, 11
	strh	w0, [x7,x20]
	ldr	w0, [x7,x20]
	bfi	w0, w3, 11, 8
	str	w0, [x7,x20]
	b	.L1096
.L1104:
	cmp	w0, 16
	bne	.L1096
	sdiv	w3, w8, w3
	ldr	w4, [x7,x20]
	add	w3, w3, w3, lsl 1
	add	w3, w9, w3, lsr 2
	bfi	w4, w3, 11, 8
	asr	w3, w8, 5
	str	w4, [x7,x20]
	bfi	w4, w3, 0, 11
	strh	w4, [x7,x20]
.L1096:
	mov	w0, w22
	add	w23, w23, 6
	and	w23, w23, 7
	bl	zftl_remove_data_node
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w23, 4
	ldr	x2, [x1,584]
	add	x2, x2, x20
	ldrb	w0, [x2,2]
	and	w0, w0, 31
	strb	w0, [x2,2]
	ldr	x0, [x1,600]
	strh	wzr, [x0,x24,lsl 1]
	bhi	.L1106
	mov	w0, w22
	bl	gc_free_bad_sblk
.L1106:
	ldrb	w0, [x21,2]
	tbz	x0, 3, .L1107
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,608]
	ldrh	w1, [x0,586]
	cmp	w1, w22
	bne	.L1107
	mov	w1, -1
	ldrh	w2, [x0,590]
	strh	w1, [x0,586]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L1107
	strh	w22, [x0,590]
	adrp	x0, .LC107
	mov	w1, w22
	add	x0, x0, :lo12:.LC107
	bl	printk
	b	.L1094
.L1107:
	mov	w0, w22
	bl	zftl_insert_free_list
.L1094:
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
	add	x24, x23, 2912
	add	x21, x21, 1512
.L1117:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L1155
	add	x0, x24, x20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,600]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L1118
	adrp	x0, .LC108
	mov	w1, w22
	add	x0, x0, :lo12:.LC108
	bl	printk
.L1118:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,600]
	strh	wzr, [x1,x26]
	ldr	x1, [x0,600]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L1119
	ldr	x0, [x0,584]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L1120
	ldrb	w2, [x22,2]
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L1120:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L1133
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L1121
.L1133:
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 1115
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1121:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,608]
	ldrh	w1, [x2,124]
	cbz	w1, .L1123
	mov	x0, 0
.L1125:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L1124
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L1123
.L1124:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1125
.L1123:
	ldrh	w1, [x2,120]
	cbz	w1, .L1126
	mov	x0, 0
.L1128:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L1127
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L1126
.L1127:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1128
.L1126:
	ldrh	w1, [x2,122]
	cbz	w1, .L1129
	mov	x0, 0
.L1131:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L1130
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L1129
.L1130:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1131
	b	.L1129
.L1119:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L1129:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1117
.L1155:
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
	ldr	x2, [x19,2880]
	ldr	x0, [x19,608]
	ldrh	w8, [x19,2870]
	ldrh	w1, [x2,148]
	ldrh	w2, [x2,146]
	str	w8, [sp]
	ldrh	w6, [x19,2866]
	ldr	w8, [x0,524]
	str	w8, [sp,8]
	ldrh	w7, [x19,2868]
	ldr	w0, [x0,528]
	str	w0, [sp,16]
	ldr	w0, [x19,2856]
	ldrh	w3, [x19,2860]
	ldrh	w4, [x19,2862]
	ldrh	w5, [x19,2864]
	str	w0, [sp,24]
	adrp	x0, .LC110
	add	x0, x0, :lo12:.LC110
	bl	printk
	ldr	x5, [x19,2880]
	adrp	x0, .LC111
	ldrb	w3, [x19,2873]
	add	x0, x0, :lo12:.LC111
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,2880]
	adrp	x0, .LC112
	add	x0, x0, :lo12:.LC112
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,2880]
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
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
	bl	printk
	ldrh	w1, [x19,2890]
	adrp	x0, .LC114
	ldrh	w2, [x19,2892]
	add	x0, x0, :lo12:.LC114
	ldrh	w3, [x19,2894]
	ldrh	w4, [x19,2896]
	ldrh	w5, [x19,2888]
	bl	printk
	ldr	x4, [x19,608]
	adrp	x0, .LC115
	ldr	x6, [x19,2880]
	add	x0, x0, :lo12:.LC115
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
	cbnz	x0, .L1158
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 676
	add	x1, x1, 1528
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L1167
.L1158:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2848]
	cmp	w1, w0
	bls	.L1163
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 681
	add	x1, x1, 1528
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1163:
	ldrb	w0, [x20,56]
	cbz	w0, .L1160
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,2848]
	cmp	w1, w0
	bcs	.L1161
.L1160:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	zbuf_free
	ldrb	w0, [x19,2872]
	b	.L1167
.L1161:
	add	x0, x21, 2904
	mov	x1, x20
	bl	buf_add_tail
	ldr	x0, [x21,2880]
	ldrb	w1, [x20,56]
	ldrb	w2, [x21,2872]
	ldr	w3, [x0,16]
	add	w2, w2, 1
	add	w1, w3, w1
	str	w1, [x0,16]
	ldr	w1, [x0,32]
	uxtb	w2, w2
	strb	w2, [x21,2872]
	add	w1, w1, 1
	str	w1, [x0,32]
	mov	w0, w2
.L1167:
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
	add	x20, x19, 800
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x21, x19
	mov	w23, 0
	mov	x22, x20
.L1169:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w3, [x0,-174]
	cmp	w3, 255
	beq	.L1178
	lsl	x1, x3, 6
	add	x24, x20, x1
	add	x4, x24, 32
	ldrb	w1, [x20,x1]
	strb	w1, [x0,-174]
	ldr	w0, [x24,52]
	cmn	w0, #1
	bne	.L1170
	ldrb	w2, [x19,744]
	mov	w0, 24
	ldrh	w1, [x19,3536]
	sub	w0, w0, w2
	mov	w2, 1
	sub	w0, w0, w1
	lsl	w0, w2, w0
	ldr	w2, [x4,8]
	sub	w0, w0, #1
	lsr	w1, w2, w1
	mov	w2, 65535
	and	w1, w0, w1
	ldrb	w0, [x19,3538]
	udiv	w1, w1, w0
	ldr	x0, [x19,608]
	ldr	w5, [x0,560]
	cmp	w5, w2
	uxth	w23, w1
	and	w1, w1, w2
	bne	.L1171
	str	w1, [x0,560]
	ldr	w2, [x4,8]
	str	w2, [x0,564]
.L1171:
	add	x3, x22, x3, lsl 6
	adrp	x0, .LC116
	add	x3, x3, 32
	add	x0, x0, :lo12:.LC116
	ldr	w2, [x3,4]
	ldr	w3, [x3,8]
	bl	printk
	ldr	x0, [x21,608]
	ldr	w1, [x0,556]
	add	w1, w1, 1
	str	w1, [x0,556]
	ldrh	w1, [x0,16]
	cmp	w1, w23
	bne	.L1172
	strh	wzr, [x0,22]
	b	.L1173
.L1172:
	ldrh	w1, [x0,48]
	cmp	w1, w23
	bne	.L1173
	strh	wzr, [x0,54]
.L1173:
	mov	x0, x24
	mov	w23, 1
	bl	ftl_write_buf
	b	.L1169
.L1170:
	cmp	w23, 1
	bne	.L1175
	ldr	w1, [x4,4]
	adrp	x0, .LC117
	ldr	w2, [x4,8]
	add	x0, x0, :lo12:.LC117
	bl	printk
	mov	x0, x24
	bl	ftl_write_buf
	b	.L1169
.L1175:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x24,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x24,2]
	and	w23, w0, 4
	uxtb	w23, w23
	cbz	w23, .L1176
	and	w0, w0, -3
	mov	w23, 0
	strb	w0, [x24,2]
	b	.L1169
.L1176:
	mov	x0, x24
	bl	zbuf_free
	b	.L1169
.L1178:
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
	cbnz	x0, .L1180
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 972
	add	x1, x1, 1544
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	b	.L1179
.L1180:
	ldrb	w1, [x0,56]
	adrp	x0, .LANCHOR0+2848
	ldrb	w0, [x0,#:lo12:.LANCHOR0+2848]
	cmp	w1, w0
	bls	.L1183
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 977
	add	x1, x1, 1544
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1183:
	adrp	x19, .LANCHOR3
	mov	x1, x20
	add	x19, x19, :lo12:.LANCHOR3
	sub	x0, x19, #173
	bl	buf_add_tail
	ldrb	w0, [x19,-172]
	add	w0, w0, 1
	strb	w0, [x19,-172]
.L1179:
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
	stp	x19, x20, [sp,16]
	mov	w20, w0
	str	x23, [sp,48]
	mov	w0, 2
	uxth	w23, w20
	stp	x21, x22, [sp,32]
	mov	w1, w23
	csel	w22, wzr, w0, ne
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1186
	adrp	x0, .LANCHOR0+584
	ldr	x21, [x0,#:lo12:.LANCHOR0+584]
	add	x21, x21, x19, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L1187
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 846
	add	x1, x1, 1568
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1187:
	ldrb	w1, [x21,2]
	bfi	w1, w20, 5, 3
	ubfx	x2, x1, 3, 2
	orr	w0, w22, w2
	bfi	w1, w0, 3, 2
	strb	w1, [x21,2]
	b	.L1193
.L1186:
	adrp	x21, .LC118
	bl	print_ftl_debug_info
	add	x21, x21, :lo12:.LC118
	mov	w1, w19
	mov	w2, w20
	mov	x0, x21
	bl	printk
	mov	w1, w23
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w1, w19
	mov	w2, w20
	mov	x0, x21
	bl	printk
	bl	dump_all_list_info
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 856
	add	x1, x1, 1568
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1193:
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
	adrp	x2, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR0
	add	x2, x2, :lo12:.LANCHOR3
	mov	w1, -1
	strb	w1, [x0,2851]
	strb	w1, [x0,3552]
	mov	w0, 0
	strb	w1, [x2,-174]
	strb	w1, [x2,-171]
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC119
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC119
	ldrb	w1, [x20,2851]
	bl	printk
	ldrb	w19, [x20,2851]
	cmp	w19, 255
	beq	.L1195
	add	x20, x20, 800
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC120
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC120
.L1197:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1195
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1197
.L1195:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.type	queue_lun_state, %function
queue_lun_state:
	adrp	x2, .LANCHOR0
	mov	w6, w0
	add	x2, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrb	w3, [x2,2851]
	cmp	w3, 255
	beq	.L1205
	ldrb	w4, [x2,744]
	mov	w9, 24
	ldrh	w11, [x2,3536]
	mov	w0, 1
	sub	w9, w9, w4
	ldrb	w8, [x2,3538]
	sub	w5, w9, w11
	lsl	w4, w0, w4
	lsl	w5, w0, w5
	sub	w8, w8, #1
	sub	w4, w4, #1
	sub	w5, w5, #1
	asr	w10, w6, w9
	uxth	w8, w8
	asr	w6, w6, w11
	uxth	w4, w4
	uxth	w5, w5
	and	w6, w6, w8
	adrp	x12, .L1208
	and	w10, w4, w10
	and	w6, w6, w5
	add	x2, x2, 800
	add	x12, x12, :lo12:.L1208
.L1211:
	add	x13, x2, x3, lsl 6
	ldr	w7, [x13,40]
	lsr	w0, w7, w9
	and	w0, w4, w0
	cmp	w0, w10
	bne	.L1206
	lsr	w7, w7, w11
	ldrb	w0, [x13,58]
	and	w7, w8, w7
	and	w7, w5, w7
	cmp	w7, w6
	bne	.L1207
	cmp	w1, 1
	beq	.L1209
	b	.L1205
.L1207:
	cmp	w1, 3
	bhi	.L1206
	ldrb	w7, [x12,w1,uxtw]
	adr	x13, .Lrtx1208
	add	x7, x13, w7, sxtb #2
	br	x7
.Lrtx1208:
	.section	.rodata
	.align	0
	.align	2
.L1208:
	.byte	(.L1205 - .Lrtx1208) / 4
	.byte	(.L1209 - .Lrtx1208) / 4
	.byte	(.L1210 - .Lrtx1208) / 4
	.byte	(.L1205 - .Lrtx1208) / 4
	.text
.L1209:
	sub	w7, w0, #7
	and	w7, w7, -3
	uxtb	w7, w7
	cbz	w7, .L1206
	b	.L1205
.L1210:
	cmp	w0, 11
	bne	.L1205
.L1206:
	lsl	x3, x3, 6
	ldrb	w3, [x2,x3]
	cmp	w3, 255
	bne	.L1211
	mov	w0, 0
.L1205:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x0, .LANCHOR0
	adrp	x1, .LANCHOR3
	add	x4, x0, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w5, 0
	add	x6, x4, 800
	mov	w12, -1
	mov	x3, x1
	ldrb	w8, [x4,2851]
	ldrb	w10, [x1,-171]
	ldrb	w9, [x4,3552]
	ldrb	w11, [x1,-174]
.L1221:
	cmp	w8, 255
	beq	.L1222
	sbfiz	x7, x8, 6, 32
	sxtw	x1, w8
	add	x2, x6, x7
	ldrb	w2, [x2,58]
	sub	w2, w2, #12
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1223
	cbz	w5, .L1220
	strb	w8, [x4,2851]
	b	.L1220
.L1223:
	lsl	x5, x1, 6
	ldrb	w8, [x6,x7]
	add	x2, x6, x5
	strb	w12, [x6,x5]
	ldrb	w5, [x2,59]
	cmp	w5, 1
	bne	.L1226
	ldrh	w2, [x2,50]
	cbnz	w2, .L1234
	mov	w2, w11
	sub	x5, x3, #174
	b	.L1227
.L1226:
	cbz	w5, .L1244
.L1228:
	mov	w5, 1
	b	.L1221
.L1244:
	ldr	w2, [x2,36]
	cmn	w2, #1
	beq	.L1228
	mov	w2, w10
	sub	x5, x3, #171
	b	.L1227
.L1234:
	mov	w2, w9
	add	x5, x4, 3552
.L1227:
	cmp	w2, 255
	bne	.L1232
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x0, x1, lsl 6
	strb	w8, [x0,2851]
	ldrb	w0, [x1,801]
	strb	w0, [x5]
	b	.L1220
.L1232:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x6,x2]
	cmp	w2, 255
	bne	.L1232
	add	x1, x6, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1,1]
	strb	w1, [x6,x5]
	b	.L1228
.L1222:
	cbz	w5, .L1220
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	w1, [x0,2851]
.L1220:
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
	str	x23, [sp,48]
	add	x3, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	ldr	x2, [x3,2880]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	ldrb	w1, [x3,3572]
	strh	w0, [x2,690]
	cmp	w1, w0
	bls	.L1246
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1247
.L1246:
	adrp	x22, .LC121
	add	x22, x22, :lo12:.LC121
.L1259:
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_alloc_sblk
	uxth	w21, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x20,2880]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L1248
	mov	w1, w21
	mov	x0, x22
	bl	printk
	ldr	x2, [x20,584]
	add	x1, x2, x21, uxth 2
	ldrb	w2, [x1,2]
	orr	w2, w2, -32
	strb	w2, [x1,2]
	b	.L1259
.L1248:
	ldr	x1, [x20,2880]
	adrp	x0, .LANCHOR3-168
	mov	w2, 1
	mov	x20, 0
	str	w2, [x0,#:lo12:.LANCHOR3-168]
	mov	w2, 65535
	strh	wzr, [x1,690]
.L1250:
	add	x3, x1, x20, lsl 1
	uxth	w0, w20
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L1249
	add	x20, x20, 1
	cmp	x20, 128
	bne	.L1250
	adrp	x0, .LC0
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 227
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, w20
.L1249:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2880]
	add	x0, x1, x0, sxtw 1
	strh	w21, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L1247:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2880]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w20, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L1252
	adrp	x0, .LC0
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 232
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1252:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 24
	ldrb	w2, [x19,744]
	ldr	x1, [x19,2880]
	sub	w0, w0, w2
	ldrh	w2, [x19,3536]
	sub	w0, w0, w2
	strh	wzr, [x1,696]
	asr	w4, w20, w0
	strh	w20, [x1,692]
	strh	w4, [x1,694]
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L1256
	mov	w2, 1
	mov	w1, w20
	lsl	w2, w2, w0
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	sub	w2, w2, #1
	mov	w3, w20
	uxth	w4, w4
	bl	printk
.L1256:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
.L1263:
	sub	x1, x2, #160
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1262
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1263
	mov	x0, 0
	mov	w3, 32768
.L1265:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1264
	cmp	w2, w3
	bcs	.L1264
	mov	w3, w2
	mov	w19, w4
.L1264:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1265
	cmp	w19, 32
	mov	w0, w19
	bne	.L1262
	adrp	x0, .LANCHOR3
	mov	w2, -1
	add	x0, x0, :lo12:.LANCHOR3
	mov	w1, 0
	sub	x0, x0, #160
	ldrb	w3, [x0,512]
.L1267:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1266
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1266:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1267
	cmp	w19, 32
	bne	.L1268
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 331
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1268:
	mov	w0, w19
.L1262:
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	cbnz	w0, .L1276
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1277:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1277
.L1283:
	adrp	x20, .LANCHOR3
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR3
	mov	w2, 1024
	add	x20, x20, 356
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_memset
	mov	x0, 0
	b	.L1278
.L1276:
	cmp	w0, 1
	bne	.L1279
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L1282:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L1280
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1280:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1282
	b	.L1283
.L1279:
	cmp	w0, 2
	bne	.L1284
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L1286:
	add	x3, x4, 4
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L1286
	b	.L1283
.L1284:
	cmp	w0, 3
	bne	.L1287
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L1290:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L1288
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1288:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1290
	b	.L1283
.L1287:
	cmp	w0, 4
	bne	.L1291
	add	x2, x19, :lo12:.LANCHOR0
	mov	w5, 7
	add	x1, x2, 4
	mov	w4, 6
	strh	w0, [x1,8]
	mov	w0, 5
	strh	wzr, [x2,4]
	mov	w2, 1
	strh	w0, [x1,10]
	mov	w0, 7
	strh	w2, [x1,2]
	mov	w2, 2
	strh	w0, [x1,12]
	mov	w0, 8
	strh	w2, [x1,4]
	mov	w2, 3
	strh	w0, [x1,14]
	mov	w0, 8
	strh	w2, [x1,6]
.L1293:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w0, w0, 1
	cmp	w3, wzr
	csel	w3, w5, w4, ne
	add	x1, x1, 2
	uxth	w0, w0
	sub	w2, w2, w3
	cmp	w0, 256
	strh	w2, [x1,14]
	bne	.L1293
	b	.L1283
.L1291:
	cmp	w0, 5
	bne	.L1294
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1295:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1295
	mov	x0, 0
.L1296:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L1296
	b	.L1283
.L1294:
	cmp	w0, 8
	bne	.L1297
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1298:
	add	x1, x2, 4
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L1298
	b	.L1283
.L1297:
	cmp	w0, 9
	bne	.L1299
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, 4
	strh	wzr, [x0,4]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L1300:
	add	x2, x1, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 506
	strh	w3, [x2,6]
	bne	.L1300
	b	.L1283
.L1299:
	cmp	w0, 10
	bne	.L1283
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1301:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L1301
	mov	x0, 0
.L1302:
	add	x2, x1, x0
	add	w3, w0, 63
	add	x0, x0, 2
	cmp	x0, 386
	strh	w3, [x2,126]
	bne	.L1302
	b	.L1283
.L1278:
	add	x1, x19, 4
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x20,w1,sxtw 1]
	bne	.L1278
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	tbz	x0, 12, .L1322
	adrp	x1, .LANCHOR4
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L1322:
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x25, x22, :lo12:.LANCHOR3
	mov	w2, 8
	ldrh	w0, [x24,34]
	mov	x23, 0
	ldrb	w1, [x24,20]
	mov	w26, 2
	strh	w0, [x20,2]
	add	x27, x24, 9
	ldrh	w0, [x24,18]
	add	x28, x20, 760
	strb	wzr, [x20,633]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x25,1380]
	add	x0, x20, 736
	bl	ftl_memset
	add	x0, x25, 1384
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
.L1324:
	add	x0, x20, 752
	ldrb	w2, [x24,8]
	add	x1, x28, x23, lsl 3
	strb	w26, [x23,x0]
	mov	x0, x27
	bl	flash_mem_cmp8
	cbnz	w0, .L1323
	ldrb	w1, [x20,633]
	add	x2, x25, 1384
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	strb	w0, [x20,633]
	uxtb	w0, w23
	add	x1, x20, x1, sxtw
	strb	w0, [x1,736]
	bl	zftl_flash_enter_slc_mode
.L1323:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L1324
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 8
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1325
.L1329:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,21]
	ldrb	w0, [x21,633]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,22]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1416]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1325:
	add	x23, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w26, [x0,8]
	mov	x24, 0
	and	w20, w20, 65280
	ldrb	w27, [x1,23]
	ldrh	w2, [x23,2]
	add	x25, x0, 9
	add	x3, x23, 760
	add	x4, x22, :lo12:.LANCHOR3
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L1328:
	add	x1, x3, x24, lsl 3
	mov	x0, x25
	mov	w2, w26
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1326
	ldrb	w0, [x23,633]
	add	x1, x4, 1384
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x1,w0,sxtw 2]
	add	w1, w0, 1
	add	x0, x23, x0, sxtw
	strb	w1, [x23,633]
	strb	w24, [x0,736]
.L1326:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1328
	b	.L1329
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
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x19, 3640
	bl	ftl_memset
	ldrh	w0, [x19,3596]
	mov	w1, 255
	ldrb	w2, [x19,3572]
	mul	w2, w0, w2
	adrp	x0, .LANCHOR3-192
	lsl	w2, w2, 2
	ldr	x0, [x0,#:lo12:.LANCHOR3-192]
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
	stp	x19, x20, [sp,16]
	tbz	x0, 12, .L1340
	adrp	x1, .LANCHOR4
	adrp	x0, .LC123
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC123
	add	x1, x1, 72
	mov	w2, 225
	mov	w3, 0
	bl	printk
.L1340:
	adrp	x19, .LANCHOR0
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 512
	add	x0, x19, 3640
	adrp	x20, .LANCHOR3
	bl	ftl_memset
	ldrh	w0, [x19,3596]
	add	x1, x20, :lo12:.LANCHOR3
	ldrb	w2, [x19,3572]
	mul	w2, w0, w2
	ldr	x0, [x1,-192]
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L1341:
	ldrh	w1, [x19,3596]
	ldrb	w2, [x19,3572]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1350
	add	x3, x20, :lo12:.LANCHOR3
	uxtw	x2, w0
	ldr	x1, [x3,-200]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1342
	uxtb	w1, w1
	add	x4, x19, 3640
	ldrh	w5, [x4,w1,sxtw 1]
	strh	w0, [x4,w1,sxtw 1]
	ldr	x1, [x3,-192]
	strh	w5, [x1,x2,lsl 1]
.L1342:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1341
.L1350:
	ldp	x19, x20, [sp,16]
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
	ldrb	w2, [x0,724]
	strb	w2, [x19,7]
	ldr	w2, [x0,520]
	str	w2, [x19]
	ldrb	w2, [x1,9]
	strb	w2, [x19,6]
	mov	w2, 32
	ldrb	w4, [x0,633]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L1352:
	cmp	w4, w1, uxtb
	bls	.L1354
	add	x2, x0, 736
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1352
.L1354:
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
	mov	w2, 600
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	str	x21, [sp,32]
	add	x21, x19, 2912
	mov	x0, x21
	strb	wzr, [x19,3569]
	strb	wzr, [x20,1418]
	str	wzr, [x20,1420]
	strh	wzr, [x19,3600]
	bl	ftl_memset
	ldrh	w1, [x19,3596]
	mov	w0, -1
	ldrb	w3, [x19,3572]
	strh	w0, [x19,2912]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x19,2948]
	strh	w0, [x19,2894]
	ldrh	w0, [x19,3570]
	strh	w2, [x19,2950]
	mul	w2, w1, w3
	mul	w0, w0, w3
	strh	w1, [x19,2896]
	uxth	w2, w2
	lsl	w0, w0, 2
	sub	w4, w2, #32
	strh	w2, [x19,2890]
	strh	w4, [x19,2892]
	strh	wzr, [x19,3598]
	strh	wzr, [x19,3592]
	strh	wzr, [x19,3594]
	str	xzr, [x19,2920]
	bl	ftl_malloc
	ldrb	w1, [x19,3572]
	str	x0, [x20,1424]
	ldrh	w0, [x19,3570]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1432]
	ldrh	w1, [x19,3570]
	ldrb	w0, [x19,3572]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3560]
	ldrb	w1, [x19,3572]
	ldrh	w0, [x19,3570]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3544]
	ldrb	w1, [x19,3572]
	ldrh	w0, [x19,3570]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1440]
	ldrh	w0, [x19,3602]
	ldr	x21, [sp,32]
	lsr	w0, w0, 2
	strh	w0, [x19,2888]
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
	ldr	x0, [x0,2880]
	ldr	w1, [x0,32]
	mov	w0, 10240
	cmp	w1, w0
	bls	.L1357
	bl	ftl_tmp_into_update
.L1357:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x0, [x3,608]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L1358
	ldr	x3, [x3,2880]
	ldr	w1, [x0,572]
	ldr	w3, [x3,36]
	add	w1, w1, 32
	cmp	w3, w1
	bls	.L1409
.L1358:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w20, 65535
	adrp	x27, .LC124
	str	w22, [x29,128]
	mov	w26, w22
	ldr	x1, [x1,2880]
	mov	w25, w22
	str	w22, [x29,136]
	mov	w24, w22
	str	w22, [x29,140]
	mov	w28, w22
	ldr	w3, [x1,36]
	mov	w21, w20
	str	w3, [x0,572]
	mov	w8, w20
	str	w2, [x0,568]
	adrp	x9, .LANCHOR2
	add	x27, x27, :lo12:.LC124
	ldrh	w23, [x1,134]
.L1360:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,576]
	cmp	w0, w23
	bls	.L1470
	uxtw	x7, w23
	ldr	x0, [x4,584]
	lsl	x1, x7, 2
	add	x6, x0, x1
	ldrb	w2, [x6,2]
	and	w3, w2, 224
	cmp	w3, 224
	beq	.L1361
	tbz	x2, 3, .L1362
	ldrh	w3, [x0,x1]
	ldr	w2, [x0,x1]
	and	w3, w3, 2047
	ubfx	x2, x2, 11, 8
	b	.L1363
.L1362:
	tst	w2, 24
	bne	.L1364
	ldrh	w3, [x0,x1]
	mov	w2, 65535
	and	w3, w3, 2047
	b	.L1363
.L1364:
	ldr	w2, [x0,x1]
	mov	w3, 65535
	ubfx	x2, x2, 11, 8
	b	.L1365
.L1363:
	ldr	w4, [x29,136]
	add	w25, w25, 1
	cmp	w20, w3
	add	w4, w4, w3
	str	w4, [x29,136]
	uxth	w25, w25
	bls	.L1408
	add	x4, x19, :lo12:.LANCHOR0
	mov	w20, w3
	ldr	x4, [x4,608]
	strh	w23, [x4,586]
.L1408:
	cmp	w24, w3
	bcs	.L1366
	mov	w22, w23
	mov	w24, w3
.L1366:
	cmp	w2, w8
	beq	.L1367
.L1365:
	ldr	w4, [x29,140]
	add	w26, w26, 1
	cmp	w21, w2
	add	w4, w4, w2
	str	w4, [x29,140]
	uxth	w26, w26
	bls	.L1368
	add	x4, x19, :lo12:.LANCHOR0
	mov	w21, w2
	ldr	x4, [x4,608]
	strh	w23, [x4,584]
.L1368:
	cmp	w28, w2
	bcs	.L1367
	str	w23, [x29,128]
	mov	w28, w2
.L1367:
	cmp	w3, 9
	bls	.L1411
	cmp	w2, 9
	bhi	.L1361
.L1411:
	ldr	w2, [x9,#:lo12:.LANCHOR2]
	tbz	x2, 8, .L1361
	ldrh	w2, [x0,x1]
	ldr	w3, [x0,x1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w5, [x6,2]
	mov	w1, w23
	ldrb	w6, [x6,3]
	and	w2, w2, 2047
	ldr	x10, [x0,600]
	ubfx	x4, x5, 3, 2
	mov	x0, x27
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x9, [x29,112]
	ldrh	w7, [x10,x7,lsl 1]
	str	x8, [x29,120]
	bl	printk
	ldr	x9, [x29,112]
	ldr	x8, [x29,120]
.L1361:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1360
.L1470:
	ldr	x0, [x4,608]
	mov	w1, 255
	mov	w2, 128
	str	x4, [x29,120]
	add	x0, x0, 264
	adrp	x23, .LANCHOR2
	strh	wzr, [x0,-142]
	bl	ftl_memset
	ldr	x4, [x29,120]
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x0, [x4,608]
	ldr	x3, [x4,584]
	ldrh	w7, [x0,586]
	lsl	x0, x7, 2
	mov	x1, x7
	add	x8, x3, x0
	tbz	x2, 10, .L1371
	ldr	x9, [x4,600]
	ldrh	w2, [x3,x0]
	ldrb	w5, [x8,2]
	ldr	w3, [x3,x0]
	adrp	x0, .LC125
	ldrb	w6, [x8,3]
	ubfx	x4, x5, 3, 2
	ldrh	w7, [x9,x7,lsl 1]
	add	x0, x0, :lo12:.LC125
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x8, [x29,120]
	bl	printk
	ldr	x8, [x29,120]
.L1371:
	ldrb	w0, [x8,2]
	adrp	x27, .LANCHOR3
	and	w0, w0, 224
	cmp	w0, 32
	bne	.L1372
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w0, [x0,688]
	cmp	w0, 2
	bls	.L1372
	add	x0, x27, :lo12:.LANCHOR3
	mov	w1, 1
	str	w1, [x0,1448]
.L1372:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1373
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1374
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldr	x0, [x3,608]
	ldrh	w0, [x0,586]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,3486]
	b	.L1373
.L1374:
	tst	w0, 224
	bne	.L1373
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x2,608]
	ldrh	w3, [x1,590]
	cmp	w3, w0
	bne	.L1373
	ldrh	w0, [x1,586]
	ldrh	w1, [x1,588]
	str	x2, [x29,120]
	cmp	w1, w0
	beq	.L1373
	bl	zftl_remove_free_node
	ldr	x2, [x29,120]
	ldr	x0, [x2,608]
	ldrh	w1, [x0,586]
	strh	w1, [x0,590]
	mov	w1, -1
	strh	w1, [x0,586]
.L1373:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x0,608]
	ldr	x4, [x0,584]
	ldrh	w7, [x1,584]
	lsl	x3, x7, 2
	mov	x1, x7
	add	x8, x4, x3
	tbz	x2, 10, .L1375
	ldr	x9, [x0,600]
	adrp	x0, .LC126
	ldrh	w2, [x4,x3]
	add	x0, x0, :lo12:.LC126
	ldrb	w5, [x8,2]
	ldr	w3, [x4,x3]
	and	w2, w2, 2047
	ldrb	w6, [x8,3]
	ubfx	x4, x5, 3, 2
	ldrh	w7, [x9,x7,lsl 1]
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x8, [x29,120]
	bl	printk
	ldr	x8, [x29,120]
.L1375:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1376
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1377
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldr	x0, [x3,608]
	ldrh	w0, [x0,584]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,3486]
	b	.L1376
.L1377:
	and	w0, w0, 248
	cmp	w0, 16
	bne	.L1376
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x2,608]
	ldrh	w3, [x1,588]
	cmp	w3, w0
	bne	.L1376
	ldrh	w0, [x1,584]
	ldrh	w1, [x1,590]
	str	x2, [x29,120]
	cmp	w1, w0
	beq	.L1376
	bl	zftl_remove_free_node
	ldr	x2, [x29,120]
	ldr	x0, [x2,608]
	ldrh	w1, [x0,584]
	strh	w1, [x0,588]
	mov	w1, -1
	strh	w1, [x0,584]
.L1376:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1378
	uxtw	x7, w22
	ldr	x8, [x4,600]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w7, [x8,x7,lsl 1]
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC127
	ldrb	w5, [x6,2]
	add	x0, x0, :lo12:.LC127
	ldrb	w6, [x6,3]
	mov	w1, w22
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1378:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1379
	ldr	w7, [x29,128]
	ldr	x8, [x4,600]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC128
	ldrb	w5, [x6,2]
	mov	x1, x7
	ldrb	w6, [x6,3]
	add	x0, x0, :lo12:.LC128
	ldrh	w7, [x8,x7,lsl 1]
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1379:
	ldr	w1, [x29,136]
	add	x0, x19, :lo12:.LANCHOR0
	udiv	w3, w1, w25
	ldr	w1, [x29,140]
	ldr	x0, [x0,2880]
	strh	w24, [x0,96]
	strh	w28, [x0,98]
	strh	w20, [x0,92]
	strh	w21, [x0,94]
	udiv	w4, w1, w26
	strh	w3, [x0,88]
	strh	w4, [x0,90]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1380
	adrp	x0, .LC129
	mov	w1, w25
	add	x0, x0, :lo12:.LC129
	mov	w2, w26
	uxth	w3, w3
	uxth	w4, w4
	bl	printk
.L1380:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1381
	add	x6, x27, :lo12:.LANCHOR3
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	mov	w1, w20
	mov	w2, w21
	mov	w3, w24
	ldrh	w5, [x6,1452]
	mov	w4, w28
	ldrh	w6, [x6,1454]
	bl	printk
.L1381:
	add	x1, x27, :lo12:.LANCHOR3
	sub	w0, w28, w21
	str	w0, [x29,140]
	ldrh	w0, [x1,1454]
	ldr	w2, [x29,140]
	cmp	w2, w0
	bgt	.L1382
	ldrh	w1, [x1,1452]
	sub	w0, w24, w20
	cmp	w0, w1
	ble	.L1410
.L1382:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w22, w25
	ldr	x1, [x0,608]
	ldr	x0, [x0,2880]
	ldrh	w28, [x1,580]
	ldrh	w26, [x0,134]
	sub	w0, w24, w20
	str	w0, [x29,136]
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	adrp	x24, .LC131
	str	x0, [x29,128]
	add	x0, x24, :lo12:.LC131
	str	x0, [x29,120]
.L1384:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,576]
	cmp	w26, w0
	bcs	.L1393
	add	w7, w28, 1
	ldr	x4, [x1,584]
	uxth	w28, w7
	cmp	w28, w0
	csel	w28, w28, wzr, cc
	uxtw	x24, w28
	lsl	x3, x24, 2
	add	x0, x4, x3
	ldrb	w0, [x0,2]
	and	w2, w0, 224
	cmp	w2, 224
	beq	.L1386
	tst	w0, 192
	beq	.L1386
	ubfx	x0, x0, 3, 2
	cmp	w2, 160
	and	w1, w0, 1
	tbnz	x0, 0, .L1468
	cmp	w0, 2
.L1468:
	add	x0, x27, :lo12:.LANCHOR3
	bne	.L1389
	ldrh	w0, [x0,1454]
	ldr	w2, [x29,140]
	cmp	w2, w0
	ble	.L1390
	ldr	w0, [x4,x3]
	ubfx	x0, x0, 11, 8
	cmp	w0, w21
	bls	.L1391
	cbz	w1, .L1390
	ldrh	w0, [x4,x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1390
.L1391:
	mov	w1, 0
	mov	w0, w28
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w22, w22, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 2912
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,3486]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1390
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,120]
	ldrb	w2, [x1,2]
	b	.L1469
.L1389:
	ldrh	w0, [x0,1452]
	ldr	w2, [x29,136]
	cmp	w2, w0
	ble	.L1390
	ldrh	w0, [x4,x3]
	add	w2, w20, 8
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1392
	cbz	w1, .L1390
	ldr	w0, [x4,x3]
	add	w1, w21, 4
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1390
.L1392:
	mov	w1, 0
	mov	w0, w28
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w25, w25, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 2912
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,3486]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1390
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,128]
	ldrb	w2, [x1,2]
.L1469:
	ldrh	w3, [x10,x24,lsl 1]
	mov	w1, w28
	ldrh	w4, [x9,56]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1390:
	cmp	w25, 4
	bhi	.L1393
	cmp	w22, 4
	bhi	.L1393
.L1386:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L1384
.L1393:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,608]
	str	w28, [x0,580]
	b	.L1383
.L1410:
	mov	w25, 0
	mov	w22, w25
.L1383:
	cbz	w21, .L1396
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w0, [x0,134]
.L1397:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1471
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w21
	bcc	.L1398
	ldrb	w5, [x5,2]
	tst	w5, 24
	beq	.L1398
	sub	w2, w2, w21
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1398:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1397
.L1471:
	ldr	x0, [x1,2880]
	ldrh	w1, [x0,72]
	add	w1, w21, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w21
	bls	.L1396
	sub	w21, w1, w21
	strh	w21, [x0,98]
.L1396:
	cbz	w20, .L1402
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w0, [x0,134]
.L1403:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1472
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L1404
	ldrb	w5, [x5,2]
	and	w5, w5, 24
	cmp	w5, 16
	beq	.L1404
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1404:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1403
.L1472:
	ldr	x0, [x1,2880]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L1402
	sub	w20, w1, w20
	strh	w20, [x0,96]
.L1402:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1359
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x19,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x19,3596]
	cmp	w2, w1
	bhi	.L1359
	mov	w1, 0
	mov	w2, 1
	add	w22, w22, 1
	bl	gc_add_sblk
	b	.L1359
.L1409:
	mov	w25, 0
	mov	w22, w25
.L1359:
	add	w0, w25, w22
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
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w24, 6
	mov	w23, 32768
	ldrh	w2, [x19,576]
	adrp	x26, .LC133
	ldr	x0, [x19,528]
	mov	w25, -1
	add	x26, x26, :lo12:.LC133
	mul	w2, w2, w24
	bl	ftl_memset
	ldrh	w22, [x19,3570]
	mov	w0, 16
	strh	w0, [x19,3632]
	ldrb	w0, [x19,3572]
	str	xzr, [x19,3608]
	str	xzr, [x19,3616]
	mul	w22, w0, w22
	str	xzr, [x19,3624]
	str	xzr, [x19,3584]
	sdiv	w22, w23, w22
	str	xzr, [x19,616]
	str	xzr, [x19,3576]
	strh	wzr, [x19,2860]
	strh	wzr, [x19,2862]
	strh	wzr, [x19,2864]
	strh	wzr, [x19,2866]
	strh	wzr, [x19,2870]
	strh	wzr, [x19,2868]
	sxth	w0, w22
	str	w0, [x29,108]
	ldr	x0, [x19,2880]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1474:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w19, w1
	bge	.L1504
	sxtw	x22, w19
	ldr	x27, [x0,584]
	ldr	w1, [x29,108]
	add	x27, x27, x22, lsl 2
	ldrb	w2, [x27,3]
	cbz	w2, .L1475
	ldrb	w5, [x0,3572]
	ldrh	w6, [x0,3570]
	ldr	x4, [x0,2880]
	mov	w0, 0
	mov	w1, w0
.L1476:
	cmp	w0, w5
	bge	.L1505
	ldrb	w2, [x27,3]
	asr	w2, w2, w0
	tbnz	x2, 0, .L1477
	add	w1, w6, w1
	sxth	w1, w1
	b	.L1478
.L1477:
	ldrh	w2, [x4,146]
	add	w2, w2, 1
	strh	w2, [x4,146]
.L1478:
	add	w0, w0, 1
	b	.L1476
.L1505:
	cbz	w1, .L1480
	sdiv	w1, w23, w1
	add	w1, w1, 1
	sxth	w1, w1
	b	.L1475
.L1480:
	ldrb	w0, [x27,2]
	orr	w0, w0, -32
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	strh	w25, [x0,x22,lsl 1]
.L1475:
	add	x20, x21, :lo12:.LANCHOR0
	smull	x0, w19, w24
	ldr	x2, [x20,528]
	add	x2, x2, x0
	strh	w1, [x2,4]
	ldr	x1, [x20,528]
	add	x2, x1, x0
	strh	w25, [x2,2]
	strh	w25, [x1,x0]
	ldrb	w1, [x27,2]
	and	w0, w1, 224
	cmp	w0, 224
	beq	.L1483
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1483
	ldr	x2, [x20,608]
	ldrh	w4, [x2,16]
	cmp	w19, w4
	beq	.L1483
	ldrh	w4, [x2,48]
	cmp	w19, w4
	beq	.L1483
	ldrh	w2, [x2,80]
	cmp	w19, w2
	beq	.L1483
	cmp	w0, 64
	bne	.L1485
	uxth	w27, w19
	add	x0, x20, 616
	mov	w1, w27
	add	x2, x20, 2866
	b	.L1502
.L1485:
	cmp	w0, 96
	bne	.L1486
	uxth	w27, w19
	add	x0, x20, 3584
	mov	w1, w27
	add	x2, x20, 2868
	b	.L1502
.L1486:
	cmp	w0, 160
	bne	.L1487
	uxth	w27, w19
	add	x0, x20, 3576
	mov	w1, w27
	add	x2, x20, 2870
.L1502:
	bl	_insert_data_list
	ldr	x0, [x20,600]
	ldrh	w0, [x0,x22,lsl 1]
	cmp	w0, 7
	bhi	.L1483
	mov	w0, w27
	mov	w1, 1
	mov	w2, w28
	b	.L1500
.L1487:
	cbnz	w0, .L1483
	ldr	x0, [x20,600]
	ldrh	w2, [x0,x22,lsl 1]
	cbz	w2, .L1488
	mov	x0, x26
	mov	w1, w19
	bl	printk
	ldrb	w0, [x27,2]
	tbz	x0, 4, .L1489
	mov	w1, 5
	b	.L1496
.L1489:
	mov	w1, 2
.L1496:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x27,2]
	mov	w1, 1
	mov	w0, w19
.L1500:
	bl	gc_add_sblk
	b	.L1483
.L1488:
	ands	w1, w1, 24
	add	x2, x21, :lo12:.LANCHOR0
	bne	.L1491
	add	x0, x2, 3608
	mov	w1, w19
	add	x2, x2, 2860
	b	.L1498
.L1491:
	cmp	w1, 16
	bne	.L1492
	add	x0, x2, 3616
	mov	w1, w19
	add	x2, x2, 2862
	b	.L1498
.L1492:
	add	x0, x2, 3624
	mov	w1, w19
	add	x2, x2, 2864
.L1498:
	bl	_insert_free_list
.L1483:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1474
.L1504:
	ldr	x1, [x0,2880]
	ldrh	w2, [x0,2860]
	strh	w2, [x1,114]
	ldrh	w2, [x0,2862]
	strh	w2, [x1,118]
	ldrh	w2, [x0,2864]
	strh	w2, [x1,116]
	ldrh	w2, [x0,2866]
	strh	w2, [x1,122]
	ldrh	w2, [x0,2870]
	ldrh	w0, [x0,2868]
	strh	w2, [x1,120]
	strh	w0, [x1,124]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	pm_free_sblk
	.type	pm_free_sblk, %function
pm_free_sblk:
	stp	x29, x30, [sp, -368]!
	adrp	x0, .LANCHOR3
	add	x29, sp, 0
	add	x1, x0, :lo12:.LANCHOR3
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	ldrh	w1, [x1,1456]
	cmp	w1, 128
	bls	.L1507
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 84
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1507:
	adrp	x21, .LANCHOR0
	mov	x1, 0
	add	x0, x21, :lo12:.LANCHOR0
	mov	w2, w1
	mov	w6, 65535
	ldr	x3, [x0,2880]
	add	x0, x19, :lo12:.LANCHOR3
	ldrh	w4, [x3,688]
	ldrh	w5, [x0,1456]
.L1510:
	add	x7, x3, x1, lsl 1
	uxth	w0, w1
	ldrh	w7, [x7,416]
	cmp	w7, w6
	beq	.L1508
	add	w2, w2, 1
	uxth	w2, w2
.L1508:
	cmp	w2, w4
	bcs	.L1509
	cmp	w2, w5
	bcs	.L1509
	add	x1, x1, 1
	cmp	x1, 128
	bne	.L1510
	mov	w0, w1
.L1509:
	add	w0, w0, 1
	mov	w19, 128
	add	x23, x29, 112
	mov	w1, 0
	uxth	w0, w0
	mov	w2, 256
	cmp	w0, 128
	csel	w19, w0, w19, ls
	mov	x0, x23
	bl	ftl_memset
	uxth	w19, w19
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, 24
	mov	x3, 0
	ldrb	w2, [x1,744]
	ldr	x4, [x1,2880]
	ldrh	w6, [x1,3536]
	sub	w2, w0, w2
	mov	w0, 1
	ldrb	w8, [x1,3538]
	sub	w2, w2, w6
	ldrh	w7, [x4,698]
	lsl	w2, w0, w2
	sub	w2, w2, #1
.L1511:
	cmp	w7, w3, uxth
	bls	.L1539
	add	x0, x4, x3, lsl 2
	ldr	w1, [x0,704]
	mov	x0, 0
	lsr	w1, w1, w6
	and	w1, w1, w2
	udiv	w1, w1, w8
	uxth	w1, w1
.L1512:
	cmp	w19, w0, uxth
	bls	.L1540
	add	x5, x4, x0, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L1513
	ldrh	w5, [x23,x0,lsl 1]
	add	w5, w5, 1
	strh	w5, [x23,x0,lsl 1]
.L1513:
	add	x0, x0, 1
	b	.L1512
.L1540:
	add	x3, x3, 1
	b	.L1511
.L1539:
	add	x1, x21, :lo12:.LANCHOR0
	mov	x20, 0
	mov	w28, 65535
	adrp	x25, .LC134
	adrp	x26, .LC135
	mov	w24, w20
	ldrb	w0, [x1,3572]
	mov	w27, w28
	ldrh	w22, [x1,3596]
	add	x25, x25, :lo12:.LC134
	add	x26, x26, :lo12:.LC135
	mul	w22, w0, w22
	uxth	w22, w22
.L1516:
	uxth	w5, w20
	cmp	w5, w19
	bcs	.L1541
	add	x2, x21, :lo12:.LANCHOR0
	mov	w7, w20
	ldr	x0, [x2,2880]
	add	x1, x0, x20, sxtw 1
	ldrb	w4, [x2,3538]
	ldrh	w3, [x1,416]
	ldrh	w1, [x0,692]
	sdiv	w1, w1, w4
	cmp	w1, w3
	bne	.L1517
	ldrb	w1, [x2,3572]
	ldrh	w2, [x2,3596]
	mul	w1, w1, w2
	strh	w1, [x23,x20,lsl 1]
.L1517:
	ldrh	w2, [x23,x20,lsl 1]
	cmp	w22, w2
	bls	.L1518
	cmp	w2, wzr
	csel	w24, w24, w5, eq
	csel	w22, w22, w2, eq
.L1518:
	cmp	w3, w27
	beq	.L1520
	ldrh	w1, [x0,74]
	cmp	w1, 2
	bls	.L1520
	add	x4, x21, :lo12:.LANCHOR0
	ubfiz	x1, x3, 2, 16
	ldrh	w0, [x0,92]
	ldr	x4, [x4,584]
	add	w0, w0, 4
	ldrh	w4, [x4,x1]
	and	w4, w4, 2047
	cmp	w4, w0
	bgt	.L1520
	mov	w1, w7
	mov	x0, x25
	str	x5, [x29,96]
	str	x7, [x29,104]
	bl	printk
	ldr	x5, [x29,96]
	ldr	x7, [x29,104]
	mov	w28, w5
.L1520:
	ldrh	w2, [x23,x20,lsl 1]
	cbnz	w2, .L1521
	add	x0, x21, :lo12:.LANCHOR0
	sxtw	x5, w7
	ldr	x4, [x0,2880]
	add	x0, x5, 208
	ldrh	w3, [x4,x0,lsl 1]
	cmp	w3, w27
	beq	.L1521
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1522
	ldrh	w4, [x4,688]
	mov	x0, x26
	mov	w1, w7
	str	x5, [x29,104]
	bl	printk
	ldr	x5, [x29,104]
.L1522:
	add	x1, x21, :lo12:.LANCHOR0
	add	x5, x5, 208
	str	x1, [x29,96]
	str	x5, [x29,104]
	ldr	x0, [x1,2880]
	ldrh	w0, [x0,x5,lsl 1]
	bl	ftl_free_sblk
	ldr	x1, [x29,96]
	ldr	x5, [x29,104]
	ldr	x0, [x1,2880]
	mov	w1, -1
	strh	w1, [x0,x5,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L1521:
	add	x20, x20, 1
	b	.L1516
.L1541:
	mov	w0, 65535
	cmp	w28, w0
	csel	w0, w24, w28, eq
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 368
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
	tbz	x0, 12, .L1544
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 105
	add	x2, x2, 112
	bl	printk
.L1544:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	ldr	x0, [x19,728]
	bl	ftl_memset
	ldr	x1, [x19,728]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,728]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	add	x1, x19, 640
	bl	ftl_memcpy
	ldr	x0, [x19,728]
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
.L1549:
	cmp	w2, w3
	bls	.L1551
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1549
.L1551:
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
	adrp	x0, .LANCHOR3+1464
	ldr	x0, [x0,#:lo12:.LANCHOR3+1464]
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
	adrp	x0, .LANCHOR3+1472
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
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
	adrp	x1, .LANCHOR3+1472
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR3+1472]
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
	adrp	x3, .LANCHOR3+1472
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR3+1472]
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
	adrp	x3, .LANCHOR3+1472
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR3+1472]
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
	adrp	x4, .LANCHOR3+1480
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR3+1480]
	add	x4, x4, 4096
	cbnz	w2, .L1559
	add	x1, x4, x1
	b	.L1561
.L1559:
	add	x0, x4, x1
	mov	x1, x6
.L1561:
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
	adrp	x0, .LC137
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC137
	bl	printk
	adrp	x0, .LANCHOR3+1472
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
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
	adrp	x0, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
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
	adrp	x1, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR3+1472]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR3+1472]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR3+1472
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR3+1472]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR3+1472]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR3+1472]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR3+1472
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR3+1472]
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
	adrp	x0, .LC138
	mov	w2, 0
	add	x29, sp, 0
	mov	x5, x1
	add	x0, x0, :lo12:.LC138
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
	ldr	x25, [x0,728]
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
	bne	.L1576
	mov	w0, -89
	strb	w0, [x22,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1639
.L1576:
	cmp	w19, 3
	bne	.L1578
	mov	x5, 0
.L1579:
	sub	w0, w5, #80
	strb	w0, [x26,x5]
	add	x5, x5, 1
	cmp	x5, 8
	bne	.L1579
	mov	w27, w5
	mov	w28, w5
	b	.L1577
.L1578:
	cmp	w19, 4
	bne	.L1580
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
	b	.L1577
.L1580:
	cmp	w19, 5
	bne	.L1581
	mov	w0, 56
	strb	w0, [x25,128]
	mov	w0, 57
	strb	w0, [x25,129]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x25,130]
	mov	w0, 59
	strb	w0, [x25,131]
	b	.L1681
.L1581:
	cmp	w19, 6
	bne	.L1582
	mov	w0, 14
	strb	w0, [x25,128]
	mov	w0, 15
	strb	w0, [x25,129]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x25,130]
	mov	w0, 17
	strb	w0, [x25,131]
	b	.L1681
.L1582:
	cmp	w19, 7
	bne	.L1583
	mov	x0, 0
.L1584:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1584
	mov	w0, -44
	mov	w27, 12
	strb	w0, [x25,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x25,137]
	b	.L1577
.L1583:
	cmp	w19, 8
	bne	.L1639
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
	b	.L1577
.L1639:
	mov	w27, 7
.L1681:
	mov	w28, 4
.L1577:
	sub	w0, w19, #1
	cmp	w0, 1
	bhi	.L1678
	ldr	x0, [x29,136]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w5, 55
	add	x24, x24, 408
.L1585:
	ldrb	w0, [x19,633]
	cmp	w0, w23
	bls	.L1592
	add	x0, x19, x23, sxtw
	ldrb	w0, [x0,736]
	mov	x20, 160
	ldr	x21, [x19,536]
	ubfiz	x1, x0, 8, 8
	mov	x25, 0
	madd	x20, x0, x20, x22
	add	x21, x21, x1
	add	x20, x20, 32
.L1587:
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
	bhi	.L1587
	mov	x0, 0
.L1588:
	add	w2, w0, 8
	mov	x1, 0
.L1589:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w4, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w4, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1589
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1588
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1585
.L1678:
	sub	w0, w19, #3
	cmp	w0, 5
	bhi	.L1592
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
	adrp	x26, .LANCHOR3
	str	w0, [x29,132]
	add	x0, x24, 1
	str	x0, [x29,112]
.L1593:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w0, [x1,633]
	cmp	w0, w20
	bhi	.L1638
.L1592:
	strb	w28, [x22,1]
	strb	w27, [x22,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L1638:
	add	x0, x1, x20, sxtw
	ldrb	w24, [x0,736]
	str	x1, [x29,104]
	mov	x21, 160
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
	ldr	x1, [x29,104]
	sxtw	x0, w24
	ubfiz	x2, x24, 8, 8
	madd	x21, x0, x21, x22
	ldr	x0, [x1,536]
	mov	w1, 255
	str	x1, [x29,104]
	add	x21, x21, 32
	add	x24, x0, x2
	str	w1, [x24,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 8
	ldr	x1, [x29,104]
	bne	.L1594
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
	b	.L1684
.L1594:
	mov	w0, 54
	cmp	w19, 4
	str	w0, [x24,2056]
	bne	.L1596
	mov	w0, 64
	str	w1, [x24,2052]
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1682
.L1596:
	ldr	w0, [x29,132]
	cmp	w0, 1
	bhi	.L1598
	ldrb	w0, [x25,128]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1683
.L1598:
	cmp	w19, 7
	bne	.L1597
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1682:
	str	w0, [x24,2052]
	mov	w0, 77
.L1683:
	str	w0, [x24,2048]
.L1597:
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
	bne	.L1599
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L1600
.L1599:
	str	wzr, [x24,2052]
.L1600:
	mov	w0, 2
.L1684:
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
	cbnz	w8, .L1641
	cbnz	w7, .L1641
	cmp	w19, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L1601
.L1641:
	mov	w1, 16
.L1601:
	adrp	x0, .LANCHOR3
	mov	x9, 0
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x0, [x0,1488]
.L1602:
	ldr	w10, [x24,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L1602
	cbz	w7, .L1603
	mov	w1, 0
.L1605:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L1604
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L1604
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1605
	b	.L1606
.L1604:
	cmp	w1, 7
	bne	.L1607
.L1606:
	adrp	x0, .LC139
	mov	w1, 0
	add	x0, x0, :lo12:.LC139
	bl	printk
.L1608:
	b	.L1608
.L1603:
	cmp	w19, 7
	bne	.L1609
	mov	w1, w7
.L1611:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L1610
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L1610
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1611
	b	.L1612
.L1610:
	cmp	w1, 7
	bne	.L1607
.L1612:
	adrp	x0, .LC139
	mov	w1, 0
	add	x0, x0, :lo12:.LC139
	bl	printk
.L1613:
	b	.L1613
.L1609:
	cmp	w19, 6
	bne	.L1607
	mov	x1, 0
.L1614:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L1607
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L1607
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1614
	adrp	x0, .LC139
	mov	w1, 0
	add	x0, x0, :lo12:.LC139
	bl	printk
.L1616:
	b	.L1616
.L1607:
	add	x0, x26, :lo12:.LANCHOR3
	ldr	x10, [x0,1488]
	mov	x0, 0
.L1617:
	ldr	w1, [x29,128]
	cmp	w1, w0
	ble	.L1686
	ldr	w1, [x24,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L1617
.L1686:
	add	x0, x26, :lo12:.LANCHOR3
	mov	w11, w23
	mov	w9, 8
	ldr	x12, [x0,1488]
.L1620:
	mov	w0, 0
.L1619:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L1619
	ldr	w0, [x29,124]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L1620
	mov	x9, 0
	mov	w17, 1
.L1621:
	mov	w1, 0
	mov	w11, w1
.L1625:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L1623:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L1623
	cmp	w14, 8
	bls	.L1624
	orr	w1, w1, w13
	uxth	w1, w1
.L1624:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L1625
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L1621
	add	x1, x26, :lo12:.LANCHOR3
	mov	x9, 0
	mov	w11, w9
	ldr	x1, [x1,1488]
.L1628:
	ldr	w12, [x1,x9]
	add	x9, x9, 4
	cmp	w12, wzr
	csinc	w11, w11, w11, ne
	cmp	x9, 32
	bne	.L1628
	cmp	w11, 7
	ble	.L1629
	adrp	x0, .LC140
	mov	w2, 1
	add	x0, x0, :lo12:.LC140
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC139
	mov	w1, 0
	add	x0, x0, :lo12:.LC139
	bl	printk
.L1630:
	b	.L1630
.L1629:
	cmp	w19, 6
	mov	w9, 4
	beq	.L1631
	cmp	w19, 7
	mov	w9, 10
	beq	.L1631
	cmp	w7, wzr
	mov	w9, 5
	mov	w1, 8
	csel	w9, w1, w9, eq
.L1631:
	mov	w11, 0
.L1632:
	mov	x1, 0
.L1633:
	add	w12, w0, w1
	ldrb	w13, [x10,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x21,w12,sxtw]
	bhi	.L1633
	add	w11, w11, 1
	ldr	x1, [x29,112]
	cmp	w11, w27
	add	w0, w0, w9
	add	x10, x10, x1
	blt	.L1632
	mov	w21, 255
	str	w21, [x24,2056]
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	nandc_wait_flash_ready
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L1635
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
	b	.L1685
.L1635:
	mov	w0, 190
	cbnz	w7, .L1685
	mov	w0, 56
.L1685:
	str	w0, [x24,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1593
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
	bhi	.L1687
	bl	hynix_get_read_retry_default
.L1687:
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
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x19, .LANCHOR0
	strb	wzr, [x0,1496]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,516]
	cmp	w1, 9
	bne	.L1690
	ldr	x22, [x0,536]
	ldr	w0, [x22,16]
	str	w0, [x29,64]
	ldr	w20, [x22,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1711
	adrp	x21, .LC144
	adrp	x23, .LC143
	add	x21, x21, :lo12:.LC144
	add	x23, x23, :lo12:.LC143
	b	.L1692
.L1711:
	mov	w20, 0
.L1691:
	ldr	w1, [x22,64]
	ldr	w0, [x29,64]
	ubfx	x1, x1, 16, 6
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1694
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1693
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1693
	ldr	w1, [x29,72]
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1694
.L1693:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1695
	ldr	w2, [x22,64]
	adrp	x0, .LC142
	ldr	w3, [x29,64]
	mov	w1, w20
	ubfx	x2, x2, 16, 5
	add	x0, x0, :lo12:.LC142
	ubfx	x3, x3, 22, 6
	add	x21, x21, :lo12:.LANCHOR3
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	mov	w2, 4
	mov	w3, 64
	ldr	x1, [x1,536]
	bl	rknand_print_hex
	ldr	w1, [x29,72]
	mov	w0, 1
	strb	w0, [x21,1496]
	tbnz	x1, 13, .L1694
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	b	.L1694
.L1695:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1691
.L1694:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1699
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
	b	.L1736
.L1701:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,536]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1700
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x22,64]
	mov	x0, x21
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x24,536]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1700:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1692:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1701
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1699
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
	b	.L1735
.L1690:
	ldr	x21, [x0,536]
	ldr	w0, [x21,8]
	str	w0, [x29,64]
	ldr	w20, [x21,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1712
	adrp	x22, .LC144
	adrp	x23, .LC143
	add	x22, x22, :lo12:.LC144
	add	x23, x23, :lo12:.LC143
	b	.L1704
.L1712:
	adrp	x22, .LC142
	adrp	x23, .LC143
	mov	w20, 0
	add	x22, x22, :lo12:.LC142
	add	x23, x23, :lo12:.LC143
.L1703:
	ldr	w2, [x21,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1706
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1705
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1705
	ldr	w1, [x29,72]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	bl	printk
	b	.L1706
.L1705:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1707
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
	ldr	x1, [x1,536]
	bl	rknand_print_hex
.L1707:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1703
.L1706:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1699
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
.L1736:
	mov	w2, 0
	b	.L1734
.L1710:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,536]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1709
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x24,536]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1709:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1704:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1710
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1699
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
.L1735:
	mov	w2, 1
.L1734:
	bl	rknand_dma_unmap_single
.L1699:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,712]
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
	cbnz	w20, .L1767
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,516]
	cmp	w2, 9
	bne	.L1739
	ldr	x5, [x1,536]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1740:
	cmp	w3, w22
	bcs	.L1776
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
	tbnz	x1, 2, .L1759
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1759
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1742
	ubfx	x1, x1, 3, 7
	b	.L1743
.L1742:
	ubfx	x1, x1, 19, 7
.L1743:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1741
.L1759:
	mov	w0, -1
.L1741:
	add	w3, w3, 1
	b	.L1740
.L1776:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w4, wzr
	ldr	x1, [x22,536]
	ldr	w20, [x1]
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1746
	adrp	x0, .LC146
	mov	w1, w20
	add	x0, x0, :lo12:.LC146
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,536]
	str	w20, [x0]
	mov	w0, -1
.L1746:
	tbz	x20, 13, .L1747
	adrp	x1, .LANCHOR3+1496
	ldrb	w1, [x1,#:lo12:.LANCHOR3+1496]
	cbz	w1, .L1747
	adrp	x0, .LC147
	mov	w1, w20
	add	x0, x0, :lo12:.LC147
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,536]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1748
.L1739:
	ldrb	w0, [x1,724]
	mov	w4, 128
	lsr	w5, w22, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
	mov	w3, w20
.L1750:
	cmp	w3, w5
	add	w6, w20, w4
	bcs	.L1777
	ldr	x0, [x1,680]
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
	b	.L1750
.L1777:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,536]
	mov	w0, w2
.L1752:
	cmp	w2, w22
	bcs	.L1778
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1762
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1762
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
	bls	.L1754
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1775
.L1754:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1775:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1753
.L1762:
	mov	w0, -1
.L1753:
	add	w2, w2, 1
	b	.L1752
.L1778:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,536]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1747
	adrp	x0, .LC148
	mov	w1, w20
	add	x0, x0, :lo12:.LC148
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,536]
	str	w20, [x0]
	b	.L1748
.L1747:
	cmn	w0, #1
	beq	.L1767
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1767
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1767
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1767
.L1748:
	mov	w0, -1
.L1767:
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
	stp	x29, x30, [sp, -80]!
	mov	w5, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	uxtb	w22, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	x25, x2
	mov	x24, x3
	ldrb	w19, [x0,744]
	mov	w26, w4
	ldr	x20, [x0,536]
	mov	w0, 24
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	adrp	x0, .LANCHOR2
	sub	w19, w19, #1
	ubfx	x23, x5, 24, 2
	and	w19, w1, w19
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1780
	adrp	x0, .LC149
	mov	w1, w22
	add	x0, x0, :lo12:.LC149
	mov	w2, w23
	mov	w3, w5
	bl	printk
.L1780:
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	nandc_cs
	cbnz	w23, .L1781
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	b	.L1782
.L1781:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,624]
	ldrb	w1, [x1,12]
	cmp	w1, 3
	bne	.L1783
	ldrb	w0, [x0,756]
	cbnz	w0, .L1783
	sxtw	x0, w22
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w23, [x0,8]
	b	.L1782
.L1783:
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
.L1782:
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
	mov	w0, 48
	str	w0, [x20,2056]
	cbz	w23, .L1784
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1784
	ldrb	w0, [x21,756]
	cbnz	w0, .L1784
	add	w19, w19, w19, lsl 1
	sub	w0, w19, #1
	add	w0, w0, w23
	b	.L1795
.L1784:
	mov	w0, w19
.L1795:
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
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
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
	ldrb	w0, [x20,724]
	mov	x24, x3
	uxtb	x25, w19
	mov	w21, 0
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	str	w0, [x29,156]
	bl	nandc_wait_flash_ready
	ldr	x0, [x20,536]
	str	x0, [x29,144]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29,128]
	lsl	x0, x25, 8
	str	x0, [x29,120]
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	str	x0, [x29,112]
.L1797:
	lsl	x0, x25, 8
	mov	w20, 0
	mov	w28, -1
	adrp	x26, .LANCHOR3
	str	x0, [x29,136]
.L1798:
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w0, [x0,1512]
	cmp	w20, w0
	bcs	.L1802
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
	tbz	x0, 12, .L1799
	ldr	x0, [x29,128]
	mov	w4, w6
	mov	w1, w20
	mov	w2, w22
	mov	w3, w28
	str	x6, [x29,104]
	bl	printk
	ldr	x6, [x29,104]
.L1799:
	cmn	w6, #1
	beq	.L1800
	adrp	x0, .LANCHOR3
	cmn	w28, #1
	add	x0, x0, :lo12:.LANCHOR3
	csel	w28, w28, w6, ne
	ldr	x23, [x0,1488]
	ldr	x24, [x0,1504]
	ldr	w0, [x29,156]
	cmp	w6, w0
	bcc	.L1810
.L1800:
	mov	w20, w27
	b	.L1798
.L1810:
	mov	w28, w6
.L1802:
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
	bcc	.L1804
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1804:
	cmn	w28, #1
	cset	w26, eq
	cbnz	w26, .L1812
	cmp	w28, 256
	cset	w1, eq
	cbz	w1, .L1805
.L1812:
	ldr	x0, [x29,112]
	mov	w1, w20
	mov	w2, w22
	mov	w3, w20
	mov	w4, w28
	bl	printk
	cbz	w26, .L1807
	cbnz	w21, .L1807
	mov	w0, w19
	mov	w1, 3
	mov	w21, 1
	bl	mt_auto_read_calibration_config
	b	.L1797
.L1807:
	cbz	w21, .L1808
	mov	w0, w19
	mov	w1, 0
	bl	mt_auto_read_calibration_config
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
	b	.L1808
.L1805:
	cbz	w21, .L1808
	mov	w0, w19
	mov	w28, 256
	bl	mt_auto_read_calibration_config
.L1808:
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
	ldr	x25, [x1,536]
	lsl	x0, x0, 8
	ldrb	w1, [x1,725]
	add	x23, x25, x0
	cmp	w1, 36
	csel	w2, w3, w2, ne
	str	w2, [x29,112]
	cmp	w1, 36
	mov	w1, 26
	csel	w21, w21, w1, ne
	cbnz	w19, .L1833
	str	x0, [x29,112]
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	mov	w19, -1
	mov	w28, 1
	str	x0, [x29,104]
.L1840:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1834
	mov	w1, w28
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x25, x0
	str	w1, [x0,8]
	b	.L1835
.L1834:
	mov	w1, w28
	bl	toshiba_3d_set_slc_rr_para
.L1835:
	ldr	w4, [x29,124]
	mov	w0, w22
	mov	w1, w24
	mov	x2, x26
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1836
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w28
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1836:
	cmn	w4, #1
	beq	.L1837
	adrp	x0, .LANCHOR3
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR3
	csel	w19, w19, w4, ne
	ldr	x26, [x0,1488]
	ldr	x27, [x0,1504]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1856
.L1837:
	add	w28, w28, 1
	cmp	w28, w21
	bne	.L1840
	b	.L1839
.L1856:
	mov	w21, w28
	mov	w19, w4
.L1839:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1841
	mov	w1, 0
	mov	w2, w1
	b	.L1879
.L1841:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1842
.L1833:
	mov	x28, x0
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,104]
.L1849:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1843
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x25, x28
	mov	w1, 93
	b	.L1878
.L1843:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x25, x28
	mov	w1, 38
.L1878:
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
	tbz	x0, 4, .L1845
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1845:
	cmn	w4, #1
	beq	.L1846
	adrp	x0, .LANCHOR3
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR3
	csel	w19, w19, w4, ne
	ldr	x26, [x0,1488]
	ldr	x27, [x0,1504]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1857
.L1846:
	ldr	w0, [x29,112]
	add	w21, w21, 1
	cmp	w21, w0
	bne	.L1849
	b	.L1848
.L1857:
	mov	w19, w4
.L1848:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1850
	mov	w1, 0
	mov	w2, 1
.L1879:
	bl	toshiba_tlc_set_rr_para
	b	.L1842
.L1850:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1842:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	bne	.L1851
	ubfiz	x22, x22, 8, 8
	mov	w0, 85
	add	x25, x25, x22
	str	w0, [x25,2056]
	mov	w0, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2048]
	str	w0, [x25,2056]
.L1851:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,724]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1852
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1852:
	cmn	w19, #1
	beq	.L1858
	cmp	w19, 256
	bne	.L1853
.L1858:
	adrp	x0, .LC154
	mov	w1, w21
	add	x0, x0, :lo12:.LC154
	mov	w2, w24
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1853:
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
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	uxtb	w23, w0
	str	w1, [x29,124]
	mov	x26, x2
	mov	x27, x3
	str	w4, [x29,120]
	adrp	x19, .LANCHOR0
	bl	nandc_wait_flash_ready
	mov	w25, 0
	add	x1, x19, :lo12:.LANCHOR0
	sxtw	x0, w23
	add	x21, x0, 8
	str	x0, [x29,112]
	ldrb	w0, [x1,725]
	ldr	x22, [x1,536]
	sub	w0, w0, #67
	add	x21, x22, x21, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1881
	ldrb	w0, [x1,672]
	cbz	w0, .L1882
	mov	w0, 1
	mov	w25, 1
	bl	nandc_set_if_mode
.L1882:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1881:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1883:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w0, [x0,1512]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1914
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1884
	bl	sandisk_set_rr_para
	b	.L1885
.L1884:
	bl	toshiba_set_rr_para
.L1885:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 34
	bne	.L1886
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w0, [x0,1512]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1886
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1886:
	ldr	x0, [x29,96]
	mov	w1, 38
	ldr	w4, [x29,120]
	mov	x2, x26
	add	x0, x22, x0
	mov	x3, x27
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	mov	w0, w23
	ldr	w1, [x29,124]
	bl	flash_read_page
	cmn	w0, #1
	mov	w28, w0
	beq	.L1889
	cmn	w24, #1
	csel	w24, w24, w0, ne
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x26, [x0,1488]
	ldr	x27, [x0,1504]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1891
.L1889:
	add	w20, w20, 1
	b	.L1883
.L1914:
	mov	w28, w24
.L1891:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1893
	bl	sandisk_set_rr_para
	b	.L1894
.L1893:
	bl	toshiba_set_rr_para
.L1894:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,724]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1895
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1895:
	cmn	w28, #1
	beq	.L1901
	cmp	w28, 256
	bne	.L1896
.L1901:
	adrp	x0, .LC154
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC154
	mov	w1, w20
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1896:
	bl	nandc_wait_flash_ready
	cbz	w25, .L1898
	mov	w0, 4
	bl	nandc_set_if_mode
.L1898:
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
	.global	hynix_read_retrial
	.type	hynix_read_retrial, %function
hynix_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x27, x28, [sp,80]
	adrp	x28, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	x21, w0
	add	x0, x28, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	x25, x2
	mov	w23, w1
	ldr	x5, [x0,728]
	mov	x26, x3
	str	w4, [x29,124]
	mov	x22, x21
	add	x27, x5, 112
	mov	w19, -1
	add	x0, x27, x21
	ldrb	w24, [x27,2]
	ldrb	w20, [x0,8]
	bl	nandc_wait_flash_ready
	mov	w6, 0
	adrp	x7, .LANCHOR3
.L1916:
	cmp	w6, w24
	bcs	.L1920
	add	w20, w20, 1
	mov	w0, w22
	str	x7, [x29,104]
	uxtb	w20, w20
	str	x6, [x29,112]
	cmp	w20, w24
	csel	w20, w20, wzr, cc
	mov	w1, w20
	bl	hynix_set_rr_para
	ldr	w4, [x29,124]
	mov	w0, w22
	mov	w1, w23
	mov	x2, x25
	mov	x3, x26
	bl	flash_read_page
	cmn	w0, #1
	ldr	x6, [x29,112]
	ldr	x7, [x29,104]
	beq	.L1918
	add	x1, x7, :lo12:.LANCHOR3
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1488]
	ldr	x26, [x1,1504]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1925
.L1918:
	add	w6, w6, 1
	b	.L1916
.L1925:
	mov	w19, w0
.L1920:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,724]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1922
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1922:
	cmn	w19, #1
	beq	.L1926
	cmp	w19, 256
	bne	.L1923
.L1926:
	adrp	x0, .LC155
	mov	w1, w6
	add	x0, x0, :lo12:.LC155
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1923:
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
	adrp	x0, .LC156
	str	w27, [x29,124]
	add	x0, x0, :lo12:.LC156
	mov	w25, w27
	mov	w28, w27
	mov	w19, 1024
	mov	w26, 6
	mov	w21, -1
	str	x0, [x29,104]
.L1941:
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
	tbz	x0, 4, .L1936
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w26
	mov	w2, w22
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1936:
	add	w0, w19, 1
	cmp	w4, w0
	bhi	.L1937
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x24, [x0,1488]
	ldr	x23, [x0,1504]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	cmp	w4, w0, lsr 2
	bcs	.L1947
	add	w28, w28, 1
	cmp	w28, 7
	bls	.L1947
	sub	w27, w26, w28
	mov	w19, w4
	mov	w21, 0
	b	.L1939
.L1937:
	cmp	w25, w28
	bcs	.L1948
	sub	w0, w27, w28
	cmp	w28, 7
	str	w0, [x29,124]
	bhi	.L1940
	mov	w25, w28
	b	.L1948
.L1947:
	mov	w27, w26
	mov	w19, w4
	mov	w21, 0
	b	.L1938
.L1948:
	mov	w28, 0
.L1938:
	add	w26, w26, 2
	cmp	w26, 50
	bne	.L1941
.L1939:
	ldr	w0, [x29,124]
	cmp	w25, w28
	csel	w27, w27, w0, ls
.L1940:
	cbz	w27, .L1942
	adrp	x0, .LANCHOR0+724
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+724]
	udiv	w0, w0, w1
	cmp	w19, w0
	bcs	.L1942
	adrp	x0, .LC157
	mov	w1, w27
	add	x0, x0, :lo12:.LC157
	bl	printk
	mov	w0, w27
	b	.L1957
.L1942:
	ldrb	w0, [x29,116]
.L1957:
	bl	nandc_set_ddr_para
	cbz	w21, .L1944
	adrp	x0, .LC158
	mov	w2, w22
	mov	w1, w20
	add	x0, x0, :lo12:.LC158
	adrp	x26, .LANCHOR0
	bl	printk
	add	x27, x26, :lo12:.LANCHOR0
	mov	w0, w20
	bl	flash_reset
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	add	x0, x27, x20, sxtw
	mov	w1, 2
	strb	w1, [x0,752]
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	ldr	w4, [x29,120]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	bl	flash_read_page
	mov	w19, w0
	adrp	x0, .LC159
	mov	w1, w20
	add	x0, x0, :lo12:.LC159
	mov	w2, w22
	mov	w3, w19
	bl	printk
	ldrb	w0, [x27,724]
	cmp	w19, w0
	bhi	.L1949
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldr	w0, [x1,1516]
	add	w0, w0, 1
	str	w0, [x1,1516]
	cmp	w0, 100
	bls	.L1945
	strb	wzr, [x27,672]
	b	.L1944
.L1949:
	mov	w19, w21
.L1945:
	add	x26, x26, :lo12:.LANCHOR0
	ldrb	w0, [x26,792]
	bl	flash_set_interface_mode
	ldrb	w0, [x26,792]
	bl	nandc_set_if_mode
.L1944:
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
	ldrb	w0, [x0,633]
	mov	w22, w4
	cmp	w0, w25
	bhi	.L1959
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 405
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1959:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, x25, sxtw
	ldrb	w21, [x1,736]
	ldrb	w4, [x0,633]
	cmp	w25, w4
	bcc	.L1960
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L1986
	adrp	x0, .LC160
	mov	w1, w21
	add	x0, x0, :lo12:.LC160
	mov	w2, w25
	mov	w3, w19
	bl	printk
.L1986:
	mov	w0, -1
	b	.L1985
.L1960:
	ubfx	x1, x19, 24, 2
	cbnz	w1, .L1963
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L1964
	ldrb	w0, [x0,1]
	cbz	w0, .L1963
.L1964:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,1]
	sub	w19, w19, w0
	cbz	w2, .L1965
	add	w19, w0, w19, lsl 1
	b	.L1963
.L1965:
	add	x1, x1, 4
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1963:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1985
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,720]
	cbnz	w26, .L1967
.L1970:
	adrp	x0, .LANCHOR3+1520
	ldr	x5, [x0,#:lo12:.LANCHOR3+1520]
	cbnz	x5, .L1968
	b	.L1969
.L1967:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	strb	wzr, [x25,720]
	bl	flash_read_page
	strb	w26, [x25,720]
	cmn	w0, #1
	beq	.L1970
	b	.L1985
.L1968:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	blr	x5
	cmn	w0, #1
	bne	.L1985
.L1969:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,720]
	bl	printk
	ldrb	w0, [x20,672]
	cbz	w0, .L1986
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_ddr_tuning_read
.L1985:
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
	adrp	x0, .LANCHOR3+1380
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x24, x2
	ldrh	w19, [x0,#:lo12:.LANCHOR3+1380]
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
	bne	.L1988
	mov	w28, 0
	mov	w5, 2
.L1989:
	cmp	w28, w19
	bgt	.L1988
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
	bne	.L1990
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1989
.L1990:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L1989
.L1988:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1993
	adrp	x0, .LC162
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC162
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L1993:
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
	adrp	x6, .LANCHOR0
	uxth	w7, w0
	add	x6, x6, :lo12:.LANCHOR0
	mov	w5, 24
	stp	x29, x30, [sp, -16]!
	mov	x9, x1
	mov	x8, x2
	add	x29, sp, 0
	ldrb	w0, [x6,744]
	mov	w4, w3
	mov	x2, x9
	mov	x3, x8
	sub	w5, w5, w0
	ldrh	w0, [x6,3536]
	sub	w0, w5, w0
	mov	w5, 1
	lsl	w5, w5, w0
	asr	w0, w7, w0
	sub	w1, w5, #1
	and	w1, w7, w1
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
	adrp	x20, .LANCHOR3
	stp	x21, x22, [sp,32]
	add	x21, x19, :lo12:.LANCHOR0
	mov	w22, 1
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	mov	w23, w1
	ldrb	w0, [x21,792]
	add	x20, x20, :lo12:.LANCHOR3
	strb	w22, [x21,672]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,792]
	bl	nandc_set_if_mode
	ldr	x2, [x20,1528]
	mov	w1, w23
	ldr	x3, [x20,1536]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tuning_read
	ldr	x2, [x20,1528]
	mov	w0, w24
	ldr	x3, [x20,1536]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1999
	ldrb	w0, [x21,792]
	tbz	x0, 0, .L1999
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,672]
	b	.L2000
.L1999:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,672]
.L2000:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.type	id_block_read_data.constprop.30, %function
id_block_read_data.constprop.30:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	w26, w0
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	add	x0, x0, 8
	mov	w27, w1
	ldrb	w1, [x0,9]
	mov	x21, x2
	str	w1, [x29,164]
	mov	w22, 0
	ldrh	w0, [x0,26]
	mov	w23, 4
	mov	w25, w22
	mul	w0, w1, w0
	adrp	x1, .LANCHOR0
	str	x1, [x29,120]
	add	x19, x1, :lo12:.LANCHOR0
	uxth	w0, w0
	ldrb	w2, [x19,720]
	str	w2, [x29,136]
	udiv	w2, w26, w0
	strb	wzr, [x19,720]
	msub	w0, w2, w0, w26
	str	w0, [x29,172]
	sub	w0, w26, w0
	str	w0, [x29,160]
	ldr	w0, [x29,172]
	and	w0, w0, 3
	str	w0, [x29,168]
	adrp	x0, .LC163
	add	x0, x0, :lo12:.LC163
	str	x0, [x29,112]
.L2005:
	cmp	w22, w27
	bcs	.L2042
	ldr	w0, [x29,168]
	ldrb	w1, [x19,634]
	sub	w0, w23, w0
	uxth	w0, w0
	str	w0, [x29,144]
	ldr	w0, [x29,172]
	add	w20, w22, w0
	add	x0, x19, 4
	udiv	w20, w20, w23
	and	w20, w20, 65535
	ldrh	w0, [x0,w20,sxtw 1]
	cbnz	w1, .L2006
	mov	w20, w0
	b	.L2007
.L2006:
	ldrb	w1, [x19,1]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L2007:
	ldr	w1, [x29,160]
	ldr	w0, [x29,164]
	ldrb	w28, [x19,632]
	madd	w0, w20, w0, w1
	ldr	w1, [x29,168]
	str	w0, [x29,140]
	add	w24, w0, w1
	ldr	x0, [x19,624]
	ldrb	w0, [x0,9]
	udiv	w24, w24, w0
	ldrb	w0, [x19,724]
	str	w0, [x29,132]
	mov	w0, w28
	bl	nandc_bch_sel
.L2008:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L2015
	ldrb	w6, [x19,720]
	cbnz	w6, .L2010
.L2013:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x6, [x0,1520]
	cbnz	x6, .L2011
	b	.L2012
.L2010:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	str	x6, [x29,104]
	strb	wzr, [x19,720]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x6, [x29,104]
	strb	w6, [x19,720]
	beq	.L2013
	b	.L2041
.L2011:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	blr	x6
	cmn	w0, #1
	bne	.L2041
.L2012:
	ldrb	w0, [x19,672]
	cbz	w0, .L2015
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tuning_read
.L2041:
	mov	w20, w0
.L2015:
	cmn	w20, #1
	bne	.L2016
	cmp	w28, 16
	beq	.L2016
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L2008
.L2016:
	ldr	w0, [x29,132]
	bl	nandc_bch_sel
	cmn	w20, #1
	ldr	w0, [x29,160]
	csinv	w25, w25, wzr, ne
	cmp	w22, wzr
	cset	w1, eq
	cmp	w0, w26
	cset	w0, eq
	tst	w1, w0
	beq	.L2018
	cbnz	w25, .L2018
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L2018
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L2019
.L2018:
	ldr	x0, [x29,144]
	mov	w2, w22
	ldr	w1, [x29,140]
	ubfiz	x0, x0, 9, 16
	ldr	w3, [x29,176]
	add	x21, x21, x0
	ldr	w4, [x29,180]
	ldr	x0, [x29,112]
	bl	printk
	str	wzr, [x29,168]
.L2019:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L2005
.L2042:
	ldr	x0, [x29,120]
	ldrb	w1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,720]
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	id_block_read_data.constprop.30, .-id_block_read_data.constprop.30
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	x23, w0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	adrp	x21, .LANCHOR2
	mov	x22, x23
	ldrb	w19, [x0,744]
	add	x23, x23, 8
	ldr	x20, [x0,536]
	mov	w0, 24
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	mov	w24, w1
	sub	w19, w19, #1
	mov	x25, x3
	mov	x26, x2
	mov	w3, w4
	add	x23, x20, x23, lsl 8
	and	w19, w1, w19
	ubfx	x27, x24, 24, 2
	tbz	x0, 4, .L2044
	adrp	x0, .LC164
	mov	w2, w27
	add	x0, x0, :lo12:.LC164
	bl	printk
.L2044:
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	hynix_reconfig_rr_para
	mov	w0, w22
	bl	nandc_cs
	mov	w0, w22
	cbnz	w27, .L2045
	bl	zftl_flash_enter_slc_mode
	b	.L2046
.L2045:
	bl	zftl_flash_exit_slc_mode
.L2046:
	ubfiz	x5, x22, 8, 8
	mov	w0, 128
	add	x20, x20, x5
	add	x21, x21, :lo12:.LANCHOR2
	str	w0, [x20,2056]
	and	w0, w19, 255
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	mov	w0, w19
	bl	nandc_set_seed
	ldrb	w2, [x21,17]
	mov	w1, 1
	mov	x3, x26
	mov	x4, x25
	mov	w0, w22
	bl	nandc_xfer
	mov	w0, 16
	str	w0, [x20,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w22
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2047
	adrp	x0, .LC165
	mov	w1, w24
	add	x0, x0, :lo12:.LC165
	bl	printk
	mov	w2, -1
.L2047:
	mov	w0, w2
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	adrp	x19, .LANCHOR3
	uxth	w20, w1
	add	x19, x19, :lo12:.LANCHOR3
	mov	w1, 165
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	ldr	x0, [x19,1488]
	bl	ftl_memset
	ldr	x0, [x19,1504]
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
	bne	.L2055
.L2057:
	mov	w19, -1
	b	.L2056
.L2055:
	adrp	x21, .LANCHOR2
	ldr	x2, [x19,1488]
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x3, [x19,1504]
	mov	w0, w22
	mov	w1, w20
	add	x21, x21, 8
	ldrb	w4, [x21,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2057
	ldr	x2, [x19,1488]
	mov	w0, w22
	ldr	x3, [x19,1504]
	mov	w1, w20
	ldrb	w4, [x21,9]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2057
	ldr	x0, [x19,1488]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L2057
	ldr	x0, [x19,1504]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csetm	w19, ne
.L2056:
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
	.global	flash_dual_page_prog
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x21, w0
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w22, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	x28, x2
	mov	x20, x21
	ldrb	w19, [x0,744]
	add	x21, x21, 8
	ldr	x23, [x0,536]
	mov	w0, 24
	sub	w19, w0, w19
	mov	w0, 1
	lsl	w19, w0, w19
	adrp	x0, .LANCHOR2
	str	x0, [x29,104]
	sub	w19, w19, #1
	mov	x27, x3
	mov	x26, x4
	ldr	w2, [x0,#:lo12:.LANCHOR2]
	mov	x25, x5
	mov	w3, w6
	add	x21, x23, x21, lsl 8
	and	w19, w1, w19
	ubfx	x24, x22, 24, 2
	tbz	x2, 4, .L2065
	adrp	x0, .LC164
	mov	w2, w24
	add	x0, x0, :lo12:.LC164
	bl	printk
.L2065:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w24, .L2066
	bl	zftl_flash_enter_slc_mode
	b	.L2067
.L2066:
	bl	zftl_flash_exit_slc_mode
.L2067:
	ubfiz	x0, x20, 8, 8
	mov	w5, 128
	add	x23, x23, x0
	and	w0, w19, 255
	str	x5, [x29,96]
	str	w5, [x23,2056]
	str	wzr, [x23,2052]
	str	wzr, [x23,2052]
	str	w0, [x23,2052]
	lsr	w0, w19, 8
	str	w0, [x23,2052]
	lsr	w0, w19, 16
	str	w0, [x23,2052]
	mov	w0, w19
	bl	nandc_set_seed
	ldr	x0, [x29,104]
	mov	x4, x27
	mov	x3, x28
	mov	w1, 1
	add	x24, x0, :lo12:.LANCHOR2
	mov	w0, w20
	mov	w27, 16
	ldrb	w2, [x24,17]
	bl	nandc_xfer
	str	w27, [x23,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x29,96]
	add	w0, w19, 1
	str	w5, [x23,2056]
	and	w1, w0, 255
	str	wzr, [x23,2052]
	str	wzr, [x23,2052]
	str	w1, [x23,2052]
	lsr	w1, w0, 8
	str	w1, [x23,2052]
	lsr	w1, w0, 16
	str	w1, [x23,2052]
	bl	nandc_set_seed
	ldrb	w2, [x24,17]
	mov	w1, 1
	mov	x3, x26
	mov	x4, x25
	mov	w0, w20
	bl	nandc_xfer
	str	w27, [x23,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x21
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w20
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2068
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2069
	adrp	x0, .LC165
	mov	w1, w22
	add	x0, x0, :lo12:.LC165
	bl	printk
.L2069:
	mov	w2, -1
.L2068:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.29, %function
fw_flash_page_prog.constprop.29:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x4, x4, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	x21, x1
	str	x2, [x29,56]
	ldr	x1, [x4,624]
	ldrb	w22, [x4,724]
	ldrb	w19, [x1,9]
	udiv	w19, w0, w19
	ldrb	w0, [x4,632]
	bl	nandc_bch_sel
	adrp	x0, .LANCHOR2+15
	ldr	x3, [x29,56]
	ldrb	w0, [x0,#:lo12:.LANCHOR2+15]
	cmp	w0, 9
	bne	.L2077
	adrp	x20, .LANCHOR3
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR3
	mov	w2, 16384
	ldr	x0, [x20,1488]
	bl	ftl_memset
	ldr	x4, [x20,1488]
	mov	w1, w19
	ldr	x3, [x29,56]
	mov	x2, x21
	mov	x5, x4
	mov	w6, 4
	mov	w0, 0
	bl	flash_dual_page_prog
	b	.L2079
.L2077:
	mov	w0, 0
	mov	w1, w19
	mov	x2, x21
	mov	w4, 4
	bl	flash_prog_page
.L2079:
	mov	w19, w0
	mov	w0, w22
	bl	nandc_bch_sel
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	fw_flash_page_prog.constprop.29, .-fw_flash_page_prog.constprop.29
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
	ldrb	w0, [x0,633]
	uxtb	w22, w2
	mov	w20, w4
	mov	x24, x5
	cmp	w0, w25
	mov	x23, x6
	mov	x19, x7
	bhi	.L2081
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 749
	add	x1, x1, 160
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2081:
	add	x7, x19, :lo12:.LANCHOR0
	ldrb	w0, [x7,633]
	cmp	w0, w25
	bls	.L2080
	add	x25, x7, x25, sxtw
	ldrb	w25, [x25,736]
	ldr	x19, [x7,536]
	mov	w0, w25
	bl	nandc_cs
	cbz	w26, .L2083
	sxtw	x0, w25
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w26, [x0,8]
.L2083:
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
.L2080:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x23, [sp,48]
	add	x22, x22, :lo12:.LANCHOR0
	mov	x21, x0
	stp	x19, x20, [sp,16]
	mov	w23, 1
	ldr	x6, [x0]
	mov	w0, 24
	ldrb	w2, [x22,744]
	sub	w0, w0, w2
	ldr	w1, [x6,40]
	lsl	w20, w23, w0
	lsl	w2, w23, w2
	sub	w20, w20, #1
	sub	w19, w2, #1
	and	w20, w20, w1
	lsr	w1, w1, w0
	and	w19, w19, w1
	ldr	x5, [x6,8]
	ldrb	w0, [x6,60]
	mov	w1, w23
	uxtb	w19, w19
	ldr	x6, [x6,24]
	mov	w3, w19
	mov	w4, w20
	mov	w2, 26
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,8]
	mov	w3, w19
	ldr	x0, [x21]
	mov	w4, w20
	mov	w1, 2
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x21,16]
	mov	w3, w19
	ldr	x0, [x21]
	mov	w4, w20
	mov	w2, 16
	mov	w1, 3
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	ldr	x1, [x21]
	mov	w0, 4
	strb	w0, [x1,58]
	mov	w0, -1
	strb	w23, [x1,59]
	strb	w0, [x1]
	add	x0, x22, 2851
	bl	buf_add_tail
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	stp	x29, x30, [sp, -48]!
	mov	w3, 24
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x20, x0
	mov	w22, 1
	ldr	w2, [x0,40]
	adrp	x0, .LANCHOR0+744
	ldrb	w19, [x0,#:lo12:.LANCHOR0+744]
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	sub	w3, w3, w19
	lsl	w19, w22, w19
	lsl	w21, w22, w3
	sub	w19, w19, #1
	sub	w21, w21, #1
	lsr	w3, w2, w3
	and	w21, w21, w2
	and	w19, w19, w3
	mov	w1, w21
	mov	w3, w2
	uxtb	w19, w19
	bl	printk
	ldr	x5, [x20,8]
	mov	w1, w22
	ldr	x6, [x20,24]
	mov	w3, w19
	mov	w4, w21
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20,8]
	mov	w3, w19
	ldr	x6, [x20,24]
	mov	w4, w21
	mov	w1, 2
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20,8]
	mov	w3, w19
	ldr	x6, [x20,24]
	mov	w4, w21
	mov	w2, 16
	mov	w1, 3
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	w0, [x20,40]
	mov	w1, 64
	bl	flash_wait_device_ready
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	ldrb	w0, [x0,633]
	mov	x19, x5
	cmp	w0, w20
	bhi	.L2090
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 781
	add	x1, x1, 192
	add	x0, x0, :lo12:.LC0
	str	x4, [x29,48]
	str	x3, [x29,56]
	bl	printk
	bl	dump_stack
	ldr	x4, [x29,48]
	ldr	x3, [x29,56]
.L2090:
	add	x5, x19, :lo12:.LANCHOR0
	ldrb	w0, [x5,633]
	cmp	w0, w20
	bls	.L2089
	add	x20, x5, x20, sxtw
	ldrb	w20, [x20,736]
	ldr	x19, [x5,536]
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
.L2089:
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
	adrp	x0, .LANCHOR0+744
	ldrb	w19, [x0,#:lo12:.LANCHOR0+744]
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
	ldr	w2, [x20,40]
	adrp	x0, .LC167
	mov	w1, w21
	add	x0, x0, :lo12:.LC167
	add	w3, w2, 1
	bl	printk
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
	adrp	x26, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x26, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	w19, 24
	mov	w24, w1
	ldrb	w20, [x0,744]
	mov	x23, x2
	ldrb	w0, [x0,633]
	mov	x22, x3
	sub	w19, w19, w20
	lsl	w20, w5, w20
	lsr	w1, w1, w19
	sub	w20, w20, #1
	and	w20, w1, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2094
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 832
	add	x1, x1, 224
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2094:
	add	x28, x26, :lo12:.LANCHOR0
	ldrb	w0, [x28,633]
	cmp	w0, w20
	bls	.L2093
	mov	w0, 1
	ldr	x27, [x28,536]
	lsl	w19, w0, w19
	sub	w19, w19, #1
	add	x0, x28, x20, sxtw
	and	w19, w24, w19
	ldrb	w25, [x0,736]
	bl	nandc_rdy_status
	cbnz	w0, .L2096
	ldrb	w0, [x28,633]
	cmp	w0, 1
	bne	.L2097
	bl	nandc_wait_flash_ready
	b	.L2096
.L2097:
	mov	w0, w20
	mov	w1, w19
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L2096:
	mov	w0, w25
	ubfx	x24, x24, 24, 2
	bl	hynix_reconfig_rr_para
	mov	w0, w25
	bl	nandc_cs
	cbnz	w24, .L2098
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
	ldrb	w0, [x26,#:lo12:.LANCHOR0]
	cbz	w0, .L2099
	mov	w0, w25
	bl	zftl_flash_enter_slc_mode
	b	.L2099
.L2098:
	mov	w0, w25
	bl	zftl_flash_exit_slc_mode
.L2099:
	ubfiz	x1, x25, 8, 8
	mov	w0, 128
	add	x20, x27, x1
	str	w0, [x20,2056]
	and	w0, w19, 255
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	mov	w0, w19
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	w0, w25
	mov	w1, 1
	mov	x3, x23
	mov	x4, x22
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	str	w21, [x20,2056]
	mov	w0, w25
	bl	nandc_de_cs
.L2093:
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
	ldrb	w2, [x3,2851]
	cmp	w2, 255
	beq	.L2104
	ldrb	w5, [x3,744]
	mov	w1, 1
	mov	w7, 24
	add	x3, x3, 800
	lsl	w1, w1, w5
	sub	w7, w7, w5
	sub	w1, w1, #1
	asr	w4, w4, w7
	uxth	w1, w1
	and	w4, w1, w4
.L2106:
	add	x5, x3, x2, lsl 6
	ldr	w6, [x5,40]
	lsr	w6, w6, w7
	and	w6, w1, w6
	cmp	w4, w6
	bne	.L2105
	ldrb	w6, [x5,58]
	add	x5, x5, 48
	cmp	w6, 7
	bne	.L2105
	mov	w1, 3
	strb	w1, [x5,10]
	b	.L2104
.L2105:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L2106
.L2104:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 2851
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
	ldrb	w20, [x0,744]
	mov	w1, 1
	mov	x21, x2
	ldrb	w0, [x0,633]
	sub	w19, w19, w20
	lsl	w20, w1, w20
	lsr	w2, w24, w19
	sub	w20, w20, #1
	and	w20, w2, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2112
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 921
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2112:
	add	x3, x23, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w1, [x3,633]
	cmp	w1, w20
	bls	.L2113
	mov	w2, 1
	add	x20, x3, x20, sxtw
	ldrb	w20, [x20,736]
	lsl	w19, w2, w19
	add	w19, w19, w0
	ubfx	x25, x24, 24, 2
	mov	w0, w20
	and	w19, w19, w24
	ldr	x24, [x3,536]
	bl	nandc_cs
	cbnz	w25, .L2114
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2114:
	add	x0, x23, :lo12:.LANCHOR0
	uxtb	x5, w20
	and	w4, w19, 255
	lsr	w3, w19, 8
	lsr	w2, w19, 16
	ldrb	w1, [x0,656]
	cmp	w1, 1
	bne	.L2115
	add	x5, x24, x5, lsl 8
	mov	w0, 6
	b	.L2129
.L2115:
	ldr	x0, [x0,624]
	add	x5, x24, x5, lsl 8
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2117
	mov	w0, 5
.L2129:
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	str	w2, [x5,2052]
	b	.L2127
.L2117:
	str	wzr, [x5,2056]
	mov	w0, 5
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	str	w2, [x5,2052]
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
.L2127:
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w25, .L2118
	add	x23, x23, :lo12:.LANCHOR0
	ldr	x0, [x23,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2118
	ldrb	w0, [x23,756]
	cbnz	w0, .L2118
	sub	w1, w25, #1
	add	w0, w19, w19, lsl 1
	add	w0, w1, w0
	b	.L2128
.L2118:
	mov	w0, w19
.L2128:
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
.L2113:
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
	ldrb	w2, [x1,744]
	ubfx	x25, x21, 24, 2
	sub	w0, w0, w2
	lsl	w19, w5, w0
	lsl	w5, w5, w2
	lsr	w20, w21, w0
	sub	w5, w5, #1
	and	w20, w20, w5
	ldrb	w0, [x1,633]
	sub	w19, w19, #1
	uxtb	w20, w20
	and	w19, w19, w21
	cmp	w0, w20
	bhi	.L2131
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1045
	add	x1, x1, 280
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2131:
	add	x0, x24, :lo12:.LANCHOR0
	add	x20, x0, x20, sxtw
	ldrb	w26, [x20,736]
	ldr	x20, [x0,536]
	mov	w0, w26
	bl	nandc_cs
	cbnz	w25, .L2132
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2132:
	ubfiz	x0, x26, 8, 8
	add	x1, x20, x0
	mov	w0, 5
	str	w0, [x1,2056]
	and	w0, w19, 255
	str	wzr, [x1,2052]
	str	wzr, [x1,2052]
	str	w0, [x1,2052]
	lsr	w0, w19, 8
	str	w0, [x1,2052]
	lsr	w0, w19, 16
	str	w0, [x1,2052]
	mov	w0, 224
	str	w0, [x1,2056]
	cbz	w25, .L2133
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0,624]
	ldrb	w1, [x1,12]
	cmp	w1, 3
	bne	.L2133
	ldrb	w0, [x0,756]
	cbnz	w0, .L2133
	sub	w0, w25, #1
	add	w1, w19, w19, lsl 1
	add	w0, w0, w1
	b	.L2165
.L2133:
	mov	w0, w19
.L2165:
	adrp	x20, .LANCHOR2
	bl	nandc_set_seed
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w26
	mov	w1, 0
	mov	x3, x22
	mov	x4, x23
	add	x27, x27, 8
	ldrb	w2, [x27,9]
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L2135
	add	x28, x24, :lo12:.LANCHOR0
	ldrb	w5, [x28,720]
	cbz	w5, .L2136
	ldrb	w4, [x27,9]
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	str	x5, [x29,104]
	strb	wzr, [x28,720]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,720]
	cbnz	w25, .L2137
.L2144:
	ldrb	w2, [x24,#:lo12:.LANCHOR0]
	add	x1, x24, :lo12:.LANCHOR0
	cbz	w2, .L2137
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2137
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w1, [x20,27]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 4
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L2152
.L2137:
	cmn	w0, #1
	bne	.L2152
.L2145:
	adrp	x0, .LANCHOR3+1520
	ldr	x5, [x0,#:lo12:.LANCHOR3+1520]
	cbnz	x5, .L2140
.L2143:
	add	x24, x24, :lo12:.LANCHOR0
	adrp	x0, .LC168
	add	x0, x0, :lo12:.LC168
	mov	w1, 0
	mov	w2, w21
	mov	w3, -1
	ldrb	w4, [x24,720]
	bl	printk
	ldrb	w0, [x24,672]
	cbnz	w0, .L2141
	mov	w0, -1
	b	.L2152
.L2140:
	add	x4, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x4,17]
	blr	x5
	cmn	w0, #1
	bne	.L2152
	b	.L2143
.L2141:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x20,17]
	bl	flash_ddr_tuning_read
	b	.L2152
.L2135:
	cbz	w25, .L2144
	b	.L2152
.L2136:
	cbz	w25, .L2144
	b	.L2145
.L2152:
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
	ldrb	w22, [x0,2851]
	cmp	w22, 255
	beq	.L2260
	add	x0, x0, 800
	mov	w20, 0
	add	x0, x0, x22, lsl 6
	ldrb	w1, [x0,58]
	ldr	w24, [x0,40]
	sub	w2, w1, #1
	cmp	w2, 10
	bhi	.L2168
	adrp	x0, .L2170
	add	x0, x0, :lo12:.L2170
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x2, .Lrtx2170
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx2170:
	.section	.rodata
	.align	0
	.align	2
.L2170:
	.2byte	(.L2169 - .Lrtx2170) / 4
	.2byte	(.L2171 - .Lrtx2170) / 4
	.2byte	(.L2172 - .Lrtx2170) / 4
	.2byte	(.L2172 - .Lrtx2170) / 4
	.2byte	(.L2172 - .Lrtx2170) / 4
	.2byte	(.L2172 - .Lrtx2170) / 4
	.2byte	(.L2173 - .Lrtx2170) / 4
	.2byte	(.L2174 - .Lrtx2170) / 4
	.2byte	(.L2175 - .Lrtx2170) / 4
	.2byte	(.L2172 - .Lrtx2170) / 4
	.2byte	(.L2175 - .Lrtx2170) / 4
	.text
.L2169:
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2260
	add	x3, x19, :lo12:.LANCHOR0
	add	x0, x3, 800
	add	x0, x0, x22, lsl 6
	ldr	x2, [x0,16]
	ldr	x1, [x0,8]
	cbz	x2, .L2177
	ldrb	w4, [x0,56]
	ldrb	w0, [x3,2848]
	cmp	w4, w0
	csel	x1, x1, x2, ne
.L2177:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w24
	add	x19, x19, 800
	add	x19, x19, x22, lsl 6
	ldr	x2, [x19,24]
	bl	flash_complete_page_read
	str	w0, [x19,52]
	mov	w0, 13
	strb	w0, [x19,58]
	b	.L2261
.L2171:
	mov	w1, 64
	mov	w0, w24
	bl	flash_wait_device_ready
	add	x2, x19, :lo12:.LANCHOR0
	lsl	x4, x22, 6
	add	x3, x2, 800
	add	x1, x3, x4
	ldrb	w20, [x3,x4]
	tbz	x0, 6, .L2260
	add	x3, x3, x20, lsl 6
	ldr	x0, [x1,16]
	ldr	x27, [x1,8]
	ldr	x24, [x3,8]
	cbz	x0, .L2178
	ldrb	w3, [x1,56]
	ldrb	w1, [x2,2848]
	cmp	w3, w1
	csel	x27, x27, x0, ne
.L2178:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 800
	add	x0, x0, x20, lsl 6
	ldr	x1, [x0,16]
	cbz	x1, .L2179
	ldrb	w3, [x0,56]
	ldrb	w0, [x2,2848]
	cmp	w3, w0
	csel	x24, x24, x1, ne
.L2179:
	add	x21, x19, :lo12:.LANCHOR0
	mov	x1, x27
	add	x21, x21, 800
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
	beq	.L2180
	ldr	w0, [x28,4]
	cmn	w0, #1
	beq	.L2181
	ldr	x1, [x26,8]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2181
.L2180:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x21, x4, 800
	mov	w3, 24
	add	x21, x21, x22, lsl 6
	ldrb	w2, [x4,744]
	add	x26, x21, 32
	ldrb	w4, [x4,2848]
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
	beq	.L2182
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2182
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2182
	adrp	x0, .LC169
	ldr	w1, [x26,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC169
	bl	printk
.L2182:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 800
	add	x0, x0, x22, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2181
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2181
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 360
	add	x1, x1, 312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2181:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x3, x3, 800
	cmn	w25, #1
	add	x22, x3, x22, lsl 6
	ldrb	w0, [x22,2]
	str	w23, [x22,52]
	orr	w0, w0, 8
	strb	w1, [x22,58]
	strb	w0, [x22,2]
	beq	.L2183
	add	x3, x3, x20, lsl 6
	ldr	w0, [x3,36]
	cmn	w0, #1
	beq	.L2185
	ldr	x1, [x3,24]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2185
.L2183:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w2, 24
	add	x21, x4, 800
	mov	w0, 1
	add	x21, x21, x20, lsl 6
	ldrb	w3, [x4,744]
	add	x22, x21, 32
	ldrb	w4, [x4,2848]
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
	beq	.L2187
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2187
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2187
	adrp	x0, .LC169
	ldr	w1, [x22,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC169
	bl	printk
.L2187:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 800
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2185
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2185
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 375
	add	x1, x1, 312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2185:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x19, x19, 800
	add	x19, x19, x20, lsl 6
	str	w23, [x19,52]
	strb	w1, [x19,58]
.L2261:
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2260
.L2172:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L2260
	mov	w20, 5
	ands	w20, w21, w20
	beq	.L2188
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 12
	add	x19, x19, 800
	mov	w2, w21
	add	x19, x19, x22, lsl 6
	mov	w4, 12
	mov	w20, w21
	strb	w0, [x19,58]
	adrp	x0, .LC170
	ldrb	w1, [x19,1]
	add	x0, x0, :lo12:.LC170
	ldr	w3, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2168
.L2188:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x23, x4, 800
	add	x23, x23, x22, lsl 6
	strb	w1, [x23,58]
	ldr	x1, [x4,2880]
	str	w20, [x23,52]
	ldr	w2, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2260
	ldrh	w0, [x23,50]
	cbnz	w0, .L2260
	ldrb	w3, [x4,744]
	mov	w0, 1
	adrp	x21, .LANCHOR3
	ldr	w1, [x23,40]
	lsl	w0, w0, w3
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 24
	sub	w0, w0, #1
	sub	w2, w2, w3
	ldrb	w4, [x4,2848]
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	bic	w1, w1, w2
	and	w0, w0, w3
	ldr	x2, [x21,1528]
	ldr	x3, [x21,1536]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2189
	ldr	x1, [x23,24]
	ldr	x0, [x21,1536]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2260
.L2189:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC171
	add	x19, x4, 800
	add	x0, x0, :lo12:.LC171
	add	x19, x19, x22, lsl 6
	ldrb	w4, [x4,720]
	ldrb	w1, [x19,1]
	ldr	w2, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2168
.L2175:
	add	x25, x19, :lo12:.LANCHOR0
	cmp	w1, 11
	mov	w0, 3
	mov	w5, 10
	csel	w5, w0, w5, ne
	mov	w4, 24
	ldrb	w1, [x25,744]
	mov	w0, 1
	add	x21, x25, 800
	sub	w4, w4, w1
	lsl	w0, w0, w1
	sub	w0, w0, #1
	lsr	w2, w24, w4
	and	w2, w0, w2
	add	x3, x21, x22, lsl 6
	uxth	w2, w2
.L2191:
	ldrb	w1, [x3]
	cmp	w1, 255
	beq	.L2262
	sxtw	x23, w1
	add	x3, x21, x23, lsl 6
	ldrb	w1, [x3,58]
	cmp	w1, w5
	bne	.L2191
	ldr	w1, [x3,40]
	lsr	w1, w1, w4
	and	w1, w0, w1
	cmp	w2, w1
	bne	.L2191
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w24
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	tbz	x20, 6, .L2259
	ands	w1, w20, 15
	beq	.L2195
	add	x21, x21, x22, lsl 6
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	mov	w2, w20
	mov	w4, 12
	ldrb	w1, [x21,1]
	ldr	w3, [x21,40]
	bl	printk
	mov	w0, 12
	strb	w0, [x21,58]
	mov	w0, -1
	str	w0, [x21,52]
	b	.L2194
.L2195:
	add	x20, x21, x22, lsl 6
	mov	w2, 13
	str	w1, [x20,52]
	ldr	x1, [x25,2880]
	strb	w2, [x20,58]
	ldr	w2, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2259
	ldrh	w0, [x20,50]
	cbnz	w0, .L2259
	ldrb	w3, [x25,744]
	mov	w0, 1
	adrp	x21, .LANCHOR3
	ldr	w1, [x20,40]
	lsl	w0, w0, w3
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 24
	sub	w0, w0, #1
	sub	w2, w2, w3
	ldrb	w4, [x25,2848]
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	bic	w1, w1, w2
	and	w0, w0, w3
	ldr	x2, [x21,1528]
	ldr	x3, [x21,1536]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2197
	ldr	x1, [x20,24]
	ldr	x0, [x21,1536]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2259
.L2197:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC173
	add	x20, x4, 800
	add	x0, x0, :lo12:.LC173
	add	x20, x20, x22, lsl 6
	ldrb	w4, [x4,720]
	ldrb	w1, [x20,1]
	ldr	w2, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
.L2259:
	mov	w20, 0
.L2194:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 800
	add	x19, x1, x22, lsl 6
	add	x1, x1, x23, lsl 6
	ldrb	w0, [x19,58]
	strb	w0, [x1,58]
	ldr	w0, [x19,52]
	str	w0, [x1,52]
	b	.L2168
.L2262:
	mov	w20, -1
	b	.L2168
.L2173:
	mov	w1, 32
	mov	w0, w24
	bl	flash_wait_device_ready
	tbz	x0, 5, .L2260
	ands	w20, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	beq	.L2199
	mov	w1, 12
	mov	w20, w0
	strb	w1, [x19,858]
	mov	w1, -1
	str	w1, [x19,852]
	b	.L2168
.L2199:
	mov	w0, 13
	str	w20, [x19,852]
	strb	w0, [x19,858]
	b	.L2168
.L2174:
	mov	w1, 64
	mov	w0, w24
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2260
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	str	w0, [x19,852]
	mov	w0, 7
	strb	w0, [x19,858]
.L2260:
	mov	w20, 0
.L2168:
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
	cbz	w0, .L2264
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2264
	adrp	x0, .LC174
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC174
	mov	w2, w20
	bl	printk
.L2264:
	adrp	x24, .LANCHOR0
	adrp	x25, .LC0
	add	x23, x24, :lo12:.LANCHOR0
	mov	w21, 0
	add	x27, x23, 800
	add	x25, x25, :lo12:.LC0
.L2265:
	cbz	w20, .L2295
	ldrb	w26, [x19]
	ldr	w22, [x19,40]
.L2266:
	mov	w0, w22
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2296
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2266
.L2296:
	cmp	w20, 1
	beq	.L2268
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w1, [x0,793]
	cbnz	w1, .L2269
.L2268:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L2270
.L2269:
	ldrb	w1, [x0,744]
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
	bne	.L2271
	adrp	x1, .LANCHOR4
	mov	w2, 583
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x25
	add	x1, x1, 344
	bl	printk
	bl	dump_stack
.L2271:
	ldrb	w0, [x19]
	mov	w4, 1
	ldrb	w1, [x23,744]
	add	x0, x27, x0, lsl 6
	ldr	w2, [x0,40]
	mov	w0, 24
	sub	w0, w0, w1
	lsl	w1, w4, w1
	sub	w1, w1, #1
	lsr	w3, w2, w0
	and	w1, w1, w3
	cmp	w28, w1, uxth
	bne	.L2272
	ldrh	w1, [x23,3536]
	ldrb	w6, [x23,3538]
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
	str	x4, [x29,104]
	cmp	w5, w3
	uxth	w0, w0
	beq	.L2272
	and	w22, w0, w22
	and	w0, w0, w2
	cmp	w22, w0
	bne	.L2272
	cmp	w21, w6
	beq	.L2272
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
	add	x0, x23, 2851
	bl	buf_add_tail
	b	.L2270
.L2272:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
.L2270:
	subs	w20, w20, #1
	beq	.L2295
	add	x19, x24, :lo12:.LANCHOR0
	sbfiz	x26, x26, 6, 32
	add	x19, x19, 800
	add	x19, x19, x26
	b	.L2265
.L2295:
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
.L2298:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2851]
	cmp	w0, 255
	beq	.L2300
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2298
.L2300:
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
	adrp	x20, .LANCHOR3
	ldrb	w1, [x0,2873]
	cbz	w1, .L2302
	add	x2, x20, :lo12:.LANCHOR3
	add	x0, x0, 800
	ldrb	w2, [x2,1544]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2302:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	mov	w0, -1
	strb	wzr, [x2,2873]
	strb	w0, [x20,1544]
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
	adrp	x0, .LANCHOR0+2873
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+2873]
	cbz	w0, .L2306
	bl	timer_get_time
	adrp	x1, .LANCHOR3+1548
	ldr	w1, [x1,#:lo12:.LANCHOR3+1548]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2306
	bl	ftl_flush
.L2306:
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
	adrp	x0, .LANCHOR0+744
	mov	w19, 24
	str	x4, [x29,56]
	stp	x21, x22, [sp,32]
	ldrb	w0, [x0,#:lo12:.LANCHOR0+744]
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
	adrp	x26, .LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	add	x24, x26, :lo12:.LANCHOR0
	uxtb	w21, w1
	adrp	x28, .LC0
	mov	x23, x0
	stp	x19, x20, [sp,16]
	add	x27, x24, 800
	mov	x19, x0
	mov	w20, w21
	add	x28, x28, :lo12:.LC0
.L2313:
	cbz	w20, .L2336
	ldrb	w25, [x19]
	ldr	w22, [x19,40]
.L2314:
	mov	w0, w22
	mov	w1, 0
	bl	queue_lun_state
	cbz	w0, .L2337
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2314
.L2337:
	cmp	w20, 1
	beq	.L2320
	adrp	x0, .LANCHOR3+1552
	ldrb	w0, [x0,#:lo12:.LANCHOR3+1552]
	cbz	w0, .L2320
	add	x0, x26, :lo12:.LANCHOR0
	mov	w3, 1
	ldrb	w1, [x0,744]
	mov	w0, 24
	lsl	w3, w3, w1
	sub	w0, w0, w1
	lsr	w0, w22, w0
	sub	w3, w3, #1
	and	w3, w3, w0
	ldrb	w0, [x19]
	cmp	w0, 255
	uxth	w3, w3
	bne	.L2319
	adrp	x1, .LANCHOR4
	mov	w2, 661
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x28
	add	x1, x1, 360
	str	x3, [x29,120]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,120]
.L2319:
	ldrb	w4, [x19]
	mov	w5, 1
	ldrb	w0, [x24,744]
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
	bne	.L2320
	ldrh	w0, [x24,3634]
	add	w22, w0, w22
	cmp	w1, w22
	bne	.L2320
	ldr	w0, [x19,40]
	add	x22, x24, 2851
	str	x6, [x29,96]
	sub	w20, w20, #1
	ldrb	w25, [x27,x4]
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
	b	.L2318
.L2320:
	mov	x0, x19
	bl	queue_read_cmd
.L2318:
	subs	w20, w20, #1
	beq	.L2336
	add	x19, x26, :lo12:.LANCHOR0
	sbfiz	x25, x25, 6, 32
	add	x19, x19, 800
	add	x19, x19, x25
	b	.L2313
.L2336:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 800
.L2324:
	cbz	w21, .L2338
	ldrb	w0, [x23,58]
	cmp	w0, 13
	bne	.L2325
	ldrb	w0, [x23]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2325
	sbfiz	x23, x0, 6, 32
	add	x23, x19, x23
.L2325:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2324
.L2338:
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
.L2340:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2342
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2340
.L2342:
	mov	x0, x20
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x20, x0
	mov	x22, x1
	ldr	x0, [x0]
	mov	w23, w2
	ldr	w19, [x0,40]
.L2344:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2348
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2344
.L2348:
	cmp	w23, 2
	bne	.L2346
	adrp	x25, .LANCHOR0
	ldr	x6, [x20]
	add	x25, x25, :lo12:.LANCHOR0
	mov	w0, 24
	mov	w24, 1
	ldr	x1, [x22]
	ldr	w3, [x6,40]
	mov	w2, 17
	ldrb	w5, [x25,744]
	ldr	w1, [x1,40]
	sub	w0, w0, w5
	lsl	w5, w24, w5
	lsl	w21, w24, w0
	sub	w19, w5, #1
	sub	w21, w21, #1
	ldr	x5, [x6,8]
	and	w26, w21, w3
	lsr	w3, w3, w0
	and	w19, w19, w3
	ldrb	w0, [x6,60]
	ldr	x6, [x6,24]
	and	w21, w21, w1
	uxtb	w19, w19
	mov	w1, w24
	mov	w3, w19
	mov	w4, w26
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22]
	mov	w1, w24
	ldr	x0, [x20]
	mov	w3, w19
	mov	w4, w21
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,8]
	mov	w1, w23
	ldr	x0, [x20]
	mov	w3, w19
	mov	w4, w26
	mov	w2, 17
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,8]
	mov	w1, w23
	ldr	x0, [x20]
	mov	w3, w19
	mov	w4, w21
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x20,16]
	mov	w3, w19
	ldr	x0, [x20]
	mov	w4, w26
	mov	w1, 3
	mov	w2, 17
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,16]
	mov	w1, 3
	ldr	x0, [x20]
	mov	w2, 16
	mov	w3, w19
	mov	w4, w21
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	ldr	x1, [x20]
	mov	w0, 5
	strb	w0, [x1,58]
	mov	w0, -1
	strb	w24, [x1,59]
	strb	w0, [x1]
	add	x0, x25, 2851
	bl	buf_add_tail
	b	.L2347
.L2346:
	mov	x0, x20
	bl	queue_tlc_prog_cmd
.L2347:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
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
	add	x27, x23, 2851
.L2350:
	cbz	w22, .L2354
	ldr	x0, [x20]
	ldr	w19, [x0,40]
.L2351:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2355
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2351
.L2355:
	ldr	x0, [x20]
	sub	w22, w22, #1
	ldr	w3, [x0,40]
	ldrb	w0, [x23,744]
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
	b	.L2350
.L2354:
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
	ldrb	w1, [x1,633]
	mov	w27, w4
	uxtb	w28, w5
	ubfx	x25, x20, 24, 2
	cmp	w1, w21
	mov	x24, x0
	bhi	.L2357
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 560
	add	x1, x1, 376
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2357:
	add	x1, x24, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,633]
	cmp	w2, w21
	bls	.L2358
	add	x0, x1, x21, sxtw
	mov	w19, w20
	ldrb	w26, [x0,736]
	cbnz	w25, .L2359
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L2360
	ldrb	w0, [x1,1]
	cbz	w0, .L2359
.L2360:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w19, [x0,2]
	ldrb	w2, [x0,1]
	udiv	w1, w20, w19
	mul	w19, w1, w19
	sub	w1, w20, w19
	cbz	w2, .L2361
	add	w19, w19, w1, lsl 1
	b	.L2359
.L2361:
	add	x0, x0, 4
	ldrh	w0, [x0,w1,uxtw 1]
	add	w19, w0, w19
.L2359:
	adrp	x25, .LC175
	adrp	x24, .LC176
	adrp	x0, .LC177
	add	x25, x25, :lo12:.LC175
	add	x0, x0, :lo12:.LC177
	add	x24, x24, :lo12:.LC176
	str	x0, [x29,120]
.L2365:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2362
	mov	x0, x25
	mov	w1, w26
	mov	w2, w20
	mov	w3, w19
	bl	printk
.L2362:
	mov	w0, w26
	mov	w1, w19
	mov	x2, x23
	mov	x3, x22
	mov	w4, w27
	bl	flash_prog_page
	mov	w5, w0
	cbz	w28, .L2363
	adrp	x6, .LANCHOR3
	mov	w4, w27
	add	x7, x6, :lo12:.LANCHOR3
	mov	w0, w21
	mov	w1, w20
	str	x6, [x29,104]
	str	x5, [x29,96]
	ldr	x2, [x7,1528]
	ldr	x3, [x7,1536]
	str	x7, [x29,112]
	bl	flash_read_page_en
	cmp	w0, 512
	cset	w4, eq
	ldr	x6, [x29,104]
	cbnz	w4, .L2364
	cmn	w0, #1
	beq	.L2364
	ldr	x7, [x29,112]
	ldr	w1, [x23]
	ldr	x5, [x29,96]
	ldr	x0, [x7,1528]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2364
	ldr	x0, [x7,1536]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2363
.L2364:
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
	adrp	x0, .LC178
	mov	w3, w2
	add	x6, x6, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC178
	str	x6, [x29,112]
	ldr	x1, [x6,1536]
	bl	rknand_print_hex
	ldr	x6, [x29,112]
	mov	w2, 4
	adrp	x0, .LC179
	mov	w3, w2
	add	x0, x0, :lo12:.LC179
	ldr	x1, [x6,1528]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
	cbnz	w4, .L2365
.L2367:
	adrp	x0, .LC180
	mov	w1, w20
	add	x0, x0, :lo12:.LC180
	bl	printk
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 593
	add	x1, x1, 376
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, -1
	b	.L2358
.L2363:
	cmn	w5, #1
	mov	w0, w5
	beq	.L2367
.L2358:
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
	bne	.L2386
	adrp	x2, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 400
	mov	w2, 2344
	bl	printk
	bl	dump_stack
	adrp	x0, .LC180
	mov	w1, w20
	add	x0, x0, :lo12:.LC180
	bl	printk
.L2386:
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
	adrp	x21, .LANCHOR3
	ldrb	w2, [x19,2848]
	add	x0, x21, :lo12:.LANCHOR3
	add	x0, x0, 1600
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrb	w0, [x19,3554]
	cbz	w0, .L2388
	ldr	x0, [x19,2880]
	str	wzr, [x0,156]
	strh	wzr, [x0,150]
.L2388:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w1, [x0,74]
	cbz	w1, .L2389
	strh	wzr, [x0,150]
.L2389:
	adrp	x22, .LANCHOR4
	add	x25, x21, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR4
	mov	w24, 0
	add	x23, x25, 1920
	adrp	x26, .LANCHOR2
	add	x22, x22, 416
.L2400:
	add	x2, x20, :lo12:.LANCHOR0
	add	x3, x25, 1600
	str	x2, [x29,112]
	str	x3, [x29,104]
	ldr	x1, [x2,2880]
	ldrh	w28, [x2,3634]
	ldrb	w19, [x25,1856]
	ldr	w0, [x1,4]
	ldrh	w27, [x25,1858]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldr	w0, [x29,124]
	str	w0, [x25,1600]
	ldr	x0, [x2,2880]
	ldrb	w1, [x2,2848]
	ldr	w0, [x0,4]
	lsl	w1, w1, 9
	str	w0, [x25,1604]
	mov	x0, x23
	bl	js_hash
	ldr	x3, [x29,104]
	ldr	x2, [x29,112]
	str	w0, [x3,8]
	ldr	w0, [x26,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2390
	ldr	x3, [x2,2880]
	adrp	x0, .LC181
	ldrb	w1, [x25,1856]
	add	x0, x0, :lo12:.LC181
	ldrh	w2, [x25,1858]
	ldr	w3, [x3,4]
	bl	printk
.L2390:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x25,1858]
	ldrh	w1, [x1,3596]
	cmp	w1, w0
	bhi	.L2391
	adrp	x19, .LC0
	add	x19, x19, :lo12:.LC0
.L2396:
	ldrb	w1, [x25,1857]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x25,1857]
	cmp	w1, 7
	bls	.L2392
	mov	x1, 0
.L2395:
	add	x0, x20, :lo12:.LANCHOR0
	uxth	w24, w1
	ldr	x2, [x0,728]
	add	w0, w1, 8
	add	x0, x2, x0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2393
	mov	x1, x22
	mov	w2, 761
	mov	x0, x19
	bl	printk
	bl	dump_stack
	b	.L2394
.L2393:
	cmp	w2, 255
	bne	.L2394
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2395
	mov	w24, w1
.L2394:
	strb	w24, [x25,1857]
	mov	w24, 1
.L2392:
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w1, [x25,1857]
	ldr	x0, [x28,728]
	add	x1, x0, x1
	ldrb	w1, [x1,40]
	strb	w1, [x25,1856]
	cmp	w1, 255
	beq	.L2396
	ldrh	w19, [x28,3634]
	mov	w0, 0
	mul	w19, w1, w19
	mov	w1, w19
	bl	flash_erase_block
	ldrb	w4, [x28,2848]
	mov	w1, w19
	mov	w0, 0
	mov	x2, x23
	add	x3, x25, 1600
	add	w19, w19, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x25,1858]
	b	.L2397
.L2391:
	madd	w19, w19, w28, w27
	cbnz	w0, .L2397
	mov	w1, w19
	bl	flash_erase_block
.L2397:
	add	x4, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w0, 0
	mov	x2, x23
	add	x3, x25, 1600
	ldrb	w4, [x4,2848]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x25,1858]
	adrp	x0, .LANCHOR5
	add	w1, w1, 1
	strh	w1, [x25,1858]
	beq	.L2398
	add	x1, x0, :lo12:.LANCHOR5
	ldrb	w1, [x1,896]
	cbz	w1, .L2399
.L2398:
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,896]
	b	.L2400
.L2399:
	cbnz	w24, .L2401
.L2409:
	add	x21, x21, :lo12:.LANCHOR3
	ldrb	w0, [x21,1856]
	cmp	w0, 255
	bne	.L2403
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 797
	add	x1, x1, 416
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2403
.L2401:
	ldrb	w19, [x25,1857]
	adrp	x22, .LANCHOR4
	adrp	x23, .LC0
	add	x22, x22, :lo12:.LANCHOR4
	add	w19, w19, 1
	add	x23, x23, :lo12:.LC0
	add	x22, x22, 416
.L2404:
	cmp	w19, 7
	bhi	.L2409
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0,728]
	add	w0, w19, 8
	add	x0, x1, x0, sxtw
	ldrb	w24, [x0,32]
	add	w0, w24, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2405
	mov	x1, x22
	mov	w2, 790
	mov	x0, x23
	bl	printk
	bl	dump_stack
	b	.L2406
.L2405:
	cmp	w24, 255
	beq	.L2407
.L2406:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3634]
	mov	w0, 0
	mul	w1, w24, w1
	bl	flash_erase_block
.L2407:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2404
.L2403:
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
	adrp	x19, .LANCHOR5
	stp	x23, x24, [sp,48]
	add	x23, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR3
	strb	w0, [x19,896]
	ldrh	w1, [x23,576]
	add	x22, x21, :lo12:.LANCHOR3
	add	x24, x22, 1920
	strb	w0, [x19,898]
	mov	x0, x24
	strb	wzr, [x19,897]
	add	x1, x24, x1, lsl 2
	str	x1, [x23,2880]
	mov	w1, 0
	str	x24, [x23,584]
	adrp	x26, .LC182
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC182
	add	x0, x19, 960
	mov	w1, 0
	mov	w2, 16384
	mov	w19, 7
	bl	ftl_memset
	strb	wzr, [x22,1857]
	ldr	x0, [x23,728]
	add	x23, x22, 1600
	strh	wzr, [x22,1858]
	ldrb	w0, [x0,40]
	strb	w0, [x22,1856]
.L2433:
	add	x5, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w25, w19
	ldr	x1, [x5,728]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L2429
	ldrh	w22, [x5,3634]
	mov	w0, 0
	ldrb	w4, [x5,2848]
	mov	x2, x24
	mov	x3, x23
	str	x5, [x29,104]
	mul	w22, w1, w22
	mov	w1, w22
	bl	ftl_read_page
	cmn	w0, #1
	mov	w28, w0
	ldr	x5, [x29,104]
	bne	.L2430
	ldrb	w4, [x5,2848]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L2430:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2431
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, w28
	mov	w3, 672
	ldr	x4, [x0,2880]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L2431:
	cmn	w28, #1
	beq	.L2429
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2443
.L2429:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L2433
	mov	w25, 0
	b	.L2432
.L2443:
	mov	w19, w25
.L2432:
	adrp	x24, .LANCHOR2
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2434
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,2880]
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	ldr	w3, [x3]
	bl	printk
.L2434:
	cmn	w19, #1
	bne	.L2435
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, 1920
	mov	w2, 16384
	bl	ftl_memset
	ldr	x1, [x20,2880]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, 32
	movk	w0, 0x6, lsl 16
	ldr	x1, [x20,2880]
	str	w0, [x1,12]
	b	.L2460
.L2435:
	add	x27, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR3
	add	w0, w25, 8
	add	x22, x23, 1920
	add	x19, x23, 1600
	mov	x2, x22
	ldr	x1, [x27,728]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	mov	x3, x19
	mov	w0, 0
	mov	w4, 4
	strb	w25, [x23,1857]
	strb	w1, [x23,1856]
	bl	flash_get_last_written_page
	sxth	w26, w0
	add	w0, w0, 1
	ldrb	w23, [x23,1856]
	uxth	w25, w0
	ldrh	w0, [x27,3634]
	adrp	x27, .LC184
	add	x27, x27, :lo12:.LC184
	mul	w23, w23, w0
	b	.L2437
.L2460:
	mov	w0, w19
	b	.L2436
.L2442:
	add	x28, x20, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w26, w23
	mov	x2, x22
	mov	x3, x19
	ldrb	w4, [x28,2848]
	bl	ftl_read_page
	cmn	w0, #1
	bne	.L2461
.L2438:
	sub	w26, w26, #1
	sxth	w26, w26
.L2437:
	tbz	w26, #31, .L2442
	b	.L2441
.L2461:
	ldr	x0, [x28,2880]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2438
	ldr	w2, [x19,8]
	cbnz	w2, .L2439
.L2441:
	add	x21, x21, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	strh	w25, [x21,1858]
	bl	ftl_tmp_into_update
	ldr	x1, [x20,2880]
	ldr	w0, [x1,64]
	add	w0, w0, 1
	str	w0, [x1,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	and	w19, w0, 16384
	tbz	x0, 14, .L2460
	ldr	x1, [x20,2880]
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	ldr	w1, [x1,156]
	bl	printk
	mov	w0, 0
	b	.L2436
.L2439:
	ldrb	w1, [x28,2848]
	mov	x0, x22
	str	x2, [x29,104]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2441
	ldr	w1, [x19,8]
	mov	x0, x27
	bl	printk
	b	.L2438
.L2436:
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
	stp	x25, x26, [sp,64]
	bl	timer_get_time
	adrp	x22, .LANCHOR0
	mov	w1, 100
	udiv	w0, w0, w1
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x1, [x1,608]
	ldr	w3, [x1,520]
	cmp	w0, w3
	bls	.L2463
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2476
.L2463:
	bcs	.L2464
.L2476:
	str	w0, [x1,520]
.L2464:
	adrp	x23, .LANCHOR4
	adrp	x19, .LANCHOR3
	add	x23, x23, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR3
	mov	w0, 0
	add	x23, x23, 432
	add	x19, x19, 1600
	bl	ftl_total_vpn_update
.L2465:
	adrp	x24, .LANCHOR5
	add	x24, x24, :lo12:.LANCHOR5
	add	x25, x24, 960
.L2468:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2880]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3596]
	cmp	w1, w0
	bcc	.L2466
	bl	ftl_ext_alloc_new_blk
.L2466:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2467
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 1926
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2467:
	add	x20, x22, :lo12:.LANCHOR0
	mov	w0, 24
	ldr	x2, [x20,2880]
	ldrb	w1, [x20,744]
	ldrh	w21, [x20,3536]
	ldrh	w3, [x2,130]
	sub	w1, w0, w1
	sub	w1, w1, w21
	mov	w21, 1
	asr	w26, w3, w1
	lsl	w1, w21, w1
	sub	w21, w1, #1
	ldrh	w0, [x2,140]
	and	w3, w21, w3
	ldrh	w21, [x20,3634]
	ldrb	w2, [x20,2848]
	mov	w1, 0
	sxth	w3, w3
	lsl	w2, w2, 1
	madd	w21, w3, w21, w0
	mov	x0, x19
	bl	ftl_memset
	str	wzr, [x19]
	ldr	x0, [x20,2880]
	ldrb	w1, [x20,2848]
	ldr	w0, [x0,56]
	lsl	w1, w1, 9
	str	w0, [x19,4]
	mov	x0, x25
	bl	js_hash
	str	w0, [x19,8]
	ldrb	w4, [x20,2848]
	mov	w1, w21
	mov	x2, x25
	mov	w0, w26
	mov	x3, x19
	bl	ftl_prog_page
	ldr	x2, [x20,2880]
	ldrh	w1, [x2,140]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x2,140]
	cmp	w1, 1
	beq	.L2468
	cmn	w0, #1
	beq	.L2469
	ldrb	w0, [x24,898]
	cbz	w0, .L2470
.L2469:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,898]
	b	.L2465
.L2470:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	stp	x29, x30, [sp, -112]!
	mov	w1, 24
	mov	w2, 1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR3
	stp	x27, x28, [sp,80]
	add	x27, x20, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR3
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR5
	strh	wzr, [x3,-184]
	ldr	x0, [x27,2880]
	add	x3, x3, 1600
	mov	w4, 4
	adrp	x23, .LANCHOR2
	ldrh	w22, [x0,130]
	ldrb	w0, [x27,744]
	sub	w1, w1, w0
	ldrh	w0, [x27,3536]
	sub	w1, w1, w0
	asr	w28, w22, w1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	add	x2, x21, :lo12:.LANCHOR5
	and	w22, w1, w22
	uxtb	w26, w28
	mov	w0, w26
	mov	w1, w22
	add	x2, x2, 960
	bl	flash_get_last_written_page
	sxth	w25, w0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	mov	w24, w25
	tbz	x0, 12, .L2478
	ldr	x5, [x27,2880]
	adrp	x1, .LANCHOR4
	adrp	x0, .LC186
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC186
	add	x1, x1, 456
	ldrh	w5, [x5,130]
	mov	w2, 1971
	mov	w3, w25
	uxth	w4, w28
	bl	printk
.L2478:
	add	x21, x21, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR3
	adrp	x28, .LC187
	mov	w27, w25
	add	x21, x21, 960
	add	x19, x19, 1600
	add	x28, x28, :lo12:.LC187
.L2479:
	tbnz	w27, #31, .L2484
	add	x5, x20, :lo12:.LANCHOR0
	mov	w0, w26
	mov	x2, x21
	mov	x3, x19
	str	x5, [x29,104]
	ldrh	w1, [x5,3634]
	ldrb	w4, [x5,2848]
	madd	w1, w22, w1, w27
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2480
	cmn	w0, #1
	beq	.L2480
	ldr	x5, [x29,104]
	ldr	x0, [x5,608]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2480
	ldr	w2, [x19,8]
	cbnz	w2, .L2481
.L2484:
	bl	zftl_sblk_list_init
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w1, [x0,140]
	cmp	w1, w25
	bgt	.L2483
	add	w24, w24, 1
	strh	w24, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2483
.L2481:
	ldrb	w1, [x5,2848]
	mov	x0, x21
	str	x2, [x29,104]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2484
	ldr	w1, [x19,8]
	mov	x0, x28
	bl	printk
.L2480:
	sub	w27, w27, #1
	sxth	w27, w27
	b	.L2479
.L2483:
	add	x19, x20, :lo12:.LANCHOR0
	ldr	x21, [x19,608]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x21,520]
	ldr	x21, [x19,608]
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x19,608]
	mov	w1, -1
	strh	w1, [x0,584]
	strh	w1, [x0,586]
	strh	w1, [x0,588]
	strh	w1, [x0,590]
	mov	w1, 65535
	str	w1, [x0,560]
	mov	w1, -1
	str	w1, [x0,564]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	str	wzr, [x0,608]
	tbz	x1, 12, .L2486
	ldr	w2, [x0,520]
	ldr	w3, [x0,12]
	str	x2, [x29,96]
	str	x3, [x29,104]
	bl	timer_get_time
	mov	w4, w0
	adrp	x1, .LANCHOR4
	adrp	x0, .LC188
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x2, [x29,96]
	ldr	x3, [x29,104]
	add	x0, x0, :lo12:.LC188
	add	x1, x1, 456
	bl	printk
.L2486:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, -1
	strh	w1, [x0,3512]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	adrp	x0, .LANCHOR0+744
	stp	x29, x30, [sp, -16]!
	mov	w5, 24
	mov	x10, x1
	add	x29, sp, 0
	ldrb	w6, [x0,#:lo12:.LANCHOR0+744]
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
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3-200
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	w21, -1
	ldr	x20, [x1,#:lo12:.LANCHOR3-200]
	ldrh	w1, [x0,6]
	ldrh	w22, [x0,12]
	cmp	w1, 1
	bne	.L2501
	mov	x19, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L2501
	ldrh	w0, [x19]
	add	x22, x20, x22, uxth 2
	adrp	x19, .LANCHOR6
	adrp	x20, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR6
	add	x20, x20, :lo12:.LANCHOR0
	bl	ftl_vpn_decrement
	mov	w0, 15555
	sub	x2, x19, #64
	movk	w0, 0xf55f, lsl 16
	ldrh	w1, [x20,3596]
	str	w0, [x19,-64]
	ldrb	w0, [x20,3572]
	str	x2, [x29,72]
	mul	w1, w1, w0
	mov	x0, x22
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x19,-60]
	ldr	x2, [x29,72]
	mov	w0, w23
	ldrb	w3, [x20,2848]
	mov	x1, x22
	str	w21, [x19,-56]
	str	w21, [x19,-52]
	bl	ftl_prog_ppa_page
.L2501:
	mov	w0, w21
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	stp	x29, x30, [sp, -336]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	mov	x19, x0
	str	x1, [x29,120]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	tbz	x0, 12, .L2506
	adrp	x0, .LC189
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC189
	bl	printk
.L2506:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2507
	adrp	x0, .LC190
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC190
	bl	printk
.L2507:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2508
	adrp	x0, .LC191
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC191
	bl	printk
.L2508:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2509
	adrp	x0, .LC192
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC192
	bl	printk
.L2509:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2510
	adrp	x0, .LC193
	ldrb	w1, [x19,9]
	add	x0, x0, :lo12:.LC193
	bl	printk
.L2510:
	ldrh	w0, [x19,10]
	adrp	x20, .LANCHOR0
	strh	w0, [x19,14]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldrh	w0, [x0,576]
	cmp	w1, w0
	bcs	.L2505
	mov	w0, 1
	add	x21, x29, 144
	add	x26, x29, 272
	mov	w27, 2
	bl	buf_alloc
	mov	x25, x0
	mov	w1, 255
	mov	w2, 64
	mov	x0, x21
	bl	ftl_memset
	mov	w1, 255
	mov	w2, 64
	add	x0, x29, 208
	bl	ftl_memset
	mov	x0, x26
	mov	w1, 255
	mov	w2, 64
	bl	ftl_memset
	ldrb	w23, [x19,5]
	adrp	x0, .LANCHOR3
	ldrh	w24, [x19,2]
	add	x0, x0, :lo12:.LANCHOR3
	str	wzr, [x29,136]
	add	x28, x0, 1600
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 480
	str	x0, [x29,112]
.L2512:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	cmp	w0, w24
	bls	.L2515
	ldrb	w23, [x19,5]
.L2513:
	ldrb	w0, [x19,9]
	cmp	w0, w23
	bls	.L2643
	add	x0, x19, x23, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2514
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x2, [x25,24]
	ldrh	w1, [x3,3634]
	ldrb	w3, [x3,2848]
	madd	w0, w0, w1, w24
	ldr	x1, [x25,8]
	str	w0, [x29,132]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w3, w0
	beq	.L2515
	cmn	w0, #1
	beq	.L2516
	ldr	x0, [x25,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2516
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2516
	ldr	x0, [x25,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2515
.L2516:
	adrp	x0, .LANCHOR5+897
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+897]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w24, w0, w23
	cmp	w1, w0
	beq	.L2517
	ldr	x1, [x29,112]
	adrp	x0, .LC0
	mov	w2, 1498
	add	x0, x0, :lo12:.LC0
	str	x3, [x29,136]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,136]
.L2517:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2518
	ldr	x1, [x29,112]
	adrp	x0, .LC0
	mov	w2, 1499
	add	x0, x0, :lo12:.LC0
	str	x3, [x29,136]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,136]
.L2518:
	cmn	w3, #1
	beq	.L2522
	ldr	x0, [x25,24]
	ldr	w0, [x0,4]
	cmn	w0, #1
	beq	.L2522
	bl	lpa_hash_get_ppa
	ldr	x1, [x29,120]
	cbz	x1, .L2520
	ldr	x7, [x25,24]
	ldr	w1, [x7,8]
	cmp	w0, w1
	beq	.L2520
	cmn	w0, #1
	beq	.L2520
	add	x27, x20, :lo12:.LANCHOR0
	mov	w6, 24
	mov	w4, 1
	ldrb	w2, [x27,744]
	ldrh	w1, [x27,3536]
	sub	w2, w6, w2
	lsr	w3, w0, w1
	sub	w1, w2, w1
	lsl	w1, w4, w1
	ldrb	w2, [x27,3538]
	sub	w1, w1, #1
	and	w1, w3, w1
	udiv	w1, w1, w2
	ldr	x2, [x29,120]
	ldrh	w2, [x2]
	cmp	w2, w1, uxth
	bne	.L2520
	ldr	w7, [x7]
	mov	x2, x28
	ldr	x1, [x25,8]
	ldrb	w3, [x27,2848]
	str	x4, [x29,96]
	str	x6, [x29,104]
	str	x7, [x29,136]
	bl	ftl_read_ppa_page
	ldr	w0, [x28]
	ldr	x7, [x29,136]
	ldr	x6, [x29,104]
	cmp	w0, w7
	ldr	x4, [x29,96]
	bcc	.L2520
	ldr	x0, [x25,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2522
	ldrb	w2, [x27,744]
	ldrh	w0, [x27,3536]
	sub	w6, w6, w2
	sub	w6, w6, w0
	lsr	w1, w1, w0
	lsl	w4, w4, w6
	ldrb	w0, [x27,3538]
	sub	w4, w4, #1
	and	w1, w4, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2522
.L2520:
	ldr	x2, [x25,24]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2856]
	cmp	w3, w0
	bcs	.L2522
	ldrb	w0, [x19,9]
	ldrh	w4, [x1,3596]
	ldrh	w3, [x19,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L2644
.L2522:
	ldrh	w0, [x19,6]
	mov	w27, 4
	sub	w0, w0, #1
	strh	w0, [x19,6]
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
	mov	w0, 1
	str	w0, [x29,136]
.L2514:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L2513
.L2643:
	add	w24, w24, 1
	strb	wzr, [x19,5]
	uxth	w24, w24
	b	.L2512
.L2515:
	add	x2, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	strh	w24, [x19,2]
	add	w1, w1, w0
	ldrh	w2, [x2,3596]
	ldrb	w0, [x19,9]
	strb	w23, [x19,5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2526
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1579
	add	x1, x1, 480
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2526:
	adrp	x1, .LANCHOR3
	ldrh	w3, [x19,10]
	add	x0, x1, :lo12:.LANCHOR3
	str	x1, [x29,120]
	ldr	x4, [x0,-200]
	mov	w0, 0
	mov	w2, w0
.L2527:
	cmp	w2, w3
	bcs	.L2645
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L2528
	add	w1, w0, 1
	uxth	w0, w1
.L2528:
	add	w2, w2, 1
	b	.L2527
.L2645:
	add	x2, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w23, [x2,3596]
	madd	w0, w1, w23, w0
	uxth	w23, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2530
	ldrh	w1, [x19]
	adrp	x0, .LC194
	ldr	x4, [x2,600]
	add	x0, x0, :lo12:.LC194
	ubfiz	x3, x1, 1, 16
	mov	w2, w23
	ldrh	w3, [x4,x3]
	bl	printk
.L2530:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldr	x0, [x0,600]
	strh	w23, [x0,x1,lsl 1]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2531
	adrp	x0, .LC195
	ldr	w1, [x29,144]
	ldr	w2, [x29,148]
	add	x0, x0, :lo12:.LC195
	ldr	w3, [x29,152]
	ldr	w4, [x29,156]
	bl	printk
.L2531:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 0
	adrp	x28, .LC197
	mov	x24, 0
	ldrb	w2, [x0,2848]
	ldr	x0, [x25,8]
	lsl	w2, w2, 9
	bl	ftl_memset
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	str	x0, [x29,112]
	add	x0, x28, :lo12:.LC197
	str	x0, [x29,104]
.L2532:
	add	x3, x20, :lo12:.LANCHOR0
	str	w24, [x29,132]
	ldrb	w0, [x3,3572]
	mul	w0, w27, w0
	cmp	w24, w0
	bcs	.L2646
	ldr	w0, [x21,x24,lsl 2]
	cmn	w0, #1
	bne	.L2533
.L2537:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x25,24]
	ldr	x1, [x1,2880]
	ldr	w1, [x1,8]
	str	w1, [x0]
	mov	w1, -1
	mov	w23, w1
	ldr	x0, [x25,24]
	str	w1, [x0,4]
	ldr	x0, [x25,24]
	str	w1, [x0,8]
	ldr	x0, [x25,24]
	str	wzr, [x0,12]
	ldr	x0, [x25,8]
	str	wzr, [x0]
	b	.L2534
.L2533:
	ldr	x1, [x25,8]
	ldr	x2, [x25,24]
	ldrb	w3, [x3,2848]
	bl	ftl_read_ppa_page
	mov	w23, w0
	ldr	x0, [x25,24]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2535
	ldr	x3, [x25,24]
	mov	w1, w28
	ldr	x0, [x29,112]
	ldr	w2, [x21,x24,lsl 2]
	ldr	w3, [x3,4]
	bl	printk
.L2535:
	ldr	w0, [x21,x24,lsl 2]
	mov	w1, 1
	cmp	w28, w0
	ldr	x0, [x25,24]
	csinv	w23, w23, wzr, eq
	cmn	w23, #1
	str	w28, [x0,8]
	ldr	x0, [x25,24]
	str	w1, [x0,12]
	beq	.L2537
.L2534:
	ldr	w0, [x29,136]
	cbz	w0, .L2539
.L2642:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L2539
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2541
	ldr	x2, [x25,24]
	mov	w1, w28
	ldrh	w0, [x19,12]
	ldrh	w3, [x19,10]
	ldr	w2, [x2,4]
	add	w3, w3, w0
	ldr	x0, [x29,104]
	sub	w3, w3, #1
	bl	printk
.L2541:
	add	x3, x20, :lo12:.LANCHOR0
	ldr	w1, [x29,132]
	ldrh	w0, [x19,6]
	ldrb	w2, [x3,3572]
	mul	w2, w27, w2
	add	w2, w2, 1
	sub	w2, w2, w1
	cmp	w0, w2
	bls	.L2539
	ldr	x1, [x25,8]
	mov	w0, w28
	ldr	x2, [x25,24]
	ldrb	w3, [x3,2848]
	bl	ftl_prog_ppa_page
	mov	w2, w0
	ldrh	w0, [x19]
	str	x2, [x29,96]
	bl	ftl_vpn_decrement
	ldr	x2, [x29,96]
	cmn	w23, #1
	cset	w1, ne
	cmn	w2, #1
	beq	.L2542
	cbz	w1, .L2539
	add	x0, x29, 208
	ldrh	w1, [x19,10]
	str	w28, [x0,x24,lsl 2]
	ldrh	w0, [x19,12]
	add	w0, w1, w0
	sub	w0, w0, #1
	str	w0, [x26,x24,lsl 2]
	b	.L2539
.L2542:
	cbnz	w1, .L2642
.L2539:
	add	x24, x24, 1
	b	.L2532
.L2646:
	adrp	x23, .LC198
	adrp	x24, .LC197
	mov	x28, 0
	add	x23, x23, :lo12:.LC198
	add	x24, x24, :lo12:.LC197
.L2546:
	add	x3, x20, :lo12:.LANCHOR0
	ldrb	w0, [x3,3572]
	mul	w0, w27, w0
	cmp	w0, w28
	bls	.L2647
	add	x0, x29, 208
	ldr	w0, [x0,x28,lsl 2]
	cmn	w0, #1
	beq	.L2548
	ldr	w0, [x21,x28,lsl 2]
	ldr	x1, [x25,8]
	ldr	x2, [x25,24]
	ldrb	w3, [x3,2848]
	bl	ftl_read_ppa_page
	cmp	w0, 256
	beq	.L2558
	cmn	w0, #1
	bne	.L2548
.L2558:
	add	x3, x20, :lo12:.LANCHOR0
	add	x0, x29, 208
	ldr	x1, [x25,8]
	ldr	x2, [x25,24]
	ldr	w0, [x0,x28,lsl 2]
	ldrb	w3, [x3,2848]
	bl	ftl_read_ppa_page
	mov	w4, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2551
	ldr	x2, [x25,24]
	mov	w1, w4
	ldr	w3, [x21,x28,lsl 2]
	mov	x0, x24
	str	x4, [x29,136]
	ldr	w2, [x2,8]
	bl	printk
	ldr	x4, [x29,136]
.L2551:
	cmn	w4, #1
	beq	.L2548
	ldr	x1, [x25,24]
	ldr	w2, [x21,x28,lsl 2]
	ldr	w0, [x1,8]
	cmp	w2, w0
	bne	.L2548
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L2552
	ldr	w1, [x1,4]
	mov	x0, x23
	ldr	w3, [x26,x28,lsl 2]
	bl	printk
.L2552:
	ldr	x1, [x25,24]
	ldr	w2, [x26,x28,lsl 2]
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
.L2548:
	add	x28, x28, 1
	b	.L2546
.L2647:
	mov	x0, x25
	str	x3, [x29,136]
	bl	zbuf_free
	ldr	x3, [x29,136]
	ldrb	w2, [x19,9]
	ldrh	w0, [x19,12]
	ldrh	w1, [x3,3596]
	madd	w0, w1, w2, w0
	ldr	x1, [x29,120]
	mov	x2, -4
	add	x0, x2, x0, sxtw 2
	add	x1, x1, :lo12:.LANCHOR3
	ldr	x1, [x1,-200]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L2554
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1670
	add	x1, x1, 480
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2554:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2505
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2505
.L2644:
	ldr	x0, [x1,2880]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2523
	str	w2, [x0,8]
.L2523:
	ldr	x1, [x25,24]
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
	ldr	w0, [x29,132]
	str	w0, [x21,12]
	b	.L2522
.L2505:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 336
	ret
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
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
	add	x22, x22, 504
.L2649:
	add	x2, x25, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x2,2880]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3596]
	cmp	w3, w1
	bcs	.L2650
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2651
.L2650:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2651:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2652
	mov	x1, x22
	mov	w2, 263
	mov	x0, x23
	bl	printk
	bl	dump_stack
.L2652:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR6
	add	x26, x20, :lo12:.LANCHOR6
	mov	w2, 64
	ldr	x0, [x19,2880]
	ldrh	w1, [x19,3634]
	ldrh	w27, [x0,692]
	ldrh	w0, [x0,696]
	madd	w27, w27, w1, w0
	ldr	x0, [x26,192]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x26,192]
	str	w21, [x0]
	ldr	x1, [x19,2880]
	ldr	x0, [x26,192]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	x0, x24
	ldrb	w1, [x19,2848]
	ldr	x28, [x26,192]
	lsl	w1, w1, 9
	bl	js_hash
	str	w0, [x28,8]
	ldr	x0, [x19,2880]
	mov	w1, w27
	ldr	x3, [x26,192]
	mov	x2, x24
	ldrb	w4, [x19,2848]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x1, [x19,2880]
	ldrh	w2, [x1,696]
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x1,696]
	cmp	w2, 1
	beq	.L2653
	ldrb	w2, [x26,200]
	cbz	w2, .L2654
.L2653:
	add	x20, x20, :lo12:.LANCHOR6
	strb	wzr, [x20,200]
	b	.L2649
.L2654:
	cmn	w0, #1
	bne	.L2656
	adrp	x0, .LC199
	mov	w1, w27
	add	x0, x0, :lo12:.LC199
	bl	printk
	b	.L2649
.L2656:
	ldrh	w0, [x1,698]
	cmp	w21, w0
	bcs	.L2657
	add	x21, x1, x21, uxtw 2
	str	w27, [x21,704]
.L2657:
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
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp,96]
	uxth	w23, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,64]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	ldrh	w1, [x0,576]
	cmp	w1, w23
	bls	.L2661
	ldrb	w1, [x0,3568]
	cbnz	w1, .L2663
	ldrb	w0, [x0,3554]
	cbz	w0, .L2661
.L2663:
	add	x1, x22, :lo12:.LANCHOR0
	ubfiz	x19, x23, 2, 16
	mov	w27, 1
	ldr	x0, [x1,584]
	add	x0, x0, x19
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2664
	ldrb	w27, [x1,3553]
.L2664:
	add	x24, x29, 128
	mov	w0, w23
	add	x1, x24, 16
	strh	w23, [x29,128]
	mov	w21, 0
	adrp	x25, .LC201
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	add	x1, x22, :lo12:.LANCHOR0
	strb	w0, [x29,137]
	strh	wzr, [x29,130]
	mov	w20, w21
	strb	wzr, [x29,133]
	mov	w26, w21
	ldrh	w2, [x1,3596]
	add	x25, x25, :lo12:.LC201
	strh	wzr, [x29,138]
	mul	w0, w0, w2
	strh	w0, [x29,134]
	ldr	x0, [x1,584]
	add	x1, x0, x19
	ldrh	w4, [x0,x19]
	ldr	w5, [x0,x19]
	adrp	x0, .LC200
	ldrb	w3, [x1,2]
	and	w4, w4, 2047
	mov	w1, w23
	ubfx	x5, x5, 11, 8
	ubfx	x2, x3, 5, 3
	add	x0, x0, :lo12:.LC200
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	mov	w8, 65535
.L2665:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	cmp	w0, w20
	bls	.L2686
	lsl	w28, w20, 1
	mov	w26, 0
	sub	w28, w28, #1
.L2675:
	ldrb	w0, [x24,9]
	cmp	w0, w26
	bls	.L2690
	mov	w21, 1
	sxtw	x9, w26
.L2674:
	cmp	w21, w27
	bhi	.L2691
	add	x0, x9, 8
	ldrh	w5, [x24,x0,lsl 1]
	cmp	w5, w8
	beq	.L2667
	add	x0, x22, :lo12:.LANCHOR0
	cmp	w27, 3
	ldrh	w2, [x0,3634]
	mul	w2, w5, w2
	bne	.L2668
	add	w2, w2, w20
	orr	w3, w2, w21, lsl 24
	b	.L2669
.L2668:
	cmp	w27, 2
	add	w3, w2, w20
	bne	.L2669
	ldrb	w3, [x0,3553]
	add	w2, w2, w28
	add	w2, w2, w21
	orr	w3, w2, w3, lsl 24
.L2669:
	str	w3, [x19,40]
	mov	w1, 1
	mov	x0, x19
	str	x8, [x29,96]
	str	x9, [x29,104]
	str	x5, [x29,112]
	str	x3, [x29,120]
	bl	sblk_read_page
	ldr	x7, [x19,8]
	mov	w2, w20
	ldr	x0, [x19,24]
	ldr	w10, [x19,52]
	ldr	w1, [x7,12]
	str	w1, [sp]
	mov	w4, w10
	ldr	x5, [x29,112]
	ldr	w1, [x0]
	str	w1, [sp,8]
	ldr	x3, [x29,120]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	str	x10, [x29,120]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w5
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	mov	x0, x25
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x10, [x29,120]
	ldr	x9, [x29,104]
	cmp	w10, 512
	ldr	x8, [x29,96]
	beq	.L2686
.L2667:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2674
.L2691:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L2675
.L2690:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2665
.L2686:
.L2671:
	adrp	x0, .LC202
	mov	w4, w21
	mov	w2, w20
	mov	w3, w26
	mov	w1, w23
	mov	w25, w20
	add	x0, x0, :lo12:.LC202
	add	x20, x22, :lo12:.LANCHOR0
	adrp	x21, .LC203
	mov	w27, 24
	bl	printk
	add	x21, x21, :lo12:.LC203
	ldrb	w2, [x20,2848]
	mov	w1, 0
	ldr	x0, [x19,8]
	mov	w28, 1
	lsl	w2, w2, 9
	bl	ftl_memset
	ldrb	w2, [x20,2848]
	mov	w1, 0
	ldr	x0, [x19,24]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2676:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	cmp	w0, w25
	bls	.L2681
	lsl	w26, w25, 1
	mov	w20, 0
.L2682:
	ldrb	w0, [x24,9]
	cmp	w0, w20
	bls	.L2692
	mov	w2, w20
	mov	x0, x21
	mov	w1, w25
	bl	printk
	add	x4, x22, :lo12:.LANCHOR0
	sxtw	x0, w20
	ldrb	w5, [x4,3568]
	ldrh	w2, [x4,3634]
	cbz	w5, .L2677
	add	x0, x0, 8
	ldrh	w0, [x24,x0,lsl 1]
	mul	w2, w0, w2
	mov	x0, x19
	orr	w2, w2, w25
	str	w2, [x19,40]
	bl	sblk_3d_tlc_dump_prog
	b	.L2678
.L2677:
	add	x0, x0, 8
	ldrb	w1, [x4,3553]
	cmp	w1, 2
	ldrh	w0, [x24,x0,lsl 1]
	mul	w2, w0, w2
	bne	.L2679
	orr	w2, w2, w26
	mov	x0, x19
	orr	w2, w2, 33554432
	str	w2, [x19,40]
	bl	sblk_mlc_dump_prog
	b	.L2678
.L2679:
	ldrb	w0, [x4,744]
	orr	w2, w2, w25
	ldrb	w4, [x4,2848]
	sub	w1, w27, w0
	lsl	w0, w28, w0
	sub	w0, w0, #1
	lsr	w3, w2, w1
	lsl	w1, w0, w1
	str	w2, [x19,40]
	and	w0, w3, w0
	bic	w1, w2, w1
	ldr	x3, [x19,24]
	ldr	x2, [x19,8]
	bl	flash_prog_page_en
.L2678:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2682
.L2692:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2676
.L2681:
	mov	x0, x19
	bl	zbuf_free
	adrp	x0, .LC204
	mov	w1, w23
	add	x0, x0, :lo12:.LC204
	bl	printk
.L2661:
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
	.global	ftl_ink_check_sblk
	.type	ftl_ink_check_sblk, %function
ftl_ink_check_sblk:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	add	x22, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxth	w20, w0
	ubfiz	x24, x20, 2, 16
	ldr	x0, [x22,584]
	mov	w1, w20
	ldrh	w2, [x0,x24]
	ldr	w3, [x0,x24]
	adrp	x0, .LC205
	add	x0, x0, :lo12:.LC205
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	bl	printk
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2693
	ldrh	w0, [x22,576]
	cmp	w0, w20
	bls	.L2693
	mov	w1, 0
	mov	w0, w20
	add	x23, x29, 112
	bl	ftl_erase_sblk
	strh	w20, [x29,112]
	add	x1, x23, 16
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	strb	w0, [x29,121]
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldr	x0, [x22,584]
	ldrb	w2, [x22,2848]
	ldrh	w0, [x0,x24]
	and	w0, w0, 2047
	cmp	w0, 1
	ldr	x0, [x19,8]
	bgt	.L2695
	mov	w1, 85
	b	.L2710
.L2695:
	mov	w1, 170
.L2710:
	lsl	w2, w2, 9
	mov	w22, 0
	mov	w26, 65535
	mov	w27, 24
	mov	w28, 1
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
.L2697:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,3596]
	cmp	w0, w22
	bls	.L2711
	mov	w25, 0
.L2700:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2712
	add	x0, x23, x25, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w26
	beq	.L2698
	add	x4, x21, :lo12:.LANCHOR0
	mov	w5, 0
	ldrh	w2, [x4,3634]
	madd	w2, w0, w2, w22
	ldr	x0, [x19,8]
	str	w2, [x0]
	mov	w0, 65280
	movk	w0, 0x55aa, lsl 16
	ldr	x1, [x19,8]
	str	w0, [x1,4]
	ldr	x0, [x19,24]
	str	wzr, [x0]
	ldrb	w0, [x4,744]
	ldrb	w4, [x4,2848]
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
.L2698:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2700
.L2712:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2697
.L2711:
	mov	w22, 0
	mov	w26, 65535
	mov	w27, -1
.L2701:
	add	x25, x21, :lo12:.LANCHOR0
	ldrh	w0, [x25,3596]
	cmp	w0, w22
	bls	.L2713
	mov	w25, 0
.L2706:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2714
	sxtw	x28, w25
	add	x28, x28, 8
	ldrh	w0, [x23,x28,lsl 1]
	cmp	w0, w26
	beq	.L2703
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w2, [x1,3634]
	mov	w1, 1
	madd	w2, w0, w2, w22
	str	w2, [x19,40]
	mov	x0, x19
	str	x2, [x29,104]
	bl	sblk_read_page
	ldr	x0, [x19,8]
	ldr	x2, [x29,104]
	ldr	w0, [x0]
	cmp	w0, w2
	beq	.L2703
	mov	w0, w2
	bl	ftl_mask_bad_block
	strh	w27, [x23,x28,lsl 1]
.L2703:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2706
.L2714:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2701
.L2713:
	mov	x0, x19
	bl	zbuf_free
	ldr	x0, [x25,584]
	mov	w1, w20
	ldrh	w2, [x0,x24]
	ldr	w3, [x0,x24]
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	bl	printk
.L2693:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LC121
	adrp	x24, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w22, w1
	mov	x25, x24
	add	x26, x0, 16
	adrp	x27, .LANCHOR3
	add	x21, x21, :lo12:.LC121
.L2716:
.L2720:
	mov	w0, w22
	bl	ftl_alloc_sblk
	uxth	w23, w0
	add	x2, x24, :lo12:.LANCHOR0
	mov	w28, w23
	uxtw	x20, w23
	ldr	x1, [x2,2880]
	ldrh	w1, [x1,150]
	cbz	w1, .L2717
	lsl	x1, x20, 2
	ldr	x2, [x2,584]
	ldrh	w1, [x2,x1]
	and	w1, w1, 2047
	cmp	w1, 2
	bgt	.L2717
	bl	ftl_ink_check_sblk
.L2717:
	mov	w1, 0
	mov	w0, w28
	bl	ftl_erase_sblk
	mov	x1, x26
	mov	w0, w28
	bl	ftl_get_blk_list_in_sblk
	strh	w28, [x19]
	add	x2, x25, :lo12:.LANCHOR0
	uxtb	w0, w0
	strb	w0, [x19,9]
	cmp	w22, 2
	strh	wzr, [x19,2]
	mov	w3, 0
	ldrh	w1, [x2,3596]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w0, w1, w0
	strh	w0, [x19,6]
	strb	w22, [x19,4]
	beq	.L2718
	ldrb	w3, [x2,3572]
	mul	w3, w1, w3
	uxth	w3, w3
.L2718:
	add	x28, x25, :lo12:.LANCHOR0
	add	x0, x27, :lo12:.LANCHOR3
	strh	w3, [x19,12]
	lsl	x20, x20, 1
	ldrb	w2, [x28,3572]
	ldr	x0, [x0,-200]
	add	x0, x0, x3, uxth 2
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x28,600]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x20]
	ldrb	w0, [x19,9]
	cbnz	w0, .L2715
	mov	w1, w23
	mov	x0, x21
	bl	printk
	ldr	x0, [x28,600]
	mov	w1, -1
	strh	w1, [x0,x20]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L2720
.L2715:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	adrp	x0, .LANCHOR2
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	tbz	x0, 12, .L2726
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 325
	add	x2, x2, 520
	bl	printk
.L2726:
	adrp	x22, .LANCHOR6
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR6
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC208
	adrp	x25, .LC209
	ldr	x0, [x0,208]
	mov	w26, 0
	add	x24, x24, :lo12:.LC208
	adrp	x27, .LANCHOR3
	add	x25, x25, :lo12:.LC209
	bl	ftl_memset
	ldr	x1, [x19,728]
	adrp	x0, .LC207
	add	x0, x0, :lo12:.LC207
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,728]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2727:
	add	x21, x22, :lo12:.LANCHOR6
	add	x28, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,216]
	ldrh	w19, [x21,218]
	ldrh	w3, [x28,2]
	mov	w1, w20
	mov	w2, w19
	str	x3, [x29,104]
	bl	printk
	add	x1, x27, :lo12:.LANCHOR3
	ldrh	w0, [x21,218]
	ldr	x3, [x29,104]
	ldrh	w1, [x1,1380]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2728
	ldr	x1, [x28,728]
	strh	wzr, [x21,218]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,216]
	ldr	x19, [x28,728]
	ldrb	w1, [x21,217]
	strb	w0, [x21,217]
	mov	x0, x19
	strb	w1, [x21,216]
	ldrh	w1, [x19,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
	ldrb	w1, [x21,216]
	mov	w0, 0
	ldrh	w19, [x28,2]
	mul	w19, w1, w19
	b	.L2736
.L2728:
	madd	w19, w20, w3, w19
	cbnz	w0, .L2729
.L2736:
	mov	w1, w19
	bl	flash_erase_block
.L2729:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR6
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,728]
	ldr	x0, [x20,208]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,208]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,728]
	ldr	x3, [x20,208]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,218]
	add	w1, w1, 1
	strh	w1, [x20,218]
	bne	.L2730
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2727
.L2730:
	cbnz	w26, .L2737
	mov	w26, 1
	b	.L2727
.L2737:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	tbz	x0, 12, .L2739
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR4
	adrp	x0, .LC210
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC210
	mov	w1, 50
	ldr	x4, [x4,728]
	add	x2, x2, 544
	mov	w3, 2048
	bl	printk
.L2739:
	mov	w23, 4
	adrp	x24, .LANCHOR6
.L2742:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x24, :lo12:.LANCHOR6
	mov	w0, 0
	mov	w4, 4
	adrp	x22, .LANCHOR6
	ldrh	w1, [x19,2]
	ldr	x2, [x19,728]
	ldr	x3, [x21,208]
	mul	w1, w23, w1
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2740
	ldr	x2, [x19,728]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L2741
.L2740:
	add	w23, w23, 1
	cmp	w23, 16
	bne	.L2742
.L2767:
	mov	w0, -1
	b	.L2766
.L2741:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,2]
	strb	w1, [x21,216]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,208]
	strb	w0, [x21,217]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2744
	ldr	x0, [x19,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2744
	ldr	w21, [x0,4]
.L2744:
	add	x19, x22, :lo12:.LANCHOR6
	add	x23, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,217]
	ldrh	w1, [x23,2]
	ldr	x3, [x19,208]
	mul	w1, w2, w1
	ldr	x2, [x23,728]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2745
	ldr	x0, [x23,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2745
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L2745
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,216]
	strb	w0, [x19,217]
.L2745:
	add	x19, x22, :lo12:.LANCHOR6
	add	x21, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x19,216]
	ldr	x2, [x21,728]
	ldr	x3, [x19,208]
	bl	flash_get_last_written_page
	ldrh	w23, [x21,2]
	ldrb	w1, [x19,216]
	uxth	w24, w0
	add	w0, w24, 1
	mov	w21, 0
	strh	w0, [x19,218]
	mul	w23, w1, w23
.L2746:
	sub	w19, w24, w21
	sxth	w19, w19
	tbnz	w19, #31, .L2768
	add	x25, x20, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR6
	mov	w0, 0
	add	w1, w19, w23
	mov	w4, 4
	ldr	x2, [x25,728]
	ldr	x3, [x3,208]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2747
	ldr	x0, [x25,728]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	beq	.L2748
.L2747:
	add	w21, w21, 1
	b	.L2746
.L2768:
	cmn	w19, #1
	bne	.L2748
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	ldr	x1, [x20,728]
	ldr	w1, [x1]
	bl	printk
	b	.L2767
.L2748:
	cmp	w21, 1
	mov	w0, 0
	bls	.L2766
	bl	flash_info_flush
	mov	w0, 0
.L2766:
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
	stp	x25, x26, [sp,64]
	tbz	x0, 12, .L2770
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 2759
	add	x2, x2, 568
	bl	printk
.L2770:
	adrp	x21, .LANCHOR3
	mov	x0, x19
	add	x23, x21, :lo12:.LANCHOR3
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w25, 44
	add	x24, x22, 760
	str	wzr, [x23,1516]
	bl	nandc_init
	mov	w0, 1
	add	x1, x20, :lo12:.LANCHOR2
	add	x1, x1, 8
	strb	w0, [x22,633]
	mov	w0, 3
	str	x1, [x22,624]
	strb	w0, [x22,744]
	mov	w1, 0
	mov	w2, 8
	add	x0, x22, 736
	bl	ftl_memset
	add	x0, x23, 1384
	mov	w1, 0
	mov	w2, 32
	mov	w23, 0
	bl	ftl_memset
.L2776:
	mov	w0, w23
	mov	x1, x24
	bl	flash_read_id
	cbnz	w23, .L2771
	ldrb	w0, [x22,760]
	add	x1, x22, 760
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2772
.L2774:
	mov	w22, -2
	b	.L2849
.L2772:
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L2774
.L2771:
	ldrb	w0, [x24]
	cmp	w0, 181
	bne	.L2775
	strb	w25, [x24]
.L2775:
	add	w23, w23, 1
	add	x24, x24, 8
	uxtb	w23, w23
	cmp	w23, 4
	bne	.L2776
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x24, x24, 760
.L2779:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x24
	bl	flash_mem_cmp8
	cbnz	w0, .L2777
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L2774
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2781
.L2777:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 45
	bne	.L2779
	b	.L2774
.L2781:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1880]
	cmp	w4, w3
	beq	.L2780
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2781
	mov	w2, w0
.L2780:
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x23, 1880
	ubfiz	x2, x2, 5, 32
	add	x1, x1, x2
	add	x0, x24, 640
	mov	w2, 32
	add	x23, x23, 8
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x24,516]
	cmp	w0, 8
	bhi	.L2782
	ldrb	w0, [x23,20]
	cmp	w0, 60
	bls	.L2782
	mov	w0, 60
	strb	w0, [x23,20]
.L2782:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2814
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 2781
	add	x2, x2, 568
	bl	printk
.L2814:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,624]
	ldrh	w1, [x1,10]
	cmp	w1, 1023
	bls	.L2783
	mov	w1, 2
	strb	w1, [x0,744]
.L2783:
	add	x24, x21, :lo12:.LANCHOR3
	mov	w0, 16384
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR6
	bl	ftl_malloc
	add	x25, x23, :lo12:.LANCHOR6
	str	x0, [x24,1528]
	mov	w0, 16384
	add	x26, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x24,1488]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x22,728]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x24,1536]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x24,1504]
	mov	w0, 64
	bl	ftl_malloc
	strb	wzr, [x25,220]
	str	x0, [x25,208]
	bl	flash_die_info_init
	ldrb	w0, [x26,26]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x26,28]
	bl	nandc_bch_sel
	str	xzr, [x24,1520]
	ldr	x2, [x22,624]
	ldrh	w0, [x2,16]
	ubfx	x1, x0, 8, 3
	strb	w1, [x22,792]
	ubfx	x1, x0, 3, 1
	strb	w1, [x24,1552]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,793]
	ubfx	x1, x0, 12, 1
	strb	w1, [x22,3568]
	ubfx	x1, x0, 13, 1
	strb	w1, [x22,3554]
	ubfx	x1, x0, 11, 1
	strb	w1, [x22,756]
	ldrb	w1, [x2,31]
	ldrb	w2, [x2,28]
	ubfx	x3, x1, 1, 1
	ubfx	x1, x1, 2, 1
	strb	w1, [x25,222]
	ubfx	x1, x0, 14, 1
	strb	w1, [x22,1]
	lsr	w0, w0, 15
	ldrb	w1, [x22,516]
	strb	w0, [x22,3555]
	mov	w0, 60
	strb	w3, [x25,221]
	cmp	w1, 9
	strb	w2, [x19,#:lo12:.LANCHOR0]
	strb	w0, [x22,632]
	bne	.L2784
	mov	w0, 70
	strb	w0, [x22,632]
.L2784:
	add	x0, x19, :lo12:.LANCHOR0
	strb	w2, [x0,634]
	add	x2, x20, :lo12:.LANCHOR2
	ldrb	w2, [x2,15]
	cmp	w2, 9
	bne	.L2785
	strb	wzr, [x0,634]
.L2785:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrb	w2, [x0,31]
	tbz	x2, 0, .L2786
	ldrb	w0, [x0,29]
	cbz	w0, .L2787
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 2
	b	.L2851
.L2787:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 3
.L2851:
	strb	w2, [x0,634]
.L2786:
	cmp	w1, 8
	bne	.L2788
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,760]
	cmp	w0, 137
	beq	.L2815
	cmp	w0, 44
	bne	.L2788
.L2815:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,36]
	cmp	w0, 3
	bne	.L2788
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,634]
.L2788:
	add	x1, x20, :lo12:.LANCHOR2
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x1, 8
	ldrb	w0, [x1,19]
	ldrh	w1, [x1,16]
	strb	w0, [x2,725]
	tbz	x1, 6, .L2791
	sub	w1, w0, #17
	cmp	w0, 21
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 2
	bls	.L2816
	cbz	w2, .L2792
.L2816:
	add	x0, x21, :lo12:.LANCHOR3
	adrp	x1, micron_read_retrial
	add	x1, x1, :lo12:micron_read_retrial
	str	x1, [x0,1520]
	mov	w1, 15
	cbz	w2, .L2852
	mov	w1, 4
	b	.L2852
.L2792:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2817
	cmp	w0, 33
	bne	.L2796
.L2817:
	add	x0, x21, :lo12:.LANCHOR3
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1520]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,726]
	b	.L2854
.L2796:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2818
	cbz	w2, .L2798
.L2818:
	add	x3, x21, :lo12:.LANCHOR3
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x3,1520]
	beq	.L2800
	cmp	w0, 68
	beq	.L2800
	mov	w0, 7
	strb	w0, [x3,1512]
	b	.L2801
.L2800:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, 17
	strb	w1, [x0,1512]
.L2801:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L2853
	mov	w1, 5
.L2853:
	strb	w1, [x0,726]
	b	.L2791
.L2798:
	sub	w0, w0, #36
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L2791
	add	x0, x21, :lo12:.LANCHOR3
	adrp	x1, toshiba_3d_read_retrial
	add	x1, x1, :lo12:toshiba_3d_read_retrial
	str	x1, [x0,1520]
.L2854:
	mov	w1, 7
.L2852:
	strb	w1, [x0,1512]
.L2791:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2803
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 2857
	add	x2, x2, 568
	bl	printk
.L2803:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,672]
	ldrb	w0, [x0,792]
	tbz	x0, 0, .L2804
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2855
.L2804:
	mov	w0, 4
.L2855:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2806
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2807
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 2875
	add	x2, x2, 568
	bl	printk
.L2807:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19,728]
	strb	wzr, [x19,752]
	strb	w1, [x0,32]
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	b	.L2849
.L2806:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L2808
	add	x21, x21, :lo12:.LANCHOR3
	adrp	x0, hynix_read_retrial
	add	x0, x0, :lo12:hynix_read_retrial
	str	x0, [x21,1520]
.L2808:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,792]
	tbz	x0, 2, .L2810
	ldr	x0, [x19,728]
	ldrb	w0, [x0,19]
	cbz	w0, .L2810
	add	x23, x23, :lo12:.LANCHOR6
	ldrh	w1, [x19,2]
	mov	w0, 0
	ldrb	w2, [x23,216]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,672]
	cbnz	w0, .L2810
	ldr	x1, [x19,728]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2810:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2812
	adrp	x2, .LANCHOR4
	adrp	x0, .LC136
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC136
	mov	w1, 2929
	add	x2, x2, 568
	bl	printk
.L2812:
	mov	w22, 0
	bl	nand_flash_print_info
.L2849:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	stp	x21, x22, [sp,80]
	stp	x25, x26, [sp,112]
	str	x27, [sp,128]
	mov	w21, 0
	bl	buf_alloc
	adrp	x23, .LC201
	ldr	x26, [x0,8]
	mov	x20, x0
	mov	w19, w21
	adrp	x27, .LANCHOR0
	add	x23, x23, :lo12:.LC201
.L2857:
	add	x3, x27, :lo12:.LANCHOR0
	ldrh	w0, [x3,3596]
	cmp	w0, w19
	bls	.L2869
	ldrh	w22, [x3,3634]
	ldr	x1, [x20,8]
	ldr	x2, [x20,24]
	madd	w22, w24, w22, w19
	ldrb	w3, [x3,2848]
	mov	w0, w22
	bl	ftl_read_ppa_page
	mov	w25, w0
	ldr	x7, [x20,8]
	mov	w1, w24
	ldr	x2, [x20,24]
	mov	w3, w22
	mov	w4, w25
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
	mov	x0, x23
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	cmp	w25, 512
	beq	.L2862
	cmn	w25, #1
	bne	.L2858
.L2862:
	mov	w21, 1
.L2858:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2857
.L2869:
	adrp	x0, .LC212
	add	x1, x26, 704
	add	x0, x0, :lo12:.LC212
	mov	w2, 4
	mov	w3, 32
	bl	rknand_print_hex
	mov	x0, x20
	bl	zbuf_free
	cbz	w21, .L2861
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1432
	add	x1, x1, 584
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2861:
	sub	sp, x29, #48
	mov	w0, w21
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldr	x27, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 144
	ret
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LC213
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC213
	ldrb	w1, [x19,2850]
	bl	printk
	ldrh	w3, [x20,1858]
	adrp	x0, .LC214
	ldrb	w1, [x20,1856]
	add	x0, x0, :lo12:.LC214
	ldrb	w2, [x20,1857]
	bl	printk
	ldr	x2, [x19,2880]
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x19,3596]
	mov	w2, 4
	ldrb	w3, [x19,3572]
	ldr	x1, [x20,-200]
	mul	w3, w0, w3
	adrp	x0, .LC219
	add	x0, x0, :lo12:.LC219
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,600]
	adrp	x0, .LC220
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC220
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,2880]
	adrp	x0, .LC212
	add	x0, x0, :lo12:.LC212
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,584]
	adrp	x0, .LC221
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC221
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC222
	add	x1, x19, 3640
	add	x0, x0, :lo12:.LC222
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3596]
	mov	w2, 2
	ldrb	w3, [x19,3572]
	ldr	x1, [x20,-192]
	mul	w3, w0, w3
	adrp	x0, .LC223
	add	x0, x0, :lo12:.LC223
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
	adrp	x5, .LANCHOR0
	mov	w6, 24
	add	x5, x5, :lo12:.LANCHOR0
	mov	w7, 1
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w3, [x5,744]
	ldrh	w4, [x5,3536]
	sub	w6, w6, w3
	ldrb	w3, [x5,3538]
	sub	w6, w6, w4
	lsr	w4, w2, w4
	lsl	w6, w7, w6
	sub	w6, w6, #1
	and	w4, w6, w4
	udiv	w4, w4, w3
	ldr	x3, [x5,584]
	add	x4, x3, x4, uxth 2
	ldrb	w3, [x4,2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 7
	cset	w4, eq
	cbnz	w4, .L2875
	cmp	w3, w7
	bne	.L2872
.L2875:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L2872:
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxth	x23, w1
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	cmp	w23, 31
	uxth	w21, w0
	bls	.L2878
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 32
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2878:
	adrp	x24, .LANCHOR0
	adrp	x20, .LANCHOR6
	add	x26, x24, :lo12:.LANCHOR0
	ldr	x0, [x26,2880]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L2879
	adrp	x0, .LC225
	mov	w1, w21
	add	x0, x0, :lo12:.LC225
	mov	x19, 0
	add	x20, x20, :lo12:.LANCHOR6
	bl	printk
	ldr	x0, [x19,8]
	mov	w1, 255
	ldrh	w2, [x20,224]
	bl	ftl_memset
	ldr	x0, [x26,2880]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L2889
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 36
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2881
.L2879:
	add	x0, x0, x21, sxtw 2
	adrp	x22, .LANCHOR3
	ldr	w19, [x0,704]
	add	x0, x22, :lo12:.LANCHOR3
	sub	x0, x0, #160
	lsl	x1, x23, 4
	add	x27, x0, x1
	strh	w21, [x0,x1]
	strh	wzr, [x27,2]
	cbnz	w19, .L2882
	adrp	x0, .LC226
	mov	w1, w21
	mov	w2, w19
	add	x20, x20, :lo12:.LANCHOR6
	add	x0, x0, :lo12:.LC226
	bl	printk
	ldr	x0, [x27,8]
	mov	w1, 255
	ldrh	w2, [x20,224]
	bl	ftl_memset
	b	.L2881
.L2882:
	add	x25, x20, :lo12:.LANCHOR6
	ldrb	w3, [x26,2848]
	ldr	x1, [x27,8]
	mov	w0, w19
	ldr	x2, [x25,192]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x25,192]
	ldr	w2, [x0]
	cmp	w2, w21
	bne	.L2883
	cmn	w3, #1
	beq	.L2883
	cmp	w3, 512
	beq	.L2883
.L2886:
	add	x20, x20, :lo12:.LANCHOR6
	ldr	x0, [x20,192]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L2889
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 63
	add	x1, x1, 600
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2889
.L2883:
	adrp	x0, .LC227
	mov	w4, w19
	add	x24, x24, :lo12:.LANCHOR0
	mov	w1, w21
	add	x0, x0, :lo12:.LC227
	add	x22, x22, :lo12:.LANCHOR3
	sub	x22, x22, #160
	bl	printk
	add	x22, x22, x23, lsl 4
	ldr	x3, [x24,2880]
	adrp	x0, .LC228
	add	x0, x0, :lo12:.LC228
	mov	w2, 4
	add	x1, x3, 704
	add	x23, x20, :lo12:.LANCHOR6
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x24,2848]
	adrp	x0, .LC229
	ldr	x1, [x22,8]
	add	x0, x0, :lo12:.LC229
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldr	x1, [x23,192]
	adrp	x0, .LC230
	add	x0, x0, :lo12:.LC230
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x1, [x22,8]
	mov	w0, w19
	ldr	x2, [x23,192]
	mov	w19, -1
	ldrb	w3, [x24,2848]
	bl	ftl_read_ppa_page
	ldr	x1, [x23,192]
	ldr	w1, [x1]
	cmp	w1, w21
	bne	.L2881
	cmp	w0, w19
	beq	.L2888
	cmp	w0, 512
	bne	.L2886
	b	.L2888
.L2889:
	mov	w19, 0
	b	.L2881
.L2888:
	mov	w19, -1
.L2881:
	mov	w0, w19
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	add	x0, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x25, .LANCHOR3
	ldr	x0, [x0,2880]
	add	x1, x25, :lo12:.LANCHOR3
	ldrh	w2, [x0,688]
	ldrh	w0, [x1,1456]
	sub	w0, w0, #1
	cmp	w2, w0
	bge	.L2891
	ldr	w0, [x1,1448]
	cbz	w0, .L2892
.L2891:
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x25, :lo12:.LANCHOR3
	ldr	x2, [x1,2880]
	ldrh	w1, [x3,1456]
	ldrh	w4, [x2,688]
	sub	w1, w1, #1
	cmp	w4, w1
	bge	.L2893
	ldr	w1, [x3,1448]
	cbz	w1, .L2892
.L2893:
	add	x1, x25, :lo12:.LANCHOR3
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w20, 65535
	str	wzr, [x1,1448]
	cmp	w21, w20
	bne	.L2895
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 162
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2880]
	add	x0, x1, x0, uxth 1
	ldrh	w21, [x0,416]
	cmp	w21, w20
	beq	.L2892
.L2895:
	bl	pm_select_ram_region
	uxth	x20, w0
	add	x0, x25, :lo12:.LANCHOR3
	lsl	x1, x20, 4
	sub	x0, x0, #160
	mov	x22, x20
	add	x23, x0, x1
	ldrh	w0, [x0,x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2896
	ldr	x1, [x23,8]
	cbz	x1, .L2896
	ldrsh	w2, [x23,2]
	tbz	w2, #31, .L2896
	bl	pm_write_page
	ldrh	w0, [x23,2]
	and	w0, w0, 32767
	strh	w0, [x23,2]
.L2896:
	add	x25, x25, :lo12:.LANCHOR3
	mov	w26, 0
	sub	x25, x25, #160
	mov	w23, 24
	mov	w24, 1
	add	x20, x25, x20, lsl 4
.L2897:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x0, [x3,2880]
	ldrh	w1, [x0,698]
	cmp	w1, w26
	bls	.L2907
	ldrb	w2, [x3,744]
	add	x0, x0, x26, sxtw 2
	ldrh	w1, [x3,3536]
	ldr	w0, [x0,704]
	sub	w2, w23, w2
	lsr	w0, w0, w1
	sub	w1, w2, w1
	lsl	w1, w24, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x3,3538]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L2898
	mov	w0, w26
	mov	w1, w22
	bl	load_l2p_region
	cbnz	w0, .L2899
	ldr	x1, [x20,8]
	mov	w0, w26
	bl	pm_write_page
.L2899:
	mov	w0, -1
	strh	w0, [x20]
.L2898:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L2897
.L2907:
	bl	pm_free_sblk
.L2892:
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
	sub	x1, x19, #160
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,-168]
	cbz	w0, .L2909
	bl	pm_gc
	str	wzr, [x19,-168]
.L2909:
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
	sub	x20, x20, #160
.L2915:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2914
	bl	pm_flush_id
.L2914:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2915
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
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR6
	add	x22, x21, :lo12:.LANCHOR6
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR3
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x20, x19, :lo12:.LANCHOR3
	mov	w23, w0
	mov	w0, 1
	strb	w0, [x22,200]
	mov	w0, 64
	mov	w24, -1
	str	wzr, [x20,1448]
	adrp	x25, .LANCHOR0
	str	wzr, [x20,-168]
	bl	ftl_malloc
	str	x0, [x22,192]
	sub	x22, x20, #160
	add	x20, x20, 352
.L2921:
	strh	w24, [x22]
	strh	wzr, [x22,2]
	cbz	w23, .L2920
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w0, [x0,2848]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x22,8]
.L2920:
	add	x22, x22, 16
	cmp	x22, x20
	bne	.L2921
	add	x0, x19, :lo12:.LANCHOR3
	adrp	x20, .LANCHOR0
	add	x23, x20, :lo12:.LANCHOR0
	mov	w4, 4
	ldr	x26, [x0,-152]
	add	x0, x21, :lo12:.LANCHOR6
	ldr	x1, [x23,2880]
	mov	x2, x26
	ldr	x22, [x0,192]
	ldrb	w0, [x1,694]
	ldrh	w1, [x1,692]
	mov	x3, x22
	bl	flash_get_last_written_page
	sxth	w24, w0
	ldr	x1, [x23,2880]
	mov	w25, w24
	ldrh	w2, [x1,696]
	cmp	w2, w24
	bgt	.L2922
	adrp	x0, .LC231
	ldrh	w1, [x1,692]
	add	x0, x0, :lo12:.LC231
	mov	w3, w24
	adrp	x27, .LC232
	adrp	x28, .LC233
	bl	printk
	add	x28, x28, :lo12:.LC233
	ldr	x0, [x23,2880]
	ldrsh	w23, [x0,696]
	add	w0, w24, 1
	str	w0, [x29,124]
	add	x0, x27, :lo12:.LC232
	str	x0, [x29,112]
.L2923:
	ldr	w0, [x29,124]
	cmp	w23, w0
	bge	.L2944
	add	x27, x20, :lo12:.LANCHOR0
	mov	x2, x26
	mov	x3, x22
	ldr	x0, [x27,2880]
	ldrh	w1, [x27,3634]
	ldrb	w4, [x27,2848]
	ldrh	w24, [x0,692]
	ldrb	w0, [x0,694]
	madd	w24, w24, w1, w23
	mov	w1, w24
	bl	flash_read_page_en
	mov	w4, w0
	ldr	x3, [x27,2880]
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
	beq	.L2924
	cmn	w4, #1
	beq	.L2924
	ldr	x0, [x27,2880]
	ldr	w1, [x22]
	ldrh	w0, [x0,698]
	cmp	w1, w0
	bcs	.L2924
	ldr	w2, [x22,8]
	cbz	w2, .L2925
	ldrb	w1, [x27,2848]
	mov	x0, x26
	str	x2, [x29,104]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2925
	ldr	w1, [x22,8]
	mov	x0, x28
	bl	printk
	b	.L2924
.L2925:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0,2880]
	ldr	w0, [x22]
	add	x0, x0, 176
	str	w24, [x1,x0,lsl 2]
.L2924:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L2923
.L2944:
	adrp	x0, .LANCHOR5+897
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 1
	add	w25, w25, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+897]
	ldr	x0, [x20,2880]
	strh	w25, [x0,696]
	bl	pm_free_sblk
.L2922:
	add	x19, x19, :lo12:.LANCHOR3
	add	x21, x21, :lo12:.LANCHOR6
	mov	w1, 255
	ldrh	w2, [x21,224]
	ldr	x0, [x19,-152]
	bl	ftl_memset
	ldr	x1, [x19,-152]
	mov	w0, -1
	bl	pm_write_page
	adrp	x0, .LANCHOR5+897
	ldrb	w0, [x0,#:lo12:.LANCHOR5+897]
	cbz	w0, .L2927
	ldr	x1, [x19,-152]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,-152]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,-152]
	mov	w0, -1
	bl	pm_write_page
.L2927:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w25, w2
	mov	x23, x1
	ldrb	w20, [x0,2848]
	ldr	w2, [x0,2856]
	lsl	w24, w20, 7
	lsl	w20, w20, 7
	cmp	w3, w2
	udiv	w24, w3, w24
	uxth	w26, w24
	msub	w20, w26, w20, w3
	uxth	x20, w20
	bcc	.L2946
	adrp	x0, .LC234
	mov	w1, w3
	add	x0, x0, :lo12:.LC234
	bl	printk
	mov	w0, -1
	cbnz	w25, .L2947
	str	w0, [x23]
	b	.L2947
.L2946:
	adrp	x21, .LANCHOR3
	mov	x19, 0
	add	x1, x21, :lo12:.LANCHOR3
	sub	x1, x1, #160
.L2952:
	ldr	x0, [x1,8]
	cbz	x0, .L2948
	ldrh	w0, [x1]
	cmp	w0, w26
	bne	.L2948
.L2949:
	cbnz	w25, .L2950
	add	x0, x21, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,-152]
	ldr	w1, [x0,x20,lsl 2]
	mov	w0, 24
	str	w1, [x23]
	ldrb	w2, [x22,744]
	sub	w0, w0, w2
	lsr	w0, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldrb	w1, [x22,3636]
	sub	w2, w2, #1
	and	w0, w0, w2
	cmp	w0, w1
	bcc	.L2951
	mov	w0, -1
	str	w0, [x23]
	b	.L2951
.L2950:
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w3, [x23]
	sub	x0, x2, #160
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w3, [x1,x20,lsl 2]
	strb	w24, [x2,352]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
.L2951:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 32767
	sub	x21, x21, #160
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2947
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2947
.L2948:
	add	w19, w19, 1
	add	x1, x1, 16
	uxth	x19, w19
	cmp	w19, 32
	bne	.L2952
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR3
	sbfiz	x3, x19, 4, 32
	sub	x2, x2, #160
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	beq	.L2953
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2953
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
.L2953:
	adrp	x0, .LANCHOR6+226
	strb	w19, [x0,#:lo12:.LANCHOR6+226]
	mov	w0, w26
	bl	load_l2p_region
	b	.L2949
.L2947:
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
	sub	sp, sp, #208
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	stp	x27, x28, [sp,112]
	adrp	x22, .LANCHOR3
	strb	wzr, [x20,3569]
	ldr	x21, [x20,608]
	add	x0, x22, :lo12:.LANCHOR3
	add	x23, x21, 80
	ldrh	w1, [x21,80]
	strb	wzr, [x0,1418]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L2961
	mov	w0, -1
	strh	w0, [x21,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	ldrb	w0, [x23,9]
	ldrh	w1, [x20,3634]
	sub	w0, w0, #1
	add	x0, x23, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x20,3553]
	str	w0, [x29,168]
	cmp	w1, 2
	beq	.L2962
	ldrb	w2, [x20,756]
	cbz	w2, .L2963
.L2962:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w2, [x2,3570]
	sub	w2, w2, #1
	add	w0, w2, w0
	orr	w0, w0, w1, lsl 24
	b	.L3038
.L2963:
	cmp	w1, 3
	bne	.L2964
	ldrh	w1, [x20,3596]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
.L3038:
	str	w0, [x27,40]
.L2964:
	mov	x0, x27
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x27,52]
	cmp	w0, 512
	beq	.L2965
	cmn	w0, #1
	cset	w26, eq
	cbnz	w26, .L2965
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2966
.L2965:
	mov	x0, x27
	mov	w20, 0
	bl	zbuf_free
	ldr	x7, [x27,24]
	ldr	x6, [x27,8]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC235
	add	x0, x0, :lo12:.LC235
	ldr	w1, [x27,40]
	ldr	w2, [x27,52]
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x21,80]
	ldr	x1, [x0,600]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,608]
	ldrh	w1, [x21,80]
	strh	w1, [x0,130]
	b	.L2967
.L2966:
	add	x24, x19, :lo12:.LANCHOR0
	add	x25, x22, :lo12:.LANCHOR3
	ldr	x1, [x27,8]
	ldrh	w0, [x24,3570]
	ldrb	w2, [x24,3572]
	mul	w2, w0, w2
	ldr	x0, [x25,1424]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w20, [x24,2848]
	ldrh	w0, [x24,3570]
	ldrb	w3, [x24,3572]
	mul	w3, w0, w3
	lsl	w2, w3, 2
	cmp	w20, w3, lsr 6
	bge	.L2968
	ldr	x0, [x25,1432]
	mov	w1, w26
	lsl	w20, w20, 7
	sub	w20, w20, w3
	bl	ftl_memset
	lsl	w20, w20, 2
	ldrh	w0, [x24,3570]
	ldrb	w1, [x24,3572]
	ldr	x2, [x27,8]
	mul	w1, w0, w1
	add	x1, x2, x1, sxtw 2
	ldr	x0, [x25,1432]
	mov	w2, w20
	bl	ftl_memcpy
	mov	w10, 1
	b	.L2969
.L2968:
	ldr	x0, [x25,1432]
	mov	w20, w26
	ldr	x1, [x27,8]
	add	x1, x1, x3, sxtw 2
	bl	ftl_memcpy
	mov	w10, w26
.L2969:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w24, [x0,3553]
	ldrh	w26, [x0,3596]
	cmp	w24, 2
	bne	.L2970
	ldrb	w0, [x0,3554]
	cbz	w0, .L2971
.L2970:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,756]
	cbz	w0, .L2972
.L2971:
	mul	w26, w26, w24
	mov	w24, 1
.L2972:
	mov	w28, 0
	adrp	x25, .LC236
	add	x0, x25, :lo12:.LC236
	mov	w13, w28
	str	x0, [x29,136]
.L2973:
	cmp	w28, w26
	bcs	.L2985
	sub	w0, w28, #1
	mov	w25, 0
	str	w0, [x29,148]
.L2986:
	ldrb	w0, [x23,9]
	cmp	w25, w0
	bge	.L2982
	sxtw	x0, w25
	mov	w11, 1
	str	x0, [x29,152]
.L2983:
	cmp	w11, w24
	bhi	.L3039
	ldr	x0, [x29,152]
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w1, [x2,3634]
	ldrh	w0, [x23,x0,lsl 1]
	mul	w0, w0, w1
	ldrb	w1, [x2,3553]
	str	w0, [x29,168]
	cmp	w1, 2
	beq	.L2974
	ldrb	w2, [x2,756]
	cbz	w2, .L2975
.L2974:
	ldr	w2, [x29,148]
	add	w0, w2, w0
	add	w0, w0, w11
	orr	w0, w0, w1, lsl 24
	b	.L2977
.L2975:
	cmp	w1, 3
	add	w0, w0, w28
	bne	.L2977
	orr	w0, w0, w11, lsl 24
.L2977:
	str	w0, [x27,40]
	mov	w1, 1
	mov	x0, x27
	str	x10, [x29,112]
	str	x11, [x29,120]
	str	x13, [x29,128]
	bl	sblk_read_page
	ldr	x13, [x29,128]
	ldr	x10, [x29,112]
	ldr	x11, [x29,120]
	sxtw	x14, w13
	cbz	w10, .L2978
	add	x0, x22, :lo12:.LANCHOR3
	lsl	x1, x14, 2
	ldr	x0, [x0,1432]
	ldr	w2, [x0,x1]
	cbnz	w2, .L2978
	ldr	x2, [x27,24]
	ldr	w2, [x2,8]
	str	w2, [x0,x1]
.L2978:
	add	x0, x22, :lo12:.LANCHOR3
	lsl	x14, x14, 2
	ldr	x7, [x27,24]
	ldr	x1, [x0,1424]
	ldr	w6, [x7,4]
	ldr	w3, [x1,x14]
	cmp	w3, w6
	bne	.L2979
	ldr	x0, [x0,1432]
	ldr	w1, [x0,x14]
	ldr	w0, [x7,8]
	cmp	w1, w0
	beq	.L3006
.L2979:
	add	x15, x22, :lo12:.LANCHOR3
	ldr	w0, [x7,12]
	str	x10, [x29,96]
	str	x11, [x29,104]
	ldr	x4, [x15,1432]
	str	w0, [sp]
	ldr	x0, [x29,136]
	ldr	w4, [x4,x14]
	ldr	w5, [x7]
	ldr	w1, [x27,40]
	ldr	w2, [x27,52]
	ldr	w7, [x7,8]
	str	x13, [x29,112]
	str	x15, [x29,120]
	str	x14, [x29,128]
	bl	printk
	ldr	x15, [x29,120]
	ldr	x14, [x29,128]
	ldr	x13, [x29,112]
	ldr	x0, [x15,1424]
	ldr	x11, [x29,104]
	ldr	x10, [x29,96]
	ldr	w0, [x0,x14]
	cmn	w0, #1
	beq	.L3006
	mov	x0, x27
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x21,80]
	ldr	x1, [x0,600]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,608]
	ldrh	w1, [x21,80]
	strh	w1, [x0,130]
	b	.L2967
.L3006:
	add	w13, w13, 1
	add	w11, w11, 1
	b	.L2983
.L3039:
	add	w25, w25, 1
	b	.L2986
.L2982:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3554]
	cmp	w0, wzr
	csinc	w28, w28, w28, eq
	add	w28, w28, 1
	b	.L2973
.L2985:
	mov	x0, x27
	mov	w20, 0
	mov	w28, w20
	bl	zbuf_free
	str	w20, [x29,152]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x23,9]
	ldrh	w3, [x21,80]
	ldr	x2, [x0,600]
	ldrh	w0, [x0,3570]
	mul	w0, w1, w0
	strh	w0, [x2,x3,lsl 1]
.L2987:
	cmp	w28, w26
	bcs	.L3000
	sub	w0, w28, #1
	mov	w25, 0
	str	w0, [x29,120]
.L3001:
	ldrb	w0, [x23,9]
	cmp	w25, w0
	bge	.L2997
	sxtw	x0, w25
	mov	w27, 1
	str	x0, [x29,128]
.L2998:
	cmp	w27, w24
	bhi	.L3040
	add	x1, x22, :lo12:.LANCHOR3
	ldr	w0, [x29,152]
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1424]
	ldr	w5, [x2,x0]
	cmn	w5, #1
	beq	.L2988
	ldr	x1, [x1,1432]
	str	x5, [x29,136]
	ldr	w0, [x1,x0]
	str	w0, [x29,148]
	mov	w0, w5
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x5, [x29,136]
	bne	.L2989
	mov	w0, w5
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
	ldr	x5, [x29,136]
.L2989:
	ldr	x0, [x29,128]
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w1, [x23,x0,lsl 1]
	ldrh	w0, [x2,3634]
	mul	w1, w1, w0
	ldrb	w0, [x2,3553]
	cmp	w0, 2
	beq	.L2990
	ldrb	w2, [x2,756]
	cbz	w2, .L2991
.L2990:
	ldr	w2, [x29,120]
	add	w1, w2, w1
	add	w1, w1, w27
	orr	w1, w1, w0, lsl 24
	b	.L2993
.L2991:
	cmp	w0, 3
	add	w1, w1, w28
	bne	.L2993
	orr	w1, w1, w27, lsl 24
.L2993:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,148]
	str	w1, [x29,168]
	mov	w3, 24
	ldrh	w1, [x0,3536]
	lsr	w6, w2, w1
	ldrb	w2, [x0,744]
	ldrb	w0, [x0,3538]
	sub	w2, w3, w2
	sub	w1, w2, w1
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w1, w6, w1
	udiv	w1, w1, w0
	uxth	w0, w1
	ldr	w1, [x29,148]
	str	w0, [x29,136]
	ldr	w0, [x29,172]
	cmp	w0, w1
	bne	.L2994
	mov	w0, w5
	add	x1, x29, 168
	add	w20, w20, 1
	bl	pm_log2phys
	ldrh	w0, [x29,136]
	bl	ftl_vpn_decrement
	b	.L2995
.L2994:
	ldr	w1, [x29,168]
	cmp	w0, w1
	csinc	w20, w20, w20, ne
.L2995:
	ldr	w0, [x29,152]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,152]
	ldr	w0, [x29,136]
	ldr	x1, [x2,584]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L2988
	lsl	x0, x0, 1
	ldr	x2, [x2,600]
	ldrh	w5, [x2,x0]
	cbz	w5, .L2988
	strh	w1, [x2,x0]
.L2988:
	add	w27, w27, 1
	b	.L2998
.L3040:
	add	w25, w25, 1
	b	.L3001
.L2997:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3554]
	cmp	w0, wzr
	csinc	w28, w28, w28, eq
	add	w28, w28, 1
	b	.L2987
.L3000:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x21,80]
	ldr	x0, [x0,600]
	strh	w20, [x0,x1,lsl 1]
	ldrh	w0, [x21,80]
	bl	zftl_insert_data_list
.L2967:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,80]
	ubfiz	x2, x0, 1, 16
	ldr	x1, [x1,600]
	ldrh	w1, [x1,x2]
	cbnz	w1, .L3002
	bl	ftl_dump_write_open_sblk
.L3002:
	ldrh	w1, [x21,80]
	adrp	x0, .LC237
	mov	w2, w20
	add	x0, x0, :lo12:.LC237
	bl	printk
	mov	w0, -1
	strh	w0, [x21,80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldr	x0, [x0,608]
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L3003
	bl	ftl_free_sblk
.L3003:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,2880]
	strh	w0, [x1,126]
	ldr	x1, [x19,608]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2960
.L2961:
	ldrh	w0, [x21,130]
	cmp	w0, w1
	beq	.L2960
	ldr	x1, [x20,2880]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L3005
	bl	pm_flush
	ldr	x0, [x20,608]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x20,2880]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L3005:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,608]
	strh	w1, [x0,130]
.L2960:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldp	x27, x28, [sp,112]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 208
	ret
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	zftl_init
	.type	zftl_init, %function
zftl_init:
	stp	x29, x30, [sp, -64]!
	mov	w1, -1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR3
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	add	x0, x22, :lo12:.LANCHOR3
	adrp	x23, .LANCHOR6
	adrp	x19, .LANCHOR0
	add	x24, x23, :lo12:.LANCHOR6
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR2
	strb	w1, [x0,-173]
	strb	w1, [x0,1544]
	strb	wzr, [x0,-172]
	mov	w0, -1
	strb	w1, [x21,2904]
	adrp	x1, .LC2
	str	w0, [x24,228]
	adrp	x0, .LC1
	add	x1, x1, :lo12:.LC2
	add	x0, x0, :lo12:.LC1
	strb	wzr, [x21,2872]
	strb	wzr, [x21,2873]
	bl	printk
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w6, [x21,633]
	strb	w6, [x21,3636]
	ldrh	w4, [x21,2]
	ldrb	w2, [x1,21]
	ldrb	w7, [x1,20]
	ldrh	w3, [x1,18]
	mul	w6, w2, w6
	ldrb	w0, [x1,17]
	ldrh	w8, [x1,22]
	mov	w1, 0
	sdiv	w9, w3, w7
	strb	w2, [x21,3538]
	strb	w7, [x21,3553]
	uxtb	w6, w6
	strb	w0, [x21,2848]
	ubfiz	w10, w0, 9, 7
	strh	w3, [x21,3570]
	mov	w2, 1
	strh	w9, [x21,3596]
	strb	w6, [x21,3572]
	strh	w8, [x21,576]
	strh	w4, [x21,3634]
	mov	x21, x23
	strh	w10, [x24,224]
.L3042:
	cmp	w2, w4
	bhi	.L3113
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L3042
.L3113:
	mul	w3, w0, w3
	add	x2, x19, :lo12:.LANCHOR0
	sub	w1, w1, #1
	mul	w4, w3, w8
	strh	w1, [x2,3536]
	mov	w1, 1
	lsr	w5, w4, 21
	mov	w2, 0
.L3044:
	cmp	w1, w5
	bhi	.L3114
	add	w2, w2, 1
	lsl	w1, w1, 1
	uxth	w2, w2
	b	.L3044
.L3114:
	mov	w1, 57344
	mov	w5, 1
	movk	w1, 0x1c, lsl 16
	sub	w2, w2, #1
	lsl	w2, w5, w2
	add	x5, x19, :lo12:.LANCHOR0
	mul	w1, w6, w1
	mul	w4, w4, w6
	str	w4, [x5,2852]
	add	x11, x21, :lo12:.LANCHOR6
	mul	w1, w1, w2
	mul	w9, w9, w6
	add	w4, w1, 24576
	str	w4, [x11,232]
	mul	w3, w3, w6
	udiv	w2, w4, w0
	sub	w0, w10, #1
	str	w2, [x5,2856]
	sub	w3, w3, #1
	str	w1, [x5,520]
	add	w0, w0, w2, lsl 2
	add	x2, x22, :lo12:.LANCHOR3
	udiv	w4, w4, w3
	add	w4, w4, 8
	strh	w4, [x5,3604]
	udiv	w0, w0, w10
	strh	w0, [x11,236]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w9
	strh	w0, [x2,1456]
	add	w0, w8, w8, lsl 1
	asr	w0, w0, 2
	strh	w0, [x2,-178]
	lsr	w0, w8, 4
	strh	w0, [x5,3602]
	cmp	w0, 79
	bhi	.L3046
	mov	w0, 80
	strh	w0, [x5,3602]
.L3046:
	add	x0, x22, :lo12:.LANCHOR3
	mov	w2, 2000
	add	x3, x19, :lo12:.LANCHOR0
	cmp	w7, 2
	strh	w2, [x0,-176]
	mov	w2, 50
	strh	w2, [x0,-180]
	mov	w2, 256
	strh	w2, [x0,1452]
	mov	w2, 48
	strh	w2, [x0,1454]
	mov	w2, 32
	strh	w2, [x3,592]
	beq	.L3047
	ldrb	w4, [x3,3568]
	cbz	w4, .L3048
.L3047:
	add	x0, x22, :lo12:.LANCHOR3
	mov	w2, 150
	mov	w3, 12
	strh	w2, [x0,-180]
	mov	w2, 64
	strh	w2, [x0,1454]
	add	x2, x19, :lo12:.LANCHOR0
	strh	w3, [x2,592]
	ldrb	w3, [x19,#:lo12:.LANCHOR0]
	cbnz	w3, .L3050
	mov	w3, 4
	strh	w3, [x2,592]
	mov	w2, 600
	strh	w2, [x0,-176]
	mov	w2, 128
	strh	w2, [x0,1452]
	b	.L3050
.L3048:
	ldrb	w3, [x3,756]
	cbz	w3, .L3050
	mov	w3, 1200
	strh	w2, [x0,-180]
	strh	w3, [x0,-176]
	strh	w2, [x0,1454]
.L3050:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3052
	adrp	x0, .LC238
	add	x0, x0, :lo12:.LC238
	bl	printk
.L3052:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3053
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC239
	add	x0, x0, :lo12:.LC239
	ldr	w1, [x1,2852]
	bl	printk
.L3053:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3054
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC240
	add	x0, x0, :lo12:.LC240
	ldr	w1, [x1,2856]
	bl	printk
.L3054:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3055
	add	x1, x21, :lo12:.LANCHOR6
	adrp	x0, .LC241
	add	x0, x0, :lo12:.LC241
	ldr	w1, [x1,232]
	bl	printk
.L3055:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3056
	add	x1, x21, :lo12:.LANCHOR6
	adrp	x0, .LC242
	add	x0, x0, :lo12:.LC242
	ldrh	w1, [x1,236]
	bl	printk
.L3056:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3057
	add	x1, x21, :lo12:.LANCHOR6
	adrp	x0, .LC243
	add	x0, x0, :lo12:.LC243
	ldrh	w1, [x1,224]
	bl	printk
.L3057:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3058
	add	x1, x22, :lo12:.LANCHOR3
	adrp	x0, .LC244
	add	x0, x0, :lo12:.LC244
	ldrh	w1, [x1,1456]
	bl	printk
.L3058:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3059
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC245
	add	x0, x0, :lo12:.LC245
	ldrh	w1, [x1,3602]
	bl	printk
.L3059:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3060
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC246
	add	x0, x0, :lo12:.LC246
	ldrh	w1, [x1,3604]
	bl	printk
.L3060:
	add	x23, x19, :lo12:.LANCHOR0
	bl	zbuf_init
	mov	w0, 6
	add	x22, x22, :lo12:.LANCHOR3
	ldrh	w1, [x23,576]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x23,528]
	ldrb	w1, [x23,3572]
	ldrh	w0, [x23,3596]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22,-192]
	ldrb	w1, [x23,3572]
	adrp	x4, .LANCHOR5
	ldrh	w0, [x23,3596]
	add	x2, x4, :lo12:.LANCHOR5
	add	x2, x2, 960
	str	x2, [x22,-200]
	mul	w1, w0, w1
	ldrh	w0, [x23,576]
	lsl	w1, w1, 1
	add	x3, x2, x1, sxtw 2
	str	x3, [x23,600]
	add	w1, w1, w0, lsr 1
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	add	x1, x2, x1, sxtw 2
	str	x1, [x23,608]
	mov	x23, x4
	tbz	x0, 12, .L3061
	adrp	x0, .LC247
	add	x0, x0, :lo12:.LC247
	bl	printk
.L3061:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR6
	ldrh	w0, [x2,3596]
	ldrb	w22, [x2,3572]
	ldrh	w1, [x2,576]
	ldrh	w24, [x3,236]
	mul	w22, w0, w22
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	lsl	w24, w24, 2
	add	w24, w24, w1, lsl 2
	add	w22, w1, w22, lsl 2
	add	w24, w24, 704
	lsl	w22, w22, 1
	add	w22, w22, 632
	tbz	x0, 12, .L3062
	adrp	x0, .LC248
	ldrh	w3, [x3,224]
	add	x0, x0, :lo12:.LC248
	mov	w1, w22
	mov	w2, w24
	bl	printk
.L3062:
	add	x21, x21, :lo12:.LANCHOR6
	ldrh	w0, [x21,224]
	cmp	w22, w0
	bhi	.L3064
	cmp	w24, w0
	bls	.L3063
.L3064:
.L3065:
	b	.L3065
.L3063:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L3066
	bl	ftl_ext_info_init
	add	x19, x19, :lo12:.LANCHOR0
	bl	gc_init
	add	x23, x23, :lo12:.LANCHOR5
	mov	w0, 1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	x0, [x19,608]
	mov	x1, 0
	add	x0, x0, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,608]
	add	x0, x1, 48
	add	x1, x1, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,2880]
	ldr	w0, [x1,8]
	add	w0, w0, 16
	str	w0, [x1,8]
	ldr	x0, [x19,608]
	add	x0, x0, 16
	bl	ftl_info_data_recovery
	ldr	x0, [x19,608]
	add	x0, x0, 48
	bl	ftl_info_data_recovery
	ldr	x0, [x19,608]
	add	x0, x0, 80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	w0, 1
	bl	ftl_total_vpn_update
	ldrb	w0, [x23,897]
	cbz	w0, .L3067
	ldr	x1, [x19,2880]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L3067:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L3066:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
	adrp	x21, .LANCHOR3
	ldr	x0, [x0,#:lo12:jiffies]
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR3
	adrp	x20, .LANCHOR6
	add	x20, x20, :lo12:.LANCHOR6
	str	x0, [x19,1464]
	mov	w0, 136
	strb	wzr, [x20,238]
	bl	ftl_malloc
	str	x0, [x19,1472]
	cbnz	x0, .L3116
.L3118:
	mov	w0, -1
	b	.L3117
.L3116:
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x20,240]
	mov	x1, x20
	add	x0, x19, 1480
	str	xzr, [x19,1480]
	str	xzr, [x1,248]!
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1480]
	cbz	x0, .L3118
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20,240]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1472]
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
	ldr	x0, [x19,1480]
	bl	nand_flash_init
	mov	w22, w0
	cbnz	w0, .L3119
	bl	zftl_init
	mov	w19, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x20,238]
	b	.L3120
.L3119:
	ldr	x1, [x19,1472]
	adrp	x2, ftl_read_flash_info
	add	x2, x2, :lo12:ftl_read_flash_info
	adrp	x0, ftl_deinit
	add	x0, x0, :lo12:ftl_deinit
	str	x2, [x1,112]
	adrp	x2, ftl_read
	add	x2, x2, :lo12:ftl_read
	str	x2, [x1,40]
	adrp	x2, ftl_write
	str	x0, [x1,80]
	add	x2, x2, :lo12:ftl_write
	str	x2, [x1,48]
	adrp	x2, ftl_sys_read
	adrp	x0, ftl_cache_flush
	add	x2, x2, :lo12:ftl_sys_read
	str	x2, [x1,24]
	adrp	x2, ftl_sys_write
	add	x0, x0, :lo12:ftl_cache_flush
	add	x2, x2, :lo12:ftl_sys_write
	str	x2, [x1,32]
	adrp	x2, ftl_vendor_read
	str	x0, [x1,64]
	add	x2, x2, :lo12:ftl_vendor_read
	str	x2, [x1,8]
	adrp	x2, ftl_vendor_write
	adrp	x0, ftl_flash_suspend
	add	x2, x2, :lo12:ftl_vendor_write
	str	x2, [x1,16]
	adrp	x2, ftl_nandc_get_irq_status
	add	x0, x0, :lo12:ftl_flash_suspend
	add	x2, x2, :lo12:ftl_nandc_get_irq_status
	str	x0, [x1,88]
	str	x2, [x1,120]
	adrp	x0, ftl_flash_resume
	adrp	x2, ftl_proc_ftl_read
	add	x0, x0, :lo12:ftl_flash_resume
	add	x2, x2, :lo12:ftl_proc_ftl_read
	str	x0, [x1,96]
	str	x2, [x1,128]
	adrp	x0, ftl_get_density
	adrp	x2, ftl_do_gc
	add	x0, x0, :lo12:ftl_get_density
	add	x2, x2, :lo12:ftl_do_gc
	str	x0, [x1,72]
	str	x2, [x1,104]
	adrp	x2, ftl_discard
	ldr	x0, [x19,1480]
	add	x2, x2, :lo12:ftl_discard
	str	x2, [x1,56]
	bl	FlashInit
	mov	w19, w0
	cmn	w22, #2
	adrp	x22, .LANCHOR2
	bne	.L3121
	add	x20, x22, :lo12:.LANCHOR2
	adrp	x1, gNandParaInfo
	add	x20, x20, 8
	add	x1, x1, :lo12:gNandParaInfo
	mov	w2, 32
	mov	x0, x20
	bl	ftl_memcpy
	ldrb	w0, [x20,18]
	bl	flash_lsb_page_tbl_build
	ldrh	w0, [x20,10]
	strh	w0, [x20,26]
.L3121:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x0, .LANCHOR0
	bne	.L3122
	adrp	x1, gFlashSlcMode
	ldrb	w1, [x1,#:lo12:gFlashSlcMode]
	cbnz	w1, .L3123
.L3122:
	add	x1, x0, :lo12:.LANCHOR0
	strb	wzr, [x0,#:lo12:.LANCHOR0]
	strb	wzr, [x1,634]
.L3123:
	adrp	x2, gNandFlashIDBEccBits
	add	x1, x0, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR3
	ldrb	w2, [x2,#:lo12:gNandFlashIDBEccBits]
	strb	w2, [x1,632]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x22,18]
	str	xzr, [x21,1520]
	ldrb	w2, [x2,#:lo12:gNandFlashEccBits]
	strb	w2, [x1,724]
	mov	w2, 1
	mov	w1, 0
.L3124:
	cmp	w2, w3
	bhi	.L3129
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L3124
.L3129:
	add	x0, x0, :lo12:.LANCHOR0
	sub	w1, w1, #1
	strb	wzr, [x0,672]
	strh	w1, [x0,3536]
	cbnz	w19, .L3120
	bl	FtlInit
	mov	w19, w0
.L3120:
	adrp	x0, .LC249
	mov	w1, w19
	add	x0, x0, :lo12:.LC249
	bl	printk
	mov	w0, w19
.L3117:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	gc_update_l2p_map_new
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x22, .LANCHOR2
	ldr	x20, [x0,608]
	ldrh	w24, [x0,3570]
	add	x0, x20, 80
	str	x0, [x29,128]
	ldrb	w0, [x0,9]
	mul	w24, w24, w0
	bl	timer_get_time
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3131
	adrp	x0, .LC250
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC250
	bl	printk
.L3131:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	sub	w2, w24, #1
	mov	w21, 0
	mov	w23, w21
	adrp	x27, .LANCHOR3
	ldr	x0, [x0,600]
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC252
	add	x0, x0, :lo12:.LC252
	str	x0, [x29,120]
	adrp	x0, .LC251
	add	x0, x0, :lo12:.LC251
	str	x0, [x29,112]
.L3132:
	cmp	w23, w24
	beq	.L3164
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldr	x0, [x0,1424]
	ldr	w2, [x0,w23,sxtw 2]
	cmn	w2, #1
	beq	.L3133
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2848]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,140]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3134
	ldr	x0, [x29,112]
	mov	w3, w23
	ldr	w1, [x29,140]
	bl	printk
.L3134:
	mov	w25, w23
.L3140:
	add	x1, x27, :lo12:.LANCHOR3
	sbfiz	x26, x25, 2, 32
	ldr	x0, [x1,1424]
	ldr	w4, [x0,x26]
	cmn	w4, #1
	beq	.L3135
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,140]
	ldrb	w0, [x0,2848]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w2, w0, uxth
	bne	.L3135
	ldr	x0, [x1,1432]
	str	x4, [x29,104]
	ldr	w28, [x0,x26]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x4, [x29,104]
	bne	.L3136
	mov	w0, w4
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x4, [x29,104]
.L3136:
	ldr	w3, [x29,156]
	cmp	w3, w28
	bne	.L3137
	add	x0, x27, :lo12:.LANCHOR3
	add	x1, x29, 152
	mov	w2, 1
	add	w21, w21, 1
	ldr	x0, [x0,1440]
	ldr	w0, [x0,x26]
	str	w0, [x29,152]
	mov	w0, w4
	bl	pm_log2phys
	add	x3, x19, :lo12:.LANCHOR0
	mov	w2, 24
	ldrb	w0, [x3,744]
	ldrh	w1, [x3,3536]
	sub	w0, w2, w0
	mov	w2, 1
	sub	w0, w0, w1
	lsr	w28, w28, w1
	lsl	w0, w2, w0
	sub	w0, w0, #1
	and	w28, w0, w28
	ldrb	w0, [x3,3538]
	udiv	w0, w28, w0
	b	.L3163
.L3137:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3139
	ldr	x0, [x29,120]
	mov	w1, w4
	mov	w2, w28
	bl	printk
.L3139:
	ldr	x0, [x29,128]
	ldrh	w0, [x0]
.L3163:
	bl	ftl_vpn_decrement
	add	x0, x27, :lo12:.LANCHOR3
	mov	w1, -1
	ldr	x0, [x0,1424]
	str	w1, [x0,x26]
	bl	timer_get_time
.L3135:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L3140
.L3133:
	add	w23, w23, 1
	b	.L3132
.L3164:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3142
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,608]
	ldr	x2, [x0,600]
	adrp	x0, .LC253
	add	x0, x0, :lo12:.LC253
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w21
	bl	printk
.L3142:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w21
	beq	.L3143
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 749
	add	x1, x1, 624
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3143:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x19,600]
	strh	w21, [x0,x1,lsl 1]
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
	mov	w5, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2912
	stp	x21, x22, [sp,32]
	ldrb	w0, [x1,4]
	add	x3, x1, x0, sxtw 1
	ldrb	w6, [x2,3572]
	ldrh	w22, [x3,40]
	mov	w3, 0
	ldrh	w2, [x1,2]
	mov	w4, w3
.L3166:
	cmp	w22, w5
	bne	.L3196
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w6
	bne	.L3167
	add	w2, w2, 1
	mov	w0, 0
	mov	w4, 1
	uxth	w2, w2
.L3167:
	add	x3, x1, x0, sxtw 1
	ldrh	w22, [x3,40]
	mov	w3, 1
	b	.L3166
.L3196:
	cbz	w4, .L3169
	add	x1, x19, :lo12:.LANCHOR0
	strh	w2, [x1,2914]
.L3169:
	cbz	w3, .L3170
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,2916]
.L3170:
	mov	w0, 1
	mov	w23, 1
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x21, x0
.L3171:
	add	x20, x19, 2912
	ldrb	w1, [x20,6]
	cmp	w23, w1
	bgt	.L3197
	ldrh	w0, [x19,3634]
	cmp	w1, 2
	mul	w0, w22, w0
	beq	.L3172
	cmp	w1, 3
	bne	.L3173
	ldrb	w1, [x19,756]
	cbz	w1, .L3174
.L3172:
	ldrh	w1, [x19,2914]
	add	w0, w0, w1, lsl 1
	ldrb	w1, [x19,3553]
	sub	w0, w0, #1
	add	w0, w0, w23
	orr	w0, w0, w1, lsl 24
	b	.L3195
.L3174:
	ldrh	w1, [x20,2]
	add	w0, w0, w1
	orr	w0, w0, w23, lsl 24
	b	.L3195
.L3173:
	ldrh	w1, [x20,2]
	add	w0, w0, w1
.L3195:
	str	w0, [x21,40]
	mov	w1, 1
	mov	x0, x21
	bl	sblk_read_page
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L3178
	cmn	w0, #1
	beq	.L3178
	ldr	x0, [x21,24]
	ldr	w20, [x0,4]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L3179
	ldr	w0, [x19,2856]
	cmp	w20, w0
	bcs	.L3179
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L3179:
	ldr	w1, [x21,40]
	ldr	w0, [x29,76]
	cmp	w1, w0
	bne	.L3178
	ldrh	w3, [x19,2936]
	ldr	x2, [x19,3544]
	str	w1, [x2,x3,lsl 2]
	ldrh	w1, [x19,2936]
	add	w1, w1, 1
	strh	w1, [x19,2936]
.L3178:
	ldrh	w0, [x19,2938]
	add	w23, w23, 1
	add	w0, w0, 1
	strh	w0, [x19,2938]
	b	.L3171
.L3197:
	mov	x0, x21
	bl	zbuf_free
	ldrb	w0, [x20,4]
	ldrb	w1, [x19,3572]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x20,4]
	cmp	w1, w0
	bne	.L3165
	ldrh	w0, [x20,2]
	strb	wzr, [x20,4]
	add	w0, w0, 1
	strh	w0, [x20,2]
.L3165:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w1, [x0,2912]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3199
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1236
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3199:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,2912]
	cmp	w1, w3
	beq	.L3200
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3201
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,600]
	adrp	x0, .LC254
	add	x0, x0, :lo12:.LC254
	ldrh	w2, [x2,x3]
	bl	printk
.L3201:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2912
	add	x1, x20, 2952
	ldrh	w0, [x20,2912]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L3202
	mov	w0, -1
	strh	w0, [x20,2912]
	b	.L3232
.L3202:
	ldrh	w3, [x20,2912]
	ldr	x2, [x20,584]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L3203
	cmp	w4, 224
	beq	.L3203
	ldr	x2, [x20,608]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L3203
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L3203
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L3204
.L3203:
	cbnz	w4, .L3205
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L3206
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1256
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3206:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2912]
	ldr	x0, [x0,600]
	strh	wzr, [x0,x1,lsl 1]
.L3205:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2936]
	strh	w0, [x19,2912]
.L3232:
	mov	w0, 0
	b	.L3200
.L3204:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w20, [x0,40]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L3207
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1264
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3207:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w3, [x2,2912]
	ldr	x4, [x2,584]
	ldrh	w0, [x2,3596]
	add	x3, x4, x3, lsl 2
	ldrh	w1, [x2,3634]
	sub	w0, w0, #1
	ldrb	w3, [x3,2]
	uxth	w0, w0
	mul	w1, w20, w1
	and	w3, w3, 224
	cmp	w3, 160
	bne	.L3208
	ldrb	w3, [x2,3553]
	cmp	w3, 2
	uxth	w25, w3
	orr	w1, w1, w3, lsl 24
	bne	.L3209
	ldrh	w0, [x2,3570]
	sub	w0, w0, #1
	uxth	w0, w0
.L3209:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w3, [x2,756]
	cbz	w3, .L3208
	ldrh	w0, [x2,3570]
	sub	w0, w0, #1
	uxth	w0, w0
.L3208:
	add	x23, x19, :lo12:.LANCHOR0
	orr	w22, w0, w1
	mov	w0, 1
	strb	w25, [x23,2918]
	strh	wzr, [x23,2914]
	strb	wzr, [x23,2916]
	strh	wzr, [x23,2938]
	strh	wzr, [x23,2940]
	bl	buf_alloc
	mov	w1, 1
	str	w22, [x0,40]
	mov	x20, x0
	bl	sblk_read_page
	ldrh	w0, [x23,3570]
	mov	w1, 255
	ldrb	w2, [x23,3572]
	mul	w2, w0, w2
	ldr	x0, [x23,3544]
	lsl	w2, w2, 2
	bl	ftl_memset
	strh	wzr, [x23,2936]
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3233
	cmn	w0, #1
	beq	.L3233
	ldr	x0, [x20,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3212
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1299
	add	x1, x1, 648
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3233:
	mov	x0, x20
	bl	zbuf_free
	mov	w0, -1
	b	.L3200
.L3212:
	ldrb	w0, [x23,3572]
	mov	x26, 0
	ldrh	w21, [x23,3596]
	mov	w24, 1
	ldr	x22, [x20,8]
	mul	w21, w0, w21
	mul	w21, w25, w21
	mov	x25, x23
	mov	w23, 24
	uxth	w21, w21
.L3213:
	cmp	w21, w26
	ble	.L3234
	ldr	w0, [x22,x26,lsl 2]
	cmn	w0, #1
	beq	.L3215
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L3216
	ldr	w0, [x22,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L3216:
	ldrb	w1, [x25,744]
	ldrh	w0, [x25,3536]
	sub	w1, w23, w1
	ldr	w2, [x29,92]
	sub	w1, w1, w0
	ldrh	w3, [x25,2912]
	lsl	w1, w24, w1
	lsr	w0, w2, w0
	sub	w1, w1, #1
	and	w1, w1, w0
	ldrb	w0, [x25,3538]
	udiv	w1, w1, w0
	cmp	w1, w3
	bne	.L3215
	ldrh	w3, [x25,2936]
	ldr	x1, [x25,3544]
	str	w2, [x1,x3,lsl 2]
	ldrh	w1, [x25,2936]
	add	w1, w1, 1
	strh	w1, [x25,2936]
.L3215:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L3213
.L3234:
	mov	x0, x20
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2912]
	ldr	x3, [x0,600]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2936]
	cmp	w2, w3
	beq	.L3219
	adrp	x0, .LC255
	add	x0, x0, :lo12:.LC255
	bl	printk
.L3219:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2912]
	ldr	x1, [x19,600]
	ldrh	w3, [x19,2936]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2940]
	ldrh	w0, [x19,2936]
.L3200:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
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
	ldr	x0, [x3,608]
	ldr	w1, [x0,544]
	cmn	w1, #1
	beq	.L3236
	adrp	x21, .LANCHOR4
	adrp	x22, .LC0
	add	x21, x21, :lo12:.LANCHOR4
	mov	w20, 11
	add	x22, x22, :lo12:.LC0
	add	x21, x21, 664
.L3237:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,608]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L3238
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L3239
	ldrb	w2, [x24,744]
	mov	w3, 24
	ldrh	w0, [x24,3536]
	mov	w1, 1
	sub	w2, w3, w2
	ldr	w3, [x29,76]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	lsr	w3, w3, w0
	sub	w2, w2, #1
	ldrb	w0, [x24,3538]
	and	w2, w2, w3
	udiv	w0, w2, w0
	mov	w2, 0
	bl	gc_add_sblk
.L3239:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,608]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L3240
	mov	x1, x21
	mov	w2, 1738
	mov	x0, x22
	bl	printk
	bl	dump_stack
.L3240:
	mov	x0, x23
	bl	zbuf_free
.L3238:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,608]
	ldr	w2, [x2,2856]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L3241
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
	b	.L3235
.L3241:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L3235
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L3237
	b	.L3235
.L3236:
	ldr	w1, [x0,536]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L3246
	ldr	x3, [x3,2880]
	ldr	w1, [x0,540]
	ldr	w3, [x3,44]
	add	w1, w1, 4096
	add	w1, w1, 904
	cmp	w3, w1
	bls	.L3235
.L3246:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2880]
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w2, [x0,536]
	str	wzr, [x0,544]
.L3235:
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
	ldr	x1, [x20,608]
	ldrh	w0, [x20,576]
	ldr	w1, [x1,608]
	cmp	w1, w0
	bcs	.L3251
	bl	timer_get_time
	ldr	x21, [x20,608]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L3251
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,608]
	ldrh	w2, [x20,576]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L3255
	ldr	x2, [x20,2880]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L3256
.L3255:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2880]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L3256:
	ldr	w25, [x0,600]
	mov	w0, 65535
	uxth	w20, w25
	cmp	w20, w0
	bne	.L3257
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2035
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3257:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,608]
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
	cbz	w1, .L3251
	uxtw	x22, w20
	ldr	x24, [x21,584]
	add	x24, x24, x22, lsl 2
	ldrb	w1, [x24,2]
	tst	w1, 192
	and	w2, w1, 224
	beq	.L3258
	cmp	w2, 224
	beq	.L3258
	ldr	x1, [x21,608]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L3258
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L3258
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L3259
.L3258:
	cbnz	w2, .L3251
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x22]
	cbz	w0, .L3260
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2051
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3260:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,600]
	strh	wzr, [x0,x22]
	b	.L3251
.L3259:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L3262
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2057
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3262:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w3, [x24,2]
	mov	w26, 1
	and	w3, w3, 224
	ldrh	w0, [x2,3596]
	cmp	w3, 160
	ldrh	w1, [x2,3634]
	sub	w0, w0, #1
	mul	w1, w21, w1
	uxth	w0, w0
	bne	.L3263
	ldrb	w3, [x2,3553]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L3263
	ldrh	w0, [x2,3570]
	sub	w0, w0, #1
	uxth	w0, w0
.L3263:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x19, :lo12:.LANCHOR0
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,3570]
	mov	w1, 255
	ldrb	w2, [x21,3572]
	mul	w2, w0, w2
	ldr	x0, [x21,3544]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L3275
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L3264
.L3275:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L3288
.L3264:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3266
	mov	w0, w20
	mov	w1, 1
.L3288:
	bl	gc_add_sblk
	mov	x0, x23
	bl	zbuf_free
	b	.L3251
.L3266:
	ldrb	w0, [x21,3572]
	mov	x27, 0
	ldrh	w1, [x21,3596]
	and	w28, w25, 65535
	mov	w21, w27
	mul	w0, w0, w1
	mul	w26, w26, w0
	uxth	w0, w26
	ldr	x26, [x23,8]
	str	w0, [x29,108]
.L3267:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L3289
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L3268
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L3269
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L3269:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 24
	ldrb	w0, [x2,744]
	ldrh	w4, [x2,3536]
	sub	w0, w1, w0
	mov	w1, 1
	sub	w0, w0, w4
	lsl	w0, w1, w0
	ldr	w1, [x29,124]
	sub	w0, w0, #1
	lsr	w1, w1, w4
	and	w0, w0, w1
	ldrb	w1, [x2,3538]
	udiv	w0, w0, w1
	cmp	w0, w28
	bne	.L3268
	add	w21, w21, 1
	uxth	w21, w21
.L3268:
	add	x27, x27, 1
	b	.L3267
.L3289:
	mov	x0, x23
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3271
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w25
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,600]
	adrp	x0, .LC256
	add	x0, x0, :lo12:.LC256
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L3271:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,600]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L3251
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L3251:
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
	sub	sp, sp, #256
	uxth	x0, w0
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x23, x24, [sp,96]
	adrp	x23, .LANCHOR0
	str	x0, [x29,128]
	stp	x19, x20, [sp,64]
	add	x19, x23, :lo12:.LANCHOR0
	mov	x20, x0
	stp	x25, x26, [sp,112]
	stp	x21, x22, [sp,80]
	stp	x27, x28, [sp,128]
	lsl	x25, x0, 2
	str	x1, [x29,144]
	ldr	x0, [x19,584]
	add	x1, x0, x25
	ldrh	w4, [x0,x25]
	ldr	w5, [x0,x25]
	adrp	x0, .LC257
	ldrb	w3, [x1,2]
	add	x0, x0, :lo12:.LC257
	mov	w1, w20
	and	w4, w4, 2047
	ubfx	x2, x3, 5, 3
	ubfx	x5, x5, 11, 8
	ubfx	x3, x3, 3, 2
	bl	printk
	str	x23, [x29,152]
	mov	w1, 65535
	mov	w0, 0
	cmp	w20, w1
	beq	.L3291
	ldrh	w1, [x19,576]
	cmp	w1, w20
	bls	.L3291
	ldr	x0, [x19,584]
	mov	w26, 1
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L3292
	ldrb	w26, [x19,3553]
.L3292:
	add	x1, x29, 192
	mov	w0, w20
	strh	w20, [x29,176]
	mov	w22, 0
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	ldr	x0, [x29,152]
	strb	w1, [x29,185]
	mov	w3, w26
	add	x0, x0, :lo12:.LANCHOR0
	strh	wzr, [x29,178]
	strb	wzr, [x29,181]
	uxtb	w4, w4
	strh	wzr, [x29,186]
	mov	w27, w22
	ldrh	w2, [x0,3596]
	mov	w19, w22
	mul	w1, w1, w2
	strh	w1, [x29,182]
	ldr	x1, [x0,584]
	adrp	x0, .LC258
	add	x0, x0, :lo12:.LC258
	add	x1, x1, x25
	ldrb	w2, [x1,2]
	mov	w1, w20
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	adrp	x0, .LC201
	str	w22, [x29,140]
	add	x0, x0, :lo12:.LC201
	str	x0, [x29,112]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 712
	str	x0, [x29,104]
.L3293:
	ldr	x0, [x29,152]
	add	x21, x0, :lo12:.LANCHOR0
	ldrh	w0, [x21,3596]
	cmp	w0, w19
	bls	.L3310
	lsl	w0, w19, 1
	mov	w24, 0
	add	w1, w0, w19
	sub	w0, w0, #1
	sub	w1, w1, #1
	str	w0, [x29,124]
	str	w1, [x29,136]
.L3311:
	ldrb	w0, [x29,185]
	cmp	w0, w24
	bls	.L3308
	mov	w21, 1
.L3309:
	cmp	w21, w26
	bhi	.L3329
	add	x0, x29, 176
	add	x0, x0, x24, sxtw 1
	ldrh	w3, [x0,16]
	mov	w0, 65535
	cmp	w3, w0
	beq	.L3294
	ldr	x0, [x29,152]
	cmp	w26, 3
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,3634]
	mul	w2, w3, w2
	bne	.L3295
	ldrb	w1, [x0,756]
	cbz	w1, .L3296
	ldr	w1, [x29,136]
	b	.L3328
.L3296:
	add	w2, w2, w19
	orr	w23, w2, w21, lsl 24
	b	.L3297
.L3295:
	cmp	w26, 2
	add	w23, w2, w19
	bne	.L3297
	ldr	w1, [x29,124]
.L3328:
	ldrb	w9, [x0,3553]
	add	w2, w2, w1
	add	w2, w2, w21
	orr	w23, w2, w9, lsl 24
.L3297:
	mov	w1, 1
	str	w23, [x28,40]
	mov	x0, x28
	str	x3, [x29,96]
	bl	sblk_read_page
	ldr	x7, [x28,8]
	mov	w2, w19
	ldr	x0, [x28,24]
	ldr	w10, [x28,52]
	ldr	w1, [x7,12]
	str	w1, [sp]
	mov	w4, w10
	ldr	x3, [x29,96]
	ldr	w1, [x0]
	str	w1, [sp,8]
	str	x10, [x29,96]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w3
	mov	w3, w23
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	ldr	x0, [x29,112]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x10, [x29,96]
	cmp	w10, 512
	beq	.L3315
	cmn	w10, #1
	bne	.L3299
.L3315:
	mov	w0, 1
	str	w0, [x29,140]
.L3299:
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x0, [x0,584]
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L3294
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L3294
	ldr	x0, [x28,24]
	str	x2, [x29,96]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x2, [x29,96]
	bne	.L3301
	ldr	x0, [x28,24]
	add	x1, x29, 172
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L3301:
	ldr	w0, [x29,172]
	cmp	w0, w23
	bne	.L3302
	ldr	x2, [x28,24]
	add	w22, w22, 1
	adrp	x0, .LC259
	mov	w1, w23
	add	x0, x0, :lo12:.LC259
	mov	w3, w22
	ldr	w2, [x2,4]
	bl	printk
.L3302:
	ldr	x0, [x29,144]
	cbz	x0, .L3304
	ubfiz	x3, x27, 2, 32
	ldr	w2, [x0,x3]
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L3305
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3305
	adrp	x0, .LC260
	mov	w1, w27
	add	x0, x0, :lo12:.LC260
	str	x3, [x29,96]
	bl	printk
	ldr	x3, [x29,96]
.L3305:
	ldr	x0, [x29,144]
	ldr	x1, [x28,24]
	ldr	w0, [x0,x3]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3304
	cmn	w0, #1
	beq	.L3304
	adrp	x0, .LC0
	ldr	x1, [x29,104]
	mov	w2, 1393
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3304:
	add	w27, w27, 1
.L3294:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3309
.L3329:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L3311
.L3308:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L3293
.L3310:
	mov	x0, x28
	bl	zbuf_free
	ldr	x3, [x29,128]
	adrp	x0, .LC261
	ldr	x2, [x21,600]
	add	x0, x0, :lo12:.LC261
	mov	w1, w20
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
	ldr	w0, [x29,140]
.L3291:
	sub	sp, x29, #48
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 256
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
	mov	w21, w1
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	mov	w24, w2
	mov	x22, x3
	tbz	x0, 12, .L3331
	adrp	x0, .LC262
	mov	w1, w19
	add	x0, x0, :lo12:.LC262
	mov	w2, w21
	mov	w3, w24
	bl	printk
.L3331:
	cbnz	w19, .L3332
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3333
.L3332:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3334
	lsl	w19, w19, 13
	mov	w2, 8192
.L3333:
	add	w1, w21, w24
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3334
	adrp	x20, .LANCHOR0
	add	w21, w19, w21
	add	x1, x20, :lo12:.LANCHOR0
	add	w28, w21, w24
	str	wzr, [x29,152]
	ldr	x2, [x1,2880]
	ldr	w0, [x2,24]
	add	w0, w0, w24
	str	w0, [x2,24]
	ldrb	w0, [x1,2848]
	udiv	w1, w21, w0
	str	w1, [x29,156]
	sub	w1, w28, #1
	udiv	w0, w1, w0
	ldr	w1, [x29,156]
	str	w0, [x29,140]
	sub	w23, w0, w1
	mov	x19, x1
	add	w23, w23, 1
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,128]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 728
	str	x0, [x29,120]
.L3335:
	cbz	w23, .L3400
	ldr	w1, [x29,156]
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w19, w1
	ldr	w1, [x29,140]
	ldrb	w0, [x0,2848]
	cset	w2, eq
	cmp	w19, w1
	cset	w26, eq
	uxth	w25, w0
	cbnz	w26, .L3366
	cbz	w2, .L3336
	b	.L3399
.L3366:
	cbz	w2, .L3338
.L3399:
	udiv	w26, w21, w0
	msub	w26, w26, w0, w21
	uxth	w0, w24
	uxth	w26, w26
	sub	w25, w25, w26
	uxth	w25, w25
	cmp	w25, w24
	csel	w25, w0, w25, hi
	b	.L3336
.L3338:
	msub	w25, w0, w19, w28
	mov	w26, w2
	uxtb	w25, w25
.L3336:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 800
.L3341:
	ldr	w2, [x1,36]
	cmp	w2, w19
	bne	.L3339
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L3339
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x0, 6, 32
	add	x0, x2, x1
	ubfiz	x26, x26, 9, 16
	lsl	w2, w25, 9
	ubfiz	x25, x25, 9, 16
	ldr	x1, [x0,808]
	mov	x0, x22
	add	x22, x22, x25
	add	x1, x1, x26
	bl	ftl_memcpy
	b	.L3340
.L3339:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3341
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L3342
	mov	w0, w19
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L3342:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L3343
	mov	w25, 0
.L3344:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,2848]
	cmp	w25, w0
	bcs	.L3340
	madd	w0, w19, w0, w25
	cmp	w0, w21
	bcc	.L3345
	cmp	w0, w28
	bcs	.L3345
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	add	x22, x22, 512
	bl	ftl_memset
.L3345:
	add	w25, w25, 1
	b	.L3344
.L3343:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3347
	add	x2, x20, :lo12:.LANCHOR0
	ldr	x3, [x2,2880]
	ldr	w2, [x3,40]
	add	w2, w2, 1
	str	w2, [x3,40]
	ldr	w2, [x29,172]
	strb	w25, [x0,56]
	ubfiz	x25, x25, 9, 16
	str	x22, [x0,16]
	add	x22, x22, x25
	str	w2, [x0,40]
	str	w2, [x0,44]
	str	w19, [x0,36]
	strb	w26, [x0,57]
	bl	zftl_add_read_buf
.L3340:
	add	w19, w19, 1
	sub	w23, w23, #1
.L3347:
	cbz	w23, .L3367
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,2850]
	cmp	w0, 2
	bhi	.L3335
.L3367:
	adrp	x25, .LANCHOR3
	str	x25, [x29,144]
	add	x2, x25, :lo12:.LANCHOR3
	ldrb	w1, [x2,-172]
	cbz	w1, .L3335
	ldrb	w2, [x2,-173]
	add	x26, x20, :lo12:.LANCHOR0
	add	x0, x26, 800
	mov	x25, x26
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
.L3350:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w1, [x0,-172]
	cbz	w1, .L3401
	ldrb	w0, [x0,-173]
	cmp	w0, 255
	bne	.L3351
	ldr	x1, [x29,120]
	mov	w2, 1088
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3351:
	ldr	x0, [x29,144]
	add	x1, x0, :lo12:.LANCHOR3
	add	x0, x25, 800
	ldrb	w27, [x1,-173]
	lsl	x2, x27, 6
	add	x26, x0, x2
	ldrb	w0, [x0,x2]
	ldr	w5, [x26,52]
	strb	w0, [x1,-173]
	cmn	w5, #1
	bne	.L3352
	ldr	x1, [x25,608]
	str	w5, [x29,152]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L3353
.L3352:
	cmp	w5, 256
	bne	.L3353
	ldrb	w4, [x25,744]
	mov	w1, 24
	ldrh	w0, [x25,3536]
	mov	w6, 1
	sub	w4, w1, w4
	ldr	w3, [x26,40]
	sub	w4, w4, w0
	ldr	w2, [x26,36]
	lsl	w4, w6, w4
	lsr	w0, w3, w0
	sub	w4, w4, #1
	str	x5, [x29,96]
	and	w0, w4, w0
	ldrb	w4, [x25,3538]
	str	x6, [x29,104]
	udiv	w4, w0, w4
	adrp	x0, .LC263
	add	x0, x0, :lo12:.LC263
	str	x4, [x29,112]
	uxth	w1, w4
	bl	printk
	ldr	x4, [x29,112]
	mov	w2, 0
	ldr	x6, [x29,104]
	mov	w0, w4
	mov	w1, w6
	bl	gc_add_sblk
	ldr	x5, [x29,96]
.L3353:
	add	x0, x25, 800
	cmn	w5, #1
	add	x0, x0, x27, lsl 6
	cset	w11, eq
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3368
	cbz	w11, .L3354
.L3368:
	ldr	x1, [x25,608]
	add	x10, x25, 800
	add	x10, x10, x27, lsl 6
	mov	w2, w5
	add	x10, x10, 32
	str	x11, [x29,104]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	ldr	x7, [x10,-8]
	ldrb	w1, [x10,-31]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC264
	add	x0, x0, :lo12:.LC264
	ldr	w3, [x10,4]
	ldr	w4, [x10,8]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	str	x10, [x29,112]
	bl	printk
	ldrb	w2, [x25,744]
	mov	w1, 24
	ldrh	w0, [x25,3536]
	ldr	x10, [x29,112]
	sub	w2, w1, w2
	sub	w2, w2, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x10,8]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x25,3538]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x11, [x29,104]
.L3354:
	add	x0, x25, 800
	add	x0, x0, x27, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3369
	cbz	w11, .L3356
.L3369:
	ldr	x1, [x29,120]
	mov	w2, 1109
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3356:
	add	x0, x25, 800
	ldrb	w1, [x25,2848]
	add	x9, x0, x27, lsl 6
	ldrb	w2, [x9,56]
	cmp	w1, w2
	bls	.L3358
	ldrb	w3, [x9,57]
	lsl	w2, w2, 9
	ldr	x1, [x9,8]
	ldr	x0, [x9,16]
	add	x1, x1, x3, lsl 9
	bl	ftl_memcpy
	b	.L3359
.L3358:
	ldrb	w0, [x9,2]
	and	w0, w0, -9
	strb	w0, [x9,2]
.L3359:
	ldr	x0, [x29,144]
	mov	x1, x26
	add	x2, x0, :lo12:.LANCHOR3
	str	x2, [x29,112]
	sub	x0, x2, #171
	bl	buf_remove_buf
	mov	x0, x26
	bl	zbuf_free
	ldr	x2, [x29,112]
	ldrb	w0, [x2,-172]
	sub	w0, w0, #1
	strb	w0, [x2,-172]
	b	.L3350
.L3401:
	mov	w2, -1
	strb	w1, [x0,-172]
	strb	w2, [x0,-173]
	b	.L3335
.L3400:
	bl	timer_get_time
	adrp	x1, .LANCHOR6+256
	str	w0, [x1,#:lo12:.LANCHOR6+256]
	ldr	w0, [x29,152]
.L3334:
	sub	sp, x29, #16
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
	mov	x22, x2
	bhi	.L3405
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x21, x0
	mov	x0, -14
	cbnz	x21, .L3405
	adrp	x0, .LC265
	mov	x1, x19
	add	x0, x0, :lo12:.LC265
	strb	w21, [x19,x22]
	bl	printk
	adrp	x0, .LC266
	mov	x1, x19
	add	x0, x0, :lo12:.LC266
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC267
	mov	x0, x19
	add	x1, x1, :lo12:.LC267
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3406
	adrp	x23, .LANCHOR0
	adrp	x0, .LC268
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC268
	mov	w2, 4
	adrp	x20, .LANCHOR3
	ldr	x3, [x19,2880]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR3
	ldr	x1, [x19,2880]
	adrp	x0, .LC269
	mov	w2, 2
	add	x0, x0, :lo12:.LC269
	add	x1, x1, 416
	ldrh	w3, [x3,1456]
	mov	w19, w21
	mov	w21, 65535
	bl	rknand_print_hex
.L3407:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1456]
	cmp	w19, w0
	bge	.L3410
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w21
	beq	.L3408
	mov	x1, 0
	bl	ftl_sblk_dump
.L3408:
	add	w19, w19, 1
	b	.L3407
.L3406:
	adrp	x1, .LC270
	mov	x0, x19
	add	x1, x1, :lo12:.LC270
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3411
	adrp	x23, .LANCHOR0
	adrp	x0, .LC268
	add	x20, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC268
	mov	w2, 4
	adrp	x21, .LANCHOR3
	add	x21, x21, :lo12:.LANCHOR3
	adrp	x25, .LC273
	ldr	x3, [x20,2880]
	add	x25, x25, :lo12:.LC273
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x20,2880]
	adrp	x0, .LC269
	ldrh	w3, [x21,1456]
	mov	w2, 2
	add	x0, x0, :lo12:.LC269
	add	x1, x1, 416
	add	x20, x29, 192
	bl	rknand_print_hex
	adrp	x0, .LC271
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC271
	str	x1, [x20,-88]!
	mov	x19, x23
	adrp	x23, .LC272
	bl	printk
	add	x23, x23, :lo12:.LC272
	ldr	x0, [x29,104]
	mov	x1, x20
	sub	x20, x21, #160
	add	x21, x21, 352
	bl	rk_simple_strtoull.constprop.33
	mov	w24, w0
	str	w0, [x29,100]
	uxth	w26, w0
.L3413:
	ldrh	w1, [x20]
	mov	x0, x23
	ldrh	w2, [x20,2]
	bl	printk
	ldrh	w0, [x20]
	cmp	w0, w26
	bne	.L3412
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x20,8]
	mov	w2, 4
	ldrb	w3, [x0,2848]
	mov	x0, x25
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L3412:
	add	x20, x20, 16
	cmp	x20, x21
	bne	.L3413
	mov	w0, 300
	add	x19, x19, :lo12:.LANCHOR0
	bl	msleep
	mov	w0, 1
	bl	buf_alloc
	mov	x20, x0
	ldr	x1, [x19,2880]
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
	adrp	x0, .LC235
	add	x0, x0, :lo12:.LC235
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x20,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x19,2848]
	adrp	x0, .LC274
	ldr	x1, [x20,8]
	add	x0, x0, :lo12:.LC274
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	mov	x0, x20
	b	.L3428
.L3411:
	adrp	x1, .LC275
	mov	x0, x19
	add	x1, x1, :lo12:.LC275
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3414
	bl	dump_ftl_info
	b	.L3410
.L3414:
	adrp	x1, .LC276
	mov	x0, x19
	add	x1, x1, :lo12:.LC276
	mov	x2, 9
	bl	memcmp
	cbnz	w0, .L3415
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1,-88]!
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29,100]
	adrp	x1, .LANCHOR0+3632
	strh	w0, [x1,#:lo12:.LANCHOR0+3632]
	bl	dump_all_list_info
	b	.L3410
.L3415:
	adrp	x1, .LC277
	mov	x0, x19
	add	x1, x1, :lo12:.LC277
	mov	x2, 8
	bl	memcmp
	cbz	w0, .L3410
	adrp	x1, .LC278
	mov	x0, x19
	add	x1, x1, :lo12:.LC278
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3417
	add	x20, x29, 192
	adrp	x0, .LC271
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC271
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	bl	rk_simple_strtoull.constprop.33
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
	adrp	x0, .LC235
	add	x0, x0, :lo12:.LC235
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w3, [x6]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	ldr	w2, [x19,52]
	bl	printk
	ldrb	w3, [x20,2848]
	adrp	x0, .LC229
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC229
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,2848]
	adrp	x0, .LC230
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC230
	mov	w2, 4
	lsl	w3, w3, 1
	bl	rknand_print_hex
	mov	x0, x19
.L3428:
	bl	zbuf_free
	b	.L3410
.L3417:
	adrp	x1, .LC279
	mov	x0, x19
	add	x1, x1, :lo12:.LC279
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3418
	add	x20, x29, 192
	adrp	x0, .LC271
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC271
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29,100]
	mov	x1, x21
	bl	ftl_sblk_dump
	b	.L3410
.L3418:
	adrp	x1, .LC280
	mov	x0, x19
	add	x1, x1, :lo12:.LC280
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3419
	add	x20, x29, 192
	adrp	x0, .LC271
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC271
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3410
.L3419:
	adrp	x1, .LC281
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC281
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3420
	add	x1, x19, 8
	adrp	x0, .LC271
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC271
	str	x1, [x19,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.33
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29,100]
	cmn	w0, #1
	ldr	x2, [x29,88]
	bne	.L3421
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3421:
	adrp	x0, .LC282
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC282
	bl	printk
	b	.L3410
.L3420:
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
	adrp	x0, .LC288
	add	x0, x0, :lo12:.LC288
	bl	printk
	adrp	x0, .LC289
	add	x0, x0, :lo12:.LC289
	bl	printk
	adrp	x0, .LC290
	add	x0, x0, :lo12:.LC290
	bl	printk
	adrp	x0, .LC291
	add	x0, x0, :lo12:.LC291
	bl	printk
.L3410:
	bl	rknand_device_unlock
	mov	x0, x22
.L3405:
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
	.global	gc_check_data_one_wl
	.type	gc_check_data_one_wl, %function
gc_check_data_one_wl:
	sub	sp, sp, #128
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x20,2920]
	ldr	x23, [x20,608]
	add	x20, x20, 2912
	cbnz	x0, .L3430
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L3430:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,2920]
	cbnz	x20, .L3431
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 613
	add	x1, x1, 744
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3431:
	mov	w24, 0
	add	x25, x23, 80
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x27, .LANCHOR3
.L3432:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L3459
	mov	w22, 1
	sxtw	x28, w24
.L3442:
	add	x2, x21, 2912
	ldrh	w0, [x2,20]
	cmp	w22, w0
	bgt	.L3460
	add	x0, x28, 8
	ldrh	w1, [x21,3634]
	ldrh	w0, [x25,x0,lsl 1]
	mul	w0, w0, w1
	ldrb	w1, [x21,3553]
	cmp	w1, 2
	beq	.L3433
	ldrb	w3, [x21,756]
	cbz	w3, .L3434
.L3433:
	ldrh	w2, [x21,2928]
	sub	w2, w2, #1
	add	w0, w2, w0
	add	w0, w0, w22
	orr	w0, w0, w1, lsl 24
	b	.L3436
.L3434:
	cmp	w1, 3
	ldrh	w1, [x2,16]
	add	w0, w0, w1
	bne	.L3436
	orr	w0, w0, w22, lsl 24
.L3436:
	str	w0, [x20,40]
	mov	w1, 1
	mov	x0, x20
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmn	w0, #1
	beq	.L3437
	add	x2, x27, :lo12:.LANCHOR3
	ldrh	w0, [x21,2934]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x3, [x2,1424]
	ldr	w4, [x3,x0]
	ldr	w3, [x1,4]
	cmp	w4, w3
	bne	.L3437
	ldr	x2, [x2,1432]
	ldr	w2, [x2,x0]
	ldr	w0, [x1,8]
	cmp	w2, w0
	beq	.L3438
.L3437:
	add	x26, x27, :lo12:.LANCHOR3
	ldrh	w0, [x21,2934]
	add	x4, x21, 2912
	ldr	x1, [x26,1424]
	ldr	w0, [x1,x0,lsl 2]
	cmn	w0, #1
	beq	.L3438
	ldrb	w5, [x25,9]
	adrp	x0, .LC292
	ldrh	w3, [x21,3570]
	add	x0, x0, :lo12:.LC292
	mov	w2, 4
	str	x4, [x29,104]
	mul	w3, w5, w3
	bl	rknand_print_hex
	ldrb	w5, [x25,9]
	adrp	x0, .LC293
	ldrh	w3, [x21,3570]
	add	x0, x0, :lo12:.LC293
	ldr	x1, [x26,1432]
	mov	w2, 4
	mul	w3, w5, w3
	bl	rknand_print_hex
	adrp	x0, .LANCHOR2
	ldr	x4, [x29,104]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L3439
	ldr	x7, [x20,24]
	ldrh	w4, [x4,22]
	ldr	x5, [x26,1432]
	ldr	w0, [x7,12]
	lsl	x4, x4, 2
	ldr	x3, [x26,1424]
	str	w0, [sp]
	adrp	x0, .LC294
	add	x0, x0, :lo12:.LC294
	ldr	w3, [x3,x4]
	ldr	w6, [x7,4]
	ldr	w4, [x5,x4]
	ldr	w1, [x20,40]
	ldr	w5, [x7]
	ldr	w2, [x20,52]
	ldr	w7, [x7,8]
	bl	printk
.L3439:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x1, 0
	ldr	x0, [x19,608]
	ldrh	w0, [x0,80]
	bl	ftl_sblk_dump
	ldrh	w1, [x23,80]
	ldr	x0, [x19,600]
	strh	wzr, [x0,x1,lsl 1]
	ldr	x1, [x19,608]
	ldr	w0, [x1,556]
	add	w0, w0, 1
	str	w0, [x1,556]
	ldr	x0, [x19,2880]
	mov	w19, -1
	ldr	w1, [x0,156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	bne	.L3440
	ldr	w0, [x20,40]
	bl	ftl_mask_bad_block
	b	.L3440
.L3438:
	ldrh	w0, [x21,2934]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2934]
	b	.L3442
.L3460:
	add	w24, w24, 1
	b	.L3432
.L3459:
	add	x19, x19, :lo12:.LANCHOR0
	add	x2, x19, 2912
	ldrh	w1, [x19,2928]
	add	w0, w1, 1
	strh	w0, [x19,2928]
	ldrb	w0, [x19,3554]
	mov	w19, 0
	cbz	w0, .L3440
	add	w1, w1, 2
	strh	w1, [x2,16]
.L3440:
	sub	sp, x29, #16
	mov	w0, w19
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 128
	ret
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	ftl_update_l2p_map
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp,48]
	mov	x23, x0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrh	w20, [x23,12]
	ldrh	w24, [x0,3596]
	ldrb	w0, [x23,9]
	mul	w24, w24, w0
	adrp	x0, .LANCHOR3
	str	x0, [x29,120]
	add	x1, x0, :lo12:.LANCHOR3
	ldr	x1, [x1,-200]
	add	x20, x1, x20, lsl 2
	add	x1, x20, x24, sxtw 2
	ldr	w1, [x1,-4]
	cmn	w1, #1
	beq	.L3462
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1743
	add	x1, x1, 768
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3462:
	adrp	x0, .LC296
	mov	x28, 0
	add	x0, x0, :lo12:.LC296
	adrp	x27, .LC295
	str	x0, [x29,112]
	adrp	x0, .LC297
	add	x0, x0, :lo12:.LC297
	mov	w22, w28
	str	x0, [x29,104]
	add	x27, x27, :lo12:.LC295
.L3463:
	cmp	w28, w24
	mov	w25, w28
	bge	.L3490
	ldr	w2, [x20,x28,lsl 2]
	cmn	w2, #1
	beq	.L3464
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2848]
	adrp	x0, .LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L3468
	mov	x0, x27
	mov	w1, w21
	mov	w3, w28
	bl	printk
.L3468:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L3466
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w1, [x2,2848]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L3466
	ldrb	w0, [x23,9]
	ldrh	w2, [x2,3634]
	sdiv	w1, w25, w0
	msub	w0, w1, w0, w25
	add	x0, x23, x0, sxtw 1
	ldrh	w0, [x0,16]
	madd	w2, w0, w2, w1
	str	w2, [x29,140]
	ldr	x0, [x29,112]
	ldr	w1, [x20,x26]
	bl	pm_ppa_update_check
	cbz	w0, .L3467
	ldr	x0, [x29,120]
	mov	w2, 4
	mov	w3, w24
	add	x1, x0, :lo12:.LANCHOR3
	ldr	x0, [x29,104]
	ldr	x1, [x1,-200]
	bl	rknand_print_hex
.L3467:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	mov	w0, -1
	str	w0, [x20,x26]
.L3466:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L3468
.L3464:
	add	x28, x28, 1
	b	.L3463
.L3490:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3470
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,600]
	adrp	x0, .LC298
	add	x0, x0, :lo12:.LC298
	ldrh	w3, [x4,x3]
	bl	printk
.L3470:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	ldr	x0, [x19,600]
	strh	w22, [x0,x1,lsl 1]
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
	beq	.L3492
	bl	zftl_insert_data_list
.L3492:
	adrp	x19, .LANCHOR0
	mov	w2, 2
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 3
	ldr	x0, [x19,608]
	add	x0, x0, 16
	cmp	x20, x0
	mov	x0, x20
	csel	w1, w2, w1, eq
	bl	ftl_open_sblk_init
	ldr	x0, [x19,608]
	ldr	w0, [x0,560]
	cmp	w0, w21
	bne	.L3494
	mov	w20, 65535
	cmp	w21, w20
	beq	.L3494
	adrp	x0, .LC299
	mov	w1, w21
	add	x0, x0, :lo12:.LC299
	bl	printk
	ldr	x0, [x19,608]
	ldr	w0, [x0,564]
	bl	gc_mark_bad_ppa
	ldr	x0, [x19,608]
	mov	w1, -1
	str	w20, [x0,560]
	str	w1, [x0,564]
.L3494:
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
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	stp	x25, x26, [sp,64]
	add	x24, x24, :lo12:.LANCHOR0
	adrp	x25, .LANCHOR3
	add	x0, x24, 800
	add	x25, x25, :lo12:.LANCHOR3
	str	x0, [x29,112]
	sub	x0, x25, #171
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	str	x0, [x29,104]
.L3503:
	ldrb	w0, [x24,2872]
	adrp	x22, .LANCHOR0
	cbz	w0, .L3505
	ldrb	w1, [x24,2904]
	sub	w0, w0, #1
	ldr	x2, [x29,112]
	str	x1, [x29,120]
	lsl	x1, x1, 6
	add	x27, x2, x1
	strb	w0, [x24,2872]
	ldr	w0, [x24,2856]
	ldrb	w1, [x2,x1]
	strb	w1, [x24,2904]
	ldr	w1, [x27,36]
	cmp	w1, w0
	bcc	.L3506
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 510
	add	x1, x1, 792
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3506:
	ldr	x0, [x29,120]
	add	x1, x22, :lo12:.LANCHOR0
	add	x4, x1, 800
	add	x0, x4, x0, lsl 6
	ldr	w2, [x1,2856]
	ldr	w23, [x0,36]
	cmp	w23, w2
	bcc	.L3507
	mov	x0, x27
	bl	zbuf_free
	mov	w0, -1
	b	.L3561
.L3507:
	ldr	x2, [x0,24]
	ldr	x26, [x0,8]
	ldrb	w21, [x0,57]
	ldrb	w20, [x0,56]
	ldrb	w0, [x1,2873]
	str	x2, [x29,128]
	cbz	w0, .L3509
	ldrb	w3, [x25,1544]
	add	x3, x4, x3, lsl 6
.L3510:
	ldrb	w5, [x3]
	cmp	w5, 255
	beq	.L3563
	sbfiz	x3, x5, 6, 32
	add	x3, x4, x3
	b	.L3510
.L3563:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L3509
	ldr	x0, [x3,8]
	ubfiz	x1, x21, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x26, x1
	bl	ftl_memcpy
	mov	x0, x27
	bl	zbuf_free
	b	.L3503
.L3509:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3513
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3513:
	add	x4, x22, :lo12:.LANCHOR0
	mov	w3, 0
	add	x4, x4, 800
	ldr	x19, [x4,-192]
	add	x19, x19, 16
.L3516:
	ldr	w0, [x4,36]
	cmp	w0, w23
	bne	.L3514
	ldrb	w5, [x4,2]
	tbz	x5, 3, .L3514
	add	x0, x22, :lo12:.LANCHOR0
	ubfiz	x3, x3, 6, 32
	add	x0, x0, 800
	and	w5, w5, -9
	add	x3, x0, x3
	ldr	w0, [x3,40]
	strb	w5, [x3,2]
	ldr	x28, [x3,8]
	str	w0, [x29,156]
	b	.L3515
.L3514:
	add	w3, w3, 1
	add	x4, x4, 64
	cmp	w3, 32
	bne	.L3516
	mov	x28, 0
.L3515:
	add	x3, x22, :lo12:.LANCHOR0
	str	wzr, [x29,136]
	ldrb	w0, [x3,2848]
	cmp	w20, w0
	bcs	.L3517
	add	w20, w21, w20
	cbz	x28, .L3518
	cbz	w21, .L3519
	mov	x0, x26
	mov	x1, x28
	lsl	w2, w21, 9
	str	x3, [x29,136]
	bl	ftl_memcpy
	ldr	x3, [x29,136]
	ldr	x19, [x3,608]
	add	x19, x19, 48
.L3519:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w2, [x0,2848]
	cmp	w20, w2
	bcc	.L3520
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3562
.L3520:
	ubfiz	x1, x20, 9, 9
	sub	w2, w2, w20
	add	x0, x26, x1
	lsl	w2, w2, 9
	add	x1, x28, x1
	bl	ftl_memcpy
.L3562:
	str	wzr, [x29,136]
	b	.L3517
.L3518:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L3521
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	ldr	w1, [x29,156]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x0, [x28,24]
	ldr	w1, [x0,12]
	ldr	w0, [x0,4]
	add	w1, w1, 1
	str	w1, [x29,136]
	cmp	w0, w23
	bne	.L3522
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L3523
.L3522:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	x1, [x0,608]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	adrp	x0, .LC300
	ldrb	w1, [x28,1]
	add	x0, x0, :lo12:.LC300
	ldr	w4, [x28,52]
	bl	printk
	adrp	x0, .LC230
	mov	w2, 4
	ldr	x1, [x28,24]
	add	x0, x0, :lo12:.LC230
	mov	w3, w2
	bl	rknand_print_hex
.L3523:
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3524
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L3525
.L3524:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 581
	add	x1, x1, 792
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3525:
	cbz	w21, .L3526
	ldr	w0, [x29,156]
	lsl	w2, w21, 9
	cmn	w0, #1
	beq	.L3527
	ldr	x1, [x28,8]
	mov	x0, x26
	bl	ftl_memcpy
	b	.L3528
.L3521:
	cbz	w21, .L3526
	lsl	w2, w21, 9
.L3527:
	mov	x0, x26
	mov	w1, 0
	bl	ftl_memset
.L3528:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x19, [x0,608]
	add	x19, x19, 48
.L3526:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w2, [x0,2848]
	cmp	w20, w2
	bcc	.L3529
	bls	.L3530
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3530
.L3529:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L3531
	ldr	x1, [x28,8]
	lsl	x20, x0, 2
	add	x0, x26, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L3530
.L3531:
	add	x0, x26, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3530:
	cbz	x28, .L3517
	ldrb	w0, [x28,2]
	mov	x1, x28
	and	w0, w0, -9
	strb	w0, [x28,2]
	ldr	x0, [x29,104]
	bl	buf_remove_buf
	mov	x0, x28
	bl	zbuf_free
.L3517:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3532
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3532:
	mov	x0, x19
	add	x20, x22, :lo12:.LANCHOR0
	bl	ftl_get_new_free_page
	ldr	x1, [x29,120]
	add	x3, x20, 800
	ldr	x6, [x29,128]
	lsl	x2, x1, 6
	ldr	w7, [x29,136]
	add	x1, x3, x2
	ldr	w5, [x1,32]
	str	w5, [x6]
	str	w23, [x6,4]
	ldr	w5, [x29,156]
	str	w5, [x6,8]
	str	w7, [x6,12]
	str	w0, [x1,40]
	mov	w0, -1
	strb	w0, [x3,x2]
	mov	w0, 10
	ldrb	w2, [x1,2]
	str	w5, [x1,44]
	orr	w0, w2, w0
	strb	w0, [x1,2]
	ldrh	w2, [x19,10]
	ldrh	w0, [x19,12]
	add	w0, w2, w0
	sub	w0, w0, #1
	strh	w0, [x1,48]
	add	x0, x25, 1544
	mov	x1, x27
	bl	buf_add_tail
	ldrb	w0, [x20,2873]
	add	w0, w0, 1
	strb	w0, [x20,2873]
	bl	timer_get_time
	str	w0, [x25,1548]
	ldrb	w2, [x20,2873]
	cmp	w2, 2
	bhi	.L3533
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3505
.L3533:
	ldrb	w0, [x19,5]
	mov	w4, 2
	ldrb	w5, [x25,1544]
	mov	w1, 0
	cmp	w0, wzr
	ldrh	w0, [x19,6]
	csinc	w4, w4, wzr, ne
	mov	w3, w5
	cmp	w0, 1
	add	x0, x22, :lo12:.LANCHOR0
	csel	w4, w4, w2, ne
	add	x0, x0, 800
.L3537:
	cmp	w1, w4
	beq	.L3564
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x0,x3]
	b	.L3537
.L3564:
	add	x0, x22, :lo12:.LANCHOR0
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	add	x0, x0, 800
	strb	w3, [x25,1544]
	strb	w2, [x0,2073]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3505
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3505:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w19, [x22,2872]
	cbnz	w19, .L3503
	bl	ftl_write_completed
	mov	w0, w19
.L3561:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	sub	sp, sp, #208
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp,48]
	add	x21, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldrb	w0, [x21,3569]
	ldr	x22, [x21,608]
	cbnz	w0, .L3566
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3565
	ldrh	w22, [x21,2938]
	mov	w0, w22
	add	w22, w22, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w23, w0
	mov	w1, 1
	mov	x0, x20
	strh	w22, [x21,2938]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3643
	cmn	w0, #1
	bne	.L3568
.L3643:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 864
	add	x1, x1, 816
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3568:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,144]
	cmn	w0, #1
	bne	.L3570
	mov	w0, w21
	add	x1, x29, 144
	mov	w2, 0
	bl	pm_log2phys
.L3570:
	ldr	w22, [x29,144]
	cmp	w22, w23
	bne	.L3571
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 800
	add	x1, x1, 2848
.L3574:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L3572
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L3572
	mov	x0, x20
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3565
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC301
	mov	w1, w21
	mov	w2, w22
	add	x0, x0, :lo12:.LC301
	ldrh	w3, [x19,2938]
	bl	printk
	b	.L3565
.L3572:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3574
	add	x23, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20,2]
	str	w21, [x20,36]
	strb	wzr, [x20,57]
	ldrb	w0, [x23,2848]
	strb	w0, [x20,56]
	mov	w0, 10
	orr	w0, w1, w0
	strb	w0, [x20,2]
	ldr	x0, [x20,24]
	ldr	w0, [x0]
	str	w0, [x20,32]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3575
	ldr	w2, [x29,144]
	mov	w0, w21
	str	x2, [x29,136]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,136]
	adrp	x0, .LC302
	ldrh	w5, [x23,2938]
	add	x0, x0, :lo12:.LC302
	mov	w1, w21
	mov	w4, w22
	bl	printk
.L3575:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,2880]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,2940]
	add	w0, w0, 1
	strh	w0, [x19,2940]
	b	.L3565
.L3571:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3577
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2938]
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	mov	w1, w21
	mov	w2, w22
	mov	w4, w23
	bl	printk
.L3577:
	mov	x0, x20
	bl	zbuf_free
	b	.L3565
.L3566:
	ldrb	w0, [x21,3553]
	add	x1, x21, 2912
	add	x22, x22, 80
	cmp	w0, 3
	bne	.L3578
	ldrb	w2, [x21,756]
	cbz	w2, .L3579
	ldrh	w26, [x1,570]
	adrp	x1, .LANCHOR6
	ldrb	w20, [x22,9]
	add	x1, x1, :lo12:.LANCHOR6
	sdiv	w2, w26, w20
	ldrb	w3, [x1,221]
	uxth	w27, w2
	cbz	w3, .L3580
	adrp	x0, .LANCHOR2
	mov	w21, 2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2008
	ldrh	w0, [x0,w2,sxtw 1]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L3581
.L3580:
	ldrb	w1, [x1,222]
	mov	w21, 1
	cbz	w1, .L3581
	sub	w1, w27, #62
	mov	w21, 2
	uxth	w1, w1
	cmp	w1, 2159
	bhi	.L3581
	udiv	w0, w27, w0
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	uxth	w0, w0
	cmp	w0, wzr
	csinc	w21, w21, wzr, eq
.L3581:
	msub	w20, w27, w20, w26
	uxth	w0, w20
	str	w0, [x29,136]
	b	.L3584
.L3579:
	ldrb	w2, [x21,3568]
	cbz	w2, .L3583
	ldrb	w20, [x22,9]
	mov	w21, w0
	ldrh	w26, [x1,570]
	add	w8, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w27, w26, w8
	madd	w20, w27, w20, w26
	uxth	w1, w20
	str	w1, [x29,136]
	b	.L3584
.L3583:
	ldrh	w20, [x1,570]
	adrp	x1, .LANCHOR2
	ldrb	w9, [x22,9]
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 3544
	mov	w21, w0
	sdiv	w2, w20, w9
	msub	w20, w2, w9, w20
	ldrh	w23, [x1,w2,sxtw 1]
	uxth	w1, w20
	lsr	w27, w23, 3
	and	w23, w23, 7
	str	w1, [x29,136]
	madd	w9, w27, w9, w1
	cmp	w23, 1
	add	w9, w9, w9, lsl 1
	uxth	w26, w9
	bne	.L3585
	b	.L3584
.L3578:
	ldrh	w26, [x1,570]
	ldrb	w20, [x22,9]
	ldrb	w0, [x21,3554]
	sdiv	w27, w26, w20
	msub	w20, w27, w20, w26
	cbnz	w0, .L3586
	uxth	w0, w20
	mov	w21, 1
	str	w0, [x29,136]
	b	.L3584
.L3586:
	uxth	w0, w20
	mov	w21, 2
	str	w0, [x29,136]
.L3584:
	adrp	x0, .LANCHOR2
	mov	w23, 0
	adrp	x28, .LANCHOR6
	add	x20, x0, :lo12:.LANCHOR2
	sxtw	x2, w27
	adrp	x4, .LANCHOR3
.L3587:
	ldrb	w1, [x22,9]
	mul	w0, w21, w1
	cmp	w23, w0
	bge	.L3639
	add	x0, x19, :lo12:.LANCHOR0
	add	w24, w26, w23
	ldrh	w0, [x0,3570]
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w24, w0
	bne	.L3588
	add	x0, x28, :lo12:.LANCHOR6
	ldrb	w0, [x0,221]
	cbz	w0, .L3639
	add	x0, x20, 2008
	ldrh	w0, [x0,x2,lsl 1]
	cmp	w0, w27
	bcs	.L3639
	tbz	x23, 0, .L3639
.L3588:
	add	x0, x4, :lo12:.LANCHOR3
	sbfiz	x24, x24, 2, 32
	ldr	x3, [x0,1432]
	ldr	w0, [x3,x24]
	cmn	w0, #1
	bne	.L3589
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2912
	ldrh	w25, [x0,26]
	ldrh	w0, [x0,24]
	cmp	w0, w25
	bls	.L3565
	add	x0, x28, :lo12:.LANCHOR6
	ldrb	w0, [x0,221]
	cbz	w0, .L3590
	add	x0, x20, 2008
	ldrh	w0, [x0,x2,lsl 1]
	cmp	w0, w27
	bcs	.L3590
	tbz	x23, 0, .L3590
	ubfiz	x0, x0, 2, 16
	ldr	w0, [x3,x0]
	str	w0, [x3,x24]
	b	.L3589
.L3590:
	mov	w0, w25
	str	x4, [x29,112]
	str	x2, [x29,120]
	add	w25, w25, 1
	str	x3, [x29,128]
	bl	gc_get_src_ppa_from_index
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	ldr	x4, [x29,112]
	ldr	x2, [x29,120]
	strh	w25, [x1,2938]
	str	w0, [x3,x24]
.L3589:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L3587
.L3639:
	mov	w23, 1
.L3585:
	adrp	x24, .LANCHOR2
	str	x24, [x29,120]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3592
	adrp	x1, .LANCHOR3+1432
	add	x0, x0, 2008
	ubfiz	x5, x26, 2, 16
	ldr	w3, [x29,136]
	mov	w2, w23
	ldr	x6, [x1,#:lo12:.LANCHOR3+1432]
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,w27,sxtw 1]
	add	x7, x6, x5
	ldrh	w4, [x1,3482]
	mov	w1, w27
	str	w0, [sp]
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	ldr	w5, [x6,x5]
	ldr	w6, [x7,4]
	mov	w7, w26
	bl	printk
.L3592:
	adrp	x0, .LC0
	mov	w24, 0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,112]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 816
	str	x0, [x29,104]
.L3593:
	ldrb	w1, [x22,9]
	mul	w0, w21, w1
	cmp	w24, w0
	bge	.L3714
	add	w25, w26, w24
	sxtw	x0, w25
	str	x0, [x29,128]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x29,128]
	ldr	x0, [x0,3560]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	bne	.L3594
	mov	w0, 0
	bl	buf_alloc
	mov	x20, x0
	cbnz	x0, .L3595
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w20
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3565
.L3595:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	ldrb	w2, [x20,1]
	ldr	x1, [x0,3560]
	strb	w2, [x1,x3]
	strh	w25, [x20,48]
	ldrb	w1, [x0,2919]
	strb	w23, [x20,61]
	add	w1, w1, 1
	strb	w1, [x0,2919]
	ldrh	w0, [x0,3570]
	ldrb	w1, [x22,9]
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w25, w0
	adrp	x25, .LANCHOR3
	bne	.L3596
	adrp	x1, .LANCHOR6
	add	x0, x1, :lo12:.LANCHOR6
	mov	x6, x1
	ldrb	w0, [x0,221]
	cbz	w0, .L3597
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2008
	ldrh	w0, [x0,w27,sxtw 1]
	cmp	w0, w27
	bcs	.L3597
	tbnz	x24, 0, .L3596
.L3597:
	add	x5, x25, :lo12:.LANCHOR3
	ldr	x2, [x29,128]
	add	x28, x19, :lo12:.LANCHOR0
	mov	w1, -1
	str	x6, [x29,96]
	ldr	x0, [x5,1424]
	str	w1, [x0,x2,lsl 2]
	ldrh	w0, [x28,3570]
	ldrb	w2, [x28,3572]
	ldr	x1, [x5,1424]
	mul	w2, w0, w2
	ldr	x0, [x20,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w1, [x28,3572]
	ldrh	w0, [x28,3570]
	ldr	x6, [x29,96]
	mul	w0, w0, w1
	add	x1, x6, :lo12:.LANCHOR6
	lsl	w2, w0, 2
	ldrb	w1, [x1,222]
	cbz	w1, .L3598
	ldrb	w1, [x28,2848]
	lsl	w1, w1, 9
	sub	w2, w1, w2
.L3598:
	add	x25, x25, :lo12:.LANCHOR3
	ldr	x1, [x20,8]
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x25,1432]
	add	x25, x19, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldrb	w2, [x25,2848]
	mov	w1, 0
	ldr	x0, [x20,24]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x20,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	ldrh	w0, [x25,3570]
	ldrb	w1, [x25,3572]
	ldr	x28, [x20,24]
	mul	w1, w0, w1
	ldr	x0, [x20,8]
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x28,4]
	ldr	x1, [x25,608]
	ldr	x0, [x20,24]
	ldr	w1, [x1,132]
	b	.L3710
.L3596:
	ldr	x0, [x29,128]
	mov	w1, 1
	lsl	x28, x0, 2
	add	x0, x25, :lo12:.LANCHOR3
	ldr	x0, [x0,1432]
	ldr	w0, [x0,x28]
	str	w0, [x20,40]
	mov	x0, x20
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3599
	cmn	w0, #1
	bne	.L3600
.L3599:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 24
	ldrb	w2, [x5,744]
	ldrh	w0, [x5,3536]
	sub	w2, w1, w2
	mov	w1, 1
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x20,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x5,3538]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3644
	cmn	w0, #1
	bne	.L3600
.L3644:
	ldr	x0, [x20,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3600:
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3645
	cmn	w0, #1
	bne	.L3603
.L3645:
	ldr	x1, [x29,104]
	mov	w2, 1003
	ldr	x0, [x29,112]
	bl	printk
	bl	dump_stack
.L3603:
	ldr	x1, [x20,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2856]
	cmp	w2, w0
	bcc	.L3605
	mov	w0, -1
	str	w0, [x1,4]
.L3605:
	ldr	x0, [x20,24]
	add	x25, x25, :lo12:.LANCHOR3
	ldr	w1, [x0,4]
	ldr	x0, [x25,1424]
	str	w1, [x0,x28]
	ldr	x0, [x20,24]
	ldr	w1, [x20,40]
.L3710:
	str	w1, [x0,8]
.L3594:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	add	x2, x1, 800
	mov	w5, 2
	sxtw	x7, w24
	adrp	x6, .LANCHOR6
	ldr	x0, [x1,3560]
	ldrb	w3, [x0,x3]
	add	x0, x29, 144
	add	x2, x2, x3, lsl 6
	str	x2, [x0,w24,sxtw 3]
	ldrb	w0, [x1,3553]
	strh	w5, [x2,50]
	cmp	w0, 3
	strb	w23, [x2,61]
	adrp	x5, .LANCHOR3
	bne	.L3606
	ldrb	w10, [x1,756]
	cbz	w10, .L3607
	add	x1, x6, :lo12:.LANCHOR6
	and	w2, w24, 1
	add	w0, w27, w2
	ldrb	w1, [x1,221]
	cbz	w1, .L3608
	ldr	x1, [x29,120]
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2008
	ldrh	w1, [x1,w27,sxtw 1]
	cbz	w2, .L3608
	cmp	w1, w27
	csel	w0, w0, w1, cs
.L3608:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	w4, [x29,136]
	udiv	w1, w24, w21
	add	x10, x2, x3, lsl 6
	add	w1, w1, w4
	ldrh	w2, [x2,3634]
	add	x1, x22, x1, sxtw 1
	ldrh	w1, [x1,16]
	madd	w0, w1, w2, w0
	orr	w0, w0, 50331648
	str	w0, [x10,840]
	b	.L3609
.L3607:
	udiv	w0, w24, w0
	ldr	w3, [x29,136]
	ldrh	w1, [x1,3634]
	add	w3, w0, w3
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	ldrh	w10, [x2,48]
	mul	w1, w3, w1
	add	x3, x5, :lo12:.LANCHOR3
	add	w0, w0, w0, lsl 1
	orr	w1, w1, w27
	sub	w0, w24, w0
	str	w1, [x2,40]
	ldr	x2, [x3,1440]
	uxth	w0, w0
	add	w0, w0, 1
	orr	w0, w1, w0, lsl 24
	str	w0, [x2,x10,lsl 2]
	b	.L3610
.L3606:
	cmp	w0, 2
	bne	.L3609
	ldrb	w10, [x1,3554]
	add	x2, x2, 32
	ldrh	w0, [x1,3634]
	ldr	w1, [x29,136]
	cbnz	w10, .L3612
	add	w1, w24, w1
	add	x1, x22, x1, sxtw 1
	ldrh	w1, [x1,16]
	madd	w0, w1, w0, w27
	b	.L3711
.L3612:
	add	w1, w1, w24, lsr 1
	add	x1, x22, x1, sxtw 1
	ldrh	w1, [x1,16]
	madd	w0, w1, w0, w27
	and	w1, w24, 1
	add	w0, w0, w1
.L3711:
	orr	w0, w0, 33554432
	str	w0, [x2,8]
.L3609:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 800
	add	x3, x0, x3, lsl 6
	add	x0, x5, :lo12:.LANCHOR3
	ldrh	w1, [x3,48]
	ldr	x0, [x0,1440]
	ldr	w2, [x3,40]
	str	w2, [x0,x1,lsl 2]
.L3610:
	add	x6, x6, :lo12:.LANCHOR6
	ldrb	w0, [x6,221]
	cbz	w0, .L3613
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2008
	ldrh	w0, [x0,w27,sxtw 1]
	cmp	w0, w27
	bcs	.L3613
	tbz	x24, 0, .L3613
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	mov	w2, -1
	add	x5, x5, :lo12:.LANCHOR3
	ldr	x0, [x1,3560]
	strb	w2, [x0,x3]
	add	x0, x29, 144
	ldr	x1, [x1,3560]
	ldr	x2, [x0,x7,lsl 3]
	ldrb	w0, [x22,9]
	mul	w0, w21, w0
	sub	w0, w26, w0, lsl 1
	uxth	w0, w0
	strh	w0, [x2,48]
	ldrb	w2, [x2,1]
	uxth	x0, w0
	strb	w2, [x1,x0]
	mov	w1, -1
	ldr	x0, [x5,1432]
	str	w1, [x0,x3,lsl 2]
.L3613:
	add	w4, w24, 1
	uxth	w24, w4
	b	.L3593
.L3714:
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w0, [x3,3553]
	cmp	w0, 3
	add	x0, x29, 144
	bne	.L3615
	ldrb	w2, [x3,756]
	cbz	w2, .L3715
	mov	w1, 0
.L3616:
	ldrb	w2, [x22,9]
	mul	w2, w2, w21
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L3716
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L3616
.L3716:
	ldr	x0, [x0,w2,sxtw 3]
	b	.L3713
.L3715:
	ldrb	w1, [x3,3568]
	cbnz	w1, .L3712
	cmp	w23, 1
	mov	w2, 9
	beq	.L3712
	cmp	w23, 2
	bne	.L3623
	mov	w2, 13
.L3712:
	ldr	x1, [x29,144]
	strb	w2, [x1,60]
	b	.L3622
.L3623:
	ldr	x2, [x29,144]
	strb	w1, [x2,60]
.L3622:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L3619
.L3615:
	ldrb	w2, [x3,3554]
	cbz	w2, .L3687
	ldrb	w3, [x3,3555]
	mov	w2, 0
	cbz	w3, .L3687
	bl	sblk_3d_mlc_prog_pages
	b	.L3619
.L3687:
	ldrb	w1, [x22,9]
	mul	w1, w1, w21
	sub	w1, w1, #1
	cmp	w2, w1
	bge	.L3717
	ldr	x1, [x0,w2,sxtw 3]
	add	w2, w2, 1
	ldr	x3, [x0,w2,sxtw 3]
	uxth	w2, w2
	ldrb	w3, [x3,1]
	strb	w3, [x1]
	b	.L3687
.L3717:
	ldr	x0, [x0,w1,sxtw 3]
.L3713:
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,144]
	mul	w1, w21, w1
	bl	sblk_prog_page
.L3619:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,3568]
	cbz	w1, .L3626
	ldrb	w21, [x22,9]
	add	w21, w21, w21, lsl 1
	and	w21, w21, 1023
	b	.L3627
.L3626:
	ldrb	w1, [x0,3554]
	cbz	w1, .L3628
	ldrb	w21, [x22,9]
	ubfiz	w21, w21, 1, 8
	b	.L3627
.L3628:
	ldrb	w0, [x0,756]
	cbz	w0, .L3629
	ldrb	w0, [x22,9]
	adrp	x1, .LANCHOR6+221
	mul	w21, w21, w0
	ldrb	w1, [x1,#:lo12:.LANCHOR6+221]
	uxth	w21, w21
	cbz	w1, .L3627
	ldr	x1, [x29,120]
	add	x24, x1, :lo12:.LANCHOR2
	add	x24, x24, 2008
	ldrh	w1, [x24,w27,sxtw 1]
	cmp	w1, w27
	csel	w21, w21, w0, cs
	b	.L3627
.L3629:
	ldrb	w21, [x22,9]
.L3627:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2880]
	ldr	w0, [x1,52]
	add	w0, w0, w21
	str	w0, [x1,52]
	ldrh	w0, [x19,3482]
	add	w21, w21, w0
	ldrh	w0, [x19,3570]
	uxth	w21, w21
	strh	w21, [x19,3482]
	ldrb	w1, [x22,9]
	mul	w0, w0, w1
	cmp	w21, w0
	blt	.L3630
	ldr	x0, [x19,608]
	strh	wzr, [x0,86]
.L3630:
	bl	gc_write_completed
.L3565:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 208
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
	add	x1, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp,80]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x27, x28, [sp,96]
	mov	w26, w0
	ldrh	w20, [x1,2860]
	cmp	w26, 1
	ldrh	w0, [x1,2864]
	ldr	x22, [x1,608]
	add	w20, w0, w20
	ldrh	w21, [x1,2862]
	uxth	w20, w20
	bne	.L3719
	adrp	x23, .LANCHOR6
	add	x1, x23, :lo12:.LANCHOR6
	ldr	w24, [x1,256]
	cbnz	w24, .L3720
	ldr	w1, [x1,260]
	cbz	w1, .L3719
.L3720:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,3602]
	cmp	w20, w2, lsr 2
	bls	.L3719
	ldrh	w1, [x1,2888]
	cmp	w1, w20
	bcs	.L3719
	add	w24, w24, 20
	bl	timer_get_time
	cmp	w24, w0
	bcs	.L3721
	add	x0, x23, :lo12:.LANCHOR6
	str	wzr, [x0,256]
.L3721:
	add	x19, x23, :lo12:.LANCHOR6
	ldr	w20, [x19,260]
	bl	timer_get_time
	add	w20, w20, 20
	cmp	w20, w0
	bcs	.L3913
	str	wzr, [x19,260]
	b	.L3913
.L3719:
	adrp	x23, .LANCHOR3
	add	x1, x23, :lo12:.LANCHOR3
	ldrb	w2, [x1,1418]
	mov	w1, 16
	cmp	w2, 6
	bhi	.L3884
	adrp	x1, .L3725
	add	x1, x1, :lo12:.L3725
	ldrh	w1, [x1,w2,uxtw #1]
	adr	x2, .Lrtx3725
	add	x1, x2, w1, sxth #2
	br	x1
.Lrtx3725:
	.section	.rodata
	.align	0
	.align	2
.L3725:
	.2byte	(.L3724 - .Lrtx3725) / 4
	.2byte	(.L3726 - .Lrtx3725) / 4
	.2byte	(.L3727 - .Lrtx3725) / 4
	.2byte	(.L3728 - .Lrtx3725) / 4
	.2byte	(.L3729 - .Lrtx3725) / 4
	.2byte	(.L3838 - .Lrtx3725) / 4
	.2byte	(.L3731 - .Lrtx3725) / 4
	.text
.L3724:
	add	w21, w21, w0
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x22,80]
	uxth	w21, w21
	ldrh	w1, [x0,2866]
	ldrh	w25, [x0,2868]
	add	w25, w25, w1
	mov	w1, 65535
	cmp	w2, w1
	uxth	w25, w25
	beq	.L3732
	cbnz	w26, .L3733
	ldrh	w0, [x0,2888]
	cmp	w20, w0, lsl 1
	bge	.L3913
.L3733:
	add	x24, x19, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w0, [x24,3600]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x24,3600]
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3735
	add	x3, x23, :lo12:.LANCHOR3
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1420]
	add	w1, w1, 1
	str	w1, [x3,1420]
	ldr	x3, [x24,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x24,3596]
	cmp	w3, w2
	bcs	.L3736
	ldrh	w3, [x24,576]
	cmp	w1, w3, lsr 4
	bls	.L3735
	ldrh	w1, [x24,2890]
	cmp	w1, w2
	bls	.L3735
.L3736:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x2,3569]
	bl	gc_add_sblk
	cbz	w0, .L3737
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 1
	str	wzr, [x23,1420]
	strb	w0, [x23,1418]
	b	.L3913
.L3735:
	add	x0, x19, :lo12:.LANCHOR0
	strh	wzr, [x0,3600]
.L3737:
	cmp	w21, 15
	mov	w24, 2
	bls	.L3738
	cbz	w25, .L3840
	add	x0, x19, :lo12:.LANCHOR0
	mov	w24, 1
	ldrh	w1, [x0,2870]
	ldrh	w0, [x0,3604]
	cmp	w1, w0
	bls	.L3738
.L3840:
	mov	w24, 2
.L3738:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3739
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, w20
	mov	w4, w21
	ldr	x0, [x1,608]
	ldrb	w2, [x1,3569]
	mov	w1, 2171
	ldrh	w5, [x0,124]
	ldrh	w6, [x0,120]
	ldrh	w7, [x0,122]
	ldrh	w0, [x22,80]
	str	w0, [sp]
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	bl	printk
.L3739:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w24
	mov	w2, 1
	ldrb	w0, [x19,3569]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3740
.L3741:
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 1
	b	.L3916
.L3740:
	ldrb	w0, [x19,3569]
	mov	w1, 3
	mov	w2, 1
	bl	gc_search_src_blk
	cmp	w0, wzr
	bgt	.L3741
	b	.L3913
.L3732:
	adrp	x27, .LANCHOR2
	ldr	w1, [x27,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3742
	ldrb	w2, [x0,3569]
	adrp	x0, .LC304
	ldrh	w5, [x22,124]
	add	x0, x0, :lo12:.LC304
	ldrh	w6, [x22,120]
	mov	w1, 2183
	ldrh	w7, [x22,122]
	mov	w3, w20
	str	w26, [sp]
	mov	w4, w21
	bl	printk
.L3742:
	cmp	w26, 1
	bne	.L3743
	add	x22, x19, :lo12:.LANCHOR0
	bl	gc_scan_static_data
	ldr	x0, [x22,608]
	ldrh	w0, [x0,122]
	cbz	w0, .L3744
.L3745:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR3
	strb	w0, [x19,3569]
	b	.L3916
.L3744:
	bl	gc_static_wearleveling
	cbnz	w0, .L3745
	bl	gc_block_vpn_scan
	cbz	w21, .L3841
	cmp	w20, w25
	bcs	.L3748
	ldrh	w0, [x22,3602]
	cmp	w0, w20
	bhi	.L3749
.L3748:
	add	x0, x19, :lo12:.LANCHOR0
	add	w1, w20, w25
	ldrh	w2, [x0,3602]
	cmp	w1, w2
	blt	.L3749
	ldrh	w1, [x0,2870]
	ldrh	w0, [x0,3604]
	cmp	w1, w0
	bcc	.L3841
.L3749:
	add	x22, x19, :lo12:.LANCHOR0
	add	x28, x23, :lo12:.LANCHOR3
	mov	w2, 1
	ldr	w1, [x28,1420]
	ldrh	w0, [x22,576]
	add	w1, w1, 1
	strb	w2, [x22,3569]
	str	w1, [x28,1420]
	cmp	w1, w0, lsr 5
	bls	.L3751
	ldrh	w0, [x22,2870]
	cmp	w0, w21
	bls	.L3751
	mov	w1, 5
	mov	w0, 0
	str	x2, [x29,104]
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w0, 65535
	cmp	w1, w0
	beq	.L3841
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x22,600]
	ldrb	w3, [x22,3572]
	mov	w24, 16
	ldr	x2, [x29,104]
	ldrh	w0, [x0,x1]
	ldrh	w1, [x22,3596]
	mul	w1, w1, w3
	cmp	w0, w1
	bgt	.L3747
	str	wzr, [x28,1420]
	mov	w0, w2
	mov	w1, 2
	b	.L3906
.L3751:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2870]
	ldrh	w1, [x0,3604]
	cmp	w2, w1
	bcc	.L3754
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	b	.L3907
.L3754:
	ldrh	w24, [x0,2866]
	cbnz	w24, .L3755
	ldrh	w0, [x0,2868]
	cmp	w0, 8
	bls	.L3747
.L3755:
	mov	w0, 1
	mov	w1, w0
.L3906:
	mov	w2, 4
.L3907:
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3741
	b	.L3841
.L3743:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2888]
	cmp	w0, w20
	bcc	.L3913
.L3841:
	mov	w24, 16
.L3747:
	add	x22, x19, :lo12:.LANCHOR0
	ldr	w0, [x22,2900]
	cbz	w0, .L3756
	mov	w0, 1
	mov	w1, 5
	strb	w0, [x22,3569]
	mov	w0, 0
	str	wzr, [x22,2900]
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3757
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x22,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 8
	bhi	.L3757
	mov	w2, 1
	mov	w1, 0
	str	w2, [x22,2900]
	bl	gc_add_sblk
	cbnz	w0, .L3741
.L3757:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3758
	add	x3, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x3,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3758
	mov	w2, 1
	mov	w1, 0
	str	w2, [x3,2900]
	bl	gc_add_sblk
	cbnz	w0, .L3741
.L3758:
	mov	w0, 0
	bl	zftl_get_gc_node.part.11
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3756
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3756
	mov	w1, 1
	str	w1, [x2,2900]
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3741
.L3756:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	x7, [x22,608]
	strb	w0, [x22,3569]
	ldrh	w5, [x7,124]
	cbz	w5, .L3759
	add	x23, x23, :lo12:.LANCHOR3
	strb	wzr, [x22,3569]
	strb	w0, [x23,1418]
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3760
	ldrh	w6, [x7,120]
	adrp	x0, .LC305
	ldrh	w7, [x7,122]
	mov	w1, 2273
	mov	w2, 0
	mov	w3, w20
	mov	w4, w21
	add	x0, x0, :lo12:.LC305
	bl	printk
	b	.L3760
.L3759:
	ldrh	w2, [x22,2888]
	cmp	w20, w2
	bcs	.L3761
	cbz	w21, .L3762
	cmp	w21, 16
	bls	.L3763
	ldrh	w2, [x22,2870]
	ldrh	w1, [x22,3604]
	cmp	w2, w1
	bhi	.L3763
	mov	w1, w0
	mov	w2, 4
	str	x5, [x29,104]
	bl	gc_search_src_blk
	uxth	w0, w0
	ldr	x5, [x29,104]
	cbnz	w0, .L3764
	ldrb	w0, [x22,3569]
	b	.L3915
.L3764:
	mov	w1, 5
	mov	w0, w5
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3835
	add	x3, x23, :lo12:.LANCHOR3
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1420]
	add	w1, w1, 1
	str	w1, [x3,1420]
	ldr	x3, [x22,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x22,3596]
	cmp	w3, w2
	bcs	.L3767
	ldrh	w3, [x22,576]
	cmp	w1, w3, lsr 4
	bls	.L3835
	ldrh	w1, [x22,2890]
	cmp	w1, w2
	bls	.L3835
.L3767:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x19,3569]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19,2900]
	add	x0, x23, :lo12:.LANCHOR3
	str	wzr, [x0,1420]
	b	.L3835
.L3763:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	add	x19, x19, :lo12:.LANCHOR0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3835
	b	.L3910
.L3762:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	strb	w21, [x22,3569]
	tbz	x0, 8, .L3769
	adrp	x0, .LC305
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC305
	mov	w1, 2303
	mov	w2, w21
	mov	w3, w20
	mov	w4, w21
	mov	w5, w21
	bl	printk
.L3769:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, 16
	ldrb	w0, [x19,3569]
	bls	.L3770
.L3915:
	mov	w1, 3
	mov	w2, 4
	b	.L3909
.L3770:
	mov	w1, 1
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3835
.L3910:
	ldrb	w0, [x19,3569]
	mov	w1, 3
	mov	w2, 2
.L3909:
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3765
.L3761:
	cmp	w26, 1
	mov	w1, w24
	bne	.L3884
	cmp	w20, w2, lsl 1
	bge	.L3771
	cmp	w25, w21, lsr 1
	bcs	.L3772
	ldrh	w2, [x22,2870]
	ldrh	w0, [x22,3604]
	cmp	w2, w0
	bcs	.L3772
	ldrh	w0, [x22,3602]
	lsr	w0, w0, 2
	strh	w0, [x22,2888]
	b	.L3884
.L3772:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w22, w0
	mov	w1, 65535
	mov	w27, w22
	cmp	w22, w1
	bne	.L3773
.L3778:
	cmp	w21, 1
	bhi	.L3774
	b	.L3775
.L3773:
	add	x26, x23, :lo12:.LANCHOR3
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x22, w22
	ldr	w1, [x26,1420]
	ldrh	w3, [x2,576]
	add	w1, w1, 1
	str	w1, [x26,1420]
	cmp	w1, w3, lsr 4
	bls	.L3776
	ldr	x1, [x2,600]
	str	wzr, [x26,1420]
	ldrh	w3, [x1,x22,lsl 1]
	ldrh	w1, [x2,2890]
	cmp	w3, w1
	bcs	.L3776
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	cbz	w0, .L3776
	mov	w0, 1
	strb	w0, [x26,1418]
	b	.L3760
.L3776:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x22,lsl 1]
	ldrh	w0, [x1,3596]
	cmp	w2, w0, lsr 1
	bhi	.L3777
	mov	w0, w27
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L3835
.L3777:
	ldrh	w3, [x1,2866]
	ldrh	w0, [x1,2868]
	add	w0, w3, w0
	ldrh	w3, [x1,3602]
	cmp	w0, w3, lsl 1
	ble	.L3778
	ldrh	w0, [x1,2890]
	cmp	w0, w2
	bcc	.L3775
	b	.L3778
.L3774:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 1
	cmp	w21, 16
	strb	w0, [x22,3569]
	bls	.L3779
	ldrh	w2, [x22,2870]
	ldrh	w1, [x22,3604]
	cmp	w2, w1
	bhi	.L3779
	mov	w1, w0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3780
	ldrb	w0, [x22,3569]
	mov	w1, 3
	mov	w2, 4
	b	.L3908
.L3779:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3780
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 3
	mov	w2, 2
	ldrb	w0, [x0,3569]
.L3908:
	bl	gc_search_src_blk
	uxth	w0, w0
.L3780:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, w25, lsr 1
	ldrh	w1, [x19,3602]
	bls	.L3911
	lsr	w1, w1, 2
	b	.L3912
.L3775:
	cmp	w20, w25
	add	x19, x19, :lo12:.LANCHOR0
	bcs	.L3782
	mov	w0, 4
	strb	wzr, [x19,3569]
	bl	zftl_get_gc_node.part.11
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3782
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,600]
	ldrb	w2, [x19,3572]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x19,3596]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	bgt	.L3782
	mov	w1, 3
	mov	w0, 0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	ldrh	w1, [x19,3602]
.L3911:
	lsr	w1, w1, 1
.L3912:
	strh	w1, [x19,2888]
	b	.L3765
.L3782:
	ldrh	w0, [x19,3602]
	lsr	w0, w0, 2
	strh	w0, [x19,2888]
	b	.L3760
.L3771:
	ldrh	w0, [x22,3602]
	mov	w24, w5
	lsr	w0, w0, 2
	strh	w0, [x22,2888]
	b	.L3760
.L3765:
	mov	w1, w24
	cbz	w0, .L3884
.L3835:
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x23,1418]
	b	.L3760
.L3726:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x20,2912]
	cmp	w1, w0
	bne	.L3785
	bl	gc_get_src_blk
	strh	w0, [x20,2912]
.L3785:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0,2912]
	cmp	w1, w2
	beq	.L3786
	ldrh	w2, [x0,576]
	cmp	w2, w1
	bhi	.L3786
	mov	w1, -1
	strh	w1, [x0,2912]
.L3786:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2912
	ldrh	w3, [x0,2912]
	cmp	w3, w2
	beq	.L3914
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,584]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3789
	mov	x0, 0
.L3788:
	cmp	w5, w0, uxth
	bls	.L3789
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3788
.L3794:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2912]
	b	.L3913
.L3789:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L3848
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3791
.L3848:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3794
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2538
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3794
.L3791:
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 2
	b	.L3916
.L3727:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3795
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 3
.L3916:
	strb	w0, [x23,1418]
	b	.L3913
.L3795:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,2912]
	cmp	w0, w3
	beq	.L3741
	ldrh	w3, [x1,2936]
	add	x23, x23, :lo12:.LANCHOR3
	cbz	w3, .L3796
	mov	w0, 4
	strh	wzr, [x1,2938]
	strb	w0, [x23,1418]
	b	.L3913
.L3796:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,600]
	mov	w2, 1
	strb	w2, [x23,1418]
	ldrh	w0, [x1,x0]
	cbz	w0, .L3797
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2566
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3797:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x20, 2912
	ldrh	w0, [x20,2912]
	bl	ftl_free_sblk
	ldr	x0, [x20,600]
	ldrh	w1, [x20,2912]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x21,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3798
	strh	w0, [x21,30]
	b	.L3794
.L3798:
	strh	wzr, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3794
.L3728:
	add	x21, x19, :lo12:.LANCHOR0
.L3885:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,2914]
	add	x0, x21, 2912
	ldrh	w1, [x21,3596]
	cmp	w2, w1
	bcs	.L3800
	cmp	w20, 7
	bls	.L3885
	b	.L3913
.L3800:
	ldrh	w3, [x0,24]
	adrp	x2, .LANCHOR2
	cbz	w3, .L3801
	add	x23, x23, :lo12:.LANCHOR3
	mov	w1, 4
	strh	wzr, [x0,26]
	strb	w1, [x23,1418]
	ldr	w1, [x2,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3802
	ldrh	w1, [x0]
	adrp	x0, .LC306
	ldr	x4, [x21,600]
	add	x0, x0, :lo12:.LC306
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L3802:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2912]
	ldr	x1, [x0,600]
	ldrh	w0, [x0,2936]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L3803
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2600
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3803:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,2912]
	ldr	x0, [x19,600]
	ldrh	w2, [x19,2936]
	strh	w2, [x0,x1,lsl 1]
	b	.L3913
.L3801:
	add	x23, x23, :lo12:.LANCHOR3
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x20, [x21,584]
	add	x20, x20, x1, uxth 2
	strb	w0, [x23,1418]
	ldr	w0, [x2,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3804
	ldrb	w2, [x20,2]
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
	ubfx	x2, x2, 5, 3
	bl	printk
.L3804:
	ldrb	w0, [x20,2]
	tst	w0, 192
	beq	.L3849
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3805
.L3849:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2610
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3805:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2912
	ldrh	w0, [x19,2912]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,2912]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3807
	strh	w0, [x20,30]
	b	.L3913
.L3807:
	strh	wzr, [x20,30]
	b	.L3917
.L3729:
	cbnz	w26, .L3808
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2888]
	cmp	w0, w20
	bcc	.L3913
.L3808:
	ldrh	w1, [x22,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3809
	add	x24, x19, :lo12:.LANCHOR0
	ldrb	w21, [x24,3569]
	cmp	w21, 1
	bne	.L3809
	bl	ftl_flush
	ldrh	w0, [x24,3486]
	cbz	w0, .L3810
	mov	w0, w21
.L3810:
	mov	w1, 5
	bl	zftl_gc_get_free_sblk
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L3812
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x21, [x0,584]
	add	x21, x21, x20, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L3813
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2639
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3813:
	ldrb	w0, [x21,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x21,2]
.L3836:
	mov	w0, w20
	mov	w1, 1
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	bl	ftl_erase_sblk
	mov	w0, 5
	add	x1, x22, 96
	strb	w0, [x22,84]
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x19,3596]
	strb	w0, [x22,89]
	ldrb	w2, [x19,3572]
	mul	w0, w0, w1
	strh	w0, [x22,86]
	mov	w1, 255
	ldrh	w0, [x19,3570]
	strh	w20, [x22,80]
	strh	wzr, [x22,82]
	mul	w2, w0, w2
	ldr	x0, [x23,1424]
	strb	wzr, [x22,85]
	lsl	w2, w2, 2
	strh	wzr, [x22,90]
	bl	ftl_memset
	ldrh	w0, [x19,3570]
	mov	w1, 255
	ldrb	w2, [x19,3572]
	mul	w2, w0, w2
	ldr	x0, [x23,1432]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x19,3570]
	mov	w1, 255
	ldrb	w2, [x19,3572]
	ldr	x0, [x19,3560]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x19,608]
	mov	w1, -1
	str	w20, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x19,2968]
	ldr	x0, [x19,2880]
	strh	w20, [x0,126]
	mov	w0, -1
	str	w0, [x19,3492]
	mov	w0, 0
	strh	wzr, [x19,3482]
	strh	wzr, [x19,3484]
	strh	wzr, [x19,3488]
	bl	ftl_info_flush
	b	.L3913
.L3809:
	cmp	w26, 1
	mov	w21, 4
	csinc	w21, w21, wzr, eq
	cmp	w20, 15
	add	w0, w21, 4
	add	x24, x19, :lo12:.LANCHOR0
	csel	w21, w0, w21, ls
.L3816:
	sub	w21, w21, #1
	uxtb	w21, w21
	cmp	w21, 255
	beq	.L3913
	bl	gc_do_copy_back
	ldrb	w0, [x24,3569]
	cbnz	w0, .L3817
	ldrb	w0, [x24,2850]
	cmp	w0, 3
	bhi	.L3818
	bl	ftl_write_commit
.L3818:
	ldrh	w1, [x24,2938]
	ldrh	w0, [x24,2936]
	cmp	w1, w0
	bcc	.L3816
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x23,1418]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x24,2912]
	ldr	x0, [x24,600]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3820
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2714
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3820:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2912]
	ldr	x3, [x1,600]
	ubfiz	x2, x0, 1, 16
	ldrh	w4, [x3,x2]
	cbnz	w4, .L3821
	strh	w4, [x3,x2]
	ldrh	w0, [x1,2912]
	bl	ftl_free_sblk
	b	.L3794
.L3821:
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	b	.L3794
.L3817:
	add	x20, x24, 2912
	ldrh	w0, [x20,576]
	cbz	w0, .L3822
	strh	wzr, [x20,576]
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	w0, [x20,580]
	cmn	w0, #1
	beq	.L3823
	bl	ftl_mask_bad_block
.L3823:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,608]
	str	wzr, [x20,3492]
	strh	wzr, [x20,2968]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x20,608]
	mov	w0, -1
	ldr	x2, [x20,2880]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x20,2920]
	cbz	x0, .L3824
	bl	zbuf_free
.L3824:
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	str	xzr, [x19,2920]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2752
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x23,1418]
	bl	printk
	bl	dump_stack
	b	.L3913
.L3822:
	ldrh	w0, [x22,86]
	cmp	w0, 1
	bls	.L3825
	ldrh	w1, [x20,26]
	ldrh	w0, [x20,24]
	cmp	w1, w0
	bcc	.L3816
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x23,1418]
	ldrh	w0, [x20,56]
	add	w1, w0, 1
	add	x0, x20, x0, sxtw 1
	strh	w1, [x20,56]
	ldrh	w1, [x20]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x20]
	b	.L3913
.L3825:
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 5
	ldrh	w1, [x20,26]
	strb	w0, [x23,1418]
	ldrh	w0, [x20,24]
	cmp	w1, w0
	bcc	.L3826
	ldrh	w0, [x20,56]
	add	w1, w0, 1
	add	x0, x20, x0, sxtw 1
	strh	w1, [x20,56]
	ldrh	w1, [x20]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x20]
.L3826:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,608]
	ldrh	w1, [x22,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,2928]
	ldrh	w1, [x20,3596]
	strh	w1, [x20,2930]
	ldrb	w2, [x20,756]
	ldrb	w1, [x20,3553]
	strh	w1, [x20,2932]
	cbz	w2, .L3827
	ldrh	w2, [x20,3570]
	strh	w2, [x20,2930]
	mov	w2, 1
	strh	w2, [x20,2932]
.L3827:
	cmp	w1, 2
	bne	.L3829
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w1, [x2,2930]
	lsl	w1, w1, 1
	strh	w1, [x2,2930]
	ldrb	w1, [x2,3554]
	cbnz	w1, .L3829
	mov	w1, 1
	strh	w1, [x2,2932]
.L3829:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2934]
	b	.L3913
.L3838:
	mov	w21, 0
	add	x24, x19, :lo12:.LANCHOR0
.L3730:
	bl	gc_check_data_one_wl
	cbz	w0, .L3832
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	ldr	x0, [x19,608]
	strh	wzr, [x19,2968]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,608]
	mov	w0, -1
	ldr	x2, [x19,2880]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,2920]
	bl	zbuf_free
	str	xzr, [x19,2920]
	strb	wzr, [x23,1418]
.L3917:
	bl	flt_sys_flush
	b	.L3913
.L3832:
	ldrh	w1, [x24,2928]
	ldrh	w0, [x24,2930]
	cmp	w1, w0
	bcc	.L3833
	add	x23, x23, :lo12:.LANCHOR3
	mov	w0, 6
	strb	w0, [x23,1418]
	ldr	x0, [x24,2920]
	bl	zbuf_free
	str	xzr, [x24,2920]
	b	.L3913
.L3833:
	cmp	w20, 15
	bls	.L3730
	cmp	w26, 1
	bne	.L3913
	add	w21, w21, 1
	uxtb	w21, w21
	cmp	w21, 4
	bls	.L3730
	b	.L3913
.L3731:
	bl	gc_update_l2p_map_new
	mov	w20, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	pm_flush
	strh	w20, [x22,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,2880]
	strh	w20, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L3914:
	add	x23, x23, :lo12:.LANCHOR3
	strb	wzr, [x23,1418]
.L3913:
	mov	w1, 16
	b	.L3884
.L3760:
	mov	w1, w24
	b	.L3884
.L3812:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2645
	add	x1, x1, 832
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3836
.L3884:
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
	.global	zftl_write
	.type	zftl_write, %function
zftl_write:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	adrp	x0, .LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w28, w1
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w23, w2
	mov	x24, x3
	tbz	x0, 12, .L3919
	adrp	x0, .LC308
	ldr	w4, [x24]
	add	x0, x0, :lo12:.LC308
	mov	w1, w19
	mov	w2, w28
	mov	w3, w23
	bl	printk
.L3919:
	cbnz	w19, .L3920
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3921
.L3920:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3922
	lsl	w19, w19, 13
	mov	w2, 8192
.L3921:
	add	w1, w28, w23
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3922
	add	w28, w19, w28
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	sub	w22, w23, #1
	add	w22, w22, w28
	add	w26, w28, w23
	ldrb	w0, [x0,2848]
	udiv	w25, w28, w0
	mov	w21, w25
	udiv	w22, w22, w0
	sub	w20, w22, w25
	add	w20, w20, 1
.L3923:
	cbz	w20, .L3942
	mov	w0, 0
	bl	buf_alloc
	mov	x27, x0
	cbnz	x0, .L3924
	bl	ftl_write_commit
	b	.L3923
.L3924:
	strb	wzr, [x0,57]
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w21, w25
	ldrb	w4, [x0,2848]
	cset	w0, eq
	strb	w4, [x27,56]
	cmp	w21, w22
	beq	.L3926
	cbz	w0, .L3927
	b	.L3936
.L3926:
	cbz	w0, .L3928
.L3936:
	udiv	w0, w28, w4
	msub	w0, w0, w4, w28
	uxtb	w0, w0
	strb	w0, [x27,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w23
	csel	w4, w23, w4, hi
	b	.L3940
.L3928:
	msub	w4, w4, w21, w26
.L3940:
	strb	w4, [x27,56]
.L3927:
	ldrb	w1, [x27,57]
	sub	w20, w20, #1
	ldr	x0, [x27,8]
	ldrb	w2, [x27,56]
	add	x0, x0, x1, lsl 9
	mov	x1, x24
	lsl	w2, w2, 9
	bl	ftl_memcpy
	str	w21, [x27,36]
	add	x0, x19, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldr	x1, [x0,2880]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x27,32]
	mov	x0, x27
	bl	ftl_write_buf
	ldrb	w0, [x27,56]
	add	x24, x24, x0, lsl 9
	b	.L3923
.L3942:
	bl	ftl_write_commit
	mov	w0, w20
.L3941:
	mov	w1, 1
	bl	zftl_do_gc
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,2860]
	ldrh	w0, [x1,2864]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L3943
	mov	w0, 0
	b	.L3941
.L3943:
	bl	timer_get_time
	adrp	x1, .LANCHOR6+260
	str	w0, [x1,#:lo12:.LANCHOR6+260]
	mov	w0, 0
.L3922:
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
	add	w2, w0, w1
	mov	w4, -1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w1, [x1,520]
	cmp	w2, w1
	bhi	.L3947
	adrp	x22, .LANCHOR6
	add	w23, w0, 24576
	add	x0, x22, :lo12:.LANCHOR6
	adrp	x25, .LANCHOR2
	ldr	w1, [x0,264]
	add	w1, w19, w1
	str	w1, [x0,264]
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3948
	adrp	x0, .LC309
	mov	w2, w23
	add	x0, x0, :lo12:.LC309
	mov	w3, w19
	mov	w4, 0
	bl	printk
.L3948:
	add	x20, x21, :lo12:.LANCHOR0
	ldr	x0, [x20,2880]
	ldr	w26, [x0,8]
	add	w1, w26, 1
	str	w1, [x0,8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	w24, [x20,2848]
	udiv	w20, w23, w24
	msub	w27, w20, w24, w23
	cbz	w27, .L3949
	sub	w24, w24, w27
	mov	w0, w20
	cmp	w24, w19
	csel	w24, w24, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3950
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3950:
	ldr	w0, [x29,120]
	and	w28, w24, 65535
	cmn	w0, #1
	beq	.L3952
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3952
	strb	w27, [x0,57]
	ubfiz	x27, x27, 9, 25
	str	w20, [x0,36]
	mov	w1, 0
	strb	w24, [x0,56]
	lsl	w2, w28, 9
	str	w26, [x0,32]
	ldr	x0, [x0,8]
	str	x3, [x29,104]
	add	x0, x0, x27
	bl	ftl_memset
	ldr	x3, [x29,104]
	mov	x0, x3
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2880]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3952:
	add	w20, w20, 1
	sub	w19, w19, w28
.L3949:
	cbz	w19, .L3954
	bl	ftl_flush
.L3954:
	mov	w0, -1
	mov	w27, 1
	str	w0, [x29,124]
	mov	w28, 24
.L3955:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,2848]
	cmp	w19, w0
	bcc	.L3994
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L3956
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3958
	ldrb	w2, [x24,2848]
	mov	w1, 0
	str	w20, [x0,36]
	strb	w2, [x0,56]
	lsl	w2, w2, 9
	str	w26, [x0,32]
	strb	wzr, [x0,57]
	ldr	x0, [x0,8]
	str	x3, [x29,104]
	bl	ftl_memset
	ldr	x3, [x29,104]
	mov	x0, x3
	bl	ftl_write_buf
	bl	ftl_write_commit
	b	.L3993
.L3956:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3958
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	ldrb	w1, [x24,744]
	ldrh	w2, [x24,3536]
	sub	w1, w28, w1
	ldr	w0, [x29,120]
	sub	w1, w1, w2
	lsl	w1, w27, w1
	lsr	w0, w0, w2
	sub	w1, w1, #1
	and	w1, w1, w0
	ldrb	w0, [x24,3538]
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L3993:
	ldr	x1, [x24,2880]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3958:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,2848]
	sub	w19, w19, w0
	b	.L3955
.L3994:
	cbz	w19, .L3962
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3963
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3963:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3962
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L3962
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w26, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,2848]
	cmp	w19, w0
	bcc	.L3965
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1259
	add	x1, x1, 848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3965:
	ldr	x0, [x24,8]
	mov	w1, 0
	lsl	w2, w19, 9
	bl	ftl_memset
	mov	x0, x24
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2880]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3962:
	add	x0, x22, :lo12:.LANCHOR6
	mov	w4, 0
	ldr	w1, [x0,264]
	cmp	w1, 8192
	bls	.L3947
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3966
	adrp	x0, .LC309
	mov	w2, w23
	add	x0, x0, :lo12:.LC309
	mov	w3, w19
	bl	printk
.L3966:
	add	x22, x22, :lo12:.LANCHOR6
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x22,264]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x21,2900]
.L3947:
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
	adrp	x0, .LC268
	mov	w2, 4
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	add	x0, x0, :lo12:.LC268
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR3
	mov	w22, 65535
	ldr	x3, [x19,2880]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR3
	ldr	x1, [x19,2880]
	adrp	x0, .LC269
	mov	w2, 2
	add	x0, x0, :lo12:.LC269
	add	x1, x1, 416
	ldrh	w3, [x3,1456]
	mov	w19, 0
	bl	rknand_print_hex
.L3996:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1456]
	cmp	w0, w19
	bls	.L3999
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,2880]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3997
	mov	x1, 0
	bl	ftl_sblk_dump
.L3997:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L3996
.L3999:
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
	uxtb	w24, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxth	w19, w2
	ldr	x2, [x0,624]
	ldrb	w0, [x0,634]
	ldrb	w2, [x2,19]
	cbnz	w0, .L4000
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L4002
	cmp	w2, 68
	beq	.L4002
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L4000
.L4002:
	adrp	x20, .LANCHOR3
	adrp	x22, .LC310
	add	x20, x20, :lo12:.LANCHOR3
	mov	w23, w1
	add	x22, x22, :lo12:.LC310
	add	x26, x20, 356
	mov	w27, 65535
.L4004:
	add	x25, x21, :lo12:.LANCHOR0
	ldr	x0, [x25,624]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L4000
	add	w28, w19, w23
	ldrh	w3, [x26,w19,sxtw 1]
	mov	x0, x22
	mov	w1, w19
	mov	w2, w28
	bl	printk
	ldrh	w0, [x26,w19,sxtw 1]
	cmp	w0, w27
	bne	.L4000
	ldr	x0, [x20,1488]
	mov	w1, 255
	mov	w2, 16384
	add	w19, w19, 1
	bl	ftl_memset
	uxth	w19, w19
	ldr	x4, [x25,624]
	mov	w0, w24
	ldr	x2, [x20,1488]
	mov	w1, w28
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L4004
.L4000:
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
	stp	x29, x30, [sp, -272]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x25, x26, [sp,64]
	add	x26, x21, :lo12:.LANCHOR0
	mov	x25, x1
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	w19, w0
	str	x2, [x29,176]
	ldr	x0, [x26,624]
	ldrb	w20, [x0,9]
	ldrh	w22, [x0,10]
	adrp	x0, .LANCHOR2+34
	ldrh	w23, [x0,#:lo12:.LANCHOR2+34]
	ldrb	w0, [x21,#:lo12:.LANCHOR0]
	str	w0, [x29,172]
	ldrh	w0, [x29,172]
	str	w0, [x29,200]
	ldrb	w0, [x26,634]
	str	w0, [x29,164]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x21, [x29,184]
	mov	x24, x0
	mov	w0, -1
	cbz	x24, .L4082
	adrp	x0, .LANCHOR6+238
	mov	w21, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR6+238]
	cbz	w0, .L4008
	ldr	x0, [x29,184]
	ldrb	w1, [x0,#:lo12:.LANCHOR0]
	ldrb	w0, [x26,634]
	cbz	w1, .L4009
	cmp	w0, w21
	cset	w21, eq
	b	.L4008
.L4009:
	cmp	w0, 3
	beq	.L4047
	ldr	w2, [x29,200]
	cmp	w0, 2
	mov	w1, 2
	mov	w21, 3
	csel	w1, w2, w1, ne
	cmp	w0, 2
	str	w1, [x29,200]
	csel	w21, wzr, w21, ne
	b	.L4008
.L4047:
	str	w0, [x29,200]
	mov	w21, 2
.L4008:
	mov	w0, 0
	add	w19, w19, 511
	lsr	w19, w19, 9
	bl	zftl_flash_exit_slc_mode
	ldr	x0, [x29,184]
	cmp	w19, 8
	ldr	x1, [x29,184]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,634]
	strb	w0, [x1,#:lo12:.LANCHOR0]
	bls	.L4049
	cmp	w19, 500
	bhi	.L4011
	b	.L4010
.L4049:
	mov	w19, 8
.L4010:
	ldr	w1, [x25]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	beq	.L4012
.L4011:
	mov	x0, x24
	bl	ftl_free
	mov	w0, -1
	b	.L4082
.L4012:
	uxth	w20, w20
	mov	w3, 0
	mov	w2, 63871
	mov	w4, 4097
	mul	w22, w20, w22
	uxth	w22, w22
	sub	w0, w22, #1
	add	w0, w0, w19
	udiv	w0, w0, w22
	str	w0, [x29,160]
	add	x0, x25, 253952
	add	x0, x0, 1532
.L4016:
	ldr	w1, [x0]
	cbnz	w1, .L4013
	ldr	w1, [x25,w3,uxtw 2]
	add	w3, w3, 1
	cmp	w3, w4
	sub	w2, w2, #1
	str	w1, [x0,512]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L4016
	b	.L4015
.L4013:
	adrp	x0, .LC311
	add	x0, x0, :lo12:.LC311
	bl	printk
.L4015:
	mul	w20, w20, w23
	ldr	x1, [x29,176]
	mov	w2, 4
	mov	w3, 5
	mov	x22, 0
	uxth	w0, w20
	str	w0, [x29,192]
	adrp	x0, .LC312
	adrp	x27, .LANCHOR0
	add	x0, x0, :lo12:.LC312
	bl	rknand_print_hex
	adrp	x0, .LC313
	mov	w1, w19
	add	x0, x0, :lo12:.LC313
	mov	w2, w19
	bl	printk
	str	w22, [x29,204]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	x0, [x29,144]
	adrp	x0, .LC314
	add	x0, x0, :lo12:.LC314
	str	x0, [x29,136]
.L4042:
	adrp	x1, .LANCHOR6+238
	mov	w0, w22
	ldrb	w1, [x1,#:lo12:.LANCHOR6+238]
	cbnz	w1, .L4017
	ldr	x1, [x29,176]
	ldr	w2, [x29,192]
	ldr	w1, [x1,x22,lsl 2]
	mul	w20, w1, w2
	cbz	w22, .L4018
	ldr	w2, [x29,160]
	cmp	w2, 1
	bls	.L4018
	sub	w0, w22, #1
	ldr	x2, [x29,176]
	ldr	w0, [x2,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L4018
	b	.L4019
.L4017:
	adrp	x1, .LANCHOR0
	add	x28, x1, :lo12:.LANCHOR0
	ldr	x1, [x28,728]
	add	x1, x1, x22, uxtw
	ldrb	w20, [x1,32]
	cmp	w20, 255
	beq	.L4019
	ldr	w1, [x29,192]
	mul	w20, w20, w1
	cbz	w21, .L4018
	tbnz	x0, 0, .L4054
	cmp	w21, 3
	cset	w23, eq
	cbz	w23, .L4020
.L4054:
	add	x1, x27, :lo12:.LANCHOR0
	ldrb	w0, [x29,200]
	strb	w0, [x27,#:lo12:.LANCHOR0]
	strb	w0, [x1,634]
	b	.L4018
.L4020:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	strb	w23, [x28,634]
	adrp	x0, .LANCHOR0
	strb	w23, [x0,#:lo12:.LANCHOR0]
.L4018:
	mov	w1, 0
	mov	w2, 512
	mov	x0, x24
	bl	ftl_memset
	ldr	x0, [x29,184]
	add	x1, x0, :lo12:.LANCHOR0
	ldr	x2, [x1,624]
	ldrb	w28, [x2,9]
	ldrh	w2, [x2,10]
	uxth	w0, w28
	mul	w2, w0, w2
	uxth	w26, w2
	ldr	x2, [x29,144]
	ldrh	w23, [x2,34]
	mul	w23, w0, w23
	ldrb	w0, [x1,720]
	strb	wzr, [x1,720]
	udiv	w1, w20, w28
	str	w0, [x29,156]
	mov	w0, 0
	uxth	w23, w23
	bl	flash_erase_block
	cmp	w19, w26
	mov	w3, 1
	bls	.L4022
	mov	w0, 0
	add	w1, w20, w23
	bl	flash_erase_block
	mov	w3, 2
.L4022:
	adrp	x0, .LANCHOR0
	add	x26, x0, :lo12:.LANCHOR0
	ldr	x1, [x26,624]
	ldrh	w0, [x1,10]
	ldrb	w1, [x1,12]
	lsl	w0, w0, 2
	mul	w0, w0, w3
	sdiv	w0, w0, w1
	str	w0, [x29,152]
	udiv	w0, w20, w23
	msub	w0, w0, w23, w20
	str	w0, [x29,168]
	sub	w0, w20, w0
	str	w0, [x29,196]
	cmp	w20, w0
	bne	.L4052
	ldrb	w0, [x26,516]
	cmp	w0, 9
	bne	.L4052
	adrp	x0, .LANCHOR3+1528
	mov	w1, 0
	mov	w2, 1024
	ldr	x3, [x0,#:lo12:.LANCHOR3+1528]
	str	x3, [x29,128]
	mov	x0, x3
	bl	ftl_memset
	ldr	x3, [x29,128]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	str	w0, [x3]
	mov	w0, 12
	str	w0, [x3,4]
	ldrb	w0, [x26,634]
	str	wzr, [x3,12]
	strb	wzr, [x3,16]
	cbz	w0, .L4024
	ldr	x0, [x26,624]
	ldrb	w0, [x0,29]
	strb	w0, [x3,16]
.L4024:
	mov	w0, 4
	strb	w0, [x3,17]
	add	x0, x27, :lo12:.LANCHOR0
	cmp	w28, 8
	str	x3, [x29,128]
	sub	w23, w19, #4
	ldr	x1, [x0,624]
	ldrh	w0, [x1,10]
	ldrb	w1, [x1,12]
	strb	wzr, [x3,20]
	strh	wzr, [x3,22]
	sdiv	w0, w0, w1
	mov	w1, 16
	strh	w0, [x3,18]
	mov	w0, 70
	csel	w0, w0, w1, hi
	mov	w1, 12
	strb	w0, [x3,21]
	add	x0, x3, 12
	bl	js_hash
	ldr	x3, [x29,128]
	str	w0, [x3,8]
	b	.L4023
.L4052:
	mov	w23, w19
	mov	x3, 0
.L4023:
	adrp	x0, .LANCHOR0
	mov	x26, x25
	mov	w9, 0
	add	x11, x0, :lo12:.LANCHOR0
.L4026:
	ldr	w0, [x29,152]
	cmp	w9, w0
	bcs	.L4085
	ldr	w0, [x29,168]
	ldrb	w2, [x11,634]
	add	w8, w9, w0
	add	x0, x11, 4
	ubfx	x8, x8, 2, 16
	add	w1, w8, 1
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L4028
	ldrb	w4, [x11,1]
	lsl	w0, w1, 1
	cmp	w4, wzr
	csel	w0, w0, w1, ne
.L4028:
	ldrb	w1, [x11,516]
	cmp	w1, 9
	beq	.L4084
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L4084:
	str	w0, [x29,208]
	mov	w0, 61424
	str	w0, [x29,212]
	add	x0, x11, 4
	ldrh	w0, [x0,w8,sxtw 1]
	cbnz	w2, .L4032
	mov	w8, w0
	b	.L4033
.L4032:
	ldrb	w1, [x11,1]
	lsl	w0, w8, 1
	cmp	w1, wzr
	csel	w8, w0, w8, ne
.L4033:
	mul	w0, w8, w28
	cbnz	x3, .L4034
	ldr	w1, [x29,196]
	mov	w2, w9
	ldr	w5, [x26]
	mov	w3, w19
	add	w12, w0, w1
	ldr	w6, [x29,208]
	ldr	x0, [x29,136]
	mov	w1, w12
	mov	x4, x26
	mov	w7, 61424
	str	x11, [x29,104]
	str	x8, [x29,112]
	str	x9, [x29,128]
	str	x12, [x29,120]
	bl	printk
	ldr	x12, [x29,120]
	mov	x1, x26
	add	x2, x29, 208
	mov	w0, w12
	bl	fw_flash_page_prog.constprop.29
	ldr	x11, [x29,104]
	ldr	x9, [x29,128]
	ldr	x8, [x29,112]
	ldrb	w0, [x11,634]
	cbnz	w0, .L4035
	ldr	w1, [x29,196]
	add	w2, w8, 1
	str	x11, [x29,120]
	udiv	w1, w1, w28
	bl	id_block_prog_msb_ff_data
	ldr	x11, [x29,120]
	ldr	x9, [x29,128]
.L4035:
	add	w0, w23, 16
	add	x26, x26, 2048
	cmp	w9, w0
	bcc	.L4036
	add	w1, w23, 20
	add	x0, x25, 2048
	cmp	w9, w1
	csel	x26, x26, x0, cs
	b	.L4036
.L4034:
	ldr	w1, [x29,196]
	add	x2, x29, 208
	str	x11, [x29,120]
	add	w0, w0, w1
	mov	x1, x3
	str	x9, [x29,128]
	bl	fw_flash_page_prog.constprop.29
	ldr	x11, [x29,120]
	ldr	x9, [x29,128]
.L4036:
	add	w9, w9, 4
	mov	x3, 0
	uxth	w9, w9
	b	.L4026
.L4085:
	add	x28, x27, :lo12:.LANCHOR0
	ldrb	w0, [x29,156]
	mov	w1, w23
	mov	x2, x24
	strb	w0, [x28,720]
	mov	w0, w20
	bl	id_block_read_data.constprop.30
	cbz	w21, .L4038
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	strb	wzr, [x27,#:lo12:.LANCHOR0]
	strb	wzr, [x28,634]
.L4038:
	lsl	w23, w23, 7
	mov	x0, 0
.L4039:
	cmp	w23, w0
	bls	.L4086
	ldr	w2, [x24,x0,lsl 2]
	add	x0, x0, 1
	add	x1, x25, x0, lsl 2
	ldr	w1, [x1,-4]
	cmp	w2, w1
	beq	.L4039
	mov	x0, x24
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L4019
.L4086:
	ldr	w0, [x29,204]
	add	w0, w0, 1
	str	w0, [x29,204]
	cmp	w0, 5
	bhi	.L4044
.L4019:
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L4042
.L4044:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR0
	ldrb	w2, [x29,172]
	add	x1, x0, :lo12:.LANCHOR0
	strb	w2, [x0,#:lo12:.LANCHOR0]
	cbz	w21, .L4043
	ldrb	w2, [x29,164]
	strb	w2, [x1,634]
.L4043:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 2
	strb	w1, [x0,752]
	mov	w0, 0
	bl	zftl_flash_enter_slc_mode
	mov	x0, x24
	bl	ftl_free
	ldr	w0, [x29,204]
	cmp	w0, wzr
	csetm	w0, eq
.L4082:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 272
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	mov	x2, 0
	adrp	x3, .LANCHOR7
	mov	x5, x0
	add	x3, x3, :lo12:.LANCHOR7
	mov	w0, w2
.L4088:
	cmp	w1, w2
	bls	.L4090
	ldrb	w4, [x5,x2]
	sub	x6, x3, #40
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L4088
.L4090:
	ret
	.size	CRC_32, .-CRC_32
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
	adrp	x20, .LANCHOR6
	bne	.L4092
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L4092
	add	x22, x20, :lo12:.LANCHOR6
	mov	w0, 1
	strb	w0, [x22,268]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,272]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	str	w19, [x22,280]
.L4092:
	add	x23, x20, :lo12:.LANCHOR6
	adrp	x0, .LC315
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC315
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,272]
	bl	printk
	ldrb	w0, [x23,268]
	cbz	w0, .L4091
	ldr	x22, [x23,272]
	cbz	x22, .L4091
	sub	w0, w19, #64
	cmp	w0, 499
	bhi	.L4094
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L4095
.L4094:
	cmp	w19, 563
	bhi	.L4105
.L4095:
	add	x23, x20, :lo12:.LANCHOR6
	ldr	w0, [x23,280]
	cmp	w0, w19
	beq	.L4103
	mov	x0, x22
	strb	wzr, [x23,268]
	bl	ftl_free
	str	xzr, [x23,272]
.L4103:
	add	x20, x20, :lo12:.LANCHOR6
	add	w19, w19, w21
	str	w19, [x20,280]
	b	.L4091
.L4105:
	ldr	w2, [x23,280]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L4106
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
	b	.L4097
.L4106:
	mov	x3, 0
.L4096:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L4098
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L4099
.L4098:
	str	w3, [x0,x3,lsl 2]
.L4099:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L4096
.L4097:
	add	x0, x22, 245760
	mov	w3, 61952
	add	x0, x0, 2048
.L4102:
	ldr	w1, [x0]
	cbz	w1, .L4100
	add	w3, w3, 2048
	lsl	w0, w3, 2
	b	.L4101
.L4100:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L4102
	lsl	w0, w2, 9
.L4101:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR6
	bl	write_idblock
	ldr	x0, [x22,272]
	strb	wzr, [x22,268]
	bl	ftl_free
	str	xzr, [x22,272]
	b	.L4103
.L4091:
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
	bhi	.L4116
	cbnz	w21, .L4116
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L4116:
	adrp	x0, .LANCHOR3+1472
	mov	w1, w19
	mov	w2, w20
	ldr	x0, [x0,#:lo12:.LANCHOR3+1472]
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
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	w19, w1
	mov	x20, x2
	beq	.L4125
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4126
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4127
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4128
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4129
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4130
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L4187
	bl	rknand_dev_flush
	b	.L4221
.L4130:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4132
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4133
	b	.L4187
.L4128:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4134
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4135
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4136
	b	.L4187
.L4135:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4134
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4134
	b	.L4187
.L4126:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4137
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4138
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4139
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L4140
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4141
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4142
	b	.L4187
.L4138:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4143
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4144
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4145
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4146
	b	.L4187
.L4144:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4146
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4143
	b	.L4187
.L4140:
	adrp	x0, .LC316
	add	x0, x0, :lo12:.LC316
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L4147
.L4152:
	mov	x0, -12
	b	.L4124
.L4147:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L4228
	adrp	x0, .LC318
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC318
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L4225
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	id_block_read_data.constprop.30
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L4231
	adrp	x0, .LC319
	add	x0, x0, :lo12:.LC319
.L4223:
	bl	printk
.L4225:
	mov	x0, x19
.L4226:
	bl	ftl_free
.L4224:
	mov	x0, -14
	b	.L4124
.L4139:
	adrp	x0, .LC320
	add	x0, x0, :lo12:.LC320
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L4152
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L4228
	adrp	x0, .LC321
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC321
	bl	printk
	adrp	x0, .LANCHOR6
	add	x22, x0, :lo12:.LANCHOR6
	mov	x20, x0
	ldr	x21, [x22,288]
	cbnz	x21, .L4154
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,288]
	cbz	x0, .L4225
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L4154:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L4225
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L4225
	add	x20, x20, :lo12:.LANCHOR6
	uxtw	x2, w2
	ldr	x1, [x20,288]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L4231:
	mov	x0, x19
	bl	ftl_free
	b	.L4221
.L4142:
	adrp	x0, .LC322
	add	x0, x0, :lo12:.LC322
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L4152
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L4157
.L4228:
	adrp	x0, .LC317
	add	x0, x0, :lo12:.LC317
	b	.L4223
.L4157:
	ldr	w1, [x19]
	adrp	x0, .LC323
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC323
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L4225
	adrp	x20, .LANCHOR6
	add	x20, x20, :lo12:.LANCHOR6
	ldr	x0, [x20,288]
	cbz	x0, .L4225
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L4160
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L4124
.L4160:
	bl	rknand_device_lock
	ldr	x1, [x20,288]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,288]
	bl	ftl_free
	str	xzr, [x20,288]
	b	.L4231
.L4141:
	adrp	x0, .LC324
	add	x0, x0, :lo12:.LC324
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L4152
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L4233
.L4125:
	adrp	x0, .LC325
	add	x0, x0, :lo12:.LC325
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L4152
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L4233
.L4137:
	adrp	x0, .LC326
	add	x0, x0, :lo12:.LC326
	bl	printk
	adrp	x0, .LANCHOR6+296
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR6+296]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L4216:
	bl	rk_copy_to_user
	cbnz	x0, .L4224
	b	.L4221
.L4145:
	adrp	x0, .LC327
	add	x0, x0, :lo12:.LC327
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L4152
	bl	rknand_device_lock
	mov	w1, 264
	mov	w2, 2
	mov	x3, x21
	mov	w0, 16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	x0, x20
	mov	x1, x21
	mov	x2, 1024
.L4233:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L4226
	bl	ftl_free
	b	.L4151
.L4129:
	adrp	x0, .LC328
	add	x0, x0, :lo12:.LC328
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L4165
.L4170:
	adrp	x0, .LC317
	add	x0, x0, :lo12:.LC317
	bl	printk
	b	.L4224
.L4165:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L4166
.L4167:
	mov	x19, -1
	b	.L4151
.L4166:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L4167
	adrp	x21, .LANCHOR6
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR6
	mov	x2, 512
	ldr	x1, [x21,296]
	bl	memcpy
	ldr	w1, [x21,304]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L4168
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L4168:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L4230
.L4132:
	adrp	x0, .LC329
	add	x0, x0, :lo12:.LC329
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L4167
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L4167
	adrp	x1, .LANCHOR6
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR6
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,304]
	cmp	w3, w2
	bne	.L4124
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L4124
	ldr	x19, [x1,296]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L4222
.L4136:
	adrp	x0, .LC330
	add	x0, x0, :lo12:.LC330
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L4167
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L4167
	adrp	x19, .LANCHOR6
	add	x0, x19, :lo12:.LANCHOR6
	ldr	w1, [x0,308]
	cbnz	w1, .L4171
.L4174:
	mov	x0, 0
	b	.L4124
.L4171:
	ldr	x1, [x0,312]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L4172
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,312]
	str	w2, [x1,4]
	ldr	x0, [x0,312]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L4172:
	add	x20, x19, :lo12:.LANCHOR6
	mov	w0, 0
	ldr	x1, [x20,312]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,296]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4173
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,296]
	str	w1, [x0,4]
	ldr	x0, [x20,296]
	str	wzr, [x0,8]
.L4173:
	add	x19, x19, :lo12:.LANCHOR6
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,296]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,308]
	str	wzr, [x19,304]
	b	.L4221
.L4127:
	adrp	x0, .LC331
	add	x0, x0, :lo12:.LC331
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L4167
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L4167
	adrp	x19, .LANCHOR6
	add	x1, x19, :lo12:.LANCHOR6
	ldr	w2, [x1,308]
	cmp	w2, 1
	beq	.L4174
	ldr	x2, [x1,312]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L4175
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,312]
	str	w3, [x2,4]
	ldr	x1, [x1,312]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L4175:
	add	x20, x19, :lo12:.LANCHOR6
	mov	w0, 1
	ldr	x1, [x20,312]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,296]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4176
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,296]
	str	w1, [x0,4]
	ldr	x0, [x20,296]
	str	wzr, [x0,8]
.L4176:
	add	x19, x19, :lo12:.LANCHOR6
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,296]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,308]
	b	.L4221
.L4133:
	adrp	x0, .LC332
	add	x0, x0, :lo12:.LC332
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L4167
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L4167
	adrp	x1, .LANCHOR6
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR6
	uxtw	x2, w2
	add	x1, x1, 320
	b	.L4229
.L4134:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4177
	adrp	x0, .LC333
	add	x0, x0, :lo12:.LC333
	b	.L4219
.L4177:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4179
	adrp	x0, .LC334
	add	x0, x0, :lo12:.LC334
	b	.L4219
.L4179:
	adrp	x0, .LC335
	add	x0, x0, :lo12:.LC335
.L4219:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L4224
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR6
	bne	.L4180
	add	x0, x0, :lo12:.LANCHOR6
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,296]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L4124
	b	.L4224
.L4180:
	add	x20, x0, :lo12:.LANCHOR6
	ldr	w1, [x20,832]
	cmp	w1, 10
	bhi	.L4224
	ldr	x1, [x20,296]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L4181
	cbz	w3, .L4181
	adrp	x0, .LC336
	mov	w1, w2
	add	x0, x0, :lo12:.LC336
	bl	printk
	ldr	w0, [x20,832]
	add	w0, w0, 1
	str	w0, [x20,832]
	b	.L4224
.L4181:
	add	x0, x0, :lo12:.LANCHOR6
	str	wzr, [x0,832]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4182
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L4183
.L4182:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L4183:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L4221
	b	.L4151
.L4146:
	adrp	x0, .LC337
	add	x0, x0, :lo12:.LC337
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4167
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L4167
	mov	w0, 30224
	adrp	x1, .LANCHOR6
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR6
	add	x0, x29, 64
	bne	.L4184
	ldr	x1, [x1,840]
	b	.L4220
.L4184:
	ldr	x1, [x1,848]
.L4220:
	add	x1, x1, 8
.L4229:
	bl	memcpy
.L4230:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L4216
.L4143:
	adrp	x0, .LC338
	add	x0, x0, :lo12:.LC338
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4170
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4167
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L4167
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR6
	add	x19, x19, :lo12:.LANCHOR6
	bne	.L4186
	ldr	x0, [x19,840]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,840]
	b	.L4222
.L4186:
	ldr	x0, [x19,848]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,848]
	mov	w0, 3
.L4222:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L4151
.L4221:
	mov	x19, 0
.L4151:
	adrp	x0, .LC339
	mov	x1, x19
	add	x0, x0, :lo12:.LC339
	bl	printk
	mov	x0, x19
	b	.L4124
.L4187:
	mov	x0, -22
.L4124:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	adrp	x20, .LANCHOR6
	str	x21, [sp,32]
	add	x19, x20, :lo12:.LANCHOR6
	ldr	x1, [x19,240]
	str	w0, [x19,280]
	add	x0, x1, 512
	str	x0, [x19,296]
	add	x0, x1, 1024
	str	x1, [x19,312]
	str	x0, [x19,840]
	add	x1, x1, 1536
	add	x0, x19, 320
	str	x1, [x19,848]
	strb	wzr, [x19,268]
	str	xzr, [x19,272]
	str	xzr, [x19,288]
	bl	ftl_memcpy
	ldr	x0, [x19,312]
	str	wzr, [x19,304]
	str	wzr, [x19,832]
	ldr	w1, [x0,16]
	ldr	w21, [x0,508]
	str	w1, [x19,308]
	cbz	w21, .L4237
	mov	w1, 508
	bl	js_hash
	cmp	w21, w0
	beq	.L4237
	ldr	x0, [x19,312]
	str	wzr, [x19,308]
	str	wzr, [x0,16]
	adrp	x0, .LC340
	add	x0, x0, :lo12:.LC340
	bl	printk
.L4237:
	add	x0, x20, :lo12:.LANCHOR6
	ldr	w1, [x0,308]
	cbz	w1, .L4239
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,304]
.L4239:
	add	x20, x20, :lo12:.LANCHOR6
	mov	w0, 2
	ldr	x1, [x20,840]
	bl	StorageSysDataLoad
	ldr	x1, [x20,848]
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
	adrp	x21, .LANCHOR6
	bl	ftl_malloc
	add	x1, x21, :lo12:.LANCHOR6
	mov	w19, -12
	str	x0, [x1,856]
	cbz	x0, .L4245
	mov	w23, 0
	adrp	x24, .LC341
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC341
.L4249:
	add	x20, x21, :lo12:.LANCHOR6
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,856]
	bl	FlashBootVendorRead
	cbnz	w0, .L4246
	ldr	x3, [x20,856]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,856]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L4247
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L4247
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L4247:
	cbnz	w22, .L4257
	mov	w22, 1
	b	.L4249
.L4257:
	cbz	w19, .L4250
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L4245
	b	.L4246
.L4250:
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
	b	.L4245
.L4246:
	add	x21, x21, :lo12:.LANCHOR6
	mov	w19, -1
	ldr	x0, [x21,856]
	bl	kfree
	str	xzr, [x21,856]
.L4245:
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
	adrp	x3, .LANCHOR6+856
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR6+856]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L4259
	ldrh	w3, [x5,10]
	mov	x4, 0
.L4260:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L4264
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L4260
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
	b	.L4259
.L4264:
	mov	w3, -1
.L4259:
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
	adrp	x0, .LANCHOR6+856
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR6+856]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L4266
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L4267:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L4282
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L4267
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L4268
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L4266
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L4269:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L4283
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
	b	.L4269
.L4283:
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
	b	.L4281
.L4268:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L4281
.L4282:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L4266
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
.L4281:
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
.L4266:
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
	cbz	x0, .L4285
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4287
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4288
	b	.L4299
.L4287:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4299
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L4290
.L4291:
	mov	x20, -1
	b	.L4286
.L4290:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L4291
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L4286
.L4288:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4299
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L4291
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L4291
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L4299
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L4286
.L4299:
	mov	x20, -14
.L4286:
	mov	x0, x19
	bl	kfree
.L4285:
	mov	x0, x20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
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
	.align	3
.LANCHOR1 = . + 0
	.type	__func__.38783, %object
	.size	__func__.38783, 18
__func__.38783:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.38808, %object
	.size	__func__.38808, 23
__func__.38808:
	.string	"_list_update_data_list"
	.zero	1
	.type	__func__.20392, %object
	.size	__func__.20392, 22
__func__.20392:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.20478, %object
	.size	__func__.20478, 11
__func__.20478:
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
	.type	__func__.19935, %object
	.size	__func__.19935, 28
__func__.19935:
	.string	"flash_wait_device_ready_raw"
	.zero	4
	.type	__func__.19999, %object
	.size	__func__.19999, 22
__func__.19999:
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
	.type	__func__.20024, %object
	.size	__func__.20024, 23
__func__.20024:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.19910, %object
	.size	__func__.19910, 26
__func__.19910:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.19921, %object
	.size	__func__.19921, 21
__func__.19921:
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
	.type	__func__.20661, %object
	.size	__func__.20661, 13
__func__.20661:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.20674, %object
	.size	__func__.20674, 10
__func__.20674:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.20688, %object
	.size	__func__.20688, 16
__func__.20688:
	.string	"buf_remove_free"
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 256
zftl_debug_proc_fops:
	.xword	0
	.xword	seq_lseek
	.xword	seq_read
	.xword	zftl_debug_proc_write
	.zero	72
	.xword	zftl_debug_proc_open
	.zero	8
	.xword	single_release
	.zero	128
	.type	__func__.38347, %object
	.size	__func__.38347, 12
__func__.38347:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.38441, %object
	.size	__func__.38441, 19
__func__.38441:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.38990, %object
	.size	__func__.38990, 18
__func__.38990:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.39000, %object
	.size	__func__.39000, 17
__func__.39000:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.39139, %object
	.size	__func__.39139, 23
__func__.39139:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.39179, %object
	.size	__func__.39179, 22
__func__.39179:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.39190, %object
	.size	__func__.39190, 22
__func__.39190:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.38497, %object
	.size	__func__.38497, 16
__func__.38497:
	.string	"gc_free_src_blk"
	.type	__func__.38098, %object
	.size	__func__.38098, 14
__func__.38098:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.38143, %object
	.size	__func__.38143, 18
__func__.38143:
	.string	"zftl_add_read_buf"
	.zero	6
	.type	__func__.38948, %object
	.size	__func__.38948, 15
__func__.38948:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR4 = . + 0
	.type	__func__.39576, %object
	.size	__func__.39576, 17
__func__.39576:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.39604, %object
	.size	__func__.39604, 21
__func__.39604:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.20382, %object
	.size	__func__.20382, 20
__func__.20382:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.38033, %object
	.size	__func__.38033, 17
__func__.38033:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.39536, %object
	.size	__func__.39536, 13
__func__.39536:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.20712, %object
	.size	__func__.20712, 21
__func__.20712:
	.string	"flash_info_data_init"
	.zero	3
	.type	__func__.19852, %object
	.size	__func__.19852, 19
__func__.19852:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.19959, %object
	.size	__func__.19959, 26
__func__.19959:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.19970, %object
	.size	__func__.19970, 29
__func__.19970:
	.string	"flash_start_3d_mlc_page_prog"
	.zero	3
	.type	__func__.19988, %object
	.size	__func__.19988, 22
__func__.19988:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.20012, %object
	.size	__func__.20012, 31
__func__.20012:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.20037, %object
	.size	__func__.20037, 25
__func__.20037:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.39375, %object
	.size	__func__.39375, 31
__func__.39375:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.39429, %object
	.size	__func__.39429, 15
__func__.39429:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.39456, %object
	.size	__func__.39456, 15
__func__.39456:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.19894, %object
	.size	__func__.19894, 19
__func__.19894:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.39253, %object
	.size	__func__.39253, 14
__func__.39253:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.38916, %object
	.size	__func__.38916, 15
__func__.38916:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.39215, %object
	.size	__func__.39215, 19
__func__.39215:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.39227, %object
	.size	__func__.39227, 18
__func__.39227:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.39106, %object
	.size	__func__.39106, 23
__func__.39106:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.39586, %object
	.size	__func__.39586, 14
__func__.39586:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.20734, %object
	.size	__func__.20734, 17
__func__.20734:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.20701, %object
	.size	__func__.20701, 20
__func__.20701:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.20437, %object
	.size	__func__.20437, 16
__func__.20437:
	.string	"nand_flash_init"
	.type	__func__.39082, %object
	.size	__func__.39082, 16
__func__.39082:
	.string	"ftl_sysblk_dump"
	.type	__func__.39520, %object
	.size	__func__.39520, 16
__func__.39520:
	.string	"load_l2p_region"
	.type	__func__.39560, %object
	.size	__func__.39560, 6
__func__.39560:
	.string	"pm_gc"
	.zero	2
	.type	__func__.38428, %object
	.size	__func__.38428, 22
__func__.38428:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.38541, %object
	.size	__func__.38541, 16
__func__.38541:
	.string	"gc_scan_src_blk"
	.type	__func__.38599, %object
	.size	__func__.38599, 20
__func__.38599:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.38662, %object
	.size	__func__.38662, 18
__func__.38662:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.39058, %object
	.size	__func__.39058, 14
__func__.39058:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.38176, %object
	.size	__func__.38176, 10
__func__.38176:
	.string	"zftl_read"
	.zero	6
	.type	__func__.38394, %object
	.size	__func__.38394, 21
__func__.38394:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.39160, %object
	.size	__func__.39160, 19
__func__.39160:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.38080, %object
	.size	__func__.38080, 17
__func__.38080:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.38470, %object
	.size	__func__.38470, 16
__func__.38470:
	.string	"gc_do_copy_back"
	.type	__func__.38690, %object
	.size	__func__.38690, 11
__func__.38690:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.38207, %object
	.size	__func__.38207, 13
__func__.38207:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.20 20200218"
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
	.string	"g_flash_slc_mode: %x\n"
.LC18:
	.string	"OptMode: %x\n"
.LC19:
	.string	"g_nand_max_die: %x\n"
.LC20:
	.string	"Cache read enable: %x\n"
.LC21:
	.string	"Cache random read enable: %x\n"
.LC22:
	.string	"Cache prog enable: %x\n"
.LC23:
	.string	"multi read enable: %x\n"
.LC24:
	.string	"multi prog enable: %x\n"
.LC25:
	.string	"interleave enable: %x\n"
.LC26:
	.string	"read retry enable: %x\n"
.LC27:
	.string	"randomizer enable: %x\n"
.LC28:
	.string	"SDR enable: %x\n"
.LC29:
	.string	"ONFI enable: %x\n"
.LC30:
	.string	"TOGGLE enable: %x\n"
.LC31:
	.string	"g_flash_slc_mode: %x %x\n"
.LC32:
	.string	"MultiPlaneProgCmd: %x %x\n"
.LC33:
	.string	"MultiPlaneReadCmd: %x %x\n"
.LC34:
	.string	"g_flash_toggle_mode_en: %x\n"
.LC35:
	.string	"...%s enter... %p\n"
.LC36:
	.string	"0:%x %x %x %x %x\n"
.LC37:
	.string	"g_nandc_ver...%d\n"
.LC38:
	.string	"nand sdr mode %x\n"
.LC39:
	.string	"nand ddr mode %x\n"
.LC40:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC41:
	.string	"otp:%x %x %x %x\n"
.LC42:
	.string	"bad block test:%x %x\n"
.LC43:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC44:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC45:
	.string	"flash_erase_block %x %x %x\n"
.LC46:
	.string	"flash_erase_block %d block = %x status = %x\n"
.LC47:
	.string	"erase done: %x\n"
.LC48:
	.string	"flash_mask_bad_block %d %d\n"
.LC49:
	.string	"zftl_debug"
.LC50:
	.string	"FLASH ID: %x %x %x %x %x %x\n"
.LC51:
	.string	"density: %d MB\n"
.LC52:
	.string	"device density: %d MB\n"
.LC53:
	.string	"FTL INFO:\n"
.LC54:
	.string	"max_lpn = 0x%x\n"
.LC55:
	.string	"density = 0x%x\n"
.LC56:
	.string	"slc vpn = 0x%x\n"
.LC57:
	.string	"xlc vpn = 0x%x\n"
.LC58:
	.string	"free slc blk = 0x%x\n"
.LC59:
	.string	"free xlc blk = 0x%x\n"
.LC60:
	.string	"free mix blk = 0x%x\n"
.LC61:
	.string	"slc data blk = 0x%x\n"
.LC62:
	.string	"slc cache blk = 0x%x\n"
.LC63:
	.string	"xlc data blk = 0x%x\n"
.LC64:
	.string	"free buf = %d, %d, %d\n"
.LC65:
	.string	"bad blk = %d %d\n"
.LC66:
	.string	"TBW = %d MB\n"
.LC67:
	.string	"TBR = %d MB\n"
.LC68:
	.string	"POC = %d\n"
.LC69:
	.string	"PLC = %d\n"
.LC70:
	.string	"sys run time = %d S\n"
.LC71:
	.string	"slc mode = %d\n"
.LC72:
	.string	"prog err = %d\n"
.LC73:
	.string	"read err = %d\n"
.LC74:
	.string	"GC XLC page = %d\n"
.LC75:
	.string	"GC SLC page = %d\n"
.LC76:
	.string	"discard page = 0x%x\n"
.LC77:
	.string	"version = %d\n"
.LC78:
	.string	"acblk = 0x%x %d %d\n"
.LC79:
	.string	"tmblk = 0x%x %d %d\n"
.LC80:
	.string	"gcblk = 0x%x %d %d\n"
.LC81:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC82:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC83:
	.string	"gc free blk th = %d\n"
.LC84:
	.string	"gc vpn th = %d %d %d %d %d\n"
.LC85:
	.string	"swl blk = %x %x %x %x\n"
.LC86:
	.string	"rf info = %x %x %x %x %x\n"
.LC87:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\n"
.LC88:
	.string	"gc_add_sblk = %d, %d, %d\n"
.LC89:
	.string	"gc_add_sblk = %d, %d, %d,last update:%d, %d\n"
.LC90:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC91:
	.string	"gc_mark_bad_ppa %d %x %x\n"
.LC92:
	.string	"status: %x, ppa: %x\n"
.LC93:
	.string	"gc_write_completed: %x  %x  %d %x\n"
.LC94:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC95:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC96:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC97:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC98:
	.string	"swl_tlc_free_mini_ec_blk alloc sblk %x\n"
.LC99:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC100:
	.string	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\n"
.LC101:
	.string	"swl_slc_free_mini_ec_blk alloc sblk %x\n"
.LC102:
	.string	"list count:%p %d\n"
.LC103:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC104:
	.string	"ftl_vpn_decrement %x = %d, %d\n"
.LC105:
	.string	"mask bad block:cs %x %x block: %x %x\n"
.LC106:
	.string	"gc_free_bad_sblk 0x%x\n"
.LC107:
	.string	"swl_slc_free_mini_ec_blk sblk %x\n"
.LC108:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC109:
	.string	"gc_free_src_blk %x, %d\n"
.LC110:
	.string	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC111:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC112:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC113:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC114:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC115:
	.string	"swl : %x %x %x %x %x %x\n"
.LC116:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC117:
	.string	"ftl re prog: lpa = %x, ppa= %x\n"
.LC118:
	.string	"alloc sblk %x %d\n"
.LC119:
	.string	"dump_sblk_queue: %d\n"
.LC120:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC121:
	.string	"blk %x is bad block\n"
.LC122:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC123:
	.string	"%s %d %d\n"
.LC124:
	.string	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC125:
	.string	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC126:
	.string	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC127:
	.string	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC128:
	.string	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC129:
	.string	"gc_static_wearleveling: slc blk: %x, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC130:
	.string	"gc_static_wearleveling: min slc ec: %x, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC131:
	.string	"swl add tlc gc = %x, %d, %d, %d, %d, %d\n"
.LC132:
	.string	"swl add slc gc  = %x, %d, %d, %d, %d, %d\n"
.LC133:
	.string	"free blk vpn error: %x %x\n"
.LC134:
	.string	"GC PM block %x %x %x %d\n"
.LC135:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC136:
	.string	"...%d @ %s\n"
.LC137:
	.string	"rk_ftl_de_init %x\n"
.LC138:
	.string	"\0013"
.LC139:
	.string	"otp error! %d"
.LC140:
	.string	"rr"
.LC141:
	.string	"flash_abort_clear = %d\n"
.LC142:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC143:
	.string	"nandc:"
.LC144:
	.string	"%d flReg.d32=%x %x\n"
.LC145:
	.string	"nandc_xfer_done read error %x\n"
.LC146:
	.string	"dqs data abort %x\n"
.LC147:
	.string	"dqs data timeout %x\n"
.LC148:
	.string	"xfer error %x\n"
.LC149:
	.string	"flash_read_page %x %x %x\n"
.LC150:
	.string	"MT %d row=%x,last status %d,status = %d\n"
.LC151:
	.string	"MT RR %d row=%x,count %d,status=%d\n"
.LC152:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC153:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC154:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC155:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC156:
	.string	"%d flash_ddr_tuning_read %x ecc=%d\n"
.LC157:
	.string	"sync para %d\n"
.LC158:
	.string	"DDR mode Read error %x %x\n"
.LC159:
	.string	"SDR mode Read %x %x ecc:%x\n"
.LC160:
	.string	"flash_read_page_en %x %x %x %x\n"
.LC161:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC162:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC163:
	.string	"read page: %x %x %x %x\n"
.LC164:
	.string	"flash_prog_page %x %x %x\n"
.LC165:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC166:
	.string	"sblk_3d_tlc_dump_prog wl_addr= %x ppa = %x ppa = %x\n"
.LC167:
	.string	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\n"
.LC168:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC169:
	.string	"read: %x %x %x %x\n"
.LC170:
	.string	"0set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC171:
	.string	"prog end %x %x error_ecc %d %d\n"
.LC172:
	.string	"1set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC173:
	.string	"dp prog end %x %x error_ecc %d %d\n"
.LC174:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC175:
	.string	"flash_prog_page_en:%x %x %x\n"
.LC176:
	.string	"w d:"
.LC177:
	.string	"w s:"
.LC178:
	.string	"spare"
.LC179:
	.string	"data"
.LC180:
	.string	"write error: %x\n"
.LC181:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC182:
	.string	"%d %x @%d %x\n"
.LC183:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC184:
	.string	"ftl info hash %x error\n"
.LC185:
	.string	"ink flag: %x\n"
.LC186:
	.string	"%s %d %d %x %x\n"
.LC187:
	.string	"ext info hash %x error\n"
.LC188:
	.string	"%s %x %x %x\n"
.LC189:
	.string	"saved_active_page  = %x\n"
.LC190:
	.string	"saved_active_plane = %x\n"
.LC191:
	.string	"sblk = %x\n"
.LC192:
	.string	"phy_blk = %x %x\n"
.LC193:
	.string	"num_planes = %x\n"
.LC194:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC195:
	.string	"dump_write_lpa = %x %x %x %x\n"
.LC196:
	.string	"dump write new ppa = %x, last ppa = %x lpa = %x\n"
.LC197:
	.string	"dump write = %x %x %x\n"
.LC198:
	.string	"dump write hash update = %x %x %x\n"
.LC199:
	.string	"pm_write_page write error: %x\n"
.LC200:
	.string	"ftl_sblk_dump_write = %x %d %d %d %d\n"
.LC201:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC202:
	.string	"ftl_sblk_dump_write2 = %x %d %d %d\n"
.LC203:
	.string	"ftl_sblk_dump_write = %x %x\n"
.LC204:
	.string	"ftl_sblk_dump_write done = %x\n"
.LC205:
	.string	"ftl_ink_check_sblk = %x %d %d\n"
.LC206:
	.string	"ftl_ink_check_sblk = %x %d %d end\n"
.LC207:
	.string	"finfo:"
.LC208:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC209:
	.string	"sys_info_flush error:%x\n"
.LC210:
	.string	"...%d @ %s %d %p\n"
.LC211:
	.string	"no sys info %x\n"
.LC212:
	.string	"l2p:"
.LC213:
	.string	"free_buf_count: %d\n"
.LC214:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC215:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC216:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC217:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC218:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC219:
	.string	"lpa:"
.LC220:
	.string	"vpn:"
.LC221:
	.string	"sblk:"
.LC222:
	.string	"lpa_hash:"
.LC223:
	.string	"lpa_hash_index:"
.LC224:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC225:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC226:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC227:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC228:
	.string	"pm_ppa:"
.LC229:
	.string	"data:"
.LC230:
	.string	"spare:"
.LC231:
	.string	"pm_init posr %x %x %x\n"
.LC232:
	.string	"pm_init recovery %x %x %x\n"
.LC233:
	.string	"pm_init hash %x error\n"
.LC234:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC235:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC236:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC237:
	.string	"gc_recovery: %x vpn = %x\n"
.LC238:
	.string	"_c_user_data_density := %d\n"
.LC239:
	.string	"_c_totle_phy_density := %d\n"
.LC240:
	.string	"_c_totle_log_page := %d\n"
.LC241:
	.string	"_c_totle_data_density := %d\n"
.LC242:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC243:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC244:
	.string	"_c_max_pm_sblk := %d\n"
.LC245:
	.string	"_min_slc_super_block := %d\n"
.LC246:
	.string	"_max_xlc_super_block := %d\n"
.LC247:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC248:
	.string	"flash info size: %d %d %d\n"
.LC249:
	.string	"ftl_init %x\n"
.LC250:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC251:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC252:
	.string	"lpa: %x %x %x\n"
.LC253:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC254:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC255:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC256:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC257:
	.string	"ftl_sblk_dump = %x %d %d %d %d\n"
.LC258:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC259:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC260:
	.string	"index= %x, lpa=%x\n"
.LC261:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC262:
	.string	"ftl_read %x %x %x\n"
.LC263:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC264:
	.string	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC265:
	.string	"zftl debug cmd: %s\n"
.LC266:
	.string	"cmd:"
.LC267:
	.string	"dumpl2p"
.LC268:
	.string	"pm l2p:"
.LC269:
	.string	"pm blk:"
.LC270:
	.string	"dumppm:"
.LC271:
	.string	"p_cmd: %s\n"
.LC272:
	.string	"pm ram = %x, %x\n"
.LC273:
	.string	"ram:"
.LC274:
	.string	"pm:"
.LC275:
	.string	"dumpsys"
.LC276:
	.string	"dumplist:"
.LC277:
	.string	"vpncheck"
.LC278:
	.string	"dumpppa:"
.LC279:
	.string	"dumpblk:"
.LC280:
	.string	"setzdebug:"
.LC281:
	.string	"lpa2ppa:"
.LC282:
	.string	"lpa: %x--> ppa: %x\n"
.LC283:
	.string	"help:\n"
.LC284:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC285:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC286:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC287:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC288:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC289:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC290:
	.string	"7. echo dumplist > /proc/zftl_debug\n"
.LC291:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC292:
	.string	"gc_lpa:"
.LC293:
	.string	"gc_ppa:"
.LC294:
	.string	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC295:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC296:
	.string	"ftl_update_l2p_map"
.LC297:
	.string	"lpa_tbl:"
.LC298:
	.string	"sblk %x vpn: %d %d\n"
.LC299:
	.string	"error gc_add_sblk: %x\n"
.LC300:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC301:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC302:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC303:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x %x %d\n"
.LC304:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC305:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC306:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC307:
	.string	"gc free %x, %d\n"
.LC308:
	.string	"ftlwrite %x %x %x %x\n"
.LC309:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC310:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC311:
	.string	"write_idblock fix data %x %x\n"
.LC312:
	.string	"idblk:"
.LC313:
	.string	"write_idblock totle_sec %x %x\n"
.LC314:
	.string	"prog page: %x %x %x, %p %x %x %x\n"
.LC315:
	.string	"wl_lba %p %x %x %x\n"
.LC316:
	.string	"READ_SECTOR_IO\n"
.LC317:
	.string	"rk_copy_from_user error\n"
.LC318:
	.string	"READ_SECTOR_IO %x %x\n"
.LC319:
	.string	"rk_copy_to_user error\n"
.LC320:
	.string	"WRITE_SECTOR_IO\n"
.LC321:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC322:
	.string	"END_WRITE_SECTOR_IO\n"
.LC323:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC324:
	.string	"GET_FLASH_INFO_IO\n"
.LC325:
	.string	"GET_BAD_BLOCK_IO\n"
.LC326:
	.string	"GET_LOCK_FLAG_IO\n"
.LC327:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC328:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC329:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC330:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC331:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC332:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC333:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC334:
	.string	"RKNAND_LOADER_STATUS\n"
.LC335:
	.string	"RKNAND_LOADER_LOCK\n"
.LC336:
	.string	"LockKey not match %d\n"
.LC337:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC338:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC339:
	.string	"return ret = %lx\n"
.LC340:
	.string	"secureBootEn check error\n"
.LC341:
	.string	"\0013vendor storage %x,%x,%x\n"
	.data
	.align	3
.LANCHOR2 = . + 0
.LANCHOR7 = . + 4352
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
	.size	zftl_nand_flash_para_tbl, 1440
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
	.byte	4
	.byte	0
	.hword	1024
	.byte	2
	.byte	-38
	.byte	-33
	.byte	0
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
	.byte	4
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
	.byte	4
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
.LANCHOR3 = . + 4352
.LANCHOR5 = . + 21760
.LANCHOR6 = . + 39168
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
	.size	g_lsb_page_tbl, 512
g_lsb_page_tbl:
	.zero	512
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
	.type	_c_ftl_cs_bits, %object
	.size	_c_ftl_cs_bits, 1
_c_ftl_cs_bits:
	.zero	1
	.zero	7
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 4
g_flash_cur_mode:
	.zero	4
	.type	g_flash_micron_3d_tlc_flag, %object
	.size	g_flash_micron_3d_tlc_flag, 1
g_flash_micron_3d_tlc_flag:
	.zero	1
	.zero	3
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
	.zero	6
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
	.size	g_gc_info, 600
g_gc_info:
	.zero	600
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
	.type	sblk_gc_write_completed_queue_head, %object
	.size	sblk_gc_write_completed_queue_head, 1
sblk_gc_write_completed_queue_head:
	.zero	1
	.type	_c_ftl_nand_type, %object
	.size	_c_ftl_nand_type, 1
_c_ftl_nand_type:
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
	.type	_c_ftl_nand_planes_num, %object
	.size	_c_ftl_nand_planes_num, 1
_c_ftl_nand_planes_num:
	.zero	1
	.zero	3
	.type	gp_data_xlc_data_head, %object
	.size	gp_data_xlc_data_head, 8
gp_data_xlc_data_head:
	.zero	8
	.type	gp_data_slc_cache_head, %object
	.size	gp_data_slc_cache_head, 8
gp_data_slc_cache_head:
	.zero	8
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
	.type	gc_slc_data_index, %object
	.size	gc_slc_data_index, 2
gc_slc_data_index:
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
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.zero	2
	.type	_max_slc_super_block, %object
	.size	_max_slc_super_block, 2
_max_slc_super_block:
	.zero	2
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.zero	2
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.zero	1
	.type	read_buf_head, %object
	.size	read_buf_head, 1
read_buf_head:
	.zero	1
	.type	read_buf_count, %object
	.size	read_buf_count, 1
read_buf_count:
	.zero	1
	.type	sblk_read_completed_queue_head, %object
	.size	sblk_read_completed_queue_head, 1
sblk_read_completed_queue_head:
	.zero	1
	.zero	2
	.type	pm_gc_enable, %object
	.size	pm_gc_enable, 4
pm_gc_enable:
	.zero	4
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
	.size	g_msb_page_tbl, 1024
g_msb_page_tbl:
	.zero	1024
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
	.zero	60
	.type	ftl_info_data_buffer, %object
	.size	ftl_info_data_buffer, 16384
ftl_info_data_buffer:
	.zero	16384
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
	.zero	61
	.type	ftl_ext_info_data_buffer, %object
	.size	ftl_ext_info_data_buffer, 16384
ftl_ext_info_data_buffer:
	.zero	16384
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
	.type	g_flash_micron_3d_tlc_b05a, %object
	.size	g_flash_micron_3d_tlc_b05a, 1
g_flash_micron_3d_tlc_b05a:
	.zero	1
	.type	g_flash_micron_3d_tlc_b16a, %object
	.size	g_flash_micron_3d_tlc_b16a, 1
g_flash_micron_3d_tlc_b16a:
	.zero	1
	.zero	1
	.type	_c_ftl_byte_pre_page, %object
	.size	_c_ftl_byte_pre_page, 2
_c_ftl_byte_pre_page:
	.zero	2
	.type	pm_last_load_ram_id, %object
	.size	pm_last_load_ram_id, 1
pm_last_load_ram_id:
	.zero	1
	.zero	1
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
	.type	rk_zftl_enable, %object
	.size	rk_zftl_enable, 1
rk_zftl_enable:
	.zero	1
	.zero	1
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 8
gLoaderBootInfo:
	.zero	8
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 8
RK29_NANDC1_REG_BASE:
	.zero	8
	.type	_last_read_time, %object
	.size	_last_read_time, 4
_last_read_time:
	.zero	4
	.type	_last_write_time, %object
	.size	_last_write_time, 4
_last_write_time:
	.zero	4
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
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 8
g_idb_buffer:
	.zero	8
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
	.type	g_vendor, %object
	.size	g_vendor, 8
g_vendor:
	.zero	8
	.type	ftl_low_format_cur_blk, %object
	.size	ftl_low_format_cur_blk, 2
ftl_low_format_cur_blk:
	.zero	2
	.zero	30
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 16384
ftl_tmp_buffer:
	.zero	16384
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
