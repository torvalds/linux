/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-06-15
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
	.type	rk_simple_strtoull.constprop.31, %function
rk_simple_strtoull.constprop.31:
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
	.size	rk_simple_strtoull.constprop.31, .-rk_simple_strtoull.constprop.31
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
	mov	w2, 599
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
	.global	nandc_wait_flash_ready
	.type	nandc_wait_flash_ready, %function
nandc_wait_flash_ready:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	movk	w19, 0x1, lsl 16
.L314:
	mov	w0, 100
	bl	timer_delay_ns
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L315
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
.L318:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,726]
	cmp	w4, w2
	bls	.L323
	ldrb	w3, [x3,725]
	cmp	w3, 67
	bne	.L319
	ldrsb	w3, [x0,x2]
	b	.L322
.L319:
	ldrsb	w3, [x1,x2]
.L322:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L318
.L323:
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
	add	x0, x0, 272
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
	ldrsb	w1, [x1,673]
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
	cbz	w2, .L327
	mov	w2, 18
	str	w2, [x19,4]
	mov	x2, 7
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 8
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
	b	.L328
.L327:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 344
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L328:
	bl	nandc_wait_flash_ready
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	zftl_flash_enter_slc_mode
	.type	zftl_flash_enter_slc_mode, %function
zftl_flash_enter_slc_mode:
	adrp	x2, .LANCHOR0
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x1, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	cbz	w2, .L329
	cmp	w2, 1
	ldr	x3, [x1,536]
	bne	.L332
	adrp	x1, .LANCHOR2+405
	ldrb	w2, [x1,#:lo12:.LANCHOR2+405]
	cbz	w2, .L329
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x3, x1, lsl 8
	str	w2, [x0,8]
	b	.L329
.L332:
	cmp	w2, 2
	bne	.L334
	add	x1, x1, x0
	ldrb	w2, [x1,744]
	cbz	w2, .L329
	strb	wzr, [x1,744]
	adrp	x1, .LANCHOR2+405
	ldrb	w1, [x1,#:lo12:.LANCHOR2+405]
	cbz	w1, .L329
	add	x0, x0, 8
	add	x0, x3, x0, lsl 8
	str	w1, [x0,8]
	b	.L329
.L334:
	cmp	w2, 3
	bne	.L329
	add	x1, x1, x0, sxtw
	ldrb	w2, [x1,744]
	cbz	w2, .L329
	strb	wzr, [x1,744]
	ubfiz	x1, x0, 8, 8
	add	x0, x3, x1
	mov	w1, 239
	str	w1, [x0,2056]
	mov	w1, 145
	str	w1, [x0,2052]
	mov	w1, 1
	str	wzr, [x0,2048]
	str	w1, [x0,2048]
	str	wzr, [x0,2048]
	str	wzr, [x0,2048]
	bl	nandc_wait_flash_ready
.L329:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	adrp	x2, .LANCHOR0
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x1, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	cbz	w2, .L347
	cmp	w2, 1
	ldr	x4, [x1,536]
	bne	.L350
	adrp	x1, .LANCHOR2+406
	ldrb	w2, [x1,#:lo12:.LANCHOR2+406]
	cbz	w2, .L347
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x4, x1, lsl 8
	str	w2, [x0,8]
	b	.L347
.L350:
	cmp	w2, 2
	bne	.L352
	add	x1, x1, x0
	ldrb	w2, [x1,744]
	cbnz	w2, .L347
	adrp	x2, .LANCHOR2
	mov	w5, 4
	add	x2, x2, :lo12:.LANCHOR2
	ldrb	w3, [x2,388]
	cmp	w3, 2
	csel	w3, w3, w5, eq
	strb	w3, [x1,744]
	ldrb	w1, [x2,406]
	cbz	w1, .L347
	add	x0, x0, 8
	add	x0, x4, x0, lsl 8
	str	w1, [x0,8]
	b	.L347
.L352:
	cmp	w2, 3
	bne	.L347
	add	x1, x1, x0, sxtw
	ldrb	w3, [x1,744]
	cbnz	w3, .L347
	adrp	x2, .LANCHOR2+388
	mov	w5, 4
	ldrb	w2, [x2,#:lo12:.LANCHOR2+388]
	cmp	w2, 2
	csel	w2, w2, w5, eq
	strb	w2, [x1,744]
	ubfiz	x1, x0, 8, 8
	add	x0, x4, x1
	mov	w1, 239
	str	w1, [x0,2056]
	mov	w1, 145
	str	w1, [x0,2052]
	mov	w1, 1
	str	w2, [x0,2048]
	str	w1, [x0,2048]
	str	w3, [x0,2048]
	str	w3, [x0,2048]
	bl	nandc_wait_flash_ready
.L347:
	ldp	x29, x30, [sp], 16
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
	bhi	.L366
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 741
	add	x1, x1, 688
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L366:
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x20, x19, sxtw
	ldrb	w19, [x19,736]
	mov	w0, w19
	ldr	x24, [x20,536]
	bl	nandc_cs
	cbnz	w22, .L367
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L368
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	b	.L368
.L367:
	ldr	x0, [x20,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L369
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x24, x0, lsl 8
	str	w22, [x0,8]
	b	.L368
.L369:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L368:
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
	bhi	.L374
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 836
	add	x1, x1, 712
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L374:
	add	x26, x22, :lo12:.LANCHOR0
	add	x19, x26, x19, sxtw
	ldrb	w24, [x19,736]
	mov	w0, w24
	ldr	x25, [x26,536]
	bl	nandc_cs
	cbnz	w20, .L375
	mov	w0, w23
	bl	slc_phy_page_address_calc
	mov	w23, w0
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	adrp	x0, .LANCHOR0
	ldrb	w0, [x0,#:lo12:.LANCHOR0]
	cbz	w0, .L376
	mov	w0, w24
	bl	zftl_flash_enter_slc_mode
	b	.L376
.L375:
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L377
	sxtw	x0, w24
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w20, [x0,8]
	b	.L376
.L377:
	mov	w0, w24
	bl	zftl_flash_exit_slc_mode
.L376:
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
	bne	.L378
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
	cbz	w20, .L379
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L379
	str	w20, [x19,2056]
.L379:
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	b	.L397
.L378:
	add	x19, x25, x19, lsl 8
	ldrb	w0, [x0,8]
	str	w0, [x19,2056]
	str	w2, [x19,2052]
	str	w1, [x19,2052]
	str	w23, [x19,2052]
	cbz	w20, .L381
	ldr	x0, [x26,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L381
	str	w20, [x19,2056]
.L381:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22,649]
	str	w0, [x19,2056]
.L397:
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
	adrp	x21, .LANCHOR0
	adrp	x22, .LC38
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC38
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	w24, w0
	mov	x20, 0
	add	x25, x21, 748
	adrp	x23, .LANCHOR2
.L408:
	lsl	x0, x20, 3
	ldr	x19, [x21,536]
	ldrb	w1, [x0,x25]
	cmp	w1, 44
	cset	w26, eq
	cmp	w1, 69
	cset	w2, eq
	cmp	w1, 152
	cset	w0, eq
	adrp	x1, .LANCHOR0
	orr	w0, w2, w0
	cbnz	w0, .L409
	cbz	w26, .L399
.L409:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w24, 1
	ldrb	w1, [x1,780]
	bne	.L401
	tbz	x1, 0, .L399
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L402
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	bl	printk
.L402:
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	mov	w0, 1
	cbz	w26, .L403
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L426
.L403:
	mov	w2, 128
	str	w2, [x1,2052]
.L426:
	str	w0, [x1,2048]
	b	.L407
.L401:
	tbz	x1, 2, .L399
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L405
	mov	x0, x22
	bl	printk
.L405:
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	cbz	w26, .L406
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L426
.L406:
	mov	w0, 128
	str	w0, [x1,2052]
	str	w26, [x1,2048]
.L407:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L399:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L408
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
	bhi	.L428
	adrp	x0, .LC39
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC39
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L428:
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
	adrp	x3, .LANCHOR2+385
	ldr	x19, [x4,#:lo12:.LANCHOR0+536]
	str	x2, [x29,40]
	add	x19, x19, x0
	ldrb	w3, [x3,#:lo12:.LANCHOR2+385]
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
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR0+536
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x2,#:lo12:.LANCHOR0+536]
	add	x2, x20, :lo12:.LANCHOR2
	str	x21, [sp,32]
	ldrb	w3, [x2,405]
	sxtw	x2, w0
	add	x2, x2, 8
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L445
	mov	w3, 162
.L445:
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
	beq	.L434
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L434
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	bl	printk
.L434:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,406]
	cbz	w0, .L435
	str	w0, [x19,2056]
.L435:
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
	adrp	x1, .LANCHOR0
	mov	w3, 1
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,516]
	strb	w0, [x1,724]
	cmp	w2, 9
	ldr	x2, [x1,536]
	bne	.L448
	str	w3, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L449
	cmp	w0, 60
	mov	w1, 3
	beq	.L449
	cmp	w0, 40
	mov	w1, 2
	csel	w1, w1, w3, eq
.L449:
	lsl	w1, w1, 25
	orr	w1, w1, 1
	str	w1, [x2,32]
	b	.L447
.L448:
	str	w3, [x2,8]
	cmp	w0, 16
	mov	w1, 4096
	bne	.L451
.L454:
	and	w1, w1, -17
	b	.L452
.L451:
	cmp	w0, 24
	bne	.L453
	orr	w1, w1, 16
	b	.L452
.L453:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L454
.L452:
	orr	w1, w1, 1
	str	w1, [x2,12]
.L447:
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
	bne	.L462
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
	b	.L463
.L462:
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
.L463:
	add	x22, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x23, x22, 748
	mov	w24, 2
.L465:
	lsl	x0, x19, 3
	ldrb	w0, [x0,x23]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L464
	uxtb	w21, w19
	mov	w0, w21
	bl	flash_reset
	add	x0, x22, 744
	strb	w24, [x19,x0]
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
.L464:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L465
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,672]
	cbz	w0, .L466
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,560]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L466:
	adrp	x0, .LANCHOR2+396
	ldrb	w0, [x0,#:lo12:.LANCHOR2+396]
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
	bne	.L473
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L474
.L473:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L474:
	ldr	w1, [x0]
	tbnz	x1, 9, .L475
	bl	wait_for_nand_flash_ready
	b	.L472
.L475:
	mov	w1, 1
	bl	nandc_irq_disable
.L472:
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
	bhi	.L478
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 510
	add	x1, x1, 736
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L478:
	add	x0, x20, :lo12:.LANCHOR0
	add	x19, x0, x19, sxtw
	ldrb	w23, [x19,736]
	ldr	x19, [x0,536]
	adrp	x0, .LANCHOR2
	mov	x21, x23
	add	x23, x23, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x23, x19, x23, lsl 8
	tbz	x0, 4, .L479
	adrp	x0, .LC41
	mov	w1, w21
	add	x0, x0, :lo12:.LC41
	mov	w2, w22
	mov	w3, w24
	bl	printk
.L479:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L480
	bl	zftl_flash_enter_slc_mode
	b	.L481
.L480:
	bl	zftl_flash_exit_slc_mode
.L481:
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
	cbnz	w0, .L482
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	and	w20, w0, w20
.L482:
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
	cbz	w19, .L483
	adrp	x0, .LC42
	mov	w1, w22
	add	x0, x0, :lo12:.LC42
	bl	printk
.L483:
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
	uxtb	w20, w0
	add	x0, x19, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	uxtb	w24, w1
	mov	w22, w2
	ldrb	w0, [x0,633]
	and	w23, w2, 2097151
	cmp	w0, w20
	bhi	.L492
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 555
	add	x1, x1, 768
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L492:
	add	x0, x19, :lo12:.LANCHOR0
	add	x20, x0, x20, sxtw
	ldrb	w21, [x20,736]
	ldr	x25, [x0,536]
	adrp	x0, .LANCHOR2
	mov	x20, x21
	add	x21, x21, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x21, x25, x21, lsl 8
	tbz	x0, 4, .L493
	adrp	x0, .LC43
	mov	w1, w20
	add	x0, x0, :lo12:.LC43
	mov	w2, w22
	bl	printk
.L493:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w24, .L494
	bl	zftl_flash_enter_slc_mode
	b	.L495
.L494:
	bl	zftl_flash_exit_slc_mode
.L495:
	ubfiz	x3, x20, 8, 8
	mov	w0, 96
	add	x3, x25, x3
	str	w0, [x3,2056]
	and	w0, w22, 255
	str	w0, [x3,2052]
	lsr	w0, w23, 8
	str	w0, [x3,2052]
	lsr	w0, w23, 16
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
	beq	.L496
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w20
	ldrh	w1, [x19,2]
	udiv	w1, w22, w1
	bl	printk
.L496:
	mov	w0, w20
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	ldrb	w1, [x0,389]
	ldrh	w20, [x0,390]
	adrp	x0, .LANCHOR0
	add	x23, x0, :lo12:.LANCHOR0
	mov	x21, x0
	mul	w20, w1, w20
	uxth	w20, w20
.L505:
	ldrb	w0, [x23,633]
	cmp	w0, w19
	bls	.L509
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,736]
	mov	w22, 0
.L506:
	cmp	w20, w22, uxth
	bls	.L510
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L506
.L510:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L505
.L509:
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
	stp	x19, x20, [sp,16]
	uxtb	w24, w0
	uxtb	w23, w1
	ldr	x0, [x21,536]
	add	w20, w23, 1
	str	x3, [x29,88]
	bl	rk_nandc_xfer_irq_flag_init
	ldrb	w0, [x21,516]
	ldr	x4, [x21,536]
	cmp	w0, 9
	ldr	x3, [x29,88]
	ldr	x2, [x29,80]
	bne	.L513
	ldr	w0, [x4,292]
	ubfiz	w19, w24, 1, 1
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
	cbz	w0, .L515
	ldrb	w1, [x21,720]
	orr	w0, w19, 512
	cmp	w1, wzr
	csel	w19, w0, w19, ne
	b	.L515
.L513:
	ldr	w0, [x4,368]
	ubfiz	w19, w24, 1, 1
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
	cbz	w24, .L527
	ldrb	w0, [x21,724]
	ubfx	x23, x23, 1, 7
	mov	w1, 64
	mov	w4, 128
	cmp	w0, 25
	add	x23, x3, x23, lsl 2
	csel	w4, w1, w4, cc
	mov	w1, 0
	b	.L520
.L515:
	add	x22, x22, :lo12:.LANCHOR0
	ubfx	x21, x19, 22, 6
	mov	x0, x2
	lsl	w1, w21, 10
	str	x2, [x22,688]
	mov	w2, w24
	str	x3, [x22,696]
	bl	rknand_dma_map_single
	str	w0, [x22,704]
	ldr	x0, [x22,696]
	lsl	w1, w21, 2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,708]
	ldr	x3, [x22,536]
	mov	w1, 1
	str	w1, [x22,712]
	cmp	w24, wzr
	ldr	w1, [x22,704]
	str	w1, [x3,52]
	str	w0, [x3,56]
	mov	w0, 16
	ldr	w1, [x3,48]
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
	str	w1, [x3,48]
	str	w19, [x3,16]
	orr	w19, w19, 4
	str	w19, [x3,16]
	b	.L512
.L520:
	cmp	x3, x23
	add	w0, w1, w4
	beq	.L522
	ldrh	w5, [x3,2]
	and	x1, x1, 4294967292
	ldr	x6, [x21,680]
	ldrh	w7, [x3],4
	orr	w5, w7, w5, lsl 16
	str	w5, [x6,x1]
	mov	w1, w0
	b	.L520
.L527:
	ldr	x1, [x21,680]
	str	w0, [x1]
.L522:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	lsl	w1, w20, 10
	ldr	x0, [x22,680]
	str	x2, [x22,688]
	str	x0, [x22,696]
	mov	x0, x2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,704]
	ldr	x0, [x22,696]
	lsl	w1, w20, 7
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,708]
	ldr	x3, [x22,536]
	mov	w0, 1
	str	w0, [x22,712]
	cmp	w24, wzr
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
.L512:
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
	bne	.L530
	str	w1, [x0,520]
	b	.L528
.L530:
	str	w1, [x0,336]
.L528:
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
	cbz	w0, .L535
	ldrb	w0, [x20,780]
	tbz	x0, 0, .L535
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,672]
.L535:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,720]
	cbz	w0, .L536
	mov	w0, 0
	strb	wzr, [x19,720]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,720]
.L536:
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
.L549:
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
	bne	.L549
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
.L552:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L552
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
	bne	.L560
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L556
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 74
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L556:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L554
.L560:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 784
.L561:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L561
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L558
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 81
	add	x1, x1, 1048
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L558:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 784
	strb	w1, [x0,x20]
.L554:
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
	cbz	w1, .L564
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L564
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2833
	bl	buf_add_tail
	b	.L565
.L564:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2833]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2833]
.L565:
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
	cbz	w0, .L573
.L576:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	ldrb	w2, [x1,2833]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L574
	b	.L575
.L573:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 121
	add	x1, x1, 1064
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w1, [x20,2834]
	cbnz	w1, .L576
.L578:
	mov	x0, 0
	b	.L577
.L574:
	ldrb	w1, [x1,2834]
	cmp	w1, 1
	beq	.L578
.L575:
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
.L577:
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
	bne	.L585
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L589
.L585:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 784
.L587:
	cmp	w2, 255
	beq	.L590
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L587
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L589:
	mov	w0, 1
	b	.L584
.L590:
	mov	w0, 0
.L584:
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
	cbnz	w0, .L592
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 172
	add	x1, x1, 1080
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L592:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2834]
	cbz	w0, .L591
	add	x0, x19, 2833
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L591
	ldrb	w0, [x19,2834]
	sub	w0, w0, #1
	strb	w0, [x19,2834]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L591:
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
	add	x2, x2, 376
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
	ldrb	w0, [x3,389]
	mov	w2, w21
	ldrh	w20, [x3,390]
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
	bne	.L599
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L599:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L601:
	ldrb	w2, [x0]
	cbz	w2, .L608
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L602
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L603
.L602:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L604
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L603
.L604:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L608
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L603:
	add	x0, x0, 1
	b	.L601
.L608:
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
	add	x7, x7, 376
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
	bhi	.L616
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 258
	add	x1, x1, 1312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L616:
	add	x6, x22, :lo12:.LANCHOR0
	uxtw	x19, w21
	lsl	x25, x19, 1
	adrp	x24, .LANCHOR2
	ldr	x0, [x6,600]
	ldrh	w26, [x0,x25]
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L617
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
.L617:
	add	x3, x22, :lo12:.LANCHOR0
	ldr	x0, [x3,584]
	add	x0, x0, x19, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L618
	cbz	w26, .L619
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 263
	add	x1, x1, 1312
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L619
.L618:
	ldrh	w1, [x3,2896]
	add	x6, x3, 2896
	mov	w19, 0
	cmp	w1, w21
	beq	.L619
	ldr	x3, [x3,608]
	ldrh	w1, [x3,48]
	cmp	w1, w21
	beq	.L619
	ldrh	w1, [x3,16]
	cmp	w1, w21
	beq	.L619
	ldrh	w1, [x3,80]
	cmp	w1, w21
	beq	.L619
	ldrh	w5, [x6,56]
	mov	x4, 0
.L620:
	cmp	w5, w4
	bls	.L651
	add	x4, x4, 1
	add	x1, x6, x4, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L620
	mov	w19, 0
	b	.L619
.L651:
	cbnz	w23, .L622
	add	x4, x22, :lo12:.LANCHOR0
	mov	w19, w23
	ldrh	w7, [x4,3224]
	cmp	w7, w21
	beq	.L619
	mov	x6, 0
.L624:
	add	x1, x4, 3232
	ldrh	w1, [x6,x1]
	cmp	w21, w1
	bne	.L623
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	mov	w19, 0
	tbz	x1, 8, .L619
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
	b	.L619
.L623:
	add	x6, x6, 2
	cmp	x6, 16
	bne	.L624
	sxtw	x4, w20
	add	x3, x3, x4, lsl 7
	add	x19, x3, 136
	b	.L626
.L622:
	add	x19, x3, 392
.L626:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L627
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
.L627:
	mov	x3, x19
	add	x0, x19, 128
	mov	w1, 65535
.L630:
	ldrh	w2, [x3]
	cmp	w2, w1
	bne	.L628
	strh	w21, [x3]
	add	x22, x22, :lo12:.LANCHOR0
	cbz	w23, .L629
	ldr	x1, [x22,608]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L650
.L629:
	ldr	x0, [x22,608]
	add	x2, x0, x20, uxth 1
	ldrh	w0, [x2,120]
	add	w0, w0, 1
	strh	w0, [x2,120]
.L650:
	mov	w19, 1
	b	.L619
.L628:
	add	x3, x3, 2
	cmp	x3, x0
	bne	.L630
	b	.L650
.L619:
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
.L654:
	ldrb	w0, [x21,3256]
	adrp	x19, .LANCHOR0
	cmp	w0, 255
	beq	.L676
	sxtw	x25, w0
	lsl	x0, x25, 6
	add	x2, x23, x0
	ldrb	w0, [x23,x0]
	ldr	w1, [x2,52]
	strb	w0, [x21,3256]
	ldrh	w24, [x2,48]
	cbz	w1, .L655
	ldr	w2, [x2,40]
	adrp	x0, .LC86
	mov	w3, 1
	add	x0, x0, :lo12:.LC86
	strh	w3, [x21,3216]
	str	w2, [x21,3220]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 539
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L655:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w20, 1
	ldrb	w1, [x0,3257]
	cmp	w1, 3
	bne	.L656
	ldr	x0, [x0,608]
	ldrb	w20, [x0,89]
	add	w20, w20, w20, lsl 1
	and	w20, w20, 1023
.L656:
	adrp	x0, .LANCHOR2
	uxth	x26, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L657
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
.L657:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3264]
	add	x0, x0, x25, lsl 6
	ldrb	w1, [x1,x26]
	ldrb	w0, [x0,785]
	cmp	w1, w0
	beq	.L658
	adrp	x0, .LC0
	mov	x1, x22
	mov	w2, 546
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L658:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 784
	add	x0, x1, x25, lsl 6
	ldrb	w0, [x0,61]
	cmp	w0, 3
	beq	.L659
	ldrb	w0, [x2,3257]
	cmp	w0, 3
	bne	.L659
	ldrb	w0, [x2,3272]
	cbnz	w0, .L659
	ldr	x3, [x2,3264]
	b	.L660
.L659:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w26, -1
	add	x27, x19, 784
	strh	w24, [x19,3212]
.L661:
	cmp	w20, w25, uxth
	bls	.L654
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
	b	.L661
.L660:
	cmp	w20, w0, uxth
	bls	.L654
	add	w2, w0, w24
	add	w0, w0, 1
	ldrb	w2, [x3,x2]
	add	x2, x1, x2, lsl 6
	strb	wzr, [x2,61]
	b	.L660
.L676:
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
	cbz	w4, .L678
	add	x1, x1, 392
	mov	w2, 1
	b	.L679
.L678:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L680
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L679:
	add	x4, x1, 128
	mov	w5, 65535
.L683:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L681
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L682
	ldr	x2, [x3,608]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L680
.L682:
	ldrb	w1, [x3,3273]
	ldr	x2, [x3,608]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L680
.L681:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L683
.L680:
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
	cbz	w3, .L688
	ldrb	w3, [x2,2834]
	cmp	w3, 1
	bhi	.L688
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
.L689:
	cmp	w1, w3
	bcs	.L702
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L690
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L690
	str	x1, [x29,56]
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L691
	ldr	x2, [x20,3264]
	adrp	x0, .LC88
	add	x0, x0, :lo12:.LC88
	ldrb	w2, [x2,x21]
	bl	printk
.L691:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3264]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2903]
	sub	w0, w0, #1
	strb	w0, [x19,2903]
	mov	w0, 1
	b	.L688
.L690:
	add	w1, w1, 1
	b	.L689
.L702:
	mov	w0, 0
.L688:
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
	bne	.L705
	adrp	x0, .LANCHOR0+3280
	ldr	x0, [x0,#:lo12:.LANCHOR0+3280]
	b	.L708
.L705:
	cmp	w1, 2
	bne	.L707
	mov	w0, w2
	bl	zftl_get_gc_node.part.12
	b	.L709
.L707:
	adrp	x0, .LANCHOR0+3288
	ldr	x0, [x0,#:lo12:.LANCHOR0+3288]
.L708:
	mov	w1, w2
	bl	_list_get_gc_head_node.isra.5
.L709:
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
	cbnz	w19, .L757
	cbnz	w21, .L713
	adrp	x26, .LC90
	strh	w21, [x0,3296]
	mov	w19, w21
	mov	w25, w21
	uxth	w23, w22
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x26, x26, :lo12:.LC90
.L714:
	cmp	w25, w23
	bcs	.L719
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
	beq	.L715
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	uxtw	x5, w1
	tbz	x0, 8, .L716
	ldr	x3, [x3,600]
	mov	x0, x26
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w3, [x3,x5,lsl 1]
	bl	printk
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
.L716:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x5,lsl 1]
	ldrh	w0, [x1,2878]
	cmp	w2, w0
	bcs	.L717
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L718
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L718
	b	.L719
.L717:
	strh	wzr, [x1,3296]
	b	.L719
.L715:
	strh	wzr, [x3,3296]
	b	.L719
.L718:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L714
.L719:
	tbz	x24, 1, .L721
	mov	w26, 0
	mov	w27, 65535
.L722:
	cmp	w26, w23
	beq	.L726
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w28, [x25,3298]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x25,3298]
	cmp	w1, w27
	beq	.L723
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x25,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x25,2880]
	cmp	w2, w1
	bcs	.L723
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L725
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L725
	b	.L728
.L723:
	strh	wzr, [x25,3298]
	b	.L726
.L725:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L722
.L726:
	cmp	w19, w23
	bcs	.L728
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3274]
	ldrb	w4, [x0,3276]
	ldrh	w2, [x0,3300]
	ldrh	w3, [x0,2880]
	mul	w1, w1, w4
	sub	w1, w1, w2, lsr 2
	cmp	w3, w1
	bge	.L721
	add	w2, w3, w2, lsr 3
	strh	w2, [x0,2880]
	b	.L721
.L728:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2880]
	ldrh	w0, [x1,3300]
	cmp	w2, w0
	bls	.L721
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,2880]
.L721:
	tbz	x24, 0, .L729
	cmp	w19, w23
	bcs	.L729
	mov	w25, 0
	mov	w27, 65535
.L730:
	cmp	w25, w23
	beq	.L733
	add	x28, x20, :lo12:.LANCHOR0
	ldrh	w26, [x28,3302]
	mov	w0, w26
	add	w26, w26, 1
	bl	zftl_get_gc_node.part.12
	strh	w26, [x28,3302]
	cmp	w27, w0, uxth
	beq	.L731
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L732
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bcc	.L732
	b	.L735
.L731:
	strh	wzr, [x28,3302]
	b	.L733
.L732:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L730
.L733:
	cmp	w19, w23
	bcs	.L735
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,2878]
	ldrh	w0, [x20,3300]
	cmp	w1, w0
	bcs	.L729
	add	w0, w1, w0, lsr 3
	b	.L810
.L735:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,3300]
	ldrh	w1, [x20,2878]
	cmp	w1, w0, lsr 1
	bls	.L729
	sub	w0, w1, w0, lsr 3
.L810:
	strh	w0, [x20,2878]
	b	.L729
.L713:
	and	w1, w24, 1
	strh	w19, [x0,3214]
	strh	w19, [x0,3304]
	str	w1, [x29,104]
	tbz	x24, 0, .L736
	ldrh	w2, [x0,3306]
	ldrh	w1, [x0,2852]
	cmp	w1, w2, lsr 2
	bcc	.L737
	ldrh	w0, [x0,2850]
	cmp	w0, w1
	bls	.L738
.L737:
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
	beq	.L739
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x23,600]
	ldrh	w2, [x23,3300]
	ldrh	w1, [x3,x1]
	cmp	w1, w2, lsr 2
	bcs	.L739
	mov	w1, w19
	mov	w2, w21
	strh	w19, [x23,3302]
	bl	gc_add_sblk
	cmp	w0, w19
	cset	w19, ne
.L739:
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L741:
	add	x25, x20, :lo12:.LANCHOR0
	ldrh	w28, [x25,3302]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node.part.12
	strh	w28, [x25,3302]
	cmp	w26, w0, uxth
	beq	.L738
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L740
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L738
.L740:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L741
.L738:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3306]
	ldrh	w1, [x0,2852]
	cmp	w1, w2, lsr 3
	bhi	.L758
	ldrh	w0, [x0,2850]
	add	w0, w0, 8
	cmp	w1, w0
	bgt	.L758
.L736:
	tbz	x24, 1, .L744
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 0
	mov	w27, 65535
	uxth	w28, w22
	strh	wzr, [x0,3298]
.L749:
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
	beq	.L745
	cmp	w22, 1
	bne	.L746
	ldrh	w3, [x23,3274]
	ldrb	w2, [x23,3276]
	mul	w2, w2, w3
	ldrh	w3, [x23,3300]
	sub	w2, w2, w3, lsr 3
	strh	w2, [x23,2874]
.L746:
	add	x2, x20, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w3, [x3,x1]
	ldrh	w1, [x2,2874]
	cmp	w3, w1
	bcs	.L747
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L748
	add	w19, w19, 1
	add	w26, w26, 1
	uxth	w19, w19
	uxth	w26, w26
	cmp	w19, w28
	bcc	.L748
	b	.L745
.L747:
	strh	wzr, [x2,3298]
	b	.L745
.L748:
	sub	w0, w25, #1
	uxth	w25, w0
	cbnz	w25, .L749
.L745:
	cmp	w19, w22
	bcc	.L750
	cbnz	w26, .L751
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bls	.L751
.L750:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3274]
	ldrb	w4, [x0,3276]
	ldrh	w3, [x0,3300]
	ldrh	w2, [x0,2874]
	mul	w1, w1, w4
	lsr	w3, w3, 3
	sub	w1, w1, w3
	cmp	w2, w1
	bge	.L744
	add	w2, w3, w2
	strh	w2, [x0,2874]
	b	.L744
.L751:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,3300]
	ldrb	w2, [x0,3276]
	ldrh	w3, [x0,2874]
	mul	w2, w1, w2
	cmp	w3, w2
	ble	.L744
	sub	w1, w3, w1, lsr 3
	strh	w1, [x0,2874]
.L744:
	ldr	w0, [x29,104]
	cbz	w0, .L729
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3296]
.L755:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x23,3296]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x23,3296]
	cmp	w1, w26
	beq	.L752
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x23,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 2
	bls	.L753
	ldrh	w2, [x23,2876]
	cmp	w2, w1
	bls	.L753
	ldrh	w2, [x23,3306]
	ldrh	w1, [x23,2852]
	cmp	w1, w2, lsr 1
	bls	.L752
.L753:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L754
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L752
.L754:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L755
.L752:
	cmp	w19, w22
	add	x20, x20, :lo12:.LANCHOR0
	bcs	.L756
	ldrh	w0, [x20,3300]
	ldrh	w1, [x20,2876]
	cmp	w1, w0, lsr 1
	bls	.L729
	sub	w0, w1, w0, lsr 3
	strh	w0, [x20,2876]
	b	.L729
.L756:
	ldrh	w1, [x20,3300]
	ldrb	w0, [x20,3276]
	ldrh	w2, [x20,2876]
	mul	w0, w1, w0
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L729
	add	w1, w2, w1, lsr 3
	strh	w1, [x20,2876]
.L729:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L757
	adrp	x0, .LC91
	mov	w1, w21
	add	x0, x0, :lo12:.LC91
	mov	w2, w24
	mov	w3, w19
	mov	w4, w22
	bl	printk
.L757:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L758:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
	strh	wzr, [x0,3296]
