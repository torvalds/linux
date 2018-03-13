/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-03-16
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
	.align	2
	.global	zftl_get_density
	.type	zftl_get_density, %function
zftl_get_density:
	cbnz	w0, .L12
	adrp	x0, .LANCHOR0+4
	ldr	w0, [x0,#:lo12:.LANCHOR0+4]
	b	.L13
.L12:
	cmp	w0, 4
	mov	w0, 8192
	csel	w0, w0, wzr, cc
.L13:
	ret
	.size	zftl_get_density, .-zftl_get_density
	.align	2
	.type	_list_remove_node, %function
_list_remove_node:
	adrp	x6, .LANCHOR0
	uxth	w1, w1
	add	x3, x6, :lo12:.LANCHOR0
	mov	w7, 65535
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x5, [x3,8]
	mov	w3, 6
	umull	x1, w1, w3
	add	x4, x5, x1
	ldrh	w3, [x5,x1]
	cmp	w3, w7
	bne	.L16
	ldrh	w7, [x4,2]
	cmp	w7, w3
	bne	.L16
	ldr	x7, [x0]
	cmp	x4, x7
	bne	.L15
.L16:
	ldrh	w7, [x4,2]
	mov	w8, 65535
	cmp	w7, w8
	bne	.L18
	ldr	x8, [x0]
	cmp	x4, x8
	beq	.L18
	adrp	x0, .LC0
	adrp	x1, .LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 182
	bl	printk
.L19:
	b	.L19
.L18:
	ldr	x8, [x0]
	cmp	x4, x8
	mov	w8, 65535
	bne	.L20
	cmp	w3, w8
	bne	.L21
	str	xzr, [x0]
	b	.L22
.L21:
	mov	w6, 6
	umull	x3, w3, w6
	add	x3, x5, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L22
.L20:
	cmp	w3, w8
	bne	.L23
	cmp	w7, w3
	beq	.L22
	mov	w0, 6
	umull	x7, w7, w0
	mov	w0, -1
	strh	w0, [x5,x7]
	b	.L22
.L23:
	mov	w8, 6
	add	x6, x6, :lo12:.LANCHOR0
	umull	x3, w3, w8
	add	x3, x5, x3
	strh	w7, [x3,2]
	ldrh	w0, [x4,2]
	ldr	x3, [x6,8]
	ldrh	w7, [x5,x1]
	umull	x0, w0, w8
	strh	w7, [x3,x0]
.L22:
	mov	w0, -1
	strh	w0, [x5,x1]
	strh	w0, [x4,2]
	ldrh	w0, [x2]
	sub	w0, w0, #1
	strh	w0, [x2]
.L15:
	ldp	x29, x30, [sp], 16
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
	cbz	x20, .L25
	adrp	x3, .LANCHOR0+8
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3,#:lo12:.LANCHOR0+8]
.L26:
	cbnz	w1, .L27
.L29:
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
	b	.L25
.L27:
	ldrh	w3, [x20]
	cmp	w3, w4
	beq	.L29
	umull	x20, w3, w5
	sub	w1, w1, #1
	add	x20, x19, x20
	uxth	w1, w1
	b	.L26
.L25:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	_list_pop_index_node, .-_list_pop_index_node
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
	bne	.L32
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
	b	.L34
.L32:
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
.L34:
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
	bne	.L36
	ldr	w3, [x0,292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,292]
	ldr	w2, [x0,288]
	bic	w1, w2, w1
	str	w1, [x0,288]
	b	.L35
.L36:
	ldr	w3, [x0,368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
.L35:
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
	bls	.L38
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
	cbz	x2, .L45
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
.L43:
	ubfiz	x14, x3, 2, 16
	ldr	w5, [x9,x14]
	ubfx	x6, x5, 11, 8
	ldrh	w5, [x9,x14]
	ubfx	x5, x5, 0, 11
	add	w5, w5, w6, lsl 3
	and	w5, w5, 4095
	cmp	w5, w4
	bcs	.L41
	ldrh	w5, [x2]
	cmp	w5, w15
	bne	.L42
	strh	w3, [x7,2]
	strh	w1, [x2]
	b	.L38
.L42:
	umull	x2, w5, w8
	mov	w3, w5
	add	x2, x13, x2
	b	.L43
.L41:
	ldrh	w4, [x2,2]
	strh	w4, [x7,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L44
	strh	w1, [x2,2]
.L45:
	str	x7, [x0]
	b	.L38
.L44:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,8]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L38:
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
	bls	.L46
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
	cbz	x2, .L61
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
.L57:
	add	w7, w7, 1
	cmp	w1, w3
	uxth	w7, w7
	beq	.L46
	cmp	w7, w18
	bhi	.L46
	uxtw	x8, w3
	ldrh	w5, [x2,4]
	cmp	w5, wzr
	ldrh	w4, [x15,x8,lsl 1]
	mul	w4, w4, w5
	csinv	w4, w4, wzr, ne
	cmp	w4, w13
	bne	.L53
	lsl	x8, x8, 2
	ldr	w4, [x17,x8]
	ubfx	x5, x4, 11, 8
	ldrh	w4, [x17,x8]
	ubfx	x4, x4, 0, 11
	add	w4, w4, w5, lsl 3
	and	w4, w4, 4095
	cmp	w4, w16
	bcc	.L55
	b	.L54
.L53:
	bhi	.L54
.L55:
	ldrh	w4, [x2]
	cmp	w4, w19
	bne	.L56
	strh	w3, [x6,2]
	strh	w1, [x2]
	b	.L46
.L56:
	umull	x2, w4, w9
	mov	w3, w4
	add	x2, x14, x2
	b	.L57
.L54:
	ldrh	w4, [x2,2]
	strh	w4, [x6,2]
	strh	w3, [x12,x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L58
	strh	w1, [x2,2]
.L61:
	str	x6, [x0]
	b	.L46
.L58:
	ldrh	w0, [x2,2]
	add	x3, x10, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3,8]
	umull	x0, w0, w4
	strh	w1, [x3,x0]
	strh	w1, [x2,2]
.L46:
	ldr	x19, [sp]
	add	sp, sp, 16
	ret
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_get_gc_head_node.isra.5, %function
_list_get_gc_head_node.isra.5:
	uxth	w1, w1
	mov	w2, 65535
	cbz	x0, .L68
	adrp	x2, .LANCHOR0+8
	mov	w4, 65535
	mov	w5, 6
	ldr	x3, [x2,#:lo12:.LANCHOR0+8]
.L64:
	cbz	w1, .L65
	ldrh	w2, [x0]
	cmp	w2, w4
	beq	.L68
	umull	x2, w2, w5
	sub	w1, w1, #1
	add	x0, x3, x2
	uxth	w1, w1
	b	.L64
.L65:
	sub	x0, x0, x3
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	madd	x0, x1, x0, x0
	uxth	w2, w0
.L68:
	mov	w0, w2
	ret
	.size	_list_get_gc_head_node.isra.5, .-_list_get_gc_head_node.isra.5
	.align	2
	.type	_list_update_data_list, %function
_list_update_data_list:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w1
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	str	x21, [sp,32]
	ldr	x3, [x4,80]
	ldrh	w5, [x3,16]
	cmp	w5, w19
	beq	.L69
	ldrh	w5, [x3,48]
	cmp	w5, w19
	beq	.L69
	ldrh	w3, [x3,80]
	cmp	w3, w19
	beq	.L69
	ldr	x5, [x4,8]
	mov	w4, 6
	ldr	x3, [x0]
	umull	x4, w19, w4
	add	x6, x5, x4
	cmp	x6, x3
	beq	.L69
	ldrh	w3, [x6,2]
	mov	x20, x0
	mov	w0, 65535
	mov	x21, x2
	cmp	w3, w0
	bne	.L71
	ldrh	w0, [x5,x4]
	cmp	w0, w3
	bne	.L71
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 24
	mov	w2, 249
	bl	printk
.L72:
	b	.L72
.L71:
	add	x1, x1, :lo12:.LANCHOR0
	ubfiz	x0, x19, 1, 16
	mov	x2, -6148914691236517206
	ldr	x4, [x1,72]
	ldrh	w1, [x4,x0]
	ldrh	w0, [x6,4]
	cmp	w0, wzr
	mul	w1, w1, w0
	mov	w0, 6
	csinv	w1, w1, wzr, ne
	umull	x3, w3, w0
	asr	x0, x3, 1
	add	x3, x5, x3
	madd	x0, x2, x0, x0
	ldrh	w3, [x3,4]
	ldrh	w0, [x4,x0,lsl 1]
	cmp	w3, wzr
	mul	w0, w0, w3
	csinv	w0, w0, wzr, ne
	cmp	w1, w0
	bcs	.L69
	mov	x0, x20
	mov	w1, w19
	mov	x2, x21
	bl	_list_remove_node
	mov	x0, x20
	mov	w1, w19
	mov	x2, x21
	bl	_insert_data_list
.L69:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	_list_update_data_list, .-_list_update_data_list
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	ret
	.size	nand_flash_print_info, .-nand_flash_print_info
	.align	2
	.global	nandc_init
	.type	nandc_init, %function
nandc_init:
	stp	x29, x30, [sp, -48]!
	mov	w2, 6
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	str	wzr, [x29,40]
	strb	w2, [x19,#:lo12:.LANCHOR0]
	str	x0, [x1,16]
	mov	w1, 12336
	ldr	w2, [x0,352]
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L79
	mov	w1, 8
	strb	w1, [x19,#:lo12:.LANCHOR0]
.L79:
	ldr	w2, [x0,128]
	mov	w1, 12336
	movk	w1, 0x5639, lsl 16
	cmp	w2, w1
	bne	.L80
	mov	w1, 9
	strb	w1, [x19,#:lo12:.LANCHOR0]
.L80:
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	add	x20, x19, :lo12:.LANCHOR0
	cmp	w1, 9
	bne	.L81
	mov	w1, 1
	strb	w1, [x20,88]
	ldr	w1, [x29,40]
	orr	w1, w1, 256
	str	w1, [x29,40]
	ldr	w1, [x29,40]
	str	w1, [x0]
	mov	w1, 4225
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
	b	.L82
.L81:
	ldr	w1, [x29,40]
	strb	wzr, [x20,88]
	orr	w1, w1, 256
	str	w1, [x29,40]
	ldr	w1, [x29,40]
	str	w1, [x0]
	mov	w1, 4225
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
	str	x0, [x20,96]
.L82:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strh	wzr, [x19,138]
	strb	w0, [x19,136]
	strb	wzr, [x19,140]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	nandc_init, .-nandc_init
	.align	2
	.global	timer_delay_ns
	.type	timer_delay_ns, %function
timer_delay_ns:
	sub	sp, sp, #16
	lsr	w0, w0, 4
	str	w0, [sp,12]
.L84:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L84
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
.L88:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,142]
	cmp	w0, w20
	bls	.L94
	add	x0, x19, 48
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,141]
	cmp	w0, 34
	bne	.L89
	ldrsb	w0, [x24,x20]
	b	.L93
.L89:
	cmp	w0, 35
	bne	.L91
	ldrsb	w0, [x21,x20]
	b	.L93
.L91:
	ldrsb	w0, [x23,192]
.L93:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L88
.L94:
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
	ldr	x4, [x3,144]
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
.L96:
	cmp	w22, w21, uxtb
	bls	.L98
	ldrb	w0, [x23,x21]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x20,x21]
	add	x21, x21, 1
	str	w0, [x19,2048]
	b	.L96
.L98:
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
	ldrb	w1, [x19,141]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 6
	bhi	.L99
	ldr	x1, [x19,144]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L99
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,144]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L99:
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
	bne	.L105
	str	w0, [x2,80]
	b	.L104
.L105:
	str	w0, [x2,304]
.L104:
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
	bne	.L108
	ldr	w0, [x0,80]
	b	.L110
.L108:
	ldr	w0, [x0,304]
.L110:
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
	beq	.L112
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
	bne	.L114
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L115
.L114:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L115
.L112:
	and	w1, w1, -8193
.L115:
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
	stp	x29, x30, [sp, -64]!
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x29, sp, 0
	add	x3, x3, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	w21, w2
	ldrb	w2, [x3,152]
	cmp	w2, w0
	bhi	.L121
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 200
	mov	w2, 597
	bl	printk
.L122:
	b	.L122
.L121:
	add	x0, x3, x0, sxtw
	ldrb	w22, [x0,156]
	ldr	x20, [x3,16]
	mov	w0, w22
	str	x1, [x29,56]
	bl	nandc_cs
	ubfiz	x3, x22, 8, 8
	ldr	x1, [x29,56]
	add	x20, x20, x3
	mov	w0, 120
	and	w2, w1, 255
	str	w0, [x20,2056]
	str	w2, [x20,2052]
	lsr	w2, w1, 8
	str	w2, [x20,2052]
	lsr	w1, w1, 16
	str	w1, [x20,2052]
.L125:
	bl	timer_delay_ns
	ldr	w19, [x20,2048]
	and	w19, w19, 255
	and	w0, w19, w21
	cmp	w0, w21
	beq	.L126
.L124:
	mov	w0, 20
	b	.L125
.L126:
	cmp	w19, 255
	beq	.L124
	mov	w0, w22
	bl	nandc_de_cs
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
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
	cbnz	w3, .L128
	adrp	x4, .LANCHOR0
	add	x3, x4, :lo12:.LANCHOR0
	ldrb	w0, [x3,164]
	cbz	w0, .L129
	ldrb	w0, [x3,165]
	cbz	w0, .L128
.L129:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w5, [x4,166]
	ldrb	w0, [x4,165]
	udiv	w3, w1, w5
	mul	w5, w3, w5
	sub	w3, w1, w5
	add	w1, w5, w3, lsl 1
	cbnz	w0, .L128
	add	x4, x4, 168
	ldrh	w3, [x4,w3,uxtw 1]
	add	w1, w3, w5
.L128:
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
.L139:
	mov	w0, 100
	bl	timer_delay_ns
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L140
	subs	w19, w19, #1
	bne	.L139
	mov	w0, -1
	b	.L138
.L140:
	mov	w0, 0
.L138:
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
	add	x0, x2, 48
	add	x2, x2, 96
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L143:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,142]
	cmp	w4, w2
	bls	.L148
	ldrb	w3, [x3,141]
	cmp	w3, 67
	bne	.L144
	ldrsb	w3, [x0,x2]
	b	.L147
.L144:
	ldrsb	w3, [x1,x2]
.L147:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L143
.L148:
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
	add	x0, x0, 232
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
	ldrsb	w1, [x1,633]
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
	cbz	w2, .L152
	mov	w2, 18
	str	w2, [x19,4]
	mov	x2, 7
	add	x0, x0, :lo12:.LANCHOR2
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
	b	.L153
.L152:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 336
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L153:
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
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x1,164]
	cbz	w2, .L154
	cmp	w2, 1
	ldr	x3, [x1,16]
	beq	.L174
	cmp	w2, 2
	bne	.L159
	ldrb	w2, [x1,680]
	cbz	w2, .L154
	strb	wzr, [x1,680]
.L174:
	adrp	x1, .LANCHOR2+397
	ldrb	w2, [x1,#:lo12:.LANCHOR2+397]
	cbz	w2, .L154
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x3, x1, lsl 8
	str	w2, [x0,8]
	b	.L154
.L159:
	cmp	w2, 3
	bne	.L154
	ldrb	w2, [x1,680]
	cbz	w2, .L154
	strb	wzr, [x1,680]
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
.L154:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x1,164]
	cbz	w2, .L175
	cmp	w2, 1
	ldr	x4, [x1,16]
	bne	.L178
	adrp	x1, .LANCHOR2+398
	ldrb	w2, [x1,#:lo12:.LANCHOR2+398]
	b	.L195
.L178:
	cmp	w2, 2
	bne	.L180
	ldrb	w2, [x1,680]
	cbnz	w2, .L175
	adrp	x2, .LANCHOR2
	mov	w5, 4
	add	x2, x2, :lo12:.LANCHOR2
	add	x2, x2, 368
	ldrb	w3, [x2,12]
	ldrb	w2, [x2,30]
	cmp	w3, 2
	csel	w3, w3, w5, eq
	strb	w3, [x1,680]
.L195:
	cbz	w2, .L175
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x4, x1, lsl 8
	str	w2, [x0,8]
	b	.L175
.L180:
	cmp	w2, 3
	bne	.L175
	ldrb	w3, [x1,680]
	cbnz	w3, .L175
	adrp	x2, .LANCHOR2+380
	mov	w5, 4
	ldrb	w2, [x2,#:lo12:.LANCHOR2+380]
	cmp	w2, 2
	csel	w2, w2, w5, eq
	strb	w2, [x1,680]
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
.L175:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_set_interface_mode
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	adrp	x1, .LANCHOR0
	mov	x3, 0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w8, 239
	stp	x29, x30, [sp, -16]!
	mov	w9, 128
	mov	w11, 1
	add	x29, sp, 0
	ldrb	w2, [x1,681]
	mov	w12, 35
	ldr	x7, [x1,16]
	mov	w13, 5
	and	w10, w2, 1
	and	w2, w2, 4
	uxtb	w4, w2
	add	x2, x1, 684
.L204:
	lsl	x1, x3, 3
	ldrb	w6, [x1,x2]
	cmp	w6, 44
	cset	w5, eq
	cmp	w6, 69
	cset	w14, eq
	cmp	w6, 152
	cset	w1, eq
	orr	w1, w14, w1
	cbnz	w1, .L205
	cbz	w5, .L197
.L205:
	cmp	w0, 1
	bne	.L199
	cbz	w10, .L197
	add	x1, x7, x3, lsl 8
	str	w8, [x1,2056]
	cbz	w5, .L200
	str	w0, [x1,2052]
	str	w13, [x1,2048]
	b	.L203
.L200:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L203
.L199:
	cbz	w4, .L197
	add	x1, x7, x3, lsl 8
	str	w8, [x1,2056]
	cbz	w5, .L202
	str	w11, [x1,2052]
	str	w12, [x1,2048]
	b	.L203
.L202:
	str	w9, [x1,2052]
	str	w5, [x1,2048]
.L203:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L197:
	add	x3, x3, 1
	cmp	x3, 4
	bne	.L204
	bl	nandc_wait_flash_ready
	mov	w0, 0
	ldp	x29, x30, [sp], 16
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
	bhi	.L217
	adrp	x0, .LC1
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC1
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L217:
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
	adrp	x3, .LANCHOR2+377
	ldr	x19, [x4,#:lo12:.LANCHOR0+16]
	str	x2, [x29,40]
	add	x19, x19, x0
	ldrb	w3, [x3,#:lo12:.LANCHOR2+377]
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
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0+16
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x2,#:lo12:.LANCHOR0+16]
	adrp	x2, .LANCHOR2
	add	x3, x2, :lo12:.LANCHOR2
	mov	x20, x2
	sxtw	x2, w0
	add	x2, x2, 8
	ldrb	w3, [x3,397]
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L227
	mov	w3, 162
.L227:
	ubfiz	x0, x0, 8, 8
	str	w3, [x2,8]
	add	x19, x19, x0
	mov	w0, 128
	str	w0, [x19,2056]
	and	w0, w1, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w1, 8
	str	w0, [x19,2052]
	lsr	w1, w1, 16
	str	w1, [x19,2052]
	mov	w0, 16
	str	w0, [x19,2056]
	bl	nandc_wait_flash_ready
	mov	w0, 112
	str	w0, [x19,2056]
	mov	w0, 80
	bl	timer_delay_ns
	add	x2, x20, :lo12:.LANCHOR2
	ldr	w3, [x19,2048]
	ldrb	w1, [x2,398]
	cbz	w1, .L223
	str	w1, [x19,2056]
.L223:
	mov	w0, 5
	and	w0, w3, w0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	strb	w0, [x1,140]
	cmp	w2, 9
	ldr	x2, [x1,16]
	bne	.L230
	str	w3, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L231
	cmp	w0, 60
	mov	w1, 3
	beq	.L231
	cmp	w0, 40
	mov	w1, 2
	csel	w1, w1, w3, eq
.L231:
	lsl	w1, w1, 25
	orr	w1, w1, 1
	str	w1, [x2,32]
	b	.L229
.L230:
	str	w3, [x2,8]
	cmp	w0, 16
	mov	w1, 4096
	bne	.L233
.L236:
	and	w1, w1, -17
	b	.L234
.L233:
	cmp	w0, 24
	bne	.L235
	orr	w1, w1, 16
	b	.L234
.L235:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L236
.L234:
	orr	w1, w1, 1
	str	w1, [x2,12]
.L229:
	ret
	.size	nandc_bch_sel, .-nandc_bch_sel
	.align	2
	.global	zftl_flash_resume
	.type	zftl_flash_resume, %function
zftl_flash_resume:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x0, x20, :lo12:.LANCHOR0
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
	bne	.L244
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
	b	.L245
.L244:
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
.L245:
	add	x21, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x21, x21, 684
.L247:
	lsl	x0, x19, 3
	ldrb	w0, [x0,x21]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L246
	mov	w0, w19
	bl	flash_reset
.L246:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L247
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,716]
	cbz	w0, .L248
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,40]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L248:
	adrp	x0, .LANCHOR2+388
	ldrb	w0, [x0,#:lo12:.LANCHOR2+388]
	bl	nandc_bch_sel
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
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
	bne	.L255
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L256
.L255:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L256:
	ldr	w1, [x0]
	tbnz	x1, 9, .L257
	bl	wait_for_nand_flash_ready
	b	.L254
.L257:
	mov	w1, 1
	bl	nandc_irq_disable
.L254:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	stp	x29, x30, [sp, -96]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x25, [sp,64]
	uxtb	w25, w1
	add	x1, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	mov	w24, w2
	mov	w23, w3
	ldrb	w4, [x1,152]
	cmp	w4, w0
	bhi	.L260
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 648
	mov	w2, 508
	bl	printk
.L261:
	b	.L261
.L260:
	add	x0, x1, x0, sxtw
	ldrb	w22, [x0,156]
	adrp	x0, .LANCHOR2+400
	ldr	x19, [x1,16]
	mov	x21, x22
	add	x22, x22, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	add	x22, x19, x22, lsl 8
	tbz	x0, 4, .L262
	adrp	x0, .LC2
	mov	w1, w21
	add	x0, x0, :lo12:.LC2
	bl	printk
.L262:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L263
	bl	zftl_flash_enter_slc_mode
	b	.L264
.L263:
	bl	zftl_flash_exit_slc_mode
.L264:
	ubfiz	x4, x21, 8, 8
	mov	w0, 96
	add	x19, x19, x4
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w24, 255
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w0, w24, 16
	str	w0, [x19,2052]
	ldrb	w0, [x20,717]
	mov	w20, 0
	cbnz	w0, .L265
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w20, 5
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	and	w20, w0, w20
.L265:
	mov	w0, 96
	str	w0, [x19,2056]
	and	w0, w23, 255
	str	w0, [x19,2052]
	lsr	w0, w23, 8
	str	w0, [x19,2052]
	lsr	w23, w23, 16
	str	w23, [x19,2052]
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
	cbz	w19, .L266
	adrp	x0, .LC3
	mov	w1, w24
	add	x0, x0, :lo12:.LC3
	bl	printk
.L266:
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
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	uxtb	w24, w1
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	and	w23, w2, 2097151
	mov	w22, w2
	ldrb	w3, [x1,152]
	cmp	w3, w0
	bhi	.L275
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 680
	mov	w2, 553
	bl	printk
.L276:
	b	.L276
.L275:
	add	x0, x1, x0, sxtw
	ldrb	w21, [x0,156]
	adrp	x0, .LANCHOR2+400
	ldr	x25, [x1,16]
	mov	x19, x21
	add	x21, x21, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	add	x21, x25, x21, lsl 8
	tbz	x0, 4, .L277
	adrp	x0, .LC4
	mov	w1, w19
	add	x0, x0, :lo12:.LC4
	bl	printk
.L277:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	bl	nandc_cs
	mov	w0, w19
	cbnz	w24, .L278
	bl	zftl_flash_enter_slc_mode
	b	.L279
.L278:
	bl	zftl_flash_exit_slc_mode
.L279:
	ubfiz	x3, x19, 8, 8
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
	mov	w0, w19
	bl	nandc_de_cs
	mov	w3, 5
	ands	w19, w21, w3
	beq	.L280
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	mov	w2, w19
	ldrh	w1, [x20,166]
	udiv	w1, w22, w1
	bl	printk
.L280:
	mov	w0, w19
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
	ldrb	w1, [x0,381]
	ldrh	w20, [x0,382]
	adrp	x0, .LANCHOR0
	add	x23, x0, :lo12:.LANCHOR0
	mov	x21, x0
	mul	w20, w1, w20
	uxth	w20, w20
.L289:
	ldrb	w0, [x23,152]
	cmp	w0, w19
	bls	.L293
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,156]
	mov	w22, 0
.L290:
	cmp	w20, w22, uxth
	bls	.L294
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,166]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L290
.L294:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L289
.L293:
	adrp	x0, .LC6
	mov	w1, 0
	add	x0, x0, :lo12:.LC6
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
	bne	.L297
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
	ldrb	w0, [x21,88]
	orr	w19, w19, 536870912
	orr	w19, w19, 1024
	and	w19, w19, -17
	bfi	w19, w20, 22, 6
	cbz	w0, .L299
	ldrb	w1, [x21,136]
	orr	w0, w19, 512
	cmp	w1, wzr
	csel	w19, w0, w19, ne
	b	.L299
.L297:
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
	cbz	w24, .L311
	ldrb	w0, [x21,140]
	ubfx	x23, x23, 1, 7
	mov	w1, 64
	mov	w4, 128
	cmp	w0, 25
	add	x23, x3, x23, lsl 2
	csel	w4, w1, w4, cc
	mov	w1, 0
	b	.L304
.L299:
	add	x22, x22, :lo12:.LANCHOR0
	ubfx	x21, x19, 22, 6
	mov	x0, x2
	lsl	w1, w21, 10
	str	x2, [x22,104]
	mov	w2, w24
	str	x3, [x22,112]
	bl	rknand_dma_map_single
	str	w0, [x22,120]
	ldr	x0, [x22,112]
	lsl	w1, w21, 2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,124]
	ldr	x3, [x22,16]
	mov	w1, 1
	str	w1, [x22,128]
	cmp	w24, wzr
	ldr	w1, [x22,120]
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
	ldrh	w0, [x22,138]
	orr	w1, w1, 1
	bfi	w1, w0, 16, 11
	str	w1, [x3,48]
	str	w19, [x3,16]
	orr	w19, w19, 4
	str	w19, [x3,16]
	b	.L296
.L304:
	cmp	x3, x23
	add	w0, w1, w4
	beq	.L306
	ldrh	w5, [x3,2]
	and	x1, x1, 4294967292
	ldr	x6, [x21,96]
	ldrh	w7, [x3],4
	orr	w5, w7, w5, lsl 16
	str	w5, [x6,x1]
	mov	w1, w0
	b	.L304
.L311:
	ldr	x1, [x21,96]
	str	w0, [x1]
.L306:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	lsl	w1, w20, 10
	ldr	x0, [x22,96]
	str	x2, [x22,104]
	str	x0, [x22,112]
	mov	x0, x2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,120]
	ldr	x0, [x22,112]
	lsl	w1, w20, 7
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,124]
	ldr	x3, [x22,16]
	mov	w0, 1
	str	w0, [x22,128]
	cmp	w24, wzr
	ldr	w0, [x22,120]
	mov	w1, 8660
	str	w0, [x3,20]
	ldr	w0, [x22,124]
	str	w0, [x3,24]
	cset	w0, eq
	bfi	w1, w0, 1, 1
	orr	w1, w1, 1
	str	w1, [x3,16]
	str	w25, [x3,12]
	str	w19, [x3,8]
	orr	w19, w19, 4
	str	w19, [x3,8]
.L296:
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
	add	x1, x1, 704
	ldrh	w0, [x1,w0,uxtw 1]
	add	x1, x2, :lo12:.LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	orr	w3, w0, -1073741824
	ldrb	w4, [x1,136]
	ldr	x1, [x1,16]
	cmp	w4, wzr
	csel	w0, w3, w0, ne
	cmp	w2, 9
	bne	.L314
	str	w0, [x1,520]
	b	.L312
.L314:
	str	w0, [x1,336]
.L312:
	ret
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	stp	x29, x30, [sp, -80]!
	ubfx	x2, x1, 21, 3
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	add	x23, x22, :lo12:.LANCHOR0
	uxtb	w24, w0
	stp	x19, x20, [sp,16]
	ubfx	x21, x1, 24, 2
	and	w19, w1, 2097151
	ldrb	w0, [x23,152]
	cmp	w0, w2
	bhi	.L319
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 960
	mov	w2, 736
	bl	printk
.L320:
	b	.L320
.L319:
	add	x2, x23, x2, sxtw
	ldrb	w20, [x2,156]
	ldr	x25, [x23,16]
	mov	w0, w20
	bl	nandc_cs
	cbnz	w21, .L321
	ldrb	w0, [x23,164]
	cbz	w0, .L322
	ldrb	w0, [x23,165]
	cbz	w0, .L323
.L322:
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w1, [x2,166]
	udiv	w0, w19, w1
	mul	w1, w0, w1
	ldrb	w0, [x2,165]
	sub	w19, w19, w1
	cbz	w0, .L324
	add	w19, w1, w19, lsl 1
	b	.L323
.L324:
	add	x2, x2, 168
	ldrh	w19, [x2,w19,uxtw 1]
	add	w19, w19, w1
	b	.L325
.L323:
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	b	.L325
.L321:
	ldr	x0, [x23,720]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L326
	sxtw	x1, w20
	add	x1, x1, 8
	add	x1, x25, x1, lsl 8
	str	w21, [x1,8]
	b	.L325
.L326:
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
.L325:
	ubfiz	x20, x20, 8, 8
	and	w0, w19, 255
	add	x2, x25, x20
	str	wzr, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w0, [x2,2052]
	lsr	w0, w19, 8
	str	w0, [x2,2052]
	lsr	w0, w19, 16
	str	w0, [x2,2052]
	str	w24, [x2,2056]
	cbz	w21, .L327
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,720]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L327
	sub	w21, w21, #1
	add	w0, w19, w19, lsl 1
	add	w0, w21, w0
	b	.L338
.L327:
	mov	w0, w19
.L338:
	bl	nandc_set_seed
	mov	w0, 0
	bl	nandc_de_cs
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_page_read, .-flash_start_page_read
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
	ldrb	w0, [x20,716]
	cbz	w0, .L340
	ldrb	w0, [x20,681]
	tbz	x0, 0, .L340
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,716]
.L340:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,136]
	cbz	w0, .L341
	mov	w0, 0
	strb	wzr, [x19,136]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,136]
.L341:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+136
	strb	w0, [x1,#:lo12:.LANCHOR0+136]
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
	add	x19, x19, 728
	mov	w21, 0
	mov	x22, x0
.L354:
	add	x20, x22, :lo12:.LANCHOR0
	uxtb	w0, w21
	add	w1, w0, 1
	strb	w0, [x19,1]
	strb	w1, [x19]
	add	w21, w21, 1
	ldrb	w0, [x20,2776]
	add	x19, x19, 64
	strb	wzr, [x19,-62]
	str	xzr, [x19,-48]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x19,-56]
	ldrb	w0, [x20,2776]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,-40]
	cmp	w21, 32
	bne	.L354
	strb	w21, [x20,2778]
	mov	w0, -1
	strb	wzr, [x20,2777]
	strb	w0, [x20,2712]
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
	add	x1, x1, 728
.L357:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L357
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0,2777]
	strb	w1, [x0,2712]
	strb	w2, [x0,2778]
	ret
	.size	buf_reinit, .-buf_reinit
	.align	2
	.global	buf_add_tail
	.type	buf_add_tail, %function
buf_add_tail:
	stp	x29, x30, [sp, -16]!
	mov	w2, -1
	add	x29, sp, 0
	strb	w2, [x1]
	ldrb	w3, [x0]
	cmp	w3, 255
	bne	.L367
	ldrb	w1, [x1,1]
	cmp	w1, 255
	bne	.L361
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 984
	mov	w2, 74
	bl	printk
.L362:
	b	.L362
.L361:
	strb	w1, [x0]
	b	.L359
.L367:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 728
.L368:
	mov	w2, w3
	sbfiz	x3, x3, 6, 32
	ldrb	w3, [x0,x3]
	cmp	w3, 255
	bne	.L368
	ldrb	w1, [x1,1]
	cmp	w1, 255
	bne	.L364
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 984
	mov	w2, 81
	bl	printk
.L365:
	b	.L365
.L364:
	sbfiz	x2, x2, 6, 32
	strb	w1, [x0,x2]
.L359:
	ldp	x29, x30, [sp], 16
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
	cbz	w1, .L370
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2777
	bl	buf_add_tail
	b	.L371
.L370:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2777]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2777]
.L371:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2778]
	add	w0, w0, 1
	strb	w0, [x19,2778]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_free, .-buf_free
	.align	2
	.global	buf_alloc
	.type	buf_alloc, %function
buf_alloc:
	uxtb	w5, w0
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	mov	x1, x0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w3, [x2,2778]
	cbnz	w3, .L373
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1000
	mov	w2, 121
	bl	printk
.L374:
	b	.L374
.L373:
	add	x4, x2, 728
	ldrb	w2, [x2,2777]
	add	x4, x4, x2, lsl 6
	cbnz	w5, .L378
	cmp	w3, 1
	mov	x0, 0
	beq	.L375
.L378:
	add	x0, x1, :lo12:.LANCHOR0
	lsl	x1, x2, 6
	add	x5, x0, 728
	sub	w3, w3, #1
	add	x2, x5, x1
	strb	w3, [x0,2778]
	ldrb	w6, [x5,x1]
	strb	w6, [x0,2777]
	mov	w0, 1
	strb	w0, [x2,2]
	mov	w0, -1
	strb	w0, [x5,x1]
	mov	w0, -1
	strh	wzr, [x2,50]
	str	w0, [x2,36]
	mov	x0, x4
	str	xzr, [x2,16]
.L375:
	ldp	x29, x30, [sp], 16
	ret
	.size	buf_alloc, .-buf_alloc
	.align	2
	.global	buf_remove_buf
	.type	buf_remove_buf, %function
buf_remove_buf:
	ldrb	w4, [x1,1]
	ldrb	w2, [x0]
	cmp	w4, w2
	bne	.L385
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L389
.L385:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 728
.L387:
	cmp	w2, 255
	beq	.L390
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L387
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L389:
	mov	w0, 1
	b	.L384
.L390:
	mov	w0, 0
.L384:
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
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,2778]
	cbnz	w0, .L392
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1016
	mov	w2, 170
	bl	printk
.L393:
	b	.L393
.L392:
	add	x0, x19, 2777
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L391
	ldrb	w0, [x19,2778]
	sub	w0, w0, #1
	strb	w0, [x19,2778]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L391:
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
	add	x2, x2, 368
	ldrb	w3, [x2,13]
	ldrh	w2, [x2,14]
	mul	w2, w3, w2
	ubfx	x3, x1, 5, 27
	uxth	w2, w2
	add	w2, w2, 31
	asr	w2, w2, 5
	lsl	w2, w2, 2
	mul	w2, w0, w2
	adrp	x0, .LANCHOR0+144
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+144]
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
	ldrb	w0, [x3,381]
	mov	w2, w21
	ldrh	w20, [x3,382]
	mul	w20, w0, w20
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	uxth	w20, w20
	add	w20, w20, 31
	bl	printk
	asr	w20, w20, 5
	adrp	x0, .LANCHOR0+144
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+144]
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
	.global	ftl_print_info_to_buf
	.type	ftl_print_info_to_buf, %function
ftl_print_info_to_buf:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LC8
	adrp	x2, .LC9
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC8
	add	x2, x2, :lo12:.LC9
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	bl	sprintf
	adrp	x21, .LANCHOR0
	adrp	x7, .LANCHOR2
	adrp	x1, .LC10
	add	x7, x7, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x7, x7, 368
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC10
	mov	x0, x19
	ldrb	w5, [x7,4]
	ldrb	w6, [x7,5]
	ldrb	w4, [x7,3]
	ldrb	w3, [x7,2]
	ldrb	w2, [x7,1]
	ldrb	w7, [x7,6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,4]
	adrp	x1, .LC11
	mov	x0, x19
	add	x1, x1, :lo12:.LC11
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,2780]
	adrp	x1, .LC12
	mov	x0, x19
	add	x1, x1, :lo12:.LC12
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	adrp	x1, .LC13
	mov	x0, x19
	add	x1, x1, :lo12:.LC13
	add	x19, x19, 10
	bl	strcpy
	ldr	w2, [x21,2784]
	adrp	x1, .LC14
	mov	x0, x19
	add	x1, x1, :lo12:.LC14
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,4]
	adrp	x1, .LC15
	mov	x0, x19
	add	x1, x1, :lo12:.LC15
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC16
	mov	x0, x19
	add	x1, x1, :lo12:.LC16
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC17
	mov	x0, x19
	add	x1, x1, :lo12:.LC17
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2788]
	adrp	x1, .LC18
	mov	x0, x19
	add	x1, x1, :lo12:.LC18
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2790]
	adrp	x1, .LC19
	mov	x0, x19
	add	x1, x1, :lo12:.LC19
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2792]
	adrp	x1, .LC20
	mov	x0, x19
	add	x1, x1, :lo12:.LC20
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2794]
	adrp	x1, .LC21
	mov	x0, x19
	add	x1, x1, :lo12:.LC21
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2796]
	adrp	x1, .LC22
	mov	x0, x19
	add	x1, x1, :lo12:.LC22
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2798]
	adrp	x1, .LC23
	mov	x0, x19
	add	x1, x1, :lo12:.LC23
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x3, [x21,2800]
	adrp	x1, .LC24
	mov	x0, x19
	add	x1, x1, :lo12:.LC24
	ldrh	w2, [x3,148]
	ldrh	w3, [x3,146]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2800]
	adrp	x1, .LC25
	add	x1, x1, :lo12:.LC25
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2800]
	adrp	x1, .LC26
	add	x1, x1, :lo12:.LC26
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC27
	mov	x0, x19
	add	x1, x1, :lo12:.LC27
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC28
	mov	x0, x19
	add	x1, x1, :lo12:.LC28
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,80]
	mov	w2, 10
	adrp	x1, .LC29
	add	x1, x1, :lo12:.LC29
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrb	w2, [x21,164]
	adrp	x1, .LC30
	mov	x0, x19
	add	x1, x1, :lo12:.LC30
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC31
	mov	x0, x19
	add	x1, x1, :lo12:.LC31
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,80]
	adrp	x1, .LC32
	mov	x0, x19
	add	x1, x1, :lo12:.LC32
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC33
	mov	x0, x19
	add	x1, x1, :lo12:.LC33
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC34
	mov	x0, x19
	add	x1, x1, :lo12:.LC34
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC35
	mov	x0, x19
	add	x1, x1, :lo12:.LC35
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC36
	mov	x0, x19
	add	x1, x1, :lo12:.LC36
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC37
	mov	x0, x19
	add	x1, x1, :lo12:.LC37
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC38
	mov	x0, x19
	add	x1, x1, :lo12:.LC38
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,80]
	adrp	x1, .LC39
	mov	x0, x19
	add	x1, x1, :lo12:.LC39
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2800]
	adrp	x1, .LC40
	mov	x0, x19
	add	x1, x1, :lo12:.LC40
	ldrh	w2, [x6,74]
	ldr	w3, [x6,84]
	ldrh	w4, [x6,88]
	ldrh	w5, [x6,92]
	ldrh	w6, [x6,96]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2800]
	adrp	x1, .LC41
	mov	x0, x19
	add	x1, x1, :lo12:.LC41
	ldr	w3, [x6,80]
	ldrh	w4, [x6,90]
	ldrh	w5, [x6,94]
	ldrh	w2, [x6,72]
	ldrh	w6, [x6,98]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2808]
	adrp	x1, .LC42
	mov	x0, x19
	add	x1, x1, :lo12:.LC42
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x21,2810]
	adrp	x1, .LC43
	ldrh	w3, [x21,2812]
	mov	x0, x19
	ldrh	w4, [x21,2814]
	add	x1, x1, :lo12:.LC43
	ldrh	w5, [x21,2816]
	bl	sprintf
	add	x0, x19, x0, sxtw
	ldr	x21, [sp,32]
	sub	w0, w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_print_info_to_buf, .-ftl_print_info_to_buf
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
	add	x0, x19, 2818
	bl	buf_add_tail
	ldrb	w0, [x19,2819]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,2819]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_gc_write_buf, .-ftl_gc_write_buf
	.align	2
	.global	ftl_read_buf
	.type	ftl_read_buf, %function
ftl_read_buf:
	stp	x29, x30, [sp, -32]!
	mov	w2, 0
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x3, x19, :lo12:.LANCHOR0
	add	x3, x3, 728
	ldr	w4, [x3,2056]
	cmp	w0, w4
	bcc	.L409
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1032
	mov	w2, 835
	bl	printk
.L401:
	b	.L401
.L414:
	ldrb	w4, [x3,2]
	tbnz	x4, 3, .L412
.L402:
	add	w2, w2, 1
	add	x3, x3, 64
	cmp	w2, 32
	beq	.L413
.L409:
	ldr	w4, [x3,36]
	cmp	w4, w0
	bne	.L402
	b	.L414
.L412:
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x2, x2, 6, 32
	add	x19, x19, 728
	orr	w0, w4, 4
	add	x19, x19, x2
	strb	w0, [x19,2]
	tbnz	x4, 0, .L404
	mov	x0, x19
	bl	buf_remove_free
	b	.L404
.L413:
	add	x2, x19, :lo12:.LANCHOR0
	mov	x19, 0
	ldr	w3, [x2,2820]
	cmn	w3, #1
	bne	.L404
	str	w0, [x2,2820]
	str	x1, [x2,2824]
.L404:
	mov	x0, x19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
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
	add	x0, x0, 2832
	bl	buf_remove_buf
	ldrb	w0, [x19,2]
	tbz	x0, 1, .L416
	and	w0, w0, -5
	strb	w0, [x19,2]
	b	.L415
.L416:
	mov	x0, x19
	bl	buf_free
.L415:
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
	add	x6, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	uxth	w21, w0
	uxth	w24, w1
	ldrh	w0, [x6,56]
	uxth	w22, w2
	cmp	w0, w21
	bhi	.L420
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1048
	mov	w2, 252
	bl	printk
.L421:
	b	.L421
.L420:
	uxtw	x19, w21
	ldr	x0, [x6,72]
	lsl	x25, x19, 1
	adrp	x23, .LANCHOR2
	ldrh	w26, [x0,x25]
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L422
	ldr	x0, [x6,64]
	mov	w1, w21
	ldrh	w5, [x6,2896]
	mov	w2, w24
	add	x0, x0, x19, lsl 2
	ldrh	w6, [x6,2814]
	mov	w4, w26
	ldrb	w3, [x0,2]
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	ubfx	x3, x3, 5, 3
	bl	printk
.L422:
	add	x5, x20, :lo12:.LANCHOR0
	ldr	x6, [x5,64]
	add	x6, x6, x19, lsl 2
	ldrb	w3, [x6,2]
	ands	w0, w3, 224
	bne	.L423
	cbz	w26, .L424
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1048
	mov	w2, 257
	bl	printk
.L425:
	b	.L425
.L423:
	ldrh	w1, [x5,2840]
	add	x3, x5, 2840
	mov	w0, 0
	cmp	w1, w21
	beq	.L424
	ldr	x4, [x5,80]
	ldrh	w1, [x4,48]
	cmp	w1, w21
	beq	.L424
	ldrh	w1, [x4,16]
	cmp	w1, w21
	beq	.L424
	ldrh	w1, [x4,80]
	cmp	w1, w21
	beq	.L424
	ldrh	w5, [x3,56]
	mov	x0, 0
.L426:
	cmp	w5, w0
	bls	.L449
	add	x0, x0, 1
	add	x1, x3, x0, lsl 1
	ldrh	w1, [x1,56]
	cmp	w1, w21
	bne	.L426
	mov	w0, 0
	b	.L424
.L449:
	add	x19, x4, 392
	cbnz	w24, .L429
	sxtw	x3, w22
	add	x3, x4, x3, lsl 7
	add	x19, x3, 136
.L429:
	add	x23, x23, :lo12:.LANCHOR2
	ldr	w0, [x23,400]
	tbz	x0, 8, .L430
	ldrb	w3, [x6,2]
	add	x6, x20, :lo12:.LANCHOR0
	adrp	x0, .LC44
	mov	w1, w21
	add	x0, x0, :lo12:.LC44
	mov	w2, w24
	ldr	x4, [x6,72]
	ubfx	x3, x3, 5, 3
	ldrh	w6, [x6,2814]
	ldrh	w4, [x4,x25]
	bl	printk
.L430:
	mov	x3, x19
	add	x0, x19, 128
	mov	w1, 65535
.L433:
	ldrh	w2, [x3]
	cmp	w2, w1
	bne	.L431
	strh	w21, [x3]
	add	x20, x20, :lo12:.LANCHOR0
	cbz	w24, .L432
	ldr	x1, [x20,80]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L448
.L432:
	ldr	x0, [x20,80]
	add	x22, x0, x22, uxth 1
	ldrh	w0, [x22,120]
	add	w0, w0, 1
	strh	w0, [x22,120]
.L448:
	mov	w0, 1
	b	.L424
.L431:
	add	x3, x3, 2
	cmp	x3, x0
	bne	.L433
	b	.L448
.L424:
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
	adrp	x1, .LANCHOR0+3168
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3168]
	ldr	w0, [x1,x0]
	ret
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.type	gc_write_completed, %function
gc_write_completed:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	add	x25, x19, 728
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x22, x19
	mov	x24, x25
.L452:
	ldrb	w20, [x19,3176]
	cmp	w20, 255
	beq	.L474
	lsl	x1, x20, 6
	add	x0, x25, x1
	ldrb	w1, [x25,x1]
	strb	w1, [x19,3176]
	ldr	w1, [x0,52]
	ldrh	w23, [x0,48]
	cbz	w1, .L453
	mov	w2, 1
	strh	w2, [x19,3158]
	ldr	w2, [x0,40]
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	str	w2, [x19,3160]
	bl	printk
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1064
	mov	w2, 519
	bl	printk
.L454:
	b	.L454
.L453:
	ldrb	w0, [x19,3177]
	mov	w21, 1
	cmp	w0, 3
	bne	.L455
	ldr	x0, [x19,80]
	ldrb	w21, [x0,89]
	add	w21, w21, w21, lsl 1
	and	w21, w21, 1023
.L455:
	adrp	x0, .LANCHOR2+400
	uxth	x26, w23
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L456
	add	x2, x24, x20, lsl 6
	ldr	x4, [x22,3184]
	adrp	x0, .LC46
	mov	w1, w23
	add	x0, x0, :lo12:.LC46
	mov	w3, w23
	ldrb	w2, [x2,1]
	ldrb	w4, [x4,x26]
	bl	printk
.L456:
	ldr	x2, [x22,3184]
	add	x20, x24, x20, lsl 6
	ldrb	w1, [x2,x26]
	ldrb	w0, [x20,1]
	cmp	w1, w0
	beq	.L457
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1064
	mov	w2, 526
	bl	printk
.L458:
	b	.L458
.L457:
	ldrb	w0, [x20,61]
	cmp	w0, 3
	beq	.L459
	ldrb	w0, [x22,3177]
	cmp	w0, 3
	bne	.L459
	ldrb	w0, [x22,3192]
	cbz	w0, .L460
.L459:
	strh	w23, [x22,3156]
	mov	w20, 0
	mov	w26, -1
.L461:
	cmp	w21, w20, uxth
	bls	.L452
	add	w1, w20, w23
	ldr	x2, [x22,3184]
	add	w20, w20, 1
	ldrb	w0, [x2,x1]
	strb	w26, [x2,x1]
	sbfiz	x0, x0, 6, 32
	add	x0, x24, x0
	strb	wzr, [x0,61]
	bl	buf_free
	ldrb	w0, [x22,2847]
	sub	w0, w0, #1
	strb	w0, [x22,2847]
	b	.L461
.L460:
	cmp	w21, w0, uxth
	bls	.L452
	add	w1, w0, w23
	add	w0, w0, 1
	ldrb	w1, [x2,x1]
	add	x1, x24, x1, lsl 6
	strb	wzr, [x1,61]
	b	.L460
.L474:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	adrp	x3, .LANCHOR0
	add	x0, x3, :lo12:.LANCHOR0
	ldr	x1, [x0,80]
	ldrb	w0, [x0,3193]
	ldrh	w4, [x1,124]
	cbz	w4, .L476
	add	x1, x1, 392
	mov	w2, 1
	b	.L477
.L476:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L478
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L477:
	add	x4, x1, 128
	mov	w5, 65535
.L481:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L479
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L480
	ldr	x2, [x3,80]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L478
.L480:
	ldrb	w1, [x3,3193]
	ldr	x2, [x3,80]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L478
.L479:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L481
.L478:
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
	ldrb	w3, [x2,2847]
	cbz	w3, .L486
	ldrb	w3, [x2,2778]
	cmp	w3, 1
	bhi	.L486
	ldrh	w4, [x2,3194]
	mov	x20, x2
	ldrb	w0, [x2,3196]
	add	x2, x2, 728
	ldrh	w1, [x2,2428]
	mul	w0, w4, w0
	add	w3, w1, 24
	ldr	x4, [x2,2456]
	cmp	w0, w3
	csel	w3, w0, w3, ls
.L487:
	cmp	w1, w3
	bcs	.L500
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L488
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L488
	str	x1, [x29,56]
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L489
	ldr	x2, [x20,3184]
	adrp	x0, .LC47
	add	x0, x0, :lo12:.LC47
	ldrb	w2, [x2,x21]
	bl	printk
.L489:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3184]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2847]
	sub	w0, w0, #1
	strb	w0, [x19,2847]
	mov	w0, 1
	b	.L486
.L488:
	add	w1, w1, 1
	b	.L487
.L500:
	mov	w0, 0
.L486:
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
	adrp	x0, .LC48
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC48
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,2840]
	ldrh	w2, [x6,2842]
	ldrh	w3, [x6,3154]
	ldrb	w4, [x6,2778]
	ldrb	w5, [x6,2847]
	ldrh	w6, [x6,2808]
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
	bne	.L503
	ldr	x0, [x2,3200]
	b	.L506
.L503:
	cmp	w1, 2
	bne	.L505
	ldr	x0, [x2,3208]
	b	.L506
.L505:
	ldr	x0, [x2,3216]
.L506:
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
	cbnz	w19, .L552
	cbnz	w21, .L510
	adrp	x26, .LC49
	strh	w21, [x0,3224]
	mov	w19, w21
	mov	w25, w21
	uxth	w23, w22
	mov	w27, 65535
	adrp	x28, .LANCHOR2
	add	x26, x26, :lo12:.LC49
.L511:
	cmp	w25, w23
	bcs	.L516
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, 3
	str	x3, [x29,96]
	ldrh	w2, [x3,3224]
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
	strh	w2, [x3,3224]
	beq	.L512
	add	x0, x28, :lo12:.LANCHOR2
	uxtw	x5, w1
	ldr	w0, [x0,400]
	tbz	x0, 8, .L513
	ldr	x3, [x3,72]
	mov	x0, x26
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrh	w3, [x3,x5,lsl 1]
	bl	printk
	ldr	x4, [x29,96]
	ldr	x5, [x29,104]
.L513:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,72]
	ldrh	w2, [x0,x5,lsl 1]
	ldrh	w0, [x1,2814]
	cmp	w2, w0
	bcs	.L514
	mov	w1, 0
	mov	w0, w4
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L515
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L515
	b	.L516
.L514:
	strh	wzr, [x1,3224]
	b	.L516
.L512:
	strh	wzr, [x3,3224]
	b	.L516
.L515:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L511
.L516:
	tbz	x24, 1, .L518
	mov	w26, 0
	mov	w27, 65535
.L519:
	cmp	w26, w23
	beq	.L523
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w28, [x25,3226]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	uxth	w1, w0
	strh	w28, [x25,3226]
	cmp	w1, w27
	beq	.L520
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x25,72]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x25,2816]
	cmp	w2, w1
	bcs	.L521
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L522
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bls	.L522
	b	.L525
.L521:
	strh	wzr, [x25,3226]
	b	.L523
.L520:
	strh	wzr, [x25,3224]
	b	.L523
.L522:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L519
.L523:
	cmp	w19, w23
	bcs	.L525
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w0, [x1,3228]
	ldrb	w4, [x1,3196]
	lsr	w3, w0, 3
	ldrh	w2, [x1,2816]
	mul	w0, w0, w4
	sub	w0, w0, w3
	cmp	w2, w0
	bge	.L518
	add	w2, w3, w2
	strh	w2, [x1,2816]
	b	.L518
.L525:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,2816]
	ldrh	w0, [x1,3228]
	cmp	w2, w0
	bls	.L518
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,2816]
.L518:
	tbz	x24, 0, .L526
	cmp	w19, w23
	bcs	.L526
	mov	w25, 64
	mov	w27, 65535
.L530:
	add	x26, x20, :lo12:.LANCHOR0
	ldrh	w28, [x26,3230]
	ldr	x0, [x26,3208]
	mov	w1, w28
	add	w28, w28, 1
	bl	_list_get_gc_head_node.isra.5
	strh	w28, [x26,3230]
	cmp	w27, w0, uxth
	beq	.L527
	mov	w1, 0
	mov	w2, w1
	bl	gc_add_sblk
	cbz	w0, .L528
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w23
	bcc	.L528
	b	.L531
.L527:
	strh	wzr, [x26,3230]
	b	.L529
.L528:
	sub	w25, w25, #1
	uxth	w25, w25
	cbnz	w25, .L530
.L529:
	cmp	w19, w23
	bcs	.L531
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x20,2814]
	ldrh	w1, [x20,3228]
	cmp	w1, w0
	bls	.L526
	add	w0, w0, 8
	b	.L609
.L531:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w1, [x20,3228]
	ldrh	w0, [x20,2814]
	cmp	w0, w1, lsr 1
	bls	.L526
	sub	w0, w0, #8
.L609:
	strh	w0, [x20,2814]
	b	.L526
.L510:
	and	w1, w24, 1
	strh	w19, [x0,3230]
	str	w1, [x29,104]
	tbz	x24, 0, .L532
	ldrh	w2, [x0,3232]
	ldrh	w1, [x0,2796]
	cmp	w1, w2, lsr 1
	bcc	.L555
	ldrh	w0, [x0,2794]
	cmp	w0, w1
	bls	.L534
.L555:
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L586:
	add	x28, x20, :lo12:.LANCHOR0
	ldrh	w25, [x28,3230]
	ldr	x0, [x28,3208]
	mov	w1, w25
	bl	_list_get_gc_head_node.isra.5
	cmp	w26, w0, uxth
	add	w2, w25, 1
	strh	w2, [x28,3230]
	beq	.L535
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L536
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcc	.L536
	b	.L534
.L535:
	strh	wzr, [x28,3230]
	b	.L534
.L536:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L586
.L534:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3232]
	ldrh	w1, [x0,2796]
	cmp	w1, w2, lsr 1
	bls	.L532
	strh	wzr, [x0,3224]
	mov	w23, 64
	mov	w26, 65535
	uxth	w27, w22
.L538:
	add	x25, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w28, [x25,3224]
	mov	w0, w28
	add	w28, w28, 1
	bl	zftl_get_gc_node
	strh	w28, [x25,3224]
	cmp	w26, w0, uxth
	bne	.L610
.L532:
	tbz	x24, 1, .L539
	add	x0, x20, :lo12:.LANCHOR0
	mov	w25, 64
	mov	w26, 0
	mov	w27, 65535
	uxth	w28, w22
	strh	wzr, [x0,3226]
.L544:
	add	x23, x20, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w2, [x23,3226]
	str	x2, [x29,96]
	mov	w0, w2
	bl	zftl_get_gc_node
	uxth	w1, w0
	ldr	x2, [x29,96]
	cmp	w1, w27
	add	w2, w2, 1
	strh	w2, [x23,3226]
	beq	.L540
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x23,72]
	ldrh	w2, [x2,x1]
	ldrh	w1, [x23,2810]
	cmp	w2, w1
	bcs	.L541
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L542
	add	w19, w19, 1
	add	w26, w26, 1
	uxth	w19, w19
	uxth	w26, w26
	cmp	w19, w28
	bcc	.L542
	b	.L543
.L541:
	strh	wzr, [x23,3226]
	b	.L543
.L540:
	strh	wzr, [x23,3224]
	b	.L543
.L542:
	sub	w3, w25, #1
	uxth	w25, w3
	cbnz	w25, .L544
.L543:
	cmp	w19, w22
	bcc	.L545
	cbnz	w26, .L546
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0,2798]
	ldrh	w0, [x0,3234]
	cmp	w1, w0
	bls	.L546
.L545:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w3, [x0,3196]
	ldrh	w2, [x0,3194]
	ldrh	w1, [x0,2810]
	mul	w2, w2, w3
	ldrh	w3, [x0,3228]
	sub	w2, w2, w3, lsr 3
	cmp	w1, w2
	bge	.L539
	add	w1, w1, 4
	b	.L608
.L546:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w2, [x0,3228]
	ldrb	w3, [x0,3196]
	ldrh	w1, [x0,2810]
	mul	w2, w2, w3
	cmp	w1, w2
	ble	.L539
	sub	w1, w1, #4
.L608:
	strh	w1, [x0,2810]
.L539:
	ldr	w0, [x29,104]
	cbz	w0, .L526
	add	x0, x20, :lo12:.LANCHOR0
	mov	w23, 64
	mov	w25, 65535
	uxth	w26, w22
	strh	wzr, [x0,3226]
.L550:
	add	x27, x20, :lo12:.LANCHOR0
	mov	w1, 3
	ldrh	w0, [x27,3226]
	bl	zftl_get_gc_node
	uxth	w1, w0
	cmp	w1, w25
	beq	.L547
	ldr	x2, [x27,72]
	ubfiz	x1, x1, 1, 16
	ldrh	w1, [x2,x1]
	ldrh	w2, [x27,2812]
	cmp	w2, w1
	bls	.L557
	cmp	w1, 2
	bhi	.L548
.L557:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L548
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w26
	bcs	.L547
.L548:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L550
.L547:
	cmp	w19, w22
	add	x20, x20, :lo12:.LANCHOR0
	bcs	.L551
	ldrh	w1, [x20,3228]
	ldrh	w0, [x20,2812]
	cmp	w0, w1, lsr 1
	bls	.L526
	sub	w0, w0, #4
	strh	w0, [x20,2812]
	b	.L526
.L551:
	ldrh	w0, [x20,3228]
	ldrb	w2, [x20,3196]
	ldrh	w1, [x20,2812]
	mul	w0, w0, w2
	sub	w0, w0, #32
	cmp	w1, w0
	bge	.L526
	add	w1, w1, 4
	strh	w1, [x20,2812]
.L526:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L552
	adrp	x0, .LC50
	mov	w1, w21
	add	x0, x0, :lo12:.LC50
	mov	w2, w24
	mov	w3, w19
	mov	w4, w22
	bl	printk
.L552:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L610:
	mov	w1, 0
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L537
	add	w19, w19, 1
	uxth	w19, w19
	cmp	w19, w27
	bcs	.L532
.L537:
	sub	w23, w23, #1
	uxth	w23, w23
	cbnz	w23, .L538
	b	.L532
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
	bne	.L612
	add	x0, x2, 3240
	add	x2, x2, 2788
	b	.L615
.L612:
	cmp	w0, 16
	bne	.L614
	add	x0, x2, 3248
	add	x2, x2, 2790
	b	.L615
.L614:
	add	x0, x2, 3256
	add	x2, x2, 2792
.L615:
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
	bne	.L617
	add	x0, x2, 3208
	add	x2, x2, 2794
	b	.L620
.L617:
	cmp	w3, 96
	bne	.L619
	add	x0, x2, 3216
	add	x2, x2, 2796
	b	.L620
.L619:
	cmp	w3, 160
	bne	.L616
	add	x0, x2, 3200
	add	x2, x2, 2798
.L620:
	bl	_insert_data_list
.L616:
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
	bne	.L622
	ldrh	w1, [x2,2790]
	ldrh	w0, [x2,2792]
	cmp	w1, w0
	bls	.L623
	add	x0, x2, 3248
	mov	w1, w4
	add	x2, x2, 2790
	b	.L626
.L623:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, w4
	add	x0, x2, 3256
	add	x2, x2, 2792
	b	.L626
.L622:
	ldrh	w1, [x2,2788]
	ldrh	w0, [x2,2792]
	cmp	w1, w0
	bls	.L623
	add	x0, x2, 3240
	mov	w1, w4
	add	x2, x2, 2788
.L626:
	bl	_list_pop_index_node
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L625
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC51
	add	x0, x0, :lo12:.LC51
	mov	w1, w20
	mov	w2, w21
	ldr	x3, [x19,3240]
	ldrh	w4, [x19,2788]
	ldrh	w5, [x19,2792]
	bl	printk
.L625:
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
	bne	.L628
	add	x0, x2, 3208
	add	x2, x2, 2794
	b	.L631
.L628:
	cmp	w3, 96
	bne	.L630
	add	x0, x2, 3216
	add	x2, x2, 2796
	b	.L631
.L630:
	cmp	w3, 160
	bne	.L627
	add	x0, x2, 3200
	add	x2, x2, 2798
.L631:
	bl	_list_remove_node
.L627:
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
	bne	.L633
	add	x0, x2, 3240
	add	x2, x2, 2788
	b	.L636
.L633:
	cmp	w0, 16
	bne	.L635
	add	x0, x2, 3248
	add	x2, x2, 2790
	b	.L636
.L635:
	add	x0, x2, 3256
	add	x2, x2, 2792
.L636:
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
	bne	.L638
	add	x0, x2, 3208
	add	x2, x2, 2794
	b	.L641
.L638:
	cmp	w3, 96
	bne	.L640
	add	x0, x2, 3216
	add	x2, x2, 2796
	b	.L641
.L640:
	cmp	w3, 160
	bne	.L637
	add	x0, x2, 3200
	add	x2, x2, 2798
.L641:
	bl	_list_update_data_list
.L637:
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
	adrp	x0, .LC52
	stp	x21, x22, [sp,64]
	stp	x23, x24, [sp,80]
	stp	x25, x26, [sp,96]
	add	x0, x0, :lo12:.LC52
	ldrh	w2, [x1]
	ldr	x1, [x19]
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L642
	mov	x22, -6148914691236517206
	adrp	x23, .LC53
	mov	w20, 0
	adrp	x25, .LANCHOR0
	add	x22, x22, 1
	add	x23, x23, :lo12:.LC53
	mov	w26, 65535
	mov	w24, 6
.L645:
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
	beq	.L642
	add	w20, w20, 1
	ldr	x0, [x21,8]
	umull	x19, w19, w24
	cmp	w20, 33
	add	x19, x0, x19
	bne	.L645
.L642:
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
	add	x0, x19, 3240
	add	x1, x19, 2788
	bl	print_list_info
	add	x0, x19, 3248
	add	x1, x19, 2790
	bl	print_list_info
	add	x0, x19, 3256
	add	x1, x19, 2792
	bl	print_list_info
	add	x0, x19, 3208
	add	x1, x19, 2794
	bl	print_list_info
	add	x0, x19, 3216
	add	x1, x19, 2796
	bl	print_list_info
	add	x0, x19, 3200
	add	x1, x19, 2798
	bl	print_list_info
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	zftl_proc_ftl_read
	.type	zftl_proc_ftl_read, %function
zftl_proc_ftl_read:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	bl	ftl_print_info_to_buf
	mov	w19, w0
	bl	dump_all_list_info
	mov	w0, 200
	bl	msleep
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_proc_ftl_read, .-zftl_proc_ftl_read
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
.L652:
	add	x8, x7, :lo12:.LANCHOR0
	ldrb	w3, [x8,3196]
	cmp	w6, w3
	bge	.L658
	asr	w3, w11, w6
	tbnz	x3, 0, .L653
	ldrb	w5, [x8,3264]
	sbfiz	x10, x2, 1, 32
	ldrh	w3, [x8,3266]
	sdiv	w9, w6, w5
	sub	w3, w12, w3
	lsl	w3, w9, w3
	madd	w3, w0, w5, w3
	uxth	w4, w3
	strh	w4, [x1,x10]
	ldrb	w3, [x8,3264]
	cmp	w3, 1
	bls	.L654
	sub	w3, w3, #1
	and	w3, w6, w3
	add	w3, w4, w3
	strh	w3, [x1,x10]
.L654:
	add	w2, w2, 1
.L653:
	add	w6, w6, 1
	b	.L652
.L658:
	mov	w0, w2
	mov	w4, -1
.L656:
	add	x3, x7, :lo12:.LANCHOR0
	ldrb	w3, [x3,3196]
	cmp	w2, w3
	bge	.L659
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L656
.L659:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x23, .LC54
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	w20, 0
	add	x22, x21, 2840
	add	x23, x23, :lo12:.LC54
	adrp	x24, .LANCHOR2
.L662:
	ldrh	w0, [x22,56]
	adrp	x26, .LANCHOR0
	cmp	w0, w20
	bls	.L698
	add	x0, x22, x20, sxtw 1
	ldrh	w25, [x0,58]
	ldr	x0, [x21,72]
	lsl	x27, x25, 1
	mov	x19, x25
	ldrh	w2, [x0,x27]
	cbz	w2, .L663
	mov	x0, x23
	mov	w1, w25
	bl	printk
.L663:
	add	x0, x26, :lo12:.LANCHOR0
	ldr	x1, [x0,72]
	ldrh	w1, [x1,x27]
	cbnz	w1, .L664
	ldr	x0, [x0,64]
	add	x25, x0, x25, lsl 2
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L665
	ldrb	w2, [x25,2]
	adrp	x0, .LC55
	add	x0, x0, :lo12:.LC55
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L665:
	ldrb	w0, [x25,2]
	tst	w0, 192
	beq	.L679
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L666
.L679:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1088
	mov	w2, 770
	bl	printk
.L668:
	b	.L668
.L666:
	mov	w0, w19
	add	x26, x26, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x26,80]
	ldrh	w0, [x2,124]
	cbz	w0, .L669
	mov	x1, 0
.L671:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L670
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,392]
	sub	w0, w0, #1
	strh	w0, [x2,124]
	b	.L669
.L670:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L671
.L669:
	ldrh	w0, [x2,120]
	cbz	w0, .L672
	mov	x1, 0
.L674:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L673
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,136]
	sub	w0, w0, #1
	strh	w0, [x2,120]
	b	.L672
.L673:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L674
.L672:
	ldrh	w0, [x2,122]
	cbz	w0, .L675
	mov	x1, 0
.L677:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L676
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,264]
	sub	w0, w0, #1
	strh	w0, [x2,122]
	b	.L675
.L676:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L677
	b	.L675
.L664:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L675:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L662
.L698:
	strh	wzr, [x22,56]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
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
	ldrh	w19, [x5,3266]
	mov	x20, x3
	sub	w19, w0, w19
	ldrb	w0, [x5,3268]
	asr	w22, w2, w19
	lsl	w19, w4, w19
	sub	w19, w19, #1
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L700
	ldrh	w2, [x5,3270]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L700:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,3270]
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
.L705:
	add	x2, x23, :lo12:.LANCHOR0
	ldrb	w0, [x2,3272]
	cmp	w19, w0
	bge	.L716
	ldrb	w3, [x2,3264]
	mov	w21, 0
	ldrh	w2, [x2,3270]
	mov	w1, w21
	sub	w4, w3, #1
	mul	w6, w19, w3
	mul	w5, w24, w3
.L717:
	cmp	w1, w3
	bge	.L730
	add	w0, w1, w6
	asr	w0, w25, w0
	tbnz	x0, 0, .L706
	and	w0, w1, w4
	add	x7, x29, 80
	add	w0, w5, w0
	mul	w0, w0, w2
	str	w0, [x7,w21,sxtw 2]
	add	w21, w21, 1
.L706:
	add	w1, w1, 1
	b	.L717
.L730:
	cmp	w3, 4
	bne	.L729
	mov	x26, 0
.L708:
	cmp	w21, w26
	ble	.L711
	add	x2, x29, 80
	mov	w0, w19
	mov	w1, w22
	ldr	w2, [x2,x26,lsl 2]
	add	x26, x26, 1
	bl	flash_erase_block_en
	b	.L708
.L729:
	cmp	w21, 2
	bne	.L712
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3268]
	cbz	w0, .L713
	cmp	w22, wzr
	ldr	w2, [x29,80]
	ldr	w3, [x29,84]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L713:
	ldr	w2, [x29,80]
	mov	w0, w19
	ldr	w3, [x29,84]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L711
.L712:
	cmp	w21, 1
	bne	.L711
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,3268]
	cbz	w0, .L715
	cmp	w22, wzr
	ldr	w2, [x29,80]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L715:
	ldr	w2, [x29,80]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L711:
	add	w19, w19, 1
	b	.L705
.L716:
	cbnz	w22, .L718
	ldr	x1, [x2,64]
	ldrh	w0, [x1,x20]
	add	w3, w0, 1
	bfi	w0, w3, 0, 11
	strh	w0, [x1,x20]
	ldr	x0, [x2,2800]
	ldr	w1, [x0,84]
	ldrh	w3, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,84]
	ldr	x1, [x2,64]
	ldrh	w1, [x1,x20]
	ubfx	x1, x1, 0, 11
	uxth	w2, w1
	cmp	w3, w2
	bge	.L720
	strh	w1, [x0,96]
	b	.L720
.L718:
	ldr	x3, [x2,64]
	ldr	w0, [x3,x20]
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x3,x20]
	ldr	x0, [x2,2800]
	ldr	w1, [x0,80]
	add	w1, w1, 1
	str	w1, [x0,80]
	ldr	x1, [x2,64]
	ldrh	w2, [x0,98]
	ldr	w1, [x1,x20]
	ubfx	x1, x1, 11, 8
	cmp	w2, w1, uxtb
	bcs	.L720
	strh	w1, [x0,98]
.L720:
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
	adrp	x0, .LANCHOR0+2800
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x0,#:lo12:.LANCHOR0+2800]
	ldrh	w0, [x1,136]
	cmp	w0, 63
	bls	.L732
	strh	wzr, [x1,136]
.L732:
	ldrh	w3, [x1,112]
	mov	w5, 65535
	cbnz	w3, .L738
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1104
	mov	w2, 988
	bl	printk
.L734:
	b	.L734
.L737:
	sxtw	x4, w2
	add	x4, x4, 80
	ldrh	w0, [x1,x4,lsl 1]
	cmp	w0, w5
	bne	.L740
	add	w2, w2, 1
.L735:
	cmp	w2, 63
	ble	.L737
	strh	wzr, [x1,136]
.L738:
	ldrh	w2, [x1,136]
	b	.L735
.L740:
	mov	w5, -1
	strh	w5, [x1,x4,lsl 1]
	strh	w2, [x1,136]
	sub	w3, w3, #1
	strh	w3, [x1,112]
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_alloc_sys_blk, .-ftl_alloc_sys_blk
	.align	2
	.global	ftl_free_sys_blk
	.type	ftl_free_sys_blk, %function
ftl_free_sys_blk:
	adrp	x1, .LANCHOR0+2800
	uxth	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR0+2800]
	ldrh	w2, [x1,138]
	cmp	w2, 63
	bls	.L742
	strh	wzr, [x1,138]
.L742:
	ldrh	w3, [x1,112]
	mov	w5, 65535
	cmp	w3, 63
	bls	.L748
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1128
	mov	w2, 1012
	bl	printk
.L744:
	b	.L744
.L746:
	add	w2, w2, 1
.L745:
	cmp	w2, 63
	ble	.L747
	strh	wzr, [x1,138]
.L748:
	ldrh	w2, [x1,138]
	b	.L745
.L747:
	sxtw	x4, w2
	add	x4, x4, 80
	ldrh	w6, [x1,x4,lsl 1]
	cmp	w6, w5
	bne	.L746
	strh	w0, [x1,x4,lsl 1]
	add	w3, w3, 1
	strh	w2, [x1,138]
	strh	w3, [x1,112]
	ldp	x29, x30, [sp], 16
	ret
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
	beq	.L750
	adrp	x19, .LANCHOR0
	ubfiz	x20, x2, 2, 16
	add	x23, x19, :lo12:.LANCHOR0
	ldr	x21, [x23,64]
	add	x22, x21, x20
	ldrb	w1, [x22,2]
	tst	w1, 224
	bne	.L750
	ldrb	w0, [x0,4]
	bfi	w1, w0, 5, 3
	mov	w0, w2
	strb	w1, [x22,2]
	bl	zftl_remove_free_node
	ldrb	w0, [x22,2]
	tbz	x0, 3, .L754
	ldr	x1, [x23,2800]
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L755
.L754:
	tst	w0, 24
	ldr	x1, [x23,2800]
	bne	.L756
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L755
.L756:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L755:
	ldrb	w0, [x22,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L757
	ldr	w0, [x21,x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21,x20]
	ldr	x1, [x19,2800]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L750
.L757:
	ldrh	w1, [x21,x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21,x20]
	bne	.L758
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L750
.L758:
	cmp	w0, 96
	bne	.L750
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L750:
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
	adrp	x2, .LANCHOR0
	uxth	w1, w0
	add	x3, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w0, [x3,3228]
	ldr	x4, [x3,80]
	ldrb	w3, [x3,3196]
	mul	w0, w0, w3
	cmp	w1, w0
	bge	.L760
	add	x4, x4, 16
	b	.L761
.L760:
	sub	w1, w1, w0
	add	x4, x4, 48
	uxth	w1, w1
.L761:
	ldrb	w3, [x4,9]
	sdiv	w5, w1, w3
	msub	w1, w5, w3, w1
	add	x1, x4, x1, uxth 1
	ldrh	w0, [x1,16]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L762
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1152
	mov	w2, 1390
	bl	printk
.L763:
	b	.L763
.L762:
	add	x2, x2, :lo12:.LANCHOR0
	ldp	x29, x30, [sp], 16
	ldrh	w1, [x2,3270]
	madd	w0, w0, w1, w5
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
	ldrh	w2, [x2,3280]
	ldr	x4, [x1,3792]
	ldr	x5, [x1,3800]
.L765:
	cmp	w2, w3
	beq	.L769
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L766
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L767
.L766:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L765
.L769:
	mov	w0, -1
.L767:
	ldp	x29, x30, [sp], 16
	ret
	.size	lpa_hash_get_ppa, .-lpa_hash_get_ppa
	.align	2
	.global	ftl_get_new_free_page
	.type	ftl_get_new_free_page, %function
ftl_get_new_free_page:
	stp	x29, x30, [sp, -16]!
	mov	x1, x0
	mov	w4, 65535
	add	x29, sp, 0
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L771
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1176
	mov	w2, 1505
	bl	printk
.L772:
	b	.L772
.L771:
	adrp	x2, .LANCHOR0
	ldrh	w3, [x1,2]
	add	x5, x2, :lo12:.LANCHOR0
	ldrh	w0, [x5,3228]
	cmp	w3, w0
	bne	.L773
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1176
	mov	w2, 1506
	bl	printk
.L774:
	b	.L774
.L773:
	ldrh	w3, [x1,6]
	cbnz	w3, .L775
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1176
	mov	w2, 1507
	bl	printk
.L776:
	b	.L776
.L775:
	ldrb	w0, [x1,5]
	ldrb	w5, [x5,3196]
	add	x0, x0, 8
	ldrh	w0, [x1,x0,lsl 1]
.L777:
	cmp	w0, w4
	bne	.L781
	ldrb	w0, [x1,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x1,5]
	cmp	w0, w5
	bne	.L778
	ldrh	w0, [x1,2]
	strb	wzr, [x1,5]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L778:
	ldrb	w0, [x1,5]
	add	x0, x0, 8
	ldrh	w0, [x1,x0,lsl 1]
	b	.L777
.L781:
	sub	w3, w3, #1
	strh	w3, [x1,6]
	ldrh	w3, [x1,10]
	add	x2, x2, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	add	w3, w3, 1
	strh	w3, [x1,10]
	ldrb	w3, [x1,5]
	ldrh	w5, [x2,3270]
	add	w3, w3, 1
	ldrb	w2, [x2,3196]
	uxtb	w3, w3
	strb	w3, [x1,5]
	mul	w0, w0, w5
	cmp	w2, w3
	orr	w0, w0, w4
	bne	.L780
	add	w4, w4, 1
	strb	wzr, [x1,5]
	strh	w4, [x1,2]
.L780:
	ldp	x29, x30, [sp], 16
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
	uxth	w1, w0
	mov	w20, w1
	mov	w2, 65533
	sub	w1, w1, #1
	cmp	w2, w1, uxth
	bcs	.L783
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1200
	mov	w2, 1538
	bl	printk
.L784:
	b	.L784
.L783:
	adrp	x19, .LANCHOR0
	mov	w1, 0
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_erase_phy_blk
	ldr	x0, [x19,2800]
	ldrh	w0, [x0,130]
	bl	ftl_free_sys_blk
	ldr	x1, [x19,2800]
	mov	w0, 0
	strh	w20, [x1,130]
	strh	wzr, [x1,140]
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
	ldrh	w2, [x3,3808]
	cbnz	w0, .L786
	cmp	w2, 4
	bhi	.L786
	add	w2, w2, 1
	strh	w2, [x3,3808]
	b	.L785
.L786:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w9, 65535
	strh	wzr, [x0,3808]
	ldrh	w6, [x0,56]
	ldr	x7, [x0,72]
	ldr	x8, [x0,64]
	mov	x0, 0
	mov	w2, w0
	mov	w3, w0
.L788:
	cmp	w6, w0, uxth
	bls	.L796
	ldrh	w4, [x7,x0,lsl 1]
	cmp	w4, w9
	beq	.L789
	add	x5, x8, x0, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L790
	add	w2, w2, w4
	b	.L789
.L790:
	add	w3, w3, w4
.L789:
	add	x0, x0, 1
	b	.L788
.L796:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,80]
	ldr	x1, [x1,2800]
	str	w3, [x0,524]
	str	w2, [x0,528]
	ldrh	w1, [x1,120]
	cbz	w1, .L785
	udiv	w2, w2, w1
	str	w2, [x0,532]
.L785:
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
	beq	.L800
	add	x0, x2, :lo12:.LANCHOR0
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,72]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L801
	adrp	x0, .LC56
	mov	w2, w19
	add	x0, x0, :lo12:.LC56
	bl	printk
	b	.L805
.L801:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L800:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3810]
	cmp	w2, w1
	beq	.L802
	mov	w0, 65535
	cmp	w2, w0
	bne	.L803
	strh	w1, [x19,3810]
.L805:
	mov	w0, 0
	b	.L802
.L803:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,3810]
.L802:
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
	add	x4, x6, 3280
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,3792]
	ldr	x12, [x6,3800]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L807:
	cmp	w4, w11
	beq	.L811
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L808
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR0
	bne	.L809
	ldr	x6, [x4,3800]
	add	x4, x4, 3280
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L810
.L809:
	ldr	x4, [x4,3800]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L810:
	add	x4, x3, :lo12:.LANCHOR0
	mov	w6, -1
	ldr	x4, [x4,3800]
	strh	w6, [x4,x7,lsl 1]
	b	.L811
.L808:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L807
.L811:
	add	x3, x3, :lo12:.LANCHOR0
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,3792]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 3280
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,3800]
	strh	w6, [x0,x4,lsl 1]
	beq	.L813
	ldrh	w0, [x3,3266]
	mov	w2, 21
	mov	w4, 1
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w4, w2
	ldrb	w0, [x3,3264]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L813:
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
	ldrh	w5, [x1,3266]
	sub	w2, w2, w5
	lsr	w0, w0, w5
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x1,3264]
	udiv	w2, w0, w2
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	uxtb	w20, w2
	tbz	x0, 14, .L818
	adrp	x0, .LC57
	mov	w1, w21
	add	x0, x0, :lo12:.LC57
	mov	w2, w20
	bl	printk
.L818:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,56]
	cmp	w1, w20
	bls	.L817
	ldr	x0, [x0,64]
	add	x20, x0, x20, uxtb 2
	mov	w0, 1
	lsl	w1, w0, w21
	ldrb	w0, [x20,3]
	orr	w1, w1, w0
	strb	w1, [x20,3]
.L817:
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
	ldrh	w6, [x19,2798]
	ldrh	w1, [x19,2788]
	ldr	w0, [x7,528]
	str	w0, [sp]
	ldr	w0, [x19,2784]
	str	w0, [sp,8]
	adrp	x0, .LC58
	ldrh	w2, [x19,2790]
	add	x0, x0, :lo12:.LC58
	ldr	w7, [x7,524]
	ldrh	w3, [x19,2792]
	ldrh	w4, [x19,2794]
	ldrh	w5, [x19,2796]
	bl	printk
	ldr	x5, [x19,2800]
	adrp	x0, .LC59
	ldrb	w3, [x19,3812]
	add	x0, x0, :lo12:.LC59
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,2800]
	adrp	x0, .LC60
	add	x0, x0, :lo12:.LC60
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,2800]
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
	adrp	x0, .LC61
	add	x0, x0, :lo12:.LC61
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
	bl	printk
	ldrh	w1, [x19,2810]
	adrp	x0, .LC62
	ldrh	w2, [x19,2812]
	add	x0, x0, :lo12:.LC62
	ldrh	w3, [x19,2814]
	ldrh	w4, [x19,2816]
	ldrh	w5, [x19,2808]
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	cbnz	x0, .L825
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1224
	mov	w2, 592
	bl	printk
.L826:
	b	.L826
.L825:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x0,56]
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w2, [x19,2776]
	cmp	w2, w1
	bcs	.L827
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1224
	mov	w2, 597
	bl	printk
.L828:
	b	.L828
.L827:
	cbnz	w1, .L829
	bl	buf_free
	ldrb	w0, [x19,2819]
	b	.L830
.L829:
	add	x0, x19, 2818
	mov	x1, x20
	bl	buf_add_tail
	ldrb	w2, [x19,2819]
	ldrb	w1, [x20,56]
	add	w2, w2, 1
	uxtb	w0, w2
	ldr	x2, [x19,2800]
	strb	w0, [x19,2819]
	ldr	w3, [x2,16]
	add	w1, w3, w1
	str	w1, [x2,16]
	ldr	w1, [x2,32]
	add	w1, w1, 1
	str	w1, [x2,32]
.L830:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	adrp	x21, .LC63
	stp	x23, x24, [sp,48]
	add	x22, x19, 728
	mov	w23, 21
	mov	w24, 1
	add	x21, x21, :lo12:.LC63
.L832:
	ldrb	w0, [x19,3813]
	cmp	w0, 255
	beq	.L839
	sbfiz	x0, x0, 6, 32
	add	x20, x22, x0
	add	x4, x20, 32
	ldrb	w0, [x22,x0]
	strb	w0, [x19,3813]
	ldr	w0, [x20,52]
	cmn	w0, #1
	bne	.L833
	ldrh	w2, [x19,3266]
	mov	x0, x21
	ldr	w3, [x4,8]
	sub	w1, w23, w2
	lsl	w1, w24, w1
	lsr	w2, w3, w2
	sub	w1, w1, #1
	and	w2, w1, w2
	ldrb	w1, [x19,3264]
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
	bne	.L834
	strh	wzr, [x0,22]
	b	.L835
.L834:
	ldrh	w1, [x0,48]
	cmp	w1, w25
	bne	.L835
	strh	wzr, [x0,54]
.L835:
	mov	x0, x20
	bl	ftl_write_buf
	b	.L832
.L833:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x20,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x20,2]
	tbz	x0, 2, .L837
	and	w0, w0, -3
	strb	w0, [x20,2]
	b	.L832
.L837:
	mov	x0, x20
	bl	buf_free
	b	.L832
.L839:
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
	stp	x29, x30, [sp, -48]!
	cmp	w0, 5
	mov	w1, 2
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxth	w22, w0
	csel	w21, wzr, w1, ne
	stp	x19, x20, [sp,16]
	mov	w1, w22
	mov	w20, w0
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w19, w0
	mov	w1, 65535
	cmp	w19, w1
	beq	.L842
	adrp	x1, .LANCHOR0+64
	ldr	x1, [x1,#:lo12:.LANCHOR0+64]
	add	x19, x1, x19, uxth 2
	ldrb	w3, [x19,2]
	tst	w3, 224
	beq	.L843
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1240
	mov	w2, 752
	bl	printk
.L844:
	b	.L844
.L843:
	bfi	w3, w20, 5, 3
	ubfx	x2, x3, 3, 2
	orr	w1, w21, w2
	bfi	w3, w1, 3, 2
	strb	w3, [x19,2]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
.L842:
	adrp	x21, .LC64
	bl	print_ftl_debug_info
	add	x21, x21, :lo12:.LC64
	mov	w2, w20
	mov	w1, w19
	mov	x0, x21
	bl	printk
	mov	w1, w22
	mov	w0, 0
	bl	zftl_get_free_sblk
	uxth	w1, w0
	mov	w2, w20
	mov	x0, x21
	bl	printk
	bl	dump_all_list_info
	adrp	x1, .LANCHOR1
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 1240
	mov	w2, 762
	bl	printk
.L845:
	b	.L845
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	adrp	x0, .LANCHOR0
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,3814]
	strb	w1, [x0,3813]
	strb	w1, [x0,2832]
	strb	w1, [x0,3176]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC65
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC65
	ldrb	w1, [x20,3814]
	bl	printk
	ldrb	w19, [x20,3814]
	cmp	w19, 255
	beq	.L849
	add	x20, x20, 728
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC66
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC66
.L851:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L849
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L851
.L849:
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
	ldrb	w2, [x3,3814]
	cmp	w2, 255
	beq	.L859
	ldrh	w8, [x3,3266]
	mov	w4, 21
	ldrb	w7, [x3,3264]
	mov	w0, 1
	sub	w4, w4, w8
	ubfx	x9, x5, 21, 3
	lsl	w4, w0, w4
	sub	w7, w7, #1
	sub	w4, w4, #1
	asr	w5, w5, w8
	uxth	w7, w7
	adrp	x10, .L863
	uxth	w4, w4
	and	w5, w5, w7
	and	w5, w5, w4
	add	x3, x3, 728
	add	x10, x10, :lo12:.L863
.L866:
	add	x0, x3, x2, lsl 6
	ldr	w6, [x0,40]
	ubfx	x11, x6, 21, 3
	cmp	w11, w9
	bne	.L860
	lsr	w6, w6, w8
	ldrb	w0, [x0,58]
	and	w6, w7, w6
	and	w6, w4, w6
	cmp	w6, w5
	bne	.L861
	cmp	w1, 1
	beq	.L864
	b	.L859
.L861:
	cmp	w1, 3
	bhi	.L860
	ldrb	w6, [x10,w1,uxtw]
	adr	x11, .Lrtx863
	add	x6, x11, w6, sxtb #2
	br	x6
.Lrtx863:
	.section	.rodata
	.align	0
	.align	2
.L863:
	.byte	(.L862 - .Lrtx863) / 4
	.byte	(.L864 - .Lrtx863) / 4
	.byte	(.L865 - .Lrtx863) / 4
	.byte	(.L859 - .Lrtx863) / 4
	.text
.L862:
	cmp	w0, 2
	b	.L875
.L864:
	sub	w6, w0, #6
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L860
	b	.L859
.L865:
	cmp	w0, 10
.L875:
	bne	.L859
.L860:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L866
	mov	w0, 0
.L859:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w2, [x1,3814]
	cmp	w2, 255
	beq	.L877
	add	x6, x1, 728
	lsl	x5, x2, 6
	add	x7, x6, x5
	ldrb	w3, [x7,58]
	sub	w3, w3, #11
	uxtb	w3, w3
	cmp	w3, 1
	bhi	.L877
	ldrb	w3, [x6,x5]
	strb	w3, [x1,3814]
	mov	w3, -1
	strb	w3, [x6,x5]
	ldrb	w3, [x7,59]
	cmp	w3, 1
	bne	.L879
	ldrh	w3, [x7,50]
	cbnz	w3, .L880
	ldrb	w3, [x1,3813]
	add	x1, x1, 3813
	b	.L881
.L880:
	ldrb	w3, [x1,3176]
	add	x1, x1, 3176
	b	.L881
.L879:
	cbnz	w3, .L877
	ldr	w3, [x7,36]
	cmn	w3, #1
	beq	.L877
	ldrb	w3, [x1,2832]
	add	x1, x1, 2832
.L881:
	cmp	w3, 255
	add	x0, x0, :lo12:.LANCHOR0
	bne	.L885
	add	x0, x0, x2, lsl 6
	ldrb	w0, [x0,729]
	strb	w0, [x1]
	b	.L877
.L885:
	add	x0, x0, 728
.L892:
	mov	w1, w3
	sbfiz	x3, x3, 6, 32
	ldrb	w3, [x0,x3]
	cmp	w3, 255
	bne	.L892
	add	x2, x0, x2, lsl 6
	sbfiz	x1, x1, 6, 32
	ldrb	w2, [x2,1]
	strb	w2, [x0,x1]
.L877:
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
	ldr	x2, [x3,2800]
	ldrh	w1, [x2,690]
	add	w1, w1, 1
	uxth	w0, w1
	ldrb	w1, [x3,3196]
	strh	w0, [x2,690]
	cmp	w1, w0
	bls	.L894
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L895
.L894:
	adrp	x22, .LC67
	add	x22, x22, :lo12:.LC67
.L909:
	mov	w0, 1
	add	x21, x19, :lo12:.LANCHOR0
	bl	ftl_alloc_sblk
	uxth	w20, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x21,2800]
	mov	w0, w20
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	uxth	w0, w0
	cbnz	w0, .L896
	mov	w1, w20
	mov	x0, x22
	bl	printk
	ldr	x0, [x21,64]
	add	x20, x0, x20, uxth 2
	ldrb	w1, [x20,2]
	orr	w1, w1, -32
	strb	w1, [x20,2]
	b	.L909
.L896:
	ldr	x0, [x21,2800]
	mov	w1, 1
	mov	w3, 65535
	str	w1, [x21,3816]
	mov	x1, 0
	strh	wzr, [x0,690]
.L898:
	add	x4, x0, x1, lsl 1
	mov	w2, w1
	ldrh	w4, [x4,416]
	cmp	w4, w3
	beq	.L897
	add	x1, x1, 1
	cmp	x1, 128
	bne	.L898
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 188
	bl	printk
.L900:
	b	.L900
.L897:
	add	x1, x0, x2, sxtw 1
	strh	w20, [x1,416]
	ldrh	w1, [x0,688]
	add	w1, w1, 1
	strh	w1, [x0,688]
.L895:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 65533
	ldr	x0, [x19,2800]
	ldrh	w1, [x0,690]
	add	x1, x1, 336
	ldrh	w1, [x0,x1,lsl 1]
	sub	w3, w1, #1
	cmp	w2, w3, uxth
	bcs	.L901
	adrp	x0, .LC0
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 193
	bl	printk
.L902:
	b	.L902
.L901:
	ldrh	w3, [x19,3266]
	mov	w2, 21
	strh	wzr, [x0,696]
	sub	w2, w2, w3
	strh	w1, [x0,692]
	asr	w4, w1, w2
	strh	w4, [x0,694]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L906
	mov	w0, 1
	mov	w3, w1
	lsl	w2, w0, w2
	adrp	x0, .LC68
	add	x0, x0, :lo12:.LC68
	sub	w2, w2, #1
	uxth	w4, w4
	bl	printk
.L906:
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
	adrp	x3, .LANCHOR0
	mov	x0, 0
	stp	x29, x30, [sp, -16]!
	add	x3, x3, :lo12:.LANCHOR0
	mov	w4, 65535
	add	x29, sp, 0
.L913:
	add	x2, x3, 3824
	lsl	x5, x0, 4
	uxth	w1, w0
	ldrh	w5, [x5,x2]
	cmp	w5, w4
	beq	.L919
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L913
	mov	x1, 0
	mov	w4, 32768
.L915:
	add	x3, x2, x1, lsl 4
	uxth	w5, w1
	ldrh	w3, [x3,2]
	tbnz	x3, 15, .L914
	cmp	w3, w4
	bcs	.L914
	mov	w4, w3
	mov	w0, w5
.L914:
	add	x1, x1, 1
	cmp	x1, 32
	bne	.L915
	cmp	w0, 32
	mov	w3, w0
	bne	.L912
	adrp	x0, .LANCHOR4-16
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w2, 0
	add	x1, x1, 3824
	ldrb	w4, [x0,#:lo12:.LANCHOR4-16]
	mov	w0, -1
.L917:
	ldrh	w6, [x1,2]
	cmp	w6, w0
	bcs	.L916
	ldrh	w5, [x1]
	cmp	w5, w4
	csel	w0, w0, w6, eq
	cmp	w5, w4
	csel	w3, w3, w2, eq
.L916:
	add	w2, w2, 1
	add	x1, x1, 16
	uxth	w2, w2
	cmp	w2, 32
	bne	.L917
	cmp	w3, 32
	mov	w0, w3
	bne	.L912
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 24
	mov	w2, 289
	bl	printk
.L918:
	b	.L918
.L919:
	mov	w0, w1
.L912:
	ldp	x29, x30, [sp], 16
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
	cbnz	w0, .L927
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L928:
	add	x1, x2, 168
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L928
.L934:
	adrp	x20, .LANCHOR4
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1024
	sub	x0, x20, #12
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, 0
	b	.L929
.L927:
	cmp	w0, 1
	bne	.L930
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L933:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L931
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L931:
	add	x2, x3, 168
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L933
	b	.L934
.L930:
	cmp	w0, 2
	bne	.L935
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L937:
	add	x3, x4, 168
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L937
	b	.L934
.L935:
	cmp	w0, 3
	bne	.L938
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L941:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L939
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L939:
	add	x2, x3, 168
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L941
	b	.L934
.L938:
	cmp	w0, 4
	bne	.L942
	add	x2, x19, :lo12:.LANCHOR0
	mov	w5, 7
	add	x1, x2, 168
	mov	w4, 6
	strh	w0, [x1,8]
	mov	w0, 5
	strh	wzr, [x2,168]
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
.L944:
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
	bne	.L944
	b	.L934
.L942:
	cmp	w0, 5
	bne	.L945
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L946:
	add	x1, x2, 168
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L946
	mov	x0, 0
.L947:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L947
	b	.L934
.L945:
	cmp	w0, 8
	bne	.L934
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L948:
	add	x1, x2, 168
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L948
	b	.L934
.L929:
	add	x1, x19, 168
	sub	x2, x20, #12
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x2,w1,sxtw 1]
	bne	.L929
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.type	flash_die_info_init, %function
flash_die_info_init:
	stp	x29, x30, [sp, -112]!
	mov	w2, 8
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x21, .LANCHOR0
	add	x25, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	ldrh	w0, [x25,394]
	add	x26, x22, :lo12:.LANCHOR4
	ldrb	w1, [x25,380]
	mov	x24, 0
	strh	w0, [x20,166]
	add	x25, x25, 369
	ldrh	w0, [x25,9]
	add	x27, x20, 684
	strb	wzr, [x20,152]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x26,1012]
	add	x0, x20, 156
	bl	ftl_memset
	add	x0, x26, 1016
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
	ldrb	w23, [x25,-1]
.L966:
	mov	x0, x25
	add	x1, x27, x24, lsl 3
	mov	w2, w23
	bl	flash_mem_cmp8
	cbnz	w0, .L965
	ldrb	w1, [x20,152]
	add	x2, x26, 1016
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x20, x1, sxtw
	strb	w0, [x20,152]
	strb	w24, [x1,156]
.L965:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L966
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 368
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L967
.L971:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR4
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,381]
	ldrb	w0, [x21,152]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,382]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1048]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L967:
	add	x24, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w27, [x1,23]
	mov	x25, 0
	and	w20, w20, 65280
	add	x26, x0, 369
	ldrh	w2, [x24,166]
	add	x3, x24, 684
	add	x4, x22, :lo12:.LANCHOR4
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L970:
	add	x1, x3, x25, lsl 3
	mov	x0, x26
	mov	w2, w23
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L968
	ldrb	w1, [x24,152]
	add	x0, x4, 1016
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x0,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x24, x1, sxtw
	strb	w0, [x24,152]
	strb	w25, [x1,156]
.L968:
	add	x25, x25, 1
	cmp	x25, 4
	bne	.L970
	b	.L971
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
	add	x0, x19, 3280
	bl	ftl_memset
	ldrh	w0, [x19,3228]
	mov	w1, 255
	ldrb	w2, [x19,3196]
	mul	w2, w0, w2
	ldr	x0, [x19,3800]
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
	adrp	x0, .LANCHOR2+400
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	str	x19, [sp,16]
	tbz	x0, 12, .L979
	adrp	x1, .LANCHOR3
	adrp	x0, .LC69
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC69
	add	x1, x1, 48
	mov	w2, 219
	mov	w3, 0
	bl	printk
.L979:
	adrp	x19, .LANCHOR0
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 512
	add	x0, x19, 3280
	bl	ftl_memset
	ldrh	w0, [x19,3228]
	mov	w1, 255
	ldrb	w2, [x19,3196]
	mul	w2, w0, w2
	ldr	x0, [x19,3800]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L980:
	ldrh	w1, [x19,3228]
	ldrb	w2, [x19,3196]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L989
	uxtw	x2, w0
	ldr	x1, [x19,3792]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L981
	uxtb	w1, w1
	add	x3, x19, 3280
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,3800]
	strh	w4, [x1,x2,lsl 1]
.L981:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L980
.L989:
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
	add	x1, x1, 368
	mov	w5, 1
	ldrb	w2, [x1,9]
	ldrh	w3, [x0,166]
	mul	w2, w2, w3
	ldrb	w3, [x0,140]
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
	ldrb	w4, [x0,152]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L991:
	cmp	w4, w1, uxtb
	bls	.L993
	add	x2, x0, 156
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L991
.L993:
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
	add	x21, x19, 2840
	mov	x0, x21
	strb	wzr, [x19,3193]
	strb	wzr, [x20,1050]
	bl	ftl_memset
	ldrh	w1, [x19,3228]
	mov	w0, -1
	ldrb	w3, [x19,3196]
	strh	w0, [x19,2840]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x19,2876]
	strh	w0, [x19,2814]
	ldrh	w0, [x19,3194]
	strh	w2, [x19,2878]
	mul	w2, w1, w3
	mul	w0, w0, w3
	strh	w1, [x19,2816]
	uxth	w2, w2
	lsl	w0, w0, 2
	sub	w4, w2, #32
	strh	w2, [x19,2810]
	strh	w4, [x19,2812]
	str	xzr, [x19,2848]
	strh	wzr, [x19,3230]
	strh	wzr, [x19,3224]
	strh	wzr, [x19,3226]
	bl	ftl_malloc
	ldrb	w1, [x19,3196]
	str	x0, [x20,1056]
	ldrh	w0, [x19,3194]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1064]
	ldrh	w1, [x19,3194]
	ldrb	w0, [x19,3196]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3184]
	ldrb	w1, [x19,3196]
	ldrh	w0, [x19,3194]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3168]
	ldrb	w1, [x19,3196]
	ldrh	w0, [x19,3194]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1072]
	ldrh	w0, [x19,3232]
	strh	w0, [x19,2808]
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
	ldr	x6, [x0,2800]
	ldr	w0, [x6,32]
	cmp	w0, w1
	bls	.L996
	ldr	w1, [x6,36]
	add	w1, w1, w0, lsr 10
	and	w0, w0, 1023
	str	w1, [x6,36]
	str	w0, [x6,32]
.L996:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,80]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L997
	ldr	w1, [x0,572]
	ldr	w3, [x6,36]
	add	w1, w1, 32
	cmp	w3, w1
	bls	.L995
.L997:
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
.L999:
	cmp	w3, w11
	bcs	.L1043
	ubfiz	x0, x3, 2, 16
	add	x5, x10, x0
	ldrb	w5, [x5,2]
	tbz	x5, 3, .L1000
	ldrh	w5, [x10,x0]
	and	w8, w5, 2047
	ldr	w5, [x10,x0]
	ubfx	x0, x5, 11, 8
	b	.L1001
.L1000:
	tst	w5, 24
	bne	.L1002
	ldrh	w5, [x10,x0]
	mov	w0, 65535
	and	w8, w5, 2047
	b	.L1001
.L1002:
	ldr	w5, [x10,x0]
	ubfx	x0, x5, 11, 8
.L1025:
	cmp	w19, w0
	add	w2, w2, 1
	csel	w19, w19, w0, cc
	cmp	w21, w0
	add	w4, w4, w0
	csel	w0, w21, w0, hi
	uxth	w2, w2
	uxth	w19, w19
	uxth	w21, w0
.L1026:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L999
.L1043:
	ldrh	w0, [x6,96]
	cmp	w0, w22
	bcs	.L1004
	strh	w22, [x6,96]
.L1004:
	ldrh	w0, [x6,98]
	cmp	w0, w21
	bcs	.L1005
	strh	w21, [x6,98]
.L1005:
	adrp	x23, .LANCHOR2
	str	x23, [x29,128]
	add	x0, x23, :lo12:.LANCHOR2
	udiv	w3, w9, w1
	strh	w3, [x6,88]
	ldr	w0, [x0,400]
	udiv	w4, w4, w2
	strh	w4, [x6,90]
	tbz	x0, 10, .L1006
	adrp	x0, .LC70
	uxth	w3, w3
	add	x0, x0, :lo12:.LC70
	uxth	w4, w4
	bl	printk
.L1006:
	ldr	x0, [x29,128]
	adrp	x25, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 10, .L1007
	add	x6, x25, :lo12:.LANCHOR4
	adrp	x0, .LC71
	add	x0, x0, :lo12:.LC71
	mov	w1, w26
	mov	w2, w19
	mov	w3, w22
	ldrh	w5, [x6,1080]
	mov	w4, w21
	ldrh	w6, [x6,1082]
	bl	printk
.L1007:
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
	ldrh	w0, [x1,1082]
	ldr	w2, [x29,136]
	cmp	w2, w0
	bgt	.L1008
	ldrh	w1, [x1,1080]
	sub	w0, w22, w26
	cmp	w0, w1
	bgt	.L1008
.L1015:
	cbz	w19, .L1017
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w0, [x0,134]
	b	.L1018
.L1008:
	add	x9, x20, :lo12:.LANCHOR0
	adrp	x27, .LC73
	adrp	x28, .LC72
	mov	w24, 0
	mov	x23, x9
	str	w24, [x29,140]
	ldr	x0, [x9,2800]
	ldrh	w21, [x0,134]
	sub	w0, w22, w26
	str	w0, [x29,124]
	add	x0, x27, :lo12:.LC73
	str	x0, [x29,112]
	add	x0, x28, :lo12:.LC72
	str	x0, [x29,104]
.L1011:
	ldrh	w0, [x23,56]
	cmp	w0, w21
	bls	.L1015
	uxtw	x22, w21
	ldr	x28, [x23,64]
	lsl	x27, x22, 2
	add	x0, x28, x27
	ldrb	w0, [x0,2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L1012
	tst	w0, 192
	cset	w2, eq
	cbnz	w2, .L1012
	ubfx	x0, x0, 3, 2
	and	w0, w0, 2
	uxtb	w1, w0
	add	x0, x25, :lo12:.LANCHOR4
	cbz	w1, .L1013
	ldrh	w0, [x0,1082]
	ldr	w1, [x29,136]
	cmp	w1, w0
	ble	.L1014
	ldr	w0, [x28,x27]
	add	w1, w19, 1
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1014
	mov	w1, w2
	mov	w0, w21
	mov	w2, 1
	bl	gc_add_sblk
	ldr	w0, [x29,140]
	add	w0, w0, 1
	str	w0, [x29,140]
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 10, .L1014
	ldr	x0, [x23,64]
	ldr	x11, [x23,72]
	add	x0, x0, x27
	ldrh	w5, [x28,x27]
	ldr	w6, [x28,x27]
	ldrb	w2, [x0,2]
	ldr	x0, [x29,104]
	b	.L1042
.L1013:
	ldrh	w0, [x0,1080]
	ldr	w2, [x29,124]
	cmp	w2, w0
	ble	.L1014
	ldrh	w0, [x28,x27]
	add	w2, w26, 1
	and	w0, w0, 2047
	cmp	w0, w2
	bgt	.L1014
	mov	w0, w21
	mov	w2, 1
	add	w24, w24, 1
	bl	gc_add_sblk
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 10, .L1014
	ldr	x0, [x23,64]
	ldr	x11, [x23,72]
	add	x0, x0, x27
	ldrh	w5, [x28,x27]
	ldr	w6, [x28,x27]
	ldrb	w2, [x0,2]
	ldr	x0, [x29,112]
.L1042:
	ldrh	w3, [x11,x22,lsl 1]
	mov	w1, w21
	ldrh	w4, [x23,2896]
	ubfx	x2, x2, 5, 3
	and	w5, w5, 2047
	ubfx	x6, x6, 11, 8
	bl	printk
.L1014:
	cmp	w24, 4
	bhi	.L1015
	ldr	w0, [x29,140]
	cmp	w0, 4
	bhi	.L1015
.L1012:
	add	w10, w21, 1
	uxth	w21, w10
	b	.L1011
.L1020:
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,64]
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w19
	bcc	.L1019
	sub	w2, w2, w19
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L1019:
	add	w0, w0, 1
	uxth	w0, w0
.L1018:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,56]
	cmp	w2, w0
	bhi	.L1020
	ldr	x0, [x1,2800]
	ldrh	w1, [x0,72]
	add	w1, w19, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w19
	bls	.L1017
	sub	w19, w1, w19
	strh	w19, [x0,98]
.L1017:
	cbz	w26, .L995
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w0, [x0,134]
.L1022:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,56]
	cmp	w2, w0
	bls	.L1044
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,64]
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w26
	blt	.L1023
	sub	w2, w2, w26
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L1023:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1022
.L1044:
	ldr	x0, [x1,2800]
	ldrh	w1, [x0,74]
	add	w1, w26, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w26
	bls	.L995
	sub	w7, w1, w26
	strh	w7, [x0,96]
	b	.L995
.L1001:
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
	bne	.L1025
	b	.L1026
.L995:
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
	adrp	x26, .LC74
	ldr	x0, [x19,8]
	mov	w25, -1
	add	x26, x26, :lo12:.LC74
	mul	w2, w2, w24
	bl	ftl_memset
	ldrh	w22, [x19,3228]
	ldrb	w0, [x19,3196]
	str	xzr, [x19,3240]
	str	xzr, [x19,3248]
	mul	w22, w0, w22
	str	xzr, [x19,3256]
	str	xzr, [x19,3216]
	sdiv	w22, w23, w22
	str	xzr, [x19,3208]
	str	xzr, [x19,3200]
	strh	wzr, [x19,2788]
	strh	wzr, [x19,2790]
	strh	wzr, [x19,2792]
	strh	wzr, [x19,2794]
	strh	wzr, [x19,2798]
	strh	wzr, [x19,2796]
	sxth	w0, w22
	str	w0, [x29,108]
	ldr	x0, [x19,2800]
	ldrsh	w19, [x0,134]
	strh	wzr, [x0,146]
.L1046:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x1,56]
	cmp	w19, w0
	bge	.L1076
	sxtw	x22, w19
	ldr	x27, [x1,64]
	ldr	w2, [x29,108]
	add	x27, x27, x22, lsl 2
	ldrb	w0, [x27,3]
	cbz	w0, .L1047
	mov	w0, 0
	ldrb	w5, [x1,3196]
	ldrh	w6, [x1,3228]
	mov	w2, w0
	ldr	x4, [x1,2800]
.L1048:
	cmp	w0, w5
	bge	.L1077
	ldrb	w1, [x27,3]
	asr	w1, w1, w0
	tbnz	x1, 0, .L1049
	add	w2, w6, w2
	sxth	w2, w2
	b	.L1050
.L1049:
	ldrh	w1, [x4,146]
	add	w1, w1, 1
	strh	w1, [x4,146]
.L1050:
	add	w0, w0, 1
	b	.L1048
.L1077:
	cbz	w2, .L1052
	sdiv	w2, w23, w2
	sxth	w2, w2
	b	.L1047
.L1052:
	ldrb	w0, [x27,2]
	orr	w0, w0, -32
	strb	w0, [x27,2]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	strh	w25, [x0,x22,lsl 1]
.L1047:
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
	beq	.L1055
	cmp	w0, 32
	cset	w28, eq
	cbnz	w28, .L1055
	ldr	x2, [x20,80]
	ldrh	w4, [x2,16]
	cmp	w19, w4
	beq	.L1055
	ldrh	w4, [x2,48]
	cmp	w19, w4
	beq	.L1055
	ldrh	w2, [x2,80]
	cmp	w19, w2
	beq	.L1055
	cmp	w0, 64
	bne	.L1057
	uxth	w27, w19
	add	x0, x20, 3208
	mov	w1, w27
	add	x2, x20, 2794
	b	.L1074
.L1057:
	cmp	w0, 96
	bne	.L1058
	uxth	w27, w19
	add	x0, x20, 3216
	mov	w1, w27
	add	x2, x20, 2796
	b	.L1074
.L1058:
	cmp	w0, 160
	bne	.L1059
	uxth	w27, w19
	add	x0, x20, 3200
	mov	w1, w27
	add	x2, x20, 2798
.L1074:
	bl	_insert_data_list
	ldr	x0, [x20,72]
	ldrh	w0, [x0,x22,lsl 1]
	cmp	w0, 7
	bhi	.L1055
	mov	w0, w27
	mov	w1, 1
	mov	w2, w28
	b	.L1072
.L1059:
	cbnz	w0, .L1055
	ldr	x0, [x20,72]
	ldrh	w2, [x0,x22,lsl 1]
	cbz	w2, .L1060
	mov	x0, x26
	mov	w1, w19
	bl	printk
	ldrb	w0, [x27,2]
	tbz	x0, 4, .L1061
	mov	w1, 5
	b	.L1068
.L1061:
	mov	w1, 2
.L1068:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x27,2]
	mov	w1, 1
	mov	w0, w19
.L1072:
	bl	gc_add_sblk
	b	.L1055
.L1060:
	ands	w1, w1, 24
	add	x2, x21, :lo12:.LANCHOR0
	bne	.L1063
	add	x0, x2, 3240
	mov	w1, w19
	add	x2, x2, 2788
	b	.L1070
.L1063:
	cmp	w1, 16
	bne	.L1064
	add	x0, x2, 3248
	mov	w1, w19
	add	x2, x2, 2790
	b	.L1070
.L1064:
	add	x0, x2, 3256
	mov	w1, w19
	add	x2, x2, 2792
.L1070:
	bl	_insert_free_list
.L1055:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1046
.L1076:
	ldr	x0, [x1,2800]
	ldrh	w2, [x1,2788]
	strh	w2, [x0,114]
	ldrh	w2, [x1,2790]
	strh	w2, [x0,118]
	ldrh	w2, [x1,2792]
	strh	w2, [x0,116]
	ldrh	w2, [x1,2794]
	strh	w2, [x0,122]
	ldrh	w2, [x1,2798]
	ldrh	w1, [x1,2796]
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
	adrp	x20, .LC67
	adrp	x23, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	add	x25, x0, 16
	mov	x26, x23
	add	x20, x20, :lo12:.LC67
	mov	w24, -1
.L1079:
.L1082:
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
	ldrh	w1, [x0,3228]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w2, w1, w2
	strh	w2, [x19,6]
	strb	w21, [x19,4]
	beq	.L1080
	ldrb	w3, [x0,3196]
	mul	w3, w1, w3
	uxth	w3, w3
.L1080:
	add	x27, x26, :lo12:.LANCHOR0
	strh	w3, [x19,12]
	ubfiz	x28, x22, 1, 16
	ldrb	w2, [x27,3196]
	ldr	x0, [x27,3792]
	add	x0, x0, x3, uxth 2
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,72]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L1078
	mov	x0, x20
	mov	w1, w22
	bl	printk
	ldr	x0, [x27,72]
	strh	w24, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L1082
.L1078:
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
	stp	x29, x30, [sp, -352]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR4
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR4
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w0, [x19,1084]
	cmp	w0, 128
	bls	.L1085
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 72
	mov	w2, 74
	bl	printk
.L1086:
	b	.L1086
.L1085:
	add	x23, x29, 96
	mov	w1, 0
	mov	w2, 256
	mov	x0, x23
	adrp	x22, .LANCHOR0
	bl	ftl_memset
	add	x2, x22, :lo12:.LANCHOR0
	mov	w0, 21
	mov	w1, 1
	ldrh	w9, [x19,1084]
	mov	x3, 0
	ldr	x4, [x2,2800]
	ldrh	w6, [x2,3266]
	ldrb	w8, [x2,3264]
	sub	w0, w0, w6
	ldrh	w7, [x4,698]
	lsl	w0, w1, w0
	sub	w0, w0, #1
.L1087:
	cmp	w7, w3, uxth
	bls	.L1108
	add	x1, x4, x3, lsl 2
	mov	x2, 0
	ldr	w1, [x1,704]
	lsr	w1, w1, w6
	and	w1, w1, w0
	udiv	w1, w1, w8
	uxth	w1, w1
.L1088:
	cmp	w9, w2, uxth
	bls	.L1109
	add	x5, x4, x2, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L1089
	ldrh	w5, [x23,x2,lsl 1]
	add	w5, w5, 1
	strh	w5, [x23,x2,lsl 1]
.L1089:
	add	x2, x2, 1
	b	.L1088
.L1109:
	add	x3, x3, 1
	b	.L1087
.L1108:
	add	x0, x22, :lo12:.LANCHOR0
	mov	w25, 0
	mov	w19, w25
	mov	w26, 65535
	mov	w27, -1
	ldrb	w1, [x0,3196]
	ldrh	w20, [x0,3228]
	mul	w20, w1, w20
	uxth	w20, w20
.L1092:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,1084]
	cmp	w0, w19
	bls	.L1110
	add	x1, x22, :lo12:.LANCHOR0
	sxtw	x24, w19
	add	x0, x24, 208
	ldr	x2, [x1,2800]
	ldrb	w3, [x1,3264]
	ldrh	w0, [x2,x0,lsl 1]
	ldrh	w2, [x2,692]
	sdiv	w2, w2, w3
	cmp	w2, w0
	bne	.L1093
	ldrb	w2, [x1,3196]
	ldrh	w1, [x1,3228]
	mul	w1, w2, w1
	strh	w1, [x23,w19,sxtw 1]
.L1093:
	ldrh	w1, [x23,x24,lsl 1]
	cmp	w20, w1
	bls	.L1094
	cmp	w1, wzr
	csel	w25, w25, w19, eq
	csel	w20, w20, w1, eq
.L1094:
	cbnz	w1, .L1096
	cmp	w0, w26
	beq	.L1096
	bl	ftl_free_sblk
	add	x24, x24, 208
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	strh	w27, [x0,x24,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L1096:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1092
.L1110:
	mov	w0, w25
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 352
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
	stp	x29, x30, [sp, -32]!
	mov	w1, 0
	mov	w2, 2048
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,144]
	bl	ftl_memset
	ldr	x1, [x19,144]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,144]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1088
	bl	ftl_memcpy
	ldr	x0, [x19,144]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	mov	w2, 32
	add	x1, x1, 368
	add	x0, x0, 48
	bl	ftl_memcpy
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_into_data_init, .-flash_into_data_init
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L1114:
	cmp	w2, w3
	bls	.L1116
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L1114
.L1116:
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
.L1119:
	cmp	w1, w2
	bls	.L1121
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L1119
.L1121:
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
	adrp	x0, .LANCHOR4+1120
	ldr	x0, [x0,#:lo12:.LANCHOR4+1120]
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
	adrp	x0, .LANCHOR4+1128
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
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
	adrp	x1, .LANCHOR4+1128
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR4+1128]
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
	adrp	x3, .LANCHOR4+1128
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1128]
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
	adrp	x3, .LANCHOR4+1128
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1128]
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
	adrp	x4, .LANCHOR4+1136
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1136]
	add	x4, x4, 4096
	cbnz	w2, .L1128
	add	x1, x4, x1
	b	.L1130
.L1128:
	add	x0, x4, x1
	mov	x1, x6
.L1130:
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
	adrp	x0, .LC75
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC75
	bl	printk
	adrp	x0, .LANCHOR4+1128
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
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
	adrp	x0, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1128]
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
	adrp	x1, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1128]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1128]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	adrp	x4, .LANCHOR4+1128
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1128]
	ldr	x4, [x4,48]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR4+1128
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1128]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1128]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1128]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR4+1128
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1128]
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
	adrp	x23, .LC76
	adrp	x21, .LC77
	adrp	x24, .LC8
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC76
	add	x21, x21, :lo12:.LC77
	add	x24, x24, :lo12:.LC8
.L1145:
	cmp	x19, x26
	beq	.L1153
	cbnz	w20, .L1146
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L1146:
	cmp	w25, 4
	mov	x0, x21
	bne	.L1147
	ldr	w1, [x22,x19,lsl 2]
	b	.L1152
.L1147:
	cmp	w25, 2
	bne	.L1149
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L1152
.L1149:
	ldrb	w1, [x22,x19]
.L1152:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L1150
	adrp	x1, .LC78
	mov	x0, x24
	add	x1, x1, :lo12:.LC78
	mov	w20, 0
	bl	printk
.L1150:
	add	x19, x19, 1
	b	.L1145
.L1153:
	adrp	x0, .LC8
	adrp	x1, .LC78
	add	x1, x1, :lo12:.LC78
	add	x0, x0, :lo12:.LC8
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
	ldr	x27, [x0,144]
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
	bne	.L1155
	ldr	x1, [x29,136]
	mov	w0, -89
	strb	w0, [x1,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L1210
.L1155:
	cmp	w25, 3
	bne	.L1157
	mov	x4, 0
.L1158:
	sub	w0, w4, #80
	strb	w0, [x26,x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L1158
	mov	w21, w4
	mov	w28, w4
	b	.L1156
.L1157:
	cmp	w25, 4
	bne	.L1159
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
	b	.L1156
.L1159:
	cmp	w25, 5
	bne	.L1160
	mov	w0, 56
	strb	w0, [x27,128]
	mov	w0, 57
	strb	w0, [x27,129]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x27,130]
	mov	w0, 59
	strb	w0, [x27,131]
	b	.L1244
.L1160:
	cmp	w25, 6
	bne	.L1161
	mov	w0, 14
	strb	w0, [x27,128]
	mov	w0, 15
	strb	w0, [x27,129]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x27,130]
	mov	w0, 17
	strb	w0, [x27,131]
	b	.L1244
.L1161:
	cmp	w25, 7
	bne	.L1210
	mov	x0, 0
.L1162:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1162
	mov	w0, -44
	mov	w21, 12
	strb	w0, [x27,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x27,137]
	b	.L1156
.L1210:
	mov	w21, 7
.L1244:
	mov	w28, 4
.L1156:
	sub	w0, w25, #1
	cmp	w0, 1
	bhi	.L1241
	ldr	x0, [x29,128]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w27, 55
	add	x24, x24, 408
.L1163:
	ldrb	w0, [x19,152]
	cmp	w0, w23
	bls	.L1170
	add	x0, x19, x23, sxtw
	ldr	x2, [x29,136]
	ldrb	w0, [x0,156]
	mov	x20, 160
	ldr	x22, [x19,16]
	mov	x25, 0
	ubfiz	x1, x0, 8, 8
	madd	x20, x0, x20, x2
	add	x22, x22, x1
	add	x20, x20, 32
.L1165:
	str	w27, [x22,2056]
	ldrb	w0, [x26,x25]
	str	w0, [x22,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x22,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	cmp	w28, w25, uxtb
	bhi	.L1165
	mov	x0, 0
.L1166:
	add	w2, w0, 8
	mov	x1, 0
.L1167:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w5, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L1167
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1166
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L1163
.L1241:
	sub	w0, w25, #3
	cmp	w0, 4
	bhi	.L1170
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
.L1171:
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,152]
	cmp	w1, w20
	bhi	.L1209
.L1170:
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
.L1209:
	add	x1, x0, x20, sxtw
	ldrb	w1, [x1,156]
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
	bne	.L1172
	mov	w0, 64
	str	w1, [x23,2052]
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1245
.L1172:
	cmp	w19, 1
	bhi	.L1174
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1246
.L1174:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1245:
	str	w0, [x23,2052]
	mov	w0, 77
.L1246:
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
	bne	.L1175
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L1176
.L1175:
	str	wzr, [x23,2052]
.L1176:
	mov	w7, 2
	str	w7, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	str	x7, [x29,104]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w1, 16
	bls	.L1178
	ldr	x7, [x29,104]
	cmp	w25, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L1178:
	adrp	x0, .LANCHOR4
	mov	x7, 0
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1144]
.L1179:
	ldr	w8, [x23,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L1179
	cmp	w25, 7
	bne	.L1180
	mov	w1, 0
.L1182:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1181
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L1181
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L1182
	b	.L1183
.L1181:
	cmp	w1, 7
	bne	.L1184
.L1183:
	adrp	x0, .LC79
	mov	w1, 0
	add	x0, x0, :lo12:.LC79
	bl	printk
.L1185:
	b	.L1185
.L1180:
	cmp	w25, 6
	bne	.L1184
	mov	x1, 0
.L1186:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L1184
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L1184
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1186
	adrp	x0, .LC79
	mov	w1, 0
	add	x0, x0, :lo12:.LC79
	bl	printk
.L1188:
	b	.L1188
.L1184:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x9, [x0,1144]
	mov	x0, 0
.L1189:
	ldr	w1, [x29,120]
	cmp	w1, w0
	ble	.L1248
	ldr	w1, [x23,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L1189
.L1248:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w8, w22
	mov	w7, 8
	ldr	x11, [x0,1144]
.L1192:
	mov	w0, 0
.L1191:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L1191
	ldr	w0, [x29,124]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L1192
	mov	x7, 0
	mov	w15, 1
.L1193:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w10, w1
	ldr	x14, [x0,1144]
.L1197:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L1195:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L1195
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L1197
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L1193
	add	x1, x26, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w8, w7
	ldr	x1, [x1,1144]
.L1200:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L1200
	cmp	w8, 7
	ble	.L1201
	adrp	x0, .LC80
	mov	w2, 1
	add	x0, x0, :lo12:.LC80
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC79
	mov	w1, 0
	add	x0, x0, :lo12:.LC79
	bl	printk
.L1202:
	b	.L1202
.L1201:
	cmp	w25, 6
	mov	w7, 4
	beq	.L1203
	cmp	w25, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L1203:
	mov	w8, 0
.L1204:
	mov	x1, 0
.L1205:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x24,w10,sxtw]
	bhi	.L1205
	add	w8, w8, 1
	ldr	x1, [x29,112]
	cmp	w8, w21
	add	w0, w0, w7
	add	x9, x9, x1
	blt	.L1204
	mov	w24, 255
	str	w24, [x23,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w0, 56
	bhi	.L1247
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
.L1247:
	str	w0, [x23,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L1171
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	adrp	x0, .LANCHOR2+387
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR2+387]
	sub	w1, w0, #1
	uxtb	w1, w1
	cmp	w1, 6
	bhi	.L1249
	bl	hynix_get_read_retry_default
.L1249:
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	x0, [x20,16]
	bl	wait_for_nandc_xfer_completed
	ldr	x21, [x20,16]
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w0, 9
	bne	.L1252
	ldr	w0, [x21,16]
	str	w0, [x29,64]
	ldr	w20, [x21,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1272
	adrp	x22, .LC83
	adrp	x23, .LC82
	add	x22, x22, :lo12:.LC83
	add	x23, x23, :lo12:.LC82
	b	.L1254
.L1272:
	adrp	x22, .LC81
	adrp	x23, .LC82
	mov	w20, 0
	add	x22, x22, :lo12:.LC81
	add	x23, x23, :lo12:.LC82
.L1253:
	ldr	w2, [x21,64]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 6
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1260
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1255
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1255
.L1260:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 96
	ldr	w0, [x20,32]
	cbz	w0, .L1261
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
	b	.L1297
.L1255:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1253
	ldr	w2, [x21,64]
	mov	w1, w20
	ldr	w3, [x29,64]
	mov	x0, x22
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	mov	x0, x23
	mov	w2, 4
	mov	w3, 256
	ldr	x1, [x1,16]
	bl	rknand_print_hex
	b	.L1253
.L1263:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,16]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1254
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,64]
	mov	x0, x22
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x24,16]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
.L1254:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1263
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 96
	ldr	w0, [x20,32]
	cbz	w0, .L1261
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
	b	.L1296
.L1252:
	ldr	w0, [x21,8]
	str	w0, [x29,64]
	ldr	w20, [x21,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1273
	adrp	x22, .LC83
	adrp	x23, .LC82
	add	x22, x22, :lo12:.LC83
	add	x23, x23, :lo12:.LC82
	b	.L1265
.L1273:
	adrp	x22, .LC81
	adrp	x23, .LC82
	mov	w20, 0
	add	x22, x22, :lo12:.LC81
	add	x23, x23, :lo12:.LC82
.L1264:
	ldr	w2, [x21,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1267
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1266
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1266
	ldr	w1, [x29,72]
	adrp	x0, .LC84
	add	x0, x0, :lo12:.LC84
	bl	printk
	b	.L1267
.L1266:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1264
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
	mov	w3, 256
	ldr	x1, [x1,16]
	bl	rknand_print_hex
	b	.L1264
.L1267:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 96
	ldr	w0, [x20,32]
	cbz	w0, .L1261
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
.L1297:
	mov	w2, 0
	b	.L1295
.L1271:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,16]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1265
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x24,16]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
.L1265:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1271
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 96
	ldr	w0, [x20,32]
	cbz	w0, .L1261
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
.L1296:
	mov	w2, 1
.L1295:
	bl	rknand_dma_unmap_single
.L1261:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,128]
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
	uxtb	w21, w2
	uxtb	w19, w1
	mov	w0, w19
	mov	x2, x3
	mov	w1, w21
	mov	x22, x3
	mov	x3, x4
	str	x4, [x29,56]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	mov	w0, 0
	cbnz	w19, .L1299
	adrp	x20, .LANCHOR0
	ldr	x4, [x29,56]
	add	x3, x20, :lo12:.LANCHOR0
	ldrb	w1, [x20,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L1300
	ldr	x5, [x3,16]
	lsr	w21, w21, 2
	mov	w4, 1
	mov	w3, w0
.L1301:
	cmp	w3, w21
	bcs	.L1327
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
	tbnz	x1, 2, .L1318
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1318
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1303
	ubfx	x1, x1, 3, 7
	b	.L1304
.L1303:
	ubfx	x1, x1, 19, 7
.L1304:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1302
.L1318:
	mov	w0, -1
.L1302:
	add	w3, w3, 1
	b	.L1301
.L1327:
	add	x20, x20, :lo12:.LANCHOR0
	cmp	w4, wzr
	ldr	x1, [x20,16]
	ldr	w19, [x1]
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w19, w1
	cmp	w1, 139264
	bne	.L1299
	adrp	x0, .LC85
	mov	w1, w19
	add	x0, x0, :lo12:.LC85
	orr	w19, w19, 131072
	bl	printk
	ldr	x0, [x20,16]
	b	.L1326
.L1300:
	ldrb	w0, [x3,140]
	mov	w6, 128
	lsr	w7, w21, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w6, w0, w6, cc
	mov	w1, w19
	mov	w5, w19
.L1308:
	cmp	w5, w7
	add	w8, w1, w6
	bcs	.L1328
	ldr	x0, [x3,96]
	and	x1, x1, 4294967292
	ldr	w0, [x0,x1]
	ubfiz	x1, x5, 2, 30
	add	w5, w5, 1
	lsr	w9, w0, 16
	strb	w0, [x4,x1]
	lsr	w1, w0, 8
	strb	w1, [x4,w2,uxtw]
	add	w1, w2, 1
	lsr	w0, w0, 24
	strb	w9, [x4,x1]
	add	w1, w2, 2
	add	w2, w2, 4
	strb	w0, [x4,x1]
	mov	w1, w8
	b	.L1308
.L1328:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w5, 0
	lsr	w21, w21, 2
	ldr	x7, [x0,16]
	mov	w0, w5
.L1310:
	cmp	w5, w21
	bcs	.L1329
	uxtw	x1, w5
	add	x1, x1, 8
	ldr	w1, [x7,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1321
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1321
	ldr	w6, [x29,72]
	ubfx	x3, x6, 3, 5
	ldr	w6, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x6, x6, 27, 1
	ubfx	x2, x1, 16, 5
	ldr	w1, [x29,72]
	orr	w6, w3, w6, lsl 5
	ubfx	x1, x1, 29, 1
	orr	w1, w2, w1, lsl 5
	cmp	w6, w1
	ldr	w1, [x29,72]
	bls	.L1312
	ubfx	x2, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1325
.L1312:
	ubfx	x2, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1325:
	orr	w1, w2, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1311
.L1321:
	mov	w0, -1
.L1311:
	add	w5, w5, 1
	b	.L1310
.L1329:
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w0, #1
	ldr	x1, [x1,16]
	str	wzr, [x1,16]
	beq	.L1315
	ldr	w2, [x4]
	cmn	w2, #1
	bne	.L1315
	ldr	w2, [x4,4]
	cmn	w2, #1
	bne	.L1315
	ldr	w2, [x22]
	cmn	w2, #1
	mov	w2, 512
	csel	w0, w0, w2, ne
.L1315:
	ldr	w19, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w19, w1
	cmp	w1, 139264
	bne	.L1299
	adrp	x0, .LC86
	mov	w1, w19
	add	x0, x0, :lo12:.LC86
	orr	w19, w19, 131072
	bl	printk
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,16]
.L1326:
	str	w19, [x0]
	mov	w0, -1
.L1299:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	adrp	x0, .LANCHOR2+400
	mov	x25, x3
	mov	w27, w4
	and	w20, w1, 2097151
	ubfx	x24, x23, 24, 2
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1331
	adrp	x0, .LC87
	mov	w1, w21
	add	x0, x0, :lo12:.LC87
	mov	w2, w24
	mov	w3, w23
	bl	printk
.L1331:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	cbnz	w24, .L1332
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1333
.L1332:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,720]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1334
	sxtw	x0, w21
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w24, [x0,8]
	b	.L1333
.L1334:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1333:
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
	cbz	w24, .L1335
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,720]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1335
	add	w20, w20, w20, lsl 1
	sub	w0, w20, #1
	add	w0, w0, w24
	b	.L1346
.L1335:
	mov	w0, w20
.L1346:
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
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	uxtb	w25, w0
	mov	w24, w2
	ldrb	w21, [x19,140]
	mov	x26, x3
	mov	x27, x4
	mov	w28, w5
	uxtb	x20, w25
	bl	nandc_wait_flash_ready
	add	w21, w21, w21, lsl 1
	ldr	x6, [x19,16]
	asr	w21, w21, 2
	mov	w23, 0
	mov	w19, -1
	adrp	x8, .LANCHOR4
	lsl	x9, x20, 8
	mov	w10, 239
	mov	w11, 137
.L1348:
	add	x5, x8, :lo12:.LANCHOR4
	ldrb	w0, [x5,1160]
	cmp	w23, w0
	bcs	.L1351
	add	x22, x6, x9
	mov	w0, 200
	str	x8, [x29,104]
	str	x9, [x29,120]
	str	w10, [x22,2056]
	str	w11, [x22,2052]
	str	x6, [x29,128]
	str	x10, [x29,136]
	str	x11, [x29,144]
	str	x5, [x29,112]
	bl	timer_delay_ns
	add	w7, w23, 1
	str	w7, [x22,2048]
	str	wzr, [x22,2048]
	mov	w0, w25
	str	wzr, [x22,2048]
	mov	w1, w24
	str	wzr, [x22,2048]
	mov	x2, x26
	mov	x3, x27
	mov	w4, w28
	str	x7, [x29,152]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x7, [x29,152]
	ldr	x11, [x29,144]
	ldr	x10, [x29,136]
	ldr	x6, [x29,128]
	ldr	x9, [x29,120]
	ldr	x8, [x29,104]
	beq	.L1349
	cmn	w19, #1
	ldr	x5, [x29,112]
	csel	w19, w19, w0, ne
	cmp	w0, w21
	ldr	x26, [x5,1144]
	ldr	x27, [x5,1152]
	bcc	.L1356
.L1349:
	mov	w23, w7
	b	.L1348
.L1356:
	mov	w19, w0
.L1351:
	add	x20, x6, x20, lsl 8
	mov	w0, 239
	str	w0, [x20,2056]
	mov	w0, 137
	str	w0, [x20,2052]
	mov	w0, 200
	bl	timer_delay_ns
	str	wzr, [x20,2048]
	str	wzr, [x20,2048]
	cmp	w19, w21
	str	wzr, [x20,2048]
	str	wzr, [x20,2048]
	bcc	.L1353
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1353:
	cmn	w19, #1
	beq	.L1357
	cmp	w19, 256
	bne	.L1354
.L1357:
	adrp	x0, .LC88
	mov	w1, w23
	add	x0, x0, :lo12:.LC88
	mov	w2, w24
	mov	w3, w23
	mov	w4, w19
	bl	printk
.L1354:
	mov	w0, w19
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
	ldrb	w0, [x0,141]
	add	x24, x26, x22
	cmp	w0, 36
	csel	w1, w2, w1, ne
	cmp	w0, 36
	mov	w0, 26
	mov	w23, w1
	csel	w21, w21, w0, ne
	cbnz	w19, .L1368
	adrp	x0, .LC89
	str	x22, [x29,112]
	add	x0, x0, :lo12:.LC89
	mov	w19, -1
	mov	w23, 1
	str	x0, [x29,104]
	adrp	x22, .LANCHOR4
.L1375:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1369
	mov	w1, w23
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x26, x0
	str	w1, [x0,8]
	b	.L1370
.L1369:
	mov	w1, w23
	bl	toshiba_3d_set_slc_rr_para
.L1370:
	ldr	w4, [x29,124]
	mov	w1, w25
	ldrb	w0, [x29,128]
	mov	x2, x27
	mov	x3, x28
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 4, .L1371
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w23
	mov	w2, w25
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1371:
	cmn	w4, #1
	beq	.L1372
	add	x0, x22, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1144]
	ldr	x28, [x0,1152]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,140]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1391
.L1372:
	add	w23, w23, 1
	cmp	w23, w21
	bne	.L1375
	b	.L1374
.L1391:
	mov	w21, w23
	mov	w19, w4
.L1374:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1376
	mov	w1, 0
	mov	w2, w1
	b	.L1414
.L1376:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1377
.L1368:
	adrp	x0, .LC90
	orr	w25, w25, w19, lsl 24
	add	x0, x0, :lo12:.LC90
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,112]
.L1384:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1378
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x26, x22
	mov	w1, 93
	b	.L1413
.L1378:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x26, x22
	mov	w1, 38
.L1413:
	str	w1, [x0,8]
	mov	x2, x27
	mov	w1, w25
	mov	x3, x28
	ldr	w4, [x29,124]
	ldrb	w0, [x29,128]
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 4, .L1380
	ldr	x0, [x29,112]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w25
	str	x4, [x29,104]
	bl	printk
	ldr	x4, [x29,104]
.L1380:
	cmn	w4, #1
	beq	.L1381
	adrp	x0, .LANCHOR4
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1144]
	ldr	x28, [x0,1152]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,140]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1392
.L1381:
	add	w21, w21, 1
	cmp	w21, w23
	bne	.L1384
	b	.L1383
.L1392:
	mov	w19, w4
.L1383:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1385
	mov	w1, 0
	mov	w2, 1
.L1414:
	bl	toshiba_tlc_set_rr_para
	b	.L1377
.L1385:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1377:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 36
	bne	.L1386
	ldr	x0, [x29,128]
	ubfiz	x23, x0, 8, 8
	mov	w0, 85
	add	x26, x26, x23
	str	w0, [x26,2056]
	mov	w0, 255
	str	wzr, [x26,2052]
	str	wzr, [x26,2048]
	str	w0, [x26,2056]
.L1386:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,140]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1387
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1387:
	cmn	w19, #1
	beq	.L1393
	cmp	w19, 256
	bne	.L1388
.L1393:
	adrp	x0, .LC91
	mov	w1, w21
	add	x0, x0, :lo12:.LC91
	mov	w2, w25
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1388:
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
	ldrb	w0, [x1,141]
	ldr	x22, [x1,16]
	sub	w0, w0, #67
	add	x21, x22, x21, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1416
	ldrb	w0, [x1,716]
	cbz	w0, .L1417
	mov	w0, 1
	mov	w24, 1
	bl	nandc_set_if_mode
.L1417:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1416:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1418:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1160]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1449
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,141]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1419
	bl	sandisk_set_rr_para
	b	.L1420
.L1419:
	bl	toshiba_set_rr_para
.L1420:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	cmp	w0, 34
	bne	.L1421
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1160]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1421
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1421:
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
	beq	.L1424
	cmn	w25, #1
	csel	w25, w25, w0, ne
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x27, [x0,1144]
	ldr	x28, [x0,1152]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,140]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1426
.L1424:
	add	w20, w20, 1
	b	.L1418
.L1449:
	mov	w26, w25
.L1426:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,141]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1428
	bl	sandisk_set_rr_para
	b	.L1429
.L1428:
	bl	toshiba_set_rr_para
.L1429:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,140]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1430
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L1430:
	cmn	w26, #1
	beq	.L1436
	cmp	w26, 256
	bne	.L1431
.L1436:
	adrp	x0, .LC91
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC91
	mov	w1, w20
	mov	w3, w20
	mov	w4, w26
	bl	printk
.L1431:
	bl	nandc_wait_flash_ready
	cbz	w24, .L1433
	mov	w0, 4
	bl	nandc_set_if_mode
.L1433:
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
	ldr	x5, [x0,144]
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
.L1451:
	cmp	w6, w24
	bcs	.L1455
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
	beq	.L1453
	add	x1, x7, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1144]
	ldr	x26, [x1,1152]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,140]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1460
.L1453:
	add	w6, w6, 1
	b	.L1451
.L1460:
	mov	w19, w0
.L1455:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,140]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1457
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1457:
	cmn	w19, #1
	beq	.L1461
	cmp	w19, 256
	bne	.L1458
.L1461:
	adrp	x0, .LC92
	mov	w1, w6
	add	x0, x0, :lo12:.LC92
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1458:
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
	adrp	x0, .LC93
	mov	w22, w26
	add	x0, x0, :lo12:.LC93
	mov	w28, w26
	mov	w27, w26
	mov	w25, 1024
	mov	w21, 6
	mov	w19, -1
	str	x0, [x29,104]
.L1476:
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
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 4, .L1471
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w20
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1471:
	add	w0, w25, 1
	cmp	w4, w0
	bhi	.L1472
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x23, [x0,1144]
	ldr	x24, [x0,1152]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,140]
	cmp	w4, w0, lsr 2
	bcs	.L1481
	add	w27, w27, 1
	cmp	w27, 7
	bls	.L1481
	sub	w26, w21, w27
	mov	w25, w4
	mov	w19, 0
	b	.L1474
.L1472:
	cmp	w28, w27
	bcs	.L1482
	cmp	w27, 7
	sub	w22, w26, w27
	bhi	.L1475
	mov	w28, w27
	b	.L1482
.L1481:
	mov	w26, w21
	mov	w25, w4
	mov	w19, 0
	b	.L1473
.L1482:
	mov	w27, 0
.L1473:
	add	w21, w21, 2
	cmp	w21, 50
	bne	.L1476
.L1474:
	cmp	w28, w27
	csel	w26, w26, w22, ls
.L1475:
	cbz	w26, .L1477
	adrp	x0, .LANCHOR0+140
	mov	w1, 3
	ldrb	w0, [x0,#:lo12:.LANCHOR0+140]
	udiv	w0, w0, w1
	cmp	w25, w0
	bcs	.L1477
	adrp	x0, .LC94
	mov	w1, w26
	add	x0, x0, :lo12:.LC94
	bl	printk
	mov	w0, w26
	b	.L1490
.L1477:
	ldrb	w0, [x29,120]
.L1490:
	bl	nandc_set_ddr_para
	cbz	w19, .L1479
	adrp	x0, .LC95
	ldr	w1, [x29,124]
	add	x0, x0, :lo12:.LC95
	mov	w2, w20
	mov	w25, w19
	bl	printk
.L1479:
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
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	w19, w1
	add	x1, x20, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x25, x2
	mov	x24, x3
	ldrb	w2, [x1,152]
	mov	w23, w4
	ubfx	x21, x19, 24, 2
	cmp	w2, w0
	bhi	.L1492
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 88
	mov	w2, 376
	bl	printk
.L1493:
	b	.L1493
.L1492:
	add	x0, x1, x0, sxtw
	ldrb	w22, [x0,156]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1494
	adrp	x0, .LC96
	mov	w1, w22
	add	x0, x0, :lo12:.LC96
	mov	w2, w19
	bl	printk
.L1494:
	cbnz	w21, .L1495
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,164]
	cbz	w1, .L1496
	ldrb	w0, [x0,165]
	cbz	w0, .L1495
.L1496:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,166]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,165]
	sub	w19, w19, w0
	cbz	w2, .L1497
	add	w19, w0, w19, lsl 1
	b	.L1495
.L1497:
	add	x1, x1, 168
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1495:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w21, w0
	bne	.L1504
	add	x26, x20, :lo12:.LANCHOR0
	ldrb	w27, [x26,136]
	cbnz	w27, .L1499
.L1502:
	adrp	x0, .LANCHOR4+1168
	ldr	x6, [x0,#:lo12:.LANCHOR4+1168]
	cbnz	x6, .L1500
	b	.L1501
.L1499:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	strb	wzr, [x26,136]
	bl	flash_read_page
	strb	w27, [x26,136]
	cmn	w0, #1
	beq	.L1502
	b	.L1520
.L1500:
	mov	w0, w22
	mov	w1, 0
	mov	w2, w19
	mov	x3, x25
	mov	x4, x24
	mov	w5, w23
	blr	x6
	cmn	w0, #1
	bne	.L1520
.L1501:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,136]
	bl	printk
	ldrb	w0, [x20,716]
	cbz	w0, .L1504
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1520:
	mov	w21, w0
.L1504:
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
	stp	x29, x30, [sp, -96]!
	uxth	w1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR4+1012
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	ldrh	w19, [x0,#:lo12:.LANCHOR4+1012]
	adrp	x0, .LANCHOR2+394
	stp	x23, x24, [sp,48]
	sub	w19, w19, #1
	str	x27, [sp,80]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+394]
	mov	x22, x2
	sxth	w19, w19
	mov	x23, x3
	mov	w24, w4
	mul	w25, w1, w0
	mov	w0, w21
	add	w1, w25, w19
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L1522
	mov	w26, 0
	mov	w27, 2
.L1523:
	cmp	w26, w19
	bgt	.L1522
	add	w0, w26, w19
	mov	x2, x22
	mov	x3, x23
	mov	w4, w24
	sdiv	w20, w0, w27
	mov	w0, w21
	add	w1, w25, w20, sxth
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L1524
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1523
.L1524:
	add	w20, w20, 1
	sxth	w26, w20
	b	.L1523
.L1522:
	mov	w0, w19
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	uxth	w6, w0
	adrp	x0, .LANCHOR0+3266
	stp	x29, x30, [sp, -16]!
	mov	w5, 21
	mov	x8, x1
	add	x29, sp, 0
	ldrh	w0, [x0,#:lo12:.LANCHOR0+3266]
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	mov	w22, 4
	stp	x19, x20, [sp,16]
	adrp	x24, .LANCHOR0
	adrp	x25, .LANCHOR4
.L1531:
	add	x19, x24, :lo12:.LANCHOR0
	add	x21, x25, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w4, 4
	adrp	x20, .LANCHOR0
	adrp	x23, .LANCHOR4
	ldrh	w1, [x19,166]
	ldr	x2, [x19,144]
	ldr	x3, [x21,1176]
	mul	w1, w22, w1
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1529
	ldr	x2, [x19,144]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L1530
.L1529:
	add	w22, w22, 1
	cmp	w22, 16
	bne	.L1531
.L1552:
	mov	w0, -1
	b	.L1551
.L1530:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,166]
	strb	w1, [x21,1184]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,1176]
	strb	w0, [x21,1185]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1533
	ldr	x0, [x19,144]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1533
	ldr	w21, [x0,4]
.L1533:
	add	x19, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,1185]
	ldrh	w1, [x22,166]
	ldr	x3, [x19,1176]
	mul	w1, w2, w1
	ldr	x2, [x22,144]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1534
	ldr	x0, [x22,144]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1534
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L1534
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,1184]
	strb	w0, [x19,1185]
.L1534:
	add	x21, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x21,1184]
	ldr	x3, [x21,1176]
	ldr	x2, [x22,144]
	bl	flash_get_last_written_page
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x21,1186]
	ldrb	w0, [x21,1184]
	ldrh	w21, [x22,166]
	mul	w21, w0, w21
.L1535:
	tbnz	w19, #31, .L1553
	add	x22, x20, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w0, 0
	add	w1, w19, w21
	mov	w4, 4
	ldr	x2, [x22,144]
	ldr	x3, [x3,1176]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1536
	ldr	x0, [x22,144]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	bne	.L1536
.L1538:
	mov	w0, 0
	b	.L1551
.L1536:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1535
.L1553:
	cmn	w19, #1
	bne	.L1538
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	ldr	x1, [x20,144]
	ldr	w1, [x1]
	bl	printk
	b	.L1552
.L1551:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
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
	ldrb	w0, [x21,681]
	add	x20, x20, :lo12:.LANCHOR4
	strb	w22, [x21,716]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,681]
	bl	nandc_set_if_mode
	ldr	x2, [x20,1192]
	mov	w1, w23
	ldr	x3, [x20,1200]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tunning_read
	ldr	x2, [x20,1192]
	mov	w0, w24
	ldr	x3, [x20,1200]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1555
	ldrb	w0, [x21,681]
	tbz	x0, 0, .L1555
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,716]
	b	.L1556
.L1555:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,716]
.L1556:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.type	id_block_read_data.constprop.38, %function
id_block_read_data.constprop.38:
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
	ldr	x0, [x19,720]
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
	ldrb	w2, [x19,136]
	str	w2, [x29,136]
	uxth	w0, w0
	strb	wzr, [x19,136]
	udiv	w2, w26, w0
	msub	w0, w2, w0, w26
	str	w0, [x29,172]
	sub	w0, w26, w0
	str	w0, [x29,160]
	ldr	w0, [x29,172]
	and	w0, w0, 3
	str	w0, [x29,168]
	adrp	x0, .LC99
	add	x0, x0, :lo12:.LC99
	str	x0, [x29,112]
.L1561:
	cmp	w22, w27
	bcs	.L1598
	ldr	w0, [x29,168]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	sub	w0, w23, w0
	uxth	w0, w0
	str	w0, [x29,144]
	ldr	w0, [x29,172]
	ldrb	w1, [x1,1208]
	add	w20, w22, w0
	add	x0, x19, 168
	udiv	w20, w20, w23
	and	w20, w20, 65535
	ldrh	w0, [x0,w20,sxtw 1]
	cbnz	w1, .L1562
	mov	w20, w0
	b	.L1563
.L1562:
	ldrb	w1, [x19,165]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L1563:
	ldr	w1, [x29,160]
	ldr	w0, [x29,164]
	madd	w0, w20, w0, w1
	str	w0, [x29,140]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x29,168]
	ldrb	w28, [x0,1209]
	ldr	w0, [x29,140]
	add	w24, w0, w1
	ldr	x0, [x19,720]
	ldrb	w0, [x0,9]
	udiv	w24, w24, w0
	ldrb	w0, [x19,140]
	str	w0, [x29,132]
	mov	w0, w28
	bl	nandc_bch_sel
.L1564:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L1571
	ldrb	w5, [x19,136]
	cbnz	w5, .L1566
.L1569:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x7, [x0,1168]
	cbnz	x7, .L1567
	b	.L1568
.L1566:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	str	x5, [x29,104]
	strb	wzr, [x19,136]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x5, [x29,104]
	strb	w5, [x19,136]
	beq	.L1569
	b	.L1597
.L1567:
	mov	w0, 0
	mov	w2, w24
	mov	w1, w0
	mov	x3, x21
	add	x4, x29, 176
	mov	w5, w23
	blr	x7
	cmn	w0, #1
	bne	.L1597
.L1568:
	ldrb	w0, [x19,716]
	cbz	w0, .L1571
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1597:
	mov	w20, w0
.L1571:
	cmn	w20, #1
	bne	.L1572
	cmp	w28, 16
	beq	.L1572
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L1564
.L1572:
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
	beq	.L1574
	cbnz	w25, .L1574
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L1574
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L1575
.L1574:
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
.L1575:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L1561
.L1598:
	ldr	x0, [x29,120]
	ldrb	w1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,136]
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	id_block_read_data.constprop.38, .-id_block_read_data.constprop.38
	.align	2
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	and	w19, w0, 2097151
	stp	x21, x22, [sp,32]
	add	x21, x20, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	w24, w0
	mov	x25, x1
	ldrb	w1, [x21,152]
	ubfx	x0, x24, 21, 3
	mov	x26, x2
	ubfx	x22, x24, 24, 2
	cmp	w1, w0
	bhi	.L1600
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 112
	mov	w2, 789
	bl	printk
.L1601:
	b	.L1601
.L1600:
	add	x0, x21, x0, sxtw
	ldrb	w23, [x0,156]
	ldr	x27, [x21,16]
	mov	w0, w23
	bl	nandc_cs
	cbnz	w22, .L1602
	ldrb	w0, [x21,164]
	cbz	w0, .L1603
	ldrb	w0, [x21,165]
	cbz	w0, .L1602
.L1603:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,166]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,165]
	sub	w19, w19, w0
	cbz	w2, .L1604
	add	w19, w0, w19, lsl 1
	b	.L1602
.L1604:
	add	x1, x1, 168
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1602:
	ubfiz	x0, x23, 8, 8
	adrp	x21, .LANCHOR2
	add	x5, x27, x0
	mov	w0, 5
	add	x27, x21, :lo12:.LANCHOR2
	mov	w1, 0
	mov	x3, x25
	mov	x4, x26
	str	w0, [x5,2056]
	and	w0, w19, 255
	str	wzr, [x5,2052]
	add	x27, x27, 368
	str	wzr, [x5,2052]
	str	w0, [x5,2052]
	lsr	w0, w19, 8
	str	w0, [x5,2052]
	lsr	w0, w19, 16
	ldrb	w2, [x27,9]
	str	w0, [x5,2052]
	mov	w0, 224
	str	w0, [x5,2056]
	mov	w0, w23
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L1605
	add	x28, x20, :lo12:.LANCHOR0
	ldrb	w5, [x28,136]
	cbz	w5, .L1606
	ldrb	w4, [x27,9]
	mov	w0, w23
	orr	w1, w19, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	str	x5, [x29,104]
	strb	wzr, [x28,136]
	bl	flash_read_page
	ldr	x5, [x29,104]
	strb	w5, [x28,136]
	cbnz	w22, .L1607
.L1614:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1,164]
	cbz	w2, .L1607
	ldrb	w1, [x1,140]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L1607
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w1, [x21,387]
	sub	w1, w1, #4
	uxtb	w1, w1
	cmp	w1, 3
	mov	w1, 256
	csel	w0, w0, w1, hi
	b	.L1622
.L1607:
	cmn	w0, #1
	bne	.L1622
.L1615:
	adrp	x0, .LANCHOR4+1168
	ldr	x6, [x0,#:lo12:.LANCHOR4+1168]
	cbnz	x6, .L1610
.L1613:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	mov	w1, 0
	mov	w2, w24
	mov	w3, -1
	ldrb	w4, [x20,136]
	bl	printk
	ldrb	w0, [x20,716]
	cbnz	w0, .L1611
	mov	w0, -1
	b	.L1622
.L1610:
	add	x5, x21, :lo12:.LANCHOR2
	mov	w0, w23
	mov	w1, w22
	mov	w2, w19
	mov	x3, x25
	mov	x4, x26
	ldrb	w5, [x5,377]
	blr	x6
	cmn	w0, #1
	bne	.L1622
	b	.L1613
.L1611:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w23
	orr	w1, w19, w22, lsl 24
	mov	x2, x25
	mov	x3, x26
	ldrb	w4, [x21,377]
	bl	flash_ddr_tunning_read
	b	.L1622
.L1605:
	cbz	w22, .L1614
	b	.L1622
.L1606:
	cbz	w22, .L1614
	b	.L1615
.L1622:
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
	ldrb	w20, [x0,3814]
	cmp	w20, 255
	beq	.L1671
	add	x0, x0, 728
	mov	w1, 0
	add	x0, x0, x20, lsl 6
	ldrb	w2, [x0,58]
	ldr	w21, [x0,40]
	sub	w3, w2, #1
	cmp	w3, 9
	bhi	.L1637
	adrp	x0, .L1639
	add	x0, x0, :lo12:.L1639
	ldrb	w0, [x0,w3,uxtw]
	adr	x1, .Lrtx1639
	add	x0, x1, w0, sxtb #2
	br	x0
.Lrtx1639:
	.section	.rodata
	.align	0
	.align	2
.L1639:
	.byte	(.L1638 - .Lrtx1639) / 4
	.byte	(.L1638 - .Lrtx1639) / 4
	.byte	(.L1640 - .Lrtx1639) / 4
	.byte	(.L1640 - .Lrtx1639) / 4
	.byte	(.L1640 - .Lrtx1639) / 4
	.byte	(.L1641 - .Lrtx1639) / 4
	.byte	(.L1642 - .Lrtx1639) / 4
	.byte	(.L1643 - .Lrtx1639) / 4
	.byte	(.L1640 - .Lrtx1639) / 4
	.byte	(.L1643 - .Lrtx1639) / 4
	.text
.L1638:
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1671
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w21
	add	x19, x19, 728
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
	b	.L1671
.L1640:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L1671
	mov	w1, 5
	add	x19, x19, :lo12:.LANCHOR0
	ands	w1, w21, w1
	beq	.L1646
	add	x19, x19, 728
	mov	w0, 11
	add	x20, x19, x20, lsl 6
	mov	w4, 11
	strb	w0, [x20,58]
	adrp	x0, .LC101
	ldrb	w1, [x20,1]
	add	x0, x0, :lo12:.LC101
	ldr	w2, [x20,52]
	ldr	w3, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
	b	.L1672
.L1646:
	add	x20, x19, x20, lsl 6
	b	.L1653
.L1643:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 3
	cmp	w2, 10
	mov	w1, 9
	add	x22, x22, 728
	csel	w1, w0, w1, ne
	ubfx	x3, x21, 21, 3
	add	x0, x22, x20, lsl 6
.L1648:
	ldrb	w23, [x0]
	cmp	w23, 255
	beq	.L1673
	add	x0, x22, x23, lsl 6
	ldrb	w2, [x0,58]
	cmp	w2, w1
	bne	.L1648
	ldr	w2, [x0,40]
	ubfx	x2, x2, 21, 3
	cmp	w3, w2
	bne	.L1648
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	and	w0, w0, 64
	tbz	x21, 6, .L1656
	ands	w0, w21, 15
	add	x22, x22, x20, lsl 6
	beq	.L1651
	adrp	x0, .LC101
	ldrb	w1, [x22,1]
	ldr	w2, [x22,52]
	add	x0, x0, :lo12:.LC101
	ldr	w3, [x22,40]
	mov	w4, 11
	bl	printk
	mov	w0, 11
	strb	w0, [x22,58]
	mov	w0, -1
	str	w0, [x22,52]
.L1650:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 728
	add	x20, x19, x20, lsl 6
	add	x23, x19, x23, lsl 6
	ldrb	w0, [x20,58]
	strb	w0, [x23,58]
	ldr	w0, [x20,52]
	str	w0, [x23,52]
.L1672:
	mov	w1, w21
	b	.L1637
.L1651:
	mov	w1, 12
	str	w0, [x22,52]
	strb	w1, [x22,58]
.L1656:
	mov	w21, w0
	b	.L1650
.L1673:
	mov	w1, -1
	b	.L1637
.L1641:
	mov	w1, 32
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 5, .L1671
	ands	w1, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	beq	.L1653
	mov	w1, 11
	strb	w1, [x20,786]
	mov	w1, -1
	str	w1, [x20,780]
	mov	w1, w0
	b	.L1637
.L1653:
	mov	w0, 12
	str	w1, [x20,780]
	strb	w0, [x20,786]
	b	.L1637
.L1642:
	mov	w1, 64
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1671
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	str	w0, [x20,780]
	mov	w0, 6
	strb	w0, [x20,786]
.L1671:
	mov	w1, 0
.L1637:
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
.L1675:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3814]
	cmp	w0, 255
	beq	.L1677
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1675
.L1677:
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
	add	x21, x21, 1216
	bl	ftl_get_blk_list_in_sblk
	add	x22, x22, 192
	strh	w19, [x29,64]
	strb	w0, [x29,73]
	mov	w19, 0
.L1681:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x23, x0
	ldrh	w1, [x1,3228]
	cmp	w1, w19
	bls	.L1680
	mov	w20, 0
.L1687:
	add	x3, x23, :lo12:.LANCHOR0
	ldrb	w0, [x3,3196]
	cmp	w0, w20
	bls	.L1693
	add	x0, x29, 64
	add	x0, x0, x20, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1683
	ldrh	w1, [x3,3270]
	mov	x2, x22
	ldrb	w3, [x3,2776]
	madd	w0, w0, w1, w19
	mov	x1, x21
	bl	ftl_read_ppa_page
	cmp	w0, 512
	beq	.L1680
.L1683:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1687
.L1693:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1681
.L1680:
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
	add	x26, x23, 3814
.L1695:
	cbz	w22, .L1707
	ldrb	w27, [x19]
	ldr	w28, [x19,40]
.L1696:
	mov	w0, w28
	mov	w1, 0
	bl	queue_lun_state
	mov	w2, w0
	cbz	w0, .L1708
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1696
.L1708:
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
	beq	.L1707
	add	x19, x23, 728
	sbfiz	x27, x27, 6, 32
	add	x19, x19, x27
	b	.L1695
.L1707:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 728
.L1700:
	cbz	w21, .L1709
	ldrb	w0, [x20,58]
	cmp	w0, 12
	bne	.L1701
	ldrb	w20, [x20]
	sub	w21, w21, #1
	add	x20, x19, x20, lsl 6
	b	.L1700
.L1701:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1700
.L1709:
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
	ldr	x0, [x20,2848]
	ldr	x23, [x20,80]
	add	x20, x20, 2840
	cbnz	x0, .L1711
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L1711:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w24, 0
	ldr	x20, [x21,2848]
	cbnz	x20, .L1712
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 144
	mov	w2, 348
	bl	printk
.L1713:
	b	.L1713
.L1712:
	add	x25, x23, 80
	adrp	x26, .LANCHOR4
.L1734:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L1736
	mov	w22, 1
	sxtw	x27, w24
.L1722:
	add	x0, x21, 2840
	ldrh	w1, [x0,20]
	cmp	w22, w1
	bgt	.L1737
	add	x1, x27, 8
	ldrh	w2, [x21,3270]
	ldrh	w0, [x0,16]
	ldrh	w1, [x25,x1,lsl 1]
	mul	w1, w1, w2
	ldrb	w2, [x21,3177]
	cmp	w2, 3
	bne	.L1714
	add	w1, w1, w0
	orr	w1, w1, w22, lsl 24
	b	.L1735
.L1714:
	cmp	w2, 2
	bne	.L1716
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L1735
.L1716:
	add	w1, w1, w0
.L1735:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L1717
	add	x3, x26, :lo12:.LANCHOR4
	ldrh	w0, [x21,2862]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,1056]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L1717
	ldr	x3, [x3,1064]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L1718
.L1717:
	add	x0, x26, :lo12:.LANCHOR4
	ldrh	w4, [x21,2862]
	lsl	x4, x4, 2
	ldr	x1, [x0,1056]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L1718
	adrp	x1, .LANCHOR2+400
	ldr	w1, [x1,#:lo12:.LANCHOR2+400]
	tbz	x1, 10, .L1719
	ldr	x7, [x20,24]
	ldr	x5, [x0,1064]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC102
	add	x0, x0, :lo12:.LC102
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L1719:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23,80]
	ldr	x0, [x19,72]
	strh	wzr, [x0,x1,lsl 1]
	mov	w0, -1
	b	.L1720
.L1718:
	ldrh	w0, [x21,2862]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2862]
	b	.L1722
.L1737:
	add	w24, w24, 1
	b	.L1734
.L1736:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2856]
	add	w0, w0, 1
	strh	w0, [x19,2856]
	mov	w0, 0
.L1720:
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
	adrp	x0, .LANCHOR0+16
	adrp	x21, .LANCHOR2
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR0+16]
	add	x0, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x20, x22
	add	x22, x22, 8
	ldr	w0, [x0,400]
	mov	w23, w1
	mov	x24, x3
	mov	x25, x2
	mov	w3, w4
	add	x22, x19, x22, lsl 8
	and	w26, w1, 2097151
	ubfx	x27, x23, 24, 2
	tbz	x0, 4, .L1739
	adrp	x0, .LC103
	mov	w2, w27
	add	x0, x0, :lo12:.LC103
	bl	printk
.L1739:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	hynix_reconfig_rr_para
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w27, .L1740
	bl	zftl_flash_enter_slc_mode
	b	.L1741
.L1740:
	bl	zftl_flash_exit_slc_mode
.L1741:
	ubfiz	x4, x20, 8, 8
	mov	w0, 128
	add	x19, x19, x4
	and	w1, w23, 255
	add	x21, x21, :lo12:.LANCHOR2
	str	w0, [x19,2056]
	lsr	w0, w26, 8
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w1, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w26, 16
	str	w0, [x19,2052]
	mov	w0, w26
	bl	nandc_set_seed
	ldrb	w1, [x21,377]
	mov	x2, x25
	mov	x3, x24
	mov	w0, 1
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	mov	w0, 16
	str	w0, [x19,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w19, w0
	mov	w0, w20
	bl	nandc_de_cs
	ldr	x27, [sp,80]
	and	w0, w19, 4
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
	cbnz	w22, .L1746
	adrp	x0, .LANCHOR5+448
	ldrb	w1, [x0,#:lo12:.LANCHOR5+448]
	mov	w0, w22
	cmp	w1, w20
	bhi	.L1747
.L1746:
	adrp	x19, .LANCHOR4
	mov	w1, 165
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 32
	ldr	x0, [x19,1200]
	bl	ftl_memset
	ldr	x0, [x19,1192]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	adrp	x0, .LANCHOR0+166
	ldrh	w21, [x0,#:lo12:.LANCHOR0+166]
	mov	w0, w22
	mul	w21, w20, w21
	mov	w1, w21
	bl	flash_erase_block
	cmn	w0, #1
	beq	.L1752
	adrp	x20, .LANCHOR2
	ldr	x2, [x19,1192]
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x3, [x19,1200]
	mov	w0, w22
	mov	w1, w21
	add	x20, x20, 368
	ldrb	w4, [x20,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L1752
	ldrb	w4, [x20,9]
	mov	w0, w22
	ldr	x2, [x19,1192]
	mov	w1, w21
	ldr	x3, [x19,1200]
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	beq	.L1752
	ldr	x0, [x19,1192]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L1752
	ldr	x0, [x19,1200]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csinv	w20, w20, wzr, eq
	b	.L1749
.L1752:
	mov	w20, -1
.L1749:
	mov	w0, w22
	mov	w1, w21
	bl	flash_erase_block
	mov	w0, w20
.L1747:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	fw_flash_page_prog.constprop.39, %function
fw_flash_page_prog.constprop.39:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x4, x4, :lo12:.LANCHOR0
	str	x21, [sp,32]
	mov	x21, x1
	stp	x19, x20, [sp,16]
	str	x2, [x29,56]
	ldr	x1, [x4,720]
	ldrb	w20, [x4,140]
	ldrb	w19, [x1,9]
	udiv	w19, w0, w19
	adrp	x0, .LANCHOR4+1209
	ldrb	w0, [x0,#:lo12:.LANCHOR4+1209]
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
	.size	fw_flash_page_prog.constprop.39, .-fw_flash_page_prog.constprop.39
	.text
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	stp	x29, x30, [sp, -96]!
	adrp	x7, .LANCHOR0
	uxtb	w3, w3
	add	x29, sp, 0
	add	x7, x7, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	uxtb	w25, w0
	uxtb	w24, w1
	uxtb	w20, w2
	ldrb	w0, [x7,152]
	mov	x22, x5
	mov	x21, x6
	cmp	w0, w3
	bhi	.L1762
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 168
	mov	w2, 648
	bl	printk
.L1763:
	b	.L1763
.L1762:
	add	x3, x7, x3, sxtw
	ldrb	w23, [x3,156]
	str	x4, [x29,88]
	mov	w0, w23
	ldr	x19, [x7,16]
	bl	nandc_cs
	ldr	x4, [x29,88]
	cbz	w25, .L1764
	sxtw	x7, w23
	add	x7, x7, 8
	add	x7, x19, x7, lsl 8
	str	w25, [x7,8]
.L1764:
	ubfiz	x7, x23, 8, 8
	mov	w0, 128
	add	x19, x19, x7
	str	w24, [x19,2056]
	str	w0, [x19,2056]
	and	w0, w4, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w4, 8
	str	w0, [x19,2052]
	lsr	w0, w4, 16
	add	w4, w4, w4, lsl 1
	str	w0, [x19,2052]
	sub	w0, w4, #1
	add	w0, w0, w24
	bl	nandc_set_seed
	adrp	x1, .LANCHOR2+377
	mov	x2, x22
	mov	x3, x21
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+377]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w20, [x19,2056]
	mov	w0, w23
	bl	nandc_de_cs
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
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
	add	x0, x0, 3814
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
.L1770:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L1772
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1770
.L1772:
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
.L1774:
	mov	w0, w20
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L1778
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1774
.L1778:
	cmp	w24, 2
	bne	.L1776
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
	add	x0, x0, 3814
	bl	buf_add_tail
	b	.L1777
.L1776:
	mov	x0, x19
	bl	queue_tlc_prog_cmd
.L1777:
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
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	ubfx	x27, x1, 21, 3
	and	w19, w1, 2097151
	ubfx	x28, x1, 24, 2
	adrp	x1, .LANCHOR0
	stp	x25, x26, [sp,64]
	add	x26, x1, :lo12:.LANCHOR0
	mov	x25, x2
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	mov	x24, x3
	ldrb	w0, [x26,152]
	stp	x21, x22, [sp,32]
	cmp	w0, w27
	mov	x21, x1
	bhi	.L1780
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 200
	mov	w2, 685
	bl	printk
.L1781:
	b	.L1781
.L1780:
	add	x0, x26, x27, sxtw
	ldr	x20, [x26,16]
	ldrb	w22, [x0,156]
	bl	nandc_rdy_status
	cbnz	w0, .L1782
	ldrb	w0, [x26,152]
	cmp	w0, 1
	bne	.L1783
	bl	nandc_wait_flash_ready
	b	.L1782
.L1783:
	mov	w0, w27
	mov	w1, w19
	mov	w2, 64
	bl	flash_wait_device_ready_raw
.L1782:
	mov	w0, w22
	bl	hynix_reconfig_rr_para
	mov	w0, w22
	bl	nandc_cs
	cbnz	w28, .L1784
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x0,164]
	cbz	w1, .L1785
	ldrb	w0, [x0,165]
	cbz	w0, .L1786
.L1785:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w2, [x1,166]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,165]
	sub	w19, w19, w0
	cbz	w2, .L1787
	add	w19, w0, w19, lsl 1
	b	.L1786
.L1787:
	add	x1, x1, 168
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
	b	.L1788
.L1786:
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	b	.L1788
.L1784:
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
.L1788:
	ubfiz	x1, x22, 8, 8
	mov	w0, 128
	add	x20, x20, x1
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
	adrp	x1, .LANCHOR2+377
	mov	x2, x25
	mov	x3, x24
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+377]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w23, [x20,2056]
	mov	w0, w22
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
	ldrb	w2, [x1,3814]
	cmp	w2, 255
	beq	.L1793
	ubfx	x4, x4, 21, 3
	add	x1, x1, 728
.L1795:
	add	x3, x1, x2, lsl 6
	ldr	w5, [x3,40]
	ubfx	x5, x5, 21, 3
	cmp	w4, w5
	bne	.L1794
	ldrb	w5, [x3,58]
	add	x3, x3, 48
	cmp	w5, 6
	bne	.L1794
	mov	w1, 3
	strb	w1, [x3,10]
	b	.L1793
.L1794:
	lsl	x2, x2, 6
	ldrb	w2, [x1,x2]
	cmp	w2, 255
	bne	.L1795
.L1793:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 3814
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
	cbz	w0, .L1801
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L1801
	adrp	x0, .LC104
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC104
	mov	w2, w20
	bl	printk
.L1801:
	adrp	x21, .LANCHOR0
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	mov	w23, 1
	add	x25, x21, 728
	mov	w26, 21
.L1802:
	cbz	w20, .L1829
	ldrb	w24, [x19]
	ldr	w27, [x19,40]
.L1803:
	mov	w0, w27
	mov	w1, 1
	bl	queue_lun_state
	mov	w28, w0
	cbz	w0, .L1830
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1803
.L1830:
	cmp	w20, 1
	beq	.L1805
	ldrb	w0, [x21,717]
	cbnz	w0, .L1806
.L1805:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L1807
.L1806:
	ldrb	w1, [x19]
	ubfx	x2, x27, 21, 3
	cmp	w1, 255
	bne	.L1808
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 224
	mov	w2, 486
	bl	printk
.L1809:
	b	.L1809
.L1808:
	sbfiz	x1, x1, 6, 32
	add	x1, x25, x1
	ldr	w5, [x1,40]
	ubfx	x0, x5, 21, 3
	cmp	w2, w0
	bne	.L1810
	ldrh	w3, [x21,3266]
	ldrb	w6, [x21,3264]
	sub	w2, w26, w3
	lsr	w1, w27, w3
	sub	w6, w6, #1
	lsl	w2, w23, w2
	sub	w2, w2, #1
	lsl	w4, w23, w3
	uxth	w7, w6
	sub	w4, w4, #1
	and	w0, w7, w1
	lsr	w1, w5, w3
	uxth	w2, w2
	and	w3, w7, w1
	and	w3, w2, w3
	and	w1, w2, w0
	cmp	w1, w3
	uxth	w4, w4
	beq	.L1811
	and	w27, w4, w27
	and	w4, w4, w5
	cmp	w27, w4
	bne	.L1811
	cmp	w22, w6
	beq	.L1810
	ldr	w1, [x19,40]
	mov	w0, 17
	ldr	x2, [x19,8]
	add	w22, w22, 1
	ldr	x3, [x19,24]
	bl	flash_start_page_prog
	strb	w23, [x19,59]
	mov	w0, 8
	strb	w0, [x19,58]
	mov	w0, -1
	mov	x1, x19
	strb	w0, [x19]
	add	x0, x21, 3814
	bl	buf_add_tail
	b	.L1807
.L1811:
	mov	x0, x19
	mov	w22, 0
	bl	queue_prog_cmd
	b	.L1807
.L1810:
	mov	x0, x19
	mov	w22, w28
	bl	queue_prog_cmd
.L1807:
	sbfiz	x19, x24, 6, 32
	sub	w20, w20, #1
	add	x19, x25, x19
	b	.L1802
.L1829:
	mov	w0, w20
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
	adrp	x20, .LANCHOR5
	ldrb	w1, [x0,3812]
	cbz	w1, .L1832
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 728
	ldrb	w2, [x2,449]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L1832:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2,3812]
	strb	w0, [x20,449]
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
	adrp	x0, .LANCHOR0+3812
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+3812]
	cbz	w0, .L1836
	bl	timer_get_time
	adrp	x1, .LANCHOR5+452
	ldr	w1, [x1,#:lo12:.LANCHOR5+452]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L1836
	bl	ftl_flush
.L1836:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	stp	x29, x30, [sp, -80]!
	adrp	x7, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x21, x2
	add	x2, x7, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	w24, w1
	uxtb	w25, w0
	ldrb	w1, [x2,152]
	mov	x20, x3
	mov	w22, w4
	uxtb	w23, w5
	cmp	w1, w25
	ubfx	x0, x24, 24, 2
	bhi	.L1841
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 240
	mov	w2, 471
	bl	printk
.L1842:
	b	.L1842
.L1841:
	add	x1, x2, x25, sxtw
	mov	w19, w24
	ldrb	w26, [x1,156]
	cbnz	w0, .L1843
	ldrb	w0, [x2,164]
	cbz	w0, .L1844
	ldrb	w0, [x2,165]
	cbz	w0, .L1843
.L1844:
	add	x7, x7, :lo12:.LANCHOR0
	ldrh	w19, [x7,166]
	ldrb	w1, [x7,165]
	udiv	w6, w24, w19
	mul	w19, w6, w19
	sub	w0, w24, w19
	cbz	w1, .L1845
	add	w19, w19, w0, lsl 1
	b	.L1843
.L1845:
	add	x7, x7, 168
	ldrh	w0, [x7,w0,uxtw 1]
	add	w19, w0, w19
.L1843:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1846
	adrp	x0, .LC105
	mov	w1, w26
	add	x0, x0, :lo12:.LC105
	mov	w2, w24
	bl	printk
.L1846:
	mov	w0, w26
	mov	w1, w19
	mov	x2, x21
	mov	x3, x20
	mov	w4, w22
	bl	flash_prog_page
	mov	w26, w0
	cbz	w23, .L1847
	adrp	x19, .LANCHOR4
	mov	w0, w25
	add	x23, x19, :lo12:.LANCHOR4
	mov	w1, w24
	mov	w4, w22
	ldr	x2, [x23,1192]
	ldr	x3, [x23,1200]
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L1848
	cmn	w0, #1
	beq	.L1848
	ldr	x0, [x23,1192]
	ldr	w1, [x21]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L1848
	ldr	x0, [x23,1200]
	ldr	w1, [x20]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L1847
.L1848:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 4
	adrp	x0, .LC106
	mov	w3, w2
	add	x0, x0, :lo12:.LC106
	ldr	x1, [x19,1200]
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC107
	ldr	x1, [x19,1192]
	add	x0, x0, :lo12:.LC107
	mov	w3, w2
	bl	rknand_print_hex
	b	.L1849
.L1847:
	cmn	w26, #1
	bne	.L1850
.L1849:
	adrp	x0, .LC108
	mov	w1, w24
	add	x0, x0, :lo12:.LC108
	bl	printk
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 240
	mov	w2, 497
	bl	printk
.L1851:
	b	.L1851
.L1850:
	mov	w0, w26
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_test_block
	.type	ftl_test_block, %function
ftl_test_block:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x25, .LANCHOR4
	adrp	x27, .LANCHOR5
	add	x25, x25, :lo12:.LANCHOR4
	add	x27, x27, :lo12:.LANCHOR5
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	w26, 0
	adrp	x28, .LC109
	uxth	w23, w0
	str	wzr, [x25,1216]
	add	x0, x25, 1216
	str	wzr, [x27,192]!
	mov	w21, w26
	add	x28, x28, :lo12:.LC109
	str	x0, [x29,104]
.L1865:
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,3272]
	cmp	w1, w21
	bls	.L1874
	mov	w24, 0
.L1875:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,3264]
	cmp	w0, w24
	bls	.L1890
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1866
	mov	x0, x28
	mov	w1, w23
	bl	printk
.L1866:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w19, [x0,3264]
	madd	w19, w23, w19, w24
	uxth	w19, w19
	cbnz	w21, .L1867
	ldr	x0, [x0,144]
	ldrb	w0, [x0,47]
	cmp	w0, w19
	bcs	.L1868
.L1867:
	uxtb	w22, w21
	mov	w1, w19
	mov	w0, w22
	bl	flash_check_bad_block
	cbnz	w0, .L1868
	add	x4, x20, :lo12:.LANCHOR0
	mov	w0, w22
	mov	w1, w26
	str	x4, [x29,96]
	ldrh	w3, [x4,3270]
	mul	w25, w19, w3
	mov	w2, w25
	bl	flash_erase_block_en
	ldr	x4, [x29,96]
	cbz	w0, .L1869
	ldrb	w0, [x4,3177]
	cmp	w0, 2
	bne	.L1889
	ldrb	w0, [x4,164]
	cbz	w0, .L1889
	mov	w0, w22
	mov	w1, 1
	mov	w2, w25
	bl	flash_erase_block_en
	cbz	w0, .L1871
	b	.L1889
.L1869:
	cbz	w26, .L1872
.L1871:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w26, 1
	ldrb	w0, [x0,3177]
	add	w25, w25, w0, lsl 24
.L1872:
	add	x4, x20, :lo12:.LANCHOR0
	ldr	x2, [x29,104]
	mov	w0, w22
	mov	w1, w25
	mov	x3, x27
	mov	w5, 1
	ldrb	w4, [x4,2776]
	bl	flash_prog_page_en
	cbz	w0, .L1868
.L1889:
	mov	w0, w22
	mov	w1, w19
	bl	flash_mask_bad_block
.L1868:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L1875
.L1890:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1865
.L1874:
	cbz	w26, .L1876
	ldr	x0, [x0,64]
	add	x23, x0, x23, uxth 2
	ldrb	w0, [x23,2]
	mov	w1, 2
	bfi	w0, w1, 3, 2
	strb	w0, [x23,2]
.L1876:
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
	mov	w5, 1
	ldr	x3, [x29,48]
	ldr	x4, [x29,56]
	bl	flash_prog_page_en
	cmn	w0, #1
	bne	.L1892
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 264
	mov	w2, 2020
	bl	printk
.L1893:
	b	.L1893
.L1892:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_prog_page, .-ftl_prog_page
	.align	2
	.global	ftl_info_flush
	.type	ftl_info_flush, %function
ftl_info_flush:
	stp	x29, x30, [sp, -112]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	adrp	x21, .LANCHOR5
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	add	x19, x21, :lo12:.LANCHOR5
	adrp	x23, .LC110
	ldrb	w2, [x0,2776]
	mov	w24, 0
	add	x0, x19, 512
	add	x23, x23, :lo12:.LC110
	lsl	w2, w2, 1
	mov	w27, 1
	add	x25, x19, 832
	bl	ftl_memset
.L1895:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w28, [x19,768]
	ldrh	w20, [x19,770]
	ldr	x2, [x0,2800]
	ldrh	w4, [x0,3270]
	ldr	w3, [x2,4]
	add	w3, w3, 1
	str	w3, [x2,4]
	str	w26, [x19,512]
	ldr	x3, [x0,2800]
	ldr	w0, [x3,4]
	str	w0, [x19,516]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1896
	ldr	w3, [x3,4]
	mov	x0, x23
	mov	w1, w28
	mov	w2, w20
	str	x4, [x29,104]
	bl	printk
	ldr	x4, [x29,104]
.L1896:
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w0, [x19,770]
	ldrh	w1, [x2,3228]
	cmp	w1, w0
	bhi	.L1897
	ldrb	w0, [x19,769]
	mov	w4, 0
	ldr	x5, [x2,144]
	ldrb	w1, [x19,768]
.L1904:
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, 7
	bls	.L1898
	mov	x2, 0
.L1903:
	add	x3, x5, x2
	uxth	w6, w2
	ldrb	w7, [x3,40]
	add	w3, w7, 127
	uxtb	w3, w3
	cmp	w3, 125
	bhi	.L1899
	add	x21, x21, :lo12:.LANCHOR5
	strb	w0, [x21,769]
	cbz	w4, .L1900
	strb	w1, [x21,768]
.L1900:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 280
	mov	w2, 668
	bl	printk
.L1901:
	b	.L1901
.L1899:
	cmp	w7, 255
	bne	.L1902
	add	x2, x2, 1
	cmp	x2, 8
	bne	.L1903
	mov	w6, w2
.L1902:
	uxtb	w0, w6
	mov	w24, 1
.L1898:
	add	w1, w0, 8
	add	x1, x5, x1, sxtw
	ldrb	w1, [x1,32]
	mov	w4, 1
	cmp	w1, 255
	beq	.L1904
	add	x28, x22, :lo12:.LANCHOR0
	strb	w0, [x19,769]
	strb	w1, [x19,768]
	mov	w0, 0
	ldrh	w20, [x28,3270]
	mul	w20, w1, w20
	mov	w1, w20
	bl	flash_erase_block
	ldrb	w4, [x28,2776]
	mov	w1, w20
	mov	w0, 0
	mov	x2, x25
	add	x3, x19, 512
	add	w20, w20, 1
	bl	ftl_prog_page
	strh	w27, [x19,770]
	b	.L1905
.L1897:
	madd	w20, w28, w4, w20
	cbnz	w0, .L1905
	mov	w1, w20
	bl	flash_erase_block
.L1905:
	add	x4, x22, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w0, 0
	mov	x2, x25
	add	x3, x19, 512
	ldrb	w4, [x4,2776]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x19,770]
	add	w1, w1, 1
	strh	w1, [x19,770]
	beq	.L1895
	cbnz	w24, .L1906
.L1914:
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w0, [x21,768]
	cmp	w0, 255
	bne	.L1930
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 703
	add	x1, x1, 280
	add	x0, x0, :lo12:.LC0
	bl	printk
	b	.L1915
.L1906:
	ldrb	w19, [x19,769]
	add	w19, w19, 1
.L1909:
	cmp	w19, 7
	bhi	.L1914
	add	x2, x22, :lo12:.LANCHOR0
	add	w0, w19, 8
	ldr	x1, [x2,144]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	add	w0, w1, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L1910
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 280
	mov	w2, 696
	bl	printk
.L1911:
	b	.L1911
.L1910:
	cmp	w1, 255
	beq	.L1912
	ldrh	w2, [x2,3270]
	mov	w0, 0
	mul	w1, w1, w2
	bl	flash_erase_block
.L1912:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1909
.L1915:
	b	.L1915
.L1930:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	stp	x29, x30, [sp, -112]!
	mov	w2, 16384
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR5
	adrp	x23, .LANCHOR6
	ldrh	w1, [x19,56]
	add	x22, x21, :lo12:.LANCHOR5
	add	x24, x22, 832
	add	x23, x23, :lo12:.LANCHOR6
	mov	x0, x24
	str	x24, [x19,64]
	add	x1, x24, x1, lsl 2
	str	x1, [x19,2800]
	mov	w1, 0
	strb	wzr, [x23,-192]
	adrp	x26, .LC111
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC111
	sub	x0, x23, #128
	mov	w1, 0
	mov	w2, 16384
	add	x23, x22, 512
	bl	ftl_memset
	strb	wzr, [x22,769]
	ldr	x0, [x19,144]
	mov	w19, 7
	strh	wzr, [x22,770]
	ldrb	w0, [x0,40]
	strb	w0, [x22,768]
.L1936:
	add	x5, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w25, w19
	ldr	x1, [x5,144]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L1932
	ldrh	w22, [x5,3270]
	mov	w0, 0
	ldrb	w4, [x5,2776]
	mov	x2, x24
	mov	x3, x23
	str	x5, [x29,104]
	mul	w22, w1, w22
	mov	w1, w22
	bl	ftl_read_page
	cmn	w0, #1
	mov	w28, w0
	ldr	x5, [x29,104]
	bne	.L1933
	ldrb	w4, [x5,2776]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L1933:
	add	x0, x27, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1934
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, w28
	mov	w3, 576
	ldr	x4, [x0,2800]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L1934:
	cmn	w28, #1
	beq	.L1932
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L1949
.L1932:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L1936
	mov	w25, 0
	b	.L1935
.L1949:
	mov	w19, w25
.L1935:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1937
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,2800]
	adrp	x0, .LC112
	add	x0, x0, :lo12:.LC112
	ldr	w3, [x3]
	bl	printk
.L1937:
	cmn	w19, #1
	bne	.L1938
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, 0
	add	x0, x0, 832
	mov	w2, 16384
	add	x20, x20, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	w0, 21574
	ldr	x1, [x20,2800]
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, w19
	b	.L1939
.L1938:
	add	x26, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR5
	add	w0, w25, 8
	add	x24, x23, 832
	mov	x2, x24
	mov	w4, 4
	ldr	x1, [x26,144]
	add	x0, x1, x0, sxtw
	strb	w25, [x23,769]
	add	x25, x23, 512
	ldrb	w1, [x0,32]
	mov	x3, x25
	mov	w0, 0
	strb	w1, [x23,768]
	bl	flash_get_last_written_page
	sxth	w22, w0
	add	w0, w0, 1
	uxth	w19, w0
	ldrb	w0, [x23,768]
	ldrh	w23, [x26,3270]
	mul	w23, w0, w23
.L1940:
	tbnz	w22, #31, .L1945
	add	x26, x20, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w22, w23
	mov	x2, x24
	mov	x3, x25
	ldrb	w4, [x26,2776]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L1941
	ldr	x0, [x26,2800]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L1941
.L1945:
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR5
	ldr	x0, [x20,2800]
	strh	w19, [x21,770]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L1943
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
	b	.L1943
.L1941:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1940
.L1943:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L1946
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L1946:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L1947
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L1947:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L1948
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L1948:
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
.L1939:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	bl	timer_get_time
	adrp	x22, .LANCHOR0
	mov	w1, 100
	udiv	w0, w0, w1
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x1, [x1,80]
	ldr	w3, [x1,520]
	cmp	w0, w3
	bls	.L1967
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L1974
.L1967:
	bcs	.L1968
.L1974:
	str	w0, [x1,520]
.L1968:
	adrp	x20, .LANCHOR5
	adrp	x23, .LANCHOR6
	add	x20, x20, :lo12:.LANCHOR5
	add	x23, x23, :lo12:.LANCHOR6
	mov	w0, 0
	add	x20, x20, 512
	sub	x23, x23, #128
	bl	ftl_total_vpn_update
.L1973:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3228]
	cmp	w1, w0
	bcc	.L1969
	bl	ftl_ext_alloc_new_blk
.L1969:
	add	x21, x22, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x3, [x21,2800]
	ldrh	w1, [x3,130]
	cmp	w1, w0
	bne	.L1970
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 296
	mov	w2, 1608
	bl	printk
.L1971:
	b	.L1971
.L1970:
	ldrh	w19, [x21,3266]
	mov	w0, 21
	mov	w2, 1
	sub	w19, w0, w19
	ldrh	w0, [x3,140]
	asr	w24, w1, w19
	lsl	w19, w2, w19
	sub	w19, w19, #1
	ldrb	w2, [x21,2776]
	and	w1, w19, w1
	ldrh	w19, [x21,3270]
	lsl	w2, w2, 1
	madd	w19, w1, w19, w0
	mov	w1, 0
	mov	x0, x20
	bl	ftl_memset
	str	wzr, [x20]
	ldr	x0, [x21,2800]
	uxth	w19, w19
	ldrb	w4, [x21,2776]
	mov	w1, w19
	mov	x2, x23
	mov	x3, x20
	ldr	w0, [x0,56]
	str	w0, [x20,4]
	mov	w0, w24
	bl	ftl_prog_page
	cmn	w0, #1
	ldr	x2, [x21,2800]
	ldrh	w1, [x2,140]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x2,140]
	beq	.L1973
	cmp	w1, 1
	cset	w0, eq
	cbnz	w0, .L1973
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
	strh	wzr, [x1,3808]
	adrp	x21, .LANCHOR5
	ldr	x0, [x1,2800]
	add	x2, x20, :lo12:.LANCHOR6
	ldrh	w22, [x1,3266]
	mov	w1, 21
	add	x3, x21, :lo12:.LANCHOR5
	sub	x2, x2, #128
	ldrh	w0, [x0,130]
	sub	w22, w1, w22
	add	x3, x3, 512
	asr	w22, w0, w22
	and	w25, w0, 16383
	mov	w1, w25
	uxtb	w22, w22
	mov	w0, w22
	bl	flash_get_last_written_page
	sxth	w24, w0
	adrp	x0, .LANCHOR2+400
	mov	w23, w24
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1976
	adrp	x1, .LANCHOR3
	adrp	x0, .LC69
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC69
	add	x1, x1, 320
	mov	w2, 1651
	mov	w3, w24
	bl	printk
.L1976:
	add	x20, x20, :lo12:.LANCHOR6
	add	x21, x21, :lo12:.LANCHOR5
	mov	w26, w24
	sub	x20, x20, #128
	add	x21, x21, 512
.L1977:
	tbnz	w26, #31, .L1982
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, w22
	mov	x2, x20
	mov	x3, x21
	ldrh	w1, [x27,3270]
	ldrb	w4, [x27,2776]
	madd	w1, w25, w1, w26
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L1978
	cmn	w0, #1
	beq	.L1978
	ldr	x0, [x27,80]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L1978
.L1982:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w1, [x0,140]
	cmp	w1, w24
	bgt	.L1980
	add	w23, w23, 1
	strh	w23, [x0,140]
	bl	ftl_ext_info_flush
	b	.L1980
.L1978:
	sub	w26, w26, #1
	sxth	w26, w26
	b	.L1977
.L1980:
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
	strh	w0, [x19,3810]
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w19, 8
	adrp	x20, .LANCHOR0
.L1996:
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR0
	ldr	x0, [x2,144]
	add	x0, x0, x19, sxtw
	ldrb	w1, [x0,32]
	add	w0, w1, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L1993
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 344
	mov	w2, 1822
	bl	printk
.L1994:
	b	.L1994
.L1993:
	cmp	w1, 255
	beq	.L1995
	ldrh	w2, [x2,3270]
	mov	w0, 0
	mul	w1, w1, w2
	bl	flash_erase_block
.L1995:
	add	w19, w19, 1
	cmp	w19, 16
	bne	.L1996
	bl	sblk_init
	bl	ftl_info_blk_init
	adrp	x0, .LANCHOR5
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR5
	mov	w2, 16384
	add	x0, x0, 832
	bl	ftl_memset
	add	x1, x21, :lo12:.LANCHOR0
	mov	w6, 1
	ldr	x0, [x1,2800]
	ldrh	w5, [x1,56]
	ldrh	w0, [x0,134]
.L1997:
	cmp	w0, w5
	bcs	.L2032
	add	x4, x21, :lo12:.LANCHOR0
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
	b	.L1997
.L2032:
	add	x0, x21, :lo12:.LANCHOR0
	mov	w1, 21574
	movk	w1, 0x494c, lsl 16
	mov	w19, 0
	adrp	x24, .LANCHOR7
	mov	w25, 1
	ldr	x2, [x0,2800]
	str	w1, [x2]
	ldr	x0, [x0,2800]
	strh	wzr, [x0,148]
.L1999:
	add	x20, x21, :lo12:.LANCHOR0
	ldrh	w0, [x20,56]
	cmp	w0, w19
	bls	.L2033
	add	x0, x24, :lo12:.LANCHOR7
	mov	w27, 0
	ubfiz	x22, x19, 2, 16
	mov	w23, w27
	strh	w19, [x0,3200]
	mov	w0, w19
	bl	ftl_test_block
	ldrb	w28, [x20,3264]
	ldrb	w4, [x20,3272]
	mov	w20, w27
	mul	w6, w19, w28
	uxth	w5, w28
.L2000:
	cmp	w20, w4
	bcs	.L2004
	mov	x26, 0
.L2005:
	cmp	w5, w26, uxth
	bls	.L2034
	add	w1, w6, w26
	mov	w0, w20
	str	x5, [x29,104]
	str	x4, [x29,112]
	str	x6, [x29,120]
	bl	flash_check_bad_block
	ldr	x6, [x29,120]
	ldr	x4, [x29,112]
	ldr	x5, [x29,104]
	cbz	w0, .L2001
	add	x3, x21, :lo12:.LANCHOR0
	add	w1, w27, w26
	lsl	w1, w25, w1
	ldr	x0, [x3,64]
	add	x0, x0, x22
	ldrb	w2, [x0,3]
	orr	w1, w1, w2
	strb	w1, [x0,3]
	ldr	x1, [x3,2800]
	ldrh	w0, [x1,148]
	add	w0, w0, 1
	strh	w0, [x1,148]
	b	.L2002
.L2001:
	add	w23, w23, 1
	uxth	w23, w23
.L2002:
	add	x26, x26, 1
	b	.L2005
.L2034:
	add	w20, w20, 1
	add	w27, w27, w28
	uxth	w20, w20
	b	.L2000
.L2004:
	cbnz	w23, .L2006
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,64]
	add	x22, x0, x22
	ldrb	w0, [x22,2]
	orr	w0, w0, -32
	strb	w0, [x22,2]
.L2006:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1999
.L2033:
	adrp	x0, .LANCHOR6
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR6
	mov	w2, 16384
	sub	x0, x0, #128
	bl	ftl_memset
	ldr	x1, [x20,80]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	mov	w6, 3
	mov	w7, 15
	str	w0, [x1]
	mov	x1, 0
	ldr	x0, [x20,144]
	ldrb	w4, [x20,3264]
	ldrb	w20, [x0,47]
	udiv	w20, w20, w4
	add	w20, w20, 1
.L2010:
	add	x3, x21, :lo12:.LANCHOR0
	lsl	x0, x1, 2
	cmp	w4, 1
	csinc	w5, w6, wzr, ne
	cmp	w4, 4
	ldr	x2, [x3,64]
	add	x2, x2, x0
	strb	w5, [x2,3]
	bne	.L2009
	ldr	x2, [x3,64]
	add	x2, x2, x0
	strb	w7, [x2,3]
.L2009:
	add	x2, x21, :lo12:.LANCHOR0
	add	x1, x1, 1
	cmp	w20, w1, uxth
	ldr	x5, [x2,2800]
	ldrh	w3, [x5,148]
	add	w3, w4, w3
	strh	w3, [x5,148]
	ldr	x3, [x2,64]
	add	x0, x3, x0
	ldrb	w3, [x0,2]
	orr	w3, w3, -32
	strb	w3, [x0,2]
	bhi	.L2010
	mov	w0, 16
	sdiv	w4, w0, w4
	ldr	x0, [x2,2800]
	add	w4, w20, w4
	uxth	w4, w4
	strh	w4, [x0,134]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L2011
	adrp	x0, .LC113
	mov	w1, 1869
	add	x0, x0, :lo12:.LC113
	mov	w2, 128
	mov	w3, w20
	bl	printk
.L2011:
	add	x19, x21, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 128
	ldr	x0, [x19,2800]
	add	x0, x0, 160
	bl	ftl_memset
	ldrb	w8, [x19,3264]
	mov	w10, 21
	ldrh	w0, [x19,3266]
	ldrb	w13, [x19,3272]
	uxth	w9, w8
	sub	w10, w10, w0
	mov	w0, 0
	mul	w11, w20, w9
	mov	w22, w0
	uxth	w11, w11
.L2012:
	add	x19, x21, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	ldrh	w1, [x1,134]
	cmp	w1, w20
	bls	.L2035
	ldr	x2, [x19,64]
	ubfiz	x1, x20, 2, 16
	add	w6, w0, w11
	mov	w5, 0
	add	x2, x2, x1
	mov	w4, w5
	uxth	w6, w6
	ldrb	w3, [x2,2]
	orr	w3, w3, -32
	strb	w3, [x2,2]
	ldr	x2, [x19,64]
	ldr	x14, [x19,2800]
	add	x1, x2, x1
.L2013:
	cmp	w13, w4, uxth
	bls	.L2017
	lsl	w12, w4, w10
	mov	x3, 0
	uxth	w12, w12
.L2018:
	uxth	w2, w3
	cmp	w2, w9
	bcs	.L2036
	ldrb	w7, [x1,3]
	add	w15, w5, w3
	asr	w7, w7, w15
	tbnz	x7, 0, .L2014
	cmp	w8, 1
	mov	w7, w6
	bls	.L2015
	and	w2, w2, 1
	add	w2, w6, w2
	uxth	w7, w2
.L2015:
	add	x15, x14, x22, sxtw 1
	add	w22, w22, 1
	add	w2, w7, w12
	strh	w2, [x15,160]
	uxth	w22, w22
.L2014:
	add	x3, x3, 1
	b	.L2018
.L2036:
	add	w4, w4, 1
	add	w5, w5, w8
	b	.L2013
.L2017:
	add	w20, w20, 1
	add	w0, w0, w9
	uxth	w20, w20
	uxth	w0, w0
	b	.L2012
.L2035:
	ldrh	w2, [x19,56]
	mov	w1, 0
	ldr	x0, [x19,72]
	lsl	w2, w2, 1
	bl	ftl_memset
	bl	zftl_sblk_list_init
	ldr	x0, [x19,2800]
	ldrh	w1, [x19,56]
	ldrh	w2, [x0,134]
	strh	w22, [x0,112]
	sub	w1, w1, w2
	strh	wzr, [x0,114]
	strh	w1, [x0,116]
	strh	wzr, [x0,118]
	mov	w0, 1
	bl	ftl_alloc_sblk
	mov	w21, w0
	mov	w1, 0
	bl	ftl_erase_sblk
	ldr	x1, [x19,2800]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	ldr	x1, [x19,2800]
	mov	w0, 65533
	ldrh	w20, [x1,672]
	strh	wzr, [x1,690]
	sub	w2, w20, #1
	cmp	w0, w2, uxth
	bcs	.L2020
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 344
	mov	w2, 1900
	bl	printk
.L2021:
	b	.L2021
.L2020:
	strh	wzr, [x1,696]
	add	x0, x1, 416
	mov	w2, 256
	mov	w1, 255
	bl	ftl_memset
	ldr	x1, [x19,2800]
	mov	w2, 21
	ldrh	w0, [x19,3266]
	sub	w2, w2, w0
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3202
	asr	w2, w20, w2
	strh	w20, [x1,692]
	strh	w2, [x1,694]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3202]
	strh	w21, [x1,416]
	strh	w0, [x1,698]
	bl	ftl_alloc_sys_blk
	mov	w20, w0
	mov	w1, 0
	bl	ftl_erase_phy_blk
	ldr	x1, [x19,2800]
	ldr	x0, [x19,80]
	strh	w20, [x1,130]
	add	x0, x0, 16
	mov	w1, 2
	bl	ftl_open_sblk_init
	ldr	x0, [x19,80]
	mov	w1, 3
	add	x0, x0, 48
	bl	ftl_open_sblk_init
	ldr	x1, [x19,2800]
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
	adrp	x0, .LC114
	mov	w1, 1
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC114
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	adrp	x19, .LANCHOR0
	bl	printk
	bl	sblk_init
	bl	ftl_info_blk_init
	bl	ftl_ext_info_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	ldrh	w2, [x0,56]
	mov	w0, 0
	mov	w3, w0
	mov	w4, w0
	ldrh	w1, [x1,134]
.L2038:
	cmp	w1, w2
	bcs	.L2045
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x6, [x5,64]
	add	x6, x6, x1, uxth 2
	ldrb	w5, [x6,2]
	and	w7, w5, 31
	ands	w5, w5, 24
	strb	w7, [x6,2]
	bne	.L2039
	add	w4, w4, 1
	uxth	w4, w4
	b	.L2040
.L2039:
	cmp	w5, 16
	bne	.L2041
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2040
.L2041:
	add	w0, w0, 1
	uxth	w0, w0
.L2040:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2038
.L2045:
	add	x19, x19, :lo12:.LANCHOR0
	lsl	w2, w2, 1
	ldr	x1, [x19,2800]
	strh	w0, [x1,116]
	ldr	x0, [x19,72]
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
	ldr	x1, [x19,2800]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	ldr	x1, [x19,2800]
	mov	w0, 65533
	ldrh	w20, [x1,672]
	strh	wzr, [x1,690]
	sub	w2, w20, #1
	cmp	w0, w2, uxth
	bcs	.L2043
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 360
	mov	w2, 1972
	bl	printk
.L2044:
	b	.L2044
.L2043:
	strh	wzr, [x1,696]
	add	x0, x1, 416
	mov	w2, 256
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19,2800]
	mov	w1, 0
	mov	w2, 4096
	add	x0, x0, 704
	bl	ftl_memset
	ldr	x1, [x19,2800]
	lsr	w0, w20, 14
	ldrh	w2, [x19,56]
	and	w20, w20, 16383
	strh	w0, [x1,694]
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3202
	lsl	w2, w2, 1
	strh	w20, [x1,692]
	strh	w21, [x1,416]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3202]
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
	ldr	x1, [x19,2800]
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
	ldr	x22, [x19,3792]
	bne	.L2048
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L2048
	ldrh	w0, [x20]
	add	x22, x22, x24, uxth 2
	bl	ftl_vpn_decrement
	adrp	x1, .LANCHOR5
	mov	w0, 15555
	add	x1, x1, :lo12:.LANCHOR5
	movk	w0, 0xf55f, lsl 16
	add	x2, x1, 192
	str	x2, [x29,72]
	str	w0, [x1,192]
	ldrb	w0, [x19,3196]
	ldrh	w1, [x19,3228]
	mul	w1, w1, w0
	mov	x0, x22
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,72]
	mov	x1, x22
	ldrb	w3, [x19,2776]
	str	w0, [x2,4]
	mov	w0, w23
	str	w21, [x2,8]
	str	w21, [x2,12]
	bl	ftl_prog_ppa_page
.L2048:
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
	sub	sp, sp, #160
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x21, x22, [sp,80]
	adrp	x21, .LANCHOR2
	stp	x27, x28, [sp,128]
	mov	x28, x0
	add	x0, x21, :lo12:.LANCHOR2
	stp	x25, x26, [sp,112]
	stp	x19, x20, [sp,64]
	stp	x23, x24, [sp,96]
	mov	x25, x1
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2053
	adrp	x0, .LC115
	ldrh	w1, [x28,2]
	add	x0, x0, :lo12:.LC115
	bl	printk
.L2053:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2054
	adrp	x0, .LC116
	ldrb	w1, [x28,5]
	add	x0, x0, :lo12:.LC116
	bl	printk
.L2054:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2055
	adrp	x0, .LC117
	ldrh	w1, [x28]
	add	x0, x0, :lo12:.LC117
	bl	printk
.L2055:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2056
	adrp	x0, .LC118
	ldrh	w1, [x28,16]
	ldrh	w2, [x28,18]
	add	x0, x0, :lo12:.LC118
	bl	printk
.L2056:
	ldrh	w0, [x28,10]
	adrp	x23, .LANCHOR5
	strh	w0, [x28,14]
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR5
	adrp	x24, .LC119
	bl	buf_alloc
	ldrb	w19, [x28,5]
	ldrh	w20, [x28,2]
	mov	x27, x0
	mov	w26, 0
	add	x23, x23, 512
	add	x24, x24, :lo12:.LC119
.L2057:
	adrp	x0, .LANCHOR0
	adrp	x22, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,3228]
	cmp	w0, w20
	bls	.L2060
	ldrb	w19, [x28,5]
.L2058:
	ldrb	w0, [x28,9]
	cmp	w0, w19
	bls	.L2139
	add	x0, x28, x19, sxtw 1
	ldrh	w5, [x0,16]
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2059
	add	x3, x22, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	str	x5, [x29,96]
	ldrh	w4, [x3,3270]
	ldrb	w3, [x3,2776]
	madd	w4, w5, w4, w20
	str	x4, [x29,104]
	mov	w0, w4
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w10, w0
	beq	.L2060
	cmn	w0, #1
	ldr	x4, [x29,104]
	ldr	x5, [x29,96]
	beq	.L2061
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L2061
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L2061
	ldr	x0, [x27,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L2060
.L2061:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2062
	ldr	x7, [x27,8]
	mov	w3, w4
	ldr	x0, [x27,24]
	mov	w4, w10
	mov	w2, w20
	str	x10, [x29,104]
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
	mov	x0, x24
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x10, [x29,104]
.L2062:
	adrp	x0, .LANCHOR6-192
	ldrb	w2, [x28,9]
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-192]
	ldrh	w0, [x28,10]
	madd	w1, w20, w2, w19
	cmp	w0, w1
	beq	.L2063
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 384
	mov	w2, 1260
	bl	printk
.L2064:
	b	.L2064
.L2063:
	add	x26, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28,6]
	add	w0, w1, w0
	ldrh	w1, [x26,3228]
	mul	w1, w1, w2
	cmp	w0, w1
	beq	.L2065
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 384
	mov	w2, 1261
	bl	printk
.L2066:
	b	.L2066
.L2065:
	cmn	w10, #1
	beq	.L2070
	ldr	x4, [x27,24]
	str	x4, [x29,104]
	ldr	w0, [x4,4]
	cmn	w0, #1
	beq	.L2070
	bl	lpa_hash_get_ppa
	cbz	x25, .L2068
	ldr	x4, [x29,104]
	ldr	w1, [x4,8]
	cmp	w0, w1
	beq	.L2068
	cmn	w0, #1
	beq	.L2068
	ldrh	w5, [x26,3266]
	mov	w1, 21
	mov	w2, 1
	sub	w1, w1, w5
	lsr	w3, w0, w5
	lsl	w1, w2, w1
	ldrb	w2, [x26,3264]
	sub	w1, w1, #1
	and	w1, w3, w1
	udiv	w1, w1, w2
	ldrh	w2, [x25]
	cmp	w2, w1, uxth
	bne	.L2068
	ldr	w4, [x4]
	mov	x2, x23
	ldr	x1, [x27,8]
	ldrb	w3, [x26,2776]
	str	x4, [x29,104]
	bl	ftl_read_ppa_page
	ldr	w0, [x23]
	ldr	x4, [x29,104]
	cmp	w0, w4
	bcc	.L2068
	ldr	x0, [x27,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L2070
	add	x4, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w3, 1
	ldrh	w0, [x4,3266]
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w3, w2
	ldrb	w0, [x4,3264]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L2070
.L2068:
	ldr	x2, [x27,24]
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2784]
	cmp	w3, w0
	bcs	.L2070
	ldrb	w0, [x28,9]
	ldrh	w4, [x1,3228]
	ldrh	w3, [x28,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L2140
.L2070:
	ldrh	w0, [x28,6]
	mov	w26, 1
	sub	w0, w0, #1
	strh	w0, [x28,6]
	ldrh	w0, [x28,10]
	add	w0, w0, 1
	strh	w0, [x28,10]
.L2059:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2058
.L2139:
	add	w20, w20, 1
	strb	wzr, [x28,5]
	uxth	w20, w20
	b	.L2057
.L2060:
	add	x4, x22, :lo12:.LANCHOR0
	ldrb	w1, [x28,9]
	ldrh	w2, [x28,10]
	ldrh	w3, [x28,6]
	ldrh	w0, [x4,3228]
	add	w3, w3, w2
	strh	w20, [x28,2]
	strb	w19, [x28,5]
	mul	w0, w1, w0
	cmp	w3, w0
	bne	.L2074
	mov	w19, 0
	ldr	x4, [x4,3792]
	mov	w1, w19
	b	.L2075
.L2074:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 384
	mov	w2, 1337
	bl	printk
.L2076:
	b	.L2076
.L2078:
	ldrh	w3, [x28,12]
	add	w3, w1, w3
	ldr	w3, [x4,x3,lsl 2]
	cmn	w3, #1
	beq	.L2077
	add	w19, w19, 1
	uxth	w19, w19
.L2077:
	add	w1, w1, 1
.L2075:
	cmp	w1, w2
	bcc	.L2078
	sub	w0, w0, w2
	add	w19, w19, w0
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w19, w19
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2079
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28]
	mov	w2, w19
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,72]
	adrp	x0, .LC120
	add	x0, x0, :lo12:.LC120
	ldrh	w3, [x4,x3]
	bl	printk
.L2079:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28]
	adrp	x20, .LC121
	mov	w23, -1
	add	x20, x20, :lo12:.LC121
	ldr	x0, [x0,72]
	strh	w19, [x0,x1,lsl 1]
	mov	x0, x27
	mov	w19, 0
	bl	buf_free
.L2080:
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w0, [x2,3196]
	cmp	w19, w0, lsl 1
	bcs	.L2141
	cbz	w26, .L2081
	ldrh	w0, [x28,6]
	cmp	w0, 1
	bls	.L2081
	mov	x0, x28
	bl	ftl_get_new_free_page
	mov	w24, w0
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2082
	mov	x0, x20
	mov	w1, w24
	bl	printk
.L2082:
	add	x3, x22, :lo12:.LANCHOR0
	ldr	x0, [x27,24]
	ldr	x1, [x3,2800]
	ldr	w1, [x1,8]
	str	w1, [x0]
	ldr	x0, [x27,24]
	str	w23, [x0,4]
	ldr	x0, [x27,24]
	str	w23, [x0,8]
	ldr	x0, [x27,24]
	str	wzr, [x0,12]
	ldr	x0, [x27,8]
	str	wzr, [x0]
	mov	w0, w24
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	ldrb	w3, [x3,2776]
	bl	ftl_prog_ppa_page
	ldrh	w0, [x28]
	bl	ftl_vpn_decrement
.L2081:
	add	w19, w19, 1
	b	.L2080
.L2141:
	ldrh	w1, [x2,3228]
	ldrh	w0, [x28,12]
	ldrb	w3, [x28,9]
	madd	w0, w1, w3, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x2,3792]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L2084
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 384
	mov	w2, 1364
	bl	printk
.L2085:
	b	.L2085
.L2084:
	ldrh	w0, [x28,6]
	cmp	w0, 1
	bne	.L2052
	mov	x0, x28
	bl	ftl_write_last_log_page
	b	.L2052
.L2140:
	ldr	x0, [x1,2800]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L2071
	str	w2, [x0,8]
.L2071:
	ldr	x1, [x27,24]
	ldrh	w0, [x28,10]
	ldrh	w2, [x28,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	b	.L2070
.L2052:
	sub	sp, x29, #48
	ldp	x19, x20, [sp,64]
	ldp	x21, x22, [sp,80]
	ldp	x23, x24, [sp,96]
	ldp	x25, x26, [sp,112]
	ldp	x27, x28, [sp,128]
	ldp	x29, x30, [sp,48]
	add	sp, sp, 160
	ret
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	adrp	x21, .LC122
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, w0
	mov	x24, x1
	mov	x25, x22
	mov	w23, 65535
	add	x21, x21, :lo12:.LC122
.L2143:
	adrp	x26, .LANCHOR7
.L2148:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3228]
	cmp	w3, w1
	bcs	.L2144
	ldrh	w0, [x0,692]
	cmp	w0, w23
	bne	.L2145
.L2144:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2145:
	add	x27, x25, :lo12:.LANCHOR0
	ldr	x0, [x27,2800]
	ldrh	w3, [x0,692]
	cmp	w3, w23
	bne	.L2146
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 408
	mov	w2, 224
	bl	printk
.L2147:
	b	.L2147
.L2146:
	add	x28, x26, :lo12:.LANCHOR7
	ldrh	w0, [x0,696]
	ldrh	w19, [x27,3270]
	mov	w1, 0
	mov	w2, 64
	madd	w19, w3, w19, w0
	ldr	x0, [x28,3208]
	bl	ftl_memset
	ldr	x0, [x28,3208]
	mov	x2, x24
	str	w20, [x0]
	ldr	x1, [x27,2800]
	ldr	x0, [x28,3208]
	ldrb	w4, [x27,2776]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	w1, w19
	ldr	x0, [x27,2800]
	ldr	x3, [x28,3208]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x1, [x27,2800]
	ldrh	w3, [x1,696]
	add	w3, w3, 1
	uxth	w3, w3
	strh	w3, [x1,696]
	cmp	w3, 1
	beq	.L2148
	cmn	w0, #1
	bne	.L2149
	mov	x0, x21
	mov	w1, w19
	bl	printk
	b	.L2143
.L2149:
	ldrh	w0, [x1,698]
	cmp	w20, w0
	bcs	.L2150
	add	x20, x1, x20, uxtw 2
	str	w19, [x20,704]
.L2150:
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
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	mov	w2, 64
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR4
	add	x0, x22, :lo12:.LANCHOR4
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x23, .LANCHOR0
	adrp	x24, .LC124
	ldr	x0, [x0,1176]
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x25, .LC125
	mov	w26, 0
	add	x24, x24, :lo12:.LC124
	add	x25, x25, :lo12:.LC125
	bl	ftl_memset
	ldr	x1, [x19,144]
	adrp	x0, .LC123
	add	x0, x0, :lo12:.LC123
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,144]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L2154:
	add	x21, x22, :lo12:.LANCHOR4
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,1184]
	ldrh	w19, [x21,1186]
	mov	w1, w20
	ldrh	w28, [x27,166]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,1012]
	ldrh	w0, [x21,1186]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L2155
	ldr	x1, [x27,144]
	ldrb	w19, [x21,1185]
	strh	wzr, [x21,1186]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,1184]
	ldr	x20, [x27,144]
	strb	w0, [x21,1185]
	mov	x0, x20
	strb	w19, [x21,1184]
	ldrh	w1, [x20,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x20,8]
	bl	js_hash
	str	w0, [x20,12]
	ldrh	w0, [x27,166]
	mul	w19, w19, w0
	mov	w0, 0
	b	.L2160
.L2155:
	madd	w19, w20, w28, w19
	cbnz	w0, .L2156
.L2160:
	mov	w1, w19
	bl	flash_erase_block
.L2156:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR4
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,144]
	ldr	x0, [x20,1176]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,1176]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,144]
	ldr	x3, [x20,1176]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,1186]
	add	w1, w1, 1
	strh	w1, [x20,1186]
	bne	.L2157
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L2154
.L2157:
	cbnz	w26, .L2161
	mov	w26, 1
	b	.L2154
.L2161:
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
	stp	x29, x30, [sp, -64]!
	mov	w1, 2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp,32]
	add	x21, x19, :lo12:.LANCHOR0
	mov	w22, 0
	stp	x23, x24, [sp,48]
	add	x21, x21, 684
	mov	w24, 44
	strb	w1, [x21,-4]
	bl	nandc_init
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 368
	str	x0, [x21,36]
	mov	x23, x21
.L2168:
	mov	w0, w22
	mov	x1, x21
	bl	flash_read_id
	cbnz	w22, .L2163
	ldrb	w0, [x23]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L2164
.L2166:
	mov	w0, -2
	b	.L2201
.L2164:
	ldrb	w0, [x23,1]
	cmp	w0, 255
	beq	.L2166
.L2163:
	ldrb	w0, [x21]
	cmp	w0, 181
	bne	.L2167
	strb	w24, [x21]
.L2167:
	add	w22, w22, 1
	add	x21, x21, 8
	uxtb	w22, w22
	cmp	w22, 4
	bne	.L2168
	add	x22, x20, :lo12:.LANCHOR2
	add	x23, x19, :lo12:.LANCHOR0
	add	x22, x22, 440
	mov	w21, 0
	add	x23, x23, 684
.L2171:
	ldrb	w2, [x22]
	add	x0, x22, 1
	mov	x1, x23
	bl	flash_mem_cmp8
	cbnz	w0, .L2169
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x21, x21, 5, 32
	add	x22, x1, 440
	adds	x22, x22, x21
	beq	.L2166
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L2173
.L2169:
	add	w21, w21, 1
	add	x22, x22, 32
	cmp	w21, 27
	bne	.L2171
	b	.L2166
.L2173:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1304]
	cmp	w4, w3
	beq	.L2172
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2173
	mov	w2, w0
.L2172:
	add	x23, x20, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR4
	add	x1, x23, 1304
	ubfiz	x2, x2, 5, 32
	add	x0, x21, :lo12:.LANCHOR4
	add	x1, x1, x2
	add	x0, x0, 1088
	mov	w2, 32
	add	x23, x23, 368
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w0, 8
	bhi	.L2174
	ldrb	w1, [x23,20]
	cmp	w1, 60
	bls	.L2175
	mov	w1, 60
	strb	w1, [x23,20]
.L2175:
	cmp	w0, 8
	bne	.L2174
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,684]
	cmp	w0, 44
	beq	.L2218
.L2174:
	add	x22, x21, :lo12:.LANCHOR4
	mov	w0, 16384
	add	x23, x19, :lo12:.LANCHOR0
	add	x24, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x22,1192]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x22,1144]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x23,144]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x22,1200]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x22,1152]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x22,1176]
	adrp	x0, .LANCHOR5+448
	strb	wzr, [x0,#:lo12:.LANCHOR5+448]
	bl	flash_die_info_init
	ldrb	w0, [x24,386]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24,388]
	bl	nandc_bch_sel
	str	xzr, [x22,1168]
	ldr	x1, [x23,720]
	ldrh	w0, [x1,16]
	lsr	w3, w0, 8
	ubfx	x4, x0, 3, 1
	and	w2, w3, 7
	strb	w2, [x23,681]
	adrp	x2, .LANCHOR7+3216
	strb	w4, [x2,#:lo12:.LANCHOR7+3216]
	ubfx	x2, x0, 4, 1
	strb	w2, [x23,717]
	ubfx	x2, x0, 12, 1
	strb	w2, [x23,3192]
	ubfx	x2, x0, 13, 1
	ubfx	x0, x0, 14, 1
	strb	w0, [x23,165]
	ldrb	w0, [x1,28]
	mov	w1, 60
	strb	w1, [x22,1209]
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	strb	w2, [x23,3268]
	strb	w0, [x23,164]
	cmp	w1, 9
	bne	.L2176
	mov	w1, 70
	strb	w1, [x22,1209]
.L2176:
	add	x20, x20, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	add	x2, x19, :lo12:.LANCHOR0
	strb	w0, [x1,1208]
	ldrb	w0, [x20,387]
	strb	w0, [x2,141]
	ldrh	w2, [x20,384]
	tbz	x2, 6, .L2178
	sub	w2, w0, #17
	uxtb	w2, w2
	cmp	w2, 2
	bhi	.L2179
	adrp	x2, micron_read_retrial
	cmp	w0, 19
	add	x2, x2, :lo12:micron_read_retrial
	str	x2, [x1,1168]
	bne	.L2214
	mov	w0, 15
	b	.L2215
.L2179:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2195
	cmp	w0, 33
	bne	.L2182
.L2195:
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1168]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,142]
	mov	w1, 7
	strb	w1, [x0,1160]
	b	.L2178
.L2182:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w4, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L2196
	cbz	w4, .L2184
.L2196:
	add	x2, x21, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x1, [x2,1168]
	beq	.L2186
	cmp	w0, 68
	beq	.L2186
	mov	w0, 7
	strb	w0, [x2,1160]
	b	.L2187
.L2186:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1160]
.L2187:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w4, .L2216
	mov	w1, 5
.L2216:
	strb	w1, [x0,142]
	b	.L2178
.L2184:
	sub	w0, w0, #36
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L2178
	adrp	x0, toshiba_3d_read_retrial
	add	x0, x0, :lo12:toshiba_3d_read_retrial
	str	x0, [x1,1168]
.L2214:
	mov	w0, 7
.L2215:
	strb	w0, [x1,1160]
.L2178:
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,716]
	tbz	x3, 0, .L2189
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L2217
.L2189:
	mov	w0, 4
.L2217:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	beq	.L2201
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,141]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L2191
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,1168]
.L2191:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,681]
	tbnz	x0, 2, .L2192
.L2193:
	mov	w0, 0
	b	.L2201
.L2192:
	ldr	x0, [x19,144]
	ldrb	w0, [x0,19]
	cbz	w0, .L2193
	add	x21, x21, :lo12:.LANCHOR4
	ldrh	w1, [x19,166]
	mov	w0, 0
	ldrb	w2, [x21,1184]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w20, [x19,716]
	cbnz	w20, .L2193
	ldr	x0, [x19,144]
	strb	w20, [x0,19]
	bl	flash_info_flush
	mov	w0, w20
	b	.L2201
.L2218:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 368
	ldrb	w1, [x0,28]
	cmp	w1, 3
	bne	.L2174
	strb	wzr, [x0,28]
	b	.L2174
.L2201:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
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
	stp	x21, x22, [sp,80]
	stp	x27, x28, [sp,128]
	stp	x19, x20, [sp,64]
	stp	x25, x26, [sp,112]
	bl	buf_alloc
	ldr	x23, [x0,8]
	mov	w27, 0
	adrp	x21, .LC119
	adrp	x22, .LC126
	mov	x26, x0
	mov	w19, w27
	adrp	x28, .LANCHOR0
	add	x21, x21, :lo12:.LC119
	add	x22, x22, :lo12:.LC126
	add	x23, x23, 704
.L2220:
	add	x3, x28, :lo12:.LANCHOR0
	ldrh	w1, [x3,3228]
	cmp	w1, w19
	bls	.L2230
	ldrh	w20, [x3,3270]
	ldr	x1, [x26,8]
	ldr	x2, [x26,24]
	madd	w20, w24, w20, w19
	ldrb	w3, [x3,2776]
	mov	w0, w20
	bl	ftl_read_ppa_page
	mov	w25, w0
	ldr	x7, [x26,8]
	mov	w1, w24
	ldr	x2, [x26,24]
	mov	w3, w20
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
	mov	x0, x21
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	mov	x0, x22
	mov	x1, x23
	mov	w2, 4
	mov	w3, 32
	bl	rknand_print_hex
	cmp	w25, 512
	beq	.L2226
	cmn	w25, #1
	bne	.L2221
.L2226:
	mov	w27, 1
.L2221:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2220
.L2230:
	mov	x0, x26
	bl	buf_free
	cbz	w27, .L2224
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 424
	mov	w2, 1204
	bl	printk
.L2225:
	b	.L2225
.L2224:
	sub	sp, x29, #48
	mov	w0, w27
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
	adrp	x3, .LANCHOR5
	adrp	x0, .LC127
	add	x29, sp, 0
	add	x3, x3, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC127
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w1, [x3,768]
	ldrb	w2, [x3,769]
	ldrh	w3, [x3,770]
	bl	printk
	ldr	x2, [x19,2800]
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,80]
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x19,3228]
	mov	w2, 4
	ldrb	w3, [x19,3196]
	ldr	x1, [x19,3792]
	mul	w3, w0, w3
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,72]
	adrp	x0, .LC133
	ldrh	w3, [x19,56]
	add	x0, x0, :lo12:.LC133
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x1, [x19,2800]
	adrp	x0, .LC126
	add	x0, x0, :lo12:.LC126
	mov	w2, 4
	add	x1, x1, 704
	mov	w3, 32
	bl	rknand_print_hex
	ldr	x1, [x19,64]
	adrp	x0, .LC134
	ldrh	w3, [x19,56]
	add	x0, x0, :lo12:.LC134
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC135
	add	x1, x19, 3280
	add	x0, x0, :lo12:.LC135
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3228]
	mov	w2, 2
	ldrb	w3, [x19,3196]
	ldr	x1, [x19,3800]
	mul	w3, w0, w3
	adrp	x0, .LC136
	add	x0, x0, :lo12:.LC136
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
	ldrh	w4, [x5,3266]
	ldrb	w3, [x5,3264]
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
	cbnz	w4, .L2236
	cmp	w3, w7
	bne	.L2233
.L2236:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC137
	add	x0, x0, :lo12:.LC137
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L2233:
	mov	w0, w4
	ldp	x29, x30, [sp], 16
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -64]!
	uxth	w1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	cmp	w1, 31
	uxth	w21, w0
	bls	.L2239
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 440
	mov	w2, 30
	bl	printk
.L2240:
	b	.L2240
.L2239:
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR7
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,2800]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L2241
	adrp	x0, .LC138
	mov	w1, w21
	add	x0, x0, :lo12:.LC138
	add	x19, x19, :lo12:.LANCHOR7
	bl	printk
	mov	x0, 0
	ldrh	w2, [x19,3218]
	mov	w1, 255
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,2800]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L2245
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 440
	mov	w2, 34
	bl	printk
.L2243:
	b	.L2243
.L2241:
	add	x0, x0, x21, sxtw 2
	sbfiz	x1, x1, 4, 32
	ldr	w22, [x0,704]
	add	x0, x20, 3824
	add	x23, x0, x1
	strh	w21, [x0,x1]
	strh	wzr, [x23,2]
	cbnz	w22, .L2244
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x23,8]
	mov	w1, 255
	ldrh	w2, [x19,3218]
	bl	ftl_memset
	b	.L2245
.L2244:
	add	x24, x19, :lo12:.LANCHOR7
	ldrb	w3, [x20,2776]
	ldr	x1, [x23,8]
	mov	w0, w22
	ldr	x2, [x24,3208]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x24,3208]
	ldr	w2, [x0]
	cmp	w2, w21
	beq	.L2246
	adrp	x0, .LC139
	mov	w4, w22
	mov	w1, w21
	add	x0, x0, :lo12:.LC139
	bl	printk
	ldr	x3, [x20,2800]
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x23,8]
	adrp	x0, .LC141
	add	x0, x0, :lo12:.LC141
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x1, [x24,3208]
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldrh	w0, [x20,3266]
	mov	w1, 21
	mov	w2, 1
	sub	w1, w1, w0
	lsr	w22, w22, w0
	lsl	w1, w2, w1
	ldrb	w0, [x20,3264]
	sub	w1, w1, #1
	and	w22, w1, w22
	mov	x1, 0
	udiv	w0, w22, w0
	bl	ftl_sblk_dump
.L2246:
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x19,3208]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L2245
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 440
	mov	w2, 55
	bl	printk
.L2247:
	b	.L2247
.L2245:
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
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR4
	stp	x21, x22, [sp,32]
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x19,2800]
	ldrh	w1, [x0,688]
	ldrh	w0, [x20,1084]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2250
	bl	pm_free_sblk
	ldr	x2, [x19,2800]
	ldrh	w1, [x20,1084]
	ldrh	w3, [x2,688]
	sub	w1, w1, #1
	cmp	w3, w1
	blt	.L2250
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w25, 65535
	cmp	w21, w25
	bne	.L2252
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 456
	mov	w2, 127
	bl	printk
.L2253:
	b	.L2253
.L2252:
	bl	pm_select_ram_region
	uxth	x20, w0
	add	x19, x19, 3824
	lsl	x0, x20, 4
	add	x24, x19, x0
	mov	x22, x20
	ldrh	w0, [x19,x0]
	cmp	w0, w25
	beq	.L2254
	ldr	x1, [x24,8]
	cbz	x1, .L2254
	ldrsh	w2, [x24,2]
	tbz	w2, #31, .L2254
	bl	pm_write_page
	ldrh	w0, [x24,2]
	and	w0, w0, 32767
	strh	w0, [x24,2]
.L2254:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w19, 0
	add	x0, x23, 3824
	mov	w24, 21
	mov	w25, 1
	add	x20, x0, x20, lsl 4
.L2255:
	ldr	x0, [x23,2800]
	ldrh	w1, [x0,698]
	cmp	w1, w19
	bls	.L2261
	ldrh	w1, [x23,3266]
	add	x0, x0, x19, sxtw 2
	ldr	w0, [x0,704]
	lsr	w0, w0, w1
	sub	w1, w24, w1
	lsl	w1, w25, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x23,3264]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L2256
	mov	w1, w22
	mov	w0, w19
	bl	load_l2p_region
	ldr	x1, [x20,8]
	mov	w0, w19
	bl	pm_write_page
.L2256:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2255
.L2261:
	bl	pm_free_sblk
.L2250:
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
	add	x1, x19, 3824
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,3816]
	cbz	w0, .L2263
	bl	pm_gc
	str	wzr, [x19,3816]
.L2263:
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
	add	x20, x20, 3824
.L2269:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2268
	bl	pm_flush_id
.L2268:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2269
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	mov	w23, w0
	mov	w0, 64
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	wzr, [x20,3816]
	bl	ftl_malloc
	adrp	x1, .LANCHOR7
	add	x20, x20, 3824
	add	x2, x1, :lo12:.LANCHOR7
	add	x22, x20, 512
	mov	x21, x1
	mov	w24, -1
	str	x0, [x2,3208]
.L2275:
	strh	w24, [x20]
	strh	wzr, [x20,2]
	cbz	w23, .L2274
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x20,8]
.L2274:
	add	x20, x20, 16
	cmp	x20, x22
	bne	.L2275
	add	x20, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR7
	mov	w4, 4
	ldr	x25, [x1,3208]
	ldr	x1, [x20,2800]
	ldr	x23, [x20,3832]
	mov	x3, x25
	ldrb	w0, [x1,694]
	mov	x2, x23
	ldrh	w1, [x1,692]
	bl	flash_get_last_written_page
	sxth	w21, w0
	ldr	x1, [x20,2800]
	mov	w24, w21
	ldrh	w2, [x1,696]
	cmp	w2, w21
	bgt	.L2276
	adrp	x0, .LC143
	ldrh	w1, [x1,692]
	mov	w3, w21
	add	x0, x0, :lo12:.LC143
	adrp	x26, .LC144
	add	w21, w21, 1
	bl	printk
	add	x26, x26, :lo12:.LC144
	ldr	x0, [x20,2800]
	ldrsh	w20, [x0,696]
.L2277:
	cmp	w20, w21
	bge	.L2290
	add	x27, x19, :lo12:.LANCHOR0
	mov	x2, x23
	mov	x3, x25
	ldr	x0, [x27,2800]
	ldrh	w22, [x27,3270]
	ldrb	w4, [x27,2776]
	ldrh	w1, [x0,692]
	ldrb	w0, [x0,694]
	madd	w22, w1, w22, w20
	mov	w1, w22
	bl	flash_read_page_en
	mov	w28, w0
	ldr	x3, [x27,2800]
	mov	x0, x26
	mov	w2, w22
	ldr	w1, [x3,48]
	add	w1, w1, 1
	str	w1, [x3,48]
	ldrh	w3, [x3,694]
	ldr	w1, [x25]
	bl	printk
	cmp	w28, 512
	beq	.L2278
	cmn	w28, #1
	beq	.L2278
	ldr	x1, [x27,2800]
	ldr	w0, [x25]
	ldrh	w2, [x1,698]
	cmp	w0, w2
	bcs	.L2278
	add	x0, x1, x0, uxtw 2
	str	w22, [x0,704]
.L2278:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2277
.L2290:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR6-192
	mov	w1, 1
	add	w24, w24, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-192]
	ldr	x0, [x19,2800]
	strh	w24, [x0,696]
	bl	pm_free_sblk
	ldr	x1, [x19,3832]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3832]
	mov	w0, -1
	bl	pm_write_page
.L2276:
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
	ldrb	w4, [x3,2776]
	ldr	w2, [x3,2784]
	add	x3, x3, 3824
	lsl	w20, w4, 7
	lsl	w24, w4, 7
	cmp	w0, w2
	udiv	w20, w0, w20
	uxth	w25, w20
	msub	w24, w25, w24, w0
	uxth	x24, w24
	bcc	.L2303
	adrp	x0, .LC145
	mov	w1, w5
	add	x0, x0, :lo12:.LC145
	bl	printk
	mov	w0, -1
	b	.L2293
.L2307:
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L2295
.L2294:
	add	w19, w19, 1
	add	x3, x3, 16
	uxth	x19, w19
	cmp	w19, 32
	beq	.L2306
.L2303:
	ldr	x0, [x3,8]
	cbnz	x0, .L2307
	b	.L2294
.L2306:
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR0
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 3824
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2308
.L2299:
	adrp	x0, .LANCHOR7+3220
	strb	w19, [x0,#:lo12:.LANCHOR7+3220]
	mov	w0, w25
	bl	load_l2p_region
.L2295:
	cbnz	w23, .L2296
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,3832]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x22]
	b	.L2297
.L2296:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x22]
	add	x0, x0, 3824
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w2, [x1,x24,lsl 2]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
	adrp	x0, .LANCHOR4-16
	strb	w20, [x0,#:lo12:.LANCHOR4-16]
.L2297:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 32767
	add	x21, x21, 3824
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2293
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2293
.L2308:
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2299
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
	b	.L2299
.L2293:
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
	stp	x21, x22, [sp,32]
	add	x22, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	ldr	w0, [x22,2820]
	cmn	w0, #1
	beq	.L2319
	mov	w0, 0
	bl	buf_alloc
	mov	x19, x0
	mov	w1, -1
	cbz	x0, .L2320
	ldr	w21, [x22,2820]
	str	w21, [x0,36]
	str	w1, [x22,2820]
	ldr	x23, [x0,8]
	ldr	x22, [x0,24]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2312
	mov	w0, w21
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2312:
	ldr	w1, [x29,76]
	str	w1, [x19,40]
	str	w1, [x19,44]
	cmn	w1, #1
	bne	.L2313
	add	x20, x20, :lo12:.LANCHOR0
	mov	x0, x23
	mov	w1, 0
	ldrb	w2, [x20,2776]
	lsl	w2, w2, 9
	bl	ftl_memset
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2319
.L2313:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w23, [x19,52]
	cmn	w23, #1
	bne	.L2314
	ldrb	w0, [x19,2]
	add	x20, x20, :lo12:.LANCHOR0
	and	w0, w0, -9
	strb	w0, [x19,2]
	ldr	x1, [x20,80]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2315
.L2314:
	cmp	w23, 256
	bne	.L2315
	add	x20, x20, :lo12:.LANCHOR0
	mov	w5, 21
	mov	w24, 1
	ldr	w4, [x29,76]
	ldr	w2, [x19,36]
	adrp	x0, .LC146
	ldrh	w3, [x20,3266]
	add	x0, x0, :lo12:.LC146
	ldrb	w20, [x20,3264]
	sub	w5, w5, w3
	lsr	w3, w4, w3
	lsl	w5, w24, w5
	sub	w5, w5, #1
	and	w3, w5, w3
	udiv	w20, w3, w20
	ldr	w3, [x19,40]
	uxth	w1, w20
	bl	printk
	mov	w0, w20
	mov	w1, w24
	mov	w2, 0
	bl	gc_add_sblk
.L2315:
	ldr	w5, [x22,4]
	cmp	w5, w21
	beq	.L2316
	adrp	x0, .LC147
	ldrb	w1, [x19,1]
	ldr	w3, [x29,76]
	add	x0, x0, :lo12:.LC147
	ldr	w4, [x22]
	mov	w2, w21
	ldr	w6, [x22,8]
	ldr	w7, [x22,12]
	bl	printk
.L2316:
	ldr	w0, [x22,4]
	cmp	w0, w21
	cset	w0, ne
	cbnz	w0, .L2322
	cmn	w23, #1
	bne	.L2310
.L2322:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 464
	mov	w2, 822
	bl	printk
.L2318:
	b	.L2318
.L2320:
	mov	w0, w1
	b	.L2310
.L2319:
	mov	w0, 0
.L2310:
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
	strb	wzr, [x24,3193]
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x20, [x24,80]
	add	x21, x20, 80
	strb	wzr, [x0,1050]
	bl	gc_init
	ldrh	w1, [x20,80]
	mov	w0, 65535
	str	x22, [x29,152]
	cmp	w1, w0
	ldr	x0, [x24,80]
	beq	.L2325
	mov	w1, -1
	strh	w1, [x0,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldrb	w0, [x21,9]
	ldrh	w1, [x24,3270]
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x24,3177]
	str	w0, [x29,168]
	cmp	w1, 3
	bne	.L2326
	ldrh	w1, [x24,3228]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
	b	.L2372
.L2326:
	cmp	w1, 2
	bne	.L2327
	ldrh	w1, [x24,3228]
	add	w0, w0, w1, lsl 1
	sub	w0, w0, #1
	orr	w0, w0, 33554432
.L2372:
	str	w0, [x23,40]
.L2327:
	mov	x0, x23
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2328
	cmn	w0, #1
	beq	.L2328
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2329
.L2328:
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
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x23,40]
	ldr	w2, [x23,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2375
.L2329:
	add	x24, x19, :lo12:.LANCHOR0
	ldr	x0, [x29,152]
	ldr	x1, [x23,8]
	add	x25, x0, :lo12:.LANCHOR4
	ldrb	w2, [x24,3196]
	ldrh	w0, [x24,3194]
	mul	w2, w0, w2
	ldr	x0, [x25,1056]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x24,3194]
	ldrb	w2, [x24,3196]
	ldr	x1, [x23,8]
	mul	w2, w0, w2
	add	x1, x1, x2, sxtw 2
	ldr	x0, [x25,1064]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w27, [x24,3177]
	ldrh	w26, [x24,3228]
	cmp	w27, 2
	bne	.L2331
	lsl	w26, w26, 1
	mov	w27, 1
.L2331:
	mov	w22, 0
	adrp	x24, .LC149
	add	x0, x24, :lo12:.LC149
	mov	w25, w22
	str	x0, [x29,128]
	b	.L2332
.L2377:
	ldr	x0, [x0,1064]
	ldr	w1, [x0,x12]
	ldr	w0, [x7,8]
	cmp	w1, w0
	bne	.L2336
.L2361:
	add	w25, w25, 1
	add	w28, w28, 1
.L2340:
	cmp	w28, w27
	bhi	.L2376
	ldr	x0, [x29,144]
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w2, [x1,3270]
	ldrb	w1, [x1,3177]
	ldrh	w0, [x21,x0,lsl 1]
	cmp	w1, 3
	mul	w0, w0, w2
	str	w0, [x29,168]
	bne	.L2333
	add	w0, w0, w22
	orr	w0, w0, w28, lsl 24
.L2373:
	str	w0, [x23,40]
	mov	w1, 1
	mov	x0, x23
	bl	sblk_read_page
	ldr	x0, [x29,152]
	sbfiz	x12, x25, 2, 32
	ldr	x7, [x23,24]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w6, [x7,4]
	ldr	x1, [x0,1056]
	ldr	w3, [x1,x12]
	cmp	w3, w6
	beq	.L2377
.L2336:
	ldr	x0, [x29,152]
	str	x12, [x29,120]
	add	x13, x0, :lo12:.LANCHOR4
	ldr	w0, [x7,12]
	str	x13, [x29,112]
	ldr	x4, [x13,1064]
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
	ldr	x0, [x13,1056]
	ldr	w0, [x0,x12]
	cmn	w0, #1
	beq	.L2361
	mov	x0, x23
	bl	buf_free
.L2375:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	mov	w28, 0
	ldr	x1, [x0,72]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,80]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L2330
.L2339:
	add	w22, w22, 1
.L2332:
	cmp	w22, w26
	beq	.L2341
	sub	w0, w22, #1
	mov	w24, 0
	str	w0, [x29,140]
.L2342:
	ldrb	w0, [x21,9]
	cmp	w24, w0
	bge	.L2339
	sxtw	x0, w24
	mov	w28, 1
	str	x0, [x29,144]
	b	.L2340
.L2333:
	cmp	w1, 2
	bne	.L2335
	ldr	w1, [x29,140]
	add	w0, w0, w1
	add	w0, w0, w28
	orr	w0, w0, 33554432
	b	.L2373
.L2335:
	add	w0, w0, w22
	b	.L2373
.L2376:
	add	w24, w24, 1
	b	.L2342
.L2341:
	mov	x0, x23
	mov	w28, 0
	mov	w23, w28
	bl	buf_free
	str	w28, [x29,144]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,9]
	ldrh	w3, [x20,80]
	ldr	x2, [x0,72]
	ldrh	w0, [x0,3194]
	mul	w0, w1, w0
	strh	w0, [x2,x3,lsl 1]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29,104]
.L2343:
	cmp	w23, w26
	beq	.L2356
	sub	w0, w23, #1
	mov	w25, 0
	str	w0, [x29,112]
.L2357:
	ldrb	w0, [x21,9]
	cmp	w25, w0
	bge	.L2354
	sxtw	x0, w25
	mov	w24, 1
	str	x0, [x29,120]
.L2355:
	cmp	w24, w27
	bhi	.L2378
	ldr	x1, [x29,152]
	ldr	w0, [x29,144]
	add	x1, x1, :lo12:.LANCHOR4
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1056]
	ldr	w22, [x2,x0]
	cmn	w22, #1
	beq	.L2344
	ldr	x1, [x1,1064]
	ldr	w0, [x1,x0]
	str	w0, [x29,140]
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2345
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2345:
	ldr	x0, [x29,120]
	add	x0, x0, 8
	ldrh	w3, [x21,x0,lsl 1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,3270]
	ldrb	w0, [x0,3177]
	cmp	w0, 3
	mul	w3, w3, w1
	bne	.L2346
	add	w3, w3, w23
	orr	w3, w3, w24, lsl 24
	b	.L2374
.L2346:
	cmp	w0, 2
	bne	.L2348
	ldr	w0, [x29,112]
	add	w3, w3, w0
	add	w3, w3, w24
	orr	w3, w3, 33554432
	b	.L2374
.L2348:
	add	w3, w3, w23
.L2374:
	add	x0, x19, :lo12:.LANCHOR0
	str	w3, [x29,168]
	ldr	w1, [x29,140]
	mov	w4, 21
	mov	w2, 1
	ldrh	w3, [x0,3266]
	ldrb	w0, [x0,3264]
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
	bne	.L2349
	ldr	x0, [x29,104]
	mov	w1, w22
	ldr	w2, [x29,168]
	bl	pm_ppa_update_check
	cbz	w0, .L2350
.L2371:
	b	.L2371
.L2350:
	mov	w0, w22
	add	x1, x29, 168
	mov	w2, 1
	add	w28, w28, 1
	bl	pm_log2phys
	ldrh	w0, [x29,128]
	bl	ftl_vpn_decrement
	b	.L2352
.L2349:
	ldr	w1, [x29,168]
	cmp	w0, w1
	csinc	w28, w28, w28, ne
.L2352:
	ldr	w0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,144]
	ldr	w0, [x29,128]
	ldr	x1, [x2,64]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L2344
	lsl	x0, x0, 1
	ldr	x2, [x2,72]
	ldrh	w3, [x2,x0]
	cbz	w3, .L2344
	strh	w1, [x2,x0]
.L2344:
	add	w24, w24, 1
	b	.L2355
.L2378:
	add	w25, w25, 1
	b	.L2357
.L2354:
	add	w23, w23, 1
	b	.L2343
.L2356:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,72]
	strh	w28, [x0,x1,lsl 1]
.L2330:
	ldrh	w1, [x20,80]
	adrp	x0, .LC151
	mov	w2, w28
	add	x0, x0, :lo12:.LC151
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
	beq	.L2358
	bl	ftl_free_sblk
.L2358:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,2800]
	strh	w0, [x1,126]
	ldr	x1, [x19,80]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2324
.L2325:
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2324
	ldr	x1, [x24,2800]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L2360
	bl	pm_flush
	ldr	x0, [x24,80]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x24,2800]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L2360:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,80]
	strh	w1, [x0,130]
.L2324:
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
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR5+449
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x19, :lo12:.LANCHOR0
	strb	w0, [x1,#:lo12:.LANCHOR5+449]
	adrp	x1, .LC9
	add	x1, x1, :lo12:.LC9
	strb	w0, [x20,2818]
	mov	w0, -1
	str	w0, [x20,2820]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	strb	wzr, [x20,2819]
	strb	wzr, [x20,3812]
	bl	printk
	adrp	x5, .LANCHOR2
	ldrb	w4, [x20,152]
	add	x5, x5, :lo12:.LANCHOR2
	adrp	x8, .LANCHOR7
	strb	w4, [x20,3272]
	add	x3, x8, :lo12:.LANCHOR7
	ldrb	w2, [x5,381]
	ldrb	w6, [x5,380]
	ldrh	w1, [x5,378]
	mul	w4, w2, w4
	ldrb	w0, [x5,377]
	ldrh	w7, [x5,382]
	sdiv	w9, w1, w6
	ldrh	w5, [x20,166]
	strb	w2, [x20,3264]
	uxtb	w4, w4
	strb	w6, [x20,3177]
	ubfiz	w11, w0, 9, 7
	strb	w0, [x20,2776]
	mov	w2, 0
	strh	w1, [x20,3194]
	strh	w9, [x20,3228]
	strb	w4, [x20,3196]
	strh	w7, [x20,56]
	strh	w11, [x3,3218]
	mov	w3, 1
	strh	w5, [x20,3270]
	mov	x20, x8
.L2380:
	cmp	w3, w5
	bhi	.L2400
	add	w2, w2, 1
	lsl	w3, w3, 1
	uxth	w2, w2
	b	.L2380
.L2400:
	add	x8, x19, :lo12:.LANCHOR0
	sub	w2, w2, #1
	mul	w3, w0, w1
	add	x10, x20, :lo12:.LANCHOR7
	strh	w2, [x8,3266]
	mov	w2, 57344
	movk	w2, 0x1c, lsl 16
	mul	w5, w3, w7
	mul	w2, w4, w2
	lsr	w1, w5, 21
	mul	w5, w5, w4
	mul	w2, w2, w1
	str	w2, [x8,4]
	add	w2, w2, 24576
	str	w5, [x8,2780]
	adrp	x5, .LANCHOR4
	str	w2, [x10,3224]
	udiv	w1, w2, w0
	sub	w0, w11, #1
	str	w1, [x8,2784]
	add	w0, w0, w1, lsl 2
	mul	w1, w9, w4
	udiv	w0, w0, w11
	add	x11, x5, :lo12:.LANCHOR4
	strh	w0, [x10,3202]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w1
	mul	w1, w3, w4
	strh	w0, [x11,1084]
	sub	w1, w1, #1
	add	w0, w7, w7, lsl 1
	asr	w0, w0, 2
	strh	w0, [x10,3228]
	udiv	w2, w2, w1
	lsr	w0, w7, 4
	cmp	w0, 79
	strh	w0, [x8,3232]
	add	w2, w2, 8
	strh	w2, [x8,3234]
	mov	x0, x5
	bhi	.L2382
	mov	w1, 80
	strh	w1, [x8,3232]
.L2382:
	add	x1, x20, :lo12:.LANCHOR7
	mov	w2, 4000
	cmp	w6, 2
	strh	w2, [x1,3230]
	mov	w2, 50
	strh	w2, [x1,3232]
	add	x1, x0, :lo12:.LANCHOR4
	mov	w2, 1024
	strh	w2, [x1,1080]
	mov	w2, 64
	strh	w2, [x1,1082]
	beq	.L2383
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w1, [x1,3192]
	cbz	w1, .L2384
.L2383:
	add	x1, x20, :lo12:.LANCHOR7
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 150
	strh	w2, [x1,3232]
	mov	w2, 128
	strh	w2, [x0,1082]
	add	x2, x19, :lo12:.LANCHOR0
	ldrb	w2, [x2,164]
	cbnz	w2, .L2384
	mov	w2, 600
	strh	w2, [x1,3230]
	mov	w1, 512
	strh	w1, [x0,1080]
.L2384:
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_init
	mov	w0, 6
	adrp	x21, .LANCHOR6
	add	x21, x21, :lo12:.LANCHOR6
	add	x20, x20, :lo12:.LANCHOR7
	ldrh	w1, [x19,56]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,8]
	ldrb	w1, [x19,3196]
	ldrh	w0, [x19,3228]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3800]
	ldrb	w1, [x19,3196]
	sub	x3, x21, #128
	ldrh	w0, [x19,3228]
	str	x3, [x19,3792]
	mul	w0, w0, w1
	lsl	w2, w0, 1
	add	x1, x3, x2, sxtw 2
	str	x1, [x19,72]
	ldrh	w1, [x19,56]
	add	w0, w1, w0, lsl 2
	add	w2, w2, w1, lsr 1
	add	x2, x3, x2, sxtw 2
	lsl	w0, w0, 1
	str	x2, [x19,80]
	ldrh	w2, [x20,3218]
	add	w0, w0, 612
	cmp	w0, w2
	bhi	.L2387
	ldrh	w0, [x20,3202]
	lsl	w0, w0, 2
	add	w1, w0, w1, lsl 2
	add	w1, w1, 704
	cmp	w1, w2
	bls	.L2386
.L2387:
.L2388:
	b	.L2388
.L2386:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L2389
	bl	ftl_ext_info_init
	mov	w0, 1
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
	ldr	x1, [x19,2800]
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
	ldrb	w0, [x21,-192]
	cbz	w0, .L2390
	ldr	x1, [x19,2800]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L2390:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L2389:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
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
	str	x0, [x19,1120]
	mov	w0, 136
	bl	ftl_malloc
	str	x0, [x19,1128]
	cbnz	x0, .L2402
.L2404:
	mov	w0, -1
	b	.L2403
.L2402:
	adrp	x21, .LANCHOR7
	mov	w0, 2048
	add	x21, x21, :lo12:.LANCHOR7
	bl	ftl_malloc
	str	xzr, [x19,1136]
	add	x1, x21, 3248
	str	x0, [x21,3240]
	add	x0, x19, 1136
	str	xzr, [x21,3248]
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1136]
	cbz	x0, .L2404
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x21,3240]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1128]
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
	ldr	x0, [x19,1136]
	bl	nand_flash_init
	cbnz	w0, .L2405
	bl	zftl_init
	b	.L2412
.L2405:
	ldr	x0, [x19,1128]
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
	ldr	x0, [x19,1136]
	bl	FlashInit
	mov	w19, w0
	adrp	x0, .LANCHOR2
	adrp	x1, gNandParaInfo
	add	x0, x0, :lo12:.LANCHOR2
	add	x1, x1, :lo12:gNandParaInfo
	add	x0, x0, 368
	mov	w2, 32
	bl	ftl_memcpy
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0,#:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L2407
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0,#:lo12:gFlashSlcMode]
	cbnz	w0, .L2408
.L2407:
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x0, .LANCHOR0+164
	strb	wzr, [x20,1208]
	strb	wzr, [x0,#:lo12:.LANCHOR0+164]
.L2408:
	cbnz	w19, .L2406
	bl	FtlInit
.L2412:
	mov	w19, w0
.L2406:
	adrp	x0, .LC152
	mov	w1, w19
	add	x0, x0, :lo12:.LC152
	bl	printk
	mov	w0, w19
.L2403:
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
	adrp	x20, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR0
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	x24, [x0,80]
	ldrh	w1, [x0,3194]
	add	x28, x24, 80
	ldrb	w0, [x28,9]
	mul	w0, w1, w0
	str	w0, [x29,140]
	bl	timer_get_time
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2414
	adrp	x0, .LC153
	ldrh	w1, [x24,80]
	add	x0, x0, :lo12:.LC153
	bl	printk
.L2414:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x24,80]
	ldr	w2, [x29,140]
	mov	w23, 0
	mov	w21, w23
	ldr	x0, [x0,72]
	sub	w2, w2, #1
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	str	x0, [x29,120]
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	str	x0, [x29,112]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	str	x0, [x29,104]
.L2415:
	ldr	w0, [x29,140]
	cmp	w21, w0
	beq	.L2450
	adrp	x22, .LANCHOR4
	str	x22, [x29,128]
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x0, [x0,1056]
	ldr	w2, [x0,w21,sxtw 2]
	cmn	w2, #1
	beq	.L2416
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,136]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2417
	ldr	x0, [x29,104]
	mov	w3, w21
	ldr	w1, [x29,136]
	bl	printk
.L2417:
	mov	w25, w21
.L2423:
	ldr	x0, [x29,128]
	sbfiz	x26, x25, 2, 32
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x0, [x1,1056]
	ldr	w22, [x0,x26]
	cmn	w22, #1
	beq	.L2418
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 7
	udiv	w0, w22, w0
	cmp	w2, w0, uxth
	bne	.L2418
	ldr	x0, [x1,1064]
	ldr	w27, [x0,x26]
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L2419
	mov	w0, w22
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L2419:
	ldr	w3, [x29,156]
	cmp	w3, w27
	bne	.L2420
	ldr	x0, [x29,128]
	add	x1, x29, 152
	mov	w2, 1
	add	w23, w23, 1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1072]
	ldr	w0, [x0,x26]
	str	w0, [x29,152]
	mov	w0, w22
	bl	pm_log2phys
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w0, 1
	ldrh	w3, [x1,3266]
	sub	w2, w2, w3
	lsr	w3, w27, w3
	lsl	w2, w0, w2
	sub	w2, w2, #1
	and	w3, w2, w3
	ldrb	w2, [x1,3264]
	udiv	w2, w3, w2
	uxth	w2, w2
	str	x2, [x29,96]
	mov	w0, w2
	bl	ftl_vpn_decrement
	ldrh	w0, [x28]
	ldr	x2, [x29,96]
	cmp	w0, w2
	bne	.L2421
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2421
	ldr	x0, [x29,112]
	mov	w1, w22
	ldr	w3, [x29,156]
	mov	w2, w27
	bl	printk
	b	.L2421
.L2420:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2422
	ldr	x0, [x29,120]
	mov	w1, w22
	mov	w2, w27
	bl	printk
.L2422:
	ldrh	w0, [x28]
	bl	ftl_vpn_decrement
.L2421:
	ldr	x0, [x29,128]
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1056]
	str	w1, [x0,x26]
	bl	timer_get_time
.L2418:
	ldr	w0, [x29,140]
	add	w25, w25, 1
	cmp	w25, w0
	bne	.L2423
.L2416:
	add	w21, w21, 1
	b	.L2415
.L2450:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,400]
	tbz	x0, 8, .L2425
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x24,80]
	ldr	x1, [x0,80]
	ldr	x2, [x0,72]
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w23
	bl	printk
.L2425:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x24,80]
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x19,72]
	ldrh	w1, [x2,x1]
	cmp	w1, w23
	beq	.L2426
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 480
	mov	w2, 463
	bl	printk
.L2427:
	b	.L2427
.L2426:
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
	add	x2, x3, 2840
	stp	x21, x22, [sp,32]
	ldrb	w0, [x2,4]
	add	x1, x2, x0, sxtw 1
	ldrb	w7, [x3,3196]
	ldrh	w1, [x1,40]
	ldrh	w3, [x2,2]
.L2452:
	cmp	w1, w6
	bne	.L2494
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w7
	bne	.L2453
	add	w3, w3, 1
	mov	w4, 1
	mov	w0, 0
	uxth	w3, w3
.L2453:
	add	x1, x2, x0, sxtw 1
	mov	w5, 1
	ldrh	w1, [x1,40]
	b	.L2452
.L2494:
	cbz	w5, .L2455
	add	x2, x19, :lo12:.LANCHOR0
	strb	w0, [x2,2844]
.L2455:
	cbz	w4, .L2456
	add	x0, x19, :lo12:.LANCHOR0
	strh	w3, [x0,2842]
.L2456:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,3177]
	ldrh	w20, [x0,3270]
	cmp	w2, 2
	mul	w1, w1, w20
	bne	.L2457
	ldrb	w2, [x0,2846]
	add	x0, x0, 2840
	cbnz	w2, .L2457
	ldrh	w20, [x0,2]
	add	w20, w1, w20, lsl 1
	b	.L2458
.L2457:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w20, [x0,2842]
	add	w20, w1, w20
.L2458:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2840
	ldrb	w0, [x2,2844]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x2,2844]
	ldrb	w2, [x2,3196]
	cmp	w2, w0
	bne	.L2459
	ldrh	w0, [x1,2]
	strb	wzr, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L2459:
	mov	w0, 1
	mov	w21, 0
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x22, x0
.L2470:
	ldrb	w0, [x19,2846]
	cbnz	w21, .L2472
	cmp	w0, 3
	beq	.L2460
.L2472:
	cmp	w0, w21
	bgt	.L2462
.L2471:
	mov	x0, x22
	bl	buf_free
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L2462:
	ldrb	w1, [x19,3177]
	cmp	w1, 2
	bne	.L2463
	cbnz	w0, .L2463
	add	w0, w21, w20
	orr	w0, w0, 33554432
	b	.L2493
.L2463:
	orr	w0, w20, w21, lsl 24
.L2493:
	str	w0, [x22,40]
	mov	w1, 1
	mov	x0, x22
	bl	sblk_read_page
	ldr	w0, [x22,52]
	cmp	w0, 512
	beq	.L2467
	cmn	w0, #1
	beq	.L2467
	ldr	x0, [x22,24]
	ldr	w23, [x0,4]
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2468
	mov	w0, w23
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2468:
	ldr	w0, [x29,76]
	cmp	w20, w0
	bne	.L2467
	ldrh	w2, [x19,2864]
	ldr	x1, [x19,3168]
	str	w20, [x1,x2,lsl 2]
	ldrh	w1, [x19,2864]
	add	w1, w1, 1
	strh	w1, [x19,2864]
.L2467:
	ldrh	w0, [x19,2866]
	add	w0, w0, 1
	strh	w0, [x19,2866]
.L2460:
	add	w21, w21, 1
	cmp	w21, 4
	bne	.L2470
	b	.L2471
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
	beq	.L2496
	mov	w20, 11
	mov	w21, 21
	mov	w22, 1
.L2497:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,80]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2498
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L2499
	ldrh	w1, [x24,3266]
	ldr	w2, [x29,76]
	sub	w3, w21, w1
	ldrb	w0, [x24,3264]
	lsl	w3, w22, w3
	lsr	w1, w2, w1
	sub	w3, w3, #1
	mov	w2, 0
	and	w1, w3, w1
	udiv	w0, w1, w0
	mov	w1, 1
	bl	gc_add_sblk
.L2499:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,80]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L2500
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 504
	mov	w2, 1369
	bl	printk
.L2501:
	b	.L2501
.L2500:
	mov	x0, x23
	bl	buf_free
.L2498:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,80]
	ldr	w2, [x2,2784]
	ldr	w1, [x0,544]
	add	w1, w1, 1
	str	w1, [x0,544]
	cmp	w1, w2
	bcc	.L2502
	mov	w1, -1
	str	w1, [x0,544]
	ldr	w1, [x0,548]
	add	w1, w1, 1
	str	w1, [x0,548]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2495
.L2502:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L2495
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L2497
	b	.L2495
.L2496:
	ldr	x1, [x1,2800]
	ldr	w2, [x1,40]
	cmp	w2, 20480
	bls	.L2507
	ldr	w3, [x1,44]
	add	w3, w3, w2, lsr 10
	and	w2, w2, 1023
	str	w3, [x1,44]
	str	w2, [x1,40]
.L2507:
	ldr	w2, [x0,536]
	ldr	w3, [x0,12]
	add	w2, w2, 12959744
	add	w2, w2, 256
	cmp	w3, w2
	bhi	.L2508
	ldr	w2, [x0,540]
	ldr	w4, [x1,44]
	add	w2, w2, 98304
	add	w2, w2, 1696
	cmp	w4, w2
	bls	.L2495
.L2508:
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w3, [x0,536]
	str	wzr, [x0,544]
.L2495:
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
	bcs	.L2513
	bl	timer_get_time
	ldr	x21, [x20,80]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L2513
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,80]
	ldrh	w2, [x20,56]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L2519
	ldr	x2, [x20,2800]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L2520
.L2519:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2800]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L2520:
	ldr	w24, [x0,600]
	add	w1, w24, 1
	str	w1, [x0,600]
	ldr	w1, [x0,608]
	uxth	w20, w24
	add	w1, w1, 1
	str	w1, [x0,608]
	add	x1, x29, 128
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	uxth	w1, w0
	cbz	w1, .L2513
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x22, w20
	ldr	x25, [x2,64]
	add	x25, x25, x22, lsl 2
	ldrb	w1, [x25,2]
	tst	w1, 192
	and	w3, w1, 224
	beq	.L2521
	cmp	w3, 224
	beq	.L2521
	ldr	x1, [x2,80]
	ldrh	w4, [x1,16]
	cmp	w4, w20
	beq	.L2521
	ldrh	w4, [x1,48]
	cmp	w4, w20
	beq	.L2521
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L2522
.L2521:
	cbnz	w3, .L2513
	add	x19, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x19,72]
	ldrh	w1, [x0,x22]
	cbz	w1, .L2523
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 528
	mov	w2, 1606
	bl	printk
.L2524:
	b	.L2524
.L2523:
	strh	w3, [x0,x22]
	b	.L2513
.L2522:
	add	x1, x29, 144
	and	w0, w0, 65535
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w1, [x0,-16]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2526
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 528
	mov	w2, 1612
	bl	printk
.L2527:
	b	.L2527
.L2526:
	ldrh	w0, [x2,3228]
	cmp	w3, 160
	ldrh	w4, [x2,3270]
	mov	w26, 1
	sub	w0, w0, #1
	mul	w1, w1, w4
	uxth	w0, w0
	bne	.L2528
	ldrb	w3, [x2,3177]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2528
	ldrh	w0, [x2,3194]
	sub	w0, w0, #1
	uxth	w0, w0
.L2528:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x19, :lo12:.LANCHOR0
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,3194]
	mov	w1, 255
	ldrb	w2, [x21,3196]
	mul	w2, w0, w2
	ldr	x0, [x21,3168]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2540
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2529
.L2540:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L2550
.L2529:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2531
	mov	w0, w20
	mov	w1, 1
.L2550:
	bl	gc_add_sblk
	mov	x0, x23
	bl	buf_free
	b	.L2513
.L2531:
	ldrb	w0, [x21,3196]
	mov	x27, 0
	ldrh	w1, [x21,3228]
	and	w28, w24, 65535
	mov	w21, w27
	mul	w0, w0, w1
	mul	w26, w26, w0
	uxth	w0, w26
	ldr	x26, [x23,8]
	str	w0, [x29,108]
.L2532:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L2551
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L2533
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2534
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2534:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 21
	ldrh	w0, [x4,3266]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x29,124]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3264]
	and	w0, w2, w0
	udiv	w0, w0, w1
	cmp	w0, w28
	bne	.L2533
	add	w21, w21, 1
	uxth	w21, w21
.L2533:
	add	x27, x27, 1
	b	.L2532
.L2551:
	mov	x0, x23
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2536
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x25,2]
	uxth	w1, w24
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,72]
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L2536:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,72]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L2513
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L2513:
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
	stp	x29, x30, [sp,48]
	add	x29, sp, 48
	stp	x23, x24, [sp,96]
	uxth	w23, w0
	mov	w0, 65535
	stp	x19, x20, [sp,64]
	stp	x21, x22, [sp,80]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	cmp	w23, w0
	beq	.L2574
	adrp	x25, .LANCHOR0
	uxtw	x0, w23
	add	x2, x25, :lo12:.LANCHOR0
	lsl	x19, x0, 2
	str	x0, [x29,136]
	mov	w26, 1
	str	x25, [x29,152]
	ldr	x0, [x2,64]
	add	x0, x0, x19
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2554
	ldrb	w26, [x2,3177]
.L2554:
	mov	x27, x1
	mov	w0, w23
	add	x1, x29, 192
	strh	w23, [x29,176]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	ldr	x0, [x29,152]
	strb	w1, [x29,185]
	add	x0, x0, :lo12:.LANCHOR0
	strh	wzr, [x29,178]
	strb	wzr, [x29,181]
	strh	wzr, [x29,186]
	ldrh	w2, [x0,3228]
	mul	w1, w1, w2
	strh	w1, [x29,182]
	adrp	x1, .LANCHOR2
	add	x2, x1, :lo12:.LANCHOR2
	str	x1, [x29,128]
	ldr	w2, [x2,400]
	tbz	x2, 12, .L2555
	ldr	x0, [x0,64]
	mov	w1, w23
	mov	w3, w26
	uxtb	w4, w4
	add	x19, x0, x19
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	ldrb	w2, [x19,2]
	ubfx	x2, x2, 5, 3
	bl	printk
.L2555:
	mov	w0, 1
	mov	w21, 0
	mov	w28, w21
	mov	w19, w21
	bl	buf_alloc
	mov	x25, x0
	adrp	x0, .LC119
	str	w21, [x29,148]
	add	x0, x0, :lo12:.LC119
	str	x0, [x29,120]
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	str	x0, [x29,112]
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	str	x0, [x29,104]
.L2556:
	ldr	x0, [x29,152]
	add	x20, x0, :lo12:.LANCHOR0
	ldrh	w0, [x20,3228]
	cmp	w0, w19
	bls	.L2571
	lsl	w0, w19, 1
	mov	w24, 0
	sub	w0, w0, #1
	str	w0, [x29,144]
.L2572:
	ldrb	w0, [x29,185]
	cmp	w0, w24
	bls	.L2569
	mov	w22, 1
.L2570:
	cmp	w22, w26
	bhi	.L2598
	add	x0, x29, 176
	add	x0, x0, x24, sxtw 1
	ldrh	w3, [x0,16]
	mov	w0, 65535
	cmp	w3, w0
	beq	.L2557
	ldr	x0, [x29,152]
	cmp	w26, 3
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,3270]
	mul	w2, w3, w2
	add	w20, w2, w19
	bne	.L2558
	orr	w20, w20, w22, lsl 24
	b	.L2559
.L2558:
	cmp	w26, 2
	bne	.L2559
	ldr	w1, [x29,144]
	ldrb	w20, [x0,3177]
	add	w2, w2, w1
	add	w2, w2, w22
	orr	w20, w2, w20, lsl 24
.L2559:
	mov	w1, 1
	str	w20, [x25,40]
	mov	x0, x25
	str	x3, [x29,96]
	bl	sblk_read_page
	ldr	x7, [x25,8]
	mov	w2, w19
	ldr	x0, [x25,24]
	ldr	w9, [x25,52]
	ldr	w1, [x7,12]
	str	w1, [sp]
	mov	w4, w9
	ldr	x3, [x29,96]
	ldr	w1, [x0]
	str	w1, [sp,8]
	str	x9, [x29,96]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w3
	mov	w3, w20
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	ldr	x0, [x29,120]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x9, [x29,96]
	cmp	w9, 512
	beq	.L2576
	cmn	w9, #1
	bne	.L2561
.L2576:
	mov	w0, 1
	str	w0, [x29,148]
.L2561:
	ldr	x0, [x25,24]
	ldr	w1, [x0,4]
	str	x1, [x29,96]
	mov	w0, w1
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x1, [x29,96]
	bne	.L2563
	mov	w0, w1
	mov	w2, 0
	add	x1, x29, 172
	bl	pm_log2phys
.L2563:
	ldr	w0, [x29,172]
	cmp	w0, w20
	bne	.L2564
	ldr	x2, [x25,24]
	add	w21, w21, 1
	ldr	x0, [x29,104]
	mov	w1, w20
	mov	w3, w21
	ldr	w2, [x2,4]
	bl	printk
.L2564:
	cbz	x27, .L2565
	ubfiz	x20, x28, 2, 32
	ldr	x0, [x25,24]
	ldr	w2, [x27,x20]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L2566
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2566
	ldr	x0, [x29,112]
	mov	w1, w28
	bl	printk
.L2566:
	ldr	x1, [x25,24]
	ldr	w0, [x27,x20]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L2565
	cmn	w0, #1
	beq	.L2565
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 552
	mov	w2, 1165
	bl	printk
.L2567:
	b	.L2567
.L2565:
	add	w28, w28, 1
.L2557:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2570
.L2598:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2572
.L2569:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2556
.L2571:
	mov	x0, x25
	bl	buf_free
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2573
	ldr	x3, [x29,136]
	adrp	x0, .LC162
	ldr	x2, [x20,72]
	add	x0, x0, :lo12:.LC162
	mov	w1, w23
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w21
	bl	printk
.L2573:
	ldr	w0, [x29,148]
	b	.L2553
.L2574:
	mov	w0, 0
.L2553:
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
	adrp	x0, .LANCHOR2+400
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	mov	w24, w1
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w25, w2
	mov	x23, x3
	tbz	x0, 12, .L2600
	adrp	x0, .LC163
	mov	w1, w19
	add	x0, x0, :lo12:.LC163
	mov	w2, w24
	mov	w3, w25
	bl	printk
.L2600:
	cbnz	w19, .L2601
	adrp	x0, .LANCHOR0+4
	mov	w19, 24576
	ldr	w0, [x0,#:lo12:.LANCHOR0+4]
	b	.L2602
.L2601:
	cmp	w19, 3
	bhi	.L2628
	lsl	w19, w19, 13
	mov	w0, 8192
.L2602:
	add	w1, w24, w25
	cmp	w1, w0
	bhi	.L2628
	adrp	x22, .LANCHOR0
	add	w24, w19, w24
	add	x1, x22, :lo12:.LANCHOR0
	add	w28, w24, w25
	str	wzr, [x29,152]
	ldr	x2, [x1,2800]
	ldr	w0, [x2,24]
	add	w0, w0, w25
	str	w0, [x2,24]
	ldrb	w0, [x1,2776]
	sub	w1, w28, #1
	udiv	w27, w24, w0
	mov	w21, w27
	udiv	w0, w1, w0
	str	w0, [x29,148]
	sub	w26, w0, w27
	add	w0, w26, 1
	str	w0, [x29,156]
	uxth	w0, w25
	str	w0, [x29,144]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	str	x0, [x29,136]
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	str	x0, [x29,128]
.L2604:
	ldr	w0, [x29,156]
	cbz	w0, .L2648
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,148]
	cmp	w21, w27
	cset	w2, eq
	cmp	w21, w1
	ldrb	w0, [x0,2776]
	cset	w20, eq
	uxth	w19, w0
	cbnz	w20, .L2631
	cbz	w2, .L2605
	b	.L2647
.L2631:
	cbz	w2, .L2607
.L2647:
	udiv	w20, w24, w0
	msub	w20, w20, w0, w24
	ldr	w0, [x29,144]
	uxth	w20, w20
	sub	w19, w19, w20
	uxth	w19, w19
	cmp	w19, w25
	csel	w19, w0, w19, hi
	b	.L2605
.L2607:
	msub	w19, w0, w21, w28
	mov	w20, w2
	uxtb	w19, w19
.L2605:
	add	x1, x22, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 728
.L2610:
	ldr	w2, [x1,36]
	cmp	w2, w21
	bne	.L2608
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L2608
	add	x2, x22, :lo12:.LANCHOR0
	ubfiz	x1, x0, 6, 32
	add	x0, x2, x1
	ubfiz	x20, x20, 9, 16
	lsl	w2, w19, 9
	ubfiz	x19, x19, 9, 16
	ldr	x1, [x0,736]
	mov	x0, x23
	add	x23, x23, x19
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L2609
.L2608:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L2610
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2611
	mov	w0, w21
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2611:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L2612
	mov	w19, 0
.L2613:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	cmp	w19, w0
	bcs	.L2609
	madd	w0, w21, w0, w19
	cmp	w0, w24
	bcc	.L2614
	cmp	w0, w28
	bcs	.L2614
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	add	x23, x23, 512
	bl	ftl_memset
.L2614:
	add	w19, w19, 1
	b	.L2613
.L2612:
	mov	w0, 0
	bl	buf_alloc
	mov	x26, x0
	cbnz	x0, .L2616
	bl	ftl_read_ahead
	b	.L2604
.L2616:
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x2, [x1,2800]
	ldr	w0, [x2,40]
	add	w0, w0, 1
	str	w0, [x2,40]
	ldr	w0, [x29,172]
	str	w0, [x26,40]
	str	w0, [x26,44]
	ldrb	w0, [x1,2776]
	cmp	w0, w19
	bne	.L2618
	str	x23, [x26,16]
.L2618:
	mov	x0, x26
	mov	w1, 1
	bl	sblk_read_page
	ldr	w9, [x26,52]
	cmn	w9, #1
	bne	.L2619
	add	x0, x22, :lo12:.LANCHOR0
	str	w9, [x29,152]
	ldr	x1, [x0,80]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2620
.L2619:
	cmp	w9, 256
	bne	.L2620
	add	x1, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w5, 1
	ldr	w0, [x29,172]
	ldr	w3, [x26,40]
	ldrh	w4, [x1,3266]
	str	x9, [x29,104]
	sub	w2, w2, w4
	lsr	w4, w0, w4
	lsl	w2, w5, w2
	ldrb	w0, [x1,3264]
	sub	w2, w2, #1
	str	x5, [x29,112]
	and	w4, w2, w4
	ldr	w2, [x26,36]
	udiv	w4, w4, w0
	ldr	x0, [x29,128]
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
.L2620:
	ldr	x7, [x26,24]
	ldr	w5, [x7,4]
	cmp	w5, w21
	beq	.L2621
	ldr	w4, [x7]
	mov	w2, w21
	ldr	w6, [x7,8]
	ldrb	w1, [x26,1]
	ldr	w3, [x29,172]
	ldr	w7, [x7,12]
	ldr	x0, [x29,136]
	str	x9, [x29,120]
	bl	printk
	add	x3, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3266]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,172]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3264]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x9, [x29,120]
.L2621:
	ldr	x0, [x26,24]
	ldr	w0, [x0,4]
	cmp	w0, w21
	bne	.L2632
	cmn	w9, #1
	bne	.L2622
.L2632:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 568
	mov	w2, 977
	bl	printk
.L2624:
	b	.L2624
.L2622:
	ldr	x0, [x26,16]
	cbnz	x0, .L2625
	ldr	x1, [x26,8]
	ubfiz	x20, x20, 9, 16
	mov	x0, x23
	lsl	w2, w19, 9
	add	x1, x1, x20
	bl	ftl_memcpy
.L2625:
	mov	x0, x26
	ubfiz	x19, x19, 9, 16
	add	x23, x23, x19
	bl	buf_free
.L2609:
	ldr	w0, [x29,156]
	add	w21, w21, 1
	sub	w0, w0, #1
	str	w0, [x29,156]
	b	.L2604
.L2648:
	bl	timer_get_time
	adrp	x1, .LANCHOR7+3256
	str	w0, [x1,#:lo12:.LANCHOR7+3256]
	ldr	w0, [x29,152]
	b	.L2603
.L2628:
	mov	w0, -1
.L2603:
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
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	timer_get_time
	adrp	x22, .LC168
	bl	dump_ftl_info
	adrp	x23, .LC169
	ldr	x3, [x20,80]
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	mov	w21, 0
	mov	w24, 21
	add	x22, x22, :lo12:.LC168
	ldrh	w1, [x3,16]
	add	x23, x23, :lo12:.LC169
	ldrh	w2, [x3,48]
	ldrh	w3, [x3,80]
	bl	printk
	ldr	x0, [x20,80]
	ldr	x3, [x20,72]
	ldrh	w1, [x0,16]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,22]
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	sub	w4, w3, w2
	bl	printk
	ldr	x0, [x20,80]
	ldr	x3, [x20,72]
	adrp	x20, .LANCHOR7
	ldrh	w1, [x0,48]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,54]
	adrp	x0, .LC167
	add	x0, x0, :lo12:.LC167
	sub	w4, w3, w2
	bl	printk
	add	x0, x20, :lo12:.LANCHOR7
	mov	w1, 0
	add	x0, x0, 3264
	mov	w2, 4608
	bl	ftl_memset
.L2652:
	add	x25, x19, :lo12:.LANCHOR0
	ldr	w0, [x25,2784]
	cmp	w21, w0
	bcs	.L2683
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2653
	mov	w0, w21
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
	b	.L2654
.L2653:
	ldrh	w1, [x25,3266]
	mov	w2, 1
	lsr	w0, w0, w1
	sub	w1, w24, w1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w0, w0, w1
	ldrb	w1, [x25,3264]
	udiv	w0, w0, w1
	ldrh	w1, [x25,56]
	cmp	w1, w0, uxth
	bhi	.L2654
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 584
	mov	w2, 1197
	bl	printk
.L2655:
	b	.L2655
.L2654:
	ldr	w2, [x29,124]
	cmn	w2, #1
	beq	.L2656
	add	x25, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldrh	w4, [x25,3266]
	sub	w3, w24, w4
	lsr	w4, w2, w4
	lsl	w3, w0, w3
	ldrh	w0, [x25,56]
	sub	w3, w3, #1
	and	w4, w3, w4
	ldrb	w3, [x25,3264]
	udiv	w3, w4, w3
	uxth	w26, w3
	and	w3, w3, 65535
	cmp	w0, w26
	bls	.L2657
	add	x0, x20, :lo12:.LANCHOR7
	add	x0, x0, 3264
	ldrh	w1, [x0,w3,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x0,w3,sxtw 1]
	b	.L2658
.L2657:
	mov	w1, w21
	mov	x0, x22
	bl	printk
	ldrb	w3, [x25,2776]
	mov	x0, x23
	ldr	x1, [x25,3832]
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L2658:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,56]
	cmp	w0, w26
	bhi	.L2656
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 584
	mov	w2, 1207
	bl	printk
.L2659:
	b	.L2659
.L2656:
	bl	timer_get_time
	add	w21, w21, 1
	b	.L2652
.L2683:
	mov	w23, 0
	adrp	x26, .LC170
	mov	w21, w23
	add	x27, x20, :lo12:.LANCHOR7
	add	x26, x26, :lo12:.LC170
	mov	w28, 65535
.L2661:
	add	x22, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22,56]
	cmp	w1, w21
	bls	.L2684
	uxtw	x1, w21
	ldr	x0, [x22,72]
	lsl	x25, x1, 1
	add	x24, x27, 3264
	sxtw	x5, w21
	ldrh	w3, [x0,x25]
	ldrh	w2, [x24,w21,sxtw 1]
	cmp	w3, w2
	beq	.L2662
	ldr	x0, [x22,64]
	str	x5, [x29,104]
	add	x1, x0, x1, lsl 2
	mov	x0, x26
	ldrb	w4, [x1,2]
	mov	w1, w21
	ubfx	x4, x4, 5, 3
	bl	printk
	ldr	x0, [x22,72]
	ldrh	w1, [x0,x25]
	cmp	w1, w28
	beq	.L2662
	ldr	x5, [x29,104]
	ldrh	w0, [x24,x5,lsl 1]
	cmp	w0, w1
	bls	.L2662
	ldr	x1, [x22,80]
	ldrh	w0, [x1,16]
	cmp	w0, w21
	beq	.L2662
	ldrh	w0, [x1,80]
	cmp	w0, w21
	beq	.L2662
	ldrh	w0, [x1,48]
	cmp	w0, w21
	csinc	w23, w23, wzr, eq
.L2662:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2661
.L2684:
	adrp	x22, .LC171
	mov	w21, 0
	add	x20, x20, :lo12:.LANCHOR7
	add	x22, x22, :lo12:.LC171
.L2664:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,56]
	cmp	w0, w21
	bls	.L2685
	uxtw	x24, w21
	ldr	x0, [x1,64]
	add	x0, x0, x24, lsl 2
	ldrb	w0, [x0,2]
	tst	w0, 224
	bne	.L2670
	add	x0, x20, 3264
	sxtw	x26, w21
	ldrh	w0, [x0,w21,sxtw 1]
	cbnz	w0, .L2666
	lsl	x25, x24, 1
	ldr	x0, [x1,72]
	ldrh	w0, [x0,x25]
	cbz	w0, .L2667
	mov	x0, x22
	mov	w1, w21
	bl	printk
.L2667:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	strh	wzr, [x0,x25]
.L2666:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x24,lsl 1]
	cbz	w0, .L2670
	add	x0, x20, 3264
	ldrh	w0, [x0,x26,lsl 1]
	cbz	w0, .L2670
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 584
	mov	w2, 1239
	bl	printk
.L2668:
	b	.L2668
.L2670:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2664
.L2685:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC172
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, w23
	add	x1, x1, 584
	add	x0, x0, :lo12:.LC172
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
	mov	w2, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrh	w1, [x0,2840]
	cmp	w1, w2
	bne	.L2687
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 600
	mov	w2, 897
	bl	printk
.L2688:
	b	.L2688
.L2687:
	adrp	x2, .LANCHOR2+400
	ldr	w2, [x2,#:lo12:.LANCHOR2+400]
	tbz	x2, 8, .L2689
	ldr	x3, [x0,72]
	ubfiz	x2, x1, 1, 16
	adrp	x0, .LC173
	add	x0, x0, :lo12:.LC173
	ldrh	w2, [x3,x2]
	bl	printk
.L2689:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2840
	add	x1, x20, 2880
	ldrh	w0, [x20,2840]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L2690
	mov	w0, -1
	strh	w0, [x20,2840]
	b	.L2718
.L2690:
	ldrh	w1, [x20,2840]
	ldr	x2, [x20,64]
	add	x2, x2, x1, lsl 2
	ldrb	w4, [x2,2]
	tst	w4, 192
	and	w2, w4, 224
	beq	.L2692
	cmp	w2, 224
	beq	.L2692
	ldr	x4, [x20,80]
	ldrh	w5, [x4,16]
	cmp	w5, w1
	beq	.L2692
	ldrh	w5, [x4,48]
	cmp	w5, w1
	beq	.L2692
	ldrh	w4, [x4,80]
	cmp	w4, w1
	bne	.L2693
.L2692:
	cbnz	w2, .L2694
	add	x3, x19, :lo12:.LANCHOR0
	lsl	x0, x1, 1
	ldr	x1, [x3,72]
	ldrh	w3, [x1,x0]
	cbz	w3, .L2695
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 600
	mov	w2, 917
	bl	printk
.L2696:
	b	.L2696
.L2695:
	strh	w2, [x1,x0]
.L2694:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2864]
	strh	w0, [x19,2840]
.L2718:
	mov	w0, 0
	b	.L2691
.L2693:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w1, [x0,40]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2697
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 600
	mov	w2, 925
	bl	printk
.L2698:
	b	.L2698
.L2697:
	ldrh	w0, [x20,3228]
	cmp	w2, 160
	ldrh	w3, [x20,3270]
	mov	w24, 1
	sub	w0, w0, #1
	mul	w1, w1, w3
	uxth	w0, w0
	bne	.L2699
	ldrb	w2, [x20,3177]
	cmp	w2, 2
	uxth	w24, w2
	orr	w1, w1, w2, lsl 24
	bne	.L2699
	ldrh	w0, [x20,3194]
	sub	w0, w0, #1
	uxth	w0, w0
.L2699:
	add	x20, x19, :lo12:.LANCHOR0
	orr	w23, w0, w1
	mov	w0, 1
	strb	w24, [x20,2846]
	strh	wzr, [x20,2842]
	strb	wzr, [x20,2844]
	strh	wzr, [x20,2866]
	bl	buf_alloc
	mov	w1, 1
	str	w23, [x0,40]
	mov	x21, x0
	bl	sblk_read_page
	ldrh	w0, [x20,3194]
	mov	w1, 255
	ldrb	w2, [x20,3196]
	mul	w2, w0, w2
	ldr	x0, [x20,3168]
	lsl	w2, w2, 2
	bl	ftl_memset
	strh	wzr, [x20,2864]
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L2703
	cmn	w0, #1
	beq	.L2703
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2702
	ldrh	w0, [x20,2840]
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2703
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 600
	mov	w2, 955
	bl	printk
.L2704:
	b	.L2704
.L2703:
	mov	x0, x21
	bl	buf_free
	mov	w0, -1
	b	.L2691
.L2702:
	ldrb	w0, [x20,3196]
	mov	x26, 0
	ldrh	w22, [x20,3228]
	mov	w25, 1
	ldr	x23, [x21,8]
	mul	w22, w0, w22
	mul	w22, w24, w22
	mov	w24, 21
	uxth	w22, w22
.L2705:
	cmp	w22, w26
	ble	.L2719
	ldr	w0, [x23,x26,lsl 2]
	cmn	w0, #1
	beq	.L2707
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L2708
	ldr	w0, [x23,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L2708:
	ldrh	w0, [x20,3266]
	ldr	w2, [x29,92]
	sub	w1, w24, w0
	ldrh	w3, [x20,2840]
	lsl	w1, w25, w1
	lsr	w0, w2, w0
	sub	w1, w1, #1
	and	w0, w1, w0
	ldrb	w1, [x20,3264]
	udiv	w0, w0, w1
	cmp	w0, w3
	bne	.L2707
	ldrh	w3, [x20,2864]
	ldr	x0, [x20,3168]
	str	w2, [x0,x3,lsl 2]
	ldrh	w0, [x20,2864]
	add	w0, w0, 1
	strh	w0, [x20,2864]
.L2707:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L2705
.L2719:
	mov	x0, x21
	bl	buf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2840]
	ldr	x3, [x0,72]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2864]
	cmp	w2, w3
	beq	.L2711
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	bl	printk
.L2711:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2840]
	ldr	x1, [x0,72]
	ldrh	w0, [x0,2864]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	bcs	.L2712
	bl	vpn_check
.L2712:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2840]
	ldr	x1, [x19,72]
	ldrh	w3, [x19,2864]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2868]
	ldrh	w0, [x19,2864]
.L2691:
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
	adrp	x24, .LC176
	mov	x19, x0
	adrp	x25, .LC177
	adrp	x0, .LC175
	mov	w1, 0
	add	x0, x0, :lo12:.LC175
	mov	w20, 0
	add	x24, x24, :lo12:.LC176
	add	x27, x25, :lo12:.LC177
	bl	printk
.L2721:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2784]
	cmp	w20, w0
	bcs	.L2744
	mov	w0, -1
	str	w0, [x19,36]
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2722
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2722:
	and	w0, w20, 4095
	cbnz	w0, .L2723
	ldr	w2, [x29,124]
	mov	x0, x24
	mov	w1, w20
	bl	printk
.L2723:
	ldr	w0, [x29,124]
	str	w0, [x19,40]
	str	w0, [x19,44]
	cmn	w0, #1
	beq	.L2725
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w3, [x19,52]
	cmn	w3, #1
	beq	.L2726
	cmp	w3, 256
	beq	.L2726
	ldr	x0, [x19,24]
	ldr	w0, [x0,4]
	cmp	w0, w20
	beq	.L2725
.L2726:
	ldr	x0, [x19,8]
	add	x23, x25, :lo12:.LC177
	ldr	x7, [x19,24]
	adrp	x26, .LC178
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
	bne	.L2728
.L2730:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, w20
	bl	lpa_hash_get_ppa
	mov	w4, w0
	ldrb	w22, [x21,2776]
	add	x0, x26, :lo12:.LC178
	mov	w1, w20
	adrp	x26, .LC180
	lsl	w28, w22, 7
	lsl	w3, w22, 7
	sub	w3, w3, #1
	mov	x22, 0
	sdiv	w28, w20, w28
	and	w3, w20, w3
	add	x21, x21, 3824
	add	x26, x26, :lo12:.LC180
	uxth	w3, w3
	uxth	w23, w28
	mov	w2, w23
	bl	printk
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
	b	.L2729
.L2728:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w1, [x1,2784]
	cmp	w22, w1
	bcs	.L2730
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
	bne	.L2731
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2731:
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
	ldrb	w23, [x5,2776]
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
	add	x0, x26, :lo12:.LC178
	uxth	w3, w3
	bl	printk
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	mov	x22, 0
	bl	pm_log2phys
	ldr	x5, [x29,96]
	adrp	x4, .LC179
	add	x4, x4, :lo12:.LC179
	add	x23, x5, 3824
.L2733:
	add	x0, x23, x22
	ldr	x1, [x0,8]
	cbz	x1, .L2732
	ldrh	w0, [x22,x23]
	cmp	w0, w28
	bne	.L2732
	mov	x0, x4
	mov	w2, 4
	mov	w3, 4096
	str	x4, [x29,104]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
.L2732:
	add	x22, x22, 16
	cmp	x22, 512
	bne	.L2733
	b	.L2730
.L2745:
	ldrh	w0, [x22,x21]
	cmp	w0, w23
	bne	.L2734
	mov	x0, x26
	mov	w2, 4
	mov	w3, 4096
	bl	rknand_print_hex
.L2734:
	add	x22, x22, 16
	cmp	x22, 512
	beq	.L2725
.L2729:
	add	x0, x21, x22
	ldr	x1, [x0,8]
	cbnz	x1, .L2745
	b	.L2734
.L2725:
	add	w20, w20, 1
	b	.L2721
.L2744:
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
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	ldrb	w1, [x23,9]
	mov	x27, 0
	ldrh	w24, [x0,3228]
	mov	w22, w27
	ldr	x0, [x0,3792]
	ldrh	w20, [x23,12]
	mul	w24, w24, w1
	add	x20, x0, x20, lsl 2
	add	x0, x20, x24, sxtw 2
	ldr	w0, [x0,-4]
	cmn	w0, #1
	beq	.L2747
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 616
	mov	w2, 1437
	bl	printk
.L2748:
	b	.L2748
.L2747:
	adrp	x25, .LC182
	adrp	x26, .LC181
	add	x0, x25, :lo12:.LC182
	str	x0, [x29,120]
	add	x0, x26, :lo12:.LC181
	str	x0, [x29,112]
.L2771:
	cmp	w27, w24
	mov	w25, w27
	bge	.L2772
	ldr	w2, [x20,x27,lsl 2]
	cmn	w2, #1
	beq	.L2749
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2776]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,400]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L2750
	ldr	x0, [x29,112]
	mov	w1, w21
	mov	w3, w27
	bl	printk
.L2750:
	mov	w28, -1
.L2754:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L2751
	add	x6, x19, :lo12:.LANCHOR0
	ldrb	w1, [x6,2776]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L2751
	ldrb	w0, [x23,9]
	ldrh	w2, [x6,3270]
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
	cbz	w0, .L2752
	adrp	x0, .LC183
	ldr	x1, [x6,3792]
	add	x0, x0, :lo12:.LC183
	mov	w2, 4
	mov	w3, w24
	bl	rknand_print_hex
.L2753:
	b	.L2753
.L2752:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	str	w28, [x20,x26]
.L2751:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L2754
.L2749:
	add	x27, x27, 1
	b	.L2771
.L2772:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L2756
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,72]
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
	ldrh	w3, [x4,x3]
	bl	printk
.L2756:
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
	beq	.L2774
	bl	zftl_insert_data_list
.L2774:
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
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	add	x0, x24, 728
	str	x0, [x29,128]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	x0, [x29,120]
	stp	x19, x20, [sp,16]
	add	x0, x0, 449
	str	x0, [x29,104]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
.L2778:
	ldrb	w0, [x24,2819]
	adrp	x21, .LANCHOR0
	cbz	w0, .L2780
	ldrb	w1, [x24,2818]
	sub	w0, w0, #1
	ldr	x2, [x29,128]
	str	x1, [x29,112]
	lsl	x1, x1, 6
	add	x26, x2, x1
	strb	w0, [x24,2819]
	ldr	w0, [x24,2784]
	ldrb	w1, [x2,x1]
	ldr	w23, [x26,36]
	strb	w1, [x24,2818]
	cmp	w23, w0
	bcc	.L2781
	mov	x0, x26
	bl	buf_free
	mov	w0, -1
	b	.L2836
.L2781:
	ldr	x0, [x26,24]
	str	x0, [x29,136]
	ldrb	w0, [x24,3812]
	ldr	x27, [x26,8]
	ldrb	w22, [x26,57]
	ldrb	w20, [x26,56]
	cbz	w0, .L2783
	ldr	x0, [x29,120]
	add	x1, x21, :lo12:.LANCHOR0
	add	x1, x1, 728
	ldrb	w3, [x0,449]
	ldr	x0, [x29,128]
	add	x3, x0, x3, lsl 6
.L2784:
	ldrb	w0, [x3]
	cmp	w0, 255
	beq	.L2837
	sbfiz	x3, x0, 6, 32
	add	x3, x1, x3
	b	.L2784
.L2837:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L2783
	ldr	x0, [x3,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x27, x1
	bl	ftl_memcpy
	mov	x0, x26
	bl	buf_free
	b	.L2778
.L2783:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L2787
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L2787:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, 0
	ldr	x19, [x1,80]
	add	x1, x1, 728
	add	x19, x19, 16
.L2790:
	ldr	w2, [x1,36]
	cmp	w2, w23
	bne	.L2788
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L2788
	add	x1, x21, :lo12:.LANCHOR0
	ubfiz	x0, x0, 6, 32
	add	x1, x1, 728
	and	w2, w2, -9
	add	x0, x1, x0
	strb	w2, [x0,2]
	ldr	x28, [x0,8]
	ldr	w0, [x0,40]
	str	w0, [x29,156]
	b	.L2789
.L2788:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L2790
	mov	x28, 0
.L2789:
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w0, [x25,2776]
	cmp	w20, w0
	bcs	.L2815
	cbz	x28, .L2792
	cbz	w22, .L2793
	mov	x0, x27
	mov	x1, x28
	lsl	w2, w22, 9
	bl	ftl_memcpy
	ldr	x19, [x25,80]
	add	x19, x19, 48
.L2793:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w22, w20
	ldrb	w3, [x0,2776]
	cmp	w20, w3
	bcc	.L2794
	ldr	x19, [x0,80]
	add	x19, x19, 16
	b	.L2815
.L2794:
	ubfiz	x1, x20, 9, 9
	sub	w2, w3, w20
	add	x0, x27, x1
	lsl	w2, w2, 9
	add	x1, x28, x1
	bl	ftl_memcpy
	b	.L2815
.L2792:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L2795
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	ldr	w1, [x29,156]
	str	w1, [x0,40]
	mov	w1, 1
	str	w23, [x0,36]
	bl	sblk_read_page
	ldr	x0, [x28,24]
	ldr	w25, [x0,12]
	ldr	w0, [x0,4]
	add	w25, w25, 1
	cmp	w0, w23
	bne	.L2796
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L2797
.L2796:
	ldrb	w1, [x28,1]
	adrp	x0, .LC185
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	w4, [x28,52]
	add	x0, x0, :lo12:.LC185
	bl	printk
	mov	w2, 4
	ldr	x1, [x28,24]
	adrp	x0, .LC142
	mov	w3, w2
	add	x0, x0, :lo12:.LC142
	bl	rknand_print_hex
	add	x3, x21, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3266]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,156]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3264]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
.L2797:
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L2798
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L2799
.L2798:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 640
	mov	w2, 497
	bl	printk
.L2800:
	b	.L2800
.L2799:
	cbz	w22, .L2801
	ldr	w0, [x29,156]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L2802
	ldr	x1, [x28,8]
	mov	x0, x27
	bl	ftl_memcpy
	b	.L2803
.L2795:
	mov	w25, w28
	cbz	w22, .L2801
	lsl	w2, w22, 9
	mov	w25, w28
.L2802:
	mov	x0, x27
	mov	w1, 0
	bl	ftl_memset
.L2803:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x19, [x0,80]
	add	x19, x19, 48
.L2801:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w22, w20
	ldrb	w2, [x0,2776]
	cmp	w20, w2
	bcc	.L2804
	bls	.L2805
	ldr	x19, [x0,80]
	add	x19, x19, 16
	b	.L2805
.L2804:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L2806
	ldr	x1, [x28,8]
	lsl	x20, x0, 2
	add	x0, x27, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L2805
.L2806:
	add	x0, x27, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L2805:
	cbz	x28, .L2791
	ldrb	w0, [x28,2]
	mov	x1, x28
	and	w0, w0, -9
	strb	w0, [x28,2]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 2832
	bl	buf_remove_buf
	mov	x0, x28
	bl	buf_free
	b	.L2791
.L2815:
	mov	w25, 0
.L2791:
	ldrh	w0, [x19,6]
	cbnz	w0, .L2807
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L2807:
	mov	x0, x19
	add	x20, x21, :lo12:.LANCHOR0
	bl	ftl_get_new_free_page
	ldr	x1, [x29,112]
	add	x3, x20, 728
	ldr	x6, [x29,136]
	lsl	x2, x1, 6
	add	x1, x3, x2
	ldr	w5, [x1,32]
	str	w5, [x6]
	str	w23, [x6,4]
	ldr	w5, [x29,156]
	str	w5, [x6,8]
	str	w25, [x6,12]
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
	mov	x1, x26
	bl	buf_add_tail
	ldrb	w0, [x20,3812]
	add	w0, w0, 1
	strb	w0, [x20,3812]
	bl	timer_get_time
	ldr	x1, [x29,120]
	ldrb	w2, [x20,3812]
	str	w0, [x1,452]
	cmp	w2, 2
	adrp	x0, .LANCHOR5
	bhi	.L2808
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L2780
.L2808:
	ldrb	w1, [x19,5]
	mov	w4, 2
	add	x6, x21, :lo12:.LANCHOR0
	cmp	w1, wzr
	ldrh	w1, [x19,6]
	csinc	w4, w4, wzr, ne
	add	x6, x6, 728
	cmp	w1, 1
	add	x1, x0, :lo12:.LANCHOR5
	csel	w4, w4, w2, ne
	ldrb	w5, [x1,449]
	mov	w1, 0
	mov	w3, w5
.L2812:
	cmp	w1, w4
	beq	.L2838
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L2812
.L2838:
	add	x0, x0, :lo12:.LANCHOR5
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,449]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 728
	strb	w2, [x0,3084]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2780
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L2780:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w19, [x21,2819]
	cbnz	w19, .L2778
	bl	ftl_write_completed
	mov	w0, w19
.L2836:
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
	sub	sp, sp, #192
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp,48]
	add	x21, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldrb	w0, [x21,3193]
	ldr	x22, [x21,80]
	cbnz	w0, .L2840
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L2839
	ldrh	w23, [x21,2866]
	mov	w0, w23
	add	w23, w23, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w22, w0
	mov	w1, 1
	mov	x0, x20
	strh	w23, [x21,2866]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L2897
	cmn	w0, #1
	bne	.L2842
.L2897:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w2, 21
	ldrh	w1, [x3,3266]
	lsr	w0, w22, w1
	sub	w1, w2, w1
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x3,3264]
	udiv	w0, w1, w0
	mov	x1, 0
	bl	ftl_sblk_dump
.L2842:
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L2898
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2844
.L2898:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 664
	mov	w2, 573
	bl	printk
.L2846:
	b	.L2846
.L2844:
	ldr	x0, [x20,24]
	str	x2, [x29,120]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,128]
	cmn	w0, #1
	ldr	x2, [x29,120]
	bne	.L2847
	mov	w0, w21
	add	x1, x29, 128
	bl	pm_log2phys
.L2847:
	ldr	w23, [x29,128]
	cmp	w23, w22
	bne	.L2848
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 728
	add	x1, x1, 2776
.L2851:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L2849
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L2849
	mov	x0, x20
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2839
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC186
	mov	w1, w21
	mov	w2, w23
	add	x0, x0, :lo12:.LC186
	ldrh	w3, [x19,2866]
	bl	printk
	b	.L2839
.L2849:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L2851
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w1, [x20,2]
	str	w21, [x20,36]
	strb	wzr, [x20,57]
	ldrb	w0, [x22,2776]
	strb	w0, [x20,56]
	mov	w0, 10
	orr	w0, w1, w0
	strb	w0, [x20,2]
	ldr	x0, [x20,24]
	ldr	w0, [x0]
	str	w0, [x20,32]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2852
	ldr	w2, [x29,128]
	mov	w0, w21
	str	x2, [x29,120]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,120]
	adrp	x0, .LC187
	ldrh	w5, [x22,2866]
	add	x0, x0, :lo12:.LC187
	mov	w1, w21
	mov	w4, w23
	bl	printk
.L2852:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,2800]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,2868]
	add	w0, w0, 1
	strh	w0, [x19,2868]
	b	.L2839
.L2848:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2854
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2866]
	adrp	x0, .LC187
	add	x0, x0, :lo12:.LC187
	mov	w1, w21
	mov	w2, w23
	mov	w4, w22
	bl	printk
.L2854:
	mov	x0, x20
	bl	buf_free
	b	.L2839
.L2840:
	ldrb	w24, [x21,3177]
	add	x0, x21, 2840
	add	x22, x22, 80
	cmp	w24, 3
	bne	.L2855
	ldrb	w1, [x21,3192]
	cbz	w1, .L2856
	ldrb	w20, [x22,9]
	ldrh	w21, [x0,314]
	add	w23, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w23, w21, w23
	madd	w20, w23, w20, w21
	uxth	w20, w20
	b	.L2858
.L2856:
	ldrh	w20, [x0,314]
	adrp	x0, .LANCHOR2
	ldrb	w21, [x22,9]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1432
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
	bne	.L2859
	b	.L2858
.L2855:
	ldrb	w20, [x22,9]
	mov	w24, 1
	ldrb	w1, [x21,3268]
	ldrh	w21, [x0,314]
	sdiv	w23, w21, w20
	msub	w20, w23, w20, w21
	uxth	w20, w20
	cbz	w1, .L2858
	mov	w24, 2
.L2858:
	mov	w25, 0
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR4
.L2861:
	ldrb	w1, [x22,9]
	mul	w0, w24, w1
	cmp	w25, w0
	bge	.L2895
	ldrh	w0, [x2,3194]
	add	w26, w21, w25
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	beq	.L2895
	add	x0, x3, :lo12:.LANCHOR4
	sbfiz	x26, x26, 2, 32
	ldr	x1, [x0,1064]
	ldr	w0, [x1,x26]
	cmn	w0, #1
	bne	.L2862
	ldrh	w27, [x2,2866]
	add	x28, x2, 2840
	ldrh	w0, [x2,2864]
	str	x3, [x29,104]
	str	x1, [x29,112]
	cmp	w0, w27
	str	x2, [x29,120]
	bls	.L2839
	mov	w0, w27
	add	w27, w27, 1
	bl	gc_get_src_ppa_from_index
	strh	w27, [x28,26]
	ldr	x1, [x29,112]
	ldr	x3, [x29,104]
	ldr	x2, [x29,120]
	str	w0, [x1,x26]
.L2862:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2861
.L2895:
	mov	w25, 1
.L2859:
	adrp	x0, .LANCHOR2
	str	x0, [x29,112]
	add	x1, x0, :lo12:.LANCHOR2
	ldr	w1, [x1,400]
	tbz	x1, 8, .L2864
	adrp	x0, .LANCHOR4+1064
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x5, x21, 2, 16
	mov	w1, w23
	mov	w2, w25
	mov	w3, w20
	ldr	x6, [x0,#:lo12:.LANCHOR4+1064]
	adrp	x0, .LC188
	ldrh	w4, [x4,3154]
	add	x0, x0, :lo12:.LC188
	ldr	w5, [x6,x5]
	mov	w6, w21
	bl	printk
.L2864:
	adrp	x0, .LC189
	mov	w28, 0
	add	x0, x0, :lo12:.LC189
	str	x0, [x29,104]
.L2865:
	ldrb	w0, [x22,9]
	mul	w0, w24, w0
	cmp	w28, w0
	bge	.L2939
	add	w1, w21, w28
	sxtw	x0, w1
	str	x0, [x29,120]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,120]
	ldr	x0, [x0,3184]
	ldrb	w0, [x0,x2]
	cmp	w0, 255
	bne	.L2866
	mov	w0, 0
	str	x1, [x29,96]
	bl	buf_alloc
	mov	x27, x0
	ldr	x1, [x29,96]
	cbnz	x0, .L2867
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w27
	bl	buf_alloc
	mov	x27, x0
	ldr	x1, [x29,96]
	cbz	x0, .L2839
.L2867:
	add	x26, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,120]
	ldrb	w2, [x27,1]
	ldr	x0, [x26,3184]
	strb	w2, [x0,x3]
	strh	w1, [x27,48]
	ldrb	w0, [x26,2847]
	strb	w25, [x27,61]
	add	w0, w0, 1
	strb	w0, [x26,2847]
	ldrb	w2, [x22,9]
	ldrh	w0, [x26,3194]
	mul	w0, w0, w2
	sub	w0, w0, #1
	cmp	w1, w0
	bne	.L2868
	adrp	x0, .LANCHOR4
	mov	w2, -1
	add	x5, x0, :lo12:.LANCHOR4
	str	x5, [x29,96]
	ldr	x0, [x5,1056]
	str	w2, [x0,x3,lsl 2]
	ldrh	w0, [x26,3194]
	ldrb	w2, [x26,3196]
	ldr	x1, [x5,1056]
	mul	w2, w0, w2
	ldr	x0, [x27,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x26,3194]
	ldrb	w2, [x26,3196]
	ldr	x5, [x29,96]
	mul	w2, w0, w2
	ldr	x1, [x5,1064]
	ldr	x0, [x27,8]
	add	x0, x0, x2, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrb	w2, [x26,2776]
	mov	w1, 0
	ldr	x0, [x27,24]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x1, [x27,24]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	str	w0, [x1]
	ldrh	w0, [x26,3194]
	ldrb	w1, [x26,3196]
	ldr	x2, [x27,24]
	str	x2, [x29,96]
	mul	w1, w0, w1
	ldr	x0, [x27,8]
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,96]
	str	w0, [x2,4]
	ldr	x1, [x26,80]
	ldr	x0, [x27,24]
	ldr	w1, [x1,132]
	b	.L2936
.L2868:
	ldr	x0, [x29,120]
	mov	w1, 1
	lsl	x4, x0, 2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x4, [x29,96]
	ldr	x0, [x0,1064]
	ldr	w0, [x0,x4]
	str	w0, [x27,40]
	mov	x0, x27
	bl	sblk_read_page
	ldr	w0, [x27,52]
	ldr	x4, [x29,96]
	cmp	w0, 512
	beq	.L2869
	cmn	w0, #1
	bne	.L2870
.L2869:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 21
	str	x4, [x29,96]
	ldrh	w0, [x5,3266]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x27,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x5,3264]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x27,52]
	ldr	x4, [x29,96]
	cmp	w0, 512
	beq	.L2899
	cmn	w0, #1
	bne	.L2870
.L2899:
	ldr	x0, [x27,24]
	mov	w1, -1
	str	w1, [x0,4]
.L2870:
	ldr	w0, [x27,52]
	cmp	w0, 512
	beq	.L2900
	cmn	w0, #1
	bne	.L2873
.L2900:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 664
	mov	w2, 688
	bl	printk
.L2875:
	b	.L2875
.L2873:
	ldr	x1, [x27,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2784]
	cmp	w2, w0
	bcc	.L2876
	mov	w0, -1
	str	w0, [x1,4]
.L2876:
	ldr	x0, [x27,24]
	ldr	w1, [x0,4]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1056]
	str	w1, [x0,x4]
	ldr	x0, [x27,24]
	ldr	w1, [x27,40]
.L2936:
	str	w1, [x0,8]
.L2866:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,120]
	add	x1, x2, 728
	mov	w5, 2
	sxtw	x4, w28
	ldr	x0, [x2,3184]
	ldrb	w3, [x0,x3]
	add	x0, x29, 128
	add	x1, x1, x3, lsl 6
	str	x1, [x0,w28,sxtw 3]
	ldrb	w0, [x2,3177]
	strb	w25, [x1,61]
	cmp	w0, 3
	strh	w5, [x1,50]
	bne	.L2877
	udiv	w0, w28, w0
	ldrh	w2, [x2,3270]
	ldrh	w5, [x1,48]
	add	w3, w0, w20
	add	x3, x22, x3, sxtw 1
	ldrh	w3, [x3,16]
	mul	w2, w3, w2
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	add	w0, w0, w0, lsl 1
	sub	w0, w28, w0
	orr	w2, w2, w23
	str	w2, [x1,40]
	uxth	w0, w0
	ldr	x1, [x3,1072]
	add	w0, w0, 1
	orr	w0, w2, w0, lsl 24
	str	w0, [x1,x5,lsl 2]
	b	.L2878
.L2877:
	cmp	w0, 2
	bne	.L2879
	ldrb	w5, [x2,3268]
	add	x1, x1, 32
	ldrh	w0, [x2,3270]
	cbnz	w5, .L2880
	add	w2, w28, w20
	add	x2, x22, x2, sxtw 1
	ldrh	w2, [x2,16]
	madd	w0, w2, w0, w23
	b	.L2938
.L2880:
	add	w2, w20, w28, lsr 1
	add	x2, x22, x2, sxtw 1
	ldrh	w2, [x2,16]
	madd	w0, w2, w0, w23
	and	w2, w28, 1
	add	w0, w0, w2
.L2938:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L2879:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x1, .LANCHOR4
	add	x0, x0, 728
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, x3, lsl 6
	ldr	w3, [x0,40]
	ldrh	w2, [x0,48]
	ldr	x0, [x1,1072]
	str	w3, [x0,x2,lsl 2]
.L2878:
	ldr	x0, [x29,112]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2881
	add	x0, x29, 128
	ldr	x1, [x29,120]
	mov	w2, w25
	ldr	x5, [x0,x4,lsl 3]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x0,2846]
	ldr	x0, [x0,3184]
	ldrh	w6, [x5,48]
	ldrb	w7, [x5,1]
	ldrb	w4, [x0,x1]
	mov	w0, 3
	mov	w1, w28
	udiv	w0, w28, w0
	add	w0, w20, w0
	str	w0, [sp]
	ldr	x0, [x5,24]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	ldr	x0, [x29,104]
	ldr	w5, [x5,40]
	bl	printk
.L2881:
	add	w8, w28, 1
	uxth	w28, w8
	b	.L2865
.L2939:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x2,3177]
	cmp	w0, 3
	add	x0, x29, 128
	bne	.L2924
	ldrb	w1, [x2,3192]
	cbz	w1, .L2884
.L2887:
	ldr	x1, [x29,128]
	strb	wzr, [x1,60]
	b	.L2885
.L2884:
	cmp	w25, 1
	mov	w2, 9
	beq	.L2937
	cmp	w25, 2
	bne	.L2887
	mov	w2, 13
.L2937:
	ldr	x1, [x29,128]
	strb	w2, [x1,60]
.L2885:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L2888
.L2924:
	ldrb	w2, [x22,9]
	mul	w2, w2, w24
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L2940
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L2924
.L2940:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,128]
	mul	w1, w24, w1
	bl	sblk_prog_page
.L2888:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x22,9]
	ldrb	w3, [x1,3192]
	cbz	w3, .L2890
	add	w0, w2, w2, lsl 1
	and	w2, w0, 1023
	b	.L2891
.L2890:
	ldrb	w1, [x1,3268]
	ubfiz	w0, w2, 1, 8
	cmp	w1, wzr
	csel	w2, w0, w2, ne
.L2891:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	ldr	w0, [x1,52]
	add	w0, w0, w2
	str	w0, [x1,52]
	ldrh	w1, [x19,3154]
	add	w0, w2, w1
	ldrh	w1, [x19,3194]
	uxth	w0, w0
	strh	w0, [x19,3154]
	ldrb	w2, [x22,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L2892
	ldr	x0, [x19,80]
	strh	wzr, [x0,86]
.L2892:
	bl	gc_write_completed
.L2839:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 192
	ret
	.size	gc_do_copy_back, .-gc_do_copy_back
	.align	2
	.global	zftl_discard
	.type	zftl_discard, %function
zftl_discard:
	stp	x29, x30, [sp, -112]!
	add	w3, w0, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w2, [x1,4]
	cmp	w3, w2
	bhi	.L2961
	adrp	x22, .LANCHOR8
	add	w24, w0, 24576
	add	x2, x22, :lo12:.LANCHOR8
	ldr	w0, [x2,3520]
	add	w0, w19, w0
	str	w0, [x2,3520]
	ldr	x0, [x1,2800]
	ldrb	w1, [x1,2776]
	ldr	w25, [x0,8]
	udiv	w20, w24, w1
	add	w2, w25, 1
	str	w2, [x0,8]
	msub	w26, w20, w1, w24
	cbz	w26, .L2943
	sub	w1, w1, w26
	mov	w0, w20
	cmp	w1, w19
	csel	w28, w1, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	bne	.L2944
	mov	w0, w20
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
.L2944:
	ldr	w0, [x29,104]
	and	w27, w28, 65535
	cmn	w0, #1
	beq	.L2946
	mov	w0, 0
	bl	buf_alloc
	mov	x23, x0
	cbz	x0, .L2946
	str	w20, [x0,36]
	lsl	w2, w27, 9
	strb	w26, [x0,57]
	strb	w28, [x0,56]
	str	w25, [x0,32]
	ubfiz	x0, x26, 9, 25
	ldr	x1, [x23,8]
	add	x0, x1, x0
	mov	w1, 0
	bl	ftl_memset
	mov	x0, x23
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2946:
	add	w20, w20, 1
	sub	w19, w19, w27
.L2943:
	cbz	w19, .L2948
	bl	ftl_flush
.L2948:
	mov	w0, -1
	mov	w27, 1
	str	w0, [x29,108]
	mov	w28, 21
.L2949:
	add	x26, x21, :lo12:.LANCHOR0
	ldrb	w0, [x26,2776]
	cmp	w19, w0
	bcc	.L2982
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	beq	.L2950
	mov	w0, 0
	bl	buf_alloc
	mov	x23, x0
	cbz	x0, .L2952
	ldrb	w2, [x26,2776]
	mov	w1, 0
	str	w20, [x0,36]
	strb	w2, [x0,56]
	lsl	w2, w2, 9
	str	w25, [x0,32]
	strb	wzr, [x0,57]
	ldr	x0, [x0,8]
	bl	ftl_memset
	mov	x0, x23
	bl	ftl_write_buf
	bl	ftl_write_commit
	b	.L2981
.L2950:
	mov	w0, w20
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,104]
	cmn	w0, #1
	beq	.L2952
	add	x1, x29, 108
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	ldrh	w0, [x26,3266]
	ldr	w1, [x29,104]
	sub	w2, w28, w0
	lsl	w2, w27, w2
	lsr	w0, w1, w0
	sub	w2, w2, #1
	ldrb	w1, [x26,3264]
	and	w0, w2, w0
	udiv	w0, w0, w1
	bl	ftl_vpn_decrement
.L2981:
	ldr	x1, [x26,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2952:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldrb	w0, [x0,2776]
	sub	w19, w19, w0
	b	.L2949
.L2982:
	cbz	w19, .L2956
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	bne	.L2957
	mov	w0, w20
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
.L2957:
	ldr	w0, [x29,104]
	cmn	w0, #1
	beq	.L2956
	mov	w0, 0
	bl	buf_alloc
	mov	x23, x0
	cbz	x0, .L2956
	add	x21, x21, :lo12:.LANCHOR0
	str	w20, [x0,36]
	strb	wzr, [x0,57]
	strb	w19, [x0,56]
	str	w25, [x0,32]
	ldrb	w0, [x21,2776]
	cmp	w19, w0
	bcc	.L2959
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 680
	mov	w2, 1118
	bl	printk
.L2960:
	b	.L2960
.L2959:
	ldr	x0, [x23,8]
	mov	w1, 0
	lsl	w2, w19, 9
	bl	ftl_memset
	mov	x0, x23
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	x1, [x21,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2956:
	add	x22, x22, :lo12:.LANCHOR8
	mov	w20, 0
	ldr	w1, [x22,3520]
	cmp	w1, 8192
	bls	.L2942
	adrp	x0, .LC190
	mov	w2, w24
	mov	w3, w19
	mov	w4, w20
	add	x0, x0, :lo12:.LC190
	bl	printk
	str	w20, [x22,3520]
	bl	flt_sys_flush
	mov	w0, 1
	str	w0, [x22,3524]
	b	.L2942
.L2961:
	mov	w20, -1
.L2942:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	ldrh	w1, [x0,2792]
	ldrh	w20, [x0,2788]
	ldr	x23, [x0,80]
	add	w20, w1, w20
	ldrh	w22, [x0,2790]
	uxth	w20, w20
	bne	.L2984
	adrp	x0, .LANCHOR7
	adrp	x25, .LANCHOR8
	add	x2, x0, :lo12:.LANCHOR7
	mov	x21, x0
	ldr	w26, [x2,3256]
	cbnz	w26, .L2985
	add	x0, x25, :lo12:.LANCHOR8
	ldr	w0, [x0,3528]
	cbz	w0, .L2984
.L2985:
	add	w26, w26, 50
	bl	timer_get_time
	cmp	w26, w0
	bcs	.L2986
	add	x0, x21, :lo12:.LANCHOR7
	str	wzr, [x0,3256]
.L2986:
	add	x19, x25, :lo12:.LANCHOR8
	ldr	w20, [x19,3528]
	bl	timer_get_time
	add	w20, w20, 50
	cmp	w20, w0
	bcs	.L3099
	str	wzr, [x19,3528]
	b	.L3099
.L2984:
	adrp	x0, .LANCHOR4
	add	x2, x0, :lo12:.LANCHOR4
	mov	x21, x0
	ldrb	w2, [x2,1050]
	cmp	w2, 6
	bhi	.L3099
	adrp	x0, .L2990
	add	x0, x0, :lo12:.L2990
	ldrh	w0, [x0,w2,uxtw #1]
	adr	x2, .Lrtx2990
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx2990:
	.section	.rodata
	.align	0
	.align	2
.L2990:
	.2byte	(.L2989 - .Lrtx2990) / 4
	.2byte	(.L2991 - .Lrtx2990) / 4
	.2byte	(.L2992 - .Lrtx2990) / 4
	.2byte	(.L2993 - .Lrtx2990) / 4
	.2byte	(.L2994 - .Lrtx2990) / 4
	.2byte	(.L3075 - .Lrtx2990) / 4
	.2byte	(.L2996 - .Lrtx2990) / 4
	.text
.L2989:
	ldrh	w28, [x23,80]
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	add	w22, w22, w1
	cmp	w28, w0
	uxth	w22, w22
	ldrh	w25, [x27,2794]
	ldrh	w26, [x27,2796]
	beq	.L2997
	adrp	x0, .LANCHOR2+400
	cmp	w22, 7
	mov	w24, 3
	csinc	w24, w24, wzr, hi
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2999
	ldrh	w5, [x23,124]
	adrp	x0, .LC191
	ldrh	w6, [x23,120]
	add	x0, x0, :lo12:.LC191
	ldrh	w7, [x23,122]
	mov	w1, 1705
	ldrb	w2, [x27,3193]
	mov	w3, w20
	str	w28, [sp]
	mov	w4, w22
	bl	printk
.L2999:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w24
	mov	w2, 1
	ldrb	w0, [x19,3193]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L3099
.L3005:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 1
	b	.L3117
.L2997:
	cmp	w24, 1
	bne	.L3001
	bl	gc_scan_static_data
	adrp	x23, .LANCHOR8
	bl	gc_static_wearleveling
	bl	gc_block_vpn_scan
	add	x0, x23, :lo12:.LANCHOR8
	ldr	w0, [x0,3524]
	cbz	w0, .L3003
	mov	w1, 5
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	cmp	w1, w28
	beq	.L3004
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x27,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3004
	mov	w1, w24
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3005
.L3004:
	mov	w1, 4
	mov	w0, 0
	bl	zftl_get_gc_node
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3006
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3006
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3005
.L3006:
	add	x27, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldr	x0, [x27,3208]
	bl	_list_get_gc_head_node.isra.5
	uxth	w1, w0
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3007
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x27,72]
	ldrh	w1, [x2,x1]
	cmp	w1, 4
	bhi	.L3007
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3005
.L3007:
	add	x23, x23, :lo12:.LANCHOR8
	str	wzr, [x23,3524]
	b	.L3003
.L3001:
	ldrh	w0, [x27,2808]
	cmp	w0, w20
	bcc	.L3099
.L3003:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldr	x7, [x1,80]
	strb	w0, [x1,3193]
	ldrh	w5, [x7,124]
	cbz	w5, .L3009
	add	x21, x21, :lo12:.LANCHOR4
	strb	wzr, [x1,3193]
	strb	w0, [x21,1050]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L3099
	ldrh	w6, [x7,120]
	adrp	x0, .LC192
	ldrh	w7, [x7,122]
	mov	w1, 1762
	mov	w2, 0
	mov	w3, w20
	mov	w4, w22
	add	x0, x0, :lo12:.LC192
	bl	printk
	b	.L3099
.L3009:
	ldrh	w3, [x1,2808]
	cmp	w20, w3
	bcs	.L3012
	cmp	w22, 1
	bls	.L3013
	cmp	w22, 16
	bls	.L3014
	ldrh	w2, [x1,2798]
	ldrh	w1, [x1,3234]
	cmp	w2, w1
	bcs	.L3014
	mov	w1, 3
	mov	w2, 16
	b	.L3115
.L3014:
	mov	w1, 2
	mov	w0, 1
	mov	w2, w1
	add	x19, x19, :lo12:.LANCHOR0
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3005
	b	.L3113
.L3013:
	adrp	x0, .LANCHOR2+400
	strb	w5, [x1,3193]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L3017
	adrp	x0, .LC192
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC192
	mov	w1, 1778
	mov	w2, w5
	mov	w3, w20
	mov	w4, w22
	bl	printk
.L3017:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w20, 16
	ldrb	w0, [x19,3193]
	bls	.L3018
	mov	w1, 3
	mov	w2, 4
	b	.L3115
.L3018:
	mov	w1, 1
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3005
.L3113:
	ldrb	w0, [x19,3193]
	mov	w1, 3
	mov	w2, 2
.L3115:
	bl	gc_search_src_blk
	uxth	w0, w0
	b	.L3015
.L3012:
	cmp	w24, 1
	lsl	w0, w3, 1
	bne	.L3019
	cmp	w20, w0
	bge	.L3019
	add	w0, w26, w25
	uxth	w0, w0
	cmp	w0, w22, lsr 1
	bcs	.L3020
	ldrh	w3, [x1,2798]
	ldrh	w2, [x1,3234]
	cmp	w3, w2
	bcs	.L3020
	ldrh	w0, [x1,3232]
	lsr	w0, w0, 1
	strh	w0, [x1,2808]
	b	.L3099
.L3020:
	cmp	w22, 1
	bls	.L3021
	cmp	w22, 16
	mov	w0, 1
	bls	.L3022
	mov	w1, 3
	mov	w2, 8
	b	.L3112
.L3022:
	mov	w1, 2
	mov	w2, w1
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L3023
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 3
	mov	w2, 2
	ldrb	w0, [x0,3193]
.L3112:
	bl	gc_search_src_blk
	uxth	w0, w0
.L3023:
	add	x19, x19, :lo12:.LANCHOR0
	b	.L3114
.L3021:
	cmp	w20, w0
	bcs	.L3024
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w1, 3
	mov	w2, 8
	strb	wzr, [x19,3193]
	bl	gc_search_src_blk
	uxth	w0, w0
.L3114:
	ldrh	w1, [x19,3232]
	strh	w1, [x19,2808]
	b	.L3015
.L3019:
	cmp	w20, w0
	blt	.L3099
.L3024:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,3232]
	lsr	w0, w0, 1
	strh	w0, [x19,2808]
	b	.L3099
.L3015:
	cbnz	w0, .L3005
	b	.L3099
.L2991:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x20,2840]
	cmp	w1, w0
	bne	.L3025
	bl	gc_get_src_blk
	strh	w0, [x20,2840]
.L3025:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2840
	ldrh	w3, [x0,2840]
	cmp	w3, w2
	beq	.L3026
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,64]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3028
	mov	x0, 0
.L3027:
	cmp	w5, w0, uxth
	bls	.L3028
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L3027
.L3032:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2840]
	b	.L3099
.L3028:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L3079
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L3030
.L3079:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,72]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L3032
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 1972
	bl	printk
.L3033:
	b	.L3033
.L3030:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 2
.L3117:
	strb	w1, [x0,1050]
	b	.L3099
.L3026:
	add	x0, x21, :lo12:.LANCHOR4
	strb	wzr, [x0,1050]
	b	.L3099
.L2992:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3034
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 3
	b	.L3117
.L3034:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x20, x22, 2840
	ldrh	w0, [x22,2840]
	cmp	w0, w1
	beq	.L3005
	ldrh	w1, [x20,24]
	cbz	w1, .L3035
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 4
	strh	wzr, [x20,26]
	strb	w1, [x0,1050]
	b	.L3099
.L3035:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w1, 1
	ldr	x2, [x22,72]
	strb	w1, [x21,1050]
	ubfiz	x1, x0, 1, 16
	ldrh	w21, [x2,x1]
	cbz	w21, .L3036
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2000
	bl	printk
.L3037:
	b	.L3037
.L3036:
	bl	ftl_free_sblk
	ldrh	w1, [x22,2840]
	ldr	x0, [x22,72]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w1, [x20,30]
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, 8
	bhi	.L3038
	strh	w1, [x20,30]
	b	.L3032
.L3038:
	strh	w21, [x20,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, w21
	bl	ftl_info_flush
	b	.L3032
.L2993:
	add	x22, x19, :lo12:.LANCHOR0
.L3100:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x22,2842]
	add	x1, x22, 2840
	ldrh	w0, [x22,3228]
	cmp	w2, w0
	bcs	.L3039
	cmp	w20, 7
	bls	.L3100
	b	.L3099
.L3039:
	ldrh	w3, [x1,24]
	adrp	x2, .LANCHOR2
	cbz	w3, .L3040
	add	x0, x21, :lo12:.LANCHOR4
	add	x2, x2, :lo12:.LANCHOR2
	mov	w4, 4
	strh	wzr, [x1,26]
	strb	w4, [x0,1050]
	ldr	w0, [x2,400]
	tbz	x0, 8, .L3041
	ldrh	w1, [x1]
	adrp	x0, .LC193
	ldr	x4, [x22,72]
	add	x0, x0, :lo12:.LC193
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L3041:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,72]
	ldrh	w1, [x19,2840]
	ldrh	w1, [x0,x1,lsl 1]
	ldrh	w0, [x19,2864]
	cmp	w1, w0
	beq	.L3099
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2034
	bl	printk
.L3042:
	b	.L3042
.L3040:
	add	x0, x21, :lo12:.LANCHOR4
	add	x2, x2, :lo12:.LANCHOR2
	mov	w3, 1
	ldrh	w1, [x1]
	ldr	x20, [x22,64]
	add	x20, x20, x1, uxth 2
	strb	w3, [x0,1050]
	ldr	w0, [x2,400]
	tbz	x0, 8, .L3043
	ldrb	w2, [x20,2]
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	ubfx	x2, x2, 5, 3
	bl	printk
.L3043:
	ldrb	w0, [x20,2]
	tst	w0, 192
	beq	.L3080
	and	w0, w0, 224
	cmp	w0, 224
	cset	w21, eq
	cbz	w21, .L3044
.L3080:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2044
	bl	printk
.L3046:
	b	.L3046
.L3044:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2840
	ldrh	w0, [x19,2840]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,2840]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L3047
	strh	w0, [x20,30]
	b	.L3099
.L3047:
	strh	w21, [x20,30]
	b	.L3118
.L2994:
	cbnz	w24, .L3048
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2808]
	cmp	w0, w20
	bcc	.L3099
.L3048:
	ldrh	w1, [x23,80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3049
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w0, [x22,3193]
	cmp	w0, 1
	bne	.L3049
	bl	ftl_flush
	ldrh	w0, [x22,2792]
	cbnz	w0, .L3116
	ldrh	w0, [x22,2790]
.L3116:
	mov	w1, 5
	lsr	w0, w0, 1
	bl	zftl_get_free_sblk
	uxth	w25, w0
	mov	w1, 65535
	cmp	w25, w1
	beq	.L3052
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x2, [x19,64]
	add	x2, x2, x25, uxth 2
	ldrb	w1, [x2,2]
	ands	w20, w1, 224
	beq	.L3053
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2069
	bl	printk
.L3054:
	b	.L3054
.L3053:
	mov	w26, 5
	add	x21, x21, :lo12:.LANCHOR4
	bfi	w1, w26, 5, 3
	orr	w1, w1, 16
	strb	w1, [x2,2]
	mov	w1, 1
	bl	ftl_erase_sblk
	strb	w26, [x23,84]
	add	x1, x23, 96
	mov	w0, w25
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x19,3228]
	strb	w0, [x23,89]
	ldrb	w2, [x19,3196]
	mul	w0, w0, w1
	ldrh	w1, [x19,3194]
	strh	w0, [x23,86]
	ldr	x0, [x21,1056]
	mul	w2, w1, w2
	strh	w25, [x23,80]
	mov	w1, 255
	strh	w20, [x23,82]
	lsl	w2, w2, 2
	strb	w20, [x23,85]
	strh	w20, [x23,90]
	bl	ftl_memset
	ldrh	w1, [x19,3194]
	ldrb	w2, [x19,3196]
	ldr	x0, [x21,1064]
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x19,3194]
	mov	w1, 255
	ldrb	w2, [x19,3196]
	ldr	x0, [x19,3184]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x19,80]
	mov	w1, -1
	str	w25, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	w20, [x19,2896]
	ldr	x0, [x19,2800]
	strh	w25, [x0,126]
	mov	w0, -1
	str	w0, [x19,3160]
	mov	w0, w20
	strh	w20, [x19,3154]
	strh	w20, [x19,3156]
	strh	w20, [x19,3158]
	bl	ftl_info_flush
	b	.L3099
.L3049:
	cmp	w24, 1
	mov	w22, 4
	csinc	w22, w22, wzr, eq
	cmp	w20, 15
	add	w0, w22, 4
	add	x20, x19, :lo12:.LANCHOR0
	csel	w22, w0, w22, ls
.L3058:
	sub	w22, w22, #1
	uxtb	w22, w22
	cmp	w22, 255
	beq	.L3099
	bl	gc_do_copy_back
	ldrb	w0, [x20,3193]
	cbnz	w0, .L3059
	ldrb	w0, [x20,2778]
	cmp	w0, 3
	bhi	.L3060
	bl	ftl_write_commit
.L3060:
	add	x24, x20, 2840
	ldrh	w1, [x24,26]
	ldrh	w0, [x24,24]
	cmp	w1, w0
	bcc	.L3058
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,1050]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w0, [x24]
	ldr	x1, [x20,72]
	lsl	x0, x0, 1
	ldrh	w2, [x1,x0]
	cbz	w2, .L3062
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2144
	bl	printk
.L3063:
	b	.L3063
.L3062:
	strh	w2, [x1,x0]
	ldrh	w0, [x24]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x24]
	b	.L3099
.L3059:
	add	x1, x20, 2840
	ldrh	w0, [x1,318]
	cbz	w0, .L3064
	ldr	w0, [x1,320]
	strh	wzr, [x1,318]
	cmn	w0, #1
	beq	.L3065
	bl	ftl_mask_bad_block
.L3065:
	add	x20, x19, :lo12:.LANCHOR0
	ldr	x0, [x20,80]
	str	wzr, [x20,3160]
	strh	wzr, [x20,2896]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x20,80]
	mov	w0, -1
	ldr	x2, [x20,2800]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x20,2848]
	cbz	x0, .L3066
	bl	buf_free
.L3066:
	add	x19, x19, :lo12:.LANCHOR0
	str	xzr, [x19,2848]
	bl	flt_sys_flush
	add	x0, x21, :lo12:.LANCHOR4
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 2177
	add	x1, x1, 696
	strb	wzr, [x0,1050]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
.L3067:
	b	.L3067
.L3064:
	ldrh	w0, [x23,86]
	cmp	w0, 1
	bls	.L3068
	ldrh	w2, [x1,26]
	ldrh	w0, [x1,24]
	cmp	w2, w0
	bcc	.L3058
	add	x0, x21, :lo12:.LANCHOR4
	mov	w2, 1
	strb	w2, [x0,1050]
	ldrh	w0, [x1,56]
	add	w2, w0, 1
	add	x0, x1, x0, sxtw 1
	strh	w2, [x1,56]
	ldrh	w2, [x1]
	strh	w2, [x0,58]
	mov	w0, -1
	strh	w0, [x1]
	b	.L3099
.L3068:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w2, 5
	strb	w2, [x0,1050]
	ldrh	w2, [x1,26]
	ldrh	w0, [x1,24]
	cmp	w2, w0
	bcc	.L3069
	ldrh	w0, [x1,56]
	add	w2, w0, 1
	add	x0, x1, x0, sxtw 1
	strh	w2, [x1,56]
	ldrh	w2, [x1]
	strh	w2, [x0,58]
	mov	w0, -1
	strh	w0, [x1]
.L3069:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,80]
	ldrh	w1, [x23,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,2856]
	ldrb	w2, [x20,3177]
	ldrh	w1, [x20,3228]
	strh	w1, [x20,2858]
	cmp	w2, 2
	strh	w2, [x20,2860]
	bne	.L3070
	lsl	w1, w1, 1
	strh	w1, [x20,2858]
	mov	w1, 1
	strh	w1, [x20,2860]
.L3070:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2862]
	b	.L3099
.L3075:
	mov	w22, 0
	add	x25, x19, :lo12:.LANCHOR0
.L2995:
	bl	gc_check_data_one_wl
	cbz	w0, .L3072
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,80]
	strh	wzr, [x19,2896]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,80]
	mov	w0, -1
	ldr	x2, [x19,2800]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,2848]
	bl	buf_free
	str	xzr, [x19,2848]
	add	x0, x21, :lo12:.LANCHOR4
	strb	wzr, [x0,1050]
.L3118:
	bl	flt_sys_flush
	b	.L3099
.L3072:
	ldrh	w1, [x25,2856]
	ldrh	w0, [x25,2858]
	cmp	w1, w0
	bcc	.L3073
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 6
	strb	w1, [x0,1050]
	ldr	x0, [x25,2848]
	bl	buf_free
	str	xzr, [x25,2848]
	b	.L3099
.L3073:
	cmp	w20, 7
	bls	.L2995
	cmp	w24, 1
	bne	.L3099
	add	w22, w22, 1
	uxtb	w22, w22
	cmp	w22, 4
	bls	.L2995
	b	.L3099
.L2996:
	bl	gc_update_l2p_map_new
	mov	w20, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	pm_flush
	strh	w20, [x23,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,2800]
	strh	w20, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
	add	x0, x21, :lo12:.LANCHOR4
	strb	wzr, [x0,1050]
	bl	print_ftl_debug_info
	b	.L3099
.L3052:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC0
	add	x1, x1, 696
	mov	w2, 2075
	bl	printk
.L3055:
	b	.L3055
.L3099:
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
	adrp	x0, .LANCHOR2+400
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	mov	w23, w2
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	mov	w28, w1
	mov	x25, x3
	tbz	x0, 12, .L3120
	adrp	x0, .LC195
	mov	w1, w19
	add	x0, x0, :lo12:.LC195
	mov	w2, w28
	mov	w3, w23
	bl	printk
.L3120:
	cbnz	w19, .L3121
	adrp	x0, .LANCHOR0+4
	mov	w19, 24576
	ldr	w2, [x0,#:lo12:.LANCHOR0+4]
	b	.L3122
.L3121:
	cmp	w19, 3
	mov	w0, -1
	bhi	.L3123
	lsl	w19, w19, 13
	mov	w2, 8192
.L3122:
	add	w1, w28, w23
	mov	w0, -1
	cmp	w1, w2
	bhi	.L3123
	add	w28, w19, w28
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	sub	w22, w23, #1
	add	w22, w22, w28
	add	w26, w28, w23
	ldrb	w0, [x0,2776]
	udiv	w24, w28, w0
	mov	w21, w24
	udiv	w22, w22, w0
	sub	w20, w22, w24
	add	w20, w20, 1
.L3124:
	cbz	w20, .L3143
	mov	w0, 0
	bl	buf_alloc
	mov	x27, x0
	cbnz	x0, .L3125
	bl	ftl_write_commit
	b	.L3124
.L3125:
	strb	wzr, [x0,57]
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w21, w24
	ldrb	w4, [x0,2776]
	cset	w0, eq
	strb	w4, [x27,56]
	cmp	w21, w22
	beq	.L3127
	cbz	w0, .L3128
	b	.L3137
.L3127:
	cbz	w0, .L3129
.L3137:
	udiv	w0, w28, w4
	msub	w0, w0, w4, w28
	uxtb	w0, w0
	strb	w0, [x27,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w23
	csel	w4, w23, w4, hi
	b	.L3141
.L3129:
	msub	w4, w4, w21, w26
.L3141:
	strb	w4, [x27,56]
.L3128:
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
	ldr	x1, [x0,2800]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x27,32]
	mov	x0, x27
	bl	ftl_write_buf
	ldrb	w0, [x27,56]
	add	x25, x25, x0, lsl 9
	b	.L3124
.L3143:
	bl	ftl_write_commit
	mov	w0, w20
.L3142:
	mov	w1, 1
	bl	zftl_do_gc
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w2, [x1,2788]
	ldrh	w0, [x1,2792]
	add	w0, w2, w0
	cmp	w0, 7
	bgt	.L3144
	mov	w0, 0
	b	.L3142
.L3144:
	bl	timer_get_time
	adrp	x1, .LANCHOR8+3528
	str	w0, [x1,#:lo12:.LANCHOR8+3528]
	mov	w0, 0
.L3123:
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
	ldr	x0, [x0,720]
	ldrb	w2, [x0,19]
	add	x0, x20, :lo12:.LANCHOR4
	ldrb	w0, [x0,1208]
	cbnz	w0, .L3147
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L3149
	cmp	w2, 68
	beq	.L3149
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L3147
.L3149:
	adrp	x24, .LC196
	mov	w25, w1
	add	x24, x24, :lo12:.LC196
	add	x20, x20, :lo12:.LANCHOR4
	mov	w27, 65535
.L3151:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,720]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L3147
	sub	x22, x20, #12
	add	w26, w19, w25
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	ldrh	w3, [x22,w19,sxtw 1]
	bl	printk
	ldrh	w0, [x22,w19,sxtw 1]
	cmp	w0, w27
	bne	.L3147
	ldr	x0, [x20,1144]
	mov	w1, 255
	mov	w2, 16384
	add	w19, w19, 1
	bl	ftl_memset
	uxth	w19, w19
	ldr	x4, [x28,720]
	mov	w0, w23
	ldr	x2, [x20,1144]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L3151
.L3147:
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
	ldr	x0, [x0,720]
	ldrb	w23, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x22, x0
	mov	w0, -1
	cbz	x22, .L3187
	add	w19, w19, 511
	add	x2, x26, 253952
	lsr	w19, w19, 9
	add	x2, x2, 2044
	mov	w0, 0
	mov	w24, 63999
	mov	w3, 4097
.L3158:
	ldr	w1, [x2]
	cbnz	w1, .L3155
	ldr	w1, [x26,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w3
	sub	w24, w24, #1
	csel	w0, w0, wzr, cc
	str	w1, [x2],-4
	cmp	w24, 4096
	bne	.L3158
	b	.L3157
.L3155:
	adrp	x0, .LC197
	mov	w2, w24
	add	x0, x0, :lo12:.LC197
	bl	printk
.L3157:
	add	x0, x21, :lo12:.LANCHOR0
	add	w1, w19, 128
	mul	w20, w20, w23
	mov	w25, 0
	ldr	x2, [x0,720]
	ldrh	w0, [x2,10]
	ldrb	w19, [x2,12]
	mov	w2, w24
	lsl	w0, w0, 2
	sdiv	w19, w0, w19
	mov	w0, 500
	cmp	w19, 500
	csel	w19, w19, w0, ls
	adrp	x0, .LC198
	cmp	w19, w1
	add	x0, x0, :lo12:.LC198
	csel	w19, w19, w1, ls
	ldr	w1, [x26,w24,uxtw 2]
	mov	w3, w19
	bl	printk
	str	w25, [x29,140]
	uxth	w0, w20
	str	w0, [x29,116]
.L3177:
	mov	w1, 0
	mov	x0, x22
	mov	w2, 512
	add	x24, x21, :lo12:.LANCHOR0
	bl	ftl_memset
	ldr	x0, [x24,144]
	ldr	w1, [x29,140]
	add	x0, x0, x1, uxtw
	ldrb	w20, [x0,32]
	cmp	w20, 255
	beq	.L3159
	ldr	w0, [x29,116]
	mul	w20, w20, w0
	ldr	x0, [x24,720]
	ldrb	w27, [x0,9]
	ldrh	w23, [x0,10]
	ldrb	w0, [x24,136]
	udiv	w1, w20, w27
	str	w0, [x29,112]
	mul	w23, w27, w23
	mov	w0, 0
	strb	wzr, [x24,136]
	uxth	w23, w23
	bl	flash_erase_block
	udiv	w0, w20, w23
	msub	w0, w0, w23, w20
	str	w0, [x29,136]
	sub	w28, w20, w0
	cmp	w20, w28
	bne	.L3181
	ldrb	w0, [x21,#:lo12:.LANCHOR0]
	cmp	w0, 9
	bne	.L3181
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 1024
	ldr	x3, [x0,1192]
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
	ldr	x2, [x24,720]
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
	b	.L3160
.L3181:
	str	w19, [x29,128]
	mov	x3, 0
.L3160:
	str	x26, [x29,120]
	mov	w24, 0
	add	x23, x21, :lo12:.LANCHOR0
.L3162:
	cmp	w24, w19
	bcs	.L3195
	ldr	w0, [x29,136]
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w24, w0
	add	x0, x23, 168
	ubfx	x4, x4, 2, 16
	add	w1, w4, 1
	ldrb	w2, [x2,1208]
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L3164
	ldrb	w6, [x23,165]
	lsl	w0, w1, 1
	cmp	w6, wzr
	csel	w0, w0, w1, ne
.L3164:
	ldrb	w1, [x23]
	cmp	w1, 9
	beq	.L3194
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L3194:
	str	w0, [x29,144]
	mov	w0, 61424
	str	w0, [x29,148]
	add	x0, x23, 168
	ldrh	w0, [x0,w4,sxtw 1]
	cbnz	w2, .L3168
	mov	w4, w0
	b	.L3169
.L3168:
	ldrb	w1, [x23,165]
	lsl	w0, w4, 1
	cmp	w1, wzr
	csel	w4, w0, w4, ne
.L3169:
	mul	w0, w4, w27
	cbnz	x3, .L3170
	ldr	x1, [x29,120]
	add	w0, w0, w28
	add	x2, x29, 144
	str	x4, [x29,104]
	bl	fw_flash_page_prog.constprop.39
	adrp	x0, .LANCHOR4
	ldr	x4, [x29,104]
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1208]
	cbnz	w0, .L3171
	udiv	w1, w28, w27
	add	w2, w4, 1
	bl	id_block_prog_msb_ff_data
.L3171:
	ldr	x0, [x29,120]
	add	x0, x0, 2048
	str	x0, [x29,120]
	b	.L3172
.L3170:
	add	w0, w0, w28
	mov	x1, x3
	add	x2, x29, 144
	bl	fw_flash_page_prog.constprop.39
.L3172:
	add	w24, w24, 4
	mov	x3, 0
	uxth	w24, w24
	b	.L3162
.L3195:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,112]
	mov	x2, x22
	strb	w1, [x0,136]
	mov	w0, w20
	ldr	w1, [x29,128]
	bl	id_block_read_data.constprop.38
	ldr	w0, [x29,128]
	lsl	w23, w0, 7
	mov	x0, 0
.L3174:
	cmp	w23, w0
	bls	.L3196
	ldr	w2, [x22,x0,lsl 2]
	add	x0, x0, 1
	add	x1, x26, x0, lsl 2
	ldr	w1, [x1,-4]
	cmp	w2, w1
	beq	.L3174
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L3159
.L3196:
	add	w25, w25, 1
	cmp	w25, 5
	bhi	.L3178
.L3159:
	ldr	w0, [x29,140]
	add	w0, w0, 1
	str	w0, [x29,140]
	cmp	w0, 4
	bne	.L3177
.L3178:
	mov	x0, x22
	bl	ftl_free
	cmp	w25, wzr
	csetm	w0, eq
.L3187:
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
.L3198:
	cmp	w1, w2
	bls	.L3200
	ldrb	w4, [x5,x2]
	add	x6, x3, 2200
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3198
.L3200:
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
	beq	.L3203
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3204
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3205
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3206
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3207
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3208
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3264
	bl	rknand_dev_flush
	b	.L3298
.L3208:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3210
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3211
	b	.L3264
.L3206:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3212
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3213
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3214
	b	.L3264
.L3213:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3212
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3212
	b	.L3264
.L3204:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3215
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3216
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3217
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3218
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3219
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3220
	b	.L3264
.L3216:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3221
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3222
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3223
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3224
	b	.L3264
.L3222:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3224
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3221
	b	.L3264
.L3218:
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3225
.L3230:
	mov	x0, -12
	b	.L3202
.L3225:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3305
	adrp	x0, .LC201
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC201
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3302
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	id_block_read_data.constprop.38
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L3308
	adrp	x0, .LC202
	add	x0, x0, :lo12:.LC202
.L3300:
	bl	printk
.L3302:
	mov	x0, x19
.L3303:
	bl	ftl_free
.L3301:
	mov	x0, -14
	b	.L3202
.L3217:
	adrp	x0, .LC203
	add	x0, x0, :lo12:.LC203
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3230
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3305
	ldr	w1, [x19]
	adrp	x0, .LC204
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC204
	bl	printk
	adrp	x0, .LANCHOR8
	add	x21, x0, :lo12:.LANCHOR8
	mov	x20, x0
	ldr	x1, [x21,3536]
	cbnz	x1, .L3232
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x21,3536]
	cbz	x0, .L3302
.L3232:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3302
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3302
	add	x20, x20, :lo12:.LANCHOR8
	uxtw	x2, w2
	ldr	x1, [x20,3536]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3308:
	mov	x0, x19
	bl	ftl_free
	b	.L3298
.L3220:
	adrp	x0, .LC205
	add	x0, x0, :lo12:.LC205
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3230
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3234
.L3305:
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	b	.L3300
.L3234:
	ldr	w1, [x19]
	adrp	x0, .LC206
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC206
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3302
	adrp	x20, .LANCHOR8
	add	x20, x20, :lo12:.LANCHOR8
	ldr	x0, [x20,3536]
	cbz	x0, .L3302
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3237
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3202
.L3237:
	bl	rknand_device_lock
	ldr	x1, [x20,3536]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,3536]
	bl	ftl_free
	str	xzr, [x20,3536]
	b	.L3308
.L3219:
	adrp	x0, .LC207
	add	x0, x0, :lo12:.LC207
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3230
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3310
.L3203:
	adrp	x0, .LC208
	add	x0, x0, :lo12:.LC208
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3230
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3310
.L3215:
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	bl	printk
	adrp	x0, .LANCHOR8+3544
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR8+3544]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3293:
	bl	rk_copy_to_user
	cbnz	x0, .L3301
	b	.L3298
.L3223:
	adrp	x0, .LC210
	add	x0, x0, :lo12:.LC210
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3230
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
.L3310:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3303
	bl	ftl_free
	b	.L3229
.L3207:
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3242
.L3247:
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	bl	printk
	b	.L3301
.L3242:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3243
.L3244:
	mov	x19, -1
	b	.L3229
.L3243:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3244
	adrp	x21, .LANCHOR8
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR8
	mov	x2, 512
	ldr	x1, [x21,3544]
	bl	memcpy
	ldr	w1, [x21,3552]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3245
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3245:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3307
.L3210:
	adrp	x0, .LC212
	add	x0, x0, :lo12:.LC212
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3244
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3244
	adrp	x1, .LANCHOR8
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR8
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,3552]
	cmp	w3, w2
	bne	.L3202
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3202
	ldr	x19, [x1,3544]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3299
.L3214:
	adrp	x0, .LC213
	add	x0, x0, :lo12:.LC213
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3244
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3244
	adrp	x19, .LANCHOR8
	add	x0, x19, :lo12:.LANCHOR8
	ldr	w1, [x0,3556]
	cbnz	w1, .L3248
.L3251:
	mov	x0, 0
	b	.L3202
.L3248:
	ldr	x1, [x0,3560]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3249
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,3560]
	str	w2, [x1,4]
	ldr	x0, [x0,3560]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3249:
	add	x20, x19, :lo12:.LANCHOR8
	mov	w0, 0
	ldr	x1, [x20,3560]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,3544]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3250
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,3544]
	str	w1, [x0,4]
	ldr	x0, [x20,3544]
	str	wzr, [x0,8]
.L3250:
	add	x19, x19, :lo12:.LANCHOR8
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,3544]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,3556]
	str	wzr, [x19,3552]
	b	.L3298
.L3205:
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3244
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3244
	adrp	x19, .LANCHOR8
	add	x1, x19, :lo12:.LANCHOR8
	ldr	w2, [x1,3556]
	cmp	w2, 1
	beq	.L3251
	ldr	x2, [x1,3560]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3252
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,3560]
	str	w3, [x2,4]
	ldr	x1, [x1,3560]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3252:
	add	x20, x19, :lo12:.LANCHOR8
	mov	w0, 1
	ldr	x1, [x20,3560]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,3544]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3253
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,3544]
	str	w1, [x0,4]
	ldr	x0, [x20,3544]
	str	wzr, [x0,8]
.L3253:
	add	x19, x19, :lo12:.LANCHOR8
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,3544]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,3556]
	b	.L3298
.L3211:
	adrp	x0, .LC215
	add	x0, x0, :lo12:.LC215
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3244
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3244
	adrp	x1, .LANCHOR8
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR8
	uxtw	x2, w2
	add	x1, x1, 3568
	b	.L3306
.L3212:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3254
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	b	.L3296
.L3254:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3256
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	b	.L3296
.L3256:
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
.L3296:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3301
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR8
	bne	.L3257
	add	x0, x0, :lo12:.LANCHOR8
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,3544]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3202
	b	.L3301
.L3257:
	add	x20, x0, :lo12:.LANCHOR8
	ldr	w1, [x20,4080]
	cmp	w1, 10
	bhi	.L3301
	ldr	x1, [x20,3544]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3258
	cbz	w3, .L3258
	adrp	x0, .LC219
	mov	w1, w2
	add	x0, x0, :lo12:.LC219
	bl	printk
	ldr	w0, [x20,4080]
	add	w0, w0, 1
	str	w0, [x20,4080]
	b	.L3301
.L3258:
	add	x0, x0, :lo12:.LANCHOR8
	str	wzr, [x0,4080]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3259
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3260
.L3259:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3260:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3298
	b	.L3229
.L3224:
	adrp	x0, .LC220
	add	x0, x0, :lo12:.LC220
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3244
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3244
	mov	w0, 30224
	uxtw	x2, w2
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	add	x0, x29, 64
	bne	.L3261
	adrp	x1, .LANCHOR8+4088
	ldr	x1, [x1,#:lo12:.LANCHOR8+4088]
	b	.L3297
.L3261:
	adrp	x1, .LANCHOR9-256
	ldr	x1, [x1,#:lo12:.LANCHOR9-256]
.L3297:
	add	x1, x1, 8
.L3306:
	bl	memcpy
.L3307:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3293
.L3221:
	adrp	x0, .LC221
	add	x0, x0, :lo12:.LC221
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3247
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3244
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3244
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3263
	adrp	x19, .LANCHOR8
	add	x1, x29, 56
	add	x19, x19, :lo12:.LANCHOR8
	ldr	x0, [x19,4088]
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,4088]
	b	.L3299
.L3263:
	adrp	x19, .LANCHOR9
	add	x1, x29, 56
	add	x19, x19, :lo12:.LANCHOR9
	ldr	x0, [x19,-256]
	bl	memcpy
	ldr	x1, [x19,-256]
	mov	w0, 3
.L3299:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3229
.L3298:
	mov	x19, 0
.L3229:
	adrp	x0, .LC222
	mov	x1, x19
	add	x0, x0, :lo12:.LC222
	bl	printk
	mov	x0, x19
	b	.L3202
.L3264:
	mov	x0, -22
.L3202:
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
	adrp	x0, .LANCHOR7+3240
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR8
	ldr	x1, [x0,#:lo12:.LANCHOR7+3240]
	add	x19, x20, :lo12:.LANCHOR8
	str	x23, [sp,48]
	add	x0, x1, 512
	stp	x21, x22, [sp,32]
	str	x0, [x19,3544]
	adrp	x21, .LANCHOR9
	add	x0, x1, 1024
	str	x0, [x19,4088]
	add	x0, x21, :lo12:.LANCHOR9
	str	x1, [x19,3560]
	add	x1, x1, 1536
	str	xzr, [x19,3536]
	str	x1, [x0,-256]
	add	x0, x19, 3568
	bl	ftl_memcpy
	ldr	x22, [x19,3560]
	str	wzr, [x19,3552]
	str	wzr, [x19,4080]
	ldr	w0, [x22,16]
	ldr	w23, [x22,508]
	str	w0, [x19,3556]
	cbz	w23, .L3313
	mov	x0, x22
	mov	w1, 508
	bl	js_hash
	cmp	w23, w0
	beq	.L3313
	adrp	x0, .LC223
	str	wzr, [x22,16]
	add	x0, x0, :lo12:.LC223
	str	wzr, [x19,3556]
	bl	printk
.L3313:
	add	x0, x20, :lo12:.LANCHOR8
	ldr	w1, [x0,3556]
	cbz	w1, .L3314
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,3552]
.L3314:
	add	x20, x20, :lo12:.LANCHOR8
	add	x21, x21, :lo12:.LANCHOR9
	mov	w0, 2
	ldr	x1, [x20,4088]
	bl	StorageSysDataLoad
	ldr	x1, [x21,-256]
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
	mov	w1, 193
	mov	x0, 65536
	add	x29, sp, 0
	movk	w1, 0x240, lsl 16
	mov	w2, 4
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR9
	bl	kmalloc_order_trace
	add	x1, x21, :lo12:.LANCHOR9
	mov	w19, -12
	str	x0, [x1,-248]
	cbz	x0, .L3323
	mov	w23, 0
	adrp	x24, .LC224
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC224
.L3327:
	add	x20, x21, :lo12:.LANCHOR9
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,-248]
	bl	FlashBootVendorRead
	cbnz	w0, .L3324
	ldr	x3, [x20,-248]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,-248]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3325
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3325
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3325:
	cbnz	w22, .L3335
	mov	w22, 1
	b	.L3327
.L3335:
	cbz	w19, .L3328
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3323
	b	.L3324
.L3328:
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
	b	.L3323
.L3324:
	add	x21, x21, :lo12:.LANCHOR9
	mov	w19, -1
	ldr	x0, [x21,-248]
	bl	kfree
	str	xzr, [x21,-248]
.L3323:
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
	adrp	x3, .LANCHOR9-248
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR9-248]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3337
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3338:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3342
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3338
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
	b	.L3337
.L3342:
	mov	w3, -1
.L3337:
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
	adrp	x0, .LANCHOR9-248
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR9-248]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3344
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3345:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3360
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3345
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3346
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3344
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3347:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3361
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
	b	.L3347
.L3361:
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
	b	.L3359
.L3346:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3359
.L3360:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3344
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
.L3359:
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
.L3344:
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
	stp	x29, x30, [sp, -48]!
	adrp	x0, kmalloc_caches+96
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	ldr	x0, [x0,#:lo12:kmalloc_caches+96]
	mov	w22, w1
	mov	w1, 192
	mov	x21, x2
	movk	w1, 0x240, lsl 16
	mov	x2, 4096
	stp	x19, x20, [sp,16]
	bl	kmem_cache_alloc_trace
	mov	x20, -1
	mov	x19, x0
	cbz	x0, .L3363
	mov	w1, 30209
	movk	w1, 0x4004, lsl 16
	cmp	w22, w1
	beq	.L3365
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w22, w2
	beq	.L3366
	b	.L3377
.L3365:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3377
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3368
.L3369:
	mov	x20, -1
	b	.L3364
.L3368:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3369
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3364
.L3366:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3377
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3369
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3369
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3377
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3364
.L3377:
	mov	x20, -14
.L3364:
	mov	x0, x19
	bl	kfree
.L3363:
	mov	x0, x20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	.type	__func__.20006, %object
	.size	__func__.20006, 18
__func__.20006:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.20031, %object
	.size	__func__.20031, 23
__func__.20031:
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
	.type	__func__.18483, %object
	.size	__func__.18483, 28
__func__.18483:
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
	.type	__func__.18458, %object
	.size	__func__.18458, 26
__func__.18458:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.18469, %object
	.size	__func__.18469, 21
__func__.18469:
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
	.type	__func__.18534, %object
	.size	__func__.18534, 22
__func__.18534:
	.string	"flash_start_page_read"
	.zero	2
	.type	__func__.19121, %object
	.size	__func__.19121, 13
__func__.19121:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.19134, %object
	.size	__func__.19134, 10
__func__.19134:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.19148, %object
	.size	__func__.19148, 16
__func__.19148:
	.string	"buf_remove_free"
	.type	__func__.19396, %object
	.size	__func__.19396, 13
__func__.19396:
	.string	"ftl_read_buf"
	.zero	3
	.type	__func__.19614, %object
	.size	__func__.19614, 12
__func__.19614:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.19679, %object
	.size	__func__.19679, 19
__func__.19679:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.19730, %object
	.size	__func__.19730, 16
__func__.19730:
	.string	"gc_free_src_blk"
	.type	__func__.20200, %object
	.size	__func__.20200, 18
__func__.20200:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.20210, %object
	.size	__func__.20210, 17
__func__.20210:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.20313, %object
	.size	__func__.20313, 23
__func__.20313:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.20353, %object
	.size	__func__.20353, 22
__func__.20353:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.20364, %object
	.size	__func__.20364, 22
__func__.20364:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.19337, %object
	.size	__func__.19337, 14
__func__.19337:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.20162, %object
	.size	__func__.20162, 15
__func__.20162:
	.string	"ftl_alloc_sblk"
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.20785, %object
	.size	__func__.20785, 17
__func__.20785:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.20813, %object
	.size	__func__.20813, 21
__func__.20813:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.19274, %object
	.size	__func__.19274, 17
__func__.19274:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.20749, %object
	.size	__func__.20749, 13
__func__.20749:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.18414, %object
	.size	__func__.18414, 19
__func__.18414:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.18549, %object
	.size	__func__.18549, 25
__func__.18549:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.19632, %object
	.size	__func__.19632, 21
__func__.19632:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.18508, %object
	.size	__func__.18508, 26
__func__.18508:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.18521, %object
	.size	__func__.18521, 22
__func__.18521:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.20676, %object
	.size	__func__.20676, 15
__func__.20676:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.18443, %object
	.size	__func__.18443, 19
__func__.18443:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.20501, %object
	.size	__func__.20501, 14
__func__.20501:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.20130, %object
	.size	__func__.20130, 15
__func__.20130:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.20389, %object
	.size	__func__.20389, 19
__func__.20389:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.20401, %object
	.size	__func__.20401, 18
__func__.20401:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.20447, %object
	.size	__func__.20447, 15
__func__.20447:
	.string	"ftl_low_format"
	.zero	1
	.type	__func__.20491, %object
	.size	__func__.20491, 23
__func__.20491:
	.string	"ftl_re_low_format_test"
	.zero	1
	.type	__func__.20287, %object
	.size	__func__.20287, 23
__func__.20287:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.20795, %object
	.size	__func__.20795, 14
__func__.20795:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.20268, %object
	.size	__func__.20268, 16
__func__.20268:
	.string	"ftl_sysblk_dump"
	.type	__func__.20735, %object
	.size	__func__.20735, 16
__func__.20735:
	.string	"load_l2p_region"
	.type	__func__.20769, %object
	.size	__func__.20769, 6
__func__.20769:
	.string	"pm_gc"
	.zero	2
	.type	__func__.19388, %object
	.size	__func__.19388, 15
__func__.19388:
	.string	"ftl_read_ahead"
	.zero	1
	.type	__func__.19666, %object
	.size	__func__.19666, 22
__func__.19666:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.19834, %object
	.size	__func__.19834, 20
__func__.19834:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.19892, %object
	.size	__func__.19892, 18
__func__.19892:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.20244, %object
	.size	__func__.20244, 14
__func__.20244:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.19434, %object
	.size	__func__.19434, 10
__func__.19434:
	.string	"zftl_read"
	.zero	6
	.type	__func__.19501, %object
	.size	__func__.19501, 10
__func__.19501:
	.string	"vpn_check"
	.zero	6
	.type	__func__.19775, %object
	.size	__func__.19775, 16
__func__.19775:
	.string	"gc_scan_src_blk"
	.type	__func__.20334, %object
	.size	__func__.20334, 19
__func__.20334:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.19326, %object
	.size	__func__.19326, 17
__func__.19326:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.19708, %object
	.size	__func__.19708, 16
__func__.19708:
	.string	"gc_do_copy_back"
	.type	__func__.19460, %object
	.size	__func__.19460, 13
__func__.19460:
	.string	"_ftl_discard"
	.zero	3
	.type	__func__.19919, %object
	.size	__func__.19919, 11
__func__.19919:
	.string	"zftl_do_gc"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC2:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC3:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC4:
	.string	"flash_erase_block %x %x\n"
.LC5:
	.string	"flash_erase_block block = %x status = %x\n"
.LC6:
	.string	"erase done: %x\n"
.LC7:
	.string	"flash_mask_bad_block %d %d\n"
.LC8:
	.string	"%s\n"
.LC9:
	.string	"FTL version: 6.0.5 20180316"
.LC10:
	.string	"FLASH ID: %x %x %x %x %x %x\n"
.LC11:
	.string	"density: %d MB\n"
.LC12:
	.string	"device density: %d MB\n"
.LC13:
	.string	"FTL INFO:\n"
.LC14:
	.string	"max_lpn = 0x%x\n"
.LC15:
	.string	"density = 0x%x\n"
.LC16:
	.string	"slc vpn = 0x%x\n"
.LC17:
	.string	"xlc vpn = 0x%x\n"
.LC18:
	.string	"free slc blk = 0x%x\n"
.LC19:
	.string	"free xlc blk = 0x%x\n"
.LC20:
	.string	"free mix blk = 0x%x\n"
.LC21:
	.string	"slc data blk = 0x%x\n"
.LC22:
	.string	"slc cache blk = 0x%x\n"
.LC23:
	.string	"xlc data blk = 0x%x\n"
.LC24:
	.string	"bad blk = %d %d\n"
.LC25:
	.string	"TBW = %d MB\n"
.LC26:
	.string	"TBR = %d MB\n"
.LC27:
	.string	"POC = %d\n"
.LC28:
	.string	"PLC = %d\n"
.LC29:
	.string	"sys run time = %d S\n"
.LC30:
	.string	"slc mode = %d\n"
.LC31:
	.string	"prog err = %d\n"
.LC32:
	.string	"read err = %d\n"
.LC33:
	.string	"GC XLC page = %d\n"
.LC34:
	.string	"GC SLC page = %d\n"
.LC35:
	.string	"discard page = 0x%x\n"
.LC36:
	.string	"version = %d\n"
.LC37:
	.string	"acblk = 0x%x %d %d\n"
.LC38:
	.string	"tmblk = 0x%x %d %d\n"
.LC39:
	.string	"gcblk = 0x%x %d %d\n"
.LC40:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC41:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC42:
	.string	"gc free blk th = %d\n"
.LC43:
	.string	"gc vpn th = %d %d %d %d\n"
.LC44:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC45:
	.string	"status: %x, ppa: %x\n"
.LC46:
	.string	"gc_write_completed: %x  %x  %x %x\n"
.LC47:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC48:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC49:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC50:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC51:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC52:
	.string	"list count:%p %d\n"
.LC53:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC54:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC55:
	.string	"gc_free_src_blk %x, %d\n"
.LC56:
	.string	"ftl_vpn_decrement %x = %d\n"
.LC57:
	.string	"mask bad block:cs %x block: %x\n"
.LC58:
	.string	"free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC59:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC60:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC61:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC62:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC63:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC64:
	.string	"alloc sblk %x %d\n"
.LC65:
	.string	"dump_sblk_queue: %d\n"
.LC66:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC67:
	.string	"blk %x is bad block\n"
.LC68:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC69:
	.string	"%s %d %d\n"
.LC70:
	.string	"gc_static_wearleveling: slc blk: %d, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC71:
	.string	"gc_static_wearleveling: min slc ec: %d, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC72:
	.string	"swl add tlc gc = %d, %d, %d, %d, %d, %d\n"
.LC73:
	.string	"swl add slc gc  = %d, %d, %d, %d, %d, %d\n"
.LC74:
	.string	"free blk vpn error: %x %x\n"
.LC75:
	.string	"rk_ftl_de_init %x\n"
.LC76:
	.string	"%s %p:0x%x:"
.LC77:
	.string	"%x "
.LC78:
	.string	""
.LC79:
	.string	"otp error! %d"
.LC80:
	.string	"rr"
.LC81:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC82:
	.string	"nandc:"
.LC83:
	.string	"%d flReg.d32=%x %x\n"
.LC84:
	.string	"nandc_xfer_done read error %x\n"
.LC85:
	.string	"dqs data abort %x\n"
.LC86:
	.string	"xfer error %x\n"
.LC87:
	.string	"flash_read_page %x %x %x\n"
.LC88:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC89:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC90:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC91:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC92:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC93:
	.string	"%d flash_ddr_tunning_read %x ecc=%d\n"
.LC94:
	.string	"sync para %d\n"
.LC95:
	.string	"DDR mode Read error %x %x\n"
.LC96:
	.string	"flash_read_page_en %x %x\n"
.LC97:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC98:
	.string	"no sys info %x\n"
.LC99:
	.string	"read page: %x %x %x %x\n"
.LC100:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC101:
	.string	"set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC102:
	.string	"1ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC103:
	.string	"flash_prog_page %x %x %x\n"
.LC104:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC105:
	.string	"flash_prog_page_en:%x %x\n"
.LC106:
	.string	"spare"
.LC107:
	.string	"data"
.LC108:
	.string	"write error: %x\n"
.LC109:
	.string	"low format %d\n"
.LC110:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC111:
	.string	"%d %x @%d %x\n"
.LC112:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC113:
	.string	"low format %d %d %d %d\n"
.LC114:
	.string	"re low format %d\n"
.LC115:
	.string	"saved_active_page  = %x\n"
.LC116:
	.string	"saved_active_plane = %x\n"
.LC117:
	.string	"sblk = %x\n"
.LC118:
	.string	"phy_blk = %x %x\n"
.LC119:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC120:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC121:
	.string	"dump write = %x\n"
.LC122:
	.string	"pm_write_page write error: %x\n"
.LC123:
	.string	"finfo:"
.LC124:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC125:
	.string	"sys_info_flush error:%x\n"
.LC126:
	.string	"l2p:"
.LC127:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC128:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC129:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC130:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC131:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC132:
	.string	"lpa:"
.LC133:
	.string	"vpn:"
.LC134:
	.string	"sblk:"
.LC135:
	.string	"lpa_hash:"
.LC136:
	.string	"lpa_hash_index:"
.LC137:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC138:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC139:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC140:
	.string	"pm_ppa:"
.LC141:
	.string	"data:"
.LC142:
	.string	"spare:"
.LC143:
	.string	"pm_init posr %x %x %x\n"
.LC144:
	.string	"pm_init recovery %x %x %x\n"
.LC145:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC146:
	.string	"ftl_read_ahead refresh =%x, lpa = %x, ppa= %x\n"
.LC147:
	.string	"id=%d, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC148:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC149:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC150:
	.string	"gc_recovery"
.LC151:
	.string	"gc_recovery: %x vpn = %x\n"
.LC152:
	.string	"ftl_init %x\n"
.LC153:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC154:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC155:
	.string	"0lpa: %x %x %x\n"
.LC156:
	.string	"lpa: %x %x %x\n"
.LC157:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC158:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC159:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC160:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC161:
	.string	"index= %x, lpa=%x\n"
.LC162:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC163:
	.string	"ftl_read %x %x %x\n"
.LC164:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC165:
	.string	"acblk = %x, tempblk = %x, gc_des = %x\n"
.LC166:
	.string	"acblk = %x, free page = %x, vpn = %x %x\n"
.LC167:
	.string	"tmp_blk = %x, free page = %x, vpn = %x %x\n"
.LC168:
	.string	"vpn_check lpa = %x, ppa = %x, blk = %x\n"
.LC169:
	.string	"l2p"
.LC170:
	.string	"vpn_check %x = c %x  s %x function: %x\n"
.LC171:
	.string	"vpn_check blk = %x fix vpc\n"
.LC172:
	.string	"...%s exit...%d\n"
.LC173:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC174:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC175:
	.string	"ftl_scan_all_data = %x\n"
.LC176:
	.string	"scan lpa = %x ppa= %x\n"
.LC177:
	.string	"lba = %x,addr= %x, ststus = %x, spare= %x %x %x %x data=%x %x\n"
.LC178:
	.string	"lba = %x, id= %x, index = %x hash ppa = %x\n"
.LC179:
	.string	"0pm:"
.LC180:
	.string	"1pm:"
.LC181:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC182:
	.string	"ftl_update_l2p_map"
.LC183:
	.string	"lpa_tbl:"
.LC184:
	.string	"sblk %x vpn: %d %d\n"
.LC185:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC186:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC187:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC188:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x\n"
.LC189:
	.string	"%d prog_step: %x %x buf id= %x ppa = %x hash=%x id = %x plane = %x lpa=%x\n"
.LC190:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC191:
	.string	"gc %d: %d %d %d %d %d %d %d\n"
.LC192:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC193:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC194:
	.string	"gc free %x, %d\n"
.LC195:
	.string	"ftlwrite %x %x %x\n"
.LC196:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC197:
	.string	"write_idblock fix data %x %x\n"
.LC198:
	.string	"1 write_idblock fix data %x %x %x\n"
.LC199:
	.string	"READ_SECTOR_IO\n"
.LC200:
	.string	"rk_copy_from_user error\n"
.LC201:
	.string	"READ_SECTOR_IO %x %x\n"
.LC202:
	.string	"rk_copy_to_user error\n"
.LC203:
	.string	"WRITE_SECTOR_IO\n"
.LC204:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC205:
	.string	"END_WRITE_SECTOR_IO\n"
.LC206:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC207:
	.string	"GET_FLASH_INFO_IO\n"
.LC208:
	.string	"GET_BAD_BLOCK_IO\n"
.LC209:
	.string	"GET_LOCK_FLAG_IO\n"
.LC210:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC211:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC212:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC213:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC214:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC215:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC216:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC217:
	.string	"RKNAND_LOADER_STATUS\n"
.LC218:
	.string	"RKNAND_LOADER_LOCK\n"
.LC219:
	.string	"LockKey not match %d\n"
.LC220:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC221:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC222:
	.string	"return ret = %lx\n"
.LC223:
	.string	"secureBootEn check error\n"
.LC224:
	.string	"\0013vendor storage %x,%x,%x\n"
	.data
	.align	3
.LANCHOR2 = . + 0
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
	.type	zftl_debug, %object
	.size	zftl_debug, 4
zftl_debug:
	.word	17476
	.zero	4
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
	.size	zftl_nand_flash_para_tbl, 864
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
	.hword	1497
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
	.hword	1497
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
	.byte	6
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
	.byte	6
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
	.byte	6
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
	.type	nandc_hw_seed, %object
	.size	nandc_hw_seed, 1
nandc_hw_seed:
	.zero	1
	.zero	7
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
	.type	g_nand_max_die, %object
	.size	g_nand_max_die, 1
g_nand_max_die:
	.zero	1
	.zero	3
	.type	g_die_cs_idx, %object
	.size	g_die_cs_idx, 8
g_die_cs_idx:
	.zero	8
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
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 1
g_flash_cur_mode:
	.zero	1
	.type	g_flash_interface_mode, %object
	.size	g_flash_interface_mode, 1
g_flash_interface_mode:
	.zero	1
	.zero	2
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.zero	32
	.type	g_flash_toggle_mode_en, %object
	.size	g_flash_toggle_mode_en, 1
g_flash_toggle_mode_en:
	.zero	1
	.type	g_flash_multi_page_prog_en, %object
	.size	g_flash_multi_page_prog_en, 1
g_flash_multi_page_prog_en:
	.zero	1
	.zero	2
	.type	gp_nand_para_info, %object
	.size	gp_nand_para_info, 8
gp_nand_para_info:
	.zero	8
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
	.type	write_buf_count, %object
	.size	write_buf_count, 1
write_buf_count:
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
	.type	write_commit_count, %object
	.size	write_commit_count, 1
write_commit_count:
	.zero	1
	.type	sblk_write_completed_queue_head, %object
	.size	sblk_write_completed_queue_head, 1
sblk_write_completed_queue_head:
	.zero	1
	.type	sblk_queue_head, %object
	.size	sblk_queue_head, 1
sblk_queue_head:
	.zero	1
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
	.type	g_nand_opt_para, %object
	.size	g_nand_opt_para, 32
g_nand_opt_para:
	.zero	32
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
	.type	g_idb_ecc_bits, %object
	.size	g_idb_ecc_bits, 1
g_idb_ecc_bits:
	.zero	1
	.zero	6
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
	.type	ftl_power_lost_flag, %object
	.size	ftl_power_lost_flag, 1
ftl_power_lost_flag:
	.zero	1
	.zero	63
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
	.type	g_flash_multi_page_read_en, %object
	.size	g_flash_multi_page_read_en, 1
g_flash_multi_page_read_en:
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
