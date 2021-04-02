/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-01-07
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
	bne	.L40
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
	b	.L42
.L40:
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
.L42:
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
	bne	.L44
	ldr	w3, [x0,292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,292]
	ldr	w2, [x0,288]
	bic	w1, w2, w1
	str	w1, [x0,288]
	b	.L43
.L44:
	ldr	w3, [x0,368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
.L43:
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
	bls	.L46
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
	cbz	x2, .L53
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
.L51:
	ubfiz	x15, x3, 2, 16
	ldr	w6, [x9,x15]
	ubfx	x7, x6, 11, 8
	ldrh	w6, [x9,x15]
	ubfx	x6, x6, 0, 11
	madd	w6, w13, w7, w6
	cmp	w4, w6, uxth
	bls	.L49
	ldrh	w6, [x2]
	cmp	w6, w16
	bne	.L50
	strh	w3, [x5,2]
	strh	w1, [x2]
	b	.L46
.L50:
	umull	x2, w6, w8
	mov	w3, w6
	add	x2, x14, x2
	b	.L51
.L49:
	ldrh	w4, [x2,2]
	strh	w4, [x5,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L52
	strh	w1, [x2,2]
.L53:
	str	x5, [x0]
	b	.L46
.L52:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,1040]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L46:
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
	bls	.L54
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
	cbz	x2, .L72
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
	cbz	w5, .L58
	madd	w3, w16, w4, w3
	add	w5, w5, w3, uxth
.L58:
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
.L64:
	add	w9, w9, 1
	cmp	w1, w3
	uxth	w9, w9
	beq	.L54
	cmp	w9, w20
	bhi	.L54
	uxtw	x12, w3
	ldrh	w18, [x2,4]
	ldrh	w4, [x15,x12,lsl 1]
	lsl	x12, x12, 2
	ldr	w10, [x13,x12]
	mul	w4, w4, w18
	ubfx	x11, x10, 11, 8
	ldrh	w10, [x13,x12]
	and	w10, w10, 2047
	cbz	w4, .L61
	madd	w10, w16, w11, w10
	add	w4, w4, w10, uxth
.L61:
	cmp	w4, w5
	bcs	.L62
	cbz	w18, .L62
	ldrh	w4, [x2]
	cmp	w4, w21
	bne	.L63
	strh	w3, [x7,2]
	strh	w1, [x2]
	b	.L54
.L63:
	umull	x2, w4, w19
	mov	w3, w4
	add	x2, x17, x2
	b	.L64
.L62:
	ldrh	w4, [x2,2]
	strh	w4, [x7,2]
	strh	w3, [x14,x8]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L65
	strh	w1, [x2,2]
.L72:
	str	x7, [x0]
	b	.L54
.L65:
	ldrh	w0, [x2,2]
	add	x6, x6, :lo12:.LANCHOR0
	mov	w3, 6
	umull	x0, w0, w3
	ldr	x3, [x6,1040]
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L54:
	ldp	x19, x20, [sp]
	ldr	x21, [sp,16]
	add	sp, sp, 32
	ret
	.size	_insert_data_list, .-_insert_data_list
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
	beq	.L73
	ldrh	w1, [x3,48]
	cmp	w1, w19
	beq	.L73
	ldrh	w1, [x3,80]
	cmp	w1, w19
	beq	.L73
	mov	w23, 6
	ldr	x25, [x4,1040]
	ldr	x1, [x0]
	umull	x23, w19, w23
	add	x24, x25, x23
	cmp	x24, x1
	beq	.L73
	mov	x21, x0
	ldrh	w0, [x24,2]
	mov	w1, 65535
	mov	x22, x2
	cmp	w0, w1
	bne	.L77
	ldrh	w1, [x25,x23]
	cmp	w1, w0
	bne	.L77
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 273
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L77:
	ldrh	w3, [x24,2]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L78
	ldrh	w0, [x25,x23]
	cmp	w0, w3
	beq	.L73
.L78:
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
	bcs	.L73
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_list_remove_node
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_insert_data_list
.L73:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	_list_update_data_list, .-_list_update_data_list
	.section	.text.unlikely
	.align	2
	.type	rk_simple_strtoull.constprop.34, %function
rk_simple_strtoull.constprop.34:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	mov	x20, x0
	stp	x21, x22, [sp,32]
	mov	x23, x1
	ldrb	w0, [x0]
	cmp	w0, 48
	bne	.L94
	ldrb	w0, [x20,1]
	add	x19, x20, 1
	mov	w21, 8
	orr	w0, w0, 32
	uxtb	w0, w0
	cmp	w0, 120
	bne	.L84
	ldrb	w0, [x20,2]
	bl	isxdigit
	cbz	w0, .L84
	add	x19, x20, 2
	mov	w21, 16
	b	.L84
.L94:
	mov	x19, x20
	mov	w21, 10
.L84:
	mov	w22, 0
.L85:
	ldrb	w20, [x19]
	mov	w0, w20
	bl	isxdigit
	cbnz	w0, .L86
.L92:
	cbz	x23, .L98
	str	x19, [x23]
	b	.L98
.L86:
	sub	w2, w20, #48
	cmp	w2, 9
	bhi	.L99
.L93:
	cmp	w2, w21
	bcs	.L92
	madd	w22, w22, w21, w2
	add	x19, x19, 1
	b	.L85
.L99:
	orr	w20, w20, 32
	sub	w2, w20, #87
	b	.L93
.L98:
	mov	w0, w22
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	rk_simple_strtoull.constprop.34, .-rk_simple_strtoull.constprop.34
	.text
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
	ldr	x2, [x3,1128]
	ldr	x19, [x3,1048]
	add	x23, x2, 128
	add	x21, x2, 112
	ldrb	w22, [x2,113]
	ldrb	w2, [x2,112]
	mul	w1, w1, w22
	cmp	w2, 8
	sxtw	x1, w1
	beq	.L101
	sxtw	x2, w0
	mov	x3, 160
	madd	x2, x2, x3, x1
	add	x2, x2, 32
	add	x21, x21, x2
	b	.L102
.L101:
	add	x1, x1, 32
	add	x21, x21, x1
.L102:
	ubfiz	x0, x0, 8, 8
	mov	x20, 0
	add	x19, x19, x0
	mov	w0, 54
	str	w0, [x19,2056]
.L103:
	cmp	w22, w20, uxtb
	bls	.L105
	ldrb	w0, [x23,x20]
	str	w0, [x19,2052]
	mov	x0, 600
	bl	__const_udelay
	ldrsb	w0, [x21,x20]
	add	x20, x20, 1
	str	w0, [x19,2048]
	b	.L103
.L105:
	mov	w0, 22
	str	w0, [x19,2056]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	hynix_set_rr_para, .-hynix_set_rr_para
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
	mov	x0, 600
	bl	__const_udelay
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
	add	x23, x2, w1, sxtw
	add	x24, x2, 48
	add	x21, x2, 96
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
.L108:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,1137]
	cmp	w0, w20
	bls	.L114
	add	x0, x19, 48
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x25,1136]
	cmp	w0, 34
	bne	.L109
	ldrsb	w0, [x24,x20]
	b	.L113
.L109:
	cmp	w0, 35
	bne	.L111
	ldrsb	w0, [x21,x20]
	b	.L113
.L111:
	ldrsb	w0, [x23,192]
.L113:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L108
.L114:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
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
	ldrb	w1, [x19,1136]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 7
	bhi	.L115
	ldr	x1, [x19,1128]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L115
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,1128]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L115:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L121
	adrp	x1, .LANCHOR1
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 200
	bl	printk
.L121:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L122
	adrp	x0, .LANCHOR0+1144
	ldr	x6, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	ldrb	w1, [x6,1]
	ldrb	w2, [x6,2]
	ldrb	w3, [x6,3]
	ldrb	w4, [x6,4]
	ldrb	w5, [x6,5]
	ldrb	w6, [x6,6]
	bl	printk
.L122:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L123
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,8]
	bl	printk
.L123:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L124
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrb	w1, [x1,9]
	bl	printk
.L124:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L125
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrh	w1, [x1,10]
	bl	printk
.L125:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L126
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,12]
	bl	printk
.L126:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L127
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,13]
	bl	printk
.L127:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L128
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrh	w1, [x1,14]
	bl	printk
.L128:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	ldrb	w1, [x1,23]
	bl	printk
.L129:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,18]
	bl	printk
.L130:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,19]
	bl	printk
.L131:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L132
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrb	w1, [x1,20]
	bl	printk
.L132:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L133
	adrp	x1, .LANCHOR0+1152
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1152]
	bl	printk
.L133:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L134
	adrp	x0, .LANCHOR0+1144
	ldr	x1, [x0,#:lo12:.LANCHOR0+1144]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,22]
	bl	printk
.L134:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L135
	adrp	x1, .LANCHOR0+1153
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1153]
	bl	printk
.L135:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L136
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	and	w1, w1, 1
	bl	printk
.L136:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L137
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	ubfx	x1, x1, 1, 1
	bl	printk
.L137:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L138
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 2, 1
	bl	printk
.L138:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L139
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 3, 1
	bl	printk
.L139:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L140
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 4, 1
	bl	printk
.L140:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L141
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 5, 1
	bl	printk
.L141:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L142
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 6, 1
	bl	printk
.L142:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L143
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 7, 1
	bl	printk
.L143:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L144
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0,17]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	and	w1, w1, 1
	bl	printk
.L144:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L145
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	ubfx	x1, x1, 9, 1
	bl	printk
.L145:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L146
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0,16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 10, 1
	bl	printk
.L146:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L147
	adrp	x1, .LANCHOR0
	adrp	x0, .LC30
	add	x2, x1, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC30
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldrb	w2, [x2,1154]
	bl	printk
.L147:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L148
	adrp	x2, .LANCHOR0
	adrp	x0, .LC31
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC31
	add	x2, x2, 1160
	ldrb	w1, [x2,10]
	ldrb	w2, [x2,11]
	bl	printk
.L148:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L149
	adrp	x2, .LANCHOR0
	adrp	x0, .LC32
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC32
	add	x2, x2, 1160
	ldrb	w1, [x2,8]
	ldrb	w2, [x2,9]
	bl	printk
.L149:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L120
	adrp	x1, .LANCHOR0+1192
	adrp	x0, .LC33
	add	x0, x0, :lo12:.LC33
	ldrb	w1, [x1,#:lo12:.LANCHOR0+1192]
	bl	printk
.L120:
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
	tbz	x0, 12, .L242
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 224
	mov	x2, x22
	bl	printk
.L242:
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
	bne	.L243
	mov	w1, 8
	strb	w1, [x0,1028]
.L243:
	ldr	w1, [x22,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L244
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 9
	strb	w1, [x0,1028]
.L244:
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,1028]
	cmp	w0, 9
	bne	.L245
	mov	w0, 1
	strb	w0, [x19,1193]
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
	tbz	x1, 12, .L247
	ldr	w1, [x0]
	ldr	w2, [x0,8]
	ldr	w3, [x0,80]
	ldr	w4, [x0,84]
	ldr	w5, [x0,88]
	b	.L259
.L245:
	ldr	w0, [x29,56]
	mov	w1, 1
	strb	wzr, [x19,1193]
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
	str	x0, [x19,1200]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L247
	ldr	x0, [x19,1048]
	ldr	w1, [x0]
	ldr	w2, [x0,344]
	ldr	w3, [x0,304]
	ldr	w4, [x0,308]
	ldr	w5, [x0,312]
.L259:
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L247:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x1,1240]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	strh	wzr, [x1,1242]
	strb	wzr, [x1,1244]
	tbz	x0, 12, .L241
	adrp	x0, .LC36
	ldrb	w1, [x1,1028]
	add	x0, x0, :lo12:.LC36
	bl	printk
.L241:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	nandc_init, .-nandc_init
	.align	2
	.global	timer_delay_ns
	.type	timer_delay_ns, %function
timer_delay_ns:
	stp	x29, x30, [sp, -16]!
	uxtw	x0, w0
	add	x29, sp, 0
	bl	__ndelay
	ldp	x29, x30, [sp], 16
	ret
	.size	timer_delay_ns, .-timer_delay_ns
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
	bne	.L262
	str	w0, [x1,80]
	b	.L261
.L262:
	str	w0, [x1,304]
.L261:
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
	bne	.L265
	ldr	w0, [x0,80]
	b	.L267
.L265:
	ldr	w0, [x0,304]
.L267:
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
	beq	.L269
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
	bne	.L271
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L272
.L271:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L272
.L269:
	and	w1, w1, -8193
.L272:
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
	ldrb	w0, [x0,1153]
	cmp	w0, w21
	bhi	.L278
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 812
	add	x1, x1, 240
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L278:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x0, w21, sxtw
	ldrb	w21, [x21,1248]
	lsr	w23, w19, 8
	lsr	w24, w19, 16
	lsr	w25, w19, 24
	ldr	x26, [x0,1048]
	ubfiz	x27, x21, 8, 8
	mov	w28, 120
	and	w19, w19, 255
.L283:
	mov	w0, w21
	bl	nandc_cs
	add	x1, x26, x27
	add	x0, x20, :lo12:.LANCHOR0
	str	w28, [x1,2056]
	str	w19, [x1,2052]
	str	w23, [x1,2052]
	ldrb	w0, [x0,1256]
	str	w24, [x1,2052]
	cbz	w0, .L279
	str	w25, [x1,2052]
.L279:
	mov	x0, 600
	str	x1, [x29,104]
	bl	__const_udelay
	ldr	x1, [x29,104]
	mov	w0, w21
	ldr	w1, [x1,2048]
	and	w1, w1, 255
	str	x1, [x29,104]
	bl	nandc_de_cs
	ldr	x1, [x29,104]
	and	w0, w22, w1
	cmp	w0, w22
	bne	.L283
	cmp	w1, 255
	beq	.L283
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
	cbnz	w0, .L286
	ldrb	w0, [x4,#:lo12:.LANCHOR0]
	cbz	w0, .L287
	ldrb	w0, [x7,1]
	cbz	w0, .L286
.L287:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w3, [x4,2]
	udiv	w0, w1, w3
	mul	w0, w0, w3
	sub	w3, w1, w0
	ldrb	w1, [x4,1]
	cbz	w1, .L288
	add	w1, w0, w3, lsl 1
	b	.L286
.L288:
	add	x4, x4, 4
	ldrh	w3, [x4,w3,uxtw 1]
	add	w1, w3, w0
.L286:
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
.L297:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L298
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L297
	mov	w0, -1
	b	.L296
.L298:
	mov	w0, 0
.L296:
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
	cbz	w1, .L300
	cmp	w1, 1
	ldr	x19, [x0,1048]
	bne	.L303
	adrp	x0, .LANCHOR2+37
	ldrb	w1, [x0,#:lo12:.LANCHOR2+37]
	cbz	w1, .L300
	sxtw	x0, w20
	add	x0, x0, 8
	add	x20, x19, x0, lsl 8
	str	w1, [x20,8]
	b	.L300
.L303:
	cmp	w1, 2
	bne	.L305
	add	x0, x0, x20
	ldrb	w1, [x0,1264]
	cbz	w1, .L300
	adrp	x21, .LANCHOR2
	strb	wzr, [x0,1264]
	add	x21, x21, :lo12:.LANCHOR2
	add	x21, x21, 8
	ldrb	w0, [x21,29]
	cbz	w0, .L300
	add	x20, x20, 8
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x21,29]
	add	x20, x19, x20, lsl 8
	str	w0, [x20,8]
	b	.L300
.L305:
	cmp	w1, 3
	bne	.L300
	add	x21, x0, w20, sxtw
	ldrb	w0, [x21,1264]
	cbz	w0, .L300
	bl	nandc_wait_flash_ready_no_delay
	strb	wzr, [x21,1264]
	ubfiz	x0, x20, 8, 8
	add	x19, x19, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	x0, 500
	bl	__const_udelay
	str	wzr, [x19,2048]
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	mov	x0, 750
	str	wzr, [x19,2048]
	bl	__const_udelay
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 218
	str	w0, [x19,2056]
	mov	x0, 250
	bl	__const_udelay
.L300:
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
	cbz	w0, .L318
	cmp	w0, 1
	ldr	x19, [x21,1048]
	bne	.L321
	adrp	x0, .LANCHOR2+38
	ldrb	w1, [x0,#:lo12:.LANCHOR2+38]
	cbz	w1, .L318
	sxtw	x0, w20
	add	x0, x0, 8
	add	x20, x19, x0, lsl 8
	str	w1, [x20,8]
	b	.L318
.L321:
	cmp	w0, 2
	bne	.L323
	add	x21, x21, x20
	ldrb	w0, [x21,1264]
	cbnz	w0, .L318
	adrp	x22, .LANCHOR2
	mov	w1, 4
	add	x22, x22, :lo12:.LANCHOR2
	add	x22, x22, 8
	ldrb	w0, [x22,12]
	cmp	w0, 2
	csel	w0, w0, w1, eq
	strb	w0, [x21,1264]
	ldrb	w0, [x22,30]
	cbz	w0, .L318
	add	x20, x20, 8
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x22,30]
	add	x20, x19, x20, lsl 8
	str	w0, [x20,8]
	b	.L318
.L323:
	cmp	w0, 3
	bne	.L318
	add	x21, x21, w20, sxtw
	ldrb	w0, [x21,1264]
	cbnz	w0, .L318
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
	mov	x0, 500
	bl	__const_udelay
	ldrb	w0, [x22,15]
	cmp	w0, 9
	mov	w0, 1
	bne	.L326
	str	w0, [x19,2048]
	b	.L327
.L326:
	ldrb	w1, [x21,1264]
	str	w1, [x19,2048]
.L327:
	str	w0, [x19,2048]
	mov	x0, 750
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	__const_udelay
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 223
	str	w0, [x19,2056]
	mov	x0, 250
	bl	__const_udelay
.L318:
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
	ldrb	w0, [x0,1153]
	sub	w20, w20, w19
	lsl	w19, w2, w19
	lsr	w1, w1, w20
	sub	w19, w19, #1
	and	w19, w1, w19
	uxtb	w19, w19
	cmp	w0, w19
	bhi	.L339
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1013
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L339:
	add	x24, x22, :lo12:.LANCHOR0
	ldrb	w0, [x24,1153]
	cmp	w0, w19
	bls	.L338
	mov	w0, 1
	add	x19, x24, w19, sxtw
	lsl	w20, w0, w20
	ldrb	w19, [x19,1248]
	sub	w20, w20, #1
	ldr	x25, [x24,1048]
	and	w20, w20, w23
	mov	w0, w19
	ubfx	x23, x23, 24, 2
	bl	nandc_cs
	cbnz	w23, .L341
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L342
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	b	.L342
.L341:
	ldr	x0, [x24,1144]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L343
	ldrb	w0, [x24,1268]
	cbnz	w0, .L343
	ldrb	w0, [x24,1269]
	cbnz	w0, .L343
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w23, [x0,8]
	b	.L342
.L343:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L342:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,1144]
	ldrb	w1, [x0,7]
	cmp	w1, 1
	bne	.L344
	ldrb	w0, [x0,12]
	cmp	w0, 2
	bne	.L344
	sxtw	x0, w19
	mov	w1, 38
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w1, [x0,8]
.L344:
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
	cbz	w0, .L345
	lsr	w20, w20, 24
	str	w20, [x1,2052]
.L345:
	str	w21, [x1,2056]
.L338:
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
	mov	x0, 750
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	movk	w19, 0x1, lsl 16
	bl	__const_udelay
.L354:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L355
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L354
	mov	w0, -1
	b	.L353
.L355:
	mov	w0, 0
.L353:
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
	mov	x0, 1000
	bl	__const_udelay
	mov	w1, 5
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	adrp	x5, .LANCHOR0
	smull	x1, w19, w1
	add	x0, x2, 48
	add	x2, x2, 96
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L358:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,1137]
	cmp	w4, w2
	bls	.L363
	ldrb	w3, [x3,1136]
	cmp	w3, 67
	bne	.L359
	ldrsb	w3, [x0,x2]
	b	.L362
.L359:
	ldrsb	w3, [x1,x2]
.L362:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L358
.L363:
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
	add	x1, x2, w1, uxtb
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
	cbz	w2, .L367
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
	b	.L368
.L367:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 376
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L368:
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
	add	x1, x2, w1, uxtb
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
	ubfx	x24, x0, 24, 2
	ldrb	w5, [x4,1257]
	sub	w3, w3, w5
	lsl	w21, w2, w3
	lsl	w2, w2, w5
	lsr	w19, w0, w3
	sub	w2, w2, #1
	sub	w21, w21, #1
	and	w19, w19, w2
	and	w22, w21, w0
	ldrb	w0, [x4,1153]
	uxtb	w19, w19
	and	w21, w21, w1
	cmp	w0, w19
	bhi	.L372
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1148
	add	x1, x1, 1088
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L372:
	add	x26, x20, :lo12:.LANCHOR0
	add	x19, x26, w19, sxtw
	ldrb	w23, [x19,1248]
	mov	w0, w23
	ldr	x25, [x26,1048]
	bl	nandc_cs
	cbnz	w24, .L373
	mov	w0, w22
	bl	slc_phy_page_address_calc
	mov	w22, w0
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L374
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
	b	.L374
.L373:
	ldr	x0, [x26,1144]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L375
	ldrb	w0, [x26,1268]
	cbnz	w0, .L375
	ldrb	w0, [x26,1269]
	cbnz	w0, .L375
	sxtw	x0, w23
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w24, [x0,8]
	b	.L374
.L375:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
.L374:
	add	x0, x20, :lo12:.LANCHOR0
	uxtb	x19, w23
	and	w3, w22, 255
	lsr	w2, w22, 8
	and	w28, w21, 255
	lsr	w27, w21, 8
	ldrb	w1, [x0,1176]
	lsr	w26, w21, 16
	cmp	w1, 1
	lsr	w1, w22, 16
	bne	.L376
	add	x19, x25, x19, lsl 8
	ldrb	w4, [x0,1168]
	ldrb	w0, [x0,1256]
	str	w4, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w3, [x19,2052]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	cbz	w0, .L377
	lsr	w22, w22, 24
	str	w22, [x19,2052]
.L377:
	add	x22, x20, :lo12:.LANCHOR0
	ldrb	w0, [x22,1169]
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	cbz	w24, .L378
	ldr	x0, [x22,1144]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L378
	str	w24, [x19,2056]
.L378:
	str	wzr, [x19,2056]
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w28, [x19,2052]
	str	w27, [x19,2052]
	ldrb	w0, [x20,1256]
	str	w26, [x19,2052]
	cbz	w0, .L404
	lsr	w21, w21, 24
	str	w21, [x19,2052]
	b	.L404
.L376:
	ldr	x0, [x0,1144]
	ldrb	w4, [x0,7]
	cmp	w4, 1
	bne	.L381
	ldrb	w4, [x0,12]
	cmp	w4, 2
	bne	.L381
	add	x23, x23, 8
	mov	w4, 38
	add	x23, x25, x23, lsl 8
	str	w4, [x23,8]
.L381:
	add	x4, x20, :lo12:.LANCHOR0
	add	x19, x25, x19, lsl 8
	ldrb	w4, [x4,1168]
	str	w4, [x19,2056]
	str	w3, [x19,2052]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	cbz	w24, .L382
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L382
	str	w24, [x19,2056]
.L382:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1169]
	str	w0, [x19,2056]
	str	w28, [x19,2052]
	str	w27, [x19,2052]
	str	w26, [x19,2052]
.L404:
	mov	w0, 48
	str	w0, [x19,2056]
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
.L420:
	add	x0, x22, x21, lsl 3
	ldr	x20, [x22,1048]
	adrp	x1, .LANCHOR0
	ldrb	w19, [x0,1272]
	cmp	w19, 152
	beq	.L406
	cmp	w19, 69
	beq	.L406
	cmp	w19, 155
	beq	.L406
	cmp	w19, 44
	cset	w2, eq
	cmp	w19, 137
	cset	w0, eq
	orr	w0, w2, w0
	cbz	w0, .L407
.L406:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w25, 1
	ldrb	w1, [x1,1304]
	bne	.L409
	tbz	x1, 0, .L407
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L410
	mov	x0, x24
	bl	printk
.L410:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L421
	cmp	w19, 44
	bne	.L411
.L421:
	mov	w0, 1
	b	.L442
.L411:
	cmp	w19, 155
	mov	w0, 1
	bne	.L419
.L442:
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L440
.L409:
	tbz	x1, 2, .L407
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L415
	mov	x0, x23
	bl	printk
.L415:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L422
	cmp	w19, 44
	cset	w0, eq
	cbz	w0, .L416
.L422:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L440
.L416:
	cmp	w19, 155
	bne	.L419
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 37
	b	.L440
.L419:
	mov	w2, 128
	str	w2, [x1,2052]
.L440:
	str	w0, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L407:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L420
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
	mov	x0, 1000
	bl	__const_udelay
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
	mov	x0, 1000
	str	wzr, [x20,2052]
	bl	__const_udelay
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
	adrp	x0, .LC39
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC39
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
	mov	x0, 250
	str	w22, [x19,2052]
	bl	__const_udelay
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
	mov	x0, 250
	bl	__const_udelay
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
	cbnz	w3, .L466
	mov	w3, 162
.L466:
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
	mov	x0, 1000
	bl	__const_udelay
	ldr	w2, [x19,2048]
	ldr	x1, [x29,56]
	cmp	w2, 255
	bne	.L454
	ldr	w2, [x19,2048]
.L454:
	mov	w0, 5
	ands	w21, w2, w0
	beq	.L455
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L455
	adrp	x0, .LC41
	add	x0, x0, :lo12:.LC41
	bl	printk
.L455:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,38]
	cbz	w0, .L456
	str	w0, [x19,2056]
.L456:
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
	strb	w0, [x1,1244]
	cmp	w2, 9
	ldr	x2, [x1,1048]
	ldr	w1, [sp,8]
	bne	.L469
	str	w1, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L470
	cmp	w0, 60
	mov	w1, 3
	beq	.L470
	cmp	w0, 40
	mov	w1, 2
	csinc	w1, w1, wzr, eq
.L470:
	str	wzr, [sp]
	ldr	w0, [sp]
	bfi	w0, w1, 25, 3
	str	w0, [sp]
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,32]
	b	.L468
.L469:
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
	beq	.L478
	cmp	w0, 24
	bne	.L474
	ldr	w0, [sp]
	orr	w0, w0, 16
	b	.L479
.L474:
	ldr	w1, [sp]
	cmp	w0, 40
	orr	w1, w1, 262144
	str	w1, [sp]
	ldr	w1, [sp]
	orr	w1, w1, 16
	str	w1, [sp]
	bne	.L473
.L478:
	ldr	w0, [sp]
	and	w0, w0, -17
.L479:
	str	w0, [sp]
.L473:
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2,12]
.L468:
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
	bne	.L481
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
	b	.L482
.L481:
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
.L482:
	mov	x19, 0
	add	x21, x20, :lo12:.LANCHOR0
	mov	w23, 2
.L484:
	add	x0, x21, x19, lsl 3
	ldrb	w0, [x0,1272]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L483
	uxtb	w22, w19
	mov	w0, w22
	bl	flash_reset
	add	x0, x21, 1264
	strb	w23, [x19,x0]
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L483:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L484
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1192]
	cbz	w0, .L485
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,1072]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L485:
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
	bne	.L492
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L493
.L492:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L493:
	ldr	w1, [x0]
	tbnz	x1, 9, .L494
	bl	wait_for_nand_flash_ready
	b	.L491
.L494:
	mov	w1, 1
	bl	nandc_irq_disable
.L491:
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
	ldrb	w0, [x0,1153]
	mov	w23, w3
	cmp	w0, w19
	bhi	.L497
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 695
	add	x1, x1, 1112
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L497:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1153]
	cmp	w2, w19
	bls	.L498
	adrp	x0, .LANCHOR2
	add	x19, x1, w19, sxtw
	ldrb	w25, [x19,1248]
	ldr	x19, [x1,1048]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	x22, x25
	add	x25, x25, 8
	add	x25, x19, x25, lsl 8
	tbz	x0, 4, .L499
	adrp	x0, .LC42
	mov	w1, w22
	add	x0, x0, :lo12:.LC42
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L499:
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	nandc_cs
	mov	w0, w22
	cbnz	w20, .L500
	bl	zftl_flash_enter_slc_mode
	b	.L501
.L500:
	bl	zftl_flash_exit_slc_mode
.L501:
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
	cbz	w0, .L502
	lsr	w0, w24, 24
	str	w0, [x19,2052]
.L502:
	add	x0, x21, :lo12:.LANCHOR0
	mov	w20, 0
	ldrb	w0, [x0,1305]
	cbnz	w0, .L503
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x25
	bl	flash_read_status
	and	w20, w0, w20
.L503:
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
	cbz	w0, .L504
	lsr	w23, w23, 24
	str	w23, [x19,2052]
.L504:
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
	cbz	w19, .L505
	adrp	x0, .LC43
	mov	w1, w24
	add	x0, x0, :lo12:.LC43
	bl	printk
.L505:
	mov	w0, w19
.L498:
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
	ldrb	w0, [x1,1153]
	sub	w19, w19, #1
	cmp	w0, w22
	and	w19, w19, w2
	bhi	.L521
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 757
	add	x1, x1, 1144
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L521:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1153]
	cmp	w2, w22
	bls	.L522
	add	x0, x1, w22, sxtw
	ldrb	w24, [x0,1248]
	adrp	x0, .LANCHOR2
	ldr	x26, [x1,1048]
	mov	x21, x24
	add	x24, x24, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x24, x26, x24, lsl 8
	tbz	x0, 4, .L523
	adrp	x0, .LC44
	mov	w1, w21
	add	x0, x0, :lo12:.LC44
	mov	w2, w23
	mov	w3, w25
	bl	printk
.L523:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L524
	bl	zftl_flash_enter_slc_mode
	b	.L525
.L524:
	bl	zftl_flash_exit_slc_mode
.L525:
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
	cbz	w0, .L526
	lsr	w19, w19, 24
	str	w19, [x3,2052]
.L526:
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
	beq	.L527
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	mov	w1, w22
	ldrh	w2, [x20,2]
	udiv	w2, w23, w2
	bl	printk
	mov	w3, -1
.L527:
	mov	w0, w3
.L522:
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
.L540:
	ldrb	w0, [x23,1153]
	cmp	w0, w19
	bls	.L544
	add	x0, x23, w19, sxtw
	ldrb	w24, [x0,1248]
	mov	w22, 0
.L541:
	cmp	w20, w22, uxth
	bls	.L545
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L541
.L545:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L540
.L544:
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
	bne	.L548
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
	ldrb	w1, [x4,1193]
	cbz	w1, .L549
	ldrb	w1, [x4,1240]
	cbz	w1, .L549
	ldr	w1, [x29,56]
	orr	w1, w1, 512
	str	w1, [x29,56]
.L549:
	ldr	w1, [x29,56]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	str	x3, [x20,1216]
	str	x0, [x20,1208]
	bl	rknand_dma_map_single
	str	w0, [x20,1224]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,1216]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	bl	rknand_dma_map_single
	str	w0, [x20,1228]
	mov	w1, 1
	str	w1, [x20,1232]
	ldr	x1, [x20,1048]
	cmp	w21, wzr
	ldr	w2, [x20,1224]
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
	ldrh	w2, [x20,1242]
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
	b	.L547
.L548:
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
	cbz	w21, .L551
	ldrb	w2, [x4,1244]
	ubfx	x1, x1, 1, 7
	mov	w5, 128
	cmp	w2, 25
	mov	w2, 64
	csel	w2, w2, w5, cc
	add	x1, x3, x1, lsl 2
	mov	w5, 0
.L553:
	cmp	x3, x1
	add	w7, w5, w2
	beq	.L555
	ldrh	w6, [x3,2]
	and	x5, x5, 4294967292
	ldr	x8, [x4,1200]
	ldrh	w9, [x3],4
	orr	w6, w9, w6, lsl 16
	str	w6, [x8,x5]
	mov	w5, w7
	b	.L553
.L551:
	ldr	x1, [x4,1200]
	str	w6, [x1]
.L555:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ldr	x1, [x20,1200]
	str	x1, [x20,1216]
	ldr	w1, [x29,56]
	str	x0, [x20,1208]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_map_single
	str	w0, [x20,1224]
	ldr	w1, [x29,56]
	mov	w2, w21
	ldr	x0, [x20,1216]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
	bl	rknand_dma_map_single
	str	w0, [x20,1228]
	ldr	x1, [x20,1048]
	mov	w0, 1
	str	w0, [x20,1232]
	mov	w2, 16
	ldr	w0, [x20,1224]
	cmp	w21, wzr
	str	w0, [x1,20]
	ldr	w0, [x20,1228]
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
.L547:
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
	ldrb	w3, [x0,1240]
	cmp	w3, wzr
	csel	w1, w2, w1, ne
	ldrb	w2, [x0,1028]
	ldr	x0, [x0,1048]
	cmp	w2, 9
	bne	.L565
	str	w1, [x0,520]
	b	.L563
.L565:
	str	w1, [x0,336]
.L563:
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
	cbz	w0, .L570
	ldrb	w0, [x1,1154]
	cbnz	w0, .L570
	ldrb	w1, [x1,1028]
	cmp	w1, 9
	beq	.L570
	bl	zftl_flash_exit_slc_mode
.L570:
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	hynix_reconfig_rr_para
	ldrb	w0, [x20,1192]
	cbz	w0, .L571
	ldrb	w0, [x20,1304]
	tbz	x0, 0, .L571
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,1192]
.L571:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,1240]
	cbz	w0, .L572
	mov	w0, 0
	strb	wzr, [x19,1240]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,1240]
.L572:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+1240
	strb	w0, [x1,#:lo12:.LANCHOR0+1240]
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
.L591:
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
	bne	.L591
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
.L594:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L594
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
	bne	.L602
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L598
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 74
	add	x1, x1, 1424
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L598:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L596
.L602:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1312
.L603:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L603
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L600
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 81
	add	x1, x1, 1424
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L600:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 1312
	strb	w1, [x0,x20]
.L596:
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
	cbz	w1, .L606
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L606
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 3361
	bl	buf_add_tail
	b	.L607
.L606:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,3361]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,3361]
.L607:
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
	cbz	w0, .L615
.L618:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1312
	ldrb	w2, [x1,3361]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L616
	b	.L617
.L615:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 121
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w1, [x20,3362]
	cbnz	w1, .L618
.L620:
	mov	x0, 0
	b	.L619
.L616:
	ldrb	w1, [x1,3362]
	cmp	w1, 1
	beq	.L620
.L617:
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
.L619:
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
	bne	.L627
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L631
.L627:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1312
.L629:
	cmp	w2, 255
	beq	.L632
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L629
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L631:
	mov	w0, 1
	b	.L626
.L632:
	mov	w0, 0
.L626:
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
	cbnz	w0, .L634
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 172
	add	x1, x1, 1456
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L634:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,3362]
	cbz	w0, .L633
	add	x0, x19, 3361
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L633
	ldrb	w0, [x19,3362]
	sub	w0, w0, #1
	strb	w0, [x19,3362]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L633:
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
.L639:
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
	bne	.L639
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
	adrp	x0, .LANCHOR0+1128
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+1128]
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
	adrp	x0, .LANCHOR0+1128
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+1128]
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
	bne	.L644
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L644:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L646:
	ldrb	w2, [x0]
	cbz	w2, .L653
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L647
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L648
.L647:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L649
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L648
.L649:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L653
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L648:
	add	x0, x0, 1
	b	.L646
.L653:
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
	add	x19, x20, w0, sxtw
	add	x1, x1, :lo12:.LC56
	ldrb	w5, [x7,4]
	mov	x0, x19
	ldrb	w6, [x7,5]
	ldrb	w3, [x7,2]
	ldrb	w4, [x7,3]
	ldrb	w2, [x7,1]
	ldrb	w7, [x7,6]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x21,1032]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x21,3368]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, w0, sxtw
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
	add	x19, x19, w0, sxtw
	ldr	w2, [x21,1032]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC62
	mov	x0, x19
	add	x1, x1, :lo12:.LC62
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3376]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3378]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3380]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3382]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3384]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3386]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrb	w4, [x21,3389]
	adrp	x1, .LC70
	ldrb	w2, [x21,3362]
	mov	x0, x19
	ldrb	w3, [x21,3388]
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x3, [x21,3392]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21,3392]
	adrp	x1, .LC72
	add	x1, x1, :lo12:.LC72
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21,3392]
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21,1120]
	mov	w2, 10
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x4, [x21,3392]
	adrp	x1, .LC77
	ldrb	w2, [x22,#:lo12:.LANCHOR0]
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	ldr	w3, [x4,156]
	ldrh	w4, [x4,150]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC78
	mov	x0, x19
	add	x1, x1, :lo12:.LC78
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,1120]
	adrp	x1, .LC79
	mov	x0, x19
	add	x1, x1, :lo12:.LC79
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC80
	mov	x0, x19
	add	x1, x1, :lo12:.LC80
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC81
	mov	x0, x19
	add	x1, x1, :lo12:.LC81
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC82
	mov	x0, x19
	add	x1, x1, :lo12:.LC82
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x2, [x21,3392]
	adrp	x1, .LC83
	mov	x0, x19
	add	x1, x1, :lo12:.LC83
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC84
	mov	x0, x19
	add	x1, x1, :lo12:.LC84
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC85
	mov	x0, x19
	add	x1, x1, :lo12:.LC85
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x4, [x21,1120]
	adrp	x1, .LC86
	mov	x0, x19
	add	x1, x1, :lo12:.LC86
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, w0, sxtw
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
	add	x19, x19, w0, sxtw
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
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21,3400]
	adrp	x1, .LC89
	mov	x0, x19
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w6, [x21,3412]
	adrp	x1, .LC90
	ldrh	w2, [x21,3402]
	mov	x0, x19
	ldrh	w3, [x21,3404]
	add	x1, x1, :lo12:.LC90
	ldrh	w4, [x21,3406]
	ldrh	w5, [x21,3408]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x5, [x21,1120]
	adrp	x1, .LC91
	mov	x0, x19
	add	x1, x1, :lo12:.LC91
	ldrh	w2, [x5,584]
	ldrh	w3, [x5,586]
	ldrh	w4, [x5,588]
	ldrh	w5, [x5,590]
	bl	sprintf
	add	x19, x19, w0, sxtw
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
	add	x0, x19, w0, sxtw
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
	tbz	x0, 8, .L662
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
.L662:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w0, [x0,1088]
	cmp	w0, w21
	bhi	.L663
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 543
	add	x1, x1, 1760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L663:
	add	x5, x22, :lo12:.LANCHOR0
	ldrh	w0, [x5,1088]
	cmp	w0, w21
	bhi	.L664
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x0, 10, .L665
	adrp	x0, .LC94
	mov	w1, w21
	mov	w2, w23
	mov	w3, w20
	add	x0, x0, :lo12:.LC94
	bl	printk
	b	.L665
.L664:
	uxtw	x0, w21
	ldr	x1, [x5,1112]
	ldrh	w4, [x1,x0,lsl 1]
	ldr	x1, [x5,1096]
	add	x0, x1, x0, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L666
	cbz	w4, .L665
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 553
	add	x1, x1, 1760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L665
.L666:
	ldrh	w1, [x5,3424]
	add	x8, x5, 3424
	mov	w19, 0
	cmp	w1, w21
	beq	.L665
	ldr	x7, [x5,1120]
	ldrh	w1, [x7,48]
	cmp	w1, w21
	beq	.L665
	ldrh	w1, [x7,16]
	cmp	w1, w21
	beq	.L665
	ldrh	w1, [x7,80]
	cmp	w1, w21
	beq	.L665
	ldrh	w5, [x8,56]
	mov	x6, 0
.L667:
	cmp	w5, w6
	bls	.L700
	add	x6, x6, 1
	add	x1, x8, x6, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L667
	mov	w19, 0
	b	.L665
.L700:
	cbnz	w23, .L669
	adrp	x6, .LANCHOR3
	mov	w19, w23
	add	x6, x6, :lo12:.LANCHOR3
	ldrh	w9, [x6,1288]
	cmp	w9, w21
	beq	.L665
	mov	x8, 0
.L671:
	add	x1, x6, 1296
	ldrh	w1, [x8,x1]
	cmp	w21, w1
	bne	.L670
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x1, 8, .L665
	ldrb	w2, [x0,2]
	adrp	x0, .LC95
	mov	w3, w4
	mov	w1, w21
	ubfx	x2, x2, 5, 3
	mov	w4, w21
	mov	w5, w9
	add	x0, x0, :lo12:.LC95
	bl	printk
	b	.L665
.L670:
	add	x8, x8, 2
	cmp	x8, 16
	bne	.L671
	sxtw	x6, w20
	add	x19, x7, x6, lsl 7
	add	x19, x19, 136
	b	.L673
.L669:
	add	x19, x7, 392
.L673:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L674
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w3, [x0,2]
	adrp	x0, .LC96
	mov	w1, w21
	add	x0, x0, :lo12:.LC96
	mov	w2, w23
	ldrh	w6, [x6,3406]
	ubfx	x3, x3, 5, 3
	bl	printk
.L674:
	mov	x4, x19
	add	x0, x19, 128
	mov	w1, 65535
.L677:
	ldrh	w2, [x4]
	cmp	w2, w1
	bne	.L675
	strh	w21, [x4]
	add	x22, x22, :lo12:.LANCHOR0
	cbz	w23, .L676
	ldr	x1, [x22,1120]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L699
.L676:
	ldr	x0, [x22,1120]
	add	x20, x0, w20, uxth 1
	ldrh	w0, [x20,120]
	add	w0, w0, 1
	strh	w0, [x20,120]
.L699:
	mov	w19, 1
	b	.L665
.L675:
	add	x4, x4, 2
	cmp	x4, x0
	bne	.L677
	b	.L699
.L665:
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
	ldrh	w0, [x4,1312]
	sub	w19, w1, w19
	ldr	w1, [x21,2200]
	sub	w19, w19, w0
	lsr	w2, w3, w0
	lsl	w19, w23, w19
	ldrb	w0, [x4,1314]
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
	ldr	w0, [x21,2200]
	mov	w1, 0
.L702:
	cmp	w1, w0
	bcs	.L705
	add	x2, x21, w1, sxtw 1
	ldrh	w2, [x2,2204]
	cmp	w2, w22
	beq	.L703
	add	w1, w1, 1
	uxth	w1, w1
	b	.L702
.L705:
	cmp	w0, 5
	bhi	.L703
	add	x20, x20, :lo12:.LANCHOR0
	add	w1, w0, 1
	str	w1, [x20,5624]
	add	x20, x20, w0, uxtw 1
	strh	w22, [x20,5628]
.L703:
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
	adrp	x1, .LANCHOR3+1320
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR3+1320]
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
.L708:
	ldrb	w20, [x26,3365]
	adrp	x19, .LANCHOR0
	cmp	w20, 255
	beq	.L737
	lsl	x1, x20, 6
	add	x0, x22, x1
	ldrb	w1, [x22,x1]
	strb	w1, [x26,3365]
	ldr	w1, [x0,52]
	ldrh	w23, [x0,48]
	cbz	w1, .L709
	mov	w2, 1
	strh	w2, [x26,5618]
	ldr	w2, [x0,40]
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	str	w2, [x26,5620]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 956
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L709:
	adrp	x25, .LANCHOR3
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w0, [x1,1328]
	cmp	w0, 3
	bne	.L710
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,1268]
	cbnz	w2, .L710
	ldr	x0, [x0,1120]
	ldrb	w1, [x1,1329]
	ldrb	w0, [x0,89]
	cmp	w1, w0
	bhi	.L723
	cmp	w0, 3
	mov	w1, 2
	csel	w0, w0, w1, cc
	b	.L712
.L723:
	mov	w0, 1
.L712:
	add	w24, w0, w0, lsl 1
	b	.L713
.L710:
	add	x0, x25, :lo12:.LANCHOR3
	mov	w24, 1
	ldrb	w1, [x0,1330]
	cbz	w1, .L713
	ldrb	w0, [x0,1331]
	cmp	w0, wzr
	mov	w0, 2
	csel	w24, w24, w0, eq
.L713:
	add	x0, x25, :lo12:.LANCHOR3
	uxth	x1, w23
	ldr	x2, [x0,1336]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, x20, lsl 6
	ldrb	w1, [x2,x1]
	ldrb	w0, [x0,1313]
	cmp	w1, w0
	beq	.L714
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 976
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L714:
	add	x0, x19, :lo12:.LANCHOR0
	add	x20, x0, x20, lsl 6
	ldrb	w1, [x20,1373]
	cmp	w1, 3
	beq	.L715
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w2, [x1,1328]
	cmp	w2, 3
	bne	.L715
	ldrb	w2, [x1,1344]
	cbnz	w2, .L715
	ldrb	w2, [x0,1268]
	cbnz	w2, .L715
	ldrb	w0, [x0,1269]
	cbnz	w0, .L715
	ldrb	w0, [x1,1330]
	cbz	w0, .L716
	ldrb	w0, [x1,1331]
	cbnz	w0, .L715
.L716:
	add	x25, x25, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x19, x19, 1312
	ldr	x2, [x25,1336]
	b	.L717
.L715:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 0
	mov	w27, -1
	add	x28, x19, 1312
	strh	w23, [x19,5532]
.L718:
	cmp	w24, w20, uxth
	bls	.L708
	add	x0, x25, :lo12:.LANCHOR3
	add	w2, w20, w23
	add	w20, w20, 1
	ldr	x0, [x0,1336]
	ldrb	w1, [x0,x2]
	strb	w27, [x0,x2]
	sbfiz	x1, x1, 6, 32
	add	x0, x28, x1
	strb	wzr, [x0,61]
	bl	zbuf_free
	ldrb	w0, [x19,3431]
	sub	w0, w0, #1
	strb	w0, [x19,3431]
	b	.L718
.L717:
	cmp	w24, w1, uxth
	bls	.L708
	add	w0, w1, w23
	add	w1, w1, 1
	ldrb	w0, [x2,x0]
	add	x0, x19, x0, lsl 6
	strb	wzr, [x0,61]
	b	.L717
.L737:
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
	ldrb	w0, [x0,1345]
	cbz	w5, .L739
	add	x1, x1, 392
	mov	w2, 1
	b	.L740
.L739:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w6, [x0,120]
	mov	w0, 65535
	cbz	w6, .L741
	add	x1, x1, x2, lsl 7
	mov	w2, w5
	add	x1, x1, 136
.L740:
	add	x5, x1, 128
	mov	w6, 65535
.L744:
	ldrh	w0, [x1]
	cmp	w0, w6
	beq	.L742
	mov	w5, -1
	strh	w5, [x1]
	cbz	w2, .L743
	add	x3, x3, :lo12:.LANCHOR0
	ldr	x2, [x3,1120]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L741
.L743:
	add	x4, x4, :lo12:.LANCHOR3
	add	x3, x3, :lo12:.LANCHOR0
	ldrb	w1, [x4,1345]
	ldr	x2, [x3,1120]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L741
.L742:
	add	x1, x1, 2
	cmp	x1, x5
	bne	.L744
.L741:
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
	cbz	w1, .L749
	ldrb	w1, [x4,3362]
	cmp	w1, 1
	bhi	.L749
	adrp	x20, .LANCHOR3
	ldrh	w1, [x4,5532]
	add	x5, x20, :lo12:.LANCHOR3
	add	x4, x4, 1312
	add	w2, w1, 24
	ldrh	w3, [x5,1346]
	ldrb	w0, [x5,1329]
	mul	w0, w3, w0
	ldr	x3, [x5,1336]
	cmp	w0, w2
	csel	w2, w0, w2, ls
.L750:
	cmp	w1, w2
	bcs	.L763
	uxtw	x21, w1
	ldrb	w0, [x3,x21]
	cmp	w0, 255
	beq	.L751
	sbfiz	x0, x0, 6, 32
	add	x0, x4, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L751
	str	x1, [x29,56]
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L752
	add	x0, x20, :lo12:.LANCHOR3
	ldr	x2, [x0,1336]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	ldrb	w2, [x2,x21]
	bl	printk
.L752:
	add	x20, x20, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x20,1336]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,3431]
	sub	w0, w0, #1
	strb	w0, [x19,3431]
	mov	w0, 1
	b	.L749
.L751:
	add	w1, w1, 1
	b	.L750
.L763:
	mov	w0, 0
.L749:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	get_ink_scaned_blk
	.type	get_ink_scaned_blk, %function
get_ink_scaned_blk:
	adrp	x1, .LANCHOR0
	mov	w0, 65535
	add	x1, x1, :lo12:.LANCHOR0
	add	x3, x1, 3424
	ldrh	w2, [x1,5536]
	cbz	w2, .L765
	sub	w2, w2, #1
	add	x0, x3, w2, sxtw 1
	ldrh	w0, [x0,2114]
	strh	w2, [x1,5536]
.L765:
	ret
	.size	get_ink_scaned_blk, .-get_ink_scaned_blk
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
	.global	_list_pop_index_node
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	stp	x29, x30, [sp, -32]!
	uxth	w1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 65535
	ldr	x20, [x0]
	cbz	x20, .L770
	adrp	x3, .LANCHOR0+1040
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3,#:lo12:.LANCHOR0+1040]
.L771:
	cbnz	w1, .L772
.L774:
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
	b	.L770
.L772:
	ldrh	w3, [x20]
	cmp	w3, w4
	beq	.L774
	umull	x20, w3, w5
	sub	w1, w1, #1
	add	x20, x19, x20
	uxth	w1, w1
	b	.L771
.L770:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	_list_pop_index_node, .-_list_pop_index_node
	.align	2
	.global	_list_get_gc_head_node
	.type	_list_get_gc_head_node, %function
_list_get_gc_head_node:
	ldr	x2, [x0]
	uxth	w1, w1
	mov	w0, 65535
	cbz	x2, .L782
	adrp	x0, .LANCHOR0+1040
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x0,#:lo12:.LANCHOR0+1040]
.L778:
	cbz	w1, .L779
	ldrh	w0, [x2]
	cmp	w0, w4
	beq	.L782
	umull	x0, w0, w5
	sub	w1, w1, #1
	add	x2, x3, x0
	uxth	w1, w1
	b	.L778
.L779:
	sub	x2, x2, x3
	mov	x0, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x0, x2, x2
	uxth	w0, w2
.L782:
	ret
	.size	_list_get_gc_head_node, .-_list_get_gc_head_node
	.align	2
	.type	zftl_get_gc_node.part.10, %function
zftl_get_gc_node.part.10:
	stp	x29, x30, [sp, -16]!
	uxth	w1, w0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x0, x0, 1352
	bl	_list_get_gc_head_node
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node.part.10, .-zftl_get_gc_node.part.10
	.align	2
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	uxtb	w0, w1
	add	x1, x19, :lo12:.LANCHOR0
	str	w0, [x29,144]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w21, w2
	ldr	x3, [x1,1120]
	add	x0, x3, w23, sxtw 1
	ldrh	w0, [x0,120]
	cbnz	w0, .L905
	ldrh	w2, [x1,3480]
	cmp	w2, 1
	bhi	.L787
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	strh	w0, [x1,1360]
	strh	w0, [x1,1362]
	strh	w0, [x1,1364]
.L787:
	cbnz	w23, .L909
	adrp	x20, .LANCHOR3
	adrp	x26, .LC101
	add	x20, x20, :lo12:.LANCHOR3
	mov	w24, w23
	mov	w25, w23
	add	w22, w21, 1
	add	x27, x20, 1368
	add	x26, x26, :lo12:.LC101
.L788:
	cmp	w25, w22
	bge	.L794
	ldrh	w28, [x20,1362]
	mov	x0, x27
	mov	w1, w28
	bl	_list_get_gc_head_node
	uxth	w1, w0
	add	w2, w28, 1
	mov	w0, 65535
	cmp	w1, w0
	mov	w4, w1
	uxth	w2, w2
	strh	w2, [x20,1362]
	beq	.L790
	adrp	x0, .LANCHOR2
	uxtw	x28, w1
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L791
	add	x0, x19, :lo12:.LANCHOR0
	str	x4, [x29,136]
	ldr	x3, [x0,1112]
	mov	x0, x26
	ldrh	w3, [x3,x28,lsl 1]
	bl	printk
	ldr	x4, [x29,136]
.L791:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1112]
	ldrh	w0, [x0,3406]
	ldrh	w1, [x1,x28,lsl 1]
	cmp	w1, w0
	bcs	.L792
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L793
	add	w5, w24, 1
	uxth	w24, w5
	cmp	w24, w21
	bcc	.L793
	b	.L794
.L792:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	strh	wzr, [x0,1362]
	b	.L794
.L790:
	strh	wzr, [x20,1362]
	b	.L794
.L793:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L788
.L794:
	ldr	x0, [x29,144]
	tbz	x0, 1, .L796
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3386]
	cmp	w0, 32
	bls	.L796
	adrp	x28, .LANCHOR3
	mov	w25, 0
	add	x28, x28, :lo12:.LANCHOR3
	mov	w26, 65535
	add	x0, x28, 1376
	uxth	w27, w21
	str	x0, [x29,136]
.L797:
	cmp	w25, w22
	adrp	x20, .LANCHOR3
	bge	.L801
	ldrh	w2, [x28,1364]
	ldr	x0, [x29,136]
	mov	w1, w2
	str	x2, [x29,128]
	bl	_list_get_gc_head_node
	uxth	w1, w0
	ldr	x2, [x29,128]
	cmp	w1, w26
	add	w2, w2, 1
	strh	w2, [x28,1364]
	beq	.L798
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x4, [x2,1112]
	ldrh	w4, [x4,x1]
	ldrh	w1, [x2,3408]
	cmp	w4, w1
	bcs	.L798
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L800
	add	w5, w24, 1
	uxth	w24, w5
	cmp	w24, w27
	bcc	.L800
	b	.L801
.L798:
	strh	wzr, [x28,1364]
	b	.L801
.L800:
	add	w3, w25, 1
	uxth	w25, w3
	b	.L797
.L801:
	cmp	w24, w21
	bcs	.L803
	add	x20, x20, :lo12:.LANCHOR3
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,1346]
	ldrb	w4, [x20,1329]
	ldrh	w1, [x20,1384]
	ldrh	w3, [x2,3408]
	mul	w0, w0, w4
	sub	w0, w0, w1, lsr 2
	cmp	w3, w0
	bge	.L796
	add	w1, w3, w1, lsr 3
	strh	w1, [x2,3408]
	b	.L796
.L803:
	add	x1, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w2, [x1,3408]
	ldrh	w0, [x20,1384]
	cmp	w2, w0
	bls	.L796
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,3408]
.L796:
	ldr	x0, [x29,144]
	tbz	x0, 0, .L804
	uxth	w27, w21
	cmp	w24, w27
	bcs	.L804
	mov	w22, 0
	adrp	x25, .LANCHOR3
	mov	w26, 65535
.L809:
	add	x28, x25, :lo12:.LANCHOR3
	ldrh	w20, [x28,1360]
	mov	w0, w20
	add	w20, w20, 1
	bl	zftl_get_gc_node.part.10
	strh	w20, [x28,1360]
	cmp	w26, w0, uxth
	adrp	x20, .LANCHOR3
	beq	.L805
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L806
	add	w5, w24, 1
	uxth	w24, w5
	cmp	w24, w27
	bcc	.L806
	b	.L807
.L805:
	strh	wzr, [x28,1360]
	b	.L808
.L806:
	add	w22, w22, 1
	uxth	w22, w22
	cmp	w22, w27
	bcc	.L809
.L808:
	cmp	w24, w27
	bcs	.L807
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w1, [x19,3406]
	ldrh	w0, [x20,1384]
	cmp	w1, w0
	bcs	.L804
	add	w0, w1, w0, lsr 3
	b	.L908
.L807:
	add	x20, x20, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,1384]
	ldrh	w1, [x19,3406]
	cmp	w1, w0, lsr 1
	bls	.L804
	sub	w0, w1, w0, lsr 3
.L908:
	strh	w0, [x19,3406]
	b	.L804
.L909:
	adrp	x20, .LANCHOR3
	cmp	w21, 1
	add	x1, x20, :lo12:.LANCHOR3
	ldrb	w0, [x1,1329]
	ldrh	w22, [x1,1346]
	mul	w22, w0, w22
	uxth	w22, w22
	bne	.L810
	cbz	w2, .L810
	ldrh	w1, [x3,80]
	mov	w0, 65535
	mov	w21, 8
	cmp	w1, w0
	beq	.L810
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,5530]
	sub	w22, w22, w0
	uxth	w22, w22
.L810:
	ldr	w0, [x29,144]
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR3
	and	w0, w0, 1
	str	w0, [x29,136]
	ldr	x0, [x29,144]
	strh	wzr, [x1,5616]
	strh	wzr, [x3,1386]
	tbz	x0, 0, .L844
	ldrh	w3, [x3,1388]
	ldrh	w0, [x1,3382]
	cmp	w0, w3, lsr 2
	bhi	.L812
	ldrh	w1, [x1,3384]
	cmp	w1, w0
	bcs	.L845
.L812:
	cmp	w2, 1
	bls	.L814
.L816:
	mov	w24, 0
	b	.L815
.L814:
	add	x26, x20, :lo12:.LANCHOR3
	lsr	w0, w0, 2
	strh	w0, [x26,1360]
	mov	w0, 0
	bl	zftl_get_gc_node.part.10
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L816
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w2, [x26,1384]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 2
	bcs	.L816
	mov	w1, 1
	mov	w2, w23
	strh	w1, [x26,1360]
	mov	w1, 0
	bl	gc_add_sblk
	cmp	w0, wzr
	cset	w24, ne
.L815:
	mov	w28, 64
	mov	w25, 0
	mov	w26, 65535
	uxth	w27, w21
.L819:
	add	x2, x20, :lo12:.LANCHOR3
	str	x2, [x29,112]
	ldrh	w1, [x2,1360]
	str	x1, [x29,120]
	mov	w0, w1
	bl	zftl_get_gc_node.part.10
	uxth	w4, w0
	cmp	w4, w26
	str	x4, [x29,128]
	ldr	x2, [x29,112]
	beq	.L817
	ldr	x1, [x29,120]
	add	w1, w1, 1
	strh	w1, [x2,1360]
	mov	w1, 0
	mov	w2, w23
	bl	gc_add_sblk
	ldr	x4, [x29,128]
	cbz	w0, .L818
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x4, x4, 1, 16
	add	w5, w24, 1
	ldr	x1, [x0,1112]
	uxth	w24, w5
	ldrh	w1, [x1,x4]
	add	w3, w25, w1
	uxth	w25, w3
	cmp	w25, w22
	bhi	.L813
	cmp	w24, w27
	bcs	.L813
	ldrh	w1, [x0,3384]
	ldrh	w0, [x0,3382]
	cmp	w1, w0, lsl 1
	ble	.L818
	b	.L813
.L817:
	strh	wzr, [x2,1360]
	b	.L813
.L818:
	sub	w28, w28, #1
	uxth	w28, w28
	cbnz	w28, .L819
	b	.L813
.L844:
	ldr	w25, [x29,136]
	mov	x24, x25
.L811:
	ldr	x0, [x29,144]
	tbz	x0, 1, .L823
	cmp	w25, w22
	bcs	.L823
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3386]
	cmp	w0, 32
	bls	.L823
	add	x28, x20, :lo12:.LANCHOR3
	mov	w27, 64
	add	x0, x28, 1376
	mov	w26, 0
	str	x0, [x29,128]
	uxth	w0, w21
	str	w0, [x29,120]
.L829:
	ldrh	w2, [x28,1364]
	ldr	x0, [x29,128]
	mov	w1, w2
	str	x2, [x29,112]
	bl	_list_get_gc_head_node
	uxth	w8, w0
	mov	w1, 65535
	cmp	w8, w1
	beq	.L824
	ldr	x2, [x29,112]
	cmp	w21, 1
	add	w2, w2, 1
	strh	w2, [x28,1364]
	bne	.L825
	ldrh	w9, [x28,1346]
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x28,1329]
	mul	w2, w2, w9
	ldrh	w9, [x28,1384]
	sub	w2, w2, w9, lsr 3
	strh	w2, [x1,3402]
.L825:
	add	x9, x19, :lo12:.LANCHOR0
	ubfiz	x8, x8, 1, 16
	str	x9, [x29,112]
	str	x8, [x29,104]
	ldr	x1, [x9,1112]
	ldrh	w2, [x1,x8]
	ldrh	w1, [x9,3402]
	cmp	w2, w1
	bcs	.L826
	mov	w1, 0
	mov	w2, w23
	bl	gc_add_sblk
	ldr	x9, [x29,112]
	ldr	x8, [x29,104]
	cbz	w0, .L827
	ldr	x0, [x9,1112]
	add	w6, w26, 1
	add	w5, w24, 1
	uxth	w26, w6
	ldrh	w0, [x0,x8]
	uxth	w24, w5
	add	w3, w25, w0
	uxth	w25, w3
	cmp	w25, w22
	bhi	.L828
	ldr	w0, [x29,120]
	cmp	w24, w0
	bcc	.L827
	b	.L828
.L826:
	add	x0, x20, :lo12:.LANCHOR3
	strh	wzr, [x0,1364]
	b	.L828
.L824:
	strh	wzr, [x28,1364]
	b	.L828
.L827:
	sub	w7, w27, #1
	uxth	w27, w7
	cbnz	w27, .L829
.L828:
	cmp	w24, w21
	bcc	.L830
	cbnz	w26, .L831
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x1,3386]
	ldrh	w0, [x0,1390]
	cmp	w1, w0
	bls	.L831
.L830:
	add	x4, x20, :lo12:.LANCHOR3
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w2, [x4,1384]
	ldrh	w0, [x4,1346]
	ldrb	w4, [x4,1329]
	lsr	w2, w2, 3
	ldrh	w1, [x6,3402]
	mul	w0, w0, w4
	sub	w0, w0, w2
	cmp	w1, w0
	bge	.L823
	add	w1, w2, w1
	strh	w1, [x6,3402]
	b	.L823
.L831:
	add	x20, x20, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,1384]
	ldrb	w1, [x20,1329]
	ldrh	w2, [x19,3402]
	mul	w1, w0, w1
	cmp	w2, w1
	ble	.L804
	sub	w0, w2, w0, lsr 3
	strh	w0, [x19,3402]
	b	.L804
.L823:
	ldr	w0, [x29,136]
	cbz	w0, .L804
	uxth	w26, w21
	cmp	w24, w26
	bcs	.L804
	cmp	w25, w22
	bcs	.L804
	add	x4, x20, :lo12:.LANCHOR3
	mov	w27, 64
	add	x0, x4, 1368
	str	x0, [x29,136]
.L837:
	ldrh	w28, [x4,1362]
	ldr	x0, [x29,136]
	mov	w1, w28
	str	x4, [x29,128]
	bl	_list_get_gc_head_node
	uxth	w1, w0
	mov	w2, 65535
	ldr	x4, [x29,128]
	cmp	w1, w2
	beq	.L833
	add	w28, w28, 1
	strh	w28, [x4,1362]
	ubfiz	x28, x1, 1, 16
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,1112]
	ldrh	w7, [x2,x28]
	ldrh	w2, [x1,3404]
	cmp	w7, w2
	bcs	.L834
	ldrh	w2, [x4,1388]
	ldrh	w1, [x1,3384]
	cmp	w1, w2, lsr 1
	bls	.L835
.L834:
	mov	w1, 0
	mov	w2, w23
	str	x4, [x29,128]
	bl	gc_add_sblk
	ldr	x4, [x29,128]
	cbz	w0, .L836
	add	x0, x19, :lo12:.LANCHOR0
	add	w5, w24, 1
	uxth	w24, w5
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x28]
	add	w3, w25, w0
	uxth	w25, w3
	cmp	w25, w22
	bhi	.L835
	cmp	w24, w26
	bcc	.L836
	b	.L835
.L833:
	cmp	w28, 64
	bls	.L835
	strh	wzr, [x4,1362]
	b	.L835
.L836:
	sub	w27, w27, #1
	uxth	w27, w27
	cbnz	w27, .L837
.L835:
	cmp	w24, w26
	add	x19, x19, :lo12:.LANCHOR0
	bcs	.L838
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w1, [x19,3404]
	ldrh	w0, [x20,1384]
	cmp	w1, w0, lsr 1
	bls	.L804
	sub	w0, w1, w0, lsr 3
	strh	w0, [x19,3404]
	b	.L804
.L838:
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w2, [x19,3404]
	ldrh	w1, [x20,1384]
	ldrb	w0, [x20,1329]
	mul	w0, w1, w0
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L804
	add	w1, w2, w1, lsr 3
	strh	w1, [x19,3404]
.L804:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L839
	adrp	x0, .LC102
	ldr	w2, [x29,144]
	add	x0, x0, :lo12:.LC102
	mov	w1, w23
	mov	w3, w24
	mov	w4, w21
	bl	printk
.L839:
	mov	w0, w24
	b	.L905
.L845:
	mov	w25, 0
	mov	w24, w25
.L813:
	add	x2, x20, :lo12:.LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x2,1388]
	ldrh	w1, [x0,3384]
	cmp	w1, w2, lsr 2
	bhi	.L840
	ldrh	w0, [x0,3382]
	add	w0, w0, 8
	cmp	w1, w0
	ble	.L811
.L840:
	cmp	w25, w22
	bcs	.L811
	add	x27, x20, :lo12:.LANCHOR3
	mov	w26, 64
	add	x0, x27, 1368
	uxth	w28, w21
	str	x0, [x29,128]
.L822:
	ldrh	w2, [x27,1362]
	ldr	x0, [x29,128]
	mov	w1, w2
	str	x2, [x29,112]
	bl	_list_get_gc_head_node
	uxth	w7, w0
	mov	w1, 65535
	str	x7, [x29,120]
	cmp	w7, w1
	ldr	x2, [x29,112]
	beq	.L820
	add	w2, w2, 1
	mov	w1, 0
	strh	w2, [x27,1362]
	mov	w2, w23
	bl	gc_add_sblk
	ldr	x7, [x29,120]
	cbz	w0, .L821
	add	x0, x19, :lo12:.LANCHOR0
	ubfiz	x7, x7, 1, 16
	add	w5, w24, 1
	ldr	x0, [x0,1112]
	uxth	w24, w5
	ldrh	w0, [x0,x7]
	add	w3, w25, w0
	uxth	w25, w3
	cmp	w25, w22
	bhi	.L811
	cmp	w24, w28
	bcc	.L821
	b	.L811
.L820:
	cmp	w2, 64
	bls	.L811
	strh	wzr, [x27,1362]
	b	.L811
.L821:
	sub	w4, w26, #1
	uxth	w26, w4
	cbnz	w26, .L822
	b	.L811
.L905:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_get_gc_node
	.type	zftl_get_gc_node, %function
zftl_get_gc_node:
	stp	x29, x30, [sp, -16]!
	uxth	w1, w1
	uxth	w2, w0
	cmp	w1, 5
	add	x29, sp, 0
	bne	.L911
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1376
	b	.L914
.L911:
	cmp	w1, 2
	bne	.L913
	mov	w0, w2
	bl	zftl_get_gc_node.part.10
	b	.L915
.L913:
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1368
.L914:
	mov	w1, w2
	bl	_list_get_gc_head_node
.L915:
	uxth	w0, w0
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node, .-zftl_get_gc_node
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
	add	x0, x0, w1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L917
	add	x0, x3, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1392
	b	.L920
.L917:
	cmp	w0, 16
	add	x0, x3, :lo12:.LANCHOR3
	bne	.L919
	add	x0, x0, 1400
	add	x2, x2, 3378
	b	.L920
.L919:
	add	x0, x0, 1408
	add	x2, x2, 3380
.L920:
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
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L922
	adrp	x0, .LANCHOR3
	add	x2, x2, 3382
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1352
	b	.L925
.L922:
	cmp	w3, 96
	bne	.L924
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1368
	b	.L925
.L924:
	cmp	w3, 160
	bne	.L921
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1376
.L925:
	bl	_insert_data_list
.L921:
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
	cbnz	w22, .L927
	mov	w0, 65535
	cmp	w19, w0
	beq	.L927
	adrp	x0, .LC103
	mov	w1, w19
	add	x0, x0, :lo12:.LC103
	bl	printk
	ldr	x0, [x21,1120]
	mov	w1, -1
	strh	w1, [x0,588]
	b	.L928
.L927:
	add	x0, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldrh	w3, [x0,3378]
	ldrh	w1, [x0,3380]
	cmp	w3, w1
	bcc	.L929
	ldrh	w0, [x0,3376]
	cmp	w0, w3
	bls	.L930
	cbz	w1, .L930
.L929:
	lsr	w1, w1, 2
	cbz	w22, .L932
	mov	w1, 7
	mul	w3, w3, w1
	lsr	w1, w3, 3
.L932:
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1408
	add	x2, x2, 3380
	b	.L954
.L930:
	mov	w1, 0
	lsr	w3, w3, 3
	cmp	w22, w1
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	csel	w1, w3, w1, ne
	add	x0, x0, 1400
	add	x2, x2, 3378
.L954:
	bl	_list_pop_index_node
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L935
	add	x5, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	adrp	x0, .LC104
	mov	w1, w19
	add	x0, x0, :lo12:.LC104
	mov	w2, w23
	ldrh	w4, [x5,3376]
	ldr	x3, [x21,1392]
	ldrh	w5, [x5,3380]
	bl	printk
.L935:
	cbz	w22, .L928
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L928
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
.L928:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w22, w1
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	cmp	w22, 5
	uxth	w21, w0
	adrp	x20, .LANCHOR0
	bne	.L956
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	ldrh	w3, [x2,3378]
	ldrh	w1, [x2,3380]
	cmp	w3, w1
	bcc	.L957
	ldrh	w2, [x2,3376]
	cmp	w2, w3
	bls	.L958
	cbz	w1, .L958
.L957:
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1408
	lsr	w1, w1, 1
	b	.L987
.L958:
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, 1400
	mov	w1, 0
	add	x2, x2, 3378
	b	.L986
.L956:
	add	x23, x20, :lo12:.LANCHOR0
	cmp	w22, 1
	ldr	x0, [x23,1120]
	ldrh	w19, [x0,590]
	beq	.L960
	mov	w0, 65535
	cmp	w19, w0
	beq	.L960
	adrp	x0, .LC106
	mov	w1, w19
	add	x0, x0, :lo12:.LC106
	bl	printk
	ldr	x0, [x23,1120]
	mov	w1, -1
	strh	w1, [x0,590]
	b	.L961
.L960:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1,3376]
	ldrh	w2, [x1,3380]
	cmp	w0, w2
	bcc	.L962
	ldrh	w1, [x1,3378]
	cmp	w1, w0
	bls	.L963
	cbz	w2, .L963
.L962:
	bl	get_ink_scaned_blk
	uxth	w0, w0
	mov	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L961
	cmp	w22, 1
	bne	.L964
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,3380]
	lsr	w21, w21, 1
.L964:
	adrp	x0, .LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR3
	mov	w1, w21
	add	x0, x0, 1408
.L987:
	add	x2, x2, 3380
	b	.L986
.L963:
	lsr	w0, w0, 1
	cmp	w22, 1
	csel	w21, w0, w21, eq
	adrp	x0, .LANCHOR3
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1392
	mov	w1, w21
	add	x2, x2, 3376
.L986:
	bl	_list_pop_index_node
	uxth	w0, w0
	mov	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L961
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3+1392
	adrp	x0, .LC104
	mov	w1, w19
	add	x0, x0, :lo12:.LC104
	mov	w2, w22
	ldr	x3, [x3,#:lo12:.LANCHOR3+1392]
	ldrh	w4, [x20,3376]
	ldrh	w5, [x20,3380]
	bl	printk
.L961:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
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
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L989
	adrp	x0, .LANCHOR3
	add	x2, x2, 3382
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1352
	b	.L992
.L989:
	cmp	w3, 96
	bne	.L991
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1368
	b	.L992
.L991:
	cmp	w3, 160
	bne	.L988
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1376
.L992:
	bl	_list_remove_node
.L988:
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
	add	x0, x0, w1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L994
	add	x0, x3, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1392
	b	.L997
.L994:
	cmp	w0, 16
	add	x0, x3, :lo12:.LANCHOR3
	bne	.L996
	add	x0, x0, 1400
	add	x2, x2, 3378
	b	.L997
.L996:
	add	x0, x0, 1408
	add	x2, x2, 3380
.L997:
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
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L999
	adrp	x0, .LANCHOR3
	add	x2, x2, 3382
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1352
	b	.L1002
.L999:
	cmp	w3, 96
	bne	.L1001
	adrp	x0, .LANCHOR3
	add	x2, x2, 3384
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1368
	b	.L1002
.L1001:
	cmp	w3, 160
	bne	.L998
	adrp	x0, .LANCHOR3
	add	x2, x2, 3386
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1376
.L1002:
	bl	_list_update_data_list
.L998:
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
	cbz	x19, .L1003
	mov	x22, -6148914691236517206
	adrp	x23, .LC108
	mov	w20, 0
	adrp	x26, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC108
	mov	w27, 65535
	mov	w25, 6
	adrp	x24, .LANCHOR3
.L1006:
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
	beq	.L1003
	ldr	x0, [x21,1040]
	add	w20, w20, 1
	umull	x19, w19, w25
	uxth	w20, w20
	add	x19, x0, x19
	add	x0, x24, :lo12:.LANCHOR3
	ldrh	w0, [x0,1416]
	cmp	w0, w20
	bcs	.L1006
.L1003:
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
	add	x20, x20, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x20, 1392
	add	x1, x19, 3376
	bl	print_list_info
	add	x0, x20, 1400
	add	x1, x19, 3378
	bl	print_list_info
	add	x0, x20, 1408
	add	x1, x19, 3380
	bl	print_list_info
	add	x0, x20, 1352
	add	x1, x19, 3382
	bl	print_list_info
	add	x0, x20, 1368
	add	x1, x19, 3384
	bl	print_list_info
	add	x0, x20, 1376
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
	bls	.L1009
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
.L1009:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L1010
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L1010:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L1011
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L1011:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L1008
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L1008:
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
	add	x2, x2, w0, uxth 2
	ldrb	w12, [x2,3]
	mov	w2, 0
	mov	w6, w2
.L1014:
	add	x8, x11, :lo12:.LANCHOR3
	adrp	x4, .LANCHOR3
	ldrb	w3, [x8,1329]
	cmp	w6, w3
	bge	.L1020
	asr	w3, w12, w6
	tbnz	x3, 0, .L1015
	add	x3, x9, :lo12:.LANCHOR0
	ldrb	w5, [x8,1314]
	sbfiz	x10, x2, 1, 32
	ldrb	w7, [x3,1257]
	ldrh	w3, [x8,1312]
	sdiv	w4, w6, w5
	sub	w7, w13, w7
	sub	w3, w7, w3
	lsl	w3, w4, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,1314]
	cmp	w3, 1
	bls	.L1016
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L1016:
	add	w2, w2, 1
.L1015:
	add	w6, w6, 1
	b	.L1014
.L1020:
	mov	w0, w2
	mov	w5, -1
.L1018:
	add	x3, x4, :lo12:.LANCHOR3
	ldrb	w3, [x3,1329]
	cmp	w2, w3
	bge	.L1021
	strh	w5, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L1018
.L1021:
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
	ldrh	w19, [x5,1312]
	mov	w21, w1
	sub	w4, w0, w4
	ldrb	w0, [x5,1330]
	sub	w4, w4, w19
	mov	w19, 1
	lsl	w19, w19, w4
	asr	w22, w2, w4
	sub	w19, w19, #1
	mov	x20, x3
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L1023
	ldrb	w0, [x5,1331]
	cbnz	w0, .L1023
	ldrh	w2, [x5,1418]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L1023:
	add	x3, x20, :lo12:.LANCHOR3
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,1418]
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
.L1028:
	add	x1, x25, :lo12:.LANCHOR3
	ldrb	w0, [x1,1420]
	cmp	w19, w0
	bge	.L1039
	ldrb	w0, [x1,1314]
	mov	w20, 0
	ldrh	w3, [x1,1418]
	mov	w1, w20
	sub	w4, w0, #1
	mul	w6, w19, w0
	mul	w5, w24, w0
.L1040:
	cmp	w1, w0
	bge	.L1053
	add	w2, w1, w6
	asr	w2, w27, w2
	tbnz	x2, 0, .L1029
	and	w2, w1, w4
	add	x7, x29, 96
	add	w2, w5, w2
	mul	w2, w2, w3
	str	w2, [x7,w20,sxtw 2]
	add	w20, w20, 1
.L1029:
	add	w1, w1, 1
	b	.L1040
.L1053:
	cmp	w0, 4
	bne	.L1052
	mov	x28, 0
.L1031:
	cmp	w20, w28
	ble	.L1034
	add	x2, x29, 96
	mov	w0, w19
	mov	w1, w23
	ldr	w2, [x2,x28,lsl 2]
	add	x28, x28, 1
	bl	flash_erase_block_en
	b	.L1031
.L1052:
	cmp	w20, 2
	bne	.L1035
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w1, [x0,1330]
	cbz	w1, .L1036
	ldrb	w0, [x0,1331]
	cbnz	w0, .L1036
	cmp	w23, wzr
	ldr	w2, [x29,96]
	ldr	w3, [x29,100]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L1036:
	ldr	w2, [x29,96]
	mov	w0, w19
	ldr	w3, [x29,100]
	mov	w1, w23
	bl	flash_erase_duplane_block
	b	.L1034
.L1035:
	cmp	w20, 1
	bne	.L1034
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w1, [x0,1330]
	cbz	w1, .L1038
	ldrb	w0, [x0,1331]
	cbnz	w0, .L1038
	cmp	w23, wzr
	ldr	w2, [x29,96]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L1038:
	ldr	w2, [x29,96]
	mov	w0, w19
	mov	w1, w23
	bl	flash_erase_block_en
.L1034:
	add	w19, w19, 1
	b	.L1028
.L1039:
	add	x0, x21, :lo12:.LANCHOR0
	cbnz	w23, .L1041
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
	bge	.L1043
	strh	w0, [x1,96]
	b	.L1043
.L1041:
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
	bcs	.L1043
	strh	w0, [x1,98]
.L1043:
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
	bls	.L1055
	strh	wzr, [x1,136]
.L1055:
	ldrh	w0, [x1,112]
	cbnz	w0, .L1056
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1359
	add	x1, x1, 1800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1056:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,3392]
.L1060:
	ldrh	w2, [x1,136]
.L1057:
	cmp	w2, 63
	bgt	.L1063
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L1062
	add	w2, w2, 1
	b	.L1057
.L1063:
	strh	wzr, [x1,136]
	b	.L1060
.L1062:
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
	bls	.L1065
	strh	wzr, [x2,138]
.L1065:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L1066
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1386
	add	x1, x1, 1824
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1066:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,3392]
.L1070:
	ldrh	w2, [x1,138]
.L1067:
	cmp	w2, 63
	bgt	.L1072
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L1068
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L1068:
	add	w2, w2, 1
	b	.L1067
.L1072:
	strh	wzr, [x1,138]
	b	.L1070
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
	beq	.L1073
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,1096]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L1073
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L1077
	ldr	x1, [x23,3392]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L1078
.L1077:
	tst	w0, 24
	ldr	x1, [x23,3392]
	bne	.L1079
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L1078
.L1079:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L1078:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1080
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
	b	.L1073
.L1080:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L1081
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L1073
.L1081:
	cmp	w0, 96
	bne	.L1073
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L1073:
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
	ldrh	w0, [x4,1384]
	ldrb	w4, [x4,1329]
	mul	w0, w0, w4
	cmp	w1, w0
	bge	.L1083
	add	x3, x3, 16
	b	.L1084
.L1083:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L1084:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, w1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L1085
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1945
	add	x1, x1, 1848
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1085:
	add	x2, x19, :lo12:.LANCHOR3
	ldrh	w0, [x2,1418]
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
	add	x2, x1, w0, uxtb 1
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w2, [x2,1424]
	ldr	x4, [x1,1936]
	ldr	x5, [x1,1944]
.L1087:
	cmp	w2, w3
	beq	.L1091
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L1088
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L1089
.L1088:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L1087
.L1091:
	mov	w0, -1
.L1089:
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
	bne	.L1093
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2088
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1093:
	adrp	x20, .LANCHOR3
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w1, w0
	bne	.L1094
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2089
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1094:
	ldrh	w0, [x19,6]
	cbnz	w0, .L1095
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2090
	add	x1, x1, 1872
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1095:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR3
	add	x0, x0, 8
	ldrb	w2, [x1,1329]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L1096:
	cmp	w0, w1
	bne	.L1100
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L1097
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1097:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1096
.L1100:
	add	x20, x20, :lo12:.LANCHOR3
	ldrh	w2, [x19,2]
	ldrh	w1, [x20,1418]
	ldrb	w3, [x20,1329]
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
	bne	.L1099
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L1099:
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
	bcs	.L1102
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 2125
	add	x1, x1, 1896
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1102:
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
	ldrh	w2, [x3,1952]
	cbnz	w0, .L1104
	cmp	w2, 4
	bhi	.L1104
	add	w2, w2, 1
	strh	w2, [x3,1952]
	b	.L1103
.L1104:
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LANCHOR0
	mov	w9, 65535
	strh	wzr, [x1,1952]
	add	x1, x0, :lo12:.LANCHOR0
	ldrh	w6, [x1,1088]
	ldr	x7, [x1,1112]
	ldr	x8, [x1,1096]
	mov	x1, 0
	mov	w2, w1
	mov	w3, w1
.L1106:
	cmp	w6, w1, uxth
	bls	.L1114
	ldrh	w4, [x7,x1,lsl 1]
	cmp	w4, w9
	beq	.L1107
	add	x5, x8, x1, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1108
	add	w2, w2, w4
	b	.L1107
.L1108:
	add	w3, w3, w4
.L1107:
	add	x1, x1, 1
	b	.L1106
.L1114:
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,1120]
	ldr	x0, [x0,3392]
	str	w3, [x1,524]
	str	w2, [x1,528]
	ldrh	w0, [x0,120]
	cbz	w0, .L1103
	udiv	w2, w2, w0
	str	w2, [x1,532]
.L1103:
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
	bls	.L1116
	cmp	w0, 2
	bne	.L1117
	cbz	x2, .L1117
	mov	x19, x1
	mov	w1, 21320
	movk	w1, 0x4841, lsl 16
	mov	x0, x2
	str	w1, [x19]
	mov	w1, 1024
	bl	js_hash
	str	w0, [x19,4]
	b	.L1116
.L1117:
	str	wzr, [x1]
	str	wzr, [x1,4]
.L1116:
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
	cbnz	w2, .L1125
	mov	w0, 1
	str	w0, [x1,3412]
.L1125:
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
	beq	.L1128
	adrp	x2, .LANCHOR0
	uxtw	x0, w1
	add	x2, x2, :lo12:.LANCHOR0
	lsl	x3, x0, 1
	ldr	x4, [x2,1112]
	ldrh	w19, [x4,x3]
	cbnz	w19, .L1129
	ldr	x2, [x2,1096]
	add	x0, x2, x0, lsl 2
	mov	w2, w19
	ldrb	w3, [x0,2]
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	ubfx	x3, x3, 5, 3
	bl	printk
	b	.L1135
.L1129:
	sub	w19, w19, #1
	strh	w19, [x4,x3]
.L1128:
	adrp	x19, .LANCHOR3
	mov	w0, 0
	add	x19, x19, :lo12:.LANCHOR3
	ldrh	w2, [x19,1288]
	cmp	w2, w1
	beq	.L1130
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1131
	strh	w1, [x19,1288]
.L1135:
	mov	w0, 0
	b	.L1130
.L1131:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldrh	w3, [x19,1954]
	cset	w0, ne
	ldrh	w2, [x19,1288]
	add	w3, w3, 1
	ldr	x1, [x29,40]
	strh	w1, [x19,1288]
	uxth	w3, w3
	cmp	w3, 7
	csel	w3, w3, wzr, ls
	strh	w3, [x19,1954]
	add	x3, x19, w3, uxth 1
	strh	w2, [x3,1296]
.L1130:
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
	add	x4, x6, 1424
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,1936]
	ldr	x12, [x6,1944]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L1137:
	cmp	w4, w11
	beq	.L1141
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L1138
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR3
	bne	.L1139
	ldr	x6, [x4,1944]
	add	x4, x4, 1424
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L1140
.L1139:
	ldr	x4, [x4,1944]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L1140:
	add	x4, x3, :lo12:.LANCHOR3
	mov	w6, -1
	ldr	x4, [x4,1944]
	strh	w6, [x4,x7,lsl 1]
	b	.L1141
.L1138:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L1137
.L1141:
	add	x3, x3, :lo12:.LANCHOR3
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,1936]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 1424
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,1944]
	strh	w6, [x0,x4,lsl 1]
	beq	.L1143
	adrp	x2, .LANCHOR0+1257
	ldrh	w0, [x3,1312]
	lsr	w1, w1, w0
	ldrb	w4, [x2,#:lo12:.LANCHOR0+1257]
	mov	w2, 24
	sub	w2, w2, w4
	mov	w4, 1
	sub	w2, w2, w0
	ldrb	w0, [x3,1314]
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L1143:
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
	ldrb	w5, [x7,1314]
	uxtb	w1, w1
	cmp	w5, 1
	mul	w2, w1, w5
	uxtb	w20, w2
	ldrh	w2, [x7,1312]
	sub	w3, w3, w2
	lsr	w0, w0, w2
	lsl	w3, w4, w3
	sub	w3, w3, #1
	and	w3, w3, w0
	uxth	w4, w3
	udiv	w3, w3, w5
	uxth	w21, w3
	bls	.L1148
	sub	w5, w5, #1
	and	w5, w4, w5
	add	w2, w20, w5
	uxtb	w20, w2
.L1148:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L1149
	adrp	x0, .LC110
	mov	w2, w20
	add	x0, x0, :lo12:.LC110
	uxth	w3, w3
	bl	printk
.L1149:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,1088]
	cmp	w1, w21
	bls	.L1147
	ldr	x0, [x0,1096]
	mov	w2, 1
	add	x21, x0, w21, uxth 2
	ldrb	w0, [x21,3]
	lsl	w2, w2, w20
	orr	w2, w2, w0
	strb	w2, [x21,3]
.L1147:
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
	ldr	w0, [x0,5624]
	cbz	w0, .L1171
	adrp	x22, .LANCHOR3
	adrp	x21, .LC111
	add	x0, x21, :lo12:.LC111
	mov	x23, x22
	mov	w25, 24
	str	x0, [x29,104]
.L1172:
	add	x2, x22, :lo12:.LANCHOR3
	ldrb	w0, [x2,1329]
	cmp	w0, w27
	bls	.L1171
	add	x4, x20, :lo12:.LANCHOR0
	ldrb	w3, [x2,1314]
	ldrh	w2, [x2,1312]
	cmp	w3, 1
	ldrb	w19, [x4,1257]
	uxth	w1, w3
	sdiv	w0, w27, w3
	sub	w19, w25, w19
	sub	w19, w19, w2
	lsl	w19, w0, w19
	madd	w19, w24, w1, w19
	uxth	w19, w19
	bls	.L1156
	sub	w1, w1, #1
	and	w1, w27, w1
	add	w19, w19, w1
	uxth	w19, w19
.L1156:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w26, 0
	add	x28, x21, 3424
.L1157:
	ldr	w0, [x28,2200]
	cmp	w26, w0
	bcs	.L1173
	add	x0, x28, w26, sxtw 1
	ldrh	w0, [x0,2204]
	cmp	w0, w19
	bne	.L1158
	ldr	x0, [x29,104]
	mov	w1, w19
	bl	printk
	add	x0, x23, :lo12:.LANCHOR3
	ldrb	w1, [x0,1956]
	cbnz	w1, .L1159
	ldrb	w0, [x0,1330]
	cbz	w0, .L1160
.L1159:
	ldr	x0, [x21,3392]
	ldr	w1, [x0,156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	beq	.L1161
.L1160:
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1312]
	lsl	w0, w19, w0
	bl	ftl_mask_bad_block
.L1161:
	ldr	w1, [x28,2200]
	mov	w0, w26
.L1162:
	cmp	w0, w1
	bcs	.L1174
	add	w2, w0, 1
	add	x0, x28, w0, sxtw 1
	add	x4, x28, w2, sxtw 1
	ldrh	w4, [x4,2204]
	strh	w4, [x0,2204]
	uxth	w0, w2
	b	.L1162
.L1174:
	sub	w1, w1, #1
	str	w1, [x28,2200]
.L1158:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L1157
.L1173:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1172
.L1171:
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
	tbz	x2, 3, .L1176
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
	ldrh	w10, [x6,1958]
	add	w9, w7, w9
	cmp	w9, w10
	ble	.L1177
	ldrh	w0, [x4,3378]
	ldrh	w7, [x4,3386]
	add	w7, w7, w0
	ldrh	w0, [x6,1390]
	add	w0, w0, 8
	cmp	w7, w0
	blt	.L1183
	ldrh	w0, [x4,3376]
	ldrh	w7, [x4,3382]
	add	w7, w7, w0
	ldrh	w0, [x4,3384]
	add	w4, w7, w0
	ldrh	w0, [x6,1388]
	add	w0, w0, 8
	cmp	w4, w0
	b	.L1197
.L1177:
	madd	w7, w7, w3, w0
	ldrh	w0, [x6,1960]
	cmp	w7, w0
	ble	.L1179
	ldrh	w0, [x4,3376]
	ldrh	w7, [x4,3382]
	add	w7, w7, w0
	ldrh	w0, [x4,3384]
	add	w7, w7, w0
	ldrh	w0, [x6,1388]
	add	w9, w0, 8
	cmp	w7, w9
	blt	.L1195
	ldrh	w9, [x4,3386]
	ldrh	w4, [x4,3378]
	add	w9, w9, w4
	ldrh	w4, [x6,1390]
	add	w4, w4, 8
	cmp	w9, w4
	blt	.L1183
	add	w0, w0, 24
	cmp	w7, w0
.L1197:
	bge	.L1183
.L1195:
	and	w2, w2, -25
	b	.L1194
.L1183:
	mov	w0, 2
	bfi	w2, w0, 3, 2
.L1194:
	strb	w2, [x21,2]
.L1179:
	ldrb	w0, [x21,2]
	ands	w0, w0, 24
	bne	.L1184
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
	b	.L1185
.L1184:
	cmp	w0, 16
	bne	.L1185
	sdiv	w3, w1, w3
	ldr	w0, [x5,x19]
	add	w3, w3, w3, lsl 1
	add	w3, w8, w3, lsr 2
	bfi	w0, w3, 11, 8
	asr	w3, w1, 5
	str	w0, [x5,x19]
	bfi	w0, w3, 0, 11
	strh	w0, [x5,x19]
	b	.L1185
.L1176:
	tst	w2, 24
	bne	.L1185
	ldrh	w0, [x4,3380]
	cbnz	w0, .L1185
	ldrh	w0, [x4,3378]
	cmp	w0, 15
	bhi	.L1185
	ldrh	w1, [x4,3376]
	ldrh	w6, [x4,3382]
	add	w6, w6, w1
	ldrh	w1, [x4,3384]
	add	w6, w6, w1
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldrh	w3, [x1,1388]
	add	w3, w3, 16
	cmp	w6, w3
	ble	.L1185
	ldrh	w3, [x4,3386]
	ldrh	w1, [x1,1390]
	add	w0, w3, w0
	add	w1, w1, 8
	cmp	w0, w1
	bge	.L1185
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
.L1185:
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
	bhi	.L1188
	mov	w0, w22
	bl	gc_free_bad_sblk
.L1188:
	ldrb	w0, [x21,2]
	tbz	x0, 3, .L1189
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,1120]
	ldrh	w1, [x0,586]
	cmp	w1, w22
	bne	.L1189
	mov	w1, -1
	ldrh	w2, [x0,590]
	strh	w1, [x0,586]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L1189
	strh	w22, [x0,590]
	adrp	x0, .LC112
	mov	w1, w22
	add	x0, x0, :lo12:.LC112
	bl	printk
	b	.L1175
.L1189:
	mov	w0, w22
	bl	zftl_insert_free_list
.L1175:
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
.L1199:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L1237
	add	x0, x24, w20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,1112]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L1200
	adrp	x0, .LC113
	mov	w1, w22
	add	x0, x0, :lo12:.LC113
	bl	printk
.L1200:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x26]
	ldr	x1, [x0,1112]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L1201
	ldr	x0, [x0,1096]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L1202
	ldrb	w2, [x22,2]
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L1202:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L1215
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L1203
.L1215:
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 1363
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1203:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,1120]
	ldrh	w1, [x2,124]
	cbz	w1, .L1205
	mov	x0, 0
.L1207:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L1206
	add	x0, x2, w3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L1205
.L1206:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1207
.L1205:
	ldrh	w1, [x2,120]
	cbz	w1, .L1208
	mov	x0, 0
.L1210:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L1209
	add	x0, x2, w3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L1208
.L1209:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1210
.L1208:
	ldrh	w1, [x2,122]
	cbz	w1, .L1211
	mov	x0, 0
.L1213:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L1212
	add	x0, x2, w3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L1211
.L1212:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L1213
	b	.L1211
.L1201:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L1211:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1199
.L1237:
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
	cbnz	x0, .L1240
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 810
	add	x1, x1, 1936
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L1249
.L1240:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w1, w0
	bls	.L1245
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 817
	add	x1, x1, 1936
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1245:
	ldrb	w0, [x20,56]
	cbz	w0, .L1242
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,3360]
	cmp	w1, w0
	bcs	.L1243
.L1242:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	zbuf_free
	ldrb	w0, [x19,3388]
	b	.L1249
.L1243:
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
.L1249:
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
.L1251:
	ldrb	w2, [x19,3366]
	cmp	w2, 255
	beq	.L1266
	lsl	x0, x2, 6
	add	x24, x21, x0
	ldrb	w0, [x21,x0]
	strb	w0, [x19,3366]
	ldr	w0, [x24,52]
	cmn	w0, #1
	bne	.L1252
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	ldrb	w3, [x1,1956]
	cbnz	w3, .L1253
	ldrb	w1, [x1,1330]
	cbz	w1, .L1254
.L1253:
	ldr	x1, [x20,3392]
	ldr	w3, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w3, w1
	beq	.L1255
.L1254:
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w4, [x20,1257]
	mov	w3, 24
	add	x5, x22, x2, lsl 6
	sub	w4, w3, w4
	mov	w3, 1
	ldrh	w1, [x0,1312]
	add	x5, x5, 32
	ldrb	w0, [x0,1314]
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
	bne	.L1256
	str	w1, [x0,560]
	ldr	w3, [x5,8]
	str	w3, [x0,564]
.L1256:
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
	bne	.L1257
	strh	wzr, [x0,22]
	b	.L1255
.L1257:
	ldrh	w1, [x0,48]
	cmp	w1, w23
	bne	.L1255
	strh	wzr, [x0,54]
.L1255:
	mov	x0, x24
	mov	w23, 1
	bl	ftl_write_buf
	b	.L1251
.L1252:
	cmp	w23, 1
	add	x2, x24, 32
	bne	.L1260
	ldr	w1, [x2,4]
	adrp	x0, .LC122
	ldr	w2, [x2,8]
	add	x0, x0, :lo12:.LC122
	bl	printk
	mov	x0, x24
	bl	ftl_write_buf
	b	.L1251
.L1260:
	ldr	w0, [x2,4]
	ldr	w1, [x2,12]
	ldrh	w2, [x24,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x24,2]
	and	w23, w0, 4
	uxtb	w23, w23
	cbz	w23, .L1261
	and	w0, w0, -3
	mov	w23, 0
	strb	w0, [x24,2]
	b	.L1251
.L1261:
	mov	x0, x24
	bl	zbuf_free
	b	.L1251
.L1266:
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
	cbnz	x0, .L1268
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1150
	add	x1, x1, 1952
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	b	.L1267
.L1268:
	ldrb	w1, [x0,56]
	adrp	x0, .LANCHOR0+3360
	ldrb	w0, [x0,#:lo12:.LANCHOR0+3360]
	cmp	w1, w0
	bls	.L1271
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1157
	add	x1, x1, 1952
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1271:
	adrp	x19, .LANCHOR3
	mov	x1, x20
	add	x19, x19, :lo12:.LANCHOR3
	add	x0, x19, 1962
	bl	buf_add_tail
	ldrb	w0, [x19,1963]
	add	w0, w0, 1
	strb	w0, [x19,1963]
.L1267:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_add_read_buf, .-zftl_add_read_buf
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
	adrp	x0, .LC123
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC123
	ldrb	w1, [x20,3363]
	bl	printk
	ldrb	w19, [x20,3363]
	cmp	w19, 255
	beq	.L1273
	add	x20, x20, 1312
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC124
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC124
.L1275:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1273
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1275
.L1273:
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
	beq	.L1283
	ldrb	w5, [x3,1257]
	mov	w0, 1
	mov	w8, 24
	adrp	x13, .L1286
	sub	w8, w8, w5
	lsl	w5, w0, w5
	sub	w5, w5, #1
	asr	w10, w4, w8
	add	x3, x3, 1312
	add	x13, x13, :lo12:.L1286
	uxth	w7, w5
	adrp	x5, .LANCHOR3
	add	x5, x5, :lo12:.LANCHOR3
	and	w10, w7, w10
	ldrh	w11, [x5,1312]
	ldrb	w12, [x5,1314]
	sub	w6, w8, w11
	asr	w4, w4, w11
	lsl	w0, w0, w6
	sub	w12, w12, #1
	sub	w0, w0, #1
	uxth	w12, w12
	uxth	w9, w0
	and	w4, w4, w12
	and	w4, w4, w9
.L1289:
	add	x6, x3, x2, lsl 6
	ldr	w5, [x6,40]
	lsr	w0, w5, w8
	and	w0, w7, w0
	cmp	w0, w10
	bne	.L1284
	lsr	w5, w5, w11
	ldrb	w0, [x6,58]
	and	w5, w12, w5
	and	w5, w9, w5
	cmp	w5, w4
	bne	.L1285
	cmp	w1, 1
	beq	.L1287
	b	.L1283
.L1285:
	cmp	w1, 3
	bhi	.L1284
	ldrb	w5, [x13,w1,uxtw]
	adr	x6, .Lrtx1286
	add	x5, x6, w5, sxtb #2
	br	x5
.Lrtx1286:
	.section	.rodata
	.align	0
	.align	2
.L1286:
	.byte	(.L1283 - .Lrtx1286) / 4
	.byte	(.L1287 - .Lrtx1286) / 4
	.byte	(.L1288 - .Lrtx1286) / 4
	.byte	(.L1283 - .Lrtx1286) / 4
	.text
.L1287:
	sub	w6, w0, #7
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L1284
	b	.L1283
.L1288:
	cmp	w0, 11
	bne	.L1283
.L1284:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L1289
	mov	w0, 0
.L1283:
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
.L1299:
	cmp	w7, 255
	beq	.L1300
	sbfiz	x6, x7, 6, 32
	sxtw	x1, w7
	add	x2, x4, x6
	ldrb	w2, [x2,58]
	sub	w2, w2, #12
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1301
	cbz	w5, .L1298
	strb	w7, [x0,3363]
	b	.L1298
.L1301:
	lsl	x5, x1, 6
	ldrb	w7, [x4,x6]
	add	x2, x4, x5
	strb	w11, [x4,x5]
	ldrb	w5, [x2,59]
	cmp	w5, 1
	bne	.L1304
	ldrh	w2, [x2,50]
	cbnz	w2, .L1312
	mov	w2, w10
	mov	x5, x14
	b	.L1305
.L1304:
	cbz	w5, .L1322
.L1306:
	mov	w5, 1
	b	.L1299
.L1322:
	ldr	w2, [x2,36]
	cmn	w2, #1
	beq	.L1306
	mov	w2, w9
	mov	x5, x12
	b	.L1305
.L1312:
	mov	w2, w8
	mov	x5, x13
.L1305:
	cmp	w2, 255
	bne	.L1310
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x3, x1, lsl 6
	strb	w7, [x3,3363]
	ldrb	w0, [x1,1313]
	strb	w0, [x5]
	b	.L1298
.L1310:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x4,x2]
	cmp	w2, 255
	bne	.L1310
	add	x1, x4, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1,1]
	strb	w1, [x4,x5]
	b	.L1306
.L1300:
	cbz	w5, .L1298
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, -1
	strb	w0, [x3,3363]
.L1298:
	ret
	.size	queue_remove_completed_req, .-queue_remove_completed_req
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
.L1325:
	add	x1, x2, 1968
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1324
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1325
	mov	x0, 0
	mov	w3, 32768
.L1327:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1326
	cmp	w2, w3
	bcs	.L1326
	mov	w3, w2
	mov	w19, w4
.L1326:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1327
	cmp	w19, 32
	mov	w0, w19
	bne	.L1324
	adrp	x0, .LANCHOR3
	mov	w2, -1
	add	x0, x0, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x0, 1968
	ldrb	w3, [x0,512]
.L1329:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1328
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1328:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1329
	cmp	w19, 32
	bne	.L1330
	adrp	x0, .LC0
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 377
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1330:
	mov	w0, w19
.L1324:
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
	ldr	x2, [x1,1144]
	ldrb	w2, [x2,12]
	sdiv	w21, w21, w2
	cbnz	w0, .L1338
	mov	x0, 0
.L1339:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1339
.L1345:
	adrp	x20, .LANCHOR3
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR3
	mov	w2, 2048
	add	x20, x20, 2484
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_memset
	mov	x0, 0
	b	.L1340
.L1338:
	cmp	w0, 1
	bne	.L1341
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
.L1344:
	cmp	x0, 3
	uxth	w3, w0
	mov	w2, w3
	bls	.L1342
	ubfiz	w2, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w3, w4, w5, ne
	sub	w2, w2, w3
	uxth	w2, w2
.L1342:
	add	x3, x1, 4
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1344
	b	.L1345
.L1341:
	cmp	w0, 2
	bne	.L1346
	mov	w2, 65535
	mov	x0, 0
.L1348:
	add	x4, x1, 4
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w2, w3, hi
	add	w2, w2, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	uxth	w2, w2
	bne	.L1348
	b	.L1345
.L1346:
	cmp	w0, 3
	bne	.L1349
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
.L1352:
	cmp	x0, 5
	uxth	w3, w0
	mov	w2, w3
	bls	.L1350
	ubfiz	w2, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w3, w4, w5, ne
	sub	w2, w2, w3
	uxth	w2, w2
.L1350:
	add	x3, x1, 4
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1352
	b	.L1345
.L1349:
	cmp	w0, 4
	bne	.L1353
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
.L1355:
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
	bne	.L1355
	b	.L1345
.L1353:
	cmp	w0, 5
	bne	.L1356
	mov	x0, 0
.L1357:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1357
	mov	x0, 0
.L1358:
	add	x1, x2, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 992
	strh	w3, [x1,32]
	bne	.L1358
	b	.L1345
.L1356:
	cmp	w0, 8
	bne	.L1359
	mov	x0, 0
.L1360:
	add	x2, x1, 4
	strh	w0, [x0,x2]
	add	x0, x0, 2
	cmp	x0, 1024
	bne	.L1360
	b	.L1345
.L1359:
	cmp	w0, 9
	bne	.L1361
	add	x2, x1, 4
	mov	w0, 1
	strh	wzr, [x1,4]
	strh	w0, [x2,2]
	mov	w0, 2
	strh	w0, [x2,4]
	mov	x0, 0
.L1362:
	add	x1, x2, x0
	add	w3, w0, 3
	add	x0, x0, 2
	cmp	x0, 1018
	strh	w3, [x1,6]
	bne	.L1362
	b	.L1345
.L1361:
	cmp	w0, 10
	bne	.L1363
	mov	x0, 0
.L1364:
	add	x2, x1, 4
	strh	w0, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L1364
	mov	x0, 0
.L1365:
	add	x1, x2, x0
	add	w3, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w3, [x1,126]
	bne	.L1365
	b	.L1345
.L1363:
	cmp	w0, 11
	bne	.L1366
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1367:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1367
	mov	w5, 7
	mov	w4, 6
.L1369:
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
	bne	.L1369
	b	.L1345
.L1366:
	cmp	w0, 13
	bne	.L1345
	mov	x0, 0
	add	x3, x19, :lo12:.LANCHOR0
.L1370:
	add	x1, x3, 4
	add	w2, w0, w0, lsl 1
	strh	w2, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1370
	b	.L1345
.L1340:
	cmp	w21, w0, uxth
	bls	.L1393
	add	x1, x19, 4
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x20,w1,sxtw 1]
	b	.L1340
.L1393:
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
	tbz	x0, 12, .L1395
	adrp	x1, .LANCHOR4
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 24
	bl	printk
.L1395:
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
	strb	wzr, [x20,1153]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x25,180]
	add	x0, x20, 1248
	bl	ftl_memset
	add	x0, x25, 184
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
.L1397:
	add	x0, x20, 1264
	ldrb	w2, [x24,8]
	add	x1, x28, x23, lsl 3
	strb	w26, [x23,x0]
	mov	x0, x27
	bl	flash_mem_cmp8
	cbnz	w0, .L1396
	ldrb	w1, [x20,1153]
	add	x2, x25, 184
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	strb	w0, [x20,1153]
	uxtb	w0, w23
	add	x1, x20, w1, sxtw
	strb	w0, [x1,1248]
	bl	zftl_flash_enter_slc_mode
.L1396:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L1397
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 8
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1398
.L1402:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR5
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,21]
	ldrb	w0, [x21,1153]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,22]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,216]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1398:
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
.L1401:
	add	x1, x3, x24, lsl 3
	mov	x0, x25
	mov	w2, w26
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1399
	ldrb	w0, [x23,1153]
	add	x1, x4, 184
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x1,w0,sxtw 2]
	add	w1, w0, 1
	add	x0, x23, w0, sxtw
	strb	w1, [x23,1153]
	strb	w24, [x0,1248]
.L1399:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1401
	b	.L1402
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
	add	x0, x19, 1424
	bl	ftl_memset
	ldrh	w0, [x19,1384]
	mov	w1, 255
	ldrb	w2, [x19,1329]
	mul	w2, w0, w2
	ldr	x0, [x19,1944]
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
	tbz	x0, 12, .L1413
	adrp	x1, .LANCHOR4
	adrp	x0, .LC125
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC125
	add	x1, x1, 48
	mov	w2, 239
	mov	w3, 0
	bl	printk
.L1413:
	adrp	x19, .LANCHOR3
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR3
	mov	w2, 512
	add	x0, x19, 1424
	bl	ftl_memset
	ldrh	w0, [x19,1384]
	mov	w1, 255
	ldrb	w2, [x19,1329]
	mul	w2, w0, w2
	ldr	x0, [x19,1944]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L1414:
	ldrh	w1, [x19,1384]
	ldrb	w2, [x19,1329]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1423
	uxtw	x2, w0
	ldr	x1, [x19,1936]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1415
	uxtb	w1, w1
	add	x3, x19, 1424
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,1944]
	strh	w4, [x1,x2,lsl 1]
.L1415:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1414
.L1423:
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
	ldrb	w2, [x0,1244]
	strb	w2, [x19,7]
	ldr	w2, [x0,1032]
	str	w2, [x19]
	ldrb	w2, [x1,9]
	strb	w2, [x19,6]
	mov	w2, 32
	ldrb	w4, [x0,1153]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L1425:
	cmp	w4, w1, uxtb
	bls	.L1427
	add	x2, x0, 1248
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1425
.L1427:
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
	mov	w2, 2216
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
	strb	wzr, [x19,1345]
	strh	wzr, [x19,1386]
	strb	wzr, [x21,218]
	str	wzr, [x21,220]
	bl	ftl_memset
	ldrh	w1, [x19,1384]
	mov	w0, -1
	ldrb	w3, [x19,1329]
	strh	w0, [x20,3424]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x20,3460]
	strh	w0, [x20,3406]
	ldrh	w0, [x19,1346]
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
	strh	w1, [x21,224]
	strh	wzr, [x19,1360]
	strh	wzr, [x19,1362]
	strh	wzr, [x19,1364]
	str	xzr, [x20,3432]
	bl	ftl_malloc
	ldrb	w1, [x19,1329]
	str	x0, [x21,232]
	ldrh	w0, [x19,1346]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x21,240]
	ldrh	w1, [x19,1346]
	ldrb	w0, [x19,1329]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1336]
	ldrb	w1, [x19,1329]
	ldrh	w0, [x19,1346]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1320]
	ldrb	w1, [x19,1329]
	ldrh	w0, [x19,1346]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x21,248]
	ldrh	w0, [x19,1388]
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
	bls	.L1430
	bl	ftl_tmp_into_update
.L1430:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	x1, [x4,1120]
	ldr	w2, [x1,568]
	ldr	w3, [x1,12]
	add	w0, w2, 32768
	add	w0, w0, 3232
	cmp	w3, w0
	bcs	.L1431
	ldr	x0, [x4,3392]
	ldr	w4, [x1,572]
	ldr	w5, [x0,36]
	add	w4, w4, 256
	mov	w0, 0
	cmp	w5, w4
	bcc	.L1524
.L1431:
	add	w2, w2, 860160
	add	w2, w2, 3840
	cmp	w3, w2
	bhi	.L1433
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x2, [x0,3392]
	ldr	w0, [x1,572]
	ldr	w2, [x2,36]
	add	w0, w0, 32
	cmp	w2, w0
	bls	.L1485
.L1433:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w20, 65535
	adrp	x25, .LC126
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
	add	x25, x25, :lo12:.LC126
	ldrh	w23, [x0,134]
.L1435:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,1088]
	cmp	w0, w23
	bls	.L1546
	uxtw	x7, w23
	ldr	x0, [x4,1096]
	lsl	x1, x7, 2
	add	x6, x0, x1
	ldrb	w2, [x6,2]
	and	w3, w2, 224
	cmp	w3, 224
	beq	.L1436
	tbz	x2, 3, .L1437
	ldrh	w3, [x0,x1]
	ldr	w2, [x0,x1]
	and	w3, w3, 2047
	ubfx	x2, x2, 11, 8
	b	.L1438
.L1437:
	tst	w2, 24
	bne	.L1439
	ldrh	w3, [x0,x1]
	mov	w2, 65535
	and	w3, w3, 2047
	b	.L1438
.L1439:
	ldr	w2, [x0,x1]
	mov	w3, 65535
	ubfx	x2, x2, 11, 8
	b	.L1440
.L1438:
	ldr	w4, [x29,136]
	add	w26, w26, 1
	cmp	w20, w3
	add	w4, w4, w3
	str	w4, [x29,136]
	uxth	w26, w26
	bls	.L1483
	add	x4, x19, :lo12:.LANCHOR0
	mov	w20, w3
	ldr	x4, [x4,1120]
	strh	w23, [x4,586]
.L1483:
	cmp	w24, w3
	bcs	.L1441
	mov	w22, w23
	mov	w24, w3
.L1441:
	cmp	w2, w8
	beq	.L1442
.L1440:
	ldr	w4, [x29,140]
	add	w27, w27, 1
	cmp	w21, w2
	add	w4, w4, w2
	str	w4, [x29,140]
	uxth	w27, w27
	bls	.L1443
	add	x4, x19, :lo12:.LANCHOR0
	mov	w21, w2
	ldr	x4, [x4,1120]
	strh	w23, [x4,584]
.L1443:
	cmp	w28, w2
	bcs	.L1442
	str	w23, [x29,128]
	mov	w28, w2
.L1442:
	cmp	w3, 9
	bls	.L1487
	cmp	w2, 9
	bhi	.L1436
.L1487:
	ldr	w2, [x9,#:lo12:.LANCHOR2]
	tbz	x2, 8, .L1436
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
.L1436:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1435
.L1546:
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
	tbz	x0, 10, .L1446
	ldr	x11, [x4,1112]
	adrp	x0, .LC127
	ldrh	w2, [x10,x9]
	add	x0, x0, :lo12:.LC127
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
.L1446:
	ldrb	w0, [x8,2]
	adrp	x25, .LANCHOR5
	and	w0, w0, 224
	cmp	w0, 32
	bne	.L1447
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,688]
	cmp	w0, 2
	bls	.L1447
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, 1
	str	w1, [x0,256]
.L1447:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1448
	add	x3, x19, :lo12:.LANCHOR0
	add	x1, x25, :lo12:.LANCHOR5
	ldr	x0, [x3,3392]
	ldrh	w1, [x1,260]
	ldrh	w2, [x0,96]
	ldrh	w0, [x10,x9]
	and	w0, w0, 2047
	add	w0, w0, w1, lsr 2
	cmp	w2, w0
	ble	.L1448
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1449
	ldr	x0, [x3,1120]
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldrh	w0, [x0,586]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,5616]
	b	.L1448
.L1449:
	tst	w0, 224
	bne	.L1448
	ldr	x1, [x3,1120]
	mov	w0, 65535
	ldrh	w2, [x1,590]
	cmp	w2, w0
	bne	.L1448
	ldrh	w0, [x1,586]
	ldrh	w1, [x1,588]
	str	x3, [x29,120]
	cmp	w1, w0
	beq	.L1448
	bl	zftl_remove_free_node
	ldr	x3, [x29,120]
	ldr	x0, [x3,1120]
	ldrh	w1, [x0,586]
	strh	w1, [x0,590]
	mov	w1, -1
	strh	w1, [x0,586]
.L1448:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x0,1120]
	ldr	x10, [x0,1096]
	ldrh	w7, [x1,584]
	lsl	x9, x7, 2
	mov	x1, x7
	add	x8, x10, x9
	tbz	x2, 10, .L1450
	ldr	x11, [x0,1112]
	adrp	x0, .LC128
	ldrh	w2, [x10,x9]
	add	x0, x0, :lo12:.LC128
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
.L1450:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1451
	add	x3, x19, :lo12:.LANCHOR0
	add	x1, x25, :lo12:.LANCHOR5
	ldr	x0, [x3,3392]
	ldrh	w1, [x1,262]
	ldrh	w2, [x0,98]
	ldr	w0, [x10,x9]
	ubfx	x0, x0, 11, 8
	add	w0, w0, w1, lsr 2
	cmp	w2, w0
	ble	.L1451
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1452
	ldr	x0, [x3,1120]
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,120]
	ldrh	w0, [x0,584]
	bl	gc_add_sblk
	ldr	x3, [x29,120]
	mov	w0, 1
	strh	w0, [x3,5616]
	b	.L1451
.L1452:
	and	w0, w0, 248
	cmp	w0, 16
	bne	.L1451
	ldr	x1, [x3,1120]
	mov	w0, 65535
	ldrh	w2, [x1,588]
	cmp	w2, w0
	bne	.L1451
	ldrh	w0, [x1,584]
	ldrh	w1, [x1,590]
	str	x3, [x29,120]
	cmp	w1, w0
	beq	.L1451
	bl	zftl_remove_free_node
	ldr	x3, [x29,120]
	ldr	x0, [x3,1120]
	ldrh	w1, [x0,584]
	strh	w1, [x0,588]
	mov	w1, -1
	strh	w1, [x0,584]
.L1451:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,1096]
	tbz	x0, 10, .L1453
	uxtw	x7, w22
	ldr	x8, [x4,1112]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w7, [x8,x7,lsl 1]
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC129
	ldrb	w5, [x6,2]
	add	x0, x0, :lo12:.LC129
	ldrb	w6, [x6,3]
	mov	w1, w22
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1453:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,1096]
	tbz	x0, 10, .L1454
	ldr	w7, [x29,128]
	ldr	x8, [x4,1112]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC130
	ldrb	w5, [x6,2]
	mov	x1, x7
	ldrb	w6, [x6,3]
	add	x0, x0, :lo12:.LC130
	ldrh	w7, [x8,x7,lsl 1]
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1454:
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
	tbz	x0, 10, .L1455
	adrp	x0, .LC131
	mov	w1, w26
	add	x0, x0, :lo12:.LC131
	mov	w2, w27
	uxth	w3, w3
	uxth	w4, w4
	bl	printk
.L1455:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1456
	add	x6, x25, :lo12:.LANCHOR5
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	mov	w1, w20
	mov	w2, w21
	mov	w3, w24
	ldrh	w5, [x6,260]
	mov	w4, w28
	ldrh	w6, [x6,262]
	bl	printk
.L1456:
	add	x1, x25, :lo12:.LANCHOR5
	sub	w0, w28, w21
	str	w0, [x29,140]
	ldrh	w0, [x1,262]
	ldr	w2, [x29,140]
	cmp	w2, w0
	bgt	.L1457
	ldrh	w1, [x1,260]
	sub	w0, w24, w20
	cmp	w0, w1
	ble	.L1486
.L1457:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w26, 0
	mov	w22, w26
	ldr	x1, [x0,1120]
	ldr	x0, [x0,3392]
	ldrh	w28, [x1,580]
	ldrh	w27, [x0,134]
	sub	w0, w24, w20
	str	w0, [x29,136]
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	adrp	x24, .LC133
	str	x0, [x29,128]
	add	x0, x24, :lo12:.LC133
	str	x0, [x29,120]
.L1459:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,1088]
	cmp	w27, w0
	bcs	.L1468
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
	beq	.L1461
	tst	w0, 192
	beq	.L1461
	ubfx	x0, x0, 3, 2
	cmp	w2, 160
	and	w1, w0, 1
	tbnz	x0, 0, .L1544
	cmp	w0, 2
.L1544:
	add	x0, x25, :lo12:.LANCHOR5
	bne	.L1464
	ldrh	w0, [x0,262]
	ldr	w2, [x29,140]
	cmp	w2, w0
	ble	.L1465
	ldr	w0, [x4,x3]
	ubfx	x0, x0, 11, 8
	cmp	w0, w21
	bls	.L1466
	cbz	w1, .L1465
	ldrh	w0, [x4,x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1465
.L1466:
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
	strh	w1, [x0,5616]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1465
	ldr	x1, [x0,1096]
	ldr	x10, [x0,1112]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,120]
	ldrb	w2, [x1,2]
	b	.L1545
.L1464:
	ldrh	w0, [x0,260]
	ldr	w2, [x29,136]
	cmp	w2, w0
	ble	.L1465
	ldrh	w0, [x4,x3]
	add	w2, w20, 8
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1467
	cbz	w1, .L1465
	ldr	w0, [x4,x3]
	add	w1, w21, 4
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1465
.L1467:
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
	strh	w1, [x0,5616]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1465
	ldr	x1, [x0,1096]
	ldr	x10, [x0,1112]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,128]
	ldrb	w2, [x1,2]
.L1545:
	ldrh	w3, [x10,x24,lsl 1]
	mov	w1, w28
	ldrh	w4, [x9,56]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1465:
	cmp	w26, 4
	bhi	.L1468
	cmp	w22, 4
	bhi	.L1468
.L1461:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1459
.L1468:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	str	w28, [x0,580]
	b	.L1458
.L1486:
	mov	w26, 0
	mov	w22, w26
.L1458:
	cbz	w21, .L1471
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,134]
.L1472:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,1088]
	cmp	w2, w0
	bls	.L1547
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1096]
	add	x5, x4, x3
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w21
	bcc	.L1473
	ldrb	w5, [x5,2]
	tst	w5, 24
	beq	.L1473
	sub	w2, w2, w21
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1473:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1472
.L1547:
	ldr	x0, [x1,3392]
	ldrh	w1, [x0,72]
	add	w1, w21, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w21
	bls	.L1471
	sub	w21, w1, w21
	strh	w21, [x0,98]
.L1471:
	cbz	w20, .L1477
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w0, [x0,134]
.L1478:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,1088]
	cmp	w2, w0
	bls	.L1548
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1096]
	add	x5, x4, x3
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L1479
	ldrb	w5, [x5,2]
	and	w5, w5, 24
	cmp	w5, 16
	beq	.L1479
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1479:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1478
.L1548:
	ldr	x0, [x1,3392]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L1477
	sub	w20, w1, w20
	strh	w20, [x0,96]
.L1477:
	adrp	x20, .LANCHOR3
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR3
	add	x0, x20, 1376
	bl	_list_get_gc_head_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1434
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldrh	w2, [x20,1384]
	ldr	x3, [x19,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 1
	bhi	.L1434
	mov	w1, 0
	mov	w2, 1
	add	w22, w22, 1
	bl	gc_add_sblk
	b	.L1434
.L1485:
	mov	w26, 0
	mov	w22, w26
.L1434:
	add	w0, w26, w22
.L1524:
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
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	w24, 0
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR3
	adrp	x26, .LANCHOR4
	ldrh	w3, [x19,1088]
	adrp	x27, .LC0
	ldr	x0, [x19,1040]
	add	x26, x26, :lo12:.LANCHOR4
	add	x27, x27, :lo12:.LC0
	add	x26, x26, 72
	mul	w2, w3, w2
	bl	ftl_memset
	strh	wzr, [x19,3376]
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, 32
	strh	wzr, [x19,3378]
	strh	wzr, [x19,3380]
	strh	w1, [x0,1416]
	ldrh	w25, [x0,1346]
	ldrb	w1, [x0,1329]
	str	xzr, [x0,1392]
	str	xzr, [x0,1400]
	mul	w25, w1, w25
	str	xzr, [x0,1408]
	str	xzr, [x0,1368]
	str	xzr, [x0,1352]
	str	xzr, [x0,1376]
	mov	w0, 32768
	strh	wzr, [x19,3382]
	sdiv	w25, w0, w25
	strh	wzr, [x19,3386]
	strh	wzr, [x19,3384]
	sxth	w0, w25
	str	w0, [x29,108]
	ldr	x0, [x19,3392]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1550:
	add	x2, x23, :lo12:.LANCHOR0
	ldrh	w0, [x2,1088]
	cmp	w19, w0
	bge	.L1581
	sxtw	x25, w19
	ldr	x20, [x2,1096]
	ldr	w0, [x29,108]
	add	x20, x20, x25, lsl 2
	ldrb	w1, [x20,3]
	cbz	w1, .L1551
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, 0
	ldr	x4, [x2,3392]
	ldrb	w5, [x0,1329]
	ldrh	w6, [x0,1346]
	mov	w0, w1
.L1552:
	cmp	w1, w5
	bge	.L1582
	ldrb	w2, [x20,3]
	asr	w2, w2, w1
	tbnz	x2, 0, .L1553
	add	w0, w6, w0
	sxth	w0, w0
	b	.L1554
.L1553:
	ldrh	w2, [x4,146]
	add	w2, w2, 1
	strh	w2, [x4,146]
.L1554:
	add	w1, w1, 1
	b	.L1552
.L1582:
	cbz	w0, .L1556
	mov	w1, 32768
	sdiv	w0, w1, w0
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1551
.L1556:
	ldrb	w1, [x20,2]
	mov	w2, -1
	orr	w1, w1, -32
	strb	w1, [x20,2]
	add	x1, x23, :lo12:.LANCHOR0
	ldr	x1, [x1,1112]
	strh	w2, [x1,x25,lsl 1]
.L1551:
	add	x22, x23, :lo12:.LANCHOR0
	mov	w1, 6
	smull	x1, w19, w1
	ldr	x2, [x22,1040]
	add	x2, x2, x1
	strh	w0, [x2,4]
	mov	w2, -1
	ldr	x0, [x22,1040]
	add	x4, x0, x1
	strh	w2, [x4,2]
	strh	w2, [x0,x1]
	ldrb	w0, [x20,2]
	and	w0, w0, 224
	cmp	w0, 224
	beq	.L1557
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1557
	ldr	x1, [x22,1120]
	ldrh	w2, [x1,16]
	cmp	w19, w2
	beq	.L1557
	ldrh	w2, [x1,48]
	cmp	w19, w2
	beq	.L1557
	ldrh	w1, [x1,80]
	cmp	w19, w1
	beq	.L1557
	cmp	w0, 64
	bne	.L1558
	uxth	w20, w19
	add	x0, x21, :lo12:.LANCHOR3
	add	x0, x0, 1352
	mov	w1, w20
	add	x2, x22, 3382
	b	.L1579
.L1558:
	cmp	w0, 96
	bne	.L1559
	uxth	w20, w19
	add	x0, x21, :lo12:.LANCHOR3
	add	x0, x0, 1368
	mov	w1, w20
	add	x2, x22, 3384
	b	.L1579
.L1559:
	cmp	w0, 160
	bne	.L1560
	uxth	w20, w19
	add	x0, x21, :lo12:.LANCHOR3
	add	x0, x0, 1376
	mov	w1, w20
	add	x2, x22, 3386
.L1579:
	bl	_insert_data_list
	ldr	x0, [x22,1112]
	ldrh	w0, [x0,x25,lsl 1]
	cmp	w0, 7
	bhi	.L1557
	mov	w0, w20
	mov	w1, 1
	mov	w2, w28
	b	.L1577
.L1560:
	cbnz	w0, .L1557
	ldr	x0, [x22,1112]
	ldrh	w2, [x0,x25,lsl 1]
	cbz	w2, .L1561
	cmp	w24, 2
	bgt	.L1562
	adrp	x0, .LC135
	mov	w1, w19
	add	x0, x0, :lo12:.LC135
	add	w24, w24, 1
	bl	printk
	sxth	w24, w24
	ldrb	w0, [x20,2]
	tbz	x0, 4, .L1563
	mov	w1, 5
	b	.L1573
.L1563:
	mov	w1, 2
.L1573:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x20,2]
	mov	w1, 1
	mov	w0, w19
.L1577:
	bl	gc_add_sblk
	b	.L1557
.L1562:
	mov	x1, x26
	mov	w2, 656
	mov	x0, x27
	bl	printk
	bl	dump_stack
.L1561:
	ldrb	w0, [x20,2]
	ands	w0, w0, 24
	bne	.L1565
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x23, :lo12:.LANCHOR0
	add	x0, x0, 1392
	mov	w1, w19
	add	x2, x2, 3376
	b	.L1575
.L1565:
	cmp	w0, 16
	add	x2, x23, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	bne	.L1566
	add	x0, x0, 1400
	mov	w1, w19
	add	x2, x2, 3378
	b	.L1575
.L1566:
	add	x0, x0, 1408
	mov	w1, w19
	add	x2, x2, 3380
.L1575:
	bl	_insert_free_list
.L1557:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1550
.L1581:
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
	ldrh	w1, [x1,264]
	cmp	w1, 128
	bls	.L1584
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 94
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1584:
	adrp	x20, .LANCHOR0
	mov	x1, 0
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, w1
	mov	w6, 65535
	ldr	x3, [x0,3392]
	add	x0, x19, :lo12:.LANCHOR5
	ldrh	w4, [x3,688]
	ldrh	w5, [x0,264]
.L1587:
	add	x7, x3, x1, lsl 1
	uxth	w0, w1
	ldrh	w7, [x7,416]
	cmp	w7, w6
	beq	.L1585
	add	w2, w2, 1
	uxth	w2, w2
.L1585:
	cmp	w2, w4
	bcs	.L1586
	cmp	w2, w5
	bcs	.L1586
	add	x1, x1, 1
	cmp	x1, 128
	bne	.L1587
	mov	w0, w1
.L1586:
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
	ldrh	w6, [x1,1312]
	sub	w2, w0, w2
	mov	w0, 1
	ldrb	w8, [x1,1314]
	sub	w2, w2, w6
	ldrh	w7, [x4,698]
	lsl	w2, w0, w2
	sub	w2, w2, #1
.L1588:
	cmp	w7, w3, uxth
	bls	.L1616
	add	x0, x4, x3, lsl 2
	mov	x1, 0
	ldr	w0, [x0,704]
	lsr	w0, w0, w6
	and	w0, w0, w2
	udiv	w0, w0, w8
	uxth	w0, w0
.L1589:
	ldr	w5, [x29,124]
	cmp	w5, w1, uxth
	bls	.L1617
	add	x5, x4, x1, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w0
	bne	.L1590
	ldrh	w5, [x22,x1,lsl 1]
	add	w5, w5, 1
	strh	w5, [x22,x1,lsl 1]
.L1590:
	add	x1, x1, 1
	b	.L1589
.L1617:
	add	x3, x3, 1
	b	.L1588
.L1616:
	add	x1, x23, :lo12:.LANCHOR3
	mov	x19, 0
	mov	w28, 65535
	adrp	x25, .LC136
	adrp	x26, .LC137
	mov	w24, w19
	ldrb	w0, [x1,1329]
	mov	w27, w28
	ldrh	w21, [x1,1384]
	add	x25, x25, :lo12:.LC136
	add	x26, x26, :lo12:.LC137
	mul	w21, w0, w21
	uxth	w21, w21
.L1593:
	ldr	w0, [x29,124]
	uxth	w6, w19
	cmp	w6, w0
	bcs	.L1618
	add	x0, x20, :lo12:.LANCHOR0
	add	x2, x23, :lo12:.LANCHOR3
	mov	w8, w19
	ldr	x0, [x0,3392]
	add	x1, x0, w19, sxtw 1
	ldrb	w4, [x2,1314]
	ldrh	w3, [x1,416]
	ldrh	w1, [x0,692]
	sdiv	w1, w1, w4
	cmp	w1, w3
	bne	.L1594
	ldrb	w1, [x2,1329]
	ldrh	w2, [x2,1384]
	mul	w1, w1, w2
	strh	w1, [x22,x19,lsl 1]
.L1594:
	ldrh	w2, [x22,x19,lsl 1]
	cmp	w21, w2
	bls	.L1595
	cmp	w2, wzr
	csel	w24, w24, w6, eq
	csel	w21, w21, w2, eq
.L1595:
	cmp	w3, w27
	beq	.L1597
	ldrh	w1, [x0,74]
	cmp	w1, 2
	bls	.L1597
	add	x4, x20, :lo12:.LANCHOR0
	ubfiz	x1, x3, 2, 16
	ldrh	w0, [x0,92]
	ldr	x4, [x4,1096]
	add	w0, w0, 4
	ldrh	w4, [x4,x1]
	and	w4, w4, 2047
	cmp	w4, w0
	bgt	.L1597
	mov	w1, w8
	mov	x0, x25
	str	x6, [x29,104]
	str	x8, [x29,112]
	bl	printk
	ldr	x6, [x29,104]
	ldr	x8, [x29,112]
	mov	w28, w6
.L1597:
	ldrh	w2, [x22,x19,lsl 1]
	cbnz	w2, .L1598
	add	x0, x20, :lo12:.LANCHOR0
	sxtw	x6, w8
	ldr	x4, [x0,3392]
	add	x0, x6, 208
	ldrh	w3, [x4,x0,lsl 1]
	cmp	w3, w27
	beq	.L1598
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1599
	ldrh	w4, [x4,688]
	mov	x0, x26
	mov	w1, w8
	str	x6, [x29,112]
	bl	printk
	ldr	x6, [x29,112]
.L1599:
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
.L1598:
	add	x19, x19, 1
	b	.L1593
.L1618:
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
	tbz	x0, 12, .L1621
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 120
	add	x2, x2, 112
	bl	printk
.L1621:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	ldr	x0, [x19,1128]
	bl	ftl_memset
	ldr	x1, [x19,1128]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,1128]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	add	x1, x19, 1160
	bl	ftl_memcpy
	ldr	x0, [x19,1128]
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
.L1626:
	cmp	w2, w3
	bls	.L1628
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1626
.L1628:
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
	adrp	x0, .LANCHOR5+272
	ldr	x0, [x0,#:lo12:.LANCHOR5+272]
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
	adrp	x0, .LANCHOR5+280
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
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
	adrp	x1, .LANCHOR5+280
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR5+280]
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
	adrp	x3, .LANCHOR5+280
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR5+280]
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
	adrp	x3, .LANCHOR5+280
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR5+280]
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
	adrp	x4, .LANCHOR5+288
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR5+288]
	add	x4, x4, 4096
	cbnz	w2, .L1636
	add	x1, x4, x1
	b	.L1638
.L1636:
	add	x0, x4, x1
	mov	x1, x6
.L1638:
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
	adrp	x0, .LC139
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC139
	bl	printk
	adrp	x0, .LANCHOR5+280
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
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
	adrp	x0, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
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
	adrp	x1, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+280]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+280]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR5+280
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR5+280]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR5+280]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR5+280]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR5+280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR5+280]
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
	adrp	x0, .LC140
	mov	w2, 0
	add	x29, sp, 0
	mov	x5, x1
	add	x0, x0, :lo12:.LC140
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
	ldr	x25, [x0,1128]
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
	bne	.L1653
	mov	w0, -89
	strb	w0, [x22,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1716
.L1653:
	cmp	w19, 3
	bne	.L1655
	mov	x5, 0
.L1656:
	sub	w0, w5, #80
	strb	w0, [x26,x5]
	add	x5, x5, 1
	cmp	x5, 8
	bne	.L1656
	mov	w27, w5
	mov	w28, w5
	b	.L1654
.L1655:
	cmp	w19, 4
	bne	.L1657
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
	b	.L1654
.L1657:
	cmp	w19, 5
	bne	.L1658
	mov	w0, 56
	strb	w0, [x25,128]
	mov	w0, 57
	strb	w0, [x25,129]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x25,130]
	mov	w0, 59
	strb	w0, [x25,131]
	b	.L1758
.L1658:
	cmp	w19, 6
	bne	.L1659
	mov	w0, 14
	strb	w0, [x25,128]
	mov	w0, 15
	strb	w0, [x25,129]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x25,130]
	mov	w0, 17
	strb	w0, [x25,131]
	b	.L1758
.L1659:
	cmp	w19, 7
	bne	.L1660
	mov	x0, 0
.L1661:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1661
	mov	w0, -44
	mov	w27, 12
	strb	w0, [x25,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x25,137]
	b	.L1654
.L1660:
	cmp	w19, 8
	bne	.L1716
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
	b	.L1654
.L1716:
	mov	w27, 7
.L1758:
	mov	w28, 4
.L1654:
	sub	w0, w19, #1
	cmp	w0, 1
	bhi	.L1755
	ldr	x0, [x29,136]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w5, 55
	add	x24, x24, 408
.L1662:
	ldrb	w0, [x19,1153]
	cmp	w0, w23
	bls	.L1669
	add	x0, x19, w23, sxtw
	ldrb	w0, [x0,1248]
	mov	x20, 160
	ldr	x21, [x19,1048]
	ubfiz	x1, x0, 8, 8
	mov	x25, 0
	madd	x20, x0, x20, x22
	add	x21, x21, x1
	add	x20, x20, 32
.L1664:
	str	w5, [x21,2056]
	str	x5, [x29,136]
	ldrb	w0, [x26,x25]
	str	w0, [x21,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldr	w0, [x21,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	ldr	x5, [x29,136]
	cmp	w28, w25, uxtb
	bhi	.L1664
	mov	x0, 0
.L1665:
	add	w2, w0, 8
	mov	x1, 0
.L1666:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w4, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w4, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1666
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1665
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1662
.L1755:
	sub	w0, w19, #3
	cmp	w0, 5
	bhi	.L1669
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
.L1670:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w0, [x1,1153]
	cmp	w0, w20
	bhi	.L1715
.L1669:
	strb	w28, [x22,1]
	strb	w27, [x22,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L1715:
	add	x0, x1, w20, sxtw
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
	bne	.L1671
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
	b	.L1761
.L1671:
	mov	w0, 54
	cmp	w19, 4
	str	w0, [x24,2056]
	bne	.L1673
	mov	w0, 64
	str	w1, [x24,2052]
	str	w0, [x24,2048]
	mov	w0, 204
	b	.L1759
.L1673:
	ldr	w0, [x29,132]
	cmp	w0, 1
	bhi	.L1675
	ldrb	w0, [x25,128]
	str	w0, [x24,2052]
	mov	w0, 82
	b	.L1760
.L1675:
	cmp	w19, 7
	bne	.L1674
	mov	w0, 174
	str	w0, [x24,2052]
	str	wzr, [x24,2048]
	mov	w0, 176
.L1759:
	str	w0, [x24,2052]
	mov	w0, 77
.L1760:
	str	w0, [x24,2048]
.L1674:
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
	bne	.L1676
	mov	w0, 31
	str	w0, [x24,2052]
	b	.L1677
.L1676:
	str	wzr, [x24,2052]
.L1677:
	mov	w0, 2
.L1761:
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
	cbnz	w8, .L1718
	cbnz	w7, .L1718
	cmp	w19, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L1678
.L1718:
	mov	w1, 16
.L1678:
	adrp	x0, .LANCHOR5
	mov	x9, 0
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,296]
.L1679:
	ldr	w10, [x24,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L1679
	cbz	w7, .L1680
	mov	w1, 0
.L1682:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L1681
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L1681
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1682
	b	.L1683
.L1681:
	cmp	w1, 7
	bne	.L1684
.L1683:
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1685:
	b	.L1685
.L1680:
	cmp	w19, 7
	bne	.L1686
	mov	w1, w7
.L1688:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L1687
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L1687
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1688
	b	.L1689
.L1687:
	cmp	w1, 7
	bne	.L1684
.L1689:
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1690:
	b	.L1690
.L1686:
	cmp	w19, 6
	bne	.L1684
	mov	x1, 0
.L1691:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L1684
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L1684
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1691
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1693:
	b	.L1693
.L1684:
	add	x0, x26, :lo12:.LANCHOR5
	ldr	x10, [x0,296]
	mov	x0, 0
.L1694:
	ldr	w1, [x29,128]
	cmp	w1, w0
	ble	.L1763
	ldr	w1, [x24,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L1694
.L1763:
	add	x0, x26, :lo12:.LANCHOR5
	mov	w11, w23
	mov	w9, 8
	ldr	x12, [x0,296]
.L1697:
	mov	w0, 0
.L1696:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L1696
	ldr	w0, [x29,124]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L1697
	mov	x9, 0
	mov	w17, 1
.L1698:
	mov	w1, 0
	mov	w11, w1
.L1702:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L1700:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L1700
	cmp	w14, 8
	bls	.L1701
	orr	w1, w1, w13
	uxth	w1, w1
.L1701:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L1702
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L1698
	add	x1, x26, :lo12:.LANCHOR5
	mov	x9, 0
	mov	w11, w9
	ldr	x1, [x1,296]
.L1705:
	ldr	w12, [x1,x9]
	add	x9, x9, 4
	cmp	w12, wzr
	csinc	w11, w11, w11, ne
	cmp	x9, 32
	bne	.L1705
	cmp	w11, 7
	ble	.L1706
	adrp	x0, .LC142
	mov	w2, 1
	add	x0, x0, :lo12:.LC142
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1707:
	b	.L1707
.L1706:
	cmp	w19, 6
	mov	w9, 4
	beq	.L1708
	cmp	w19, 7
	mov	w9, 10
	beq	.L1708
	cmp	w7, wzr
	mov	w9, 5
	mov	w1, 8
	csel	w9, w1, w9, eq
.L1708:
	mov	w11, 0
.L1709:
	mov	x1, 0
.L1710:
	add	w12, w0, w1
	ldrb	w13, [x10,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w13, [x21,w12,sxtw]
	bhi	.L1710
	add	w11, w11, 1
	ldr	x1, [x29,112]
	cmp	w11, w27
	add	w0, w0, w9
	add	x10, x10, x1
	blt	.L1709
	mov	w21, 255
	str	w21, [x24,2056]
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	nandc_wait_flash_ready
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L1712
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
	b	.L1762
.L1712:
	mov	w0, 190
	cbnz	w7, .L1762
	mov	w0, 56
.L1762:
	str	w0, [x24,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1670
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
	bhi	.L1764
	bl	hynix_get_read_retry_default
.L1764:
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
	strb	wzr, [x0,304]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1028]
	cmp	w1, 9
	bne	.L1767
	ldr	x22, [x0,1048]
	ldr	w0, [x22,16]
	str	w0, [x29,64]
	ldr	w20, [x22,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1788
	adrp	x21, .LC145
	adrp	x23, .LC146
	add	x21, x21, :lo12:.LC145
	add	x23, x23, :lo12:.LC146
	b	.L1769
.L1788:
	mov	w20, 0
.L1768:
	ldr	w1, [x22,64]
	ldr	w0, [x29,64]
	ubfx	x1, x1, 16, 6
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1771
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1770
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1770
	ldr	w1, [x29,72]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1771
.L1770:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1772
	ldr	w2, [x22,64]
	adrp	x0, .LC144
	ldr	w3, [x29,64]
	mov	w1, w20
	add	x0, x0, :lo12:.LC144
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	add	x21, x21, :lo12:.LANCHOR5
	bl	printk
	ldr	w1, [x29,72]
	mov	w0, 1
	strb	w0, [x21,304]
	tbnz	x1, 13, .L1771
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	b	.L1771
.L1772:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1768
.L1771:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1200
	ldr	w0, [x20,32]
	cbz	w0, .L1776
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
	b	.L1813
.L1778:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,1048]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1777
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
.L1777:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1769:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1778
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1200
	ldr	w0, [x20,32]
	cbz	w0, .L1776
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
	b	.L1812
.L1767:
	ldr	x21, [x0,1048]
	ldr	w0, [x21,8]
	str	w0, [x29,64]
	ldr	w20, [x21,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1789
	adrp	x22, .LC145
	adrp	x23, .LC146
	add	x22, x22, :lo12:.LC145
	add	x23, x23, :lo12:.LC146
	b	.L1781
.L1789:
	adrp	x22, .LC144
	adrp	x23, .LC146
	mov	w20, 0
	add	x22, x22, :lo12:.LC144
	add	x23, x23, :lo12:.LC146
.L1780:
	ldr	w2, [x21,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1783
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1048]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1782
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1782
	ldr	w1, [x29,72]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
	b	.L1783
.L1782:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1784
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
.L1784:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L1780
.L1783:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1200
	ldr	w0, [x20,32]
	cbz	w0, .L1776
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
.L1813:
	mov	w2, 0
	b	.L1811
.L1787:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,1048]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1786
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
.L1786:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
.L1781:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1787
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 1200
	ldr	w0, [x20,32]
	cbz	w0, .L1776
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
.L1812:
	mov	w2, 1
.L1811:
	bl	rknand_dma_unmap_single
.L1776:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,1232]
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
	cbnz	w20, .L1844
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,1028]
	cmp	w2, 9
	bne	.L1816
	ldr	x5, [x1,1048]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1817:
	cmp	w3, w22
	bcs	.L1853
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
	tbnz	x1, 2, .L1836
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1836
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1819
	ubfx	x1, x1, 3, 7
	b	.L1820
.L1819:
	ubfx	x1, x1, 19, 7
.L1820:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1818
.L1836:
	mov	w0, -1
.L1818:
	add	w3, w3, 1
	b	.L1817
.L1853:
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
	bne	.L1823
	adrp	x0, .LC148
	mov	w1, w20
	add	x0, x0, :lo12:.LC148
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,1048]
	str	w20, [x0]
	mov	w0, -1
.L1823:
	tbz	x20, 13, .L1824
	adrp	x1, .LANCHOR5+304
	ldrb	w1, [x1,#:lo12:.LANCHOR5+304]
	cbz	w1, .L1824
	adrp	x0, .LC149
	mov	w1, w20
	add	x0, x0, :lo12:.LC149
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,1048]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1825
.L1816:
	ldrb	w0, [x1,1244]
	mov	w4, 128
	lsr	w5, w22, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
	mov	w3, w20
.L1827:
	cmp	w3, w5
	add	w6, w20, w4
	bcs	.L1854
	ldr	x0, [x1,1200]
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
	b	.L1827
.L1854:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,1048]
	mov	w0, w2
.L1829:
	cmp	w2, w22
	bcs	.L1855
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1839
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1839
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
	bls	.L1831
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1852
.L1831:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1852:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1830
.L1839:
	mov	w0, -1
.L1830:
	add	w2, w2, 1
	b	.L1829
.L1855:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,1048]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1824
	adrp	x0, .LC150
	mov	w1, w20
	add	x0, x0, :lo12:.LC150
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,1048]
	str	w20, [x0]
	b	.L1825
.L1824:
	cmn	w0, #1
	beq	.L1844
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1844
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1844
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1844
.L1825:
	mov	w0, -1
.L1844:
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
	cbnz	w23, .L1857
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	b	.L1858
.L1857:
	ldr	x0, [x27,1144]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1859
	ldrb	w0, [x27,1268]
	cbnz	w0, .L1859
	ldrb	w0, [x27,1269]
	cbnz	w0, .L1859
	sxtw	x0, w22
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w23, [x0,8]
	b	.L1858
.L1859:
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
.L1858:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,1144]
	ldrb	w0, [x1,7]
	cmp	w0, 1
	bne	.L1860
	ldrb	w0, [x1,12]
	cmp	w0, 2
	bne	.L1860
	sxtw	x0, w22
	mov	w2, 38
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
.L1860:
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
	cbz	w0, .L1861
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L1861:
	mov	w0, 48
	str	w0, [x20,2056]
	cbz	w23, .L1862
	ldrb	w0, [x1,12]
	cmp	w0, 3
	bne	.L1862
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1268]
	cbnz	w0, .L1862
	ldrb	w0, [x21,1269]
	cbnz	w0, .L1862
	add	w19, w19, w19, lsl 1
	sub	w0, w19, #1
	add	w0, w0, w23
	b	.L1873
.L1862:
	mov	w0, w19
.L1873:
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
	ldrb	w0, [x20,1244]
	mov	x24, x3
	uxtb	x25, w19
	mov	w21, 0
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	str	w0, [x29,156]
	bl	nandc_wait_flash_ready
	ldr	x0, [x20,1048]
	str	x0, [x29,144]
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	str	x0, [x29,128]
	lsl	x0, x25, 8
	str	x0, [x29,120]
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	str	x0, [x29,112]
.L1875:
	mov	w0, w19
	mov	w20, 0
	mov	w28, -1
	adrp	x26, .LANCHOR5
	bl	zftl_flash_enter_slc_mode
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	lsl	x0, x25, 8
	str	x0, [x29,136]
.L1876:
	add	x0, x26, :lo12:.LANCHOR5
	ldrb	w0, [x0,320]
	cmp	w20, w0
	bcs	.L1880
	ldr	x0, [x29,144]
	add	w27, w20, 1
	ldr	x1, [x29,136]
	add	x1, x0, x1
	mov	w0, 239
	str	x1, [x29,104]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	x0, 1000
	bl	__const_udelay
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
	tbz	x0, 12, .L1877
	ldr	x0, [x29,128]
	mov	w4, w6
	mov	w1, w20
	mov	w2, w22
	mov	w3, w28
	str	x6, [x29,104]
	bl	printk
	ldr	x6, [x29,104]
.L1877:
	cmn	w6, #1
	beq	.L1878
	adrp	x0, .LANCHOR5
	cmn	w28, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w28, w28, w6, ne
	ldr	x23, [x0,296]
	ldr	x24, [x0,312]
	ldr	w0, [x29,156]
	cmp	w6, w0
	bcc	.L1888
.L1878:
	mov	w20, w27
	b	.L1876
.L1888:
	mov	w28, w6
.L1880:
	ldr	x0, [x29,144]
	ldr	x1, [x29,120]
	add	x26, x0, x1
	mov	w0, 239
	str	w0, [x26,2056]
	mov	w0, 137
	str	w0, [x26,2052]
	mov	x0, 1000
	bl	__const_udelay
	str	wzr, [x26,2048]
	str	wzr, [x26,2048]
	ldr	w0, [x29,156]
	str	wzr, [x26,2048]
	str	wzr, [x26,2048]
	cmp	w28, w0
	bcc	.L1882
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1882:
	cmn	w28, #1
	cset	w26, eq
	cbnz	w26, .L1890
	cmp	w28, 256
	cset	w1, eq
	cbz	w1, .L1883
.L1890:
	ldr	x0, [x29,112]
	mov	w1, w19
	mov	w2, w22
	mov	w3, w20
	mov	w4, w28
	bl	printk
	cbz	w26, .L1885
	cbnz	w21, .L1885
	mov	w0, w19
	mov	w1, 3
	mov	w21, 1
	bl	mt_auto_read_calibration_config
	b	.L1875
.L1885:
	cbz	w21, .L1886
	mov	w0, w19
	mov	w1, 0
	bl	mt_auto_read_calibration_config
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
	b	.L1886
.L1883:
	cbz	w21, .L1886
	mov	w0, w19
	mov	w28, 256
	bl	mt_auto_read_calibration_config
.L1886:
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
	mov	w23, w1
	uxtb	w24, w0
	stp	x27, x28, [sp,80]
	mov	x26, x2
	mov	x27, x3
	str	w4, [x29,124]
	adrp	x20, .LANCHOR0
	bl	nandc_wait_flash_ready
	ubfx	x19, x23, 24, 2
	add	x1, x20, :lo12:.LANCHOR0
	sxtw	x0, w24
	mov	w2, 46
	mov	w3, 56
	add	x0, x0, 8
	mov	w21, 10
	ldr	x25, [x1,1048]
	lsl	x0, x0, 8
	ldrb	w1, [x1,1136]
	add	x22, x25, x0
	cmp	w1, 36
	csel	w2, w3, w2, ne
	str	w2, [x29,112]
	cmp	w1, 36
	mov	w1, 26
	csel	w21, w21, w1, ne
	cbnz	w19, .L1911
	str	x0, [x29,112]
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	mov	w19, -1
	mov	w28, 1
	str	x0, [x29,104]
.L1918:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 36
	mov	x0, x22
	bne	.L1912
	mov	w1, w28
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x25, x0
	str	w1, [x0,8]
	b	.L1913
.L1912:
	mov	w1, w28
	bl	toshiba_3d_set_slc_rr_para
.L1913:
	ldr	w4, [x29,124]
	mov	w0, w24
	mov	w1, w23
	mov	x2, x26
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1914
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w28
	mov	w2, w23
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1914:
	cmn	w4, #1
	beq	.L1915
	adrp	x0, .LANCHOR5
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w19, w19, w4, ne
	ldr	x26, [x0,296]
	ldr	x27, [x0,312]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1934
.L1915:
	add	w28, w28, 1
	cmp	w28, w21
	bne	.L1918
	b	.L1917
.L1934:
	mov	w21, w28
	mov	w19, w4
.L1917:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 36
	mov	x0, x22
	bne	.L1919
	mov	w1, 0
	mov	w2, w1
	b	.L1957
.L1919:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1920
.L1911:
	mov	x28, x0
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,104]
.L1927:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 36
	mov	x0, x22
	bne	.L1921
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x25, x28
	mov	w1, 93
	b	.L1956
.L1921:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x25, x28
	mov	w1, 38
.L1956:
	str	w1, [x0,8]
	mov	x2, x26
	mov	w0, w24
	mov	w1, w23
	ldr	w4, [x29,124]
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1923
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w23
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1923:
	cmn	w4, #1
	beq	.L1924
	adrp	x0, .LANCHOR5
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w19, w19, w4, ne
	ldr	x26, [x0,296]
	ldr	x27, [x0,312]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1935
.L1924:
	ldr	w0, [x29,112]
	add	w21, w21, 1
	cmp	w21, w0
	bne	.L1927
	b	.L1926
.L1935:
	mov	w19, w4
.L1926:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 36
	mov	x0, x22
	bne	.L1928
	mov	w1, 0
	mov	w2, 1
.L1957:
	bl	toshiba_tlc_set_rr_para
	b	.L1920
.L1928:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1920:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 36
	bne	.L1929
	ubfiz	x0, x24, 8, 8
	add	x25, x25, x0
	mov	w0, 85
	str	w0, [x25,2056]
	mov	w0, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2048]
	str	w0, [x25,2056]
.L1929:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,1244]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1930
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1930:
	cmn	w19, #1
	beq	.L1936
	cmp	w19, 256
	bne	.L1931
.L1936:
	adrp	x0, .LC155
	mov	w1, w24
	add	x0, x0, :lo12:.LC155
	mov	w2, w23
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1931:
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
	ldrb	w0, [x1,1136]
	ldr	x23, [x1,1048]
	sub	w0, w0, #67
	add	x22, x23, x22, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1959
	ldrb	w0, [x1,1192]
	cbz	w0, .L1960
	mov	w0, 1
	mov	w25, 1
	bl	nandc_set_if_mode
.L1960:
	ubfiz	x0, x21, 8, 8
	mov	w1, 92
	add	x0, x23, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1959:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x21, 8, 8
	str	x0, [x29,96]
.L1961:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0,320]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1992
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,1136]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x22
	bhi	.L1962
	bl	sandisk_set_rr_para
	b	.L1963
.L1962:
	bl	toshiba_set_rr_para
.L1963:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	cmp	w0, 34
	bne	.L1964
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0,320]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1964
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x23, x0
	str	w1, [x0,8]
.L1964:
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
	beq	.L1967
	cmn	w24, #1
	csel	w24, w24, w0, ne
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x26, [x0,296]
	ldr	x27, [x0,312]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1969
.L1967:
	add	w20, w20, 1
	b	.L1961
.L1992:
	mov	w28, w24
.L1969:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,1136]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x22
	bhi	.L1971
	bl	sandisk_set_rr_para
	b	.L1972
.L1971:
	bl	toshiba_set_rr_para
.L1972:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x23, x23, x0, lsl 8
	mov	w0, 255
	str	w0, [x23,8]
	ldrb	w0, [x19,1244]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1973
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1973:
	cmn	w28, #1
	beq	.L1979
	cmp	w28, 256
	bne	.L1974
.L1979:
	adrp	x0, .LC155
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC155
	mov	w1, w20
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1974:
	bl	nandc_wait_flash_ready
	cbz	w25, .L1976
	mov	w0, 4
	bl	nandc_set_if_mode
.L1976:
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
	cbnz	w0, .L2003
.L1998:
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
	beq	.L1995
	add	x1, x28, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,296]
	ldr	x26, [x1,312]
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1,1244]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2007
.L1995:
	add	w20, w20, 1
	cmp	w20, 10
	bne	.L1998
	b	.L1997
.L2007:
	mov	w19, w0
.L1997:
	mov	x0, x22
	mov	w1, 0
	bl	ymtc_3d_set_slc_rr_para
	b	.L1999
.L2023:
	add	x1, x28, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,296]
	ldr	x26, [x1,312]
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1,1244]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2008
.L2000:
	add	w20, w20, 1
	cmp	w20, 51
	beq	.L2002
.L2003:
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
	bne	.L2023
	b	.L2000
.L2008:
	mov	w19, w0
.L2002:
	mov	x0, x22
	mov	w1, 0
	bl	ymtc_3d_set_tlc_rr_para
.L1999:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,1244]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2004
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2004:
	cmn	w19, #1
	beq	.L2009
	cmp	w19, 256
	bne	.L2005
.L2009:
	adrp	x0, .LC156
	mov	w1, w20
	add	x0, x0, :lo12:.LC156
	mov	w2, w24
	mov	w3, w20
	mov	w4, w19
	bl	printk
.L2005:
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
	cbnz	w0, .L2025
	add	x0, x5, 161
	add	x5, x5, 136
	str	x0, [x29,120]
	mov	w0, 1
	sub	w0, w0, w5
	str	w0, [x29,112]
	lsl	x0, x19, 8
	str	x0, [x29,128]
	add	x19, x24, x0
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	mov	w27, -1
	str	x0, [x29,104]
.L2030:
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
	tbz	x0, 4, .L2026
	ldr	x0, [x29,104]
	mov	w1, w20
	mov	w2, w22
	mov	w3, w28
	bl	printk
	ldr	x5, [x29,96]
.L2026:
	cmn	w28, #1
	beq	.L2027
	adrp	x0, .LANCHOR5
	cmn	w27, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w27, w27, w28, ne
	ldr	x25, [x0,296]
	ldr	x26, [x0,312]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L2040
.L2027:
	ldr	x0, [x29,120]
	add	x5, x5, 1
	cmp	x5, x0
	bne	.L2030
	mov	w20, 26
	b	.L2029
.L2040:
	mov	w27, w28
.L2029:
	ldr	x0, [x29,128]
	add	x19, x24, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 141
	b	.L2062
.L2025:
	lsl	x0, x19, 8
	str	x0, [x29,128]
	add	x19, x24, x0
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	add	x28, x5, 168
	mov	w27, -1
	mov	w20, 1
	str	x0, [x29,120]
.L2036:
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
	tbz	x0, 4, .L2032
	ldr	x0, [x29,120]
	mov	w3, w4
	mov	w1, w20
	mov	w2, w22
	str	x4, [x29,112]
	bl	printk
	ldr	x4, [x29,112]
.L2032:
	cmn	w4, #1
	beq	.L2033
	adrp	x0, .LANCHOR5
	cmn	w27, #1
	add	x0, x0, :lo12:.LANCHOR5
	csel	w27, w27, w4, ne
	ldr	x25, [x0,296]
	ldr	x26, [x0,312]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L2041
.L2033:
	add	w20, w20, 1
	add	x28, x28, 4
	cmp	w20, 26
	bne	.L2036
	b	.L2035
.L2041:
	mov	w27, w4
.L2035:
	ldr	x0, [x29,128]
	add	x19, x24, x0
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 137
.L2062:
	str	w0, [x19,2052]
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	bl	nandc_wait_flash_ready
	ldrb	w0, [x21,1244]
	add	w0, w0, w0, lsl 1
	cmp	w27, w0, lsr 2
	bcc	.L2037
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L2037:
	cmn	w27, #1
	beq	.L2042
	cmp	w27, 256
	bne	.L2038
.L2042:
	adrp	x0, .LC159
	mov	w1, w20
	add	x0, x0, :lo12:.LC159
	mov	w2, w22
	mov	w3, w20
	mov	w4, w27
	bl	printk
.L2038:
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
	ldr	x5, [x0,1128]
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
.L2064:
	cmp	w6, w24
	bcs	.L2068
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
	beq	.L2066
	add	x1, x7, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,296]
	ldr	x26, [x1,312]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,1244]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2073
.L2066:
	add	w6, w6, 1
	b	.L2064
.L2073:
	mov	w19, w0
.L2068:
	add	x28, x28, :lo12:.LANCHOR0
	add	x22, x27, x22
	ldrb	w0, [x28,1244]
	strb	w20, [x22,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2070
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2070:
	cmn	w19, #1
	beq	.L2074
	cmp	w19, 256
	bne	.L2071
.L2074:
	adrp	x0, .LC160
	mov	w1, w6
	add	x0, x0, :lo12:.LC160
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L2071:
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
	adrp	x0, .LC161
	str	w27, [x29,124]
	add	x0, x0, :lo12:.LC161
	mov	w25, w27
	mov	w28, w27
	mov	w19, 1024
	mov	w26, 6
	mov	w21, -1
	str	x0, [x29,104]
.L2089:
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
	tbz	x0, 4, .L2084
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w26
	mov	w2, w22
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L2084:
	add	w0, w19, 1
	cmp	w4, w0
	bhi	.L2085
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x24, [x0,296]
	ldr	x23, [x0,312]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,1244]
	cmp	w4, w0, lsr 2
	bcs	.L2095
	add	w28, w28, 1
	cmp	w28, 7
	bls	.L2095
	sub	w27, w26, w28
	mov	w19, w4
	mov	w21, 0
	b	.L2087
.L2085:
	cmp	w25, w28
	bcs	.L2096
	sub	w0, w27, w28
	cmp	w28, 7
	str	w0, [x29,124]
	bhi	.L2088
	mov	w25, w28
	b	.L2096
.L2095:
	mov	w27, w26
	mov	w19, w4
	mov	w21, 0
	b	.L2086
.L2096:
	mov	w28, 0
.L2086:
	add	w26, w26, 2
	cmp	w26, 50
	bne	.L2089
.L2087:
	ldr	w0, [x29,124]
	cmp	w25, w28
	csel	w27, w27, w0, ls
.L2088:
	cbz	w27, .L2090
	adrp	x0, .LANCHOR0+1244
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+1244]
	udiv	w0, w0, w1
	cmp	w19, w0
	bcs	.L2090
	adrp	x0, .LC162
	mov	w1, w27
	add	x0, x0, :lo12:.LC162
	bl	printk
	mov	w0, w27
	b	.L2108
.L2090:
	ldrb	w0, [x29,116]
.L2108:
	bl	nandc_set_ddr_para
	cbz	w21, .L2092
	adrp	x0, .LANCHOR0
	add	x27, x0, :lo12:.LANCHOR0
	mov	x26, x0
	ldrb	w1, [x27,1304]
	tbz	x1, 0, .L2092
	adrp	x0, .LC163
	mov	w2, w22
	mov	w1, w20
	add	x0, x0, :lo12:.LC163
	bl	printk
	mov	w0, w20
	bl	flash_reset
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	add	x0, x27, w20, sxtw
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
	adrp	x0, .LC164
	mov	w1, w20
	add	x0, x0, :lo12:.LC164
	mov	w2, w22
	mov	w3, w19
	bl	printk
	ldrb	w0, [x27,1244]
	cmp	w19, w0
	bhi	.L2097
	adrp	x1, .LANCHOR5
	add	x1, x1, :lo12:.LANCHOR5
	ldr	w0, [x1,324]
	add	w0, w0, 1
	str	w0, [x1,324]
	cmp	w0, 100
	bls	.L2093
	strb	wzr, [x27,1192]
	b	.L2092
.L2097:
	mov	w19, w21
.L2093:
	add	x20, x26, :lo12:.LANCHOR0
	ldrb	w0, [x20,1304]
	bl	flash_set_interface_mode
	ldrb	w0, [x20,1304]
	bl	nandc_set_if_mode
.L2092:
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
	ldrb	w0, [x0,1153]
	mov	w22, w4
	cmp	w0, w25
	bhi	.L2110
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 431
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2110:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, w25, sxtw
	ldrb	w21, [x1,1248]
	ldrb	w4, [x0,1153]
	cmp	w25, w4
	bcc	.L2111
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2137
	adrp	x0, .LC165
	mov	w1, w21
	add	x0, x0, :lo12:.LC165
	mov	w2, w25
	mov	w3, w19
	bl	printk
.L2137:
	mov	w0, -1
	b	.L2136
.L2111:
	ubfx	x1, x19, 24, 2
	cbnz	w1, .L2114
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L2115
	ldrb	w0, [x0,1]
	cbz	w0, .L2114
.L2115:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,1]
	sub	w19, w19, w0
	cbz	w2, .L2116
	add	w19, w0, w19, lsl 1
	b	.L2114
.L2116:
	add	x1, x1, 4
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L2114:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2136
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,1240]
	cbnz	w26, .L2118
.L2121:
	adrp	x0, .LANCHOR5+328
	ldr	x5, [x0,#:lo12:.LANCHOR5+328]
	cbnz	x5, .L2119
	b	.L2120
.L2118:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	strb	wzr, [x25,1240]
	bl	flash_read_page
	strb	w26, [x25,1240]
	cmn	w0, #1
	beq	.L2121
	b	.L2136
.L2119:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	blr	x5
	cmn	w0, #1
	bne	.L2136
.L2120:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,1240]
	bl	printk
	ldrb	w0, [x20,1192]
	cbz	w0, .L2137
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_ddr_tuning_read
.L2136:
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
	adrp	x0, .LANCHOR5+180
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x24, x2
	ldrh	w19, [x0,#:lo12:.LANCHOR5+180]
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
	bne	.L2139
	mov	w28, 0
	mov	w5, 2
.L2140:
	cmp	w28, w19
	bgt	.L2139
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
	bne	.L2141
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L2140
.L2141:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L2140
.L2139:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2144
	adrp	x0, .LC167
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC167
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L2144:
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
	adrp	x0, .LANCHOR3+1312
	mov	x3, x7
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1312]
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
	strb	w22, [x21,1192]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,1304]
	bl	nandc_set_if_mode
	ldr	x2, [x20,336]
	mov	w1, w23
	ldr	x3, [x20,344]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tuning_read
	ldr	x2, [x20,336]
	mov	w0, w24
	ldr	x3, [x20,344]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2150
	ldrb	w0, [x21,1304]
	tbz	x0, 0, .L2150
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,1192]
	b	.L2151
.L2150:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,1192]
.L2151:
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
	stp	x29, x30, [sp, -80]!
	mov	w5, 1
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x26, .LANCHOR0
	mov	x25, x2
	stp	x21, x22, [sp,32]
	uxtb	x22, w0
	add	x0, x26, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	w23, w1
	mov	w1, 24
	ldrb	w19, [x0,1257]
	mov	x21, x22
	mov	x24, x3
	ldr	x20, [x0,1048]
	sub	w19, w1, w19
	add	x22, x22, 8
	lsl	w19, w5, w19
	bl	nandc_wait_flash_ready
	mov	w0, w21
	sub	w19, w19, #1
	add	x22, x20, x22, lsl 8
	and	w19, w19, w23
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	ubfx	x0, x23, 24, 2
	cbnz	w0, .L2156
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L2157
.L2156:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L2157:
	ubfiz	x0, x21, 8, 8
	add	x20, x20, x0
	mov	w0, 128
	str	w0, [x20,2056]
	and	w0, w19, 255
	str	wzr, [x20,2052]
	str	wzr, [x20,2052]
	str	w0, [x20,2052]
	lsr	w0, w19, 8
	str	w0, [x20,2052]
	lsr	w0, w19, 16
	str	w0, [x20,2052]
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,1256]
	cbz	w0, .L2158
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L2158:
	mov	w0, w19
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	w1, 1
	mov	x3, x25
	mov	x4, x24
	mov	w0, w21
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	mov	w0, 16
	str	w0, [x20,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w21
	bl	nandc_de_cs
	and	w2, w19, 4
	tbz	x19, 2, .L2159
	adrp	x0, .LC168
	mov	w1, w23
	add	x0, x0, :lo12:.LC168
	bl	printk
	mov	w2, -1
.L2159:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
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
	ldr	x0, [x19,296]
	bl	ftl_memset
	ldr	x0, [x19,312]
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
	bne	.L2167
.L2169:
	mov	w19, -1
	b	.L2168
.L2167:
	adrp	x21, .LANCHOR2
	ldr	x2, [x19,296]
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x3, [x19,312]
	mov	w0, w22
	mov	w1, w20
	add	x21, x21, 8
	ldrb	w4, [x21,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2169
	ldr	x2, [x19,296]
	mov	w0, w22
	ldr	x3, [x19,312]
	mov	w1, w20
	ldrb	w4, [x21,9]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2169
	ldr	x0, [x19,296]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L2169
	ldr	x0, [x19,312]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csetm	w19, ne
.L2168:
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
	cbz	w26, .L2177
	sxtw	x1, w25
	add	x1, x1, 8
	add	x1, x19, x1, lsl 8
	str	w26, [x1,8]
.L2177:
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
	cbz	w0, .L2178
	lsr	w0, w21, 24
	str	w0, [x19,2052]
.L2178:
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
	tbz	x0, 4, .L2186
	adrp	x0, .LC169
	mov	w2, w28
	add	x0, x0, :lo12:.LC169
	bl	printk
.L2186:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w28, .L2187
	bl	zftl_flash_enter_slc_mode
	b	.L2188
.L2187:
	bl	zftl_flash_exit_slc_mode
.L2188:
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
	tbz	x19, 2, .L2189
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2190
	adrp	x0, .LC168
	mov	w1, w22
	add	x0, x0, :lo12:.LC168
	bl	printk
.L2190:
	mov	w2, -1
.L2189:
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
	tbz	x19, 2, .L2198
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2199
	adrp	x0, .LC170
	mov	w1, w25
	add	x0, x0, :lo12:.LC170
	bl	printk
.L2199:
	mov	w2, -1
.L2198:
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
	.type	fw_flash_page_prog.constprop.30, %function
fw_flash_page_prog.constprop.30:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x19, x19, :lo12:.LANCHOR0
	str	x2, [x29,72]
	stp	x21, x22, [sp,32]
	mov	x22, x1
	ldr	x1, [x19,1144]
	ldrb	w21, [x19,1244]
	ldrb	w20, [x1,9]
	udiv	w20, w0, w20
	ldrb	w0, [x19,1152]
	bl	nandc_bch_sel
	adrp	x4, .LANCHOR2
	ldr	x3, [x29,72]
	add	x4, x4, :lo12:.LANCHOR2
	add	x4, x4, 8
	ldrb	w0, [x4,7]
	cmp	w0, 9
	bne	.L2204
	ldrb	w23, [x19,1154]
	cbnz	w23, .L2204
	ldrb	w0, [x4,12]
	cmp	w0, 3
	bne	.L2205
	mov	w0, w23
	mov	w1, w20
	mov	x2, x22
	bl	ymtc_flash_tlc_page_prog
	b	.L2207
.L2205:
	adrp	x19, .LANCHOR5
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR5
	mov	w2, 16384
	str	x3, [x29,72]
	ldr	x0, [x19,296]
	bl	ftl_memset
	ldr	x4, [x19,296]
	mov	w0, w23
	ldr	x3, [x29,72]
	mov	w1, w20
	mov	x2, x22
	mov	x5, x4
	mov	w6, 4
	bl	flash_dual_page_prog
	b	.L2207
.L2204:
	mov	w0, 0
	mov	w1, w20
	mov	x2, x22
	mov	w4, 4
	bl	flash_prog_page
.L2207:
	mov	w19, w0
	mov	w0, w21
	bl	nandc_bch_sel
	ldr	x23, [sp,48]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	fw_flash_page_prog.constprop.30, .-fw_flash_page_prog.constprop.30
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
	ldrb	w0, [x0,1153]
	uxtb	w22, w2
	mov	w20, w4
	mov	x24, x5
	cmp	w0, w25
	mov	x23, x6
	mov	x19, x7
	bhi	.L2209
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 868
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2209:
	add	x7, x19, :lo12:.LANCHOR0
	ldrb	w0, [x7,1153]
	cmp	w0, w25
	bls	.L2208
	add	x25, x7, w25, sxtw
	ldrb	w25, [x25,1248]
	ldr	x19, [x7,1048]
	mov	w0, w25
	bl	nandc_cs
	cbz	w26, .L2211
	sxtw	x0, w25
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w26, [x0,8]
.L2211:
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
.L2208:
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
	cbz	w0, .L2216
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
	b	.L2217
.L2216:
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
.L2217:
	cbz	w24, .L2215
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
.L2215:
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
	cbz	w23, .L2223
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
	b	.L2224
.L2223:
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
.L2224:
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
	ldrb	w0, [x0,1153]
	mov	x19, x5
	cmp	w0, w20
	bhi	.L2226
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 903
	add	x1, x1, 328
	add	x0, x0, :lo12:.LC0
	str	x4, [x29,48]
	str	x3, [x29,56]
	bl	printk
	bl	dump_stack
	ldr	x4, [x29,48]
	ldr	x3, [x29,56]
.L2226:
	add	x5, x19, :lo12:.LANCHOR0
	ldrb	w0, [x5,1153]
	cmp	w0, w20
	bls	.L2225
	add	x20, x5, w20, sxtw
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
.L2225:
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
	tbz	x0, 12, .L2229
	ldr	w2, [x20,40]
	adrp	x0, .LC171
	add	x0, x0, :lo12:.LC171
	mov	w1, w21
	add	w3, w2, 1
	bl	printk
.L2229:
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
	ldrb	w0, [x0,1153]
	mov	x22, x3
	sub	w19, w19, w20
	lsl	w20, w5, w20
	lsr	w1, w1, w19
	sub	w20, w20, #1
	and	w20, w1, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2234
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 956
	add	x1, x1, 360
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2234:
	add	x28, x25, :lo12:.LANCHOR0
	ldrb	w0, [x28,1153]
	cmp	w0, w20
	bls	.L2233
	mov	w0, 1
	ldr	x27, [x28,1048]
	lsl	w19, w0, w19
	sub	w19, w19, #1
	add	x0, x28, w20, sxtw
	and	w19, w24, w19
	ldrb	w26, [x0,1248]
	bl	nandc_rdy_status
	cbnz	w0, .L2236
	ldrb	w0, [x28,1153]
	cmp	w0, 1
	bne	.L2237
	bl	nandc_wait_flash_ready
	b	.L2236
.L2237:
	mov	w0, w20
	mov	w1, w19
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L2236:
	mov	w0, w26
	ubfx	x24, x24, 24, 2
	bl	hynix_reconfig_rr_para
	mov	w0, w26
	bl	nandc_cs
	cbnz	w24, .L2238
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
	ldrb	w0, [x25,#:lo12:.LANCHOR0]
	cbz	w0, .L2239
	mov	w0, w26
	bl	zftl_flash_enter_slc_mode
	b	.L2239
.L2238:
	mov	w0, w26
	bl	zftl_flash_exit_slc_mode
.L2239:
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
	cbz	w0, .L2240
	lsr	w0, w19, 24
	str	w0, [x20,2052]
.L2240:
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
.L2233:
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
	beq	.L2248
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
.L2250:
	add	x5, x3, x2, lsl 6
	ldr	w6, [x5,40]
	lsr	w6, w6, w7
	and	w6, w1, w6
	cmp	w4, w6
	bne	.L2249
	ldrb	w6, [x5,58]
	add	x5, x5, 48
	cmp	w6, 7
	bne	.L2249
	mov	w1, 3
	strb	w1, [x5,10]
	b	.L2248
.L2249:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L2250
.L2248:
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
	ldrb	w0, [x0,1153]
	sub	w19, w19, w20
	lsl	w20, w1, w20
	lsr	w2, w24, w19
	sub	w20, w20, #1
	and	w20, w2, w20
	uxtb	w20, w20
	cmp	w0, w20
	bhi	.L2256
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1070
	add	x1, x1, 384
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2256:
	add	x3, x23, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w1, [x3,1153]
	cmp	w1, w20
	bls	.L2257
	mov	w2, 1
	add	x20, x3, w20, sxtw
	ldrb	w20, [x20,1248]
	lsl	w19, w2, w19
	add	w19, w19, w0
	ubfx	x25, x24, 24, 2
	mov	w0, w20
	and	w19, w19, w24
	ldr	x24, [x3,1048]
	bl	nandc_cs
	cbnz	w25, .L2258
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2258:
	add	x7, x23, :lo12:.LANCHOR0
	uxtb	x5, w20
	and	w6, w19, 255
	lsr	w4, w19, 8
	lsr	w3, w19, 16
	ldrb	w0, [x7,1176]
	ldrb	w2, [x7,1256]
	cmp	w0, 1
	bne	.L2259
	add	x5, x24, x5, lsl 8
	mov	w0, 6
	b	.L2287
.L2259:
	ldr	x0, [x7,1144]
	add	x5, x24, x5, lsl 8
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2262
	mov	w0, 5
.L2287:
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w6, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	cbz	w2, .L2283
	lsr	w0, w19, 24
	str	w0, [x5,2052]
	b	.L2283
.L2262:
	str	wzr, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w6, [x5,2052]
	str	w4, [x5,2052]
	str	w3, [x5,2052]
	cbz	w2, .L2264
	lsr	w0, w19, 24
	str	w0, [x5,2052]
.L2264:
	mov	w0, 5
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
.L2283:
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w25, .L2265
	add	x23, x23, :lo12:.LANCHOR0
	ldr	x0, [x23,1144]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2265
	ldrb	w0, [x23,1268]
	cbnz	w0, .L2265
	ldrb	w0, [x23,1269]
	cbnz	w0, .L2265
	sub	w1, w25, #1
	add	w0, w19, w19, lsl 1
	add	w0, w1, w0
	b	.L2284
.L2265:
	mov	w0, w19
.L2284:
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
.L2257:
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
	ldrb	w0, [x1,1153]
	sub	w19, w19, #1
	uxtb	w20, w20
	and	w19, w19, w21
	cmp	w0, w20
	bhi	.L2289
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1232
	add	x1, x1, 416
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2289:
	add	x0, x24, :lo12:.LANCHOR0
	add	x20, x0, w20, sxtw
	ldrb	w26, [x20,1248]
	ldr	x27, [x0,1048]
	mov	w0, w26
	bl	nandc_cs
	cbnz	w25, .L2290
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2290:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,20]
	cmp	w0, 3
	bne	.L2291
	ubfiz	x0, x26, 8, 8
	add	x1, x27, x0
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
.L2291:
	cbz	w25, .L2292
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0,1144]
	ldrb	w1, [x1,12]
	cmp	w1, 3
	bne	.L2292
	ldrb	w1, [x0,1268]
	cbnz	w1, .L2292
	ldrb	w0, [x0,1269]
	cbnz	w0, .L2292
	sub	w0, w25, #1
	add	w1, w19, w19, lsl 1
	add	w0, w0, w1
	b	.L2324
.L2292:
	mov	w0, w19
.L2324:
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
	bne	.L2294
	add	x28, x24, :lo12:.LANCHOR0
	ldrb	w5, [x28,1240]
	cbz	w5, .L2295
	ldrb	w4, [x27,9]
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	str	x5, [x29,104]
	strb	wzr, [x28,1240]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,1240]
	cbnz	w25, .L2296
.L2303:
	ldrb	w2, [x24,#:lo12:.LANCHOR0]
	add	x1, x24, :lo12:.LANCHOR0
	cbz	w2, .L2296
	ldrb	w1, [x1,1244]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2296
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w1, [x20,27]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 4
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L2311
.L2296:
	cmn	w0, #1
	bne	.L2311
.L2304:
	adrp	x0, .LANCHOR5+328
	ldr	x5, [x0,#:lo12:.LANCHOR5+328]
	cbnz	x5, .L2299
.L2302:
	add	x24, x24, :lo12:.LANCHOR0
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	mov	w1, 0
	mov	w2, w21
	mov	w3, -1
	ldrb	w4, [x24,1240]
	bl	printk
	ldrb	w0, [x24,1192]
	cbnz	w0, .L2300
	mov	w0, -1
	b	.L2311
.L2299:
	add	x4, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x4,17]
	blr	x5
	cmn	w0, #1
	bne	.L2311
	b	.L2302
.L2300:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, w26
	orr	w1, w19, w25, lsl 24
	mov	x2, x22
	mov	x3, x23
	ldrb	w4, [x20,17]
	bl	flash_ddr_tuning_read
	b	.L2311
.L2294:
	cbz	w25, .L2303
	b	.L2311
.L2295:
	cbz	w25, .L2303
	b	.L2304
.L2311:
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
	beq	.L2413
	add	x0, x0, 1312
	mov	w20, 0
	add	x0, x0, x22, lsl 6
	ldrb	w1, [x0,58]
	ldr	w25, [x0,40]
	sub	w2, w1, #1
	cmp	w2, 10
	bhi	.L2327
	adrp	x0, .L2329
	add	x0, x0, :lo12:.L2329
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x2, .Lrtx2329
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx2329:
	.section	.rodata
	.align	0
	.align	2
.L2329:
	.2byte	(.L2328 - .Lrtx2329) / 4
	.2byte	(.L2330 - .Lrtx2329) / 4
	.2byte	(.L2331 - .Lrtx2329) / 4
	.2byte	(.L2331 - .Lrtx2329) / 4
	.2byte	(.L2331 - .Lrtx2329) / 4
	.2byte	(.L2331 - .Lrtx2329) / 4
	.2byte	(.L2332 - .Lrtx2329) / 4
	.2byte	(.L2333 - .Lrtx2329) / 4
	.2byte	(.L2334 - .Lrtx2329) / 4
	.2byte	(.L2331 - .Lrtx2329) / 4
	.2byte	(.L2334 - .Lrtx2329) / 4
	.text
.L2328:
	bl	nandc_wait_flash_ready
	add	x3, x19, :lo12:.LANCHOR0
	add	x0, x3, 1312
	add	x0, x0, x22, lsl 6
	ldr	x2, [x0,16]
	ldr	x1, [x0,8]
	cbz	x2, .L2335
	ldrb	w4, [x0,56]
	ldrb	w0, [x3,3360]
	cmp	w4, w0
	csel	x1, x1, x2, ne
.L2335:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w25
	add	x19, x19, 1312
	add	x19, x19, x22, lsl 6
	ldr	x2, [x19,24]
	bl	flash_complete_page_read
	str	w0, [x19,52]
	mov	w0, 13
	strb	w0, [x19,58]
	b	.L2414
.L2330:
	bl	nandc_wait_flash_ready
	add	x2, x19, :lo12:.LANCHOR0
	lsl	x3, x22, 6
	add	x0, x2, 1312
	add	x1, x0, x3
	ldrb	w20, [x0,x3]
	ldr	x27, [x1,8]
	add	x0, x0, x20, lsl 6
	ldr	x24, [x0,8]
	ldr	x0, [x1,16]
	cbz	x0, .L2336
	ldrb	w3, [x1,56]
	ldrb	w1, [x2,3360]
	cmp	w3, w1
	csel	x27, x27, x0, ne
.L2336:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 1312
	add	x0, x0, x20, lsl 6
	ldr	x1, [x0,16]
	cbz	x1, .L2337
	ldrb	w3, [x0,56]
	ldrb	w0, [x2,3360]
	cmp	w3, w0
	csel	x24, x24, x1, ne
.L2337:
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
	beq	.L2338
	ldr	w0, [x28,4]
	cmn	w0, #1
	beq	.L2339
	ldr	x1, [x26,8]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2339
.L2338:
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
	beq	.L2340
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2340
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2340
	adrp	x0, .LC173
	ldr	w1, [x26,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2340:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x0, x0, x22, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2339
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2339
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 431
	add	x1, x1, 448
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2339:
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
	beq	.L2341
	add	x3, x3, x20, lsl 6
	ldr	w0, [x3,36]
	cmn	w0, #1
	beq	.L2343
	ldr	x1, [x3,24]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2343
.L2341:
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
	beq	.L2345
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2345
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2345
	adrp	x0, .LC173
	ldr	w1, [x22,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2345:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2343
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2343
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 450
	add	x1, x1, 448
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2343:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x19, x19, 1312
	add	x19, x19, x20, lsl 6
	str	w23, [x19,52]
	strb	w1, [x19,58]
.L2414:
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2413
.L2331:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w25
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L2413
	mov	w20, 5
	ands	w20, w21, w20
	beq	.L2347
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 12
	add	x19, x19, 1312
	mov	w2, w21
	add	x19, x19, x22, lsl 6
	mov	w4, 12
	mov	w20, w21
	strb	w0, [x19,58]
	adrp	x0, .LC174
	ldrb	w1, [x19,1]
	add	x0, x0, :lo12:.LC174
	ldr	w3, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2327
.L2347:
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
	bne	.L2413
	ldrh	w0, [x23,50]
	cbnz	w0, .L2413
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
	ldr	x2, [x21,336]
	ldr	x3, [x21,344]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2348
	ldr	x1, [x23,24]
	ldr	x0, [x21,344]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2413
.L2348:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC175
	add	x19, x4, 1312
	add	x0, x0, :lo12:.LC175
	add	x19, x19, x22, lsl 6
	ldrb	w4, [x4,1240]
	ldrb	w1, [x19,1]
	ldr	w2, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2327
.L2334:
	add	x24, x19, :lo12:.LANCHOR0
	cmp	w1, 11
	mov	w0, 3
	mov	w5, 10
	csel	w5, w0, w5, ne
	mov	w4, 24
	ldrb	w1, [x24,1257]
	mov	w0, 1
	add	x21, x24, 1312
	sub	w4, w4, w1
	lsl	w0, w0, w1
	sub	w0, w0, #1
	lsr	w2, w25, w4
	and	w2, w0, w2
	add	x3, x21, x22, lsl 6
	uxth	w2, w2
.L2350:
	ldrb	w1, [x3]
	cmp	w1, 255
	beq	.L2415
	sxtw	x23, w1
	add	x3, x21, x23, lsl 6
	ldrb	w1, [x3,58]
	cmp	w1, w5
	bne	.L2350
	ldr	w1, [x3,40]
	lsr	w1, w1, w4
	and	w1, w0, w1
	cmp	w2, w1
	bne	.L2350
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w25
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	tbz	x20, 6, .L2412
	ands	w1, w20, 15
	beq	.L2354
	add	x21, x21, x22, lsl 6
	adrp	x0, .LC176
	add	x0, x0, :lo12:.LC176
	mov	w2, w20
	mov	w4, 12
	ldrb	w1, [x21,1]
	ldr	w3, [x21,40]
	bl	printk
	mov	w0, 12
	strb	w0, [x21,58]
	mov	w0, -1
	str	w0, [x21,52]
	b	.L2353
.L2354:
	add	x20, x21, x22, lsl 6
	mov	w2, 13
	str	w1, [x20,52]
	ldr	x1, [x24,3392]
	strb	w2, [x20,58]
	ldr	w2, [x1,156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2412
	ldrh	w0, [x20,50]
	cbnz	w0, .L2412
	ldrb	w3, [x24,1257]
	mov	w0, 1
	adrp	x21, .LANCHOR5
	ldr	w1, [x20,40]
	lsl	w0, w0, w3
	add	x21, x21, :lo12:.LANCHOR5
	mov	w2, 24
	sub	w0, w0, #1
	sub	w2, w2, w3
	ldrb	w4, [x24,3360]
	lsr	w3, w1, w2
	lsl	w2, w0, w2
	bic	w1, w1, w2
	and	w0, w0, w3
	ldr	x2, [x21,336]
	ldr	x3, [x21,344]
	bl	flash_read_page_en
	cmn	w0, #1
	mov	w3, w0
	beq	.L2356
	ldr	x1, [x20,24]
	ldr	x0, [x21,344]
	ldr	w1, [x1]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2412
.L2356:
	add	x4, x19, :lo12:.LANCHOR0
	adrp	x0, .LC177
	add	x20, x4, 1312
	add	x0, x0, :lo12:.LC177
	add	x20, x20, x22, lsl 6
	ldrb	w4, [x4,1240]
	ldrb	w1, [x20,1]
	ldr	w2, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
.L2412:
	mov	w20, 0
.L2353:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 1312
	add	x19, x1, x22, lsl 6
	add	x1, x1, x23, lsl 6
	ldrb	w0, [x19,58]
	strb	w0, [x1,58]
	ldr	w0, [x19,52]
	str	w0, [x1,52]
	b	.L2327
.L2415:
	mov	w20, -1
	b	.L2327
.L2332:
	mov	w1, 32
	mov	w0, w25
	bl	flash_wait_device_ready
	tbz	x0, 5, .L2413
	ands	w20, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	beq	.L2358
	mov	w1, 12
	mov	w20, w0
	strb	w1, [x19,1370]
	mov	w1, -1
	str	w1, [x19,1364]
	b	.L2327
.L2358:
	mov	w0, 13
	str	w20, [x19,1364]
	strb	w0, [x19,1370]
	b	.L2327
.L2333:
	mov	w1, 64
	mov	w0, w25
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2413
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x22, lsl 6
	str	w0, [x19,1364]
	mov	w0, 7
	strb	w0, [x19,1370]
.L2413:
	mov	w20, 0
.L2327:
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
	cbz	w0, .L2417
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2417
	adrp	x0, .LC178
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC178
	mov	w2, w20
	bl	printk
.L2417:
	adrp	x23, .LANCHOR0
	adrp	x25, .LC0
	add	x24, x23, :lo12:.LANCHOR0
	mov	w21, 0
	add	x27, x24, 1312
	add	x25, x25, :lo12:.LC0
.L2418:
	cbz	w20, .L2451
	ldrb	w26, [x19]
	ldr	w22, [x19,40]
.L2419:
	mov	w0, w22
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2452
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2419
.L2452:
	cmp	w20, 1
	beq	.L2421
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x0,1305]
	cbz	w1, .L2421
	ldrb	w1, [x0,1269]
	cbz	w1, .L2422
.L2421:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L2423
.L2422:
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
	bne	.L2424
	adrp	x1, .LANCHOR4
	mov	w2, 697
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x25
	add	x1, x1, 480
	bl	printk
	bl	dump_stack
.L2424:
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
	bne	.L2425
	adrp	x5, .LANCHOR3
	str	x4, [x29,104]
	add	x5, x5, :lo12:.LANCHOR3
	ldrh	w1, [x5,1312]
	ldrb	w6, [x5,1314]
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
	beq	.L2425
	and	w22, w0, w22
	and	w0, w0, w2
	cmp	w22, w0
	bne	.L2425
	cmp	w21, w6
	beq	.L2425
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
	b	.L2423
.L2425:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
.L2423:
	subs	w20, w20, #1
	beq	.L2451
	add	x19, x23, :lo12:.LANCHOR0
	sbfiz	x26, x26, 6, 32
	add	x19, x19, 1312
	add	x19, x19, x26
	b	.L2418
.L2451:
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
.L2454:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3363]
	cmp	w0, 255
	beq	.L2456
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2454
.L2456:
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
	cbz	w1, .L2458
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 1312
	ldrb	w2, [x2,352]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2458:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2,3389]
	strb	w0, [x20,352]
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
	cbz	w0, .L2462
	bl	timer_get_time
	adrp	x1, .LANCHOR5+356
	ldr	w1, [x1,#:lo12:.LANCHOR5+356]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2462
	bl	ftl_flush
.L2462:
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
.L2469:
	cbz	w20, .L2492
	ldrb	w24, [x19]
	ldr	w22, [x19,40]
.L2470:
	mov	w0, w22
	mov	w1, 0
	bl	queue_lun_state
	cbz	w0, .L2493
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2470
.L2493:
	cmp	w20, 1
	beq	.L2476
	adrp	x0, .LANCHOR5+360
	ldrb	w0, [x0,#:lo12:.LANCHOR5+360]
	cbz	w0, .L2476
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
	bne	.L2475
	adrp	x1, .LANCHOR4
	mov	w2, 782
	add	x1, x1, :lo12:.LANCHOR4
	mov	x0, x28
	add	x1, x1, 496
	str	x3, [x29,120]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,120]
.L2475:
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
	bne	.L2476
	adrp	x0, .LANCHOR3+1418
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1418]
	add	w22, w0, w22
	cmp	w1, w22
	bne	.L2476
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
	b	.L2474
.L2476:
	mov	x0, x19
	bl	queue_read_cmd
.L2474:
	subs	w20, w20, #1
	beq	.L2492
	add	x19, x25, :lo12:.LANCHOR0
	sbfiz	x24, x24, 6, 32
	add	x19, x19, 1312
	add	x19, x19, x24
	b	.L2469
.L2492:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 1312
.L2480:
	cbz	w21, .L2494
	ldrb	w0, [x23,58]
	cmp	w0, 13
	bne	.L2481
	ldrb	w0, [x23]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2481
	sbfiz	x23, x0, 6, 32
	add	x23, x19, x23
.L2481:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2480
.L2494:
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
	cbnz	x0, .L2496
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L2496:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,3432]
	cbnz	x20, .L2497
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 729
	add	x1, x1, 512
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2497:
	mov	w24, 0
	add	x25, x21, 80
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x27, .LANCHOR3
	adrp	x26, .LANCHOR5
.L2498:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L2529
	mov	w23, 1
	sxtw	x28, w24
.L2509:
	add	x2, x22, 3424
	ldrh	w0, [x2,20]
	cmp	w23, w0
	bgt	.L2530
	add	x0, x28, 8
	ldrh	w1, [x25,x0,lsl 1]
	add	x0, x27, :lo12:.LANCHOR3
	ldrh	w3, [x0,1418]
	ldrb	w0, [x0,1328]
	cmp	w0, 2
	mul	w1, w1, w3
	beq	.L2499
	ldrb	w3, [x22,1268]
	cbz	w3, .L2500
.L2499:
	ldrh	w2, [x22,3440]
	sub	w2, w2, #1
	add	w1, w2, w1
	add	w1, w1, w23
	orr	w1, w1, w0, lsl 24
	b	.L2528
.L2500:
	cmp	w0, 3
	bne	.L2502
	ldrb	w0, [x22,1269]
	cbz	w0, .L2503
	ldrh	w0, [x2,16]
	add	w0, w0, w0, lsl 1
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w23
	orr	w1, w1, 50331648
	b	.L2528
.L2503:
	ldrh	w0, [x2,16]
	add	w1, w1, w0
	orr	w1, w1, w23, lsl 24
	b	.L2528
.L2502:
	ldrh	w0, [x2,16]
	add	w1, w1, w0
.L2528:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L2504
	add	x3, x26, :lo12:.LANCHOR5
	ldrh	w0, [x22,3446]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,232]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L2504
	ldr	x3, [x3,240]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L2505
.L2504:
	add	x0, x26, :lo12:.LANCHOR5
	ldrh	w4, [x22,3446]
	lsl	x4, x4, 2
	ldr	x1, [x0,232]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L2505
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L2506
	ldr	x7, [x20,24]
	ldr	x5, [x0,240]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC179
	add	x0, x0, :lo12:.LC179
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L2506:
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
	bne	.L2507
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w1, [x0,1956]
	cbnz	w1, .L2507
	ldrb	w0, [x0,1330]
	cbnz	w0, .L2507
	ldr	w0, [x20,40]
	bl	ftl_mask_bad_block
	b	.L2507
.L2505:
	ldrh	w0, [x22,3446]
	add	w23, w23, 1
	add	w0, w0, 1
	strh	w0, [x22,3446]
	b	.L2509
.L2530:
	add	w24, w24, 1
	b	.L2498
.L2529:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w21, 0
	add	x19, x19, 3424
	ldrh	w0, [x19,16]
	add	w1, w0, 1
	strh	w1, [x19,16]
	adrp	x1, .LANCHOR3+1330
	ldrb	w1, [x1,#:lo12:.LANCHOR3+1330]
	cbz	w1, .L2507
	add	w0, w0, 2
	strh	w0, [x19,16]
.L2507:
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
.L2532:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2534
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2532
.L2534:
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
.L2536:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	mov	w24, w0
	cbz	w0, .L2547
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2536
.L2547:
	cmp	w27, 2
	bne	.L2538
	adrp	x25, .LANCHOR0
	add	x2, x25, :lo12:.LANCHOR0
	ldrb	w0, [x2,1305]
	cbz	w0, .L2539
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
	cbz	w0, .L2540
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
	b	.L2541
.L2540:
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
.L2541:
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
	b	.L2542
.L2539:
	ldr	x0, [x22]
	ldr	w19, [x0,40]
.L2543:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	mov	w20, w0
	cbz	w0, .L2548
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2543
.L2548:
	mov	x0, x21
	mov	w1, 1
	bl	queue_tlc_prog_cmd
	mov	x0, x22
	mov	w1, w20
	bl	queue_tlc_prog_cmd
.L2545:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2542
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2545
.L2538:
	mov	x0, x21
	mov	w1, 1
	bl	queue_tlc_prog_cmd
.L2542:
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
.L2550:
	cbz	w22, .L2554
	ldr	x0, [x20]
	ldr	w19, [x0,40]
.L2551:
	mov	w0, w19
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2555
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2551
.L2555:
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
	b	.L2550
.L2554:
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
	ldrb	w1, [x1,1153]
	mov	w27, w4
	uxtb	w28, w5
	ubfx	x25, x20, 24, 2
	cmp	w1, w21
	mov	x24, x0
	bhi	.L2557
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 642
	add	x1, x1, 536
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2557:
	add	x1, x24, :lo12:.LANCHOR0
	mov	w0, -1
	ldrb	w2, [x1,1153]
	cmp	w2, w21
	bls	.L2558
	add	x0, x1, w21, sxtw
	mov	w19, w20
	ldrb	w26, [x0,1248]
	cbnz	w25, .L2559
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L2560
	ldrb	w0, [x1,1]
	cbz	w0, .L2559
.L2560:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w19, [x0,2]
	ldrb	w2, [x0,1]
	udiv	w1, w20, w19
	mul	w19, w1, w19
	sub	w1, w20, w19
	cbz	w2, .L2561
	add	w19, w19, w1, lsl 1
	b	.L2559
.L2561:
	add	x0, x0, 4
	ldrh	w0, [x0,w1,uxtw 1]
	add	w19, w0, w19
.L2559:
	adrp	x25, .LC180
	adrp	x24, .LC181
	adrp	x0, .LC182
	add	x25, x25, :lo12:.LC180
	add	x0, x0, :lo12:.LC182
	add	x24, x24, :lo12:.LC181
	str	x0, [x29,120]
.L2565:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2562
	mov	x0, x25
	mov	w1, w26
	mov	w2, w20
	mov	w3, w19
	bl	printk
.L2562:
	mov	w0, w26
	mov	w1, w19
	mov	x2, x23
	mov	x3, x22
	mov	w4, w27
	bl	flash_prog_page
	mov	w5, w0
	cbz	w28, .L2563
	adrp	x6, .LANCHOR5
	mov	w4, w27
	add	x7, x6, :lo12:.LANCHOR5
	mov	w0, w21
	mov	w1, w20
	str	x6, [x29,104]
	str	x5, [x29,96]
	ldr	x2, [x7,336]
	ldr	x3, [x7,344]
	str	x7, [x29,112]
	bl	flash_read_page_en
	cmp	w0, 512
	cset	w4, eq
	ldr	x6, [x29,104]
	cbnz	w4, .L2564
	cmn	w0, #1
	beq	.L2564
	ldr	x7, [x29,112]
	ldr	w1, [x23]
	ldr	x5, [x29,96]
	ldr	x0, [x7,336]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2564
	ldr	x0, [x7,344]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2563
.L2564:
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
	adrp	x0, .LC183
	mov	w3, w2
	add	x6, x6, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC183
	str	x6, [x29,112]
	ldr	x1, [x6,344]
	bl	rknand_print_hex
	ldr	x6, [x29,112]
	mov	w2, 4
	adrp	x0, .LC184
	mov	w3, w2
	add	x0, x0, :lo12:.LC184
	ldr	x1, [x6,336]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
	cbnz	w4, .L2565
.L2567:
	adrp	x0, .LC185
	mov	w1, w20
	add	x0, x0, :lo12:.LC185
	bl	printk
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 685
	add	x1, x1, 536
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, -1
	b	.L2558
.L2563:
	cmn	w5, #1
	mov	w0, w5
	beq	.L2567
.L2558:
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
	bne	.L2586
	adrp	x2, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 560
	mov	w2, 2678
	bl	printk
	bl	dump_stack
	adrp	x0, .LC185
	mov	w1, w20
	add	x0, x0, :lo12:.LC185
	bl	printk
.L2586:
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
	add	x0, x0, 384
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x19,3392]
	ldrh	w1, [x0,74]
	cmp	w1, 1
	bls	.L2588
	strh	wzr, [x0,150]
.L2588:
	adrp	x23, .LANCHOR4
	adrp	x24, .LC186
	add	x23, x23, :lo12:.LANCHOR4
	mov	w25, 0
	add	x26, x21, :lo12:.LANCHOR5
	add	x24, x24, :lo12:.LC186
	add	x23, x23, 576
.L2599:
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x0, x22, :lo12:.LANCHOR3
	add	x3, x26, 384
	str	x2, [x29,112]
	ldr	x1, [x2,3392]
	ldrh	w28, [x0,1418]
	str	x3, [x29,104]
	ldr	w0, [x1,4]
	ldrb	w19, [x26,640]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldr	w0, [x29,124]
	str	w0, [x26,384]
	ldr	x0, [x2,3392]
	ldrb	w1, [x2,3360]
	ldrh	w27, [x26,642]
	ldr	w0, [x0,4]
	lsl	w1, w1, 9
	str	w0, [x26,388]
	ldr	x0, [x26,648]
	bl	js_hash
	ldr	x3, [x29,104]
	ldr	x2, [x29,112]
	str	w0, [x3,8]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2589
	ldr	x3, [x2,3392]
	mov	x0, x24
	ldrb	w1, [x26,640]
	ldrh	w2, [x26,642]
	ldr	w3, [x3,4]
	bl	printk
.L2589:
	add	x1, x22, :lo12:.LANCHOR3
	ldrh	w0, [x26,642]
	ldrh	w1, [x1,1384]
	cmp	w1, w0
	bhi	.L2590
	adrp	x19, .LC0
	add	x19, x19, :lo12:.LC0
.L2595:
	ldrb	w1, [x26,641]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x26,641]
	cmp	w1, 7
	bls	.L2591
	mov	x1, 0
.L2594:
	add	x0, x20, :lo12:.LANCHOR0
	uxth	w25, w1
	ldr	x2, [x0,1128]
	add	w0, w1, 8
	add	x0, x2, w0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2592
	mov	x1, x23
	mov	w2, 846
	mov	x0, x19
	bl	printk
	bl	dump_stack
	b	.L2593
.L2592:
	cmp	w2, 255
	bne	.L2593
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2594
	mov	w25, w1
.L2593:
	strb	w25, [x26,641]
	mov	w25, 1
.L2591:
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w1, [x26,641]
	ldr	x0, [x28,1128]
	add	x1, x0, x1
	ldrb	w1, [x1,40]
	strb	w1, [x26,640]
	cmp	w1, 255
	beq	.L2595
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w19, [x0,1418]
	mov	w0, 0
	mul	w19, w1, w19
	mov	w1, w19
	bl	flash_erase_block
	ldr	x2, [x26,648]
	mov	w1, w19
	ldrb	w4, [x28,3360]
	mov	w0, 0
	add	x3, x26, 384
	add	w19, w19, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x26,642]
	b	.L2596
.L2590:
	madd	w19, w19, w28, w27
	cbnz	w0, .L2596
	mov	w1, w19
	bl	flash_erase_block
.L2596:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	x2, [x26,648]
	mov	w1, w19
	mov	w0, 0
	add	x3, x26, 384
	ldrb	w4, [x4,3360]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x26,642]
	add	w1, w1, 1
	strh	w1, [x26,642]
	beq	.L2597
	ldrb	w0, [x26,656]
	cbz	w0, .L2598
.L2597:
	strb	wzr, [x26,656]
	b	.L2599
.L2598:
	cbnz	w25, .L2600
.L2608:
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w0, [x21,640]
	cmp	w0, 255
	bne	.L2602
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 890
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2602
.L2600:
	ldrb	w19, [x26,641]
	adrp	x23, .LANCHOR4
	adrp	x24, .LC0
	add	x23, x23, :lo12:.LANCHOR4
	add	w19, w19, 1
	add	x24, x24, :lo12:.LC0
	add	x23, x23, 576
.L2603:
	cmp	w19, 7
	bhi	.L2608
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0,1128]
	add	w0, w19, 8
	add	x0, x1, w0, sxtw
	ldrb	w25, [x0,32]
	add	w0, w25, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2604
	mov	x1, x23
	mov	w2, 881
	mov	x0, x24
	bl	printk
	bl	dump_stack
	b	.L2605
.L2604:
	cmp	w25, 255
	beq	.L2606
.L2605:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w1, [x0,1418]
	mov	w0, 0
	mul	w1, w25, w1
	bl	flash_erase_block
.L2606:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2603
.L2602:
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
	strb	w0, [x21,656]
	adrp	x25, .LC187
	ldrh	w1, [x19,1088]
	adrp	x27, .LANCHOR3
	strb	w0, [x21,658]
	add	x26, x21, 384
	ldr	x0, [x21,648]
	adrp	x28, .LANCHOR2
	str	x0, [x19,1096]
	add	x25, x25, :lo12:.LC187
	add	x1, x0, x1, lsl 2
	str	x1, [x19,3392]
	mov	w1, 0
	strb	wzr, [x21,657]
	bl	ftl_memset
	ldr	x0, [x21,664]
	mov	w1, 0
	mov	w2, 16384
	bl	ftl_memset
	strb	wzr, [x21,641]
	ldr	x0, [x19,1128]
	mov	w19, 7
	strh	wzr, [x21,642]
	ldrb	w0, [x0,40]
	strb	w0, [x21,640]
.L2626:
	add	x6, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w24, w19
	ldr	x1, [x6,1128]
	add	x0, x1, w0, sxtw
	ldrb	w0, [x0,32]
	cmp	w0, 255
	beq	.L2622
	add	x1, x27, :lo12:.LANCHOR3
	ldrb	w4, [x6,3360]
	ldr	x2, [x21,648]
	mov	x3, x26
	str	x6, [x29,104]
	ldrh	w23, [x1,1418]
	mul	w23, w0, w23
	mov	w0, 0
	mov	w1, w23
	bl	ftl_read_page
	cmn	w0, #1
	mov	w5, w0
	ldr	x6, [x29,104]
	bne	.L2623
	ldr	x2, [x21,648]
	mov	w0, 0
	ldrb	w4, [x6,3360]
	add	w1, w23, 1
	mov	x3, x26
	bl	ftl_read_page
	mov	w5, w0
.L2623:
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2624
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, w5
	mov	w1, w19
	mov	w3, 749
	str	x5, [x29,104]
	ldr	x4, [x0,3392]
	mov	x0, x25
	ldr	w4, [x4]
	bl	printk
	ldr	x5, [x29,104]
.L2624:
	cmn	w5, #1
	beq	.L2622
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2636
.L2622:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L2626
	mov	w24, 0
	b	.L2625
.L2636:
	mov	w19, w24
.L2625:
	adrp	x23, .LANCHOR2
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2627
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,3392]
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	ldr	w3, [x3]
	bl	printk
.L2627:
	cmn	w19, #1
	bne	.L2628
	add	x22, x22, :lo12:.LANCHOR5
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, 16384
	ldr	x0, [x22,648]
	bl	ftl_memset
	ldr	x1, [x20,3392]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, 36
	movk	w0, 0x6, lsl 16
	ldr	x1, [x20,3392]
	str	w0, [x1,12]
	b	.L2653
.L2628:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x22, :lo12:.LANCHOR5
	add	x19, x21, 384
	mov	w4, 4
	mov	x3, x19
	adrp	x27, .LC189
	ldr	x1, [x0,1128]
	add	w0, w24, 8
	ldr	x2, [x21,648]
	add	x0, x1, w0, sxtw
	ldrb	w1, [x0,32]
	mov	w0, 0
	strb	w24, [x21,641]
	add	x27, x27, :lo12:.LC189
	strb	w1, [x21,640]
	bl	flash_get_last_written_page
	sxth	w25, w0
	add	w0, w0, 1
	ldrb	w26, [x21,640]
	uxth	w24, w0
	adrp	x0, .LANCHOR3+1418
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1418]
	mul	w26, w26, w0
	b	.L2630
.L2653:
	mov	w0, w19
	b	.L2629
.L2635:
	add	x28, x20, :lo12:.LANCHOR0
	ldr	x2, [x21,648]
	mov	w0, 0
	add	w1, w25, w26
	mov	x3, x19
	ldrb	w4, [x28,3360]
	bl	ftl_read_page
	cmn	w0, #1
	bne	.L2654
.L2631:
	sub	w25, w25, #1
	sxth	w25, w25
.L2630:
	tbz	w25, #31, .L2635
	b	.L2634
.L2654:
	ldr	x0, [x28,3392]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2631
	ldr	w2, [x19,8]
	cbnz	w2, .L2632
.L2634:
	add	x22, x22, :lo12:.LANCHOR5
	add	x20, x20, :lo12:.LANCHOR0
	strh	w24, [x22,642]
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
	tbz	x0, 14, .L2653
	ldr	x1, [x20,3392]
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	ldr	w1, [x1,156]
	bl	printk
	mov	w0, 0
	b	.L2629
.L2632:
	ldrb	w1, [x28,3360]
	ldr	x0, [x21,648]
	lsl	w1, w1, 9
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2634
	ldr	w1, [x19,8]
	mov	x0, x27
	bl	printk
	b	.L2631
.L2629:
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
	bls	.L2656
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2669
.L2656:
	bcs	.L2657
.L2669:
	str	w0, [x1,520]
.L2657:
	adrp	x23, .LANCHOR4
	adrp	x22, .LANCHOR5
	add	x23, x23, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR5
	mov	w0, 0
	add	x23, x23, 592
	add	x24, x22, 384
	bl	ftl_total_vpn_update
.L2661:
	add	x0, x21, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3
	add	x2, x3, :lo12:.LANCHOR3
	mov	x19, x3
	ldr	x0, [x0,3392]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,1384]
	cmp	w1, w0
	bcc	.L2659
	bl	ftl_ext_alloc_new_blk
.L2659:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2660
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 2211
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2660:
	add	x20, x21, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR3
	mov	w0, 24
	ldr	x4, [x20,3392]
	ldrb	w1, [x20,1257]
	ldrh	w19, [x3,1312]
	ldrh	w2, [x4,130]
	sub	w1, w0, w1
	sub	w1, w1, w19
	mov	w19, 1
	asr	w25, w2, w1
	lsl	w1, w19, w1
	sub	w19, w1, #1
	ldrh	w0, [x4,140]
	and	w2, w19, w2
	ldrh	w19, [x3,1418]
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
	ldr	x0, [x22,664]
	bl	js_hash
	str	w0, [x24,8]
	ldr	x2, [x22,664]
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
	beq	.L2661
	cmn	w0, #1
	beq	.L2662
	ldrb	w0, [x22,658]
	cbz	w0, .L2663
.L2662:
.L2658:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,658]
	b	.L2661
.L2663:
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
	add	x3, x3, 384
	ldrb	w2, [x22,1257]
	strh	wzr, [x1,1952]
	ldrh	w23, [x0,130]
	mov	w0, 24
	ldrh	w1, [x1,1312]
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
	tbz	x1, 12, .L2671
	ldr	x5, [x22,3392]
	adrp	x1, .LANCHOR4
	adrp	x0, .LC191
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC191
	add	x1, x1, 616
	ldrh	w5, [x5,130]
	mov	w2, 2256
	mov	w3, w25
	uxth	w4, w26
	bl	printk
.L2671:
	add	x20, x20, :lo12:.LANCHOR5
	adrp	x27, .LC192
	mov	w22, w25
	add	x26, x20, 384
	add	x27, x27, :lo12:.LC192
.L2672:
	tbnz	w22, #31, .L2677
	add	x0, x21, :lo12:.LANCHOR3
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x2, [x20,664]
	mov	x3, x26
	str	x5, [x29,96]
	ldrh	w1, [x0,1418]
	mov	w0, w28
	ldrb	w4, [x5,3360]
	madd	w1, w23, w1, w22
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2673
	cmn	w0, #1
	beq	.L2673
	ldr	x5, [x29,96]
	ldr	x0, [x5,1120]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2673
	ldr	w2, [x26,8]
	cbnz	w2, .L2674
.L2677:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,140]
	cmp	w1, w25
	bgt	.L2676
	add	w24, w24, 1
	strh	w24, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2676
.L2674:
	ldrb	w1, [x5,3360]
	ldr	x0, [x20,664]
	lsl	w1, w1, 9
	str	x2, [x29,96]
	bl	js_hash
	ldr	x2, [x29,96]
	cmp	w2, w0
	beq	.L2677
	ldr	w1, [x26,8]
	mov	x0, x27
	bl	printk
.L2673:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L2672
.L2676:
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
	tbz	x1, 12, .L2679
	ldr	w2, [x0,520]
	ldr	w3, [x0,12]
	str	x2, [x29,96]
	str	x3, [x29,104]
	bl	timer_get_time
	mov	w4, w0
	adrp	x1, .LANCHOR4
	adrp	x0, .LC193
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x2, [x29,96]
	ldr	x3, [x29,104]
	add	x0, x0, :lo12:.LC193
	add	x1, x1, 616
	bl	printk
.L2679:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, -1
	ldp	x19, x20, [sp,16]
	ldp	x23, x24, [sp,48]
	strh	w0, [x21,1288]
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
	ldr	x21, [x22,1936]
	bne	.L2694
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w24, w0
	mov	w19, 0
	beq	.L2694
	ldrh	w0, [x20]
	add	x23, x21, w23, uxth 2
	adrp	x21, .LANCHOR5
	add	x21, x21, :lo12:.LANCHOR5
	bl	ftl_vpn_decrement
	add	x20, x21, 704
	mov	w0, 15555
	ldrh	w1, [x22,1384]
	movk	w0, 0xf55f, lsl 16
	str	w0, [x21,704]
	ldrb	w0, [x22,1329]
	mul	w1, w1, w0
	mov	x0, x23
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x21,708]
	add	x1, x21, 720
	mov	x2, 0
	mov	w0, 2
	str	w19, [x21,712]
	str	w19, [x21,716]
	str	w19, [x21,720]
	bl	ftl_debug_info_fill
	adrp	x3, .LANCHOR0+3360
	mov	w0, w24
	mov	x1, x23
	mov	x2, x20
	ldrb	w3, [x3,#:lo12:.LANCHOR0+3360]
	bl	ftl_prog_ppa_page
.L2694:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
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
	bls	.L2698
	adrp	x20, .LANCHOR3
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w1, [x0,1344]
	cbnz	w1, .L2700
	ldrb	w0, [x0,1330]
	cbz	w0, .L2698
.L2700:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L2698
	ldr	x0, [x0,1096]
	ubfiz	x19, x22, 2, 16
	mov	w24, 1
	add	x0, x0, x19
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2701
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w24, [x0,1328]
.L2701:
	add	x23, x29, 128
	mov	w0, w22
	add	x1, x23, 16
	strh	w22, [x29,128]
	mov	w27, 0
	adrp	x26, .LC195
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	add	x1, x20, :lo12:.LANCHOR3
	strb	w0, [x29,137]
	strh	wzr, [x29,130]
	mov	w28, w27
	strb	wzr, [x29,133]
	mov	w25, w27
	ldrh	w1, [x1,1384]
	add	x26, x26, :lo12:.LC195
	strh	wzr, [x29,138]
	mul	w0, w0, w1
	strh	w0, [x29,134]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,1096]
	add	x1, x0, x19
	ldrh	w4, [x0,x19]
	ldr	w5, [x0,x19]
	adrp	x0, .LC194
	ldrb	w3, [x1,2]
	and	w4, w4, 2047
	mov	w1, w22
	ubfx	x5, x5, 11, 8
	ubfx	x2, x3, 5, 3
	add	x0, x0, :lo12:.LC194
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
.L2702:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w28
	bls	.L2726
	lsl	w0, w28, 1
	add	w25, w0, w28
	sub	w0, w0, #1
	sub	w1, w25, #1
	str	w0, [x29,120]
	str	w1, [x29,124]
	mov	w25, 0
.L2714:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2738
	sxtw	x0, w25
	mov	w27, 1
	str	x0, [x29,112]
.L2713:
	cmp	w27, w24
	bhi	.L2739
	ldr	x0, [x29,112]
	add	x0, x0, 8
	ldrh	w2, [x23,x0,lsl 1]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2704
	add	x1, x20, :lo12:.LANCHOR3
	cmp	w24, 3
	ldrh	w0, [x1,1418]
	mul	w0, w2, w0
	bne	.L2705
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w3, [x3,1269]
	cbz	w3, .L2706
	ldr	w3, [x29,124]
	b	.L2737
.L2706:
	add	w0, w0, w28
	orr	w3, w0, w27, lsl 24
	b	.L2707
.L2705:
	cmp	w24, 2
	add	w3, w0, w28
	bne	.L2707
	ldr	w3, [x29,120]
.L2737:
	add	w0, w0, w3
	ldrb	w3, [x1,1328]
	add	w0, w0, w27
	orr	w3, w0, w3, lsl 24
.L2707:
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
	beq	.L2727
	cbz	w11, .L2704
.L2727:
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
	cbnz	w11, .L2726
.L2704:
	add	w9, w27, 1
	uxth	w27, w9
	b	.L2713
.L2739:
	add	w10, w25, 1
	uxth	w25, w10
	b	.L2714
.L2738:
	add	w8, w28, 1
	uxth	w28, w8
	b	.L2702
.L2726:
.L2711:
	adrp	x0, .LC196
	mov	w3, w25
	mov	w4, w27
	mov	w2, w28
	mov	w1, w22
	add	x25, x21, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC196
	adrp	x26, .LC197
	mov	w24, w28
	add	x26, x26, :lo12:.LC197
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
.L2715:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w24
	bls	.L2721
	lsl	w0, w24, 1
	mov	w25, 0
	str	w0, [x29,124]
.L2722:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2740
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2716
	mov	x0, x26
	mov	w1, w24
	mov	w2, w25
	bl	printk
.L2716:
	add	x1, x20, :lo12:.LANCHOR3
	sxtw	x0, w25
	ldrb	w5, [x1,1344]
	ldrh	w2, [x1,1418]
	cbz	w5, .L2717
	add	x0, x0, 8
	ldrh	w0, [x23,x0,lsl 1]
	mul	w2, w0, w2
	mov	x0, x19
	orr	w2, w2, w24
	str	w2, [x19,40]
	bl	sblk_3d_tlc_dump_prog
	b	.L2718
.L2717:
	add	x0, x0, 8
	ldrb	w1, [x1,1328]
	cmp	w1, 2
	ldrh	w0, [x23,x0,lsl 1]
	mul	w2, w0, w2
	bne	.L2719
	ldr	w0, [x29,124]
	orr	w2, w2, w0
	mov	x0, x19
	orr	w2, w2, 33554432
	str	w2, [x19,40]
	bl	sblk_mlc_dump_prog
	b	.L2718
.L2719:
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
.L2718:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2722
.L2740:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2715
.L2721:
	mov	x0, x19
	bl	zbuf_free
	adrp	x0, .LC198
	mov	w1, w22
	add	x0, x0, :lo12:.LC198
	bl	printk
.L2698:
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
	.global	gc_ink_check_sblk
	.type	gc_ink_check_sblk, %function
gc_ink_check_sblk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x27, [sp,80]
	add	x0, x19, :lo12:.LANCHOR0
	mov	x20, x19
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w1, [x0,5534]
	ldr	x21, [x0,5608]
	cmp	w1, 3
	bhi	.L2742
	adrp	x0, .L2744
	add	x0, x0, :lo12:.L2744
	ldrb	w0, [x0,w1,uxtw]
	adr	x1, .Lrtx2744
	add	x0, x1, w0, sxtb #2
	br	x0
.Lrtx2744:
	.section	.rodata
	.align	0
	.align	2
.L2744:
	.byte	(.L2743 - .Lrtx2744) / 4
	.byte	(.L2745 - .Lrtx2744) / 4
	.byte	(.L2746 - .Lrtx2744) / 4
	.byte	(.L2747 - .Lrtx2744) / 4
	.text
.L2743:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,3380]
	cmp	w0, 7
	bls	.L2741
	ldrb	w0, [x19,3362]
	cmp	w0, 2
	bls	.L2741
	adrp	x20, .LANCHOR3
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR3
	mov	w22, 65535
	add	x20, x20, 1408
	mov	x0, x20
	bl	_list_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w22
	beq	.L2741
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x19,1096]
	ldrh	w0, [x1,x0]
	and	w0, w0, 2047
	cmp	w0, 2
	bgt	.L2741
	mov	w0, 1
	add	x21, x19, 3424
	bl	buf_alloc
	str	x0, [x19,5608]
	cbz	x0, .L2741
	mov	x0, x20
	mov	w1, 0
	add	x2, x19, 3380
	bl	_list_pop_index_node
	uxth	w20, w0
	cmp	w20, w22
	bne	.L2751
	ldr	x0, [x19,5608]
	bl	zbuf_free
	str	xzr, [x19,5608]
	b	.L2741
.L2751:
	mov	w0, w20
	add	x1, x21, 2162
	bl	ftl_get_blk_list_in_sblk
	strb	w0, [x21,2155]
	mov	w0, 1
	strh	w20, [x19,5570]
	strh	w0, [x19,5534]
	ubfiz	x20, x20, 2, 16
	ldr	x0, [x19,1096]
	strh	wzr, [x19,5572]
	ldrb	w2, [x19,3360]
	ldrh	w0, [x0,x20]
	tbz	x0, 0, .L2752
	ldr	x0, [x19,5608]
	mov	w1, 85
	ldr	x0, [x0,8]
	b	.L2766
.L2752:
	ldr	x0, [x19,5608]
	mov	w1, 170
	ldr	x0, [x0,8]
.L2766:
	lsl	w2, w2, 9
	bl	ftl_memset
	b	.L2741
.L2745:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w0, [x19,5570]
	bl	ftl_erase_sblk
	mov	w0, 2
	strh	w0, [x19,5534]
	b	.L2741
.L2746:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w23, 0
	add	x22, x19, 3424
	mov	w20, 65535
	adrp	x24, .LANCHOR3
	mov	w25, 24
	bl	sblk_wait_write_queue_completed
.L2753:
	ldrb	w0, [x22,2155]
	cmp	w0, w23
	bls	.L2767
	add	x0, x22, w23, sxtw 1
	ldrh	w0, [x0,2162]
	cmp	w0, w20
	beq	.L2754
	add	x1, x24, :lo12:.LANCHOR3
	mov	w5, 0
	ldrh	w2, [x1,1418]
	ldrh	w1, [x22,2148]
	madd	w2, w0, w2, w1
	ldr	x0, [x21,8]
	str	w2, [x0]
	mov	w0, 65280
	movk	w0, 0x55aa, lsl 16
	ldr	x1, [x21,8]
	str	w0, [x1,4]
	ldr	x0, [x21,24]
	str	wzr, [x0]
	mov	w0, 1
	ldrb	w3, [x19,1257]
	ldrb	w4, [x19,3360]
	lsl	w0, w0, w3
	sub	w1, w25, w3
	sub	w0, w0, #1
	lsr	w3, w2, w1
	lsl	w1, w0, w1
	and	w0, w3, w0
	bic	w1, w2, w1
	ldr	x3, [x21,24]
	ldr	x2, [x21,8]
	bl	flash_prog_page_en
.L2754:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L2753
.L2767:
	ldrh	w0, [x22,2148]
	adrp	x1, .LANCHOR3+1384
	add	w0, w0, 1
	ldrh	w1, [x1,#:lo12:.LANCHOR3+1384]
	uxth	w0, w0
	strh	w0, [x22,2148]
	cmp	w1, w0
	bhi	.L2741
	mov	w0, 3
	strh	wzr, [x22,2148]
	strh	w0, [x22,2110]
	b	.L2741
.L2747:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w22, 0
	add	x19, x19, 3424
	mov	w24, 65535
	adrp	x25, .LANCHOR3
	mov	w26, -1
	bl	sblk_wait_write_queue_completed
.L2756:
	ldrb	w0, [x19,2155]
	cmp	w0, w22
	bls	.L2768
	add	x27, x19, w22, sxtw 1
	ldrh	w0, [x27,2162]
	add	x27, x27, 16
	cmp	w0, w24
	beq	.L2758
	add	x1, x25, :lo12:.LANCHOR3
	ldrh	w23, [x1,1418]
	ldrh	w1, [x19,2148]
	madd	w23, w0, w23, w1
	str	w23, [x21,40]
	mov	x0, x21
	mov	w1, 1
	bl	sblk_read_page
	ldr	x0, [x21,8]
	ldr	w0, [x0]
	cmp	w0, w23
	beq	.L2758
	mov	w0, w23
	bl	ftl_mask_bad_block
	strh	w26, [x27,2146]
.L2758:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2756
.L2768:
	ldrh	w0, [x19,2148]
	adrp	x1, .LANCHOR3+1384
	add	w0, w0, 1
	ldrh	w1, [x1,#:lo12:.LANCHOR3+1384]
	uxth	w0, w0
	strh	w0, [x19,2148]
	cmp	w1, w0
	bhi	.L2741
	ldr	x0, [x19,2184]
	strh	wzr, [x19,2110]
	bl	zbuf_free
	str	xzr, [x19,2184]
	ldrh	w0, [x19,2112]
	cmp	w0, 15
	bhi	.L2761
	add	w1, w0, 1
	strh	w1, [x19,2112]
	ldrh	w1, [x19,2146]
	add	x0, x19, w0, sxtw 1
	strh	w1, [x0,2114]
	b	.L2762
.L2761:
	ldrh	w0, [x19,2146]
	bl	zftl_insert_free_list
.L2762:
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	ldrh	w1, [x19,5570]
	ldrh	w2, [x19,5536]
	bl	printk
	b	.L2741
.L2742:
	strh	wzr, [x0,5534]
.L2741:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	gc_ink_check_sblk, .-gc_ink_check_sblk
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
	ldr	x0, [x22,1096]
	mov	w1, w20
	ldrh	w2, [x0,x24]
	ldr	w3, [x0,x24]
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	bl	printk
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2769
	ldrh	w0, [x22,1088]
	cmp	w0, w20
	bls	.L2769
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
	ldr	x0, [x22,1096]
	ldrb	w2, [x22,3360]
	ldrh	w0, [x0,x24]
	and	w0, w0, 2047
	cmp	w0, 1
	ldr	x0, [x19,8]
	bgt	.L2771
	mov	w1, 85
	b	.L2786
.L2771:
	mov	w1, 170
.L2786:
	lsl	w2, w2, 9
	adrp	x26, .LANCHOR3
	mov	w22, 0
	mov	x27, x26
	bl	ftl_memset
	mov	w28, 65535
	bl	sblk_wait_write_queue_completed
	mov	w6, 24
.L2773:
	add	x0, x26, :lo12:.LANCHOR3
	adrp	x25, .LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w22
	bls	.L2787
	mov	w25, 0
	mov	w7, 1
.L2776:
	ldrb	w0, [x23,9]
	cmp	w0, w25
	bls	.L2788
	add	x0, x23, w25, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w28
	beq	.L2774
	add	x1, x27, :lo12:.LANCHOR3
	add	x4, x21, :lo12:.LANCHOR0
	mov	w5, 0
	str	x6, [x29,96]
	str	x7, [x29,104]
	ldrh	w2, [x1,1418]
	madd	w2, w0, w2, w22
	ldr	x0, [x19,8]
	str	w2, [x0]
	mov	w0, 65280
	movk	w0, 0x55aa, lsl 16
	ldr	x1, [x19,8]
	str	w0, [x1,4]
	ldr	x0, [x19,24]
	str	wzr, [x0]
	ldrb	w0, [x4,1257]
	ldrb	w4, [x4,3360]
	sub	w1, w6, w0
	lsl	w0, w7, w0
	sub	w0, w0, #1
	lsr	w3, w2, w1
	lsl	w1, w0, w1
	and	w0, w3, w0
	bic	w1, w2, w1
	ldr	x3, [x19,24]
	ldr	x2, [x19,8]
	bl	flash_prog_page_en
	ldr	x6, [x29,96]
	ldr	x7, [x29,104]
.L2774:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2776
.L2788:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2773
.L2787:
	mov	w22, 0
	mov	w27, -1
.L2777:
	add	x0, x25, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w22
	bls	.L2789
	mov	w26, 0
.L2782:
	ldrb	w0, [x23,9]
	cmp	w0, w26
	bls	.L2790
	sxtw	x28, w26
	mov	w1, 65535
	add	x28, x28, 8
	ldrh	w0, [x23,x28,lsl 1]
	cmp	w0, w1
	beq	.L2779
	add	x1, x25, :lo12:.LANCHOR3
	ldrh	w3, [x1,1418]
	mov	w1, 1
	madd	w3, w0, w3, w22
	str	w3, [x19,40]
	mov	x0, x19
	str	x3, [x29,104]
	bl	sblk_read_page
	ldr	x0, [x19,8]
	ldr	x3, [x29,104]
	ldr	w0, [x0]
	cmp	w0, w3
	beq	.L2779
	mov	w0, w3
	bl	ftl_mask_bad_block
	strh	w27, [x23,x28,lsl 1]
.L2779:
	add	w2, w26, 1
	uxth	w26, w2
	b	.L2782
.L2790:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2777
.L2789:
	mov	x0, x19
	add	x21, x21, :lo12:.LANCHOR0
	bl	zbuf_free
	ldr	x0, [x21,1096]
	mov	w1, w20
	ldrh	w2, [x0,x24]
	ldr	w3, [x0,x24]
	adrp	x0, .LC201
	add	x0, x0, :lo12:.LC201
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	bl	printk
.L2769:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	stp	x29, x30, [sp, -80]!
	cmp	w0, 5
	mov	w1, 2
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w21, w0
	mov	w22, w0
	stp	x23, x24, [sp,48]
	mov	w0, 0
	csel	w23, wzr, w1, ne
	mov	w1, w21
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w0, 65535
	mov	w24, w19
	cmp	w19, w0
	beq	.L2793
	adrp	x1, .LANCHOR0
	ubfiz	x25, x19, 2, 16
	add	x0, x1, :lo12:.LANCHOR0
	mov	x20, x1
	ldr	x21, [x0,1096]
	add	x21, x21, x25
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L2794
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1012
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2794:
	ldrb	w3, [x21,2]
	bfi	w3, w22, 5, 3
	ubfx	x1, x3, 3, 2
	mov	w2, w3
	orr	w1, w23, w1
	bfi	w2, w1, 3, 2
	strb	w2, [x21,2]
	cbnz	w23, .L2795
	and	w0, w2, 24
	cmp	w0, 24
	bne	.L2795
	mov	w0, 1
	bfi	w2, w0, 3, 2
	strb	w2, [x21,2]
.L2795:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,3392]
	ldrh	w0, [x0,150]
	cbz	w0, .L2796
	ldr	x0, [x1,1096]
	ldrh	w1, [x0,x25]
	and	w1, w1, 2047
	orr	w1, w23, w1
	cbnz	w1, .L2796
	mov	w0, w24
	bl	ftl_ink_check_sblk
	b	.L2796
.L2793:
	adrp	x20, .LC202
	bl	print_ftl_debug_info
	add	x20, x20, :lo12:.LC202
	mov	w1, w19
	mov	w2, w22
	mov	x0, x20
	bl	printk
	mov	w1, w21
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w1, w19
	mov	w2, w22
	mov	x0, x20
	bl	printk
	bl	dump_all_list_info
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1031
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2796:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LC203
	adrp	x22, .LANCHOR3
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	mov	w23, 65535
	add	x24, x0, 16
	mov	x25, x22
	add	x20, x20, :lo12:.LC203
.L2812:
	mov	w0, w21
	bl	ftl_alloc_sblk
	uxth	w26, w0
	cmp	w26, w23
	beq	.L2812
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
	ldrh	w1, [x2,1384]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w0, w1, w0
	strh	w0, [x19,6]
	strb	w21, [x19,4]
	beq	.L2813
	ldrb	w3, [x2,1329]
	mul	w3, w1, w3
	uxth	w3, w3
.L2813:
	add	x2, x25, :lo12:.LANCHOR3
	adrp	x27, .LANCHOR0
	strh	w3, [x19,12]
	add	x27, x27, :lo12:.LANCHOR0
	ubfiz	x28, x26, 1, 16
	ldr	x0, [x2,1936]
	add	x0, x0, w3, uxth 2
	ldrb	w2, [x2,1329]
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,1112]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L2810
.L2811:
	mov	w1, w26
	mov	x0, x20
	bl	printk
	ldr	x0, [x27,1112]
	mov	w1, -1
	strh	w1, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L2812
.L2810:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	pm_alloc_new_blk
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR3
	ldr	x2, [x0,3392]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	add	x1, x22, :lo12:.LANCHOR3
	strh	w0, [x2,690]
	ldrb	w1, [x1,1329]
	cmp	w1, w0
	bls	.L2818
	add	x0, x2, w0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2819
.L2818:
	adrp	x23, .LC203
	mov	w24, 65535
	add	x23, x23, :lo12:.LC203
.L2820:
	mov	w0, 1
	bl	ftl_alloc_sblk
	uxth	w21, w0
	cmp	w21, w24
	beq	.L2820
	mov	w1, 0
	mov	w0, w21
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_erase_sblk
	mov	w0, w21
	ldr	x1, [x20,3392]
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L2821
	mov	w1, w21
	mov	x0, x23
	bl	printk
	ldr	x1, [x20,1096]
	add	x21, x1, w21, uxth 2
	ldrb	w1, [x21,2]
	orr	w1, w1, -32
	strb	w1, [x21,2]
	b	.L2820
.L2821:
	ldr	x1, [x20,3392]
	adrp	x0, .LANCHOR5+960
	mov	w2, 1
	mov	x20, 0
	str	w2, [x0,#:lo12:.LANCHOR5+960]
	mov	w2, 65535
	strh	wzr, [x1,690]
.L2823:
	add	x3, x1, x20, lsl 1
	uxth	w0, w20
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L2822
	add	x20, x20, 1
	cmp	x20, 128
	bne	.L2823
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 264
	add	x1, x1, 656
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, w20
.L2822:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	add	x0, x1, w0, sxtw 1
	strh	w21, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L2819:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w20, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L2825
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 270
	add	x1, x1, 656
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2825:
	add	x19, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	mov	w0, 24
	ldrb	w2, [x19,1257]
	ldr	x1, [x19,3392]
	sub	w0, w0, w2
	ldrh	w2, [x22,1312]
	sub	w0, w0, w2
	strh	wzr, [x1,696]
	asr	w4, w20, w0
	strh	w20, [x1,692]
	strh	w4, [x1,694]
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L2829
	mov	w2, 1
	mov	w1, w20
	lsl	w2, w2, w0
	adrp	x0, .LC204
	add	x0, x0, :lo12:.LC204
	sub	w2, w2, #1
	mov	w3, w20
	uxth	w4, w4
	bl	printk
.L2829:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
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
	add	x22, x22, 680
.L2835:
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
	ldrh	w2, [x2,1384]
	cmp	w3, w2
	bcs	.L2836
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2837
.L2836:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2837:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2838
	mov	x1, x22
	mov	w2, 303
	mov	x0, x23
	bl	printk
	bl	dump_stack
.L2838:
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x20, :lo12:.LANCHOR3
	adrp	x20, .LANCHOR5
	add	x26, x20, :lo12:.LANCHOR5
	ldr	x2, [x19,3392]
	ldrh	w1, [x0,1418]
	ldrh	w27, [x2,692]
	ldrh	w0, [x2,696]
	mov	w2, 64
	madd	w27, w27, w1, w0
	ldr	x0, [x26,968]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x26,968]
	str	w21, [x0]
	ldr	x1, [x19,3392]
	ldr	x0, [x26,968]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	x0, x24
	ldrb	w1, [x19,3360]
	ldr	x28, [x26,968]
	lsl	w1, w1, 9
	bl	js_hash
	str	w0, [x28,8]
	ldr	x0, [x19,3392]
	mov	x2, x24
	ldr	x3, [x26,968]
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
	beq	.L2839
	ldrb	w1, [x26,976]
	cbz	w1, .L2840
.L2839:
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x20,976]
	b	.L2835
.L2840:
	cmn	w0, #1
	bne	.L2842
	adrp	x0, .LC205
	mov	w1, w27
	add	x0, x0, :lo12:.LC205
	bl	printk
	b	.L2835
.L2842:
	ldrh	w0, [x3,698]
	cmp	w21, w0
	bcs	.L2843
	add	x21, x3, w21, uxtw 2
	str	w27, [x21,704]
.L2843:
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
	tbz	x0, 12, .L2848
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 365
	add	x2, x2, 696
	bl	printk
.L2848:
	adrp	x22, .LANCHOR5
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR5
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC207
	adrp	x25, .LC208
	ldr	x0, [x0,984]
	mov	w26, 0
	add	x24, x24, :lo12:.LC207
	add	x25, x25, :lo12:.LC208
	bl	ftl_memset
	ldr	x1, [x19,1128]
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,1128]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2849:
	add	x21, x22, :lo12:.LANCHOR5
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,992]
	ldrh	w19, [x21,994]
	mov	w1, w20
	ldrh	w28, [x27,2]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,180]
	ldrh	w0, [x21,994]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2850
	ldr	x1, [x27,1128]
	strh	wzr, [x21,994]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,992]
	ldr	x19, [x27,1128]
	ldrb	w1, [x21,993]
	strb	w0, [x21,993]
	mov	x0, x19
	strb	w1, [x21,992]
	ldrh	w1, [x19,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
	ldrb	w1, [x21,992]
	mov	w0, 0
	ldrh	w19, [x27,2]
	mul	w19, w1, w19
	b	.L2858
.L2850:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2851
.L2858:
	mov	w1, w19
	bl	flash_erase_block
.L2851:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR5
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,1128]
	ldr	x0, [x20,984]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,984]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,1128]
	ldr	x3, [x20,984]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,994]
	add	w1, w1, 1
	strh	w1, [x20,994]
	bne	.L2852
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2849
.L2852:
	cbnz	w26, .L2859
	mov	w26, 1
	b	.L2849
.L2859:
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
	tbz	x0, 12, .L2861
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR4
	adrp	x0, .LC209
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC209
	mov	w1, 50
	ldr	x4, [x4,1128]
	add	x2, x2, 720
	mov	w3, 2048
	bl	printk
.L2861:
	mov	w24, 4
	adrp	x25, .LANCHOR5
.L2862:
	mov	w23, 0
.L2865:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x25, :lo12:.LANCHOR5
	mov	w0, 0
	mov	w4, 4
	adrp	x22, .LANCHOR5
	ldrh	w1, [x19,2]
	ldr	x2, [x19,1128]
	ldr	x3, [x21,984]
	madd	w1, w24, w1, w23
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2863
	ldr	x2, [x19,1128]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L2864
.L2863:
	add	w23, w23, 1
	cmp	w23, 4
	bne	.L2865
	add	w24, w24, 1
	cmp	w24, 16
	bne	.L2862
	b	.L2874
.L2864:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,2]
	strb	w1, [x21,992]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,984]
	strb	w0, [x21,993]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2867
	ldr	x0, [x19,1128]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2867
	ldr	w21, [x0,4]
.L2867:
	add	x19, x22, :lo12:.LANCHOR5
	add	x23, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,993]
	ldrh	w1, [x23,2]
	ldr	x3, [x19,984]
	mul	w1, w2, w1
	ldr	x2, [x23,1128]
	bl	flash_read_page_en
	cmn	w0, #1
	bne	.L2891
.L2868:
	add	x19, x22, :lo12:.LANCHOR5
	add	x21, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x19,992]
	ldr	x2, [x21,1128]
	ldr	x3, [x19,984]
	bl	flash_get_last_written_page
	ldrh	w23, [x21,2]
	ldrb	w1, [x19,992]
	uxth	w24, w0
	add	w0, w24, 1
	mov	w21, 0
	strh	w0, [x19,994]
	mul	w23, w1, w23
.L2869:
	sub	w19, w24, w21
	sxth	w19, w19
	tbz	w19, #31, .L2872
	cmn	w19, #1
	bne	.L2871
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC210
	add	x0, x0, :lo12:.LC210
	ldr	x1, [x20,1128]
	ldr	w1, [x1]
	bl	printk
.L2874:
	mov	w0, -1
	b	.L2890
.L2891:
	ldr	x0, [x23,1128]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2868
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L2868
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,992]
	strb	w0, [x19,993]
	b	.L2868
.L2872:
	add	x25, x20, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR5
	mov	w0, 0
	add	w1, w19, w23
	mov	w4, 4
	ldr	x2, [x25,1128]
	ldr	x3, [x3,984]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2870
	ldr	x0, [x25,1128]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	beq	.L2871
.L2870:
	add	w21, w21, 1
	b	.L2869
.L2871:
	cmp	w21, 1
	mov	w0, 0
	bls	.L2890
	bl	flash_info_flush
	mov	w0, 0
.L2890:
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
	tbz	x0, 12, .L2893
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 3450
	add	x2, x2, 744
	bl	printk
.L2893:
	adrp	x21, .LANCHOR5
	mov	x0, x19
	add	x23, x21, :lo12:.LANCHOR5
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w25, 44
	add	x24, x22, 1272
	str	wzr, [x23,324]
	bl	nandc_init
	mov	w0, 1
	add	x1, x20, :lo12:.LANCHOR2
	add	x1, x1, 8
	strb	w0, [x22,1153]
	mov	w0, 3
	str	x1, [x22,1144]
	strb	w0, [x22,1257]
	mov	w1, 0
	mov	w2, 8
	add	x0, x22, 1248
	bl	ftl_memset
	add	x0, x23, 184
	mov	w1, 0
	mov	w2, 32
	mov	w23, 0
	bl	ftl_memset
.L2899:
	mov	w0, w23
	mov	x1, x24
	bl	flash_read_id
	cbnz	w23, .L2894
	ldrb	w0, [x22,1272]
	add	x1, x22, 1272
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2895
.L2897:
	mov	w22, -2
	b	.L2977
.L2895:
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L2897
.L2894:
	ldrb	w0, [x24]
	cmp	w0, 181
	bne	.L2898
	strb	w25, [x24]
.L2898:
	add	w23, w23, 1
	add	x24, x24, 8
	uxtb	w23, w23
	cmp	w23, 4
	bne	.L2899
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x24, x24, 1272
.L2902:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x24
	bl	flash_mem_cmp8
	cbnz	w0, .L2900
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L2897
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2904
.L2900:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 49
	bne	.L2902
	b	.L2897
.L2904:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,2008]
	cmp	w4, w3
	beq	.L2903
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2904
	mov	w2, w0
.L2903:
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x23, 2008
	ubfiz	x2, x2, 5, 32
	add	x1, x1, x2
	add	x0, x24, 1160
	mov	w2, 32
	add	x23, x23, 8
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x24,1028]
	cmp	w0, 8
	bhi	.L2905
	ldrb	w0, [x23,20]
	cmp	w0, 60
	bls	.L2905
	mov	w0, 60
	strb	w0, [x23,20]
.L2905:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2939
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 3477
	add	x2, x2, 744
	bl	printk
.L2939:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1144]
	ldrh	w1, [x1,10]
	cmp	w1, 1023
	bls	.L2906
	mov	w1, 2
	strb	w1, [x0,1257]
.L2906:
	add	x23, x21, :lo12:.LANCHOR5
	mov	w0, 16384
	add	x22, x19, :lo12:.LANCHOR0
	add	x24, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x23,336]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x23,296]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x22,1128]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,344]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,312]
	mov	w0, 64
	bl	ftl_malloc
	strb	wzr, [x23,996]
	str	x0, [x23,984]
	bl	flash_die_info_init
	ldrb	w0, [x24,26]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,28]
	bl	nandc_bch_sel
	str	xzr, [x23,328]
	ldr	x3, [x22,1144]
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldrh	w0, [x3,16]
	ubfx	x1, x0, 8, 3
	strb	w1, [x22,1304]
	ubfx	x1, x0, 3, 1
	strb	w1, [x23,360]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,1305]
	ubfx	x1, x0, 12, 1
	strb	w1, [x2,1344]
	ubfx	x1, x0, 13, 1
	strb	w1, [x2,1330]
	ubfx	x1, x0, 11, 1
	strb	w1, [x22,1268]
	ldrb	w1, [x3,31]
	ubfx	x4, x1, 1, 1
	strb	w4, [x2,1956]
	ubfx	x4, x1, 2, 1
	strb	w4, [x23,997]
	ubfx	x4, x0, 14, 1
	lsr	w0, w0, 15
	strb	w0, [x2,1331]
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
	strb	w1, [x22,1152]
	bne	.L2907
	mov	w1, 70
	strb	w1, [x22,1152]
.L2907:
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1154]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrb	w3, [x0,31]
	tbz	x3, 0, .L2908
	ldrb	w0, [x0,29]
	cbz	w0, .L2909
	mov	w0, 2
	b	.L2979
.L2909:
	mov	w0, 3
.L2979:
	strb	w0, [x1,1154]
.L2908:
	cmp	w2, 8
	bne	.L2910
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1272]
	cmp	w0, 137
	beq	.L2940
	cmp	w0, 44
	bne	.L2910
.L2940:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,36]
	cmp	w0, 3
	bne	.L2910
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1154]
.L2910:
	add	x1, x20, :lo12:.LANCHOR2
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x1, 8
	ldrb	w0, [x1,19]
	ldrh	w1, [x1,16]
	strb	w0, [x2,1136]
	tbz	x1, 6, .L2913
	sub	w1, w0, #17
	cmp	w0, 21
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 2
	bls	.L2941
	cbz	w2, .L2914
.L2941:
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, micron_read_retrial
	add	x1, x1, :lo12:micron_read_retrial
	str	x1, [x0,328]
	mov	w1, 15
	cbz	w2, .L2980
	mov	w1, 4
.L2980:
	strb	w1, [x0,320]
	b	.L2913
.L2914:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2942
	cmp	w0, 33
	bne	.L2918
.L2942:
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,328]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,1137]
	b	.L2983
.L2918:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w2, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L2943
	cbz	w2, .L2920
.L2943:
	add	x3, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x3,328]
	beq	.L2922
	cmp	w0, 68
	beq	.L2922
	mov	w0, 7
	strb	w0, [x3,320]
	b	.L2923
.L2922:
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, 17
	strb	w1, [x0,320]
.L2923:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w2, .L2981
	mov	w1, 5
.L2981:
	strb	w1, [x0,1137]
	b	.L2913
.L2920:
	sub	w1, w0, #36
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L2925
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_3d_read_retrial
	add	x1, x1, :lo12:toshiba_3d_read_retrial
	str	x1, [x0,328]
.L2983:
	mov	w1, 7
	b	.L2980
.L2925:
	cmp	w0, 50
	bne	.L2926
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, samsung_read_retrial
	add	x1, x1, :lo12:samsung_read_retrial
	str	x1, [x0,328]
	mov	w1, 25
	b	.L2980
.L2926:
	cmp	w0, 81
	bne	.L2913
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, ymtc_3d_read_retrial
	add	x1, x1, :lo12:ymtc_3d_read_retrial
	str	x1, [x0,328]
	mov	w1, 7
	strb	w1, [x0,320]
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,1193]
.L2913:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2927
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 3570
	add	x2, x2, 744
	bl	printk
.L2927:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1304]
	strb	wzr, [x0,1192]
	tbz	x1, 0, .L2928
	ldrb	w0, [x0,1272]
	cmp	w0, 155
	beq	.L2929
	mov	w0, 4
	bl	flash_set_interface_mode
	mov	w0, 4
	bl	nandc_set_if_mode
.L2929:
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2982
.L2928:
	mov	w0, 4
.L2982:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2931
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19,1128]
	strb	wzr, [x19,1264]
	strb	w1, [x0,32]
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	b	.L2977
.L2931:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,15]
	cmp	w0, 9
	bne	.L2932
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1128]
	ldrb	w1, [x1,20]
	cmp	w1, 1
	beq	.L2932
	strb	wzr, [x0,1154]
.L2932:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1136]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L2933
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,328]
.L2933:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,1304]
	tbz	x0, 2, .L2935
	ldr	x0, [x19,1128]
	ldrb	w0, [x0,19]
	cbz	w0, .L2935
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w1, [x19,2]
	mov	w0, 0
	ldrb	w2, [x21,992]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,1192]
	cbnz	w0, .L2935
	ldr	x1, [x19,1128]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2935:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2937
	adrp	x2, .LANCHOR4
	adrp	x0, .LC138
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC138
	mov	w1, 3673
	add	x2, x2, 744
	bl	printk
.L2937:
	mov	w22, 0
	bl	nand_flash_print_info
.L2977:
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
	adrp	x22, .LC195
	ldr	x26, [x0,8]
	mov	x25, x0
	mov	w19, w20
	adrp	x27, .LANCHOR3
	adrp	x28, .LANCHOR0
	add	x22, x22, :lo12:.LC195
.L2985:
	add	x2, x27, :lo12:.LANCHOR3
	ldrh	w3, [x2,1384]
	cmp	w3, w19
	bls	.L2997
	ldrh	w21, [x2,1418]
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
	beq	.L2990
	cmn	w23, #1
	bne	.L2986
.L2990:
	mov	w20, 1
.L2986:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2985
.L2997:
	adrp	x0, .LC211
	add	x1, x26, 704
	add	x0, x0, :lo12:.LC211
	mov	w2, 4
	mov	w3, 32
	bl	rknand_print_hex
	mov	x0, x25
	bl	zbuf_free
	cbz	w20, .L2989
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1619
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2989:
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
	tbz	x0, 12, .L2999
	adrp	x0, .LC212
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC212
	bl	printk
.L2999:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3000
	adrp	x0, .LC213
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC213
	bl	printk
.L3000:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3001
	adrp	x0, .LC214
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC214
	bl	printk
.L3001:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3002
	adrp	x0, .LC215
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC215
	bl	printk
.L3002:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3003
	adrp	x0, .LC216
	ldrb	w1, [x19,9]
	add	x0, x0, :lo12:.LC216
	bl	printk
.L3003:
	adrp	x20, .LANCHOR0
	ldrh	w0, [x19,10]
	add	x21, x20, :lo12:.LANCHOR0
	strh	w0, [x19,14]
	ldrh	w1, [x19]
	ldrh	w0, [x21,1088]
	cmp	w1, w0
	bcs	.L2998
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
	add	x0, x0, 384
	str	x0, [x29,128]
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	str	x0, [x29,120]
.L3005:
	adrp	x23, .LANCHOR3
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w25
	bls	.L3008
	ldrb	w24, [x19,5]
.L3006:
	ldrb	w0, [x19,9]
	cmp	w0, w24
	bls	.L3145
	add	x0, x19, w24, sxtw 1
	ldrh	w28, [x0,16]
	mov	w0, 65535
	cmp	w28, w0
	beq	.L3007
	add	x0, x23, :lo12:.LANCHOR3
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrh	w26, [x0,1418]
	ldrb	w3, [x3,3360]
	madd	w0, w28, w26, w25
	str	w0, [x29,152]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w26, w0
	beq	.L3008
	cmn	w0, #1
	beq	.L3009
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L3009
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L3009
	ldr	x0, [x27,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L3008
.L3009:
	adrp	x0, .LANCHOR5
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR5
	strb	w1, [x0,657]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w25, w0, w24
	cmp	w1, w0
	beq	.L3010
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1694
	add	x1, x1, 776
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3010:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L3011
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1695
	add	x1, x1, 776
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3011:
	add	x0, x20, :lo12:.LANCHOR0
	str	x0, [x29,168]
	ldrb	w0, [x0,3360]
	cmp	w0, 8
	bls	.L3012
	ldr	x0, [x27,24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3012
	cmn	w26, #1
	beq	.L3017
	ldr	w1, [x0,4]
	cmn	w1, #1
	beq	.L3052
	ldr	w2, [x0,16]
	mov	w1, 21320
	movk	w1, 0x4841, lsl 16
	cmp	w2, w1
	bne	.L3052
	ldr	w2, [x0,20]
	mov	w1, 1024
	ldr	x0, [x29,160]
	str	x2, [x29,144]
	bl	js_hash
	ldr	x2, [x29,144]
	cmp	w2, w0
	beq	.L3052
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
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x0, [x29,168]
	mov	w2, 4
	ldr	x1, [x27,24]
	ldrb	w3, [x0,3360]
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	lsr	w3, w3, 1
	bl	rknand_print_hex
	b	.L3017
.L3012:
	cmn	w26, #1
	beq	.L3017
.L3052:
	ldr	x1, [x27,24]
	ldr	w0, [x1,4]
	cmn	w0, #1
	beq	.L3017
	ldr	w2, [x1]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	cmp	w2, w1
	beq	.L3017
	bl	lpa_hash_get_ppa
	ldr	x1, [x29,136]
	cbz	x1, .L3016
	ldr	x8, [x27,24]
	ldr	w1, [x8,8]
	cmp	w0, w1
	beq	.L3016
	cmn	w0, #1
	beq	.L3016
	add	x7, x20, :lo12:.LANCHOR0
	add	x4, x23, :lo12:.LANCHOR3
	mov	w5, 24
	mov	w28, 1
	ldrb	w2, [x7,1257]
	ldrh	w1, [x4,1312]
	sub	w2, w5, w2
	lsr	w3, w0, w1
	sub	w1, w2, w1
	lsl	w1, w28, w1
	ldrb	w2, [x4,1314]
	sub	w1, w1, #1
	and	w1, w3, w1
	udiv	w1, w1, w2
	ldr	x2, [x29,136]
	ldrh	w2, [x2]
	cmp	w2, w1, uxth
	bne	.L3016
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
	bcc	.L3016
	ldr	x0, [x27,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L3017
	ldrb	w2, [x7,1257]
	ldrh	w0, [x4,1312]
	sub	w5, w5, w2
	sub	w5, w5, w0
	lsr	w1, w1, w0
	lsl	w28, w28, w5
	ldrb	w0, [x4,1314]
	sub	w28, w28, #1
	and	w1, w28, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L3017
.L3016:
	ldr	x2, [x27,24]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,3372]
	cmp	w3, w0
	bcs	.L3017
	add	x4, x23, :lo12:.LANCHOR3
	ldrb	w0, [x19,9]
	ldrh	w3, [x19,10]
	ldrh	w4, [x4,1384]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L3146
.L3017:
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
.L3007:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L3006
.L3145:
	add	w25, w25, 1
	strb	wzr, [x19,5]
	uxth	w25, w25
	b	.L3005
.L3008:
	add	x2, x23, :lo12:.LANCHOR3
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	strh	w25, [x19,2]
	add	w1, w1, w0
	ldrh	w2, [x2,1384]
	ldrb	w0, [x19,9]
	strb	w24, [x19,5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L3021
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1802
	add	x1, x1, 776
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3021:
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w3, [x19,10]
	ldr	x4, [x0,1936]
	mov	w0, 0
	mov	w2, w0
.L3022:
	cmp	w2, w3
	bcs	.L3147
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L3023
	add	w1, w0, 1
	uxth	w0, w1
.L3023:
	add	w2, w2, 1
	b	.L3022
.L3147:
	add	x2, x23, :lo12:.LANCHOR3
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w24, [x2,1384]
	madd	w0, w1, w24, w0
	uxth	w24, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3025
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	mov	w2, w24
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,1112]
	adrp	x0, .LC219
	add	x0, x0, :lo12:.LC219
	ldrh	w3, [x4,x3]
	bl	printk
.L3025:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldr	x0, [x0,1112]
	strh	w24, [x0,x1,lsl 1]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3026
	adrp	x0, .LC220
	ldr	w1, [x29,176]
	ldr	w2, [x29,180]
	add	x0, x0, :lo12:.LC220
	ldr	w3, [x29,184]
	ldr	w4, [x29,188]
	bl	printk
.L3026:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 0
	adrp	x28, .LC222
	mov	x24, 0
	mov	w26, -1
	ldrb	w2, [x0,3360]
	ldr	x0, [x27,8]
	lsl	w2, w2, 9
	bl	ftl_memset
	adrp	x0, .LC221
	add	x0, x0, :lo12:.LC221
	str	x0, [x29,152]
	add	x0, x28, :lo12:.LC222
	str	x0, [x29,136]
.L3027:
	add	x0, x23, :lo12:.LANCHOR3
	ldr	w1, [x29,168]
	str	w24, [x29,160]
	ldrb	w0, [x0,1329]
	mul	w0, w1, w0
	cmp	w24, w0
	bcs	.L3148
	ldr	w0, [x21,x24,lsl 2]
	cmn	w0, #1
	bne	.L3028
.L3032:
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
	b	.L3029
.L3028:
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
	tbz	x0, 14, .L3030
	ldr	x3, [x27,24]
	mov	w1, w28
	ldr	x0, [x29,152]
	ldr	w2, [x21,x24,lsl 2]
	ldr	w3, [x3,4]
	bl	printk
.L3030:
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
	beq	.L3032
.L3029:
	ldr	w0, [x29,144]
	cbz	w0, .L3034
.L3144:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L3034
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3036
	ldr	x2, [x27,24]
	mov	w1, w28
	ldrh	w0, [x19,12]
	ldrh	w3, [x19,10]
	ldr	w2, [x2,4]
	add	w3, w3, w0
	ldr	x0, [x29,136]
	sub	w3, w3, #1
	bl	printk
.L3036:
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w0, [x19,6]
	ldrb	w2, [x1,1329]
	ldr	w1, [x29,168]
	mul	w2, w1, w2
	ldr	w1, [x29,160]
	add	w2, w2, 1
	sub	w2, w2, w1
	cmp	w0, w2
	bls	.L3034
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
	beq	.L3037
	cbz	w1, .L3034
	add	x0, x29, 240
	ldrh	w1, [x19,10]
	str	w28, [x0,x24,lsl 2]
	ldrh	w0, [x19,12]
	add	w0, w1, w0
	add	x1, x29, 304
	sub	w0, w0, #1
	str	w0, [x1,x24,lsl 2]
	b	.L3034
.L3037:
	cbnz	w1, .L3144
.L3034:
	add	x24, x24, 1
	b	.L3027
.L3148:
	adrp	x24, .LC223
	adrp	x25, .LC222
	mov	x26, 0
	add	x24, x24, :lo12:.LC223
	add	x25, x25, :lo12:.LC222
.L3041:
	add	x28, x23, :lo12:.LANCHOR3
	ldr	w1, [x29,168]
	ldrb	w0, [x28,1329]
	mul	w0, w1, w0
	cmp	w0, w26
	bls	.L3149
	add	x0, x29, 240
	ldr	w0, [x0,x26,lsl 2]
	cmn	w0, #1
	beq	.L3043
	add	x3, x20, :lo12:.LANCHOR0
	ldr	w0, [x21,x26,lsl 2]
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrb	w3, [x3,3360]
	bl	ftl_read_ppa_page
	cmp	w0, 256
	beq	.L3054
	cmn	w0, #1
	bne	.L3043
.L3054:
	add	x3, x20, :lo12:.LANCHOR0
	add	x0, x29, 240
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldr	w0, [x0,x26,lsl 2]
	ldrb	w3, [x3,3360]
	bl	ftl_read_ppa_page
	mov	w28, w0
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3046
	ldr	x2, [x27,24]
	mov	x0, x25
	ldr	w3, [x21,x26,lsl 2]
	mov	w1, w28
	ldr	w2, [x2,8]
	bl	printk
.L3046:
	cmn	w28, #1
	beq	.L3043
	ldr	x1, [x27,24]
	ldr	w2, [x21,x26,lsl 2]
	ldr	w0, [x1,8]
	cmp	w2, w0
	bne	.L3043
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 14, .L3047
	add	x3, x29, 304
	ldr	w1, [x1,4]
	mov	x0, x24
	ldr	w3, [x3,x26,lsl 2]
	bl	printk
.L3047:
	ldr	x1, [x27,24]
	add	x0, x29, 304
	ldr	w2, [x0,x26,lsl 2]
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
.L3043:
	add	x26, x26, 1
	b	.L3041
.L3149:
	mov	x0, x27
	bl	zbuf_free
	ldrh	w1, [x28,1384]
	ldrh	w0, [x19,12]
	ldrb	w2, [x19,9]
	madd	w0, w1, w2, w0
	mov	x1, -4
	add	x0, x1, w0, sxtw 2
	ldr	x1, [x28,1936]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L3049
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1917
	add	x1, x1, 776
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3049:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2998
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2998
.L3146:
	ldr	x0, [x1,3392]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L3018
	str	w2, [x0,8]
.L3018:
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
	b	.L3017
.L2998:
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
	adrp	x0, .LC224
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x0, :lo12:.LC224
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	ldrb	w1, [x19,3362]
	bl	printk
	adrp	x3, .LANCHOR5
	adrp	x0, .LC225
	add	x3, x3, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC225
	ldrb	w1, [x3,640]
	ldrb	w2, [x3,641]
	ldrh	w3, [x3,642]
	bl	printk
	ldr	x2, [x19,3392]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC228
	add	x0, x0, :lo12:.LC228
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,1120]
	adrp	x0, .LC229
	add	x0, x0, :lo12:.LC229
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x20,1384]
	mov	w2, 4
	ldrb	w3, [x20,1329]
	ldr	x1, [x20,1936]
	mul	w3, w0, w3
	adrp	x0, .LC230
	add	x0, x0, :lo12:.LC230
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,1112]
	adrp	x0, .LC231
	ldrh	w3, [x19,1088]
	add	x0, x0, :lo12:.LC231
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,3392]
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,1096]
	adrp	x0, .LC232
	ldrh	w3, [x19,1088]
	add	x0, x0, :lo12:.LC232
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC233
	add	x1, x20, 1424
	add	x0, x0, :lo12:.LC233
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x20,1384]
	mov	w2, 2
	ldrb	w3, [x20,1329]
	ldr	x1, [x20,1944]
	mul	w3, w0, w3
	adrp	x0, .LC234
	add	x0, x0, :lo12:.LC234
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
	ldrh	w4, [x7,1312]
	sub	w5, w5, w6
	mov	w6, 1
	sub	w5, w5, w4
	lsr	w4, w2, w4
	lsl	w5, w6, w5
	ldr	x3, [x3,1096]
	sub	w5, w5, #1
	and	w4, w5, w4
	ldrb	w5, [x7,1314]
	udiv	w4, w4, w5
	add	x4, x3, w4, uxth 2
	ldrb	w3, [x4,2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 7
	cset	w4, eq
	cbnz	w4, .L3155
	cmp	w3, w6
	bne	.L3152
.L3155:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC235
	add	x0, x0, :lo12:.LC235
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L3152:
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
	bls	.L3158
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 32
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3158:
	adrp	x24, .LANCHOR0
	adrp	x20, .LANCHOR5
	add	x26, x24, :lo12:.LANCHOR0
	ldr	x0, [x26,3392]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L3159
	adrp	x0, .LC236
	mov	w1, w21
	add	x0, x0, :lo12:.LC236
	mov	x19, 0
	add	x20, x20, :lo12:.LANCHOR5
	bl	printk
	ldr	x0, [x19,8]
	mov	w1, 255
	ldrh	w2, [x20,998]
	bl	ftl_memset
	ldr	x0, [x26,3392]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L3171
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 800
	mov	w2, 37
	b	.L3172
.L3159:
	add	x0, x0, w21, sxtw 2
	adrp	x25, .LANCHOR3
	ldr	w19, [x0,704]
	add	x0, x25, :lo12:.LANCHOR3
	add	x0, x0, 1968
	lsl	x1, x22, 4
	add	x27, x0, x1
	strh	w21, [x0,x1]
	strh	wzr, [x27,2]
	cbnz	w19, .L3162
	adrp	x0, .LC237
	mov	w1, w21
	mov	w2, w19
	add	x20, x20, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC237
	bl	printk
	ldr	x0, [x27,8]
	mov	w1, 255
	ldrh	w2, [x20,998]
	bl	ftl_memset
	b	.L3171
.L3162:
	add	x23, x20, :lo12:.LANCHOR5
	ldrb	w3, [x26,3360]
	ldr	x1, [x27,8]
	mov	w0, w19
	ldr	x2, [x23,968]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x23,968]
	ldr	w2, [x0]
	cmp	w2, w21
	bne	.L3163
	cmn	w3, #1
	beq	.L3163
	cmp	w3, 512
	beq	.L3163
.L3168:
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x20,968]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L3171
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 800
	mov	w2, 73
	b	.L3172
.L3163:
	adrp	x0, .LC238
	mov	w4, w19
	add	x24, x24, :lo12:.LANCHOR0
	mov	w1, w21
	add	x0, x0, :lo12:.LC238
	add	x23, x25, :lo12:.LANCHOR3
	add	x23, x23, 1968
	add	x26, x20, :lo12:.LANCHOR5
	bl	printk
	add	x23, x23, x22, lsl 4
	ldr	x3, [x24,3392]
	adrp	x0, .LC239
	add	x0, x0, :lo12:.LC239
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x24,3360]
	adrp	x0, .LC218
	ldr	x1, [x23,8]
	add	x0, x0, :lo12:.LC218
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldr	x1, [x26,968]
	adrp	x0, .LC240
	add	x0, x0, :lo12:.LC240
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x1, [x23,8]
	mov	w0, w19
	ldr	x2, [x26,968]
	ldrb	w3, [x24,3360]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	beq	.L3170
	cmn	w0, #1
	bne	.L3166
.L3170:
	add	x25, x25, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	add	x22, x25, x22, lsl 4
	mov	w1, 255
	ldrh	w2, [x20,998]
	ldr	x0, [x22,1976]
	bl	ftl_memset
	mov	w0, -1
	b	.L3161
.L3166:
	ldr	x0, [x26,968]
	ldr	w1, [x0]
	mov	w0, -1
	cmp	w1, w21
	beq	.L3168
	b	.L3161
.L3172:
	bl	printk
	bl	dump_stack
.L3171:
	mov	w0, 0
.L3161:
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
	ldrh	w0, [x1,264]
	sub	w0, w0, #1
	cmp	w2, w0
	bge	.L3174
	ldr	w0, [x1,256]
	cbz	w0, .L3175
.L3174:
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x2, [x1,3392]
	ldrh	w1, [x3,264]
	ldrh	w4, [x2,688]
	sub	w1, w1, #1
	cmp	w4, w1
	bge	.L3176
	ldr	w1, [x3,256]
	cbz	w1, .L3175
.L3176:
	add	x20, x20, :lo12:.LANCHOR5
	add	x0, x2, w0, uxth 1
	ldrh	w21, [x0,416]
	str	wzr, [x20,256]
	mov	w20, 65535
	cmp	w21, w20
	bne	.L3178
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 182
	add	x1, x1, 816
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	add	x0, x1, w0, uxth 1
	ldrh	w21, [x0,416]
	cmp	w21, w20
	beq	.L3175
.L3178:
	bl	pm_select_ram_region
	adrp	x26, .LANCHOR3
	uxth	x20, w0
	add	x0, x26, :lo12:.LANCHOR3
	lsl	x1, x20, 4
	add	x0, x0, 1968
	add	x23, x0, x1
	mov	x22, x20
	ldrh	w0, [x0,x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3179
	ldr	x1, [x23,8]
	cbz	x1, .L3179
	ldrsh	w2, [x23,2]
	tbz	w2, #31, .L3179
	bl	pm_write_page
	ldrh	w0, [x23,2]
	and	w0, w0, 32767
	strh	w0, [x23,2]
.L3179:
	add	x26, x26, :lo12:.LANCHOR3
	mov	w25, 0
	add	x0, x26, 1968
	mov	w23, 24
	mov	w24, 1
	add	x20, x0, x20, lsl 4
.L3180:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,3392]
	ldrh	w1, [x0,698]
	cmp	w1, w25
	bls	.L3190
	ldrb	w2, [x2,1257]
	add	x0, x0, w25, sxtw 2
	ldrh	w1, [x26,1312]
	ldr	w0, [x0,704]
	sub	w2, w23, w2
	lsr	w0, w0, w1
	sub	w1, w2, w1
	lsl	w1, w24, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x26,1314]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L3181
	mov	w0, w25
	mov	w1, w22
	bl	load_l2p_region
	cbnz	w0, .L3182
	ldr	x1, [x20,8]
	mov	w0, w25
	bl	pm_write_page
.L3182:
	mov	w0, -1
	strh	w0, [x20]
.L3181:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L3180
.L3190:
	bl	pm_free_sblk
.L3175:
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
	adrp	x1, .LANCHOR3
	ubfiz	x0, x0, 4, 16
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 1968
	str	x19, [sp,16]
	add	x19, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x19,8]
	bl	pm_write_page
	ldrh	w0, [x19,2]
	and	w0, w0, 32767
	strh	w0, [x19,2]
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	ldr	w0, [x19,960]
	cbz	w0, .L3192
	bl	pm_gc
	str	wzr, [x19,960]
.L3192:
	mov	w0, 0
	ldr	x19, [sp,16]
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
	add	x20, x20, 1968
.L3198:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L3197
	bl	pm_flush_id
.L3197:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L3198
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
	add	x19, x20, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w23, w0
	mov	w0, 1
	strb	w0, [x19,976]
	mov	w0, 64
	str	wzr, [x19,256]
	mov	w24, -1
	str	wzr, [x19,960]
	bl	ftl_malloc
	str	x0, [x19,968]
	adrp	x19, .LANCHOR3
	add	x1, x19, :lo12:.LANCHOR3
	adrp	x25, .LANCHOR0
	add	x21, x1, 1968
	add	x22, x1, 2480
.L3204:
	strh	w24, [x21]
	strh	wzr, [x21,2]
	cbz	w23, .L3203
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x21,8]
.L3203:
	add	x21, x21, 16
	cmp	x21, x22
	bne	.L3204
	add	x0, x19, :lo12:.LANCHOR3
	adrp	x21, .LANCHOR0
	add	x23, x21, :lo12:.LANCHOR0
	mov	w4, 4
	ldr	x26, [x0,1976]
	add	x0, x20, :lo12:.LANCHOR5
	ldr	x1, [x23,3392]
	mov	x2, x26
	ldr	x22, [x0,968]
	ldrb	w0, [x1,694]
	ldrh	w1, [x1,692]
	mov	x3, x22
	bl	flash_get_last_written_page
	sxth	w24, w0
	ldr	x1, [x23,3392]
	mov	w25, w24
	ldrh	w2, [x1,696]
	cmp	w2, w24
	bgt	.L3205
	adrp	x0, .LC241
	ldrh	w1, [x1,692]
	add	x0, x0, :lo12:.LC241
	mov	w3, w24
	adrp	x27, .LC242
	adrp	x28, .LC243
	bl	printk
	add	x28, x28, :lo12:.LC243
	ldr	x0, [x23,3392]
	ldrsh	w23, [x0,696]
	add	w0, w24, 1
	str	w0, [x29,124]
	add	x0, x27, :lo12:.LC242
	str	x0, [x29,112]
.L3206:
	ldr	w0, [x29,124]
	cmp	w23, w0
	bge	.L3227
	add	x27, x21, :lo12:.LANCHOR0
	add	x1, x19, :lo12:.LANCHOR3
	mov	x2, x26
	mov	x3, x22
	ldr	x0, [x27,3392]
	ldrh	w1, [x1,1418]
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
	beq	.L3207
	cmn	w4, #1
	beq	.L3207
	ldr	x0, [x27,3392]
	ldr	w1, [x22]
	ldrh	w0, [x0,698]
	cmp	w1, w0
	bcs	.L3207
	ldr	w2, [x22,8]
	cbz	w2, .L3208
	ldrb	w1, [x27,3360]
	mov	x0, x26
	str	x2, [x29,104]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L3208
	ldr	w1, [x22,8]
	mov	x0, x28
	bl	printk
	b	.L3207
.L3208:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,3392]
	ldr	w0, [x22]
	add	x0, x0, 176
	str	w24, [x1,x0,lsl 2]
.L3207:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L3206
.L3227:
	add	x0, x20, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	mov	w1, 1
	add	w25, w25, 1
	strb	w1, [x0,657]
	ldr	x0, [x21,3392]
	strh	w25, [x0,696]
	bl	pm_free_sblk
.L3205:
	add	x19, x19, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, 255
	add	x19, x19, 1968
	ldr	x0, [x19,8]
	ldrh	w2, [x20,998]
	bl	ftl_memset
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldrb	w0, [x20,657]
	cbz	w0, .L3210
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,8]
	mov	w0, -1
	bl	pm_write_page
.L3210:
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
	bcc	.L3229
	adrp	x0, .LC244
	mov	w1, w3
	add	x0, x0, :lo12:.LC244
	bl	printk
	mov	w0, -1
	cbnz	w25, .L3230
	str	w0, [x22]
	b	.L3230
.L3229:
	adrp	x21, .LANCHOR3
	mov	x19, 0
	add	x1, x21, :lo12:.LANCHOR3
	add	x1, x1, 1968
.L3235:
	ldr	x0, [x1,8]
	cbz	x0, .L3231
	ldrh	w0, [x1]
	cmp	w0, w26
	bne	.L3231
.L3232:
	cbnz	w25, .L3233
	add	x3, x21, :lo12:.LANCHOR3
	add	x23, x23, :lo12:.LANCHOR0
	add	x0, x3, x19, lsl 4
	mov	w2, 1
	ldr	x0, [x0,1976]
	ldr	w1, [x0,x20,lsl 2]
	mov	w0, 24
	str	w1, [x22]
	ldrb	w4, [x23,1257]
	sub	w0, w0, w4
	lsl	w2, w2, w4
	lsr	w1, w1, w0
	sub	w2, w2, #1
	and	w0, w1, w2
	ldrb	w1, [x3,1420]
	cmp	w0, w1
	bcc	.L3234
	mov	w0, -1
	str	w0, [x22]
	b	.L3234
.L3233:
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w3, [x22]
	add	x0, x2, 1968
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w3, [x1,x20,lsl 2]
	strb	w24, [x2,2480]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
.L3234:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 32767
	add	x21, x21, 1968
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L3230
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L3230
.L3231:
	add	w19, w19, 1
	add	x1, x1, 16
	uxth	x19, w19
	cmp	w19, 32
	bne	.L3235
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR3
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 1968
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	beq	.L3236
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L3236
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
.L3236:
	adrp	x0, .LANCHOR5+1000
	strb	w19, [x0,#:lo12:.LANCHOR5+1000]
	mov	w0, w26
	bl	load_l2p_region
	b	.L3232
.L3230:
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
	strb	wzr, [x25,1345]
	add	x24, x20, 80
	ldrh	w1, [x20,80]
	strb	wzr, [x0,218]
	mov	w0, 65535
	cmp	w1, w0
	beq	.L3244
	mov	w0, -1
	strh	w0, [x20,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	ldrb	w0, [x24,9]
	ldrh	w1, [x25,1418]
	sub	w0, w0, #1
	add	x0, x24, w0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x25,1328]
	str	w0, [x29,184]
	cmp	w1, 2
	beq	.L3245
	ldrb	w2, [x21,1268]
	cbnz	w2, .L3245
	ldrb	w2, [x21,1269]
	cbz	w2, .L3246
.L3245:
	add	x2, x22, :lo12:.LANCHOR3
	ldrh	w2, [x2,1346]
	sub	w2, w2, #1
	add	w0, w2, w0
	orr	w0, w0, w1, lsl 24
	b	.L3332
.L3246:
	cmp	w1, 3
	bne	.L3247
	ldrh	w1, [x25,1384]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
.L3332:
	str	w0, [x28,40]
.L3247:
	mov	x0, x28
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3248
	cmn	w0, #1
	beq	.L3248
	ldr	x0, [x28,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3249
.L3248:
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
	adrp	x0, .LC245
	add	x0, x0, :lo12:.LC245
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x28,40]
	ldr	w2, [x28,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L3337
.L3249:
	add	x1, x23, :lo12:.LANCHOR5
	ldrb	w0, [x1,1001]
	cmp	w0, 2
	bne	.L3251
	add	x21, x22, :lo12:.LANCHOR3
	ldrb	w0, [x21,1328]
	cmp	w0, 3
	bne	.L3251
	ldrh	w3, [x1,998]
	ldr	x4, [x1,232]
	ldrh	w1, [x21,1346]
	and	x0, x3, 65532
	ldrb	w2, [x21,1329]
	add	x0, x4, x0
	lsl	w1, w1, 2
	mul	w2, w1, w2
	ldr	x1, [x28,8]
	sub	w2, w2, w3
	uxth	w2, w2
	bl	ftl_memcpy
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L3252
	ldrb	w0, [x0,1269]
	cbz	w0, .L3253
.L3252:
	ldr	w0, [x28,40]
	sub	w0, w0, #1
	b	.L3333
.L3253:
	ldr	w0, [x29,184]
	ldrh	w1, [x21,1384]
	sub	w0, w0, #1
	add	w0, w0, w1
	orr	w0, w0, 33554432
.L3333:
	str	w0, [x28,40]
	mov	w1, 1
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3255
	cmn	w0, #1
	beq	.L3255
	ldr	x0, [x28,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3256
.L3255:
	mov	x0, x28
	bl	zbuf_free
.L3337:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	mov	w21, 0
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,1120]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L3250
.L3256:
	add	x2, x23, :lo12:.LANCHOR5
	ldr	x1, [x28,8]
	ldr	x0, [x2,232]
	ldrh	w2, [x2,998]
	b	.L3334
.L3251:
	add	x2, x22, :lo12:.LANCHOR3
	add	x1, x23, :lo12:.LANCHOR5
	ldrh	w0, [x2,1346]
	ldrb	w2, [x2,1329]
	mul	w2, w0, w2
	ldr	x0, [x1,232]
	ldr	x1, [x28,8]
	lsl	w2, w2, 2
.L3334:
	add	x21, x22, :lo12:.LANCHOR3
	bl	ftl_memcpy
	ldrh	w0, [x21,1346]
	ldrb	w1, [x21,1329]
	mul	w1, w0, w1
	add	x0, x19, :lo12:.LANCHOR0
	lsl	w2, w1, 2
	ldrb	w0, [x0,3360]
	cmp	w0, w1, lsr 6
	bge	.L3258
	add	x25, x23, :lo12:.LANCHOR5
	mov	w1, 0
	ldr	x0, [x25,240]
	bl	ftl_memset
	ldrb	w26, [x25,1001]
	cmp	w26, 1
	bne	.L3298
	ldrh	w0, [x21,1346]
	ldrb	w1, [x21,1329]
	ldrh	w21, [x25,998]
	ldr	x2, [x28,8]
	mul	w1, w0, w1
	ldr	x0, [x25,240]
	sub	w21, w21, w1, lsl 2
	add	x1, x2, w1, sxtw 2
	mov	w2, w21
	bl	ftl_memcpy
	mov	w10, w26
	b	.L3259
.L3258:
	add	x0, x23, :lo12:.LANCHOR5
	ldr	x3, [x28,8]
	add	x1, x3, w1, sxtw 2
	ldr	x0, [x0,240]
	bl	ftl_memcpy
	mov	w10, 0
	mov	w21, w10
	b	.L3259
.L3298:
	mov	w10, 1
	mov	w21, 0
.L3259:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w25, [x0,1328]
	ldrh	w27, [x0,1384]
	cmp	w25, 2
	bne	.L3260
	ldrb	w0, [x0,1330]
	cbz	w0, .L3261
.L3260:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1268]
	cbz	w0, .L3262
.L3261:
	mul	w27, w27, w25
	mov	w25, 1
.L3262:
	adrp	x0, .LC246
	mov	w26, 0
	add	x0, x0, :lo12:.LC246
	str	w26, [x29,172]
	str	x0, [x29,144]
.L3263:
	cmp	w26, w27
	bcs	.L3275
	add	w0, w26, w26, lsl 1
	mov	w11, 0
	sub	w0, w0, #1
	str	w0, [x29,168]
	sub	w0, w26, #1
	str	w0, [x29,152]
.L3276:
	ldrb	w0, [x24,9]
	cmp	w11, w0
	bge	.L3338
	sxtw	x0, w11
	mov	x12, 0
	str	x0, [x29,160]
.L3273:
	add	w2, w12, 1
	ldr	w0, [x29,172]
	cmp	w2, w25
	add	w13, w0, w12
	bhi	.L3339
	ldr	x0, [x29,160]
	add	x3, x22, :lo12:.LANCHOR3
	add	x0, x0, 8
	ldrh	w1, [x24,x0,lsl 1]
	ldrh	w0, [x3,1418]
	mul	w0, w1, w0
	ldrb	w1, [x3,1328]
	str	w0, [x29,184]
	cmp	w1, 2
	beq	.L3264
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w4, [x3,1268]
	cbz	w4, .L3265
.L3264:
	ldr	w3, [x29,152]
	add	w0, w3, w0
	add	w0, w0, w2
	orr	w0, w0, w1, lsl 24
	b	.L3335
.L3265:
	cmp	w1, 3
	bne	.L3267
	ldrb	w1, [x3,1269]
	cbz	w1, .L3268
	ldr	w1, [x29,168]
	add	w0, w0, w1
	add	w0, w0, w2
	orr	w0, w0, 50331648
	b	.L3335
.L3268:
	add	w0, w0, w26
	orr	w0, w0, w2, lsl 24
	b	.L3335
.L3267:
	add	w0, w0, w26
.L3335:
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
	cbz	w10, .L3269
	add	x0, x23, :lo12:.LANCHOR5
	lsl	x1, x13, 2
	ldr	x0, [x0,240]
	ldr	w2, [x0,x1]
	cbnz	w2, .L3269
	ldr	x2, [x28,24]
	ldr	w2, [x2,8]
	str	w2, [x0,x1]
.L3269:
	add	x0, x23, :lo12:.LANCHOR5
	lsl	x13, x13, 2
	ldr	x7, [x28,24]
	ldr	x1, [x0,232]
	ldr	w6, [x7,4]
	ldr	w3, [x1,x13]
	cmp	w3, w6
	bne	.L3270
	ldr	x0, [x0,240]
	ldr	w1, [x0,x13]
	ldr	w0, [x7,8]
	cmp	w1, w0
	beq	.L3300
.L3270:
	add	x14, x23, :lo12:.LANCHOR5
	ldr	w0, [x7,12]
	str	x12, [x29,104]
	str	x10, [x29,112]
	ldr	x4, [x14,240]
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
	ldr	x0, [x14,232]
	ldr	x10, [x29,112]
	ldr	x12, [x29,104]
	ldr	w0, [x0,x13]
	cmn	w0, #1
	beq	.L3300
	mov	x0, x28
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	ldr	x1, [x0,1112]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,1120]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L3250
.L3300:
	add	x12, x12, 1
	b	.L3273
.L3339:
	str	w13, [x29,172]
	add	w11, w11, 1
	b	.L3276
.L3338:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0,1330]
	cmp	w0, wzr
	csinc	w26, w26, w26, eq
	add	w26, w26, 1
	b	.L3263
.L3275:
	mov	x0, x28
	mov	w21, 0
	mov	w28, w21
	bl	zbuf_free
	str	w21, [x29,168]
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR3
	ldrh	w2, [x20,80]
	ldr	x1, [x0,1112]
	ldrh	w3, [x3,1346]
	ldrb	w0, [x24,9]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
.L3277:
	cmp	w28, w27
	bcs	.L3291
	add	w0, w28, w28, lsl 1
	str	wzr, [x29,172]
	sub	w0, w0, #1
	str	w0, [x29,144]
	sub	w0, w28, #1
	str	w0, [x29,128]
.L3292:
	ldrb	w0, [x24,9]
	ldr	w1, [x29,172]
	cmp	w1, w0
	bge	.L3288
	ldrsw	x0, [x29,172]
	mov	w26, 1
	str	x0, [x29,136]
.L3289:
	cmp	w26, w25
	bhi	.L3340
	add	x1, x23, :lo12:.LANCHOR5
	ldr	w0, [x29,168]
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,232]
	ldr	w5, [x2,x0]
	cmn	w5, #1
	beq	.L3278
	ldr	x1, [x1,240]
	str	x5, [x29,152]
	ldr	w0, [x1,x0]
	str	w0, [x29,160]
	mov	w0, w5
	bl	lpa_hash_get_ppa
	str	w0, [x29,188]
	cmn	w0, #1
	ldr	x5, [x29,152]
	bne	.L3279
	mov	w0, w5
	add	x1, x29, 188
	mov	w2, 0
	bl	pm_log2phys
	ldr	x5, [x29,152]
.L3279:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	ldrh	w1, [x24,x0,lsl 1]
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w2, [x0,1418]
	ldrb	w0, [x0,1328]
	cmp	w0, 2
	mul	w1, w1, w2
	beq	.L3280
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w6, [x2,1268]
	cbz	w6, .L3281
.L3280:
	ldr	w2, [x29,128]
	add	w1, w2, w1
	add	w1, w1, w26
	orr	w1, w1, w0, lsl 24
	b	.L3336
.L3281:
	cmp	w0, 3
	bne	.L3283
	ldrb	w2, [x2,1269]
	lsl	w0, w26, 24
	cbz	w2, .L3284
	ldr	w2, [x29,144]
	add	w2, w1, w2
	add	w2, w2, w26
	orr	w1, w0, w2
	b	.L3336
.L3284:
	add	w1, w1, w28
	orr	w1, w0, w1
	b	.L3336
.L3283:
	add	w1, w1, w28
.L3336:
	add	x2, x22, :lo12:.LANCHOR3
	ldr	w0, [x29,160]
	str	w1, [x29,184]
	mov	w3, 24
	ldrh	w1, [x2,1312]
	lsr	w6, w0, w1
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1257]
	sub	w0, w3, w0
	sub	w1, w0, w1
	mov	w0, 1
	lsl	w1, w0, w1
	ldrb	w0, [x2,1314]
	sub	w1, w1, #1
	and	w1, w6, w1
	udiv	w1, w1, w0
	uxth	w0, w1
	ldr	w1, [x29,160]
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w0, w1
	bne	.L3285
	mov	w0, w5
	add	x1, x29, 184
	mov	w2, 1
	add	w21, w21, 1
	bl	pm_log2phys
	ldrh	w0, [x29,152]
	bl	ftl_vpn_decrement
	b	.L3286
.L3285:
	ldr	w1, [x29,184]
	cmp	w0, w1
	csinc	w21, w21, w21, ne
.L3286:
	ldr	w0, [x29,168]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,168]
	ldr	w0, [x29,152]
	ldr	x1, [x2,1096]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L3278
	lsl	x0, x0, 1
	ldr	x2, [x2,1112]
	ldrh	w5, [x2,x0]
	cbz	w5, .L3278
	strh	w1, [x2,x0]
.L3278:
	add	w26, w26, 1
	b	.L3289
.L3340:
	ldr	w0, [x29,172]
	add	w0, w0, 1
	str	w0, [x29,172]
	b	.L3292
.L3288:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0,1330]
	cmp	w0, wzr
	csinc	w28, w28, w28, eq
	add	w28, w28, 1
	b	.L3277
.L3291:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,1112]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20,80]
	bl	zftl_insert_data_list
.L3250:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20,80]
	ubfiz	x2, x0, 1, 16
	ldr	x1, [x1,1112]
	ldrh	w1, [x1,x2]
	cbnz	w1, .L3293
	bl	ftl_dump_write_open_sblk
.L3293:
	ldrh	w1, [x20,80]
	adrp	x0, .LC247
	mov	w2, w21
	add	x0, x0, :lo12:.LC247
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
	beq	.L3294
	ldrh	w1, [x1,1088]
	cmp	w1, w0
	bhi	.L3295
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 517
	add	x1, x1, 824
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3295:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
.L3294:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,3392]
	strh	w0, [x1,126]
	ldr	x1, [x19,1120]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3243
.L3244:
	ldrh	w0, [x20,130]
	cmp	w0, w1
	beq	.L3243
	ldr	x1, [x21,3392]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L3297
	bl	pm_flush
	ldr	x0, [x21,1120]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x21,3392]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L3297:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,1120]
	strh	w1, [x0,130]
.L3243:
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
	ldrh	w25, [x1,1346]
	add	x1, x20, 80
	str	x1, [x29,128]
	ldrb	w1, [x1,9]
	mul	w25, w25, w1
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3342
	adrp	x0, .LC248
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC248
	bl	printk
.L3342:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR5
	ldrh	w2, [x20,80]
	mov	w22, 0
	mov	w24, w22
	ldr	x1, [x0,1112]
	add	x0, x21, :lo12:.LANCHOR5
	ldrb	w0, [x0,1001]
	sub	w0, w25, w0
	strh	w0, [x1,x2,lsl 1]
	adrp	x0, .LC250
	add	x0, x0, :lo12:.LC250
	str	x0, [x29,112]
	adrp	x0, .LC249
	add	x0, x0, :lo12:.LC249
	str	x0, [x29,104]
.L3343:
	cmp	w24, w25
	beq	.L3375
	add	x0, x21, :lo12:.LANCHOR5
	ldr	x0, [x0,232]
	ldr	w2, [x0,w24,sxtw 2]
	cmn	w2, #1
	beq	.L3344
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,140]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3345
	ldr	x0, [x29,104]
	mov	w3, w24
	ldr	w1, [x29,140]
	bl	printk
.L3345:
	mov	w26, w24
.L3351:
	add	x1, x21, :lo12:.LANCHOR5
	sbfiz	x27, x26, 2, 32
	ldr	x0, [x1,232]
	ldr	w4, [x0,x27]
	cmn	w4, #1
	beq	.L3346
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,140]
	ldrb	w0, [x0,3360]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w2, w0, uxth
	bne	.L3346
	ldr	x0, [x1,240]
	str	x4, [x29,96]
	ldr	w28, [x0,x27]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x4, [x29,96]
	bne	.L3347
	mov	w0, w4
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x4, [x29,96]
.L3347:
	ldr	w3, [x29,156]
	cmp	w3, w28
	bne	.L3348
	add	x0, x21, :lo12:.LANCHOR5
	add	x1, x29, 152
	mov	w2, 1
	add	w22, w22, 1
	ldr	x0, [x0,248]
	ldr	w0, [x0,x27]
	str	w0, [x29,152]
	mov	w0, w4
	bl	pm_log2phys
	ldr	x0, [x29,120]
	mov	w2, 24
	add	x3, x0, :lo12:.LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,1257]
	ldrh	w1, [x3,1312]
	sub	w0, w2, w0
	mov	w2, 1
	sub	w0, w0, w1
	lsr	w28, w28, w1
	lsl	w0, w2, w0
	sub	w0, w0, #1
	and	w28, w0, w28
	ldrb	w0, [x3,1314]
	udiv	w0, w28, w0
	b	.L3374
.L3348:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3350
	ldr	x0, [x29,112]
	mov	w1, w4
	mov	w2, w28
	bl	printk
.L3350:
	ldr	x0, [x29,128]
	ldrh	w0, [x0]
.L3374:
	bl	ftl_vpn_decrement
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, -1
	ldr	x0, [x0,232]
	str	w1, [x0,x27]
.L3346:
	add	w26, w26, 1
	cmp	w26, w25
	bne	.L3351
.L3344:
	add	w24, w24, 1
	b	.L3343
.L3375:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3353
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,1120]
	ldr	x2, [x0,1112]
	adrp	x0, .LC251
	add	x0, x0, :lo12:.LC251
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
.L3353:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w22
	beq	.L3354
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 898
	add	x1, x1, 840
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3354:
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
	add	x2, x1, w0, sxtw 1
	ldrh	w22, [x2,40]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w6, [x2,1329]
	ldrh	w2, [x1,2]
.L3377:
	cmp	w22, w5
	bne	.L3406
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w6
	bne	.L3378
	add	w2, w2, 1
	mov	w0, 0
	mov	w4, 1
	uxth	w2, w2
.L3378:
	add	x3, x1, w0, sxtw 1
	ldrh	w22, [x3,40]
	mov	w3, 1
	b	.L3377
.L3406:
	cbz	w4, .L3380
	add	x1, x19, :lo12:.LANCHOR0
	strh	w2, [x1,3426]
.L3380:
	cbz	w3, .L3381
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,3428]
.L3381:
	mov	w0, 1
	mov	w24, 1
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x23, x0
.L3382:
	add	x20, x19, 3424
	ldrb	w0, [x20,6]
	cmp	w24, w0
	bgt	.L3407
	add	x3, x21, :lo12:.LANCHOR3
	cmp	w0, 2
	ldrh	w1, [x3,1418]
	mul	w1, w22, w1
	bne	.L3383
	ldrh	w2, [x20,2]
	ldrb	w0, [x3,1328]
	add	w1, w1, w2, lsl 1
	sub	w1, w1, #1
	add	w1, w1, w24
	b	.L3405
.L3383:
	cmp	w0, 3
	bne	.L3385
	ldrb	w0, [x19,1268]
	cbnz	w0, .L3386
	ldrb	w0, [x19,1269]
	cbz	w0, .L3387
.L3386:
	ldrh	w2, [x19,3426]
	add	x0, x21, :lo12:.LANCHOR3
	add	w2, w2, w2, lsl 1
	ldrb	w0, [x0,1328]
	sub	w2, w2, #1
	add	w1, w2, w1
	add	w1, w1, w24
.L3405:
	orr	w1, w1, w0, lsl 24
	b	.L3404
.L3387:
	ldrh	w0, [x20,2]
	add	w1, w1, w0
	orr	w1, w1, w24, lsl 24
	b	.L3404
.L3385:
	ldrh	w0, [x20,2]
	add	w1, w1, w0
.L3404:
	str	w1, [x23,40]
	mov	x0, x23
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L3390
	cmn	w0, #1
	beq	.L3390
	ldr	x0, [x23,24]
	ldr	w20, [x0,4]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L3391
	ldr	w0, [x19,3372]
	cmp	w20, w0
	bcs	.L3391
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L3391:
	ldr	w2, [x23,40]
	ldr	w0, [x29,76]
	cmp	w2, w0
	bne	.L3390
	add	x1, x21, :lo12:.LANCHOR3
	ldrh	w3, [x19,3448]
	ldr	x1, [x1,1320]
	str	w2, [x1,x3,lsl 2]
	ldrh	w1, [x19,3448]
	add	w1, w1, 1
	strh	w1, [x19,3448]
.L3390:
	ldrh	w0, [x19,3450]
	add	w24, w24, 1
	add	w0, w0, 1
	strh	w0, [x19,3450]
	b	.L3382
.L3407:
	mov	x0, x23
	add	x21, x21, :lo12:.LANCHOR3
	bl	zbuf_free
	ldrb	w0, [x20,4]
	ldrb	w1, [x21,1329]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x20,4]
	cmp	w1, w0
	bne	.L3376
	ldrh	w0, [x20,2]
	strb	wzr, [x20,4]
	add	w0, w0, 1
	strh	w0, [x20,2]
.L3376:
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
	bne	.L3409
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1505
	add	x1, x1, 864
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3409:
	add	x2, x20, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,3424]
	cmp	w1, w3
	beq	.L3410
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3411
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,1112]
	adrp	x0, .LC252
	add	x0, x0, :lo12:.LC252
	ldrh	w2, [x2,x3]
	bl	printk
.L3411:
	add	x19, x20, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x19, 3424
	add	x1, x19, 3464
	ldrh	w0, [x19,3424]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L3412
	mov	w0, -1
	strh	w0, [x19,3424]
	b	.L3464
.L3412:
	ldrh	w3, [x19,3424]
	ldr	x2, [x19,1096]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L3413
	cmp	w4, 224
	beq	.L3413
	ldr	x2, [x19,1120]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L3413
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L3413
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L3414
.L3413:
	cbnz	w4, .L3415
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L3416
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1530
	add	x1, x1, 864
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3416:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3424]
	ldr	x0, [x0,1112]
	strh	wzr, [x0,x1,lsl 1]
.L3415:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x20,3448]
	strh	w0, [x20,3424]
.L3464:
	mov	w0, 0
	b	.L3410
.L3414:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, w0, sxtw 1
	ldrh	w21, [x0,40]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L3417
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1540
	add	x1, x1, 864
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3417:
	add	x3, x20, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR3
	add	x1, x22, :lo12:.LANCHOR3
	mov	w26, 1
	ldrh	w2, [x3,3424]
	ldr	x3, [x3,1096]
	ldrh	w0, [x1,1384]
	add	x2, x3, x2, lsl 2
	ldrh	w23, [x1,1418]
	sub	w0, w0, #1
	ldrb	w2, [x2,2]
	uxth	w0, w0
	mul	w23, w21, w23
	and	w2, w2, 224
	cmp	w2, 160
	bne	.L3418
	ldrb	w2, [x1,1328]
	cmp	w2, 2
	uxth	w26, w2
	orr	w23, w23, w2, lsl 24
	bne	.L3419
	ldrh	w0, [x1,1346]
	sub	w0, w0, #1
	uxth	w0, w0
.L3419:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,1268]
	cbnz	w2, .L3420
	ldrb	w1, [x1,1269]
	cbz	w1, .L3418
.L3420:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w0, [x0,1346]
	sub	w0, w0, #1
	uxth	w0, w0
.L3418:
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
	beq	.L3467
	cmn	w0, #1
	beq	.L3467
	ldr	x0, [x19,24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3423
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 864
	mov	w2, 1578
	b	.L3468
.L3423:
	add	x2, x22, :lo12:.LANCHOR3
	cmp	w26, 3
	ldrb	w1, [x2,1329]
	ldrh	w25, [x2,1384]
	mul	w25, w1, w25
	mul	w25, w26, w25
	uxth	w25, w25
	bne	.L3424
	adrp	x26, .LANCHOR5
	add	x3, x26, :lo12:.LANCHOR5
	ldrb	w4, [x3,1001]
	cmp	w4, 2
	bne	.L3424
	ldrh	w24, [x2,1346]
	ldrh	w0, [x3,998]
	lsl	w24, w24, 2
	mul	w24, w1, w24
	ldr	x1, [x2,1320]
	sub	w24, w24, w0
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,8]
	uxth	w24, w24
	mov	w2, w24
	bl	ftl_memcpy
	ldr	x0, [x19,24]
	ldr	w27, [x0,4]
	cbz	w27, .L3425
	ldr	x0, [x19,8]
	mov	w1, w24
	bl	js_hash
	cmp	w27, w0
	beq	.L3425
	mov	x0, x19
	bl	zbuf_free
	ldr	x1, [x19,24]
	adrp	x0, .LC253
	add	x0, x0, :lo12:.LC253
	ldr	w2, [x19,40]
	mov	w3, w24
	ldr	w1, [x1,4]
	b	.L3466
.L3425:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,1268]
	cbnz	w1, .L3426
	ldrb	w0, [x0,1269]
	cbz	w0, .L3427
.L3426:
	sub	w0, w23, #1
	str	w0, [x19,40]
	b	.L3428
.L3427:
	add	x1, x22, :lo12:.LANCHOR3
	ldrh	w0, [x1,1384]
	ldrh	w1, [x1,1418]
	sub	w0, w0, #1
	mul	w21, w21, w1
	uxth	w0, w0
	orr	w0, w0, 33554432
	orr	w21, w0, w21
	str	w21, [x19,40]
.L3428:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x19,52]
	cmp	w0, 512
	beq	.L3467
	cmn	w0, #1
	beq	.L3467
	ldr	x0, [x19,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3431
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 864
	mov	w2, 1619
.L3468:
	bl	printk
	bl	dump_stack
.L3467:
	mov	x0, x19
	bl	zbuf_free
	b	.L3465
.L3431:
	add	x0, x22, :lo12:.LANCHOR3
	add	x26, x26, :lo12:.LANCHOR5
	ldr	x1, [x19,8]
	ldr	x0, [x0,1320]
	ldrh	w2, [x26,998]
	b	.L3463
.L3424:
	ldr	w23, [x0,4]
	lsl	w21, w25, 2
	ldr	x0, [x19,8]
	mov	w1, w21
	bl	js_hash
	cmp	w23, w0
	beq	.L3433
	mov	x0, x19
	bl	zbuf_free
	ldr	x1, [x19,24]
	adrp	x0, .LC253
	ldr	w2, [x19,40]
	add	x0, x0, :lo12:.LC253
	mov	w3, w21
	ldr	w1, [x1,4]
.L3466:
	bl	printk
.L3465:
	mov	w0, -1
	b	.L3410
.L3433:
	add	x0, x22, :lo12:.LANCHOR3
	ldr	x1, [x19,8]
	mov	w2, w21
	ldr	x0, [x0,1320]
.L3463:
	bl	ftl_memcpy
	mov	x27, 0
	add	x0, x22, :lo12:.LANCHOR3
	add	x21, x20, :lo12:.LANCHOR0
	mov	w24, 24
	mov	w26, 1
	ldr	x23, [x0,1320]
.L3434:
	cmp	w25, w27
	ble	.L3469
	ldr	w0, [x23,x27,lsl 2]
	cmn	w0, #1
	beq	.L3436
	bl	lpa_hash_get_ppa
	str	w0, [x29,108]
	cmn	w0, #1
	bne	.L3437
	ldr	w0, [x23,x27,lsl 2]
	add	x1, x29, 108
	mov	w2, 0
	bl	pm_log2phys
.L3437:
	add	x2, x22, :lo12:.LANCHOR3
	ldrb	w0, [x21,1257]
	ldr	w3, [x29,108]
	sub	w0, w24, w0
	ldrh	w4, [x21,3424]
	ldrh	w1, [x2,1312]
	sub	w0, w0, w1
	lsr	w1, w3, w1
	lsl	w0, w26, w0
	sub	w0, w0, #1
	and	w0, w0, w1
	ldrb	w1, [x2,1314]
	udiv	w0, w0, w1
	cmp	w0, w4
	bne	.L3436
	ldrh	w4, [x21,3448]
	ldr	x0, [x2,1320]
	str	w3, [x0,x4,lsl 2]
	ldrh	w0, [x21,3448]
	add	w0, w0, 1
	strh	w0, [x21,3448]
.L3436:
	bl	timer_get_time
	add	x27, x27, 1
	b	.L3434
.L3469:
	mov	x0, x19
	bl	zbuf_free
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3424]
	ldr	x3, [x0,1112]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,3448]
	cmp	w2, w3
	beq	.L3440
	adrp	x0, .LC254
	add	x0, x0, :lo12:.LC254
	bl	printk
.L3440:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x20,3424]
	ldr	x1, [x20,1112]
	ldrh	w3, [x20,3448]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x20,3452]
	ldrh	w0, [x20,3448]
.L3410:
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
	beq	.L3471
	adrp	x20, .LANCHOR4
	adrp	x22, .LC0
	add	x20, x20, :lo12:.LANCHOR4
	mov	w21, 0
	add	x22, x22, :lo12:.LC0
	add	x20, x20, 880
.L3472:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,1120]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L3473
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L3474
	adrp	x4, .LANCHOR3
	ldrb	w2, [x24,1257]
	add	x4, x4, :lo12:.LANCHOR3
	mov	w3, 24
	sub	w2, w3, w2
	mov	w1, 1
	ldr	w3, [x29,76]
	ldrh	w0, [x4,1312]
	sub	w2, w2, w0
	lsr	w3, w3, w0
	lsl	w2, w1, w2
	ldrb	w0, [x4,1314]
	sub	w2, w2, #1
	and	w2, w2, w3
	udiv	w0, w2, w0
	mov	w2, 0
	bl	gc_add_sblk
.L3474:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,1120]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L3475
	mov	x1, x20
	mov	w2, 2163
	mov	x0, x22
	bl	printk
	bl	dump_stack
.L3475:
	mov	x0, x23
	bl	zbuf_free
.L3473:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,1120]
	ldr	w2, [x2,3372]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L3476
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
	b	.L3470
.L3476:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L3470
	adrp	x1, .LANCHOR5+998
	add	w0, w21, 1
	uxth	w0, w0
	ldrh	w1, [x1,#:lo12:.LANCHOR5+998]
	cmp	w21, w1, lsr 2
	bcs	.L3470
	mov	w21, w0
	b	.L3472
.L3471:
	ldr	w1, [x0,536]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L3481
	ldr	x3, [x3,3392]
	ldr	w1, [x0,540]
	ldr	w3, [x3,44]
	add	w1, w1, 4096
	add	w1, w1, 904
	cmp	w3, w1
	bls	.L3470
.L3481:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,3392]
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w2, [x0,536]
	str	wzr, [x0,544]
.L3470:
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
	bcs	.L3486
	bl	timer_get_time
	ldr	x21, [x20,1120]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L3486
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,1120]
	ldrh	w2, [x20,1088]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L3490
	ldr	x2, [x20,3392]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L3491
.L3490:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,3392]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L3491:
	ldr	w26, [x0,600]
	mov	w0, 65535
	uxth	w20, w26
	cmp	w20, w0
	bne	.L3492
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2504
	add	x1, x1, 904
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3492:
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
	cbz	w1, .L3486
	uxtw	x22, w20
	ldr	x24, [x21,1096]
	add	x24, x24, x22, lsl 2
	ldrb	w1, [x24,2]
	tst	w1, 192
	and	w2, w1, 224
	beq	.L3493
	cmp	w2, 224
	beq	.L3493
	ldr	x1, [x21,1120]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L3493
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L3493
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L3494
.L3493:
	cbnz	w2, .L3486
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x22]
	cbz	w0, .L3495
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2521
	add	x1, x1, 904
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3495:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,1112]
	strh	wzr, [x0,x22]
	b	.L3486
.L3494:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, w0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L3497
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 2529
	add	x1, x1, 904
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3497:
	adrp	x25, .LANCHOR3
	ldrb	w3, [x24,2]
	add	x2, x25, :lo12:.LANCHOR3
	mov	w27, 1
	and	w3, w3, 224
	cmp	w3, 160
	ldrh	w0, [x2,1384]
	ldrh	w1, [x2,1418]
	sub	w0, w0, #1
	mul	w1, w21, w1
	uxth	w0, w0
	bne	.L3498
	ldrb	w3, [x2,1328]
	cmp	w3, 2
	uxth	w27, w3
	orr	w1, w1, w3, lsl 24
	bne	.L3498
	ldrh	w0, [x2,1346]
	sub	w0, w0, #1
	uxth	w0, w0
.L3498:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x25, :lo12:.LANCHOR3
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,1346]
	mov	w1, 255
	ldrb	w2, [x21,1329]
	mul	w2, w0, w2
	ldr	x0, [x21,1320]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L3510
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L3499
.L3510:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L3523
.L3499:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3501
	mov	w0, w20
	mov	w1, 1
.L3523:
	bl	gc_add_sblk
	mov	x0, x23
	bl	zbuf_free
	b	.L3486
.L3501:
	ldrb	w0, [x21,1329]
	mov	x28, 0
	ldrh	w1, [x21,1384]
	mov	w21, w28
	mul	w0, w0, w1
	mul	w27, w27, w0
	uxth	w0, w27
	ldr	x27, [x23,8]
	str	w0, [x29,108]
	and	w0, w26, 65535
	str	w0, [x29,104]
.L3502:
	ldr	w0, [x29,108]
	cmp	w0, w28
	ble	.L3524
	ldr	w0, [x27,x28,lsl 2]
	cmn	w0, #1
	beq	.L3503
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L3504
	ldr	w0, [x27,x28,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L3504:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x25, :lo12:.LANCHOR3
	mov	w1, 24
	ldrb	w0, [x0,1257]
	ldrh	w4, [x2,1312]
	sub	w0, w1, w0
	mov	w1, 1
	sub	w0, w0, w4
	lsl	w0, w1, w0
	ldr	w1, [x29,124]
	sub	w0, w0, #1
	lsr	w1, w1, w4
	and	w0, w0, w1
	ldrb	w1, [x2,1314]
	udiv	w0, w0, w1
	ldr	w1, [x29,104]
	cmp	w0, w1
	bne	.L3503
	add	w21, w21, 1
	uxth	w21, w21
.L3503:
	add	x28, x28, 1
	b	.L3502
.L3524:
	mov	x0, x23
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3506
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w26
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,1112]
	adrp	x0, .LC255
	add	x0, x0, :lo12:.LC255
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L3506:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,1112]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L3486
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L3486:
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
	adrp	x0, .LC256
	ldrb	w3, [x1,2]
	add	x0, x0, :lo12:.LC256
	mov	w1, w22
	and	w4, w4, 2047
	ubfx	x2, x3, 5, 3
	ubfx	x5, x5, 11, 8
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w1, 65535
	mov	w0, 0
	cmp	w22, w1
	beq	.L3526
	ldrh	w1, [x19,1088]
	cmp	w1, w22
	bls	.L3526
	ldr	x0, [x19,1096]
	mov	w25, 1
	adrp	x27, .LANCHOR3
	add	x0, x0, x24
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L3527
	add	x0, x27, :lo12:.LANCHOR3
	ldrb	w25, [x0,1328]
.L3527:
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
	ldrh	w0, [x0,1384]
	mov	w20, w23
	strb	wzr, [x29,197]
	strh	wzr, [x29,202]
	mul	w1, w1, w0
	add	x0, x28, :lo12:.LANCHOR0
	strh	w1, [x29,198]
	ldr	x1, [x0,1096]
	adrp	x0, .LC257
	add	x0, x0, :lo12:.LC257
	add	x1, x1, x24
	ldrb	w2, [x1,2]
	mov	w1, w22
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x26, x0
	adrp	x0, .LC195
	str	w23, [x29,168]
	add	x0, x0, :lo12:.LC195
	str	x0, [x29,120]
	adrp	x0, .LANCHOR4
	str	w23, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
	str	x0, [x29,112]
.L3528:
	add	x0, x27, :lo12:.LANCHOR3
	ldrh	w0, [x0,1384]
	cmp	w0, w20
	bls	.L3546
	lsl	w0, w20, 1
	str	wzr, [x29,172]
	add	w1, w0, w20
	sub	w0, w0, #1
	sub	w1, w1, #1
	str	w0, [x29,136]
	str	w1, [x29,140]
.L3547:
	ldrb	w0, [x29,201]
	ldr	w1, [x29,172]
	cmp	w0, w1
	bls	.L3544
	mov	w21, 1
.L3545:
	cmp	w21, w25
	ldr	w0, [x29,172]
	bhi	.L3566
	add	x1, x29, 192
	add	x0, x1, w0, sxtw 1
	ldrh	w3, [x0,16]
	mov	w0, 65535
	cmp	w3, w0
	beq	.L3529
	add	x0, x27, :lo12:.LANCHOR3
	cmp	w25, 3
	ldrh	w2, [x0,1418]
	mul	w2, w3, w2
	bne	.L3530
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w4, [x1,1268]
	cbz	w4, .L3531
	ldr	w1, [x29,140]
	b	.L3564
.L3531:
	ldrb	w0, [x1,1269]
	lsl	w19, w21, 24
	cbz	w0, .L3533
	ldr	w0, [x29,140]
	add	w2, w2, w0
	add	w2, w2, w21
	b	.L3565
.L3533:
	add	w2, w2, w20
.L3565:
	orr	w19, w19, w2
	b	.L3532
.L3530:
	cmp	w25, 2
	add	w19, w2, w20
	bne	.L3532
	ldr	w1, [x29,136]
.L3564:
	ldrb	w19, [x0,1328]
	add	w2, w2, w1
	add	w2, w2, w21
	orr	w19, w2, w19, lsl 24
.L3532:
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
	beq	.L3551
	cmn	w9, #1
	bne	.L3535
.L3551:
	mov	w0, 1
	str	w0, [x29,168]
.L3535:
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	add	x0, x28, :lo12:.LANCHOR0
	ldr	x0, [x0,1096]
	add	x0, x0, x24
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L3529
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L3529
	ldr	x0, [x26,24]
	str	x2, [x29,104]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,188]
	cmn	w0, #1
	ldr	x2, [x29,104]
	bne	.L3537
	ldr	x0, [x26,24]
	add	x1, x29, 188
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L3537:
	ldr	w0, [x29,188]
	cmp	w0, w19
	bne	.L3538
	ldr	x2, [x26,24]
	add	w23, w23, 1
	adrp	x0, .LC258
	mov	w1, w19
	add	x0, x0, :lo12:.LC258
	mov	w3, w23
	ldr	w2, [x2,4]
	bl	printk
.L3538:
	ldr	x0, [x29,160]
	cbz	x0, .L3540
	ldr	x0, [x29,144]
	ubfiz	x19, x0, 2, 32
	ldr	x0, [x29,160]
	ldr	w2, [x0,x19]
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L3541
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3541
	adrp	x0, .LC259
	ldr	w1, [x29,144]
	add	x0, x0, :lo12:.LC259
	bl	printk
.L3541:
	ldr	x0, [x29,160]
	ldr	x1, [x26,24]
	ldr	w0, [x0,x19]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3540
	cmn	w0, #1
	beq	.L3540
	adrp	x0, .LC0
	ldr	x1, [x29,112]
	mov	w2, 1575
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3540:
	ldr	w0, [x29,144]
	add	w0, w0, 1
	str	w0, [x29,144]
.L3529:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L3545
.L3566:
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,172]
	b	.L3547
.L3544:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L3528
.L3546:
	mov	x0, x26
	bl	zbuf_free
	add	x0, x28, :lo12:.LANCHOR0
	ldr	x3, [x29,128]
	mov	w1, w22
	ldr	x2, [x0,1112]
	adrp	x0, .LC260
	add	x0, x0, :lo12:.LC260
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w23
	bl	printk
	ldr	w0, [x29,168]
.L3526:
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
	tbz	x0, 12, .L3568
	adrp	x0, .LC261
	mov	w1, w19
	add	x0, x0, :lo12:.LC261
	mov	w2, w20
	mov	w3, w23
	bl	printk
.L3568:
	cbnz	w19, .L3569
	adrp	x0, .LANCHOR0+1032
	mov	w19, 24576
	ldr	w1, [x0,#:lo12:.LANCHOR0+1032]
	b	.L3570
.L3569:
	cmp	w19, 3
	mov	w3, -1
	bhi	.L3571
	lsl	w19, w19, 13
	mov	w1, 8192
.L3570:
	cmp	w20, w1
	bcs	.L3603
	cmp	w23, w1
	cset	w0, hi
	cbnz	w0, .L3603
	add	w2, w20, w23
	mov	w3, -1
	cmp	w2, w1
	bhi	.L3571
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
	add	x0, x0, 944
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
.L3572:
	cbz	w25, .L3645
	ldr	w1, [x29,156]
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w19, w1
	ldr	w1, [x29,144]
	ldrb	w0, [x0,3360]
	cset	w2, eq
	cmp	w19, w1
	cset	w26, eq
	uxth	w24, w0
	cbnz	w26, .L3607
	cbz	w2, .L3573
	b	.L3644
.L3607:
	cbz	w2, .L3575
.L3644:
	udiv	w26, w20, w0
	msub	w26, w26, w0, w20
	uxth	w0, w23
	uxth	w26, w26
	sub	w24, w24, w26
	uxth	w24, w24
	cmp	w24, w23
	csel	w24, w0, w24, hi
	b	.L3573
.L3575:
	ldr	w1, [x29,152]
	mov	w26, w2
	msub	w24, w0, w19, w1
	uxtb	w24, w24
.L3573:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 1312
.L3578:
	ldr	w2, [x1,36]
	cmp	w2, w19
	bne	.L3576
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L3576
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
	b	.L3577
.L3576:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3578
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L3579
	mov	w0, w19
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L3579:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L3580
	mov	w24, 0
.L3581:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w24, w0
	bcs	.L3577
	madd	w0, w19, w0, w24
	cmp	w0, w20
	bcc	.L3582
	ldr	w1, [x29,152]
	cmp	w0, w1
	bcs	.L3582
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	add	x22, x22, 512
	bl	ftl_memset
.L3582:
	add	w24, w24, 1
	b	.L3581
.L3580:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3584
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
.L3577:
	add	w19, w19, 1
	sub	w25, w25, #1
.L3584:
	cbz	w25, .L3608
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3362]
	cmp	w0, 2
	bhi	.L3572
.L3608:
	adrp	x24, .LANCHOR3
	add	x2, x24, :lo12:.LANCHOR3
	ldrb	w1, [x2,1963]
	cbz	w1, .L3572
	ldrb	w2, [x2,1962]
	add	x26, x21, :lo12:.LANCHOR0
	add	x0, x26, 1312
	mov	x27, x26
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
.L3587:
	add	x0, x24, :lo12:.LANCHOR3
	ldrb	w1, [x0,1963]
	cbz	w1, .L3646
	ldrb	w0, [x0,1962]
	cmp	w0, 255
	bne	.L3588
	ldr	x1, [x29,128]
	mov	w2, 1283
	ldr	x0, [x29,136]
	bl	printk
	bl	dump_stack
.L3588:
	add	x0, x24, :lo12:.LANCHOR3
	add	x1, x27, 1312
	ldrb	w28, [x0,1962]
	lsl	x2, x28, 6
	add	x26, x1, x2
	ldrb	w1, [x1,x2]
	ldr	w6, [x26,52]
	strb	w1, [x0,1962]
	cmn	w6, #1
	bne	.L3589
	ldr	x1, [x27,1120]
	str	w6, [x29,148]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L3590
.L3589:
	cmp	w6, 256
	bne	.L3590
	ldrb	w4, [x27,1257]
	mov	w1, 24
	ldrh	w5, [x0,1312]
	mov	w7, 1
	sub	w4, w1, w4
	ldr	w3, [x26,40]
	sub	w4, w4, w5
	ldr	w2, [x26,36]
	lsl	w4, w7, w4
	lsr	w5, w3, w5
	sub	w4, w4, #1
	str	x6, [x29,104]
	and	w5, w4, w5
	ldrb	w4, [x0,1314]
	adrp	x0, .LC262
	str	x7, [x29,112]
	add	x0, x0, :lo12:.LC262
	udiv	w4, w5, w4
	str	x4, [x29,120]
	uxth	w1, w4
	bl	printk
	ldr	x4, [x29,120]
	mov	w2, 0
	ldr	x7, [x29,112]
	mov	w0, w4
	mov	w1, w7
	bl	gc_add_sblk
	ldr	x6, [x29,104]
.L3590:
	add	x0, x27, 1312
	add	x0, x0, x28, lsl 6
	ldr	x3, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x3,4]
	cmp	w1, w0
	bne	.L3609
	cmn	w6, #1
	bne	.L3591
.L3609:
	add	x0, x27, 1312
	ldrb	w4, [x27,3360]
	add	x0, x0, x28, lsl 6
	ldrb	w6, [x27,1257]
	mov	w5, 24
	sub	w5, w5, w6
	ldrb	w1, [x0,56]
	ldr	x2, [x0,8]
	ldr	x7, [x0,16]
	cmp	w1, w4
	ldr	w1, [x0,40]
	mov	w0, 1
	lsl	w0, w0, w6
	csel	x2, x7, x2, cs
	sub	w0, w0, #1
	lsr	w6, w1, w5
	lsl	w5, w0, w5
	and	w0, w0, w6
	bic	w1, w1, w5
	bl	flash_read_page_en
	mov	w6, w0
.L3591:
	cmn	w6, #1
	cset	w11, eq
	cbnz	w11, .L3610
	add	x0, x27, 1312
	add	x0, x0, x28, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3631
.L3610:
	ldr	x1, [x27,1120]
	add	x10, x27, 1312
	add	x10, x10, x28, lsl 6
	mov	w2, w6
	add	x10, x10, 32
	str	x11, [x29,112]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	ldr	x7, [x10,-8]
	ldrb	w1, [x10,-31]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC263
	add	x0, x0, :lo12:.LC263
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
	ldrh	w0, [x3,1312]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x10,8]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,1314]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x11, [x29,112]
	cbnz	w11, .L3611
.L3631:
	add	x0, x27, 1312
	add	x0, x0, x28, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3596
.L3611:
	ldr	x1, [x29,128]
	mov	w2, 1319
	ldr	x0, [x29,136]
	bl	printk
	bl	dump_stack
.L3596:
	add	x0, x27, 1312
	ldrb	w3, [x27,3360]
	add	x0, x0, x28, lsl 6
	ldrb	w2, [x0,56]
	cmp	w3, w2
	bls	.L3598
	ldrb	w3, [x0,57]
	lsl	w2, w2, 9
	ldr	x1, [x0,8]
	ldr	x0, [x0,16]
	add	x1, x1, x3, lsl 9
	bl	ftl_memcpy
	b	.L3599
.L3598:
	ldrb	w1, [x0,2]
	and	w1, w1, -9
	strb	w1, [x0,2]
.L3599:
	mov	x1, x26
	add	x0, x27, 3364
	bl	buf_remove_buf
	mov	x0, x26
	bl	zbuf_free
	add	x1, x24, :lo12:.LANCHOR3
	ldrb	w0, [x1,1963]
	sub	w0, w0, #1
	strb	w0, [x1,1963]
	b	.L3587
.L3646:
	mov	w2, -1
	strb	w1, [x0,1963]
	strb	w2, [x0,1962]
	b	.L3572
.L3645:
	bl	timer_get_time
	adrp	x1, .LANCHOR5+1004
	ldr	w3, [x29,148]
	str	w0, [x1,#:lo12:.LANCHOR5+1004]
	b	.L3571
.L3603:
	mov	w3, -1
.L3571:
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
	bhi	.L3650
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x22, x0
	mov	x0, -14
	cbnz	x22, .L3650
	adrp	x0, .LC264
	mov	x1, x19
	add	x0, x0, :lo12:.LC264
	strb	w22, [x19,x21]
	bl	printk
	adrp	x0, .LC265
	mov	x1, x19
	add	x0, x0, :lo12:.LC265
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC266
	mov	x0, x19
	add	x1, x1, :lo12:.LC266
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3651
	adrp	x23, .LANCHOR0
	adrp	x0, .LC267
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC267
	mov	w2, 4
	adrp	x20, .LANCHOR5
	ldr	x3, [x19,3392]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x1, [x19,3392]
	adrp	x0, .LC268
	mov	w2, 2
	add	x0, x0, :lo12:.LC268
	add	x1, x1, 416
	ldrh	w3, [x3,264]
	mov	w19, w22
	mov	w22, 65535
	bl	rknand_print_hex
.L3652:
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w0, [x0,264]
	cmp	w19, w0
	bge	.L3655
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	add	x0, x0, w19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3653
	mov	x1, 0
	bl	ftl_sblk_dump
.L3653:
	add	w19, w19, 1
	b	.L3652
.L3651:
	adrp	x1, .LC269
	mov	x0, x19
	add	x1, x1, :lo12:.LC269
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3656
	adrp	x22, .LANCHOR0
	adrp	x0, .LC267
	add	x20, x22, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC267
	mov	w2, 4
	adrp	x26, .LC272
	add	x26, x26, :lo12:.LC272
	ldr	x3, [x20,3392]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	adrp	x3, .LANCHOR5+264
	ldr	x1, [x20,3392]
	adrp	x0, .LC268
	mov	w2, 2
	add	x1, x1, 416
	add	x0, x0, :lo12:.LC268
	ldrh	w3, [x3,#:lo12:.LANCHOR5+264]
	add	x20, x29, 192
	bl	rknand_print_hex
	mov	w0, 50
	bl	msleep
	adrp	x0, .LC270
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC270
	str	x1, [x20,-88]!
	mov	x19, x22
	adrp	x22, .LC271
	bl	printk
	add	x22, x22, :lo12:.LC271
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.34
	mov	w24, w0
	adrp	x2, .LANCHOR3
	str	w0, [x29,100]
	add	x2, x2, :lo12:.LANCHOR3
	uxth	w25, w0
	add	x20, x2, 1968
	add	x23, x2, 2480
.L3658:
	ldrh	w1, [x20]
	mov	x0, x22
	ldrh	w2, [x20,2]
	bl	printk
	ldrh	w0, [x20]
	cmp	w0, w25
	bne	.L3657
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x20,8]
	mov	w2, 4
	ldrb	w3, [x0,3360]
	mov	x0, x26
	lsl	w3, w3, 7
	bl	rknand_print_hex
	mov	w0, 50
	bl	msleep
.L3657:
	add	x20, x20, 16
	cmp	x20, x23
	bne	.L3658
	mov	w0, 300
	add	x19, x19, :lo12:.LANCHOR0
	bl	msleep
	mov	w0, 1
	bl	buf_alloc
	mov	x20, x0
	ldr	x1, [x19,3392]
	add	x1, x1, w24, uxth 2
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
	adrp	x0, .LC245
	add	x0, x0, :lo12:.LC245
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x20,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x19,3360]
	adrp	x0, .LC273
	ldr	x1, [x20,8]
	add	x0, x0, :lo12:.LC273
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	mov	x0, x20
	b	.L3673
.L3656:
	adrp	x1, .LC274
	mov	x0, x19
	add	x1, x1, :lo12:.LC274
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3659
	bl	dump_ftl_info
	b	.L3655
.L3659:
	adrp	x1, .LC275
	mov	x0, x19
	add	x1, x1, :lo12:.LC275
	mov	x2, 9
	bl	memcmp
	cbnz	w0, .L3660
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1,-88]!
	bl	rk_simple_strtoull.constprop.34
	str	w0, [x29,100]
	adrp	x1, .LANCHOR3+1416
	strh	w0, [x1,#:lo12:.LANCHOR3+1416]
	bl	dump_all_list_info
	b	.L3655
.L3660:
	adrp	x1, .LC276
	mov	x0, x19
	add	x1, x1, :lo12:.LC276
	mov	x2, 8
	bl	memcmp
	cbz	w0, .L3655
	adrp	x1, .LC277
	mov	x0, x19
	add	x1, x1, :lo12:.LC277
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3662
	add	x20, x29, 192
	adrp	x0, .LC270
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC270
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	bl	rk_simple_strtoull.constprop.34
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
	adrp	x0, .LC245
	add	x0, x0, :lo12:.LC245
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w3, [x6]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	ldr	w2, [x19,52]
	bl	printk
	ldrb	w3, [x20,3360]
	adrp	x0, .LC218
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC218
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,3360]
	adrp	x0, .LC240
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC240
	mov	w2, 4
	lsl	w3, w3, 1
	bl	rknand_print_hex
	mov	x0, x19
.L3673:
	bl	zbuf_free
	b	.L3655
.L3662:
	adrp	x1, .LC278
	mov	x0, x19
	add	x1, x1, :lo12:.LC278
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3663
	add	x20, x29, 192
	adrp	x0, .LC270
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC270
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.34
	str	w0, [x29,100]
	mov	x1, x22
	bl	ftl_sblk_dump
	b	.L3655
.L3663:
	adrp	x1, .LC279
	mov	x0, x19
	add	x1, x1, :lo12:.LC279
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3664
	add	x20, x29, 192
	adrp	x0, .LC270
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC270
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.34
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3655
.L3664:
	adrp	x1, .LC280
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC280
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3665
	add	x1, x19, 8
	adrp	x0, .LC270
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC270
	str	x1, [x19,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.34
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29,100]
	cmn	w0, #1
	ldr	x2, [x29,88]
	bne	.L3666
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3666:
	adrp	x0, .LC281
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC281
	bl	printk
	b	.L3655
.L3665:
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
	adrp	x0, .LC288
	add	x0, x0, :lo12:.LC288
	bl	printk
	adrp	x0, .LC289
	add	x0, x0, :lo12:.LC289
	bl	printk
	adrp	x0, .LC290
	add	x0, x0, :lo12:.LC290
	bl	printk
.L3655:
	bl	rknand_device_unlock
	mov	x0, x21
.L3650:
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
	ldrh	w23, [x1,1384]
	ldrh	w19, [x22,12]
	ldr	x1, [x1,1936]
	mul	w23, w23, w2
	add	x19, x1, x19, lsl 2
	add	x1, x19, w23, sxtw 2
	ldr	w1, [x1,-4]
	cmn	w1, #1
	beq	.L3675
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1998
	add	x1, x1, 960
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3675:
	adrp	x0, .LC292
	mov	x28, 0
	add	x0, x0, :lo12:.LC292
	adrp	x27, .LC291
	str	x0, [x29,112]
	adrp	x0, .LC293
	add	x0, x0, :lo12:.LC293
	mov	w21, w28
	str	x0, [x29,104]
	add	x27, x27, :lo12:.LC291
.L3676:
	cmp	w28, w23
	mov	w24, w28
	bge	.L3703
	ldr	w2, [x19,x28,lsl 2]
	cmn	w2, #1
	beq	.L3677
	adrp	x25, .LANCHOR0
	add	x0, x25, :lo12:.LANCHOR0
	ldrb	w20, [x0,3360]
	adrp	x0, .LANCHOR2
	lsl	w20, w20, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w20, w2, w20
	and	w20, w20, 65535
	tbz	x0, 12, .L3681
	mov	x0, x27
	mov	w1, w20
	mov	w3, w28
	bl	printk
.L3681:
	sbfiz	x26, x24, 2, 32
	ldr	w0, [x19,x26]
	cmn	w0, #1
	beq	.L3679
	add	x1, x25, :lo12:.LANCHOR0
	ldrb	w1, [x1,3360]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w20, w0, uxth
	bne	.L3679
	ldrb	w0, [x22,9]
	ldr	x2, [x29,120]
	add	x3, x2, :lo12:.LANCHOR3
	str	x3, [x29,96]
	sdiv	w1, w24, w0
	ldrh	w2, [x3,1418]
	msub	w0, w1, w0, w24
	add	x0, x22, w0, sxtw 1
	ldrh	w0, [x0,16]
	madd	w2, w0, w2, w1
	str	w2, [x29,140]
	ldr	x0, [x29,112]
	ldr	w1, [x19,x26]
	bl	pm_ppa_update_check
	ldr	x3, [x29,96]
	cbz	w0, .L3680
	ldr	x1, [x3,1936]
	mov	w2, 4
	ldr	x0, [x29,104]
	mov	w3, w23
	bl	rknand_print_hex
.L3680:
	ldr	w0, [x19,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w21, w21, 1
	bl	pm_log2phys
	uxth	w21, w21
	mov	w0, -1
	str	w0, [x19,x26]
.L3679:
	add	w24, w24, 1
	cmp	w24, w23
	bne	.L3681
.L3677:
	add	x28, x28, 1
	b	.L3676
.L3703:
	adrp	x0, .LANCHOR2
	adrp	x19, .LANCHOR0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3683
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22]
	mov	w2, w21
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,1112]
	adrp	x0, .LC294
	add	x0, x0, :lo12:.LC294
	ldrh	w3, [x4,x3]
	bl	printk
.L3683:
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
	beq	.L3705
	bl	zftl_insert_data_list
.L3705:
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
	bne	.L3707
	mov	w20, 65535
	cmp	w21, w20
	beq	.L3707
	adrp	x0, .LC295
	mov	w1, w21
	add	x0, x0, :lo12:.LC295
	bl	printk
	ldr	x0, [x19,1120]
	ldr	w0, [x0,564]
	bl	gc_mark_bad_ppa
	ldr	x0, [x19,1120]
	mov	w1, -1
	str	w20, [x0,560]
	str	w1, [x0,564]
.L3707:
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
	add	x0, x0, 352
	str	x0, [x29,120]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
.L3716:
	ldrb	w0, [x26,3388]
	adrp	x20, .LANCHOR0
	cbz	w0, .L3718
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
	bcc	.L3719
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 606
	add	x1, x1, 984
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3719:
	ldr	x0, [x29,144]
	add	x1, x20, :lo12:.LANCHOR0
	add	x3, x1, 1312
	add	x0, x3, x0, lsl 6
	ldr	w2, [x1,3372]
	ldr	w23, [x0,36]
	cmp	w23, w2
	bcc	.L3720
	mov	x0, x28
	bl	zbuf_free
	mov	w0, -1
	b	.L3779
.L3720:
	ldr	x25, [x0,8]
	ldr	x24, [x0,24]
	ldrb	w22, [x0,57]
	ldrb	w21, [x0,56]
	ldrb	w0, [x1,3389]
	cbz	w0, .L3722
	ldr	x0, [x29,136]
	ldrb	w19, [x0,352]
	add	x19, x3, x19, lsl 6
.L3723:
	ldrb	w4, [x19]
	cmp	w4, 255
	beq	.L3781
	sbfiz	x19, x4, 6, 32
	add	x19, x3, x19
	b	.L3723
.L3781:
	ldr	w0, [x19,36]
	cmp	w0, w23
	bne	.L3722
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
	b	.L3716
.L3722:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L3726
	mov	w0, w23
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L3726:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	w0, [x29,172]
	add	x3, x4, 1312
	mov	w1, 0
	mov	x27, 0
	ldr	x19, [x4,1120]
	add	x4, x4, 3360
	add	x19, x19, 16
.L3728:
	ldr	w2, [x3,36]
	cmp	w2, w23
	bne	.L3727
	ldrb	w6, [x3,2]
	tbz	x6, 3, .L3727
	ldr	x27, [x3,8]
	and	w6, w6, -9
	ldr	w0, [x3,40]
	mov	w1, 1
	strb	w6, [x3,2]
.L3727:
	add	x3, x3, 64
	cmp	x3, x4
	bne	.L3728
	cbz	w1, .L3729
	str	w0, [x29,172]
.L3729:
	add	x3, x20, :lo12:.LANCHOR0
	str	wzr, [x29,152]
	ldrb	w0, [x3,3360]
	cmp	w21, w0
	bcs	.L3730
	add	w21, w22, w21
	cbz	x27, .L3731
	cbz	w22, .L3732
	mov	x0, x25
	mov	x1, x27
	lsl	w2, w22, 9
	str	x3, [x29,152]
	bl	ftl_memcpy
	ldr	x3, [x29,152]
	ldr	x19, [x3,1120]
	add	x19, x19, 48
.L3732:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w2, [x0,3360]
	cmp	w21, w2
	bcc	.L3733
	ldr	x19, [x0,1120]
	add	x19, x19, 16
	b	.L3780
.L3733:
	ubfiz	x1, x21, 9, 9
	sub	w2, w2, w21
	add	x0, x25, x1
	lsl	w2, w2, 9
	add	x1, x27, x1
	bl	ftl_memcpy
.L3780:
	str	wzr, [x29,152]
	b	.L3730
.L3731:
	ldr	w0, [x29,172]
	cmn	w0, #1
	beq	.L3734
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	ldr	w1, [x29,172]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x3, [x27,24]
	ldr	w0, [x3,12]
	add	w0, w0, 1
	str	w0, [x29,152]
	ldr	w0, [x3,4]
	cmp	w0, w23
	bne	.L3735
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3736
.L3735:
	add	x4, x20, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	w1, [x27,40]
	mov	w2, 24
	ldrb	w6, [x4,1257]
	ldrb	w4, [x4,3360]
	lsl	w0, w0, w6
	sub	w2, w2, w6
	sub	w0, w0, #1
	lsr	w6, w1, w2
	lsl	w2, w0, w2
	and	w0, w0, w6
	bic	w1, w1, w2
	ldr	x2, [x27,8]
	bl	flash_read_page_en
	str	w0, [x27,52]
.L3736:
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3737
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3738
.L3737:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,172]
	mov	w3, w23
	ldr	x1, [x0,1120]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	adrp	x0, .LC296
	ldrb	w1, [x27,1]
	add	x0, x0, :lo12:.LC296
	ldr	w4, [x27,52]
	bl	printk
	adrp	x0, .LC240
	mov	w2, 4
	ldr	x1, [x27,24]
	add	x0, x0, :lo12:.LC240
	mov	w3, w2
	bl	rknand_print_hex
.L3738:
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3739
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3740
.L3739:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 698
	add	x1, x1, 984
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3740:
	cbz	w22, .L3741
	ldr	w0, [x29,172]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3742
	ldr	x1, [x27,8]
	mov	x0, x25
	bl	ftl_memcpy
	b	.L3743
.L3734:
	cbz	w22, .L3741
	lsl	w2, w22, 9
.L3742:
	mov	x0, x25
	mov	w1, 0
	bl	ftl_memset
.L3743:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x19, [x0,1120]
	add	x19, x19, 48
.L3741:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w2, [x0,3360]
	cmp	w21, w2
	bcc	.L3744
	bls	.L3745
	ldr	x19, [x0,1120]
	add	x19, x19, 16
	b	.L3745
.L3744:
	ldr	w0, [x29,172]
	sub	w2, w2, w21
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x21, 7, 9
	beq	.L3746
	ldr	x1, [x27,8]
	lsl	x21, x0, 2
	add	x0, x25, x21
	add	x1, x1, x21
	bl	ftl_memcpy
	b	.L3745
.L3746:
	add	x0, x25, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3745:
	cbz	x27, .L3730
	ldrb	w0, [x27,2]
	mov	x1, x27
	and	w0, w0, -9
	strb	w0, [x27,2]
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 3364
	bl	buf_remove_buf
	mov	x0, x27
	bl	zbuf_free
.L3730:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3747
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3747:
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
	str	w0, [x1,356]
	cmp	w2, 2
	adrp	x0, .LANCHOR5
	bhi	.L3748
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L3718
.L3748:
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
	ldrb	w5, [x1,352]
	mov	w1, 0
	mov	w3, w5
.L3752:
	cmp	w1, w4
	beq	.L3782
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L3752
.L3782:
	add	x0, x0, :lo12:.LANCHOR5
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,352]
	add	x0, x20, :lo12:.LANCHOR0
	add	x0, x0, 1312
	strb	w2, [x0,2077]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3718
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3718:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w19, [x20,3388]
	cbnz	w19, .L3716
	bl	ftl_write_completed
	mov	w0, w19
.L3779:
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
	ldrb	w0, [x1,1345]
	ldr	x26, [x22,1120]
	cbnz	w0, .L3784
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3783
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
	beq	.L3869
	cmn	w0, #1
	bne	.L3786
.L3869:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1032
	add	x1, x1, 1008
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3786:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,192]
	cmn	w0, #1
	bne	.L3788
	mov	w0, w21
	add	x1, x29, 192
	mov	w2, 0
	bl	pm_log2phys
.L3788:
	ldr	w2, [x29,192]
	cmp	w2, w23
	bne	.L3789
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1312
	add	x1, x1, 3360
.L3792:
	ldr	w3, [x0,36]
	cmp	w3, w21
	bne	.L3790
	ldrb	w3, [x0,2]
	tbz	x3, 1, .L3790
	mov	x0, x20
	str	x2, [x29,184]
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	x2, [x29,184]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3783
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC297
	mov	w1, w21
	add	x0, x0, :lo12:.LC297
	ldrh	w3, [x19,3450]
	bl	printk
	b	.L3783
.L3790:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3792
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
	tbz	x0, 8, .L3793
	mov	w0, w21
	str	x2, [x29,184]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,184]
	adrp	x0, .LC298
	ldrh	w5, [x22,3450]
	add	x0, x0, :lo12:.LC298
	mov	w1, w21
	mov	w4, w2
	bl	printk
.L3793:
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
	b	.L3783
.L3789:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3795
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	str	x2, [x29,184]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,184]
	adrp	x0, .LC298
	ldrh	w5, [x19,3450]
	add	x0, x0, :lo12:.LC298
	mov	w1, w21
	mov	w4, w23
	bl	printk
.L3795:
	mov	x0, x20
	bl	zbuf_free
	b	.L3783
.L3784:
	add	x0, x26, 80
	str	x0, [x29,184]
	ldrb	w1, [x1,1329]
	mov	w24, 1
	ldrb	w0, [x0,9]
	uxth	w20, w0
	cmp	w1, w20
	bhi	.L3797
	cmp	w20, 2
	mov	w24, 2
	csel	w24, w20, w24, ls
.L3797:
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w3, [x2,1328]
	cmp	w3, 3
	bne	.L3798
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w4, [x1,1268]
	cbz	w4, .L3799
	ldrh	w22, [x1,5530]
	ldrb	w1, [x2,1956]
	sdiv	w0, w22, w0
	uxth	w26, w0
	cbz	w1, .L3800
	adrp	x1, .LANCHOR2
	mov	w23, 2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w0, [x1,w0,sxtw 1]
	cmp	w0, wzr
	csinc	w23, w23, wzr, ne
	b	.L3801
.L3800:
	adrp	x0, .LANCHOR5+997
	mov	w23, 1
	ldrb	w0, [x0,#:lo12:.LANCHOR5+997]
	cbz	w0, .L3801
	sub	w0, w26, #62
	mov	w23, 2
	uxth	w0, w0
	cmp	w0, 2159
	bhi	.L3801
	udiv	w0, w26, w3
	add	w0, w0, w0, lsl 1
	sub	w0, w26, w0
	uxth	w0, w0
	cmp	w0, wzr
	csinc	w23, w23, wzr, eq
.L3801:
	msub	w20, w26, w20, w22
	uxth	w20, w20
	b	.L3804
.L3799:
	ldrb	w2, [x2,1344]
	cbz	w2, .L3803
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
	cbz	w0, .L3804
	add	w4, w26, w26, lsl 1
	uxth	w26, w4
	b	.L3804
.L3803:
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
	bne	.L3805
	b	.L3804
.L3798:
	ldrb	w1, [x2,1330]
	cbnz	w1, .L3806
	add	x1, x19, :lo12:.LANCHOR0
	mov	w23, 1
	ldrh	w22, [x1,5530]
	sdiv	w26, w22, w0
	msub	w20, w26, w20, w22
	uxth	w20, w20
	b	.L3804
.L3806:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w23, 2
	ldrh	w22, [x1,5530]
	sdiv	w26, w22, w0
	msub	w20, w26, w20, w22
	uxth	w20, w20
.L3804:
	adrp	x3, .LANCHOR5
	adrp	x2, .LANCHOR2
	mul	w7, w23, w24
	mov	w28, 0
	mov	x8, x3
	add	x2, x2, :lo12:.LANCHOR2
	sxtw	x5, w26
	add	x6, x19, :lo12:.LANCHOR0
	b	.L3807
.L3938:
	add	x0, x2, 2136
	ldrh	w0, [x0,x5,lsl 1]
	cmp	w0, w26
	bcs	.L3867
	tbz	x28, 0, .L3867
.L3808:
	add	x0, x8, :lo12:.LANCHOR5
	sbfiz	x25, x25, 2, 32
	ldr	x1, [x0,240]
	ldr	w0, [x1,x25]
	cmn	w0, #1
	beq	.L3937
.L3811:
	add	w28, w28, 1
	uxth	w28, w28
.L3807:
	cmp	w28, w7
	bge	.L3867
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x4, [x29,184]
	add	w25, w22, w28
	ldrb	w9, [x4,9]
	ldrh	w0, [x1,1346]
	mul	w0, w0, w9
	add	x9, x3, :lo12:.LANCHOR5
	ldrb	w9, [x9,1001]
	sub	w0, w0, w9
	cmp	w25, w0
	blt	.L3808
	ldrb	w0, [x1,1956]
	cbnz	w0, .L3938
.L3867:
	mov	w25, 1
.L3805:
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
	add	x0, x0, 1008
	str	x0, [x29,104]
	b	.L3810
.L3937:
	ldrh	w27, [x6,3450]
	ldrh	w0, [x6,3448]
	cmp	w0, w27
	bls	.L3783
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w0, [x0,1956]
	cbz	w0, .L3812
	add	x0, x2, 2136
	ldrh	w0, [x0,x5,lsl 1]
	cmp	w0, w26
	bcs	.L3812
	tbz	x28, 0, .L3812
	ubfiz	x0, x0, 2, 16
	ldr	w0, [x1,x0]
	str	w0, [x1,x25]
	b	.L3811
.L3812:
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
	b	.L3811
.L3838:
	ldr	w0, [x29,160]
	add	w2, w22, w0
	sxtw	x0, w2
	str	x0, [x29,176]
	add	x0, x21, :lo12:.LANCHOR3
	ldr	x1, [x29,176]
	ldr	x0, [x0,1336]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	beq	.L3939
.L3814:
	add	x5, x21, :lo12:.LANCHOR3
	ldr	x2, [x29,176]
	add	x3, x29, 192
	add	x6, x19, :lo12:.LANCHOR0
	add	x1, x6, 1312
	ldr	x0, [x5,1336]
	ldrb	w2, [x0,x2]
	ldr	w0, [x29,160]
	add	x1, x1, x2, lsl 6
	str	x1, [x3,w0,sxtw 3]
	mov	w3, 2
	ldrb	w0, [x5,1328]
	strh	w3, [x1,50]
	adrp	x3, .LANCHOR5
	strb	w25, [x1,61]
	cmp	w0, 3
	bne	.L3829
	ldrb	w7, [x6,1268]
	cbnz	w7, .L3940
	ldrb	w6, [x6,1269]
	add	x1, x1, 32
	udiv	w0, w27, w0
	ldrh	w7, [x5,1418]
	cbz	w6, .L3833
	add	w6, w0, w20
	ldr	x4, [x29,184]
	add	x6, x4, w6, sxtw 1
	ldrh	w5, [x6,16]
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	madd	w5, w5, w7, w26
	add	w0, w5, w0, uxth
.L3936:
	str	w0, [x1,8]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x2, x0, x2, lsl 6
	add	x0, x3, :lo12:.LANCHOR5
	ldr	x1, [x0,248]
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
.L3857:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1956]
	cbnz	w0, .L3941
.L3837:
	ldr	w0, [x29,152]
	add	w0, w0, 1
	str	w0, [x29,152]
.L3810:
	ldr	w0, [x29,152]
	ldr	w1, [x29,168]
	and	w0, w0, 65535
	str	w0, [x29,160]
	cmp	w0, w1
	ldrh	w27, [x29,152]
	blt	.L3838
	b	.L3942
.L3939:
	mov	w0, 0
	str	x2, [x29,144]
	bl	buf_alloc
	mov	x28, x0
	ldr	x2, [x29,144]
	cbnz	x0, .L3815
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w28
	bl	buf_alloc
	mov	x28, x0
	ldr	x2, [x29,144]
	cbz	x0, .L3783
.L3815:
	add	x1, x21, :lo12:.LANCHOR3
	ldr	x3, [x29,176]
	ldrb	w5, [x28,1]
	ldr	x0, [x1,1336]
	strb	w5, [x0,x3]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,184]
	strb	w25, [x28,61]
	ldrb	w5, [x0,3431]
	add	w5, w5, 1
	strb	w5, [x0,3431]
	add	w0, w27, w22
	strh	w0, [x28,48]
	ldrh	w0, [x1,1346]
	ldrb	w5, [x3,9]
	mul	w0, w0, w5
	adrp	x5, .LANCHOR5
	str	x5, [x29,144]
	add	x6, x5, :lo12:.LANCHOR5
	ldrb	w6, [x6,1001]
	sub	w0, w0, w6
	cmp	w2, w0
	blt	.L3816
	ldrb	w0, [x1,1956]
	cbz	w0, .L3817
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0,w26,sxtw 1]
	cmp	w0, w26
	bcs	.L3817
	tbnz	x27, 0, .L3816
.L3817:
	ldr	x0, [x29,144]
	mov	w1, -1
	ldr	x3, [x29,176]
	add	x5, x21, :lo12:.LANCHOR3
	add	x6, x0, :lo12:.LANCHOR5
	ldr	x0, [x6,232]
	str	w1, [x0,x3,lsl 2]
	ldrb	w0, [x6,1001]
	cmp	w0, 2
	ldrh	w0, [x5,1346]
	bne	.L3818
	ldr	x1, [x29,184]
	ldrb	w1, [x1,9]
	mul	w1, w0, w1
	sub	w1, w1, #2
	cmp	w2, w1
	ldrh	w2, [x6,998]
	bne	.L3819
	ldr	x1, [x6,232]
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
	ldrh	w1, [x6,998]
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
	b	.L3820
.L3819:
	ldrb	w5, [x5,1329]
	ldr	x1, [x6,232]
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
	b	.L3820
.L3818:
	ldrb	w2, [x5,1329]
	ldr	x1, [x6,232]
	str	x5, [x29,96]
	mul	w2, w0, w2
	ldr	x0, [x28,8]
	lsl	w2, w2, 2
	str	x6, [x29,136]
	bl	ftl_memcpy
	ldr	x5, [x29,96]
	ldr	x6, [x29,136]
	ldrb	w1, [x5,1329]
	ldrh	w0, [x5,1346]
	mul	w0, w0, w1
	add	x1, x19, :lo12:.LANCHOR0
	lsl	w2, w0, 2
	ldrb	w1, [x1,3360]
	cmp	w1, w0, lsr 6
	bge	.L3821
	ldrh	w1, [x6,998]
	sub	w2, w1, w2
.L3821:
	ldr	x1, [x29,144]
	ldr	x5, [x28,8]
	add	x0, x5, w0, sxtw 2
	add	x1, x1, :lo12:.LANCHOR5
	ldr	x1, [x1,240]
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
	ldrh	w0, [x1,1346]
	ldrb	w1, [x1,1329]
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
	b	.L3932
.L3816:
	ldr	x0, [x29,176]
	lsl	x0, x0, 2
	str	x0, [x29,136]
	ldr	x0, [x29,144]
	ldr	x1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,240]
	ldr	w0, [x0,x1]
	mov	w1, 1
	str	w0, [x28,40]
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3822
	cmn	w0, #1
	bne	.L3823
.L3822:
	add	x1, x19, :lo12:.LANCHOR0
	add	x5, x21, :lo12:.LANCHOR3
	ldrb	w2, [x1,1257]
	mov	w1, 24
	ldrh	w0, [x5,1312]
	sub	w2, w1, w2
	mov	w1, 1
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x28,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x5,1314]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3870
	cmn	w0, #1
	bne	.L3823
.L3870:
	ldr	x0, [x28,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3823:
	ldr	w0, [x28,52]
	cmp	w0, 512
	beq	.L3871
	cmn	w0, #1
	bne	.L3826
.L3871:
	ldr	x1, [x29,104]
	mov	w2, 1223
	ldr	x0, [x29,112]
	bl	printk
	bl	dump_stack
.L3826:
	ldr	x1, [x28,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,3372]
	cmp	w2, w0
	bcc	.L3828
	mov	w0, -1
	str	w0, [x1,4]
.L3828:
	ldr	x0, [x28,24]
	ldr	x2, [x29,136]
	ldr	w1, [x0,4]
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0,232]
	str	w1, [x0,x2]
	ldr	x0, [x28,24]
	ldr	w1, [x28,40]
.L3932:
	str	w1, [x0,8]
.L3820:
	ldr	x0, [x28,24]
	str	wzr, [x0,16]
	b	.L3814
.L3940:
	ldrb	w1, [x5,1956]
	and	w6, w27, 1
	add	w0, w26, w6
	cbz	w1, .L3831
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w1, [x1,w26,sxtw 1]
	cbz	w6, .L3831
	cmp	w1, w26
	csel	w0, w0, w1, cs
.L3831:
	add	x6, x21, :lo12:.LANCHOR3
	ldr	x4, [x29,184]
	udiv	w1, w27, w23
	add	x5, x19, :lo12:.LANCHOR0
	add	x5, x5, x2, lsl 6
	add	w1, w1, w20
	ldrh	w6, [x6,1418]
	add	x1, x4, w1, sxtw 1
	ldrh	w1, [x1,16]
	madd	w0, w1, w6, w0
	orr	w0, w0, 50331648
	str	w0, [x5,1352]
	b	.L3832
.L3833:
	add	w0, w0, w20
	ldr	x4, [x29,184]
	add	x0, x4, w0, sxtw 1
	ldrh	w5, [x0,16]
	madd	w0, w5, w7, w26
	b	.L3936
.L3829:
	cmp	w0, 2
	bne	.L3832
	ldrb	w6, [x5,1330]
	add	x1, x1, 32
	ldrh	w0, [x5,1418]
	cbnz	w6, .L3836
	ldr	w4, [x29,160]
	add	w5, w4, w20
	ldr	x4, [x29,184]
	add	x5, x4, w5, sxtw 1
	ldrh	w5, [x5,16]
	madd	w0, w5, w0, w26
	b	.L3933
.L3836:
	add	w5, w20, w27, lsr 1
	ldr	x4, [x29,184]
	add	x5, x4, w5, sxtw 1
	ldrh	w5, [x5,16]
	madd	w0, w5, w0, w26
	and	w5, w27, 1
	add	w0, w0, w5
.L3933:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L3832:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1312
	add	x2, x0, x2, lsl 6
	add	x0, x3, :lo12:.LANCHOR5
	ldrh	w1, [x2,48]
	ldr	x0, [x0,248]
	ldr	w5, [x2,40]
	str	w5, [x0,x1,lsl 2]
	b	.L3857
.L3941:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0,w26,sxtw 1]
	cmp	w0, w26
	bcs	.L3837
	tbz	x27, 0, .L3837
	ldr	x0, [x1,1336]
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
	ldr	x0, [x1,1336]
	ldr	x1, [x29,120]
	strb	w2, [x0,x1]
	mov	w1, -1
	ldr	x0, [x3,240]
	str	w1, [x0,x4,lsl 2]
	b	.L3837
.L3942:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1328]
	cmp	w0, 3
	add	x0, x29, 192
	bne	.L3839
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w2, [x2,1268]
	cbz	w2, .L3840
	ldr	w1, [x29,168]
	sub	w2, w1, #1
	mov	w1, 0
.L3841:
	cmp	w1, w2
	bge	.L3935
	ldr	x3, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w5, [x5,1]
	strb	w5, [x3]
	b	.L3841
.L3840:
	ldrb	w1, [x1,1344]
	cbnz	w1, .L3934
	cmp	w25, 1
	mov	w2, 9
	beq	.L3934
	cmp	w25, 2
	bne	.L3847
	mov	w2, 13
.L3934:
	ldr	x1, [x29,192]
	strb	w2, [x1,60]
	b	.L3846
.L3847:
	ldr	x2, [x29,192]
	strb	w1, [x2,60]
.L3846:
	add	x1, x0, 24
	mov	w2, w24
	bl	sblk_xlc_prog_pages
	b	.L3843
.L3839:
	ldrb	w2, [x1,1330]
	cbz	w2, .L3848
	ldrb	w1, [x1,1331]
	cbnz	w1, .L3849
.L3848:
	ldr	w1, [x29,168]
	sub	w2, w1, #1
	mov	w1, 0
	b	.L3850
.L3849:
	mov	w1, w24
	bl	sblk_3d_mlc_prog_pages
	b	.L3843
.L3850:
	cmp	w1, w2
	bge	.L3935
	ldr	x3, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w5, [x5,1]
	strb	w5, [x3]
	b	.L3850
.L3935:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldr	x0, [x29,192]
	ldrb	w1, [x29,168]
	bl	sblk_prog_page
.L3843:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1,1344]
	cbz	w0, .L3852
	add	w0, w24, w24, lsl 1
	b	.L3853
.L3852:
	ldrb	w0, [x1,1330]
	cbz	w0, .L3854
	lsl	w0, w24, 1
	b	.L3853
.L3854:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,1268]
	mov	w0, w24
	cbz	w2, .L3853
	ldrb	w1, [x1,1956]
	ldrh	w0, [x29,168]
	cbz	w1, .L3853
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w1, [x1,w26,sxtw 1]
	cmp	w1, w26
	csel	w0, w0, w24, cs
.L3853:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	ldr	x2, [x19,3392]
	ldr	w1, [x2,52]
	add	w1, w1, w0
	str	w1, [x2,52]
	ldr	x2, [x29,184]
	ldrh	w1, [x19,5530]
	add	w0, w0, w1
	ldrh	w1, [x21,1346]
	uxth	w0, w0
	strh	w0, [x19,5530]
	ldrb	w2, [x2,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L3855
	ldr	x0, [x19,1120]
	strh	wzr, [x0,86]
.L3855:
	bl	gc_write_completed
.L3783:
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
	beq	.L3944
.L3955:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, 16
	ldrb	w2, [x0,218]
	cmp	w2, 6
	bhi	.L4140
	adrp	x0, .L3947
	add	x0, x0, :lo12:.L3947
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x1, .Lrtx3947
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx3947:
	.section	.rodata
	.align	0
	.align	2
.L3947:
	.2byte	(.L3946 - .Lrtx3947) / 4
	.2byte	(.L3948 - .Lrtx3947) / 4
	.2byte	(.L3949 - .Lrtx3947) / 4
	.2byte	(.L3950 - .Lrtx3947) / 4
	.2byte	(.L3951 - .Lrtx3947) / 4
	.2byte	(.L4073 - .Lrtx3947) / 4
	.2byte	(.L3953 - .Lrtx3947) / 4
	.text
.L3944:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w21, [x0,1004]
	cbnz	w21, .L3954
	ldr	w0, [x0,1008]
	cbz	w0, .L3955
.L3954:
	adrp	x0, .LANCHOR3+1388
	ldrh	w0, [x0,#:lo12:.LANCHOR3+1388]
	cmp	w22, w0, lsr 2
	bls	.L3955
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3400]
	cmp	w0, w22
	bcs	.L3955
	add	w21, w21, 20
	bl	timer_get_time
	cmp	w21, w0
	bcs	.L3956
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0,1004]
.L3956:
	add	x27, x20, :lo12:.LANCHOR5
	ldr	w21, [x27,1008]
	bl	timer_get_time
	add	w21, w21, 20
	cmp	w21, w0
	bcs	.L3957
	str	wzr, [x27,1008]
.L3957:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,124]
	cbz	w0, .L4180
	b	.L3955
.L3946:
	add	x0, x19, :lo12:.LANCHOR0
	add	w23, w25, w23
	ldrh	w2, [x24,80]
	uxth	w23, w23
	ldrh	w1, [x0,3382]
	ldrh	w25, [x0,3384]
	add	w25, w25, w1
	mov	w1, 65535
	cmp	w2, w1
	uxth	w25, w25
	beq	.L3958
	cbnz	w26, .L3959
	ldrh	w0, [x0,3400]
	cmp	w22, w0, lsl 1
	bge	.L4180
.L3959:
	adrp	x21, .LANCHOR3
	add	x26, x21, :lo12:.LANCHOR3
	add	x0, x26, 1376
	ldrh	w1, [x26,1386]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x26,1386]
	bl	_list_get_gc_head_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3961
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,220]
	add	w1, w1, 1
	str	w1, [x3,220]
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x4, [x3,1112]
	ldrh	w2, [x4,x2]
	ldrh	w4, [x26,1384]
	cmp	w4, w2
	bcs	.L3962
	ldrh	w4, [x3,1088]
	cmp	w1, w4, lsr 4
	bls	.L3961
	ldrh	w1, [x3,3402]
	cmp	w1, w2
	bls	.L3961
.L3962:
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 0
	ldrb	w2, [x2,1345]
	bl	gc_add_sblk
	cbz	w0, .L3963
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	str	wzr, [x20,220]
	strb	w0, [x20,218]
	b	.L4180
.L3961:
	add	x0, x21, :lo12:.LANCHOR3
	strh	wzr, [x0,1386]
.L3963:
	cmp	w23, 15
	mov	w26, 2
	bls	.L3964
	cbz	w25, .L4075
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	mov	w26, 1
	ldrh	w1, [x1,3386]
	ldrh	w0, [x0,1390]
	cmp	w1, w0
	bls	.L3964
.L4075:
	mov	w26, 2
.L3964:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3965
	add	x19, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR3
	mov	w3, w22
	mov	w4, w23
	ldr	x0, [x19,1120]
	ldrb	w2, [x1,1345]
	mov	w1, 2807
	ldrh	w5, [x0,124]
	ldrh	w6, [x0,120]
	ldrh	w7, [x0,122]
	ldrh	w0, [x24,80]
	str	w0, [sp]
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	bl	printk
.L3965:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, w26
	mov	w2, 1
	ldrb	w0, [x21,1345]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3966
.L3967:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	b	.L4182
.L3966:
	ldrb	w0, [x21,1345]
	mov	w1, 3
	mov	w2, 1
	bl	gc_search_src_blk
	cmp	w0, wzr
	bgt	.L3967
	b	.L4180
.L3958:
	cmp	w26, 1
	bne	.L3968
	ldr	x1, [x0,3392]
	ldrh	w1, [x1,150]
	cbz	w1, .L3969
	ldrh	w0, [x0,3380]
	cmp	w0, 8
	bls	.L3969
	bl	gc_ink_check_sblk
.L3969:
	bl	gc_scan_static_data
	adrp	x21, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,122]
	cbz	w0, .L3970
.L3971:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	add	x20, x20, :lo12:.LANCHOR5
	strb	w0, [x21,1345]
.L4182:
	strb	w0, [x20,218]
	b	.L4180
.L3970:
	bl	gc_static_wearleveling
	cbnz	w0, .L3971
	bl	gc_block_vpn_scan
	cbz	w23, .L3972
	cmp	w22, w25
	bcs	.L3973
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0,1388]
	cmp	w0, w22
	bhi	.L3974
.L3973:
	add	x1, x21, :lo12:.LANCHOR3
	add	w0, w22, w25
	ldrh	w2, [x1,1388]
	cmp	w0, w2
	blt	.L3974
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,3386]
	ldrh	w0, [x1,1390]
	cmp	w2, w0
	bcc	.L3972
.L3974:
	add	x24, x21, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x24,1345]
	mov	w0, 16
	bl	zftl_get_gc_node.part.10
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3975
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,224]
	cmp	w3, w1
	bcs	.L3975
	cmp	w22, 2
	bls	.L3975
	str	wzr, [x2,220]
	mov	w1, 1
	mov	w2, 0
	strb	wzr, [x24,1345]
	bl	gc_add_sblk
	cbnz	w0, .L3967
.L3975:
	add	x24, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x24, 1368
	bl	_list_get_gc_head_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3976
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,224]
	cmp	w3, w1
	bcs	.L3976
	cmp	w22, 2
	bls	.L3976
	str	wzr, [x2,220]
	mov	w1, 1
	mov	w2, 0
	strb	wzr, [x24,1345]
	bl	gc_add_sblk
	cbnz	w0, .L3967
.L3976:
	add	x1, x20, :lo12:.LANCHOR5
	add	x2, x21, :lo12:.LANCHOR3
	ldrh	w0, [x1,224]
	ldrh	w24, [x2,1384]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x1,224]
	cmp	w0, w24, lsr 5
	bls	.L3977
	mov	w0, 4
	strh	w0, [x1,224]
.L3977:
	add	x28, x20, :lo12:.LANCHOR5
	add	x2, x19, :lo12:.LANCHOR0
	add	x27, x21, :lo12:.LANCHOR3
	ldr	w0, [x28,220]
	ldrh	w1, [x2,1088]
	add	w0, w0, 1
	strh	wzr, [x27,1360]
	str	w0, [x28,220]
	strh	wzr, [x27,1362]
	cmp	w0, w1, lsr 5
	strh	wzr, [x27,1364]
	bls	.L3978
	ldrh	w0, [x2,3386]
	cmp	w0, w23
	bls	.L3978
	mov	w1, 0
	add	x0, x27, 1376
	str	x2, [x29,104]
	bl	_list_get_gc_head_node
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L4174
	ldr	x2, [x29,104]
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x2,1112]
	ldrh	w1, [x1,x0]
	ldrb	w0, [x27,1329]
	mul	w0, w24, w0
	mov	w24, 16
	cmp	w1, w0
	bgt	.L3984
	str	wzr, [x28,220]
	mov	w1, 2
	ldrb	w0, [x27,1345]
	b	.L4173
.L3978:
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR3
	ldrh	w3, [x0,3386]
	ldrh	w2, [x1,1390]
	cmp	w3, w2
	bcc	.L3981
	ldrb	w0, [x1,1345]
	mov	w2, 1
	mov	w1, 2
	b	.L4172
.L3981:
	ldrh	w1, [x0,3382]
	cbnz	w1, .L3982
	ldrh	w1, [x0,3384]
	cmp	w1, 8
	bls	.L3983
.L3982:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, 1
	ldrb	w0, [x21,1345]
.L4173:
	mov	w2, 4
.L4172:
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3967
	b	.L4174
.L3983:
	ldrh	w0, [x0,5534]
	mov	w24, 16
	cmp	w0, wzr
	csel	w24, w24, wzr, ne
	b	.L3984
.L3972:
	add	x21, x21, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x21,1384]
	ldrh	w1, [x0,224]
	cmp	w1, w2, lsr 5
	bcc	.L4174
	mov	w1, 4
	strh	w1, [x0,224]
.L4174:
	mov	w24, 16
	b	.L3984
.L3968:
	ldrh	w1, [x0,3400]
	cmp	w1, w22
	bcs	.L4174
	ldr	x0, [x0,1120]
	ldrh	w0, [x0,124]
	cbz	w0, .L4180
	b	.L4174
.L3984:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldr	w1, [x0,3412]
	cbz	w1, .L3986
	str	wzr, [x0,3412]
	cmp	w22, 15
	add	x0, x21, :lo12:.LANCHOR3
	bhi	.L4083
	cbnz	w23, .L3987
.L4083:
	strb	wzr, [x0,1345]
	b	.L3989
.L3987:
	mov	w1, 1
	strb	w1, [x0,1345]
.L3989:
	add	x27, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x27, 1376
	bl	_list_get_gc_head_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3990
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 8
	bhi	.L3990
	mov	w1, 1
	str	w1, [x2,3412]
	ldrb	w2, [x27,1345]
	mov	w1, 0
	bl	gc_add_sblk
	cbnz	w0, .L3967
.L3990:
	add	x27, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x27, 1368
	bl	_list_get_gc_head_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3991
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3991
	mov	w1, 1
	str	w1, [x2,3412]
	ldrb	w2, [x27,1345]
	mov	w1, 0
	bl	gc_add_sblk
	cbnz	w0, .L3967
.L3991:
	mov	w0, 0
	bl	zftl_get_gc_node.part.10
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3986
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,1112]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3986
	mov	w1, 1
	str	w1, [x2,3412]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w2, [x2,1345]
	bl	gc_add_sblk
	cbnz	w0, .L3967
.L3986:
	add	x28, x19, :lo12:.LANCHOR0
	add	x27, x21, :lo12:.LANCHOR3
	mov	w1, 1
	ldr	x0, [x28,1120]
	strb	w1, [x27,1345]
	ldrh	w3, [x0,124]
	cbz	w3, .L3992
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x27,1345]
	strb	w1, [x20,218]
	b	.L3993
.L3992:
	ldrh	w0, [x28,3400]
	cmp	w22, w0
	bcs	.L3994
	cbz	w23, .L3995
	cmp	w23, 16
	bls	.L3996
	ldrh	w2, [x28,3386]
	ldrh	w0, [x27,1390]
	cmp	w2, w0
	bhi	.L3996
	mov	w0, w1
	mov	w2, 4
	str	x3, [x29,104]
	bl	gc_search_src_blk
	uxth	w0, w0
	ldr	x3, [x29,104]
	cbz	w0, .L4179
	mov	w1, w3
	add	x0, x27, 1376
	bl	_list_get_gc_head_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L4070
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,220]
	add	w1, w1, 1
	str	w1, [x3,220]
	ldr	x3, [x28,1112]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x27,1384]
	cmp	w3, w2
	bcs	.L4000
	ldrh	w3, [x28,1088]
	cmp	w1, w3, lsr 4
	bls	.L4070
	ldrh	w1, [x28,3402]
	cmp	w1, w2
	bls	.L4070
.L4000:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w2, [x21,1345]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19,3412]
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0,220]
	b	.L4070
.L3996:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4070
	add	x21, x21, :lo12:.LANCHOR3
	ldrb	w0, [x21,1345]
	b	.L4176
.L3995:
	strb	w23, [x27,1345]
	cmp	w22, 16
	mov	w0, w23
	bhi	.L4176
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4070
.L4179:
	ldrb	w0, [x27,1345]
.L4176:
	mov	w1, 3
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3998
.L3994:
	cmp	w26, 1
	mov	w1, w24
	bne	.L4140
	cmp	w22, w0, lsl 1
	bge	.L4003
	cmp	w25, w23, lsr 1
	bcs	.L4004
	ldrh	w2, [x28,3386]
	ldrh	w0, [x27,1390]
	cmp	w2, w0
	bcs	.L4004
	ldrh	w0, [x27,1388]
	lsr	w0, w0, 2
	strh	w0, [x28,3400]
	b	.L4140
.L4004:
	mov	w0, 8
	bl	zftl_get_gc_node.part.10
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L4005
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,1112]
	ldrh	w1, [x2,x1]
	cmp	w1, 3
	bhi	.L4005
	cbz	w22, .L4005
	add	x1, x21, :lo12:.LANCHOR3
	mov	w2, 0
	strb	wzr, [x1,1345]
	mov	w1, 1
	bl	gc_add_sblk
	cbnz	w0, .L4070
.L4005:
	add	x4, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x4, 1376
	str	x4, [x29,104]
	bl	_list_get_gc_head_node
	uxth	w26, w0
	mov	w1, 65535
	mov	w28, w26
	cmp	w26, w1
	ldr	x4, [x29,104]
	bne	.L4006
.L4011:
	cmp	w23, 1
	bhi	.L4007
	b	.L4008
.L4006:
	add	x27, x20, :lo12:.LANCHOR5
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x26, w26
	ldr	w1, [x27,220]
	ldrh	w3, [x2,1088]
	add	w1, w1, 1
	str	w1, [x27,220]
	cmp	w1, w3, lsr 4
	bls	.L4009
	ldr	x1, [x2,1112]
	str	wzr, [x27,220]
	ldrh	w3, [x1,x26,lsl 1]
	ldrh	w1, [x2,3402]
	cmp	w3, w1
	bcs	.L4009
	mov	w3, 1
	mov	w1, 0
	mov	w2, 1
	strb	w3, [x4,1345]
	str	x3, [x29,104]
	bl	gc_add_sblk
	ldr	x3, [x29,104]
	cbz	w0, .L4009
	strb	w3, [x27,218]
	b	.L3993
.L4009:
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR3
	ldr	x0, [x1,1112]
	ldrh	w2, [x0,x26,lsl 1]
	ldrh	w0, [x3,1384]
	cmp	w2, w0, lsr 1
	bhi	.L4010
	mov	w0, w28
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L4070
.L4010:
	ldrh	w4, [x1,3382]
	ldrh	w0, [x1,3384]
	ldrh	w3, [x3,1388]
	add	w0, w4, w0
	cmp	w0, w3, lsl 1
	ble	.L4011
	ldrh	w0, [x1,3402]
	cmp	w0, w2
	bcc	.L4008
	b	.L4011
.L4007:
	add	x26, x21, :lo12:.LANCHOR3
	mov	w0, 1
	cmp	w23, 16
	strb	w0, [x26,1345]
	bls	.L4012
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,3386]
	ldrh	w1, [x26,1390]
	cmp	w2, w1
	bhi	.L4012
	mov	w1, w0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4013
	ldrb	w0, [x26,1345]
	b	.L4175
.L4012:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L4013
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w0, [x0,1345]
.L4175:
	mov	w1, 3
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
.L4013:
	add	x21, x21, :lo12:.LANCHOR3
	cmp	w22, w25, lsr 1
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x21,1388]
	bls	.L4177
	lsr	w1, w1, 2
	b	.L4178
.L4008:
	cmp	w22, w25
	bcs	.L4015
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 4
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x21,1345]
	bl	zftl_get_gc_node.part.10
	mov	w1, 65535
	uxth	w0, w0
	cmp	w0, w1
	beq	.L4184
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,1112]
	ldrb	w2, [x21,1329]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x21,1384]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	bgt	.L4184
	mov	w1, 3
	mov	w0, 0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	ldrh	w1, [x21,1388]
.L4177:
	lsr	w1, w1, 1
.L4178:
	strh	w1, [x19,3400]
	b	.L3998
.L4015:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
.L4184:
	ldrh	w0, [x21,1388]
	lsr	w0, w0, 2
	strh	w0, [x19,3400]
	b	.L3993
.L4003:
	ldrh	w0, [x27,1388]
	lsr	w0, w0, 2
	strh	w0, [x28,3400]
	ldrh	w0, [x28,5534]
	cmp	w0, wzr
	csel	w24, w24, wzr, ne
	b	.L3993
.L3998:
	mov	w1, w24
	cbz	w0, .L4140
.L4070:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,218]
	b	.L3993
.L3948:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21,3424]
	cmp	w1, w0
	bne	.L4018
	bl	gc_get_src_blk
	strh	w0, [x21,3424]
.L4018:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0,3424]
	cmp	w1, w2
	beq	.L4019
	ldrh	w2, [x0,1088]
	cmp	w2, w1
	bhi	.L4019
	mov	w1, -1
	strh	w1, [x0,3424]
.L4019:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 3424
	ldrh	w3, [x0,3424]
	cmp	w3, w2
	beq	.L4181
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,1096]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L4022
	mov	x0, 0
.L4021:
	cmp	w5, w0, uxth
	bls	.L4022
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L4021
.L4027:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,3424]
	b	.L4180
.L4022:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L4084
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L4024
.L4084:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,1112]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L4027
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3306
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L4027
.L4024:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	b	.L4182
.L3949:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L4028
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 3
	b	.L4182
.L4028:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,3424]
	cmp	w0, w3
	beq	.L3967
	ldrh	w3, [x1,3448]
	add	x20, x20, :lo12:.LANCHOR5
	cbz	w3, .L4029
	mov	w0, 4
	strh	wzr, [x1,3450]
	strb	w0, [x20,218]
	b	.L4180
.L4029:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,1112]
	mov	w2, 1
	strb	w2, [x20,218]
	ldrh	w0, [x1,x0]
	cbz	w0, .L4030
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3336
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4030:
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
	bhi	.L4031
	strh	w0, [x21,30]
	b	.L4027
.L4031:
	strh	wzr, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L4027
.L3950:
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR3
.L4141:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,3426]
	add	x0, x21, 3424
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w1, [x1,1384]
	cmp	w2, w1
	bcs	.L4033
	cmp	w22, 7
	bls	.L4141
	b	.L4180
.L4033:
	ldrh	w3, [x0,24]
	cbz	w3, .L4034
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, 4
	strh	wzr, [x0,26]
	ldr	x2, [x21,1112]
	strb	w1, [x20,218]
	ldrh	w1, [x0]
	ubfiz	x0, x1, 1, 16
	ldrh	w2, [x2,x0]
	cmp	w2, w3
	beq	.L4035
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L4035
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	bl	printk
.L4035:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,3424]
	ldr	x1, [x0,1112]
	ldrh	w0, [x0,3448]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L4036
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3379
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4036:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,3424]
	ldr	x0, [x19,1112]
	ldrh	w2, [x19,3448]
	strh	w2, [x0,x1,lsl 1]
	b	.L4180
.L4034:
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x21, [x21,1096]
	add	x21, x21, w1, uxth 2
	strb	w0, [x20,218]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L4037
	ldrb	w2, [x21,2]
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
	ubfx	x2, x2, 5, 3
	bl	printk
.L4037:
	ldrb	w0, [x21,2]
	tst	w0, 192
	beq	.L4085
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L4038
.L4085:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3389
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4038:
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
	bhi	.L4040
	strh	w0, [x20,30]
	b	.L4180
.L4040:
	strh	wzr, [x20,30]
	b	.L4183
.L3951:
	cbnz	w26, .L4041
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3400]
	cmp	w0, w22
	bcc	.L4180
.L4041:
	ldrh	w1, [x24,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L4042
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w23, [x0,1345]
	cmp	w23, 1
	bne	.L4042
	bl	ftl_flush
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,5616]
	cbz	w0, .L4043
	mov	w0, w23
.L4043:
	mov	w1, 5
	bl	zftl_gc_get_free_sblk
	uxth	w23, w0
	mov	w0, 65535
	cmp	w23, w0
	beq	.L4045
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x22, [x0,1096]
	add	x22, x22, w23, uxth 2
	ldrb	w0, [x22,2]
	tst	w0, 224
	beq	.L4046
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3423
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4046:
	ldrb	w0, [x22,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x22,2]
.L4071:
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
	ldrh	w1, [x21,1384]
	strb	w0, [x24,89]
	ldrb	w2, [x21,1329]
	mul	w0, w0, w1
	strh	w0, [x24,86]
	mov	w1, 255
	ldrh	w0, [x21,1346]
	strh	w23, [x24,80]
	strh	wzr, [x24,82]
	mul	w2, w0, w2
	ldr	x0, [x20,232]
	strb	wzr, [x24,85]
	lsl	w2, w2, 2
	strh	wzr, [x24,90]
	bl	ftl_memset
	ldrh	w0, [x21,1346]
	mov	w1, 255
	ldrb	w2, [x21,1329]
	mul	w2, w0, w2
	ldr	x0, [x20,240]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x21,1346]
	mov	w1, 255
	ldrb	w2, [x21,1329]
	ldr	x0, [x21,1336]
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
	str	w0, [x19,5620]
	mov	w0, 0
	strh	wzr, [x19,5530]
	strh	wzr, [x19,5532]
	strh	wzr, [x19,5618]
	bl	ftl_info_flush
	b	.L4180
.L4042:
	cmp	w26, 1
	mov	w23, 4
	csinc	w23, w23, wzr, eq
	cmp	w22, 15
	add	w0, w23, 4
	adrp	x27, .LANCHOR3
	csel	w23, w0, w23, ls
	add	x25, x19, :lo12:.LANCHOR0
.L4049:
	sub	w23, w23, #1
	uxtb	w23, w23
	cmp	w23, 255
	beq	.L4180
	add	x26, x27, :lo12:.LANCHOR3
	bl	gc_do_copy_back
	adrp	x22, .LANCHOR3
	ldrb	w0, [x26,1345]
	cbnz	w0, .L4050
	ldrb	w0, [x25,3362]
	cmp	w0, 3
	bhi	.L4051
	bl	ftl_write_commit
.L4051:
	ldrh	w1, [x25,3450]
	ldrh	w0, [x25,3448]
	cmp	w1, w0
	bcc	.L4049
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,218]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x25,3424]
	ldr	x0, [x25,1112]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L4053
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3507
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4053:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,3424]
	ldr	x3, [x1,1112]
	ubfiz	x2, x0, 1, 16
	ldrh	w4, [x3,x2]
	cbnz	w4, .L4054
	strh	w4, [x3,x2]
	ldrh	w0, [x1,3424]
	bl	ftl_free_sblk
	b	.L4027
.L4054:
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	b	.L4027
.L4050:
	ldrh	w0, [x25,5618]
	add	x21, x25, 3424
	cbz	w0, .L4055
	strh	wzr, [x21,2194]
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	w0, [x21,2196]
	cmn	w0, #1
	beq	.L4056
	ldrb	w1, [x26,1956]
	cbnz	w1, .L4057
	ldrb	w1, [x26,1330]
	cbz	w1, .L4058
.L4057:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x2, [x1,3392]
	ldr	w3, [x2,156]
	mov	w2, 20041
	movk	w2, 0x444b, lsl 16
	cmp	w3, w2
	bne	.L4058
	add	x22, x22, :lo12:.LANCHOR3
	ldrb	w3, [x1,1257]
	mov	w2, 24
	sub	w2, w2, w3
	mov	w3, 1
	ldrh	w4, [x22,1312]
	sub	w2, w2, w4
	lsr	w0, w0, w4
	lsl	w2, w3, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x22,1314]
	udiv	w0, w0, w2
	ldr	x2, [x1,1096]
	lsl	x0, x0, 2
	add	x5, x2, x0
	ldrb	w3, [x5,2]
	tbz	x3, 3, .L4056
	ldrh	w4, [x1,3376]
	ldrh	w6, [x22,1388]
	add	w4, w4, 8
	cmp	w4, w6
	bge	.L4056
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
	b	.L4056
.L4058:
	bl	ftl_mask_bad_block
.L4056:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,1120]
	str	wzr, [x21,5620]
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
	cbz	x0, .L4059
	bl	zbuf_free
.L4059:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	str	xzr, [x19,3432]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3567
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x20,218]
	bl	printk
	bl	dump_stack
	b	.L4180
.L4055:
	ldrh	w0, [x24,86]
	cmp	w0, 1
	bls	.L4060
	ldrh	w1, [x21,26]
	ldrh	w0, [x21,24]
	cmp	w1, w0
	bcc	.L4049
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20,218]
	ldrh	w0, [x21,56]
	add	w1, w0, 1
	add	x0, x21, w0, sxtw 1
	strh	w1, [x21,56]
	ldrh	w1, [x21]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x21]
	b	.L4180
.L4060:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 5
	ldrh	w1, [x21,26]
	strb	w0, [x20,218]
	ldrh	w0, [x21,24]
	cmp	w1, w0
	bcc	.L4061
	ldrh	w0, [x21,56]
	add	w1, w0, 1
	add	x0, x21, w0, sxtw 1
	strh	w1, [x21,56]
	ldrh	w1, [x21]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x21]
.L4061:
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
	ldrh	w2, [x1,1384]
	strh	w2, [x20,3442]
	ldrb	w2, [x1,1328]
	strh	w2, [x20,3444]
	cbz	w3, .L4062
	ldrh	w1, [x1,1346]
	strh	w1, [x20,3442]
	mov	w1, 1
	strh	w1, [x20,3444]
.L4062:
	cmp	w2, 2
	bne	.L4064
	add	x0, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	add	x0, x0, 3424
	ldrh	w1, [x0,18]
	lsl	w1, w1, 1
	strh	w1, [x0,18]
	ldrb	w1, [x22,1330]
	cbnz	w1, .L4064
	mov	w1, 1
	strh	w1, [x0,20]
.L4064:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,3446]
	b	.L4180
.L4073:
	mov	w21, 0
	add	x24, x19, :lo12:.LANCHOR0
.L3952:
	bl	gc_check_data_one_wl
	cbz	w0, .L4067
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
	strb	wzr, [x20,218]
.L4183:
	bl	flt_sys_flush
	b	.L4180
.L4067:
	ldrh	w1, [x24,3440]
	ldrh	w0, [x24,3442]
	cmp	w1, w0
	bcc	.L4068
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 6
	strb	w0, [x20,218]
	ldr	x0, [x24,3432]
	bl	zbuf_free
	str	xzr, [x24,3432]
	b	.L4180
.L4068:
	cmp	w22, 15
	bls	.L3952
	cmp	w26, 1
	bne	.L4180
	add	w21, w21, 1
	uxtb	w21, w21
	cmp	w21, 4
	bls	.L3952
	b	.L4180
.L3953:
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
.L4181:
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x20,218]
.L4180:
	mov	w1, 16
	b	.L4140
.L3993:
	mov	w1, w24
	b	.L4140
.L4045:
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 3430
	add	x1, x1, 1024
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L4071
.L4140:
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
	strb	w0, [x23,1962]
	strb	w0, [x24,3416]
	strb	w0, [x25,352]
	mov	w0, -1
	str	w0, [x25,1012]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	strb	wzr, [x24,3388]
	strb	wzr, [x23,1963]
	strb	wzr, [x24,3389]
	bl	printk
	add	x1, x22, :lo12:.LANCHOR2
	ldrb	w3, [x24,1153]
	ldrh	w4, [x24,2]
	strb	w3, [x23,1420]
	ldrb	w2, [x1,21]
	ldrb	w9, [x1,20]
	ldrh	w5, [x1,18]
	strb	w2, [x23,1314]
	mul	w2, w2, w3
	ldrb	w0, [x1,17]
	mov	w3, 1
	ldrh	w6, [x1,22]
	mov	w1, 0
	sdiv	w10, w5, w9
	uxtb	w2, w2
	ubfiz	w8, w0, 9, 7
	strb	w9, [x23,1328]
	strb	w0, [x24,3360]
	strh	w5, [x23,1346]
	strh	w10, [x23,1384]
	strb	w2, [x23,1329]
	strh	w6, [x24,1088]
	strh	w8, [x25,998]
	strh	w4, [x23,1418]
.L4186:
	cmp	w3, w4
	bhi	.L4266
	add	w1, w1, 1
	lsl	w3, w3, 1
	uxth	w1, w1
	b	.L4186
.L4266:
	add	x3, x21, :lo12:.LANCHOR3
	sub	w1, w1, #1
	mul	w7, w0, w5
	mov	w4, 0
	strh	w1, [x3,1312]
	mov	w1, 1
	mul	w3, w7, w6
	lsr	w11, w3, 21
.L4188:
	cmp	w1, w11
	bhi	.L4267
	add	w4, w4, 1
	lsl	w1, w1, 1
	uxth	w4, w4
	b	.L4188
.L4267:
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
	str	w3, [x4,1016]
	str	w0, [x11,3372]
	sub	w11, w8, #1
	mul	w7, w7, w2
	sub	w7, w7, #1
	add	w0, w11, w0, lsl 2
	udiv	w3, w3, w7
	add	w3, w3, 8
	udiv	w0, w0, w8
	strh	w0, [x4,1020]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w10
	strh	w0, [x4,264]
	add	x0, x21, :lo12:.LANCHOR3
	strh	w3, [x0,1390]
	strh	w6, [x0,1388]
	bhi	.L4190
	mov	w3, 80
	strh	w3, [x0,1388]
.L4190:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 2000
	add	x6, x20, :lo12:.LANCHOR0
	mov	w4, 256
	cmp	w9, 2
	strh	w3, [x0,1960]
	mov	w3, 50
	strh	w3, [x0,1958]
	add	x3, x19, :lo12:.LANCHOR5
	strh	w4, [x3,260]
	mov	w4, 48
	strh	w4, [x3,262]
	mov	w4, 32
	strh	w4, [x6,1104]
	beq	.L4191
	ldrb	w7, [x0,1344]
	cbz	w7, .L4192
.L4191:
	add	x3, x21, :lo12:.LANCHOR3
	mov	w0, 150
	mov	w4, 64
	mov	w6, 12
	strh	w0, [x3,1958]
	add	x0, x19, :lo12:.LANCHOR5
	strh	w4, [x0,262]
	add	x4, x20, :lo12:.LANCHOR0
	strh	w6, [x4,1104]
	ldrb	w6, [x20,#:lo12:.LANCHOR0]
	cbnz	w6, .L4193
	mov	w6, 4
	strh	w6, [x4,1104]
	mov	w4, 600
	strh	w4, [x3,1960]
	mov	w3, 128
	strh	w3, [x0,260]
.L4193:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,1269]
	cbz	w0, .L4195
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 200
	strh	w3, [x0,1958]
	mov	w3, 2000
	strh	w3, [x0,1960]
	b	.L4195
.L4192:
	ldrb	w6, [x6,1268]
	cbz	w6, .L4195
	mov	w6, 1200
	strh	w4, [x0,1958]
	strh	w6, [x0,1960]
	strh	w4, [x3,262]
.L4195:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w3, 1
	mul	w2, w5, w2
	str	wzr, [x0,3412]
	add	x0, x19, :lo12:.LANCHOR5
	cmp	w8, w2, lsl 2
	strb	w3, [x0,1001]
	bge	.L4197
	mov	w2, 2
	strb	w2, [x0,1001]
.L4197:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4198
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	bl	printk
.L4198:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4199
	add	x1, x20, :lo12:.LANCHOR0
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	ldr	w1, [x1,3368]
	bl	printk
.L4199:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4200
	add	x1, x20, :lo12:.LANCHOR0
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	ldr	w1, [x1,3372]
	bl	printk
.L4200:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4201
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC305
	add	x0, x0, :lo12:.LC305
	ldr	w1, [x1,1016]
	bl	printk
.L4201:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4202
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC306
	add	x0, x0, :lo12:.LC306
	ldrh	w1, [x1,1020]
	bl	printk
.L4202:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4203
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
	ldrh	w1, [x1,998]
	bl	printk
.L4203:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4204
	add	x1, x19, :lo12:.LANCHOR5
	adrp	x0, .LC308
	add	x0, x0, :lo12:.LC308
	ldrh	w1, [x1,264]
	bl	printk
.L4204:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4205
	add	x1, x21, :lo12:.LANCHOR3
	adrp	x0, .LC309
	add	x0, x0, :lo12:.LC309
	ldrh	w1, [x1,1388]
	bl	printk
.L4205:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4206
	add	x1, x21, :lo12:.LANCHOR3
	adrp	x0, .LC310
	add	x0, x0, :lo12:.LC310
	ldrh	w1, [x1,1390]
	bl	printk
.L4206:
	bl	zbuf_init
	add	x25, x19, :lo12:.LANCHOR5
	mov	w0, 16384
	add	x24, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR3
	bl	ftl_malloc
	str	x0, [x25,648]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x25,664]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x25,1024]
	ldrh	w1, [x24,1088]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x24,1040]
	ldrb	w1, [x23,1329]
	ldrh	w0, [x23,1384]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x23,1944]
	ldrb	w1, [x23,1329]
	ldrh	w0, [x23,1384]
	ldr	x2, [x25,664]
	str	x2, [x23,1936]
	mul	w0, w0, w1
	add	x3, x2, w0, sxtw 3
	ldrh	w1, [x24,1088]
	str	x3, [x24,1112]
	lsr	w1, w1, 1
	add	w1, w1, w0, lsl 1
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	add	x1, x2, w1, sxtw 2
	str	x1, [x24,1120]
	tbz	x0, 12, .L4207
	adrp	x0, .LC311
	add	x0, x0, :lo12:.LC311
	bl	printk
.L4207:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	add	x3, x19, :lo12:.LANCHOR5
	ldrh	w1, [x0,1088]
	ldrh	w0, [x21,1384]
	ldrb	w21, [x21,1329]
	ldrh	w23, [x3,1020]
	mul	w21, w0, w21
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	lsl	w23, w23, 2
	add	w23, w23, w1, lsl 2
	add	w21, w1, w21, lsl 2
	add	w23, w23, 704
	lsl	w21, w21, 1
	add	w21, w21, 632
	tbz	x0, 12, .L4208
	adrp	x0, .LC312
	ldrh	w3, [x3,998]
	add	x0, x0, :lo12:.LC312
	mov	w1, w21
	mov	w2, w23
	bl	printk
.L4208:
	add	x19, x19, :lo12:.LANCHOR5
	ldrh	w0, [x19,998]
	cmp	w21, w0
	bhi	.L4210
	cmp	w23, w0
	bls	.L4209
.L4210:
.L4211:
	b	.L4211
.L4209:
	bl	sblk_init
	bl	gc_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L4212
	bl	ftl_ext_info_init
	add	x21, x20, :lo12:.LANCHOR0
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
	ldrb	w0, [x19,657]
	cbz	w0, .L4213
	ldr	x1, [x21,3392]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L4213:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w0, [x0,124]
	cbnz	w0, .L4217
	ldrh	w2, [x1,3376]
	mov	w19, 16384
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	ble	.L4214
.L4216:
	mov	w0, 0
	b	.L4212
.L4217:
	mov	w19, 16384
.L4214:
	mov	w21, 65535
.L4249:
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w0, 1
	mov	w1, w0
	bl	zftl_do_gc
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w2, [x0,124]
	cbnz	w2, .L4215
	ldrh	w0, [x0,80]
	cmp	w0, w21
	bne	.L4215
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L4216
.L4215:
	subs	w19, w19, #1
	bne	.L4249
	b	.L4216
.L4212:
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
	str	x0, [x19,272]
	mov	w0, 136
	strb	wzr, [x19,1032]
	bl	ftl_malloc
	str	x0, [x19,280]
	cbnz	x0, .L4269
.L4271:
	mov	w0, -1
	b	.L4270
.L4269:
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x19,1040]
	add	x0, x19, 288
	str	xzr, [x19,288]
	add	x1, x0, 760
	str	xzr, [x19,1048]
	bl	rknand_get_reg_addr
	ldr	x0, [x19,288]
	cbz	x0, .L4271
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x19,1040]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,280]
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
	ldr	x0, [x19,288]
	bl	nand_flash_init
	mov	w22, w0
	cbnz	w0, .L4272
	bl	zftl_init
	mov	w20, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x19,1032]
	b	.L4273
.L4272:
	ldr	x1, [x19,280]
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
	ldr	x0, [x19,288]
	add	x2, x2, :lo12:ftl_discard
	str	x2, [x1,56]
	bl	FlashInit
	mov	w20, w0
	cmn	w22, #2
	adrp	x22, .LANCHOR2
	bne	.L4274
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
.L4274:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x1, .LANCHOR0
	bne	.L4275
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L4276
.L4275:
	add	x0, x1, :lo12:.LANCHOR0
	strb	wzr, [x1,#:lo12:.LANCHOR0]
	strb	wzr, [x0,1154]
.L4276:
	adrp	x2, gNandFlashIDBEccBits
	add	x0, x1, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w2, [x2,#:lo12:gNandFlashIDBEccBits]
	strb	w2, [x0,1152]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x22,18]
	str	xzr, [x21,328]
	ldrb	w2, [x2,#:lo12:gNandFlashEccBits]
	strb	w2, [x0,1244]
	mov	w2, 1
	mov	w0, 0
.L4277:
	cmp	w2, w3
	bhi	.L4282
	add	w0, w0, 1
	lsl	w2, w2, 1
	uxth	w0, w0
	b	.L4277
.L4282:
	adrp	x2, .LANCHOR3+1312
	add	x1, x1, :lo12:.LANCHOR0
	sub	w0, w0, #1
	strb	wzr, [x1,1192]
	strh	w0, [x2,#:lo12:.LANCHOR3+1312]
	cbnz	w20, .L4273
	bl	FtlInit
	mov	w20, w0
.L4273:
	adrp	x0, .LC313
	mov	w1, w20
	add	x0, x0, :lo12:.LC313
	bl	printk
	mov	w0, w20
.L4270:
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
	tbz	x0, 12, .L4284
	adrp	x0, .LC314
	ldr	w4, [x25]
	add	x0, x0, :lo12:.LC314
	mov	w1, w20
	mov	w2, w19
	mov	w3, w21
	bl	printk
.L4284:
	cbnz	w20, .L4285
	adrp	x0, .LANCHOR0+1032
	mov	w20, 24576
	ldr	w1, [x0,#:lo12:.LANCHOR0+1032]
	b	.L4286
.L4285:
	cmp	w20, 3
	mov	w0, -1
	bhi	.L4287
	lsl	w20, w20, 13
	mov	w1, 8192
.L4286:
	cmp	w19, w1
	bcs	.L4303
	cmp	w21, w1
	bhi	.L4303
	add	w2, w19, w21
	mov	w0, -1
	cmp	w2, w1
	bhi	.L4287
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
.L4288:
	cbz	w22, .L4314
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,3388]
	cbz	w1, .L4289
	ldrb	w0, [x0,3362]
	cmp	w0, 2
	bhi	.L4289
	bl	ftl_write_commit
.L4289:
	mov	w0, 0
	bl	buf_alloc
	mov	x28, x0
	cbnz	x0, .L4290
	bl	ftl_write_commit
	b	.L4288
.L4290:
	strb	wzr, [x0,57]
	add	x0, x20, :lo12:.LANCHOR0
	cmp	w23, w26
	ldrb	w4, [x0,3360]
	cset	w0, eq
	strb	w4, [x28,56]
	cmp	w23, w24
	beq	.L4292
	cbz	w0, .L4293
	b	.L4305
.L4292:
	cbz	w0, .L4294
.L4305:
	udiv	w0, w19, w4
	msub	w0, w0, w4, w19
	uxtb	w0, w0
	strb	w0, [x28,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w21
	csel	w4, w21, w4, hi
	b	.L4312
.L4294:
	msub	w4, w4, w23, w27
.L4312:
	strb	w4, [x28,56]
.L4293:
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
	b	.L4288
.L4314:
	bl	ftl_write_commit
	mov	w1, 1
	mov	w0, w22
	bl	zftl_do_gc
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,1120]
	ldrh	w0, [x0,124]
	cbnz	w0, .L4298
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 11
	bgt	.L4300
.L4298:
	mov	w0, 0
	mov	w1, 1
.L4313:
	bl	zftl_do_gc
.L4300:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,3376]
	ldrh	w0, [x1,3380]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L4315
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w0, 1
	mov	w1, w0
	b	.L4313
.L4315:
	bl	timer_get_time
	adrp	x1, .LANCHOR5+1008
	str	w0, [x1,#:lo12:.LANCHOR5+1008]
	mov	w0, 0
	b	.L4287
.L4303:
	mov	w0, -1
.L4287:
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
	bcs	.L4339
	cmp	w19, w1
	cset	w5, hi
	cbnz	w5, .L4339
	add	w2, w0, w19
	mov	w4, -1
	cmp	w2, w1
	bhi	.L4319
	adrp	x23, .LANCHOR5
	add	w22, w0, 24576
	add	x0, x23, :lo12:.LANCHOR5
	ldr	w1, [x0,1056]
	add	w1, w19, w1
	str	w1, [x0,1056]
	adrp	x0, .LANCHOR2
	str	x0, [x29,104]
	ldr	w2, [x0,#:lo12:.LANCHOR2]
	tbz	x2, 12, .L4320
	adrp	x0, .LC315
	mov	w2, w22
	add	x0, x0, :lo12:.LC315
	mov	w3, w19
	mov	w4, w5
	bl	printk
.L4320:
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
	cbz	w26, .L4321
	sub	w24, w24, w26
	mov	w0, w20
	cmp	w24, w19
	csel	w24, w24, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L4322
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L4322:
	ldr	w0, [x29,120]
	and	w27, w24, 65535
	cmn	w0, #1
	beq	.L4324
	mov	w0, 0
	bl	buf_alloc
	mov	x28, x0
	cbz	x0, .L4324
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
.L4324:
	add	w20, w20, 1
	sub	w19, w19, w27
.L4321:
	cbz	w19, .L4326
	bl	ftl_flush
.L4326:
	mov	w0, -1
	mov	w26, 1
	str	w0, [x29,124]
	adrp	x27, .LANCHOR3
	mov	w28, 24
.L4327:
	add	x24, x21, :lo12:.LANCHOR0
	ldrb	w0, [x24,3360]
	cmp	w19, w0
	bcc	.L4367
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L4328
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L4330
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
	b	.L4366
.L4328:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L4330
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	add	x2, x27, :lo12:.LANCHOR3
	ldrb	w1, [x24,1257]
	ldr	w0, [x29,120]
	sub	w1, w28, w1
	ldrh	w3, [x2,1312]
	sub	w1, w1, w3
	lsr	w0, w0, w3
	lsl	w1, w26, w1
	sub	w1, w1, #1
	and	w1, w1, w0
	ldrb	w0, [x2,1314]
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L4366:
	ldr	x1, [x24,3392]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L4330:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,3360]
	sub	w19, w19, w0
	b	.L4327
.L4367:
	cbz	w19, .L4334
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L4335
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L4335:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L4334
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L4334
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w25, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3360]
	cmp	w19, w0
	bcc	.L4337
	adrp	x1, .LANCHOR4
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 1495
	add	x1, x1, 1040
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4337:
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
.L4334:
	add	x0, x23, :lo12:.LANCHOR5
	mov	w4, 0
	ldr	w1, [x0,1056]
	cmp	w1, 8192
	bls	.L4319
	ldr	x0, [x29,104]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L4338
	adrp	x0, .LC315
	mov	w2, w22
	add	x0, x0, :lo12:.LC315
	mov	w3, w19
	bl	printk
.L4338:
	add	x23, x23, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x23,1056]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x21,3412]
	b	.L4319
.L4339:
	mov	w4, -1
.L4319:
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
	adrp	x0, .LC267
	mov	w2, 4
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	add	x0, x0, :lo12:.LC267
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
	adrp	x0, .LC268
	mov	w2, 2
	add	x0, x0, :lo12:.LC268
	add	x1, x1, 416
	ldrh	w3, [x3,264]
	mov	w19, 0
	bl	rknand_print_hex
.L4369:
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w0, [x0,264]
	cmp	w0, w19
	bls	.L4372
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,3392]
	add	x0, x0, w19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L4370
	mov	x1, 0
	bl	ftl_sblk_dump
.L4370:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L4369
.L4372:
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
	ldr	x2, [x0,1144]
	ldrb	w0, [x0,1154]
	ldrb	w2, [x2,19]
	cbnz	w0, .L4373
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L4375
	cmp	w2, 68
	beq	.L4375
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L4373
.L4375:
	adrp	x20, .LANCHOR3
	adrp	x24, .LC316
	add	x20, x20, :lo12:.LANCHOR3
	mov	w25, w1
	add	x24, x24, :lo12:.LC316
	add	x20, x20, 2484
	mov	w27, 65535
.L4377:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,1144]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L4373
	add	w26, w19, w25
	ldrh	w3, [x20,w19,sxtw 1]
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	bl	printk
	ldrh	w0, [x20,w19,sxtw 1]
	cmp	w0, w27
	bne	.L4373
	adrp	x22, .LANCHOR5
	mov	w1, 255
	add	x22, x22, :lo12:.LANCHOR5
	mov	w2, 16384
	add	w19, w19, 1
	ldr	x0, [x22,296]
	uxth	w19, w19
	bl	ftl_memset
	ldr	x4, [x28,1144]
	mov	w0, w23
	ldr	x2, [x22,296]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L4377
.L4373:
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
	ldr	x4, [x3,1144]
	ldrb	w3, [x3,1154]
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
	beq	.L4380
	mov	w4, 19282
	movk	w4, 0x5353, lsl 16
	cmp	w3, w4
	bne	.L4514
.L4380:
	cmp	w0, 15
	bls	.L4514
	mov	w19, w0
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	str	x2, [x29,160]
	str	x1, [x29,216]
	bl	ftl_malloc
	mov	x22, x0
	cbz	x0, .L4514
	adrp	x0, .LANCHOR5+1032
	str	wzr, [x29,236]
	ldrb	w0, [x0,#:lo12:.LANCHOR5+1032]
	cbz	w0, .L4507
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w1, [x23,#:lo12:.LANCHOR0]
	ldrb	w0, [x0,1154]
	cbz	w1, .L4384
	cmp	w0, wzr
	cset	w0, eq
	b	.L4510
.L4384:
	cmp	w0, 3
	beq	.L4438
	ldr	w2, [x29,212]
	cmp	w0, 2
	mov	w1, 2
	csel	w1, w2, w1, ne
	cmp	w0, 2
	mov	w0, 3
	str	w1, [x29,212]
	csel	w0, wzr, w0, ne
	b	.L4510
.L4438:
	str	w0, [x29,212]
	mov	w0, 2
.L4510:
	str	w0, [x29,236]
.L4507:
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
	beq	.L4386
	mov	w1, 19282
	movk	w1, 0x5353, lsl 16
	cmp	w0, w1
	beq	.L4386
	mov	x0, x22
	bl	ftl_free
.L4514:
	mov	w0, -1
	b	.L4508
.L4386:
	mov	w0, 0
	add	w19, w19, 511
	mul	w24, w20, w24
	bl	zftl_flash_exit_slc_mode
	lsr	w19, w19, 9
	add	x0, x23, :lo12:.LANCHOR0
	cmp	w19, 8
	mov	w3, 0
	mov	w2, 63871
	ldrb	w0, [x0,1154]
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
.L4392:
	sub	x1, x0, #512
	ldr	w1, [x1]
	cbnz	w1, .L4387
	cbnz	w3, .L4388
	mov	w1, 17739
	movk	w1, 0x4e52, lsl 16
	b	.L4511
.L4388:
	ldr	x1, [x29,216]
	ldr	w1, [x1,w3,uxtw 2]
.L4511:
	add	w3, w3, 1
	sub	w2, w2, #1
	cmp	w3, 4095
	str	w1, [x0]
	csel	w3, w3, wzr, cc
	cmp	w2, 4096
	sub	x0, x0, #4
	bne	.L4392
	b	.L4391
.L4387:
	adrp	x0, .LC317
	add	x0, x0, :lo12:.LC317
	bl	printk
.L4391:
	mul	w0, w20, w21
	str	w0, [x29,168]
	adrp	x0, .LC318
	ldr	x1, [x29,160]
	add	x0, x0, :lo12:.LC318
	mov	w2, 4
	mov	w3, 5
	mov	x21, 0
	bl	rknand_print_hex
	adrp	x0, .LC319
	mov	w1, w19
	add	x0, x0, :lo12:.LC319
	mov	w2, w19
	bl	printk
	str	w21, [x29,232]
	adrp	x0, .LC320
	add	x0, x0, :lo12:.LC320
	str	x0, [x29,112]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	x0, [x29,104]
.L4434:
	adrp	x26, .LANCHOR5
	mov	w0, w21
	add	x1, x26, :lo12:.LANCHOR5
	ldrb	w1, [x1,1032]
	cbnz	w1, .L4393
	ldr	x1, [x29,160]
	ldr	w2, [x29,168]
	ldr	w1, [x1,x21,lsl 2]
	mul	w20, w1, w2
	cbz	w21, .L4394
	ldr	w2, [x29,136]
	cmp	w2, 1
	bls	.L4394
	sub	w0, w21, #1
	ldr	x2, [x29,160]
	ldr	w0, [x2,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L4394
	b	.L4395
.L4393:
	adrp	x24, .LANCHOR0
	add	x25, x24, :lo12:.LANCHOR0
	ldr	x1, [x25,1128]
	add	x1, x1, w21, uxtw
	ldrb	w20, [x1,32]
	mov	x1, x24
	cmp	w20, 255
	beq	.L4395
	ldr	w2, [x29,168]
	mul	w20, w20, w2
	ldr	w2, [x29,236]
	cbz	w2, .L4394
	tbnz	x0, 0, .L4444
	cmp	w2, 3
	cset	w23, eq
	cbz	w23, .L4396
.L4444:
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w0, [x29,212]
	strb	w0, [x1,#:lo12:.LANCHOR0]
	strb	w0, [x2,1154]
	b	.L4394
.L4396:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	strb	w23, [x24,#:lo12:.LANCHOR0]
	strb	w23, [x25,1154]
.L4394:
	mov	w1, 0
	mov	w2, 512
	mov	x0, x22
	adrp	x24, .LANCHOR0
	bl	ftl_memset
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0,1144]
	ldrb	w25, [x1,9]
	ldrh	w28, [x1,10]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mul	w28, w25, w28
	ldrh	w27, [x1,34]
	ldrb	w1, [x0,1240]
	str	w1, [x29,132]
	udiv	w1, w20, w25
	strb	wzr, [x0,1240]
	mov	w0, 0
	mul	w27, w25, w27
	bl	flash_erase_block
	cmp	w19, w28
	mov	w1, 1
	bls	.L4398
	add	w1, w20, w27
	mov	w0, 0
	bl	flash_erase_block
	mov	w1, 2
.L4398:
	add	x3, x24, :lo12:.LANCHOR0
	ldr	x0, [x3,1144]
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
	bne	.L4442
	ldrb	w0, [x3,1028]
	cmp	w0, 9
	bne	.L4442
	add	x26, x26, :lo12:.LANCHOR5
	mov	w1, 0
	mov	w2, 1024
	str	x3, [x29,224]
	ldr	x26, [x26,336]
	mov	x0, x26
	bl	ftl_memset
	str	wzr, [x26,12]
	mov	w0, 18766
	ldr	x3, [x29,224]
	movk	w0, 0x464e, lsl 16
	str	w0, [x26]
	mov	w0, 12
	str	w0, [x26,4]
	ldrb	w0, [x3,1154]
	strb	wzr, [x26,16]
	cbz	w0, .L4400
	ldr	x0, [x3,1144]
	ldrb	w0, [x0,29]
	strb	w0, [x26,16]
.L4400:
	add	x24, x24, :lo12:.LANCHOR0
	mov	w0, 4
	strb	w0, [x26,17]
	cmp	w25, 8
	ldr	x1, [x24,1144]
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
	b	.L4399
.L4442:
	str	w19, [x29,224]
	mov	x26, 0
.L4399:
	adrp	x10, .LANCHOR0
	ldr	x24, [x29,216]
	mov	w23, 0
	add	x27, x10, :lo12:.LANCHOR0
.L4402:
	ldr	w0, [x29,204]
	cmp	w23, w0
	bcs	.L4515
	ldr	w0, [x29,208]
	ldrb	w2, [x27,1154]
	add	w8, w23, w0
	add	x0, x27, 4
	ubfx	x8, x8, 2, 16
	add	w1, w8, 1
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L4404
	ldrb	w3, [x27,1]
	lsl	w0, w1, 1
	cmp	w3, wzr
	csel	w0, w0, w1, ne
.L4404:
	ldrb	w1, [x27,1028]
	cmp	w1, 9
	beq	.L4512
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L4512:
	str	w0, [x29,240]
	mov	w0, 61424
	str	w0, [x29,244]
	add	x0, x27, 4
	ldrh	w0, [x0,w8,sxtw 1]
	cbnz	w2, .L4408
	mov	w8, w0
	b	.L4409
.L4408:
	ldrb	w1, [x27,1]
	lsl	w0, w8, 1
	cmp	w1, wzr
	csel	w8, w0, w8, ne
.L4409:
	mul	w0, w8, w25
	cbnz	x26, .L4410
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
	bl	fw_flash_page_prog.constprop.30
	ldrb	w0, [x27,1154]
	ldr	x8, [x29,192]
	cbnz	w0, .L4411
	udiv	w1, w28, w25
	add	w2, w8, 1
	bl	id_block_prog_msb_ff_data
.L4411:
	ldr	w0, [x29,224]
	add	x24, x24, 2048
	add	w0, w0, 16
	cmp	w23, w0
	bcc	.L4412
	ldr	w0, [x29,224]
	add	w1, w0, 20
	ldr	x0, [x29,216]
	cmp	w23, w1
	add	x0, x0, 2048
	csel	x24, x24, x0, cs
	b	.L4412
.L4410:
	add	w0, w0, w28
	mov	x1, x26
	add	x2, x29, 240
	bl	fw_flash_page_prog.constprop.30
.L4412:
	add	w9, w23, 4
	mov	x26, 0
	uxth	w23, w9
	b	.L4402
.L4515:
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
	strb	wzr, [x23,1240]
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
	adrp	x0, .LC321
	add	x0, x0, :lo12:.LC321
	str	x0, [x29,120]
.L4414:
	ldr	w0, [x29,172]
	adrp	x27, .LANCHOR0
	cmp	w25, w0
	bcs	.L4516
	ldr	w0, [x29,192]
	add	x1, x23, 4
	ldrb	w2, [x23,1154]
	sub	w0, w26, w0
	uxth	w0, w0
	str	w0, [x29,176]
	ldr	w0, [x29,204]
	add	w0, w25, w0
	udiv	w0, w0, w26
	and	w0, w0, 65535
	ldrh	w1, [x1,w0,sxtw 1]
	cbnz	w2, .L4415
	mov	w0, w1
	b	.L4416
.L4415:
	ldrb	w2, [x23,1]
	lsl	w1, w0, 1
	cmp	w2, wzr
	csel	w0, w1, w0, ne
.L4416:
	ldr	w1, [x29,152]
	ldr	w2, [x29,148]
	madd	w0, w0, w1, w2
	str	w0, [x29,144]
	ldrb	w0, [x23,1152]
	str	w0, [x29,208]
	ldr	w1, [x29,192]
	ldr	w0, [x29,144]
	add	w27, w0, w1
	ldr	x0, [x23,1144]
	ldrb	w0, [x0,9]
	udiv	w27, w27, w0
	ldrb	w0, [x23,1244]
	str	w0, [x29,128]
	ldr	w0, [x29,208]
	bl	nandc_bch_sel
.L4417:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	bl	flash_read_page
	cmn	w0, #1
	mov	w28, w0
	bne	.L4424
	ldrb	w6, [x23,1240]
	cbnz	w6, .L4419
.L4422:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x6, [x0,328]
	cbnz	x6, .L4420
	b	.L4421
.L4419:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	str	x6, [x29,96]
	strb	wzr, [x23,1240]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x6, [x29,96]
	strb	w6, [x23,1240]
	beq	.L4422
	b	.L4513
.L4420:
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	blr	x6
	cmn	w0, #1
	bne	.L4513
.L4421:
	ldrb	w0, [x23,1192]
	cbz	w0, .L4424
	mov	w0, 0
	mov	w1, w27
	mov	x2, x24
	add	x3, x29, 240
	mov	w4, w26
	bl	flash_ddr_tuning_read
.L4513:
	mov	w28, w0
.L4424:
	cmn	w28, #1
	bne	.L4425
	ldr	w0, [x29,208]
	cmp	w0, 16
	beq	.L4425
	mov	w0, 16
	bl	nandc_bch_sel
	mov	w0, 16
	str	w0, [x29,208]
	b	.L4417
.L4425:
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
	beq	.L4427
	ldr	w0, [x29,200]
	cbnz	w0, .L4427
	ldr	w1, [x24]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L4427
	ldr	w0, [x29,172]
	ldr	w1, [x29,176]
	ldrb	w26, [x24,17]
	add	w0, w0, w1
	str	w0, [x29,172]
	b	.L4428
.L4427:
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
.L4428:
	ldr	w0, [x29,176]
	add	w25, w0, w25
	uxth	w25, w25
	b	.L4414
.L4516:
	add	x23, x27, :lo12:.LANCHOR0
	ldrb	w0, [x29,132]
	strb	w0, [x23,1240]
	ldr	w0, [x29,236]
	cbz	w0, .L4430
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	strb	wzr, [x27,#:lo12:.LANCHOR0]
	strb	wzr, [x23,1154]
.L4430:
	ldr	w0, [x29,224]
	mov	x1, 0
	lsl	w0, w0, 7
.L4431:
	cmp	w0, w1
	bls	.L4517
	ldr	w3, [x22,x1,lsl 2]
	add	x1, x1, 1
	ldr	x2, [x29,216]
	add	x2, x2, x1, lsl 2
	ldr	w2, [x2,-4]
	cmp	w3, w2
	beq	.L4431
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L4395
.L4517:
	ldr	w0, [x29,232]
	add	w0, w0, 1
	str	w0, [x29,232]
	cmp	w0, 5
	bhi	.L4436
.L4395:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L4434
.L4436:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR0
	ldrb	w2, [x29,156]
	add	x1, x0, :lo12:.LANCHOR0
	strb	w2, [x0,#:lo12:.LANCHOR0]
	ldr	w2, [x29,236]
	cbz	w2, .L4435
	ldrb	w2, [x29,140]
	strb	w2, [x1,1154]
.L4435:
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
.L4508:
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
	bne	.L4519
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
	cbnz	w1, .L4535
	mov	w1, 19282
	movk	w1, 0x5353, lsl 16
	cmp	w0, w1
	bne	.L4519
.L4535:
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22,1060]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,1064]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	mov	w0, 64
	str	w0, [x22,1072]
.L4519:
	add	x23, x20, :lo12:.LANCHOR5
	adrp	x0, .LC322
	ldr	w2, [x24]
	add	x0, x0, :lo12:.LC322
	mov	w3, w19
	mov	w4, w21
	ldr	x1, [x23,1064]
	bl	printk
	ldrb	w0, [x23,1060]
	cbz	w0, .L4518
	ldr	x22, [x23,1064]
	cbz	x22, .L4518
	sub	w0, w19, #64
	cmp	w0, 499
	bhi	.L4522
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x24
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L4523
.L4522:
	cmp	w19, 563
	bhi	.L4533
.L4523:
	add	x23, x20, :lo12:.LANCHOR5
	ldr	w0, [x23,1072]
	cmp	w0, w19
	beq	.L4531
	mov	x0, x22
	strb	wzr, [x23,1060]
	bl	ftl_free
	str	xzr, [x23,1064]
.L4531:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20,1072]
	b	.L4518
.L4533:
	ldr	w2, [x23,1072]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0,#:lo12:.LANCHOR0+1144]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L4534
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
	b	.L4525
.L4534:
	mov	x3, 0
.L4524:
	cmp	w2, 256
	add	x0, x29, 72
	bls	.L4526
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L4527
.L4526:
	str	w3, [x0,x3,lsl 2]
.L4527:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L4524
.L4525:
	add	x0, x22, 245760
	mov	w3, 61952
	add	x0, x0, 2048
.L4530:
	ldr	w1, [x0]
	cbz	w1, .L4528
	add	w2, w3, 2048
	lsl	w0, w2, 2
	b	.L4529
.L4528:
	sub	w3, w3, #1
	sub	x0, x0, #4
	cmp	w3, 4096
	bne	.L4530
	lsl	w0, w2, 9
.L4529:
	mov	x1, x22
	add	x2, x29, 72
	add	x22, x20, :lo12:.LANCHOR5
	bl	write_idblock
	ldr	x0, [x22,1064]
	strb	wzr, [x22,1060]
	bl	ftl_free
	str	xzr, [x22,1064]
	b	.L4531
.L4518:
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
	bhi	.L4548
	cbnz	w21, .L4548
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L4548:
	adrp	x0, .LANCHOR5+280
	mov	w1, w19
	mov	w2, w20
	ldr	x0, [x0,#:lo12:.LANCHOR5+280]
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
	beq	.L4557
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4558
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4559
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4560
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4561
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4562
	b	.L4593
.L4560:
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4563
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4564
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L4593
	b	.L4615
.L4558:
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4566
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L4567
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4557
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4557
	b	.L4593
.L4567:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4566
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L4568
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L4568
	b	.L4593
.L4562:
	adrp	x0, .LC323
	add	x0, x0, :lo12:.LC323
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbz	x0, .L4569
.L4575:
	adrp	x0, .LC324
	add	x0, x0, :lo12:.LC324
	bl	printk
	b	.L4612
.L4569:
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L4570
.L4572:
	mov	x19, -1
	b	.L4571
.L4570:
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4572
	adrp	x19, .LANCHOR5
	add	x0, x29, 40
	add	x19, x19, :lo12:.LANCHOR5
	mov	x2, 512
	ldr	x1, [x19,1080]
	bl	memcpy
	ldr	w1, [x19,1088]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L4573
	add	x0, x29, 104
	mov	w1, 0
	mov	x2, 128
	str	wzr, [x29,48]
	str	wzr, [x29,52]
	bl	memset
.L4573:
	add	x0, x29, 296
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,56]
	bl	memset
.L4607:
	mov	x0, x20
	add	x1, x29, 40
	mov	x2, 520
	bl	rk_copy_to_user
	cbnz	x0, .L4612
	b	.L4610
.L4559:
	adrp	x0, .LC325
	add	x0, x0, :lo12:.LC325
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4572
	adrp	x1, .LANCHOR5
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR5
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,1088]
	cmp	w3, w2
	bne	.L4556
	ldr	w2, [x29,52]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L4556
	ldr	x19, [x1,1080]
	add	x1, x29, 104
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,52]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L4611
.L4564:
	adrp	x0, .LC326
	add	x0, x0, :lo12:.LC326
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4572
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w0, [x0,1092]
	cbnz	w0, .L4576
.L4579:
	mov	x0, 0
	b	.L4556
.L4576:
	add	x0, x19, :lo12:.LANCHOR5
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	x1, [x0,1096]
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L4577
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,1096]
	str	w2, [x1,4]
	ldr	x0, [x0,1096]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L4577:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 0
	ldr	x1, [x20,1096]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,1080]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4578
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1080]
	str	w1, [x0,4]
	ldr	x0, [x20,1080]
	str	wzr, [x0,8]
.L4578:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1080]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,1092]
	str	wzr, [x19,1088]
	b	.L4610
.L4563:
	adrp	x0, .LC327
	add	x0, x0, :lo12:.LC327
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 20037
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w0, [x29,44]
	cmp	w0, 512
	bhi	.L4572
	adrp	x19, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR5
	ldr	w1, [x0,1092]
	cmp	w1, 1
	beq	.L4579
	ldr	x0, [x0,1096]
	mov	w1, 22867
	movk	w1, 0x4453, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4580
	mov	w1, 22867
	mov	w2, 504
	movk	w1, 0x4453, lsl 16
	str	w1, [x0]
	add	x0, x19, :lo12:.LANCHOR5
	ldr	x1, [x0,1096]
	str	w2, [x1,4]
	ldr	x0, [x0,1096]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L4580:
	add	x20, x19, :lo12:.LANCHOR5
	mov	w0, 1
	ldr	x1, [x20,1096]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,1080]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L4581
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1080]
	str	w1, [x0,4]
	ldr	x0, [x20,1080]
	str	wzr, [x0,8]
.L4581:
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1080]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,1092]
	b	.L4610
.L4615:
	adrp	x0, .LC328
	add	x0, x0, :lo12:.LC328
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w2, [x29,44]
	cmp	w2, 512
	bhi	.L4572
	adrp	x1, .LANCHOR5
	add	x0, x29, 48
	add	x1, x1, :lo12:.LANCHOR5
	uxtw	x2, w2
	add	x1, x1, 1104
	b	.L4613
.L4557:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4583
	adrp	x0, .LC329
	add	x0, x0, :lo12:.LC329
	b	.L4609
.L4583:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4585
	adrp	x0, .LC330
	add	x0, x0, :lo12:.LC330
	b	.L4609
.L4585:
	adrp	x0, .LC331
	add	x0, x0, :lo12:.LC331
.L4609:
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L4612
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR5
	bne	.L4586
	add	x0, x0, :lo12:.LANCHOR5
	add	x1, x29, 40
	mov	x2, 16
	ldr	x0, [x0,1080]
	ldr	w0, [x0,20]
	str	w0, [x29,44]
	strb	w0, [x29,48]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L4556
	b	.L4612
.L4586:
	add	x20, x0, :lo12:.LANCHOR5
	ldr	w1, [x20,1616]
	cmp	w1, 10
	bhi	.L4612
	ldr	x1, [x20,1080]
	ldr	w2, [x29,44]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L4587
	cbz	w3, .L4587
	adrp	x0, .LC332
	mov	w1, w2
	add	x0, x0, :lo12:.LC332
	bl	printk
	ldr	w0, [x20,1616]
	add	w0, w0, 1
	str	w0, [x20,1616]
.L4612:
	mov	x0, -14
	b	.L4556
.L4587:
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1616]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L4588
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L4589
.L4588:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L4589:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L4610
	b	.L4571
.L4566:
	adrp	x0, .LC333
	add	x0, x0, :lo12:.LC333
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L4572
	mov	w0, 30224
	adrp	x1, .LANCHOR5
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR5
	add	x0, x29, 48
	bne	.L4590
	ldr	x1, [x1,1624]
	b	.L4614
.L4590:
	ldr	x1, [x1,1632]
.L4614:
	add	x1, x1, 8
.L4613:
	bl	memcpy
	b	.L4607
.L4568:
	adrp	x0, .LC334
	add	x0, x0, :lo12:.LC334
	bl	printk
	add	x0, x29, 40
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L4575
	ldr	w1, [x29,40]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L4572
	ldr	w2, [x29,44]
	cmp	w2, 504
	bhi	.L4572
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	bne	.L4592
	ldr	x0, [x19,1624]
	add	x1, x29, 40
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1624]
	b	.L4611
.L4592:
	ldr	x0, [x19,1632]
	add	x1, x29, 40
	bl	memcpy
	ldr	x1, [x19,1632]
	mov	w0, 3
.L4611:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L4571
.L4561:
	bl	rknand_dev_flush
.L4610:
	mov	x19, 0
.L4571:
	adrp	x0, .LC335
	mov	x1, x19
	add	x0, x0, :lo12:.LC335
	bl	printk
	mov	x0, x19
	b	.L4556
.L4593:
	mov	x0, -22
.L4556:
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
	ldr	x1, [x19,1040]
	str	w0, [x19,1072]
	add	x0, x1, 512
	str	x0, [x19,1080]
	add	x0, x1, 1024
	str	x1, [x19,1096]
	str	x0, [x19,1624]
	add	x1, x1, 1536
	add	x0, x19, 1104
	str	x1, [x19,1632]
	strb	wzr, [x19,1060]
	str	xzr, [x19,1064]
	str	xzr, [x19,1640]
	bl	ftl_memcpy
	ldr	x0, [x19,1096]
	str	wzr, [x19,1088]
	str	wzr, [x19,1616]
	ldr	w1, [x0,16]
	ldr	w21, [x0,508]
	str	w1, [x19,1092]
	cbz	w21, .L4618
	mov	w1, 508
	bl	js_hash
	cmp	w21, w0
	beq	.L4618
	ldr	x0, [x19,1096]
	str	wzr, [x19,1092]
	str	wzr, [x0,16]
	adrp	x0, .LC336
	add	x0, x0, :lo12:.LC336
	bl	printk
.L4618:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0,1092]
	cbz	w1, .L4620
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,1088]
.L4620:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	ldr	x1, [x20,1624]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1632]
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
	str	x0, [x1,1648]
	cbz	x0, .L4626
	mov	w23, 0
	adrp	x24, .LC337
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC337
.L4630:
	add	x20, x21, :lo12:.LANCHOR5
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,1648]
	bl	FlashBootVendorRead
	cbnz	w0, .L4627
	ldr	x3, [x20,1648]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,1648]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L4628
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L4628
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L4628:
	cbnz	w22, .L4638
	mov	w22, 1
	b	.L4630
.L4638:
	cbz	w19, .L4631
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L4626
	b	.L4627
.L4631:
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
	b	.L4626
.L4627:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w19, -1
	ldr	x0, [x21,1648]
	bl	kfree
	str	xzr, [x21,1648]
.L4626:
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
	adrp	x3, .LANCHOR5+1648
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR5+1648]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L4640
	ldrh	w3, [x5,10]
	mov	x4, 0
.L4641:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L4645
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L4641
	add	x6, x5, w6, uxtw 3
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
	b	.L4640
.L4645:
	mov	w3, -1
.L4640:
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
	adrp	x0, .LANCHOR5+1648
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR5+1648]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L4647
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L4648:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L4663
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L4648
	add	x21, x19, w4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L4649
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L4647
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L4650:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L4664
	add	x3, x19, x3, lsl 3
	add	w25, w4, 1
	add	x1, x19, w25, uxtw 3
	ldrh	w0, [x1,16]
	str	x6, [x29,96]
	strh	w0, [x3,16]
	str	x5, [x29,104]
	ldrh	w0, [x1,20]
	strh	w0, [x3,20]
	strh	w21, [x3,18]
	add	x0, x5, w21, uxtw
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
	b	.L4650
.L4664:
	add	x3, x19, x3, lsl 3
	uxth	w21, w21
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, w21, uxth
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
	b	.L4662
.L4649:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L4662
.L4663:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L4647
	add	x3, x19, w3, uxth 3
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
.L4662:
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
.L4647:
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
	cbz	x0, .L4666
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4668
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L4669
	b	.L4680
.L4668:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4680
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L4671
.L4672:
	mov	x20, -1
	b	.L4667
.L4671:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L4672
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L4667
.L4669:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L4680
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L4672
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L4672
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L4680
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L4667
.L4680:
	mov	x20, -14
.L4667:
	mov	x0, x19
	bl	kfree
.L4666:
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
	.type	__func__.39624, %object
	.size	__func__.39624, 18
__func__.39624:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.39649, %object
	.size	__func__.39649, 23
__func__.39649:
	.string	"_list_update_data_list"
	.zero	1
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
	.type	__func__.20817, %object
	.size	__func__.20817, 22
__func__.20817:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.20903, %object
	.size	__func__.20903, 11
__func__.20903:
	.string	"nandc_init"
	.zero	5
	.type	__func__.20290, %object
	.size	__func__.20290, 28
__func__.20290:
	.string	"flash_wait_device_ready_raw"
	.zero	4
	.type	__func__.20354, %object
	.size	__func__.20354, 22
__func__.20354:
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
	.type	__func__.20379, %object
	.size	__func__.20379, 23
__func__.20379:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.20265, %object
	.size	__func__.20265, 26
__func__.20265:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.20276, %object
	.size	__func__.20276, 21
__func__.20276:
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
	.type	__func__.21082, %object
	.size	__func__.21082, 13
__func__.21082:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.21095, %object
	.size	__func__.21095, 10
__func__.21095:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.21109, %object
	.size	__func__.21109, 16
__func__.21109:
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
	.type	__func__.39157, %object
	.size	__func__.39157, 12
__func__.39157:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.39249, %object
	.size	__func__.39249, 19
__func__.39249:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.39855, %object
	.size	__func__.39855, 18
__func__.39855:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.39865, %object
	.size	__func__.39865, 17
__func__.39865:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.39986, %object
	.size	__func__.39986, 23
__func__.39986:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.40026, %object
	.size	__func__.40026, 22
__func__.40026:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.40037, %object
	.size	__func__.40037, 22
__func__.40037:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.39306, %object
	.size	__func__.39306, 16
__func__.39306:
	.string	"gc_free_src_blk"
	.type	__func__.38900, %object
	.size	__func__.38900, 14
__func__.38900:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.38945, %object
	.size	__func__.38945, 18
__func__.38945:
	.string	"zftl_add_read_buf"
	.align	3
.LANCHOR4 = . + 0
	.type	__func__.40459, %object
	.size	__func__.40459, 21
__func__.40459:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.20807, %object
	.size	__func__.20807, 20
__func__.20807:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.38832, %object
	.size	__func__.38832, 17
__func__.38832:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.39724, %object
	.size	__func__.39724, 20
__func__.39724:
	.string	"zftl_sblk_list_init"
	.zero	4
	.type	__func__.40391, %object
	.size	__func__.40391, 13
__func__.40391:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.21145, %object
	.size	__func__.21145, 21
__func__.21145:
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
	.type	__func__.20188, %object
	.size	__func__.20188, 19
__func__.20188:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.20314, %object
	.size	__func__.20314, 26
__func__.20314:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.20325, %object
	.size	__func__.20325, 29
__func__.20325:
	.string	"flash_start_3d_mlc_page_prog"
	.zero	3
	.type	__func__.20343, %object
	.size	__func__.20343, 22
__func__.20343:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.20367, %object
	.size	__func__.20367, 31
__func__.20367:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.20392, %object
	.size	__func__.20392, 25
__func__.20392:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.40224, %object
	.size	__func__.40224, 31
__func__.40224:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.40278, %object
	.size	__func__.40278, 15
__func__.40278:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.40305, %object
	.size	__func__.40305, 15
__func__.40305:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.39204, %object
	.size	__func__.39204, 21
__func__.39204:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.20249, %object
	.size	__func__.20249, 19
__func__.20249:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.40101, %object
	.size	__func__.40101, 14
__func__.40101:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.39758, %object
	.size	__func__.39758, 15
__func__.39758:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.40063, %object
	.size	__func__.40063, 19
__func__.40063:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.40075, %object
	.size	__func__.40075, 18
__func__.40075:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.39813, %object
	.size	__func__.39813, 15
__func__.39813:
	.string	"ftl_alloc_sblk"
	.zero	1
	.type	__func__.40431, %object
	.size	__func__.40431, 17
__func__.40431:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.40441, %object
	.size	__func__.40441, 14
__func__.40441:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.21167, %object
	.size	__func__.21167, 17
__func__.21167:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.21130, %object
	.size	__func__.21130, 20
__func__.21130:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.20862, %object
	.size	__func__.20862, 16
__func__.20862:
	.string	"nand_flash_init"
	.type	__func__.39928, %object
	.size	__func__.39928, 16
__func__.39928:
	.string	"ftl_sysblk_dump"
	.type	__func__.39953, %object
	.size	__func__.39953, 23
__func__.39953:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.40375, %object
	.size	__func__.40375, 16
__func__.40375:
	.string	"load_l2p_region"
	.type	__func__.40415, %object
	.size	__func__.40415, 6
__func__.40415:
	.string	"pm_gc"
	.zero	2
	.type	__func__.39147, %object
	.size	__func__.39147, 12
__func__.39147:
	.string	"gc_recovery"
	.zero	4
	.type	__func__.39236, %object
	.size	__func__.39236, 22
__func__.39236:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.39350, %object
	.size	__func__.39350, 16
__func__.39350:
	.string	"gc_scan_src_blk"
	.type	__func__.39411, %object
	.size	__func__.39411, 20
__func__.39411:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.39474, %object
	.size	__func__.39474, 18
__func__.39474:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.39904, %object
	.size	__func__.39904, 14
__func__.39904:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.38978, %object
	.size	__func__.38978, 10
__func__.38978:
	.string	"zftl_read"
	.zero	6
	.type	__func__.40007, %object
	.size	__func__.40007, 19
__func__.40007:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.38882, %object
	.size	__func__.38882, 17
__func__.38882:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.39279, %object
	.size	__func__.39279, 16
__func__.39279:
	.string	"gc_do_copy_back"
	.type	__func__.39529, %object
	.size	__func__.39529, 11
__func__.39529:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.39010, %object
	.size	__func__.39010, 13
__func__.39010:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.24 20210107"
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
	.string	"slc mode = %x %x %x\n"
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
	.string	"dump_sblk_queue: %d\n"
.LC124:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC125:
	.string	"%s %d %d\n"
.LC126:
	.string	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC127:
	.string	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC128:
	.string	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC129:
	.string	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC130:
	.string	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC131:
	.string	"gc_static_wearleveling: slc blk: %x, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC132:
	.string	"gc_static_wearleveling: min slc ec: %x, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC133:
	.string	"swl add tlc gc = %x, %d, %d, %d, %d, %d\n"
.LC134:
	.string	"swl add slc gc  = %x, %d, %d, %d, %d, %d\n"
.LC135:
	.string	"free blk vpn error: %x %x\n"
.LC136:
	.string	"GC PM block %x %x %x %d\n"
.LC137:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC138:
	.string	"...%d @ %s\n"
.LC139:
	.string	"rk_ftl_de_init %x\n"
.LC140:
	.string	"\0013"
.LC141:
	.string	"otp error! %d"
.LC142:
	.string	"rr"
.LC143:
	.string	"flash_abort_clear = %d\n"
.LC144:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC145:
	.string	"%d flReg.d32=%x %x\n"
.LC146:
	.string	"nandc:"
.LC147:
	.string	"nandc_xfer_done read error %x\n"
.LC148:
	.string	"dqs data abort %x\n"
.LC149:
	.string	"dqs data timeout %x\n"
.LC150:
	.string	"xfer error %x\n"
.LC151:
	.string	"MT %d row=%x,last status %d,status = %d\n"
.LC152:
	.string	"MT RR %d row=%x,count %d,status=%d\n"
.LC153:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC154:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC155:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC156:
	.string	"YMTC RR %d row=%x,count %d,status=%d\n"
.LC157:
	.string	"samsung SRR %d row=%x, status=%d\n"
.LC158:
	.string	"samsung TRR %d row=%x, status=%d\n"
.LC159:
	.string	"samsung RR %d row=%x,count %d,status=%d\n"
.LC160:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC161:
	.string	"%d flash_ddr_tuning_read %x ecc=%d\n"
.LC162:
	.string	"sync para %d\n"
.LC163:
	.string	"DDR mode Read error %x %x\n"
.LC164:
	.string	"SDR mode Read %x %x ecc:%x\n"
.LC165:
	.string	"flash_read_page_en %x %x %x %x\n"
.LC166:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC167:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC168:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC169:
	.string	"flash_prog_page %x %x %x\n"
.LC170:
	.string	"ymtc_flash_tlc_page_prog page_addr = %x status = %x\n"
.LC171:
	.string	"sblk_mlc_dump_prog wl_addr= %x ppa = %x ppa = %x\n"
.LC172:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC173:
	.string	"read: %x %x %x %x\n"
.LC174:
	.string	"0set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC175:
	.string	"prog end %x %x error_ecc %d %d\n"
.LC176:
	.string	"1set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC177:
	.string	"dp prog end %x %x error_ecc %d %d\n"
.LC178:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC179:
	.string	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC180:
	.string	"flash_prog_page_en:%x %x %x\n"
.LC181:
	.string	"w d:"
.LC182:
	.string	"w s:"
.LC183:
	.string	"spare"
.LC184:
	.string	"data"
.LC185:
	.string	"write error: %x\n"
.LC186:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC187:
	.string	"%d %x @%d %x\n"
.LC188:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC189:
	.string	"ftl info hash %x error\n"
.LC190:
	.string	"ink flag: %x\n"
.LC191:
	.string	"%s %d %d %x %x\n"
.LC192:
	.string	"ext info hash %x error\n"
.LC193:
	.string	"%s %x %x %x\n"
.LC194:
	.string	"ftl_sblk_dump_write = %x %d %d %d %d\n"
.LC195:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC196:
	.string	"ftl_sblk_dump_write2 = %x %d %d %d\n"
.LC197:
	.string	"ftl_sblk_dump_write = %x %x\n"
.LC198:
	.string	"ftl_sblk_dump_write done = %x\n"
.LC199:
	.string	"%x: ink_scaned_blk_num %x\n"
.LC200:
	.string	"ftl_ink_check_sblk = %x %d %d\n"
.LC201:
	.string	"ftl_ink_check_sblk = %x %d %d end\n"
.LC202:
	.string	"alloc sblk %x %d\n"
.LC203:
	.string	"blk %x is bad block\n"
.LC204:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC205:
	.string	"pm_write_page write error: %x\n"
.LC206:
	.string	"finfo:"
.LC207:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC208:
	.string	"sys_info_flush error:%x\n"
.LC209:
	.string	"...%d @ %s %d %p\n"
.LC210:
	.string	"no sys info %x\n"
.LC211:
	.string	"l2p:"
.LC212:
	.string	"saved_active_page  = %x\n"
.LC213:
	.string	"saved_active_plane = %x\n"
.LC214:
	.string	"sblk = %x\n"
.LC215:
	.string	"phy_blk = %x %x\n"
.LC216:
	.string	"num_planes = %x\n"
.LC217:
	.string	"recovery blk=%x, page=%x, ppa = %x, status = %x, hash:%x\n"
.LC218:
	.string	"data:"
.LC219:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC220:
	.string	"dump_write_lpa = %x %x %x %x\n"
.LC221:
	.string	"dump write new ppa = %x, last ppa = %x lpa = %x\n"
.LC222:
	.string	"dump write = %x %x %x\n"
.LC223:
	.string	"dump write hash update = %x %x %x\n"
.LC224:
	.string	"free_buf_count: %d\n"
.LC225:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC226:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC227:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC228:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC229:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC230:
	.string	"lpa:"
.LC231:
	.string	"vpn:"
.LC232:
	.string	"sblk:"
.LC233:
	.string	"lpa_hash:"
.LC234:
	.string	"lpa_hash_index:"
.LC235:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC236:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC237:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC238:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC239:
	.string	"pm_ppa:"
.LC240:
	.string	"spare:"
.LC241:
	.string	"pm_init posr %x %x %x\n"
.LC242:
	.string	"pm_init recovery %x %x %x\n"
.LC243:
	.string	"pm_init hash %x error\n"
.LC244:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC245:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC246:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC247:
	.string	"gc_recovery: %x vpn = %x\n"
.LC248:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC249:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC250:
	.string	"lpa: %x %x %x\n"
.LC251:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC252:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC253:
	.string	"js hash error:%x %x %x\n"
.LC254:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC255:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC256:
	.string	"ftl_sblk_dump = %x %d %d %d %d\n"
.LC257:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC258:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC259:
	.string	"index= %x, lpa=%x\n"
.LC260:
	.string	"block = %x, vpn=%x check vpn = %x\n"
.LC261:
	.string	"ftl_read %x %x %x\n"
.LC262:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC263:
	.string	"id=%d, status = %x, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC264:
	.string	"zftl debug cmd: %s\n"
.LC265:
	.string	"cmd:"
.LC266:
	.string	"dumpl2p"
.LC267:
	.string	"pm l2p:"
.LC268:
	.string	"pm blk:"
.LC269:
	.string	"dumppm:"
.LC270:
	.string	"p_cmd: %s\n"
.LC271:
	.string	"pm ram = %x, %x\n"
.LC272:
	.string	"ram:"
.LC273:
	.string	"pm:"
.LC274:
	.string	"dumpsys"
.LC275:
	.string	"dumplist:"
.LC276:
	.string	"vpncheck"
.LC277:
	.string	"dumpppa:"
.LC278:
	.string	"dumpblk:"
.LC279:
	.string	"setzdebug:"
.LC280:
	.string	"lpa2ppa:"
.LC281:
	.string	"lpa: %x--> ppa: %x\n"
.LC282:
	.string	"help:\n"
.LC283:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC284:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC285:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC286:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC287:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC288:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC289:
	.string	"7. echo dumplist:x > /proc/zftl_debug\n"
.LC290:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC291:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC292:
	.string	"ftl_update_l2p_map"
.LC293:
	.string	"lpa_tbl:"
.LC294:
	.string	"sblk %x vpn: %d %d\n"
.LC295:
	.string	"error gc_add_sblk: %x\n"
.LC296:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC297:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC298:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC299:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC300:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC301:
	.string	"gc free %x, %d\n"
.LC302:
	.string	"_c_user_data_density := %d\n"
.LC303:
	.string	"_c_totle_phy_density := %d\n"
.LC304:
	.string	"_c_totle_log_page := %d\n"
.LC305:
	.string	"_c_totle_data_density := %d\n"
.LC306:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC307:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC308:
	.string	"_c_max_pm_sblk := %d\n"
.LC309:
	.string	"_min_slc_super_block := %d\n"
.LC310:
	.string	"_max_xlc_super_block := %d\n"
.LC311:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC312:
	.string	"flash info size: %d %d %d\n"
.LC313:
	.string	"ftl_init %x\n"
.LC314:
	.string	"ftlwrite %x %x %x %x\n"
.LC315:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC316:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC317:
	.string	"write_idblock fix data %x %x\n"
.LC318:
	.string	"idblk:"
.LC319:
	.string	"write_idblock totle_sec %x %x\n"
.LC320:
	.string	"prog page: %x %x %x, %p %x %x %x\n"
.LC321:
	.string	"read page: %x %x %x %x\n"
.LC322:
	.string	"wl_lba %p %x %x %x\n"
.LC323:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC324:
	.string	"rk_copy_from_user error\n"
.LC325:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC326:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC327:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC328:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC329:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC330:
	.string	"RKNAND_LOADER_STATUS\n"
.LC331:
	.string	"RKNAND_LOADER_LOCK\n"
.LC332:
	.string	"LockKey not match %d\n"
.LC333:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC334:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC335:
	.string	"return ret = %lx\n"
.LC336:
	.string	"secureBootEn check error\n"
.LC337:
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
	.type	gp_flash_info, %object
	.size	gp_flash_info, 8
gp_flash_info:
	.zero	8
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.zero	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.zero	1
	.zero	6
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
	.zero	3
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
	.size	g_gc_info, 2216
g_gc_info:
	.zero	2216
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
	.type	gp_data_slc_data_head, %object
	.size	gp_data_slc_data_head, 8
gp_data_slc_data_head:
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
	.zero	2
	.type	gp_data_slc_cache_head, %object
	.size	gp_data_slc_cache_head, 8
gp_data_slc_cache_head:
	.zero	8
	.type	gp_data_xlc_data_head, %object
	.size	gp_data_xlc_data_head, 8
gp_data_xlc_data_head:
	.zero	8
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
	.zero	23
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
	.type	pm_gc_enable, %object
	.size	pm_gc_enable, 4
pm_gc_enable:
	.zero	4
	.zero	4
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