.L743:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x25,3296]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	strh	w28, [x25,3296]
	cmp	w26, w0, uxth
	beq	.L736
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L742
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L736
.L742:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L743
	b	.L736
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
	bne	.L812
	add	x0, x2, 3312
	add	x2, x2, 2844
	b	.L815
.L812:
	cmp	w0, 16
	bne	.L814
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L815
.L814:
	add	x0, x2, 3328
	add	x2, x2, 2848
.L815:
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
	bne	.L817
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L820
.L817:
	cmp	w3, 96
	bne	.L819
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L820
.L819:
	cmp	w3, 160
	bne	.L816
	add	x0, x2, 3280
	add	x2, x2, 2854
.L820:
	bl	_insert_data_list
.L816:
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
	cbnz	w21, .L822
	mov	w0, 65535
	cmp	w19, w0
	beq	.L822
	adrp	x0, .LC92
	mov	w1, w19
	add	x0, x0, :lo12:.LC92
	bl	printk
	ldr	x0, [x23,608]
	mov	w1, -1
	strh	w1, [x0,588]
	b	.L823
.L822:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w3, [x0,2846]
	ldrh	w1, [x0,2848]
	cmp	w3, w1
	bls	.L824
	mov	w1, 0
	mov	x2, x0
	lsr	w3, w3, 3
	cmp	w21, w1
	csel	w1, w3, w1, ne
	add	x0, x0, 3320
	add	x2, x2, 2846
	b	.L842
.L824:
	lsr	w1, w1, 2
	cbz	w21, .L828
	mov	w1, 7
	mul	w3, w3, w1
	mov	w1, 8
	sdiv	w3, w3, w1
	uxth	w1, w3
.L828:
	add	x2, x20, :lo12:.LANCHOR0
	add	x0, x2, 3328
	add	x2, x2, 2848
.L842:
	bl	_list_pop_index_node
	uxth	w19, w0
	mov	w0, 65535
	cmp	w19, w0
	bne	.L829
	add	x5, x20, :lo12:.LANCHOR0
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	mov	w1, w19
	mov	w2, w22
	ldr	x3, [x5,3312]
	ldrh	w4, [x5,2844]
	ldrh	w5, [x5,2848]
	bl	printk
.L829:
	cbz	w21, .L823
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L823
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
.L823:
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
	bne	.L844
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2846]
	ldrh	w1, [x0,2848]
	cmp	w2, w1
	bcc	.L845
	ldrh	w0, [x0,2844]
	cmp	w0, w2
	bls	.L846
	cbz	w1, .L846
.L845:
	add	x2, x19, :lo12:.LANCHOR0
	lsr	w1, w1, 1
	add	x0, x2, 3328
	b	.L874
.L846:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L873
.L844:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w21, 1
	ldr	x0, [x22,608]
	ldrh	w20, [x0,590]
	beq	.L848
	mov	w0, 65535
	cmp	w20, w0
	beq	.L848
	adrp	x0, .LC95
	mov	w1, w20
	add	x0, x0, :lo12:.LC95
	bl	printk
	ldr	x0, [x22,608]
	mov	w1, -1
	strh	w1, [x0,590]
	b	.L849
.L848:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,2844]
	ldrh	w1, [x2,2848]
	cmp	w0, w1
	bcc	.L850
	ldrh	w2, [x2,2846]
	cmp	w2, w0
	bls	.L851
	cbz	w1, .L851
.L850:
	lsr	w1, w1, 1
	cmp	w21, 1
	csel	w3, w1, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3328
	mov	w1, w3
.L874:
	add	x2, x2, 2848
	b	.L873
.L851:
	lsr	w0, w0, 1
	cmp	w21, 1
	csel	w3, w0, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 3312
	mov	w1, w3
	add	x2, x2, 2844
.L873:
	bl	_list_pop_index_node
	uxth	w0, w0
	mov	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L849
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	mov	w1, w20
	mov	w2, w21
	ldr	x3, [x19,3312]
	ldrh	w4, [x19,2844]
	ldrh	w5, [x19,2848]
	bl	printk
.L849:
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
	bne	.L876
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L879
.L876:
	cmp	w3, 96
	bne	.L878
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L879
.L878:
	cmp	w3, 160
	bne	.L875
	add	x0, x2, 3280
	add	x2, x2, 2854
.L879:
	bl	_list_remove_node
.L875:
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
	bne	.L881
	add	x0, x2, 3312
	add	x2, x2, 2844
	b	.L884
.L881:
	cmp	w0, 16
	bne	.L883
	add	x0, x2, 3320
	add	x2, x2, 2846
	b	.L884
.L883:
	add	x0, x2, 3328
	add	x2, x2, 2848
.L884:
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
	bne	.L886
	add	x0, x2, 616
	add	x2, x2, 2850
	b	.L889
.L886:
	cmp	w3, 96
	bne	.L888
	add	x0, x2, 3288
	add	x2, x2, 2852
	b	.L889
.L888:
	cmp	w3, 160
	bne	.L885
	add	x0, x2, 3280
	add	x2, x2, 2854
.L889:
	bl	_list_update_data_list
.L885:
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
	cbz	x19, .L890
	mov	x22, -6148914691236517206
	adrp	x23, .LC97
	mov	w20, 0
	adrp	x25, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC97
	mov	w26, 65535
	mov	w24, 6
.L893:
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
	beq	.L890
	ldr	x0, [x21,528]
	add	w20, w20, 1
	umull	x19, w19, w24
	uxth	w20, w20
	add	x19, x0, x19
	ldrh	w0, [x21,3336]
	cmp	w0, w20
	bcs	.L893
.L890:
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
	bls	.L896
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
.L896:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L897
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L897:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L898
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L898:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L895
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L895:
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
.L901:
	add	x8, x7, :lo12:.LANCHOR0
	ldrb	w3, [x8,3276]
	cmp	w6, w3
	bge	.L907
	asr	w3, w11, w6
	tbnz	x3, 0, .L902
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
	bls	.L903
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L903:
	add	w2, w2, 1
.L902:
	add	w6, w6, 1
	b	.L901
.L907:
	mov	w0, w2
	mov	w4, -1
.L905:
	add	x3, x7, :lo12:.LANCHOR0
	ldrb	w3, [x3,3276]
	cmp	w2, w3
	bge	.L908
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L905
.L908:
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
	tbz	x2, 3, .L911
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
	ble	.L912
	ldrh	w0, [x3,2846]
	ldrh	w6, [x3,2854]
	add	w6, w6, w0
	ldrh	w0, [x3,3308]
	add	w0, w0, 8
	cmp	w6, w0
	blt	.L928
	ldrh	w0, [x3,2844]
	ldrh	w6, [x3,2850]
	add	w6, w6, w0
	ldrh	w0, [x3,2852]
	add	w6, w6, w0
	ldrh	w0, [x3,3344]
	add	w0, w0, 8
	cmp	w6, w0
	bge	.L928
	b	.L917
.L912:
	madd	w0, w0, w4, w8
	ldrh	w6, [x3,3346]
	cmp	w0, w6
	ble	.L914
	ldrh	w0, [x3,2844]
	ldrh	w6, [x3,2850]
	add	w6, w6, w0
	ldrh	w0, [x3,2852]
	add	w6, w6, w0
	ldrh	w0, [x3,3344]
	add	w0, w0, 8
	cmp	w6, w0
	blt	.L917
	ldrh	w0, [x3,2846]
	ldrh	w6, [x3,2854]
	add	w6, w6, w0
	ldrh	w0, [x3,3308]
	add	w0, w0, 8
	cmp	w6, w0
	bge	.L917
.L928:
	mov	w0, 2
	bfi	w2, w0, 3, 2
	b	.L926
.L917:
	and	w2, w2, -25
.L926:
	strb	w2, [x20,2]
.L914:
	ldrb	w0, [x20,2]
	ands	w0, w0, 24
	bne	.L918
	mul	w4, w9, w4
	ldrh	w0, [x7,x19]
	add	w4, w4, w4, lsl 1
	add	w4, w5, w4, lsr 2
	ubfx	x5, x4, 2, 9
	bfi	w0, w5, 0, 11
	strh	w0, [x7,x19]
	b	.L911
.L918:
	cmp	w0, 16
	bne	.L911
	sdiv	w4, w5, w4
	ldr	w0, [x7,x19]
	add	w4, w4, w4, lsl 1
	add	w4, w9, w4, lsr 2
	ubfx	x4, x4, 2, 6
	bfi	w0, w4, 11, 8
	str	w0, [x7,x19]
.L911:
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
	tbz	x2, 3, .L920
	ldr	x2, [x0,608]
	ldrh	w0, [x2,584]
	cmp	w0, w1
	bne	.L921
	mov	w0, -1
	ldrh	w3, [x2,588]
	strh	w0, [x2,584]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L921
	adrp	x0, .LC98
	strh	w1, [x2,588]
	add	x0, x0, :lo12:.LC98
	b	.L927
.L921:
	ldrh	w0, [x2,586]
	cmp	w0, w1
	bne	.L920
	mov	w0, -1
	ldrh	w3, [x2,590]
	strh	w0, [x2,586]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L920
	adrp	x0, .LC99
	strh	w1, [x2,590]
	add	x0, x0, :lo12:.LC99
.L927:
	bl	printk
	b	.L909
.L920:
	mov	w0, w1
	bl	zftl_insert_free_list
.L909:
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
.L930:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L968
	add	x0, x24, x20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,600]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L931
	adrp	x0, .LC100
	mov	w1, w22
	add	x0, x0, :lo12:.LC100
	bl	printk
.L931:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,600]
	strh	wzr, [x1,x26]
	ldr	x1, [x0,600]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L932
	ldr	x0, [x0,584]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L933
	ldrb	w2, [x22,2]
	adrp	x0, .LC101
	add	x0, x0, :lo12:.LC101
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L933:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L946
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L934
.L946:
	adrp	x0, .LC0
	mov	x1, x21
	mov	w2, 793
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L934:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,608]
	ldrh	w1, [x2,124]
	cbz	w1, .L936
	mov	x0, 0
.L938:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L937
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L936
.L937:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L938
.L936:
	ldrh	w1, [x2,120]
	cbz	w1, .L939
	mov	x0, 0
.L941:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L940
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L939
.L940:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L941
.L939:
	ldrh	w1, [x2,122]
	cbz	w1, .L942
	mov	x0, 0
.L944:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L943
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L942
.L943:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L944
	b	.L942
.L932:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L942:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L930
.L968:
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
	cbz	w0, .L970
	ldrh	w2, [x5,3350]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L970:
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
.L975:
	add	x2, x23, :lo12:.LANCHOR0
	ldrb	w0, [x2,3352]
	cmp	w19, w0
	bge	.L986
	ldrb	w3, [x2,3338]
	mov	w21, 0
	ldrh	w2, [x2,3350]
	mov	w1, w21
	sub	w4, w3, #1
	mul	w6, w19, w3
	mul	w5, w24, w3
.L987:
	cmp	w1, w3
	bge	.L1000
	add	w0, w1, w6
	asr	w0, w25, w0
	tbnz	x0, 0, .L976
	and	w0, w1, w4
	add	x7, x29, 80
	add	w0, w5, w0
	mul	w0, w0, w2
	str	w0, [x7,w21,sxtw 2]
	add	w21, w21, 1
.L976:
	add	w1, w1, 1
	b	.L987
.L1000:
	cmp	w3, 4
	bne	.L999
	mov	x26, 0
.L978:
	cmp	w21, w26
	ble	.L981
	add	x2, x29, 80
	mov	w0, w19
	mov	w1, w22
	ldr	w2, [x2,x26,lsl 2]
	add	x26, x26, 1
	bl	flash_erase_block_en
	b	.L978
.L999:
	cmp	w21, 2
	bne	.L982
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3348]
	cbz	w0, .L983
	cmp	w22, wzr
	ldr	w2, [x29,80]
	ldr	w3, [x29,84]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L983:
	ldr	w2, [x29,80]
	mov	w0, w19
	ldr	w3, [x29,84]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L981
.L982:
	cmp	w21, 1
	bne	.L981
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3348]
	cbz	w0, .L985
	cmp	w22, wzr
	ldr	w2, [x29,80]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L985:
	ldr	w2, [x29,80]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L981:
	add	w19, w19, 1
	b	.L975
.L986:
	cbnz	w22, .L988
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
	bge	.L990
	strh	w1, [x0,96]
	b	.L990
.L988:
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
	bcs	.L990
	strh	w1, [x0,98]
.L990:
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
	bls	.L1002
	strh	wzr, [x1,136]
.L1002:
	ldrh	w0, [x1,112]
	cbnz	w0, .L1003
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1118
	add	x1, x1, 1368
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1003:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,2864]
.L1007:
	ldrh	w2, [x1,136]
.L1004:
	cmp	w2, 63
	bgt	.L1010
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L1009
	add	w2, w2, 1
	b	.L1004
.L1010:
	strh	wzr, [x1,136]
	b	.L1007
.L1009:
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
	bls	.L1012
	strh	wzr, [x2,138]
.L1012:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L1013
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1142
	add	x1, x1, 1392
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1013:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,2864]
.L1017:
	ldrh	w2, [x1,138]
.L1014:
	cmp	w2, 63
	bgt	.L1019
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L1015
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L1015:
	add	w2, w2, 1
	b	.L1014
.L1019:
	strh	wzr, [x1,138]
	b	.L1017
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
	beq	.L1020
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,584]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L1020
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L1024
	ldr	x1, [x23,2864]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L1025
.L1024:
	tst	w0, 24
	ldr	x1, [x23,2864]
	bne	.L1026
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L1025
.L1026:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L1025:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1027
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
	b	.L1020
.L1027:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L1028
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L1020
.L1028:
	cmp	w0, 96
	bne	.L1020
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L1020:
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
	bge	.L1030
	add	x3, x3, 16
	b	.L1031
.L1030:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L1031:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, x1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L1032
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1529
	add	x1, x1, 1416
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1032:
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
.L1034:
	cmp	w2, w3
	beq	.L1038
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L1035
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L1036
.L1035:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L1034
.L1038:
	mov	w0, -1
.L1036:
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
	bne	.L1040
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1644
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1040:
	adrp	x20, .LANCHOR0
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	cmp	w1, w0
	bne	.L1041
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1645
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1041:
	ldrh	w0, [x19,6]
	cbnz	w0, .L1042
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1646
	add	x1, x1, 1440
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1042:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrb	w2, [x1,3276]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L1043:
	cmp	w0, w1
	bne	.L1047
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L1044
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L1044:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L1043
.L1047:
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
	bne	.L1046
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L1046:
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
	bcs	.L1049
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 1677
	add	x1, x1, 1464
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1049:
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
	cbnz	w0, .L1051
	cmp	w2, 4
	bhi	.L1051
	add	w2, w2, 1
	strh	w2, [x3,3888]
	b	.L1050
.L1051:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w9, 65535
	strh	wzr, [x0,3888]
	ldrh	w6, [x0,576]
	ldr	x7, [x0,600]
	ldr	x8, [x0,584]
	mov	x0, 0
	mov	w2, w0
	mov	w3, w0
.L1053:
	cmp	w6, w0, uxth
	bls	.L1061
	ldrh	w4, [x7,x0,lsl 1]
	cmp	w4, w9
	beq	.L1054
	add	x5, x8, x0, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1055
	add	w2, w2, w4
	b	.L1054
.L1055:
	add	w3, w3, w4
.L1054:
	add	x0, x0, 1
	b	.L1053
.L1061:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,608]
	ldr	x1, [x1,2864]
	str	w3, [x0,524]
	str	w2, [x0,528]
	ldrh	w1, [x1,120]
	cbz	w1, .L1050
	udiv	w2, w2, w1
	str	w2, [x0,532]
.L1050:
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
	cbnz	w2, .L1064
	mov	w0, 1
	str	w0, [x1,2884]
.L1064:
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
	beq	.L1067
	add	x0, x2, :lo12:.LANCHOR0
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,600]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L1068
	adrp	x0, .LC102
	mov	w2, w19
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L1074
.L1068:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L1067:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3224]
	cmp	w2, w1
	beq	.L1069
	mov	w0, 65535
	cmp	w2, w0
	bne	.L1070
	strh	w1, [x19,3224]
.L1074:
	mov	w0, 0
	b	.L1069
.L1070:
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
.L1069:
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
.L1076:
	cmp	w4, w11
	beq	.L1080
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L1077
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR0
	bne	.L1078
	ldr	x6, [x4,3880]
	add	x4, x4, 3360
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L1079
.L1078:
	ldr	x4, [x4,3880]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L1079:
	add	x4, x3, :lo12:.LANCHOR0
	mov	w6, -1
	ldr	x4, [x4,3880]
	strh	w6, [x4,x7,lsl 1]
	b	.L1080
.L1077:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L1076
.L1080:
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
	beq	.L1082
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
.L1082:
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
	tbz	x0, 14, .L1087
	adrp	x0, .LC103
	mov	w1, w21
	add	x0, x0, :lo12:.LC103
	mov	w2, w20
	bl	printk
.L1087:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w1, w20
	bls	.L1086
	ldr	x0, [x0,584]
	add	x20, x0, x20, uxth 2
	mov	w0, 1
	lsl	w1, w0, w21
	ldrb	w0, [x20,3]
	orr	w1, w1, w0
	strb	w1, [x20,3]
.L1086:
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
	cbnz	x0, .L1094
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 661
	add	x1, x1, 1488
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L1103
.L1094:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w1, w0
	bls	.L1099
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 666
	add	x1, x1, 1488
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1099:
	ldrb	w0, [x20,56]
	cbz	w0, .L1096
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,2832]
	cmp	w1, w0
	bcs	.L1097
.L1096:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_free
	ldrb	w0, [x19,2856]
	b	.L1103
.L1097:
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
.L1103:
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
.L1105:
	ldrb	w0, [x19,3892]
	cmp	w0, 255
	beq	.L1112
	sbfiz	x0, x0, 6, 32
	add	x20, x22, x0
	add	x4, x20, 32
	ldrb	w0, [x22,x0]
	strb	w0, [x19,3892]
	ldr	w0, [x20,52]
	cmn	w0, #1
	bne	.L1106
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
	bne	.L1107
	strh	wzr, [x0,22]
	b	.L1108
.L1107:
	ldrh	w1, [x0,48]
	cmp	w1, w25
	bne	.L1108
	strh	wzr, [x0,54]
.L1108:
	mov	x0, x20
	bl	ftl_write_buf
	b	.L1105
.L1106:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x20,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x20,2]
	tbz	x0, 2, .L1110
	and	w0, w0, -3
	strb	w0, [x20,2]
	b	.L1105
.L1110:
	mov	x0, x20
	bl	buf_free
	b	.L1105
.L1112:
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
	cbnz	x0, .L1114
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 948
	add	x1, x1, 1504
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	b	.L1113
.L1114:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w1, w0
	bls	.L1117
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 953
	add	x1, x1, 1504
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1117:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x1, x20
	add	x0, x19, 3893
	bl	buf_add_tail
	ldrb	w0, [x19,3894]
	add	w0, w0, 1
	strb	w0, [x19,3894]
.L1113:
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
	beq	.L1120
	adrp	x0, .LANCHOR0+584
	ldr	x21, [x0,#:lo12:.LANCHOR0+584]
	add	x21, x21, x19, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L1121
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 827
	add	x1, x1, 1528
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1121:
	ldrb	w1, [x21,2]
	bfi	w1, w20, 5, 3
	ubfx	x2, x1, 3, 2
	orr	w0, w22, w2
	bfi	w1, w0, 3, 2
	strb	w1, [x21,2]
	b	.L1127
.L1120:
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
.L1127:
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
	beq	.L1129
	add	x20, x20, 784
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC112
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC112
.L1131:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1129
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1131
.L1129:
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
	beq	.L1139
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
	adrp	x10, .L1142
	uxth	w4, w4
	and	w5, w5, w7
	and	w5, w5, w4
	add	x3, x3, 784
	add	x10, x10, :lo12:.L1142
.L1145:
	add	x0, x3, x2, lsl 6
	ldr	w6, [x0,40]
	ubfx	x11, x6, 21, 3
	cmp	w11, w9
	bne	.L1140
	lsr	w6, w6, w8
	ldrb	w0, [x0,58]
	and	w6, w7, w6
	and	w6, w4, w6
	cmp	w6, w5
	bne	.L1141
	cmp	w1, 1
	beq	.L1143
	b	.L1139
.L1141:
	cmp	w1, 3
	bhi	.L1140
	ldrb	w6, [x10,w1,uxtw]
	adr	x11, .Lrtx1142
	add	x6, x11, w6, sxtb #2
	br	x6
.Lrtx1142:
	.section	.rodata
	.align	0
	.align	2
.L1142:
	.byte	(.L1139 - .Lrtx1142) / 4
	.byte	(.L1143 - .Lrtx1142) / 4
	.byte	(.L1144 - .Lrtx1142) / 4
	.byte	(.L1139 - .Lrtx1142) / 4
	.text
.L1143:
	sub	w6, w0, #6
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L1140
	b	.L1139
.L1144:
	cmp	w0, 10
	bne	.L1139
.L1140:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L1145
	mov	w0, 0
.L1139:
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
.L1155:
	cmp	w7, 255
	beq	.L1156
	sbfiz	x6, x7, 6, 32
	sxtw	x1, w7
	add	x2, x4, x6
	ldrb	w2, [x2,58]
	sub	w2, w2, #11
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1157
	cbz	w5, .L1154
	strb	w7, [x0,2835]
	b	.L1154
.L1157:
	lsl	x5, x1, 6
	ldrb	w7, [x4,x6]
	add	x2, x4, x5
	strb	w11, [x4,x5]
	ldrb	w5, [x2,59]
	cmp	w5, 1
	bne	.L1160
	ldrh	w2, [x2,50]
	cbnz	w2, .L1168
	mov	w2, w10
	mov	x5, x13
	b	.L1161
.L1160:
	cbz	w5, .L1178
.L1162:
	mov	w5, 1
	b	.L1155
.L1178:
	ldr	w2, [x2,36]
	cmn	w2, #1
	beq	.L1162
	mov	w2, w9
	mov	x5, x12
	b	.L1161
.L1168:
	mov	w2, w8
	add	x5, x0, 3256
.L1161:
	cmp	w2, 255
	bne	.L1166
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x3, x1, lsl 6
	strb	w7, [x3,2835]
	ldrb	w0, [x1,785]
	strb	w0, [x5]
	b	.L1154
.L1166:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x4,x2]
	cmp	w2, 255
	bne	.L1166
	add	x1, x4, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1,1]
	strb	w1, [x4,x5]
	b	.L1162
.L1156:
	cbz	w5, .L1154
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, -1
	strb	w0, [x3,2835]
.L1154:
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
	bls	.L1180
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1181
.L1180:
	adrp	x22, .LC113
	add	x22, x22, :lo12:.LC113
.L1193:
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
	cbnz	w0, .L1182
	mov	w1, w21
	mov	x0, x22
	bl	printk
	ldr	x2, [x20,584]
	add	x1, x2, x21, uxth 2
	ldrb	w2, [x1,2]
	orr	w2, w2, -32
	strb	w2, [x1,2]
	b	.L1193
.L1182:
	ldr	x1, [x20,2864]
	mov	w0, 1
	mov	w2, 65535
	str	w0, [x20,3896]
	mov	x20, 0
	strh	wzr, [x1,690]
.L1184:
	add	x3, x1, x20, lsl 1
	uxth	w0, w20
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L1183
	add	x20, x20, 1
	cmp	x20, 128
	bne	.L1184
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 194
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, w20
.L1183:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2864]
	add	x0, x1, x0, sxtw 1
	strh	w21, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L1181:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2864]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w20, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L1186
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 199
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1186:
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
	tbz	x1, 12, .L1190
	mov	w2, 1
	mov	w1, w20
	lsl	w2, w2, w0
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	sub	w2, w2, #1
	mov	w3, w20
	uxth	w4, w4
	bl	printk
.L1190:
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
.L1197:
	add	x1, x2, 3904
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1196
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1197
	mov	x0, 0
	mov	w3, 32768
.L1199:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1198
	cmp	w2, w3
	bcs	.L1198
	mov	w3, w2
	mov	w19, w4
.L1198:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1199
	cmp	w19, 32
	mov	w0, w19
	bne	.L1196
	adrp	x0, .LANCHOR4+64
	mov	w2, -1
	mov	w1, 0
	ldrb	w3, [x0,#:lo12:.LANCHOR4+64]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3904
.L1201:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1200
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1200:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1201
	cmp	w19, 32
	bne	.L1202
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 297
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1202:
	mov	w0, w19
.L1196:
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
	cbnz	w0, .L1210
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1211:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1211
.L1217:
	adrp	x20, .LANCHOR4
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1024
	add	x0, x20, 68
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, 0
	b	.L1212
.L1210:
	cmp	w0, 1
	bne	.L1213
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L1216:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L1214
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1214:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1216
	b	.L1217
.L1213:
	cmp	w0, 2
	bne	.L1218
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L1220:
	add	x3, x4, 4
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L1220
	b	.L1217
.L1218:
	cmp	w0, 3
	bne	.L1221
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L1224:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L1222
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1222:
	add	x2, x3, 4
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1224
	b	.L1217
.L1221:
	cmp	w0, 4
	bne	.L1225
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
.L1227:
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
	bne	.L1227
	b	.L1217
.L1225:
	cmp	w0, 5
	bne	.L1228
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1229:
	add	x1, x2, 4
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1229
	mov	x0, 0
.L1230:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L1230
	b	.L1217
.L1228:
	cmp	w0, 8
	bne	.L1217
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1231:
	add	x1, x2, 4
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L1231
	b	.L1217
.L1212:
	add	x1, x19, 4
	add	x2, x20, 68
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x2,w1,sxtw 1]
	bne	.L1212
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
	tbz	x0, 12, .L1248
	adrp	x1, .LANCHOR3
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L1248:
	add	x25, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	add	x26, x22, :lo12:.LANCHOR4
	mov	w2, 8
	ldrh	w0, [x25,402]
	mov	x24, 0
	ldrb	w1, [x25,388]
	add	x27, x20, 748
	strh	w0, [x20,2]
	add	x25, x25, 377
	ldrh	w0, [x25,9]
	strb	wzr, [x20,633]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x26,1092]
	add	x0, x20, 736
	bl	ftl_memset
	add	x0, x26, 1096
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
	ldrb	w23, [x25,-1]
.L1250:
	mov	x0, x25
	add	x1, x27, x24, lsl 3
	mov	w2, w23
	bl	flash_mem_cmp8
	cbnz	w0, .L1249
	ldrb	w1, [x20,633]
	add	x2, x26, 1096
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x20, x1, sxtw
	strb	w0, [x20,633]
	strb	w24, [x1,736]
.L1249:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1250
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 376
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1251
.L1255:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR4
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,389]
	ldrb	w0, [x21,633]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,390]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1128]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1251:
	add	x24, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w27, [x1,23]
	mov	x25, 0
	and	w20, w20, 65280
	add	x26, x0, 377
	ldrh	w2, [x24,2]
	add	x3, x24, 748
	add	x4, x22, :lo12:.LANCHOR4
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L1254:
	add	x1, x3, x25, lsl 3
	mov	x0, x26
	mov	w2, w23
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1252
	ldrb	w1, [x24,633]
	add	x0, x4, 1096
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x0,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x24, x1, sxtw
	strb	w0, [x24,633]
	strb	w25, [x1,736]
.L1252:
	add	x25, x25, 1
	cmp	x25, 4
	bne	.L1254
	b	.L1255
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
	tbz	x0, 12, .L1266
	adrp	x1, .LANCHOR3
	adrp	x0, .LC115
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	add	x1, x1, 72
	mov	w2, 222
	mov	w3, 0
	bl	printk
.L1266:
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
.L1267:
	ldrh	w1, [x19,3300]
	ldrb	w2, [x19,3276]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1276
	uxtw	x2, w0
	ldr	x1, [x19,3872]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1268
	uxtb	w1, w1
	add	x3, x19, 3360
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,3880]
	strh	w4, [x1,x2,lsl 1]
.L1268:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1267
.L1276:
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
	add	x1, x1, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x1, 376
	mov	w5, 1
	ldrb	w2, [x1,9]
	ldrh	w3, [x0,2]
	mul	w2, w2, w3
	ldrb	w3, [x0,724]
	strb	w3, [x19,7]
	uxth	w2, w2
	strh	w2, [x19,4]
	ldrb	w3, [x1,13]
	mul	w2, w3, w2
	ldrh	w3, [x1,14]
	strb	wzr, [x19,10]
	mul	w2, w2, w3
	ldrb	w3, [x1,8]
	mul	w2, w2, w3
	str	w2, [x19]
	ldrb	w2, [x1,9]
	strb	w2, [x19,6]
	mov	w2, 32
	ldrb	w4, [x0,633]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L1278:
	cmp	w4, w1, uxtb
	bls	.L1280
	add	x2, x0, 736
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1278
.L1280:
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
	bls	.L1283
	bl	ftl_tmp_into_update
.L1283:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x0, [x3,608]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L1284
	ldr	x3, [x3,2864]
	ldr	w1, [x0,572]
	ldr	w3, [x3,36]
	add	w1, w1, 32
	cmp	w3, w1
	bls	.L1334
.L1284:
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
.L1286:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,576]
	cmp	w0, w23
	bls	.L1395
	uxtw	x7, w23
	ldr	x0, [x4,584]
	lsl	x1, x7, 2
	add	x6, x0, x1
	ldrb	w2, [x6,2]
	and	w3, w2, 224
	cmp	w3, 224
	beq	.L1287
	tbz	x2, 3, .L1288
	ldrh	w3, [x0,x1]
	ldr	w2, [x0,x1]
	and	w3, w3, 2047
	ubfx	x2, x2, 11, 8
	b	.L1289
.L1288:
	tst	w2, 24
	bne	.L1290
	ldrh	w3, [x0,x1]
	mov	w2, 65535
	and	w3, w3, 2047
	b	.L1289
.L1290:
	ldr	w2, [x0,x1]
	mov	w3, 65535
	ubfx	x2, x2, 11, 8
	b	.L1291
.L1289:
	ldr	w4, [x29,136]
	add	w26, w26, 1
	cmp	w20, w3
	add	w4, w4, w3
	str	w4, [x29,136]
	uxth	w26, w26
	bls	.L1333
	add	x4, x19, :lo12:.LANCHOR0
	mov	w20, w3
	ldr	x4, [x4,608]
	strh	w23, [x4,586]
.L1333:
	cmp	w25, w3
	bcs	.L1292
	mov	w22, w23
	mov	w25, w3
.L1292:
	cmp	w2, w9
	beq	.L1293
.L1291:
	ldr	w4, [x29,140]
	add	w27, w27, 1
	cmp	w21, w2
	add	w4, w4, w2
	str	w4, [x29,140]
	uxth	w27, w27
	bls	.L1294
	add	x4, x19, :lo12:.LANCHOR0
	mov	w21, w2
	ldr	x4, [x4,608]
	strh	w23, [x4,584]
.L1294:
	cmp	w24, w2
	bcs	.L1293
	mov	w28, w23
	mov	w24, w2
.L1293:
	cmp	w3, 9
	bls	.L1336
	cmp	w2, 9
	bhi	.L1287
