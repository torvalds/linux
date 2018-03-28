/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-04-08
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
	.global	zftl_nandc_get_irq_status
	.type	zftl_nandc_get_irq_status, %function
zftl_nandc_get_irq_status:
	adrp	x1, .LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L9
	ldr	w0, [x0,296]
	b	.L10
.L9:
	ldr	w0, [x0,372]
.L10:
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
	bls	.L12
	sub	w0, w0, #48
	cmp	w0, 9
	cset	w2, ls
.L12:
	mov	w0, w2
	ret
	.size	isxdigit, .-isxdigit
	.text
	.align	2
	.global	zftl_get_density
	.type	zftl_get_density, %function
zftl_get_density:
	cbnz	w0, .L15
	adrp	x0, .LANCHOR0+4
	ldr	w0, [x0,#:lo12:.LANCHOR0+4]
	b	.L16
.L15:
	cmp	w0, 4
	mov	w0, 8192
	csel	w0, w0, wzr, cc
.L16:
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
	ldr	x23, [x3,8]
	mov	w3, 65535
	add	x20, x23, x21
	ldrh	w1, [x23,x21]
	cmp	w1, w3
	bne	.L19
	ldrh	w3, [x20,2]
	cmp	w3, w1
	bne	.L19
	ldr	x1, [x0]
	cmp	x20, x1
	bne	.L18
.L19:
	ldrh	w1, [x20,2]
	mov	x22, x0
	mov	w0, 65535
	mov	x24, x2
	cmp	w1, w0
	bne	.L21
	ldr	x0, [x22]
	cmp	x20, x0
	beq	.L21
	adrp	x0, .LC0
	adrp	x1, .LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 184
	bl	printk
.L21:
	ldr	x0, [x22]
	mov	w2, 65535
	cmp	x20, x0
	bne	.L22
	ldrh	w1, [x23,x21]
	cmp	w1, w2
	bne	.L23
	str	xzr, [x22]
	b	.L24
.L23:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	umull	x1, w1, w0
	ldr	x0, [x19,8]
	add	x1, x0, x1
	str	x1, [x22]
	mov	w0, -1
	strh	w0, [x1,2]
	b	.L24
.L22:
	ldrh	w0, [x23,x21]
	cmp	w0, w2
	bne	.L25
	ldrh	w1, [x20,2]
	cmp	w1, w0
	beq	.L24
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 6
	mov	w2, -1
	umull	x1, w1, w0
	ldr	x0, [x19,8]
	b	.L26
.L25:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	ldrh	w2, [x20,2]
	umull	x0, w0, w1
	ldr	x3, [x19,8]
	add	x0, x3, x0
	strh	w2, [x0,2]
	ldrh	w0, [x20,2]
	ldrh	w2, [x23,x21]
	umull	x0, w0, w1
	ldr	x1, [x19,8]
.L26:
	strh	w2, [x1,x0]
.L24:
	mov	w0, -1
	strh	w0, [x23,x21]
	strh	w0, [x20,2]
	ldrh	w0, [x24]
	sub	w0, w0, #1
	strh	w0, [x24]
.L18:
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
	cbz	x20, .L28
	adrp	x3, .LANCHOR0+8
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3,#:lo12:.LANCHOR0+8]
.L29:
	cbnz	w1, .L30
.L32:
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
	b	.L28
.L30:
	ldrh	w3, [x20]
	cmp	w3, w4
	beq	.L32
	umull	x20, w3, w5
	sub	w1, w1, #1
	add	x20, x19, x20
	uxth	w1, w1
	b	.L29
.L28:
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
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cmp	w1, 9
	ldr	x1, [x0,16]
	ldr	w2, [x1]
	str	w2, [x0,24]
	ldr	w2, [x1,4]
	str	w2, [x0,28]
	bne	.L37
	ldr	w2, [x1,16]
	str	w2, [x0,32]
	ldr	w2, [x1,32]
	str	w2, [x0,36]
	ldr	w2, [x1,80]
	str	w2, [x0,40]
	ldr	w2, [x1,84]
	str	w2, [x0,44]
	ldr	w2, [x1,520]
	str	w2, [x0,48]
	ldr	w1, [x1,8]
	b	.L39
.L37:
	ldr	w2, [x1,8]
	str	w2, [x0,32]
	ldr	w2, [x1,12]
	str	w2, [x0,36]
	ldr	w2, [x1,304]
	str	w2, [x0,40]
	ldr	w2, [x1,308]
	str	w2, [x0,44]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,48]
.L39:
	str	w1, [x0,52]
	ret
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	adrp	x2, .LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	cmp	w2, 9
	mov	x2, 1
	bne	.L41
	ldr	w3, [x0,292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,292]
	ldr	w2, [x0,288]
	bic	w1, w2, w1
	str	w1, [x0,288]
	b	.L40
.L41:
	ldr	w3, [x0,368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
.L40:
	ret
	.size	nandc_irq_disable, .-nandc_irq_disable
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	adrp	x3, .LANCHOR0
	uxth	w1, w1
	add	x6, x3, :lo12:.LANCHOR0
	mov	x10, x3
	ldrh	w4, [x6,56]
	cmp	w4, w1
	bls	.L43
	ldrh	w3, [x2]
	mov	w5, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x11, w1, w5
	ldr	x12, [x6,8]
	mov	w2, -1
	add	x7, x12, x11
	strh	w2, [x7,2]
	strh	w2, [x12,x11]
	ldr	x2, [x0]
	cbz	x2, .L50
	ldr	x9, [x6,64]
	ubfiz	x8, x1, 2, 16
	ldr	x13, [x6,8]
	mov	x6, -6148914691236517206
	mov	w15, 65535
	ldr	w4, [x9,x8]
	ubfx	x3, x4, 11, 8
	ldrh	w4, [x9,x8]
	mov	w8, w5
	ubfx	x4, x4, 0, 11
	add	w4, w4, w3, lsl 3
	sub	x3, x2, x13
	asr	x3, x3, 1
	and	w4, w4, 4095
	madd	x3, x6, x3, x3
	uxth	w3, w3
.L48:
	ubfiz	x14, x3, 2, 16
	ldr	w5, [x9,x14]
	ubfx	x6, x5, 11, 8
	ldrh	w5, [x9,x14]
	ubfx	x5, x5, 0, 11
	add	w5, w5, w6, lsl 3
	and	w5, w5, 4095
	cmp	w5, w4
	bcs	.L46
	ldrh	w5, [x2]
	cmp	w5, w15
	bne	.L47
	strh	w3, [x7,2]
	strh	w1, [x2]
	b	.L43
.L47:
	umull	x2, w5, w8
	mov	w3, w5
	add	x2, x13, x2
	b	.L48
.L46:
	ldrh	w4, [x2,2]
	strh	w4, [x7,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L49
	strh	w1, [x2,2]
.L50:
	str	x7, [x0]
	b	.L43
.L49:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,8]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L43:
	ret
	.size	_insert_free_list, .-_insert_free_list
	.align	2
	.type	_insert_data_list, %function
_insert_data_list:
	sub	sp, sp, #16
	adrp	x3, .LANCHOR0
	add	x4, x3, :lo12:.LANCHOR0
	uxth	w1, w1
	mov	x10, x3
	str	x19, [sp]
	ldrh	w5, [x4,56]
	cmp	w5, w1
	bls	.L51
	ldrh	w3, [x2]
	mov	w9, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x11, w1, w9
	ldr	x12, [x4,8]
	mov	w2, -1
	add	x6, x12, x11
	strh	w2, [x6,2]
	strh	w2, [x12,x11]
	ldr	x2, [x0]
	cbz	x2, .L66
	ubfiz	x3, x1, 1, 16
	ldr	x15, [x4,72]
	ldr	x14, [x4,8]
	mov	x5, -6148914691236517206
	ldrh	w18, [x4,56]
	mov	w7, 0
	ldrh	w16, [x15,x3]
	mov	w19, 65535
	ldrh	w3, [x6,4]
	ldr	x17, [x4,64]
	cmp	w3, wzr
	mul	w13, w16, w3
	sub	x3, x2, x14
	asr	x3, x3, 1
	csinv	w13, w13, wzr, ne
	madd	x3, x5, x3, x3
	uxth	w3, w3
.L62:
	add	w7, w7, 1
	cmp	w1, w3
	uxth	w7, w7
	beq	.L51
	cmp	w7, w18
	bhi	.L51
	uxtw	x8, w3
	ldrh	w5, [x2,4]
	cmp	w5, wzr
	ldrh	w4, [x15,x8,lsl 1]
	mul	w4, w4, w5
	csinv	w4, w4, wzr, ne
	cmp	w4, w13
	bne	.L58
	lsl	x8, x8, 2
	ldr	w4, [x17,x8]
	ubfx	x5, x4, 11, 8
	ldrh	w4, [x17,x8]
	ubfx	x4, x4, 0, 11
	add	w4, w4, w5, lsl 3
	and	w4, w4, 4095
	cmp	w4, w16
	bcc	.L60
	b	.L59
.L58:
	bhi	.L59
.L60:
	ldrh	w4, [x2]
	cmp	w4, w19
	bne	.L61
	strh	w3, [x6,2]
	strh	w1, [x2]
	b	.L51
.L61:
	umull	x2, w4, w9
	mov	w3, w4
	add	x2, x14, x2
	b	.L62
.L59:
	ldrh	w4, [x2,2]
	strh	w4, [x6,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L63
	strh	w1, [x2,2]
.L66:
	str	x6, [x0]
	b	.L51
.L63:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,8]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L51:
	ldr	x19, [sp]
	add	sp, sp, 16
	ret
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_get_gc_head_node.isra.5, %function
_list_get_gc_head_node.isra.5:
	uxth	w1, w1
	mov	w2, 65535
	cbz	x0, .L73
	adrp	x2, .LANCHOR0+8
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x2,#:lo12:.LANCHOR0+8]
.L69:
	cbz	w1, .L70
	ldrh	w2, [x0]
	cmp	w2, w4
	beq	.L73
	umull	x2, w2, w5
	sub	w1, w1, #1
	add	x0, x3, x2
	uxth	w1, w1
	b	.L69
.L70:
	sub	x0, x0, x3
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	madd	x0, x1, x0, x0
	uxth	w2, w0
.L73:
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
	ldr	x3, [x4,80]
	ldrh	w5, [x3,16]
	cmp	w5, w19
	beq	.L74
	ldrh	w1, [x3,48]
	cmp	w1, w19
	beq	.L74
	ldrh	w1, [x3,80]
	cmp	w1, w19
	beq	.L74
	mov	w23, 6
	ldr	x25, [x4,8]
	ldr	x1, [x0]
	umull	x23, w19, w23
	add	x24, x25, x23
	cmp	x24, x1
	beq	.L74
	mov	x21, x0
	ldrh	w0, [x24,2]
	mov	w1, 65535
	mov	x22, x2
	cmp	w0, w1
	bne	.L78
	ldrh	w1, [x25,x23]
	cmp	w1, w0
	bne	.L78
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 24
	mov	w2, 251
	bl	printk
.L78:
	ldrh	w3, [x24,2]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L79
	ldrh	w0, [x25,x23]
	cmp	w0, w3
	beq	.L74
.L79:
	add	x1, x20, :lo12:.LANCHOR0
	ubfiz	x0, x19, 1, 16
	mov	x5, -6148914691236517206
	ldr	x4, [x1,72]
	ldr	x1, [x1,8]
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
	bcs	.L74
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_list_remove_node
	mov	x0, x21
	mov	w1, w19
	mov	x2, x22
	bl	_insert_data_list
.L74:
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
	bne	.L95
	ldrb	w0, [x20,1]
	add	x19, x20, 1
	mov	w21, 8
	orr	w0, w0, 32
	uxtb	w0, w0
	cmp	w0, 120
	bne	.L85
	ldrb	w0, [x20,2]
	bl	isxdigit
	cbz	w0, .L85
	add	x19, x20, 2
	mov	w21, 16
	b	.L85
.L95:
	mov	x19, x20
	mov	w21, 10
.L85:
	mov	w22, 0
.L86:
	ldrb	w20, [x19]
	mov	w0, w20
	bl	isxdigit
	cbnz	w0, .L87
.L93:
	cbz	x23, .L99
	str	x19, [x23]
	b	.L99
.L87:
	sub	w2, w20, #48
	cmp	w2, 9
	bhi	.L100
.L94:
	cmp	w2, w21
	bcs	.L93
	madd	w22, w22, w21, w2
	add	x19, x19, 1
	b	.L86
.L100:
	orr	w20, w20, 32
	sub	w2, w20, #87
	b	.L94
.L99:
	mov	w0, w22
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	rk_simple_strtoull.constprop.34, .-rk_simple_strtoull.constprop.34
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
	tbz	x0, 12, .L102
	adrp	x1, .LANCHOR1
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L102:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L103
	adrp	x0, .LANCHOR0+88
	ldr	x6, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	ldrb	w1, [x6,1]
	ldrb	w2, [x6,2]
	ldrb	w3, [x6,3]
	ldrb	w4, [x6,4]
	ldrb	w5, [x6,5]
	ldrb	w6, [x6,6]
	bl	printk
.L103:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L104
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,8]
	bl	printk
.L104:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L105
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrb	w1, [x1,9]
	bl	printk
.L105:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L106
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrh	w1, [x1,10]
	bl	printk
.L106:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L107
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,12]
	bl	printk
.L107:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L108
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,13]
	bl	printk
.L108:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L109
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrh	w1, [x1,14]
	bl	printk
.L109:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L110
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	ldrb	w1, [x1,23]
	bl	printk
.L110:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L111
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,18]
	bl	printk
.L111:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L112
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,19]
	bl	printk
.L112:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L113
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrb	w1, [x1,20]
	bl	printk
.L113:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L114
	adrp	x1, .LANCHOR0+96
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	ldrb	w1, [x1,#:lo12:.LANCHOR0+96]
	bl	printk
.L114:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L115
	adrp	x1, .LANCHOR0+97
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,#:lo12:.LANCHOR0+97]
	bl	printk
.L115:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L116
	adrp	x0, .LANCHOR0+88
	ldr	x1, [x0,#:lo12:.LANCHOR0+88]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ldrb	w1, [x1,22]
	bl	printk
.L116:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L117
	adrp	x1, .LANCHOR0+98
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	ldrb	w1, [x1,#:lo12:.LANCHOR0+98]
	bl	printk
.L117:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L118
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	and	w1, w1, 1
	bl	printk
.L118:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L119
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 1, 1
	bl	printk
.L119:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L120
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 2, 1
	bl	printk
.L120:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L121
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 3, 1
	bl	printk
.L121:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L122
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 4, 1
	bl	printk
.L122:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L123
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 5, 1
	bl	printk
.L123:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L124
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 6, 1
	bl	printk
.L124:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L125
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	ubfx	x1, x1, 7, 1
	bl	printk
.L125:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L126
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrb	w1, [x0,17]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	and	w1, w1, 1
	bl	printk
.L126:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L127
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 9, 1
	bl	printk
.L127:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L128
	adrp	x0, .LANCHOR0+88
	ldr	x0, [x0,#:lo12:.LANCHOR0+88]
	ldrh	w1, [x0,16]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	ubfx	x1, x1, 10, 1
	bl	printk
.L128:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x1, .LANCHOR0+97
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ldrb	w1, [x1,#:lo12:.LANCHOR0+97]
	bl	printk
.L129:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x2, .LANCHOR0
	adrp	x0, .LC31
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC31
	add	x2, x2, 104
	ldrb	w1, [x2,10]
	ldrb	w2, [x2,11]
	bl	printk
.L130:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x2, .LANCHOR0
	adrp	x0, .LC32
	add	x2, x2, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC32
	add	x2, x2, 104
	ldrb	w1, [x2,8]
	ldrb	w2, [x2,9]
	bl	printk
.L131:
	ldr	w0, [x19,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L101
	adrp	x1, .LANCHOR0+136
	adrp	x0, .LC33
	add	x0, x0, :lo12:.LC33
	ldrb	w1, [x1,#:lo12:.LANCHOR0+136]
	bl	printk
.L101:
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
	tbz	x0, 12, .L227
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 72
	mov	x2, x22
	bl	printk
.L227:
	adrp	x19, .LANCHOR0
	mov	w1, 6
	add	x0, x19, :lo12:.LANCHOR0
	strb	w1, [x19,#:lo12:.LANCHOR0]
	str	x22, [x0,16]
	mov	w0, 12336
	ldr	w1, [x22,352]
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L228
	mov	w0, 8
	strb	w0, [x19,#:lo12:.LANCHOR0]
.L228:
	ldr	w1, [x22,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L229
	mov	w0, 9
	strb	w0, [x19,#:lo12:.LANCHOR0]
.L229:
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	add	x20, x19, :lo12:.LANCHOR0
	cmp	w0, 9
	bne	.L230
	mov	w0, 1
	strb	w0, [x20,137]
	ldr	w0, [x29,56]
	mov	w1, 4225
	orr	w0, w0, 256
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	str	w0, [x22]
	ldr	x0, [x20,16]
	str	wzr, [x0,520]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x20,16]
	str	w1, [x0,8]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0,80]
	mov	w1, 38
	str	w1, [x0,84]
	mov	w1, 39
	str	w1, [x0,84]
	ldr	w1, [x21,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L232
	ldr	w1, [x0]
	ldr	w2, [x0,8]
	ldr	w3, [x0,80]
	ldr	w4, [x0,84]
	ldr	w5, [x0,88]
	b	.L244
.L230:
	ldr	w0, [x29,56]
	mov	w1, 4225
	strb	wzr, [x20,137]
	orr	w0, w0, 256
	str	w0, [x29,56]
	ldr	w0, [x29,56]
	str	w0, [x22]
	ldr	x0, [x20,16]
	str	wzr, [x0,336]
	str	w1, [x0,4]
	mov	w1, 8321
	ldr	x0, [x20,16]
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
	str	x0, [x20,144]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L232
	ldr	x0, [x20,16]
	ldr	w1, [x0]
	ldr	w2, [x0,344]
	ldr	w3, [x0,304]
	ldr	w4, [x0,308]
	ldr	w5, [x0,312]
.L244:
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L232:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,184]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	strh	wzr, [x19,186]
	strb	wzr, [x19,188]
	tbz	x0, 12, .L226
	adrp	x1, .LANCHOR0
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	bl	printk
.L226:
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
.L246:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L246
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
.L250:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,190]
	cmp	w0, w20
	bls	.L256
	add	x0, x19, 88
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,189]
	cmp	w0, 34
	bne	.L251
	ldrsb	w0, [x24,x20]
	b	.L255
.L251:
	cmp	w0, 35
	bne	.L253
	ldrsb	w0, [x21,x20]
	b	.L255
.L253:
	ldrsb	w0, [x23,232]
.L255:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L250
.L256:
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
	ldr	x4, [x3,192]
	ldr	x19, [x3,16]
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
.L258:
	cmp	w22, w21, uxtb
	bls	.L260
	ldrb	w0, [x23,x21]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x20,x21]
	add	x21, x21, 1
	str	w0, [x19,2048]
	b	.L258
.L260:
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
	ldrb	w1, [x19,189]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 6
	bhi	.L261
	ldr	x1, [x19,192]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L261
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,192]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L261:
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
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldr	x2, [x2,16]
	cmp	w1, 9
	lsl	w1, w0, 16
	lsl	w0, w0, 8
	orr	w0, w1, w0
	orr	w0, w0, 3
	bne	.L267
	str	w0, [x2,80]
	b	.L266
.L267:
	str	w0, [x2,304]
.L266:
	ret
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldr	x0, [x0,16]
	cmp	w1, 9
	bne	.L270
	ldr	w0, [x0,80]
	b	.L272
.L270:
	ldr	w0, [x0,304]
.L272:
	ubfx	x0, x0, 8, 8
	ret
	.size	nandc_get_ddr_para, .-nandc_get_ddr_para
	.align	2
	.global	nandc_set_if_mode
	.type	nandc_set_if_mode, %function
nandc_set_if_mode:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x1, x3, :lo12:.LANCHOR0
	tst	w0, 6
	ldr	x2, [x1,16]
	ldr	w1, [x2]
	beq	.L274
	orr	w1, w1, 24576
	and	w0, w0, 4
	and	w1, w1, -32769
	cmp	w0, wzr
	orr	w1, w1, 196608
	ldrb	w0, [x3,#:lo12:.LANCHOR0]
	orr	w4, w1, 32768
	csel	w1, w4, w1, ne
	cmp	w0, 9
	mov	w0, 8321
	bne	.L276
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L277
.L276:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L277
.L274:
	and	w1, w1, -8193
.L277:
	str	w1, [x2]
	ret
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	adrp	x1, .LANCHOR0+16
	mov	w2, 1
	lsl	w0, w2, w0
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	ldr	w1, [x3]
	bfi	w1, w0, 0, 8
	str	w1, [x3]
	ret
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	adrp	x0, .LANCHOR0+16
	ldr	x1, [x0,#:lo12:.LANCHOR0+16]
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
	ldrb	w0, [x0,98]
	mov	x19, x3
	cmp	w0, w21
	bhi	.L283
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 240
	mov	w2, 599
	bl	printk
.L283:
	add	x3, x19, :lo12:.LANCHOR0
	add	x21, x3, x21, sxtw
	ldrb	w23, [x21,200]
	lsr	w24, w20, 8
	lsr	w25, w20, 16
	mov	w26, 120
	mov	w0, w23
	ldr	x19, [x3,16]
	and	w21, w20, 255
	bl	nandc_cs
	ubfiz	x3, x23, 8, 8
	add	x19, x19, x3
.L285:
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
	bne	.L285
	cmp	w20, 255
	beq	.L285
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
	cbnz	w3, .L287
	adrp	x4, .LANCHOR0
	add	x3, x4, :lo12:.LANCHOR0
	ldrb	w0, [x3,97]
	cbz	w0, .L288
	ldrb	w0, [x3,208]
	cbz	w0, .L287
.L288:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w5, [x4,210]
	ldrb	w0, [x4,208]
	udiv	w3, w1, w5
	mul	w5, w3, w5
	sub	w3, w1, w5
	add	w1, w5, w3, lsl 1
	cbnz	w0, .L287
	add	x4, x4, 212
	ldrh	w3, [x4,w3,uxtw 1]
	add	w1, w3, w5
.L287:
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
.L298:
	mov	w0, 100
	bl	timer_delay_ns
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L299
	subs	w19, w19, #1
	bne	.L298
	mov	w0, -1
	b	.L297
.L299:
	mov	w0, 0
.L297:
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
.L302:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,190]
	cmp	w4, w2
	bls	.L307
	ldrb	w3, [x3,189]
	cmp	w3, 67
	bne	.L303
	ldrsb	w3, [x0,x2]
	b	.L306
.L303:
	ldrsb	w3, [x1,x2]
.L306:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L302
.L307:
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
	cbz	w2, .L311
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
	b	.L312
.L311:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 344
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L312:
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
	adrp	x1, .LANCHOR0
	uxtb	x0, w0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x1,97]
	cbz	w2, .L313
	cmp	w2, 1
	ldr	x3, [x1,16]
	bne	.L316
	adrp	x1, .LANCHOR2+405
	ldrb	w2, [x1,#:lo12:.LANCHOR2+405]
	cbz	w2, .L313
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x3, x1, lsl 8
	str	w2, [x0,8]
	b	.L313
.L316:
	cmp	w2, 2
	bne	.L318
	add	x1, x1, x0
	ldrb	w2, [x1,728]
	cbz	w2, .L313
	strb	wzr, [x1,728]
	adrp	x1, .LANCHOR2+405
	ldrb	w1, [x1,#:lo12:.LANCHOR2+405]
	cbz	w1, .L313
	add	x0, x0, 8
	add	x0, x3, x0, lsl 8
	str	w1, [x0,8]
	b	.L313
.L318:
	cmp	w2, 3
	bne	.L313
	add	x1, x1, x0, sxtw
	ldrb	w2, [x1,728]
	cbz	w2, .L313
	strb	wzr, [x1,728]
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
.L313:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	adrp	x1, .LANCHOR0
	uxtb	x0, w0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x1,97]
	cbz	w2, .L331
	cmp	w2, 1
	ldr	x4, [x1,16]
	bne	.L334
	adrp	x1, .LANCHOR2+406
	ldrb	w2, [x1,#:lo12:.LANCHOR2+406]
	cbz	w2, .L331
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x4, x1, lsl 8
	str	w2, [x0,8]
	b	.L331
.L334:
	cmp	w2, 2
	bne	.L336
	add	x1, x1, x0
	ldrb	w2, [x1,728]
	cbnz	w2, .L331
	adrp	x2, .LANCHOR2
	mov	w5, 4
	add	x2, x2, :lo12:.LANCHOR2
	ldrb	w3, [x2,388]
	cmp	w3, 2
	csel	w3, w3, w5, eq
	strb	w3, [x1,728]
	ldrb	w1, [x2,406]
	cbz	w1, .L331
	add	x0, x0, 8
	add	x0, x4, x0, lsl 8
	str	w1, [x0,8]
	b	.L331
.L336:
	cmp	w2, 3
	bne	.L331
	add	x1, x1, x0, sxtw
	ldrb	w3, [x1,728]
	cbnz	w3, .L331
	adrp	x2, .LANCHOR2+388
	mov	w5, 4
	ldrb	w2, [x2,#:lo12:.LANCHOR2+388]
	cmp	w2, 2
	csel	w2, w2, w5, eq
	strb	w2, [x1,728]
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
.L331:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	stp	x29, x30, [sp, -80]!
	adrp	x2, .LANCHOR0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	add	x0, x2, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x25, [sp,64]
	ubfx	x19, x1, 21, 3
	and	w20, w1, 2097151
	ubfx	x22, x1, 24, 2
	ldrb	w0, [x0,98]
	mov	x21, x2
	cmp	w0, w19
	bhi	.L350
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 688
	mov	w2, 736
	bl	printk
.L350:
	add	x25, x21, :lo12:.LANCHOR0
	add	x19, x25, x19, sxtw
	ldrb	w19, [x19,200]
	mov	w0, w19
	ldr	x24, [x25,16]
	bl	nandc_cs
	cbnz	w22, .L351
	ldrb	w0, [x25,97]
	cbz	w0, .L352
	ldrb	w0, [x25,208]
	cbz	w0, .L353
.L352:
	add	x2, x21, :lo12:.LANCHOR0
	ldrh	w1, [x2,210]
	udiv	w0, w20, w1
	mul	w0, w0, w1
	ldrb	w1, [x2,208]
	sub	w20, w20, w0
	cbz	w1, .L354
	add	w20, w0, w20, lsl 1
	b	.L353
.L354:
	add	x2, x2, 212
	ldrh	w20, [x2,w20,uxtw 1]
	add	w20, w20, w0
	b	.L355
.L353:
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	b	.L355
.L351:
	ldr	x0, [x25,88]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L356
	sxtw	x0, w19
	add	x0, x0, 8
	add	x0, x24, x0, lsl 8
	str	w22, [x0,8]
	b	.L355
.L356:
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L355:
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
	str	w23, [x1,2056]
	bl	nandc_de_cs
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_page_read, .-flash_start_page_read
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
	add	x25, x21, 732
	adrp	x23, .LANCHOR2
.L370:
	lsl	x0, x20, 3
	ldr	x19, [x21,16]
	ldrb	w1, [x0,x25]
	cmp	w1, 44
	cset	w26, eq
	cmp	w1, 69
	cset	w2, eq
	cmp	w1, 152
	cset	w0, eq
	adrp	x1, .LANCHOR0
	orr	w0, w2, w0
	cbnz	w0, .L371
	cbz	w26, .L361
.L371:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w24, 1
	ldrb	w1, [x1,764]
	bne	.L363
	tbz	x1, 0, .L361
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L364
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	bl	printk
.L364:
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	mov	w0, 1
	cbz	w26, .L365
	str	w0, [x1,2052]
	mov	w0, 5
	b	.L388
.L365:
	mov	w2, 128
	str	w2, [x1,2052]
.L388:
	str	w0, [x1,2048]
	b	.L369
.L363:
	tbz	x1, 2, .L361
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L367
	mov	x0, x22
	bl	printk
.L367:
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	cbz	w26, .L368
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
	b	.L388
.L368:
	mov	w0, 128
	str	w0, [x1,2052]
	str	w26, [x1,2048]
.L369:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L361:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L370
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
	adrp	x1, .LANCHOR0+16
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 8
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR0+16]
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
	adrp	x0, .LANCHOR0+16
	stp	x19, x20, [sp,16]
	mov	x19, x1
	ldr	x20, [x0,#:lo12:.LANCHOR0+16]
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
	bhi	.L390
	adrp	x0, .LC39
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC39
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L390:
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
	adrp	x4, .LANCHOR0+16
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x3, .LANCHOR2+385
	ldr	x19, [x4,#:lo12:.LANCHOR0+16]
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
	adrp	x2, .LANCHOR0+16
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x2,#:lo12:.LANCHOR0+16]
	add	x2, x20, :lo12:.LANCHOR2
	str	x21, [sp,32]
	ldrb	w3, [x2,405]
	sxtw	x2, w0
	add	x2, x2, 8
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L407
	mov	w3, 162
.L407:
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
	beq	.L396
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L396
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	bl	printk
.L396:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,406]
	cbz	w0, .L397
	str	w0, [x19,2056]
.L397:
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
	adrp	x0, .LANCHOR0+16
	sub	sp, sp, #16
	ldr	x0, [x0,#:lo12:.LANCHOR0+16]
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
	adrp	x2, .LANCHOR0
	mov	w3, 1
	add	x1, x2, :lo12:.LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	strb	w0, [x1,188]
	cmp	w2, 9
	ldr	x2, [x1,16]
	bne	.L410
	str	w3, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L411
	cmp	w0, 60
	mov	w1, 3
	beq	.L411
	cmp	w0, 40
	mov	w1, 2
	csel	w1, w1, w3, eq
.L411:
	lsl	w1, w1, 25
	orr	w1, w1, 1
	str	w1, [x2,32]
	b	.L409
.L410:
	str	w3, [x2,8]
	cmp	w0, 16
	mov	w1, 4096
	bne	.L413
.L416:
	and	w1, w1, -17
	b	.L414
.L413:
	cmp	w0, 24
	bne	.L415
	orr	w1, w1, 16
	b	.L414
.L415:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L416
.L414:
	orr	w1, w1, 1
	str	w1, [x2,12]
.L409:
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
	ldrb	w1, [x20,#:lo12:.LANCHOR0]
	ldr	w2, [x0,24]
	cmp	w1, 9
	ldr	x1, [x0,16]
	str	w2, [x1]
	ldr	x1, [x0,16]
	ldr	w2, [x0,28]
	str	w2, [x1,4]
	ldr	x1, [x0,16]
	ldr	w2, [x0,32]
	bne	.L424
	str	w2, [x1,16]
	ldr	w2, [x0,36]
	str	w2, [x1,32]
	ldr	w2, [x0,40]
	str	w2, [x1,80]
	ldr	w2, [x0,44]
	str	w2, [x1,84]
	ldr	w2, [x0,48]
	str	w2, [x1,520]
	ldr	w0, [x0,52]
	str	w0, [x1,8]
	b	.L425
.L424:
	str	w2, [x1,8]
	ldr	w2, [x0,36]
	str	w2, [x1,12]
	ldr	w2, [x0,40]
	str	w2, [x1,304]
	ldr	w2, [x0,44]
	str	w2, [x1,308]
	ldr	w2, [x0,48]
	str	w2, [x1,336]
	ldr	w0, [x0,52]
	str	w0, [x1,344]
.L425:
	add	x21, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x23, x21, 732
	mov	w24, 2
.L427:
	lsl	x0, x19, 3
	ldrb	w0, [x0,x23]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L426
	uxtb	w22, w19
	mov	w0, w22
	bl	flash_reset
	add	x0, x21, 728
	strb	w24, [x19,x0]
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L426:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L427
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,136]
	cbz	w0, .L428
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,40]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L428:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,16]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	ldr	x0, [x20,16]
	cmp	w1, 9
	bne	.L435
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L436
.L435:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L436:
	ldr	w1, [x0]
	tbnz	x1, 9, .L437
	bl	wait_for_nand_flash_ready
	b	.L434
.L437:
	mov	w1, 1
	bl	nandc_irq_disable
.L434:
	ldp	x19, x20, [sp,16]
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
	ldrb	w0, [x0,98]
	mov	w24, w3
	cmp	w0, w19
	bhi	.L440
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 712
	mov	w2, 510
	bl	printk
.L440:
	add	x0, x20, :lo12:.LANCHOR0
	add	x19, x0, x19, sxtw
	ldrb	w23, [x19,200]
	ldr	x19, [x0,16]
	adrp	x0, .LANCHOR2
	mov	x21, x23
	add	x23, x23, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x23, x19, x23, lsl 8
	tbz	x0, 4, .L441
	adrp	x0, .LC41
	mov	w1, w21
	add	x0, x0, :lo12:.LC41
	mov	w2, w22
	mov	w3, w24
	bl	printk
.L441:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L442
	bl	zftl_flash_enter_slc_mode
	b	.L443
.L442:
	bl	zftl_flash_exit_slc_mode
.L443:
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
	ldrb	w0, [x20,765]
	mov	w20, 0
	cbnz	w0, .L444
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	and	w20, w0, w20
.L444:
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
	cbz	w19, .L445
	adrp	x0, .LC42
	mov	w1, w22
	add	x0, x0, :lo12:.LC42
	bl	printk
.L445:
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
	ldrb	w0, [x0,98]
	and	w23, w2, 2097151
	cmp	w0, w20
	bhi	.L454
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 744
	mov	w2, 555
	bl	printk
.L454:
	add	x0, x19, :lo12:.LANCHOR0
	add	x20, x0, x20, sxtw
	ldrb	w21, [x20,200]
	ldr	x25, [x0,16]
	adrp	x0, .LANCHOR2
	mov	x20, x21
	add	x21, x21, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	add	x21, x25, x21, lsl 8
	tbz	x0, 4, .L455
	adrp	x0, .LC43
	mov	w1, w20
	add	x0, x0, :lo12:.LC43
	mov	w2, w22
	bl	printk
.L455:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w24, .L456
	bl	zftl_flash_enter_slc_mode
	b	.L457
.L456:
	bl	zftl_flash_exit_slc_mode
.L457:
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
	beq	.L458
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w20
	ldrh	w1, [x19,210]
	udiv	w1, w22, w1
	bl	printk
.L458:
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
.L467:
	ldrb	w0, [x23,98]
	cmp	w0, w19
	bls	.L471
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,200]
	mov	w22, 0
.L468:
	cmp	w20, w22, uxth
	bls	.L472
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,210]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L468
.L472:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L467
.L471:
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
	ldr	x0, [x21,16]
	add	w20, w23, 1
	str	x3, [x29,88]
	bl	rk_nandc_xfer_irq_flag_init
	ldrb	w0, [x22,#:lo12:.LANCHOR0]
	ldr	x4, [x21,16]
	cmp	w0, 9
	ldr	x3, [x29,88]
	ldr	x2, [x29,80]
	bne	.L475
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
	ldrb	w0, [x21,137]
	orr	w19, w19, 536870912
	orr	w19, w19, 1024
	and	w19, w19, -17
	bfi	w19, w20, 22, 6
	orr	w19, w19, 128
	cbz	w0, .L477
	ldrb	w1, [x21,184]
	orr	w0, w19, 512
	cmp	w1, wzr
	csel	w19, w0, w19, ne
	b	.L477
.L475:
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
	cbz	w24, .L489
	ldrb	w0, [x21,188]
	ubfx	x23, x23, 1, 7
	mov	w1, 64
	mov	w4, 128
	cmp	w0, 25
	add	x23, x3, x23, lsl 2
	csel	w4, w1, w4, cc
	mov	w1, 0
	b	.L482
.L477:
	add	x22, x22, :lo12:.LANCHOR0
	ubfx	x21, x19, 22, 6
	mov	x0, x2
	lsl	w1, w21, 10
	str	x2, [x22,152]
	mov	w2, w24
	str	x3, [x22,160]
	bl	rknand_dma_map_single
	str	w0, [x22,168]
	ldr	x0, [x22,160]
	lsl	w1, w21, 2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,172]
	ldr	x3, [x22,16]
	mov	w1, 1
	str	w1, [x22,176]
	cmp	w24, wzr
	ldr	w1, [x22,168]
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
	ldrh	w0, [x22,186]
	orr	w1, w1, 1
	bfi	w1, w0, 16, 11
	str	w1, [x3,48]
	str	w19, [x3,16]
	orr	w19, w19, 4
	str	w19, [x3,16]
	b	.L474
.L482:
	cmp	x3, x23
	add	w0, w1, w4
	beq	.L484
	ldrh	w5, [x3,2]
	and	x1, x1, 4294967292
	ldr	x6, [x21,144]
	ldrh	w7, [x3],4
	orr	w5, w7, w5, lsl 16
	str	w5, [x6,x1]
	mov	w1, w0
	b	.L482
.L489:
	ldr	x1, [x21,144]
	str	w0, [x1]
.L484:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	lsl	w1, w20, 10
	ldr	x0, [x22,144]
	str	x2, [x22,152]
	str	x0, [x22,160]
	mov	x0, x2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,168]
	ldr	x0, [x22,160]
	lsl	w1, w20, 7
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,172]
	ldr	x3, [x22,16]
	mov	w0, 1
	str	w0, [x22,176]
	cmp	w24, wzr
	ldr	w0, [x22,168]
	mov	w1, 8660
	str	w0, [x3,20]
	ldr	w0, [x22,172]
	str	w0, [x3,24]
	cset	w0, eq
	bfi	w1, w0, 1, 1
	orr	w1, w1, 1
	str	w1, [x3,16]
	str	w25, [x3,12]
	str	w19, [x3,8]
	orr	w19, w19, 4
	str	w19, [x3,8]
.L474:
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
	adrp	x2, .LANCHOR0
	add	x1, x1, 768
	ldrh	w0, [x1,w0,uxtw 1]
	add	x1, x2, :lo12:.LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	orr	w3, w0, -1073741824
	ldrb	w4, [x1,184]
	ldr	x1, [x1,16]
	cmp	w4, wzr
	csel	w0, w3, w0, ne
	cmp	w2, 9
	bne	.L492
	str	w0, [x1,520]
	b	.L490
.L492:
	str	w0, [x1,336]
.L490:
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
	ldrb	w0, [x20,136]
	cbz	w0, .L497
	ldrb	w0, [x20,764]
	tbz	x0, 0, .L497
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,136]
.L497:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,184]
	cbz	w0, .L498
	mov	w0, 0
	strb	wzr, [x19,184]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,184]
.L498:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+184
	strb	w0, [x1,#:lo12:.LANCHOR0+184]
	ret
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	adrp	x1, .LANCHOR0+16
	uxtb	x0, w0
	add	x0, x0, 8
	ldr	x1, [x1,#:lo12:.LANCHOR0+16]
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
	add	x19, x19, 768
	mov	w21, 0
	mov	x22, x0
.L511:
	add	x20, x22, :lo12:.LANCHOR0
	uxtb	w0, w21
	add	w1, w0, 1
	strb	w0, [x19,1]
	strb	w1, [x19]
	add	w21, w21, 1
	ldrb	w0, [x20,2816]
	add	x19, x19, 64
	strb	wzr, [x19,-62]
	str	xzr, [x19,-48]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x19,-56]
	ldrb	w0, [x20,2816]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,-40]
	cmp	w21, 32
	bne	.L511
	strb	w21, [x20,2818]
	mov	w0, -1
	strb	wzr, [x20,2817]
	strb	w0, [x20,2752]
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
	add	x1, x1, 768
.L514:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L514
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0,2817]
	strb	w1, [x0,2752]
	strb	w2, [x0,2818]
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
	bne	.L522
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L518
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1024
	mov	w2, 74
	bl	printk
.L518:
	ldrb	w0, [x19,1]
	strb	w0, [x20]
	b	.L516
.L522:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 768
.L523:
	mov	w20, w1
	sbfiz	x1, x1, 6, 32
	ldrb	w1, [x0,x1]
	cmp	w1, 255
	bne	.L523
	ldrb	w0, [x19,1]
	cmp	w0, 255
	bne	.L520
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1024
	mov	w2, 81
	bl	printk
.L520:
	adrp	x0, .LANCHOR0
	sbfiz	x20, x20, 6, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x19,1]
	add	x0, x0, 768
	strb	w1, [x0,x20]
.L516:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_add_tail, .-buf_add_tail
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
	cbz	w1, .L525
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2817
	bl	buf_add_tail
	b	.L526
.L525:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2817]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2817]
.L526:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2818]
	add	w0, w0, 1
	strb	w0, [x19,2818]
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
	ldrb	w0, [x20,2818]
	cbz	w0, .L528
.L531:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 768
	ldrb	w2, [x1,2817]
	add	x0, x0, x2, lsl 6
	cbz	w21, .L529
	b	.L530
.L528:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1040
	mov	w2, 121
	bl	printk
	ldrb	w1, [x20,2818]
	cbnz	w1, .L531
.L533:
	mov	x0, 0
	b	.L532
.L529:
	ldrb	w1, [x1,2818]
	cmp	w1, 1
	beq	.L533
.L530:
	add	x1, x19, :lo12:.LANCHOR0
	lsl	x2, x2, 6
	add	x4, x1, 768
	add	x3, x4, x2
	ldrb	w5, [x4,x2]
	strb	w5, [x1,2817]
	ldrb	w5, [x1,2818]
	strh	wzr, [x3,50]
	sub	w5, w5, #1
	strb	w5, [x1,2818]
	mov	w1, 1
	strb	w1, [x3,2]
	mov	w1, -1
	str	xzr, [x3,16]
	strb	w1, [x4,x2]
	mov	w1, -1
	str	w1, [x3,36]
.L532:
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
	bne	.L540
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L544
.L540:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 768
.L542:
	cmp	w2, 255
	beq	.L545
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L542
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L544:
	mov	w0, 1
	b	.L539
.L545:
	mov	w0, 0
.L539:
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
	ldrb	w0, [x0,2818]
	cbnz	w0, .L547
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1056
	mov	w2, 170
	bl	printk
.L547:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2818]
	cbz	w0, .L546
	add	x0, x19, 2817
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L546
	ldrb	w0, [x19,2818]
	sub	w0, w0, #1
	strb	w0, [x19,2818]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L546:
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
	adrp	x0, .LANCHOR0+192
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+192]
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
	adrp	x0, .LANCHOR0+192
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+192]
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
	bne	.L554
	ldrb	w1, [x0,1]
	add	x2, x0, 2
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	csel	x0, x0, x2, ne
.L554:
	ldrb	w1, [x0]
	and	w1, w1, -33
	uxtb	w1, w1
	cmp	w1, 88
	mov	w1, 0
	csinc	x0, x0, x0, ne
.L556:
	ldrb	w2, [x0]
	cbz	w2, .L563
	sub	w3, w2, #48
	uxtb	w3, w3
	cmp	w3, 9
	bhi	.L557
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
	b	.L558
.L557:
	sub	w3, w2, #97
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L559
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L558
.L559:
	sub	w3, w2, #65
	uxtb	w3, w3
	cmp	w3, 5
	bhi	.L563
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
.L558:
	add	x0, x0, 1
	b	.L556
.L563:
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
	add	x3, x3, 1072
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
	str	x21, [sp,32]
	mov	x20, x0
	bl	sprintf
	adrp	x21, .LANCHOR0
	adrp	x7, .LANCHOR2
	adrp	x1, .LC48
	add	x7, x7, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x7, x7, 376
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC48
	mov	x0, x19
	ldrb	w5, [x7,4]
	ldrb	w6, [x7,5]
	ldrb	w3, [x7,2]
	ldrb	w4, [x7,3]
	ldrb	w2, [x7,1]
	ldrb	w7, [x7,6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,4]
	adrp	x1, .LC49
	mov	x0, x19
	add	x1, x1, :lo12:.LC49
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,2820]
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
	ldr	w2, [x21,2824]
	adrp	x1, .LC52
	mov	x0, x19
	add	x1, x1, :lo12:.LC52
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,4]
	adrp	x1, .LC53
	mov	x0, x19
	add	x1, x1, :lo12:.LC53
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC54
	mov	x0, x19
	add	x1, x1, :lo12:.LC54
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC55
	mov	x0, x19
	add	x1, x1, :lo12:.LC55
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2828]
	adrp	x1, .LC56
	mov	x0, x19
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2830]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2832]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2834]
	adrp	x1, .LC59
	mov	x0, x19
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2836]
	adrp	x1, .LC60
	mov	x0, x19
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2838]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w4, [x21,2841]
	adrp	x1, .LC62
	ldrb	w2, [x21,2818]
	mov	x0, x19
	ldrb	w3, [x21,2840]
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x3, [x21,2848]
	adrp	x1, .LC63
	mov	x0, x19
	add	x1, x1, :lo12:.LC63
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2848]
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2848]
	adrp	x1, .LC65
	add	x1, x1, :lo12:.LC65
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC66
	mov	x0, x19
	add	x1, x1, :lo12:.LC66
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,80]
	mov	w2, 10
	adrp	x1, .LC68
	add	x1, x1, :lo12:.LC68
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w2, [x21,97]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC73
	mov	x0, x19
	add	x1, x1, :lo12:.LC73
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2848]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC76
	mov	x0, x19
	add	x1, x1, :lo12:.LC76
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC77
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC78
	mov	x0, x19
	add	x1, x1, :lo12:.LC78
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2848]
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
	ldr	x6, [x21,2848]
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
	ldrh	w2, [x21,2856]
	adrp	x1, .LC81
	mov	x0, x19
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2858]
	adrp	x1, .LC82
	ldrh	w3, [x21,2860]
	mov	x0, x19
	ldrh	w4, [x21,2862]
	add	x1, x1, :lo12:.LC82
	ldrh	w5, [x21,2864]
	bl	sprintf
	add	x0, x19, x0, sxtw
	ldr	x21, [sp,32]
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
	add	x0, x19, 2866
	bl	buf_add_tail
	ldrb	w0, [x19,2840]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,2840]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_gc_write_buf, .-ftl_gc_write_buf
	.align	2
	.global	ftl_read_buf
	.type	ftl_read_buf, %function
ftl_read_buf:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	w20, w0
	add	x0, x19, :lo12:.LANCHOR0
	str	x21, [sp,32]
	mov	x21, x1
	ldr	w0, [x0,2824]
	cmp	w20, w0
	bcc	.L569
	adrp	x2, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1288
	mov	w2, 887
	bl	printk
.L569:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w2, 0
	add	x3, x3, 768
.L573:
	ldr	w0, [x3,36]
	cmp	w0, w20
	bne	.L570
	ldrb	w0, [x3,2]
	tbz	x0, 3, .L570
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x2, x2, 6, 32
	add	x19, x19, 768
	orr	w1, w0, 4
	add	x19, x19, x2
	strb	w1, [x19,2]
	tbnz	x0, 0, .L571
	mov	x0, x19
	bl	buf_remove_free
.L571:
	mov	x0, x19
	b	.L572
.L570:
	add	w2, w2, 1
	add	x3, x3, 64
	cmp	w2, 32
	bne	.L573
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, 0
	ldr	w1, [x19,2868]
	cmn	w1, #1
	bne	.L572
	str	w20, [x19,2868]
	str	x21, [x19,2872]
.L572:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_read_buf, .-ftl_read_buf
	.align	2
	.global	ftl_read_buf_free
	.type	ftl_read_buf_free, %function
ftl_read_buf_free:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	adrp	x0, .LANCHOR0
	mov	x1, x19
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 2880
	bl	buf_remove_buf
	ldrb	w0, [x19,2]
	tbz	x0, 1, .L580
	and	w0, w0, -5
	strb	w0, [x19,2]
	b	.L579
.L580:
	mov	x0, x19
	bl	buf_free
.L579:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_read_buf_free, .-ftl_read_buf_free
	.align	2
	.global	gc_hook
	.type	gc_hook, %function
gc_hook:
	ret
	.size	gc_hook, .-gc_hook
	.align	2
	.global	gc_add_sblk
	.type	gc_add_sblk, %function
gc_add_sblk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxth	w23, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	uxth	w21, w1
	uxth	w22, w2
	ldrh	w0, [x0,56]
	cmp	w0, w23
	bhi	.L584
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1304
	mov	w2, 252
	bl	printk
.L584:
	add	x6, x20, :lo12:.LANCHOR0
	uxtw	x19, w23
	lsl	x25, x19, 1
	adrp	x24, .LANCHOR2
	ldr	x0, [x6,72]
	ldrh	w26, [x0,x25]
	ldr	w0, [x24,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L585
	ldr	x0, [x6,64]
	mov	w1, w23
	ldrh	w5, [x6,2944]
	mov	w2, w21
	add	x0, x0, x19, lsl 2
	ldrh	w6, [x6,2862]
	mov	w4, w26
	ldrb	w3, [x0,2]
	adrp	x0, .LC83
	add	x0, x0, :lo12:.LC83
	ubfx	x3, x3, 5, 3
	bl	printk
.L585:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,64]
	add	x0, x0, x19, lsl 2
	ldrb	w19, [x0,2]
	ands	w19, w19, 224
	bne	.L586
	cbz	w26, .L587
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 257
	add	x1, x1, 1304
	add	x0, x0, :lo12:.LC0
	bl	printk
	b	.L587
.L586:
	ldrh	w2, [x1,2888]
	add	x3, x1, 2888
	mov	w19, 0
	cmp	w2, w23
	beq	.L587
	ldr	x1, [x1,80]
	ldrh	w2, [x1,48]
	cmp	w2, w23
	beq	.L587
	ldrh	w2, [x1,16]
	cmp	w2, w23
	beq	.L587
	ldrh	w2, [x1,80]
	cmp	w2, w23
	beq	.L587
	ldrh	w5, [x3,56]
	mov	x2, 0
.L588:
	cmp	w5, w2
	bls	.L611
	add	x2, x2, 1
	add	x4, x3, x2, lsl 1
	ldrh	w4, [x4,56]
	cmp	w4, w23
	bne	.L588
	mov	w19, 0
	b	.L587
.L611:
	add	x19, x1, 392
	cbnz	w21, .L591
	sxtw	x3, w22
	add	x3, x1, x3, lsl 7
	add	x19, x3, 136
.L591:
	ldr	w1, [x24,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L592
	add	x6, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,2]
	adrp	x0, .LC83
	mov	w1, w23
	add	x0, x0, :lo12:.LC83
	mov	w2, w21
	ldr	x4, [x6,72]
	ubfx	x3, x3, 5, 3
	ldrh	w6, [x6,2862]
	ldrh	w4, [x4,x25]
	bl	printk
.L592:
	mov	x3, x19
	add	x0, x19, 128
	mov	w1, 65535
.L595:
	ldrh	w2, [x3]
	cmp	w2, w1
	bne	.L593
	strh	w23, [x3]
	add	x20, x20, :lo12:.LANCHOR0
	cbz	w21, .L594
	ldr	x1, [x20,80]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L610
.L594:
	ldr	x0, [x20,80]
	add	x22, x0, x22, uxth 1
	ldrh	w0, [x22,120]
	add	w0, w0, 1
	strh	w0, [x22,120]
.L610:
	mov	w19, 1
	b	.L587
.L593:
	add	x3, x3, 2
	cmp	x3, x0
	bne	.L595
	b	.L610
.L587:
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
	adrp	x1, .LANCHOR0+3216
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3216]
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
	add	x23, x21, 768
	add	x22, x22, 1320
.L614:
	ldrb	w0, [x21,3224]
	adrp	x19, .LANCHOR0
	cmp	w0, 255
	beq	.L636
	sxtw	x25, w0
	lsl	x0, x25, 6
	add	x2, x23, x0
	ldrb	w0, [x23,x0]
	ldr	w1, [x2,52]
	strb	w0, [x21,3224]
	ldrh	w24, [x2,48]
	cbz	w1, .L615
	ldr	w2, [x2,40]
	adrp	x0, .LC84
	mov	w3, 1
	add	x0, x0, :lo12:.LC84
	str	w2, [x21,3208]
	strh	w3, [x21,3206]
	bl	printk
	adrp	x0, .LC0
	mov	x1, x22
	add	x0, x0, :lo12:.LC0
	mov	w2, 519
	bl	printk
.L615:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w20, 1
	ldrb	w1, [x0,3225]
	cmp	w1, 3
	bne	.L616
	ldr	x0, [x0,80]
	ldrb	w20, [x0,89]
	add	w20, w20, w20, lsl 1
	and	w20, w20, 1023
.L616:
	adrp	x0, .LANCHOR2
	uxth	x26, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L617
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w24
	add	x2, x0, x25, lsl 6
	mov	w3, w24
	ldr	x4, [x0,3232]
	adrp	x0, .LC85
	ldrb	w2, [x2,769]
	add	x0, x0, :lo12:.LC85
	ldrb	w4, [x4,x26]
	bl	printk
.L617:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,3232]
	add	x0, x0, x25, lsl 6
	ldrb	w1, [x1,x26]
	ldrb	w0, [x0,769]
	cmp	w1, w0
	beq	.L618
	adrp	x0, .LC0
	mov	x1, x22
	add	x0, x0, :lo12:.LC0
	mov	w2, 526
	bl	printk
.L618:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 768
	add	x0, x1, x25, lsl 6
	ldrb	w0, [x0,61]
	cmp	w0, 3
	beq	.L619
	ldrb	w0, [x2,3225]
	cmp	w0, 3
	bne	.L619
	ldrb	w0, [x2,3240]
	cbnz	w0, .L619
	ldr	x3, [x2,3232]
	b	.L620
.L619:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w26, -1
	add	x27, x19, 768
	strh	w24, [x19,3204]
.L621:
	cmp	w20, w25, uxth
	bls	.L614
	add	w1, w25, w24
	ldr	x2, [x19,3232]
	add	w25, w25, 1
	ldrb	w0, [x2,x1]
	strb	w26, [x2,x1]
	sbfiz	x0, x0, 6, 32
	add	x0, x27, x0
	strb	wzr, [x0,61]
	bl	buf_free
	ldrb	w0, [x19,2895]
	sub	w0, w0, #1
	strb	w0, [x19,2895]
	b	.L621
.L620:
	cmp	w20, w0, uxth
	bls	.L614
	add	w2, w0, w24
	add	w0, w0, 1
	ldrb	w2, [x3,x2]
	add	x2, x1, x2, lsl 6
	strb	wzr, [x2,61]
	b	.L620
.L636:
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
	ldr	x1, [x0,80]
	ldrb	w0, [x0,3241]
	ldrh	w4, [x1,124]
	cbz	w4, .L638
	add	x1, x1, 392
	mov	w2, 1
	b	.L639
.L638:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L640
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L639:
	add	x4, x1, 128
	mov	w5, 65535
.L643:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L641
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L642
	ldr	x2, [x3,80]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L640
.L642:
	ldrb	w1, [x3,3241]
	ldr	x2, [x3,80]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L640
.L641:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L643
.L640:
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
	ldrb	w3, [x2,2895]
	cbz	w3, .L648
	ldrb	w3, [x2,2818]
	cmp	w3, 1
	bhi	.L648
	ldrh	w4, [x2,3242]
	mov	x20, x2
	ldrb	w0, [x2,3244]
	add	x2, x2, 768
	ldrh	w1, [x2,2436]
	mul	w0, w4, w0
	add	w3, w1, 24
	ldr	x4, [x2,2464]
	cmp	w0, w3
	csel	w3, w0, w3, ls
.L649:
	cmp	w1, w3
	bcs	.L662
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L650
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L650
	str	x1, [x29,56]
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L651
	ldr	x2, [x20,3232]
	adrp	x0, .LC86
	add	x0, x0, :lo12:.LC86
	ldrb	w2, [x2,x21]
	bl	printk
.L651:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3232]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2895]
	sub	w0, w0, #1
	strb	w0, [x19,2895]
	mov	w0, 1
	b	.L648
.L650:
	add	w1, w1, 1
	b	.L649
.L662:
	mov	w0, 0
.L648:
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
	adrp	x0, .LC87
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC87
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,2888]
	ldrh	w2, [x6,2890]
	ldrh	w3, [x6,3202]
	ldrb	w4, [x6,2818]
	ldrb	w5, [x6,2895]
	ldrh	w6, [x6,2856]
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
	adrp	x2, .LANCHOR0
	cmp	w1, 5
	add	x29, sp, 0
	uxth	w3, w0
	add	x2, x2, :lo12:.LANCHOR0
	bne	.L665
	ldr	x0, [x2,3248]
	b	.L668
.L665:
	cmp	w1, 2
	bne	.L667
	ldr	x0, [x2,3256]
	b	.L668
.L667:
	ldr	x0, [x2,3264]
.L668:
	mov	w1, w3
	bl	_list_get_gc_head_node.isra.5
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
	ldr	x1, [x0,80]
	add	x1, x1, x21, sxtw 1
	ldrh	w19, [x1,120]
	cbnz	w19, .L714
	cbnz	w21, .L672
	adrp	x26, .LC88
	strh	w21, [x0,3272]
	mov	w19, w21
	mov	w25, w21
	uxth	w23, w22
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x26, x26, :lo12:.LC88
.L673:
	cmp	w25, w23
	bcs	.L678
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, 3
	str	x3, [x29,96]
	ldrh	w2, [x3,3272]
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
	strh	w2, [x3,3272]
	beq	.L674
	ldr	w0, [x28,#:lo12:.LANCHOR2]
	uxtw	x5, w1
	tbz	x0, 8, .L675
	ldr	x3, [x3,72]
	mov	x0, x26
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w3, [x3,x5,lsl 1]
	bl	printk
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
.L675:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,72]
	ldrh	w2, [x0,x5,lsl 1]
	ldrh	w0, [x1,2862]
	cmp	w2, w0
	bcs	.L676
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L677
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L677
	b	.L678
.L676:
	strh	wzr, [x1,3272]
	b	.L678
.L674:
	strh	wzr, [x3,3272]
	b	.L678
.L677:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L673
.L678:
	tbz	x24, 1, .L680
	mov	w26, 0
	mov	w27, 65535
.L681:
	cmp	w26, w23
	beq	.L685
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w28, [x25,3274]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x25,3274]
	cmp	w1, w27
	beq	.L682
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x25,72]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x25,2864]
	cmp	w2, w1
	bcs	.L683
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L684
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L684
	b	.L687
.L683:
	strh	wzr, [x25,3274]
	b	.L685
.L682:
	strh	wzr, [x25,3272]
	b	.L685
.L684:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L681
.L685:
	cmp	w19, w23
	bcs	.L687
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1,3276]
	ldrb	w4, [x1,3244]
	lsr	w3, w0, 3
	ldrh	w2, [x1,2864]
	mul	w0, w0, w4
	sub	w0, w0, w3
	cmp	w2, w0
	bge	.L680
	add	w2, w3, w2
	strh	w2, [x1,2864]
	b	.L680
.L687:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2864]
	ldrh	w0, [x1,3276]
	cmp	w2, w0
	bls	.L680
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,2864]
.L680:
	tbz	x24, 0, .L688
	cmp	w19, w23
	bcs	.L688
	mov	w25, 64
	mov	w27, 65535
.L692:
	add	x26, x20, :lo12:.LANCHOR0
	ldrh	w28, [x26,3278]
	ldr	x0, [x26,3256]
	mov	w1, w28
	add	w28, w28, 1
	bl	_list_get_gc_head_node.isra.5
	strh	w28, [x26,3278]
	cmp	w27, w0, uxth
	beq	.L689
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L690
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bcc	.L690
	b	.L693
.L689:
	strh	wzr, [x26,3278]
	b	.L691
.L690:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L692
.L691:
	cmp	w19, w23
	bcs	.L693
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,2862]
	ldrh	w1, [x20,3276]
	cmp	w1, w0
	bls	.L688
	add	w0, w0, 8
	b	.L771
.L693:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,3276]
	ldrh	w0, [x20,2862]
	cmp	w0, w1, lsr 1
	bls	.L688
	sub	w0, w0, #8
.L771:
	strh	w0, [x20,2862]
	b	.L688
.L672:
	and	w1, w24, 1
	strh	w19, [x0,3278]
	str	w1, [x29,104]
	tbz	x24, 0, .L694
	ldrh	w2, [x0,3280]
	ldrh	w1, [x0,2836]
	cmp	w1, w2, lsr 1
	bcc	.L717
	ldrh	w0, [x0,2834]
	cmp	w0, w1
	bls	.L696
.L717:
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L748:
	add	x28, x20, :lo12:.LANCHOR0
	ldrh	w25, [x28,3278]
	ldr	x0, [x28,3256]
	mov	w1, w25
	bl	_list_get_gc_head_node.isra.5
	cmp	w26, w0, uxth
	add	w2, w25, 1
	strh	w2, [x28,3278]
	beq	.L697
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L698
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcc	.L698
	b	.L696
.L697:
	strh	wzr, [x28,3278]
	b	.L696
.L698:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L748
.L696:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3280]
	ldrh	w1, [x0,2836]
	cmp	w1, w2, lsr 1
	bls	.L694
	strh	wzr, [x0,3272]
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L700:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x25,3272]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	strh	w28, [x25,3272]
	cmp	w26, w0, uxth
	bne	.L772
.L694:
	tbz	x24, 1, .L701
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 0
	mov	w27, 65535
	uxth	w28, w22
	strh	wzr, [x0,3274]
.L706:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w2, [x23,3274]
	str	x2, [x29,96]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,96]
	cmp	w1, w27
	add	w2, w2, 1
	strh	w2, [x23,3274]
	beq	.L702
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x23,72]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x23,2858]
	cmp	w2, w1
	bcs	.L703
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L704
	add	w19, w19, 1
	add	w26, w26, 1
	uxth	w19, w19
	uxth	w26, w26
	cmp	w19, w28
	bcc	.L704
	b	.L705
.L703:
	strh	wzr, [x23,3274]
	b	.L705
.L702:
	strh	wzr, [x23,3272]
	b	.L705
.L704:
	sub	w3, w25, #1
	uxth	w25, w3
	cbnz	w25, .L706
.L705:
	cmp	w19, w22
	bcc	.L707
	cbnz	w26, .L708
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,2838]
	ldrh	w0, [x0,3282]
	cmp	w1, w0
	bls	.L708
.L707:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,3244]
	ldrh	w2, [x0,3242]
	ldrh	w1, [x0,2858]
	mul	w2, w2, w3
	ldrh	w3, [x0,3276]
	sub	w2, w2, w3, lsr 3
	cmp	w1, w2
	bge	.L701
	add	w1, w1, 4
	b	.L770
.L708:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3276]
	ldrb	w3, [x0,3244]
	ldrh	w1, [x0,2858]
	mul	w2, w2, w3
	cmp	w1, w2
	ble	.L701
	sub	w1, w1, #4
.L770:
	strh	w1, [x0,2858]
.L701:
	ldr	w0, [x29,104]
	cbz	w0, .L688
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 64
	mov	w25, 65535
	uxth	w26, w22
	strh	wzr, [x0,3274]
.L712:
	add	x27, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w0, [x27,3274]
	bl	zftl_get_gc_node
	uxth	w1, w0
	cmp	w1, w25
	beq	.L709
	ldr	x2, [x27,72]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2,x1]
	ldrh	w2, [x27,2860]
	cmp	w2, w1
	bls	.L719
	cmp	w1, 2
	bhi	.L710
.L719:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L710
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w26
	bcs	.L709
.L710:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L712
.L709:
	cmp	w19, w22
	add	x20, x20, :lo12:.LANCHOR0
	bcs	.L713
	ldrh	w1, [x20,3276]
	ldrh	w0, [x20,2860]
	cmp	w0, w1, lsr 1
	bls	.L688
	sub	w0, w0, #4
	strh	w0, [x20,2860]
	b	.L688
.L713:
	ldrh	w0, [x20,3276]
	ldrb	w2, [x20,3244]
	ldrh	w1, [x20,2860]
	mul	w0, w0, w2
	sub	w0, w0, #32
	cmp	w1, w0
	bge	.L688
	add	w1, w1, 4
	strh	w1, [x20,2860]
.L688:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L714
	adrp	x0, .LC89
	mov	w1, w21
	add	x0, x0, :lo12:.LC89
	mov	w2, w24
	mov	w3, w19
	mov	w4, w22
	bl	printk
.L714:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L772:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L699
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L694
.L699:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L700
	b	.L694
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
	ldr	x0, [x2,64]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L774
	add	x0, x2, 3288
	add	x2, x2, 2828
	b	.L777
.L774:
	cmp	w0, 16
	bne	.L776
	add	x0, x2, 3296
	add	x2, x2, 2830
	b	.L777
.L776:
	add	x0, x2, 3304
	add	x2, x2, 2832
.L777:
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
	ldr	x0, [x2,64]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L779
	add	x0, x2, 3256
	add	x2, x2, 2834
	b	.L782
.L779:
	cmp	w3, 96
	bne	.L781
	add	x0, x2, 3264
	add	x2, x2, 2836
	b	.L782
.L781:
	cmp	w3, 160
	bne	.L778
	add	x0, x2, 3248
	add	x2, x2, 2838
.L782:
	bl	_insert_data_list
.L778:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_insert_data_list, .-zftl_insert_data_list
	.align	2
	.global	zftl_get_free_sblk
	.type	zftl_get_free_sblk, %function
zftl_get_free_sblk:
	stp	x29, x30, [sp, -48]!
	uxth	w4, w0
	add	x29, sp, 0
	str	x21, [sp,32]
	uxth	w21, w1
	stp	x19, x20, [sp,16]
	cmp	w21, 5
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	bne	.L784
	ldrh	w1, [x2,2830]
	ldrh	w0, [x2,2832]
	cmp	w1, w0
	bls	.L785
	add	x0, x2, 3296
	mov	w1, w4
	add	x2, x2, 2830
	b	.L788
.L785:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, w4
	add	x0, x2, 3304
	add	x2, x2, 2832
	b	.L788
.L784:
	ldrh	w1, [x2,2828]
	ldrh	w0, [x2,2832]
	cmp	w1, w0
	bls	.L785
	add	x0, x2, 3288
	mov	w1, w4
	add	x2, x2, 2828
.L788:
	bl	_list_pop_index_node
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L787
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	mov	w1, w20
	mov	w2, w21
	ldr	x3, [x19,3288]
	ldrh	w4, [x19,2828]
	ldrh	w5, [x19,2832]
	bl	printk
.L787:
	mov	w0, w20
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
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
	ldr	x0, [x2,64]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L790
	add	x0, x2, 3256
	add	x2, x2, 2834
	b	.L793
.L790:
	cmp	w3, 96
	bne	.L792
	add	x0, x2, 3264
	add	x2, x2, 2836
	b	.L793
.L792:
	cmp	w3, 160
	bne	.L789
	add	x0, x2, 3248
	add	x2, x2, 2838
.L793:
	bl	_list_remove_node
.L789:
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
	ldr	x0, [x2,64]
	add	x0, x0, x1, uxth 2
	ldrb	w0, [x0,2]
	ands	w0, w0, 24
	bne	.L795
	add	x0, x2, 3288
	add	x2, x2, 2828
	b	.L798
.L795:
	cmp	w0, 16
	bne	.L797
	add	x0, x2, 3296
	add	x2, x2, 2830
	b	.L798
.L797:
	add	x0, x2, 3304
	add	x2, x2, 2832
.L798:
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
	ldr	x0, [x2,64]
	add	x0, x0, x1, uxth 2
	ldrb	w3, [x0,2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L800
	add	x0, x2, 3256
	add	x2, x2, 2834
	b	.L803
.L800:
	cmp	w3, 96
	bne	.L802
	add	x0, x2, 3264
	add	x2, x2, 2836
	b	.L803
.L802:
	cmp	w3, 160
	bne	.L799
	add	x0, x2, 3248
	add	x2, x2, 2838
.L803:
	bl	_list_update_data_list
.L799:
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
	adrp	x0, .LC91
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	add	x0, x0, :lo12:.LC91
	ldrh	w2, [x1]
	ldr	x1, [x19]
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L804
	mov	x22, -6148914691236517206
	adrp	x23, .LC92
	mov	w20, 0
	adrp	x25, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC92
	mov	w26, 65535
	mov	w24, 6
.L807:
	add	x21, x25, :lo12:.LANCHOR0
	ldrh	w3, [x19]
	ldrh	w4, [x19,2]
	ldrh	w5, [x19,4]
	ldr	x2, [x21,8]
	ldr	x1, [x21,64]
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
	ldr	x0, [x21,72]
	ldrh	w0, [x0,x8,lsl 1]
	str	w0, [sp,16]
	mov	x0, x23
	bl	printk
	ldrh	w19, [x19]
	cmp	w19, w26
	beq	.L804
	add	w20, w20, 1
	ldr	x0, [x21,8]
	umull	x19, w19, w24
	cmp	w20, 33
	add	x19, x0, x19
	bne	.L807
.L804:
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
	add	x0, x19, 3288
	add	x1, x19, 2828
	bl	print_list_info
	add	x0, x19, 3296
	add	x1, x19, 2830
	bl	print_list_info
	add	x0, x19, 3304
	add	x1, x19, 2832
	bl	print_list_info
	add	x0, x19, 3256
	add	x1, x19, 2834
	bl	print_list_info
	add	x0, x19, 3264
	add	x1, x19, 2836
	bl	print_list_info
	add	x0, x19, 3248
	add	x1, x19, 2838
	bl	print_list_info
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x7, .LANCHOR0
	uxth	w0, w0
	add	x2, x7, :lo12:.LANCHOR0
	mov	w12, 21
	ldr	x2, [x2,64]
	add	x2, x2, x0, uxth 2
	ldrb	w11, [x2,3]
	mov	w2, 0
	mov	w6, w2
.L813:
	add	x8, x7, :lo12:.LANCHOR0
	ldrb	w3, [x8,3244]
	cmp	w6, w3
	bge	.L819
	asr	w3, w11, w6
	tbnz	x3, 0, .L814
	ldrb	w5, [x8,3312]
	sbfiz	x10, x2, 1, 32
	ldrh	w3, [x8,3314]
	sdiv	w9, w6, w5
	sub	w3, w12, w3
	lsl	w3, w9, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,3312]
	cmp	w3, 1
	bls	.L815
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L815:
	add	w2, w2, 1
.L814:
	add	w6, w6, 1
	b	.L813
.L819:
	mov	w0, w2
	mov	w4, -1
.L817:
	add	x3, x7, :lo12:.LANCHOR0
	ldrb	w3, [x3,3244]
	cmp	w2, w3
	bge	.L820
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L817
.L820:
	ret
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	x19, w0
	mov	w0, w19
	bl	zftl_remove_data_node
	mov	w0, w19
	bl	zftl_insert_free_list
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,64]
	add	x1, x1, x19, lsl 2
	ldrb	w2, [x1,2]
	and	w2, w2, 31
	strb	w2, [x1,2]
	ldr	x0, [x0,72]
	strh	wzr, [x0,x19,lsl 1]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
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
	add	x24, x23, 2888
	add	x21, x21, 1344
.L823:
	ldrh	w0, [x24,56]
	adrp	x25, .LANCHOR0
	cmp	w0, w20
	bls	.L861
	add	x0, x24, x20, sxtw 1
	ldrh	w22, [x0,58]
	ldr	x0, [x23,72]
	lsl	x26, x22, 1
	mov	x19, x22
	ldrh	w2, [x0,x26]
	cbz	w2, .L824
	adrp	x0, .LC93
	mov	w1, w22
	add	x0, x0, :lo12:.LC93
	bl	printk
.L824:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x1, [x0,72]
	ldrh	w1, [x1,x26]
	cbnz	w1, .L825
	ldr	x0, [x0,64]
	add	x22, x0, x22, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L826
	ldrb	w2, [x22,2]
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L826:
	ldrb	w0, [x22,2]
	tst	w0, 192
	beq	.L839
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L827
.L839:
	adrp	x0, .LC0
	mov	x1, x21
	add	x0, x0, :lo12:.LC0
	mov	w2, 770
	bl	printk
.L827:
	mov	w0, w19
	add	x25, x25, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x25,80]
	ldrh	w1, [x2,124]
	cbz	w1, .L829
	mov	x0, 0
.L831:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L830
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,392]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L829
.L830:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L831
.L829:
	ldrh	w1, [x2,120]
	cbz	w1, .L832
	mov	x0, 0
.L834:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L833
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,136]
	sub	w1, w1, #1
	strh	w1, [x2,120]
	b	.L832
.L833:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L834
.L832:
	ldrh	w1, [x2,122]
	cbz	w1, .L835
	mov	x0, 0
.L837:
	add	x4, x2, x0, lsl 1
	mov	w3, w0
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L836
	add	x0, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x0,264]
	sub	w1, w1, #1
	strh	w1, [x2,122]
	b	.L835
.L836:
	add	x0, x0, 1
	cmp	x0, 64
	bne	.L837
	b	.L835
.L825:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L835:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L823
.L861:
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
	ldrh	w19, [x5,3314]
	mov	x20, x3
	sub	w19, w0, w19
	ldrb	w0, [x5,3316]
	asr	w22, w2, w19
	lsl	w19, w4, w19
	sub	w19, w19, #1
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L863
	ldrh	w2, [x5,3318]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L863:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,3318]
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
	ldr	x0, [x0,64]
	mov	w19, 0
	add	x0, x0, x20
	ldrb	w25, [x0,3]
.L868:
	add	x2, x23, :lo12:.LANCHOR0
	ldrb	w0, [x2,3320]
	cmp	w19, w0
	bge	.L879
	ldrb	w3, [x2,3312]
	mov	w21, 0
	ldrh	w2, [x2,3318]
	mov	w1, w21
	sub	w4, w3, #1
	mul	w6, w19, w3
	mul	w5, w24, w3
.L880:
	cmp	w1, w3
	bge	.L893
	add	w0, w1, w6
	asr	w0, w25, w0
	tbnz	x0, 0, .L869
	and	w0, w1, w4
	add	x7, x29, 80
	add	w0, w5, w0
	mul	w0, w0, w2
	str	w0, [x7,w21,sxtw 2]
	add	w21, w21, 1
.L869:
	add	w1, w1, 1
	b	.L880
.L893:
	cmp	w3, 4
	bne	.L892
	mov	x26, 0
.L871:
	cmp	w21, w26
	ble	.L874
	add	x2, x29, 80
	mov	w0, w19
	mov	w1, w22
	ldr	w2, [x2,x26,lsl 2]
	add	x26, x26, 1
	bl	flash_erase_block_en
	b	.L871
.L892:
	cmp	w21, 2
	bne	.L875
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3316]
	cbz	w0, .L876
	cmp	w22, wzr
	ldr	w2, [x29,80]
	ldr	w3, [x29,84]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L876:
	ldr	w2, [x29,80]
	mov	w0, w19
	ldr	w3, [x29,84]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L874
.L875:
	cmp	w21, 1
	bne	.L874
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3316]
	cbz	w0, .L878
	cmp	w22, wzr
	ldr	w2, [x29,80]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L878:
	ldr	w2, [x29,80]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L874:
	add	w19, w19, 1
	b	.L868
.L879:
	cbnz	w22, .L881
	ldr	x1, [x2,64]
	ldrh	w0, [x1,x20]
	add	w3, w0, 1
	bfi	w0, w3, 0, 11
	strh	w0, [x1,x20]
	ldr	x0, [x2,2848]
	ldr	w1, [x0,84]
	ldrh	w3, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,84]
	ldr	x1, [x2,64]
	ldrh	w1, [x1,x20]
	ubfx	x1, x1, 0, 11
	uxth	w2, w1
	cmp	w3, w2
	bge	.L883
	strh	w1, [x0,96]
	b	.L883
.L881:
	ldr	x3, [x2,64]
	ldr	w0, [x3,x20]
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x3,x20]
	ldr	x0, [x2,2848]
	ldr	w1, [x0,80]
	add	w1, w1, 1
	str	w1, [x0,80]
	ldr	x1, [x2,64]
	ldrh	w2, [x0,98]
	ldr	w1, [x1,x20]
	ubfx	x1, x1, 11, 8
	cmp	w2, w1, uxtb
	bcs	.L883
	strh	w1, [x0,98]
.L883:
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
	ldr	x1, [x1,2848]
	ldrh	w2, [x1,136]
	cmp	w2, 63
	bls	.L895
	strh	wzr, [x1,136]
.L895:
	ldrh	w0, [x1,112]
	cbnz	w0, .L896
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1360
	mov	w2, 994
	bl	printk
.L896:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0,2848]
.L900:
	ldrh	w2, [x1,136]
.L897:
	cmp	w2, 63
	bgt	.L903
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w0, [x1,x3,lsl 1]
	cmp	w0, w4
	bne	.L902
	add	w2, w2, 1
	b	.L897
.L903:
	strh	wzr, [x1,136]
	b	.L900
.L902:
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
	ldr	x2, [x0,2848]
	ldrh	w0, [x2,138]
	cmp	w0, 63
	bls	.L905
	strh	wzr, [x2,138]
.L905:
	ldrh	w0, [x2,112]
	cmp	w0, 63
	bls	.L906
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1384
	mov	w2, 1018
	bl	printk
.L906:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x1, [x1,2848]
.L910:
	ldrh	w2, [x1,138]
.L907:
	cmp	w2, 63
	bgt	.L912
	sxtw	x3, w2
	add	x3, x3, 80
	ldrh	w4, [x1,x3,lsl 1]
	cmp	w4, w0
	bne	.L908
	strh	w20, [x1,x3,lsl 1]
	strh	w2, [x1,138]
	ldrh	w0, [x1,112]
	add	w0, w0, 1
	strh	w0, [x1,112]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
.L908:
	add	w2, w2, 1
	b	.L907
.L912:
	strh	wzr, [x1,138]
	b	.L910
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
	beq	.L913
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,64]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L913
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L917
	ldr	x1, [x23,2848]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L918
.L917:
	tst	w0, 24
	ldr	x1, [x23,2848]
	bne	.L919
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L918
.L919:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L918:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L920
	ldr	w0, [x21,x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21,x20]
	ldr	x1, [x19,2848]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L913
.L920:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L921
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2848]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L913
.L921:
	cmp	w0, 96
	bne	.L913
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2848]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L913:
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
	ldrh	w0, [x4,3276]
	ldr	x3, [x4,80]
	ldrb	w4, [x4,3244]
	mul	w0, w0, w4
	cmp	w1, w0
	bge	.L923
	add	x3, x3, 16
	b	.L924
.L923:
	sub	w1, w1, w0
	add	x3, x3, 48
	uxth	w1, w1
.L924:
	ldrb	w2, [x3,9]
	mov	w0, 65535
	sdiv	w20, w1, w2
	msub	w1, w20, w2, w1
	add	x1, x3, x1, uxth 1
	ldrh	w21, [x1,16]
	cmp	w21, w0
	bne	.L925
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1408
	mov	w2, 1403
	bl	printk
.L925:
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x2,3318]
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
	ldrh	w2, [x2,3328]
	ldr	x4, [x1,3840]
	ldr	x5, [x1,3848]
.L927:
	cmp	w2, w3
	beq	.L931
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L928
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L929
.L928:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L927
.L931:
	mov	w0, -1
.L929:
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
	bne	.L933
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1432
	mov	w2, 1518
	bl	printk
.L933:
	adrp	x20, .LANCHOR0
	ldrh	w1, [x19,2]
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3276]
	cmp	w1, w0
	bne	.L934
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1432
	mov	w2, 1519
	bl	printk
.L934:
	ldrh	w0, [x19,6]
	cbnz	w0, .L935
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1432
	mov	w2, 1520
	bl	printk
.L935:
	ldrb	w0, [x19,5]
	add	x1, x20, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrb	w2, [x1,3244]
	mov	w1, 65535
	ldrh	w0, [x19,x0,lsl 1]
.L936:
	cmp	w0, w1
	bne	.L940
	ldrb	w0, [x19,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,5]
	cmp	w0, w2
	bne	.L937
	ldrh	w0, [x19,2]
	strb	wzr, [x19,5]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L937:
	ldrb	w0, [x19,5]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L936
.L940:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x19,2]
	ldrh	w1, [x20,3318]
	ldrb	w3, [x20,3244]
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
	bne	.L939
	add	w2, w2, 1
	strb	wzr, [x19,5]
	strh	w2, [x19,2]
.L939:
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
	bcs	.L942
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1456
	mov	w2, 1551
	bl	printk
.L942:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w20
	bl	ftl_erase_phy_blk
	ldr	x0, [x19,2848]
	ldrh	w0, [x0,130]
	bl	ftl_free_sys_blk
	ldr	x0, [x19,2848]
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
	ldrh	w2, [x3,3856]
	cbnz	w0, .L944
	cmp	w2, 4
	bhi	.L944
	add	w2, w2, 1
	strh	w2, [x3,3856]
	b	.L943
.L944:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w9, 65535
	strh	wzr, [x0,3856]
	ldrh	w6, [x0,56]
	ldr	x7, [x0,72]
	ldr	x8, [x0,64]
	mov	x0, 0
	mov	w2, w0
	mov	w3, w0
.L946:
	cmp	w6, w0, uxth
	bls	.L954
	ldrh	w4, [x7,x0,lsl 1]
	cmp	w4, w9
	beq	.L947
	add	x5, x8, x0, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L948
	add	w2, w2, w4
	b	.L947
.L948:
	add	w3, w3, w4
.L947:
	add	x0, x0, 1
	b	.L946
.L954:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,80]
	ldr	x1, [x1,2848]
	str	w3, [x0,524]
	str	w2, [x0,528]
	ldrh	w1, [x1,120]
	cbz	w1, .L943
	udiv	w2, w2, w1
	str	w2, [x0,532]
.L943:
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
	adrp	x0, .LANCHOR0+72
	ldr	x0, [x0,#:lo12:.LANCHOR0+72]
	ldrh	w0, [x0,x19]
	ldr	x19, [sp,16]
	cmp	w0, wzr
	cset	w0, eq
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
	beq	.L958
	add	x0, x2, :lo12:.LANCHOR0
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,72]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L959
	adrp	x0, .LC95
	mov	w2, w19
	add	x0, x0, :lo12:.LC95
	bl	printk
	b	.L963
.L959:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L958:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3858]
	cmp	w2, w1
	beq	.L960
	mov	w0, 65535
	cmp	w2, w0
	bne	.L961
	strh	w1, [x19,3858]
.L963:
	mov	w0, 0
	b	.L960
.L961:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,3858]
.L960:
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
	add	x4, x6, 3328
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,3840]
	ldr	x12, [x6,3848]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L965:
	cmp	w4, w11
	beq	.L969
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L966
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR0
	bne	.L967
	ldr	x6, [x4,3848]
	add	x4, x4, 3328
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L968
.L967:
	ldr	x4, [x4,3848]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L968:
	add	x4, x3, :lo12:.LANCHOR0
	mov	w6, -1
	ldr	x4, [x4,3848]
	strh	w6, [x4,x7,lsl 1]
	b	.L969
.L966:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L965
.L969:
	add	x3, x3, :lo12:.LANCHOR0
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,3840]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 3328
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,3848]
	strh	w6, [x0,x4,lsl 1]
	beq	.L971
	ldrh	w0, [x3,3314]
	mov	w2, 21
	mov	w4, 1
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w4, w2
	ldrb	w0, [x3,3312]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L971:
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
	ldrh	w5, [x1,3314]
	sub	w2, w2, w5
	lsr	w0, w0, w5
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x1,3312]
	udiv	w2, w0, w2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	uxtb	w20, w2
	tbz	x0, 14, .L976
	adrp	x0, .LC96
	mov	w1, w21
	add	x0, x0, :lo12:.LC96
	mov	w2, w20
	bl	printk
.L976:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,56]
	cmp	w1, w20
	bls	.L975
	ldr	x0, [x0,64]
	add	x20, x0, x20, uxtb 2
	mov	w0, 1
	lsl	w1, w0, w21
	ldrb	w0, [x20,3]
	orr	w1, w1, w0
	strb	w1, [x20,3]
.L975:
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
	ldr	x7, [x19,80]
	ldrh	w6, [x19,2838]
	ldrh	w1, [x19,2828]
	ldr	w0, [x7,528]
	str	w0, [sp]
	ldr	w0, [x19,2824]
	str	w0, [sp,8]
	adrp	x0, .LC97
	ldrh	w2, [x19,2830]
	add	x0, x0, :lo12:.LC97
	ldr	w7, [x7,524]
	ldrh	w3, [x19,2832]
	ldrh	w4, [x19,2834]
	ldrh	w5, [x19,2836]
	bl	printk
	ldr	x5, [x19,2848]
	adrp	x0, .LC98
	ldrb	w3, [x19,2841]
	add	x0, x0, :lo12:.LC98
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,2848]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,2848]
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
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
	bl	printk
	ldrh	w1, [x19,2858]
	adrp	x0, .LC101
	ldrh	w2, [x19,2860]
	add	x0, x0, :lo12:.LC101
	ldrh	w3, [x19,2862]
	ldrh	w4, [x19,2864]
	ldrh	w5, [x19,2856]
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
	cbnz	x0, .L983
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 639
	add	x1, x1, 1480
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	print_ftl_debug_info
	mov	w0, -1
	b	.L992
.L983:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x20,56]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2816]
	cmp	w1, w0
	bls	.L988
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1480
	mov	w2, 644
	bl	printk
.L988:
	ldrb	w0, [x20,56]
	cbz	w0, .L985
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,2816]
	cmp	w1, w0
	bcs	.L986
.L985:
	mov	x0, x20
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_free
	ldrb	w0, [x19,2840]
	b	.L992
.L986:
	add	x0, x21, 2866
	mov	x1, x20
	bl	buf_add_tail
	ldr	x0, [x21,2848]
	ldrb	w1, [x20,56]
	ldrb	w2, [x21,2840]
	ldr	w3, [x0,16]
	add	w2, w2, 1
	add	w1, w3, w1
	str	w1, [x0,16]
	ldr	w1, [x0,32]
	uxtb	w2, w2
	strb	w2, [x21,2840]
	add	w1, w1, 1
	str	w1, [x0,32]
	mov	w0, w2
.L992:
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
	adrp	x21, .LC102
	stp	x23, x24, [sp,48]
	add	x22, x19, 768
	mov	w23, 21
	mov	w24, 1
	add	x21, x21, :lo12:.LC102
.L994:
	ldrb	w0, [x19,3860]
	cmp	w0, 255
	beq	.L1001
	sbfiz	x0, x0, 6, 32
	add	x20, x22, x0
	add	x4, x20, 32
	ldrb	w0, [x22,x0]
	strb	w0, [x19,3860]
	ldr	w0, [x20,52]
	cmn	w0, #1
	bne	.L995
	ldrh	w2, [x19,3314]
	mov	x0, x21
	ldr	w3, [x4,8]
	sub	w1, w23, w2
	lsl	w1, w24, w1
	lsr	w2, w3, w2
	sub	w1, w1, #1
	and	w2, w1, w2
	ldrb	w1, [x19,3312]
	udiv	w1, w2, w1
	ldr	w2, [x4,4]
	uxth	w25, w1
	mov	w1, w25
	bl	printk
	mov	w1, 1
	mov	w0, w25
	mov	w2, 0
	bl	gc_add_sblk
	ldr	x0, [x19,80]
	ldr	w1, [x0,556]
	add	w1, w1, 1
	str	w1, [x0,556]
	ldrh	w1, [x0,16]
	cmp	w1, w25
	bne	.L996
	strh	wzr, [x0,22]
	b	.L997
.L996:
	ldrh	w1, [x0,48]
	cmp	w1, w25
	bne	.L997
	strh	wzr, [x0,54]
.L997:
	mov	x0, x20
	bl	ftl_write_buf
	b	.L994
.L995:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x20,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x20,2]
	tbz	x0, 2, .L999
	and	w0, w0, -3
	strb	w0, [x20,2]
	b	.L994
.L999:
	mov	x0, x20
	bl	buf_free
	b	.L994
.L1001:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_write_completed, .-ftl_write_completed
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
	beq	.L1004
	adrp	x0, .LANCHOR0+64
	ldr	x21, [x0,#:lo12:.LANCHOR0+64]
	add	x21, x21, x19, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L1005
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1496
	mov	w2, 757
	bl	printk
.L1005:
	ldrb	w1, [x21,2]
	bfi	w1, w20, 5, 3
	ubfx	x2, x1, 3, 2
	orr	w0, w22, w2
	bfi	w1, w0, 3, 2
	strb	w1, [x21,2]
	b	.L1011
.L1004:
	adrp	x21, .LC103
	bl	print_ftl_debug_info
	add	x21, x21, :lo12:.LC103
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
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1496
	mov	w2, 767
	bl	printk
.L1011:
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
	strb	w1, [x0,3861]
	strb	w1, [x0,3860]
	strb	w1, [x0,2880]
	strb	w1, [x0,3224]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC104
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC104
	ldrb	w1, [x20,3861]
	bl	printk
	ldrb	w19, [x20,3861]
	cmp	w19, 255
	beq	.L1013
	add	x20, x20, 768
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC105
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC105
.L1015:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1013
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L1015
.L1013:
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
	ldrb	w2, [x3,3861]
	cmp	w2, 255
	beq	.L1023
	ldrh	w8, [x3,3314]
	mov	w4, 21
	ldrb	w7, [x3,3312]
	mov	w0, 1
	sub	w4, w4, w8
	ubfx	x9, x5, 21, 3
	lsl	w4, w0, w4
	sub	w7, w7, #1
	sub	w4, w4, #1
	asr	w5, w5, w8
	uxth	w7, w7
	adrp	x10, .L1027
	uxth	w4, w4
	and	w5, w5, w7
	and	w5, w5, w4
	add	x3, x3, 768
	add	x10, x10, :lo12:.L1027
.L1030:
	add	x0, x3, x2, lsl 6
	ldr	w6, [x0,40]
	ubfx	x11, x6, 21, 3
	cmp	w11, w9
	bne	.L1024
	lsr	w6, w6, w8
	ldrb	w0, [x0,58]
	and	w6, w7, w6
	and	w6, w4, w6
	cmp	w6, w5
	bne	.L1025
	cmp	w1, 1
	beq	.L1028
	b	.L1023
.L1025:
	cmp	w1, 3
	bhi	.L1024
	ldrb	w6, [x10,w1,uxtw]
	adr	x11, .Lrtx1027
	add	x6, x11, w6, sxtb #2
	br	x6
.Lrtx1027:
	.section	.rodata
	.align	0
	.align	2
.L1027:
	.byte	(.L1026 - .Lrtx1027) / 4
	.byte	(.L1028 - .Lrtx1027) / 4
	.byte	(.L1029 - .Lrtx1027) / 4
	.byte	(.L1023 - .Lrtx1027) / 4
	.text
.L1026:
	cmp	w0, 2
	b	.L1039
.L1028:
	sub	w6, w0, #6
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L1024
	b	.L1023
.L1029:
	cmp	w0, 10
.L1039:
	bne	.L1023
.L1024:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L1030
	mov	w0, 0
.L1023:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w2, [x1,3861]
	cmp	w2, 255
	beq	.L1041
	add	x6, x1, 768
	lsl	x5, x2, 6
	add	x7, x6, x5
	ldrb	w3, [x7,58]
	sub	w3, w3, #11
	uxtb	w3, w3
	cmp	w3, 1
	bhi	.L1041
	ldrb	w3, [x6,x5]
	strb	w3, [x1,3861]
	mov	w3, -1
	strb	w3, [x6,x5]
	ldrb	w3, [x7,59]
	cmp	w3, 1
	bne	.L1043
	ldrh	w3, [x7,50]
	cbnz	w3, .L1044
	ldrb	w3, [x1,3860]
	add	x1, x1, 3860
	b	.L1045
.L1044:
	ldrb	w3, [x1,3224]
	add	x1, x1, 3224
	b	.L1045
.L1043:
	cbnz	w3, .L1041
	ldr	w3, [x7,36]
	cmn	w3, #1
	beq	.L1041
	ldrb	w3, [x1,2880]
	add	x1, x1, 2880
.L1045:
	cmp	w3, 255
	add	x0, x0, :lo12:.LANCHOR0
	bne	.L1049
	add	x0, x0, x2, lsl 6
	ldrb	w0, [x0,769]
	strb	w0, [x1]
	b	.L1041
.L1049:
	add	x0, x0, 768
.L1056:
	mov	w1, w3
	sbfiz	x3, x3, 6, 32
	ldrb	w3, [x0,x3]
	cmp	w3, 255
	bne	.L1056
	add	x2, x0, x2, lsl 6
	sbfiz	x1, x1, 6, 32
	ldrb	w2, [x2,1]
	strb	w2, [x0,x1]
.L1041:
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
	ldr	x2, [x3,2848]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	ldrb	w1, [x3,3244]
	strh	w0, [x2,690]
	cmp	w1, w0
	bls	.L1058
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1059
.L1058:
	adrp	x22, .LC106
	add	x22, x22, :lo12:.LC106
.L1071:
	mov	w0, 1
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_alloc_sblk
	uxth	w21, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x20,2848]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L1060
	mov	w1, w21
	mov	x0, x22
	bl	printk
	ldr	x2, [x20,64]
	add	x1, x2, x21, uxth 2
	ldrb	w2, [x1,2]
	orr	w2, w2, -32
	strb	w2, [x1,2]
	b	.L1071
.L1060:
	ldr	x1, [x20,2848]
	mov	w0, 1
	mov	w2, 65535
	str	w0, [x20,3864]
	mov	x20, 0
	strh	wzr, [x1,690]
.L1062:
	add	x3, x1, x20, lsl 1
	uxth	w0, w20
	ldrh	w3, [x3,416]
	cmp	w3, w2
	beq	.L1061
	add	x20, x20, 1
	cmp	x20, 128
	bne	.L1062
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 192
	bl	printk
	mov	w0, w20
.L1061:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2848]
	add	x0, x1, x0, sxtw 1
	strh	w21, [x0,416]
	ldrh	w0, [x1,688]
	add	w0, w0, 1
	strh	w0, [x1,688]
.L1059:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2848]
	ldrh	w0, [x1,690]
	add	x0, x0, 336
	ldrh	w20, [x1,x0,lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L1064
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 197
	bl	printk
.L1064:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 21
	ldr	x1, [x19,2848]
	ldrh	w2, [x19,3314]
	sub	w0, w0, w2
	strh	wzr, [x1,696]
	asr	w4, w20, w0
	strh	w20, [x1,692]
	strh	w4, [x1,694]
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 12, .L1068
	mov	w2, 1
	mov	w1, w20
	lsl	w2, w2, w0
	adrp	x0, .LC107
	add	x0, x0, :lo12:.LC107
	sub	w2, w2, #1
	mov	w3, w20
	uxth	w4, w4
	bl	printk
.L1068:
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
.L1075:
	add	x1, x2, 3872
	lsl	x4, x19, 4
	uxth	w0, w19
	ldrh	w4, [x4,x1]
	cmp	w4, w3
	beq	.L1074
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L1075
	mov	x0, 0
	mov	w3, 32768
.L1077:
	add	x2, x1, x0, lsl 4
	uxth	w4, w0
	ldrh	w2, [x2,2]
	tbnz	x2, 15, .L1076
	cmp	w2, w3
	bcs	.L1076
	mov	w3, w2
	mov	w19, w4
.L1076:
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L1077
	cmp	w19, 32
	mov	w0, w19
	bne	.L1074
	adrp	x0, .LANCHOR4+32
	mov	w2, -1
	mov	w1, 0
	ldrb	w3, [x0,#:lo12:.LANCHOR4+32]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 3872
.L1079:
	ldrh	w5, [x0,2]
	cmp	w5, w2
	bcs	.L1078
	ldrh	w4, [x0]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	cmp	w4, w3
	csel	w19, w19, w1, eq
.L1078:
	add	w1, w1, 1
	add	x0, x0, 16
	uxth	w1, w1
	cmp	w1, 32
	bne	.L1079
	cmp	w19, 32
	bne	.L1080
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 24
	mov	w2, 295
	bl	printk
.L1080:
	mov	w0, w19
.L1074:
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
	cbnz	w0, .L1088
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1089:
	add	x1, x2, 212
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1089
.L1095:
	adrp	x20, .LANCHOR4
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1024
	add	x0, x20, 36
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, 0
	b	.L1090
.L1088:
	cmp	w0, 1
	bne	.L1091
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L1094:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L1092
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1092:
	add	x2, x3, 212
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1094
	b	.L1095
.L1091:
	cmp	w0, 2
	bne	.L1096
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L1098:
	add	x3, x4, 212
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L1098
	b	.L1095
.L1096:
	cmp	w0, 3
	bne	.L1099
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L1102:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L1100
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L1100:
	add	x2, x3, 212
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L1102
	b	.L1095
.L1099:
	cmp	w0, 4
	bne	.L1103
	add	x2, x19, :lo12:.LANCHOR0
	mov	w5, 7
	add	x1, x2, 212
	mov	w4, 6
	strh	w0, [x1,8]
	mov	w0, 5
	strh	wzr, [x2,212]
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
.L1105:
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
	bne	.L1105
	b	.L1095
.L1103:
	cmp	w0, 5
	bne	.L1106
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1107:
	add	x1, x2, 212
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1107
	mov	x0, 0
.L1108:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L1108
	b	.L1095
.L1106:
	cmp	w0, 8
	bne	.L1095
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L1109:
	add	x1, x2, 212
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L1109
	b	.L1095
.L1090:
	add	x1, x19, 212
	add	x2, x20, 36
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x2,w1,sxtw 1]
	bne	.L1090
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
	tbz	x0, 12, .L1126
	adrp	x1, .LANCHOR3
	adrp	x0, .LC4
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC4
	add	x1, x1, 48
	bl	printk
.L1126:
	add	x25, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	add	x26, x22, :lo12:.LANCHOR4
	mov	w2, 8
	ldrh	w0, [x25,402]
	mov	x24, 0
	ldrb	w1, [x25,388]
	add	x27, x20, 732
	strh	w0, [x20,210]
	add	x25, x25, 377
	ldrh	w0, [x25,9]
	strb	wzr, [x20,98]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x26,1060]
	add	x0, x20, 200
	bl	ftl_memset
	add	x0, x26, 1064
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
	ldrb	w23, [x25,-1]
.L1128:
	mov	x0, x25
	add	x1, x27, x24, lsl 3
	mov	w2, w23
	bl	flash_mem_cmp8
	cbnz	w0, .L1127
	ldrb	w1, [x20,98]
	add	x2, x26, 1064
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x20, x1, sxtw
	strb	w0, [x20,98]
	strb	w24, [x1,200]
.L1127:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L1128
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 376
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L1129
.L1133:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR4
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,389]
	ldrb	w0, [x21,98]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,390]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1096]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L1129:
	add	x24, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w27, [x1,23]
	mov	x25, 0
	and	w20, w20, 65280
	add	x26, x0, 377
	ldrh	w2, [x24,210]
	add	x3, x24, 732
	add	x4, x22, :lo12:.LANCHOR4
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L1132:
	add	x1, x3, x25, lsl 3
	mov	x0, x26
	mov	w2, w23
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L1130
	ldrb	w1, [x24,98]
	add	x0, x4, 1064
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x0,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x24, x1, sxtw
	strb	w0, [x24,98]
	strb	w25, [x1,200]
.L1130:
	add	x25, x25, 1
	cmp	x25, 4
	bne	.L1132
	b	.L1133
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
	add	x0, x19, 3328
	bl	ftl_memset
	ldrh	w0, [x19,3276]
	mov	w1, 255
	ldrb	w2, [x19,3244]
	mul	w2, w0, w2
	ldr	x0, [x19,3848]
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
	tbz	x0, 12, .L1144
	adrp	x1, .LANCHOR3
	adrp	x0, .LC108
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC108
	add	x1, x1, 72
	mov	w2, 218
	mov	w3, 0
	bl	printk
.L1144:
	adrp	x19, .LANCHOR0
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 512
	add	x0, x19, 3328
	bl	ftl_memset
	ldrh	w0, [x19,3276]
	mov	w1, 255
	ldrb	w2, [x19,3244]
	mul	w2, w0, w2
	ldr	x0, [x19,3848]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L1145:
	ldrh	w1, [x19,3276]
	ldrb	w2, [x19,3244]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L1154
	uxtw	x2, w0
	ldr	x1, [x19,3840]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L1146
	uxtb	w1, w1
	add	x3, x19, 3328
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,3848]
	strh	w4, [x1,x2,lsl 1]
.L1146:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1145
.L1154:
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
	ldrh	w3, [x0,210]
	mul	w2, w2, w3
	ldrb	w3, [x0,188]
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
	ldrb	w4, [x0,98]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L1156:
	cmp	w4, w1, uxtb
	bls	.L1158
	add	x2, x0, 200
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L1156
.L1158:
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
	add	x21, x19, 2888
	mov	x0, x21
	strb	wzr, [x19,3241]
	strb	wzr, [x20,1098]
	bl	ftl_memset
	ldrh	w1, [x19,3276]
	mov	w0, -1
	ldrb	w3, [x19,3244]
	strh	w0, [x19,2888]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x19,2924]
	strh	w0, [x19,2862]
	ldrh	w0, [x19,3242]
	strh	w2, [x19,2926]
	mul	w2, w1, w3
	mul	w0, w0, w3
	strh	w1, [x19,2864]
	uxth	w2, w2
	lsl	w0, w0, 2
	sub	w4, w2, #32
	strh	w2, [x19,2858]
	strh	w4, [x19,2860]
	str	xzr, [x19,2896]
	strh	wzr, [x19,3278]
	strh	wzr, [x19,3272]
	strh	wzr, [x19,3274]
	bl	ftl_malloc
	ldrb	w1, [x19,3244]
	str	x0, [x20,1104]
	ldrh	w0, [x19,3242]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1112]
	ldrh	w1, [x19,3242]
	ldrb	w0, [x19,3244]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3232]
	ldrb	w1, [x19,3244]
	ldrh	w0, [x19,3242]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3216]
	ldrb	w1, [x19,3244]
	ldrh	w0, [x19,3242]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1120]
	ldrh	w0, [x19,3280]
	strh	w0, [x19,2856]
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	gc_init, .-gc_init
	.align	2
	.global	gc_static_wearleveling
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	stp	x29, x30, [sp, -144]!
	mov	w1, 10240
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x6, [x0,2848]
	ldr	w0, [x6,32]
	cmp	w0, w1
	bls	.L1161
	ldr	w1, [x6,36]
	add	w1, w1, w0, lsr 10
	and	w0, w0, 1023
	str	w1, [x6,36]
	str	w0, [x6,32]
.L1161:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,80]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L1162
	ldr	w1, [x0,572]
	ldr	w3, [x6,36]
	add	w1, w1, 32
	cmp	w3, w1
	bls	.L1160
.L1162:
	ldr	w1, [x6,36]
	mov	w9, 0
	str	w1, [x0,572]
	mov	w26, 65535
	str	w2, [x0,568]
	add	x0, x20, :lo12:.LANCHOR0
	mov	w4, w9
	mov	w2, w9
	ldrh	w3, [x6,134]
	mov	w1, w9
	ldrh	w11, [x0,56]
	mov	w22, w9
	ldr	x10, [x0,64]
	mov	w21, w9
	mov	w19, w26
	mov	w12, w26
.L1164:
	cmp	w3, w11
	bcs	.L1208
	ubfiz	x0, x3, 2, 16
	add	x5, x10, x0
	ldrb	w5, [x5,2]
	tbz	x5, 3, .L1165
	ldrh	w5, [x10,x0]
	and	w8, w5, 2047
	ldr	w5, [x10,x0]
	ubfx	x0, x5, 11, 8
	b	.L1166
.L1165:
	tst	w5, 24
	bne	.L1167
	ldrh	w5, [x10,x0]
	mov	w0, 65535
	and	w8, w5, 2047
	b	.L1166
.L1167:
	ldr	w5, [x10,x0]
	ubfx	x0, x5, 11, 8
.L1190:
	cmp	w19, w0
	add	w2, w2, 1
	csel	w19, w19, w0, cc
	cmp	w21, w0
	add	w4, w4, w0
	csel	w0, w21, w0, hi
	uxth	w2, w2
	uxth	w19, w19
	uxth	w21, w0
.L1191:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L1164
.L1208:
	ldrh	w0, [x6,96]
	cmp	w0, w22
	bcs	.L1169
	strh	w22, [x6,96]
.L1169:
	ldrh	w0, [x6,98]
	cmp	w0, w21
	bcs	.L1170
	strh	w21, [x6,98]
.L1170:
	adrp	x23, .LANCHOR2
	str	x23, [x29,128]
	udiv	w3, w9, w1
	strh	w3, [x6,88]
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	udiv	w4, w4, w2
	strh	w4, [x6,90]
	tbz	x0, 10, .L1171
	adrp	x0, .LC109
	uxth	w3, w3
	add	x0, x0, :lo12:.LC109
	uxth	w4, w4
	bl	printk
.L1171:
	ldr	x0, [x29,128]
	adrp	x25, .LANCHOR4
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1172
	add	x6, x25, :lo12:.LANCHOR4
	adrp	x0, .LC110
	add	x0, x0, :lo12:.LC110
	mov	w1, w26
	mov	w2, w19
	mov	w3, w22
	ldrh	w5, [x6,1128]
	mov	w4, w21
	ldrh	w6, [x6,1130]
	bl	printk
.L1172:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 128
	ldr	x0, [x0,80]
	add	x0, x0, 264
	strh	wzr, [x0,-142]
	bl	ftl_memset
	add	x1, x25, :lo12:.LANCHOR4
	sub	w0, w21, w19
	str	w0, [x29,136]
	ldrh	w0, [x1,1130]
	ldr	w2, [x29,136]
	cmp	w2, w0
	bgt	.L1173
	ldrh	w1, [x1,1128]
	sub	w0, w22, w26
	cmp	w0, w1
	bgt	.L1173
.L1180:
	cbz	w19, .L1182
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldrh	w0, [x0,134]
	b	.L1183
.L1173:
	add	x9, x20, :lo12:.LANCHOR0
	adrp	x27, .LC112
	adrp	x28, .LC111
	mov	w24, 0
	mov	x23, x9
	str	w24, [x29,140]
	ldr	x0, [x9,2848]
	ldrh	w21, [x0,134]
	sub	w0, w22, w26
	str	w0, [x29,124]
	add	x0, x27, :lo12:.LC112
	str	x0, [x29,112]
	add	x0, x28, :lo12:.LC111
	str	x0, [x29,104]
.L1176:
	ldrh	w0, [x23,56]
	cmp	w0, w21
	bls	.L1180
	uxtw	x22, w21
	ldr	x28, [x23,64]
	lsl	x27, x22, 2
	add	x0, x28, x27
	ldrb	w0, [x0,2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L1177
	tst	w0, 192
	cset	w2, eq
	cbnz	w2, .L1177
	ubfx	x0, x0, 3, 2
	and	w0, w0, 2
	uxtb	w1, w0
	add	x0, x25, :lo12:.LANCHOR4
	cbz	w1, .L1178
	ldrh	w0, [x0,1130]
	ldr	w1, [x29,136]
	cmp	w1, w0
	ble	.L1179
	ldr	w0, [x28,x27]
	add	w1, w19, 1
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1179
	mov	w1, w2
	mov	w0, w21
	mov	w2, 1
	bl	gc_add_sblk
	ldr	w0, [x29,140]
	add	w0, w0, 1
	str	w0, [x29,140]
	ldr	x0, [x29,128]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1179
	ldr	x0, [x23,64]
	ldr	x11, [x23,72]
	add	x0, x0, x27
	ldrh	w5, [x28,x27]
	ldr	w6, [x28,x27]
	ldrb	w2, [x0,2]
	ldr	x0, [x29,104]
	b	.L1207
.L1178:
	ldrh	w0, [x0,1128]
	ldr	w2, [x29,124]
	cmp	w2, w0
	ble	.L1179
	ldrh	w0, [x28,x27]
	add	w2, w26, 1
	and	w0, w0, 2047
	cmp	w0, w2
	bgt	.L1179
	mov	w0, w21
	mov	w2, 1
	add	w24, w24, 1
	bl	gc_add_sblk
	ldr	x0, [x29,128]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 10, .L1179
	ldr	x0, [x23,64]
	ldr	x11, [x23,72]
	add	x0, x0, x27
	ldrh	w5, [x28,x27]
	ldr	w6, [x28,x27]
	ldrb	w2, [x0,2]
	ldr	x0, [x29,112]
.L1207:
	ldrh	w3, [x11,x22,lsl 1]
	mov	w1, w21
	ldrh	w4, [x23,2944]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1179:
	cmp	w24, 4
	bhi	.L1180
	ldr	w0, [x29,140]
	cmp	w0, 4
	bhi	.L1180
.L1177:
	add	w10, w21, 1
	uxth	w21, w10
	b	.L1176
.L1185:
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,64]
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w19
	bcc	.L1184
	sub	w2, w2, w19
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1184:
	add	w0, w0, 1
	uxth	w0, w0
.L1183:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,56]
	cmp	w2, w0
	bhi	.L1185
	ldr	x0, [x1,2848]
	ldrh	w1, [x0,72]
	add	w1, w19, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w19
	bls	.L1182
	sub	w19, w1, w19
	strh	w19, [x0,98]
.L1182:
	cbz	w26, .L1160
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldrh	w0, [x0,134]
.L1187:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,56]
	cmp	w2, w0
	bls	.L1209
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,64]
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w26
	blt	.L1188
	sub	w2, w2, w26
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1188:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1187
.L1209:
	ldr	x0, [x1,2848]
	ldrh	w1, [x0,74]
	add	w1, w26, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w26
	bls	.L1160
	sub	w7, w1, w26
	strh	w7, [x0,96]
	b	.L1160
.L1166:
	cmp	w26, w8
	add	w1, w1, 1
	csel	w7, w26, w8, cc
	cmp	w22, w8
	add	w9, w9, w8
	csel	w8, w22, w8, hi
	cmp	w0, w12
	uxth	w1, w1
	uxth	w26, w7
	uxth	w22, w8
	bne	.L1190
	b	.L1191
.L1160:
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
	ldrh	w2, [x19,56]
	adrp	x26, .LC113
	ldr	x0, [x19,8]
	mov	w25, -1
	add	x26, x26, :lo12:.LC113
	mul	w2, w2, w24
	bl	ftl_memset
	ldrh	w22, [x19,3276]
	ldrb	w0, [x19,3244]
	str	xzr, [x19,3288]
	str	xzr, [x19,3296]
	mul	w22, w0, w22
	str	xzr, [x19,3304]
	str	xzr, [x19,3264]
	sdiv	w22, w23, w22
	str	xzr, [x19,3256]
	str	xzr, [x19,3248]
	strh	wzr, [x19,2828]
	strh	wzr, [x19,2830]
	strh	wzr, [x19,2832]
	strh	wzr, [x19,2834]
	strh	wzr, [x19,2838]
	strh	wzr, [x19,2836]
	sxth	w0, w22
	str	w0, [x29,108]
	ldr	x0, [x19,2848]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1211:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x1,56]
	cmp	w19, w0
	bge	.L1241
	sxtw	x22, w19
	ldr	x27, [x1,64]
	ldr	w2, [x29,108]
	add	x27, x27, x22, lsl 2
	ldrb	w0, [x27,3]
	cbz	w0, .L1212
	mov	w0, 0
	ldrb	w5, [x1,3244]
	ldrh	w6, [x1,3276]
	mov	w2, w0
	ldr	x4, [x1,2848]
.L1213:
	cmp	w0, w5
	bge	.L1242
	ldrb	w1, [x27,3]
	asr	w1, w1, w0
	tbnz	x1, 0, .L1214
	add	w2, w6, w2
	sxth	w2, w2
	b	.L1215
.L1214:
	ldrh	w1, [x4,146]
	add	w1, w1, 1
	strh	w1, [x4,146]
.L1215:
	add	w0, w0, 1
	b	.L1213
.L1242:
	cbz	w2, .L1217
	sdiv	w2, w23, w2
	sxth	w2, w2
	b	.L1212
.L1217:
	ldrb	w0, [x27,2]
	orr	w0, w0, -32
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	strh	w25, [x0,x22,lsl 1]
.L1212:
	add	x20, x21, :lo12:.LANCHOR0
	smull	x0, w19, w24
	ldr	x1, [x20,8]
	add	x1, x1, x0
	strh	w2, [x1,4]
	ldr	x1, [x20,8]
	add	x2, x1, x0
	strh	w25, [x2,2]
	strh	w25, [x1,x0]
	ldrb	w1, [x27,2]
	and	w0, w1, 224
	cmp	w0, 224
	beq	.L1220
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1220
	ldr	x2, [x20,80]
	ldrh	w4, [x2,16]
	cmp	w19, w4
	beq	.L1220
	ldrh	w4, [x2,48]
	cmp	w19, w4
	beq	.L1220
	ldrh	w2, [x2,80]
	cmp	w19, w2
	beq	.L1220
	cmp	w0, 64
	bne	.L1222
	uxth	w27, w19
	add	x0, x20, 3256
	mov	w1, w27
	add	x2, x20, 2834
	b	.L1239
.L1222:
	cmp	w0, 96
	bne	.L1223
	uxth	w27, w19
	add	x0, x20, 3264
	mov	w1, w27
	add	x2, x20, 2836
	b	.L1239
.L1223:
	cmp	w0, 160
	bne	.L1224
	uxth	w27, w19
	add	x0, x20, 3248
	mov	w1, w27
	add	x2, x20, 2838
.L1239:
	bl	_insert_data_list
	ldr	x0, [x20,72]
	ldrh	w0, [x0,x22,lsl 1]
	cmp	w0, 7
	bhi	.L1220
	mov	w0, w27
	mov	w1, 1
	mov	w2, w28
	b	.L1237
.L1224:
	cbnz	w0, .L1220
	ldr	x0, [x20,72]
	ldrh	w2, [x0,x22,lsl 1]
	cbz	w2, .L1225
	mov	x0, x26
	mov	w1, w19
	bl	printk
	ldrb	w0, [x27,2]
	tbz	x0, 4, .L1226
	mov	w1, 5
	b	.L1233
.L1226:
	mov	w1, 2
.L1233:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x27,2]
	mov	w1, 1
	mov	w0, w19
.L1237:
	bl	gc_add_sblk
	b	.L1220
.L1225:
	ands	w1, w1, 24
	add	x2, x21, :lo12:.LANCHOR0
	bne	.L1228
	add	x0, x2, 3288
	mov	w1, w19
	add	x2, x2, 2828
	b	.L1235
.L1228:
	cmp	w1, 16
	bne	.L1229
	add	x0, x2, 3296
	mov	w1, w19
	add	x2, x2, 2830
	b	.L1235
.L1229:
	add	x0, x2, 3304
	mov	w1, w19
	add	x2, x2, 2832
.L1235:
	bl	_insert_free_list
.L1220:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1211
.L1241:
	ldr	x0, [x1,2848]
	ldrh	w2, [x1,2828]
	strh	w2, [x0,114]
	ldrh	w2, [x1,2830]
	strh	w2, [x0,118]
	ldrh	w2, [x1,2832]
	strh	w2, [x0,116]
	ldrh	w2, [x1,2834]
	strh	w2, [x0,122]
	ldrh	w2, [x1,2838]
	ldrh	w1, [x1,2836]
	strh	w2, [x0,120]
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
	stp	x23, x24, [sp,48]
	adrp	x20, .LC106
	adrp	x23, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	add	x25, x0, 16
	mov	x26, x23
	add	x20, x20, :lo12:.LC106
	mov	w24, -1
.L1244:
.L1247:
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
	ldrh	w1, [x0,3276]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w2, w1, w2
	strh	w2, [x19,6]
	strb	w21, [x19,4]
	beq	.L1245
	ldrb	w3, [x0,3244]
	mul	w3, w1, w3
	uxth	w3, w3
.L1245:
	add	x27, x26, :lo12:.LANCHOR0
	strh	w3, [x19,12]
	ubfiz	x28, x22, 1, 16
	ldrb	w2, [x27,3244]
	ldr	x0, [x27,3840]
	add	x0, x0, x3, uxth 2
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,72]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L1243
	mov	x0, x20
	mov	w1, w22
	bl	printk
	ldr	x0, [x27,72]
	strh	w24, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L1247
.L1243:
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
	ldrh	w0, [x0,1132]
	cmp	w0, 128
	bls	.L1250
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 96
	mov	w2, 76
	bl	printk
.L1250:
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
	ldrh	w6, [x2,3314]
	ldr	x4, [x2,2848]
	sub	w0, w0, w6
	ldrb	w8, [x2,3312]
	lsl	w0, w1, w0
	add	x1, x23, :lo12:.LANCHOR4
	ldrh	w7, [x4,698]
	sub	w0, w0, #1
	ldrh	w9, [x1,1132]
.L1251:
	cmp	w7, w3, uxth
	bls	.L1276
	add	x1, x4, x3, lsl 2
	mov	x2, 0
	ldr	w1, [x1,704]
	lsr	w1, w1, w6
	and	w1, w1, w0
	udiv	w1, w1, w8
	uxth	w1, w1
.L1252:
	cmp	w9, w2, uxth
	bls	.L1277
	add	x5, x4, x2, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L1253
	ldrh	w5, [x28,x2,lsl 1]
	add	w5, w5, 1
	strh	w5, [x28,x2,lsl 1]
.L1253:
	add	x2, x2, 1
	b	.L1252
.L1277:
	add	x3, x3, 1
	b	.L1251
.L1276:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w25, 0
	adrp	x24, .LC114
	mov	w19, w25
	adrp	x26, .LANCHOR2
	mov	w27, -1
	ldrb	w0, [x1,3244]
	add	x24, x24, :lo12:.LC114
	ldrh	w20, [x1,3276]
	mul	w20, w0, w20
	uxth	w20, w20
.L1256:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,1132]
	cmp	w0, w19
	bls	.L1278
	add	x0, x21, :lo12:.LANCHOR0
	sxtw	x22, w19
	add	x1, x22, 208
	ldr	x4, [x0,2848]
	ldrb	w2, [x0,3312]
	ldrh	w3, [x4,x1,lsl 1]
	ldrh	w1, [x4,692]
	sdiv	w1, w1, w2
	cmp	w1, w3
	bne	.L1257
	ldrb	w1, [x0,3244]
	ldrh	w0, [x0,3276]
	mul	w0, w1, w0
	strh	w0, [x28,w19,sxtw 1]
.L1257:
	ldrh	w0, [x28,x22,lsl 1]
	cmp	w20, w0
	bls	.L1258
	cmp	w0, wzr
	csel	w25, w25, w19, eq
	csel	w20, w20, w0, eq
.L1258:
	cbnz	w0, .L1260
	mov	w0, 65535
	cmp	w3, w0
	beq	.L1260
	ldr	w0, [x26,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1261
	ldrh	w4, [x4,688]
	mov	x0, x24
	mov	w1, w19
	mov	w2, 0
	bl	printk
.L1261:
	add	x1, x21, :lo12:.LANCHOR0
	add	x22, x22, 208
	str	x1, [x29,104]
	ldr	x0, [x1,2848]
	ldrh	w0, [x0,x22,lsl 1]
	bl	ftl_free_sblk
	ldr	x1, [x29,104]
	ldr	x0, [x1,2848]
	strh	w27, [x0,x22,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L1260:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1256
.L1278:
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
	tbz	x0, 12, .L1281
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 100
	add	x2, x2, 112
	bl	printk
.L1281:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	ldr	x0, [x19,192]
	bl	ftl_memset
	ldr	x1, [x19,192]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,192]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	add	x1, x19, 104
	bl	ftl_memcpy
	ldr	x0, [x19,192]
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
.L1286:
	cmp	w2, w3
	bls	.L1288
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1286
.L1288:
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
.L1291:
	cmp	w1, w2
	bls	.L1293
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L1291
.L1293:
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
	adrp	x0, .LANCHOR4+1136
	ldr	x0, [x0,#:lo12:.LANCHOR4+1136]
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
	adrp	x0, .LANCHOR4+1144
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
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
	adrp	x1, .LANCHOR4+1144
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR4+1144]
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
	adrp	x3, .LANCHOR4+1144
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1144]
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
	adrp	x3, .LANCHOR4+1144
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1144]
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
	adrp	x4, .LANCHOR4+1152
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1152]
	add	x4, x4, 4096
	cbnz	w2, .L1300
	add	x1, x4, x1
	b	.L1302
.L1300:
	add	x0, x4, x1
	mov	x1, x6
.L1302:
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
	adrp	x0, .LC116
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC116
	bl	printk
	adrp	x0, .LANCHOR4+1144
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
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
	adrp	x0, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1144]
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
	adrp	x1, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1144]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1144]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	adrp	x4, .LANCHOR4+1144
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1144]
	ldr	x4, [x4,48]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR4+1144
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1144]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1144]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1144]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR4+1144
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1144]
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
	adrp	x23, .LC117
	adrp	x21, .LC118
	adrp	x24, .LC1
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC117
	add	x21, x21, :lo12:.LC118
	add	x24, x24, :lo12:.LC1
.L1317:
	cmp	x19, x26
	beq	.L1325
	cbnz	w20, .L1318
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L1318:
	cmp	w25, 4
	mov	x0, x21
	bne	.L1319
	ldr	w1, [x22,x19,lsl 2]
	b	.L1324
.L1319:
	cmp	w25, 2
	bne	.L1321
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L1324
.L1321:
	ldrb	w1, [x22,x19]
.L1324:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L1322
	adrp	x1, .LC119
	mov	x0, x24
	add	x1, x1, :lo12:.LC119
	mov	w20, 0
	bl	printk
.L1322:
	add	x19, x19, 1
	b	.L1317
.L1325:
	adrp	x0, .LC1
	adrp	x1, .LC119
	add	x1, x1, :lo12:.LC119
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
	ldr	x27, [x0,192]
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
	bne	.L1327
	ldr	x1, [x29,136]
	mov	w0, -89
	strb	w0, [x1,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1382
.L1327:
	cmp	w25, 3
	bne	.L1329
	mov	x4, 0
.L1330:
	sub	w0, w4, #80
	strb	w0, [x26,x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L1330
	mov	w21, w4
	mov	w28, w4
	b	.L1328
.L1329:
	cmp	w25, 4
	bne	.L1331
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
	b	.L1328
.L1331:
	cmp	w25, 5
	bne	.L1332
	mov	w0, 56
	strb	w0, [x27,128]
	mov	w0, 57
	strb	w0, [x27,129]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x27,130]
	mov	w0, 59
	strb	w0, [x27,131]
	b	.L1416
.L1332:
	cmp	w25, 6
	bne	.L1333
	mov	w0, 14
	strb	w0, [x27,128]
	mov	w0, 15
	strb	w0, [x27,129]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x27,130]
	mov	w0, 17
	strb	w0, [x27,131]
	b	.L1416
.L1333:
	cmp	w25, 7
	bne	.L1382
	mov	x0, 0
.L1334:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1334
	mov	w0, -44
	mov	w21, 12
	strb	w0, [x27,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x27,137]
	b	.L1328
.L1382:
	mov	w21, 7
.L1416:
	mov	w28, 4
.L1328:
	sub	w0, w25, #1
	cmp	w0, 1
	bhi	.L1413
	ldr	x0, [x29,128]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w27, 55
	add	x24, x24, 408
.L1335:
	ldrb	w0, [x19,98]
	cmp	w0, w23
	bls	.L1342
	add	x0, x19, x23, sxtw
	ldr	x2, [x29,136]
	ldrb	w0, [x0,200]
	mov	x20, 160
	ldr	x22, [x19,16]
	mov	x25, 0
	ubfiz	x1, x0, 8, 8
	madd	x20, x0, x20, x2
	add	x22, x22, x1
	add	x20, x20, 32
.L1337:
	str	w27, [x22,2056]
	ldrb	w0, [x26,x25]
	str	w0, [x22,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x22,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	cmp	w28, w25, uxtb
	bhi	.L1337
	mov	x0, 0
.L1338:
	add	w2, w0, 8
	mov	x1, 0
.L1339:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w5, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1339
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1338
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1335
.L1413:
	sub	w0, w25, #3
	cmp	w0, 4
	bhi	.L1342
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
.L1343:
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,98]
	cmp	w1, w20
	bhi	.L1381
.L1342:
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
.L1381:
	add	x1, x0, x20, sxtw
	ldrb	w1, [x1,200]
	ldr	x0, [x0,16]
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
	bne	.L1344
	mov	w0, 64
	str	w1, [x23,2052]
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1417
.L1344:
	cmp	w19, 1
	bhi	.L1346
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1418
.L1346:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1417:
	str	w0, [x23,2052]
	mov	w0, 77
.L1418:
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
	bne	.L1347
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L1348
.L1347:
	str	wzr, [x23,2052]
.L1348:
	mov	w7, 2
	str	w7, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	str	x7, [x29,104]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w1, 16
	bls	.L1350
	ldr	x7, [x29,104]
	cmp	w25, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L1350:
	adrp	x0, .LANCHOR4
	mov	x7, 0
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1160]
.L1351:
	ldr	w8, [x23,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L1351
	cmp	w25, 7
	bne	.L1352
	mov	w1, 0
.L1354:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1353
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L1353
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1354
	b	.L1355
.L1353:
	cmp	w1, 7
	bne	.L1356
.L1355:
	adrp	x0, .LC120
	mov	w1, 0
	add	x0, x0, :lo12:.LC120
	bl	printk
.L1357:
	b	.L1357
.L1352:
	cmp	w25, 6
	bne	.L1356
	mov	x1, 0
.L1358:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L1356
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L1356
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1358
	adrp	x0, .LC120
	mov	w1, 0
	add	x0, x0, :lo12:.LC120
	bl	printk
.L1360:
	b	.L1360
.L1356:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x9, [x0,1160]
	mov	x0, 0
.L1361:
	ldr	w1, [x29,120]
	cmp	w1, w0
	ble	.L1420
	ldr	w1, [x23,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L1361
.L1420:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w8, w22
	mov	w7, 8
	ldr	x11, [x0,1160]
.L1364:
	mov	w0, 0
.L1363:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L1363
	ldr	w0, [x29,124]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L1364
	mov	x7, 0
	mov	w15, 1
.L1365:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w10, w1
	ldr	x14, [x0,1160]
.L1369:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L1367:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L1367
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L1369
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L1365
	add	x1, x26, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w8, w7
	ldr	x1, [x1,1160]
.L1372:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L1372
	cmp	w8, 7
	ble	.L1373
	adrp	x0, .LC121
	mov	w2, 1
	add	x0, x0, :lo12:.LC121
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC120
	mov	w1, 0
	add	x0, x0, :lo12:.LC120
	bl	printk
.L1374:
	b	.L1374
.L1373:
	cmp	w25, 6
	mov	w7, 4
	beq	.L1375
	cmp	w25, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L1375:
	mov	w8, 0
.L1376:
	mov	x1, 0
.L1377:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x24,w10,sxtw]
	bhi	.L1377
	add	w8, w8, 1
	ldr	x1, [x29,112]
	cmp	w8, w21
	add	w0, w0, w7
	add	x9, x9, x1
	blt	.L1376
	mov	w24, 255
	str	w24, [x23,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w0, 56
	bhi	.L1419
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
.L1419:
	str	w0, [x23,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1343
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
	bhi	.L1421
	bl	hynix_get_read_retry_default
.L1421:
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
	ldr	x0, [x20,16]
	bl	wait_for_nandc_xfer_completed
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	x23, x0
	strb	wzr, [x1,1168]
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L1424
	ldr	x24, [x20,16]
	ldr	w0, [x24,16]
	str	w0, [x29,64]
	ldr	w20, [x24,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1444
	adrp	x21, .LC125
	adrp	x23, .LC124
	add	x21, x21, :lo12:.LC125
	add	x23, x23, :lo12:.LC124
	b	.L1426
.L1444:
	adrp	x21, .LC123
	adrp	x22, .LC124
	mov	w20, 0
	add	x21, x21, :lo12:.LC123
	add	x22, x22, :lo12:.LC124
.L1425:
	ldr	w2, [x24,64]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 6
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1428
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1427
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1427
	ldr	w1, [x29,72]
	adrp	x0, .LC122
	add	x0, x0, :lo12:.LC122
	ubfx	x1, x1, 17, 1
	bl	printk
	b	.L1428
.L1427:
	ldr	w0, [x29,64]
	add	w20, w20, 1
	ubfx	x0, x0, 22, 6
	cmp	w20, w0, lsl 12
	bne	.L1425
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
	ldr	x1, [x1,16]
	bl	rknand_print_hex
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1425
	add	x0, x23, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,1168]
.L1428:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 144
	ldr	w0, [x20,32]
	cbz	w0, .L1432
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
	b	.L1472
.L1434:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,16]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1426
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x24,64]
	mov	x0, x21
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x22,16]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1426:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1434
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 144
	ldr	w0, [x20,32]
	cbz	w0, .L1432
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
	b	.L1471
.L1424:
	ldr	x23, [x20,16]
	ldr	w0, [x23,8]
	str	w0, [x29,64]
	ldr	w20, [x23,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1445
	adrp	x21, .LC125
	adrp	x24, .LC124
	add	x21, x21, :lo12:.LC125
	add	x24, x24, :lo12:.LC124
	b	.L1437
.L1445:
	adrp	x21, .LC123
	adrp	x22, .LC124
	mov	w20, 0
	add	x21, x21, :lo12:.LC123
	add	x22, x22, :lo12:.LC124
.L1436:
	ldr	w2, [x23,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1439
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1438
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1438
	ldr	w1, [x29,72]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	bl	printk
	b	.L1439
.L1438:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1436
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
	ldr	x1, [x1,16]
	bl	rknand_print_hex
	b	.L1436
.L1439:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 144
	ldr	w0, [x20,32]
	cbz	w0, .L1432
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
.L1472:
	mov	w2, 0
	b	.L1470
.L1443:
	add	x22, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x22,16]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1437
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x23,28]
	mov	x0, x21
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x22,16]
	mov	x0, x24
	mov	w2, 4
	mov	w3, 64
	bl	rknand_print_hex
.L1437:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1443
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 144
	ldr	w0, [x20,32]
	cbz	w0, .L1432
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
.L1471:
	mov	w2, 1
.L1470:
	bl	rknand_dma_unmap_single
.L1432:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,176]
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
	cbnz	w20, .L1503
	adrp	x19, .LANCHOR0
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L1475
	ldr	x5, [x3,16]
	lsr	w22, w22, 2
	mov	w4, 1
	mov	w3, w0
.L1476:
	cmp	w3, w22
	bcs	.L1512
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
	tbnz	x1, 2, .L1495
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1495
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1478
	ubfx	x1, x1, 3, 7
	b	.L1479
.L1478:
	ubfx	x1, x1, 19, 7
.L1479:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1477
.L1495:
	mov	w0, -1
.L1477:
	add	w3, w3, 1
	b	.L1476
.L1512:
	add	x22, x19, :lo12:.LANCHOR0
	cmp	w4, wzr
	ldr	x1, [x22,16]
	ldr	w20, [x1]
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1482
	adrp	x0, .LC127
	mov	w1, w20
	add	x0, x0, :lo12:.LC127
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x22,16]
	str	w20, [x0]
	mov	w0, -1
.L1482:
	tbz	x20, 13, .L1483
	adrp	x1, .LANCHOR4+1168
	ldrb	w1, [x1,#:lo12:.LANCHOR4+1168]
	cbz	w1, .L1483
	adrp	x0, .LC128
	mov	w1, w20
	add	x0, x0, :lo12:.LC128
	add	x19, x19, :lo12:.LANCHOR0
	bl	printk
	ldr	x0, [x19,16]
	mov	w1, 1
	str	w1, [x0,16]
	b	.L1484
.L1475:
	ldrb	w0, [x3,188]
	mov	w5, 128
	lsr	w6, w22, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w5, w0, w5, cc
	mov	w1, w20
	mov	w4, w20
.L1486:
	cmp	w4, w6
	add	w7, w1, w5
	bcs	.L1513
	ldr	x0, [x3,144]
	and	x1, x1, 4294967292
	ldr	w0, [x0,x1]
	ubfiz	x1, x4, 2, 30
	add	w4, w4, 1
	lsr	w8, w0, 16
	strb	w0, [x23,x1]
	lsr	w1, w0, 8
	strb	w1, [x23,w2,uxtw]
	add	w1, w2, 1
	lsr	w0, w0, 24
	strb	w8, [x23,x1]
	add	w1, w2, 2
	add	w2, w2, 4
	strb	w0, [x23,x1]
	mov	w1, w7
	b	.L1486
.L1513:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 0
	lsr	w22, w22, 2
	ldr	x6, [x0,16]
	mov	w0, w2
.L1488:
	cmp	w2, w22
	bcs	.L1514
	uxtw	x1, w2
	add	x1, x1, 8
	ldr	w1, [x6,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1498
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1498
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
	bls	.L1490
	ubfx	x3, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1511
.L1490:
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1511:
	orr	w1, w3, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1489
.L1498:
	mov	w0, -1
.L1489:
	add	w2, w2, 1
	b	.L1488
.L1514:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,16]
	str	wzr, [x1,16]
	ldr	w20, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1483
	adrp	x0, .LC129
	mov	w1, w20
	add	x0, x0, :lo12:.LC129
	orr	w20, w20, 131072
	bl	printk
	ldr	x0, [x19,16]
	str	w20, [x0]
	b	.L1484
.L1483:
	cmn	w0, #1
	beq	.L1503
	ldr	w1, [x23]
	cmn	w1, #1
	bne	.L1503
	ldr	w1, [x23,4]
	cmn	w1, #1
	bne	.L1503
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1503
.L1484:
	mov	w0, -1
.L1503:
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
	ldr	x19, [x0,16]
	adrp	x0, .LANCHOR2
	mov	x25, x3
	mov	w27, w4
	and	w20, w1, 2097151
	ubfx	x24, x23, 24, 2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1516
	adrp	x0, .LC130
	mov	w1, w21
	add	x0, x0, :lo12:.LC130
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L1516:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	cbnz	w24, .L1517
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1518
.L1517:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,88]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1519
	sxtw	x0, w21
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w24, [x0,8]
	b	.L1518
.L1519:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1518:
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
	cbz	w24, .L1520
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,88]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1520
	add	w20, w20, w20, lsl 1
	sub	w0, w20, #1
	add	w0, w0, w24
	b	.L1531
.L1520:
	mov	w0, w20
.L1531:
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
	add	x19, x19, :lo12:.LANCHOR0
	str	w5, [x29,120]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w25, w0
	mov	w22, w2
	ldrb	w20, [x19,188]
	mov	x26, x3
	mov	x27, x4
	mov	w21, 0
	adrp	x24, .LANCHOR4
	add	w20, w20, w20, lsl 1
	asr	w0, w20, 2
	str	w0, [x29,124]
	mov	w20, -1
	bl	nandc_wait_flash_ready
	ldr	x23, [x19,16]
	uxtb	x19, w25
	lsl	x0, x19, 8
	str	x0, [x29,112]
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	str	x0, [x29,104]
.L1533:
	add	x0, x24, :lo12:.LANCHOR4
	ldrb	w0, [x0,1184]
	cmp	w21, w0
	bcs	.L1537
	ldr	x0, [x29,112]
	add	w28, w21, 1
	add	x1, x23, x0
	mov	w0, 239
	str	x1, [x29,96]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	w0, 200
	bl	timer_delay_ns
	ldr	x1, [x29,96]
	mov	w0, w25
	ldr	w4, [x29,120]
	mov	x2, x26
	mov	x3, x27
	str	w28, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	mov	w1, w22
	bl	flash_read_page
	mov	w6, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1534
	ldr	x0, [x29,104]
	mov	w1, w21
	mov	w2, w22
	mov	w3, w21
	mov	w4, w20
	str	x6, [x29,96]
	bl	printk
	ldr	x6, [x29,96]
.L1534:
	cmn	w6, #1
	beq	.L1535
	adrp	x0, .LANCHOR4
	cmn	w20, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w20, w20, w6, ne
	ldr	x26, [x0,1160]
	ldr	x27, [x0,1176]
	ldr	w0, [x29,124]
	cmp	w6, w0
	bcc	.L1542
.L1535:
	mov	w21, w28
	b	.L1533
.L1542:
	mov	w20, w6
.L1537:
	add	x19, x23, x19, lsl 8
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
	cmp	w20, w0
	bcc	.L1539
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L1539:
	cmn	w20, #1
	beq	.L1543
	cmp	w20, 256
	bne	.L1540
.L1543:
	adrp	x0, .LC132
	mov	w1, w21
	add	x0, x0, :lo12:.LC132
	mov	w2, w22
	mov	w3, w21
	mov	w4, w20
	bl	printk
.L1540:
	mov	w0, w20
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
	stp	x29, x30, [sp, -144]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w19, w1
	mov	w25, w2
	str	w0, [x29,128]
	mov	x27, x3
	mov	x28, x4
	str	w5, [x29,124]
	adrp	x20, .LANCHOR0
	bl	nandc_wait_flash_ready
	add	x0, x20, :lo12:.LANCHOR0
	ldrsw	x22, [x29,128]
	mov	w1, 46
	mov	w2, 56
	add	x22, x22, 8
	mov	w21, 10
	ldr	x26, [x0,16]
	lsl	x22, x22, 8
	ldrb	w0, [x0,189]
	add	x24, x26, x22
	cmp	w0, 36
	csel	w1, w2, w1, ne
	cmp	w0, 36
	mov	w0, 26
	mov	w23, w1
	csel	w21, w21, w0, ne
	cbnz	w19, .L1557
	adrp	x0, .LC133
	str	x22, [x29,112]
	add	x0, x0, :lo12:.LC133
	mov	w19, -1
	mov	w23, 1
	str	x0, [x29,104]
	adrp	x22, .LANCHOR4
.L1564:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1558
	mov	w1, w23
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x26, x0
	str	w1, [x0,8]
	b	.L1559
.L1558:
	mov	w1, w23
	bl	toshiba_3d_set_slc_rr_para
.L1559:
	ldr	w4, [x29,124]
	mov	w1, w25
	ldrb	w0, [x29,128]
	mov	x2, x27
	mov	x3, x28
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1560
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w23
	mov	w2, w25
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1560:
	cmn	w4, #1
	beq	.L1561
	add	x0, x22, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1160]
	ldr	x28, [x0,1176]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,188]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1580
.L1561:
	add	w23, w23, 1
	cmp	w23, w21
	bne	.L1564
	b	.L1563
.L1580:
	mov	w21, w23
	mov	w19, w4
.L1563:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1565
	mov	w1, 0
	mov	w2, w1
	b	.L1603
.L1565:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1566
.L1557:
	adrp	x0, .LC134
	orr	w25, w25, w19, lsl 24
	add	x0, x0, :lo12:.LC134
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,112]
.L1573:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1567
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x26, x22
	mov	w1, 93
	b	.L1602
.L1567:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x26, x22
	mov	w1, 38
.L1602:
	str	w1, [x0,8]
	mov	x2, x27
	mov	w1, w25
	mov	x3, x28
	ldr	w4, [x29,124]
	ldrb	w0, [x29,128]
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1569
	ldr	x0, [x29,112]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w25
	str	x4, [x29,104]
	bl	printk
	ldr	x4, [x29,104]
.L1569:
	cmn	w4, #1
	beq	.L1570
	adrp	x0, .LANCHOR4
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1160]
	ldr	x28, [x0,1176]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,188]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1581
.L1570:
	add	w21, w21, 1
	cmp	w21, w23
	bne	.L1573
	b	.L1572
.L1581:
	mov	w19, w4
.L1572:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1574
	mov	w1, 0
	mov	w2, 1
.L1603:
	bl	toshiba_tlc_set_rr_para
	b	.L1566
.L1574:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1566:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 36
	bne	.L1575
	ldr	x0, [x29,128]
	ubfiz	x23, x0, 8, 8
	mov	w0, 85
	add	x26, x26, x23
	str	w0, [x26,2056]
	mov	w0, 255
	str	wzr, [x26,2052]
	str	wzr, [x26,2048]
	str	w0, [x26,2056]
.L1575:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,188]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1576
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1576:
	cmn	w19, #1
	beq	.L1582
	cmp	w19, 256
	bne	.L1577
.L1582:
	adrp	x0, .LC135
	mov	w1, w21
	add	x0, x0, :lo12:.LC135
	mov	w2, w25
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1577:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	toshiba_3d_read_retrial, .-toshiba_3d_read_retrial
	.align	2
	.global	toshiba_read_retrial
	.type	toshiba_read_retrial, %function
toshiba_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	uxtb	w23, w0
	str	w2, [x29,124]
	stp	x25, x26, [sp,64]
	mov	x27, x3
	mov	x28, x4
	str	w5, [x29,120]
	adrp	x19, .LANCHOR0
	bl	nandc_wait_flash_ready
	mov	w24, 0
	add	x1, x19, :lo12:.LANCHOR0
	sxtw	x0, w23
	add	x21, x0, 8
	str	x0, [x29,112]
	ldrb	w0, [x1,189]
	ldr	x22, [x1,16]
	sub	w0, w0, #67
	add	x21, x22, x21, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1605
	ldrb	w0, [x1,136]
	cbz	w0, .L1606
	mov	w0, 1
	mov	w24, 1
	bl	nandc_set_if_mode
.L1606:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1605:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1607:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1184]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1638
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,189]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1608
	bl	sandisk_set_rr_para
	b	.L1609
.L1608:
	bl	toshiba_set_rr_para
.L1609:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	cmp	w0, 34
	bne	.L1610
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1184]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1610
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1610:
	ldr	x0, [x29,96]
	mov	w1, 38
	ldr	w4, [x29,120]
	mov	x2, x27
	add	x0, x22, x0
	mov	x3, x28
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	mov	w0, w23
	ldr	w1, [x29,124]
	bl	flash_read_page
	cmn	w0, #1
	mov	w26, w0
	beq	.L1613
	cmn	w25, #1
	csel	w25, w25, w0, ne
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x27, [x0,1160]
	ldr	x28, [x0,1176]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,188]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1615
.L1613:
	add	w20, w20, 1
	b	.L1607
.L1638:
	mov	w26, w25
.L1615:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,189]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1617
	bl	sandisk_set_rr_para
	b	.L1618
.L1617:
	bl	toshiba_set_rr_para
.L1618:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,188]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1619
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L1619:
	cmn	w26, #1
	beq	.L1625
	cmp	w26, 256
	bne	.L1620
.L1625:
	adrp	x0, .LC135
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC135
	mov	w1, w20
	mov	w3, w20
	mov	w4, w26
	bl	printk
.L1620:
	bl	nandc_wait_flash_ready
	cbz	w24, .L1622
	mov	w0, 4
	bl	nandc_set_if_mode
.L1622:
	mov	w0, w26
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
	str	w5, [x29,124]
	mov	w23, w2
	ldr	x5, [x0,192]
	mov	x25, x3
	mov	x26, x4
	mov	x22, x21
	add	x27, x5, 112
	mov	w19, -1
	add	x0, x27, x21
	ldrb	w24, [x27,2]
	ldrb	w20, [x0,8]
	bl	nandc_wait_flash_ready
	mov	w6, 0
	adrp	x7, .LANCHOR4
.L1640:
	cmp	w6, w24
	bcs	.L1644
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
	beq	.L1642
	add	x1, x7, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1160]
	ldr	x26, [x1,1176]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,188]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1649
.L1642:
	add	w6, w6, 1
	b	.L1640
.L1649:
	mov	w19, w0
.L1644:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,188]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1646
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1646:
	cmn	w19, #1
	beq	.L1650
	cmp	w19, 256
	bne	.L1647
.L1650:
	adrp	x0, .LC136
	mov	w1, w6
	add	x0, x0, :lo12:.LC136
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1647:
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
	.global	flash_ddr_tunning_read
	.type	flash_ddr_tunning_read, %function
flash_ddr_tunning_read:
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
	adrp	x0, .LC137
	mov	w22, w26
	add	x0, x0, :lo12:.LC137
	mov	w28, w26
	mov	w27, w26
	mov	w25, 1024
	mov	w21, 6
	mov	w19, -1
	str	x0, [x29,104]
.L1665:
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
	tbz	x0, 4, .L1660
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w20
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1660:
	add	w0, w25, 1
	cmp	w4, w0
	bhi	.L1661
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x23, [x0,1160]
	ldr	x24, [x0,1176]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,188]
	cmp	w4, w0, lsr 2
	bcs	.L1670
	add	w27, w27, 1
	cmp	w27, 7
	bls	.L1670
	sub	w26, w21, w27
	mov	w25, w4
	mov	w19, 0
	b	.L1663
.L1661:
	cmp	w28, w27
	bcs	.L1671
	cmp	w27, 7
	sub	w22, w26, w27
	bhi	.L1664
	mov	w28, w27
	b	.L1671
.L1670:
	mov	w26, w21
	mov	w25, w4
	mov	w19, 0
	b	.L1662
.L1671:
	mov	w27, 0
.L1662:
	add	w21, w21, 2
	cmp	w21, 50
	bne	.L1665
.L1663:
	cmp	w28, w27
	csel	w26, w26, w22, ls
.L1664:
	cbz	w26, .L1666
	adrp	x0, .LANCHOR0+188
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+188]
	udiv	w0, w0, w1
	cmp	w25, w0
	bcs	.L1666
	adrp	x0, .LC138
	mov	w1, w26
	add	x0, x0, :lo12:.LC138
	bl	printk
	mov	w0, w26
	b	.L1679
.L1666:
	ldrb	w0, [x29,120]
.L1679:
	bl	nandc_set_ddr_para
	cbz	w19, .L1668
	adrp	x0, .LC139
	ldr	w1, [x29,124]
	add	x0, x0, :lo12:.LC139
	mov	w2, w20
	mov	w25, w19
	bl	printk
.L1668:
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	flash_ddr_tunning_read, .-flash_ddr_tunning_read
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
	ldrb	w0, [x0,98]
	mov	x24, x3
	mov	w23, w4
	ubfx	x26, x19, 24, 2
	cmp	w0, w21
	bhi	.L1681
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 136
	mov	w2, 378
	bl	printk
.L1681:
	add	x0, x20, :lo12:.LANCHOR0
	add	x21, x0, x21, sxtw
	adrp	x0, .LANCHOR2
	ldrb	w22, [x21,200]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L1682
	adrp	x0, .LC140
	mov	w1, w22
	add	x0, x0, :lo12:.LC140
	mov	w2, w19
	bl	printk
.L1682:
	cbnz	w26, .L1683
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,97]
	cbz	w1, .L1684
	ldrb	w0, [x0,208]
	cbz	w0, .L1683
.L1684:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,210]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,208]
	sub	w19, w19, w0
	cbz	w2, .L1685
	add	w19, w0, w19, lsl 1
	b	.L1683
.L1685:
	add	x1, x1, 212
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1683:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w21, w0
	bne	.L1692
	add	x26, x20, :lo12:.LANCHOR0
	ldrb	w27, [x26,184]
	cbnz	w27, .L1687
.L1690:
	adrp	x0, .LANCHOR4+1192
	ldr	x6, [x0,#:lo12:.LANCHOR4+1192]
	cbnz	x6, .L1688
	b	.L1689
.L1687:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	strb	wzr, [x26,184]
	bl	flash_read_page
	strb	w27, [x26,184]
	cmn	w0, #1
	beq	.L1690
	b	.L1708
.L1688:
	mov	w0, w22
	mov	w1, 0
	mov	w2, w19
	mov	x3, x25
	mov	x4, x24
	mov	w5, w23
	blr	x6
	cmn	w0, #1
	bne	.L1708
.L1689:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,184]
	bl	printk
	ldrb	w0, [x20,136]
	cbz	w0, .L1692
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1708:
	mov	w21, w0
.L1692:
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
	adrp	x0, .LANCHOR4+1060
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x24, x2
	ldrh	w19, [x0,#:lo12:.LANCHOR4+1060]
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
	bne	.L1710
	mov	w28, 0
	mov	w5, 2
.L1711:
	cmp	w28, w19
	bgt	.L1710
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
	bne	.L1712
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1711
.L1712:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L1711
.L1710:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1715
	adrp	x0, .LC142
	ldr	w3, [x21]
	add	x0, x0, :lo12:.LC142
	mov	w1, w22
	mov	w2, w19
	bl	printk
.L1715:
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
	adrp	x0, .LANCHOR0+3314
	stp	x29, x30, [sp, -16]!
	mov	w5, 21
	mov	x8, x1
	add	x29, sp, 0
	ldrh	w0, [x0,#:lo12:.LANCHOR0+3314]
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
	tbz	x0, 12, .L1721
	add	x4, x20, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR3
	adrp	x0, .LC143
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC143
	mov	w1, 49
	ldr	x4, [x4,192]
	add	x2, x2, 160
	mov	w3, 2048
	bl	printk
.L1721:
	mov	w22, 4
	adrp	x24, .LANCHOR4
.L1724:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x24, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w4, 4
	adrp	x23, .LANCHOR4
	ldrh	w1, [x19,210]
	ldr	x2, [x19,192]
	ldr	x3, [x21,1200]
	mul	w1, w22, w1
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1722
	ldr	x2, [x19,192]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L1723
.L1722:
	add	w22, w22, 1
	cmp	w22, 16
	bne	.L1724
.L1748:
	mov	w0, -1
	b	.L1747
.L1723:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,210]
	strb	w1, [x21,1208]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,1200]
	strb	w0, [x21,1209]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1726
	ldr	x0, [x19,192]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1726
	ldr	w21, [x0,4]
.L1726:
	add	x19, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,1209]
	ldrh	w1, [x22,210]
	ldr	x3, [x19,1200]
	mul	w1, w2, w1
	ldr	x2, [x22,192]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1727
	ldr	x0, [x22,192]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1727
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L1727
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,1208]
	strb	w0, [x19,1209]
.L1727:
	add	x21, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x21,1208]
	ldr	x3, [x21,1200]
	ldr	x2, [x22,192]
	bl	flash_get_last_written_page
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x21,1210]
	ldrb	w0, [x21,1208]
	ldrh	w21, [x22,210]
	mul	w21, w0, w21
.L1728:
	tbnz	w19, #31, .L1749
	add	x22, x20, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w0, 0
	add	w1, w19, w21
	mov	w4, 4
	ldr	x2, [x22,192]
	ldr	x3, [x3,1200]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1729
	ldr	x0, [x22,192]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	bne	.L1729
.L1731:
	mov	w0, 0
	b	.L1747
.L1729:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1728
.L1749:
	cmn	w19, #1
	bne	.L1731
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC144
	add	x0, x0, :lo12:.LC144
	ldr	x1, [x20,192]
	ldr	w1, [x1]
	bl	printk
	b	.L1748
.L1747:
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
	ldrb	w0, [x21,764]
	add	x20, x20, :lo12:.LANCHOR4
	strb	w22, [x21,136]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,764]
	bl	nandc_set_if_mode
	ldr	x2, [x20,1216]
	mov	w1, w23
	ldr	x3, [x20,1224]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tunning_read
	ldr	x2, [x20,1216]
	mov	w0, w24
	ldr	x3, [x20,1224]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1751
	ldrb	w0, [x21,764]
	tbz	x0, 0, .L1751
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,136]
	b	.L1752
.L1751:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,136]
.L1752:
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
	stp	x27, x28, [sp,80]
	mov	w27, w1
	adrp	x1, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x1, :lo12:.LANCHOR0
	str	x1, [x29,120]
	stp	x25, x26, [sp,64]
	mov	w26, w0
	ldr	x0, [x19,88]
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
	ldrb	w2, [x19,184]
	str	w2, [x29,136]
	uxth	w0, w0
	strb	wzr, [x19,184]
	udiv	w2, w26, w0
	msub	w0, w2, w0, w26
	str	w0, [x29,172]
	sub	w0, w26, w0
	str	w0, [x29,160]
	ldr	w0, [x29,172]
	and	w0, w0, 3
	str	w0, [x29,168]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	str	x0, [x29,112]
.L1757:
	cmp	w22, w27
	bcs	.L1794
	ldr	w0, [x29,168]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	sub	w0, w23, w0
	uxth	w0, w0
	str	w0, [x29,144]
	ldr	w0, [x29,172]
	ldrb	w1, [x1,1232]
	add	w20, w22, w0
	add	x0, x19, 212
	udiv	w20, w20, w23
	and	w20, w20, 65535
	ldrh	w0, [x0,w20,sxtw 1]
	cbnz	w1, .L1758
	mov	w20, w0
	b	.L1759
.L1758:
	ldrb	w1, [x19,208]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L1759:
	ldr	w1, [x29,160]
	ldr	w0, [x29,164]
	ldrb	w28, [x19,96]
	madd	w0, w20, w0, w1
	ldr	w1, [x29,168]
	str	w0, [x29,140]
	add	w24, w0, w1
	ldr	x0, [x19,88]
	ldrb	w0, [x0,9]
	udiv	w24, w24, w0
	ldrb	w0, [x19,188]
	str	w0, [x29,132]
	mov	w0, w28
	bl	nandc_bch_sel
.L1760:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L1767
	ldrb	w5, [x19,184]
	cbnz	w5, .L1762
.L1765:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x7, [x0,1192]
	cbnz	x7, .L1763
	b	.L1764
.L1762:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	str	x5, [x29,104]
	strb	wzr, [x19,184]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x5, [x29,104]
	strb	w5, [x19,184]
	beq	.L1765
	b	.L1793
.L1763:
	mov	w0, 0
	mov	w2, w24
	mov	w1, w0
	mov	x3, x21
	add	x4, x29, 176
	mov	w5, w23
	blr	x7
	cmn	w0, #1
	bne	.L1793
.L1764:
	ldrb	w0, [x19,136]
	cbz	w0, .L1767
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1793:
	mov	w20, w0
.L1767:
	cmn	w20, #1
	bne	.L1768
	cmp	w28, 16
	beq	.L1768
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L1760
.L1768:
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
	beq	.L1770
	cbnz	w25, .L1770
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L1770
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L1771
.L1770:
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
.L1771:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L1757
.L1794:
	ldr	x0, [x29,120]
	ldrb	w1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,184]
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
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	and	w19, w0, 2097151
	stp	x23, x24, [sp,48]
	mov	w24, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	ubfx	x23, x24, 21, 3
	mov	x25, x1
	ldrb	w0, [x0,98]
	mov	x26, x2
	ubfx	x22, x24, 24, 2
	cmp	w0, w23
	bhi	.L1796
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 184
	mov	w2, 789
	bl	printk
.L1796:
	add	x21, x20, :lo12:.LANCHOR0
	add	x23, x21, x23, sxtw
	ldrb	w23, [x23,200]
	mov	w0, w23
	ldr	x27, [x21,16]
	bl	nandc_cs
	cbnz	w22, .L1797
	ldrb	w0, [x21,97]
	cbz	w0, .L1798
	ldrb	w0, [x21,208]
	cbz	w0, .L1797
.L1798:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,210]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,208]
	sub	w19, w19, w0
	cbz	w2, .L1799
	add	w19, w0, w19, lsl 1
	b	.L1797
.L1799:
	add	x1, x1, 212
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1797:
	ubfiz	x0, x23, 8, 8
	add	x5, x27, x0
	mov	w0, 5
	str	w0, [x5,2056]
	and	w0, w19, 255
	str	wzr, [x5,2052]
	str	wzr, [x5,2052]
	str	w0, [x5,2052]
	lsr	w0, w19, 8
	str	w0, [x5,2052]
	lsr	w0, w19, 16
	str	w0, [x5,2052]
	mov	w0, 224
	str	w0, [x5,2056]
	cbz	w22, .L1800
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,88]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1800
	sub	w0, w22, #1
	add	w1, w19, w19, lsl 1
	add	w0, w0, w1
	b	.L1838
.L1800:
	mov	w0, w19
.L1838:
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
	bne	.L1802
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w5, [x28,184]
	cbz	w5, .L1803
	ldrb	w4, [x27,9]
	mov	w0, w23
	orr	w1, w19, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	str	x5, [x29,104]
	strb	wzr, [x28,184]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,184]
	cbnz	w22, .L1804
.L1811:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,97]
	cbz	w2, .L1804
	ldrb	w1, [x1,188]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L1804
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w1, [x21,395]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 3
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L1821
.L1804:
	cmn	w0, #1
	bne	.L1821
.L1812:
	adrp	x0, .LANCHOR4+1192
	ldr	x6, [x0,#:lo12:.LANCHOR4+1192]
	cbnz	x6, .L1807
.L1810:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC146
	add	x0, x0, :lo12:.LC146
	mov	w1, 0
	mov	w2, w24
	mov	w3, -1
	ldrb	w4, [x20,184]
	bl	printk
	ldrb	w0, [x20,136]
	cbnz	w0, .L1808
	mov	w0, -1
	b	.L1821
.L1807:
	add	x5, x21, :lo12:.LANCHOR2
	mov	w0, w23
	mov	w1, w22
	mov	w2, w19
	mov	x3, x25
	mov	x4, x26
	ldrb	w5, [x5,385]
	blr	x6
	cmn	w0, #1
	bne	.L1821
	b	.L1810
.L1808:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w19, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x21,385]
	bl	flash_ddr_tunning_read
	b	.L1821
.L1802:
	cbz	w22, .L1811
	b	.L1821
.L1803:
	cbz	w22, .L1811
	b	.L1812
.L1821:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldrb	w20, [x0,3861]
	cmp	w20, 255
	beq	.L1875
	add	x0, x0, 768
	mov	w1, 0
	add	x0, x0, x20, lsl 6
	ldrb	w2, [x0,58]
	ldr	w21, [x0,40]
	sub	w3, w2, #1
	cmp	w3, 9
	bhi	.L1841
	adrp	x0, .L1843
	add	x0, x0, :lo12:.L1843
	ldrb	w0, [x0,w3,uxtw]
	adr	x1, .Lrtx1843
	add	x0, x1, w0, sxtb #2
	br	x0
.Lrtx1843:
	.section	.rodata
	.align	0
	.align	2
.L1843:
	.byte	(.L1842 - .Lrtx1843) / 4
	.byte	(.L1842 - .Lrtx1843) / 4
	.byte	(.L1844 - .Lrtx1843) / 4
	.byte	(.L1844 - .Lrtx1843) / 4
	.byte	(.L1844 - .Lrtx1843) / 4
	.byte	(.L1845 - .Lrtx1843) / 4
	.byte	(.L1846 - .Lrtx1843) / 4
	.byte	(.L1847 - .Lrtx1843) / 4
	.byte	(.L1844 - .Lrtx1843) / 4
	.byte	(.L1847 - .Lrtx1843) / 4
	.text
.L1842:
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1875
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w21
	add	x19, x19, 768
	add	x20, x19, x20, lsl 6
	mov	x2, x20
	ldr	x3, [x20,8]
	ldr	x1, [x2,16]!
	cmp	x1, xzr
	ldr	x2, [x2,8]
	csel	x1, x3, x1, eq
	bl	flash_complete_page_read
	str	w0, [x20,52]
	mov	w0, 12
	strb	w0, [x20,58]
	ldrb	w0, [x20,2]
	orr	w0, w0, 8
	strb	w0, [x20,2]
	b	.L1875
.L1844:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L1875
	mov	w1, 5
	add	x19, x19, :lo12:.LANCHOR0
	ands	w1, w21, w1
	beq	.L1850
	add	x19, x19, 768
	mov	w0, 11
	add	x20, x19, x20, lsl 6
	mov	w4, 11
	strb	w0, [x20,58]
	adrp	x0, .LC147
	ldrb	w1, [x20,1]
	add	x0, x0, :lo12:.LC147
	ldr	w2, [x20,52]
	ldr	w3, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
	b	.L1876
.L1850:
	add	x20, x19, x20, lsl 6
	b	.L1857
.L1847:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 3
	cmp	w2, 10
	mov	w1, 9
	add	x22, x22, 768
	csel	w1, w0, w1, ne
	ubfx	x3, x21, 21, 3
	add	x0, x22, x20, lsl 6
.L1852:
	ldrb	w23, [x0]
	cmp	w23, 255
	beq	.L1877
	add	x0, x22, x23, lsl 6
	ldrb	w2, [x0,58]
	cmp	w2, w1
	bne	.L1852
	ldr	w2, [x0,40]
	ubfx	x2, x2, 21, 3
	cmp	w3, w2
	bne	.L1852
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	and	w0, w0, 64
	tbz	x21, 6, .L1860
	ands	w0, w21, 15
	add	x22, x22, x20, lsl 6
	beq	.L1855
	adrp	x0, .LC147
	ldrb	w1, [x22,1]
	ldr	w2, [x22,52]
	add	x0, x0, :lo12:.LC147
	ldr	w3, [x22,40]
	mov	w4, 11
	bl	printk
	mov	w0, 11
	strb	w0, [x22,58]
	mov	w0, -1
	str	w0, [x22,52]
.L1854:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 768
	add	x20, x19, x20, lsl 6
	add	x23, x19, x23, lsl 6
	ldrb	w0, [x20,58]
	strb	w0, [x23,58]
	ldr	w0, [x20,52]
	str	w0, [x23,52]
.L1876:
	mov	w1, w21
	b	.L1841
.L1855:
	mov	w1, 12
	str	w0, [x22,52]
	strb	w1, [x22,58]
.L1860:
	mov	w21, w0
	b	.L1854
.L1877:
	mov	w1, -1
	b	.L1841
.L1845:
	mov	w1, 32
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 5, .L1875
	ands	w1, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	beq	.L1857
	mov	w1, 11
	strb	w1, [x20,826]
	mov	w1, -1
	str	w1, [x20,820]
	mov	w1, w0
	b	.L1841
.L1857:
	mov	w0, 12
	str	w1, [x20,820]
	strb	w0, [x20,826]
	b	.L1841
.L1846:
	mov	w1, 64
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1875
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	str	w0, [x20,820]
	mov	w0, 6
	strb	w0, [x20,826]
.L1875:
	mov	w1, 0
.L1841:
	mov	w0, w1
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
.L1879:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3861]
	cmp	w0, 255
	beq	.L1881
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1879
.L1881:
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
	.global	ftl_open_sblk_read_test
	.type	ftl_open_sblk_read_test, %function
ftl_open_sblk_read_test:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w0
	add	x1, x29, 80
	stp	x21, x22, [sp,32]
	mov	w0, w19
	adrp	x21, .LANCHOR4
	adrp	x22, .LANCHOR5
	add	x21, x21, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR5
	str	x23, [sp,48]
	add	x21, x21, 1280
	bl	ftl_get_blk_list_in_sblk
	add	x22, x22, 256
	strh	w19, [x29,64]
	strb	w0, [x29,73]
	mov	w19, 0
.L1885:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x23, x0
	ldrh	w1, [x1,3276]
	cmp	w1, w19
	bls	.L1884
	mov	w20, 0
.L1891:
	add	x3, x23, :lo12:.LANCHOR0
	ldrb	w0, [x3,3244]
	cmp	w0, w20
	bls	.L1897
	add	x0, x29, 64
	add	x0, x0, x20, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1887
	ldrh	w1, [x3,3318]
	mov	x2, x22
	ldrb	w3, [x3,2816]
	madd	w0, w0, w1, w19
	mov	x1, x21
	bl	ftl_read_ppa_page
	cmp	w0, 512
	beq	.L1884
.L1887:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1891
.L1897:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1885
.L1884:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_open_sblk_read_test, .-ftl_open_sblk_read_test
	.align	2
	.global	sblk_read_page
	.type	sblk_read_page, %function
sblk_read_page:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	mov	w24, 1
	stp	x21, x22, [sp,32]
	add	x23, x23, :lo12:.LANCHOR0
	uxtb	w21, w1
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x20, x0
	mov	x19, x0
	mov	w22, w21
	mov	w25, -1
	add	x26, x23, 3861
.L1899:
	cbz	w22, .L1911
	ldrb	w27, [x19]
	ldr	w28, [x19,40]
.L1900:
	mov	w0, w28
	mov	w1, 0
	bl	queue_lun_state
	mov	w2, w0
	cbz	w0, .L1912
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1900
.L1912:
	ldr	w1, [x19,40]
	mov	w0, 48
	str	x2, [x29,104]
	bl	flash_start_page_read
	strb	w24, [x19,58]
	ldr	x2, [x29,104]
	mov	x0, x26
	strb	w2, [x19,59]
	mov	x1, x19
	strb	w25, [x19]
	bl	buf_add_tail
	subs	w22, w22, #1
	beq	.L1911
	add	x19, x23, 768
	sbfiz	x27, x27, 6, 32
	add	x19, x19, x27
	b	.L1899
.L1911:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 768
.L1904:
	cbz	w21, .L1913
	ldrb	w0, [x20,58]
	cmp	w0, 12
	bne	.L1905
	ldrb	w20, [x20]
	sub	w21, w21, #1
	add	x20, x19, x20, lsl 6
	b	.L1904
.L1905:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1904
.L1913:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	ldr	x0, [x20,2896]
	ldr	x23, [x20,80]
	add	x20, x20, 2888
	cbnz	x0, .L1915
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L1915:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x20, [x0,2896]
	cbnz	x20, .L1916
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 216
	mov	w2, 348
	bl	printk
.L1916:
	mov	w24, 0
	add	x25, x23, 80
	add	x21, x19, :lo12:.LANCHOR0
	adrp	x26, .LANCHOR4
.L1917:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L1938
	mov	w22, 1
	sxtw	x27, w24
.L1926:
	add	x0, x21, 2888
	ldrh	w1, [x0,20]
	cmp	w22, w1
	bgt	.L1939
	add	x1, x27, 8
	ldrh	w2, [x21,3318]
	ldrh	w0, [x0,16]
	ldrh	w1, [x25,x1,lsl 1]
	mul	w1, w1, w2
	ldrb	w2, [x21,3225]
	cmp	w2, 3
	bne	.L1918
	add	w1, w1, w0
	orr	w1, w1, w22, lsl 24
	b	.L1937
.L1918:
	cmp	w2, 2
	bne	.L1920
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L1937
.L1920:
	add	w1, w1, w0
.L1937:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L1921
	add	x3, x26, :lo12:.LANCHOR4
	ldrh	w0, [x21,2910]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,1104]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L1921
	ldr	x3, [x3,1112]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L1922
.L1921:
	add	x0, x26, :lo12:.LANCHOR4
	ldrh	w4, [x21,2910]
	lsl	x4, x4, 2
	ldr	x1, [x0,1104]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L1922
	adrp	x1, .LANCHOR2
	ldr	w1, [x1,#:lo12:.LANCHOR2]
	tbz	x1, 10, .L1923
	ldr	x7, [x20,24]
	ldr	x5, [x0,1112]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L1923:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23,80]
	ldr	x0, [x19,72]
	strh	wzr, [x0,x1,lsl 1]
	mov	w0, -1
	b	.L1924
.L1922:
	ldrh	w0, [x21,2910]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2910]
	b	.L1926
.L1939:
	add	w24, w24, 1
	b	.L1917
.L1938:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2904]
	add	w0, w0, 1
	strh	w0, [x19,2904]
	mov	w0, 0
.L1924:
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
	adrp	x0, .LANCHOR0+16
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x19, [x0,#:lo12:.LANCHOR0+16]
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
	tbz	x0, 4, .L1941
	adrp	x0, .LC149
	mov	w2, w27
	add	x0, x0, :lo12:.LC149
	bl	printk
.L1941:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	hynix_reconfig_rr_para
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w27, .L1942
	bl	zftl_flash_enter_slc_mode
	b	.L1943
.L1942:
	bl	zftl_flash_exit_slc_mode
.L1943:
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
	tbz	x21, 2, .L1944
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L1944
	adrp	x0, .LC150
	mov	w1, w23
	add	x0, x0, :lo12:.LC150
	mov	w2, w19
	bl	printk
.L1944:
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
	cbnz	w22, .L1955
	adrp	x0, .LANCHOR5+512
	ldrb	w1, [x0,#:lo12:.LANCHOR5+512]
	mov	w0, w22
	cmp	w1, w20
	bhi	.L1956
.L1955:
	adrp	x19, .LANCHOR4
	mov	w1, 165
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 32
	ldr	x0, [x19,1224]
	bl	ftl_memset
	ldr	x0, [x19,1216]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	adrp	x0, .LANCHOR0+210
	ldrh	w21, [x0,#:lo12:.LANCHOR0+210]
	mov	w0, w22
	mul	w21, w20, w21
	mov	w1, w21
	bl	flash_erase_block
	cmn	w0, #1
	beq	.L1961
	adrp	x20, .LANCHOR2
	ldr	x2, [x19,1216]
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x3, [x19,1224]
	mov	w0, w22
	mov	w1, w21
	add	x20, x20, 376
	ldrb	w4, [x20,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L1961
	ldrb	w4, [x20,9]
	mov	w0, w22
	ldr	x2, [x19,1216]
	mov	w1, w21
	ldr	x3, [x19,1224]
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	beq	.L1961
	ldr	x0, [x19,1216]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L1961
	ldr	x0, [x19,1224]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csinv	w20, w20, wzr, eq
	b	.L1958
.L1961:
	mov	w20, -1
.L1958:
	mov	w0, w22
	mov	w1, w21
	bl	flash_erase_block
	mov	w0, w20
.L1956:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.31, %function
fw_flash_page_prog.constprop.31:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x4, x4, :lo12:.LANCHOR0
	str	x21, [sp,32]
	mov	x21, x1
	stp	x19, x20, [sp,16]
	str	x2, [x29,56]
	ldr	x1, [x4,88]
	ldrb	w20, [x4,188]
	ldrb	w19, [x1,9]
	udiv	w19, w0, w19
	ldrb	w0, [x4,96]
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
	.size	fw_flash_page_prog.constprop.31, .-fw_flash_page_prog.constprop.31
	.text
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	and	w19, w1, 2097151
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ubfx	x27, x1, 21, 3
	mov	x24, x2
	ldrb	w0, [x0,98]
	mov	x23, x3
	ubfx	x26, x1, 24, 2
	cmp	w0, w27
	bhi	.L1971
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 240
	mov	w2, 685
	bl	printk
.L1971:
	add	x28, x20, :lo12:.LANCHOR0
	add	x0, x28, x27, sxtw
	ldrb	w21, [x0,200]
	ldr	x25, [x28,16]
	bl	nandc_rdy_status
	cbnz	w0, .L1972
	ldrb	w0, [x28,98]
	cmp	w0, 1
	bne	.L1973
	bl	nandc_wait_flash_ready
	b	.L1972
.L1973:
	mov	w0, w27
	mov	w1, w19
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L1972:
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	cbnz	w26, .L1974
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,97]
	cbz	w1, .L1975
	ldrb	w0, [x0,208]
	cbz	w0, .L1976
.L1975:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,210]
	udiv	w0, w19, w1
	mul	w0, w0, w1
	ldrb	w1, [x20,208]
	sub	w19, w19, w0
	cbz	w1, .L1977
	add	w19, w0, w19, lsl 1
	b	.L1976
.L1977:
	add	x20, x20, 212
	ldrh	w19, [x20,w19,uxtw 1]
	add	w19, w19, w0
	b	.L1978
.L1976:
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1978
.L1974:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1978:
	ubfiz	x1, x21, 8, 8
	mov	w0, 128
	add	x20, x25, x1
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
	adrp	x1, .LANCHOR2+385
	mov	x2, x24
	mov	x3, x23
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+385]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w22, [x20,2056]
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
	ldrb	w2, [x1,3861]
	cmp	w2, 255
	beq	.L1983
	ubfx	x4, x4, 21, 3
	add	x1, x1, 768
.L1985:
	add	x3, x1, x2, lsl 6
	ldr	w5, [x3,40]
	ubfx	x5, x5, 21, 3
	cmp	w4, w5
	bne	.L1984
	ldrb	w5, [x3,58]
	add	x3, x3, 48
	cmp	w5, 6
	bne	.L1984
	mov	w1, 3
	strb	w1, [x3,10]
	b	.L1983
.L1984:
	lsl	x2, x2, 6
	ldrb	w2, [x1,x2]
	cmp	w2, 255
	bne	.L1985
.L1983:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 3861
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
	str	x27, [sp,80]
	mov	x19, x0
	uxtb	w20, w1
	ldrh	w0, [x0,50]
	cbz	w0, .L1991
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L1991
	adrp	x0, .LC151
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC151
	mov	w2, w20
	bl	printk
.L1991:
	adrp	x21, .LANCHOR0
	adrp	x24, .LANCHOR3
	add	x21, x21, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR3
	mov	w22, 0
	add	x26, x21, 768
	add	x24, x24, 264
.L1992:
	cbz	w20, .L2018
	ldrb	w25, [x19]
	ldr	w23, [x19,40]
.L1993:
	mov	w0, w23
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2019
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1993
.L2019:
	cmp	w20, 1
	beq	.L1995
	ldrb	w0, [x21,765]
	cbnz	w0, .L1996
.L1995:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L1997
.L1996:
	ldrb	w0, [x19]
	ubfx	x27, x23, 21, 3
	cmp	w0, 255
	bne	.L1998
	adrp	x0, .LC0
	mov	x1, x24
	add	x0, x0, :lo12:.LC0
	mov	w2, 489
	bl	printk
.L1998:
	ldrb	w0, [x19]
	add	x0, x26, x0, lsl 6
	ldr	w4, [x0,40]
	ubfx	x0, x4, 21, 3
	cmp	w27, w0
	bne	.L1999
	ldrh	w3, [x21,3314]
	mov	w1, 21
	ldrb	w6, [x21,3312]
	mov	w27, 1
	sub	w1, w1, w3
	lsr	w0, w23, w3
	sub	w6, w6, #1
	lsl	w1, w27, w1
	sub	w1, w1, #1
	lsl	w2, w27, w3
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
	beq	.L1999
	and	w23, w2, w23
	and	w2, w2, w4
	cmp	w23, w2
	bne	.L1999
	cmp	w22, w6
	beq	.L1999
	ldr	w1, [x19,40]
	mov	w0, 17
	ldr	x2, [x19,8]
	add	w22, w22, w27
	ldr	x3, [x19,24]
	bl	flash_start_page_prog
	strb	w27, [x19,59]
	mov	w0, 8
	strb	w0, [x19,58]
	mov	w0, -1
	mov	x1, x19
	strb	w0, [x19]
	add	x0, x21, 3861
	bl	buf_add_tail
	b	.L1997
.L1999:
	mov	x0, x19
	mov	w22, 0
	bl	queue_prog_cmd
.L1997:
	sbfiz	x19, x25, 6, 32
	sub	w20, w20, #1
	add	x19, x26, x19
	b	.L1992
.L2018:
	mov	w0, w20
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	adrp	x20, .LANCHOR5
	ldrb	w1, [x0,2841]
	cbz	w1, .L2021
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 768
	ldrb	w2, [x2,513]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2021:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2,2841]
	strb	w0, [x20,513]
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
	adrp	x0, .LANCHOR0+2841
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+2841]
	cbz	w0, .L2025
	bl	timer_get_time
	adrp	x1, .LANCHOR5+516
	ldr	w1, [x1,#:lo12:.LANCHOR5+516]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2025
	bl	ftl_flush
.L2025:
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
	ldrb	w0, [x0,98]
	uxtb	w22, w2
	mov	x24, x5
	mov	x23, x6
	cmp	w0, w26
	mov	x19, x4
	bhi	.L2030
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 280
	mov	w2, 648
	bl	printk
.L2030:
	add	x4, x19, :lo12:.LANCHOR0
	add	x26, x4, x26, sxtw
	ldrb	w26, [x26,200]
	mov	w0, w26
	ldr	x19, [x4,16]
	bl	nandc_cs
	cbz	w25, .L2031
	sxtw	x1, w26
	add	x1, x1, 8
	add	x1, x19, x1, lsl 8
	str	w25, [x1,8]
.L2031:
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
	add	x0, x0, 3861
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
.L2037:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L2039
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2037
.L2039:
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
.L2041:
	mov	w0, w20
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L2045
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2041
.L2045:
	cmp	w24, 2
	bne	.L2043
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
	add	x0, x0, 3861
	bl	buf_add_tail
	b	.L2044
.L2043:
	mov	x0, x19
	bl	queue_tlc_prog_cmd
.L2044:
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
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x6, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, w1
	mov	x23, x2
	ldrb	w0, [x0,98]
	mov	x22, x3
	mov	w24, w4
	uxtb	w25, w5
	cmp	w0, w21
	ubfx	x28, x20, 24, 2
	mov	x19, x6
	bhi	.L2047
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 312
	mov	w2, 473
	bl	printk
.L2047:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w26, w20
	add	x0, x2, x21, sxtw
	ldrb	w27, [x0,200]
	cbnz	w28, .L2048
	ldrb	w0, [x2,97]
	cbz	w0, .L2049
	ldrb	w0, [x2,208]
	cbz	w0, .L2048
.L2049:
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w26, [x6,210]
	ldrb	w1, [x6,208]
	udiv	w2, w20, w26
	mul	w26, w2, w26
	sub	w0, w20, w26
	cbz	w1, .L2050
	add	w26, w26, w0, lsl 1
	b	.L2048
.L2050:
	add	x6, x6, 212
	ldrh	w0, [x6,w0,uxtw 1]
	add	w26, w0, w26
.L2048:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 4, .L2051
	adrp	x0, .LC152
	mov	w1, w27
	add	x0, x0, :lo12:.LC152
	mov	w2, w20
	bl	printk
.L2051:
	mov	w1, w26
	mov	w0, w27
	mov	x2, x23
	mov	x3, x22
	mov	w4, w24
	bl	flash_prog_page
	mov	w26, w0
	cbz	w25, .L2052
	adrp	x19, .LANCHOR4
	mov	w0, w21
	add	x25, x19, :lo12:.LANCHOR4
	mov	w1, w20
	mov	w4, w24
	ldr	x2, [x25,1216]
	ldr	x3, [x25,1224]
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2053
	cmn	w0, #1
	beq	.L2053
	ldr	x0, [x25,1216]
	ldr	w1, [x23]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2053
	ldr	x0, [x25,1224]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2052
.L2053:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 4
	adrp	x0, .LC153
	mov	w3, w2
	add	x0, x0, :lo12:.LC153
	ldr	x1, [x19,1224]
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC154
	ldr	x1, [x19,1216]
	add	x0, x0, :lo12:.LC154
	mov	w3, w2
	bl	rknand_print_hex
	b	.L2054
.L2052:
	cmn	w26, #1
	mov	w0, w26
	bne	.L2055
.L2054:
	adrp	x0, .LC155
	mov	w1, w20
	add	x0, x0, :lo12:.LC155
	bl	printk
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 312
	mov	w2, 499
	bl	printk
	mov	w0, -1
.L2055:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_test_block
	.type	ftl_test_block, %function
ftl_test_block:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR4
	adrp	x26, .LANCHOR5
	add	x25, x25, :lo12:.LANCHOR4
	add	x26, x26, :lo12:.LANCHOR5
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	w27, 0
	adrp	x28, .LC156
	uxth	w23, w0
	str	wzr, [x26,256]
	add	x0, x25, 1280
	str	wzr, [x25,1280]
	mov	w21, w27
	add	x28, x28, :lo12:.LC156
	str	x0, [x29,104]
	add	x26, x26, 256
.L2069:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,3320]
	cmp	w1, w21
	bls	.L2078
	mov	w24, 0
.L2079:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,3312]
	cmp	w0, w24
	bls	.L2094
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2070
	mov	x0, x28
	mov	w1, w23
	bl	printk
.L2070:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w19, [x0,3312]
	madd	w19, w23, w19, w24
	uxth	w19, w19
	cbnz	w21, .L2071
	ldr	x0, [x0,192]
	ldrb	w0, [x0,47]
	cmp	w0, w19
	bcs	.L2072
.L2071:
	uxtb	w22, w21
	mov	w1, w19
	mov	w0, w22
	bl	flash_check_bad_block
	cbnz	w0, .L2072
	add	x4, x20, :lo12:.LANCHOR0
	mov	w0, w22
	mov	w1, w27
	str	x4, [x29,96]
	ldrh	w3, [x4,3318]
	mul	w25, w19, w3
	mov	w2, w25
	bl	flash_erase_block_en
	ldr	x4, [x29,96]
	cbz	w0, .L2073
	ldrb	w0, [x4,3225]
	cmp	w0, 2
	bne	.L2093
	ldrb	w0, [x4,97]
	cbz	w0, .L2093
	mov	w0, w22
	mov	w1, 1
	mov	w2, w25
	bl	flash_erase_block_en
	cbz	w0, .L2075
	b	.L2093
.L2073:
	cbz	w27, .L2076
.L2075:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w27, 1
	ldrb	w0, [x0,3225]
	add	w25, w25, w0, lsl 24
.L2076:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	x2, [x29,104]
	mov	w0, w22
	mov	w1, w25
	mov	x3, x26
	mov	w5, 1
	ldrb	w4, [x4,2816]
	bl	flash_prog_page_en
	cbz	w0, .L2072
.L2093:
	mov	w0, w22
	mov	w1, w19
	bl	flash_mask_bad_block
.L2072:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2079
.L2094:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2069
.L2078:
	cbz	w27, .L2080
	ldr	x0, [x0,64]
	add	x23, x0, x23, uxth 2
	ldrb	w0, [x23,2]
	mov	w1, 2
	bfi	w0, w1, 3, 2
	strb	w0, [x23,2]
.L2080:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	ftl_test_block, .-ftl_test_block
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
	bne	.L2096
	adrp	x2, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 336
	mov	w2, 2034
	bl	printk
	adrp	x0, .LC155
	mov	w1, w20
	add	x0, x0, :lo12:.LC155
	bl	printk
.L2096:
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
	adrp	x22, .LANCHOR5
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR3
	add	x19, x22, :lo12:.LANCHOR5
	ldrb	w2, [x0,2816]
	adrp	x24, .LC157
	add	x23, x23, :lo12:.LANCHOR3
	add	x0, x19, 576
	lsl	w2, w2, 1
	mov	w25, 0
	add	x24, x24, :lo12:.LC157
	add	x23, x23, 352
	bl	ftl_memset
.L2098:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w27, [x19,832]
	ldrh	w20, [x19,834]
	ldr	x2, [x0,2848]
	ldrh	w28, [x0,3318]
	ldr	w1, [x2,4]
	add	w1, w1, 1
	str	w1, [x2,4]
	str	w26, [x19,576]
	ldr	x3, [x0,2848]
	ldr	w0, [x3,4]
	str	w0, [x19,580]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2099
	ldr	w3, [x3,4]
	mov	x0, x24
	mov	w1, w27
	mov	w2, w20
	bl	printk
.L2099:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,834]
	ldrh	w1, [x1,3276]
	cmp	w1, w0
	bhi	.L2100
	adrp	x20, .LC0
	add	x20, x20, :lo12:.LC0
.L2105:
	ldrb	w1, [x19,833]
	add	w1, w1, 1
	uxtb	w1, w1
	strb	w1, [x19,833]
	cmp	w1, 7
	bls	.L2101
	mov	x1, 0
.L2104:
	add	x0, x21, :lo12:.LANCHOR0
	uxth	w25, w1
	ldr	x2, [x0,192]
	add	w0, w1, 8
	add	x0, x2, x0, sxtw
	ldrb	w2, [x0,32]
	add	w0, w2, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2102
	mov	x0, x20
	mov	x1, x23
	mov	w2, 672
	bl	printk
	b	.L2103
.L2102:
	cmp	w2, 255
	bne	.L2103
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L2104
	mov	w25, w1
.L2103:
	strb	w25, [x19,833]
	mov	w25, 1
.L2101:
	add	x27, x21, :lo12:.LANCHOR0
	ldrb	w1, [x19,833]
	ldr	x0, [x27,192]
	add	x1, x0, x1
	ldrb	w2, [x1,40]
	strb	w2, [x19,832]
	cmp	w2, 255
	beq	.L2105
	ldrh	w20, [x27,3318]
	mov	w0, 0
	mul	w20, w2, w20
	mov	w1, w20
	bl	flash_erase_block
	ldrb	w4, [x27,2816]
	mov	w1, w20
	mov	w0, 0
	add	x2, x19, 896
	add	x3, x19, 576
	add	w20, w20, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x19,834]
	b	.L2106
.L2100:
	madd	w20, w27, w28, w20
	cbnz	w0, .L2106
	mov	w1, w20
	bl	flash_erase_block
.L2106:
	add	x4, x21, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w0, 0
	add	x2, x19, 896
	add	x3, x19, 576
	ldrb	w4, [x4,2816]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x19,834]
	adrp	x0, .LANCHOR6
	add	w1, w1, 1
	strh	w1, [x19,834]
	beq	.L2107
	add	x1, x0, :lo12:.LANCHOR6
	ldrb	w1, [x1,-128]
	cbz	w1, .L2108
.L2107:
	add	x0, x0, :lo12:.LANCHOR6
	strb	wzr, [x0,-128]
	b	.L2098
.L2108:
	cbnz	w25, .L2109
.L2117:
	add	x22, x22, :lo12:.LANCHOR5
	ldrb	w0, [x22,832]
	cmp	w0, 255
	bne	.L2111
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 708
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC0
	bl	printk
	b	.L2111
.L2109:
	ldrb	w19, [x19,833]
	adrp	x20, .LANCHOR3
	adrp	x23, .LC0
	add	x20, x20, :lo12:.LANCHOR3
	add	w19, w19, 1
	add	x23, x23, :lo12:.LC0
	add	x20, x20, 352
.L2112:
	cmp	w19, 7
	bhi	.L2117
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,192]
	add	w0, w19, 8
	add	x0, x1, x0, sxtw
	ldrb	w24, [x0,32]
	add	w0, w24, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2113
	mov	x0, x23
	mov	x1, x20
	mov	w2, 701
	bl	printk
	b	.L2114
.L2113:
	cmp	w24, 255
	beq	.L2115
.L2114:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,3318]
	mov	w0, 0
	mul	w1, w24, w1
	bl	flash_erase_block
.L2115:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2112
.L2111:
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
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR6
	stp	x23, x24, [sp,48]
	add	x23, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR6
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR5
	strb	w0, [x19,-128]
	ldrh	w1, [x23,56]
	add	x22, x21, :lo12:.LANCHOR5
	add	x24, x22, 896
	strb	w0, [x19,-126]
	mov	x0, x24
	strb	wzr, [x19,-127]
	add	x1, x24, x1, lsl 2
	str	x1, [x23,2848]
	mov	w1, 0
	str	x24, [x23,64]
	adrp	x26, .LC158
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC158
	sub	x0, x19, #64
	mov	w1, 0
	mov	w2, 16384
	mov	w19, 7
	bl	ftl_memset
	strb	wzr, [x22,833]
	ldr	x0, [x23,192]
	add	x23, x22, 576
	strh	wzr, [x22,834]
	ldrb	w0, [x0,40]
	strb	w0, [x22,832]
.L2135:
	add	x5, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w25, w19
	ldr	x1, [x5,192]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L2131
	ldrh	w22, [x5,3318]
	mov	w0, 0
	ldrb	w4, [x5,2816]
	mov	x2, x24
	mov	x3, x23
	str	x5, [x29,104]
	mul	w22, w1, w22
	mov	w1, w22
	bl	ftl_read_page
	cmn	w0, #1
	mov	w28, w0
	ldr	x5, [x29,104]
	bne	.L2132
	ldrb	w4, [x5,2816]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L2132:
	ldr	w0, [x27,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2133
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, w28
	mov	w3, 580
	ldr	x4, [x0,2848]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L2133:
	cmn	w28, #1
	beq	.L2131
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L2148
.L2131:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L2135
	mov	w25, 0
	b	.L2134
.L2148:
	mov	w19, w25
.L2134:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2136
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,2848]
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	ldr	w3, [x3]
	bl	printk
.L2136:
	cmn	w19, #1
	bne	.L2137
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, 0
	add	x0, x0, 896
	mov	w2, 16384
	add	x20, x20, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	w0, 21574
	ldr	x1, [x20,2848]
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, w19
	b	.L2138
.L2137:
	add	x26, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR5
	add	w0, w25, 8
	add	x24, x23, 896
	mov	x2, x24
	mov	w4, 4
	ldr	x1, [x26,192]
	add	x0, x1, x0, sxtw
	strb	w25, [x23,833]
	add	x25, x23, 576
	ldrb	w1, [x0,32]
	mov	x3, x25
	mov	w0, 0
	strb	w1, [x23,832]
	bl	flash_get_last_written_page
	sxth	w22, w0
	add	w0, w0, 1
	uxth	w19, w0
	ldrb	w0, [x23,832]
	ldrh	w23, [x26,3318]
	mul	w23, w0, w23
.L2139:
	tbnz	w22, #31, .L2144
	add	x26, x20, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w22, w23
	mov	x2, x24
	mov	x3, x25
	ldrb	w4, [x26,2816]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L2140
	ldr	x0, [x26,2848]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L2140
.L2144:
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR5
	ldr	x0, [x20,2848]
	strh	w19, [x21,834]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L2142
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
	b	.L2142
.L2140:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L2139
.L2142:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L2145
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L2145:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L2146
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L2146:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L2147
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L2147:
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
.L2138:
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
	ldr	x1, [x1,80]
	ldr	w3, [x1,520]
	cmp	w0, w3
	bls	.L2166
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L2179
.L2166:
	bcs	.L2167
.L2179:
	str	w0, [x1,520]
.L2167:
	adrp	x23, .LANCHOR3
	adrp	x24, .LANCHOR6
	add	x23, x23, :lo12:.LANCHOR3
	add	x24, x24, :lo12:.LANCHOR6
	mov	w0, 0
	add	x23, x23, 368
	sub	x25, x24, #64
	bl	ftl_total_vpn_update
.L2168:
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	add	x19, x19, 576
.L2171:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2848]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3276]
	cmp	w1, w0
	bcc	.L2169
	bl	ftl_ext_alloc_new_blk
.L2169:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldrh	w1, [x0,130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2170
	adrp	x0, .LC0
	mov	x1, x23
	add	x0, x0, :lo12:.LC0
	mov	w2, 1621
	bl	printk
.L2170:
	add	x21, x22, :lo12:.LANCHOR0
	mov	w0, 21
	mov	w1, 1
	ldr	x2, [x21,2848]
	ldrh	w20, [x21,3314]
	ldrh	w3, [x2,130]
	sub	w20, w0, w20
	ldrh	w0, [x2,140]
	asr	w26, w3, w20
	lsl	w20, w1, w20
	sub	w20, w20, #1
	ldrb	w2, [x21,2816]
	and	w3, w20, w3
	ldrh	w20, [x21,3318]
	mov	w1, 0
	lsl	w2, w2, 1
	madd	w20, w3, w20, w0
	mov	x0, x19
	bl	ftl_memset
	str	wzr, [x19]
	ldr	x0, [x21,2848]
	uxth	w20, w20
	ldrb	w4, [x21,2816]
	mov	w1, w20
	mov	x2, x25
	mov	x3, x19
	ldr	w0, [x0,56]
	str	w0, [x19,4]
	mov	w0, w26
	bl	ftl_prog_page
	ldr	x2, [x21,2848]
	ldrh	w1, [x2,140]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x2,140]
	cmp	w1, 1
	beq	.L2171
	cmn	w0, #1
	beq	.L2172
	ldrb	w0, [x24,-126]
	cbz	w0, .L2173
.L2172:
	adrp	x0, .LANCHOR6
	add	x0, x0, :lo12:.LANCHOR6
	strb	wzr, [x0,-126]
	b	.L2168
.L2173:
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
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR6
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	strh	wzr, [x1,3856]
	adrp	x21, .LANCHOR5
	ldr	x0, [x1,2848]
	add	x2, x20, :lo12:.LANCHOR6
	ldrh	w22, [x1,3314]
	mov	w1, 21
	add	x3, x21, :lo12:.LANCHOR5
	sub	x2, x2, #64
	ldrh	w0, [x0,130]
	sub	w22, w1, w22
	add	x3, x3, 576
	asr	w22, w0, w22
	and	w25, w0, 16383
	mov	w1, w25
	uxtb	w22, w22
	mov	w0, w22
	bl	flash_get_last_written_page
	sxth	w24, w0
	adrp	x0, .LANCHOR2
	mov	w23, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2181
	adrp	x1, .LANCHOR3
	adrp	x0, .LC108
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC108
	add	x1, x1, 392
	mov	w2, 1665
	mov	w3, w24
	bl	printk
.L2181:
	add	x20, x20, :lo12:.LANCHOR6
	add	x21, x21, :lo12:.LANCHOR5
	mov	w26, w24
	sub	x20, x20, #64
	add	x21, x21, 576
.L2182:
	tbnz	w26, #31, .L2187
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, w22
	mov	x2, x20
	mov	x3, x21
	ldrh	w1, [x27,3318]
	ldrb	w4, [x27,2816]
	madd	w1, w25, w1, w26
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L2183
	cmn	w0, #1
	beq	.L2183
	ldr	x0, [x27,80]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L2183
.L2187:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldrh	w1, [x0,140]
	cmp	w1, w24
	bgt	.L2185
	add	w23, w23, 1
	strh	w23, [x0,140]
	bl	ftl_ext_info_flush
	b	.L2185
.L2183:
	sub	w26, w26, #1
	sxth	w26, w26
	b	.L2182
.L2185:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x20, [x19,80]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x20,520]
	ldr	x20, [x19,80]
	bl	timer_get_time
	str	w0, [x20,604]
	ldr	x0, [x19,80]
	str	wzr, [x0,608]
	mov	w0, -1
	strh	w0, [x19,3858]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR3
	stp	x21, x22, [sp,32]
	add	x20, x20, :lo12:.LANCHOR3
	adrp	x22, .LC0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w21, 8
	adrp	x24, .LANCHOR0
	add	x22, x22, :lo12:.LC0
	add	x20, x20, 416
.L2200:
	add	x0, x24, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x0,192]
	add	x0, x0, x21, sxtw
	ldrb	w23, [x0,32]
	add	w0, w23, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L2198
	mov	x0, x22
	mov	x1, x20
	mov	w2, 1836
	bl	printk
.L2198:
	cmp	w23, 255
	beq	.L2199
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,3318]
	mov	w0, 0
	mul	w1, w23, w1
	bl	flash_erase_block
.L2199:
	add	w21, w21, 1
	cmp	w21, 16
	bne	.L2200
	bl	sblk_init
	bl	ftl_info_blk_init
	adrp	x0, .LANCHOR5
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR5
	mov	w2, 16384
	add	x0, x0, 896
	bl	ftl_memset
	add	x1, x19, :lo12:.LANCHOR0
	mov	w6, 1
	ldr	x0, [x1,2848]
	ldrh	w5, [x1,56]
	ldrh	w0, [x0,134]
.L2201:
	cmp	w0, w5
	bcs	.L2235
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x1, x0, 2, 16
	add	w0, w0, 1
	ldr	x2, [x4,64]
	uxth	w0, w0
	add	x2, x2, x1
	ldrb	w3, [x2,2]
	and	w3, w3, 31
	strb	w3, [x2,2]
	ldr	x2, [x4,64]
	add	x1, x2, x1
	ldrb	w2, [x1,2]
	bfi	w2, w6, 3, 2
	strb	w2, [x1,2]
	b	.L2201
.L2235:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 21574
	movk	w1, 0x494c, lsl 16
	mov	w20, 0
	adrp	x24, .LANCHOR7
	mov	w25, 1
	ldr	x2, [x0,2848]
	str	w1, [x2]
	ldr	x0, [x0,2848]
	strh	wzr, [x0,148]
.L2203:
	add	x21, x19, :lo12:.LANCHOR0
	ldrh	w0, [x21,56]
	cmp	w0, w20
	bls	.L2236
	add	x0, x24, :lo12:.LANCHOR7
	mov	w27, 0
	ubfiz	x22, x20, 2, 16
	mov	w23, w27
	strh	w20, [x0,3264]
	mov	w0, w20
	bl	ftl_test_block
	ldrb	w28, [x21,3312]
	ldrb	w4, [x21,3320]
	mov	w21, w27
	mul	w6, w20, w28
	uxth	w5, w28
.L2204:
	cmp	w21, w4
	bcs	.L2208
	mov	x26, 0
.L2209:
	cmp	w5, w26, uxth
	bls	.L2237
	add	w1, w6, w26
	mov	w0, w21
	str	x5, [x29,104]
	str	x4, [x29,112]
	str	x6, [x29,120]
	bl	flash_check_bad_block
	ldr	x6, [x29,120]
	ldr	x4, [x29,112]
	ldr	x5, [x29,104]
	cbz	w0, .L2205
	add	x3, x19, :lo12:.LANCHOR0
	add	w1, w27, w26
	lsl	w1, w25, w1
	ldr	x0, [x3,64]
	add	x0, x0, x22
	ldrb	w2, [x0,3]
	orr	w1, w1, w2
	strb	w1, [x0,3]
	ldr	x1, [x3,2848]
	ldrh	w0, [x1,148]
	add	w0, w0, 1
	strh	w0, [x1,148]
	b	.L2206
.L2205:
	add	w23, w23, 1
	uxth	w23, w23
.L2206:
	add	x26, x26, 1
	b	.L2209
.L2237:
	add	w21, w21, 1
	add	w27, w27, w28
	uxth	w21, w21
	b	.L2204
.L2208:
	cbnz	w23, .L2210
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,64]
	add	x22, x0, x22
	ldrb	w0, [x22,2]
	orr	w0, w0, -32
	strb	w0, [x22,2]
.L2210:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2203
.L2236:
	adrp	x0, .LANCHOR6
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR6
	mov	w2, 16384
	sub	x0, x0, #64
	bl	ftl_memset
	ldr	x1, [x21,80]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	mov	w6, 3
	mov	w7, 15
	str	w0, [x1]
	mov	x1, 0
	ldr	x0, [x21,192]
	ldrb	w4, [x21,3312]
	ldrb	w20, [x0,47]
	udiv	w20, w20, w4
	add	w20, w20, 1
.L2214:
	add	x3, x19, :lo12:.LANCHOR0
	lsl	x0, x1, 2
	cmp	w4, 1
	csinc	w5, w6, wzr, ne
	cmp	w4, 4
	ldr	x2, [x3,64]
	add	x2, x2, x0
	strb	w5, [x2,3]
	bne	.L2213
	ldr	x2, [x3,64]
	add	x2, x2, x0
	strb	w7, [x2,3]
.L2213:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x1, 1
	cmp	w20, w1, uxth
	ldr	x5, [x2,2848]
	ldrh	w3, [x5,148]
	add	w3, w4, w3
	strh	w3, [x5,148]
	ldr	x3, [x2,64]
	add	x0, x3, x0
	ldrb	w3, [x0,2]
	orr	w3, w3, -32
	strb	w3, [x0,2]
	bhi	.L2214
	mov	w0, 16
	sdiv	w4, w0, w4
	ldr	x0, [x2,2848]
	add	w4, w20, w4
	uxth	w4, w4
	strh	w4, [x0,134]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2215
	adrp	x0, .LC160
	mov	w1, 1883
	add	x0, x0, :lo12:.LC160
	mov	w2, 128
	mov	w3, w20
	bl	printk
.L2215:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 128
	ldr	x0, [x21,2848]
	add	x0, x0, 160
	bl	ftl_memset
	ldrb	w8, [x21,3312]
	mov	w10, 21
	ldrh	w0, [x21,3314]
	ldrb	w13, [x21,3320]
	uxth	w9, w8
	sub	w10, w10, w0
	mov	w0, 0
	mul	w11, w20, w9
	mov	w22, w0
	uxth	w11, w11
.L2216:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x1, [x21,2848]
	ldrh	w1, [x1,134]
	cmp	w1, w20
	bls	.L2238
	ldr	x2, [x21,64]
	ubfiz	x1, x20, 2, 16
	add	w6, w0, w11
	mov	w5, 0
	add	x2, x2, x1
	mov	w4, w5
	uxth	w6, w6
	ldrb	w3, [x2,2]
	orr	w3, w3, -32
	strb	w3, [x2,2]
	ldr	x2, [x21,64]
	ldr	x14, [x21,2848]
	add	x1, x2, x1
.L2217:
	cmp	w13, w4, uxth
	bls	.L2221
	lsl	w12, w4, w10
	mov	x3, 0
	uxth	w12, w12
.L2222:
	uxth	w2, w3
	cmp	w2, w9
	bcs	.L2239
	ldrb	w7, [x1,3]
	add	w15, w5, w3
	asr	w7, w7, w15
	tbnz	x7, 0, .L2218
	cmp	w8, 1
	mov	w7, w6
	bls	.L2219
	and	w2, w2, 1
	add	w2, w6, w2
	uxth	w7, w2
.L2219:
	add	x15, x14, x22, sxtw 1
	add	w22, w22, 1
	add	w2, w7, w12
	strh	w2, [x15,160]
	uxth	w22, w22
.L2218:
	add	x3, x3, 1
	b	.L2222
.L2239:
	add	w4, w4, 1
	add	w5, w5, w8
	b	.L2217
.L2221:
	add	w20, w20, 1
	add	w0, w0, w9
	uxth	w20, w20
	uxth	w0, w0
	b	.L2216
.L2238:
	ldrh	w2, [x21,56]
	mov	w1, 0
	ldr	x0, [x21,72]
	lsl	w2, w2, 1
	bl	ftl_memset
	bl	zftl_sblk_list_init
	ldr	x0, [x21,2848]
	ldrh	w1, [x21,56]
	ldrh	w2, [x0,134]
	strh	w22, [x0,112]
	sub	w1, w1, w2
	strh	wzr, [x0,114]
	strh	w1, [x0,116]
	strh	wzr, [x0,118]
	mov	w0, 1
	bl	ftl_alloc_sblk
	mov	w22, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x21,2848]
	mov	w0, w22
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	ldr	x0, [x21,2848]
	ldrh	w20, [x0,672]
	strh	wzr, [x0,690]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L2224
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 416
	mov	w2, 1914
	bl	printk
.L2224:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 256
	ldr	x0, [x19,2848]
	add	x0, x0, 416
	strh	wzr, [x0,280]
	bl	ftl_memset
	ldr	x1, [x19,2848]
	mov	w2, 21
	ldrh	w0, [x19,3314]
	sub	w2, w2, w0
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3266
	asr	w2, w20, w2
	strh	w20, [x1,692]
	strh	w2, [x1,694]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3266]
	strh	w22, [x1,416]
	strh	w0, [x1,698]
	bl	ftl_alloc_sys_blk
	mov	w20, w0
	mov	w1, 0
	bl	ftl_erase_phy_blk
	ldr	x1, [x19,2848]
	ldr	x0, [x19,80]
	strh	w20, [x1,130]
	add	x0, x0, 16
	mov	w1, 2
	bl	ftl_open_sblk_init
	ldr	x0, [x19,80]
	mov	w1, 3
	add	x0, x0, 48
	bl	ftl_open_sblk_init
	ldr	x1, [x19,2848]
	ldr	x0, [x19,80]
	ldrh	w2, [x1,134]
	add	x0, x0, 136
	strh	w2, [x0,-10]
	mov	w2, -1
	strh	wzr, [x0,-12]
	strh	w2, [x0,-56]
	strh	w2, [x0,-6]
	mov	w2, -1
	strh	wzr, [x0,-16]
	str	w2, [x0,408]
	strh	wzr, [x0,-14]
	strh	w2, [x1,126]
	mov	w1, 255
	mov	w2, 256
	bl	ftl_memset
	ldr	x0, [x19,80]
	mov	w1, 255
	mov	w2, 128
	add	x0, x0, 392
	bl	ftl_memset
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	ftl_info_blk_init
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	ftl_re_low_format_test
	.type	ftl_re_low_format_test, %function
ftl_re_low_format_test:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC161
	mov	w1, 1
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC161
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	adrp	x19, .LANCHOR0
	bl	printk
	bl	sblk_init
	bl	ftl_info_blk_init
	bl	ftl_ext_info_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2848]
	ldrh	w2, [x0,56]
	mov	w0, 0
	mov	w3, w0
	mov	w4, w0
	ldrh	w1, [x1,134]
.L2241:
	cmp	w1, w2
	bcs	.L2247
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x6, [x5,64]
	add	x6, x6, x1, uxth 2
	ldrb	w5, [x6,2]
	and	w7, w5, 31
	ands	w5, w5, 24
	strb	w7, [x6,2]
	bne	.L2242
	add	w4, w4, 1
	uxth	w4, w4
	b	.L2243
.L2242:
	cmp	w5, 16
	bne	.L2244
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2243
.L2244:
	add	w0, w0, 1
	uxth	w0, w0
.L2243:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2241
.L2247:
	add	x20, x19, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	ldr	x1, [x20,2848]
	strh	w0, [x1,116]
	ldr	x0, [x20,72]
	strh	w4, [x1,114]
	strh	w3, [x1,118]
	strh	wzr, [x1,122]
	strh	wzr, [x1,120]
	strh	wzr, [x1,124]
	mov	w1, 0
	bl	ftl_memset
	bl	zftl_sblk_list_init
	mov	w0, 1
	bl	ftl_alloc_sblk
	mov	w21, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x20,2848]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	ldr	x0, [x20,2848]
	ldrh	w20, [x0,672]
	strh	wzr, [x0,690]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L2246
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 432
	mov	w2, 1986
	bl	printk
.L2246:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 256
	ldr	x0, [x19,2848]
	add	x0, x0, 416
	strh	wzr, [x0,280]
	bl	ftl_memset
	ldr	x0, [x19,2848]
	mov	w1, 0
	mov	w2, 4096
	add	x0, x0, 704
	bl	ftl_memset
	ldr	x1, [x19,2848]
	lsr	w0, w20, 14
	ldrh	w2, [x19,56]
	and	w20, w20, 16383
	strh	w0, [x1,694]
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3266
	lsl	w2, w2, 1
	strh	w20, [x1,692]
	strh	w21, [x1,416]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3266]
	strh	w0, [x1,698]
	mov	w1, 0
	ldr	x0, [x19,72]
	bl	ftl_memset
	ldr	x0, [x19,80]
	mov	w1, 2
	add	x0, x0, 16
	bl	ftl_open_sblk_init
	ldr	x0, [x19,80]
	mov	w1, 3
	add	x0, x0, 48
	bl	ftl_open_sblk_init
	ldr	x1, [x19,2848]
	ldr	x0, [x19,80]
	ldrh	w2, [x1,134]
	add	x0, x0, 136
	strh	w2, [x0,-10]
	mov	w2, -1
	strh	wzr, [x0,-12]
	strh	w2, [x0,-56]
	strh	w2, [x0,-6]
	mov	w2, -1
	strh	wzr, [x0,-16]
	str	w2, [x0,408]
	strh	wzr, [x0,-14]
	strh	w2, [x1,126]
	mov	w1, 255
	mov	w2, 256
	bl	ftl_memset
	ldr	x0, [x19,80]
	mov	w1, 255
	mov	w2, 128
	add	x0, x0, 392
	bl	ftl_memset
	ldr	x0, [x19,80]
	str	wzr, [x0,520]
	str	wzr, [x0,524]
	str	wzr, [x0,528]
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	ftl_info_blk_init
	ldr	x21, [sp,32]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_re_low_format_test, .-ftl_re_low_format_test
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
	ldr	x22, [x19,3840]
	bne	.L2250
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L2250
	ldrh	w0, [x20]
	add	x22, x22, x24, uxth 2
	bl	ftl_vpn_decrement
	adrp	x1, .LANCHOR5
	mov	w0, 15555
	add	x1, x1, :lo12:.LANCHOR5
	movk	w0, 0xf55f, lsl 16
	add	x2, x1, 256
	str	x2, [x29,72]
	str	w0, [x1,256]
	ldrb	w0, [x19,3244]
	ldrh	w1, [x19,3276]
	mul	w1, w1, w0
	mov	x0, x22
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,72]
	mov	x1, x22
	ldrb	w3, [x19,2816]
	str	w0, [x2,4]
	mov	w0, w23
	str	w21, [x2,8]
	str	w21, [x2,12]
	bl	ftl_prog_ppa_page
.L2250:
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
	sub	sp, sp, #176
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x23, x24, [sp,96]
	adrp	x23, .LANCHOR2
	str	x1, [x29,120]
	stp	x19, x20, [sp,64]
	mov	x19, x0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	stp	x21, x22, [sp,80]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	tbz	x0, 12, .L2255
	adrp	x0, .LC162
	ldrh	w1, [x19,2]
	add	x0, x0, :lo12:.LC162
	bl	printk
.L2255:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2256
	adrp	x0, .LC163
	ldrb	w1, [x19,5]
	add	x0, x0, :lo12:.LC163
	bl	printk
.L2256:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2257
	adrp	x0, .LC164
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LC164
	bl	printk
.L2257:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2258
	adrp	x0, .LC165
	ldrh	w1, [x19,16]
	ldrh	w2, [x19,18]
	add	x0, x0, :lo12:.LC165
	bl	printk
.L2258:
	ldrh	w0, [x19,10]
	adrp	x24, .LANCHOR5
	strh	w0, [x19,14]
	mov	w0, 1
	adrp	x25, .LANCHOR3
	add	x24, x24, :lo12:.LANCHOR5
	bl	buf_alloc
	add	x25, x25, :lo12:.LANCHOR3
	ldrb	w21, [x19,5]
	mov	x27, x0
	ldrh	w22, [x19,2]
	mov	w28, 0
	add	x24, x24, 576
	add	x25, x25, 456
.L2259:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3276]
	cmp	w0, w22
	bls	.L2262
	ldrb	w21, [x19,5]
.L2260:
	ldrb	w0, [x19,9]
	cmp	w0, w21
	bls	.L2340
	add	x0, x19, x21, sxtw 1
	ldrh	w5, [x0,16]
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2261
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	str	x5, [x29,104]
	ldrh	w4, [x3,3318]
	ldrb	w3, [x3,2816]
	madd	w4, w5, w4, w22
	str	x4, [x29,112]
	mov	w0, w4
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w26, w0
	beq	.L2262
	cmn	w0, #1
	ldr	x4, [x29,112]
	ldr	x5, [x29,104]
	beq	.L2263
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2263
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2263
	ldr	x0, [x27,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2262
.L2263:
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2264
	ldr	x7, [x27,8]
	mov	w3, w4
	ldr	x0, [x27,24]
	mov	w2, w22
	mov	w4, w26
	ldr	w1, [x7,12]
	str	w1, [sp]
	ldr	w1, [x0]
	str	w1, [sp,8]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w5
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
.L2264:
	adrp	x0, .LANCHOR6-127
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-127]
	ldrb	w0, [x19,9]
	ldrh	w1, [x19,10]
	madd	w0, w22, w0, w21
	cmp	w1, w0
	beq	.L2265
	adrp	x0, .LC0
	mov	x1, x25
	add	x0, x0, :lo12:.LC0
	mov	w2, 1270
	bl	printk
.L2265:
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	ldrb	w2, [x19,9]
	add	w1, w1, w0
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,3276]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2266
	adrp	x0, .LC0
	mov	x1, x25
	add	x0, x0, :lo12:.LC0
	mov	w2, 1271
	bl	printk
.L2266:
	cmn	w26, #1
	beq	.L2270
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmn	w0, #1
	beq	.L2270
	bl	lpa_hash_get_ppa
	ldr	x1, [x29,120]
	cbz	x1, .L2268
	ldr	x3, [x27,24]
	ldr	w1, [x3,8]
	cmp	w0, w1
	beq	.L2268
	cmn	w0, #1
	beq	.L2268
	add	x28, x20, :lo12:.LANCHOR0
	mov	w4, 21
	mov	w26, 1
	ldrh	w1, [x28,3314]
	lsr	w2, w0, w1
	sub	w1, w4, w1
	lsl	w1, w26, w1
	sub	w1, w1, #1
	and	w1, w2, w1
	ldrb	w2, [x28,3312]
	udiv	w1, w1, w2
	ldr	x2, [x29,120]
	ldrh	w2, [x2]
	cmp	w2, w1, uxth
	bne	.L2268
	ldr	w5, [x3]
	mov	x2, x24
	ldr	x1, [x27,8]
	ldrb	w3, [x28,2816]
	str	x4, [x29,104]
	str	x5, [x29,112]
	bl	ftl_read_ppa_page
	ldr	w0, [x24]
	ldr	x5, [x29,112]
	ldr	x4, [x29,104]
	cmp	w0, w5
	bcc	.L2268
	ldr	x0, [x27,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2270
	ldrh	w0, [x28,3314]
	sub	w4, w4, w0
	lsr	w1, w1, w0
	lsl	w26, w26, w4
	ldrb	w0, [x28,3312]
	sub	w26, w26, #1
	and	w1, w26, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2270
.L2268:
	ldr	x2, [x27,24]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2824]
	cmp	w3, w0
	bcs	.L2270
	ldrb	w0, [x19,9]
	ldrh	w4, [x1,3276]
	ldrh	w3, [x19,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L2341
.L2270:
	ldrh	w0, [x19,6]
	mov	w28, 1
	sub	w0, w0, #1
	strh	w0, [x19,6]
	ldrh	w0, [x19,10]
	add	w0, w0, 1
	strh	w0, [x19,10]
.L2261:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2260
.L2340:
	add	w22, w22, 1
	strb	wzr, [x19,5]
	uxth	w22, w22
	b	.L2259
.L2262:
	add	x2, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,10]
	ldrh	w1, [x19,6]
	strh	w22, [x19,2]
	add	w1, w1, w0
	ldrh	w2, [x2,3276]
	ldrb	w0, [x19,9]
	strb	w21, [x19,5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L2274
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 456
	mov	w2, 1347
	bl	printk
.L2274:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w3, [x19,10]
	ldr	x4, [x0,3840]
	mov	w0, 0
	mov	w2, w0
.L2275:
	cmp	w2, w3
	bcs	.L2342
	ldrh	w1, [x19,12]
	add	w1, w2, w1
	ldr	w1, [x4,x1,lsl 2]
	cmn	w1, #1
	beq	.L2276
	add	w1, w0, 1
	uxth	w0, w1
.L2276:
	add	w2, w2, 1
	b	.L2275
.L2342:
	add	x2, x20, :lo12:.LANCHOR0
	ldrb	w1, [x19,9]
	sub	w0, w0, w3
	ldrh	w21, [x2,3276]
	madd	w0, w1, w21, w0
	uxth	w21, w0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2278
	ldrh	w1, [x19]
	adrp	x0, .LC167
	ldr	x4, [x2,72]
	add	x0, x0, :lo12:.LC167
	ubfiz	x3, x1, 1, 16
	mov	w2, w21
	ldrh	w3, [x4,x3]
	bl	printk
.L2278:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	adrp	x22, .LC168
	mov	w24, -1
	add	x22, x22, :lo12:.LC168
	ldr	x0, [x0,72]
	strh	w21, [x0,x1,lsl 1]
	mov	x0, x27
	mov	w21, 0
	bl	buf_free
.L2279:
	add	x2, x20, :lo12:.LANCHOR0
	ldrb	w0, [x2,3244]
	cmp	w21, w0, lsl 1
	bcs	.L2343
	cbz	w28, .L2280
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bls	.L2280
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w25, w0
	ldr	w0, [x23,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2281
	mov	x0, x22
	mov	w1, w25
	bl	printk
.L2281:
	add	x3, x20, :lo12:.LANCHOR0
	ldr	x0, [x27,24]
	ldr	x1, [x3,2848]
	ldr	w1, [x1,8]
	str	w1, [x0]
	ldr	x0, [x27,24]
	str	w24, [x0,4]
	ldr	x0, [x27,24]
	str	w24, [x0,8]
	ldr	x0, [x27,24]
	str	wzr, [x0,12]
	ldr	x0, [x27,8]
	str	wzr, [x0]
	mov	w0, w25
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrb	w3, [x3,2816]
	bl	ftl_prog_ppa_page
	ldrh	w0, [x19]
	bl	ftl_vpn_decrement
.L2280:
	add	w21, w21, 1
	b	.L2279
.L2343:
	ldrh	w1, [x2,3276]
	ldrh	w0, [x19,12]
	ldrb	w3, [x19,9]
	madd	w0, w1, w3, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x2,3840]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L2283
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 456
	mov	w2, 1377
	bl	printk
.L2283:
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2254
	mov	x0, x19
	bl	ftl_write_last_log_page
	b	.L2254
.L2341:
	ldr	x0, [x1,2848]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2271
	str	w2, [x0,8]
.L2271:
	ldr	x1, [x27,24]
	ldrh	w0, [x19,10]
	ldrh	w2, [x19,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	b	.L2270
.L2254:
	sub	sp, x29, #48
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 176
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
	add	x22, x22, 480
.L2345:
	add	x2, x25, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR0
	ldr	x0, [x2,2848]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3276]
	cmp	w3, w1
	bcs	.L2346
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2347
.L2346:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2347:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	ldrh	w1, [x0,692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2348
	mov	x0, x23
	mov	x1, x22
	mov	w2, 228
	bl	printk
.L2348:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR7
	add	x27, x20, :lo12:.LANCHOR7
	mov	w2, 64
	ldr	x1, [x19,2848]
	ldrh	w26, [x19,3318]
	ldrh	w0, [x1,692]
	ldrh	w1, [x1,696]
	madd	w26, w0, w26, w1
	ldr	x0, [x27,3272]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x27,3272]
	mov	x2, x24
	str	w21, [x0]
	ldr	x1, [x19,2848]
	ldr	x0, [x27,3272]
	ldrb	w4, [x19,2816]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	w1, w26
	ldr	x0, [x19,2848]
	ldr	x3, [x27,3272]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x1, [x19,2848]
	ldrh	w2, [x1,696]
	add	w2, w2, 1
	uxth	w2, w2
	strh	w2, [x1,696]
	cmp	w2, 1
	beq	.L2349
	ldrb	w2, [x27,3280]
	cbz	w2, .L2350
.L2349:
	add	x20, x20, :lo12:.LANCHOR7
	strb	wzr, [x20,3280]
	b	.L2345
.L2350:
	cmn	w0, #1
	bne	.L2352
	adrp	x0, .LC169
	mov	w1, w26
	add	x0, x0, :lo12:.LC169
	bl	printk
	b	.L2345
.L2352:
	ldrh	w0, [x1,698]
	cmp	w21, w0
	bcs	.L2353
	add	x21, x1, x21, uxtw 2
	str	w26, [x21,704]
.L2353:
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
	tbz	x0, 12, .L2358
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 251
	add	x2, x2, 496
	bl	printk
.L2358:
	adrp	x22, .LANCHOR4
	adrp	x23, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC171
	adrp	x25, .LC172
	ldr	x0, [x0,1200]
	mov	w26, 0
	add	x24, x24, :lo12:.LC171
	add	x25, x25, :lo12:.LC172
	bl	ftl_memset
	ldr	x1, [x19,192]
	adrp	x0, .LC170
	add	x0, x0, :lo12:.LC170
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,192]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2359:
	add	x21, x22, :lo12:.LANCHOR4
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,1208]
	ldrh	w19, [x21,1210]
	mov	w1, w20
	ldrh	w28, [x27,210]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,1060]
	ldrh	w0, [x21,1210]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2360
	ldr	x1, [x27,192]
	ldrb	w19, [x21,1209]
	strh	wzr, [x21,1210]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,1208]
	ldr	x20, [x27,192]
	strb	w0, [x21,1209]
	mov	x0, x20
	strb	w19, [x21,1208]
	ldrh	w1, [x20,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x20,8]
	bl	js_hash
	str	w0, [x20,12]
	ldrh	w0, [x27,210]
	mul	w19, w19, w0
	mov	w0, 0
	b	.L2368
.L2360:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2361
.L2368:
	mov	w1, w19
	bl	flash_erase_block
.L2361:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR4
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,192]
	ldr	x0, [x20,1200]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,1200]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,192]
	ldr	x3, [x20,1200]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,1210]
	add	w1, w1, 1
	strh	w1, [x20,1210]
	bne	.L2362
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2359
.L2362:
	cbnz	w26, .L2369
	mov	w26, 1
	b	.L2359
.L2369:
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
	stp	x25, x26, [sp,64]
	tbz	x0, 12, .L2371
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 2083
	add	x2, x2, 520
	bl	printk
.L2371:
	mov	x0, x19
	adrp	x19, .LANCHOR0
	mov	x21, 0
	mov	w25, 2
	bl	nandc_init
	mov	w26, 44
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x20, :lo12:.LANCHOR2
	add	x22, x0, 732
	add	x1, x1, 376
	mov	x23, x0
	mov	x24, x22
	str	x1, [x0,88]
.L2377:
	mov	w0, w21
	mov	x1, x22
	bl	flash_read_id
	add	x0, x23, 728
	strb	w25, [x21,x0]
	cbnz	x21, .L2372
	ldrb	w0, [x24]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2373
.L2375:
	mov	w22, -2
	b	.L2419
.L2373:
	ldrb	w0, [x24,1]
	cmp	w0, 255
	beq	.L2375
.L2372:
	ldrb	w0, [x22]
	cmp	w0, 181
	bne	.L2376
	strb	w26, [x22]
.L2376:
	add	x21, x21, 1
	add	x22, x22, 8
	cmp	x21, 4
	bne	.L2377
	add	x22, x20, :lo12:.LANCHOR2
	add	x23, x19, :lo12:.LANCHOR0
	add	x22, x22, 440
	mov	w21, 0
	add	x23, x23, 732
.L2380:
	ldrb	w2, [x22]
	add	x0, x22, 1
	mov	x1, x23
	bl	flash_mem_cmp8
	cbnz	w0, .L2378
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x21, x21, 5, 32
	add	x0, x1, 440
	adds	x21, x0, x21
	beq	.L2375
	ldrb	w3, [x21,22]
	mov	x0, 0
	b	.L2382
.L2378:
	add	w21, w21, 1
	add	x22, x22, 32
	cmp	w21, 30
	bne	.L2380
	b	.L2375
.L2382:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1400]
	cmp	w4, w3
	beq	.L2381
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2382
	mov	w2, w0
.L2381:
	add	x22, x20, :lo12:.LANCHOR2
	ubfiz	x2, x2, 5, 32
	add	x1, x22, 1400
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x1, x2
	add	x0, x0, 104
	mov	w2, 32
	add	x22, x22, 376
	bl	ftl_memcpy
	mov	x0, x22
	mov	x1, x21
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w0, 8
	bhi	.L2383
	ldrb	w1, [x22,20]
	cmp	w1, 60
	bls	.L2384
	mov	w1, 60
	strb	w1, [x22,20]
.L2384:
	cmp	w0, 8
	bne	.L2383
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,732]
	cmp	w0, 44
	beq	.L2444
.L2383:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2385
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 2104
	add	x2, x2, 520
	bl	printk
.L2385:
	adrp	x21, .LANCHOR4
	mov	w0, 16384
	add	x23, x21, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR0
	bl	ftl_malloc
	add	x24, x20, :lo12:.LANCHOR2
	str	x0, [x23,1216]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x23,1160]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x22,192]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1224]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1176]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x23,1200]
	adrp	x0, .LANCHOR5+512
	strb	wzr, [x0,#:lo12:.LANCHOR5+512]
	bl	flash_die_info_init
	ldrb	w0, [x24,394]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,396]
	bl	nandc_bch_sel
	str	xzr, [x23,1192]
	ldr	x1, [x22,88]
	ldrh	w0, [x1,16]
	ubfx	x2, x0, 8, 3
	strb	w2, [x22,764]
	adrp	x2, .LANCHOR7+3281
	ubfx	x3, x0, 3, 1
	strb	w3, [x2,#:lo12:.LANCHOR7+3281]
	ubfx	x2, x0, 4, 1
	strb	w2, [x22,765]
	ubfx	x2, x0, 12, 1
	strb	w2, [x22,3240]
	ubfx	x2, x0, 13, 1
	ubfx	x0, x0, 14, 1
	strb	w0, [x22,208]
	ldrb	w0, [x1,28]
	mov	w1, 60
	strb	w1, [x22,96]
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	strb	w2, [x22,3316]
	strb	w0, [x22,97]
	cmp	w1, 9
	bne	.L2386
	mov	w1, 70
	strb	w1, [x22,96]
.L2386:
	add	x2, x20, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	add	x2, x2, 376
	add	x3, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1232]
	ldrb	w0, [x2,19]
	ldrh	w2, [x2,16]
	strb	w0, [x3,189]
	tbz	x2, 6, .L2388
	sub	w2, w0, #17
	uxtb	w2, w2
	cmp	w2, 2
	bhi	.L2389
	adrp	x2, micron_read_retrial
	cmp	w0, 19
	add	x2, x2, :lo12:micron_read_retrial
	str	x2, [x1,1192]
	bne	.L2440
	mov	w0, 15
	b	.L2441
.L2389:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2409
	cmp	w0, 33
	bne	.L2392
.L2409:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1192]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,190]
	mov	w1, 7
	strb	w1, [x0,1184]
	b	.L2388
.L2392:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w3, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2410
	cbz	w3, .L2394
.L2410:
	add	x2, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x2,1192]
	beq	.L2396
	cmp	w0, 68
	beq	.L2396
	mov	w0, 7
	strb	w0, [x2,1184]
	b	.L2397
.L2396:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1184]
.L2397:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w3, .L2442
	mov	w1, 5
.L2442:
	strb	w1, [x0,190]
	b	.L2388
.L2394:
	sub	w0, w0, #36
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L2388
	adrp	x0, toshiba_3d_read_retrial
	add	x0, x0, :lo12:toshiba_3d_read_retrial
	str	x0, [x1,1192]
.L2440:
	mov	w0, 7
.L2441:
	strb	w0, [x1,1184]
.L2388:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2399
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 2152
	add	x2, x2, 520
	bl	printk
.L2399:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,136]
	ldrb	w0, [x0,764]
	tbz	x0, 0, .L2400
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2443
.L2400:
	mov	w0, 4
.L2443:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w22, w0
	bne	.L2402
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2419
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	mov	w1, 2172
	add	x2, x2, 520
	add	x0, x0, :lo12:.LC115
	bl	printk
	b	.L2419
.L2402:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,189]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L2403
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,1192]
.L2403:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,764]
	tbz	x0, 2, .L2405
	ldr	x0, [x19,192]
	ldrb	w0, [x0,19]
	cbz	w0, .L2405
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w1, [x19,210]
	mov	w0, 0
	ldrb	w2, [x21,1208]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,136]
	cbnz	w0, .L2405
	ldr	x1, [x19,192]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L2405:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2407
	adrp	x2, .LANCHOR3
	adrp	x0, .LC115
	add	x2, x2, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC115
	mov	w1, 2225
	add	x2, x2, 520
	bl	printk
.L2407:
	bl	nand_flash_print_info
	mov	w22, 0
	b	.L2419
.L2444:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 376
	ldrb	w1, [x0,28]
	cmp	w1, 3
	bne	.L2383
	strb	wzr, [x0,28]
	b	.L2383
.L2419:
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
	adrp	x22, .LC166
	adrp	x23, .LC173
	mov	x27, x0
	mov	w19, w20
	adrp	x28, .LANCHOR0
	add	x22, x22, :lo12:.LC166
	add	x23, x23, :lo12:.LC173
	add	x24, x24, 704
.L2446:
	add	x3, x28, :lo12:.LANCHOR0
	ldrh	w1, [x3,3276]
	cmp	w1, w19
	bls	.L2458
	ldrh	w21, [x3,3318]
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	madd	w21, w25, w21, w19
	ldrb	w3, [x3,2816]
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
	beq	.L2451
	cmn	w26, #1
	bne	.L2447
.L2451:
	mov	w20, 1
.L2447:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2446
.L2458:
	mov	x0, x27
	bl	buf_free
	cbz	w20, .L2450
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 536
	mov	w2, 1214
	bl	printk
.L2450:
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
	adrp	x0, .LC174
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC174
	ldrb	w1, [x19,2818]
	bl	printk
	adrp	x3, .LANCHOR5
	adrp	x0, .LC175
	add	x3, x3, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC175
	ldrb	w1, [x3,832]
	ldrb	w2, [x3,833]
	ldrh	w3, [x3,834]
	bl	printk
	ldr	x2, [x19,2848]
	adrp	x0, .LC176
	add	x0, x0, :lo12:.LC176
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC177
	add	x0, x0, :lo12:.LC177
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC178
	add	x0, x0, :lo12:.LC178
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC179
	add	x0, x0, :lo12:.LC179
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x19,3276]
	mov	w2, 4
	ldrb	w3, [x19,3244]
	ldr	x1, [x19,3840]
	mul	w3, w0, w3
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,72]
	adrp	x0, .LC181
	ldrh	w3, [x19,56]
	add	x0, x0, :lo12:.LC181
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x3, [x19,2848]
	adrp	x0, .LC173
	add	x0, x0, :lo12:.LC173
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x19,64]
	adrp	x0, .LC182
	ldrh	w3, [x19,56]
	add	x0, x0, :lo12:.LC182
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC183
	add	x1, x19, 3328
	add	x0, x0, :lo12:.LC183
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3276]
	mov	w2, 2
	ldrb	w3, [x19,3244]
	ldr	x1, [x19,3848]
	mul	w3, w0, w3
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
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
	ldrh	w4, [x5,3314]
	ldrb	w3, [x5,3312]
	sub	w6, w6, w4
	lsr	w4, w2, w4
	lsl	w6, w7, w6
	sub	w6, w6, #1
	and	w4, w6, w4
	udiv	w4, w4, w3
	ldr	x3, [x5,64]
	add	x4, x3, x4, uxth 2
	ldrb	w3, [x4,2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 7
	cset	w4, eq
	cbnz	w4, .L2464
	cmp	w3, w7
	bne	.L2461
.L2464:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L2461:
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxth	w23, w1
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	cmp	w23, 31
	uxth	w21, w0
	bls	.L2467
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 552
	mov	w2, 31
	bl	printk
.L2467:
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR7
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,2848]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L2468
	adrp	x0, .LC186
	mov	w1, w21
	add	x0, x0, :lo12:.LC186
	add	x19, x19, :lo12:.LANCHOR7
	bl	printk
	mov	x0, 0
	ldrh	w2, [x19,3282]
	mov	w1, 255
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,2848]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L2470
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 552
	mov	w2, 35
	b	.L2474
.L2468:
	add	x0, x0, x21, sxtw 2
	sbfiz	x23, x23, 4, 32
	ldr	w22, [x0,704]
	add	x0, x20, 3872
	add	x24, x0, x23
	strh	w21, [x0,x23]
	strh	wzr, [x24,2]
	cbnz	w22, .L2471
	adrp	x0, .LC187
	mov	w1, w21
	mov	w2, w22
	add	x19, x19, :lo12:.LANCHOR7
	add	x0, x0, :lo12:.LC187
	bl	printk
	ldr	x0, [x24,8]
	mov	w1, 255
	ldrh	w2, [x19,3282]
	bl	ftl_memset
	b	.L2470
.L2471:
	add	x23, x19, :lo12:.LANCHOR7
	ldrb	w3, [x20,2816]
	ldr	x1, [x24,8]
	mov	w0, w22
	ldr	x2, [x23,3272]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x23,3272]
	ldr	w2, [x0]
	cmp	w2, w21
	beq	.L2472
	adrp	x0, .LC188
	mov	w4, w22
	mov	w1, w21
	add	x0, x0, :lo12:.LC188
	bl	printk
	ldr	x3, [x20,2848]
	adrp	x0, .LC189
	add	x0, x0, :lo12:.LC189
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldrb	w3, [x20,2816]
	adrp	x0, .LC190
	ldr	x1, [x24,8]
	add	x0, x0, :lo12:.LC190
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldr	x1, [x23,3272]
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldrh	w0, [x20,3314]
	mov	w1, 21
	mov	w2, 1
	sub	w1, w1, w0
	lsr	w22, w22, w0
	lsl	w1, w2, w1
	ldrb	w0, [x20,3312]
	sub	w1, w1, #1
	and	w22, w1, w22
	mov	x1, 0
	udiv	w0, w22, w0
	bl	ftl_sblk_dump
.L2472:
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x19,3272]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L2470
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 552
	mov	w2, 57
.L2474:
	bl	printk
.L2470:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
	ldr	x0, [x20,2848]
	ldrh	w1, [x0,688]
	ldrh	w0, [x19,1132]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2477
	bl	pm_free_sblk
	ldr	x2, [x20,2848]
	ldrh	w1, [x19,1132]
	ldrh	w3, [x2,688]
	sub	w1, w1, #1
	cmp	w3, w1
	blt	.L2477
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2479
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 129
	add	x1, x1, 568
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	pm_free_sblk
	ldr	x1, [x20,2848]
	add	x0, x1, x0, uxth 1
	ldrh	w21, [x0,416]
.L2479:
	bl	pm_select_ram_region
	uxth	x20, w0
	add	x0, x23, :lo12:.LANCHOR0
	lsl	x1, x20, 4
	add	x0, x0, 3872
	mov	x22, x20
	add	x19, x0, x1
	ldrh	w0, [x0,x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2480
	ldr	x1, [x19,8]
	cbz	x1, .L2480
	ldrsh	w2, [x19,2]
	tbz	w2, #31, .L2480
	bl	pm_write_page
	ldrh	w0, [x19,2]
	and	w0, w0, 32767
	strh	w0, [x19,2]
.L2480:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w19, 0
	add	x0, x23, 3872
	mov	w24, 21
	mov	w25, 1
	add	x20, x0, x20, lsl 4
.L2481:
	ldr	x0, [x23,2848]
	ldrh	w1, [x0,698]
	cmp	w1, w19
	bls	.L2487
	ldrh	w1, [x23,3314]
	add	x0, x0, x19, sxtw 2
	ldr	w0, [x0,704]
	lsr	w0, w0, w1
	sub	w1, w24, w1
	lsl	w1, w25, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x23,3312]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L2482
	mov	w1, w22
	mov	w0, w19
	bl	load_l2p_region
	ldr	x1, [x20,8]
	mov	w0, w19
	bl	pm_write_page
	mov	w0, -1
	strh	w0, [x20]
.L2482:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2481
.L2487:
	bl	pm_free_sblk
.L2477:
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
	add	x1, x19, 3872
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,3864]
	cbz	w0, .L2489
	bl	pm_gc
	str	wzr, [x19,3864]
.L2489:
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
	add	x20, x20, 3872
.L2495:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2494
	bl	pm_flush_id
.L2494:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2495
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
	strb	w0, [x22,3280]
	mov	w0, 64
	str	wzr, [x20,3864]
	add	x20, x20, 3872
	bl	ftl_malloc
	mov	w24, -1
	str	x0, [x22,3272]
	add	x22, x20, 512
.L2501:
	strh	w24, [x20]
	strh	wzr, [x20,2]
	cbz	w23, .L2500
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2816]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x20,8]
.L2500:
	add	x20, x20, 16
	cmp	x20, x22
	bne	.L2501
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR7
	mov	w4, 4
	ldr	x1, [x20,2848]
	ldr	x25, [x21,3272]
	ldr	x23, [x20,3880]
	ldrb	w0, [x1,694]
	mov	x3, x25
	ldrh	w1, [x1,692]
	mov	x2, x23
	bl	flash_get_last_written_page
	sxth	w21, w0
	ldr	x1, [x20,2848]
	mov	w24, w21
	ldrh	w2, [x1,696]
	cmp	w2, w21
	bgt	.L2502
	adrp	x0, .LC192
	ldrh	w1, [x1,692]
	mov	w3, w21
	add	x0, x0, :lo12:.LC192
	adrp	x26, .LC193
	add	w21, w21, 1
	bl	printk
	add	x26, x26, :lo12:.LC193
	ldr	x0, [x20,2848]
	ldrsh	w20, [x0,696]
.L2503:
	cmp	w20, w21
	bge	.L2516
	add	x27, x19, :lo12:.LANCHOR0
	mov	x2, x23
	mov	x3, x25
	ldr	x0, [x27,2848]
	ldrh	w22, [x27,3318]
	ldrb	w4, [x27,2816]
	ldrh	w1, [x0,692]
	ldrb	w0, [x0,694]
	madd	w22, w1, w22, w20
	mov	w1, w22
	bl	flash_read_page_en
	mov	w28, w0
	ldr	x3, [x27,2848]
	mov	x0, x26
	mov	w2, w22
	ldr	w1, [x3,48]
	add	w1, w1, 1
	str	w1, [x3,48]
	ldrh	w3, [x3,694]
	ldr	w1, [x25]
	bl	printk
	cmp	w28, 512
	beq	.L2504
	cmn	w28, #1
	beq	.L2504
	ldr	x1, [x27,2848]
	ldr	w0, [x25]
	ldrh	w2, [x1,698]
	cmp	w0, w2
	bcs	.L2504
	add	x0, x1, x0, uxtw 2
	str	w22, [x0,704]
.L2504:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2503
.L2516:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR6-127
	mov	w1, 1
	add	w24, w24, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-127]
	ldr	x0, [x19,2848]
	strh	w24, [x0,696]
	bl	pm_free_sblk
	ldr	x1, [x19,3880]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3880]
	mov	w0, -1
	bl	pm_write_page
.L2502:
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
	ldrb	w4, [x3,2816]
	ldr	w2, [x3,2824]
	add	x3, x3, 3872
	lsl	w20, w4, 7
	lsl	w24, w4, 7
	cmp	w0, w2
	udiv	w20, w0, w20
	uxth	w25, w20
	msub	w24, w25, w24, w0
	uxth	x24, w24
	bcc	.L2529
	adrp	x0, .LC194
	mov	w1, w5
	add	x0, x0, :lo12:.LC194
	bl	printk
	mov	w0, -1
	b	.L2519
.L2533:
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L2521
.L2520:
	add	w19, w19, 1
	add	x3, x3, 16
	uxth	x19, w19
	cmp	w19, 32
	beq	.L2532
.L2529:
	ldr	x0, [x3,8]
	cbnz	x0, .L2533
	b	.L2520
.L2532:
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR0
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 3872
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2534
.L2525:
	adrp	x0, .LANCHOR7+3284
	strb	w19, [x0,#:lo12:.LANCHOR7+3284]
	mov	w0, w25
	bl	load_l2p_region
.L2521:
	cbnz	w23, .L2522
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,3880]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x22]
	b	.L2523
.L2522:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x22]
	add	x0, x0, 3872
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w2, [x1,x24,lsl 2]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
	adrp	x0, .LANCHOR4+32
	strb	w20, [x0,#:lo12:.LANCHOR4+32]
.L2523:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 32767
	add	x21, x21, 3872
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2519
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2519
.L2534:
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2525
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
	b	.L2525
.L2519:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	ftl_read_ahead
	.type	ftl_read_ahead, %function
ftl_read_ahead:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp,48]
	add	x23, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	mov	w21, 0
	ldr	w0, [x23,2868]
	cmn	w0, #1
	beq	.L2536
	mov	w0, w21
	bl	buf_alloc
	mov	x19, x0
	mov	w0, -1
	cbz	x19, .L2545
	ldr	w22, [x23,2868]
	str	w22, [x19,36]
	str	w0, [x23,2868]
	mov	w0, w22
	ldr	x24, [x19,8]
	ldr	x23, [x19,24]
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2538
	mov	w0, w22
	add	x1, x29, 76
	mov	w2, w21
	bl	pm_log2phys
.L2538:
	ldr	w1, [x29,76]
	str	w1, [x19,40]
	str	w1, [x19,44]
	cmn	w1, #1
	bne	.L2539
	add	x20, x20, :lo12:.LANCHOR0
	mov	x0, x24
	mov	w1, 0
	ldrb	w2, [x20,2816]
	lsl	w2, w2, 9
	bl	ftl_memset
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2550
.L2539:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w24, [x19,52]
	cmn	w24, #1
	bne	.L2540
	ldrb	w0, [x19,2]
	add	x20, x20, :lo12:.LANCHOR0
	and	w0, w0, -9
	strb	w0, [x19,2]
	ldr	x1, [x20,80]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2541
.L2540:
	cmp	w24, 256
	bne	.L2541
	add	x20, x20, :lo12:.LANCHOR0
	mov	w5, 21
	mov	w21, 1
	ldr	w4, [x29,76]
	ldr	w2, [x19,36]
	adrp	x0, .LC195
	ldrh	w3, [x20,3314]
	add	x0, x0, :lo12:.LC195
	ldrb	w20, [x20,3312]
	sub	w5, w5, w3
	lsr	w3, w4, w3
	lsl	w5, w21, w5
	sub	w5, w5, #1
	and	w3, w5, w3
	udiv	w20, w3, w20
	ldr	w3, [x19,40]
	uxth	w1, w20
	bl	printk
	mov	w0, w20
	mov	w1, w21
	mov	w2, 0
	bl	gc_add_sblk
.L2541:
	ldr	w5, [x23,4]
	cmp	w5, w22
	beq	.L2542
	adrp	x0, .LC196
	ldrb	w1, [x19,1]
	ldr	w3, [x29,76]
	add	x0, x0, :lo12:.LC196
	ldr	w4, [x23]
	mov	w2, w22
	ldr	w6, [x23,8]
	ldr	w7, [x23,12]
	bl	printk
.L2542:
	ldr	w0, [x23,4]
	cmp	w0, w22
	cset	w21, ne
	cbnz	w21, .L2547
	cmn	w24, #1
	bne	.L2536
.L2547:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 576
	mov	w2, 874
	bl	printk
.L2550:
	mov	w21, 0
	b	.L2536
.L2545:
	mov	w21, w0
.L2536:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_read_ahead, .-ftl_read_ahead
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	sub	sp, sp, #208
	stp	x29, x30, [sp,32]
	add	x29, sp, 32
	stp	x19, x20, [sp,48]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp,80]
	add	x24, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,64]
	stp	x25, x26, [sp,96]
	stp	x27, x28, [sp,112]
	adrp	x22, .LANCHOR4
	strb	wzr, [x24,3241]
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x20, [x24,80]
	add	x21, x20, 80
	strb	wzr, [x0,1098]
	bl	gc_init
	ldrh	w1, [x20,80]
	mov	w0, 65535
	str	x22, [x29,152]
	cmp	w1, w0
	ldr	x0, [x24,80]
	beq	.L2552
	mov	w1, -1
	strh	w1, [x0,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldrb	w0, [x21,9]
	ldrh	w1, [x24,3318]
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x24,3225]
	str	w0, [x29,168]
	cmp	w1, 3
	bne	.L2553
	ldrh	w1, [x24,3276]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
	b	.L2599
.L2553:
	cmp	w1, 2
	bne	.L2554
	ldrh	w1, [x24,3276]
	add	w0, w0, w1, lsl 1
	sub	w0, w0, #1
	orr	w0, w0, 33554432
.L2599:
	str	w0, [x23,40]
.L2554:
	mov	x0, x23
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2555
	cmn	w0, #1
	beq	.L2555
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2556
.L2555:
	mov	x0, x23
	bl	buf_free
	ldr	x7, [x23,24]
	ldr	x6, [x23,8]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	w0, [x7,8]
	str	w0, [sp,8]
	ldr	w0, [x7,12]
	str	w0, [sp,16]
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x23,40]
	ldr	w2, [x23,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2602
.L2556:
	add	x24, x19, :lo12:.LANCHOR0
	ldr	x0, [x29,152]
	ldr	x1, [x23,8]
	add	x25, x0, :lo12:.LANCHOR4
	ldrb	w2, [x24,3244]
	ldrh	w0, [x24,3242]
	mul	w2, w0, w2
	ldr	x0, [x25,1104]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x24,3242]
	ldrb	w2, [x24,3244]
	ldr	x1, [x23,8]
	mul	w2, w0, w2
	add	x1, x1, x2, sxtw 2
	ldr	x0, [x25,1112]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w27, [x24,3225]
	ldrh	w26, [x24,3276]
	cmp	w27, 2
	bne	.L2558
	lsl	w26, w26, 1
	mov	w27, 1
.L2558:
	mov	w22, 0
	adrp	x24, .LC198
	add	x0, x24, :lo12:.LC198
	mov	w25, w22
	str	x0, [x29,128]
	b	.L2559
.L2604:
	ldr	x0, [x0,1112]
	ldr	w1, [x0,x12]
	ldr	w0, [x7,8]
	cmp	w1, w0
	bne	.L2563
.L2588:
	add	w25, w25, 1
	add	w28, w28, 1
.L2567:
	cmp	w28, w27
	bhi	.L2603
	ldr	x0, [x29,144]
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w2, [x1,3318]
	ldrb	w1, [x1,3225]
	ldrh	w0, [x21,x0,lsl 1]
	cmp	w1, 3
	mul	w0, w0, w2
	str	w0, [x29,168]
	bne	.L2560
	add	w0, w0, w22
	orr	w0, w0, w28, lsl 24
.L2600:
	str	w0, [x23,40]
	mov	w1, 1
	mov	x0, x23
	bl	sblk_read_page
	ldr	x0, [x29,152]
	sbfiz	x12, x25, 2, 32
	ldr	x7, [x23,24]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w6, [x7,4]
	ldr	x1, [x0,1104]
	ldr	w3, [x1,x12]
	cmp	w3, w6
	beq	.L2604
.L2563:
	ldr	x0, [x29,152]
	str	x12, [x29,120]
	add	x13, x0, :lo12:.LANCHOR4
	ldr	w0, [x7,12]
	str	x13, [x29,112]
	ldr	x4, [x13,1112]
	str	w0, [sp]
	ldr	x0, [x29,128]
	ldr	w4, [x4,x12]
	ldr	w5, [x7]
	ldr	w1, [x23,40]
	ldr	w2, [x23,52]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x13, [x29,112]
	ldr	x12, [x29,120]
	ldr	x0, [x13,1104]
	ldr	w0, [x0,x12]
	cmn	w0, #1
	beq	.L2588
	mov	x0, x23
	bl	buf_free
.L2602:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	mov	w28, 0
	ldr	x1, [x0,72]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,80]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L2557
.L2566:
	add	w22, w22, 1
.L2559:
	cmp	w22, w26
	beq	.L2568
	sub	w0, w22, #1
	mov	w24, 0
	str	w0, [x29,140]
.L2569:
	ldrb	w0, [x21,9]
	cmp	w24, w0
	bge	.L2566
	sxtw	x0, w24
	mov	w28, 1
	str	x0, [x29,144]
	b	.L2567
.L2560:
	cmp	w1, 2
	bne	.L2562
	ldr	w1, [x29,140]
	add	w0, w0, w1
	add	w0, w0, w28
	orr	w0, w0, 33554432
	b	.L2600
.L2562:
	add	w0, w0, w22
	b	.L2600
.L2603:
	add	w24, w24, 1
	b	.L2569
.L2568:
	mov	x0, x23
	mov	w28, 0
	mov	w23, w28
	bl	buf_free
	str	w28, [x29,144]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,9]
	ldrh	w3, [x20,80]
	ldr	x2, [x0,72]
	ldrh	w0, [x0,3242]
	mul	w0, w1, w0
	strh	w0, [x2,x3,lsl 1]
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	str	x0, [x29,104]
.L2570:
	cmp	w23, w26
	beq	.L2583
	sub	w0, w23, #1
	mov	w25, 0
	str	w0, [x29,112]
.L2584:
	ldrb	w0, [x21,9]
	cmp	w25, w0
	bge	.L2581
	sxtw	x0, w25
	mov	w24, 1
	str	x0, [x29,120]
.L2582:
	cmp	w24, w27
	bhi	.L2605
	ldr	x1, [x29,152]
	ldr	w0, [x29,144]
	add	x1, x1, :lo12:.LANCHOR4
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1104]
	ldr	w22, [x2,x0]
	cmn	w22, #1
	beq	.L2571
	ldr	x1, [x1,1112]
	ldr	w0, [x1,x0]
	str	w0, [x29,140]
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2572
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2572:
	ldr	x0, [x29,120]
	add	x0, x0, 8
	ldrh	w3, [x21,x0,lsl 1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,3318]
	ldrb	w0, [x0,3225]
	cmp	w0, 3
	mul	w3, w3, w1
	bne	.L2573
	add	w3, w3, w23
	orr	w3, w3, w24, lsl 24
	b	.L2601
.L2573:
	cmp	w0, 2
	bne	.L2575
	ldr	w0, [x29,112]
	add	w3, w3, w0
	add	w3, w3, w24
	orr	w3, w3, 33554432
	b	.L2601
.L2575:
	add	w3, w3, w23
.L2601:
	add	x0, x19, :lo12:.LANCHOR0
	str	w3, [x29,168]
	ldr	w1, [x29,140]
	mov	w4, 21
	mov	w2, 1
	ldrh	w3, [x0,3314]
	ldrb	w0, [x0,3312]
	lsr	w1, w1, w3
	sub	w3, w4, w3
	lsl	w3, w2, w3
	sub	w3, w3, #1
	and	w3, w1, w3
	ldr	w1, [x29,140]
	udiv	w3, w3, w0
	uxth	w0, w3
	str	w0, [x29,128]
	ldr	w0, [x29,172]
	cmp	w0, w1
	bne	.L2576
	ldr	x0, [x29,104]
	mov	w1, w22
	ldr	w2, [x29,168]
	bl	pm_ppa_update_check
	cbz	w0, .L2577
.L2598:
	b	.L2598
.L2577:
	mov	w0, w22
	add	x1, x29, 168
	mov	w2, 1
	add	w28, w28, 1
	bl	pm_log2phys
	ldrh	w0, [x29,128]
	bl	ftl_vpn_decrement
	b	.L2579
.L2576:
	ldr	w1, [x29,168]
	cmp	w0, w1
	csinc	w28, w28, w28, ne
.L2579:
	ldr	w0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,144]
	ldr	w0, [x29,128]
	ldr	x1, [x2,64]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L2571
	lsl	x0, x0, 1
	ldr	x2, [x2,72]
	ldrh	w3, [x2,x0]
	cbz	w3, .L2571
	strh	w1, [x2,x0]
.L2571:
	add	w24, w24, 1
	b	.L2582
.L2605:
	add	w25, w25, 1
	b	.L2584
.L2581:
	add	w23, w23, 1
	b	.L2570
.L2583:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,72]
	strh	w28, [x0,x1,lsl 1]
.L2557:
	ldrh	w1, [x20,80]
	adrp	x0, .LC200
	mov	w2, w28
	add	x0, x0, :lo12:.LC200
	bl	printk
	mov	w0, -1
	strh	w0, [x20,80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldr	x0, [x0,80]
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2585
	bl	ftl_free_sblk
.L2585:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,2848]
	strh	w0, [x1,126]
	ldr	x1, [x19,80]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2551
.L2552:
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2551
	ldr	x1, [x24,2848]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L2587
	bl	pm_flush
	ldr	x0, [x24,80]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x24,2848]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L2587:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,80]
	strh	w1, [x0,130]
.L2551:
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
	adrp	x1, .LANCHOR5+513
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	strb	w0, [x1,#:lo12:.LANCHOR5+513]
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x1, .LC2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	strb	w0, [x20,2866]
	mov	w0, -1
	str	w0, [x20,2868]
	adrp	x0, .LC1
	add	x1, x1, :lo12:.LC2
	add	x0, x0, :lo12:.LC1
	strb	wzr, [x20,2840]
	adrp	x21, .LANCHOR7
	strb	wzr, [x20,2841]
	bl	printk
	adrp	x8, .LANCHOR2
	ldrb	w4, [x20,98]
	add	x2, x8, :lo12:.LANCHOR2
	strb	w4, [x20,3320]
	ldrh	w5, [x20,210]
	strh	w5, [x20,3318]
	ldrb	w3, [x2,389]
	strb	w3, [x20,3312]
	ldrb	w6, [x2,388]
	mul	w3, w3, w4
	ldrh	w1, [x2,386]
	ldrb	w0, [x2,385]
	uxtb	w4, w3
	add	x3, x21, :lo12:.LANCHOR7
	ldrh	w7, [x2,390]
	ubfiz	w11, w0, 9, 7
	sdiv	w9, w1, w6
	strb	w6, [x20,3225]
	strb	w0, [x20,2816]
	mov	w2, 0
	strh	w1, [x20,3242]
	strh	w9, [x20,3276]
	strb	w4, [x20,3244]
	strh	w7, [x20,56]
	mov	x20, x8
	strh	w11, [x3,3282]
	mov	w3, 1
.L2607:
	cmp	w3, w5
	bhi	.L2671
	add	w2, w2, 1
	lsl	w3, w3, 1
	uxth	w2, w2
	b	.L2607
.L2671:
	mul	w3, w0, w1
	mov	w1, 57344
	add	x8, x19, :lo12:.LANCHOR0
	movk	w1, 0x1c, lsl 16
	sub	w2, w2, #1
	mul	w5, w3, w7
	mul	w1, w4, w1
	strh	w2, [x8,3314]
	lsr	w2, w5, 21
	mul	w5, w5, w4
	mul	w1, w1, w2
	str	w5, [x8,2820]
	add	x10, x21, :lo12:.LANCHOR7
	add	w5, w1, 24576
	adrp	x22, .LANCHOR4
	mul	w3, w3, w4
	udiv	w2, w5, w0
	sub	w0, w11, #1
	str	w2, [x8,2824]
	sub	w3, w3, #1
	str	w1, [x8,4]
	str	w5, [x10,3288]
	add	w0, w0, w2, lsl 2
	mul	w2, w9, w4
	udiv	w0, w0, w11
	add	x11, x22, :lo12:.LANCHOR4
	strh	w0, [x10,3266]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w2
	strh	w0, [x11,1132]
	add	w0, w7, w7, lsl 1
	asr	w0, w0, 2
	strh	w0, [x10,3292]
	udiv	w3, w5, w3
	lsr	w0, w7, 4
	cmp	w0, 79
	strh	w0, [x8,3280]
	add	w3, w3, 8
	strh	w3, [x8,3282]
	bhi	.L2609
	mov	w0, 80
	strh	w0, [x8,3280]
.L2609:
	add	x0, x21, :lo12:.LANCHOR7
	mov	w2, 4000
	cmp	w6, 2
	strh	w2, [x0,3294]
	mov	w2, 50
	strh	w2, [x0,3296]
	add	x0, x22, :lo12:.LANCHOR4
	mov	w2, 1024
	strh	w2, [x0,1128]
	mov	w2, 64
	strh	w2, [x0,1130]
	beq	.L2610
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3240]
	cbz	w0, .L2611
.L2610:
	add	x2, x21, :lo12:.LANCHOR7
	mov	w0, 150
	mov	w3, 128
	strh	w0, [x2,3296]
	add	x0, x22, :lo12:.LANCHOR4
	strh	w3, [x0,1130]
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w3, [x3,97]
	cbnz	w3, .L2611
	mov	w3, 600
	strh	w3, [x2,3294]
	mov	w2, 512
	strh	w2, [x0,1128]
.L2611:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2613
	adrp	x0, .LC201
	add	x0, x0, :lo12:.LC201
	bl	printk
.L2613:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2614
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC202
	add	x0, x0, :lo12:.LC202
	ldr	w1, [x1,2820]
	bl	printk
.L2614:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2615
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC203
	add	x0, x0, :lo12:.LC203
	ldr	w1, [x1,2824]
	bl	printk
.L2615:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2616
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC204
	add	x0, x0, :lo12:.LC204
	ldr	w1, [x1,3288]
	bl	printk
.L2616:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2617
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC205
	add	x0, x0, :lo12:.LC205
	ldrh	w1, [x1,3266]
	bl	printk
.L2617:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2618
	add	x1, x21, :lo12:.LANCHOR7
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	ldrh	w1, [x1,3282]
	bl	printk
.L2618:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2619
	add	x1, x22, :lo12:.LANCHOR4
	adrp	x0, .LC207
	add	x0, x0, :lo12:.LC207
	ldrh	w1, [x1,1132]
	bl	printk
.L2619:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2620
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC208
	add	x0, x0, :lo12:.LC208
	ldrh	w1, [x1,3280]
	bl	printk
.L2620:
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2621
	add	x1, x19, :lo12:.LANCHOR0
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	ldrh	w1, [x1,3282]
	bl	printk
.L2621:
	add	x22, x19, :lo12:.LANCHOR0
	bl	buf_init
	mov	w0, 6
	adrp	x23, .LANCHOR6
	ldrh	w1, [x22,56]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x22,8]
	ldrb	w1, [x22,3244]
	ldrh	w0, [x22,3276]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x22,3848]
	ldrb	w1, [x22,3244]
	add	x2, x23, :lo12:.LANCHOR6
	ldrh	w0, [x22,3276]
	sub	x2, x2, #64
	str	x2, [x22,3840]
	mul	w1, w0, w1
	ldrh	w0, [x22,56]
	lsl	w1, w1, 1
	add	x3, x2, x1, sxtw 2
	str	x3, [x22,72]
	add	w1, w1, w0, lsr 1
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	add	x1, x2, x1, sxtw 2
	str	x1, [x22,80]
	tbz	x0, 12, .L2622
	adrp	x0, .LC210
	add	x0, x0, :lo12:.LC210
	bl	printk
.L2622:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR7
	ldrh	w0, [x2,3276]
	ldrb	w22, [x2,3244]
	ldrh	w1, [x2,56]
	ldrh	w24, [x3,3266]
	mul	w22, w0, w22
	ldr	w0, [x20,#:lo12:.LANCHOR2]
	lsl	w24, w24, 2
	add	w24, w24, w1, lsl 2
	add	w22, w1, w22, lsl 2
	add	w24, w24, 704
	lsl	w22, w22, 1
	add	w22, w22, 612
	tbz	x0, 12, .L2623
	adrp	x0, .LC211
	ldrh	w3, [x3,3282]
	add	x0, x0, :lo12:.LC211
	mov	w1, w22
	mov	w2, w24
	bl	printk
.L2623:
	add	x21, x21, :lo12:.LANCHOR7
	ldrh	w0, [x21,3282]
	cmp	w22, w0
	bhi	.L2625
	cmp	w24, w0
	bls	.L2624
.L2625:
.L2626:
	b	.L2626
.L2624:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L2627
	bl	ftl_ext_info_init
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR6
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	x0, [x19,80]
	mov	x1, 0
	add	x0, x0, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,80]
	add	x0, x1, 48
	add	x1, x1, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,2848]
	ldr	w0, [x1,8]
	add	w0, w0, 16
	str	w0, [x1,8]
	ldr	x0, [x19,80]
	add	x0, x0, 16
	bl	ftl_info_data_recovery
	ldr	x0, [x19,80]
	add	x0, x0, 48
	bl	ftl_info_data_recovery
	ldr	x0, [x19,80]
	add	x0, x0, 80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	w0, 1
	bl	ftl_total_vpn_update
	ldrb	w0, [x23,-127]
	cbz	w0, .L2628
	ldr	x1, [x19,2848]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L2628:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L2627:
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
	str	x21, [sp,32]
	str	x0, [x19,1136]
	mov	w0, 136
	bl	ftl_malloc
	str	x0, [x19,1144]
	cbnz	x0, .L2673
.L2675:
	mov	w0, -1
	b	.L2674
.L2673:
	adrp	x21, .LANCHOR7
	mov	w0, 2048
	add	x21, x21, :lo12:.LANCHOR7
	bl	ftl_malloc
	str	xzr, [x19,1152]
	add	x1, x21, 3312
	str	x0, [x21,3304]
	add	x0, x19, 1152
	str	xzr, [x21,3312]
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1152]
	cbz	x0, .L2675
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x21,3304]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1144]
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
	ldr	x0, [x19,1152]
	bl	nand_flash_init
	cbnz	w0, .L2676
	bl	zftl_init
	mov	w19, w0
	bl	zftl_proc_debug_init
	b	.L2677
.L2676:
	ldr	x0, [x19,1144]
	adrp	x1, ftl_deinit
	add	x1, x1, :lo12:ftl_deinit
	str	x1, [x0,80]
	adrp	x1, ftl_cache_flush
	add	x1, x1, :lo12:ftl_cache_flush
	str	x1, [x0,64]
	adrp	x1, ftl_flash_suspend
	add	x1, x1, :lo12:ftl_flash_suspend
	str	x1, [x0,88]
	adrp	x1, ftl_flash_resume
	add	x1, x1, :lo12:ftl_flash_resume
	str	x1, [x0,96]
	adrp	x1, ftl_get_density
	add	x1, x1, :lo12:ftl_get_density
	str	x1, [x0,72]
	adrp	x1, ftl_read_flash_info
	add	x1, x1, :lo12:ftl_read_flash_info
	str	x1, [x0,112]
	adrp	x1, ftl_read
	add	x1, x1, :lo12:ftl_read
	str	x1, [x0,40]
	adrp	x1, ftl_write
	add	x1, x1, :lo12:ftl_write
	str	x1, [x0,48]
	adrp	x1, ftl_sys_read
	add	x1, x1, :lo12:ftl_sys_read
	str	x1, [x0,24]
	adrp	x1, ftl_sys_write
	add	x1, x1, :lo12:ftl_sys_write
	str	x1, [x0,32]
	adrp	x1, ftl_vendor_read
	add	x1, x1, :lo12:ftl_vendor_read
	str	x1, [x0,8]
	adrp	x1, ftl_vendor_write
	add	x1, x1, :lo12:ftl_vendor_write
	str	x1, [x0,16]
	adrp	x1, ftl_nandc_get_irq_status
	add	x1, x1, :lo12:ftl_nandc_get_irq_status
	str	x1, [x0,120]
	adrp	x1, ftl_proc_ftl_read
	add	x1, x1, :lo12:ftl_proc_ftl_read
	str	x1, [x0,128]
	adrp	x1, ftl_do_gc
	add	x1, x1, :lo12:ftl_do_gc
	str	x1, [x0,104]
	adrp	x1, ftl_discard
	add	x1, x1, :lo12:ftl_discard
	str	x1, [x0,56]
	ldr	x0, [x19,1152]
	bl	FlashInit
	mov	w19, w0
	adrp	x0, .LANCHOR2
	adrp	x1, gNandParaInfo
	add	x0, x0, :lo12:.LANCHOR2
	add	x1, x1, :lo12:gNandParaInfo
	add	x0, x0, 376
	mov	w2, 32
	bl	ftl_memcpy
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L2678
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L2679
.L2678:
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x0, .LANCHOR0+97
	strb	wzr, [x20,1232]
	strb	wzr, [x0,#:lo12:.LANCHOR0+97]
.L2679:
	cbnz	w19, .L2677
	bl	FtlInit
	mov	w19, w0
.L2677:
	adrp	x0, .LC212
	mov	w1, w19
	add	x0, x0, :lo12:.LC212
	bl	printk
	mov	w0, w19
.L2674:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
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
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	adrp	x21, .LANCHOR2
	ldr	x20, [x0,80]
	ldrh	w1, [x0,3242]
	add	x28, x20, 80
	ldrb	w0, [x28,9]
	mul	w0, w1, w0
	str	w0, [x29,140]
	bl	timer_get_time
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2684
	adrp	x0, .LC213
	ldrh	w1, [x20,80]
	add	x0, x0, :lo12:.LC213
	bl	printk
.L2684:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	w2, [x29,140]
	mov	w22, 0
	mov	w23, w22
	ldr	x0, [x0,72]
	sub	w2, w2, #1
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	str	x0, [x29,120]
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	str	x0, [x29,112]
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	str	x0, [x29,104]
.L2685:
	ldr	w0, [x29,140]
	cmp	w23, w0
	beq	.L2719
	adrp	x24, .LANCHOR4
	str	x24, [x29,128]
	add	x0, x24, :lo12:.LANCHOR4
	ldr	x0, [x0,1104]
	ldr	w2, [x0,w23,sxtw 2]
	cmn	w2, #1
	beq	.L2686
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2816]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,136]
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2687
	ldr	x0, [x29,104]
	mov	w3, w23
	ldr	w1, [x29,136]
	bl	printk
.L2687:
	mov	w25, w23
.L2693:
	ldr	x0, [x29,128]
	sbfiz	x26, x25, 2, 32
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x0, [x1,1104]
	ldr	w24, [x0,x26]
	cmn	w24, #1
	beq	.L2688
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrb	w0, [x0,2816]
	lsl	w0, w0, 7
	udiv	w0, w24, w0
	cmp	w2, w0, uxth
	bne	.L2688
	ldr	x0, [x1,1112]
	ldr	w27, [x0,x26]
	mov	w0, w24
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L2689
	mov	w0, w24
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L2689:
	ldr	w3, [x29,156]
	cmp	w3, w27
	bne	.L2690
	ldr	x0, [x29,128]
	add	x1, x29, 152
	mov	w2, 1
	add	w22, w22, 1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1120]
	ldr	w0, [x0,x26]
	str	w0, [x29,152]
	mov	w0, w24
	bl	pm_log2phys
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w0, 1
	ldrh	w3, [x1,3314]
	sub	w2, w2, w3
	lsr	w3, w27, w3
	lsl	w2, w0, w2
	sub	w2, w2, #1
	and	w3, w2, w3
	ldrb	w2, [x1,3312]
	udiv	w2, w3, w2
	uxth	w2, w2
	str	x2, [x29,96]
	mov	w0, w2
	bl	ftl_vpn_decrement
	ldrh	w0, [x28]
	ldr	x2, [x29,96]
	cmp	w0, w2
	bne	.L2691
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2691
	ldr	x0, [x29,112]
	mov	w1, w24
	ldr	w3, [x29,156]
	mov	w2, w27
	bl	printk
	b	.L2691
.L2690:
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2692
	ldr	x0, [x29,120]
	mov	w1, w24
	mov	w2, w27
	bl	printk
.L2692:
	ldrh	w0, [x28]
	bl	ftl_vpn_decrement
.L2691:
	ldr	x0, [x29,128]
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1104]
	str	w1, [x0,x26]
	bl	timer_get_time
.L2688:
	ldr	w0, [x29,140]
	add	w25, w25, 1
	cmp	w25, w0
	bne	.L2693
.L2686:
	add	w23, w23, 1
	b	.L2685
.L2719:
	ldr	w0, [x21,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2695
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x20,80]
	ldr	x1, [x0,80]
	ldr	x2, [x0,72]
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
.L2695:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w22
	beq	.L2696
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 592
	mov	w2, 463
	bl	printk
.L2696:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x19,72]
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
	mov	w4, 0
	mov	w6, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x3, x19, :lo12:.LANCHOR0
	mov	w5, w4
	add	x2, x3, 2888
	stp	x21, x22, [sp,32]
	ldrb	w0, [x2,4]
	add	x1, x2, x0, sxtw 1
	ldrb	w7, [x3,3244]
	ldrh	w1, [x1,40]
	ldrh	w3, [x2,2]
.L2721:
	cmp	w1, w6
	bne	.L2763
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w7
	bne	.L2722
	add	w3, w3, 1
	mov	w4, 1
	mov	w0, 0
	uxth	w3, w3
.L2722:
	add	x1, x2, x0, sxtw 1
	mov	w5, 1
	ldrh	w1, [x1,40]
	b	.L2721
.L2763:
	cbz	w5, .L2724
	add	x2, x19, :lo12:.LANCHOR0
	strb	w0, [x2,2892]
.L2724:
	cbz	w4, .L2725
	add	x0, x19, :lo12:.LANCHOR0
	strh	w3, [x0,2890]
.L2725:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,3225]
	ldrh	w20, [x0,3318]
	cmp	w2, 2
	mul	w1, w1, w20
	bne	.L2726
	ldrb	w2, [x0,2894]
	add	x0, x0, 2888
	cbnz	w2, .L2726
	ldrh	w20, [x0,2]
	add	w20, w1, w20, lsl 1
	b	.L2727
.L2726:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w20, [x0,2890]
	add	w20, w1, w20
.L2727:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2888
	ldrb	w0, [x2,2892]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x2,2892]
	ldrb	w2, [x2,3244]
	cmp	w2, w0
	bne	.L2728
	ldrh	w0, [x1,2]
	strb	wzr, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L2728:
	mov	w0, 1
	mov	w21, 0
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x22, x0
.L2739:
	ldrb	w0, [x19,2894]
	cbnz	w21, .L2741
	cmp	w0, 3
	beq	.L2729
.L2741:
	cmp	w0, w21
	bgt	.L2731
.L2740:
	mov	x0, x22
	bl	buf_free
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L2731:
	ldrb	w1, [x19,3225]
	cmp	w1, 2
	bne	.L2732
	cbnz	w0, .L2732
	add	w0, w21, w20
	orr	w0, w0, 33554432
	b	.L2762
.L2732:
	orr	w0, w20, w21, lsl 24
.L2762:
	str	w0, [x22,40]
	mov	w1, 1
	mov	x0, x22
	bl	sblk_read_page
	ldr	w0, [x22,52]
	cmp	w0, 512
	beq	.L2736
	cmn	w0, #1
	beq	.L2736
	ldr	x0, [x22,24]
	ldr	w23, [x0,4]
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2737
	mov	w0, w23
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2737:
	ldr	w0, [x29,76]
	cmp	w20, w0
	bne	.L2736
	ldrh	w2, [x19,2912]
	ldr	x1, [x19,3216]
	str	w20, [x1,x2,lsl 2]
	ldrh	w1, [x19,2912]
	add	w1, w1, 1
	strh	w1, [x19,2912]
.L2736:
	ldrh	w0, [x19,2914]
	add	w0, w0, 1
	strh	w0, [x19,2914]
.L2729:
	add	w21, w21, 1
	cmp	w21, 4
	bne	.L2739
	b	.L2740
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_static_data
	.type	gc_scan_static_data, %function
gc_scan_static_data:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x1,80]
	ldr	w2, [x0,544]
	cmn	w2, #1
	beq	.L2765
	adrp	x21, .LANCHOR3
	adrp	x22, .LC0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w20, 11
	add	x22, x22, :lo12:.LC0
	add	x21, x21, 616
.L2766:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,80]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2767
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L2768
	ldrh	w0, [x24,3314]
	mov	w2, 21
	mov	w1, 1
	ldr	w3, [x29,76]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	lsr	w3, w3, w0
	sub	w2, w2, #1
	ldrb	w0, [x24,3312]
	and	w2, w2, w3
	udiv	w0, w2, w0
	mov	w2, 0
	bl	gc_add_sblk
.L2768:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,80]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L2769
	mov	x0, x22
	mov	x1, x21
	mov	w2, 1369
	bl	printk
.L2769:
	mov	x0, x23
	bl	buf_free
.L2767:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x1, [x2,80]
	ldr	w2, [x2,2824]
	ldr	w0, [x1,544]
	add	w0, w0, 1
	str	w0, [x1,544]
	cmp	w0, w2
	bcc	.L2770
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
	b	.L2764
.L2770:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L2764
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L2766
	b	.L2764
.L2765:
	ldr	x1, [x1,2848]
	ldr	w2, [x1,40]
	cmp	w2, 20480
	bls	.L2775
	ldr	w3, [x1,44]
	add	w3, w3, w2, lsr 10
	and	w2, w2, 1023
	str	w3, [x1,44]
	str	w2, [x1,40]
.L2775:
	ldr	w2, [x0,536]
	ldr	w3, [x0,12]
	add	w2, w2, 12959744
	add	w2, w2, 256
	cmp	w3, w2
	bhi	.L2776
	ldr	w2, [x0,540]
	ldr	w4, [x1,44]
	add	w2, w2, 98304
	add	w2, w2, 1696
	cmp	w4, w2
	bls	.L2764
.L2776:
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w3, [x0,536]
	str	wzr, [x0,544]
.L2764:
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
	ldr	x1, [x20,80]
	ldrh	w0, [x20,56]
	ldr	w1, [x1,608]
	cmp	w1, w0
	bcs	.L2781
	bl	timer_get_time
	ldr	x20, [x20,80]
	ldr	w1, [x20,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L2781
	bl	timer_get_time
	str	w0, [x20,604]
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1,80]
	ldrh	w3, [x1,56]
	ldr	w2, [x0,600]
	cmp	w2, w3
	bcs	.L2786
	ldr	x1, [x1,2848]
	ldrh	w1, [x1,134]
	cmp	w2, w1
	bcs	.L2787
.L2786:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2848]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L2787:
	ldr	w25, [x0,600]
	add	w1, w25, 1
	str	w1, [x0,600]
	ldr	w1, [x0,608]
	uxth	w20, w25
	add	w1, w1, 1
	str	w1, [x0,608]
	add	x1, x29, 128
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	uxth	w1, w0
	cbz	w1, .L2781
	add	x1, x19, :lo12:.LANCHOR0
	uxtw	x22, w20
	ldr	x24, [x1,64]
	add	x24, x24, x22, lsl 2
	ldrb	w3, [x24,2]
	tst	w3, 192
	and	w2, w3, 224
	beq	.L2788
	cmp	w2, 224
	beq	.L2788
	ldr	x1, [x1,80]
	ldrh	w3, [x1,16]
	cmp	w3, w20
	beq	.L2788
	ldrh	w3, [x1,48]
	cmp	w3, w20
	beq	.L2788
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L2789
.L2788:
	cbnz	w2, .L2781
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x22]
	cbz	w0, .L2790
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 640
	mov	w2, 1606
	bl	printk
.L2790:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,72]
	strh	wzr, [x0,x22]
	b	.L2781
.L2789:
	and	w0, w0, 65535
	add	x1, x29, 144
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w21, [x0,-16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2792
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 640
	mov	w2, 1612
	bl	printk
.L2792:
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w3, [x24,2]
	mov	w26, 1
	and	w3, w3, 224
	ldrh	w0, [x2,3276]
	cmp	w3, 160
	ldrh	w1, [x2,3318]
	sub	w0, w0, #1
	mul	w1, w21, w1
	uxth	w0, w0
	bne	.L2793
	ldrb	w3, [x2,3225]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2793
	ldrh	w0, [x2,3242]
	sub	w0, w0, #1
	uxth	w0, w0
.L2793:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x19, :lo12:.LANCHOR0
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,3242]
	mov	w1, 255
	ldrb	w2, [x21,3244]
	mul	w2, w0, w2
	ldr	x0, [x21,3216]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2805
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2794
.L2805:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L2818
.L2794:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2796
	mov	w0, w20
	mov	w1, 1
.L2818:
	bl	gc_add_sblk
	mov	x0, x23
	bl	buf_free
	b	.L2781
.L2796:
	ldrb	w0, [x21,3244]
	mov	x27, 0
	ldrh	w1, [x21,3276]
	and	w28, w25, 65535
	mov	w21, w27
	mul	w0, w0, w1
	mul	w26, w26, w0
	uxth	w0, w26
	ldr	x26, [x23,8]
	str	w0, [x29,108]
.L2797:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L2819
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L2798
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2799
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2799:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 21
	ldrh	w0, [x4,3314]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x29,124]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3312]
	and	w0, w2, w0
	udiv	w0, w0, w1
	cmp	w0, w28
	bne	.L2798
	add	w21, w21, 1
	uxth	w21, w21
.L2798:
	add	x27, x27, 1
	b	.L2797
.L2819:
	mov	x0, x23
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2801
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24,2]
	uxth	w1, w25
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,72]
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L2801:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,72]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L2781
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L2781:
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
	mov	w2, 65535
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x21, x22, [sp,80]
	uxth	w22, w0
	mov	w0, 0
	stp	x19, x20, [sp,64]
	stp	x23, x24, [sp,96]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	cmp	w22, w2
	beq	.L2821
	adrp	x23, .LANCHOR0
	add	x2, x23, :lo12:.LANCHOR0
	ldrh	w3, [x2,56]
	cmp	w3, w22
	bls	.L2821
	uxtw	x0, w22
	str	x0, [x29,128]
	lsl	x0, x0, 2
	str	x0, [x29,152]
	ldr	x0, [x2,64]
	mov	w26, 1
	ldr	x3, [x29,152]
	add	x0, x0, x3
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2822
	ldrb	w26, [x2,3225]
.L2822:
	str	x1, [x29,144]
	mov	w0, w22
	add	x1, x29, 192
	strh	w22, [x29,176]
	mov	w21, 0
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	add	x0, x23, :lo12:.LANCHOR0
	strb	w1, [x29,185]
	mov	w3, w26
	strh	wzr, [x29,178]
	uxtb	w4, w4
	ldrh	w2, [x0,3276]
	mov	w28, w21
	strb	wzr, [x29,181]
	mov	w19, w21
	strh	wzr, [x29,186]
	mul	w1, w1, w2
	strh	w1, [x29,182]
	ldr	x1, [x0,64]
	ldr	x0, [x29,152]
	add	x1, x1, x0
	adrp	x0, .LC219
	add	x0, x0, :lo12:.LC219
	ldrb	w2, [x1,2]
	mov	w1, w22
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	adrp	x0, .LC166
	str	w21, [x29,140]
	add	x0, x0, :lo12:.LC166
	str	x0, [x29,120]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 664
	str	x0, [x29,112]
.L2823:
	add	x20, x23, :lo12:.LANCHOR0
	ldrh	w0, [x20,3276]
	cmp	w0, w19
	bls	.L2839
	lsl	w0, w19, 1
	mov	w25, 0
	sub	w0, w0, #1
	str	w0, [x29,136]
.L2840:
	ldrb	w0, [x29,185]
	cmp	w0, w25
	bls	.L2837
	mov	w24, 1
.L2838:
	cmp	w24, w26
	bhi	.L2857
	add	x0, x29, 176
	add	x0, x0, x25, sxtw 1
	ldrh	w2, [x0,16]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2824
	add	x1, x23, :lo12:.LANCHOR0
	cmp	w26, 3
	ldrh	w0, [x1,3318]
	mul	w0, w2, w0
	add	w20, w0, w19
	bne	.L2825
	orr	w20, w20, w24, lsl 24
	b	.L2826
.L2825:
	cmp	w26, 2
	bne	.L2826
	ldr	w3, [x29,136]
	ldrb	w20, [x1,3225]
	add	w0, w0, w3
	add	w0, w0, w24
	orr	w20, w0, w20, lsl 24
.L2826:
	mov	w1, 1
	str	w20, [x27,40]
	mov	x0, x27
	str	x2, [x29,104]
	bl	sblk_read_page
	ldr	x7, [x27,8]
	mov	w3, w20
	ldr	x0, [x27,24]
	ldr	w9, [x27,52]
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
	beq	.L2844
	cmn	w9, #1
	bne	.L2828
.L2844:
	mov	w0, 1
	str	w0, [x29,140]
.L2828:
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x1, [x29,152]
	ldr	x0, [x0,64]
	add	x0, x0, x1
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 32
	beq	.L2824
	cmp	w0, 224
	cset	w2, eq
	cbnz	w2, .L2824
	ldr	x0, [x27,24]
	str	x2, [x29,104]
	ldr	w0, [x0,4]
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x2, [x29,104]
	bne	.L2830
	ldr	x0, [x27,24]
	add	x1, x29, 172
	ldr	w0, [x0,4]
	bl	pm_log2phys
.L2830:
	ldr	w0, [x29,172]
	cmp	w0, w20
	bne	.L2831
	ldr	x2, [x27,24]
	add	w21, w21, 1
	adrp	x0, .LC220
	mov	w1, w20
	add	x0, x0, :lo12:.LC220
	mov	w3, w21
	ldr	w2, [x2,4]
	bl	printk
.L2831:
	ldr	x0, [x29,144]
	cbz	x0, .L2833
	ubfiz	x20, x28, 2, 32
	ldr	w2, [x0,x20]
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L2834
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L2834
	adrp	x0, .LC221
	mov	w1, w28
	add	x0, x0, :lo12:.LC221
	bl	printk
.L2834:
	ldr	x0, [x29,144]
	ldr	x1, [x27,24]
	ldr	w0, [x0,x20]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L2833
	cmn	w0, #1
	beq	.L2833
	adrp	x0, .LC0
	ldr	x1, [x29,112]
	add	x0, x0, :lo12:.LC0
	mov	w2, 1175
	bl	printk
.L2833:
	add	w28, w28, 1
.L2824:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2838
.L2857:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2840
.L2837:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2823
.L2839:
	mov	x0, x27
	bl	buf_free
	ldr	x3, [x29,128]
	adrp	x0, .LC222
	ldr	x2, [x20,72]
	add	x0, x0, :lo12:.LC222
	mov	w1, w22
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w21
	bl	printk
	ldr	w0, [x29,140]
.L2821:
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
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	adrp	x0, .LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	mov	w24, w1
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w25, w2
	mov	x23, x3
	tbz	x0, 12, .L2859
	adrp	x0, .LC223
	mov	w1, w19
	add	x0, x0, :lo12:.LC223
	mov	w2, w24
	mov	w3, w25
	bl	printk
.L2859:
	cbnz	w19, .L2860
	adrp	x0, .LANCHOR0+4
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+4]
	b	.L2861
.L2860:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L2862
	lsl	w19, w19, 13
	mov	w2, 8192
.L2861:
	add	w1, w24, w25
	mov	w0, -1
	cmp	w1, w2
	bhi	.L2862
	adrp	x22, .LANCHOR0
	add	w24, w19, w24
	add	x1, x22, :lo12:.LANCHOR0
	add	w28, w24, w25
	str	wzr, [x29,152]
	ldr	x2, [x1,2848]
	ldr	w0, [x2,24]
	add	w0, w0, w25
	str	w0, [x2,24]
	ldrb	w0, [x1,2816]
	sub	w1, w28, #1
	udiv	w27, w24, w0
	mov	w21, w27
	udiv	w0, w1, w0
	str	w0, [x29,148]
	sub	w26, w0, w27
	add	w0, w26, 1
	str	w0, [x29,156]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,136]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 680
	str	x0, [x29,128]
.L2863:
	ldr	w0, [x29,156]
	cbz	w0, .L2909
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,148]
	cmp	w21, w27
	cset	w2, eq
	cmp	w21, w1
	ldrb	w0, [x0,2816]
	cset	w20, eq
	uxth	w19, w0
	cbnz	w20, .L2889
	cbz	w2, .L2864
	b	.L2908
.L2889:
	cbz	w2, .L2866
.L2908:
	udiv	w20, w24, w0
	msub	w20, w20, w0, w24
	uxth	w0, w25
	uxth	w20, w20
	sub	w19, w19, w20
	uxth	w19, w19
	cmp	w19, w25
	csel	w19, w0, w19, hi
	b	.L2864
.L2866:
	msub	w19, w0, w21, w28
	mov	w20, w2
	uxtb	w19, w19
.L2864:
	add	x1, x22, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 768
.L2869:
	ldr	w2, [x1,36]
	cmp	w2, w21
	bne	.L2867
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L2867
	add	x2, x22, :lo12:.LANCHOR0
	ubfiz	x1, x0, 6, 32
	add	x0, x2, x1
	ubfiz	x20, x20, 9, 16
	lsl	w2, w19, 9
	ubfiz	x19, x19, 9, 16
	ldr	x1, [x0,776]
	mov	x0, x23
	add	x23, x23, x19
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L2868
.L2867:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L2869
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2870
	mov	w0, w21
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2870:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L2871
	mov	w19, 0
.L2872:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2816]
	cmp	w19, w0
	bcs	.L2868
	madd	w0, w21, w0, w19
	cmp	w0, w24
	bcc	.L2873
	cmp	w0, w28
	bcs	.L2873
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	add	x23, x23, 512
	bl	ftl_memset
.L2873:
	add	w19, w19, 1
	b	.L2872
.L2871:
	mov	w0, 0
	bl	buf_alloc
	mov	x26, x0
	cbnz	x0, .L2875
	bl	ftl_read_ahead
	b	.L2863
.L2875:
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x2, [x1,2848]
	ldr	w0, [x2,40]
	add	w0, w0, 1
	str	w0, [x2,40]
	ldr	w0, [x29,172]
	str	w0, [x26,40]
	str	w0, [x26,44]
	ldrb	w0, [x1,2816]
	cmp	w0, w19
	bne	.L2877
	str	x23, [x26,16]
.L2877:
	mov	x0, x26
	mov	w1, 1
	bl	sblk_read_page
	ldr	w9, [x26,52]
	cmn	w9, #1
	bne	.L2878
	add	x0, x22, :lo12:.LANCHOR0
	str	w9, [x29,152]
	ldr	x1, [x0,80]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2879
.L2878:
	cmp	w9, 256
	bne	.L2879
	add	x1, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w5, 1
	ldr	w0, [x29,172]
	ldr	w3, [x26,40]
	ldrh	w4, [x1,3314]
	str	x9, [x29,104]
	sub	w2, w2, w4
	lsr	w4, w0, w4
	lsl	w2, w5, w2
	ldrb	w0, [x1,3312]
	sub	w2, w2, #1
	str	x5, [x29,112]
	and	w4, w2, w4
	ldr	w2, [x26,36]
	udiv	w4, w4, w0
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	str	x4, [x29,120]
	uxth	w1, w4
	bl	printk
	ldr	x4, [x29,120]
	mov	w2, 0
	ldr	x5, [x29,112]
	mov	w0, w4
	mov	w1, w5
	bl	gc_add_sblk
	ldr	x9, [x29,104]
.L2879:
	ldr	x7, [x26,24]
	ldr	w5, [x7,4]
	cmp	w5, w21
	beq	.L2880
	ldr	w4, [x7]
	adrp	x0, .LC196
	ldr	w6, [x7,8]
	mov	w2, w21
	ldrb	w1, [x26,1]
	add	x0, x0, :lo12:.LC196
	ldr	w3, [x29,172]
	ldr	w7, [x7,12]
	str	x9, [x29,120]
	bl	printk
	add	x3, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3314]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,172]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3312]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x9, [x29,120]
.L2880:
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w21
	bne	.L2890
	cmn	w9, #1
	bne	.L2881
.L2890:
	ldr	x0, [x29,136]
	mov	w2, 1030
	ldr	x1, [x29,128]
	bl	printk
.L2881:
	ldr	x0, [x26,16]
	cbnz	x0, .L2883
	ldr	x1, [x26,8]
	ubfiz	x20, x20, 9, 16
	mov	x0, x23
	lsl	w2, w19, 9
	add	x1, x1, x20
	bl	ftl_memcpy
.L2883:
	mov	x0, x26
	ubfiz	x19, x19, 9, 16
	add	x23, x23, x19
	bl	buf_free
.L2868:
	ldr	w0, [x29,156]
	add	w21, w21, 1
	sub	w0, w0, #1
	str	w0, [x29,156]
	b	.L2863
.L2909:
	bl	timer_get_time
	adrp	x1, .LANCHOR7+3320
	str	w0, [x1,#:lo12:.LANCHOR7+3320]
	ldr	w0, [x29,152]
.L2862:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
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
	.global	vpn_check
	.type	vpn_check, %function
vpn_check:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	timer_get_time
	adrp	x21, .LANCHOR7
	bl	dump_ftl_info
	adrp	x22, .LANCHOR3
	ldr	x3, [x19,80]
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	adrp	x23, .LC0
	add	x22, x22, :lo12:.LANCHOR3
	add	x23, x23, :lo12:.LC0
	ldrh	w1, [x3,16]
	add	x22, x22, 696
	ldrh	w2, [x3,48]
	ldrh	w3, [x3,80]
	bl	printk
	ldr	x0, [x19,80]
	ldr	x3, [x19,72]
	ldrh	w1, [x0,16]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,22]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	sub	w4, w3, w2
	bl	printk
	ldr	x0, [x19,80]
	ldr	x3, [x19,72]
	mov	w19, 0
	ldrh	w1, [x0,48]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,54]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	sub	w4, w3, w2
	bl	printk
	add	x0, x21, :lo12:.LANCHOR7
	mov	w1, 0
	add	x0, x0, 3328
	mov	w2, 4608
	bl	ftl_memset
.L2913:
	add	x24, x20, :lo12:.LANCHOR0
	ldr	w0, [x24,2824]
	cmp	w19, w0
	bcs	.L2941
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2914
	mov	w0, w19
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
	b	.L2915
.L2914:
	ldrh	w2, [x24,3314]
	mov	w1, 21
	lsr	w0, w0, w2
	sub	w1, w1, w2
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w0, w0, w1
	ldrb	w1, [x24,3312]
	udiv	w0, w0, w1
	ldrh	w1, [x24,56]
	cmp	w1, w0, uxth
	bhi	.L2915
	mov	x0, x23
	mov	x1, x22
	mov	w2, 1256
	bl	printk
.L2915:
	ldr	w2, [x29,124]
	cmn	w2, #1
	beq	.L2917
	add	x24, x20, :lo12:.LANCHOR0
	mov	w3, 21
	mov	w0, 1
	ldrh	w4, [x24,3314]
	sub	w3, w3, w4
	lsr	w4, w2, w4
	lsl	w3, w0, w3
	ldrh	w0, [x24,56]
	sub	w3, w3, #1
	and	w4, w3, w4
	ldrb	w3, [x24,3312]
	udiv	w3, w4, w3
	uxth	w25, w3
	and	w3, w3, 65535
	cmp	w0, w25
	bls	.L2918
	add	x0, x21, :lo12:.LANCHOR7
	add	x0, x0, 3328
	ldrh	w1, [x0,w3,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x0,w3,sxtw 1]
	b	.L2919
.L2918:
	adrp	x0, .LC228
	mov	w1, w19
	add	x0, x0, :lo12:.LC228
	bl	printk
	ldrb	w3, [x24,2816]
	adrp	x0, .LC229
	ldr	x1, [x24,3880]
	add	x0, x0, :lo12:.LC229
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L2919:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w0, [x0,56]
	cmp	w0, w25
	bhi	.L2917
	mov	x0, x23
	mov	x1, x22
	mov	w2, 1266
	bl	printk
.L2917:
	bl	timer_get_time
	add	w19, w19, 1
	b	.L2913
.L2941:
	mov	w23, 0
	adrp	x26, .LC230
	mov	w19, w23
	add	x27, x21, :lo12:.LANCHOR7
	add	x26, x26, :lo12:.LC230
	mov	w28, 65535
.L2922:
	add	x22, x20, :lo12:.LANCHOR0
	ldrh	w1, [x22,56]
	cmp	w1, w19
	bls	.L2942
	uxtw	x1, w19
	ldr	x0, [x22,72]
	lsl	x25, x1, 1
	add	x24, x27, 3328
	sxtw	x5, w19
	ldrh	w3, [x0,x25]
	ldrh	w2, [x24,w19,sxtw 1]
	cmp	w3, w2
	beq	.L2923
	ldr	x0, [x22,64]
	str	x5, [x29,104]
	add	x1, x0, x1, lsl 2
	mov	x0, x26
	ldrb	w4, [x1,2]
	mov	w1, w19
	ubfx	x4, x4, 5, 3
	bl	printk
	ldr	x0, [x22,72]
	ldrh	w1, [x0,x25]
	cmp	w1, w28
	beq	.L2923
	ldr	x5, [x29,104]
	ldrh	w0, [x24,x5,lsl 1]
	cmp	w0, w1
	bls	.L2923
	ldr	x1, [x22,80]
	ldrh	w0, [x1,16]
	cmp	w0, w19
	beq	.L2923
	ldrh	w0, [x1,80]
	cmp	w0, w19
	beq	.L2923
	ldrh	w0, [x1,48]
	cmp	w0, w19
	csinc	w23, w23, wzr, eq
.L2923:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2922
.L2942:
	adrp	x22, .LANCHOR3
	adrp	x24, .LC0
	add	x22, x22, :lo12:.LANCHOR3
	mov	w19, 0
	add	x21, x21, :lo12:.LANCHOR7
	add	x24, x24, :lo12:.LC0
	add	x22, x22, 696
.L2925:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1,56]
	cmp	w0, w19
	bls	.L2943
	uxtw	x25, w19
	ldr	x0, [x1,64]
	add	x0, x0, x25, lsl 2
	ldrb	w0, [x0,2]
	tst	w0, 224
	bne	.L2927
	add	x0, x21, 3328
	sxtw	x27, w19
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L2928
	lsl	x26, x25, 1
	ldr	x0, [x1,72]
	ldrh	w0, [x0,x26]
	cbz	w0, .L2929
	adrp	x0, .LC231
	mov	w1, w19
	add	x0, x0, :lo12:.LC231
	bl	printk
.L2929:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	strh	wzr, [x0,x26]
.L2928:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x25,lsl 1]
	cbz	w0, .L2927
	add	x0, x21, 3328
	ldrh	w0, [x0,x27,lsl 1]
	cbz	w0, .L2927
	mov	x0, x24
	mov	x1, x22
	mov	w2, 1298
	bl	printk
.L2927:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2925
.L2943:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC232
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, w23
	add	x1, x1, 696
	add	x0, x0, :lo12:.LC232
	bl	printk
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	vpn_check, .-vpn_check
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
	ldrh	w1, [x0,2888]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2945
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 712
	mov	w2, 897
	bl	printk
.L2945:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	mov	w0, 0
	ldrh	w1, [x2,2888]
	cmp	w1, w3
	beq	.L2946
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L2947
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,72]
	adrp	x0, .LC233
	add	x0, x0, :lo12:.LC233
	ldrh	w2, [x2,x3]
	bl	printk
.L2947:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2888
	add	x1, x20, 2928
	ldrh	w0, [x20,2888]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L2948
	mov	w0, -1
	strh	w0, [x20,2888]
	b	.L2976
.L2948:
	ldrh	w3, [x20,2888]
	ldr	x2, [x20,64]
	add	x2, x2, x3, lsl 2
	ldrb	w2, [x2,2]
	tst	w2, 192
	and	w4, w2, 224
	beq	.L2949
	cmp	w4, 224
	beq	.L2949
	ldr	x2, [x20,80]
	ldrh	w5, [x2,16]
	cmp	w5, w3
	beq	.L2949
	ldrh	w5, [x2,48]
	cmp	w5, w3
	beq	.L2949
	ldrh	w2, [x2,80]
	cmp	w2, w3
	bne	.L2950
.L2949:
	cbnz	w4, .L2951
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x3,lsl 1]
	cbz	w0, .L2952
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 712
	mov	w2, 917
	bl	printk
.L2952:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2888]
	ldr	x0, [x0,72]
	strh	wzr, [x0,x1,lsl 1]
.L2951:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2912]
	strh	w0, [x19,2888]
.L2976:
	mov	w0, 0
	b	.L2946
.L2950:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w20, [x0,40]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2953
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 712
	mov	w2, 925
	bl	printk
.L2953:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w24, 1
	ldrh	w3, [x2,2888]
	ldr	x4, [x2,64]
	ldrh	w0, [x2,3276]
	add	x3, x4, x3, lsl 2
	ldrh	w1, [x2,3318]
	sub	w0, w0, #1
	ldrb	w3, [x3,2]
	uxth	w0, w0
	mul	w1, w20, w1
	and	w3, w3, 224
	cmp	w3, 160
	bne	.L2954
	ldrb	w3, [x2,3225]
	cmp	w3, 2
	uxth	w24, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2954
	ldrh	w0, [x2,3242]
	sub	w0, w0, #1
	uxth	w0, w0
.L2954:
	add	x20, x19, :lo12:.LANCHOR0
	orr	w23, w0, w1
	mov	w0, 1
	strb	w24, [x20,2894]
	strh	wzr, [x20,2890]
	strb	wzr, [x20,2892]
	strh	wzr, [x20,2914]
	bl	buf_alloc
	mov	w1, 1
	str	w23, [x0,40]
	mov	x21, x0
	bl	sblk_read_page
	ldrh	w0, [x20,3242]
	mov	w1, 255
	ldrb	w2, [x20,3244]
	mul	w2, w0, w2
	ldr	x0, [x20,3216]
	lsl	w2, w2, 2
	bl	ftl_memset
	strh	wzr, [x20,2912]
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L2958
	cmn	w0, #1
	beq	.L2958
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2957
	ldrh	w0, [x20,2888]
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2958
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 712
	mov	w2, 955
	bl	printk
.L2958:
	mov	x0, x21
	bl	buf_free
	mov	w0, -1
	b	.L2946
.L2957:
	ldrb	w0, [x20,3244]
	mov	x26, 0
	ldrh	w22, [x20,3276]
	mov	w25, 1
	ldr	x23, [x21,8]
	mul	w22, w0, w22
	mul	w22, w24, w22
	mov	w24, 21
	uxth	w22, w22
.L2959:
	cmp	w22, w26
	ble	.L2977
	ldr	w0, [x23,x26,lsl 2]
	cmn	w0, #1
	beq	.L2961
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L2962
	ldr	w0, [x23,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L2962:
	ldrh	w0, [x20,3314]
	ldr	w2, [x29,92]
	sub	w1, w24, w0
	ldrh	w3, [x20,2888]
	lsl	w1, w25, w1
	lsr	w0, w2, w0
	sub	w1, w1, #1
	and	w0, w1, w0
	ldrb	w1, [x20,3312]
	udiv	w0, w0, w1
	cmp	w0, w3
	bne	.L2961
	ldrh	w3, [x20,2912]
	ldr	x0, [x20,3216]
	str	w2, [x0,x3,lsl 2]
	ldrh	w0, [x20,2912]
	add	w0, w0, 1
	strh	w0, [x20,2912]
.L2961:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L2959
.L2977:
	mov	x0, x21
	bl	buf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2888]
	ldr	x3, [x0,72]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2912]
	cmp	w2, w3
	beq	.L2965
	adrp	x0, .LC234
	add	x0, x0, :lo12:.LC234
	bl	printk
.L2965:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2888]
	ldr	x1, [x0,72]
	ldrh	w0, [x0,2912]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	bcs	.L2966
	bl	vpn_check
.L2966:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2888]
	ldr	x1, [x19,72]
	ldrh	w3, [x19,2912]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2916]
	ldrh	w0, [x19,2912]
.L2946:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	gc_scan_src_blk, .-gc_scan_src_blk
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	sub	sp, sp, #144
	mov	w0, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	stp	x21, x22, [sp,48]
	bl	buf_alloc
	adrp	x24, .LC236
	mov	x19, x0
	adrp	x25, .LC237
	adrp	x0, .LC235
	mov	w1, 0
	add	x0, x0, :lo12:.LC235
	mov	w20, 0
	add	x24, x24, :lo12:.LC236
	add	x27, x25, :lo12:.LC237
	bl	printk
.L2979:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2824]
	cmp	w20, w0
	bcs	.L3002
	mov	w0, -1
	str	w0, [x19,36]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2980
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2980:
	and	w0, w20, 4095
	cbnz	w0, .L2981
	ldr	w2, [x29,124]
	mov	x0, x24
	mov	w1, w20
	bl	printk
.L2981:
	ldr	w0, [x29,124]
	str	w0, [x19,40]
	str	w0, [x19,44]
	cmn	w0, #1
	beq	.L2983
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w3, [x19,52]
	cmn	w3, #1
	beq	.L2984
	cmp	w3, 256
	beq	.L2984
	ldr	x0, [x19,24]
	ldr	w0, [x0,4]
	cmp	w0, w20
	beq	.L2983
.L2984:
	ldr	x0, [x19,8]
	add	x23, x25, :lo12:.LC237
	ldr	x7, [x19,24]
	adrp	x26, .LC238
	ldr	w1, [x0]
	str	w1, [sp]
	mov	w1, w20
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x19,40]
	ldr	w7, [x7,12]
	bl	printk
	ldr	x0, [x19,24]
	ldr	w22, [x0,4]
	ldr	w0, [x0,8]
	cmn	w0, #1
	bne	.L2986
.L2988:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, w20
	ldrb	w22, [x21,2816]
	bl	lpa_hash_get_ppa
	mov	w1, w20
	mov	w4, w0
	lsl	w28, w22, 7
	lsl	w3, w22, 7
	sub	w3, w3, #1
	add	x0, x26, :lo12:.LC238
	sdiv	w28, w20, w28
	and	w3, w20, w3
	adrp	x26, .LC240
	mov	x22, 0
	uxth	w3, w3
	add	x21, x21, 3872
	add	x26, x26, :lo12:.LC240
	uxth	w23, w28
	mov	w2, w23
	bl	printk
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
	b	.L2987
.L2986:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w1, [x1,2824]
	cmp	w22, w1
	bcs	.L2988
	str	w0, [x19,40]
	mov	w1, 1
	mov	x0, x19
	bl	sblk_read_page
	ldr	x0, [x19,8]
	ldr	x7, [x19,24]
	ldr	w1, [x0]
	str	w1, [sp]
	mov	w1, w20
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x19,40]
	ldr	w3, [x19,52]
	ldr	w7, [x7,12]
	bl	printk
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2989
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2989:
	ldr	w0, [x29,124]
	mov	w1, 1
	str	w0, [x19,40]
	mov	x0, x19
	bl	sblk_read_page
	ldr	x0, [x19,8]
	ldr	x7, [x19,24]
	ldr	w1, [x0]
	str	w1, [sp]
	mov	w1, w22
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x27
	ldr	w6, [x7,8]
	ldr	w4, [x7]
	ldr	w3, [x19,52]
	ldr	w5, [x7,4]
	ldr	w2, [x19,40]
	ldr	w7, [x7,12]
	bl	printk
	add	x5, x21, :lo12:.LANCHOR0
	mov	w0, w22
	str	x5, [x29,96]
	ldrb	w23, [x5,2816]
	lsl	w2, w23, 7
	udiv	w2, w22, w2
	str	x2, [x29,104]
	uxth	w28, w2
	bl	lpa_hash_get_ppa
	lsl	w3, w23, 7
	ldr	x2, [x29,104]
	sub	w3, w3, #1
	mov	w4, w0
	and	w3, w3, w22
	mov	w1, w22
	uxth	w2, w2
	add	x0, x26, :lo12:.LC238
	uxth	w3, w3
	bl	printk
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	mov	x22, 0
	bl	pm_log2phys
	ldr	x5, [x29,96]
	adrp	x4, .LC239
	add	x4, x4, :lo12:.LC239
	add	x23, x5, 3872
.L2991:
	add	x0, x23, x22
	ldr	x1, [x0,8]
	cbz	x1, .L2990
	ldrh	w0, [x22,x23]
	cmp	w0, w28
	bne	.L2990
	mov	x0, x4
	mov	w2, 4
	mov	w3, 4096
	str	x4, [x29,104]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
.L2990:
	add	x22, x22, 16
	cmp	x22, 512
	bne	.L2991
	b	.L2988
.L3003:
	ldrh	w0, [x22,x21]
	cmp	w0, w23
	bne	.L2992
	mov	x0, x26
	mov	w2, 4
	mov	w3, 4096
	bl	rknand_print_hex
.L2992:
	add	x22, x22, 16
	cmp	x22, 512
	beq	.L2983
.L2987:
	add	x0, x21, x22
	ldr	x1, [x0,8]
	cbnz	x1, .L3003
	b	.L2992
.L2983:
	add	w20, w20, 1
	b	.L2979
.L3002:
	mov	x0, x19
	bl	buf_free
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 144
	ret
	.size	ftl_scan_all_data, .-ftl_scan_all_data
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
	bhi	.L3005
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x22, x0
	mov	x0, -14
	cbnz	x22, .L3005
	adrp	x0, .LC241
	mov	x1, x19
	add	x0, x0, :lo12:.LC241
	strb	w22, [x19,x21]
	bl	printk
	adrp	x0, .LC242
	mov	x1, x19
	add	x0, x0, :lo12:.LC242
	mov	w2, 1
	mov	w3, 16
	bl	rknand_print_hex
	bl	rknand_device_lock
	adrp	x1, .LC243
	mov	x0, x19
	add	x1, x1, :lo12:.LC243
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3006
	adrp	x23, .LANCHOR0
	adrp	x0, .LC244
	add	x19, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC244
	mov	w2, 4
	adrp	x20, .LANCHOR4
	ldr	x3, [x19,2848]
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	add	x3, x20, :lo12:.LANCHOR4
	ldr	x1, [x19,2848]
	adrp	x0, .LC245
	mov	w2, 2
	add	x0, x0, :lo12:.LC245
	add	x1, x1, 416
	ldrh	w3, [x3,1132]
	mov	w19, w22
	mov	w22, 65535
	bl	rknand_print_hex
.L3007:
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,1132]
	cmp	w19, w0
	bge	.L3010
	mov	w0, 300
	bl	msleep
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0,2848]
	add	x0, x0, x19, sxtw 1
	ldrh	w0, [x0,416]
	cmp	w0, w22
	beq	.L3008
	mov	x1, 0
	bl	ftl_sblk_dump
.L3008:
	add	w19, w19, 1
	b	.L3007
.L3006:
	adrp	x1, .LC246
	mov	x0, x19
	add	x1, x1, :lo12:.LC246
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3011
	adrp	x23, .LANCHOR0
	adrp	x0, .LC244
	add	x20, x23, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC244
	mov	w2, 4
	add	x22, x29, 192
	adrp	x24, .LC249
	ldr	x3, [x20,2848]
	add	x24, x24, :lo12:.LC249
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	adrp	x3, .LANCHOR4+1132
	ldr	x1, [x20,2848]
	adrp	x0, .LC245
	mov	w2, 2
	add	x0, x0, :lo12:.LC245
	add	x1, x1, 416
	ldrh	w3, [x3,#:lo12:.LANCHOR4+1132]
	bl	rknand_print_hex
	adrp	x0, .LC247
	add	x1, x19, 7
	add	x0, x0, :lo12:.LC247
	str	x1, [x22,-88]!
	add	x19, x20, 3872
	mov	x20, x23
	bl	printk
	adrp	x23, .LC248
	ldr	x0, [x29,104]
	mov	x1, x22
	add	x25, x19, 512
	add	x23, x23, :lo12:.LC248
	bl	rk_simple_strtoull.constprop.34
	mov	w22, w0
	str	w0, [x29,100]
	uxth	w26, w0
.L3013:
	ldrh	w1, [x19]
	mov	x0, x23
	ldrh	w2, [x19,2]
	bl	printk
	ldrh	w0, [x19]
	cmp	w0, w26
	bne	.L3012
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x19,8]
	mov	w2, 4
	ldrb	w3, [x0,2816]
	mov	x0, x24
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L3012:
	add	x19, x19, 16
	cmp	x19, x25
	bne	.L3013
	mov	w0, 300
	add	x20, x20, :lo12:.LANCHOR0
	bl	msleep
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldr	x1, [x20,2848]
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
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	ldr	w3, [x6]
	ldr	w2, [x19,52]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	ldrb	w3, [x20,2816]
	adrp	x0, .LC250
	add	x0, x0, :lo12:.LC250
	ldr	x1, [x19,8]
	mov	w2, 4
	lsl	w3, w3, 7
	b	.L3025
.L3011:
	adrp	x1, .LC251
	mov	x0, x19
	add	x1, x1, :lo12:.LC251
	mov	x2, 7
	bl	memcmp
	cbnz	w0, .L3014
	bl	dump_ftl_info
	b	.L3010
.L3014:
	adrp	x1, .LC252
	mov	x0, x19
	add	x1, x1, :lo12:.LC252
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3015
	bl	dump_all_list_info
	b	.L3010
.L3015:
	adrp	x1, .LC253
	mov	x0, x19
	add	x1, x1, :lo12:.LC253
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3016
	bl	vpn_check
	b	.L3010
.L3016:
	adrp	x1, .LC254
	mov	x0, x19
	add	x1, x1, :lo12:.LC254
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3017
	add	x20, x29, 192
	adrp	x0, .LC247
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC247
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
	adrp	x0, .LC197
	add	x0, x0, :lo12:.LC197
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldrb	w3, [x20,2816]
	adrp	x0, .LC190
	ldr	x1, [x19,8]
	add	x0, x0, :lo12:.LC190
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20,2816]
	adrp	x0, .LC191
	ldr	x1, [x19,24]
	add	x0, x0, :lo12:.LC191
	mov	w2, 4
	lsl	w3, w3, 1
.L3025:
	bl	rknand_print_hex
	mov	x0, x19
	bl	buf_free
	b	.L3010
.L3017:
	adrp	x1, .LC255
	mov	x0, x19
	add	x1, x1, :lo12:.LC255
	mov	x2, 8
	bl	memcmp
	cbnz	w0, .L3018
	add	x20, x29, 192
	adrp	x0, .LC247
	add	x1, x19, 8
	add	x0, x0, :lo12:.LC247
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.34
	str	w0, [x29,100]
	mov	x1, x22
	bl	ftl_sblk_dump
	b	.L3010
.L3018:
	adrp	x1, .LC256
	mov	x0, x19
	add	x1, x1, :lo12:.LC256
	mov	x2, 10
	bl	memcmp
	cbnz	w0, .L3019
	add	x20, x29, 192
	adrp	x0, .LC247
	add	x1, x19, 10
	add	x0, x0, :lo12:.LC247
	str	x1, [x20,-88]!
	bl	printk
	ldr	x0, [x29,104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.34
	str	w0, [x29,100]
	adrp	x1, .LANCHOR2
	str	w0, [x1,#:lo12:.LANCHOR2]
	b	.L3010
.L3019:
	adrp	x1, .LC257
	mov	x2, 8
	mov	x0, x19
	add	x1, x1, :lo12:.LC257
	bl	memcmp
	mov	w2, w0
	str	x2, [x29,88]
	cbnz	w0, .L3020
	add	x1, x19, 8
	adrp	x0, .LC247
	add	x19, x29, 192
	add	x0, x0, :lo12:.LC247
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
	bne	.L3021
	mov	w0, w19
	add	x1, x29, 100
	bl	pm_log2phys
.L3021:
	adrp	x0, .LC258
	ldr	w2, [x29,100]
	mov	w1, w19
	add	x0, x0, :lo12:.LC258
	bl	printk
	b	.L3010
.L3020:
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
	adrp	x0, .LC266
	add	x0, x0, :lo12:.LC266
	bl	printk
	adrp	x0, .LC267
	add	x0, x0, :lo12:.LC267
	bl	printk
.L3010:
	bl	rknand_device_unlock
	mov	x0, x21
.L3005:
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
	ldrh	w24, [x0,3276]
	ldrh	w20, [x23,12]
	ldr	x0, [x0,3840]
	mul	w24, w24, w1
	add	x20, x0, x20, lsl 2
	add	x0, x20, x24, sxtw 2
	ldr	w0, [x0,-4]
	cmn	w0, #1
	beq	.L3027
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 728
	mov	w2, 1450
	bl	printk
.L3027:
	adrp	x25, .LC269
	mov	x27, 0
	add	x0, x25, :lo12:.LC269
	adrp	x26, .LC268
	str	x0, [x29,120]
	mov	w22, w27
	add	x0, x26, :lo12:.LC268
	str	x0, [x29,112]
.L3028:
	cmp	w27, w24
	mov	w25, w27
	bge	.L3053
	ldr	w2, [x20,x27,lsl 2]
	cmn	w2, #1
	beq	.L3029
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2816]
	adrp	x0, .LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L3030
	ldr	x0, [x29,112]
	mov	w1, w21
	mov	w3, w27
	bl	printk
.L3030:
	mov	w28, -1
.L3034:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L3031
	add	x6, x19, :lo12:.LANCHOR0
	ldrb	w1, [x6,2816]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L3031
	ldrb	w0, [x23,9]
	ldrh	w2, [x6,3318]
	str	x6, [x29,104]
	sdiv	w1, w25, w0
	msub	w0, w1, w0, w25
	add	x0, x23, x0, sxtw 1
	ldrh	w0, [x0,16]
	madd	w2, w0, w2, w1
	str	w2, [x29,140]
	ldr	x0, [x29,120]
	ldr	w1, [x20,x26]
	bl	pm_ppa_update_check
	ldr	x6, [x29,104]
	cbz	w0, .L3032
	adrp	x0, .LC270
	ldr	x1, [x6,3840]
	add	x0, x0, :lo12:.LC270
	mov	w2, 4
	mov	w3, w24
	bl	rknand_print_hex
.L3033:
	b	.L3033
.L3032:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	str	w28, [x20,x26]
.L3031:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L3034
.L3029:
	add	x27, x27, 1
	b	.L3028
.L3053:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3036
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,72]
	adrp	x0, .LC271
	add	x0, x0, :lo12:.LC271
	ldrh	w3, [x4,x3]
	bl	printk
.L3036:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	ldr	x0, [x19,72]
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
	beq	.L3055
	bl	zftl_insert_data_list
.L3055:
	adrp	x0, .LANCHOR0+80
	mov	w2, 2
	mov	w1, 3
	ldr	x0, [x0,#:lo12:.LANCHOR0+80]
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
	adrp	x0, .LANCHOR5
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR5
	add	x24, x24, :lo12:.LANCHOR0
	str	x0, [x29,120]
	stp	x27, x28, [sp,80]
	add	x0, x0, 513
	add	x28, x24, 768
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	str	x0, [x29,104]
.L3059:
	ldrb	w0, [x24,2840]
	adrp	x21, .LANCHOR0
	cbz	w0, .L3061
	ldrb	w1, [x24,2866]
	sub	w0, w0, #1
	str	x1, [x29,112]
	lsl	x1, x1, 6
	strb	w0, [x24,2840]
	add	x25, x28, x1
	ldr	w0, [x24,2824]
	ldrb	w1, [x28,x1]
	ldr	w23, [x25,36]
	strb	w1, [x24,2866]
	cmp	w23, w0
	bcc	.L3062
	mov	x0, x25
	bl	buf_free
	mov	w0, -1
	b	.L3116
.L3062:
	ldr	x0, [x25,24]
	str	x0, [x29,128]
	ldrb	w0, [x24,2841]
	ldr	x26, [x25,8]
	ldrb	w22, [x25,57]
	ldrb	w20, [x25,56]
	cbz	w0, .L3064
	ldr	x0, [x29,120]
	ldrb	w3, [x0,513]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 768
	add	x3, x28, x3, lsl 6
.L3065:
	ldrb	w4, [x3]
	cmp	w4, 255
	beq	.L3118
	sbfiz	x3, x4, 6, 32
	add	x3, x0, x3
	b	.L3065
.L3118:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L3064
	ldr	x0, [x3,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x26, x1
	bl	ftl_memcpy
	mov	x0, x25
	bl	buf_free
	b	.L3059
.L3064:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L3068
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L3068:
	add	x4, x21, :lo12:.LANCHOR0
	mov	w3, 0
	ldr	x19, [x4,80]
	add	x4, x4, 768
	add	x19, x19, 16
.L3071:
	ldr	w0, [x4,36]
	cmp	w0, w23
	bne	.L3069
	ldrb	w5, [x4,2]
	tbz	x5, 3, .L3069
	add	x0, x21, :lo12:.LANCHOR0
	ubfiz	x3, x3, 6, 32
	add	x0, x0, 768
	and	w5, w5, -9
	add	x3, x0, x3
	ldr	w0, [x3,40]
	strb	w5, [x3,2]
	ldr	x27, [x3,8]
	str	w0, [x29,156]
	b	.L3070
.L3069:
	add	w3, w3, 1
	add	x4, x4, 64
	cmp	w3, 32
	bne	.L3071
	mov	x27, 0
.L3070:
	add	x3, x21, :lo12:.LANCHOR0
	str	wzr, [x29,136]
	ldrb	w0, [x3,2816]
	cmp	w20, w0
	bcs	.L3072
	add	w20, w22, w20
	cbz	x27, .L3073
	cbz	w22, .L3074
	mov	x0, x26
	mov	x1, x27
	lsl	w2, w22, 9
	str	x3, [x29,136]
	bl	ftl_memcpy
	ldr	x3, [x29,136]
	ldr	x19, [x3,80]
	add	x19, x19, 48
.L3074:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2816]
	cmp	w20, w2
	bcc	.L3075
	ldr	x19, [x0,80]
	add	x19, x19, 16
	b	.L3117
.L3075:
	ubfiz	x1, x20, 9, 9
	sub	w2, w2, w20
	add	x0, x26, x1
	lsl	w2, w2, 9
	add	x1, x27, x1
	bl	ftl_memcpy
.L3117:
	str	wzr, [x29,136]
	b	.L3072
.L3073:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L3076
	mov	w0, 1
	bl	buf_alloc
	mov	x27, x0
	ldr	w1, [x29,156]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x0, [x27,24]
	ldr	w1, [x0,12]
	ldr	w0, [x0,4]
	add	w1, w1, 1
	str	w1, [x29,136]
	cmp	w0, w23
	bne	.L3077
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3078
.L3077:
	ldrb	w1, [x27,1]
	adrp	x0, .LC272
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	w4, [x27,52]
	add	x0, x0, :lo12:.LC272
	bl	printk
	mov	w2, 4
	ldr	x1, [x27,24]
	adrp	x0, .LC191
	mov	w3, w2
	add	x0, x0, :lo12:.LC191
	bl	rknand_print_hex
	add	x3, x21, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3314]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,156]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3312]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
.L3078:
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L3079
	ldr	w0, [x27,52]
	cmn	w0, #1
	bne	.L3080
.L3079:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 752
	mov	w2, 544
	bl	printk
.L3080:
	cbz	w22, .L3081
	ldr	w0, [x29,156]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3082
	ldr	x1, [x27,8]
	mov	x0, x26
	bl	ftl_memcpy
	b	.L3083
.L3076:
	cbz	w22, .L3081
	lsl	w2, w22, 9
.L3082:
	mov	x0, x26
	mov	w1, 0
	bl	ftl_memset
.L3083:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x19, [x0,80]
	add	x19, x19, 48
.L3081:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w2, [x0,2816]
	cmp	w20, w2
	bcc	.L3084
	bls	.L3085
	ldr	x19, [x0,80]
	add	x19, x19, 16
	b	.L3085
.L3084:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L3086
	ldr	x1, [x27,8]
	lsl	x20, x0, 2
	add	x0, x26, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L3085
.L3086:
	add	x0, x26, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L3085:
	cbz	x27, .L3072
	ldrb	w0, [x27,2]
	mov	x1, x27
	and	w0, w0, -9
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 2880
	bl	buf_remove_buf
	mov	x0, x27
	bl	buf_free
.L3072:
	ldrh	w0, [x19,6]
	cbnz	w0, .L3087
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3087:
	mov	x0, x19
	add	x20, x21, :lo12:.LANCHOR0
	bl	ftl_get_new_free_page
	ldr	x1, [x29,112]
	add	x3, x20, 768
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
	ldr	x0, [x29,104]
	mov	x1, x25
	bl	buf_add_tail
	ldrb	w0, [x20,2841]
	add	w0, w0, 1
	strb	w0, [x20,2841]
	bl	timer_get_time
	ldr	x1, [x29,120]
	ldrb	w2, [x20,2841]
	str	w0, [x1,516]
	cmp	w2, 2
	adrp	x0, .LANCHOR5
	bhi	.L3088
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L3061
.L3088:
	ldrb	w1, [x19,5]
	mov	w4, 2
	add	x6, x21, :lo12:.LANCHOR0
	cmp	w1, wzr
	ldrh	w1, [x19,6]
	csinc	w4, w4, wzr, ne
	add	x6, x6, 768
	cmp	w1, 1
	add	x1, x0, :lo12:.LANCHOR5
	csel	w4, w4, w2, ne
	ldrb	w5, [x1,513]
	mov	w1, 0
	mov	w3, w5
.L3092:
	cmp	w1, w4
	beq	.L3119
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L3092
.L3119:
	add	x0, x0, :lo12:.LANCHOR5
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,513]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 768
	strb	w2, [x0,2073]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L3061
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3061:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w19, [x21,2840]
	cbnz	w19, .L3059
	bl	ftl_write_completed
	mov	w0, w19
.L3116:
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
	ldrb	w0, [x21,3241]
	ldr	x22, [x21,80]
	cbnz	w0, .L3121
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3120
	ldrh	w23, [x21,2914]
	mov	w0, w23
	add	w23, w23, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w22, w0
	mov	w1, 1
	mov	x0, x20
	strh	w23, [x21,2914]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3176
	cmn	w0, #1
	bne	.L3123
.L3176:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w2, 21
	ldrh	w1, [x3,3314]
	lsr	w0, w22, w1
	sub	w1, w2, w1
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x3,3312]
	udiv	w0, w1, w0
	mov	x1, 0
	bl	ftl_sblk_dump
.L3123:
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L3177
	cmn	w0, #1
	bne	.L3125
.L3177:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 776
	mov	w2, 573
	bl	printk
.L3125:
	ldr	x0, [x20,24]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,144]
	cmn	w0, #1
	bne	.L3127
	mov	w0, w21
	add	x1, x29, 144
	mov	w2, 0
	bl	pm_log2phys
.L3127:
	ldr	w23, [x29,144]
	cmp	w23, w22
	bne	.L3128
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 768
	add	x1, x1, 2816
.L3131:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L3129
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L3129
	mov	x0, x20
	bl	buf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3120
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC273
	mov	w1, w21
	mov	w2, w23
	add	x0, x0, :lo12:.LC273
	ldrh	w3, [x19,2914]
	bl	printk
	b	.L3120
.L3129:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L3131
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20,2]
	str	w21, [x20,36]
	strb	wzr, [x20,57]
	ldrb	w0, [x22,2816]
	strb	w0, [x20,56]
	mov	w0, 10
	orr	w0, w1, w0
	strb	w0, [x20,2]
	ldr	x0, [x20,24]
	ldr	w0, [x0]
	str	w0, [x20,32]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3132
	ldr	w2, [x29,144]
	mov	w0, w21
	str	x2, [x29,136]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,136]
	adrp	x0, .LC274
	ldrh	w5, [x22,2914]
	add	x0, x0, :lo12:.LC274
	mov	w1, w21
	mov	w4, w23
	bl	printk
.L3132:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,2848]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,2916]
	add	w0, w0, 1
	strh	w0, [x19,2916]
	b	.L3120
.L3128:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3134
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2914]
	adrp	x0, .LC274
	add	x0, x0, :lo12:.LC274
	mov	w1, w21
	mov	w2, w23
	mov	w4, w22
	bl	printk
.L3134:
	mov	x0, x20
	bl	buf_free
	b	.L3120
.L3121:
	ldrb	w24, [x21,3225]
	add	x0, x21, 2888
	add	x22, x22, 80
	cmp	w24, 3
	bne	.L3135
	ldrb	w1, [x21,3240]
	cbz	w1, .L3136
	ldrb	w20, [x22,9]
	ldrh	w21, [x0,314]
	str	w24, [x29,136]
	add	w23, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w23, w21, w23
	madd	w20, w23, w20, w21
	uxth	w20, w20
	b	.L3138
.L3136:
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
	bne	.L3139
	b	.L3138
.L3135:
	ldrb	w20, [x22,9]
	ldrb	w1, [x21,3316]
	ldrh	w21, [x0,314]
	mov	w0, 1
	sdiv	w23, w21, w20
	msub	w20, w23, w20, w21
	uxth	w20, w20
	cbz	w1, .L3221
	mov	w0, 2
.L3221:
	str	w0, [x29,136]
.L3138:
	mov	w25, 0
	add	x24, x19, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR4
.L3141:
	ldrb	w1, [x22,9]
	ldr	w0, [x29,136]
	mul	w0, w0, w1
	cmp	w25, w0
	bge	.L3174
	ldrh	w0, [x24,3242]
	add	w26, w21, w25
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	beq	.L3174
	add	x0, x3, :lo12:.LANCHOR4
	sbfiz	x26, x26, 2, 32
	ldr	x1, [x0,1112]
	ldr	w0, [x1,x26]
	cmn	w0, #1
	bne	.L3142
	ldrh	w27, [x24,2914]
	ldrh	w0, [x24,2912]
	str	x3, [x29,120]
	str	x1, [x29,128]
	cmp	w0, w27
	bls	.L3120
	mov	w0, w27
	add	w27, w27, 1
	bl	gc_get_src_ppa_from_index
	strh	w27, [x24,2914]
	ldr	x1, [x29,128]
	ldr	x3, [x29,120]
	str	w0, [x1,x26]
.L3142:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L3141
.L3174:
	mov	w25, 1
.L3139:
	adrp	x0, .LANCHOR2
	str	x0, [x29,120]
	ldr	w1, [x0,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3144
	adrp	x0, .LANCHOR4+1112
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x5, x21, 2, 16
	mov	w1, w23
	mov	w2, w25
	mov	w3, w20
	ldr	x6, [x0,#:lo12:.LANCHOR4+1112]
	adrp	x0, .LC275
	ldrh	w4, [x4,3202]
	add	x0, x0, :lo12:.LC275
	ldr	w5, [x6,x5]
	mov	w6, w21
	bl	printk
.L3144:
	adrp	x0, .LC0
	mov	w27, 0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29,112]
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 776
	str	x0, [x29,104]
.L3145:
	ldrb	w0, [x22,9]
	ldr	w1, [x29,136]
	mul	w0, w1, w0
	cmp	w27, w0
	bge	.L3225
	add	w26, w21, w27
	sxtw	x0, w26
	str	x0, [x29,128]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x29,128]
	ldr	x0, [x0,3232]
	ldrb	w0, [x0,x1]
	cmp	w0, 255
	bne	.L3146
	mov	w0, 0
	bl	buf_alloc
	mov	x24, x0
	cbnz	x0, .L3147
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w24
	bl	buf_alloc
	mov	x24, x0
	cbz	x0, .L3120
.L3147:
	add	x28, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,128]
	ldrb	w1, [x24,1]
	ldr	x0, [x28,3232]
	strb	w1, [x0,x2]
	strh	w26, [x24,48]
	ldrb	w0, [x28,2895]
	strb	w25, [x24,61]
	add	w0, w0, 1
	strb	w0, [x28,2895]
	ldrb	w1, [x22,9]
	ldrh	w0, [x28,3242]
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	adrp	x26, .LANCHOR4
	bne	.L3148
	add	x26, x26, :lo12:.LANCHOR4
	mov	w1, -1
	ldr	x0, [x26,1104]
	str	w1, [x0,x2,lsl 2]
	ldrh	w0, [x28,3242]
	ldrb	w2, [x28,3244]
	ldr	x1, [x26,1104]
	mul	w2, w0, w2
	ldr	x0, [x24,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x28,3242]
	ldrb	w2, [x28,3244]
	ldr	x1, [x26,1112]
	mul	w2, w0, w2
	ldr	x0, [x24,8]
	add	x0, x0, x2, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w2, [x28,2816]
	mov	w1, 0
	ldr	x0, [x24,24]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x24,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	ldrh	w0, [x28,3242]
	ldrb	w1, [x28,3244]
	ldr	x26, [x24,24]
	mul	w1, w0, w1
	ldr	x0, [x24,8]
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x26,4]
	ldr	x1, [x28,80]
	ldr	x0, [x24,24]
	ldr	w1, [x1,132]
	b	.L3222
.L3148:
	ldr	x0, [x29,128]
	mov	w1, 1
	lsl	x28, x0, 2
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x0, [x0,1112]
	ldr	w0, [x0,x28]
	str	w0, [x24,40]
	mov	x0, x24
	bl	sblk_read_page
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3149
	cmn	w0, #1
	bne	.L3150
.L3149:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x4,3314]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x24,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3312]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3178
	cmn	w0, #1
	bne	.L3150
.L3178:
	ldr	x0, [x24,24]
	mov	w1, -1
	str	w1, [x0,4]
.L3150:
	ldr	w0, [x24,52]
	cmp	w0, 512
	beq	.L3179
	cmn	w0, #1
	bne	.L3153
.L3179:
	ldr	x0, [x29,112]
	mov	w2, 688
	ldr	x1, [x29,104]
	bl	printk
.L3153:
	ldr	x1, [x24,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2824]
	cmp	w2, w0
	bcc	.L3155
	mov	w0, -1
	str	w0, [x1,4]
.L3155:
	ldr	x0, [x24,24]
	add	x26, x26, :lo12:.LANCHOR4
	ldr	w1, [x0,4]
	ldr	x0, [x26,1104]
	str	w1, [x0,x28]
	ldr	x0, [x24,24]
	ldr	w1, [x24,40]
.L3222:
	str	w1, [x0,8]
.L3146:
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,128]
	add	x1, x3, 768
	sxtw	x5, w27
	ldr	x0, [x3,3232]
	ldrb	w4, [x0,x2]
	add	x0, x29, 144
	mov	w2, 2
	add	x1, x1, x4, lsl 6
	str	x1, [x0,w27,sxtw 3]
	ldrb	w0, [x3,3225]
	strh	w2, [x1,50]
	cmp	w0, 3
	strb	w25, [x1,61]
	adrp	x2, .LANCHOR4
	bne	.L3156
	udiv	w0, w27, w0
	ldrh	w3, [x3,3318]
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
	ldr	x1, [x2,1120]
	uxth	w0, w0
	add	w0, w0, 1
	orr	w0, w3, w0, lsl 24
	str	w0, [x1,x4,lsl 2]
	b	.L3157
.L3156:
	cmp	w0, 2
	bne	.L3158
	ldrb	w6, [x3,3316]
	add	x1, x1, 32
	ldrh	w0, [x3,3318]
	cbnz	w6, .L3159
	add	w3, w27, w20
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	b	.L3224
.L3159:
	add	w3, w20, w27, lsr 1
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	madd	w0, w3, w0, w23
	and	w3, w27, 1
	add	w0, w0, w3
.L3224:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L3158:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x2, :lo12:.LANCHOR4
	add	x0, x0, 768
	add	x0, x0, x4, lsl 6
	ldr	w3, [x0,40]
	ldrh	w1, [x0,48]
	ldr	x0, [x2,1120]
	str	w3, [x0,x1,lsl 2]
.L3157:
	ldr	x0, [x29,120]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3160
	add	x0, x29, 144
	ldr	x1, [x29,128]
	mov	w2, w25
	ldr	x5, [x0,x5,lsl 3]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x0,2894]
	ldr	x0, [x0,3232]
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
	adrp	x0, .LC276
	add	x0, x0, :lo12:.LC276
	ldr	w5, [x5,40]
	bl	printk
.L3160:
	add	w8, w27, 1
	uxth	w27, w8
	b	.L3145
.L3225:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x2,3225]
	cmp	w0, 3
	add	x0, x29, 144
	bne	.L3207
	ldrb	w1, [x2,3240]
	cbz	w1, .L3163
.L3166:
	ldr	x1, [x29,144]
	strb	wzr, [x1,60]
	b	.L3164
.L3163:
	cmp	w25, 1
	mov	w2, 9
	beq	.L3223
	cmp	w25, 2
	bne	.L3166
	mov	w2, 13
.L3223:
	ldr	x1, [x29,144]
	strb	w2, [x1,60]
.L3164:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L3167
.L3207:
	ldrb	w2, [x22,9]
	ldr	w3, [x29,136]
	mul	w2, w2, w3
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L3226
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L3207
.L3226:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,144]
	mul	w1, w3, w1
	bl	sblk_prog_page
.L3167:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x22,9]
	ldrb	w3, [x1,3240]
	cbz	w3, .L3169
	add	w0, w2, w2, lsl 1
	and	w2, w0, 1023
	b	.L3170
.L3169:
	ldrb	w1, [x1,3316]
	ubfiz	w0, w2, 1, 8
	cmp	w1, wzr
	csel	w2, w0, w2, ne
.L3170:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2848]
	ldr	w0, [x1,52]
	add	w0, w0, w2
	str	w0, [x1,52]
	ldrh	w1, [x19,3202]
	add	w0, w2, w1
	ldrh	w1, [x19,3242]
	uxth	w0, w0
	strh	w0, [x19,3202]
	ldrb	w2, [x22,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L3171
	ldr	x0, [x19,80]
	strh	wzr, [x0,86]
.L3171:
	bl	gc_write_completed
.L3120:
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
	ldr	w1, [x1,4]
	cmp	w2, w1
	bhi	.L3228
	adrp	x23, .LANCHOR8
	add	w24, w0, 24576
	add	x0, x23, :lo12:.LANCHOR8
	adrp	x25, .LANCHOR2
	ldr	w1, [x0,3584]
	add	w1, w19, w1
	str	w1, [x0,3584]
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3229
	adrp	x0, .LC277
	mov	w2, w24
	add	x0, x0, :lo12:.LC277
	mov	w3, w19
	mov	w4, 0
	bl	printk
.L3229:
	add	x20, x21, :lo12:.LANCHOR0
	ldr	x0, [x20,2848]
	ldr	w26, [x0,8]
	add	w1, w26, 1
	str	w1, [x0,8]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrb	w22, [x20,2816]
	udiv	w20, w24, w22
	msub	w27, w20, w22, w24
	cbz	w27, .L3230
	sub	w22, w22, w27
	mov	w0, w20
	cmp	w22, w19
	csel	w22, w22, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3231
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3231:
	ldr	w0, [x29,120]
	and	w28, w22, 65535
	cmn	w0, #1
	beq	.L3233
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3233
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
	ldr	x1, [x0,2848]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3233:
	add	w20, w20, 1
	sub	w19, w19, w28
.L3230:
	cbz	w19, .L3235
	bl	ftl_flush
.L3235:
	mov	w0, -1
	mov	w27, 1
	str	w0, [x29,124]
	mov	w28, 21
.L3236:
	add	x22, x21, :lo12:.LANCHOR0
	ldrb	w0, [x22,2816]
	cmp	w19, w0
	bcc	.L3275
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	beq	.L3237
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L3239
	ldrb	w2, [x22,2816]
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
	b	.L3274
.L3237:
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3239
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	ldrh	w0, [x22,3314]
	ldr	w1, [x29,120]
	sub	w2, w28, w0
	lsl	w2, w27, w2
	lsr	w0, w1, w0
	sub	w2, w2, #1
	ldrb	w1, [x22,3312]
	and	w0, w2, w0
	udiv	w0, w0, w1
	bl	ftl_vpn_decrement
.L3274:
	ldr	x1, [x22,2848]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3239:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,2816]
	sub	w19, w19, w0
	b	.L3236
.L3275:
	cbz	w19, .L3243
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,120]
	cmn	w0, #1
	bne	.L3244
	mov	w0, w20
	add	x1, x29, 120
	mov	w2, 0
	bl	pm_log2phys
.L3244:
	ldr	w0, [x29,120]
	cmn	w0, #1
	beq	.L3243
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L3243
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w26, [x0,32]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,2816]
	cmp	w19, w0
	bcc	.L3246
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 792
	mov	w2, 1177
	bl	printk
.L3246:
	ldr	x0, [x22,8]
	mov	w1, 0
	lsl	w2, w19, 9
	add	x21, x21, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	x1, [x21,2848]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L3243:
	add	x0, x23, :lo12:.LANCHOR8
	mov	w4, 0
	ldr	w1, [x0,3584]
	cmp	w1, 8192
	bls	.L3228
	ldr	w0, [x25,#:lo12:.LANCHOR2]
	tbz	x0, 12, .L3247
	adrp	x0, .LC277
	mov	w2, w24
	add	x0, x0, :lo12:.LC277
	mov	w3, w19
	bl	printk
.L3247:
	add	x23, x23, :lo12:.LANCHOR8
	str	wzr, [x23,3584]
	bl	flt_sys_flush
	mov	w0, 1
	mov	w4, 0
	str	w0, [x23,3588]
.L3228:
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
	.global	zftl_do_gc
	.type	zftl_do_gc, %function
zftl_do_gc:
	sub	sp, sp, #112
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	stp	x23, x24, [sp,64]
	mov	w24, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,48]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmp	w24, 1
	ldrh	w1, [x0,2832]
	ldrh	w21, [x0,2828]
	ldr	x23, [x0,80]
	add	w21, w1, w21
	ldrh	w22, [x0,2830]
	uxth	w21, w21
	bne	.L3277
	adrp	x0, .LANCHOR7
	adrp	x25, .LANCHOR8
	add	x2, x0, :lo12:.LANCHOR7
	mov	x20, x0
	ldr	w26, [x2,3320]
	cbnz	w26, .L3278
	add	x0, x25, :lo12:.LANCHOR8
	ldr	w0, [x0,3592]
	cbz	w0, .L3277
.L3278:
	add	w26, w26, 50
	bl	timer_get_time
	cmp	w26, w0
	bcs	.L3279
	add	x0, x20, :lo12:.LANCHOR7
	str	wzr, [x0,3320]
.L3279:
	add	x19, x25, :lo12:.LANCHOR8
	ldr	w20, [x19,3592]
	bl	timer_get_time
	add	w20, w20, 50
	cmp	w20, w0
	bcs	.L3395
	str	wzr, [x19,3592]
	b	.L3395
.L3277:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldrb	w2, [x0,1098]
	cmp	w2, 6
	bhi	.L3395
	adrp	x0, .L3283
	add	x0, x0, :lo12:.L3283
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x2, .Lrtx3283
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx3283:
	.section	.rodata
	.align	0
	.align	2
.L3283:
	.2byte	(.L3282 - .Lrtx3283) / 4
	.2byte	(.L3284 - .Lrtx3283) / 4
	.2byte	(.L3285 - .Lrtx3283) / 4
	.2byte	(.L3286 - .Lrtx3283) / 4
	.2byte	(.L3287 - .Lrtx3283) / 4
	.2byte	(.L3365 - .Lrtx3283) / 4
	.2byte	(.L3289 - .Lrtx3283) / 4
	.text
.L3282:
	ldrh	w28, [x23,80]
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	w22, w22, w1
	cmp	w28, w0
	uxth	w22, w22
	ldrh	w25, [x27,2834]
	ldrh	w26, [x27,2836]
	beq	.L3290
	adrp	x0, .LANCHOR2
	cmp	w22, 7
	mov	w24, 3
	csinc	w24, w24, wzr, hi
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3292
	ldrh	w5, [x23,124]
	adrp	x0, .LC278
	ldrh	w6, [x23,120]
	add	x0, x0, :lo12:.LC278
	ldrh	w7, [x23,122]
	mov	w1, 1705
	ldrb	w2, [x27,3241]
	mov	w3, w21
	str	w28, [sp]
	mov	w4, w22
	bl	printk
.L3292:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w24
	mov	w2, 1
	ldrb	w0, [x19,3241]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3395
.L3298:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 1
	b	.L3416
.L3290:
	cmp	w24, 1
	bne	.L3294
	bl	gc_scan_static_data
	adrp	x23, .LANCHOR8
	bl	gc_static_wearleveling
	bl	gc_block_vpn_scan
	add	x0, x23, :lo12:.LANCHOR8
	ldr	w0, [x0,3588]
	cbz	w0, .L3296
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	cmp	w1, w28
	beq	.L3297
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x27,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3297
	mov	w1, w24
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3298
.L3297:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3299
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3299
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3298
.L3299:
	add	x27, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x27,3256]
	bl	_list_get_gc_head_node.isra.5
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3300
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x27,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3300
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3298
.L3300:
	add	x23, x23, :lo12:.LANCHOR8
	str	wzr, [x23,3588]
	b	.L3296
.L3294:
	ldrh	w0, [x27,2856]
	cmp	w0, w21
	bcc	.L3395
.L3296:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	x7, [x1,80]
	strb	w0, [x1,3241]
	ldrh	w5, [x7,124]
	cbz	w5, .L3302
	add	x20, x20, :lo12:.LANCHOR4
	strb	wzr, [x1,3241]
	strb	w0, [x20,1098]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3395
	ldrh	w6, [x7,120]
	adrp	x0, .LC279
	ldrh	w7, [x7,122]
	mov	w1, 1762
	mov	w2, 0
	mov	w3, w21
	mov	w4, w22
	add	x0, x0, :lo12:.LC279
	bl	printk
	b	.L3395
.L3302:
	ldrh	w3, [x1,2856]
	cmp	w21, w3
	bcs	.L3305
	cmp	w22, 1
	bls	.L3306
	cmp	w22, 16
	bls	.L3307
	ldrh	w2, [x1,2838]
	ldrh	w1, [x1,3282]
	cmp	w2, w1
	bcs	.L3307
	mov	w1, 3
	mov	w2, 16
	b	.L3414
.L3307:
	mov	w1, 2
	mov	w0, 1
	mov	w2, w1
	add	x19, x19, :lo12:.LANCHOR0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3298
	b	.L3412
.L3306:
	adrp	x0, .LANCHOR2
	strb	w5, [x1,3241]
	ldr	w0, [x0,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3310
	adrp	x0, .LC279
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC279
	mov	w1, 1778
	mov	w2, w5
	mov	w3, w21
	mov	w4, w22
	bl	printk
.L3310:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 16
	ldrb	w0, [x19,3241]
	bls	.L3311
	mov	w1, 3
	mov	w2, 4
	b	.L3414
.L3311:
	mov	w1, 1
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3298
.L3412:
	ldrb	w0, [x19,3241]
	mov	w1, 3
	mov	w2, 2
.L3414:
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3308
.L3305:
	cmp	w24, 1
	lsl	w0, w3, 1
	bne	.L3312
	cmp	w21, w0
	bge	.L3312
	add	w0, w26, w25
	uxth	w0, w0
	cmp	w0, w22, lsr 1
	bcs	.L3313
	ldrh	w3, [x1,2838]
	ldrh	w2, [x1,3282]
	cmp	w3, w2
	bcs	.L3313
	ldrh	w0, [x1,3280]
	lsr	w0, w0, 1
	strh	w0, [x1,2856]
	b	.L3395
.L3313:
	cmp	w22, 1
	bls	.L3314
	cmp	w22, 16
	mov	w0, 1
	bls	.L3315
	mov	w1, 3
	mov	w2, 8
	b	.L3411
.L3315:
	mov	w1, 2
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3316
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 3
	mov	w2, 2
	ldrb	w0, [x0,3241]
.L3411:
	bl	gc_search_src_blk
	uxth	w0, w0
.L3316:
	add	x19, x19, :lo12:.LANCHOR0
	b	.L3413
.L3314:
	cmp	w21, w0
	bcs	.L3317
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w1, 3
	mov	w2, 8
	strb	wzr, [x19,3241]
	bl	gc_search_src_blk
	uxth	w0, w0
.L3413:
	ldrh	w1, [x19,3280]
	strh	w1, [x19,2856]
	b	.L3308
.L3312:
	cmp	w21, w0
	blt	.L3395
.L3317:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,3280]
	lsr	w0, w0, 1
	strh	w0, [x19,2856]
	b	.L3395
.L3308:
	cbnz	w0, .L3298
	b	.L3395
.L3284:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21,2888]
	cmp	w1, w0
	bne	.L3318
	bl	gc_get_src_blk
	strh	w0, [x21,2888]
.L3318:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2888
	ldrh	w3, [x0,2888]
	cmp	w3, w2
	beq	.L3319
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,64]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3321
	mov	x0, 0
.L3320:
	cmp	w5, w0, uxth
	bls	.L3321
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3320
.L3326:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2888]
	b	.L3395
.L3321:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L3369
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3323
.L3369:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3326
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 1972
	add	x1, x1, 808
	add	x0, x0, :lo12:.LC0
	bl	printk
	b	.L3326
.L3323:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
.L3416:
	strb	w0, [x20,1098]
	b	.L3395
.L3319:
	add	x20, x20, :lo12:.LANCHOR4
	strb	wzr, [x20,1098]
	b	.L3395
.L3285:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3327
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 3
	b	.L3416
.L3327:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1,2888]
	cmp	w0, w3
	beq	.L3298
	ldrh	w3, [x1,2912]
	add	x20, x20, :lo12:.LANCHOR4
	cbz	w3, .L3328
	mov	w0, 4
	strh	wzr, [x1,2914]
	strb	w0, [x20,1098]
	b	.L3395
.L3328:
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,72]
	mov	w2, 1
	strb	w2, [x20,1098]
	ldrh	w0, [x1,x0]
	cbz	w0, .L3329
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 808
	mov	w2, 2000
	bl	printk
.L3329:
	add	x20, x19, :lo12:.LANCHOR0
	add	x22, x20, 2888
	ldrh	w0, [x20,2888]
	ldr	x2, [x20,72]
	ubfiz	x1, x0, 1, 16
	ldrh	w21, [x2,x1]
	cbnz	w21, .L3326
	bl	ftl_free_sblk
	ldrh	w1, [x20,2888]
	ldr	x0, [x20,72]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w1, [x22,30]
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, 8
	bhi	.L3331
	strh	w1, [x22,30]
	b	.L3326
.L3331:
	strh	w21, [x22,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, w21
	bl	ftl_info_flush
	b	.L3326
.L3286:
	add	x22, x19, :lo12:.LANCHOR0
.L3396:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x22,2890]
	add	x0, x22, 2888
	ldrh	w1, [x22,3276]
	cmp	w2, w1
	bcs	.L3332
	cmp	w21, 7
	bls	.L3396
	b	.L3395
.L3332:
	ldrh	w3, [x0,24]
	adrp	x2, .LANCHOR2
	cbz	w3, .L3333
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 4
	strh	wzr, [x0,26]
	strb	w1, [x20,1098]
	ldr	w1, [x2,#:lo12:.LANCHOR2]
	tbz	x1, 8, .L3334
	ldrh	w1, [x0]
	adrp	x0, .LC280
	ldr	x4, [x22,72]
	add	x0, x0, :lo12:.LC280
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L3334:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2888]
	ldr	x1, [x0,72]
	ldrh	w0, [x0,2912]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	beq	.L3335
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 808
	mov	w2, 2034
	bl	printk
.L3335:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19,2888]
	ldr	x0, [x19,72]
	ldrh	w2, [x19,2912]
	strh	w2, [x0,x1,lsl 1]
	b	.L3395
.L3333:
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w1, [x0]
	mov	w0, 1
	ldr	x21, [x22,64]
	add	x21, x21, x1, uxth 2
	strb	w0, [x20,1098]
	ldr	w0, [x2,#:lo12:.LANCHOR2]
	tbz	x0, 8, .L3336
	ldrb	w2, [x21,2]
	adrp	x0, .LC281
	add	x0, x0, :lo12:.LC281
	ubfx	x2, x2, 5, 3
	bl	printk
.L3336:
	ldrb	w0, [x21,2]
	tst	w0, 192
	beq	.L3370
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3337
.L3370:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 808
	mov	w2, 2044
	bl	printk
.L3337:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2888
	ldrh	w0, [x19,2888]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,2888]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3339
	strh	w0, [x20,30]
	b	.L3395
.L3339:
	strh	wzr, [x20,30]
	b	.L3417
.L3287:
	cbnz	w24, .L3340
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2856]
	cmp	w0, w21
	bcc	.L3395
.L3340:
	ldrh	w1, [x23,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3341
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w0, [x22,3241]
	cmp	w0, 1
	bne	.L3341
	bl	ftl_flush
	ldrh	w0, [x22,2832]
	cbnz	w0, .L3415
	ldrh	w0, [x22,2830]
.L3415:
	lsr	w0, w0, 1
	mov	w1, 5
	bl	zftl_get_free_sblk
	uxth	w24, w0
	mov	w0, 65535
	mov	w22, w24
	cmp	w24, w0
	beq	.L3344
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x21, [x0,64]
	add	x21, x21, x24, uxth 2
	ldrb	w0, [x21,2]
	tst	w0, 224
	beq	.L3345
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 808
	mov	w2, 2069
	bl	printk
.L3345:
	ldrb	w0, [x21,2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x21,2]
.L3364:
	mov	w0, w22
	mov	w1, 1
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	bl	ftl_erase_sblk
	mov	w0, 5
	add	x1, x23, 96
	strb	w0, [x23,84]
	mov	w0, w22
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x19,3276]
	strb	w0, [x23,89]
	ldrb	w2, [x19,3244]
	mul	w0, w0, w1
	strh	w0, [x23,86]
	mov	w1, 255
	ldrh	w0, [x19,3242]
	strh	w22, [x23,80]
	strh	wzr, [x23,82]
	mul	w2, w0, w2
	ldr	x0, [x20,1104]
	strb	wzr, [x23,85]
	lsl	w2, w2, 2
	strh	wzr, [x23,90]
	bl	ftl_memset
	ldrh	w0, [x19,3242]
	mov	w1, 255
	ldrb	w2, [x19,3244]
	mul	w2, w0, w2
	ldr	x0, [x20,1112]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x19,3242]
	mov	w1, 255
	ldrb	w2, [x19,3244]
	ldr	x0, [x19,3232]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x19,80]
	mov	w1, -1
	str	w24, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x19,2944]
	ldr	x0, [x19,2848]
	strh	w22, [x0,126]
	mov	w0, -1
	str	w0, [x19,3208]
	mov	w0, 0
	strh	wzr, [x19,3202]
	strh	wzr, [x19,3204]
	strh	wzr, [x19,3206]
	bl	ftl_info_flush
	b	.L3395
.L3341:
	cmp	w24, 1
	mov	w22, 4
	csinc	w22, w22, wzr, eq
	cmp	w21, 15
	add	w0, w22, 4
	add	x21, x19, :lo12:.LANCHOR0
	csel	w22, w0, w22, ls
.L3348:
	sub	w22, w22, #1
	uxtb	w22, w22
	cmp	w22, 255
	beq	.L3395
	bl	gc_do_copy_back
	ldrb	w0, [x21,3241]
	cbnz	w0, .L3349
	ldrb	w0, [x21,2818]
	cmp	w0, 3
	bhi	.L3350
	bl	ftl_write_commit
.L3350:
	ldrh	w1, [x21,2914]
	ldrh	w0, [x21,2912]
	cmp	w1, w0
	bcc	.L3348
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x20,1098]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x21,2888]
	ldr	x0, [x21,72]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L3352
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 808
	mov	w2, 2144
	bl	printk
.L3352:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x20, 2888
	ldrh	w1, [x20,2888]
	ldr	x3, [x20,72]
	ubfiz	x0, x1, 1, 16
	ldrh	w2, [x3,x0]
	cbnz	w2, .L3353
	strh	w2, [x3,x0]
	ldrh	w0, [x20,2888]
	bl	ftl_free_sblk
	b	.L3326
.L3353:
	ldrh	w3, [x21,24]
	adrp	x0, .LC282
	add	x0, x0, :lo12:.LC282
	bl	printk
	ldrh	w3, [x21,24]
	adrp	x0, .LC283
	ldr	x1, [x20,3216]
	add	x0, x0, :lo12:.LC283
	mov	w2, 4
	add	w3, w3, 1
	bl	rknand_print_hex
	ldrh	w1, [x20,2888]
	adrp	x0, .LC284
	ldr	x3, [x20,72]
	add	x0, x0, :lo12:.LC284
	ubfiz	x2, x1, 1, 16
	ldrh	w4, [x21,26]
	ldrh	w5, [x21,28]
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,24]
	bl	printk
	ldrh	w0, [x20,2888]
	mov	x1, 0
	bl	ftl_sblk_dump
	bl	gc_scan_src_blk
	ldrh	w1, [x20,2888]
	adrp	x0, .LC285
	ldr	x3, [x20,72]
	add	x0, x0, :lo12:.LC285
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x21,24]
	bl	printk
	adrp	x0, .LC286
	ldr	x1, [x20,3216]
	ldrh	w3, [x21,24]
	add	x0, x0, :lo12:.LC286
	mov	w2, 4
	bl	rknand_print_hex
	b	.L3326
.L3349:
	add	x0, x21, 2888
	ldrh	w1, [x0,318]
	cbz	w1, .L3354
	strh	wzr, [x0,318]
	ldr	w0, [x0,320]
	cmn	w0, #1
	beq	.L3355
	bl	ftl_mask_bad_block
.L3355:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,80]
	str	wzr, [x21,3208]
	strh	wzr, [x21,2944]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x21,80]
	mov	w0, -1
	ldr	x2, [x21,2848]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x21,2896]
	cbz	x0, .L3356
	bl	buf_free
.L3356:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	str	xzr, [x19,2896]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2177
	add	x1, x1, 808
	add	x0, x0, :lo12:.LC0
	strb	wzr, [x20,1098]
	bl	printk
	b	.L3395
.L3354:
	ldrh	w1, [x23,86]
	cmp	w1, 1
	bls	.L3357
	ldrh	w2, [x0,26]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L3348
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x20,1098]
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
	b	.L3395
.L3357:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 5
	ldrh	w2, [x0,26]
	strb	w1, [x20,1098]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L3358
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
.L3358:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,80]
	ldrh	w1, [x23,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,2904]
	ldrb	w2, [x20,3225]
	ldrh	w1, [x20,3276]
	strh	w1, [x20,2906]
	cmp	w2, 2
	strh	w2, [x20,2908]
	bne	.L3359
	lsl	w1, w1, 1
	strh	w1, [x20,2906]
	mov	w1, 1
	strh	w1, [x20,2908]
.L3359:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2910]
	b	.L3395
.L3365:
	mov	w22, 0
	add	x25, x19, :lo12:.LANCHOR0
.L3288:
	bl	gc_check_data_one_wl
	cbz	w0, .L3361
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x19,80]
	strh	wzr, [x19,2944]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,80]
	mov	w0, -1
	ldr	x2, [x19,2848]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,2896]
	bl	buf_free
	str	xzr, [x19,2896]
	strb	wzr, [x20,1098]
.L3417:
	bl	flt_sys_flush
	b	.L3395
.L3361:
	ldrh	w1, [x25,2904]
	ldrh	w0, [x25,2906]
	cmp	w1, w0
	bcc	.L3362
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 6
	strb	w0, [x20,1098]
	ldr	x0, [x25,2896]
	bl	buf_free
	str	xzr, [x25,2896]
	b	.L3395
.L3362:
	cmp	w21, 7
	bls	.L3288
	cmp	w24, 1
	bne	.L3395
	add	w22, w22, 1
	uxtb	w22, w22
	cmp	w22, 4
	bls	.L3288
	b	.L3395
.L3289:
	bl	gc_update_l2p_map_new
	mov	w21, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	add	x20, x20, :lo12:.LANCHOR4
	bl	pm_flush
	strh	w21, [x23,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,2848]
	strh	w21, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
	strb	wzr, [x20,1098]
	bl	print_ftl_debug_info
	b	.L3395
.L3344:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2075
	add	x1, x1, 808
	add	x0, x0, :lo12:.LC0
	bl	printk
	b	.L3364
.L3395:
	sub	sp, x29, #16
	mov	w0, 16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 112
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
	tbz	x0, 12, .L3419
	adrp	x0, .LC287
	mov	w1, w19
	add	x0, x0, :lo12:.LC287
	mov	w2, w28
	mov	w3, w23
	bl	printk
.L3419:
	cbnz	w19, .L3420
	adrp	x0, .LANCHOR0+4
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+4]
	b	.L3421
.L3420:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3422
	lsl	w19, w19, 13
	mov	w2, 8192
.L3421:
	add	w1, w28, w23
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3422
	add	w28, w19, w28
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	sub	w22, w23, #1
	add	w22, w22, w28
	add	w26, w28, w23
	ldrb	w0, [x0,2816]
	udiv	w24, w28, w0
	mov	w21, w24
	udiv	w22, w22, w0
	sub	w20, w22, w24
	add	w20, w20, 1
.L3423:
	cbz	w20, .L3442
	mov	w0, 0
	bl	buf_alloc
	mov	x27, x0
	cbnz	x0, .L3424
	bl	ftl_write_commit
	b	.L3423
.L3424:
	strb	wzr, [x0,57]
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w21, w24
	ldrb	w4, [x0,2816]
	cset	w0, eq
	strb	w4, [x27,56]
	cmp	w21, w22
	beq	.L3426
	cbz	w0, .L3427
	b	.L3436
.L3426:
	cbz	w0, .L3428
.L3436:
	udiv	w0, w28, w4
	msub	w0, w0, w4, w28
	uxtb	w0, w0
	strb	w0, [x27,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w23
	csel	w4, w23, w4, hi
	b	.L3440
.L3428:
	msub	w4, w4, w21, w26
.L3440:
	strb	w4, [x27,56]
.L3427:
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
	ldr	x1, [x0,2848]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x27,32]
	mov	x0, x27
	bl	ftl_write_buf
	ldrb	w0, [x27,56]
	add	x25, x25, x0, lsl 9
	b	.L3423
.L3442:
	bl	ftl_write_commit
	mov	w0, w20
.L3441:
	mov	w1, 1
	bl	zftl_do_gc
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,2828]
	ldrh	w0, [x1,2832]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L3443
	mov	w0, 0
	b	.L3441
.L3443:
	bl	timer_get_time
	adrp	x1, .LANCHOR8+3592
	str	w0, [x1,#:lo12:.LANCHOR8+3592]
	mov	w0, 0
.L3422:
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
	ldr	x0, [x0,88]
	ldrb	w2, [x0,19]
	add	x0, x20, :lo12:.LANCHOR4
	ldrb	w0, [x0,1232]
	cbnz	w0, .L3446
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L3448
	cmp	w2, 68
	beq	.L3448
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L3446
.L3448:
	adrp	x24, .LC288
	mov	w25, w1
	add	x24, x24, :lo12:.LC288
	add	x20, x20, :lo12:.LANCHOR4
	mov	w27, 65535
.L3450:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,88]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L3446
	add	x22, x20, 36
	add	w26, w19, w25
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	ldrh	w3, [x22,w19,sxtw 1]
	bl	printk
	ldrh	w0, [x22,w19,sxtw 1]
	cmp	w0, w27
	bne	.L3446
	ldr	x0, [x20,1160]
	mov	w1, 255
	mov	w2, 16384
	add	w19, w19, 1
	bl	ftl_memset
	uxth	w19, w19
	ldr	x4, [x28,88]
	mov	w0, w23
	ldr	x2, [x20,1160]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L3450
.L3446:
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
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	x26, x1
	ldr	x0, [x0,88]
	ldrb	w23, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x22, x0
	mov	w0, -1
	cbz	x22, .L3486
	add	w19, w19, 511
	add	x2, x26, 253952
	lsr	w19, w19, 9
	add	x2, x2, 2044
	mov	w0, 0
	mov	w24, 63999
	mov	w3, 4097
.L3457:
	ldr	w1, [x2]
	cbnz	w1, .L3454
	ldr	w1, [x26,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w3
	sub	w24, w24, #1
	csel	w0, w0, wzr, cc
	str	w1, [x2],-4
	cmp	w24, 4096
	bne	.L3457
	b	.L3456
.L3454:
	adrp	x0, .LC289
	mov	w2, w24
	add	x0, x0, :lo12:.LC289
	bl	printk
.L3456:
	add	x0, x21, :lo12:.LANCHOR0
	add	w1, w19, 128
	mul	w20, w20, w23
	mov	w25, 0
	ldr	x2, [x0,88]
	ldrh	w0, [x2,10]
	ldrb	w19, [x2,12]
	mov	w2, w24
	lsl	w0, w0, 2
	sdiv	w19, w0, w19
	mov	w0, 500
	cmp	w19, 500
	csel	w19, w19, w0, ls
	adrp	x0, .LC290
	cmp	w19, w1
	add	x0, x0, :lo12:.LC290
	csel	w19, w19, w1, ls
	ldr	w1, [x26,w24,uxtw 2]
	mov	w3, w19
	bl	printk
	str	w25, [x29,140]
	uxth	w0, w20
	str	w0, [x29,116]
.L3476:
	mov	w1, 0
	mov	x0, x22
	mov	w2, 512
	add	x24, x21, :lo12:.LANCHOR0
	bl	ftl_memset
	ldr	x0, [x24,192]
	ldr	w1, [x29,140]
	add	x0, x0, x1, uxtw
	ldrb	w20, [x0,32]
	cmp	w20, 255
	beq	.L3458
	ldr	w0, [x29,116]
	mul	w20, w20, w0
	ldr	x0, [x24,88]
	ldrb	w27, [x0,9]
	ldrh	w23, [x0,10]
	ldrb	w0, [x24,184]
	udiv	w1, w20, w27
	str	w0, [x29,112]
	mul	w23, w27, w23
	mov	w0, 0
	strb	wzr, [x24,184]
	uxth	w23, w23
	bl	flash_erase_block
	udiv	w0, w20, w23
	msub	w0, w0, w23, w20
	str	w0, [x29,136]
	sub	w28, w20, w0
	cmp	w20, w28
	bne	.L3480
	ldrb	w0, [x21,#:lo12:.LANCHOR0]
	cmp	w0, 9
	bne	.L3480
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 1024
	ldr	x3, [x0,1216]
	str	x3, [x29,128]
	mov	x0, x3
	bl	ftl_memset
	ldr	x3, [x29,128]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	mov	w1, 12
	cmp	w27, 8
	str	w0, [x3]
	str	w1, [x3,4]
	str	wzr, [x3,12]
	ldr	x2, [x24,88]
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
	ldr	x3, [x29,128]
	str	w0, [x3,8]
	sub	w0, w19, #4
	str	w0, [x29,128]
	b	.L3459
.L3480:
	str	w19, [x29,128]
	mov	x3, 0
.L3459:
	str	x26, [x29,120]
	mov	w24, 0
	add	x23, x21, :lo12:.LANCHOR0
.L3461:
	cmp	w24, w19
	bcs	.L3494
	ldr	w0, [x29,136]
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w24, w0
	add	x0, x23, 212
	ubfx	x4, x4, 2, 16
	add	w1, w4, 1
	ldrb	w2, [x2,1232]
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L3463
	ldrb	w6, [x23,208]
	lsl	w0, w1, 1
	cmp	w6, wzr
	csel	w0, w0, w1, ne
.L3463:
	ldrb	w1, [x23]
	cmp	w1, 9
	beq	.L3493
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L3493:
	str	w0, [x29,144]
	mov	w0, 61424
	str	w0, [x29,148]
	add	x0, x23, 212
	ldrh	w0, [x0,w4,sxtw 1]
	cbnz	w2, .L3467
	mov	w4, w0
	b	.L3468
.L3467:
	ldrb	w1, [x23,208]
	lsl	w0, w4, 1
	cmp	w1, wzr
	csel	w4, w0, w4, ne
.L3468:
	mul	w0, w4, w27
	cbnz	x3, .L3469
	ldr	x1, [x29,120]
	add	w0, w0, w28
	add	x2, x29, 144
	str	x4, [x29,104]
	bl	fw_flash_page_prog.constprop.31
	adrp	x0, .LANCHOR4
	ldr	x4, [x29,104]
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1232]
	cbnz	w0, .L3470
	udiv	w1, w28, w27
	add	w2, w4, 1
	bl	id_block_prog_msb_ff_data
.L3470:
	ldr	x0, [x29,120]
	add	x0, x0, 2048
	str	x0, [x29,120]
	b	.L3471
.L3469:
	add	w0, w0, w28
	mov	x1, x3
	add	x2, x29, 144
	bl	fw_flash_page_prog.constprop.31
.L3471:
	add	w24, w24, 4
	mov	x3, 0
	uxth	w24, w24
	b	.L3461
.L3494:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,112]
	mov	x2, x22
	strb	w1, [x0,184]
	mov	w0, w20
	ldr	w1, [x29,128]
	bl	id_block_read_data.constprop.30
	ldr	w0, [x29,128]
	lsl	w23, w0, 7
	mov	x0, 0
.L3473:
	cmp	w23, w0
	bls	.L3495
	ldr	w2, [x22,x0,lsl 2]
	add	x0, x0, 1
	add	x1, x26, x0, lsl 2
	ldr	w1, [x1,-4]
	cmp	w2, w1
	beq	.L3473
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L3458
.L3495:
	add	w25, w25, 1
	cmp	w25, 5
	bhi	.L3477
.L3458:
	ldr	w0, [x29,140]
	add	w0, w0, 1
	str	w0, [x29,140]
	cmp	w0, 4
	bne	.L3476
.L3477:
	mov	x0, x22
	bl	ftl_free
	cmp	w25, wzr
	csetm	w0, eq
.L3486:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 208
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
.L3497:
	cmp	w1, w2
	bls	.L3499
	ldrb	w4, [x5,x2]
	add	x6, x3, 2296
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3497
.L3499:
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
	str	x21, [sp,32]
	mov	w19, w1
	mov	x20, x2
	beq	.L3502
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3503
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3504
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3505
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3506
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3507
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3563
	bl	rknand_dev_flush
	b	.L3597
.L3507:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3509
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3510
	b	.L3563
.L3505:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3511
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3512
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3513
	b	.L3563
.L3512:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3511
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3511
	b	.L3563
.L3503:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3514
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3515
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3516
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3517
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3518
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3519
	b	.L3563
.L3515:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3520
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3521
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3522
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3523
	b	.L3563
.L3521:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3523
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3520
	b	.L3563
.L3517:
	adrp	x0, .LC291
	add	x0, x0, :lo12:.LC291
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3524
.L3529:
	mov	x0, -12
	b	.L3501
.L3524:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3604
	adrp	x0, .LC293
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC293
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3601
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
	cbz	x0, .L3607
	adrp	x0, .LC294
	add	x0, x0, :lo12:.LC294
.L3599:
	bl	printk
.L3601:
	mov	x0, x19
.L3602:
	bl	ftl_free
.L3600:
	mov	x0, -14
	b	.L3501
.L3516:
	adrp	x0, .LC295
	add	x0, x0, :lo12:.LC295
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3529
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3604
	ldr	w1, [x19]
	adrp	x0, .LC296
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC296
	bl	printk
	adrp	x0, .LANCHOR8
	add	x21, x0, :lo12:.LANCHOR8
	mov	x20, x0
	ldr	x1, [x21,3600]
	cbnz	x1, .L3531
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x21,3600]
	cbz	x0, .L3601
.L3531:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3601
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3601
	add	x20, x20, :lo12:.LANCHOR8
	uxtw	x2, w2
	ldr	x1, [x20,3600]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3607:
	mov	x0, x19
	bl	ftl_free
	b	.L3597
.L3519:
	adrp	x0, .LC297
	add	x0, x0, :lo12:.LC297
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3529
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3533
.L3604:
	adrp	x0, .LC292
	add	x0, x0, :lo12:.LC292
	b	.L3599
.L3533:
	ldr	w1, [x19]
	adrp	x0, .LC298
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC298
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3601
	adrp	x20, .LANCHOR8
	add	x20, x20, :lo12:.LANCHOR8
	ldr	x0, [x20,3600]
	cbz	x0, .L3601
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3536
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3501
.L3536:
	bl	rknand_device_lock
	ldr	x1, [x20,3600]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,3600]
	bl	ftl_free
	str	xzr, [x20,3600]
	b	.L3607
.L3518:
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3529
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3609
.L3502:
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3529
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3609
.L3514:
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
	bl	printk
	adrp	x0, .LANCHOR8+3608
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR8+3608]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3592:
	bl	rk_copy_to_user
	cbnz	x0, .L3600
	b	.L3597
.L3522:
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3529
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
.L3609:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3602
	bl	ftl_free
	b	.L3528
.L3506:
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3541
.L3546:
	adrp	x0, .LC292
	add	x0, x0, :lo12:.LC292
	bl	printk
	b	.L3600
.L3541:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3542
.L3543:
	mov	x19, -1
	b	.L3528
.L3542:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3543
	adrp	x21, .LANCHOR8
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR8
	mov	x2, 512
	ldr	x1, [x21,3608]
	bl	memcpy
	ldr	w1, [x21,3616]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3544
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3544:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3606
.L3509:
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3543
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3543
	adrp	x1, .LANCHOR8
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR8
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,3616]
	cmp	w3, w2
	bne	.L3501
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3501
	ldr	x19, [x1,3608]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3598
.L3513:
	adrp	x0, .LC305
	add	x0, x0, :lo12:.LC305
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3543
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3543
	adrp	x19, .LANCHOR8
	add	x0, x19, :lo12:.LANCHOR8
	ldr	w1, [x0,3620]
	cbnz	w1, .L3547
.L3550:
	mov	x0, 0
	b	.L3501
.L3547:
	ldr	x1, [x0,3624]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3548
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,3624]
	str	w2, [x1,4]
	ldr	x0, [x0,3624]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3548:
	add	x20, x19, :lo12:.LANCHOR8
	mov	w0, 0
	ldr	x1, [x20,3624]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,3608]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3549
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,3608]
	str	w1, [x0,4]
	ldr	x0, [x20,3608]
	str	wzr, [x0,8]
.L3549:
	add	x19, x19, :lo12:.LANCHOR8
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,3608]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,3620]
	str	wzr, [x19,3616]
	b	.L3597
.L3504:
	adrp	x0, .LC306
	add	x0, x0, :lo12:.LC306
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3543
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3543
	adrp	x19, .LANCHOR8
	add	x1, x19, :lo12:.LANCHOR8
	ldr	w2, [x1,3620]
	cmp	w2, 1
	beq	.L3550
	ldr	x2, [x1,3624]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3551
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,3624]
	str	w3, [x2,4]
	ldr	x1, [x1,3624]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3551:
	add	x20, x19, :lo12:.LANCHOR8
	mov	w0, 1
	ldr	x1, [x20,3624]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,3608]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3552
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,3608]
	str	w1, [x0,4]
	ldr	x0, [x20,3608]
	str	wzr, [x0,8]
.L3552:
	add	x19, x19, :lo12:.LANCHOR8
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,3608]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,3620]
	b	.L3597
.L3510:
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3543
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3543
	adrp	x1, .LANCHOR8
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR8
	uxtw	x2, w2
	add	x1, x1, 3632
	b	.L3605
.L3511:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3553
	adrp	x0, .LC308
	add	x0, x0, :lo12:.LC308
	b	.L3595
.L3553:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3555
	adrp	x0, .LC309
	add	x0, x0, :lo12:.LC309
	b	.L3595
.L3555:
	adrp	x0, .LC310
	add	x0, x0, :lo12:.LC310
.L3595:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3600
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3556
	adrp	x0, .LANCHOR8+3608
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,#:lo12:.LANCHOR8+3608]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3501
	b	.L3600
.L3556:
	adrp	x0, .LANCHOR9
	add	x20, x0, :lo12:.LANCHOR9
	ldr	w1, [x20,-208]
	cmp	w1, 10
	bhi	.L3600
	adrp	x1, .LANCHOR8+3608
	ldr	w2, [x29,60]
	ldr	x1, [x1,#:lo12:.LANCHOR8+3608]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3557
	cbz	w3, .L3557
	adrp	x0, .LC311
	mov	w1, w2
	add	x0, x0, :lo12:.LC311
	bl	printk
	ldr	w0, [x20,-208]
	add	w0, w0, 1
	str	w0, [x20,-208]
	b	.L3600
.L3557:
	add	x0, x0, :lo12:.LANCHOR9
	str	wzr, [x0,-208]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3558
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3559
.L3558:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3559:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3597
	b	.L3528
.L3523:
	adrp	x0, .LC312
	add	x0, x0, :lo12:.LC312
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3543
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3543
	mov	w0, 30224
	adrp	x1, .LANCHOR9
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR9
	add	x0, x29, 64
	bne	.L3560
	ldr	x1, [x1,-200]
	b	.L3596
.L3560:
	ldr	x1, [x1,-192]
.L3596:
	add	x1, x1, 8
.L3605:
	bl	memcpy
.L3606:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3592
.L3520:
	adrp	x0, .LC313
	add	x0, x0, :lo12:.LC313
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3546
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3543
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3543
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR9
	add	x19, x19, :lo12:.LANCHOR9
	bne	.L3562
	ldr	x0, [x19,-200]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,-200]
	b	.L3598
.L3562:
	ldr	x0, [x19,-192]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,-192]
	mov	w0, 3
.L3598:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3528
.L3597:
	mov	x19, 0
.L3528:
	adrp	x0, .LC314
	mov	x1, x19
	add	x0, x0, :lo12:.LC314
	bl	printk
	mov	x0, x19
	b	.L3501
.L3563:
	mov	x0, -22
.L3501:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	add	sp, sp, 528
	ret
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -64]!
	adrp	x0, .LANCHOR7+3304
	mov	w2, 512
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR8
	ldr	x1, [x0,#:lo12:.LANCHOR7+3304]
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR8
	adrp	x20, .LANCHOR9
	str	x23, [sp,48]
	add	x23, x20, :lo12:.LANCHOR9
	add	x0, x1, 512
	str	x0, [x19,3608]
	add	x0, x1, 1024
	str	x1, [x19,3624]
	add	x1, x1, 1536
	str	x0, [x23,-200]
	add	x0, x19, 3632
	str	x1, [x23,-192]
	str	xzr, [x19,3600]
	bl	ftl_memcpy
	ldr	x22, [x19,3624]
	str	wzr, [x23,-208]
	str	wzr, [x19,3616]
	ldr	w0, [x22,16]
	ldr	w23, [x22,508]
	str	w0, [x19,3620]
	cbz	w23, .L3612
	mov	x0, x22
	mov	w1, 508
	bl	js_hash
	cmp	w23, w0
	beq	.L3612
	adrp	x0, .LC315
	str	wzr, [x22,16]
	add	x0, x0, :lo12:.LC315
	str	wzr, [x19,3620]
	bl	printk
.L3612:
	add	x21, x21, :lo12:.LANCHOR8
	ldr	w0, [x21,3620]
	cbz	w0, .L3613
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	str	w0, [x21,3616]
.L3613:
	add	x20, x20, :lo12:.LANCHOR9
	mov	w0, 2
	ldr	x1, [x20,-200]
	bl	StorageSysDataLoad
	ldr	x1, [x20,-192]
	mov	w0, 3
	bl	StorageSysDataLoad
	bl	rknand_sys_storage_init
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
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
	adrp	x21, .LANCHOR9
	bl	ftl_malloc
	add	x1, x21, :lo12:.LANCHOR9
	mov	w19, -12
	str	x0, [x1,-184]
	cbz	x0, .L3622
	mov	w23, 0
	adrp	x24, .LC316
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC316
.L3626:
	add	x20, x21, :lo12:.LANCHOR9
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,-184]
	bl	FlashBootVendorRead
	cbnz	w0, .L3623
	ldr	x3, [x20,-184]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,-184]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3624
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3624
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3624:
	cbnz	w22, .L3634
	mov	w22, 1
	b	.L3626
.L3634:
	cbz	w19, .L3627
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3622
	b	.L3623
.L3627:
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
	b	.L3622
.L3623:
	add	x21, x21, :lo12:.LANCHOR9
	mov	w19, -1
	ldr	x0, [x21,-184]
	bl	kfree
	str	xzr, [x21,-184]
.L3622:
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
	adrp	x3, .LANCHOR9-184
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR9-184]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3636
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3637:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3641
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3637
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
	b	.L3636
.L3641:
	mov	w3, -1
.L3636:
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
	adrp	x0, .LANCHOR9-184
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR9-184]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3643
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3644:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3659
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3644
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3645
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3643
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3646:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3660
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
	b	.L3646
.L3660:
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
	b	.L3658
.L3645:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3658
.L3659:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3643
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
.L3658:
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
.L3643:
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
	cbz	x0, .L3662
	mov	w2, 30209
	ldr	x1, [x29,56]
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3664
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w1, w2
	beq	.L3665
	b	.L3676
.L3664:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3676
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3667
.L3668:
	mov	x20, -1
	b	.L3663
.L3667:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3668
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3663
.L3665:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3676
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3668
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3668
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3676
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3663
.L3676:
	mov	x20, -14
.L3663:
	mov	x0, x19
	bl	kfree
.L3662:
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
	.global	ftl_ext_info_first_write
	.global	ftl_sys_info_first_write
	.global	ftl_low_format_cur_blk
	.global	ftl_power_lost_flag
	.global	ftl_vpn_update_count
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
	.global	check_vpc_tbl
	.global	p_read_ahead_ext_buf
	.global	discard_sector_count
	.global	read_ahead_lpa
	.global	write_commit_count
	.global	write_commit_head
	.global	write_buf_count
	.global	write_buf_head
	.global	ftl_flush_jiffies
	.global	lpa_hash
	.global	lpa_hash_index
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
	.type	__func__.33843, %object
	.size	__func__.33843, 18
__func__.33843:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.33868, %object
	.size	__func__.33868, 23
__func__.33868:
	.string	"_list_update_data_list"
	.zero	1
	.type	__func__.18878, %object
	.size	__func__.18878, 22
__func__.18878:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.18959, %object
	.size	__func__.18959, 11
__func__.18959:
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
	.type	__func__.18487, %object
	.size	__func__.18487, 28
__func__.18487:
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
	.type	__func__.18537, %object
	.size	__func__.18537, 22
__func__.18537:
	.string	"flash_start_page_read"
	.zero	2
	.type	__func__.18462, %object
	.size	__func__.18462, 26
__func__.18462:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.18473, %object
	.size	__func__.18473, 21
__func__.18473:
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
	.type	__func__.19129, %object
	.size	__func__.19129, 13
__func__.19129:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.19142, %object
	.size	__func__.19142, 10
__func__.19142:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.19156, %object
	.size	__func__.19156, 16
__func__.19156:
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
	.type	__func__.33231, %object
	.size	__func__.33231, 13
__func__.33231:
	.string	"ftl_read_buf"
	.zero	3
	.type	__func__.33449, %object
	.size	__func__.33449, 12
__func__.33449:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.33514, %object
	.size	__func__.33514, 19
__func__.33514:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.33565, %object
	.size	__func__.33565, 16
__func__.33565:
	.string	"gc_free_src_blk"
	.type	__func__.34037, %object
	.size	__func__.34037, 18
__func__.34037:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.34047, %object
	.size	__func__.34047, 17
__func__.34047:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.34151, %object
	.size	__func__.34151, 23
__func__.34151:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.34191, %object
	.size	__func__.34191, 22
__func__.34191:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.34202, %object
	.size	__func__.34202, 22
__func__.34202:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.33172, %object
	.size	__func__.33172, 14
__func__.33172:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.33999, %object
	.size	__func__.33999, 15
__func__.33999:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.34624, %object
	.size	__func__.34624, 17
__func__.34624:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.34652, %object
	.size	__func__.34652, 21
__func__.34652:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.18868, %object
	.size	__func__.18868, 20
__func__.18868:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.33109, %object
	.size	__func__.33109, 17
__func__.33109:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.34588, %object
	.size	__func__.34588, 13
__func__.34588:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.19179, %object
	.size	__func__.19179, 21
__func__.19179:
	.string	"flash_into_data_init"
	.zero	3
	.type	__func__.18418, %object
	.size	__func__.18418, 19
__func__.18418:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.19168, %object
	.size	__func__.19168, 20
__func__.19168:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.18552, %object
	.size	__func__.18552, 25
__func__.18552:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.33467, %object
	.size	__func__.33467, 21
__func__.33467:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.18524, %object
	.size	__func__.18524, 22
__func__.18524:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.34514, %object
	.size	__func__.34514, 15
__func__.34514:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.18511, %object
	.size	__func__.18511, 26
__func__.18511:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.18447, %object
	.size	__func__.18447, 19
__func__.18447:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.34339, %object
	.size	__func__.34339, 14
__func__.34339:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.33967, %object
	.size	__func__.33967, 15
__func__.33967:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.34227, %object
	.size	__func__.34227, 19
__func__.34227:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.34239, %object
	.size	__func__.34239, 18
__func__.34239:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.34285, %object
	.size	__func__.34285, 15
__func__.34285:
	.string	"ftl_low_format"
	.zero	1
	.type	__func__.34329, %object
	.size	__func__.34329, 23
__func__.34329:
	.string	"ftl_re_low_format_test"
	.zero	1
	.type	__func__.34125, %object
	.size	__func__.34125, 23
__func__.34125:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.34634, %object
	.size	__func__.34634, 14
__func__.34634:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.19201, %object
	.size	__func__.19201, 17
__func__.19201:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.18922, %object
	.size	__func__.18922, 16
__func__.18922:
	.string	"nand_flash_init"
	.type	__func__.34106, %object
	.size	__func__.34106, 16
__func__.34106:
	.string	"ftl_sysblk_dump"
	.type	__func__.34574, %object
	.size	__func__.34574, 16
__func__.34574:
	.string	"load_l2p_region"
	.type	__func__.34608, %object
	.size	__func__.34608, 6
__func__.34608:
	.string	"pm_gc"
	.zero	2
	.type	__func__.33223, %object
	.size	__func__.33223, 15
__func__.33223:
	.string	"ftl_read_ahead"
	.zero	1
	.type	__func__.33501, %object
	.size	__func__.33501, 22
__func__.33501:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.33669, %object
	.size	__func__.33669, 20
__func__.33669:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.33727, %object
	.size	__func__.33727, 18
__func__.33727:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.34082, %object
	.size	__func__.34082, 14
__func__.34082:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.33269, %object
	.size	__func__.33269, 10
__func__.33269:
	.string	"zftl_read"
	.zero	6
	.type	__func__.33336, %object
	.size	__func__.33336, 10
__func__.33336:
	.string	"vpn_check"
	.zero	6
	.type	__func__.33610, %object
	.size	__func__.33610, 16
__func__.33610:
	.string	"gc_scan_src_blk"
	.type	__func__.34172, %object
	.size	__func__.34172, 19
__func__.34172:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.33161, %object
	.size	__func__.33161, 17
__func__.33161:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.33543, %object
	.size	__func__.33543, 16
__func__.33543:
	.string	"gc_do_copy_back"
	.type	__func__.33295, %object
	.size	__func__.33295, 13
__func__.33295:
	.string	"_ftl_discard"
	.zero	3
	.type	__func__.33754, %object
	.size	__func__.33754, 11
__func__.33754:
	.string	"zftl_do_gc"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"%s\n"
.LC2:
	.string	"FTL version: 6.0.6 20180402"
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
	.string	"gc vpn th = %d %d %d %d\n"
.LC83:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC84:
	.string	"status: %x, ppa: %x\n"
.LC85:
	.string	"gc_write_completed: %x  %x  %x %x\n"
.LC86:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC87:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC88:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC89:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC90:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC91:
	.string	"list count:%p %d\n"
.LC92:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC93:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC94:
	.string	"gc_free_src_blk %x, %d\n"
.LC95:
	.string	"ftl_vpn_decrement %x = %d\n"
.LC96:
	.string	"mask bad block:cs %x block: %x\n"
.LC97:
	.string	"free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC98:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC99:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC100:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC101:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC102:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC103:
	.string	"alloc sblk %x %d\n"
.LC104:
	.string	"dump_sblk_queue: %d\n"
.LC105:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC106:
	.string	"blk %x is bad block\n"
.LC107:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC108:
	.string	"%s %d %d\n"
.LC109:
	.string	"gc_static_wearleveling: slc blk: %d, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC110:
	.string	"gc_static_wearleveling: min slc ec: %d, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC111:
	.string	"swl add tlc gc = %d, %d, %d, %d, %d, %d\n"
.LC112:
	.string	"swl add slc gc  = %d, %d, %d, %d, %d, %d\n"
.LC113:
	.string	"free blk vpn error: %x %x\n"
.LC114:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC115:
	.string	"...%d @ %s\n"
.LC116:
	.string	"rk_ftl_de_init %x\n"
.LC117:
	.string	"%s %p:0x%x:"
.LC118:
	.string	"%x "
.LC119:
	.string	""
.LC120:
	.string	"otp error! %d"
.LC121:
	.string	"rr"
.LC122:
	.string	"flash_abort_clear = %d\n"
.LC123:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC124:
	.string	"nandc:"
.LC125:
	.string	"%d flReg.d32=%x %x\n"
.LC126:
	.string	"nandc_xfer_done read error %x\n"
.LC127:
	.string	"dqs data abort %x\n"
.LC128:
	.string	"dqs data timeout %x\n"
.LC129:
	.string	"xfer error %x\n"
.LC130:
	.string	"flash_read_page %x %x %x\n"
.LC131:
	.string	"micron %d row=%x,count %d,status=%d\n"
.LC132:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC133:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC134:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC135:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC136:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC137:
	.string	"%d flash_ddr_tunning_read %x ecc=%d\n"
.LC138:
	.string	"sync para %d\n"
.LC139:
	.string	"DDR mode Read error %x %x\n"
.LC140:
	.string	"flash_read_page_en %x %x\n"
.LC141:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC142:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC143:
	.string	"...%d @ %s %d %p\n"
.LC144:
	.string	"no sys info %x\n"
.LC145:
	.string	"read page: %x %x %x %x\n"
.LC146:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC147:
	.string	"set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC148:
	.string	"1ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC149:
	.string	"flash_prog_page %x %x %x\n"
.LC150:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC151:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC152:
	.string	"flash_prog_page_en:%x %x\n"
.LC153:
	.string	"spare"
.LC154:
	.string	"data"
.LC155:
	.string	"write error: %x\n"
.LC156:
	.string	"low format %d\n"
.LC157:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC158:
	.string	"%d %x @%d %x\n"
.LC159:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC160:
	.string	"low format %d %d %d %d\n"
.LC161:
	.string	"re low format %d\n"
.LC162:
	.string	"saved_active_page  = %x\n"
.LC163:
	.string	"saved_active_plane = %x\n"
.LC164:
	.string	"sblk = %x\n"
.LC165:
	.string	"phy_blk = %x %x\n"
.LC166:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC167:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC168:
	.string	"dump write = %x\n"
.LC169:
	.string	"pm_write_page write error: %x\n"
.LC170:
	.string	"finfo:"
.LC171:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC172:
	.string	"sys_info_flush error:%x\n"
.LC173:
	.string	"l2p:"
.LC174:
	.string	"free_buf_count: %d\n"
.LC175:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC176:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC177:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC178:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC179:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC180:
	.string	"lpa:"
.LC181:
	.string	"vpn:"
.LC182:
	.string	"sblk:"
.LC183:
	.string	"lpa_hash:"
.LC184:
	.string	"lpa_hash_index:"
.LC185:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC186:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC187:
	.string	"load_l2p_region no ppa = %x , %x, all setting 0xff....\n"
.LC188:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC189:
	.string	"pm_ppa:"
.LC190:
	.string	"data:"
.LC191:
	.string	"spare:"
.LC192:
	.string	"pm_init posr %x %x %x\n"
.LC193:
	.string	"pm_init recovery %x %x %x\n"
.LC194:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC195:
	.string	"ftl_read_ahead refresh =%x, lpa = %x, ppa= %x\n"
.LC196:
	.string	"id=%d, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC197:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC198:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC199:
	.string	"gc_recovery"
.LC200:
	.string	"gc_recovery: %x vpn = %x\n"
.LC201:
	.string	"_c_user_data_density := %d\n"
.LC202:
	.string	"_c_totle_phy_density := %d\n"
.LC203:
	.string	"_c_totle_log_page := %d\n"
.LC204:
	.string	"_c_totle_data_density := %d\n"
.LC205:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC206:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC207:
	.string	"_c_max_pm_sblk := %d\n"
.LC208:
	.string	"_min_slc_super_block := %d\n"
.LC209:
	.string	"_max_xlc_super_block := %d\n"
.LC210:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC211:
	.string	"flash info size: %d %d %d\n"
.LC212:
	.string	"ftl_init %x\n"
.LC213:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC214:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC215:
	.string	"0lpa: %x %x %x\n"
.LC216:
	.string	"lpa: %x %x %x\n"
.LC217:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC218:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC219:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC220:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC221:
	.string	"index= %x, lpa=%x\n"
.LC222:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC223:
	.string	"ftl_read %x %x %x\n"
.LC224:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC225:
	.string	"acblk = %x, tempblk = %x, gc_des = %x\n"
.LC226:
	.string	"acblk = %x, free page = %x, vpn = %x %x\n"
.LC227:
	.string	"tmp_blk = %x, free page = %x, vpn = %x %x\n"
.LC228:
	.string	"vpn_check lpa = %x, ppa = %x, blk = %x\n"
.LC229:
	.string	"l2p"
.LC230:
	.string	"vpn_check %x = c %x  s %x function: %x\n"
.LC231:
	.string	"vpn_check blk = %x fix vpc\n"
.LC232:
	.string	"...%s exit...%d\n"
.LC233:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC234:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC235:
	.string	"ftl_scan_all_data = %x\n"
.LC236:
	.string	"scan lpa = %x ppa= %x\n"
.LC237:
	.string	"lba = %x,addr= %x, ststus = %x, spare= %x %x %x %x data=%x %x\n"
.LC238:
	.string	"lba = %x, id= %x, index = %x hash ppa = %x\n"
.LC239:
	.string	"0pm:"
.LC240:
	.string	"1pm:"
.LC241:
	.string	"zftl debug cmd: %s\n"
.LC242:
	.string	"cmd:"
.LC243:
	.string	"dumpl2p"
.LC244:
	.string	"pm l2p:"
.LC245:
	.string	"pm blk:"
.LC246:
	.string	"dumppm:"
.LC247:
	.string	"p_cmd: %s\n"
.LC248:
	.string	"pm ram = %x, %x\n"
.LC249:
	.string	"ram:"
.LC250:
	.string	"pm:"
.LC251:
	.string	"dumpsys"
.LC252:
	.string	"dumplist"
.LC253:
	.string	"vpncheck"
.LC254:
	.string	"dumpppa:"
.LC255:
	.string	"dumpblk:"
.LC256:
	.string	"setzdebug:"
.LC257:
	.string	"lpa2ppa:"
.LC258:
	.string	"lpa: %x--> ppa: %x\n"
.LC259:
	.string	"help:\n"
.LC260:
	.string	"1. echo dumpl2p > /proc/zftl_debug\n"
.LC261:
	.string	"2. echo dumppm:x > /proc/zftl_debug\n"
.LC262:
	.string	"3. echo dumpsys > /proc/zftl_debug\n"
.LC263:
	.string	"4. echo dumpppa:x > /proc/zftl_debug\n"
.LC264:
	.string	"5. echo vpncheck > /proc/zftl_debug\n"
.LC265:
	.string	"6. echo setzdebug:x > /proc/zftl_debug\n"
.LC266:
	.string	"7. echo dumplist > /proc/zftl_debug\n"
.LC267:
	.string	"8. echo lpa2ppa:x> /proc/zftl_debug\n"
.LC268:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC269:
	.string	"ftl_update_l2p_map"
.LC270:
	.string	"lpa_tbl:"
.LC271:
	.string	"sblk %x vpn: %d %d\n"
.LC272:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC273:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC274:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC275:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x\n"
.LC276:
	.string	"%d prog_step: %x %x buf id= %x ppa = %x hash=%x id = %x plane = %x lpa=%x\n"
.LC277:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC278:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC279:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC280:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC281:
	.string	"gc free %x, %d\n"
.LC282:
	.string	"error gc slc free sblk = %x, vpn = %d %d\n"
.LC283:
	.string	"vpage0:"
.LC284:
	.string	"block %x, vpn = %d %d %d %d\n"
.LC285:
	.string	"block %x, vpn = %d %d\n"
.LC286:
	.string	"vpage1:"
.LC287:
	.string	"ftlwrite %x %x %x\n"
.LC288:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC289:
	.string	"write_idblock fix data %x %x\n"
.LC290:
	.string	"1 write_idblock fix data %x %x %x\n"
.LC291:
	.string	"READ_SECTOR_IO\n"
.LC292:
	.string	"rk_copy_from_user error\n"
.LC293:
	.string	"READ_SECTOR_IO %x %x\n"
.LC294:
	.string	"rk_copy_to_user error\n"
.LC295:
	.string	"WRITE_SECTOR_IO\n"
.LC296:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC297:
	.string	"END_WRITE_SECTOR_IO\n"
.LC298:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC299:
	.string	"GET_FLASH_INFO_IO\n"
.LC300:
	.string	"GET_BAD_BLOCK_IO\n"
.LC301:
	.string	"GET_LOCK_FLAG_IO\n"
.LC302:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC303:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC304:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC305:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC306:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC307:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC308:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC309:
	.string	"RKNAND_LOADER_STATUS\n"
.LC310:
	.string	"RKNAND_LOADER_LOCK\n"
.LC311:
	.string	"LockKey not match %d\n"
.LC312:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC313:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC314:
	.string	"return ret = %lx\n"
.LC315:
	.string	"secureBootEn check error\n"
.LC316:
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.hword	9695
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
	.byte	2
	.hword	1096
	.hword	9695
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
	.byte	2
	.hword	1096
	.hword	9695
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
.LANCHOR6 = . + 39168
.LANCHOR7 = . + 52224
.LANCHOR8 = . + 56576
.LANCHOR9 = . + 60928
	.type	g_nandc_ver, %object
	.size	g_nandc_ver, 1
g_nandc_ver:
	.zero	1
	.zero	3
	.type	_c_user_data_density, %object
	.size	_c_user_data_density, 4
_c_user_data_density:
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
	.type	ftl_sblk_vpn, %object
	.size	ftl_sblk_vpn, 8
ftl_sblk_vpn:
	.zero	8
	.type	gp_ftl_ext_info, %object
	.size	gp_ftl_ext_info, 8
gp_ftl_ext_info:
	.zero	8
	.type	gp_nand_para_info, %object
	.size	gp_nand_para_info, 8
gp_nand_para_info:
	.zero	8
	.type	g_idb_ecc_bits, %object
	.size	g_idb_ecc_bits, 1
g_idb_ecc_bits:
	.zero	1
	.type	g_flash_slc_mode, %object
	.size	g_flash_slc_mode, 1
g_flash_slc_mode:
	.zero	1
	.type	g_nand_max_die, %object
	.size	g_nand_max_die, 1
g_nand_max_die:
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
	.type	g_slc_mode_addr2, %object
	.size	g_slc_mode_addr2, 1
g_slc_mode_addr2:
	.zero	1
	.zero	1
	.type	g_block_align_addr, %object
	.size	g_block_align_addr, 2
g_block_align_addr:
	.zero	2
	.type	g_lsb_page_tbl, %object
	.size	g_lsb_page_tbl, 512
g_lsb_page_tbl:
	.zero	512
	.zero	4
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
	.type	write_buf_head, %object
	.size	write_buf_head, 1
write_buf_head:
	.zero	1
	.zero	1
	.type	read_ahead_lpa, %object
	.size	read_ahead_lpa, 4
read_ahead_lpa:
	.zero	4
	.type	p_read_ahead_ext_buf, %object
	.size	p_read_ahead_ext_buf, 8
p_read_ahead_ext_buf:
	.zero	8
	.type	sblk_read_completed_queue_head, %object
	.size	sblk_read_completed_queue_head, 1
sblk_read_completed_queue_head:
	.zero	1
	.zero	7
	.type	g_gc_info, %object
	.size	g_gc_info, 328
g_gc_info:
	.zero	328
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
	.type	gp_data_slc_data_head, %object
	.size	gp_data_slc_data_head, 8
gp_data_slc_data_head:
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
	.type	_min_slc_super_block, %object
	.size	_min_slc_super_block, 2
_min_slc_super_block:
	.zero	2
	.type	_max_xlc_super_block, %object
	.size	_max_xlc_super_block, 2
_max_xlc_super_block:
	.zero	2
	.zero	4
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
	.type	_c_ftl_planes_per_die, %object
	.size	_c_ftl_planes_per_die, 1
_c_ftl_planes_per_die:
	.zero	1
	.zero	1
	.type	_c_ftl_block_addr_log2, %object
	.size	_c_ftl_block_addr_log2, 2
_c_ftl_block_addr_log2:
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
	.type	ftl_sblk_vpn_update_id, %object
	.size	ftl_sblk_vpn_update_id, 2
ftl_sblk_vpn_update_id:
	.zero	2
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.zero	1
	.type	sblk_queue_head, %object
	.size	sblk_queue_head, 1
sblk_queue_head:
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
	.zero	5
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
	.zero	47
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 16384
ftl_tmp_buffer:
	.zero	16384
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 256
ftl_tmp_spare:
	.zero	256
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.zero	1
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.zero	1
	.zero	2
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.zero	4
	.zero	56
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
	.type	ftl_low_format_cur_blk, %object
	.size	ftl_low_format_cur_blk, 2
ftl_low_format_cur_blk:
	.zero	2
	.type	_c_ftl_pm_page_num, %object
	.size	_c_ftl_pm_page_num, 2
_c_ftl_pm_page_num:
	.zero	2
	.zero	4
	.type	g_pm_spare, %object
	.size	g_pm_spare, 8
g_pm_spare:
	.zero	8
	.type	pm_first_write, %object
	.size	pm_first_write, 1
pm_first_write:
	.zero	1
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
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
	.type	_c_totle_data_density, %object
	.size	_c_totle_data_density, 4
_c_totle_data_density:
	.zero	4
	.type	_max_slc_super_block, %object
	.size	_max_slc_super_block, 2
_max_slc_super_block:
	.zero	2
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.zero	2
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.zero	2
	.zero	6
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
	.zero	4
	.type	check_vpc_tbl, %object
	.size	check_vpc_tbl, 4608
check_vpc_tbl:
	.zero	4608
	.type	discard_sector_count, %object
	.size	discard_sector_count, 4
discard_sector_count:
	.zero	4
	.type	_gc_after_discard_en, %object
	.size	_gc_after_discard_en, 4
_gc_after_discard_en:
	.zero	4
	.type	_last_write_time, %object
	.size	_last_write_time, 4
_last_write_time:
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
	.type	_c_ftl_nand_blks_per_die, %object
	.size	_c_ftl_nand_blks_per_die, 2
_c_ftl_nand_blks_per_die:
	.zero	2
	.type	g_slc_mode_enable, %object
	.size	g_slc_mode_enable, 1
g_slc_mode_enable:
	.zero	1
