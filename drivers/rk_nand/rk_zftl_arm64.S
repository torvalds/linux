/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
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
	mov	w4, w3
	cmp	w3, w2
	bcc	.L4
	mov	w0, 0
	ret
.L4:
	ldrb	w5, [x0, x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6, -1]
	cmp	w6, w5
	beq	.L2
	add	w0, w4, 1
	ret
	.size	flash_mem_cmp8, .-flash_mem_cmp8
	.align	2
	.type	slc_phy_page_address_calc, %function
slc_phy_page_address_calc:
	adrp	x1, .LANCHOR0
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w3, [x1, #:lo12:.LANCHOR0]
	cbz	w3, .L7
	ldrb	w2, [x2, 1]
	cbz	w2, .L8
.L7:
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w3, [x1, 2]
	udiv	w2, w0, w3
	mul	w2, w2, w3
	ldrb	w3, [x1, 1]
	sub	w0, w0, w2
	cbz	w3, .L9
	add	w0, w2, w0, lsl 1
	ret
.L9:
	add	x1, x1, 4
	ldrh	w0, [x1, w0, uxtw 1]
	add	w0, w0, w2
.L8:
	ret
	.size	slc_phy_page_address_calc, .-slc_phy_page_address_calc
	.align	2
	.global	zftl_nandc_get_irq_status
	.type	zftl_nandc_get_irq_status, %function
zftl_nandc_get_irq_status:
	adrp	x1, .LANCHOR0+1028
	ldrb	w1, [x1, #:lo12:.LANCHOR0+1028]
	cmp	w1, 9
	bne	.L17
	ldr	w0, [x0, 296]
	ret
.L17:
	ldr	w0, [x0, 372]
	ret
	.size	zftl_nandc_get_irq_status, .-zftl_nandc_get_irq_status
	.section	.text.unlikely,"ax",@progbits
	.align	2
	.type	isxdigit, %function
isxdigit:
	and	w1, w0, -33
	sub	w1, w1, #65
	cmp	w1, 25
	bls	.L21
	sub	w0, w0, #48
	cmp	w0, 9
	cset	w0, ls
	ret
.L21:
	mov	w0, 1
	ret
	.size	isxdigit, .-isxdigit
	.text
	.align	2
	.global	zftl_get_density
	.type	zftl_get_density, %function
zftl_get_density:
	cbnz	w0, .L23
	adrp	x0, .LANCHOR0+1032
	ldr	w0, [x0, #:lo12:.LANCHOR0+1032]
	ret
.L23:
	cmp	w0, 4
	cset	w0, cc
	lsl	w0, w0, 13
	ret
	.size	zftl_get_density, .-zftl_get_density
	.align	2
	.type	_list_remove_node, %function
_list_remove_node:
	stp	x29, x30, [sp, -64]!
	and	w1, w1, 65535
	mov	w4, 6
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x3, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	umull	x21, w1, w4
	mov	w1, 65535
	ldr	x23, [x3, 1040]
	add	x20, x23, x21
	ldrh	w4, [x23, x21]
	ldrh	w3, [x20, 2]
	cmp	w4, w1
	ldr	x1, [x0]
	bne	.L27
	cmp	w3, w4
	bne	.L27
	cmp	x20, x1
	bne	.L26
.L27:
	mov	x22, x0
	mov	x24, x2
	mov	w0, 65535
	cmp	w3, w0
	bne	.L29
	cmp	x20, x1
	beq	.L29
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 202
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L29:
	ldr	x0, [x22]
	ldrh	w3, [x23, x21]
	cmp	x20, x0
	mov	w0, 65535
	bne	.L30
	cmp	w3, w0
	bne	.L31
	str	xzr, [x22]
.L32:
	mov	w0, -1
	strh	w0, [x23, x21]
	strh	w0, [x20, 2]
	ldrh	w0, [x24]
	sub	w0, w0, #1
	strh	w0, [x24]
.L26:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L31:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x0, [x19, 1040]
	umaddl	x3, w3, w1, x0
	mov	w0, -1
	str	x3, [x22]
	strh	w0, [x3, 2]
	b	.L32
.L30:
	cmp	w3, w0
	ldrh	w0, [x20, 2]
	bne	.L33
	cmp	w0, w3
	beq	.L32
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	mov	w2, -1
	umull	x0, w0, w1
	ldr	x1, [x19, 1040]
.L35:
	strh	w2, [x1, x0]
	b	.L32
.L33:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 6
	ldr	x2, [x19, 1040]
	umaddl	x3, w3, w1, x2
	strh	w0, [x3, 2]
	ldrh	w0, [x20, 2]
	ldrh	w2, [x23, x21]
	umull	x0, w0, w1
	ldr	x1, [x19, 1040]
	b	.L35
	.size	_list_remove_node, .-_list_remove_node
	.align	2
	.type	hynix_set_rr_para, %function
hynix_set_rr_para:
	stp	x29, x30, [sp, -64]!
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	and	w1, w1, 255
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w0, w0, 255
	str	x23, [sp, 48]
	stp	x19, x20, [sp, 16]
	ldr	x2, [x3, 1048]
	ldr	x19, [x3, 1056]
	add	x23, x2, 128
	add	x21, x2, 112
	ldrb	w22, [x2, 113]
	ldrb	w2, [x2, 112]
	cmp	w2, 8
	mov	x2, 32
	umaddl	x1, w1, w22, x2
	beq	.L37
	mov	w2, 160
	umaddl	x1, w2, w0, x1
.L37:
	ubfiz	x0, x0, 8, 8
	add	x21, x21, x1
	add	x19, x19, x0
	mov	x20, 0
	mov	w0, 54
	str	w0, [x19, 2056]
.L39:
	cmp	w22, w20, uxtb
	bhi	.L40
	mov	w0, 22
	str	w0, [x19, 2056]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L40:
	ldrb	w0, [x23, x20]
	str	w0, [x19, 2052]
	mov	x0, 600
	bl	__const_udelay
	ldrsb	w0, [x21, x20]
	add	x20, x20, 1
	str	w0, [x19, 2048]
	b	.L39
	.size	hynix_set_rr_para, .-hynix_set_rr_para
	.align	2
	.type	zftl_debug_proc_open, %function
zftl_debug_proc_open:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x1
	bl	PDE_DATA
	mov	x2, x0
	adrp	x1, zftl_debug_proc_show
	mov	x0, x19
	add	x1, x1, :lo12:zftl_debug_proc_show
	bl	single_open
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_debug_proc_open, .-zftl_debug_proc_open
	.align	2
	.type	zftl_debug_proc_show, %function
zftl_debug_proc_show:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LC1
	adrp	x1, .LC2
	add	x2, x2, :lo12:.LC1
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x1, x1, :lo12:.LC2
	mov	x19, x0
	bl	seq_printf
	adrp	x0, .LANCHOR2
	adrp	x1, .LC3
	add	x1, x1, :lo12:.LC3
	ldr	w2, [x0, #:lo12:.LANCHOR2]
	mov	x0, x19
	bl	seq_printf
	ldr	x19, [sp, 16]
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
	ldrb	w1, [x0, 1028]
	cmp	w1, 9
	ldr	x1, [x0, 1056]
	ldr	w2, [x1]
	str	w2, [x0, 1064]
	ldr	w2, [x1, 4]
	str	w2, [x0, 1068]
	bne	.L47
	ldr	w2, [x1, 16]
	str	w2, [x0, 1072]
	ldr	w2, [x1, 32]
	str	w2, [x0, 1076]
	ldr	w2, [x1, 80]
	str	w2, [x0, 1080]
	ldr	w2, [x1, 84]
	str	w2, [x0, 1084]
	ldr	w2, [x1, 520]
	ldr	w1, [x1, 8]
	str	w2, [x0, 1088]
.L49:
	str	w1, [x0, 1092]
	ret
.L47:
	ldr	w2, [x1, 8]
	str	w2, [x0, 1072]
	ldr	w2, [x1, 12]
	str	w2, [x0, 1076]
	ldr	w2, [x1, 304]
	str	w2, [x0, 1080]
	ldr	w2, [x1, 308]
	str	w2, [x0, 1084]
	ldr	w2, [x1, 336]
	str	w2, [x0, 1088]
	ldr	w1, [x1, 344]
	b	.L49
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	adrp	x2, .LANCHOR0+1028
	ldrb	w2, [x2, #:lo12:.LANCHOR0+1028]
	cmp	w2, 9
	mov	x2, 1
	bne	.L51
	ldr	w3, [x0, 292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0, 292]
	ldr	w2, [x0, 288]
	bic	w1, w2, w1
	str	w1, [x0, 288]
	ret
.L51:
	ldr	w3, [x0, 368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0, 368]
	ldr	w2, [x0, 364]
	bic	w1, w2, w1
	str	w1, [x0, 364]
	ret
	.size	nandc_irq_disable, .-nandc_irq_disable
	.align	2
	.type	_insert_free_list, %function
_insert_free_list:
	adrp	x9, .LANCHOR0
	add	x3, x9, :lo12:.LANCHOR0
	and	w1, w1, 65535
	ldrh	w4, [x3, 1096]
	cmp	w4, w1
	bls	.L53
	ldrh	w4, [x2]
	mov	w6, 6
	add	w4, w4, 1
	strh	w4, [x2]
	umull	x10, w1, w6
	mov	w2, -1
	ldr	x11, [x3, 1040]
	add	x5, x11, x10
	strh	w2, [x5, 2]
	strh	w2, [x11, x10]
	ldr	x2, [x0]
	cbnz	x2, .L55
.L60:
	str	x5, [x0]
	ret
.L55:
	ldr	x8, [x3, 1104]
	ubfiz	x7, x1, 2, 16
	ldrh	w13, [x3, 1112]
	mov	w15, 65535
	ldr	x14, [x3, 1040]
	ldr	w4, [x8, x7]
	sub	x3, x2, x14
	ldrh	w7, [x8, x7]
	asr	x3, x3, 1
	and	w7, w7, 2047
	ubfx	x4, x4, 11, 8
	madd	w4, w13, w4, w7
	mov	x7, -6148914691236517206
	movk	x7, 0xaaab, lsl 0
	and	w4, w4, 65535
	mul	x3, x3, x7
	mov	w7, w6
	and	w3, w3, 65535
.L58:
	ubfiz	x12, x3, 2, 16
	ldr	w6, [x8, x12]
	ldrh	w12, [x8, x12]
	ubfx	x6, x6, 11, 8
	and	w12, w12, 2047
	madd	w6, w13, w6, w12
	cmp	w4, w6, uxth
	bls	.L56
	ldrh	w6, [x2]
	cmp	w6, w15
	bne	.L57
	strh	w3, [x5, 2]
	strh	w1, [x2]
	ret
.L57:
	umaddl	x2, w6, w7, x14
	mov	w3, w6
	b	.L58
.L56:
	ldrh	w4, [x2, 2]
	strh	w4, [x5, 2]
	strh	w3, [x11, x10]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L59
	strh	w1, [x2, 2]
	b	.L60
.L59:
	ldrh	w0, [x2, 2]
	add	x3, x9, :lo12:.LANCHOR0
	mov	w4, 6
	ldr	x3, [x3, 1040]
	umull	x0, w0, w4
	strh	w1, [x3, x0]
	strh	w1, [x2, 2]
.L53:
	ret
	.size	_insert_free_list, .-_insert_free_list
	.align	2
	.type	_insert_data_list, %function
_insert_data_list:
	adrp	x9, .LANCHOR0
	add	x5, x9, :lo12:.LANCHOR0
	and	w1, w1, 65535
	ldrh	w3, [x5, 1096]
	cmp	w3, w1
	bls	.L83
	ldrh	w3, [x2]
	mov	w11, 6
	add	w3, w3, 1
	strh	w3, [x2]
	umull	x11, w1, w11
	mov	w2, -1
	ldr	x13, [x5, 1040]
	add	x10, x13, x11
	strh	w2, [x10, 2]
	strh	w2, [x13, x11]
	ldr	x2, [x0]
	cbnz	x2, .L64
	str	x10, [x0]
	ret
.L64:
	stp	x29, x30, [sp, -32]!
	uxtw	x4, w1
	add	x29, sp, 0
	str	x19, [sp, 16]
	ldr	x14, [x5, 1120]
	ldrh	w6, [x10, 4]
	ldr	x12, [x5, 1104]
	ldrh	w15, [x5, 1112]
	ldrh	w8, [x14, x4, lsl 1]
	lsl	x4, x4, 2
	ldr	w3, [x12, x4]
	mul	w8, w8, w6
	ldrh	w4, [x12, x4]
	ubfx	x3, x3, 11, 8
	and	w4, w4, 2047
	cbz	w8, .L65
	madd	w3, w15, w3, w4
	add	w8, w8, w3, uxth
.L65:
	add	x4, x9, :lo12:.LANCHOR0
	mov	x5, -6148914691236517206
	movk	x5, 0xaaab, lsl 0
	cmp	w6, 0
	csinv	w8, w8, wzr, ne
	mov	w19, 65535
	ldr	x16, [x4, 1040]
	mov	w18, 6
	ldrh	w30, [x4, 1096]
	sub	x3, x2, x16
	asr	x3, x3, 1
	mul	x3, x3, x5
	mov	w5, 0
	and	w3, w3, 65535
.L71:
	add	w5, w5, 1
	and	w5, w5, 65535
	cmp	w5, w30
	bhi	.L61
	cmp	w1, w3
	beq	.L61
	uxtw	x7, w3
	ldrh	w17, [x2, 4]
	ldrh	w4, [x14, x7, lsl 1]
	lsl	x7, x7, 2
	ldr	w6, [x12, x7]
	mul	w4, w4, w17
	ldrh	w7, [x12, x7]
	ubfx	x6, x6, 11, 8
	and	w7, w7, 2047
	cbz	w4, .L68
	madd	w6, w15, w6, w7
	add	w4, w4, w6, uxth
.L68:
	cbz	w17, .L69
	cmp	w8, w4
	bls	.L69
	ldrh	w4, [x2]
	cmp	w4, w19
	bne	.L70
	strh	w3, [x10, 2]
	strh	w1, [x2]
.L61:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L70:
	umaddl	x2, w4, w18, x16
	mov	w3, w4
	b	.L71
.L69:
	ldrh	w4, [x2, 2]
	strh	w4, [x10, 2]
	strh	w3, [x13, x11]
	ldr	x3, [x0]
	cmp	x2, x3
	bne	.L72
	strh	w1, [x2, 2]
	str	x10, [x0]
	b	.L61
.L72:
	ldrh	w0, [x2, 2]
	add	x9, x9, :lo12:.LANCHOR0
	mov	w3, 6
	umull	x0, w0, w3
	ldr	x3, [x9, 1040]
	strh	w1, [x3, x0]
	strh	w1, [x2, 2]
	b	.L61
.L83:
	ret
	.size	_insert_data_list, .-_insert_data_list
	.align	2
	.type	_list_update_data_list, %function
_list_update_data_list:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w1, 65535
	adrp	x1, .LANCHOR0
	add	x4, x1, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	x3, [x4, 1128]
	ldrh	w5, [x3, 16]
	cmp	w5, w19
	beq	.L85
	mov	x20, x1
	ldrh	w1, [x3, 48]
	cmp	w1, w19
	beq	.L85
	ldrh	w1, [x3, 80]
	cmp	w1, w19
	beq	.L85
	mov	w23, 6
	ldr	x25, [x4, 1040]
	ldr	x1, [x0]
	umull	x23, w19, w23
	add	x24, x25, x23
	cmp	x24, x1
	beq	.L85
	mov	x21, x0
	ldrh	w0, [x24, 2]
	mov	w1, 65535
	mov	x22, x2
	cmp	w0, w1
	bne	.L89
	ldrh	w1, [x25, x23]
	cmp	w1, w0
	bne	.L89
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 24
	mov	w2, 273
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L89:
	ldrh	w3, [x24, 2]
	mov	w0, 65535
	cmp	w3, w0
	bne	.L90
	ldrh	w0, [x25, x23]
	cmp	w0, w3
	beq	.L85
.L90:
	add	x1, x20, :lo12:.LANCHOR0
	ubfiz	x2, x19, 1, 16
	ldrh	w4, [x24, 4]
	mov	x6, -6148914691236517206
	movk	x6, 0xaaab, lsl 0
	mov	w5, -1
	ldr	x0, [x1, 1120]
	cmp	w4, 0
	ldr	x1, [x1, 1040]
	ldrh	w2, [x0, x2]
	mul	w2, w2, w4
	mov	w4, 6
	umull	x3, w3, w4
	csel	w2, w2, w5, ne
	asr	x4, x3, 1
	add	x3, x1, x3
	mul	x4, x4, x6
	ldrh	w1, [x3, 4]
	cmp	w1, 0
	ldrh	w0, [x0, x4, lsl 1]
	mul	w0, w0, w1
	csel	w0, w0, w5, ne
	cmp	w2, w0
	bcs	.L85
	mov	x2, x22
	mov	w1, w19
	mov	x0, x21
	bl	_list_remove_node
	mov	x2, x22
	mov	w1, w19
	mov	x0, x21
	bl	_insert_data_list
.L85:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	_list_update_data_list, .-_list_update_data_list
	.section	.text.unlikely
	.align	2
	.type	rk_simple_strtoull.constprop.33, %function
rk_simple_strtoull.constprop.33:
	stp	x29, x30, [sp, -16]!
	mov	x3, x0
	mov	x6, x1
	add	x29, sp, 0
	ldrb	w0, [x0]
	cmp	w0, 48
	bne	.L107
	ldrb	w0, [x3, 1]
	add	x2, x3, 1
	orr	w0, w0, 32
	and	w0, w0, 255
	cmp	w0, 120
	bne	.L109
	ldrb	w0, [x3, 2]
	bl	isxdigit
	cbz	w0, .L109
	add	x2, x3, 2
	mov	w4, 16
.L97:
	mov	w5, 0
.L98:
	ldrb	w3, [x2]
	mov	w0, w3
	bl	isxdigit
	cbnz	w0, .L99
.L105:
	cbnz	x6, .L100
.L96:
	mov	w0, w5
	ldp	x29, x30, [sp], 16
	ret
.L107:
	mov	x2, x3
	mov	w4, 10
	b	.L97
.L109:
	mov	w4, 8
	b	.L97
.L99:
	sub	w0, w3, #48
	cmp	w0, 9
	bhi	.L110
.L106:
	cmp	w0, w4
	bcs	.L105
	madd	w5, w5, w4, w0
	add	x2, x2, 1
	b	.L98
.L100:
	str	x2, [x6]
	b	.L96
.L110:
	orr	w0, w3, 32
	sub	w0, w0, #87
	b	.L106
	.size	rk_simple_strtoull.constprop.33, .-rk_simple_strtoull.constprop.33
	.text
	.align	2
	.type	nandc_de_cs.constprop.35, %function
nandc_de_cs.constprop.35:
	adrp	x0, .LANCHOR0+1056
	ldr	x1, [x0, #:lo12:.LANCHOR0+1056]
	ldr	w0, [x1]
	and	w0, w0, -256
	and	w0, w0, -131073
	str	w0, [x1]
	ret
	.size	nandc_de_cs.constprop.35, .-nandc_de_cs.constprop.35
	.align	2
	.global	flash_read_status
	.type	flash_read_status, %function
flash_read_status:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	mov	w0, 112
	str	w0, [x19, 8]
	mov	x0, 600
	bl	__const_udelay
	ldr	w0, [x19]
	ldr	x19, [sp, 16]
	and	w0, w0, 255
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_read_status, .-flash_read_status
	.align	2
	.global	toshiba_set_rr_para
	.type	toshiba_set_rr_para, %function
toshiba_set_rr_para:
	stp	x29, x30, [sp, -80]!
	and	w1, w1, 255
	add	w2, w1, 1
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	mov	w0, 5
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	adrp	x22, .LANCHOR0
	umull	x2, w2, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	stp	x19, x20, [sp, 16]
	add	x25, x0, 48
	add	x24, x0, 144
	add	x25, x25, x2
	add	x24, x24, x2
	add	x22, x22, :lo12:.LANCHOR0
	mov	x19, x0
	add	x23, x0, w1, sxtw
	mov	x20, 0
	mov	w26, 85
.L116:
	ldrb	w0, [x22, 1137]
	cmp	w0, w20
	bhi	.L120
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L120:
	add	x0, x19, 144
	str	w26, [x21, 8]
	ldrsb	w0, [x20, x0]
	str	w0, [x21, 4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x22, 1136]
	cmp	w0, 34
	bne	.L117
	ldrsb	w0, [x24, x20]
.L122:
	add	x20, x20, 1
	str	w0, [x21]
	b	.L116
.L117:
	cmp	w0, 35
	bne	.L119
	ldrsb	w0, [x25, x20]
	b	.L122
.L119:
	ldrsb	w0, [x23, 192]
	b	.L122
	.size	toshiba_set_rr_para, .-toshiba_set_rr_para
	.align	2
	.global	hynix_reconfig_rr_para
	.type	hynix_reconfig_rr_para, %function
hynix_reconfig_rr_para:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w1, [x19, 1136]
	sub	w1, w1, #1
	and	w1, w1, 255
	cmp	w1, 7
	bhi	.L123
	and	w0, w0, 255
	ldr	x1, [x19, 1048]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1, 120]
	cbz	w1, .L123
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19, 1048]
	add	x0, x0, x20
	strb	wzr, [x0, 120]
.L123:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	hynix_reconfig_rr_para, .-hynix_reconfig_rr_para
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR2
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L130
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x0, .LC4
	add	x1, x1, 200
	add	x0, x0, :lo12:.LC4
	bl	printk
.L130:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L131
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w6, [x0, 6]
	ldrb	w5, [x0, 5]
	ldrb	w4, [x0, 4]
	ldrb	w3, [x0, 3]
	ldrb	w2, [x0, 2]
	ldrb	w1, [x0, 1]
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	bl	printk
.L131:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L132
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 8]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	bl	printk
.L132:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L133
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 9]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	bl	printk
.L133:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L134
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 10]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	bl	printk
.L134:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L135
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 12]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	bl	printk
.L135:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L136
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 13]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	bl	printk
.L136:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L137
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 14]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	bl	printk
.L137:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L138
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 23]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	bl	printk
.L138:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L139
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 18]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	bl	printk
.L139:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L140
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 19]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	bl	printk
.L140:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L141
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 20]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	bl	printk
.L141:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L142
	adrp	x0, .LANCHOR0+1152
	ldrb	w1, [x0, #:lo12:.LANCHOR0+1152]
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	bl	printk
.L142:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L143
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 22]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	bl	printk
.L143:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L144
	adrp	x0, .LANCHOR0+1153
	ldrb	w1, [x0, #:lo12:.LANCHOR0+1153]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	bl	printk
.L144:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L145
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 16]
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	and	w1, w1, 1
	bl	printk
.L145:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L146
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	ubfx	x1, x1, 1, 1
	bl	printk
.L146:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L147
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 2, 1
	bl	printk
.L147:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L148
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 3, 1
	bl	printk
.L148:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L149
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 4, 1
	bl	printk
.L149:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L150
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	ubfx	x1, x1, 5, 1
	bl	printk
.L150:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L151
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 6, 1
	bl	printk
.L151:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L152
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 7, 1
	bl	printk
.L152:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L153
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x0, 17]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	and	w1, w1, 1
	bl	printk
.L153:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L154
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC28
	add	x0, x0, :lo12:.LC28
	ubfx	x1, x1, 9, 1
	bl	printk
.L154:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L155
	adrp	x0, .LANCHOR0+1144
	ldr	x0, [x0, #:lo12:.LANCHOR0+1144]
	ldrh	w1, [x0, 16]
	adrp	x0, .LC29
	add	x0, x0, :lo12:.LC29
	ubfx	x1, x1, 10, 1
	bl	printk
.L155:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L156
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w2, [x1, 1154]
	ldrb	w1, [x0, #:lo12:.LANCHOR0]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	bl	printk
.L156:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L157
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0, 1171]
	ldrb	w1, [x0, 1170]
	adrp	x0, .LC31
	add	x0, x0, :lo12:.LC31
	bl	printk
.L157:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L158
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0, 1169]
	ldrb	w1, [x0, 1168]
	adrp	x0, .LC32
	add	x0, x0, :lo12:.LC32
	bl	printk
.L158:
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L129
	adrp	x0, .LANCHOR0+1192
	ldrb	w1, [x0, #:lo12:.LANCHOR0+1192]
	adrp	x0, .LC33
	add	x0, x0, :lo12:.LC33
	bl	printk
.L129:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nand_flash_print_info, .-nand_flash_print_info
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
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	and	w0, w0, 255
	ldrb	w1, [x2, 1028]
	ldr	x2, [x2, 1056]
	cmp	w1, 9
	lsl	w1, w0, 16
	lsl	w0, w0, 8
	orr	w1, w1, w0
	orr	w1, w1, 3
	bne	.L254
	str	w1, [x2, 80]
	ret
.L254:
	str	w1, [x2, 304]
	ret
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1028]
	ldr	x0, [x0, 1056]
	cmp	w1, 9
	bne	.L257
	ldr	w0, [x0, 80]
.L259:
	ubfx	x0, x0, 8, 8
	ret
.L257:
	ldr	w0, [x0, 304]
	b	.L259
	.size	nandc_get_ddr_para, .-nandc_get_ddr_para
	.align	2
	.global	nandc_set_if_mode
	.type	nandc_set_if_mode, %function
nandc_set_if_mode:
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	and	w0, w0, 255
	tst	w0, 6
	ldr	x2, [x3, 1056]
	ldr	w1, [x2]
	beq	.L261
	orr	w1, w1, 24576
	tst	x0, 4
	and	w1, w1, -32769
	ldrb	w0, [x3, 1028]
	orr	w1, w1, 196608
	mov	w3, 8321
	orr	w4, w1, 32768
	csel	w1, w4, w1, ne
	cmp	w0, 9
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	bne	.L263
	str	w3, [x2, 8]
	str	w0, [x2, 80]
	mov	w0, 38
	str	w0, [x2, 84]
	mov	w0, 39
	str	w0, [x2, 84]
.L264:
	str	w1, [x2]
	ret
.L263:
	str	w3, [x2, 344]
	str	w0, [x2, 304]
	mov	w0, 38
	str	w0, [x2, 308]
	mov	w0, 39
	str	w0, [x2, 308]
	b	.L264
.L261:
	and	w1, w1, -8193
	b	.L264
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	adrp	x1, .LANCHOR0+1056
	mov	w2, 1
	lsl	w0, w2, w0
	ldr	x3, [x1, #:lo12:.LANCHOR0+1056]
	ldr	w1, [x3]
	bfi	w1, w0, 0, 8
	str	w1, [x3]
	ret
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	flash_wait_device_ready_raw
	.type	flash_wait_device_ready_raw, %function
flash_wait_device_ready_raw:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x23, x24, [sp, 48]
	mov	w24, w2
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	x19, x2
	stp	x25, x26, [sp, 64]
	mov	w21, w1
	str	x27, [sp, 80]
	ldrb	w0, [x0, 1153]
	cmp	w0, w20
	bhi	.L269
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 224
	mov	w2, 812
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L269:
	add	x2, x19, :lo12:.LANCHOR0
	lsr	w23, w21, 8
	add	x20, x2, w20, sxtw
	lsr	w25, w21, 16
	lsr	w26, w21, 24
	and	w21, w21, 255
	ldr	x19, [x2, 1056]
	mov	w27, 120
	ldrb	w22, [x20, 1196]
	mov	x20, x2
	ubfiz	x0, x22, 8, 8
	add	x19, x19, x0
.L275:
	mov	w0, w22
	bl	nandc_cs
	str	w27, [x19, 2056]
	str	w21, [x19, 2052]
	ldrb	w0, [x20, 1204]
	str	w23, [x19, 2052]
	str	w25, [x19, 2052]
	cbz	w0, .L270
	str	w26, [x19, 2052]
.L270:
	mov	x0, 600
	bl	__const_udelay
	ldr	w2, [x19, 2048]
	and	w2, w2, 255
	bl	nandc_de_cs.constprop.35
	bics	wzr, w24, w2
	bne	.L275
	cmp	w2, 255
	beq	.L275
	mov	w0, w2
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_wait_device_ready_raw, .-flash_wait_device_ready_raw
	.align	2
	.global	flash_wait_device_ready
	.type	flash_wait_device_ready, %function
flash_wait_device_ready:
	adrp	x4, .LANCHOR0
	add	x7, x4, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	mov	w6, 24
	mov	w5, 1
	mov	w2, w1
	add	x29, sp, 0
	ldrb	w8, [x7, 1205]
	tst	x0, 50331648
	sub	w6, w6, w8
	lsl	w3, w5, w6
	sub	w3, w3, #1
	lsl	w5, w5, w8
	sub	w5, w5, #1
	asr	w6, w0, w6
	and	w5, w5, w6
	and	w1, w3, w0
	and	w5, w5, 255
	bne	.L280
	ldrb	w0, [x4, #:lo12:.LANCHOR0]
	cbz	w0, .L281
	ldrb	w0, [x7, 1]
	cbz	w0, .L280
.L281:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w3, [x4, 2]
	udiv	w0, w1, w3
	mul	w0, w0, w3
	sub	w3, w1, w0
	ldrb	w1, [x4, 1]
	cbz	w1, .L282
	add	w1, w0, w3, lsl 1
.L280:
	mov	w0, w5
	bl	flash_wait_device_ready_raw
	ldp	x29, x30, [sp], 16
	ret
.L282:
	add	x4, x4, 4
	ldrh	w3, [x4, w3, uxtw 1]
	add	w1, w3, w0
	b	.L280
	.size	flash_wait_device_ready, .-flash_wait_device_ready
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	adrp	x0, .LANCHOR0+1056
	ldr	x1, [x0, #:lo12:.LANCHOR0+1056]
	ldr	w0, [x1]
	and	w0, w0, -256
	and	w0, w0, -131073
	str	w0, [x1]
	ret
	.size	nandc_de_cs, .-nandc_de_cs
	.align	2
	.global	nandc_wait_flash_ready_no_delay
	.type	nandc_wait_flash_ready_no_delay, %function
nandc_wait_flash_ready_no_delay:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	movk	w19, 0x1, lsl 16
.L293:
	ldr	x0, [x20, 1056]
	ldr	w0, [x0]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L294
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L293
	mov	w0, -1
.L291:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L294:
	mov	w0, 0
	b	.L291
	.size	nandc_wait_flash_ready_no_delay, .-nandc_wait_flash_ready_no_delay
	.align	2
	.global	zftl_flash_enter_slc_mode
	.type	zftl_flash_enter_slc_mode, %function
zftl_flash_enter_slc_mode:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	x19, x0, 255
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1, #:lo12:.LANCHOR0]
	str	x21, [sp, 32]
	cbz	w1, .L297
	cmp	w1, 1
	ldr	x20, [x0, 1056]
	bne	.L300
	adrp	x0, .LANCHOR2+37
	ldrb	w0, [x0, #:lo12:.LANCHOR2+37]
	cbz	w0, .L297
.L316:
	add	x19, x19, 8
	add	x19, x20, x19, lsl 8
	str	w0, [x19, 8]
.L297:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L300:
	cmp	w1, 2
	bne	.L302
	add	x0, x0, x19
	ldrb	w1, [x0, 1208]
	cbz	w1, .L297
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	strb	wzr, [x0, 1208]
	add	x21, x21, 8
	ldrb	w0, [x21, 29]
	cbz	w0, .L297
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x21, 29]
	b	.L316
.L302:
	cmp	w1, 3
	bne	.L297
	add	x21, x0, w19, sxtw
	ldrb	w0, [x21, 1208]
	cbz	w0, .L297
	ubfiz	x19, x19, 8, 8
	bl	nandc_wait_flash_ready_no_delay
	add	x19, x20, x19
	mov	w0, 239
	strb	wzr, [x21, 1208]
	str	w0, [x19, 2056]
	mov	w0, 145
	str	w0, [x19, 2052]
	mov	x0, 500
	bl	__const_udelay
	str	wzr, [x19, 2048]
	mov	w0, 1
	str	w0, [x19, 2048]
	str	wzr, [x19, 2048]
	mov	x0, 750
	str	wzr, [x19, 2048]
	bl	__const_udelay
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 218
	str	w0, [x19, 2056]
	mov	x0, 250
	bl	__const_udelay
	b	.L297
	.size	zftl_flash_enter_slc_mode, .-zftl_flash_enter_slc_mode
	.align	2
	.global	zftl_flash_exit_slc_mode
	.type	zftl_flash_exit_slc_mode, %function
zftl_flash_exit_slc_mode:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	x19, x0, 255
	adrp	x0, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0, #:lo12:.LANCHOR0]
	cbz	w0, .L317
	cmp	w0, 1
	ldr	x20, [x21, 1056]
	bne	.L320
	adrp	x0, .LANCHOR2+38
	ldrb	w0, [x0, #:lo12:.LANCHOR2+38]
	cbz	w0, .L317
.L338:
	add	x19, x19, 8
	add	x19, x20, x19, lsl 8
	str	w0, [x19, 8]
.L317:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L320:
	cmp	w0, 2
	bne	.L322
	add	x21, x21, x19
	ldrb	w0, [x21, 1208]
	cbnz	w0, .L317
	adrp	x22, .LANCHOR2
	add	x22, x22, :lo12:.LANCHOR2
	mov	w1, 4
	add	x22, x22, 8
	ldrb	w0, [x22, 12]
	cmp	w0, 2
	csel	w0, w0, w1, eq
	strb	w0, [x21, 1208]
	ldrb	w0, [x22, 30]
	cbz	w0, .L317
	bl	nandc_wait_flash_ready_no_delay
	ldrb	w0, [x22, 30]
	b	.L338
.L322:
	cmp	w0, 3
	bne	.L317
	add	x21, x21, w19, sxtw
	ldrb	w0, [x21, 1208]
	cbnz	w0, .L317
	adrp	x22, .LANCHOR2
	add	x22, x22, :lo12:.LANCHOR2
	bl	nandc_wait_flash_ready_no_delay
	ubfiz	x19, x19, 8, 8
	add	x19, x20, x19
	mov	w1, 4
	ldrb	w0, [x22, 20]
	cmp	w0, 2
	csel	w0, w0, w1, eq
	strb	w0, [x21, 1208]
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 145
	str	w0, [x19, 2052]
	mov	x0, 500
	bl	__const_udelay
	ldrb	w0, [x22, 15]
	cmp	w0, 9
	mov	w0, 1
	bne	.L325
	str	w0, [x19, 2048]
.L326:
	str	w0, [x19, 2048]
	mov	x0, 750
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	bl	__const_udelay
	bl	nandc_wait_flash_ready_no_delay
	mov	w0, 223
	str	w0, [x19, 2056]
	mov	x0, 250
	bl	__const_udelay
	b	.L317
.L325:
	ldrb	w1, [x21, 1208]
	str	w1, [x19, 2048]
	b	.L326
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_start_page_read
	.type	flash_start_page_read, %function
flash_start_page_read:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w19, 24
	stp	x23, x24, [sp, 48]
	mov	w23, w1
	mov	w20, 1
	ldrb	w1, [x0, 1205]
	ldrb	w0, [x0, 1153]
	sub	w24, w19, w1
	lsl	w20, w20, w1
	sub	w20, w20, #1
	lsr	w1, w23, w24
	and	w20, w20, w1
	and	w20, w20, 255
	cmp	w0, w20
	bhi	.L340
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 256
	mov	w2, 1013
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L340:
	add	x5, x22, :lo12:.LANCHOR0
	ldrb	w0, [x5, 1153]
	cmp	w0, w20
	bls	.L339
	add	x20, x5, w20, sxtw
	mov	w19, 1
	ubfx	x4, x23, 24, 2
	lsl	w19, w19, w24
	sub	w19, w19, #1
	and	w19, w19, w23
	ldrb	w23, [x20, 1196]
	ldr	x20, [x5, 1056]
	mov	w0, w23
	bl	nandc_cs
	cbnz	w4, .L342
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
	ldrb	w0, [x22, #:lo12:.LANCHOR0]
	cbz	w0, .L343
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
.L343:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0, 1144]
	ldrb	w1, [x0, 7]
	cmp	w1, 1
	bne	.L345
	ldrb	w0, [x0, 12]
	cmp	w0, 2
	bne	.L345
	sxtw	x0, w23
	mov	w1, 38
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w1, [x0, 8]
.L345:
	ubfiz	x0, x23, 8, 8
	and	w1, w19, 255
	add	x0, x20, x0
	add	x22, x22, :lo12:.LANCHOR0
	str	wzr, [x0, 2056]
	str	wzr, [x0, 2052]
	str	wzr, [x0, 2052]
	str	w1, [x0, 2052]
	lsr	w1, w19, 8
	str	w1, [x0, 2052]
	lsr	w1, w19, 16
	str	w1, [x0, 2052]
	ldrb	w1, [x22, 1204]
	cbz	w1, .L346
	lsr	w19, w19, 24
	str	w19, [x0, 2052]
.L346:
	str	w21, [x0, 2056]
.L339:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L342:
	ldr	x0, [x5, 1144]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L344
	ldrb	w0, [x5, 1212]
	cbnz	w0, .L344
	ldrb	w0, [x5, 1213]
	cbnz	w0, .L344
	sxtw	x0, w23
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w4, [x0, 8]
	b	.L343
.L344:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	b	.L343
	.size	flash_start_page_read, .-flash_start_page_read
	.align	2
	.global	nandc_wait_flash_ready
	.type	nandc_wait_flash_ready, %function
nandc_wait_flash_ready:
	stp	x29, x30, [sp, -48]!
	mov	x0, 750
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, 34464
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	movk	w19, 0x1, lsl 16
	bl	__const_udelay
.L356:
	ldr	x0, [x20, 1056]
	ldr	w0, [x0]
	str	w0, [x29, 40]
	ldr	w0, [x29, 40]
	tbnz	x0, 9, .L357
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L356
	mov	w0, -1
.L354:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L357:
	mov	w0, 0
	b	.L354
	.size	nandc_wait_flash_ready, .-nandc_wait_flash_ready
	.align	2
	.global	sandisk_set_rr_para
	.type	sandisk_set_rr_para, %function
sandisk_set_rr_para:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	mov	w0, 239
	and	w19, w1, 255
	str	w0, [x20, 8]
	mov	w0, 17
	str	w0, [x20, 4]
	mov	x0, 1000
	bl	__const_udelay
	add	w1, w19, 1
	mov	w0, 5
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	umull	x1, w1, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x3, x0, 48
	add	x0, x0, 144
	add	x3, x3, x1
	add	x0, x0, x1
	mov	x1, 0
.L361:
	ldrb	w4, [x2, 1137]
	cmp	w4, w1
	bhi	.L364
	bl	nandc_wait_flash_ready
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L364:
	ldrb	w4, [x2, 1136]
	cmp	w4, 67
	bne	.L362
	ldrsb	w4, [x0, x1]
.L366:
	add	x1, x1, 1
	str	w4, [x20]
	b	.L361
.L362:
	ldrsb	w4, [x3, x1]
	b	.L366
	.size	sandisk_set_rr_para, .-sandisk_set_rr_para
	.align	2
	.global	toshiba_3d_set_tlc_rr_para
	.type	toshiba_3d_set_tlc_rr_para, %function
toshiba_3d_set_tlc_rr_para:
	stp	x29, x30, [sp, -48]!
	and	x2, x1, 255
	mov	x1, 7
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w21, 213
	mov	w0, -119
	madd	x1, x2, x1, x1
	str	w21, [x19, 8]
	str	wzr, [x19, 4]
	str	w0, [x19, 4]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 280
	add	x20, x0, x1
	ldrsb	w0, [x0, x1]
	str	w0, [x19]
	ldrsb	w0, [x20, 1]
	str	w0, [x19]
	ldrsb	w0, [x20, 2]
	str	w0, [x19]
	ldrsb	w0, [x20, 3]
	str	w0, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19, 8]
	str	wzr, [x19, 4]
	mov	w0, -118
	str	w0, [x19, 4]
	ldrsb	w0, [x20, 4]
	str	w0, [x19]
	ldrsb	w0, [x20, 5]
	str	w0, [x19]
	ldrsb	w0, [x20, 6]
	str	w0, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
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
	str	w2, [x0, 8]
	mov	w2, -117
	str	wzr, [x0, 4]
	str	w2, [x0, 4]
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	add	x1, x2, w1, uxtb
	ldrsb	w1, [x1, 681]
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
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w21, 239
	adrp	x0, .LANCHOR2
	str	w21, [x19, 8]
	cbz	w2, .L372
	mov	w2, 18
	str	w2, [x19, 4]
	mov	x2, 7
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 40
	mul	x1, x1, x2
	add	x20, x0, x1
	ldrb	w0, [x0, x1]
	str	w0, [x19]
	ldrb	w0, [x20, 1]
	str	w0, [x19]
	ldrb	w0, [x20, 2]
	str	w0, [x19]
	ldrb	w0, [x20, 3]
	str	w0, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19, 8]
	mov	w0, 19
	str	w0, [x19, 4]
	ldrb	w0, [x20, 4]
	str	w0, [x19]
	ldrb	w0, [x20, 5]
	str	w0, [x19]
	ldrb	w0, [x20, 6]
	str	w0, [x19]
.L375:
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L372:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 20
	add	x0, x0, 376
	str	w2, [x19, 4]
	ldrb	w0, [x0, x1]
	str	w0, [x19]
	str	wzr, [x19]
	str	wzr, [x19]
	b	.L375
	.size	toshiba_tlc_set_rr_para, .-toshiba_tlc_set_rr_para
	.align	2
	.global	ymtc_3d_set_tlc_rr_para
	.type	ymtc_3d_set_tlc_rr_para, %function
ymtc_3d_set_tlc_rr_para:
	stp	x29, x30, [sp, -48]!
	and	x1, x1, 255
	mov	x2, 7
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w0, 160
	mov	w21, 239
	mul	x1, x1, x2
	str	w21, [x19, 8]
	str	w0, [x19, 4]
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	add	x0, x0, 696
	add	x20, x0, x1
	ldrsb	w0, [x0, x1]
	str	w0, [x19]
	ldrsb	w0, [x20, 4]
	str	w0, [x19]
	str	wzr, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19, 8]
	mov	w0, 161
	str	w0, [x19, 4]
	ldrsb	w0, [x20, 1]
	str	w0, [x19]
	ldrsb	w0, [x20, 3]
	str	w0, [x19]
	ldrsb	w0, [x20, 5]
	str	w0, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	str	w21, [x19, 8]
	mov	w0, 162
	str	w0, [x19, 4]
	ldrsb	w0, [x20, 2]
	str	w0, [x19]
	ldrsb	w0, [x20, 6]
	str	w0, [x19]
	str	wzr, [x19]
	str	wzr, [x19]
	bl	nandc_wait_flash_ready
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ymtc_3d_set_tlc_rr_para, .-ymtc_3d_set_tlc_rr_para
	.align	2
	.global	ymtc_3d_set_slc_rr_para
	.type	ymtc_3d_set_slc_rr_para, %function
ymtc_3d_set_slc_rr_para:
	stp	x29, x30, [sp, -16]!
	mov	w2, 239
	and	x1, x1, 255
	add	x29, sp, 0
	str	w2, [x0, 8]
	mov	w2, 163
	str	w2, [x0, 4]
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	add	x2, x2, 1056
	ldrsb	w1, [x2, x1]
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
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x4, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ubfx	x24, x0, 24, 2
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrb	w19, [x4, 1205]
	sub	w3, w3, w19
	lsl	w19, w2, w19
	sub	w19, w19, #1
	lsl	w21, w2, w3
	sub	w21, w21, #1
	and	w22, w21, w0
	lsr	w3, w0, w3
	ldrb	w0, [x4, 1153]
	and	w19, w19, w3
	and	w19, w19, 255
	and	w21, w21, w1
	cmp	w0, w19
	bhi	.L381
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1072
	mov	w2, 1148
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L381:
	add	x4, x20, :lo12:.LANCHOR0
	add	x19, x4, w19, sxtw
	ldr	x25, [x4, 1056]
	ldrb	w23, [x19, 1196]
	mov	w0, w23
	bl	nandc_cs
	cbnz	w24, .L382
	mov	w0, w22
	bl	slc_phy_page_address_calc
	mov	w22, w0
	mov	w0, w21
	bl	slc_phy_page_address_calc
	mov	w21, w0
	ldrb	w0, [x20, #:lo12:.LANCHOR0]
	cbz	w0, .L383
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
.L383:
	add	x0, x20, :lo12:.LANCHOR0
	and	x19, x23, 255
	and	w3, w22, 255
	lsr	w2, w22, 8
	and	w28, w21, 255
	lsr	w27, w21, 8
	ldrb	w1, [x0, 1176]
	lsr	w26, w21, 16
	cmp	w1, 1
	lsr	w1, w22, 16
	bne	.L385
	add	x19, x25, x19, lsl 8
	ldrb	w4, [x0, 1168]
	ldrb	w0, [x0, 1204]
	str	w4, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w3, [x19, 2052]
	str	w2, [x19, 2052]
	str	w1, [x19, 2052]
	cbz	w0, .L386
	lsr	w22, w22, 24
	str	w22, [x19, 2052]
.L386:
	add	x22, x20, :lo12:.LANCHOR0
	ldrb	w0, [x22, 1169]
	str	w0, [x19, 2056]
	bl	nandc_wait_flash_ready
	ldr	x0, [x22, 1144]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L387
	cbz	w24, .L387
	str	w24, [x19, 2056]
.L387:
	str	wzr, [x19, 2056]
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w28, [x19, 2052]
	ldrb	w0, [x20, 1204]
	str	w27, [x19, 2052]
	str	w26, [x19, 2052]
	cbz	w0, .L408
	lsr	w21, w21, 24
	str	w21, [x19, 2052]
.L408:
	mov	w0, 48
	str	w0, [x19, 2056]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L382:
	ldr	x0, [x4, 1144]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L384
	ldrb	w0, [x4, 1212]
	cbnz	w0, .L384
	ldrb	w0, [x4, 1213]
	cbnz	w0, .L384
	sxtw	x0, w23
	add	x0, x0, 8
	add	x0, x25, x0, lsl 8
	str	w24, [x0, 8]
	b	.L383
.L384:
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	b	.L383
.L385:
	ldr	x0, [x0, 1144]
	ldrb	w4, [x0, 7]
	cmp	w4, 1
	bne	.L390
	ldrb	w4, [x0, 12]
	cmp	w4, 2
	bne	.L390
	add	x23, x23, 8
	mov	w4, 38
	add	x23, x25, x23, lsl 8
	str	w4, [x23, 8]
.L390:
	add	x4, x20, :lo12:.LANCHOR0
	add	x19, x25, x19, lsl 8
	ldrb	w4, [x4, 1168]
	str	w4, [x19, 2056]
	str	w3, [x19, 2052]
	str	w2, [x19, 2052]
	str	w1, [x19, 2052]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L391
	cbz	w24, .L391
	str	w24, [x19, 2056]
.L391:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20, 1169]
	str	w0, [x19, 2056]
	str	w28, [x19, 2052]
	str	w27, [x19, 2052]
	str	w26, [x19, 2052]
	b	.L408
	.size	flash_start_plane_read, .-flash_start_plane_read
	.align	2
	.global	flash_set_interface_mode
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	str	x25, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x25, x25, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x19, x20, [sp, 16]
	mov	w21, w0
	add	x22, x25, 1216
	mov	x20, x25
	stp	x23, x24, [sp, 48]
	mov	x24, 0
.L422:
	lsl	x0, x24, 3
	ldr	x19, [x25, 1056]
	ldrb	w23, [x0, x22]
	cmp	w23, 69
	beq	.L410
	add	w0, w23, 119
	cmp	w23, 44
	and	w1, w0, 255
	cset	w2, eq
	cmp	w1, 18
	bhi	.L423
	mov	x0, 32769
	movk	x0, 0x4, lsl 16
	lsr	x0, x0, x1
	mvn	x0, x0
.L411:
	and	w0, w0, 1
	eor	w0, w0, 1
	orr	w0, w2, w0
	cbz	w0, .L412
.L410:
	ldrb	w1, [x20, 1248]
	cmp	w21, 1
	bne	.L413
	tbz	x1, 0, .L412
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L414
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	bl	printk
.L414:
	add	x0, x19, x24, lsl 8
	mov	w1, 239
	cmp	w23, 44
	str	w1, [x0, 2056]
	mov	w1, 137
	ccmp	w23, w1, 4, ne
	mov	w1, 1
	bne	.L415
.L446:
	str	w1, [x0, 2052]
	mov	w1, 5
.L445:
	str	w1, [x0, 2048]
.L420:
	str	wzr, [x0, 2048]
	str	wzr, [x0, 2048]
	str	wzr, [x0, 2048]
.L412:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L422
	bl	nandc_wait_flash_ready
	ldr	x25, [sp, 64]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L423:
	mov	x0, 1
	b	.L411
.L415:
	cmp	w23, 155
	beq	.L446
	mov	w2, 128
	str	w2, [x0, 2052]
	b	.L445
.L413:
	tbz	x1, 2, .L412
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L418
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L418:
	add	x0, x19, x24, lsl 8
	mov	w1, 239
	cmp	w23, 44
	str	w1, [x0, 2056]
	mov	w1, 137
	ccmp	w23, w1, 4, ne
	bne	.L419
	mov	w1, 1
	str	w1, [x0, 2052]
	mov	w1, 35
	b	.L445
.L419:
	cmp	w23, 155
	bne	.L421
	mov	w1, 1
	str	w1, [x0, 2052]
	mov	w1, 37
	b	.L445
.L421:
	mov	w1, 128
	str	w1, [x0, 2052]
	str	wzr, [x0, 2048]
	b	.L420
	.size	flash_set_interface_mode, .-flash_set_interface_mode
	.align	2
	.type	mt_auto_read_calibration_config, %function
mt_auto_read_calibration_config:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	and	w19, w0, 255
	bl	nandc_wait_flash_ready
	adrp	x1, .LANCHOR0+1056
	ubfiz	x0, x19, 8, 8
	ldr	x19, [x1, #:lo12:.LANCHOR0+1056]
	add	x19, x19, x0
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 150
	str	w0, [x19, 2052]
	mov	x0, 1000
	bl	__const_udelay
	str	w20, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	mt_auto_read_calibration_config, .-mt_auto_read_calibration_config
	.align	2
	.global	flash_reset
	.type	flash_reset, %function
flash_reset:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+1056
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	ldr	x1, [x1, #:lo12:.LANCHOR0+1056]
	add	x0, x0, 2048
	add	x1, x1, x0
	mov	w0, 255
	str	w0, [x1, 8]
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
	str	x21, [sp, 32]
	and	w21, w0, 255
	adrp	x0, .LANCHOR0+1056
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	ldr	x20, [x0, #:lo12:.LANCHOR0+1056]
	mov	w0, w21
	bl	flash_reset
	mov	w0, w21
	bl	nandc_cs
	ubfiz	x0, x21, 8, 8
	add	x20, x20, x0
	mov	w0, 144
	str	w0, [x20, 2056]
	mov	x0, 1000
	str	wzr, [x20, 2052]
	bl	__const_udelay
	ldr	w0, [x20, 2048]
	strb	w0, [x19]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 1]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 2]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 3]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 4]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 5]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 6]
	ldr	w0, [x20, 2048]
	strb	w0, [x19, 7]
	bl	nandc_de_cs.constprop.35
	ldrb	w2, [x19]
	sub	w0, w2, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L451
	ldrb	w7, [x19, 5]
	adrp	x0, .LC36
	ldrb	w6, [x19, 4]
	add	w1, w21, 1
	ldrb	w5, [x19, 3]
	add	x0, x0, :lo12:.LC36
	ldrb	w4, [x19, 2]
	ldrb	w3, [x19, 1]
	bl	printk
.L451:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_read_id, .-flash_read_id
	.align	2
	.global	flash_read_spare
	.type	flash_read_spare, %function
flash_read_spare:
	stp	x29, x30, [sp, -32]!
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	adrp	x2, .LANCHOR2+17
	ldrb	w3, [x2, #:lo12:.LANCHOR2+17]
	adrp	x2, .LANCHOR0+1056
	ldr	x19, [x2, #:lo12:.LANCHOR0+1056]
	lsl	w3, w3, 9
	add	x19, x19, x0
	and	w0, w1, 255
	str	wzr, [x19, 2056]
	str	w3, [x19, 2052]
	lsr	w3, w3, 8
	str	w3, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w1, 8
	str	w0, [x19, 2052]
	lsr	w1, w1, 16
	str	w1, [x19, 2052]
	mov	w0, 48
	str	w0, [x19, 2056]
	bl	nandc_wait_flash_ready
	ldr	w0, [x19, 2048]
	strb	w0, [x20]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_read_spare, .-flash_read_spare
	.align	2
	.global	flash_read_otp_data
	.type	flash_read_otp_data, %function
flash_read_otp_data:
	stp	x29, x30, [sp, -48]!
	and	w4, w0, 255
	adrp	x0, .LANCHOR0+1056
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	ldr	x19, [x0, #:lo12:.LANCHOR0+1056]
	mov	w0, w4
	stp	x21, x22, [sp, 32]
	mov	x22, x2
	bl	nandc_cs
	ubfiz	x4, x4, 8, 8
	add	x19, x19, x4
	mov	w0, 239
	mov	w21, 144
	str	w0, [x19, 2056]
	mov	x0, 250
	str	w21, [x19, 2052]
	bl	__const_udelay
	mov	w0, 1
	str	w0, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	bl	nandc_wait_flash_ready
	mov	w0, 238
	str	w0, [x19, 2056]
	str	w21, [x19, 2052]
	mov	w1, w20
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	ldr	w2, [x19, 2048]
	ldr	w3, [x19, 2048]
	ldr	w4, [x19, 2048]
	bl	printk
	bl	nandc_wait_flash_ready
	str	wzr, [x19, 2056]
	str	wzr, [x19, 2052]
	and	w0, w20, 255
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w20, 8
	str	w0, [x19, 2052]
	lsr	w20, w20, 16
	str	w20, [x19, 2052]
	mov	w0, 48
	str	w0, [x19, 2056]
	bl	nandc_wait_flash_ready
	mov	x0, 0
.L457:
	ldr	w1, [x19, 2048]
	strb	w1, [x22, x0]
	add	x0, x0, 1
	cmp	x0, 16384
	bne	.L457
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 144
	str	w0, [x19, 2052]
	mov	x0, 250
	bl	__const_udelay
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	bl	nandc_de_cs.constprop.35
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_read_otp_data, .-flash_read_otp_data
	.align	2
	.global	sandisk_prog_test_bad_block
	.type	sandisk_prog_test_bad_block, %function
sandisk_prog_test_bad_block:
	stp	x29, x30, [sp, -48]!
	and	x2, x0, 255
	mov	x0, x2
	add	x2, x2, 8
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	adrp	x1, .LANCHOR0+1056
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x1, #:lo12:.LANCHOR0+1056]
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w1, [x1, 37]
	add	x2, x19, x2, lsl 8
	cbz	w1, .L461
.L476:
	ubfiz	x0, x0, 8, 8
	str	w1, [x2, 8]
	add	x19, x19, x0
	mov	w0, 128
	str	w0, [x19, 2056]
	and	w0, w22, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w22, 8
	str	w0, [x19, 2052]
	lsr	w0, w22, 16
	str	w0, [x19, 2052]
	mov	w0, 16
	str	w0, [x19, 2056]
	bl	nandc_wait_flash_ready
	mov	w0, 112
	str	w0, [x19, 2056]
	mov	x0, 1000
	bl	__const_udelay
	ldr	w2, [x19, 2048]
	cmp	w2, 255
	bne	.L463
	ldr	w2, [x19, 2048]
.L463:
	mov	w0, 5
	ands	w21, w2, w0
	beq	.L464
	ldr	w0, [x20, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L464
	adrp	x0, .LC38
	mov	w1, w22
	add	x0, x0, :lo12:.LC38
	bl	printk
.L464:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20, 38]
	cbz	w0, .L460
	str	w0, [x19, 2056]
.L460:
	mov	w0, w21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L461:
	mov	w1, 162
	b	.L476
	.size	sandisk_prog_test_bad_block, .-sandisk_prog_test_bad_block
	.align	2
	.global	nandc_rdy_status
	.type	nandc_rdy_status, %function
nandc_rdy_status:
	adrp	x0, .LANCHOR0+1056
	sub	sp, sp, #16
	ldr	x0, [x0, #:lo12:.LANCHOR0+1056]
	ldr	w0, [x0]
	str	w0, [sp, 8]
	ldr	w0, [sp, 8]
	add	sp, sp, 16
	ubfx	x0, x0, 9, 1
	ret
	.size	nandc_rdy_status, .-nandc_rdy_status
	.align	2
	.global	nandc_bch_sel
	.type	nandc_bch_sel, %function
nandc_bch_sel:
	sub	sp, sp, #16
	str	wzr, [sp, 8]
	ldr	w1, [sp, 8]
	orr	w1, w1, 1
	str	w1, [sp, 8]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1, 1028]
	strb	w0, [x1, 1249]
	cmp	w2, 9
	bne	.L480
	ldr	x1, [x1, 1056]
	cmp	w0, 70
	ldr	w2, [sp, 8]
	str	w2, [x1, 16]
	beq	.L486
	cmp	w0, 60
	beq	.L487
	cmp	w0, 40
	cset	w0, eq
	add	w0, w0, 1
.L481:
	str	wzr, [sp]
	ldr	w2, [sp]
	bfi	w2, w0, 25, 3
	str	w2, [sp]
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x1, 32]
.L479:
	add	sp, sp, 16
	ret
.L486:
	mov	w0, 0
	b	.L481
.L487:
	mov	w0, 3
	b	.L481
.L480:
	ldr	x2, [x1, 1056]
	mov	w3, 16
	ldr	w1, [sp, 8]
	cmp	w0, w3
	str	w1, [x2, 8]
	str	wzr, [sp]
	ldr	w1, [sp]
	bfi	w1, w3, 8, 8
	str	w1, [sp]
	ldr	w1, [sp]
	and	w1, w1, -262145
	str	w1, [sp]
	bne	.L483
.L490:
	ldr	w0, [sp]
	and	w0, w0, -17
	b	.L491
.L483:
	cmp	w0, 24
	bne	.L485
	ldr	w0, [sp]
	orr	w0, w0, 16
.L491:
	str	w0, [sp]
	b	.L484
.L485:
	ldr	w1, [sp]
	cmp	w0, 40
	orr	w1, w1, 262144
	str	w1, [sp]
	ldr	w1, [sp]
	orr	w1, w1, 16
	str	w1, [sp]
	beq	.L490
.L484:
	ldr	w0, [sp]
	orr	w0, w0, 1
	str	w0, [sp]
	ldr	w0, [sp]
	str	w0, [x2, 12]
	b	.L479
	.size	nandc_bch_sel, .-nandc_bch_sel
	.align	2
	.global	zftl_flash_resume
	.type	zftl_flash_resume, %function
zftl_flash_resume:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	ldrb	w1, [x0, 1028]
	ldr	w2, [x0, 1064]
	cmp	w1, 9
	ldr	x1, [x0, 1056]
	str	w2, [x1]
	ldr	x1, [x0, 1056]
	ldr	w2, [x0, 1068]
	str	w2, [x1, 4]
	ldr	w2, [x0, 1072]
	ldr	x1, [x0, 1056]
	bne	.L493
	str	w2, [x1, 16]
	ldr	w2, [x0, 1076]
	str	w2, [x1, 32]
	ldr	w2, [x0, 1080]
	str	w2, [x1, 80]
	ldr	w2, [x0, 1084]
	str	w2, [x1, 84]
	ldr	w2, [x0, 1088]
	str	w2, [x1, 520]
	ldr	w0, [x0, 1092]
	str	w0, [x1, 8]
.L495:
	add	x0, x20, :lo12:.LANCHOR0
	mov	x19, 0
	add	x23, x0, 1216
	add	x21, x0, 1208
	mov	w24, 2
.L494:
	lsl	x0, x19, 3
	ldrb	w0, [x0, x23]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bhi	.L496
	and	w22, w19, 255
	mov	w0, w22
	bl	flash_reset
	strb	w24, [x19, x21]
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L496:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L494
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20, 1192]
	cbz	w0, .L497
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20, 1080]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L497:
	adrp	x0, .LANCHOR2+28
	ldrb	w0, [x0, #:lo12:.LANCHOR2+28]
	bl	nandc_bch_sel
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L493:
	str	w2, [x1, 8]
	ldr	w2, [x0, 1076]
	str	w2, [x1, 12]
	ldr	w2, [x0, 1080]
	str	w2, [x1, 304]
	ldr	w2, [x0, 1084]
	str	w2, [x1, 308]
	ldr	w2, [x0, 1088]
	str	w2, [x1, 336]
	ldr	w0, [x0, 1092]
	str	w0, [x1, 344]
	b	.L495
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
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 1056]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	w1, [x19, 1028]
	ldr	x0, [x19, 1056]
	cmp	w1, 9
	bne	.L506
	ldr	w1, [x0, 292]
	orr	w1, w1, 2
	str	w1, [x0, 292]
	ldr	w1, [x0, 288]
	orr	w1, w1, 2
	str	w1, [x0, 288]
.L507:
	ldr	w1, [x0]
	tbnz	x1, 9, .L508
	bl	wait_for_nand_flash_ready
.L505:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L506:
	ldr	w1, [x0, 368]
	orr	w1, w1, 2
	str	w1, [x0, 368]
	ldr	w1, [x0, 364]
	orr	w1, w1, 2
	str	w1, [x0, 364]
	b	.L507
.L508:
	mov	w1, 1
	bl	nandc_irq_disable
	b	.L505
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w22, w2
	stp	x25, x26, [sp, 64]
	mov	w21, w3
	stp	x23, x24, [sp, 48]
	and	w25, w1, 255
	ldrb	w0, [x0, 1153]
	cmp	w0, w19
	bhi	.L512
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1096
	mov	w2, 695
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L512:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1153]
	cmp	w1, w19
	bls	.L521
	add	x19, x0, w19, sxtw
	ldr	x26, [x0, 1056]
	adrp	x0, .LANCHOR2
	ldrb	w23, [x19, 1196]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	and	x19, x23, 255
	add	x24, x19, 8
	add	x24, x26, x24, lsl 8
	tbz	x0, 4, .L514
	adrp	x0, .LC39
	mov	w3, w21
	mov	w2, w22
	mov	w1, w23
	add	x0, x0, :lo12:.LC39
	bl	printk
.L514:
	bl	nandc_wait_flash_ready
	mov	w0, w23
	bl	nandc_cs
	mov	w0, w23
	cbnz	w25, .L515
	bl	zftl_flash_enter_slc_mode
.L516:
	add	x19, x26, x19, lsl 8
	mov	w0, 96
	str	w0, [x19, 2056]
	and	w0, w22, 255
	str	w0, [x19, 2052]
	lsr	w0, w22, 8
	str	w0, [x19, 2052]
	lsr	w0, w22, 16
	str	w0, [x19, 2052]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1204]
	cbz	w0, .L517
	lsr	w0, w22, 24
	str	w0, [x19, 2052]
.L517:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1250]
	cbnz	w0, .L522
	mov	w0, 208
	str	w0, [x19, 2056]
	mov	w23, 5
	bl	nandc_wait_flash_ready
	mov	x0, x24
	bl	flash_read_status
	and	w23, w0, w23
.L518:
	mov	w0, 96
	add	x20, x20, :lo12:.LANCHOR0
	str	w0, [x19, 2056]
	and	w0, w21, 255
	str	w0, [x19, 2052]
	lsr	w0, w21, 8
	str	w0, [x19, 2052]
	lsr	w0, w21, 16
	str	w0, [x19, 2052]
	ldrb	w0, [x20, 1204]
	cbz	w0, .L519
	lsr	w3, w21, 24
	str	w3, [x19, 2052]
.L519:
	mov	w0, 208
	str	w0, [x19, 2056]
	mov	w19, 5
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x24
	bl	flash_read_status
	mov	w2, w0
	bl	nandc_de_cs.constprop.35
	and	w19, w2, w19
	orr	w19, w19, w23
	cbz	w19, .L520
	adrp	x0, .LC40
	mov	w1, w22
	add	x0, x0, :lo12:.LC40
	bl	printk
.L520:
	mov	w0, w19
.L511:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L515:
	bl	zftl_flash_exit_slc_mode
	b	.L516
.L522:
	mov	w23, 0
	b	.L518
.L521:
	mov	w0, -1
	b	.L511
	.size	flash_erase_duplane_block, .-flash_erase_duplane_block
	.align	2
	.global	flash_erase_block_en
	.type	flash_erase_block_en, %function
flash_erase_block_en:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	w23, w2
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	w19, 24
	stp	x25, x26, [sp, 64]
	and	w25, w1, 255
	str	x27, [sp, 80]
	mov	x24, x0
	ldrb	w1, [x2, 1205]
	sub	w1, w19, w1
	mov	w19, 1
	lsl	w19, w19, w1
	ldrb	w1, [x2, 1153]
	sub	w19, w19, #1
	and	w19, w19, w23
	cmp	w1, w22
	bhi	.L537
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1128
	mov	w2, 757
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L537:
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1153]
	cmp	w1, w22
	bls	.L544
	add	x1, x0, w22, sxtw
	ldr	x27, [x0, 1056]
	adrp	x0, .LANCHOR2
	ldrb	w21, [x1, 1196]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	and	x20, x21, 255
	add	x26, x20, 8
	add	x26, x27, x26, lsl 8
	tbz	x0, 4, .L539
	adrp	x0, .LC41
	mov	w3, w25
	mov	w2, w23
	mov	w1, w21
	add	x0, x0, :lo12:.LC41
	bl	printk
.L539:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	mov	w0, w21
	cbnz	w25, .L540
	bl	zftl_flash_enter_slc_mode
.L541:
	add	x0, x27, x20, lsl 8
	mov	w1, 96
	str	w1, [x0, 2056]
	and	w1, w19, 255
	str	w1, [x0, 2052]
	lsr	w1, w19, 8
	str	w1, [x0, 2052]
	lsr	w1, w19, 16
	str	w1, [x0, 2052]
	add	x1, x24, :lo12:.LANCHOR0
	ldrb	w1, [x1, 1204]
	cbz	w1, .L542
	lsr	w19, w19, 24
	str	w19, [x0, 2052]
.L542:
	mov	w1, 208
	str	w1, [x0, 2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x26
	bl	flash_read_status
	mov	w3, w0
	bl	nandc_de_cs.constprop.35
	mov	w0, 5
	ands	w3, w3, w0
	beq	.L543
	add	x0, x24, :lo12:.LANCHOR0
	mov	w1, w22
	ldrh	w2, [x0, 2]
	adrp	x0, .LC42
	add	x0, x0, :lo12:.LC42
	udiv	w2, w23, w2
	bl	printk
	mov	w3, -1
.L543:
	mov	w0, w3
.L536:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L540:
	bl	zftl_flash_exit_slc_mode
	b	.L541
.L544:
	mov	w0, -1
	b	.L536
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
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x23, x20, 1024
	str	x25, [sp, 64]
	mov	x24, x20
	mov	w19, 0
	ldrb	w1, [x0, 21]
	ldrh	w21, [x0, 22]
	mul	w21, w21, w1
	and	w21, w21, 65535
.L558:
	ldrb	w0, [x20, 1153]
	cmp	w0, w19
	bhi	.L561
	mov	w1, 0
	adrp	x0, .LC43
	add	x0, x0, :lo12:.LC43
	bl	printk
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L561:
	add	x0, x23, w19, sxtw
	mov	w22, 0
	ldrb	w25, [x0, 172]
.L559:
	cmp	w21, w22, uxth
	bhi	.L560
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L558
.L560:
	ldrh	w1, [x24, 2]
	mov	w0, w25
	mul	w1, w1, w22
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L559
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
	and	w1, w1, 255
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	str	x21, [sp, 32]
	and	w21, w0, 255
	mov	x0, x2
	add	x2, x20, :lo12:.LANCHOR0
	and	w6, w21, 1
	ldrb	w4, [x2, 1028]
	cmp	w4, 9
	add	w4, w1, 1
	bne	.L566
	str	wzr, [x29, 56]
	mov	w5, 1
	ubfx	x4, x4, 1, 6
	ldr	w1, [x29, 56]
	bfi	w1, w6, 1, 1
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	orr	w1, w1, 8
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	bfi	w1, w5, 5, 2
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	orr	w1, w1, 536870912
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	orr	w1, w1, 1024
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	and	w1, w1, -17
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	bfi	w1, w4, 22, 6
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	orr	w1, w1, 128
	str	w1, [x29, 56]
	ldrb	w1, [x2, 1251]
	cbz	w1, .L567
	ldrb	w1, [x2, 1252]
	cbz	w1, .L567
	ldr	w1, [x29, 56]
	orr	w1, w1, 512
	str	w1, [x29, 56]
.L567:
	ldr	w1, [x29, 56]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ubfx	x1, x1, 22, 6
	str	x3, [x20, 1272]
	lsl	w1, w1, 10
	str	x0, [x20, 1264]
	bl	rknand_dma_map_single
	str	w0, [x20, 1280]
	ldr	w1, [x29, 56]
	mov	w2, w21
	ldr	x0, [x20, 1272]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 2
	bl	rknand_dma_map_single
	str	w0, [x20, 1284]
	mov	w1, 1
	str	w1, [x20, 1288]
	ldr	x1, [x20, 1056]
	cmp	w21, 0
	ldr	w2, [x20, 1280]
	str	w2, [x1, 52]
	mov	w2, 16
	str	w0, [x1, 56]
	ldr	w0, [x1, 48]
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	bfi	w0, w2, 9, 5
	str	w0, [x29, 64]
	mov	w2, 2
	ldr	w0, [x29, 64]
	orr	w0, w0, 448
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	bfi	w0, w2, 3, 3
	str	w0, [x29, 64]
	cset	w2, eq
	ldr	w0, [x29, 64]
	orr	w0, w0, 4
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	bfi	w0, w2, 1, 1
	str	w0, [x29, 64]
	ldrh	w2, [x20, 1296]
	ldr	w0, [x29, 64]
	orr	w0, w0, 1
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	bfi	w0, w2, 16, 11
	str	w0, [x29, 64]
	ldr	w0, [x29, 64]
	str	w0, [x1, 48]
	ldr	w0, [x29, 56]
	str	w0, [x1, 16]
	ldr	w0, [x29, 56]
	orr	w0, w0, 4
	str	w0, [x29, 56]
	ldr	w0, [x29, 56]
	str	w0, [x1, 16]
.L565:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 80
	ret
.L566:
	ldr	x5, [x2, 1056]
	mov	w7, 16
	ubfx	x4, x4, 1, 6
	ldr	w5, [x5, 12]
	str	w5, [x29, 72]
	ldr	w5, [x29, 72]
	bfi	w5, w7, 8, 8
	str	w5, [x29, 72]
	ldr	w5, [x29, 72]
	and	w5, w5, -9
	str	w5, [x29, 72]
	ldr	w5, [x29, 72]
	and	w5, w5, -225
	str	w5, [x29, 72]
	str	wzr, [x29, 56]
	ldr	w5, [x29, 56]
	bfi	w5, w6, 1, 1
	str	w5, [x29, 56]
	mov	w6, 1
	ldr	w5, [x29, 56]
	orr	w5, w5, 8
	str	w5, [x29, 56]
	ldr	w5, [x29, 56]
	bfi	w5, w6, 5, 2
	str	w5, [x29, 56]
	ldr	w5, [x29, 56]
	orr	w5, w5, 536870912
	str	w5, [x29, 56]
	ldr	w5, [x29, 56]
	orr	w5, w5, 1024
	str	w5, [x29, 56]
	ldr	w5, [x29, 56]
	and	w5, w5, -17
	str	w5, [x29, 56]
	ldr	w5, [x29, 56]
	bfi	w5, w4, 22, 6
	str	w5, [x29, 56]
	cbz	w21, .L569
	ldrb	w4, [x2, 1249]
	lsr	w1, w1, 1
	mov	w6, 64
	mov	x5, 0
	cmp	w4, 25
	mov	w4, 128
	csel	w6, w6, w4, cc
	mov	w4, 0
.L571:
	add	w7, w6, w4
	cmp	w1, w5
	bgt	.L572
.L573:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w2, w21
	ldr	x1, [x20, 1256]
	str	x1, [x20, 1272]
	ldr	w1, [x29, 56]
	str	x0, [x20, 1264]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_map_single
	ldr	w1, [x29, 56]
	mov	w2, w21
	str	w0, [x20, 1280]
	ldr	x0, [x20, 1272]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 7
	bl	rknand_dma_map_single
	str	w0, [x20, 1284]
	mov	w0, 1
	str	w0, [x20, 1288]
	ldr	x0, [x20, 1056]
	mov	w2, 16
	ldr	w1, [x20, 1280]
	cmp	w21, 0
	str	w1, [x0, 20]
	ldr	w1, [x20, 1284]
	str	w1, [x0, 24]
	str	wzr, [x29, 64]
	ldr	w1, [x29, 64]
	bfi	w1, w2, 9, 5
	str	w1, [x29, 64]
	mov	w2, 2
	ldr	w1, [x29, 64]
	orr	w1, w1, 448
	str	w1, [x29, 64]
	ldr	w1, [x29, 64]
	bfi	w1, w2, 3, 3
	str	w1, [x29, 64]
	cset	w2, eq
	ldr	w1, [x29, 64]
	orr	w1, w1, 4
	str	w1, [x29, 64]
	ldr	w1, [x29, 64]
	bfi	w1, w2, 1, 1
	str	w1, [x29, 64]
	ldr	w1, [x29, 64]
	orr	w1, w1, 1
	str	w1, [x29, 64]
	ldr	w1, [x29, 64]
	str	w1, [x0, 16]
	ldr	w1, [x29, 72]
	str	w1, [x0, 12]
	ldr	w1, [x29, 56]
	str	w1, [x0, 8]
	ldr	w1, [x29, 56]
	orr	w1, w1, 4
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	str	w1, [x0, 8]
	b	.L565
.L572:
	ldr	x8, [x2, 1256]
	and	x4, x4, 4294967292
	ldr	w9, [x3, x5, lsl 2]
	add	x5, x5, 1
	str	w9, [x8, x4]
	mov	w4, w7
	b	.L571
.L569:
	ldr	x1, [x2, 1256]
	str	w6, [x1]
	b	.L573
	.size	nandc_xfer_start, .-nandc_xfer_start
	.align	2
	.global	nandc_set_seed
	.type	nandc_set_seed, %function
nandc_set_seed:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	and	x0, x0, 127
	add	x1, x1, 1152
	ldrh	w1, [x1, x0, lsl 1]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	orr	w2, w1, -1073741824
	ldrb	w3, [x0, 1252]
	cmp	w3, 0
	csel	w1, w2, w1, ne
	ldrb	w2, [x0, 1028]
	ldr	x0, [x0, 1056]
	cmp	w2, 9
	bne	.L584
	str	w1, [x0, 520]
	ret
.L584:
	str	w1, [x0, 336]
	ret
	.size	nandc_set_seed, .-nandc_set_seed
	.align	2
	.global	zftl_flash_de_init
	.type	zftl_flash_de_init, %function
zftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	bl	nandc_wait_flash_ready
	adrp	x4, .LANCHOR0
	mov	x19, x4
	ldrb	w1, [x4, #:lo12:.LANCHOR0]
	cbz	w1, .L589
	add	x0, x4, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1154]
	cbnz	w1, .L589
	ldrb	w0, [x0, 1028]
	cmp	w0, 9
	beq	.L589
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
.L589:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w0, 0
	bl	hynix_reconfig_rr_para
	ldrb	w0, [x20, 1192]
	cbz	w0, .L590
	ldrb	w0, [x20, 1248]
	tbz	x0, 0, .L590
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20, 1192]
.L590:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19, 1252]
	cbz	w0, .L591
	mov	w0, 0
	strb	wzr, [x19, 1252]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19, 1252]
.L591:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+1252
	strb	w0, [x1, #:lo12:.LANCHOR0+1252]
	ret
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	adrp	x1, .LANCHOR0+1056
	ubfiz	x0, x0, 8, 8
	add	x0, x0, 2048
	ldr	x1, [x1, #:lo12:.LANCHOR0+1056]
	add	x0, x1, x0
	ret
	.size	nandc_get_chip_if, .-nandc_get_chip_if
	.align	2
	.global	buf_reinit
	.type	buf_reinit, %function
buf_reinit:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x1, x1, 1304
	mov	w2, 0
.L611:
	and	w3, w2, 255
	strb	wzr, [x1, 2]
	add	w4, w3, 1
	strb	w3, [x1, 1]
	strb	w4, [x1]
	add	w2, w2, 1
	str	xzr, [x1, 16]
	cmp	w2, 32
	add	x1, x1, 64
	bne	.L611
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0, 3352]
	strb	w1, [x0, 3288]
	strb	w2, [x0, 3353]
	ret
	.size	buf_reinit, .-buf_reinit
	.align	2
	.global	buf_add_tail
	.type	buf_add_tail, %function
buf_add_tail:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	str	x21, [sp, 32]
	mov	w0, -1
	strb	w0, [x1]
	mov	x20, x1
	ldrb	w1, [x19]
	cmp	w1, 255
	bne	.L619
	ldrb	w0, [x20, 1]
	cmp	w0, 255
	bne	.L615
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1408
	mov	w2, 74
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L615:
	ldrb	w0, [x20, 1]
	strb	w0, [x19]
.L613:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L619:
	adrp	x0, .LANCHOR0
	add	x2, x0, :lo12:.LANCHOR0
	add	x2, x2, 1304
	mov	x19, x0
.L620:
	sbfiz	x0, x1, 6, 32
	mov	w21, w1
	ldrb	w1, [x2, x0]
	cmp	w1, 255
	bne	.L620
	ldrb	w0, [x20, 1]
	cmp	w0, 255
	bne	.L617
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1408
	mov	w2, 81
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L617:
	add	x0, x19, :lo12:.LANCHOR0
	sbfiz	x21, x21, 6, 32
	add	x0, x0, 1304
	ldrb	w1, [x20, 1]
	strb	w1, [x0, x21]
	b	.L613
	.size	buf_add_tail, .-buf_add_tail
	.align	2
	.type	queue_read_cmd, %function
queue_read_cmd:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	mov	w0, 48
	ldr	w1, [x19, 40]
	bl	flash_start_page_read
	strb	wzr, [x19, 59]
	mov	w0, 1
	strb	w0, [x19, 58]
	mov	w0, -1
	strb	w0, [x19]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	x1, x19
	add	x0, x0, 3354
	bl	buf_add_tail
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	queue_read_cmd, .-queue_read_cmd
	.align	2
	.global	zbuf_free
	.type	zbuf_free, %function
zbuf_free:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	ldrb	w1, [x0, 2]
	and	w1, w1, 8
	strb	w1, [x0, 2]
	cbz	w1, .L625
	ldr	w1, [x0, 36]
	cmn	w1, #1
	beq	.L625
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 3352
	bl	buf_add_tail
.L626:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19, 3353]
	add	w0, w0, 1
	strb	w0, [x19, 3353]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L625:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1, 3352]
	strb	w2, [x0]
	ldrb	w0, [x0, 1]
	strb	w0, [x1, 3352]
	b	.L626
	.size	zbuf_free, .-zbuf_free
	.align	2
	.global	buf_alloc
	.type	buf_alloc, %function
buf_alloc:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x20, x1, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	and	w21, w0, 255
	mov	x19, x1
	ldrb	w0, [x20, 3353]
	cbz	w0, .L635
.L638:
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1304
	ldrb	w3, [x1, 3352]
	ubfiz	x2, x3, 6, 8
	add	x0, x0, x2
	cbz	w21, .L636
.L637:
	add	x1, x19, :lo12:.LANCHOR0
	sbfiz	x3, x3, 6, 32
	add	x4, x1, 1304
	add	x2, x4, x3
	ldrb	w5, [x4, x3]
	strb	w5, [x1, 3352]
	ldrb	w5, [x1, 3353]
	strh	wzr, [x2, 50]
	sub	w5, w5, #1
	strb	w5, [x1, 3353]
	mov	w1, 1
	strb	w1, [x2, 2]
	mov	w1, -1
	strb	wzr, [x2, 56]
	strb	w1, [x4, x3]
	mov	w1, -1
	strb	wzr, [x2, 57]
	str	xzr, [x2, 16]
	str	w1, [x2, 36]
	b	.L634
.L635:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1424
	mov	w2, 121
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldrb	w0, [x20, 3353]
	cbnz	w0, .L638
.L640:
	mov	x0, 0
.L634:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L636:
	ldrb	w1, [x1, 3353]
	cmp	w1, 1
	bne	.L637
	b	.L640
	.size	buf_alloc, .-buf_alloc
	.align	2
	.global	buf_remove_buf
	.type	buf_remove_buf, %function
buf_remove_buf:
	ldrb	w4, [x1, 1]
	ldrb	w2, [x0]
	cmp	w4, w2
	bne	.L648
	ldrb	w1, [x1]
	strb	w1, [x0]
.L652:
	mov	w0, 1
	ret
.L649:
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0, x2]
	cmp	w4, w2
	bne	.L650
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0, x3]
	mov	w0, -1
	strb	w0, [x1]
	b	.L652
.L648:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 1304
.L650:
	cmp	w2, 255
	bne	.L649
	mov	w0, 0
	ret
	.size	buf_remove_buf, .-buf_remove_buf
	.align	2
	.global	buf_remove_free
	.type	buf_remove_free, %function
buf_remove_free:
	stp	x29, x30, [sp, -32]!
	adrp	x5, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	add	x0, x5, :lo12:.LANCHOR0
	mov	x19, x5
	ldrb	w0, [x0, 3353]
	cbnz	w0, .L654
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1440
	mov	w2, 172
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L654:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19, 3353]
	cbz	w0, .L653
	mov	x1, x20
	add	x0, x19, 3352
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L653
	ldrb	w0, [x19, 3353]
	sub	w0, w0, #1
	strb	w0, [x19, 3353]
	ldrb	w0, [x20, 2]
	orr	w0, w0, 1
	strb	w0, [x20, 2]
.L653:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	buf_remove_free, .-buf_remove_free
	.align	2
	.global	dump_buf_info
	.type	dump_buf_info, %function
dump_buf_info:
	sub	sp, sp, #64
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	str	x21, [sp, 48]
	add	x20, x19, 1304
	adrp	x21, .LC50
	add	x19, x19, 3352
	add	x21, x21, :lo12:.LC50
	ldrb	w1, [x19, 2]
	bl	printk
	ldrb	w1, [x19, 3]
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	bl	printk
	ldrb	w1, [x19, 4]
	adrp	x0, .LC46
	add	x0, x0, :lo12:.LC46
	bl	printk
	ldrb	w1, [x19, 5]
	adrp	x0, .LC47
	add	x0, x0, :lo12:.LC47
	bl	printk
	ldrb	w1, [x19]
	adrp	x0, .LC48
	add	x0, x0, :lo12:.LC48
	bl	printk
	ldrb	w1, [x19, 1]
	adrp	x0, .LC49
	add	x0, x0, :lo12:.LC49
	bl	printk
.L660:
	ldr	w0, [x20, 40]
	add	x20, x20, 64
	ldrb	w5, [x20, -6]
	ldrh	w4, [x20, -14]
	ldrb	w3, [x20, -62]
	ldrb	w2, [x20, -64]
	ldrb	w1, [x20, -63]
	str	w0, [sp]
	mov	x0, x21
	ldr	w7, [x20, -28]
	ldr	w6, [x20, -12]
	bl	printk
	cmp	x20, x19
	bne	.L660
	ldp	x19, x20, [sp, 32]
	ldp	x29, x30, [sp, 16]
	ldr	x21, [sp, 48]
	add	sp, sp, 64
	ret
	.size	dump_buf_info, .-dump_buf_info
	.align	2
	.global	flash_check_bad_block
	.type	flash_check_bad_block, %function
flash_check_bad_block:
	adrp	x3, .LANCHOR2
	add	x3, x3, :lo12:.LANCHOR2
	and	w5, w0, 255
	lsr	w4, w1, 5
	ldrb	w0, [x3, 21]
	ldrh	w2, [x3, 22]
	mul	w2, w2, w0
	mov	x0, 912
	and	w2, w2, 65535
	add	w2, w2, 31
	asr	w2, w2, 5
	lsl	w2, w2, 2
	umaddl	x0, w2, w5, x0
	adrp	x2, .LANCHOR0+1048
	ldr	x2, [x2, #:lo12:.LANCHOR0+1048]
	add	x0, x0, w4, uxtw 2
	ldr	w0, [x2, x0]
	lsr	w0, w0, w1
	and	w0, w0, 1
	ret
	.size	flash_check_bad_block, .-flash_check_bad_block
	.align	2
	.global	flash_mask_bad_block
	.type	flash_mask_bad_block, %function
flash_mask_bad_block:
	stp	x29, x30, [sp, -48]!
	mov	w2, w1
	add	x29, sp, 0
	str	x21, [sp, 32]
	and	w21, w0, 255
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	stp	x19, x20, [sp, 16]
	mov	w20, w1
	ldrh	w19, [x0, 22]
	ldrb	w1, [x0, 21]
	adrp	x0, .LC51
	add	x0, x0, :lo12:.LC51
	mul	w19, w19, w1
	mov	w1, w21
	bl	printk
	and	w19, w19, 65535
	lsr	w0, w20, 5
	add	w1, w19, 31
	mov	x19, 912
	asr	w1, w1, 5
	mov	w2, 1
	lsl	w1, w1, 2
	umaddl	x1, w1, w21, x19
	add	x1, x1, w0, uxtw 2
	adrp	x0, .LANCHOR0+1048
	ldr	x3, [x0, #:lo12:.LANCHOR0+1048]
	lsl	w0, w2, w20
	ldr	w2, [x3, x1]
	orr	w2, w2, w0
	str	w2, [x3, x1]
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_mask_bad_block, .-flash_mask_bad_block
	.align	2
	.global	str2hex
	.type	str2hex, %function
str2hex:
	ldrb	w1, [x0]
	cmp	w1, 48
	bne	.L667
	ldrb	w1, [x0, 1]
	add	x2, x0, 2
	and	w1, w1, -33
	and	w1, w1, 255
	cmp	w1, 88
	csel	x0, x2, x0, eq
.L667:
	mov	x2, x0
	ldrb	w1, [x2], 1
	and	w1, w1, -33
	and	w1, w1, 255
	cmp	w1, 88
	mov	w1, 0
	csel	x0, x2, x0, eq
.L669:
	ldrb	w2, [x0]
	cbnz	w2, .L674
.L666:
	mov	w0, w1
	ret
.L674:
	sub	w3, w2, #48
	and	w3, w3, 255
	cmp	w3, 9
	bhi	.L670
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #48
.L671:
	add	x0, x0, 1
	b	.L669
.L670:
	sub	w3, w2, #97
	and	w3, w3, 255
	cmp	w3, 5
	bhi	.L672
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #87
	b	.L671
.L672:
	sub	w3, w2, #65
	and	w3, w3, 255
	cmp	w3, 5
	bhi	.L666
	add	w1, w2, w1, lsl 4
	sub	w1, w1, #55
	b	.L671
	.size	str2hex, .-str2hex
	.align	2
	.global	zftl_proc_debug_init
	.type	zftl_proc_debug_init, %function
zftl_proc_debug_init:
	stp	x29, x30, [sp, -16]!
	adrp	x3, .LANCHOR1
	add	x3, x3, :lo12:.LANCHOR1
	mov	x4, 0
	add	x29, sp, 0
	add	x3, x3, 1456
	mov	x2, 0
	mov	w1, 292
	adrp	x0, .LC52
	add	x0, x0, :lo12:.LC52
	bl	proc_create_data
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_proc_debug_init, .-zftl_proc_debug_init
	.align	2
	.global	ftl_print_info_to_buf
	.type	ftl_print_info_to_buf, %function
ftl_print_info_to_buf:
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LC1
	adrp	x1, .LC2
	add	x2, x2, :lo12:.LC1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x1, x1, :lo12:.LC2
	stp	x21, x22, [sp, 32]
	mov	x20, x0
	bl	sprintf
	add	x19, x20, w0, sxtw
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	add	x21, x22, :lo12:.LANCHOR0
	adrp	x1, .LC53
	add	x1, x1, :lo12:.LC53
	ldrb	w7, [x0, 14]
	ldrb	w6, [x0, 13]
	ldrb	w5, [x0, 12]
	ldrb	w4, [x0, 11]
	ldrb	w3, [x0, 10]
	ldrb	w2, [x0, 9]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x21, 1032]
	mov	x0, x19
	adrp	x1, .LC54
	add	x1, x1, :lo12:.LC54
	lsr	w2, w2, 11
	bl	sprintf
	ldr	w2, [x21, 3360]
	add	x19, x19, w0, sxtw
	mov	x0, x19
	adrp	x1, .LC55
	add	x1, x1, :lo12:.LC55
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, w0, sxtw
	adrp	x1, .LC56
	mov	x0, x19
	add	x1, x1, :lo12:.LC56
	bl	strcpy
	add	x19, x19, 10
	ldr	w2, [x21, 3364]
	mov	x0, x19
	adrp	x1, .LC57
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w2, [x21, 1032]
	mov	x0, x19
	adrp	x1, .LC58
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC59
	add	x1, x1, :lo12:.LC59
	ldr	w2, [x0, 524]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC60
	add	x1, x1, :lo12:.LC60
	ldr	w2, [x0, 528]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3368]
	mov	x0, x19
	adrp	x1, .LC61
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3370]
	mov	x0, x19
	adrp	x1, .LC62
	add	x1, x1, :lo12:.LC62
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3372]
	mov	x0, x19
	adrp	x1, .LC63
	add	x1, x1, :lo12:.LC63
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3374]
	mov	x0, x19
	adrp	x1, .LC64
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3376]
	mov	x0, x19
	adrp	x1, .LC65
	add	x1, x1, :lo12:.LC65
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3378]
	mov	x0, x19
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrb	w4, [x21, 3380]
	mov	x0, x19
	ldrb	w3, [x21, 3381]
	adrp	x1, .LC67
	ldrb	w2, [x21, 3353]
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC68
	add	x1, x1, :lo12:.LC68
	ldrh	w3, [x0, 146]
	ldrh	w2, [x0, 148]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC69
	add	x1, x1, :lo12:.LC69
	ldp	w2, w0, [x0, 16]
	add	w2, w0, w2, lsr 11
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC70
	add	x1, x1, :lo12:.LC70
	ldp	w2, w0, [x0, 24]
	add	w2, w0, w2, lsr 11
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC71
	add	x1, x1, :lo12:.LC71
	ldr	w2, [x0, 64]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC72
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x0, 68]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC73
	add	x1, x1, :lo12:.LC73
	ldr	w2, [x0, 12]
	mov	w0, 10
	udiv	w2, w2, w0
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC74
	ldrb	w2, [x22, #:lo12:.LANCHOR0]
	add	x1, x1, :lo12:.LC74
	ldrh	w4, [x0, 150]
	ldr	w3, [x0, 156]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC75
	add	x1, x1, :lo12:.LC75
	ldr	w2, [x0, 556]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC76
	add	x1, x1, :lo12:.LC76
	ldr	w2, [x0, 552]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC77
	add	x1, x1, :lo12:.LC77
	ldr	w2, [x0, 52]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC78
	add	x1, x1, :lo12:.LC78
	ldr	w2, [x0, 60]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC79
	add	x1, x1, :lo12:.LC79
	ldr	w2, [x0, 76]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC80
	add	x1, x1, :lo12:.LC80
	ldr	w2, [x0, 8]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC81
	add	x1, x1, :lo12:.LC81
	ldrb	w4, [x0, 25]
	ldrh	w3, [x0, 22]
	ldrh	w2, [x0, 16]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC82
	add	x1, x1, :lo12:.LC82
	ldrb	w4, [x0, 57]
	ldrh	w3, [x0, 54]
	ldrh	w2, [x0, 48]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC83
	add	x1, x1, :lo12:.LC83
	ldrb	w4, [x0, 89]
	ldrh	w3, [x0, 86]
	ldrh	w2, [x0, 80]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC84
	add	x1, x1, :lo12:.LC84
	ldrh	w6, [x0, 96]
	ldrh	w5, [x0, 92]
	ldrh	w4, [x0, 88]
	ldrh	w2, [x0, 74]
	ldr	w3, [x0, 84]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 3384]
	adrp	x1, .LC85
	add	x1, x1, :lo12:.LC85
	ldrh	w6, [x0, 98]
	ldrh	w5, [x0, 94]
	ldrh	w4, [x0, 90]
	ldr	w3, [x0, 80]
	ldrh	w2, [x0, 72]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldrh	w2, [x21, 3392]
	mov	x0, x19
	adrp	x1, .LC86
	add	x1, x1, :lo12:.LC86
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	w6, [x21, 3396]
	mov	x0, x19
	ldrh	w5, [x21, 3400]
	adrp	x1, .LC87
	ldrh	w4, [x21, 3402]
	add	x1, x1, :lo12:.LC87
	ldrh	w3, [x21, 3404]
	ldrh	w2, [x21, 3406]
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	adrp	x1, .LC88
	add	x1, x1, :lo12:.LC88
	ldrh	w5, [x0, 590]
	ldrh	w4, [x0, 588]
	ldrh	w3, [x0, 586]
	ldrh	w2, [x0, 584]
	mov	x0, x19
	bl	sprintf
	add	x19, x19, w0, sxtw
	ldr	x0, [x21, 1128]
	ldr	x1, [x21, 3384]
	add	x5, x0, 512
	mov	x0, x19
	ldp	w2, w3, [x5, 24]
	ldp	w4, w6, [x5, 32]
	ldr	w5, [x1, 44]
	adrp	x1, .LC89
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x0, x19, w0, sxtw
	sub	w0, w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
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
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 2]
	orr	w1, w1, 2
	strb	w1, [x0, 2]
	mov	x1, x0
	add	x0, x19, 3408
	bl	buf_add_tail
	ldrb	w0, [x19, 3381]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 3381]
	ldr	x19, [sp, 16]
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
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp, 16]
	and	w19, w0, 65535
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w24, w1, 65535
	and	w23, w2, 65535
	tbz	x0, 8, .L688
	add	x1, x21, :lo12:.LANCHOR0
	uxtw	x0, w19
	ldr	x2, [x1, 1104]
	ldr	x3, [x1, 1120]
	ldrh	w7, [x1, 3402]
	add	x2, x2, x0, lsl 2
	ldrh	w6, [x1, 3472]
	mov	w1, w19
	ldrh	w5, [x3, x0, lsl 1]
	adrp	x0, .LC90
	ldrb	w4, [x2, 2]
	mov	w3, w23
	mov	w2, w24
	add	x0, x0, :lo12:.LC90
	ubfx	x4, x4, 5, 3
	bl	printk
.L688:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w0, [x0, 1096]
	cmp	w0, w19
	bhi	.L689
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1552
	mov	w2, 543
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L689:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w0, [x1, 1096]
	cmp	w0, w19
	bhi	.L690
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	mov	w20, 0
	tbz	x0, 10, .L687
	adrp	x0, .LC91
	mov	w3, w23
	mov	w2, w24
	mov	w1, w19
	add	x0, x0, :lo12:.LC91
	bl	printk
.L687:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L690:
	ldr	x2, [x1, 1120]
	uxtw	x0, w19
	ldrh	w3, [x2, x0, lsl 1]
	ldr	x2, [x1, 1104]
	add	x0, x2, x0, lsl 2
	ldrb	w2, [x0, 2]
	tst	w2, 224
	bne	.L692
	mov	w20, 0
	cbz	w3, .L687
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1552
	mov	w2, 553
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L687
.L692:
	ldrh	w0, [x1, 3416]
	cmp	w0, w19
	beq	.L711
	ldr	x0, [x1, 1128]
	ldrh	w5, [x0, 48]
	cmp	w5, w19
	beq	.L711
	ldrh	w5, [x0, 16]
	cmp	w5, w19
	beq	.L711
	ldrh	w5, [x0, 80]
	cmp	w5, w19
	beq	.L711
	ldrh	w5, [x1, 3472]
	add	x1, x1, 3474
	mov	w4, 0
.L693:
	cmp	w4, w5
	bcc	.L694
	cbnz	w24, .L698
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	ldrh	w6, [x1, 1280]
	cmp	w19, w6
	beq	.L711
	add	x1, x1, 1288
	mov	x4, 0
.L697:
	ldrh	w7, [x4, x1]
	cmp	w19, w7
	bne	.L696
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	mov	w20, 0
	tbz	x0, 8, .L687
	mov	w5, w6
	mov	w4, w19
	ubfx	x2, x2, 5, 3
	mov	w1, w19
	adrp	x0, .LC92
	add	x0, x0, :lo12:.LC92
	bl	printk
	b	.L687
.L694:
	ldrh	w6, [x1], 2
	cmp	w6, w19
	beq	.L711
	add	w4, w4, 1
	b	.L693
.L696:
	add	x4, x4, 2
	cmp	x4, 16
	bne	.L697
	ubfiz	x20, x23, 7, 16
	add	x20, x20, 136
	add	x20, x0, x20
.L699:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L700
	add	x0, x21, :lo12:.LANCHOR0
	mov	w4, w3
	mov	w1, w19
	ubfx	x3, x2, 5, 3
	mov	w2, w24
	ldrh	w6, [x0, 3402]
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	bl	printk
.L700:
	mov	x0, x20
	add	x1, x20, 128
	mov	w2, 65535
.L703:
	ldrh	w3, [x0]
	cmp	w3, w2
	bne	.L701
	strh	w19, [x0]
	add	x21, x21, :lo12:.LANCHOR0
	cbz	w24, .L702
	ldr	x1, [x21, 1128]
	ldrh	w0, [x1, 124]
	add	w0, w0, 1
	strh	w0, [x1, 124]
.L725:
	mov	w20, 1
	b	.L687
.L698:
	add	x20, x0, 392
	b	.L699
.L702:
	ldr	x0, [x21, 1128]
	add	x23, x0, w23, uxth 1
	ldrh	w0, [x23, 120]
	add	w0, w0, 1
	strh	w0, [x23, 120]
	b	.L725
.L701:
	add	x0, x0, 2
	cmp	x1, x0
	bne	.L703
	b	.L725
.L711:
	mov	w20, 0
	b	.L687
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_mark_bad_ppa
	.type	gc_mark_bad_ppa, %function
gc_mark_bad_ppa:
	stp	x29, x30, [sp, -64]!
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x20, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	mov	w19, 24
	mov	w23, 1
	ldrb	w4, [x21, 1205]
	add	x21, x21, 3416
	ldrh	w3, [x1, 1304]
	sub	w19, w19, w4
	ldrb	w1, [x1, 1306]
	sub	w19, w19, w3
	lsr	w2, w0, w3
	mov	w3, w0
	lsl	w19, w23, w19
	sub	w19, w19, #1
	and	w19, w19, w2
	and	w22, w2, 65535
	mov	w2, w22
	adrp	x0, .LC94
	add	x0, x0, :lo12:.LC94
	udiv	w19, w19, w1
	ldr	w1, [x21, 2200]
	and	w19, w19, 65535
	bl	printk
	mov	w1, w23
	mov	w2, 0
	mov	w0, w19
	bl	gc_add_sblk
	ldr	w0, [x21, 2200]
	mov	w1, 0
.L727:
	cmp	w1, w0
	bcc	.L729
	cmp	w0, 5
	bhi	.L728
	add	x20, x20, :lo12:.LANCHOR0
	add	w1, w0, 1
	str	w1, [x20, 5616]
	add	x20, x20, w0, uxtw 1
	strh	w22, [x20, 5620]
.L728:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L729:
	add	x2, x21, w1, sxtw 1
	ldrh	w2, [x2, 2204]
	cmp	w2, w22
	beq	.L728
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L727
	.size	gc_mark_bad_ppa, .-gc_mark_bad_ppa
	.align	2
	.global	gc_get_src_ppa_from_index
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	adrp	x1, .LANCHOR3+1312
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1, #:lo12:.LANCHOR3+1312]
	ldr	w0, [x1, x0]
	ret
	.size	gc_get_src_ppa_from_index, .-gc_get_src_ppa_from_index
	.align	2
	.global	gc_write_completed
	.type	gc_write_completed, %function
gc_write_completed:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x25, x24, 1304
	stp	x19, x20, [sp, 16]
.L733:
	add	x3, x22, :lo12:.LANCHOR0
	ldrb	w0, [x3, 3356]
	cmp	w0, 255
	bne	.L746
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L746:
	sxtw	x21, w0
	add	x1, x3, 1304
	lsl	x4, x21, 6
	add	x2, x1, x4
	ldrb	w0, [x1, x4]
	ldr	w1, [x2, 52]
	strb	w0, [x3, 3356]
	ldrh	w23, [x2, 48]
	cbz	w1, .L734
	ldr	w2, [x2, 40]
	mov	w0, 1
	str	w2, [x3, 5612]
	strh	w0, [x3, 5610]
	adrp	x0, .LC95
	add	x0, x0, :lo12:.LC95
	bl	printk
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1568
	mov	w2, 956
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L734:
	adrp	x20, .LANCHOR3
	add	x1, x20, :lo12:.LANCHOR3
	ldrb	w0, [x1, 1320]
	cmp	w0, 3
	bne	.L735
	ldrb	w0, [x24, 1212]
	cbnz	w0, .L735
	ldr	x0, [x24, 1128]
	ldrb	w1, [x1, 1321]
	ldrb	w0, [x0, 89]
	mov	w19, w0
	cmp	w1, w0
	bhi	.L747
	cmp	w0, 2
	mov	w0, 2
	csel	w19, w19, w0, ls
.L736:
	add	w19, w19, w19, lsl 1
.L737:
	add	x0, x20, :lo12:.LANCHOR3
	and	x1, x23, 65535
	ldr	x2, [x0, 1328]
	add	x0, x25, x21, lsl 6
	ldrb	w0, [x0, 1]
	ldrb	w1, [x2, x1]
	cmp	w1, w0
	beq	.L738
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1568
	mov	w2, 976
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L738:
	add	x0, x25, x21, lsl 6
	ldrb	w0, [x0, 61]
	cmp	w0, 3
	beq	.L739
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1320]
	cmp	w1, 3
	bne	.L739
	ldrb	w1, [x0, 1336]
	cbnz	w1, .L739
	ldrb	w1, [x24, 1212]
	cbnz	w1, .L739
	ldrb	w1, [x24, 1213]
	cbnz	w1, .L739
	ldrb	w1, [x0, 1322]
	cbz	w1, .L740
	ldrb	w0, [x0, 1323]
	cbnz	w0, .L739
.L740:
	add	x20, x20, :lo12:.LANCHOR3
	mov	w0, 0
	ldr	x2, [x20, 1328]
.L741:
	cmp	w19, w0, uxth
	bls	.L733
	add	w1, w23, w0
	add	w0, w0, 1
	ldrb	w1, [x2, x1]
	add	x1, x25, x1, lsl 6
	strb	wzr, [x1, 61]
	b	.L741
.L747:
	mov	w19, 1
	b	.L736
.L735:
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1322]
	cbz	w1, .L748
	ldrb	w0, [x0, 1323]
	cmp	w0, 0
	cset	w19, ne
	add	w19, w19, 1
	b	.L737
.L748:
	mov	w19, 1
	b	.L737
.L739:
	add	x20, x20, :lo12:.LANCHOR3
	strh	w23, [x24, 5524]
	mov	w21, 0
	mov	w26, -1
.L742:
	cmp	w19, w21, uxth
	bls	.L733
	ldr	x2, [x20, 1328]
	add	w1, w23, w21
	add	w21, w21, 1
	ldrb	w0, [x2, x1]
	strb	w26, [x2, x1]
	sbfiz	x1, x0, 6, 32
	ubfiz	x0, x0, 6, 8
	add	x1, x25, x1
	add	x0, x25, x0
	strb	wzr, [x1, 61]
	bl	zbuf_free
	ldrb	w0, [x24, 3423]
	sub	w0, w0, #1
	strb	w0, [x24, 3423]
	b	.L742
	.size	gc_write_completed, .-gc_write_completed
	.align	2
	.global	gc_get_src_blk
	.type	gc_get_src_blk, %function
gc_get_src_blk:
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	ldr	x1, [x0, 1128]
	ldrh	w0, [x1, 124]
	cbz	w0, .L761
	add	x1, x1, 392
	mov	w3, 1
.L762:
	add	x4, x1, 128
	mov	w5, 65535
.L766:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L764
	mov	w4, -1
	strh	w4, [x1]
	add	x2, x2, :lo12:.LANCHOR0
	cbz	w3, .L765
	ldr	x2, [x2, 1128]
	ldrh	w1, [x2, 124]
	sub	w1, w1, #1
	strh	w1, [x2, 124]
	ret
.L761:
	adrp	x0, .LANCHOR3+1337
	ldrb	w0, [x0, #:lo12:.LANCHOR3+1337]
	add	x3, x1, w0, sxtw 1
	ldrh	w3, [x3, 120]
	cbz	w3, .L767
	ubfiz	x0, x0, 7, 8
	mov	w3, 0
	add	x0, x0, 136
	add	x1, x1, x0
	b	.L762
.L765:
	adrp	x1, .LANCHOR3+1337
	ldr	x2, [x2, 1128]
	ldrb	w1, [x1, #:lo12:.LANCHOR3+1337]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1, 120]
	sub	w2, w2, #1
	strh	w2, [x1, 120]
	ret
.L764:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L766
	ret
.L767:
	mov	w0, 65535
	ret
	.size	gc_get_src_blk, .-gc_get_src_blk
	.align	2
	.global	gc_free_temp_buf
	.type	gc_free_temp_buf, %function
gc_free_temp_buf:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldrb	w1, [x0, 3423]
	cbz	w1, .L776
	ldrb	w1, [x0, 3353]
	cmp	w1, 1
	bhi	.L776
	adrp	x21, .LANCHOR3
	add	x2, x21, :lo12:.LANCHOR3
	ldrh	w20, [x0, 5524]
	add	x0, x0, 1304
	ldrb	w4, [x2, 1321]
	add	w3, w20, 24
	ldrh	w1, [x2, 1338]
	mul	w1, w1, w4
	ldr	x4, [x2, 1328]
	cmp	w1, w3
	csel	w1, w1, w3, ls
.L771:
	cmp	w20, w1
	bcc	.L774
.L776:
	mov	w0, 0
	b	.L769
.L774:
	uxtw	x22, w20
	ldrb	w2, [x4, x22]
	cmp	w2, 255
	beq	.L772
	sbfiz	x3, x2, 6, 32
	add	x3, x0, x3
	ldrb	w3, [x3, 61]
	cbnz	w3, .L772
	ubfiz	x2, x2, 6, 8
	add	x0, x0, x2
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L773
	add	x0, x21, :lo12:.LANCHOR3
	mov	w1, w20
	ldr	x0, [x0, 1328]
	ldrb	w2, [x0, x22]
	adrp	x0, .LC96
	add	x0, x0, :lo12:.LC96
	bl	printk
.L773:
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x21, 1328]
	strb	w1, [x0, x22]
	ldrb	w0, [x19, 3423]
	sub	w0, w0, #1
	strb	w0, [x19, 3423]
	mov	w0, 1
.L769:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L772:
	add	w20, w20, 1
	b	.L771
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	get_ink_scaned_blk
	.type	get_ink_scaned_blk, %function
get_ink_scaned_blk:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x0, x1, 3416
	ldrh	w2, [x1, 5528]
	cbz	w2, .L786
	sub	w2, w2, #1
	strh	w2, [x1, 5528]
	add	x0, x0, w2, sxtw 1
	ldrh	w0, [x0, 2114]
	ret
.L786:
	mov	w0, 65535
	ret
	.size	get_ink_scaned_blk, .-get_ink_scaned_blk
	.align	2
	.global	print_gc_debug_info
	.type	print_gc_debug_info, %function
print_gc_debug_info:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w6, [x0, 3392]
	ldrb	w5, [x0, 3423]
	ldrb	w4, [x0, 3353]
	ldrh	w3, [x0, 5522]
	ldrh	w2, [x0, 3418]
	ldrh	w1, [x0, 3416]
	adrp	x0, .LC97
	add	x0, x0, :lo12:.LC97
	bl	printk
	ldp	x29, x30, [sp], 16
	ret
	.size	print_gc_debug_info, .-print_gc_debug_info
	.align	2
	.global	_list_pop_index_node
	.type	_list_pop_index_node, %function
_list_pop_index_node:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	x20, [x0]
	cbz	x20, .L795
	adrp	x3, .LANCHOR0+1040
	and	w1, w1, 65535
	mov	w4, 65535
	mov	w5, 6
	ldr	x19, [x3, #:lo12:.LANCHOR0+1040]
.L791:
	cbnz	w1, .L792
.L794:
	sub	x19, x20, x19
	mov	x1, -6148914691236517206
	asr	x19, x19, 1
	movk	x1, 0xaaab, lsl 0
	mul	x19, x19, x1
	and	w19, w19, 65535
	mov	w1, w19
	bl	_list_remove_node
	mov	w0, -1
	strh	w0, [x20]
	strh	w0, [x20, 2]
	mov	w0, w19
.L789:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L792:
	ldrh	w3, [x20]
	cmp	w3, w4
	beq	.L794
	sub	w1, w1, #1
	umaddl	x20, w3, w5, x19
	and	w1, w1, 65535
	b	.L791
.L795:
	mov	w0, 65535
	b	.L789
	.size	_list_pop_index_node, .-_list_pop_index_node
	.align	2
	.global	_list_get_gc_head_node
	.type	_list_get_gc_head_node, %function
_list_get_gc_head_node:
	ldr	x0, [x0]
	and	w1, w1, 65535
	cbz	x0, .L802
	adrp	x2, .LANCHOR0+1040
	mov	w3, 65535
	mov	w4, 6
	ldr	x2, [x2, #:lo12:.LANCHOR0+1040]
.L799:
	cbz	w1, .L800
	ldrh	w0, [x0]
	cmp	w0, w3
	bne	.L801
	ret
.L801:
	sub	w1, w1, #1
	umaddl	x0, w0, w4, x2
	and	w1, w1, 65535
	b	.L799
.L802:
	mov	w0, 65535
	ret
.L800:
	sub	x0, x0, x2
	mov	x1, -6148914691236517206
	asr	x0, x0, 1
	movk	x1, 0xaaab, lsl 0
	mul	x0, x0, x1
	and	w0, w0, 65535
	ret
	.size	_list_get_gc_head_node, .-_list_get_gc_head_node
	.align	2
	.type	zftl_get_gc_node.part.10, %function
zftl_get_gc_node.part.10:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x0, x0, 1344
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
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	and	w0, w1, 255
	stp	x19, x20, [sp, 16]
	str	w0, [x29, 144]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	and	w20, w2, 255
	stp	x27, x28, [sp, 80]
	ldr	x2, [x0, 1128]
	add	x1, x2, w22, sxtw 1
	ldrh	w21, [x1, 120]
	cbz	w21, .L807
	mov	w0, w21
.L806:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L807:
	ldrh	w1, [x0, 3472]
	cmp	w1, 1
	bhi	.L809
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	strh	wzr, [x0, 1352]
	strh	wzr, [x0, 1354]
	strh	wzr, [x0, 1356]
.L809:
	cbnz	w22, .L810
	adrp	x25, .LANCHOR3
	adrp	x27, .LC98
	add	x26, x25, :lo12:.LANCHOR3
	add	x27, x27, :lo12:.LC98
	add	x0, x26, 1360
	mov	w23, 0
	mov	w24, 0
	str	x0, [x29, 136]
.L811:
	add	w28, w20, 1
	cmp	w24, w28
	bge	.L816
	ldrh	w6, [x26, 1354]
	ldr	x0, [x29, 136]
	mov	w1, w6
	bl	_list_get_gc_head_node
	add	w2, w6, 1
	and	w2, w2, 65535
	strh	w2, [x26, 1354]
	and	w1, w0, 65535
	mov	w0, 65535
	mov	w4, w1
	cmp	w1, w0
	beq	.L812
	adrp	x0, .LANCHOR2
	uxtw	x6, w1
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L813
	add	x0, x19, :lo12:.LANCHOR0
	str	x6, [x29, 128]
	str	w1, [x29, 120]
	ldr	x0, [x0, 1120]
	ldrh	w3, [x0, x6, lsl 1]
	mov	x0, x27
	bl	printk
	ldr	w4, [x29, 120]
	ldr	x6, [x29, 128]
.L813:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 1120]
	ldrh	w0, [x0, 3402]
	ldrh	w1, [x1, x6, lsl 1]
	cmp	w1, w0
	bcs	.L814
	mov	w2, 0
	mov	w1, 0
	mov	w0, w4
	bl	gc_add_sblk
	cbz	w0, .L815
	add	w5, w23, 1
	and	w23, w5, 65535
	cmp	w23, w20
	bcc	.L815
.L816:
	ldr	x0, [x29, 144]
	tbz	x0, 1, .L818
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w0, [x6, 3378]
	cmp	w0, 32
	bls	.L818
	adrp	x27, .LANCHOR3
	add	x25, x27, :lo12:.LANCHOR3
	add	x0, x25, 1368
	mov	x26, x6
	str	x0, [x29, 136]
	mov	w24, 0
	and	w0, w20, 65535
	str	w0, [x29, 128]
.L819:
	cmp	w28, w24
	ble	.L823
	ldrh	w7, [x25, 1356]
	ldr	x0, [x29, 136]
	mov	w1, w7
	bl	_list_get_gc_head_node
	add	w7, w7, 1
	strh	w7, [x25, 1356]
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L820
	ldr	x2, [x26, 1120]
	ubfiz	x1, x1, 1, 16
	ldrh	w2, [x2, x1]
	ldrh	w1, [x26, 3400]
	cmp	w2, w1
	bcs	.L820
	mov	w2, 0
	mov	w1, 0
	bl	gc_add_sblk
	cbz	w0, .L822
	add	w5, w23, 1
	ldr	w0, [x29, 128]
	and	w23, w5, 65535
	cmp	w23, w0
	bcc	.L822
.L823:
	cmp	w23, w20
	bcs	.L825
	add	x27, x27, :lo12:.LANCHOR3
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w0, [x27, 1338]
	ldrb	w4, [x27, 1321]
	ldrh	w1, [x27, 1376]
	ldrh	w3, [x2, 3400]
	mul	w0, w0, w4
	sub	w0, w0, w1, lsr 2
	cmp	w3, w0
	bge	.L818
	add	w1, w3, w1, lsr 3
	strh	w1, [x2, 3400]
.L818:
	ldr	x0, [x29, 144]
	tbz	x0, 0, .L826
	and	w25, w20, 65535
	cmp	w23, w25
	bcs	.L826
	adrp	x24, .LANCHOR3
	add	x27, x24, :lo12:.LANCHOR3
	mov	w26, 65535
.L831:
	ldrh	w6, [x27, 1352]
	mov	w0, w6
	bl	zftl_get_gc_node.part.10
	add	w6, w6, 1
	strh	w6, [x27, 1352]
	cmp	w26, w0, uxth
	beq	.L827
	mov	w2, 0
	mov	w1, 0
	bl	gc_add_sblk
	cbz	w0, .L828
	add	w5, w23, 1
	and	w23, w5, 65535
	cmp	w25, w23
	bhi	.L828
.L829:
	add	x24, x24, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x24, 1376]
	ldrh	w1, [x19, 3402]
	cmp	w1, w0, lsr 1
	bls	.L826
	sub	w0, w1, w0, lsr 3
	b	.L917
.L814:
	add	x25, x25, :lo12:.LANCHOR3
	strh	wzr, [x25, 1354]
	b	.L816
.L812:
	strh	wzr, [x26, 1354]
	b	.L816
.L815:
	add	w24, w24, 1
	and	w24, w24, 65535
	b	.L811
.L820:
	strh	wzr, [x25, 1356]
	b	.L823
.L822:
	add	w3, w24, 1
	and	w24, w3, 65535
	b	.L819
.L825:
	add	x1, x19, :lo12:.LANCHOR0
	add	x27, x27, :lo12:.LANCHOR3
	ldrh	w2, [x1, 3400]
	ldrh	w0, [x27, 1376]
	cmp	w2, w0
	bls	.L818
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1, 3400]
	b	.L818
.L827:
	strh	wzr, [x27, 1352]
.L830:
	cmp	w23, w25
	bcs	.L829
	add	x19, x19, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR3
	ldrh	w1, [x19, 3402]
	ldrh	w0, [x24, 1376]
	cmp	w1, w0
	bcs	.L826
	add	w0, w1, w0, lsr 3
.L917:
	strh	w0, [x19, 3402]
.L826:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L862
	ldr	w2, [x29, 144]
	adrp	x0, .LC99
	mov	w4, w20
	mov	w3, w23
	mov	w1, w22
	add	x0, x0, :lo12:.LC99
	bl	printk
.L862:
	mov	w0, w23
	b	.L806
.L828:
	add	w21, w21, 1
	and	w21, w21, 65535
	cmp	w25, w21
	bhi	.L831
	b	.L830
.L810:
	adrp	x24, .LANCHOR3
	add	x0, x24, :lo12:.LANCHOR3
	cmp	w20, 1
	ldrb	w25, [x0, 1321]
	ldrh	w0, [x0, 1338]
	mul	w25, w25, w0
	and	w25, w25, 65535
	bne	.L832
	cbz	w1, .L832
	ldrh	w2, [x2, 80]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L865
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 5522]
	sub	w25, w25, w0
	and	w25, w25, 65535
.L865:
	mov	w20, 8
.L832:
	ldr	w0, [x29, 144]
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x24, :lo12:.LANCHOR3
	and	w0, w0, 1
	str	w0, [x29, 136]
	ldr	x0, [x29, 144]
	strh	wzr, [x2, 5608]
	strh	wzr, [x3, 1378]
	tbz	x0, 0, .L866
	ldrh	w0, [x2, 3374]
	ldrh	w3, [x3, 1380]
	cmp	w0, w3, lsr 2
	bhi	.L834
	ldrh	w2, [x2, 3376]
	cmp	w2, w0
	bcs	.L867
.L834:
	cmp	w1, 1
	bls	.L836
.L838:
	mov	w23, 0
.L837:
	add	x27, x24, :lo12:.LANCHOR3
	add	x8, x19, :lo12:.LANCHOR0
	and	w0, w20, 65535
	mov	w28, 0
	mov	w26, 64
	str	w0, [x29, 128]
.L841:
	ldrh	w6, [x27, 1352]
	str	x8, [x29, 112]
	mov	w0, w6
	bl	zftl_get_gc_node.part.10
	and	w4, w0, 65535
	str	w4, [x29, 120]
	mov	w1, 65535
	cmp	w4, w1
	beq	.L839
	add	w6, w6, 1
	mov	w2, w22
	strh	w6, [x27, 1352]
	mov	w1, 0
	bl	gc_add_sblk
	ldr	x8, [x29, 112]
	cbz	w0, .L840
	ldr	w4, [x29, 120]
	add	w5, w23, 1
	ldr	x0, [x8, 1120]
	and	w23, w5, 65535
	ubfiz	x4, x4, 1, 16
	ldrh	w0, [x0, x4]
	add	w3, w28, w0
	ldr	w0, [x29, 128]
	and	w28, w3, 65535
	cmp	w23, w0
	bcs	.L835
	cmp	w25, w28
	bcc	.L835
	ldrh	w0, [x8, 3376]
	ldrh	w1, [x8, 3374]
	cmp	w0, w1, lsl 1
	ble	.L840
.L835:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x24, :lo12:.LANCHOR3
	ldrh	w1, [x0, 3376]
	ldrh	w2, [x2, 1380]
	cmp	w1, w2, lsr 2
	bhi	.L863
	ldrh	w0, [x0, 3374]
	add	w0, w0, 8
	cmp	w1, w0
	ble	.L833
.L863:
	cmp	w25, w28
	bls	.L833
	add	x27, x24, :lo12:.LANCHOR3
	mov	w26, 64
	add	x0, x27, 1360
	str	x0, [x29, 128]
	add	x0, x19, :lo12:.LANCHOR0
	str	x0, [x29, 120]
	and	w0, w20, 65535
	str	w0, [x29, 112]
.L844:
	ldrh	w6, [x27, 1354]
	ldr	x0, [x29, 128]
	mov	w1, w6
	bl	_list_get_gc_head_node
	and	w7, w0, 65535
	str	w7, [x29, 104]
	mov	w1, 65535
	cmp	w7, w1
	beq	.L842
	add	w6, w6, 1
	mov	w2, w22
	strh	w6, [x27, 1354]
	mov	w1, 0
	bl	gc_add_sblk
	cbz	w0, .L843
	ldr	x0, [x29, 120]
	add	w5, w23, 1
	ldr	w7, [x29, 104]
	and	w23, w5, 65535
	ldr	x0, [x0, 1120]
	ubfiz	x7, x7, 1, 16
	ldrh	w0, [x0, x7]
	add	w3, w28, w0
	ldr	w0, [x29, 112]
	and	w28, w3, 65535
	cmp	w23, w0
	bcs	.L833
	cmp	w25, w28
	bcs	.L843
.L833:
	ldr	x0, [x29, 144]
	tbz	x0, 1, .L845
	add	x7, x19, :lo12:.LANCHOR0
	ldrh	w0, [x7, 3378]
	cmp	w0, 32
	bls	.L845
	cmp	w28, w25
	bcs	.L845
	add	x27, x24, :lo12:.LANCHOR3
	mov	w26, 64
	add	x0, x27, 1368
	str	x0, [x29, 128]
	and	w0, w20, 65535
	str	w0, [x29, 120]
.L851:
	ldrh	w8, [x27, 1356]
	ldr	x0, [x29, 128]
	mov	w1, w8
	str	x7, [x29, 112]
	bl	_list_get_gc_head_node
	and	w4, w0, 65535
	mov	w1, 65535
	cmp	w4, w1
	beq	.L846
	add	w8, w8, 1
	strh	w8, [x27, 1356]
	cmp	w20, 1
	ldr	x7, [x29, 112]
	bne	.L847
	ldrb	w1, [x27, 1321]
	ldrh	w2, [x27, 1338]
	mul	w2, w1, w2
	ldrh	w1, [x27, 1376]
	sub	w1, w2, w1, lsr 3
	strh	w1, [x7, 3406]
.L847:
	ldr	x1, [x7, 1120]
	ubfiz	x4, x4, 1, 16
	stp	x4, x7, [x29, 104]
	ldrh	w2, [x1, x4]
	ldrh	w1, [x7, 3406]
	cmp	w2, w1
	bcs	.L848
	mov	w2, w22
	mov	w1, 0
	bl	gc_add_sblk
	ldr	x7, [x29, 112]
	cbz	w0, .L849
	ldr	x0, [x7, 1120]
	add	w5, w23, 1
	ldr	x4, [x29, 104]
	add	w21, w21, 1
	and	w23, w5, 65535
	and	w21, w21, 65535
	ldrh	w0, [x0, x4]
	add	w3, w28, w0
	ldr	w0, [x29, 120]
	and	w28, w3, 65535
	cmp	w23, w0
	bcs	.L850
	cmp	w25, w28
	bcs	.L849
.L850:
	cmp	w23, w20
	bcc	.L852
	cbnz	w21, .L853
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x24, :lo12:.LANCHOR3
	ldrh	w1, [x1, 3378]
	ldrh	w0, [x0, 1382]
	cmp	w1, w0
	bls	.L853
.L852:
	add	x4, x24, :lo12:.LANCHOR3
	add	x6, x19, :lo12:.LANCHOR0
	ldrh	w2, [x4, 1376]
	ldrh	w0, [x4, 1338]
	ldrb	w4, [x4, 1321]
	ldrh	w1, [x6, 3406]
	lsr	w2, w2, 3
	mul	w0, w0, w4
	sub	w0, w0, w2
	cmp	w1, w0
	bge	.L845
	add	w1, w1, w2
	strh	w1, [x6, 3406]
.L845:
	ldr	w0, [x29, 136]
	cbz	w0, .L826
	and	w27, w20, 65535
	cmp	w23, w27
	bcs	.L826
	cmp	w28, w25
	bcs	.L826
	add	x4, x24, :lo12:.LANCHOR3
	add	x7, x19, :lo12:.LANCHOR0
	add	x0, x4, 1360
	mov	w26, 64
	str	x0, [x29, 136]
.L861:
	ldrh	w6, [x4, 1354]
	ldr	x0, [x29, 136]
	mov	w1, w6
	stp	x7, x4, [x29, 120]
	bl	_list_get_gc_head_node
	and	w21, w0, 65535
	mov	w1, 65535
	ldr	x4, [x29, 128]
	cmp	w21, w1
	beq	.L855
	ldr	x7, [x29, 120]
	ubfiz	x21, x21, 1, 16
	add	w6, w6, 1
	strh	w6, [x4, 1354]
	ldr	x1, [x7, 1120]
	ldrh	w2, [x1, x21]
	ldrh	w1, [x7, 3404]
	cmp	w2, w1
	bcs	.L856
	ldrh	w2, [x4, 1380]
	ldrh	w1, [x7, 3376]
	cmp	w1, w2, lsr 1
	bls	.L857
.L856:
	stp	x7, x4, [x29, 120]
	mov	w2, w22
	mov	w1, 0
	bl	gc_add_sblk
	ldp	x7, x4, [x29, 120]
	cbz	w0, .L858
	ldr	x0, [x7, 1120]
	add	w5, w23, 1
	and	w23, w5, 65535
	cmp	w27, w23
	ldrh	w0, [x0, x21]
	add	w3, w28, w0
	and	w28, w3, 65535
	bls	.L859
	cmp	w25, w28
	bcs	.L858
.L860:
	add	x24, x24, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x24, 1376]
	ldrh	w1, [x19, 3404]
	cmp	w1, w0, lsr 1
	bls	.L826
	sub	w0, w1, w0, lsr 3
	strh	w0, [x19, 3404]
	b	.L826
.L836:
	add	x5, x24, :lo12:.LANCHOR3
	lsr	w0, w0, 2
	strh	w0, [x5, 1352]
	mov	w0, 0
	bl	zftl_get_gc_node.part.10
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L838
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	ldrh	w2, [x5, 1376]
	ldrh	w1, [x3, x1]
	cmp	w1, w2, lsr 2
	bcs	.L838
	mov	w1, 1
	mov	w2, w22
	strh	w1, [x5, 1352]
	mov	w1, 0
	bl	gc_add_sblk
	cmp	w0, 0
	cset	w23, ne
	b	.L837
.L839:
	strh	wzr, [x27, 1352]
	b	.L835
.L840:
	sub	w26, w26, #1
	ands	w26, w26, 65535
	bne	.L841
	b	.L835
.L842:
	cmp	w6, 64
	bls	.L833
	strh	wzr, [x27, 1354]
	b	.L833
.L843:
	sub	w26, w26, #1
	ands	w26, w26, 65535
	bne	.L844
	b	.L833
.L866:
	mov	w28, 0
	mov	w23, 0
	b	.L833
.L848:
	add	x0, x24, :lo12:.LANCHOR3
	strh	wzr, [x0, 1356]
	b	.L850
.L846:
	strh	wzr, [x27, 1356]
	b	.L850
.L849:
	sub	w26, w26, #1
	ands	w26, w26, 65535
	bne	.L851
	b	.L850
.L853:
	add	x24, x24, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x24, 1376]
	ldrb	w1, [x24, 1321]
	ldrh	w2, [x19, 3406]
	mul	w1, w1, w0
	cmp	w2, w1
	ble	.L826
	sub	w0, w2, w0, lsr 3
	strh	w0, [x19, 3406]
	b	.L826
.L855:
	cmp	w6, 64
	bls	.L857
	strh	wzr, [x4, 1354]
.L857:
	cmp	w23, w27
	bcc	.L860
.L859:
	add	x24, x24, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x24, 1376]
	ldrb	w0, [x24, 1321]
	ldrh	w2, [x19, 3404]
	mul	w0, w0, w1
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L826
	add	w1, w2, w1, lsr 3
	strh	w1, [x19, 3404]
	b	.L826
.L858:
	sub	w26, w26, #1
	ands	w26, w26, 65535
	bne	.L861
	b	.L857
.L867:
	mov	w28, 0
	mov	w23, 0
	b	.L835
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_get_gc_node
	.type	zftl_get_gc_node, %function
zftl_get_gc_node:
	stp	x29, x30, [sp, -16]!
	and	w1, w1, 65535
	and	w0, w0, 65535
	cmp	w1, 5
	add	x29, sp, 0
	bne	.L919
	mov	w1, w0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1368
.L923:
	bl	_list_get_gc_head_node
	b	.L924
.L919:
	cmp	w1, 2
	bne	.L921
	bl	zftl_get_gc_node.part.10
.L924:
	and	w0, w0, 65535
	ldp	x29, x30, [sp], 16
	ret
.L921:
	mov	w1, w0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x0, x0, 1360
	b	.L923
	.size	zftl_get_gc_node, .-zftl_get_gc_node
	.align	2
	.global	zftl_insert_free_list
	.type	zftl_insert_free_list, %function
zftl_insert_free_list:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	adrp	x3, .LANCHOR3
	add	x3, x3, :lo12:.LANCHOR3
	add	x29, sp, 0
	ldr	x0, [x2, 1104]
	add	x0, x0, w1, uxth 2
	ldrb	w0, [x0, 2]
	ands	w0, w0, 24
	bne	.L926
	add	x2, x2, 3368
	add	x0, x3, 1384
.L930:
	bl	_insert_free_list
	ldp	x29, x30, [sp], 16
	ret
.L926:
	cmp	w0, 16
	bne	.L928
	add	x2, x2, 3370
	add	x0, x3, 1392
	b	.L930
.L928:
	add	x2, x2, 3372
	add	x0, x3, 1400
	b	.L930
	.size	zftl_insert_free_list, .-zftl_insert_free_list
	.align	2
	.global	zftl_insert_data_list
	.type	zftl_insert_data_list, %function
zftl_insert_data_list:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	add	x29, sp, 0
	ldr	x0, [x2, 1104]
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0, 2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L932
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3374
	add	x0, x0, 1344
.L936:
	bl	_insert_data_list
.L931:
	ldp	x29, x30, [sp], 16
	ret
.L932:
	cmp	w3, 96
	bne	.L934
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1360
	b	.L936
.L934:
	cmp	w3, 160
	bne	.L931
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3378
	add	x0, x0, 1368
	b	.L936
	.size	zftl_insert_data_list, .-zftl_insert_data_list
	.align	2
	.global	zftl_gc_get_free_sblk
	.type	zftl_gc_get_free_sblk, %function
zftl_gc_get_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 65535
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x21, x19, :lo12:.LANCHOR0
	str	x23, [sp, 48]
	and	w23, w1, 65535
	ldr	x0, [x21, 1128]
	ldrh	w20, [x0, 588]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L938
	cbnz	w22, .L938
	mov	w1, w20
	adrp	x0, .LC100
	add	x0, x0, :lo12:.LC100
	bl	printk
	ldr	x0, [x21, 1128]
	mov	w1, -1
	strh	w1, [x0, 588]
.L939:
	mov	w0, w20
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L938:
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldrh	w0, [x2, 3370]
	ldrh	w1, [x2, 3372]
	cmp	w0, w1
	bcc	.L940
	ldrh	w2, [x2, 3368]
	cmp	w2, w0
	bls	.L941
	cbz	w1, .L941
.L940:
	cbnz	w22, .L942
	lsr	w1, w1, 2
.L943:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	add	x2, x2, 3372
	add	x0, x0, 1400
.L958:
	bl	_list_pop_index_node
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	bne	.L946
	add	x0, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, w23
	mov	w1, w20
	ldrh	w5, [x0, 3372]
	ldrh	w4, [x0, 3368]
	adrp	x0, .LC101
	ldr	x3, [x21, 1384]
	add	x0, x0, :lo12:.LC101
	bl	printk
.L946:
	cbz	w22, .L939
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L939
	add	x19, x19, :lo12:.LANCHOR0
	uxtw	x3, w20
	lsl	x0, x3, 2
	ldr	x1, [x19, 1104]
	ldr	x6, [x19, 1120]
	add	x2, x1, x0
	ldr	w4, [x1, x0]
	ldrh	w6, [x6, x3, lsl 1]
	ldrb	w2, [x2, 2]
	ldrh	w5, [x1, x0]
	ubfx	x4, x4, 11, 8
	mov	w1, w20
	adrp	x0, .LC102
	ubfx	x3, x2, 3, 2
	and	w5, w5, 2047
	ubfx	x2, x2, 5, 3
	add	x0, x0, :lo12:.LC102
	bl	printk
	b	.L939
.L942:
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
	b	.L943
.L941:
	lsr	w1, w0, 3
	cmp	w22, 0
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	csel	w1, w1, wzr, ne
	add	x2, x2, 3370
	add	x0, x0, 1392
	b	.L958
	.size	zftl_gc_get_free_sblk, .-zftl_gc_get_free_sblk
	.align	2
	.global	zftl_get_free_sblk
	.type	zftl_get_free_sblk, %function
zftl_get_free_sblk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w21, w1, 65535
	cmp	w21, 5
	bne	.L960
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	ldrh	w3, [x2, 3370]
	ldrh	w1, [x2, 3372]
	cmp	w3, w1
	bcc	.L961
	ldrh	w2, [x2, 3368]
	cmp	w2, w3
	bls	.L962
	cbz	w1, .L962
.L961:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3372
	lsr	w1, w1, 1
.L981:
	add	x0, x0, 1400
	b	.L980
.L962:
	add	x2, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3370
	add	x0, x0, 1392
	mov	w1, 0
.L980:
	bl	_list_pop_index_node
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	bne	.L965
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3+1384
	mov	w2, w21
	mov	w1, w20
	ldr	x3, [x0, #:lo12:.LANCHOR3+1384]
	adrp	x0, .LC101
	ldrh	w5, [x19, 3372]
	add	x0, x0, :lo12:.LC101
	ldrh	w4, [x19, 3368]
	bl	printk
	b	.L965
.L960:
	add	x22, x19, :lo12:.LANCHOR0
	and	w3, w0, 65535
	ldr	x0, [x22, 1128]
	ldrh	w20, [x0, 590]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L964
	cmp	w21, 1
	beq	.L964
	mov	w1, w20
	adrp	x0, .LC103
	add	x0, x0, :lo12:.LC103
	bl	printk
	ldr	x0, [x22, 1128]
	mov	w1, -1
	strh	w1, [x0, 590]
.L965:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L964:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 3368]
	ldrh	w2, [x1, 3372]
	cmp	w0, w2
	bcc	.L966
	ldrh	w1, [x1, 3370]
	cmp	w1, w0
	bls	.L967
	cbz	w2, .L967
.L966:
	bl	get_ink_scaned_blk
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	bne	.L965
	cmp	w21, 1
	bne	.L968
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x0, 3372]
	lsr	w3, w3, 1
.L968:
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	add	x2, x2, 3372
	mov	w1, w3
	add	x0, x0, :lo12:.LANCHOR3
	b	.L981
.L967:
	lsr	w0, w0, 1
	cmp	w21, 1
	csel	w3, w0, w3, eq
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3368
	mov	w1, w3
	add	x0, x0, 1384
	b	.L980
	.size	zftl_get_free_sblk, .-zftl_get_free_sblk
	.align	2
	.global	zftl_remove_data_node
	.type	zftl_remove_data_node, %function
zftl_remove_data_node:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	add	x29, sp, 0
	ldr	x0, [x2, 1104]
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0, 2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L983
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3374
	add	x0, x0, 1344
.L987:
	bl	_list_remove_node
.L982:
	ldp	x29, x30, [sp], 16
	ret
.L983:
	cmp	w3, 96
	bne	.L985
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1360
	b	.L987
.L985:
	cmp	w3, 160
	bne	.L982
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3378
	add	x0, x0, 1368
	b	.L987
	.size	zftl_remove_data_node, .-zftl_remove_data_node
	.align	2
	.global	zftl_remove_free_node
	.type	zftl_remove_free_node, %function
zftl_remove_free_node:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	adrp	x3, .LANCHOR3
	add	x3, x3, :lo12:.LANCHOR3
	add	x29, sp, 0
	ldr	x0, [x2, 1104]
	add	x0, x0, w1, uxth 2
	ldrb	w0, [x0, 2]
	ands	w0, w0, 24
	bne	.L989
	add	x2, x2, 3368
	add	x0, x3, 1384
.L993:
	bl	_list_remove_node
	ldp	x29, x30, [sp], 16
	ret
.L989:
	cmp	w0, 16
	bne	.L991
	add	x2, x2, 3370
	add	x0, x3, 1392
	b	.L993
.L991:
	add	x2, x2, 3372
	add	x0, x3, 1400
	b	.L993
	.size	zftl_remove_free_node, .-zftl_remove_free_node
	.align	2
	.global	zftl_list_update_data_list
	.type	zftl_list_update_data_list, %function
zftl_list_update_data_list:
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	stp	x29, x30, [sp, -16]!
	and	w1, w0, 65535
	add	x29, sp, 0
	ldr	x0, [x2, 1104]
	add	x0, x0, w1, uxth 2
	ldrb	w3, [x0, 2]
	and	w3, w3, 224
	cmp	w3, 64
	bne	.L995
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3374
	add	x0, x0, 1344
.L999:
	bl	_list_update_data_list
.L994:
	ldp	x29, x30, [sp], 16
	ret
.L995:
	cmp	w3, 96
	bne	.L997
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3376
	add	x0, x0, 1360
	b	.L999
.L997:
	cmp	w3, 160
	bne	.L994
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	x2, x2, 3378
	add	x0, x0, 1368
	b	.L999
	.size	zftl_list_update_data_list, .-zftl_list_update_data_list
	.align	2
	.global	print_list_info
	.type	print_list_info, %function
print_list_info:
	sub	sp, sp, #96
	stp	x29, x30, [sp, 32]
	add	x29, sp, 32
	stp	x19, x20, [sp, 48]
	mov	x19, x0
	stp	x21, x22, [sp, 64]
	stp	x23, x24, [sp, 80]
	ldrh	w2, [x1]
	ldr	x1, [x0]
	adrp	x0, .LC104
	add	x0, x0, :lo12:.LC104
	bl	printk
	ldr	x19, [x19]
	cbz	x19, .L1000
	adrp	x21, .LANCHOR0
	mov	x24, -6148914691236517206
	adrp	x23, .LC105
	adrp	x22, .LANCHOR3
	add	x21, x21, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC105
	add	x22, x22, :lo12:.LANCHOR3
	mov	w20, 0
	movk	x24, 0xaaab, lsl 0
.L1003:
	ldr	x2, [x21, 1040]
	ldr	x1, [x21, 1104]
	sub	x2, x19, x2
	ldr	x8, [x21, 1120]
	asr	x2, x2, 1
	ldrh	w5, [x19, 4]
	ldrh	w4, [x19, 2]
	mul	x2, x2, x24
	and	x7, x2, 65535
	and	w2, w2, 65535
	lsl	x0, x7, 2
	add	x3, x1, x0
	ldrh	w7, [x8, x7, lsl 1]
	ldrb	w6, [x3, 2]
	ldrh	w3, [x19]
	str	w7, [sp, 16]
	ldrh	w7, [x1, x0]
	and	w7, w7, 2047
	str	w7, [sp, 8]
	ubfx	x7, x6, 3, 2
	ubfx	x6, x6, 5, 3
	ldr	w0, [x1, x0]
	mov	w1, w20
	ubfx	x0, x0, 11, 8
	str	w0, [sp]
	mov	x0, x23
	bl	printk
	ldrh	w19, [x19]
	mov	w0, 65535
	cmp	w19, w0
	beq	.L1000
	ldr	x0, [x21, 1040]
	mov	w1, 6
	add	w20, w20, 1
	and	w20, w20, 65535
	umaddl	x19, w19, w1, x0
	ldrh	w0, [x22, 1408]
	cmp	w0, w20
	bcs	.L1003
.L1000:
	ldp	x19, x20, [sp, 48]
	ldp	x21, x22, [sp, 64]
	ldp	x23, x24, [sp, 80]
	ldp	x29, x30, [sp, 32]
	add	sp, sp, 96
	ret
	.size	print_list_info, .-print_list_info
	.align	2
	.global	dump_all_list_info
	.type	dump_all_list_info, %function
dump_all_list_info:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR3
	add	x1, x20, 3368
	add	x0, x19, 1384
	bl	print_list_info
	add	x1, x20, 3370
	add	x0, x19, 1392
	bl	print_list_info
	add	x1, x20, 3372
	add	x0, x19, 1400
	bl	print_list_info
	add	x1, x20, 3374
	add	x0, x19, 1344
	bl	print_list_info
	add	x1, x20, 3376
	add	x0, x19, 1360
	bl	print_list_info
	add	x1, x20, 3378
	add	x0, x19, 1368
	bl	print_list_info
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_all_list_info, .-dump_all_list_info
	.align	2
	.global	ftl_tmp_into_update
	.type	ftl_tmp_into_update, %function
ftl_tmp_into_update:
	adrp	x0, .LANCHOR0+3384
	ldr	x0, [x0, #:lo12:.LANCHOR0+3384]
	ldr	w1, [x0, 16]
	cmp	w1, 2048
	bls	.L1008
	ldr	w2, [x0, 20]
	add	w2, w2, w1, lsr 11
	and	w1, w1, 2047
	stp	w1, w2, [x0, 16]
.L1008:
	ldr	w1, [x0, 24]
	cmp	w1, 2048
	bls	.L1009
	ldr	w2, [x0, 28]
	add	w2, w2, w1, lsr 11
	and	w1, w1, 2047
	stp	w1, w2, [x0, 24]
.L1009:
	ldr	w1, [x0, 32]
	cmp	w1, 1024
	bls	.L1010
	ldr	w2, [x0, 36]
	add	w2, w2, w1, lsr 10
	and	w1, w1, 1023
	stp	w1, w2, [x0, 32]
.L1010:
	ldr	w1, [x0, 40]
	cmp	w1, 1024
	bls	.L1007
	ldr	w2, [x0, 44]
	add	w2, w2, w1, lsr 10
	and	w1, w1, 1023
	stp	w1, w2, [x0, 40]
.L1007:
	ret
	.size	ftl_tmp_into_update, .-ftl_tmp_into_update
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x5, .LANCHOR0
	add	x5, x5, :lo12:.LANCHOR0
	and	w9, w0, 65535
	mov	w4, 0
	mov	w0, 0
	mov	w12, 24
	ldr	x2, [x5, 1104]
	add	x2, x2, w9, uxth 2
	ldrb	w11, [x2, 3]
	adrp	x2, .LANCHOR3
	add	x7, x2, :lo12:.LANCHOR3
	mov	x3, x2
.L1013:
	ldrb	w2, [x7, 1321]
	cmp	w4, w2
	blt	.L1016
	mov	w4, w0
	add	x2, x3, :lo12:.LANCHOR3
	mov	w5, -1
.L1017:
	ldrb	w3, [x2, 1321]
	cmp	w4, w3
	blt	.L1018
	ret
.L1016:
	asr	w2, w11, w4
	tbnz	x2, 0, .L1014
	ldrb	w2, [x7, 1306]
	sbfiz	x10, x0, 1, 32
	ldrb	w6, [x5, 1205]
	ldrh	w13, [x7, 1304]
	sub	w6, w12, w6
	sdiv	w8, w4, w2
	sub	w6, w6, w13
	lsl	w6, w8, w6
	madd	w2, w9, w2, w6
	and	w2, w2, 65535
	strh	w2, [x1, x10]
	ldrb	w6, [x7, 1306]
	cmp	w6, 1
	bls	.L1015
	sub	w6, w6, #1
	and	w6, w6, w4
	add	w2, w2, w6
	strh	w2, [x1, x10]
.L1015:
	add	w0, w0, 1
.L1014:
	add	w4, w4, 1
	b	.L1013
.L1018:
	strh	w5, [x1, w4, sxtw 1]
	add	w4, w4, 1
	b	.L1017
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_erase_phy_blk
	.type	ftl_erase_phy_blk, %function
ftl_erase_phy_blk:
	stp	x29, x30, [sp, -48]!
	mov	w2, 24
	and	w0, w0, 65535
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	adrp	x1, .LANCHOR0+1205
	stp	x19, x20, [sp, 16]
	ldrb	w19, [x1, #:lo12:.LANCHOR0+1205]
	adrp	x1, .LANCHOR3
	add	x3, x1, :lo12:.LANCHOR3
	mov	x20, x1
	sub	w19, w2, w19
	ldrh	w2, [x3, 1304]
	sub	w2, w19, w2
	mov	w19, 1
	lsl	w19, w19, w2
	sub	w19, w19, #1
	and	w19, w19, w0
	asr	w21, w0, w2
	ldrb	w0, [x3, 1322]
	sxth	w19, w19
	cbz	w0, .L1020
	ldrb	w0, [x3, 1323]
	cbnz	w0, .L1020
	ldrh	w2, [x3, 1410]
	cmp	w22, 0
	cset	w1, eq
	mov	w0, w21
	mul	w2, w2, w19
	bl	flash_erase_block_en
.L1020:
	add	x1, x20, :lo12:.LANCHOR3
	mov	w0, w21
	ldrh	w2, [x1, 1410]
	mov	w1, w22
	mul	w2, w2, w19
	bl	flash_erase_block_en
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_erase_phy_blk, .-ftl_erase_phy_blk
	.align	2
	.global	ftl_erase_sblk
	.type	ftl_erase_sblk, %function
ftl_erase_sblk:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	stp	x25, x26, [sp, 64]
	and	w25, w0, 65535
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	ubfiz	x21, x25, 2, 16
	stp	x23, x24, [sp, 48]
	adrp	x26, .LANCHOR3
	stp	x27, x28, [sp, 80]
	mov	x20, x0
	add	x27, x29, 112
	add	x24, x26, :lo12:.LANCHOR3
	ldr	x1, [x1, 1104]
	mov	w19, 0
	add	x1, x1, x21
	ldrb	w28, [x1, 3]
.L1026:
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1412]
	cmp	w19, w1
	bge	.L1037
	ldrb	w1, [x0, 1306]
	mov	w23, 0
	ldrh	w3, [x0, 1410]
	mov	w0, 0
	sub	w4, w1, #1
	mul	w6, w19, w1
	mul	w5, w25, w1
	b	.L1038
.L1028:
	add	w2, w6, w0
	asr	w2, w28, w2
	tbnz	x2, 0, .L1027
	and	w2, w0, w4
	add	w2, w2, w5
	mul	w2, w2, w3
	str	w2, [x27, w23, sxtw 2]
	add	w23, w23, 1
.L1027:
	add	w0, w0, 1
.L1038:
	cmp	w0, w1
	blt	.L1028
	cmp	w1, 4
	bne	.L1029
	mov	x3, 0
.L1030:
	cmp	w23, w3
	bgt	.L1031
.L1032:
	add	w19, w19, 1
	b	.L1026
.L1031:
	ldr	w2, [x27, x3, lsl 2]
	mov	w1, w22
	str	x3, [x29, 104]
	mov	w0, w19
	bl	flash_erase_block_en
	ldr	x3, [x29, 104]
	add	x3, x3, 1
	b	.L1030
.L1029:
	cmp	w23, 2
	bne	.L1033
	ldrb	w0, [x24, 1322]
	cbz	w0, .L1034
	ldrb	w0, [x24, 1323]
	cbnz	w0, .L1034
	ldp	w2, w3, [x29, 112]
	cmp	w22, 0
	cset	w1, eq
	mov	w0, w19
	bl	flash_erase_duplane_block
.L1034:
	ldp	w2, w3, [x29, 112]
	mov	w1, w22
	mov	w0, w19
	bl	flash_erase_duplane_block
	b	.L1032
.L1033:
	cmp	w23, 1
	bne	.L1032
	ldrb	w0, [x24, 1322]
	cbz	w0, .L1036
	ldrb	w0, [x24, 1323]
	cbnz	w0, .L1036
	ldr	w2, [x29, 112]
	cmp	w22, 0
	cset	w1, eq
	mov	w0, w19
	bl	flash_erase_block_en
.L1036:
	ldr	w2, [x29, 112]
	mov	w1, w22
	mov	w0, w19
	bl	flash_erase_block_en
	b	.L1032
.L1037:
	add	x0, x20, :lo12:.LANCHOR0
	cbnz	w22, .L1039
	ldr	x2, [x0, 1104]
	ldrh	w1, [x2, x21]
	add	w3, w1, 1
	bfi	w1, w3, 0, 11
	strh	w1, [x2, x21]
	ldr	x1, [x0, 3384]
	ldr	x0, [x0, 1104]
	ldr	w2, [x1, 84]
	ldrh	w3, [x1, 96]
	add	w2, w2, 1
	str	w2, [x1, 84]
	ldrh	w0, [x0, x21]
	ubfx	x0, x0, 0, 11
	and	w2, w0, 65535
	cmp	w3, w2
	bge	.L1041
	strh	w0, [x1, 96]
.L1041:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 176
	ret
.L1039:
	ldr	x3, [x0, 1104]
	ldr	w1, [x3, x21]
	ubfx	x2, x1, 11, 8
	add	w2, w2, 1
	bfi	w1, w2, 11, 8
	str	w1, [x3, x21]
	ldr	x1, [x0, 3384]
	ldr	x0, [x0, 1104]
	ldr	w2, [x1, 80]
	add	w2, w2, 1
	str	w2, [x1, 80]
	ldrh	w2, [x1, 98]
	ldr	w0, [x0, x21]
	ubfx	x0, x0, 11, 8
	cmp	w2, w0, uxtb
	bcs	.L1041
	strh	w0, [x1, 98]
	b	.L1041
	.size	ftl_erase_sblk, .-ftl_erase_sblk
	.align	2
	.global	ftl_alloc_sys_blk
	.type	ftl_alloc_sys_blk, %function
ftl_alloc_sys_blk:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	ldr	x1, [x1, 3384]
	ldrh	w2, [x1, 136]
	cmp	w2, 63
	bls	.L1051
	strh	wzr, [x1, 136]
.L1051:
	ldrh	w0, [x1, 112]
	cbnz	w0, .L1052
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1592
	mov	w2, 1359
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1052:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w4, 65535
	ldr	x1, [x0, 3384]
.L1056:
	ldrh	w2, [x1, 136]
	ubfiz	x3, x2, 1, 16
	add	x3, x3, 160
	add	x3, x1, x3
.L1053:
	cmp	w2, 63
	ble	.L1055
	strh	wzr, [x1, 136]
	b	.L1056
.L1055:
	ldrh	w0, [x3], 2
	cmp	w0, w4
	bne	.L1058
	add	w2, w2, 1
	b	.L1053
.L1058:
	add	x3, x1, w2, sxtw 1
	mov	w4, -1
	strh	w4, [x3, 160]
	strh	w2, [x1, 136]
	ldrh	w2, [x1, 112]
	sub	w2, w2, #1
	strh	w2, [x1, 112]
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_alloc_sys_blk, .-ftl_alloc_sys_blk
	.align	2
	.global	ftl_free_sys_blk
	.type	ftl_free_sys_blk, %function
ftl_free_sys_blk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x19, x0
	ldr	x1, [x1, 3384]
	ldrh	w2, [x1, 138]
	cmp	w2, 63
	bls	.L1061
	strh	wzr, [x1, 138]
.L1061:
	ldrh	w0, [x1, 112]
	cmp	w0, 63
	bls	.L1062
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1616
	mov	w2, 1386
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1062:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldr	x0, [x0, 3384]
.L1066:
	ldrh	w1, [x0, 138]
	ubfiz	x2, x1, 1, 16
	add	x2, x2, 160
	add	x2, x0, x2
.L1063:
	cmp	w1, 63
	ble	.L1065
	strh	wzr, [x0, 138]
	b	.L1066
.L1065:
	ldrh	w4, [x2], 2
	cmp	w4, w3
	bne	.L1064
	add	x2, x0, w1, sxtw 1
	strh	w20, [x2, 160]
	strh	w1, [x0, 138]
	ldrh	w1, [x0, 112]
	add	w1, w1, 1
	strh	w1, [x0, 112]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1064:
	add	w1, w1, 1
	b	.L1063
	.size	ftl_free_sys_blk, .-ftl_free_sys_blk
	.align	2
	.global	ftl_info_data_recovery
	.type	ftl_info_data_recovery, %function
ftl_info_data_recovery:
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1079
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	str	x23, [sp, 48]
	add	x23, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ubfiz	x20, x2, 2, 16
	ldr	x21, [x23, 1104]
	add	x22, x21, x20
	ldrb	w1, [x22, 2]
	tst	w1, 224
	bne	.L1069
	ldrb	w0, [x0, 4]
	bfi	w1, w0, 5, 3
	strb	w1, [x22, 2]
	mov	w0, w2
	bl	zftl_remove_free_node
	ldrb	w0, [x22, 2]
	ldr	x1, [x23, 3384]
	tbz	x0, 3, .L1073
	ldrh	w0, [x1, 116]
	sub	w0, w0, #1
	strh	w0, [x1, 116]
.L1074:
	ldrb	w0, [x22, 2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L1076
	ldr	w0, [x21, x20]
	add	x19, x19, :lo12:.LANCHOR0
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x21, x20]
	ldr	x1, [x19, 3384]
	ldrh	w0, [x1, 120]
	sub	w0, w0, #1
	strh	w0, [x1, 120]
.L1069:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1073:
	tst	w0, 24
	bne	.L1075
	ldrh	w0, [x1, 114]
	sub	w0, w0, #1
	strh	w0, [x1, 114]
	b	.L1074
.L1075:
	ldrh	w0, [x1, 118]
	sub	w0, w0, #1
	strh	w0, [x1, 118]
	b	.L1074
.L1076:
	ldrh	w1, [x21, x20]
	cmp	w0, 64
	add	w2, w1, 1
	bfi	w1, w2, 0, 11
	strh	w1, [x21, x20]
	bne	.L1077
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19, 3384]
	ldrh	w0, [x1, 122]
	sub	w0, w0, #1
	strh	w0, [x1, 122]
	b	.L1069
.L1077:
	cmp	w0, 96
	bne	.L1069
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19, 3384]
	ldrh	w0, [x1, 124]
	sub	w0, w0, #1
	strh	w0, [x1, 124]
	b	.L1069
.L1079:
	ret
	.size	ftl_info_data_recovery, .-ftl_info_data_recovery
	.align	2
	.global	ftl_get_ppa_from_index
	.type	ftl_get_ppa_from_index, %function
ftl_get_ppa_from_index:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0+1128
	and	w0, w0, 65535
	add	x29, sp, 0
	ldr	x2, [x1, #:lo12:.LANCHOR0+1128]
	adrp	x1, .LANCHOR3
	add	x4, x1, :lo12:.LANCHOR3
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	mov	x19, x1
	ldrh	w3, [x4, 1376]
	ldrb	w4, [x4, 1321]
	mul	w3, w3, w4
	cmp	w0, w3
	bge	.L1083
	add	x2, x2, 16
.L1084:
	ldrb	w1, [x2, 9]
	sdiv	w20, w0, w1
	msub	w0, w20, w1, w0
	add	x0, x2, w0, uxth 1
	ldrh	w21, [x0, 16]
	mov	w0, 65535
	cmp	w21, w0
	bne	.L1085
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1640
	mov	w2, 1945
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1085:
	add	x1, x19, :lo12:.LANCHOR3
	ldrh	w0, [x1, 1410]
	madd	w0, w0, w21, w20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L1083:
	sub	w0, w0, w3
	add	x2, x2, 48
	and	w0, w0, 65535
	b	.L1084
	.size	ftl_get_ppa_from_index, .-ftl_get_ppa_from_index
	.align	2
	.global	lpa_hash_get_ppa
	.type	lpa_hash_get_ppa, %function
lpa_hash_get_ppa:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	and	x3, x0, 255
	add	x2, x1, 1416
	ldr	x4, [x1, 1928]
	ldr	x5, [x1, 1936]
	ldrh	w2, [x2, x3, lsl 1]
	mov	w3, 65535
.L1088:
	cmp	w2, w3
	bne	.L1091
	mov	w0, -1
	ret
.L1091:
	uxtw	x1, w2
	ldr	w6, [x4, x1, lsl 2]
	cmp	w0, w6
	bne	.L1089
	stp	x29, x30, [sp, -16]!
	mov	w0, w2
	add	x29, sp, 0
	bl	ftl_get_ppa_from_index
	ldp	x29, x30, [sp], 16
	ret
.L1089:
	ldrh	w2, [x5, x1, lsl 1]
	b	.L1088
	.size	lpa_hash_get_ppa, .-lpa_hash_get_ppa
	.align	2
	.global	ftl_get_new_free_page
	.type	ftl_get_new_free_page, %function
ftl_get_new_free_page:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	ldrh	w1, [x0]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L1097
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1664
	mov	w2, 2088
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1097:
	adrp	x20, .LANCHOR3
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w1, [x19, 2]
	ldrh	w0, [x0, 1376]
	cmp	w1, w0
	bne	.L1098
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1664
	mov	w2, 2089
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1098:
	ldrh	w0, [x19, 6]
	cbnz	w0, .L1099
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1664
	mov	w2, 2090
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1099:
	ldrb	w0, [x19, 5]
	mov	w2, 65535
	add	x0, x0, 8
	ldrh	w3, [x19, x0, lsl 1]
	add	x0, x20, :lo12:.LANCHOR3
	ldrb	w4, [x0, 1321]
.L1100:
	cmp	w3, w2
	ldrb	w1, [x19, 5]
	beq	.L1102
	add	x20, x20, :lo12:.LANCHOR3
	add	w1, w1, 1
	and	w1, w1, 255
	ldrh	w2, [x19, 2]
	strb	w1, [x19, 5]
	cmp	w4, w1
	ldrh	w0, [x20, 1410]
	mul	w0, w0, w3
	ldrh	w3, [x19, 6]
	sub	w3, w3, #1
	strh	w3, [x19, 6]
	ldrh	w3, [x19, 10]
	orr	w0, w0, w2
	add	w3, w3, 1
	strh	w3, [x19, 10]
	bne	.L1096
	add	w2, w2, 1
	strb	wzr, [x19, 5]
	strh	w2, [x19, 2]
.L1096:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1102:
	add	w1, w1, 1
	and	w1, w1, 255
	strb	w1, [x19, 5]
	cmp	w1, w4
	bne	.L1101
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 5]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L1101:
	ldrb	w0, [x19, 5]
	add	x0, x0, 8
	ldrh	w3, [x19, x0, lsl 1]
	b	.L1100
	.size	ftl_get_new_free_page, .-ftl_get_new_free_page
	.align	2
	.global	ftl_ext_alloc_new_blk
	.type	ftl_ext_alloc_new_blk, %function
ftl_ext_alloc_new_blk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	bl	ftl_alloc_sys_blk
	and	w0, w0, 65535
	mov	w1, 65533
	mov	w20, w0
	sub	w0, w0, #1
	cmp	w1, w0, uxth
	bcs	.L1106
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1688
	mov	w2, 2125
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1106:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w0, w20
	bl	ftl_erase_phy_blk
	ldr	x0, [x19, 3384]
	ldrh	w0, [x0, 130]
	bl	ftl_free_sys_blk
	ldr	x0, [x19, 3384]
	strh	w20, [x0, 130]
	strh	wzr, [x0, 140]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_ext_alloc_new_blk, .-ftl_ext_alloc_new_blk
	.align	2
	.global	ftl_total_vpn_update
	.type	ftl_total_vpn_update, %function
ftl_total_vpn_update:
	adrp	x1, .LANCHOR3
	add	x3, x1, :lo12:.LANCHOR3
	ldrh	w2, [x3, 1944]
	cmp	w2, 4
	bhi	.L1109
	cbnz	w0, .L1109
	add	w2, w2, 1
	strh	w2, [x3, 1944]
	ret
.L1109:
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x0, .LANCHOR0
	mov	x2, 0
	mov	w3, 0
	mov	w9, 65535
	strh	wzr, [x1, 1944]
	add	x1, x0, :lo12:.LANCHOR0
	ldrh	w6, [x1, 1096]
	ldr	x8, [x1, 1104]
	ldr	x7, [x1, 1120]
	mov	w1, 0
.L1111:
	cmp	w6, w2, uxth
	bhi	.L1114
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x2, [x0, 1128]
	ldr	x0, [x0, 3384]
	str	w3, [x2, 524]
	str	w1, [x2, 528]
	ldrh	w0, [x0, 120]
	cbz	w0, .L1108
	udiv	w1, w1, w0
	str	w1, [x2, 532]
.L1108:
	ret
.L1114:
	ldrh	w4, [x7, x2, lsl 1]
	cmp	w4, w9
	beq	.L1112
	add	x5, x8, x2, lsl 2
	ldrb	w5, [x5, 2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L1113
	add	w1, w1, w4
.L1112:
	add	x2, x2, 1
	b	.L1111
.L1113:
	add	w3, w3, w4
	b	.L1112
	.size	ftl_total_vpn_update, .-ftl_total_vpn_update
	.align	2
	.global	ftl_debug_info_fill
	.type	ftl_debug_info_fill, %function
ftl_debug_info_fill:
	adrp	x3, .LANCHOR3+1946
	ldrb	w3, [x3, #:lo12:.LANCHOR3+1946]
	cmp	w3, 8
	bls	.L1123
	and	w0, w0, 255
	cmp	x2, 0
	ccmp	w0, 2, 0, ne
	bne	.L1121
	stp	x29, x30, [sp, -32]!
	mov	x0, x2
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x1
	mov	w1, 21320
	movk	w1, 0x4841, lsl 16
	str	w1, [x19]
	mov	w1, 1024
	bl	js_hash
	str	w0, [x19, 4]
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1121:
	stp	wzr, wzr, [x1]
.L1123:
	mov	w0, 0
	ret
	.size	ftl_debug_info_fill, .-ftl_debug_info_fill
	.align	2
	.global	ftl_vpn_update
	.type	ftl_vpn_update, %function
ftl_vpn_update:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	and	w19, w0, 65535
	mov	w0, w19
	bl	zftl_list_update_data_list
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ubfiz	x19, x19, 1, 16
	ldr	x0, [x1, 1120]
	ldrh	w0, [x0, x19]
	cbnz	w0, .L1128
	mov	w0, 1
	str	w0, [x1, 3396]
.L1126:
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1128:
	mov	w0, 0
	b	.L1126
	.size	ftl_vpn_update, .-ftl_vpn_update
	.align	2
	.global	ftl_vpn_decrement
	.type	ftl_vpn_decrement, %function
ftl_vpn_decrement:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	beq	.L1131
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	uxtw	x1, w20
	lsl	x3, x1, 1
	ldr	x4, [x2, 1120]
	ldrh	w0, [x4, x3]
	cbnz	w0, .L1132
	ldr	x0, [x2, 1104]
	mov	w2, 0
	add	x1, x0, x1, lsl 2
	adrp	x0, .LC106
	add	x0, x0, :lo12:.LC106
	ldrb	w3, [x1, 2]
	mov	w1, w20
	ubfx	x3, x3, 5, 3
	bl	printk
.L1137:
	mov	w0, 0
	b	.L1130
.L1132:
	sub	w0, w0, #1
	strh	w0, [x4, x3]
.L1131:
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	ldrh	w0, [x19, 1280]
	cmp	w20, w0
	beq	.L1137
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1134
	strh	w20, [x19, 1280]
	b	.L1137
.L1134:
	bl	ftl_vpn_update
	cmp	w0, 0
	ldrh	w2, [x19, 1948]
	cset	w0, ne
	add	x1, x19, 1288
	ldrh	w3, [x19, 1280]
	add	w2, w2, 1
	strh	w20, [x19, 1280]
	and	w2, w2, 65535
	cmp	w2, 7
	csel	w2, w2, wzr, ls
	strh	w2, [x19, 1948]
	and	x2, x2, 65535
	strh	w3, [x1, x2, lsl 1]
.L1130:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_vpn_decrement, .-ftl_vpn_decrement
	.align	2
	.global	lpa_hash_update_ppa
	.type	lpa_hash_update_ppa, %function
lpa_hash_update_ppa:
	adrp	x3, .LANCHOR3
	add	x6, x3, :lo12:.LANCHOR3
	add	x5, x6, 1416
	and	x4, x0, 255
	and	w2, w2, 65535
	ldr	x10, [x6, 1928]
	ldrh	w5, [x5, x4, lsl 1]
	ldr	x11, [x6, 1936]
	mov	w6, 65535
	mov	w9, w6
.L1140:
	cmp	w5, w9
	beq	.L1144
	uxtw	x8, w5
	lsl	x7, x8, 2
	add	x12, x10, x7
	ldr	w7, [x10, x7]
	cmp	w0, w7
	lsl	x7, x8, 1
	bne	.L1141
	mov	w5, -1
	str	w5, [x12]
	cmp	w6, w9
	add	x5, x3, :lo12:.LANCHOR3
	bne	.L1142
	ldr	x6, [x5, 1936]
	add	x5, x5, 1416
	ldrh	w6, [x6, x7]
	strh	w6, [x5, x4, lsl 1]
.L1143:
	add	x5, x3, :lo12:.LANCHOR3
	mov	w6, -1
	ldr	x5, [x5, 1936]
	strh	w6, [x5, x8, lsl 1]
.L1144:
	add	x3, x3, :lo12:.LANCHOR3
	uxtw	x5, w2
	cmn	w1, #1
	ldr	x6, [x3, 1928]
	str	w0, [x6, x5, lsl 2]
	add	x0, x3, 1416
	ldrh	w6, [x0, x4, lsl 1]
	strh	w2, [x0, x4, lsl 1]
	ldr	x0, [x3, 1936]
	strh	w6, [x0, x5, lsl 1]
	beq	.L1151
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR0+1205
	add	x29, sp, 0
	ldrb	w2, [x0, #:lo12:.LANCHOR0+1205]
	mov	w0, 24
	ldrh	w4, [x3, 1304]
	sub	w0, w0, w2
	sub	w2, w0, w4
	mov	w0, 1
	lsr	w1, w1, w4
	lsl	w0, w0, w2
	sub	w0, w0, #1
	and	w1, w0, w1
	ldrb	w0, [x3, 1306]
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	mov	w0, -1
	ldp	x29, x30, [sp], 16
	ret
.L1142:
	ldr	x5, [x5, 1936]
	ldrh	w7, [x5, x7]
	strh	w7, [x5, w6, uxtw 1]
	b	.L1143
.L1141:
	mov	w6, w5
	ldrh	w5, [x11, x7]
	b	.L1140
.L1151:
	mov	w0, -1
	ret
	.size	lpa_hash_update_ppa, .-lpa_hash_update_ppa
	.align	2
	.global	ftl_mask_bad_block
	.type	ftl_mask_bad_block, %function
ftl_mask_bad_block:
	stp	x29, x30, [sp, -48]!
	adrp	x6, .LANCHOR0
	add	x1, x6, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR3
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x3, x3, :lo12:.LANCHOR3
	str	x21, [sp, 32]
	mov	w7, 24
	mov	w4, 1
	mov	x19, x6
	ldrb	w1, [x1, 1205]
	ldrb	w5, [x3, 1306]
	ldrh	w3, [x3, 1304]
	sub	w7, w7, w1
	lsl	w1, w4, w1
	sub	w1, w1, #1
	lsr	w2, w0, w7
	and	w1, w1, w2
	lsr	w0, w0, w3
	sub	w3, w7, w3
	and	w1, w1, 255
	cmp	w5, 1
	lsl	w3, w4, w3
	sub	w3, w3, #1
	and	w3, w3, w0
	and	w4, w3, 65535
	mul	w2, w1, w5
	udiv	w3, w3, w5
	and	w20, w2, 255
	and	w21, w3, 65535
	bls	.L1155
	sub	w2, w5, #1
	and	w2, w2, w4
	add	w2, w20, w2
	and	w20, w2, 255
.L1155:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 14, .L1156
	adrp	x0, .LC107
	and	w3, w3, 65535
	mov	w2, w20
	add	x0, x0, :lo12:.LC107
	bl	printk
.L1156:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 1096]
	cmp	w1, w21
	bls	.L1154
	ldr	x0, [x0, 1104]
	mov	w2, 1
	lsl	w2, w2, w20
	add	x21, x0, w21, uxth 2
	ldrb	w20, [x21, 3]
	orr	w2, w2, w20
	strb	w2, [x21, 3]
.L1154:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_mask_bad_block, .-ftl_mask_bad_block
	.align	2
	.global	gc_free_bad_sblk
	.type	gc_free_bad_sblk, %function
gc_free_bad_sblk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x24, .LANCHOR0
	add	x24, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 65535
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	ldr	w0, [x24, 5616]
	str	x27, [sp, 80]
	cbz	w0, .L1177
	mov	w23, 20041
	add	x27, x24, 3416
	mov	w26, 0
	movk	w23, 0x444b, lsl 16
.L1164:
	adrp	x21, .LANCHOR3
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w0, [x2, 1321]
	cmp	w0, w26
	bhi	.L1174
.L1177:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L1174:
	ldrb	w3, [x2, 1306]
	mov	w1, 24
	ldrb	w4, [x24, 1205]
	ldrh	w2, [x2, 1304]
	and	w0, w3, 65535
	sub	w1, w1, w4
	cmp	w3, 1
	sdiv	w19, w26, w3
	sub	w1, w1, w2
	lsl	w19, w19, w1
	madd	w19, w22, w0, w19
	and	w19, w19, 65535
	bls	.L1165
	sub	w0, w0, #1
	and	w0, w0, w26
	add	w19, w19, w0
	and	w19, w19, 65535
.L1165:
	adrp	x25, .LC108
	add	x25, x25, :lo12:.LC108
	mov	w20, 0
.L1166:
	ldr	w0, [x27, 2200]
	cmp	w20, w0
	bcc	.L1173
	add	w26, w26, 1
	and	w26, w26, 65535
	b	.L1164
.L1173:
	add	x0, x27, w20, sxtw 1
	ldrh	w0, [x0, 2204]
	cmp	w0, w19
	bne	.L1167
	mov	w1, w19
	mov	x0, x25
	bl	printk
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1950]
	cbnz	w1, .L1168
	ldrb	w0, [x0, 1322]
	cbz	w0, .L1169
.L1168:
	ldr	x0, [x24, 3384]
	ldr	w0, [x0, 156]
	cmp	w0, w23
	beq	.L1170
.L1169:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1304]
	lsl	w0, w19, w0
	bl	ftl_mask_bad_block
.L1170:
	ldr	w1, [x27, 2200]
	mov	w0, w20
.L1171:
	cmp	w0, w1
	bcc	.L1172
	sub	w1, w1, #1
	str	w1, [x27, 2200]
.L1167:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1166
.L1172:
	add	w2, w0, 1
	add	x0, x27, w0, sxtw 1
	add	x3, x27, w2, sxtw 1
	ldrh	w3, [x3, 2204]
	strh	w3, [x0, 2204]
	and	w0, w2, 65535
	b	.L1171
	.size	gc_free_bad_sblk, .-gc_free_bad_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x3, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	and	x24, x0, 65535
	lsl	x19, x24, 2
	mov	x22, x24
	ldr	x4, [x3, 1104]
	add	x21, x4, x19
	ldrb	w0, [x21, 2]
	ubfx	x23, x0, 5, 3
	tbz	x0, 3, .L1184
	ldr	x5, [x3, 3384]
	ldr	w2, [x4, x19]
	ldrh	w1, [x4, x19]
	ldrh	w6, [x5, 74]
	and	w1, w1, 2047
	ubfx	x7, x2, 11, 8
	ldrh	w2, [x3, 1112]
	add	w6, w1, w6
	and	w8, w6, 65535
	ldrh	w6, [x5, 72]
	adrp	x5, .LANCHOR3
	add	x5, x5, :lo12:.LANCHOR3
	add	w6, w7, w6
	udiv	w9, w8, w2
	and	w6, w6, 65535
	ldrh	w10, [x5, 1952]
	add	w9, w9, w6
	cmp	w9, w10
	ble	.L1185
	ldrh	w6, [x3, 3370]
	ldrh	w8, [x3, 3378]
	add	w8, w8, w6
	ldrh	w6, [x5, 1382]
	add	w6, w6, 8
	cmp	w8, w6
	bge	.L1186
.L1191:
	mov	w3, 2
	bfi	w0, w3, 3, 2
	b	.L1203
.L1186:
	ldrh	w6, [x3, 3374]
	ldrh	w8, [x3, 3368]
	ldrh	w5, [x5, 1380]
	ldrh	w3, [x3, 3376]
	add	w6, w6, w8
	add	w5, w5, 8
	add	w3, w6, w3
	cmp	w3, w5
.L1207:
	bge	.L1191
	b	.L1205
.L1185:
	madd	w6, w2, w6, w8
	ldrh	w8, [x5, 1954]
	cmp	w6, w8
	ble	.L1187
	ldrh	w8, [x3, 3368]
	ldrh	w6, [x3, 3374]
	add	w6, w6, w8
	ldrh	w8, [x3, 3376]
	add	w6, w6, w8
	ldrh	w8, [x5, 1380]
	add	w9, w8, 8
	cmp	w6, w9
	bge	.L1189
.L1205:
	and	w0, w0, -25
.L1203:
	strb	w0, [x21, 2]
.L1187:
	ldrb	w0, [x21, 2]
	ands	w0, w0, 24
	bne	.L1192
	mul	w2, w7, w2
	ldrh	w0, [x4, x19]
	add	w2, w2, w2, lsl 1
	add	w1, w1, w2, lsr 2
	lsr	w2, w7, 3
	bfi	w0, w1, 0, 11
	strh	w0, [x4, x19]
	ldr	w0, [x4, x19]
	bfi	w0, w2, 11, 8
	str	w0, [x4, x19]
.L1193:
	mov	w0, w22
	bl	zftl_remove_data_node
	add	x1, x20, :lo12:.LANCHOR0
	add	w23, w23, 6
	and	w23, w23, 7
	cmp	w23, 4
	ldr	x0, [x1, 1104]
	add	x19, x0, x19
	ldrb	w0, [x19, 2]
	and	w0, w0, 31
	strb	w0, [x19, 2]
	ldr	x0, [x1, 1120]
	strh	wzr, [x0, x24, lsl 1]
	bhi	.L1196
	mov	w0, w22
	bl	gc_free_bad_sblk
.L1196:
	ldrb	w0, [x21, 2]
	tbz	x0, 3, .L1197
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20, 1128]
	ldrh	w1, [x0, 586]
	cmp	w1, w22
	bne	.L1197
	mov	w1, -1
	ldrh	w2, [x0, 590]
	strh	w1, [x0, 586]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L1197
	strh	w22, [x0, 590]
	mov	w1, w22
	adrp	x0, .LC109
	add	x0, x0, :lo12:.LC109
	bl	printk
.L1183:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L1189:
	ldrh	w9, [x3, 3378]
	ldrh	w3, [x3, 3370]
	add	w9, w9, w3
	ldrh	w3, [x5, 1382]
	add	w3, w3, 8
	cmp	w9, w3
	blt	.L1191
	add	w8, w8, 24
	cmp	w6, w8
	b	.L1207
.L1192:
	cmp	w0, 16
	bne	.L1193
	sdiv	w2, w1, w2
	ldr	w0, [x4, x19]
	add	w2, w2, w2, lsl 1
	add	w2, w7, w2, lsr 2
.L1204:
	bfi	w0, w2, 11, 8
	asr	w1, w1, 5
	str	w0, [x4, x19]
	bfi	w0, w1, 0, 11
	strh	w0, [x4, x19]
	b	.L1193
.L1184:
	tst	w0, 24
	bne	.L1193
	ldrh	w1, [x3, 3372]
	cbnz	w1, .L1193
	ldrh	w7, [x3, 3370]
	cmp	w7, 15
	bhi	.L1193
	ldrh	w2, [x3, 3368]
	ldrh	w1, [x3, 3374]
	add	w1, w1, w2
	ldrh	w2, [x3, 3376]
	add	w1, w1, w2
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldrh	w6, [x2, 1380]
	add	w6, w6, 16
	cmp	w1, w6
	ble	.L1193
	ldrh	w5, [x3, 3378]
	ldrh	w1, [x2, 1382]
	add	w5, w5, w7
	add	w1, w1, 8
	cmp	w5, w1
	bge	.L1193
	mov	w1, 2
	bfi	w0, w1, 3, 2
	strb	w0, [x21, 2]
	ldrh	w0, [x3, 1112]
	ldrh	w1, [x4, x19]
	and	w1, w1, 2047
	sdiv	w0, w1, w0
	add	w2, w0, w0, lsl 1
	ldr	w0, [x4, x19]
	ubfx	x3, x0, 11, 8
	add	w2, w3, w2, lsr 2
	b	.L1204
.L1197:
	mov	w0, w22
	bl	zftl_insert_free_list
	b	.L1183
	.size	ftl_free_sblk, .-ftl_free_sblk
	.align	2
	.global	gc_free_src_blk
	.type	gc_free_src_blk, %function
gc_free_src_blk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x24, x22, :lo12:.LANCHOR0
	adrp	x23, .LC110
	add	x21, x24, 3416
	add	x23, x23, :lo12:.LC110
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	mov	w20, 0
.L1209:
	ldrh	w0, [x21, 56]
	cmp	w0, w20
	bhi	.L1224
	strh	wzr, [x21, 56]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1224:
	add	x0, x21, w20, sxtw 1
	ldrh	w25, [x0, 58]
	ldr	x0, [x24, 1120]
	mov	x19, x25
	lsl	x26, x25, 1
	ldrh	w2, [x0, x26]
	cbz	w2, .L1210
	mov	w1, w25
	mov	x0, x23
	bl	printk
.L1210:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0, 1120]
	strh	wzr, [x1, x26]
	ldr	x1, [x0, 1120]
	ldrh	w1, [x1, x26]
	cbnz	w1, .L1211
	ldr	x0, [x0, 1104]
	add	x25, x0, x25, lsl 2
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L1212
	ldrb	w2, [x25, 2]
	adrp	x0, .LC111
	mov	w1, w19
	add	x0, x0, :lo12:.LC111
	ubfx	x2, x2, 5, 3
	bl	printk
.L1212:
	ldrb	w0, [x25, 2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L1213
	tst	w0, 192
	bne	.L1214
.L1213:
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1712
	mov	w2, 1363
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1214:
	mov	w0, w19
	bl	ftl_free_sblk
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0, 1128]
	ldrh	w2, [x0, 124]
	cbz	w2, .L1215
	add	x3, x0, 392
	mov	w1, 0
.L1217:
	ldrh	w4, [x3]
	cmp	w4, w19
	bne	.L1216
	add	x1, x0, w1, sxtw 1
	mov	w3, -1
	sub	w2, w2, #1
	strh	w3, [x1, 392]
	strh	w2, [x0, 124]
.L1215:
	ldrh	w2, [x0, 120]
	cbz	w2, .L1218
	add	x3, x0, 136
	mov	w1, 0
.L1220:
	ldrh	w4, [x3]
	cmp	w4, w19
	bne	.L1219
	add	x1, x0, w1, sxtw 1
	mov	w3, -1
	sub	w2, w2, #1
	strh	w3, [x1, 136]
	strh	w2, [x0, 120]
.L1218:
	ldrh	w2, [x0, 122]
	cbz	w2, .L1221
	add	x3, x0, 264
	mov	w1, 0
.L1223:
	ldrh	w4, [x3]
	cmp	w4, w19
	bne	.L1222
	add	x1, x0, w1, sxtw 1
	mov	w3, -1
	sub	w2, w2, #1
	strh	w3, [x1, 264]
	strh	w2, [x0, 122]
.L1221:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L1209
.L1216:
	add	w1, w1, 1
	add	x3, x3, 2
	cmp	w1, 64
	bne	.L1217
	b	.L1215
.L1219:
	add	w1, w1, 1
	add	x3, x3, 2
	cmp	w1, 64
	bne	.L1220
	b	.L1218
.L1222:
	add	w1, w1, 1
	add	x3, x3, 2
	cmp	w1, 64
	bne	.L1223
	b	.L1221
.L1211:
	mov	w2, 0
	mov	w1, 1
	mov	w0, w19
	bl	gc_add_sblk
	b	.L1221
	.size	gc_free_src_blk, .-gc_free_src_blk
	.align	2
	.global	print_ftl_debug_info
	.type	print_ftl_debug_info, %function
print_ftl_debug_info:
	sub	sp, sp, #64
	stp	x29, x30, [sp, 32]
	add	x29, sp, 32
	str	x19, [sp, 48]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19, 3384]
	ldr	x0, [x19, 1128]
	ldr	w8, [x19, 3364]
	ldrh	w7, [x19, 3376]
	ldrh	w2, [x1, 146]
	ldrh	w1, [x1, 148]
	str	w8, [sp, 24]
	ldrh	w6, [x19, 3374]
	ldr	w8, [x0, 528]
	str	w8, [sp, 16]
	ldrh	w5, [x19, 3372]
	ldr	w0, [x0, 524]
	ldrh	w4, [x19, 3370]
	ldrh	w3, [x19, 3368]
	str	w0, [sp, 8]
	ldrh	w0, [x19, 3378]
	str	w0, [sp]
	adrp	x0, .LC112
	add	x0, x0, :lo12:.LC112
	bl	printk
	ldr	x0, [x19, 3384]
	ldrb	w3, [x19, 3380]
	ldr	w4, [x0, 8]
	ldr	w5, [x0, 64]
	ldr	w1, [x0, 20]
	ldr	w2, [x0, 28]
	adrp	x0, .LC113
	add	x0, x0, :lo12:.LC113
	bl	printk
	ldr	x0, [x19, 3384]
	ldr	w1, [x0, 52]
	ldr	w2, [x0, 60]
	ldr	w3, [x0, 16]
	adrp	x0, .LC114
	add	x0, x0, :lo12:.LC114
	lsr	w3, w3, 11
	bl	printk
	ldr	x0, [x19, 3384]
	ldrh	w2, [x0, 98]
	ldrh	w6, [x0, 72]
	ldrh	w5, [x0, 96]
	ldrh	w4, [x0, 92]
	ldrh	w3, [x0, 88]
	ldrh	w1, [x0, 74]
	str	w2, [sp, 16]
	ldrh	w2, [x0, 94]
	str	w2, [sp, 8]
	ldrh	w2, [x0, 90]
	str	w2, [sp]
	ldp	w7, w2, [x0, 80]
	adrp	x0, .LC115
	add	x0, x0, :lo12:.LC115
	bl	printk
	ldrh	w5, [x19, 3392]
	adrp	x0, .LC116
	ldrh	w4, [x19, 3400]
	add	x0, x0, :lo12:.LC116
	ldrh	w3, [x19, 3402]
	ldrh	w2, [x19, 3404]
	ldrh	w1, [x19, 3406]
	bl	printk
	ldr	x0, [x19, 1128]
	ldr	x1, [x19, 3384]
	ldrh	w4, [x0, 590]
	ldrh	w6, [x1, 150]
	ldrh	w3, [x0, 588]
	ldrh	w2, [x0, 586]
	ldr	w5, [x1, 156]
	ldrh	w1, [x0, 584]
	adrp	x0, .LC117
	add	x0, x0, :lo12:.LC117
	bl	printk
	ldr	x19, [sp, 48]
	ldp	x29, x30, [sp, 32]
	add	sp, sp, 64
	ret
	.size	print_ftl_debug_info, .-print_ftl_debug_info
	.align	2
	.global	ftl_write_buf
	.type	ftl_write_buf, %function
ftl_write_buf:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	cbnz	x0, .L1250
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1728
	mov	w2, 811
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
	mov	w0, -1
.L1249:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1253:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x1, x20
	add	x0, x19, 3408
	bl	buf_add_tail
	ldrb	w3, [x20, 56]
	ldr	x1, [x19, 3384]
	ldrb	w0, [x19, 3381]
	add	w0, w0, 1
	ldr	w2, [x1, 16]
	and	w0, w0, 255
	strb	w0, [x19, 3381]
	add	w2, w2, w3
	str	w2, [x1, 16]
	ldr	w2, [x1, 32]
	add	w2, w2, 1
	str	w2, [x1, 32]
	b	.L1249
.L1250:
	mov	x20, x0
	adrp	x0, .LANCHOR3
	add	x1, x0, :lo12:.LANCHOR3
	mov	x21, x0
	ldrb	w2, [x20, 56]
	ldrb	w1, [x1, 1946]
	cmp	w2, w1
	bls	.L1255
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1728
	mov	w2, 818
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1255:
	ldrb	w1, [x20, 56]
	adrp	x19, .LANCHOR0
	cbz	w1, .L1252
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w0, [x0, 1946]
	cmp	w0, w1
	bcs	.L1253
.L1252:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	zbuf_free
	ldrb	w0, [x19, 3381]
	b	.L1249
	.size	ftl_write_buf, .-ftl_write_buf
	.align	2
	.global	ftl_write_completed
	.type	ftl_write_completed, %function
ftl_write_completed:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x19, x20, [sp, 16]
	add	x19, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	add	x22, x19, 1304
	mov	w23, 20041
	str	x25, [sp, 64]
	movk	w23, 0x444b, lsl 16
	mov	w25, 0
.L1261:
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w0, [x1, 3357]
	cmp	w0, 255
	bne	.L1272
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L1272:
	add	x2, x1, 1304
	lsl	x3, x0, 6
	add	x24, x2, x3
	ubfiz	x20, x0, 6, 8
	add	x20, x2, x20
	ldrb	w2, [x2, x3]
	strb	w2, [x1, 3357]
	ldr	w1, [x24, 52]
	cmn	w1, #1
	bne	.L1262
	adrp	x2, .LANCHOR3
	add	x1, x2, :lo12:.LANCHOR3
	ldrb	w3, [x1, 1950]
	cbnz	w3, .L1263
	ldrb	w1, [x1, 1322]
	cbz	w1, .L1264
.L1263:
	ldr	x1, [x19, 3384]
	ldr	w1, [x1, 156]
	cmp	w1, w23
	beq	.L1265
.L1264:
	add	x2, x2, :lo12:.LANCHOR3
	ldrb	w3, [x19, 1205]
	mov	w1, 24
	add	x4, x22, x0, lsl 6
	sub	w1, w1, w3
	ldrh	w5, [x2, 1304]
	ldrb	w2, [x2, 1306]
	sub	w3, w1, w5
	mov	w1, 1
	lsl	w1, w1, w3
	ldr	w3, [x4, 40]
	sub	w1, w1, #1
	lsr	w3, w3, w5
	and	w1, w1, w3
	mov	w3, 65535
	udiv	w1, w1, w2
	ldr	x2, [x19, 1128]
	ldr	w5, [x2, 560]
	and	w24, w1, 65535
	and	w1, w1, w3
	cmp	w5, w3
	bne	.L1266
	str	w1, [x2, 560]
	ldr	w3, [x4, 40]
	str	w3, [x2, 564]
.L1266:
	add	x0, x22, x0, lsl 6
	ldp	w2, w3, [x0, 36]
	adrp	x0, .LC118
	add	x0, x0, :lo12:.LC118
	bl	printk
	ldr	x0, [x19, 1128]
	ldr	w1, [x0, 556]
	add	w1, w1, 1
	str	w1, [x0, 556]
	ldrh	w1, [x0, 16]
	cmp	w1, w24
	bne	.L1267
	strh	wzr, [x0, 22]
.L1265:
	mov	x0, x20
	mov	w25, 1
	bl	ftl_write_buf
	b	.L1261
.L1267:
	ldrh	w1, [x0, 48]
	cmp	w1, w24
	bne	.L1265
	strh	wzr, [x0, 54]
	b	.L1265
.L1262:
	cbz	w25, .L1270
	ldp	w1, w2, [x24, 36]
	adrp	x0, .LC119
	add	x0, x0, :lo12:.LC119
	bl	printk
	mov	x0, x20
	bl	ftl_write_buf
	b	.L1261
.L1270:
	ldrh	w2, [x24, 48]
	ldr	w0, [x24, 36]
	ldr	w1, [x24, 44]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x24, 2]
	tbz	x0, 2, .L1271
	and	w0, w0, -3
	strb	w0, [x24, 2]
	b	.L1261
.L1271:
	mov	x0, x20
	bl	zbuf_free
	b	.L1261
	.size	ftl_write_completed, .-ftl_write_completed
	.align	2
	.global	zftl_add_read_buf
	.type	zftl_add_read_buf, %function
zftl_add_read_buf:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	cbnz	x0, .L1278
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1744
	mov	w2, 1151
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	print_ftl_debug_info
.L1277:
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1278:
	mov	x20, x0
	adrp	x19, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR3
	ldrb	w1, [x20, 56]
	ldrb	w0, [x0, 1946]
	cmp	w1, w0
	bls	.L1281
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 1744
	mov	w2, 1158
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1281:
	add	x19, x19, :lo12:.LANCHOR3
	mov	x1, x20
	add	x0, x19, 1956
	bl	buf_add_tail
	ldrb	w0, [x19, 1957]
	add	w0, w0, 1
	strb	w0, [x19, 1957]
	b	.L1277
	.size	zftl_add_read_buf, .-zftl_add_read_buf
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, -1
	strb	w1, [x0, 3354]
	strb	w1, [x0, 3357]
	strb	w1, [x0, 3355]
	strb	w1, [x0, 3356]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC120
	add	x0, x0, :lo12:.LC120
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	ldrb	w1, [x20, 3354]
	bl	printk
	ldrb	w19, [x20, 3354]
	cmp	w19, 255
	beq	.L1284
	add	x20, x20, 1304
	ubfiz	x19, x19, 6, 8
	adrp	x21, .LC121
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC121
.L1286:
	ldrb	w2, [x19, 58]
	mov	x0, x21
	ldrb	w1, [x19, 1]
	ldr	w3, [x19, 40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L1284
	ubfiz	x19, x19, 6, 8
	add	x19, x20, x19
	b	.L1286
.L1284:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	dump_sblk_queue, .-dump_sblk_queue
	.align	2
	.global	queue_lun_state
	.type	queue_lun_state, %function
queue_lun_state:
	adrp	x5, .LANCHOR0
	add	x5, x5, :lo12:.LANCHOR0
	ldrb	w2, [x5, 3354]
	cmp	w2, 255
	beq	.L1302
	adrp	x6, .LANCHOR3
	add	x6, x6, :lo12:.LANCHOR3
	ldrb	w3, [x5, 1205]
	mov	w9, 24
	mov	w4, 1
	adrp	x12, .L1298
	ldrh	w11, [x6, 1304]
	sub	w9, w9, w3
	ldrb	w8, [x6, 1306]
	lsl	w3, w4, w3
	sub	w7, w9, w11
	sub	w3, w3, #1
	sub	w8, w8, #1
	and	w3, w3, 65535
	lsl	w4, w4, w7
	and	w8, w8, 65535
	sub	w4, w4, #1
	asr	w6, w0, w11
	and	w4, w4, 65535
	and	w6, w6, w8
	asr	w10, w0, w9
	and	w6, w6, w4
	and	w10, w10, w3
	add	x5, x5, 1304
	add	x12, x12, :lo12:.L1298
.L1301:
	add	x13, x5, x2, lsl 6
	ldr	w7, [x13, 40]
	lsr	w0, w7, w9
	and	w0, w3, w0
	cmp	w10, w0
	bne	.L1296
	lsr	w7, w7, w11
	and	w7, w8, w7
	and	w7, w4, w7
	ldrb	w0, [x13, 58]
	cmp	w6, w7
	bne	.L1297
	cmp	w1, 1
	bne	.L1294
	cmp	w0, 7
	ccmp	w0, 9, 4, ne
	beq	.L1296
	ret
.L1297:
	cmp	w1, 3
	bhi	.L1296
	ldrb	w7, [x12,w1,uxtw]
	adr	x13, .Lrtx1298
	add	x7, x13, w7, sxtb #2
	br	x7
.Lrtx1298:
	.section	.rodata
	.align	0
	.align	2
.L1298:
	.byte	(.L1294 - .Lrtx1298) / 4
	.byte	(.L1299 - .Lrtx1298) / 4
	.byte	(.L1300 - .Lrtx1298) / 4
	.byte	(.L1294 - .Lrtx1298) / 4
	.text
.L1299:
	cmp	w0, 7
	ccmp	w0, 9, 4, ne
	beq	.L1296
	ret
.L1300:
	cmp	w0, 11
	bne	.L1294
.L1296:
	lsl	x2, x2, 6
	ldrb	w2, [x5, x2]
	cmp	w2, 255
	bne	.L1301
.L1302:
	mov	w0, 0
.L1294:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x3, .LANCHOR0
	add	x0, x3, :lo12:.LANCHOR0
	add	x4, x0, 1304
	add	x12, x0, 3355
	add	x13, x0, 3356
	add	x14, x0, 3357
	ldrb	w7, [x0, 3354]
	mov	w5, 0
	ldrb	w9, [x0, 3355]
	mov	w11, -1
	ldrb	w10, [x0, 3357]
	ldrb	w8, [x0, 3356]
.L1305:
	cmp	w7, 255
	beq	.L1306
	sbfiz	x6, x7, 6, 32
	sxtw	x1, w7
	add	x2, x4, x6
	ldrb	w2, [x2, 58]
	sub	w2, w2, #12
	and	w2, w2, 255
	cmp	w2, 1
	bls	.L1307
	cbz	w5, .L1304
	strb	w7, [x0, 3354]
	ret
.L1307:
	lsl	x5, x1, 6
	ldrb	w7, [x4, x6]
	add	x2, x4, x5
	strb	w11, [x4, x5]
	ldrb	w5, [x2, 59]
	cmp	w5, 1
	bne	.L1310
	ldrh	w2, [x2, 50]
	cbnz	w2, .L1318
	mov	w2, w10
	mov	x5, x14
.L1311:
	cmp	w2, 255
	bne	.L1316
	add	x3, x3, :lo12:.LANCHOR0
	add	x1, x3, x1, lsl 6
	strb	w7, [x3, 3354]
	ldrb	w0, [x1, 1305]
	strb	w0, [x5]
	ret
.L1310:
	cbnz	w5, .L1312
	ldr	w2, [x2, 36]
	cmn	w2, #1
	beq	.L1312
	mov	w2, w9
	mov	x5, x12
	b	.L1311
.L1318:
	mov	w2, w8
	mov	x5, x13
	b	.L1311
.L1316:
	mov	w5, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x4, x2]
	cmp	w2, 255
	bne	.L1316
	add	x1, x4, x1, lsl 6
	sbfiz	x5, x5, 6, 32
	ldrb	w1, [x1, 1]
	strb	w1, [x4, x5]
.L1312:
	mov	w5, 1
	b	.L1305
.L1306:
	cbz	w5, .L1304
	add	x3, x3, :lo12:.LANCHOR0
	mov	w0, -1
	strb	w0, [x3, 3354]
.L1304:
	ret
	.size	queue_remove_completed_req, .-queue_remove_completed_req
	.align	2
	.global	pm_select_ram_region
	.type	pm_select_ram_region, %function
pm_select_ram_region:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LANCHOR3
	add	x0, x1, :lo12:.LANCHOR3
	mov	x2, 0
	add	x29, sp, 0
	add	x3, x0, 1960
	mov	w4, 65535
	str	x19, [sp, 16]
.L1330:
	lsl	x5, x2, 4
	and	w19, w2, 65535
	ldrh	w5, [x5, x3]
	cmp	w5, w4
	beq	.L1329
	add	x2, x2, 1
	cmp	x2, 32
	bne	.L1330
	add	x0, x0, 1962
	mov	w19, w2
	mov	x3, x0
	mov	w5, 32768
	mov	w2, 0
.L1332:
	ldrh	w4, [x3]
	tbnz	x4, 15, .L1331
	cmp	w4, w5
	bcs	.L1331
	mov	w5, w4
	mov	w19, w2
.L1331:
	add	w2, w2, 1
	add	x3, x3, 16
	and	w2, w2, 65535
	cmp	w2, 32
	bne	.L1332
	cmp	w19, 32
	bne	.L1329
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, -1
	ldrb	w3, [x1, 2472]
	mov	w1, 0
.L1334:
	ldrh	w5, [x0]
	cmp	w5, w2
	bcs	.L1333
	ldrh	w4, [x0, -2]
	cmp	w4, w3
	csel	w2, w2, w5, eq
	csel	w19, w19, w1, eq
.L1333:
	add	w1, w1, 1
	add	x0, x0, 16
	and	w1, w1, 65535
	cmp	w1, 32
	bne	.L1334
	cmp	w19, 32
	bne	.L1329
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w2, 377
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1329:
	mov	w0, w19
	ldr	x19, [sp, 16]
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	str	x21, [sp, 32]
	mov	w20, 1024
	ldr	x2, [x1, 1144]
	ldrb	w2, [x2, 12]
	sdiv	w20, w20, w2
	cbnz	w0, .L1342
	add	x1, x1, 4
	mov	x0, 0
.L1343:
	strh	w0, [x1, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L1343
.L1349:
	adrp	x21, .LANCHOR3
	add	x21, x21, :lo12:.LANCHOR3
	add	x21, x21, 2476
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	mov	w1, 255
	mov	x0, x21
	add	x19, x19, 4
	bl	ftl_memset
	mov	x0, 0
.L1344:
	ldrh	w1, [x19, x0, lsl 1]
	add	x0, x0, 1
	cmp	w20, w0, uxth
	strh	w1, [x21, w1, sxtw 1]
	bhi	.L1344
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1342:
	cmp	w0, 1
	bne	.L1345
	add	x1, x1, 4
	mov	x2, 0
.L1348:
	and	w0, w2, 65535
	cmp	x2, 3
	bls	.L1346
	ubfiz	w3, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 2
	sub	w0, w3, w0
	and	w0, w0, 65535
.L1346:
	strh	w0, [x1, x2, lsl 1]
	add	x2, x2, 1
	cmp	x2, 512
	bne	.L1348
	b	.L1349
.L1345:
	cmp	w0, 2
	bne	.L1350
	add	x1, x1, 4
	mov	w2, 65535
	mov	x0, 0
.L1352:
	cmp	x0, 2
	and	w3, w0, 65535
	csel	w3, w3, w2, cc
	strh	w3, [x1, x0, lsl 1]
	add	w2, w2, 2
	add	x0, x0, 1
	and	w2, w2, 65535
	cmp	x0, 512
	bne	.L1352
	b	.L1349
.L1350:
	cmp	w0, 3
	bne	.L1353
	add	x1, x1, 4
	mov	x2, 0
.L1356:
	and	w0, w2, 65535
	cmp	x2, 5
	bls	.L1354
	ubfiz	w3, w0, 1, 15
	and	w0, w0, 1
	add	w0, w0, 4
	sub	w0, w3, w0
	and	w0, w0, 65535
.L1354:
	strh	w0, [x1, x2, lsl 1]
	add	x2, x2, 1
	cmp	x2, 512
	bne	.L1356
	b	.L1349
.L1353:
	cmp	w0, 4
	bne	.L1357
	mov	w3, 1
	strh	w0, [x1, 12]
	mov	w0, 5
	strh	w3, [x1, 6]
	strh	w0, [x1, 14]
	mov	w3, 2
	mov	w0, 7
	strh	w3, [x1, 8]
	strh	w0, [x1, 16]
	mov	w3, 3
	mov	w0, 8
	strh	wzr, [x1, 4]
	strh	w0, [x1, 18]
	add	x1, x1, 20
	strh	w3, [x1, -10]
	mov	w0, 8
.L1359:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w3, w3, 6
	add	w0, w0, 1
	sub	w2, w2, w3
	strh	w2, [x1], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L1359
	b	.L1349
.L1357:
	cmp	w0, 5
	bne	.L1360
	add	x2, x1, 4
	mov	x0, 0
.L1361:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L1361
	add	x1, x1, 36
.L1362:
	strh	w0, [x1], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 1008
	bne	.L1362
	b	.L1349
.L1360:
	cmp	w0, 8
	bne	.L1363
	add	x1, x1, 4
	mov	x0, 0
.L1364:
	strh	w0, [x0, x1]
	add	x0, x0, 2
	cmp	x0, 1024
	bne	.L1364
	b	.L1349
.L1363:
	cmp	w0, 9
	bne	.L1365
	mov	w2, 1
	strh	wzr, [x1, 4]
	strh	w2, [x1, 6]
	mov	w2, 2
	add	x1, x1, 10
	strh	w2, [x1, -2]
	mov	w0, 3
.L1366:
	strh	w0, [x1], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 1021
	bne	.L1366
	b	.L1349
.L1365:
	cmp	w0, 10
	bne	.L1367
	add	x2, x1, 4
	mov	x0, 0
.L1368:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L1368
	add	x1, x1, 130
.L1369:
	strh	w0, [x1], 2
	add	w0, w0, 2
	and	w0, w0, 65535
	cmp	w0, 961
	bne	.L1369
	b	.L1349
.L1367:
	cmp	w0, 11
	bne	.L1370
	add	x1, x19, :lo12:.LANCHOR0
	mov	x0, 0
	add	x2, x1, 4
.L1371:
	strh	w0, [x2, x0, lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1371
	add	x1, x1, 20
.L1373:
	and	w3, w0, 1
	ubfiz	w2, w0, 1, 15
	add	w3, w3, 6
	add	w0, w0, 1
	sub	w2, w2, w3
	strh	w2, [x1], 2
	and	w0, w0, 65535
	cmp	w0, 512
	bne	.L1373
	b	.L1349
.L1370:
	cmp	w0, 13
	bne	.L1349
	add	x2, x19, :lo12:.LANCHOR0
	mov	x1, 0
	add	x2, x2, 4
	mov	w0, 0
.L1374:
	strh	w0, [x1, x2]
	add	w0, w0, 3
	and	w0, w0, 65535
	add	x1, x1, 2
	cmp	w0, 1536
	bne	.L1374
	b	.L1349
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.type	flash_die_info_init, %function
flash_die_info_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp, 32]
	ldr	w0, [x19, #:lo12:.LANCHOR2]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	tbz	x0, 12, .L1397
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC4
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC4
	bl	printk
.L1397:
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	add	x20, x22, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR5
	add	x23, x21, :lo12:.LANCHOR5
	mov	w2, 8
	ldrh	w0, [x24, 34]
	add	x25, x20, 1208
	ldrb	w1, [x24, 20]
	add	x27, x24, 9
	strh	w0, [x20, 2]
	mov	w26, 2
	ldrh	w0, [x24, 18]
	strb	wzr, [x20, 1153]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x23, 172]
	add	x0, x20, 1196
	bl	ftl_memset
	add	x0, x23, 176
	mov	x23, 0
	mov	w2, 32
	mov	w1, 0
	bl	ftl_memset
.L1399:
	ldrb	w2, [x24, 8]
	add	x7, x20, 1024
	strb	w26, [x23, x25]
	add	x1, x20, 1216
	add	x1, x1, x23, lsl 3
	mov	x0, x27
	bl	flash_mem_cmp8
	cbnz	w0, .L1398
	ldrb	w2, [x20, 1153]
	add	x1, x21, :lo12:.LANCHOR5
	add	x1, x1, 176
	add	x7, x7, x2
	add	w0, w2, 1
	strb	w0, [x20, 1153]
	and	w0, w23, 255
	str	wzr, [x1, x2, lsl 2]
	strb	w0, [x7, 172]
	bl	zftl_flash_enter_slc_mode
.L1398:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L1399
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 8
	ldrb	w2, [x1, 8]
	cmp	w2, 2
	beq	.L1400
.L1404:
	add	x19, x19, :lo12:.LANCHOR2
	add	x22, x22, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR5
	ldr	x27, [sp, 80]
	ldp	x23, x24, [sp, 48]
	ldrb	w1, [x19, 21]
	ldrb	w0, [x22, 1153]
	ldp	x25, x26, [sp, 64]
	mul	w0, w0, w1
	ldrh	w1, [x19, 22]
	ldp	x19, x20, [sp, 16]
	mul	w0, w0, w1
	strh	w0, [x21, 208]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 96
	ret
.L1400:
	add	x8, x22, :lo12:.LANCHOR0
	ldrh	w7, [x1, 14]
	add	x11, x21, :lo12:.LANCHOR5
	ldrb	w12, [x0, 8]
	and	w7, w7, 65280
	ldrb	w13, [x1, 23]
	ldrh	w2, [x8, 2]
	add	x15, x8, 1216
	add	x10, x0, 9
	add	x11, x11, 176
	add	x18, x8, 1024
	mov	x9, 0
	mul	w7, w7, w2
	ldrb	w2, [x1, 13]
	mul	w7, w7, w2
	lsl	w14, w7, 1
.L1403:
	mov	w2, w12
	add	x1, x15, x9, lsl 3
	mov	x0, x10
	bl	flash_mem_cmp8
	cbnz	w0, .L1401
	ldrb	w0, [x8, 1153]
	cmp	w13, 0
	csel	w2, w7, w14, eq
	add	w1, w0, 1
	strb	w1, [x8, 1153]
	str	w2, [x11, x0, lsl 2]
	add	x0, x18, x0
	strb	w9, [x0, 172]
.L1401:
	add	x9, x9, 1
	cmp	x9, 4
	bne	.L1403
	b	.L1404
	.size	flash_die_info_init, .-flash_die_info_init
	.align	2
	.global	lpa_hash_init
	.type	lpa_hash_init, %function
lpa_hash_init:
	stp	x29, x30, [sp, -32]!
	mov	w2, 512
	mov	w1, 255
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	add	x0, x19, 1416
	bl	ftl_memset
	mov	w1, 255
	ldrb	w0, [x19, 1321]
	ldrh	w2, [x19, 1376]
	mul	w2, w2, w0
	ldr	x0, [x19, 1936]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	lpa_hash_init, .-lpa_hash_init
	.align	2
	.global	lpa_rebuild_hash
	.type	lpa_rebuild_hash, %function
lpa_rebuild_hash:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR2
	add	x29, sp, 0
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	stp	x19, x20, [sp, 16]
	tbz	x0, 12, .L1417
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC122
	mov	w3, 0
	mov	w2, 239
	add	x1, x1, 48
	add	x0, x0, :lo12:.LC122
	bl	printk
.L1417:
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	add	x20, x19, 1416
	mov	w2, 512
	mov	w1, 255
	mov	x0, x20
	bl	ftl_memset
	ldrb	w0, [x19, 1321]
	mov	w1, 255
	ldrh	w2, [x19, 1376]
	mul	w2, w2, w0
	ldr	x0, [x19, 1936]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w1, 0
.L1418:
	ldrh	w0, [x19, 1376]
	ldrb	w2, [x19, 1321]
	mul	w0, w0, w2
	cmp	w1, w0, lsl 1
	blt	.L1420
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1420:
	ldr	x0, [x19, 1928]
	uxtw	x2, w1
	ldr	w0, [x0, x2, lsl 2]
	cmn	w0, #1
	beq	.L1419
	and	x0, x0, 255
	ldrh	w3, [x20, x0, lsl 1]
	strh	w1, [x20, x0, lsl 1]
	ldr	x0, [x19, 1936]
	strh	w3, [x0, x2, lsl 1]
.L1419:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1418
	.size	lpa_rebuild_hash, .-lpa_rebuild_hash
	.align	2
	.global	zftl_read_flash_info
	.type	zftl_read_flash_info, %function
zftl_read_flash_info:
	stp	x29, x30, [sp, -32]!
	mov	w2, 11
	mov	w1, 0
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	bl	ftl_memset
	adrp	x1, .LANCHOR2
	adrp	x0, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x1, 8
	strb	wzr, [x19, 10]
	mov	w4, 1
	ldrb	w2, [x1, 9]
	ldrh	w3, [x0, 2]
	add	x0, x0, 1196
	mul	w2, w2, w3
	strh	w2, [x19, 4]
	ldrb	w2, [x0, 53]
	strb	w2, [x19, 7]
	ldr	w2, [x0, -164]
	str	w2, [x19]
	ldrb	w2, [x1, 9]
	strb	w2, [x19, 6]
	ldrb	w3, [x0, -43]
	mov	w2, 32
	ldrb	w1, [x1, 7]
	strb	w2, [x19, 8]
	mov	x2, 0
	strb	w1, [x19, 9]
.L1429:
	cmp	w3, w2, uxtb
	bhi	.L1430
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L1430:
	ldrb	w1, [x2, x0]
	add	x2, x2, 1
	ldrb	w5, [x19, 10]
	lsl	w1, w4, w1
	orr	w1, w1, w5
	strb	w1, [x19, 10]
	b	.L1429
	.size	zftl_read_flash_info, .-zftl_read_flash_info
	.align	2
	.global	gc_static_wearleveling
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x0, [x0, 3384]
	ldr	w1, [x0, 32]
	mov	w0, 10240
	cmp	w1, w0
	bls	.L1433
	bl	ftl_tmp_into_update
.L1433:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w4, 36000
	ldr	x0, [x3, 1128]
	ldr	w1, [x0, 568]
	ldr	w2, [x0, 12]
	add	w4, w1, w4
	cmp	w2, w4
	bcs	.L1434
	ldr	x4, [x3, 3384]
	ldr	w3, [x0, 572]
	add	w3, w3, 256
	ldr	w4, [x4, 36]
	cmp	w4, w3
	bcc	.L1486
.L1434:
	add	w1, w1, 860160
	add	w1, w1, 3840
	cmp	w2, w1
	bhi	.L1436
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x3, [x1, 3384]
	ldr	w1, [x0, 572]
	add	w1, w1, 32
	ldr	w3, [x3, 36]
	cmp	w3, w1
	bls	.L1487
.L1436:
	add	x24, x19, :lo12:.LANCHOR0
	mov	w20, 65535
	adrp	x8, .LC123
	mov	w21, w20
	mov	w9, w20
	add	x8, x8, :lo12:.LC123
	ldr	x1, [x24, 3384]
	mov	w28, 0
	mov	w27, 0
	mov	w26, 0
	mov	w25, 0
	mov	w23, 0
	adrp	x10, .LANCHOR2
	stp	wzr, wzr, [x29, 128]
	ldr	w3, [x1, 36]
	str	w3, [x0, 572]
	str	w2, [x0, 568]
	str	wzr, [x29, 136]
	ldrh	w22, [x1, 134]
.L1438:
	ldrh	w0, [x24, 1096]
	cmp	w0, w22
	bhi	.L1447
	ldr	x0, [x24, 1128]
	mov	w2, 128
	mov	w1, 255
	adrp	x22, .LANCHOR2
	add	x0, x0, 264
	strh	wzr, [x0, -142]
	bl	ftl_memset
	ldr	x0, [x24, 1128]
	ldr	w2, [x22, #:lo12:.LANCHOR2]
	ldr	x10, [x24, 1104]
	ldrh	w0, [x0, 586]
	mov	x1, x0
	lsl	x9, x0, 2
	add	x8, x10, x9
	tbz	x2, 10, .L1448
	ldr	x5, [x24, 1120]
	ldrb	w6, [x8, 3]
	ldrb	w4, [x8, 2]
	ldr	w3, [x10, x9]
	ldrh	w7, [x5, x0, lsl 1]
	adrp	x0, .LC124
	ldrh	w2, [x10, x9]
	ubfx	x5, x4, 5, 3
	stp	x9, x10, [x29, 104]
	ubfx	x3, x3, 11, 8
	str	x8, [x29, 120]
	and	w2, w2, 2047
	ubfx	x4, x4, 3, 2
	add	x0, x0, :lo12:.LC124
	bl	printk
	ldp	x9, x10, [x29, 104]
	ldr	x8, [x29, 120]
.L1448:
	ldrb	w0, [x8, 2]
	adrp	x24, .LANCHOR5
	and	w0, w0, 224
	cmp	w0, 32
	bne	.L1449
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldrh	w0, [x0, 688]
	cmp	w0, 2
	bls	.L1449
	add	x0, x24, :lo12:.LANCHOR5
	mov	w1, 1
	str	w1, [x0, 212]
.L1449:
	ldrb	w0, [x8, 2]
	tbz	x0, 3, .L1450
	add	x3, x19, :lo12:.LANCHOR0
	ldrh	w1, [x10, x9]
	and	w1, w1, 2047
	ldr	x0, [x3, 3384]
	ldrh	w2, [x0, 96]
	add	x0, x24, :lo12:.LANCHOR5
	ldrh	w0, [x0, 216]
	add	w0, w1, w0, lsr 2
	cmp	w2, w0
	ble	.L1450
	ldrb	w0, [x8, 2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1451
	ldr	x0, [x3, 1128]
	mov	w2, 1
	str	x3, [x29, 120]
	mov	w1, 0
	ldrh	w0, [x0, 586]
	bl	gc_add_sblk
	ldr	x3, [x29, 120]
	mov	w0, 1
	strh	w0, [x3, 5608]
.L1450:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x22, #:lo12:.LANCHOR2]
	ldr	x1, [x0, 1128]
	ldr	x10, [x0, 1104]
	ldrh	w5, [x1, 584]
	mov	x1, x5
	lsl	x9, x5, 2
	add	x8, x10, x9
	tbz	x2, 10, .L1452
	ldr	x0, [x0, 1120]
	ldrb	w6, [x8, 3]
	ldrb	w4, [x8, 2]
	ldr	w3, [x10, x9]
	ldrh	w7, [x0, x5, lsl 1]
	adrp	x0, .LC125
	ldrh	w2, [x10, x9]
	ubfx	x5, x4, 5, 3
	stp	x9, x10, [x29, 104]
	ubfx	x3, x3, 11, 8
	str	x8, [x29, 120]
	and	w2, w2, 2047
	ubfx	x4, x4, 3, 2
	add	x0, x0, :lo12:.LC125
	bl	printk
	ldp	x9, x10, [x29, 104]
	ldr	x8, [x29, 120]
.L1452:
	ldrb	w0, [x8, 2]
	tbz	x0, 3, .L1453
	add	x3, x19, :lo12:.LANCHOR0
	ldr	w1, [x10, x9]
	ldr	x0, [x3, 3384]
	ubfx	x1, x1, 11, 8
	ldrh	w2, [x0, 98]
	add	x0, x24, :lo12:.LANCHOR5
	ldrh	w0, [x0, 218]
	add	w0, w1, w0, lsr 2
	cmp	w2, w0
	ble	.L1453
	ldrb	w0, [x8, 2]
	and	w1, w0, 192
	cmp	w1, 64
	bne	.L1454
	ldr	x0, [x3, 1128]
	mov	w2, 1
	str	x3, [x29, 120]
	mov	w1, 0
	ldrh	w0, [x0, 584]
	bl	gc_add_sblk
	ldr	x3, [x29, 120]
	mov	w0, 1
	strh	w0, [x3, 5608]
.L1453:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1455
	add	x0, x19, :lo12:.LANCHOR0
	uxtw	x6, w28
	lsl	x1, x6, 2
	ldr	x2, [x0, 1104]
	ldr	x0, [x0, 1120]
	add	x5, x2, x1
	ldr	w3, [x2, x1]
	ldrh	w7, [x0, x6, lsl 1]
	adrp	x0, .LC126
	ldrb	w6, [x5, 3]
	add	x0, x0, :lo12:.LC126
	ldrb	w4, [x5, 2]
	ubfx	x3, x3, 11, 8
	ldrh	w2, [x2, x1]
	mov	w1, w28
	ubfx	x5, x4, 5, 3
	and	w2, w2, 2047
	ubfx	x4, x4, 3, 2
	bl	printk
.L1455:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1456
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w6, [x29, 128]
	ldr	x2, [x0, 1104]
	lsl	x1, x6, 2
	ldr	x0, [x0, 1120]
	add	x5, x2, x1
	ldr	w3, [x2, x1]
	ldrh	w7, [x0, x6, lsl 1]
	adrp	x0, .LC127
	ldrh	w2, [x2, x1]
	add	x0, x0, :lo12:.LC127
	ldrb	w6, [x5, 3]
	ubfx	x3, x3, 11, 8
	ldr	w1, [x29, 128]
	and	w2, w2, 2047
	ldrb	w4, [x5, 2]
	ubfx	x5, x4, 5, 3
	ubfx	x4, x4, 3, 2
	bl	printk
.L1456:
	ldr	w1, [x29, 132]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	udiv	w3, w1, w26
	ldr	w1, [x29, 136]
	strh	w25, [x0, 96]
	udiv	w4, w1, w27
	strh	w23, [x0, 98]
	strh	w20, [x0, 92]
	strh	w21, [x0, 94]
	strh	w3, [x0, 88]
	strh	w4, [x0, 90]
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1457
	adrp	x0, .LC128
	and	w4, w4, 65535
	and	w3, w3, 65535
	mov	w2, w27
	mov	w1, w26
	add	x0, x0, :lo12:.LC128
	bl	printk
.L1457:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1458
	add	x0, x24, :lo12:.LANCHOR5
	mov	w4, w23
	mov	w3, w25
	mov	w2, w21
	mov	w1, w20
	ldrh	w6, [x0, 218]
	ldrh	w5, [x0, 216]
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	bl	printk
.L1458:
	add	x1, x24, :lo12:.LANCHOR5
	sub	w0, w23, w21
	str	w0, [x29, 128]
	ldrh	w0, [x1, 218]
	ldr	w2, [x29, 128]
	cmp	w2, w0
	bgt	.L1459
	ldrh	w1, [x1, 216]
	sub	w0, w25, w20
	cmp	w0, w1
	ble	.L1488
.L1459:
	add	x8, x19, :lo12:.LANCHOR0
	mov	w26, 0
	mov	x27, x8
	mov	w23, 0
	ldr	x0, [x8, 1128]
	ldrh	w28, [x0, 580]
	ldr	x0, [x8, 3384]
	ldrh	w0, [x0, 134]
	str	w0, [x29, 132]
	add	x0, x8, 3416
	str	x0, [x29, 136]
	adrp	x0, .LC131
	add	x0, x0, :lo12:.LC131
	str	x0, [x29, 120]
.L1461:
	ldrh	w0, [x27, 1096]
	ldr	w1, [x29, 132]
	cmp	w1, w0
	bcc	.L1471
.L1470:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 1128]
	str	w28, [x0, 580]
.L1460:
	cbz	w21, .L1473
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldrh	w1, [x1, 134]
.L1474:
	ldrh	w2, [x0, 1096]
	cmp	w2, w1
	bhi	.L1476
	ldr	x0, [x0, 3384]
	ldrh	w1, [x0, 72]
	add	w1, w21, w1
	strh	w1, [x0, 72]
	ldrh	w1, [x0, 98]
	cmp	w21, w1
	bcs	.L1473
	sub	w21, w1, w21
	strh	w21, [x0, 98]
.L1473:
	cbz	w20, .L1479
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldrh	w1, [x1, 134]
.L1480:
	ldrh	w2, [x0, 1096]
	cmp	w2, w1
	bhi	.L1482
	ldr	x0, [x0, 3384]
	ldrh	w1, [x0, 74]
	add	w1, w20, w1
	strh	w1, [x0, 74]
	ldrh	w1, [x0, 96]
	cmp	w20, w1
	bcs	.L1479
	sub	w20, w1, w20
	strh	w20, [x0, 96]
.L1479:
	adrp	x5, .LANCHOR3
	add	x5, x5, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x5, 1368
	bl	_list_get_gc_head_node
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L1437
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldrh	w2, [x5, 1376]
	ldr	x3, [x19, 1120]
	ldrh	w1, [x3, x1]
	cmp	w1, w2, lsr 1
	bhi	.L1437
	add	w23, w23, 1
	mov	w2, 1
	mov	w1, 0
	bl	gc_add_sblk
.L1437:
	add	w0, w23, w26
.L1432:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1447:
	uxtw	x7, w22
	ldr	x5, [x24, 1104]
	lsl	x2, x7, 2
	add	x6, x5, x2
	ldrb	w0, [x6, 2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L1439
	tbz	x0, 3, .L1440
	ldr	w0, [x5, x2]
	ldrh	w1, [x5, x2]
	ubfx	x0, x0, 11, 8
	and	w1, w1, 2047
.L1441:
	ldr	w3, [x29, 132]
	add	w26, w26, 1
	and	w26, w26, 65535
	cmp	w20, w1
	add	w3, w3, w1
	str	w3, [x29, 132]
	bls	.L1485
	ldr	x3, [x24, 1128]
	mov	w20, w1
	strh	w22, [x3, 586]
.L1485:
	cmp	w25, w1
	bcs	.L1444
	mov	w28, w22
	mov	w25, w1
.L1444:
	cmp	w0, w9
	bne	.L1443
.L1445:
	cmp	w0, 9
	ccmp	w1, 9, 0, hi
	bhi	.L1439
	ldr	w0, [x10, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L1439
	ldr	x0, [x24, 1120]
	mov	w1, w22
	ldrb	w4, [x6, 2]
	ldrb	w6, [x6, 3]
	ldr	w3, [x5, x2]
	ldrh	w7, [x0, x7, lsl 1]
	mov	x0, x8
	ldrh	w2, [x5, x2]
	ubfx	x5, x4, 5, 3
	str	x10, [x29, 104]
	ubfx	x4, x4, 3, 2
	str	w9, [x29, 112]
	ubfx	x3, x3, 11, 8
	str	x8, [x29, 120]
	and	w2, w2, 2047
	bl	printk
	ldr	w9, [x29, 112]
	ldr	x10, [x29, 104]
	ldr	x8, [x29, 120]
.L1439:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L1438
.L1440:
	tst	w0, 24
	bne	.L1442
	ldrh	w1, [x5, x2]
	mov	w0, 65535
	and	w1, w1, 2047
	b	.L1441
.L1442:
	ldr	w0, [x5, x2]
	mov	w1, 65535
	ubfx	x0, x0, 11, 8
.L1443:
	ldr	w3, [x29, 136]
	add	w27, w27, 1
	and	w27, w27, 65535
	cmp	w21, w0
	add	w3, w3, w0
	str	w3, [x29, 136]
	bls	.L1446
	ldr	x3, [x24, 1128]
	mov	w21, w0
	strh	w22, [x3, 584]
.L1446:
	cmp	w23, w0
	bcs	.L1445
	mov	w23, w0
	str	w22, [x29, 128]
	b	.L1445
.L1451:
	tst	w0, 224
	bne	.L1450
	ldr	x1, [x3, 1128]
	mov	w0, 65535
	ldrh	w2, [x1, 590]
	cmp	w2, w0
	bne	.L1450
	ldrh	w0, [x1, 586]
	str	x3, [x29, 120]
	ldrh	w1, [x1, 588]
	cmp	w1, w0
	beq	.L1450
	bl	zftl_remove_free_node
	ldr	x3, [x29, 120]
	ldr	x0, [x3, 1128]
	ldrh	w1, [x0, 586]
	strh	w1, [x0, 590]
	mov	w1, -1
	strh	w1, [x0, 586]
	b	.L1450
.L1454:
	and	w0, w0, 248
	cmp	w0, 16
	bne	.L1453
	ldr	x1, [x3, 1128]
	mov	w0, 65535
	ldrh	w2, [x1, 588]
	cmp	w2, w0
	bne	.L1453
	ldrh	w0, [x1, 584]
	str	x3, [x29, 120]
	ldrh	w1, [x1, 590]
	cmp	w1, w0
	beq	.L1453
	bl	zftl_remove_free_node
	ldr	x3, [x29, 120]
	ldr	x0, [x3, 1128]
	ldrh	w1, [x0, 584]
	strh	w1, [x0, 588]
	mov	w1, -1
	strh	w1, [x0, 584]
	b	.L1453
.L1471:
	add	w7, w28, 1
	ldr	x4, [x27, 1104]
	and	w28, w7, 65535
	cmp	w0, w28
	csel	w28, w28, wzr, hi
	uxtw	x9, w28
	lsl	x3, x9, 2
	add	x0, x4, x3
	ldrb	w0, [x0, 2]
	tst	w0, 192
	beq	.L1463
	and	w2, w0, 224
	cmp	w2, 224
	beq	.L1463
	ubfx	x0, x0, 3, 2
	and	w1, w0, 1
	tbz	x0, 0, .L1464
	cmp	w2, 160
.L1551:
	bne	.L1466
	add	x0, x24, :lo12:.LANCHOR5
	ldr	w2, [x29, 128]
	ldrh	w0, [x0, 218]
	cmp	w2, w0
	ble	.L1467
	ldr	w0, [x4, x3]
	ubfx	x0, x0, 11, 8
	cmp	w0, w21
	bls	.L1468
	cbz	w1, .L1467
	ldrh	w0, [x4, x3]
	and	w0, w0, 2047
	cmp	w0, w20
	bgt	.L1467
.L1468:
	mov	w1, 0
	stp	x4, x9, [x29, 96]
	str	x3, [x29, 112]
	mov	w2, 1
	mov	w0, w28
	bl	gc_add_sblk
	ldr	x1, [x29, 136]
	mov	w0, 1
	add	w23, w23, 1
	ldr	x3, [x29, 112]
	ldp	x4, x9, [x29, 96]
	strh	w0, [x1, 2192]
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1467
	ldr	x0, [x27, 1104]
	ldr	w6, [x4, x3]
	ldrh	w5, [x4, x3]
	add	x3, x0, x3
	ldr	x1, [x27, 1120]
	ldr	x0, [x29, 136]
	ubfx	x6, x6, 11, 8
	ldrb	w2, [x3, 2]
	and	w5, w5, 2047
	ldrh	w3, [x1, x9, lsl 1]
	mov	w1, w28
	ldrh	w4, [x0, 56]
	ubfx	x2, x2, 5, 3
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
.L1552:
	bl	printk
.L1467:
	cmp	w23, 4
	ccmp	w26, 4, 2, ls
	bhi	.L1470
.L1463:
	ldr	w0, [x29, 132]
	add	w0, w0, 1
	and	w0, w0, 65535
	str	w0, [x29, 132]
	b	.L1461
.L1464:
	cmp	w0, 2
	b	.L1551
.L1466:
	add	x2, x24, :lo12:.LANCHOR5
	sub	w0, w25, w20
	ldrh	w2, [x2, 216]
	cmp	w0, w2
	ble	.L1467
	ldrh	w0, [x4, x3]
	add	w2, w20, 8
	and	w0, w0, 2047
	cmp	w0, w2
	ble	.L1469
	cbz	w1, .L1467
	ldr	w0, [x4, x3]
	add	w1, w21, 4
	ubfx	x0, x0, 11, 8
	cmp	w0, w1
	bgt	.L1467
.L1469:
	mov	w1, 0
	stp	x4, x9, [x29, 96]
	str	x3, [x29, 112]
	mov	w2, 1
	mov	w0, w28
	bl	gc_add_sblk
	ldr	x1, [x29, 136]
	mov	w0, 1
	add	w26, w26, 1
	ldr	x3, [x29, 112]
	ldp	x4, x9, [x29, 96]
	strh	w0, [x1, 2192]
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L1467
	ldr	x0, [x27, 1104]
	ldr	w6, [x4, x3]
	ldrh	w5, [x4, x3]
	add	x3, x0, x3
	ldr	x1, [x27, 1120]
	ldr	x0, [x29, 136]
	ubfx	x6, x6, 11, 8
	ldrb	w2, [x3, 2]
	and	w5, w5, 2047
	ldrh	w3, [x1, x9, lsl 1]
	mov	w1, w28
	ldrh	w4, [x0, 56]
	ubfx	x2, x2, 5, 3
	ldr	x0, [x29, 120]
	b	.L1552
.L1488:
	mov	w26, 0
	mov	w23, 0
	b	.L1460
.L1476:
	ldr	x5, [x0, 1104]
	ubfiz	x4, x1, 2, 16
	add	x6, x5, x4
	ldr	w2, [x5, x4]
	ubfx	x3, x2, 11, 8
	cmp	w21, w3
	bhi	.L1475
	ldrb	w6, [x6, 2]
	tst	w6, 24
	beq	.L1475
	sub	w3, w3, w21
	bfi	w2, w3, 11, 8
	str	w2, [x5, x4]
.L1475:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1474
.L1482:
	ldr	x5, [x0, 1104]
	ubfiz	x4, x1, 2, 16
	add	x6, x5, x4
	ldrh	w2, [x5, x4]
	and	w3, w2, 2047
	cmp	w3, w20
	blt	.L1481
	ldrb	w6, [x6, 2]
	and	w6, w6, 24
	cmp	w6, 16
	beq	.L1481
	sub	w3, w3, w20
	bfi	w2, w3, 0, 11
	strh	w2, [x5, x4]
.L1481:
	add	w1, w1, 1
	and	w1, w1, 65535
	b	.L1480
.L1487:
	mov	w26, 0
	mov	w23, 0
	b	.L1437
.L1486:
	mov	w0, 0
	b	.L1432
	.size	gc_static_wearleveling, .-gc_static_wearleveling
	.align	2
	.global	zftl_sblk_list_init
	.type	zftl_sblk_list_init, %function
zftl_sblk_list_init:
	stp	x29, x30, [sp, -96]!
	mov	w0, 6
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR4
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR3
	ldrh	w2, [x19, 1096]
	add	x25, x25, :lo12:.LANCHOR4
	str	x27, [sp, 80]
	add	x26, x19, 3372
	add	x25, x25, 72
	mov	w23, 0
	mul	w2, w2, w0
	ldr	x0, [x19, 1040]
	bl	ftl_memset
	strh	wzr, [x19, 3368]
	add	x0, x22, :lo12:.LANCHOR3
	mov	w1, 32
	strh	wzr, [x19, 3370]
	strh	wzr, [x19, 3372]
	ldrb	w24, [x0, 1321]
	strh	w1, [x0, 1408]
	str	xzr, [x0, 1344]
	str	xzr, [x0, 1360]
	str	xzr, [x0, 1368]
	str	xzr, [x0, 1384]
	str	xzr, [x0, 1392]
	str	xzr, [x0, 1400]
	ldrh	w0, [x0, 1338]
	strh	wzr, [x19, 3374]
	strh	wzr, [x19, 3378]
	strh	wzr, [x19, 3376]
	mul	w0, w24, w0
	mov	w24, 32768
	sdiv	w24, w24, w0
	ldr	x0, [x19, 3384]
	ldrsh	w20, [x0, 134]
	sxth	w24, w24
	strh	wzr, [x0, 146]
.L1554:
	ldrh	w0, [x19, 1096]
	cmp	w20, w0
	blt	.L1571
	ldr	x0, [x19, 3384]
	ldrh	w1, [x19, 3368]
	strh	w1, [x0, 114]
	ldrh	w1, [x19, 3370]
	strh	w1, [x0, 118]
	ldrh	w1, [x19, 3372]
	strh	w1, [x0, 116]
	ldrh	w1, [x19, 3374]
	strh	w1, [x0, 122]
	ldrh	w1, [x19, 3378]
	strh	w1, [x0, 120]
	ldrh	w1, [x19, 3376]
	strh	w1, [x0, 124]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L1571:
	ldr	x21, [x19, 1104]
	sxtw	x27, w20
	add	x21, x21, x27, lsl 2
	ldrb	w0, [x21, 3]
	cbz	w0, .L1572
	add	x0, x22, :lo12:.LANCHOR3
	ldr	x3, [x19, 3384]
	mov	w1, 0
	ldrb	w4, [x0, 1321]
	ldrh	w5, [x0, 1338]
	mov	w0, 0
.L1556:
	cmp	w1, w4
	blt	.L1559
	cbz	w0, .L1560
	mov	w1, 32768
	sdiv	w0, w1, w0
	add	w0, w0, 1
	sxth	w0, w0
.L1555:
	mov	w1, 6
	ldr	x2, [x19, 1040]
	smull	x1, w20, w1
	add	x2, x2, x1
	strh	w0, [x2, 4]
	mov	w2, -1
	ldr	x0, [x19, 1040]
	add	x3, x0, x1
	strh	w2, [x3, 2]
	strh	w2, [x0, x1]
	mov	w1, 224
	ldrb	w0, [x21, 2]
	and	w0, w0, 224
	cmp	w0, 32
	ccmp	w0, w1, 4, ne
	beq	.L1561
	ldr	x1, [x19, 1128]
	ldrh	w2, [x1, 16]
	cmp	w20, w2
	beq	.L1561
	ldrh	w2, [x1, 48]
	cmp	w20, w2
	beq	.L1561
	ldrh	w1, [x1, 80]
	cmp	w20, w1
	beq	.L1561
	cmp	w0, 64
	bne	.L1562
	and	w21, w20, 65535
	add	x0, x22, :lo12:.LANCHOR3
	add	x2, x19, 3374
	mov	w1, w21
	add	x0, x0, 1344
.L1583:
	bl	_insert_data_list
	ldr	x0, [x19, 1120]
	ldrh	w0, [x0, x27, lsl 1]
	cmp	w0, 7
	bhi	.L1561
	mov	w0, w21
	mov	w2, 0
	mov	w1, 1
.L1581:
	bl	gc_add_sblk
.L1561:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1554
.L1559:
	ldrb	w2, [x21, 3]
	asr	w2, w2, w1
	tbnz	x2, 0, .L1557
	add	w0, w5, w0
	sxth	w0, w0
.L1558:
	add	w1, w1, 1
	b	.L1556
.L1557:
	ldrh	w2, [x3, 146]
	add	w2, w2, 1
	strh	w2, [x3, 146]
	b	.L1558
.L1560:
	ldrb	w1, [x21, 2]
	mov	w2, -1
	orr	w1, w1, -32
	strb	w1, [x21, 2]
	ldr	x1, [x19, 1120]
	strh	w2, [x1, x27, lsl 1]
	b	.L1555
.L1572:
	mov	w0, w24
	b	.L1555
.L1562:
	cmp	w0, 96
	bne	.L1563
	and	w21, w20, 65535
	add	x0, x22, :lo12:.LANCHOR3
	add	x2, x19, 3376
	mov	w1, w21
	add	x0, x0, 1360
	b	.L1583
.L1563:
	cmp	w0, 160
	bne	.L1564
	and	w21, w20, 65535
	add	x0, x22, :lo12:.LANCHOR3
	add	x2, x19, 3378
	mov	w1, w21
	add	x0, x0, 1368
	b	.L1583
.L1564:
	cbnz	w0, .L1561
	ldr	x0, [x19, 1120]
	ldrh	w2, [x0, x27, lsl 1]
	cbz	w2, .L1565
	cmp	w23, 2
	bgt	.L1566
	mov	w1, w20
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	bl	printk
	ldrb	w0, [x21, 2]
	add	w23, w23, 1
	sxth	w23, w23
	tbz	x0, 4, .L1567
	mov	w1, 5
.L1577:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x21, 2]
	mov	w1, 1
	mov	w0, w20
	b	.L1581
.L1567:
	mov	w1, 2
	b	.L1577
.L1566:
	mov	x1, x25
	mov	w2, 656
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1565:
	ldrb	w0, [x21, 2]
	ands	w0, w0, 24
	bne	.L1569
	add	x0, x22, :lo12:.LANCHOR3
	add	x2, x19, 3368
	mov	w1, w20
	add	x0, x0, 1384
.L1579:
	bl	_insert_free_list
	b	.L1561
.L1569:
	cmp	w0, 16
	add	x0, x22, :lo12:.LANCHOR3
	bne	.L1570
	add	x2, x19, 3370
	mov	w1, w20
	add	x0, x0, 1392
	b	.L1579
.L1570:
	mov	x2, x26
	mov	w1, w20
	add	x0, x0, 1400
	b	.L1579
	.size	zftl_sblk_list_init, .-zftl_sblk_list_init
	.align	2
	.global	pm_free_sblk
	.type	pm_free_sblk, %function
pm_free_sblk:
	stp	x29, x30, [sp, -368]!
	adrp	x0, .LANCHOR5
	add	x1, x0, :lo12:.LANCHOR5
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w1, [x1, 220]
	cmp	w1, 128
	bls	.L1586
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 96
	mov	w2, 94
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L1586:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, 0
	mov	w4, 65535
	ldr	x0, [x0, 3384]
	ldrh	w3, [x19, 220]
	mov	w19, 0
	add	x0, x0, 416
	ldrh	w2, [x0, 272]
.L1589:
	ldrh	w5, [x0]
	cmp	w5, w4
	beq	.L1587
	add	w1, w1, 1
	and	w1, w1, 65535
.L1587:
	cmp	w1, w2
	bcs	.L1588
	cmp	w1, w3
	bcs	.L1588
	add	w19, w19, 1
	add	x0, x0, 2
	and	w19, w19, 65535
	cmp	w19, 128
	bne	.L1589
.L1588:
	add	w19, w19, 1
	mov	w0, 128
	and	w19, w19, 65535
	add	x22, x29, 112
	cmp	w19, 129
	mov	w2, 256
	csel	w19, w19, w0, ne
	mov	w1, 0
	mov	x0, x22
	adrp	x23, .LANCHOR3
	bl	ftl_memset
	str	x23, [x29, 104]
	add	x0, x21, :lo12:.LANCHOR0
	add	x1, x23, :lo12:.LANCHOR3
	mov	w2, 24
	ldr	x5, [x0, 3384]
	ldrb	w0, [x0, 1205]
	ldrh	w8, [x1, 1304]
	add	x4, x5, 704
	sub	w2, w2, w0
	ldrb	w9, [x1, 1306]
	ldrh	w1, [x5, 698]
	sub	w0, w2, w8
	mov	w2, 1
	add	x1, x1, 176
	lsl	w2, w2, w0
	add	x1, x5, x1, lsl 2
	sub	w2, w2, #1
.L1591:
	cmp	x1, x4
	bne	.L1594
	ldr	x0, [x29, 104]
	adrp	x27, .LC133
	adrp	x28, .LC134
	add	x21, x21, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LANCHOR3
	add	x27, x27, :lo12:.LC133
	add	x28, x28, :lo12:.LC134
	mov	x20, 0
	mov	w23, 65535
	mov	w26, 0
	ldrb	w24, [x0, 1321]
	ldrh	w0, [x0, 1376]
	mul	w24, w24, w0
	and	w24, w24, 65535
.L1600:
	ldr	x0, [x21, 3384]
	and	w25, w20, 65535
	ldr	x2, [x29, 104]
	mov	w6, w20
	add	x1, x0, w20, sxtw 1
	add	x2, x2, :lo12:.LANCHOR3
	ldrh	w3, [x1, 416]
	ldrb	w4, [x2, 1306]
	ldrh	w1, [x0, 692]
	sdiv	w1, w1, w4
	cmp	w1, w3
	bne	.L1595
	ldrb	w1, [x2, 1321]
	ldrh	w2, [x2, 1376]
	mul	w1, w1, w2
	strh	w1, [x22, x20, lsl 1]
.L1595:
	ldrh	w2, [x22, x20, lsl 1]
	cmp	w2, w24
	bcs	.L1596
	cmp	w2, 0
	csel	w26, w26, w25, eq
	csel	w24, w24, w2, eq
.L1596:
	ldrh	w1, [x0, 74]
	cmp	w1, 2
	bls	.L1597
	mov	w1, 65535
	cmp	w3, w1
	beq	.L1597
	ldr	x4, [x21, 1104]
	ubfiz	x1, x3, 2, 16
	ldrh	w0, [x0, 92]
	add	w0, w0, 4
	ldrh	w4, [x4, x1]
	and	w4, w4, 2047
	cmp	w4, w0
	bgt	.L1597
	mov	w1, w6
	str	w6, [x29, 100]
	mov	x0, x27
	bl	printk
	ldr	w6, [x29, 100]
	mov	w23, w25
.L1597:
	ldrh	w0, [x22, x20, lsl 1]
	cbnz	w0, .L1598
	sxtw	x25, w6
	ldr	x0, [x21, 3384]
	add	x1, x25, 208
	ldrh	w3, [x0, x1, lsl 1]
	mov	w1, 65535
	cmp	w3, w1
	beq	.L1598
	adrp	x1, .LANCHOR2
	ldr	w1, [x1, #:lo12:.LANCHOR2]
	tbz	x1, 12, .L1599
	ldrh	w4, [x0, 688]
	mov	w2, 0
	mov	w1, w6
	mov	x0, x28
	bl	printk
.L1599:
	ldr	x0, [x21, 3384]
	add	x25, x25, 208
	ldrh	w0, [x0, x25, lsl 1]
	bl	ftl_free_sblk
	ldr	x0, [x21, 3384]
	mov	w1, -1
	strh	w1, [x0, x25, lsl 1]
	ldrh	w1, [x0, 688]
	sub	w1, w1, #1
	strh	w1, [x0, 688]
.L1598:
	add	x20, x20, 1
	cmp	w19, w20, uxth
	bhi	.L1600
	mov	w0, 65535
	cmp	w23, w0
	csel	w0, w26, w23, eq
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 368
	ret
.L1594:
	ldr	w0, [x4]
	add	x6, x5, 416
	mov	x3, 0
	lsr	w0, w0, w8
	and	w0, w0, w2
	udiv	w0, w0, w9
	and	w0, w0, 65535
.L1593:
	ldrh	w7, [x6]
	cmp	w7, w0
	bne	.L1592
	ldrh	w7, [x22, x3, lsl 1]
	add	w7, w7, 1
	strh	w7, [x22, x3, lsl 1]
.L1592:
	add	x3, x3, 1
	add	x6, x6, 2
	cmp	w19, w3, uxth
	bhi	.L1593
	add	x4, x4, 4
	b	.L1591
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
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LANCHOR2
	add	x29, sp, 0
	ldr	w0, [x1, #:lo12:.LANCHOR2]
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	tbz	x0, 12, .L1614
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 112
	mov	w1, 120
	add	x0, x0, :lo12:.LC135
	bl	printk
.L1614:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 2048
	mov	w1, 0
	ldr	x0, [x19, 1048]
	bl	ftl_memset
	ldr	x0, [x19, 1048]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w2, 32
	str	w1, [x0]
	mov	w1, 2032
	ldr	x0, [x19, 1048]
	add	x0, x0, 80
	str	w1, [x0, -72]
	mov	w1, 1
	strh	w1, [x0, -64]
	add	x1, x19, 1160
	bl	ftl_memcpy
	ldr	x0, [x19, 1048]
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 32
	add	x1, x1, 8
	add	x0, x0, 48
	bl	ftl_memcpy
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_info_data_init, .-flash_info_data_init
	.align	2
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	mov	x3, 0
.L1620:
	cmp	w2, w3
	bhi	.L1621
	ret
.L1621:
	ldr	w4, [x1, x3, lsl 2]
	str	w4, [x0, x3, lsl 2]
	add	x3, x3, 1
	b	.L1620
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
	stp	x29, x30, [sp, -16]!
	adrp	x0, jiffies
	add	x29, sp, 0
	ldr	x1, [x0, #:lo12:jiffies]
	adrp	x0, .LANCHOR5+224
	ldr	x0, [x0, #:lo12:.LANCHOR5+224]
	sub	x0, x1, x0
	bl	jiffies_to_msecs
	ldp	x29, x30, [sp], 16
	ret
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	stp	x29, x30, [sp, -32]!
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x1
	mov	w1, 0
	mov	w20, w0
	mov	x0, x19
	bl	ftl_memset
	bl	rknand_device_lock
	adrp	x0, .LANCHOR5+232
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x3, [x0, 24]
	mov	w0, w20
	blr	x3
	mov	w19, w0
	bl	rknand_device_unlock
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	mov	x20, x1
	bl	rknand_device_lock
	adrp	x0, .LANCHOR5+232
	mov	x2, x20
	mov	w1, 1
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x3, [x0, 32]
	mov	w0, w19
	blr	x3
	mov	w19, w0
	bl	rknand_device_unlock
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	mov	w20, w1
	str	x21, [sp, 32]
	mov	x21, x2
	bl	rknand_device_lock
	adrp	x0, .LANCHOR5+232
	mov	x2, x21
	mov	w1, w20
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x3, [x0, 8]
	mov	w0, w19
	blr	x3
	mov	w19, w0
	bl	rknand_device_unlock
	ldr	x21, [sp, 32]
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	mov	w20, w1
	str	x21, [sp, 32]
	mov	x21, x2
	bl	rknand_device_lock
	adrp	x0, .LANCHOR5+232
	mov	x2, x21
	mov	w1, w20
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x3, [x0, 16]
	mov	w0, w19
	blr	x3
	mov	w19, w0
	bl	rknand_device_unlock
	ldr	x21, [sp, 32]
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	flash_sram_load_store
	.type	flash_sram_load_store, %function
flash_sram_load_store:
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR5+240
	uxtw	x5, w1
	add	x29, sp, 0
	ldr	x4, [x4, #:lo12:.LANCHOR5+240]
	add	x4, x4, 4096
	cbnz	w2, .L1635
	mov	w2, w3
	add	x1, x4, x5
.L1638:
	bl	ftl_memcpy
	ldp	x29, x30, [sp], 16
	ret
.L1635:
	mov	x1, x0
	mov	w2, w3
	add	x0, x4, x5
	b	.L1638
	.size	flash_sram_load_store, .-flash_sram_load_store
	.align	2
	.global	FlashCs123Init
	.type	FlashCs123Init, %function
FlashCs123Init:
	ret
	.size	FlashCs123Init, .-FlashCs123Init
	.align	2
	.global	ftl_dma32_malloc
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	stp	x29, x30, [sp, -48]!
	cmp	w0, 8192
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	ble	.L1641
	bl	ftl_malloc
.L1640:
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L1641:
	adrp	x1, .LANCHOR5
	add	x21, x1, :lo12:.LANCHOR5
	add	w19, w0, 63
	mov	x20, x1
	and	w19, w19, -64
	ldr	w0, [x21, 248]
	cmp	w19, w0
	ble	.L1643
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x21, 256]
	mov	w0, 16384
	str	w0, [x21, 248]
.L1643:
	add	x1, x20, :lo12:.LANCHOR5
	ldr	w0, [x1, 248]
	sub	w0, w0, w19
	str	w0, [x1, 248]
	ldr	x0, [x1, 256]
	add	x19, x0, w19, sxtw
	str	x19, [x1, 256]
	b	.L1640
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	2
	.global	nandc_init
	.type	nandc_init, %function
nandc_init:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	mov	x22, x0
	stp	x19, x20, [sp, 16]
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	str	wzr, [x29, 56]
	tbz	x0, 12, .L1646
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	adrp	x0, .LC136
	mov	x2, x22
	add	x1, x1, 136
	add	x0, x0, :lo12:.LC136
	bl	printk
.L1646:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ldr	w3, [x22, 352]
	mov	w2, 6
	mov	x20, x0
	strb	w2, [x1, 1028]
	mov	w2, 12336
	str	x22, [x1, 1056]
	movk	w2, 0x5638, lsl 16
	cmp	w3, w2
	bne	.L1647
	mov	w0, 8
	strb	w0, [x1, 1028]
.L1647:
	ldr	w1, [x22, 128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L1648
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 9
	strb	w1, [x0, 1028]
.L1648:
	add	x19, x20, :lo12:.LANCHOR0
	ldrb	w0, [x19, 1028]
	cmp	w0, 9
	bne	.L1649
	mov	w0, 1
	strb	w0, [x19, 1251]
	ldr	w0, [x29, 56]
	mov	w1, 2
	orr	w0, w0, 256
	str	w0, [x29, 56]
	ldr	w0, [x29, 56]
	bfi	w0, w1, 18, 3
	str	w0, [x29, 56]
	mov	w1, 4161
	ldr	w0, [x29, 56]
	str	w0, [x22]
	ldr	x0, [x19, 1056]
	str	wzr, [x0, 520]
	str	w1, [x0, 4]
	mov	w1, 8321
	ldr	x0, [x19, 1056]
	str	w1, [x0, 8]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0, 80]
	mov	w1, 38
	str	w1, [x0, 84]
	mov	w1, 39
	str	w1, [x0, 84]
	ldr	w1, [x21, #:lo12:.LANCHOR2]
	tbz	x1, 12, .L1651
	ldr	w1, [x0]
	ldr	w2, [x0, 8]
	ldr	w3, [x0, 80]
	ldr	w4, [x0, 84]
	ldr	w5, [x0, 88]
.L1664:
	adrp	x0, .LC137
	add	x0, x0, :lo12:.LC137
	bl	printk
.L1651:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 1
	strb	w1, [x0, 1252]
	ldr	w1, [x21, #:lo12:.LANCHOR2]
	strh	wzr, [x0, 1296]
	strb	wzr, [x0, 1249]
	tbz	x1, 12, .L1645
	ldrb	w1, [x0, 1028]
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	bl	printk
.L1645:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1649:
	ldr	w0, [x29, 56]
	mov	w1, 1
	strb	wzr, [x19, 1251]
	orr	w0, w0, 256
	str	w0, [x29, 56]
	ldr	w0, [x29, 56]
	bfi	w0, w1, 24, 3
	str	w0, [x29, 56]
	mov	w1, 4193
	ldr	w0, [x29, 56]
	str	w0, [x22]
	ldr	x0, [x19, 1056]
	str	wzr, [x0, 336]
	str	w1, [x0, 4]
	mov	w1, 8321
	ldr	x0, [x19, 1056]
	str	w1, [x0, 344]
	mov	w1, 4099
	movk	w1, 0x10, lsl 16
	str	w1, [x0, 304]
	mov	w1, 38
	str	w1, [x0, 308]
	mov	w1, 39
	str	w1, [x0, 308]
	mov	w0, 2048
	bl	ftl_dma32_malloc
	str	x0, [x19, 1256]
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L1651
	ldr	x0, [x19, 1056]
	ldr	w1, [x0]
	ldr	w2, [x0, 344]
	ldr	w3, [x0, 304]
	ldr	w4, [x0, 308]
	ldr	w5, [x0, 312]
	b	.L1664
	.size	nandc_init, .-nandc_init
	.align	2
	.global	zbuf_init
	.type	zbuf_init, %function
zbuf_init:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x19, x0, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x19, x19, 1304
	adrp	x22, .LANCHOR3
	mov	x20, x0
	add	x22, x22, :lo12:.LANCHOR3
	mov	w21, 0
.L1666:
	and	w0, w21, 255
	strb	w0, [x19, 1]
	add	w1, w0, 1
	ldrb	w0, [x22, 1946]
	strb	w1, [x19]
	add	w21, w21, 1
	strb	wzr, [x19, 2]
	add	x19, x19, 64
	str	xzr, [x19, -48]
	lsl	w0, w0, 9
	bl	ftl_dma32_malloc
	str	x0, [x19, -56]
	mov	w0, 64
	bl	ftl_dma32_malloc
	str	x0, [x19, -40]
	cmp	w21, 32
	bne	.L1666
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, -1
	strb	wzr, [x0, 3352]
	strb	w1, [x0, 3288]
	strb	w21, [x0, 3353]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	zbuf_init, .-zbuf_init
	.align	2
	.global	gc_init
	.type	gc_init, %function
gc_init:
	stp	x29, x30, [sp, -48]!
	mov	w2, 2216
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR3
	stp	x21, x22, [sp, 32]
	add	x19, x19, :lo12:.LANCHOR3
	adrp	x21, .LANCHOR5
	add	x21, x21, :lo12:.LANCHOR5
	adrp	x20, .LANCHOR0
	add	x20, x20, :lo12:.LANCHOR0
	add	x22, x20, 3416
	strb	wzr, [x19, 1337]
	strb	wzr, [x21, 264]
	mov	x0, x22
	strh	wzr, [x19, 1378]
	str	wzr, [x21, 268]
	bl	ftl_memset
	ldrh	w1, [x19, 1376]
	mov	w0, -1
	ldrb	w3, [x19, 1321]
	strh	w0, [x20, 3416]
	lsr	w0, w1, 2
	lsr	w2, w1, 1
	strh	w0, [x20, 3452]
	strh	w0, [x20, 3402]
	ldrh	w0, [x19, 1338]
	strh	w2, [x20, 3454]
	mul	w2, w1, w3
	strh	w1, [x20, 3400]
	mov	w1, 4
	and	w2, w2, 65535
	strh	w1, [x21, 272]
	sub	w4, w2, #32
	mul	w0, w0, w3
	strh	w4, [x20, 3404]
	strh	w2, [x20, 3406]
	strh	wzr, [x19, 1352]
	lsl	w0, w0, 2
	strh	wzr, [x19, 1354]
	strh	wzr, [x19, 1356]
	str	xzr, [x20, 3424]
	bl	ftl_dma32_malloc
	ldrb	w1, [x19, 1321]
	str	x0, [x21, 280]
	ldrh	w0, [x19, 1338]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_dma32_malloc
	ldrh	w1, [x19, 1338]
	str	x0, [x21, 288]
	ldrb	w0, [x19, 1321]
	mul	w0, w1, w0
	bl	ftl_dma32_malloc
	str	x0, [x19, 1328]
	ldrb	w1, [x19, 1321]
	ldrh	w0, [x19, 1338]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_dma32_malloc
	ldrb	w1, [x19, 1321]
	str	x0, [x19, 1312]
	ldrh	w0, [x19, 1338]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_dma32_malloc
	str	x0, [x21, 296]
	ldrh	w0, [x19, 1380]
	ldp	x21, x22, [sp, 32]
	lsr	w0, w0, 2
	strh	w0, [x20, 3392]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	gc_init, .-gc_init
	.align	2
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	stp	x29, x30, [sp, -16]!
	mov	w1, 0
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
	add	x29, sp, 0
	bl	printk
	adrp	x0, .LANCHOR5+232
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x0, [x0, 80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x1, [x0, 64]
	mov	w0, 0
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x0, [x0, 88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x0, [x0, 96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x1, [x0, 72]
	mov	w0, 0
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x1, [x1, #:lo12:.LANCHOR5+232]
	ldr	x1, [x1, 120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x1, [x1, #:lo12:.LANCHOR5+232]
	ldr	x1, [x1, 128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR5+232
	and	w0, w0, 255
	add	x29, sp, 0
	ldr	x4, [x4, #:lo12:.LANCHOR5+232]
	ldr	x4, [x4, 40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	stp	x29, x30, [sp, -16]!
	adrp	x2, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR5+232]
	ldr	x2, [x2, 56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	stp	x29, x30, [sp, -16]!
	adrp	x2, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x2, [x2, #:lo12:.LANCHOR5+232]
	ldr	x2, [x2, 104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR5+232
	add	x29, sp, 0
	ldr	x1, [x1, #:lo12:.LANCHOR5+232]
	ldr	x1, [x1, 112]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	mul	w6, w2, w3
	stp	x29, x30, [sp, -16]!
	mov	x5, x1
	mov	w4, w2
	add	x29, sp, 0
	mov	x1, x0
	mov	w7, 0
	mov	w3, 16
	mov	w2, 0
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	bl	print_hex_dump
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	hynix_get_read_retry_default
	.type	hynix_get_read_retry_default, %function
hynix_get_read_retry_default:
	stp	x29, x30, [sp, -144]!
	mov	w4, -83
	mov	w1, -82
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w23, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	stp	x21, x22, [sp, 32]
	cmp	w23, 2
	stp	x27, x28, [sp, 80]
	ldr	x26, [x0, 1048]
	add	x0, x26, 112
	str	x0, [x29, 136]
	mov	w0, -84
	add	x25, x26, 128
	strb	w0, [x26, 128]
	mov	w0, -81
	strb	w23, [x26, 112]
	strb	w4, [x26, 129]
	strb	w1, [x26, 130]
	strb	w0, [x26, 131]
	bne	.L1696
	mov	w0, -89
	strb	w0, [x26, 128]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0, #:lo12:.LANCHOR2+425]
.L1761:
	mov	w28, 7
	b	.L1806
.L1696:
	cmp	w23, 3
	bne	.L1698
	mov	x4, 0
.L1699:
	sub	w0, w4, #80
	strb	w0, [x25, x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L1699
	mov	w28, w4
	mov	w27, w4
.L1697:
	sub	w0, w23, #1
	cmp	w0, 1
	bhi	.L1705
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	add	x23, x20, 1024
	add	x21, x21, 408
	mov	w26, 0
.L1706:
	ldrb	w0, [x20, 1153]
	cmp	w0, w26
	bhi	.L1712
.L1713:
	ldr	x0, [x29, 136]
	strb	w27, [x0, 1]
	strb	w28, [x0, 2]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1698:
	cmp	w23, 4
	bne	.L1700
	mov	w5, -52
	strb	w5, [x26, 128]
	mov	w5, -65
	strb	w5, [x26, 129]
	mov	w5, -86
	strb	w5, [x26, 130]
	mov	w5, -85
	mov	w28, 8
	strb	w5, [x26, 131]
	mov	w27, w28
	mov	w5, -51
	strb	w4, [x26, 133]
	strb	w5, [x26, 132]
	strb	w1, [x26, 134]
	strb	w0, [x26, 135]
	b	.L1697
.L1700:
	cmp	w23, 5
	bne	.L1701
	mov	w0, 56
	strb	w0, [x26, 128]
	mov	w0, 57
	strb	w0, [x26, 129]
	mov	w0, 58
	mov	w28, 8
	strb	w0, [x26, 130]
	mov	w0, 59
	strb	w0, [x26, 131]
.L1806:
	mov	w27, 4
	b	.L1697
.L1701:
	cmp	w23, 6
	bne	.L1702
	mov	w0, 14
	strb	w0, [x26, 128]
	mov	w0, 15
	strb	w0, [x26, 129]
	mov	w0, 16
	mov	w28, 12
	strb	w0, [x26, 130]
	mov	w0, 17
	strb	w0, [x26, 131]
	b	.L1806
.L1702:
	cmp	w23, 7
	bne	.L1703
	mov	x0, 0
.L1704:
	sub	w1, w0, #80
	strb	w1, [x25, x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L1704
	mov	w0, -44
	mov	w28, 12
	strb	w0, [x26, 136]
	mov	w27, 10
	mov	w0, -43
	strb	w0, [x26, 137]
	b	.L1697
.L1703:
	cmp	w23, 8
	bne	.L1761
	mov	w0, 6
	strb	w0, [x26, 128]
	mov	w0, 7
	strb	w0, [x26, 129]
	mov	w0, 9
	strb	w23, [x26, 130]
	strb	w0, [x26, 131]
	mov	w28, 50
	mov	w0, 10
	mov	w27, 5
	strb	w0, [x26, 132]
	b	.L1697
.L1712:
	add	x0, x23, w26, sxtw
	mov	x1, 32
	mov	w19, 160
	ldr	x24, [x20, 1056]
	mov	x22, 0
	ldrb	w0, [x0, 172]
	umaddl	x19, w19, w0, x1
	ldr	x1, [x29, 136]
	ubfiz	x0, x0, 8, 8
	add	x24, x24, x0
	add	x19, x1, x19
	mov	w1, 55
.L1707:
	str	w1, [x24, 2056]
	str	w1, [x29, 132]
	ldrb	w0, [x25, x22]
	str	w0, [x24, 2052]
	mov	x0, 1000
	bl	__const_udelay
	ldr	w0, [x24, 2048]
	strb	w0, [x19, x22]
	add	x22, x22, 1
	cmp	w27, w22, uxtb
	ldr	w1, [x29, 132]
	bhi	.L1707
	mov	x0, 0
.L1710:
	add	x1, x0, 4
	add	x2, x0, 28
	add	w3, w0, 8
	add	x1, x21, x1
	add	x2, x21, x2
.L1709:
	ldrb	w7, [x1], 4
	ldrb	w6, [x19, x0]
	cmp	x2, x1
	add	w6, w6, w7
	strb	w6, [x19, w3, sxtw]
	add	w3, w3, 8
	bne	.L1709
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1710
	add	w26, w26, 1
	strb	wzr, [x19, 16]
	strb	wzr, [x19, 24]
	and	w26, w26, 255
	strb	wzr, [x19, 32]
	strb	wzr, [x19, 40]
	strb	wzr, [x19, 48]
	strb	wzr, [x19, 41]
	strb	wzr, [x19, 49]
	b	.L1706
.L1705:
	sub	w0, w23, #3
	cmp	w0, 5
	bhi	.L1713
	mul	w0, w27, w28
	add	x20, x20, :lo12:.LANCHOR0
	sub	w25, w27, #1
	mov	w21, 0
	and	x25, x25, 255
	asr	w24, w0, 1
	lsl	w0, w0, 4
	str	w0, [x29, 128]
	lsl	w0, w24, 1
	str	w0, [x29, 132]
	add	x0, x20, 1024
	str	x0, [x29, 120]
	add	x0, x25, 1
	str	x0, [x29, 112]
.L1714:
	ldrb	w0, [x20, 1153]
	cmp	w0, w21
	bls	.L1713
	ldr	x0, [x29, 120]
	mov	w22, 160
	sub	w25, w23, #5
	add	x0, x0, w21, sxtw
	ldrb	w19, [x0, 172]
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	mov	x0, 32
	mov	w1, 255
	str	w1, [x29, 108]
	nop // between mem op and mult-accumulate
	umaddl	x22, w22, w19, x0
	ldr	x0, [x29, 136]
	ubfiz	x19, x19, 8, 8
	add	x22, x0, x22
	ldr	x0, [x20, 1056]
	add	x19, x0, x19
	str	w1, [x19, 2056]
	bl	nandc_wait_flash_ready
	cmp	w23, 8
	ldr	w1, [x29, 108]
	bne	.L1715
	mov	w0, 120
	str	w0, [x19, 2056]
	str	wzr, [x19, 2052]
	mov	w0, 23
	str	wzr, [x19, 2052]
	mov	w1, 25
	str	wzr, [x19, 2052]
	add	x22, x26, 144
	str	w0, [x19, 2056]
	mov	w0, 4
	str	w0, [x19, 2056]
	str	w1, [x19, 2056]
	mov	w1, 218
	str	w1, [x19, 2056]
	mov	w1, 21
	str	wzr, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w1, [x19, 2052]
.L1809:
	str	w0, [x19, 2052]
	mov	w0, 48
	str	wzr, [x19, 2052]
	str	w0, [x19, 2056]
	bl	nandc_wait_flash_ready
	cmp	w25, 1
	ccmp	w23, 8, 4, hi
	beq	.L1762
	cmp	w23, 7
	mov	w0, 32
	mov	w1, 2
	csel	w1, w1, w0, ne
.L1722:
	adrp	x8, .LANCHOR5
	add	x0, x8, :lo12:.LANCHOR5
	mov	x7, 0
	ldr	x0, [x0, 304]
.L1723:
	ldr	w9, [x19, 2048]
	strb	w9, [x0, x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L1723
	cmp	w23, 8
	bne	.L1724
	mov	w1, 0
.L1726:
	ldrb	w7, [x0]
	cmp	w7, 50
	beq	.L1725
	ldrb	w7, [x0, 1]
	cmp	w7, 5
	beq	.L1725
	add	w1, w1, 1
	add	x0, x0, 4
	and	w1, w1, 255
	cmp	w1, 8
	bne	.L1726
.L1727:
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1729:
	b	.L1729
.L1715:
	mov	w0, 54
	str	w0, [x19, 2056]
	cmp	w23, 4
	bne	.L1717
	mov	w0, 64
	str	w1, [x19, 2052]
	str	w0, [x19, 2048]
	mov	w0, 204
.L1807:
	str	w0, [x19, 2052]
	mov	w0, 77
	b	.L1808
.L1717:
	cmp	w25, 1
	bhi	.L1719
	ldrb	w0, [x26, 128]
	str	w0, [x19, 2052]
	mov	w0, 82
.L1808:
	str	w0, [x19, 2048]
.L1718:
	mov	w0, 22
	str	w0, [x19, 2056]
	mov	w0, 23
	str	w0, [x19, 2056]
	mov	w0, 4
	str	w0, [x19, 2056]
	mov	w0, 25
	str	w0, [x19, 2056]
	str	wzr, [x19, 2056]
	cmp	w23, 6
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	bne	.L1720
	mov	w0, 31
	str	w0, [x19, 2052]
.L1721:
	mov	w0, 2
	b	.L1809
.L1719:
	cmp	w23, 7
	bne	.L1718
	mov	w0, 174
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 176
	b	.L1807
.L1720:
	str	wzr, [x19, 2052]
	b	.L1721
.L1762:
	mov	w1, 16
	b	.L1722
.L1725:
	cmp	w1, 6
	bhi	.L1727
.L1728:
	add	x0, x8, :lo12:.LANCHOR5
	ldr	x10, [x0, 304]
	mov	x0, 0
.L1738:
	ldr	w1, [x29, 128]
	cmp	w1, w0
	bgt	.L1739
	add	x0, x8, :lo12:.LANCHOR5
	mov	w9, w24
	mov	w7, 8
	ldr	x11, [x0, 304]
.L1741:
	mov	w0, 0
.L1740:
	add	w1, w0, w9
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w24, w0
	ldrh	w12, [x11, x1]
	mvn	w12, w12
	strh	w12, [x11, x1]
	bgt	.L1740
	ldr	w0, [x29, 132]
	subs	w7, w7, #1
	add	w9, w9, w0
	bne	.L1741
	mov	x7, 0
	mov	w15, 1
.L1747:
	mov	w1, 0
	mov	w0, 0
.L1746:
	mov	w13, w7
	lsl	w14, w15, w0
	mov	w9, 16
	mov	w12, 0
.L1744:
	ldrh	w16, [x11, w13, sxtw 1]
	add	w13, w13, w24
	bics	wzr, w14, w16
	cinc	w12, w12, eq
	subs	w9, w9, #1
	bne	.L1744
	cmp	w12, 8
	bls	.L1745
	orr	w1, w1, w14
	and	w1, w1, 65535
.L1745:
	add	w0, w0, 1
	cmp	w0, 16
	bne	.L1746
	strh	w1, [x11, x7, lsl 1]
	add	x7, x7, 1
	cmp	w24, w7
	bgt	.L1747
	add	x0, x8, :lo12:.LANCHOR5
	mov	w7, 0
	ldr	x1, [x0, 304]
	mov	x0, 0
.L1750:
	ldr	w8, [x1, x0]
	add	x0, x0, 4
	cmp	w8, 0
	cinc	w7, w7, eq
	cmp	x0, 32
	bne	.L1750
	cmp	w7, 7
	ble	.L1751
	mov	w3, 1024
	mov	w2, 1
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
	bl	rknand_print_hex
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1752:
	b	.L1752
.L1724:
	cmp	w23, 7
	bne	.L1730
	mov	w1, 0
.L1732:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L1731
	ldrb	w7, [x0, 1]
	cmp	w7, 10
	beq	.L1731
	add	w1, w1, 1
	add	x0, x0, 4
	and	w1, w1, 255
	cmp	w1, 8
	bne	.L1732
.L1733:
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1734:
	b	.L1734
.L1731:
	cmp	w1, 6
	bls	.L1728
	b	.L1733
.L1730:
	cmp	w23, 6
	bne	.L1728
	mov	x1, 0
.L1735:
	ldrb	w7, [x0, x1]
	cmp	w7, 12
	beq	.L1728
	add	x7, x0, x1
	ldrb	w7, [x7, 8]
	cmp	w7, 4
	beq	.L1728
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L1735
	adrp	x0, .LC141
	mov	w1, 0
	add	x0, x0, :lo12:.LC141
	bl	printk
.L1737:
	b	.L1737
.L1739:
	ldr	w1, [x19, 2048]
	strb	w1, [x10, x0]
	add	x0, x0, 1
	b	.L1738
.L1751:
	cmp	w23, 6
	beq	.L1764
	cmp	w23, 7
	beq	.L1765
	cmp	w23, 8
	mov	w0, 5
	mov	w1, 8
	csel	w1, w1, w0, ne
.L1753:
	mov	w7, 0
.L1754:
	mov	x0, 0
.L1755:
	add	w8, w9, w0
	ldrb	w11, [x10, x0]
	add	x0, x0, 1
	cmp	w27, w0, uxtb
	strb	w11, [x22, w8, sxtw]
	bhi	.L1755
	ldr	x0, [x29, 112]
	add	w7, w7, 1
	add	w9, w9, w1
	cmp	w28, w7
	add	x10, x10, x0
	bgt	.L1754
	mov	w22, 255
	str	w22, [x19, 2056]
	bl	nandc_wait_flash_ready
	cmp	w25, 1
	bhi	.L1757
	mov	w0, 54
	str	w0, [x19, 2056]
	ldrb	w0, [x26, 128]
	str	w0, [x19, 2052]
	str	wzr, [x19, 2048]
	mov	w0, 22
	str	w0, [x19, 2056]
	mov	w0, 48
	str	wzr, [x19, 2056]
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w22, [x19, 2052]
	str	w22, [x19, 2052]
	str	w22, [x19, 2052]
.L1810:
	str	w0, [x19, 2056]
	add	w21, w21, 1
	and	w21, w21, 255
	bl	nandc_wait_flash_ready
	b	.L1714
.L1764:
	mov	w1, 4
	b	.L1753
.L1765:
	mov	w1, 10
	b	.L1753
.L1757:
	cmp	w23, 8
	bne	.L1759
	mov	w0, 190
	b	.L1810
.L1759:
	mov	w0, 56
	b	.L1810
	.size	hynix_get_read_retry_default, .-hynix_get_read_retry_default
	.align	2
	.global	flash_get_read_retry_tbl
	.type	flash_get_read_retry_tbl, %function
flash_get_read_retry_tbl:
	adrp	x0, .LANCHOR2+27
	ldrb	w0, [x0, #:lo12:.LANCHOR2+27]
	sub	w1, w0, #1
	and	w1, w1, 255
	cmp	w1, 7
	bhi	.L1814
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	hynix_get_read_retry_default
	ldp	x29, x30, [sp], 16
	ret
.L1814:
	ret
	.size	flash_get_read_retry_tbl, .-flash_get_read_retry_tbl
	.align	2
	.global	nandc_xfer_done
	.type	nandc_xfer_done, %function
nandc_xfer_done:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR5
	add	x0, x21, :lo12:.LANCHOR5
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	adrp	x20, .LANCHOR0
	strb	wzr, [x0, 312]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1028]
	cmp	w1, 9
	bne	.L1818
	ldr	x22, [x0, 1056]
	ldr	w1, [x22, 16]
	str	w1, [x29, 64]
	ldr	w1, [x22, 48]
	tbnz	x1, 1, .L1819
	adrp	x23, .LC145
	adrp	x24, .LC146
	mov	x19, x0
	add	x23, x23, :lo12:.LC145
	add	x24, x24, :lo12:.LC146
	mov	w21, 0
.L1820:
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L1830
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1256
	ldr	w0, [x19, 32]
	cbz	w0, .L1828
	ldr	w1, [x29, 64]
	mov	w2, 1
	ldr	w0, [x19, 24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 1
	ubfx	x1, x1, 22, 6
	b	.L1865
.L1819:
	mov	x19, x0
	mov	w23, 0
.L1821:
	ldr	w1, [x22, 64]
	ldr	w0, [x29, 64]
	ubfx	x1, x1, 16, 6
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1823
	ldr	x0, [x19, 1056]
	ldr	w0, [x0]
	str	w0, [x29, 72]
	ldr	w0, [x29, 72]
	tbz	x0, 13, .L1822
	ldr	w0, [x29, 72]
	tbz	x0, 17, .L1822
	ldr	w1, [x29, 72]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	ubfx	x1, x1, 17, 1
	bl	printk
.L1823:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1256
	ldr	w0, [x19, 32]
	cbz	w0, .L1828
	ldr	w1, [x29, 64]
	mov	w2, 0
	ldr	w0, [x19, 24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 0
	ubfx	x1, x1, 22, 6
.L1865:
	lsl	w1, w1, 2
.L1863:
	ldr	w0, [x19, 28]
	bl	rknand_dma_unmap_single
.L1828:
	add	x20, x20, :lo12:.LANCHOR0
	ldp	x21, x22, [sp, 32]
	str	wzr, [x20, 1288]
	ldp	x23, x24, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 80
	ret
.L1822:
	ldr	w0, [x29, 64]
	add	w23, w23, 1
	ubfx	x0, x0, 22, 6
	cmp	w23, w0, lsl 12
	bne	.L1824
	ldr	w2, [x22, 64]
	mov	w1, w23
	ldr	w3, [x29, 64]
	adrp	x0, .LC144
	add	x21, x21, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC144
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	ldr	w1, [x29, 72]
	mov	w0, 1
	strb	w0, [x21, 312]
	tbnz	x1, 13, .L1823
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	b	.L1823
.L1824:
	mov	x1, 10
	mov	x0, 5
	bl	usleep_range
	b	.L1821
.L1830:
	ldr	x0, [x19, 1056]
	add	w21, w21, 1
	tst	x21, 16777215
	ldr	w0, [x0, 16]
	str	w0, [x29, 64]
	bne	.L1829
	ldr	w2, [x29, 64]
	mov	w1, w21
	ldr	w3, [x22, 64]
	mov	x0, x23
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x19, 1056]
	mov	w3, 64
	mov	w2, 4
	mov	x0, x24
	bl	rknand_print_hex
.L1829:
	mov	x1, 10
	mov	x0, 5
	bl	usleep_range
	b	.L1820
.L1818:
	ldr	x21, [x0, 1056]
	mov	w22, 0
	mov	x19, x0
	ldr	w1, [x21, 8]
	str	w1, [x29, 64]
	ldr	w1, [x21, 16]
	tbnz	x1, 1, .L1832
	adrp	x23, .LC145
	adrp	x24, .LC146
	add	x23, x23, :lo12:.LC145
	add	x24, x24, :lo12:.LC146
.L1833:
	ldr	w0, [x29, 64]
	tbz	x0, 20, .L1840
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1256
	ldr	w0, [x19, 32]
	cbz	w0, .L1828
	ldr	w1, [x29, 64]
	mov	w2, 1
	ldr	w0, [x19, 24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 1
	ubfx	x1, x1, 22, 6
	b	.L1864
.L1832:
	adrp	x23, .LC144
	adrp	x24, .LC146
	add	x23, x23, :lo12:.LC144
	add	x24, x24, :lo12:.LC146
.L1834:
	ldr	w1, [x21, 28]
	ldr	w0, [x29, 64]
	ubfx	x1, x1, 16, 5
	ubfx	x0, x0, 22, 6
	cmp	w1, w0
	bge	.L1836
	ldr	x0, [x19, 1056]
	ldr	w0, [x0]
	str	w0, [x29, 72]
	ldr	w0, [x29, 72]
	tbz	x0, 13, .L1835
	ldr	w0, [x29, 72]
	tbz	x0, 17, .L1835
	ldr	w1, [x29, 72]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	bl	printk
.L1836:
	add	x19, x20, :lo12:.LANCHOR0
	add	x19, x19, 1256
	ldr	w0, [x19, 32]
	cbz	w0, .L1828
	ldr	w1, [x29, 64]
	mov	w2, 0
	ldr	w0, [x19, 24]
	ubfx	x1, x1, 22, 6
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29, 64]
	mov	w2, 0
	ubfx	x1, x1, 22, 6
.L1864:
	lsl	w1, w1, 7
	b	.L1863
.L1835:
	add	w22, w22, 1
	tst	x22, 16777215
	bne	.L1837
	ldr	w2, [x21, 28]
	mov	w1, w22
	ldr	w3, [x29, 64]
	mov	x0, x23
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	ldr	x1, [x19, 1056]
	mov	w3, 64
	mov	w2, 4
	mov	x0, x24
	bl	rknand_print_hex
.L1837:
	mov	x1, 10
	mov	x0, 5
	bl	usleep_range
	b	.L1834
.L1840:
	ldr	x0, [x19, 1056]
	add	w22, w22, 1
	tst	x22, 16777215
	ldr	w0, [x0, 8]
	str	w0, [x29, 64]
	bne	.L1839
	ldr	w2, [x29, 64]
	mov	w1, w22
	ldr	w3, [x21, 28]
	mov	x0, x23
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x19, 1056]
	mov	w3, 64
	mov	w2, 4
	mov	x0, x24
	bl	rknand_print_hex
.L1839:
	mov	x1, 10
	mov	x0, 5
	bl	usleep_range
	b	.L1833
	.size	nandc_xfer_done, .-nandc_xfer_done
	.align	2
	.global	nandc_xfer
	.type	nandc_xfer, %function
nandc_xfer:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w2, 255
	stp	x21, x22, [sp, 32]
	mov	x21, x3
	and	w19, w1, 255
	mov	x3, x4
	mov	x2, x21
	mov	w1, w20
	mov	w0, w19
	mov	x22, x4
	bl	nandc_xfer_start
	mov	w0, w19
	bl	nandc_xfer_done
	cbnz	w19, .L1885
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1028]
	cmp	w1, 9
	bne	.L1868
	lsr	w20, w20, 2
	ldr	x5, [x0, 1056]
	mov	w3, 1
	mov	w2, 0
	mov	w0, 0
.L1869:
	cmp	w2, w20
	bcc	.L1873
	ldr	w20, [x5]
	cmp	w3, 0
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1875
	mov	w1, w20
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	orr	w20, w20, 131072
	ldr	x0, [x0, 1056]
	str	w20, [x0]
	mov	w0, -1
.L1875:
	tbz	x20, 13, .L1876
	adrp	x1, .LANCHOR5+312
	ldrb	w1, [x1, #:lo12:.LANCHOR5+312]
	cbz	w1, .L1876
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w20
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	bl	printk
	ldr	x0, [x19, 1056]
	mov	w1, 1
	str	w1, [x0, 16]
.L1904:
	mov	w0, -1
.L1867:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L1873:
	uxtw	x1, w2
	add	x1, x1, 84
	ldr	w1, [x5, x1, lsl 2]
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	ldr	w4, [x29, 56]
	ubfx	x4, x4, 26, 1
	and	w1, w4, w1, lsr 10
	and	w3, w3, w1
	ldr	w1, [x29, 56]
	tbnz	x1, 2, .L1887
	ldr	w1, [x29, 56]
	tbnz	x1, 18, .L1887
	ldr	w4, [x29, 56]
	ldr	w1, [x29, 56]
	ubfx	x4, x4, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w4, w1
	ldr	w1, [x29, 56]
	ble	.L1871
	ubfx	x1, x1, 3, 7
.L1872:
	cmp	w0, w1
	csel	w0, w0, w1, cs
.L1870:
	add	w2, w2, 1
	b	.L1869
.L1871:
	ubfx	x1, x1, 19, 7
	b	.L1872
.L1887:
	mov	w0, -1
	b	.L1870
.L1868:
	ldrb	w1, [x0, 1249]
	lsr	w5, w20, 1
	mov	w4, 64
	mov	w2, 1
	cmp	w1, 25
	mov	w1, 128
	mov	w3, 0
	csel	w4, w4, w1, cc
	mov	w1, 0
.L1878:
	add	w6, w4, w1
	cmp	w3, w5
	bcc	.L1879
	add	x0, x19, :lo12:.LANCHOR0
	lsr	w20, w20, 2
	mov	w3, 0
	ldr	x4, [x0, 1056]
	mov	w0, 0
.L1880:
	cmp	w3, w20
	bcc	.L1884
	str	wzr, [x4, 16]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	ldr	w20, [x4]
	and	w1, w20, w1
	cmp	w1, 139264
	bne	.L1876
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w20
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	bl	printk
	orr	w20, w20, 131072
	ldr	x0, [x19, 1056]
	str	w20, [x0]
	b	.L1904
.L1879:
	ldr	x7, [x0, 1256]
	and	x1, x1, 4294967292
	ldr	w1, [x7, x1]
	lsl	w7, w3, 2
	add	w3, w3, 1
	strb	w1, [x22, x7]
	lsr	w7, w1, 8
	strb	w7, [x22, w2, uxtw]
	add	w7, w2, 1
	lsr	w8, w1, 16
	lsr	w1, w1, 24
	strb	w8, [x22, x7]
	add	w7, w2, 2
	add	w2, w2, 4
	strb	w1, [x22, x7]
	mov	w1, w6
	b	.L1878
.L1884:
	uxtw	x1, w3
	add	x1, x1, 8
	ldr	w1, [x4, x1, lsl 2]
	str	w1, [x29, 56]
	ldr	w1, [x29, 56]
	tbnz	x1, 2, .L1890
	ldr	w1, [x29, 56]
	tbnz	x1, 15, .L1890
	ldr	w2, [x29, 56]
	ubfx	x6, x2, 3, 5
	ldr	w2, [x29, 56]
	ldr	w1, [x29, 56]
	ubfx	x2, x2, 27, 1
	ubfx	x5, x1, 16, 5
	ldr	w1, [x29, 56]
	orr	w2, w6, w2, lsl 5
	ubfx	x1, x1, 29, 1
	orr	w1, w5, w1, lsl 5
	cmp	w2, w1
	ldr	w1, [x29, 56]
	bls	.L1882
	ubfx	x2, x1, 3, 5
	ldr	w1, [x29, 56]
	ubfx	x1, x1, 27, 1
.L1903:
	orr	w1, w2, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
.L1881:
	add	w3, w3, 1
	b	.L1880
.L1882:
	ubfx	x2, x1, 16, 5
	ldr	w1, [x29, 56]
	ubfx	x1, x1, 29, 1
	b	.L1903
.L1890:
	mov	w0, -1
	b	.L1881
.L1876:
	cmn	w0, #1
	beq	.L1867
	ldr	w1, [x22]
	cmn	w1, #1
	bne	.L1867
	ldr	w1, [x22, 4]
	cmn	w1, #1
	bne	.L1867
	ldr	w1, [x21]
	cmn	w1, #1
	mov	w1, 512
	csel	w0, w0, w1, ne
	b	.L1867
.L1885:
	mov	w0, 0
	b	.L1867
	.size	nandc_xfer, .-nandc_xfer
	.align	2
	.global	flash_read_page
	.type	flash_read_page, %function
flash_read_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	str	x27, [sp, 80]
	add	x27, x21, :lo12:.LANCHOR0
	and	w22, w0, 255
	stp	x19, x20, [sp, 16]
	mov	w19, 24
	stp	x23, x24, [sp, 48]
	ldrb	w0, [x27, 1205]
	ubfx	x23, x1, 24, 2
	stp	x25, x26, [sp, 64]
	mov	w24, w4
	sub	w0, w19, w0
	mov	w19, 1
	mov	x25, x2
	mov	x26, x3
	lsl	w19, w19, w0
	sub	w19, w19, #1
	and	w19, w19, w1
	ldr	x20, [x27, 1056]
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	nandc_cs
	cbnz	w23, .L1906
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
.L1907:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 1144]
	ldrb	w0, [x1, 7]
	cmp	w0, 1
	bne	.L1909
	ldrb	w0, [x1, 12]
	cmp	w0, 2
	bne	.L1909
	sxtw	x0, w22
	mov	w2, 38
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w2, [x0, 8]
.L1909:
	ubfiz	x0, x22, 8, 8
	add	x20, x20, x0
	and	w0, w19, 255
	str	wzr, [x20, 2056]
	str	wzr, [x20, 2052]
	str	wzr, [x20, 2052]
	str	w0, [x20, 2052]
	lsr	w0, w19, 8
	str	w0, [x20, 2052]
	lsr	w0, w19, 16
	str	w0, [x20, 2052]
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1204]
	cbz	w0, .L1910
	lsr	w0, w19, 24
	str	w0, [x20, 2052]
.L1910:
	mov	w0, 48
	str	w0, [x20, 2056]
	ldrb	w0, [x1, 12]
	cmp	w0, 3
	bne	.L1911
	cbz	w23, .L1911
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21, 1212]
	cbnz	w0, .L1911
	ldrb	w0, [x21, 1213]
	cbnz	w0, .L1911
	add	w19, w19, w19, lsl 1
	sub	w0, w19, #1
	add	w0, w0, w23
.L1920:
	bl	nandc_set_seed
	bl	nandc_wait_flash_ready
	mov	w0, 5
	str	w0, [x20, 2056]
	str	wzr, [x20, 2052]
	mov	w0, 224
	str	wzr, [x20, 2052]
	mov	w2, w24
	str	w0, [x20, 2056]
	mov	x4, x26
	mov	x3, x25
	mov	w1, 0
	mov	w0, w22
	bl	nandc_xfer
	mov	w2, w0
	bl	nandc_de_cs.constprop.35
	mov	w0, w2
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L1906:
	ldr	x0, [x27, 1144]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L1908
	ldrb	w0, [x27, 1212]
	cbnz	w0, .L1908
	ldrb	w0, [x27, 1213]
	cbnz	w0, .L1908
	sxtw	x0, w22
	add	x0, x0, 8
	add	x0, x20, x0, lsl 8
	str	w23, [x0, 8]
	b	.L1907
.L1908:
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
	b	.L1907
.L1911:
	mov	w0, w19
	b	.L1920
	.size	flash_read_page, .-flash_read_page
	.align	2
	.global	micron_read_retrial
	.type	micron_read_retrial, %function
micron_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	and	w20, w0, 255
	stp	x25, x26, [sp, 64]
	mov	w24, w1
	stp	x27, x28, [sp, 80]
	mov	x26, x3
	str	x2, [x29, 112]
	mov	w22, 0
	ldrb	w0, [x19, 1249]
	add	w0, w0, w0, lsl 1
	asr	w1, w0, 2
	stp	w4, w1, [x29, 120]
	bl	nandc_wait_flash_ready
	ldr	x19, [x19, 1056]
	ubfiz	x0, x20, 8, 8
	add	x19, x19, x0
.L1922:
	adrp	x23, .LANCHOR5
	add	x23, x23, :lo12:.LANCHOR5
	mov	x27, x23
	mov	w21, 0
	mov	w25, -1
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
.L1923:
	ldrb	w0, [x23, 328]
	cmp	w21, w0
	bcc	.L1928
.L1927:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 137
	str	w0, [x19, 2052]
	mov	x0, 1000
	bl	__const_udelay
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	ldr	w0, [x29, 124]
	str	wzr, [x19, 2048]
	cmp	w25, w0
	bcc	.L1929
	cmn	w25, #1
	mov	w0, 256
	csel	w25, w25, w0, eq
.L1929:
	cmn	w25, #1
	cset	w23, eq
	cmp	w25, 256
	cset	w0, eq
	orr	w0, w23, w0
	cbz	w0, .L1930
	mov	w4, w25
	mov	w3, w21
	mov	w2, w24
	mov	w1, w20
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	bl	printk
	eor	w0, w22, 1
	tst	w23, w0
	beq	.L1931
	mov	w1, 3
	mov	w0, w20
	mov	w22, 1
	bl	mt_auto_read_calibration_config
	b	.L1922
.L1928:
	mov	w0, 239
	str	w0, [x19, 2056]
	mov	w0, 137
	str	w0, [x19, 2052]
	add	w28, w21, 1
	mov	x0, 1000
	bl	__const_udelay
	str	w28, [x19, 2048]
	str	wzr, [x19, 2048]
	mov	x3, x26
	ldr	w4, [x29, 120]
	mov	w1, w24
	ldr	x2, [x29, 112]
	mov	w0, w20
	str	wzr, [x19, 2048]
	str	wzr, [x19, 2048]
	bl	flash_read_page
	mov	w6, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L1924
	mov	w4, w6
	str	w6, [x29, 108]
	mov	w3, w25
	mov	w2, w24
	mov	w1, w21
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	bl	printk
	ldr	w6, [x29, 108]
.L1924:
	cmn	w6, #1
	beq	.L1925
	ldr	x0, [x27, 304]
	cmn	w25, #1
	str	x0, [x29, 112]
	csel	w25, w25, w6, ne
	ldr	w0, [x29, 124]
	ldr	x26, [x27, 320]
	cmp	w6, w0
	bcc	.L1934
.L1925:
	mov	w21, w28
	b	.L1923
.L1934:
	mov	w25, w6
	b	.L1927
.L1931:
	cbz	w22, .L1932
	mov	w0, w20
	mov	w1, 0
	bl	mt_auto_read_calibration_config
	cmn	w25, #1
	mov	w0, 256
	csel	w25, w25, w0, eq
.L1932:
	bl	nandc_wait_flash_ready
	mov	w0, w25
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L1930:
	cbz	w22, .L1932
	mov	w1, 0
	mov	w0, w20
	mov	w25, 256
	bl	mt_auto_read_calibration_config
	b	.L1932
	.size	micron_read_retrial, .-micron_read_retrial
	.align	2
	.global	toshiba_3d_read_retrial
	.type	toshiba_3d_read_retrial, %function
toshiba_3d_read_retrial:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w24, w1
	stp	x25, x26, [sp, 64]
	and	w23, w0, 255
	stp	x27, x28, [sp, 80]
	mov	x26, x2
	stp	x19, x20, [sp, 16]
	mov	x27, x3
	mov	w28, w4
	bl	nandc_wait_flash_ready
	add	x0, x22, :lo12:.LANCHOR0
	and	x1, x23, 255
	add	x21, x1, 8
	str	x1, [x29, 120]
	mov	w2, 46
	mov	w3, 56
	ldrb	w1, [x0, 1136]
	ubfx	x19, x24, 24, 2
	ldr	x25, [x0, 1056]
	cmp	w1, 36
	mov	w1, 26
	csel	w2, w3, w2, ne
	str	w2, [x29, 112]
	mov	w2, 10
	csel	w1, w2, w1, ne
	str	w1, [x29, 128]
	add	x21, x25, x21, lsl 8
	cbnz	w19, .L1952
	str	x0, [x29, 136]
	sxtw	x0, w23
	add	x0, x0, 8
	mov	w19, -1
	add	x0, x25, x0, lsl 8
	mov	w20, 1
	str	x0, [x29, 112]
.L1959:
	ldr	x0, [x29, 136]
	ldrb	w0, [x0, 1136]
	cmp	w0, 36
	bne	.L1953
	mov	w1, w20
	mov	x0, x21
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x1, [x29, 112]
	mov	w0, 93
	str	w0, [x1, 8]
.L1954:
	mov	w4, w28
	mov	x3, x27
	mov	x2, x26
	mov	w1, w24
	mov	w0, w23
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L1955
	mov	w3, w4
	str	w4, [x29, 108]
	mov	w2, w24
	mov	w1, w20
	adrp	x0, .LC153
	add	x0, x0, :lo12:.LC153
	bl	printk
	ldr	w4, [x29, 108]
.L1955:
	cmn	w4, #1
	beq	.L1956
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w4, ne
	ldr	x26, [x0, 304]
	ldr	x27, [x0, 320]
	ldr	x0, [x29, 136]
	ldrb	w0, [x0, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1974
.L1956:
	ldr	w0, [x29, 128]
	add	w20, w20, 1
	cmp	w0, w20
	bne	.L1959
.L1958:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1136]
	cmp	w0, 36
	bne	.L1960
	mov	w2, 0
.L1997:
	mov	w1, 0
	mov	x0, x21
	bl	toshiba_tlc_set_rr_para
	b	.L1961
.L1953:
	mov	w1, w20
	mov	x0, x21
	bl	toshiba_3d_set_slc_rr_para
	b	.L1954
.L1974:
	mov	w19, w4
	b	.L1958
.L1960:
	mov	w1, 0
	mov	x0, x21
	bl	toshiba_3d_set_slc_rr_para
.L1961:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1136]
	cmp	w0, 36
	bne	.L1970
	ldr	x0, [x29, 120]
	add	x25, x25, x0, lsl 8
	mov	w0, 85
	str	w0, [x25, 2056]
	mov	w0, 255
	str	wzr, [x25, 2052]
	str	wzr, [x25, 2048]
	str	w0, [x25, 2056]
.L1970:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1971
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1971:
	cmp	w19, 256
	ccmn	w19, #1, 4, ne
	bne	.L1972
	adrp	x0, .LC155
	mov	w4, w19
	mov	w3, w20
	mov	w2, w24
	mov	w1, w23
	add	x0, x0, :lo12:.LC155
	bl	printk
.L1972:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L1952:
	str	x0, [x29, 136]
	sxtw	x0, w23
	add	x0, x0, 8
	mov	w19, -1
	add	x0, x25, x0, lsl 8
	mov	w20, 1
	str	x0, [x29, 128]
.L1968:
	ldr	x0, [x29, 136]
	ldrb	w0, [x0, 1136]
	cmp	w0, 36
	bne	.L1962
	mov	x0, x21
	mov	w2, 1
	mov	w1, w20
	bl	toshiba_tlc_set_rr_para
	mov	w0, 93
.L1996:
	ldr	x1, [x29, 128]
	mov	w4, w28
	mov	x3, x27
	mov	x2, x26
	str	w0, [x1, 8]
	mov	w1, w24
	mov	w0, w23
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L1964
	mov	w3, w4
	str	w4, [x29, 108]
	mov	w2, w24
	mov	w1, w20
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	bl	printk
	ldr	w4, [x29, 108]
.L1964:
	cmn	w4, #1
	beq	.L1965
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	cmn	w19, #1
	csel	w19, w19, w4, ne
	ldr	x26, [x0, 304]
	ldr	x27, [x0, 320]
	ldr	x0, [x29, 136]
	ldrb	w0, [x0, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1975
.L1965:
	ldr	w0, [x29, 112]
	add	w20, w20, 1
	cmp	w0, w20
	bne	.L1968
.L1967:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1136]
	cmp	w0, 36
	bne	.L1969
	mov	w2, 1
	b	.L1997
.L1962:
	mov	x0, x21
	mov	w1, w20
	bl	toshiba_3d_set_tlc_rr_para
	mov	w0, 38
	b	.L1996
.L1975:
	mov	w19, w4
	b	.L1967
.L1969:
	mov	w1, 0
	mov	x0, x21
	bl	toshiba_3d_set_tlc_rr_para
	b	.L1961
	.size	toshiba_3d_read_retrial, .-toshiba_3d_read_retrial
	.align	2
	.global	toshiba_read_retrial
	.type	toshiba_read_retrial, %function
toshiba_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	stp	x27, x28, [sp, 80]
	mov	x27, x2
	stp	w4, w1, [x29, 120]
	mov	x28, x3
	stp	x21, x22, [sp, 32]
	adrp	x20, .LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	bl	nandc_wait_flash_ready
	mov	w0, w19
	bl	zftl_flash_enter_slc_mode
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	add	x1, x20, :lo12:.LANCHOR0
	and	x0, x19, 255
	add	x22, x0, 8
	str	x0, [x29, 112]
	ldrb	w0, [x1, 1136]
	ldr	x23, [x1, 1056]
	sub	w0, w0, #67
	and	w0, w0, 255
	add	x22, x23, x22, lsl 8
	cmp	w0, 1
	bls	.L2016
	ldrb	w0, [x1, 1192]
	cbz	w0, .L2017
	mov	w24, 1
	mov	w0, 1
	bl	nandc_set_if_mode
.L2000:
	and	x0, x19, 255
	mov	w1, 92
	add	x0, x23, x0, lsl 8
	str	w1, [x0, 2056]
	mov	w1, 197
	str	w1, [x0, 2056]
.L1999:
	sxtw	x0, w19
	mov	w21, 1
	add	x0, x0, 8
	mov	w25, -1
	add	x0, x23, x0, lsl 8
	str	x0, [x29, 104]
.L2001:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0, 328]
	add	w0, w0, 1
	cmp	w21, w0
	bcc	.L2010
	mov	w26, w25
.L2009:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0, 1136]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x22
	bhi	.L2011
	bl	sandisk_set_rr_para
.L2012:
	sxtw	x19, w19
	add	x20, x20, :lo12:.LANCHOR0
	add	x19, x19, 8
	mov	w0, 255
	add	x23, x23, x19, lsl 8
	str	w0, [x23, 8]
	ldrb	w0, [x20, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L2013
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L2013:
	cmp	w26, 256
	ccmn	w26, #1, 4, ne
	bne	.L2014
	ldr	w2, [x29, 124]
	adrp	x0, .LC155
	mov	w4, w26
	mov	w3, w21
	mov	w1, w21
	add	x0, x0, :lo12:.LC155
	bl	printk
.L2014:
	bl	nandc_wait_flash_ready
	cbz	w24, .L1998
	mov	w0, 4
	bl	nandc_set_if_mode
.L1998:
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2017:
	mov	w24, 0
	b	.L2000
.L2016:
	mov	w24, 0
	b	.L1999
.L2010:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w21
	ldrb	w0, [x0, 1136]
	sub	w0, w0, #67
	and	w0, w0, 255
	cmp	w0, 1
	mov	x0, x22
	bhi	.L2002
	bl	sandisk_set_rr_para
.L2003:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1136]
	cmp	w0, 34
	bne	.L2004
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrb	w0, [x0, 328]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L2004
	ldr	x1, [x29, 104]
	mov	w0, 179
	str	w0, [x1, 8]
.L2004:
	ldr	x0, [x29, 112]
	mov	w1, 38
	ldr	w4, [x29, 120]
	mov	x3, x28
	mov	x2, x27
	add	x0, x23, x0, lsl 8
	str	w1, [x0, 2056]
	mov	w1, 93
	str	w1, [x0, 2056]
	mov	w0, w19
	ldr	w1, [x29, 124]
	bl	flash_read_page
	mov	w26, w0
	cmn	w0, #1
	beq	.L2007
	cmn	w25, #1
	csel	w25, w25, w0, ne
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x27, [x0, 304]
	ldr	x28, [x0, 320]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L2009
.L2007:
	add	w21, w21, 1
	b	.L2001
.L2002:
	bl	toshiba_set_rr_para
	b	.L2003
.L2011:
	bl	toshiba_set_rr_para
	b	.L2012
	.size	toshiba_read_retrial, .-toshiba_read_retrial
	.align	2
	.global	ymtc_3d_read_retrial
	.type	ymtc_3d_read_retrial, %function
ymtc_3d_read_retrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, -1
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w24, w1
	and	w23, w0, 255
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	mov	x25, x2
	mov	x26, x3
	mov	w27, w4
	bl	nandc_wait_flash_ready
	ubfiz	x28, x23, 8, 8
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	add	x1, x22, :lo12:.LANCHOR0
	add	x0, x28, 2048
	adrp	x5, .LANCHOR5
	mov	w20, 1
	add	x5, x5, :lo12:.LANCHOR5
	mov	x21, x1
	ldr	x28, [x1, 1056]
	tst	x24, 50331648
	add	x28, x28, x0
	bne	.L2040
.L2035:
	str	x5, [x29, 104]
	mov	w1, w20
	mov	x0, x28
	bl	ymtc_3d_set_slc_rr_para
	mov	w4, w27
	mov	x3, x26
	mov	x2, x25
	mov	w1, w24
	mov	w0, w23
	bl	flash_read_page
	ldr	x5, [x29, 104]
	cmn	w0, #1
	beq	.L2032
	ldrb	w1, [x21, 1249]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x5, 304]
	ldr	x26, [x5, 320]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2043
.L2032:
	add	w20, w20, 1
	cmp	w20, 10
	bne	.L2035
.L2034:
	mov	w1, 0
	mov	x0, x28
	bl	ymtc_3d_set_slc_rr_para
.L2036:
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w0, [x22, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2041
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2041:
	cmp	w19, 256
	ccmn	w19, #1, 4, ne
	bne	.L2042
	adrp	x0, .LC156
	mov	w4, w19
	mov	w3, w20
	mov	w2, w24
	mov	w1, w20
	add	x0, x0, :lo12:.LC156
	bl	printk
.L2042:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2043:
	mov	w19, w0
	b	.L2034
.L2040:
	str	x5, [x29, 104]
	mov	w1, w20
	mov	x0, x28
	bl	ymtc_3d_set_tlc_rr_para
	mov	w4, w27
	mov	x3, x26
	mov	x2, x25
	mov	w1, w24
	mov	w0, w23
	bl	flash_read_page
	ldr	x5, [x29, 104]
	cmn	w0, #1
	beq	.L2037
	ldrb	w1, [x21, 1249]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x5, 304]
	ldr	x26, [x5, 320]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2044
.L2037:
	add	w20, w20, 1
	cmp	w20, 51
	bne	.L2040
.L2039:
	mov	w1, 0
	mov	x0, x28
	bl	ymtc_3d_set_tlc_rr_para
	b	.L2036
.L2044:
	mov	w19, w0
	b	.L2039
	.size	ymtc_3d_read_retrial, .-ymtc_3d_read_retrial
	.align	2
	.global	samsung_read_retrial
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	mov	w21, w1
	stp	x23, x24, [sp, 48]
	str	w4, [x29, 108]
	mov	x23, x2
	mov	x24, x3
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	adrp	x20, .LANCHOR0
	stp	x27, x28, [sp, 80]
	bl	nandc_wait_flash_ready
	mov	w0, w22
	bl	zftl_flash_enter_slc_mode
	mov	w0, w22
	bl	zftl_flash_exit_slc_mode
	add	x0, x20, :lo12:.LANCHOR0
	tst	x21, 50331648
	and	x5, x22, 255
	adrp	x7, .LANCHOR4
	ldr	x0, [x0, 1056]
	bne	.L2060
	add	x7, x7, :lo12:.LANCHOR4
	adrp	x25, .LC157
	add	x25, x25, :lo12:.LC157
	add	x27, x0, x5, lsl 8
	mov	x28, 0
	add	x0, x7, 152
	mov	w26, -1
	str	x0, [x29, 96]
.L2065:
	mov	w0, 239
	str	w0, [x27, 2056]
	mov	w0, 141
	str	w0, [x27, 2052]
	ldr	x0, [x29, 96]
	add	w19, w28, 1
	add	x0, x0, x28
	ldrsb	w0, [x0, 1]
	str	w0, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
	bl	nandc_wait_flash_ready
	ldr	w4, [x29, 108]
	mov	x3, x24
	mov	x2, x23
	mov	w1, w21
	mov	w0, w22
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L2061
	mov	w3, w4
	str	w4, [x29, 104]
	mov	w2, w21
	mov	w1, w19
	mov	x0, x25
	bl	printk
	ldr	w4, [x29, 104]
.L2061:
	cmn	w4, #1
	beq	.L2062
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	cmn	w26, #1
	csel	w26, w26, w4, ne
	ldr	x23, [x0, 304]
	ldr	x24, [x0, 320]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L2074
.L2062:
	add	x28, x28, 1
	cmp	x28, 25
	bne	.L2065
	mov	w19, 26
.L2064:
	mov	w0, 239
	str	w0, [x27, 2056]
	mov	w0, 141
	str	w0, [x27, 2052]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
	str	wzr, [x27, 2048]
.L2096:
	add	x20, x20, :lo12:.LANCHOR0
	bl	nandc_wait_flash_ready
	ldrb	w0, [x20, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L2072
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L2072:
	cmp	w26, 256
	ccmn	w26, #1, 4, ne
	bne	.L2073
	adrp	x0, .LC159
	mov	w4, w26
	mov	w3, w19
	mov	w2, w21
	mov	w1, w19
	add	x0, x0, :lo12:.LC159
	bl	printk
.L2073:
	bl	nandc_wait_flash_ready
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2074:
	mov	w26, w4
	b	.L2064
.L2060:
	add	x7, x7, :lo12:.LANCHOR4
	adrp	x25, .LC158
	add	x27, x7, 188
	add	x28, x0, x5, lsl 8
	add	x25, x25, :lo12:.LC158
	mov	w26, -1
	mov	w19, 1
.L2071:
	mov	w0, 239
	str	w0, [x28, 2056]
	mov	w0, 137
	str	w0, [x28, 2052]
	ldrb	w0, [x27]
	str	w0, [x28, 2048]
	ldrb	w0, [x27, 1]
	str	w0, [x28, 2048]
	ldrb	w0, [x27, 2]
	str	w0, [x28, 2048]
	ldrb	w0, [x27, 3]
	str	w0, [x28, 2048]
	bl	nandc_wait_flash_ready
	ldr	w4, [x29, 108]
	mov	x3, x24
	mov	x2, x23
	mov	w1, w21
	mov	w0, w22
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L2067
	mov	w3, w4
	str	w4, [x29, 96]
	mov	w2, w21
	mov	w1, w19
	mov	x0, x25
	bl	printk
	ldr	w4, [x29, 96]
.L2067:
	cmn	w4, #1
	beq	.L2068
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	cmn	w26, #1
	csel	w26, w26, w4, ne
	ldr	x23, [x0, 304]
	ldr	x24, [x0, 320]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1249]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L2075
.L2068:
	add	w19, w19, 1
	add	x27, x27, 4
	cmp	w19, 26
	bne	.L2071
.L2070:
	mov	w0, 239
	str	w0, [x28, 2056]
	mov	w0, 137
	str	w0, [x28, 2052]
	str	wzr, [x28, 2048]
	str	wzr, [x28, 2048]
	str	wzr, [x28, 2048]
	str	wzr, [x28, 2048]
	b	.L2096
.L2075:
	mov	w26, w4
	b	.L2070
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	hynix_read_retrial
	.type	hynix_read_retrial, %function
hynix_read_retrial:
	stp	x29, x30, [sp, -128]!
	adrp	x5, .LANCHOR0
	add	x8, x5, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	stp	x25, x26, [sp, 64]
	mov	x25, x2
	stp	x19, x20, [sp, 16]
	mov	x26, x3
	stp	x23, x24, [sp, 48]
	and	x23, x0, 255
	stp	x27, x28, [sp, 80]
	mov	w27, w4
	stp	x5, x8, [x29, 112]
	mov	x21, x23
	ldr	x28, [x8, 1048]
	mov	w19, -1
	add	x28, x28, 112
	add	x0, x28, x23
	ldrb	w24, [x28, 2]
	ldrb	w20, [x0, 8]
	bl	nandc_wait_flash_ready
	mov	w0, w23
	bl	zftl_flash_enter_slc_mode
	mov	w0, w23
	bl	zftl_flash_exit_slc_mode
	ldp	x5, x8, [x29, 112]
	adrp	x7, .LANCHOR5
	add	x7, x7, :lo12:.LANCHOR5
	mov	w6, 0
.L2098:
	cmp	w6, w24
	bcc	.L2103
.L2102:
	add	x1, x5, :lo12:.LANCHOR0
	add	x23, x28, x23
	ldrb	w0, [x1, 1249]
	strb	w20, [x23, 8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L2104
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L2104:
	cmp	w19, 256
	ccmn	w19, #1, 4, ne
	bne	.L2105
	adrp	x0, .LC160
	mov	w4, w19
	mov	w3, w6
	mov	w2, w22
	mov	w1, w6
	add	x0, x0, :lo12:.LC160
	bl	printk
.L2105:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2103:
	add	w20, w20, 1
	stp	x8, x7, [x29, 96]
	and	w20, w20, 255
	str	x5, [x29, 112]
	cmp	w24, w20
	str	w6, [x29, 120]
	csel	w20, w20, wzr, hi
	mov	w0, w21
	mov	w1, w20
	bl	hynix_set_rr_para
	mov	w4, w27
	mov	x3, x26
	mov	x2, x25
	mov	w1, w22
	mov	w0, w21
	bl	flash_read_page
	ldr	w6, [x29, 120]
	cmn	w0, #1
	ldp	x8, x7, [x29, 96]
	ldr	x5, [x29, 112]
	beq	.L2100
	ldrb	w1, [x8, 1249]
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x7, 304]
	ldr	x26, [x7, 320]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L2106
.L2100:
	add	w6, w6, 1
	b	.L2098
.L2106:
	mov	w19, w0
	b	.L2102
	.size	hynix_read_retrial, .-hynix_read_retrial
	.align	2
	.global	flash_ddr_tuning_read
	.type	flash_ddr_tuning_read, %function
flash_ddr_tuning_read:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w22, w1
	stp	x23, x24, [sp, 48]
	mov	x24, x2
	stp	x25, x26, [sp, 64]
	adrp	x23, .LANCHOR5
	stp	x27, x28, [sp, 80]
	mov	x25, x3
	str	w4, [x29, 120]
	add	x23, x23, :lo12:.LANCHOR5
	bl	nandc_get_ddr_para
	mov	w28, 0
	str	w0, [x29, 116]
	mov	w26, 0
	adrp	x0, .LC161
	mov	w27, 0
	add	x0, x0, :lo12:.LC161
	mov	w19, 1024
	mov	w21, -1
	mov	w7, 6
	str	wzr, [x29, 124]
	str	x0, [x29, 104]
.L2122:
	mov	w0, w7
	str	w7, [x29, 112]
	bl	nandc_set_ddr_para
	ldr	w4, [x29, 120]
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	mov	w0, w20
	bl	flash_read_page
	mov	w4, w0
	adrp	x0, .LANCHOR2
	ldr	w7, [x29, 112]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L2117
	ldr	x0, [x29, 104]
	mov	w3, w4
	mov	w1, w7
	str	w4, [x29, 100]
	str	w7, [x29, 112]
	mov	w2, w22
	bl	printk
	ldr	w4, [x29, 100]
	ldr	w7, [x29, 112]
.L2117:
	add	w0, w19, 1
	cmp	w4, w0
	bhi	.L2118
	adrp	x0, .LANCHOR0+1249
	ldr	x24, [x23, 304]
	ldr	x25, [x23, 320]
	ldrb	w0, [x0, #:lo12:.LANCHOR0+1249]
	cmp	w4, w0, lsr 2
	bcs	.L2128
	add	w27, w27, 1
	cmp	w27, 7
	bls	.L2128
	sub	w28, w7, w27
	mov	w19, w4
	mov	w21, 0
.L2120:
	ldr	w0, [x29, 124]
	cmp	w27, w26
	csel	w28, w28, w0, cs
.L2121:
	cbz	w28, .L2123
	adrp	x0, .LANCHOR0+1249
	mov	w1, 3
	ldrb	w0, [x0, #:lo12:.LANCHOR0+1249]
	udiv	w0, w0, w1
	cmp	w0, w19
	bls	.L2123
	mov	w1, w28
	adrp	x0, .LC162
	add	x0, x0, :lo12:.LC162
	bl	printk
	mov	w0, w28
.L2142:
	bl	nandc_set_ddr_para
	cbz	w21, .L2116
	adrp	x0, .LANCHOR0
	add	x27, x0, :lo12:.LANCHOR0
	mov	x23, x0
	ldrb	w1, [x27, 1248]
	tbz	x1, 0, .L2116
	mov	w2, w22
	mov	w1, w20
	adrp	x0, .LC163
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
	strb	w1, [x0, 1208]
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	ldr	w4, [x29, 120]
	mov	x3, x25
	mov	x2, x24
	mov	w1, w22
	mov	w0, w20
	bl	flash_read_page
	mov	w19, w0
	mov	w3, w0
	mov	w2, w22
	mov	w1, w20
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	bl	printk
	ldrb	w0, [x27, 1249]
	cmp	w19, w0
	bhi	.L2130
	adrp	x1, .LANCHOR5
	add	x1, x1, :lo12:.LANCHOR5
	ldr	w0, [x1, 332]
	add	w0, w0, 1
	str	w0, [x1, 332]
	cmp	w0, 100
	bls	.L2126
	strb	wzr, [x27, 1192]
.L2116:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2118:
	cmp	w27, w26
	bls	.L2129
	sub	w0, w28, w27
	str	w0, [x29, 124]
	cmp	w27, 7
	bhi	.L2121
	mov	w26, w27
.L2129:
	mov	w27, 0
	b	.L2119
.L2128:
	mov	w28, w7
	mov	w19, w4
	mov	w21, 0
.L2119:
	add	w7, w7, 2
	cmp	w7, 50
	bne	.L2122
	b	.L2120
.L2123:
	ldrb	w0, [x29, 116]
	b	.L2142
.L2130:
	mov	w19, w21
.L2126:
	add	x23, x23, :lo12:.LANCHOR0
	ldrb	w0, [x23, 1248]
	bl	flash_set_interface_mode
	ldrb	w0, [x23, 1248]
	bl	nandc_set_if_mode
	b	.L2116
	.size	flash_ddr_tuning_read, .-flash_ddr_tuning_read
	.align	2
	.global	flash_read_page_en
	.type	flash_read_page_en, %function
flash_read_page_en:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp, 64]
	and	w25, w0, 255
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x21, x22, [sp, 32]
	mov	w19, w1
	mov	x22, x2
	mov	x23, x3
	ldrb	w0, [x0, 1153]
	mov	w24, w4
	cmp	w0, w25
	bhi	.L2144
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 288
	mov	w2, 431
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2144:
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, w25, sxtw
	ldrb	w4, [x0, 1153]
	ldrb	w21, [x1, 1196]
	cmp	w25, w4
	bcc	.L2145
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbnz	x0, 6, .L2146
.L2170:
	mov	w0, -1
.L2143:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L2146:
	mov	w3, w19
	mov	w2, w25
	mov	w1, w21
	adrp	x0, .LC165
	add	x0, x0, :lo12:.LC165
	bl	printk
	b	.L2170
.L2145:
	tst	x19, 50331648
	bne	.L2148
	ldrb	w1, [x20, #:lo12:.LANCHOR0]
	cbz	w1, .L2149
	ldrb	w0, [x0, 1]
	cbz	w0, .L2148
.L2149:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1, 2]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1, 1]
	sub	w19, w19, w0
	cbz	w2, .L2150
	add	w19, w0, w19, lsl 1
.L2148:
	mov	w4, w24
	mov	x3, x23
	mov	x2, x22
	mov	w1, w19
	mov	w0, w21
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2143
	add	x25, x20, :lo12:.LANCHOR0
	ldrb	w26, [x25, 1252]
	cbnz	w26, .L2151
.L2154:
	adrp	x0, .LANCHOR5+336
	ldr	x5, [x0, #:lo12:.LANCHOR5+336]
	cbnz	x5, .L2152
.L2153:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w3, -1
	mov	w2, w19
	mov	w1, 0
	adrp	x0, .LC166
	add	x0, x0, :lo12:.LC166
	ldrb	w4, [x20, 1252]
	bl	printk
	ldrb	w0, [x20, 1192]
	cbz	w0, .L2170
	mov	w4, w24
	mov	x3, x23
	mov	x2, x22
	mov	w1, w19
	mov	w0, w21
	bl	flash_ddr_tuning_read
	b	.L2143
.L2150:
	add	x1, x1, 4
	ldrh	w19, [x1, w19, uxtw 1]
	add	w19, w19, w0
	b	.L2148
.L2151:
	strb	wzr, [x25, 1252]
	mov	w4, w24
	mov	x3, x23
	mov	x2, x22
	mov	w1, w19
	mov	w0, w21
	bl	flash_read_page
	strb	w26, [x25, 1252]
	cmn	w0, #1
	beq	.L2154
	b	.L2143
.L2152:
	mov	w4, w24
	mov	x3, x23
	mov	x2, x22
	mov	w1, w19
	mov	w0, w21
	blr	x5
	cmn	w0, #1
	bne	.L2143
	b	.L2153
	.size	flash_read_page_en, .-flash_read_page_en
	.align	2
	.global	flash_get_last_written_page
	.type	flash_get_last_written_page, %function
flash_get_last_written_page:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	adrp	x0, .LANCHOR5+172
	stp	x19, x20, [sp, 16]
	stp	x27, x28, [sp, 80]
	adrp	x27, .LANCHOR2
	ldrh	w19, [x0, #:lo12:.LANCHOR5+172]
	add	x0, x27, :lo12:.LANCHOR2
	stp	x21, x22, [sp, 32]
	and	w21, w1, 65535
	stp	x25, x26, [sp, 64]
	sub	w19, w19, #1
	sxth	w19, w19
	mov	x24, x2
	ldrh	w26, [x0, 34]
	mov	x22, x3
	mov	w25, w4
	mov	w0, w23
	mul	w26, w26, w21
	add	w1, w19, w26
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L2172
	mov	w28, 0
	mov	w5, 2
.L2173:
	cmp	w28, w19
	ble	.L2176
.L2172:
	ldr	w0, [x27, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2177
	ldr	w3, [x22]
	adrp	x0, .LC167
	mov	w2, w19
	mov	w1, w21
	add	x0, x0, :lo12:.LC167
	bl	printk
.L2177:
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2176:
	add	w20, w28, w19
	str	w5, [x29, 108]
	mov	w4, w25
	mov	x3, x22
	mov	x2, x24
	mov	w0, w23
	sdiv	w20, w20, w5
	add	w1, w26, w20, sxth
	bl	flash_read_page_en
	ldr	w5, [x29, 108]
	cmp	w0, 512
	bne	.L2174
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L2173
.L2174:
	add	w20, w20, 1
	sxth	w28, w20
	b	.L2173
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR0+1205
	mov	w5, 24
	mov	w6, 1
	add	x29, sp, 0
	ldrb	w4, [x4, #:lo12:.LANCHOR0+1205]
	and	w0, w0, 65535
	sub	w5, w5, w4
	adrp	x4, .LANCHOR3+1304
	ldrh	w4, [x4, #:lo12:.LANCHOR3+1304]
	sub	w5, w5, w4
	mov	w4, w3
	mov	x3, x2
	mov	x2, x1
	lsl	w6, w6, w5
	sub	w6, w6, #1
	and	w1, w6, w0
	asr	w0, w0, w5
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
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x21, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w23, w0, 255
	mov	w24, w1
	mov	w22, 1
	ldrb	w0, [x21, 1248]
	adrp	x20, .LANCHOR5
	strb	w22, [x21, 1192]
	add	x20, x20, :lo12:.LANCHOR5
	bl	flash_set_interface_mode
	ldrb	w0, [x21, 1248]
	bl	nandc_set_if_mode
	ldp	x3, x2, [x20, 344]
	mov	w4, 4
	mov	w1, w24
	mov	w0, w23
	bl	flash_ddr_tuning_read
	ldp	x3, x2, [x20, 344]
	mov	w4, 4
	mov	w1, w24
	mov	w0, w23
	bl	flash_read_page
	cmn	w0, #1
	bne	.L2185
	ldrb	w0, [x21, 1248]
	tbz	x0, 0, .L2185
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21, 1192]
.L2186:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L2185:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19, 1192]
	b	.L2186
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	str	x27, [sp, 80]
	adrp	x27, .LANCHOR0
	add	x0, x27, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	mov	w22, w1
	stp	x25, x26, [sp, 64]
	mov	w20, 24
	mov	x24, x2
	mov	x25, x3
	ldrb	w1, [x0, 1205]
	and	x19, x21, 255
	ldr	x26, [x0, 1056]
	add	x23, x19, 8
	sub	w1, w20, w1
	mov	w20, 1
	lsl	w20, w20, w1
	sub	w20, w20, #1
	bl	nandc_wait_flash_ready
	and	w20, w20, w22
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	add	x23, x26, x23, lsl 8
	mov	w0, w21
	tst	x22, 50331648
	bne	.L2192
	bl	zftl_flash_enter_slc_mode
.L2193:
	add	x19, x26, x19, lsl 8
	mov	w0, 128
	str	w0, [x19, 2056]
	and	w0, w20, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w20, 8
	str	w0, [x19, 2052]
	lsr	w0, w20, 16
	str	w0, [x19, 2052]
	add	x0, x27, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1204]
	cbz	w0, .L2194
	lsr	w0, w20, 24
	str	w0, [x19, 2052]
.L2194:
	mov	w0, w20
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x25
	mov	x3, x24
	mov	w1, 1
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w21
	bl	nandc_xfer
	mov	w0, 16
	str	w0, [x19, 2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x23
	bl	flash_read_status
	mov	w3, w0
	bl	nandc_de_cs.constprop.35
	and	w2, w3, 4
	tbz	x3, 2, .L2191
	mov	w1, w22
	adrp	x0, .LC168
	add	x0, x0, :lo12:.LC168
	bl	printk
	mov	w2, -1
.L2191:
	mov	w0, w2
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L2192:
	bl	zftl_flash_exit_slc_mode
	b	.L2193
	.size	flash_prog_page, .-flash_prog_page
	.align	2
	.global	flash_test_blk
	.type	flash_test_blk, %function
flash_test_blk:
	stp	x29, x30, [sp, -48]!
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	stp	x21, x22, [sp, 32]
	and	w22, w0, 255
	and	w20, w1, 65535
	mov	w1, 165
	ldr	x0, [x19, 304]
	bl	ftl_memset
	ldr	x0, [x19, 320]
	mov	w2, 8
	mov	w1, 90
	bl	ftl_memset
	adrp	x0, .LANCHOR0+2
	ldrh	w0, [x0, #:lo12:.LANCHOR0+2]
	mul	w20, w0, w20
	mov	w0, w22
	mov	w1, w20
	bl	flash_erase_block
	cmn	w0, #1
	bne	.L2204
.L2206:
	mov	w19, -1
.L2205:
	mov	w1, w20
	mov	w0, w22
	bl	flash_erase_block
	mov	w0, w19
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2204:
	adrp	x21, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x2, [x19, 304]
	mov	w1, w20
	ldr	x3, [x19, 320]
	mov	w0, w22
	ldrb	w4, [x21, 17]
	add	x21, x21, 8
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L2206
	ldrb	w4, [x21, 9]
	mov	w1, w20
	ldr	x2, [x19, 304]
	mov	w0, w22
	ldr	x3, [x19, 320]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2206
	ldr	x0, [x19, 304]
	ldr	w1, [x0]
	mov	w0, 42405
	movk	w0, 0xa5a5, lsl 16
	cmp	w1, w0
	bne	.L2206
	ldr	x0, [x19, 320]
	ldr	w1, [x0]
	mov	w0, 23130
	movk	w0, 0x5a5a, lsl 16
	cmp	w1, w0
	csetm	w19, ne
	b	.L2205
	.size	flash_test_blk, .-flash_test_blk
	.align	2
	.global	flash_start_one_pass_page_prog
	.type	flash_start_one_pass_page_prog, %function
flash_start_one_pass_page_prog:
	stp	x29, x30, [sp, -32]!
	and	w9, w0, 255
	and	w8, w3, 255
	adrp	x7, .LANCHOR0
	add	x29, sp, 0
	add	x0, x7, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	and	w20, w2, 255
	ldr	x19, [x0, 1056]
	mov	w0, w8
	bl	nandc_cs
	cbz	w9, .L2215
	sxtw	x0, w8
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w9, [x0, 8]
.L2215:
	ubfiz	x0, x8, 8, 8
	add	x7, x7, :lo12:.LANCHOR0
	add	x19, x19, x0
	mov	w0, 128
	str	w0, [x19, 2056]
	and	w0, w4, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w4, 8
	str	w0, [x19, 2052]
	lsr	w0, w4, 16
	str	w0, [x19, 2052]
	ldrb	w0, [x7, 1204]
	cbz	w0, .L2216
	lsr	w0, w4, 24
	str	w0, [x19, 2052]
.L2216:
	mov	w0, w4
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x6
	mov	x3, x5
	mov	w1, 1
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w8
	bl	nandc_xfer
	str	w20, [x19, 2056]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_start_one_pass_page_prog, .-flash_start_one_pass_page_prog
	.align	2
	.global	flash_dual_page_prog
	.type	flash_dual_page_prog, %function
flash_dual_page_prog:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ubfiz	x22, x20, 8, 8
	stp	x23, x24, [sp, 48]
	mov	w19, 24
	stp	x25, x26, [sp, 64]
	mov	x25, x2
	stp	x27, x28, [sp, 80]
	add	x2, x22, 2048
	adrp	x27, .LANCHOR2
	mov	w21, w1
	ldr	x22, [x0, 1056]
	mov	x26, x3
	ldrb	w0, [x0, 1205]
	mov	x23, x4
	mov	x24, x5
	ubfx	x28, x21, 24, 2
	sub	w0, w19, w0
	mov	w19, 1
	add	x22, x22, x2
	lsl	w19, w19, w0
	ldr	w0, [x27, #:lo12:.LANCHOR2]
	sub	w19, w19, #1
	and	w19, w19, w1
	tbz	x0, 4, .L2225
	adrp	x0, .LC169
	mov	w3, w6
	mov	w2, w28
	add	x0, x0, :lo12:.LC169
	bl	printk
.L2225:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w28, .L2226
	bl	zftl_flash_enter_slc_mode
.L2227:
	mov	x6, x26
	mov	x5, x25
	mov	w4, w19
	mov	w3, w20
	mov	w2, 16
	mov	w1, 0
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	x6, x24
	mov	x5, x23
	add	w4, w19, 1
	mov	w3, w20
	mov	w2, 16
	mov	w1, 0
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w3, w0
	bl	nandc_de_cs.constprop.35
	and	w2, w3, 4
	tbz	x3, 2, .L2224
	ldr	w0, [x27, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2229
	adrp	x0, .LC168
	mov	w1, w21
	add	x0, x0, :lo12:.LC168
	bl	printk
.L2229:
	mov	w2, -1
.L2224:
	mov	w0, w2
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L2226:
	bl	zftl_flash_exit_slc_mode
	b	.L2227
	.size	flash_dual_page_prog, .-flash_dual_page_prog
	.align	2
	.global	ymtc_flash_tlc_page_prog
	.type	ymtc_flash_tlc_page_prog, %function
ymtc_flash_tlc_page_prog:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	str	x25, [sp, 64]
	mov	w25, w1
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w19, 24
	stp	x23, x24, [sp, 48]
	ubfiz	x24, x20, 8, 8
	add	x0, x24, 2048
	mov	x22, x2
	ldr	x24, [x1, 1056]
	mov	x23, x3
	mov	w21, 1
	add	x24, x24, x0
	ldrb	w0, [x1, 1205]
	sub	w19, w19, w0
	bl	nandc_wait_flash_ready
	lsl	w19, w21, w19
	mov	w0, w20
	sub	w19, w19, #1
	bl	nandc_cs
	and	w19, w19, w25
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
	mov	x6, x23
	mov	x5, x22
	mov	w4, w19
	mov	w3, w20
	mov	w1, w21
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	x6, x23
	mov	x5, x22
	add	w4, w19, w21
	mov	w3, w20
	mov	w1, w21
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	mov	x6, x23
	mov	x5, x22
	add	w4, w19, 2
	mov	w3, w20
	mov	w1, w21
	mov	w2, 16
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x24
	bl	flash_read_status
	mov	w3, w0
	bl	nandc_de_cs.constprop.35
	and	w2, w3, 4
	tbz	x3, 2, .L2237
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2239
	adrp	x0, .LC170
	mov	w1, w25
	add	x0, x0, :lo12:.LC170
	bl	printk
.L2239:
	mov	w2, -1
.L2237:
	mov	w0, w2
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ymtc_flash_tlc_page_prog, .-ymtc_flash_tlc_page_prog
	.section	.text.unlikely
	.align	2
	.type	fw_flash_page_prog.constprop.29, %function
fw_flash_page_prog.constprop.29:
	stp	x29, x30, [sp, -64]!
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x21, x1
	str	x23, [sp, 48]
	mov	x23, x2
	stp	x19, x20, [sp, 16]
	ldr	x1, [x4, 1144]
	ldrb	w22, [x4, 1249]
	ldrb	w19, [x1, 9]
	udiv	w19, w0, w19
	ldrb	w0, [x4, 1152]
	bl	nandc_bch_sel
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w1, [x0, 15]
	cmp	w1, 9
	bne	.L2245
	ldrb	w1, [x4, 1154]
	cbnz	w1, .L2245
	add	x0, x0, 8
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L2246
	mov	x3, x23
	mov	x2, x21
	mov	w1, w19
	mov	w0, 0
	bl	ymtc_flash_tlc_page_prog
.L2249:
	mov	w4, w0
	mov	w0, w22
	bl	nandc_bch_sel
	mov	w0, w4
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L2246:
	adrp	x20, .LANCHOR5
	add	x20, x20, :lo12:.LANCHOR5
	mov	w2, 16384
	mov	w1, 255
	ldr	x0, [x20, 304]
	bl	ftl_memset
	ldr	x5, [x20, 304]
	mov	w6, 4
	mov	x3, x23
	mov	x2, x21
	mov	w1, w19
	mov	x4, x5
	mov	w0, 0
	bl	flash_dual_page_prog
	b	.L2249
.L2245:
	mov	w4, 4
	mov	x3, x23
	mov	x2, x21
	mov	w1, w19
	mov	w0, 0
	bl	flash_prog_page
	b	.L2249
	.size	fw_flash_page_prog.constprop.29, .-fw_flash_page_prog.constprop.29
	.text
	.align	2
	.global	flash_start_tlc_page_prog
	.type	flash_start_tlc_page_prog, %function
flash_start_tlc_page_prog:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w1, 255
	stp	x25, x26, [sp, 64]
	and	w25, w0, 255
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	and	w26, w3, 255
	stp	x23, x24, [sp, 48]
	and	w22, w2, 255
	mov	w20, w4
	mov	x23, x5
	ldrb	w1, [x1, 1153]
	mov	x24, x6
	mov	x19, x0
	cmp	w1, w26
	bhi	.L2251
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 312
	mov	w2, 868
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2251:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1153]
	cmp	w1, w26
	bls	.L2250
	add	x26, x0, w26, sxtw
	ldr	x19, [x0, 1056]
	ldrb	w5, [x26, 1196]
	mov	w0, w5
	bl	nandc_cs
	cbz	w25, .L2253
	sxtw	x0, w5
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w25, [x0, 8]
.L2253:
	ubfiz	x0, x5, 8, 8
	add	x19, x19, x0
	mov	w0, 128
	str	w21, [x19, 2056]
	str	w0, [x19, 2056]
	and	w0, w20, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w20, 8
	str	w0, [x19, 2052]
	lsr	w0, w20, 16
	add	w20, w20, w20, lsl 1
	str	w0, [x19, 2052]
	sub	w0, w20, #1
	add	w0, w0, w21
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x24
	mov	x3, x23
	mov	w1, 1
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w5
	bl	nandc_xfer
	str	w22, [x19, 2056]
	bl	nandc_de_cs.constprop.35
.L2250:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_start_tlc_page_prog, .-flash_start_tlc_page_prog
	.align	2
	.type	queue_tlc_prog_cmd, %function
queue_tlc_prog_cmd:
	stp	x29, x30, [sp, -64]!
	mov	w3, 24
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	mov	x21, x0
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR0
	mov	w24, w1
	mov	w23, 1
	ldr	x7, [x0]
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1205]
	ldr	w2, [x7, 40]
	sub	w3, w3, w1
	ldrb	w0, [x0, 1213]
	lsl	w19, w23, w1
	sub	w19, w19, #1
	lsl	w20, w23, w3
	sub	w20, w20, #1
	and	w20, w20, w2
	lsr	w2, w2, w3
	and	w19, w19, w2
	and	w19, w19, 255
	cbz	w0, .L2259
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x0, [x21]
	mov	w4, w20
	mov	w3, w19
	mov	w1, w23
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x0, [x21, 8]
	add	w4, w20, w23
	mov	w3, w19
	mov	w1, w23
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x0, [x21, 16]
	add	w4, w20, 2
	mov	w3, w19
	mov	w2, 16
	mov	w1, w23
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
.L2260:
	cbz	w24, .L2258
	ldr	x1, [x21]
	mov	w0, 4
	strb	w0, [x1, 58]
	mov	w0, 1
	strb	w0, [x1, 59]
	mov	w0, -1
	strb	w0, [x1]
	add	x0, x22, :lo12:.LANCHOR0
	add	x0, x0, 3354
	bl	buf_add_tail
.L2258:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L2259:
	ldr	x5, [x7, 8]
	mov	w4, w20
	ldr	x6, [x7, 24]
	mov	w3, w19
	ldrb	w0, [x7, 60]
	mov	w1, w23
	mov	w2, 26
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldp	x7, x0, [x21]
	mov	w4, w20
	mov	w3, w19
	mov	w2, 26
	mov	w1, 2
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x0, [x21, 16]
	mov	w4, w20
	ldr	x7, [x21]
	mov	w3, w19
	mov	w2, 16
	mov	w1, 3
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	b	.L2260
	.size	queue_tlc_prog_cmd, .-queue_tlc_prog_cmd
	.align	2
	.global	sblk_3d_tlc_dump_prog
	.type	sblk_3d_tlc_dump_prog, %function
sblk_3d_tlc_dump_prog:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w3, 24
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	stp	x21, x22, [sp, 32]
	mov	w22, 1
	ldr	w2, [x0, 40]
	ldrb	w0, [x1, 1205]
	sub	w3, w3, w0
	lsl	w19, w22, w0
	ldrb	w0, [x1, 1213]
	lsl	w21, w22, w3
	sub	w19, w19, #1
	sub	w21, w21, #1
	and	w21, w21, w2
	lsr	w2, w2, w3
	and	w19, w19, w2
	and	w19, w19, 255
	cbz	w0, .L2267
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x5, [x20, 8]
	mov	w4, w21
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w1, w22
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20, 8]
	add	w4, w21, w22
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w1, w22
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20, 8]
	add	w4, w21, 2
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w2, 16
	mov	w1, w22
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
.L2268:
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	w0, [x20, 40]
	mov	w1, 64
	bl	flash_wait_device_ready
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L2267:
	ldr	x5, [x20, 8]
	mov	w4, w21
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w1, w22
	mov	w2, 26
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20, 8]
	mov	w4, w21
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w2, 26
	mov	w1, 2
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x5, [x20, 8]
	mov	w4, w21
	ldr	x6, [x20, 24]
	mov	w3, w19
	mov	w2, 16
	mov	w1, 3
	mov	w0, 0
	bl	flash_start_tlc_page_prog
	b	.L2268
	.size	sblk_3d_tlc_dump_prog, .-sblk_3d_tlc_dump_prog
	.align	2
	.global	flash_start_3d_mlc_page_prog
	.type	flash_start_3d_mlc_page_prog, %function
flash_start_3d_mlc_page_prog:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w1, 255
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	x22, x3
	mov	w24, w2
	mov	x23, x4
	ldrb	w1, [x1, 1153]
	mov	x19, x0
	cmp	w1, w20
	bhi	.L2271
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 344
	mov	w2, 903
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2271:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1153]
	cmp	w1, w20
	bls	.L2270
	add	x20, x0, w20, sxtw
	ldr	x19, [x0, 1056]
	ldrb	w5, [x20, 1196]
	mov	w0, w5
	bl	nandc_cs
	ubfiz	x0, x5, 8, 8
	add	x19, x19, x0
	mov	w0, 128
	str	w0, [x19, 2056]
	and	w0, w24, 255
	str	wzr, [x19, 2052]
	str	wzr, [x19, 2052]
	str	w0, [x19, 2052]
	lsr	w0, w24, 8
	str	w0, [x19, 2052]
	lsr	w0, w24, 16
	str	w0, [x19, 2052]
	mov	w0, w24
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x23
	mov	x3, x22
	mov	w1, 1
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w5
	bl	nandc_xfer
	str	w21, [x19, 2056]
.L2270:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_start_3d_mlc_page_prog, .-flash_start_3d_mlc_page_prog
	.align	2
	.global	sblk_mlc_dump_prog
	.type	sblk_mlc_dump_prog, %function
sblk_mlc_dump_prog:
	stp	x29, x30, [sp, -48]!
	mov	w2, 24
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	str	x21, [sp, 32]
	ldr	w1, [x0, 40]
	adrp	x0, .LANCHOR0+1205
	ldrb	w19, [x0, #:lo12:.LANCHOR0+1205]
	mov	w0, 1
	sub	w2, w2, w19
	lsl	w19, w0, w19
	sub	w19, w19, #1
	lsl	w21, w0, w2
	sub	w21, w21, #1
	and	w21, w21, w1
	lsr	w1, w1, w2
	and	w19, w19, w1
	and	w19, w19, 255
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2275
	ldr	w2, [x20, 40]
	adrp	x0, .LC171
	mov	w1, w21
	add	x0, x0, :lo12:.LC171
	add	w3, w2, 1
	bl	printk
.L2275:
	ldr	x3, [x20, 8]
	mov	w2, w21
	ldr	x4, [x20, 24]
	mov	w1, w19
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	x3, [x20, 8]
	add	w2, w21, 1
	ldr	x4, [x20, 24]
	mov	w1, w19
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	ldr	w0, [x20, 40]
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w2, w0
	bl	nandc_de_cs.constprop.35
	mov	w0, w2
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	sblk_mlc_dump_prog, .-sblk_mlc_dump_prog
	.align	2
	.global	flash_start_page_prog
	.type	flash_start_page_prog, %function
flash_start_page_prog:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	stp	x25, x26, [sp, 64]
	adrp	x26, .LANCHOR0
	add	x0, x26, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	mov	w24, w1
	str	x27, [sp, 80]
	mov	w19, 24
	mov	w20, 1
	mov	x22, x2
	ldrb	w1, [x0, 1205]
	mov	x23, x3
	ldrb	w0, [x0, 1153]
	sub	w25, w19, w1
	lsl	w20, w20, w1
	sub	w20, w20, #1
	lsr	w1, w24, w25
	and	w20, w20, w1
	and	w20, w20, 255
	cmp	w0, w20
	bhi	.L2281
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 376
	mov	w2, 956
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2281:
	add	x1, x26, :lo12:.LANCHOR0
	ldrb	w0, [x1, 1153]
	cmp	w0, w20
	bls	.L2280
	add	x0, x1, w20, sxtw
	mov	w19, 1
	lsl	w19, w19, w25
	sub	w19, w19, #1
	and	w19, w19, w24
	ldr	x25, [x1, 1056]
	ldrb	w27, [x0, 1196]
	bl	nandc_rdy_status
	cbnz	w0, .L2283
	ldrb	w0, [x1, 1153]
	cmp	w0, 1
	bne	.L2284
	bl	nandc_wait_flash_ready
.L2283:
	mov	w0, w27
	bl	hynix_reconfig_rr_para
	mov	w0, w27
	bl	nandc_cs
	tst	x24, 50331648
	bne	.L2285
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
	ldrb	w0, [x26, #:lo12:.LANCHOR0]
	cbz	w0, .L2286
	mov	w0, w27
	bl	zftl_flash_enter_slc_mode
.L2286:
	ubfiz	x20, x27, 8, 8
	mov	w0, 128
	add	x20, x25, x20
	add	x26, x26, :lo12:.LANCHOR0
	str	w0, [x20, 2056]
	and	w0, w19, 255
	str	wzr, [x20, 2052]
	str	wzr, [x20, 2052]
	str	w0, [x20, 2052]
	lsr	w0, w19, 8
	str	w0, [x20, 2052]
	lsr	w0, w19, 16
	str	w0, [x20, 2052]
	ldrb	w0, [x26, 1204]
	cbz	w0, .L2287
	lsr	w0, w19, 24
	str	w0, [x20, 2052]
.L2287:
	mov	w0, w19
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x23
	mov	x3, x22
	mov	w1, 1
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w27
	bl	nandc_xfer
	str	w21, [x20, 2056]
	bl	nandc_de_cs.constprop.35
.L2280:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L2284:
	mov	w2, 64
	mov	w1, w19
	mov	w0, w20
	bl	flash_wait_device_ready_raw
	b	.L2283
.L2285:
	mov	w0, w27
	bl	zftl_flash_exit_slc_mode
	b	.L2286
	.size	flash_start_page_prog, .-flash_start_page_prog
	.align	2
	.type	queue_prog_cmd, %function
queue_prog_cmd:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	mov	x19, x0
	ldr	w1, [x0, 40]
	ldr	x3, [x0, 24]
	ldr	x2, [x0, 8]
	mov	w0, 16
	bl	flash_start_page_prog
	adrp	x0, .LANCHOR0
	add	x3, x0, :lo12:.LANCHOR0
	ldr	w4, [x19, 40]
	ldrb	w1, [x3, 3354]
	cmp	w1, 255
	beq	.L2296
	ldrb	w5, [x3, 1205]
	mov	w2, 1
	mov	w6, 24
	add	x3, x3, 1304
	sub	w6, w6, w5
	lsl	w2, w2, w5
	sub	w2, w2, #1
	and	w2, w2, 65535
	asr	w4, w4, w6
	and	w4, w4, w2
.L2298:
	add	x7, x3, x1, lsl 6
	ldr	w5, [x7, 40]
	lsr	w5, w5, w6
	and	w5, w2, w5
	cmp	w4, w5
	bne	.L2297
	ldrb	w5, [x7, 58]
	cmp	w5, 7
	bne	.L2297
	mov	w1, 3
	strb	w1, [x7, 58]
.L2296:
	mov	w1, 3
	strb	w1, [x19, 58]
	mov	w1, 1
	strb	w1, [x19, 59]
	mov	w1, -1
	strb	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR0
	mov	x1, x19
	add	x0, x0, 3354
	bl	buf_add_tail
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2297:
	lsl	x1, x1, 6
	ldrb	w1, [x3, x1]
	cmp	w1, 255
	bne	.L2298
	b	.L2296
	.size	queue_prog_cmd, .-queue_prog_cmd
	.align	2
	.global	flash_complete_plane_page_read
	.type	flash_complete_plane_page_read, %function
flash_complete_plane_page_read:
	stp	x29, x30, [sp, -64]!
	mov	w4, 24
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp, 48]
	mov	w24, w0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	mov	x20, x1
	mov	w19, 1
	mov	x21, x2
	ldrb	w1, [x0, 1205]
	ldrb	w0, [x0, 1153]
	sub	w23, w4, w1
	lsl	w19, w19, w1
	sub	w19, w19, #1
	lsr	w1, w24, w23
	and	w19, w19, w1
	and	w19, w19, 255
	cmp	w0, w19
	bhi	.L2305
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 400
	mov	w2, 1070
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2305:
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w0, [x2, 1153]
	cmp	w0, w19
	bls	.L2316
	add	x19, x2, w19, sxtw
	mov	w4, 1
	lsl	w4, w4, w23
	ldr	x7, [x2, 1056]
	sub	w4, w4, #1
	ldrb	w9, [x19, 1196]
	and	w4, w4, w24
	ubfx	x24, x24, 24, 2
	mov	w0, w9
	bl	nandc_cs
	cbnz	w24, .L2307
	mov	w0, w4
	bl	slc_phy_page_address_calc
	mov	w4, w0
.L2307:
	add	x8, x22, :lo12:.LANCHOR0
	and	x1, x9, 255
	and	w6, w4, 255
	lsr	w5, w4, 8
	lsr	w3, w4, 16
	ldrb	w0, [x8, 1176]
	ldrb	w2, [x8, 1204]
	cmp	w0, 1
	bne	.L2308
	add	x1, x7, x1, lsl 8
	mov	w0, 6
.L2334:
	str	w0, [x1, 2056]
	str	wzr, [x1, 2052]
	str	wzr, [x1, 2052]
	str	w6, [x1, 2052]
	str	w5, [x1, 2052]
	str	w3, [x1, 2052]
	cbz	w2, .L2330
	lsr	w0, w4, 24
	str	w0, [x1, 2052]
.L2330:
	add	x22, x22, :lo12:.LANCHOR0
	mov	w0, 224
	str	w0, [x1, 2056]
	ldr	x0, [x22, 1144]
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L2314
	cbz	w24, .L2314
	ldrb	w0, [x22, 1212]
	cbnz	w0, .L2314
	ldrb	w0, [x22, 1213]
	cbnz	w0, .L2314
	add	w4, w4, w4, lsl 1
	sub	w0, w24, #1
	add	w0, w4, w0
.L2331:
	bl	nandc_set_seed
	adrp	x0, .LANCHOR2+17
	mov	x4, x21
	mov	x3, x20
	mov	w1, 0
	ldrb	w2, [x0, #:lo12:.LANCHOR2+17]
	mov	w0, w9
	bl	nandc_xfer
	mov	w2, w0
	bl	nandc_de_cs.constprop.35
	mov	w0, w2
.L2304:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L2308:
	ldr	x0, [x8, 1144]
	add	x1, x7, x1, lsl 8
	ldrb	w0, [x0, 12]
	cmp	w0, 3
	bne	.L2311
	mov	w0, 5
	b	.L2334
.L2311:
	str	wzr, [x1, 2056]
	str	wzr, [x1, 2052]
	str	wzr, [x1, 2052]
	str	w6, [x1, 2052]
	str	w5, [x1, 2052]
	str	w3, [x1, 2052]
	cbz	w2, .L2313
	lsr	w0, w4, 24
	str	w0, [x1, 2052]
.L2313:
	mov	w0, 5
	str	w0, [x1, 2056]
	str	wzr, [x1, 2052]
	str	wzr, [x1, 2052]
	b	.L2330
.L2314:
	mov	w0, w4
	b	.L2331
.L2316:
	mov	w0, -1
	b	.L2304
	.size	flash_complete_plane_page_read, .-flash_complete_plane_page_read
	.align	2
	.global	flash_complete_page_read
	.type	flash_complete_page_read, %function
flash_complete_page_read:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	mov	x23, x2
	adrp	x24, .LANCHOR0
	add	x2, x24, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	stp	x19, x20, [sp, 16]
	mov	w0, 1
	stp	x25, x26, [sp, 64]
	mov	x22, x1
	stp	x27, x28, [sp, 80]
	mov	w1, 24
	ubfx	x25, x21, 24, 2
	ldrb	w20, [x2, 1205]
	sub	w1, w1, w20
	lsl	w20, w0, w20
	sub	w20, w20, #1
	lsl	w19, w0, w1
	ldrb	w0, [x2, 1153]
	lsr	w1, w21, w1
	and	w20, w20, w1
	sub	w19, w19, #1
	and	w20, w20, 255
	and	w19, w19, w21
	cmp	w0, w20
	bhi	.L2336
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 432
	mov	w2, 1232
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2336:
	add	x0, x24, :lo12:.LANCHOR0
	add	x20, x0, w20, sxtw
	ldr	x4, [x0, 1056]
	ldrb	w26, [x20, 1196]
	mov	w0, w26
	bl	nandc_cs
	cbnz	w25, .L2337
	mov	w0, w19
	bl	slc_phy_page_address_calc
	mov	w19, w0
.L2337:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0, 20]
	cmp	w0, 3
	bne	.L2338
	ubfiz	x0, x26, 8, 8
	mov	w1, 5
	add	x0, x4, x0
	str	w1, [x0, 2056]
	and	w1, w19, 255
	str	wzr, [x0, 2052]
	str	wzr, [x0, 2052]
	str	w1, [x0, 2052]
	lsr	w1, w19, 8
	str	w1, [x0, 2052]
	lsr	w1, w19, 16
	str	w1, [x0, 2052]
	mov	w1, 224
	str	w1, [x0, 2056]
.L2338:
	add	x0, x24, :lo12:.LANCHOR0
	ldr	x1, [x0, 1144]
	ldrb	w1, [x1, 12]
	cmp	w1, 3
	bne	.L2339
	cbz	w25, .L2339
	ldrb	w1, [x0, 1212]
	cbnz	w1, .L2339
	ldrb	w0, [x0, 1213]
	cbnz	w0, .L2339
	sub	w0, w25, #1
	add	w1, w19, w19, lsl 1
	add	w0, w0, w1
.L2366:
	add	x27, x20, :lo12:.LANCHOR2
	bl	nandc_set_seed
	mov	x4, x23
	mov	x3, x22
	mov	w1, 0
	mov	w0, w26
	ldrb	w2, [x27, 17]
	add	x27, x27, 8
	bl	nandc_xfer
	cmn	w0, #1
	bne	.L2341
	add	x28, x24, :lo12:.LANCHOR0
	ldrb	w5, [x28, 1252]
	cbz	w5, .L2342
	ldrb	w4, [x27, 9]
	mov	x3, x23
	str	w5, [x29, 108]
	mov	x2, x22
	strb	wzr, [x28, 1252]
	orr	w1, w19, w25, lsl 24
	mov	w0, w26
	bl	flash_read_page
	ldr	w5, [x29, 108]
	strb	w5, [x28, 1252]
	cbnz	w25, .L2343
.L2348:
	ldrb	w2, [x24, #:lo12:.LANCHOR0]
	add	x1, x24, :lo12:.LANCHOR0
	cbz	w2, .L2343
	ldrb	w1, [x1, 1249]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	blt	.L2343
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w1, [x20, 27]
	sub	w1, w1, #4
	and	w1, w1, 255
	cmp	w1, 4
	mov	w1, 256
	csel	w0, w0, w1, hi
.L2335:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2339:
	mov	w0, w19
	b	.L2366
.L2343:
	cmn	w0, #1
	bne	.L2335
.L2349:
	adrp	x0, .LANCHOR5+336
	ldr	x5, [x0, #:lo12:.LANCHOR5+336]
	cbnz	x5, .L2345
.L2347:
	add	x24, x24, :lo12:.LANCHOR0
	mov	w3, -1
	mov	w2, w21
	mov	w1, 0
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	ldrb	w4, [x24, 1252]
	bl	printk
	ldrb	w0, [x24, 1192]
	cbnz	w0, .L2346
	mov	w0, -1
	b	.L2335
.L2345:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x3, x23
	mov	x2, x22
	orr	w1, w19, w25, lsl 24
	ldrb	w4, [x0, 17]
	mov	w0, w26
	blr	x5
	cmn	w0, #1
	bne	.L2335
	b	.L2347
.L2346:
	add	x20, x20, :lo12:.LANCHOR2
	mov	x3, x23
	mov	x2, x22
	orr	w1, w19, w25, lsl 24
	mov	w0, w26
	ldrb	w4, [x20, 17]
	bl	flash_ddr_tuning_read
	b	.L2335
.L2342:
	cbz	w25, .L2348
	b	.L2349
.L2341:
	cbnz	w25, .L2335
	b	.L2348
	.size	flash_complete_page_read, .-flash_complete_page_read
	.align	2
	.type	queue_wait_first_req_completed, %function
queue_wait_first_req_completed:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrb	w0, [x1, 3354]
	cmp	w0, 255
	bne	.L2368
.L2401:
	mov	w21, 0
	b	.L2367
.L2368:
	add	x1, x1, 1304
	sxtw	x20, w0
	add	x1, x1, x20, lsl 6
	ldrb	w2, [x1, 58]
	ldr	w21, [x1, 40]
	sub	w3, w2, #1
	cmp	w3, 10
	bhi	.L2401
	adrp	x1, .L2371
	add	x1, x1, :lo12:.L2371
	ldrh	w1, [x1,w3,uxtw #1]
	adr	x3, .Lrtx2371
	add	x1, x3, w1, sxth #2
	br	x1
.Lrtx2371:
	.section	.rodata
	.align	0
	.align	2
.L2371:
	.2byte	(.L2370 - .Lrtx2371) / 4
	.2byte	(.L2372 - .Lrtx2371) / 4
	.2byte	(.L2373 - .Lrtx2371) / 4
	.2byte	(.L2373 - .Lrtx2371) / 4
	.2byte	(.L2373 - .Lrtx2371) / 4
	.2byte	(.L2373 - .Lrtx2371) / 4
	.2byte	(.L2374 - .Lrtx2371) / 4
	.2byte	(.L2375 - .Lrtx2371) / 4
	.2byte	(.L2376 - .Lrtx2371) / 4
	.2byte	(.L2373 - .Lrtx2371) / 4
	.2byte	(.L2376 - .Lrtx2371) / 4
	.text
.L2370:
	bl	nandc_wait_flash_ready
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x20, lsl 6
	ldp	x1, x2, [x0, 8]
	cbz	x2, .L2377
	ldrb	w3, [x0, 56]
	adrp	x0, .LANCHOR3+1946
	ldrb	w0, [x0, #:lo12:.LANCHOR3+1946]
	cmp	w3, w0
	csel	x1, x1, x2, ne
.L2377:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w21
	add	x19, x19, 1304
	add	x20, x19, x20, lsl 6
	ldr	x2, [x20, 24]
	bl	flash_complete_page_read
	str	w0, [x20, 52]
	mov	w0, 13
	strb	w0, [x20, 58]
	ldrb	w0, [x20, 2]
	orr	w0, w0, 8
	strb	w0, [x20, 2]
	b	.L2401
.L2372:
	bl	nandc_wait_flash_ready
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x2, x20, 6
	add	x0, x0, 1304
	add	x1, x0, x2
	ldrb	w21, [x0, x2]
	add	x0, x0, x21, lsl 6
	ldr	x23, [x0, 8]
	ldp	x26, x0, [x1, 8]
	cbz	x0, .L2378
	ldrb	w2, [x1, 56]
	adrp	x1, .LANCHOR3+1946
	ldrb	w1, [x1, #:lo12:.LANCHOR3+1946]
	cmp	w2, w1
	csel	x26, x26, x0, ne
.L2378:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x21, lsl 6
	ldr	x1, [x0, 16]
	cbz	x1, .L2379
	ldrb	w2, [x0, 56]
	adrp	x0, .LANCHOR3+1946
	ldrb	w0, [x0, #:lo12:.LANCHOR3+1946]
	cmp	w2, w0
	csel	x23, x23, x1, ne
.L2379:
	add	x24, x19, :lo12:.LANCHOR0
	mov	x1, x26
	add	x24, x24, 1304
	add	x27, x24, x20, lsl 6
	add	x24, x24, x21, lsl 6
	ldr	x2, [x27, 24]
	ldr	w0, [x27, 40]
	bl	flash_complete_plane_page_read
	mov	w22, w0
	ldr	x2, [x24, 24]
	mov	x1, x23
	ldr	w0, [x24, 40]
	bl	flash_complete_plane_page_read
	mov	w25, w0
	cmn	w22, #1
	beq	.L2380
	ldr	w0, [x27, 36]
	cmn	w0, #1
	beq	.L2381
	ldr	x1, [x27, 24]
	ldr	w1, [x1, 4]
	cmp	w0, w1
	beq	.L2381
.L2380:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w5, 1
	add	x24, x0, 1304
	add	x24, x24, x20, lsl 6
	ldrb	w2, [x0, 1205]
	mov	w0, 24
	sub	w0, w0, w2
	ldr	x3, [x24, 24]
	lsl	w5, w5, w2
	adrp	x2, .LANCHOR3+1946
	sub	w5, w5, #1
	ldr	w1, [x24, 40]
	ldrb	w4, [x2, #:lo12:.LANCHOR3+1946]
	mov	x2, x26
	lsl	w6, w5, w0
	lsr	w0, w1, w0
	bic	w1, w1, w6
	and	w0, w0, w5
	bl	flash_read_page_en
	mov	w22, w0
	ldr	w2, [x24, 36]
	cmn	w2, #1
	beq	.L2382
	ldr	x0, [x24, 24]
	ldr	w4, [x0, 4]
	cmp	w2, w4
	beq	.L2382
	adrp	x1, .LANCHOR2
	ldr	w1, [x1, #:lo12:.LANCHOR2]
	tbz	x1, 6, .L2382
	ldr	w3, [x0]
	adrp	x0, .LC173
	ldr	w1, [x24, 40]
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2382:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x20, lsl 6
	ldr	w1, [x0, 36]
	cmn	w1, #1
	beq	.L2381
	ldr	x0, [x0, 24]
	ldr	w0, [x0, 4]
	cmp	w1, w0
	beq	.L2381
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 464
	mov	w2, 431
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2381:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x0, x0, 1304
	cmn	w25, #1
	add	x20, x0, x20, lsl 6
	strb	w1, [x20, 58]
	ldrb	w1, [x20, 2]
	str	w22, [x20, 52]
	orr	w1, w1, 8
	strb	w1, [x20, 2]
	beq	.L2383
	add	x0, x0, x21, lsl 6
	ldr	w1, [x0, 36]
	cmn	w1, #1
	beq	.L2385
	ldr	x0, [x0, 24]
	ldr	w0, [x0, 4]
	cmp	w1, w0
	beq	.L2385
.L2383:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w5, 1
	add	x20, x0, 1304
	add	x20, x20, x21, lsl 6
	ldrb	w2, [x0, 1205]
	mov	w0, 24
	sub	w0, w0, w2
	ldr	x3, [x20, 24]
	lsl	w5, w5, w2
	adrp	x2, .LANCHOR3+1946
	sub	w5, w5, #1
	ldr	w1, [x20, 40]
	ldrb	w4, [x2, #:lo12:.LANCHOR3+1946]
	mov	x2, x23
	lsl	w6, w5, w0
	lsr	w0, w1, w0
	bic	w1, w1, w6
	and	w0, w0, w5
	bl	flash_read_page_en
	ldr	w2, [x20, 36]
	cmn	w2, #1
	beq	.L2387
	ldr	x0, [x20, 24]
	ldr	w4, [x0, 4]
	cmp	w2, w4
	beq	.L2387
	adrp	x1, .LANCHOR2
	ldr	w1, [x1, #:lo12:.LANCHOR2]
	tbz	x1, 6, .L2387
	ldr	w3, [x0]
	adrp	x0, .LC173
	ldr	w1, [x20, 40]
	add	x0, x0, :lo12:.LC173
	bl	printk
.L2387:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x21, lsl 6
	ldr	w1, [x0, 36]
	cmn	w1, #1
	beq	.L2385
	ldr	x0, [x0, 24]
	ldr	w0, [x0, 4]
	cmp	w1, w0
	beq	.L2385
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 464
	mov	w2, 450
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2385:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 13
	add	x19, x19, 1304
	add	x21, x19, x21, lsl 6
	strb	w0, [x21, 58]
	ldrb	w0, [x21, 2]
	str	w22, [x21, 52]
	orr	w0, w0, 8
	strb	w0, [x21, 2]
	b	.L2401
.L2373:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L2401
	mov	w0, 5
	tst	w21, w0
	beq	.L2389
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 12
	add	x19, x19, 1304
	mov	w4, 12
	add	x20, x19, x20, lsl 6
	mov	w2, w21
	ldrb	w1, [x20, 1]
	ldr	w3, [x20, 40]
	strb	w0, [x20, 58]
	adrp	x0, .LC174
	add	x0, x0, :lo12:.LC174
	bl	printk
.L2459:
	mov	w0, -1
	str	w0, [x20, 52]
	b	.L2367
.L2389:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 13
	add	x21, x0, 1304
	add	x21, x21, x20, lsl 6
	strb	w1, [x21, 58]
	ldr	x1, [x0, 3384]
	str	wzr, [x21, 52]
	ldr	w2, [x1, 156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L2401
	ldrh	w1, [x21, 50]
	cbnz	w1, .L2401
	ldrb	w2, [x0, 1205]
	mov	w5, 1
	adrp	x22, .LANCHOR5
	add	x22, x22, :lo12:.LANCHOR5
	mov	w0, 24
	sub	w0, w0, w2
	lsl	w5, w5, w2
	adrp	x2, .LANCHOR3+1946
	sub	w5, w5, #1
	ldr	w1, [x21, 40]
	ldrb	w4, [x2, #:lo12:.LANCHOR3+1946]
	ldp	x3, x2, [x22, 344]
	lsl	w6, w5, w0
	lsr	w0, w1, w0
	bic	w1, w1, w6
	and	w0, w0, w5
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2390
	ldr	x2, [x21, 24]
	ldr	x1, [x22, 344]
	ldr	w2, [x2]
	ldr	w1, [x1]
	cmp	w2, w1
	beq	.L2401
.L2390:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w3, w0
	add	x1, x19, 1304
	adrp	x0, .LC175
	add	x20, x1, x20, lsl 6
	add	x0, x0, :lo12:.LC175
	ldrb	w4, [x19, 1252]
	ldrb	w1, [x20, 1]
	ldr	w2, [x20, 40]
	bl	printk
	mov	w0, -1
	str	w0, [x20, 52]
	b	.L2401
.L2376:
	cmp	w2, 11
	mov	w1, 3
	mov	w5, 10
	csel	w5, w5, w1, eq
	add	x1, x19, :lo12:.LANCHOR0
	mov	w4, 24
	mov	w2, 1
	add	x22, x1, 1304
	ubfiz	x0, x0, 6, 8
	mov	x24, x1
	ldrb	w3, [x1, 1205]
	add	x0, x22, x0
	sub	w4, w4, w3
	lsl	w2, w2, w3
	sub	w2, w2, #1
	lsr	w3, w21, w4
	and	w3, w3, w2
	and	w3, w3, 65535
.L2392:
	ldrb	w1, [x0]
	cmp	w1, 255
	bne	.L2399
	mov	w21, -1
	b	.L2367
.L2399:
	sxtw	x23, w1
	ubfiz	x0, x1, 6, 8
	add	x1, x22, x23, lsl 6
	add	x0, x22, x0
	ldrb	w6, [x1, 58]
	cmp	w6, w5
	bne	.L2392
	ldr	w1, [x1, 40]
	lsr	w1, w1, w4
	and	w1, w1, w2
	cmp	w3, w1
	bne	.L2392
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbnz	x21, 6, .L2394
.L2458:
	mov	w21, 0
	b	.L2395
.L2394:
	tst	x21, 15
	beq	.L2396
	add	x22, x22, x20, lsl 6
	mov	w2, w0
	mov	w4, 12
	adrp	x0, .LC176
	add	x0, x0, :lo12:.LC176
	ldrb	w1, [x22, 1]
	ldr	w3, [x22, 40]
	bl	printk
	mov	w0, 12
	strb	w0, [x22, 58]
	mov	w0, -1
	str	w0, [x22, 52]
.L2395:
	add	x1, x19, :lo12:.LANCHOR0
	add	x1, x1, 1304
	add	x20, x1, x20, lsl 6
	add	x1, x1, x23, lsl 6
	ldrb	w0, [x20, 58]
	strb	w0, [x1, 58]
	ldr	w0, [x20, 52]
	str	w0, [x1, 52]
.L2367:
	mov	w0, w21
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L2396:
	add	x21, x22, x20, lsl 6
	mov	w0, 13
	strb	w0, [x21, 58]
	ldr	x0, [x24, 3384]
	str	wzr, [x21, 52]
	ldr	w1, [x0, 156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	bne	.L2458
	ldrh	w0, [x21, 50]
	cbnz	w0, .L2458
	ldrb	w2, [x24, 1205]
	mov	w5, 1
	adrp	x22, .LANCHOR5
	add	x22, x22, :lo12:.LANCHOR5
	mov	w0, 24
	sub	w0, w0, w2
	lsl	w5, w5, w2
	adrp	x2, .LANCHOR3+1946
	sub	w5, w5, #1
	ldr	w1, [x21, 40]
	ldrb	w4, [x2, #:lo12:.LANCHOR3+1946]
	ldp	x3, x2, [x22, 344]
	lsl	w6, w5, w0
	lsr	w0, w1, w0
	bic	w1, w1, w6
	and	w0, w0, w5
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2398
	ldr	x2, [x21, 24]
	ldr	x1, [x22, 344]
	ldr	w2, [x2]
	ldr	w1, [x1]
	cmp	w2, w1
	beq	.L2458
.L2398:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, w0
	add	x21, x1, 1304
	adrp	x0, .LC177
	add	x21, x21, x20, lsl 6
	add	x0, x0, :lo12:.LC177
	ldrb	w4, [x1, 1252]
	ldr	w2, [x21, 40]
	ldrb	w1, [x21, 1]
	bl	printk
	mov	w0, -1
	str	w0, [x21, 52]
	b	.L2458
.L2374:
	mov	w0, w21
	mov	w1, 32
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 5, .L2401
	add	x19, x19, :lo12:.LANCHOR0
	tst	x21, 15
	add	x19, x19, 1304
	add	x20, x19, x20, lsl 6
	beq	.L2400
	mov	w0, 12
	strb	w0, [x20, 58]
	b	.L2459
.L2400:
	mov	w0, 13
	str	wzr, [x20, 52]
	strb	w0, [x20, 58]
	b	.L2401
.L2375:
	mov	w1, 64
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 6, .L2401
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 1304
	add	x19, x19, x20, lsl 6
	str	w0, [x19, 52]
	mov	w0, 7
	strb	w0, [x19, 58]
	b	.L2401
	.size	queue_wait_first_req_completed, .-queue_wait_first_req_completed
	.align	2
	.global	sblk_prog_page
	.type	sblk_prog_page, %function
sblk_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	and	w20, w1, 255
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w0, [x0, 50]
	cbz	w0, .L2461
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L2461
	ldr	w1, [x19, 40]
	adrp	x0, .LC178
	mov	w2, w20
	add	x0, x0, :lo12:.LC178
	bl	printk
.L2461:
	adrp	x24, .LANCHOR0
	add	x25, x24, :lo12:.LANCHOR0
	add	x27, x25, 1304
	mov	w21, 0
	mov	w23, 1
.L2462:
	cbnz	w20, .L2473
.L2489:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L2473:
	ldrb	w26, [x19]
	ldr	w22, [x19, 40]
.L2463:
	mov	w1, 1
	mov	w0, w22
	bl	queue_lun_state
	cbnz	w0, .L2464
	cmp	w20, 1
	beq	.L2465
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1250]
	cbz	w1, .L2465
	ldrb	w1, [x0, 1213]
	cbz	w1, .L2466
.L2465:
	mov	x0, x19
	bl	queue_prog_cmd
.L2467:
	subs	w20, w20, #1
	beq	.L2489
	add	x19, x24, :lo12:.LANCHOR0
	ubfiz	x26, x26, 6, 8
	add	x19, x19, 1304
	add	x19, x19, x26
	b	.L2462
.L2464:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2463
.L2466:
	ldrb	w0, [x0, 1205]
	mov	w28, 24
	sub	w28, w28, w0
	lsl	w0, w23, w0
	sub	w0, w0, #1
	lsr	w28, w22, w28
	and	w28, w28, w0
	ldrb	w0, [x19]
	and	w28, w28, 65535
	cmp	w0, 255
	bne	.L2468
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 496
	mov	w2, 697
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2468:
	ldrb	w0, [x19]
	mov	w1, 24
	add	x0, x27, x0, lsl 6
	ldr	w4, [x0, 40]
	ldrb	w0, [x25, 1205]
	sub	w1, w1, w0
	lsl	w0, w23, w0
	sub	w0, w0, #1
	lsr	w2, w4, w1
	and	w0, w0, w2
	cmp	w28, w0, uxth
	bne	.L2469
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	ldrh	w2, [x0, 1304]
	ldrb	w3, [x0, 1306]
	sub	w0, w1, w2
	sub	w3, w3, #1
	lsl	w1, w23, w2
	lsl	w0, w23, w0
	sub	w0, w0, #1
	and	w0, w0, w3
	sub	w1, w1, #1
	and	w0, w0, 65535
	lsr	w5, w22, w2
	and	w1, w1, 65535
	and	w5, w0, w5
	lsr	w2, w4, w2
	and	w0, w0, w2
	and	w22, w1, w22
	cmp	w5, w0
	and	w1, w1, w4
	ccmp	w22, w1, 0, ne
	bne	.L2469
	cmp	w21, w3
	beq	.L2469
	ldr	w1, [x19, 40]
	mov	w0, 17
	ldr	x2, [x19, 8]
	add	w21, w21, 1
	ldr	x3, [x19, 24]
	bl	flash_start_page_prog
	strb	w23, [x19, 59]
	mov	w0, 9
	strb	w0, [x19, 58]
	mov	w0, -1
	strb	w0, [x19]
	mov	x1, x19
	add	x0, x25, 3354
	bl	buf_add_tail
	b	.L2467
.L2469:
	mov	x0, x19
	mov	w21, 0
	bl	queue_prog_cmd
	b	.L2467
	.size	sblk_prog_page, .-sblk_prog_page
	.align	2
	.global	sblk_wait_write_queue_completed
	.type	sblk_wait_write_queue_completed, %function
sblk_wait_write_queue_completed:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
.L2492:
	ldrb	w0, [x19, 3354]
	cmp	w0, 255
	bne	.L2493
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2493:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2492
	.size	sblk_wait_write_queue_completed, .-sblk_wait_write_queue_completed
	.align	2
	.global	ftl_flush
	.type	ftl_flush, %function
ftl_flush:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x2
	adrp	x20, .LANCHOR5
	ldrb	w1, [x0, 3380]
	cbz	w1, .L2496
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 1304
	ldrb	w2, [x2, 360]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L2496:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2, 3380]
	strb	w0, [x20, 360]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	w0, -1
	bl	ftl_vpn_decrement
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_flush, .-ftl_flush
	.align	2
	.global	zftl_cache_flush
	.type	zftl_cache_flush, %function
zftl_cache_flush:
	adrp	x0, .LANCHOR0+3380
	ldrb	w0, [x0, #:lo12:.LANCHOR0+3380]
	cbz	w0, .L2506
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	timer_get_time
	adrp	x1, .LANCHOR5+364
	ldr	w1, [x1, #:lo12:.LANCHOR5+364]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L2501
	bl	ftl_flush
.L2501:
	ldp	x29, x30, [sp], 16
	ret
.L2506:
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	ftl_read_page
	.type	ftl_read_page, %function
ftl_read_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w20, w1
	mov	x21, x2
	mov	x22, x3
	str	x23, [sp, 48]
	mov	w23, w4
	bl	sblk_wait_write_queue_completed
	mov	w4, w23
	mov	x3, x22
	mov	x2, x21
	mov	w1, w20
	mov	w0, w19
	bl	flash_read_page_en
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_read_page, .-ftl_read_page
	.align	2
	.global	ftl_read_ppa_page
	.type	ftl_read_ppa_page, %function
ftl_read_ppa_page:
	stp	x29, x30, [sp, -64]!
	mov	w5, 1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	adrp	x0, .LANCHOR0+1205
	mov	w19, 24
	stp	x21, x22, [sp, 32]
	mov	x21, x1
	ldrb	w0, [x0, #:lo12:.LANCHOR0+1205]
	mov	x22, x2
	str	x23, [sp, 48]
	mov	w23, w3
	sub	w19, w19, w0
	lsl	w5, w5, w0
	sub	w5, w5, #1
	lsr	w19, w20, w19
	and	w19, w19, w5
	and	w19, w19, 255
	bl	sblk_wait_write_queue_completed
	mov	w4, w23
	mov	x3, x22
	mov	x2, x21
	mov	w1, w20
	mov	w0, w19
	bl	flash_read_page_en
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_read_ppa_page, .-ftl_read_ppa_page
	.align	2
	.global	sblk_read_page
	.type	sblk_read_page, %function
sblk_read_page:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w1, 255
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR0
	add	x26, x25, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	mov	x22, x0
	mov	x19, x0
	mov	w20, w21
	add	x23, x26, 1304
	stp	x27, x28, [sp, 80]
	adrp	x28, .LANCHOR5
.L2514:
	cbnz	w20, .L2524
.L2537:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 1304
.L2525:
	cbnz	w21, .L2527
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2524:
	ldrb	w24, [x19]
	ldr	w27, [x19, 40]
.L2515:
	mov	w1, 0
	mov	w0, w27
	bl	queue_lun_state
	cbnz	w0, .L2516
	cmp	w20, 1
	beq	.L2521
	add	x0, x28, :lo12:.LANCHOR5
	ldrb	w0, [x0, 368]
	cbnz	w0, .L2518
.L2521:
	mov	x0, x19
	bl	queue_read_cmd
	b	.L2519
.L2516:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2515
.L2518:
	add	x0, x25, :lo12:.LANCHOR0
	mov	w3, 24
	ldrb	w1, [x0, 1205]
	mov	w0, 1
	sub	w3, w3, w1
	lsl	w0, w0, w1
	sub	w0, w0, #1
	lsr	w3, w27, w3
	and	w3, w3, w0
	ldrb	w0, [x19]
	and	w3, w3, 65535
	cmp	w0, 255
	bne	.L2520
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 512
	mov	w2, 782
	str	w3, [x29, 124]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	ldr	w3, [x29, 124]
.L2520:
	ldrb	w4, [x19]
	mov	w0, 24
	ldrb	w7, [x26, 1205]
	mov	w2, 1
	sbfiz	x5, x4, 6, 32
	sub	w0, w0, w7
	add	x6, x23, x5
	lsl	w2, w2, w7
	sub	w2, w2, #1
	ldr	w1, [x6, 40]
	lsr	w0, w1, w0
	and	w0, w0, w2
	cmp	w3, w0, uxth
	bne	.L2521
	adrp	x0, .LANCHOR3+1410
	ldrh	w0, [x0, #:lo12:.LANCHOR3+1410]
	add	w27, w0, w27
	cmp	w1, w27
	bne	.L2521
	ldr	w0, [x19, 40]
	mov	w27, -1
	ldrb	w24, [x23, x5]
	sub	w20, w20, #1
	stp	x6, x5, [x29, 104]
	str	w4, [x29, 120]
	bl	flash_start_plane_read
	strb	wzr, [x19, 59]
	mov	w2, 2
	strb	w27, [x19]
	strb	w2, [x19, 58]
	add	x0, x26, 3354
	mov	x1, x19
	str	w2, [x29, 124]
	mov	x19, x0
	bl	buf_add_tail
	ldp	x6, x5, [x29, 104]
	strb	wzr, [x6, 59]
	ldp	w4, w2, [x29, 120]
	strb	w2, [x6, 58]
	strb	w27, [x23, x5]
	mov	x0, x19
	ubfiz	x1, x4, 6, 8
	add	x1, x23, x1
	bl	buf_add_tail
.L2519:
	subs	w20, w20, #1
	beq	.L2537
	add	x19, x25, :lo12:.LANCHOR0
	ubfiz	x24, x24, 6, 8
	add	x19, x19, 1304
	add	x19, x19, x24
	b	.L2514
.L2527:
	ldrb	w0, [x22, 58]
	cmp	w0, 13
	bne	.L2526
	ldrb	w0, [x22]
	sub	w21, w21, #1
	cmp	w0, 255
	beq	.L2526
	ubfiz	x22, x0, 6, 8
	add	x22, x19, x22
.L2526:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2525
	.size	sblk_read_page, .-sblk_read_page
	.align	2
	.global	gc_check_data_one_wl
	.type	gc_check_data_one_wl, %function
gc_check_data_one_wl:
	sub	sp, sp, #128
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 48]
	stp	x23, x24, [sp, 64]
	stp	x25, x26, [sp, 80]
	stp	x27, x28, [sp, 96]
	ldr	x0, [x20, 3424]
	ldr	x21, [x20, 1128]
	cbnz	x0, .L2540
	add	x20, x20, 3416
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20, 8]
.L2540:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x22, [x0, 3424]
	cbnz	x22, .L2541
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 528
	mov	w2, 729
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2541:
	add	x27, x19, :lo12:.LANCHOR0
	adrp	x4, .LANCHOR3
	add	x23, x27, 3416
	mov	x20, x4
	mov	w25, 0
.L2542:
	add	x28, x21, 80
	ldrb	w0, [x28, 9]
	cmp	w25, w0
	bge	.L2553
	sxtw	x26, w25
	mov	w24, 1
	add	x26, x26, 8
	b	.L2554
.L2552:
	add	x1, x4, :lo12:.LANCHOR3
	ldrh	w0, [x28, x26, lsl 1]
	ldrh	w2, [x1, 1410]
	ldrb	w1, [x1, 1320]
	cmp	w1, 2
	mul	w2, w0, w2
	beq	.L2543
	ldrb	w0, [x27, 1212]
	cbz	w0, .L2544
.L2543:
	ldrh	w0, [x23, 16]
	sub	w3, w0, #1
	add	w0, w24, w2
	add	w0, w3, w0
	orr	w1, w0, w1, lsl 24
	str	w1, [x22, 40]
.L2545:
	str	x4, [x29, 104]
	mov	w1, 1
	mov	x0, x22
	bl	sblk_read_page
	ldr	w2, [x22, 52]
	adrp	x0, .LANCHOR5
	ldr	x4, [x29, 104]
	cmn	w2, #1
	beq	.L2548
	add	x5, x0, :lo12:.LANCHOR5
	ldrh	w1, [x23, 22]
	ldr	x3, [x22, 24]
	ldr	x6, [x5, 280]
	lsl	x1, x1, 2
	ldr	w7, [x6, x1]
	ldr	w6, [x3, 4]
	cmp	w7, w6
	bne	.L2548
	ldr	x5, [x5, 288]
	ldr	w5, [x5, x1]
	ldr	w1, [x3, 8]
	cmp	w5, w1
	beq	.L2549
.L2548:
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w1, [x23, 22]
	ldr	x3, [x0, 280]
	lsl	x1, x1, 2
	ldr	w3, [x3, x1]
	cmn	w3, #1
	beq	.L2549
	adrp	x4, .LANCHOR2
	ldr	w4, [x4, #:lo12:.LANCHOR2]
	tbz	x4, 10, .L2550
	ldr	x4, [x22, 24]
	ldr	x0, [x0, 288]
	ldr	w5, [x4, 12]
	str	w5, [sp]
	ldp	w5, w6, [x4]
	ldr	w7, [x4, 8]
	ldr	w4, [x0, x1]
	adrp	x0, .LC179
	ldr	w1, [x22, 40]
	add	x0, x0, :lo12:.LC179
	bl	printk
.L2550:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x21, 80]
	ldr	x0, [x19, 1120]
	strh	wzr, [x0, x1, lsl 1]
	ldr	x1, [x19, 1128]
	ldr	w0, [x1, 556]
	add	w0, w0, 1
	str	w0, [x1, 556]
	ldr	x0, [x19, 3384]
	ldr	w1, [x0, 156]
	mov	w0, 20041
	movk	w0, 0x444b, lsl 16
	cmp	w1, w0
	bne	.L2557
	add	x20, x20, :lo12:.LANCHOR3
	ldrb	w0, [x20, 1950]
	cbnz	w0, .L2557
	ldrb	w0, [x20, 1322]
	cbnz	w0, .L2557
	ldr	w0, [x22, 40]
	bl	ftl_mask_bad_block
.L2557:
	mov	w0, -1
.L2539:
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 128
	ret
.L2544:
	cmp	w1, 3
	ldrh	w0, [x23, 16]
	bne	.L2546
	ldrb	w1, [x27, 1213]
	cbz	w1, .L2547
	add	w0, w0, w0, lsl 1
	sub	w1, w0, #1
	add	w0, w24, w2
	add	w0, w1, w0
	orr	w0, w0, 50331648
.L2572:
	str	w0, [x22, 40]
	b	.L2545
.L2547:
	add	w0, w0, w2
	orr	w0, w0, w24, lsl 24
	b	.L2572
.L2546:
	add	w0, w0, w2
	b	.L2572
.L2549:
	ldrh	w0, [x23, 22]
	add	w24, w24, 1
	add	w0, w0, 1
	strh	w0, [x23, 22]
.L2554:
	ldrh	w0, [x23, 20]
	cmp	w24, w0
	ble	.L2552
	add	w25, w25, 1
	b	.L2542
.L2553:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 3416
	ldrh	w0, [x19, 16]
	add	w1, w0, 1
	strh	w1, [x19, 16]
	adrp	x1, .LANCHOR3+1322
	ldrb	w1, [x1, #:lo12:.LANCHOR3+1322]
	cbz	w1, .L2558
	add	w0, w0, 2
	strh	w0, [x19, 16]
.L2558:
.L2551:
	mov	w0, 0
	b	.L2539
	.size	gc_check_data_one_wl, .-gc_check_data_one_wl
	.align	2
	.global	sblk_tlc_prog_one_page
	.type	sblk_tlc_prog_one_page, %function
sblk_tlc_prog_one_page:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	ldr	x0, [x0]
	ldr	w20, [x0, 40]
.L2574:
	mov	w1, 1
	mov	w0, w20
	bl	queue_lun_state
	cbnz	w0, .L2575
	mov	x0, x19
	mov	w1, 1
	bl	queue_tlc_prog_cmd
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L2575:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2574
	.size	sblk_tlc_prog_one_page, .-sblk_tlc_prog_one_page
	.align	2
	.global	sblk_xlc_prog_pages
	.type	sblk_xlc_prog_pages, %function
sblk_xlc_prog_pages:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	stp	x21, x22, [sp, 32]
	mov	x22, x1
	stp	x25, x26, [sp, 64]
	mov	w25, w2
	stp	x23, x24, [sp, 48]
	ldr	x0, [x0]
	ldr	w19, [x0, 40]
.L2578:
	mov	w1, 1
	mov	w0, w19
	bl	queue_lun_state
	cbnz	w0, .L2579
	cmp	w25, 2
	bne	.L2580
	adrp	x23, .LANCHOR0
	add	x4, x23, :lo12:.LANCHOR0
	ldr	x5, [x22]
	ldrb	w0, [x4, 1250]
	cbz	w0, .L2581
	ldr	x0, [x20]
	mov	w26, 1
	ldrb	w1, [x4, 1205]
	mov	w3, 24
	ldr	w5, [x5, 40]
	sub	w3, w3, w1
	lsl	w19, w26, w1
	ldr	w2, [x0, 40]
	ldrb	w1, [x4, 1213]
	lsl	w21, w26, w3
	sub	w19, w19, #1
	sub	w21, w21, #1
	and	w24, w2, w21
	lsr	w2, w2, w3
	and	w19, w19, w2
	and	w21, w21, w5
	and	w19, w19, 255
	cbz	w1, .L2582
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x0, [x20]
	mov	w4, w24
	mov	w3, w19
	mov	w1, w26
	mov	w2, 17
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x22]
	mov	w4, w21
	mov	w3, w19
	mov	w1, w26
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x20, 8]
	add	w4, w24, w26
	mov	w3, w19
	mov	w1, w25
	mov	w2, 17
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x22, 8]
	add	w4, w21, w26
	mov	w3, w19
	mov	w1, w25
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x20, 16]
	add	w4, w24, 2
	mov	w3, w19
	mov	w2, 17
	mov	w1, 3
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x22, 16]
	add	w4, w21, 2
	mov	w3, w19
	mov	w2, 16
	mov	w1, 3
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	mov	w0, 0
	bl	flash_start_one_pass_page_prog
.L2583:
	ldr	x1, [x20]
	mov	w0, 5
	strb	w0, [x1, 58]
	mov	w0, 1
	strb	w0, [x1, 59]
	mov	w0, -1
	strb	w0, [x1]
	add	x0, x23, :lo12:.LANCHOR0
	add	x0, x0, 3354
	bl	buf_add_tail
.L2584:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L2579:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2578
.L2582:
	ldr	x5, [x0, 8]
	mov	w4, w24
	ldr	x6, [x0, 24]
	mov	w3, w19
	ldrb	w0, [x0, 60]
	mov	w1, w26
	mov	w2, 17
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x7, [x20]
	mov	w4, w21
	ldr	x0, [x22]
	mov	w3, w19
	mov	w1, w26
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldp	x7, x0, [x20]
	mov	w4, w24
	mov	w3, w19
	mov	w1, w25
	mov	w2, 17
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x7, [x20]
	mov	w4, w21
	ldr	x0, [x22, 8]
	mov	w3, w19
	mov	w1, w25
	mov	w2, 26
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x7, [x20]
	mov	w4, w24
	ldr	x0, [x20, 16]
	mov	w3, w19
	mov	w2, 17
	mov	w1, 3
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x22, 16]
	mov	w4, w21
	ldr	x7, [x20]
	mov	w3, w19
	mov	w2, 16
	mov	w1, 3
	ldr	x5, [x0, 8]
	ldr	x6, [x0, 24]
	ldrb	w0, [x7, 60]
	bl	flash_start_tlc_page_prog
	b	.L2583
.L2581:
	ldr	w19, [x5, 40]
.L2585:
	mov	w1, 1
	mov	w0, w19
	bl	queue_lun_state
	cbnz	w0, .L2586
	mov	w1, 1
	mov	x0, x20
	bl	queue_tlc_prog_cmd
	mov	w1, 0
	mov	x0, x22
	bl	queue_tlc_prog_cmd
.L2587:
	mov	w1, 1
	mov	w0, w19
	bl	queue_lun_state
	cbz	w0, .L2584
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2587
.L2586:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2585
.L2580:
	mov	w1, 1
	mov	x0, x20
	bl	queue_tlc_prog_cmd
	b	.L2584
	.size	sblk_xlc_prog_pages, .-sblk_xlc_prog_pages
	.align	2
	.global	sblk_3d_mlc_prog_pages
	.type	sblk_3d_mlc_prog_pages, %function
sblk_3d_mlc_prog_pages:
	stp	x29, x30, [sp, -80]!
	ubfiz	x1, x1, 4, 32
	add	x1, x1, 8
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	add	x22, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp, 64]
	add	x21, x0, x1
	add	x26, x0, 8
	add	x25, x22, 3354
	stp	x23, x24, [sp, 48]
	stp	x19, x20, [sp, 16]
	mov	w23, 1
	mov	w24, 24
.L2591:
	cmp	x21, x26
	bne	.L2594
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L2594:
	ldr	x0, [x26, -8]
	ldr	w19, [x0, 40]
.L2592:
	mov	w1, 1
	mov	w0, w19
	bl	queue_lun_state
	cbnz	w0, .L2593
	ldr	x0, [x26, -8]
	add	x26, x26, 16
	ldrb	w1, [x22, 1205]
	sub	w2, w24, w1
	ldr	w0, [x0, 40]
	lsl	w19, w23, w1
	lsl	w20, w23, w2
	sub	w19, w19, #1
	sub	w20, w20, #1
	and	w20, w20, w0
	lsr	w0, w0, w2
	and	w19, w19, w0
	and	w19, w19, 255
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
	ldr	x0, [x26, -24]
	mov	w2, w20
	mov	w1, w19
	ldr	x3, [x0, 8]
	ldr	x4, [x0, 24]
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_wait_flash_ready
	ldr	x0, [x26, -16]
	add	w2, w20, 1
	mov	w1, w19
	ldr	x3, [x0, 8]
	ldr	x4, [x0, 24]
	mov	w0, 16
	bl	flash_start_3d_mlc_page_prog
	bl	nandc_de_cs.constprop.35
	ldr	x1, [x26, -24]
	mov	w0, 4
	strb	w23, [x1, 59]
	strb	w0, [x1, 58]
	mov	w0, -1
	strb	w0, [x1]
	mov	x0, x25
	bl	buf_add_tail
	b	.L2591
.L2593:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L2592
	.size	sblk_3d_mlc_prog_pages, .-sblk_3d_mlc_prog_pages
	.align	2
	.global	flash_prog_page_en
	.type	flash_prog_page_en, %function
flash_prog_page_en:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 255
	and	w0, w5, 255
	stp	x19, x20, [sp, 16]
	str	w0, [x29, 108]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	mov	w20, w1
	stp	x25, x26, [sp, 64]
	mov	x23, x2
	mov	x22, x3
	mov	w27, w4
	ldrb	w0, [x0, 1153]
	ubfx	x24, x20, 24, 2
	cmp	w0, w21
	bhi	.L2597
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 552
	mov	w2, 642
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2597:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1153]
	cmp	w1, w21
	bls	.L2608
	add	x1, x0, w21, sxtw
	ldrb	w26, [x1, 1196]
	cbnz	w24, .L2610
	ldrb	w1, [x19, #:lo12:.LANCHOR0]
	cbz	w1, .L2600
	ldrb	w0, [x0, 1]
	cbz	w0, .L2610
.L2600:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0, 2]
	ldrb	w2, [x0, 1]
	udiv	w19, w20, w1
	mul	w19, w19, w1
	sub	w1, w20, w19
	cbz	w2, .L2601
	add	w19, w19, w1, lsl 1
.L2599:
	adrp	x24, .LC180
	adrp	x28, .LANCHOR5
	add	x24, x24, :lo12:.LC180
	add	x25, x28, :lo12:.LANCHOR5
.L2605:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 4, .L2602
	mov	w3, w19
	mov	w2, w20
	mov	w1, w26
	mov	x0, x24
	bl	printk
.L2602:
	mov	w4, w27
	mov	x3, x22
	mov	x2, x23
	mov	w1, w19
	mov	w0, w26
	bl	flash_prog_page
	mov	w5, w0
	ldr	w0, [x29, 108]
	cbz	w0, .L2603
	add	x6, x28, :lo12:.LANCHOR5
	mov	w4, w27
	mov	w1, w20
	mov	w0, w21
	str	x6, [x29, 96]
	ldp	x3, x2, [x6, 344]
	str	w5, [x29, 104]
	bl	flash_read_page_en
	cmp	w0, 512
	mov	w4, w0
	ccmn	w0, #1, 4, ne
	beq	.L2604
	ldr	x6, [x29, 96]
	ldr	w1, [x23]
	ldr	w5, [x29, 104]
	ldr	x0, [x6, 352]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L2604
	ldr	x0, [x6, 344]
	ldr	w1, [x22]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L2603
.L2604:
	str	w4, [x29, 96]
	mov	w3, 4
	mov	x1, x23
	mov	w2, w3
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	bl	rknand_print_hex
	mov	w3, 4
	mov	x1, x22
	mov	w2, w3
	adrp	x0, .LC182
	add	x0, x0, :lo12:.LC182
	bl	rknand_print_hex
	ldr	x1, [x25, 344]
	mov	w3, 4
	adrp	x0, .LC183
	mov	w2, w3
	add	x0, x0, :lo12:.LC183
	bl	rknand_print_hex
	ldr	x1, [x25, 352]
	mov	w3, 4
	adrp	x0, .LC184
	mov	w2, w3
	add	x0, x0, :lo12:.LC184
	bl	rknand_print_hex
	ldr	w4, [x29, 96]
	cmp	w4, 512
	beq	.L2605
.L2607:
	mov	w1, w20
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	bl	printk
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 552
	mov	w2, 685
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2608:
	mov	w0, -1
	b	.L2596
.L2601:
	add	x0, x0, 4
	ldrh	w0, [x0, w1, uxtw 1]
	add	w19, w0, w19
	b	.L2599
.L2610:
	mov	w19, w20
	b	.L2599
.L2603:
	mov	w0, w5
	cmn	w5, #1
	beq	.L2607
.L2596:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
	.size	flash_prog_page_en, .-flash_prog_page_en
	.align	2
	.global	ftl_prog_page
	.type	ftl_prog_page, %function
ftl_prog_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w19, w0, 255
	stp	x21, x22, [sp, 32]
	mov	w20, w1
	mov	x21, x2
	mov	x22, x3
	str	x23, [sp, 48]
	mov	w23, w4
	bl	sblk_wait_write_queue_completed
	mov	w0, w19
	mov	w5, 1
	mov	w4, w23
	mov	x3, x22
	mov	x2, x21
	mov	w1, w20
	bl	flash_prog_page_en
	mov	w19, w0
	cmn	w0, #1
	bne	.L2626
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 576
	mov	w2, 2678
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	adrp	x0, .LC185
	mov	w1, w20
	add	x0, x0, :lo12:.LC185
	bl	printk
.L2626:
	mov	w0, w19
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
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
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR3
	stp	x25, x26, [sp, 64]
	mov	w26, w0
	add	x0, x21, :lo12:.LANCHOR3
	stp	x23, x24, [sp, 48]
	stp	x19, x20, [sp, 16]
	adrp	x22, .LANCHOR5
	stp	x27, x28, [sp, 80]
	adrp	x23, .LANCHOR0
	ldrb	w2, [x0, 1946]
	add	x0, x22, :lo12:.LANCHOR5
	ldr	x0, [x0, 376]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldrh	w1, [x0, 74]
	cmp	w1, 1
	bls	.L2630
	strh	wzr, [x0, 150]
.L2630:
	adrp	x24, .LANCHOR4
	add	x24, x24, :lo12:.LANCHOR4
	add	x19, x22, :lo12:.LANCHOR5
	add	x24, x24, 592
	mov	w25, 0
.L2643:
	add	x2, x23, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w20, [x19, 384]
	ldrh	w27, [x19, 386]
	ldr	x3, [x2, 3384]
	ldrh	w28, [x1, 1410]
	ldr	w0, [x3, 4]
	add	w0, w0, 1
	str	w0, [x3, 4]
	ldr	x0, [x19, 376]
	str	w26, [x0]
	ldr	x3, [x2, 3384]
	ldr	x0, [x19, 376]
	ldrb	w1, [x1, 1946]
	ldr	w3, [x3, 4]
	str	w3, [x0, 4]
	lsl	w1, w1, 9
	ldr	x3, [x19, 376]
	ldr	x0, [x19, 392]
	stp	x2, x3, [x29, 96]
	bl	js_hash
	ldp	x2, x3, [x29, 96]
	str	w0, [x3, 8]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2631
	ldr	x0, [x2, 3384]
	ldrb	w1, [x19, 384]
	ldrh	w2, [x19, 386]
	ldr	w3, [x0, 4]
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	bl	printk
.L2631:
	add	x1, x21, :lo12:.LANCHOR3
	ldrh	w0, [x19, 386]
	ldrh	w1, [x1, 1376]
	cmp	w1, w0
	bhi	.L2632
	add	x20, x23, :lo12:.LANCHOR0
.L2639:
	ldrb	w0, [x19, 385]
	add	w0, w0, 1
	and	w0, w0, 255
	strb	w0, [x19, 385]
	cmp	w0, 7
	bls	.L2633
	mov	x0, 0
.L2638:
	ldr	x2, [x20, 1048]
	add	w1, w0, 8
	and	w25, w0, 65535
	add	x1, x2, w1, sxtw
	ldrb	w2, [x1, 32]
	add	w1, w2, 127
	and	w1, w1, 255
	cmp	w1, 125
	bhi	.L2634
	mov	x1, x24
	mov	w2, 846
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2637:
	strb	w25, [x19, 385]
	mov	w25, 1
.L2633:
	ldrb	w1, [x19, 385]
	ldr	x0, [x20, 1048]
	add	x0, x0, x1
	ldrb	w0, [x0, 40]
	strb	w0, [x19, 384]
	cmp	w0, 255
	beq	.L2639
	add	x27, x21, :lo12:.LANCHOR3
	ldrh	w20, [x27, 1410]
	mul	w20, w20, w0
	mov	w0, 0
	mov	w1, w20
	bl	flash_erase_block
	ldrb	w4, [x27, 1946]
	mov	w1, w20
	ldr	x3, [x19, 376]
	mov	w0, 0
	ldr	x2, [x19, 392]
	add	w20, w20, 1
	bl	ftl_prog_page
	mov	w0, 1
	strh	w0, [x19, 386]
.L2640:
	add	x0, x21, :lo12:.LANCHOR3
	ldr	x3, [x19, 376]
	ldr	x2, [x19, 392]
	mov	w1, w20
	ldrb	w4, [x0, 1946]
	mov	w0, 0
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x19, 386]
	add	w1, w1, 1
	strh	w1, [x19, 386]
	beq	.L2641
	ldrb	w0, [x19, 400]
	cbz	w0, .L2642
.L2641:
	strb	wzr, [x19, 400]
	b	.L2643
.L2634:
	cmp	w2, 255
	bne	.L2637
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L2638
	mov	w25, w0
	b	.L2637
.L2632:
	madd	w20, w20, w28, w27
	cbnz	w0, .L2640
	mov	w1, w20
	bl	flash_erase_block
	b	.L2640
.L2642:
	cbnz	w25, .L2644
.L2652:
	add	x22, x22, :lo12:.LANCHOR5
	ldrb	w0, [x22, 384]
	cmp	w0, 255
	bne	.L2646
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 592
	mov	w2, 890
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2646:
	ldp	x19, x20, [sp, 16]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2644:
	ldrb	w19, [x19, 385]
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	adrp	x24, .LC0
	add	w19, w19, 1
	add	x20, x20, 592
	add	x24, x24, :lo12:.LC0
.L2647:
	cmp	w19, 7
	bhi	.L2652
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x1, [x0, 1048]
	add	w0, w19, 8
	add	x0, x1, w0, sxtw
	ldrb	w25, [x0, 32]
	add	w0, w25, 127
	and	w0, w0, 255
	cmp	w0, 125
	bhi	.L2648
	mov	x1, x20
	mov	w2, 881
	mov	x0, x24
	bl	printk
	bl	dump_stack
.L2649:
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 1410]
	mov	w0, 0
	mul	w1, w1, w25
	bl	flash_erase_block
	b	.L2650
.L2648:
	cmp	w25, 255
	bne	.L2649
.L2650:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L2647
	.size	ftl_info_flush, .-ftl_info_flush
	.align	2
	.global	ftl_info_blk_init
	.type	ftl_info_blk_init, %function
ftl_info_blk_init:
	stp	x29, x30, [sp, -112]!
	mov	w0, 1
	mov	w2, 16384
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR5
	add	x20, x21, :lo12:.LANCHOR5
	stp	x25, x26, [sp, 64]
	adrp	x26, .LC187
	stp	x27, x28, [sp, 80]
	add	x26, x26, :lo12:.LC187
	stp	x23, x24, [sp, 48]
	mov	w27, 21574
	strb	w0, [x20, 400]
	movk	w27, 0x494c, lsl 16
	strb	w0, [x20, 402]
	ldrh	w1, [x22, 1096]
	ldr	x0, [x20, 392]
	strb	wzr, [x20, 401]
	str	x0, [x22, 1104]
	add	x1, x0, x1, lsl 2
	str	x1, [x22, 3384]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x20, 408]
	mov	w2, 16384
	mov	w1, 0
	bl	ftl_memset
	strb	wzr, [x20, 385]
	ldr	x0, [x22, 1048]
	adrp	x22, .LANCHOR3
	add	x22, x22, :lo12:.LANCHOR3
	strh	wzr, [x20, 386]
	ldrb	w0, [x0, 40]
	strb	w0, [x20, 384]
	mov	w20, 7
.L2668:
	add	x0, x19, :lo12:.LANCHOR0
	sxth	w24, w20
	ldr	x1, [x0, 1048]
	add	w0, w20, 8
	add	x0, x1, w0, sxtw
	ldrb	w0, [x0, 32]
	cmp	w0, 255
	bne	.L2667
.L2672:
	sub	w20, w20, #1
	cmn	w20, #1
	bne	.L2668
	mov	w24, 0
.L2669:
	adrp	x23, .LANCHOR2
	ldr	w0, [x23, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2673
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 4800
	mov	w1, w20
	ldr	x0, [x0, 3384]
	ldr	w3, [x0]
	adrp	x0, .LC188
	add	x0, x0, :lo12:.LC188
	bl	printk
.L2673:
	cmn	w20, #1
	bne	.L2674
	add	x21, x21, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 16384
	ldr	x0, [x21, 392]
	bl	ftl_memset
	ldr	x0, [x19, 3384]
	mov	w1, 21574
	movk	w1, 0x494c, lsl 16
	str	w1, [x0]
	mov	w1, 36
	movk	w1, 0x6, lsl 16
	ldr	x0, [x19, 3384]
	str	w1, [x0, 12]
	mov	w0, w20
.L2666:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L2667:
	add	x28, x21, :lo12:.LANCHOR5
	ldrh	w25, [x22, 1410]
	ldrb	w4, [x22, 1946]
	ldr	x3, [x28, 376]
	ldr	x2, [x28, 392]
	mul	w25, w25, w0
	mov	w0, 0
	mov	w1, w25
	bl	ftl_read_page
	mov	w23, w0
	cmn	w0, #1
	bne	.L2670
	ldrb	w4, [x22, 1946]
	add	w1, w25, 1
	ldr	x3, [x28, 376]
	mov	w0, 0
	ldr	x2, [x28, 392]
	bl	ftl_read_page
	mov	w23, w0
.L2670:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2671
	add	x0, x19, :lo12:.LANCHOR0
	mov	w3, 749
	mov	w2, w23
	mov	w1, w20
	ldr	x0, [x0, 3384]
	ldr	w4, [x0]
	mov	x0, x26
	bl	printk
.L2671:
	cmn	w23, #1
	beq	.L2672
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldr	w0, [x0]
	cmp	w0, w27
	bne	.L2672
	mov	w20, w24
	b	.L2669
.L2674:
	add	x0, x19, :lo12:.LANCHOR0
	add	x20, x21, :lo12:.LANCHOR5
	adrp	x22, .LANCHOR3
	add	x22, x22, :lo12:.LANCHOR3
	mov	w4, 4
	mov	w28, 21574
	ldr	x1, [x0, 1048]
	add	w0, w24, 8
	ldr	x3, [x20, 376]
	adrp	x27, .LC189
	ldr	x2, [x20, 392]
	add	x27, x27, :lo12:.LC189
	add	x0, x1, w0, sxtw
	strb	w24, [x20, 385]
	movk	w28, 0x494c, lsl 16
	ldrb	w1, [x0, 32]
	mov	w0, 0
	strb	w1, [x20, 384]
	bl	flash_get_last_written_page
	sxth	w25, w0
	add	w0, w0, 1
	ldrb	w26, [x20, 384]
	and	w24, w0, 65535
	ldrh	w0, [x22, 1410]
	madd	w26, w26, w0, w25
.L2676:
	tbnz	w25, #31, .L2680
	ldrb	w4, [x22, 1946]
	mov	w1, w26
	ldr	x3, [x20, 376]
	mov	w0, 0
	ldr	x2, [x20, 392]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L2677
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldr	w0, [x0]
	cmp	w0, w28
	bne	.L2677
	ldr	x0, [x20, 376]
	ldr	w2, [x0, 8]
	cbnz	w2, .L2678
.L2680:
	add	x21, x21, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR0
	strh	w24, [x21, 386]
	bl	ftl_tmp_into_update
	ldr	x1, [x19, 3384]
	ldr	w0, [x1, 64]
	add	w0, w0, 1
	str	w0, [x1, 64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	ldr	w0, [x23, #:lo12:.LANCHOR2]
	tbnz	x0, 14, .L2679
.L2696:
	mov	w0, 0
	b	.L2666
.L2678:
	ldr	x0, [x20, 392]
	ldrb	w1, [x22, 1946]
	str	w2, [x29, 108]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	w2, [x29, 108]
	cmp	w2, w0
	beq	.L2680
	ldr	x0, [x20, 376]
	ldr	w1, [x0, 8]
	mov	x0, x27
	bl	printk
.L2677:
	sub	w25, w25, #1
	sub	w26, w26, #1
	sxth	w25, w25
	b	.L2676
.L2679:
	ldr	x0, [x19, 3384]
	ldr	w1, [x0, 156]
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	bl	printk
	b	.L2696
	.size	ftl_info_blk_init, .-ftl_info_blk_init
	.align	2
	.global	ftl_ext_info_flush
	.type	ftl_ext_info_flush, %function
ftl_ext_info_flush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1, 1128]
	ldr	w3, [x1, 520]
	cmp	w0, w3
	bls	.L2698
	ldr	w2, [x1, 12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1, 12]
.L2712:
	str	w0, [x1, 520]
	b	.L2699
.L2698:
	bcc	.L2712
.L2699:
	adrp	x22, .LANCHOR4
	add	x22, x22, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR0
	add	x22, x22, 608
	mov	w0, 0
	bl	ftl_total_vpn_update
.L2700:
	adrp	x23, .LANCHOR3
.L2703:
	ldr	x1, [x19, 3384]
	ldr	w0, [x1, 56]
	add	w0, w0, 1
	str	w0, [x1, 56]
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w1, [x1, 140]
	ldrh	w0, [x0, 1376]
	cmp	w1, w0
	bcc	.L2701
	bl	ftl_ext_alloc_new_blk
.L2701:
	ldr	x0, [x19, 3384]
	ldrh	w1, [x0, 130]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2702
	mov	x1, x22
	mov	w2, 2211
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2702:
	add	x25, x23, :lo12:.LANCHOR3
	ldr	x1, [x19, 3384]
	ldrb	w20, [x19, 1205]
	mov	w0, 24
	adrp	x24, .LANCHOR5
	add	x21, x24, :lo12:.LANCHOR5
	sub	w20, w0, w20
	ldrh	w0, [x25, 1304]
	ldrh	w2, [x1, 130]
	sub	w0, w20, w0
	mov	w20, 1
	lsl	w20, w20, w0
	sub	w20, w20, #1
	asr	w26, w2, w0
	and	w20, w20, w2
	ldrh	w0, [x1, 140]
	sxth	w20, w20
	ldrh	w2, [x25, 1410]
	mov	w1, 0
	madd	w20, w20, w2, w0
	ldr	x0, [x21, 376]
	ldrb	w2, [x25, 1946]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x21, 376]
	str	wzr, [x0]
	ldr	x1, [x19, 3384]
	ldr	x0, [x21, 376]
	ldr	w1, [x1, 56]
	str	w1, [x0, 4]
	ldrb	w1, [x25, 1946]
	ldr	x0, [x21, 408]
	ldr	x27, [x21, 376]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	x2, [x21, 408]
	mov	w1, w20
	ldrb	w4, [x25, 1946]
	ldr	x3, [x21, 376]
	str	w0, [x27, 8]
	mov	w0, w26
	bl	ftl_prog_page
	ldr	x2, [x19, 3384]
	ldrh	w1, [x2, 140]
	add	w1, w1, 1
	and	w1, w1, 65535
	strh	w1, [x2, 140]
	cmp	w1, 1
	beq	.L2703
	cmn	w0, #1
	beq	.L2704
	ldrb	w0, [x21, 402]
	cbz	w0, .L2705
.L2704:
	add	x24, x24, :lo12:.LANCHOR5
	strb	wzr, [x24, 402]
	b	.L2700
.L2705:
	mov	w0, 0
	ldr	x27, [sp, 80]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_ext_info_flush, .-ftl_ext_info_flush
	.align	2
	.global	ftl_ext_info_init
	.type	ftl_ext_info_init, %function
ftl_ext_info_init:
	stp	x29, x30, [sp, -128]!
	mov	w4, 4
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR3
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	add	x2, x21, :lo12:.LANCHOR3
	stp	x27, x28, [sp, 80]
	adrp	x25, .LANCHOR5
	ldr	x0, [x22, 3384]
	ldrb	w20, [x22, 1205]
	strh	wzr, [x2, 1944]
	ldrh	w1, [x0, 130]
	mov	w0, 24
	sub	w0, w0, w20
	ldrh	w20, [x2, 1304]
	sub	w0, w0, w20
	mov	w20, 1
	asr	w24, w1, w0
	and	w27, w24, 255
	lsl	w20, w20, w0
	add	x0, x25, :lo12:.LANCHOR5
	sub	w20, w20, #1
	and	w20, w20, w1
	ldr	x3, [x0, 376]
	mov	w1, w20
	ldr	x2, [x0, 408]
	mov	w0, w27
	bl	flash_get_last_written_page
	sxth	w23, w0
	adrp	x0, .LANCHOR2
	stp	x0, x25, [x29, 112]
	ldr	w1, [x0, #:lo12:.LANCHOR2]
	tbz	x1, 12, .L2714
	ldr	x0, [x22, 3384]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	and	w4, w24, 65535
	mov	w3, w23
	mov	w2, 2256
	add	x1, x1, 632
	ldrh	w5, [x0, 130]
	adrp	x0, .LC191
	add	x0, x0, :lo12:.LC191
	bl	printk
.L2714:
	adrp	x26, .LC192
	and	w22, w23, 65535
	add	x25, x21, :lo12:.LANCHOR3
	add	x26, x26, :lo12:.LC192
	mov	w24, 0
.L2715:
	sub	w0, w22, w24
	tbnz	x0, 15, .L2720
	ldr	x0, [x29, 120]
	sub	w1, w23, w24
	ldrb	w4, [x25, 1946]
	add	x28, x0, :lo12:.LANCHOR5
	ldrh	w0, [x25, 1410]
	ldr	x3, [x28, 376]
	madd	w1, w0, w20, w1
	ldr	x2, [x28, 408]
	mov	w0, w27
	bl	flash_read_page_en
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L2716
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 20038
	movk	w1, 0x4549, lsl 16
	ldr	x0, [x0, 1128]
	ldr	w0, [x0]
	cmp	w0, w1
	bne	.L2716
	ldr	x0, [x28, 376]
	ldr	w2, [x0, 8]
	cbnz	w2, .L2717
.L2720:
	bl	zftl_sblk_list_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldrh	w1, [x0, 140]
	cmp	w1, w23
	bgt	.L2719
	add	w22, w22, 1
	strh	w22, [x0, 140]
	bl	ftl_ext_info_flush
.L2719:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x20, [x19, 1128]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x20, 520]
	ldr	x20, [x19, 1128]
	bl	timer_get_time
	mov	w1, -1
	str	w0, [x20, 604]
	ldr	x0, [x19, 1128]
	strh	w1, [x0, 584]
	strh	w1, [x0, 586]
	strh	w1, [x0, 588]
	strh	w1, [x0, 590]
	mov	w1, 65535
	str	w1, [x0, 560]
	mov	w1, -1
	str	w1, [x0, 564]
	ldr	x1, [x29, 112]
	str	wzr, [x0, 608]
	ldr	w1, [x1, #:lo12:.LANCHOR2]
	tbz	x1, 12, .L2722
	ldr	w20, [x0, 12]
	ldr	w19, [x0, 520]
	bl	timer_get_time
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	mov	w4, w0
	mov	w3, w20
	adrp	x0, .LC193
	mov	w2, w19
	add	x1, x1, 632
	add	x0, x0, :lo12:.LC193
	bl	printk
.L2722:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, -1
	ldp	x19, x20, [sp, 16]
	strh	w0, [x21, 1280]
	mov	w0, 0
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L2717:
	ldr	x0, [x28, 408]
	ldrb	w1, [x25, 1946]
	str	w2, [x29, 108]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	w2, [x29, 108]
	cmp	w2, w0
	beq	.L2720
	ldr	x0, [x28, 376]
	ldr	w1, [x0, 8]
	mov	x0, x26
	bl	printk
.L2716:
	add	w24, w24, 1
	b	.L2715
	.size	ftl_ext_info_init, .-ftl_ext_info_init
	.align	2
	.global	ftl_prog_ppa_page
	.type	ftl_prog_ppa_page, %function
ftl_prog_ppa_page:
	stp	x29, x30, [sp, -16]!
	adrp	x4, .LANCHOR0+1205
	mov	w5, 1
	add	x29, sp, 0
	ldrb	w6, [x4, #:lo12:.LANCHOR0+1205]
	mov	w4, 24
	sub	w4, w4, w6
	lsl	w7, w5, w4
	sub	w7, w7, #1
	lsl	w5, w5, w6
	sub	w6, w5, #1
	lsr	w5, w0, w4
	mov	w4, w3
	mov	x3, x2
	mov	x2, x1
	and	w1, w7, w0
	and	w0, w6, w5
	bl	ftl_prog_page
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_prog_ppa_page, .-ftl_prog_ppa_page
	.align	2
	.global	ftl_write_last_log_page
	.type	ftl_write_last_log_page, %function
ftl_write_last_log_page:
	ldrh	w1, [x0, 6]
	cmp	w1, 1
	bne	.L2737
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	stp	x21, x22, [sp, 32]
	str	x23, [sp, 48]
	mov	x19, x0
	ldr	x21, [x20, 1928]
	ldrh	w23, [x0, 12]
	bl	ftl_get_new_free_page
	mov	w22, w0
	cmn	w0, #1
	beq	.L2738
	ldrh	w0, [x19]
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	add	x21, x21, w23, uxth 2
	bl	ftl_vpn_decrement
	ldr	x0, [x19, 416]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	str	w1, [x0]
	ldrb	w0, [x20, 1321]
	ldrh	w1, [x20, 1376]
	ldr	x23, [x19, 416]
	mul	w1, w1, w0
	mov	x0, x21
	lsl	w1, w1, 2
	bl	js_hash
	str	w0, [x23, 4]
	mov	x2, 0
	mov	w0, 2
	ldr	x1, [x19, 416]
	stp	wzr, wzr, [x1, 8]
	str	wzr, [x1, 16]!
	bl	ftl_debug_info_fill
	ldrb	w3, [x20, 1946]
	mov	x1, x21
	ldr	x2, [x19, 416]
	mov	w0, w22
	bl	ftl_prog_ppa_page
.L2738:
	mov	w0, 0
	ldr	x23, [sp, 48]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L2737:
	mov	w0, -1
	ret
	.size	ftl_write_last_log_page, .-ftl_write_last_log_page
	.align	2
	.global	ftl_dump_write_open_sblk
	.type	ftl_dump_write_open_sblk, %function
ftl_dump_write_open_sblk:
	sub	sp, sp, #224
	stp	x29, x30, [sp, 48]
	add	x29, sp, 48
	stp	x19, x20, [sp, 64]
	and	w20, w0, 65535
	stp	x21, x22, [sp, 80]
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 96]
	stp	x25, x26, [sp, 112]
	stp	x27, x28, [sp, 128]
	ldrh	w0, [x0, 1096]
	cmp	w0, w20
	bls	.L2743
	adrp	x23, .LANCHOR3
	add	x0, x23, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1336]
	cbnz	w1, .L2745
	ldrb	w0, [x0, 1322]
	cbz	w0, .L2743
.L2745:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1212]
	cbnz	w1, .L2743
	ldr	x0, [x0, 1104]
	ubfiz	x14, x20, 2, 16
	add	x0, x0, x14
	ldrb	w0, [x0, 2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2767
	add	x0, x23, :lo12:.LANCHOR3
	ldrb	w26, [x0, 1320]
.L2746:
	add	x22, x29, 176
	add	x24, x23, :lo12:.LANCHOR3
	mov	w0, w20
	adrp	x27, .LC195
	add	x27, x27, :lo12:.LC195
	mov	w28, 0
	strh	w20, [x22, -32]!
	add	x1, x22, 16
	bl	ftl_get_blk_list_in_sblk
	ldrh	w1, [x24, 1376]
	and	w0, w0, 255
	strb	w0, [x29, 153]
	strh	wzr, [x29, 146]
	strb	wzr, [x29, 149]
	mul	w0, w0, w1
	strh	wzr, [x29, 154]
	strh	w0, [x29, 150]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0, 1104]
	add	x1, x0, x14
	ldr	w5, [x0, x14]
	ldrb	w2, [x1, 2]
	mov	w1, w20
	ldrh	w4, [x0, x14]
	adrp	x0, .LC194
	ubfx	x5, x5, 11, 8
	add	x0, x0, :lo12:.LC194
	ubfx	x3, x2, 3, 2
	and	w4, w4, 2047
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	mov	x11, x24
	mov	w9, 0
	mov	w10, 0
.L2747:
	ldrh	w0, [x11, 1376]
	cmp	w0, w28
	bls	.L2755
	lsl	w24, w28, 1
	mov	w10, 0
	sub	w0, w24, #1
	add	w24, w24, w28
	sub	w24, w24, #1
	str	w0, [x29, 136]
	b	.L2758
.L2767:
	mov	w26, 1
	b	.L2746
.L2756:
	ldrh	w12, [x22, x25]
	mov	w0, 65535
	cmp	w12, w0
	bne	.L2748
.L2754:
	add	w9, w9, 1
	and	w9, w9, 65535
.L2749:
	cmp	w26, w9
	bcs	.L2756
	add	w10, w10, 1
	and	w10, w10, 65535
.L2758:
	ldrb	w0, [x29, 153]
	cmp	w0, w10
	bls	.L2757
	sxtw	x25, w10
	mov	w9, 1
	add	x25, x25, 8
	lsl	x25, x25, 1
	b	.L2749
.L2748:
	ldrh	w3, [x11, 1410]
	cmp	w26, 3
	mul	w3, w3, w12
	add	w0, w3, w9
	bne	.L2750
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1, 1213]
	cbz	w1, .L2751
	ldrb	w3, [x11, 1320]
	add	w0, w0, w24
.L2781:
	orr	w3, w0, w3, lsl 24
	b	.L2752
.L2751:
	add	w3, w28, w3
	orr	w3, w3, w9, lsl 24
.L2752:
	str	w3, [x19, 40]
	mov	w1, 1
	str	x11, [x29, 104]
	mov	x0, x19
	str	w9, [x29, 112]
	stp	w12, w10, [x29, 124]
	str	w3, [x29, 132]
	bl	sblk_read_page
	ldr	w13, [x19, 52]
	ldr	w9, [x29, 112]
	cmp	w13, 512
	ldr	w3, [x29, 132]
	ccmn	w13, #1, 4, ne
	ldr	x11, [x29, 104]
	ldp	w12, w10, [x29, 124]
	bne	.L2754
	ldr	x1, [x19, 24]
	mov	w4, w13
	ldr	x0, [x19, 8]
	str	x11, [x29, 112]
	stp	w9, w10, [x29, 124]
	ldr	w2, [x1, 12]
	str	w2, [sp, 32]
	str	w13, [x29, 132]
	ldr	w2, [x1, 8]
	str	w2, [sp, 24]
	ldr	w2, [x1, 4]
	str	w2, [sp, 16]
	mov	w2, w28
	ldr	w1, [x1]
	str	w1, [sp, 8]
	ldr	w1, [x0, 12]
	str	w1, [sp]
	mov	w1, w12
	ldp	w5, w6, [x0]
	ldr	w7, [x0, 8]
	mov	x0, x27
	bl	printk
	ldr	w13, [x29, 132]
	ldp	w9, w10, [x29, 124]
	cmp	w13, 512
	ldr	x11, [x29, 112]
	bne	.L2754
.L2755:
	add	x24, x23, :lo12:.LANCHOR3
	mov	w4, w9
	mov	w3, w10
	mov	w2, w28
	mov	w1, w20
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	bl	printk
	ldr	x0, [x19, 8]
	mov	w1, 0
	ldrb	w2, [x24, 1946]
	adrp	x26, .LC197
	add	x26, x26, :lo12:.LC197
	lsl	w2, w2, 9
	bl	ftl_memset
	ldr	x0, [x19, 24]
	mov	w1, 0
	ldrb	w2, [x24, 1946]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2759:
	add	x24, x23, :lo12:.LANCHOR3
	ldrh	w0, [x24, 1376]
	cmp	w0, w28
	bls	.L2765
	add	x0, x21, :lo12:.LANCHOR0
	lsl	w27, w28, 1
	mov	w25, 0
	str	x0, [x29, 136]
	b	.L2766
.L2750:
	cmp	w26, 2
	bne	.L2753
	ldr	w1, [x29, 136]
	ldrb	w3, [x11, 1320]
	add	w0, w1, w0
	b	.L2781
.L2753:
	add	w3, w28, w3
	b	.L2752
.L2757:
	add	w8, w28, 1
	and	w28, w8, 65535
	b	.L2747
.L2764:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2760
	mov	w2, w25
	mov	w1, w28
	mov	x0, x26
	bl	printk
.L2760:
	ldrb	w1, [x24, 1336]
	sxtw	x0, w25
	ldrh	w2, [x24, 1410]
	cbz	w1, .L2761
	add	x0, x0, 8
	ldrh	w1, [x22, x0, lsl 1]
	mov	x0, x19
	mul	w1, w1, w2
	orr	w1, w1, w28
	str	w1, [x19, 40]
	bl	sblk_3d_tlc_dump_prog
.L2762:
	add	w25, w25, 1
	and	w25, w25, 65535
.L2766:
	ldrb	w0, [x29, 153]
	cmp	w0, w25
	bhi	.L2764
	add	w8, w28, 1
	and	w28, w8, 65535
	b	.L2759
.L2761:
	add	x0, x0, 8
	ldrb	w1, [x24, 1320]
	cmp	w1, 2
	ldrh	w1, [x22, x0, lsl 1]
	mul	w1, w1, w2
	bne	.L2763
	orr	w1, w1, w27
	mov	x0, x19
	orr	w1, w1, 33554432
	str	w1, [x19, 40]
	bl	sblk_mlc_dump_prog
	b	.L2762
.L2763:
	ldr	x0, [x29, 136]
	mov	w6, 1
	ldrb	w4, [x24, 1946]
	orr	w1, w1, w28
	ldr	x3, [x19, 24]
	mov	w5, 0
	str	w1, [x19, 40]
	ldrb	w2, [x0, 1205]
	mov	w0, 24
	sub	w0, w0, w2
	lsl	w6, w6, w2
	ldr	x2, [x19, 8]
	sub	w6, w6, #1
	lsl	w7, w6, w0
	lsr	w0, w1, w0
	bic	w1, w1, w7
	and	w0, w0, w6
	bl	flash_prog_page_en
	b	.L2762
.L2765:
	mov	x0, x19
	bl	zbuf_free
	adrp	x0, .LC198
	mov	w1, w20
	add	x0, x0, :lo12:.LC198
	bl	printk
.L2743:
	ldp	x19, x20, [sp, 64]
	ldp	x21, x22, [sp, 80]
	ldp	x23, x24, [sp, 96]
	ldp	x25, x26, [sp, 112]
	ldp	x27, x28, [sp, 128]
	ldp	x29, x30, [sp, 48]
	add	sp, sp, 224
	ret
	.size	ftl_dump_write_open_sblk, .-ftl_dump_write_open_sblk
	.align	2
	.global	gc_ink_check_sblk
	.type	gc_ink_check_sblk, %function
gc_ink_check_sblk:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	ldrh	w1, [x0, 5526]
	ldr	x21, [x0, 5600]
	cmp	w1, 3
	bhi	.L2783
	adrp	x0, .L2785
	mov	x20, x19
	add	x0, x0, :lo12:.L2785
	ldrh	w0, [x0,w1,uxtw #1]
	adr	x1, .Lrtx2785
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx2785:
	.section	.rodata
	.align	0
	.align	2
.L2785:
	.2byte	(.L2784 - .Lrtx2785) / 4
	.2byte	(.L2786 - .Lrtx2785) / 4
	.2byte	(.L2787 - .Lrtx2785) / 4
	.2byte	(.L2788 - .Lrtx2785) / 4
	.text
.L2784:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19, 3372]
	cmp	w0, 7
	bls	.L2782
	ldrb	w0, [x19, 3353]
	cmp	w0, 2
	bls	.L2782
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	add	x23, x20, 1400
	mov	w1, 0
	mov	x0, x23
	bl	_list_get_gc_head_node
	and	w0, w0, 65535
	mov	w22, 65535
	cmp	w0, w22
	beq	.L2782
	ldr	x1, [x19, 1104]
	ubfiz	x0, x0, 2, 16
	ldrh	w0, [x1, x0]
	and	w0, w0, 2047
	cmp	w0, 2
	bgt	.L2782
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x19, 5600]
	add	x21, x19, 3416
	cbz	x0, .L2782
	add	x2, x19, 3372
	mov	w1, 0
	mov	x0, x23
	bl	_list_pop_index_node
	and	w14, w0, 65535
	cmp	w14, w22
	bne	.L2792
	ldr	x0, [x19, 5600]
	bl	zbuf_free
	str	xzr, [x19, 5600]
.L2782:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L2792:
	mov	w0, w14
	add	x1, x21, 2162
	bl	ftl_get_blk_list_in_sblk
	strb	w0, [x21, 2155]
	mov	w0, 1
	strh	w0, [x19, 5526]
	ldr	x0, [x19, 1104]
	strh	w14, [x19, 5562]
	ubfiz	x14, x14, 2, 16
	strh	wzr, [x19, 5564]
	ldrb	w2, [x20, 1946]
	ldrh	w0, [x0, x14]
	tbz	x0, 0, .L2793
	ldr	x0, [x19, 5600]
	lsl	w2, w2, 9
	mov	w1, 85
.L2808:
	ldr	x0, [x0, 8]
	bl	ftl_memset
	b	.L2782
.L2793:
	lsl	w2, w2, 9
	mov	w1, 170
	ldr	x0, [x19, 5600]
	b	.L2808
.L2786:
	add	x20, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrh	w0, [x20, 5562]
	bl	ftl_erase_sblk
	mov	w0, 2
	strh	w0, [x20, 5526]
	b	.L2782
.L2787:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR3
	mov	w20, 65280
	add	x22, x19, 3416
	add	x24, x24, :lo12:.LANCHOR3
	mov	w23, 0
	movk	w20, 0x55aa, lsl 16
	bl	sblk_wait_write_queue_completed
.L2794:
	ldrb	w0, [x22, 2155]
	cmp	w0, w23
	bhi	.L2796
	ldrh	w0, [x22, 2148]
	adrp	x1, .LANCHOR3+1376
	add	w0, w0, 1
	ldrh	w1, [x1, #:lo12:.LANCHOR3+1376]
	and	w0, w0, 65535
	strh	w0, [x22, 2148]
	cmp	w1, w0
	bhi	.L2782
	mov	w0, 3
	strh	wzr, [x22, 2148]
	strh	w0, [x22, 2110]
	b	.L2782
.L2796:
	add	x0, x22, w23, sxtw 1
	mov	w1, 65535
	ldrh	w0, [x0, 2162]
	cmp	w0, w1
	beq	.L2795
	ldrh	w2, [x22, 2148]
	mov	w6, 1
	ldrh	w1, [x24, 1410]
	mov	w5, 0
	madd	w1, w1, w0, w2
	ldr	x0, [x21, 8]
	str	w1, [x0]
	ldr	x0, [x21, 8]
	str	w20, [x0, 4]
	ldr	x0, [x21, 24]
	str	wzr, [x0]
	mov	w0, 24
	ldrb	w2, [x19, 1205]
	ldrb	w4, [x24, 1946]
	sub	w0, w0, w2
	ldr	x3, [x21, 24]
	lsl	w6, w6, w2
	ldr	x2, [x21, 8]
	sub	w6, w6, #1
	lsl	w7, w6, w0
	lsr	w0, w1, w0
	bic	w1, w1, w7
	and	w0, w0, w6
	bl	flash_prog_page_en
.L2795:
	add	w23, w23, 1
	and	w23, w23, 65535
	b	.L2794
.L2788:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x25, .LANCHOR3
	add	x19, x19, 3416
	add	x25, x25, :lo12:.LANCHOR3
	mov	w22, 0
	mov	w26, 65535
	bl	sblk_wait_write_queue_completed
.L2797:
	ldrb	w0, [x19, 2155]
	cmp	w0, w22
	bhi	.L2801
	ldrh	w0, [x19, 2148]
	adrp	x1, .LANCHOR3+1376
	add	w0, w0, 1
	ldrh	w1, [x1, #:lo12:.LANCHOR3+1376]
	and	w0, w0, 65535
	strh	w0, [x19, 2148]
	cmp	w1, w0
	bhi	.L2782
	ldr	x0, [x19, 2184]
	strh	wzr, [x19, 2110]
	bl	zbuf_free
	str	xzr, [x19, 2184]
	ldrh	w0, [x19, 2112]
	cmp	w0, 15
	bhi	.L2802
	add	w1, w0, 1
	add	x0, x19, w0, sxtw 1
	strh	w1, [x19, 2112]
	ldrh	w1, [x19, 2146]
	strh	w1, [x0, 2114]
.L2803:
	add	x19, x20, :lo12:.LANCHOR0
	adrp	x0, .LC199
	add	x0, x0, :lo12:.LC199
	ldrh	w2, [x19, 5528]
	ldrh	w1, [x19, 5562]
	bl	printk
	b	.L2782
.L2801:
	add	x24, x19, w22, sxtw 1
	add	x24, x24, 16
	ldrh	w0, [x24, 2146]
	cmp	w0, w26
	beq	.L2799
	ldrh	w1, [x19, 2148]
	ldrh	w23, [x25, 1410]
	madd	w23, w23, w0, w1
	mov	w1, 1
	str	w23, [x21, 40]
	mov	x0, x21
	bl	sblk_read_page
	ldr	x0, [x21, 8]
	ldr	w0, [x0]
	cmp	w23, w0
	beq	.L2799
	mov	w0, w23
	bl	ftl_mask_bad_block
	mov	w0, -1
	strh	w0, [x24, 2146]
.L2799:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L2797
.L2802:
	ldrh	w0, [x19, 2146]
	bl	zftl_insert_free_list
	b	.L2803
.L2783:
	strh	wzr, [x0, 5526]
	b	.L2782
	.size	gc_ink_check_sblk, .-gc_ink_check_sblk
	.align	2
	.global	ftl_ink_check_sblk
	.type	ftl_ink_check_sblk, %function
ftl_ink_check_sblk:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x22, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	and	w20, w0, 65535
	stp	x23, x24, [sp, 48]
	ubfiz	x25, x20, 2, 16
	stp	x27, x28, [sp, 80]
	mov	w1, w20
	ldr	x0, [x22, 1104]
	ldr	w3, [x0, x25]
	ldrh	w2, [x0, x25]
	adrp	x0, .LC200
	add	x0, x0, :lo12:.LC200
	ubfx	x3, x3, 11, 8
	and	w2, w2, 2047
	bl	printk
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2809
	ldrh	w0, [x22, 1096]
	cmp	w0, w20
	bls	.L2809
	add	x24, x29, 144
	mov	w1, 0
	mov	w0, w20
	bl	ftl_erase_sblk
	mov	w0, w20
	strh	w20, [x24, -32]!
	add	x1, x24, 16
	bl	ftl_get_blk_list_in_sblk
	strb	w0, [x29, 121]
	mov	w0, 1
	bl	buf_alloc
	mov	x19, x0
	ldr	x0, [x22, 1104]
	adrp	x22, .LANCHOR3
	ldrh	w0, [x0, x25]
	and	w0, w0, 2047
	cmp	w0, 1
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w2, [x0, 1946]
	lsl	w2, w2, 9
	bgt	.L2811
	mov	w1, 85
.L2827:
	ldr	x0, [x19, 8]
	add	x27, x22, :lo12:.LANCHOR3
	mov	w26, 0
	bl	ftl_memset
	bl	sblk_wait_write_queue_completed
	mov	w9, 65280
	mov	w8, 65535
	movk	w9, 0x55aa, lsl 16
.L2813:
	ldrh	w0, [x27, 1376]
	cmp	w0, w26
	bls	.L2816
	mov	w23, 0
	add	x7, x21, :lo12:.LANCHOR0
	mov	w28, 24
	b	.L2817
.L2811:
	mov	w1, 170
	b	.L2827
.L2815:
	add	x0, x24, w23, sxtw 1
	ldrh	w0, [x0, 16]
	cmp	w0, w8
	beq	.L2814
	ldrh	w1, [x27, 1410]
	mov	w6, 1
	stp	w8, w9, [x29, 96]
	mov	w5, 0
	str	x7, [x29, 104]
	madd	w1, w1, w0, w26
	ldr	x0, [x19, 8]
	str	w1, [x0]
	ldr	x0, [x19, 8]
	str	w9, [x0, 4]
	ldr	x0, [x19, 24]
	str	wzr, [x0]
	ldrb	w2, [x7, 1205]
	ldrb	w4, [x27, 1946]
	sub	w0, w28, w2
	ldr	x3, [x19, 24]
	lsl	w6, w6, w2
	ldr	x2, [x19, 8]
	sub	w6, w6, #1
	lsl	w10, w6, w0
	lsr	w0, w1, w0
	bic	w1, w1, w10
	and	w0, w0, w6
	bl	flash_prog_page_en
	ldp	w8, w9, [x29, 96]
	ldr	x7, [x29, 104]
.L2814:
	add	w23, w23, 1
	and	w23, w23, 65535
.L2817:
	ldrb	w0, [x29, 121]
	cmp	w0, w23
	bhi	.L2815
	add	w26, w26, 1
	and	w26, w26, 65535
	b	.L2813
.L2816:
	add	x22, x22, :lo12:.LANCHOR3
	mov	w26, 0
	mov	w27, 65535
.L2818:
	ldrh	w0, [x22, 1376]
	cmp	w0, w26
	bls	.L2823
	mov	w23, 0
	b	.L2824
.L2822:
	sxtw	x28, w23
	add	x28, x28, 8
	lsl	x28, x28, 1
	ldrh	w0, [x24, x28]
	cmp	w0, w27
	beq	.L2820
	ldrh	w3, [x22, 1410]
	mov	w1, 1
	madd	w3, w3, w0, w26
	mov	x0, x19
	str	w3, [x19, 40]
	str	w3, [x29, 104]
	bl	sblk_read_page
	ldr	x0, [x19, 8]
	ldr	w3, [x29, 104]
	ldr	w0, [x0]
	cmp	w3, w0
	beq	.L2820
	mov	w0, w3
	bl	ftl_mask_bad_block
	mov	w0, -1
	strh	w0, [x24, x28]
.L2820:
	add	w23, w23, 1
	and	w23, w23, 65535
.L2824:
	ldrb	w0, [x29, 121]
	cmp	w0, w23
	bhi	.L2822
	add	w2, w26, 1
	and	w26, w2, 65535
	b	.L2818
.L2823:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x0, x19
	bl	zbuf_free
	mov	w1, w20
	ldr	x0, [x21, 1104]
	ldr	w3, [x0, x25]
	ldrh	w2, [x0, x25]
	adrp	x0, .LC201
	add	x0, x0, :lo12:.LC201
	ubfx	x3, x3, 11, 8
	and	w2, w2, 2047
	bl	printk
.L2809:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ftl_ink_check_sblk, .-ftl_ink_check_sblk
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
	stp	x29, x30, [sp, -80]!
	cmp	w0, 5
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	and	w21, w0, 65535
	stp	x19, x20, [sp, 16]
	mov	w22, w0
	stp	x23, x24, [sp, 48]
	mov	w1, w21
	cset	w23, eq
	mov	w0, 0
	str	x25, [sp, 64]
	bl	zftl_get_free_sblk
	and	w20, w0, 65535
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2830
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ubfiz	x25, x20, 2, 16
	lsl	w23, w23, 1
	mov	w24, w20
	mov	x19, x0
	ldr	x21, [x1, 1104]
	add	x21, x21, x25
	ldrb	w1, [x21, 2]
	tst	w1, 224
	beq	.L2831
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 656
	mov	w2, 1012
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2831:
	ldrb	w0, [x21, 2]
	bfi	w0, w22, 5, 3
	ubfx	x1, x0, 3, 2
	orr	w1, w23, w1
	bfi	w0, w1, 3, 2
	strb	w0, [x21, 2]
	and	w1, w0, 24
	cmp	w1, 24
	bne	.L2832
	cbnz	w23, .L2832
	mov	w1, 1
	bfi	w0, w1, 3, 2
	strb	w0, [x21, 2]
.L2832:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldrh	w1, [x1, 150]
	cbz	w1, .L2833
	ldr	x0, [x0, 1104]
	ldrh	w0, [x0, x25]
	tst	x0, 2047
	bne	.L2833
	cbnz	w23, .L2833
	mov	w0, w24
	bl	ftl_ink_check_sblk
.L2833:
	mov	w0, w20
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L2830:
	bl	print_ftl_debug_info
	adrp	x19, .LC202
	mov	w2, w22
	add	x19, x19, :lo12:.LC202
	mov	w1, w20
	mov	x0, x19
	bl	printk
	mov	w1, w21
	mov	w0, 0
	bl	zftl_get_free_sblk
	and	w20, w0, 65535
	mov	w2, w22
	mov	w1, w20
	mov	x0, x19
	bl	printk
	bl	dump_all_list_info
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 656
	mov	w2, 1031
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L2833
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	adrp	x22, .LC203
	stp	x19, x20, [sp, 16]
	adrp	x24, .LANCHOR3
	stp	x25, x26, [sp, 64]
	mov	x19, x0
	mov	w23, w1
	add	x25, x24, :lo12:.LANCHOR3
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC203
.L2843:
	mov	w26, 65535
.L2844:
	mov	w0, w23
	bl	ftl_alloc_sblk
	and	w20, w0, 65535
	cmp	w20, w26
	beq	.L2844
	mov	w1, 0
	mov	w0, w20
	bl	ftl_erase_sblk
	add	x1, x19, 16
	mov	w0, w20
	bl	ftl_get_blk_list_in_sblk
	strh	w20, [x19]
	add	x2, x24, :lo12:.LANCHOR3
	and	w0, w0, 255
	strb	w0, [x19, 9]
	cmp	w23, 2
	strh	wzr, [x19, 2]
	ldrh	w1, [x2, 1376]
	strb	wzr, [x19, 5]
	strh	wzr, [x19, 10]
	strb	w23, [x19, 4]
	mul	w0, w1, w0
	strh	w0, [x19, 6]
	beq	.L2847
	ldrb	w0, [x2, 1321]
	mul	w0, w1, w0
	and	w0, w0, 65535
.L2845:
	ldrb	w2, [x25, 1321]
	ubfiz	x26, x20, 1, 16
	ldr	x3, [x25, 1928]
	strh	w0, [x19, 12]
	mul	w2, w2, w1
	add	x0, x3, w0, uxth 2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x21, 1120]
	ldrh	w1, [x19, 6]
	strh	w1, [x0, x26]
	ldrb	w0, [x19, 9]
	cbnz	w0, .L2842
	mov	w1, w20
	mov	x0, x22
	bl	printk
	ldr	x0, [x21, 1120]
	mov	w1, -1
	strh	w1, [x0, x26]
	mov	w0, 7
	strb	w0, [x19, 4]
	b	.L2843
.L2847:
	mov	w0, 0
	b	.L2845
.L2842:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_open_sblk_init, .-ftl_open_sblk_init
	.align	2
	.global	pm_alloc_new_blk
	.type	pm_alloc_new_blk, %function
pm_alloc_new_blk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR3
	add	x2, x22, :lo12:.LANCHOR3
	ldr	x1, [x0, 3384]
	ldrb	w2, [x2, 1321]
	ldrh	w0, [x1, 690]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 690]
	cmp	w2, w0
	bls	.L2851
	add	x0, x1, w0, sxtw 1
	ldrh	w1, [x0, 672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2852
.L2851:
	adrp	x23, .LC203
	add	x20, x19, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC203
	mov	w24, 65535
.L2853:
	mov	w0, 1
	bl	ftl_alloc_sblk
	and	w21, w0, 65535
	cmp	w21, w24
	beq	.L2853
	mov	w1, 0
	mov	w0, w21
	bl	ftl_erase_sblk
	ldr	x1, [x20, 3384]
	mov	w0, w21
	add	x1, x1, 672
	bl	ftl_get_blk_list_in_sblk
	tst	w0, 65535
	bne	.L2854
	mov	w1, w21
	mov	x0, x23
	bl	printk
	ldr	x0, [x20, 1104]
	add	x21, x0, w21, uxth 2
	ldrb	w0, [x21, 2]
	orr	w0, w0, -32
	strb	w0, [x21, 2]
	b	.L2853
.L2854:
	ldr	x0, [x20, 3384]
	adrp	x1, .LANCHOR5+424
	mov	w2, 1
	mov	w20, 0
	add	x0, x0, 416
	str	w2, [x1, #:lo12:.LANCHOR5+424]
	mov	w1, 65535
	strh	wzr, [x0, 274]
.L2856:
	ldrh	w2, [x0]
	cmp	w2, w1
	beq	.L2855
	add	w20, w20, 1
	add	x0, x0, 2
	and	w20, w20, 65535
	cmp	w20, 128
	bne	.L2856
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 672
	mov	w2, 264
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2855:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	add	x20, x0, w20, sxtw 1
	strh	w21, [x20, 416]
	ldrh	w1, [x0, 688]
	add	w1, w1, 1
	strh	w1, [x0, 688]
.L2852:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldrh	w0, [x1, 690]
	add	x0, x0, 336
	ldrh	w20, [x1, x0, lsl 1]
	mov	w0, 65533
	sub	w1, w20, #1
	cmp	w0, w1, uxth
	bcs	.L2858
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 672
	mov	w2, 270
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2858:
	add	x19, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	mov	w2, 24
	ldrb	w1, [x19, 1205]
	ldr	x0, [x19, 3384]
	sub	w2, w2, w1
	ldrh	w1, [x22, 1304]
	sub	w2, w2, w1
	strh	wzr, [x0, 696]
	asr	w4, w20, w2
	strh	w20, [x0, 692]
	strh	w4, [x0, 694]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2861
	mov	w0, 1
	and	w4, w4, 65535
	lsl	w2, w0, w2
	mov	w3, w20
	adrp	x0, .LC204
	sub	w2, w2, #1
	mov	w1, w20
	add	x0, x0, :lo12:.LC204
	bl	printk
.L2861:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
	.size	pm_alloc_new_blk, .-pm_alloc_new_blk
	.align	2
	.global	pm_write_page
	.type	pm_write_page, %function
pm_write_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	w21, w0
	stp	x23, x24, [sp, 48]
	mov	x24, x1
	stp	x25, x26, [sp, 64]
	adrp	x25, .LANCHOR0
	adrp	x26, .LANCHOR3
	add	x22, x25, :lo12:.LANCHOR0
	add	x23, x26, :lo12:.LANCHOR3
	stp	x19, x20, [sp, 16]
	stp	x27, x28, [sp, 80]
.L2869:
	add	x0, x25, :lo12:.LANCHOR0
	ldr	x0, [x0, 3384]
	ldr	w1, [x0, 48]
	ldrh	w2, [x0, 696]
	add	w1, w1, 1
	str	w1, [x0, 48]
	add	x1, x26, :lo12:.LANCHOR3
	ldrh	w1, [x1, 1376]
	cmp	w2, w1
	bcs	.L2870
	ldrh	w1, [x0, 692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2871
.L2870:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L2871:
	ldr	x0, [x22, 3384]
	ldrh	w1, [x0, 692]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2872
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 696
	mov	w2, 303
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L2872:
	ldr	x0, [x22, 3384]
	adrp	x19, .LANCHOR5
	ldrh	w1, [x23, 1410]
	add	x20, x19, :lo12:.LANCHOR5
	mov	w2, 64
	ldrh	w27, [x0, 692]
	ldrh	w0, [x0, 696]
	madd	w27, w27, w1, w0
	ldr	x0, [x20, 432]
	mov	w1, 0
	bl	ftl_memset
	ldr	x0, [x20, 432]
	str	w21, [x0]
	ldr	x1, [x22, 3384]
	ldr	x0, [x20, 432]
	ldr	w1, [x1, 48]
	str	w1, [x0, 4]
	mov	x0, x24
	ldrb	w1, [x23, 1946]
	ldr	x28, [x20, 432]
	lsl	w1, w1, 9
	bl	js_hash
	ldrb	w4, [x23, 1946]
	mov	x2, x24
	str	w0, [x28, 8]
	mov	w1, w27
	ldr	x0, [x22, 3384]
	ldr	x3, [x20, 432]
	ldrb	w0, [x0, 694]
	bl	ftl_prog_page
	ldr	x2, [x22, 3384]
	ldrh	w1, [x2, 696]
	add	w1, w1, 1
	and	w1, w1, 65535
	strh	w1, [x2, 696]
	cmp	w1, 1
	beq	.L2873
	ldrb	w1, [x20, 440]
	cbz	w1, .L2874
.L2873:
	add	x19, x19, :lo12:.LANCHOR5
	strb	wzr, [x19, 440]
	b	.L2869
.L2874:
	cmn	w0, #1
	bne	.L2876
	mov	w1, w27
	adrp	x0, .LC205
	add	x0, x0, :lo12:.LC205
	bl	printk
	b	.L2869
.L2876:
	ldrh	w0, [x2, 698]
	cmp	w21, w0
	bcs	.L2877
	add	x21, x2, w21, uxtw 2
	str	w27, [x21, 704]
.L2877:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
	.size	pm_write_page, .-pm_write_page
	.align	2
	.global	flash_info_flush
	.type	flash_info_flush, %function
flash_info_flush:
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR2
	add	x29, sp, 0
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	tbz	x0, 12, .L2883
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 712
	mov	w1, 365
	add	x0, x0, :lo12:.LC135
	bl	printk
.L2883:
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	adrp	x20, .LANCHOR0
	add	x22, x20, :lo12:.LANCHOR0
	mov	w2, 64
	mov	w1, 0
	ldr	x0, [x19, 448]
	adrp	x23, .LC207
	mov	w25, 21321
	add	x23, x23, :lo12:.LC207
	mov	w24, 0
	movk	w25, 0x5359, lsl 16
	bl	ftl_memset
	ldr	x1, [x22, 1048]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	bl	rknand_print_hex
	ldr	x21, [x22, 1048]
	add	x0, x21, 16
	ldr	w1, [x21, 8]
	bl	js_hash
	str	w0, [x21, 12]
.L2884:
	ldrb	w26, [x19, 456]
	mov	x0, x23
	ldrh	w21, [x19, 458]
	mov	w1, w26
	ldrh	w20, [x22, 2]
	mov	w2, w21
	bl	printk
	ldrh	w0, [x19, 172]
	ldrh	w1, [x19, 458]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2885
	ldr	x1, [x22, 1048]
	strh	wzr, [x19, 458]
	ldr	w0, [x1, 4]
	add	w0, w0, 1
	str	w0, [x1, 4]
	ldrb	w0, [x19, 456]
	ldr	x21, [x22, 1048]
	ldrb	w1, [x19, 457]
	strb	w0, [x19, 457]
	mov	x0, x21
	strb	w1, [x19, 456]
	ldrh	w1, [x21, 16]
	add	w1, w1, 1
	strh	w1, [x0, 16]!
	ldr	w1, [x21, 8]
	bl	js_hash
	ldrb	w20, [x19, 456]
	str	w0, [x21, 12]
	ldrh	w0, [x22, 2]
	mul	w20, w20, w0
.L2894:
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L2886
.L2889:
	mov	w24, 1
	b	.L2884
.L2885:
	madd	w20, w20, w26, w21
	cbz	w1, .L2894
.L2886:
	ldr	x1, [x22, 1048]
	mov	w5, 1
	ldr	x0, [x19, 448]
	mov	w4, 4
	ldr	w1, [x1, 4]
	str	w1, [x0]
	mov	w1, w20
	ldr	x0, [x19, 448]
	str	w25, [x0, 4]
	mov	w0, 0
	ldr	x3, [x19, 448]
	ldr	x2, [x22, 1048]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x19, 458]
	add	w1, w1, 1
	strh	w1, [x19, 458]
	bne	.L2887
	mov	w1, w20
	adrp	x0, .LC208
	add	x0, x0, :lo12:.LC208
	bl	printk
	b	.L2884
.L2887:
	cbz	w24, .L2889
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
	.size	flash_info_flush, .-flash_info_flush
	.align	2
	.global	flash_info_blk_init
	.type	flash_info_blk_init, %function
flash_info_blk_init:
	stp	x29, x30, [sp, -80]!
	adrp	x0, .LANCHOR2
	add	x29, sp, 0
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	tbz	x0, 12, .L2896
	add	x0, x22, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	mov	w3, 2048
	add	x2, x2, 736
	mov	w1, 50
	ldr	x4, [x0, 1048]
	adrp	x0, .LC209
	add	x0, x0, :lo12:.LC209
	bl	printk
.L2896:
	adrp	x20, .LANCHOR5
	mov	w25, 21321
	add	x21, x20, :lo12:.LANCHOR5
	add	x19, x22, :lo12:.LANCHOR0
	mov	w24, 4
	movk	w25, 0x5359, lsl 16
.L2900:
	mov	w23, 0
.L2899:
	ldrh	w1, [x19, 2]
	mov	w4, 4
	ldr	x2, [x19, 1048]
	mov	w0, 0
	ldr	x3, [x21, 448]
	madd	w1, w1, w24, w23
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2897
	ldr	x2, [x19, 1048]
	ldr	w0, [x2]
	cmp	w0, w25
	beq	.L2898
.L2897:
	add	w23, w23, 1
	cmp	w23, 4
	bne	.L2899
	add	w24, w24, 1
	cmp	w24, 16
	bne	.L2900
.L2926:
	mov	w0, -1
.L2895:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L2908:
	ldr	x0, [x19, 1048]
	ldr	w1, [x0]
	cmp	w1, w25
	bne	.L2909
	ldr	w21, [x0, 4]
.L2902:
	add	x19, x20, :lo12:.LANCHOR5
	add	x23, x22, :lo12:.LANCHOR0
	mov	w4, 4
	ldrh	w0, [x23, 2]
	ldrb	w1, [x19, 457]
	ldr	x3, [x19, 448]
	ldr	x2, [x23, 1048]
	mul	w1, w1, w0
	mov	w0, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2903
	ldr	x0, [x23, 1048]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L2903
	ldr	w1, [x0, 4]
	cmp	w21, w1
	bcs	.L2903
	ldrb	w1, [x0, 37]
	ldrb	w0, [x0, 36]
	strb	w1, [x19, 456]
	strb	w0, [x19, 457]
.L2903:
	add	x21, x20, :lo12:.LANCHOR5
	add	x19, x22, :lo12:.LANCHOR0
	mov	w4, 4
	mov	w0, 0
	mov	w26, 21321
	mov	x20, x19
	ldrb	w1, [x21, 456]
	mov	w23, 0
	ldr	x2, [x19, 1048]
	movk	w26, 0x5359, lsl 16
	ldr	x3, [x21, 448]
	bl	flash_get_last_written_page
	and	w25, w0, 65535
	add	w1, w25, 1
	ldrb	w24, [x21, 456]
	strh	w1, [x21, 458]
	ldrh	w1, [x19, 2]
	mul	w24, w24, w1
	add	w24, w24, w0, sxth
.L2904:
	sub	w0, w25, w23
	sxth	w19, w0
	tbz	w19, #31, .L2907
	cmn	w19, #1
	bne	.L2906
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22, 1048]
	ldr	w1, [x0]
	adrp	x0, .LC210
	add	x0, x0, :lo12:.LC210
	bl	printk
	b	.L2926
.L2907:
	ldr	x2, [x20, 1048]
	mov	w4, 4
	ldr	x3, [x21, 448]
	sub	w1, w24, w23
	mov	w0, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L2905
	ldr	x0, [x20, 1048]
	ldr	w0, [x0]
	cmp	w0, w26
	beq	.L2906
.L2905:
	add	w23, w23, 1
	b	.L2904
.L2906:
	cmp	w23, 1
	bls	.L2910
	bl	flash_info_flush
.L2910:
	mov	w0, 0
	b	.L2895
.L2898:
	ldrb	w1, [x2, 37]
	mov	w4, 4
	ldrb	w0, [x2, 36]
	strb	w1, [x21, 457]
	ldrh	w1, [x19, 2]
	ldr	x3, [x21, 448]
	strb	w0, [x21, 456]
	mul	w1, w1, w0
	mov	w0, 0
	bl	flash_read_page_en
	cmn	w0, #1
	bne	.L2908
.L2909:
	mov	w21, 0
	b	.L2902
	.size	flash_info_blk_init, .-flash_info_blk_init
	.align	2
	.global	nand_flash_init
	.type	nand_flash_init, %function
nand_flash_init:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR2
	mov	x19, x0
	stp	x21, x22, [sp, 32]
	ldr	w0, [x20, #:lo12:.LANCHOR2]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	tbz	x0, 12, .L2928
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 760
	mov	w1, 3450
	add	x0, x0, :lo12:.LC135
	bl	printk
.L2928:
	adrp	x21, .LANCHOR5
	add	x23, x21, :lo12:.LANCHOR5
	mov	x0, x19
	adrp	x19, .LANCHOR0
	add	x22, x19, :lo12:.LANCHOR0
	mov	w25, 44
	str	wzr, [x23, 332]
	bl	nandc_init
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 8
	add	x0, x0, 8
	str	x0, [x22, 1144]
	mov	w0, 1
	strb	w0, [x22, 1153]
	mov	w0, 3
	strb	w0, [x22, 1205]
	add	x0, x22, 1196
	add	x22, x22, 1216
	mov	w1, 0
	mov	x24, x22
	bl	ftl_memset
	add	x0, x23, 176
	mov	w23, 0
	mov	w2, 32
	mov	w1, 0
	bl	ftl_memset
.L2934:
	mov	x1, x22
	mov	w0, w23
	bl	flash_read_id
	cbnz	w23, .L2929
	ldrb	w0, [x24]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 253
	bls	.L2930
.L2932:
	mov	w22, -2
.L2927:
	mov	w0, w22
	ldr	x25, [sp, 64]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	ret
.L2930:
	ldrb	w0, [x24, 1]
	cmp	w0, 255
	beq	.L2932
.L2929:
	ldrb	w0, [x22]
	cmp	w0, 181
	bne	.L2933
	strb	w25, [x22]
.L2933:
	add	w23, w23, 1
	add	x22, x22, 8
	and	w23, w23, 255
	cmp	w23, 4
	bne	.L2934
	add	x7, x20, :lo12:.LANCHOR2
	add	x9, x19, :lo12:.LANCHOR0
	add	x7, x7, 441
	add	x9, x9, 1216
	mov	x8, 0
.L2937:
	ldrb	w2, [x7, -1]
	mov	w10, w8
	lsl	x24, x8, 5
	mov	x1, x9
	mov	x0, x7
	bl	flash_mem_cmp8
	cbnz	w0, .L2935
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x10, x10, 5, 32
	add	x0, x2, 440
	add	x1, x2, 2008
	add	x24, x0, x24
	add	x0, x0, x10
	ldrb	w3, [x0, 22]
	mov	x0, 0
.L2936:
	lsl	x4, x0, 5
	mov	w2, w0
	ldrb	w4, [x4, x1]
	cmp	w4, w3
	beq	.L2938
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L2936
	mov	w2, w0
.L2938:
	add	x22, x20, :lo12:.LANCHOR2
	ubfiz	x1, x2, 5, 32
	add	x0, x22, 2008
	add	x23, x19, :lo12:.LANCHOR0
	add	x1, x0, x1
	mov	w2, 32
	add	x0, x23, 1160
	add	x22, x22, 8
	bl	ftl_memcpy
	mov	w2, 32
	mov	x1, x24
	mov	x0, x22
	bl	ftl_memcpy
	ldrb	w0, [x23, 1028]
	cmp	w0, 8
	bhi	.L2939
	ldrb	w1, [x22, 20]
	cmp	w1, 60
	bls	.L2940
	mov	w1, 60
	strb	w1, [x22, 20]
.L2940:
	cmp	w0, 6
	beq	.L2932
.L2939:
	ldr	w0, [x20, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2941
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 760
	mov	w1, 3480
	add	x0, x0, :lo12:.LC135
	bl	printk
.L2941:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 1144]
	ldrh	w1, [x1, 10]
	cmp	w1, 1023
	bls	.L2942
	mov	w1, 2
	strb	w1, [x0, 1205]
.L2942:
	add	x23, x21, :lo12:.LANCHOR5
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 16384
	bl	ftl_malloc
	add	x24, x20, :lo12:.LANCHOR2
	str	x0, [x23, 352]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x23, 304]
	mov	w0, 2048
	bl	ftl_dma32_malloc
	str	x0, [x22, 1048]
	mov	w0, 64
	bl	ftl_dma32_malloc
	str	x0, [x23, 344]
	mov	w0, 64
	bl	ftl_dma32_malloc
	str	x0, [x23, 320]
	mov	w0, 64
	bl	ftl_dma32_malloc
	strb	wzr, [x23, 460]
	str	x0, [x23, 448]
	bl	flash_die_info_init
	ldrb	w0, [x24, 26]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x24, 28]
	bl	nandc_bch_sel
	str	xzr, [x23, 336]
	ldr	x3, [x22, 1144]
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldrh	w0, [x3, 16]
	ubfx	x1, x0, 8, 3
	strb	w1, [x22, 1248]
	ubfx	x1, x0, 3, 1
	strb	w1, [x23, 368]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22, 1250]
	ubfx	x1, x0, 12, 1
	strb	w1, [x2, 1336]
	ubfx	x1, x0, 13, 1
	strb	w1, [x2, 1322]
	ubfx	x1, x0, 11, 1
	strb	w1, [x22, 1212]
	ldrb	w1, [x3, 31]
	ubfx	x4, x1, 1, 1
	strb	w4, [x2, 1950]
	ubfx	x4, x1, 2, 1
	strb	w4, [x23, 461]
	ubfx	x4, x0, 14, 1
	lsr	w0, w0, 15
	strb	w0, [x2, 1323]
	ubfx	x2, x1, 3, 1
	ldrb	w0, [x3, 28]
	ubfx	x1, x1, 4, 1
	strb	w2, [x22, 1204]
	strb	w1, [x22, 1213]
	mov	w1, 60
	ldrb	w2, [x22, 1028]
	strb	w4, [x22, 1]
	strb	w0, [x19, #:lo12:.LANCHOR0]
	cmp	w2, 9
	strb	w1, [x22, 1152]
	bne	.L2943
	mov	w1, 70
	strb	w1, [x22, 1152]
.L2943:
	add	x1, x19, :lo12:.LANCHOR0
	strb	w0, [x1, 1154]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 8
	ldrb	w3, [x0, 31]
	tbz	x3, 0, .L2944
	ldrb	w0, [x0, 29]
	cbz	w0, .L2945
	mov	w0, 2
.L3007:
	strb	w0, [x1, 1154]
.L2944:
	cmp	w2, 8
	bne	.L2946
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 137
	ldrb	w1, [x0, 1216]
	cmp	w1, 44
	ccmp	w1, w2, 4, ne
	bne	.L2946
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w1, [x1, 36]
	cmp	w1, 3
	bne	.L2946
	strb	wzr, [x0, 1154]
.L2946:
	add	x1, x20, :lo12:.LANCHOR2
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x1, 8
	ldrb	w0, [x1, 19]
	ldrh	w1, [x1, 16]
	strb	w0, [x2, 1136]
	tbz	x1, 6, .L2948
	sub	w1, w0, #17
	and	w1, w1, 255
	cmp	w1, 2
	ccmp	w0, 21, 4, hi
	bne	.L2949
	add	x1, x21, :lo12:.LANCHOR5
	adrp	x2, micron_read_retrial
	add	x2, x2, :lo12:micron_read_retrial
	cmp	w0, 21
	str	x2, [x1, 336]
	beq	.L2950
	mov	w0, 15
.L3009:
	strb	w0, [x1, 328]
.L2948:
	ldr	w0, [x20, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2959
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 760
	mov	w1, 3573
	add	x0, x0, :lo12:.LC135
	bl	printk
.L2959:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1248]
	strb	wzr, [x0, 1192]
	tbz	x1, 0, .L2960
	ldrb	w0, [x0, 1216]
	cmp	w0, 155
	beq	.L2961
	mov	w0, 4
	bl	flash_set_interface_mode
	mov	w0, 4
	bl	nandc_set_if_mode
.L2961:
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
.L3012:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	mov	w22, w0
	cmn	w0, #1
	bne	.L2963
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 17
	ldr	x0, [x19, 1048]
	strb	wzr, [x19, 1208]
	strb	w1, [x0, 32]
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	b	.L2927
.L2935:
	add	x8, x8, 1
	add	x7, x7, 32
	cmp	x8, 49
	bne	.L2937
	b	.L2932
.L2945:
	mov	w0, 3
	b	.L3007
.L2950:
	mov	w0, 4
	b	.L3009
.L2949:
	sub	w1, w0, #65
	cmp	w0, 33
	and	w1, w1, 255
	ccmp	w1, 1, 0, ne
	bhi	.L2952
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	str	x1, [x0, 336]
	mov	w1, 4
	strb	w1, [x2, 1137]
.L3013:
	mov	w1, 7
.L3011:
	strb	w1, [x0, 328]
	b	.L2948
.L2952:
	sub	w3, w0, #34
	sub	w1, w0, #67
	and	w3, w3, 255
	and	w1, w1, 255
	cmp	w3, 1
	ccmp	w1, 1, 0, hi
	bhi	.L2953
	add	x2, x21, :lo12:.LANCHOR5
	adrp	x3, toshiba_read_retrial
	add	x3, x3, :lo12:toshiba_read_retrial
	cmp	w0, 35
	str	x3, [x2, 336]
	mov	w3, 68
	ccmp	w0, w3, 4, ne
	beq	.L2954
	mov	w0, 7
.L3008:
	strb	w0, [x2, 328]
	cmp	w1, 1
	add	x0, x19, :lo12:.LANCHOR0
	bhi	.L2956
	mov	w1, 4
.L3010:
	strb	w1, [x0, 1137]
	b	.L2948
.L2954:
	mov	w0, 17
	b	.L3008
.L2956:
	mov	w1, 5
	b	.L3010
.L2953:
	sub	w1, w0, #36
	and	w1, w1, 255
	cmp	w1, 1
	bhi	.L2957
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, toshiba_3d_read_retrial
	add	x1, x1, :lo12:toshiba_3d_read_retrial
	str	x1, [x0, 336]
	b	.L3013
.L2957:
	cmp	w0, 50
	bne	.L2958
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, samsung_read_retrial
	add	x1, x1, :lo12:samsung_read_retrial
	str	x1, [x0, 336]
	mov	w1, 25
	b	.L3011
.L2958:
	cmp	w0, 81
	bne	.L2948
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, ymtc_3d_read_retrial
	add	x1, x1, :lo12:ymtc_3d_read_retrial
	strb	wzr, [x2, 1251]
	str	x1, [x0, 336]
	mov	w1, 7
	strb	w1, [x0, 328]
	b	.L2948
.L2960:
	mov	w0, 4
	b	.L3012
.L2963:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0, 15]
	cmp	w0, 9
	bne	.L2964
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0, 1048]
	ldrb	w1, [x1, 20]
	cmp	w1, 1
	beq	.L2964
	strb	wzr, [x0, 1154]
.L2964:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1136]
	sub	w0, w0, #1
	and	w0, w0, 255
	cmp	w0, 7
	bhi	.L2965
	add	x0, x21, :lo12:.LANCHOR5
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0, 336]
.L2965:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19, 1248]
	tbz	x0, 2, .L2967
	ldr	x0, [x19, 1048]
	ldrb	w0, [x0, 19]
	cbz	w0, .L2967
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x19, 2]
	ldrb	w1, [x21, 456]
	mul	w1, w1, w0
	mov	w0, 0
	bl	flash_ddr_para_scan
	ldrb	w0, [x19, 1192]
	cbnz	w0, .L2967
	ldr	x0, [x19, 1048]
	strb	wzr, [x0, 19]
	bl	flash_info_flush
.L2967:
	ldr	w0, [x20, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L2969
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	adrp	x0, .LC135
	add	x2, x2, 760
	mov	w1, 3676
	add	x0, x0, :lo12:.LC135
	bl	printk
.L2969:
	bl	nand_flash_print_info
	mov	w22, 0
	b	.L2927
	.size	nand_flash_init, .-nand_flash_init
	.align	2
	.global	ftl_sysblk_dump
	.type	ftl_sysblk_dump, %function
ftl_sysblk_dump:
	sub	sp, sp, #144
	stp	x29, x30, [sp, 48]
	add	x29, sp, 48
	stp	x19, x20, [sp, 64]
	mov	w20, 0
	stp	x25, x26, [sp, 112]
	adrp	x26, .LANCHOR3
	stp	x27, x28, [sp, 128]
	add	x26, x26, :lo12:.LANCHOR3
	stp	x21, x22, [sp, 80]
	adrp	x22, .LC195
	stp	x23, x24, [sp, 96]
	and	w24, w0, 65535
	mov	w0, 1
	bl	buf_alloc
	ldr	x27, [x0, 8]
	mov	x25, x0
	add	x22, x22, :lo12:.LC195
	mov	w19, 0
	mov	w28, 1
.L3015:
	ldrh	w0, [x26, 1376]
	cmp	w0, w19
	bhi	.L3017
	add	x1, x27, 704
	mov	w3, 32
	mov	w2, 4
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	bl	rknand_print_hex
	mov	x0, x25
	bl	zbuf_free
	cbz	w20, .L3018
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 776
	mov	w2, 1619
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3018:
	mov	w0, w20
	ldp	x19, x20, [sp, 64]
	ldp	x21, x22, [sp, 80]
	ldp	x23, x24, [sp, 96]
	ldp	x25, x26, [sp, 112]
	ldp	x27, x28, [sp, 128]
	ldp	x29, x30, [sp, 48]
	add	sp, sp, 144
	ret
.L3017:
	ldrh	w21, [x26, 1410]
	ldrb	w3, [x26, 1946]
	ldr	x1, [x25, 8]
	ldr	x2, [x25, 24]
	madd	w21, w21, w24, w19
	mov	w0, w21
	bl	ftl_read_ppa_page
	mov	w23, w0
	ldr	x1, [x25, 24]
	mov	w4, w0
	ldr	x0, [x25, 8]
	mov	w3, w21
	ldr	w2, [x1, 12]
	str	w2, [sp, 32]
	ldr	w2, [x1, 8]
	str	w2, [sp, 24]
	ldr	w2, [x1, 4]
	str	w2, [sp, 16]
	mov	w2, w19
	add	w19, w19, 1
	ldr	w1, [x1]
	and	w19, w19, 65535
	str	w1, [sp, 8]
	ldr	w1, [x0, 12]
	str	w1, [sp]
	mov	w1, w24
	ldp	w5, w6, [x0]
	ldr	w7, [x0, 8]
	mov	x0, x22
	bl	printk
	cmp	w23, 512
	ccmn	w23, #1, 4, ne
	csel	w20, w20, w28, ne
	b	.L3015
	.size	ftl_sysblk_dump, .-ftl_sysblk_dump
	.align	2
	.global	ftl_open_sblk_recovery
	.type	ftl_open_sblk_recovery, %function
ftl_open_sblk_recovery:
	stp	x29, x30, [sp, -352]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	str	x1, [x29, 112]
	tbz	x0, 12, .L3026
	ldrh	w1, [x19, 2]
	adrp	x0, .LC212
	add	x0, x0, :lo12:.LC212
	bl	printk
.L3026:
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3027
	ldrb	w1, [x19, 5]
	adrp	x0, .LC213
	add	x0, x0, :lo12:.LC213
	bl	printk
.L3027:
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3028
	ldrh	w1, [x19]
	adrp	x0, .LC214
	add	x0, x0, :lo12:.LC214
	bl	printk
.L3028:
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3029
	ldrh	w2, [x19, 18]
	adrp	x0, .LC215
	ldrh	w1, [x19, 16]
	add	x0, x0, :lo12:.LC215
	bl	printk
.L3029:
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3030
	ldrb	w1, [x19, 9]
	adrp	x0, .LC216
	add	x0, x0, :lo12:.LC216
	bl	printk
.L3030:
	ldrh	w0, [x19, 10]
	adrp	x24, .LANCHOR0
	strh	w0, [x19, 14]
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldrh	w0, [x0, 1096]
	cmp	w1, w0
	bcs	.L3025
	mov	w0, 1
	bl	buf_alloc
	adrp	x20, .LANCHOR3
	mov	x27, x0
	add	x0, x20, :lo12:.LANCHOR3
	add	x25, x29, 160
	mov	w2, 64
	add	x28, x29, 288
	ldr	x1, [x27, 8]
	ldrb	w0, [x0, 1946]
	sub	w0, w0, #2
	sbfiz	x0, x0, 9, 32
	add	x0, x1, x0
	mov	w1, 255
	str	x0, [x29, 144]
	mov	x0, x25
	bl	ftl_memset
	mov	w2, 64
	mov	w1, 255
	add	x0, x29, 224
	bl	ftl_memset
	mov	w2, 64
	mov	w1, 255
	mov	x0, x28
	bl	ftl_memset
	ldrb	w22, [x19, 5]
	mov	w0, 2
	ldrh	w23, [x19, 2]
	str	w0, [x29, 152]
	adrp	x0, .LANCHOR5
	str	wzr, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR5
	str	x0, [x29, 104]
.L3032:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1376]
	cmp	w0, w23
	bhi	.L3049
.L3035:
	add	x2, x20, :lo12:.LANCHOR3
	ldrh	w0, [x19, 10]
	ldrh	w1, [x19, 6]
	strh	w23, [x19, 2]
	add	w1, w1, w0
	ldrh	w2, [x2, 1376]
	ldrb	w0, [x19, 9]
	strb	w22, [x19, 5]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L3050
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 792
	mov	w2, 1802
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3050:
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w3, [x19, 10]
	mov	w1, 0
	ldr	x4, [x0, 1928]
	mov	w0, 0
.L3051:
	cmp	w1, w3
	bcc	.L3053
	add	x1, x20, :lo12:.LANCHOR3
	ldrb	w22, [x19, 9]
	ldrh	w1, [x1, 1376]
	madd	w22, w22, w1, w0
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	sub	w22, w22, w3
	and	w22, w22, 65535
	tbz	x0, 12, .L3054
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldr	x0, [x0, 1120]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x0, x2]
	adrp	x0, .LC219
	mov	w2, w22
	add	x0, x0, :lo12:.LC219
	bl	printk
.L3054:
	add	x0, x24, :lo12:.LANCHOR0
	ldrh	w1, [x19]
	ldr	x0, [x0, 1120]
	strh	w22, [x0, x1, lsl 1]
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 14, .L3055
	ldp	w1, w2, [x29, 160]
	adrp	x0, .LC220
	ldp	w3, w4, [x29, 168]
	add	x0, x0, :lo12:.LC220
	bl	printk
.L3055:
	add	x0, x20, :lo12:.LANCHOR3
	mov	w1, 0
	mov	x23, 0
	ldrb	w2, [x0, 1946]
	ldr	x0, [x27, 8]
	lsl	w2, w2, 9
	bl	ftl_memset
	adrp	x0, .LC221
	add	x0, x0, :lo12:.LC221
	str	x0, [x29, 128]
	adrp	x0, .LC222
	add	x0, x0, :lo12:.LC222
	str	x0, [x29, 112]
.L3056:
	add	x1, x20, :lo12:.LANCHOR3
	ldr	w2, [x29, 152]
	str	w23, [x29, 140]
	ldrb	w0, [x1, 1321]
	mul	w0, w0, w2
	cmp	w23, w0
	bcc	.L3068
	adrp	x22, .LC223
	adrp	x23, .LC222
	mov	x20, x1
	add	x22, x22, :lo12:.LC223
	add	x23, x23, :lo12:.LC222
	mov	x24, 0
.L3069:
	ldrb	w0, [x20, 1321]
	ldr	w1, [x29, 152]
	mul	w0, w0, w1
	cmp	w0, w24
	bhi	.L3075
	mov	x0, x27
	bl	zbuf_free
	ldrh	w1, [x19, 12]
	ldrh	w0, [x20, 1376]
	ldrb	w2, [x19, 9]
	madd	w0, w0, w2, w1
	mov	x1, -4
	add	x0, x1, w0, sxtw 2
	ldr	x1, [x20, 1928]
	ldr	w0, [x1, x0]
	cmn	w0, #1
	beq	.L3076
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 792
	mov	w2, 1917
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3076:
	ldrh	w0, [x19, 6]
	cmp	w0, 1
	bne	.L3025
	mov	x0, x19
	bl	ftl_write_last_log_page
.L3025:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 352
	ret
.L3049:
	ldrb	w22, [x19, 5]
.L3033:
	ldrb	w0, [x19, 9]
	cmp	w0, w22
	bhi	.L3048
	add	w23, w23, 1
	strb	wzr, [x19, 5]
	and	w23, w23, 65535
	b	.L3032
.L3048:
	add	x0, x19, w22, sxtw 1
	ldrh	w0, [x0, 16]
	str	w0, [x29, 140]
	mov	w0, 65535
	ldr	w1, [x29, 140]
	cmp	w1, w0
	beq	.L3034
	add	x0, x20, :lo12:.LANCHOR3
	ldr	w2, [x29, 140]
	ldrh	w1, [x0, 1410]
	ldrb	w3, [x0, 1946]
	madd	w1, w1, w2, w23
	ldr	x2, [x27, 24]
	str	w1, [x29, 128]
	ldr	x1, [x27, 8]
	ldr	w0, [x29, 128]
	bl	ftl_read_ppa_page
	mov	w26, w0
	cmp	w0, 512
	beq	.L3035
	cmn	w0, #1
	beq	.L3036
	ldr	x0, [x27, 24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L3036
	ldr	w0, [x0, 4]
	cmn	w0, #1
	bne	.L3036
	ldr	x0, [x27, 8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L3035
.L3036:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	mov	w1, 1
	strb	w1, [x0, 401]
	ldrb	w0, [x19, 9]
	ldrh	w1, [x19, 10]
	madd	w0, w0, w23, w22
	cmp	w1, w0
	beq	.L3037
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 792
	mov	w2, 1694
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3037:
	ldrh	w0, [x19, 10]
	ldrh	w1, [x19, 6]
	ldrb	w2, [x19, 9]
	add	w1, w1, w0
	add	x0, x20, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1376]
	mul	w0, w0, w2
	cmp	w1, w0
	beq	.L3038
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 792
	mov	w2, 1695
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3038:
	add	x0, x20, :lo12:.LANCHOR3
	str	x0, [x29, 152]
	ldrb	w0, [x0, 1946]
	cmp	w0, 8
	bls	.L3039
	ldr	x0, [x27, 24]
	mov	w2, 15555
	movk	w2, 0xf55f, lsl 16
	ldr	w1, [x0]
	cmp	w1, w2
	beq	.L3039
	cmn	w26, #1
	beq	.L3041
	ldr	w1, [x0, 4]
	cmn	w1, #1
	bne	.L3042
.L3045:
	ldr	x1, [x27, 24]
	ldr	w0, [x1, 4]
	cmn	w0, #1
	bne	.L3043
.L3041:
	ldrh	w0, [x19, 6]
	sub	w0, w0, #1
	strh	w0, [x19, 6]
	ldrh	w0, [x19, 10]
	add	w0, w0, 1
	strh	w0, [x19, 10]
	mov	w0, 4
	str	w0, [x29, 152]
	mov	w0, 1
	str	w0, [x29, 120]
.L3034:
	add	w22, w22, 1
	and	w22, w22, 65535
	b	.L3033
.L3042:
	ldr	w1, [x0, 16]
	mov	w2, 21320
	movk	w2, 0x4841, lsl 16
	cmp	w1, w2
	bne	.L3045
	ldr	w2, [x0, 20]
	mov	w1, 1024
	ldr	x0, [x29, 144]
	str	w2, [x29, 152]
	bl	js_hash
	ldr	w2, [x29, 152]
	cmp	w2, w0
	beq	.L3045
	ldr	x0, [x29, 144]
	mov	w1, 1024
	bl	js_hash
	mov	w5, w0
	ldr	w3, [x29, 128]
	mov	w4, w26
	ldr	w1, [x29, 140]
	mov	w2, w23
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	bl	printk
	ldr	x1, [x29, 144]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	bl	rknand_print_hex
	add	x0, x20, :lo12:.LANCHOR3
	ldr	x1, [x27, 24]
	mov	w2, 4
	ldrb	w0, [x0, 1946]
	lsr	w3, w0, 1
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	bl	rknand_print_hex
	b	.L3041
.L3039:
	cmn	w26, #1
	bne	.L3045
	b	.L3041
.L3043:
	ldr	w1, [x1]
	mov	w2, 15555
	movk	w2, 0xf55f, lsl 16
	cmp	w1, w2
	beq	.L3041
	bl	lpa_hash_get_ppa
	ldr	x1, [x29, 112]
	cbz	x1, .L3046
	ldr	x3, [x27, 24]
	ldr	w1, [x3, 8]
	cmp	w0, w1
	beq	.L3046
	cmn	w0, #1
	beq	.L3046
	add	x7, x24, :lo12:.LANCHOR0
	add	x4, x20, :lo12:.LANCHOR3
	mov	w5, 24
	mov	w26, 1
	ldrb	w1, [x7, 1205]
	ldrh	w8, [x4, 1304]
	sub	w1, w5, w1
	sub	w1, w1, w8
	lsr	w2, w0, w8
	lsl	w1, w26, w1
	sub	w1, w1, #1
	and	w1, w1, w2
	ldrb	w2, [x4, 1306]
	udiv	w1, w1, w2
	ldr	x2, [x29, 112]
	ldrh	w2, [x2]
	cmp	w2, w1, uxth
	bne	.L3046
	ldr	x1, [x29, 104]
	ldr	w8, [x3]
	ldrb	w3, [x4, 1946]
	stp	w5, w8, [x29, 136]
	ldr	x2, [x1, 376]
	ldr	x1, [x27, 8]
	str	x7, [x29, 120]
	str	x4, [x29, 152]
	bl	ftl_read_ppa_page
	ldr	x0, [x29, 104]
	ldr	w8, [x29, 140]
	ldr	x0, [x0, 376]
	ldr	w0, [x0]
	cmp	w8, w0
	bhi	.L3046
	ldr	x0, [x27, 24]
	ldr	w3, [x0, 8]
	cmn	w3, #1
	beq	.L3041
	ldr	x7, [x29, 120]
	ldr	x4, [x29, 152]
	ldr	w5, [x29, 136]
	ldrb	w2, [x7, 1205]
	ldrh	w1, [x4, 1304]
	sub	w5, w5, w2
	sub	w5, w5, w1
	lsr	w0, w3, w1
	lsl	w26, w26, w5
	sub	w26, w26, #1
	and	w26, w26, w0
	ldrb	w0, [x4, 1306]
	udiv	w0, w26, w0
	bl	ftl_vpn_decrement
	b	.L3041
.L3053:
	ldrh	w2, [x19, 12]
	add	w2, w2, w1
	ldr	w2, [x4, x2, lsl 2]
	cmn	w2, #1
	beq	.L3052
	add	w22, w0, 1
	and	w0, w22, 65535
.L3052:
	add	w1, w1, 1
	b	.L3051
.L3068:
	ldr	w0, [x25, x23, lsl 2]
	cmn	w0, #1
	bne	.L3057
.L3061:
	add	x1, x24, :lo12:.LANCHOR0
	ldr	x0, [x27, 24]
	mov	w22, -1
	mov	x2, 0
	ldr	x1, [x1, 3384]
	ldr	w1, [x1, 8]
	str	w1, [x0]
	ldr	x0, [x27, 24]
	str	w22, [x0, 4]
	ldr	x0, [x27, 24]
	str	w22, [x0, 8]
	ldr	x0, [x27, 24]
	str	wzr, [x0, 12]
	ldr	x0, [x27, 24]
	str	wzr, [x0, 16]
	ldr	x0, [x27, 8]
	str	wzr, [x0]
	mov	w0, 2
	ldr	x1, [x27, 24]
	add	x1, x1, 16
	bl	ftl_debug_info_fill
.L3058:
	ldr	w0, [x29, 120]
	cbz	w0, .L3063
	ldrh	w0, [x19, 6]
	cmp	w0, 1
	bls	.L3063
	add	x0, x20, :lo12:.LANCHOR3
	str	x0, [x29, 144]
.L3122:
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w26, w0
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 14, .L3065
	ldrh	w0, [x19, 12]
	mov	w1, w26
	ldrh	w3, [x19, 10]
	add	w3, w3, w0
	ldr	x0, [x27, 24]
	sub	w3, w3, #1
	ldr	w2, [x0, 4]
	ldr	x0, [x29, 112]
	bl	printk
.L3065:
	ldr	x0, [x29, 144]
	ldr	w2, [x29, 152]
	ldrh	w1, [x19, 6]
	ldrb	w0, [x0, 1321]
	mul	w0, w0, w2
	ldr	w2, [x29, 140]
	add	w0, w0, 1
	sub	w0, w0, w2
	cmp	w1, w0
	bls	.L3063
	ldr	x0, [x29, 144]
	ldr	x1, [x27, 8]
	ldr	x2, [x27, 24]
	ldrb	w3, [x0, 1946]
	mov	w0, w26
	bl	ftl_prog_ppa_page
	mov	w1, w0
	ldrh	w0, [x19]
	str	w1, [x29, 104]
	bl	ftl_vpn_decrement
	ldr	w1, [x29, 104]
	cmn	w22, #1
	ccmn	w1, #1, 4, ne
	beq	.L3066
	add	x0, x29, 224
	ldrh	w1, [x19, 12]
	str	w26, [x0, x23, lsl 2]
	ldrh	w0, [x19, 10]
	add	w0, w0, w1
	sub	w0, w0, #1
	str	w0, [x28, x23, lsl 2]
.L3063:
	add	x23, x23, 1
	b	.L3056
.L3057:
	ldrb	w3, [x1, 1946]
	ldr	x2, [x27, 24]
	ldr	x1, [x27, 8]
	bl	ftl_read_ppa_page
	mov	w22, w0
	ldr	x0, [x27, 24]
	ldr	w0, [x0, 4]
	bl	lpa_hash_get_ppa
	mov	w26, w0
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 14, .L3059
	ldr	x0, [x27, 24]
	mov	w1, w26
	ldr	w2, [x25, x23, lsl 2]
	ldr	w3, [x0, 4]
	ldr	x0, [x29, 128]
	bl	printk
.L3059:
	ldr	w0, [x25, x23, lsl 2]
	mov	w1, 1
	mov	x2, 0
	cmp	w26, w0
	ldr	x0, [x27, 24]
	csinv	w22, w22, wzr, eq
	str	w26, [x0, 8]
	ldr	x0, [x27, 24]
	str	w1, [x0, 12]
	ldr	x0, [x27, 24]
	str	wzr, [x0, 16]
	mov	w0, 2
	ldr	x1, [x27, 24]
	add	x1, x1, 16
	bl	ftl_debug_info_fill
	cmn	w22, #1
	bne	.L3058
	b	.L3061
.L3066:
	ldrh	w0, [x19, 6]
	cmp	w0, 1
	bls	.L3063
	cmn	w22, #1
	bne	.L3122
	b	.L3063
.L3075:
	add	x0, x29, 224
	ldr	w0, [x0, x24, lsl 2]
	cmn	w0, #1
	beq	.L3071
	ldrb	w3, [x20, 1946]
	ldr	w0, [x25, x24, lsl 2]
	ldr	x1, [x27, 8]
	ldr	x2, [x27, 24]
	bl	ftl_read_ppa_page
	cmp	w0, 256
	ccmn	w0, #1, 4, ne
	bne	.L3071
	add	x0, x29, 224
	ldrb	w3, [x20, 1946]
	ldr	x1, [x27, 8]
	ldr	x2, [x27, 24]
	ldr	w0, [x0, x24, lsl 2]
	bl	ftl_read_ppa_page
	mov	w26, w0
	ldr	w0, [x21, #:lo12:.LANCHOR2]
	tbz	x0, 14, .L3073
	ldr	x0, [x27, 24]
	mov	w1, w26
	ldr	w3, [x25, x24, lsl 2]
	ldr	w2, [x0, 8]
	mov	x0, x23
	bl	printk
.L3073:
	cmn	w26, #1
	beq	.L3071
	ldr	x0, [x27, 24]
	ldr	w2, [x25, x24, lsl 2]
	ldr	w1, [x0, 8]
	cmp	w2, w1
	bne	.L3071
	ldr	w1, [x21, #:lo12:.LANCHOR2]
	tbz	x1, 14, .L3074
	ldr	w1, [x0, 4]
	mov	x0, x22
	ldr	w3, [x28, x24, lsl 2]
	bl	printk
.L3074:
	ldr	x0, [x27, 24]
	lsl	x1, x24, 2
	ldrh	w2, [x28, x1]
	ldr	w1, [x0, 8]
	ldr	w0, [x0, 4]
	bl	lpa_hash_update_ppa
.L3071:
	add	x24, x24, 1
	b	.L3069
.L3046:
	ldr	x2, [x27, 24]
	add	x1, x24, :lo12:.LANCHOR0
	ldr	w0, [x1, 3364]
	ldr	w3, [x2, 4]
	cmp	w3, w0
	bcs	.L3041
	add	x4, x20, :lo12:.LANCHOR3
	ldrb	w0, [x19, 9]
	ldrh	w3, [x19, 10]
	ldrh	w4, [x4, 1376]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	bge	.L3041
	ldr	x0, [x1, 3384]
	ldr	w2, [x2]
	ldr	w1, [x0, 8]
	cmp	w2, w1
	bls	.L3047
	str	w2, [x0, 8]
.L3047:
	ldr	x0, [x27, 24]
	ldrh	w1, [x19, 12]
	ldrh	w2, [x19, 10]
	add	w2, w2, w1
	ldr	w1, [x0, 8]
	ldr	w0, [x0, 4]
	bl	lpa_hash_update_ppa
	ldr	w0, [x29, 164]
	str	w0, [x29, 160]
	ldr	w0, [x29, 168]
	str	w0, [x29, 164]
	ldr	w0, [x29, 172]
	str	w0, [x29, 168]
	ldr	w0, [x29, 128]
	str	w0, [x29, 172]
	b	.L3041
	.size	ftl_open_sblk_recovery, .-ftl_open_sblk_recovery
	.align	2
	.global	dump_ftl_info
	.type	dump_ftl_info, %function
dump_ftl_info:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	ldrb	w1, [x19, 3353]
	bl	printk
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w3, [x0, 386]
	ldrb	w2, [x0, 385]
	ldrb	w1, [x0, 384]
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	bl	printk
	ldr	x0, [x19, 3384]
	ldrh	w2, [x0, 140]
	ldrh	w1, [x0, 130]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	bl	printk
	ldr	x1, [x19, 1128]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	ldrh	w5, [x1, 26]
	ldrh	w4, [x1, 22]
	ldrb	w3, [x1, 21]
	ldrh	w2, [x1, 18]
	ldrh	w1, [x1, 16]
	bl	printk
	ldr	x1, [x19, 1128]
	adrp	x0, .LC228
	add	x0, x0, :lo12:.LC228
	ldrh	w5, [x1, 58]
	ldrh	w4, [x1, 54]
	ldrb	w3, [x1, 53]
	ldrh	w2, [x1, 50]
	ldrh	w1, [x1, 48]
	bl	printk
	ldr	x1, [x19, 1128]
	adrp	x0, .LC229
	add	x0, x0, :lo12:.LC229
	ldrh	w5, [x1, 90]
	ldrh	w4, [x1, 86]
	ldrb	w3, [x1, 85]
	ldrh	w2, [x1, 82]
	ldrh	w1, [x1, 80]
	bl	printk
	ldrb	w0, [x20, 1321]
	mov	w2, 4
	ldrh	w3, [x20, 1376]
	ldr	x1, [x20, 1928]
	mul	w3, w3, w0
	adrp	x0, .LC230
	add	x0, x0, :lo12:.LC230
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldrh	w3, [x19, 1096]
	mov	w2, 2
	ldr	x1, [x19, 1120]
	adrp	x0, .LC231
	add	x0, x0, :lo12:.LC231
	bl	rknand_print_hex
	ldr	x1, [x19, 3384]
	mov	w2, 4
	adrp	x0, .LC211
	add	x0, x0, :lo12:.LC211
	add	x1, x1, 704
	ldrh	w3, [x1, -6]
	bl	rknand_print_hex
	ldrh	w3, [x19, 1096]
	mov	w2, 4
	ldr	x1, [x19, 1104]
	adrp	x0, .LC232
	add	x0, x0, :lo12:.LC232
	bl	rknand_print_hex
	add	x1, x20, 1416
	mov	w3, 256
	mov	w2, 2
	adrp	x0, .LC233
	add	x0, x0, :lo12:.LC233
	bl	rknand_print_hex
	ldrb	w0, [x20, 1321]
	mov	w2, 2
	ldrh	w3, [x20, 1376]
	ldr	x1, [x20, 1936]
	mul	w3, w3, w0
	adrp	x0, .LC234
	add	x0, x0, :lo12:.LC234
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	dump_ftl_info, .-dump_ftl_info
	.align	2
	.global	pm_ppa_update_check
	.type	pm_ppa_update_check, %function
pm_ppa_update_check:
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	adrp	x6, .LANCHOR3
	add	x6, x6, :lo12:.LANCHOR3
	mov	w5, 24
	ldrb	w4, [x3, 1205]
	ldrh	w7, [x6, 1304]
	sub	w4, w5, w4
	ldr	x3, [x3, 1104]
	sub	w5, w4, w7
	mov	w4, 1
	lsr	w7, w2, w7
	lsl	w4, w4, w5
	ldrb	w5, [x6, 1306]
	sub	w4, w4, #1
	and	w4, w4, w7
	udiv	w4, w4, w5
	add	x4, x3, w4, uxth 2
	ldrb	w3, [x4, 2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 1
	ccmp	w3, 7, 4, ne
	bne	.L3151
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LC235
	add	x0, x0, :lo12:.LC235
	add	x29, sp, 0
	bl	printk
	bl	dump_ftl_info
	mov	w0, -1
	ldp	x29, x30, [sp], 16
	ret
.L3151:
	mov	w0, 0
	ret
	.size	pm_ppa_update_check, .-pm_ppa_update_check
	.align	2
	.type	load_l2p_region, %function
load_l2p_region:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 65535
	stp	x21, x22, [sp, 32]
	and	x21, x1, 65535
	stp	x23, x24, [sp, 48]
	cmp	w21, 31
	stp	x25, x26, [sp, 64]
	bls	.L3157
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 816
	mov	w2, 32
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3157:
	adrp	x23, .LANCHOR0
	add	x22, x23, :lo12:.LANCHOR0
	adrp	x19, .LANCHOR5
	ldr	x0, [x22, 3384]
	ldrh	w2, [x0, 698]
	cmp	w2, w20
	bcs	.L3158
	mov	w1, w20
	adrp	x0, .LC236
	add	x19, x19, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC236
	bl	printk
	mov	x0, 0
	mov	w1, 255
	ldrh	w2, [x19, 462]
	ldr	x0, [x0, 8]
	bl	ftl_memset
	ldr	x0, [x22, 3384]
	ldrh	w0, [x0, 698]
	cmp	w0, w20
	bcc	.L3159
.L3169:
	mov	w0, 0
.L3156:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L3159:
	mov	w2, 37
.L3170:
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 816
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3169
.L3158:
	add	x0, x0, w20, sxtw 2
	adrp	x22, .LANCHOR3
	lsl	x1, x21, 4
	ldr	w24, [x0, 704]
	add	x0, x22, :lo12:.LANCHOR3
	add	x2, x0, 1960
	add	x26, x2, x1
	strh	w20, [x2, x1]
	strh	wzr, [x26, 2]
	cbnz	w24, .L3161
	add	x19, x19, :lo12:.LANCHOR5
	mov	w1, w20
	mov	w2, 0
	adrp	x0, .LC237
	add	x0, x0, :lo12:.LC237
	bl	printk
	ldrh	w2, [x19, 462]
	mov	w1, 255
	ldr	x0, [x26, 8]
	bl	ftl_memset
	b	.L3169
.L3161:
	add	x25, x19, :lo12:.LANCHOR5
	ldrb	w3, [x0, 1946]
	ldr	x1, [x26, 8]
	mov	w0, w24
	ldr	x2, [x25, 432]
	bl	ftl_read_ppa_page
	ldr	x1, [x25, 432]
	ldr	w2, [x1]
	cmp	w2, w20
	bne	.L3162
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L3162
.L3166:
	add	x19, x19, :lo12:.LANCHOR5
	ldr	x0, [x19, 432]
	ldr	w0, [x0]
	cmp	w20, w0
	beq	.L3169
	mov	w2, 73
	b	.L3170
.L3162:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w4, w24
	mov	w3, w0
	mov	w1, w20
	adrp	x0, .LC238
	add	x0, x0, :lo12:.LC238
	bl	printk
	add	x22, x22, :lo12:.LANCHOR3
	ldr	x1, [x23, 3384]
	mov	w2, 4
	adrp	x0, .LC239
	add	x0, x0, :lo12:.LC239
	add	x1, x1, 704
	add	x23, x19, :lo12:.LANCHOR5
	ldrh	w3, [x1, -6]
	bl	rknand_print_hex
	add	x0, x22, 1960
	ldrb	w3, [x22, 1946]
	add	x21, x0, x21, lsl 4
	mov	w2, 4
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	lsl	w3, w3, 7
	ldr	x1, [x21, 8]
	bl	rknand_print_hex
	ldr	x1, [x23, 432]
	mov	w3, 16
	mov	w2, 4
	adrp	x0, .LC240
	add	x0, x0, :lo12:.LC240
	bl	rknand_print_hex
	ldrb	w3, [x22, 1946]
	mov	w0, w24
	ldr	x1, [x21, 8]
	ldr	x2, [x23, 432]
	bl	ftl_read_ppa_page
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3165
	ldrh	w2, [x23, 462]
	mov	w1, 255
	ldr	x0, [x21, 8]
	bl	ftl_memset
.L3167:
	mov	w0, -1
	b	.L3156
.L3165:
	ldr	x0, [x23, 432]
	ldr	w0, [x0]
	cmp	w20, w0
	beq	.L3166
	b	.L3167
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	pm_gc
	.type	pm_gc, %function
pm_gc:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	adrp	x20, .LANCHOR5
	add	x1, x20, :lo12:.LANCHOR5
	ldr	x0, [x0, 3384]
	ldrh	w2, [x0, 688]
	ldrh	w0, [x1, 220]
	sub	w0, w0, #1
	cmp	w2, w0
	bge	.L3172
	ldr	w0, [x1, 212]
	cbz	w0, .L3173
.L3172:
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR5
	ldr	x2, [x1, 3384]
	ldrh	w1, [x3, 220]
	sub	w1, w1, #1
	ldrh	w4, [x2, 688]
	cmp	w4, w1
	bge	.L3174
	ldr	w1, [x3, 212]
	cbz	w1, .L3173
.L3174:
	add	x20, x20, :lo12:.LANCHOR5
	add	x0, x2, w0, uxth 1
	str	wzr, [x20, 212]
	mov	w20, 65535
	ldrh	w22, [x0, 416]
	cmp	w22, w20
	bne	.L3176
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 832
	mov	w2, 182
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	bl	pm_free_sblk
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1, 3384]
	add	x0, x1, w0, uxth 1
	ldrh	w22, [x0, 416]
	cmp	w22, w20
	beq	.L3173
.L3176:
	bl	pm_select_ram_region
	adrp	x24, .LANCHOR3
	and	x21, x0, 65535
	add	x0, x24, :lo12:.LANCHOR3
	lsl	x1, x21, 4
	add	x0, x0, 1960
	add	x20, x0, x1
	mov	x23, x21
	ldrh	w0, [x0, x1]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3177
	ldr	x1, [x20, 8]
	cbz	x1, .L3177
	ldrsh	w2, [x20, 2]
	tbz	w2, #31, .L3177
	bl	pm_write_page
	ldrh	w0, [x20, 2]
	and	w0, w0, 32767
	strh	w0, [x20, 2]
.L3177:
	add	x24, x24, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x24, 1960
	mov	w20, 0
	add	x21, x0, x21, lsl 4
.L3178:
	ldr	x2, [x19, 3384]
	ldrh	w0, [x2, 698]
	cmp	w0, w20
	bhi	.L3181
	bl	pm_free_sblk
.L3173:
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.L3181:
	ldrb	w1, [x19, 1205]
	mov	w0, 24
	ldrh	w3, [x24, 1304]
	add	x2, x2, w20, sxtw 2
	sub	w0, w0, w1
	sub	w1, w0, w3
	mov	w0, 1
	lsl	w0, w0, w1
	ldr	w1, [x2, 704]
	sub	w0, w0, #1
	lsr	w1, w1, w3
	and	w0, w0, w1
	ldrb	w1, [x24, 1306]
	udiv	w0, w0, w1
	cmp	w22, w0, uxth
	bne	.L3179
	mov	w1, w23
	mov	w0, w20
	bl	load_l2p_region
	cbnz	w0, .L3180
	ldr	x1, [x21, 8]
	mov	w0, w20
	bl	pm_write_page
.L3180:
	mov	w0, -1
	strh	w0, [x21]
.L3179:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L3178
	.size	pm_gc, .-pm_gc
	.align	2
	.global	pm_flush_id
	.type	pm_flush_id, %function
pm_flush_id:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ubfiz	x0, x0, 4, 16
	add	x2, x2, 1960
	add	x29, sp, 0
	str	x19, [sp, 16]
	add	x19, x2, x0
	ldrh	w0, [x2, x0]
	ldr	x1, [x19, 8]
	bl	pm_write_page
	ldrh	w0, [x19, 2]
	and	w0, w0, 32767
	strh	w0, [x19, 2]
	adrp	x19, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR5
	ldr	w0, [x19, 424]
	cbz	w0, .L3190
	bl	pm_gc
	str	wzr, [x19, 424]
.L3190:
	mov	w0, 0
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
	.size	pm_flush_id, .-pm_flush_id
	.align	2
	.global	pm_flush
	.type	pm_flush, %function
pm_flush:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR3
	add	x19, x19, :lo12:.LANCHOR3
	mov	w20, 0
	add	x19, x19, 1962
.L3197:
	ldrsh	w0, [x19]
	tbz	w0, #31, .L3196
	mov	w0, w20
	bl	pm_flush_id
.L3196:
	add	w20, w20, 1
	add	x19, x19, 16
	and	w20, w20, 65535
	cmp	w20, 32
	bne	.L3197
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
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
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR5
	add	x19, x20, :lo12:.LANCHOR5
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	mov	w24, w0
	stp	x25, x26, [sp, 64]
	mov	w0, 1
	stp	x27, x28, [sp, 80]
	mov	w25, -1
	strb	w0, [x19, 440]
	mov	w0, 64
	str	wzr, [x19, 212]
	str	wzr, [x19, 424]
	bl	ftl_dma32_malloc
	str	x0, [x19, 432]
	adrp	x19, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR3
	add	x21, x0, 1960
	add	x23, x0, 2472
	mov	x22, x0
.L3206:
	strh	w25, [x21]
	strh	wzr, [x21, 2]
	cbz	w24, .L3205
	ldrb	w0, [x22, 1946]
	lsl	w0, w0, 9
	bl	ftl_dma32_malloc
	str	x0, [x21, 8]
.L3205:
	add	x21, x21, 16
	cmp	x21, x23
	bne	.L3206
	add	x0, x19, :lo12:.LANCHOR3
	adrp	x21, .LANCHOR0
	add	x23, x21, :lo12:.LANCHOR0
	mov	w4, 4
	ldr	x26, [x0, 1968]
	add	x0, x20, :lo12:.LANCHOR5
	mov	x2, x26
	ldr	x22, [x0, 432]
	ldr	x0, [x23, 3384]
	mov	x3, x22
	ldrh	w1, [x0, 692]
	ldrb	w0, [x0, 694]
	bl	flash_get_last_written_page
	sxth	w24, w0
	ldr	x0, [x23, 3384]
	mov	w25, w24
	ldrh	w2, [x0, 696]
	cmp	w2, w24
	bgt	.L3207
	ldrh	w1, [x0, 692]
	mov	w3, w24
	adrp	x0, .LC241
	add	x0, x0, :lo12:.LC241
	adrp	x28, .LC243
	add	x28, x28, :lo12:.LC243
	adrp	x27, .LC242
	bl	printk
	ldr	x0, [x23, 3384]
	ldrsh	w23, [x0, 696]
	add	w0, w24, 1
	str	w0, [x29, 124]
	add	x0, x27, :lo12:.LC242
	str	x0, [x29, 112]
.L3208:
	ldr	w0, [x29, 124]
	cmp	w23, w0
	blt	.L3211
	add	x0, x20, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	mov	w1, 1
	add	w25, w25, 1
	strb	w1, [x0, 401]
	ldr	x0, [x21, 3384]
	strh	w25, [x0, 696]
	bl	pm_free_sblk
.L3207:
	add	x19, x19, :lo12:.LANCHOR3
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, 255
	add	x19, x19, 1960
	ldrh	w2, [x20, 462]
	ldr	x0, [x19, 8]
	bl	ftl_memset
	ldr	x1, [x19, 8]
	mov	w0, -1
	bl	pm_write_page
	ldrb	w0, [x20, 401]
	cbz	w0, .L3212
	ldr	x1, [x19, 8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19, 8]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19, 8]
	mov	w0, -1
	bl	pm_write_page
.L3212:
	bl	pm_free_sblk
	bl	pm_gc
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L3211:
	add	x27, x21, :lo12:.LANCHOR0
	add	x5, x19, :lo12:.LANCHOR3
	mov	x3, x22
	mov	x2, x26
	str	x5, [x29, 104]
	ldr	x0, [x27, 3384]
	ldrh	w1, [x5, 1410]
	ldrb	w4, [x5, 1946]
	ldrh	w24, [x0, 692]
	ldrb	w0, [x0, 694]
	madd	w24, w24, w1, w23
	mov	w1, w24
	bl	flash_read_page_en
	mov	w4, w0
	ldr	x0, [x27, 3384]
	str	w4, [x29, 120]
	mov	w2, w24
	ldr	w1, [x0, 48]
	ldrh	w3, [x0, 694]
	add	w1, w1, 1
	str	w1, [x0, 48]
	ldr	x0, [x29, 112]
	ldr	w1, [x22]
	bl	printk
	ldr	w4, [x29, 120]
	cmp	w4, 512
	ccmn	w4, #1, 4, ne
	beq	.L3209
	ldr	x0, [x27, 3384]
	ldr	w1, [x22]
	ldrh	w0, [x0, 698]
	cmp	w1, w0
	bcs	.L3209
	ldr	w2, [x22, 8]
	ldr	x5, [x29, 104]
	cbz	w2, .L3210
	ldrb	w1, [x5, 1946]
	mov	x0, x26
	str	w2, [x29, 120]
	lsl	w1, w1, 9
	bl	js_hash
	ldr	w2, [x29, 120]
	cmp	w2, w0
	beq	.L3210
	ldr	w1, [x22, 8]
	mov	x0, x28
	bl	printk
.L3209:
	add	w23, w23, 1
	sxth	w23, w23
	b	.L3208
.L3210:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldr	w0, [x22]
	add	x0, x0, 176
	str	w24, [x1, x0, lsl 2]
	b	.L3209
	.size	pm_init, .-pm_init
	.align	2
	.global	pm_log2phys
	.type	pm_log2phys, %function
pm_log2phys:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR3
	stp	x23, x24, [sp, 48]
	mov	x23, x1
	add	x1, x21, :lo12:.LANCHOR3
	stp	x25, x26, [sp, 64]
	stp	x19, x20, [sp, 16]
	mov	w24, w2
	str	x27, [sp, 80]
	adrp	x22, .LANCHOR0
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w20, [x1, 1946]
	ldr	w2, [x2, 3364]
	lsl	w25, w20, 7
	ubfiz	w20, w20, 7, 9
	cmp	w0, w2
	udiv	w25, w0, w25
	and	w26, w25, 65535
	msub	w20, w26, w20, w0
	bcc	.L3228
	mov	w1, w0
	adrp	x0, .LC244
	add	x0, x0, :lo12:.LC244
	bl	printk
	mov	w0, -1
	cbnz	w24, .L3227
	str	w0, [x23]
.L3227:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L3228:
	and	x20, x20, 65535
	add	x0, x1, 1968
	mov	x19, 0
.L3234:
	ldr	x1, [x0]
	cbz	x1, .L3230
	ldrh	w1, [x0, -8]
	cmp	w1, w26
	bne	.L3230
.L3231:
	cbnz	w24, .L3232
	add	x3, x21, :lo12:.LANCHOR3
	add	x22, x22, :lo12:.LANCHOR0
	add	x0, x3, x19, lsl 4
	mov	w2, 24
	mov	w1, 1
	ldr	x0, [x0, 1968]
	ldr	w0, [x0, x20, lsl 2]
	str	w0, [x23]
	ldrb	w4, [x22, 1205]
	sub	w2, w2, w4
	lsl	w1, w1, w4
	sub	w1, w1, #1
	lsr	w0, w0, w2
	and	w0, w0, w1
	ldrb	w1, [x3, 1412]
	cmp	w0, w1
	bcc	.L3233
	mov	w0, -1
	str	w0, [x23]
.L3233:
	add	x21, x21, :lo12:.LANCHOR3
	add	x21, x21, 1960
	add	x19, x21, x19, lsl 4
	ldrh	w0, [x19, 2]
	mvn	x1, x0
	tst	x1, 32767
	beq	.L3237
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L3237:
	mov	w0, 0
	b	.L3227
.L3232:
	add	x2, x21, :lo12:.LANCHOR3
	ldr	w3, [x23]
	add	x0, x2, 1960
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0, 8]
	str	w3, [x1, x20, lsl 2]
	strb	w25, [x2, 2472]
	ldrh	w1, [x0, 2]
	orr	w1, w1, -32768
	strh	w1, [x0, 2]
	b	.L3233
.L3230:
	add	w19, w19, 1
	add	x0, x0, 16
	and	x19, x19, 65535
	cmp	w19, 32
	bne	.L3234
	bl	pm_select_ram_region
	and	x19, x0, 65535
	add	x2, x21, :lo12:.LANCHOR3
	sbfiz	x1, x19, 4, 32
	add	x2, x2, 1960
	mov	w27, w0
	add	x3, x2, x1
	ldrh	w2, [x2, x1]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3235
	ldrsh	w1, [x3, 2]
	tbz	w1, #31, .L3235
	bl	pm_flush_id
.L3235:
	adrp	x0, .LANCHOR5+464
	mov	w1, w27
	strb	w19, [x0, #:lo12:.LANCHOR5+464]
	mov	w0, w26
	bl	load_l2p_region
	b	.L3231
	.size	pm_log2phys, .-pm_log2phys
	.align	2
	.global	gc_recovery
	.type	gc_recovery, %function
gc_recovery:
	sub	sp, sp, #224
	stp	x29, x30, [sp, 32]
	add	x29, sp, 32
	stp	x21, x22, [sp, 64]
	adrp	x22, .LANCHOR3
	stp	x19, x20, [sp, 48]
	adrp	x19, .LANCHOR0
	add	x21, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 80]
	stp	x25, x26, [sp, 96]
	adrp	x23, .LANCHOR5
	stp	x27, x28, [sp, 112]
	add	x0, x23, :lo12:.LANCHOR5
	add	x25, x22, :lo12:.LANCHOR3
	ldr	x20, [x21, 1128]
	strb	wzr, [x0, 264]
	mov	w0, 65535
	strb	wzr, [x25, 1337]
	ldrh	w1, [x20, 80]
	cmp	w1, w0
	beq	.L3244
	add	x24, x20, 80
	mov	w0, -1
	strh	w0, [x20, 130]
	mov	w0, 1
	bl	buf_alloc
	mov	x28, x0
	ldrb	w0, [x24, 9]
	ldrh	w1, [x25, 1410]
	sub	w0, w0, #1
	ldrb	w2, [x25, 1320]
	add	x0, x24, w0, sxtw 1
	cmp	w2, 2
	ldrh	w0, [x0, 16]
	mul	w1, w0, w1
	str	w1, [x29, 184]
	beq	.L3245
	ldrb	w0, [x21, 1212]
	cbnz	w0, .L3245
	ldrb	w0, [x21, 1213]
	cbz	w0, .L3246
.L3245:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1338]
	sub	w0, w0, #1
	add	w0, w0, w1
	orr	w2, w0, w2, lsl 24
	str	w2, [x28, 40]
.L3247:
	mov	w1, 1
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L3248
	ldr	x0, [x28, 24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3249
.L3248:
	mov	x0, x28
	bl	zbuf_free
	ldr	x1, [x28, 24]
	ldr	x0, [x28, 8]
	ldr	w2, [x1, 12]
	str	w2, [sp, 16]
	ldr	w2, [x1, 8]
	str	w2, [sp, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC245
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC245
	ldr	w1, [x28, 40]
	ldr	w2, [x28, 52]
	bl	printk
.L3338:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20, 80]
	mov	w21, 0
	ldr	x1, [x0, 1120]
	strh	wzr, [x1, x2, lsl 1]
	ldr	x0, [x0, 1128]
	ldrh	w1, [x20, 80]
	strh	w1, [x0, 130]
.L3250:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x20, 80]
	ldr	x1, [x1, 1120]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1, x2]
	cbnz	w1, .L3294
	bl	ftl_dump_write_open_sblk
.L3294:
	ldrh	w1, [x20, 80]
	mov	w2, w21
	adrp	x0, .LC247
	add	x0, x0, :lo12:.LC247
	bl	printk
	mov	w0, -1
	strh	w0, [x20, 80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldr	x0, [x1, 1128]
	ldrh	w0, [x0, 130]
	cmp	w0, w2
	beq	.L3295
	ldrh	w1, [x1, 1096]
	cmp	w1, w0
	bhi	.L3296
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 840
	mov	w2, 517
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3296:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 1128]
	ldrh	w0, [x0, 130]
	bl	ftl_free_sblk
.L3295:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19, 3384]
	strh	w0, [x1, 126]
	ldr	x1, [x19, 1128]
	strh	w0, [x1, 130]
	mov	w0, 0
	bl	ftl_info_flush
.L3243:
	ldp	x19, x20, [sp, 48]
	ldp	x21, x22, [sp, 64]
	ldp	x23, x24, [sp, 80]
	ldp	x25, x26, [sp, 96]
	ldp	x27, x28, [sp, 112]
	ldp	x29, x30, [sp, 32]
	add	sp, sp, 224
	ret
.L3246:
	cmp	w2, 3
	bne	.L3247
	ldrh	w0, [x25, 1376]
	sub	w0, w0, #1
	add	w0, w0, w1
	orr	w0, w0, 50331648
	str	w0, [x28, 40]
	b	.L3247
.L3249:
	add	x1, x23, :lo12:.LANCHOR5
	ldrb	w0, [x1, 465]
	cmp	w0, 2
	bne	.L3251
	add	x21, x22, :lo12:.LANCHOR3
	ldrb	w0, [x21, 1320]
	cmp	w0, 3
	bne	.L3251
	ldrh	w2, [x21, 1338]
	ldrb	w3, [x21, 1321]
	ldrh	w0, [x1, 462]
	ubfiz	w2, w2, 2, 14
	mul	w2, w2, w3
	ldr	x3, [x1, 280]
	ldr	x1, [x28, 8]
	sub	w2, w2, w0
	and	x0, x0, 65532
	and	w2, w2, 65535
	add	x0, x3, x0
	bl	ftl_memcpy
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1212]
	cbnz	w1, .L3252
	ldrb	w0, [x0, 1213]
	cbz	w0, .L3253
.L3252:
	ldr	w0, [x28, 40]
	sub	w0, w0, #1
.L3332:
	str	w0, [x28, 40]
	mov	w1, 1
	mov	x0, x28
	bl	sblk_read_page
	ldr	w0, [x28, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L3255
	ldr	x0, [x28, 24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3256
.L3255:
	mov	x0, x28
	bl	zbuf_free
	b	.L3338
.L3253:
	ldr	w0, [x29, 184]
	ldrh	w1, [x21, 1376]
	sub	w0, w0, #1
	add	w0, w0, w1
	orr	w0, w0, 33554432
	b	.L3332
.L3256:
	add	x0, x23, :lo12:.LANCHOR5
	ldrh	w2, [x0, 462]
.L3333:
	ldr	x1, [x28, 8]
	add	x21, x22, :lo12:.LANCHOR3
	ldr	x0, [x0, 280]
	bl	ftl_memcpy
	ldrb	w1, [x21, 1321]
	ldrh	w0, [x21, 1338]
	mul	w0, w0, w1
	ldrb	w1, [x21, 1946]
	lsl	w2, w0, 2
	cmp	w1, w0, lsr 6
	bge	.L3258
	add	x25, x23, :lo12:.LANCHOR5
	mov	w1, 0
	ldr	x0, [x25, 288]
	bl	ftl_memset
	ldrb	w26, [x25, 465]
	cmp	w26, 1
	bne	.L3299
	ldrb	w1, [x21, 1321]
	ldrh	w0, [x21, 1338]
	ldrh	w21, [x25, 462]
	mul	w0, w0, w1
	ldr	x1, [x28, 8]
	sub	w21, w21, w0, lsl 2
	add	x1, x1, w0, sxtw 2
	ldr	x0, [x25, 288]
	mov	w2, w21
	bl	ftl_memcpy
	mov	w10, w26
.L3259:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w1, [x0, 1376]
	ldrb	w25, [x0, 1320]
	str	w1, [x29, 168]
	cmp	w25, 2
	bne	.L3260
	ldrb	w0, [x0, 1322]
	cbz	w0, .L3261
.L3260:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1212]
	cbz	w0, .L3262
.L3261:
	ldr	w0, [x29, 168]
	mul	w0, w0, w25
	mov	w25, 1
	str	w0, [x29, 168]
.L3262:
	add	x14, x22, :lo12:.LANCHOR3
	adrp	x26, .LC246
	mov	w9, 0
	add	x0, x26, :lo12:.LC246
	mov	w13, 0
	str	x0, [x29, 152]
.L3263:
	sub	w0, w9, #1
	str	w0, [x29, 164]
	ldr	w0, [x29, 168]
	cmp	w0, w9
	bls	.L3276
	add	w0, w9, w9, lsl 1
	mov	w26, 0
	sub	w0, w0, #1
	str	w0, [x29, 172]
	b	.L3277
.L3251:
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w2, [x0, 1338]
	ldrb	w0, [x0, 1321]
	mul	w2, w2, w0
	add	x0, x23, :lo12:.LANCHOR5
	lsl	w2, w2, 2
	b	.L3333
.L3258:
	add	x3, x23, :lo12:.LANCHOR5
	ldr	x1, [x28, 8]
	add	x1, x1, w0, sxtw 2
	ldr	x0, [x3, 288]
	bl	ftl_memcpy
	mov	w10, 0
.L3334:
	mov	w21, 0
	b	.L3259
.L3299:
	mov	w10, 1
	b	.L3334
.L3272:
	ldrh	w1, [x14, 1410]
	ldrh	w0, [x24, x27, lsl 1]
	ldrb	w2, [x14, 1320]
	cmp	w2, 2
	mul	w0, w0, w1
	str	w0, [x29, 184]
	add	w1, w11, w0
	beq	.L3264
	add	x3, x19, :lo12:.LANCHOR0
	ldrb	w4, [x3, 1212]
	cbz	w4, .L3265
.L3264:
	ldr	w0, [x29, 164]
	add	w1, w1, w0
	orr	w1, w1, w2, lsl 24
.L3335:
	str	w1, [x28, 40]
.L3266:
	str	x14, [x29, 120]
	mov	w1, 1
	str	w10, [x29, 128]
	mov	x0, x28
	str	w9, [x29, 136]
	str	w13, [x29, 144]
	str	w11, [x29, 160]
	bl	sblk_read_page
	ldr	w10, [x29, 128]
	ldr	w13, [x29, 144]
	ldr	w9, [x29, 136]
	ldr	w11, [x29, 160]
	sxtw	x15, w13
	ldr	x14, [x29, 120]
	cbz	w10, .L3269
	add	x0, x23, :lo12:.LANCHOR5
	lsl	x1, x15, 2
	ldr	x0, [x0, 288]
	ldr	w2, [x0, x1]
	cbnz	w2, .L3269
	ldr	x2, [x28, 24]
	ldr	w2, [x2, 8]
	str	w2, [x0, x1]
.L3269:
	add	x1, x23, :lo12:.LANCHOR5
	lsl	x15, x15, 2
	ldr	x0, [x1, 280]
	ldr	w3, [x0, x15]
	ldr	x0, [x28, 24]
	ldr	w6, [x0, 4]
	cmp	w3, w6
	bne	.L3270
	ldr	x1, [x1, 288]
	ldr	w2, [x1, x15]
	ldr	w1, [x0, 8]
	cmp	w2, w1
	beq	.L3271
.L3270:
	add	x18, x23, :lo12:.LANCHOR5
	ldr	w2, [x0, 12]
	str	x14, [x29, 104]
	stp	w10, w11, [x29, 116]
	ldr	x1, [x18, 288]
	str	w2, [sp]
	str	w9, [x29, 128]
	ldr	w5, [x0]
	ldr	w4, [x1, x15]
	ldr	w7, [x0, 8]
	ldr	w1, [x28, 40]
	ldr	w2, [x28, 52]
	ldr	x0, [x29, 152]
	stp	x18, x15, [x29, 136]
	str	w13, [x29, 160]
	bl	printk
	ldp	x18, x15, [x29, 136]
	ldp	w10, w11, [x29, 116]
	ldr	x0, [x18, 280]
	ldr	w9, [x29, 128]
	ldr	w13, [x29, 160]
	ldr	x14, [x29, 104]
	ldr	w0, [x0, x15]
	cmn	w0, #1
	beq	.L3271
	mov	x0, x28
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20, 80]
	ldr	x1, [x0, 1120]
	strh	wzr, [x1, x2, lsl 1]
	ldr	x0, [x0, 1128]
	ldrh	w1, [x20, 80]
	strh	w1, [x0, 130]
	b	.L3250
.L3265:
	cmp	w2, 3
	bne	.L3267
	ldrb	w2, [x3, 1213]
	cbz	w2, .L3268
	ldr	w0, [x29, 172]
	add	w1, w1, w0
	orr	w1, w1, 50331648
	b	.L3335
.L3268:
	add	w0, w9, w0
	orr	w0, w0, w11, lsl 24
.L3336:
	str	w0, [x28, 40]
	b	.L3266
.L3267:
	add	w0, w9, w0
	b	.L3336
.L3271:
	add	w13, w13, 1
	add	w11, w11, 1
.L3274:
	cmp	w25, w11
	bcs	.L3272
	add	w26, w26, 1
.L3277:
	ldrb	w0, [x24, 9]
	cmp	w26, w0
	bge	.L3273
	sxtw	x27, w26
	mov	w11, 1
	add	x27, x27, 8
	b	.L3274
.L3273:
	ldrb	w0, [x14, 1322]
	cmp	w0, 0
	cinc	w9, w9, ne
	add	w9, w9, 1
	b	.L3263
.L3276:
	mov	x0, x28
	bl	zbuf_free
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR3
	ldrh	w2, [x20, 80]
	mov	w21, 0
	mov	w27, 0
	str	wzr, [x29, 164]
	ldrh	w3, [x3, 1338]
	ldr	x1, [x0, 1120]
	ldrb	w0, [x24, 9]
	mul	w0, w0, w3
	strh	w0, [x1, x2, lsl 1]
.L3278:
	sub	w0, w27, #1
	str	w0, [x29, 136]
	ldr	w0, [x29, 168]
	cmp	w0, w27
	bls	.L3292
	add	w0, w27, w27, lsl 1
	add	x28, x23, :lo12:.LANCHOR5
	sub	w0, w0, #1
	str	wzr, [x29, 172]
	str	w0, [x29, 160]
	b	.L3293
.L3288:
	ldr	w0, [x29, 164]
	ldr	x1, [x28, 280]
	sbfiz	x0, x0, 2, 32
	ldr	w5, [x1, x0]
	cmn	w5, #1
	beq	.L3279
	ldr	x1, [x28, 288]
	str	w5, [x29, 144]
	ldr	w0, [x1, x0]
	str	w0, [x29, 152]
	mov	w0, w5
	bl	lpa_hash_get_ppa
	str	w0, [x29, 188]
	cmn	w0, #1
	ldr	w5, [x29, 144]
	bne	.L3280
	mov	w0, w5
	str	w5, [x29, 144]
	mov	w2, 0
	add	x1, x29, 188
	bl	pm_log2phys
	ldr	w5, [x29, 144]
.L3280:
	ldr	x0, [x29, 128]
	add	x2, x22, :lo12:.LANCHOR3
	ldrh	w1, [x2, 1410]
	ldrh	w0, [x24, x0, lsl 1]
	ldrb	w2, [x2, 1320]
	cmp	w2, 2
	mul	w0, w0, w1
	add	w1, w26, w0
	beq	.L3281
	add	x6, x19, :lo12:.LANCHOR0
	ldrb	w7, [x6, 1212]
	cbz	w7, .L3282
.L3281:
	ldr	w0, [x29, 136]
	add	w1, w0, w1
	orr	w1, w1, w2, lsl 24
	str	w1, [x29, 184]
.L3283:
	add	x0, x19, :lo12:.LANCHOR0
	add	x6, x22, :lo12:.LANCHOR3
	ldr	w3, [x29, 152]
	ldrb	w2, [x0, 1205]
	mov	w0, 24
	ldrh	w1, [x6, 1304]
	sub	w0, w0, w2
	mov	w2, 1
	sub	w0, w0, w1
	lsr	w1, w3, w1
	lsl	w0, w2, w0
	sub	w0, w0, #1
	and	w0, w0, w1
	ldrb	w1, [x6, 1306]
	udiv	w0, w0, w1
	and	w0, w0, 65535
	str	w0, [x29, 144]
	ldr	w0, [x29, 188]
	cmp	w3, w0
	bne	.L3286
	add	x1, x29, 184
	mov	w0, w5
	bl	pm_log2phys
	add	w21, w21, 1
	ldrh	w0, [x29, 144]
	bl	ftl_vpn_decrement
.L3287:
	ldr	w0, [x29, 164]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29, 164]
	ldr	x1, [x2, 1104]
	ldr	w0, [x29, 144]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1, 2]
	tst	w1, 224
	bne	.L3279
	ldr	x1, [x2, 1120]
	lsl	x0, x0, 1
	ldrh	w2, [x1, x0]
	cbz	w2, .L3279
	strh	wzr, [x1, x0]
.L3279:
	add	w26, w26, 1
.L3290:
	cmp	w25, w26
	bcs	.L3288
	ldr	w0, [x29, 172]
	add	w0, w0, 1
	str	w0, [x29, 172]
.L3293:
	ldrb	w0, [x24, 9]
	ldr	w1, [x29, 172]
	cmp	w1, w0
	bge	.L3289
	ldrsw	x0, [x29, 172]
	mov	w26, 1
	add	x0, x0, 8
	str	x0, [x29, 128]
	b	.L3290
.L3282:
	cmp	w2, 3
	bne	.L3284
	ldrb	w6, [x6, 1213]
	lsl	w2, w26, 24
	cbz	w6, .L3285
	ldr	w0, [x29, 160]
	add	w1, w0, w1
	orr	w0, w1, w2
.L3337:
	str	w0, [x29, 184]
	b	.L3283
.L3285:
	add	w0, w27, w0
	orr	w0, w0, w2
	b	.L3337
.L3284:
	add	w0, w27, w0
	b	.L3337
.L3286:
	ldr	w1, [x29, 184]
	cmp	w0, w1
	cinc	w21, w21, eq
	b	.L3287
.L3289:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0, 1322]
	cmp	w0, 0
	cinc	w27, w27, ne
	add	w27, w27, 1
	b	.L3278
.L3292:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20, 80]
	ldr	x0, [x0, 1120]
	strh	w21, [x0, x1, lsl 1]
	ldrh	w0, [x20, 80]
	bl	zftl_insert_data_list
	b	.L3250
.L3244:
	ldrh	w0, [x20, 130]
	cmp	w0, w1
	beq	.L3243
	ldr	x1, [x21, 3384]
	ldrh	w1, [x1, 126]
	cmp	w1, w0
	bne	.L3298
	bl	pm_flush
	ldr	x0, [x21, 1128]
	ldrh	w0, [x0, 130]
	bl	ftl_free_sblk
	ldr	x0, [x21, 3384]
	mov	w1, -1
	strh	w1, [x0, 126]
	mov	w0, 0
	bl	ftl_info_flush
.L3298:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19, 1128]
	strh	w1, [x0, 130]
	b	.L3243
	.size	gc_recovery, .-gc_recovery
	.align	2
	.global	gc_update_l2p_map_new
	.type	gc_update_l2p_map_new, %function
gc_update_l2p_map_new:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	adrp	x22, .LANCHOR3
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x20, [x0, 1128]
	add	x0, x22, :lo12:.LANCHOR3
	ldrh	w24, [x0, 1338]
	add	x0, x20, 80
	str	x0, [x29, 112]
	ldrb	w0, [x0, 9]
	mul	w24, w24, w0
	adrp	x0, .LANCHOR2
	str	x0, [x29, 136]
	ldr	w1, [x0, #:lo12:.LANCHOR2]
	tbz	x1, 8, .L3340
	ldrh	w1, [x20, 80]
	adrp	x0, .LC248
	add	x0, x0, :lo12:.LC248
	bl	printk
.L3340:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x1, .LANCHOR5
	ldrh	w3, [x20, 80]
	mov	w23, 0
	mov	w21, 0
	str	x1, [x29, 120]
	ldr	x2, [x0, 1120]
	add	x0, x1, :lo12:.LANCHOR5
	ldrb	w0, [x0, 465]
	sub	w0, w24, w0
	strh	w0, [x2, x3, lsl 1]
	adrp	x0, .LC250
	add	x0, x0, :lo12:.LC250
	str	x0, [x29, 104]
.L3341:
	cmp	w21, w24
	bne	.L3350
	ldr	x0, [x29, 136]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3351
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20, 80]
	mov	w3, w23
	ldr	x1, [x0, 1120]
	ldr	x0, [x0, 1128]
	ldrh	w2, [x1, x2, lsl 1]
	ldrh	w1, [x0, 80]
	adrp	x0, .LC251
	add	x0, x0, :lo12:.LC251
	bl	printk
.L3351:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20, 80]
	ldr	x0, [x0, 1120]
	ldrh	w0, [x0, x1, lsl 1]
	cmp	w23, w0
	beq	.L3352
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 856
	mov	w2, 898
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3352:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20, 80]
	ldr	x0, [x19, 1120]
	strh	w23, [x0, x1, lsl 1]
	ldrh	w0, [x20, 80]
	bl	zftl_insert_data_list
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 160
	ret
.L3350:
	ldr	x0, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	x0, [x0, 280]
	ldr	w2, [x0, w21, sxtw 2]
	cmn	w2, #1
	beq	.L3342
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w0, [x0, 1946]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29, 132]
	ldr	x0, [x29, 136]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3343
	ldr	w1, [x29, 132]
	adrp	x0, .LC249
	mov	w3, w21
	add	x0, x0, :lo12:.LC249
	bl	printk
.L3343:
	ldr	x0, [x29, 120]
	mov	w25, w21
	add	x26, x0, :lo12:.LANCHOR5
.L3349:
	ldr	x0, [x26, 280]
	sbfiz	x27, x25, 2, 32
	ldr	w4, [x0, x27]
	cmn	w4, #1
	beq	.L3344
	add	x0, x22, :lo12:.LANCHOR3
	ldr	w1, [x29, 132]
	ldrb	w0, [x0, 1946]
	lsl	w0, w0, 7
	udiv	w0, w4, w0
	cmp	w1, w0, uxth
	bne	.L3344
	ldr	x0, [x26, 288]
	str	w4, [x29, 128]
	ldr	w28, [x0, x27]
	mov	w0, w4
	bl	lpa_hash_get_ppa
	str	w0, [x29, 156]
	cmn	w0, #1
	ldr	w4, [x29, 128]
	bne	.L3345
	mov	w0, w4
	str	w4, [x29, 128]
	mov	w2, 0
	add	x1, x29, 156
	bl	pm_log2phys
	ldr	w4, [x29, 128]
.L3345:
	ldr	w3, [x29, 156]
	cmp	w28, w3
	bne	.L3346
	ldr	x0, [x26, 296]
	mov	w2, 1
	add	x1, x29, 152
	add	w23, w23, 1
	ldr	w0, [x0, x27]
	str	w0, [x29, 152]
	mov	w0, w4
	bl	pm_log2phys
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x22, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1205]
	mov	w0, 24
	ldrh	w2, [x3, 1304]
	sub	w0, w0, w1
	sub	w1, w0, w2
	mov	w0, 1
	lsr	w28, w28, w2
	lsl	w0, w0, w1
	sub	w0, w0, #1
	and	w28, w0, w28
	ldrb	w0, [x3, 1306]
	udiv	w0, w28, w0
.L3373:
	bl	ftl_vpn_decrement
	ldr	x0, [x26, 280]
	mov	w1, -1
	str	w1, [x0, x27]
.L3344:
	add	w25, w25, 1
	cmp	w24, w25
	bne	.L3349
.L3342:
	add	w21, w21, 1
	b	.L3341
.L3346:
	ldr	x0, [x29, 136]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3348
	ldr	x0, [x29, 104]
	mov	w2, w28
	mov	w1, w4
	bl	printk
.L3348:
	ldr	x0, [x29, 112]
	ldrh	w0, [x0]
	b	.L3373
	.size	gc_update_l2p_map_new, .-gc_update_l2p_map_new
	.align	2
	.global	gc_scan_src_blk_one_page
	.type	gc_scan_src_blk_one_page, %function
gc_scan_src_blk_one_page:
	stp	x29, x30, [sp, -96]!
	mov	w3, 0
	mov	w4, 0
	mov	w5, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x1, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	add	x1, x1, 3416
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	adrp	x21, .LANCHOR3
	ldrb	w0, [x1, 4]
	add	x2, x1, w0, sxtw 1
	ldrh	w22, [x2, 40]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w6, [x2, 1321]
	ldrh	w2, [x1, 2]
.L3375:
	cmp	w22, w5
	beq	.L3377
	cbz	w4, .L3378
	add	x1, x20, :lo12:.LANCHOR0
	strh	w2, [x1, 3418]
.L3378:
	cbz	w3, .L3379
	add	x1, x20, :lo12:.LANCHOR0
	strb	w0, [x1, 3420]
.L3379:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 1
	add	x19, x20, 3416
	bl	buf_alloc
	add	x25, x21, :lo12:.LANCHOR3
	mov	x23, x0
	mov	w24, 1
.L3380:
	ldrb	w1, [x19, 6]
	cmp	w24, w1
	ble	.L3390
	mov	x0, x23
	bl	zbuf_free
	ldrb	w0, [x19, 4]
	add	x21, x21, :lo12:.LANCHOR3
	add	w0, w0, 1
	and	w0, w0, 255
	ldrb	w1, [x21, 1321]
	strb	w0, [x19, 4]
	cmp	w1, w0
	bne	.L3374
	ldrh	w0, [x19, 2]
	strb	wzr, [x19, 4]
	add	w0, w0, 1
	strh	w0, [x19, 2]
.L3374:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
.L3377:
	add	w0, w0, 1
	and	w0, w0, 255
	cmp	w0, w6
	bne	.L3376
	add	w2, w2, 1
	mov	w0, 0
	and	w2, w2, 65535
	mov	w4, 1
.L3376:
	add	x3, x1, w0, sxtw 1
	ldrh	w22, [x3, 40]
	mov	w3, 1
	b	.L3375
.L3390:
	ldrh	w0, [x25, 1410]
	cmp	w1, 2
	mul	w0, w0, w22
	bne	.L3381
	ldrh	w1, [x19, 2]
	lsl	w1, w1, 1
.L3400:
	sub	w1, w1, #1
	add	w0, w24, w0
	add	w0, w1, w0
	ldrb	w1, [x25, 1320]
	orr	w0, w0, w1, lsl 24
.L3399:
	str	w0, [x23, 40]
	mov	w1, 1
	mov	x0, x23
	bl	sblk_read_page
	ldr	w0, [x23, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L3387
	ldr	x0, [x23, 24]
	ldr	w26, [x0, 4]
	mov	w0, w26
	bl	lpa_hash_get_ppa
	str	w0, [x29, 92]
	cmn	w0, #1
	bne	.L3388
	ldr	w0, [x20, 3364]
	cmp	w26, w0
	bcs	.L3388
	mov	w2, 0
	add	x1, x29, 92
	mov	w0, w26
	bl	pm_log2phys
.L3388:
	ldr	w0, [x23, 40]
	ldr	w1, [x29, 92]
	cmp	w0, w1
	bne	.L3387
	ldrh	w2, [x19, 24]
	ldr	x1, [x25, 1312]
	str	w0, [x1, x2, lsl 2]
	ldrh	w0, [x19, 24]
	add	w0, w0, 1
	strh	w0, [x19, 24]
.L3387:
	ldrh	w0, [x19, 26]
	add	w24, w24, 1
	add	w0, w0, 1
	strh	w0, [x19, 26]
	b	.L3380
.L3381:
	cmp	w1, 3
	bne	.L3383
	ldrb	w1, [x20, 1212]
	cbnz	w1, .L3384
	ldrb	w1, [x20, 1213]
	cbz	w1, .L3385
.L3384:
	ldrh	w1, [x19, 2]
	add	w1, w1, w1, lsl 1
	b	.L3400
.L3385:
	ldrh	w1, [x19, 2]
	add	w0, w1, w0
	orr	w0, w0, w24, lsl 24
	b	.L3399
.L3383:
	ldrh	w1, [x19, 2]
	add	w0, w1, w0
	b	.L3399
	.size	gc_scan_src_blk_one_page, .-gc_scan_src_blk_one_page
	.align	2
	.global	gc_scan_src_blk
	.type	gc_scan_src_blk, %function
gc_scan_src_blk:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	str	x27, [sp, 80]
	ldrh	w1, [x0, 3416]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3402
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 880
	mov	w2, 1505
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3402:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3416]
	cmp	w1, w2
	beq	.L3432
	adrp	x2, .LANCHOR2
	ldr	w2, [x2, #:lo12:.LANCHOR2]
	tbz	x2, 8, .L3404
	ldr	x0, [x0, 1120]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x0, x2]
	adrp	x0, .LC252
	add	x0, x0, :lo12:.LC252
	bl	printk
.L3404:
	bl	timer_get_time
	add	x14, x20, :lo12:.LANCHOR0
	add	x15, x14, 3416
	add	x1, x14, 3456
	ldrh	w0, [x14, 3416]
	bl	ftl_get_blk_list_in_sblk
	and	w1, w0, 255
	strb	w1, [x15, 5]
	cbnz	w1, .L3405
	mov	w0, -1
	strh	w0, [x14, 3416]
.L3432:
	mov	w0, 0
	b	.L3401
.L3405:
	ldrh	w3, [x14, 3416]
	ldr	x1, [x14, 1104]
	mov	x2, x3
	add	x1, x1, x3, lsl 2
	ldrb	w1, [x1, 2]
	and	w1, w1, 224
	cmp	w1, 32
	beq	.L3406
	cmp	w1, 224
	beq	.L3406
	cbz	w1, .L3407
	ldr	x1, [x14, 1128]
	ldrh	w3, [x1, 16]
	cmp	w3, w2
	beq	.L3406
	ldrh	w3, [x1, 48]
	cmp	w3, w2
	beq	.L3406
	ldrh	w1, [x1, 80]
	cmp	w1, w2
	bne	.L3452
.L3406:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x20, 3440]
	strh	w0, [x20, 3416]
	b	.L3432
.L3407:
	ldr	x0, [x14, 1120]
	ldrh	w0, [x0, x3, lsl 1]
	cbz	w0, .L3409
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 880
	mov	w2, 1530
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3409:
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3416]
	ldr	x0, [x0, 1120]
	strh	wzr, [x0, x1, lsl 1]
	b	.L3406
.L3452:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x15, w0, sxtw 1
	ldrh	w22, [x0, 40]
	mov	w0, 65535
	cmp	w22, w0
	bne	.L3410
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 880
	mov	w2, 1540
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3410:
	add	x3, x20, :lo12:.LANCHOR0
	adrp	x23, .LANCHOR3
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w2, [x3, 3416]
	ldr	x3, [x3, 1104]
	ldrh	w21, [x1, 1410]
	ldrh	w0, [x1, 1376]
	add	x2, x3, x2, lsl 2
	sub	w0, w0, #1
	and	w0, w0, 65535
	mul	w21, w21, w22
	ldrb	w2, [x2, 2]
	and	w2, w2, 224
	cmp	w2, 160
	bne	.L3433
	ldrb	w2, [x1, 1320]
	and	w27, w2, 65535
	cmp	w2, 2
	orr	w21, w21, w2, lsl 24
	bne	.L3412
	ldrh	w0, [x1, 1338]
	sub	w0, w0, #1
	and	w0, w0, 65535
.L3412:
	add	x1, x20, :lo12:.LANCHOR0
	ldrb	w2, [x1, 1212]
	cbnz	w2, .L3413
	ldrb	w1, [x1, 1213]
	cbz	w1, .L3411
.L3413:
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1338]
	sub	w0, w0, #1
	and	w0, w0, 65535
.L3411:
	add	x24, x20, :lo12:.LANCHOR0
	orr	w21, w0, w21
	mov	w0, 1
	strb	w27, [x24, 3422]
	strh	wzr, [x24, 3418]
	strb	wzr, [x24, 3420]
	strh	wzr, [x24, 3442]
	strh	wzr, [x24, 3444]
	bl	buf_alloc
	mov	x19, x0
	str	w21, [x0, 40]
	mov	w1, 1
	bl	sblk_read_page
	strh	wzr, [x24, 3440]
	ldr	w0, [x19, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3414
.L3457:
	mov	x0, x19
	bl	zbuf_free
.L3455:
	mov	w0, -1
.L3401:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L3433:
	mov	w27, 1
	b	.L3411
.L3414:
	ldr	x0, [x19, 24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3415
	mov	w2, 1578
.L3458:
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 880
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3457
.L3415:
	add	x1, x23, :lo12:.LANCHOR3
	adrp	x26, .LANCHOR5
	add	x2, x26, :lo12:.LANCHOR5
	ldrb	w24, [x1, 1321]
	ldrh	w25, [x1, 1376]
	ldrb	w3, [x2, 465]
	cmp	w3, 2
	mul	w25, w24, w25
	mul	w25, w27, w25
	and	w25, w25, 65535
	bne	.L3416
	cmp	w27, 3
	bne	.L3416
	ldrh	w0, [x2, 462]
	ldrh	w2, [x1, 1338]
	ldr	x3, [x1, 1312]
	ldr	x1, [x19, 8]
	ubfiz	w2, w2, 2, 14
	mul	w24, w24, w2
	sub	w24, w24, w0
	and	x0, x0, 65532
	and	w24, w24, 65535
	add	x0, x3, x0
	mov	w2, w24
	bl	ftl_memcpy
	ldr	x0, [x19, 24]
	ldr	w27, [x0, 4]
	cbz	w27, .L3417
	ldr	x0, [x19, 8]
	mov	w1, w24
	bl	js_hash
	cmp	w27, w0
	beq	.L3417
	mov	x0, x19
	bl	zbuf_free
	ldr	x0, [x19, 24]
	mov	w3, w24
.L3456:
	ldr	w1, [x0, 4]
	adrp	x0, .LC253
	ldr	w2, [x19, 40]
	add	x0, x0, :lo12:.LC253
	bl	printk
	b	.L3455
.L3417:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0, 1212]
	cbnz	w1, .L3418
	ldrb	w0, [x0, 1213]
	cbz	w0, .L3419
.L3418:
	sub	w0, w21, #1
	str	w0, [x19, 40]
.L3420:
	mov	w1, 1
	mov	x0, x19
	bl	sblk_read_page
	ldr	w0, [x19, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	beq	.L3457
	ldr	x0, [x19, 24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L3422
	mov	w2, 1619
	b	.L3458
.L3419:
	add	x1, x23, :lo12:.LANCHOR3
	ldrh	w0, [x1, 1376]
	ldrh	w1, [x1, 1410]
	sub	w0, w0, #1
	and	w0, w0, 65535
	orr	w0, w0, 33554432
	mul	w22, w1, w22
	orr	w22, w0, w22
	str	w22, [x19, 40]
	b	.L3420
.L3422:
	add	x26, x26, :lo12:.LANCHOR5
	add	x0, x23, :lo12:.LANCHOR3
	ldrh	w2, [x26, 462]
.L3454:
	ldr	x0, [x0, 1312]
	add	x23, x23, :lo12:.LANCHOR3
	ldr	x1, [x19, 8]
	add	x24, x20, :lo12:.LANCHOR0
	add	x21, x24, 3416
	mov	x27, 0
	mov	w26, 24
	bl	ftl_memcpy
	ldr	x22, [x23, 1312]
.L3425:
	cmp	w25, w27
	bgt	.L3430
	mov	x0, x19
	bl	zbuf_free
	add	x0, x20, :lo12:.LANCHOR0
	ldrh	w1, [x0, 3416]
	ldr	x3, [x0, 1120]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3, x2]
	ldrh	w3, [x0, 3440]
	cmp	w2, w3
	beq	.L3431
	adrp	x0, .LC254
	add	x0, x0, :lo12:.LC254
	bl	printk
.L3431:
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w2, [x20, 3416]
	ldr	x1, [x20, 1120]
	ldrh	w3, [x20, 3440]
	strh	w3, [x1, x2, lsl 1]
	strh	wzr, [x20, 3444]
	ldrh	w0, [x20, 3440]
	b	.L3401
.L3416:
	ldr	w22, [x0, 4]
	lsl	w21, w25, 2
	ldr	x0, [x19, 8]
	mov	w1, w21
	bl	js_hash
	cmp	w22, w0
	beq	.L3424
	mov	x0, x19
	bl	zbuf_free
	mov	w3, w21
	ldr	x0, [x19, 24]
	b	.L3456
.L3424:
	add	x0, x23, :lo12:.LANCHOR3
	mov	w2, w21
	b	.L3454
.L3430:
	ldr	w0, [x22, x27, lsl 2]
	cmn	w0, #1
	beq	.L3427
	bl	lpa_hash_get_ppa
	str	w0, [x29, 108]
	cmn	w0, #1
	bne	.L3428
	ldr	w0, [x22, x27, lsl 2]
	mov	w2, 0
	add	x1, x29, 108
	bl	pm_log2phys
.L3428:
	ldrb	w1, [x24, 1205]
	ldrh	w2, [x23, 1304]
	sub	w0, w26, w1
	ldr	w3, [x29, 108]
	sub	w1, w0, w2
	mov	w0, 1
	lsl	w0, w0, w1
	ldrb	w1, [x23, 1306]
	sub	w0, w0, #1
	lsr	w2, w3, w2
	and	w0, w0, w2
	udiv	w0, w0, w1
	ldrh	w1, [x21]
	cmp	w0, w1
	bne	.L3427
	ldrh	w1, [x21, 24]
	ldr	x0, [x23, 1312]
	str	w3, [x0, x1, lsl 2]
	ldrh	w0, [x21, 24]
	add	w0, w0, 1
	strh	w0, [x21, 24]
.L3427:
	bl	timer_get_time
	add	x27, x27, 1
	b	.L3425
	.size	gc_scan_src_blk, .-gc_scan_src_blk
	.align	2
	.global	gc_scan_static_data
	.type	gc_scan_static_data, %function
gc_scan_static_data:
	stp	x29, x30, [sp, -64]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x19, x1, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	ldr	x0, [x19, 1128]
	ldr	w2, [x0, 544]
	cmn	w2, #1
	beq	.L3460
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	add	x20, x20, 896
	mov	w21, 0
.L3468:
	ldr	x0, [x19, 1128]
	mov	w2, 0
	add	x1, x29, 60
	ldr	w0, [x0, 544]
	bl	pm_log2phys
	ldr	w0, [x29, 60]
	cmn	w0, #1
	beq	.L3461
	mov	w0, 1
	bl	buf_alloc
	ldr	w1, [x29, 60]
	mov	x22, x0
	str	w1, [x0, 40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x22, 52]
	cmp	w0, 256
	bne	.L3462
	adrp	x2, .LANCHOR3
	add	x2, x2, :lo12:.LANCHOR3
	ldrb	w1, [x19, 1205]
	mov	w0, 24
	ldr	w3, [x29, 60]
	ldrh	w4, [x2, 1304]
	sub	w0, w0, w1
	mov	w1, 1
	sub	w0, w0, w4
	lsr	w3, w3, w4
	lsl	w0, w1, w0
	sub	w0, w0, #1
	and	w0, w0, w3
	ldrb	w3, [x2, 1306]
	mov	w2, 0
	udiv	w0, w0, w3
	bl	gc_add_sblk
.L3462:
	ldr	x0, [x19, 1128]
	ldr	x1, [x22, 24]
	ldr	w0, [x0, 544]
	ldr	w1, [x1, 4]
	cmp	w1, w0
	beq	.L3463
	mov	x1, x20
	mov	w2, 2163
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3463:
	mov	x0, x22
	bl	zbuf_free
.L3461:
	ldr	x0, [x19, 1128]
	ldr	w2, [x19, 3364]
	ldr	w1, [x0, 544]
	add	w1, w1, 1
	str	w1, [x0, 544]
	cmp	w1, w2
	bcc	.L3464
	mov	w1, -1
	str	w1, [x0, 544]
	ldr	w1, [x0, 548]
	add	w1, w1, 1
	str	w1, [x0, 548]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
.L3459:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 64
	ret
.L3464:
	ldr	w0, [x29, 60]
	cmn	w0, #1
	bne	.L3459
	adrp	x1, .LANCHOR5+462
	add	w0, w21, 1
	and	w0, w0, 65535
	ldrh	w1, [x1, #:lo12:.LANCHOR5+462]
	cmp	w21, w1, lsr 2
	bcs	.L3459
	mov	w21, w0
	b	.L3468
.L3460:
	ldr	w2, [x0, 536]
	ldr	w3, [x0, 12]
	add	w2, w2, 12959744
	add	w2, w2, 256
	cmp	w3, w2
	bhi	.L3470
	ldr	w2, [x0, 540]
	mov	w4, 5000
	add	w2, w2, w4
	ldr	x4, [x19, 3384]
	ldr	w4, [x4, 44]
	cmp	w4, w2
	bls	.L3459
.L3470:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x1, [x1, 3384]
	ldr	w1, [x1, 44]
	str	w1, [x0, 540]
	str	w3, [x0, 536]
	str	wzr, [x0, 544]
	b	.L3459
	.size	gc_scan_static_data, .-gc_scan_static_data
	.align	2
	.global	gc_block_vpn_scan
	.type	gc_block_vpn_scan, %function
gc_block_vpn_scan:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	x1, [x20, 1128]
	ldrh	w0, [x20, 1096]
	ldr	w1, [x1, 608]
	cmp	w1, w0
	bcs	.L3476
	bl	timer_get_time
	ldr	x21, [x20, 1128]
	mov	w2, 30000
	ldr	w1, [x21, 604]
	add	w1, w1, w2
	cmp	w0, w1
	bls	.L3476
	bl	timer_get_time
	str	w0, [x21, 604]
	ldr	x0, [x20, 1128]
	ldrh	w2, [x20, 1096]
	ldr	w1, [x0, 600]
	cmp	w1, w2
	bcs	.L3478
	ldr	x2, [x20, 3384]
	ldrh	w2, [x2, 134]
	cmp	w1, w2
	bcs	.L3479
.L3478:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1, 3384]
	ldrh	w1, [x1, 134]
	str	w1, [x0, 600]
.L3479:
	ldr	w25, [x0, 600]
	mov	w0, 65535
	and	w21, w25, 65535
	cmp	w21, w0
	bne	.L3480
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 920
	mov	w2, 2504
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3480:
	add	x14, x19, :lo12:.LANCHOR0
	add	x15, x29, 128
	ldr	x0, [x14, 1128]
	ldr	w1, [x0, 600]
	add	w1, w1, 1
	str	w1, [x0, 600]
	ldr	w1, [x0, 608]
	add	w1, w1, 1
	str	w1, [x0, 608]
	mov	x1, x15
	mov	w0, w21
	bl	ftl_get_blk_list_in_sblk
	mov	w1, w0
	tst	w0, 65535
	beq	.L3476
	ldr	x24, [x14, 1104]
	uxtw	x23, w21
	mov	w2, 224
	add	x24, x24, x23, lsl 2
	ldrb	w0, [x24, 2]
	and	w0, w0, 224
	cmp	w0, 32
	ccmp	w0, w2, 4, ne
	beq	.L3481
	cbz	w0, .L3482
	ldr	x0, [x14, 1128]
	ldrh	w2, [x0, 16]
	cmp	w2, w21
	beq	.L3476
	ldrh	w2, [x0, 48]
	cmp	w2, w21
	beq	.L3476
	ldrh	w0, [x0, 80]
	cmp	w0, w21
	bne	.L3512
.L3476:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L3481:
	cbnz	w0, .L3476
.L3482:
	add	x0, x19, :lo12:.LANCHOR0
	lsl	x23, x23, 1
	ldr	x0, [x0, 1120]
	ldrh	w0, [x0, x23]
	cbz	w0, .L3484
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 920
	mov	w2, 2521
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3484:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 1120]
	strh	wzr, [x0, x23]
	b	.L3476
.L3512:
	and	w1, w1, 65535
	mov	w0, 65535
	sub	w1, w1, #1
	ldrh	w26, [x15, w1, sxtw 1]
	cmp	w26, w0
	bne	.L3486
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 920
	mov	w2, 2529
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3486:
	adrp	x20, .LANCHOR3
	add	x2, x20, :lo12:.LANCHOR3
	ldrb	w1, [x24, 2]
	ldrh	w22, [x2, 1410]
	and	w1, w1, 224
	ldrh	w0, [x2, 1376]
	cmp	w1, 160
	sub	w0, w0, #1
	and	w0, w0, 65535
	mul	w22, w22, w26
	bne	.L3496
	ldrb	w1, [x2, 1320]
	cmp	w1, 2
	orr	w22, w22, w1, lsl 24
	beq	.L3488
	and	w26, w1, 65535
.L3487:
	add	x20, x20, :lo12:.LANCHOR3
	orr	w22, w0, w22
	mov	w0, 1
	bl	buf_alloc
	str	w22, [x0, 40]
	mov	x27, x0
	mov	w1, 1
	bl	sblk_read_page
	ldrb	w0, [x20, 1321]
	mov	w1, 255
	ldrh	w2, [x20, 1338]
	mul	w2, w2, w0
	ldr	x0, [x20, 1312]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x27, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3489
.L3514:
	mov	w2, 0
	mov	w1, 1
	mov	w0, w21
	bl	gc_add_sblk
	mov	x0, x27
	bl	zbuf_free
	b	.L3476
.L3488:
	ldrh	w0, [x2, 1338]
	mov	w26, w1
	sub	w0, w0, #1
	and	w0, w0, 65535
	b	.L3487
.L3496:
	mov	w26, 1
	b	.L3487
.L3489:
	ldr	x0, [x27, 24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	bne	.L3514
	ldrh	w0, [x20, 1376]
	mov	x28, 0
	ldrb	w1, [x20, 1321]
	mov	w22, 0
	mul	w1, w1, w0
	mul	w1, w26, w1
	ldr	x26, [x27, 8]
	and	w0, w1, 65535
	str	w0, [x29, 108]
	and	w0, w25, 65535
	str	w0, [x29, 104]
	add	x0, x19, :lo12:.LANCHOR0
	str	x0, [x29, 96]
.L3491:
	ldr	w0, [x29, 108]
	cmp	w0, w28
	bgt	.L3494
	mov	x0, x27
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3495
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x24, 2]
	mov	w3, w22
	and	w1, w25, 65535
	ldr	x0, [x0, 1120]
	ubfx	x4, x4, 5, 3
	ldrh	w2, [x0, x23, lsl 1]
	adrp	x0, .LC255
	add	x0, x0, :lo12:.LC255
	bl	printk
.L3495:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w22, 31
	ldr	x0, [x19, 1120]
	strh	w22, [x0, x23, lsl 1]
	bhi	.L3476
	mov	w2, 0
	mov	w1, 1
	mov	w0, w21
	bl	gc_add_sblk
	b	.L3476
.L3494:
	ldr	w0, [x26, x28, lsl 2]
	cmn	w0, #1
	beq	.L3492
	bl	lpa_hash_get_ppa
	str	w0, [x29, 124]
	cmn	w0, #1
	bne	.L3493
	ldr	w0, [x26, x28, lsl 2]
	mov	w2, 0
	add	x1, x29, 124
	bl	pm_log2phys
.L3493:
	ldr	x0, [x29, 96]
	mov	w1, 24
	ldrh	w2, [x20, 1304]
	ldrb	w0, [x0, 1205]
	sub	w0, w1, w0
	sub	w1, w0, w2
	mov	w0, 1
	lsl	w0, w0, w1
	ldr	w1, [x29, 124]
	sub	w0, w0, #1
	lsr	w1, w1, w2
	and	w0, w0, w1
	ldrb	w1, [x20, 1306]
	udiv	w0, w0, w1
	ldr	w1, [x29, 104]
	cmp	w1, w0
	bne	.L3492
	add	w22, w22, 1
	and	w22, w22, 65535
.L3492:
	add	x28, x28, 1
	b	.L3491
	.size	gc_block_vpn_scan, .-gc_block_vpn_scan
	.align	2
	.global	ftl_sblk_dump
	.type	ftl_sblk_dump, %function
ftl_sblk_dump:
	sub	sp, sp, #288
	and	x0, x0, 65535
	stp	x29, x30, [sp, 48]
	add	x29, sp, 48
	stp	x19, x20, [sp, 64]
	mov	x20, x0
	stp	x21, x22, [sp, 80]
	adrp	x21, .LANCHOR0
	add	x19, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 96]
	stp	x25, x26, [sp, 112]
	lsl	x23, x0, 2
	stp	x27, x28, [sp, 128]
	str	x0, [x29, 136]
	ldr	x0, [x19, 1104]
	str	x1, [x29, 152]
	add	x1, x0, x23
	ldr	w5, [x0, x23]
	ldrb	w2, [x1, 2]
	mov	w1, w20
	ldrh	w4, [x0, x23]
	adrp	x0, .LC256
	ubfx	x5, x5, 11, 8
	add	x0, x0, :lo12:.LC256
	ubfx	x3, x2, 3, 2
	and	w4, w4, 2047
	ubfx	x2, x2, 5, 3
	bl	printk
	str	x21, [x29, 160]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L3537
	ldrh	w1, [x19, 1096]
	mov	w0, 0
	cmp	w1, w20
	bls	.L3515
	ldr	x0, [x19, 1104]
	adrp	x28, .LANCHOR3
	add	x0, x0, x23
	ldrb	w0, [x0, 2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L3539
	add	x0, x28, :lo12:.LANCHOR3
	ldrb	w24, [x0, 1320]
.L3517:
	add	x27, x29, 240
	mov	w0, w20
	mov	w19, 0
	strh	w20, [x27, -32]!
	add	x1, x27, 16
	bl	ftl_get_blk_list_in_sblk
	add	x2, x28, :lo12:.LANCHOR3
	and	w1, w0, 255
	strb	w1, [x29, 217]
	and	w4, w0, 255
	strh	wzr, [x29, 210]
	mov	w3, w24
	ldrh	w2, [x2, 1376]
	adrp	x0, .LC257
	strb	wzr, [x29, 213]
	add	x0, x0, :lo12:.LC257
	strh	wzr, [x29, 218]
	mul	w1, w1, w2
	strh	w1, [x29, 214]
	ldr	x1, [x29, 160]
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x1, [x1, 1104]
	add	x1, x1, x23
	ldrb	w2, [x1, 2]
	mov	w1, w20
	ubfx	x2, x2, 5, 3
	bl	printk
	mov	w0, 1
	bl	buf_alloc
	stp	wzr, wzr, [x29, 168]
	mov	x25, x0
	str	wzr, [x29, 176]
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	str	x0, [x29, 120]
.L3518:
	add	x0, x28, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1376]
	cmp	w0, w19
	bls	.L3535
	lsl	w0, w19, 1
	mov	w22, 0
	sub	w1, w0, #1
	add	w0, w0, w19
	sub	w0, w0, #1
	stp	w1, w0, [x29, 144]
	b	.L3536
.L3539:
	mov	w24, 1
	b	.L3517
.L3532:
	ldr	x0, [x29, 128]
	ldrh	w10, [x27, x0]
	mov	w0, 65535
	cmp	w10, w0
	beq	.L3519
	add	x2, x28, :lo12:.LANCHOR3
	cmp	w24, 3
	ldrh	w8, [x2, 1410]
	mul	w1, w8, w10
	add	w0, w21, w1
	bne	.L3520
	ldr	x3, [x29, 160]
	add	x3, x3, :lo12:.LANCHOR0
	ldrb	w4, [x3, 1212]
	cbz	w4, .L3521
	ldrb	w8, [x2, 1320]
	ldr	w1, [x29, 148]
.L3548:
	add	w0, w0, w1
	orr	w26, w0, w8, lsl 24
	b	.L3522
.L3521:
	ldrb	w2, [x3, 1213]
	lsl	w8, w21, 24
	cbz	w2, .L3523
	ldr	w1, [x29, 148]
	add	w0, w0, w1
	orr	w26, w0, w8
.L3522:
	str	w26, [x25, 40]
	mov	w1, 1
	str	w10, [x29, 108]
	mov	x0, x25
	bl	sblk_read_page
	ldr	x1, [x25, 24]
	mov	w3, w26
	ldr	x0, [x25, 8]
	ldr	w11, [x25, 52]
	ldr	w10, [x29, 108]
	ldr	w2, [x1, 12]
	mov	w4, w11
	str	w2, [sp, 32]
	str	w11, [x29, 112]
	ldr	w2, [x1, 8]
	str	w2, [sp, 24]
	ldr	w2, [x1, 4]
	str	w2, [sp, 16]
	mov	w2, w19
	ldr	w1, [x1]
	str	w1, [sp, 8]
	ldr	w1, [x0, 12]
	str	w1, [sp]
	mov	w1, w10
	ldp	w5, w6, [x0]
	ldr	w7, [x0, 8]
	ldr	x0, [x29, 120]
	bl	printk
	ldr	w11, [x29, 112]
	ldr	w0, [x29, 168]
	cmp	w11, 512
	ccmn	w11, #1, 4, ne
	csinc	w0, w0, wzr, ne
	str	w0, [x29, 168]
	mov	x0, 35160
	movk	x0, 0x41, lsl 16
	bl	__const_udelay
	ldr	x0, [x29, 160]
	mov	w1, 32
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x0, [x0, 1104]
	add	x0, x0, x23
	ldrb	w0, [x0, 2]
	and	w0, w0, 224
	cmp	w0, 224
	ccmp	w0, w1, 4, ne
	beq	.L3519
	ldr	x0, [x25, 24]
	ldr	w0, [x0, 4]
	bl	lpa_hash_get_ppa
	str	w0, [x29, 204]
	cmn	w0, #1
	bne	.L3526
	ldr	x0, [x25, 24]
	mov	w2, 0
	add	x1, x29, 204
	ldr	w0, [x0, 4]
	bl	pm_log2phys
.L3526:
	ldr	w0, [x29, 204]
	cmp	w26, w0
	bne	.L3527
	ldr	w0, [x29, 172]
	mov	w1, w26
	add	w0, w0, 1
	str	w0, [x29, 172]
	ldr	x0, [x25, 24]
	ldr	w3, [x29, 172]
	ldr	w2, [x0, 4]
	adrp	x0, .LC258
	add	x0, x0, :lo12:.LC258
	bl	printk
.L3527:
	ldr	x0, [x29, 152]
	cbz	x0, .L3529
	ldr	x0, [x29, 176]
	ubfiz	x3, x0, 2, 32
	ldr	x0, [x29, 152]
	ldr	w2, [x0, x3]
	ldr	x0, [x25, 24]
	ldr	w0, [x0, 4]
	cmp	w0, w2
	beq	.L3530
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3530
	ldr	w1, [x29, 176]
	adrp	x0, .LC259
	str	x3, [x29, 112]
	add	x0, x0, :lo12:.LC259
	bl	printk
	ldr	x3, [x29, 112]
.L3530:
	ldr	x1, [x25, 24]
	ldr	x0, [x29, 152]
	ldr	w1, [x1, 4]
	ldr	w0, [x0, x3]
	cmp	w1, w0
	beq	.L3529
	cmn	w0, #1
	beq	.L3529
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 944
	mov	w2, 1575
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3529:
	ldr	w0, [x29, 176]
	add	w0, w0, 1
	str	w0, [x29, 176]
.L3519:
	add	w21, w21, 1
	and	w21, w21, 65535
.L3534:
	cmp	w24, w21
	bcs	.L3532
	add	w22, w22, 1
	and	w22, w22, 65535
.L3536:
	ldrb	w0, [x29, 217]
	cmp	w0, w22
	bls	.L3533
	sxtw	x0, w22
	mov	w21, 1
	add	x0, x0, 8
	lsl	x0, x0, 1
	str	x0, [x29, 128]
	b	.L3534
.L3523:
	add	w1, w19, w1
	orr	w26, w8, w1
	b	.L3522
.L3520:
	cmp	w24, 2
	bne	.L3524
	ldrb	w8, [x2, 1320]
	ldr	w1, [x29, 144]
	b	.L3548
.L3524:
	add	w26, w19, w1
	b	.L3522
.L3533:
	add	w19, w19, 1
	and	w19, w19, 65535
	b	.L3518
.L3535:
	mov	x0, x25
	bl	zbuf_free
	ldr	x0, [x29, 160]
	ldr	x1, [x29, 136]
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w3, [x29, 172]
	ldr	x0, [x0, 1120]
	ldrh	w2, [x0, x1, lsl 1]
	mov	w1, w20
	adrp	x0, .LC260
	add	x0, x0, :lo12:.LC260
	bl	printk
	ldr	w0, [x29, 168]
.L3515:
	ldp	x19, x20, [sp, 64]
	ldp	x21, x22, [sp, 80]
	ldp	x23, x24, [sp, 96]
	ldp	x25, x26, [sp, 112]
	ldp	x27, x28, [sp, 128]
	ldp	x29, x30, [sp, 48]
	add	sp, sp, 288
	ret
.L3537:
	mov	w0, 0
	b	.L3515
	.size	ftl_sblk_dump, .-ftl_sblk_dump
	.align	2
	.global	zftl_read
	.type	zftl_read, %function
zftl_read:
	sub	sp, sp, #176
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	mov	w20, w0
	adrp	x0, .LANCHOR2
	stp	x23, x24, [sp, 64]
	stp	x21, x22, [sp, 48]
	mov	w19, w1
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	mov	w24, w2
	stp	x25, x26, [sp, 80]
	mov	x23, x3
	stp	x27, x28, [sp, 96]
	tbz	x0, 12, .L3550
	mov	w3, w2
	adrp	x0, .LC261
	mov	w2, w1
	add	x0, x0, :lo12:.LC261
	mov	w1, w20
	bl	printk
.L3550:
	cbnz	w20, .L3551
	adrp	x0, .LANCHOR0+1032
	mov	w20, 24576
	ldr	w0, [x0, #:lo12:.LANCHOR0+1032]
.L3552:
	cmp	w0, w19
	ccmp	w0, w24, 0, hi
	bcc	.L3585
	add	w1, w19, w24
	cmp	w0, w1
	bcc	.L3585
	add	w20, w20, w19
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldr	x1, [x0, 3384]
	ldr	w0, [x1, 24]
	add	w0, w0, w24
	str	w0, [x1, 24]
	add	x0, x21, :lo12:.LANCHOR3
	add	w1, w24, w20
	stp	w1, wzr, [x29, 136]
	sub	w1, w1, #1
	ldrb	w0, [x0, 1946]
	udiv	w26, w20, w0
	udiv	w0, w1, w0
	mov	w22, w26
	sub	w25, w0, w26
	str	w0, [x29, 132]
	add	w25, w25, 1
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 960
	str	x0, [x29, 112]
.L3554:
	cbnz	w25, .L3582
	bl	timer_get_time
	adrp	x1, .LANCHOR5+468
	str	w0, [x1, #:lo12:.LANCHOR5+468]
	ldr	w0, [x29, 140]
.L3549:
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 176
	ret
.L3551:
	cmp	w20, 3
	bhi	.L3585
	lsl	w20, w20, 13
	mov	w0, 8192
	b	.L3552
.L3582:
	add	x0, x21, :lo12:.LANCHOR3
	cmp	w22, w26
	ldrb	w1, [x0, 1946]
	ldr	w0, [x29, 132]
	and	w27, w1, 65535
	ccmp	w22, w0, 4, ne
	bne	.L3586
	cmp	w22, w26
	bne	.L3556
	udiv	w3, w20, w1
	and	w0, w24, 65535
	msub	w1, w3, w1, w20
	and	w28, w1, 65535
	sub	w27, w27, w28
	and	w27, w27, 65535
	cmp	w24, w27
	csel	w27, w0, w27, cc
.L3555:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 1306
.L3559:
	ldr	w2, [x1, 34]
	cmp	w22, w2
	bne	.L3557
	ldrb	w2, [x1]
	tbz	x2, 3, .L3557
	add	x1, x19, :lo12:.LANCHOR0
	ubfiz	x0, x0, 6, 32
	add	x0, x1, x0
	lsl	w2, w27, 9
	ubfiz	x27, x27, 9, 16
	ubfiz	x28, x28, 9, 16
	ldr	x1, [x0, 1312]
	mov	x0, x23
	add	x23, x23, x27
	add	x1, x1, x28
	bl	ftl_memcpy
.L3558:
	add	w22, w22, 1
	sub	w25, w25, #1
.L3565:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 3353]
	cmp	w0, 2
	bls	.L3566
	cbnz	w25, .L3554
.L3566:
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w1, [x2, 1957]
	cbz	w1, .L3554
	ldrb	w2, [x2, 1956]
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x2, lsl 6
	bl	sblk_read_page
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x0, [x29, 120]
.L3568:
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1957]
	cbnz	w1, .L3581
	mov	w1, -1
	strb	wzr, [x0, 1957]
	strb	w1, [x0, 1956]
	b	.L3554
.L3556:
	ldr	w0, [x29, 136]
	msub	w27, w1, w22, w0
	and	w27, w27, 255
.L3586:
	mov	w28, 0
	b	.L3555
.L3557:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L3559
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29, 156]
	cmn	w0, #1
	bne	.L3560
	mov	w2, 0
	add	x1, x29, 156
	mov	w0, w22
	bl	pm_log2phys
.L3560:
	ldr	w0, [x29, 156]
	cmn	w0, #1
	bne	.L3561
	add	x28, x21, :lo12:.LANCHOR3
	mov	w27, 0
.L3562:
	ldrb	w0, [x28, 1946]
	cmp	w27, w0
	bcs	.L3558
	madd	w0, w22, w0, w27
	cmp	w20, w0
	bhi	.L3563
	ldr	w1, [x29, 136]
	cmp	w1, w0
	bls	.L3563
	mov	x0, x23
	add	x23, x23, 512
	mov	w2, 512
	mov	w1, 0
	bl	ftl_memset
.L3563:
	add	w27, w27, 1
	b	.L3562
.L3561:
	mov	w0, 0
	bl	buf_alloc
	cbz	x0, .L3565
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x4, [x2, 3384]
	ldr	w2, [x4, 40]
	add	w2, w2, 1
	str	w2, [x4, 40]
	ldr	w2, [x29, 156]
	strb	w27, [x0, 56]
	ubfiz	x27, x27, 9, 16
	str	x23, [x0, 16]
	add	x23, x23, x27
	strb	w28, [x0, 57]
	stp	w22, w2, [x0, 36]
	str	w2, [x0, 44]
	bl	zftl_add_read_buf
	b	.L3558
.L3581:
	ldrb	w0, [x0, 1956]
	cmp	w0, 255
	bne	.L3569
	ldp	x1, x0, [x29, 112]
	mov	w2, 1284
	bl	printk
	bl	dump_stack
.L3569:
	add	x0, x21, :lo12:.LANCHOR3
	add	x4, x19, :lo12:.LANCHOR0
	add	x1, x4, 1304
	ldrb	w8, [x0, 1956]
	ubfiz	x27, x8, 6, 8
	add	x28, x1, x27
	sxtw	x27, w8
	lsl	x3, x27, 6
	add	x2, x1, x3
	ldrb	w1, [x1, x3]
	ldr	w9, [x2, 52]
	strb	w1, [x0, 1956]
	cmn	w9, #1
	bne	.L3570
	ldr	x1, [x4, 1128]
	str	w9, [x29, 140]
	ldr	w0, [x1, 552]
	add	w0, w0, 1
	str	w0, [x1, 552]
.L3571:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x27, lsl 6
	ldr	x3, [x0, 24]
	ldr	w0, [x0, 36]
	ldr	w1, [x3, 4]
	cmp	w1, w0
	bne	.L3572
	cmn	w9, #1
	bne	.L3573
.L3572:
	add	x0, x21, :lo12:.LANCHOR3
	add	x5, x19, :lo12:.LANCHOR0
	ldrb	w4, [x0, 1946]
	add	x0, x5, 1304
	add	x0, x0, x27, lsl 6
	ldrb	w6, [x5, 1205]
	mov	w5, 1
	ldrb	w1, [x0, 56]
	lsl	w5, w5, w6
	ldp	x2, x7, [x0, 8]
	cmp	w1, w4
	sub	w5, w5, #1
	ldr	w1, [x0, 40]
	mov	w0, 24
	sub	w0, w0, w6
	csel	x2, x7, x2, cs
	lsl	w6, w5, w0
	lsr	w0, w1, w0
	bic	w1, w1, w6
	and	w0, w0, w5
	bl	flash_read_page_en
	mov	w9, w0
.L3573:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x27, lsl 6
	ldr	x1, [x0, 24]
	ldr	w0, [x0, 36]
	ldr	w1, [x1, 4]
	cmp	w1, w0
	bne	.L3575
	cmn	w9, #1
	bne	.L3576
.L3575:
	add	x11, x19, :lo12:.LANCHOR0
	str	w9, [x29, 128]
	add	x10, x11, 1304
	add	x10, x10, x27, lsl 6
	stp	x11, x10, [x29, 96]
	ldr	x1, [x11, 1128]
	ldr	w0, [x1, 552]
	add	w0, w0, 1
	str	w0, [x1, 552]
	ldr	x0, [x10, 24]
	ldrb	w1, [x10, 1]
	ldr	w2, [x0, 12]
	str	w2, [sp]
	mov	w2, w9
	ldp	w3, w4, [x10, 36]
	ldp	w5, w6, [x0]
	ldr	w7, [x0, 8]
	adrp	x0, .LC263
	add	x0, x0, :lo12:.LC263
	bl	printk
	ldp	x11, x10, [x29, 96]
	add	x2, x21, :lo12:.LANCHOR3
	mov	w0, 24
	ldrb	w1, [x11, 1205]
	ldrh	w3, [x2, 1304]
	sub	w0, w0, w1
	ldrb	w2, [x2, 1306]
	sub	w1, w0, w3
	mov	w0, 1
	lsl	w0, w0, w1
	ldr	w1, [x10, 40]
	sub	w0, w0, #1
	lsr	w1, w1, w3
	and	w0, w0, w1
	mov	x1, 0
	udiv	w0, w0, w2
	bl	ftl_sblk_dump
	ldr	w9, [x29, 128]
.L3576:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x0, x0, x27, lsl 6
	ldr	x1, [x0, 24]
	ldr	w0, [x0, 36]
	ldr	w1, [x1, 4]
	cmp	w1, w0
	bne	.L3577
	cmn	w9, #1
	bne	.L3578
.L3577:
	ldp	x1, x0, [x29, 112]
	mov	w2, 1320
	bl	printk
	bl	dump_stack
.L3578:
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x8, x0, x27, lsl 6
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w2, [x8, 56]
	ldrb	w0, [x0, 1946]
	cmp	w0, w2
	bls	.L3579
	ldr	x0, [x8, 8]
	lsl	w2, w2, 9
	ldrb	w1, [x8, 57]
	add	x1, x0, x1, lsl 9
	ldr	x0, [x8, 16]
	bl	ftl_memcpy
.L3580:
	mov	x1, x28
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 3355
	bl	buf_remove_buf
	mov	x0, x28
	bl	zbuf_free
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w0, [x1, 1957]
	sub	w0, w0, #1
	strb	w0, [x1, 1957]
	b	.L3568
.L3570:
	cmp	w9, 256
	bne	.L3571
	ldrb	w5, [x4, 1205]
	mov	w4, 24
	ldrh	w1, [x0, 1304]
	sub	w4, w4, w5
	ldr	w3, [x2, 40]
	sub	w4, w4, w1
	mov	w5, 1
	ldrb	w0, [x0, 1306]
	lsl	w4, w5, w4
	sub	w4, w4, #1
	lsr	w1, w3, w1
	and	w4, w4, w1
	ldr	w2, [x2, 36]
	str	w9, [x29, 96]
	udiv	w4, w4, w0
	str	w5, [x29, 104]
	adrp	x0, .LC262
	add	x0, x0, :lo12:.LC262
	and	w1, w4, 65535
	str	w4, [x29, 128]
	bl	printk
	ldr	w5, [x29, 104]
	mov	w2, 0
	ldr	w4, [x29, 128]
	mov	w1, w5
	mov	w0, w4
	bl	gc_add_sblk
	ldr	w9, [x29, 96]
	b	.L3571
.L3579:
	ldrb	w0, [x8, 2]
	and	w0, w0, -9
	strb	w0, [x8, 2]
	b	.L3580
.L3585:
	mov	w0, -1
	b	.L3549
	.size	zftl_read, .-zftl_read
	.align	2
	.global	zftl_vendor_read
	.type	zftl_vendor_read, %function
zftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	add	w1, w0, 512
	add	x29, sp, 0
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
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
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
	stp	x29, x30, [sp, 32]
	add	x29, sp, 32
	stp	x19, x20, [sp, 48]
	add	x19, x29, 112
	stp	x21, x22, [sp, 64]
	stp	x23, x24, [sp, 80]
	stp	x25, x26, [sp, 96]
	stp	x27, x28, [sp, 112]
	str	x19, [x29, 104]
	bhi	.L3623
	mov	x21, x2
	mov	x0, x19
	bl	rk_copy_from_user
	mov	x1, -14
	cbnz	x0, .L3605
	mov	x1, x19
	strb	wzr, [x19, x21]
	adrp	x0, .LC264
	add	x0, x0, :lo12:.LC264
	bl	printk
	mov	x1, x19
	adrp	x0, .LC265
	add	x0, x0, :lo12:.LC265
	mov	w3, 16
	mov	w2, 1
	bl	rknand_print_hex
	bl	rknand_device_lock
	mov	x2, 7
	adrp	x1, .LC266
	mov	x0, x19
	add	x1, x1, :lo12:.LC266
	bl	memcmp
	mov	w22, w0
	cbnz	w0, .L3607
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR5
	add	x20, x20, :lo12:.LANCHOR5
	mov	w2, 4
	adrp	x0, .LC267
	ldr	x1, [x19, 3384]
	add	x0, x0, :lo12:.LC267
	mov	w23, 65535
	add	x1, x1, 704
	ldrh	w3, [x1, -6]
	bl	rknand_print_hex
	ldrh	w3, [x20, 220]
	adrp	x0, .LC268
	ldr	x1, [x19, 3384]
	mov	w2, 2
	add	x0, x0, :lo12:.LC268
	add	x1, x1, 416
	bl	rknand_print_hex
.L3608:
	ldrh	w0, [x20, 220]
	cmp	w22, w0
	blt	.L3610
.L3611:
	bl	rknand_device_unlock
	mov	x1, x21
.L3605:
	ldp	x19, x20, [sp, 48]
	mov	x0, x1
	ldp	x21, x22, [sp, 64]
	ldp	x23, x24, [sp, 80]
	ldp	x25, x26, [sp, 96]
	ldp	x27, x28, [sp, 112]
	ldp	x29, x30, [sp, 32]
	add	sp, sp, 224
	ret
.L3610:
	mov	w0, 300
	bl	msleep
	ldr	x0, [x19, 3384]
	add	x0, x0, w22, sxtw 1
	ldrh	w0, [x0, 416]
	cmp	w0, w23
	beq	.L3609
	mov	x1, 0
	bl	ftl_sblk_dump
.L3609:
	add	w22, w22, 1
	b	.L3608
.L3607:
	adrp	x1, .LC269
	mov	x2, 7
	add	x1, x1, :lo12:.LC269
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3612
	adrp	x22, .LANCHOR0
	add	x20, x22, :lo12:.LANCHOR0
	mov	w2, 4
	adrp	x0, .LC267
	add	x0, x0, :lo12:.LC267
	adrp	x25, .LC271
	ldr	x1, [x20, 3384]
	adrp	x24, .LC272
	add	x25, x25, :lo12:.LC271
	add	x24, x24, :lo12:.LC272
	add	x1, x1, 704
	ldrh	w3, [x1, -6]
	bl	rknand_print_hex
	adrp	x0, .LANCHOR5+220
	ldr	x1, [x20, 3384]
	mov	w2, 2
	add	x20, x29, 192
	ldrh	w3, [x0, #:lo12:.LANCHOR5+220]
	add	x1, x1, 416
	adrp	x0, .LC268
	add	x0, x0, :lo12:.LC268
	bl	rknand_print_hex
	mov	w0, 50
	bl	msleep
	add	x1, x19, 7
	adrp	x0, .LC270
	str	x1, [x20, -88]!
	add	x0, x0, :lo12:.LC270
	bl	printk
	ldr	x0, [x29, 104]
	mov	x1, x20
	adrp	x20, .LANCHOR3
	bl	rk_simple_strtoull.constprop.33
	mov	w23, w0
	add	x1, x20, :lo12:.LANCHOR3
	and	w27, w0, 65535
	add	x19, x1, 1960
	add	x26, x1, 2472
	mov	x28, x1
	str	w0, [x29, 100]
.L3614:
	ldrh	w2, [x19, 2]
	mov	x0, x25
	ldrh	w1, [x19]
	bl	printk
	ldrh	w0, [x19]
	cmp	w0, w27
	bne	.L3613
	ldrb	w3, [x28, 1946]
	mov	w2, 4
	ldr	x1, [x19, 8]
	mov	x0, x24
	lsl	w3, w3, 7
	bl	rknand_print_hex
	mov	w0, 50
	bl	msleep
.L3613:
	add	x19, x19, 16
	cmp	x19, x26
	bne	.L3614
	add	x22, x22, :lo12:.LANCHOR0
	mov	w0, 300
	bl	msleep
	add	x20, x20, :lo12:.LANCHOR3
	mov	w0, 1
	bl	buf_alloc
	ldr	x1, [x22, 3384]
	mov	x19, x0
	add	x1, x1, w23, uxth 2
	ldr	w1, [x1, 704]
	str	w1, [x0, 40]
	str	w1, [x29, 100]
	mov	w1, 1
	bl	sblk_read_page
	ldr	x1, [x19, 24]
	ldr	x0, [x19, 8]
	ldr	w2, [x1, 12]
	str	w2, [sp, 16]
	ldr	w2, [x1, 8]
	str	w2, [sp, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC245
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC245
	ldr	w2, [x19, 52]
	ldr	w1, [x29, 100]
	bl	printk
	ldrb	w3, [x20, 1946]
	adrp	x0, .LC273
	ldr	x1, [x19, 8]
	add	x0, x0, :lo12:.LC273
	mov	w2, 4
	lsl	w3, w3, 7
.L3630:
	bl	rknand_print_hex
	mov	x0, x19
	bl	zbuf_free
	b	.L3611
.L3612:
	adrp	x1, .LC274
	mov	x2, 7
	add	x1, x1, :lo12:.LC274
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3615
	bl	dump_ftl_info
	b	.L3611
.L3615:
	adrp	x1, .LC275
	mov	x2, 9
	add	x1, x1, :lo12:.LC275
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3616
	add	x1, x29, 192
	add	x0, x19, 9
	str	x0, [x1, -88]!
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29, 100]
	adrp	x1, .LANCHOR3+1408
	strh	w0, [x1, #:lo12:.LANCHOR3+1408]
	bl	dump_all_list_info
	b	.L3611
.L3616:
	adrp	x1, .LC276
	mov	x2, 8
	add	x1, x1, :lo12:.LC276
	mov	x0, x19
	bl	memcmp
	cbz	w0, .L3611
	adrp	x1, .LC277
	mov	x2, 8
	add	x1, x1, :lo12:.LC277
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3618
	add	x20, x29, 192
	add	x1, x19, 8
	adrp	x0, .LC270
	add	x0, x0, :lo12:.LC270
	str	x1, [x20, -88]!
	bl	printk
	ldr	x0, [x29, 104]
	mov	x1, x20
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29, 100]
	mov	w0, 1
	bl	buf_alloc
	ldr	w1, [x29, 100]
	mov	x19, x0
	str	w1, [x0, 40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	x1, [x19, 24]
	ldr	x0, [x19, 8]
	ldr	w2, [x1, 12]
	str	w2, [sp, 16]
	ldr	w2, [x1, 8]
	str	w2, [sp, 8]
	ldr	w2, [x1, 4]
	str	w2, [sp]
	ldp	w3, w4, [x0]
	ldp	w5, w6, [x0, 8]
	adrp	x0, .LC245
	ldr	w7, [x1]
	add	x0, x0, :lo12:.LC245
	ldr	w2, [x19, 52]
	ldr	w1, [x29, 100]
	bl	printk
	ldr	x1, [x19, 8]
	mov	w2, 4
	ldrb	w3, [x20, 1946]
	adrp	x0, .LC218
	add	x0, x0, :lo12:.LC218
	lsl	w3, w3, 7
	bl	rknand_print_hex
	ldrb	w3, [x20, 1946]
	adrp	x0, .LC240
	mov	w2, 4
	add	x0, x0, :lo12:.LC240
	ldr	x1, [x19, 24]
	lsl	w3, w3, 1
	b	.L3630
.L3618:
	adrp	x1, .LC278
	mov	x2, 8
	add	x1, x1, :lo12:.LC278
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3619
	add	x20, x29, 192
	add	x1, x19, 8
	adrp	x0, .LC270
	add	x0, x0, :lo12:.LC270
	str	x1, [x20, -88]!
	bl	printk
	ldr	x0, [x29, 104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29, 100]
	mov	x1, 0
	bl	ftl_sblk_dump
	b	.L3611
.L3619:
	adrp	x1, .LC279
	mov	x2, 10
	add	x1, x1, :lo12:.LC279
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3620
	add	x20, x29, 192
	add	x1, x19, 10
	adrp	x0, .LC270
	add	x0, x0, :lo12:.LC270
	str	x1, [x20, -88]!
	bl	printk
	ldr	x0, [x29, 104]
	mov	x1, x20
	bl	rk_simple_strtoull.constprop.33
	str	w0, [x29, 100]
	adrp	x1, .LANCHOR2
	str	w0, [x1, #:lo12:.LANCHOR2]
	b	.L3611
.L3620:
	adrp	x1, .LC280
	mov	x2, 8
	add	x1, x1, :lo12:.LC280
	mov	x0, x19
	bl	memcmp
	cbnz	w0, .L3621
	add	x1, x19, 8
	add	x19, x29, 192
	adrp	x0, .LC270
	add	x0, x0, :lo12:.LC270
	str	x1, [x19, -88]!
	bl	printk
	ldr	x0, [x29, 104]
	mov	x1, x19
	bl	rk_simple_strtoull.constprop.33
	mov	w19, w0
	bl	lpa_hash_get_ppa
	str	w0, [x29, 100]
	cmn	w0, #1
	bne	.L3622
	mov	w2, 0
	add	x1, x29, 100
	mov	w0, w19
	bl	pm_log2phys
.L3622:
	ldr	w2, [x29, 100]
	mov	w1, w19
	adrp	x0, .LC281
	add	x0, x0, :lo12:.LC281
	bl	printk
	b	.L3611
.L3621:
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
	b	.L3611
.L3623:
	mov	x1, -22
	b	.L3605
	.size	zftl_debug_proc_write, .-zftl_debug_proc_write
	.align	2
	.global	ftl_update_l2p_map
	.type	ftl_update_l2p_map, %function
ftl_update_l2p_map:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	stp	x25, x26, [sp, 64]
	adrp	x26, .LANCHOR3
	add	x0, x26, :lo12:.LANCHOR3
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x27, x28, [sp, 80]
	ldrh	w23, [x0, 1376]
	ldrb	w1, [x22, 9]
	ldr	x0, [x0, 1928]
	ldrh	w19, [x22, 12]
	mul	w23, w23, w1
	add	x19, x0, x19, lsl 2
	add	x0, x19, w23, sxtw 2
	ldr	w0, [x0, -4]
	cmn	w0, #1
	beq	.L3632
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 976
	mov	w2, 1998
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3632:
	adrp	x27, .LC292
	mov	w21, 0
	add	x0, x27, :lo12:.LC292
	mov	w25, 0
	str	x0, [x29, 104]
.L3633:
	cmp	w25, w23
	bne	.L3639
	adrp	x0, .LANCHOR2
	adrp	x19, .LANCHOR0
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L3640
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22]
	ldr	x0, [x0, 1120]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x0, x2]
	adrp	x0, .LC294
	mov	w2, w21
	add	x0, x0, :lo12:.LC294
	bl	printk
.L3640:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x22]
	ldr	x0, [x19, 1120]
	strh	w21, [x0, x1, lsl 1]
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
.L3639:
	ldr	w2, [x19]
	cmn	w2, #1
	beq	.L3634
	add	x0, x26, :lo12:.LANCHOR3
	ldrb	w20, [x0, 1946]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	lsl	w20, w20, 7
	udiv	w20, w2, w20
	and	w20, w20, 65535
	tbz	x0, 12, .L3635
	adrp	x0, .LC291
	mov	w3, w25
	mov	w1, w20
	add	x0, x0, :lo12:.LC291
	bl	printk
.L3635:
	adrp	x28, .LC293
	mov	x24, x19
	mov	w27, w25
	add	x28, x28, :lo12:.LC293
.L3638:
	ldr	w0, [x24]
	cmn	w0, #1
	beq	.L3636
	add	x5, x26, :lo12:.LANCHOR3
	ldrb	w1, [x5, 1946]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w20, w0, uxth
	bne	.L3636
	ldrb	w0, [x22, 9]
	str	x5, [x29, 96]
	sdiv	w1, w27, w0
	msub	w0, w1, w0, w27
	add	x0, x22, w0, sxtw 1
	ldrh	w2, [x0, 16]
	ldrh	w0, [x5, 1410]
	madd	w2, w2, w0, w1
	ldr	x0, [x29, 104]
	str	w2, [x29, 124]
	ldr	w1, [x24]
	bl	pm_ppa_update_check
	ldr	x5, [x29, 96]
	cbz	w0, .L3637
	ldr	x1, [x5, 1928]
	mov	w3, w23
	mov	w2, 4
	mov	x0, x28
	bl	rknand_print_hex
.L3637:
	ldr	w0, [x24]
	add	w21, w21, 1
	mov	w2, 1
	add	x1, x29, 124
	and	w21, w21, 65535
	bl	pm_log2phys
	mov	w0, -1
	str	w0, [x24]
.L3636:
	add	w27, w27, 1
	add	x24, x24, 4
	cmp	w23, w27
	bne	.L3638
.L3634:
	add	w25, w25, 1
	add	x19, x19, 4
	b	.L3633
	.size	ftl_update_l2p_map, .-ftl_update_l2p_map
	.align	2
	.global	ftl_alloc_new_data_sblk
	.type	ftl_alloc_new_data_sblk, %function
ftl_alloc_new_data_sblk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x0
	str	x21, [sp, 32]
	ldrh	w21, [x0]
	bl	ftl_update_l2p_map
	bl	pm_flush
	ldrh	w0, [x20]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L3662
	bl	zftl_insert_data_list
.L3662:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19, 1128]
	add	x0, x0, 16
	cmp	x20, x0
	mov	x0, x20
	cset	w1, ne
	add	w1, w1, 2
	bl	ftl_open_sblk_init
	ldr	x0, [x19, 1128]
	ldr	w0, [x0, 560]
	cmp	w0, w21
	bne	.L3664
	mov	w20, 65535
	cmp	w21, w20
	beq	.L3664
	mov	w1, w21
	adrp	x0, .LC295
	add	x0, x0, :lo12:.LC295
	bl	printk
	ldr	x0, [x19, 1128]
	ldr	w0, [x0, 564]
	bl	gc_mark_bad_ppa
	ldr	x0, [x19, 1128]
	mov	w1, -1
	str	w20, [x0, 560]
	str	w1, [x0, 564]
.L3664:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	lpa_rebuild_hash
	ldr	x21, [sp, 32]
	mov	w0, 0
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_alloc_new_data_sblk, .-ftl_alloc_new_data_sblk
	.align	2
	.global	ftl_write_commit
	.type	ftl_write_commit, %function
ftl_write_commit:
	stp	x29, x30, [sp, -144]!
	adrp	x0, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	add	x20, x0, :lo12:.LANCHOR0
	stp	x27, x28, [sp, 80]
	add	x28, x20, 1304
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
.L3668:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0, 3381]
	cbz	w1, .L3670
	ldrb	w3, [x0, 3408]
	add	x2, x0, 1304
	sub	w1, w1, #1
	strb	w1, [x0, 3381]
	ubfiz	x4, x3, 6, 8
	add	x4, x2, x4
	stp	x3, x4, [x29, 104]
	lsl	x3, x3, 6
	add	x4, x2, x3
	ldrb	w2, [x2, x3]
	strb	w2, [x0, 3408]
	ldr	w1, [x4, 36]
	ldr	w0, [x0, 3364]
	cmp	w1, w0
	bcc	.L3672
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1000
	mov	w2, 607
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3672:
	ldr	x0, [x29, 104]
	ldr	w1, [x20, 3364]
	add	x0, x28, x0, lsl 6
	ldr	w23, [x0, 36]
	cmp	w23, w1
	bcc	.L3673
	ldr	x0, [x29, 112]
	bl	zbuf_free
	mov	w0, -1
.L3667:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 144
	ret
.L3673:
	ldrb	w22, [x0, 57]
	ldrb	w21, [x0, 56]
	ldr	x27, [x0, 8]
	ldr	x24, [x0, 24]
	ldrb	w0, [x20, 3380]
	cbz	w0, .L3675
	adrp	x0, .LANCHOR5+360
	ldrb	w19, [x0, #:lo12:.LANCHOR5+360]
	add	x19, x28, x19, lsl 6
.L3676:
	ldrb	w0, [x19]
	cmp	w0, 255
	bne	.L3677
	ldr	w0, [x19, 36]
	cmp	w23, w0
	bne	.L3675
	ldr	x0, [x19, 8]
	ubfiz	x22, x22, 9, 8
	lsl	w2, w21, 9
	add	x1, x27, x22
	add	x0, x0, x22
	bl	ftl_memcpy
	adrp	x0, .LANCHOR3+1946
	ldr	x2, [x19, 8]
	ldr	x1, [x19, 24]
	ldrb	w0, [x0, #:lo12:.LANCHOR3+1946]
	add	x1, x1, 16
	sub	w0, w0, #2
	sbfiz	x0, x0, 9, 32
	add	x2, x2, x0
	mov	w0, 2
	bl	ftl_debug_info_fill
	ldr	x0, [x29, 112]
	bl	zbuf_free
	b	.L3668
.L3677:
	ubfiz	x19, x0, 6, 8
	add	x19, x28, x19
	b	.L3676
.L3675:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29, 140]
	cmn	w0, #1
	bne	.L3679
	mov	w2, 0
	add	x1, x29, 140
	mov	w0, w23
	bl	pm_log2phys
.L3679:
	ldr	x19, [x20, 1128]
	add	x0, x20, 1306
	ldr	w2, [x29, 140]
	add	x4, x20, 3354
	add	x19, x19, 16
	mov	w3, 0
	mov	x26, 0
.L3681:
	ldr	w1, [x0, 34]
	cmp	w23, w1
	bne	.L3680
	ldrb	w1, [x0]
	tbz	x1, 3, .L3680
	ldr	w2, [x0, 38]
	and	w1, w1, -9
	ldr	x26, [x0, 6]
	mov	w3, 1
	strb	w1, [x0]
.L3680:
	add	x0, x0, 64
	cmp	x0, x4
	bne	.L3681
	cbz	w3, .L3682
	str	w2, [x29, 140]
.L3682:
	adrp	x25, .LANCHOR3
	add	x0, x25, :lo12:.LANCHOR3
	str	x25, [x29, 120]
	ldrb	w0, [x0, 1946]
	cmp	w21, w0
	bcs	.L3708
	add	w21, w22, w21
	cbz	x26, .L3684
	cbz	w22, .L3685
	lsl	w2, w22, 9
	mov	x1, x26
	mov	x0, x27
	bl	ftl_memcpy
	ldr	x19, [x20, 1128]
	add	x19, x19, 48
.L3685:
	ldr	x0, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w2, [x0, 1946]
	cmp	w21, w2
	bcc	.L3686
	ldr	x19, [x20, 1128]
	add	x19, x19, 16
.L3708:
	mov	w26, 0
	b	.L3683
.L3686:
	ubfiz	x0, x21, 9, 9
	sub	w2, w2, w21
	add	x1, x26, x0
	lsl	w2, w2, 9
	add	x0, x27, x0
	bl	ftl_memcpy
	b	.L3708
.L3684:
	ldr	w0, [x29, 140]
	cmn	w0, #1
	beq	.L3687
	mov	w0, 1
	bl	buf_alloc
	ldr	w1, [x29, 140]
	mov	x25, x0
	stp	w23, w1, [x0, 36]
	mov	w1, 1
	bl	sblk_read_page
	ldr	x3, [x25, 24]
	ldr	w0, [x3, 4]
	ldr	w26, [x3, 12]
	cmp	w23, w0
	add	w26, w26, 1
	bne	.L3688
	ldr	w0, [x25, 52]
	cmn	w0, #1
	bne	.L3689
.L3688:
	ldrb	w2, [x20, 1205]
	mov	w6, 1
	mov	w0, 24
	ldr	w1, [x25, 40]
	sub	w0, w0, w2
	lsl	w6, w6, w2
	ldr	x2, [x29, 120]
	sub	w6, w6, #1
	add	x2, x2, :lo12:.LANCHOR3
	lsl	w7, w6, w0
	lsr	w0, w1, w0
	bic	w1, w1, w7
	ldrb	w4, [x2, 1946]
	and	w0, w0, w6
	ldr	x2, [x25, 8]
	bl	flash_read_page_en
	str	w0, [x25, 52]
.L3689:
	ldr	x0, [x25, 24]
	ldr	w0, [x0, 4]
	cmp	w23, w0
	bne	.L3690
	ldr	w0, [x25, 52]
	cmn	w0, #1
	bne	.L3691
.L3690:
	ldr	x1, [x20, 1128]
	mov	w3, w23
	ldr	w2, [x29, 140]
	ldr	w0, [x1, 552]
	add	w0, w0, 1
	str	w0, [x1, 552]
	adrp	x0, .LC296
	add	x0, x0, :lo12:.LC296
	ldrb	w1, [x25, 1]
	ldr	w4, [x25, 52]
	bl	printk
	ldr	x1, [x25, 24]
	mov	w3, 4
	adrp	x0, .LC240
	mov	w2, w3
	add	x0, x0, :lo12:.LC240
	bl	rknand_print_hex
.L3691:
	ldr	x0, [x25, 24]
	ldr	w0, [x0, 4]
	cmp	w23, w0
	bne	.L3692
	ldr	w0, [x25, 52]
	cmn	w0, #1
	bne	.L3693
.L3692:
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1000
	mov	w2, 699
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3693:
	cbz	w22, .L3694
	ldr	w0, [x29, 140]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L3695
	ldr	x1, [x25, 8]
	mov	x0, x27
	bl	ftl_memcpy
.L3696:
	ldr	x19, [x20, 1128]
	add	x19, x19, 48
.L3694:
	ldr	x0, [x29, 120]
	add	x0, x0, :lo12:.LANCHOR3
	ldrb	w2, [x0, 1946]
	cmp	w21, w2
	bcc	.L3697
	bls	.L3698
	ldr	x19, [x20, 1128]
	add	x19, x19, 16
.L3698:
	cbz	x25, .L3683
	ldrb	w0, [x25, 2]
	mov	x1, x25
	and	w0, w0, -9
	strb	w0, [x25, 2]
	add	x0, x20, 3355
	bl	buf_remove_buf
	mov	x0, x25
	bl	zbuf_free
.L3683:
	ldrh	w0, [x19, 6]
	cbnz	w0, .L3700
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3700:
	mov	x0, x19
	bl	ftl_get_new_free_page
	mov	w3, w0
	ldr	x0, [x29, 104]
	mov	x1, x24
	str	w3, [x29, 100]
	lsl	x22, x0, 6
	add	x21, x28, x22
	ldr	w0, [x21, 32]
	stp	w0, w23, [x24]
	str	w26, [x24, 12]
	ldr	w0, [x29, 140]
	str	w0, [x24, 8]
	ldr	x0, [x29, 120]
	str	wzr, [x1, 16]!
	add	x25, x0, :lo12:.LANCHOR3
	mov	w0, 2
	ldrb	w2, [x25, 1946]
	sub	w2, w2, #2
	sbfiz	x2, x2, 9, 32
	add	x2, x27, x2
	bl	ftl_debug_info_fill
	ldr	w0, [x29, 140]
	mov	w1, 10
	ldr	w3, [x29, 100]
	stp	w3, w0, [x21, 40]
	mov	w0, -1
	strb	w0, [x28, x22]
	ldrb	w0, [x21, 2]
	orr	w0, w0, w1
	strb	w0, [x21, 2]
	ldrh	w1, [x19, 12]
	ldrh	w0, [x19, 10]
	add	w0, w0, w1
	ldr	x1, [x29, 112]
	sub	w0, w0, #1
	strh	w0, [x21, 48]
	adrp	x21, .LANCHOR5
	add	x22, x21, :lo12:.LANCHOR5
	add	x0, x22, 360
	bl	buf_add_tail
	ldrb	w0, [x20, 3380]
	add	w0, w0, 1
	strb	w0, [x20, 3380]
	bl	timer_get_time
	str	w0, [x22, 364]
	ldrb	w2, [x20, 3380]
	ldrh	w0, [x19, 6]
	cmp	w2, 2
	bhi	.L3701
	cmp	w0, 1
	bne	.L3671
.L3701:
	ldrb	w1, [x19, 5]
	cmp	w1, 0
	mov	w1, 0
	cset	w4, ne
	cmp	w0, 1
	add	x0, x21, :lo12:.LANCHOR5
	csinc	w4, w2, w4, eq
	ldrb	w0, [x0, 360]
	mov	w3, w0
.L3705:
	cmp	w1, w4
	bne	.L3706
	and	w1, w1, 255
	add	x21, x21, :lo12:.LANCHOR5
	sub	w2, w2, w1
	ubfiz	x0, x0, 6, 8
	strb	w2, [x20, 3380]
	add	x0, x28, x0
	strb	w3, [x21, 360]
	bl	sblk_prog_page
	ldrh	w0, [x19, 6]
	cmp	w0, 1
	bne	.L3671
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L3671:
	ldrb	w0, [x20, 3381]
	cbnz	w0, .L3668
.L3670:
	bl	ftl_write_completed
	mov	w0, 0
	b	.L3667
.L3697:
	ldr	w0, [x29, 140]
	sub	w2, w2, w21
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x21, 7, 9
	beq	.L3699
	ldr	x1, [x25, 8]
	lsl	x0, x0, 2
	add	x1, x1, x0
	add	x0, x27, x0
	bl	ftl_memcpy
	b	.L3698
.L3699:
	mov	w1, 0
	add	x0, x27, x0, lsl 2
	bl	ftl_memset
	b	.L3698
.L3706:
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x28, x3]
	b	.L3705
.L3707:
	lsl	w2, w22, 9
	mov	x25, 0
	mov	w26, 0
.L3695:
	mov	w1, 0
	mov	x0, x27
	bl	ftl_memset
	b	.L3696
.L3687:
	cbnz	w22, .L3707
	mov	w26, 0
	mov	x25, 0
	b	.L3694
	.size	ftl_write_commit, .-ftl_write_commit
	.align	2
	.global	gc_do_copy_back
	.type	gc_do_copy_back, %function
gc_do_copy_back:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x22, .LANCHOR3
	add	x1, x22, :lo12:.LANCHOR3
	stp	x19, x20, [sp, 16]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrb	w0, [x1, 1337]
	cbnz	w0, .L3734
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L3733
	adrp	x19, .LANCHOR0
	add	x2, x19, :lo12:.LANCHOR0
	ldrh	w3, [x2, 3442]
	mov	w0, w3
	bl	gc_get_src_ppa_from_index
	add	w3, w3, 1
	mov	w23, w0
	str	w23, [x20, 40]
	strh	w3, [x2, 3442]
	mov	w1, 1
	mov	x0, x20
	bl	sblk_read_page
	ldr	w0, [x20, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3736
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1024
	mov	w2, 1032
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3736:
	ldr	x0, [x20, 24]
	ldr	w21, [x0, 4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29, 192]
	cmn	w0, #1
	bne	.L3737
	mov	w2, 0
	add	x1, x29, 192
	mov	w0, w21
	bl	pm_log2phys
.L3737:
	ldr	w24, [x29, 192]
	cmp	w23, w24
	bne	.L3738
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 1306
	add	x1, x1, 3354
.L3741:
	ldr	w2, [x0, 34]
	cmp	w21, w2
	bne	.L3739
	ldrb	w2, [x0]
	tbz	x2, 1, .L3739
	mov	x0, x20
	bl	zbuf_free
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3733
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC297
	mov	w2, w23
	mov	w1, w21
	add	x0, x0, :lo12:.LC297
	ldrh	w3, [x19, 3442]
	bl	printk
.L3733:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 240
	ret
.L3739:
	add	x0, x0, 64
	cmp	x1, x0
	bne	.L3741
	add	x22, x22, :lo12:.LANCHOR3
	strb	wzr, [x20, 57]
	str	w21, [x20, 36]
	ldrb	w0, [x22, 1946]
	strb	w0, [x20, 56]
	ldr	x0, [x20, 24]
	ldr	w1, [x0]
	str	w1, [x20, 32]
	str	wzr, [x0, 16]
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3742
	mov	w0, w21
	bl	lpa_hash_get_ppa
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, w0
	mov	w4, w23
	adrp	x0, .LC298
	mov	w2, w24
	add	x0, x0, :lo12:.LC298
	ldrh	w5, [x1, 3442]
	mov	w1, w21
	bl	printk
.L3742:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19, 3384]
	ldr	w0, [x1, 60]
	add	w0, w0, 1
	str	w0, [x1, 60]
	ldrh	w0, [x19, 3444]
	add	w0, w0, 1
	strh	w0, [x19, 3444]
	b	.L3733
.L3738:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3743
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, w21
	bl	lpa_hash_get_ppa
	mov	w3, w0
	mov	w4, w23
	adrp	x0, .LC298
	ldrh	w5, [x19, 3442]
	mov	w2, w24
	mov	w1, w21
	add	x0, x0, :lo12:.LC298
	bl	printk
.L3743:
	mov	x0, x20
	bl	zbuf_free
	b	.L3733
.L3734:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1, 1321]
	ldr	x26, [x0, 1128]
	add	x0, x26, 80
	str	x0, [x29, 184]
	ldrb	w0, [x0, 9]
	and	w19, w0, 65535
	cmp	w1, w19
	bhi	.L3798
	cmp	w19, 2
	mov	w23, 2
	csel	w23, w19, w23, ls
	and	w23, w23, 65535
.L3744:
	add	x1, x22, :lo12:.LANCHOR3
	ldrb	w20, [x1, 1320]
	cmp	w20, 3
	bne	.L3745
	add	x3, x21, :lo12:.LANCHOR0
	ldrb	w2, [x3, 1212]
	ldrh	w5, [x3, 5522]
	cbz	w2, .L3746
	sdiv	w0, w5, w0
	ldrb	w1, [x1, 1950]
	mov	w24, w5
	and	w26, w0, 65535
	cbz	w1, .L3747
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 2136
	ldrh	w0, [x1, w0, sxtw 1]
	cmp	w0, 0
	cset	w20, ne
.L3865:
	add	w20, w20, 1
.L3748:
	msub	w19, w19, w26, w5
	and	w19, w19, 65535
.L3749:
	adrp	x8, .LANCHOR5
	adrp	x0, .LANCHOR2
	add	x8, x8, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LANCHOR2
	add	x7, x21, :lo12:.LANCHOR0
	mul	w11, w20, w23
	add	x10, x22, :lo12:.LANCHOR3
	add	x6, x0, 2136
	sxtw	x12, w26
	mov	x13, x8
	add	x7, x7, 3416
	mov	w5, 0
.L3753:
	cmp	w5, w11
	bge	.L3806
	ldr	x1, [x29, 184]
	add	w2, w24, w5
	ldrh	w0, [x10, 1338]
	ldrb	w1, [x1, 9]
	mul	w0, w0, w1
	ldrb	w1, [x8, 465]
	sub	w0, w0, w1
	cmp	w2, w0
	blt	.L3754
	ldrb	w0, [x10, 1950]
	cbz	w0, .L3806
	ldrh	w0, [x6, x12, lsl 1]
	cmp	w0, w26
	bcc	.L3755
.L3806:
	mov	w0, 1
	str	w0, [x29, 180]
	b	.L3751
.L3798:
	mov	w23, 1
	b	.L3744
.L3747:
	adrp	x0, .LANCHOR5+461
	ldrb	w0, [x0, #:lo12:.LANCHOR5+461]
	cbz	w0, .L3800
	sub	w0, w26, #62
	and	w0, w0, 65535
	cmp	w0, 2159
	bhi	.L3801
	udiv	w20, w26, w20
	add	w20, w20, w20, lsl 1
	sub	w20, w26, w20
	and	w20, w20, 65535
	cmp	w20, 0
	cset	w20, eq
	b	.L3865
.L3800:
	mov	w20, 1
	b	.L3748
.L3801:
	mov	w20, 2
	b	.L3748
.L3746:
	ldrb	w1, [x1, 1336]
	cbz	w1, .L3750
	add	w19, w0, w0, lsl 1
	mov	w24, w5
	sdiv	w19, w5, w19
	mul	w0, w0, w19
	and	w26, w19, 65535
	sub	w0, w0, w0, lsl 2
	add	w19, w0, w5
	ldrb	w0, [x3, 1213]
	sdiv	w19, w19, w20
	and	w19, w19, 65535
	cbz	w0, .L3749
	add	w4, w26, w26, lsl 1
	and	w26, w4, 65535
	b	.L3749
.L3750:
	sdiv	w2, w5, w0
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x1, x1, 3672
	msub	w19, w19, w2, w5
	ldrh	w25, [x1, w2, sxtw 1]
	and	w19, w19, 65535
	and	w1, w25, 7
	str	w1, [x29, 180]
	lsr	w26, w25, 3
	madd	w0, w26, w0, w19
	add	w0, w0, w0, lsl 1
	and	w24, w0, 65535
	uxtw	x0, w1
	cmp	w0, 1
	beq	.L3749
.L3751:
	mul	w0, w20, w23
	str	wzr, [x29, 160]
	str	w0, [x29, 176]
	ubfiz	w0, w23, 1, 15
	strh	w0, [x29, 128]
	str	w0, [x29, 156]
	msub	w0, w0, w20, w24
	and	w0, w0, 65535
	str	w0, [x29, 152]
	ldrh	w0, [x29, 152]
	str	x0, [x29, 120]
	adrp	x0, .LANCHOR5
	add	x28, x0, :lo12:.LANCHOR5
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29, 112]
.L3756:
	ldr	w0, [x29, 160]
	ldr	w1, [x29, 176]
	and	w0, w0, 65535
	str	w0, [x29, 164]
	ldrh	w27, [x29, 160]
	cmp	w0, w1
	blt	.L3780
	add	x1, x22, :lo12:.LANCHOR3
	ldrb	w0, [x1, 1320]
	cmp	w0, 3
	add	x0, x29, 192
	bne	.L3781
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w2, [x2, 1212]
	cbz	w2, .L3782
	ldr	w2, [x29, 176]
	mov	w1, 0
	sub	w2, w2, #1
.L3783:
	cmp	w1, w2
	blt	.L3784
.L3870:
	ldr	x0, [x0, w2, sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x29, 176]
	ldr	x0, [x29, 192]
	bl	sblk_prog_page
	b	.L3785
.L3745:
	ldrb	w1, [x1, 1322]
	cbnz	w1, .L3752
	add	x1, x21, :lo12:.LANCHOR0
	mov	w20, 1
	ldrh	w24, [x1, 5522]
	sdiv	w26, w24, w0
	msub	w19, w19, w26, w24
	and	w19, w19, 65535
	b	.L3749
.L3752:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w20, 2
	ldrh	w24, [x1, 5522]
	sdiv	w26, w24, w0
	msub	w19, w19, w26, w24
	and	w19, w19, 65535
	b	.L3749
.L3755:
	tbz	x5, 0, .L3806
.L3754:
	ldr	x9, [x13, 288]
	sbfiz	x2, x2, 2, 32
	ldr	w0, [x9, x2]
	cmn	w0, #1
	bne	.L3757
	ldrh	w3, [x7, 26]
	ldrh	w0, [x7, 24]
	cmp	w0, w3
	bls	.L3733
	ldrb	w0, [x10, 1950]
	cbz	w0, .L3758
	ldrh	w0, [x6, x12, lsl 1]
	cmp	w0, w26
	bcs	.L3758
	tbz	x5, 0, .L3758
	ubfiz	x0, x0, 2, 16
	ldr	w0, [x9, x0]
.L3866:
	str	w0, [x9, x2]
.L3757:
	add	w5, w5, 1
	and	w5, w5, 65535
	b	.L3753
.L3758:
	mov	w0, w3
	bl	gc_get_src_ppa_from_index
	add	w3, w3, 1
	strh	w3, [x7, 26]
	b	.L3866
.L3780:
	ldr	w0, [x29, 164]
	add	w1, w24, w0
	sxtw	x0, w1
	str	x0, [x29, 168]
	add	x0, x22, :lo12:.LANCHOR3
	sxtw	x2, w1
	ldr	x0, [x0, 1328]
	ldrb	w0, [x0, x2]
	cmp	w0, 255
	bne	.L3760
	str	w1, [x29, 144]
	mov	w0, 0
	bl	buf_alloc
	mov	x25, x0
	ldr	w1, [x29, 144]
	cbnz	x0, .L3761
	str	w1, [x29, 144]
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, 0
	bl	buf_alloc
	ldr	w1, [x29, 144]
	mov	x25, x0
	cbz	x0, .L3733
.L3761:
	add	x2, x22, :lo12:.LANCHOR3
	ldr	x3, [x29, 168]
	ldrb	w5, [x25, 1]
	ldr	x0, [x2, 1328]
	strb	w5, [x0, x3]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x3, [x29, 184]
	ldrb	w5, [x0, 3423]
	add	w5, w5, 1
	strb	w5, [x0, 3423]
	add	w0, w27, w24
	strh	w0, [x25, 48]
	ldrb	w0, [x29, 180]
	strb	w0, [x25, 61]
	ldrh	w0, [x2, 1338]
	ldrb	w5, [x3, 9]
	adrp	x3, .LANCHOR5
	mul	w0, w0, w5
	add	x5, x3, :lo12:.LANCHOR5
	ldrb	w5, [x5, 465]
	sub	w0, w0, w5
	cmp	w1, w0
	blt	.L3762
	ldrb	w0, [x2, 1950]
	cbz	w0, .L3763
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0, w26, sxtw 1]
	cmp	w0, w26
	bcs	.L3763
	tbnz	x27, 0, .L3762
.L3763:
	ldr	x0, [x28, 280]
	mov	w2, -1
	ldr	x3, [x29, 168]
	str	w2, [x0, x3, lsl 2]
	ldrb	w0, [x28, 465]
	cmp	w0, 2
	bne	.L3764
	ldr	x0, [x29, 184]
	add	x7, x22, :lo12:.LANCHOR3
	ldrh	w2, [x28, 462]
	ldrh	w6, [x7, 1338]
	ldrb	w0, [x0, 9]
	mul	w0, w0, w6
	sub	w0, w0, #2
	cmp	w1, w0
	bne	.L3765
	ldr	x1, [x28, 280]
	ldr	x0, [x25, 8]
	str	x7, [x29, 144]
	bl	ftl_memcpy
	ldr	x7, [x29, 144]
	mov	w1, 0
	ldr	x0, [x25, 24]
	ldrb	w2, [x7, 1946]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x25, 24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	str	w1, [x0]
	ldr	x2, [x25, 24]
	ldrh	w1, [x28, 462]
	ldr	x0, [x25, 8]
	str	x2, [x29, 144]
	bl	js_hash
	ldr	x2, [x29, 144]
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x2, 4]
	ldr	x1, [x1, 1128]
	ldr	x0, [x25, 24]
	ldr	w1, [x1, 132]
	str	w1, [x0, 8]
	ldr	x0, [x25, 24]
	str	wzr, [x0, 12]
.L3766:
	ldr	x0, [x25, 24]
	str	wzr, [x0, 16]
.L3760:
	add	x5, x22, :lo12:.LANCHOR3
	ldr	x1, [x29, 168]
	ldrsw	x2, [x29, 164]
	add	x3, x29, 192
	add	x7, x21, :lo12:.LANCHOR0
	ldr	x0, [x5, 1328]
	add	x6, x7, 1304
	ldrb	w1, [x0, x1]
	ubfiz	x0, x1, 6, 8
	add	x0, x6, x0
	add	x6, x6, x1, lsl 6
	str	x0, [x3, x2, lsl 3]
	ldrb	w0, [x29, 180]
	strb	w0, [x6, 61]
	mov	w0, 2
	strh	w0, [x6, 50]
	ldrb	w0, [x5, 1320]
	cmp	w0, 3
	bne	.L3771
	ldrb	w8, [x7, 1212]
	cbz	w8, .L3772
	ldrb	w0, [x5, 1950]
	and	w6, w27, 1
	add	w7, w6, w26
	cbz	w0, .L3773
	adrp	x5, .LANCHOR2
	add	x5, x5, :lo12:.LANCHOR2
	add	x5, x5, 2136
	ldrh	w0, [x5, w26, sxtw 1]
	cmp	w0, w26
	bcs	.L3773
	cmp	w6, 0
	csel	w7, w7, w0, eq
.L3773:
	udiv	w5, w27, w20
	ldr	x0, [x29, 184]
	add	x6, x21, :lo12:.LANCHOR0
	add	x6, x6, x1, lsl 6
	add	w5, w5, w19
	add	x5, x0, w5, sxtw 1
	ldrh	w0, [x5, 16]
	add	x5, x22, :lo12:.LANCHOR3
	ldrh	w5, [x5, 1410]
	madd	w0, w0, w5, w7
	orr	w0, w0, 50331648
	str	w0, [x6, 1344]
.L3778:
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 1304
	add	x1, x0, x1, lsl 6
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w5, [x1, 40]
	ldr	x0, [x0, 296]
	ldrh	w1, [x1, 48]
	str	w5, [x0, x1, lsl 2]
	b	.L3797
.L3765:
	ldrb	w5, [x7, 1321]
	and	x1, x2, 65532
	ldr	x0, [x28, 280]
	str	x7, [x29, 144]
	add	x1, x0, x1
	ldr	x0, [x25, 8]
	mul	w5, w5, w6
	lsl	w5, w5, 2
	sub	w5, w5, w2
	str	w5, [x29, 104]
	mov	w2, w5
	bl	ftl_memcpy
	ldr	x7, [x29, 144]
	mov	w1, 0
	ldr	x0, [x25, 24]
	ldrb	w2, [x7, 1946]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x25, 24]
	mov	w1, 15555
	movk	w1, 0xf55f, lsl 16
	ldr	w5, [x29, 104]
	str	w1, [x0]
	mov	w1, w5
	ldr	x2, [x25, 24]
	ldr	x0, [x25, 8]
	str	x2, [x29, 144]
	bl	js_hash
	ldr	x2, [x29, 144]
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x2, 4]
	ldr	x1, [x1, 1128]
	ldr	x0, [x25, 24]
	ldr	w1, [x1, 132]
	str	w1, [x0, 8]
	mov	w1, 1
	ldr	x0, [x25, 24]
	str	w1, [x0, 12]
	b	.L3766
.L3764:
	add	x5, x22, :lo12:.LANCHOR3
	ldr	x1, [x28, 280]
	str	x5, [x29, 144]
	ldrb	w0, [x5, 1321]
	ldrh	w2, [x5, 1338]
	mul	w2, w2, w0
	ldr	x0, [x25, 8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldr	x5, [x29, 144]
	ldrb	w1, [x5, 1321]
	ldrh	w0, [x5, 1338]
	mul	w0, w0, w1
	ldrb	w1, [x5, 1946]
	lsl	w2, w0, 2
	cmp	w1, w0, lsr 6
	bge	.L3767
	ldrh	w1, [x28, 462]
	sub	w2, w1, w2
.L3767:
	ldr	x5, [x25, 8]
	ldr	x1, [x28, 288]
	add	x0, x5, w0, sxtw 2
	bl	ftl_memcpy
	add	x5, x22, :lo12:.LANCHOR3
	ldr	x0, [x25, 24]
	mov	w1, 0
	str	x5, [x29, 104]
	ldrb	w2, [x5, 1946]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldr	x0, [x25, 24]
	mov	w1, 15555
	ldr	x5, [x29, 104]
	movk	w1, 0xf55f, lsl 16
	str	w1, [x0]
	ldrb	w0, [x5, 1321]
	ldrh	w1, [x5, 1338]
	ldr	x2, [x25, 24]
	str	x2, [x29, 144]
	mul	w1, w1, w0
	ldr	x0, [x25, 8]
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29, 144]
	add	x1, x21, :lo12:.LANCHOR0
	str	w0, [x2, 4]
	ldr	x1, [x1, 1128]
	ldr	x0, [x25, 24]
	ldr	w1, [x1, 132]
.L3867:
	str	w1, [x0, 8]
	b	.L3766
.L3762:
	ldr	x0, [x29, 168]
	lsl	x0, x0, 2
	str	x0, [x29, 144]
	ldr	x0, [x28, 288]
	ldr	x1, [x29, 144]
	ldr	w0, [x0, x1]
	mov	w1, 1
	str	w0, [x25, 40]
	mov	x0, x25
	bl	sblk_read_page
	ldr	w0, [x25, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3769
	add	x0, x21, :lo12:.LANCHOR0
	add	x2, x22, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1205]
	mov	w0, 24
	ldrh	w5, [x2, 1304]
	sub	w0, w0, w1
	ldrb	w2, [x2, 1306]
	sub	w1, w0, w5
	mov	w0, 1
	lsl	w0, w0, w1
	ldr	w1, [x25, 40]
	sub	w0, w0, #1
	lsr	w1, w1, w5
	and	w0, w0, w1
	mov	x1, 0
	udiv	w0, w0, w2
	bl	ftl_sblk_dump
	ldr	w0, [x25, 52]
	cmp	w0, 512
	ccmn	w0, #1, 4, ne
	bne	.L3769
	ldr	x0, [x25, 24]
	mov	w1, -1
	str	w1, [x0, 4]
	ldr	w0, [x25, 52]
	cmp	w0, 512
	ccmp	w0, w1, 4, ne
	bne	.L3769
	ldr	x0, [x29, 112]
	mov	w2, 1223
	add	x1, x0, 1024
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3769:
	ldr	x1, [x25, 24]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0, 3364]
	ldr	w2, [x1, 4]
	cmp	w2, w0
	bcc	.L3770
	mov	w0, -1
	str	w0, [x1, 4]
.L3770:
	ldr	x0, [x25, 24]
	ldr	x2, [x29, 144]
	ldr	w1, [x0, 4]
	ldr	x0, [x28, 280]
	str	w1, [x0, x2]
	ldr	w1, [x25, 40]
	ldr	x0, [x25, 24]
	b	.L3867
.L3772:
	udiv	w0, w27, w0
	ldrb	w7, [x7, 1213]
	ldrh	w8, [x5, 1410]
	cbz	w7, .L3775
	ldr	x4, [x29, 184]
	add	w7, w0, w19
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	add	x7, x4, w7, sxtw 1
	ldrh	w5, [x7, 16]
	madd	w5, w5, w8, w26
	add	w0, w5, w0, uxth
.L3871:
	str	w0, [x6, 40]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	add	x5, x21, :lo12:.LANCHOR0
	add	x5, x5, 1304
	add	x1, x5, x1, lsl 6
	ldr	x5, [x0, 296]
	mov	w0, 3
	udiv	w0, w27, w0
	ldrh	w6, [x1, 48]
	ldr	w1, [x1, 40]
	add	w0, w0, w0, lsl 1
	sub	w0, w27, w0
	and	w0, w0, 65535
	add	w0, w0, 1
	orr	w0, w1, w0, lsl 24
	str	w0, [x5, x6, lsl 2]
.L3797:
	add	x1, x22, :lo12:.LANCHOR3
	ldrb	w0, [x1, 1950]
	cbz	w0, .L3779
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0, w26, sxtw 1]
	cmp	w0, w26
	bcs	.L3779
	tbz	x27, 0, .L3779
	ldr	x0, [x1, 1328]
	mov	w5, -1
	ldr	x4, [x29, 168]
	strb	w5, [x0, x4]
	ldr	x0, [x3, x2, lsl 3]
	ldrh	w2, [x29, 152]
	strh	w2, [x0, 48]
	ldrb	w2, [x0, 1]
	ldr	x0, [x1, 1328]
	ldr	x1, [x29, 120]
	strb	w2, [x0, x1]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	mov	w1, -1
	ldr	x0, [x0, 288]
	str	w1, [x0, x4, lsl 2]
.L3779:
	ldr	w0, [x29, 160]
	add	w0, w0, 1
	str	w0, [x29, 160]
	b	.L3756
.L3775:
	ldr	x4, [x29, 184]
	add	w0, w0, w19
	add	x0, x4, w0, sxtw 1
	ldrh	w0, [x0, 16]
	madd	w0, w0, w8, w26
	b	.L3871
.L3771:
	cmp	w0, 2
	bne	.L3778
	ldrb	w0, [x5, 1322]
	ldrh	w7, [x5, 1410]
	cbnz	w0, .L3777
	ldr	w0, [x29, 164]
	add	w5, w19, w0
	ldr	x0, [x29, 184]
	add	x5, x0, w5, sxtw 1
	ldrh	w0, [x5, 16]
	madd	w0, w0, w7, w26
.L3868:
	orr	w0, w0, 33554432
	str	w0, [x6, 40]
	b	.L3778
.L3777:
	ldr	x0, [x29, 184]
	add	w5, w19, w27, lsr 1
	add	x5, x0, w5, sxtw 1
	ldrh	w0, [x5, 16]
	and	w5, w27, 1
	madd	w0, w0, w7, w26
	add	w0, w0, w5
	b	.L3868
.L3784:
	ldr	x3, [x0, w1, sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0, w1, sxtw 3]
	and	w1, w1, 65535
	ldrb	w5, [x5, 1]
	strb	w5, [x3]
	b	.L3783
.L3782:
	ldrb	w2, [x1, 1336]
	ldr	x1, [x29, 192]
	cbz	w2, .L3786
.L3789:
	strb	wzr, [x1, 60]
	b	.L3788
.L3786:
	ldr	w2, [x29, 180]
	cmp	w2, 1
	bne	.L3787
	mov	w2, 9
.L3869:
	strb	w2, [x1, 60]
.L3788:
	mov	w2, w23
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
.L3785:
	add	x0, x22, :lo12:.LANCHOR3
	ldrb	w1, [x0, 1336]
	cbz	w1, .L3794
	ldr	w0, [x29, 128]
	add	w23, w23, w0
	and	w23, w23, 65535
.L3795:
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	ldr	x1, [x21, 3384]
	ldr	w0, [x1, 52]
	add	w0, w0, w23
	str	w0, [x1, 52]
	ldr	x1, [x29, 184]
	ldrh	w0, [x21, 5522]
	add	w23, w23, w0
	ldrh	w0, [x22, 1338]
	and	w23, w23, 65535
	strh	w23, [x21, 5522]
	ldrb	w1, [x1, 9]
	mul	w0, w0, w1
	cmp	w23, w0
	blt	.L3796
	ldr	x0, [x21, 1128]
	strh	wzr, [x0, 86]
.L3796:
	bl	gc_write_completed
	b	.L3733
.L3787:
	ldr	w2, [x29, 180]
	cmp	w2, 2
	bne	.L3789
	mov	w2, 13
	b	.L3869
.L3781:
	ldrb	w2, [x1, 1322]
	cbz	w2, .L3807
	ldrb	w1, [x1, 1323]
	cbnz	w1, .L3791
.L3790:
	ldr	w2, [x29, 176]
	sub	w2, w2, #1
.L3792:
	cmp	w1, w2
	bge	.L3870
	ldr	x3, [x0, w1, sxtw 3]
	add	w1, w1, 1
	ldr	x5, [x0, w1, sxtw 3]
	and	w1, w1, 65535
	ldrb	w5, [x5, 1]
	strb	w5, [x3]
	b	.L3792
.L3807:
	mov	w1, 0
	b	.L3790
.L3791:
	mov	w1, w23
	bl	sblk_3d_mlc_prog_pages
	b	.L3785
.L3794:
	ldrb	w1, [x0, 1322]
	cbnz	w1, .L3808
	add	x1, x21, :lo12:.LANCHOR0
	ldrb	w1, [x1, 1212]
	cbz	w1, .L3795
	ldrb	w0, [x0, 1950]
	ldrh	w1, [x29, 176]
	cbz	w0, .L3809
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 2136
	ldrh	w0, [x0, w26, sxtw 1]
	cmp	w0, w26
	csel	w23, w23, w1, cc
	b	.L3795
.L3808:
	ldr	w23, [x29, 156]
	b	.L3795
.L3809:
	mov	w23, w1
	b	.L3795
	.size	gc_do_copy_back, .-gc_do_copy_back
	.align	2
	.global	zftl_do_gc
	.type	zftl_do_gc, %function
zftl_do_gc:
	sub	sp, sp, #128
	stp	x29, x30, [sp, 16]
	add	x29, sp, 16
	stp	x19, x20, [sp, 32]
	adrp	x19, .LANCHOR0
	stp	x27, x28, [sp, 96]
	mov	w27, w0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 48]
	stp	x23, x24, [sp, 64]
	cmp	w27, 1
	stp	x25, x26, [sp, 80]
	adrp	x20, .LANCHOR5
	ldrh	w24, [x0, 3372]
	ldrh	w22, [x0, 3368]
	ldrh	w23, [x0, 3370]
	add	w22, w24, w22
	ldr	x25, [x0, 1128]
	and	w22, w22, 65535
	beq	.L3873
.L3885:
	add	x0, x20, :lo12:.LANCHOR5
	ldrb	w1, [x0, 264]
	cmp	w1, 6
	bhi	.L4000
	adrp	x0, .L3876
	add	x0, x0, :lo12:.L3876
	ldrh	w0, [x0,w1,uxtw #1]
	adr	x1, .Lrtx3876
	add	x0, x1, w0, sxth #2
	br	x0
.Lrtx3876:
	.section	.rodata
	.align	0
	.align	2
.L3876:
	.2byte	(.L3875 - .Lrtx3876) / 4
	.2byte	(.L3877 - .Lrtx3876) / 4
	.2byte	(.L3878 - .Lrtx3876) / 4
	.2byte	(.L3879 - .Lrtx3876) / 4
	.2byte	(.L3880 - .Lrtx3876) / 4
	.2byte	(.L3881 - .Lrtx3876) / 4
	.2byte	(.L3882 - .Lrtx3876) / 4
	.text
.L3881:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w23, 0
	add	x21, x21, 3416
.L3883:
	bl	gc_check_data_one_wl
	cbz	w0, .L3996
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x0, [x19, 1128]
	strh	wzr, [x19, 3472]
	ldrh	w0, [x0, 80]
	bl	ftl_free_sblk
	ldr	x1, [x19, 1128]
	mov	w0, -1
	ldr	x2, [x19, 3384]
	strh	w0, [x1, 80]
	strh	w0, [x2, 126]
	strh	w0, [x1, 130]
	ldr	x0, [x19, 3424]
	bl	zbuf_free
	strb	wzr, [x20, 264]
	str	xzr, [x19, 3424]
	b	.L4090
.L3873:
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w21, [x0, 468]
	cbnz	w21, .L3884
	ldr	w0, [x0, 472]
	cbz	w0, .L3885
.L3884:
	adrp	x0, .LANCHOR3+1380
	ldrh	w0, [x0, #:lo12:.LANCHOR3+1380]
	cmp	w22, w0, lsr 2
	bls	.L3885
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3392]
	cmp	w0, w22
	bcs	.L3885
	add	w21, w21, 20
	bl	timer_get_time
	cmp	w21, w0
	bcs	.L3886
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0, 468]
.L3886:
	add	x26, x20, :lo12:.LANCHOR5
	ldr	w21, [x26, 472]
	bl	timer_get_time
	add	w21, w21, 20
	cmp	w21, w0
	bcs	.L3887
	str	wzr, [x26, 472]
.L3887:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 1128]
	ldrh	w0, [x0, 124]
	cbnz	w0, .L3885
.L4000:
	mov	w23, 16
	b	.L3872
.L3875:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x25, 80]
	add	w24, w24, w23
	and	w24, w24, 65535
	ldrh	w1, [x0, 3376]
	ldrh	w26, [x0, 3374]
	add	w26, w26, w1
	mov	w1, 65535
	and	w26, w26, 65535
	cmp	w2, w1
	beq	.L3888
	cbnz	w27, .L3889
	ldrh	w0, [x0, 3392]
	cmp	w22, w0, lsl 1
	bge	.L4000
.L3889:
	adrp	x21, .LANCHOR3
	add	x5, x21, :lo12:.LANCHOR3
	add	x0, x5, 1368
	ldrh	w1, [x5, 1378]
	add	w1, w1, 1
	and	w1, w1, 65535
	strh	w1, [x5, 1378]
	bl	_list_get_gc_head_node
	and	w2, w0, 65535
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3891
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3, 268]
	add	w1, w1, 1
	str	w1, [x3, 268]
	add	x3, x19, :lo12:.LANCHOR0
	ldr	x4, [x3, 1120]
	ldrh	w2, [x4, x2]
	ldrh	w4, [x5, 1376]
	cmp	w4, w2
	bcs	.L3892
	ldrh	w4, [x3, 1096]
	cmp	w1, w4, lsr 4
	bls	.L3891
	ldrh	w1, [x3, 3406]
	cmp	w1, w2
	bls	.L3891
.L3892:
	add	x1, x21, :lo12:.LANCHOR3
	ldrb	w2, [x1, 1337]
	mov	w1, 0
	bl	gc_add_sblk
	cbz	w0, .L3893
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	str	wzr, [x20, 268]
	strb	w0, [x20, 264]
	b	.L4000
.L3891:
	add	x0, x21, :lo12:.LANCHOR3
	strh	wzr, [x0, 1378]
.L3893:
	cmp	w24, 15
	bls	.L4002
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x1, 3378]
	ldrh	w0, [x0, 1382]
	cmp	w1, w0
	bhi	.L4002
	cmp	w26, 0
	cset	w23, eq
	add	w23, w23, 1
.L3894:
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3895
	add	x19, x19, :lo12:.LANCHOR0
	mov	w4, w24
	mov	w3, w22
	mov	w1, 2807
	ldr	x0, [x19, 1128]
	ldrh	w7, [x0, 122]
	ldrh	w6, [x0, 120]
	ldrh	w5, [x0, 124]
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w2, [x0, 1337]
	ldrh	w0, [x25, 80]
	str	w0, [sp]
	adrp	x0, .LC299
	add	x0, x0, :lo12:.LC299
	bl	printk
.L3895:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 1
	mov	w1, w23
	ldrb	w0, [x21, 1337]
	bl	gc_search_src_blk
	cmp	w0, 0
	ble	.L3896
.L3897:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
.L4092:
	strb	w0, [x20, 264]
	b	.L4000
.L4002:
	mov	w23, 2
	b	.L3894
.L3896:
	ldrb	w0, [x21, 1337]
	mov	w2, 1
	mov	w1, 3
	bl	gc_search_src_blk
	cmp	w0, 0
	bgt	.L3897
	b	.L4000
.L3888:
	cmp	w27, 1
	bne	.L3898
	ldr	x1, [x0, 3384]
	ldrh	w1, [x1, 150]
	cbz	w1, .L3899
	ldrh	w0, [x0, 3372]
	cmp	w0, 8
	bls	.L3899
	bl	gc_ink_check_sblk
.L3899:
	bl	gc_scan_static_data
	adrp	x21, .LANCHOR3
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 1128]
	ldrh	w0, [x0, 122]
	cbz	w0, .L3900
.L3901:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 1
	add	x20, x20, :lo12:.LANCHOR5
	strb	w0, [x21, 1337]
	b	.L4092
.L3900:
	bl	gc_static_wearleveling
	mov	w23, w0
	cbnz	w0, .L3901
	bl	gc_block_vpn_scan
	cbz	w24, .L3902
	cmp	w22, w26
	bcs	.L3903
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w0, [x0, 1380]
	cmp	w0, w22
	bhi	.L3904
.L3903:
	add	x1, x21, :lo12:.LANCHOR3
	add	w0, w22, w26
	ldrh	w2, [x1, 1380]
	cmp	w0, w2
	blt	.L3904
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0, 3378]
	ldrh	w0, [x1, 1382]
	cmp	w2, w0
	bcc	.L3902
.L3904:
	add	x5, x21, :lo12:.LANCHOR3
	mov	w0, 1
	strb	w0, [x5, 1337]
	mov	w0, 16
	bl	zftl_get_gc_node.part.10
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3905
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3, x1]
	ldrh	w1, [x2, 272]
	cmp	w3, w1
	bcs	.L3905
	cmp	w22, 2
	bls	.L3905
	str	wzr, [x2, 268]
	mov	w1, 1
	strb	wzr, [x5, 1337]
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3897
.L3905:
	add	x5, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x5, 1360
	bl	_list_get_gc_head_node
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3906
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	add	x2, x20, :lo12:.LANCHOR5
	ldrh	w3, [x3, x1]
	ldrh	w1, [x2, 272]
	cmp	w3, w1
	bcs	.L3906
	cmp	w22, 2
	bls	.L3906
	str	wzr, [x2, 268]
	mov	w1, 1
	strb	wzr, [x5, 1337]
	mov	w2, 0
	bl	gc_add_sblk
	cbnz	w0, .L3897
.L3906:
	add	x1, x20, :lo12:.LANCHOR5
	add	x2, x21, :lo12:.LANCHOR3
	ldrh	w0, [x1, 272]
	ldrh	w8, [x2, 1376]
	add	w0, w0, 1
	and	w0, w0, 65535
	strh	w0, [x1, 272]
	cmp	w0, w8, lsr 5
	bls	.L3907
	mov	w0, 4
	strh	w0, [x1, 272]
.L3907:
	add	x6, x20, :lo12:.LANCHOR5
	add	x7, x19, :lo12:.LANCHOR0
	add	x5, x21, :lo12:.LANCHOR3
	ldr	w0, [x6, 268]
	ldrh	w1, [x7, 1096]
	add	w0, w0, 1
	strh	wzr, [x5, 1352]
	strh	wzr, [x5, 1354]
	strh	wzr, [x5, 1356]
	cmp	w0, w1, lsr 5
	str	w0, [x6, 268]
	bls	.L3908
	ldrh	w0, [x7, 3378]
	cmp	w0, w24
	bls	.L3908
	mov	w1, 0
	add	x0, x5, 1368
	bl	_list_get_gc_head_node
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	bne	.L3909
.L4082:
	mov	w23, 16
	b	.L3910
.L3909:
	ldr	x1, [x7, 1120]
	ubfiz	x0, x0, 1, 16
	ldrh	w1, [x1, x0]
	ldrb	w0, [x5, 1321]
	mul	w0, w0, w8
	cmp	w1, w0
	bgt	.L4082
	ldrb	w0, [x5, 1337]
	mov	w2, 4
	mov	w1, 2
	str	wzr, [x6, 268]
.L4081:
	bl	gc_search_src_blk
	and	w0, w0, 65535
	cbz	w0, .L4082
	b	.L3897
.L3908:
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR3
	ldrh	w2, [x0, 3378]
	ldrh	w1, [x3, 1382]
	cmp	w2, w1
	bcc	.L3913
	mov	w2, 1
	mov	w1, 2
	ldrb	w0, [x3, 1337]
	b	.L4081
.L3913:
	ldrh	w1, [x0, 3374]
	cbnz	w1, .L3914
	ldrh	w1, [x0, 3376]
	cmp	w1, 8
	bls	.L3915
.L3914:
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 4
	mov	w1, 1
	ldrb	w0, [x21, 1337]
	b	.L4081
.L3915:
	ldrh	w0, [x0, 5526]
	cbnz	w0, .L4082
.L3910:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR3
	ldr	w1, [x0, 3396]
	cbz	w1, .L3916
	str	wzr, [x0, 3396]
	cmp	w24, 0
	ccmp	w22, 15, 2, ne
	add	x0, x21, :lo12:.LANCHOR3
	bls	.L3917
	strb	wzr, [x0, 1337]
.L3918:
	add	x5, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x5, 1368
	bl	_list_get_gc_head_node
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3919
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	ldrh	w1, [x3, x1]
	cmp	w1, 8
	bhi	.L3919
	mov	w1, 1
	str	w1, [x2, 3396]
	ldrb	w2, [x5, 1337]
	mov	w1, 0
	bl	gc_add_sblk
	cbnz	w0, .L3897
.L3919:
	add	x5, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x5, 1360
	bl	_list_get_gc_head_node
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3920
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	ldrh	w1, [x3, x1]
	cmp	w1, 4
	bhi	.L3920
	mov	w1, 1
	str	w1, [x2, 3396]
	ldrb	w2, [x5, 1337]
	mov	w1, 0
	bl	gc_add_sblk
	cbnz	w0, .L3897
.L3920:
	mov	w0, 0
	bl	zftl_get_gc_node.part.10
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3916
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x3, [x2, 1120]
	ldrh	w1, [x3, x1]
	cmp	w1, 4
	bhi	.L3916
	mov	w1, 1
	str	w1, [x2, 3396]
	add	x2, x21, :lo12:.LANCHOR3
	ldrb	w2, [x2, 1337]
	bl	gc_add_sblk
	cbnz	w0, .L3897
.L3916:
	add	x28, x19, :lo12:.LANCHOR0
	add	x25, x21, :lo12:.LANCHOR3
	mov	w1, 1
	ldr	x0, [x28, 1128]
	strb	w1, [x25, 1337]
	ldrh	w0, [x0, 124]
	cbz	w0, .L3921
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x25, 1337]
	strb	w1, [x20, 264]
.L3872:
	mov	w0, w23
	ldp	x19, x20, [sp, 32]
	ldp	x21, x22, [sp, 48]
	ldp	x23, x24, [sp, 64]
	ldp	x25, x26, [sp, 80]
	ldp	x27, x28, [sp, 96]
	ldp	x29, x30, [sp, 16]
	add	sp, sp, 128
	ret
.L3902:
	add	x21, x21, :lo12:.LANCHOR3
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x21, 1376]
	ldrh	w1, [x0, 272]
	cmp	w1, w2, lsr 5
	bcc	.L4082
	mov	w1, 4
	strh	w1, [x0, 272]
	b	.L4082
.L3898:
	ldrh	w1, [x0, 3392]
	cmp	w1, w22
	bcs	.L4082
	ldr	x0, [x0, 1128]
	ldrh	w0, [x0, 124]
	cbz	w0, .L4000
	b	.L4082
.L3917:
	mov	w1, 1
	strb	w1, [x0, 1337]
	b	.L3918
.L3921:
	ldrh	w0, [x28, 3392]
	cmp	w22, w0
	bcs	.L3922
	cbz	w24, .L3923
	cmp	w24, 16
	bls	.L3924
	ldrh	w2, [x28, 3378]
	ldrh	w0, [x25, 1382]
	cmp	w2, w0
	bhi	.L3924
	mov	w2, 4
	mov	w0, w1
	bl	gc_search_src_blk
	tst	w0, 65535
	bne	.L3925
.L4089:
	mov	w2, 4
	mov	w1, 3
	ldrb	w0, [x25, 1337]
	b	.L4084
.L3925:
	mov	w1, 0
	add	x0, x25, 1368
	bl	_list_get_gc_head_node
	and	w2, w0, 65535
	mov	w1, 65535
	cmp	w2, w1
	beq	.L3928
	add	x3, x20, :lo12:.LANCHOR5
	ubfiz	x2, x2, 1, 16
	ldr	w1, [x3, 268]
	add	w1, w1, 1
	str	w1, [x3, 268]
	ldr	x3, [x28, 1120]
	ldrh	w2, [x3, x2]
	ldrh	w3, [x25, 1376]
	cmp	w3, w2
	bcs	.L3929
	ldrh	w3, [x28, 1096]
	cmp	w1, w3, lsr 4
	bls	.L3928
	ldrh	w1, [x28, 3406]
	cmp	w1, w2
	bls	.L3928
.L3929:
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w2, [x21, 1337]
	bl	gc_add_sblk
	mov	w0, 1
	str	w0, [x19, 3396]
	add	x0, x20, :lo12:.LANCHOR5
	str	wzr, [x0, 268]
.L3928:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20, 264]
	b	.L3872
.L3924:
	mov	w2, 1
	mov	w1, 2
	mov	w0, w2
	bl	gc_search_src_blk
	tst	w0, 65535
	bne	.L3928
	add	x21, x21, :lo12:.LANCHOR3
	mov	w2, 4
	mov	w1, 3
	ldrb	w0, [x21, 1337]
.L4084:
	bl	gc_search_src_blk
	and	w0, w0, 65535
.L3926:
	cbnz	w0, .L3928
	b	.L3872
.L3923:
	strb	wzr, [x25, 1337]
	cmp	w22, 16
	bls	.L3931
	mov	w2, 4
	mov	w1, 3
	mov	w0, 0
	b	.L4084
.L3931:
	mov	w2, w1
	mov	w0, 0
	bl	gc_search_src_blk
	tst	w0, 65535
	bne	.L3928
	b	.L4089
.L3922:
	cmp	w27, 1
	bne	.L3872
	cmp	w22, w0, lsl 1
	bge	.L3932
	cmp	w26, w24, lsr 1
	bcs	.L3933
	ldrh	w1, [x28, 3378]
	ldrh	w0, [x25, 1382]
	cmp	w1, w0
	bcs	.L3933
	ldrh	w0, [x25, 1380]
	lsr	w0, w0, 2
	strh	w0, [x28, 3392]
	b	.L3872
.L3933:
	mov	w0, 8
	bl	zftl_get_gc_node.part.10
	and	w1, w0, 65535
	mov	w2, 65535
	cmp	w1, w2
	beq	.L3934
	add	x2, x19, :lo12:.LANCHOR0
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x2, 1120]
	ldrh	w1, [x2, x1]
	cmp	w1, 3
	bhi	.L3934
	cbz	w22, .L3934
	add	x1, x21, :lo12:.LANCHOR3
	mov	w2, 0
	strb	wzr, [x1, 1337]
	mov	w1, 1
	bl	gc_add_sblk
	cbnz	w0, .L3928
.L3934:
	add	x5, x21, :lo12:.LANCHOR3
	mov	w1, 0
	add	x0, x5, 1368
	bl	_list_get_gc_head_node
	and	w25, w0, 65535
	mov	w1, 65535
	mov	w28, w25
	cmp	w25, w1
	bne	.L3935
.L3940:
	cmp	w24, 1
	bhi	.L3936
.L3937:
	cmp	w22, w26
	bcs	.L3944
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 4
	add	x19, x19, :lo12:.LANCHOR0
	strb	wzr, [x21, 1337]
	bl	zftl_get_gc_node.part.10
	and	w0, w0, 65535
	mov	w1, 65535
	cmp	w0, w1
	beq	.L4088
	ldr	x1, [x19, 1120]
	ubfiz	x0, x0, 1, 16
	ldrb	w2, [x21, 1321]
	ldrh	w1, [x1, x0]
	ldrh	w0, [x21, 1376]
	mul	w0, w0, w2
	cmp	w1, w0, lsr 1
	ble	.L3946
.L4088:
	ldrh	w0, [x21, 1380]
	lsr	w0, w0, 2
	strh	w0, [x19, 3392]
	b	.L3872
.L3935:
	add	x27, x20, :lo12:.LANCHOR5
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x25, w25
	ldr	w1, [x27, 268]
	ldrh	w3, [x2, 1096]
	add	w1, w1, 1
	str	w1, [x27, 268]
	cmp	w1, w3, lsr 4
	bls	.L3938
	ldr	x1, [x2, 1120]
	str	wzr, [x27, 268]
	ldrh	w3, [x1, x25, lsl 1]
	ldrh	w1, [x2, 3406]
	cmp	w3, w1
	bcs	.L3938
	mov	w3, 1
	mov	w2, 1
	strb	w3, [x5, 1337]
	mov	w1, 0
	str	w3, [x29, 108]
	bl	gc_add_sblk
	ldr	w3, [x29, 108]
	cbz	w0, .L3938
	strb	w3, [x27, 264]
	b	.L3872
.L3938:
	add	x0, x19, :lo12:.LANCHOR0
	add	x3, x21, :lo12:.LANCHOR3
	ldr	x1, [x0, 1120]
	ldrh	w2, [x1, x25, lsl 1]
	ldrh	w1, [x3, 1376]
	cmp	w2, w1, lsr 1
	bhi	.L3939
	mov	w2, 0
	mov	w1, 1
	mov	w0, w28
	bl	gc_add_sblk
	b	.L3928
.L3939:
	ldrh	w1, [x0, 3374]
	ldrh	w4, [x0, 3376]
	ldrh	w3, [x3, 1380]
	add	w1, w1, w4
	cmp	w1, w3, lsl 1
	ble	.L3940
	ldrh	w0, [x0, 3406]
	cmp	w0, w2
	bcc	.L3937
	b	.L3940
.L3936:
	add	x25, x21, :lo12:.LANCHOR3
	mov	w1, 1
	cmp	w24, 16
	strb	w1, [x25, 1337]
	bls	.L3941
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0, 3378]
	ldrh	w0, [x25, 1382]
	cmp	w2, w0
	bhi	.L3941
	mov	w2, 4
	mov	w0, w1
	bl	gc_search_src_blk
	ands	w0, w0, 65535
	bne	.L3942
	ldrb	w0, [x25, 1337]
	mov	w2, 4
	mov	w1, 3
.L4083:
	bl	gc_search_src_blk
	and	w0, w0, 65535
.L3942:
	add	x21, x21, :lo12:.LANCHOR3
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w22, w26, lsr 1
	ldrh	w1, [x21, 1380]
	bls	.L4085
	lsr	w1, w1, 2
.L4086:
	strh	w1, [x19, 3392]
	b	.L3926
.L3941:
	mov	w2, 1
	mov	w1, 2
	mov	w0, w2
	bl	gc_search_src_blk
	ands	w0, w0, 65535
	bne	.L3942
	add	x0, x21, :lo12:.LANCHOR3
	mov	w2, 4
	mov	w1, 3
	ldrb	w0, [x0, 1337]
	b	.L4083
.L3946:
	mov	w1, 3
	mov	w2, 4
	mov	w0, 0
	bl	gc_search_src_blk
	ldrh	w1, [x21, 1380]
	and	w0, w0, 65535
.L4085:
	lsr	w1, w1, 1
	b	.L4086
.L3944:
	add	x19, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	b	.L4088
.L3932:
	ldrh	w0, [x25, 1380]
	lsr	w0, w0, 2
	strh	w0, [x28, 3392]
	ldrh	w0, [x28, 5526]
	cmp	w0, 0
	csel	w23, w23, wzr, ne
	b	.L3872
.L3877:
	add	x6, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x6, 3416]
	cmp	w1, w0
	bne	.L3947
	bl	gc_get_src_blk
	strh	w0, [x6, 3416]
.L3947:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w1, [x0, 3416]
	cmp	w1, w2
	beq	.L3948
	ldrh	w2, [x0, 1096]
	cmp	w2, w1
	bhi	.L3948
	mov	w1, -1
	strh	w1, [x0, 3416]
.L3948:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	ldrh	w3, [x0, 3416]
	cmp	w3, w2
	beq	.L4091
	ldrh	w5, [x0, 3472]
	uxtw	x4, w3
	ldr	x2, [x0, 1104]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L3950
	add	x0, x0, 3474
	mov	w1, 0
.L3952:
	ldrh	w6, [x0], 2
	cmp	w6, w3
	bne	.L3951
.L3956:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19, 3416]
	b	.L4000
.L3951:
	add	w1, w1, 1
	and	w1, w1, 65535
	cmp	w5, w1
	bne	.L3952
.L3950:
	ldrb	w0, [x2, 2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L3953
	tst	w0, 192
	bne	.L3954
.L3953:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0, 1120]
	ldrh	w0, [x0, x4, lsl 1]
	cbz	w0, .L3956
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3306
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3956
.L3954:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 2
	b	.L4092
.L3878:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L3957
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 3
	b	.L4092
.L3957:
	add	x1, x19, :lo12:.LANCHOR0
	mov	w3, 65535
	ldrh	w0, [x1, 3416]
	cmp	w0, w3
	beq	.L3897
	ldrh	w3, [x1, 3440]
	add	x20, x20, :lo12:.LANCHOR5
	cbz	w3, .L3958
	mov	w0, 4
	strh	wzr, [x1, 3442]
	strb	w0, [x20, 264]
	b	.L4000
.L3958:
	ldr	x1, [x1, 1120]
	ubfiz	x0, x0, 1, 16
	mov	w2, 1
	strb	w2, [x20, 264]
	ldrh	w0, [x1, x0]
	cbz	w0, .L3959
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3336
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3959:
	add	x20, x19, :lo12:.LANCHOR0
	add	x21, x20, 3416
	ldrh	w0, [x20, 3416]
	bl	ftl_free_sblk
	ldr	x0, [x20, 1120]
	ldrh	w1, [x20, 3416]
	strh	wzr, [x0, x1, lsl 1]
	ldrh	w0, [x21, 30]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 8
	bhi	.L3960
	strh	w0, [x21, 30]
	b	.L3956
.L3960:
	strh	wzr, [x21, 30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	b	.L3956
.L3879:
	add	x23, x19, :lo12:.LANCHOR0
	adrp	x24, .LANCHOR3
	add	x21, x23, 3416
	add	x24, x24, :lo12:.LANCHOR3
.L4053:
	bl	gc_scan_src_blk_one_page
	ldrh	w1, [x21, 2]
	ldrh	w0, [x24, 1376]
	cmp	w1, w0
	bcs	.L3962
	cmp	w22, 7
	bls	.L4053
	b	.L4000
.L3962:
	ldrh	w3, [x21, 24]
	ldrh	w1, [x21]
	cbz	w3, .L3963
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 4
	ldr	x2, [x23, 1120]
	strh	wzr, [x21, 26]
	strb	w0, [x20, 264]
	ubfiz	x0, x1, 1, 16
	ldrh	w2, [x2, x0]
	cmp	w3, w2
	beq	.L3964
	adrp	x0, .LANCHOR2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 10, .L3964
	adrp	x0, .LC300
	add	x0, x0, :lo12:.LC300
	bl	printk
.L3964:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0, 3416]
	ldr	x1, [x0, 1120]
	ldrh	w0, [x0, 3440]
	ldrh	w1, [x1, x2, lsl 1]
	cmp	w1, w0
	beq	.L3965
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3379
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3965:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x19, 3416]
	ldr	x0, [x19, 1120]
	ldrh	w2, [x19, 3440]
	strh	w2, [x0, x1, lsl 1]
	b	.L4000
.L3963:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	ldr	x21, [x23, 1104]
	strb	w0, [x20, 264]
	adrp	x0, .LANCHOR2
	add	x21, x21, w1, uxth 2
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 8, .L3966
	ldrb	w2, [x21, 2]
	adrp	x0, .LC301
	add	x0, x0, :lo12:.LC301
	ubfx	x2, x2, 5, 3
	bl	printk
.L3966:
	ldrb	w0, [x21, 2]
	and	w1, w0, 224
	cmp	w1, 224
	beq	.L3967
	tst	w0, 192
	bne	.L3968
.L3967:
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3389
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3968:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 3416
	ldrh	w0, [x19, 3416]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19, 3416]
	ldrh	w0, [x20, 30]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 8
	bhi	.L3969
	strh	w0, [x20, 30]
	b	.L4000
.L3969:
	strh	wzr, [x20, 30]
.L4090:
	bl	flt_sys_flush
	b	.L4000
.L3880:
	cbnz	w27, .L3970
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0, 3392]
	cmp	w0, w22
	bcc	.L4000
.L3970:
	ldrh	w1, [x25, 80]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3971
	adrp	x21, .LANCHOR3
	add	x0, x21, :lo12:.LANCHOR3
	ldrb	w23, [x0, 1337]
	cmp	w23, 1
	bne	.L3971
	bl	ftl_flush
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 5
	ldrh	w0, [x0, 5608]
	cbz	w0, .L3972
	mov	w0, w23
.L4087:
	bl	zftl_gc_get_free_sblk
	and	w22, w0, 65535
	mov	w0, 65535
	cmp	w22, w0
	beq	.L3974
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x23, [x0, 1104]
	add	x23, x23, w22, uxth 2
	ldrb	w0, [x23, 2]
	tst	w0, 224
	beq	.L3975
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3423
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3975:
	ldrb	w0, [x23, 2]
	mov	w1, 5
	bfi	w0, w1, 5, 3
	orr	w0, w0, 16
	strb	w0, [x23, 2]
.L3999:
	mov	w1, 1
	mov	w0, w22
	bl	ftl_erase_sblk
	add	x21, x21, :lo12:.LANCHOR3
	mov	w0, 5
	strb	w0, [x25, 84]
	add	x1, x25, 96
	mov	w0, w22
	bl	ftl_get_blk_list_in_sblk
	and	w0, w0, 255
	ldrh	w1, [x21, 1376]
	add	x20, x20, :lo12:.LANCHOR5
	strb	w0, [x25, 89]
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x21, 1338]
	strh	w22, [x25, 80]
	mul	w0, w0, w1
	strh	wzr, [x25, 82]
	strh	w0, [x25, 86]
	mov	w1, 255
	ldrb	w0, [x21, 1321]
	strb	wzr, [x25, 85]
	strh	wzr, [x25, 90]
	mul	w2, w2, w0
	ldr	x0, [x20, 280]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrb	w0, [x21, 1321]
	mov	w1, 255
	ldrh	w2, [x21, 1338]
	mul	w2, w2, w0
	ldr	x0, [x20, 288]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrb	w0, [x21, 1321]
	mov	w1, 255
	ldrh	w2, [x21, 1338]
	mul	w2, w2, w0
	ldr	x0, [x21, 1328]
	bl	ftl_memset
	ldr	x0, [x19, 1128]
	mov	w1, -1
	str	w22, [x0, 132]
	strh	w1, [x0, 128]
	strh	w1, [x0, 130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x19, 3472]
	ldr	x0, [x19, 3384]
	strh	w22, [x0, 126]
	mov	w0, -1
	strh	wzr, [x19, 5522]
	str	w0, [x19, 5612]
	mov	w0, 0
	strh	wzr, [x19, 5524]
	strh	wzr, [x19, 5610]
	bl	ftl_info_flush
	b	.L4000
.L3972:
	mov	w0, 0
	b	.L4087
.L3971:
	cmp	w27, 1
	mov	w23, 4
	csinc	w23, w23, wzr, eq
	add	x26, x19, :lo12:.LANCHOR0
	cmp	w22, 15
	mov	w0, w23
	add	w23, w23, 4
	adrp	x22, .LANCHOR3
	csel	w23, w23, w0, ls
	add	x24, x22, :lo12:.LANCHOR3
	add	x21, x26, 3416
.L3978:
	sub	w23, w23, #1
	and	w23, w23, 255
	cmp	w23, 255
	beq	.L4000
	bl	gc_do_copy_back
	ldrb	w0, [x24, 1337]
	cbnz	w0, .L3979
	ldrb	w0, [x26, 3353]
	cmp	w0, 3
	bhi	.L3980
	bl	ftl_write_commit
.L3980:
	ldrh	w1, [x21, 26]
	ldrh	w0, [x21, 24]
	cmp	w1, w0
	bcc	.L3978
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20, 264]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w1, [x21]
	ldr	x0, [x26, 1120]
	ldrh	w0, [x0, x1, lsl 1]
	cbz	w0, .L3982
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3507
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L3982:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1, 3416]
	ldr	x1, [x1, 1120]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1, x2]
	cbnz	w1, .L3983
	bl	ftl_free_sblk
	b	.L3956
.L3983:
	mov	w2, 1
	mov	w1, 0
	bl	gc_add_sblk
	b	.L3956
.L3979:
	ldrh	w0, [x21, 2194]
	cbz	w0, .L3984
	strh	wzr, [x21, 2194]
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	w0, [x21, 2196]
	cmn	w0, #1
	beq	.L3985
	ldrb	w1, [x24, 1950]
	cbnz	w1, .L3986
	ldrb	w1, [x24, 1322]
	cbz	w1, .L3987
.L3986:
	add	x4, x19, :lo12:.LANCHOR0
	ldr	x1, [x4, 3384]
	ldr	w2, [x1, 156]
	mov	w1, 20041
	movk	w1, 0x444b, lsl 16
	cmp	w2, w1
	bne	.L3987
	add	x22, x22, :lo12:.LANCHOR3
	ldrb	w2, [x4, 1205]
	mov	w1, 24
	sub	w1, w1, w2
	ldrh	w3, [x22, 1304]
	sub	w2, w1, w3
	mov	w1, 1
	lsr	w0, w0, w3
	lsl	w1, w1, w2
	sub	w1, w1, #1
	and	w0, w1, w0
	ldrb	w1, [x22, 1306]
	ldr	x2, [x4, 1104]
	udiv	w0, w0, w1
	lsl	x0, x0, 2
	add	x5, x2, x0
	ldrb	w1, [x5, 2]
	tbz	x1, 3, .L3985
	ldrh	w3, [x4, 3368]
	ldrh	w6, [x22, 1380]
	add	w3, w3, 8
	cmp	w3, w6
	bge	.L3985
	and	w1, w1, -25
	strb	w1, [x5, 2]
	ldrh	w1, [x4, 1112]
	ldr	w3, [x2, x0]
	ldrh	w4, [x2, x0]
	ubfx	x3, x3, 11, 8
	and	w5, w4, 2047
	mul	w1, w1, w3
	lsr	w3, w3, 3
	add	w1, w1, w1, lsl 1
	add	w1, w5, w1, lsr 2
	bfi	w4, w1, 0, 11
	strh	w4, [x2, x0]
	ldr	w1, [x2, x0]
	bfi	w1, w3, 11, 8
	str	w1, [x2, x0]
.L3985:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21, 1128]
	str	wzr, [x21, 5612]
	strh	wzr, [x21, 3472]
	ldrh	w0, [x0, 80]
	bl	ftl_free_sblk
	ldr	x1, [x21, 1128]
	mov	w0, -1
	ldr	x2, [x21, 3384]
	strh	w0, [x1, 80]
	strh	w0, [x2, 126]
	strh	w0, [x1, 130]
	ldr	x0, [x21, 3424]
	cbz	x0, .L3988
	bl	zbuf_free
.L3988:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	str	xzr, [x19, 3424]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3567
	strb	wzr, [x20, 264]
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L4000
.L3987:
	bl	ftl_mask_bad_block
	b	.L3985
.L3984:
	ldrh	w0, [x25, 86]
	ldrh	w1, [x21, 26]
	cmp	w0, 1
	ldrh	w0, [x21, 24]
	bls	.L3989
	cmp	w1, w0
	bcc	.L3978
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x20, 264]
	ldrh	w0, [x21, 56]
	add	w1, w0, 1
	strh	w1, [x21, 56]
	add	x0, x21, w0, sxtw 1
	ldrh	w1, [x21]
	strh	w1, [x0, 58]
	mov	w0, -1
	strh	w0, [x21]
	b	.L4000
.L3989:
	add	x20, x20, :lo12:.LANCHOR5
	mov	w2, 5
	cmp	w1, w0
	strb	w2, [x20, 264]
	bcc	.L3990
	ldrh	w0, [x21, 56]
	add	w1, w0, 1
	strh	w1, [x21, 56]
	add	x0, x21, w0, sxtw 1
	ldrh	w1, [x21]
	strh	w1, [x0, 58]
	mov	w0, -1
	strh	w0, [x21]
.L3990:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20, 1128]
	ldrh	w1, [x25, 80]
	strh	w1, [x0, 128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20, 3432]
	add	x1, x22, :lo12:.LANCHOR3
	ldrb	w3, [x20, 1212]
	ldrh	w2, [x1, 1376]
	strh	w2, [x20, 3434]
	ldrb	w2, [x1, 1320]
	strh	w2, [x20, 3436]
	cbz	w3, .L3991
	ldrh	w1, [x1, 1338]
	strh	w1, [x20, 3434]
	mov	w1, 1
	strh	w1, [x20, 3436]
.L3991:
	cmp	w2, 2
	bne	.L3993
	add	x0, x19, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR3
	add	x0, x0, 3416
	ldrh	w1, [x0, 18]
	ubfiz	w1, w1, 1, 15
	strh	w1, [x0, 18]
	ldrb	w1, [x22, 1322]
	cbnz	w1, .L3993
	mov	w1, 1
	strh	w1, [x0, 20]
.L3993:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19, 3438]
	b	.L4000
.L3996:
	ldrh	w1, [x21, 16]
	ldrh	w0, [x21, 18]
	cmp	w1, w0
	bcc	.L3997
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 6
	strb	w0, [x20, 264]
	ldr	x0, [x21, 8]
	bl	zbuf_free
	str	xzr, [x21, 8]
	b	.L4000
.L3997:
	cmp	w22, 15
	bls	.L3883
	cmp	w27, 1
	bne	.L4000
	add	w23, w23, 1
	and	w23, w23, 255
	cmp	w23, 4
	bls	.L3883
	b	.L4000
.L3882:
	bl	gc_update_l2p_map_new
	mov	w21, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	pm_flush
	strh	w21, [x25, 80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19, 3384]
	strh	w21, [x0, 126]
	mov	w0, 0
	bl	ftl_info_flush
.L4091:
	add	x20, x20, :lo12:.LANCHOR5
	strb	wzr, [x20, 264]
	b	.L4000
.L3974:
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1040
	mov	w2, 3430
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
	b	.L3999
	.size	zftl_do_gc, .-zftl_do_gc
	.align	2
	.global	zftl_init
	.type	zftl_init, %function
zftl_init:
	stp	x29, x30, [sp, -80]!
	mov	w0, -1
	adrp	x1, .LC1
	add	x1, x1, :lo12:.LC1
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x20, .LANCHOR5
	stp	x23, x24, [sp, 48]
	adrp	x21, .LANCHOR3
	str	x25, [sp, 64]
	add	x23, x21, :lo12:.LANCHOR3
	add	x24, x19, :lo12:.LANCHOR0
	add	x25, x20, :lo12:.LANCHOR5
	adrp	x22, .LANCHOR2
	strb	w0, [x23, 1956]
	strb	w0, [x24, 3408]
	strb	w0, [x25, 360]
	mov	w0, -1
	strb	wzr, [x24, 3381]
	str	w0, [x25, 476]
	adrp	x0, .LC2
	strb	wzr, [x23, 1957]
	add	x0, x0, :lo12:.LC2
	strb	wzr, [x24, 3380]
	bl	printk
	add	x1, x22, :lo12:.LANCHOR2
	ldrb	w2, [x24, 1153]
	strb	w2, [x23, 1412]
	ldrh	w4, [x24, 2]
	ldrb	w9, [x1, 20]
	ldrh	w5, [x1, 18]
	ldrb	w3, [x1, 21]
	ldrb	w0, [x1, 17]
	ldrh	w6, [x1, 22]
	mov	w1, 0
	sdiv	w10, w5, w9
	strb	w3, [x23, 1306]
	mul	w2, w2, w3
	ubfiz	w8, w0, 9, 7
	strb	w9, [x23, 1320]
	mov	w3, 1
	and	w2, w2, 255
	strb	w0, [x23, 1946]
	strh	w5, [x23, 1338]
	strh	w10, [x23, 1376]
	strb	w2, [x23, 1321]
	strh	w6, [x24, 1096]
	strh	w8, [x25, 462]
	strh	w4, [x23, 1410]
.L4094:
	cmp	w4, w3
	bcs	.L4095
	mul	w7, w0, w5
	add	x3, x21, :lo12:.LANCHOR3
	sub	w1, w1, #1
	mov	w4, 0
	strh	w1, [x3, 1304]
	mov	w1, 1
	mul	w3, w6, w7
	lsr	w11, w3, 21
.L4096:
	cmp	w11, w1
	bcs	.L4097
	mov	w1, 57344
	add	x11, x19, :lo12:.LANCHOR0
	movk	w1, 0x1c, lsl 16
	sub	w4, w4, #1
	mul	w3, w3, w2
	lsr	w6, w6, 4
	mul	w1, w2, w1
	str	w3, [x11, 3360]
	mul	w10, w10, w2
	cmp	w6, 79
	mul	w7, w7, w2
	lsl	w1, w1, w4
	add	w3, w1, 24576
	str	w1, [x11, 1032]
	add	x4, x20, :lo12:.LANCHOR5
	sub	w7, w7, #1
	udiv	w0, w3, w0
	str	w3, [x4, 480]
	udiv	w3, w3, w7
	str	w0, [x11, 3364]
	sub	w11, w8, #1
	add	w0, w11, w0, lsl 2
	add	w3, w3, 8
	udiv	w0, w0, w8
	strh	w0, [x4, 484]
	ubfiz	w0, w0, 4, 16
	sdiv	w0, w0, w10
	strh	w0, [x4, 220]
	add	x0, x21, :lo12:.LANCHOR3
	strh	w3, [x0, 1382]
	strh	w6, [x0, 1380]
	bhi	.L4098
	mov	w3, 80
	strh	w3, [x0, 1380]
.L4098:
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 2000
	add	x6, x19, :lo12:.LANCHOR0
	mov	w4, 256
	cmp	w9, 2
	strh	w3, [x0, 1954]
	mov	w3, 50
	strh	w3, [x0, 1952]
	add	x3, x20, :lo12:.LANCHOR5
	strh	w4, [x3, 216]
	mov	w4, 48
	strh	w4, [x3, 218]
	mov	w4, 32
	strh	w4, [x6, 1112]
	beq	.L4099
	ldrb	w7, [x0, 1336]
	cbz	w7, .L4100
.L4099:
	add	x3, x21, :lo12:.LANCHOR3
	mov	w0, 150
	mov	w4, 64
	mov	w6, 12
	strh	w0, [x3, 1952]
	add	x0, x20, :lo12:.LANCHOR5
	strh	w4, [x0, 218]
	add	x4, x19, :lo12:.LANCHOR0
	strh	w6, [x4, 1112]
	ldrb	w6, [x19, #:lo12:.LANCHOR0]
	cbnz	w6, .L4101
	mov	w6, 4
	strh	w6, [x4, 1112]
	mov	w4, 600
	strh	w4, [x3, 1954]
	mov	w3, 128
	strh	w3, [x0, 216]
.L4101:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0, 1213]
	cbz	w0, .L4103
	add	x0, x21, :lo12:.LANCHOR3
	mov	w3, 200
	strh	w3, [x0, 1952]
	mov	w3, 2000
	strh	w3, [x0, 1954]
.L4103:
	add	x0, x19, :lo12:.LANCHOR0
	mul	w2, w5, w2
	mov	w3, 1
	str	wzr, [x0, 3396]
	add	x0, x20, :lo12:.LANCHOR5
	cmp	w8, w2, lsl 2
	strb	w3, [x0, 465]
	bge	.L4105
	mov	w2, 2
	strb	w2, [x0, 465]
.L4105:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4106
	adrp	x0, .LC302
	add	x0, x0, :lo12:.LC302
	bl	printk
.L4106:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4107
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0, 3360]
	adrp	x0, .LC303
	add	x0, x0, :lo12:.LC303
	bl	printk
.L4107:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4108
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x0, 3364]
	adrp	x0, .LC304
	add	x0, x0, :lo12:.LC304
	bl	printk
.L4108:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4109
	add	x0, x20, :lo12:.LANCHOR5
	ldr	w1, [x0, 480]
	adrp	x0, .LC305
	add	x0, x0, :lo12:.LC305
	bl	printk
.L4109:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4110
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w1, [x0, 484]
	adrp	x0, .LC306
	add	x0, x0, :lo12:.LC306
	bl	printk
.L4110:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4111
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w1, [x0, 462]
	adrp	x0, .LC307
	add	x0, x0, :lo12:.LC307
	bl	printk
.L4111:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4112
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w1, [x0, 220]
	adrp	x0, .LC308
	add	x0, x0, :lo12:.LC308
	bl	printk
.L4112:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4113
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 1380]
	adrp	x0, .LC309
	add	x0, x0, :lo12:.LC309
	bl	printk
.L4113:
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4114
	add	x0, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 1382]
	adrp	x0, .LC310
	add	x0, x0, :lo12:.LC310
	bl	printk
.L4114:
	add	x24, x20, :lo12:.LANCHOR5
	bl	zbuf_init
	mov	w0, 16384
	bl	ftl_malloc
	add	x25, x19, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR3
	str	x0, [x24, 392]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x24, 408]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x24, 488]
	mov	w0, 256
	bl	ftl_dma32_malloc
	str	x0, [x24, 376]
	mov	w0, 256
	bl	ftl_dma32_malloc
	ldrh	w1, [x25, 1096]
	str	x0, [x24, 416]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_dma32_malloc
	str	x0, [x25, 1040]
	ldrb	w1, [x23, 1321]
	ldrh	w0, [x23, 1376]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_dma32_malloc
	ldrb	w1, [x23, 1321]
	str	x0, [x23, 1936]
	ldrh	w0, [x23, 1376]
	ldr	x2, [x24, 408]
	str	x2, [x23, 1928]
	mul	w0, w0, w1
	ldrh	w1, [x25, 1096]
	add	x3, x2, w0, sxtw 3
	lsl	w0, w0, 1
	add	w1, w0, w1, lsr 1
	ldr	w0, [x22, #:lo12:.LANCHOR2]
	add	x1, x2, w1, sxtw 2
	str	x3, [x25, 1120]
	str	x1, [x25, 1128]
	tbz	x0, 12, .L4115
	adrp	x0, .LC311
	add	x0, x0, :lo12:.LC311
	bl	printk
.L4115:
	add	x0, x19, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR3
	ldrh	w1, [x0, 1096]
	ldrh	w0, [x21, 1376]
	ldrb	w21, [x21, 1321]
	mul	w21, w0, w21
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w23, [x0, 484]
	add	w21, w1, w21, lsl 2
	lsl	w1, w1, 2
	lsl	w21, w21, 1
	add	w21, w21, 632
	add	w23, w1, w23, lsl 2
	ldr	w1, [x22, #:lo12:.LANCHOR2]
	add	w23, w23, 704
	tbz	x1, 12, .L4116
	ldrh	w3, [x0, 462]
	mov	w2, w23
	adrp	x0, .LC312
	mov	w1, w21
	add	x0, x0, :lo12:.LC312
	bl	printk
.L4116:
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w0, [x20, 462]
	cmp	w21, w0
	bhi	.L4117
	cmp	w23, w0
	bls	.L4118
.L4117:
.L4171:
	b	.L4171
.L4095:
	add	w1, w1, 1
	lsl	w3, w3, 1
	and	w1, w1, 65535
	b	.L4094
.L4097:
	add	w4, w4, 1
	lsl	w1, w1, 1
	and	w4, w4, 65535
	b	.L4096
.L4100:
	ldrb	w6, [x6, 1212]
	cbz	w6, .L4103
	mov	w6, 1200
	strh	w4, [x0, 1952]
	strh	w6, [x0, 1954]
	strh	w4, [x3, 218]
	b	.L4103
.L4118:
	bl	sblk_init
	bl	gc_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L4093
	add	x21, x19, :lo12:.LANCHOR0
	bl	ftl_ext_info_init
	mov	w0, 1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	x0, [x21, 1128]
	mov	x1, 0
	add	x0, x0, 16
	bl	ftl_open_sblk_recovery
	ldr	x0, [x21, 1128]
	add	x1, x0, 16
	add	x0, x0, 48
	bl	ftl_open_sblk_recovery
	ldr	x1, [x21, 3384]
	ldr	w0, [x1, 8]
	add	w0, w0, 16
	str	w0, [x1, 8]
	ldr	x0, [x21, 1128]
	add	x0, x0, 16
	bl	ftl_info_data_recovery
	ldr	x0, [x21, 1128]
	add	x0, x0, 48
	bl	ftl_info_data_recovery
	ldr	x0, [x21, 1128]
	add	x0, x0, 80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	pm_flush
	mov	w0, 1
	bl	ftl_total_vpn_update
	ldrb	w0, [x20, 401]
	cbz	w0, .L4120
	ldr	x1, [x21, 3384]
	ldr	w0, [x1, 68]
	add	w0, w0, 1
	str	w0, [x1, 68]
.L4120:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x0, [x1, 1128]
	ldrh	w0, [x0, 124]
	cbnz	w0, .L4127
	ldrh	w0, [x1, 3368]
	ldrh	w1, [x1, 3372]
	add	w0, w0, w1
	cmp	w0, 7
	ble	.L4127
.L4123:
	mov	w0, 0
.L4093:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L4127:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16384
	mov	w21, 65535
.L4124:
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w1, 1
	mov	w0, w1
	bl	zftl_do_gc
	ldr	x0, [x19, 1128]
	ldrh	w1, [x0, 124]
	cbnz	w1, .L4122
	ldrh	w0, [x0, 80]
	cmp	w0, w21
	bne	.L4122
	ldrh	w0, [x19, 3368]
	ldrh	w1, [x19, 3372]
	add	w0, w0, w1
	cmp	w0, 7
	bgt	.L4123
.L4122:
	subs	w20, w20, #1
	bne	.L4124
	b	.L4123
	.size	zftl_init, .-zftl_init
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	stp	x29, x30, [sp, -48]!
	adrp	x0, jiffies
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR5
	add	x19, x21, :lo12:.LANCHOR5
	ldr	x0, [x0, #:lo12:jiffies]
	str	x0, [x19, 224]
	mov	w0, 136
	strb	wzr, [x19, 496]
	str	wzr, [x19, 248]
	bl	ftl_dma32_malloc
	str	x0, [x19, 232]
	cbnz	x0, .L4174
.L4176:
	mov	w20, -1
.L4173:
	mov	w0, w20
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L4174:
	mov	w0, 2048
	bl	ftl_dma32_malloc
	add	x1, x19, 512
	stp	x0, xzr, [x19, 504]
	str	xzr, [x19, 240]
	add	x0, x19, 240
	bl	rknand_get_reg_addr
	ldr	x0, [x19, 240]
	cbz	x0, .L4176
	bl	rk_nandc_irq_init
	ldr	x0, [x19, 504]
	mov	w3, 2048
	mov	w2, 0
	mov	w1, 0
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19, 232]
	adrp	x1, zftl_deinit
	add	x1, x1, :lo12:zftl_deinit
	str	x1, [x0, 80]
	adrp	x1, zftl_cache_flush
	add	x1, x1, :lo12:zftl_cache_flush
	str	x1, [x0, 64]
	adrp	x1, zftl_flash_suspend
	add	x1, x1, :lo12:zftl_flash_suspend
	str	x1, [x0, 88]
	adrp	x1, zftl_flash_resume
	add	x1, x1, :lo12:zftl_flash_resume
	str	x1, [x0, 96]
	adrp	x1, zftl_get_density
	add	x1, x1, :lo12:zftl_get_density
	str	x1, [x0, 72]
	adrp	x1, zftl_read_flash_info
	add	x1, x1, :lo12:zftl_read_flash_info
	str	x1, [x0, 112]
	adrp	x1, zftl_read
	add	x1, x1, :lo12:zftl_read
	str	x1, [x0, 40]
	adrp	x1, zftl_write
	add	x1, x1, :lo12:zftl_write
	str	x1, [x0, 48]
	adrp	x1, zftl_sys_read
	add	x1, x1, :lo12:zftl_sys_read
	str	x1, [x0, 24]
	adrp	x1, zftl_sys_write
	add	x1, x1, :lo12:zftl_sys_write
	str	x1, [x0, 32]
	adrp	x1, zftl_vendor_read
	add	x1, x1, :lo12:zftl_vendor_read
	str	x1, [x0, 8]
	adrp	x1, zftl_vendor_write
	add	x1, x1, :lo12:zftl_vendor_write
	str	x1, [x0, 16]
	adrp	x1, zftl_nandc_get_irq_status
	add	x1, x1, :lo12:zftl_nandc_get_irq_status
	str	x1, [x0, 120]
	adrp	x1, zftl_proc_ftl_read
	add	x1, x1, :lo12:zftl_proc_ftl_read
	str	x1, [x0, 128]
	adrp	x1, zftl_do_gc
	add	x1, x1, :lo12:zftl_do_gc
	str	x1, [x0, 104]
	adrp	x1, zftl_discard
	add	x1, x1, :lo12:zftl_discard
	str	x1, [x0, 56]
	ldr	x0, [x19, 240]
	bl	nand_flash_init
	mov	w22, w0
	cbnz	w0, .L4177
	bl	zftl_init
	mov	w20, w0
	bl	zftl_proc_debug_init
	mov	w0, 1
	strb	w0, [x19, 496]
.L4178:
	mov	w1, w20
	adrp	x0, .LC313
	add	x0, x0, :lo12:.LC313
	bl	printk
	b	.L4173
.L4177:
	ldr	x0, [x19, 232]
	adrp	x1, ftl_deinit
	add	x1, x1, :lo12:ftl_deinit
	str	x1, [x0, 80]
	adrp	x1, ftl_cache_flush
	add	x1, x1, :lo12:ftl_cache_flush
	str	x1, [x0, 64]
	adrp	x1, ftl_flash_suspend
	add	x1, x1, :lo12:ftl_flash_suspend
	str	x1, [x0, 88]
	adrp	x1, ftl_flash_resume
	add	x1, x1, :lo12:ftl_flash_resume
	str	x1, [x0, 96]
	adrp	x1, ftl_get_density
	add	x1, x1, :lo12:ftl_get_density
	str	x1, [x0, 72]
	adrp	x1, ftl_read_flash_info
	add	x1, x1, :lo12:ftl_read_flash_info
	str	x1, [x0, 112]
	adrp	x1, ftl_read
	add	x1, x1, :lo12:ftl_read
	str	x1, [x0, 40]
	adrp	x1, ftl_write
	add	x1, x1, :lo12:ftl_write
	str	x1, [x0, 48]
	adrp	x1, ftl_sys_read
	add	x1, x1, :lo12:ftl_sys_read
	str	x1, [x0, 24]
	adrp	x1, ftl_sys_write
	add	x1, x1, :lo12:ftl_sys_write
	str	x1, [x0, 32]
	adrp	x1, ftl_vendor_read
	add	x1, x1, :lo12:ftl_vendor_read
	str	x1, [x0, 8]
	adrp	x1, ftl_vendor_write
	add	x1, x1, :lo12:ftl_vendor_write
	str	x1, [x0, 16]
	adrp	x1, ftl_nandc_get_irq_status
	add	x1, x1, :lo12:ftl_nandc_get_irq_status
	str	x1, [x0, 120]
	adrp	x1, ftl_proc_ftl_read
	add	x1, x1, :lo12:ftl_proc_ftl_read
	str	x1, [x0, 128]
	adrp	x1, ftl_do_gc
	add	x1, x1, :lo12:ftl_do_gc
	str	x1, [x0, 104]
	adrp	x1, ftl_discard
	add	x1, x1, :lo12:ftl_discard
	str	x1, [x0, 56]
	ldr	x0, [x19, 240]
	bl	FlashInit
	mov	w20, w0
	cmn	w22, #2
	adrp	x22, .LANCHOR2
	bne	.L4179
	add	x19, x22, :lo12:.LANCHOR2
	mov	w2, 32
	add	x19, x19, 8
	adrp	x1, gNandParaInfo
	mov	x0, x19
	add	x1, x1, :lo12:gNandParaInfo
	bl	ftl_memcpy
	ldrb	w0, [x19, 18]
	bl	flash_lsb_page_tbl_build
	ldrh	w0, [x19, 10]
	strh	w0, [x19, 26]
.L4179:
	adrp	x0, g_nandc_version_data
	ldr	w1, [x0, #:lo12:g_nandc_version_data]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	adrp	x1, .LANCHOR0
	bne	.L4180
	adrp	x0, gFlashSlcMode
	ldrb	w0, [x0, #:lo12:gFlashSlcMode]
	cbnz	w0, .L4181
.L4180:
	add	x0, x1, :lo12:.LANCHOR0
	strb	wzr, [x1, #:lo12:.LANCHOR0]
	strb	wzr, [x0, 1154]
.L4181:
	adrp	x2, gNandFlashIDBEccBits
	add	x0, x1, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w2, [x2, #:lo12:gNandFlashIDBEccBits]
	strb	w2, [x0, 1152]
	adrp	x2, gNandFlashEccBits
	ldrh	w3, [x22, 18]
	str	xzr, [x21, 336]
	ldrb	w2, [x2, #:lo12:gNandFlashEccBits]
	strb	w2, [x0, 1249]
	mov	w0, 0
	mov	w2, 1
.L4182:
	cmp	w3, w2
	bcs	.L4183
	add	x1, x1, :lo12:.LANCHOR0
	adrp	x2, .LANCHOR3+1304
	sub	w0, w0, #1
	strh	w0, [x2, #:lo12:.LANCHOR3+1304]
	strb	wzr, [x1, 1192]
	cbnz	w20, .L4178
	bl	FtlInit
	mov	w20, w0
	b	.L4178
.L4183:
	add	w0, w0, 1
	lsl	w2, w2, 1
	and	w0, w0, 65535
	b	.L4182
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	zftl_write
	.type	zftl_write, %function
zftl_write:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	adrp	x0, .LANCHOR2
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	mov	w20, w1
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	mov	w21, w2
	stp	x23, x24, [sp, 48]
	mov	x26, x3
	stp	x27, x28, [sp, 80]
	tbz	x0, 12, .L4189
	ldr	w4, [x26]
	mov	w3, w2
	adrp	x0, .LC314
	mov	w2, w1
	add	x0, x0, :lo12:.LC314
	mov	w1, w19
	bl	printk
.L4189:
	cbnz	w19, .L4190
	adrp	x0, .LANCHOR0+1032
	mov	w19, 24576
	ldr	w0, [x0, #:lo12:.LANCHOR0+1032]
.L4191:
	cmp	w0, w20
	ccmp	w0, w21, 0, hi
	bcc	.L4209
	add	w1, w20, w21
	cmp	w0, w1
	bcc	.L4209
	adrp	x0, .LANCHOR3
	add	x0, x0, :lo12:.LANCHOR3
	add	w19, w19, w20
	sub	w23, w21, #1
	add	w23, w23, w19
	adrp	x22, .LANCHOR0
	ldrb	w1, [x0, 1946]
	add	x27, x22, :lo12:.LANCHOR0
	str	x0, [x29, 104]
	add	w0, w19, w21
	str	w0, [x29, 100]
	udiv	w25, w19, w1
	udiv	w23, w23, w1
	mov	w20, w25
	sub	w24, w23, w25
	add	w24, w24, 1
.L4193:
	cbnz	w24, .L4202
	bl	ftl_write_commit
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x0, [x1, 1128]
	ldrh	w0, [x0, 124]
	cbnz	w0, .L4203
	ldrh	w0, [x1, 3368]
	ldrh	w1, [x1, 3372]
	add	w0, w0, w1
	cmp	w0, 11
	bgt	.L4204
.L4203:
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
.L4204:
	add	x22, x22, :lo12:.LANCHOR0
.L4205:
	ldrh	w0, [x22, 3368]
	ldrh	w1, [x22, 3372]
	add	w0, w0, w1
	cmp	w0, 7
	ble	.L4206
	bl	timer_get_time
	adrp	x1, .LANCHOR5+472
	str	w0, [x1, #:lo12:.LANCHOR5+472]
	mov	w0, 0
.L4188:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L4190:
	cmp	w19, 3
	bhi	.L4209
	lsl	w19, w19, 13
	mov	w0, 8192
	b	.L4191
.L4202:
	ldrb	w0, [x27, 3381]
	cbz	w0, .L4194
	ldrb	w0, [x27, 3353]
	cmp	w0, 2
	bhi	.L4194
	bl	ftl_write_commit
.L4194:
	mov	w0, 0
	bl	buf_alloc
	mov	x28, x0
	cbnz	x0, .L4195
	bl	ftl_write_commit
	b	.L4193
.L4195:
	strb	wzr, [x0, 57]
	cmp	w20, w25
	ldr	x0, [x29, 104]
	ccmp	w20, w23, 4, ne
	ldrb	w0, [x0, 1946]
	strb	w0, [x28, 56]
	bne	.L4198
	cmp	w20, w25
	bne	.L4199
	udiv	w1, w19, w0
	msub	w1, w1, w0, w19
	and	w1, w1, 255
	strb	w1, [x28, 57]
	sub	w0, w0, w1
	and	w0, w0, 255
	cmp	w21, w0
	csel	w0, w21, w0, cc
.L4217:
	strb	w0, [x28, 56]
.L4198:
	ldrb	w4, [x28, 57]
	mov	x1, x26
	ldrb	w2, [x28, 56]
	sub	w24, w24, #1
	ldr	x0, [x28, 8]
	lsl	w2, w2, 9
	add	x0, x0, x4, lsl 9
	bl	ftl_memcpy
	ldr	x1, [x27, 3384]
	str	w20, [x28, 36]
	add	w20, w20, 1
	ldr	w0, [x1, 8]
	add	w2, w0, 1
	str	w2, [x1, 8]
	str	w0, [x28, 32]
	mov	x0, x28
	bl	ftl_write_buf
	ldrb	w0, [x28, 56]
	add	x26, x26, x0, lsl 9
	b	.L4193
.L4199:
	ldr	w1, [x29, 100]
	msub	w0, w0, w20, w1
	b	.L4217
.L4206:
	mov	w1, 1
	mov	w0, 0
	bl	zftl_do_gc
	mov	w1, 1
	mov	w0, w1
	bl	zftl_do_gc
	b	.L4205
.L4209:
	mov	w0, -1
	b	.L4188
	.size	zftl_write, .-zftl_write
	.align	2
	.global	zftl_vendor_write
	.type	zftl_vendor_write, %function
zftl_vendor_write:
	stp	x29, x30, [sp, -16]!
	mov	x3, x2
	mov	w2, w1
	add	w1, w0, 512
	add	x29, sp, 0
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
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	add	x29, sp, 0
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
	stp	x19, x20, [sp, 16]
	mov	w19, w1
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldr	w1, [x1, 1032]
	cmp	w0, w1
	ccmp	w19, w1, 2, cc
	bhi	.L4244
	add	w2, w0, w19
	cmp	w1, w2
	bcc	.L4244
	add	w23, w0, 24576
	adrp	x24, .LANCHOR5
	add	x0, x24, :lo12:.LANCHOR5
	ldr	w1, [x0, 520]
	add	w1, w19, w1
	str	w1, [x0, 520]
	adrp	x0, .LANCHOR2
	str	x0, [x29, 104]
	ldr	w2, [x0, #:lo12:.LANCHOR2]
	tbz	x2, 12, .L4224
	adrp	x0, .LC315
	mov	w4, 0
	mov	w3, w19
	mov	w2, w23
	add	x0, x0, :lo12:.LC315
	bl	printk
.L4224:
	add	x0, x21, :lo12:.LANCHOR0
	adrp	x25, .LANCHOR3
	ldr	x0, [x0, 3384]
	ldr	w27, [x0, 8]
	add	w1, w27, 1
	str	w1, [x0, 8]
	bl	ftl_write_commit
	bl	ftl_flush
	add	x0, x25, :lo12:.LANCHOR3
	ldrb	w22, [x0, 1946]
	udiv	w20, w23, w22
	msub	w26, w20, w22, w23
	cbz	w26, .L4225
	sub	w22, w22, w26
	mov	w0, w20
	cmp	w22, w19
	csel	w22, w22, w19, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29, 120]
	cmn	w0, #1
	bne	.L4226
	mov	w2, 0
	add	x1, x29, 120
	mov	w0, w20
	bl	pm_log2phys
.L4226:
	ldr	w0, [x29, 120]
	and	w28, w22, 65535
	cmn	w0, #1
	beq	.L4228
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L4228
	strb	w26, [x0, 57]
	ubfiz	x26, x26, 9, 25
	strb	w22, [x0, 56]
	mov	w1, 0
	ldr	x0, [x0, 8]
	lsl	w2, w28, 9
	stp	w27, w20, [x3, 32]
	str	x3, [x29, 96]
	add	x0, x0, x26
	bl	ftl_memset
	ldr	x3, [x29, 96]
	mov	x0, x3
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldr	w0, [x1, 76]
	add	w0, w0, 1
	str	w0, [x1, 76]
.L4228:
	add	w20, w20, 1
	sub	w19, w19, w28
.L4225:
	cbz	w19, .L4230
	bl	ftl_flush
.L4230:
	add	x22, x25, :lo12:.LANCHOR3
	add	x26, x21, :lo12:.LANCHOR0
	mov	w0, -1
	mov	w28, 1
	str	w0, [x29, 124]
.L4231:
	ldrb	w0, [x22, 1946]
	cmp	w19, w0
	bcs	.L4236
	cbz	w19, .L4238
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29, 120]
	cmn	w0, #1
	bne	.L4239
	mov	w2, 0
	add	x1, x29, 120
	mov	w0, w20
	bl	pm_log2phys
.L4239:
	ldr	w0, [x29, 120]
	cmn	w0, #1
	beq	.L4238
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L4238
	add	x25, x25, :lo12:.LANCHOR3
	strb	wzr, [x0, 57]
	strb	w19, [x0, 56]
	stp	w27, w20, [x22, 32]
	ldrb	w0, [x25, 1946]
	cmp	w19, w0
	bcc	.L4241
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 1056
	mov	w2, 1496
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	printk
	bl	dump_stack
.L4241:
	ldr	x0, [x22, 8]
	lsl	w2, w19, 9
	mov	w1, 0
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0, 3384]
	ldr	w0, [x1, 76]
	add	w0, w0, 1
	str	w0, [x1, 76]
.L4238:
	add	x0, x24, :lo12:.LANCHOR5
	ldr	w1, [x0, 520]
	cmp	w1, 8192
	bls	.L4245
	ldr	x0, [x29, 104]
	ldr	w0, [x0, #:lo12:.LANCHOR2]
	tbz	x0, 12, .L4242
	adrp	x0, .LC315
	mov	w4, 0
	mov	w3, w19
	mov	w2, w23
	add	x0, x0, :lo12:.LC315
	bl	printk
.L4242:
	add	x24, x24, :lo12:.LANCHOR5
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x24, 520]
	bl	flt_sys_flush
	mov	w0, 1
	str	w0, [x21, 3396]
.L4245:
	mov	w0, 0
	b	.L4222
.L4236:
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29, 120]
	cmn	w0, #1
	beq	.L4232
	mov	w0, 0
	bl	buf_alloc
	mov	x3, x0
	cbz	x0, .L4234
	ldrb	w2, [x22, 1946]
	mov	w1, 0
	strb	w2, [x0, 56]
	strb	wzr, [x0, 57]
	ldr	x0, [x0, 8]
	lsl	w2, w2, 9
	stp	w27, w20, [x3, 32]
	str	x3, [x29, 96]
	bl	ftl_memset
	ldr	x3, [x29, 96]
	mov	x0, x3
	bl	ftl_write_buf
	bl	ftl_write_commit
.L4271:
	ldr	x1, [x26, 3384]
	ldr	w0, [x1, 76]
	add	w0, w0, 1
	str	w0, [x1, 76]
.L4234:
	ldrb	w0, [x22, 1946]
	add	w20, w20, 1
	sub	w19, w19, w0
	b	.L4231
.L4232:
	mov	w2, 0
	add	x1, x29, 120
	mov	w0, w20
	bl	pm_log2phys
	ldr	w0, [x29, 120]
	cmn	w0, #1
	beq	.L4234
	add	x1, x29, 124
	mov	w2, 1
	mov	w0, w20
	bl	pm_log2phys
	ldrb	w1, [x26, 1205]
	mov	w0, 24
	ldrh	w2, [x22, 1304]
	sub	w0, w0, w1
	ldr	w1, [x29, 120]
	sub	w0, w0, w2
	lsr	w1, w1, w2
	lsl	w0, w28, w0
	sub	w0, w0, #1
	and	w0, w0, w1
	ldrb	w1, [x22, 1306]
	udiv	w0, w0, w1
	bl	ftl_vpn_decrement
	b	.L4271
.L4244:
	mov	w0, -1
.L4222:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 128
	ret
	.size	zftl_discard, .-zftl_discard
	.align	2
	.global	dump_pm_blk
	.type	dump_pm_blk, %function
dump_pm_blk:
	stp	x29, x30, [sp, -48]!
	mov	w2, 4
	adrp	x0, .LC267
	add	x0, x0, :lo12:.LC267
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR5
	add	x21, x21, :lo12:.LANCHOR5
	mov	w20, 0
	mov	w22, 65535
	ldr	x1, [x19, 3384]
	add	x1, x1, 704
	ldrh	w3, [x1, -6]
	bl	rknand_print_hex
	ldrh	w3, [x21, 220]
	adrp	x0, .LC268
	ldr	x1, [x19, 3384]
	mov	w2, 2
	add	x0, x0, :lo12:.LC268
	add	x1, x1, 416
	bl	rknand_print_hex
.L4273:
	ldrh	w0, [x21, 220]
	cmp	w0, w20
	bhi	.L4275
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
.L4275:
	ldr	x0, [x19, 3384]
	add	x0, x0, w20, sxtw 1
	ldrh	w0, [x0, 416]
	cmp	w0, w22
	beq	.L4274
	mov	x1, 0
	bl	ftl_sblk_dump
.L4274:
	add	w20, w20, 1
	and	w20, w20, 65535
	b	.L4273
	.size	dump_pm_blk, .-dump_pm_blk
	.align	2
	.global	id_block_prog_msb_ff_data
	.type	id_block_prog_msb_ff_data, %function
id_block_prog_msb_ff_data:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp, 32]
	adrp	x21, .LANCHOR0
	stp	x23, x24, [sp, 48]
	and	w24, w0, 255
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp, 16]
	stp	x25, x26, [sp, 64]
	and	w19, w2, 65535
	str	x27, [sp, 80]
	ldrb	w2, [x0, 1154]
	cbnz	w2, .L4277
	ldr	x0, [x0, 1144]
	ldrb	w0, [x0, 19]
	sub	w0, w0, #5
	and	w0, w0, 255
	cmp	w0, 63
	bhi	.L4277
	mov	x2, 16391
	movk	x2, 0x4000, lsl 16
	movk	x2, 0x8000, lsl 48
	lsr	x0, x2, x0
	tbz	x0, 0, .L4277
	adrp	x20, .LANCHOR3
	add	x20, x20, :lo12:.LANCHOR3
	adrp	x23, .LC316
	mov	w25, w1
	add	x20, x20, 2476
	add	x23, x23, :lo12:.LC316
.L4279:
	add	x26, x21, :lo12:.LANCHOR0
	ldr	x0, [x26, 1144]
	ldrh	w0, [x0, 10]
	cmp	w0, w19
	bhi	.L4280
.L4277:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldr	x27, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
.L4280:
	sxtw	x22, w19
	add	w27, w19, w25
	mov	w1, w19
	mov	w2, w27
	mov	x0, x23
	ldrh	w3, [x20, x22, lsl 1]
	bl	printk
	ldrh	w1, [x20, x22, lsl 1]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L4277
	adrp	x22, .LANCHOR5
	add	x22, x22, :lo12:.LANCHOR5
	mov	w2, 16384
	mov	w1, 255
	add	w19, w19, 1
	ldr	x0, [x22, 304]
	and	w19, w19, 65535
	bl	ftl_memset
	ldr	x0, [x26, 1144]
	mov	w1, w27
	ldr	x3, [x22, 304]
	mov	x2, x3
	ldrb	w4, [x0, 9]
	mov	w0, w24
	bl	flash_prog_page
	b	.L4279
	.size	id_block_prog_msb_ff_data, .-id_block_prog_msb_ff_data
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	stp	x29, x30, [sp, -304]!
	mov	w4, 35899
	movk	w4, 0xfcdc, lsl 16
	add	x29, sp, 0
	stp	x23, x24, [sp, 48]
	adrp	x23, .LANCHOR0
	stp	x19, x20, [sp, 16]
	ldrb	w3, [x23, #:lo12:.LANCHOR0]
	str	w3, [x29, 160]
	stp	x21, x22, [sp, 32]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ldrh	w3, [x29, 160]
	str	w3, [x29, 212]
	ldr	w3, [x1]
	cmp	w3, w4
	mov	w4, 19282
	movk	w4, 0x534e, lsl 16
	ccmp	w3, w4, 4, ne
	beq	.L4283
	add	w4, w4, 327680
	cmp	w3, w4
	beq	.L4283
.L4401:
	mov	w0, -1
.L4282:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 304
	ret
.L4283:
	cmp	w0, 15
	bls	.L4401
	add	x25, x23, :lo12:.LANCHOR0
	mov	w19, w0
	str	x2, [x29, 168]
	str	x1, [x29, 216]
	ldr	x0, [x25, 1144]
	ldrb	w20, [x0, 9]
	ldrh	w24, [x0, 10]
	adrp	x0, .LANCHOR2+34
	ldrh	w21, [x0, #:lo12:.LANCHOR2+34]
	ldrb	w0, [x25, 1154]
	str	w0, [x29, 144]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x22, x0
	cbz	x0, .L4401
	adrp	x0, .LANCHOR5+496
	ldrb	w0, [x0, #:lo12:.LANCHOR5+496]
	cbz	w0, .L4340
	ldrb	w1, [x23, #:lo12:.LANCHOR0]
	ldrb	w0, [x25, 1154]
	cbz	w1, .L4287
	cmp	w0, 0
	cset	w0, eq
.L4398:
	str	w0, [x29, 236]
	b	.L4286
.L4287:
	cmp	w0, 3
	beq	.L4341
	ldr	w2, [x29, 212]
	cmp	w0, 2
	mov	w1, 2
	mov	w0, 3
	csel	w1, w2, w1, ne
	csel	w0, wzr, w0, ne
	str	w1, [x29, 212]
	b	.L4398
.L4340:
	str	wzr, [x29, 236]
.L4286:
	add	w19, w19, 511
	lsr	w19, w19, 9
	cmp	w19, 8
	bls	.L4343
	cmp	w19, 500
	bhi	.L4289
.L4288:
	ldr	x0, [x29, 216]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	ldr	w0, [x0]
	cmp	w0, w1
	mov	w1, 19282
	movk	w1, 0x534e, lsl 16
	ccmp	w0, w1, 4, ne
	beq	.L4290
	add	w1, w1, 327680
	cmp	w0, w1
	beq	.L4290
.L4289:
	mov	x0, x22
	bl	ftl_free
	b	.L4401
.L4341:
	str	w0, [x29, 212]
	mov	w0, 2
	b	.L4398
.L4343:
	mov	w19, 8
	b	.L4288
.L4290:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	add	x0, x23, :lo12:.LANCHOR0
	mul	w24, w24, w20
	mov	w4, 17739
	mov	w2, 63871
	movk	w4, 0x4e52, lsl 16
	ldrb	w0, [x0, 1154]
	strb	w0, [x23, #:lo12:.LANCHOR0]
	sub	w0, w24, #1
	add	w0, w0, w19
	udiv	w0, w0, w24
	str	w0, [x29, 132]
	ldr	x0, [x29, 216]
	add	x3, x0, 253952
	mov	w0, 0
	add	x3, x3, 1532
.L4296:
	ldr	w1, [x3]
	cbnz	w1, .L4291
	cbnz	w0, .L4292
	str	w4, [x3, 512]
.L4293:
	add	w0, w0, 1
	sub	w2, w2, #1
	cmp	w0, 4095
	sub	x3, x3, #4
	csel	w0, w0, wzr, cc
	cmp	w2, 4096
	bne	.L4296
.L4295:
	ldr	x1, [x29, 168]
	mul	w0, w21, w20
	mov	w3, 5
	mov	w2, 4
	str	w0, [x29, 164]
	adrp	x0, .LC318
	add	x0, x0, :lo12:.LC318
	bl	rknand_print_hex
	mov	w2, w19
	mov	w1, w19
	adrp	x0, .LC319
	add	x0, x0, :lo12:.LC319
	bl	printk
	str	xzr, [x29, 224]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 8
	str	wzr, [x29, 232]
	str	x0, [x29, 136]
.L4337:
	adrp	x27, .LANCHOR5
	add	x1, x27, :lo12:.LANCHOR5
	ldr	w0, [x29, 224]
	ldrb	w1, [x1, 496]
	cbnz	w1, .L4297
	ldr	x2, [x29, 224]
	cmp	w0, 0
	ldr	x1, [x29, 168]
	ldr	w1, [x1, x2, lsl 2]
	ldr	w2, [x29, 164]
	mul	w21, w2, w1
	ldr	w2, [x29, 132]
	ccmp	w2, 1, 0, ne
	bls	.L4298
	ldr	x2, [x29, 168]
	sub	w0, w0, #1
	ldr	w0, [x2, x0, lsl 2]
	add	w0, w0, 1
	cmp	w1, w0
	bne	.L4298
.L4299:
	ldr	x0, [x29, 224]
	add	x0, x0, 1
	str	x0, [x29, 224]
	cmp	x0, 4
	bne	.L4337
.L4339:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR0
	ldrb	w2, [x29, 160]
	add	x1, x0, :lo12:.LANCHOR0
	strb	w2, [x0, #:lo12:.LANCHOR0]
	ldr	w2, [x29, 236]
	cbz	w2, .L4338
	ldrb	w2, [x29, 144]
	strb	w2, [x1, 1154]
.L4338:
	add	x0, x0, :lo12:.LANCHOR0
	mov	w1, 2
	strb	w1, [x0, 1208]
	mov	w0, 0
	bl	zftl_flash_enter_slc_mode
	mov	x0, x22
	bl	ftl_free
	ldr	w0, [x29, 232]
	cmp	w0, 0
	csetm	w0, eq
	b	.L4282
.L4292:
	ldr	x1, [x29, 216]
	ldr	w1, [x1, w0, uxtw 2]
	str	w1, [x3, 512]
	b	.L4293
.L4291:
	adrp	x0, .LC317
	add	x0, x0, :lo12:.LC317
	bl	printk
	b	.L4295
.L4297:
	adrp	x20, .LANCHOR0
	add	x23, x20, :lo12:.LANCHOR0
	ldr	w2, [x29, 224]
	ldr	x1, [x23, 1048]
	add	x1, x1, w2, uxtw
	ldrb	w21, [x1, 32]
	mov	x1, x20
	cmp	w21, 255
	beq	.L4299
	ldr	w2, [x29, 164]
	mul	w21, w21, w2
	ldr	w2, [x29, 236]
	cbz	w2, .L4298
	and	w0, w0, -3
	cmp	w0, 1
	beq	.L4300
	cmp	w2, 3
	bne	.L4301
.L4300:
	add	x2, x1, :lo12:.LANCHOR0
	ldrb	w0, [x29, 212]
	strb	w0, [x1, #:lo12:.LANCHOR0]
	strb	w0, [x2, 1154]
.L4298:
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0, 1144]
	ldrb	w24, [x1, 9]
	ldrh	w23, [x1, 10]
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w25, [x1, 34]
	mul	w23, w23, w24
	ldrb	w1, [x0, 1252]
	str	w1, [x29, 128]
	udiv	w1, w21, w24
	strb	wzr, [x0, 1252]
	mov	w0, 0
	mul	w25, w25, w24
	bl	flash_erase_block
	cmp	w23, w19
	bcs	.L4344
	add	w1, w21, w25
	mov	w0, 0
	bl	flash_erase_block
	mov	w1, 2
.L4302:
	add	x28, x20, :lo12:.LANCHOR0
	ldr	x0, [x28, 1144]
	ldrh	w23, [x0, 10]
	ldrb	w0, [x0, 12]
	lsl	w23, w23, 2
	mul	w23, w23, w1
	sdiv	w0, w23, w0
	str	w0, [x29, 176]
	udiv	w0, w21, w25
	msub	w0, w0, w25, w21
	str	w0, [x29, 208]
	sub	w26, w21, w0
	cmp	w21, w26
	bne	.L4346
	ldrb	w0, [x28, 1028]
	cmp	w0, 9
	bne	.L4346
	add	x27, x27, :lo12:.LANCHOR5
	mov	w2, 1024
	mov	w1, 0
	ldr	x27, [x27, 352]
	mov	x0, x27
	bl	ftl_memset
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	str	w0, [x27]
	mov	w0, 12
	str	w0, [x27, 4]
	ldrb	w0, [x28, 1154]
	strb	wzr, [x27, 16]
	str	wzr, [x27, 12]
	cbz	w0, .L4304
	ldr	x0, [x28, 1144]
	ldrb	w0, [x0, 29]
	strb	w0, [x27, 16]
.L4304:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, 4
	strb	w0, [x27, 17]
	cmp	w24, 8
	sub	w28, w19, #4
	ldr	x1, [x20, 1144]
	ldrh	w0, [x1, 10]
	ldrb	w1, [x1, 12]
	strb	wzr, [x27, 20]
	strh	wzr, [x27, 22]
	sdiv	w0, w0, w1
	mov	w1, 16
	strh	w0, [x27, 18]
	mov	w0, 70
	csel	w0, w0, w1, hi
	strb	w0, [x27, 21]
	mov	w1, 12
	add	x0, x27, 12
	bl	js_hash
	str	w0, [x27, 8]
.L4303:
	ldr	x20, [x29, 216]
	adrp	x0, .LANCHOR0
	add	x25, x0, :lo12:.LANCHOR0
	mov	w23, 0
	add	x0, x25, 4
	str	x0, [x29, 200]
.L4306:
	ldr	w0, [x29, 176]
	cmp	w0, w23
	bhi	.L4317
	ldr	x0, [x29, 136]
	mov	x23, x22
	mov	w24, 4
	mov	w25, 0
	stp	w28, wzr, [x29, 192]
	ldrb	w0, [x0, 9]
	str	w0, [x29, 156]
	ldr	x0, [x29, 136]
	ldr	w1, [x29, 156]
	ldrh	w0, [x0, 26]
	mul	w0, w0, w1
	adrp	x1, .LANCHOR0
	add	x20, x1, :lo12:.LANCHOR0
	udiv	w1, w21, w0
	strb	wzr, [x20, 1252]
	msub	w0, w1, w0, w21
	str	w0, [x29, 208]
	sub	w0, w21, w0
	str	w0, [x29, 152]
	ldr	w0, [x29, 208]
	and	w0, w0, 3
	str	w0, [x29, 200]
	add	x0, x20, 4
	str	x0, [x29, 104]
.L4318:
	ldr	w0, [x29, 192]
	cmp	w25, w0
	bcc	.L4332
	adrp	x0, .LANCHOR0
	add	x20, x0, :lo12:.LANCHOR0
	ldrb	w0, [x29, 128]
	strb	w0, [x20, 1252]
	ldr	w0, [x29, 236]
	cbz	w0, .L4333
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	adrp	x0, .LANCHOR0
	strb	wzr, [x20, 1154]
	strb	wzr, [x0, #:lo12:.LANCHOR0]
.L4333:
	lsl	w0, w28, 7
	mov	x1, 0
.L4334:
	cmp	w0, w1
	bhi	.L4335
	ldr	w0, [x29, 232]
	add	w0, w0, 1
	str	w0, [x29, 232]
	cmp	w0, 5
	bls	.L4299
	b	.L4339
.L4301:
	mov	w0, 0
	bl	zftl_flash_exit_slc_mode
	strb	wzr, [x20, #:lo12:.LANCHOR0]
	strb	wzr, [x23, 1154]
	b	.L4298
.L4344:
	mov	w1, 1
	b	.L4302
.L4346:
	mov	w28, w19
	mov	x27, 0
	b	.L4303
.L4317:
	ldr	w0, [x29, 208]
	ldrb	w2, [x25, 1154]
	add	w8, w0, w23
	ldr	x0, [x29, 200]
	ubfx	x8, x8, 2, 16
	add	w1, w8, 1
	ldrh	w0, [x0, w1, sxtw 1]
	cbz	w2, .L4308
	ldrb	w4, [x25, 1]
	lsl	w0, w1, 1
	cmp	w4, 0
	csel	w0, w0, w1, ne
.L4308:
	ldrb	w1, [x25, 1028]
	cmp	w1, 9
	bne	.L4310
.L4399:
	str	w0, [x29, 240]
	mov	w0, 61424
	str	w0, [x29, 244]
	ldr	x0, [x29, 200]
	ldrh	w0, [x0, w8, sxtw 1]
	cbnz	w2, .L4312
	mov	w8, w0
.L4313:
	mul	w0, w24, w8
	cbnz	x27, .L4314
	ldr	w5, [x20]
	mov	x4, x20
	ldr	w6, [x29, 240]
	mov	w3, w19
	add	w27, w0, w26
	mov	w7, 61424
	str	w8, [x29, 196]
	mov	w2, w23
	mov	w1, w27
	adrp	x0, .LC320
	add	x0, x0, :lo12:.LC320
	bl	printk
	add	x2, x29, 240
	mov	x1, x20
	mov	w0, w27
	bl	fw_flash_page_prog.constprop.29
	ldrb	w0, [x25, 1154]
	ldr	w8, [x29, 196]
	cbnz	w0, .L4315
	udiv	w1, w26, w24
	add	w2, w8, 1
	bl	id_block_prog_msb_ff_data
.L4315:
	add	w0, w28, 16
	add	x20, x20, 2048
	cmp	w23, w0
	bcc	.L4316
	ldr	x0, [x29, 216]
	add	w1, w28, 20
	cmp	w23, w1
	add	x0, x0, 2048
	csel	x20, x0, x20, cc
.L4316:
	add	w9, w23, 4
	mov	x27, 0
	and	w23, w9, 65535
	b	.L4306
.L4310:
	sub	w0, w0, #1
	lsl	w0, w0, 2
	b	.L4399
.L4312:
	ldrb	w1, [x25, 1]
	lsl	w0, w8, 1
	cmp	w1, 0
	csel	w8, w0, w8, ne
	b	.L4313
.L4314:
	add	x2, x29, 240
	mov	x1, x27
	add	w0, w0, w26
	bl	fw_flash_page_prog.constprop.29
	b	.L4316
.L4332:
	ldr	w0, [x29, 200]
	ldr	x1, [x29, 104]
	sub	w0, w24, w0
	ldrb	w2, [x20, 1154]
	and	w0, w0, 65535
	str	w0, [x29, 176]
	ldr	w0, [x29, 208]
	add	w0, w0, w25
	udiv	w0, w0, w24
	and	w0, w0, 65535
	ldrh	w1, [x1, w0, sxtw 1]
	cbnz	w2, .L4319
	mov	w0, w1
.L4320:
	ldp	w2, w1, [x29, 152]
	ldrb	w27, [x20, 1152]
	madd	w0, w1, w0, w2
	str	w0, [x29, 148]
	ldr	w0, [x29, 200]
	ldr	w1, [x29, 148]
	add	w26, w0, w1
	ldr	x0, [x20, 1144]
	ldrb	w0, [x0, 9]
	udiv	w26, w26, w0
	ldrb	w0, [x20, 1249]
	str	w0, [x29, 124]
	mov	w0, w27
	bl	nandc_bch_sel
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	x0, [x29, 112]
.L4321:
	mov	w4, w24
	add	x3, x29, 240
	mov	x2, x23
	mov	w1, w26
	mov	w0, 0
	bl	flash_read_page
	mov	w5, w0
	cmn	w0, #1
	bne	.L4322
	ldrb	w6, [x20, 1252]
	cbnz	w6, .L4323
.L4326:
	ldr	x0, [x29, 112]
	ldr	x6, [x0, 336]
	cbnz	x6, .L4324
.L4325:
	ldrb	w0, [x20, 1192]
	cbz	w0, .L4322
	mov	w4, w24
	add	x3, x29, 240
	mov	x2, x23
	mov	w1, w26
	mov	w0, 0
	bl	flash_ddr_tuning_read
	b	.L4400
.L4319:
	ldrb	w2, [x20, 1]
	lsl	w1, w0, 1
	cmp	w2, 0
	csel	w0, w1, w0, ne
	b	.L4320
.L4323:
	str	w6, [x29, 100]
	mov	w4, w24
	str	w5, [x29, 120]
	add	x3, x29, 240
	strb	wzr, [x20, 1252]
	mov	x2, x23
	mov	w1, w26
	mov	w0, 0
	bl	flash_read_page
	cmn	w0, #1
	ldr	w6, [x29, 100]
	strb	w6, [x20, 1252]
	ldr	w5, [x29, 120]
	beq	.L4326
.L4400:
	mov	w5, w0
.L4322:
	cmn	w5, #1
	cset	w4, eq
	cmp	w27, 16
	cset	w0, ne
	tst	w4, w0
	beq	.L4328
	mov	w0, 16
	mov	w27, 16
	bl	nandc_bch_sel
	b	.L4321
.L4324:
	str	w5, [x29, 120]
	mov	w4, w24
	add	x3, x29, 240
	mov	x2, x23
	mov	w1, w26
	mov	w0, 0
	blr	x6
	cmn	w0, #1
	ldr	w5, [x29, 120]
	beq	.L4325
	b	.L4400
.L4328:
	ldr	w0, [x29, 124]
	bl	nandc_bch_sel
	cmp	w4, 0
	ldr	w0, [x29, 196]
	csinv	w0, w0, wzr, eq
	str	w0, [x29, 196]
	ldr	w0, [x29, 152]
	cmp	w25, 0
	ccmp	w21, w0, 0, eq
	bne	.L4330
	ldr	w0, [x29, 196]
	cbnz	w0, .L4330
	ldr	w0, [x23]
	mov	w1, 18766
	movk	w1, 0x464e, lsl 16
	cmp	w0, w1
	bne	.L4330
	ldr	w0, [x29, 192]
	ldr	w1, [x29, 176]
	ldrb	w24, [x23, 17]
	add	w0, w0, w1
	str	w0, [x29, 192]
.L4331:
	ldr	w0, [x29, 176]
	add	w25, w0, w25
	and	w25, w25, 65535
	b	.L4318
.L4330:
	ldr	x0, [x29, 176]
	mov	w2, w25
	ldr	w1, [x29, 148]
	ldp	w3, w4, [x29, 240]
	ubfiz	x0, x0, 9, 16
	add	x23, x23, x0
	adrp	x0, .LC321
	add	x0, x0, :lo12:.LC321
	bl	printk
	str	wzr, [x29, 200]
	b	.L4331
.L4335:
	ldr	x2, [x29, 216]
	ldr	w3, [x22, x1, lsl 2]
	add	x1, x1, 1
	add	x2, x2, x1, lsl 2
	ldr	w2, [x2, -4]
	cmp	w3, w2
	beq	.L4334
	mov	w2, 512
	mov	w1, 0
	mov	x0, x22
	bl	ftl_memset
	mov	w1, w21
	mov	w0, 0
	bl	flash_erase_block
	b	.L4299
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	stp	x29, x30, [sp, -112]!
	cmp	w0, 64
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	w19, w0
	stp	x21, x22, [sp, 32]
	adrp	x20, .LANCHOR5
	stp	x23, x24, [sp, 48]
	mov	w21, w1
	mov	x24, x2
	bne	.L4403
	ldr	w0, [x2]
	mov	w1, 35899
	movk	w1, 0xfcdc, lsl 16
	cmp	w0, w1
	mov	w1, 19282
	movk	w1, 0x534e, lsl 16
	ccmp	w0, w1, 4, ne
	beq	.L4404
	add	w1, w1, 327680
	cmp	w0, w1
	bne	.L4403
.L4404:
	add	x22, x20, :lo12:.LANCHOR5
	mov	w0, 1
	strb	w0, [x22, 524]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	w2, 59392
	mov	w1, 0
	movk	w2, 0x3, lsl 16
	str	x0, [x22, 528]
	bl	ftl_memset
	mov	w0, 64
	str	w0, [x22, 536]
.L4403:
	add	x23, x20, :lo12:.LANCHOR5
	ldr	w2, [x24]
	mov	w4, w21
	mov	w3, w19
	adrp	x0, .LC322
	add	x0, x0, :lo12:.LC322
	ldr	x1, [x23, 528]
	bl	printk
	ldrb	w0, [x23, 524]
	cbz	w0, .L4402
	ldr	x22, [x23, 528]
	cbz	x22, .L4402
	sub	w0, w19, #64
	cmp	w0, 499
	bhi	.L4406
	mov	w2, 564
	sub	w2, w2, w19
	cmp	w21, w2
	ubfiz	x0, x0, 9, 25
	csel	w2, w21, w2, ls
	mov	x1, x24
	lsl	w2, w2, 9
	add	x0, x22, x0
	bl	ftl_memcpy
.L4407:
	add	x23, x20, :lo12:.LANCHOR5
	ldr	w0, [x23, 536]
	cmp	w19, w0
	beq	.L4416
	strb	wzr, [x23, 524]
	mov	x0, x22
	bl	ftl_free
	str	xzr, [x23, 528]
	b	.L4416
.L4406:
	cmp	w19, 563
	bls	.L4407
	ldr	w0, [x23, 536]
	mov	w1, 500
	sub	w0, w0, #64
	cmp	w0, 500
	csel	w0, w0, w1, ls
	adrp	x1, .LANCHOR0+1144
	ldr	x1, [x1, #:lo12:.LANCHOR0+1144]
	ldrb	w1, [x1, 9]
	cmp	w1, 4
	beq	.L4408
	mov	w1, 2
	str	w1, [x29, 72]
	mov	w1, 3
	str	w1, [x29, 76]
	mov	w1, 4
	str	w1, [x29, 80]
	mov	w1, 5
	str	w1, [x29, 84]
	mov	w1, 6
	str	w1, [x29, 88]
.L4409:
	add	x2, x22, 245760
	mov	w1, 61952
	add	x2, x2, 2048
.L4415:
	ldr	w3, [x2]
	cbz	w3, .L4413
	add	w0, w1, 2048
	lsl	w0, w0, 2
.L4414:
	mov	x1, x22
	add	x22, x20, :lo12:.LANCHOR5
	add	x2, x29, 72
	bl	write_idblock
	ldr	x0, [x22, 528]
	strb	wzr, [x22, 524]
	bl	ftl_free
	str	xzr, [x22, 528]
.L4416:
	add	x20, x20, :lo12:.LANCHOR5
	add	w19, w19, w21
	str	w19, [x20, 536]
.L4402:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 112
	ret
.L4408:
	add	x2, x29, 72
	mov	x1, 0
.L4412:
	cmp	w0, 256
	bls	.L4410
	lsl	w3, w1, 1
	str	w3, [x2, x1, lsl 2]
.L4411:
	add	x1, x1, 1
	cmp	x1, 5
	bne	.L4412
	b	.L4409
.L4410:
	str	w1, [x2, x1, lsl 2]
	b	.L4411
.L4413:
	sub	w1, w1, #1
	sub	x2, x2, #4
	cmp	w1, 4096
	bne	.L4415
	lsl	w0, w0, 9
	b	.L4414
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	and	w20, w0, 255
	stp	x21, x22, [sp, 32]
	sub	w0, w1, #64
	mov	w19, w1
	mov	w21, w2
	mov	x22, x3
	cmp	w0, 1983
	bhi	.L4428
	cbnz	w20, .L4428
	mov	x2, x3
	mov	w1, w21
	mov	w0, w19
	bl	write_loader_lba
.L4428:
	adrp	x0, .LANCHOR5+232
	mov	x3, x22
	mov	w2, w21
	mov	w1, w19
	ldr	x0, [x0, #:lo12:.LANCHOR5+232]
	ldr	x4, [x0, 48]
	mov	w0, w20
	blr	x4
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L4434
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	rknand_dev_flush
	mov	x1, 0
	adrp	x0, .LC323
	add	x0, x0, :lo12:.LC323
	bl	printk
	mov	x0, 0
	ldp	x29, x30, [sp], 16
	ret
.L4434:
	mov	x0, -22
	ret
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	stp	x29, x30, [sp, -16]!
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	mov	w1, -1
	add	x29, sp, 0
	str	w1, [x0, 536]
	strb	wzr, [x0, 524]
	str	xzr, [x0, 528]
	str	xzr, [x0, 544]
	bl	rknand_sys_storage_init
	ldp	x29, x30, [sp], 16
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	adrp	x19, .LANCHOR5
	add	x20, x19, :lo12:.LANCHOR5
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	ldr	x0, [x20, 552]
	cbnz	x0, .L4443
	mov	w0, 65536
	bl	ftl_malloc
	str	x0, [x20, 552]
.L4443:
	add	x19, x19, :lo12:.LANCHOR5
	ldr	x0, [x19, 552]
	cbz	x0, .L4448
	adrp	x23, .LC324
	mov	w25, 22084
	add	x23, x23, :lo12:.LC324
	mov	w24, 0
	mov	w22, 0
	mov	w21, 0
	movk	w25, 0x524b, lsl 16
.L4446:
	ldr	x2, [x19, 552]
	mov	w1, 128
	lsl	w0, w21, 7
	bl	FlashBootVendorRead
	cbnz	w0, .L4449
	ldr	x0, [x19, 552]
	add	x1, x0, 61440
	ldr	w3, [x0, 4]
	ldr	w2, [x1, 4092]
	ldr	w1, [x0]
	mov	x0, x23
	bl	printk
	ldr	x20, [x19, 552]
	ldr	w0, [x20]
	cmp	w0, w25
	bne	.L4445
	add	x0, x20, 61440
	ldr	w1, [x20, 4]
	ldr	w0, [x0, 4092]
	cmp	w0, w1
	bne	.L4445
	cmp	w0, w22
	bls	.L4445
	mov	w24, w21
	mov	w22, w0
.L4445:
	add	w21, w21, 1
	cmp	w21, 2
	bne	.L4446
	cbz	w22, .L4447
	mov	x2, x20
	mov	w1, 128
	lsl	w0, w24, 7
	bl	FlashBootVendorRead
	cmp	w0, 0
	csetm	w0, ne
.L4442:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	ret
.L4447:
	mov	w1, 0
	mov	x2, 65536
	mov	x0, x20
	bl	memset
	mov	w1, 22084
	mov	w0, 1
	movk	w1, 0x524b, lsl 16
	stp	w1, w0, [x20]
	add	x1, x20, 61440
	str	w0, [x1, 4092]
	mov	w0, -1032
	strh	w0, [x20, 14]
	mov	w0, 0
	b	.L4442
.L4448:
	mov	w0, -12
	b	.L4442
.L4449:
	mov	w0, -1
	b	.L4442
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	adrp	x3, .LANCHOR5+552
	ldr	x4, [x3, #:lo12:.LANCHOR5+552]
	cbz	x4, .L4457
	ldrh	w6, [x4, 10]
	add	x5, x4, 16
	mov	w3, 0
.L4454:
	cmp	w3, w6
	bcc	.L4456
.L4457:
	mov	w0, -1
	ret
.L4456:
	ldrh	w7, [x5], 8
	cmp	w7, w0
	bne	.L4455
	stp	x29, x30, [sp, -32]!
	add	x3, x4, w3, uxtw 3
	mov	x0, x1
	add	x29, sp, 0
	str	x19, [sp, 16]
	ldrh	w19, [x3, 20]
	ldrh	w1, [x3, 18]
	cmp	w19, w2
	csel	w19, w19, w2, ls
	add	x1, x1, 1024
	uxtw	x2, w19
	add	x1, x4, x1
	bl	memcpy
	mov	w0, w19
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
.L4455:
	add	w3, w3, 1
	b	.L4454
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	stp	x29, x30, [sp, -112]!
	adrp	x3, .LANCHOR5+552
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	ldr	x19, [x3, #:lo12:.LANCHOR5+552]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	cbz	x19, .L4477
	add	w4, w2, 63
	ldrh	w3, [x19, 10]
	ldrh	w24, [x19, 8]
	mov	x28, x1
	mov	w26, w2
	and	w22, w4, -64
	add	x1, x19, 16
	mov	w20, 0
.L4464:
	cmp	w20, w3
	bcc	.L4472
	ldrh	w1, [x19, 14]
	cmp	w22, w1
	bhi	.L4477
	add	x3, x19, w3, uxth 3
	strh	w0, [x3, 16]
	and	w0, w22, 65535
	ldrh	w2, [x19, 12]
	strh	w2, [x3, 18]
	strh	w26, [x3, 20]
	add	w2, w2, w0
	sub	w0, w1, w0
	strh	w2, [x19, 12]
	strh	w0, [x19, 14]
	uxtw	x2, w26
	mov	x1, x28
	ldrh	w0, [x3, 18]
	add	x0, x0, 1024
	add	x0, x19, x0
	bl	memcpy
	ldrh	w0, [x19, 10]
	add	w0, w0, 1
	strh	w0, [x19, 10]
	b	.L4479
.L4472:
	ldrh	w6, [x1], 8
	cmp	w6, w0
	bne	.L4465
	uxtw	x23, w20
	add	x5, x19, 1024
	add	x21, x19, x23, lsl 3
	ldrh	w25, [x21, 20]
	add	w25, w25, 63
	and	w25, w25, -64
	cmp	w26, w25
	bls	.L4466
	ldrh	w0, [x19, 14]
	cmp	w22, w0
	bhi	.L4477
	add	x23, x23, 2
	ldrh	w21, [x21, 18]
	add	x23, x19, x23, lsl 3
	sub	w3, w3, #1
.L4467:
	cmp	w20, w3
	bcc	.L4468
	add	x20, x19, w20, uxtw 3
	and	w21, w21, 65535
	add	x0, x5, w21, uxth
	uxtw	x2, w26
	mov	x1, x28
	strh	w21, [x20, 18]
	strh	w6, [x20, 16]
	strh	w26, [x20, 20]
	bl	memcpy
	ldrh	w0, [x19, 14]
	and	w4, w22, 65535
	add	w21, w21, w4
	strh	w21, [x19, 12]
	sub	w0, w0, w4
	add	w25, w0, w25
	strh	w25, [x19, 14]
.L4479:
	ldr	w0, [x19, 4]
	add	x1, x19, 61440
	mov	x2, x19
	add	w0, w0, 1
	str	w0, [x19, 4]
	str	w0, [x1, 4092]
	mov	w1, 128
	ldrh	w0, [x19, 8]
	add	w0, w0, 1
	and	w0, w0, 65535
	cmp	w0, 1
	csel	w0, w0, wzr, ls
	strh	w0, [x19, 8]
	lsl	w0, w24, 7
	bl	FlashBootVendorWrite
	mov	w0, 0
.L4462:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 112
	ret
.L4468:
	add	w20, w20, 1
	stp	w3, w6, [x29, 96]
	add	x0, x19, w20, uxtw 3
	str	x5, [x29, 104]
	add	x23, x23, 8
	ldrh	w1, [x0, 16]
	strh	w1, [x23, -8]
	ldrh	w1, [x0, 20]
	strh	w1, [x23, -4]
	strh	w21, [x23, -6]
	ldrh	w27, [x0, 20]
	ldrh	w1, [x0, 18]
	add	x0, x5, w21, uxtw
	add	w27, w27, 63
	and	w27, w27, -64
	add	x1, x5, x1
	and	x2, x27, 131008
	bl	memcpy
	add	w21, w21, w27
	ldr	x5, [x29, 104]
	ldp	w3, w6, [x29, 96]
	b	.L4467
.L4466:
	ldrh	w0, [x21, 18]
	uxtw	x2, w26
	mov	x1, x28
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21, 20]
	b	.L4479
.L4465:
	add	w20, w20, 1
	b	.L4464
.L4477:
	mov	w0, -1
	b	.L4462
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	stp	x29, x30, [sp, -48]!
	mov	w0, 9216
	add	x29, sp, 0
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	str	x21, [sp, 32]
	mov	w21, w1
	bl	ftl_malloc
	cbz	x0, .L4488
	mov	w1, 30209
	mov	x19, x0
	movk	w1, 0x4004, lsl 16
	cmp	w21, w1
	beq	.L4483
	add	w1, w1, 1
	cmp	w21, w1
	beq	.L4484
.L4494:
	mov	x20, -14
	b	.L4482
.L4483:
	mov	x2, 8
	mov	x1, x20
	bl	rk_copy_from_user
	cbnz	x0, .L4494
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L4486
.L4487:
	mov	x20, -1
.L4482:
	mov	x0, x19
	bl	kfree
.L4480:
	mov	x0, x20
	ldr	x21, [sp, 32]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 48
	ret
.L4486:
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L4487
	strh	w0, [x19, 6]
	and	x0, x0, 65535
	add	x2, x0, 8
	mov	x1, x19
	mov	x0, x20
	bl	rk_copy_to_user
	cbnz	x0, .L4494
	mov	x20, 0
	b	.L4482
.L4484:
	mov	x2, 8
	mov	x1, x20
	bl	rk_copy_from_user
	cbnz	x0, .L4494
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L4487
	ldrh	w2, [x19, 6]
	cmp	w2, 4087
	bhi	.L4487
	add	w2, w2, 8
	mov	x1, x20
	sxtw	x2, w2
	mov	x0, x19
	bl	rk_copy_from_user
	cbnz	x0, .L4494
	ldrh	w2, [x19, 6]
	add	x1, x19, 8
	ldrh	w0, [x19, 4]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L4482
.L4488:
	mov	x20, -1
	b	.L4480
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
	.data
	.align	3
	.set	.LANCHOR2,. + 0
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
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 4352
	.set	.LANCHOR5,. + 8704
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
	.type	gp_flash_info, %object
	.size	gp_flash_info, 8
gp_flash_info:
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
	.zero	2
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
	.type	g_nandc_ecc_bits, %object
	.size	g_nandc_ecc_bits, 1
g_nandc_ecc_bits:
	.zero	1
	.type	g_flash_multi_page_prog_en, %object
	.size	g_flash_multi_page_prog_en, 1
g_flash_multi_page_prog_en:
	.zero	1
	.type	nandc_hw_seed, %object
	.size	nandc_hw_seed, 1
nandc_hw_seed:
	.zero	1
	.type	nandc_randomizer_en, %object
	.size	nandc_randomizer_en, 1
nandc_randomizer_en:
	.zero	1
	.zero	3
	.type	g_nandc_v6_master_info, %object
	.size	g_nandc_v6_master_info, 40
g_nandc_v6_master_info:
	.zero	40
	.type	fill_spare_size, %object
	.size	fill_spare_size, 2
fill_spare_size:
	.zero	2
	.zero	6
	.type	g_buf, %object
	.size	g_buf, 2048
g_buf:
	.zero	2048
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
	.zero	2
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
	.type	write_commit_count, %object
	.size	write_commit_count, 1
write_commit_count:
	.zero	1
	.type	write_buf_count, %object
	.size	write_buf_count, 1
write_buf_count:
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
	.zero	2
	.type	_gc_after_discard_en, %object
	.size	_gc_after_discard_en, 4
_gc_after_discard_en:
	.zero	4
	.type	gc_slc_mode_tlc_vpn_th, %object
	.size	gc_slc_mode_tlc_vpn_th, 2
gc_slc_mode_tlc_vpn_th:
	.zero	2
	.type	gc_slc_mode_vpn_th, %object
	.size	gc_slc_mode_vpn_th, 2
gc_slc_mode_vpn_th:
	.zero	2
	.type	gc_tlc_mode_slc_vpn_th, %object
	.size	gc_tlc_mode_slc_vpn_th, 2
gc_tlc_mode_slc_vpn_th:
	.zero	2
	.type	gc_tlc_mode_tlc_vpn_th, %object
	.size	gc_tlc_mode_tlc_vpn_th, 2
gc_tlc_mode_tlc_vpn_th:
	.zero	2
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
	.type	_c_ftl_sec_per_page, %object
	.size	_c_ftl_sec_per_page, 1
_c_ftl_sec_per_page:
	.zero	1
	.zero	1
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
	.zero	2
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
	.zero	2
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
	.type	ftl_dma32_buffer_size, %object
	.size	ftl_dma32_buffer_size, 4
ftl_dma32_buffer_size:
	.zero	4
	.zero	4
	.type	ftl_dma32_buffer, %object
	.size	ftl_dma32_buffer, 8
ftl_dma32_buffer:
	.zero	8
	.type	gc_state, %object
	.size	gc_state, 1
gc_state:
	.zero	1
	.zero	3
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
	.type	g_flash_spare_buffer, %object
	.size	g_flash_spare_buffer, 8
g_flash_spare_buffer:
	.zero	8
	.type	g_flash_page_buffer, %object
	.size	g_flash_page_buffer, 8
g_flash_page_buffer:
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
	.zero	7
	.type	ftl_info_spare, %object
	.size	ftl_info_spare, 8
ftl_info_spare:
	.zero	8
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
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 8
ftl_tmp_spare:
	.zero	8
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
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 8
g_idb_buffer:
	.zero	8
	.type	g_vendor, %object
	.size	g_vendor, 8
g_vendor:
	.zero	8
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.zero	4
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.zero	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.zero	4
	.zero	4
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 8
gpVendor1Info:
	.zero	8
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 8
gpVendor0Info:
	.zero	8
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.zero	512
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 8
gpDrmKeyInfo:
	.zero	8
	.type	gpBootConfig, %object
	.size	gpBootConfig, 8
gpBootConfig:
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
	.section	.rodata
	.align	3
	.set	.LANCHOR1,. + 0
	.type	__func__.46344, %object
	.size	__func__.46344, 18
__func__.46344:
	.string	"_list_remove_node"
	.zero	6
	.type	__func__.46369, %object
	.size	__func__.46369, 23
__func__.46369:
	.string	"_list_update_data_list"
	.zero	1
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
	.type	__func__.26714, %object
	.size	__func__.26714, 22
__func__.26714:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.26187, %object
	.size	__func__.26187, 28
__func__.26187:
	.string	"flash_wait_device_ready_raw"
	.zero	4
	.type	__func__.26251, %object
	.size	__func__.26251, 22
__func__.26251:
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
	.type	__func__.26276, %object
	.size	__func__.26276, 23
__func__.26276:
	.string	"flash_start_plane_read"
	.zero	1
	.type	__func__.26162, %object
	.size	__func__.26162, 26
__func__.26162:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.26173, %object
	.size	__func__.26173, 21
__func__.26173:
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
	.type	__func__.26979, %object
	.size	__func__.26979, 13
__func__.26979:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.26992, %object
	.size	__func__.26992, 10
__func__.26992:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.27006, %object
	.size	__func__.27006, 16
__func__.27006:
	.string	"buf_remove_free"
	.type	zftl_debug_proc_fops, %object
	.size	zftl_debug_proc_fops, 96
zftl_debug_proc_fops:
	.zero	8
	.xword	zftl_debug_proc_open
	.xword	seq_read
	.zero	8
	.xword	zftl_debug_proc_write
	.xword	seq_lseek
	.xword	single_release
	.zero	40
	.type	__func__.45877, %object
	.size	__func__.45877, 12
__func__.45877:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.45969, %object
	.size	__func__.45969, 19
__func__.45969:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.46575, %object
	.size	__func__.46575, 18
__func__.46575:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.46585, %object
	.size	__func__.46585, 17
__func__.46585:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.46706, %object
	.size	__func__.46706, 23
__func__.46706:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.46746, %object
	.size	__func__.46746, 22
__func__.46746:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.46757, %object
	.size	__func__.46757, 22
__func__.46757:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.46026, %object
	.size	__func__.46026, 16
__func__.46026:
	.string	"gc_free_src_blk"
	.type	__func__.45620, %object
	.size	__func__.45620, 14
__func__.45620:
	.string	"ftl_write_buf"
	.zero	2
	.type	__func__.45665, %object
	.size	__func__.45665, 18
__func__.45665:
	.string	"zftl_add_read_buf"
	.align	3
	.set	.LANCHOR4,. + 0
	.type	__func__.47179, %object
	.size	__func__.47179, 21
__func__.47179:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.26704, %object
	.size	__func__.26704, 20
__func__.26704:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.45552, %object
	.size	__func__.45552, 17
__func__.45552:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.46444, %object
	.size	__func__.46444, 20
__func__.46444:
	.string	"zftl_sblk_list_init"
	.zero	4
	.type	__func__.47111, %object
	.size	__func__.47111, 13
__func__.47111:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.27042, %object
	.size	__func__.27042, 21
__func__.27042:
	.string	"flash_info_data_init"
	.zero	3
	.type	__func__.26800, %object
	.size	__func__.26800, 11
__func__.26800:
	.string	"nandc_init"
	.zero	5
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
	.type	__func__.26085, %object
	.size	__func__.26085, 19
__func__.26085:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.26211, %object
	.size	__func__.26211, 26
__func__.26211:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.26222, %object
	.size	__func__.26222, 29
__func__.26222:
	.string	"flash_start_3d_mlc_page_prog"
	.zero	3
	.type	__func__.26240, %object
	.size	__func__.26240, 22
__func__.26240:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.26264, %object
	.size	__func__.26264, 31
__func__.26264:
	.string	"flash_complete_plane_page_read"
	.zero	1
	.type	__func__.26289, %object
	.size	__func__.26289, 25
__func__.26289:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.46944, %object
	.size	__func__.46944, 31
__func__.46944:
	.string	"queue_wait_first_req_completed"
	.zero	1
	.type	__func__.46998, %object
	.size	__func__.46998, 15
__func__.46998:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.47025, %object
	.size	__func__.47025, 15
__func__.47025:
	.string	"sblk_read_page"
	.zero	1
	.type	__func__.45924, %object
	.size	__func__.45924, 21
__func__.45924:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.26146, %object
	.size	__func__.26146, 19
__func__.26146:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.46821, %object
	.size	__func__.46821, 14
__func__.46821:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.46478, %object
	.size	__func__.46478, 15
__func__.46478:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.46783, %object
	.size	__func__.46783, 19
__func__.46783:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.46795, %object
	.size	__func__.46795, 18
__func__.46795:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.46533, %object
	.size	__func__.46533, 15
__func__.46533:
	.string	"ftl_alloc_sblk"
	.zero	1
	.type	__func__.47151, %object
	.size	__func__.47151, 17
__func__.47151:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.47161, %object
	.size	__func__.47161, 14
__func__.47161:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.27064, %object
	.size	__func__.27064, 17
__func__.27064:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.27027, %object
	.size	__func__.27027, 20
__func__.27027:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.26759, %object
	.size	__func__.26759, 16
__func__.26759:
	.string	"nand_flash_init"
	.type	__func__.46648, %object
	.size	__func__.46648, 16
__func__.46648:
	.string	"ftl_sysblk_dump"
	.type	__func__.46673, %object
	.size	__func__.46673, 23
__func__.46673:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.47095, %object
	.size	__func__.47095, 16
__func__.47095:
	.string	"load_l2p_region"
	.type	__func__.47135, %object
	.size	__func__.47135, 6
__func__.47135:
	.string	"pm_gc"
	.zero	2
	.type	__func__.45867, %object
	.size	__func__.45867, 12
__func__.45867:
	.string	"gc_recovery"
	.zero	4
	.type	__func__.45956, %object
	.size	__func__.45956, 22
__func__.45956:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.46070, %object
	.size	__func__.46070, 16
__func__.46070:
	.string	"gc_scan_src_blk"
	.type	__func__.46131, %object
	.size	__func__.46131, 20
__func__.46131:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.46194, %object
	.size	__func__.46194, 18
__func__.46194:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.46624, %object
	.size	__func__.46624, 14
__func__.46624:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.45698, %object
	.size	__func__.45698, 10
__func__.45698:
	.string	"zftl_read"
	.zero	6
	.type	__func__.46727, %object
	.size	__func__.46727, 19
__func__.46727:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.45602, %object
	.size	__func__.45602, 17
__func__.45602:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.45999, %object
	.size	__func__.45999, 16
__func__.45999:
	.string	"gc_do_copy_back"
	.type	__func__.46249, %object
	.size	__func__.46249, 11
__func__.46249:
	.string	"zftl_do_gc"
	.zero	5
	.type	__func__.45730, %object
	.size	__func__.45730, 13
__func__.45730:
	.string	"_ftl_discard"
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC1:
	.string	"FTL version: 6.0.24 20210716"
.LC2:
	.string	"%s\n"
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
	.string	"nand sdr mode %x\n"
.LC35:
	.string	"nand ddr mode %x\n"
.LC36:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC37:
	.string	"otp:%x %x %x %x\n"
.LC38:
	.string	"bad block test:%x %x\n"
.LC39:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC40:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC41:
	.string	"flash_erase_block %x %x %x\n"
.LC42:
	.string	"flash_erase_block %d block = %x status = %x\n"
.LC43:
	.string	"erase done: %x\n"
.LC44:
	.string	"sblk_queue_head = %d\n"
.LC45:
	.string	"sblk_read_completed_queue_head = %d\n"
.LC46:
	.string	"sblk_gc_write_completed_queue_head = %d\n"
.LC47:
	.string	"sblk_write_completed_queue_head = %d\n"
.LC48:
	.string	"p_free_buf_head = %d\n"
.LC49:
	.string	"free_buf_count = %d\n"
.LC50:
	.string	"buf = %d, next=%d, flag=%d gc_write_flag=%d, lun_state=%d, op_status = %d lpa=%x, ppa=%x\n"
.LC51:
	.string	"flash_mask_bad_block %d %d\n"
.LC52:
	.string	"zftl_debug"
.LC53:
	.string	"FLASH ID: %x %x %x %x %x %x\n"
.LC54:
	.string	"density: %d MB\n"
.LC55:
	.string	"device density: %d MB\n"
.LC56:
	.string	"FTL INFO:\n"
.LC57:
	.string	"max_lpn = 0x%x\n"
.LC58:
	.string	"density = 0x%x\n"
.LC59:
	.string	"slc vpn = 0x%x\n"
.LC60:
	.string	"xlc vpn = 0x%x\n"
.LC61:
	.string	"free slc blk = 0x%x\n"
.LC62:
	.string	"free xlc blk = 0x%x\n"
.LC63:
	.string	"free mix blk = 0x%x\n"
.LC64:
	.string	"slc data blk = 0x%x\n"
.LC65:
	.string	"slc cache blk = 0x%x\n"
.LC66:
	.string	"xlc data blk = 0x%x\n"
.LC67:
	.string	"free buf = %d, %d, %d\n"
.LC68:
	.string	"bad blk = %d %d\n"
.LC69:
	.string	"TBW = %d MB\n"
.LC70:
	.string	"TBR = %d MB\n"
.LC71:
	.string	"POC = %d\n"
.LC72:
	.string	"PLC = %d\n"
.LC73:
	.string	"sys run time = %d S\n"
.LC74:
	.string	"slc mode = %x %x %x\n"
.LC75:
	.string	"prog err = %d\n"
.LC76:
	.string	"read err = %d\n"
.LC77:
	.string	"GC XLC page = %d\n"
.LC78:
	.string	"GC SLC page = %d\n"
.LC79:
	.string	"discard page = 0x%x\n"
.LC80:
	.string	"version = %d\n"
.LC81:
	.string	"acblk = 0x%x %d %d\n"
.LC82:
	.string	"tmblk = 0x%x %d %d\n"
.LC83:
	.string	"gcblk = 0x%x %d %d\n"
.LC84:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC85:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC86:
	.string	"gc free blk th = %d\n"
.LC87:
	.string	"gc vpn th = %d %d %d %d %d\n"
.LC88:
	.string	"swl blk = %x %x %x %x\n"
.LC89:
	.string	"rf info = %x %x %x %x %x\n"
.LC90:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d, %d\n"
.LC91:
	.string	"gc_add_sblk = %d, %d, %d\n"
.LC92:
	.string	"gc_add_sblk = %d, %d, %d,last update:%d, %d\n"
.LC93:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC94:
	.string	"gc_mark_bad_ppa %d %x %x\n"
.LC95:
	.string	"status: %x, ppa: %x\n"
.LC96:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC97:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC98:
	.string	"zftl_get_gc_node cache = %x index = %d vpn = %x\n"
.LC99:
	.string	"gc_search_src_blk mode = %x, src mode = %x, count= %d %d\n"
.LC100:
	.string	"swl_tlc_free_mini_ec_blk alloc sblk %x\n"
.LC101:
	.string	"zftl_get_free_sblk %x %d, %p %d %d\n"
.LC102:
	.string	"zftl_gc_get_free_sblk %x %x %x, %d %d %d\n"
.LC103:
	.string	"swl_slc_free_mini_ec_blk alloc sblk %x\n"
.LC104:
	.string	"list count:%p %d\n"
.LC105:
	.string	"%d: node:%x %x %x %x, %d %d %d %d %d\n"
.LC106:
	.string	"ftl_vpn_decrement %x = %d, %d\n"
.LC107:
	.string	"mask bad block:cs %x %x block: %x %x\n"
.LC108:
	.string	"gc_free_bad_sblk 0x%x\n"
.LC109:
	.string	"swl_slc_free_mini_ec_blk sblk %x\n"
.LC110:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC111:
	.string	"gc_free_src_blk %x, %d\n"
.LC112:
	.string	"bad blk = %x, %x free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, max_vpn: %x\n"
.LC113:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC114:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC115:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC116:
	.string	"gc th: tlc_tlc: %d tlc_slc: %d slc_slc: %d slc_tlc:%d free_th: %d\n"
.LC117:
	.string	"swl : %x %x %x %x %x %x\n"
.LC118:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC119:
	.string	"ftl re prog: lpa = %x, ppa= %x\n"
.LC120:
	.string	"dump_sblk_queue: %d\n"
.LC121:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC122:
	.string	"%s %d %d\n"
.LC123:
	.string	"gc_static_wearleveling: min blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC124:
	.string	"gc_static_wearleveling: min slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC125:
	.string	"gc_static_wearleveling: min tlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC126:
	.string	"gc_static_wearleveling: max slc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC127:
	.string	"gc_static_wearleveling: max xlc blk: %x,sec=%d,xec = %d ,mode=%d, func=%x, bbt=%x vpn = %d\n"
.LC128:
	.string	"gc_static_wearleveling: slc blk: %x, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC129:
	.string	"gc_static_wearleveling: min slc ec: %x, min tlc ec: %d max slc ec: %d, max tlc ec: %d; %d %d\n"
.LC130:
	.string	"swl add tlc gc = %x, %d, %d, %d, %d, %d\n"
.LC131:
	.string	"swl add slc gc  = %x, %d, %d, %d, %d, %d\n"
.LC132:
	.string	"free blk vpn error: %x %x\n"
.LC133:
	.string	"GC PM block %x %x %x %d\n"
.LC134:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC135:
	.string	"...%d @ %s\n"
.LC136:
	.string	"...%s enter... %p\n"
.LC137:
	.string	"0:%x %x %x %x %x\n"
.LC138:
	.string	"g_nandc_ver...%d\n"
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
	.string	"return ret = %lx\n"
.LC324:
	.string	"\0013vendor storage %x,%x,%x\n"