.L1336:
	ldr	w2, [x10,#:lo12:.LANCHOR2]
	tbz	x2, 8, .L1287
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
.L1287:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1286
.L1395:
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
	tbz	x2, 10, .L1297
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
.L1297:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1298
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1299
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
	b	.L1298
.L1299:
	tst	w0, 224
	bne	.L1298
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldr	x0, [x1,608]
	ldrh	w3, [x0,590]
	cmp	w3, w2
	bne	.L1298
	ldrh	w0, [x0,586]
	str	x1, [x29,128]
	bl	zftl_remove_free_node
	ldr	x1, [x29,128]
	ldr	x0, [x1,608]
	ldrh	w1, [x0,586]
	strh	w1, [x0,590]
	mov	w1, -1
	strh	w1, [x0,586]
.L1298:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x0,608]
	ldr	x4, [x0,584]
	ldrh	w7, [x1,584]
	lsl	x3, x7, 2
	mov	x1, x7
	add	x8, x4, x3
	tbz	x2, 10, .L1300
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
.L1300:
	ldrb	w0, [x8,2]
	tbz	x0, 3, .L1301
	ldrb	w0, [x8,2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1302
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
	b	.L1301
.L1302:
	tst	w0, 224
	bne	.L1301
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldr	x0, [x1,608]
	ldrh	w3, [x0,588]
	cmp	w3, w2
	bne	.L1301
	ldrh	w0, [x0,584]
	str	x1, [x29,128]
	bl	zftl_remove_free_node
	ldr	x1, [x29,128]
	ldr	x0, [x1,608]
	ldrh	w1, [x0,584]
	strh	w1, [x0,588]
	mov	w1, -1
	strh	w1, [x0,584]
.L1301:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1303
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
.L1303:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	ldr	x1, [x4,584]
	tbz	x0, 10, .L1304
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
.L1304:
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
	tbz	x0, 10, .L1305
	adrp	x0, .LC121
	mov	w1, w26
	add	x0, x0, :lo12:.LC121
	mov	w2, w27
	uxth	w3, w3
	uxth	w4, w4
	bl	printk
.L1305:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	adrp	x28, .LANCHOR4
	tbz	x0, 10, .L1306
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
.L1306:
	add	x1, x28, :lo12:.LANCHOR4
	sub	w0, w24, w21
	str	w0, [x29,140]
	ldrh	w0, [x1,1162]
	ldr	w2, [x29,140]
	cmp	w2, w0
	bgt	.L1307
	ldrh	w1, [x1,1160]
	sub	w0, w25, w20
	cmp	w0, w1
	ble	.L1335
.L1307:
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
.L1309:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,576]
	cmp	w27, w0
	bcs	.L1318
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
	beq	.L1311
	tst	w0, 192
	beq	.L1311
	ubfx	x0, x0, 3, 2
	cmp	w2, 160
	and	w1, w0, 1
	tbnz	x0, 0, .L1393
	cmp	w0, 2
.L1393:
	add	x0, x28, :lo12:.LANCHOR4
	bne	.L1314
	ldrh	w0, [x0,1162]
	ldr	w2, [x29,140]
	cmp	w2, w0
	ble	.L1315
	ldr	w0, [x4,x3]
	add	w2, w21, 2
	ubfx	x0, x0, 11, 8
	cmp	w0, w2
	ble	.L1316
	cbz	w1, .L1315
	ldrh	w0, [x4,x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1315
.L1316:
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
	tbz	x1, 10, .L1315
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,120]
	ldrb	w2, [x1,2]
	b	.L1394
.L1314:
	ldrh	w0, [x0,1160]
	ldr	w2, [x29,136]
	cmp	w2, w0
	ble	.L1315
	ldrh	w0, [x4,x3]
	add	w2, w20, 2
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1317
	cbz	w1, .L1315
	ldr	w0, [x4,x3]
	add	w1, w21, 2
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1315
.L1317:
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
	tbz	x1, 10, .L1315
	ldr	x1, [x0,584]
	ldr	x10, [x0,600]
	add	x1, x1, x3
	ldrh	w5, [x4,x3]
	ldr	w6, [x4,x3]
	ldr	x0, [x29,128]
	ldrb	w2, [x1,2]
.L1394:
	ldrh	w3, [x10,x25,lsl 1]
	mov	w1, w24
	ldrh	w4, [x9,56]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1315:
	cmp	w26, 4
	bhi	.L1318
	cmp	w22, 4
	bhi	.L1318
.L1311:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1309
.L1318:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,608]
	str	w24, [x0,580]
	b	.L1308
.L1335:
	mov	w26, 0
	mov	w22, w26
.L1308:
	cbz	w21, .L1321
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w0, [x0,134]
.L1322:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1396
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w21
	bcc	.L1323
	ldrb	w5, [x5,2]
	tst	w5, 24
	beq	.L1323
	sub	w2, w2, w21
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1323:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1322
.L1396:
	ldr	x0, [x1,2864]
	ldrh	w1, [x0,72]
	add	w1, w21, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w21
	bls	.L1321
	sub	w21, w1, w21
	strh	w21, [x0,98]
.L1321:
	cbz	w20, .L1327
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w0, [x0,134]
.L1328:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,576]
	cmp	w2, w0
	bls	.L1397
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,584]
	add	x5, x4, x3
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L1329
	ldrb	w5, [x5,2]
	and	w5, w5, 24
	cmp	w5, 16
	beq	.L1329
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1329:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1328
.L1397:
	ldr	x0, [x1,2864]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L1327
	sub	w20, w1, w20
	strh	w20, [x0,96]
.L1327:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1285
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x19,600]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x19,3300]
	cmp	w2, w1
	bhi	.L1285
	mov	w1, 0
	mov	w2, 1
	add	w22, w22, 1
	bl	gc_add_sblk
	b	.L1285
.L1334:
	mov	w26, 0
	mov	w22, w26
.L1285:
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
.L1399:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,576]
	cmp	w19, w1
	bge	.L1429
	sxtw	x22, w19
	ldr	x27, [x0,584]
	ldr	w1, [x29,108]
	add	x27, x27, x22, lsl 2
	ldrb	w2, [x27,3]
	cbz	w2, .L1400
	ldrb	w5, [x0,3276]
	ldrh	w6, [x0,3274]
	ldr	x4, [x0,2864]
	mov	w0, 0
	mov	w1, w0
.L1401:
	cmp	w0, w5
	bge	.L1430
	ldrb	w2, [x27,3]
	asr	w2, w2, w0
	tbnz	x2, 0, .L1402
	add	w1, w6, w1
	sxth	w1, w1
	b	.L1403
.L1402:
	ldrh	w2, [x4,146]
	add	w2, w2, 1
	strh	w2, [x4,146]
.L1403:
	add	w0, w0, 1
	b	.L1401
.L1430:
	cbz	w1, .L1405
	sdiv	w1, w23, w1
	add	w1, w1, 1
	sxth	w1, w1
	b	.L1400
.L1405:
	ldrb	w0, [x27,2]
	orr	w0, w0, -32
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	strh	w25, [x0,x22,lsl 1]
.L1400:
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
	beq	.L1408
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1408
	ldr	x2, [x20,608]
	ldrh	w4, [x2,16]
	cmp	w19, w4
	beq	.L1408
	ldrh	w4, [x2,48]
	cmp	w19, w4
	beq	.L1408
	ldrh	w2, [x2,80]
	cmp	w19, w2
	beq	.L1408
	cmp	w0, 64
	bne	.L1410
	uxth	w27, w19
	add	x0, x20, 616
	mov	w1, w27
	add	x2, x20, 2850
	b	.L1427
.L1410:
	cmp	w0, 96
	bne	.L1411
	uxth	w27, w19
	add	x0, x20, 3288
	mov	w1, w27
	add	x2, x20, 2852
	b	.L1427
.L1411:
	cmp	w0, 160
	bne	.L1412
	uxth	w27, w19
	add	x0, x20, 3280
	mov	w1, w27
	add	x2, x20, 2854
.L1427:
	bl	_insert_data_list
	ldr	x0, [x20,600]
	ldrh	w0, [x0,x22,lsl 1]
	cmp	w0, 7
	bhi	.L1408
	mov	w0, w27
	mov	w1, 1
	mov	w2, w28
	b	.L1425
.L1412:
	cbnz	w0, .L1408
	ldr	x0, [x20,600]
	ldrh	w2, [x0,x22,lsl 1]
	cbz	w2, .L1413
	mov	x0, x26
	mov	w1, w19
	bl	printk
	ldrb	w0, [x27,2]
	tbz	x0, 4, .L1414
	mov	w1, 5
	b	.L1421
.L1414:
	mov	w1, 2
.L1421:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x27,2]
	mov	w1, 1
	mov	w0, w19
.L1425:
	bl	gc_add_sblk
	b	.L1408
.L1413:
	ands	w1, w1, 24
	add	x2, x21, :lo12:.LANCHOR0
	bne	.L1416
	add	x0, x2, 3312
	mov	w1, w19
	add	x2, x2, 2844
	b	.L1423
.L1416:
	cmp	w1, 16
	bne	.L1417
	add	x0, x2, 3320
	mov	w1, w19
	add	x2, x2, 2846
	b	.L1423
.L1417:
	add	x0, x2, 3328
	mov	w1, w19
	add	x2, x2, 2848
.L1423:
	bl	_insert_free_list
.L1408:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1399
.L1429:
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
.L1432:
.L1435:
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
	beq	.L1433
	ldrb	w3, [x0,3276]
	mul	w3, w1, w3
	uxth	w3, w3
.L1433:
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
	cbnz	w0, .L1431
	mov	x0, x20
	mov	w1, w22
	bl	printk
	ldr	x0, [x27,600]
	strh	w24, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L1435
.L1431:
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
	bls	.L1438
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 78
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1438:
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
.L1439:
	cmp	w7, w3, uxth
	bls	.L1464
	add	x1, x4, x3, lsl 2
	mov	x2, 0
	ldr	w1, [x1,704]
	lsr	w1, w1, w6
	and	w1, w1, w0
	udiv	w1, w1, w8
	uxth	w1, w1
.L1440:
	cmp	w9, w2, uxth
	bls	.L1465
	add	x5, x4, x2, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L1441
	ldrh	w5, [x28,x2,lsl 1]
	add	w5, w5, 1
	strh	w5, [x28,x2,lsl 1]
.L1441:
	add	x2, x2, 1
	b	.L1440
.L1465:
	add	x3, x3, 1
	b	.L1439
.L1464:
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
.L1444:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,1164]
	cmp	w0, w19
	bls	.L1466
	add	x0, x21, :lo12:.LANCHOR0
	sxtw	x22, w19
	add	x1, x22, 208
	ldr	x4, [x0,2864]
	ldrb	w2, [x0,3338]
	ldrh	w3, [x4,x1,lsl 1]
	ldrh	w1, [x4,692]
	sdiv	w1, w1, w2
	cmp	w1, w3
	bne	.L1445
	ldrb	w1, [x0,3276]
	ldrh	w0, [x0,3300]
	mul	w0, w1, w0
	strh	w0, [x28,w19,sxtw 1]
.L1445:
	ldrh	w0, [x28,x22,lsl 1]
	cmp	w20, w0
	bls	.L1446
	cmp	w0, wzr
	csel	w25, w25, w19, eq
	csel	w20, w20, w0, eq
.L1446:
	cbnz	w0, .L1448
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1448
	ldr	w0, [x26,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1449
	ldrh	w4, [x4,688]
	mov	x0, x24
	mov	w1, w19
	mov	w2, 0
	bl	printk
.L1449:
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
.L1448:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1444
.L1466:
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
	.global	flash_into_data_init
	.type	flash_into_data_init, %function
flash_into_data_init:
	adrp	x1, .LANCHOR2
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x1,#:lo12:.LANCHOR2]
	stp	x19, x20, [sp,16]
	mov	x20, x1
	tbz	x0, 12, .L1469
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 100
	add	x2, x2, 112
	bl	printk
.L1469:
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
	add	x1, x1, 376
	mov	w2, 32
	add	x0, x0, 48
	bl	ftl_memcpy
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_into_data_init, .-flash_into_data_init
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L1474:
	cmp	w2, w3
	bls	.L1476
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1474
.L1476:
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
.L1479:
	cmp	w1, w2
	bls	.L1481
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L1479
.L1481:
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
	cbnz	w2, .L1488
	add	x1, x4, x1
	b	.L1490
.L1488:
	add	x0, x4, x1
	mov	x1, x6
.L1490:
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
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	adrp	x4, .LANCHOR4+1176
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1176]
	ldr	x4, [x4,48]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlWrite, .-FtlWrite
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
.L1505:
	cmp	x19, x26
	beq	.L1513
	cbnz	w20, .L1506
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L1506:
	cmp	w25, 4
	mov	x0, x21
	bne	.L1507
	ldr	w1, [x22,x19,lsl 2]
	b	.L1512
.L1507:
	cmp	w25, 2
	bne	.L1509
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L1512
.L1509:
	ldrb	w1, [x22,x19]
.L1512:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L1510
	adrp	x1, .LC131
	mov	x0, x24
	add	x1, x1, :lo12:.LC131
	mov	w20, 0
	bl	printk
.L1510:
	add	x19, x19, 1
	b	.L1505
.L1513:
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
	bne	.L1515
	ldr	x1, [x29,136]
	mov	w0, -89
	strb	w0, [x1,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1570
.L1515:
	cmp	w25, 3
	bne	.L1517
	mov	x4, 0
.L1518:
	sub	w0, w4, #80
	strb	w0, [x26,x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L1518
	mov	w21, w4
	mov	w28, w4
	b	.L1516
.L1517:
	cmp	w25, 4
	bne	.L1519
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
	b	.L1516
.L1519:
	cmp	w25, 5
	bne	.L1520
	mov	w0, 56
	strb	w0, [x27,128]
	mov	w0, 57
	strb	w0, [x27,129]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x27,130]
	mov	w0, 59
	strb	w0, [x27,131]
	b	.L1604
.L1520:
	cmp	w25, 6
	bne	.L1521
	mov	w0, 14
	strb	w0, [x27,128]
	mov	w0, 15
	strb	w0, [x27,129]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x27,130]
	mov	w0, 17
	strb	w0, [x27,131]
	b	.L1604
.L1521:
	cmp	w25, 7
	bne	.L1570
	mov	x0, 0
.L1522:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1522
	mov	w0, -44
	mov	w21, 12
	strb	w0, [x27,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x27,137]
	b	.L1516
.L1570:
	mov	w21, 7
.L1604:
	mov	w28, 4
.L1516:
	sub	w0, w25, #1
	cmp	w0, 1
	bhi	.L1601
	ldr	x0, [x29,128]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w27, 55
	add	x24, x24, 408
.L1523:
	ldrb	w0, [x19,633]
	cmp	w0, w23
	bls	.L1530
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
.L1525:
	str	w27, [x22,2056]
	ldrb	w0, [x26,x25]
	str	w0, [x22,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x22,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	cmp	w28, w25, uxtb
	bhi	.L1525
	mov	x0, 0
.L1526:
	add	w2, w0, 8
	mov	x1, 0
.L1527:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w5, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1527
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1526
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1523
.L1601:
	sub	w0, w25, #3
	cmp	w0, 4
	bhi	.L1530
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
.L1531:
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,633]
	cmp	w1, w20
	bhi	.L1569
.L1530:
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
.L1569:
	add	x1, x0, x20, sxtw
	ldrb	w1, [x1,736]
	ldr	x0, [x0,536]
	mov	x24, 160
	ubfiz	x2, x1, 8, 8
	ldr	x3, [x29,136]
	add	x23, x0, x2
	madd	x24, x1, x24, x3
	mov	w1, 255
	str	w1, [x23,2056]
	str	x1, [x29,104]
	add	x24, x24, 32
	bl	nandc_wait_flash_ready
	mov	w0, 54
	cmp	w25, 4
	str	w0, [x23,2056]
	ldr	x1, [x29,104]
	bne	.L1532
	mov	w0, 64
	str	w1, [x23,2052]
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1605
.L1532:
	cmp	w19, 1
	bhi	.L1534
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1606
.L1534:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1605:
	str	w0, [x23,2052]
	mov	w0, 77
.L1606:
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
	bne	.L1535
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L1536
.L1535:
	str	wzr, [x23,2052]
.L1536:
	mov	w7, 2
	str	w7, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	str	x7, [x29,104]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w1, 16
	bls	.L1538
	ldr	x7, [x29,104]
	cmp	w25, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L1538:
	adrp	x0, .LANCHOR4
	mov	x7, 0
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1192]
.L1539:
	ldr	w8, [x23,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L1539
	cmp	w25, 7
	bne	.L1540
	mov	w1, 0
.L1542:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1541
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L1541
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1542
	b	.L1543
.L1541:
	cmp	w1, 7
	bne	.L1544
.L1543:
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1545:
	b	.L1545
.L1540:
	cmp	w25, 6
	bne	.L1544
	mov	x1, 0
.L1546:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L1544
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L1544
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1546
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1548:
	b	.L1548
.L1544:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x9, [x0,1192]
	mov	x0, 0
.L1549:
	ldr	w1, [x29,120]
	cmp	w1, w0
	ble	.L1608
	ldr	w1, [x23,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L1549
.L1608:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w8, w22
	mov	w7, 8
	ldr	x11, [x0,1192]
.L1552:
	mov	w0, 0
.L1551:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L1551
	ldr	w0, [x29,124]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L1552
	mov	x7, 0
	mov	w15, 1
.L1553:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w10, w1
	ldr	x14, [x0,1192]
.L1557:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L1555:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L1555
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L1557
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L1553
	add	x1, x26, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w8, w7
	ldr	x1, [x1,1192]
.L1560:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L1560
	cmp	w8, 7
	ble	.L1561
	adrp	x0, .LC133
	mov	w2, 1
	add	x0, x0, :lo12:.LC133
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC132
	mov	w1, 0
	add	x0, x0, :lo12:.LC132
	bl	printk
.L1562:
	b	.L1562
.L1561:
	cmp	w25, 6
	mov	w7, 4
	beq	.L1563
	cmp	w25, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L1563:
	mov	w8, 0
.L1564:
	mov	x1, 0
.L1565:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x24,w10,sxtw]
	bhi	.L1565
	add	w8, w8, 1
	ldr	x1, [x29,112]
	cmp	w8, w21
	add	w0, w0, w7
	add	x9, x9, x1
	blt	.L1564
	mov	w24, 255
	str	w24, [x23,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w0, 56
	bhi	.L1607
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
.L1607:
	str	w0, [x23,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1531
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	adrp	x0, .LANCHOR2+395
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR2+395]
	sub	w1, w0, #1
	uxtb	w1, w1
	cmp	w1, 6
	bhi	.L1609
	bl	hynix_get_read_retry_default
.L1609:
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
	bne	.L1612
	ldr	x24, [x20,536]
	ldr	w0, [x24,16]
	str	w0, [x29,64]
	ldr	w20, [x24,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1632
	adrp	x21, .LC137
	adrp	x23, .LC136
	add	x21, x21, :lo12:.LC137
	add	x23, x23, :lo12:.LC136
	b	.L1614
.L1632:
	adrp	x21, .LC135
	adrp	x22, .LC136
	mov	w20, 0
	add	x21, x21, :lo12:.LC135
	add	x22, x22, :lo12:.LC136
.L1613:
	ldr	w2, [x24,64]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 6
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1616
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1615
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1615
	ldr	w1, [x29,72]
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1616
.L1615:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1613
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
	tbz	x0, 13, .L1613
	add	x0, x23, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,1200]
.L1616:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1620
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
	b	.L1660
.L1622:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,536]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1614
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
.L1614:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1622
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1620
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
	b	.L1659
.L1612:
	ldr	x23, [x20,536]
	ldr	w0, [x23,8]
	str	w0, [x29,64]
	ldr	w20, [x23,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1633
	adrp	x21, .LC137
	adrp	x24, .LC136
	add	x21, x21, :lo12:.LC137
	add	x24, x24, :lo12:.LC136
	b	.L1625
.L1633:
	adrp	x21, .LC135
	adrp	x22, .LC136
	mov	w20, 0
	add	x21, x21, :lo12:.LC135
	add	x22, x22, :lo12:.LC136
.L1624:
	ldr	w2, [x23,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1627
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,536]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1626
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1626
	ldr	w1, [x29,72]
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	printk
	b	.L1627
.L1626:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1624
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
	b	.L1624
.L1627:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1620
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
.L1660:
	mov	w2, 0
	b	.L1658
.L1631:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,536]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1625
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
.L1625:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1631
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 680
	ldr	w0, [x20,32]
	cbz	w0, .L1620
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
.L1659:
	mov	w2, 1
.L1658:
	bl	rknand_dma_unmap_single
.L1620:
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
	cbnz	w20, .L1691
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w1, [x2,516]
	cmp	w1, 9
	bne	.L1663
	ldr	x5, [x2,536]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1664:
	cmp	w3, w22
	bcs	.L1700
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
	tbnz	x1, 2, .L1683
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1683
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1666
	ubfx	x1, x1, 3, 7
	b	.L1667
.L1666:
	ubfx	x1, x1, 19, 7
.L1667:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1665
.L1683:
	mov	w0, -1
.L1665:
	add	w3, w3, 1
	b	.L1664
.L1700:
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
	bne	.L1670
	adrp	x0, .LC139
	mov	w1, w20
	add	x0, x0, :lo12:.LC139
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,536]
	str	w20, [x0]
	mov	w0, -1
.L1670:
	tbz	x20, 13, .L1671
	adrp	x1, .LANCHOR4+1200
	ldrb	w1, [x1,#:lo12:.LANCHOR4+1200]
	cbz	w1, .L1671
	adrp	x0, .LC140
	mov	w1, w20
	add	x0, x0, :lo12:.LC140
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,536]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1672
.L1663:
	ldrb	w0, [x2,724]
	mov	w5, 128
	lsr	w6, w22, 1
	mov	w3, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w5, w0, w5, cc
	mov	w1, w20
	mov	w4, w20
.L1674:
	cmp	w4, w6
	add	w7, w1, w5
	bcs	.L1701
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
	b	.L1674
.L1701:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,536]
	mov	w0, w2
.L1676:
	cmp	w2, w22
	bcs	.L1702
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1686
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1686
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
	bls	.L1678
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1699
.L1678:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1699:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1677
.L1686:
	mov	w0, -1
.L1677:
	add	w2, w2, 1
	b	.L1676
.L1702:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,536]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1671
	adrp	x0, .LC141
	mov	w1, w20
	add	x0, x0, :lo12:.LC141
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,536]
	str	w20, [x0]
	b	.L1672
.L1671:
	cmn	w0, #1
	beq	.L1691
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1691
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1691
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1691
.L1672:
	mov	w0, -1
.L1691:
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
	tbz	x0, 4, .L1704
	adrp	x0, .LC142
	mov	w1, w21
	add	x0, x0, :lo12:.LC142
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L1704:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	cbnz	w24, .L1705
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1706
.L1705:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1707
	sxtw	x0, w21
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w24, [x0,8]
	b	.L1706
.L1707:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1706:
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
	cbz	w24, .L1708
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1708
	add	w20, w20, w20, lsl 1
	sub	w0, w20, #1
	add	w0, w0, w24
	b	.L1719
.L1708:
	mov	w0, w20
.L1719:
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
.L1721:
	add	x0, x23, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	cmp	w20, w0
	bcs	.L1725
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
	tbz	x0, 12, .L1722
	ldr	x0, [x29,104]
	mov	w1, w20
	mov	w2, w21
	mov	w3, w20
	mov	w4, w28
	str	x6, [x29,96]
	bl	printk
	ldr	x6, [x29,96]
.L1722:
	cmn	w6, #1
	beq	.L1723
	adrp	x0, .LANCHOR4
	cmn	w28, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w28, w28, w6, ne
	ldr	x25, [x0,1192]
	ldr	x26, [x0,1208]
	ldr	w0, [x29,124]
	cmp	w6, w0
	bcc	.L1730
.L1723:
	mov	w20, w27
	b	.L1721
.L1730:
	mov	w28, w6
.L1725:
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
	bcc	.L1727
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1727:
	cmn	w28, #1
	beq	.L1731
	cmp	w28, 256
	bne	.L1728
.L1731:
	adrp	x0, .LC144
	mov	w1, w20
	add	x0, x0, :lo12:.LC144
	mov	w2, w21
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1728:
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
	cbnz	w19, .L1745
	str	x0, [x29,112]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w19, -1
	mov	w28, 1
	str	x0, [x29,104]
.L1752:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1746
	mov	w1, w28
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x25, x0
	str	w1, [x0,8]
	b	.L1747
.L1746:
	mov	w1, w28
	bl	toshiba_3d_set_slc_rr_para
.L1747:
	ldr	w4, [x29,124]
	mov	w0, w22
	mov	w1, w24
	mov	x2, x26
	mov	x3, x27
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1748
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w28
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1748:
	cmn	w4, #1
	beq	.L1749
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
	bcc	.L1768
.L1749:
	add	w28, w28, 1
	cmp	w28, w21
	bne	.L1752
	b	.L1751
.L1768:
	mov	w21, w28
	mov	w19, w4
.L1751:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1753
	mov	w1, 0
	mov	w2, w1
	b	.L1791
.L1753:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1754
.L1745:
	mov	x28, x0
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,104]
.L1761:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1755
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x25, x28
	mov	w1, 93
	b	.L1790
.L1755:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x25, x28
	mov	w1, 38
.L1790:
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
	tbz	x0, 4, .L1757
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w24
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1757:
	cmn	w4, #1
	beq	.L1758
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
	bcc	.L1769
.L1758:
	ldr	w0, [x29,112]
	add	w21, w21, 1
	cmp	w21, w0
	bne	.L1761
	b	.L1760
.L1769:
	mov	w19, w4
.L1760:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	mov	x0, x23
	bne	.L1762
	mov	w1, 0
	mov	w2, 1
.L1791:
	bl	toshiba_tlc_set_rr_para
	b	.L1754
.L1762:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1754:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 36
	bne	.L1763
	ubfiz	x22, x22, 8, 8
	mov	w0, 85
	add	x25, x25, x22
	str	w0, [x25,2056]
	mov	w0, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2048]
	str	w0, [x25,2056]
.L1763:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,724]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1764
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1764:
	cmn	w19, #1
	beq	.L1770
	cmp	w19, 256
	bne	.L1765
.L1770:
	adrp	x0, .LC147
	mov	w1, w21
	add	x0, x0, :lo12:.LC147
	mov	w2, w24
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1765:
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
	bls	.L1793
	ldrb	w0, [x1,672]
	cbz	w0, .L1794
	mov	w0, 1
	mov	w25, 1
	bl	nandc_set_if_mode
.L1794:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1793:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w24, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1795:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1826
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1796
	bl	sandisk_set_rr_para
	b	.L1797
.L1796:
	bl	toshiba_set_rr_para
.L1797:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	cmp	w0, 34
	bne	.L1798
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1216]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1798
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1798:
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
	beq	.L1801
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
	bcc	.L1803
.L1801:
	add	w20, w20, 1
	b	.L1795
.L1826:
	mov	w28, w24
.L1803:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,725]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1805
	bl	sandisk_set_rr_para
	b	.L1806
.L1805:
	bl	toshiba_set_rr_para
.L1806:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,724]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L1807
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L1807:
	cmn	w28, #1
	beq	.L1813
	cmp	w28, 256
	bne	.L1808
.L1813:
	adrp	x0, .LC147
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC147
	mov	w1, w20
	mov	w3, w20
	mov	w4, w28
	bl	printk
.L1808:
	bl	nandc_wait_flash_ready
	cbz	w25, .L1810
	mov	w0, 4
	bl	nandc_set_if_mode
.L1810:
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
.L1828:
	cmp	w6, w24
	bcs	.L1832
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
	beq	.L1830
	add	x1, x7, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1192]
	ldr	x26, [x1,1208]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1837
.L1830:
	add	w6, w6, 1
	b	.L1828
.L1837:
	mov	w19, w0
.L1832:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,724]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1834
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1834:
	cmn	w19, #1
	beq	.L1838
	cmp	w19, 256
	bne	.L1835
.L1838:
	adrp	x0, .LC148
	mov	w1, w6
	add	x0, x0, :lo12:.LC148
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1835:
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
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	w0, [x29,124]
	mov	w20, w1
	mov	x23, x2
	mov	x24, x3
	str	w4, [x29,116]
	bl	nandc_get_ddr_para
	mov	w26, 0
	str	w0, [x29,120]
	adrp	x0, .LC149
	mov	w22, w26
	add	x0, x0, :lo12:.LC149
	mov	w28, w26
	mov	w27, w26
	mov	w25, 1024
	mov	w21, 6
	mov	w19, -1
	str	x0, [x29,104]
.L1853:
	mov	w0, w21
	bl	nandc_set_ddr_para
	ldr	w4, [x29,116]
	mov	w1, w20
	ldrb	w0, [x29,124]
	mov	x2, x23
	mov	x3, x24
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1848
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w20
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1848:
	add	w0, w25, 1
	cmp	w4, w0
	bhi	.L1849
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x23, [x0,1192]
	ldr	x24, [x0,1208]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,724]
	cmp	w4, w0, lsr 2
	bcs	.L1858
	add	w27, w27, 1
	cmp	w27, 7
	bls	.L1858
	sub	w26, w21, w27
	mov	w25, w4
	mov	w19, 0
	b	.L1851
.L1849:
	cmp	w28, w27
	bcs	.L1859
	cmp	w27, 7
	sub	w22, w26, w27
	bhi	.L1852
	mov	w28, w27
	b	.L1859
.L1858:
	mov	w26, w21
	mov	w25, w4
	mov	w19, 0
	b	.L1850
.L1859:
	mov	w27, 0
.L1850:
	add	w21, w21, 2
	cmp	w21, 50
	bne	.L1853
.L1851:
	cmp	w28, w27
	csel	w26, w26, w22, ls
.L1852:
	cbz	w26, .L1854
	adrp	x0, .LANCHOR0+724
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+724]
	udiv	w0, w0, w1
	cmp	w25, w0
	bcs	.L1854
	adrp	x0, .LC150
	mov	w1, w26
	add	x0, x0, :lo12:.LC150
	bl	printk
	mov	w0, w26
	b	.L1867
.L1854:
	ldrb	w0, [x29,120]
.L1867:
	bl	nandc_set_ddr_para
	cbz	w19, .L1856
	adrp	x0, .LC151
	ldr	w1, [x29,124]
	add	x0, x0, :lo12:.LC151
	mov	w2, w20
	mov	w25, w19
	bl	printk
.L1856:
	mov	w0, w25
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w19, w1
	mov	x25, x2
	ldrb	w0, [x0,633]
	mov	x24, x3
	mov	w23, w4
	ubfx	x26, x19, 24, 2
	cmp	w0, w21
	bhi	.L1869
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 377
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1869:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, x21, sxtw
	ldrb	w22, [x1,736]
	ldrb	w0, [x0,633]
	cmp	w0, w21
	bcs	.L1870
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L1870
	adrp	x0, .LC152
	mov	w1, w22
	add	x0, x0, :lo12:.LC152
	mov	w2, w21
	mov	w3, w19
	bl	printk
.L1870:
	cbnz	w26, .L1871
	ldrb	w1, [x20,#:lo12:.LANCHOR0]
	add	x0, x20, :lo12:.LANCHOR0
	cbz	w1, .L1872
	ldrb	w0, [x0,1]
	cbz	w0, .L1871
.L1872:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,1]
	sub	w19, w19, w0
	cbz	w2, .L1873
	add	w19, w0, w19, lsl 1
	b	.L1871
.L1873:
	add	x1, x1, 4
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1871:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w21, w0
	bne	.L1880
	add	x26, x20, :lo12:.LANCHOR0
	ldrb	w27, [x26,720]
	cbnz	w27, .L1875
.L1878:
	adrp	x0, .LANCHOR4+1224
	ldr	x5, [x0,#:lo12:.LANCHOR4+1224]
	cbnz	x5, .L1876
	b	.L1877
.L1875:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	strb	wzr, [x26,720]
	bl	flash_read_page
	strb	w27, [x26,720]
	cmn	w0, #1
	beq	.L1878
	b	.L1896
.L1876:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	blr	x5
	cmn	w0, #1
	bne	.L1896
.L1877:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,720]
	bl	printk
	ldrb	w0, [x20,672]
	cbz	w0, .L1880
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_ddr_tuning_read
.L1896:
	mov	w21, w0
.L1880:
	mov	w0, w21
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
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
	ldrh	w26, [x0,402]
	mov	w0, w23
	sxth	w19, w19
	mov	x21, x3
	mov	w25, w4
	mov	x27, x20
	mul	w26, w22, w26
	add	w1, w26, w19
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L1898
	mov	w28, 0
	mov	w5, 2
