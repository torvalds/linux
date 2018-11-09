/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-11-08
 * function: rk ftl v5 for rockchip soc base on arm v8 to support 3D/2D
 *	     TLC and MLC.
 */
	.cpu generic+fp+simd
	.file	"rk_zftl_arm64.S"
#APP
	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17\
		,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31

	.macro	mrs_s, rt, sreg
	.inst	0xd5200000|(\sreg)|(.L__reg_num_\rt)
	.endm

	.macro	msr_s, sreg, rt
	.inst	0xd5000000|(\sreg)|(.L__reg_num_\rt)
	.endm

#NO_APP
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
	adrp	x5, .LANCHOR0
	add	x10, x5, :lo12:.LANCHOR0
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
	add	x6, x14, x8
	strh	w2, [x6,2]
	strh	w2, [x14,x8]
	ldr	x2, [x0]
	cbz	x2, .L81
	uxtw	x9, w1
	ldr	x16, [x10,600]
	ldr	x12, [x10,584]
	ldrh	w11, [x6,4]
	ldrh	w4, [x16,x9,lsl 1]
	lsl	x9, x9, 2
	ldrh	w17, [x10,592]
	ldr	w3, [x12,x9]
	mul	w4, w4, w11
	ubfx	x7, x3, 11, 8
	ldrh	w3, [x12,x9]
	and	w3, w3, 2047
	cbz	w4, .L65
	madd	w3, w17, w7, w3
	add	w4, w4, w3, uxth
.L65:
	add	x7, x5, :lo12:.LANCHOR0
	mov	x9, -6148914691236517206
	cmp	w11, wzr
	mov	w20, 65535
	csinv	w4, w4, wzr, ne
	mov	w21, 6
	ldr	x18, [x7,528]
	ldrh	w19, [x7,576]
	mov	w7, 0
	sub	x3, x2, x18
	asr	x3, x3, 1
	madd	x3, x9, x3, x3
	uxth	w3, w3
.L72:
	add	w7, w7, 1
	cmp	w1, w3
	uxth	w7, w7
	beq	.L61
	cmp	w7, w19
	bhi	.L61
	uxtw	x11, w3
	ldrh	w15, [x2,4]
	ldrh	w13, [x16,x11,lsl 1]
	lsl	x11, x11, 2
	ldr	w9, [x12,x11]
	mul	w13, w13, w15
	ubfx	x10, x9, 11, 8
	ldrh	w9, [x12,x11]
	and	w9, w9, 2047
	cbz	w13, .L68
	mov	w11, -1
	cbz	w15, .L69
	madd	w9, w17, w10, w9
	add	w11, w13, w9, uxth
	b	.L69
.L68:
	cbnz	w15, .L74
	mov	w11, -1
.L69:
	cmp	w11, w4
	bhi	.L70
.L74:
	ldrh	w9, [x2]
	cmp	w9, w20
	bne	.L71
	strh	w3, [x6,2]
	strh	w1, [x2]
	b	.L61
.L71:
	umull	x2, w9, w21
	mov	w3, w9
	add	x2, x18, x2
	b	.L72
.L70:
	ldrh	w4, [x2,2]
	strh	w4, [x6,2]
	strh	w3, [x14,x8]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L73
	strh	w1, [x2,2]
.L81:
	str	x6, [x0]
	b	.L61
.L73:
	ldrh	w0, [x2,2]
	add	x5, x5, :lo12:.LANCHOR0
	mov	w3, 6
	umull	x0, w0, w3
	ldr	x3, [x5,528]
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L61:
	ldp	x19, x20, [sp]
	ldr	x21, [sp,16]
	add	sp, sp, 32
	ret
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_get_gc_head_node.isra.5, %function
_list_get_gc_head_node.isra.5:
	uxth	w1, w1
	mov	w2, 65535
	cbz	x0, .L88
	adrp	x2, .LANCHOR0+528
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x2,#:lo12:.LANCHOR0+528]
.L84:
	cbz	w1, .L85
	ldrh	w2, [x0]
	cmp	w2, w4
	beq	.L88
	umull	x2, w2, w5
	sub	w1, w1, #1
	add	x0, x3, x2
	uxth	w1, w1
	b	.L84
.L85:
	sub	x0, x0, x3
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	madd	x0, x1, x0, x0
	uxth	w2, w0
.L88:
	mov	w0, w2
	ret
	.size	_list_get_gc_head_node.isra.5, .-_list_get_gc_head_node.isra.5
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
	beq	.L89
	ldrh	w1, [x3,48]
	cmp	w1, w19
	beq	.L89
	ldrh	w1, [x3,80]
	cmp	w1, w19
	beq	.L89
	mov	w23, 6
	ldr	x25, [x4,528]
	ldr	x1, [x0]
	umull	x23, w19, w23
	add	x24, x25, x23
	cmp	x24, x1
	beq	.L89
	mov	x21, x0
	ldrh	w0, [x24,2]
	mov	w1, 65535
	mov	x22, x2
	cmp	w0, w1
	bne	.L93
	ldrh	w1, [x25,x23]
	cmp	w1, w0
	bne	.L93
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 255
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L93:
	ldrh	w3, [x24,2]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L94
	ldrh	w0, [x25,x23]
	cmp	w0, w3
	beq	.L89
.L94:
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
	bcs	.L89
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_list_remove_node
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_insert_data_list
.L89:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	_list_update_data_list, .-_list_update_data_list
	.align	2
	.type	zftl_get_gc_node.part.12, %function
zftl_get_gc_node.part.12:
	uxth	w1, w0
	adrp	x0, .LANCHOR0+616
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR0+616]
	bl	_list_get_gc_head_node.isra.5
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_get_gc_node.part.12, .-zftl_get_gc_node.part.12
	.section	.text.unlikely
	.align	2
	.type	rk_simple_strtoull.constprop.29, %function
rk_simple_strtoull.constprop.29:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	mov	x20, x0
	stp	x21, x22, [sp,32]
	mov	x23, x1
	ldrb	w0, [x0]
	cmp	w0, 48
	bne	.L111
	ldrb	w0, [x20,1]
	add	x19, x20, 1
	mov	w21, 8
	orr	w0, w0, 32
	uxtb	w0, w0
	cmp	w0, 120
	bne	.L101
	ldrb	w0, [x20,2]
	bl	isxdigit
	cbz	w0, .L101
	add	x19, x20, 2
	mov	w21, 16
	b	.L101
.L111:
	mov	x19, x20
	mov	w21, 10
.L101:
	mov	w22, 0
.L102:
	ldrb	w20, [x19]
	mov	w0, w20
	bl	isxdigit
	cbnz	w0, .L103
.L109:
	cbz	x23, .L115
	str	x19, [x23]
	b	.L115
.L103:
	sub	w2, w20, #48
	cmp	w2, 9
	bhi	.L116
.L110:
	cmp	w2, w21
	bcs	.L109
	madd	w22, w22, w21, w2
	add	x19, x19, 1
	b	.L102
.L116:
	orr	w20, w20, 32
	sub	w2, w20, #87
	b	.L110
.L115:
	mov	w0, w22
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	rk_simple_strtoull.constprop.29, .-rk_simple_strtoull.constprop.29
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
	tbz	x0, 12, .L118
	adrp	x1, .LANCHOR1
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L118:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L119
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
.L119:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L120
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,8]
	bl	printk
.L120:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L121
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrb	w1, [x1,9]
	bl	printk
.L121:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L122
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrh	w1, [x1,10]
	bl	printk
.L122:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L123
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,12]
	bl	printk
.L123:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L124
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,13]
	bl	printk
.L124:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L125
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrh	w1, [x1,14]
	bl	printk
.L125:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L126
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	ldrb	w1, [x1,23]
	bl	printk
.L126:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L127
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,18]
	bl	printk
.L127:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L128
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,19]
	bl	printk
.L128:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrb	w1, [x1,20]
	bl	printk
.L129:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x1, .LANCHOR0+632
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w1, [x1,#:lo12:.LANCHOR0+632]
	bl	printk
.L130:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x1, .LANCHOR0
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	bl	printk
.L131:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L132
	adrp	x0, .LANCHOR0+624
	ldr	x1, [x0,#:lo12:.LANCHOR0+624]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ldrb	w1, [x1,22]
	bl	printk
.L132:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L133
	adrp	x1, .LANCHOR0+633
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	ldrb	w1, [x1,#:lo12:.LANCHOR0+633]
	bl	printk
.L133:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L134
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	and	w1, w1, 1
	bl	printk
.L134:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L135
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 1, 1
	bl	printk
.L135:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L136
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 2, 1
	bl	printk
.L136:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L137
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 3, 1
	bl	printk
.L137:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L138
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 4, 1
	bl	printk
.L138:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L139
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 5, 1
	bl	printk
.L139:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L140
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 6, 1
	bl	printk
.L140:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L141
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	ubfx	x1, x1, 7, 1
	bl	printk
.L141:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L142
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrb	w1, [x0,17]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	and	w1, w1, 1
	bl	printk
.L142:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L143
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 9, 1
	bl	printk
.L143:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L144
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrh	w1, [x0,16]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	ubfx	x1, x1, 10, 1
	bl	printk
.L144:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L145
	adrp	x1, .LANCHOR0
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	bl	printk
.L145:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L146
	adrp	x2, .LANCHOR0
	adrp	x0, .LC31
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC31
	add	x2, x2, 640
	ldrb	w1, [x2,10]
	ldrb	w2, [x2,11]
	bl	printk
.L146:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L147
	adrp	x2, .LANCHOR0
	adrp	x0, .LC32
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC32
	add	x2, x2, 640
	ldrb	w1, [x2,8]
	ldrb	w2, [x2,9]
	bl	printk
.L147:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L117
	adrp	x1, .LANCHOR0+672
	adrp	x0, .LC33
	add	x0, x0, :lo12:.LC33
	ldrb	w1, [x1,#:lo12:.LANCHOR0+672]
	bl	printk
.L117:
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
	tbz	x0, 12, .L243
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 72
	mov	x2, x22
	bl	printk
.L243:
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
	bne	.L244
	mov	w1, 8
	strb	w1, [x0,516]
.L244:
	ldr	w1, [x22,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L245
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 9
	strb	w1, [x0,516]
.L245:
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19,516]
	cmp	w0, 9
	bne	.L246
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
	tbz	x1, 12, .L248
	ldr	w1, [x0]
	ldr	w2, [x0,8]
	ldr	w3, [x0,80]
	ldr	w4, [x0,84]
	ldr	w5, [x0,88]
	b	.L260
.L246:
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
	tbz	x0, 12, .L248
	ldr	x0, [x19,536]
	ldr	w1, [x0]
	ldr	w2, [x0,344]
	ldr	w3, [x0,304]
	ldr	w4, [x0,308]
	ldr	w5, [x0,312]
.L260:
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L248:
	add	x1, x20, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x1,720]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	strh	wzr, [x1,722]
	strb	wzr, [x1,724]
	tbz	x0, 12, .L242
	adrp	x0, .LC36
	ldrb	w1, [x1,516]
	add	x0, x0, :lo12:.LC36
	bl	printk
.L242:
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
.L262:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L262
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
.L266:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,726]
	cmp	w0, w20
	bls	.L272
	add	x0, x19, 88
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,725]
	cmp	w0, 34
	bne	.L267
	ldrsb	w0, [x24,x20]
	b	.L271
.L267:
	cmp	w0, 35
	bne	.L269
	ldrsb	w0, [x21,x20]
	b	.L271
.L269:
	ldrsb	w0, [x23,232]
.L271:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L266
.L272:
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
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	uxtb	w1, w1
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	x20, 160
	mov	x21, 0
	ldr	x4, [x3,728]
	ldr	x19, [x3,536]
	add	x23, x4, 128
	ldrb	w22, [x4,113]
	mul	w2, w1, w22
	sxtw	x1, w0
	ubfiz	x0, x0, 8, 8
	add	x19, x19, x0
	mov	w0, 54
	mul	x20, x1, x20
	add	x1, x20, x2, sxtw
	add	x20, x4, x1
	add	x20, x20, 144
	str	w0, [x19,2056]
.L274:
	cmp	w22, w21, uxtb
	bls	.L276
	ldrb	w0, [x23,x21]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x20,x21]
	add	x21, x21, 1
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
	cmp	w1, 6
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
	mov	w2, 612
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
	stp	x29, x30, [sp, -16]!
	ubfx	x3, x0, 24, 2
	mov	w2, w1
	add	x29, sp, 0
	and	w1, w0, 2097151
	ubfx	x6, x0, 21, 3
	cbnz	w3, .L303
	adrp	x3, .LANCHOR0
	add	x0, x3, :lo12:.LANCHOR0
	ldrb	w4, [x3,#:lo12:.LANCHOR0]
	cbz	w4, .L304
	ldrb	w0, [x0,1]
	cbz	w0, .L303
.L304:
	add	x3, x3, :lo12:.LANCHOR0
	ldrh	w5, [x3,2]
	ldrb	w0, [x3,1]
	udiv	w4, w1, w5
	mul	w5, w4, w5
	sub	w4, w1, w5
	add	w1, w5, w4, lsl 1
	cbnz	w0, .L303
	add	x3, x3, 4
	ldrh	w3, [x3,w4,uxtw 1]
	add	w1, w3, w5
.L303:
	mov	w0, w6
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
	ldrb	w2, [x1,744]
	cbz	w2, .L317
	strb	wzr, [x1,744]
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
	ldrb	w2, [x1,744]
	cbz	w2, .L317
	strb	wzr, [x1,744]
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
	str	wzr, [x19,2048]
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 218
	str	w0, [x19,2056]
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
	ldrb	w1, [x20,744]
	cbnz	w1, .L335
	adrp	x1, .LANCHOR2
	mov	w3, 4
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 8
	ldrb	w2, [x1,12]
	ldrb	w1, [x1,30]
	cmp	w2, 2
	csel	w2, w2, w3, eq
	strb	w2, [x20,744]
	cbz	w1, .L335
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w1, [x0,8]
	b	.L335
.L340:
	cmp	w1, 3
	bne	.L335
	add	x20, x20, x0, sxtw
	ldrb	w21, [x20,744]
	cbnz	w21, .L335
	adrp	x1, .LANCHOR2+20
	mov	w2, 4
	ldrb	w1, [x1,#:lo12:.LANCHOR2+20]
	cmp	w1, 2
	csel	w1, w1, w2, eq
	strb	w1, [x20,744]
	ubfiz	x1, x0, 8, 8
	mov	w0, 239
	add	x19, x19, x1
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	w0, 50
	bl	timer_delay_ns
	ldrb	w0, [x20,744]
	str	w0, [x19,2048]
	mov	w0, 1
	str	w0, [x19,2048]
	str	w21, [x19,2048]
	str	w21, [x19,2048]
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 223
	str	w0, [x19,2056]
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
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	ubfx	x19, x1, 21, 3
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	and	w21, w1, 2097151
	ubfx	x22, x1, 24, 2
	ldrb	w0, [x0,633]
	cmp	w0, w19
	bhi	.L354
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 754
	add	x1, x1, 272
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L354:
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x20, x19, sxtw
	ldrb	w19, [x19,736]
	mov	w0, w19
	ldr	x24, [x20,536]
	bl	nandc_cs
	cbnz	w22, .L355
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L356
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	b	.L356
.L355:
	ldr	x0, [x20,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L357
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x24, x0, lsl 8
	str	w22, [x0,8]
	b	.L356
.L357:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L356:
	ubfiz	x19, x19, 8, 8
	and	w0, w21, 255
	add	x1, x24, x19
	str	wzr, [x1,2056]
	str	wzr, [x1,2052]
	str	wzr, [x1,2052]
	str	w0, [x1,2052]
	lsr	w0, w21, 8
	str	w0, [x1,2052]
	lsr	w21, w21, 16
	str	w21, [x1,2052]
	mov	w0, 0
	str	w23, [x1,2056]
	bl	nandc_de_cs
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
.L363:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L364
	mov	w0, 10
	bl	timer_delay_ns
	subs	w19, w19, #1
	bne	.L363
	mov	w0, -1
	b	.L362
.L364:
	mov	w0, 0
.L362:
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
.L367:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,726]
	cmp	w4, w2
	bls	.L372
	ldrb	w3, [x3,725]
	cmp	w3, 67
	bne	.L368
	ldrsb	w3, [x0,x2]
	b	.L371
.L368:
	ldrsb	w3, [x1,x2]
.L371:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L367
.L372:
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
	cbz	w2, .L376
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
	b	.L377
.L376:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 376
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L377:
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
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	and	w21, w1, 2097151
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ubfx	x19, x0, 21, 3
	and	w23, w0, 2097151
	ubfx	x20, x0, 24, 2
	add	x0, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrb	w0, [x0,633]
	cmp	w0, w19
	bhi	.L379
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 849
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L379:
	add	x26, x22, :lo12:.LANCHOR0
	add	x19, x26, x19, sxtw
	ldrb	w24, [x19,736]
	mov	w0, w24
	ldr	x25, [x26,536]
	bl	nandc_cs
	cbnz	w20, .L380
	mov	w0, w23
	bl	slc_phy_page_address_calc
	mov	w23, w0
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L381
	mov	w0, w24
	bl	zftl_flash_enter_slc_mode
	b	.L381
.L380:
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L382
	sxtw	x0, w24
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w20, [x0,8]
	b	.L381
.L382:
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
.L381:
	add	x26, x22, :lo12:.LANCHOR0
	and	w2, w23, 255
	add	x0, x26, 640
	and	w28, w21, 255
	lsr	w27, w21, 8
	uxtb	x19, w24
	ldrb	w1, [x0,16]
	lsr	w21, w21, 16
	cmp	w1, 1
	lsr	w1, w23, 8
	lsr	w23, w23, 16
	bne	.L383
	add	x19, x25, x19, lsl 8
	ldrb	w3, [x0,8]
	ldrb	w0, [x0,9]
	str	w3, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	str	w23, [x19,2052]
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	cbz	w20, .L384
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L384
	str	w20, [x19,2056]
.L384:
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	b	.L402
.L383:
	add	x19, x25, x19, lsl 8
	ldrb	w0, [x0,8]
	str	w0, [x19,2056]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	str	w23, [x19,2052]
	cbz	w20, .L386
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L386
	str	w20, [x19,2056]
.L386:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,649]
	str	w0, [x19,2056]
.L402:
	str	w28, [x19,2052]
	mov	w0, 48
	str	w27, [x19,2052]
	str	w21, [x19,2052]
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
	adrp	x22, .LANCHOR0
	mov	x21, 0
	stp	x23, x24, [sp,48]
	add	x22, x22, :lo12:.LANCHOR0
	adrp	x23, .LC38
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w25, w0
	add	x26, x22, 748
	adrp	x24, .LANCHOR2
	add	x23, x23, :lo12:.LC38
.L416:
	lsl	x0, x21, 3
	ldr	x20, [x22,536]
	ldrb	w19, [x0,x26]
	adrp	x0, .LANCHOR0
	cmp	w19, 152
	beq	.L404
	cmp	w19, 69
	beq	.L404
	cmp	w19, 44
	beq	.L404
	cmp	w19, 137
	bne	.L405
.L404:
	add	x0, x0, :lo12:.LANCHOR0
	cmp	w25, 1
	ldrb	w1, [x0,780]
	bne	.L407
	tbz	x1, 0, .L405
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L408
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	bl	printk
.L408:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L417
	cmp	w19, 44
	bne	.L409
.L417:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L436
.L409:
	mov	w0, 128
	str	w0, [x1,2052]
	mov	w0, 1
	b	.L436
.L407:
	tbz	x1, 2, .L405
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L412
	mov	x0, x23
	bl	printk
.L412:
	add	x1, x20, x21, lsl 8
	mov	w0, 239
	cmp	w19, 137
	str	w0, [x1,2056]
	beq	.L418
	cmp	w19, 44
	cset	w0, eq
	cbz	w0, .L413
.L418:
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L436
.L413:
	mov	w2, 128
	str	w2, [x1,2052]
.L436:
	str	w0, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L405:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L416
	bl	nandc_wait_flash_ready
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_set_interface_mode, .-flash_set_interface_mode
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
	bhi	.L439
	adrp	x0, .LC39
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC39
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L439:
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
	.global	sandisk_prog_test_bad_block
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LANCHOR0+536
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x2,#:lo12:.LANCHOR0+536]
	adrp	x2, .LANCHOR2
	add	x3, x2, :lo12:.LANCHOR2
	mov	x20, x2
	sxtw	x2, w0
	add	x2, x2, 8
	ldrb	w3, [x3,37]
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L452
	mov	w3, 162
.L452:
	ubfiz	x0, x0, 8, 8
	str	w3, [x2,8]
	add	x19, x19, x0
	mov	w0, 128
	str	x1, [x29,40]
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
	ands	w19, w2, w0
	beq	.L445
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	ldr	x1, [x29,40]
	tbz	x0, 12, .L445
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	bl	printk
.L445:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
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
	adrp	x1, .LANCHOR0
	mov	w3, 1
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,516]
	strb	w0, [x1,724]
	cmp	w2, 9
	ldr	x2, [x1,536]
	bne	.L455
	str	w3, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L456
	cmp	w0, 60
	mov	w1, 3
	beq	.L456
	cmp	w0, 40
	mov	w1, 2
	csel	w1, w1, w3, eq
.L456:
	lsl	w1, w1, 25
	orr	w1, w1, 1
	str	w1, [x2,32]
	b	.L454
.L455:
	str	w3, [x2,8]
	cmp	w0, 16
	mov	w1, 4096
	bne	.L458
.L461:
	and	w1, w1, -17
	b	.L459
.L458:
	cmp	w0, 24
	bne	.L460
	orr	w1, w1, 16
	b	.L459
.L460:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L461
.L459:
	orr	w1, w1, 1
	str	w1, [x2,12]
.L454:
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
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
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
	bne	.L469
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
	b	.L470
.L469:
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
.L470:
	add	x22, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x23, x22, 748
	mov	w24, 2
.L472:
	lsl	x0, x19, 3
	ldrb	w0, [x0,x23]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L471
	uxtb	w21, w19
	mov	w0, w21
	bl	flash_reset
	add	x0, x22, 744
	strb	w24, [x19,x0]
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
.L471:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L472
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,672]
	cbz	w0, .L473
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,560]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L473:
	adrp	x0, .LANCHOR2+28
	ldrb	w0, [x0,#:lo12:.LANCHOR2+28]
	bl	nandc_bch_sel
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	bne	.L480
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L481
.L480:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L481:
	ldr	w1, [x0]
	tbnz	x1, 9, .L482
	bl	wait_for_nand_flash_ready
	b	.L479
.L482:
	mov	w1, 1
	bl	nandc_irq_disable
.L479:
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	w25, w1
	mov	w22, w2
	ldrb	w0, [x0,633]
	mov	w24, w3
	cmp	w0, w19
	bhi	.L485
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 523
	add	x1, x1, 736
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L485:
	add	x0, x20, :lo12:.LANCHOR0
	add	x19, x0, x19, sxtw
	ldrb	w23, [x19,736]
	ldr	x19, [x0,536]
	adrp	x0, .LANCHOR2
	mov	x21, x23
	add	x23, x23, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x23, x19, x23, lsl 8
	tbz	x0, 4, .L486
	adrp	x0, .LC41
	mov	w1, w21
	add	x0, x0, :lo12:.LC41
	mov	w2, w22
	mov	w3, w24
	bl	printk
.L486:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L487
	bl	zftl_flash_enter_slc_mode
	b	.L488
.L487:
	bl	zftl_flash_exit_slc_mode
.L488:
	ubfiz	x4, x21, 8, 8
	mov	w0, 96
	add	x19, x19, x4
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w22, 255
	str	w0, [x19,2052]
	lsr	w0, w22, 8
	str	w0, [x19,2052]
	lsr	w0, w22, 16
	str	w0, [x19,2052]
	ldrb	w0, [x20,781]
	mov	w20, 0
	cbnz	w0, .L489
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	and	w20, w0, w20
.L489:
	mov	w0, 96
	str	w0, [x19,2056]
	and	w0, w24, 255
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w3, w24, 16
	str	w3, [x19,2052]
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w19, 5
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	mov	w2, w0
	mov	w0, w21
	str	x2, [x29,88]
	bl	nandc_de_cs
	ldr	x2, [x29,88]
	and	w19, w2, w19
	orr	w19, w19, w20
	cbz	w19, .L490
	adrp	x0, .LC42
	mov	w1, w22
	add	x0, x0, :lo12:.LC42
	bl	printk
.L490:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w22, w2
	uxtb	w26, w1
	ldrb	w0, [x0,633]
	and	w24, w2, 2097151
	cmp	w0, w23
	bhi	.L499
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 568
	add	x1, x1, 768
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L499:
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x0, x23, sxtw
	ldrb	w21, [x1,736]
	ldr	x25, [x0,536]
	adrp	x0, .LANCHOR2
	mov	x20, x21
	add	x21, x21, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x21, x25, x21, lsl 8
	tbz	x0, 4, .L500
	adrp	x0, .LC43
	mov	w1, w20
	add	x0, x0, :lo12:.LC43
	mov	w2, w22
	mov	w3, w26
	bl	printk
.L500:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w26, .L501
	bl	zftl_flash_enter_slc_mode
	b	.L502
.L501:
	bl	zftl_flash_exit_slc_mode
.L502:
	ubfiz	x3, x20, 8, 8
	mov	w0, 96
	add	x3, x25, x3
	str	w0, [x3,2056]
	and	w0, w22, 255
	str	w0, [x3,2052]
	lsr	w0, w24, 8
	str	w0, [x3,2052]
	lsr	w0, w24, 16
	str	w0, [x3,2052]
	mov	w0, 208
	str	w0, [x3,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x21
	bl	flash_read_status
	mov	w21, w0
	mov	w0, w20
	bl	nandc_de_cs
	mov	w3, 5
	ands	w20, w21, w3
	beq	.L503
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w1, w23
	mov	w3, w20
	ldrh	w2, [x19,2]
	udiv	w2, w22, w2
	bl	printk
.L503:
	mov	w0, w20
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
.L512:
	ldrb	w0, [x23,633]
	cmp	w0, w19
	bls	.L516
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,736]
	mov	w22, 0
.L513:
	cmp	w20, w22, uxth
	bls	.L517
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L513
.L517:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L512
.L516:
	adrp	x0, .LC45
	mov	w1, 0
	add	x0, x0, :lo12:.LC45
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x25, [sp,64]
	add	x21, x22, :lo12:.LANCHOR0
	str	x2, [x29,80]
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	uxtb	w24, w1
	ldr	x0, [x21,536]
	stp	x19, x20, [sp,16]
	str	x3, [x29,88]
	add	w20, w24, 1
	bl	rk_nandc_xfer_irq_flag_init
	ldrb	w0, [x21,516]
	ldr	x4, [x21,536]
	cmp	w0, 9
	ldr	x3, [x29,88]
	ldr	x2, [x29,80]
	bne	.L520
	ldr	w0, [x4,292]
	ubfiz	w19, w23, 1, 1
	orr	w19, w19, 8
	asr	w20, w20, 1
	orr	w0, w0, 1
	str	w0, [x4,292]
	ldr	w0, [x4,288]
	orr	w0, w0, 1
	str	w0, [x4,288]
	mov	w0, 1
	bfi	w19, w0, 5, 2
	ldrb	w0, [x21,673]
	orr	w19, w19, 536870912
	orr	w19, w19, 1024
	and	w19, w19, -17
	bfi	w19, w20, 22, 6
	orr	w19, w19, 128
	cbz	w0, .L522
	ldrb	w1, [x21,720]
	orr	w0, w19, 512
	cmp	w1, wzr
	csel	w19, w0, w19, ne
	b	.L522
.L520:
	ldr	w0, [x4,368]
	ubfiz	w19, w23, 1, 1
	orr	w19, w19, 8
	asr	w20, w20, 1
	orr	w0, w0, 1
	str	w0, [x4,368]
	ldr	w0, [x4,364]
	orr	w0, w0, 1
	str	w0, [x4,364]
	mov	w0, 16
	ldr	w25, [x4,12]
	bfi	w25, w0, 8, 8
	mov	w0, 1
	bfi	w19, w0, 5, 2
	and	w25, w25, -9
	orr	w19, w19, 536870912
	and	w25, w25, -225
	orr	w19, w19, 1024
	and	w19, w19, -17
	bfi	w19, w20, 22, 6
	cbz	w23, .L534
	ldrb	w0, [x21,724]
	ubfx	x24, x24, 1, 7
	mov	w1, 64
	mov	w4, 128
	cmp	w0, 25
	add	x24, x3, x24, lsl 2
	csel	w4, w1, w4, cc
	mov	w1, 0
	b	.L527
.L522:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	mov	x0, x2
	lsl	w1, w20, 10
	str	x2, [x22,688]
	mov	w2, w23
	str	x3, [x22,696]
	bl	rknand_dma_map_single
	str	w0, [x22,704]
	ldr	x0, [x22,696]
	lsl	w1, w20, 2
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x22,708]
	ldr	x2, [x22,536]
	mov	w1, 1
	str	w1, [x22,712]
	cmp	w23, wzr
	ldr	w1, [x22,704]
	str	w1, [x2,52]
	str	w0, [x2,56]
	mov	w0, 16
	ldr	w1, [x2,48]
	bfi	w1, w0, 9, 5
	mov	w0, 2
	orr	w1, w1, 448
	bfi	w1, w0, 3, 3
	cset	w0, eq
	orr	w1, w1, 4
	bfi	w1, w0, 1, 1
	ldrh	w0, [x22,722]
	orr	w1, w1, 1
	bfi	w1, w0, 16, 11
	str	w1, [x2,48]
	str	w19, [x2,16]
	orr	w19, w19, 4
	str	w19, [x2,16]
	b	.L519
.L527:
	cmp	x3, x24
	add	w0, w1, w4
	beq	.L529
	ldrh	w5, [x3,2]
	and	x1, x1, 4294967292
	ldr	x6, [x21,680]
	ldrh	w7, [x3],4
	orr	w5, w7, w5, lsl 16
	str	w5, [x6,x1]
	mov	w1, w0
	b	.L527
.L534:
	ldr	x1, [x21,680]
	str	w0, [x1]
.L529:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	lsl	w1, w20, 10
	ldr	x0, [x22,680]
	str	x2, [x22,688]
	str	x0, [x22,696]
	mov	x0, x2
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x22,704]
	ldr	x0, [x22,696]
	lsl	w1, w20, 7
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x22,708]
	ldr	x3, [x22,536]
	mov	w0, 1
	str	w0, [x22,712]
	cmp	w23, wzr
	ldr	w0, [x22,704]
	mov	w1, 8660
	str	w0, [x3,20]
	ldr	w0, [x22,708]
	str	w0, [x3,24]
	cset	w0, eq
	bfi	w1, w0, 1, 1
	orr	w1, w1, 1
	str	w1, [x3,16]
	str	w25, [x3,12]
	str	w19, [x3,8]
	orr	w19, w19, 4
	str	w19, [x3,8]
.L519:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
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
	bne	.L537
	str	w1, [x0,520]
	b	.L535
.L537:
	str	w1, [x0,336]
.L535:
	ret
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	zftl_flash_de_init
	.type	zftl_flash_de_init, %function
zftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	bl	nandc_wait_flash_ready
	adrp	x19, .LANCHOR0
	mov	w0, 0
	add	x20, x19, :lo12:.LANCHOR0
	bl	hynix_reconfig_rr_para
	ldrb	w0, [x20,672]
	cbz	w0, .L542
	ldrb	w0, [x20,780]
	tbz	x0, 0, .L542
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,672]
.L542:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,720]
	cbz	w0, .L543
	mov	w0, 0
	strb	wzr, [x19,720]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,720]
.L543:
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
	.global	buf_init
	.type	buf_init, %function
buf_init:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x19, x0, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	add	x19, x19, 784
	mov	w21, 0
	mov	x22, x0
.L556:
	add	x20, x22, :lo12:.LANCHOR0
	uxtb	w0, w21
	add	w1, w0, 1
	strb	w0, [x19,1]
	strb	w1, [x19]
	add	w21, w21, 1
	ldrb	w0, [x20,2832]
	add	x19, x19, 64
	strb	wzr, [x19,-62]
	str	xzr, [x19,-48]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x19,-56]
	ldrb	w0, [x20,2832]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,-40]
	cmp	w21, 32
	bne	.L556
	strb	w21, [x20,2834]
	mov	w0, -1
	strb	wzr, [x20,2833]
	strb	w0, [x20,2768]
	ldp	x21, x22, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	buf_init, .-buf_init
	.align	2
	.global	buf_reinit
	.type	buf_reinit, %function
buf_reinit:
	adrp	x0, .LANCHOR0
	mov	w2, 0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 784
.L559:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L559
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0,2833]
	strb	w1, [x0,2768]
	strb	w2, [x0,2834]
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
	bne	.L567
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L563
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 74
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L563:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L561
.L567:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 784
.L568:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L568
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L565
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 81
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L565:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 784
	strb	w1, [x0,x20]
.L561:
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
	add	x0, x0, 2835
	bl	buf_add_tail
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	queue_read_cmd, .-queue_read_cmd
	.align	2
	.global	buf_free
	.type	buf_free, %function
buf_free:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	ldrb	w1, [x0,2]
	and	w1, w1, 8
	uxtb	w1, w1
	strb	w1, [x0,2]
	cbz	w1, .L571
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L571
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2833
	bl	buf_add_tail
	b	.L572
.L571:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2833]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2833]
.L572:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2834]
	add	w0, w0, 1
	strb	w0, [x19,2834]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_free, .-buf_free
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
	ldrb	w0, [x20,2834]
	cbz	w0, .L580
.L583:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	ldrb	w2, [x1,2833]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L581
	b	.L582
.L580:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 121
	add	x1, x1, 1064
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w1, [x20,2834]
	cbnz	w1, .L583
.L585:
	mov	x0, 0
	b	.L584
.L581:
	ldrb	w1, [x1,2834]
	cmp	w1, 1
	beq	.L585
.L582:
	add	x1, x19, :lo12:.LANCHOR0
	lsl	x2, x2, 6
	add	x4, x1, 784
	add	x3, x4, x2
	ldrb	w5, [x4,x2]
	strb	w5, [x1,2833]
	ldrb	w5, [x1,2834]
	strh	wzr, [x3,50]
	sub	w5, w5, #1
	strb	w5, [x1,2834]
	mov	w1, 1
	strb	w1, [x3,2]
	mov	w1, -1
	str	xzr, [x3,16]
	strb	w1, [x4,x2]
	mov	w1, -1
	strb	wzr, [x3,56]
	str	w1, [x3,36]
	strb	wzr, [x3,57]
.L584:
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
	bne	.L592
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L596
.L592:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 784
.L594:
	cmp	w2, 255
	beq	.L597
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L594
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L596:
	mov	w0, 1
	b	.L591
.L597:
	mov	w0, 0
.L591:
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
	ldrb	w0, [x0,2834]
	cbnz	w0, .L599
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 172
	add	x1, x1, 1080
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L599:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2834]
	cbz	w0, .L598
	add	x0, x19, 2833
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L598
	ldrb	w0, [x19,2834]
	sub	w0, w0, #1
	strb	w0, [x19,2834]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L598:
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
	adrp	x0, .LC46
	add	x0, x0, :lo12:.LC46
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
	bne	.L606
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L606:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L608:
	ldrb	w2, [x0]
	cbz	w2, .L615
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L609
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L610
.L609:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L611
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L610
.L611:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L615
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L610:
	add	x0, x0, 1
	b	.L608
.L615:
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
	adrp	x0, .LC47
	add	x3, x3, :lo12:.LANCHOR1
	add	x29, sp, 0
	mov	w1, 292
	add	x3, x3, 1096
	mov	x4, x2
	add	x0, x0, :lo12:.LC47
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
	adrp	x1, .LC48
	add	x7, x7, 8
	add	x21, x22, :lo12:.LANCHOR0
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC48
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
	adrp	x1, .LC49
	mov	x0, x19
	add	x1, x1, :lo12:.LC49
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,2836]
	adrp	x1, .LC50
	mov	x0, x19
	add	x1, x1, :lo12:.LC50
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x1, .LC51
	mov	x0, x19
	add	x1, x1, :lo12:.LC51
	add	x19, x19, 10
	bl	strcpy
	ldr	w2, [x21,2840]
	adrp	x1, .LC52
	mov	x0, x19
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,520]
	adrp	x1, .LC53
	mov	x0, x19
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC54
	mov	x0, x19
	add	x1, x1, :lo12:.LC54
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC55
	mov	x0, x19
	add	x1, x1, :lo12:.LC55
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2844]
	adrp	x1, .LC56
	mov	x0, x19
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2846]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2848]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2850]
	adrp	x1, .LC59
	mov	x0, x19
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2852]
	adrp	x1, .LC60
	mov	x0, x19
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2854]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w4, [x21,2857]
	adrp	x1, .LC62
	ldrb	w2, [x21,2834]
	mov	x0, x19
	ldrb	w3, [x21,2856]
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x3, [x21,2864]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2864]
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2864]
	adrp	x1, .LC65
	add	x1, x1, :lo12:.LC65
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,608]
	mov	w2, 10
	adrp	x1, .LC68
	add	x1, x1, :lo12:.LC68
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w2, [x22,#:lo12:.LANCHOR0]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,608]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC73
	mov	x0, x19
	add	x1, x1, :lo12:.LC73
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2864]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC76
	mov	x0, x19
	add	x1, x1, :lo12:.LC76
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC77
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,608]
	adrp	x1, .LC78
	mov	x0, x19
	add	x1, x1, :lo12:.LC78
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2864]
	adrp	x1, .LC79
	mov	x0, x19
	add	x1, x1, :lo12:.LC79
	ldrh	w2, [x6,74]
	ldr	w3, [x6,84]
	ldrh	w4, [x6,88]
	ldrh	w5, [x6,92]
	ldrh	w6, [x6,96]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2864]
	adrp	x1, .LC80
	mov	x0, x19
	add	x1, x1, :lo12:.LC80
	ldr	w3, [x6,80]
	ldrh	w4, [x6,90]
	ldrh	w5, [x6,94]
	ldrh	w2, [x6,72]
	ldrh	w6, [x6,98]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2872]
	adrp	x1, .LC81
	mov	x0, x19
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w6, [x21,2884]
	adrp	x1, .LC82
	ldrh	w2, [x21,2874]
	mov	x0, x19
	ldrh	w3, [x21,2876]
	add	x1, x1, :lo12:.LC82
	ldrh	w4, [x21,2878]
	ldrh	w5, [x21,2880]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x5, [x21,608]
	adrp	x1, .LC83
	mov	x0, x19
	add	x1, x1, :lo12:.LC83
	ldrh	w2, [x5,584]
	ldrh	w3, [x5,586]
	ldrh	w4, [x5,588]
	ldrh	w5, [x5,590]
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
	add	x0, x19, 2888
	bl	buf_add_tail
	ldrb	w0, [x19,2856]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,2856]
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	uxth	w21, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	uxth	w23, w1
	uxth	w20, w2
	ldrh	w0, [x0,576]
	cmp	w0, w21
	bhi	.L623
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 259
	add	x1, x1, 1312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L623:
	add	x6, x22, :lo12:.LANCHOR0
	uxtw	x19, w21
	lsl	x25, x19, 1
	adrp	x24, .LANCHOR2
	ldr	x0, [x6,600]
	ldrh	w26, [x0,x25]
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L624
	ldr	x0, [x6,584]
	mov	w1, w21
	ldrh	w5, [x6,2952]
	mov	w2, w23
	add	x0, x0, x19, lsl 2
	ldrh	w6, [x6,2878]
	mov	w4, w26
	ldrb	w3, [x0,2]
	adrp	x0, .LC84
	add	x0, x0, :lo12:.LC84
	ubfx	x3, x3, 5, 3
	bl	printk
.L624:
	add	x3, x22, :lo12:.LANCHOR0
	ldr	x0, [x3,584]
	add	x0, x0, x19, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L625
	cbz	w26, .L626
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 264
	add	x1, x1, 1312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L626
.L625:
	ldrh	w1, [x3,2896]
	add	x6, x3, 2896
	mov	w19, 0
	cmp	w1, w21
	beq	.L626
	ldr	x3, [x3,608]
	ldrh	w1, [x3,48]
	cmp	w1, w21
	beq	.L626
	ldrh	w1, [x3,16]
	cmp	w1, w21
	beq	.L626
	ldrh	w1, [x3,80]
	cmp	w1, w21
	beq	.L626
	ldrh	w5, [x6,56]
	mov	x4, 0
.L627:
	cmp	w5, w4
	bls	.L658
	add	x4, x4, 1
	add	x1, x6, x4, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L627
	mov	w19, 0
	b	.L626
.L658:
	cbnz	w23, .L629
	add	x4, x22, :lo12:.LANCHOR0
	mov	w19, w23
	ldrh	w7, [x4,3224]
	cmp	w7, w21
	beq	.L626
	mov	x6, 0
.L631:
	add	x1, x4, 3232
	ldrh	w1, [x6,x1]
	cmp	w21, w1
	bne	.L630
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x1, 8, .L626
	ldr	x3, [x4,600]
	mov	w1, w21
	ldrb	w2, [x0,2]
	adrp	x0, .LC85
	mov	w4, w21
	mov	w5, w7
	ldrh	w3, [x3,x25]
	ubfx	x2, x2, 5, 3
	add	x0, x0, :lo12:.LC85
	bl	printk
	b	.L626
.L630:
	add	x6, x6, 2
	cmp	x6, 16
	bne	.L631
	sxtw	x4, w20
	add	x3, x3, x4, lsl 7
	add	x19, x3, 136
	b	.L633
.L629:
	add	x19, x3, 392
.L633:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L634
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w3, [x0,2]
	adrp	x0, .LC84
	mov	w1, w21
	add	x0, x0, :lo12:.LC84
	mov	w2, w23
	ldr	x4, [x6,600]
	ubfx	x3, x3, 5, 3
	ldrh	w6, [x6,2878]
	ldrh	w4, [x4,x25]
	bl	printk
.L634:
	mov	x3, x19
	add	x0, x19, 128
	mov	w1, 65535
.L637:
	ldrh	w2, [x3]
	cmp	w2, w1
	bne	.L635
	strh	w21, [x3]
	add	x22, x22, :lo12:.LANCHOR0
	cbz	w23, .L636
	ldr	x1, [x22,608]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L657
.L636:
	ldr	x0, [x22,608]
	add	x2, x0, x20, uxth 1
	ldrh	w0, [x2,120]
	add	w0, w0, 1
	strh	w0, [x2,120]
.L657:
	mov	w19, 1
	b	.L626
.L635:
	add	x3, x3, 2
	cmp	x3, x0
	bne	.L637
	b	.L657
.L626:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_get_src_ppa_from_index
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	adrp	x1, .LANCHOR0+3248
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3248]
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
	add	x23, x21, 784
	add	x22, x22, 1328
.L661:
	ldrb	w0, [x21,3256]
	adrp	x19, .LANCHOR0
	cmp	w0, 255
	beq	.L683
	sxtw	x25, w0
	lsl	x0, x25, 6
	add	x2, x23, x0
	ldrb	w0, [x23,x0]
	ldr	w1, [x2,52]
	strb	w0, [x21,3256]
	ldrh	w24, [x2,48]
	cbz	w1, .L662
	ldr	w2, [x2,40]
	adrp	x0, .LC86
	mov	w3, 1
	add	x0, x0, :lo12:.LC86
	strh	w3, [x21,3216]
	str	w2, [x21,3220]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 545
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L662:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w20, 1
	ldrb	w1, [x0,3257]
	cmp	w1, 3
	bne	.L663
	ldr	x0, [x0,608]
	ldrb	w20, [x0,89]
	add	w20, w20, w20, lsl 1
	and	w20, w20, 1023
.L663:
	adrp	x0, .LANCHOR2
	uxth	x26, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L664
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w24
	add	x2, x0, x25, lsl 6
	mov	w3, w24
	ldr	x4, [x0,3264]
	adrp	x0, .LC87
	ldrb	w2, [x2,785]
	add	x0, x0, :lo12:.LC87
	ldrb	w4, [x4,x26]
	bl	printk
.L664:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3264]
	add	x0, x0, x25, lsl 6
	ldrb	w1, [x1,x26]
	ldrb	w0, [x0,785]
	cmp	w1, w0
	beq	.L665
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 552
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L665:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 784
	add	x0, x1, x25, lsl 6
	ldrb	w0, [x0,61]
	cmp	w0, 3
	beq	.L666
	ldrb	w0, [x2,3257]
	cmp	w0, 3
	bne	.L666
	ldrb	w0, [x2,3272]
	cbnz	w0, .L666
	ldr	x3, [x2,3264]
	b	.L667
.L666:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w26, -1
	add	x27, x19, 784
	strh	w24, [x19,3212]
.L668:
	cmp	w20, w25, uxth
	bls	.L661
	add	w1, w25, w24
	ldr	x2, [x19,3264]
	add	w25, w25, 1
	ldrb	w0, [x2,x1]
	strb	w26, [x2,x1]
	sbfiz	x0, x0, 6, 32
	add	x0, x27, x0
	strb	wzr, [x0,61]
	bl	buf_free
	ldrb	w0, [x19,2903]
	sub	w0, w0, #1
	strb	w0, [x19,2903]
	b	.L668
.L667:
	cmp	w20, w0, uxth
	bls	.L661
	add	w2, w0, w24
	add	w0, w0, 1
	ldrb	w2, [x3,x2]
	add	x2, x1, x2, lsl 6
	strb	wzr, [x2,61]
	b	.L667
.L683:
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
	ldrb	w0, [x0,3273]
	ldrh	w4, [x1,124]
	cbz	w4, .L685
	add	x1, x1, 392
	mov	w2, 1
	b	.L686
.L685:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L687
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L686:
	add	x4, x1, 128
	mov	w5, 65535
.L690:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L688
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L689
	ldr	x2, [x3,608]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L687
.L689:
	ldrb	w1, [x3,3273]
	ldr	x2, [x3,608]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L687
.L688:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L690
.L687:
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
	ldrb	w3, [x2,2903]
	cbz	w3, .L695
	ldrb	w3, [x2,2834]
	cmp	w3, 1
	bhi	.L695
	ldrh	w4, [x2,3274]
	mov	x20, x2
	ldrb	w0, [x2,3276]
	add	x2, x2, 784
	ldrh	w1, [x2,2428]
	mul	w0, w4, w0
	add	w3, w1, 24
	ldr	x4, [x2,2480]
	cmp	w0, w3
	csel	w3, w0, w3, ls
.L696:
	cmp	w1, w3
	bcs	.L709
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L697
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L697
	str	x1, [x29,56]
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L698
	ldr	x2, [x20,3264]
	adrp	x0, .LC88
	add	x0, x0, :lo12:.LC88
	ldrb	w2, [x2,x21]
	bl	printk
.L698:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3264]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2903]
	sub	w0, w0, #1
	strb	w0, [x19,2903]
	mov	w0, 1
	b	.L695
.L697:
	add	w1, w1, 1
	b	.L696
.L709:
	mov	w0, 0
.L695:
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
	adrp	x0, .LC89
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC89
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,2896]
	ldrh	w2, [x6,2898]
	ldrh	w3, [x6,3210]
	ldrb	w4, [x6,2834]
	ldrb	w5, [x6,2903]
	ldrh	w6, [x6,2872]
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
	bne	.L712
	adrp	x0, .LANCHOR0+3280
	ldr	x0, [x0,#:lo12:.LANCHOR0+3280]
	b	.L715
.L712:
	cmp	w1, 2
	bne	.L714
	mov	w0, w2
	bl	zftl_get_gc_node.part.12
	b	.L716
.L714:
	adrp	x0, .LANCHOR0+3288
	ldr	x0, [x0,#:lo12:.LANCHOR0+3288]
.L715:
	mov	w1, w2
	bl	_list_get_gc_head_node.isra.5
.L716:
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
	cbnz	w19, .L764
	cbnz	w21, .L720
	adrp	x26, .LC90
	strh	w21, [x0,3296]
	mov	w19, w21
	mov	w25, w21
	uxth	w23, w22
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x26, x26, :lo12:.LC90
.L721:
	cmp	w25, w23
	bcs	.L726
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, 3
	str	x3, [x29,96]
	ldrh	w2, [x3,3296]
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
	strh	w2, [x3,3296]
	beq	.L722
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	uxtw	x5, w1
	tbz	x0, 8, .L723
	ldr	x3, [x3,600]
	mov	x0, x26
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w3, [x3,x5,lsl 1]
	bl	printk
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
.L723:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x5,lsl 1]
	ldrh	w0, [x1,2878]
	cmp	w2, w0
	bcs	.L724
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L725
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L725
	b	.L726
.L724:
	strh	wzr, [x1,3296]
	b	.L726
.L722:
	strh	wzr, [x3,3296]
	b	.L726
.L725:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L721
.L726:
	tbz	x24, 1, .L728
	mov	w26, 0
	mov	w27, 65535
.L729:
	cmp	w26, w23
	beq	.L733
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w28, [x25,3298]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x25,3298]
	cmp	w1, w27
	beq	.L730
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x25,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x25,2880]
	cmp	w2, w1
	bcs	.L730
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L732
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L732
	b	.L735
.L730:
	strh	wzr, [x25,3298]
	b	.L733
.L732:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L729
.L733:
	cmp	w19, w23
	bcs	.L735
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3274]
	ldrb	w4, [x0,3276]
	ldrh	w2, [x0,3300]
	ldrh	w3, [x0,2880]
	mul	w1, w1, w4
	sub	w1, w1, w2, lsr 2
	cmp	w3, w1
	bge	.L728
	add	w2, w3, w2, lsr 3
	strh	w2, [x0,2880]
	b	.L728
.L735:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2880]
	ldrh	w0, [x1,3300]
	cmp	w2, w0
	bls	.L728
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,2880]
.L728:
	tbz	x24, 0, .L736
	cmp	w19, w23
	bcs	.L736
	mov	w25, 0
	mov	w27, 65535
.L737:
	cmp	w25, w23
	beq	.L740
	add	x28, x20, :lo12:.LANCHOR0
	ldrh	w26, [x28,3302]
	mov	w0, w26
	add	w26, w26, 1
	bl	zftl_get_gc_node.part.12
	strh	w26, [x28,3302]
	cmp	w27, w0, uxth
	beq	.L738
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L739
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bcc	.L739
	b	.L742
.L738:
	strh	wzr, [x28,3302]
	b	.L740
.L739:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L737
.L740:
	cmp	w19, w23
	bcs	.L742
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,2878]
	ldrh	w0, [x20,3300]
	cmp	w1, w0
	bcs	.L736
	add	w0, w1, w0, lsr 3
	b	.L817
.L742:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,3300]
	ldrh	w1, [x20,2878]
	cmp	w1, w0, lsr 1
	bls	.L736
	sub	w0, w1, w0, lsr 3
.L817:
	strh	w0, [x20,2878]
	b	.L736
.L720:
	and	w1, w24, 1
	strh	w19, [x0,3214]
	strh	w19, [x0,3304]
	str	w1, [x29,104]
	tbz	x24, 0, .L743
	ldrh	w2, [x0,3306]
	ldrh	w1, [x0,2852]
	cmp	w1, w2, lsr 2
	bcc	.L744
	ldrh	w0, [x0,2850]
	cmp	w0, w1
	bls	.L745
.L744:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w19, 0
	ldrh	w0, [x23,2850]
	lsr	w0, w0, 2
	strh	w0, [x23,3302]
	mov	w0, 0
	bl	zftl_get_gc_node.part.12
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L746
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x23,600]
	ldrh	w2, [x23,3300]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 2
	bcs	.L746
	mov	w1, w19
	mov	w2, w21
	strh	w19, [x23,3302]
	bl	gc_add_sblk
	cmp	w0, w19
	cset	w19, ne
.L746:
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L748:
	add	x25, x20, :lo12:.LANCHOR0
	ldrh	w28, [x25,3302]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node.part.12
	strh	w28, [x25,3302]
	cmp	w26, w0, uxth
	beq	.L745
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L747
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L745
.L747:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L748
.L745:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3306]
	ldrh	w1, [x0,2852]
	cmp	w1, w2, lsr 3
	bhi	.L765
	ldrh	w0, [x0,2850]
	add	w0, w0, 8
	cmp	w1, w0
	bgt	.L765
.L743:
	tbz	x24, 1, .L751
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 0
	mov	w27, 65535
	uxth	w28, w22
	strh	wzr, [x0,3298]
.L756:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w2, [x23,3298]
	str	x2, [x29,96]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,96]
	cmp	w1, w27
	add	w2, w2, 1
	strh	w2, [x23,3298]
	beq	.L752
	cmp	w22, 1
	bne	.L753
	ldrh	w3, [x23,3274]
	ldrb	w2, [x23,3276]
	mul	w2, w2, w3
	ldrh	w3, [x23,3300]
	sub	w2, w2, w3, lsr 3
	strh	w2, [x23,2874]
.L753:
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,2874]
	cmp	w3, w1
	bcs	.L754
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L755
	add	w19, w19, 1
	add	w26, w26, 1
	uxth	w19, w19
	uxth	w26, w26
	cmp	w19, w28
	bcc	.L755
	b	.L752
.L754:
	strh	wzr, [x2,3298]
	b	.L752
.L755:
	sub	w0, w25, #1
	uxth	w25, w0
	cbnz	w25, .L756
.L752:
	cmp	w19, w22
	bcc	.L757
	cbnz	w26, .L758
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bls	.L758
.L757:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3274]
	ldrb	w4, [x0,3276]
	ldrh	w3, [x0,3300]
	ldrh	w2, [x0,2874]
	mul	w1, w1, w4
	lsr	w3, w3, 3
	sub	w1, w1, w3
	cmp	w2, w1
	bge	.L751
	add	w2, w3, w2
	strh	w2, [x0,2874]
	b	.L751
.L758:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3300]
	ldrb	w2, [x0,3276]
	ldrh	w3, [x0,2874]
	mul	w2, w1, w2
	cmp	w3, w2
	ble	.L751
	sub	w1, w3, w1, lsr 3
	strh	w1, [x0,2874]
.L751:
	ldr	w0, [x29,104]
	cbz	w0, .L736
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3296]
.L762:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x23,3296]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x23,3296]
	cmp	w1, w26
	beq	.L759
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x23,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 2
	bls	.L760
	ldrh	w2, [x23,2876]
	cmp	w2, w1
	bls	.L760
	ldrh	w2, [x23,3306]
	ldrh	w1, [x23,2852]
	cmp	w1, w2, lsr 1
	bls	.L759
.L760:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L761
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L759
.L761:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L762
.L759:
	cmp	w19, w22
	add	x20, x20, :lo12:.LANCHOR0
	bcs	.L763
	ldrh	w0, [x20,3300]
	ldrh	w1, [x20,2876]
	cmp	w1, w0, lsr 1
	bls	.L736
	sub	w0, w1, w0, lsr 3
	strh	w0, [x20,2876]
	b	.L736
.L763:
	ldrh	w1, [x20,3300]
	ldrb	w0, [x20,3276]
	ldrh	w2, [x20,2876]
	mul	w0, w1, w0
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L736
	add	w1, w2, w1, lsr 3
	strh	w1, [x20,2876]
.L736:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L764
	adrp	x0, .LC91
	mov	w1, w21
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	mov	w3, w19
	mov	w4, w22
	bl	printk
.L764:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L765:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3296]
.L750:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x25,3296]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	strh	w28, [x25,3296]
	cmp	w26, w0, uxth
	beq	.L743
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L749
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L743
.L749:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L750
	b	.L743
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
	bne	.L819
	add	x0, x2, 3312
	add	x2, x2, 2844
	b	.L822
.L819:
	cmp	w0, 16
	bne	.L821
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L822
.L821:
	add	x0, x2, 3328
	add	x2, x2, 2848
.L822:
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
	bne	.L824
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L827
.L824:
	cmp	w3, 96
	bne	.L826
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L827
.L826:
	cmp	w3, 160
	bne	.L823
	add	x0, x2, 3280
	add	x2, x2, 2854
.L827:
	bl	_insert_data_list
.L823:
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
	cbnz	w21, .L829
	mov	w0, 65535
	cmp	w19, w0
	beq	.L829
	adrp	x0, .LC92
	mov	w1, w19
	add	x0, x0, :lo12:.LC92
	bl	printk
	ldr	x0, [x23,608]
	mov	w1, -1
	strh	w1, [x0,588]
	b	.L830
.L829:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w3, [x0,2846]
	ldrh	w1, [x0,2848]
	cmp	w3, w1
	bls	.L831
	mov	w1, 0
	mov	x2, x0
	lsr	w3, w3, 3
	cmp	w21, w1
	csel	w1, w3, w1, ne
	add	x0, x0, 3320
	add	x2, x2, 2846
	b	.L849
.L831:
	lsr	w1, w1, 2
	cbz	w21, .L835
	mov	w1, 7
	mul	w3, w3, w1
	mov	w1, 8
	sdiv	w3, w3, w1
	uxth	w1, w3
.L835:
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x2, 3328
	add	x2, x2, 2848
.L849:
	bl	_list_pop_index_node
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L836
	add	x5, x20, :lo12:.LANCHOR0
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	mov	w1, w19
	mov	w2, w22
	ldr	x3, [x5,3312]
	ldrh	w4, [x5,2844]
	ldrh	w5, [x5,2848]
	bl	printk
.L836:
	cbz	w21, .L830
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L830
	add	x20, x20, :lo12:.LANCHOR0
	uxtw	x6, w19
	lsl	x0, x6, 2
	ldr	x1, [x20,584]
	ldr	x7, [x20,600]
	add	x2, x1, x0
	ldr	w4, [x1,x0]
	ldrh	w5, [x1,x0]
	adrp	x0, .LC94
	ldrb	w3, [x2,2]
	add	x0, x0, :lo12:.LC94
	ldrh	w6, [x7,x6,lsl 1]
	mov	w1, w19
	ubfx	x2, x3, 5, 3
	ubfx	x4, x4, 11, 8
	ubfx	x3, x3, 3, 2
	and	w5, w5, 2047
	bl	printk
.L830:
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
	bne	.L851
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2846]
	ldrh	w1, [x0,2848]
	cmp	w2, w1
	bcc	.L852
	ldrh	w0, [x0,2844]
	cmp	w0, w2
	bls	.L853
	cbz	w1, .L853
.L852:
	add	x2, x19, :lo12:.LANCHOR0
	lsr	w1, w1, 1
	add	x0, x2, 3328
	b	.L881
.L853:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L880
.L851:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w21, 1
	ldr	x0, [x22,608]
	ldrh	w20, [x0,590]
	beq	.L855
	mov	w0, 65535
	cmp	w20, w0
	beq	.L855
	adrp	x0, .LC95
	mov	w1, w20
	add	x0, x0, :lo12:.LC95
	bl	printk
	ldr	x0, [x22,608]
	mov	w1, -1
	strh	w1, [x0,590]
	b	.L856
.L855:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2844]
	ldrh	w1, [x2,2848]
	cmp	w0, w1
	bcc	.L857
	ldrh	w2, [x2,2846]
	cmp	w2, w0
	bls	.L858
	cbz	w1, .L858
.L857:
	lsr	w1, w1, 1
	cmp	w21, 1
	csel	w3, w1, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3328
	mov	w1, w3
.L881:
	add	x2, x2, 2848
	b	.L880
.L858:
	lsr	w0, w0, 1
	cmp	w21, 1
	csel	w3, w0, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3312
	mov	w1, w3
	add	x2, x2, 2844
.L880:
	bl	_list_pop_index_node
	uxth	w0, w0
	mov	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L856
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	mov	w1, w20
	mov	w2, w21
	ldr	x3, [x19,3312]
	ldrh	w4, [x19,2844]
	ldrh	w5, [x19,2848]
	bl	printk
.L856:
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
	bne	.L883
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L886
.L883:
	cmp	w3, 96
	bne	.L885
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L886
.L885:
	cmp	w3, 160
	bne	.L882
	add	x0, x2, 3280
	add	x2, x2, 2854
.L886:
	bl	_list_remove_node
.L882:
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
	bne	.L888
	add	x0, x2, 3312
	add	x2, x2, 2844
	b	.L891
.L888:
	cmp	w0, 16
	bne	.L890
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L891
.L890:
	add	x0, x2, 3328
	add	x2, x2, 2848
.L891:
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
	bne	.L893
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L896
.L893:
	cmp	w3, 96
	bne	.L895
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L896
.L895:
	cmp	w3, 160
	bne	.L892
	add	x0, x2, 3280
	add	x2, x2, 2854
.L896:
	bl	_list_update_data_list
.L892:
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
	adrp	x0, .LC96
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	add	x0, x0, :lo12:.LC96
	ldrh	w2, [x1]
	ldr	x1, [x19]
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L897
	mov	x22, -6148914691236517206
	adrp	x23, .LC97
	mov	w20, 0
	adrp	x25, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC97
	mov	w26, 65535
	mov	w24, 6
.L900:
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
	beq	.L897
	ldr	x0, [x21,528]
	add	w20, w20, 1
	umull	x19, w19, w24
	uxth	w20, w20
	add	x19, x0, x19
	ldrh	w0, [x21,3336]
	cmp	w0, w20
	bcs	.L900
.L897:
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
	add	x0, x19, 3312
	add	x1, x19, 2844
	bl	print_list_info
	add	x0, x19, 3320
	add	x1, x19, 2846
	bl	print_list_info
	add	x0, x19, 3328
	add	x1, x19, 2848
	bl	print_list_info
	add	x0, x19, 616
	add	x1, x19, 2850
	bl	print_list_info
	add	x0, x19, 3288
	add	x1, x19, 2852
	bl	print_list_info
	add	x0, x19, 3280
	add	x1, x19, 2854
	bl	print_list_info
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	adrp	x0, .LANCHOR0+2864
	ldr	x0, [x0,#:lo12:.LANCHOR0+2864]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L903
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
.L903:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L904
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L904:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L905
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L905:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L902
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L902:
	ret
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x7, .LANCHOR0
	uxth	w0, w0
	add	x2, x7, :lo12:.LANCHOR0
	mov	w12, 21
	ldr	x2, [x2,584]
	add	x2, x2, x0, uxth 2
	ldrb	w11, [x2,3]
	mov	w2, 0
	mov	w6, w2
.L908:
	add	x8, x7, :lo12:.LANCHOR0
	ldrb	w3, [x8,3276]
	cmp	w6, w3
	bge	.L914
	asr	w3, w11, w6
	tbnz	x3, 0, .L909
	ldrb	w5, [x8,3338]
	sbfiz	x10, x2, 1, 32
	ldrh	w3, [x8,3340]
	sdiv	w9, w6, w5
	sub	w3, w12, w3
	lsl	w3, w9, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,3338]
	cmp	w3, 1
	bls	.L910
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L910:
	add	w2, w2, 1
.L909:
	add	w6, w6, 1
	b	.L908
.L914:
	mov	w0, w2
	mov	w4, -1
.L912:
	add	x3, x7, :lo12:.LANCHOR0
	ldrb	w3, [x3,3276]
	cmp	w2, w3
	bge	.L915
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L912
.L915:
	ret
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	x22, w0
	adrp	x0, .LANCHOR0
	add	x3, x0, :lo12:.LANCHOR0
	mov	x1, x22
	stp	x19, x20, [sp,16]
	lsl	x19, x22, 2
	mov	x21, x0
	ldr	x7, [x3,584]
	add	x20, x7, x19
	ldrb	w2, [x20,2]
	tbz	x2, 3, .L918
	ldr	x6, [x3,2864]
	ldrh	w4, [x7,x19]
	ldrh	w10, [x3,3342]
	ldrh	w0, [x6,74]
	and	w5, w4, 2047
	ldr	w4, [x7,x19]
	add	w0, w5, w0
	ldrh	w6, [x6,72]
	ubfx	x9, x4, 11, 8
	ldrh	w4, [x3,592]
	uxth	w8, w0
	add	w6, w9, w6
	uxth	w0, w6
	udiv	w6, w8, w4
	add	w6, w0, w6
	cmp	w6, w10
	ble	.L919
	ldrh	w0, [x3,2846]
	ldrh	w6, [x3,2854]
	add	w6, w6, w0
	ldrh	w0, [x3,3308]
	add	w0, w0, 8
	cmp	w6, w0
	blt	.L935
	ldrh	w0, [x3,2844]
	ldrh	w6, [x3,2850]
	add	w6, w6, w0
	ldrh	w0, [x3,2852]
	add	w6, w6, w0
	ldrh	w0, [x3,3344]
	add	w0, w0, 8
	cmp	w6, w0
	bge	.L935
	b	.L924
.L919:
	madd	w0, w0, w4, w8
	ldrh	w6, [x3,3346]
	cmp	w0, w6
	ble	.L921
	ldrh	w0, [x3,2844]
	ldrh	w6, [x3,2850]
	add	w6, w6, w0
	ldrh	w0, [x3,2852]
	add	w6, w6, w0
	ldrh	w0, [x3,3344]
	add	w0, w0, 8
	cmp	w6, w0
	blt	.L924
	ldrh	w0, [x3,2846]
	ldrh	w6, [x3,2854]
	add	w6, w6, w0
	ldrh	w0, [x3,3308]
	add	w0, w0, 8
	cmp	w6, w0
	bge	.L924
.L935:
	mov	w0, 2
	bfi	w2, w0, 3, 2
	b	.L933
.L924:
	and	w2, w2, -25
.L933:
	strb	w2, [x20,2]
.L921:
	ldrb	w0, [x20,2]
	ands	w0, w0, 24
	bne	.L925
	mul	w4, w9, w4
	ldrh	w0, [x7,x19]
	add	w4, w4, w4, lsl 1
	add	w4, w5, w4, lsr 2
	ubfx	x5, x4, 2, 9
	bfi	w0, w5, 0, 11
	strh	w0, [x7,x19]
	b	.L918
.L925:
	cmp	w0, 16
	bne	.L918
	sdiv	w4, w5, w4
	ldr	w0, [x7,x19]
	add	w4, w4, w4, lsl 1
	add	w4, w9, w4, lsr 2
	ubfx	x4, x4, 2, 6
	bfi	w0, w4, 11, 8
	str	w0, [x7,x19]
.L918:
	mov	w0, w1
	str	x1, [x29,56]
	bl	zftl_remove_data_node
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x29,56]
	ldr	x2, [x0,584]
	add	x2, x2, x19
	ldrb	w3, [x2,2]
	and	w3, w3, 31
	strb	w3, [x2,2]
	ldr	x2, [x0,600]
	strh	wzr, [x2,x22,lsl 1]
	ldrb	w2, [x20,2]
	tbz	x2, 3, .L927
	ldr	x2, [x0,608]
	ldrh	w0, [x2,584]
	cmp	w0, w1
	bne	.L928
	mov	w0, -1
	ldrh	w3, [x2,588]
	strh	w0, [x2,584]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L927
	adrp	x0, .LC98
	strh	w1, [x2,588]
	add	x0, x0, :lo12:.LC98
	b	.L934
.L928:
	ldrh	w0, [x2,586]
	cmp	w0, w1
	bne	.L927
	mov	w0, -1
	ldrh	w3, [x2,590]
	strh	w0, [x2,586]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L927
	adrp	x0, .LC99
	strh	w1, [x2,590]
	add	x0, x0, :lo12:.LC99
.L934:
	bl	printk
	b	.L916
.L927:
	mov	w0, w1
	bl	zftl_insert_free_list
.L916:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	add	x24, x23, 2896
	add	x21, x21, 1352
.L937:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L975
	add	x0, x24, x20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,600]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L938
	adrp	x0, .LC100
	mov	w1, w22
	add	x0, x0, :lo12:.LC100
	bl	printk
.L938:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,600]
	strh	wzr, [x1,x26]
	ldr	x1, [x0,600]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L939
	ldr	x0, [x0,584]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L940
	ldrb	w2, [x22,2]
	adrp	x0, .LC101
	add	x0, x0, :lo12:.LC101
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L940:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L953
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L941
.L953:
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 799
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L941:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,608]
	ldrh	w1, [x2,124]
	cbz	w1, .L943
	mov	x0, 0
.L945:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L944
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L943
.L944:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L945
.L943:
	ldrh	w1, [x2,120]
	cbz	w1, .L946
	mov	x0, 0
.L948:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L947
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L946
.L947:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L948
.L946:
	ldrh	w1, [x2,122]
	cbz	w1, .L949
	mov	x0, 0
.L951:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L950
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L949
.L950:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L951
	b	.L949
.L939:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L949:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L937
.L975:
	strh	wzr, [x24,56]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_free_src_blk, .-gc_free_src_blk
	.align	2
	.global	ftl_erase_phy_blk
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	stp	x29, x30, [sp, -48]!
	adrp	x3, .LANCHOR0
	uxth	w2, w0
	add	x5, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	mov	w0, 21
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	w4, 1
	mov	w21, w1
	ldrh	w19, [x5,3340]
	mov	x20, x3
	sub	w19, w0, w19
	ldrb	w0, [x5,3348]
	asr	w22, w2, w19
	lsl	w19, w4, w19
	sub	w19, w19, #1
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L977
	ldrh	w2, [x5,3350]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L977:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,3350]
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
.L982:
	add	x2, x23, :lo12:.LANCHOR0
	ldrb	w0, [x2,3352]
	cmp	w19, w0
	bge	.L993
	ldrb	w3, [x2,3338]
	mov	w21, 0
	ldrh	w2, [x2,3350]
	mov	w1, w21
	sub	w4, w3, #1
	mul	w6, w19, w3
	mul	w5, w24, w3
.L994:
	cmp	w1, w3
	bge	.L1007
	add	w0, w1, w6
	asr	w0, w25, w0
	tbnz	x0, 0, .L983
	and	w0, w1, w4
	add	x7, x29, 80
	add	w0, w5, w0
	mul	w0, w0, w2
	str	w0, [x7,w21,sxtw 2]
	add	w21, w21, 1
.L983:
	add	w1, w1, 1
	b	.L994
.L1007:
	cmp	w3, 4
	bne	.L1006
	mov	x26, 0
.L985:
	cmp	w21, w26
	ble	.L988
	add	x2, x29, 80
	mov	w0, w19
	mov	w1, w22
	ldr	w2, [x2,x26,lsl 2]
	add	x26, x26, 1
	bl	flash_erase_block_en
	b	.L985
.L1006:
	cmp	w21, 2
	bne	.L989
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3348]
	cbz	w0, .L990
	cmp	w22, wzr
	ldr	w2, [x29,80]
	ldr	w3, [x29,84]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L990:
	ldr	w2, [x29,80]
	mov	w0, w19
	ldr	w3, [x29,84]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L988
.L989:
	cmp	w21, 1
	bne	.L988
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3348]
	cbz	w0, .L992
	cmp	w22, wzr
	ldr	w2, [x29,80]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L992:
	ldr	w2, [x29,80]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L988:
	add	w19, w19, 1
	b	.L982
.L993:
	cbnz	w22, .L995
	ldr	x1, [x2,584]
	ldrh	w0, [x1,x20]
	add	w3, w0, 1
	bfi	w0, w3, 0, 11
	strh	w0, [x1,x20]
	ldr	x0, [x2,2864]
	ldr	w1, [x0,84]
	ldrh	w3, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,84]
	ldr	x1, [x2,584]
	ldrh	w1, [x1,x20]
	ubfx	x1, x1, 0, 11
	uxth	w2, w1
	cmp	w3, w2
	bge	.L997
	strh	w1, [x0,96]
	b	.L997
.L995:
	ldr	x3, [x2,584]
	ldr	w0, [x3,x20]
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x3,x20]
	ldr	x0, [x2,2864]
	ldr	w1, [x0,80]
	add	w1, w1, 1
	str	w1, [x0,80]
	ldr	x1, [x2,584]
	ldrh	w2, [x0,98]
	ldr	w1, [x1,x20]
	ubfx	x1, x1, 11, 8
	cmp	w2, w1, uxtb
	bcs	.L997
	strh	w1, [x0,98]
.L997:
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
	ldr	x1, [x1,2864]
	ldrh	w2, [x1,136]
	cmp	w2, 63
	bls	.L1009
	strh	wzr, [x1,136]
.L1009:
	ldrh	w0, [x1,112]
	cbnz	w0, .L1010
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1117
	add	x1, x1, 1368
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1010:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,2864]
.L1014:
	ldrh	w2, [x1,136]
.L1011:
	cmp	w2, 63
	bgt	.L1017
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L1016
	add	w2, w2, 1
	b	.L1011
.L1017:
	strh	wzr, [x1,136]
	b	.L1014
.L1016:
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
	ldr	x2, [x0,2864]
	ldrh	w0, [x2,138]
	cmp	w0, 63
	bls	.L1019
	strh	wzr, [x2,138]
.L1019:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L1020
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1141
	add	x1, x1, 1392
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1020:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,2864]
.L1024:
	ldrh	w2, [x1,138]
.L1021:
	cmp	w2, 63
	bgt	.L1026
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L1022
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L1022:
	add	w2, w2, 1
	b	.L1021
.L1026:
	strh	wzr, [x1,138]
	b	.L1024
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
	beq	.L1027
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,584]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L1027
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L1031
	ldr	x1, [x23,2864]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L1032
.L1031:
	tst	w0, 24
	ldr	x1, [x23,2864]
	bne	.L1033
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L1032
.L1033:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L1032:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1034
	ldr	w0, [x21,x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21,x20]
	ldr	x1, [x19,2864]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L1027
.L1034:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L1035
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L1027
.L1035:
	cmp	w0, 96
	bne	.L1027
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L1027:
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
	ldrh	w0, [x4,3300]
	ldr	x3, [x4,608]
	ldrb	w4, [x4,3276]
	mul	w0, w0, w4
	cmp	w1, w0
	bge	.L1037
	add	x3, x3, 16
	b	.L1038
.L1037:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L1038:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, x1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L1039
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1528
	add	x1, x1, 1416
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1039:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,3350]
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
	adrp	x1, .LANCHOR0
	mov	w3, 65535
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, x0, uxtb 1
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w2, [x2,3360]
	ldr	x4, [x1,3872]
	ldr	x5, [x1,3880]
.L1041:
	cmp	w2, w3
	beq	.L1045
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L1042
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L1043
.L1042:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L1041
.L1045:
	mov	w0, -1
.L1043:
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
	bne	.L1047
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1643
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1047:
	adrp	x20, .LANCHOR0
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	cmp	w1, w0
	bne	.L1048
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1644
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1048:
	ldrh	w0, [x19,6]
	cbnz	w0, .L1049
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1645
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1049:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrb	w2, [x1,3276]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L1050:
	cmp	w0, w1
	bne	.L1054
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L1051
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1051:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1050
.L1054:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x19,2]
	ldrh	w1, [x20,3350]
	ldrb	w3, [x20,3276]
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
	bne	.L1053
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L1053:
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
	bcs	.L1056
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1676
	add	x1, x1, 1464
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1056:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w20
	bl	ftl_erase_phy_blk
	ldr	x0, [x19,2864]
	ldrh	w0, [x0,130]
	bl	ftl_free_sys_blk
	ldr	x0, [x19,2864]
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
	adrp	x1, .LANCHOR0
	add	x3, x1, :lo12:.LANCHOR0
	ldrh	w2, [x3,3888]
	cbnz	w0, .L1058
	cmp	w2, 4
	bhi	.L1058
	add	w2, w2, 1
	strh	w2, [x3,3888]
	b	.L1057
.L1058:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w9, 65535
	strh	wzr, [x0,3888]
	ldrh	w6, [x0,576]
	ldr	x7, [x0,600]
	ldr	x8, [x0,584]
	mov	x0, 0
	mov	w2, w0
	mov	w3, w0
.L1060:
	cmp	w6, w0, uxth
	bls	.L1068
	ldrh	w4, [x7,x0,lsl 1]
	cmp	w4, w9
	beq	.L1061
	add	x5, x8, x0, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1062
	add	w2, w2, w4
	b	.L1061
.L1062:
	add	w3, w3, w4
.L1061:
	add	x0, x0, 1
	b	.L1060
.L1068:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,608]
	ldr	x1, [x1,2864]
	str	w3, [x0,524]
	str	w2, [x0,528]
	ldrh	w1, [x1,120]
	cbz	w1, .L1057
	udiv	w2, w2, w1
	str	w2, [x0,532]
.L1057:
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
	cbnz	w2, .L1071
	mov	w0, 1
	str	w0, [x1,2884]
.L1071:
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
	beq	.L1074
	add	x5, x2, :lo12:.LANCHOR0
	uxtw	x0, w1
	lsl	x3, x0, 1
	ldr	x4, [x5,600]
	ldrh	w19, [x4,x3]
	cbnz	w19, .L1075
	ldr	x2, [x5,584]
	add	x0, x2, x0, lsl 2
	mov	w2, w19
	ldrb	w3, [x0,2]
	adrp	x0, .LC102
	add	x0, x0, :lo12:.LC102
	ubfx	x3, x3, 5, 3
	bl	printk
	b	.L1081
.L1075:
	sub	w19, w19, #1
	strh	w19, [x4,x3]
.L1074:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3224]
	cmp	w2, w1
	beq	.L1076
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1077
	strh	w1, [x19,3224]
.L1081:
	mov	w0, 0
	b	.L1076
.L1077:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldrh	w3, [x19,3890]
	cset	w0, ne
	ldrh	w2, [x19,3224]
	add	w3, w3, 1
	ldr	x1, [x29,40]
	strh	w1, [x19,3224]
	uxth	w3, w3
	cmp	w3, 7
	csel	w3, w3, wzr, ls
	strh	w3, [x19,3890]
	add	x3, x19, x3, uxth 1
	strh	w2, [x3,3232]
.L1076:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	adrp	x3, .LANCHOR0
	uxtb	w5, w0
	add	x6, x3, :lo12:.LANCHOR0
	uxth	w2, w2
	add	x4, x6, 3360
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,3872]
	ldr	x12, [x6,3880]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L1083:
	cmp	w4, w11
	beq	.L1087
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L1084
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR0
	bne	.L1085
	ldr	x6, [x4,3880]
	add	x4, x4, 3360
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L1086
.L1085:
	ldr	x4, [x4,3880]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L1086:
	add	x4, x3, :lo12:.LANCHOR0
	mov	w6, -1
	ldr	x4, [x4,3880]
	strh	w6, [x4,x7,lsl 1]
	b	.L1087
.L1084:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L1083
.L1087:
	add	x3, x3, :lo12:.LANCHOR0
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,3872]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 3360
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,3880]
	strh	w6, [x0,x4,lsl 1]
	beq	.L1089
	ldrh	w0, [x3,3340]
	mov	w2, 21
	mov	w4, 1
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w4, w2
	ldrb	w0, [x3,3338]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L1089:
	mov	w0, -1
	ldp	x29, x30, [sp], 16
	ret
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	stp	x29, x30, [sp, -48]!
	adrp	x3, .LANCHOR0
	mov	w2, 21
	add	x1, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	mov	w4, 1
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	ubfx	x21, x0, 21, 3
	mov	x19, x3
	ldrh	w5, [x1,3340]
	sub	w2, w2, w5
	lsr	w0, w0, w5
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x1,3338]
	udiv	w2, w0, w2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	uxth	w20, w2
	tbz	x0, 14, .L1094
	adrp	x0, .LC103
	mov	w1, w21
	add	x0, x0, :lo12:.LC103
	mov	w2, w20
	bl	printk
.L1094:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w1, w20
	bls	.L1093
	ldr	x0, [x0,584]
	add	x20, x0, x20, uxth 2
	mov	w0, 1
	lsl	w1, w0, w21
	ldrb	w0, [x20,3]
	orr	w1, w1, w0
	strb	w1, [x20,3]
.L1093:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
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
	ldr	x7, [x19,608]
	ldrh	w6, [x19,2854]
	ldrh	w1, [x19,2844]
	ldr	w0, [x7,528]
	str	w0, [sp]
	ldr	w0, [x19,2840]
	str	w0, [sp,8]
	adrp	x0, .LC104
	ldrh	w2, [x19,2846]
	add	x0, x0, :lo12:.LC104
	ldr	w7, [x7,524]
	ldrh	w3, [x19,2848]
	ldrh	w4, [x19,2850]
	ldrh	w5, [x19,2852]
	bl	printk
	ldr	x5, [x19,2864]
	adrp	x0, .LC105
	ldrb	w3, [x19,2857]
	add	x0, x0, :lo12:.LC105
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,2864]
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,2864]
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
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
	bl	printk
	ldrh	w1, [x19,2874]
	adrp	x0, .LC108
	ldrh	w2, [x19,2876]
	add	x0, x0, :lo12:.LC108
	ldrh	w3, [x19,2878]
	ldrh	w4, [x19,2880]
	ldrh	w5, [x19,2872]
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
	cbnz	x0, .L1101
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 662
	add	x1, x1, 1488
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L1110
.L1101:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w1, w0
	bls	.L1106
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 667
	add	x1, x1, 1488
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1106:
	ldrb	w0, [x20,56]
	cbz	w0, .L1103
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,2832]
	cmp	w1, w0
	bcs	.L1104
.L1103:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_free
	ldrb	w0, [x19,2856]
	b	.L1110
.L1104:
	add	x0, x21, 2888
	mov	x1, x20
	bl	buf_add_tail
	ldr	x0, [x21,2864]
	ldrb	w1, [x20,56]
	ldrb	w2, [x21,2856]
	ldr	w3, [x0,16]
	add	w2, w2, 1
	add	w1, w3, w1
	str	w1, [x0,16]
	ldr	w1, [x0,32]
	uxtb	w2, w2
	strb	w2, [x21,2856]
	add	w1, w1, 1
	str	w1, [x0,32]
	mov	w0, w2
.L1110:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_write_buf, .-ftl_write_buf
	.align	2
	.global	ftl_write_completed
	.type	ftl_write_completed, %function
ftl_write_completed:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x21, .LC109
	stp	x23, x24, [sp,48]
	add	x22, x19, 784
	mov	w23, 21
	mov	w24, 1
	add	x21, x21, :lo12:.LC109
.L1112:
	ldrb	w0, [x19,3892]
	cmp	w0, 255
	beq	.L1119
	sbfiz	x0, x0, 6, 32
	add	x20, x22, x0
	add	x4, x20, 32
	ldrb	w0, [x22,x0]
	strb	w0, [x19,3892]
	ldr	w0, [x20,52]
	cmn	w0, #1
	bne	.L1113
	ldrh	w2, [x19,3340]
	mov	x0, x21
	ldr	w3, [x4,8]
	sub	w1, w23, w2
	lsl	w1, w24, w1
	lsr	w2, w3, w2
	sub	w1, w1, #1
	and	w2, w1, w2
	ldrb	w1, [x19,3338]
	udiv	w1, w2, w1
	ldr	w2, [x4,4]
	uxth	w25, w1
	mov	w1, w25
	bl	printk
	mov	w1, 1
	mov	w0, w25
	mov	w2, 0
	bl	gc_add_sblk
	ldr	x0, [x19,608]
	ldr	w1, [x0,556]
	add	w1, w1, 1
	str	w1, [x0,556]
	ldrh	w1, [x0,16]
	cmp	w1, w25
	bne	.L1114
	strh	wzr, [x0,22]
	b	.L1115
.L1114:
	ldrh	w1, [x0,48]
	cmp	w1, w25
	bne	.L1115
	strh	wzr, [x0,54]
.L1115:
	mov	x0, x20
	bl	ftl_write_buf
	b	.L1112
.L1113:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x20,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x20,2]
	tbz	x0, 2, .L1117
	and	w0, w0, -3
	strb	w0, [x20,2]
	b	.L1112
.L1117:
	mov	x0, x20
	bl	buf_free
	b	.L1112
.L1119:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
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
	cbnz	x0, .L1121
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 949
	add	x1, x1, 1504
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	b	.L1120
.L1121:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w1, w0
	bls	.L1124
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 954
	add	x1, x1, 1504
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1124:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x1, x20
	add	x0, x19, 3893
	bl	buf_add_tail
	ldrb	w0, [x19,3894]
	add	w0, w0, 1
	strb	w0, [x19,3894]
.L1120:
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
	beq	.L1127
	adrp	x0, .LANCHOR0+584
	ldr	x21, [x0,#:lo12:.LANCHOR0+584]
	add	x21, x21, x19, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L1128
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 827
	add	x1, x1, 1528
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1128:
	ldrb	w1, [x21,2]
	bfi	w1, w20, 5, 3
	ubfx	x2, x1, 3, 2
	orr	w0, w22, w2
	bfi	w1, w0, 3, 2
	strb	w1, [x21,2]
	b	.L1134
.L1127:
	adrp	x21, .LC110
	bl	print_ftl_debug_info
	add	x21, x21, :lo12:.LC110
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
	mov	w2, 837
	add	x1, x1, 1528
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1134:
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
	strb	w1, [x0,2835]
	strb	w1, [x0,3892]
	strb	w1, [x0,3895]
	strb	w1, [x0,3256]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC111
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC111
	ldrb	w1, [x20,2835]
	bl	printk
	ldrb	w19, [x20,2835]
	cmp	w19, 255
	beq	.L1136
	add	x20, x20, 784
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC112
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC112
.L1138:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1136
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1138
.L1136:
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
	mov	w5, w0
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, 0
	ldrb	w2, [x3,2835]
	cmp	w2, 255
	beq	.L1146
	ldrh	w8, [x3,3340]
	mov	w4, 21
	ldrb	w7, [x3,3338]
	mov	w0, 1
	sub	w4, w4, w8
	ubfx	x9, x5, 21, 3
	lsl	w4, w0, w4
	sub	w7, w7, #1
	sub	w4, w4, #1
	asr	w5, w5, w8
	uxth	w7, w7
	adrp	x10, .L1149
	uxth	w4, w4
	and	w5, w5, w7
	and	w5, w5, w4
	add	x3, x3, 784
	add	x10, x10, :lo12:.L1149
.L1152:
	add	x0, x3, x2, lsl 6
	ldr	w6, [x0,40]
	ubfx	x11, x6, 21, 3
	cmp	w11, w9
	bne	.L1147
	lsr	w6, w6, w8
	ldrb	w0, [x0,58]
	and	w6, w7, w6
	and	w6, w4, w6
	cmp	w6, w5
	bne	.L1148
	cmp	w1, 1
	beq	.L1150
	b	.L1146
.L1148:
	cmp	w1, 3
	bhi	.L1147
	ldrb	w6, [x10,w1,uxtw]
	adr	x11, .Lrtx1149
	add	x6, x11, w6, sxtb #2
	br	x6
.Lrtx1149:
	.section	.rodata
	.align	0
	.align	2
.L1149:
	.byte	(.L1146 - .Lrtx1149) / 4
	.byte	(.L1150 - .Lrtx1149) / 4
	.byte	(.L1151 - .Lrtx1149) / 4
	.byte	(.L1146 - .Lrtx1149) / 4
	.text
.L1150:
	sub	w6, w0, #6
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L1147
	b	.L1146
.L1151:
	cmp	w0, 10
	bne	.L1146
.L1147:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L1152
	mov	w0, 0
.L1146:
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
	add	x4, x0, 784
	add	x12, x0, 3895
	add	x13, x0, 3892
	ldrb	w7, [x0,2835]
	ldrb	w9, [x0,3895]
	ldrb	w8, [x0,3256]
	ldrb	w10, [x0,3892]
.L1162:
	cmp	w7, 255
	beq	.L1163
	sbfiz	x6, x7, 6, 32
	sxtw	x1, w7
	add	x2, x4, x6
	ldrb	w2, [x2,58]
	sub	w2, w2, #11
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1164
	cbz	w5, .L1161
	strb	w7, [x0,2835]
	b	.L1161
.L1164:
	lsl	x5, x1, 6
	ldrb	w7, [x4,x6]
	add	x2, x4, x5
	strb	w11, [x4,x5]
	ldrb	w5, [x2,59]
	cmp	w5, 1
	bne	.L1167
	ldrh	w2, [x2,50]
	cbnz	w2, .L1175
	mov	w2, w10
	mov	x5, x13
	b	.L1168
.L1167:
	cbz	w5, .L1185
.L1169:
	mov	w5, 1
	b	.L1162
.L1185:
	ldr	w2, [x2,36]
	cmn	w2, #1
	beq	.L1169
	mov	w2, w9
	mov	x5, x12
	b	.L1168
.L1175:
	mov	w2, w8
	add	x5, x0, 3256
.L1168:
	cmp	w2, 255
	bne	.L1173
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x3, x1, lsl 6
	strb	w7, [x3,2835]
	ldrb	w0, [x1,785]
	strb	w0, [x5]
	b	.L1161
.L1173:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x4,x2]
	cmp	w2, 255
	bne	.L1173
	add	x1, x4, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1,1]
	strb	w1, [x4,x5]
	b	.L1169
.L1163:
	cbz	w5, .L1161
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, -1
	strb	w0, [x3,2835]
.L1161:
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
	ldr	x2, [x3,2864]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	ldrb	w1, [x3,3276]
	strh	w0, [x2,690]
	cmp	w1, w0
	bls	.L1187
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1188
.L1187:
	adrp	x22, .LC113
	add	x22, x22, :lo12:.LC113
.L1200:
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_alloc_sblk
	uxth	w21, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x20,2864]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L1189
	mov	w1, w21
	mov	x0, x22
	bl	printk
	ldr	x2, [x20,584]
	add	x1, x2, x21, uxth 2
	ldrb	w2, [x1,2]
	orr	w2, w2, -32
	strb	w2, [x1,2]
	b	.L1200
.L1189:
	ldr	x1, [x20,2864]
	mov	w0, 1
	mov	w2, 65535
	str	w0, [x20,3896]
	mov	x20, 0
	strh	wzr, [x1,690]
.L1191:
	add	x3, x1, x20, lsl 1
	uxth	w0, w20
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L1190
	add	x20, x20, 1
	cmp	x20, 128
	bne	.L1191
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 194
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, w20
.L1190:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2864]
	add	x0, x1, x0, sxtw 1
	strh	w21, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L1188:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2864]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w20, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L1193
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 199
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1193:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 21
	ldr	x1, [x19,2864]
	ldrh	w2, [x19,3340]
	sub	w0, w0, w2
	strh	wzr, [x1,696]
	asr	w4, w20, w0
	strh	w20, [x1,692]
	strh	w4, [x1,694]
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L1197
	mov	w2, 1
	mov	w1, w20
	lsl	w2, w2, w0
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	sub	w2, w2, #1
	mov	w3, w20
	uxth	w4, w4
	bl	printk
.L1197:
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
	adrp	x2, .LANCHOR0
	mov	w3, 65535
	add	x29, sp, 0
	str	x19, [sp,16]
	add	x2, x2, :lo12:.LANCHOR0
	mov	x19, 0
.L1204:
	add	x1, x2, 3904
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1203
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1204
	mov	x0, 0
	mov	w3, 32768
.L1206:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1205
	cmp	w2, w3
	bcs	.L1205
	mov	w3, w2
	mov	w19, w4
.L1205:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1206
	cmp	w19, 32
	mov	w0, w19
	bne	.L1203
	adrp	x0, .LANCHOR4+64
	mov	w2, -1
	mov	w1, 0
	ldrb	w3, [x0,#:lo12:.LANCHOR4+64]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3904
.L1208:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1207
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1207:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1208
	cmp	w19, 32
	bne	.L1209
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 297
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1209:
	mov	w0, w19
.L1203:
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
	cbnz	w0, .L1217
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1218:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1218
.L1224:
	adrp	x20, .LANCHOR4
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1024
	add	x0, x20, 68
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, 0
	b	.L1219
.L1217:
	cmp	w0, 1
	bne	.L1220
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L1223:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L1221
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1221:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1223
	b	.L1224
.L1220:
	cmp	w0, 2
	bne	.L1225
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L1227:
	add	x3, x4, 4
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L1227
	b	.L1224
.L1225:
	cmp	w0, 3
	bne	.L1228
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L1231:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L1229
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1229:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1231
	b	.L1224
.L1228:
	cmp	w0, 4
	bne	.L1232
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
.L1234:
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
	bne	.L1234
	b	.L1224
.L1232:
	cmp	w0, 5
	bne	.L1235
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1236:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1236
	mov	x0, 0
.L1237:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L1237
	b	.L1224
.L1235:
	cmp	w0, 8
	bne	.L1224
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1238:
	add	x1, x2, 4
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L1238
	b	.L1224
.L1219:
	add	x1, x19, 4
	add	x2, x20, 68
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x2,w1,sxtw 1]
	bne	.L1219
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
	tbz	x0, 12, .L1255
	adrp	x1, .LANCHOR3
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L1255:
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	add	x25, x22, :lo12:.LANCHOR4
	mov	w2, 8
	ldrh	w0, [x24,34]
	mov	x23, 0
	ldrb	w1, [x24,20]
	mov	w26, 2
	strh	w0, [x20,2]
	add	x27, x24, 9
	ldrh	w0, [x24,18]
	add	x28, x20, 748
	strb	wzr, [x20,633]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x25,1092]
	add	x0, x20, 736
	bl	ftl_memset
	add	x0, x25, 1096
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
.L1257:
	add	x0, x20, 744
	ldrb	w2, [x24,8]
	add	x1, x28, x23, lsl 3
	strb	w26, [x23,x0]
	mov	x0, x27
	bl	flash_mem_cmp8
	cbnz	w0, .L1256
	ldrb	w1, [x20,633]
	add	x2, x25, 1096
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	strb	w0, [x20,633]
	uxtb	w0, w23
	add	x1, x20, x1, sxtw
	strb	w0, [x1,736]
	bl	zftl_flash_enter_slc_mode
.L1256:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L1257
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 8
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1258
.L1262:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR4
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,21]
	ldrb	w0, [x21,633]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,22]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1128]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1258:
	add	x23, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w26, [x0,8]
	mov	x24, 0
	and	w20, w20, 65280
	ldrb	w27, [x1,23]
	ldrh	w2, [x23,2]
	add	x25, x0, 9
	add	x3, x23, 748
	add	x4, x22, :lo12:.LANCHOR4
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L1261:
	add	x1, x3, x24, lsl 3
	mov	x0, x25
	mov	w2, w26
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1259
	ldrb	w0, [x23,633]
	add	x1, x4, 1096
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x1,w0,sxtw 2]
	add	w1, w0, 1
	add	x0, x23, x0, sxtw
	strb	w1, [x23,633]
	strb	w24, [x0,736]
.L1259:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1261
	b	.L1262
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
	add	x0, x19, 3360
	bl	ftl_memset
	ldrh	w0, [x19,3300]
	mov	w1, 255
	ldrb	w2, [x19,3276]
	mul	w2, w0, w2
	ldr	x0, [x19,3880]
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
	tbz	x0, 12, .L1273
	adrp	x1, .LANCHOR3
	adrp	x0, .LC115
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	add	x1, x1, 72
	mov	w2, 222
	mov	w3, 0
	bl	printk
.L1273:
	adrp	x19, .LANCHOR0
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 512
	add	x0, x19, 3360
	bl	ftl_memset
	ldrh	w0, [x19,3300]
	mov	w1, 255
	ldrb	w2, [x19,3276]
	mul	w2, w0, w2
	ldr	x0, [x19,3880]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L1274:
	ldrh	w1, [x19,3300]
	ldrb	w2, [x19,3276]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1283
	uxtw	x2, w0
	ldr	x1, [x19,3872]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1275
	uxtb	w1, w1
	add	x3, x19, 3360
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,3880]
	strh	w4, [x1,x2,lsl 1]
.L1275:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1274
.L1283:
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
.L1285:
	cmp	w4, w1, uxtb
	bls	.L1287
	add	x2, x0, 736
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1285
.L1287:
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
	mov	w2, 328
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	str	x21, [sp,32]
	add	x21, x19, 2896
	mov	x0, x21
	strb	wzr, [x19,3273]
	strb	wzr, [x20,1130]
	str	wzr, [x20,1132]
	strh	wzr, [x19,3304]
	bl	ftl_memset
	ldrh	w1, [x19,3300]
	mov	w0, -1
	ldrb	w3, [x19,3276]
	strh	w0, [x19,2896]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x19,2932]
	strh	w0, [x19,2878]
	ldrh	w0, [x19,3274]
	strh	w2, [x19,2934]
	mul	w2, w1, w3
	mul	w0, w0, w3
	strh	w1, [x19,2880]
	uxth	w2, w2
	lsl	w0, w0, 2
	sub	w4, w2, #32
	strh	w2, [x19,2874]
	strh	w4, [x19,2876]
	strh	wzr, [x19,3302]
	strh	wzr, [x19,3296]
	strh	wzr, [x19,3298]
	str	xzr, [x19,2904]
	bl	ftl_malloc
	ldrb	w1, [x19,3276]
	str	x0, [x20,1136]
	ldrh	w0, [x19,3274]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1144]
	ldrh	w1, [x19,3274]
	ldrb	w0, [x19,3276]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3264]
	ldrb	w1, [x19,3276]
	ldrh	w0, [x19,3274]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3248]
	ldrb	w1, [x19,3276]
	ldrh	w0, [x19,3274]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1152]
	ldrh	w0, [x19,3306]
	ldr	x21, [sp,32]
	lsr	w0, w0, 2
	strh	w0, [x19,2872]
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
	ldr	x0, [x0,2864]
	ldr	w1, [x0,32]
	mov	w0, 10240
	cmp	w1, w0
	bls	.L1290
	bl	ftl_tmp_into_update
.L1290:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x0, [x3,608]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L1291
	ldr	x3, [x3,2864]
	ldr	w1, [x0,572]
	ldr	w3, [x3,36]
	add	w1, w1, 32
	cmp	w3, w1
	bls	.L1341
.L1291:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w22, 0
	mov	w20, 65535
	adrp	x8, .LC116
	mov	w28, w22
	str	w22, [x29,136]
	ldr	x1, [x1,2864]
	mov	w27, w22
	str	w22, [x29,140]
	mov	w26, w22
	mov	w25, w22
	mov	w24, w22
	ldr	w3, [x1,36]
	mov	w21, w20
	str	w3, [x0,572]
	mov	w9, w20
	str	w2, [x0,568]
	adrp	x10, .LANCHOR2
	add	x8, x8, :lo12:.LC116
	ldrh	w23, [x1,134]
.L1293:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,576]
	cmp	w0, w23
	bls	.L1402
	uxtw	x7, w23
	ldr	x0, [x4,584]
	lsl	x1, x7, 2
	add	x6, x0, x1
	ldrb	w2, [x6,2]
	and	w3, w2, 224
	cmp	w3, 224
	beq	.L1294
	tbz	x2, 3, .L1295
	ldrh	w3, [x0,x1]
	ldr	w2, [x0,x1]
	and	w3, w3, 2047
	ubfx	x2, x2, 11, 8
	b	.L1296
.L1295:
	tst	w2, 24
	bne	.L1297
	ldrh	w3, [x0,x1]
	mov	w2, 65535
	and	w3, w3, 2047
	b	.L1296
.L1297:
	ldr	w2, [x0,x1]
	mov	w3, 65535
	ubfx	x2, x2, 11, 8
	b	.L1298
.L1296:
	ldr	w4, [x29,136]
	add	w26, w26, 1
	cmp	w20, w3
	add	w4, w4, w3
	str	w4, [x29,136]
	uxth	w26, w26
	bls	.L1340
	add	x4, x19, :lo12:.LANCHOR0
	mov	w20, w3
	ldr	x4, [x4,608]
	strh	w23, [x4,586]
.L1340:
	cmp	w25, w3
	bcs	.L1299
	mov	w22, w23
	mov	w25, w3
.L1299:
	cmp	w2, w9
	beq	.L1300
.L1298:
	ldr	w4, [x29,140]
	add	w27, w27, 1
	cmp	w21, w2
	add	w4, w4, w2
	str	w4, [x29,140]
	uxth	w27, w27
	bls	.L1301
	add	x4, x19, :lo12:.LANCHOR0
	mov	w21, w2
	ldr	x4, [x4,608]
	strh	w23, [x4,584]
.L1301:
	cmp	w24, w2
	bcs	.L1300
	mov	w28, w23
	mov	w24, w2
.L1300:
	cmp	w3, 9
	bls	.L1343
	cmp	w2, 9
	bhi	.L1294
.L1343:
	ldr	w2, [x10,#:lo12:.LANCHOR2]
	tbz	x2, 8, .L1294
	ldrh	w2, [x0,x1]
	ldr	w3, [x0,x1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w5, [x6,2]
	mov	w1, w23
	ldrb	w6, [x6,3]
	and	w2, w2, 2047
	ldr	x11, [x0,600]
	ubfx	x4, x5, 3, 2
	mov	x0, x8
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x10, [x29,112]
	ldrh	w7, [x11,x7,lsl 1]
	str	x9, [x29,120]
	str	x8, [x29,128]
	bl	printk
	ldr	x10, [x29,112]
	ldr	x9, [x29,120]
	ldr	x8, [x29,128]
.L1294:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1293
.L1402:
	ldr	x0, [x4,608]
	mov	w1, 255
	mov	w2, 128
	str	x4, [x29,128]
	add	x0, x0, 264
	adrp	x23, .LANCHOR2
	strh	wzr, [x0,-142]
	bl	ftl_memset
	ldr	x4, [x29,128]
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x0, [x4,608]
	ldr	x3, [x4,584]
	ldrh	w7, [x0,586]
	lsl	x0, x7, 2
	mov	x1, x7
	add	x8, x3, x0
	tbz	x2, 10, .L1304
	ldr	x9, [x4,600]
	ldrh	w2, [x3,x0]
	ldrb	w5, [x8,2]
	ldr	w3, [x3,x0]
	adrp	x0, .LC117
	ldrb	w6, [x8,3]
	ubfx	x4, x5, 3, 2
	ldrh	w7, [x9,x7,lsl 1]
	add	x0, x0, :lo12:.LC117
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x8, [x29,128]
	bl	printk
	ldr	x8, [x29,128]
.L1304:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1305
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1306
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,128]
	ldr	x0, [x3,608]
	ldrh	w0, [x0,586]
	bl	gc_add_sblk
	ldr	x3, [x29,128]
	mov	w0, 1
	strh	w0, [x3,3214]
	b	.L1305
.L1306:
	tst	w0, 224
	bne	.L1305
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x2,608]
	ldrh	w3, [x1,590]
	cmp	w3, w0
	bne	.L1305
	ldrh	w0, [x1,586]
	ldrh	w1, [x1,588]
	str	x2, [x29,128]
	cmp	w1, w0
	beq	.L1305
	bl	zftl_remove_free_node
	ldr	x2, [x29,128]
	ldr	x0, [x2,608]
	ldrh	w1, [x0,586]
	strh	w1, [x0,590]
	mov	w1, -1
	strh	w1, [x0,586]
.L1305:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x0,608]
	ldr	x4, [x0,584]
	ldrh	w7, [x1,584]
	lsl	x3, x7, 2
	mov	x1, x7
	add	x8, x4, x3
	tbz	x2, 10, .L1307
	ldr	x9, [x0,600]
	adrp	x0, .LC118
	ldrh	w2, [x4,x3]
	add	x0, x0, :lo12:.LC118
	ldrb	w5, [x8,2]
	ldr	w3, [x4,x3]
	and	w2, w2, 2047
	ldrb	w6, [x8,3]
	ubfx	x4, x5, 3, 2
	ldrh	w7, [x9,x7,lsl 1]
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	str	x8, [x29,128]
	bl	printk
	ldr	x8, [x29,128]
.L1307:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1308
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1309
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 1
	str	x3, [x29,128]
	ldr	x0, [x3,608]
	ldrh	w0, [x0,584]
	bl	gc_add_sblk
	ldr	x3, [x29,128]
	mov	w0, 1
	strh	w0, [x3,3214]
	b	.L1308
.L1309:
	tst	w0, 224
	bne	.L1308
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x2,608]
	ldrh	w3, [x1,588]
	cmp	w3, w0
	bne	.L1308
	ldrh	w0, [x1,584]
	ldrh	w1, [x1,590]
	str	x2, [x29,128]
	cmp	w1, w0
	beq	.L1308
	bl	zftl_remove_free_node
	ldr	x2, [x29,128]
	ldr	x0, [x2,608]
	ldrh	w1, [x0,584]
	strh	w1, [x0,588]
	mov	w1, -1
	strh	w1, [x0,584]
.L1308:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1310
	uxtw	x7, w22
	ldr	x8, [x4,600]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w7, [x8,x7,lsl 1]
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC119
	ldrb	w5, [x6,2]
	add	x0, x0, :lo12:.LC119
	ldrb	w6, [x6,3]
	mov	w1, w22
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1310:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1311
	uxtw	x7, w28
	ldr	x8, [x4,600]
	lsl	x0, x7, 2
	add	x6, x1, x0
	ldrh	w7, [x8,x7,lsl 1]
	ldrh	w2, [x1,x0]
	ldr	w3, [x1,x0]
	adrp	x0, .LC120
	ldrb	w5, [x6,2]
	add	x0, x0, :lo12:.LC120
	ldrb	w6, [x6,3]
	mov	w1, w28
	ubfx	x4, x5, 3, 2
	and	w2, w2, 2047
	ubfx	x3, x3, 11, 8
	ubfx	x5, x5, 5, 3
	bl	printk
.L1311:
	ldr	w1, [x29,136]
	add	x0, x19, :lo12:.LANCHOR0
	udiv	w3, w1, w26
	ldr	w1, [x29,140]
	ldr	x0, [x0,2864]
	strh	w25, [x0,96]
	strh	w24, [x0,98]
	strh	w20, [x0,92]
	strh	w21, [x0,94]
	udiv	w4, w1, w27
	strh	w3, [x0,88]
	strh	w4, [x0,90]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1312
	adrp	x0, .LC121
	mov	w1, w26
	add	x0, x0, :lo12:.LC121
	mov	w2, w27
	uxth	w3, w3
	uxth	w4, w4
	bl	printk
.L1312:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	adrp	x28, .LANCHOR4
	tbz	x0, 10, .L1313
	add	x6, x28, :lo12:.LANCHOR4
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	mov	w1, w20
	mov	w2, w21
	mov	w3, w25
	ldrh	w5, [x6,1160]
	mov	w4, w24
	ldrh	w6, [x6,1162]
	bl	printk
.L1313:
	add	x1, x28, :lo12:.LANCHOR4
	sub	w0, w24, w21
	str	w0, [x29,140]
	ldrh	w0, [x1,1162]
	ldr	w2, [x29,140]
	cmp	w2, w0
	bgt	.L1314
	ldrh	w1, [x1,1160]
	sub	w0, w25, w20
	cmp	w0, w1
	ble	.L1342
.L1314:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w26, 0
	mov	w22, w26
	ldr	x1, [x0,608]
	ldr	x0, [x0,2864]
	ldrh	w24, [x1,580]
	ldrh	w27, [x0,134]
	sub	w0, w25, w20
	str	w0, [x29,136]
	adrp	x0, .LC124
	add	x0, x0, :lo12:.LC124
	adrp	x25, .LC123
	str	x0, [x29,128]
	add	x0, x25, :lo12:.LC123
	str	x0, [x29,120]
.L1316:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,576]
	cmp	w27, w0
	bcs	.L1325
	add	w7, w24, 1
	ldr	x4, [x1,584]
	uxth	w24, w7
	cmp	w24, w0
	csel	w24, w24, wzr, cc
	uxtw	x25, w24
	lsl	x3, x25, 2
	add	x0, x4, x3
	ldrb	w0, [x0,2]
	and	w2, w0, 224
	cmp	w2, 224
	beq	.L1318
	tst	w0, 192
	beq	.L1318
	ubfx	x0, x0, 3, 2
	cmp	w2, 160
	and	w1, w0, 1
	tbnz	x0, 0, .L1400
	cmp	w0, 2
.L1400:
	add	x0, x28, :lo12:.LANCHOR4
	bne	.L1321
	ldrh	w0, [x0,1162]
	ldr	w2, [x29,140]
	cmp	w2, w0
	ble	.L1322
	ldr	w0, [x4,x3]
	ubfx	x0, x0, 11, 8
	cmp	w0, w21
	bls	.L1323
	cbz	w1, .L1322
	ldrh	w0, [x4,x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1322
.L1323:
	mov	w1, 0
	mov	w0, w24
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w22, w22, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 2896
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,3214]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1322
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,120]
	ldrb	w2, [x1,2]
	b	.L1401
.L1321:
	ldrh	w0, [x0,1160]
	ldr	w2, [x29,136]
	cmp	w2, w0
	ble	.L1322
	ldrh	w0, [x4,x3]
	add	w2, w20, 8
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1324
	cbz	w1, .L1322
	ldr	w0, [x4,x3]
	add	w1, w21, 4
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1322
.L1324:
	mov	w1, 0
	mov	w0, w24
	mov	w2, 1
	str	x4, [x29,104]
	str	x3, [x29,112]
	add	w26, w26, 1
	bl	gc_add_sblk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	add	x9, x0, 2896
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	strh	w1, [x0,3214]
	ldr	w1, [x23,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1322
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,128]
	ldrb	w2, [x1,2]
.L1401:
	ldrh	w3, [x10,x25,lsl 1]
	mov	w1, w24
	ldrh	w4, [x9,56]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1322:
	cmp	w26, 4
	bhi	.L1325
	cmp	w22, 4
	bhi	.L1325
.L1318:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1316
.L1325:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,608]
	str	w24, [x0,580]
	b	.L1315
.L1342:
	mov	w26, 0
	mov	w22, w26
.L1315:
	cbz	w21, .L1328
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w0, [x0,134]
.L1329:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1403
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w21
	bcc	.L1330
	ldrb	w5, [x5,2]
	tst	w5, 24
	beq	.L1330
	sub	w2, w2, w21
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1330:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1329
.L1403:
	ldr	x0, [x1,2864]
	ldrh	w1, [x0,72]
	add	w1, w21, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w21
	bls	.L1328
	sub	w21, w1, w21
	strh	w21, [x0,98]
.L1328:
	cbz	w20, .L1334
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w0, [x0,134]
.L1335:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1404
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L1336
	ldrb	w5, [x5,2]
	and	w5, w5, 24
	cmp	w5, 16
	beq	.L1336
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1336:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1335
.L1404:
	ldr	x0, [x1,2864]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L1334
	sub	w20, w1, w20
	strh	w20, [x0,96]
.L1334:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1292
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x19,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x19,3300]
	cmp	w2, w1
	bhi	.L1292
	mov	w1, 0
	mov	w2, 1
	add	w22, w22, 1
	bl	gc_add_sblk
	b	.L1292
.L1341:
	mov	w26, 0
	mov	w22, w26
.L1292:
	add	w0, w26, w22
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
	adrp	x26, .LC125
	ldr	x0, [x19,528]
	mov	w25, -1
	add	x26, x26, :lo12:.LC125
	mul	w2, w2, w24
	bl	ftl_memset
	ldrh	w22, [x19,3274]
	mov	w0, 16
	strh	w0, [x19,3336]
	ldrb	w0, [x19,3276]
	str	xzr, [x19,3312]
	str	xzr, [x19,3320]
	mul	w22, w0, w22
	str	xzr, [x19,3328]
	str	xzr, [x19,3288]
	sdiv	w22, w23, w22
	str	xzr, [x19,616]
	str	xzr, [x19,3280]
	strh	wzr, [x19,2844]
	strh	wzr, [x19,2846]
	strh	wzr, [x19,2848]
	strh	wzr, [x19,2850]
	strh	wzr, [x19,2854]
	strh	wzr, [x19,2852]
	sxth	w0, w22
	str	w0, [x29,108]
	ldr	x0, [x19,2864]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1406:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w19, w1
	bge	.L1436
	sxtw	x22, w19
	ldr	x27, [x0,584]
	ldr	w1, [x29,108]
	add	x27, x27, x22, lsl 2
	ldrb	w2, [x27,3]
	cbz	w2, .L1407
	ldrb	w5, [x0,3276]
	ldrh	w6, [x0,3274]
	ldr	x4, [x0,2864]
	mov	w0, 0
	mov	w1, w0
.L1408:
	cmp	w0, w5
	bge	.L1437
	ldrb	w2, [x27,3]
	asr	w2, w2, w0
	tbnz	x2, 0, .L1409
	add	w1, w6, w1
	sxth	w1, w1
	b	.L1410
.L1409:
	ldrh	w2, [x4,146]
	add	w2, w2, 1
	strh	w2, [x4,146]
.L1410:
	add	w0, w0, 1
	b	.L1408
.L1437:
	cbz	w1, .L1412
	sdiv	w1, w23, w1
	add	w1, w1, 1
	sxth	w1, w1
	b	.L1407
.L1412:
	ldrb	w0, [x27,2]
	orr	w0, w0, -32
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	strh	w25, [x0,x22,lsl 1]
.L1407:
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
	beq	.L1415
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1415
	ldr	x2, [x20,608]
	ldrh	w4, [x2,16]
	cmp	w19, w4
	beq	.L1415
	ldrh	w4, [x2,48]
	cmp	w19, w4
	beq	.L1415
	ldrh	w2, [x2,80]
	cmp	w19, w2
	beq	.L1415
	cmp	w0, 64
	bne	.L1417
	uxth	w27, w19
	add	x0, x20, 616
	mov	w1, w27
	add	x2, x20, 2850
	b	.L1434
.L1417:
	cmp	w0, 96
	bne	.L1418
	uxth	w27, w19
	add	x0, x20, 3288
	mov	w1, w27
	add	x2, x20, 2852
	b	.L1434
.L1418:
	cmp	w0, 160
	bne	.L1419
	uxth	w27, w19
	add	x0, x20, 3280
	mov	w1, w27
	add	x2, x20, 2854
.L1434:
	bl	_insert_data_list
	ldr	x0, [x20,600]
	ldrh	w0, [x0,x22,lsl 1]
	cmp	w0, 7
	bhi	.L1415
	mov	w0, w27
	mov	w1, 1
	mov	w2, w28
	b	.L1432
.L1419:
	cbnz	w0, .L1415
	ldr	x0, [x20,600]
	ldrh	w2, [x0,x22,lsl 1]
	cbz	w2, .L1420
	mov	x0, x26
	mov	w1, w19
	bl	printk
	ldrb	w0, [x27,2]
	tbz	x0, 4, .L1421
	mov	w1, 5
	b	.L1428
.L1421:
	mov	w1, 2
.L1428:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x27,2]
	mov	w1, 1
	mov	w0, w19
.L1432:
	bl	gc_add_sblk
	b	.L1415
.L1420:
	ands	w1, w1, 24
	add	x2, x21, :lo12:.LANCHOR0
	bne	.L1423
	add	x0, x2, 3312
	mov	w1, w19
	add	x2, x2, 2844
	b	.L1430
.L1423:
	cmp	w1, 16
	bne	.L1424
	add	x0, x2, 3320
	mov	w1, w19
	add	x2, x2, 2846
	b	.L1430
.L1424:
	add	x0, x2, 3328
	mov	w1, w19
	add	x2, x2, 2848
.L1430:
	bl	_insert_free_list
.L1415:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1406
.L1436:
	ldr	x1, [x0,2864]
	ldrh	w2, [x0,2844]
	strh	w2, [x1,114]
	ldrh	w2, [x0,2846]
	strh	w2, [x1,118]
	ldrh	w2, [x0,2848]
	strh	w2, [x1,116]
	ldrh	w2, [x0,2850]
	strh	w2, [x1,122]
	ldrh	w2, [x0,2854]
	ldrh	w0, [x0,2852]
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
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LC113
	adrp	x23, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	add	x25, x0, 16
	mov	x26, x23
	add	x20, x20, :lo12:.LC113
	mov	w24, -1
.L1439:
.L1442:
	mov	w0, w21
	bl	ftl_alloc_sblk
	uxth	w22, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	mov	x1, x25
	mov	w0, w22
	bl	ftl_get_blk_list_in_sblk
	uxtb	w2, w0
	add	x0, x23, :lo12:.LANCHOR0
	strh	w22, [x19]
	strb	w2, [x19,9]
	cmp	w21, 2
	strh	wzr, [x19,2]
	mov	w3, 0
	ldrh	w1, [x0,3300]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w2, w1, w2
	strh	w2, [x19,6]
	strb	w21, [x19,4]
	beq	.L1440
	ldrb	w3, [x0,3276]
	mul	w3, w1, w3
	uxth	w3, w3
.L1440:
	add	x27, x26, :lo12:.LANCHOR0
	strh	w3, [x19,12]
	ubfiz	x28, x22, 1, 16
	ldrb	w2, [x27,3276]
	ldr	x0, [x27,3872]
	add	x0, x0, x3, uxth 2
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,600]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L1438
	mov	x0, x20
	mov	w1, w22
	bl	printk
	ldr	x0, [x27,600]
	strh	w24, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L1442
.L1438:
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
	stp	x29, x30, [sp, -368]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR4
	add	x0, x23, :lo12:.LANCHOR4
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrh	w0, [x0,1164]
	cmp	w0, 128
	bls	.L1445
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 78
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1445:
	add	x28, x29, 112
	mov	w1, 0
	mov	w2, 256
	mov	x0, x28
	adrp	x21, .LANCHOR0
	bl	ftl_memset
	add	x2, x21, :lo12:.LANCHOR0
	mov	w0, 21
	mov	w1, 1
	mov	x3, 0
	ldrh	w6, [x2,3340]
	ldr	x4, [x2,2864]
	sub	w0, w0, w6
	ldrb	w8, [x2,3338]
	lsl	w0, w1, w0
	add	x1, x23, :lo12:.LANCHOR4
	ldrh	w7, [x4,698]
	sub	w0, w0, #1
	ldrh	w9, [x1,1164]
.L1446:
	cmp	w7, w3, uxth
	bls	.L1471
	add	x1, x4, x3, lsl 2
	mov	x2, 0
	ldr	w1, [x1,704]
	lsr	w1, w1, w6
	and	w1, w1, w0
	udiv	w1, w1, w8
	uxth	w1, w1
.L1447:
	cmp	w9, w2, uxth
	bls	.L1472
	add	x5, x4, x2, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L1448
	ldrh	w5, [x28,x2,lsl 1]
	add	w5, w5, 1
	strh	w5, [x28,x2,lsl 1]
.L1448:
	add	x2, x2, 1
	b	.L1447
.L1472:
	add	x3, x3, 1
	b	.L1446
.L1471:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w25, 0
	adrp	x24, .LC126
	mov	w19, w25
	adrp	x26, .LANCHOR2
	mov	w27, -1
	ldrb	w0, [x1,3276]
	add	x24, x24, :lo12:.LC126
	ldrh	w20, [x1,3300]
	mul	w20, w0, w20
	uxth	w20, w20
.L1451:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,1164]
	cmp	w0, w19
	bls	.L1473
	add	x0, x21, :lo12:.LANCHOR0
	sxtw	x22, w19
	add	x1, x22, 208
	ldr	x4, [x0,2864]
	ldrb	w2, [x0,3338]
	ldrh	w3, [x4,x1,lsl 1]
	ldrh	w1, [x4,692]
	sdiv	w1, w1, w2
	cmp	w1, w3
	bne	.L1452
	ldrb	w1, [x0,3276]
	ldrh	w0, [x0,3300]
	mul	w0, w1, w0
	strh	w0, [x28,w19,sxtw 1]
.L1452:
	ldrh	w0, [x28,x22,lsl 1]
	cmp	w20, w0
	bls	.L1453
	cmp	w0, wzr
	csel	w25, w25, w19, eq
	csel	w20, w20, w0, eq
.L1453:
	cbnz	w0, .L1455
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1455
	ldr	w0, [x26,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1456
	ldrh	w4, [x4,688]
	mov	x0, x24
	mov	w1, w19
	mov	w2, 0
	bl	printk
.L1456:
	add	x1, x21, :lo12:.LANCHOR0
	add	x22, x22, 208
	str	x1, [x29,104]
	ldr	x0, [x1,2864]
	ldrh	w0, [x0,x22,lsl 1]
	bl	ftl_free_sblk
	ldr	x1, [x29,104]
	ldr	x0, [x1,2864]
	strh	w27, [x0,x22,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L1455:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1451
.L1473:
	mov	w0, w25
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
	tbz	x0, 12, .L1476
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 100
	add	x2, x2, 112
	bl	printk
.L1476:
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
.L1481:
	cmp	w2, w3
	bls	.L1483
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1481
.L1483:
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
	.global	js_hash
	.type	js_hash, %function
js_hash:
	mov	x5, x0
	mov	w0, 42982
	mov	x2, 0
	movk	w0, 0x47c6, lsl 16
.L1486:
	cmp	w1, w2
	bls	.L1488
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L1486
.L1488:
	ret
	.size	js_hash, .-js_hash
	.align	2
	.global	timer_get_time
	.type	timer_get_time, %function
timer_get_time:
	adrp	x0, jiffies
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0,#:lo12:jiffies]
	adrp	x0, .LANCHOR4+1168
	ldr	x0, [x0,#:lo12:.LANCHOR4+1168]
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
	adrp	x0, .LANCHOR4+1176
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
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
	adrp	x1, .LANCHOR4+1176
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR4+1176]
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
	adrp	x3, .LANCHOR4+1176
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1176]
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
	adrp	x3, .LANCHOR4+1176
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1176]
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
	adrp	x4, .LANCHOR4+1184
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1184]
	add	x4, x4, 4096
	cbnz	w2, .L1495
	add	x1, x4, x1
	b	.L1497
.L1495:
	add	x0, x4, x1
	mov	x1, x6
.L1497:
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
	adrp	x0, .LC128
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC128
	bl	printk
	adrp	x0, .LANCHOR4+1176
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
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
	adrp	x0, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
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
	adrp	x1, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1176]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1176]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR4+1176
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1176]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1176]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1176]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR4+1176
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1176]
	ldr	x1, [x1,112]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x19, 0
	adrp	x23, .LC129
	adrp	x21, .LC130
	adrp	x24, .LC1
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC129
	add	x21, x21, :lo12:.LC130
	add	x24, x24, :lo12:.LC1
.L1511:
	cmp	x19, x26
	beq	.L1519
	cbnz	w20, .L1512
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L1512:
	cmp	w25, 4
	mov	x0, x21
	bne	.L1513
	ldr	w1, [x22,x19,lsl 2]
	b	.L1518
.L1513:
	cmp	w25, 2
	bne	.L1515
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L1518
.L1515:
	ldrb	w1, [x22,x19]
.L1518:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L1516
	adrp	x1, .LC131
	mov	x0, x24
	add	x1, x1, :lo12:.LC131
	mov	w20, 0
	bl	printk
.L1516:
	add	x19, x19, 1
	b	.L1511
.L1519:
	adrp	x0, .LC1
	adrp	x1, .LC131
	add	x1, x1, :lo12:.LC131
	add	x0, x0, :lo12:.LC1
	bl	printk
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	hynix_get_read_retry_default
	.type	hynix_get_read_retry_default, %function
hynix_get_read_retry_default:
	stp	x29, x30, [sp, -144]!
	mov	w2, -83
	mov	w1, -82
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	stp	x25, x26, [sp,64]
	mov	w25, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	cmp	w25, 2
	str	x19, [x29,128]
	ldr	x27, [x0,728]
	add	x0, x27, 112
	str	x0, [x29,136]
	mov	w0, -84
	strb	w25, [x27,112]
	strb	w0, [x27,128]
	mov	w0, -81
	strb	w2, [x27,129]
	add	x26, x27, 128
	strb	w1, [x27,130]
	strb	w0, [x27,131]
	bne	.L1521
	ldr	x1, [x29,136]
	mov	w0, -89
	strb	w0, [x1,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1576
.L1521:
	cmp	w25, 3
	bne	.L1523
	mov	x4, 0
.L1524:
	sub	w0, w4, #80
	strb	w0, [x26,x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L1524
	mov	w21, w4
	mov	w28, w4
	b	.L1522
.L1523:
	cmp	w25, 4
	bne	.L1525
	mov	w4, -52
	strb	w4, [x27,128]
	mov	w4, -65
	strb	w4, [x27,129]
	mov	w4, -86
	mov	w21, 8
	strb	w4, [x27,130]
	mov	w4, -85
	strb	w2, [x27,133]
	mov	w28, w21
	strb	w4, [x27,131]
	mov	w4, -51
	strb	w1, [x27,134]
	strb	w4, [x27,132]
	strb	w0, [x27,135]
	b	.L1522
.L1525:
	cmp	w25, 5
	bne	.L1526
	mov	w0, 56
	strb	w0, [x27,128]
	mov	w0, 57
	strb	w0, [x27,129]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x27,130]
	mov	w0, 59
	strb	w0, [x27,131]
	b	.L1610
.L1526:
	cmp	w25, 6
	bne	.L1527
	mov	w0, 14
	strb	w0, [x27,128]
	mov	w0, 15
	strb	w0, [x27,129]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x27,130]
	mov	w0, 17
	strb	w0, [x27,131]
	b	.L1610
.L1527:
	cmp	w25, 7
	bne	.L1576
	mov	x0, 0
.L1528:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1528
	mov	w0, -44
	mov	w21, 12
	strb	w0, [x27,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x27,137]
	b	.L1522
.L1576:
	mov	w21, 7
.L1610:
	mov	w28, 4
.L1522:
	sub	w0, w25, #1
	cmp	w0, 1
	bhi	.L1607
	ldr	x0, [x29,128]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w27, 55
	add	x24, x24, 408
.L1529:
	ldrb	w0, [x19,633]
	cmp	w0, w23
	bls	.L1536
	add	x0, x19, x23, sxtw
	ldr	x2, [x29,136]
	ldrb	w0, [x0,736]
	mov	x20, 160
	ldr	x22, [x19,536]
	mov	x25, 0
	ubfiz	x1, x0, 8, 8
	madd	x20, x0, x20, x2
	add	x22, x22, x1
	add	x20, x20, 32
.L1531:
	str	w27, [x22,2056]
	ldrb	w0, [x26,x25]
	str	w0, [x22,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x22,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	cmp	w28, w25, uxtb
	bhi	.L1531
	mov	x0, 0
.L1532:
	add	w2, w0, 8
	mov	x1, 0
.L1533:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w5, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1533
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1532
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1529
.L1607:
	sub	w0, w25, #3
	cmp	w0, 4
	bhi	.L1536
	mul	w0, w28, w21
	sub	w23, w28, #1
	mov	w20, 0
	asr	w22, w0, 2
	lsl	w0, w0, 4
	uxtb	x23, w23
	str	w0, [x29,120]
	lsl	w0, w22, 1
	sub	w19, w25, #5
	str	w0, [x29,124]
	adrp	x26, .LANCHOR4
	add	x0, x23, 1
	str	x0, [x29,112]
.L1537:
	ldr	x0, [x29,128]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w0, [x1,633]
	cmp	w0, w20
	bhi	.L1575
.L1536:
	ldr	x0, [x29,136]
	strb	w28, [x0,1]
	strb	w21, [x0,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L1575:
	add	x0, x1, x20, sxtw
	ldrb	w23, [x0,736]
	str	x1, [x29,104]
	mov	x24, 160
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	ldr	x1, [x29,136]
	sxtw	x0, w23
	ubfiz	x2, x23, 8, 8
	madd	x24, x0, x24, x1
	ldr	x1, [x29,104]
	add	x24, x24, 32
	ldr	x0, [x1,536]
	mov	w1, 255
	str	x1, [x29,104]
	add	x23, x0, x2
	str	w1, [x23,2056]
	bl	nandc_wait_flash_ready
	mov	w0, 54
	cmp	w25, 4
	str	w0, [x23,2056]
	ldr	x1, [x29,104]
	bne	.L1538
	mov	w0, 64
	str	w1, [x23,2052]
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1611
.L1538:
	cmp	w19, 1
	bhi	.L1540
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1612
.L1540:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1611:
	str	w0, [x23,2052]
	mov	w0, 77
.L1612:
	str	w0, [x23,2048]
	mov	w0, 22
	str	w0, [x23,2056]
	mov	w0, 23
	str	w0, [x23,2056]
	mov	w0, 4
	str	w0, [x23,2056]
	mov	w0, 25
	str	w0, [x23,2056]
	cmp	w25, 6
	str	wzr, [x23,2056]
	str	wzr, [x23,2052]
	str	wzr, [x23,2052]
	bne	.L1541
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L1542
.L1541:
	str	wzr, [x23,2052]
.L1542:
	mov	w7, 2
	str	w7, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	str	x7, [x29,104]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w1, 16
	bls	.L1544
	ldr	x7, [x29,104]
	cmp	w25, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L1544:
	adrp	x0, .LANCHOR4
	mov	x7, 0
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1192]
.L1545:
	ldr	w8, [x23,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L1545
	cmp	w25, 7
	bne	.L1546
	mov	w1, 0
.L1548:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1547
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L1547
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1548
	b	.L1549
.L1547:
	cmp	w1, 7
	bne	.L1550
.L1549:
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1551:
	b	.L1551
.L1546:
	cmp	w25, 6
	bne	.L1550
	mov	x1, 0
.L1552:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L1550
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L1550
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1552
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1554:
	b	.L1554
.L1550:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x9, [x0,1192]
	mov	x0, 0
.L1555:
	ldr	w1, [x29,120]
	cmp	w1, w0
	ble	.L1614
	ldr	w1, [x23,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L1555
.L1614:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w8, w22
	mov	w7, 8
	ldr	x11, [x0,1192]
.L1558:
	mov	w0, 0
.L1557:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L1557
	ldr	w0, [x29,124]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L1558
	mov	x7, 0
	mov	w15, 1
.L1559:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w10, w1
	ldr	x14, [x0,1192]
.L1563:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L1561:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L1561
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L1563
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L1559
	add	x1, x26, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w8, w7
	ldr	x1, [x1,1192]
.L1566:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L1566
	cmp	w8, 7
	ble	.L1567
	adrp	x0, .LC133
	mov	w2, 1
	add	x0, x0, :lo12:.LC133
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1568:
	b	.L1568
.L1567:
	cmp	w25, 6
	mov	w7, 4
	beq	.L1569
	cmp	w25, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L1569:
	mov	w8, 0
.L1570:
	mov	x1, 0
.L1571:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x24,w10,sxtw]
	bhi	.L1571
	add	w8, w8, 1
	ldr	x1, [x29,112]
	cmp	w8, w21
	add	w0, w0, w7
	add	x9, x9, x1
	blt	.L1570
	mov	w24, 255
	str	w24, [x23,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w0, 56
	bhi	.L1613
	mov	w0, 54
	str	w0, [x23,2056]
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 22
	str	wzr, [x23,2048]
	str	w0, [x23,2056]
	mov	w0, 48
	str	wzr, [x23,2056]
	str	wzr, [x23,2052]
	str	wzr, [x23,2052]
	str	w24, [x23,2052]
	str	w24, [x23,2052]
	str	w24, [x23,2052]
.L1613:
	str	w0, [x23,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1537
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
	cmp	w1, 6
	bhi	.L1615
	bl	hynix_get_read_retry_default
.L1615:
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_get_read_retry_tbl, .-flash_get_read_retry_tbl
	.align	2
	.global	nandc_xfer_done
	.type	nandc_xfer_done, %function
nandc_xfer_done:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	ldr	x0, [x20,536]
	bl	wait_for_nandc_xfer_completed
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	x23, x0
	strb	wzr, [x1,1200]
	ldrb	w1, [x20,516]
	cmp	w1, 9
	bne	.L1618
	ldr	x24, [x20,536]
	ldr	w0, [x24,16]
	str	w0, [x29,64]
	ldr	w20, [x24,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1638
	adrp	x21, .LC137
	adrp	x23, .LC136
	add	x21, x21, :lo12:.LC137
	add	x23, x23, :lo12:.LC136
	b	.L1620
.L1638:
	adrp	x21, .LC135
	adrp	x22, .LC136
	mov	w20, 0
	add	x21, x21, :lo12:.LC135
	add	x22, x22, :lo12:.LC136
.L1619:
	ldr	w2, [x24,64]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 6
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1622
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1621
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1621
	ldr	w1, [x29,72]
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1622
.L1621:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1619
	ldr	w2, [x24,64]
	mov	w1, w20
	ldr	w3, [x29,64]
	mov	x0, x21
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	mov	x0, x22
	mov	w2, 4
	mov	w3, 64
	ldr	x1, [x1,536]
	bl	rknand_print_hex
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1619
	add	x0, x23, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,1200]
.L1622:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1626
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
	b	.L1666
.L1628:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,536]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1620
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x24,64]
	mov	x0, x21
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x22,536]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1620:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1628
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1626
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
	b	.L1665
.L1618:
	ldr	x23, [x20,536]
	ldr	w0, [x23,8]
	str	w0, [x29,64]
	ldr	w20, [x23,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1639
	adrp	x21, .LC137
	adrp	x24, .LC136
	add	x21, x21, :lo12:.LC137
	add	x24, x24, :lo12:.LC136
	b	.L1631
.L1639:
	adrp	x21, .LC135
	adrp	x22, .LC136
	mov	w20, 0
	add	x21, x21, :lo12:.LC135
	add	x22, x22, :lo12:.LC136
.L1630:
	ldr	w2, [x23,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1633
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1632
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1632
	ldr	w1, [x29,72]
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	printk
	b	.L1633
.L1632:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1630
	ldr	w2, [x23,28]
	mov	w1, w20
	ldr	w3, [x29,64]
	mov	x0, x21
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	mov	x0, x22
	mov	w2, 4
	mov	w3, 64
	ldr	x1, [x1,536]
	bl	rknand_print_hex
	b	.L1630
.L1633:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1626
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
.L1666:
	mov	w2, 0
	b	.L1664
.L1637:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,536]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1631
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x23,28]
	mov	x0, x21
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x22,536]
	mov	x0, x24
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1631:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1637
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1626
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
.L1665:
	mov	w2, 1
.L1664:
	bl	rknand_dma_unmap_single
.L1626:
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
	mov	w0, w20
	mov	x2, x3
	mov	w1, w22
	mov	x21, x3
	mov	x3, x4
	str	x23, [sp,48]
	mov	x23, x4
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	mov	w0, 0
	cbnz	w20, .L1697
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w1, [x2,516]
	cmp	w1, 9
	bne	.L1669
	ldr	x5, [x2,536]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1670:
	cmp	w3, w22
	bcs	.L1706
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
	tbnz	x1, 2, .L1689
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1689
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1672
	ubfx	x1, x1, 3, 7
	b	.L1673
.L1672:
	ubfx	x1, x1, 19, 7
.L1673:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1671
.L1689:
	mov	w0, -1
.L1671:
	add	w3, w3, 1
	b	.L1670
.L1706:
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
	bne	.L1676
	adrp	x0, .LC139
	mov	w1, w20
	add	x0, x0, :lo12:.LC139
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,536]
	str	w20, [x0]
	mov	w0, -1
.L1676:
	tbz	x20, 13, .L1677
	adrp	x1, .LANCHOR4+1200
	ldrb	w1, [x1,#:lo12:.LANCHOR4+1200]
	cbz	w1, .L1677
	adrp	x0, .LC140
	mov	w1, w20
	add	x0, x0, :lo12:.LC140
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,536]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1678
.L1669:
	ldrb	w0, [x2,724]
	mov	w5, 128
	lsr	w6, w22, 1
	mov	w3, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w5, w0, w5, cc
	mov	w1, w20
	mov	w4, w20
.L1680:
	cmp	w4, w6
	add	w7, w1, w5
	bcs	.L1707
	ldr	x0, [x2,680]
	and	x1, x1, 4294967292
	ldr	w0, [x0,x1]
	ubfiz	x1, x4, 2, 30
	add	w4, w4, 1
	lsr	w8, w0, 16
	strb	w0, [x23,x1]
	lsr	w1, w0, 8
	strb	w1, [x23,w3,uxtw]
	add	w1, w3, 1
	lsr	w0, w0, 24
	strb	w8, [x23,x1]
	add	w1, w3, 2
	add	w3, w3, 4
	strb	w0, [x23,x1]
	mov	w1, w7
	b	.L1680
.L1707:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,536]
	mov	w0, w2
.L1682:
	cmp	w2, w22
	bcs	.L1708
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1692
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1692
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
	bls	.L1684
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1705
.L1684:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1705:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1683
.L1692:
	mov	w0, -1
.L1683:
	add	w2, w2, 1
	b	.L1682
.L1708:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,536]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1677
	adrp	x0, .LC141
	mov	w1, w20
	add	x0, x0, :lo12:.LC141
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,536]
	str	w20, [x0]
	b	.L1678
.L1677:
	cmn	w0, #1
	beq	.L1697
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1697
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1697
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1697
.L1678:
	mov	w0, -1
.L1697:
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
	adrp	x22, .LANCHOR0
	uxtb	w21, w0
	add	x0, x22, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w23, w1
	mov	x26, x2
	ldr	x19, [x0,536]
	adrp	x0, .LANCHOR2
	mov	x25, x3
	mov	w27, w4
	and	w20, w1, 2097151
	ubfx	x24, x23, 24, 2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1710
	adrp	x0, .LC142
	mov	w1, w21
	add	x0, x0, :lo12:.LC142
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L1710:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	cbnz	w24, .L1711
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1712
.L1711:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1713
	sxtw	x0, w21
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w24, [x0,8]
	b	.L1712
.L1713:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1712:
	ubfiz	x5, x21, 8, 8
	and	w23, w23, 255
	add	x19, x19, x5
	lsr	w0, w20, 8
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w23, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w20, 16
	str	w0, [x19,2052]
	mov	w0, 48
	str	w0, [x19,2056]
	cbz	w24, .L1714
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1714
	add	w20, w20, w20, lsl 1
	sub	w0, w20, #1
	add	w0, w0, w24
	b	.L1725
.L1714:
	mov	w0, w20
.L1725:
	bl	nandc_set_seed
	bl	nandc_wait_flash_ready
	mov	w0, 5
	str	w0, [x19,2056]
	str	wzr, [x19,2052]
	mov	w0, 224
	str	wzr, [x19,2052]
	mov	w2, w27
	mov	x3, x26
	mov	x4, x25
	str	w0, [x19,2056]
	mov	w1, 0
	mov	w0, w21
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	w20, 0
	add	x19, x19, :lo12:.LANCHOR0
	str	w4, [x29,120]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w24, w0
	mov	w21, w1
	ldrb	w0, [x19,724]
	mov	x25, x2
	mov	x26, x3
	mov	w28, -1
	adrp	x23, .LANCHOR4
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	str	w0, [x29,124]
	bl	nandc_wait_flash_ready
	ldr	x22, [x19,536]
	uxtb	x19, w24
	lsl	x0, x19, 8
	str	x0, [x29,112]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	str	x0, [x29,104]
.L1727:
	add	x0, x23, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	cmp	w20, w0
	bcs	.L1731
	ldr	x0, [x29,112]
	add	w27, w20, 1
	add	x1, x22, x0
	mov	w0, 239
	str	x1, [x29,96]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	w0, 200
	bl	timer_delay_ns
	ldr	x1, [x29,96]
	mov	w0, w24
	ldr	w4, [x29,120]
	mov	x2, x25
	mov	x3, x26
	str	w27, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	mov	w1, w21
	bl	flash_read_page
	mov	w6, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1728
	ldr	x0, [x29,104]
	mov	w4, w6
	mov	w1, w20
	mov	w2, w21
	mov	w3, w28
	str	x6, [x29,96]
	bl	printk
	ldr	x6, [x29,96]
.L1728:
	cmn	w6, #1
	beq	.L1729
	adrp	x0, .LANCHOR4
	cmn	w28, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w28, w28, w6, ne
	ldr	x25, [x0,1192]
	ldr	x26, [x0,1208]
	ldr	w0, [x29,124]
	cmp	w6, w0
	bcc	.L1736
.L1729:
	mov	w20, w27
	b	.L1727
.L1736:
	mov	w28, w6
.L1731:
	add	x19, x22, x19, lsl 8
	mov	w0, 239
	str	w0, [x19,2056]
	mov	w0, 137
	str	w0, [x19,2052]
	mov	w0, 200
	bl	timer_delay_ns
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	ldr	w0, [x29,124]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	cmp	w28, w0
	bcc	.L1733
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1733:
	cmn	w28, #1
	beq	.L1737
	cmp	w28, 256
	bne	.L1734
.L1737:
	adrp	x0, .LC144
	mov	w1, w20
	add	x0, x0, :lo12:.LC144
	mov	w2, w21
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1734:
	bl	nandc_wait_flash_ready
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
	cbnz	w19, .L1751
	str	x0, [x29,112]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w19, -1
	mov	w28, 1
	str	x0, [x29,104]
.L1758:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1752
	mov	w1, w28
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x25, x0
	str	w1, [x0,8]
	b	.L1753
.L1752:
	mov	w1, w28
	bl	toshiba_3d_set_slc_rr_para
.L1753:
	ldr	w4, [x29,124]
	mov	w0, w22
	mov	w1, w24
	mov	x2, x26
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1754
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w28
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1754:
	cmn	w4, #1
	beq	.L1755
	adrp	x0, .LANCHOR4
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w19, w19, w4, ne
	ldr	x26, [x0,1192]
	ldr	x27, [x0,1208]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1774
.L1755:
	add	w28, w28, 1
	cmp	w28, w21
	bne	.L1758
	b	.L1757
.L1774:
	mov	w21, w28
	mov	w19, w4
.L1757:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1759
	mov	w1, 0
	mov	w2, w1
	b	.L1797
.L1759:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1760
.L1751:
	mov	x28, x0
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,104]
.L1767:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1761
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x25, x28
	mov	w1, 93
	b	.L1796
.L1761:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x25, x28
	mov	w1, 38
.L1796:
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
	tbz	x0, 4, .L1763
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1763:
	cmn	w4, #1
	beq	.L1764
	adrp	x0, .LANCHOR4
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w19, w19, w4, ne
	ldr	x26, [x0,1192]
	ldr	x27, [x0,1208]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1775
.L1764:
	ldr	w0, [x29,112]
	add	w21, w21, 1
	cmp	w21, w0
	bne	.L1767
	b	.L1766
.L1775:
	mov	w19, w4
.L1766:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1768
	mov	w1, 0
	mov	w2, 1
.L1797:
	bl	toshiba_tlc_set_rr_para
	b	.L1760
.L1768:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1760:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	bne	.L1769
	ubfiz	x22, x22, 8, 8
	mov	w0, 85
	add	x25, x25, x22
	str	w0, [x25,2056]
	mov	w0, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2048]
	str	w0, [x25,2056]
.L1769:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,724]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1770
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1770:
	cmn	w19, #1
	beq	.L1776
	cmp	w19, 256
	bne	.L1771
.L1776:
	adrp	x0, .LC147
	mov	w1, w21
	add	x0, x0, :lo12:.LC147
	mov	w2, w24
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1771:
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
	bls	.L1799
	ldrb	w0, [x1,672]
	cbz	w0, .L1800
	mov	w0, 1
	mov	w25, 1
	bl	nandc_set_if_mode
.L1800:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1799:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1801:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1832
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1802
	bl	sandisk_set_rr_para
	b	.L1803
.L1802:
	bl	toshiba_set_rr_para
.L1803:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 34
	bne	.L1804
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1804
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1804:
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
	beq	.L1807
	cmn	w24, #1
	csel	w24, w24, w0, ne
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x26, [x0,1192]
	ldr	x27, [x0,1208]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1809
.L1807:
	add	w20, w20, 1
	b	.L1801
.L1832:
	mov	w28, w24
.L1809:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1811
	bl	sandisk_set_rr_para
	b	.L1812
.L1811:
	bl	toshiba_set_rr_para
.L1812:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,724]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1813
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1813:
	cmn	w28, #1
	beq	.L1819
	cmp	w28, 256
	bne	.L1814
.L1819:
	adrp	x0, .LC147
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC147
	mov	w1, w20
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1814:
	bl	nandc_wait_flash_ready
	cbz	w25, .L1816
	mov	w0, 4
	bl	nandc_set_if_mode
.L1816:
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
	adrp	x7, .LANCHOR4
.L1834:
	cmp	w6, w24
	bcs	.L1838
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
	beq	.L1836
	add	x1, x7, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1192]
	ldr	x26, [x1,1208]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1843
.L1836:
	add	w6, w6, 1
	b	.L1834
.L1843:
	mov	w19, w0
.L1838:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,724]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1840
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1840:
	cmn	w19, #1
	beq	.L1844
	cmp	w19, 256
	bne	.L1841
.L1844:
	adrp	x0, .LC148
	mov	w1, w6
	add	x0, x0, :lo12:.LC148
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1841:
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
	adrp	x0, .LC149
	str	w27, [x29,124]
	add	x0, x0, :lo12:.LC149
	mov	w25, w27
	mov	w28, w27
	mov	w19, 1024
	mov	w26, 6
	mov	w21, -1
	str	x0, [x29,104]
.L1859:
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
	tbz	x0, 4, .L1854
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w26
	mov	w2, w22
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1854:
	add	w0, w19, 1
	cmp	w4, w0
	bhi	.L1855
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x24, [x0,1192]
	ldr	x23, [x0,1208]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	cmp	w4, w0, lsr 2
	bcs	.L1865
	add	w28, w28, 1
	cmp	w28, 7
	bls	.L1865
	sub	w27, w26, w28
	mov	w19, w4
	mov	w21, 0
	b	.L1857
.L1855:
	cmp	w25, w28
	bcs	.L1866
	sub	w0, w27, w28
	cmp	w28, 7
	str	w0, [x29,124]
	bhi	.L1858
	mov	w25, w28
	b	.L1866
.L1865:
	mov	w27, w26
	mov	w19, w4
	mov	w21, 0
	b	.L1856
.L1866:
	mov	w28, 0
.L1856:
	add	w26, w26, 2
	cmp	w26, 50
	bne	.L1859
.L1857:
	ldr	w0, [x29,124]
	cmp	w25, w28
	csel	w27, w27, w0, ls
.L1858:
	cbz	w27, .L1860
	adrp	x0, .LANCHOR0+724
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+724]
	udiv	w0, w0, w1
	cmp	w19, w0
	bcs	.L1860
	adrp	x0, .LC150
	mov	w1, w27
	add	x0, x0, :lo12:.LC150
	bl	printk
	mov	w0, w27
	b	.L1875
.L1860:
	ldrb	w0, [x29,116]
.L1875:
	bl	nandc_set_ddr_para
	cbz	w21, .L1862
	adrp	x0, .LC151
	mov	w2, w22
	mov	w1, w20
	add	x0, x0, :lo12:.LC151
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
	strb	w1, [x0,744]
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	ldr	w4, [x29,120]
	mov	w1, w22
	mov	x2, x24
	mov	x3, x23
	mov	w0, w20
	bl	flash_read_page
	mov	w19, w0
	adrp	x0, .LC152
	mov	w1, w20
	add	x0, x0, :lo12:.LC152
	mov	w2, w22
	mov	w3, w19
	bl	printk
	ldrb	w0, [x27,724]
	cmp	w19, w0
	bhi	.L1867
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	ldr	w0, [x1,1220]
	add	w0, w0, 1
	str	w0, [x1,1220]
	cmp	w0, 100
	bls	.L1863
	strb	wzr, [x27,672]
	b	.L1862
.L1867:
	mov	w19, w21
.L1863:
	add	x26, x26, :lo12:.LANCHOR0
	ldrb	w0, [x26,780]
	bl	flash_set_interface_mode
	ldrb	w0, [x26,780]
	bl	nandc_set_if_mode
.L1862:
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
	bhi	.L1877
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 386
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1877:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, x25, sxtw
	ldrb	w21, [x1,736]
	ldrb	w1, [x0,633]
	cmp	w1, w25
	bcs	.L1878
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L1904
	adrp	x0, .LC153
	mov	w1, w21
	add	x0, x0, :lo12:.LC153
	mov	w2, w25
	mov	w3, w19
	bl	printk
.L1904:
	mov	w0, -1
	b	.L1903
.L1878:
	ubfx	x1, x19, 24, 2
	cbnz	w1, .L1881
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L1882
	ldrb	w0, [x0,1]
	cbz	w0, .L1881
.L1882:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,1]
	sub	w19, w19, w0
	cbz	w2, .L1883
	add	w19, w0, w19, lsl 1
	b	.L1881
.L1883:
	add	x1, x1, 4
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1881:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1903
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25,720]
	cbnz	w26, .L1885
.L1888:
	adrp	x0, .LANCHOR4+1224
	ldr	x5, [x0,#:lo12:.LANCHOR4+1224]
	cbnz	x5, .L1886
	b	.L1887
.L1885:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	strb	wzr, [x25,720]
	bl	flash_read_page
	strb	w26, [x25,720]
	cmn	w0, #1
	beq	.L1888
	b	.L1903
.L1886:
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	blr	x5
	cmn	w0, #1
	bne	.L1903
.L1887:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,720]
	bl	printk
	ldrb	w0, [x20,672]
	cbz	w0, .L1904
	mov	w0, w21
	mov	w1, w19
	mov	x2, x24
	mov	x3, x23
	mov	w4, w22
	bl	flash_ddr_tuning_read
.L1903:
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
	adrp	x0, .LANCHOR4+1092
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x24, x2
	ldrh	w19, [x0,#:lo12:.LANCHOR4+1092]
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
	bne	.L1906
	mov	w28, 0
	mov	w5, 2
.L1907:
	cmp	w28, w19
	bgt	.L1906
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
	bne	.L1908
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1907
.L1908:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L1907
.L1906:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1911
	adrp	x0, .LC155
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC155
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L1911:
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
	adrp	x0, .LANCHOR0+3340
	stp	x29, x30, [sp, -16]!
	mov	w5, 21
	mov	x8, x1
	add	x29, sp, 0
	ldrh	w0, [x0,#:lo12:.LANCHOR0+3340]
	mov	x7, x2
	mov	w4, w3
	mov	x2, x8
	sub	w0, w5, w0
	mov	w5, 1
	lsl	w5, w5, w0
	mov	x3, x7
	sub	w1, w5, #1
	asr	w0, w6, w0
	and	w1, w6, w1
	bl	flash_get_last_written_page
	ldp	x29, x30, [sp], 16
	ret
	.size	flash_get_last_written_page_ext, .-flash_get_last_written_page_ext
	.align	2
	.global	flash_info_blk_init
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	adrp	x0, .LANCHOR2
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x20, .LANCHOR0
	tbz	x0, 12, .L1917
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR3
	adrp	x0, .LC156
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC156
	mov	w1, 49
	ldr	x4, [x4,728]
	add	x2, x2, 160
	mov	w3, 2048
	bl	printk
.L1917:
	mov	w22, 4
	adrp	x24, .LANCHOR4
.L1920:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x24, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w4, 4
	adrp	x23, .LANCHOR4
	ldrh	w1, [x19,2]
	ldr	x2, [x19,728]
	ldr	x3, [x21,1232]
	mul	w1, w22, w1
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1918
	ldr	x2, [x19,728]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L1919
.L1918:
	add	w22, w22, 1
	cmp	w22, 16
	bne	.L1920
.L1944:
	mov	w0, -1
	b	.L1943
.L1919:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,2]
	strb	w1, [x21,1240]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,1232]
	strb	w0, [x21,1241]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1922
	ldr	x0, [x19,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1922
	ldr	w21, [x0,4]
.L1922:
	add	x19, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,1241]
	ldrh	w1, [x22,2]
	ldr	x3, [x19,1232]
	mul	w1, w2, w1
	ldr	x2, [x22,728]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1923
	ldr	x0, [x22,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1923
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L1923
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,1240]
	strb	w0, [x19,1241]
.L1923:
	add	x21, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x21,1240]
	ldr	x3, [x21,1232]
	ldr	x2, [x22,728]
	bl	flash_get_last_written_page
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x21,1242]
	ldrb	w0, [x21,1240]
	ldrh	w21, [x22,2]
	mul	w21, w0, w21
.L1924:
	tbnz	w19, #31, .L1945
	add	x22, x20, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w0, 0
	add	w1, w19, w21
	mov	w4, 4
	ldr	x2, [x22,728]
	ldr	x3, [x3,1232]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1925
	ldr	x0, [x22,728]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	bne	.L1925
.L1927:
	mov	w0, 0
	b	.L1943
.L1925:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1924
.L1945:
	cmn	w19, #1
	bne	.L1927
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	ldr	x1, [x20,728]
	ldr	w1, [x1]
	bl	printk
	b	.L1944
.L1943:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_info_blk_init, .-flash_info_blk_init
	.align	2
	.global	flash_ddr_para_scan
	.type	flash_ddr_para_scan, %function
flash_ddr_para_scan:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x21, x19, :lo12:.LANCHOR0
	mov	w22, 1
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	mov	w23, w1
	ldrb	w0, [x21,780]
	add	x20, x20, :lo12:.LANCHOR4
	strb	w22, [x21,672]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,780]
	bl	nandc_set_if_mode
	ldr	x2, [x20,1248]
	mov	w1, w23
	ldr	x3, [x20,1256]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tuning_read
	ldr	x2, [x20,1248]
	mov	w0, w24
	ldr	x3, [x20,1256]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1947
	ldrb	w0, [x21,780]
	tbz	x0, 0, .L1947
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,672]
	b	.L1948
.L1947:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,672]
.L1948:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.type	id_block_read_data.constprop.31, %function
id_block_read_data.constprop.31:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x27, x28, [sp,80]
	mov	w27, w1
	adrp	x1, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x1, :lo12:.LANCHOR0
	str	x1, [x29,120]
	stp	x25, x26, [sp,64]
	mov	w26, w0
	ldr	x0, [x19,624]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x21, x2
	mov	w22, 0
	ldrb	w2, [x0,9]
	mov	w23, 4
	ldrh	w0, [x0,10]
	mov	w25, w22
	str	w2, [x29,164]
	mul	w0, w2, w0
	ldrb	w2, [x19,720]
	str	w2, [x29,136]
	uxth	w0, w0
	strb	wzr, [x19,720]
	udiv	w2, w26, w0
	msub	w0, w2, w0, w26
	str	w0, [x29,172]
	sub	w0, w26, w0
	str	w0, [x29,160]
	ldr	w0, [x29,172]
	and	w0, w0, 3
	str	w0, [x29,168]
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	str	x0, [x29,112]
.L1953:
	cmp	w22, w27
	bcs	.L1990
	ldr	w0, [x29,168]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	sub	w0, w23, w0
	uxth	w0, w0
	str	w0, [x29,144]
	ldr	w0, [x29,172]
	ldrb	w1, [x1,1264]
	add	w20, w22, w0
	add	x0, x19, 4
	udiv	w20, w20, w23
	and	w20, w20, 65535
	ldrh	w0, [x0,w20,sxtw 1]
	cbnz	w1, .L1954
	mov	w20, w0
	b	.L1955
.L1954:
	ldrb	w1, [x19,1]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L1955:
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
.L1956:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L1963
	ldrb	w6, [x19,720]
	cbnz	w6, .L1958
.L1961:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x6, [x0,1224]
	cbnz	x6, .L1959
	b	.L1960
.L1958:
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
	beq	.L1961
	b	.L1989
.L1959:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	blr	x6
	cmn	w0, #1
	bne	.L1989
.L1960:
	ldrb	w0, [x19,672]
	cbz	w0, .L1963
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tuning_read
.L1989:
	mov	w20, w0
.L1963:
	cmn	w20, #1
	bne	.L1964
	cmp	w28, 16
	beq	.L1964
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L1956
.L1964:
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
	beq	.L1966
	cbnz	w25, .L1966
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L1966
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L1967
.L1966:
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
.L1967:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L1953
.L1990:
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
	.size	id_block_read_data.constprop.31, .-id_block_read_data.constprop.31
	.align	2
	.global	flash_complete_plane_page_read
	.type	flash_complete_plane_page_read, %function
flash_complete_plane_page_read:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	ubfx	x22, x0, 21, 3
	stp	x19, x20, [sp,16]
	and	w19, w0, 2097151
	ubfx	x20, x0, 24, 2
	add	x0, x21, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	mov	x24, x1
	mov	x23, x2
	ldrb	w0, [x0,633]
	cmp	w0, w22
	bhi	.L1992
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 795
	add	x1, x1, 184
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1992:
	add	x0, x21, :lo12:.LANCHOR0
	add	x22, x0, x22, sxtw
	ldrb	w22, [x22,736]
	ldr	x25, [x0,536]
	mov	w0, w22
	bl	nandc_cs
	cbnz	w20, .L1993
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L1993:
	add	x3, x21, :lo12:.LANCHOR0
	uxtb	x5, w22
	and	w2, w19, 255
	lsr	w1, w19, 8
	ldrb	w0, [x3,656]
	cmp	w0, 1
	lsr	w0, w19, 16
	bne	.L1994
	add	x5, x25, x5, lsl 8
	mov	w3, 6
	b	.L2007
.L1994:
	ldr	x3, [x3,624]
	add	x5, x25, x5, lsl 8
	ldrb	w3, [x3,12]
	cmp	w3, 3
	bne	.L1996
	mov	w3, 5
.L2007:
	str	w3, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w2, [x5,2052]
	str	w1, [x5,2052]
	str	w0, [x5,2052]
	b	.L2005
.L1996:
	str	wzr, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w2, [x5,2052]
	str	w1, [x5,2052]
	str	w0, [x5,2052]
	mov	w0, 5
	str	w0, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
.L2005:
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w20, .L1997
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1997
	sub	w20, w20, #1
	add	w0, w19, w19, lsl 1
	add	w0, w20, w0
	b	.L2006
.L1997:
	mov	w0, w19
.L2006:
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+17
	mov	x3, x24
	mov	x4, x23
	mov	w1, 0
	mov	w0, w22
	ldrb	w2, [x2,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer
	mov	w19, w0
	mov	w0, w22
	bl	nandc_de_cs
	ldr	x25, [sp,64]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_complete_plane_page_read, .-flash_complete_plane_page_read
	.align	2
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	and	w20, w0, 2097151
	stp	x23, x24, [sp,48]
	mov	w24, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ubfx	x21, x24, 21, 3
	mov	x25, x1
	ldrb	w0, [x0,633]
	mov	x26, x2
	ubfx	x22, x24, 24, 2
	cmp	w0, w21
	bhi	.L2009
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 917
	add	x1, x1, 216
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2009:
	add	x0, x19, :lo12:.LANCHOR0
	add	x21, x0, x21, sxtw
	ldrb	w23, [x21,736]
	ldr	x21, [x0,536]
	mov	w0, w23
	bl	nandc_cs
	cbnz	w22, .L2010
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
.L2010:
	ubfiz	x0, x23, 8, 8
	add	x5, x21, x0
	mov	w0, 5
	str	w0, [x5,2056]
	and	w0, w20, 255
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w0, [x5,2052]
	lsr	w0, w20, 8
	str	w0, [x5,2052]
	lsr	w0, w20, 16
	str	w0, [x5,2052]
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w22, .L2011
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2011
	sub	w0, w22, #1
	add	w1, w20, w20, lsl 1
	add	w0, w0, w1
	b	.L2043
.L2011:
	mov	w0, w20
.L2043:
	adrp	x21, .LANCHOR2
	bl	nandc_set_seed
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w23
	mov	w1, 0
	mov	x3, x25
	mov	x4, x26
	add	x27, x27, 8
	ldrb	w2, [x27,9]
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L2013
	add	x28, x19, :lo12:.LANCHOR0
	ldrb	w5, [x28,720]
	cbz	w5, .L2014
	ldrb	w4, [x27,9]
	mov	w0, w23
	orr	w1, w20, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	str	x5, [x29,104]
	strb	wzr, [x28,720]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,720]
	cbnz	w22, .L2015
.L2022:
	ldrb	w2, [x19,#:lo12:.LANCHOR0]
	add	x1, x19, :lo12:.LANCHOR0
	cbz	w2, .L2015
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2015
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w1, [x21,27]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 3
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L2030
.L2015:
	cmn	w0, #1
	bne	.L2030
.L2023:
	adrp	x0, .LANCHOR4+1224
	ldr	x5, [x0,#:lo12:.LANCHOR4+1224]
	cbnz	x5, .L2018
.L2021:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	mov	w1, 0
	mov	w2, w24
	mov	w3, -1
	ldrb	w4, [x19,720]
	bl	printk
	ldrb	w0, [x19,672]
	cbnz	w0, .L2019
	mov	w0, -1
	b	.L2030
.L2018:
	add	x4, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w20, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x4,17]
	blr	x5
	cmn	w0, #1
	bne	.L2030
	b	.L2021
.L2019:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w20, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x21,17]
	bl	flash_ddr_tuning_read
	b	.L2030
.L2013:
	cbz	w22, .L2022
	b	.L2030
.L2014:
	cbz	w22, .L2022
	b	.L2023
.L2030:
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
	ldrb	w21, [x0,2835]
	cmp	w21, 255
	beq	.L2128
	add	x0, x0, 784
	mov	w1, 0
	add	x0, x0, x21, lsl 6
	ldrb	w2, [x0,58]
	ldr	w20, [x0,40]
	sub	w3, w2, #1
	cmp	w3, 9
	bhi	.L2046
	adrp	x0, .L2048
	add	x0, x0, :lo12:.L2048
	ldrh	w0, [x0,w3,uxtw #1]
	adr	x1, .Lrtx2048
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx2048:
	.section	.rodata
	.align	0
	.align	2
.L2048:
	.2byte	(.L2047 - .Lrtx2048) / 4
	.2byte	(.L2049 - .Lrtx2048) / 4
	.2byte	(.L2050 - .Lrtx2048) / 4
	.2byte	(.L2050 - .Lrtx2048) / 4
	.2byte	(.L2050 - .Lrtx2048) / 4
	.2byte	(.L2051 - .Lrtx2048) / 4
	.2byte	(.L2052 - .Lrtx2048) / 4
	.2byte	(.L2053 - .Lrtx2048) / 4
	.2byte	(.L2050 - .Lrtx2048) / 4
	.2byte	(.L2053 - .Lrtx2048) / 4
	.text
.L2047:
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2128
	add	x3, x19, :lo12:.LANCHOR0
	add	x0, x3, 784
	add	x0, x0, x21, lsl 6
	ldr	x2, [x0,16]
	ldr	x1, [x0,8]
	cbz	x2, .L2055
	ldrb	w4, [x0,56]
	ldrb	w0, [x3,2832]
	cmp	w4, w0
	csel	x1, x1, x2, ne
.L2055:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w20
	add	x19, x19, 784
	add	x19, x19, x21, lsl 6
	ldr	x2, [x19,24]
	bl	flash_complete_page_read
	str	w0, [x19,52]
	mov	w0, 12
	strb	w0, [x19,58]
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2128
.L2049:
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	add	x2, x19, :lo12:.LANCHOR0
	lsl	x4, x21, 6
	add	x3, x2, 784
	add	x1, x3, x4
	ldrb	w20, [x3,x4]
	tbz	x0, 6, .L2128
	add	x3, x3, x20, lsl 6
	ldr	x0, [x1,16]
	ldr	x27, [x1,8]
	ldr	x23, [x3,8]
	cbz	x0, .L2056
	ldrb	w3, [x1,56]
	ldrb	w1, [x2,2832]
	cmp	w3, w1
	csel	x27, x27, x0, ne
.L2056:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 784
	add	x0, x0, x20, lsl 6
	ldr	x1, [x0,16]
	cbz	x1, .L2057
	ldrb	w3, [x0,56]
	ldrb	w0, [x2,2832]
	cmp	w3, w0
	csel	x23, x23, x1, ne
.L2057:
	add	x24, x19, :lo12:.LANCHOR0
	mov	x1, x27
	add	x24, x24, 784
	add	x26, x24, x21, lsl 6
	add	x24, x24, x20, lsl 6
	add	x28, x26, 32
	add	x26, x26, 16
	ldr	x2, [x26,8]
	ldr	w0, [x26,24]
	bl	flash_complete_plane_page_read
	mov	w22, w0
	ldr	x2, [x24,24]
	mov	x1, x23
	ldr	w0, [x24,40]
	bl	flash_complete_plane_page_read
	mov	w25, w0
	cmn	w22, #1
	beq	.L2058
	ldr	w0, [x28,4]
	cmn	w0, #1
	beq	.L2059
	ldr	x1, [x26,8]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2059
.L2058:
	add	x4, x19, :lo12:.LANCHOR0
	mov	x2, x27
	add	x24, x4, 784
	add	x24, x24, x21, lsl 6
	ldrb	w4, [x4,2832]
	add	x26, x24, 32
	add	x24, x24, 16
	ldr	w1, [x24,24]
	ldr	x3, [x24,8]
	ubfx	x0, x1, 21, 3
	and	w1, w1, -14680065
	bl	flash_read_page_en
	mov	w22, w0
	ldr	w2, [x26,4]
	cmn	w2, #1
	beq	.L2060
	ldr	x3, [x24,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2060
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2060
	adrp	x0, .LC160
	ldr	w1, [x26,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC160
	bl	printk
.L2060:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x21, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2059
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2059
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 338
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2059:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w1, 12
	add	x3, x3, 784
	cmn	w25, #1
	add	x21, x3, x21, lsl 6
	ldrb	w0, [x21,2]
	str	w22, [x21,52]
	orr	w0, w0, 8
	strb	w1, [x21,58]
	strb	w0, [x21,2]
	beq	.L2061
	add	x3, x3, x20, lsl 6
	ldr	w0, [x3,36]
	cmn	w0, #1
	beq	.L2063
	ldr	x1, [x3,24]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2063
.L2061:
	add	x4, x19, :lo12:.LANCHOR0
	mov	x2, x23
	add	x21, x4, 784
	add	x21, x21, x20, lsl 6
	ldrb	w4, [x4,2832]
	add	x24, x21, 32
	add	x21, x21, 16
	ldr	w1, [x21,24]
	ldr	x3, [x21,8]
	ubfx	x0, x1, 21, 3
	and	w1, w1, -14680065
	bl	flash_read_page_en
	ldr	w2, [x24,4]
	cmn	w2, #1
	beq	.L2065
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2065
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2065
	adrp	x0, .LC160
	ldr	w1, [x24,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC160
	bl	printk
.L2065:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2063
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2063
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 353
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2063:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 12
	add	x19, x19, 784
	add	x20, x19, x20, lsl 6
	ldrb	w0, [x20,2]
	str	w22, [x20,52]
	orr	w0, w0, 8
	strb	w1, [x20,58]
	strb	w0, [x20,2]
	b	.L2128
.L2050:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	tbz	x20, 6, .L2128
	mov	w1, 5
	add	x19, x19, :lo12:.LANCHOR0
	ands	w1, w20, w1
	beq	.L2066
	add	x19, x19, 784
	mov	w0, 11
	add	x19, x19, x21, lsl 6
	mov	w2, w20
	mov	w4, 11
	strb	w0, [x19,58]
	adrp	x0, .LC161
	ldrb	w1, [x19,1]
	add	x0, x0, :lo12:.LC161
	ldr	w3, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2129
.L2066:
	add	x19, x19, x21, lsl 6
	b	.L2073
.L2053:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 3
	cmp	w2, 10
	mov	w1, 9
	add	x22, x22, 784
	csel	w1, w0, w1, ne
	ubfx	x3, x20, 21, 3
	add	x0, x22, x21, lsl 6
.L2068:
	ldrb	w23, [x0]
	cmp	w23, 255
	beq	.L2130
	add	x0, x22, x23, lsl 6
	ldrb	w2, [x0,58]
	cmp	w2, w1
	bne	.L2068
	ldr	w2, [x0,40]
	ubfx	x2, x2, 21, 3
	cmp	w3, w2
	bne	.L2068
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	and	w0, w0, 64
	tbz	x20, 6, .L2076
	ands	w0, w20, 15
	add	x22, x22, x21, lsl 6
	beq	.L2071
	adrp	x0, .LC162
	ldrb	w1, [x22,1]
	ldr	w3, [x22,40]
	add	x0, x0, :lo12:.LC162
	mov	w2, w20
	mov	w4, 11
	bl	printk
	mov	w0, 11
	strb	w0, [x22,58]
	mov	w0, -1
	str	w0, [x22,52]
.L2070:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 784
	add	x21, x19, x21, lsl 6
	add	x23, x19, x23, lsl 6
	ldrb	w0, [x21,58]
	strb	w0, [x23,58]
	ldr	w0, [x21,52]
	str	w0, [x23,52]
.L2129:
	mov	w1, w20
	b	.L2046
.L2071:
	mov	w1, 12
	str	w0, [x22,52]
	strb	w1, [x22,58]
.L2076:
	mov	w20, w0
	b	.L2070
.L2130:
	mov	w1, -1
	b	.L2046
.L2051:
	mov	w1, 32
	mov	w0, w20
	bl	flash_wait_device_ready
	tbz	x0, 5, .L2128
	ands	w1, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x21, lsl 6
	beq	.L2073
	mov	w1, 11
	strb	w1, [x19,842]
	mov	w1, -1
	str	w1, [x19,836]
	mov	w1, w0
	b	.L2046
.L2073:
	mov	w0, 12
	str	w1, [x19,836]
	strb	w0, [x19,842]
	b	.L2046
.L2052:
	mov	w1, 64
	mov	w0, w20
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2128
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x21, lsl 6
	str	w0, [x19,836]
	mov	w0, 6
	strb	w0, [x19,842]
.L2128:
	mov	w1, 0
.L2046:
	mov	w0, w1
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_wait_write_queue_completed
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
.L2132:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2835]
	cmp	w0, 255
	beq	.L2134
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2132
.L2134:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
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
	mov	w4, w3
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, w0
	mov	x21, x1
	mov	x20, x2
	str	x4, [x29,56]
	bl	sblk_wait_write_queue_completed
	ldr	x4, [x29,56]
	mov	w1, w19
	mov	x2, x21
	mov	x3, x20
	ubfx	x0, x19, 21, 3
	bl	flash_read_page_en
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_read_ppa_page, .-ftl_read_ppa_page
	.align	2
	.global	sblk_read_page
	.type	sblk_read_page, %function
sblk_read_page:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	adrp	x23, .LANCHOR3
	uxtb	w21, w1
	add	x22, x22, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR3
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x25, x0
	mov	x19, x0
	mov	w20, w21
	add	x27, x22, 784
	add	x23, x23, 280
.L2138:
	cbz	w20, .L2161
	ldrb	w28, [x19]
	ldr	w26, [x19,40]
.L2139:
	mov	w0, w26
	mov	w1, 0
	bl	queue_lun_state
	cbz	w0, .L2162
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2139
.L2162:
	cmp	w20, 1
	beq	.L2145
	adrp	x0, .LANCHOR4+1265
	ldrb	w0, [x0,#:lo12:.LANCHOR4+1265]
	cbz	w0, .L2145
	ldrb	w0, [x19]
	ubfx	x3, x26, 21, 3
	cmp	w0, 255
	bne	.L2144
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 624
	add	x0, x0, :lo12:.LC0
	str	x3, [x29,120]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,120]
.L2144:
	ldrb	w24, [x19]
	lsl	x24, x24, 6
	add	x2, x27, x24
	ldr	w1, [x2,40]
	ubfx	x0, x1, 21, 3
	cmp	w3, w0
	bne	.L2145
	ldrh	w0, [x22,3350]
	add	w26, w0, w26
	cmp	w1, w26
	bne	.L2145
	ldr	w0, [x19,40]
	add	x26, x22, 2835
	str	x2, [x29,104]
	sub	w20, w20, #1
	ldrb	w28, [x27,x24]
	bl	flash_start_plane_read
	strb	wzr, [x19,59]
	mov	w4, 2
	mov	w3, -1
	strb	w4, [x19,58]
	mov	x0, x26
	strb	w3, [x19]
	mov	x1, x19
	str	x4, [x29,112]
	str	x3, [x29,120]
	bl	buf_add_tail
	ldr	x2, [x29,104]
	mov	x0, x26
	ldr	x4, [x29,112]
	ldr	x3, [x29,120]
	mov	x1, x2
	strb	w4, [x2,58]
	strb	wzr, [x2,59]
	strb	w3, [x27,x24]
	bl	buf_add_tail
	b	.L2143
.L2145:
	mov	x0, x19
	bl	queue_read_cmd
.L2143:
	subs	w20, w20, #1
	beq	.L2161
	sbfiz	x19, x28, 6, 32
	add	x19, x27, x19
	b	.L2138
.L2161:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 784
.L2149:
	cbz	w21, .L2163
	ldrb	w0, [x25,58]
	cmp	w0, 12
	bne	.L2150
	ldrb	w0, [x25]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2150
	sbfiz	x25, x0, 6, 32
	add	x25, x19, x25
.L2150:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2149
.L2163:
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
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x22, w0
	adrp	x21, .LANCHOR2
	adrp	x0, .LANCHOR0+536
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x19, [x0,#:lo12:.LANCHOR0+536]
	mov	x20, x22
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	add	x22, x22, 8
	stp	x25, x26, [sp,64]
	mov	w23, w1
	mov	x25, x3
	mov	x26, x2
	mov	w3, w4
	add	x22, x19, x22, lsl 8
	and	w24, w1, 2097151
	ubfx	x27, x23, 24, 2
	tbz	x0, 4, .L2165
	adrp	x0, .LC163
	mov	w2, w27
	add	x0, x0, :lo12:.LC163
	bl	printk
.L2165:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	hynix_reconfig_rr_para
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w27, .L2166
	bl	zftl_flash_enter_slc_mode
	b	.L2167
.L2166:
	bl	zftl_flash_exit_slc_mode
.L2167:
	ubfiz	x4, x20, 8, 8
	mov	w0, 128
	add	x19, x19, x4
	add	x21, x21, :lo12:.LANCHOR2
	str	w0, [x19,2056]
	and	w0, w23, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w0, w24, 16
	str	w0, [x19,2052]
	mov	w0, w24
	bl	nandc_set_seed
	ldrb	w1, [x21,17]
	mov	x2, x26
	mov	x3, x25
	mov	w0, 1
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	mov	w0, 16
	str	w0, [x19,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w21, w0
	mov	w0, w20
	and	w19, w21, 4
	bl	nandc_de_cs
	tbz	x21, 2, .L2168
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2168
	adrp	x0, .LC164
	mov	w1, w23
	add	x0, x0, :lo12:.LC164
	mov	w2, w19
	bl	printk
.L2168:
	mov	w0, w19
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
	adrp	x19, .LANCHOR4
	uxth	w20, w1
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 165
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	ldr	x0, [x19,1192]
	bl	ftl_memset
	ldr	x0, [x19,1208]
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
	bne	.L2179
.L2181:
	mov	w19, -1
	b	.L2180
.L2179:
	adrp	x21, .LANCHOR2
	ldr	x2, [x19,1192]
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x3, [x19,1208]
	mov	w0, w22
	mov	w1, w20
	add	x21, x21, 8
	ldrb	w4, [x21,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2181
	ldr	x2, [x19,1192]
	mov	w0, w22
	ldr	x3, [x19,1208]
	mov	w1, w20
	ldrb	w4, [x21,9]
	bl	flash_read_page
	cmn	w0, #1
	beq	.L2181
	ldr	x0, [x19,1192]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L2181
	ldr	x0, [x19,1208]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csetm	w19, ne
.L2180:
	mov	w1, w20
	mov	w0, w22
	bl	flash_erase_block
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.30, %function
fw_flash_page_prog.constprop.30:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x4, x4, :lo12:.LANCHOR0
	str	x21, [sp,32]
	mov	x21, x1
	stp	x19, x20, [sp,16]
	str	x2, [x29,56]
	ldr	x1, [x4,624]
	ldrb	w20, [x4,724]
	ldrb	w19, [x1,9]
	udiv	w19, w0, w19
	ldrb	w0, [x4,632]
	bl	nandc_bch_sel
	ldr	x3, [x29,56]
	mov	w1, w19
	mov	x2, x21
	mov	w4, 4
	mov	w0, 0
	bl	flash_prog_page
	mov	w19, w0
	mov	w0, w20
	bl	nandc_bch_sel
	ldr	x21, [sp,32]
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	fw_flash_page_prog.constprop.30, .-fw_flash_page_prog.constprop.30
	.text
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	stp	x25, x26, [sp,64]
	uxtb	w25, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	ubfx	x24, x1, 21, 3
	mov	x27, x2
	ldrb	w0, [x0,633]
	mov	x26, x3
	and	w20, w1, 2097151
	ubfx	x23, x1, 24, 2
	cmp	w0, w24
	bhi	.L2190
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 712
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2190:
	add	x28, x22, :lo12:.LANCHOR0
	add	x0, x28, x24, sxtw
	ldrb	w21, [x0,736]
	ldr	x19, [x28,536]
	bl	nandc_rdy_status
	cbnz	w0, .L2191
	ldrb	w0, [x28,633]
	cmp	w0, 1
	bne	.L2192
	bl	nandc_wait_flash_ready
	b	.L2191
.L2192:
	mov	w0, w24
	mov	w1, w20
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L2191:
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	cbnz	w23, .L2193
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
	ldrb	w0, [x22,#:lo12:.LANCHOR0]
	cbz	w0, .L2194
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L2194
.L2193:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L2194:
	ubfiz	x4, x21, 8, 8
	mov	w0, 128
	add	x19, x19, x4
	str	w0, [x19,2056]
	and	w0, w20, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w20, 8
	str	w0, [x19,2052]
	lsr	w0, w20, 16
	str	w0, [x19,2052]
	mov	w0, w20
	bl	nandc_set_seed
	adrp	x1, .LANCHOR2+17
	mov	x2, x27
	mov	x3, x26
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w25, [x19,2056]
	mov	w0, w21
	bl	nandc_de_cs
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
	ldr	w1, [x19,40]
	ldr	x2, [x19,8]
	ldr	x3, [x19,24]
	bl	flash_start_page_prog
	adrp	x0, .LANCHOR0
	ldr	w4, [x19,40]
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w2, [x1,2835]
	cmp	w2, 255
	beq	.L2199
	ubfx	x4, x4, 21, 3
	add	x1, x1, 784
.L2201:
	add	x3, x1, x2, lsl 6
	ldr	w5, [x3,40]
	ubfx	x5, x5, 21, 3
	cmp	w4, w5
	bne	.L2200
	ldrb	w5, [x3,58]
	add	x3, x3, 48
	cmp	w5, 6
	bne	.L2200
	mov	w1, 3
	strb	w1, [x3,10]
	b	.L2199
.L2200:
	lsl	x2, x2, 6
	ldrb	w2, [x1,x2]
	cmp	w2, 255
	bne	.L2201
.L2199:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 2835
	strb	w1, [x19]
	mov	x1, x19
	bl	buf_add_tail
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	queue_prog_cmd, .-queue_prog_cmd
	.align	2
	.global	sblk_prog_page
	.type	sblk_prog_page, %function
sblk_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	uxtb	w20, w1
	ldrh	w0, [x0,50]
	cbz	w0, .L2207
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2207
	adrp	x0, .LC165
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC165
	mov	w2, w20
	bl	printk
.L2207:
	adrp	x24, .LANCHOR0
	adrp	x25, .LC0
	add	x23, x24, :lo12:.LANCHOR0
	mov	w21, 0
	add	x27, x23, 784
	add	x25, x25, :lo12:.LC0
.L2208:
	cbz	w20, .L2238
	ldrb	w26, [x19]
	ldr	w22, [x19,40]
.L2209:
	mov	w0, w22
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2239
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2209
.L2239:
	cmp	w20, 1
	beq	.L2211
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w0, [x0,781]
	cbnz	w0, .L2212
.L2211:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L2213
.L2212:
	ldrb	w0, [x19]
	ubfx	x28, x22, 21, 3
	cmp	w0, 255
	bne	.L2214
	adrp	x1, .LANCHOR3
	mov	w2, 546
	add	x1, x1, :lo12:.LANCHOR3
	mov	x0, x25
	add	x1, x1, 320
	bl	printk
	bl	dump_stack
.L2214:
	ldrb	w0, [x19]
	add	x0, x27, x0, lsl 6
	ldr	w4, [x0,40]
	ubfx	x0, x4, 21, 3
	cmp	w28, w0
	bne	.L2215
	ldrh	w3, [x23,3340]
	mov	w1, 21
	ldrb	w6, [x23,3338]
	mov	w28, 1
	sub	w1, w1, w3
	lsr	w0, w22, w3
	sub	w6, w6, #1
	lsl	w1, w28, w1
	sub	w1, w1, #1
	lsl	w2, w28, w3
	uxth	w7, w6
	sub	w2, w2, #1
	and	w5, w7, w0
	lsr	w0, w4, w3
	uxth	w1, w1
	and	w3, w7, w0
	and	w3, w1, w3
	and	w0, w1, w5
	cmp	w0, w3
	uxth	w2, w2
	beq	.L2215
	and	w22, w2, w22
	and	w2, w2, w4
	cmp	w22, w2
	bne	.L2215
	cmp	w21, w6
	beq	.L2215
	ldr	w1, [x19,40]
	mov	w0, 17
	ldr	x2, [x19,8]
	add	w21, w21, w28
	ldr	x3, [x19,24]
	bl	flash_start_page_prog
	strb	w28, [x19,59]
	mov	w0, 8
	strb	w0, [x19,58]
	mov	w0, -1
	mov	x1, x19
	strb	w0, [x19]
	add	x0, x23, 2835
	bl	buf_add_tail
	b	.L2213
.L2215:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
.L2213:
	subs	w20, w20, #1
	beq	.L2238
	add	x19, x24, :lo12:.LANCHOR0
	sbfiz	x26, x26, 6, 32
	add	x19, x19, 784
	add	x19, x19, x26
	b	.L2208
.L2238:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	sblk_prog_page, .-sblk_prog_page
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
	adrp	x20, .LANCHOR4
	ldrb	w1, [x0,2857]
	cbz	w1, .L2241
	add	x2, x20, :lo12:.LANCHOR4
	add	x0, x0, 784
	ldrb	w2, [x2,1266]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2241:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strb	wzr, [x2,2857]
	strb	w0, [x20,1266]
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
	adrp	x0, .LANCHOR0+2857
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+2857]
	cbz	w0, .L2245
	bl	timer_get_time
	adrp	x1, .LANCHOR4+1268
	ldr	w1, [x1,#:lo12:.LANCHOR4+1268]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2245
	bl	ftl_flush
.L2245:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w4
	adrp	x4, .LANCHOR0
	stp	x25, x26, [sp,64]
	uxtb	w25, w0
	add	x0, x4, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxtb	w26, w3
	uxtb	w21, w1
	ldrb	w0, [x0,633]
	uxtb	w22, w2
	mov	x24, x5
	mov	x23, x6
	cmp	w0, w26
	mov	x19, x4
	bhi	.L2250
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 661
	add	x1, x1, 336
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2250:
	add	x4, x19, :lo12:.LANCHOR0
	add	x26, x4, x26, sxtw
	ldrb	w26, [x26,736]
	mov	w0, w26
	ldr	x19, [x4,536]
	bl	nandc_cs
	cbz	w25, .L2251
	sxtw	x1, w26
	add	x1, x1, 8
	add	x1, x19, x1, lsl 8
	str	w25, [x1,8]
.L2251:
	ubfiz	x7, x26, 8, 8
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
	adrp	x1, .LANCHOR2+17
	mov	x2, x24
	mov	x3, x23
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+17]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w22, [x19,2056]
	mov	w0, w26
	bl	nandc_de_cs
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
	stp	x29, x30, [sp, -48]!
	mov	w1, 1
	mov	w2, 26
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x19, x0
	ldr	x6, [x0]
	ldr	w20, [x6,40]
	ldr	x5, [x6,8]
	and	w21, w20, 2097151
	ldrb	w0, [x6,60]
	ubfx	x20, x20, 21, 3
	ldr	x6, [x6,24]
	mov	w3, w20
	mov	w4, w21
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x19,8]
	mov	w3, w20
	ldr	x0, [x19]
	mov	w4, w21
	mov	w1, 2
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x19,16]
	mov	w3, w20
	ldr	x0, [x19]
	mov	w4, w21
	mov	w2, 16
	mov	w1, 3
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	ldr	x1, [x19]
	mov	w0, 4
	strb	w0, [x1,58]
	mov	w0, 1
	strb	w0, [x1,59]
	mov	w0, -1
	strb	w0, [x1]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 2835
	bl	buf_add_tail
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
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
.L2257:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2259
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2257
.L2259:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x19, x0
	mov	x22, x1
	ldr	x0, [x0]
	mov	w24, w2
	ldr	w20, [x0,40]
.L2261:
	mov	w0, w20
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2265
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2261
.L2265:
	cmp	w24, 2
	bne	.L2263
	ldr	x6, [x19]
	mov	w1, 1
	ldr	x0, [x22]
	mov	w2, 17
	ldr	w20, [x6,40]
	ldr	x5, [x6,8]
	and	w23, w20, 2097151
	ldr	w21, [x0,40]
	ubfx	x20, x20, 21, 3
	ldrb	w0, [x6,60]
	ldr	x6, [x6,24]
	mov	w3, w20
	mov	w4, w23
	and	w21, w21, 2097151
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22]
	mov	w3, w20
	ldr	x0, [x19]
	mov	w4, w21
	mov	w1, 1
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x19,8]
	mov	w1, w24
	ldr	x0, [x19]
	mov	w3, w20
	mov	w4, w23
	mov	w2, 17
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,8]
	mov	w1, w24
	ldr	x0, [x19]
	mov	w3, w20
	mov	w4, w21
	mov	w2, 26
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x6, [x19,16]
	mov	w3, w20
	ldr	x0, [x19]
	mov	w4, w23
	mov	w1, 3
	mov	w2, 17
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x6, [x22,16]
	mov	w1, 3
	ldr	x0, [x19]
	mov	w2, 16
	mov	w3, w20
	mov	w4, w21
	ldr	x5, [x6,8]
	ldrb	w0, [x0,60]
	ldr	x6, [x6,24]
	bl	flash_start_tlc_page_prog
	ldr	x1, [x19]
	mov	w0, 5
	strb	w0, [x1,58]
	mov	w0, 1
	strb	w0, [x1,59]
	mov	w0, -1
	strb	w0, [x1]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 2835
	bl	buf_add_tail
	b	.L2264
.L2263:
	mov	x0, x19
	bl	queue_tlc_prog_cmd
.L2264:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	stp	x29, x30, [sp, -96]!
	adrp	x6, .LANCHOR0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x6, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, w1
	mov	x22, x2
	ldrb	w0, [x0,633]
	mov	x21, x3
	mov	w24, w4
	uxtb	w25, w5
	cmp	w0, w23
	ubfx	x28, x20, 24, 2
	mov	x19, x6
	bhi	.L2267
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 484
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2267:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w26, w20
	add	x1, x0, x23, sxtw
	ldrb	w27, [x1,736]
	cbnz	w28, .L2268
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L2269
	ldrb	w0, [x0,1]
	cbz	w0, .L2268
.L2269:
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w26, [x6,2]
	ldrb	w1, [x6,1]
	udiv	w2, w20, w26
	mul	w26, w2, w26
	sub	w0, w20, w26
	cbz	w1, .L2270
	add	w26, w26, w0, lsl 1
	b	.L2268
.L2270:
	add	x6, x6, 4
	ldrh	w0, [x6,w0,uxtw 1]
	add	w26, w0, w26
.L2268:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2271
	adrp	x0, .LC166
	mov	w1, w27
	add	x0, x0, :lo12:.LC166
	mov	w2, w20
	bl	printk
.L2271:
	mov	w1, w26
	mov	w0, w27
	mov	x2, x22
	mov	x3, x21
	mov	w4, w24
	bl	flash_prog_page
	mov	w26, w0
	cbz	w25, .L2272
	adrp	x19, .LANCHOR4
	mov	w0, w23
	add	x25, x19, :lo12:.LANCHOR4
	mov	w1, w20
	mov	w4, w24
	ldr	x2, [x25,1248]
	ldr	x3, [x25,1256]
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2273
	cmn	w0, #1
	beq	.L2273
	ldr	x0, [x25,1248]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2273
	ldr	x0, [x25,1256]
	ldr	w1, [x21]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2272
.L2273:
	mov	w2, 4
	adrp	x0, .LC167
	mov	x1, x22
	mov	w3, w2
	add	x0, x0, :lo12:.LC167
	add	x19, x19, :lo12:.LANCHOR4
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC168
	mov	x1, x21
	mov	w3, w2
	add	x0, x0, :lo12:.LC168
	bl	rknand_print_hex
	ldr	x1, [x19,1256]
	mov	w2, 4
	adrp	x0, .LC169
	mov	w3, w2
	add	x0, x0, :lo12:.LC169
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC170
	ldr	x1, [x19,1248]
	add	x0, x0, :lo12:.LC170
	mov	w3, w2
	bl	rknand_print_hex
	b	.L2274
.L2272:
	cmn	w26, #1
	mov	w0, w26
	bne	.L2275
.L2274:
	adrp	x0, .LC171
	mov	w1, w20
	add	x0, x0, :lo12:.LC171
	bl	printk
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 512
	add	x1, x1, 368
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, -1
.L2275:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
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
	bne	.L2289
	adrp	x2, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 392
	mov	w2, 2150
	bl	printk
	bl	dump_stack
	adrp	x0, .LC171
	mov	w1, w20
	add	x0, x0, :lo12:.LC171
	bl	printk
.L2289:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_prog_page, .-ftl_prog_page
	.align	2
	.global	ftl_info_flush
	.type	ftl_info_flush, %function
ftl_info_flush:
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	adrp	x22, .LANCHOR4
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR3
	add	x19, x22, :lo12:.LANCHOR4
	ldrb	w2, [x0,2832]
	adrp	x24, .LC172
	add	x23, x23, :lo12:.LANCHOR3
	add	x0, x19, 1280
	lsl	w2, w2, 1
	mov	w25, 0
	add	x24, x24, :lo12:.LC172
	add	x23, x23, 408
	bl	ftl_memset
.L2291:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w27, [x19,1536]
	ldrh	w20, [x19,1538]
	ldr	x2, [x0,2864]
	ldrh	w28, [x0,3350]
	ldr	w1, [x2,4]
	add	w1, w1, 1
	str	w1, [x2,4]
	str	w26, [x19,1280]
	ldr	x3, [x0,2864]
	ldr	w0, [x3,4]
	str	w0, [x19,1284]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2292
	ldr	w3, [x3,4]
	mov	x0, x24
	mov	w1, w27
	mov	w2, w20
	bl	printk
.L2292:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,1538]
	ldrh	w1, [x1,3300]
	cmp	w1, w0
	bhi	.L2293
	adrp	x20, .LC0
	add	x20, x20, :lo12:.LC0
.L2298:
	ldrb	w1, [x19,1537]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x19,1537]
	cmp	w1, 7
	bls	.L2294
	mov	x1, 0
.L2297:
	add	x0, x21, :lo12:.LANCHOR0
	uxth	w25, w1
	ldr	x2, [x0,728]
	add	w0, w1, 8
	add	x0, x2, x0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2295
	mov	x1, x23
	mov	w2, 742
	mov	x0, x20
	bl	printk
	bl	dump_stack
	b	.L2296
.L2295:
	cmp	w2, 255
	bne	.L2296
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2297
	mov	w25, w1
.L2296:
	strb	w25, [x19,1537]
	mov	w25, 1
.L2294:
	add	x27, x21, :lo12:.LANCHOR0
	ldrb	w1, [x19,1537]
	ldr	x0, [x27,728]
	add	x1, x0, x1
	ldrb	w2, [x1,40]
	strb	w2, [x19,1536]
	cmp	w2, 255
	beq	.L2298
	ldrh	w20, [x27,3350]
	mov	w0, 0
	mul	w20, w2, w20
	mov	w1, w20
	bl	flash_erase_block
	ldrb	w4, [x27,2832]
	mov	w1, w20
	mov	w0, 0
	add	x2, x19, 1600
	add	x3, x19, 1280
	add	w20, w20, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x19,1538]
	b	.L2299
.L2293:
	madd	w20, w27, w28, w20
	cbnz	w0, .L2299
	mov	w1, w20
	bl	flash_erase_block
.L2299:
	add	x4, x21, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w0, 0
	add	x2, x19, 1600
	add	x3, x19, 1280
	ldrb	w4, [x4,2832]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x19,1538]
	adrp	x0, .LANCHOR5
	add	w1, w1, 1
	strh	w1, [x19,1538]
	beq	.L2300
	add	x1, x0, :lo12:.LANCHOR5
	ldrb	w1, [x1,576]
	cbz	w1, .L2301
.L2300:
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,576]
	b	.L2291
.L2301:
	cbnz	w25, .L2302
.L2310:
	add	x22, x22, :lo12:.LANCHOR4
	ldrb	w0, [x22,1536]
	cmp	w0, 255
	bne	.L2304
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 778
	add	x1, x1, 408
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2304
.L2302:
	ldrb	w19, [x19,1537]
	adrp	x20, .LANCHOR3
	adrp	x23, .LC0
	add	x20, x20, :lo12:.LANCHOR3
	add	w19, w19, 1
	add	x23, x23, :lo12:.LC0
	add	x20, x20, 408
.L2305:
	cmp	w19, 7
	bhi	.L2310
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,728]
	add	w0, w19, 8
	add	x0, x1, x0, sxtw
	ldrb	w24, [x0,32]
	add	w0, w24, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2306
	mov	x1, x20
	mov	w2, 771
	mov	x0, x23
	bl	printk
	bl	dump_stack
	b	.L2307
.L2306:
	cmp	w24, 255
	beq	.L2308
.L2307:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,3350]
	mov	w0, 0
	mul	w1, w24, w1
	bl	flash_erase_block
.L2308:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2305
.L2304:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
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
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR5
	stp	x23, x24, [sp,48]
	add	x23, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR4
	strb	w0, [x20,576]
	ldrh	w1, [x23,576]
	add	x22, x21, :lo12:.LANCHOR4
	add	x24, x22, 1600
	strb	w0, [x20,578]
	mov	x0, x24
	strb	wzr, [x20,577]
	add	x1, x24, x1, lsl 2
	str	x1, [x23,2864]
	mov	w1, 0
	str	x24, [x23,584]
	adrp	x26, .LC173
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC173
	add	x0, x20, 640
	mov	w1, 0
	mov	w2, 16384
	mov	w20, 7
	bl	ftl_memset
	strb	wzr, [x22,1537]
	ldr	x0, [x23,728]
	add	x23, x22, 1280
	strh	wzr, [x22,1538]
	ldrb	w0, [x0,40]
	strb	w0, [x22,1536]
.L2328:
	add	x5, x19, :lo12:.LANCHOR0
	add	w0, w20, 8
	sxth	w25, w20
	ldr	x1, [x5,728]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L2324
	ldrh	w22, [x5,3350]
	mov	w0, 0
	ldrb	w4, [x5,2832]
	mov	x2, x24
	mov	x3, x23
	str	x5, [x29,104]
	mul	w22, w1, w22
	mov	w1, w22
	bl	ftl_read_page
	cmn	w0, #1
	mov	w28, w0
	ldr	x5, [x29,104]
	bne	.L2325
	ldrb	w4, [x5,2832]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L2325:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2326
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w2, w28
	mov	w3, 672
	ldr	x4, [x0,2864]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L2326:
	cmn	w28, #1
	beq	.L2324
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2336
.L2324:
	sub	w20, w20, #1
	cmn	w20, #1
	bne	.L2328
	mov	w25, 0
	b	.L2327
.L2336:
	mov	w20, w25
.L2327:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2329
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w2, 4800
	ldr	x3, [x0,2864]
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	ldr	w3, [x3]
	bl	printk
.L2329:
	cmn	w20, #1
	bne	.L2330
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 1600
	mov	w2, 16384
	bl	ftl_memset
	ldr	x1, [x19,2864]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, 20
	movk	w0, 0x6, lsl 16
	ldr	x1, [x19,2864]
	str	w0, [x1,12]
	mov	w0, w20
	b	.L2331
.L2330:
	add	x26, x19, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR4
	add	w0, w25, 8
	add	x24, x23, 1600
	mov	x2, x24
	mov	w4, 4
	ldr	x1, [x26,728]
	add	x0, x1, x0, sxtw
	strb	w25, [x23,1537]
	add	x25, x23, 1280
	ldrb	w1, [x0,32]
	mov	x3, x25
	mov	w0, 0
	strb	w1, [x23,1536]
	bl	flash_get_last_written_page
	sxth	w22, w0
	add	w0, w0, 1
	uxth	w20, w0
	ldrb	w0, [x23,1536]
	ldrh	w23, [x26,3350]
	mul	w23, w0, w23
.L2332:
	tbnz	w22, #31, .L2335
	add	x26, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w22, w23
	mov	x2, x24
	mov	x3, x25
	ldrb	w4, [x26,2832]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L2333
	ldr	x0, [x26,2864]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2333
.L2335:
	add	x21, x21, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR0
	strh	w20, [x21,1538]
	bl	ftl_tmp_into_update
	ldr	x1, [x19,2864]
	ldr	w0, [x1,64]
	add	w0, w0, 1
	str	w0, [x1,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	b	.L2331
.L2333:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L2332
.L2331:
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
	bls	.L2354
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2367
.L2354:
	bcs	.L2355
.L2367:
	str	w0, [x1,520]
.L2355:
	adrp	x23, .LANCHOR3
	adrp	x19, .LANCHOR4
	add	x23, x23, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, 0
	add	x23, x23, 424
	add	x19, x19, 1280
	bl	ftl_total_vpn_update
.L2356:
	adrp	x24, .LANCHOR5
	add	x24, x24, :lo12:.LANCHOR5
	add	x25, x24, 640
.L2359:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2864]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3300]
	cmp	w1, w0
	bcc	.L2357
	bl	ftl_ext_alloc_new_blk
.L2357:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2358
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 1746
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2358:
	add	x21, x22, :lo12:.LANCHOR0
	mov	w0, 21
	mov	w1, 1
	ldr	x2, [x21,2864]
	ldrh	w20, [x21,3340]
	ldrh	w3, [x2,130]
	sub	w20, w0, w20
	ldrh	w0, [x2,140]
	asr	w26, w3, w20
	lsl	w20, w1, w20
	sub	w20, w20, #1
	ldrb	w2, [x21,2832]
	and	w3, w20, w3
	ldrh	w20, [x21,3350]
	mov	w1, 0
	lsl	w2, w2, 1
	madd	w20, w3, w20, w0
	mov	x0, x19
	bl	ftl_memset
	str	wzr, [x19]
	ldr	x0, [x21,2864]
	uxth	w20, w20
	ldrb	w4, [x21,2832]
	mov	w1, w20
	mov	x2, x25
	mov	x3, x19
	ldr	w0, [x0,56]
	str	w0, [x19,4]
	mov	w0, w26
	bl	ftl_prog_page
	ldr	x2, [x21,2864]
	ldrh	w1, [x2,140]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x2,140]
	cmp	w1, 1
	beq	.L2359
	cmn	w0, #1
	beq	.L2360
	ldrb	w0, [x24,578]
	cbz	w0, .L2361
.L2360:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,578]
	b	.L2356
.L2361:
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
	stp	x29, x30, [sp, -96]!
	mov	w4, 4
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x27, [sp,80]
	stp	x25, x26, [sp,64]
	add	x26, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR5
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR4
	add	x2, x20, :lo12:.LANCHOR5
	ldr	x0, [x26,2864]
	add	x3, x21, :lo12:.LANCHOR4
	ldrh	w22, [x26,3340]
	add	x2, x2, 640
	add	x3, x3, 1280
	strh	wzr, [x26,3888]
	ldrh	w1, [x0,130]
	mov	w0, 21
	sub	w22, w0, w22
	mov	w0, 1
	asr	w27, w1, w22
	lsl	w22, w0, w22
	sub	w22, w22, #1
	and	w22, w22, w1
	uxtb	w25, w27
	mov	w0, w25
	mov	w1, w22
	bl	flash_get_last_written_page
	sxth	w24, w0
	adrp	x0, .LANCHOR2
	mov	w23, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2369
	ldr	x5, [x26,2864]
	adrp	x1, .LANCHOR3
	adrp	x0, .LC175
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC175
	add	x1, x1, 448
	ldrh	w5, [x5,130]
	mov	w2, 1790
	mov	w3, w24
	uxth	w4, w27
	bl	printk
.L2369:
	add	x20, x20, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR4
	mov	w26, w24
	add	x20, x20, 640
	add	x21, x21, 1280
.L2370:
	tbnz	w26, #31, .L2375
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, w25
	mov	x2, x20
	mov	x3, x21
	ldrh	w1, [x27,3350]
	ldrb	w4, [x27,2832]
	madd	w1, w22, w1, w26
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2371
	cmn	w0, #1
	beq	.L2371
	ldr	x0, [x27,608]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2371
.L2375:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,140]
	cmp	w1, w24
	bgt	.L2373
	add	w23, w23, 1
	strh	w23, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2373
.L2371:
	sub	w26, w26, #1
	sxth	w26, w26
	b	.L2370
.L2373:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x20, [x19,608]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x20,520]
	ldr	x20, [x19,608]
	bl	timer_get_time
	str	w0, [x20,604]
	ldr	x1, [x19,608]
	mov	w0, -1
	strh	w0, [x19,3224]
	strh	w0, [x1,584]
	strh	w0, [x1,586]
	strh	w0, [x1,588]
	strh	w0, [x1,590]
	mov	w0, 0
	str	wzr, [x1,608]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_prog_ppa_page
	.type	ftl_prog_ppa_page, %function
ftl_prog_ppa_page:
	stp	x29, x30, [sp, -16]!
	mov	w5, w0
	mov	x7, x1
	mov	x6, x2
	add	x29, sp, 0
	mov	w4, w3
	ubfx	x0, x5, 21, 3
	and	w1, w5, 2097151
	mov	x2, x7
	mov	x3, x6
	bl	ftl_prog_page
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_prog_ppa_page, .-ftl_prog_ppa_page
	.align	2
	.global	ftl_write_last_log_page
	.type	ftl_write_last_log_page, %function
ftl_write_last_log_page:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x19, .LANCHOR0
	mov	w21, -1
	ldrh	w1, [x0,6]
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w24, [x0,12]
	cmp	w1, 1
	ldr	x22, [x19,3872]
	bne	.L2387
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L2387
	ldrh	w0, [x20]
	add	x22, x22, x24, uxth 2
	bl	ftl_vpn_decrement
	adrp	x1, .LANCHOR6
	mov	w0, 15555
	add	x1, x1, :lo12:.LANCHOR6
	movk	w0, 0xf55f, lsl 16
	add	x2, x1, 3968
	str	x2, [x29,72]
	str	w0, [x1,3968]
	ldrb	w0, [x19,3276]
	ldrh	w1, [x19,3300]
	mul	w1, w1, w0
	mov	x0, x22
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,72]
	mov	x1, x22
	ldrb	w3, [x19,2832]
	str	w0, [x2,4]
	mov	w0, w23
	str	w21, [x2,8]
	str	w21, [x2,12]
	bl	ftl_prog_ppa_page
.L2387:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	stp	x19, x20, [sp,16]
	mov	x19, x0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x27, x1
	tbz	x0, 12, .L2392
	adrp	x0, .LC176
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC176
	bl	printk
.L2392:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2393
	adrp	x0, .LC177
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC177
	bl	printk
.L2393:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2394
	adrp	x0, .LC178
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC178
	bl	printk
.L2394:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2395
	adrp	x0, .LC179
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC179
	bl	printk
.L2395:
	ldrh	w0, [x19,10]
	adrp	x21, .LANCHOR0
	strh	w0, [x19,14]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldrh	w0, [x0,576]
	cmp	w1, w0
	bcs	.L2391
	mov	w0, 1
	adrp	x25, .LANCHOR4
	adrp	x26, .LANCHOR3
	add	x25, x25, :lo12:.LANCHOR4
	bl	buf_alloc
	add	x26, x26, :lo12:.LANCHOR3
	ldrb	w22, [x19,5]
	mov	x20, x0
	ldrh	w23, [x19,2]
	add	x0, x26, 472
	mov	w28, 0
	add	x25, x25, 1280
	str	x0, [x29,120]
.L2398:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	cmp	w0, w23
	bls	.L2401
	ldrb	w22, [x19,5]
.L2399:
	ldrb	w0, [x19,9]
	cmp	w0, w22
	bls	.L2475
	add	x0, x19, x22, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2400
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x2, [x20,24]
	ldrh	w1, [x3,3350]
	ldrb	w3, [x3,2832]
	madd	w0, w0, w1, w23
	ldr	x1, [x20,8]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w26, w0
	beq	.L2401
	cmn	w0, #1
	beq	.L2402
	ldr	x0, [x20,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2402
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2402
	ldr	x0, [x20,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2401
.L2402:
	adrp	x0, .LANCHOR5+577
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+577]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w23, w0, w22
	cmp	w1, w0
	beq	.L2403
	adrp	x0, .LC0
	ldr	x1, [x29,120]
	mov	w2, 1395
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2403:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2404
	adrp	x0, .LC0
	ldr	x1, [x29,120]
	mov	w2, 1396
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2404:
	cmn	w26, #1
	beq	.L2408
	ldr	x0, [x20,24]
	ldr	w0, [x0,4]
	cmn	w0, #1
	beq	.L2408
	bl	lpa_hash_get_ppa
	cbz	x27, .L2406
	ldr	x2, [x20,24]
	ldr	w3, [x2,8]
	cmp	w0, w3
	beq	.L2406
	cmn	w0, #1
	beq	.L2406
	add	x4, x21, :lo12:.LANCHOR0
	mov	w5, 21
	mov	w28, 1
	ldrh	w3, [x4,3340]
	lsr	w1, w0, w3
	sub	w3, w5, w3
	lsl	w3, w28, w3
	sub	w3, w3, #1
	and	w3, w1, w3
	ldrb	w1, [x4,3338]
	udiv	w3, w3, w1
	ldrh	w1, [x27]
	cmp	w1, w3, uxth
	bne	.L2406
	ldr	w6, [x2]
	mov	x2, x25
	ldrb	w3, [x4,2832]
	ldr	x1, [x20,8]
	str	x5, [x29,96]
	str	x6, [x29,104]
	str	x4, [x29,112]
	bl	ftl_read_ppa_page
	ldr	w0, [x25]
	ldr	x6, [x29,104]
	ldr	x4, [x29,112]
	cmp	w0, w6
	ldr	x5, [x29,96]
	bcc	.L2406
	ldr	x0, [x20,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2408
	ldrh	w0, [x4,3340]
	sub	w5, w5, w0
	lsr	w1, w1, w0
	lsl	w28, w28, w5
	ldrb	w0, [x4,3338]
	sub	w28, w28, #1
	and	w1, w28, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2408
.L2406:
	ldr	x2, [x20,24]
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2840]
	cmp	w3, w0
	bcs	.L2408
	ldrb	w0, [x19,9]
	ldrh	w4, [x1,3300]
	ldrh	w3, [x19,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L2476
.L2408:
	ldrh	w0, [x19,6]
	mov	w28, 1
	sub	w0, w0, #1
	strh	w0, [x19,6]
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
.L2400:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2399
.L2475:
	add	w23, w23, 1
	strb	wzr, [x19,5]
	uxth	w23, w23
	b	.L2398
.L2401:
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	strh	w23, [x19,2]
	add	w1, w1, w0
	ldrh	w2, [x2,3300]
	ldrb	w0, [x19,9]
	strb	w22, [x19,5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2412
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1472
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2412:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w3, [x19,10]
	ldr	x4, [x0,3872]
	mov	w0, 0
	mov	w2, w0
.L2413:
	cmp	w2, w3
	bcs	.L2477
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L2414
	add	w1, w0, 1
	uxth	w0, w1
.L2414:
	add	w2, w2, 1
	b	.L2413
.L2477:
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w22, [x2,3300]
	madd	w0, w1, w22, w0
	uxth	w22, w0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2416
	ldrh	w1, [x19]
	adrp	x0, .LC180
	ldr	x4, [x2,600]
	add	x0, x0, :lo12:.LC180
	ubfiz	x3, x1, 1, 16
	mov	w2, w22
	ldrh	w3, [x4,x3]
	bl	printk
.L2416:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	adrp	x23, .LC181
	mov	w25, -1
	add	x23, x23, :lo12:.LC181
	ldr	x0, [x0,600]
	strh	w22, [x0,x1,lsl 1]
	mov	x0, x20
	mov	w22, 0
	bl	buf_free
.L2417:
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w0, [x2,3276]
	cmp	w22, w0, lsl 1
	bcs	.L2478
	cbz	w28, .L2418
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L2418
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w26, w0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2419
	mov	x0, x23
	mov	w1, w26
	bl	printk
.L2419:
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x0, [x20,24]
	ldr	x1, [x3,2864]
	ldr	w1, [x1,8]
	str	w1, [x0]
	ldr	x0, [x20,24]
	str	w25, [x0,4]
	ldr	x0, [x20,24]
	str	w25, [x0,8]
	ldr	x0, [x20,24]
	str	wzr, [x0,12]
	ldr	x0, [x20,8]
	str	wzr, [x0]
	mov	w0, w26
	ldr	x1, [x20,8]
	ldr	x2, [x20,24]
	ldrb	w3, [x3,2832]
	bl	ftl_prog_ppa_page
	ldrh	w0, [x19]
	bl	ftl_vpn_decrement
.L2418:
	add	w22, w22, 1
	b	.L2417
.L2478:
	ldrh	w1, [x2,3300]
	ldrh	w0, [x19,12]
	ldrb	w3, [x19,9]
	madd	w0, w1, w3, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x2,3872]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L2421
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1502
	add	x1, x1, 472
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2421:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2391
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2391
.L2476:
	ldr	x0, [x1,2864]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2409
	str	w2, [x0,8]
.L2409:
	ldr	x1, [x20,24]
	ldrh	w0, [x19,10]
	ldrh	w2, [x19,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	b	.L2408
.L2391:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR3
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	add	x22, x22, :lo12:.LANCHOR3
	adrp	x23, .LC0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w21, w0
	mov	x24, x1
	adrp	x25, .LANCHOR0
	add	x23, x23, :lo12:.LC0
	add	x22, x22, 496
.L2480:
	add	x2, x25, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x2,2864]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3300]
	cmp	w3, w1
	bcs	.L2481
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2482
.L2481:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2482:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2483
	mov	x1, x22
	mov	w2, 230
	mov	x0, x23
	bl	printk
	bl	dump_stack
.L2483:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR7
	add	x27, x20, :lo12:.LANCHOR7
	mov	w2, 64
	ldr	x1, [x19,2864]
	ldrh	w26, [x19,3350]
	ldrh	w0, [x1,692]
	ldrh	w1, [x1,696]
	madd	w26, w0, w26, w1
	ldr	x0, [x27,-128]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x27,-128]
	mov	x2, x24
	str	w21, [x0]
	ldr	x1, [x19,2864]
	ldr	x0, [x27,-128]
	ldrb	w4, [x19,2832]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	w1, w26
	ldr	x0, [x19,2864]
	ldr	x3, [x27,-128]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x1, [x19,2864]
	ldrh	w2, [x1,696]
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x1,696]
	cmp	w2, 1
	beq	.L2484
	ldrb	w2, [x27,-120]
	cbz	w2, .L2485
.L2484:
	add	x20, x20, :lo12:.LANCHOR7
	strb	wzr, [x20,-120]
	b	.L2480
.L2485:
	cmn	w0, #1
	bne	.L2487
	adrp	x0, .LC182
	mov	w1, w26
	add	x0, x0, :lo12:.LC182
	bl	printk
	b	.L2480
.L2487:
	ldrh	w0, [x1,698]
	cmp	w21, w0
	bcs	.L2488
	add	x21, x1, x21, uxtw 2
	str	w26, [x21,704]
.L2488:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	tbz	x0, 12, .L2493
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 250
	add	x2, x2, 512
	bl	printk
.L2493:
	adrp	x22, .LANCHOR4
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC184
	adrp	x25, .LC185
	ldr	x0, [x0,1232]
	mov	w26, 0
	add	x24, x24, :lo12:.LC184
	add	x25, x25, :lo12:.LC185
	bl	ftl_memset
	ldr	x1, [x19,728]
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,728]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2494:
	add	x21, x22, :lo12:.LANCHOR4
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,1240]
	ldrh	w19, [x21,1242]
	mov	w1, w20
	ldrh	w28, [x27,2]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,1092]
	ldrh	w0, [x21,1242]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2495
	ldr	x1, [x27,728]
	ldrb	w19, [x21,1241]
	strh	wzr, [x21,1242]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,1240]
	ldr	x20, [x27,728]
	strb	w0, [x21,1241]
	mov	x0, x20
	strb	w19, [x21,1240]
	ldrh	w1, [x20,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x20,8]
	bl	js_hash
	str	w0, [x20,12]
	ldrh	w0, [x27,2]
	mul	w19, w19, w0
	mov	w0, 0
	b	.L2503
.L2495:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2496
.L2503:
	mov	w1, w19
	bl	flash_erase_block
.L2496:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR4
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,728]
	ldr	x0, [x20,1232]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,1232]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,728]
	ldr	x3, [x20,1232]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,1242]
	add	w1, w1, 1
	strh	w1, [x20,1242]
	bne	.L2497
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2494
.L2497:
	cbnz	w26, .L2504
	mov	w26, 1
	b	.L2494
.L2504:
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
	tbz	x0, 12, .L2506
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2258
	add	x2, x2, 536
	bl	printk
.L2506:
	adrp	x21, .LANCHOR4
	mov	x0, x19
	add	x23, x21, :lo12:.LANCHOR4
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w25, 44
	str	wzr, [x23,1220]
	bl	nandc_init
	add	x1, x20, :lo12:.LANCHOR2
	mov	w0, 1
	add	x1, x1, 8
	strb	w0, [x22,633]
	str	x1, [x22,624]
	add	x0, x22, 736
	mov	w1, 0
	mov	w2, 8
	add	x22, x22, 748
	bl	ftl_memset
	mov	x24, x22
	add	x0, x23, 1096
	mov	w1, 0
	mov	w2, 32
	mov	w23, 0
	bl	ftl_memset
.L2512:
	mov	w0, w23
	mov	x1, x22
	bl	flash_read_id
	cbnz	w23, .L2507
	ldrb	w0, [x24]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2508
.L2510:
	mov	w22, -2
	b	.L2579
.L2508:
	ldrb	w0, [x24,1]
	cmp	w0, 255
	beq	.L2510
.L2507:
	ldrb	w0, [x22]
	cmp	w0, 181
	bne	.L2511
	strb	w25, [x22]
.L2511:
	add	w23, w23, 1
	add	x22, x22, 8
	uxtb	w23, w23
	cmp	w23, 4
	bne	.L2512
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x24, x24, 748
.L2515:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x24
	bl	flash_mem_cmp8
	cbnz	w0, .L2513
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L2510
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2517
.L2513:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 36
	bne	.L2515
	b	.L2510
.L2517:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1592]
	cmp	w4, w3
	beq	.L2516
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2517
	mov	w2, w0
.L2516:
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x23, 1592
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
	bhi	.L2518
	ldrb	w1, [x23,20]
	cmp	w1, 60
	bls	.L2519
	mov	w1, 60
	strb	w1, [x23,20]
.L2519:
	cmp	w0, 8
	bne	.L2518
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,748]
	cmp	w0, 137
	beq	.L2543
	cmp	w0, 44
	beq	.L2543
.L2518:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2521
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2283
	add	x2, x2, 536
	bl	printk
.L2521:
	add	x23, x21, :lo12:.LANCHOR4
	mov	w0, 16384
	add	x22, x19, :lo12:.LANCHOR0
	add	x24, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x23,1248]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x23,1192]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x22,728]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1256]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1208]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1232]
	adrp	x0, .LANCHOR7-119
	strb	wzr, [x0,#:lo12:.LANCHOR7-119]
	bl	flash_die_info_init
	ldrb	w0, [x24,26]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,28]
	bl	nandc_bch_sel
	str	xzr, [x23,1224]
	ldr	x1, [x22,624]
	ldrh	w0, [x1,16]
	ubfx	x2, x0, 8, 3
	strb	w2, [x22,780]
	ubfx	x2, x0, 3, 1
	strb	w2, [x23,1265]
	ubfx	x2, x0, 4, 1
	strb	w2, [x22,781]
	ubfx	x2, x0, 12, 1
	strb	w2, [x22,3272]
	ubfx	x2, x0, 13, 1
	ubfx	x0, x0, 14, 1
	strb	w0, [x22,1]
	ldrb	w0, [x1,28]
	mov	w1, 60
	strb	w1, [x22,632]
	ldrb	w1, [x22,516]
	strb	w2, [x22,3348]
	strb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L2522
	mov	w1, 70
	strb	w1, [x22,632]
.L2522:
	add	x2, x20, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	add	x2, x2, 8
	add	x3, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1264]
	ldrb	w0, [x2,19]
	ldrh	w2, [x2,16]
	strb	w0, [x3,725]
	tbz	x2, 6, .L2523
	sub	w2, w0, #17
	uxtb	w2, w2
	cmp	w2, 2
	bhi	.L2524
	adrp	x0, micron_read_retrial
	add	x0, x0, :lo12:micron_read_retrial
	str	x0, [x1,1224]
	mov	w0, 15
	b	.L2581
.L2524:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2544
	cmp	w0, 33
	bne	.L2525
.L2544:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1224]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,726]
	mov	w1, 7
	strb	w1, [x0,1216]
	b	.L2523
.L2525:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w3, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2545
	cbz	w3, .L2527
.L2545:
	add	x2, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x2,1224]
	beq	.L2529
	cmp	w0, 68
	beq	.L2529
	mov	w0, 7
	strb	w0, [x2,1216]
	b	.L2530
.L2529:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1216]
.L2530:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w3, .L2582
	mov	w1, 5
.L2582:
	strb	w1, [x0,726]
	b	.L2523
.L2527:
	sub	w0, w0, #36
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L2523
	adrp	x0, toshiba_3d_read_retrial
	add	x0, x0, :lo12:toshiba_3d_read_retrial
	str	x0, [x1,1224]
	mov	w0, 7
.L2581:
	strb	w0, [x1,1216]
.L2523:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2532
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2331
	add	x2, x2, 536
	bl	printk
.L2532:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,672]
	ldrb	w0, [x0,780]
	tbz	x0, 0, .L2533
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2583
.L2533:
	mov	w0, 4
.L2583:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2535
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2536
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2352
	add	x2, x2, 536
	bl	printk
.L2536:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19,728]
	strb	w1, [x0,32]
	b	.L2579
.L2535:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L2537
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,1224]
.L2537:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,780]
	tbz	x0, 2, .L2539
	ldr	x0, [x19,728]
	ldrb	w0, [x0,19]
	cbz	w0, .L2539
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w1, [x19,2]
	mov	w0, 0
	ldrb	w2, [x21,1240]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,672]
	cbnz	w0, .L2539
	ldr	x1, [x19,728]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2539:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2541
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2409
	add	x2, x2, 536
	bl	printk
.L2541:
	bl	nand_flash_print_info
	mov	w22, 0
	b	.L2579
.L2543:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrb	w1, [x0,28]
	cmp	w1, 3
	bne	.L2518
	strb	wzr, [x0,28]
	b	.L2518
.L2579:
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
	stp	x25, x26, [sp,112]
	uxth	w25, w0
	mov	w0, 1
	stp	x19, x20, [sp,64]
	stp	x23, x24, [sp,96]
	stp	x27, x28, [sp,128]
	stp	x21, x22, [sp,80]
	bl	buf_alloc
	ldr	x24, [x0,8]
	mov	w20, 0
	adrp	x22, .LC186
	adrp	x23, .LC187
	mov	x27, x0
	mov	w19, w20
	adrp	x28, .LANCHOR0
	add	x22, x22, :lo12:.LC186
	add	x23, x23, :lo12:.LC187
	add	x24, x24, 704
.L2585:
	add	x3, x28, :lo12:.LANCHOR0
	ldrh	w1, [x3,3300]
	cmp	w1, w19
	bls	.L2597
	ldrh	w21, [x3,3350]
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	madd	w21, w25, w21, w19
	ldrb	w3, [x3,2832]
	mov	w0, w21
	bl	ftl_read_ppa_page
	mov	w26, w0
	ldr	x7, [x27,8]
	mov	w1, w25
	ldr	x2, [x27,24]
	mov	w3, w21
	mov	w4, w26
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
	mov	x0, x23
	mov	x1, x24
	mov	w2, 4
	mov	w3, 32
	bl	rknand_print_hex
	cmp	w26, 512
	beq	.L2590
	cmn	w26, #1
	bne	.L2586
.L2590:
	mov	w20, 1
.L2586:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2585
.L2597:
	mov	x0, x27
	bl	buf_free
	cbz	w20, .L2589
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1338
	add	x1, x1, 552
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2589:
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
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LC188
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC188
	ldrb	w1, [x19,2834]
	bl	printk
	adrp	x3, .LANCHOR4
	adrp	x0, .LC189
	add	x3, x3, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC189
	ldrb	w1, [x3,1536]
	ldrb	w2, [x3,1537]
	ldrh	w3, [x3,1538]
	bl	printk
	ldr	x2, [x19,2864]
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC192
	add	x0, x0, :lo12:.LC192
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x19,3300]
	mov	w2, 4
	ldrb	w3, [x19,3276]
	ldr	x1, [x19,3872]
	mul	w3, w0, w3
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,600]
	adrp	x0, .LC195
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC195
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,2864]
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,584]
	adrp	x0, .LC196
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC196
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC197
	add	x1, x19, 3360
	add	x0, x0, :lo12:.LC197
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3300]
	mov	w2, 2
	ldrb	w3, [x19,3276]
	ldr	x1, [x19,3880]
	mul	w3, w0, w3
	adrp	x0, .LC198
	add	x0, x0, :lo12:.LC198
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_ftl_info, .-dump_ftl_info
	.align	2
	.global	pm_ppa_update_check
	.type	pm_ppa_update_check, %function
pm_ppa_update_check:
	adrp	x5, .LANCHOR0
	mov	w6, 21
	add	x5, x5, :lo12:.LANCHOR0
	mov	w7, 1
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w4, [x5,3340]
	ldrb	w3, [x5,3338]
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
	cbnz	w4, .L2603
	cmp	w3, w7
	bne	.L2600
.L2603:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L2600:
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w22, w1
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	cmp	w22, 31
	uxth	w20, w0
	bls	.L2606
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 31
	add	x1, x1, 568
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2606:
	adrp	x21, .LANCHOR0
	adrp	x19, .LANCHOR7
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21,2864]
	ldrh	w2, [x0,698]
	cmp	w20, w2
	bls	.L2607
	adrp	x0, .LC200
	mov	w1, w20
	add	x0, x0, :lo12:.LC200
	add	x19, x19, :lo12:.LANCHOR7
	bl	printk
	mov	x0, 0
	ldrh	w2, [x19,-118]
	mov	w1, 255
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x21,2864]
	ldrh	w0, [x0,698]
	cmp	w0, w20
	bcs	.L2609
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 568
	mov	w2, 35
	b	.L2613
.L2607:
	add	x0, x0, x20, sxtw 2
	sbfiz	x22, x22, 4, 32
	ldr	w4, [x0,704]
	add	x0, x21, 3904
	add	x23, x0, x22
	strh	w20, [x0,x22]
	strh	wzr, [x23,2]
	cbnz	w4, .L2610
	adrp	x0, .LC201
	mov	w1, w20
	mov	w2, w4
	add	x19, x19, :lo12:.LANCHOR7
	add	x0, x0, :lo12:.LC201
	bl	printk
	ldr	x0, [x23,8]
	mov	w1, 255
	ldrh	w2, [x19,-118]
	bl	ftl_memset
	b	.L2609
.L2610:
	add	x22, x19, :lo12:.LANCHOR7
	ldrb	w3, [x21,2832]
	ldr	x1, [x23,8]
	mov	w0, w4
	str	x4, [x29,72]
	ldr	x2, [x22,-128]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x22,-128]
	ldr	x4, [x29,72]
	ldr	w2, [x0]
	cmp	w2, w20
	beq	.L2611
	adrp	x0, .LC202
	mov	w1, w20
	add	x0, x0, :lo12:.LC202
	bl	printk
	ldr	x3, [x21,2864]
	adrp	x0, .LC203
	add	x0, x0, :lo12:.LC203
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x21,2832]
	adrp	x0, .LC204
	ldr	x1, [x23,8]
	add	x0, x0, :lo12:.LC204
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	adrp	x0, .LC205
	ldr	x1, [x22,-128]
	add	x0, x0, :lo12:.LC205
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
.L2611:
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x19,-128]
	ldr	w0, [x0]
	cmp	w0, w20
	beq	.L2609
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 568
	mov	w2, 59
.L2613:
	bl	printk
	bl	dump_stack
.L2609:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	pm_gc
	.type	pm_gc, %function
pm_gc:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	add	x20, x23, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x19, x19, :lo12:.LANCHOR4
	ldr	x0, [x20,2864]
	ldrh	w1, [x0,688]
	ldrh	w0, [x19,1164]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2616
	bl	pm_free_sblk
	ldr	x2, [x20,2864]
	ldrh	w1, [x19,1164]
	ldrh	w3, [x2,688]
	sub	w1, w1, #1
	cmp	w3, w1
	blt	.L2616
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2618
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 131
	add	x1, x1, 584
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	x1, [x20,2864]
	add	x0, x1, x0, uxth 1
	ldrh	w21, [x0,416]
.L2618:
	bl	pm_select_ram_region
	uxth	x20, w0
	add	x0, x23, :lo12:.LANCHOR0
	lsl	x1, x20, 4
	add	x0, x0, 3904
	mov	x22, x20
	add	x19, x0, x1
	ldrh	w0, [x0,x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2619
	ldr	x1, [x19,8]
	cbz	x1, .L2619
	ldrsh	w2, [x19,2]
	tbz	w2, #31, .L2619
	bl	pm_write_page
	ldrh	w0, [x19,2]
	and	w0, w0, 32767
	strh	w0, [x19,2]
.L2619:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w19, 0
	add	x0, x23, 3904
	mov	w24, 21
	mov	w25, 1
	add	x20, x0, x20, lsl 4
.L2620:
	ldr	x0, [x23,2864]
	ldrh	w1, [x0,698]
	cmp	w1, w19
	bls	.L2626
	ldrh	w1, [x23,3340]
	add	x0, x0, x19, sxtw 2
	ldr	w0, [x0,704]
	lsr	w0, w0, w1
	sub	w1, w24, w1
	lsl	w1, w25, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x23,3338]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L2621
	mov	w1, w22
	mov	w0, w19
	bl	load_l2p_region
	ldr	x1, [x20,8]
	mov	w0, w19
	bl	pm_write_page
	mov	w0, -1
	strh	w0, [x20]
.L2621:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2620
.L2626:
	bl	pm_free_sblk
.L2616:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x1, x19, 3904
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,3896]
	cbz	w0, .L2628
	bl	pm_gc
	str	wzr, [x19,3896]
.L2628:
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
	adrp	x20, .LANCHOR0
	mov	x19, 0
	add	x20, x20, :lo12:.LANCHOR0
	add	x20, x20, 3904
.L2634:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2633
	bl	pm_flush_id
.L2633:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2634
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR7
	stp	x19, x20, [sp,16]
	add	x22, x21, :lo12:.LANCHOR7
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w23, w0
	mov	w0, 1
	strb	w0, [x22,-120]
	mov	w0, 64
	str	wzr, [x20,3896]
	add	x20, x20, 3904
	bl	ftl_malloc
	mov	w24, -1
	str	x0, [x22,-128]
	add	x22, x20, 512
.L2640:
	strh	w24, [x20]
	strh	wzr, [x20,2]
	cbz	w23, .L2639
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x20,8]
.L2639:
	add	x20, x20, 16
	cmp	x20, x22
	bne	.L2640
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR7
	mov	w4, 4
	ldr	x1, [x20,2864]
	ldr	x25, [x21,-128]
	ldr	x23, [x20,3912]
	ldrb	w0, [x1,694]
	mov	x3, x25
	ldrh	w1, [x1,692]
	mov	x2, x23
	bl	flash_get_last_written_page
	sxth	w21, w0
	ldr	x1, [x20,2864]
	mov	w24, w21
	ldrh	w2, [x1,696]
	cmp	w2, w21
	bgt	.L2641
	adrp	x0, .LC206
	ldrh	w1, [x1,692]
	mov	w3, w21
	add	x0, x0, :lo12:.LC206
	adrp	x26, .LC207
	add	w21, w21, 1
	bl	printk
	add	x26, x26, :lo12:.LC207
	ldr	x0, [x20,2864]
	ldrsh	w20, [x0,696]
.L2642:
	cmp	w20, w21
	bge	.L2655
	add	x27, x19, :lo12:.LANCHOR0
	mov	x2, x23
	mov	x3, x25
	ldr	x0, [x27,2864]
	ldrh	w22, [x27,3350]
	ldrb	w4, [x27,2832]
	ldrh	w1, [x0,692]
	ldrb	w0, [x0,694]
	madd	w22, w1, w22, w20
	mov	w1, w22
	bl	flash_read_page_en
	mov	w28, w0
	ldr	x3, [x27,2864]
	mov	x0, x26
	mov	w2, w22
	ldr	w1, [x3,48]
	add	w1, w1, 1
	str	w1, [x3,48]
	ldrh	w3, [x3,694]
	ldr	w1, [x25]
	bl	printk
	cmp	w28, 512
	beq	.L2643
	cmn	w28, #1
	beq	.L2643
	ldr	x1, [x27,2864]
	ldr	w0, [x25]
	ldrh	w2, [x1,698]
	cmp	w0, w2
	bcs	.L2643
	add	x0, x1, x0, uxtw 2
	str	w22, [x0,704]
.L2643:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2642
.L2655:
	adrp	x0, .LANCHOR5+577
	mov	w1, 1
	add	w24, w24, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+577]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	strh	w24, [x0,696]
	bl	pm_free_sblk
.L2641:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,3912]
	bl	pm_write_page
	ldr	x1, [x19,3912]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3912]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3912]
	mov	w0, -1
	bl	pm_write_page
	bl	pm_free_sblk
	bl	pm_gc
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_init, .-pm_init
	.align	2
	.global	pm_log2phys
	.type	pm_log2phys, %function
pm_log2phys:
	stp	x29, x30, [sp, -96]!
	mov	w5, w0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x25, [sp,64]
	add	x3, x21, :lo12:.LANCHOR0
	mov	x22, x1
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	w23, w2
	mov	x19, 0
	ldrb	w4, [x3,2832]
	ldr	w2, [x3,2840]
	add	x3, x3, 3904
	lsl	w20, w4, 7
	lsl	w24, w4, 7
	cmp	w0, w2
	udiv	w20, w0, w20
	uxth	w25, w20
	msub	w24, w25, w24, w0
	uxth	x24, w24
	bcc	.L2669
	adrp	x0, .LC208
	mov	w1, w5
	add	x0, x0, :lo12:.LC208
	bl	printk
	mov	w0, -1
	cbnz	w23, .L2658
	str	w0, [x22]
	b	.L2658
.L2673:
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L2660
.L2659:
	add	w19, w19, 1
	add	x3, x3, 16
	uxth	x19, w19
	cmp	w19, 32
	beq	.L2672
.L2669:
	ldr	x0, [x3,8]
	cbnz	x0, .L2673
	b	.L2659
.L2672:
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR0
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 3904
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2674
.L2664:
	adrp	x0, .LANCHOR7-116
	strb	w19, [x0,#:lo12:.LANCHOR7-116]
	mov	w0, w25
	bl	load_l2p_region
.L2660:
	cbnz	w23, .L2661
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,3912]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x22]
	b	.L2662
.L2661:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x22]
	add	x0, x0, 3904
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w2, [x1,x24,lsl 2]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
	adrp	x0, .LANCHOR4+64
	strb	w20, [x0,#:lo12:.LANCHOR4+64]
.L2662:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 32767
	add	x21, x21, 3904
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2658
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2658
.L2674:
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2664
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
	b	.L2664
.L2658:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	sub	sp, sp, #192
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp,80]
	add	x24, x20, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR4
	stp	x21, x22, [sp,64]
	stp	x25, x26, [sp,96]
	stp	x27, x28, [sp,112]
	add	x0, x23, :lo12:.LANCHOR4
	strb	wzr, [x24,3273]
	ldr	x21, [x24,608]
	strb	wzr, [x0,1130]
	bl	gc_init
	ldrh	w1, [x21,80]
	mov	w0, 65535
	str	x23, [x29,136]
	add	x22, x21, 80
	cmp	w1, w0
	ldr	x0, [x24,608]
	beq	.L2676
	mov	w1, -1
	strh	w1, [x0,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldrb	w0, [x22,9]
	ldrh	w1, [x24,3350]
	sub	w0, w0, #1
	add	x0, x22, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x24,3257]
	str	w0, [x29,152]
	cmp	w1, 3
	bne	.L2677
	ldrh	w1, [x24,3300]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
	b	.L2720
.L2677:
	cmp	w1, 2
	bne	.L2678
	ldrh	w1, [x24,3300]
	add	w0, w0, w1, lsl 1
	sub	w0, w0, #1
	orr	w0, w0, 33554432
.L2720:
	str	w0, [x19,40]
.L2678:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x19,52]
	cmp	w0, 512
	beq	.L2679
	cmn	w0, #1
	beq	.L2679
	ldr	x0, [x19,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2680
.L2679:
	mov	x0, x19
	bl	buf_free
	ldr	x7, [x19,24]
	ldr	x6, [x19,8]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x19,40]
	ldr	w2, [x19,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2723
.L2680:
	add	x24, x20, :lo12:.LANCHOR0
	ldr	x0, [x29,136]
	ldr	x1, [x19,8]
	add	x25, x0, :lo12:.LANCHOR4
	ldrb	w2, [x24,3276]
	ldrh	w0, [x24,3274]
	mul	w2, w0, w2
	ldr	x0, [x25,1136]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x24,3274]
	ldrb	w2, [x24,3276]
	ldr	x1, [x19,8]
	mul	w2, w0, w2
	add	x1, x1, x2, sxtw 2
	ldr	x0, [x25,1144]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w26, [x24,3257]
	ldrh	w25, [x24,3300]
	cmp	w26, 2
	bne	.L2682
	lsl	w25, w25, 1
	mov	w26, 1
.L2682:
	mov	w23, 0
	adrp	x24, .LC210
	add	x0, x24, :lo12:.LC210
	mov	w27, w23
	str	x0, [x29,112]
	b	.L2683
.L2725:
	ldr	x0, [x0,1144]
	ldr	w1, [x0,x12]
	ldr	w0, [x7,8]
	cmp	w1, w0
	bne	.L2687
.L2710:
	add	w27, w27, 1
	add	w28, w28, 1
.L2691:
	cmp	w28, w26
	bhi	.L2724
	ldr	x0, [x29,128]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w2, [x1,3350]
	ldrb	w1, [x1,3257]
	ldrh	w0, [x22,x0,lsl 1]
	cmp	w1, 3
	mul	w0, w0, w2
	str	w0, [x29,152]
	bne	.L2684
	add	w0, w0, w23
	orr	w0, w0, w28, lsl 24
.L2721:
	str	w0, [x19,40]
	mov	w1, 1
	mov	x0, x19
	bl	sblk_read_page
	ldr	x0, [x29,136]
	sbfiz	x12, x27, 2, 32
	ldr	x7, [x19,24]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w6, [x7,4]
	ldr	x1, [x0,1136]
	ldr	w3, [x1,x12]
	cmp	w3, w6
	beq	.L2725
.L2687:
	ldr	x0, [x29,136]
	str	x12, [x29,104]
	add	x13, x0, :lo12:.LANCHOR4
	ldr	w0, [x7,12]
	str	x13, [x29,96]
	ldr	x4, [x13,1144]
	str	w0, [sp]
	ldr	x0, [x29,112]
	ldr	w4, [x4,x12]
	ldr	w5, [x7]
	ldr	w1, [x19,40]
	ldr	w2, [x19,52]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x13, [x29,96]
	ldr	x12, [x29,104]
	ldr	x0, [x13,1136]
	ldr	w0, [x0,x12]
	cmn	w0, #1
	beq	.L2710
	mov	x0, x19
	bl	buf_free
.L2723:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x21,80]
	mov	w28, 0
	ldr	x1, [x0,600]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,608]
	ldrh	w1, [x21,80]
	strh	w1, [x0,130]
	b	.L2681
.L2690:
	add	w23, w23, 1
.L2683:
	cmp	w23, w25
	beq	.L2692
	sub	w0, w23, #1
	mov	w24, 0
	str	w0, [x29,124]
.L2693:
	ldrb	w0, [x22,9]
	cmp	w24, w0
	bge	.L2690
	sxtw	x0, w24
	mov	w28, 1
	str	x0, [x29,128]
	b	.L2691
.L2684:
	cmp	w1, 2
	bne	.L2686
	ldr	w1, [x29,124]
	add	w0, w0, w1
	add	w0, w0, w28
	orr	w0, w0, 33554432
	b	.L2721
.L2686:
	add	w0, w0, w23
	b	.L2721
.L2724:
	add	w24, w24, 1
	b	.L2693
.L2692:
	mov	x0, x19
	mov	w28, 0
	mov	w27, w28
	bl	buf_free
	str	w28, [x29,128]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x22,9]
	ldrh	w3, [x21,80]
	ldr	x2, [x0,600]
	ldrh	w0, [x0,3274]
	mul	w0, w1, w0
	strh	w0, [x2,x3,lsl 1]
.L2694:
	cmp	w27, w25
	beq	.L2705
	sub	w0, w27, #1
	mov	w24, 0
	str	w0, [x29,104]
.L2706:
	ldrb	w0, [x22,9]
	cmp	w24, w0
	bge	.L2703
	sxtw	x0, w24
	mov	w23, 1
	str	x0, [x29,112]
.L2704:
	cmp	w23, w26
	bhi	.L2726
	ldr	x1, [x29,136]
	ldr	w0, [x29,128]
	add	x1, x1, :lo12:.LANCHOR4
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1136]
	ldr	w6, [x2,x0]
	cmn	w6, #1
	beq	.L2695
	ldr	x1, [x1,1144]
	str	x6, [x29,96]
	ldr	w0, [x1,x0]
	str	w0, [x29,124]
	mov	w0, w6
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x6, [x29,96]
	bne	.L2696
	mov	w0, w6
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x6, [x29,96]
.L2696:
	ldr	x0, [x29,112]
	add	x0, x0, 8
	ldrh	w1, [x22,x0,lsl 1]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3350]
	ldrb	w0, [x0,3257]
	cmp	w0, 3
	mul	w1, w1, w2
	bne	.L2697
	add	w1, w1, w27
	orr	w1, w1, w23, lsl 24
	b	.L2722
.L2697:
	cmp	w0, 2
	bne	.L2699
	ldr	w0, [x29,104]
	add	w1, w1, w0
	add	w1, w1, w23
	orr	w1, w1, 33554432
	b	.L2722
.L2699:
	add	w1, w1, w27
.L2722:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	w2, [x29,124]
	str	w1, [x29,152]
	ldrh	w1, [x0,3340]
	lsr	w19, w2, w1
	mov	w2, 21
	sub	w1, w2, w1
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w1, w19, w1
	ldrb	w19, [x0,3338]
	ldr	w0, [x29,156]
	udiv	w19, w1, w19
	ldr	w1, [x29,124]
	cmp	w0, w1
	uxth	x19, w19
	bne	.L2700
	mov	w0, w6
	add	x1, x29, 152
	add	w28, w28, 1
	bl	pm_log2phys
	mov	w0, w19
	bl	ftl_vpn_decrement
	b	.L2701
.L2700:
	ldr	w1, [x29,152]
	cmp	w0, w1
	csinc	w28, w28, w28, ne
.L2701:
	ldr	w0, [x29,128]
	add	x1, x20, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,128]
	ldr	x0, [x1,584]
	add	x0, x0, x19, lsl 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 224
	bne	.L2695
	lsl	x19, x19, 1
	ldr	x1, [x1,600]
	ldrh	w2, [x1,x19]
	cbz	w2, .L2695
	strh	w0, [x1,x19]
.L2695:
	add	w23, w23, 1
	b	.L2704
.L2726:
	add	w24, w24, 1
	b	.L2706
.L2703:
	add	w27, w27, 1
	b	.L2694
.L2705:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x21,80]
	ldr	x0, [x0,600]
	strh	w28, [x0,x1,lsl 1]
	ldrh	w0, [x21,80]
	bl	zftl_insert_data_list
.L2681:
	ldrh	w1, [x21,80]
	adrp	x0, .LC211
	mov	w2, w28
	add	x0, x0, :lo12:.LC211
	bl	printk
	mov	w0, -1
	strh	w0, [x21,80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 65535
	ldr	x0, [x0,608]
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2707
	bl	ftl_free_sblk
.L2707:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x20,2864]
	strh	w0, [x1,126]
	ldr	x1, [x20,608]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2675
.L2676:
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2675
	ldr	x1, [x24,2864]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L2709
	bl	pm_flush
	ldr	x0, [x24,608]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x24,2864]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L2709:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x20,608]
	strh	w1, [x0,130]
.L2675:
	sub	sp, x29, #32
	ldp	x19, x20, [sp,48]
	ldp	x21, x22, [sp,64]
	ldp	x23, x24, [sp,80]
	ldp	x25, x26, [sp,96]
	ldp	x27, x28, [sp,112]
	ldp	x29, x30, [sp,32]
	add	sp, sp, 192
	ret
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	zftl_init
	.type	zftl_init, %function
zftl_init:
	stp	x29, x30, [sp, -64]!
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR0
	adrp	x22, .LANCHOR4
	add	x20, x19, :lo12:.LANCHOR0
	add	x1, x22, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR7
	stp	x23, x24, [sp,48]
	add	x23, x21, :lo12:.LANCHOR7
	strb	w0, [x20,2888]
	strb	w0, [x20,3893]
	strb	w0, [x1,1266]
	mov	w0, -1
	adrp	x1, .LC2
	str	w0, [x23,-112]
	adrp	x0, .LC1
	add	x1, x1, :lo12:.LC2
	add	x0, x0, :lo12:.LC1
	strb	wzr, [x20,2856]
	strb	wzr, [x20,3894]
	strb	wzr, [x20,2857]
	bl	printk
	adrp	x11, .LANCHOR2
	ldrb	w6, [x20,633]
	add	x1, x11, :lo12:.LANCHOR2
	strb	w6, [x20,3352]
	ldrh	w4, [x20,2]
	strh	w4, [x20,3350]
	ldrb	w2, [x1,21]
	ldrb	w8, [x1,20]
	ldrh	w3, [x1,18]
	mul	w6, w2, w6
	ldrb	w0, [x1,17]
	ldrh	w7, [x1,22]
	mov	w1, 0
	sdiv	w9, w3, w8
	strb	w2, [x20,3338]
	strb	w8, [x20,3257]
	uxtb	w6, w6
	strb	w0, [x20,2832]
	ubfiz	w10, w0, 9, 7
	strh	w3, [x20,3274]
	mov	w2, 1
	strh	w9, [x20,3300]
	strb	w6, [x20,3276]
	strh	w7, [x20,576]
	mov	x20, x11
	strh	w10, [x23,-118]
.L2728:
	cmp	w2, w4
	bhi	.L2794
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L2728
.L2794:
	mul	w3, w0, w3
	add	x2, x19, :lo12:.LANCHOR0
	sub	w1, w1, #1
	mul	w5, w3, w7
	strh	w1, [x2,3340]
	mov	w1, 1
	lsr	w4, w5, 21
	mov	w2, 0
.L2730:
	cmp	w1, w4
	bhi	.L2795
	add	w2, w2, 1
	lsl	w1, w1, 1
	uxth	w2, w2
	b	.L2730
.L2795:
	mov	w1, 57344
	mov	w4, 1
	movk	w1, 0x1c, lsl 16
	sub	w2, w2, #1
	lsl	w2, w4, w2
	add	x4, x19, :lo12:.LANCHOR0
	mul	w1, w6, w1
	mul	w5, w5, w6
	str	w5, [x4,2836]
	add	x11, x21, :lo12:.LANCHOR7
	mul	w1, w1, w2
	mul	w9, w9, w6
	add	w5, w1, 24576
	str	w5, [x11,-108]
	mul	w3, w3, w6
	udiv	w2, w5, w0
	sub	w0, w10, #1
	str	w2, [x4,2840]
	sub	w3, w3, #1
	str	w1, [x4,520]
	add	w0, w0, w2, lsl 2
	add	x2, x22, :lo12:.LANCHOR4
	udiv	w5, w5, w3
	add	w5, w5, 8
	strh	w5, [x4,3308]
	udiv	w0, w0, w10
	strh	w0, [x11,-104]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w9
	strh	w0, [x2,1164]
	add	w0, w7, w7, lsl 1
	asr	w0, w0, 2
	strh	w0, [x4,3344]
	lsr	w0, w7, 4
	strh	w0, [x4,3306]
	cmp	w0, 79
	bhi	.L2732
	mov	w0, 80
	strh	w0, [x4,3306]
.L2732:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 2000
	mov	w3, 256
	cmp	w8, 2
	strh	w2, [x0,3346]
	mov	w2, 50
	strh	w2, [x0,3342]
	add	x2, x22, :lo12:.LANCHOR4
	strh	w3, [x2,1160]
	mov	w3, 48
	strh	w3, [x2,1162]
	mov	w2, 32
	strh	w2, [x0,592]
	beq	.L2733
	ldrb	w0, [x0,3272]
	cbz	w0, .L2734
.L2733:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 150
	mov	w3, 64
	strh	w2, [x0,3342]
	add	x2, x22, :lo12:.LANCHOR4
	strh	w3, [x2,1162]
	mov	w3, 12
	strh	w3, [x0,592]
	ldrb	w3, [x19,#:lo12:.LANCHOR0]
	cbnz	w3, .L2734
	mov	w3, 4
	strh	w3, [x0,592]
	mov	w3, 600
	strh	w3, [x0,3346]
	mov	w0, 128
	strh	w0, [x2,1160]
.L2734:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2736
	adrp	x0, .LC212
	add	x0, x0, :lo12:.LC212
	bl	printk
.L2736:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2737
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC213
	add	x0, x0, :lo12:.LC213
	ldr	w1, [x1,2836]
	bl	printk
.L2737:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2738
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	ldr	w1, [x1,2840]
	bl	printk
.L2738:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2739
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	ldr	w1, [x1,-108]
	bl	printk
.L2739:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2740
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	ldrh	w1, [x1,-104]
	bl	printk
.L2740:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2741
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	ldrh	w1, [x1,-118]
	bl	printk
.L2741:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2742
	add	x22, x22, :lo12:.LANCHOR4
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	ldrh	w1, [x22,1164]
	bl	printk
.L2742:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2743
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC219
	add	x0, x0, :lo12:.LC219
	ldrh	w1, [x1,3306]
	bl	printk
.L2743:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2744
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC220
	add	x0, x0, :lo12:.LC220
	ldrh	w1, [x1,3308]
	bl	printk
.L2744:
	add	x22, x19, :lo12:.LANCHOR0
	bl	buf_init
	mov	w0, 6
	adrp	x23, .LANCHOR5
	ldrh	w1, [x22,576]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x22,528]
	ldrb	w1, [x22,3276]
	ldrh	w0, [x22,3300]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22,3880]
	ldrb	w1, [x22,3276]
	add	x2, x23, :lo12:.LANCHOR5
	ldrh	w0, [x22,3300]
	add	x2, x2, 640
	str	x2, [x22,3872]
	mul	w1, w0, w1
	ldrh	w0, [x22,576]
	lsl	w1, w1, 1
	add	x3, x2, x1, sxtw 2
	str	x3, [x22,600]
	add	w1, w1, w0, lsr 1
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	add	x1, x2, x1, sxtw 2
	str	x1, [x22,608]
	tbz	x0, 12, .L2745
	adrp	x0, .LC221
	add	x0, x0, :lo12:.LC221
	bl	printk
.L2745:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR7
	ldrh	w0, [x2,3300]
	ldrb	w22, [x2,3276]
	ldrh	w1, [x2,576]
	ldrh	w24, [x3,-104]
	mul	w22, w0, w22
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	lsl	w24, w24, 2
	add	w24, w24, w1, lsl 2
	add	w22, w1, w22, lsl 2
	add	w24, w24, 704
	lsl	w22, w22, 1
	add	w22, w22, 632
	tbz	x0, 12, .L2746
	adrp	x0, .LC222
	ldrh	w3, [x3,-118]
	add	x0, x0, :lo12:.LC222
	mov	w1, w22
	mov	w2, w24
	bl	printk
.L2746:
	add	x21, x21, :lo12:.LANCHOR7
	ldrh	w0, [x21,-118]
	cmp	w22, w0
	bhi	.L2748
	cmp	w24, w0
	bls	.L2747
.L2748:
.L2749:
	b	.L2749
.L2747:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L2750
	bl	ftl_ext_info_init
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR5
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
	ldr	x1, [x19,2864]
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
	ldrb	w0, [x23,577]
	cbz	w0, .L2751
	ldr	x1, [x19,2864]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L2751:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L2750:
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
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	ldr	x0, [x0,#:lo12:jiffies]
	add	x19, x20, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR7
	add	x21, x21, :lo12:.LANCHOR7
	str	x0, [x19,1168]
	mov	w0, 136
	strb	wzr, [x21,-102]
	bl	ftl_malloc
	str	x0, [x19,1176]
	cbnz	x0, .L2797
.L2799:
	mov	w0, -1
	b	.L2798
.L2797:
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x21,-96]
	mov	x1, x21
	add	x0, x19, 1184
	str	xzr, [x19,1184]
	str	xzr, [x1,-88]!
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1184]
	cbz	x0, .L2799
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x21,-96]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1176]
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
	ldr	x0, [x19,1184]
	bl	nand_flash_init
	mov	w22, w0
	cbnz	w0, .L2800
	bl	zftl_init
	mov	w19, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x21,-102]
	b	.L2801
.L2800:
	ldr	x1, [x19,1176]
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
	ldr	x0, [x19,1184]
	add	x2, x2, :lo12:ftl_discard
	str	x2, [x1,56]
	bl	FlashInit
	mov	w19, w0
	cmn	w22, #2
	adrp	x22, .LANCHOR2
	bne	.L2802
	add	x21, x22, :lo12:.LANCHOR2
	adrp	x1, gNandParaInfo
	add	x21, x21, 8
	add	x1, x1, :lo12:gNandParaInfo
	mov	w2, 32
	mov	x0, x21
	bl	ftl_memcpy
	ldrb	w0, [x21,18]
	bl	flash_lsb_page_tbl_build
	ldrh	w0, [x21,10]
	strh	w0, [x21,26]
.L2802:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x1, .LANCHOR0
	bne	.L2803
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L2804
.L2803:
	add	x0, x20, :lo12:.LANCHOR4
	strb	wzr, [x1,#:lo12:.LANCHOR0]
	strb	wzr, [x0,1264]
.L2804:
	adrp	x2, gNandFlashIDBEccBits
	add	x0, x1, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	ldrb	w2, [x2,#:lo12:gNandFlashIDBEccBits]
	strb	w2, [x0,632]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x22,18]
	str	xzr, [x20,1224]
	ldrb	w2, [x2,#:lo12:gNandFlashEccBits]
	strb	w2, [x0,724]
	mov	w2, 1
	mov	w0, 0
.L2805:
	cmp	w2, w3
	bhi	.L2810
	add	w0, w0, 1
	lsl	w2, w2, 1
	uxth	w0, w0
	b	.L2805
.L2810:
	add	x1, x1, :lo12:.LANCHOR0
	sub	w0, w0, #1
	strb	wzr, [x1,672]
	strh	w0, [x1,3340]
	cbnz	w19, .L2801
	bl	FtlInit
	mov	w19, w0
.L2801:
	adrp	x0, .LC223
	mov	w1, w19
	add	x0, x0, :lo12:.LC223
	bl	printk
	mov	w0, w19
.L2798:
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
	ldrh	w24, [x0,3274]
	add	x0, x20, 80
	str	x0, [x29,128]
	ldrb	w0, [x0,9]
	mul	w24, w24, w0
	bl	timer_get_time
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2812
	adrp	x0, .LC224
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC224
	bl	printk
.L2812:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	sub	w2, w24, #1
	mov	w21, 0
	mov	w23, w21
	adrp	x27, .LANCHOR4
	ldr	x0, [x0,600]
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	str	x0, [x29,120]
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	str	x0, [x29,112]
.L2813:
	cmp	w23, w24
	beq	.L2845
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1136]
	ldr	w2, [x0,w23,sxtw 2]
	cmn	w2, #1
	beq	.L2814
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,140]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2815
	ldr	x0, [x29,112]
	mov	w3, w23
	ldr	w1, [x29,140]
	bl	printk
.L2815:
	mov	w25, w23
.L2821:
	add	x1, x27, :lo12:.LANCHOR4
	sbfiz	x26, x25, 2, 32
	ldr	x0, [x1,1136]
	ldr	w4, [x0,x26]
	cmn	w4, #1
	beq	.L2816
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,140]
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w2, w0, uxth
	bne	.L2816
	ldr	x0, [x1,1144]
	str	x4, [x29,104]
	ldr	w28, [x0,x26]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x4, [x29,104]
	bne	.L2817
	mov	w0, w4
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x4, [x29,104]
.L2817:
	ldr	w3, [x29,156]
	cmp	w3, w28
	bne	.L2818
	add	x0, x27, :lo12:.LANCHOR4
	add	x1, x29, 152
	mov	w2, 1
	add	w21, w21, 1
	ldr	x0, [x0,1152]
	ldr	w0, [x0,x26]
	str	w0, [x29,152]
	mov	w0, w4
	bl	pm_log2phys
	add	x2, x19, :lo12:.LANCHOR0
	mov	w0, 21
	ldrh	w1, [x2,3340]
	sub	w3, w0, w1
	mov	w0, 1
	lsl	w0, w0, w3
	lsr	w28, w28, w1
	sub	w0, w0, #1
	and	w28, w0, w28
	ldrb	w0, [x2,3338]
	udiv	w0, w28, w0
	b	.L2844
.L2818:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2820
	ldr	x0, [x29,120]
	mov	w1, w4
	mov	w2, w28
	bl	printk
.L2820:
	ldr	x0, [x29,128]
	ldrh	w0, [x0]
.L2844:
	bl	ftl_vpn_decrement
	add	x0, x27, :lo12:.LANCHOR4
	mov	w1, -1
	ldr	x0, [x0,1136]
	str	w1, [x0,x26]
	bl	timer_get_time
.L2816:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L2821
.L2814:
	add	w23, w23, 1
	b	.L2813
.L2845:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2823
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,608]
	ldr	x2, [x0,600]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w21
	bl	printk
.L2823:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w21
	beq	.L2824
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 489
	add	x1, x1, 592
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2824:
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
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2896
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldrb	w0, [x1,4]
	add	x3, x1, x0, sxtw 1
	ldrb	w6, [x2,3276]
	ldrh	w22, [x3,40]
	mov	w3, 0
	ldrh	w2, [x1,2]
	mov	w4, w3
.L2847:
	cmp	w22, w5
	bne	.L2873
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w6
	bne	.L2848
	add	w2, w2, 1
	mov	w0, 0
	mov	w4, 1
	uxth	w2, w2
.L2848:
	add	x3, x1, x0, sxtw 1
	ldrh	w22, [x3,40]
	mov	w3, 1
	b	.L2847
.L2873:
	cbz	w4, .L2850
	add	x1, x19, :lo12:.LANCHOR0
	strh	w2, [x1,2898]
.L2850:
	cbz	w3, .L2851
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1,2900]
.L2851:
	mov	w0, 1
	mov	w23, 1
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x21, x0
.L2852:
	add	x20, x19, 2896
	ldrb	w1, [x20,6]
	cmp	w23, w1
	bgt	.L2874
	ldrh	w0, [x19,3350]
	cmp	w1, 3
	mul	w0, w22, w0
	bne	.L2853
	ldrh	w1, [x20,2]
	add	w0, w0, w1
	orr	w0, w0, w23, lsl 24
	b	.L2872
.L2853:
	cmp	w1, 2
	ldrh	w1, [x20,2]
	bne	.L2855
	add	w0, w0, w1, lsl 1
	ldrb	w1, [x19,3257]
	sub	w0, w0, #1
	add	w0, w0, w23
	orr	w0, w0, w1, lsl 24
	b	.L2872
.L2855:
	add	w0, w0, w1
.L2872:
	str	w0, [x21,40]
	mov	w1, 1
	mov	x0, x21
	ldr	w24, [x21,40]
	bl	sblk_read_page
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L2858
	cmn	w0, #1
	beq	.L2858
	ldr	x0, [x21,24]
	ldr	w20, [x0,4]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2859
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2859:
	ldr	w1, [x21,40]
	ldr	w0, [x29,76]
	cmp	w1, w0
	bne	.L2858
	ldrh	w2, [x19,2920]
	ldr	x1, [x19,3248]
	str	w24, [x1,x2,lsl 2]
	ldrh	w1, [x19,2920]
	add	w1, w1, 1
	strh	w1, [x19,2920]
.L2858:
	ldrh	w0, [x19,2922]
	add	w23, w23, 1
	add	w0, w0, 1
	strh	w0, [x19,2922]
	b	.L2852
.L2874:
	mov	x0, x21
	bl	buf_free
	ldrb	w0, [x20,4]
	ldrb	w1, [x19,3276]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x20,4]
	cmp	w1, w0
	bne	.L2846
	ldrh	w0, [x20,2]
	strb	wzr, [x20,4]
	add	w0, w0, 1
	strh	w0, [x20,2]
.L2846:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w1, [x0,2896]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2876
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 919
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2876:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,2896]
	cmp	w1, w3
	beq	.L2877
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2878
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,600]
	adrp	x0, .LC228
	add	x0, x0, :lo12:.LC228
	ldrh	w2, [x2,x3]
	bl	printk
.L2878:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2896
	add	x1, x20, 2936
	ldrh	w0, [x20,2896]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L2879
	mov	w0, -1
	strh	w0, [x20,2896]
	b	.L2905
.L2879:
	ldrh	w3, [x20,2896]
	ldr	x2, [x20,584]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L2880
	cmp	w4, 224
	beq	.L2880
	ldr	x2, [x20,608]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L2880
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L2880
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L2881
.L2880:
	cbnz	w4, .L2882
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L2883
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 939
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2883:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2896]
	ldr	x0, [x0,600]
	strh	wzr, [x0,x1,lsl 1]
.L2882:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2920]
	strh	w0, [x19,2896]
.L2905:
	mov	w0, 0
	b	.L2877
.L2881:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w20, [x0,40]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2884
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 947
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2884:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w25, 1
	ldrh	w3, [x2,2896]
	ldr	x4, [x2,584]
	ldrh	w0, [x2,3300]
	add	x3, x4, x3, lsl 2
	ldrh	w1, [x2,3350]
	sub	w0, w0, #1
	ldrb	w3, [x3,2]
	uxth	w0, w0
	mul	w1, w20, w1
	and	w3, w3, 224
	cmp	w3, 160
	bne	.L2885
	ldrb	w3, [x2,3257]
	cmp	w3, 2
	uxth	w25, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2885
	ldrh	w0, [x2,3274]
	sub	w0, w0, #1
	uxth	w0, w0
.L2885:
	add	x20, x19, :lo12:.LANCHOR0
	orr	w23, w0, w1
	mov	w0, 1
	strb	w25, [x20,2902]
	strh	wzr, [x20,2898]
	strb	wzr, [x20,2900]
	strh	wzr, [x20,2922]
	strh	wzr, [x20,2924]
	bl	buf_alloc
	mov	w1, 1
	str	w23, [x0,40]
	mov	x21, x0
	bl	sblk_read_page
	ldrh	w0, [x20,3274]
	mov	w1, 255
	ldrb	w2, [x20,3276]
	mul	w2, w0, w2
	ldr	x0, [x20,3248]
	lsl	w2, w2, 2
	bl	ftl_memset
	strh	wzr, [x20,2920]
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L2906
	cmn	w0, #1
	beq	.L2906
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2888
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 980
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2906:
	mov	x0, x21
	bl	buf_free
	mov	w0, -1
	b	.L2877
.L2888:
	ldrb	w0, [x20,3276]
	mov	x26, 0
	ldrh	w22, [x20,3300]
	mov	w24, 21
	ldr	x23, [x21,8]
	mul	w22, w0, w22
	mul	w22, w25, w22
	mov	w25, 1
	uxth	w22, w22
.L2889:
	cmp	w22, w26
	ble	.L2907
	ldr	w0, [x23,x26,lsl 2]
	cmn	w0, #1
	beq	.L2891
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L2892
	ldr	w0, [x23,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L2892:
	ldrh	w1, [x20,3340]
	ldr	w2, [x29,92]
	sub	w0, w24, w1
	ldrh	w3, [x20,2896]
	lsl	w0, w25, w0
	lsr	w1, w2, w1
	sub	w0, w0, #1
	and	w1, w0, w1
	ldrb	w0, [x20,3338]
	udiv	w1, w1, w0
	cmp	w1, w3
	bne	.L2891
	ldrh	w3, [x20,2920]
	ldr	x1, [x20,3248]
	str	w2, [x1,x3,lsl 2]
	ldrh	w1, [x20,2920]
	add	w1, w1, 1
	strh	w1, [x20,2920]
.L2891:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L2889
.L2907:
	mov	x0, x21
	bl	buf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2896]
	ldr	x3, [x0,600]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2920]
	cmp	w2, w3
	beq	.L2895
	adrp	x0, .LC229
	add	x0, x0, :lo12:.LC229
	bl	printk
.L2895:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2896]
	ldr	x1, [x19,600]
	ldrh	w3, [x19,2920]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2924]
	ldrh	w0, [x19,2920]
.L2877:
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
	beq	.L2909
	adrp	x21, .LANCHOR3
	adrp	x22, .LC0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 11
	add	x22, x22, :lo12:.LC0
	add	x21, x21, 632
.L2910:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,608]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2911
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L2912
	ldrh	w0, [x24,3340]
	mov	w2, 21
	mov	w1, 1
	ldr	w3, [x29,76]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	lsr	w3, w3, w0
	sub	w2, w2, #1
	ldrb	w0, [x24,3338]
	and	w2, w2, w3
	udiv	w0, w2, w0
	mov	w2, 0
	bl	gc_add_sblk
.L2912:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,608]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L2913
	mov	x1, x21
	mov	w2, 1419
	mov	x0, x22
	bl	printk
	bl	dump_stack
.L2913:
	mov	x0, x23
	bl	buf_free
.L2911:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,608]
	ldr	w2, [x2,2840]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L2914
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
	b	.L2908
.L2914:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L2908
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L2910
	b	.L2908
.L2909:
	ldr	w1, [x0,536]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L2919
	ldr	x3, [x3,2864]
	ldr	w1, [x0,540]
	ldr	w3, [x3,44]
	add	w1, w1, 98304
	add	w1, w1, 1696
	cmp	w3, w1
	bls	.L2908
.L2919:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w2, [x0,536]
	str	wzr, [x0,544]
.L2908:
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
	bcs	.L2924
	bl	timer_get_time
	ldr	x21, [x20,608]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L2924
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,608]
	ldrh	w2, [x20,576]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L2928
	ldr	x2, [x20,2864]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L2929
.L2928:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2864]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L2929:
	ldr	w25, [x0,600]
	mov	w0, 65535
	uxth	w20, w25
	cmp	w20, w0
	bne	.L2930
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1714
	add	x1, x1, 656
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2930:
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
	cbz	w1, .L2924
	uxtw	x22, w20
	ldr	x24, [x21,584]
	add	x24, x24, x22, lsl 2
	ldrb	w1, [x24,2]
	tst	w1, 192
	and	w2, w1, 224
	beq	.L2931
	cmp	w2, 224
	beq	.L2931
	ldr	x1, [x21,608]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L2931
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L2931
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L2932
.L2931:
	cbnz	w2, .L2924
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x22]
	cbz	w0, .L2933
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1730
	add	x1, x1, 656
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2933:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,600]
	strh	wzr, [x0,x22]
	b	.L2924
.L2932:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2935
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1736
	add	x1, x1, 656
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2935:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w3, [x24,2]
	mov	w26, 1
	and	w3, w3, 224
	ldrh	w0, [x2,3300]
	cmp	w3, 160
	ldrh	w1, [x2,3350]
	sub	w0, w0, #1
	mul	w1, w21, w1
	uxth	w0, w0
	bne	.L2936
	ldrb	w3, [x2,3257]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2936
	ldrh	w0, [x2,3274]
	sub	w0, w0, #1
	uxth	w0, w0
.L2936:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x19, :lo12:.LANCHOR0
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,3274]
	mov	w1, 255
	ldrb	w2, [x21,3276]
	mul	w2, w0, w2
	ldr	x0, [x21,3248]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2948
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2937
.L2948:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L2961
.L2937:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2939
	mov	w0, w20
	mov	w1, 1
.L2961:
	bl	gc_add_sblk
	mov	x0, x23
	bl	buf_free
	b	.L2924
.L2939:
	ldrb	w0, [x21,3276]
	mov	x27, 0
	ldrh	w1, [x21,3300]
	and	w28, w25, 65535
	mov	w21, w27
	mul	w0, w0, w1
	mul	w26, w26, w0
	uxth	w0, w26
	ldr	x26, [x23,8]
	str	w0, [x29,108]
.L2940:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L2962
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L2941
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2942
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2942:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 21
	ldrh	w0, [x4,3340]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x29,124]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3338]
	and	w0, w2, w0
	udiv	w0, w0, w1
	cmp	w0, w28
	bne	.L2941
	add	w21, w21, 1
	uxth	w21, w21
.L2941:
	add	x27, x27, 1
	b	.L2940
.L2962:
	mov	x0, x23
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2944
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w25
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,600]
	adrp	x0, .LC230
	add	x0, x0, :lo12:.LC230
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L2944:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,600]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L2924
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L2924:
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
	str	x0, [x29,136]
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
	adrp	x0, .LC231
	ldrb	w3, [x1,2]
	add	x0, x0, :lo12:.LC231
	mov	w1, w20
	and	w4, w4, 2047
	ubfx	x2, x3, 5, 3
	ubfx	x5, x5, 11, 8
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w1, 65535
	mov	w0, 0
	cmp	w20, w1
	beq	.L2964
	ldrh	w1, [x19,576]
	cmp	w1, w20
	bls	.L2964
	ldr	x0, [x19,584]
	mov	w26, 1
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2965
	ldrb	w26, [x19,3257]
.L2965:
	add	x1, x29, 192
	mov	w0, w20
	strh	w20, [x29,176]
	mov	w22, 0
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	add	x0, x23, :lo12:.LANCHOR0
	strb	w1, [x29,185]
	mov	w3, w26
	strh	wzr, [x29,178]
	uxtb	w4, w4
	ldrh	w2, [x0,3300]
	mov	w27, w22
	strb	wzr, [x29,181]
	mov	w19, w22
	strh	wzr, [x29,186]
	mul	w1, w1, w2
	strh	w1, [x29,182]
	ldr	x1, [x0,584]
	adrp	x0, .LC232
	add	x0, x0, :lo12:.LC232
	add	x1, x1, x25
	ldrb	w2, [x1,2]
	mov	w1, w20
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	adrp	x0, .LC186
	str	w22, [x29,152]
	add	x0, x0, :lo12:.LC186
	str	x0, [x29,120]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 680
	str	x0, [x29,112]
.L2966:
	add	x21, x23, :lo12:.LANCHOR0
	ldrh	w0, [x21,3300]
	cmp	w0, w19
	bls	.L2982
	lsl	w0, w19, 1
	str	wzr, [x29,156]
	sub	w0, w0, #1
	str	w0, [x29,132]
.L2983:
	ldrb	w0, [x29,185]
	ldr	w1, [x29,156]
	cmp	w0, w1
	bls	.L2980
	mov	w24, 1
.L2981:
	cmp	w24, w26
	ldr	w0, [x29,156]
	bhi	.L3000
	add	x1, x29, 176
	add	x0, x1, x0, sxtw 1
	ldrh	w2, [x0,16]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2967
	add	x1, x23, :lo12:.LANCHOR0
	cmp	w26, 3
	ldrh	w0, [x1,3350]
	mul	w0, w2, w0
	add	w21, w0, w19
	bne	.L2968
	orr	w21, w21, w24, lsl 24
	b	.L2969
.L2968:
	cmp	w26, 2
	bne	.L2969
	ldr	w3, [x29,132]
	ldrb	w21, [x1,3257]
	add	w0, w0, w3
	add	w0, w0, w24
	orr	w21, w0, w21, lsl 24
.L2969:
	mov	w1, 1
	str	w21, [x28,40]
	mov	x0, x28
	str	x2, [x29,104]
	bl	sblk_read_page
	ldr	x7, [x28,8]
	mov	w3, w21
	ldr	x0, [x28,24]
	ldr	w9, [x28,52]
	ldr	w1, [x7,12]
	str	w1, [sp]
	mov	w4, w9
	ldr	x2, [x29,104]
	ldr	w1, [x0]
	str	w1, [sp,8]
	str	x9, [x29,104]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w2
	mov	w2, w19
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	ldr	x0, [x29,120]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x9, [x29,104]
	cmp	w9, 512
	beq	.L2987
	cmn	w9, #1
	bne	.L2971
.L2987:
	mov	w0, 1
	str	w0, [x29,152]
.L2971:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,584]
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L2967
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L2967
	ldr	x0, [x28,24]
	str	x2, [x29,104]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x2, [x29,104]
	bne	.L2973
	ldr	x0, [x28,24]
	add	x1, x29, 172
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L2973:
	ldr	w0, [x29,172]
	cmp	w0, w21
	bne	.L2974
	ldr	x2, [x28,24]
	add	w22, w22, 1
	adrp	x0, .LC233
	mov	w1, w21
	add	x0, x0, :lo12:.LC233
	mov	w3, w22
	ldr	w2, [x2,4]
	bl	printk
.L2974:
	ldr	x0, [x29,144]
	cbz	x0, .L2976
	ubfiz	x21, x27, 2, 32
	ldr	w2, [x0,x21]
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L2977
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2977
	adrp	x0, .LC234
	mov	w1, w27
	add	x0, x0, :lo12:.LC234
	bl	printk
.L2977:
	ldr	x0, [x29,144]
	ldr	x1, [x28,24]
	ldr	w0, [x0,x21]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L2976
	cmn	w0, #1
	beq	.L2976
	adrp	x0, .LC0
	ldr	x1, [x29,112]
	mov	w2, 1299
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2976:
	add	w27, w27, 1
.L2967:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2981
.L3000:
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,156]
	b	.L2983
.L2980:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2966
.L2982:
	mov	x0, x28
	bl	buf_free
	ldr	x3, [x29,136]
	adrp	x0, .LC235
	ldr	x2, [x21,600]
	add	x0, x0, :lo12:.LC235
	mov	w1, w20
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
	ldr	w0, [x29,152]
.L2964:
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
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	adrp	x0, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w22, w1
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w25, w2
	mov	x23, x3
	tbz	x0, 12, .L3002
	adrp	x0, .LC236
	mov	w1, w19
	add	x0, x0, :lo12:.LC236
	mov	w2, w22
	mov	w3, w25
	bl	printk
.L3002:
	cbnz	w19, .L3003
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3004
.L3003:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3005
	lsl	w19, w19, 13
	mov	w2, 8192
.L3004:
	add	w1, w22, w25
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3005
	add	w22, w19, w22
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	add	w28, w22, w25
	str	wzr, [x29,140]
	ldr	x2, [x1,2864]
	ldr	w0, [x2,24]
	add	w0, w0, w25
	str	w0, [x2,24]
	ldrb	w0, [x1,2832]
	sub	w1, w28, #1
	udiv	w27, w22, w0
	mov	w20, w27
	udiv	w0, w1, w0
	str	w0, [x29,136]
	sub	w24, w0, w27
	add	w24, w24, 1
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,128]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 696
	str	x0, [x29,120]
.L3006:
	cbz	w24, .L3066
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,136]
	cmp	w20, w27
	cset	w2, eq
	cmp	w20, w1
	ldrb	w0, [x0,2832]
	cset	w26, eq
	uxth	w21, w0
	cbnz	w26, .L3036
	cbz	w2, .L3007
	b	.L3065
.L3036:
	cbz	w2, .L3009
.L3065:
	udiv	w26, w22, w0
	msub	w26, w26, w0, w22
	uxth	w0, w25
	uxth	w26, w26
	sub	w21, w21, w26
	uxth	w21, w21
	cmp	w21, w25
	csel	w21, w0, w21, hi
	b	.L3007
.L3009:
	msub	w21, w0, w20, w28
	mov	w26, w2
	uxtb	w21, w21
.L3007:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 784
.L3012:
	ldr	w2, [x1,36]
	cmp	w2, w20
	bne	.L3010
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L3010
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 6, 32
	add	x0, x2, x1
	ubfiz	x26, x26, 9, 16
	lsl	w2, w21, 9
	ubfiz	x21, x21, 9, 16
	ldr	x1, [x0,792]
	mov	x0, x23
	add	x23, x23, x21
	add	x1, x1, x26
	bl	ftl_memcpy
	b	.L3011
.L3010:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3012
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3013
	mov	w0, w20
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3013:
	ldr	w0, [x29,156]
	cmn	w0, #1
	bne	.L3014
	mov	w21, 0
.L3015:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w21, w0
	bcs	.L3011
	madd	w0, w20, w0, w21
	cmp	w0, w22
	bcc	.L3016
	cmp	w0, w28
	bcs	.L3016
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	add	x23, x23, 512
	bl	ftl_memset
.L3016:
	add	w21, w21, 1
	b	.L3015
.L3014:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3018
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x3, [x2,2864]
	ldr	w2, [x3,40]
	add	w2, w2, 1
	str	w2, [x3,40]
	ldr	w2, [x29,156]
	strb	w21, [x0,56]
	ubfiz	x21, x21, 9, 16
	str	x23, [x0,16]
	add	x23, x23, x21
	str	w2, [x0,40]
	str	w2, [x0,44]
	str	w20, [x0,36]
	strb	w26, [x0,57]
	bl	zftl_add_read_buf
.L3011:
	add	w20, w20, 1
	sub	w24, w24, #1
.L3018:
	cbz	w24, .L3037
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2834]
	cmp	w0, 2
	bhi	.L3006
.L3037:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w1, [x2,3894]
	cbz	w1, .L3006
	add	x0, x2, 784
	ldrb	w2, [x2,3893]
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
.L3021:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,3894]
	cbz	w1, .L3067
	ldrb	w0, [x0,3893]
	cmp	w0, 255
	bne	.L3022
	ldr	x1, [x29,120]
	mov	w2, 1065
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3022:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 784
	ldrb	w21, [x2,3893]
	lsl	x0, x21, 6
	add	x26, x1, x0
	ldrb	w0, [x1,x0]
	ldr	w8, [x26,52]
	strb	w0, [x2,3893]
	cmn	w8, #1
	bne	.L3023
	ldr	x1, [x2,608]
	str	w8, [x29,140]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L3024
.L3023:
	cmp	w8, 256
	bne	.L3024
	ldrh	w0, [x2,3340]
	mov	w1, 21
	mov	w5, 1
	ldr	w3, [x26,40]
	sub	w4, w1, w0
	str	x8, [x29,96]
	lsl	w4, w5, w4
	lsr	w0, w3, w0
	sub	w4, w4, #1
	str	x5, [x29,104]
	and	w0, w4, w0
	ldrb	w4, [x2,3338]
	ldr	w2, [x26,36]
	udiv	w4, w0, w4
	adrp	x0, .LC237
	add	x0, x0, :lo12:.LC237
	str	x4, [x29,112]
	uxth	w1, w4
	bl	printk
	ldr	x4, [x29,112]
	mov	w2, 0
	ldr	x5, [x29,104]
	mov	w0, w4
	mov	w1, w5
	bl	gc_add_sblk
	ldr	x8, [x29,96]
.L3024:
	add	x9, x19, :lo12:.LANCHOR0
	add	x1, x9, 784
	add	x1, x1, x21, lsl 6
	add	x10, x1, 32
	ldr	x0, [x1,24]
	ldr	w3, [x0,4]
	ldr	w0, [x1,36]
	cmp	w3, w0
	beq	.L3025
	ldr	x3, [x9,608]
	str	x8, [x29,96]
	str	x9, [x29,104]
	ldr	w0, [x3,552]
	str	x10, [x29,112]
	add	w0, w0, 1
	str	w0, [x3,552]
	adrp	x0, .LC238
	ldr	x7, [x1,24]
	add	x0, x0, :lo12:.LC238
	ldr	w2, [x10,4]
	ldr	w3, [x10,8]
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldrb	w1, [x1,1]
	ldr	w7, [x7,12]
	bl	printk
	ldr	x9, [x29,104]
	mov	w1, 21
	ldr	x10, [x29,112]
	ldrh	w0, [x9,3340]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x10,8]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x9,3338]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x8, [x29,96]
.L3025:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x21, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3038
	cmn	w8, #1
	bne	.L3026
.L3038:
	ldr	x1, [x29,120]
	mov	w2, 1086
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3026:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	add	x21, x0, x21, lsl 6
	ldrb	w1, [x1,2832]
	ldrb	w2, [x21,56]
	cmp	w1, w2
	bls	.L3028
	ldrb	w3, [x21,57]
	lsl	w2, w2, 9
	ldr	x1, [x21,8]
	ldr	x0, [x21,16]
	add	x1, x1, x3, lsl 9
	bl	ftl_memcpy
	b	.L3029
.L3028:
	ldrb	w0, [x21,2]
	and	w0, w0, -9
	strb	w0, [x21,2]
.L3029:
	add	x21, x19, :lo12:.LANCHOR0
	mov	x1, x26
	add	x0, x21, 3895
	bl	buf_remove_buf
	mov	x0, x26
	bl	buf_free
	ldrb	w0, [x21,3894]
	sub	w0, w0, #1
	strb	w0, [x21,3894]
	b	.L3021
.L3067:
	mov	w2, -1
	strb	w1, [x0,3894]
	strb	w2, [x0,3893]
	b	.L3006
.L3066:
	bl	timer_get_time
	adrp	x1, .LANCHOR7-80
	str	w0, [x1,#:lo12:.LANCHOR7-80]
	ldr	w0, [x29,140]
.L3005:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
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
	bhi	.L3071
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x22, x0
	mov	x0, -14
	cbnz	x22, .L3071
	adrp	x0, .LC239
	mov	x1, x19
	add	x0, x0, :lo12:.LC239
	strb	w22, [x19,x21]
	bl	printk
	adrp	x0, .LC240
	mov	x1, x19
	add	x0, x0, :lo12:.LC240
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC241
	mov	x0, x19
	add	x1, x1, :lo12:.LC241
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3072
	adrp	x23, .LANCHOR0
	adrp	x0, .LC242
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC242
	mov	w2, 4
	adrp	x20, .LANCHOR4
	ldr	x3, [x19,2864]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR4
	ldr	x1, [x19,2864]
	adrp	x0, .LC243
	mov	w2, 2
	add	x0, x0, :lo12:.LC243
	add	x1, x1, 416
	ldrh	w3, [x3,1164]
	mov	w19, w22
	mov	w22, 65535
	bl	rknand_print_hex
.L3073:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,1164]
	cmp	w19, w0
	bge	.L3076
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3074
	mov	x1, 0
	bl	ftl_sblk_dump
.L3074:
	add	w19, w19, 1
	b	.L3073
.L3072:
	adrp	x1, .LC244
	mov	x0, x19
	add	x1, x1, :lo12:.LC244
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3077
	adrp	x23, .LANCHOR0
	adrp	x0, .LC242
	add	x20, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC242
	mov	w2, 4
	add	x22, x29, 192
	adrp	x24, .LC247
	ldr	x3, [x20,2864]
	add	x24, x24, :lo12:.LC247
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	adrp	x3, .LANCHOR4+1164
	ldr	x1, [x20,2864]
	adrp	x0, .LC243
	mov	w2, 2
	add	x0, x0, :lo12:.LC243
	add	x1, x1, 416
	ldrh	w3, [x3,#:lo12:.LANCHOR4+1164]
	bl	rknand_print_hex
	adrp	x0, .LC245
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC245
	str	x1, [x22,-88]!
	add	x19, x20, 3904
	mov	x20, x23
	bl	printk
	adrp	x23, .LC246
	ldr	x0, [x29,104]
	mov	x1, x22
	add	x25, x19, 512
	add	x23, x23, :lo12:.LC246
	bl	rk_simple_strtoull.constprop.29
	mov	w22, w0
	str	w0, [x29,100]
	uxth	w26, w0
.L3079:
	ldrh	w1, [x19]
	mov	x0, x23
	ldrh	w2, [x19,2]
	bl	printk
	ldrh	w0, [x19]
	cmp	w0, w26
	bne	.L3078
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x19,8]
	mov	w2, 4
	ldrb	w3, [x0,2832]
	mov	x0, x24
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L3078:
	add	x19, x19, 16
	cmp	x19, x25
	bne	.L3079
	mov	w0, 300
	add	x20, x20, :lo12:.LANCHOR0
	bl	msleep
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldr	x1, [x20,2864]
	add	x1, x1, x22, uxth 2
	ldr	w1, [x1,704]
	str	w1, [x29,100]
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
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	ldr	w3, [x6]
	ldr	w2, [x19,52]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	ldrb	w3, [x20,2832]
	adrp	x0, .LC248
	add	x0, x0, :lo12:.LC248
	ldr	x1, [x19,8]
	mov	w2, 4
	lsl	w3, w3, 7
	b	.L3094
.L3077:
	adrp	x1, .LC249
	mov	x0, x19
	add	x1, x1, :lo12:.LC249
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3080
	bl	dump_ftl_info
	b	.L3076
.L3080:
	adrp	x1, .LC250
	mov	x0, x19
	add	x1, x1, :lo12:.LC250
	mov	x2, 9
	bl	memcmp
	cbnz	w0, .L3081
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1,-88]!
	bl	rk_simple_strtoull.constprop.29
	str	w0, [x29,100]
	adrp	x1, .LANCHOR0+3336
	strh	w0, [x1,#:lo12:.LANCHOR0+3336]
	bl	dump_all_list_info
	b	.L3076
.L3081:
	adrp	x1, .LC251
	mov	x0, x19
	add	x1, x1, :lo12:.LC251
	mov	x2, 8
	bl	memcmp
	cbz	w0, .L3076
	adrp	x1, .LC252
	mov	x0, x19
	add	x1, x1, :lo12:.LC252
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3083
	add	x20, x29, 192
	adrp	x0, .LC245
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC245
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	bl	rk_simple_strtoull.constprop.29
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
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x20,2832]
	adrp	x0, .LC204
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC204
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,2832]
	adrp	x0, .LC205
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC205
	mov	w2, 4
	lsl	w3, w3, 1
.L3094:
	bl	rknand_print_hex
	mov	x0, x19
	bl	buf_free
	b	.L3076
.L3083:
	adrp	x1, .LC253
	mov	x0, x19
	add	x1, x1, :lo12:.LC253
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3084
	add	x20, x29, 192
	adrp	x0, .LC245
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC245
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.29
	str	w0, [x29,100]
	mov	x1, x22
	bl	ftl_sblk_dump
	b	.L3076
.L3084:
	adrp	x1, .LC254
	mov	x0, x19
	add	x1, x1, :lo12:.LC254
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3085
	add	x20, x29, 192
	adrp	x0, .LC245
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC245
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.29
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3076
.L3085:
	adrp	x1, .LC255
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC255
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3086
	add	x1, x19, 8
	adrp	x0, .LC245
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC245
	str	x1, [x19,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.29
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29,100]
	cmn	w0, #1
	ldr	x2, [x29,88]
	bne	.L3087
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3087:
	adrp	x0, .LC256
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC256
	bl	printk
	b	.L3076
.L3086:
	adrp	x0, .LC257
	add	x0, x0, :lo12:.LC257
	bl	printk
	adrp	x0, .LC258
	add	x0, x0, :lo12:.LC258
	bl	printk
	adrp	x0, .LC259
	add	x0, x0, :lo12:.LC259
	bl	printk
	adrp	x0, .LC260
	add	x0, x0, :lo12:.LC260
	bl	printk
	adrp	x0, .LC261
	add	x0, x0, :lo12:.LC261
	bl	printk
	adrp	x0, .LC262
	add	x0, x0, :lo12:.LC262
	bl	printk
	adrp	x0, .LC263
	add	x0, x0, :lo12:.LC263
	bl	printk
	adrp	x0, .LC264
	add	x0, x0, :lo12:.LC264
	bl	printk
	adrp	x0, .LC265
	add	x0, x0, :lo12:.LC265
	bl	printk
.L3076:
	bl	rknand_device_unlock
	mov	x0, x21
.L3071:
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
	ldr	x0, [x20,2904]
	ldr	x23, [x20,608]
	add	x20, x20, 2896
	cbnz	x0, .L3096
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L3096:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,2904]
	cbnz	x20, .L3097
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 367
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3097:
	mov	w24, 0
	add	x25, x23, 80
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x27, .LANCHOR4
.L3098:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L3121
	mov	w22, 1
	sxtw	x28, w24
.L3107:
	add	x0, x21, 2896
	ldrh	w1, [x0,20]
	cmp	w22, w1
	bgt	.L3122
	add	x1, x28, 8
	ldrh	w2, [x21,3350]
	ldrh	w0, [x0,16]
	ldrh	w1, [x25,x1,lsl 1]
	mul	w1, w1, w2
	ldrb	w2, [x21,3257]
	cmp	w2, 3
	bne	.L3099
	add	w1, w1, w0
	orr	w1, w1, w22, lsl 24
	b	.L3120
.L3099:
	cmp	w2, 2
	bne	.L3101
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L3120
.L3101:
	add	w1, w1, w0
.L3120:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmn	w0, #1
	beq	.L3102
	add	x2, x27, :lo12:.LANCHOR4
	ldrh	w0, [x21,2918]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x3, [x2,1136]
	ldr	w4, [x3,x0]
	ldr	w3, [x1,4]
	cmp	w4, w3
	bne	.L3102
	ldr	x2, [x2,1144]
	ldr	w2, [x2,x0]
	ldr	w0, [x1,8]
	cmp	w2, w0
	beq	.L3103
.L3102:
	add	x26, x27, :lo12:.LANCHOR4
	ldrh	w0, [x21,2918]
	add	x4, x21, 2896
	ldr	x1, [x26,1136]
	ldr	w0, [x1,x0,lsl 2]
	cmn	w0, #1
	beq	.L3103
	ldrb	w5, [x25,9]
	adrp	x0, .LC266
	ldrh	w3, [x21,3274]
	add	x0, x0, :lo12:.LC266
	mov	w2, 4
	str	x4, [x29,104]
	mul	w3, w5, w3
	bl	rknand_print_hex
	ldrb	w5, [x25,9]
	adrp	x0, .LC267
	ldrh	w3, [x21,3274]
	add	x0, x0, :lo12:.LC267
	ldr	x1, [x26,1144]
	mov	w2, 4
	mul	w3, w5, w3
	bl	rknand_print_hex
	adrp	x0, .LANCHOR2
	ldr	x4, [x29,104]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L3104
	ldr	x7, [x20,24]
	ldrh	w4, [x4,22]
	ldr	x5, [x26,1144]
	ldr	w0, [x7,12]
	lsl	x4, x4, 2
	ldr	x3, [x26,1136]
	str	w0, [sp]
	adrp	x0, .LC268
	add	x0, x0, :lo12:.LC268
	ldr	w3, [x3,x4]
	ldr	w6, [x7,4]
	ldr	w4, [x5,x4]
	ldr	w1, [x20,40]
	ldr	w5, [x7]
	ldr	w2, [x20,52]
	ldr	w7, [x7,8]
	bl	printk
.L3104:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x1, 0
	ldr	x0, [x19,608]
	ldrh	w0, [x0,80]
	bl	ftl_sblk_dump
	ldr	x0, [x19,600]
	ldrh	w1, [x23,80]
	strh	wzr, [x0,x1,lsl 1]
	mov	w0, -1
	b	.L3105
.L3103:
	ldrh	w0, [x21,2918]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2918]
	b	.L3107
.L3122:
	add	w24, w24, 1
	b	.L3098
.L3121:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,2912]
	ldrb	w3, [x19,3348]
	add	w0, w1, 1
	strh	w0, [x19,2912]
	mov	w0, 0
	cbz	w3, .L3105
	add	w1, w1, 2
	strh	w1, [x19,2912]
.L3105:
	sub	sp, x29, #16
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
	ldrb	w1, [x23,9]
	ldrh	w24, [x0,3300]
	ldrh	w20, [x23,12]
	ldr	x0, [x0,3872]
	mul	w24, w24, w1
	add	x20, x0, x20, lsl 2
	add	x0, x20, x24, sxtw 2
	ldr	w0, [x0,-4]
	cmn	w0, #1
	beq	.L3124
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1575
	add	x1, x1, 736
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3124:
	adrp	x0, .LC270
	mov	x28, 0
	add	x0, x0, :lo12:.LC270
	adrp	x27, .LC269
	str	x0, [x29,120]
	adrp	x0, .LC271
	add	x0, x0, :lo12:.LC271
	mov	w22, w28
	str	x0, [x29,112]
	add	x27, x27, :lo12:.LC269
.L3125:
	cmp	w28, w24
	mov	w25, w28
	bge	.L3152
	ldr	w2, [x20,x28,lsl 2]
	cmn	w2, #1
	beq	.L3126
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2832]
	adrp	x0, .LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L3130
	mov	x0, x27
	mov	w1, w21
	mov	w3, w28
	bl	printk
.L3130:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L3128
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w1, [x3,2832]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L3128
	ldrb	w0, [x23,9]
	ldrh	w2, [x3,3350]
	str	x3, [x29,104]
	sdiv	w1, w25, w0
	msub	w0, w1, w0, w25
	add	x0, x23, x0, sxtw 1
	ldrh	w0, [x0,16]
	madd	w2, w0, w2, w1
	str	w2, [x29,140]
	ldr	x0, [x29,120]
	ldr	w1, [x20,x26]
	bl	pm_ppa_update_check
	ldr	x3, [x29,104]
	cbz	w0, .L3129
	ldr	x1, [x3,3872]
	mov	w2, 4
	ldr	x0, [x29,112]
	mov	w3, w24
	bl	rknand_print_hex
.L3129:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	mov	w0, -1
	str	w0, [x20,x26]
.L3128:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L3130
.L3126:
	add	x28, x28, 1
	b	.L3125
.L3152:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3132
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,600]
	adrp	x0, .LC272
	add	x0, x0, :lo12:.LC272
	ldrh	w3, [x4,x3]
	bl	printk
.L3132:
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	bl	ftl_update_l2p_map
	bl	pm_flush
	ldrh	w0, [x19]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3154
	bl	zftl_insert_data_list
.L3154:
	adrp	x0, .LANCHOR0+608
	mov	w2, 2
	mov	w1, 3
	ldr	x0, [x0,#:lo12:.LANCHOR0+608]
	add	x0, x0, 16
	cmp	x19, x0
	mov	x0, x19
	csel	w1, w2, w1, eq
	bl	ftl_open_sblk_init
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
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
	add	x24, x24, :lo12:.LANCHOR0
	add	x0, x24, 784
	str	x0, [x29,112]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29,120]
	stp	x19, x20, [sp,16]
	add	x0, x0, 1266
	str	x0, [x29,104]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
.L3158:
	ldrb	w0, [x24,2856]
	adrp	x21, .LANCHOR0
	cbz	w0, .L3160
	ldrb	w1, [x24,2888]
	sub	w0, w0, #1
	ldr	x2, [x29,112]
	str	x1, [x29,128]
	lsl	x1, x1, 6
	add	x27, x2, x1
	strb	w0, [x24,2856]
	ldr	w0, [x24,2840]
	ldrb	w1, [x2,x1]
	strb	w1, [x24,2888]
	ldr	w1, [x27,36]
	cmp	w1, w0
	bcc	.L3161
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 496
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3161:
	ldr	x0, [x29,128]
	add	x1, x21, :lo12:.LANCHOR0
	add	x4, x1, 784
	add	x0, x4, x0, lsl 6
	ldr	w2, [x1,2840]
	ldr	w23, [x0,36]
	cmp	w23, w2
	bcc	.L3162
	mov	x0, x27
	bl	buf_free
	mov	w0, -1
	b	.L3216
.L3162:
	ldr	x25, [x0,8]
	ldr	x28, [x0,24]
	ldrb	w22, [x0,57]
	ldrb	w20, [x0,56]
	ldrb	w0, [x1,2857]
	cbz	w0, .L3164
	ldr	x0, [x29,120]
	ldrb	w3, [x0,1266]
	add	x3, x4, x3, lsl 6
.L3165:
	ldrb	w5, [x3]
	cmp	w5, 255
	beq	.L3218
	sbfiz	x3, x5, 6, 32
	add	x3, x4, x3
	b	.L3165
.L3218:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L3164
	ldr	x0, [x3,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x25, x1
	bl	ftl_memcpy
	mov	x0, x27
	bl	buf_free
	b	.L3158
.L3164:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3168
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3168:
	add	x4, x21, :lo12:.LANCHOR0
	mov	w3, 0
	add	x4, x4, 784
	ldr	x19, [x4,-176]
	add	x19, x19, 16
.L3171:
	ldr	w0, [x4,36]
	cmp	w0, w23
	bne	.L3169
	ldrb	w5, [x4,2]
	tbz	x5, 3, .L3169
	add	x0, x21, :lo12:.LANCHOR0
	ubfiz	x3, x3, 6, 32
	add	x0, x0, 784
	and	w5, w5, -9
	add	x3, x0, x3
	ldr	w0, [x3,40]
	strb	w5, [x3,2]
	ldr	x26, [x3,8]
	str	w0, [x29,156]
	b	.L3170
.L3169:
	add	w3, w3, 1
	add	x4, x4, 64
	cmp	w3, 32
	bne	.L3171
	mov	x26, 0
.L3170:
	add	x3, x21, :lo12:.LANCHOR0
	str	wzr, [x29,136]
	ldrb	w0, [x3,2832]
	cmp	w20, w0
	bcs	.L3172
	add	w20, w22, w20
	cbz	x26, .L3173
	cbz	w22, .L3174
	mov	x0, x25
	mov	x1, x26
	lsl	w2, w22, 9
	str	x3, [x29,136]
	bl	ftl_memcpy
	ldr	x3, [x29,136]
	ldr	x19, [x3,608]
	add	x19, x19, 48
.L3174:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2832]
	cmp	w20, w2
	bcc	.L3175
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3217
.L3175:
	ubfiz	x1, x20, 9, 9
	sub	w2, w2, w20
	add	x0, x25, x1
	lsl	w2, w2, 9
	add	x1, x26, x1
	bl	ftl_memcpy
.L3217:
	str	wzr, [x29,136]
	b	.L3172
.L3173:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L3176
	mov	w0, 1
	bl	buf_alloc
	mov	x26, x0
	ldr	w1, [x29,156]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x0, [x26,24]
	ldr	w1, [x0,12]
	ldr	w0, [x0,4]
	add	w1, w1, 1
	str	w1, [x29,136]
	cmp	w0, w23
	bne	.L3177
	ldr	w0, [x26,52]
	cmn	w0, #1
	bne	.L3178
.L3177:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	x1, [x0,608]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	adrp	x0, .LC273
	ldrb	w1, [x26,1]
	add	x0, x0, :lo12:.LC273
	ldr	w4, [x26,52]
	bl	printk
	adrp	x0, .LC205
	mov	w2, 4
	ldr	x1, [x26,24]
	add	x0, x0, :lo12:.LC205
	mov	w3, w2
	bl	rknand_print_hex
.L3178:
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3179
	ldr	w0, [x26,52]
	cmn	w0, #1
	bne	.L3180
.L3179:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 567
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3180:
	cbz	w22, .L3181
	ldr	w0, [x29,156]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3182
	ldr	x1, [x26,8]
	mov	x0, x25
	bl	ftl_memcpy
	b	.L3183
.L3176:
	cbz	w22, .L3181
	lsl	w2, w22, 9
.L3182:
	mov	x0, x25
	mov	w1, 0
	bl	ftl_memset
.L3183:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x19, [x0,608]
	add	x19, x19, 48
.L3181:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2832]
	cmp	w20, w2
	bcc	.L3184
	bls	.L3185
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3185
.L3184:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L3186
	ldr	x1, [x26,8]
	lsl	x20, x0, 2
	add	x0, x25, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L3185
.L3186:
	add	x0, x25, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3185:
	cbz	x26, .L3172
	ldrb	w0, [x26,2]
	mov	x1, x26
	and	w0, w0, -9
	strb	w0, [x26,2]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 3895
	bl	buf_remove_buf
	mov	x0, x26
	bl	buf_free
.L3172:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3187
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3187:
	mov	x0, x19
	add	x20, x21, :lo12:.LANCHOR0
	bl	ftl_get_new_free_page
	ldr	x1, [x29,128]
	add	x3, x20, 784
	ldr	w6, [x29,136]
	lsl	x2, x1, 6
	add	x1, x3, x2
	ldr	w5, [x1,32]
	str	w5, [x28]
	str	w6, [x28,12]
	ldr	w5, [x29,156]
	str	w5, [x28,8]
	str	w23, [x28,4]
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
	ldr	x0, [x29,104]
	mov	x1, x27
	bl	buf_add_tail
	ldrb	w0, [x20,2857]
	add	w0, w0, 1
	strb	w0, [x20,2857]
	bl	timer_get_time
	ldr	x1, [x29,120]
	ldrb	w2, [x20,2857]
	str	w0, [x1,1268]
	cmp	w2, 2
	adrp	x0, .LANCHOR4
	bhi	.L3188
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L3160
.L3188:
	ldrb	w1, [x19,5]
	mov	w4, 2
	add	x6, x21, :lo12:.LANCHOR0
	cmp	w1, wzr
	ldrh	w1, [x19,6]
	csinc	w4, w4, wzr, ne
	add	x6, x6, 784
	cmp	w1, 1
	add	x1, x0, :lo12:.LANCHOR4
	csel	w4, w4, w2, ne
	ldrb	w5, [x1,1266]
	mov	w1, 0
	mov	w3, w5
.L3192:
	cmp	w1, w4
	beq	.L3219
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L3192
.L3219:
	add	x0, x0, :lo12:.LANCHOR4
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,1266]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 784
	strb	w2, [x0,2073]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3160
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3160:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w19, [x21,2856]
	cbnz	w19, .L3158
	bl	ftl_write_completed
	mov	w0, w19
.L3216:
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
	ldrb	w0, [x21,3273]
	ldr	x22, [x21,608]
	cbnz	w0, .L3221
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3220
	ldrh	w22, [x21,2922]
	mov	w0, w22
	add	w22, w22, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w23, w0
	mov	w1, 1
	mov	x0, x20
	strh	w22, [x21,2922]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3274
	cmn	w0, #1
	bne	.L3223
.L3274:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 601
	add	x1, x1, 784
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3223:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,144]
	cmn	w0, #1
	bne	.L3225
	mov	w0, w21
	add	x1, x29, 144
	mov	w2, 0
	bl	pm_log2phys
.L3225:
	ldr	w22, [x29,144]
	cmp	w22, w23
	bne	.L3226
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	add	x1, x1, 2832
.L3229:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L3227
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L3227
	mov	x0, x20
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3220
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC274
	mov	w1, w21
	mov	w2, w22
	add	x0, x0, :lo12:.LC274
	ldrh	w3, [x19,2922]
	bl	printk
	b	.L3220
.L3227:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3229
	add	x23, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20,2]
	str	w21, [x20,36]
	strb	wzr, [x20,57]
	ldrb	w0, [x23,2832]
	strb	w0, [x20,56]
	mov	w0, 10
	orr	w0, w1, w0
	strb	w0, [x20,2]
	ldr	x0, [x20,24]
	ldr	w0, [x0]
	str	w0, [x20,32]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3230
	ldr	w2, [x29,144]
	mov	w0, w21
	str	x2, [x29,136]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,136]
	adrp	x0, .LC275
	ldrh	w5, [x23,2922]
	add	x0, x0, :lo12:.LC275
	mov	w1, w21
	mov	w4, w22
	bl	printk
.L3230:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,2864]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,2924]
	add	w0, w0, 1
	strh	w0, [x19,2924]
	b	.L3220
.L3226:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3232
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2922]
	adrp	x0, .LC275
	add	x0, x0, :lo12:.LC275
	mov	w1, w21
	mov	w2, w22
	mov	w4, w23
	bl	printk
.L3232:
	mov	x0, x20
	bl	buf_free
	b	.L3220
.L3221:
	ldrb	w24, [x21,3257]
	add	x0, x21, 2896
	add	x22, x22, 80
	cmp	w24, 3
	bne	.L3233
	ldrb	w1, [x21,3272]
	cbz	w1, .L3234
	ldrb	w20, [x22,9]
	ldrh	w21, [x0,314]
	str	w24, [x29,136]
	add	w23, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w23, w21, w23
	madd	w20, w23, w20, w21
	uxth	w20, w20
	b	.L3236
.L3234:
	ldrh	w20, [x0,314]
	adrp	x0, .LANCHOR2
	ldrb	w21, [x22,9]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1720
	str	w24, [x29,136]
	sdiv	w1, w20, w21
	msub	w20, w1, w21, w20
	ldrh	w25, [x0,w1,sxtw 1]
	uxth	w20, w20
	lsr	w23, w25, 3
	and	w25, w25, 7
	madd	w21, w23, w21, w20
	cmp	w25, 1
	add	w21, w21, w21, lsl 1
	uxth	w21, w21
	bne	.L3237
	b	.L3236
.L3233:
	ldrb	w20, [x22,9]
	ldrb	w1, [x21,3348]
	ldrh	w21, [x0,314]
	mov	w0, 1
	sdiv	w23, w21, w20
	msub	w20, w23, w20, w21
	uxth	w20, w20
	cbz	w1, .L3315
	mov	w0, 2
.L3315:
	str	w0, [x29,136]
.L3236:
	mov	w25, 0
	add	x24, x19, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR4
.L3239:
	ldrb	w1, [x22,9]
	ldr	w0, [x29,136]
	mul	w0, w0, w1
	cmp	w25, w0
	bge	.L3272
	ldrh	w0, [x24,3274]
	add	w26, w21, w25
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	beq	.L3272
	add	x0, x3, :lo12:.LANCHOR4
	sbfiz	x26, x26, 2, 32
	ldr	x1, [x0,1144]
	ldr	w0, [x1,x26]
	cmn	w0, #1
	bne	.L3240
	ldrh	w27, [x24,2922]
	ldrh	w0, [x24,2920]
	str	x3, [x29,120]
	str	x1, [x29,128]
	cmp	w0, w27
	bls	.L3220
	mov	w0, w27
	add	w27, w27, 1
	bl	gc_get_src_ppa_from_index
	strh	w27, [x24,2922]
	ldr	x1, [x29,128]
	ldr	x3, [x29,120]
	str	w0, [x1,x26]
.L3240:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L3239
.L3272:
	mov	w25, 1
.L3237:
	adrp	x0, .LANCHOR2
	str	x0, [x29,120]
	ldr	w1, [x0,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3242
	adrp	x0, .LANCHOR4+1144
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x5, x21, 2, 16
	mov	w1, w23
	mov	w2, w25
	mov	w3, w20
	ldr	x6, [x0,#:lo12:.LANCHOR4+1144]
	adrp	x0, .LC276
	ldrh	w4, [x4,3210]
	add	x0, x0, :lo12:.LC276
	ldr	w5, [x6,x5]
	mov	w6, w21
	bl	printk
.L3242:
	adrp	x0, .LC0
	mov	w27, 0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,112]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 784
	str	x0, [x29,104]
.L3243:
	ldrb	w0, [x22,9]
	ldr	w1, [x29,136]
	mul	w0, w1, w0
	cmp	w27, w0
	bge	.L3319
	add	w26, w21, w27
	sxtw	x0, w26
	str	x0, [x29,128]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x29,128]
	ldr	x0, [x0,3264]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	bne	.L3244
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbnz	x0, .L3245
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w24
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L3220
.L3245:
	add	x28, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,128]
	ldrb	w1, [x24,1]
	ldr	x0, [x28,3264]
	strb	w1, [x0,x2]
	strh	w26, [x24,48]
	ldrb	w0, [x28,2903]
	strb	w25, [x24,61]
	add	w0, w0, 1
	strb	w0, [x28,2903]
	ldrb	w1, [x22,9]
	ldrh	w0, [x28,3274]
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	adrp	x26, .LANCHOR4
	bne	.L3246
	add	x26, x26, :lo12:.LANCHOR4
	mov	w1, -1
	ldr	x0, [x26,1136]
	str	w1, [x0,x2,lsl 2]
	ldrh	w0, [x28,3274]
	ldrb	w2, [x28,3276]
	ldr	x1, [x26,1136]
	mul	w2, w0, w2
	ldr	x0, [x24,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x28,3274]
	ldrb	w2, [x28,3276]
	ldr	x1, [x26,1144]
	mul	w2, w0, w2
	ldr	x0, [x24,8]
	add	x0, x0, x2, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w2, [x28,2832]
	mov	w1, 0
	ldr	x0, [x24,24]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x24,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	ldrh	w0, [x28,3274]
	ldrb	w1, [x28,3276]
	ldr	x26, [x24,24]
	mul	w1, w0, w1
	ldr	x0, [x24,8]
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x26,4]
	ldr	x1, [x28,608]
	ldr	x0, [x24,24]
	ldr	w1, [x1,132]
	b	.L3316
.L3246:
	ldr	x0, [x29,128]
	mov	w1, 1
	lsl	x28, x0, 2
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x0, [x0,1144]
	ldr	w0, [x0,x28]
	str	w0, [x24,40]
	mov	x0, x24
	bl	sblk_read_page
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3247
	cmn	w0, #1
	bne	.L3248
.L3247:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x4,3340]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x24,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3338]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3275
	cmn	w0, #1
	bne	.L3248
.L3275:
	ldr	x0, [x24,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3248:
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3276
	cmn	w0, #1
	bne	.L3251
.L3276:
	ldr	x1, [x29,104]
	mov	w2, 716
	ldr	x0, [x29,112]
	bl	printk
	bl	dump_stack
.L3251:
	ldr	x1, [x24,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2840]
	cmp	w2, w0
	bcc	.L3253
	mov	w0, -1
	str	w0, [x1,4]
.L3253:
	ldr	x0, [x24,24]
	add	x26, x26, :lo12:.LANCHOR4
	ldr	w1, [x0,4]
	ldr	x0, [x26,1136]
	str	w1, [x0,x28]
	ldr	x0, [x24,24]
	ldr	w1, [x24,40]
.L3316:
	str	w1, [x0,8]
.L3244:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,128]
	add	x1, x3, 784
	sxtw	x5, w27
	ldr	x0, [x3,3264]
	ldrb	w4, [x0,x2]
	add	x0, x29, 144
	mov	w2, 2
	add	x1, x1, x4, lsl 6
	str	x1, [x0,w27,sxtw 3]
	ldrb	w0, [x3,3257]
	strh	w2, [x1,50]
	cmp	w0, 3
	strb	w25, [x1,61]
	adrp	x2, .LANCHOR4
	bne	.L3254
	udiv	w0, w27, w0
	ldrh	w3, [x3,3350]
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w0, w20
	add	x4, x22, x4, sxtw 1
	ldrh	w4, [x4,16]
	mul	w3, w4, w3
	ldrh	w4, [x1,48]
	add	w0, w0, w0, lsl 1
	orr	w3, w3, w23
	sub	w0, w27, w0
	str	w3, [x1,40]
	ldr	x1, [x2,1152]
	uxth	w0, w0
	add	w0, w0, 1
	orr	w0, w3, w0, lsl 24
	str	w0, [x1,x4,lsl 2]
	b	.L3255
.L3254:
	cmp	w0, 2
	bne	.L3256
	ldrb	w6, [x3,3348]
	add	x1, x1, 32
	ldrh	w0, [x3,3350]
	cbnz	w6, .L3257
	add	w3, w27, w20
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	b	.L3318
.L3257:
	add	w3, w20, w27, lsr 1
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	and	w3, w27, 1
	add	w0, w0, w3
.L3318:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L3256:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, 784
	add	x0, x0, x4, lsl 6
	ldr	w3, [x0,40]
	ldrh	w1, [x0,48]
	ldr	x0, [x2,1152]
	str	w3, [x0,x1,lsl 2]
.L3255:
	ldr	x0, [x29,120]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3258
	add	x0, x29, 144
	ldr	x1, [x29,128]
	mov	w2, w25
	ldr	x5, [x0,x5,lsl 3]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x0,2902]
	ldr	x0, [x0,3264]
	ldrh	w6, [x5,48]
	ldrb	w7, [x5,1]
	ldrb	w4, [x0,x1]
	mov	w0, 3
	mov	w1, w27
	udiv	w0, w27, w0
	add	w0, w20, w0
	str	w0, [sp]
	ldr	x0, [x5,24]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	adrp	x0, .LC277
	add	x0, x0, :lo12:.LC277
	ldr	w5, [x5,40]
	bl	printk
.L3258:
	add	w8, w27, 1
	uxth	w27, w8
	b	.L3243
.L3319:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x2,3257]
	cmp	w0, 3
	add	x0, x29, 144
	bne	.L3302
	ldrb	w1, [x2,3272]
	cbz	w1, .L3261
.L3264:
	ldr	x1, [x29,144]
	strb	wzr, [x1,60]
	b	.L3262
.L3261:
	cmp	w25, 1
	mov	w2, 9
	beq	.L3317
	cmp	w25, 2
	bne	.L3264
	mov	w2, 13
.L3317:
	ldr	x1, [x29,144]
	strb	w2, [x1,60]
.L3262:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L3265
.L3302:
	ldrb	w2, [x22,9]
	ldr	w3, [x29,136]
	mul	w2, w2, w3
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L3320
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L3302
.L3320:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,144]
	mul	w1, w3, w1
	bl	sblk_prog_page
.L3265:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x22,9]
	ldrb	w3, [x1,3272]
	cbz	w3, .L3267
	add	w0, w2, w2, lsl 1
	and	w2, w0, 1023
	b	.L3268
.L3267:
	ldrb	w1, [x1,3348]
	ubfiz	w0, w2, 1, 8
	cmp	w1, wzr
	csel	w2, w0, w2, ne
.L3268:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldr	w0, [x1,52]
	add	w0, w0, w2
	str	w0, [x1,52]
	ldrh	w1, [x19,3210]
	add	w0, w2, w1
	ldrh	w1, [x19,3274]
	uxth	w0, w0
	strh	w0, [x19,3210]
	ldrb	w2, [x22,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L3269
	ldr	x0, [x19,608]
	strh	wzr, [x0,86]
.L3269:
	bl	gc_write_completed
.L3220:
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
	ldrh	w20, [x1,2844]
	cmp	w26, 1
	ldrh	w0, [x1,2848]
	ldr	x24, [x1,608]
	add	w20, w0, w20
	ldrh	w22, [x1,2846]
	uxth	w20, w20
	bne	.L3322
	adrp	x21, .LANCHOR7
	add	x1, x21, :lo12:.LANCHOR7
	ldr	w23, [x1,-80]
	cbnz	w23, .L3323
	ldr	w1, [x1,-76]
	cbz	w1, .L3322
.L3323:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,3306]
	cmp	w20, w2, lsr 2
	bls	.L3322
	ldrh	w1, [x1,2872]
	cmp	w1, w20
	bcs	.L3322
	add	w23, w23, 20
	bl	timer_get_time
	cmp	w23, w0
	bcs	.L3324
	add	x0, x21, :lo12:.LANCHOR7
	str	wzr, [x0,-80]
.L3324:
	add	x19, x21, :lo12:.LANCHOR7
	ldr	w20, [x19,-76]
	bl	timer_get_time
	add	w20, w20, 20
	cmp	w20, w0
	bcs	.L3507
	str	wzr, [x19,-76]
	b	.L3507
.L3322:
	adrp	x23, .LANCHOR4
	add	x1, x23, :lo12:.LANCHOR4
	ldrb	w2, [x1,1130]
	mov	w1, 16
	cmp	w2, 6
	bhi	.L3480
	adrp	x1, .L3328
	add	x1, x1, :lo12:.L3328
	ldrh	w1, [x1,w2,uxtw #1]
	adr	x2, .Lrtx3328
	add	x1, x2, w1, sxth #2
	br	x1
.Lrtx3328:
	.section	.rodata
	.align	0
	.align	2
.L3328:
	.2byte	(.L3327 - .Lrtx3328) / 4
	.2byte	(.L3329 - .Lrtx3328) / 4
	.2byte	(.L3330 - .Lrtx3328) / 4
	.2byte	(.L3331 - .Lrtx3328) / 4
	.2byte	(.L3332 - .Lrtx3328) / 4
	.2byte	(.L3438 - .Lrtx3328) / 4
	.2byte	(.L3334 - .Lrtx3328) / 4
	.text
.L3327:
	add	x21, x19, :lo12:.LANCHOR0
	add	w22, w22, w0
	ldrh	w1, [x24,80]
	uxth	w22, w22
	ldrh	w0, [x21,2850]
	ldrh	w25, [x21,2852]
	add	w25, w25, w0
	mov	w0, 65535
	cmp	w1, w0
	uxth	w25, w25
	beq	.L3335
	cbnz	w26, .L3336
	ldrh	w0, [x21,2872]
	cmp	w20, w0, lsl 1
	bge	.L3507
.L3336:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w0, [x21,3304]
	add	w0, w0, 1
	uxth	w0, w0
	strh	w0, [x21,3304]
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3338
	add	x3, x23, :lo12:.LANCHOR4
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1132]
	add	w1, w1, 1
	str	w1, [x3,1132]
	ldr	x3, [x21,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,3300]
	cmp	w3, w2
	bcs	.L3339
	ldrh	w3, [x21,576]
	cmp	w1, w3, lsr 4
	bls	.L3338
	ldrh	w1, [x21,2874]
	cmp	w1, w2
	bls	.L3338
.L3339:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x2,3273]
	bl	gc_add_sblk
	cbz	w0, .L3340
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	str	wzr, [x23,1132]
	strb	w0, [x23,1130]
	b	.L3507
.L3338:
	add	x0, x19, :lo12:.LANCHOR0
	strh	wzr, [x0,3304]
.L3340:
	cmp	w22, 15
	mov	w21, 2
	bls	.L3341
	cbz	w25, .L3440
	add	x0, x19, :lo12:.LANCHOR0
	mov	w21, 1
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bls	.L3341
.L3440:
	mov	w21, 2
.L3341:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3342
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, w20
	mov	w4, w22
	ldr	x0, [x1,608]
	ldrb	w2, [x1,3273]
	mov	w1, 1850
	ldrh	w5, [x0,124]
	ldrh	w6, [x0,120]
	ldrh	w7, [x0,122]
	ldrh	w0, [x24,80]
	str	w0, [sp]
	adrp	x0, .LC278
	add	x0, x0, :lo12:.LC278
	bl	printk
.L3342:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w2, 1
	ldrb	w0, [x19,3273]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3343
.L3344:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	b	.L3510
.L3343:
	ldrb	w0, [x19,3273]
	mov	w1, 3
	mov	w2, 1
	bl	gc_search_src_blk
	cmp	w0, wzr
	bgt	.L3344
	b	.L3507
.L3335:
	cmp	w26, 1
	bne	.L3345
	bl	gc_scan_static_data
	ldr	x0, [x21,608]
	ldrh	w0, [x0,122]
	cbz	w0, .L3346
.L3347:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR4
	strb	w0, [x19,3273]
	b	.L3510
.L3346:
	bl	gc_static_wearleveling
	cbnz	w0, .L3347
	bl	gc_block_vpn_scan
	cbz	w22, .L3441
	cmp	w20, w25
	bcs	.L3350
	ldrh	w0, [x21,3306]
	cmp	w20, w0, lsl 1
	blt	.L3351
.L3350:
	add	x0, x19, :lo12:.LANCHOR0
	add	w1, w20, w25
	ldrh	w2, [x0,3306]
	cmp	w1, w2, lsl 1
	blt	.L3351
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bcc	.L3441
.L3351:
	add	x21, x19, :lo12:.LANCHOR0
	add	x27, x23, :lo12:.LANCHOR4
	mov	w28, 1
	ldr	w1, [x27,1132]
	ldrh	w0, [x21,576]
	add	w1, w1, 1
	strb	w28, [x21,3273]
	str	w1, [x27,1132]
	cmp	w1, w0, lsr 5
	bls	.L3353
	ldrh	w0, [x21,2854]
	cmp	w0, w22
	bls	.L3353
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w0, 65535
	cmp	w1, w0
	beq	.L3441
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x21,600]
	ldrb	w2, [x21,3276]
	mov	w24, 16
	ldrh	w0, [x0,x1]
	ldrh	w1, [x21,3300]
	mul	w1, w1, w2
	cmp	w0, w1
	bgt	.L3349
	str	wzr, [x27,1132]
	mov	w0, w28
	mov	w1, 2
	b	.L3500
.L3353:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2854]
	ldrh	w1, [x0,3308]
	cmp	w2, w1
	bcc	.L3356
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	b	.L3501
.L3356:
	ldrh	w24, [x0,2850]
	cbnz	w24, .L3357
	ldrh	w0, [x0,2852]
	cmp	w0, 8
	bls	.L3349
.L3357:
	mov	w0, 1
	mov	w1, w0
.L3500:
	mov	w2, 4
.L3501:
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3344
	b	.L3441
.L3345:
	ldrh	w0, [x21,2872]
	cmp	w0, w20
	bcc	.L3507
.L3441:
	mov	w24, 16
.L3349:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	w0, [x21,2884]
	cbz	w0, .L3358
	mov	w0, 1
	mov	w1, 5
	strb	w0, [x21,3273]
	mov	w0, 0
	str	wzr, [x21,2884]
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3359
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x21,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 8
	bhi	.L3359
	mov	w2, 1
	mov	w1, 0
	str	w2, [x21,2884]
	bl	gc_add_sblk
	cbnz	w0, .L3344
.L3359:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3360
	add	x3, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x3,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3360
	mov	w2, 1
	mov	w1, 0
	str	w2, [x3,2884]
	bl	gc_add_sblk
	cbnz	w0, .L3344
.L3360:
	mov	w0, 0
	bl	zftl_get_gc_node.part.12
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3358
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3358
	mov	w1, 1
	str	w1, [x2,2884]
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3344
.L3358:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	x7, [x21,608]
	strb	w0, [x21,3273]
	ldrh	w5, [x7,124]
	cbz	w5, .L3361
	add	x23, x23, :lo12:.LANCHOR4
	strb	wzr, [x21,3273]
	strb	w0, [x23,1130]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3362
	ldrh	w6, [x7,120]
	adrp	x0, .LC279
	ldrh	w7, [x7,122]
	mov	w1, 1948
	mov	w2, 0
	mov	w3, w20
	mov	w4, w22
	add	x0, x0, :lo12:.LC279
	bl	printk
	b	.L3362
.L3361:
	ldrh	w2, [x21,2872]
	cmp	w20, w2
	bcs	.L3363
	cbz	w22, .L3364
	cmp	w22, 16
	bls	.L3365
	ldrh	w2, [x21,2854]
	ldrh	w1, [x21,3308]
	cmp	w2, w1
	bhi	.L3365
	mov	w1, w0
	mov	w2, 4
	str	x5, [x29,104]
	bl	gc_search_src_blk
	uxth	w0, w0
	ldr	x5, [x29,104]
	cbnz	w0, .L3366
	ldrb	w0, [x21,3273]
	b	.L3509
.L3366:
	mov	w1, 5
	mov	w0, w5
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3435
	add	x3, x23, :lo12:.LANCHOR4
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1132]
	add	w1, w1, 1
	str	w1, [x3,1132]
	ldr	x3, [x21,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,3300]
	cmp	w3, w2
	bcs	.L3369
	ldrh	w3, [x21,576]
	cmp	w1, w3, lsr 4
	bls	.L3435
	ldrh	w1, [x21,2874]
	cmp	w1, w2
	bls	.L3435
.L3369:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x19,3273]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19,2884]
	add	x0, x23, :lo12:.LANCHOR4
	str	wzr, [x0,1132]
	b	.L3435
.L3365:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3435
	add	x19, x19, :lo12:.LANCHOR0
.L3506:
	ldrb	w0, [x19,3273]
	mov	w1, 3
	mov	w2, 2
.L3503:
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3367
.L3364:
	adrp	x0, .LANCHOR2
	strb	w22, [x21,3273]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3371
	adrp	x0, .LC279
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC279
	mov	w1, 1978
	mov	w2, w22
	mov	w3, w20
	mov	w4, w22
	mov	w5, w22
	bl	printk
.L3371:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, 16
	ldrb	w0, [x19,3273]
	bls	.L3372
.L3509:
	mov	w1, 3
	mov	w2, 4
	b	.L3503
.L3372:
	mov	w1, 1
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3435
	b	.L3506
.L3363:
	cmp	w26, 1
	mov	w1, w24
	bne	.L3480
	cmp	w20, w2, lsl 1
	bge	.L3373
	cmp	w25, w22, lsr 1
	bcs	.L3374
	ldrh	w2, [x21,2854]
	ldrh	w0, [x21,3308]
	cmp	w2, w0
	bcs	.L3374
	ldrh	w0, [x21,3306]
	lsr	w0, w0, 2
	strh	w0, [x21,2872]
	b	.L3480
.L3374:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w21, w0
	mov	w1, 65535
	mov	w27, w21
	cmp	w21, w1
	bne	.L3375
.L3380:
	cmp	w22, 1
	bhi	.L3376
	b	.L3377
.L3375:
	add	x26, x23, :lo12:.LANCHOR4
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x21, w21
	ldr	w1, [x26,1132]
	ldrh	w3, [x2,576]
	add	w1, w1, 1
	str	w1, [x26,1132]
	cmp	w1, w3, lsr 4
	bls	.L3378
	ldr	x1, [x2,600]
	str	wzr, [x26,1132]
	ldrh	w3, [x1,x21,lsl 1]
	ldrh	w1, [x2,2874]
	cmp	w3, w1
	bcs	.L3378
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	cbz	w0, .L3378
	mov	w0, 1
	strb	w0, [x26,1130]
	b	.L3362
.L3378:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x21,lsl 1]
	ldrh	w0, [x1,3300]
	cmp	w2, w0, lsr 1
	bhi	.L3379
	mov	w0, w27
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L3435
.L3379:
	ldrh	w3, [x1,2850]
	ldrh	w0, [x1,2852]
	add	w0, w3, w0
	ldrh	w3, [x1,3306]
	cmp	w0, w3, lsl 1
	ble	.L3380
	ldrh	w0, [x1,2874]
	cmp	w0, w2
	bcc	.L3377
	b	.L3380
.L3376:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 1
	cmp	w22, 16
	strb	w0, [x21,3273]
	bls	.L3381
	ldrh	w2, [x21,2854]
	ldrh	w1, [x21,3308]
	cmp	w2, w1
	bhi	.L3381
	mov	w1, w0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3382
	ldrb	w0, [x21,3273]
	mov	w1, 3
	mov	w2, 4
	b	.L3502
.L3381:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3382
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 3
	mov	w2, 2
	ldrb	w0, [x0,3273]
.L3502:
	bl	gc_search_src_blk
	uxth	w0, w0
.L3382:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, w25, lsr 1
	ldrh	w1, [x19,3306]
	bls	.L3504
	lsr	w1, w1, 2
	b	.L3505
.L3377:
	cmp	w20, w25
	add	x19, x19, :lo12:.LANCHOR0
	bcs	.L3384
	mov	w0, 4
	strb	wzr, [x19,3273]
	bl	zftl_get_gc_node.part.12
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3384
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,600]
	ldrb	w2, [x19,3276]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x19,3300]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	bgt	.L3384
	mov	w1, 3
	mov	w0, 0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	ldrh	w1, [x19,3306]
.L3504:
	lsr	w1, w1, 1
.L3505:
	strh	w1, [x19,2872]
	b	.L3367
.L3384:
	ldrh	w0, [x19,3306]
	lsr	w0, w0, 2
	strh	w0, [x19,2872]
	b	.L3362
.L3373:
	ldrh	w0, [x21,3306]
	mov	w24, w5
	lsr	w0, w0, 2
	strh	w0, [x21,2872]
	b	.L3362
.L3367:
	mov	w1, w24
	cbz	w0, .L3480
.L3435:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x23,1130]
	b	.L3362
.L3329:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x20,2896]
	cmp	w1, w0
	bne	.L3387
	bl	gc_get_src_blk
	strh	w0, [x20,2896]
.L3387:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2896
	ldrh	w3, [x0,2896]
	cmp	w3, w2
	beq	.L3508
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,584]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3390
	mov	x0, 0
.L3389:
	cmp	w5, w0, uxth
	bls	.L3390
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3389
.L3395:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2896]
	b	.L3507
.L3390:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L3448
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3392
.L3448:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3395
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2211
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3395
.L3392:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 2
	b	.L3510
.L3330:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3396
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 3
.L3510:
	strb	w0, [x23,1130]
	b	.L3507
.L3396:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,2896]
	cmp	w0, w3
	beq	.L3344
	ldrh	w3, [x1,2920]
	add	x23, x23, :lo12:.LANCHOR4
	cbz	w3, .L3397
	mov	w0, 4
	strh	wzr, [x1,2922]
	strb	w0, [x23,1130]
	b	.L3507
.L3397:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,600]
	mov	w2, 1
	strb	w2, [x23,1130]
	ldrh	w0, [x1,x0]
	cbz	w0, .L3398
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2239
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3398:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x20, 2896
	ldrh	w0, [x20,2896]
	bl	ftl_free_sblk
	ldr	x0, [x20,600]
	ldrh	w1, [x20,2896]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x21,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3399
	strh	w0, [x21,30]
	b	.L3395
.L3399:
	strh	wzr, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3395
.L3331:
	add	x21, x19, :lo12:.LANCHOR0
.L3481:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,2898]
	add	x0, x21, 2896
	ldrh	w1, [x21,3300]
	cmp	w2, w1
	bcs	.L3401
	cmp	w20, 7
	bls	.L3481
	b	.L3507
.L3401:
	ldrh	w3, [x0,24]
	adrp	x2, .LANCHOR2
	cbz	w3, .L3402
	add	x23, x23, :lo12:.LANCHOR4
	mov	w1, 4
	strh	wzr, [x0,26]
	strb	w1, [x23,1130]
	ldr	w1, [x2,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3403
	ldrh	w1, [x0]
	adrp	x0, .LC280
	ldr	x4, [x21,600]
	add	x0, x0, :lo12:.LC280
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L3403:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2896]
	ldr	x1, [x0,600]
	ldrh	w0, [x0,2920]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L3404
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2273
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3404:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,2896]
	ldr	x0, [x19,600]
	ldrh	w2, [x19,2920]
	strh	w2, [x0,x1,lsl 1]
	b	.L3507
.L3402:
	add	x23, x23, :lo12:.LANCHOR4
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x20, [x21,584]
	add	x20, x20, x1, uxth 2
	strb	w0, [x23,1130]
	ldr	w0, [x2,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3405
	ldrb	w2, [x20,2]
	adrp	x0, .LC281
	add	x0, x0, :lo12:.LC281
	ubfx	x2, x2, 5, 3
	bl	printk
.L3405:
	ldrb	w0, [x20,2]
	tst	w0, 192
	beq	.L3449
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3406
.L3449:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2283
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3406:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2896
	ldrh	w0, [x19,2896]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,2896]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3408
	strh	w0, [x20,30]
	b	.L3507
.L3408:
	strh	wzr, [x20,30]
	b	.L3511
.L3332:
	cbnz	w26, .L3409
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2872]
	cmp	w0, w20
	bcc	.L3507
.L3409:
	ldrh	w1, [x24,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3410
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w21, [x22,3273]
	cmp	w21, 1
	bne	.L3410
	bl	ftl_flush
	ldrh	w0, [x22,3214]
	cbz	w0, .L3411
	mov	w0, w21
.L3411:
	mov	w1, 5
	bl	zftl_gc_get_free_sblk
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L3413
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x21, [x0,584]
	add	x21, x21, x20, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L3414
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2312
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3414:
	ldrb	w0, [x21,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x21,2]
.L3436:
	mov	w0, w20
	mov	w1, 1
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR4
	bl	ftl_erase_sblk
	mov	w0, 5
	add	x1, x24, 96
	strb	w0, [x24,84]
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x19,3300]
	strb	w0, [x24,89]
	ldrb	w2, [x19,3276]
	mul	w0, w0, w1
	strh	w0, [x24,86]
	mov	w1, 255
	ldrh	w0, [x19,3274]
	strh	w20, [x24,80]
	strh	wzr, [x24,82]
	mul	w2, w0, w2
	ldr	x0, [x23,1136]
	strb	wzr, [x24,85]
	lsl	w2, w2, 2
	strh	wzr, [x24,90]
	bl	ftl_memset
	ldrh	w0, [x19,3274]
	mov	w1, 255
	ldrb	w2, [x19,3276]
	mul	w2, w0, w2
	ldr	x0, [x23,1144]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x19,3274]
	mov	w1, 255
	ldrb	w2, [x19,3276]
	ldr	x0, [x19,3264]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x19,608]
	mov	w1, -1
	str	w20, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x19,2952]
	ldr	x0, [x19,2864]
	strh	w20, [x0,126]
	mov	w0, -1
	str	w0, [x19,3220]
	mov	w0, 0
	strh	wzr, [x19,3210]
	strh	wzr, [x19,3212]
	strh	wzr, [x19,3216]
	bl	ftl_info_flush
	b	.L3507
.L3410:
	cmp	w26, 1
	mov	w21, 4
	csinc	w21, w21, wzr, eq
	cmp	w20, 15
	add	w0, w21, 4
	add	x22, x19, :lo12:.LANCHOR0
	csel	w21, w0, w21, ls
.L3417:
	sub	w21, w21, #1
	uxtb	w21, w21
	cmp	w21, 255
	beq	.L3507
	bl	gc_do_copy_back
	ldrb	w0, [x22,3273]
	cbnz	w0, .L3418
	ldrb	w0, [x22,2834]
	cmp	w0, 3
	bhi	.L3419
	bl	ftl_write_commit
.L3419:
	ldrh	w1, [x22,2922]
	ldrh	w0, [x22,2920]
	cmp	w1, w0
	bcc	.L3417
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x23,1130]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x22,2896]
	ldr	x0, [x22,600]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3421
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2387
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3421:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2896]
	ldr	x3, [x1,600]
	ubfiz	x2, x0, 1, 16
	ldrh	w4, [x3,x2]
	cbnz	w4, .L3422
	strh	w4, [x3,x2]
	ldrh	w0, [x1,2896]
	bl	ftl_free_sblk
	b	.L3395
.L3422:
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	b	.L3395
.L3418:
	add	x20, x22, 2896
	ldrh	w0, [x20,320]
	cbz	w0, .L3423
	strh	wzr, [x20,320]
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	w0, [x20,324]
	cmn	w0, #1
	beq	.L3424
	bl	ftl_mask_bad_block
.L3424:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,608]
	str	wzr, [x20,3220]
	strh	wzr, [x20,2952]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x20,608]
	mov	w0, -1
	ldr	x2, [x20,2864]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x20,2904]
	cbz	x0, .L3425
	bl	buf_free
.L3425:
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR4
	str	xzr, [x19,2904]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2425
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x23,1130]
	bl	printk
	bl	dump_stack
	b	.L3507
.L3423:
	ldrh	w0, [x24,86]
	cmp	w0, 1
	bls	.L3426
	ldrh	w1, [x20,26]
	ldrh	w0, [x20,24]
	cmp	w1, w0
	bcc	.L3417
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x23,1130]
	ldrh	w0, [x20,56]
	add	w1, w0, 1
	add	x0, x20, x0, sxtw 1
	strh	w1, [x20,56]
	ldrh	w1, [x20]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x20]
	b	.L3507
.L3426:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 5
	ldrh	w1, [x20,26]
	strb	w0, [x23,1130]
	ldrh	w0, [x20,24]
	cmp	w1, w0
	bcc	.L3427
	ldrh	w0, [x20,56]
	add	w1, w0, 1
	add	x0, x20, x0, sxtw 1
	strh	w1, [x20,56]
	ldrh	w1, [x20]
	strh	w1, [x0,58]
	mov	w0, -1
	strh	w0, [x20]
.L3427:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,608]
	ldrh	w1, [x24,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,2912]
	ldrb	w2, [x20,3257]
	ldrh	w1, [x20,3300]
	strh	w1, [x20,2914]
	cmp	w2, 2
	strh	w2, [x20,2916]
	bne	.L3429
	lsl	w1, w1, 1
	strh	w1, [x20,2914]
	ldrb	w1, [x20,3348]
	cbnz	w1, .L3429
	mov	w1, 1
	strh	w1, [x20,2916]
.L3429:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2918]
	b	.L3507
.L3438:
	mov	w21, 0
	add	x24, x19, :lo12:.LANCHOR0
.L3333:
	bl	gc_check_data_one_wl
	cbz	w0, .L3432
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR4
	ldr	x0, [x19,608]
	strh	wzr, [x19,2952]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,608]
	mov	w0, -1
	ldr	x2, [x19,2864]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,2904]
	bl	buf_free
	str	xzr, [x19,2904]
	strb	wzr, [x23,1130]
.L3511:
	bl	flt_sys_flush
	b	.L3507
.L3432:
	ldrh	w1, [x24,2912]
	ldrh	w0, [x24,2914]
	cmp	w1, w0
	bcc	.L3433
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 6
	strb	w0, [x23,1130]
	ldr	x0, [x24,2904]
	bl	buf_free
	str	xzr, [x24,2904]
	b	.L3507
.L3433:
	cmp	w20, 15
	bls	.L3333
	cmp	w26, 1
	bne	.L3507
	add	w21, w21, 1
	uxtb	w21, w21
	cmp	w21, 4
	bls	.L3333
	b	.L3507
.L3334:
	bl	gc_update_l2p_map_new
	mov	w20, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	pm_flush
	strh	w20, [x24,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,2864]
	strh	w20, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L3508:
	add	x23, x23, :lo12:.LANCHOR4
	strb	wzr, [x23,1130]
.L3507:
	mov	w1, 16
	b	.L3480
.L3362:
	mov	w1, w24
	b	.L3480
.L3413:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2318
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3436
.L3480:
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
	stp	x25, x26, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w23, w2
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	mov	w28, w1
	mov	x25, x3
	tbz	x0, 12, .L3513
	adrp	x0, .LC282
	mov	w1, w19
	add	x0, x0, :lo12:.LC282
	mov	w2, w28
	mov	w3, w23
	bl	printk
.L3513:
	cbnz	w19, .L3514
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3515
.L3514:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3516
	lsl	w19, w19, 13
	mov	w2, 8192
.L3515:
	add	w1, w28, w23
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3516
	add	w28, w19, w28
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	sub	w22, w23, #1
	add	w22, w22, w28
	add	w26, w28, w23
	ldrb	w0, [x0,2832]
	udiv	w24, w28, w0
	mov	w21, w24
	udiv	w22, w22, w0
	sub	w20, w22, w24
	add	w20, w20, 1
.L3517:
	cbz	w20, .L3536
	mov	w0, 0
	bl	buf_alloc
	mov	x27, x0
	cbnz	x0, .L3518
	bl	ftl_write_commit
	b	.L3517
.L3518:
	strb	wzr, [x0,57]
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w21, w24
	ldrb	w4, [x0,2832]
	cset	w0, eq
	strb	w4, [x27,56]
	cmp	w21, w22
	beq	.L3520
	cbz	w0, .L3521
	b	.L3530
.L3520:
	cbz	w0, .L3522
.L3530:
	udiv	w0, w28, w4
	msub	w0, w0, w4, w28
	uxtb	w0, w0
	strb	w0, [x27,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w23
	csel	w4, w23, w4, hi
	b	.L3534
.L3522:
	msub	w4, w4, w21, w26
.L3534:
	strb	w4, [x27,56]
.L3521:
	ldrb	w1, [x27,57]
	sub	w20, w20, #1
	ldr	x0, [x27,8]
	ldrb	w2, [x27,56]
	add	x0, x0, x1, lsl 9
	mov	x1, x25
	lsl	w2, w2, 9
	bl	ftl_memcpy
	str	w21, [x27,36]
	add	x0, x19, :lo12:.LANCHOR0
	add	w21, w21, 1
	ldr	x1, [x0,2864]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x27,32]
	mov	x0, x27
	bl	ftl_write_buf
	ldrb	w0, [x27,56]
	add	x25, x25, x0, lsl 9
	b	.L3517
.L3536:
	bl	ftl_write_commit
	mov	w0, w20
.L3535:
	mov	w1, 1
	bl	zftl_do_gc
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,2844]
	ldrh	w0, [x1,2848]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L3537
	mov	w0, 0
	b	.L3535
.L3537:
	bl	timer_get_time
	adrp	x1, .LANCHOR7-76
	str	w0, [x1,#:lo12:.LANCHOR7-76]
	mov	w0, 0
.L3516:
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
	bhi	.L3541
	adrp	x23, .LANCHOR7
	add	w24, w0, 24576
	add	x0, x23, :lo12:.LANCHOR7
	adrp	x25, .LANCHOR2
	ldr	w1, [x0,-72]
	add	w1, w19, w1
	str	w1, [x0,-72]
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3542
	adrp	x0, .LC283
	mov	w2, w24
	add	x0, x0, :lo12:.LC283
	mov	w3, w19
	mov	w4, 0
	bl	printk
.L3542:
	add	x20, x21, :lo12:.LANCHOR0
	ldr	x0, [x20,2864]
	ldr	w26, [x0,8]
	add	w1, w26, 1
	str	w1, [x0,8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	w22, [x20,2832]
	udiv	w20, w24, w22
	msub	w27, w20, w22, w24
	cbz	w27, .L3543
	sub	w22, w22, w27
	mov	w0, w20
	cmp	w22, w19
	csel	w22, w22, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3544
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3544:
	ldr	w0, [x29,120]
	and	w28, w22, 65535
	cmn	w0, #1
	beq	.L3546
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3546
	strb	w27, [x0,57]
	ubfiz	x27, x27, 9, 25
	str	w20, [x0,36]
	mov	w1, 0
	strb	w22, [x0,56]
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
	ldr	x1, [x0,2864]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3546:
	add	w20, w20, 1
	sub	w19, w19, w28
.L3543:
	cbz	w19, .L3548
	bl	ftl_flush
.L3548:
	mov	w0, -1
	mov	w27, 1
	str	w0, [x29,124]
	mov	w28, 21
.L3549:
	add	x22, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,2832]
	cmp	w19, w0
	bcc	.L3588
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L3550
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3552
	ldrb	w2, [x22,2832]
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
	b	.L3587
.L3550:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3552
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	ldrh	w0, [x22,3340]
	ldr	w1, [x29,120]
	sub	w2, w28, w0
	lsl	w2, w27, w2
	lsr	w0, w1, w0
	sub	w2, w2, #1
	ldrb	w1, [x22,3338]
	and	w0, w2, w0
	udiv	w0, w0, w1
	bl	ftl_vpn_decrement
.L3587:
	ldr	x1, [x22,2864]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3552:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,2832]
	sub	w19, w19, w0
	b	.L3549
.L3588:
	cbz	w19, .L3556
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3557
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3557:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3556
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L3556
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w26, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w19, w0
	bcc	.L3559
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1236
	add	x1, x1, 816
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3559:
	ldr	x0, [x22,8]
	mov	w1, 0
	lsl	w2, w19, 9
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2864]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3556:
	add	x0, x23, :lo12:.LANCHOR7
	mov	w4, 0
	ldr	w1, [x0,-72]
	cmp	w1, 8192
	bls	.L3541
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3560
	adrp	x0, .LC283
	mov	w2, w24
	add	x0, x0, :lo12:.LC283
	mov	w3, w19
	bl	printk
.L3560:
	add	x23, x23, :lo12:.LANCHOR7
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x23,-72]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x21,2884]
.L3541:
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
	adrp	x20, .LANCHOR4
	uxth	w19, w2
	ldr	x0, [x0,624]
	ldrb	w2, [x0,19]
	add	x0, x20, :lo12:.LANCHOR4
	ldrb	w0, [x0,1264]
	cbnz	w0, .L3589
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L3591
	cmp	w2, 68
	beq	.L3591
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L3589
.L3591:
	adrp	x24, .LC284
	mov	w25, w1
	add	x24, x24, :lo12:.LC284
	add	x20, x20, :lo12:.LANCHOR4
	mov	w27, 65535
.L3593:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,624]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L3589
	add	x22, x20, 68
	add	w26, w19, w25
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	ldrh	w3, [x22,w19,sxtw 1]
	bl	printk
	ldrh	w0, [x22,w19,sxtw 1]
	cmp	w0, w27
	bne	.L3589
	ldr	x0, [x20,1192]
	mov	w1, 255
	mov	w2, 16384
	add	w19, w19, 1
	bl	ftl_memset
	uxth	w19, w19
	ldr	x4, [x28,624]
	mov	w0, w23
	ldr	x2, [x20,1192]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L3593
.L3589:
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
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x25, x1
	str	x2, [x29,128]
	ldr	x0, [x0,624]
	ldrb	w21, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x23, x0
	mov	w0, -1
	cbz	x23, .L3641
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3626
	cmp	w19, 500
	bhi	.L3641
	b	.L3597
.L3626:
	mov	w19, 8
.L3597:
	ldr	w2, [x25]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	mov	w0, -1
	cmp	w2, w1
	bne	.L3641
	mul	w20, w20, w21
	add	x3, x25, 253952
	mov	w2, 63999
	add	x3, x3, 2044
	mov	w4, 4097
	uxth	w0, w20
	str	w0, [x29,156]
	sub	w0, w0, #1
	ldr	w1, [x29,156]
	add	w0, w0, w19
	udiv	w0, w0, w1
	str	w0, [x29,120]
	mov	w0, 0
.L3601:
	ldr	w1, [x3]
	cbnz	w1, .L3598
	ldr	w1, [x25,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w4
	sub	w2, w2, #1
	csel	w0, w0, wzr, cc
	str	w1, [x3],-4
	cmp	w2, 4096
	bne	.L3601
	b	.L3600
.L3598:
	adrp	x0, .LC285
	add	x0, x0, :lo12:.LC285
	bl	printk
.L3600:
	ldr	x1, [x29,128]
	adrp	x0, .LC286
	add	x0, x0, :lo12:.LC286
	mov	w2, 4
	mov	w3, 5
	mov	x21, 0
	mov	w28, w21
	bl	rknand_print_hex
	adrp	x0, .LC287
	mov	w1, w19
	add	x0, x0, :lo12:.LC287
	mov	w2, w19
	bl	printk
.L3623:
	adrp	x1, .LANCHOR7
	add	x1, x1, :lo12:.LANCHOR7
	ldrb	w1, [x1,-102]
	cbnz	w1, .L3602
	ldr	x1, [x29,128]
	ldr	w2, [x29,156]
	ldr	w1, [x1,x21,lsl 2]
	mul	w20, w1, w2
	cbz	w21, .L3603
	ldr	w2, [x29,120]
	cmp	w2, 1
	bls	.L3603
	sub	w0, w21, #1
	ldr	x2, [x29,128]
	ldr	w0, [x2,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L3603
	b	.L3604
.L3602:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,728]
	add	x0, x0, x21, uxtw
	ldrb	w20, [x0,32]
	cmp	w20, 255
	beq	.L3604
	ldr	w0, [x29,156]
	mul	w20, w20, w0
.L3603:
	mov	w2, 512
	mov	w1, 0
	mov	x0, x23
	bl	ftl_memset
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0,624]
	ldrb	w26, [x1,9]
	ldrh	w24, [x1,10]
	ldrb	w1, [x0,720]
	str	w1, [x29,116]
	udiv	w1, w20, w26
	strb	wzr, [x0,720]
	mul	w24, w26, w24
	mov	w0, 0
	uxth	w24, w24
	bl	flash_erase_block
	cmp	w19, w24
	mov	w2, 1
	bls	.L3605
	mov	w0, 0
	add	w1, w20, w24
	bl	flash_erase_block
	mov	w2, 2
.L3605:
	add	x27, x22, :lo12:.LANCHOR0
	ldr	x1, [x27,624]
	ldrh	w0, [x1,10]
	ldrb	w1, [x1,12]
	lsl	w0, w0, 2
	mul	w0, w0, w2
	sdiv	w0, w0, w1
	str	w0, [x29,112]
	udiv	w0, w20, w24
	msub	w0, w0, w24, w20
	str	w0, [x29,124]
	sub	w0, w20, w0
	str	w0, [x29,152]
	cmp	w20, w0
	bne	.L3631
	ldrb	w0, [x27,516]
	cmp	w0, 9
	bne	.L3631
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 1024
	ldr	x3, [x0,1248]
	str	x3, [x29,144]
	mov	x0, x3
	bl	ftl_memset
	ldr	x3, [x29,144]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	mov	w1, 12
	cmp	w26, 8
	str	w0, [x3]
	str	w1, [x3,4]
	str	wzr, [x3,12]
	ldr	x2, [x27,624]
	ldrb	w0, [x2,29]
	strb	w0, [x3,16]
	mov	w0, 4
	strb	w0, [x3,17]
	ldrh	w0, [x2,10]
	ldrb	w2, [x2,12]
	strb	wzr, [x3,20]
	strh	wzr, [x3,22]
	sdiv	w0, w0, w2
	mov	w2, 16
	strh	w0, [x3,18]
	mov	w0, 70
	csel	w0, w0, w2, hi
	strb	w0, [x3,21]
	add	x0, x3, 12
	bl	js_hash
	ldr	x3, [x29,144]
	str	w0, [x3,8]
	sub	w0, w19, #4
	str	w0, [x29,144]
	b	.L3606
.L3631:
	str	w19, [x29,144]
	mov	x3, 0
.L3606:
	str	x25, [x29,136]
	mov	w24, 0
	add	x27, x22, :lo12:.LANCHOR0
.L3608:
	ldr	w0, [x29,112]
	cmp	w24, w0
	bcs	.L3650
	ldr	w0, [x29,124]
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w24, w0
	add	x0, x27, 4
	ubfx	x4, x4, 2, 16
	add	w1, w4, 1
	ldrb	w2, [x2,1264]
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L3610
	ldrb	w6, [x27,1]
	lsl	w0, w1, 1
	cmp	w6, wzr
	csel	w0, w0, w1, ne
.L3610:
	ldrb	w1, [x27,516]
	cmp	w1, 9
	beq	.L3649
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L3649:
	str	w0, [x29,160]
	mov	w0, 61424
	str	w0, [x29,164]
	add	x0, x27, 4
	ldrh	w0, [x0,w4,sxtw 1]
	cbnz	w2, .L3614
	mov	w4, w0
	b	.L3615
.L3614:
	ldrb	w1, [x27,1]
	lsl	w0, w4, 1
	cmp	w1, wzr
	csel	w4, w0, w4, ne
.L3615:
	mul	w0, w4, w26
	cbnz	x3, .L3616
	ldr	w1, [x29,152]
	add	x2, x29, 160
	str	x4, [x29,104]
	add	w0, w0, w1
	ldr	x1, [x29,136]
	bl	fw_flash_page_prog.constprop.30
	adrp	x0, .LANCHOR4
	ldr	x4, [x29,104]
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1264]
	cbnz	w0, .L3617
	ldr	w1, [x29,152]
	add	w2, w4, 1
	udiv	w1, w1, w26
	bl	id_block_prog_msb_ff_data
.L3617:
	ldr	x0, [x29,136]
	add	x0, x0, 2048
	str	x0, [x29,136]
	b	.L3618
.L3616:
	ldr	w1, [x29,152]
	add	x2, x29, 160
	add	w0, w0, w1
	mov	x1, x3
	bl	fw_flash_page_prog.constprop.30
.L3618:
	add	w24, w24, 4
	mov	x3, 0
	uxth	w24, w24
	b	.L3608
.L3650:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x29,116]
	mov	x2, x23
	strb	w1, [x0,720]
	mov	w0, w20
	ldr	w1, [x29,144]
	bl	id_block_read_data.constprop.31
	ldr	w0, [x29,144]
	mov	x1, 0
	lsl	w0, w0, 7
.L3620:
	cmp	w0, w1
	bls	.L3651
	ldr	w3, [x23,x1,lsl 2]
	add	x1, x1, 1
	add	x2, x25, x1, lsl 2
	ldr	w2, [x2,-4]
	cmp	w3, w2
	beq	.L3620
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L3604
.L3651:
	add	w28, w28, 1
	cmp	w28, 5
	bhi	.L3624
.L3604:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L3623
.L3624:
	mov	x0, x23
	bl	ftl_free
	cmp	w28, wzr
	csetm	w0, eq
.L3641:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	mov	x2, 0
	adrp	x3, .LANCHOR2
	mov	x5, x0
	add	x3, x3, :lo12:.LANCHOR2
	mov	w0, w2
.L3653:
	cmp	w1, w2
	bls	.L3655
	ldrb	w4, [x5,x2]
	add	x6, x3, 2488
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3653
.L3655:
	ret
	.size	CRC_32, .-CRC_32
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	stp	x29, x30, [sp, -96]!
	cmp	w0, 64
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	mov	w19, w0
	mov	w21, w1
	mov	x23, x2
	adrp	x20, .LANCHOR7
	bne	.L3657
	ldr	w1, [x2]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L3657
	add	x22, x20, :lo12:.LANCHOR7
	mov	w0, 1
	strb	w0, [x22,-68]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	str	x0, [x22,-64]
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	bl	ftl_memset
	str	w19, [x22,-56]
.L3657:
	add	x3, x20, :lo12:.LANCHOR7
	ldrb	w0, [x3,-68]
	cbz	w0, .L3656
	sub	w0, w19, #64
	ldr	x22, [x3,-64]
	cmp	w0, 499
	bhi	.L3659
	mov	w3, 564
	ubfiz	x0, x0, 9, 25
	sub	w3, w3, w19
	add	x0, x22, x0
	cmp	w3, w21
	mov	x1, x23
	csel	w2, w3, w21, ls
	lsl	w2, w2, 9
	bl	ftl_memcpy
	b	.L3660
.L3659:
	cmp	w19, 563
	bhi	.L3670
.L3660:
	add	x0, x20, :lo12:.LANCHOR7
	ldr	w1, [x0,-56]
	cmp	w1, w19
	beq	.L3668
	strb	wzr, [x0,-68]
	cbz	x22, .L3669
	mov	x0, x22
	bl	ftl_free
.L3669:
	add	x0, x20, :lo12:.LANCHOR7
	str	xzr, [x0,-64]
.L3668:
	add	x20, x20, :lo12:.LANCHOR7
	add	w19, w19, w21
	str	w19, [x20,-56]
	b	.L3656
.L3670:
	ldr	w2, [x3,-56]
	mov	w0, 500
	sub	w2, w2, #64
	cmp	w2, 500
	csel	w2, w2, w0, ls
	adrp	x0, .LANCHOR0+624
	ldr	x0, [x0,#:lo12:.LANCHOR0+624]
	ldrb	w0, [x0,9]
	cmp	w0, 4
	beq	.L3671
	mov	w0, 2
	str	w0, [x29,64]
	mov	w0, 3
	str	w0, [x29,68]
	mov	w0, 4
	str	w0, [x29,72]
	mov	w0, 5
	str	w0, [x29,76]
	mov	w0, 6
	str	w0, [x29,80]
	b	.L3662
.L3671:
	mov	x3, 0
.L3661:
	cmp	w2, 256
	add	x0, x29, 64
	bls	.L3663
	lsl	w1, w3, 1
	str	w1, [x0,x3,lsl 2]
	b	.L3664
.L3663:
	str	w3, [x0,x3,lsl 2]
.L3664:
	add	x3, x3, 1
	cmp	x3, 5
	bne	.L3661
.L3662:
	add	x0, x20, :lo12:.LANCHOR7
	mov	w4, 63999
	ldr	x3, [x0,-48]
	add	x3, x3, 61440
	add	x3, x3, 2559
.L3667:
	ldrb	w0, [x3]
	cbz	w0, .L3665
	add	w2, w4, 1
	lsl	w0, w2, 2
	b	.L3666
.L3665:
	sub	w4, w4, #1
	sub	x3, x3, #1
	cmp	w4, 4096
	bne	.L3667
	lsl	w0, w2, 9
.L3666:
	mov	x1, x22
	add	x2, x29, 64
	add	x22, x20, :lo12:.LANCHOR7
	bl	write_idblock
	ldr	x0, [x22,-64]
	strb	wzr, [x22,-68]
	bl	ftl_free
	str	xzr, [x22,-64]
	b	.L3668
.L3656:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 96
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
	bhi	.L3681
	cbnz	w21, .L3681
	mov	w0, w1
	mov	w1, w2
	mov	x2, x3
	str	x3, [x29,56]
	bl	write_loader_lba
	ldr	x3, [x29,56]
.L3681:
	adrp	x0, .LANCHOR4+1176
	mov	w1, w19
	mov	w2, w20
	ldr	x0, [x0,#:lo12:.LANCHOR4+1176]
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
	beq	.L3690
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3691
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3692
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3693
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3694
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3695
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3752
	bl	rknand_dev_flush
	b	.L3786
.L3695:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3697
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3698
	b	.L3752
.L3693:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3699
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3700
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3701
	b	.L3752
.L3700:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3699
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3699
	b	.L3752
.L3691:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3702
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3703
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3704
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3705
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3706
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3707
	b	.L3752
.L3703:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3708
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3709
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3710
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3711
	b	.L3752
.L3709:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3711
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3708
	b	.L3752
.L3705:
	adrp	x0, .LC288
	add	x0, x0, :lo12:.LC288
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3712
.L3717:
	mov	x0, -12
	b	.L3689
.L3712:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3793
	adrp	x0, .LC290
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC290
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3790
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	id_block_read_data.constprop.31
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L3796
	adrp	x0, .LC291
	add	x0, x0, :lo12:.LC291
.L3788:
	bl	printk
.L3790:
	mov	x0, x19
.L3791:
	bl	ftl_free
.L3789:
	mov	x0, -14
	b	.L3689
.L3704:
	adrp	x0, .LC292
	add	x0, x0, :lo12:.LC292
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3717
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3793
	adrp	x0, .LC293
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC293
	bl	printk
	adrp	x0, .LANCHOR7
	add	x22, x0, :lo12:.LANCHOR7
	mov	x20, x0
	ldr	x21, [x22,-48]
	cbnz	x21, .L3719
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,-48]
	cbz	x0, .L3790
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L3719:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3790
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3790
	add	x20, x20, :lo12:.LANCHOR7
	uxtw	x2, w2
	ldr	x1, [x20,-48]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3796:
	mov	x0, x19
	bl	ftl_free
	b	.L3786
.L3707:
	adrp	x0, .LC294
	add	x0, x0, :lo12:.LC294
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3717
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3722
.L3793:
	adrp	x0, .LC289
	add	x0, x0, :lo12:.LC289
	b	.L3788
.L3722:
	ldr	w1, [x19]
	adrp	x0, .LC295
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC295
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3790
	adrp	x20, .LANCHOR7
	add	x20, x20, :lo12:.LANCHOR7
	ldr	x0, [x20,-48]
	cbz	x0, .L3790
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3725
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3689
.L3725:
	bl	rknand_device_lock
	ldr	x1, [x20,-48]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,-48]
	bl	ftl_free
	str	xzr, [x20,-48]
	b	.L3796
.L3706:
	adrp	x0, .LC296
	add	x0, x0, :lo12:.LC296
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3717
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3798
.L3690:
	adrp	x0, .LC297
	add	x0, x0, :lo12:.LC297
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3717
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3798
.L3702:
	adrp	x0, .LC298
	add	x0, x0, :lo12:.LC298
	bl	printk
	adrp	x0, .LANCHOR7-40
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR7-40]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3781:
	bl	rk_copy_to_user
	cbnz	x0, .L3789
	b	.L3786
.L3710:
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3717
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
.L3798:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3791
	bl	ftl_free
	b	.L3716
.L3694:
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3730
.L3735:
	adrp	x0, .LC289
	add	x0, x0, :lo12:.LC289
	bl	printk
	b	.L3789
.L3730:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3731
.L3732:
	mov	x19, -1
	b	.L3716
.L3731:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3732
	adrp	x21, .LANCHOR7
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR7
	mov	x2, 512
	ldr	x1, [x21,-40]
	bl	memcpy
	ldr	w1, [x21,-32]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3733
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3733:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3795
.L3697:
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3732
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3732
	adrp	x1, .LANCHOR7
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR7
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,-32]
	cmp	w3, w2
	bne	.L3689
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3689
	ldr	x19, [x1,-40]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3787
.L3701:
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3732
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3732
	adrp	x19, .LANCHOR7
	add	x0, x19, :lo12:.LANCHOR7
	ldr	w1, [x0,-28]
	cbnz	w1, .L3736
.L3739:
	mov	x0, 0
	b	.L3689
.L3736:
	ldr	x1, [x0,-24]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3737
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,-24]
	str	w2, [x1,4]
	ldr	x0, [x0,-24]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3737:
	add	x20, x19, :lo12:.LANCHOR7
	mov	w0, 0
	ldr	x1, [x20,-24]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,-40]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3738
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,-40]
	str	w1, [x0,4]
	ldr	x0, [x20,-40]
	str	wzr, [x0,8]
.L3738:
	add	x19, x19, :lo12:.LANCHOR7
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,-40]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,-28]
	str	wzr, [x19,-32]
	b	.L3786
.L3692:
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3732
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3732
	adrp	x19, .LANCHOR7
	add	x1, x19, :lo12:.LANCHOR7
	ldr	w2, [x1,-28]
	cmp	w2, 1
	beq	.L3739
	ldr	x2, [x1,-24]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3740
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,-24]
	str	w3, [x2,4]
	ldr	x1, [x1,-24]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3740:
	add	x20, x19, :lo12:.LANCHOR7
	mov	w0, 1
	ldr	x1, [x20,-24]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,-40]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3741
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,-40]
	str	w1, [x0,4]
	ldr	x0, [x20,-40]
	str	wzr, [x0,8]
.L3741:
	add	x19, x19, :lo12:.LANCHOR7
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,-40]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,-28]
	b	.L3786
.L3698:
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3732
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3732
	adrp	x1, .LANCHOR7
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR7
	uxtw	x2, w2
	sub	x1, x1, #16
	b	.L3794
.L3699:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3742
	adrp	x0, .LC305
	add	x0, x0, :lo12:.LC305
	b	.L3784
.L3742:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3744
	adrp	x0, .LC306
	add	x0, x0, :lo12:.LC306
	b	.L3784
.L3744:
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
.L3784:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3789
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR7
	bne	.L3745
	add	x0, x0, :lo12:.LANCHOR7
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,-40]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3689
	b	.L3789
.L3745:
	add	x20, x0, :lo12:.LANCHOR7
	ldr	w1, [x20,496]
	cmp	w1, 10
	bhi	.L3789
	ldr	x1, [x20,-40]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3746
	cbz	w3, .L3746
	adrp	x0, .LC308
	mov	w1, w2
	add	x0, x0, :lo12:.LC308
	bl	printk
	ldr	w0, [x20,496]
	add	w0, w0, 1
	str	w0, [x20,496]
	b	.L3789
.L3746:
	add	x0, x0, :lo12:.LANCHOR7
	str	wzr, [x0,496]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3747
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3748
.L3747:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3748:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3786
	b	.L3716
.L3711:
	adrp	x0, .LC309
	add	x0, x0, :lo12:.LC309
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3732
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3732
	mov	w0, 30224
	adrp	x1, .LANCHOR7
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR7
	add	x0, x29, 64
	bne	.L3749
	ldr	x1, [x1,504]
	b	.L3785
.L3749:
	ldr	x1, [x1,512]
.L3785:
	add	x1, x1, 8
.L3794:
	bl	memcpy
.L3795:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3781
.L3708:
	adrp	x0, .LC310
	add	x0, x0, :lo12:.LC310
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3735
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3732
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3732
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR7
	add	x19, x19, :lo12:.LANCHOR7
	bne	.L3751
	ldr	x0, [x19,504]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,504]
	b	.L3787
.L3751:
	ldr	x0, [x19,512]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,512]
	mov	w0, 3
.L3787:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3716
.L3786:
	mov	x19, 0
.L3716:
	adrp	x0, .LC311
	mov	x1, x19
	add	x0, x0, :lo12:.LC311
	bl	printk
	mov	x0, x19
	b	.L3689
.L3752:
	mov	x0, -22
.L3689:
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
	adrp	x20, .LANCHOR7
	add	x19, x20, :lo12:.LANCHOR7
	stp	x21, x22, [sp,32]
	ldr	x1, [x19,-96]
	str	w0, [x19,-56]
	add	x0, x1, 512
	str	x0, [x19,-40]
	add	x0, x1, 1024
	str	x1, [x19,-24]
	str	x0, [x19,504]
	add	x1, x1, 1536
	sub	x0, x19, #16
	strb	wzr, [x19,-68]
	str	xzr, [x19,-64]
	str	xzr, [x19,-48]
	str	x1, [x19,512]
	bl	ftl_memcpy
	ldr	x21, [x19,-24]
	str	wzr, [x19,-32]
	str	wzr, [x19,496]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,-28]
	cbz	w22, .L3801
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3801
	adrp	x0, .LC312
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC312
	str	wzr, [x19,-28]
	bl	printk
.L3801:
	add	x0, x20, :lo12:.LANCHOR7
	ldr	w1, [x0,-28]
	cbz	w1, .L3802
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,-32]
.L3802:
	add	x20, x20, :lo12:.LANCHOR7
	mov	w0, 2
	ldr	x1, [x20,504]
	bl	StorageSysDataLoad
	ldr	x1, [x20,512]
	mov	w0, 3
	bl	StorageSysDataLoad
	bl	rknand_sys_storage_init
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	adrp	x21, .LANCHOR7
	bl	ftl_malloc
	add	x1, x21, :lo12:.LANCHOR7
	mov	w19, -12
	str	x0, [x1,520]
	cbz	x0, .L3811
	mov	w23, 0
	adrp	x24, .LC313
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC313
.L3815:
	add	x20, x21, :lo12:.LANCHOR7
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,520]
	bl	FlashBootVendorRead
	cbnz	w0, .L3812
	ldr	x3, [x20,520]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,520]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3813
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3813
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3813:
	cbnz	w22, .L3823
	mov	w22, 1
	b	.L3815
.L3823:
	cbz	w19, .L3816
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3811
	b	.L3812
.L3816:
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
	b	.L3811
.L3812:
	add	x21, x21, :lo12:.LANCHOR7
	mov	w19, -1
	ldr	x0, [x21,520]
	bl	kfree
	str	xzr, [x21,520]
.L3811:
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
	adrp	x3, .LANCHOR7+520
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR7+520]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3825
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3826:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3830
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3826
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
	b	.L3825
.L3830:
	mov	w3, -1
.L3825:
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
	adrp	x0, .LANCHOR7+520
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR7+520]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3832
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3833:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3848
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3833
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3834
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3832
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3835:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3849
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
	b	.L3835
.L3849:
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
	b	.L3847
.L3834:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3847
.L3848:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3832
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
.L3847:
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
.L3832:
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
	cbz	x0, .L3851
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3853
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3854
	b	.L3865
.L3853:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3865
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3856
.L3857:
	mov	x20, -1
	b	.L3852
.L3856:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3857
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3852
.L3854:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3865
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3857
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3857
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3865
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3852
.L3865:
	mov	x20, -14
.L3852:
	mov	x0, x19
	bl	kfree
.L3851:
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
	.global	g_maxRetryCount
	.global	g_maxRegNum
	.global	g_retryMode
	.global	g_flash_toggle_mode_en
	.global	g_flash_3d_mlc_flag
	.global	g_flash_3d_tlc_flag
	.global	g_flash_multi_page_prog_en
	.global	g_flash_multi_page_read_en
	.global	g_flash_interface_mode
	.global	g_idb_ecc_bits
	.global	g_idb_slc_mode_enable
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
	.type	__func__.34004, %object
	.size	__func__.34004, 18
__func__.34004:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.34029, %object
	.size	__func__.34029, 23
__func__.34029:
	.string	"_list_update_data_list"
	.zero	1
	.type	__func__.19018, %object
	.size	__func__.19018, 22
__func__.19018:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.19104, %object
	.size	__func__.19104, 11
__func__.19104:
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
	.type	__func__.18606, %object
	.size	__func__.18606, 28
__func__.18606:
	.string	"flash_wait_device_ready_raw"
	.zero	4
	.type	__func__.18659, %object
	.size	__func__.18659, 22
__func__.18659:
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
	.type	__func__.18684, %object
	.size	__func__.18684, 23
__func__.18684:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.18581, %object
	.size	__func__.18581, 26
__func__.18581:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.18592, %object
	.size	__func__.18592, 21
__func__.18592:
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
	.type	__func__.19283, %object
	.size	__func__.19283, 13
__func__.19283:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.19296, %object
	.size	__func__.19296, 10
__func__.19296:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.19310, %object
	.size	__func__.19310, 16
__func__.19310:
	.string	"buf_remove_free"
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 216
zftl_debug_proc_fops:
	.xword	0
	.xword	seq_lseek
	.xword	seq_read
	.xword	zftl_debug_proc_write
	.zero	56
	.xword	zftl_debug_proc_open
	.zero	8
	.xword	single_release
	.zero	104
	.type	__func__.33599, %object
	.size	__func__.33599, 12
__func__.33599:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.33667, %object
	.size	__func__.33667, 19
__func__.33667:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.33718, %object
	.size	__func__.33718, 16
__func__.33718:
	.string	"gc_free_src_blk"
	.type	__func__.34210, %object
	.size	__func__.34210, 18
__func__.34210:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.34220, %object
	.size	__func__.34220, 17
__func__.34220:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.34324, %object
	.size	__func__.34324, 23
__func__.34324:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.34363, %object
	.size	__func__.34363, 22
__func__.34363:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.34374, %object
	.size	__func__.34374, 22
__func__.34374:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.33381, %object
	.size	__func__.33381, 14
__func__.33381:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.33425, %object
	.size	__func__.33425, 18
__func__.33425:
	.string	"zftl_add_read_buf"
	.zero	6
	.type	__func__.34169, %object
	.size	__func__.34169, 15
__func__.34169:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.34721, %object
	.size	__func__.34721, 17
__func__.34721:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.34749, %object
	.size	__func__.34749, 21
__func__.34749:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.19008, %object
	.size	__func__.19008, 20
__func__.19008:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.33316, %object
	.size	__func__.33316, 17
__func__.33316:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.34685, %object
	.size	__func__.34685, 13
__func__.34685:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.19333, %object
	.size	__func__.19333, 21
__func__.19333:
	.string	"flash_info_data_init"
	.zero	3
	.type	__func__.18537, %object
	.size	__func__.18537, 19
__func__.18537:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.19322, %object
	.size	__func__.19322, 20
__func__.19322:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.18672, %object
	.size	__func__.18672, 31
__func__.18672:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.18697, %object
	.size	__func__.18697, 25
__func__.18697:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.34552, %object
	.size	__func__.34552, 31
__func__.34552:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.34632, %object
	.size	__func__.34632, 15
__func__.34632:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.18648, %object
	.size	__func__.18648, 22
__func__.18648:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.34605, %object
	.size	__func__.34605, 15
__func__.34605:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.18630, %object
	.size	__func__.18630, 26
__func__.18630:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.18566, %object
	.size	__func__.18566, 19
__func__.18566:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.34435, %object
	.size	__func__.34435, 14
__func__.34435:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.34137, %object
	.size	__func__.34137, 15
__func__.34137:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.34399, %object
	.size	__func__.34399, 19
__func__.34399:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.34411, %object
	.size	__func__.34411, 18
__func__.34411:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.34298, %object
	.size	__func__.34298, 23
__func__.34298:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.34731, %object
	.size	__func__.34731, 14
__func__.34731:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.19355, %object
	.size	__func__.19355, 17
__func__.19355:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.19063, %object
	.size	__func__.19063, 16
__func__.19063:
	.string	"nand_flash_init"
	.type	__func__.34279, %object
	.size	__func__.34279, 16
__func__.34279:
	.string	"ftl_sysblk_dump"
	.type	__func__.34671, %object
	.size	__func__.34671, 16
__func__.34671:
	.string	"load_l2p_region"
	.type	__func__.34705, %object
	.size	__func__.34705, 6
__func__.34705:
	.string	"pm_gc"
	.zero	2
	.type	__func__.33654, %object
	.size	__func__.33654, 22
__func__.33654:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.33762, %object
	.size	__func__.33762, 16
__func__.33762:
	.string	"gc_scan_src_blk"
	.type	__func__.33820, %object
	.size	__func__.33820, 20
__func__.33820:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.33883, %object
	.size	__func__.33883, 18
__func__.33883:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.34255, %object
	.size	__func__.34255, 14
__func__.34255:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.33458, %object
	.size	__func__.33458, 10
__func__.33458:
	.string	"zftl_read"
	.zero	6
	.type	__func__.33620, %object
	.size	__func__.33620, 21
__func__.33620:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.34345, %object
	.size	__func__.34345, 19
__func__.34345:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.33363, %object
	.size	__func__.33363, 17
__func__.33363:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.33696, %object
	.size	__func__.33696, 16
__func__.33696:
	.string	"gc_do_copy_back"
	.type	__func__.33911, %object
	.size	__func__.33911, 11
__func__.33911:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.33489, %object
	.size	__func__.33489, 13
__func__.33489:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.14 20181108"
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
	.string	"bad block test:%x %x\n"
.LC41:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC42:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC43:
	.string	"flash_erase_block %x %x %x\n"
.LC44:
	.string	"flash_erase_block %d block = %x status = %x\n"
.LC45:
	.string	"erase done: %x\n"
.LC46:
	.string	"flash_mask_bad_block %d %d\n"
.LC47:
	.string	"zftl_debug"
.LC48:
	.string	"FLASH ID: %x %x %x %x %x %x\n"
.LC49:
	.string	"density: %d MB\n"
.LC50:
	.string	"device density: %d MB\n"
.LC51:
	.string	"FTL INFO:\n"
.LC52:
	.string	"max_lpn = 0x%x\n"
.LC53:
	.string	"density = 0x%x\n"
.LC54:
	.string	"slc vpn = 0x%x\n"
.LC55:
	.string	"xlc vpn = 0x%x\n"
.LC56:
	.string	"free slc blk = 0x%x\n"
.LC57:
	.string	"free xlc blk = 0x%x\n"
.LC58:
	.string	"free mix blk = 0x%x\n"
.LC59:
	.string	"slc data blk = 0x%x\n"
.LC60:
	.string	"slc cache blk = 0x%x\n"
.LC61:
	.string	"xlc data blk = 0x%x\n"
.LC62:
	.string	"free buf = %d, %d, %d\n"
.LC63:
	.string	"bad blk = %d %d\n"
.LC64:
	.string	"TBW = %d MB\n"
.LC65:
	.string	"TBR = %d MB\n"
.LC66:
	.string	"POC = %d\n"
.LC67:
	.string	"PLC = %d\n"
.LC68:
	.string	"sys run time = %d S\n"
.LC69:
	.string	"slc mode = %d\n"
.LC70:
	.string	"prog err = %d\n"
.LC71:
	.string	"read err = %d\n"
.LC72:
	.string	"GC XLC page = %d\n"
.LC73:
	.string	"GC SLC page = %d\n"
.LC74:
	.string	"discard page = 0x%x\n"
.LC75:
	.string	"version = %d\n"
.LC76:
	.string	"acblk = 0x%x %d %d\n"
.LC77:
	.string	"tmblk = 0x%x %d %d\n"
.LC78:
	.string	"gcblk = 0x%x %d %d\n"
.LC79:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC80:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC81:
	.string	"gc free blk th = %d\n"
.LC82:
	.string	"gc vpn th = %d %d %d %d %d\n"
.LC83:
	.string	"swl blk = %x %x %x %x\n"
.LC84:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC85:
	.string	"gc_add_sblk = %d, %d, %d,last update:%d, %d\n"
.LC86:
	.string	"status: %x, ppa: %x\n"
.LC87:
	.string	"gc_write_completed: %x  %x  %x %x\n"
.LC88:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC89:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC90:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC91:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC92:
	.string	"swl_tlc_free_mini_ec_blk alloc sblk %x\n"
.LC93:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC94:
	.string	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\n"
.LC95:
	.string	"swl_slc_free_mini_ec_blk alloc sblk %x\n"
.LC96:
	.string	"list count:%p %d\n"
.LC97:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC98:
	.string	"swl_tlc_free_mini_ec_blk sblk %x\n"
.LC99:
	.string	"swl_slc_free_mini_ec_blk sblk %x\n"
.LC100:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC101:
	.string	"gc_free_src_blk %x, %d\n"
.LC102:
	.string	"ftl_vpn_decrement %x = %d, %d\n"
.LC103:
	.string	"mask bad block:cs %x block: %x\n"
.LC104:
	.string	"free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC105:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC106:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC107:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC108:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC109:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC110:
	.string	"alloc sblk %x %d\n"
.LC111:
	.string	"dump_sblk_queue: %d\n"
.LC112:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC113:
	.string	"blk %x is bad block\n"
.LC114:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC115:
	.string	"%s %d %d\n"
.LC116:
	.string	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC117:
	.string	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC118:
	.string	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC119:
	.string	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC120:
	.string	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC121:
	.string	"gc_static_wearleveling: slc blk: %x, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC122:
	.string	"gc_static_wearleveling: min slc ec: %x, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC123:
	.string	"swl add tlc gc = %x, %d, %d, %d, %d, %d\n"
.LC124:
	.string	"swl add slc gc  = %x, %d, %d, %d, %d, %d\n"
.LC125:
	.string	"free blk vpn error: %x %x\n"
.LC126:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC127:
	.string	"...%d @ %s\n"
.LC128:
	.string	"rk_ftl_de_init %x\n"
.LC129:
	.string	"%s %p:0x%x:"
.LC130:
	.string	"%x "
.LC131:
	.string	""
.LC132:
	.string	"otp error! %d"
.LC133:
	.string	"rr"
.LC134:
	.string	"flash_abort_clear = %d\n"
.LC135:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC136:
	.string	"nandc:"
.LC137:
	.string	"%d flReg.d32=%x %x\n"
.LC138:
	.string	"nandc_xfer_done read error %x\n"
.LC139:
	.string	"dqs data abort %x\n"
.LC140:
	.string	"dqs data timeout %x\n"
.LC141:
	.string	"xfer error %x\n"
.LC142:
	.string	"flash_read_page %x %x %x\n"
.LC143:
	.string	"micron %d row=%x,last status %d,status = %d\n"
.LC144:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC145:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC146:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC147:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC148:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC149:
	.string	"%d flash_ddr_tuning_read %x ecc=%d\n"
.LC150:
	.string	"sync para %d\n"
.LC151:
	.string	"DDR mode Read error %x %x\n"
.LC152:
	.string	"SDR mode Read %x %x ecc:%x\n"
.LC153:
	.string	"flash_read_page_en %x %x %x\n"
.LC154:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC155:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC156:
	.string	"...%d @ %s %d %p\n"
.LC157:
	.string	"no sys info %x\n"
.LC158:
	.string	"read page: %x %x %x %x\n"
.LC159:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC160:
	.string	"read: %x %x %x %x\n"
.LC161:
	.string	"0set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC162:
	.string	"1set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC163:
	.string	"flash_prog_page %x %x %x\n"
.LC164:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC165:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC166:
	.string	"flash_prog_page_en:%x %x\n"
.LC167:
	.string	"w d:"
.LC168:
	.string	"w s:"
.LC169:
	.string	"spare"
.LC170:
	.string	"data"
.LC171:
	.string	"write error: %x\n"
.LC172:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC173:
	.string	"%d %x @%d %x\n"
.LC174:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC175:
	.string	"%s %d %d %x %x\n"
.LC176:
	.string	"saved_active_page  = %x\n"
.LC177:
	.string	"saved_active_plane = %x\n"
.LC178:
	.string	"sblk = %x\n"
.LC179:
	.string	"phy_blk = %x %x\n"
.LC180:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC181:
	.string	"dump write = %x\n"
.LC182:
	.string	"pm_write_page write error: %x\n"
.LC183:
	.string	"finfo:"
.LC184:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC185:
	.string	"sys_info_flush error:%x\n"
.LC186:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC187:
	.string	"l2p:"
.LC188:
	.string	"free_buf_count: %d\n"
.LC189:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC190:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC191:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC192:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC193:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC194:
	.string	"lpa:"
.LC195:
	.string	"vpn:"
.LC196:
	.string	"sblk:"
.LC197:
	.string	"lpa_hash:"
.LC198:
	.string	"lpa_hash_index:"
.LC199:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC200:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC201:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC202:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC203:
	.string	"pm_ppa:"
.LC204:
	.string	"data:"
.LC205:
	.string	"spare:"
.LC206:
	.string	"pm_init posr %x %x %x\n"
.LC207:
	.string	"pm_init recovery %x %x %x\n"
.LC208:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC209:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC210:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC211:
	.string	"gc_recovery: %x vpn = %x\n"
.LC212:
	.string	"_c_user_data_density := %d\n"
.LC213:
	.string	"_c_totle_phy_density := %d\n"
.LC214:
	.string	"_c_totle_log_page := %d\n"
.LC215:
	.string	"_c_totle_data_density := %d\n"
.LC216:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC217:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC218:
	.string	"_c_max_pm_sblk := %d\n"
.LC219:
	.string	"_min_slc_super_block := %d\n"
.LC220:
	.string	"_max_xlc_super_block := %d\n"
.LC221:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC222:
	.string	"flash info size: %d %d %d\n"
.LC223:
	.string	"ftl_init %x\n"
.LC224:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC225:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC226:
	.string	"lpa: %x %x %x\n"
.LC227:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC228:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC229:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC230:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC231:
	.string	"ftl_sblk_dump = %x %d %d %d %d\n"
.LC232:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC233:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC234:
	.string	"index= %x, lpa=%x\n"
.LC235:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC236:
	.string	"ftl_read %x %x %x\n"
.LC237:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC238:
	.string	"id=%d, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC239:
	.string	"zftl debug cmd: %s\n"
.LC240:
	.string	"cmd:"
.LC241:
	.string	"dumpl2p"
.LC242:
	.string	"pm l2p:"
.LC243:
	.string	"pm blk:"
.LC244:
	.string	"dumppm:"
.LC245:
	.string	"p_cmd: %s\n"
.LC246:
	.string	"pm ram = %x, %x\n"
.LC247:
	.string	"ram:"
.LC248:
	.string	"pm:"
.LC249:
	.string	"dumpsys"
.LC250:
	.string	"dumplist:"
.LC251:
	.string	"vpncheck"
.LC252:
	.string	"dumpppa:"
.LC253:
	.string	"dumpblk:"
.LC254:
	.string	"setzdebug:"
.LC255:
	.string	"lpa2ppa:"
.LC256:
	.string	"lpa: %x--> ppa: %x\n"
.LC257:
	.string	"help:\n"
.LC258:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC259:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC260:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC261:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC262:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC263:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC264:
	.string	"7. echo dumplist > /proc/zftl_debug\n"
.LC265:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC266:
	.string	"gc_lpa:"
.LC267:
	.string	"gc_ppa:"
.LC268:
	.string	"err: ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC269:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC270:
	.string	"ftl_update_l2p_map"
.LC271:
	.string	"lpa_tbl:"
.LC272:
	.string	"sblk %x vpn: %d %d\n"
.LC273:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC274:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC275:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC276:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x\n"
.LC277:
	.string	"%d prog_step: %x %x buf id= %x ppa = %x hash=%x id = %x plane = %x lpa=%x\n"
.LC278:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC279:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC280:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC281:
	.string	"gc free %x, %d\n"
.LC282:
	.string	"ftlwrite %x %x %x\n"
.LC283:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC284:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC285:
	.string	"write_idblock fix data %x %x\n"
.LC286:
	.string	"idblk:"
.LC287:
	.string	"write_idblock totle_sec %x %x\n"
.LC288:
	.string	"READ_SECTOR_IO\n"
.LC289:
	.string	"rk_copy_from_user error\n"
.LC290:
	.string	"READ_SECTOR_IO %x %x\n"
.LC291:
	.string	"rk_copy_to_user error\n"
.LC292:
	.string	"WRITE_SECTOR_IO\n"
.LC293:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC294:
	.string	"END_WRITE_SECTOR_IO\n"
.LC295:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC296:
	.string	"GET_FLASH_INFO_IO\n"
.LC297:
	.string	"GET_BAD_BLOCK_IO\n"
.LC298:
	.string	"GET_LOCK_FLAG_IO\n"
.LC299:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC300:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC301:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC302:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC303:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC304:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC305:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC306:
	.string	"RKNAND_LOADER_STATUS\n"
.LC307:
	.string	"RKNAND_LOADER_LOCK\n"
.LC308:
	.string	"LockKey not match %d\n"
.LC309:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC310:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC311:
	.string	"return ret = %lx\n"
.LC312:
	.string	"secureBootEn check error\n"
.LC313:
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
	.size	zftl_nand_flash_para_tbl, 1152
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
	.byte	3
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
	.byte	60
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
	.byte	60
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
	.byte	2
	.byte	-65
	.byte	-66
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
	.byte	5
	.byte	-119
	.byte	100
	.byte	100
	.byte	60
	.byte	-95
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.hword	512
	.byte	2
	.byte	1
	.hword	1024
	.hword	455
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
	.byte	-119
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
	.hword	455
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
.LANCHOR4 = . + 4352
.LANCHOR5 = . + 21760
.LANCHOR6 = . + 34816
.LANCHOR7 = . + 39168
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
	.zero	6
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
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 4
g_flash_cur_mode:
	.zero	4
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
	.zero	2
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
	.size	g_gc_info, 328
g_gc_info:
	.zero	328
	.type	ftl_sblk_vpn_update_id, %object
	.size	ftl_sblk_vpn_update_id, 2
ftl_sblk_vpn_update_id:
	.zero	2
	.zero	6
	.type	ftl_sblk_update_list, %object
	.size	ftl_sblk_update_list, 16
ftl_sblk_update_list:
	.zero	16
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
	.zero	6
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
	.type	_c_ftl_planes_per_die, %object
	.size	_c_ftl_planes_per_die, 1
_c_ftl_planes_per_die:
	.zero	1
	.zero	1
	.type	_c_ftl_block_addr_log2, %object
	.size	_c_ftl_block_addr_log2, 2
_c_ftl_block_addr_log2:
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
	.type	g_flash_3d_mlc_flag, %object
	.size	g_flash_3d_mlc_flag, 1
g_flash_3d_mlc_flag:
	.zero	1
	.zero	1
	.type	_c_ftl_block_align_addr, %object
	.size	_c_ftl_block_align_addr, 2
_c_ftl_block_align_addr:
	.zero	2
	.type	_c_ftl_nand_die_num, %object
	.size	_c_ftl_nand_die_num, 1
_c_ftl_nand_die_num:
	.zero	1
	.zero	7
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
	.zero	2
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
	.type	g_flash_sys_spare_buffer, %object
	.size	g_flash_sys_spare_buffer, 8
g_flash_sys_spare_buffer:
	.zero	8
	.type	g_flash_blk_info, %object
	.size	g_flash_blk_info, 4
g_flash_blk_info:
	.zero	4
	.zero	4
	.type	g_flash_page_buffer, %object
	.size	g_flash_page_buffer, 8
g_flash_page_buffer:
	.zero	8
	.type	g_flash_spare_buffer, %object
	.size	g_flash_spare_buffer, 8
g_flash_spare_buffer:
	.zero	8
	.type	g_idb_slc_mode_enable, %object
	.size	g_idb_slc_mode_enable, 1
g_idb_slc_mode_enable:
	.zero	1
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
	.zero	1
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.zero	1
	.zero	1
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.zero	4
	.zero	8
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
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.zero	1
	.type	_c_ftl_byte_pre_page, %object
	.size	_c_ftl_byte_pre_page, 2
_c_ftl_byte_pre_page:
	.zero	2
	.type	pm_last_load_ram_id, %object
	.size	pm_last_load_ram_id, 1
pm_last_load_ram_id:
	.zero	1
	.zero	3
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
	.zero	5
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
	.zero	46
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