.L1899:
	cmp	w28, w19
	bgt	.L1898
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
	bne	.L1900
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1899
.L1900:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L1899
.L1898:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1903
	adrp	x0, .LC154
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC154
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L1903:
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
	tbz	x0, 12, .L1909
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR3
	adrp	x0, .LC155
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC155
	mov	w1, 49
	ldr	x4, [x4,728]
	add	x2, x2, 160
	mov	w3, 2048
	bl	printk
.L1909:
	mov	w22, 4
	adrp	x24, .LANCHOR4
.L1912:
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
	beq	.L1910
	ldr	x2, [x19,728]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L1911
.L1910:
	add	w22, w22, 1
	cmp	w22, 16
	bne	.L1912
.L1936:
	mov	w0, -1
	b	.L1935
.L1911:
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
	beq	.L1914
	ldr	x0, [x19,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1914
	ldr	w21, [x0,4]
.L1914:
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
	beq	.L1915
	ldr	x0, [x22,728]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1915
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L1915
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,1240]
	strb	w0, [x19,1241]
.L1915:
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
.L1916:
	tbnz	w19, #31, .L1937
	add	x22, x20, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w0, 0
	add	w1, w19, w21
	mov	w4, 4
	ldr	x2, [x22,728]
	ldr	x3, [x3,1232]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1917
	ldr	x0, [x22,728]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	bne	.L1917
.L1919:
	mov	w0, 0
	b	.L1935
.L1917:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1916
.L1937:
	cmn	w19, #1
	bne	.L1919
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	ldr	x1, [x20,728]
	ldr	w1, [x1]
	bl	printk
	b	.L1936
.L1935:
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
	bne	.L1939
	ldrb	w0, [x21,780]
	tbz	x0, 0, .L1939
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,672]
	b	.L1940
.L1939:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,672]
.L1940:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.type	id_block_read_data.constprop.27, %function
id_block_read_data.constprop.27:
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
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	str	x0, [x29,112]
.L1945:
	cmp	w22, w27
	bcs	.L1982
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
	cbnz	w1, .L1946
	mov	w20, w0
	b	.L1947
.L1946:
	ldrb	w1, [x19,1]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L1947:
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
.L1948:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L1955
	ldrb	w6, [x19,720]
	cbnz	w6, .L1950
.L1953:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x6, [x0,1224]
	cbnz	x6, .L1951
	b	.L1952
.L1950:
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
	beq	.L1953
	b	.L1981
.L1951:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	blr	x6
	cmn	w0, #1
	bne	.L1981
.L1952:
	ldrb	w0, [x19,672]
	cbz	w0, .L1955
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tuning_read
.L1981:
	mov	w20, w0
.L1955:
	cmn	w20, #1
	bne	.L1956
	cmp	w28, 16
	beq	.L1956
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L1948
.L1956:
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
	beq	.L1958
	cbnz	w25, .L1958
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L1958
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L1959
.L1958:
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
.L1959:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L1945
.L1982:
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
	.size	id_block_read_data.constprop.27, .-id_block_read_data.constprop.27
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
	bhi	.L1984
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 782
	add	x1, x1, 184
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1984:
	add	x0, x21, :lo12:.LANCHOR0
	add	x22, x0, x22, sxtw
	ldrb	w22, [x22,736]
	ldr	x25, [x0,536]
	mov	w0, w22
	bl	nandc_cs
	cbnz	w20, .L1985
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L1985:
	add	x3, x21, :lo12:.LANCHOR0
	uxtb	x5, w22
	and	w2, w19, 255
	lsr	w1, w19, 8
	ldrb	w0, [x3,656]
	cmp	w0, 1
	lsr	w0, w19, 16
	bne	.L1986
	add	x5, x25, x5, lsl 8
	mov	w3, 6
	b	.L1999
.L1986:
	ldr	x3, [x3,624]
	add	x5, x25, x5, lsl 8
	ldrb	w3, [x3,12]
	cmp	w3, 3
	bne	.L1988
	mov	w3, 5
.L1999:
	str	w3, [x5,2056]
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w2, [x5,2052]
	str	w1, [x5,2052]
	str	w0, [x5,2052]
	b	.L1997
.L1988:
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
.L1997:
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w20, .L1989
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1989
	sub	w20, w20, #1
	add	w0, w19, w19, lsl 1
	add	w0, w20, w0
	b	.L1998
.L1989:
	mov	w0, w19
.L1998:
	bl	nandc_set_seed
	adrp	x2, .LANCHOR2+385
	mov	x3, x24
	mov	x4, x23
	mov	w1, 0
	mov	w0, w22
	ldrb	w2, [x2,#:lo12:.LANCHOR2+385]
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
	bhi	.L2001
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 904
	add	x1, x1, 216
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2001:
	add	x0, x19, :lo12:.LANCHOR0
	add	x21, x0, x21, sxtw
	ldrb	w23, [x21,736]
	ldr	x21, [x0,536]
	mov	w0, w23
	bl	nandc_cs
	cbnz	w22, .L2002
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
.L2002:
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
	cbz	w22, .L2003
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,624]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L2003
	sub	w0, w22, #1
	add	w1, w20, w20, lsl 1
	add	w0, w0, w1
	b	.L2035
.L2003:
	mov	w0, w20
.L2035:
	adrp	x21, .LANCHOR2
	bl	nandc_set_seed
	add	x27, x21, :lo12:.LANCHOR2
	mov	w0, w23
	mov	w1, 0
	mov	x3, x25
	mov	x4, x26
	add	x27, x27, 376
	ldrb	w2, [x27,9]
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L2005
	add	x28, x19, :lo12:.LANCHOR0
	ldrb	w5, [x28,720]
	cbz	w5, .L2006
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
	cbnz	w22, .L2007
.L2014:
	ldrb	w2, [x19,#:lo12:.LANCHOR0]
	add	x1, x19, :lo12:.LANCHOR0
	cbz	w2, .L2007
	ldrb	w1, [x1,724]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2007
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w1, [x21,395]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 3
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L2022
.L2007:
	cmn	w0, #1
	bne	.L2022
.L2015:
	adrp	x0, .LANCHOR4+1224
	ldr	x5, [x0,#:lo12:.LANCHOR4+1224]
	cbnz	x5, .L2010
.L2013:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	mov	w1, 0
	mov	w2, w24
	mov	w3, -1
	ldrb	w4, [x19,720]
	bl	printk
	ldrb	w0, [x19,672]
	cbnz	w0, .L2011
	mov	w0, -1
	b	.L2022
.L2010:
	add	x4, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w20, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x4,385]
	blr	x5
	cmn	w0, #1
	bne	.L2022
	b	.L2013
.L2011:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w20, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x21,385]
	bl	flash_ddr_tuning_read
	b	.L2022
.L2005:
	cbz	w22, .L2014
	b	.L2022
.L2006:
	cbz	w22, .L2014
	b	.L2015
.L2022:
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
	beq	.L2120
	add	x0, x0, 784
	mov	w1, 0
	add	x0, x0, x21, lsl 6
	ldrb	w2, [x0,58]
	ldr	w20, [x0,40]
	sub	w3, w2, #1
	cmp	w3, 9
	bhi	.L2038
	adrp	x0, .L2040
	add	x0, x0, :lo12:.L2040
	ldrh	w0, [x0,w3,uxtw #1]
	adr	x1, .Lrtx2040
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx2040:
	.section	.rodata
	.align	0
	.align	2
.L2040:
	.2byte	(.L2039 - .Lrtx2040) / 4
	.2byte	(.L2041 - .Lrtx2040) / 4
	.2byte	(.L2042 - .Lrtx2040) / 4
	.2byte	(.L2042 - .Lrtx2040) / 4
	.2byte	(.L2042 - .Lrtx2040) / 4
	.2byte	(.L2043 - .Lrtx2040) / 4
	.2byte	(.L2044 - .Lrtx2040) / 4
	.2byte	(.L2045 - .Lrtx2040) / 4
	.2byte	(.L2042 - .Lrtx2040) / 4
	.2byte	(.L2045 - .Lrtx2040) / 4
	.text
.L2039:
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2120
	add	x3, x19, :lo12:.LANCHOR0
	add	x0, x3, 784
	add	x0, x0, x21, lsl 6
	ldr	x2, [x0,16]
	ldr	x1, [x0,8]
	cbz	x2, .L2047
	ldrb	w4, [x0,56]
	ldrb	w0, [x3,2832]
	cmp	w4, w0
	csel	x1, x1, x2, ne
.L2047:
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
	b	.L2120
.L2041:
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	add	x2, x19, :lo12:.LANCHOR0
	lsl	x4, x21, 6
	add	x3, x2, 784
	add	x1, x3, x4
	ldrb	w20, [x3,x4]
	tbz	x0, 6, .L2120
	add	x3, x3, x20, lsl 6
	ldr	x0, [x1,16]
	ldr	x27, [x1,8]
	ldr	x23, [x3,8]
	cbz	x0, .L2048
	ldrb	w3, [x1,56]
	ldrb	w1, [x2,2832]
	cmp	w3, w1
	csel	x27, x27, x0, ne
.L2048:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x2, 784
	add	x0, x0, x20, lsl 6
	ldr	x1, [x0,16]
	cbz	x1, .L2049
	ldrb	w3, [x0,56]
	ldrb	w0, [x2,2832]
	cmp	w3, w0
	csel	x23, x23, x1, ne
.L2049:
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
	beq	.L2050
	ldr	w0, [x28,4]
	cmn	w0, #1
	beq	.L2051
	ldr	x1, [x26,8]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2051
.L2050:
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
	beq	.L2052
	ldr	x3, [x24,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2052
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2052
	adrp	x0, .LC159
	ldr	w1, [x26,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC159
	bl	printk
.L2052:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x21, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2051
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2051
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 338
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2051:
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
	beq	.L2053
	add	x3, x3, x20, lsl 6
	ldr	w0, [x3,36]
	cmn	w0, #1
	beq	.L2055
	ldr	x1, [x3,24]
	ldr	w1, [x1,4]
	cmp	w0, w1
	beq	.L2055
.L2053:
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
	beq	.L2057
	ldr	x3, [x21,8]
	ldr	w4, [x3,4]
	cmp	w2, w4
	beq	.L2057
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 6, .L2057
	adrp	x0, .LC159
	ldr	w1, [x24,8]
	ldr	w3, [x3]
	add	x0, x0, :lo12:.LC159
	bl	printk
.L2057:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0,36]
	cmn	w1, #1
	beq	.L2055
	ldr	x0, [x0,24]
	ldr	w0, [x0,4]
	cmp	w1, w0
	beq	.L2055
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 353
	add	x1, x1, 248
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2055:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 12
	add	x19, x19, 784
	add	x20, x19, x20, lsl 6
	ldrb	w0, [x20,2]
	str	w22, [x20,52]
	orr	w0, w0, 8
	strb	w1, [x20,58]
	strb	w0, [x20,2]
	b	.L2120
.L2042:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	tbz	x20, 6, .L2120
	mov	w1, 5
	add	x19, x19, :lo12:.LANCHOR0
	ands	w1, w20, w1
	beq	.L2058
	add	x19, x19, 784
	mov	w0, 11
	add	x19, x19, x21, lsl 6
	mov	w4, 11
	strb	w0, [x19,58]
	adrp	x0, .LC160
	ldrb	w1, [x19,1]
	add	x0, x0, :lo12:.LC160
	ldr	w2, [x19,52]
	ldr	w3, [x19,40]
	bl	printk
	mov	w0, -1
	str	w0, [x19,52]
	b	.L2121
.L2058:
	add	x19, x19, x21, lsl 6
	b	.L2065
.L2045:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 3
	cmp	w2, 10
	mov	w1, 9
	add	x22, x22, 784
	csel	w1, w0, w1, ne
	ubfx	x3, x20, 21, 3
	add	x0, x22, x21, lsl 6
.L2060:
	ldrb	w23, [x0]
	cmp	w23, 255
	beq	.L2122
	add	x0, x22, x23, lsl 6
	ldrb	w2, [x0,58]
	cmp	w2, w1
	bne	.L2060
	ldr	w2, [x0,40]
	ubfx	x2, x2, 21, 3
	cmp	w3, w2
	bne	.L2060
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w20
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w20, w0
	and	w0, w0, 64
	tbz	x20, 6, .L2068
	ands	w0, w20, 15
	add	x22, x22, x21, lsl 6
	beq	.L2063
	adrp	x0, .LC160
	ldrb	w1, [x22,1]
	ldr	w2, [x22,52]
	add	x0, x0, :lo12:.LC160
	ldr	w3, [x22,40]
	mov	w4, 11
	bl	printk
	mov	w0, 11
	strb	w0, [x22,58]
	mov	w0, -1
	str	w0, [x22,52]
.L2062:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 784
	add	x21, x19, x21, lsl 6
	add	x23, x19, x23, lsl 6
	ldrb	w0, [x21,58]
	strb	w0, [x23,58]
	ldr	w0, [x21,52]
	str	w0, [x23,52]
.L2121:
	mov	w1, w20
	b	.L2038
.L2063:
	mov	w1, 12
	str	w0, [x22,52]
	strb	w1, [x22,58]
.L2068:
	mov	w20, w0
	b	.L2062
.L2122:
	mov	w1, -1
	b	.L2038
.L2043:
	mov	w1, 32
	mov	w0, w20
	bl	flash_wait_device_ready
	tbz	x0, 5, .L2120
	ands	w1, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x21, lsl 6
	beq	.L2065
	mov	w1, 11
	strb	w1, [x19,842]
	mov	w1, -1
	str	w1, [x19,836]
	mov	w1, w0
	b	.L2038
.L2065:
	mov	w0, 12
	str	w1, [x19,836]
	strb	w0, [x19,842]
	b	.L2038
.L2044:
	mov	w1, 64
	mov	w0, w20
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2120
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, x21, lsl 6
	str	w0, [x19,836]
	mov	w0, 6
	strb	w0, [x19,842]
.L2120:
	mov	w1, 0
.L2038:
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
.L2124:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2835]
	cmp	w0, 255
	beq	.L2126
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2124
.L2126:
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
.L2130:
	cbz	w20, .L2153
	ldrb	w28, [x19]
	ldr	w26, [x19,40]
.L2131:
	mov	w0, w26
	mov	w1, 0
	bl	queue_lun_state
	cbz	w0, .L2154
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2131
.L2154:
	cmp	w20, 1
	beq	.L2137
	adrp	x0, .LANCHOR4+1265
	ldrb	w0, [x0,#:lo12:.LANCHOR4+1265]
	cbz	w0, .L2137
	ldrb	w0, [x19]
	ubfx	x3, x26, 21, 3
	cmp	w0, 255
	bne	.L2136
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 624
	add	x0, x0, :lo12:.LC0
	str	x3, [x29,120]
	bl	printk
	bl	dump_stack
	ldr	x3, [x29,120]
.L2136:
	ldrb	w24, [x19]
	lsl	x24, x24, 6
	add	x2, x27, x24
	ldr	w1, [x2,40]
	ubfx	x0, x1, 21, 3
	cmp	w3, w0
	bne	.L2137
	ldrh	w0, [x22,3350]
	add	w26, w0, w26
	cmp	w1, w26
	bne	.L2137
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
	b	.L2135
.L2137:
	mov	x0, x19
	bl	queue_read_cmd
.L2135:
	subs	w20, w20, #1
	beq	.L2153
	sbfiz	x19, x28, 6, 32
	add	x19, x27, x19
	b	.L2130
.L2153:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 784
.L2141:
	cbz	w21, .L2155
	ldrb	w0, [x25,58]
	cmp	w0, 12
	bne	.L2142
	ldrb	w0, [x25]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2142
	sbfiz	x25, x0, 6, 32
	add	x25, x19, x25
.L2142:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2141
.L2155:
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
	str	x27, [sp,96]
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	ldr	x0, [x20,2904]
	ldr	x23, [x20,608]
	add	x20, x20, 2896
	cbnz	x0, .L2157
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L2157:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,2904]
	cbnz	x20, .L2158
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 366
	add	x1, x1, 296
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2158:
	mov	w24, 0
	add	x25, x23, 80
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x26, .LANCHOR4
.L2159:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L2180
	mov	w22, 1
	sxtw	x27, w24
.L2168:
	add	x0, x21, 2896
	ldrh	w1, [x0,20]
	cmp	w22, w1
	bgt	.L2181
	add	x1, x27, 8
	ldrh	w2, [x21,3350]
	ldrh	w0, [x0,16]
	ldrh	w1, [x25,x1,lsl 1]
	mul	w1, w1, w2
	ldrb	w2, [x21,3257]
	cmp	w2, 3
	bne	.L2160
	add	w1, w1, w0
	orr	w1, w1, w22, lsl 24
	b	.L2179
.L2160:
	cmp	w2, 2
	bne	.L2162
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L2179
.L2162:
	add	w1, w1, w0
.L2179:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L2163
	add	x3, x26, :lo12:.LANCHOR4
	ldrh	w0, [x21,2918]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,1136]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L2163
	ldr	x3, [x3,1144]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L2164
.L2163:
	add	x0, x26, :lo12:.LANCHOR4
	ldrh	w4, [x21,2918]
	lsl	x4, x4, 2
	ldr	x1, [x0,1136]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L2164
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L2165
	ldr	x7, [x20,24]
	ldr	x5, [x0,1144]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L2165:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23,80]
	ldr	x0, [x19,600]
	strh	wzr, [x0,x1,lsl 1]
	mov	w0, -1
	b	.L2166
.L2164:
	ldrh	w0, [x21,2918]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2918]
	b	.L2168
.L2181:
	add	w24, w24, 1
	b	.L2159
.L2180:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2912]
	add	w0, w0, 1
	strh	w0, [x19,2912]
	mov	w0, 0
.L2166:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldr	x27, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 112
	ret
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
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
	tbz	x0, 4, .L2183
	adrp	x0, .LC162
	mov	w2, w27
	add	x0, x0, :lo12:.LC162
	bl	printk
.L2183:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	hynix_reconfig_rr_para
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w27, .L2184
	bl	zftl_flash_enter_slc_mode
	b	.L2185
.L2184:
	bl	zftl_flash_exit_slc_mode
.L2185:
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
	ldrb	w1, [x21,385]
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
	tbz	x21, 2, .L2186
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2186
	adrp	x0, .LC163
	mov	w1, w23
	add	x0, x0, :lo12:.LC163
	mov	w2, w19
	bl	printk
.L2186:
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
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	uxth	w20, w1
	adrp	x19, .LANCHOR4
	cbnz	w22, .L2197
	add	x0, x19, :lo12:.LANCHOR4
	ldrb	w1, [x0,1266]
	mov	w0, w22
	cmp	w1, w20
	bhi	.L2198
.L2197:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 165
	mov	w2, 32
	ldr	x0, [x19,1256]
	bl	ftl_memset
	ldr	x0, [x19,1248]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	adrp	x0, .LANCHOR0+2
	ldrh	w21, [x0,#:lo12:.LANCHOR0+2]
	mov	w0, w22
	mul	w21, w20, w21
	mov	w1, w21
	bl	flash_erase_block
	cmn	w0, #1
	beq	.L2203
	adrp	x20, .LANCHOR2
	ldr	x2, [x19,1248]
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x3, [x19,1256]
	mov	w0, w22
	mov	w1, w21
	add	x20, x20, 376
	ldrb	w4, [x20,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2203
	ldrb	w4, [x20,9]
	mov	w0, w22
	ldr	x2, [x19,1248]
	mov	w1, w21
	ldr	x3, [x19,1256]
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	beq	.L2203
	ldr	x0, [x19,1248]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L2203
	ldr	x0, [x19,1256]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csinv	w20, w20, wzr, eq
	b	.L2200
.L2203:
	mov	w20, -1
.L2200:
	mov	w0, w22
	mov	w1, w21
	bl	flash_erase_block
	mov	w0, w20
.L2198:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.28, %function
fw_flash_page_prog.constprop.28:
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
	.size	fw_flash_page_prog.constprop.28, .-fw_flash_page_prog.constprop.28
	.text
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
	bhi	.L2213
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 648
	add	x1, x1, 320
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2213:
	add	x4, x19, :lo12:.LANCHOR0
	add	x26, x4, x26, sxtw
	ldrb	w26, [x26,736]
	mov	w0, w26
	ldr	x19, [x4,536]
	bl	nandc_cs
	cbz	w25, .L2214
	sxtw	x1, w26
	add	x1, x1, 8
	add	x1, x19, x1, lsl 8
	str	w25, [x1,8]
.L2214:
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
	adrp	x1, .LANCHOR2+385
	mov	x2, x24
	mov	x3, x23
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+385]
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
.L2220:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2222
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2220
.L2222:
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
.L2224:
	mov	w0, w20
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2228
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2224
.L2228:
	cmp	w24, 2
	bne	.L2226
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
	b	.L2227
.L2226:
	mov	x0, x19
	bl	queue_tlc_prog_cmd
.L2227:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
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
	bhi	.L2230
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 699
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2230:
	add	x28, x22, :lo12:.LANCHOR0
	add	x0, x28, x24, sxtw
	ldrb	w21, [x0,736]
	ldr	x19, [x28,536]
	bl	nandc_rdy_status
	cbnz	w0, .L2231
	ldrb	w0, [x28,633]
	cmp	w0, 1
	bne	.L2232
	bl	nandc_wait_flash_ready
	b	.L2231
.L2232:
	mov	w0, w24
	mov	w1, w20
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L2231:
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	cbnz	w23, .L2233
	mov	w0, w20
	bl	slc_phy_page_address_calc
	mov	w20, w0
	ldrb	w0, [x22,#:lo12:.LANCHOR0]
	cbz	w0, .L2234
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L2234
.L2233:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L2234:
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
	adrp	x1, .LANCHOR2+385
	mov	x2, x27
	mov	x3, x26
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+385]
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
	beq	.L2239
	ubfx	x4, x4, 21, 3
	add	x1, x1, 784
.L2241:
	add	x3, x1, x2, lsl 6
	ldr	w5, [x3,40]
	ubfx	x5, x5, 21, 3
	cmp	w4, w5
	bne	.L2240
	ldrb	w5, [x3,58]
	add	x3, x3, 48
	cmp	w5, 6
	bne	.L2240
	mov	w1, 3
	strb	w1, [x3,10]
	b	.L2239
.L2240:
	lsl	x2, x2, 6
	ldrb	w2, [x1,x2]
	cmp	w2, 255
	bne	.L2241
.L2239:
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
	cbz	w0, .L2247
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2247
	adrp	x0, .LC164
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC164
	mov	w2, w20
	bl	printk
.L2247:
	adrp	x24, .LANCHOR0
	adrp	x25, .LC0
	add	x23, x24, :lo12:.LANCHOR0
	mov	w21, 0
	add	x27, x23, 784
	add	x25, x25, :lo12:.LC0
.L2248:
	cbz	w20, .L2278
	ldrb	w26, [x19]
	ldr	w22, [x19,40]
.L2249:
	mov	w0, w22
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2279
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2249
.L2279:
	cmp	w20, 1
	beq	.L2251
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w0, [x0,781]
	cbnz	w0, .L2252
.L2251:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L2253
.L2252:
	ldrb	w0, [x19]
	ubfx	x28, x22, 21, 3
	cmp	w0, 255
	bne	.L2254
	adrp	x1, .LANCHOR3
	mov	w2, 546
	add	x1, x1, :lo12:.LANCHOR3
	mov	x0, x25
	add	x1, x1, 376
	bl	printk
	bl	dump_stack
.L2254:
	ldrb	w0, [x19]
	add	x0, x27, x0, lsl 6
	ldr	w4, [x0,40]
	ubfx	x0, x4, 21, 3
	cmp	w28, w0
	bne	.L2255
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
	beq	.L2255
	and	w22, w2, w22
	and	w2, w2, w4
	cmp	w22, w2
	bne	.L2255
	cmp	w21, w6
	beq	.L2255
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
	b	.L2253
.L2255:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
.L2253:
	subs	w20, w20, #1
	beq	.L2278
	add	x19, x24, :lo12:.LANCHOR0
	sbfiz	x26, x26, 6, 32
	add	x19, x19, 784
	add	x19, x19, x26
	b	.L2248
.L2278:
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
	cbz	w1, .L2281
	add	x2, x20, :lo12:.LANCHOR4
	add	x0, x0, 784
	ldrb	w2, [x2,1267]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2281:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strb	wzr, [x2,2857]
	strb	w0, [x20,1267]
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
	cbz	w0, .L2285
	bl	timer_get_time
	adrp	x1, .LANCHOR4+1268
	ldr	w1, [x1,#:lo12:.LANCHOR4+1268]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2285
	bl	ftl_flush
.L2285:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	stp	x29, x30, [sp, -96]!
	adrp	x6, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x6, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, w1
	mov	x23, x2
	ldrb	w0, [x0,633]
	mov	x22, x3
	mov	w24, w4
	uxtb	w25, w5
	cmp	w0, w21
	ubfx	x28, x20, 24, 2
	mov	x19, x6
	bhi	.L2290
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 473
	add	x1, x1, 392
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2290:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w26, w20
	add	x1, x0, x21, sxtw
	ldrb	w27, [x1,736]
	cbnz	w28, .L2291
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cbz	w1, .L2292
	ldrb	w0, [x0,1]
	cbz	w0, .L2291
.L2292:
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w26, [x6,2]
	ldrb	w1, [x6,1]
	udiv	w2, w20, w26
	mul	w26, w2, w26
	sub	w0, w20, w26
	cbz	w1, .L2293
	add	w26, w26, w0, lsl 1
	b	.L2291
.L2293:
	add	x6, x6, 4
	ldrh	w0, [x6,w0,uxtw 1]
	add	w26, w0, w26
.L2291:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2294
	adrp	x0, .LC165
	mov	w1, w27
	add	x0, x0, :lo12:.LC165
	mov	w2, w20
	bl	printk
.L2294:
	mov	w1, w26
	mov	w0, w27
	mov	x2, x23
	mov	x3, x22
	mov	w4, w24
	bl	flash_prog_page
	mov	w26, w0
	cbz	w25, .L2295
	adrp	x19, .LANCHOR4
	mov	w0, w21
	add	x25, x19, :lo12:.LANCHOR4
	mov	w1, w20
	mov	w4, w24
	ldr	x2, [x25,1248]
	ldr	x3, [x25,1256]
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2296
	cmn	w0, #1
	beq	.L2296
	ldr	x0, [x25,1248]
	ldr	w1, [x23]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2296
	ldr	x0, [x25,1256]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2295
.L2296:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 4
	adrp	x0, .LC166
	mov	w3, w2
	add	x0, x0, :lo12:.LC166
	ldr	x1, [x19,1256]
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC167
	ldr	x1, [x19,1248]
	add	x0, x0, :lo12:.LC167
	mov	w3, w2
	bl	rknand_print_hex
	b	.L2297
.L2295:
	cmn	w26, #1
	mov	w0, w26
	bne	.L2298
.L2297:
	adrp	x0, .LC168
	mov	w1, w20
	add	x0, x0, :lo12:.LC168
	bl	printk
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 499
	add	x1, x1, 392
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	mov	w0, -1
.L2298:
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
	bne	.L2312
	adrp	x2, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 416
	mov	w2, 2151
	bl	printk
	bl	dump_stack
	adrp	x0, .LC168
	mov	w1, w20
	add	x0, x0, :lo12:.LC168
	bl	printk
.L2312:
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
	adrp	x24, .LC169
	add	x23, x23, :lo12:.LANCHOR3
	add	x0, x19, 1280
	lsl	w2, w2, 1
	mov	w25, 0
	add	x24, x24, :lo12:.LC169
	add	x23, x23, 432
	bl	ftl_memset
.L2314:
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
	tbz	x0, 12, .L2315
	ldr	w3, [x3,4]
	mov	x0, x24
	mov	w1, w27
	mov	w2, w20
	bl	printk
.L2315:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,1538]
	ldrh	w1, [x1,3300]
	cmp	w1, w0
	bhi	.L2316
	adrp	x20, .LC0
	add	x20, x20, :lo12:.LC0
.L2321:
	ldrb	w1, [x19,1537]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x19,1537]
	cmp	w1, 7
	bls	.L2317
	mov	x1, 0
.L2320:
	add	x0, x21, :lo12:.LANCHOR0
	uxth	w25, w1
	ldr	x2, [x0,728]
	add	w0, w1, 8
	add	x0, x2, x0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2318
	mov	x1, x23
	mov	w2, 742
	mov	x0, x20
	bl	printk
	bl	dump_stack
	b	.L2319
.L2318:
	cmp	w2, 255
	bne	.L2319
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2320
	mov	w25, w1
.L2319:
	strb	w25, [x19,1537]
	mov	w25, 1
.L2317:
	add	x27, x21, :lo12:.LANCHOR0
	ldrb	w1, [x19,1537]
	ldr	x0, [x27,728]
	add	x1, x0, x1
	ldrb	w2, [x1,40]
	strb	w2, [x19,1536]
	cmp	w2, 255
	beq	.L2321
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
	b	.L2322
.L2316:
	madd	w20, w27, w28, w20
	cbnz	w0, .L2322
	mov	w1, w20
	bl	flash_erase_block
.L2322:
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
	beq	.L2323
	add	x1, x0, :lo12:.LANCHOR5
	ldrb	w1, [x1,576]
	cbz	w1, .L2324
.L2323:
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,576]
	b	.L2314
.L2324:
	cbnz	w25, .L2325
.L2333:
	add	x22, x22, :lo12:.LANCHOR4
	ldrb	w0, [x22,1536]
	cmp	w0, 255
	bne	.L2327
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 778
	add	x1, x1, 432
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2327
.L2325:
	ldrb	w19, [x19,1537]
	adrp	x20, .LANCHOR3
	adrp	x23, .LC0
	add	x20, x20, :lo12:.LANCHOR3
	add	w19, w19, 1
	add	x23, x23, :lo12:.LC0
	add	x20, x20, 432
.L2328:
	cmp	w19, 7
	bhi	.L2333
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,728]
	add	w0, w19, 8
	add	x0, x1, x0, sxtw
	ldrb	w24, [x0,32]
	add	w0, w24, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2329
	mov	x1, x20
	mov	w2, 771
	mov	x0, x23
	bl	printk
	bl	dump_stack
	b	.L2330
.L2329:
	cmp	w24, 255
	beq	.L2331
.L2330:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,3350]
	mov	w0, 0
	mul	w1, w24, w1
	bl	flash_erase_block
.L2331:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2328
.L2327:
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
	adrp	x26, .LC170
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC170
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
.L2351:
	add	x5, x19, :lo12:.LANCHOR0
	add	w0, w20, 8
	sxth	w25, w20
	ldr	x1, [x5,728]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L2347
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
	bne	.L2348
	ldrb	w4, [x5,2832]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L2348:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2349
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w2, w28
	mov	w3, 672
	ldr	x4, [x0,2864]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L2349:
	cmn	w28, #1
	beq	.L2347
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2359
.L2347:
	sub	w20, w20, #1
	cmn	w20, #1
	bne	.L2351
	mov	w25, 0
	b	.L2350
.L2359:
	mov	w20, w25
.L2350:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2352
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w2, 4800
	ldr	x3, [x0,2864]
	adrp	x0, .LC171
	add	x0, x0, :lo12:.LC171
	ldr	w3, [x3]
	bl	printk
.L2352:
	cmn	w20, #1
	bne	.L2353
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
	mov	w0, 8
	movk	w0, 0x6, lsl 16
	ldr	x1, [x19,2864]
	str	w0, [x1,12]
	mov	w0, w20
	b	.L2354
.L2353:
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
.L2355:
	tbnz	w22, #31, .L2358
	add	x26, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w22, w23
	mov	x2, x24
	mov	x3, x25
	ldrb	w4, [x26,2832]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L2356
	ldr	x0, [x26,2864]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2356
.L2358:
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
	b	.L2354
.L2356:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L2355
.L2354:
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
	bls	.L2377
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2390
.L2377:
	bcs	.L2378
.L2390:
	str	w0, [x1,520]
.L2378:
	adrp	x23, .LANCHOR3
	adrp	x19, .LANCHOR4
	add	x23, x23, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, 0
	add	x23, x23, 448
	add	x19, x19, 1280
	bl	ftl_total_vpn_update
.L2379:
	adrp	x24, .LANCHOR5
	add	x24, x24, :lo12:.LANCHOR5
	add	x25, x24, 640
.L2382:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2864]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3300]
	cmp	w1, w0
	bcc	.L2380
	bl	ftl_ext_alloc_new_blk
.L2380:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2381
	adrp	x0, .LC0
	mov	x1, x23
	mov	w2, 1747
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2381:
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
	beq	.L2382
	cmn	w0, #1
	beq	.L2383
	ldrb	w0, [x24,578]
	cbz	w0, .L2384
.L2383:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strb	wzr, [x0,578]
	b	.L2379
.L2384:
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
	tbz	x0, 12, .L2392
	ldr	x5, [x26,2864]
	adrp	x1, .LANCHOR3
	adrp	x0, .LC172
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC172
	add	x1, x1, 472
	ldrh	w5, [x5,130]
	mov	w2, 1791
	mov	w3, w24
	uxth	w4, w27
	bl	printk
.L2392:
	add	x20, x20, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR4
	mov	w26, w24
	add	x20, x20, 640
	add	x21, x21, 1280
.L2393:
	tbnz	w26, #31, .L2398
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, w25
	mov	x2, x20
	mov	x3, x21
	ldrh	w1, [x27,3350]
	ldrb	w4, [x27,2832]
	madd	w1, w22, w1, w26
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2394
	cmn	w0, #1
	beq	.L2394
	ldr	x0, [x27,608]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2394
.L2398:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,140]
	cmp	w1, w24
	bgt	.L2396
	add	w23, w23, 1
	strh	w23, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2396
.L2394:
	sub	w26, w26, #1
	sxth	w26, w26
	b	.L2393
.L2396:
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
	bne	.L2410
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L2410
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
.L2410:
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
	tbz	x0, 12, .L2415
	adrp	x0, .LC173
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2415:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2416
	adrp	x0, .LC174
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC174
	bl	printk
.L2416:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2417
	adrp	x0, .LC175
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC175
	bl	printk
.L2417:
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2418
	adrp	x0, .LC176
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC176
	bl	printk
.L2418:
	ldrh	w0, [x19,10]
	adrp	x21, .LANCHOR0
	strh	w0, [x19,14]
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldrh	w0, [x0,576]
	cmp	w1, w0
	bcs	.L2414
	mov	w0, 1
	adrp	x25, .LANCHOR4
	adrp	x26, .LANCHOR3
	add	x25, x25, :lo12:.LANCHOR4
	bl	buf_alloc
	add	x26, x26, :lo12:.LANCHOR3
	ldrb	w22, [x19,5]
	mov	x20, x0
	ldrh	w23, [x19,2]
	add	x0, x26, 496
	mov	w28, 0
	add	x25, x25, 1280
	str	x0, [x29,120]
.L2421:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	cmp	w0, w23
	bls	.L2424
	ldrb	w22, [x19,5]
.L2422:
	ldrb	w0, [x19,9]
	cmp	w0, w22
	bls	.L2498
	add	x0, x19, x22, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2423
	add	x3, x21, :lo12:.LANCHOR0
	ldr	x2, [x20,24]
	ldrh	w1, [x3,3350]
	ldrb	w3, [x3,2832]
	madd	w0, w0, w1, w23
	ldr	x1, [x20,8]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w26, w0
	beq	.L2424
	cmn	w0, #1
	beq	.L2425
	ldr	x0, [x20,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2425
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2425
	ldr	x0, [x20,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2424
.L2425:
	adrp	x0, .LANCHOR5+577
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+577]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w23, w0, w22
	cmp	w1, w0
	beq	.L2426
	adrp	x0, .LC0
	ldr	x1, [x29,120]
	mov	w2, 1396
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2426:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0,3300]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2427
	adrp	x0, .LC0
	ldr	x1, [x29,120]
	mov	w2, 1397
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2427:
	cmn	w26, #1
	beq	.L2431
	ldr	x0, [x20,24]
	ldr	w0, [x0,4]
	cmn	w0, #1
	beq	.L2431
	bl	lpa_hash_get_ppa
	cbz	x27, .L2429
	ldr	x2, [x20,24]
	ldr	w3, [x2,8]
	cmp	w0, w3
	beq	.L2429
	cmn	w0, #1
	beq	.L2429
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
	bne	.L2429
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
	bcc	.L2429
	ldr	x0, [x20,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2431
	ldrh	w0, [x4,3340]
	sub	w5, w5, w0
	lsr	w1, w1, w0
	lsl	w28, w28, w5
	ldrb	w0, [x4,3338]
	sub	w28, w28, #1
	and	w1, w28, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2431
.L2429:
	ldr	x2, [x20,24]
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2840]
	cmp	w3, w0
	bcs	.L2431
	ldrb	w0, [x19,9]
	ldrh	w4, [x1,3300]
	ldrh	w3, [x19,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L2499
.L2431:
	ldrh	w0, [x19,6]
	mov	w28, 1
	sub	w0, w0, #1
	strh	w0, [x19,6]
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
.L2423:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2422
.L2498:
	add	w23, w23, 1
	strb	wzr, [x19,5]
	uxth	w23, w23
	b	.L2421
.L2424:
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
	beq	.L2435
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1473
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2435:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w3, [x19,10]
	ldr	x4, [x0,3872]
	mov	w0, 0
	mov	w2, w0
.L2436:
	cmp	w2, w3
	bcs	.L2500
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L2437
	add	w1, w0, 1
	uxth	w0, w1
.L2437:
	add	w2, w2, 1
	b	.L2436
.L2500:
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w22, [x2,3300]
	madd	w0, w1, w22, w0
	uxth	w22, w0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2439
	ldrh	w1, [x19]
	adrp	x0, .LC177
	ldr	x4, [x2,600]
	add	x0, x0, :lo12:.LC177
	ubfiz	x3, x1, 1, 16
	mov	w2, w22
	ldrh	w3, [x4,x3]
	bl	printk
.L2439:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	adrp	x23, .LC178
	mov	w25, -1
	add	x23, x23, :lo12:.LC178
	ldr	x0, [x0,600]
	strh	w22, [x0,x1,lsl 1]
	mov	x0, x20
	mov	w22, 0
	bl	buf_free
.L2440:
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w0, [x2,3276]
	cmp	w22, w0, lsl 1
	bcs	.L2501
	cbz	w28, .L2441
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L2441
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w26, w0
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2442
	mov	x0, x23
	mov	w1, w26
	bl	printk
.L2442:
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
.L2441:
	add	w22, w22, 1
	b	.L2440
.L2501:
	ldrh	w1, [x2,3300]
	ldrh	w0, [x19,12]
	ldrb	w3, [x19,9]
	madd	w0, w1, w3, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x2,3872]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L2444
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1503
	add	x1, x1, 496
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2444:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2414
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2414
.L2499:
	ldr	x0, [x1,2864]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2432
	str	w2, [x0,8]
.L2432:
	ldr	x1, [x20,24]
	ldrh	w0, [x19,10]
	ldrh	w2, [x19,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	b	.L2431
.L2414:
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
	add	x22, x22, 520
.L2503:
	add	x2, x25, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x2,2864]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3300]
	cmp	w3, w1
	bcs	.L2504
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2505
.L2504:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2505:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2506
	mov	x1, x22
	mov	w2, 230
	mov	x0, x23
	bl	printk
	bl	dump_stack
.L2506:
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
	beq	.L2507
	ldrb	w2, [x27,-120]
	cbz	w2, .L2508
.L2507:
	add	x20, x20, :lo12:.LANCHOR7
	strb	wzr, [x20,-120]
	b	.L2503
.L2508:
	cmn	w0, #1
	bne	.L2510
	adrp	x0, .LC179
	mov	w1, w26
	add	x0, x0, :lo12:.LC179
	bl	printk
	b	.L2503
.L2510:
	ldrh	w0, [x1,698]
	cmp	w21, w0
	bcs	.L2511
	add	x21, x1, x21, uxtw 2
	str	w26, [x21,704]
.L2511:
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
	tbz	x0, 12, .L2516
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 251
	add	x2, x2, 536
	bl	printk
.L2516:
	adrp	x22, .LANCHOR4
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC181
	adrp	x25, .LC182
	ldr	x0, [x0,1232]
	mov	w26, 0
	add	x24, x24, :lo12:.LC181
	add	x25, x25, :lo12:.LC182
	bl	ftl_memset
	ldr	x1, [x19,728]
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,728]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2517:
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
	blt	.L2518
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
	b	.L2526
.L2518:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2519
.L2526:
	mov	w1, w19
	bl	flash_erase_block
.L2519:
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
	bne	.L2520
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2517
.L2520:
	cbnz	w26, .L2527
	mov	w26, 1
	b	.L2517
.L2527:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x19, x0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	tbz	x0, 12, .L2529
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2209
	add	x2, x2, 560
	bl	printk
.L2529:
	mov	x0, x19
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR4
	bl	nandc_init
	add	x24, x22, 748
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 0
	add	x0, x0, 376
	str	x0, [x22,624]
	mov	w0, 1
	mov	w2, 8
	strb	w0, [x22,633]
	add	x0, x22, 736
	mov	x23, 0
	mov	w26, 2
	bl	ftl_memset
	mov	x25, x24
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 0
	add	x0, x0, 1096
	mov	w2, 32
	mov	w27, 44
	bl	ftl_memset
.L2535:
	mov	w0, w23
	mov	x1, x24
	bl	flash_read_id
	add	x0, x22, 744
	strb	w26, [x23,x0]
	cbnz	x23, .L2530
	ldrb	w0, [x25]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2531
.L2533:
	mov	w22, -2
	b	.L2597
.L2531:
	ldrb	w0, [x25,1]
	cmp	w0, 255
	beq	.L2533
.L2530:
	ldrb	w0, [x24]
	cmp	w0, 181
	bne	.L2534
	strb	w27, [x24]
.L2534:
	add	x23, x23, 1
	add	x24, x24, 8
	cmp	x23, 4
	bne	.L2535
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x24, x24, 748
.L2538:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x24
	bl	flash_mem_cmp8
	cbnz	w0, .L2536
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L2533
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2540
.L2536:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 30
	bne	.L2538
	b	.L2533
.L2540:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1400]
	cmp	w4, w3
	beq	.L2539
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2540
	mov	w2, w0
.L2539:
	add	x23, x20, :lo12:.LANCHOR2
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x23, 1400
	ubfiz	x2, x2, 5, 32
	add	x1, x1, x2
	add	x0, x24, 640
	mov	w2, 32
	add	x23, x23, 376
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x24,516]
	cmp	w0, 8
	bhi	.L2541
	ldrb	w1, [x23,20]
	cmp	w1, 60
	bls	.L2542
	mov	w1, 60
	strb	w1, [x23,20]
.L2542:
	cmp	w0, 8
	bne	.L2541
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,748]
	cmp	w0, 44
	beq	.L2603
.L2541:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2543
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2233
	add	x2, x2, 560
	bl	printk
.L2543:
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
	strb	wzr, [x23,1266]
	str	x0, [x23,1232]
	bl	flash_die_info_init
	ldrb	w0, [x24,394]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,396]
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
	bne	.L2544
	mov	w1, 70
	strb	w1, [x22,632]
.L2544:
	add	x2, x20, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	add	x2, x2, 376
	add	x3, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1264]
	ldrb	w0, [x2,19]
	ldrh	w2, [x2,16]
	strb	w0, [x3,725]
	tbz	x2, 6, .L2546
	sub	w2, w0, #17
	uxtb	w2, w2
	cmp	w2, 2
	bhi	.L2547
	adrp	x2, micron_read_retrial
	cmp	w0, 19
	add	x2, x2, :lo12:micron_read_retrial
	str	x2, [x1,1224]
	bne	.L2599
	mov	w0, 15
	b	.L2600
.L2547:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2568
	cmp	w0, 33
	bne	.L2550
.L2568:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1224]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,726]
	mov	w1, 7
	strb	w1, [x0,1216]
	b	.L2546
.L2550:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w3, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2569
	cbz	w3, .L2552
.L2569:
	add	x2, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x2,1224]
	beq	.L2554
	cmp	w0, 68
	beq	.L2554
	mov	w0, 7
	strb	w0, [x2,1216]
	b	.L2555
.L2554:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1216]
.L2555:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w3, .L2601
	mov	w1, 5
.L2601:
	strb	w1, [x0,726]
	b	.L2546
.L2552:
	sub	w0, w0, #36
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L2546
	adrp	x0, toshiba_3d_read_retrial
	add	x0, x0, :lo12:toshiba_3d_read_retrial
	str	x0, [x1,1224]
.L2599:
	mov	w0, 7
.L2600:
	strb	w0, [x1,1216]
.L2546:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2557
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2281
	add	x2, x2, 560
	bl	printk
.L2557:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,672]
	ldrb	w0, [x0,780]
	tbz	x0, 0, .L2558
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2602
.L2558:
	mov	w0, 4
.L2602:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2560
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2561
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2301
	add	x2, x2, 560
	bl	printk
.L2561:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19,728]
	strb	w1, [x0,32]
	b	.L2597
.L2560:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,725]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L2562
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,1224]
.L2562:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,780]
	tbz	x0, 2, .L2564
	ldr	x0, [x19,728]
	ldrb	w0, [x0,19]
	cbz	w0, .L2564
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w1, [x19,2]
	mov	w0, 0
	ldrb	w2, [x21,1240]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,672]
	cbnz	w0, .L2564
	ldr	x1, [x19,728]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2564:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2566
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC127
	mov	w1, 2356
	add	x2, x2, 560
	bl	printk
.L2566:
	bl	nand_flash_print_info
	mov	w22, 0
	b	.L2597
.L2603:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 376
	ldrb	w1, [x0,28]
	cmp	w1, 3
	bne	.L2541
	strb	wzr, [x0,28]
	b	.L2541
.L2597:
	mov	w0, w22
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
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
	adrp	x22, .LC183
	adrp	x23, .LC184
	mov	x27, x0
	mov	w19, w20
	adrp	x28, .LANCHOR0
	add	x22, x22, :lo12:.LC183
	add	x23, x23, :lo12:.LC184
	add	x24, x24, 704
.L2605:
	add	x3, x28, :lo12:.LANCHOR0
	ldrh	w1, [x3,3300]
	cmp	w1, w19
	bls	.L2617
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
	beq	.L2610
	cmn	w26, #1
	bne	.L2606
.L2610:
	mov	w20, 1
.L2606:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2605
.L2617:
	mov	x0, x27
	bl	buf_free
	cbz	w20, .L2609
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1339
	add	x1, x1, 576
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2609:
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
	adrp	x0, .LC185
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC185
	ldrb	w1, [x19,2834]
	bl	printk
	adrp	x3, .LANCHOR4
	adrp	x0, .LC186
	add	x3, x3, :lo12:.LANCHOR4
	add	x0, x0, :lo12:.LC186
	ldrb	w1, [x3,1536]
	ldrb	w2, [x3,1537]
	ldrh	w3, [x3,1538]
	bl	printk
	ldr	x2, [x19,2864]
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,608]
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
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
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,600]
	adrp	x0, .LC192
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC192
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,2864]
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,584]
	adrp	x0, .LC193
	ldrh	w3, [x19,576]
	add	x0, x0, :lo12:.LC193
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC194
	add	x1, x19, 3360
	add	x0, x0, :lo12:.LC194
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3300]
	mov	w2, 2
	ldrb	w3, [x19,3276]
	ldr	x1, [x19,3880]
	mul	w3, w0, w3
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
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
	cbnz	w4, .L2623
	cmp	w3, w7
	bne	.L2620
.L2623:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L2620:
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
	bls	.L2626
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 31
	add	x1, x1, 592
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2626:
	adrp	x21, .LANCHOR0
	adrp	x19, .LANCHOR7
	add	x21, x21, :lo12:.LANCHOR0
	ldr	x0, [x21,2864]
	ldrh	w2, [x0,698]
	cmp	w20, w2
	bls	.L2627
	adrp	x0, .LC197
	mov	w1, w20
	add	x0, x0, :lo12:.LC197
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
	bcs	.L2629
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 592
	mov	w2, 35
	b	.L2633
.L2627:
	add	x0, x0, x20, sxtw 2
	sbfiz	x22, x22, 4, 32
	ldr	w4, [x0,704]
	add	x0, x21, 3904
	add	x23, x0, x22
	strh	w20, [x0,x22]
	strh	wzr, [x23,2]
	cbnz	w4, .L2630
	adrp	x0, .LC198
	mov	w1, w20
	mov	w2, w4
	add	x19, x19, :lo12:.LANCHOR7
	add	x0, x0, :lo12:.LC198
	bl	printk
	ldr	x0, [x23,8]
	mov	w1, 255
	ldrh	w2, [x19,-118]
	bl	ftl_memset
	b	.L2629
.L2630:
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
	beq	.L2631
	adrp	x0, .LC199
	mov	w1, w20
	add	x0, x0, :lo12:.LC199
	bl	printk
	ldr	x3, [x21,2864]
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x21,2832]
	adrp	x0, .LC201
	ldr	x1, [x23,8]
	add	x0, x0, :lo12:.LC201
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	adrp	x0, .LC202
	ldr	x1, [x22,-128]
	add	x0, x0, :lo12:.LC202
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
.L2631:
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x19,-128]
	ldr	w0, [x0]
	cmp	w0, w20
	beq	.L2629
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 592
	mov	w2, 59
.L2633:
	bl	printk
	bl	dump_stack
.L2629:
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
	blt	.L2636
	bl	pm_free_sblk
	ldr	x2, [x20,2864]
	ldrh	w1, [x19,1164]
	ldrh	w3, [x2,688]
	sub	w1, w1, #1
	cmp	w3, w1
	blt	.L2636
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2638
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 131
	add	x1, x1, 608
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	ldr	x1, [x20,2864]
	add	x0, x1, x0, uxth 1
	ldrh	w21, [x0,416]
.L2638:
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
	beq	.L2639
	ldr	x1, [x19,8]
	cbz	x1, .L2639
	ldrsh	w2, [x19,2]
	tbz	w2, #31, .L2639
	bl	pm_write_page
	ldrh	w0, [x19,2]
	and	w0, w0, 32767
	strh	w0, [x19,2]
.L2639:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w19, 0
	add	x0, x23, 3904
	mov	w24, 21
	mov	w25, 1
	add	x20, x0, x20, lsl 4
.L2640:
	ldr	x0, [x23,2864]
	ldrh	w1, [x0,698]
	cmp	w1, w19
	bls	.L2646
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
	bne	.L2641
	mov	w1, w22
	mov	w0, w19
	bl	load_l2p_region
	ldr	x1, [x20,8]
	mov	w0, w19
	bl	pm_write_page
	mov	w0, -1
	strh	w0, [x20]
.L2641:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2640
.L2646:
	bl	pm_free_sblk
.L2636:
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
	cbz	w0, .L2648
	bl	pm_gc
	str	wzr, [x19,3896]
.L2648:
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
.L2654:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2653
	bl	pm_flush_id
.L2653:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2654
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
.L2660:
	strh	w24, [x20]
	strh	wzr, [x20,2]
	cbz	w23, .L2659
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x20,8]
.L2659:
	add	x20, x20, 16
	cmp	x20, x22
	bne	.L2660
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
	bgt	.L2661
	adrp	x0, .LC203
	ldrh	w1, [x1,692]
	mov	w3, w21
	add	x0, x0, :lo12:.LC203
	adrp	x26, .LC204
	add	w21, w21, 1
	bl	printk
	add	x26, x26, :lo12:.LC204
	ldr	x0, [x20,2864]
	ldrsh	w20, [x0,696]
.L2662:
	cmp	w20, w21
	bge	.L2675
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
	beq	.L2663
	cmn	w28, #1
	beq	.L2663
	ldr	x1, [x27,2864]
	ldr	w0, [x25]
	ldrh	w2, [x1,698]
	cmp	w0, w2
	bcs	.L2663
	add	x0, x1, x0, uxtw 2
	str	w22, [x0,704]
.L2663:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2662
.L2675:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR5+577
	mov	w1, 1
	add	w24, w24, 1
	strb	w1, [x0,#:lo12:.LANCHOR5+577]
	ldr	x0, [x19,2864]
	strh	w24, [x0,696]
	bl	pm_free_sblk
	ldr	x1, [x19,3912]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3912]
	mov	w0, -1
	bl	pm_write_page
.L2661:
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
	bcc	.L2688
	adrp	x0, .LC205
	mov	w1, w5
	add	x0, x0, :lo12:.LC205
	bl	printk
	mov	w0, -1
	b	.L2678
.L2692:
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L2680
.L2679:
	add	w19, w19, 1
	add	x3, x3, 16
	uxth	x19, w19
	cmp	w19, 32
	beq	.L2691
.L2688:
	ldr	x0, [x3,8]
	cbnz	x0, .L2692
	b	.L2679
.L2691:
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
	bne	.L2693
.L2684:
	adrp	x0, .LANCHOR7-116
	strb	w19, [x0,#:lo12:.LANCHOR7-116]
	mov	w0, w25
	bl	load_l2p_region
.L2680:
	cbnz	w23, .L2681
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,3912]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x22]
	b	.L2682
.L2681:
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
.L2682:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 32767
	add	x21, x21, 3904
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2678
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2678
.L2693:
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2684
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
	b	.L2684
.L2678:
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
	beq	.L2695
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
	bne	.L2696
	ldrh	w1, [x24,3300]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
	b	.L2739
.L2696:
	cmp	w1, 2
	bne	.L2697
	ldrh	w1, [x24,3300]
	add	w0, w0, w1, lsl 1
	sub	w0, w0, #1
	orr	w0, w0, 33554432
.L2739:
	str	w0, [x19,40]
.L2697:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x19,52]
	cmp	w0, 512
	beq	.L2698
	cmn	w0, #1
	beq	.L2698
	ldr	x0, [x19,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2699
.L2698:
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
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x19,40]
	ldr	w2, [x19,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2742
.L2699:
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
	bne	.L2701
	lsl	w25, w25, 1
	mov	w26, 1
.L2701:
	mov	w23, 0
	adrp	x24, .LC207
	add	x0, x24, :lo12:.LC207
	mov	w27, w23
	str	x0, [x29,112]
	b	.L2702
.L2744:
	ldr	x0, [x0,1144]
	ldr	w1, [x0,x12]
	ldr	w0, [x7,8]
	cmp	w1, w0
	bne	.L2706
.L2729:
	add	w27, w27, 1
	add	w28, w28, 1
.L2710:
	cmp	w28, w26
	bhi	.L2743
	ldr	x0, [x29,128]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w2, [x1,3350]
	ldrb	w1, [x1,3257]
	ldrh	w0, [x22,x0,lsl 1]
	cmp	w1, 3
	mul	w0, w0, w2
	str	w0, [x29,152]
	bne	.L2703
	add	w0, w0, w23
	orr	w0, w0, w28, lsl 24
.L2740:
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
	beq	.L2744
.L2706:
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
	beq	.L2729
	mov	x0, x19
	bl	buf_free
.L2742:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x21,80]
	mov	w28, 0
	ldr	x1, [x0,600]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,608]
	ldrh	w1, [x21,80]
	strh	w1, [x0,130]
	b	.L2700
.L2709:
	add	w23, w23, 1
.L2702:
	cmp	w23, w25
	beq	.L2711
	sub	w0, w23, #1
	mov	w24, 0
	str	w0, [x29,124]
.L2712:
	ldrb	w0, [x22,9]
	cmp	w24, w0
	bge	.L2709
	sxtw	x0, w24
	mov	w28, 1
	str	x0, [x29,128]
	b	.L2710
.L2703:
	cmp	w1, 2
	bne	.L2705
	ldr	w1, [x29,124]
	add	w0, w0, w1
	add	w0, w0, w28
	orr	w0, w0, 33554432
	b	.L2740
.L2705:
	add	w0, w0, w23
	b	.L2740
.L2743:
	add	w24, w24, 1
	b	.L2712
.L2711:
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
.L2713:
	cmp	w27, w25
	beq	.L2724
	sub	w0, w27, #1
	mov	w24, 0
	str	w0, [x29,104]
.L2725:
	ldrb	w0, [x22,9]
	cmp	w24, w0
	bge	.L2722
	sxtw	x0, w24
	mov	w23, 1
	str	x0, [x29,112]
.L2723:
	cmp	w23, w26
	bhi	.L2745
	ldr	x1, [x29,136]
	ldr	w0, [x29,128]
	add	x1, x1, :lo12:.LANCHOR4
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1136]
	ldr	w6, [x2,x0]
	cmn	w6, #1
	beq	.L2714
	ldr	x1, [x1,1144]
	str	x6, [x29,96]
	ldr	w0, [x1,x0]
	str	w0, [x29,124]
	mov	w0, w6
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x6, [x29,96]
	bne	.L2715
	mov	w0, w6
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x6, [x29,96]
.L2715:
	ldr	x0, [x29,112]
	add	x0, x0, 8
	ldrh	w1, [x22,x0,lsl 1]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3350]
	ldrb	w0, [x0,3257]
	cmp	w0, 3
	mul	w1, w1, w2
	bne	.L2716
	add	w1, w1, w27
	orr	w1, w1, w23, lsl 24
	b	.L2741
.L2716:
	cmp	w0, 2
	bne	.L2718
	ldr	w0, [x29,104]
	add	w1, w1, w0
	add	w1, w1, w23
	orr	w1, w1, 33554432
	b	.L2741
.L2718:
	add	w1, w1, w27
.L2741:
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
	bne	.L2719
	mov	w0, w6
	add	x1, x29, 152
	add	w28, w28, 1
	bl	pm_log2phys
	mov	w0, w19
	bl	ftl_vpn_decrement
	b	.L2720
.L2719:
	ldr	w1, [x29,152]
	cmp	w0, w1
	csinc	w28, w28, w28, ne
.L2720:
	ldr	w0, [x29,128]
	add	x1, x20, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,128]
	ldr	x0, [x1,584]
	add	x0, x0, x19, lsl 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 224
	bne	.L2714
	lsl	x19, x19, 1
	ldr	x1, [x1,600]
	ldrh	w2, [x1,x19]
	cbz	w2, .L2714
	strh	w0, [x1,x19]
.L2714:
	add	w23, w23, 1
	b	.L2723
.L2745:
	add	w24, w24, 1
	b	.L2725
.L2722:
	add	w27, w27, 1
	b	.L2713
.L2724:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x21,80]
	ldr	x0, [x0,600]
	strh	w28, [x0,x1,lsl 1]
.L2700:
	ldrh	w1, [x21,80]
	adrp	x0, .LC208
	mov	w2, w28
	add	x0, x0, :lo12:.LC208
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
	beq	.L2726
	bl	ftl_free_sblk
.L2726:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x20,2864]
	strh	w0, [x1,126]
	ldr	x1, [x20,608]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2694
.L2695:
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2694
	ldr	x1, [x24,2864]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L2728
	bl	pm_flush
	ldr	x0, [x24,608]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x24,2864]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L2728:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x20,608]
	strh	w1, [x0,130]
.L2694:
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
	strb	w0, [x1,1267]
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
	ldrb	w2, [x1,389]
	ldrb	w8, [x1,388]
	ldrh	w3, [x1,386]
	mul	w6, w2, w6
	ldrb	w0, [x1,385]
	ldrh	w7, [x1,390]
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
.L2747:
	cmp	w2, w4
	bhi	.L2813
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L2747
.L2813:
	mul	w3, w0, w3
	add	x2, x19, :lo12:.LANCHOR0
	sub	w1, w1, #1
	mul	w5, w3, w7
	strh	w1, [x2,3340]
	mov	w1, 1
	lsr	w4, w5, 21
	mov	w2, 0
.L2749:
	cmp	w1, w4
	bhi	.L2814
	add	w2, w2, 1
	lsl	w1, w1, 1
	uxth	w2, w2
	b	.L2749
.L2814:
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
	bhi	.L2751
	mov	w0, 80
	strh	w0, [x4,3306]
.L2751:
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
	beq	.L2752
	ldrb	w0, [x0,3272]
	cbz	w0, .L2753
.L2752:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 150
	mov	w3, 64
	strh	w2, [x0,3342]
	add	x2, x22, :lo12:.LANCHOR4
	strh	w3, [x2,1162]
	mov	w3, 12
	strh	w3, [x0,592]
	ldrb	w3, [x19,#:lo12:.LANCHOR0]
	cbnz	w3, .L2753
	mov	w3, 4
	strh	w3, [x0,592]
	mov	w3, 600
	strh	w3, [x0,3346]
	mov	w0, 128
	strh	w0, [x2,1160]
.L2753:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2755
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	bl	printk
.L2755:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2756
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC210
	add	x0, x0, :lo12:.LC210
	ldr	w1, [x1,2836]
	bl	printk
.L2756:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2757
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	ldr	w1, [x1,2840]
	bl	printk
.L2757:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2758
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC212
	add	x0, x0, :lo12:.LC212
	ldr	w1, [x1,-108]
	bl	printk
.L2758:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2759
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC213
	add	x0, x0, :lo12:.LC213
	ldrh	w1, [x1,-104]
	bl	printk
.L2759:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2760
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	ldrh	w1, [x1,-118]
	bl	printk
.L2760:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2761
	add	x22, x22, :lo12:.LANCHOR4
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	ldrh	w1, [x22,1164]
	bl	printk
.L2761:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2762
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	ldrh	w1, [x1,3306]
	bl	printk
.L2762:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2763
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	ldrh	w1, [x1,3308]
	bl	printk
.L2763:
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
	tbz	x0, 12, .L2764
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	bl	printk
.L2764:
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
	tbz	x0, 12, .L2765
	adrp	x0, .LC219
	ldrh	w3, [x3,-118]
	add	x0, x0, :lo12:.LC219
	mov	w1, w22
	mov	w2, w24
	bl	printk
.L2765:
	add	x21, x21, :lo12:.LANCHOR7
	ldrh	w0, [x21,-118]
	cmp	w22, w0
	bhi	.L2767
	cmp	w24, w0
	bls	.L2766
.L2767:
.L2768:
	b	.L2768
.L2766:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L2769
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
	cbz	w0, .L2770
	ldr	x1, [x19,2864]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L2770:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L2769:
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
	cbnz	x0, .L2816
.L2818:
	mov	w0, -1
	b	.L2817
.L2816:
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x21,-96]
	mov	x1, x21
	add	x0, x19, 1184
	str	xzr, [x19,1184]
	str	xzr, [x1,-88]!
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1184]
	cbz	x0, .L2818
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
	cbnz	w0, .L2819
	bl	zftl_init
	mov	w19, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x21,-102]
	b	.L2820
.L2819:
	ldr	x1, [x19,1176]
	adrp	x2, ftl_read_flash_info
	add	x2, x2, :lo12:ftl_read_flash_info
	adrp	x0, ftl_deinit
	add	x0, x0, :lo12:ftl_deinit
	adrp	x21, .LANCHOR2
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
	bne	.L2821
	add	x22, x21, :lo12:.LANCHOR2
	adrp	x1, gNandParaInfo
	add	x22, x22, 376
	add	x1, x1, :lo12:gNandParaInfo
	mov	x0, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x22,18]
	bl	flash_lsb_page_tbl_build
.L2821:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x1, .LANCHOR0
	bne	.L2822
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L2823
.L2822:
	add	x0, x20, :lo12:.LANCHOR4
	strb	wzr, [x1,#:lo12:.LANCHOR0]
	strb	wzr, [x0,1264]
.L2823:
	adrp	x2, gNandFlashIDBEccBits
	add	x0, x1, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	ldrb	w2, [x2,#:lo12:gNandFlashIDBEccBits]
	strb	w2, [x0,632]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x21,386]
	str	xzr, [x20,1224]
	ldrb	w2, [x2,#:lo12:gNandFlashEccBits]
	strb	w2, [x0,724]
	mov	w2, 1
	mov	w0, 0
.L2824:
	cmp	w2, w3
	bhi	.L2829
	add	w0, w0, 1
	lsl	w2, w2, 1
	uxth	w0, w0
	b	.L2824
.L2829:
	add	x1, x1, :lo12:.LANCHOR0
	sub	w0, w0, #1
	strb	wzr, [x1,672]
	strh	w0, [x1,3340]
	cbnz	w19, .L2820
	bl	FtlInit
	mov	w19, w0
.L2820:
	adrp	x0, .LC220
	mov	w1, w19
	add	x0, x0, :lo12:.LC220
	bl	printk
	mov	w0, w19
.L2817:
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
	tbz	x0, 8, .L2831
	adrp	x0, .LC221
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC221
	bl	printk
.L2831:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	sub	w2, w24, #1
	mov	w21, 0
	mov	w23, w21
	adrp	x27, .LANCHOR4
	ldr	x0, [x0,600]
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC223
	add	x0, x0, :lo12:.LC223
	str	x0, [x29,120]
	adrp	x0, .LC222
	add	x0, x0, :lo12:.LC222
	str	x0, [x29,112]
.L2832:
	cmp	w23, w24
	beq	.L2864
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1136]
	ldr	w2, [x0,w23,sxtw 2]
	cmn	w2, #1
	beq	.L2833
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,140]
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2834
	ldr	x0, [x29,112]
	mov	w3, w23
	ldr	w1, [x29,140]
	bl	printk
.L2834:
	mov	w25, w23
.L2840:
	add	x1, x27, :lo12:.LANCHOR4
	sbfiz	x26, x25, 2, 32
	ldr	x0, [x1,1136]
	ldr	w4, [x0,x26]
	cmn	w4, #1
	beq	.L2835
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,140]
	ldrb	w0, [x0,2832]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w2, w0, uxth
	bne	.L2835
	ldr	x0, [x1,1144]
	str	x4, [x29,104]
	ldr	w28, [x0,x26]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	ldr	x4, [x29,104]
	bne	.L2836
	mov	w0, w4
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
	ldr	x4, [x29,104]
.L2836:
	ldr	w3, [x29,156]
	cmp	w3, w28
	bne	.L2837
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
	b	.L2863
.L2837:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2839
	ldr	x0, [x29,120]
	mov	w1, w4
	mov	w2, w28
	bl	printk
.L2839:
	ldr	x0, [x29,128]
	ldrh	w0, [x0]
.L2863:
	bl	ftl_vpn_decrement
	add	x0, x27, :lo12:.LANCHOR4
	mov	w1, -1
	ldr	x0, [x0,1136]
	str	w1, [x0,x26]
	bl	timer_get_time
.L2835:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L2840
.L2833:
	add	w23, w23, 1
	b	.L2832
.L2864:
	ldr	w0, [x22,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2842
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,608]
	ldr	x2, [x0,600]
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w21
	bl	printk
.L2842:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w21
	beq	.L2843
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 483
	add	x1, x1, 616
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2843:
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
	mov	w4, 0
	mov	w6, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x3, x19, :lo12:.LANCHOR0
	mov	w5, w4
	add	x2, x3, 2896
	stp	x21, x22, [sp,32]
	ldrb	w0, [x2,4]
	add	x1, x2, x0, sxtw 1
	ldrb	w7, [x3,3276]
	ldrh	w1, [x1,40]
	ldrh	w3, [x2,2]
.L2866:
	cmp	w1, w6
	bne	.L2908
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w7
	bne	.L2867
	add	w3, w3, 1
	mov	w4, 1
	mov	w0, 0
	uxth	w3, w3
.L2867:
	add	x1, x2, x0, sxtw 1
	mov	w5, 1
	ldrh	w1, [x1,40]
	b	.L2866
.L2908:
	cbz	w5, .L2869
	add	x2, x19, :lo12:.LANCHOR0
	strb	w0, [x2,2900]
.L2869:
	cbz	w4, .L2870
	add	x0, x19, :lo12:.LANCHOR0
	strh	w3, [x0,2898]
.L2870:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,3257]
	ldrh	w20, [x0,3350]
	cmp	w2, 2
	mul	w1, w1, w20
	bne	.L2871
	ldrb	w2, [x0,2902]
	add	x0, x0, 2896
	cbnz	w2, .L2871
	ldrh	w20, [x0,2]
	add	w20, w1, w20, lsl 1
	b	.L2872
.L2871:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w20, [x0,2898]
	add	w20, w1, w20
.L2872:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2896
	ldrb	w0, [x2,2900]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x2,2900]
	ldrb	w2, [x2,3276]
	cmp	w2, w0
	bne	.L2873
	ldrh	w0, [x1,2]
	strb	wzr, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L2873:
	mov	w0, 1
	mov	w21, 0
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x22, x0
.L2884:
	ldrb	w0, [x19,2902]
	cbnz	w21, .L2886
	cmp	w0, 3
	beq	.L2874
.L2886:
	cmp	w0, w21
	bgt	.L2876
.L2885:
	mov	x0, x22
	bl	buf_free
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L2876:
	ldrb	w1, [x19,3257]
	cmp	w1, 2
	bne	.L2877
	cbnz	w0, .L2877
	add	w0, w21, w20
	orr	w0, w0, 33554432
	b	.L2907
.L2877:
	orr	w0, w20, w21, lsl 24
.L2907:
	str	w0, [x22,40]
	mov	w1, 1
	mov	x0, x22
	bl	sblk_read_page
	ldr	w0, [x22,52]
	cmp	w0, 512
	beq	.L2881
	cmn	w0, #1
	beq	.L2881
	ldr	x0, [x22,24]
	ldr	w23, [x0,4]
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2882
	mov	w0, w23
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2882:
	ldr	w0, [x29,76]
	cmp	w20, w0
	bne	.L2881
	ldrh	w2, [x19,2920]
	ldr	x1, [x19,3248]
	str	w20, [x1,x2,lsl 2]
	ldrh	w1, [x19,2920]
	add	w1, w1, 1
	strh	w1, [x19,2920]
.L2881:
	ldrh	w0, [x19,2922]
	add	w0, w0, 1
	strh	w0, [x19,2922]
.L2874:
	add	w21, w21, 1
	cmp	w21, 4
	bne	.L2884
	b	.L2885
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
	bne	.L2910
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 920
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2910:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,2896]
	cmp	w1, w3
	beq	.L2911
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2912
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,600]
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	ldrh	w2, [x2,x3]
	bl	printk
.L2912:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2896
	add	x1, x20, 2936
	ldrh	w0, [x20,2896]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L2913
	mov	w0, -1
	strh	w0, [x20,2896]
	b	.L2939
.L2913:
	ldrh	w3, [x20,2896]
	ldr	x2, [x20,584]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L2914
	cmp	w4, 224
	beq	.L2914
	ldr	x2, [x20,608]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L2914
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L2914
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L2915
.L2914:
	cbnz	w4, .L2916
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L2917
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 940
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2917:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2896]
	ldr	x0, [x0,600]
	strh	wzr, [x0,x1,lsl 1]
.L2916:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2920]
	strh	w0, [x19,2896]
.L2939:
	mov	w0, 0
	b	.L2911
.L2915:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w20, [x0,40]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2918
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 948
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2918:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w24, 1
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
	bne	.L2919
	ldrb	w3, [x2,3257]
	cmp	w3, 2
	uxth	w24, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2919
	ldrh	w0, [x2,3274]
	sub	w0, w0, #1
	uxth	w0, w0
.L2919:
	add	x20, x19, :lo12:.LANCHOR0
	orr	w23, w0, w1
	mov	w0, 1
	strb	w24, [x20,2902]
	strh	wzr, [x20,2898]
	strb	wzr, [x20,2900]
	strh	wzr, [x20,2922]
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
	beq	.L2940
	cmn	w0, #1
	beq	.L2940
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2922
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 980
	add	x1, x1, 640
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2940:
	mov	x0, x21
	bl	buf_free
	mov	w0, -1
	b	.L2911
.L2922:
	ldrb	w0, [x20,3276]
	mov	x26, 0
	ldrh	w22, [x20,3300]
	mov	w25, 1
	ldr	x23, [x21,8]
	mul	w22, w0, w22
	mul	w22, w24, w22
	mov	w24, 21
	uxth	w22, w22
.L2923:
	cmp	w22, w26
	ble	.L2941
	ldr	w0, [x23,x26,lsl 2]
	cmn	w0, #1
	beq	.L2925
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L2926
	ldr	w0, [x23,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L2926:
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
	bne	.L2925
	ldrh	w3, [x20,2920]
	ldr	x1, [x20,3248]
	str	w2, [x1,x3,lsl 2]
	ldrh	w1, [x20,2920]
	add	w1, w1, 1
	strh	w1, [x20,2920]
.L2925:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L2923
.L2941:
	mov	x0, x21
	bl	buf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2896]
	ldr	x3, [x0,600]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2920]
	cmp	w2, w3
	beq	.L2929
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	bl	printk
.L2929:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2896]
	ldr	x1, [x19,600]
	ldrh	w3, [x19,2920]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2924]
	ldrh	w0, [x19,2920]
.L2911:
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
	beq	.L2943
	adrp	x21, .LANCHOR3
	adrp	x22, .LC0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 11
	add	x22, x22, :lo12:.LC0
	add	x21, x21, 656
.L2944:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,608]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2945
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L2946
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
.L2946:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,608]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L2947
	mov	x1, x21
	mov	w2, 1419
	mov	x0, x22
	bl	printk
	bl	dump_stack
.L2947:
	mov	x0, x23
	bl	buf_free
.L2945:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,608]
	ldr	w2, [x2,2840]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L2948
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
	b	.L2942
.L2948:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L2942
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L2944
	b	.L2942
.L2943:
	ldr	w1, [x0,536]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L2953
	ldr	x3, [x3,2864]
	ldr	w1, [x0,540]
	ldr	w3, [x3,44]
	add	w1, w1, 98304
	add	w1, w1, 1696
	cmp	w3, w1
	bls	.L2942
.L2953:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2864]
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w2, [x0,536]
	str	wzr, [x0,544]
.L2942:
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
	bcs	.L2958
	bl	timer_get_time
	ldr	x21, [x20,608]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L2958
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,608]
	ldrh	w2, [x20,576]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L2962
	ldr	x2, [x20,2864]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L2963
.L2962:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2864]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L2963:
	ldr	w25, [x0,600]
	mov	w0, 65535
	uxth	w20, w25
	cmp	w20, w0
	bne	.L2964
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1712
	add	x1, x1, 680
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2964:
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
	cbz	w1, .L2958
	uxtw	x22, w20
	ldr	x24, [x21,584]
	add	x24, x24, x22, lsl 2
	ldrb	w1, [x24,2]
	tst	w1, 192
	and	w2, w1, 224
	beq	.L2965
	cmp	w2, 224
	beq	.L2965
	ldr	x1, [x21,608]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L2965
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L2965
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L2966
.L2965:
	cbnz	w2, .L2958
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x22]
	cbz	w0, .L2967
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1728
	add	x1, x1, 680
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2967:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,600]
	strh	wzr, [x0,x22]
	b	.L2958
.L2966:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2969
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1734
	add	x1, x1, 680
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2969:
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
	bne	.L2970
	ldrb	w3, [x2,3257]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2970
	ldrh	w0, [x2,3274]
	sub	w0, w0, #1
	uxth	w0, w0
.L2970:
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
	beq	.L2982
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2971
.L2982:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L2995
.L2971:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2973
	mov	w0, w20
	mov	w1, 1
.L2995:
	bl	gc_add_sblk
	mov	x0, x23
	bl	buf_free
	b	.L2958
.L2973:
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
.L2974:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L2996
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L2975
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2976
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2976:
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
	bne	.L2975
	add	w21, w21, 1
	uxth	w21, w21
.L2975:
	add	x27, x27, 1
	b	.L2974
.L2996:
	mov	x0, x23
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2978
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w25
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,600]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L2978:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,600]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L2958
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L2958:
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
	adrp	x0, .LC228
	ldrb	w3, [x1,2]
	add	x0, x0, :lo12:.LC228
	mov	w1, w20
	and	w4, w4, 2047
	ubfx	x2, x3, 5, 3
	ubfx	x5, x5, 11, 8
	ubfx	x3, x3, 3, 2
	bl	printk
	mov	w1, 65535
	mov	w0, 0
	cmp	w20, w1
	beq	.L2998
	ldrh	w1, [x19,576]
	cmp	w1, w20
	bls	.L2998
	ldr	x0, [x19,584]
	mov	w26, 1
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2999
	ldrb	w26, [x19,3257]
.L2999:
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
	adrp	x0, .LC229
	add	x0, x0, :lo12:.LC229
	add	x1, x1, x25
	ldrb	w2, [x1,2]
	mov	w1, w20
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	adrp	x0, .LC183
	str	w22, [x29,152]
	add	x0, x0, :lo12:.LC183
	str	x0, [x29,120]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 704
	str	x0, [x29,112]
.L3000:
	add	x21, x23, :lo12:.LANCHOR0
	ldrh	w0, [x21,3300]
	cmp	w0, w19
	bls	.L3016
	lsl	w0, w19, 1
	str	wzr, [x29,156]
	sub	w0, w0, #1
	str	w0, [x29,132]
.L3017:
	ldrb	w0, [x29,185]
	ldr	w1, [x29,156]
	cmp	w0, w1
	bls	.L3014
	mov	w24, 1
.L3015:
	cmp	w24, w26
	ldr	w0, [x29,156]
	bhi	.L3034
	add	x1, x29, 176
	add	x0, x1, x0, sxtw 1
	ldrh	w2, [x0,16]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3001
	add	x1, x23, :lo12:.LANCHOR0
	cmp	w26, 3
	ldrh	w0, [x1,3350]
	mul	w0, w2, w0
	add	w21, w0, w19
	bne	.L3002
	orr	w21, w21, w24, lsl 24
	b	.L3003
.L3002:
	cmp	w26, 2
	bne	.L3003
	ldr	w3, [x29,132]
	ldrb	w21, [x1,3257]
	add	w0, w0, w3
	add	w0, w0, w24
	orr	w21, w0, w21, lsl 24
.L3003:
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
	beq	.L3021
	cmn	w9, #1
	bne	.L3005
.L3021:
	mov	w0, 1
	str	w0, [x29,152]
.L3005:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,584]
	add	x0, x0, x25
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L3001
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L3001
	ldr	x0, [x28,24]
	str	x2, [x29,104]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x2, [x29,104]
	bne	.L3007
	ldr	x0, [x28,24]
	add	x1, x29, 172
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L3007:
	ldr	w0, [x29,172]
	cmp	w0, w21
	bne	.L3008
	ldr	x2, [x28,24]
	add	w22, w22, 1
	adrp	x0, .LC230
	mov	w1, w21
	add	x0, x0, :lo12:.LC230
	mov	w3, w22
	ldr	w2, [x2,4]
	bl	printk
.L3008:
	ldr	x0, [x29,144]
	cbz	x0, .L3010
	ubfiz	x21, x27, 2, 32
	ldr	w2, [x0,x21]
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L3011
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3011
	adrp	x0, .LC231
	mov	w1, w27
	add	x0, x0, :lo12:.LC231
	bl	printk
.L3011:
	ldr	x0, [x29,144]
	ldr	x1, [x28,24]
	ldr	w0, [x0,x21]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L3010
	cmn	w0, #1
	beq	.L3010
	adrp	x0, .LC0
	ldr	x1, [x29,112]
	mov	w2, 1300
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3010:
	add	w27, w27, 1
.L3001:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L3015
.L3034:
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,156]
	b	.L3017
.L3014:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L3000
.L3016:
	mov	x0, x28
	bl	buf_free
	ldr	x3, [x29,136]
	adrp	x0, .LC232
	ldr	x2, [x21,600]
	add	x0, x0, :lo12:.LC232
	mov	w1, w20
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
	ldr	w0, [x29,152]
.L2998:
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
	tbz	x0, 12, .L3036
	adrp	x0, .LC233
	mov	w1, w19
	add	x0, x0, :lo12:.LC233
	mov	w2, w22
	mov	w3, w25
	bl	printk
.L3036:
	cbnz	w19, .L3037
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3038
.L3037:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3039
	lsl	w19, w19, 13
	mov	w2, 8192
.L3038:
	add	w1, w22, w25
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3039
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
	add	x0, x0, 720
	str	x0, [x29,120]
.L3040:
	cbz	w24, .L3100
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,136]
	cmp	w20, w27
	cset	w2, eq
	cmp	w20, w1
	ldrb	w0, [x0,2832]
	cset	w26, eq
	uxth	w21, w0
	cbnz	w26, .L3070
	cbz	w2, .L3041
	b	.L3099
.L3070:
	cbz	w2, .L3043
.L3099:
	udiv	w26, w22, w0
	msub	w26, w26, w0, w22
	uxth	w0, w25
	uxth	w26, w26
	sub	w21, w21, w26
	uxth	w21, w21
	cmp	w21, w25
	csel	w21, w0, w21, hi
	b	.L3041
.L3043:
	msub	w21, w0, w20, w28
	mov	w26, w2
	uxtb	w21, w21
.L3041:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 784
.L3046:
	ldr	w2, [x1,36]
	cmp	w2, w20
	bne	.L3044
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L3044
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
	b	.L3045
.L3044:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3046
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3047
	mov	w0, w20
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3047:
	ldr	w0, [x29,156]
	cmn	w0, #1
	bne	.L3048
	mov	w21, 0
.L3049:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w21, w0
	bcs	.L3045
	madd	w0, w20, w0, w21
	cmp	w0, w22
	bcc	.L3050
	cmp	w0, w28
	bcs	.L3050
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	add	x23, x23, 512
	bl	ftl_memset
.L3050:
	add	w21, w21, 1
	b	.L3049
.L3048:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3052
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
.L3045:
	add	w20, w20, 1
	sub	w24, w24, #1
.L3052:
	cbz	w24, .L3071
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2834]
	cmp	w0, 2
	bhi	.L3040
.L3071:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w1, [x2,3894]
	cbz	w1, .L3040
	add	x0, x2, 784
	ldrb	w2, [x2,3893]
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
.L3055:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,3894]
	cbz	w1, .L3101
	ldrb	w0, [x0,3893]
	cmp	w0, 255
	bne	.L3056
	ldr	x1, [x29,120]
	mov	w2, 1064
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3056:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 784
	ldrb	w21, [x2,3893]
	lsl	x0, x21, 6
	add	x26, x1, x0
	ldrb	w0, [x1,x0]
	ldr	w8, [x26,52]
	strb	w0, [x2,3893]
	cmn	w8, #1
	bne	.L3057
	ldr	x1, [x2,608]
	str	w8, [x29,140]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L3058
.L3057:
	cmp	w8, 256
	bne	.L3058
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
	adrp	x0, .LC234
	add	x0, x0, :lo12:.LC234
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
.L3058:
	add	x9, x19, :lo12:.LANCHOR0
	add	x1, x9, 784
	add	x1, x1, x21, lsl 6
	add	x10, x1, 32
	ldr	x0, [x1,24]
	ldr	w3, [x0,4]
	ldr	w0, [x1,36]
	cmp	w3, w0
	beq	.L3059
	ldr	x3, [x9,608]
	str	x8, [x29,96]
	str	x9, [x29,104]
	ldr	w0, [x3,552]
	str	x10, [x29,112]
	add	w0, w0, 1
	str	w0, [x3,552]
	adrp	x0, .LC235
	ldr	x7, [x1,24]
	add	x0, x0, :lo12:.LC235
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
.L3059:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 784
	add	x0, x0, x21, lsl 6
	ldr	x1, [x0,24]
	ldr	w0, [x0,36]
	ldr	w1, [x1,4]
	cmp	w1, w0
	bne	.L3072
	cmn	w8, #1
	bne	.L3060
.L3072:
	ldr	x1, [x29,120]
	mov	w2, 1085
	ldr	x0, [x29,128]
	bl	printk
	bl	dump_stack
.L3060:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	add	x21, x0, x21, lsl 6
	ldrb	w1, [x1,2832]
	ldrb	w2, [x21,56]
	cmp	w1, w2
	bls	.L3062
	ldrb	w3, [x21,57]
	lsl	w2, w2, 9
	ldr	x1, [x21,8]
	ldr	x0, [x21,16]
	add	x1, x1, x3, lsl 9
	bl	ftl_memcpy
	b	.L3063
.L3062:
	ldrb	w0, [x21,2]
	and	w0, w0, -9
	strb	w0, [x21,2]
.L3063:
	add	x21, x19, :lo12:.LANCHOR0
	mov	x1, x26
	add	x0, x21, 3895
	bl	buf_remove_buf
	mov	x0, x26
	bl	buf_free
	ldrb	w0, [x21,3894]
	sub	w0, w0, #1
	strb	w0, [x21,3894]
	b	.L3055
.L3101:
	mov	w2, -1
	strb	w1, [x0,3894]
	strb	w2, [x0,3893]
	b	.L3040
.L3100:
	bl	timer_get_time
	adrp	x1, .LANCHOR7-80
	str	w0, [x1,#:lo12:.LANCHOR7-80]
	ldr	w0, [x29,140]
.L3039:
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
	bhi	.L3105
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x22, x0
	mov	x0, -14
	cbnz	x22, .L3105
	adrp	x0, .LC236
	mov	x1, x19
	add	x0, x0, :lo12:.LC236
	strb	w22, [x19,x21]
	bl	printk
	adrp	x0, .LC237
	mov	x1, x19
	add	x0, x0, :lo12:.LC237
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC238
	mov	x0, x19
	add	x1, x1, :lo12:.LC238
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3106
	adrp	x23, .LANCHOR0
	adrp	x0, .LC239
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC239
	mov	w2, 4
	adrp	x20, .LANCHOR4
	ldr	x3, [x19,2864]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR4
	ldr	x1, [x19,2864]
	adrp	x0, .LC240
	mov	w2, 2
	add	x0, x0, :lo12:.LC240
	add	x1, x1, 416
	ldrh	w3, [x3,1164]
	mov	w19, w22
	mov	w22, 65535
	bl	rknand_print_hex
.L3107:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,1164]
	cmp	w19, w0
	bge	.L3110
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,2864]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3108
	mov	x1, 0
	bl	ftl_sblk_dump
.L3108:
	add	w19, w19, 1
	b	.L3107
.L3106:
	adrp	x1, .LC241
	mov	x0, x19
	add	x1, x1, :lo12:.LC241
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3111
	adrp	x23, .LANCHOR0
	adrp	x0, .LC239
	add	x20, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC239
	mov	w2, 4
	add	x22, x29, 192
	adrp	x24, .LC244
	ldr	x3, [x20,2864]
	add	x24, x24, :lo12:.LC244
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	adrp	x3, .LANCHOR4+1164
	ldr	x1, [x20,2864]
	adrp	x0, .LC240
	mov	w2, 2
	add	x0, x0, :lo12:.LC240
	add	x1, x1, 416
	ldrh	w3, [x3,#:lo12:.LANCHOR4+1164]
	bl	rknand_print_hex
	adrp	x0, .LC242
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC242
	str	x1, [x22,-88]!
	add	x19, x20, 3904
	mov	x20, x23
	bl	printk
	adrp	x23, .LC243
	ldr	x0, [x29,104]
	mov	x1, x22
	add	x25, x19, 512
	add	x23, x23, :lo12:.LC243
	bl	rk_simple_strtoull.constprop.31
	mov	w22, w0
	str	w0, [x29,100]
	uxth	w26, w0
.L3113:
	ldrh	w1, [x19]
	mov	x0, x23
	ldrh	w2, [x19,2]
	bl	printk
	ldrh	w0, [x19]
	cmp	w0, w26
	bne	.L3112
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x19,8]
	mov	w2, 4
	ldrb	w3, [x0,2832]
	mov	x0, x24
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L3112:
	add	x19, x19, 16
	cmp	x19, x25
	bne	.L3113
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
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	ldr	w3, [x6]
	ldr	w2, [x19,52]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	ldrb	w3, [x20,2832]
	adrp	x0, .LC245
	add	x0, x0, :lo12:.LC245
	ldr	x1, [x19,8]
	mov	w2, 4
	lsl	w3, w3, 7
	b	.L3128
.L3111:
	adrp	x1, .LC246
	mov	x0, x19
	add	x1, x1, :lo12:.LC246
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3114
	bl	dump_ftl_info
	b	.L3110
.L3114:
	adrp	x1, .LC247
	mov	x0, x19
	add	x1, x1, :lo12:.LC247
	mov	x2, 9
	bl	memcmp
	cbnz	w0, .L3115
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1,-88]!
	bl	rk_simple_strtoull.constprop.31
	str	w0, [x29,100]
	adrp	x1, .LANCHOR0+3336
	strh	w0, [x1,#:lo12:.LANCHOR0+3336]
	bl	dump_all_list_info
	b	.L3110
.L3115:
	adrp	x1, .LC248
	mov	x0, x19
	add	x1, x1, :lo12:.LC248
	mov	x2, 8
	bl	memcmp
	cbz	w0, .L3110
	adrp	x1, .LC249
	mov	x0, x19
	add	x1, x1, :lo12:.LC249
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3117
	add	x20, x29, 192
	adrp	x0, .LC242
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC242
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	bl	rk_simple_strtoull.constprop.31
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
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x20,2832]
	adrp	x0, .LC201
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC201
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,2832]
	adrp	x0, .LC202
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC202
	mov	w2, 4
	lsl	w3, w3, 1
.L3128:
	bl	rknand_print_hex
	mov	x0, x19
	bl	buf_free
	b	.L3110
.L3117:
	adrp	x1, .LC250
	mov	x0, x19
	add	x1, x1, :lo12:.LC250
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3118
	add	x20, x29, 192
	adrp	x0, .LC242
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC242
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.31
	str	w0, [x29,100]
	mov	x1, x22
	bl	ftl_sblk_dump
	b	.L3110
.L3118:
	adrp	x1, .LC251
	mov	x0, x19
	add	x1, x1, :lo12:.LC251
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3119
	add	x20, x29, 192
	adrp	x0, .LC242
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC242
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.31
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3110
.L3119:
	adrp	x1, .LC252
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC252
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3120
	add	x1, x19, 8
	adrp	x0, .LC242
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC242
	str	x1, [x19,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.31
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29,100]
	cmn	w0, #1
	ldr	x2, [x29,88]
	bne	.L3121
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3121:
	adrp	x0, .LC253
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC253
	bl	printk
	b	.L3110
.L3120:
	adrp	x0, .LC254
	add	x0, x0, :lo12:.LC254
	bl	printk
	adrp	x0, .LC255
	add	x0, x0, :lo12:.LC255
	bl	printk
	adrp	x0, .LC256
	add	x0, x0, :lo12:.LC256
	bl	printk
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
.L3110:
	bl	rknand_device_unlock
	mov	x0, x21
.L3105:
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
	beq	.L3130
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1576
	add	x1, x1, 736
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3130:
	adrp	x0, .LC264
	mov	x28, 0
	add	x0, x0, :lo12:.LC264
	adrp	x27, .LC263
	str	x0, [x29,120]
	adrp	x0, .LC265
	add	x0, x0, :lo12:.LC265
	mov	w22, w28
	str	x0, [x29,112]
	add	x27, x27, :lo12:.LC263
.L3131:
	cmp	w28, w24
	mov	w25, w28
	bge	.L3158
	ldr	w2, [x20,x28,lsl 2]
	cmn	w2, #1
	beq	.L3132
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2832]
	adrp	x0, .LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L3136
	mov	x0, x27
	mov	w1, w21
	mov	w3, w28
	bl	printk
.L3136:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L3134
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w1, [x3,2832]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L3134
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
	cbz	w0, .L3135
	ldr	x1, [x3,3872]
	mov	w2, 4
	ldr	x0, [x29,112]
	mov	w3, w24
	bl	rknand_print_hex
.L3135:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	mov	w0, -1
	str	w0, [x20,x26]
.L3134:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L3136
.L3132:
	add	x28, x28, 1
	b	.L3131
.L3158:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3138
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,600]
	adrp	x0, .LC266
	add	x0, x0, :lo12:.LC266
	ldrh	w3, [x4,x3]
	bl	printk
.L3138:
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
	beq	.L3160
	bl	zftl_insert_data_list
.L3160:
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
	add	x0, x0, 1267
	str	x0, [x29,104]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
.L3164:
	ldrb	w0, [x24,2856]
	adrp	x21, .LANCHOR0
	cbz	w0, .L3166
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
	bcc	.L3167
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 495
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3167:
	ldr	x0, [x29,128]
	add	x1, x21, :lo12:.LANCHOR0
	add	x4, x1, 784
	add	x0, x4, x0, lsl 6
	ldr	w2, [x1,2840]
	ldr	w23, [x0,36]
	cmp	w23, w2
	bcc	.L3168
	mov	x0, x27
	bl	buf_free
	mov	w0, -1
	b	.L3222
.L3168:
	ldr	x25, [x0,8]
	ldr	x28, [x0,24]
	ldrb	w22, [x0,57]
	ldrb	w20, [x0,56]
	ldrb	w0, [x1,2857]
	cbz	w0, .L3170
	ldr	x0, [x29,120]
	ldrb	w3, [x0,1267]
	add	x3, x4, x3, lsl 6
.L3171:
	ldrb	w5, [x3]
	cmp	w5, 255
	beq	.L3224
	sbfiz	x3, x5, 6, 32
	add	x3, x4, x3
	b	.L3171
.L3224:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L3170
	ldr	x0, [x3,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x25, x1
	bl	ftl_memcpy
	mov	x0, x27
	bl	buf_free
	b	.L3164
.L3170:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3174
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3174:
	add	x4, x21, :lo12:.LANCHOR0
	mov	w3, 0
	add	x4, x4, 784
	ldr	x19, [x4,-176]
	add	x19, x19, 16
.L3177:
	ldr	w0, [x4,36]
	cmp	w0, w23
	bne	.L3175
	ldrb	w5, [x4,2]
	tbz	x5, 3, .L3175
	add	x0, x21, :lo12:.LANCHOR0
	ubfiz	x3, x3, 6, 32
	add	x0, x0, 784
	and	w5, w5, -9
	add	x3, x0, x3
	ldr	w0, [x3,40]
	strb	w5, [x3,2]
	ldr	x26, [x3,8]
	str	w0, [x29,156]
	b	.L3176
.L3175:
	add	w3, w3, 1
	add	x4, x4, 64
	cmp	w3, 32
	bne	.L3177
	mov	x26, 0
.L3176:
	add	x3, x21, :lo12:.LANCHOR0
	str	wzr, [x29,136]
	ldrb	w0, [x3,2832]
	cmp	w20, w0
	bcs	.L3178
	add	w20, w22, w20
	cbz	x26, .L3179
	cbz	w22, .L3180
	mov	x0, x25
	mov	x1, x26
	lsl	w2, w22, 9
	str	x3, [x29,136]
	bl	ftl_memcpy
	ldr	x3, [x29,136]
	ldr	x19, [x3,608]
	add	x19, x19, 48
.L3180:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2832]
	cmp	w20, w2
	bcc	.L3181
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3223
.L3181:
	ubfiz	x1, x20, 9, 9
	sub	w2, w2, w20
	add	x0, x25, x1
	lsl	w2, w2, 9
	add	x1, x26, x1
	bl	ftl_memcpy
.L3223:
	str	wzr, [x29,136]
	b	.L3178
.L3179:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L3182
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
	bne	.L3183
	ldr	w0, [x26,52]
	cmn	w0, #1
	bne	.L3184
.L3183:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	x1, [x0,608]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	adrp	x0, .LC267
	ldrb	w1, [x26,1]
	add	x0, x0, :lo12:.LC267
	ldr	w4, [x26,52]
	bl	printk
	adrp	x0, .LC202
	mov	w2, 4
	ldr	x1, [x26,24]
	add	x0, x0, :lo12:.LC202
	mov	w3, w2
	bl	rknand_print_hex
.L3184:
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3185
	ldr	w0, [x26,52]
	cmn	w0, #1
	bne	.L3186
.L3185:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 566
	add	x1, x1, 760
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3186:
	cbz	w22, .L3187
	ldr	w0, [x29,156]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3188
	ldr	x1, [x26,8]
	mov	x0, x25
	bl	ftl_memcpy
	b	.L3189
.L3182:
	cbz	w22, .L3187
	lsl	w2, w22, 9
.L3188:
	mov	x0, x25
	mov	w1, 0
	bl	ftl_memset
.L3189:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x19, [x0,608]
	add	x19, x19, 48
.L3187:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2832]
	cmp	w20, w2
	bcc	.L3190
	bls	.L3191
	ldr	x19, [x0,608]
	add	x19, x19, 16
	b	.L3191
.L3190:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L3192
	ldr	x1, [x26,8]
	lsl	x20, x0, 2
	add	x0, x25, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L3191
.L3192:
	add	x0, x25, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3191:
	cbz	x26, .L3178
	ldrb	w0, [x26,2]
	mov	x1, x26
	and	w0, w0, -9
	strb	w0, [x26,2]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 3895
	bl	buf_remove_buf
	mov	x0, x26
	bl	buf_free
.L3178:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3193
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3193:
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
	bhi	.L3194
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L3166
.L3194:
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
	ldrb	w5, [x1,1267]
	mov	w1, 0
	mov	w3, w5
.L3198:
	cmp	w1, w4
	beq	.L3225
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L3198
.L3225:
	add	x0, x0, :lo12:.LANCHOR4
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,1267]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 784
	strb	w2, [x0,2073]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3166
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3166:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w19, [x21,2856]
	cbnz	w19, .L3164
	bl	ftl_write_completed
	mov	w0, w19
.L3222:
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
	cbnz	w0, .L3227
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3226
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
	beq	.L3280
	cmn	w0, #1
	bne	.L3229
.L3280:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 595
	add	x1, x1, 784
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3229:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,144]
	cmn	w0, #1
	bne	.L3231
	mov	w0, w21
	add	x1, x29, 144
	mov	w2, 0
	bl	pm_log2phys
.L3231:
	ldr	w22, [x29,144]
	cmp	w22, w23
	bne	.L3232
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 784
	add	x1, x1, 2832
.L3235:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L3233
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L3233
	mov	x0, x20
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3226
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC268
	mov	w1, w21
	mov	w2, w22
	add	x0, x0, :lo12:.LC268
	ldrh	w3, [x19,2922]
	bl	printk
	b	.L3226
.L3233:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3235
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
	tbz	x0, 8, .L3236
	ldr	w2, [x29,144]
	mov	w0, w21
	str	x2, [x29,136]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,136]
	adrp	x0, .LC269
	ldrh	w5, [x23,2922]
	add	x0, x0, :lo12:.LC269
	mov	w1, w21
	mov	w4, w22
	bl	printk
.L3236:
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
	b	.L3226
.L3232:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3238
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2922]
	adrp	x0, .LC269
	add	x0, x0, :lo12:.LC269
	mov	w1, w21
	mov	w2, w22
	mov	w4, w23
	bl	printk
.L3238:
	mov	x0, x20
	bl	buf_free
	b	.L3226
.L3227:
	ldrb	w24, [x21,3257]
	add	x0, x21, 2896
	add	x22, x22, 80
	cmp	w24, 3
	bne	.L3239
	ldrb	w1, [x21,3272]
	cbz	w1, .L3240
	ldrb	w20, [x22,9]
	ldrh	w21, [x0,314]
	str	w24, [x29,136]
	add	w23, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w23, w21, w23
	madd	w20, w23, w20, w21
	uxth	w20, w20
	b	.L3242
.L3240:
	ldrh	w20, [x0,314]
	adrp	x0, .LANCHOR2
	ldrb	w21, [x22,9]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1528
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
	bne	.L3243
	b	.L3242
.L3239:
	ldrb	w20, [x22,9]
	ldrb	w1, [x21,3348]
	ldrh	w21, [x0,314]
	mov	w0, 1
	sdiv	w23, w21, w20
	msub	w20, w23, w20, w21
	uxth	w20, w20
	cbz	w1, .L3321
	mov	w0, 2
.L3321:
	str	w0, [x29,136]
.L3242:
	mov	w25, 0
	add	x24, x19, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR4
.L3245:
	ldrb	w1, [x22,9]
	ldr	w0, [x29,136]
	mul	w0, w0, w1
	cmp	w25, w0
	bge	.L3278
	ldrh	w0, [x24,3274]
	add	w26, w21, w25
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	beq	.L3278
	add	x0, x3, :lo12:.LANCHOR4
	sbfiz	x26, x26, 2, 32
	ldr	x1, [x0,1144]
	ldr	w0, [x1,x26]
	cmn	w0, #1
	bne	.L3246
	ldrh	w27, [x24,2922]
	ldrh	w0, [x24,2920]
	str	x3, [x29,120]
	str	x1, [x29,128]
	cmp	w0, w27
	bls	.L3226
	mov	w0, w27
	add	w27, w27, 1
	bl	gc_get_src_ppa_from_index
	strh	w27, [x24,2922]
	ldr	x1, [x29,128]
	ldr	x3, [x29,120]
	str	w0, [x1,x26]
.L3246:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L3245
.L3278:
	mov	w25, 1
.L3243:
	adrp	x0, .LANCHOR2
	str	x0, [x29,120]
	ldr	w1, [x0,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3248
	adrp	x0, .LANCHOR4+1144
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x5, x21, 2, 16
	mov	w1, w23
	mov	w2, w25
	mov	w3, w20
	ldr	x6, [x0,#:lo12:.LANCHOR4+1144]
	adrp	x0, .LC270
	ldrh	w4, [x4,3210]
	add	x0, x0, :lo12:.LC270
	ldr	w5, [x6,x5]
	mov	w6, w21
	bl	printk
.L3248:
	adrp	x0, .LC0
	mov	w27, 0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,112]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 784
	str	x0, [x29,104]
.L3249:
	ldrb	w0, [x22,9]
	ldr	w1, [x29,136]
	mul	w0, w1, w0
	cmp	w27, w0
	bge	.L3325
	add	w26, w21, w27
	sxtw	x0, w26
	str	x0, [x29,128]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x29,128]
	ldr	x0, [x0,3264]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	bne	.L3250
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbnz	x0, .L3251
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w24
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L3226
.L3251:
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
	bne	.L3252
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
	b	.L3322
.L3252:
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
	beq	.L3253
	cmn	w0, #1
	bne	.L3254
.L3253:
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
	beq	.L3281
	cmn	w0, #1
	bne	.L3254
.L3281:
	ldr	x0, [x24,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3254:
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3282
	cmn	w0, #1
	bne	.L3257
.L3282:
	ldr	x1, [x29,104]
	mov	w2, 710
	ldr	x0, [x29,112]
	bl	printk
	bl	dump_stack
.L3257:
	ldr	x1, [x24,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2840]
	cmp	w2, w0
	bcc	.L3259
	mov	w0, -1
	str	w0, [x1,4]
.L3259:
	ldr	x0, [x24,24]
	add	x26, x26, :lo12:.LANCHOR4
	ldr	w1, [x0,4]
	ldr	x0, [x26,1136]
	str	w1, [x0,x28]
	ldr	x0, [x24,24]
	ldr	w1, [x24,40]
.L3322:
	str	w1, [x0,8]
.L3250:
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
	bne	.L3260
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
	b	.L3261
.L3260:
	cmp	w0, 2
	bne	.L3262
	ldrb	w6, [x3,3348]
	add	x1, x1, 32
	ldrh	w0, [x3,3350]
	cbnz	w6, .L3263
	add	w3, w27, w20
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	b	.L3324
.L3263:
	add	w3, w20, w27, lsr 1
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	and	w3, w27, 1
	add	w0, w0, w3
.L3324:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L3262:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, 784
	add	x0, x0, x4, lsl 6
	ldr	w3, [x0,40]
	ldrh	w1, [x0,48]
	ldr	x0, [x2,1152]
	str	w3, [x0,x1,lsl 2]
.L3261:
	ldr	x0, [x29,120]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3264
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
	adrp	x0, .LC271
	add	x0, x0, :lo12:.LC271
	ldr	w5, [x5,40]
	bl	printk
.L3264:
	add	w8, w27, 1
	uxth	w27, w8
	b	.L3249
.L3325:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x2,3257]
	cmp	w0, 3
	add	x0, x29, 144
	bne	.L3308
	ldrb	w1, [x2,3272]
	cbz	w1, .L3267
.L3270:
	ldr	x1, [x29,144]
	strb	wzr, [x1,60]
	b	.L3268
.L3267:
	cmp	w25, 1
	mov	w2, 9
	beq	.L3323
	cmp	w25, 2
	bne	.L3270
	mov	w2, 13
.L3323:
	ldr	x1, [x29,144]
	strb	w2, [x1,60]
.L3268:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L3271
.L3308:
	ldrb	w2, [x22,9]
	ldr	w3, [x29,136]
	mul	w2, w2, w3
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L3326
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L3308
.L3326:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,144]
	mul	w1, w3, w1
	bl	sblk_prog_page
.L3271:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x22,9]
	ldrb	w3, [x1,3272]
	cbz	w3, .L3273
	add	w0, w2, w2, lsl 1
	and	w2, w0, 1023
	b	.L3274
.L3273:
	ldrb	w1, [x1,3348]
	ubfiz	w0, w2, 1, 8
	cmp	w1, wzr
	csel	w2, w0, w2, ne
.L3274:
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
	blt	.L3275
	ldr	x0, [x19,608]
	strh	wzr, [x0,86]
.L3275:
	bl	gc_write_completed
.L3226:
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
	bne	.L3328
	adrp	x21, .LANCHOR7
	add	x1, x21, :lo12:.LANCHOR7
	ldr	w23, [x1,-80]
	cbnz	w23, .L3329
	ldr	w1, [x1,-76]
	cbz	w1, .L3328
.L3329:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,3306]
	cmp	w20, w2, lsr 2
	bls	.L3328
	ldrh	w1, [x1,2872]
	cmp	w1, w20
	bcs	.L3328
	add	w23, w23, 20
	bl	timer_get_time
	cmp	w23, w0
	bcs	.L3330
	add	x0, x21, :lo12:.LANCHOR7
	str	wzr, [x0,-80]
.L3330:
	add	x19, x21, :lo12:.LANCHOR7
	ldr	w20, [x19,-76]
	bl	timer_get_time
	add	w20, w20, 20
	cmp	w20, w0
	bcs	.L3511
	str	wzr, [x19,-76]
	b	.L3511
.L3328:
	adrp	x23, .LANCHOR4
	add	x1, x23, :lo12:.LANCHOR4
	ldrb	w2, [x1,1130]
	mov	w1, 16
	cmp	w2, 6
	bhi	.L3484
	adrp	x1, .L3334
	add	x1, x1, :lo12:.L3334
	ldrh	w1, [x1,w2,uxtw #1]
	adr	x2, .Lrtx3334
	add	x1, x2, w1, sxth #2
	br	x1
.Lrtx3334:
	.section	.rodata
	.align	0
	.align	2
.L3334:
	.2byte	(.L3333 - .Lrtx3334) / 4
	.2byte	(.L3335 - .Lrtx3334) / 4
	.2byte	(.L3336 - .Lrtx3334) / 4
	.2byte	(.L3337 - .Lrtx3334) / 4
	.2byte	(.L3338 - .Lrtx3334) / 4
	.2byte	(.L3442 - .Lrtx3334) / 4
	.2byte	(.L3340 - .Lrtx3334) / 4
	.text
.L3333:
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
	beq	.L3341
	cbnz	w26, .L3342
	ldrh	w0, [x21,2872]
	cmp	w20, w0, lsl 1
	bge	.L3511
.L3342:
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
	beq	.L3344
	add	x3, x23, :lo12:.LANCHOR4
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1132]
	add	w1, w1, 1
	str	w1, [x3,1132]
	ldr	x3, [x21,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,3300]
	cmp	w3, w2
	bcs	.L3345
	ldrh	w3, [x21,576]
	cmp	w1, w3, lsr 4
	bls	.L3344
	ldrh	w1, [x21,2874]
	cmp	w1, w2
	bls	.L3344
.L3345:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x2,3273]
	bl	gc_add_sblk
	cbz	w0, .L3346
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	str	wzr, [x23,1132]
	strb	w0, [x23,1130]
	b	.L3511
.L3344:
	add	x0, x19, :lo12:.LANCHOR0
	strh	wzr, [x0,3304]
.L3346:
	cmp	w22, 15
	mov	w21, 2
	bls	.L3347
	cbz	w25, .L3444
	add	x0, x19, :lo12:.LANCHOR0
	mov	w21, 1
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bls	.L3347
.L3444:
	mov	w21, 2
.L3347:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3348
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, w20
	mov	w4, w22
	ldr	x0, [x1,608]
	ldrb	w2, [x1,3273]
	mov	w1, 1848
	ldrh	w5, [x0,124]
	ldrh	w6, [x0,120]
	ldrh	w7, [x0,122]
	ldrh	w0, [x24,80]
	str	w0, [sp]
	adrp	x0, .LC272
	add	x0, x0, :lo12:.LC272
	bl	printk
.L3348:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w2, 1
	ldrb	w0, [x19,3273]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3349
.L3350:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	b	.L3514
.L3349:
	ldrb	w0, [x19,3273]
	mov	w1, 3
	mov	w2, 1
	bl	gc_search_src_blk
	cmp	w0, wzr
	bgt	.L3350
	b	.L3511
.L3341:
	cmp	w26, 1
	bne	.L3351
	bl	gc_scan_static_data
	ldr	x0, [x21,608]
	ldrh	w0, [x0,122]
	cbz	w0, .L3352
.L3353:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR4
	strb	w0, [x19,3273]
	b	.L3514
.L3352:
	bl	gc_static_wearleveling
	cbnz	w0, .L3353
	bl	gc_block_vpn_scan
	cbz	w22, .L3445
	cmp	w20, w25
	bcs	.L3356
	ldrh	w0, [x21,3306]
	cmp	w20, w0, lsl 1
	blt	.L3357
.L3356:
	add	x0, x19, :lo12:.LANCHOR0
	add	w1, w20, w25
	ldrh	w2, [x0,3306]
	cmp	w1, w2, lsl 1
	blt	.L3357
	ldrh	w1, [x0,2854]
	ldrh	w0, [x0,3308]
	cmp	w1, w0
	bcc	.L3445
.L3357:
	add	x21, x19, :lo12:.LANCHOR0
	add	x27, x23, :lo12:.LANCHOR4
	mov	w28, 1
	ldr	w1, [x27,1132]
	ldrh	w0, [x21,576]
	add	w1, w1, 1
	strb	w28, [x21,3273]
	str	w1, [x27,1132]
	cmp	w1, w0, lsr 5
	bls	.L3359
	ldrh	w0, [x21,2854]
	cmp	w0, w22
	bls	.L3359
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w0, 65535
	cmp	w1, w0
	beq	.L3445
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x21,600]
	ldrb	w2, [x21,3276]
	mov	w24, 16
	ldrh	w0, [x0,x1]
	ldrh	w1, [x21,3300]
	mul	w1, w1, w2
	cmp	w0, w1
	bgt	.L3355
	str	wzr, [x27,1132]
	mov	w0, w28
	mov	w1, 2
	b	.L3504
.L3359:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2854]
	ldrh	w1, [x0,3308]
	cmp	w2, w1
	bcc	.L3362
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	b	.L3505
.L3362:
	ldrh	w24, [x0,2850]
	cbnz	w24, .L3363
	ldrh	w0, [x0,2852]
	cmp	w0, 8
	bls	.L3355
.L3363:
	mov	w0, 1
	mov	w1, w0
.L3504:
	mov	w2, 4
.L3505:
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3350
	b	.L3445
.L3351:
	ldrh	w0, [x21,2872]
	cmp	w0, w20
	bcc	.L3511
.L3445:
	mov	w24, 16
.L3355:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	w0, [x21,2884]
	cbz	w0, .L3364
	mov	w0, 1
	mov	w1, 5
	strb	w0, [x21,3273]
	mov	w0, 0
	str	wzr, [x21,2884]
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3365
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x21,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 8
	bhi	.L3365
	mov	w2, 1
	mov	w1, 0
	str	w2, [x21,2884]
	bl	gc_add_sblk
	cbnz	w0, .L3350
.L3365:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3366
	add	x3, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x3,600]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3366
	mov	w2, 1
	mov	w1, 0
	str	w2, [x3,2884]
	bl	gc_add_sblk
	cbnz	w0, .L3350
.L3366:
	mov	w0, 0
	bl	zftl_get_gc_node.part.12
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3364
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2,600]
	ldrh	w1, [x3,x1]
	cmp	w1, 4
	bhi	.L3364
	mov	w1, 1
	str	w1, [x2,2884]
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3350
.L3364:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	x7, [x21,608]
	strb	w0, [x21,3273]
	ldrh	w5, [x7,124]
	cbz	w5, .L3367
	add	x23, x23, :lo12:.LANCHOR4
	strb	wzr, [x21,3273]
	strb	w0, [x23,1130]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3368
	ldrh	w6, [x7,120]
	adrp	x0, .LC273
	ldrh	w7, [x7,122]
	mov	w1, 1946
	mov	w2, 0
	mov	w3, w20
	mov	w4, w22
	add	x0, x0, :lo12:.LC273
	bl	printk
	b	.L3368
.L3367:
	ldrh	w2, [x21,2872]
	cmp	w20, w2
	bcs	.L3369
	cbz	w22, .L3370
	cmp	w22, 16
	bls	.L3371
	ldrh	w2, [x21,2854]
	ldrh	w1, [x21,3308]
	cmp	w2, w1
	bhi	.L3371
	mov	w1, w0
	mov	w2, 4
	str	x5, [x29,104]
	bl	gc_search_src_blk
	uxth	w0, w0
	ldr	x5, [x29,104]
	cbnz	w0, .L3372
	ldrb	w0, [x21,3273]
	b	.L3513
.L3372:
	mov	w1, 5
	mov	w0, w5
	bl	zftl_get_gc_node
	uxth	w2, w0
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3439
	add	x3, x23, :lo12:.LANCHOR4
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3,1132]
	add	w1, w1, 1
	str	w1, [x3,1132]
	ldr	x3, [x21,600]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,3300]
	cmp	w3, w2
	bcs	.L3375
	ldrh	w3, [x21,576]
	cmp	w1, w3, lsr 4
	bls	.L3439
	ldrh	w1, [x21,2874]
	cmp	w1, w2
	bls	.L3439
.L3375:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x19,3273]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19,2884]
	add	x0, x23, :lo12:.LANCHOR4
	str	wzr, [x0,1132]
	b	.L3439
.L3371:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3439
	add	x19, x19, :lo12:.LANCHOR0
.L3510:
	ldrb	w0, [x19,3273]
	mov	w1, 3
	mov	w2, 2
.L3507:
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3373
.L3370:
	adrp	x0, .LANCHOR2
	strb	w22, [x21,3273]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3377
	adrp	x0, .LC273
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC273
	mov	w1, 1976
	mov	w2, w22
	mov	w3, w20
	mov	w4, w22
	mov	w5, w22
	bl	printk
.L3377:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, 16
	ldrb	w0, [x19,3273]
	bls	.L3378
.L3513:
	mov	w1, 3
	mov	w2, 4
	b	.L3507
.L3378:
	mov	w1, 1
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3439
	b	.L3510
.L3369:
	cmp	w26, 1
	mov	w1, w24
	bne	.L3484
	cmp	w20, w2, lsl 1
	bge	.L3379
	cmp	w25, w22, lsr 1
	bcs	.L3380
	ldrh	w2, [x21,2854]
	ldrh	w0, [x21,3308]
	cmp	w2, w0
	bcs	.L3380
	ldrh	w0, [x21,3306]
	lsr	w0, w0, 2
	strh	w0, [x21,2872]
	b	.L3484
.L3380:
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w21, w0
	mov	w1, 65535
	mov	w27, w21
	cmp	w21, w1
	bne	.L3381
.L3386:
	cmp	w22, 1
	bhi	.L3382
	b	.L3383
.L3381:
	add	x26, x23, :lo12:.LANCHOR4
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x21, w21
	ldr	w1, [x26,1132]
	ldrh	w3, [x2,576]
	add	w1, w1, 1
	str	w1, [x26,1132]
	cmp	w1, w3, lsr 4
	bls	.L3384
	ldr	x1, [x2,600]
	str	wzr, [x26,1132]
	ldrh	w3, [x1,x21,lsl 1]
	ldrh	w1, [x2,2874]
	cmp	w3, w1
	bcs	.L3384
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	cbz	w0, .L3384
	mov	w0, 1
	strb	w0, [x26,1130]
	b	.L3368
.L3384:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1,600]
	ldrh	w2, [x0,x21,lsl 1]
	ldrh	w0, [x1,3300]
	cmp	w2, w0, lsr 1
	bhi	.L3385
	mov	w0, w27
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L3439
.L3385:
	ldrh	w3, [x1,2850]
	ldrh	w0, [x1,2852]
	add	w0, w3, w0
	ldrh	w3, [x1,3306]
	cmp	w0, w3, lsl 1
	ble	.L3386
	ldrh	w0, [x1,2874]
	cmp	w0, w2
	bcc	.L3383
	b	.L3386
.L3382:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 1
	cmp	w22, 16
	strb	w0, [x21,3273]
	bls	.L3387
	ldrh	w2, [x21,2854]
	ldrh	w1, [x21,3308]
	cmp	w2, w1
	bhi	.L3387
	mov	w1, w0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3388
	ldrb	w0, [x21,3273]
	mov	w1, 3
	mov	w2, 4
	b	.L3506
.L3387:
	mov	w0, 1
	mov	w1, 2
	mov	w2, w0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3388
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 3
	mov	w2, 2
	ldrb	w0, [x0,3273]
.L3506:
	bl	gc_search_src_blk
	uxth	w0, w0
.L3388:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, w25, lsr 1
	ldrh	w1, [x19,3306]
	bls	.L3508
	lsr	w1, w1, 2
	b	.L3509
.L3383:
	cmp	w20, w25
	add	x19, x19, :lo12:.LANCHOR0
	bcs	.L3390
	mov	w0, 4
	strb	wzr, [x19,3273]
	bl	zftl_get_gc_node.part.12
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3390
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x19,600]
	ldrb	w2, [x19,3276]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x19,3300]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	bgt	.L3390
	mov	w1, 3
	mov	w0, 0
	mov	w2, 4
	bl	gc_search_src_blk
	uxth	w0, w0
	ldrh	w1, [x19,3306]
.L3508:
	lsr	w1, w1, 1
.L3509:
	strh	w1, [x19,2872]
	b	.L3373
.L3390:
	ldrh	w0, [x19,3306]
	lsr	w0, w0, 2
	strh	w0, [x19,2872]
	b	.L3368
.L3379:
	ldrh	w0, [x21,3306]
	mov	w24, w5
	lsr	w0, w0, 2
	strh	w0, [x21,2872]
	b	.L3368
.L3373:
	mov	w1, w24
	cbz	w0, .L3484
.L3439:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x23,1130]
	b	.L3368
.L3335:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x20,2896]
	cmp	w1, w0
	bne	.L3393
	bl	gc_get_src_blk
	strh	w0, [x20,2896]
.L3393:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2896
	ldrh	w3, [x0,2896]
	cmp	w3, w2
	beq	.L3512
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,584]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3396
	mov	x0, 0
.L3395:
	cmp	w5, w0, uxth
	bls	.L3396
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3395
.L3401:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2896]
	b	.L3511
.L3396:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L3452
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3398
.L3452:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,600]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3401
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2209
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3401
.L3398:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 2
	b	.L3514
.L3336:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3402
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 3
.L3514:
	strb	w0, [x23,1130]
	b	.L3511
.L3402:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,2896]
	cmp	w0, w3
	beq	.L3350
	ldrh	w3, [x1,2920]
	add	x23, x23, :lo12:.LANCHOR4
	cbz	w3, .L3403
	mov	w0, 4
	strh	wzr, [x1,2922]
	strb	w0, [x23,1130]
	b	.L3511
.L3403:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,600]
	mov	w2, 1
	strb	w2, [x23,1130]
	ldrh	w0, [x1,x0]
	cbz	w0, .L3404
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2237
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3404:
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
	bhi	.L3405
	strh	w0, [x21,30]
	b	.L3401
.L3405:
	strh	wzr, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3401
.L3337:
	add	x21, x19, :lo12:.LANCHOR0
.L3485:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,2898]
	add	x0, x21, 2896
	ldrh	w1, [x21,3300]
	cmp	w2, w1
	bcs	.L3407
	cmp	w20, 7
	bls	.L3485
	b	.L3511
.L3407:
	ldrh	w3, [x0,24]
	adrp	x2, .LANCHOR2
	cbz	w3, .L3408
	add	x23, x23, :lo12:.LANCHOR4
	mov	w1, 4
	strh	wzr, [x0,26]
	strb	w1, [x23,1130]
	ldr	w1, [x2,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3409
	ldrh	w1, [x0]
	adrp	x0, .LC274
	ldr	x4, [x21,600]
	add	x0, x0, :lo12:.LC274
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L3409:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2896]
	ldr	x1, [x0,600]
	ldrh	w0, [x0,2920]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L3410
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2271
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3410:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,2896]
	ldr	x0, [x19,600]
	ldrh	w2, [x19,2920]
	strh	w2, [x0,x1,lsl 1]
	b	.L3511
.L3408:
	add	x23, x23, :lo12:.LANCHOR4
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x20, [x21,584]
	add	x20, x20, x1, uxth 2
	strb	w0, [x23,1130]
	ldr	w0, [x2,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3411
	ldrb	w2, [x20,2]
	adrp	x0, .LC275
	add	x0, x0, :lo12:.LC275
	ubfx	x2, x2, 5, 3
	bl	printk
.L3411:
	ldrb	w0, [x20,2]
	tst	w0, 192
	beq	.L3453
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3412
.L3453:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2281
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3412:
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
	bhi	.L3414
	strh	w0, [x20,30]
	b	.L3511
.L3414:
	strh	wzr, [x20,30]
	b	.L3515
.L3338:
	cbnz	w26, .L3415
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2872]
	cmp	w0, w20
	bcc	.L3511
.L3415:
	ldrh	w1, [x24,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3416
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w21, [x22,3273]
	cmp	w21, 1
	bne	.L3416
	bl	ftl_flush
	ldrh	w0, [x22,3214]
	cbz	w0, .L3417
	mov	w0, w21
.L3417:
	mov	w1, 5
	bl	zftl_gc_get_free_sblk
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	beq	.L3419
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x21, [x0,584]
	add	x21, x21, x20, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L3420
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2310
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3420:
	ldrb	w0, [x21,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x21,2]
.L3440:
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
	b	.L3511
.L3416:
	cmp	w26, 1
	mov	w21, 4
	csinc	w21, w21, wzr, eq
	cmp	w20, 15
	add	w0, w21, 4
	add	x20, x19, :lo12:.LANCHOR0
	csel	w21, w0, w21, ls
.L3423:
	sub	w21, w21, #1
	uxtb	w21, w21
	cmp	w21, 255
	beq	.L3511
	bl	gc_do_copy_back
	ldrb	w0, [x20,3273]
	cbnz	w0, .L3424
	ldrb	w0, [x20,2834]
	cmp	w0, 3
	bhi	.L3425
	bl	ftl_write_commit
.L3425:
	ldrh	w1, [x20,2922]
	ldrh	w0, [x20,2920]
	cmp	w1, w0
	bcc	.L3423
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x23,1130]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x20,2896]
	ldr	x0, [x20,600]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3427
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2385
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3427:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2896]
	ldr	x3, [x1,600]
	ubfiz	x2, x0, 1, 16
	ldrh	w4, [x3,x2]
	cbnz	w4, .L3428
	strh	w4, [x3,x2]
	ldrh	w0, [x1,2896]
	bl	ftl_free_sblk
	b	.L3401
.L3428:
	mov	w1, 0
	mov	w2, 1
	bl	gc_add_sblk
	b	.L3401
.L3424:
	add	x0, x20, 2896
	ldrh	w1, [x0,320]
	cbz	w1, .L3429
	strh	wzr, [x0,320]
	ldr	w0, [x0,324]
	cmn	w0, #1
	beq	.L3430
	bl	ftl_mask_bad_block
.L3430:
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
	cbz	x0, .L3431
	bl	buf_free
.L3431:
	add	x19, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LANCHOR4
	str	xzr, [x19,2904]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2421
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x23,1130]
	bl	printk
	bl	dump_stack
	b	.L3511
.L3429:
	ldrh	w1, [x24,86]
	cmp	w1, 1
	bls	.L3432
	ldrh	w2, [x0,26]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L3423
	add	x23, x23, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x23,1130]
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
	b	.L3511
.L3432:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w1, 5
	ldrh	w2, [x0,26]
	strb	w1, [x23,1130]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L3433
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
.L3433:
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
	bne	.L3434
	lsl	w1, w1, 1
	strh	w1, [x20,2914]
	mov	w1, 1
	strh	w1, [x20,2916]
.L3434:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2918]
	b	.L3511
.L3442:
	mov	w21, 0
	add	x24, x19, :lo12:.LANCHOR0
.L3339:
	bl	gc_check_data_one_wl
	cbz	w0, .L3436
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
.L3515:
	bl	flt_sys_flush
	b	.L3511
.L3436:
	ldrh	w1, [x24,2912]
	ldrh	w0, [x24,2914]
	cmp	w1, w0
	bcc	.L3437
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, 6
	strb	w0, [x23,1130]
	ldr	x0, [x24,2904]
	bl	buf_free
	str	xzr, [x24,2904]
	b	.L3511
.L3437:
	cmp	w20, 15
	bls	.L3339
	cmp	w26, 1
	bne	.L3511
	add	w21, w21, 1
	uxtb	w21, w21
	cmp	w21, 4
	bls	.L3339
	b	.L3511
.L3340:
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
.L3512:
	add	x23, x23, :lo12:.LANCHOR4
	strb	wzr, [x23,1130]
.L3511:
	mov	w1, 16
	b	.L3484
.L3368:
	mov	w1, w24
	b	.L3484
.L3419:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2316
	add	x1, x1, 800
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3440
.L3484:
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
	tbz	x0, 12, .L3517
	adrp	x0, .LC276
	mov	w1, w19
	add	x0, x0, :lo12:.LC276
	mov	w2, w28
	mov	w3, w23
	bl	printk
.L3517:
	cbnz	w19, .L3518
	adrp	x0, .LANCHOR0+520
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+520]
	b	.L3519
.L3518:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3520
	lsl	w19, w19, 13
	mov	w2, 8192
.L3519:
	add	w1, w28, w23
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3520
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
.L3521:
	cbz	w20, .L3540
	mov	w0, 0
	bl	buf_alloc
	mov	x27, x0
	cbnz	x0, .L3522
	bl	ftl_write_commit
	b	.L3521
.L3522:
	strb	wzr, [x0,57]
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w21, w24
	ldrb	w4, [x0,2832]
	cset	w0, eq
	strb	w4, [x27,56]
	cmp	w21, w22
	beq	.L3524
	cbz	w0, .L3525
	b	.L3534
.L3524:
	cbz	w0, .L3526
.L3534:
	udiv	w0, w28, w4
	msub	w0, w0, w4, w28
	uxtb	w0, w0
	strb	w0, [x27,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w23
	csel	w4, w23, w4, hi
	b	.L3538
.L3526:
	msub	w4, w4, w21, w26
.L3538:
	strb	w4, [x27,56]
.L3525:
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
	b	.L3521
.L3540:
	bl	ftl_write_commit
	mov	w0, w20
.L3539:
	mov	w1, 1
	bl	zftl_do_gc
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,2844]
	ldrh	w0, [x1,2848]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L3541
	mov	w0, 0
	b	.L3539
.L3541:
	bl	timer_get_time
	adrp	x1, .LANCHOR7-76
	str	w0, [x1,#:lo12:.LANCHOR7-76]
	mov	w0, 0
.L3520:
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
	bhi	.L3545
	adrp	x23, .LANCHOR7
	add	w24, w0, 24576
	add	x0, x23, :lo12:.LANCHOR7
	adrp	x25, .LANCHOR2
	ldr	w1, [x0,-72]
	add	w1, w19, w1
	str	w1, [x0,-72]
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3546
	adrp	x0, .LC277
	mov	w2, w24
	add	x0, x0, :lo12:.LC277
	mov	w3, w19
	mov	w4, 0
	bl	printk
.L3546:
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
	cbz	w27, .L3547
	sub	w22, w22, w27
	mov	w0, w20
	cmp	w22, w19
	csel	w22, w22, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3548
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3548:
	ldr	w0, [x29,120]
	and	w28, w22, 65535
	cmn	w0, #1
	beq	.L3550
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3550
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
.L3550:
	add	w20, w20, 1
	sub	w19, w19, w28
.L3547:
	cbz	w19, .L3552
	bl	ftl_flush
.L3552:
	mov	w0, -1
	mov	w27, 1
	str	w0, [x29,124]
	mov	w28, 21
.L3553:
	add	x22, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,2832]
	cmp	w19, w0
	bcc	.L3592
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L3554
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3556
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
	b	.L3591
.L3554:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3556
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
.L3591:
	ldr	x1, [x22,2864]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3556:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,2832]
	sub	w19, w19, w0
	b	.L3553
.L3592:
	cbz	w19, .L3560
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3561
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3561:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3560
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L3560
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w26, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,2832]
	cmp	w19, w0
	bcc	.L3563
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1235
	add	x1, x1, 816
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3563:
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
.L3560:
	add	x0, x23, :lo12:.LANCHOR7
	mov	w4, 0
	ldr	w1, [x0,-72]
	cmp	w1, 8192
	bls	.L3545
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3564
	adrp	x0, .LC277
	mov	w2, w24
	add	x0, x0, :lo12:.LC277
	mov	w3, w19
	bl	printk
.L3564:
	add	x23, x23, :lo12:.LANCHOR7
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x23,-72]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x21,2884]
.L3545:
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
	cbnz	w0, .L3593
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L3595
	cmp	w2, 68
	beq	.L3595
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L3593
.L3595:
	adrp	x24, .LC278
	mov	w25, w1
	add	x24, x24, :lo12:.LC278
	add	x20, x20, :lo12:.LANCHOR4
	mov	w27, 65535
.L3597:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,624]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L3593
	add	x22, x20, 68
	add	w26, w19, w25
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	ldrh	w3, [x22,w19,sxtw 1]
	bl	printk
	ldrh	w0, [x22,w19,sxtw 1]
	cmp	w0, w27
	bne	.L3593
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
	b	.L3597
.L3593:
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
	mov	x26, x1
	str	x2, [x29,128]
	ldr	x0, [x0,624]
	ldrb	w21, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x23, x0
	mov	w0, -1
	cbz	x23, .L3644
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L3630
	cmp	w19, 500
	bhi	.L3644
	b	.L3601
.L3630:
	mov	w19, 8
.L3601:
	mul	w20, w20, w21
	add	x3, x26, 253952
	mov	w2, 63999
	add	x3, x3, 2044
	mov	w4, 4097
	uxth	w0, w20
	str	w0, [x29,152]
	udiv	w0, w19, w0
	str	w0, [x29,120]
	mov	w0, 0
.L3605:
	ldr	w1, [x3]
	cbnz	w1, .L3602
	ldr	w1, [x26,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w4
	sub	w2, w2, #1
	csel	w0, w0, wzr, cc
	str	w1, [x3],-4
	cmp	w2, 4096
	bne	.L3605
	b	.L3604
.L3602:
	adrp	x0, .LC279
	add	x0, x0, :lo12:.LC279
	bl	printk
.L3604:
	ldr	x1, [x29,128]
	adrp	x0, .LC280
	add	x0, x0, :lo12:.LC280
	mov	w2, 4
	mov	w3, 5
	mov	x21, 0
	mov	w28, w21
	bl	rknand_print_hex
	adrp	x0, .LC281
	mov	w1, w19
	add	x0, x0, :lo12:.LC281
	mov	w2, w19
	bl	printk
.L3627:
	adrp	x1, .LANCHOR7
	add	x1, x1, :lo12:.LANCHOR7
	ldrb	w1, [x1,-102]
	cbnz	w1, .L3606
	ldr	x1, [x29,128]
	ldr	w2, [x29,152]
	ldr	w1, [x1,x21,lsl 2]
	mul	w20, w1, w2
	cbz	w21, .L3607
	ldr	w2, [x29,120]
	cmp	w2, 1
	bls	.L3607
	sub	w0, w21, #1
	ldr	x2, [x29,128]
	ldr	w0, [x2,x0,lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L3607
	b	.L3608
.L3606:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,728]
	add	x0, x0, x21, uxtw
	ldrb	w20, [x0,32]
	cmp	w20, 255
	beq	.L3608
	ldr	w0, [x29,152]
	mul	w20, w20, w0
.L3607:
	mov	w2, 512
	mov	w1, 0
	mov	x0, x23
	bl	ftl_memset
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0,624]
	ldrb	w25, [x1,9]
	ldrh	w24, [x1,10]
	ldrb	w1, [x0,720]
	str	w1, [x29,116]
	udiv	w1, w20, w25
	strb	wzr, [x0,720]
	mul	w24, w25, w24
	mov	w0, 0
	uxth	w24, w24
	bl	flash_erase_block
	cmp	w19, w24
	mov	w2, 1
	bls	.L3609
	mov	w0, 0
	add	w1, w20, w24
	bl	flash_erase_block
	mov	w2, 2
.L3609:
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
	str	w0, [x29,156]
	cmp	w20, w0
	bne	.L3634
	ldrb	w0, [x27,516]
	cmp	w0, 9
	bne	.L3634
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
	cmp	w25, 8
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
	b	.L3610
.L3634:
	str	w19, [x29,144]
	mov	x3, 0
.L3610:
	str	x26, [x29,136]
	mov	w24, 0
	add	x27, x22, :lo12:.LANCHOR0
.L3612:
	ldr	w0, [x29,112]
	cmp	w24, w0
	bcs	.L3653
	ldr	w0, [x29,124]
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w24, w0
	add	x0, x27, 4
	ubfx	x4, x4, 2, 16
	add	w1, w4, 1
	ldrb	w2, [x2,1264]
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L3614
	ldrb	w6, [x27,1]
	lsl	w0, w1, 1
	cmp	w6, wzr
	csel	w0, w0, w1, ne
.L3614:
	ldrb	w1, [x27,516]
	cmp	w1, 9
	beq	.L3652
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L3652:
	str	w0, [x29,160]
	mov	w0, 61424
	str	w0, [x29,164]
	add	x0, x27, 4
	ldrh	w0, [x0,w4,sxtw 1]
	cbnz	w2, .L3618
	mov	w4, w0
	b	.L3619
.L3618:
	ldrb	w1, [x27,1]
	lsl	w0, w4, 1
	cmp	w1, wzr
	csel	w4, w0, w4, ne
.L3619:
	mul	w0, w4, w25
	cbnz	x3, .L3620
	ldr	w1, [x29,156]
	add	x2, x29, 160
	str	x4, [x29,104]
	add	w0, w0, w1
	ldr	x1, [x29,136]
	bl	fw_flash_page_prog.constprop.28
	adrp	x0, .LANCHOR4
	ldr	x4, [x29,104]
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1264]
	cbnz	w0, .L3621
	ldr	w1, [x29,156]
	add	w2, w4, 1
	udiv	w1, w1, w25
	bl	id_block_prog_msb_ff_data
.L3621:
	ldr	x0, [x29,136]
	add	x0, x0, 2048
	str	x0, [x29,136]
	b	.L3622
.L3620:
	ldr	w1, [x29,156]
	add	x2, x29, 160
	add	w0, w0, w1
	mov	x1, x3
	bl	fw_flash_page_prog.constprop.28
.L3622:
	add	w24, w24, 4
	mov	x3, 0
	uxth	w24, w24
	b	.L3612
.L3653:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x29,116]
	mov	x2, x23
	strb	w1, [x0,720]
	mov	w0, w20
	ldr	w1, [x29,144]
	bl	id_block_read_data.constprop.27
	ldr	w0, [x29,144]
	mov	x1, 0
	lsl	w0, w0, 7
.L3624:
	cmp	w0, w1
	bls	.L3654
	ldr	w3, [x23,x1,lsl 2]
	add	x1, x1, 1
	add	x2, x26, x1, lsl 2
	ldr	w2, [x2,-4]
	cmp	w3, w2
	beq	.L3624
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L3608
.L3654:
	add	w28, w28, 1
	cmp	w28, 5
	bhi	.L3628
.L3608:
	add	x21, x21, 1
	cmp	x21, 4
	bne	.L3627
.L3628:
	mov	x0, x23
	bl	ftl_free
	cmp	w28, wzr
	csetm	w0, eq
.L3644:
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
.L3656:
	cmp	w1, w2
	bls	.L3658
	ldrb	w4, [x5,x2]
	add	x6, x3, 2296
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3656
.L3658:
	ret
	.size	CRC_32, .-CRC_32
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
	beq	.L3661
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3662
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3663
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3664
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3665
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3666
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3723
	bl	rknand_dev_flush
	b	.L3757
.L3666:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3668
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3669
	b	.L3723
.L3664:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3670
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3671
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3672
	b	.L3723
.L3671:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3670
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3670
	b	.L3723
.L3662:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3673
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3674
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3675
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3676
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3677
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3678
	b	.L3723
.L3674:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3679
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3680
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3681
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3682
	b	.L3723
.L3680:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3682
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3679
	b	.L3723
.L3676:
	adrp	x0, .LC282
	add	x0, x0, :lo12:.LC282
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3683
.L3688:
	mov	x0, -12
	b	.L3660
.L3683:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3764
	adrp	x0, .LC284
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC284
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3761
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	id_block_read_data.constprop.27
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L3767
	adrp	x0, .LC285
	add	x0, x0, :lo12:.LC285
.L3759:
	bl	printk
.L3761:
	mov	x0, x19
.L3762:
	bl	ftl_free
.L3760:
	mov	x0, -14
	b	.L3660
.L3675:
	adrp	x0, .LC286
	add	x0, x0, :lo12:.LC286
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3688
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3764
	adrp	x0, .LC287
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC287
	bl	printk
	adrp	x0, .LANCHOR7
	add	x22, x0, :lo12:.LANCHOR7
	mov	x20, x0
	ldr	x21, [x22,-64]
	cbnz	x21, .L3690
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x22,-64]
	cbz	x0, .L3761
	mov	w1, w21
	mov	x2, 260096
	bl	memset
.L3690:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3761
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3761
	add	x20, x20, :lo12:.LANCHOR7
	uxtw	x2, w2
	ldr	x1, [x20,-64]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3767:
	mov	x0, x19
	bl	ftl_free
	b	.L3757
.L3678:
	adrp	x0, .LC288
	add	x0, x0, :lo12:.LC288
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3688
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3693
.L3764:
	adrp	x0, .LC283
	add	x0, x0, :lo12:.LC283
	b	.L3759
.L3693:
	ldr	w1, [x19]
	adrp	x0, .LC289
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC289
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3761
	adrp	x20, .LANCHOR7
	add	x20, x20, :lo12:.LANCHOR7
	ldr	x0, [x20,-64]
	cbz	x0, .L3761
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3696
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3660
.L3696:
	bl	rknand_device_lock
	ldr	x1, [x20,-64]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,-64]
	bl	ftl_free
	str	xzr, [x20,-64]
	b	.L3767
.L3677:
	adrp	x0, .LC290
	add	x0, x0, :lo12:.LC290
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3688
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3769
.L3661:
	adrp	x0, .LC291
	add	x0, x0, :lo12:.LC291
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3688
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3769
.L3673:
	adrp	x0, .LC292
	add	x0, x0, :lo12:.LC292
	bl	printk
	adrp	x0, .LANCHOR7-56
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR7-56]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3752:
	bl	rk_copy_to_user
	cbnz	x0, .L3760
	b	.L3757
.L3681:
	adrp	x0, .LC293
	add	x0, x0, :lo12:.LC293
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3688
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
.L3769:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3762
	bl	ftl_free
	b	.L3687
.L3665:
	adrp	x0, .LC294
	add	x0, x0, :lo12:.LC294
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3701
.L3706:
	adrp	x0, .LC283
	add	x0, x0, :lo12:.LC283
	bl	printk
	b	.L3760
.L3701:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3702
.L3703:
	mov	x19, -1
	b	.L3687
.L3702:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3703
	adrp	x21, .LANCHOR7
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR7
	mov	x2, 512
	ldr	x1, [x21,-56]
	bl	memcpy
	ldr	w1, [x21,-48]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3704
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3704:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3766
.L3668:
	adrp	x0, .LC295
	add	x0, x0, :lo12:.LC295
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3703
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3703
	adrp	x1, .LANCHOR7
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR7
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,-48]
	cmp	w3, w2
	bne	.L3660
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3660
	ldr	x19, [x1,-56]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3758
.L3672:
	adrp	x0, .LC296
	add	x0, x0, :lo12:.LC296
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3703
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3703
	adrp	x19, .LANCHOR7
	add	x0, x19, :lo12:.LANCHOR7
	ldr	w1, [x0,-44]
	cbnz	w1, .L3707
.L3710:
	mov	x0, 0
	b	.L3660
.L3707:
	ldr	x1, [x0,-40]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3708
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,-40]
	str	w2, [x1,4]
	ldr	x0, [x0,-40]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3708:
	add	x20, x19, :lo12:.LANCHOR7
	mov	w0, 0
	ldr	x1, [x20,-40]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,-56]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3709
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,-56]
	str	w1, [x0,4]
	ldr	x0, [x20,-56]
	str	wzr, [x0,8]
.L3709:
	add	x19, x19, :lo12:.LANCHOR7
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,-56]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,-44]
	str	wzr, [x19,-48]
	b	.L3757
.L3663:
	adrp	x0, .LC297
	add	x0, x0, :lo12:.LC297
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3703
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3703
	adrp	x19, .LANCHOR7
	add	x1, x19, :lo12:.LANCHOR7
	ldr	w2, [x1,-44]
	cmp	w2, 1
	beq	.L3710
	ldr	x2, [x1,-40]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3711
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,-40]
	str	w3, [x2,4]
	ldr	x1, [x1,-40]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3711:
	add	x20, x19, :lo12:.LANCHOR7
	mov	w0, 1
	ldr	x1, [x20,-40]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,-56]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3712
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,-56]
	str	w1, [x0,4]
	ldr	x0, [x20,-56]
	str	wzr, [x0,8]
.L3712:
	add	x19, x19, :lo12:.LANCHOR7
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,-56]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,-44]
	b	.L3757
.L3669:
	adrp	x0, .LC298
	add	x0, x0, :lo12:.LC298
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3703
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3703
	adrp	x1, .LANCHOR7
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR7
	uxtw	x2, w2
	sub	x1, x1, #32
	b	.L3765
.L3670:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3713
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	b	.L3755
.L3713:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3715
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	b	.L3755
.L3715:
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
.L3755:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3760
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR7
	bne	.L3716
	add	x0, x0, :lo12:.LANCHOR7
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,-56]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3660
	b	.L3760
.L3716:
	add	x20, x0, :lo12:.LANCHOR7
	ldr	w1, [x20,480]
	cmp	w1, 10
	bhi	.L3760
	ldr	x1, [x20,-56]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3717
	cbz	w3, .L3717
	adrp	x0, .LC302
	mov	w1, w2
	add	x0, x0, :lo12:.LC302
	bl	printk
	ldr	w0, [x20,480]
	add	w0, w0, 1
	str	w0, [x20,480]
	b	.L3760
.L3717:
	add	x0, x0, :lo12:.LANCHOR7
	str	wzr, [x0,480]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3718
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3719
.L3718:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3719:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3757
	b	.L3687
.L3682:
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3703
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3703
	mov	w0, 30224
	adrp	x1, .LANCHOR7
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR7
	add	x0, x29, 64
	bne	.L3720
	ldr	x1, [x1,488]
	b	.L3756
.L3720:
	ldr	x1, [x1,496]
.L3756:
	add	x1, x1, 8
.L3765:
	bl	memcpy
.L3766:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3752
.L3679:
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3706
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3703
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3703
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR7
	add	x19, x19, :lo12:.LANCHOR7
	bne	.L3722
	ldr	x0, [x19,488]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,488]
	b	.L3758
.L3722:
	ldr	x0, [x19,496]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,496]
	mov	w0, 3
.L3758:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3687
.L3757:
	mov	x19, 0
.L3687:
	adrp	x0, .LC305
	mov	x1, x19
	add	x0, x0, :lo12:.LC305
	bl	printk
	mov	x0, x19
	b	.L3660
.L3723:
	mov	x0, -22
.L3660:
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
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR7
	add	x19, x20, :lo12:.LANCHOR7
	stp	x21, x22, [sp,32]
	ldr	x1, [x19,-96]
	str	x1, [x19,-40]
	add	x0, x1, 512
	str	x0, [x19,-56]
	add	x0, x1, 1024
	add	x1, x1, 1536
	str	x0, [x19,488]
	sub	x0, x19, #32
	str	xzr, [x19,-64]
	str	x1, [x19,496]
	bl	ftl_memcpy
	ldr	x21, [x19,-40]
	str	wzr, [x19,-48]
	str	wzr, [x19,480]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,-44]
	cbz	w22, .L3772
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3772
	adrp	x0, .LC306
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC306
	str	wzr, [x19,-44]
	bl	printk
.L3772:
	add	x0, x20, :lo12:.LANCHOR7
	ldr	w1, [x0,-44]
	cbz	w1, .L3773
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,-48]
.L3773:
	add	x20, x20, :lo12:.LANCHOR7
	mov	w0, 2
	ldr	x1, [x20,488]
	bl	StorageSysDataLoad
	ldr	x1, [x20,496]
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
	str	x0, [x1,504]
	cbz	x0, .L3782
	mov	w23, 0
	adrp	x24, .LC307
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC307
.L3786:
	add	x20, x21, :lo12:.LANCHOR7
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,504]
	bl	FlashBootVendorRead
	cbnz	w0, .L3783
	ldr	x3, [x20,504]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,504]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3784
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3784
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3784:
	cbnz	w22, .L3794
	mov	w22, 1
	b	.L3786
.L3794:
	cbz	w19, .L3787
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3782
	b	.L3783
.L3787:
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
	b	.L3782
.L3783:
	add	x21, x21, :lo12:.LANCHOR7
	mov	w19, -1
	ldr	x0, [x21,504]
	bl	kfree
	str	xzr, [x21,504]
.L3782:
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
	adrp	x3, .LANCHOR7+504
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR7+504]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3796
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3797:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3801
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3797
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
	b	.L3796
.L3801:
	mov	w3, -1
.L3796:
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
	adrp	x0, .LANCHOR7+504
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR7+504]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3803
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3804:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3819
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3804
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3805
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3803
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3806:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3820
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
	b	.L3806
.L3820:
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
	b	.L3818
.L3805:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3818
.L3819:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3803
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
.L3818:
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
.L3803:
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
	cbz	x0, .L3822
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3824
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3825
	b	.L3836
.L3824:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3836
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3827
.L3828:
	mov	x20, -1
	b	.L3823
.L3827:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3828
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3823
.L3825:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3836
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3828
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3828
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3836
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3823
.L3836:
	mov	x20, -14
.L3823:
	mov	x0, x19
	bl	kfree
.L3822:
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
	.type	__func__.33878, %object
	.size	__func__.33878, 18
__func__.33878:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.33903, %object
	.size	__func__.33903, 23
__func__.33903:
	.string	"_list_update_data_list"
	.zero	1
	.type	__func__.18927, %object
	.size	__func__.18927, 22
__func__.18927:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.19012, %object
	.size	__func__.19012, 11
__func__.19012:
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
	.type	__func__.18515, %object
	.size	__func__.18515, 28
__func__.18515:
	.string	"flash_wait_device_ready_raw"
	.zero	4
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
	.type	__func__.18568, %object
	.size	__func__.18568, 22
__func__.18568:
	.string	"flash_start_page_read"
	.zero	2
	.type	__func__.18593, %object
	.size	__func__.18593, 23
__func__.18593:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.18490, %object
	.size	__func__.18490, 26
__func__.18490:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.18501, %object
	.size	__func__.18501, 21
__func__.18501:
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
	.type	__func__.19182, %object
	.size	__func__.19182, 13
__func__.19182:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.19195, %object
	.size	__func__.19195, 10
__func__.19195:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.19209, %object
	.size	__func__.19209, 16
__func__.19209:
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
	.type	__func__.33472, %object
	.size	__func__.33472, 12
__func__.33472:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.33540, %object
	.size	__func__.33540, 19
__func__.33540:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.33591, %object
	.size	__func__.33591, 16
__func__.33591:
	.string	"gc_free_src_blk"
	.type	__func__.34084, %object
	.size	__func__.34084, 18
__func__.34084:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.34094, %object
	.size	__func__.34094, 17
__func__.34094:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.34198, %object
	.size	__func__.34198, 23
__func__.34198:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.34237, %object
	.size	__func__.34237, 22
__func__.34237:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.34248, %object
	.size	__func__.34248, 22
__func__.34248:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.33254, %object
	.size	__func__.33254, 14
__func__.33254:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.33298, %object
	.size	__func__.33298, 18
__func__.33298:
	.string	"zftl_add_read_buf"
	.zero	6
	.type	__func__.34043, %object
	.size	__func__.34043, 15
__func__.34043:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.34595, %object
	.size	__func__.34595, 17
__func__.34595:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.34623, %object
	.size	__func__.34623, 21
__func__.34623:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.18917, %object
	.size	__func__.18917, 20
__func__.18917:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.33189, %object
	.size	__func__.33189, 17
__func__.33189:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.34559, %object
	.size	__func__.34559, 13
__func__.34559:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.19232, %object
	.size	__func__.19232, 21
__func__.19232:
	.string	"flash_into_data_init"
	.zero	3
	.type	__func__.18446, %object
	.size	__func__.18446, 19
__func__.18446:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.19221, %object
	.size	__func__.19221, 20
__func__.19221:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.18581, %object
	.size	__func__.18581, 31
__func__.18581:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.18606, %object
	.size	__func__.18606, 25
__func__.18606:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.34426, %object
	.size	__func__.34426, 31
__func__.34426:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.34506, %object
	.size	__func__.34506, 15
__func__.34506:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.33493, %object
	.size	__func__.33493, 21
__func__.33493:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.18539, %object
	.size	__func__.18539, 26
__func__.18539:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.18557, %object
	.size	__func__.18557, 22
__func__.18557:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.34479, %object
	.size	__func__.34479, 15
__func__.34479:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.18475, %object
	.size	__func__.18475, 19
__func__.18475:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.34309, %object
	.size	__func__.34309, 14
__func__.34309:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.34011, %object
	.size	__func__.34011, 15
__func__.34011:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.34273, %object
	.size	__func__.34273, 19
__func__.34273:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.34285, %object
	.size	__func__.34285, 18
__func__.34285:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.34172, %object
	.size	__func__.34172, 23
__func__.34172:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.34605, %object
	.size	__func__.34605, 14
__func__.34605:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.19254, %object
	.size	__func__.19254, 17
__func__.19254:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.18971, %object
	.size	__func__.18971, 16
__func__.18971:
	.string	"nand_flash_init"
	.type	__func__.34153, %object
	.size	__func__.34153, 16
__func__.34153:
	.string	"ftl_sysblk_dump"
	.type	__func__.34545, %object
	.size	__func__.34545, 16
__func__.34545:
	.string	"load_l2p_region"
	.type	__func__.34579, %object
	.size	__func__.34579, 6
__func__.34579:
	.string	"pm_gc"
	.zero	2
	.type	__func__.33527, %object
	.size	__func__.33527, 22
__func__.33527:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.33636, %object
	.size	__func__.33636, 16
__func__.33636:
	.string	"gc_scan_src_blk"
	.type	__func__.33694, %object
	.size	__func__.33694, 20
__func__.33694:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.33757, %object
	.size	__func__.33757, 18
__func__.33757:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.34129, %object
	.size	__func__.34129, 14
__func__.34129:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.33331, %object
	.size	__func__.33331, 10
__func__.33331:
	.string	"zftl_read"
	.zero	6
	.type	__func__.34219, %object
	.size	__func__.34219, 19
__func__.34219:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.33236, %object
	.size	__func__.33236, 17
__func__.33236:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.33569, %object
	.size	__func__.33569, 16
__func__.33569:
	.string	"gc_do_copy_back"
	.type	__func__.33785, %object
	.size	__func__.33785, 11
__func__.33785:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.33362, %object
	.size	__func__.33362, 13
__func__.33362:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.8 20180615"
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
	.string	"flash_erase_block %x %x\n"
.LC44:
	.string	"flash_erase_block block = %x status = %x\n"
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
	.string	"ftl_vpn_decrement %x = %d\n"
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
	.string	"micron %d row=%x,count %d,status=%d\n"
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
	.string	"flash_read_page_en %x %x %x\n"
.LC153:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC154:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC155:
	.string	"...%d @ %s %d %p\n"
.LC156:
	.string	"no sys info %x\n"
.LC157:
	.string	"read page: %x %x %x %x\n"
.LC158:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC159:
	.string	"read: %x %x %x %x\n"
.LC160:
	.string	"set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC161:
	.string	"1ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC162:
	.string	"flash_prog_page %x %x %x\n"
.LC163:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC164:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC165:
	.string	"flash_prog_page_en:%x %x\n"
.LC166:
	.string	"spare"
.LC167:
	.string	"data"
.LC168:
	.string	"write error: %x\n"
.LC169:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC170:
	.string	"%d %x @%d %x\n"
.LC171:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC172:
	.string	"%s %d %d %x %x\n"
.LC173:
	.string	"saved_active_page  = %x\n"
.LC174:
	.string	"saved_active_plane = %x\n"
.LC175:
	.string	"sblk = %x\n"
.LC176:
	.string	"phy_blk = %x %x\n"
.LC177:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC178:
	.string	"dump write = %x\n"
.LC179:
	.string	"pm_write_page write error: %x\n"
.LC180:
	.string	"finfo:"
.LC181:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC182:
	.string	"sys_info_flush error:%x\n"
.LC183:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC184:
	.string	"l2p:"
.LC185:
	.string	"free_buf_count: %d\n"
.LC186:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC187:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC188:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC189:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC190:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC191:
	.string	"lpa:"
.LC192:
	.string	"vpn:"
.LC193:
	.string	"sblk:"
.LC194:
	.string	"lpa_hash:"
.LC195:
	.string	"lpa_hash_index:"
.LC196:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC197:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC198:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC199:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC200:
	.string	"pm_ppa:"
.LC201:
	.string	"data:"
.LC202:
	.string	"spare:"
.LC203:
	.string	"pm_init posr %x %x %x\n"
.LC204:
	.string	"pm_init recovery %x %x %x\n"
.LC205:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC206:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC207:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC208:
	.string	"gc_recovery: %x vpn = %x\n"
.LC209:
	.string	"_c_user_data_density := %d\n"
.LC210:
	.string	"_c_totle_phy_density := %d\n"
.LC211:
	.string	"_c_totle_log_page := %d\n"
.LC212:
	.string	"_c_totle_data_density := %d\n"
.LC213:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC214:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC215:
	.string	"_c_max_pm_sblk := %d\n"
.LC216:
	.string	"_min_slc_super_block := %d\n"
.LC217:
	.string	"_max_xlc_super_block := %d\n"
.LC218:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC219:
	.string	"flash info size: %d %d %d\n"
.LC220:
	.string	"ftl_init %x\n"
.LC221:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC222:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC223:
	.string	"lpa: %x %x %x\n"
.LC224:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC225:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC226:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC227:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC228:
	.string	"ftl_sblk_dump = %x %d %d %d %d\n"
.LC229:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC230:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC231:
	.string	"index= %x, lpa=%x\n"
.LC232:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC233:
	.string	"ftl_read %x %x %x\n"
.LC234:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC235:
	.string	"id=%d, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC236:
	.string	"zftl debug cmd: %s\n"
.LC237:
	.string	"cmd:"
.LC238:
	.string	"dumpl2p"
.LC239:
	.string	"pm l2p:"
.LC240:
	.string	"pm blk:"
.LC241:
	.string	"dumppm:"
.LC242:
	.string	"p_cmd: %s\n"
.LC243:
	.string	"pm ram = %x, %x\n"
.LC244:
	.string	"ram:"
.LC245:
	.string	"pm:"
.LC246:
	.string	"dumpsys"
.LC247:
	.string	"dumplist:"
.LC248:
	.string	"vpncheck"
.LC249:
	.string	"dumpppa:"
.LC250:
	.string	"dumpblk:"
.LC251:
	.string	"setzdebug:"
.LC252:
	.string	"lpa2ppa:"
.LC253:
	.string	"lpa: %x--> ppa: %x\n"
.LC254:
	.string	"help:\n"
.LC255:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC256:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC257:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC258:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC259:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC260:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC261:
	.string	"7. echo dumplist > /proc/zftl_debug\n"
.LC262:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC263:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC264:
	.string	"ftl_update_l2p_map"
.LC265:
	.string	"lpa_tbl:"
.LC266:
	.string	"sblk %x vpn: %d %d\n"
.LC267:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC268:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC269:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC270:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x\n"
.LC271:
	.string	"%d prog_step: %x %x buf id= %x ppa = %x hash=%x id = %x plane = %x lpa=%x\n"
.LC272:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC273:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC274:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC275:
	.string	"gc free %x, %d\n"
.LC276:
	.string	"ftlwrite %x %x %x\n"
.LC277:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC278:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC279:
	.string	"write_idblock fix data %x %x\n"
.LC280:
	.string	"idblk:"
.LC281:
	.string	"write_idblock totle_sec %x %x\n"
.LC282:
	.string	"READ_SECTOR_IO\n"
.LC283:
	.string	"rk_copy_from_user error\n"
.LC284:
	.string	"READ_SECTOR_IO %x %x\n"
.LC285:
	.string	"rk_copy_to_user error\n"
.LC286:
	.string	"WRITE_SECTOR_IO\n"
.LC287:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC288:
	.string	"END_WRITE_SECTOR_IO\n"
.LC289:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC290:
	.string	"GET_FLASH_INFO_IO\n"
.LC291:
	.string	"GET_BAD_BLOCK_IO\n"
.LC292:
	.string	"GET_LOCK_FLAG_IO\n"
.LC293:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC294:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC295:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC296:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC297:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC298:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC299:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC300:
	.string	"RKNAND_LOADER_STATUS\n"
.LC301:
	.string	"RKNAND_LOADER_LOCK\n"
.LC302:
	.string	"LockKey not match %d\n"
.LC303:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC304:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC305:
	.string	"return ret = %lx\n"
.LC306:
	.string	"secureBootEn check error\n"
.LC307:
	.string	"\0013vendor storage %x,%x,%x\n"
	.data
	.align	3
.LANCHOR2 = . + 0
	.type	zftl_debug, %object
	.size	zftl_debug, 4
zftl_debug:
	.word	17476
	.zero	4
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
	.size	zftl_nand_flash_para_tbl, 960
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
	.byte	2
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
	.hword	473
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
	.zero	7
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
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.zero	1
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
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
	.zero	62
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
