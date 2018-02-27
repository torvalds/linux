/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-02-27
 * function: rk ftl v5 for rockchip soc base on arm v8 to support 3D/2D
 *	     TLC and MLC.
 */
	.cpu generic+fp+simd
	.file	"rk_zftl_arm_v8.S"
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
	.global	zftl_flash_suspend
	.type	zftl_flash_suspend, %function
zftl_flash_suspend:
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	cmp	w1, 9
	ldr	x1, [x0,8]
	ldr	w2, [x1]
	str	w2, [x0,16]
	ldr	w2, [x1,4]
	str	w2, [x0,20]
	bne	.L16
	ldr	w2, [x1,16]
	str	w2, [x0,24]
	ldr	w2, [x1,32]
	str	w2, [x0,28]
	ldr	w2, [x1,80]
	str	w2, [x0,32]
	ldr	w2, [x1,84]
	str	w2, [x0,36]
	ldr	w2, [x1,520]
	str	w2, [x0,40]
	ldr	w1, [x1,8]
	b	.L18
.L16:
	ldr	w2, [x1,8]
	str	w2, [x0,24]
	ldr	w2, [x1,12]
	str	w2, [x0,28]
	ldr	w2, [x1,304]
	str	w2, [x0,32]
	ldr	w2, [x1,308]
	str	w2, [x0,36]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,40]
.L18:
	str	w1, [x0,44]
	ret
	.size	zftl_flash_suspend, .-zftl_flash_suspend
	.align	2
	.type	nandc_irq_disable, %function
nandc_irq_disable:
	adrp	x2, .LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	cmp	w2, 9
	mov	x2, 1
	bne	.L20
	ldr	w3, [x0,292]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,292]
	ldr	w2, [x0,288]
	bic	w1, w2, w1
	str	w1, [x0,288]
	b	.L19
.L20:
	ldr	w3, [x0,368]
	lsl	x1, x2, x1
	orr	w2, w3, w1
	str	w2, [x0,368]
	ldr	w2, [x0,364]
	bic	w1, w2, w1
	str	w1, [x0,364]
.L19:
	ret
	.size	nandc_irq_disable, .-nandc_irq_disable
	.align	2
	.global	nand_flash_print_info
	.type	nand_flash_print_info, %function
nand_flash_print_info:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LC0
	adrp	x1, .LANCHOR1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR1
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC0
	bl	printk
	ldr	x6, [x19,48]
	adrp	x0, .LC1
	add	x0, x0, :lo12:.LC1
	ldrb	w3, [x6,3]
	ldrb	w4, [x6,4]
	ldrb	w5, [x6,5]
	ldrb	w2, [x6,2]
	ldrb	w1, [x6,1]
	ldrb	w6, [x6,6]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	ldrb	w1, [x1,8]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC3
	add	x0, x0, :lo12:.LC3
	ldrb	w1, [x1,9]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	ldrh	w1, [x1,10]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC5
	add	x0, x0, :lo12:.LC5
	ldrb	w1, [x1,12]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC6
	add	x0, x0, :lo12:.LC6
	ldrb	w1, [x1,13]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC7
	add	x0, x0, :lo12:.LC7
	ldrh	w1, [x1,14]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC8
	add	x0, x0, :lo12:.LC8
	ldrb	w1, [x1,23]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC9
	add	x0, x0, :lo12:.LC9
	ldrb	w1, [x1,18]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC10
	add	x0, x0, :lo12:.LC10
	ldrb	w1, [x1,19]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC11
	add	x0, x0, :lo12:.LC11
	ldrb	w1, [x1,20]
	bl	printk
	ldrb	w1, [x19,56]
	adrp	x0, .LC12
	add	x0, x0, :lo12:.LC12
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC13
	add	x0, x0, :lo12:.LC13
	ldrb	w1, [x1,21]
	bl	printk
	ldr	x1, [x19,48]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrb	w1, [x1,22]
	bl	printk
	ldrb	w1, [x19,57]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC16
	add	x0, x0, :lo12:.LC16
	and	w1, w1, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	ubfx	x1, x1, 1, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	ubfx	x1, x1, 2, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC19
	add	x0, x0, :lo12:.LC19
	ubfx	x1, x1, 3, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC20
	add	x0, x0, :lo12:.LC20
	ubfx	x1, x1, 4, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC21
	add	x0, x0, :lo12:.LC21
	ubfx	x1, x1, 5, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC22
	add	x0, x0, :lo12:.LC22
	ubfx	x1, x1, 6, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC23
	add	x0, x0, :lo12:.LC23
	ubfx	x1, x1, 7, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrb	w1, [x0,17]
	adrp	x0, .LC24
	add	x0, x0, :lo12:.LC24
	and	w1, w1, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC25
	add	x0, x0, :lo12:.LC25
	ubfx	x1, x1, 9, 1
	bl	printk
	ldr	x0, [x19,48]
	ldrh	w1, [x0,16]
	adrp	x0, .LC26
	add	x0, x0, :lo12:.LC26
	ubfx	x1, x1, 10, 1
	bl	printk
	ldrb	w1, [x19,58]
	adrp	x0, .LC27
	add	x0, x0, :lo12:.LC27
	bl	printk
	ldrb	w1, [x19,74]
	adrp	x0, .LC28
	ldrb	w2, [x19,75]
	add	x0, x0, :lo12:.LC28
	bl	printk
	ldrb	w2, [x19,73]
	adrp	x0, .LC29
	ldrb	w1, [x19,72]
	add	x0, x0, :lo12:.LC29
	bl	printk
	ldrb	w1, [x19,96]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	bl	printk
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nand_flash_print_info, .-nand_flash_print_info
	.align	2
	.global	nandc_init
	.type	nandc_init, %function
nandc_init:
	stp	x29, x30, [sp, -64]!
	adrp	x1, .LANCHOR1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	add	x1, x1, :lo12:.LANCHOR1
	mov	x21, x0
	adrp	x0, .LC31
	add	x1, x1, 24
	add	x0, x0, :lo12:.LC31
	mov	x2, x21
	stp	x19, x20, [sp,16]
	str	wzr, [x29,56]
	adrp	x19, .LANCHOR0
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 6
	strb	w1, [x19,#:lo12:.LANCHOR0]
	ldr	w1, [x21,352]
	str	x21, [x0,8]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L24
	mov	w0, 8
	strb	w0, [x19,#:lo12:.LANCHOR0]
.L24:
	ldr	w1, [x21,128]
	mov	w0, 12336
	movk	w0, 0x5639, lsl 16
	cmp	w1, w0
	bne	.L25
	mov	w0, 9
	strb	w0, [x19,#:lo12:.LANCHOR0]
.L25:
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	add	x20, x19, :lo12:.LANCHOR0
	adrp	x22, .LC32
	cmp	w0, 9
	bne	.L26
	ldr	w1, [x29,56]
	mov	w0, 1
	strb	w0, [x20,97]
	orr	w1, w1, 256
	str	w1, [x29,56]
	mov	w1, 4225
	ldr	w0, [x29,56]
	str	w0, [x21]
	ldr	x0, [x20,8]
	str	wzr, [x0,520]
	str	w1, [x0,4]
	mov	w0, 8321
	ldr	x5, [x20,8]
	str	w0, [x5,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x5,80]
	mov	w0, 38
	str	w0, [x5,84]
	mov	w0, 39
	str	w0, [x5,84]
	ldr	w1, [x5]
	ldr	w2, [x5,8]
	ldr	w3, [x5,80]
	ldr	w4, [x5,84]
	ldr	w5, [x5,88]
	b	.L28
.L26:
	ldr	w1, [x29,56]
	strb	wzr, [x20,97]
	orr	w1, w1, 256
	str	w1, [x29,56]
	mov	w1, 4225
	ldr	w0, [x29,56]
	str	w0, [x21]
	ldr	x0, [x20,8]
	str	wzr, [x0,336]
	str	w1, [x0,4]
	mov	w0, 8321
	ldr	x1, [x20,8]
	str	w0, [x1,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x1,304]
	mov	w0, 38
	str	w0, [x1,308]
	mov	w0, 39
	str	w0, [x1,308]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x20,104]
	ldr	x5, [x20,8]
	ldr	w1, [x5]
	ldr	w2, [x5,344]
	ldr	w3, [x5,304]
	ldr	w4, [x5,308]
	ldr	w5, [x5,312]
.L28:
	add	x0, x22, :lo12:.LC32
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	strh	wzr, [x0,146]
	strb	w1, [x0,144]
	strb	wzr, [x0,148]
	adrp	x0, .LC33
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	add	x0, x0, :lo12:.LC33
	bl	printk
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
.L30:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L30
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
	add	x24, x2, 40
	add	x21, x2, 88
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
.L34:
	add	x25, x26, :lo12:.LANCHOR0
	ldrb	w0, [x25,150]
	cmp	w0, w20
	bls	.L40
	add	x0, x19, 40
	str	w27, [x22,8]
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	timer_delay_ns
	ldrb	w0, [x25,149]
	cmp	w0, 34
	bne	.L35
	ldrsb	w0, [x24,x20]
	b	.L39
.L35:
	cmp	w0, 35
	bne	.L37
	ldrsb	w0, [x21,x20]
	b	.L39
.L37:
	ldrsb	w0, [x23,184]
.L39:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L34
.L40:
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
	ldr	x4, [x3,152]
	ldr	x19, [x3,8]
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
.L42:
	cmp	w22, w21, uxtb
	bls	.L44
	ldrb	w0, [x23,x21]
	str	w0, [x19,2052]
	mov	w0, 120
	bl	timer_delay_ns
	ldrsb	w0, [x20,x21]
	add	x21, x21, 1
	str	w0, [x19,2048]
	b	.L42
.L44:
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
	ldrb	w1, [x19,149]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 6
	bhi	.L45
	ldr	x1, [x19,152]
	sxtw	x20, w0
	add	x1, x1, x20
	ldrb	w1, [x1,120]
	cbz	w1, .L45
	mov	w1, 0
	bl	hynix_set_rr_para
	ldr	x0, [x19,152]
	add	x20, x0, x20
	strb	wzr, [x20,120]
.L45:
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
	ldr	x2, [x2,8]
	cmp	w1, 9
	lsl	w1, w0, 16
	lsl	w0, w0, 8
	orr	w0, w1, w0
	orr	w0, w0, 3
	bne	.L51
	str	w0, [x2,80]
	b	.L50
.L51:
	str	w0, [x2,304]
.L50:
	ret
	.size	nandc_set_ddr_para, .-nandc_set_ddr_para
	.align	2
	.global	nandc_get_ddr_para
	.type	nandc_get_ddr_para, %function
nandc_get_ddr_para:
	adrp	x1, .LANCHOR0
	add	x0, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,#:lo12:.LANCHOR0]
	ldr	x0, [x0,8]
	cmp	w1, 9
	bne	.L54
	ldr	w0, [x0,80]
	b	.L56
.L54:
	ldr	w0, [x0,304]
.L56:
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
	ldr	x2, [x1,8]
	ldr	w1, [x2]
	beq	.L58
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
	bne	.L60
	str	w0, [x2,8]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,80]
	mov	w0, 38
	str	w0, [x2,84]
	mov	w0, 39
	str	w0, [x2,84]
	b	.L61
.L60:
	str	w0, [x2,344]
	mov	w0, 4099
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	mov	w0, 38
	str	w0, [x2,308]
	mov	w0, 39
	str	w0, [x2,308]
	b	.L61
.L58:
	and	w1, w1, -8193
.L61:
	str	w1, [x2]
	ret
	.size	nandc_set_if_mode, .-nandc_set_if_mode
	.align	2
	.global	nandc_cs
	.type	nandc_cs, %function
nandc_cs:
	adrp	x1, .LANCHOR0+8
	mov	w2, 1
	lsl	w0, w2, w0
	ldr	x3, [x1,#:lo12:.LANCHOR0+8]
	ldr	w1, [x3]
	bfi	w1, w0, 0, 8
	str	w1, [x3]
	ret
	.size	nandc_cs, .-nandc_cs
	.align	2
	.global	nandc_de_cs
	.type	nandc_de_cs, %function
nandc_de_cs:
	adrp	x0, .LANCHOR0+8
	ldr	x1, [x0,#:lo12:.LANCHOR0+8]
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
	ldrb	w2, [x3,57]
	cmp	w2, w0
	bhi	.L67
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 192
	mov	w2, 607
	bl	printk
.L68:
	b	.L68
.L67:
	add	x0, x3, x0, sxtw
	ldrb	w22, [x0,160]
	ldr	x20, [x3,8]
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
.L71:
	bl	timer_delay_ns
	ldr	w19, [x20,2048]
	and	w19, w19, 255
	and	w0, w19, w21
	cmp	w0, w21
	beq	.L72
.L70:
	mov	w0, 20
	b	.L71
.L72:
	cmp	w19, 255
	beq	.L70
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
	cbnz	w3, .L74
	adrp	x4, .LANCHOR0
	add	x3, x4, :lo12:.LANCHOR0
	ldrb	w0, [x3,58]
	cbz	w0, .L75
	ldrb	w0, [x3,168]
	cbz	w0, .L74
.L75:
	add	x4, x4, :lo12:.LANCHOR0
	ldrh	w5, [x4,170]
	ldrb	w0, [x4,168]
	udiv	w3, w1, w5
	mul	w5, w3, w5
	sub	w3, w1, w5
	add	w1, w5, w3, lsl 1
	cbnz	w0, .L74
	add	x4, x4, 172
	ldrh	w3, [x4,w3,uxtw 1]
	add	w1, w3, w5
.L74:
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
.L85:
	mov	w0, 100
	bl	timer_delay_ns
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,8]
	ldr	w0, [x0]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L86
	subs	w19, w19, #1
	bne	.L85
	mov	w0, -1
	b	.L84
.L86:
	mov	w0, 0
.L84:
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
	add	x0, x2, 40
	add	x2, x2, 88
	add	x0, x0, x1
	add	x1, x2, x1
	mov	x2, 0
.L89:
	add	x3, x5, :lo12:.LANCHOR0
	ldrb	w4, [x3,150]
	cmp	w4, w2
	bls	.L94
	ldrb	w3, [x3,149]
	cmp	w3, 67
	bne	.L90
	ldrsb	w3, [x0,x2]
	b	.L93
.L90:
	ldrsb	w3, [x1,x2]
.L93:
	str	w3, [x20]
	add	x2, x2, 1
	b	.L89
.L94:
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
	add	x0, x0, 224
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
	ldrsb	w1, [x1,625]
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
	cbz	w2, .L98
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
	b	.L99
.L98:
	add	x0, x0, :lo12:.LANCHOR2
	mov	w3, 20
	add	x0, x0, 336
	str	w3, [x19,4]
	ldrb	w0, [x0,x1]
	str	w0, [x19]
	str	w2, [x19]
	str	w2, [x19]
	str	w2, [x19]
.L99:
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
	ldrb	w2, [x1,58]
	cbz	w2, .L100
	cmp	w2, 1
	ldr	x3, [x1,8]
	bne	.L102
	adrp	x1, .LANCHOR2+397
	ldrb	w2, [x1,#:lo12:.LANCHOR2+397]
	cbz	w2, .L100
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x3, x1, lsl 8
	str	w2, [x0,8]
	b	.L100
.L102:
	cmp	w2, 2
	bne	.L100
	ldrb	w2, [x1,684]
	cbz	w2, .L100
	ubfiz	x0, x0, 8, 8
	strb	wzr, [x1,684]
	add	x0, x3, x0
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
.L100:
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
	ldrb	w2, [x1,58]
	cbz	w2, .L112
	cmp	w2, 1
	ldr	x4, [x1,8]
	bne	.L114
	adrp	x1, .LANCHOR2+398
	ldrb	w2, [x1,#:lo12:.LANCHOR2+398]
	cbz	w2, .L112
	sxtw	x1, w0
	add	x1, x1, 8
	add	x0, x4, x1, lsl 8
	str	w2, [x0,8]
	b	.L112
.L114:
	cmp	w2, 2
	bne	.L112
	ldrb	w3, [x1,684]
	cbnz	w3, .L112
	adrp	x2, .LANCHOR2+380
	ubfiz	x0, x0, 8, 8
	add	x0, x4, x0
	mov	w5, 4
	ldrb	w2, [x2,#:lo12:.LANCHOR2+380]
	cmp	w2, 2
	csel	w2, w2, w5, eq
	strb	w2, [x1,684]
	mov	w1, 239
	str	w1, [x0,2056]
	mov	w1, 145
	str	w1, [x0,2052]
	str	w2, [x0,2048]
	mov	w1, 1
	str	w1, [x0,2048]
	str	w3, [x0,2048]
	str	w3, [x0,2048]
	bl	nandc_wait_flash_ready
.L112:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_flash_exit_slc_mode, .-zftl_flash_exit_slc_mode
	.align	2
	.global	flash_set_interface_mode
	.type	flash_set_interface_mode, %function
flash_set_interface_mode:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	adrp	x22, .LC36
	adrp	x23, .LC35
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w25, w0
	mov	x20, 0
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LC36
	add	x23, x23, :lo12:.LC35
.L132:
	add	x0, x21, x20, lsl 3
	ldr	x19, [x21,8]
	ldrb	w1, [x0,688]
	cmp	w1, 44
	cset	w24, eq
	cmp	w1, 69
	cset	w2, eq
	cmp	w1, 152
	cset	w0, eq
	adrp	x1, .LANCHOR0
	orr	w0, w2, w0
	cbnz	w0, .L133
	cbz	w24, .L125
.L133:
	add	x1, x1, :lo12:.LANCHOR0
	cmp	w25, 1
	ldrb	w1, [x1,720]
	bne	.L127
	tbz	x1, 0, .L125
	mov	x0, x23
	bl	printk
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	cbz	w24, .L128
	str	w25, [x1,2052]
	mov	w0, 5
	b	.L144
.L128:
	mov	w0, 128
	str	w0, [x1,2052]
	str	w25, [x1,2048]
	b	.L131
.L127:
	tbz	x1, 2, .L125
	mov	x0, x22
	bl	printk
	add	x1, x19, x20, lsl 8
	mov	w0, 239
	str	w0, [x1,2056]
	cbz	w24, .L130
	mov	w0, 1
	str	w0, [x1,2052]
	mov	w0, 35
.L144:
	str	w0, [x1,2048]
	b	.L131
.L130:
	mov	w0, 128
	str	w0, [x1,2052]
	str	w24, [x1,2048]
.L131:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L125:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L132
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
	.global	flash_reset
	.type	flash_reset, %function
flash_reset:
	adrp	x1, .LANCHOR0+8
	uxtb	x0, w0
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 8
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR0+8]
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
	adrp	x0, .LANCHOR0+8
	stp	x19, x20, [sp,16]
	mov	x19, x1
	ldr	x20, [x0,#:lo12:.LANCHOR0+8]
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
	bhi	.L146
	adrp	x0, .LC37
	ldrb	w3, [x19,1]
	ldrb	w4, [x19,2]
	add	x0, x0, :lo12:.LC37
	ldrb	w5, [x19,3]
	add	w1, w21, 1
	ldrb	w6, [x19,4]
	ldrb	w7, [x19,5]
	bl	printk
.L146:
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
	adrp	x4, .LANCHOR0+8
	ubfiz	x0, x0, 8, 8
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x3, .LANCHOR2+377
	ldr	x19, [x4,#:lo12:.LANCHOR0+8]
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
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR0+8
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x19, [x2,#:lo12:.LANCHOR0+8]
	add	x2, x20, :lo12:.LANCHOR2
	str	x21, [sp,32]
	ldrb	w3, [x2,397]
	sxtw	x2, w0
	add	x2, x2, 8
	add	x2, x19, x2, lsl 8
	cbnz	w3, .L160
	mov	w3, 162
.L160:
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
	beq	.L152
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	bl	printk
.L152:
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,398]
	cbz	w0, .L153
	str	w0, [x19,2056]
.L153:
	mov	w0, w21
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	sandisk_prog_test_bad_block, .-sandisk_prog_test_bad_block
	.align	2
	.global	nandc_bch_sel
	.type	nandc_bch_sel, %function
nandc_bch_sel:
	adrp	x2, .LANCHOR0
	mov	w3, 1
	add	x1, x2, :lo12:.LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	strb	w0, [x1,148]
	cmp	w2, 9
	ldr	x2, [x1,8]
	bne	.L162
	str	w3, [x2,16]
	cmp	w0, 70
	mov	w1, 0
	beq	.L163
	cmp	w0, 60
	mov	w1, 3
	beq	.L163
	cmp	w0, 40
	mov	w1, 2
	csel	w1, w1, w3, eq
.L163:
	lsl	w1, w1, 25
	orr	w1, w1, 1
	str	w1, [x2,32]
	b	.L161
.L162:
	str	w3, [x2,8]
	cmp	w0, 16
	mov	w1, 4096
	bne	.L165
.L168:
	and	w1, w1, -17
	b	.L166
.L165:
	cmp	w0, 24
	bne	.L167
	orr	w1, w1, 16
	b	.L166
.L167:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L168
.L166:
	orr	w1, w1, 1
	str	w1, [x2,12]
.L161:
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
	ldr	w2, [x0,16]
	cmp	w1, 9
	ldr	x1, [x0,8]
	str	w2, [x1]
	ldr	x1, [x0,8]
	ldr	w2, [x0,20]
	str	w2, [x1,4]
	ldr	x1, [x0,8]
	ldr	w2, [x0,24]
	bne	.L176
	str	w2, [x1,16]
	ldr	w2, [x0,28]
	str	w2, [x1,32]
	ldr	w2, [x0,32]
	str	w2, [x1,80]
	ldr	w2, [x0,36]
	str	w2, [x1,84]
	ldr	w2, [x0,40]
	str	w2, [x1,520]
	ldr	w0, [x0,44]
	str	w0, [x1,8]
	b	.L177
.L176:
	str	w2, [x1,8]
	ldr	w2, [x0,28]
	str	w2, [x1,12]
	ldr	w2, [x0,32]
	str	w2, [x1,304]
	ldr	w2, [x0,36]
	str	w2, [x1,308]
	ldr	w2, [x0,40]
	str	w2, [x1,336]
	ldr	w0, [x0,44]
	str	w0, [x1,344]
.L177:
	mov	x19, 0
	add	x21, x20, :lo12:.LANCHOR0
.L179:
	add	x0, x21, x19, lsl 3
	ldrb	w0, [x0,688]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L178
	mov	w0, w19
	bl	flash_reset
.L178:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L179
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,96]
	cbz	w0, .L180
	mov	w0, 1
	bl	nandc_set_if_mode
	mov	w0, w19
	bl	flash_set_interface_mode
	mov	w0, w19
	bl	nandc_set_if_mode
	ldr	w0, [x20,32]
	lsr	w0, w0, 8
	bl	nandc_set_ddr_para
.L180:
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
	ldr	x0, [x20,8]
	bl	rk_nandc_rb_irq_flag_init
	ldrb	w1, [x19,#:lo12:.LANCHOR0]
	ldr	x0, [x20,8]
	cmp	w1, 9
	bne	.L187
	ldr	w1, [x0,292]
	orr	w1, w1, 2
	str	w1, [x0,292]
	ldr	w1, [x0,288]
	orr	w1, w1, 2
	str	w1, [x0,288]
	b	.L188
.L187:
	ldr	w1, [x0,368]
	orr	w1, w1, 2
	str	w1, [x0,368]
	ldr	w1, [x0,364]
	orr	w1, w1, 2
	str	w1, [x0,364]
.L188:
	ldr	w1, [x0]
	tbnz	x1, 9, .L189
	bl	wait_for_nand_flash_ready
	b	.L186
.L189:
	mov	w1, 1
	bl	nandc_irq_disable
.L186:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	nandc_iqr_wait_flash_ready, .-nandc_iqr_wait_flash_ready
	.align	2
	.global	flash_erase_duplane_block
	.type	flash_erase_duplane_block, %function
flash_erase_duplane_block:
	stp	x29, x30, [sp, -80]!
	uxtb	w0, w0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x25, [sp,64]
	uxtb	w25, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	w24, w2
	mov	w23, w3
	ldrb	w4, [x1,57]
	cmp	w4, w0
	bhi	.L192
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 640
	mov	w2, 518
	bl	printk
.L193:
	b	.L193
.L192:
	add	x0, x1, x0, sxtw
	ldrb	w22, [x0,160]
	adrp	x0, .LANCHOR2+400
	ldr	x19, [x1,8]
	mov	x20, x22
	add	x22, x22, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	add	x22, x19, x22, lsl 8
	tbz	x0, 4, .L194
	adrp	x0, .LC39
	mov	w1, w20
	add	x0, x0, :lo12:.LC39
	bl	printk
.L194:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w25, .L195
	bl	zftl_flash_enter_slc_mode
	b	.L196
.L195:
	bl	zftl_flash_exit_slc_mode
.L196:
	ubfiz	x4, x20, 8, 8
	mov	w0, 96
	add	x19, x19, x4
	add	x21, x21, :lo12:.LANCHOR0
	str	w0, [x19,2056]
	and	w0, w24, 255
	str	w0, [x19,2052]
	lsr	w0, w24, 8
	str	w0, [x19,2052]
	lsr	w0, w24, 16
	str	w0, [x19,2052]
	ldrb	w0, [x21,721]
	mov	w21, 0
	cbnz	w0, .L197
	mov	w0, 208
	str	w0, [x19,2056]
	mov	w21, 5
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	and	w21, w0, w21
.L197:
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
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x22
	bl	flash_read_status
	mov	w19, w0
	cbnz	w25, .L198
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
.L198:
	mov	w0, w20
	bl	nandc_de_cs
	mov	w1, 5
	and	w1, w19, w1
	orr	w21, w1, w21
	cbz	w21, .L199
	adrp	x0, .LC40
	mov	w1, w24
	add	x0, x0, :lo12:.LC40
	mov	w2, w19
	bl	printk
.L199:
	mov	w0, w21
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
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
	mov	w23, w2
	and	w22, w2, 2097151
	ldrb	w3, [x1,57]
	cmp	w3, w0
	bhi	.L208
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 672
	mov	w2, 563
	bl	printk
.L209:
	b	.L209
.L208:
	add	x0, x1, x0, sxtw
	ldrb	w21, [x0,160]
	adrp	x0, .LANCHOR2+400
	ldr	x25, [x1,8]
	mov	x19, x21
	add	x21, x21, 8
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	add	x21, x25, x21, lsl 8
	tbz	x0, 4, .L210
	adrp	x0, .LC41
	mov	w1, w19
	add	x0, x0, :lo12:.LC41
	bl	printk
.L210:
	bl	nandc_wait_flash_ready
	mov	w0, w19
	bl	nandc_cs
	mov	w0, w19
	cbnz	w24, .L211
	bl	zftl_flash_enter_slc_mode
	b	.L212
.L211:
	bl	zftl_flash_exit_slc_mode
.L212:
	ubfiz	x3, x19, 8, 8
	mov	w0, 96
	add	x3, x25, x3
	str	w0, [x3,2056]
	and	w0, w23, 255
	str	w0, [x3,2052]
	lsr	w0, w22, 8
	str	w0, [x3,2052]
	lsr	w22, w22, 16
	str	w22, [x3,2052]
	mov	w0, 208
	str	w0, [x3,2056]
	bl	nandc_iqr_wait_flash_ready
	bl	nandc_wait_flash_ready
	mov	x0, x21
	bl	flash_read_status
	mov	w21, w0
	cbnz	w24, .L213
	mov	w0, w19
	bl	zftl_flash_exit_slc_mode
.L213:
	mov	w0, w19
	bl	nandc_de_cs
	mov	w3, 5
	ands	w19, w21, w3
	beq	.L214
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC42
	add	x0, x0, :lo12:.LC42
	mov	w2, w19
	ldrh	w1, [x20,170]
	udiv	w1, w23, w1
	bl	printk
.L214:
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
.L223:
	ldrb	w0, [x23,57]
	cmp	w0, w19
	bls	.L227
	add	x0, x23, x19, sxtw
	ldrb	w24, [x0,160]
	mov	w22, 0
.L224:
	cmp	w20, w22, uxth
	bls	.L228
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,170]
	mov	w0, w24
	mul	w1, w22, w1
	add	w22, w22, 1
	bl	flash_erase_block
	b	.L224
.L228:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L223
.L227:
	adrp	x0, .LC43
	mov	w1, 0
	add	x0, x0, :lo12:.LC43
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
	ldr	x0, [x21,8]
	add	w20, w23, 1
	str	x3, [x29,88]
	bl	rk_nandc_xfer_irq_flag_init
	ldrb	w0, [x22,#:lo12:.LANCHOR0]
	ldr	x4, [x21,8]
	cmp	w0, 9
	ldr	x3, [x29,88]
	ldr	x2, [x29,80]
	bne	.L231
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
	ldrb	w0, [x21,97]
	orr	w19, w19, 536870912
	orr	w19, w19, 1024
	and	w19, w19, -17
	bfi	w19, w20, 22, 6
	cbz	w0, .L233
	ldrb	w1, [x21,144]
	orr	w0, w19, 512
	cmp	w1, wzr
	csel	w19, w0, w19, ne
	b	.L233
.L231:
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
	cbz	w24, .L245
	ldrb	w0, [x21,148]
	ubfx	x23, x23, 1, 7
	mov	w1, 64
	mov	w4, 128
	cmp	w0, 25
	add	x23, x3, x23, lsl 2
	csel	w4, w1, w4, cc
	mov	w1, 0
	b	.L238
.L233:
	add	x22, x22, :lo12:.LANCHOR0
	ubfx	x21, x19, 22, 6
	mov	x0, x2
	lsl	w1, w21, 10
	str	x2, [x22,112]
	mov	w2, w24
	str	x3, [x22,120]
	bl	rknand_dma_map_single
	str	w0, [x22,128]
	ldr	x0, [x22,120]
	lsl	w1, w21, 2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,132]
	ldr	x3, [x22,8]
	mov	w1, 1
	str	w1, [x22,136]
	cmp	w24, wzr
	ldr	w1, [x22,128]
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
	ldrh	w0, [x22,146]
	orr	w1, w1, 1
	bfi	w1, w0, 16, 11
	str	w1, [x3,48]
	str	w19, [x3,16]
	orr	w19, w19, 4
	str	w19, [x3,16]
	b	.L230
.L238:
	cmp	x3, x23
	add	w0, w1, w4
	beq	.L240
	ldrh	w5, [x3,2]
	and	x1, x1, 4294967292
	ldr	x6, [x21,104]
	ldrh	w7, [x3],4
	orr	w5, w7, w5, lsl 16
	str	w5, [x6,x1]
	mov	w1, w0
	b	.L238
.L245:
	ldr	x1, [x21,104]
	str	w0, [x1]
.L240:
	add	x22, x22, :lo12:.LANCHOR0
	and	w20, w20, 63
	lsl	w1, w20, 10
	ldr	x0, [x22,104]
	str	x2, [x22,112]
	str	x0, [x22,120]
	mov	x0, x2
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,128]
	ldr	x0, [x22,120]
	lsl	w1, w20, 7
	mov	w2, w24
	bl	rknand_dma_map_single
	str	w0, [x22,132]
	ldr	x3, [x22,8]
	mov	w0, 1
	str	w0, [x22,136]
	cmp	w24, wzr
	ldr	w0, [x22,128]
	mov	w1, 8660
	str	w0, [x3,20]
	ldr	w0, [x22,132]
	str	w0, [x3,24]
	cset	w0, eq
	bfi	w1, w0, 1, 1
	orr	w1, w1, 1
	str	w1, [x3,16]
	str	w25, [x3,12]
	str	w19, [x3,8]
	orr	w19, w19, 4
	str	w19, [x3,8]
.L230:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	nandc_xfer_start, .-nandc_xfer_start
	.align	2
	.global	dump_bch_info
	.type	dump_bch_info, %function
dump_bch_info:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR2
	adrp	x21, .LC44
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR0
	add	x21, x21, :lo12:.LC44
.L247:
	ldrb	w0, [x20,377]
	cmp	w19, w0, lsr 2
	bcs	.L249
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x1, [x0,8]
	uxtw	x0, w19
	add	x0, x0, 84
	ldr	w0, [x1,x0,lsl 2]
	mov	w1, w19
	str	w0, [x29,56]
	mov	x0, x21
	add	w19, w19, 1
	ldr	w2, [x29,56]
	ldr	w3, [x29,56]
	ubfx	x2, x2, 3, 7
	ubfx	x3, x3, 19, 7
	bl	printk
	b	.L247
.L249:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	dump_bch_info, .-dump_bch_info
	.align	2
	.global	nandc_set_seed
	.type	nandc_set_seed, %function
nandc_set_seed:
	adrp	x1, .LANCHOR1
	and	w0, w0, 127
	add	x1, x1, :lo12:.LANCHOR1
	adrp	x2, .LANCHOR0
	add	x1, x1, 696
	ldrh	w0, [x1,w0,uxtw 1]
	add	x1, x2, :lo12:.LANCHOR0
	ldrb	w2, [x2,#:lo12:.LANCHOR0]
	orr	w3, w0, -1073741824
	ldrb	w4, [x1,144]
	ldr	x1, [x1,8]
	cmp	w4, wzr
	csel	w0, w3, w0, ne
	cmp	w2, 9
	bne	.L252
	str	w0, [x1,520]
	b	.L250
.L252:
	str	w0, [x1,336]
.L250:
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
	ldrb	w0, [x23,57]
	cmp	w0, w2
	bhi	.L257
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 952
	mov	w2, 741
	bl	printk
.L258:
	b	.L258
.L257:
	add	x2, x23, x2, sxtw
	ldrb	w20, [x2,160]
	ldr	x25, [x23,8]
	mov	w0, w20
	bl	nandc_cs
	cbnz	w21, .L259
	ldrb	w0, [x23,58]
	cbz	w0, .L260
	ldrb	w0, [x23,168]
	cbz	w0, .L261
.L260:
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w1, [x2,170]
	udiv	w0, w19, w1
	mul	w1, w0, w1
	ldrb	w0, [x2,168]
	sub	w19, w19, w1
	cbz	w0, .L262
	add	w19, w1, w19, lsl 1
	b	.L261
.L262:
	add	x2, x2, 172
	ldrh	w19, [x2,w19,uxtw 1]
	add	w19, w19, w1
	b	.L263
.L261:
	mov	w0, w20
	bl	zftl_flash_enter_slc_mode
	b	.L263
.L259:
	ldr	x0, [x23,48]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L264
	sxtw	x1, w20
	add	x1, x1, 8
	add	x1, x25, x1, lsl 8
	str	w21, [x1,8]
	b	.L263
.L264:
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
.L263:
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
	cbz	w21, .L265
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,48]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L265
	sub	w21, w21, #1
	add	w0, w19, w19, lsl 1
	add	w0, w21, w0
	b	.L276
.L265:
	mov	w0, w19
.L276:
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
	ldrb	w0, [x20,96]
	cbz	w0, .L278
	ldrb	w0, [x20,720]
	tbz	x0, 0, .L278
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	bl	nandc_set_if_mode
	strb	wzr, [x20,96]
.L278:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,144]
	cbz	w0, .L279
	mov	w0, 0
	strb	wzr, [x19,144]
	bl	nandc_set_seed
	mov	w0, 1
	strb	w0, [x19,144]
.L279:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	zftl_flash_de_init, .-zftl_flash_de_init
	.align	2
	.global	nandc_randomizer_enable
	.type	nandc_randomizer_enable, %function
nandc_randomizer_enable:
	adrp	x1, .LANCHOR0+144
	strb	w0, [x1,#:lo12:.LANCHOR0+144]
	ret
	.size	nandc_randomizer_enable, .-nandc_randomizer_enable
	.align	2
	.global	nandc_get_chip_if
	.type	nandc_get_chip_if, %function
nandc_get_chip_if:
	adrp	x1, .LANCHOR0+8
	uxtb	x0, w0
	add	x0, x0, 8
	ldr	x1, [x1,#:lo12:.LANCHOR0+8]
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
.L292:
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
	bne	.L292
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
.L295:
	uxtb	w3, w2
	add	w2, w2, 1
	add	w4, w3, 1
	strb	w3, [x1,1]
	strb	w4, [x1]
	cmp	w2, 32
	strb	wzr, [x1,2]
	add	x1, x1, 64
	str	xzr, [x1,-48]
	bne	.L295
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
	bne	.L305
	ldrb	w1, [x1,1]
	cmp	w1, 255
	bne	.L299
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 976
	mov	w2, 74
	bl	printk
.L300:
	b	.L300
.L299:
	strb	w1, [x0]
	b	.L297
.L305:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 728
.L306:
	mov	w2, w3
	sbfiz	x3, x3, 6, 32
	ldrb	w3, [x0,x3]
	cmp	w3, 255
	bne	.L306
	ldrb	w1, [x1,1]
	cmp	w1, 255
	bne	.L302
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 976
	mov	w2, 81
	bl	printk
.L303:
	b	.L303
.L302:
	sbfiz	x2, x2, 6, 32
	strb	w1, [x0,x2]
.L297:
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
	cbz	w1, .L308
	mov	x1, x0
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 2777
	bl	buf_add_tail
	b	.L309
.L308:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x1,2777]
	strb	w2, [x0]
	ldrb	w0, [x0,1]
	strb	w0, [x1,2777]
.L309:
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
	cbnz	w3, .L311
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 992
	mov	w2, 121
	bl	printk
.L312:
	b	.L312
.L311:
	add	x4, x2, 728
	ldrb	w2, [x2,2777]
	add	x4, x4, x2, lsl 6
	cbnz	w5, .L316
	cmp	w3, 1
	mov	x0, 0
	beq	.L313
.L316:
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
.L313:
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
	bne	.L323
	ldrb	w1, [x1]
	strb	w1, [x0]
	b	.L327
.L323:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 728
.L325:
	cmp	w2, 255
	beq	.L328
	mov	w3, w2
	sbfiz	x2, x2, 6, 32
	ldrb	w2, [x0,x2]
	cmp	w2, w4
	bne	.L325
	sbfiz	x3, x3, 6, 32
	ldrb	w2, [x1]
	strb	w2, [x0,x3]
	mov	w0, -1
	strb	w0, [x1]
.L327:
	mov	w0, 1
	b	.L322
.L328:
	mov	w0, 0
.L322:
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
	cbnz	w0, .L330
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1008
	mov	w2, 170
	bl	printk
.L331:
	b	.L331
.L330:
	add	x0, x19, 2777
	mov	x1, x20
	bl	buf_remove_buf
	cmp	w0, 1
	bne	.L329
	ldrb	w0, [x19,2778]
	sub	w0, w0, #1
	strb	w0, [x19,2778]
	ldrb	w0, [x20,2]
	orr	w0, w0, 1
	strb	w0, [x20,2]
.L329:
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
	adrp	x0, .LANCHOR0+152
	sxtw	x2, w2
	ldr	x0, [x0,#:lo12:.LANCHOR0+152]
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
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	uxth	w20, w20
	add	w20, w20, 31
	bl	printk
	asr	w20, w20, 5
	adrp	x0, .LANCHOR0+152
	lsl	w20, w20, 2
	ubfx	x3, x21, 5, 27
	lsl	x3, x3, 2
	mul	w19, w19, w20
	ldr	x0, [x0,#:lo12:.LANCHOR0+152]
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
	adrp	x1, .LC46
	adrp	x2, .LC47
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC46
	add	x2, x2, :lo12:.LC47
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	x20, x0
	bl	sprintf
	adrp	x21, .LANCHOR0
	adrp	x7, .LANCHOR2
	adrp	x1, .LC48
	add	x7, x7, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x7, x7, 368
	add	x19, x20, x0, sxtw
	add	x1, x1, :lo12:.LC48
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
	adrp	x1, .LC49
	mov	x0, x19
	add	x1, x1, :lo12:.LC49
	lsr	w2, w2, 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x21,2780]
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
	ldr	w2, [x21,2784]
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
	ldr	x2, [x21,2792]
	adrp	x1, .LC54
	mov	x0, x19
	add	x1, x1, :lo12:.LC54
	ldr	w2, [x2,524]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2792]
	adrp	x1, .LC55
	mov	x0, x19
	add	x1, x1, :lo12:.LC55
	ldr	w2, [x2,528]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC56
	mov	x0, x19
	add	x1, x1, :lo12:.LC56
	ldrh	w2, [x2,114]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC57
	mov	x0, x19
	add	x1, x1, :lo12:.LC57
	ldrh	w2, [x2,118]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC58
	mov	x0, x19
	add	x1, x1, :lo12:.LC58
	ldrh	w2, [x2,116]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC59
	mov	x0, x19
	add	x1, x1, :lo12:.LC59
	ldrh	w2, [x2,122]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC60
	mov	x0, x19
	add	x1, x1, :lo12:.LC60
	ldrh	w2, [x2,124]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC61
	mov	x0, x19
	add	x1, x1, :lo12:.LC61
	ldrh	w2, [x2,120]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2800]
	adrp	x1, .LC62
	add	x1, x1, :lo12:.LC62
	ldr	w3, [x0,16]
	ldr	w2, [x0,20]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2800]
	adrp	x1, .LC63
	add	x1, x1, :lo12:.LC63
	ldr	w3, [x0,24]
	ldr	w2, [x0,28]
	mov	x0, x19
	add	w2, w2, w3, lsr 11
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC64
	mov	x0, x19
	add	x1, x1, :lo12:.LC64
	ldr	w2, [x2,64]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC65
	mov	x0, x19
	add	x1, x1, :lo12:.LC65
	ldr	w2, [x2,68]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2792]
	mov	w2, 10
	adrp	x1, .LC66
	add	x1, x1, :lo12:.LC66
	ldr	w3, [x0,12]
	mov	x0, x19
	udiv	w2, w3, w2
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2792]
	adrp	x1, .LC67
	mov	x0, x19
	add	x1, x1, :lo12:.LC67
	ldr	w2, [x2,556]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2792]
	adrp	x1, .LC68
	mov	x0, x19
	add	x1, x1, :lo12:.LC68
	ldr	w2, [x2,552]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC69
	mov	x0, x19
	add	x1, x1, :lo12:.LC69
	ldr	w2, [x2,52]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC70
	mov	x0, x19
	add	x1, x1, :lo12:.LC70
	ldr	w2, [x2,60]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC71
	mov	x0, x19
	add	x1, x1, :lo12:.LC71
	ldr	w2, [x2,76]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x2, [x21,2800]
	adrp	x1, .LC72
	mov	x0, x19
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x2,8]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,2792]
	adrp	x1, .LC73
	mov	x0, x19
	add	x1, x1, :lo12:.LC73
	ldrh	w2, [x4,16]
	ldrh	w3, [x4,22]
	ldrb	w4, [x4,25]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,2792]
	adrp	x1, .LC74
	mov	x0, x19
	add	x1, x1, :lo12:.LC74
	ldrh	w2, [x4,48]
	ldrh	w3, [x4,54]
	ldrb	w4, [x4,57]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x4, [x21,2792]
	adrp	x1, .LC75
	mov	x0, x19
	add	x1, x1, :lo12:.LC75
	ldrh	w2, [x4,80]
	ldrh	w3, [x4,86]
	ldrb	w4, [x4,89]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2800]
	adrp	x1, .LC76
	mov	x0, x19
	add	x1, x1, :lo12:.LC76
	ldrh	w2, [x6,74]
	ldr	w3, [x6,84]
	ldrh	w4, [x6,88]
	ldrh	w5, [x6,92]
	ldrh	w6, [x6,96]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x6, [x21,2800]
	adrp	x1, .LC77
	mov	x0, x19
	add	x1, x1, :lo12:.LC77
	ldrh	w2, [x6,72]
	ldr	w3, [x6,80]
	ldrh	w4, [x6,90]
	ldrh	w5, [x6,94]
	ldrh	w6, [x6,98]
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
	add	x0, x19, 2808
	bl	buf_add_tail
	ldrb	w0, [x19,2809]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x19,2809]
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
	bcc	.L348
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1024
	mov	w2, 794
	bl	printk
.L340:
	b	.L340
.L353:
	ldrb	w4, [x3,2]
	tbnz	x4, 3, .L351
.L341:
	add	w2, w2, 1
	add	x3, x3, 64
	cmp	w2, 32
	beq	.L352
.L348:
	ldr	w4, [x3,36]
	cmp	w4, w0
	bne	.L341
	b	.L353
.L351:
	add	x19, x19, :lo12:.LANCHOR0
	ubfiz	x2, x2, 6, 32
	add	x19, x19, 728
	orr	w0, w4, 4
	add	x19, x19, x2
	strb	w0, [x19,2]
	tbnz	x4, 0, .L343
	mov	x0, x19
	bl	buf_remove_free
	b	.L343
.L352:
	add	x2, x19, :lo12:.LANCHOR0
	mov	x19, 0
	ldr	w3, [x2,2812]
	cmn	w3, #1
	bne	.L343
	str	w0, [x2,2812]
	str	x1, [x2,2816]
.L343:
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
	add	x0, x0, 2824
	bl	buf_remove_buf
	ldrb	w0, [x19,2]
	tbz	x0, 1, .L355
	and	w0, w0, -5
	strb	w0, [x19,2]
	b	.L354
.L355:
	mov	x0, x19
	bl	buf_free
.L354:
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	uxth	w20, w0
	add	x6, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	uxth	w22, w1
	uxth	w23, w2
	ldrh	w0, [x6,2826]
	cmp	w0, w20
	bhi	.L359
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1040
	mov	w2, 243
	bl	printk
.L360:
	b	.L360
.L359:
	ldr	x0, [x6,2832]
	uxtw	x21, w20
	ldrh	w24, [x0,x21,lsl 1]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L361
	ldr	x0, [x6,2840]
	mov	w1, w20
	ldrh	w5, [x6,2904]
	mov	w2, w22
	add	x0, x0, x21, lsl 2
	ldrh	w6, [x6,3176]
	mov	w4, w24
	ldrb	w3, [x0,2]
	adrp	x0, .LC78
	add	x0, x0, :lo12:.LC78
	ubfx	x3, x3, 5, 3
	bl	printk
.L361:
	add	x5, x19, :lo12:.LANCHOR0
	ldr	x0, [x5,2840]
	add	x21, x0, x21, lsl 2
	ldrb	w3, [x21,2]
	ands	w0, w3, 224
	bne	.L362
	cbz	w24, .L363
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1040
	mov	w2, 248
	bl	printk
.L364:
	b	.L364
.L362:
	ldrh	w1, [x5,2848]
	add	x3, x5, 2848
	mov	w0, 0
	cmp	w1, w20
	beq	.L363
	ldr	x4, [x5,2792]
	ldrh	w1, [x4,48]
	cmp	w1, w20
	beq	.L363
	ldrh	w1, [x4,16]
	cmp	w1, w20
	beq	.L363
	ldrh	w1, [x4,80]
	cmp	w1, w20
	beq	.L363
	ldrh	w1, [x3,56]
	mov	x0, 0
.L365:
	cmp	w1, w0
	bls	.L384
	add	x0, x0, 1
	add	x2, x3, x0, lsl 1
	ldrh	w2, [x2,56]
	cmp	w2, w20
	bne	.L365
	mov	w0, 0
	b	.L363
.L384:
	add	x3, x4, 392
	cbnz	w22, .L368
	sxtw	x3, w23
	add	x3, x4, x3, lsl 7
	add	x3, x3, 136
.L368:
	add	x0, x3, 128
	mov	w1, 65535
.L371:
	ldrh	w2, [x3]
	cmp	w2, w1
	bne	.L369
	strh	w20, [x3]
	add	x19, x19, :lo12:.LANCHOR0
	cbz	w22, .L370
	ldr	x1, [x19,2792]
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L383
.L370:
	ldr	x0, [x19,2792]
	add	x2, x0, x23, uxth 1
	ldrh	w0, [x2,120]
	add	w0, w0, 1
	strh	w0, [x2,120]
.L383:
	mov	w0, 1
	b	.L363
.L369:
	add	x3, x3, 2
	cmp	x3, x0
	bne	.L371
	b	.L383
.L363:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_add_sblk, .-gc_add_sblk
	.align	2
	.global	gc_get_src_ppa_from_index
	.type	gc_get_src_ppa_from_index, %function
gc_get_src_ppa_from_index:
	adrp	x1, .LANCHOR0+3184
	ubfiz	x0, x0, 2, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3184]
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
.L387:
	ldrb	w20, [x19,3192]
	cmp	w20, 255
	beq	.L409
	lsl	x1, x20, 6
	add	x0, x25, x1
	ldrb	w1, [x25,x1]
	strb	w1, [x19,3192]
	ldr	w1, [x0,52]
	ldrh	w23, [x0,48]
	cbz	w1, .L388
	mov	w2, 1
	strh	w2, [x19,3166]
	ldr	w2, [x0,40]
	adrp	x0, .LC79
	add	x0, x0, :lo12:.LC79
	str	w2, [x19,3168]
	bl	printk
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1056
	mov	w2, 505
	bl	printk
.L389:
	b	.L389
.L388:
	ldrb	w0, [x19,3193]
	mov	w21, 1
	cmp	w0, 3
	bne	.L390
	ldr	x0, [x19,2792]
	ldrb	w21, [x0,89]
	add	w21, w21, w21, lsl 1
	and	w21, w21, 1023
.L390:
	adrp	x0, .LANCHOR2+400
	uxth	x26, w23
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L391
	add	x2, x24, x20, lsl 6
	ldr	x4, [x22,3200]
	adrp	x0, .LC80
	mov	w1, w23
	add	x0, x0, :lo12:.LC80
	mov	w3, w23
	ldrb	w2, [x2,1]
	ldrb	w4, [x4,x26]
	bl	printk
.L391:
	ldr	x2, [x22,3200]
	add	x20, x24, x20, lsl 6
	ldrb	w1, [x2,x26]
	ldrb	w0, [x20,1]
	cmp	w1, w0
	beq	.L392
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1056
	mov	w2, 512
	bl	printk
.L393:
	b	.L393
.L392:
	ldrb	w0, [x20,61]
	cmp	w0, 3
	beq	.L394
	ldrb	w0, [x22,3193]
	cmp	w0, 3
	bne	.L394
	ldrb	w0, [x22,3208]
	cbz	w0, .L395
.L394:
	strh	w23, [x22,3164]
	mov	w20, 0
	mov	w26, -1
.L396:
	cmp	w21, w20, uxth
	bls	.L387
	add	w1, w20, w23
	ldr	x2, [x22,3200]
	add	w20, w20, 1
	ldrb	w0, [x2,x1]
	strb	w26, [x2,x1]
	sbfiz	x0, x0, 6, 32
	add	x0, x24, x0
	strb	wzr, [x0,61]
	bl	buf_free
	ldrb	w0, [x22,2855]
	sub	w0, w0, #1
	strb	w0, [x22,2855]
	b	.L396
.L395:
	cmp	w21, w0, uxth
	bls	.L387
	add	w1, w0, w23
	add	w0, w0, 1
	ldrb	w1, [x2,x1]
	add	x1, x24, x1, lsl 6
	strb	wzr, [x1,61]
	b	.L395
.L409:
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
	ldr	x1, [x0,2792]
	ldrb	w0, [x0,3209]
	ldrh	w4, [x1,124]
	cbz	w4, .L411
	add	x1, x1, 392
	mov	w2, 1
	b	.L412
.L411:
	sxtw	x2, w0
	add	x0, x1, x2, lsl 1
	ldrh	w5, [x0,120]
	mov	w0, 65535
	cbz	w5, .L413
	add	x1, x1, x2, lsl 7
	mov	w2, w4
	add	x1, x1, 136
.L412:
	add	x4, x1, 128
	mov	w5, 65535
.L416:
	ldrh	w0, [x1]
	cmp	w0, w5
	beq	.L414
	mov	w4, -1
	strh	w4, [x1]
	add	x3, x3, :lo12:.LANCHOR0
	cbz	w2, .L415
	ldr	x2, [x3,2792]
	ldrh	w1, [x2,124]
	sub	w1, w1, #1
	strh	w1, [x2,124]
	b	.L413
.L415:
	ldrb	w1, [x3,3209]
	ldr	x2, [x3,2792]
	add	x1, x2, x1, lsl 1
	ldrh	w2, [x1,120]
	sub	w2, w2, #1
	strh	w2, [x1,120]
	b	.L413
.L414:
	add	x1, x1, 2
	cmp	x1, x4
	bne	.L416
.L413:
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
	ldrb	w3, [x2,2855]
	cbz	w3, .L421
	ldrb	w3, [x2,2778]
	cmp	w3, 1
	bhi	.L421
	ldrh	w4, [x2,3210]
	mov	x20, x2
	ldrb	w0, [x2,3212]
	add	x2, x2, 728
	ldrh	w1, [x2,2436]
	mul	w0, w4, w0
	add	w3, w1, 24
	ldr	x4, [x2,2472]
	cmp	w0, w3
	csel	w3, w0, w3, ls
.L422:
	cmp	w1, w3
	bcs	.L435
	uxtw	x21, w1
	ldrb	w0, [x4,x21]
	cmp	w0, 255
	beq	.L423
	sbfiz	x0, x0, 6, 32
	add	x0, x2, x0
	ldrb	w5, [x0,61]
	cbnz	w5, .L423
	str	x1, [x29,56]
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	x1, [x29,56]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L424
	ldr	x2, [x20,3200]
	adrp	x0, .LC81
	add	x0, x0, :lo12:.LC81
	ldrb	w2, [x2,x21]
	bl	printk
.L424:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,3200]
	strb	w1, [x0,x21]
	ldrb	w0, [x19,2855]
	sub	w0, w0, #1
	strb	w0, [x19,2855]
	mov	w0, 1
	b	.L421
.L423:
	add	w1, w1, 1
	b	.L422
.L435:
	mov	w0, 0
.L421:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_free_temp_buf, .-gc_free_temp_buf
	.align	2
	.global	gc_static_wearleveling
	.type	gc_static_wearleveling, %function
gc_static_wearleveling:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	x6, [x0,2800]
	ldr	w0, [x6,32]
	cmp	w0, 20480
	bls	.L437
	ldr	w1, [x6,36]
	add	w1, w1, w0, lsr 10
	and	w0, w0, 1023
	str	w1, [x6,36]
	str	w0, [x6,32]
.L437:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,2792]
	ldr	w1, [x0,568]
	ldr	w2, [x0,12]
	add	w1, w1, 12959744
	add	w1, w1, 256
	cmp	w2, w1
	bhi	.L438
	ldr	w1, [x0,572]
	ldr	w3, [x6,36]
	add	w1, w1, 128
	cmp	w3, w1
	bls	.L436
.L438:
	ldr	w1, [x6,36]
	mov	w8, 0
	str	w1, [x0,572]
	mov	w20, 65535
	str	w2, [x0,568]
	add	x0, x21, :lo12:.LANCHOR0
	mov	w4, w8
	mov	w2, w8
	ldrh	w9, [x6,134]
	mov	w1, w8
	ldrh	w12, [x0,2826]
	mov	w24, w8
	ldr	x10, [x0,2840]
	mov	w23, w8
	mov	w19, w20
	mov	w3, w20
.L440:
	cmp	w9, w12
	bcs	.L478
	ubfiz	x11, x9, 2, 16
	add	x0, x10, x11
	ldrb	w0, [x0,2]
	tbz	x0, 3, .L441
	ldrh	w5, [x10,x11]
	and	w7, w5, 2047
	ldr	w5, [x10,x11]
	ubfx	x0, x5, 11, 8
	b	.L442
.L441:
	tst	w0, 24
	bne	.L443
	ldrh	w5, [x10,x11]
	mov	w0, 65535
	and	w7, w5, 2047
	b	.L442
.L443:
	ldr	w5, [x10,x11]
	ubfx	x0, x5, 11, 8
.L464:
	cmp	w19, w0
	add	w2, w2, 1
	csel	w19, w19, w0, cc
	cmp	w23, w0
	add	w4, w4, w0
	csel	w0, w23, w0, hi
	uxth	w2, w2
	uxth	w19, w19
	uxth	w23, w0
.L465:
	add	w9, w9, 1
	uxth	w9, w9
	b	.L440
.L478:
	ldrh	w0, [x6,96]
	cmp	w0, w24
	bcs	.L445
	strh	w24, [x6,96]
.L445:
	ldrh	w0, [x6,98]
	cmp	w0, w23
	bcs	.L446
	strh	w23, [x6,98]
.L446:
	adrp	x5, .LANCHOR2
	add	x0, x5, :lo12:.LANCHOR2
	mov	x22, x5
	udiv	w3, w8, w1
	strh	w3, [x6,88]
	ldr	w0, [x0,400]
	udiv	w4, w4, w2
	strh	w4, [x6,90]
	tbz	x0, 8, .L447
	adrp	x0, .LC82
	uxth	w3, w3
	add	x0, x0, :lo12:.LC82
	uxth	w4, w4
	bl	printk
.L447:
	add	x5, x22, :lo12:.LANCHOR2
	ldr	w0, [x5,400]
	tbz	x0, 8, .L448
	adrp	x0, .LC83
	mov	w1, w20
	add	x0, x0, :lo12:.LC83
	mov	w2, w19
	mov	w3, w24
	mov	w4, w23
	bl	printk
.L448:
	sub	w0, w23, w19
	cmp	w0, 64
	bgt	.L449
.L455:
	cbz	w19, .L456
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w0, [x0,134]
	b	.L457
.L449:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w22, [x0,134]
.L452:
	add	x0, x21, :lo12:.LANCHOR0
	ldrh	w1, [x0,2826]
	cmp	w1, w22
	bls	.L455
	ldr	x2, [x0,2840]
	ubfiz	x1, x22, 2, 16
	add	x0, x2, x1
	ldrb	w0, [x0,2]
	and	w3, w0, 224
	cmp	w3, 224
	beq	.L453
	tst	w0, 192
	beq	.L453
	tbz	x0, 4, .L453
	ldr	w0, [x2,x1]
	ubfx	x0, x0, 11, 8
	cmp	w0, w19
	bhi	.L453
	mov	w1, 1
	mov	w0, w22
	mov	w2, w1
	bl	gc_add_sblk
.L453:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L452
.L459:
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,2840]
	ldr	w1, [x4,x3]
	ubfx	x2, x1, 11, 8
	cmp	w2, w19
	bcc	.L458
	sub	w2, w2, w19
	bfi	w1, w2, 11, 8
	str	w1, [x4,x3]
.L458:
	add	w0, w0, 1
	uxth	w0, w0
.L457:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w2, [x1,2826]
	cmp	w2, w0
	bhi	.L459
	ldr	x0, [x1,2800]
	ldrh	w1, [x0,72]
	add	w1, w19, w1
	strh	w1, [x0,72]
	ldrh	w1, [x0,98]
	cmp	w1, w19
	bls	.L456
	sub	w19, w1, w19
	strh	w19, [x0,98]
.L456:
	cbz	w20, .L436
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w0, [x0,134]
.L461:
	add	x1, x21, :lo12:.LANCHOR0
	ldrh	w2, [x1,2826]
	cmp	w2, w0
	bls	.L479
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,2840]
	ldrh	w1, [x4,x3]
	and	w2, w1, 2047
	cmp	w2, w20
	blt	.L462
	sub	w2, w2, w20
	bfi	w1, w2, 0, 11
	strh	w1, [x4,x3]
.L462:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L461
.L479:
	ldr	x0, [x1,2800]
	ldrh	w1, [x0,74]
	add	w1, w20, w1
	strh	w1, [x0,74]
	ldrh	w1, [x0,96]
	cmp	w1, w20
	bls	.L436
	sub	w20, w1, w20
	strh	w20, [x0,96]
	b	.L436
.L442:
	cmp	w20, w7
	add	w1, w1, 1
	csel	w20, w20, w7, cc
	cmp	w24, w7
	add	w8, w8, w7
	csel	w7, w24, w7, hi
	cmp	w0, w3
	uxth	w1, w1
	uxth	w20, w20
	uxth	w24, w7
	bne	.L464
	b	.L465
.L436:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	gc_static_wearleveling, .-gc_static_wearleveling
	.align	2
	.global	print_gc_debug_info
	.type	print_gc_debug_info, %function
print_gc_debug_info:
	adrp	x6, .LANCHOR0
	adrp	x0, .LC84
	add	x6, x6, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC84
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrh	w1, [x6,2848]
	ldrh	w2, [x6,2850]
	ldrh	w3, [x6,3162]
	ldrb	w4, [x6,2778]
	ldrb	w5, [x6,2855]
	ldrh	w6, [x6,3214]
	bl	printk
	ldp	x29, x30, [sp], 16
	ret
	.size	print_gc_debug_info, .-print_gc_debug_info
	.align	2
	.global	ftl_get_blk_list_in_sblk
	.type	ftl_get_blk_list_in_sblk, %function
ftl_get_blk_list_in_sblk:
	adrp	x6, .LANCHOR0
	uxth	w0, w0
	add	x2, x6, :lo12:.LANCHOR0
	mov	w11, 21
	ldr	x2, [x2,2840]
	add	x2, x2, x0, uxth 2
	ldrb	w10, [x2,3]
	mov	w2, 0
	mov	w5, w2
.L482:
	add	x7, x6, :lo12:.LANCHOR0
	ldrb	w3, [x7,3212]
	cmp	w5, w3
	bge	.L488
	asr	w3, w10, w5
	tbnz	x3, 0, .L483
	ldrb	w4, [x7,3216]
	sbfiz	x9, x2, 1, 32
	ldrh	w3, [x7,3218]
	sdiv	w8, w5, w4
	sub	w3, w11, w3
	lsl	w3, w8, w3
	madd	w3, w0, w4, w3
	uxth	w3, w3
	strh	w3, [x1,x9]
	ldrb	w4, [x7,3216]
	cmp	w4, 1
	bls	.L484
	and	w4, w5, 1
	add	w3, w3, w4
	strh	w3, [x1,x9]
.L484:
	add	w2, w2, 1
.L483:
	add	w5, w5, 1
	b	.L482
.L488:
	mov	w0, w2
	mov	w4, -1
.L486:
	add	x3, x6, :lo12:.LANCHOR0
	ldrb	w3, [x3,3212]
	cmp	w2, w3
	bge	.L489
	strh	w4, [x1,w2,sxtw 1]
	add	w2, w2, 1
	b	.L486
.L489:
	ret
	.size	ftl_get_blk_list_in_sblk, .-ftl_get_blk_list_in_sblk
	.align	2
	.global	ftl_alloc_sblk
	.type	ftl_alloc_sblk, %function
ftl_alloc_sblk:
.L491:
	stp	x29, x30, [sp, -64]!
	mov	w6, 2
	mov	w8, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	mov	w22, w0
	add	x0, x21, :lo12:.LANCHOR0
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	cmp	w22, 5
	mov	w19, 65535
	ldrh	w10, [x0,2826]
	csel	w23, wzr, w6, ne
	ldr	x4, [x0,2800]
	cmp	w22, 5
	ldr	x9, [x0,2840]
	csel	w6, w6, wzr, ne
	mov	w12, w10
	mov	w5, w19
	mov	w11, w19
.L506:
	ldrh	w7, [x4,132]
	mov	w1, w7
.L492:
	cmp	w1, w12
	bge	.L518
	sbfiz	x0, x1, 2, 32
	add	x20, x9, x0
	ldrb	w3, [x20,2]
	tst	w3, 224
	bne	.L493
	ubfx	x3, x3, 3, 2
	cmp	w3, w6
	beq	.L493
	cbz	w23, .L494
	ldr	w3, [x9,x0]
	ldrh	w2, [x4,72]
	ldrh	w0, [x4,90]
	ubfx	x3, x3, 11, 8
	add	w2, w3, w2
	add	w0, w0, 1
	cmp	w2, w0
	ble	.L495
	uxth	w3, w3
	cmp	w5, w3
	bls	.L493
	uxth	w19, w1
	mov	w5, w3
	b	.L493
.L494:
	ldrh	w3, [x9,x0]
	ldrh	w2, [x4,74]
	ldrh	w0, [x4,88]
	and	w3, w3, 2047
	add	w2, w3, w2
	add	w0, w0, 1
	cmp	w2, w0
	ble	.L495
	cmp	w5, w3
	ble	.L493
	mov	w5, w3
	uxth	w19, w1
	b	.L493
.L495:
	uxth	w19, w1
.L496:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L497
	ldrb	w3, [x20,2]
	adrp	x0, .LC85
	ldrb	w4, [x20,3]
	add	x0, x0, :lo12:.LC85
	mov	w1, w19
	mov	w2, w22
	ubfx	x3, x3, 5, 3
	bl	printk
.L497:
	ldrb	w1, [x20,2]
	add	x21, x21, :lo12:.LANCHOR0
	bfi	w1, w22, 5, 3
	ubfx	x0, x1, 3, 2
	orr	w0, w23, w0
	bfi	w1, w0, 3, 2
	strb	w1, [x20,2]
	ldr	x1, [x21,2800]
	strh	w19, [x1,132]
	ldrb	w0, [x20,2]
	tbz	x0, 3, .L498
	ldrh	w0, [x1,116]
	sub	w0, w0, #1
	strh	w0, [x1,116]
	b	.L499
.L498:
	tst	w0, 24
	bne	.L500
	ldrh	w0, [x1,114]
	sub	w0, w0, #1
	strh	w0, [x1,114]
	b	.L499
.L500:
	ldrh	w0, [x1,118]
	sub	w0, w0, #1
	strh	w0, [x1,118]
.L499:
	cmp	w22, 5
	bne	.L501
	ldrh	w0, [x1,120]
	add	w0, w0, 1
	strh	w0, [x1,120]
	b	.L511
.L501:
	cmp	w22, 2
	bne	.L503
	ldrh	w0, [x1,122]
	add	w0, w0, 1
	strh	w0, [x1,122]
	b	.L511
.L503:
	cmp	w22, 3
	bne	.L511
	ldrh	w0, [x1,124]
	add	w0, w0, 1
	strh	w0, [x1,124]
	b	.L511
.L493:
	add	w1, w1, 1
	b	.L492
.L518:
	sub	w3, w1, w7
	ldrh	w0, [x4,134]
	add	w1, w8, w3
	uxth	w8, w1
	sub	w1, w10, w0
	cmp	w8, w1
	blt	.L505
	cmp	w19, w11
	beq	.L505
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x20, [x0,2840]
	add	x20, x20, x19, uxtw 2
	b	.L496
.L505:
	strh	w0, [x4,132]
	mov	w12, w7
	b	.L506
.L511:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	ftl_alloc_sblk, .-ftl_alloc_sblk
	.align	2
	.global	ftl_free_sblk
	.type	ftl_free_sblk, %function
ftl_free_sblk:
	stp	x29, x30, [sp, -16]!
	uxth	x0, w0
	mov	w1, 65535
	cmp	w0, w1
	add	x29, sp, 0
	bne	.L520
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1080
	mov	w2, 313
	bl	printk
.L521:
	b	.L521
.L520:
	adrp	x1, .LANCHOR0
	lsl	x3, x0, 2
	add	x5, x1, :lo12:.LANCHOR0
	ldr	x7, [x5,2840]
	add	x4, x7, x3
	ldrb	w2, [x4,2]
	ands	w6, w2, 224
	bne	.L522
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1080
	mov	w2, 316
	bl	printk
.L523:
	b	.L523
.L522:
	cmp	w6, 160
	ubfx	x2, x2, 3, 2
	bne	.L524
	ldr	x6, [x5,2800]
	ldrh	w5, [x6,120]
	sub	w5, w5, #1
	strh	w5, [x6,120]
	b	.L525
.L524:
	cmp	w6, 64
	bne	.L526
	ldr	x6, [x5,2800]
	ldrh	w5, [x6,122]
	sub	w5, w5, #1
	strh	w5, [x6,122]
	b	.L525
.L526:
	cmp	w6, 96
	bne	.L525
	ldr	x6, [x5,2800]
	ldrh	w5, [x6,124]
	sub	w5, w5, #1
	strh	w5, [x6,124]
.L525:
	tbz	x2, 0, .L527
	add	x6, x1, :lo12:.LANCHOR0
	ldr	x2, [x6,2800]
	ldrh	w5, [x2,116]
	ldrh	w8, [x2,74]
	add	w5, w5, 1
	strh	w5, [x2,116]
	ldrh	w9, [x2,72]
	ldr	w5, [x7,x3]
	ldrh	w7, [x7,x3]
	ubfx	x5, x5, 11, 8
	ubfx	x7, x7, 0, 11
	add	w7, w7, w8
	ldrh	w8, [x6,3220]
	cmp	w8, w7, uxth
	bcc	.L528
	b	.L535
.L527:
	cbnz	w2, .L530
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x4, [x2,2800]
	ldrh	w2, [x4,114]
	add	w2, w2, 1
	strh	w2, [x4,114]
	b	.L532
.L530:
	add	x2, x1, :lo12:.LANCHOR0
	ldr	x4, [x2,2800]
	ldrh	w2, [x4,118]
	add	w2, w2, 1
	strh	w2, [x4,118]
	b	.L532
.L535:
	add	w5, w9, w5
	ldrh	w6, [x6,3222]
	cmp	w6, w5, uxth
	bcs	.L532
.L528:
	ldrh	w5, [x2,118]
	ldrh	w6, [x2,120]
	add	w6, w6, w5
	add	x5, x1, :lo12:.LANCHOR0
	ldrh	w7, [x5,3224]
	cmp	w6, w7
	bge	.L533
	ldrb	w2, [x4,2]
	mov	w6, 2
	bfi	w2, w6, 3, 2
	strb	w2, [x4,2]
	ldr	x2, [x5,2800]
	ldrh	w4, [x2,116]
	sub	w4, w4, #1
	strh	w4, [x2,116]
	ldrh	w4, [x2,118]
	add	w4, w4, 1
	strh	w4, [x2,118]
	b	.L532
.L533:
	ldrh	w6, [x2,122]
	ldrh	w2, [x2,114]
	add	w2, w6, w2
	ldrh	w6, [x5,3226]
	cmp	w2, w6
	bge	.L532
	ldrb	w2, [x4,2]
	and	w2, w2, -25
	strb	w2, [x4,2]
	ldr	x2, [x5,2800]
	ldrh	w4, [x2,116]
	sub	w4, w4, #1
	strh	w4, [x2,116]
	ldrh	w4, [x2,114]
	add	w4, w4, 1
	strh	w4, [x2,114]
.L532:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x2, [x1,2840]
	add	x3, x2, x3
	ldrb	w2, [x3,2]
	and	w2, w2, 31
	strb	w2, [x3,2]
	ldr	x1, [x1,2832]
	strh	wzr, [x1,x0,lsl 1]
	ldp	x29, x30, [sp], 16
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
	adrp	x23, .LC86
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	mov	w20, 0
	add	x22, x21, 2848
	add	x23, x23, :lo12:.LC86
	adrp	x24, .LANCHOR2
.L537:
	ldrh	w0, [x22,56]
	adrp	x26, .LANCHOR0
	cmp	w0, w20
	bls	.L573
	add	x0, x22, x20, sxtw 1
	ldrh	w25, [x0,58]
	ldr	x0, [x21,2832]
	lsl	x27, x25, 1
	mov	x19, x25
	ldrh	w2, [x0,x27]
	cbz	w2, .L538
	mov	x0, x23
	mov	w1, w25
	bl	printk
.L538:
	add	x0, x26, :lo12:.LANCHOR0
	ldr	x1, [x0,2832]
	ldrh	w1, [x1,x27]
	cbnz	w1, .L539
	ldr	x0, [x0,2840]
	add	x25, x0, x25, lsl 2
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L540
	ldrb	w2, [x25,2]
	adrp	x0, .LC87
	add	x0, x0, :lo12:.LC87
	mov	w1, w19
	ubfx	x2, x2, 5, 3
	bl	printk
.L540:
	ldrb	w0, [x25,2]
	tst	w0, 192
	beq	.L554
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L541
.L554:
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1096
	mov	w2, 756
	bl	printk
.L543:
	b	.L543
.L541:
	mov	w0, w19
	add	x26, x26, :lo12:.LANCHOR0
	bl	ftl_free_sblk
	ldr	x2, [x26,2792]
	ldrh	w0, [x2,124]
	cbz	w0, .L544
	mov	x1, 0
.L546:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,392]
	cmp	w4, w19
	bne	.L545
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,392]
	sub	w0, w0, #1
	strh	w0, [x2,124]
	b	.L544
.L545:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L546
.L544:
	ldrh	w0, [x2,120]
	cbz	w0, .L547
	mov	x1, 0
.L549:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,136]
	cmp	w4, w19
	bne	.L548
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,136]
	sub	w0, w0, #1
	strh	w0, [x2,120]
	b	.L547
.L548:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L549
.L547:
	ldrh	w0, [x2,122]
	cbz	w0, .L550
	mov	x1, 0
.L552:
	add	x4, x2, x1, lsl 1
	mov	w3, w1
	ldrh	w4, [x4,264]
	cmp	w4, w19
	bne	.L551
	add	x1, x2, x3, sxtw 1
	mov	w3, -1
	strh	w3, [x1,264]
	sub	w0, w0, #1
	strh	w0, [x2,122]
	b	.L550
.L551:
	add	x1, x1, 1
	cmp	x1, 64
	bne	.L552
	b	.L550
.L539:
	mov	w0, w19
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L550:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L537
.L573:
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
	ldrh	w19, [x5,3218]
	mov	x20, x3
	sub	w19, w0, w19
	ldrb	w0, [x5,3228]
	asr	w22, w2, w19
	lsl	w19, w4, w19
	sub	w19, w19, #1
	and	w19, w19, w2
	sxth	w19, w19
	cbz	w0, .L575
	ldrh	w2, [x5,3230]
	cmp	w1, wzr
	mov	w0, w22
	cset	w1, eq
	mul	w2, w19, w2
	bl	flash_erase_block_en
.L575:
	add	x3, x20, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w0, w22
	ldrh	w2, [x3,3230]
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	mov	w22, w1
	stp	x23, x24, [sp,48]
	uxth	w23, w0
	add	x0, x21, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	ubfiz	x20, x23, 2, 16
	mov	w19, 0
	ldr	x0, [x0,2840]
	add	x0, x0, x20
	ldrb	w24, [x0,3]
.L580:
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w0, [x2,3232]
	cmp	w19, w0
	bge	.L587
	ldrb	w0, [x2,3216]
	ldrh	w4, [x2,3230]
	mov	w2, 0
	mov	w1, w2
	mul	w6, w19, w0
	mul	w5, w23, w0
.L588:
	cmp	w1, w0
	bge	.L599
	add	w3, w1, w6
	asr	w3, w24, w3
	tbnz	x3, 0, .L581
	and	w3, w1, 1
	add	x7, x29, 64
	add	w3, w5, w3
	mul	w3, w3, w4
	str	w3, [x7,w2,sxtw 2]
	add	w2, w2, 1
.L581:
	add	w1, w1, 1
	b	.L588
.L599:
	cmp	w2, 2
	bne	.L583
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3228]
	cbz	w0, .L584
	cmp	w22, wzr
	ldr	w2, [x29,64]
	ldr	w3, [x29,68]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_duplane_block
.L584:
	ldr	w2, [x29,64]
	mov	w0, w19
	ldr	w3, [x29,68]
	mov	w1, w22
	bl	flash_erase_duplane_block
	b	.L585
.L583:
	cmp	w2, 1
	bne	.L585
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3228]
	cbz	w0, .L586
	cmp	w22, wzr
	ldr	w2, [x29,64]
	mov	w0, w19
	cset	w1, eq
	bl	flash_erase_block_en
.L586:
	ldr	w2, [x29,64]
	mov	w0, w19
	mov	w1, w22
	bl	flash_erase_block_en
.L585:
	add	w19, w19, 1
	b	.L580
.L587:
	cbnz	w22, .L589
	ldr	x1, [x2,2840]
	ldrh	w0, [x1,x20]
	add	w3, w0, 1
	bfi	w0, w3, 0, 11
	strh	w0, [x1,x20]
	ldr	x0, [x2,2800]
	ldr	w1, [x0,84]
	ldrh	w3, [x0,96]
	add	w1, w1, 1
	str	w1, [x0,84]
	ldr	x1, [x2,2840]
	ldrh	w1, [x1,x20]
	ubfx	x1, x1, 0, 11
	uxth	w2, w1
	cmp	w3, w2
	bge	.L591
	strh	w1, [x0,96]
	b	.L591
.L589:
	ldr	x3, [x2,2840]
	ldr	w0, [x3,x20]
	ubfx	x1, x0, 11, 8
	add	w1, w1, 1
	bfi	w0, w1, 11, 8
	str	w0, [x3,x20]
	ldr	x0, [x2,2800]
	ldr	w1, [x0,80]
	add	w1, w1, 1
	str	w1, [x0,80]
	ldr	x1, [x2,2840]
	ldrh	w2, [x0,98]
	ldr	w1, [x1,x20]
	ubfx	x1, x1, 11, 8
	cmp	w2, w1, uxtb
	bcs	.L591
	strh	w1, [x0,98]
.L591:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 128
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
	bls	.L601
	strh	wzr, [x1,136]
.L601:
	ldrh	w3, [x1,112]
	mov	w5, 65535
	cbnz	w3, .L607
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1112
	mov	w2, 441
	bl	printk
.L603:
	b	.L603
.L606:
	sxtw	x4, w2
	add	x4, x4, 80
	ldrh	w0, [x1,x4,lsl 1]
	cmp	w0, w5
	bne	.L609
	add	w2, w2, 1
.L604:
	cmp	w2, 63
	ble	.L606
	strh	wzr, [x1,136]
.L607:
	ldrh	w2, [x1,136]
	b	.L604
.L609:
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
	bls	.L611
	strh	wzr, [x1,138]
.L611:
	ldrh	w3, [x1,112]
	mov	w5, 65535
	cmp	w3, 63
	bls	.L617
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1136
	mov	w2, 465
	bl	printk
.L613:
	b	.L613
.L615:
	add	w2, w2, 1
.L614:
	cmp	w2, 63
	ble	.L616
	strh	wzr, [x1,138]
.L617:
	ldrh	w2, [x1,138]
	b	.L614
.L616:
	sxtw	x4, w2
	add	x4, x4, 80
	ldrh	w6, [x1,x4,lsl 1]
	cmp	w6, w5
	bne	.L615
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
	ldrh	w2, [x0]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L619
	adrp	x1, .LANCHOR0
	ubfiz	x2, x2, 2, 16
	add	x5, x1, :lo12:.LANCHOR0
	ldr	x4, [x5,2840]
	add	x6, x4, x2
	ldrb	w3, [x6,2]
	tst	w3, 224
	bne	.L619
	ldrb	w0, [x0,4]
	bfi	w3, w0, 5, 3
	strb	w3, [x6,2]
	tbz	x3, 3, .L623
	ldr	x3, [x5,2800]
	ldrh	w0, [x3,116]
	sub	w0, w0, #1
	strh	w0, [x3,116]
	b	.L624
.L623:
	tst	w3, 24
	ldr	x3, [x5,2800]
	bne	.L625
	ldrh	w0, [x3,114]
	sub	w0, w0, #1
	strh	w0, [x3,114]
	b	.L624
.L625:
	ldrh	w0, [x3,118]
	sub	w0, w0, #1
	strh	w0, [x3,118]
.L624:
	ldrb	w0, [x6,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L626
	ldr	w0, [x4,x2]
	add	x1, x1, :lo12:.LANCHOR0
	ubfx	x3, x0, 11, 8
	add	w3, w3, 1
	bfi	w0, w3, 11, 8
	str	w0, [x4,x2]
	ldr	x1, [x1,2800]
	ldrh	w0, [x1,120]
	sub	w0, w0, #1
	strh	w0, [x1,120]
	b	.L619
.L626:
	ldrh	w3, [x4,x2]
	cmp	w0, 64
	add	w5, w3, 1
	bfi	w3, w5, 0, 11
	strh	w3, [x4,x2]
	bne	.L627
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x1, [x1,2800]
	ldrh	w0, [x1,122]
	sub	w0, w0, #1
	strh	w0, [x1,122]
	b	.L619
.L627:
	cmp	w0, 96
	bne	.L619
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x1, [x1,2800]
	ldrh	w0, [x1,124]
	sub	w0, w0, #1
	strh	w0, [x1,124]
.L619:
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
	ldrh	w0, [x3,3234]
	ldr	x4, [x3,2792]
	ldrb	w3, [x3,3212]
	mul	w0, w0, w3
	cmp	w1, w0
	bge	.L629
	add	x4, x4, 16
	b	.L630
.L629:
	sub	w1, w1, w0
	add	x4, x4, 48
	uxth	w1, w1
.L630:
	ldrb	w3, [x4,9]
	sdiv	w5, w1, w3
	msub	w1, w5, w3, w1
	add	x1, x4, x1, uxth 1
	ldrh	w0, [x1,16]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L631
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1160
	mov	w2, 842
	bl	printk
.L632:
	b	.L632
.L631:
	add	x2, x2, :lo12:.LANCHOR0
	ldp	x29, x30, [sp], 16
	ldrh	w1, [x2,3230]
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
	ldrh	w2, [x2,3240]
	ldr	x4, [x1,3752]
	ldr	x5, [x1,3760]
.L634:
	cmp	w2, w3
	beq	.L638
	uxtw	x1, w2
	ldr	w6, [x4,x1,lsl 2]
	cmp	w6, w0
	bne	.L635
	mov	w0, w2
	bl	ftl_get_ppa_from_index
	b	.L636
.L635:
	ldrh	w2, [x5,x1,lsl 1]
	b	.L634
.L638:
	mov	w0, -1
.L636:
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
	bne	.L640
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1184
	mov	w2, 955
	bl	printk
.L641:
	b	.L641
.L640:
	adrp	x2, .LANCHOR0
	ldrh	w3, [x1,2]
	add	x5, x2, :lo12:.LANCHOR0
	ldrh	w0, [x5,3234]
	cmp	w3, w0
	bne	.L642
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1184
	mov	w2, 956
	bl	printk
.L643:
	b	.L643
.L642:
	ldrh	w3, [x1,6]
	cbnz	w3, .L644
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1184
	mov	w2, 957
	bl	printk
.L645:
	b	.L645
.L644:
	ldrb	w0, [x1,5]
	ldrb	w5, [x5,3212]
	add	x0, x0, 8
	ldrh	w0, [x1,x0,lsl 1]
.L646:
	cmp	w0, w4
	bne	.L650
	ldrb	w0, [x1,5]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x1,5]
	cmp	w0, w5
	bne	.L647
	ldrh	w0, [x1,2]
	strb	wzr, [x1,5]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L647:
	ldrb	w0, [x1,5]
	add	x0, x0, 8
	ldrh	w0, [x1,x0,lsl 1]
	b	.L646
.L650:
	sub	w3, w3, #1
	strh	w3, [x1,6]
	ldrh	w3, [x1,10]
	add	x2, x2, :lo12:.LANCHOR0
	ldrh	w4, [x1,2]
	add	w3, w3, 1
	strh	w3, [x1,10]
	ldrb	w3, [x1,5]
	ldrh	w5, [x2,3230]
	add	w3, w3, 1
	ldrb	w2, [x2,3212]
	uxtb	w3, w3
	strb	w3, [x1,5]
	mul	w0, w0, w5
	cmp	w2, w3
	orr	w0, w0, w4
	bne	.L649
	add	w4, w4, 1
	strb	wzr, [x1,5]
	strh	w4, [x1,2]
.L649:
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
	bcs	.L652
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1208
	mov	w2, 988
	bl	printk
.L653:
	b	.L653
.L652:
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
	ldrh	w2, [x3,3768]
	cbnz	w0, .L655
	cmp	w2, 4
	bhi	.L655
	add	w2, w2, 1
	strh	w2, [x3,3768]
	b	.L654
.L655:
	add	x0, x1, :lo12:.LANCHOR0
	mov	w9, 65535
	strh	wzr, [x0,3768]
	ldrh	w6, [x0,2826]
	ldr	x7, [x0,2832]
	ldr	x8, [x0,2840]
	mov	x0, 0
	mov	w2, w0
	mov	w3, w0
.L657:
	cmp	w6, w0, uxth
	bls	.L665
	ldrh	w4, [x7,x0,lsl 1]
	cmp	w4, w9
	beq	.L658
	add	x5, x8, x0, lsl 2
	ldrb	w5, [x5,2]
	and	w5, w5, 224
	cmp	w5, 160
	bne	.L659
	add	w2, w2, w4
	b	.L658
.L659:
	add	w3, w3, w4
.L658:
	add	x0, x0, 1
	b	.L657
.L665:
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,2792]
	ldr	x1, [x1,2800]
	str	w3, [x0,524]
	str	w2, [x0,528]
	ldrh	w1, [x1,120]
	cbz	w1, .L654
	udiv	w2, w2, w1
	str	w2, [x0,532]
.L654:
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
	adrp	x1, .LANCHOR0+2832
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+2832]
	ldrh	w0, [x1,x0]
	cmp	w0, wzr
	cset	w0, eq
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
	beq	.L669
	add	x0, x2, :lo12:.LANCHOR0
	ubfiz	x3, x1, 1, 16
	ldr	x0, [x0,2832]
	ldrh	w19, [x0,x3]
	cbnz	w19, .L670
	adrp	x0, .LC88
	mov	w2, w19
	add	x0, x0, :lo12:.LC88
	bl	printk
	b	.L674
.L670:
	sub	w19, w19, #1
	strh	w19, [x0,x3]
.L669:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w0, 0
	ldrh	w2, [x19,3770]
	cmp	w2, w1
	beq	.L671
	mov	w0, 65535
	cmp	w2, w0
	bne	.L672
	strh	w1, [x19,3770]
.L674:
	mov	w0, 0
	b	.L671
.L672:
	mov	w0, w2
	str	x1, [x29,40]
	bl	ftl_vpn_update
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,3770]
.L671:
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
	add	x4, x6, 3240
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x8, [x6,3752]
	ldr	x12, [x6,3760]
	mov	w6, 65535
	ldrh	w4, [x4,w5,sxtw 1]
	mov	w11, w6
.L676:
	cmp	w4, w11
	beq	.L680
	uxtw	x9, w4
	uxth	x7, w4
	lsl	x10, x9, 2
	add	x13, x8, x10
	ldr	w10, [x8,x10]
	cmp	w10, w0
	bne	.L677
	mov	w4, -1
	str	w4, [x13]
	mov	w4, 65535
	cmp	w6, w4
	add	x4, x3, :lo12:.LANCHOR0
	bne	.L678
	ldr	x6, [x4,3760]
	add	x4, x4, 3240
	ldrh	w6, [x6,x7,lsl 1]
	strh	w6, [x4,w5,sxtw 1]
	b	.L679
.L678:
	ldr	x4, [x4,3760]
	ldrh	w8, [x4,x7,lsl 1]
	strh	w8, [x4,w6,uxtw 1]
.L679:
	add	x4, x3, :lo12:.LANCHOR0
	mov	w6, -1
	ldr	x4, [x4,3760]
	strh	w6, [x4,x7,lsl 1]
	b	.L680
.L677:
	mov	w6, w4
	ldrh	w4, [x12,x9,lsl 1]
	b	.L676
.L680:
	add	x3, x3, :lo12:.LANCHOR0
	uxtw	x4, w2
	cmn	w1, #1
	ldr	x6, [x3,3752]
	str	w0, [x6,x4,lsl 2]
	add	x0, x3, 3240
	ldrh	w6, [x0,w5,sxtw 1]
	strh	w2, [x0,w5,sxtw 1]
	ldr	x0, [x3,3760]
	strh	w6, [x0,x4,lsl 1]
	beq	.L682
	ldrh	w0, [x3,3218]
	mov	w2, 21
	mov	w4, 1
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w4, w2
	ldrb	w0, [x3,3216]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
.L682:
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
	ldrh	w5, [x1,3218]
	sub	w2, w2, w5
	lsr	w0, w0, w5
	lsl	w2, w4, w2
	sub	w2, w2, #1
	and	w0, w2, w0
	ldrb	w2, [x1,3216]
	udiv	w2, w0, w2
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	uxtb	w20, w2
	tbz	x0, 14, .L687
	adrp	x0, .LC89
	mov	w1, w21
	add	x0, x0, :lo12:.LC89
	mov	w2, w20
	bl	printk
.L687:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2826]
	cmp	w1, w20
	bls	.L686
	ldr	x0, [x0,2840]
	add	x20, x0, x20, uxtb 2
	mov	w0, 1
	lsl	w1, w0, w21
	ldrb	w0, [x20,3]
	orr	w1, w1, w0
	strb	w1, [x20,3]
.L686:
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
	ldr	x0, [x19,2800]
	ldr	x7, [x19,2792]
	ldrh	w6, [x0,120]
	ldrh	w1, [x0,114]
	ldrh	w2, [x0,118]
	ldrh	w3, [x0,116]
	ldrh	w4, [x0,122]
	ldrh	w5, [x0,124]
	ldr	w0, [x7,528]
	str	w0, [sp]
	ldrh	w0, [x19,3214]
	str	w0, [sp,8]
	adrp	x0, .LC90
	add	x0, x0, :lo12:.LC90
	ldr	w7, [x7,524]
	bl	printk
	ldr	x5, [x19,2800]
	adrp	x0, .LC91
	ldrb	w3, [x19,3772]
	add	x0, x0, :lo12:.LC91
	ldr	w4, [x5,8]
	ldr	w1, [x5,20]
	ldr	w2, [x5,28]
	ldr	w5, [x5,64]
	bl	printk
	ldr	x2, [x19,2800]
	adrp	x0, .LC92
	add	x0, x0, :lo12:.LC92
	ldr	w3, [x2,16]
	ldr	w1, [x2,52]
	ldr	w2, [x2,60]
	lsr	w3, w3, 11
	bl	printk
	ldr	x7, [x19,2800]
	ldrh	w0, [x7,90]
	ldrh	w1, [x7,74]
	ldrh	w3, [x7,88]
	ldrh	w4, [x7,92]
	ldrh	w5, [x7,96]
	ldrh	w6, [x7,72]
	str	w0, [sp]
	ldrh	w0, [x7,94]
	str	w0, [sp,8]
	ldrh	w0, [x7,98]
	str	w0, [sp,16]
	adrp	x0, .LC93
	add	x0, x0, :lo12:.LC93
	ldr	w2, [x7,84]
	ldr	w7, [x7,80]
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
	cbnz	x0, .L694
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1232
	mov	w2, 558
	bl	printk
.L695:
	b	.L695
.L694:
	adrp	x19, .LANCHOR0
	ldrb	w1, [x0,56]
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w2, [x19,2776]
	cmp	w2, w1
	bcs	.L696
	adrp	x1, .LANCHOR1
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 1232
	mov	w2, 563
	bl	printk
.L697:
	b	.L697
.L696:
	cbnz	w1, .L698
	bl	buf_free
	ldrb	w0, [x19,2809]
	b	.L699
.L698:
	add	x0, x19, 2808
	mov	x1, x20
	bl	buf_add_tail
	ldrb	w2, [x19,2809]
	ldrb	w1, [x20,56]
	add	w2, w2, 1
	uxtb	w0, w2
	ldr	x2, [x19,2800]
	strb	w0, [x19,2809]
	ldr	w3, [x2,16]
	add	w1, w3, w1
	str	w1, [x2,16]
	ldr	w1, [x2,32]
	add	w1, w1, 1
	str	w1, [x2,32]
.L699:
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
	adrp	x21, .LC94
	stp	x23, x24, [sp,48]
	add	x22, x19, 728
	mov	w23, 21
	mov	w24, 1
	add	x21, x21, :lo12:.LC94
.L701:
	ldrb	w0, [x19,3773]
	cmp	w0, 255
	beq	.L708
	sbfiz	x0, x0, 6, 32
	add	x20, x22, x0
	add	x4, x20, 32
	ldrb	w0, [x22,x0]
	strb	w0, [x19,3773]
	ldr	w0, [x20,52]
	cmn	w0, #1
	bne	.L702
	ldrh	w2, [x19,3218]
	mov	x0, x21
	ldr	w3, [x4,8]
	sub	w1, w23, w2
	lsl	w1, w24, w1
	lsr	w2, w3, w2
	sub	w1, w1, #1
	and	w2, w1, w2
	ldrb	w1, [x19,3216]
	udiv	w1, w2, w1
	ldr	w2, [x4,4]
	uxth	w25, w1
	mov	w1, w25
	bl	printk
	mov	w1, 1
	mov	w0, w25
	mov	w2, 0
	bl	gc_add_sblk
	ldr	x0, [x19,2792]
	ldr	w1, [x0,556]
	add	w1, w1, 1
	str	w1, [x0,556]
	ldrh	w1, [x0,16]
	cmp	w1, w25
	bne	.L703
	strh	wzr, [x0,22]
	b	.L704
.L703:
	ldrh	w1, [x0,48]
	cmp	w1, w25
	bne	.L704
	strh	wzr, [x0,54]
.L704:
	mov	x0, x20
	bl	ftl_write_buf
	b	.L701
.L702:
	ldr	w0, [x4,4]
	ldr	w1, [x4,12]
	ldrh	w2, [x20,48]
	bl	lpa_hash_update_ppa
	ldrb	w0, [x20,2]
	tbz	x0, 2, .L706
	and	w0, w0, -3
	strb	w0, [x20,2]
	b	.L701
.L706:
	mov	x0, x20
	bl	buf_free
	b	.L701
.L708:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_write_completed, .-ftl_write_completed
	.align	2
	.global	sblk_init
	.type	sblk_init, %function
sblk_init:
	adrp	x0, .LANCHOR0
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,3774]
	strb	w1, [x0,3773]
	strb	w1, [x0,2824]
	strb	w1, [x0,3192]
	mov	w0, 0
	ret
	.size	sblk_init, .-sblk_init
	.align	2
	.global	dump_sblk_queue
	.type	dump_sblk_queue, %function
dump_sblk_queue:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LC95
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	str	x21, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC95
	ldrb	w1, [x20,3774]
	bl	printk
	ldrb	w19, [x20,3774]
	cmp	w19, 255
	beq	.L710
	add	x20, x20, 728
	sbfiz	x19, x19, 6, 32
	adrp	x21, .LC96
	add	x19, x20, x19
	add	x21, x21, :lo12:.LC96
.L712:
	ldrb	w1, [x19,1]
	mov	x0, x21
	ldrb	w2, [x19,58]
	ldr	w3, [x19,40]
	bl	printk
	ldrb	w19, [x19]
	cmp	w19, 255
	beq	.L710
	sbfiz	x19, x19, 6, 32
	add	x19, x20, x19
	b	.L712
.L710:
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
	ldrb	w2, [x3,3774]
	cmp	w2, 255
	beq	.L720
	ldrh	w8, [x3,3218]
	mov	w4, 21
	ldrb	w7, [x3,3216]
	mov	w0, 1
	sub	w4, w4, w8
	ubfx	x9, x5, 21, 3
	lsl	w4, w0, w4
	sub	w7, w7, #1
	sub	w4, w4, #1
	asr	w5, w5, w8
	uxth	w7, w7
	adrp	x10, .L724
	uxth	w4, w4
	and	w5, w5, w7
	and	w5, w5, w4
	add	x3, x3, 728
	add	x10, x10, :lo12:.L724
.L727:
	add	x0, x3, x2, lsl 6
	ldr	w6, [x0,40]
	ubfx	x11, x6, 21, 3
	cmp	w11, w9
	bne	.L721
	lsr	w6, w6, w8
	ldrb	w0, [x0,58]
	and	w6, w7, w6
	and	w6, w4, w6
	cmp	w6, w5
	bne	.L722
	cmp	w1, 1
	beq	.L725
	b	.L720
.L722:
	cmp	w1, 3
	bhi	.L721
	ldrb	w6, [x10,w1,uxtw]
	adr	x11, .Lrtx724
	add	x6, x11, w6, sxtb #2
	br	x6
.Lrtx724:
	.section	.rodata
	.align	0
	.align	2
.L724:
	.byte	(.L723 - .Lrtx724) / 4
	.byte	(.L725 - .Lrtx724) / 4
	.byte	(.L726 - .Lrtx724) / 4
	.byte	(.L720 - .Lrtx724) / 4
	.text
.L723:
	cmp	w0, 2
	b	.L736
.L725:
	sub	w6, w0, #6
	and	w6, w6, -3
	uxtb	w6, w6
	cbz	w6, .L721
	b	.L720
.L726:
	cmp	w0, 10
.L736:
	bne	.L720
.L721:
	lsl	x2, x2, 6
	ldrb	w2, [x3,x2]
	cmp	w2, 255
	bne	.L727
	mov	w0, 0
.L720:
	ret
	.size	queue_lun_state, .-queue_lun_state
	.align	2
	.global	queue_remove_completed_req
	.type	queue_remove_completed_req, %function
queue_remove_completed_req:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	ldrb	w2, [x1,3774]
	cmp	w2, 255
	beq	.L738
	add	x6, x1, 728
	lsl	x5, x2, 6
	add	x7, x6, x5
	ldrb	w3, [x7,58]
	sub	w3, w3, #11
	uxtb	w3, w3
	cmp	w3, 1
	bhi	.L738
	ldrb	w3, [x6,x5]
	strb	w3, [x1,3774]
	mov	w3, -1
	strb	w3, [x6,x5]
	ldrb	w3, [x7,59]
	cmp	w3, 1
	bne	.L740
	ldrh	w3, [x7,50]
	cbnz	w3, .L741
	ldrb	w3, [x1,3773]
	add	x1, x1, 3773
	b	.L742
.L741:
	ldrb	w3, [x1,3192]
	add	x1, x1, 3192
	b	.L742
.L740:
	cbnz	w3, .L738
	ldr	w3, [x7,36]
	cmn	w3, #1
	beq	.L738
	ldrb	w3, [x1,2824]
	add	x1, x1, 2824
.L742:
	cmp	w3, 255
	add	x0, x0, :lo12:.LANCHOR0
	bne	.L746
	add	x0, x0, x2, lsl 6
	ldrb	w0, [x0,729]
	strb	w0, [x1]
	b	.L738
.L746:
	add	x0, x0, 728
.L753:
	mov	w1, w3
	sbfiz	x3, x3, 6, 32
	ldrb	w3, [x0,x3]
	cmp	w3, 255
	bne	.L753
	add	x2, x0, x2, lsl 6
	sbfiz	x1, x1, 6, 32
	ldrb	w2, [x2,1]
	strb	w2, [x0,x1]
.L738:
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
	ldrb	w1, [x3,3212]
	strh	w0, [x2,690]
	cmp	w1, w0
	bls	.L755
	add	x0, x2, x0, sxtw 1
	ldrh	w1, [x0,672]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L756
.L755:
	adrp	x22, .LC97
	add	x22, x22, :lo12:.LC97
.L770:
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
	cbnz	w0, .L757
	mov	w1, w20
	mov	x0, x22
	bl	printk
	ldr	x0, [x21,2840]
	add	x20, x0, x20, uxth 2
	ldrb	w1, [x20,2]
	orr	w1, w1, -32
	strb	w1, [x20,2]
	b	.L770
.L757:
	ldr	x0, [x21,2800]
	mov	w1, 1
	mov	w3, 65535
	str	w1, [x21,3776]
	mov	x1, 0
	strh	wzr, [x0,690]
.L759:
	add	x4, x0, x1, lsl 1
	mov	w2, w1
	ldrh	w4, [x4,416]
	cmp	w4, w3
	beq	.L758
	add	x1, x1, 1
	cmp	x1, 128
	bne	.L759
	adrp	x0, .LC34
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 188
	bl	printk
.L761:
	b	.L761
.L758:
	add	x1, x0, x2, sxtw 1
	strh	w20, [x1,416]
	ldrh	w1, [x0,688]
	add	w1, w1, 1
	strh	w1, [x0,688]
.L756:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 65533
	ldr	x0, [x19,2800]
	ldrh	w1, [x0,690]
	add	x1, x1, 336
	ldrh	w1, [x0,x1,lsl 1]
	sub	w3, w1, #1
	cmp	w2, w3, uxth
	bcs	.L762
	adrp	x0, .LC34
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 193
	bl	printk
.L763:
	b	.L763
.L762:
	ldrh	w3, [x19,3218]
	mov	w2, 21
	strh	wzr, [x0,696]
	sub	w2, w2, w3
	strh	w1, [x0,692]
	asr	w4, w1, w2
	strh	w4, [x0,694]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L767
	mov	w0, 1
	mov	w3, w1
	lsl	w2, w0, w2
	adrp	x0, .LC98
	add	x0, x0, :lo12:.LC98
	sub	w2, w2, #1
	uxth	w4, w4
	bl	printk
.L767:
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
.L774:
	add	x2, x3, 3784
	lsl	x5, x0, 4
	uxth	w1, w0
	ldrh	w5, [x5,x2]
	cmp	w5, w4
	beq	.L780
	add	x0, x0, 1
	cmp	x0, 32
	bne	.L774
	mov	x1, 0
	mov	w4, 32768
.L776:
	add	x3, x2, x1, lsl 4
	uxth	w5, w1
	ldrh	w3, [x3,2]
	tbnz	x3, 15, .L775
	cmp	w3, w4
	bcs	.L775
	mov	w4, w3
	mov	w0, w5
.L775:
	add	x1, x1, 1
	cmp	x1, 32
	bne	.L776
	cmp	w0, 32
	mov	w3, w0
	bne	.L773
	adrp	x0, .LANCHOR4-56
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	mov	w2, 0
	add	x1, x1, 3784
	ldrb	w4, [x0,#:lo12:.LANCHOR4-56]
	mov	w0, -1
.L778:
	ldrh	w6, [x1,2]
	cmp	w6, w0
	bcs	.L777
	ldrh	w5, [x1]
	cmp	w5, w4
	csel	w0, w0, w6, eq
	cmp	w5, w4
	csel	w3, w3, w2, eq
.L777:
	add	w2, w2, 1
	add	x1, x1, 16
	uxth	w2, w2
	cmp	w2, 32
	bne	.L778
	cmp	w3, 32
	mov	w0, w3
	bne	.L773
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 24
	mov	w2, 289
	bl	printk
.L779:
	b	.L779
.L780:
	mov	w0, w1
.L773:
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
	cbnz	w0, .L788
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L789:
	add	x1, x2, 172
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L789
.L795:
	adrp	x20, .LANCHOR4
	mov	w1, 255
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1024
	sub	x0, x20, #52
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	x0, 0
	b	.L790
.L788:
	cmp	w0, 1
	bne	.L791
	mov	x0, 0
	mov	w4, 3
	mov	w5, 2
	add	x3, x19, :lo12:.LANCHOR0
.L794:
	cmp	x0, 3
	uxth	w2, w0
	mov	w1, w2
	bls	.L792
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L792:
	add	x2, x3, 172
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L794
	b	.L795
.L791:
	cmp	w0, 2
	bne	.L796
	mov	w1, 65535
	mov	x0, 0
	add	x4, x19, :lo12:.LANCHOR0
.L798:
	add	x3, x4, 172
	cmp	x0, 1
	uxth	w2, w0
	csel	w2, w1, w2, hi
	add	w1, w1, 2
	strh	w2, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L798
	b	.L795
.L796:
	cmp	w0, 3
	bne	.L799
	mov	x0, 0
	mov	w4, 5
	mov	w5, 4
	add	x3, x19, :lo12:.LANCHOR0
.L802:
	cmp	x0, 5
	uxth	w2, w0
	mov	w1, w2
	bls	.L800
	ubfiz	w1, w2, 1, 15
	and	w2, w2, 1
	cmp	w2, wzr
	csel	w2, w4, w5, ne
	sub	w1, w1, w2
	uxth	w1, w1
.L800:
	add	x2, x3, 172
	strh	w1, [x2,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L802
	b	.L795
.L799:
	cmp	w0, 4
	bne	.L803
	add	x2, x19, :lo12:.LANCHOR0
	mov	w5, 7
	add	x1, x2, 172
	mov	w4, 6
	strh	w0, [x1,8]
	mov	w0, 5
	strh	wzr, [x2,172]
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
.L805:
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
	bne	.L805
	b	.L795
.L803:
	cmp	w0, 5
	bne	.L806
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L807:
	add	x1, x2, 172
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L807
	mov	x0, 0
.L808:
	add	x2, x1, x0
	add	w3, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w3, [x2,32]
	bne	.L808
	b	.L795
.L806:
	cmp	w0, 8
	bne	.L795
	mov	x0, 0
	add	x2, x19, :lo12:.LANCHOR0
.L809:
	add	x1, x2, 172
	strh	w0, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	bne	.L809
	b	.L795
.L790:
	add	x1, x19, 172
	sub	x2, x20, #52
	ldrh	w1, [x0,x1]
	add	x0, x0, 2
	cmp	x0, 512
	strh	w1, [x2,w1,sxtw 1]
	bne	.L790
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_lsb_page_tbl_build, .-flash_lsb_page_tbl_build
	.align	2
	.global	flash_die_info_init
	.type	flash_die_info_init, %function
flash_die_info_init:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x1, x1, :lo12:.LANCHOR3
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x1, x1, 48
	add	x25, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LC0
	adrp	x21, .LANCHOR0
	add	x20, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR4
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	bl	printk
	add	x26, x22, :lo12:.LANCHOR4
	ldrh	w0, [x25,394]
	mov	w2, 8
	ldrb	w1, [x25,380]
	mov	x24, 0
	strh	w0, [x20,170]
	add	x25, x25, 369
	ldrh	w0, [x25,9]
	add	x27, x20, 688
	strb	wzr, [x20,57]
	sdiv	w0, w0, w1
	mov	w1, 0
	strh	w0, [x26,972]
	add	x0, x20, 160
	bl	ftl_memset
	add	x0, x26, 976
	mov	w1, 0
	mov	w2, 32
	bl	ftl_memset
	ldrb	w23, [x25,-1]
.L827:
	mov	x0, x25
	add	x1, x27, x24, lsl 3
	mov	w2, w23
	bl	flash_mem_cmp8
	cbnz	w0, .L826
	ldrb	w1, [x20,57]
	add	x2, x26, 976
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x20, x1, sxtw
	strb	w0, [x20,57]
	strb	w24, [x1,160]
.L826:
	add	x24, x24, 1
	cmp	x24, 4
	bne	.L827
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 368
	ldrb	w2, [x1,8]
	cmp	w2, 2
	beq	.L828
.L832:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR4
	ldp	x23, x24, [sp,48]
	ldrb	w1, [x19,381]
	ldrb	w0, [x21,57]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x19,382]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x22,1008]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
.L828:
	add	x24, x21, :lo12:.LANCHOR0
	ldrh	w20, [x1,14]
	ldrb	w27, [x1,23]
	mov	x25, 0
	and	w20, w20, 65280
	add	x26, x0, 369
	ldrh	w2, [x24,170]
	add	x3, x24, 688
	add	x4, x22, :lo12:.LANCHOR4
	mul	w20, w20, w2
	ldrb	w2, [x1,13]
	mul	w20, w20, w2
	lsl	w28, w20, 1
.L831:
	add	x1, x3, x25, lsl 3
	mov	x0, x26
	mov	w2, w23
	str	x4, [x29,96]
	str	x3, [x29,104]
	bl	flash_mem_cmp8
	ldr	x3, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L829
	ldrb	w1, [x24,57]
	add	x0, x4, 976
	cmp	w27, wzr
	csel	w2, w20, w28, eq
	str	w2, [x0,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x24, x1, sxtw
	strb	w0, [x24,57]
	strb	w25, [x1,160]
.L829:
	add	x25, x25, 1
	cmp	x25, 4
	bne	.L831
	b	.L832
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
	add	x0, x19, 3240
	bl	ftl_memset
	ldrh	w0, [x19,3234]
	mov	w1, 255
	ldrb	w2, [x19,3212]
	mul	w2, w0, w2
	ldr	x0, [x19,3760]
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
	tbz	x0, 12, .L840
	adrp	x1, .LANCHOR3
	adrp	x0, .LC99
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC99
	add	x1, x1, 72
	mov	w2, 199
	mov	w3, 0
	bl	printk
.L840:
	adrp	x19, .LANCHOR0
	mov	w1, 255
	add	x19, x19, :lo12:.LANCHOR0
	mov	w2, 512
	add	x0, x19, 3240
	bl	ftl_memset
	ldrh	w0, [x19,3234]
	mov	w1, 255
	ldrb	w2, [x19,3212]
	mul	w2, w0, w2
	ldr	x0, [x19,3760]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	w0, 0
.L841:
	ldrh	w1, [x19,3234]
	ldrb	w2, [x19,3212]
	mul	w1, w1, w2
	cmp	w0, w1, lsl 1
	bge	.L850
	uxtw	x2, w0
	ldr	x1, [x19,3752]
	ldr	w1, [x1,x2,lsl 2]
	cmn	w1, #1
	beq	.L842
	uxtb	w1, w1
	add	x3, x19, 3240
	ldrh	w4, [x3,w1,sxtw 1]
	strh	w0, [x3,w1,sxtw 1]
	ldr	x1, [x19,3760]
	strh	w4, [x1,x2,lsl 1]
.L842:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L841
.L850:
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
	ldrh	w3, [x0,170]
	mul	w2, w2, w3
	ldrb	w3, [x0,148]
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
	ldrb	w4, [x0,57]
	ldrb	w1, [x1,7]
	strb	w1, [x19,9]
	mov	x1, 0
	strb	w2, [x19,8]
.L852:
	cmp	w4, w1, uxtb
	bls	.L854
	add	x2, x0, 160
	ldrb	w3, [x19,10]
	ldrb	w2, [x1,x2]
	add	x1, x1, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L852
.L854:
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
	add	x21, x19, 2848
	mov	x0, x21
	strb	wzr, [x19,3209]
	strb	wzr, [x20,1010]
	bl	ftl_memset
	ldrh	w1, [x19,3234]
	mov	w0, -1
	strh	w0, [x19,2848]
	lsr	w0, w1, 2
	ldrb	w2, [x19,3212]
	strh	w0, [x19,2884]
	lsr	w3, w1, 1
	strh	w0, [x19,3176]
	ldrh	w0, [x19,3210]
	mul	w1, w1, w2
	strh	w3, [x19,2886]
	mul	w0, w0, w2
	sub	w1, w1, #32
	strh	w3, [x20,1014]
	lsl	w0, w0, 2
	strh	w1, [x20,1012]
	str	xzr, [x19,2856]
	bl	ftl_malloc
	str	x0, [x20,1016]
	ldrb	w1, [x19,3212]
	ldrh	w0, [x19,3210]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1024]
	ldrh	w1, [x19,3210]
	ldrb	w0, [x19,3212]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,3200]
	ldrb	w1, [x19,3212]
	ldrh	w0, [x19,3210]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3184]
	ldrb	w1, [x19,3212]
	ldrh	w0, [x19,3210]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x20,1032]
	ldrh	w0, [x19,2826]
	ldr	x21, [sp,32]
	lsr	w0, w0, 3
	strh	w0, [x19,3214]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	gc_init, .-gc_init
	.align	2
	.global	ftl_open_sblk_init
	.type	ftl_open_sblk_init, %function
ftl_open_sblk_init:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LC97
	adrp	x23, .LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w21, w1
	add	x25, x0, 16
	mov	x26, x23
	add	x20, x20, :lo12:.LC97
	mov	w24, -1
.L857:
.L860:
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
	ldrh	w1, [x0,3234]
	strb	wzr, [x19,5]
	strh	wzr, [x19,10]
	mul	w2, w1, w2
	strh	w2, [x19,6]
	strb	w21, [x19,4]
	beq	.L858
	ldrb	w3, [x0,3212]
	mul	w3, w1, w3
	uxth	w3, w3
.L858:
	add	x27, x26, :lo12:.LANCHOR0
	strh	w3, [x19,12]
	ubfiz	x28, x22, 1, 16
	ldrb	w2, [x27,3212]
	ldr	x0, [x27,3752]
	add	x0, x0, x3, uxth 2
	mul	w2, w1, w2
	mov	w1, 255
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	x0, [x27,2832]
	ldrh	w1, [x19,6]
	strh	w1, [x0,x28]
	ldrb	w0, [x19,9]
	cbnz	w0, .L856
	mov	x0, x20
	mov	w1, w22
	bl	printk
	ldr	x0, [x27,2832]
	strh	w24, [x0,x28]
	mov	w0, 7
	strb	w0, [x19,4]
	b	.L860
.L856:
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
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR4
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrh	w0, [x19,1040]
	cmp	w0, 128
	bls	.L863
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 96
	mov	w2, 74
	bl	printk
.L864:
	b	.L864
.L863:
	add	x24, x29, 112
	mov	w1, 0
	mov	w2, 256
	mov	x0, x24
	adrp	x21, .LANCHOR0
	bl	ftl_memset
	add	x2, x21, :lo12:.LANCHOR0
	mov	w0, 21
	mov	w1, 1
	ldrh	w9, [x19,1040]
	mov	x3, 0
	ldr	x4, [x2,2800]
	ldrh	w6, [x2,3218]
	ldrb	w8, [x2,3216]
	sub	w0, w0, w6
	ldrh	w7, [x4,698]
	lsl	w0, w1, w0
	sub	w0, w0, #1
.L865:
	cmp	w7, w3, uxth
	bls	.L886
	add	x1, x4, x3, lsl 2
	mov	x2, 0
	ldr	w1, [x1,704]
	lsr	w1, w1, w6
	and	w1, w1, w0
	udiv	w1, w1, w8
	uxth	w1, w1
.L866:
	cmp	w9, w2, uxth
	bls	.L887
	add	x5, x4, x2, lsl 1
	ldrh	w5, [x5,416]
	cmp	w5, w1
	bne	.L867
	ldrh	w5, [x24,x2,lsl 1]
	add	w5, w5, 1
	strh	w5, [x24,x2,lsl 1]
.L867:
	add	x2, x2, 1
	b	.L866
.L887:
	add	x3, x3, 1
	b	.L865
.L886:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w25, 0
	adrp	x26, .LC100
	mov	w19, w25
	mov	w28, 65535
	add	x26, x26, :lo12:.LC100
	ldrb	w0, [x1,3212]
	mov	w5, -1
	ldrh	w20, [x1,3234]
	mul	w20, w0, w20
	uxth	w20, w20
.L870:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,1040]
	cmp	w0, w19
	bls	.L888
	add	x1, x21, :lo12:.LANCHOR0
	sxtw	x22, w19
	add	x0, x22, 208
	ldr	x4, [x1,2800]
	ldrb	w2, [x1,3216]
	ldrh	w3, [x4,x0,lsl 1]
	ldrh	w0, [x4,692]
	sdiv	w0, w0, w2
	cmp	w0, w3
	bne	.L871
	ldrb	w0, [x1,3212]
	ldrh	w1, [x1,3234]
	mul	w0, w0, w1
	strh	w0, [x24,w19,sxtw 1]
.L871:
	ldrh	w1, [x24,x22,lsl 1]
	cmp	w20, w1
	bls	.L872
	cmp	w1, wzr
	csel	w25, w25, w19, eq
	csel	w20, w20, w1, eq
.L872:
	cbnz	w1, .L874
	cmp	w3, w28
	beq	.L874
	ldrh	w4, [x4,688]
	mov	w1, w19
	mov	w2, 0
	add	x27, x21, :lo12:.LANCHOR0
	mov	x0, x26
	str	x5, [x29,104]
	add	x22, x22, 208
	bl	printk
	ldr	x0, [x27,2800]
	ldrh	w0, [x0,x22,lsl 1]
	bl	ftl_free_sblk
	ldr	x0, [x27,2800]
	ldr	x5, [x29,104]
	strh	w5, [x0,x22,lsl 1]
	ldrh	w1, [x0,688]
	sub	w1, w1, #1
	strh	w1, [x0,688]
.L874:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L870
.L888:
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
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR3
	adrp	x0, .LC101
	add	x29, sp, 0
	str	x19, [sp,16]
	add	x2, x2, :lo12:.LANCHOR3
	adrp	x19, .LANCHOR0
	mov	w1, 100
	add	x19, x19, :lo12:.LANCHOR0
	add	x2, x2, 112
	add	x0, x0, :lo12:.LC101
	bl	printk
	ldr	x0, [x19,152]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,152]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	mov	w2, 32
	str	w0, [x1]
	mov	w1, 2032
	ldr	x0, [x19,152]
	add	x0, x0, 80
	str	w1, [x0,-72]
	mov	w1, 1
	strh	w1, [x0,-64]
	add	x1, x19, 64
	bl	ftl_memcpy
	ldr	x0, [x19,152]
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
.L892:
	cmp	w2, w3
	bls	.L894
	ldr	w4, [x1,x3,lsl 2]
	str	w4, [x0,x3,lsl 2]
	add	x3, x3, 1
	b	.L892
.L894:
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
.L897:
	cmp	w1, w2
	bls	.L899
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L897
.L899:
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
	adrp	x0, .LANCHOR4+1048
	ldr	x0, [x0,#:lo12:.LANCHOR4+1048]
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
	adrp	x0, .LANCHOR4+1056
	mov	x2, x19
	mov	w1, 1
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
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
	adrp	x1, .LANCHOR4+1056
	ldr	x2, [x29,24]
	ldr	x0, [x29,16]
	ldr	x1, [x1,#:lo12:.LANCHOR4+1056]
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
	adrp	x3, .LANCHOR4+1056
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1056]
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
	adrp	x3, .LANCHOR4+1056
	ldr	x1, [x29,32]
	ldr	x2, [x29,40]
	ldr	x0, [x29,24]
	ldr	x3, [x3,#:lo12:.LANCHOR4+1056]
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
	adrp	x4, .LANCHOR4+1064
	mov	x6, x0
	stp	x29, x30, [sp, -16]!
	uxtw	x1, w1
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1064]
	add	x4, x4, 4096
	cbnz	w2, .L906
	add	x1, x4, x1
	b	.L908
.L906:
	add	x0, x4, x1
	mov	x1, x6
.L908:
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
	adrp	x0, .LC102
	mov	w1, 0
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC102
	bl	printk
	adrp	x0, .LANCHOR4+1056
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
	ldr	x0, [x0,80]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	rk_ftl_cache_write_back
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	adrp	x0, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
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
	adrp	x0, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
	ldr	x0, [x0,88]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	2
	.global	rk_nand_resume
	.type	rk_nand_resume, %function
rk_nand_resume:
	adrp	x0, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
	ldr	x0, [x0,96]
	blr	x0
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nand_resume, .-rk_nand_resume
	.align	2
	.global	rk_ftl_get_capacity
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	adrp	x0, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR4+1056]
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
	adrp	x1, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1056]
	ldr	x1, [x1,120]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	adrp	x1, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1056]
	ldr	x1, [x1,128]
	blr	x1
	ldp	x29, x30, [sp], 16
	ret
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	adrp	x4, .LANCHOR4+1056
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1056]
	ldr	x4, [x4,48]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	adrp	x4, .LANCHOR4+1056
	uxtb	w0, w0
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x4, [x4,#:lo12:.LANCHOR4+1056]
	ldr	x4, [x4,40]
	blr	x4
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlRead, .-FtlRead
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	adrp	x2, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1056]
	ldr	x2, [x2,56]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	adrp	x2, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x2, [x2,#:lo12:.LANCHOR4+1056]
	ldr	x2, [x2,104]
	blr	x2
	ldp	x29, x30, [sp], 16
	ret
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	adrp	x1, .LANCHOR4+1056
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	x1, [x1,#:lo12:.LANCHOR4+1056]
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
	adrp	x23, .LC103
	adrp	x21, .LC104
	adrp	x24, .LC46
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x22, x1
	mov	x27, x0
	mov	w25, w2
	uxtw	x26, w3
	mov	w20, w19
	add	x23, x23, :lo12:.LC103
	add	x21, x21, :lo12:.LC104
	add	x24, x24, :lo12:.LC46
.L923:
	cmp	x19, x26
	beq	.L931
	cbnz	w20, .L924
	mov	x0, x23
	mov	x1, x27
	mov	x2, x22
	mov	w3, w19
	bl	printk
.L924:
	cmp	w25, 4
	mov	x0, x21
	bne	.L925
	ldr	w1, [x22,x19,lsl 2]
	b	.L930
.L925:
	cmp	w25, 2
	bne	.L927
	ldrsh	w1, [x22,x19,lsl 1]
	b	.L930
.L927:
	ldrb	w1, [x22,x19]
.L930:
	bl	printk
	add	w20, w20, 1
	cmp	w20, 15
	bls	.L928
	adrp	x1, .LC105
	mov	x0, x24
	add	x1, x1, :lo12:.LC105
	mov	w20, 0
	bl	printk
.L928:
	add	x19, x19, 1
	b	.L923
.L931:
	adrp	x0, .LC46
	adrp	x1, .LC105
	add	x1, x1, :lo12:.LC105
	add	x0, x0, :lo12:.LC46
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
	ldr	x27, [x0,152]
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
	bne	.L933
	ldr	x1, [x29,136]
	mov	w0, -89
	strb	w0, [x1,16]
	adrp	x0, .LANCHOR2+425
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR2+425]
	b	.L988
.L933:
	cmp	w25, 3
	bne	.L935
	mov	x4, 0
.L936:
	sub	w0, w4, #80
	strb	w0, [x26,x4]
	add	x4, x4, 1
	cmp	x4, 8
	bne	.L936
	mov	w21, w4
	mov	w28, w4
	b	.L934
.L935:
	cmp	w25, 4
	bne	.L937
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
	b	.L934
.L937:
	cmp	w25, 5
	bne	.L938
	mov	w0, 56
	strb	w0, [x27,128]
	mov	w0, 57
	strb	w0, [x27,129]
	mov	w0, 58
	mov	w21, 8
	strb	w0, [x27,130]
	mov	w0, 59
	strb	w0, [x27,131]
	b	.L1022
.L938:
	cmp	w25, 6
	bne	.L939
	mov	w0, 14
	strb	w0, [x27,128]
	mov	w0, 15
	strb	w0, [x27,129]
	mov	w0, 16
	mov	w21, 12
	strb	w0, [x27,130]
	mov	w0, 17
	strb	w0, [x27,131]
	b	.L1022
.L939:
	cmp	w25, 7
	bne	.L988
	mov	x0, 0
.L940:
	sub	w1, w0, #80
	strb	w1, [x26,x0]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L940
	mov	w0, -44
	mov	w21, 12
	strb	w0, [x27,136]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x27,137]
	b	.L934
.L988:
	mov	w21, 7
.L1022:
	mov	w28, 4
.L934:
	sub	w0, w25, #1
	cmp	w0, 1
	bhi	.L1019
	ldr	x0, [x29,128]
	adrp	x24, .LANCHOR2
	add	x24, x24, :lo12:.LANCHOR2
	mov	w23, 0
	add	x19, x0, :lo12:.LANCHOR0
	mov	w27, 55
	add	x24, x24, 408
.L941:
	ldrb	w0, [x19,57]
	cmp	w0, w23
	bls	.L948
	add	x0, x19, x23, sxtw
	ldr	x2, [x29,136]
	ldrb	w0, [x0,160]
	mov	x20, 160
	ldr	x22, [x19,8]
	mov	x25, 0
	ubfiz	x1, x0, 8, 8
	madd	x20, x0, x20, x2
	add	x22, x22, x1
	add	x20, x20, 32
.L943:
	str	w27, [x22,2056]
	ldrb	w0, [x26,x25]
	str	w0, [x22,2052]
	mov	w0, 80
	bl	timer_delay_ns
	ldr	w0, [x22,2048]
	strb	w0, [x20,x25]
	add	x25, x25, 1
	cmp	w28, w25, uxtb
	bhi	.L943
	mov	x0, 0
.L944:
	add	w2, w0, 8
	mov	x1, 0
.L945:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w5, [x3,4]
	ldrb	w3, [x20,x0]
	add	w3, w5, w3
	strb	w3, [x20,w2,sxtw]
	add	w2, w2, 8
	bne	.L945
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L944
	add	w23, w23, 1
	strb	wzr, [x20,16]
	strb	wzr, [x20,24]
	strb	wzr, [x20,32]
	uxtb	w23, w23
	strb	wzr, [x20,40]
	strb	wzr, [x20,48]
	strb	wzr, [x20,41]
	strb	wzr, [x20,49]
	b	.L941
.L1019:
	sub	w0, w25, #3
	cmp	w0, 4
	bhi	.L948
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
.L949:
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,57]
	cmp	w1, w20
	bhi	.L987
.L948:
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
.L987:
	add	x1, x0, x20, sxtw
	ldrb	w1, [x1,160]
	ldr	x0, [x0,8]
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
	bne	.L950
	mov	w0, 64
	str	w1, [x23,2052]
	str	w0, [x23,2048]
	mov	w0, 204
	b	.L1023
.L950:
	cmp	w19, 1
	bhi	.L952
	ldrb	w0, [x27,128]
	str	w0, [x23,2052]
	mov	w0, 82
	b	.L1024
.L952:
	mov	w0, 174
	str	w0, [x23,2052]
	str	wzr, [x23,2048]
	mov	w0, 176
.L1023:
	str	w0, [x23,2052]
	mov	w0, 77
.L1024:
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
	bne	.L953
	mov	w0, 31
	str	w0, [x23,2052]
	b	.L954
.L953:
	str	wzr, [x23,2052]
.L954:
	mov	w7, 2
	str	w7, [x23,2052]
	str	wzr, [x23,2052]
	mov	w0, 48
	str	w0, [x23,2056]
	str	x7, [x29,104]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w1, 16
	bls	.L956
	ldr	x7, [x29,104]
	cmp	w25, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L956:
	adrp	x0, .LANCHOR4
	mov	x7, 0
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1072]
.L957:
	ldr	w8, [x23,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L957
	cmp	w25, 7
	bne	.L958
	mov	w1, 0
.L960:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L959
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L959
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L960
	b	.L961
.L959:
	cmp	w1, 7
	bne	.L962
.L961:
	adrp	x0, .LC106
	mov	w1, 0
	add	x0, x0, :lo12:.LC106
	bl	printk
.L963:
	b	.L963
.L958:
	cmp	w25, 6
	bne	.L962
	mov	x1, 0
.L964:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L962
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L962
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L964
	adrp	x0, .LC106
	mov	w1, 0
	add	x0, x0, :lo12:.LC106
	bl	printk
.L966:
	b	.L966
.L962:
	add	x0, x26, :lo12:.LANCHOR4
	ldr	x9, [x0,1072]
	mov	x0, 0
.L967:
	ldr	w1, [x29,120]
	cmp	w1, w0
	ble	.L1026
	ldr	w1, [x23,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L967
.L1026:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w8, w22
	mov	w7, 8
	ldr	x11, [x0,1072]
.L970:
	mov	w0, 0
.L969:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L969
	ldr	w0, [x29,124]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L970
	mov	x7, 0
	mov	w15, 1
.L971:
	add	x0, x26, :lo12:.LANCHOR4
	mov	w1, 0
	mov	w10, w1
	ldr	x14, [x0,1072]
.L975:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L973:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L973
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L975
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L971
	add	x1, x26, :lo12:.LANCHOR4
	mov	x7, 0
	mov	w8, w7
	ldr	x1, [x1,1072]
.L978:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L978
	cmp	w8, 7
	ble	.L979
	adrp	x0, .LC107
	mov	w2, 1
	add	x0, x0, :lo12:.LC107
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC106
	mov	w1, 0
	add	x0, x0, :lo12:.LC106
	bl	printk
.L980:
	b	.L980
.L979:
	cmp	w25, 6
	mov	w7, 4
	beq	.L981
	cmp	w25, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L981:
	mov	w8, 0
.L982:
	mov	x1, 0
.L983:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x24,w10,sxtw]
	bhi	.L983
	add	w8, w8, 1
	ldr	x1, [x29,112]
	cmp	w8, w21
	add	w0, w0, w7
	add	x9, x9, x1
	blt	.L982
	mov	w24, 255
	str	w24, [x23,2056]
	bl	nandc_wait_flash_ready
	cmp	w19, 1
	mov	w0, 56
	bhi	.L1025
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
.L1025:
	str	w0, [x23,2056]
	add	w20, w20, 1
	bl	nandc_wait_flash_ready
	uxtb	w20, w20
	b	.L949
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
	bhi	.L1027
	bl	hynix_get_read_retry_default
.L1027:
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
	ldr	x0, [x20,8]
	bl	wait_for_nandc_xfer_completed
	ldr	x21, [x20,8]
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w0, 9
	bne	.L1030
	ldr	w0, [x21,16]
	str	w0, [x29,64]
	ldr	w20, [x21,48]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1050
	adrp	x22, .LC110
	adrp	x23, .LC109
	add	x22, x22, :lo12:.LC110
	add	x23, x23, :lo12:.LC109
	b	.L1032
.L1050:
	adrp	x22, .LC108
	adrp	x23, .LC109
	mov	w20, 0
	add	x22, x22, :lo12:.LC108
	add	x23, x23, :lo12:.LC109
.L1031:
	ldr	w2, [x21,64]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 6
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1038
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,8]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1033
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1033
.L1038:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 104
	ldr	w0, [x20,32]
	cbz	w0, .L1039
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
	b	.L1075
.L1033:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1031
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
	ldr	x1, [x1,8]
	bl	rknand_print_hex
	b	.L1031
.L1041:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,8]
	ldr	w0, [x0,16]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1032
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,64]
	mov	x0, x22
	ubfx	x3, x3, 16, 6
	bl	printk
	ldr	x1, [x24,8]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
.L1032:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1041
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 104
	ldr	w0, [x20,32]
	cbz	w0, .L1039
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
	b	.L1074
.L1030:
	ldr	w0, [x21,8]
	str	w0, [x29,64]
	ldr	w20, [x21,16]
	ubfx	x20, x20, 1, 1
	cbnz	w20, .L1051
	adrp	x22, .LC110
	adrp	x23, .LC109
	add	x22, x22, :lo12:.LC110
	add	x23, x23, :lo12:.LC109
	b	.L1043
.L1051:
	adrp	x22, .LC108
	adrp	x23, .LC109
	mov	w20, 0
	add	x22, x22, :lo12:.LC108
	add	x23, x23, :lo12:.LC109
.L1042:
	ldr	w2, [x21,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L1045
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,8]
	ldr	w0, [x0]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L1044
	ldr	w0, [x29,72]
	tbz	x0, 17, .L1044
	ldr	w1, [x29,72]
	adrp	x0, .LC111
	add	x0, x0, :lo12:.LC111
	bl	printk
	b	.L1045
.L1044:
	add	w20, w20, 1
	and	w0, w20, 16777215
	cbnz	w0, .L1042
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
	ldr	x1, [x1,8]
	bl	rknand_print_hex
	b	.L1042
.L1045:
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 104
	ldr	w0, [x20,32]
	cbz	w0, .L1039
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
.L1075:
	mov	w2, 0
	b	.L1073
.L1049:
	add	x24, x19, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x0, [x24,8]
	ldr	w0, [x0,8]
	str	w0, [x29,64]
	and	w0, w20, 16777215
	cbnz	w0, .L1043
	ldr	w2, [x29,64]
	mov	w1, w20
	ldr	w3, [x21,28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	ldr	x1, [x24,8]
	mov	x0, x23
	mov	w2, 4
	mov	w3, 256
	bl	rknand_print_hex
.L1043:
	ldr	w0, [x29,64]
	tbz	x0, 20, .L1049
	add	x20, x19, :lo12:.LANCHOR0
	add	x20, x20, 104
	ldr	w0, [x20,32]
	cbz	w0, .L1039
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
.L1074:
	mov	w2, 1
.L1073:
	bl	rknand_dma_unmap_single
.L1039:
	add	x19, x19, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	str	wzr, [x19,136]
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
	cbnz	w19, .L1077
	adrp	x20, .LANCHOR0
	ldr	x4, [x29,56]
	add	x3, x20, :lo12:.LANCHOR0
	ldrb	w1, [x20,#:lo12:.LANCHOR0]
	cmp	w1, 9
	bne	.L1078
	ldr	x5, [x3,8]
	lsr	w21, w21, 2
	mov	w4, 1
	mov	w3, w0
.L1079:
	cmp	w3, w21
	bcs	.L1105
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
	tbnz	x1, 2, .L1096
	ldr	w1, [x29,72]
	tbnz	x1, 18, .L1096
	ldr	w2, [x29,72]
	ldr	w1, [x29,72]
	ubfx	x2, x2, 3, 7
	ubfx	x1, x1, 19, 7
	cmp	w2, w1
	ldr	w1, [x29,72]
	ble	.L1081
	ubfx	x1, x1, 3, 7
	b	.L1082
.L1081:
	ubfx	x1, x1, 19, 7
.L1082:
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1080
.L1096:
	mov	w0, -1
.L1080:
	add	w3, w3, 1
	b	.L1079
.L1105:
	add	x20, x20, :lo12:.LANCHOR0
	cmp	w4, wzr
	ldr	x1, [x20,8]
	ldr	w19, [x1]
	mov	w1, 512
	csel	w0, w0, w1, eq
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w19, w1
	cmp	w1, 139264
	bne	.L1077
	adrp	x0, .LC112
	mov	w1, w19
	add	x0, x0, :lo12:.LC112
	orr	w19, w19, 131072
	bl	printk
	ldr	x0, [x20,8]
	b	.L1104
.L1078:
	ldrb	w0, [x3,148]
	mov	w6, 128
	lsr	w7, w21, 1
	mov	w2, 1
	cmp	w0, 25
	mov	w0, 64
	csel	w6, w0, w6, cc
	mov	w1, w19
	mov	w5, w19
.L1086:
	cmp	w5, w7
	add	w8, w1, w6
	bcs	.L1106
	ldr	x0, [x3,104]
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
	b	.L1086
.L1106:
	add	x0, x20, :lo12:.LANCHOR0
	mov	w5, 0
	lsr	w21, w21, 2
	ldr	x7, [x0,8]
	mov	w0, w5
.L1088:
	cmp	w5, w21
	bcs	.L1107
	uxtw	x1, w5
	add	x1, x1, 8
	ldr	w1, [x7,x1,lsl 2]
	str	w1, [x29,72]
	ldr	w1, [x29,72]
	tbnz	x1, 2, .L1099
	ldr	w1, [x29,72]
	tbnz	x1, 15, .L1099
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
	bls	.L1090
	ubfx	x2, x1, 3, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 27, 1
	b	.L1103
.L1090:
	ubfx	x2, x1, 16, 5
	ldr	w1, [x29,72]
	ubfx	x1, x1, 29, 1
.L1103:
	orr	w1, w2, w1, lsl 5
	cmp	w0, w1
	csel	w0, w0, w1, cs
	b	.L1089
.L1099:
	mov	w0, -1
.L1089:
	add	w5, w5, 1
	b	.L1088
.L1107:
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w0, #1
	ldr	x1, [x1,8]
	str	wzr, [x1,16]
	beq	.L1093
	ldr	w2, [x4]
	cmn	w2, #1
	bne	.L1093
	ldr	w2, [x4,4]
	cmn	w2, #1
	bne	.L1093
	ldr	w2, [x22]
	cmn	w2, #1
	mov	w2, 512
	csel	w0, w0, w2, ne
.L1093:
	ldr	w19, [x1]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w19, w1
	cmp	w1, 139264
	bne	.L1077
	adrp	x0, .LC113
	mov	w1, w19
	add	x0, x0, :lo12:.LC113
	orr	w19, w19, 131072
	bl	printk
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,8]
.L1104:
	str	w19, [x0]
	mov	w0, -1
.L1077:
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
	mov	w24, w1
	mov	x26, x2
	ldr	x19, [x0,8]
	adrp	x0, .LANCHOR2+400
	mov	x25, x3
	mov	w27, w4
	and	w20, w1, 2097151
	ubfx	x23, x24, 24, 2
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1109
	adrp	x0, .LC114
	mov	w1, w21
	add	x0, x0, :lo12:.LC114
	mov	w2, w23
	mov	w3, w24
	bl	printk
.L1109:
	bl	nandc_wait_flash_ready
	mov	w0, w21
	bl	nandc_cs
	cbnz	w23, .L1110
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1111
.L1110:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,48]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1112
	sxtw	x0, w21
	add	x0, x0, 8
	add	x0, x19, x0, lsl 8
	str	w23, [x0,8]
	b	.L1111
.L1112:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1111:
	ubfiz	x5, x21, 8, 8
	and	w24, w24, 255
	add	x19, x19, x5
	lsr	w0, w20, 8
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w24, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w20, 16
	str	w0, [x19,2052]
	mov	w0, 48
	str	w0, [x19,2056]
	cbz	w23, .L1113
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,48]
	ldrb	w0, [x0,12]
	cmp	w0, 3
	bne	.L1113
	add	w20, w20, w20, lsl 1
	sub	w0, w20, #1
	add	w0, w0, w23
	b	.L1125
.L1113:
	mov	w0, w20
.L1125:
	bl	nandc_set_seed
	bl	nandc_wait_flash_ready
	mov	w0, 5
	str	w0, [x19,2056]
	str	wzr, [x19,2052]
	mov	w0, 224
	str	wzr, [x19,2052]
	mov	w1, 0
	str	w0, [x19,2056]
	mov	w2, w27
	mov	w0, w21
	mov	x3, x26
	mov	x4, x25
	bl	nandc_xfer
	mov	w19, w0
	cbnz	w23, .L1115
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1115:
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
	mov	w23, w2
	ldrb	w21, [x19,148]
	mov	x26, x3
	mov	x27, x4
	mov	w28, w5
	uxtb	x20, w25
	bl	nandc_wait_flash_ready
	add	w21, w21, w21, lsl 1
	adrp	x24, .LC115
	ldr	x7, [x19,8]
	asr	w21, w21, 2
	mov	w22, 0
	mov	w19, -1
	adrp	x9, .LANCHOR4
	lsl	x10, x20, 8
	mov	w11, 239
	add	x24, x24, :lo12:.LC115
.L1127:
	add	x6, x9, :lo12:.LANCHOR4
	ldrb	w0, [x6,1088]
	cmp	w22, w0
	bcs	.L1130
	add	x1, x7, x10
	mov	w0, 137
	str	x9, [x29,104]
	str	x10, [x29,120]
	str	w11, [x1,2056]
	str	w0, [x1,2052]
	mov	w0, 200
	str	x7, [x29,128]
	str	x11, [x29,136]
	str	x6, [x29,112]
	str	x1, [x29,152]
	bl	timer_delay_ns
	ldr	x1, [x29,152]
	add	w8, w22, 1
	mov	x2, x26
	mov	x3, x27
	mov	w4, w28
	mov	w0, w25
	str	w8, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	mov	w1, w23
	str	x8, [x29,144]
	bl	flash_read_page
	mov	w5, w0
	mov	w1, w22
	mov	x0, x24
	mov	w2, w23
	mov	w3, w22
	mov	w4, w19
	str	x5, [x29,152]
	bl	printk
	ldr	x5, [x29,152]
	ldr	x8, [x29,144]
	cmn	w5, #1
	ldr	x11, [x29,136]
	ldr	x7, [x29,128]
	ldr	x10, [x29,120]
	ldr	x9, [x29,104]
	beq	.L1128
	cmn	w19, #1
	ldr	x6, [x29,112]
	csel	w19, w19, w5, ne
	cmp	w5, w21
	ldr	x26, [x6,1072]
	ldr	x27, [x6,1080]
	bcc	.L1135
.L1128:
	mov	w22, w8
	b	.L1127
.L1135:
	mov	w19, w5
.L1130:
	add	x20, x7, x20, lsl 8
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
	bcc	.L1132
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1132:
	cmn	w19, #1
	beq	.L1136
	cmp	w19, 256
	bne	.L1133
.L1136:
	adrp	x0, .LC116
	mov	w1, w22
	add	x0, x0, :lo12:.LC116
	mov	w2, w23
	mov	w3, w22
	mov	w4, w19
	bl	printk
.L1133:
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
	ldr	x26, [x0,8]
	lsl	x22, x22, 8
	ldrb	w0, [x0,149]
	add	x24, x26, x22
	cmp	w0, 36
	csel	w1, w2, w1, ne
	cmp	w0, 36
	mov	w0, 26
	mov	w23, w1
	csel	w21, w21, w0, ne
	cbnz	w19, .L1147
	adrp	x0, .LC117
	str	x22, [x29,112]
	add	x0, x0, :lo12:.LC117
	mov	w19, -1
	mov	w23, 1
	str	x0, [x29,104]
	adrp	x22, .LANCHOR4
.L1154:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1148
	mov	w1, w23
	mov	w2, 0
	bl	toshiba_tlc_set_rr_para
	ldr	x0, [x29,112]
	mov	w1, 93
	add	x0, x26, x0
	str	w1, [x0,8]
	b	.L1149
.L1148:
	mov	w1, w23
	bl	toshiba_3d_set_slc_rr_para
.L1149:
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
	tbz	x0, 4, .L1150
	ldr	x0, [x29,104]
	mov	w3, w4
	mov	w1, w23
	mov	w2, w25
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
.L1150:
	cmn	w4, #1
	beq	.L1151
	add	x0, x22, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1072]
	ldr	x28, [x0,1080]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,148]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1170
.L1151:
	add	w23, w23, 1
	cmp	w23, w21
	bne	.L1154
	b	.L1153
.L1170:
	mov	w21, w23
	mov	w19, w4
.L1153:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1155
	mov	w1, 0
	mov	w2, w1
	b	.L1193
.L1155:
	mov	w1, 0
	bl	toshiba_3d_set_slc_rr_para
	b	.L1156
.L1147:
	adrp	x0, .LC118
	orr	w25, w25, w19, lsl 24
	add	x0, x0, :lo12:.LC118
	mov	w19, -1
	mov	w21, 1
	str	x0, [x29,112]
.L1163:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1157
	mov	w1, w21
	mov	w2, 1
	bl	toshiba_tlc_set_rr_para
	add	x0, x26, x22
	mov	w1, 93
	b	.L1192
.L1157:
	mov	w1, w21
	bl	toshiba_3d_set_tlc_rr_para
	add	x0, x26, x22
	mov	w1, 38
.L1192:
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
	tbz	x0, 4, .L1159
	ldr	x0, [x29,112]
	mov	w3, w4
	mov	w1, w21
	mov	w2, w25
	str	x4, [x29,104]
	bl	printk
	ldr	x4, [x29,104]
.L1159:
	cmn	w4, #1
	beq	.L1160
	adrp	x0, .LANCHOR4
	cmn	w19, #1
	add	x0, x0, :lo12:.LANCHOR4
	csel	w19, w19, w4, ne
	ldr	x27, [x0,1072]
	ldr	x28, [x0,1080]
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,148]
	add	w0, w0, w0, lsl 1
	cmp	w4, w0, lsr 2
	bcc	.L1171
.L1160:
	add	w21, w21, 1
	cmp	w21, w23
	bne	.L1163
	b	.L1162
.L1171:
	mov	w19, w4
.L1162:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 36
	mov	x0, x24
	bne	.L1164
	mov	w1, 0
	mov	w2, 1
.L1193:
	bl	toshiba_tlc_set_rr_para
	b	.L1156
.L1164:
	mov	w1, 0
	bl	toshiba_3d_set_tlc_rr_para
.L1156:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 36
	bne	.L1165
	ldr	x0, [x29,128]
	ubfiz	x23, x0, 8, 8
	mov	w0, 85
	add	x26, x26, x23
	str	w0, [x26,2056]
	mov	w0, 255
	str	wzr, [x26,2052]
	str	wzr, [x26,2048]
	str	w0, [x26,2056]
.L1165:
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,148]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1166
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1166:
	cmn	w19, #1
	beq	.L1172
	cmp	w19, 256
	bne	.L1167
.L1172:
	adrp	x0, .LC119
	mov	w1, w21
	add	x0, x0, :lo12:.LC119
	mov	w2, w25
	mov	w3, w21
	mov	w4, w19
	bl	printk
.L1167:
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
	ldrb	w0, [x1,149]
	ldr	x22, [x1,8]
	sub	w0, w0, #67
	add	x21, x22, x21, lsl 8
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L1195
	ldrb	w0, [x1,96]
	cbz	w0, .L1196
	mov	w0, 1
	mov	w24, 1
	bl	nandc_set_if_mode
.L1196:
	ubfiz	x0, x23, 8, 8
	mov	w1, 92
	add	x0, x22, x0
	str	w1, [x0,2056]
	mov	w1, 197
	str	w1, [x0,2056]
.L1195:
	ldr	x0, [x29,112]
	mov	w20, 1
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x23, 8, 8
	str	x0, [x29,96]
.L1197:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1088]
	add	w0, w0, 1
	cmp	w20, w0
	bcs	.L1228
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, w20
	ldrb	w0, [x0,149]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1198
	bl	sandisk_set_rr_para
	b	.L1199
.L1198:
	bl	toshiba_set_rr_para
.L1199:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	cmp	w0, 34
	bne	.L1200
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1088]
	sub	w0, w0, #3
	cmp	w20, w0
	bne	.L1200
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x22, x0
	str	w1, [x0,8]
.L1200:
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
	beq	.L1203
	cmn	w25, #1
	csel	w25, w25, w0, ne
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x27, [x0,1072]
	ldr	x28, [x0,1080]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,148]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1205
.L1203:
	add	w20, w20, 1
	b	.L1197
.L1228:
	mov	w26, w25
.L1205:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x0,149]
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x21
	bhi	.L1207
	bl	sandisk_set_rr_para
	b	.L1208
.L1207:
	bl	toshiba_set_rr_para
.L1208:
	ldr	x0, [x29,112]
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	add	x22, x22, x0, lsl 8
	mov	w0, 255
	str	w0, [x22,8]
	ldrb	w0, [x19,148]
	add	w0, w0, w0, lsl 1
	cmp	w26, w0, lsr 2
	bcc	.L1209
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L1209:
	cmn	w26, #1
	beq	.L1215
	cmp	w26, 256
	bne	.L1210
.L1215:
	adrp	x0, .LC119
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC119
	mov	w1, w20
	mov	w3, w20
	mov	w4, w26
	bl	printk
.L1210:
	bl	nandc_wait_flash_ready
	cbz	w24, .L1212
	mov	w0, 4
	bl	nandc_set_if_mode
.L1212:
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
	ldr	x5, [x0,152]
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
.L1230:
	cmp	w6, w24
	bcs	.L1234
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
	beq	.L1232
	add	x1, x7, :lo12:.LANCHOR4
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldr	x25, [x1,1072]
	ldr	x26, [x1,1080]
	add	x1, x28, :lo12:.LANCHOR0
	ldrb	w1, [x1,148]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L1239
.L1232:
	add	w6, w6, 1
	b	.L1230
.L1239:
	mov	w19, w0
.L1234:
	add	x28, x28, :lo12:.LANCHOR0
	add	x21, x27, x21
	ldrb	w0, [x28,148]
	strb	w20, [x21,8]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L1236
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L1236:
	cmn	w19, #1
	beq	.L1240
	cmp	w19, 256
	bne	.L1237
.L1240:
	adrp	x0, .LC120
	mov	w1, w6
	add	x0, x0, :lo12:.LC120
	mov	w2, w23
	mov	w3, w6
	mov	w4, w19
	bl	printk
.L1237:
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
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w19, 0
	uxtb	w25, w0
	mov	w23, w1
	mov	x26, x2
	mov	x27, x3
	str	w4, [x29,156]
	adrp	x24, .LC121
	bl	nandc_get_ddr_para
	mov	w7, w19
	mov	w8, w0
	mov	w6, w19
	mov	w20, w19
	mov	w28, 1024
	mov	w21, 6
	mov	w22, -1
	add	x24, x24, :lo12:.LC121
	adrp	x9, .LANCHOR4
	adrp	x10, .LANCHOR0
.L1254:
	mov	w0, w21
	str	x10, [x29,104]
	str	x9, [x29,112]
	str	x8, [x29,120]
	str	x7, [x29,128]
	str	x6, [x29,136]
	bl	nandc_set_ddr_para
	ldr	w4, [x29,156]
	mov	w1, w23
	mov	x2, x26
	mov	x3, x27
	mov	w0, w25
	bl	flash_read_page
	mov	w4, w0
	mov	w3, w4
	mov	x0, x24
	mov	w1, w21
	mov	w2, w23
	str	x4, [x29,144]
	bl	printk
	ldr	x4, [x29,144]
	add	w0, w28, 1
	ldr	x6, [x29,136]
	cmp	w4, w0
	ldr	x7, [x29,128]
	ldr	x8, [x29,120]
	ldr	x9, [x29,112]
	ldr	x10, [x29,104]
	bhi	.L1250
	add	x0, x10, :lo12:.LANCHOR0
	add	x1, x9, :lo12:.LANCHOR4
	ldrb	w0, [x0,148]
	ldr	x26, [x1,1072]
	ldr	x27, [x1,1080]
	cmp	w4, w0, lsr 2
	bcs	.L1259
	add	w20, w20, 1
	cmp	w20, 7
	bls	.L1259
	sub	w19, w21, w20
	mov	w28, w4
	mov	w22, 0
	b	.L1252
.L1250:
	cmp	w6, w20
	bcs	.L1260
	cmp	w20, 7
	sub	w7, w19, w20
	bhi	.L1253
	mov	w6, w20
	b	.L1260
.L1259:
	mov	w19, w21
	mov	w28, w4
	mov	w22, 0
	b	.L1251
.L1260:
	mov	w20, 0
.L1251:
	add	w21, w21, 2
	cmp	w21, 50
	bne	.L1254
.L1252:
	cmp	w6, w20
	csel	w19, w19, w7, ls
.L1253:
	cbz	w19, .L1255
	adrp	x0, .LANCHOR0+148
	ldrb	w1, [x0,#:lo12:.LANCHOR0+148]
	mov	w0, 3
	udiv	w1, w1, w0
	cmp	w28, w1
	bcs	.L1255
	adrp	x0, .LC122
	mov	w1, w19
	add	x0, x0, :lo12:.LC122
	bl	printk
	mov	w0, w19
	b	.L1265
.L1255:
	mov	w0, w8
.L1265:
	bl	nandc_set_ddr_para
	cbz	w22, .L1257
	adrp	x0, .LC123
	mov	w1, w25
	add	x0, x0, :lo12:.LC123
	mov	w2, w23
	mov	w28, w22
	bl	printk
.L1257:
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
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
	ldrb	w2, [x1,57]
	mov	w23, w4
	ubfx	x21, x19, 24, 2
	cmp	w2, w0
	bhi	.L1267
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 136
	mov	w2, 386
	bl	printk
.L1268:
	b	.L1268
.L1267:
	add	x0, x1, x0, sxtw
	ldrb	w22, [x0,160]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1269
	adrp	x0, .LC124
	mov	w1, w22
	add	x0, x0, :lo12:.LC124
	mov	w2, w19
	bl	printk
.L1269:
	cbnz	w21, .L1270
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w1, [x0,58]
	cbz	w1, .L1271
	ldrb	w0, [x0,168]
	cbz	w0, .L1270
.L1271:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,170]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,168]
	sub	w19, w19, w0
	cbz	w2, .L1272
	add	w19, w0, w19, lsl 1
	b	.L1270
.L1272:
	add	x1, x1, 172
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1270:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w21, w0
	bne	.L1279
	add	x26, x20, :lo12:.LANCHOR0
	ldrb	w27, [x26,144]
	cbnz	w27, .L1274
.L1277:
	adrp	x0, .LANCHOR4+1096
	ldr	x6, [x0,#:lo12:.LANCHOR4+1096]
	cbnz	x6, .L1275
	b	.L1276
.L1274:
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	strb	wzr, [x26,144]
	bl	flash_read_page
	strb	w27, [x26,144]
	cmn	w0, #1
	beq	.L1277
	b	.L1295
.L1275:
	mov	w0, w22
	mov	w1, 0
	mov	w2, w19
	mov	x3, x25
	mov	x4, x24
	mov	w5, w23
	blr	x6
	cmn	w0, #1
	bne	.L1295
.L1276:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC125
	add	x0, x0, :lo12:.LC125
	mov	w1, 0
	mov	w2, w19
	mov	w3, -1
	ldrb	w4, [x20,144]
	bl	printk
	ldrb	w0, [x20,96]
	cbz	w0, .L1279
	mov	w0, w22
	mov	w1, w19
	mov	x2, x25
	mov	x3, x24
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1295:
	mov	w21, w0
.L1279:
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
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	adrp	x0, .LANCHOR4+972
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	ldrh	w19, [x0,#:lo12:.LANCHOR4+972]
	adrp	x0, .LANCHOR2+394
	stp	x21, x22, [sp,32]
	uxth	w22, w1
	sub	w19, w19, #1
	ldrh	w26, [x0,#:lo12:.LANCHOR2+394]
	mov	w0, w23
	sxth	w19, w19
	mov	x24, x2
	stp	x27, x28, [sp,80]
	mul	w26, w22, w26
	mov	x21, x3
	mov	w25, w4
	add	w1, w26, w19
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L1297
	mov	w27, 0
	mov	w28, 2
.L1298:
	cmp	w27, w19
	bgt	.L1297
	add	w0, w27, w19
	mov	x2, x24
	mov	x3, x21
	mov	w4, w25
	sdiv	w20, w0, w28
	mov	w0, w23
	add	w1, w26, w20, sxth
	bl	flash_read_page_en
	cmp	w0, 512
	bne	.L1299
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1298
.L1299:
	add	w20, w20, 1
	sxth	w27, w20
	b	.L1298
.L1297:
	ldr	w3, [x21]
	adrp	x0, .LC126
	mov	w1, w22
	mov	w2, w19
	add	x0, x0, :lo12:.LC126
	bl	printk
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	flash_get_last_written_page, .-flash_get_last_written_page
	.align	2
	.global	flash_get_last_written_page_ext
	.type	flash_get_last_written_page_ext, %function
flash_get_last_written_page_ext:
	uxth	w6, w0
	adrp	x0, .LANCHOR0+3218
	stp	x29, x30, [sp, -16]!
	mov	w5, 21
	mov	x8, x1
	add	x29, sp, 0
	ldrh	w0, [x0,#:lo12:.LANCHOR0+3218]
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
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR3
	adrp	x0, .LC127
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR3
	add	x4, x20, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC127
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w1, 49
	add	x2, x2, 160
	ldr	x4, [x4,152]
	mov	w3, 2048
	mov	w22, 4
	adrp	x24, .LANCHOR4
	bl	printk
.L1306:
	add	x19, x20, :lo12:.LANCHOR0
	add	x21, x24, :lo12:.LANCHOR4
	mov	w0, 0
	mov	w4, 4
	adrp	x23, .LANCHOR4
	ldrh	w1, [x19,170]
	ldr	x2, [x19,152]
	ldr	x3, [x21,1104]
	mul	w1, w22, w1
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1304
	ldr	x2, [x19,152]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	w1, [x2]
	cmp	w1, w0
	beq	.L1305
.L1304:
	add	w22, w22, 1
	cmp	w22, 16
	bne	.L1306
.L1327:
	mov	w0, -1
	b	.L1326
.L1305:
	ldrb	w1, [x2,36]
	mov	w4, 4
	ldrh	w3, [x19,170]
	strb	w1, [x21,1112]
	ldrb	w0, [x2,37]
	mul	w1, w1, w3
	ldr	x3, [x21,1104]
	strb	w0, [x21,1113]
	mov	w0, 0
	mov	w21, 0
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1308
	ldr	x0, [x19,152]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	mov	w21, 0
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1308
	ldr	w21, [x0,4]
.L1308:
	add	x19, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w2, [x19,1113]
	ldrh	w1, [x22,170]
	ldr	x3, [x19,1104]
	mul	w1, w2, w1
	ldr	x2, [x22,152]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1309
	ldr	x0, [x22,152]
	mov	w1, 21321
	movk	w1, 0x5359, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	bne	.L1309
	ldr	w1, [x0,4]
	cmp	w21, w1
	bcs	.L1309
	ldrb	w1, [x0,37]
	ldrb	w0, [x0,36]
	strb	w1, [x19,1112]
	strb	w0, [x19,1113]
.L1309:
	add	x21, x23, :lo12:.LANCHOR4
	add	x22, x20, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w4, 4
	ldrb	w1, [x21,1112]
	ldr	x3, [x21,1104]
	ldr	x2, [x22,152]
	bl	flash_get_last_written_page
	sxth	w19, w0
	add	w0, w0, 1
	strh	w0, [x21,1114]
	ldrb	w0, [x21,1112]
	ldrh	w21, [x22,170]
	mul	w21, w0, w21
.L1310:
	tbnz	w19, #31, .L1328
	add	x22, x20, :lo12:.LANCHOR0
	add	x3, x23, :lo12:.LANCHOR4
	mov	w0, 0
	add	w1, w19, w21
	mov	w4, 4
	ldr	x2, [x22,152]
	ldr	x3, [x3,1104]
	bl	flash_read_page_en
	cmn	w0, #1
	beq	.L1311
	ldr	x0, [x22,152]
	ldr	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	cmp	w1, w0
	bne	.L1311
.L1313:
	mov	w0, 0
	b	.L1326
.L1311:
	sub	w19, w19, #1
	sxth	w19, w19
	b	.L1310
.L1328:
	cmn	w19, #1
	bne	.L1313
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	ldr	x1, [x20,152]
	ldr	w1, [x1]
	bl	printk
	b	.L1327
.L1326:
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
	ldrb	w0, [x21,720]
	add	x20, x20, :lo12:.LANCHOR4
	strb	w22, [x21,96]
	bl	flash_set_interface_mode
	ldrb	w0, [x21,720]
	bl	nandc_set_if_mode
	ldr	x2, [x20,1120]
	mov	w1, w23
	ldr	x3, [x20,1128]
	mov	w4, 4
	mov	w0, w24
	bl	flash_ddr_tunning_read
	ldr	x2, [x20,1120]
	mov	w0, w24
	ldr	x3, [x20,1128]
	mov	w1, w23
	mov	w4, 4
	bl	flash_read_page
	cmn	w0, #1
	bne	.L1330
	ldrb	w0, [x21,720]
	tbz	x0, 0, .L1330
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, w22
	bl	nandc_set_if_mode
	strb	wzr, [x21,96]
	b	.L1331
.L1330:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 1
	strb	w0, [x19,96]
.L1331:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_ddr_para_scan, .-flash_ddr_para_scan
	.align	2
	.type	id_block_read_data.constprop.29, %function
id_block_read_data.constprop.29:
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
	ldr	x0, [x19,48]
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
	ldrb	w2, [x19,144]
	str	w2, [x29,136]
	uxth	w0, w0
	strb	wzr, [x19,144]
	udiv	w2, w26, w0
	msub	w0, w2, w0, w26
	str	w0, [x29,172]
	sub	w0, w26, w0
	str	w0, [x29,160]
	ldr	w0, [x29,172]
	and	w0, w0, 3
	str	w0, [x29,168]
	adrp	x0, .LC129
	add	x0, x0, :lo12:.LC129
	str	x0, [x29,112]
.L1336:
	cmp	w22, w27
	bcs	.L1373
	ldr	w0, [x29,168]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	sub	w0, w23, w0
	uxth	w0, w0
	str	w0, [x29,144]
	ldr	w0, [x29,172]
	ldrb	w1, [x1,1136]
	add	w20, w22, w0
	add	x0, x19, 172
	udiv	w20, w20, w23
	and	w20, w20, 65535
	ldrh	w0, [x0,w20,sxtw 1]
	cbnz	w1, .L1337
	mov	w20, w0
	b	.L1338
.L1337:
	ldrb	w1, [x19,168]
	lsl	w0, w20, 1
	cmp	w1, wzr
	csel	w20, w0, w20, ne
.L1338:
	ldr	w1, [x29,160]
	ldr	w0, [x29,164]
	ldrb	w28, [x19,56]
	madd	w0, w20, w0, w1
	ldr	w1, [x29,168]
	str	w0, [x29,140]
	add	w24, w0, w1
	ldr	x0, [x19,48]
	ldrb	w0, [x0,9]
	udiv	w24, w24, w0
	ldrb	w0, [x19,148]
	str	w0, [x29,132]
	mov	w0, w28
	bl	nandc_bch_sel
.L1339:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	bne	.L1346
	ldrb	w5, [x19,144]
	cbnz	w5, .L1341
.L1344:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x7, [x0,1096]
	cbnz	x7, .L1342
	b	.L1343
.L1341:
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	str	x5, [x29,104]
	strb	wzr, [x19,144]
	bl	flash_read_page
	cmn	w0, #1
	ldr	x5, [x29,104]
	strb	w5, [x19,144]
	beq	.L1344
	b	.L1372
.L1342:
	mov	w0, 0
	mov	w2, w24
	mov	w1, w0
	mov	x3, x21
	add	x4, x29, 176
	mov	w5, w23
	blr	x7
	cmn	w0, #1
	bne	.L1372
.L1343:
	ldrb	w0, [x19,96]
	cbz	w0, .L1346
	mov	w0, 0
	mov	w1, w24
	mov	x2, x21
	add	x3, x29, 176
	mov	w4, w23
	bl	flash_ddr_tunning_read
.L1372:
	mov	w20, w0
.L1346:
	cmn	w20, #1
	bne	.L1347
	cmp	w28, 16
	beq	.L1347
	mov	w0, 16
	mov	w28, 16
	bl	nandc_bch_sel
	b	.L1339
.L1347:
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
	beq	.L1349
	cbnz	w25, .L1349
	ldr	w1, [x21]
	mov	w0, 18766
	movk	w0, 0x464e, lsl 16
	cmp	w1, w0
	bne	.L1349
	ldr	w0, [x29,144]
	ldrb	w23, [x21,17]
	add	w27, w27, w0
	b	.L1350
.L1349:
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
.L1350:
	ldr	w0, [x29,144]
	add	w22, w0, w22
	uxth	w22, w22
	b	.L1336
.L1373:
	ldr	x0, [x29,120]
	ldrb	w1, [x29,136]
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,144]
	mov	w0, w25
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	id_block_read_data.constprop.29, .-id_block_read_data.constprop.29
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
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w26, w0
	mov	x24, x1
	ldrb	w1, [x21,57]
	ubfx	x0, x26, 21, 3
	mov	x23, x2
	ubfx	x25, x26, 24, 2
	cmp	w1, w0
	bhi	.L1375
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 184
	mov	w2, 794
	bl	printk
.L1376:
	b	.L1376
.L1375:
	add	x0, x21, x0, sxtw
	ldrb	w22, [x0,160]
	ldr	x27, [x21,8]
	mov	w0, w22
	bl	nandc_cs
	cbnz	w25, .L1377
	ldrb	w0, [x21,58]
	cbz	w0, .L1378
	ldrb	w0, [x21,168]
	cbz	w0, .L1377
.L1378:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,170]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,168]
	sub	w19, w19, w0
	cbz	w2, .L1379
	add	w19, w0, w19, lsl 1
	b	.L1377
.L1379:
	add	x1, x1, 172
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
.L1377:
	ubfiz	x5, x22, 8, 8
	mov	w0, 5
	add	x5, x27, x5
	adrp	x21, .LANCHOR2
	add	x28, x21, :lo12:.LANCHOR2
	mov	w1, 0
	add	x28, x28, 368
	mov	x3, x24
	str	w0, [x5,2056]
	and	w0, w19, 255
	str	wzr, [x5,2052]
	mov	x4, x23
	str	wzr, [x5,2052]
	str	w0, [x5,2052]
	lsr	w0, w19, 8
	str	w0, [x5,2052]
	lsr	w0, w19, 16
	ldrb	w2, [x28,9]
	str	w0, [x5,2052]
	mov	w0, 224
	str	w0, [x5,2056]
	mov	w0, w22
	bl	nandc_xfer
	cmn	w0, #1
	mov	w27, w0
	bne	.L1386
	add	x5, x20, :lo12:.LANCHOR0
	ldrb	w6, [x5,144]
	cbnz	w6, .L1381
.L1384:
	adrp	x0, .LANCHOR4+1096
	ldr	x6, [x0,#:lo12:.LANCHOR4+1096]
	cbnz	x6, .L1382
	b	.L1383
.L1381:
	ldrb	w4, [x28,9]
	mov	w0, w22
	orr	w1, w19, w25, lsl 24
	mov	x2, x24
	mov	x3, x23
	str	x6, [x29,96]
	strb	wzr, [x5,144]
	str	x5, [x29,104]
	bl	flash_read_page
	ldr	x5, [x29,104]
	cmn	w0, #1
	ldr	x6, [x29,96]
	strb	w6, [x5,144]
	beq	.L1384
	b	.L1399
.L1382:
	add	x5, x21, :lo12:.LANCHOR2
	mov	w0, w22
	mov	w1, w25
	mov	w2, w19
	mov	x3, x24
	mov	x4, x23
	ldrb	w5, [x5,377]
	blr	x6
	cmn	w0, #1
	bne	.L1399
.L1383:
	add	x20, x20, :lo12:.LANCHOR0
	adrp	x0, .LC130
	add	x0, x0, :lo12:.LC130
	mov	w1, 0
	mov	w2, w26
	mov	w3, -1
	ldrb	w4, [x20,144]
	bl	printk
	ldrb	w0, [x20,96]
	cbz	w0, .L1386
	add	x21, x21, :lo12:.LANCHOR2
	mov	w0, w22
	orr	w1, w19, w25, lsl 24
	mov	x2, x24
	mov	x3, x23
	ldrb	w4, [x21,377]
	bl	flash_ddr_tunning_read
.L1399:
	mov	w27, w0
.L1386:
	mov	w0, w27
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
	ldrb	w20, [x0,3774]
	cmp	w20, 255
	beq	.L1436
	add	x0, x0, 728
	mov	w1, 0
	add	x0, x0, x20, lsl 6
	ldrb	w2, [x0,58]
	ldr	w21, [x0,40]
	sub	w3, w2, #1
	cmp	w3, 9
	bhi	.L1402
	adrp	x0, .L1404
	add	x0, x0, :lo12:.L1404
	ldrb	w0, [x0,w3,uxtw]
	adr	x1, .Lrtx1404
	add	x0, x1, w0, sxtb #2
	br	x0
.Lrtx1404:
	.section	.rodata
	.align	0
	.align	2
.L1404:
	.byte	(.L1403 - .Lrtx1404) / 4
	.byte	(.L1403 - .Lrtx1404) / 4
	.byte	(.L1405 - .Lrtx1404) / 4
	.byte	(.L1405 - .Lrtx1404) / 4
	.byte	(.L1405 - .Lrtx1404) / 4
	.byte	(.L1406 - .Lrtx1404) / 4
	.byte	(.L1407 - .Lrtx1404) / 4
	.byte	(.L1408 - .Lrtx1404) / 4
	.byte	(.L1405 - .Lrtx1404) / 4
	.byte	(.L1408 - .Lrtx1404) / 4
	.text
.L1403:
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1436
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
	b	.L1436
.L1405:
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	tbz	x21, 6, .L1436
	mov	w1, 5
	add	x19, x19, :lo12:.LANCHOR0
	ands	w1, w21, w1
	beq	.L1411
	add	x19, x19, 728
	mov	w0, 11
	add	x20, x19, x20, lsl 6
	mov	w4, 11
	strb	w0, [x20,58]
	adrp	x0, .LC131
	ldrb	w1, [x20,1]
	add	x0, x0, :lo12:.LC131
	ldr	w2, [x20,52]
	ldr	w3, [x20,40]
	bl	printk
	mov	w0, -1
	str	w0, [x20,52]
	b	.L1437
.L1411:
	add	x20, x19, x20, lsl 6
	b	.L1418
.L1408:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w0, 3
	cmp	w2, 10
	mov	w1, 9
	add	x22, x22, 728
	csel	w1, w0, w1, ne
	ubfx	x3, x21, 21, 3
	add	x0, x22, x20, lsl 6
.L1413:
	ldrb	w23, [x0]
	cmp	w23, 255
	beq	.L1438
	add	x0, x22, x23, lsl 6
	ldrb	w2, [x0,58]
	cmp	w2, w1
	bne	.L1413
	ldr	w2, [x0,40]
	ubfx	x2, x2, 21, 3
	cmp	w3, w2
	bne	.L1413
	bl	nandc_iqr_wait_flash_ready
	mov	w0, w21
	mov	w1, 64
	bl	flash_wait_device_ready
	mov	w21, w0
	and	w0, w0, 64
	tbz	x21, 6, .L1421
	ands	w0, w21, 15
	add	x22, x22, x20, lsl 6
	beq	.L1416
	adrp	x0, .LC131
	ldrb	w1, [x22,1]
	ldr	w2, [x22,52]
	add	x0, x0, :lo12:.LC131
	ldr	w3, [x22,40]
	mov	w4, 11
	bl	printk
	mov	w0, 11
	strb	w0, [x22,58]
	mov	w0, -1
	str	w0, [x22,52]
.L1415:
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 728
	add	x20, x19, x20, lsl 6
	add	x23, x19, x23, lsl 6
	ldrb	w0, [x20,58]
	strb	w0, [x23,58]
	ldr	w0, [x20,52]
	str	w0, [x23,52]
.L1437:
	mov	w1, w21
	b	.L1402
.L1416:
	mov	w1, 12
	str	w0, [x22,52]
	strb	w1, [x22,58]
.L1421:
	mov	w21, w0
	b	.L1415
.L1438:
	mov	w1, -1
	b	.L1402
.L1406:
	mov	w1, 32
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 5, .L1436
	ands	w1, w0, 15
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	beq	.L1418
	mov	w1, 11
	strb	w1, [x20,786]
	mov	w1, -1
	str	w1, [x20,780]
	mov	w1, w0
	b	.L1402
.L1418:
	mov	w0, 12
	str	w1, [x20,780]
	strb	w0, [x20,786]
	b	.L1402
.L1407:
	mov	w1, 64
	mov	w0, w21
	bl	flash_wait_device_ready
	tbz	x0, 6, .L1436
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, x20, lsl 6
	str	w0, [x20,780]
	mov	w0, 6
	strb	w0, [x20,786]
.L1436:
	mov	w1, 0
.L1402:
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
.L1440:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3774]
	cmp	w0, 255
	beq	.L1442
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1440
.L1442:
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
	add	x21, x21, 1152
	bl	ftl_get_blk_list_in_sblk
	add	x22, x22, 128
	strh	w19, [x29,64]
	strb	w0, [x29,73]
	mov	w19, 0
.L1446:
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	mov	x23, x0
	ldrh	w1, [x1,3234]
	cmp	w1, w19
	bls	.L1445
	mov	w20, 0
.L1452:
	add	x3, x23, :lo12:.LANCHOR0
	ldrb	w0, [x3,3212]
	cmp	w0, w20
	bls	.L1458
	add	x0, x29, 64
	add	x0, x0, x20, sxtw 1
	ldrh	w0, [x0,16]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1448
	ldrh	w1, [x3,3230]
	mov	x2, x22
	ldrb	w3, [x3,2776]
	madd	w0, w0, w1, w19
	mov	x1, x21
	bl	ftl_read_ppa_page
	cmp	w0, 512
	beq	.L1445
.L1448:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1452
.L1458:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1446
.L1445:
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
	add	x26, x23, 3774
.L1460:
	cbz	w22, .L1472
	ldrb	w27, [x19]
	ldr	w28, [x19,40]
.L1461:
	mov	w0, w28
	mov	w1, 0
	bl	queue_lun_state
	mov	w2, w0
	cbz	w0, .L1473
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1461
.L1473:
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
	beq	.L1472
	add	x19, x23, 728
	sbfiz	x27, x27, 6, 32
	add	x19, x19, x27
	b	.L1460
.L1472:
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	add	x19, x19, 728
.L1465:
	cbz	w21, .L1474
	ldrb	w0, [x20,58]
	cmp	w0, 12
	bne	.L1466
	ldrb	w20, [x20]
	sub	w21, w21, #1
	add	x20, x19, x20, lsl 6
	b	.L1465
.L1466:
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1465
.L1474:
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
	add	x20, x20, 2848
	ldr	x0, [x20,8]
	ldr	x23, [x20,-56]
	cbnz	x0, .L1476
	mov	w0, 1
	bl	buf_alloc
	str	x0, [x20,8]
.L1476:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w24, 0
	ldr	x20, [x21,2856]
	cbnz	x20, .L1477
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 216
	mov	w2, 336
	bl	printk
.L1478:
	b	.L1478
.L1477:
	add	x25, x23, 80
	adrp	x26, .LANCHOR4
.L1499:
	ldrb	w0, [x25,9]
	cmp	w24, w0
	bge	.L1501
	mov	w22, 1
	sxtw	x27, w24
.L1487:
	add	x0, x21, 2848
	ldrh	w1, [x0,20]
	cmp	w22, w1
	bgt	.L1502
	add	x1, x27, 8
	ldrh	w2, [x21,3230]
	ldrh	w0, [x0,16]
	ldrh	w1, [x25,x1,lsl 1]
	mul	w1, w1, w2
	ldrb	w2, [x21,3193]
	cmp	w2, 3
	bne	.L1479
	add	w1, w1, w0
	orr	w1, w1, w22, lsl 24
	b	.L1500
.L1479:
	cmp	w2, 2
	bne	.L1481
	sub	w0, w0, #1
	add	w1, w0, w1
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L1500
.L1481:
	add	w1, w1, w0
.L1500:
	str	w1, [x20,40]
	mov	x0, x20
	mov	w1, 1
	bl	sblk_read_page
	ldr	w2, [x20,52]
	cmn	w2, #1
	beq	.L1482
	add	x3, x26, :lo12:.LANCHOR4
	ldrh	w0, [x21,2870]
	ldr	x1, [x20,24]
	lsl	x0, x0, 2
	ldr	x4, [x3,1016]
	ldr	w5, [x4,x0]
	ldr	w4, [x1,4]
	cmp	w5, w4
	bne	.L1482
	ldr	x3, [x3,1024]
	ldr	w3, [x3,x0]
	ldr	w0, [x1,8]
	cmp	w3, w0
	beq	.L1483
.L1482:
	add	x0, x26, :lo12:.LANCHOR4
	ldrh	w4, [x21,2870]
	lsl	x4, x4, 2
	ldr	x1, [x0,1016]
	ldr	w3, [x1,x4]
	cmn	w3, #1
	beq	.L1483
	adrp	x1, .LANCHOR2+400
	ldr	w1, [x1,#:lo12:.LANCHOR2+400]
	tbz	x1, 10, .L1484
	ldr	x7, [x20,24]
	ldr	x5, [x0,1024]
	ldr	w0, [x7,12]
	str	w0, [sp]
	adrp	x0, .LC132
	add	x0, x0, :lo12:.LC132
	ldr	w4, [x5,x4]
	ldr	w6, [x7,4]
	ldr	w5, [x7]
	ldr	w1, [x20,40]
	ldr	w7, [x7,8]
	bl	printk
.L1484:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23,80]
	ldr	x0, [x19,2832]
	strh	wzr, [x0,x1,lsl 1]
	mov	w0, -1
	b	.L1485
.L1483:
	ldrh	w0, [x21,2870]
	add	w22, w22, 1
	add	w0, w0, 1
	strh	w0, [x21,2870]
	b	.L1487
.L1502:
	add	w24, w24, 1
	b	.L1499
.L1501:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2864]
	add	w0, w0, 1
	strh	w0, [x19,2864]
	mov	w0, 0
.L1485:
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
	ldrb	w0, [x7,57]
	mov	x22, x5
	mov	x21, x6
	cmp	w0, w3
	bhi	.L1504
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 240
	mov	w2, 658
	bl	printk
.L1505:
	b	.L1505
.L1504:
	add	x3, x7, x3, sxtw
	ldrb	w23, [x3,160]
	str	x4, [x29,88]
	mov	w0, w23
	ldr	x19, [x7,8]
	bl	nandc_cs
	ldr	x4, [x29,88]
	cbz	w25, .L1506
	sxtw	x7, w23
	add	x7, x7, 8
	add	x7, x19, x7, lsl 8
	str	w25, [x7,8]
.L1506:
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
	add	x0, x0, 3774
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
.L1512:
	mov	w0, w21
	mov	w1, 1
	bl	queue_lun_state
	mov	w19, w0
	cbz	w0, .L1514
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1512
.L1514:
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
.L1516:
	mov	w0, w20
	mov	w1, 1
	bl	queue_lun_state
	cbz	w0, .L1520
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1516
.L1520:
	cmp	w24, 2
	bne	.L1518
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
	add	x0, x0, 3774
	bl	buf_add_tail
	b	.L1519
.L1518:
	mov	x0, x19
	bl	queue_tlc_prog_cmd
.L1519:
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
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x27, [sp,80]
	uxtb	w22, w0
	and	w19, w1, 2097151
	ubfx	x0, x1, 21, 3
	ubfx	x27, x1, 24, 2
	adrp	x1, .LANCHOR0
	stp	x25, x26, [sp,64]
	add	x26, x1, :lo12:.LANCHOR0
	mov	x20, x1
	stp	x23, x24, [sp,48]
	mov	x24, x2
	mov	x23, x3
	ldrb	w2, [x26,57]
	cmp	w2, w0
	bhi	.L1522
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 272
	mov	w2, 695
	bl	printk
.L1523:
	b	.L1523
.L1522:
	add	x0, x26, x0, sxtw
	ldrb	w21, [x0,160]
	ldr	x25, [x26,8]
	mov	w0, w21
	bl	hynix_reconfig_rr_para
	mov	w0, w21
	bl	nandc_cs
	cbnz	w27, .L1524
	ldrb	w0, [x26,58]
	cbz	w0, .L1525
	ldrb	w0, [x26,168]
	cbz	w0, .L1526
.L1525:
	add	x1, x20, :lo12:.LANCHOR0
	ldrh	w2, [x1,170]
	udiv	w0, w19, w2
	mul	w0, w0, w2
	ldrb	w2, [x1,168]
	sub	w19, w19, w0
	cbz	w2, .L1527
	add	w19, w0, w19, lsl 1
	b	.L1526
.L1527:
	add	x1, x1, 172
	ldrh	w19, [x1,w19,uxtw 1]
	add	w19, w19, w0
	b	.L1528
.L1526:
	mov	w0, w21
	bl	zftl_flash_enter_slc_mode
	b	.L1528
.L1524:
	mov	w0, w21
	bl	zftl_flash_exit_slc_mode
.L1528:
	ubfiz	x0, x21, 8, 8
	add	x25, x25, x0
	mov	w0, 128
	str	w0, [x25,2056]
	and	w0, w19, 255
	str	wzr, [x25,2052]
	str	wzr, [x25,2052]
	str	w0, [x25,2052]
	lsr	w0, w19, 8
	str	w0, [x25,2052]
	lsr	w0, w19, 16
	str	w0, [x25,2052]
	mov	w0, w19
	bl	nandc_set_seed
	adrp	x1, .LANCHOR2+377
	mov	x2, x24
	mov	x3, x23
	mov	w0, 1
	ldrb	w1, [x1,#:lo12:.LANCHOR2+377]
	bl	nandc_xfer_start
	bl	nandc_xfer_done
	str	w22, [x25,2056]
	mov	w0, w21
	bl	nandc_de_cs
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	ldrb	w2, [x1,3774]
	cmp	w2, 255
	beq	.L1533
	ubfx	x4, x4, 21, 3
	add	x1, x1, 728
.L1535:
	add	x3, x1, x2, lsl 6
	ldr	w5, [x3,40]
	ubfx	x5, x5, 21, 3
	cmp	w4, w5
	bne	.L1534
	ldrb	w5, [x3,58]
	add	x3, x3, 48
	cmp	w5, 6
	bne	.L1534
	mov	w1, 3
	strb	w1, [x3,10]
	b	.L1533
.L1534:
	lsl	x2, x2, 6
	ldrb	w2, [x1,x2]
	cmp	w2, 255
	bne	.L1535
.L1533:
	mov	w1, 3
	strb	w1, [x19,58]
	mov	w1, 1
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x19,59]
	mov	w1, -1
	add	x0, x0, 3774
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
	cbz	w0, .L1541
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L1541
	adrp	x0, .LC133
	ldr	w1, [x19,40]
	add	x0, x0, :lo12:.LC133
	mov	w2, w20
	bl	printk
.L1541:
	adrp	x21, .LANCHOR0
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	mov	w23, 1
	add	x25, x21, 728
	mov	w26, 21
.L1542:
	cbz	w20, .L1569
	ldrb	w24, [x19]
	ldr	w27, [x19,40]
.L1543:
	mov	w0, w27
	mov	w1, 1
	bl	queue_lun_state
	mov	w28, w0
	cbz	w0, .L1570
	bl	queue_wait_first_req_completed
	bl	queue_remove_completed_req
	b	.L1543
.L1570:
	cmp	w20, 1
	beq	.L1545
	ldrb	w0, [x21,721]
	cbnz	w0, .L1546
.L1545:
	mov	x0, x19
	bl	queue_prog_cmd
	b	.L1547
.L1546:
	ldrb	w1, [x19]
	ubfx	x2, x27, 21, 3
	cmp	w1, 255
	bne	.L1548
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 296
	mov	w2, 484
	bl	printk
.L1549:
	b	.L1549
.L1548:
	sbfiz	x1, x1, 6, 32
	add	x1, x25, x1
	ldr	w5, [x1,40]
	ubfx	x0, x5, 21, 3
	cmp	w2, w0
	bne	.L1550
	ldrh	w3, [x21,3218]
	ldrb	w6, [x21,3216]
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
	beq	.L1551
	and	w27, w4, w27
	and	w4, w4, w5
	cmp	w27, w4
	bne	.L1551
	cmp	w22, w6
	beq	.L1550
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
	add	x0, x21, 3774
	bl	buf_add_tail
	b	.L1547
.L1551:
	mov	x0, x19
	mov	w22, 0
	bl	queue_prog_cmd
	b	.L1547
.L1550:
	mov	x0, x19
	mov	w22, w28
	bl	queue_prog_cmd
.L1547:
	sbfiz	x19, x24, 6, 32
	sub	w20, w20, #1
	add	x19, x25, x19
	b	.L1542
.L1569:
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
	ldrb	w1, [x0,3772]
	cbz	w1, .L1572
	add	x2, x20, :lo12:.LANCHOR5
	add	x0, x0, 728
	ldrb	w2, [x2,384]
	add	x0, x0, x2, lsl 6
	bl	sblk_prog_page
.L1572:
	add	x2, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, -1
	strb	wzr, [x2,3772]
	strb	w0, [x20,384]
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
	adrp	x0, .LANCHOR0+3772
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+3772]
	cbz	w0, .L1576
	bl	timer_get_time
	adrp	x1, .LANCHOR5+388
	ldr	w1, [x1,#:lo12:.LANCHOR5+388]
	add	w1, w1, 100
	cmp	w0, w1
	bls	.L1576
	bl	ftl_flush
.L1576:
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_cache_flush, .-zftl_cache_flush
	.align	2
	.global	flash_prog_page
	.type	flash_prog_page, %function
flash_prog_page:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	x22, w0
	adrp	x0, .LANCHOR0+8
	adrp	x21, .LANCHOR2
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR0+8]
	add	x0, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x20, x22
	add	x22, x22, 8
	ldr	w0, [x0,400]
	mov	w23, w1
	mov	x25, x3
	mov	x26, x2
	mov	w3, w4
	add	x22, x19, x22, lsl 8
	and	w27, w1, 2097151
	ubfx	x24, x23, 24, 2
	tbz	x0, 4, .L1581
	adrp	x0, .LC134
	mov	w2, w24
	add	x0, x0, :lo12:.LC134
	bl	printk
.L1581:
	bl	nandc_wait_flash_ready
	mov	w0, w20
	bl	hynix_reconfig_rr_para
	mov	w0, w20
	bl	nandc_cs
	mov	w0, w20
	cbnz	w24, .L1582
	bl	zftl_flash_enter_slc_mode
	b	.L1583
.L1582:
	bl	zftl_flash_exit_slc_mode
.L1583:
	ubfiz	x4, x20, 8, 8
	mov	w0, 128
	add	x19, x19, x4
	add	x21, x21, :lo12:.LANCHOR2
	str	w0, [x19,2056]
	and	w0, w23, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w0, [x19,2052]
	lsr	w0, w27, 8
	str	w0, [x19,2052]
	lsr	w0, w27, 16
	str	w0, [x19,2052]
	mov	w0, w27
	bl	nandc_set_seed
	ldrb	w1, [x21,377]
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
	cbnz	w24, .L1584
	mov	w0, w20
	bl	zftl_flash_exit_slc_mode
.L1584:
	mov	w0, w20
	and	w19, w21, 4
	bl	nandc_de_cs
	tbz	x21, 2, .L1585
	adrp	x0, .LC135
	mov	w1, w23
	add	x0, x0, :lo12:.LC135
	mov	w2, w19
	bl	printk
.L1585:
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
	cbnz	w22, .L1593
	adrp	x0, .LANCHOR5+392
	ldrb	w1, [x0,#:lo12:.LANCHOR5+392]
	mov	w0, w22
	cmp	w1, w20
	bhi	.L1594
.L1593:
	adrp	x19, .LANCHOR4
	mov	w1, 165
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 32
	ldr	x0, [x19,1128]
	bl	ftl_memset
	ldr	x0, [x19,1120]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	adrp	x0, .LANCHOR0+170
	ldrh	w21, [x0,#:lo12:.LANCHOR0+170]
	mov	w0, w22
	mul	w21, w20, w21
	mov	w1, w21
	bl	flash_erase_block
	cmn	w0, #1
	beq	.L1599
	adrp	x20, .LANCHOR2
	ldr	x2, [x19,1120]
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x3, [x19,1128]
	mov	w0, w22
	mov	w1, w21
	add	x20, x20, 368
	ldrb	w4, [x20,9]
	bl	flash_prog_page
	cmn	w0, #1
	beq	.L1599
	ldrb	w4, [x20,9]
	mov	w0, w22
	ldr	x2, [x19,1120]
	mov	w1, w21
	ldr	x3, [x19,1128]
	bl	flash_read_page
	cmn	w0, #1
	mov	w20, w0
	beq	.L1599
	ldr	x0, [x19,1120]
	ldr	w1, [x0]
	mov	w0, 23130
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	bne	.L1599
	ldr	x0, [x19,1128]
	ldr	w1, [x0]
	mov	w0, 42405
	bfi	w0, w0, 16, 16
	cmp	w1, w0
	csinv	w20, w20, wzr, eq
	b	.L1596
.L1599:
	mov	w20, -1
.L1596:
	mov	w0, w22
	mov	w1, w21
	bl	flash_erase_block
	mov	w0, w20
.L1594:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	flash_test_blk, .-flash_test_blk
	.section	.text.unlikely,"ax",%progbits
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
	ldr	x1, [x4,48]
	ldrb	w20, [x4,148]
	ldrb	w19, [x1,9]
	udiv	w19, w0, w19
	ldrb	w0, [x4,56]
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
	ldrb	w1, [x2,57]
	mov	x20, x3
	mov	w22, w4
	uxtb	w23, w5
	cmp	w1, w25
	ubfx	x0, x24, 24, 2
	bhi	.L1609
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 312
	mov	w2, 481
	bl	printk
.L1610:
	b	.L1610
.L1609:
	add	x1, x2, x25, sxtw
	mov	w19, w24
	ldrb	w26, [x1,160]
	cbnz	w0, .L1611
	ldrb	w0, [x2,58]
	cbz	w0, .L1612
	ldrb	w0, [x2,168]
	cbz	w0, .L1611
.L1612:
	add	x7, x7, :lo12:.LANCHOR0
	ldrh	w19, [x7,170]
	ldrb	w1, [x7,168]
	udiv	w6, w24, w19
	mul	w19, w6, w19
	sub	w0, w24, w19
	cbz	w1, .L1613
	add	w19, w19, w0, lsl 1
	b	.L1611
.L1613:
	add	x7, x7, 172
	ldrh	w0, [x7,w0,uxtw 1]
	add	w19, w0, w19
.L1611:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 4, .L1614
	adrp	x0, .LC136
	mov	w1, w26
	add	x0, x0, :lo12:.LC136
	mov	w2, w24
	bl	printk
.L1614:
	mov	w0, w26
	mov	w1, w19
	mov	x2, x21
	mov	x3, x20
	mov	w4, w22
	bl	flash_prog_page
	mov	w26, w0
	cbz	w23, .L1615
	adrp	x19, .LANCHOR4
	mov	w0, w25
	add	x23, x19, :lo12:.LANCHOR4
	mov	w1, w24
	mov	w4, w22
	ldr	x2, [x23,1120]
	ldr	x3, [x23,1128]
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L1616
	cmn	w0, #1
	beq	.L1616
	ldr	x0, [x23,1120]
	ldr	w1, [x21]
	ldr	w0, [x0]
	cmp	w1, w0
	bne	.L1616
	ldr	x0, [x23,1128]
	ldr	w1, [x20]
	ldr	w0, [x0]
	cmp	w1, w0
	beq	.L1615
.L1616:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w2, 4
	adrp	x0, .LC137
	mov	w3, w2
	add	x0, x0, :lo12:.LC137
	ldr	x1, [x19,1128]
	bl	rknand_print_hex
	mov	w2, 4
	adrp	x0, .LC138
	ldr	x1, [x19,1120]
	add	x0, x0, :lo12:.LC138
	mov	w3, w2
	bl	rknand_print_hex
	b	.L1617
.L1615:
	cmn	w26, #1
	bne	.L1618
.L1617:
	adrp	x0, .LC139
	mov	w1, w24
	add	x0, x0, :lo12:.LC139
	bl	printk
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 312
	mov	w2, 507
	bl	printk
.L1619:
	b	.L1619
.L1618:
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
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR4
	adrp	x24, .LANCHOR5
	add	x22, x22, :lo12:.LANCHOR4
	add	x24, x24, :lo12:.LANCHOR5
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	adrp	x25, .LC140
	str	wzr, [x22,1152]
	uxth	w26, w0
	str	wzr, [x24,128]!
	mov	w20, 0
	add	x25, x25, :lo12:.LC140
	add	x22, x22, 1152
.L1633:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3232]
	cmp	w0, w20
	bls	.L1649
	mov	w23, 0
.L1640:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,3216]
	cmp	w0, w23
	bls	.L1650
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1634
	mov	x0, x25
	mov	w1, w26
	bl	printk
.L1634:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w19, [x0,3216]
	madd	w19, w26, w19, w23
	uxth	w19, w19
	cbnz	w20, .L1635
	ldr	x0, [x0,152]
	ldrb	w0, [x0,47]
	cmp	w0, w19
	bcs	.L1636
.L1635:
	uxtb	w27, w20
	mov	w1, w19
	mov	w0, w27
	bl	flash_check_bad_block
	cbnz	w0, .L1636
	add	x4, x21, :lo12:.LANCHOR0
	mov	w0, w27
	str	x4, [x29,104]
	ldrh	w28, [x4,3230]
	mul	w28, w19, w28
	mov	w1, w28
	bl	flash_erase_block
	ldr	x4, [x29,104]
	cbnz	w0, .L1648
	ldrb	w4, [x4,2776]
	mov	w0, w27
	mov	w1, w28
	mov	x2, x22
	mov	x3, x24
	mov	w5, 1
	bl	flash_prog_page_en
	cbz	w0, .L1636
.L1648:
	mov	w0, w27
	mov	w1, w19
	bl	flash_mask_bad_block
.L1636:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L1640
.L1650:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1633
.L1649:
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
	bne	.L1652
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 336
	mov	w2, 1439
	bl	printk
.L1653:
	b	.L1653
.L1652:
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
	adrp	x23, .LC141
	ldrb	w2, [x0,2776]
	mov	w24, 0
	add	x0, x19, 448
	add	x23, x23, :lo12:.LC141
	lsl	w2, w2, 1
	mov	w27, 1
	add	x25, x19, 768
	bl	ftl_memset
.L1655:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w28, [x19,704]
	ldrh	w20, [x19,706]
	ldr	x2, [x0,2800]
	ldrh	w4, [x0,3230]
	ldr	w3, [x2,4]
	add	w3, w3, 1
	str	w3, [x2,4]
	str	w26, [x19,448]
	ldr	x3, [x0,2800]
	ldr	w0, [x3,4]
	str	w0, [x19,452]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1656
	ldr	w3, [x3,4]
	mov	x0, x23
	mov	w1, w28
	mov	w2, w20
	str	x4, [x29,104]
	bl	printk
	ldr	x4, [x29,104]
.L1656:
	add	x2, x22, :lo12:.LANCHOR0
	ldrh	w0, [x19,706]
	ldrh	w1, [x2,3234]
	cmp	w1, w0
	bhi	.L1657
	ldrb	w0, [x19,705]
	mov	w4, 0
	ldr	x5, [x2,152]
	ldrb	w1, [x19,704]
.L1664:
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, 7
	bls	.L1658
	mov	x2, 0
.L1663:
	add	x3, x5, x2
	uxth	w6, w2
	ldrb	w7, [x3,40]
	add	w3, w7, 127
	uxtb	w3, w3
	cmp	w3, 125
	bhi	.L1659
	add	x21, x21, :lo12:.LANCHOR5
	strb	w0, [x21,705]
	cbz	w4, .L1660
	strb	w1, [x21,704]
.L1660:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 352
	mov	w2, 157
	bl	printk
.L1661:
	b	.L1661
.L1659:
	cmp	w7, 255
	bne	.L1662
	add	x2, x2, 1
	cmp	x2, 8
	bne	.L1663
	mov	w6, w2
.L1662:
	uxtb	w0, w6
	mov	w24, 1
.L1658:
	add	w1, w0, 8
	add	x1, x5, x1, sxtw
	ldrb	w1, [x1,32]
	mov	w4, 1
	cmp	w1, 255
	beq	.L1664
	add	x28, x22, :lo12:.LANCHOR0
	strb	w0, [x19,705]
	strb	w1, [x19,704]
	mov	w0, 0
	ldrh	w20, [x28,3230]
	mul	w20, w1, w20
	mov	w1, w20
	bl	flash_erase_block
	ldrb	w4, [x28,2776]
	mov	w1, w20
	mov	w0, 0
	mov	x2, x25
	add	x3, x19, 448
	add	w20, w20, 1
	bl	ftl_prog_page
	strh	w27, [x19,706]
	b	.L1665
.L1657:
	madd	w20, w28, w4, w20
	cbnz	w0, .L1665
	mov	w1, w20
	bl	flash_erase_block
.L1665:
	add	x4, x22, :lo12:.LANCHOR0
	mov	w1, w20
	mov	w0, 0
	mov	x2, x25
	add	x3, x19, 448
	ldrb	w4, [x4,2776]
	bl	ftl_prog_page
	cmn	w0, #1
	ldrh	w1, [x19,706]
	add	w1, w1, 1
	strh	w1, [x19,706]
	beq	.L1655
	cbnz	w24, .L1666
.L1674:
	add	x21, x21, :lo12:.LANCHOR5
	ldrb	w0, [x21,704]
	cmp	w0, 255
	bne	.L1690
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	mov	w2, 192
	add	x1, x1, 352
	add	x0, x0, :lo12:.LC34
	bl	printk
	b	.L1675
.L1666:
	ldrb	w19, [x19,705]
	add	w19, w19, 1
.L1669:
	cmp	w19, 7
	bhi	.L1674
	add	x2, x22, :lo12:.LANCHOR0
	add	w0, w19, 8
	ldr	x1, [x2,152]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	add	w0, w1, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L1670
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 352
	mov	w2, 185
	bl	printk
.L1671:
	b	.L1671
.L1670:
	cmp	w1, 255
	beq	.L1672
	ldrh	w2, [x2,3230]
	mov	w0, 0
	mul	w1, w1, w2
	bl	flash_erase_block
.L1672:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1669
.L1675:
	b	.L1675
.L1690:
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
	ldrh	w1, [x19,2826]
	add	x22, x21, :lo12:.LANCHOR5
	add	x24, x22, 768
	add	x23, x23, :lo12:.LANCHOR6
	mov	x0, x24
	str	x24, [x19,2840]
	add	x1, x24, x1, lsl 2
	str	x1, [x19,2800]
	mov	w1, 0
	strb	wzr, [x23,-256]
	adrp	x26, .LC142
	adrp	x27, .LANCHOR2
	bl	ftl_memset
	add	x26, x26, :lo12:.LC142
	sub	x0, x23, #192
	mov	w1, 0
	mov	w2, 16384
	add	x23, x22, 448
	bl	ftl_memset
	strb	wzr, [x22,705]
	ldr	x0, [x19,152]
	mov	w19, 7
	strh	wzr, [x22,706]
	ldrb	w0, [x0,40]
	strb	w0, [x22,704]
.L1696:
	add	x5, x20, :lo12:.LANCHOR0
	add	w0, w19, 8
	sxth	w25, w19
	ldr	x1, [x5,152]
	add	x0, x1, x0, sxtw
	ldrb	w1, [x0,32]
	cmp	w1, 255
	beq	.L1692
	ldrh	w22, [x5,3230]
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
	bne	.L1693
	ldrb	w4, [x5,2776]
	mov	w0, 0
	add	w1, w22, 1
	mov	x2, x24
	mov	x3, x23
	bl	ftl_read_page
	mov	w28, w0
.L1693:
	add	x0, x27, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1694
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, w28
	mov	w3, 65
	ldr	x4, [x0,2800]
	mov	x0, x26
	ldr	w4, [x4]
	bl	printk
.L1694:
	cmn	w28, #1
	beq	.L1692
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	beq	.L1709
.L1692:
	sub	w19, w19, #1
	cmn	w19, #1
	bne	.L1696
	mov	w25, 0
	b	.L1695
.L1709:
	mov	w19, w25
.L1695:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1697
	add	x0, x20, :lo12:.LANCHOR0
	mov	w1, w19
	mov	w2, 4800
	ldr	x3, [x0,2800]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	ldr	w3, [x3]
	bl	printk
.L1697:
	cmn	w19, #1
	bne	.L1698
	add	x0, x21, :lo12:.LANCHOR5
	mov	w1, 0
	add	x0, x0, 768
	mov	w2, 16384
	add	x20, x20, :lo12:.LANCHOR0
	bl	ftl_memset
	mov	w0, 21574
	ldr	x1, [x20,2800]
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
	mov	w0, w19
	b	.L1699
.L1698:
	add	x26, x20, :lo12:.LANCHOR0
	add	x23, x21, :lo12:.LANCHOR5
	add	w0, w25, 8
	add	x24, x23, 768
	mov	x2, x24
	mov	w4, 4
	ldr	x1, [x26,152]
	add	x0, x1, x0, sxtw
	strb	w25, [x23,705]
	add	x25, x23, 448
	ldrb	w1, [x0,32]
	mov	x3, x25
	mov	w0, 0
	strb	w1, [x23,704]
	bl	flash_get_last_written_page
	sxth	w22, w0
	add	w0, w0, 1
	uxth	w19, w0
	ldrb	w0, [x23,704]
	ldrh	w23, [x26,3230]
	mul	w23, w0, w23
.L1700:
	tbnz	w22, #31, .L1705
	add	x26, x20, :lo12:.LANCHOR0
	mov	w0, 0
	add	w1, w22, w23
	mov	x2, x24
	mov	x3, x25
	ldrb	w4, [x26,2776]
	bl	ftl_read_page
	cmn	w0, #1
	beq	.L1701
	ldr	x0, [x26,2800]
	ldr	w1, [x0]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	cmp	w1, w0
	bne	.L1701
.L1705:
	add	x20, x20, :lo12:.LANCHOR0
	add	x21, x21, :lo12:.LANCHOR5
	ldr	x0, [x20,2800]
	strh	w19, [x21,706]
	ldr	w2, [x0,16]
	cmp	w2, 2048
	bls	.L1703
	lsr	w1, w2, 11
	ldr	w3, [x0,20]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,20]
	str	w1, [x0,16]
	b	.L1703
.L1701:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1700
.L1703:
	ldr	w2, [x0,24]
	cmp	w2, 2048
	bls	.L1706
	lsr	w1, w2, 11
	ldr	w3, [x0,28]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 11
	str	w3, [x0,28]
	str	w1, [x0,24]
.L1706:
	ldr	w2, [x0,32]
	cmp	w2, 1024
	bls	.L1707
	lsr	w1, w2, 10
	ldr	w3, [x0,36]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,36]
	str	w1, [x0,32]
.L1707:
	ldr	w2, [x0,40]
	cmp	w2, 1024
	bls	.L1708
	lsr	w1, w2, 10
	ldr	w3, [x0,44]
	add	w3, w3, w1
	sub	w1, w2, w1, lsl 10
	str	w3, [x0,44]
	str	w1, [x0,40]
.L1708:
	ldr	w1, [x0,64]
	add	w1, w1, 1
	str	w1, [x0,64]
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	mov	w0, 0
.L1699:
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
	ldr	x1, [x1,2792]
	ldr	w3, [x1,520]
	cmp	w0, w3
	bls	.L1727
	ldr	w2, [x1,12]
	sub	w2, w2, w3
	add	w2, w2, w0
	str	w2, [x1,12]
	b	.L1734
.L1727:
	bcs	.L1728
.L1734:
	str	w0, [x1,520]
.L1728:
	adrp	x20, .LANCHOR5
	adrp	x23, .LANCHOR6
	add	x20, x20, :lo12:.LANCHOR5
	add	x23, x23, :lo12:.LANCHOR6
	mov	w0, 0
	add	x20, x20, 448
	sub	x23, x23, #192
	bl	ftl_total_vpn_update
.L1733:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldr	w1, [x0,56]
	add	w1, w1, 1
	str	w1, [x0,56]
	ldrh	w1, [x0,140]
	ldrh	w0, [x2,3234]
	cmp	w1, w0
	bcc	.L1729
	bl	ftl_ext_alloc_new_blk
.L1729:
	add	x21, x22, :lo12:.LANCHOR0
	mov	w0, 65535
	ldr	x3, [x21,2800]
	ldrh	w1, [x3,130]
	cmp	w1, w0
	bne	.L1730
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 368
	mov	w2, 1058
	bl	printk
.L1731:
	b	.L1731
.L1730:
	ldrh	w19, [x21,3218]
	mov	w0, 21
	mov	w2, 1
	sub	w19, w0, w19
	ldrh	w0, [x3,140]
	asr	w24, w1, w19
	lsl	w19, w2, w19
	sub	w19, w19, #1
	ldrb	w2, [x21,2776]
	and	w1, w19, w1
	ldrh	w19, [x21,3230]
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
	beq	.L1733
	cmp	w1, 1
	cset	w0, eq
	cbnz	w0, .L1733
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
	strh	wzr, [x1,3768]
	adrp	x21, .LANCHOR5
	ldr	x0, [x1,2800]
	add	x2, x20, :lo12:.LANCHOR6
	ldrh	w22, [x1,3218]
	mov	w1, 21
	add	x3, x21, :lo12:.LANCHOR5
	sub	x2, x2, #192
	ldrh	w0, [x0,130]
	sub	w22, w1, w22
	add	x3, x3, 448
	asr	w22, w0, w22
	and	w24, w0, 16383
	mov	w1, w24
	uxtb	w22, w22
	mov	w0, w22
	bl	flash_get_last_written_page
	sxth	w26, w0
	adrp	x0, .LANCHOR2+400
	mov	w23, w26
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1736
	adrp	x1, .LANCHOR3
	adrp	x0, .LC99
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC99
	add	x1, x1, 392
	mov	w2, 1101
	mov	w3, w26
	bl	printk
.L1736:
	add	x20, x20, :lo12:.LANCHOR6
	add	x21, x21, :lo12:.LANCHOR5
	mov	w25, w26
	sub	x20, x20, #192
	add	x21, x21, 448
.L1737:
	tbnz	w25, #31, .L1742
	add	x27, x19, :lo12:.LANCHOR0
	mov	w0, w22
	mov	x2, x20
	mov	x3, x21
	ldrh	w1, [x27,3230]
	ldrb	w4, [x27,2776]
	madd	w1, w24, w1, w25
	bl	flash_read_page_en
	cmp	w0, 512
	beq	.L1738
	cmn	w0, #1
	beq	.L1738
	ldr	x0, [x27,2792]
	ldr	w1, [x0]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	cmp	w1, w0
	bne	.L1738
.L1742:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2800]
	ldrh	w1, [x0,140]
	cmp	w1, w26
	bgt	.L1740
	add	w23, w23, 1
	strh	w23, [x0,140]
	bl	ftl_ext_info_flush
	b	.L1740
.L1738:
	sub	w25, w25, #1
	sxth	w25, w25
	b	.L1737
.L1740:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x20, [x19,2792]
	bl	timer_get_time
	mov	w1, 100
	udiv	w0, w0, w1
	str	w0, [x20,520]
	ldr	x20, [x19,2792]
	bl	timer_get_time
	str	w0, [x20,604]
	ldr	x0, [x19,2792]
	str	wzr, [x0,608]
	mov	w0, -1
	strh	w0, [x19,3770]
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
	adrp	x21, .LANCHOR0
.L1756:
	add	x2, x21, :lo12:.LANCHOR0
	adrp	x20, .LANCHOR0
	ldr	x0, [x2,152]
	add	x0, x0, x19, sxtw
	ldrb	w1, [x0,32]
	add	w0, w1, 127
	uxtb	w0, w0
	cmp	w0, 125
	bhi	.L1753
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 416
	mov	w2, 1253
	bl	printk
.L1754:
	b	.L1754
.L1753:
	cmp	w1, 255
	beq	.L1755
	ldrh	w2, [x2,3230]
	mov	w0, 0
	mul	w1, w1, w2
	bl	flash_erase_block
.L1755:
	add	w19, w19, 1
	cmp	w19, 16
	bne	.L1756
	bl	sblk_init
	mov	w19, 0
	bl	ftl_info_blk_init
	adrp	x24, .LANCHOR7
	adrp	x0, .LANCHOR5
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR5
	mov	w2, 16384
	add	x0, x0, 768
	mov	w25, 1
	bl	ftl_memset
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	mov	w0, 21574
	movk	w0, 0x494c, lsl 16
	str	w0, [x1]
.L1757:
	add	x21, x20, :lo12:.LANCHOR0
	ldrh	w0, [x21,2826]
	cmp	w0, w19
	bls	.L1791
	add	x0, x24, :lo12:.LANCHOR7
	mov	w27, 0
	ubfiz	x22, x19, 2, 16
	mov	w23, w27
	strh	w19, [x0,3136]
	mov	w0, w19
	bl	ftl_test_block
	ldrb	w28, [x21,3216]
	ldrb	w3, [x21,3232]
	mov	w21, w27
	mul	w5, w19, w28
	uxth	w4, w28
.L1758:
	cmp	w21, w3
	bcs	.L1762
	mov	x26, 0
.L1763:
	cmp	w4, w26, uxth
	bls	.L1792
	add	w1, w5, w26
	mov	w0, w21
	str	x4, [x29,104]
	str	x3, [x29,112]
	str	x5, [x29,120]
	bl	flash_check_bad_block
	ldr	x5, [x29,120]
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	cbz	w0, .L1759
	add	x0, x20, :lo12:.LANCHOR0
	add	w1, w27, w26
	lsl	w1, w25, w1
	ldr	x0, [x0,2840]
	add	x0, x0, x22
	ldrb	w2, [x0,3]
	orr	w1, w1, w2
	strb	w1, [x0,3]
	b	.L1760
.L1759:
	add	w23, w23, 1
	uxth	w23, w23
.L1760:
	add	x26, x26, 1
	b	.L1763
.L1792:
	add	w21, w21, 1
	add	w27, w27, w28
	uxth	w21, w21
	b	.L1758
.L1762:
	cbnz	w23, .L1764
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2840]
	add	x22, x0, x22
	ldrb	w0, [x22,2]
	orr	w0, w0, -32
	strb	w0, [x22,2]
.L1764:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1757
.L1791:
	adrp	x0, .LANCHOR6
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR6
	mov	w2, 16384
	sub	x0, x0, #192
	bl	ftl_memset
	ldr	x1, [x21,2792]
	mov	w0, 20038
	movk	w0, 0x4549, lsl 16
	mov	x2, 0
	mov	w6, 3
	str	w0, [x1]
	ldr	x0, [x21,152]
	ldrb	w4, [x21,3216]
	ldrb	w19, [x0,47]
	udiv	w19, w19, w4
	add	w19, w19, 1
.L1767:
	add	x1, x20, :lo12:.LANCHOR0
	lsl	x0, x2, 2
	cmp	w4, 1
	add	x2, x2, 1
	csinc	w5, w6, wzr, ne
	ldr	x3, [x1,2840]
	cmp	w19, w2, uxth
	add	x3, x3, x0
	strb	w5, [x3,3]
	ldr	x3, [x1,2840]
	add	x3, x3, x0
	ldrb	w5, [x3,2]
	orr	w5, w5, -32
	strb	w5, [x3,2]
	ldr	x3, [x1,2840]
	add	x0, x3, x0
	ldrb	w3, [x0,2]
	and	w3, w3, -25
	strb	w3, [x0,2]
	bhi	.L1767
	mov	w0, 16
	sdiv	w4, w0, w4
	ldr	x0, [x1,2800]
	add	w4, w19, w4
	uxth	w4, w4
	strh	w4, [x0,134]
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L1768
	adrp	x0, .LC144
	mov	w1, 1290
	add	x0, x0, :lo12:.LC144
	mov	w2, 128
	mov	w3, w19
	bl	printk
.L1768:
	add	x21, x20, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 128
	ldr	x0, [x21,2800]
	add	x0, x0, 160
	bl	ftl_memset
	ldrb	w10, [x21,3216]
	mov	w12, 21
	ldrh	w0, [x21,3218]
	mov	w3, 0
	ldrb	w14, [x21,3232]
	mov	w1, w3
	uxth	w11, w10
	sub	w12, w12, w0
	mul	w9, w19, w11
	uxth	w9, w9
.L1769:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldrh	w0, [x0,134]
	cmp	w19, w0
	bcs	.L1793
	ldr	x4, [x2,2840]
	ubfiz	x0, x19, 2, 16
	add	w7, w3, w9
	mov	w6, 0
	add	x4, x4, x0
	uxth	w7, w7
	ldrb	w5, [x4,2]
	orr	w5, w5, -32
	strb	w5, [x4,2]
	ldr	x4, [x2,2840]
	add	x4, x4, x0
	ldrb	w5, [x4,2]
	and	w5, w5, -25
	strb	w5, [x4,2]
	mov	w5, w6
	ldr	x4, [x2,2840]
	ldr	x15, [x2,2800]
	add	x0, x4, x0
.L1770:
	cmp	w14, w5, uxth
	bls	.L1774
	lsl	w13, w5, w12
	mov	x4, 0
	uxth	w13, w13
.L1775:
	uxth	w2, w4
	cmp	w2, w11
	bcs	.L1794
	ldrb	w8, [x0,3]
	add	w16, w6, w4
	asr	w8, w8, w16
	tbnz	x8, 0, .L1771
	cmp	w10, 1
	mov	w8, w7
	bls	.L1772
	and	w2, w2, 1
	add	w2, w7, w2
	uxth	w8, w2
.L1772:
	add	x16, x15, x1, sxtw 1
	add	w1, w1, 1
	add	w2, w8, w13
	strh	w2, [x16,160]
	uxth	w1, w1
.L1771:
	add	x4, x4, 1
	b	.L1775
.L1794:
	add	w5, w5, 1
	add	w6, w6, w10
	b	.L1770
.L1774:
	add	w19, w19, 1
	add	w3, w3, w11
	uxth	w19, w19
	uxth	w3, w3
	b	.L1769
.L1793:
	ldrh	w3, [x2,2826]
	mov	w7, 1
.L1777:
	cmp	w0, w3
	bcs	.L1795
	add	x6, x20, :lo12:.LANCHOR0
	ubfiz	x2, x0, 2, 16
	add	w0, w0, 1
	ldr	x4, [x6,2840]
	uxth	w0, w0
	add	x4, x4, x2
	ldrb	w5, [x4,2]
	and	w5, w5, 31
	strb	w5, [x4,2]
	ldr	x4, [x6,2840]
	add	x2, x4, x2
	ldrb	w4, [x2,2]
	bfi	w4, w7, 3, 2
	strb	w4, [x2,2]
	b	.L1777
.L1795:
	add	x19, x20, :lo12:.LANCHOR0
	ldr	x0, [x19,2800]
	strh	w1, [x0,112]
	ldrh	w1, [x0,134]
	strh	wzr, [x0,114]
	sub	w3, w3, w1
	strh	wzr, [x0,118]
	strh	w3, [x0,116]
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
	bcs	.L1779
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 416
	mov	w2, 1322
	bl	printk
.L1780:
	b	.L1780
.L1779:
	strh	wzr, [x1,696]
	add	x0, x1, 416
	mov	w2, 256
	mov	w1, 255
	bl	ftl_memset
	ldr	x1, [x19,2800]
	mov	w2, 21
	ldrh	w0, [x19,3218]
	sub	w2, w2, w0
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3138
	asr	w2, w20, w2
	strh	w20, [x1,692]
	strh	w2, [x1,694]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3138]
	strh	w21, [x1,416]
	strh	w0, [x1,698]
	bl	ftl_alloc_sys_blk
	mov	w20, w0
	mov	w1, 0
	bl	ftl_erase_phy_blk
	ldr	x1, [x19,2800]
	ldr	x0, [x19,2792]
	strh	w20, [x1,130]
	add	x0, x0, 16
	mov	w1, 2
	bl	ftl_open_sblk_init
	ldr	x0, [x19,2792]
	mov	w1, 3
	add	x0, x0, 48
	bl	ftl_open_sblk_init
	ldr	x1, [x19,2800]
	ldr	x0, [x19,2792]
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
	ldr	x0, [x19,2792]
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
	adrp	x0, .LC145
	mov	w1, 1
	add	x29, sp, 0
	add	x0, x0, :lo12:.LC145
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	adrp	x19, .LANCHOR0
	bl	printk
	bl	sblk_init
	bl	ftl_info_blk_init
	bl	ftl_ext_info_init
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	ldrh	w6, [x0,2826]
	mov	w0, 0
	mov	w2, w0
	mov	w3, w0
	ldrh	w1, [x1,134]
.L1797:
	cmp	w1, w6
	bcs	.L1804
	add	x4, x19, :lo12:.LANCHOR0
	ldr	x5, [x4,2840]
	add	x5, x5, x1, uxth 2
	ldrb	w4, [x5,2]
	and	w7, w4, 31
	ands	w4, w4, 24
	strb	w7, [x5,2]
	bne	.L1798
	add	w3, w3, 1
	uxth	w3, w3
	b	.L1799
.L1798:
	cmp	w4, 16
	bne	.L1800
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1799
.L1800:
	add	w0, w0, 1
	uxth	w0, w0
.L1799:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1797
.L1804:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	strh	w2, [x1,118]
	strh	w3, [x1,114]
	strh	w0, [x1,116]
	mov	w0, 1
	strh	wzr, [x1,122]
	strh	wzr, [x1,120]
	strh	wzr, [x1,124]
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
	bcs	.L1802
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 432
	mov	w2, 1391
	bl	printk
.L1803:
	b	.L1803
.L1802:
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
	ldrh	w2, [x19,2826]
	and	w20, w20, 16383
	strh	w0, [x1,694]
	mov	w0, 1
	strh	w0, [x1,688]
	adrp	x0, .LANCHOR7+3138
	lsl	w2, w2, 1
	strh	w20, [x1,692]
	strh	w21, [x1,416]
	ldrh	w0, [x0,#:lo12:.LANCHOR7+3138]
	strh	w0, [x1,698]
	mov	w1, 0
	ldr	x0, [x19,2832]
	bl	ftl_memset
	ldr	x0, [x19,2792]
	mov	w1, 2
	add	x0, x0, 16
	bl	ftl_open_sblk_init
	ldr	x0, [x19,2792]
	mov	w1, 3
	add	x0, x0, 48
	bl	ftl_open_sblk_init
	ldr	x1, [x19,2800]
	ldr	x0, [x19,2792]
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
	ldr	x0, [x19,2792]
	mov	w1, 255
	mov	w2, 128
	add	x0, x0, 392
	bl	ftl_memset
	ldr	x0, [x19,2792]
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
	ldr	x22, [x19,3752]
	bne	.L1807
	mov	x20, x0
	bl	ftl_get_new_free_page
	cmn	w0, #1
	mov	w23, w0
	mov	w21, 0
	beq	.L1807
	ldrh	w0, [x20]
	add	x22, x22, x24, uxth 2
	bl	ftl_vpn_decrement
	adrp	x1, .LANCHOR5
	mov	w0, 15555
	add	x1, x1, :lo12:.LANCHOR5
	movk	w0, 0xf55f, lsl 16
	add	x2, x1, 128
	str	x2, [x29,72]
	str	w0, [x1,128]
	ldrb	w0, [x19,3212]
	ldrh	w1, [x19,3234]
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
.L1807:
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
	tbz	x0, 12, .L1812
	adrp	x0, .LC146
	ldrh	w1, [x28,2]
	add	x0, x0, :lo12:.LC146
	bl	printk
.L1812:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1813
	adrp	x0, .LC147
	ldrb	w1, [x28,5]
	add	x0, x0, :lo12:.LC147
	bl	printk
.L1813:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1814
	adrp	x0, .LC148
	ldrh	w1, [x28]
	add	x0, x0, :lo12:.LC148
	bl	printk
.L1814:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1815
	adrp	x0, .LC149
	ldrh	w1, [x28,16]
	ldrh	w2, [x28,18]
	add	x0, x0, :lo12:.LC149
	bl	printk
.L1815:
	ldrh	w0, [x28,10]
	adrp	x23, .LANCHOR5
	strh	w0, [x28,14]
	mov	w0, 1
	add	x23, x23, :lo12:.LANCHOR5
	adrp	x24, .LC150
	bl	buf_alloc
	ldrb	w19, [x28,5]
	ldrh	w20, [x28,2]
	mov	x27, x0
	mov	w26, 0
	add	x23, x23, 448
	add	x24, x24, :lo12:.LC150
.L1816:
	adrp	x0, .LANCHOR0
	adrp	x22, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w0, [x0,3234]
	cmp	w0, w20
	bls	.L1819
	ldrb	w19, [x28,5]
.L1817:
	ldrb	w0, [x28,9]
	cmp	w0, w19
	bls	.L1898
	add	x0, x28, x19, sxtw 1
	ldrh	w5, [x0,16]
	mov	w0, 65535
	cmp	w5, w0
	beq	.L1818
	add	x3, x22, :lo12:.LANCHOR0
	ldr	x1, [x27,8]
	ldr	x2, [x27,24]
	str	x5, [x29,96]
	ldrh	w4, [x3,3230]
	ldrb	w3, [x3,2776]
	madd	w4, w5, w4, w20
	str	x4, [x29,104]
	mov	w0, w4
	bl	ftl_read_ppa_page
	cmp	w0, 512
	mov	w10, w0
	beq	.L1819
	cmn	w0, #1
	ldr	x4, [x29,104]
	ldr	x5, [x29,96]
	beq	.L1820
	ldr	x0, [x27,24]
	ldr	w1, [x0]
	cmn	w1, #1
	bne	.L1820
	ldr	w0, [x0,4]
	cmn	w0, #1
	bne	.L1820
	ldr	x0, [x27,8]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1819
.L1820:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1821
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
.L1821:
	adrp	x0, .LANCHOR6-256
	ldrb	w2, [x28,9]
	mov	w1, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-256]
	ldrh	w0, [x28,10]
	madd	w1, w20, w2, w19
	cmp	w0, w1
	beq	.L1822
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 456
	mov	w2, 712
	bl	printk
.L1823:
	b	.L1823
.L1822:
	add	x26, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28,6]
	add	w0, w1, w0
	ldrh	w1, [x26,3234]
	mul	w1, w1, w2
	cmp	w0, w1
	beq	.L1824
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 456
	mov	w2, 713
	bl	printk
.L1825:
	b	.L1825
.L1824:
	cmn	w10, #1
	beq	.L1829
	ldr	x4, [x27,24]
	str	x4, [x29,104]
	ldr	w0, [x4,4]
	cmn	w0, #1
	beq	.L1829
	bl	lpa_hash_get_ppa
	cbz	x25, .L1827
	ldr	x4, [x29,104]
	ldr	w1, [x4,8]
	cmp	w0, w1
	beq	.L1827
	cmn	w0, #1
	beq	.L1827
	ldrh	w5, [x26,3218]
	mov	w1, 21
	mov	w2, 1
	sub	w1, w1, w5
	lsr	w3, w0, w5
	lsl	w1, w2, w1
	ldrb	w2, [x26,3216]
	sub	w1, w1, #1
	and	w1, w3, w1
	udiv	w1, w1, w2
	ldrh	w2, [x25]
	cmp	w2, w1, uxth
	bne	.L1827
	ldr	w4, [x4]
	mov	x2, x23
	ldr	x1, [x27,8]
	ldrb	w3, [x26,2776]
	str	x4, [x29,104]
	bl	ftl_read_ppa_page
	ldr	w0, [x23]
	ldr	x4, [x29,104]
	cmp	w0, w4
	bcc	.L1827
	ldr	x0, [x27,24]
	ldr	w1, [x0,8]
	cmn	w1, #1
	beq	.L1829
	add	x4, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w3, 1
	ldrh	w0, [x4,3218]
	sub	w2, w2, w0
	lsr	w1, w1, w0
	lsl	w2, w3, w2
	ldrb	w0, [x4,3216]
	sub	w2, w2, #1
	and	w1, w2, w1
	udiv	w0, w1, w0
	bl	ftl_vpn_decrement
	b	.L1829
.L1827:
	ldr	x2, [x27,24]
	add	x1, x22, :lo12:.LANCHOR0
	ldr	w3, [x2,4]
	ldr	w0, [x1,2784]
	cmp	w3, w0
	bcs	.L1829
	ldrb	w0, [x28,9]
	ldrh	w4, [x1,3234]
	ldrh	w3, [x28,10]
	mul	w0, w0, w4
	sub	w0, w0, #1
	cmp	w3, w0
	blt	.L1899
.L1829:
	ldrh	w0, [x28,6]
	mov	w26, 1
	sub	w0, w0, #1
	strh	w0, [x28,6]
	ldrh	w0, [x28,10]
	add	w0, w0, 1
	strh	w0, [x28,10]
.L1818:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1817
.L1898:
	add	w20, w20, 1
	strb	wzr, [x28,5]
	uxth	w20, w20
	b	.L1816
.L1819:
	add	x4, x22, :lo12:.LANCHOR0
	ldrb	w1, [x28,9]
	ldrh	w2, [x28,10]
	ldrh	w3, [x28,6]
	ldrh	w0, [x4,3234]
	add	w3, w3, w2
	strh	w20, [x28,2]
	strb	w19, [x28,5]
	mul	w0, w1, w0
	cmp	w3, w0
	bne	.L1833
	mov	w19, 0
	ldr	x4, [x4,3752]
	mov	w1, w19
	b	.L1834
.L1833:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 456
	mov	w2, 789
	bl	printk
.L1835:
	b	.L1835
.L1837:
	ldrh	w3, [x28,12]
	add	w3, w1, w3
	ldr	w3, [x4,x3,lsl 2]
	cmn	w3, #1
	beq	.L1836
	add	w19, w19, 1
	uxth	w19, w19
.L1836:
	add	w1, w1, 1
.L1834:
	cmp	w1, w2
	bcc	.L1837
	sub	w0, w0, w2
	add	w19, w19, w0
	add	x0, x21, :lo12:.LANCHOR2
	uxth	w19, w19
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1838
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28]
	mov	w2, w19
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,2832]
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	ldrh	w3, [x4,x3]
	bl	printk
.L1838:
	add	x0, x22, :lo12:.LANCHOR0
	ldrh	w1, [x28]
	adrp	x20, .LC152
	mov	w23, -1
	add	x20, x20, :lo12:.LC152
	ldr	x0, [x0,2832]
	strh	w19, [x0,x1,lsl 1]
	mov	x0, x27
	mov	w19, 0
	bl	buf_free
.L1839:
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w0, [x2,3212]
	cmp	w19, w0, lsl 1
	bcs	.L1900
	cbz	w26, .L1840
	ldrh	w0, [x28,6]
	cmp	w0, 1
	bls	.L1840
	mov	x0, x28
	bl	ftl_get_new_free_page
	mov	w24, w0
	add	x0, x21, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L1841
	mov	x0, x20
	mov	w1, w24
	bl	printk
.L1841:
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
.L1840:
	add	w19, w19, 1
	b	.L1839
.L1900:
	ldrh	w1, [x2,3234]
	ldrh	w0, [x28,12]
	ldrb	w3, [x28,9]
	madd	w0, w1, w3, w0
	mov	x1, -4
	add	x0, x1, x0, sxtw 2
	ldr	x1, [x2,3752]
	ldr	w0, [x1,x0]
	cmn	w0, #1
	beq	.L1843
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 456
	mov	w2, 816
	bl	printk
.L1844:
	b	.L1844
.L1843:
	ldrh	w0, [x28,6]
	cmp	w0, 1
	bne	.L1811
	mov	x0, x28
	bl	ftl_write_last_log_page
	b	.L1811
.L1899:
	ldr	x0, [x1,2800]
	ldr	w2, [x2]
	ldr	w1, [x0,8]
	cmp	w2, w1
	bls	.L1830
	str	w2, [x0,8]
.L1830:
	ldr	x1, [x27,24]
	ldrh	w0, [x28,10]
	ldrh	w2, [x28,12]
	add	w2, w2, w0
	ldr	w0, [x1,4]
	ldr	w1, [x1,8]
	bl	lpa_hash_update_ppa
	b	.L1829
.L1811:
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
	adrp	x21, .LC153
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w20, w0
	mov	x24, x1
	mov	x25, x22
	mov	w23, 65535
	add	x21, x21, :lo12:.LC153
.L1902:
	adrp	x26, .LANCHOR7
.L1907:
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldr	w1, [x0,48]
	ldrh	w3, [x0,696]
	add	w1, w1, 1
	str	w1, [x0,48]
	ldrh	w1, [x2,3234]
	cmp	w3, w1
	bcs	.L1903
	ldrh	w0, [x0,692]
	cmp	w0, w23
	bne	.L1904
.L1903:
	bl	pm_alloc_new_blk
	mov	w0, 0
	bl	ftl_info_flush
.L1904:
	add	x27, x25, :lo12:.LANCHOR0
	ldr	x0, [x27,2800]
	ldrh	w3, [x0,692]
	cmp	w3, w23
	bne	.L1905
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 480
	mov	w2, 224
	bl	printk
.L1906:
	b	.L1906
.L1905:
	add	x28, x26, :lo12:.LANCHOR7
	ldrh	w0, [x0,696]
	ldrh	w19, [x27,3230]
	mov	w1, 0
	mov	w2, 64
	madd	w19, w3, w19, w0
	ldr	x0, [x28,3144]
	bl	ftl_memset
	ldr	x0, [x28,3144]
	mov	x2, x24
	str	w20, [x0]
	ldr	x1, [x27,2800]
	ldr	x0, [x28,3144]
	ldrb	w4, [x27,2776]
	ldr	w1, [x1,48]
	str	w1, [x0,4]
	mov	w1, w19
	ldr	x0, [x27,2800]
	ldr	x3, [x28,3144]
	ldrb	w0, [x0,694]
	bl	ftl_prog_page
	ldr	x1, [x27,2800]
	ldrh	w3, [x1,696]
	add	w3, w3, 1
	uxth	w3, w3
	strh	w3, [x1,696]
	cmp	w3, 1
	beq	.L1907
	cmn	w0, #1
	bne	.L1908
	mov	x0, x21
	mov	w1, w19
	bl	printk
	b	.L1902
.L1908:
	ldrh	w0, [x1,698]
	cmp	w20, w0
	bcs	.L1909
	add	x20, x1, x20, uxtw 2
	str	w19, [x20,704]
.L1909:
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
	adrp	x2, .LANCHOR3
	adrp	x0, .LC101
	add	x2, x2, :lo12:.LANCHOR3
	add	x29, sp, 0
	mov	w1, 251
	add	x2, x2, 496
	add	x0, x0, :lo12:.LC101
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	adrp	x22, .LANCHOR4
	bl	printk
	add	x0, x22, :lo12:.LANCHOR4
	adrp	x23, .LANCHOR0
	mov	w1, 0
	mov	w2, 64
	add	x19, x23, :lo12:.LANCHOR0
	adrp	x24, .LC155
	ldr	x0, [x0,1104]
	adrp	x25, .LC156
	mov	w26, 0
	add	x24, x24, :lo12:.LC155
	add	x25, x25, :lo12:.LC156
	bl	ftl_memset
	ldr	x1, [x19,152]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x19, [x19,152]
	add	x0, x19, 16
	ldr	w1, [x19,8]
	bl	js_hash
	str	w0, [x19,12]
.L1913:
	add	x21, x22, :lo12:.LANCHOR4
	add	x27, x23, :lo12:.LANCHOR0
	mov	x0, x24
	ldrb	w20, [x21,1112]
	ldrh	w19, [x21,1114]
	mov	w1, w20
	ldrh	w28, [x27,170]
	mov	w2, w19
	bl	printk
	ldrh	w1, [x21,972]
	ldrh	w0, [x21,1114]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1914
	ldr	x1, [x27,152]
	ldrb	w19, [x21,1113]
	strh	wzr, [x21,1114]
	ldr	w0, [x1,4]
	add	w0, w0, 1
	str	w0, [x1,4]
	ldrb	w0, [x21,1112]
	ldr	x20, [x27,152]
	strb	w0, [x21,1113]
	mov	x0, x20
	strb	w19, [x21,1112]
	ldrh	w1, [x20,16]
	add	w1, w1, 1
	strh	w1, [x0,16]!
	ldr	w1, [x20,8]
	bl	js_hash
	str	w0, [x20,12]
	ldrh	w0, [x27,170]
	mul	w19, w19, w0
	mov	w0, 0
	b	.L1919
.L1914:
	madd	w19, w20, w28, w19
	cbnz	w0, .L1915
.L1919:
	mov	w1, w19
	bl	flash_erase_block
.L1915:
	add	x2, x23, :lo12:.LANCHOR0
	add	x20, x22, :lo12:.LANCHOR4
	mov	w4, 4
	mov	w5, 1
	ldr	x1, [x2,152]
	ldr	x0, [x20,1104]
	ldr	w1, [x1,4]
	str	w1, [x0]
	mov	w0, 21321
	movk	w0, 0x5359, lsl 16
	ldr	x1, [x20,1104]
	str	w0, [x1,4]
	mov	w1, w19
	mov	w0, 0
	ldr	x2, [x2,152]
	ldr	x3, [x20,1104]
	bl	flash_prog_page_en
	cmn	w0, #1
	ldrh	w1, [x20,1114]
	add	w1, w1, 1
	strh	w1, [x20,1114]
	bne	.L1916
	mov	x0, x25
	mov	w1, w19
	bl	printk
	b	.L1913
.L1916:
	cbnz	w26, .L1920
	mov	w26, 1
	b	.L1913
.L1920:
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
	mov	w1, 2074
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR3
	mov	w22, 0
	stp	x23, x24, [sp,48]
	add	x2, x21, :lo12:.LANCHOR3
	adrp	x24, .LC101
	stp	x19, x20, [sp,16]
	add	x2, x2, 520
	mov	x20, x0
	adrp	x19, .LANCHOR0
	add	x0, x24, :lo12:.LC101
	add	x23, x19, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	bl	printk
	add	x25, x23, 688
	mov	w0, 2
	strb	w0, [x23,684]
	mov	x0, x20
	adrp	x20, .LANCHOR2
	mov	w26, 44
	bl	nandc_init
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 368
	str	x0, [x23,48]
.L1927:
	mov	w0, w22
	mov	x1, x25
	bl	flash_read_id
	cbnz	w22, .L1922
	ldrb	w0, [x23,688]
	add	x1, x23, 688
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bls	.L1923
.L1925:
	mov	w20, -2
	b	.L1973
.L1923:
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L1925
.L1922:
	ldrb	w0, [x25]
	cmp	w0, 181
	bne	.L1926
	strb	w26, [x25]
.L1926:
	add	w22, w22, 1
	add	x25, x25, 8
	uxtb	w22, w22
	cmp	w22, 4
	bne	.L1927
	add	x23, x20, :lo12:.LANCHOR2
	add	x25, x19, :lo12:.LANCHOR0
	add	x23, x23, 440
	mov	w22, 0
	add	x25, x25, 688
.L1930:
	ldrb	w2, [x23]
	add	x0, x23, 1
	mov	x1, x25
	bl	flash_mem_cmp8
	cbnz	w0, .L1928
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x22, x22, 5, 32
	add	x0, x1, 440
	adds	x22, x0, x22
	beq	.L1925
	ldrb	w3, [x22,22]
	mov	x0, 0
	b	.L1932
.L1928:
	add	w22, w22, 1
	add	x23, x23, 32
	cmp	w22, 24
	bne	.L1930
	b	.L1925
.L1932:
	add	x4, x1, x0, lsl 5
	mov	w2, w0
	ldrb	w4, [x4,1208]
	cmp	w4, w3
	beq	.L1931
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L1932
	mov	w2, w0
.L1931:
	add	x23, x20, :lo12:.LANCHOR2
	ubfiz	x2, x2, 5, 32
	add	x1, x23, 1208
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x1, x2
	add	x0, x0, 64
	mov	w2, 32
	add	x23, x23, 368
	bl	ftl_memcpy
	mov	x0, x23
	mov	x1, x22
	mov	w2, 32
	bl	ftl_memcpy
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	cmp	w0, 8
	bhi	.L1933
	ldrb	w1, [x23,20]
	cmp	w1, 60
	bls	.L1934
	mov	w1, 60
	strb	w1, [x23,20]
.L1934:
	cmp	w0, 8
	bne	.L1933
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,688]
	cmp	w0, 44
	beq	.L1979
.L1933:
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 2095
	add	x2, x2, 520
	add	x0, x24, :lo12:.LC101
	adrp	x22, .LANCHOR4
	add	x23, x19, :lo12:.LANCHOR0
	bl	printk
	add	x25, x22, :lo12:.LANCHOR4
	mov	w0, 16384
	add	x26, x20, :lo12:.LANCHOR2
	bl	ftl_malloc
	str	x0, [x25,1120]
	mov	w0, 16384
	bl	ftl_malloc
	str	x0, [x25,1072]
	mov	w0, 2048
	bl	ftl_malloc
	str	x0, [x23,152]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x25,1128]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x25,1080]
	mov	w0, 64
	bl	ftl_malloc
	str	x0, [x25,1104]
	adrp	x0, .LANCHOR5+392
	strb	wzr, [x0,#:lo12:.LANCHOR5+392]
	bl	flash_die_info_init
	ldrb	w0, [x26,386]
	bl	flash_lsb_page_tbl_build
	ldrb	w0, [x26,388]
	bl	nandc_bch_sel
	str	xzr, [x25,1096]
	ldr	x1, [x23,48]
	ldrh	w0, [x1,16]
	ldrb	w1, [x1,28]
	ubfx	x2, x0, 8, 3
	strb	w2, [x23,720]
	adrp	x2, .LANCHOR7+3152
	ubfx	x3, x0, 3, 1
	strb	w1, [x23,58]
	strb	w3, [x2,#:lo12:.LANCHOR7+3152]
	ubfx	x2, x0, 4, 1
	strb	w2, [x23,721]
	ubfx	x2, x0, 12, 1
	strb	w2, [x23,3208]
	ubfx	x2, x0, 13, 1
	ubfx	x0, x0, 14, 1
	strb	w0, [x23,168]
	mov	w0, 60
	strb	w0, [x23,56]
	ldrb	w0, [x19,#:lo12:.LANCHOR0]
	strb	w2, [x23,3228]
	cmp	w0, 9
	bne	.L1935
	mov	w0, 70
	strb	w0, [x23,56]
.L1935:
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x22, :lo12:.LANCHOR4
	add	x2, x19, :lo12:.LANCHOR0
	strb	w1, [x0,1136]
	ldrb	w1, [x20,387]
	strb	w1, [x2,149]
	ldrh	w2, [x20,384]
	tbz	x2, 6, .L1937
	sub	w2, w1, #17
	uxtb	w2, w2
	cmp	w2, 2
	bhi	.L1938
	adrp	x2, micron_read_retrial
	cmp	w1, 19
	add	x2, x2, :lo12:micron_read_retrial
	str	x2, [x0,1096]
	mov	w2, 7
	strb	w2, [x0,1088]
	bne	.L1937
	mov	w1, 15
	b	.L1975
.L1938:
	sub	w2, w1, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L1955
	cmp	w1, 33
	bne	.L1940
.L1955:
	add	x0, x22, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	mov	w2, 4
	str	x1, [x0,1096]
	add	x1, x19, :lo12:.LANCHOR0
	strb	w2, [x1,150]
	b	.L1976
.L1940:
	sub	w2, w1, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w1, #34
	cset	w3, ls
	cmp	w1, 35
	cset	w4, eq
	uxtb	w2, w2
	cmp	w1, 68
	cset	w5, eq
	cmp	w2, 1
	bls	.L1956
	cbz	w3, .L1942
.L1956:
	add	x0, x22, :lo12:.LANCHOR4
	adrp	x1, toshiba_read_retrial
	add	x1, x1, :lo12:toshiba_read_retrial
	str	x1, [x0,1096]
	mov	w1, 7
	strb	w1, [x0,1088]
	cbnz	w5, .L1957
	cbz	w4, .L1944
.L1957:
	add	x0, x22, :lo12:.LANCHOR4
	mov	w1, 17
	strb	w1, [x0,1088]
.L1944:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 4
	cbnz	w3, .L1977
	mov	w1, 5
.L1977:
	strb	w1, [x0,150]
	b	.L1937
.L1942:
	sub	w1, w1, #36
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L1937
	adrp	x1, toshiba_3d_read_retrial
	add	x1, x1, :lo12:toshiba_3d_read_retrial
	str	x1, [x0,1096]
.L1976:
	mov	w1, 7
.L1975:
	strb	w1, [x0,1088]
.L1937:
	add	x2, x21, :lo12:.LANCHOR3
	add	x0, x24, :lo12:.LC101
	mov	w1, 2143
	add	x2, x2, 520
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	strb	wzr, [x0,96]
	ldrb	w0, [x0,720]
	tbz	x0, 0, .L1947
	mov	w0, 4
	bl	nandc_set_if_mode
	mov	w0, 1
	bl	flash_set_interface_mode
	mov	w0, 1
	b	.L1978
.L1947:
	mov	w0, 4
.L1978:
	bl	nandc_set_if_mode
	bl	flash_info_blk_init
	cmn	w0, #1
	mov	w20, w0
	bne	.L1949
	add	x2, x21, :lo12:.LANCHOR3
	add	x0, x24, :lo12:.LC101
	mov	w1, 2161
	add	x2, x2, 520
	bl	printk
	b	.L1973
.L1949:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,149]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L1950
	add	x0, x22, :lo12:.LANCHOR4
	adrp	x1, hynix_read_retrial
	add	x1, x1, :lo12:hynix_read_retrial
	str	x1, [x0,1096]
.L1950:
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,720]
	tbz	x0, 2, .L1952
	ldr	x0, [x19,152]
	ldrb	w0, [x0,19]
	cbz	w0, .L1952
	add	x22, x22, :lo12:.LANCHOR4
	ldrh	w1, [x19,170]
	mov	w0, 0
	ldrb	w2, [x22,1112]
	mul	w1, w2, w1
	bl	flash_ddr_para_scan
	ldrb	w0, [x19,96]
	cbnz	w0, .L1952
	ldr	x1, [x19,152]
	strb	w0, [x1,19]
	bl	flash_info_flush
.L1952:
	add	x2, x21, :lo12:.LANCHOR3
	mov	w1, 2196
	add	x2, x2, 520
	add	x0, x24, :lo12:.LC101
	mov	w20, 0
	bl	printk
	bl	nand_flash_print_info
	b	.L1973
.L1979:
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 368
	ldrb	w1, [x0,28]
	cmp	w1, 2
	bne	.L1933
	strb	wzr, [x0,28]
	b	.L1933
.L1973:
	mov	w0, w20
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
	stp	x21, x22, [sp,80]
	stp	x27, x28, [sp,128]
	stp	x19, x20, [sp,64]
	stp	x25, x26, [sp,112]
	bl	buf_alloc
	ldr	x23, [x0,8]
	mov	w27, 0
	adrp	x21, .LC150
	adrp	x22, .LC157
	mov	x26, x0
	mov	w19, w27
	adrp	x28, .LANCHOR0
	add	x21, x21, :lo12:.LC150
	add	x22, x22, :lo12:.LC157
	add	x23, x23, 704
.L1981:
	add	x3, x28, :lo12:.LANCHOR0
	ldrh	w1, [x3,3234]
	cmp	w1, w19
	bls	.L1991
	ldrh	w20, [x3,3230]
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
	beq	.L1987
	cmn	w25, #1
	bne	.L1982
.L1987:
	mov	w27, 1
.L1982:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1981
.L1991:
	mov	x0, x26
	bl	buf_free
	cbz	w27, .L1985
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 536
	mov	w2, 656
	bl	printk
.L1986:
	b	.L1986
.L1985:
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
	adrp	x0, .LC158
	add	x29, sp, 0
	add	x3, x3, :lo12:.LANCHOR5
	add	x0, x0, :lo12:.LC158
	str	x19, [sp,16]
	adrp	x19, .LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w1, [x3,704]
	ldrb	w2, [x3,705]
	ldrh	w3, [x3,706]
	bl	printk
	ldr	x2, [x19,2800]
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	ldrh	w1, [x2,130]
	ldrh	w2, [x2,140]
	bl	printk
	ldr	x1, [x19,2792]
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	add	x5, x1, 16
	ldrh	w1, [x1,16]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,2792]
	adrp	x0, .LC161
	add	x0, x0, :lo12:.LC161
	add	x5, x1, 48
	ldrh	w1, [x1,48]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w4, [x5,6]
	ldrh	w5, [x5,10]
	bl	printk
	ldr	x1, [x19,2792]
	adrp	x0, .LC162
	add	x0, x0, :lo12:.LC162
	add	x5, x1, 80
	ldrh	w1, [x1,80]
	ldrh	w4, [x5,6]
	ldrh	w2, [x5,2]
	ldrb	w3, [x5,5]
	ldrh	w5, [x5,10]
	bl	printk
	ldrh	w0, [x19,3234]
	mov	w2, 4
	ldrb	w3, [x19,3212]
	ldr	x1, [x19,3752]
	mul	w3, w0, w3
	adrp	x0, .LC163
	add	x0, x0, :lo12:.LC163
	lsl	w3, w3, 1
	bl	rknand_print_hex
	ldr	x1, [x19,2832]
	adrp	x0, .LC164
	ldrh	w3, [x19,2826]
	add	x0, x0, :lo12:.LC164
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x1, [x19,2800]
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	mov	w2, 4
	add	x1, x1, 704
	mov	w3, 32
	bl	rknand_print_hex
	ldr	x1, [x19,2840]
	adrp	x0, .LC165
	ldrh	w3, [x19,2826]
	add	x0, x0, :lo12:.LC165
	mov	w2, 4
	bl	rknand_print_hex
	adrp	x0, .LC166
	add	x1, x19, 3240
	add	x0, x0, :lo12:.LC166
	mov	w2, 2
	mov	w3, 256
	bl	rknand_print_hex
	ldrh	w0, [x19,3234]
	mov	w2, 2
	ldrb	w3, [x19,3212]
	ldr	x1, [x19,3760]
	mul	w3, w0, w3
	adrp	x0, .LC167
	add	x0, x0, :lo12:.LC167
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
	ldrh	w4, [x5,3218]
	ldrb	w3, [x5,3216]
	sub	w6, w6, w4
	lsr	w4, w2, w4
	lsl	w6, w7, w6
	sub	w6, w6, #1
	and	w4, w6, w4
	udiv	w4, w4, w3
	ldr	x3, [x5,2840]
	add	x4, x3, x4, uxth 2
	ldrb	w3, [x4,2]
	ubfx	x3, x3, 5, 3
	cmp	w3, 7
	cset	w4, eq
	cbnz	w4, .L1997
	cmp	w3, w7
	bne	.L1994
.L1997:
	mov	w3, w2
	mov	w2, w1
	mov	x1, x0
	adrp	x0, .LC168
	add	x0, x0, :lo12:.LC168
	bl	printk
	bl	dump_ftl_info
	mov	w4, -1
.L1994:
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
	bls	.L2000
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 552
	mov	w2, 30
	bl	printk
.L2001:
	b	.L2001
.L2000:
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR7
	add	x20, x20, :lo12:.LANCHOR0
	ldr	x0, [x20,2800]
	ldrh	w2, [x0,698]
	cmp	w21, w2
	bls	.L2002
	adrp	x0, .LC169
	mov	w1, w21
	add	x0, x0, :lo12:.LC169
	add	x19, x19, :lo12:.LANCHOR7
	bl	printk
	mov	x0, 0
	ldrh	w2, [x19,3154]
	mov	w1, 255
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,2800]
	ldrh	w0, [x0,698]
	cmp	w0, w21
	bcs	.L2006
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 552
	mov	w2, 34
	bl	printk
.L2004:
	b	.L2004
.L2002:
	add	x0, x0, x21, sxtw 2
	sbfiz	x1, x1, 4, 32
	ldr	w22, [x0,704]
	add	x0, x20, 3784
	add	x23, x0, x1
	strh	w21, [x0,x1]
	strh	wzr, [x23,2]
	cbnz	w22, .L2005
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x23,8]
	mov	w1, 255
	ldrh	w2, [x19,3154]
	bl	ftl_memset
	b	.L2006
.L2005:
	add	x24, x19, :lo12:.LANCHOR7
	ldrb	w3, [x20,2776]
	ldr	x1, [x23,8]
	mov	w0, w22
	ldr	x2, [x24,3144]
	bl	ftl_read_ppa_page
	mov	w3, w0
	ldr	x0, [x24,3144]
	ldr	w2, [x0]
	cmp	w2, w21
	beq	.L2007
	adrp	x0, .LC170
	mov	w4, w22
	mov	w1, w21
	add	x0, x0, :lo12:.LC170
	bl	printk
	ldr	x3, [x20,2800]
	adrp	x0, .LC171
	add	x0, x0, :lo12:.LC171
	mov	w2, 4
	add	x1, x3, 704
	ldrh	w3, [x3,698]
	bl	rknand_print_hex
	ldr	x1, [x23,8]
	adrp	x0, .LC172
	add	x0, x0, :lo12:.LC172
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldr	x1, [x24,3144]
	adrp	x0, .LC173
	add	x0, x0, :lo12:.LC173
	mov	w2, 4
	mov	w3, 16
	bl	rknand_print_hex
	ldrh	w0, [x20,3218]
	mov	w1, 21
	mov	w2, 1
	sub	w1, w1, w0
	lsr	w22, w22, w0
	lsl	w1, w2, w1
	ldrb	w0, [x20,3216]
	sub	w1, w1, #1
	and	w22, w1, w22
	mov	x1, 0
	udiv	w0, w22, w0
	bl	ftl_sblk_dump
.L2007:
	add	x19, x19, :lo12:.LANCHOR7
	ldr	x0, [x19,3144]
	ldr	w0, [x0]
	cmp	w0, w21
	beq	.L2006
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 552
	mov	w2, 55
	bl	printk
.L2008:
	b	.L2008
.L2006:
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
	ldrh	w0, [x20,1040]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L2011
	bl	pm_free_sblk
	ldr	x2, [x19,2800]
	ldrh	w1, [x20,1040]
	ldrh	w3, [x2,688]
	sub	w1, w1, #1
	cmp	w3, w1
	blt	.L2011
	add	x0, x2, x0, uxth 1
	ldrh	w21, [x0,416]
	mov	w25, 65535
	cmp	w21, w25
	bne	.L2013
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 568
	mov	w2, 127
	bl	printk
.L2014:
	b	.L2014
.L2013:
	bl	pm_select_ram_region
	uxth	x20, w0
	add	x19, x19, 3784
	lsl	x0, x20, 4
	add	x24, x19, x0
	mov	x22, x20
	ldrh	w0, [x19,x0]
	cmp	w0, w25
	beq	.L2015
	ldr	x1, [x24,8]
	cbz	x1, .L2015
	ldrsh	w2, [x24,2]
	tbz	w2, #31, .L2015
	bl	pm_write_page
	ldrh	w0, [x24,2]
	and	w0, w0, 32767
	strh	w0, [x24,2]
.L2015:
	add	x23, x23, :lo12:.LANCHOR0
	mov	w19, 0
	add	x0, x23, 3784
	mov	w24, 21
	mov	w25, 1
	add	x20, x0, x20, lsl 4
.L2016:
	ldr	x0, [x23,2800]
	ldrh	w1, [x0,698]
	cmp	w1, w19
	bls	.L2022
	ldrh	w1, [x23,3218]
	add	x0, x0, x19, sxtw 2
	ldr	w0, [x0,704]
	lsr	w0, w0, w1
	sub	w1, w24, w1
	lsl	w1, w25, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x23,3216]
	udiv	w1, w1, w0
	cmp	w21, w1, uxth
	bne	.L2017
	mov	w1, w22
	mov	w0, w19
	bl	load_l2p_region
	ldr	x1, [x20,8]
	mov	w0, w19
	bl	pm_write_page
.L2017:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2016
.L2022:
	bl	pm_free_sblk
.L2011:
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
	add	x1, x19, 3784
	add	x20, x1, x0
	ldrh	w0, [x1,x0]
	ldr	x1, [x20,8]
	bl	pm_write_page
	ldrh	w0, [x20,2]
	and	w0, w0, 32767
	strh	w0, [x20,2]
	ldr	w0, [x19,3776]
	cbz	w0, .L2024
	bl	pm_gc
	str	wzr, [x19,3776]
.L2024:
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
	add	x20, x20, 3784
.L2030:
	add	x1, x20, x19, lsl 4
	uxth	w0, w19
	ldrsh	w1, [x1,2]
	tbz	w1, #31, .L2029
	bl	pm_flush_id
.L2029:
	add	x19, x19, 1
	cmp	x19, 32
	bne	.L2030
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
	str	wzr, [x20,3776]
	bl	ftl_malloc
	adrp	x1, .LANCHOR7
	add	x20, x20, 3784
	add	x2, x1, :lo12:.LANCHOR7
	add	x22, x20, 512
	mov	x21, x1
	mov	w24, -1
	str	x0, [x2,3144]
.L2036:
	strh	w24, [x20]
	strh	wzr, [x20,2]
	cbz	w23, .L2035
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 9
	bl	ftl_malloc
	str	x0, [x20,8]
.L2035:
	add	x20, x20, 16
	cmp	x20, x22
	bne	.L2036
	add	x20, x19, :lo12:.LANCHOR0
	add	x1, x21, :lo12:.LANCHOR7
	mov	w4, 4
	ldr	x25, [x1,3144]
	ldr	x1, [x20,2800]
	ldr	x23, [x20,3792]
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
	bgt	.L2037
	adrp	x0, .LC174
	ldrh	w1, [x1,692]
	mov	w3, w21
	add	x0, x0, :lo12:.LC174
	adrp	x26, .LC175
	add	w21, w21, 1
	bl	printk
	add	x26, x26, :lo12:.LC175
	ldr	x0, [x20,2800]
	ldrsh	w20, [x0,696]
.L2038:
	cmp	w20, w21
	bge	.L2051
	add	x27, x19, :lo12:.LANCHOR0
	mov	x2, x23
	mov	x3, x25
	ldr	x0, [x27,2800]
	ldrh	w22, [x27,3230]
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
	beq	.L2039
	cmn	w28, #1
	beq	.L2039
	ldr	x1, [x27,2800]
	ldr	w0, [x25]
	ldrh	w2, [x1,698]
	cmp	w0, w2
	bcs	.L2039
	add	x0, x1, x0, uxtw 2
	str	w22, [x0,704]
.L2039:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2038
.L2051:
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LANCHOR6-256
	mov	w1, 1
	add	w24, w24, 1
	strb	w1, [x0,#:lo12:.LANCHOR6-256]
	ldr	x0, [x19,2800]
	strh	w24, [x0,696]
	bl	pm_free_sblk
	ldr	x1, [x19,3792]
	mov	w0, -1
	bl	pm_write_page
	ldr	x1, [x19,3792]
	mov	w0, -1
	bl	pm_write_page
.L2037:
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
	add	x3, x3, 3784
	lsl	w20, w4, 7
	lsl	w24, w4, 7
	cmp	w0, w2
	udiv	w20, w0, w20
	uxth	w25, w20
	msub	w24, w25, w24, w0
	uxth	x24, w24
	bcc	.L2064
	adrp	x0, .LC176
	mov	w1, w5
	add	x0, x0, :lo12:.LC176
	bl	printk
	mov	w0, -1
	b	.L2054
.L2068:
	ldrh	w0, [x3]
	cmp	w0, w25
	beq	.L2056
.L2055:
	add	w19, w19, 1
	add	x3, x3, 16
	uxth	x19, w19
	cmp	w19, 32
	beq	.L2067
.L2064:
	ldr	x0, [x3,8]
	cbnz	x0, .L2068
	b	.L2055
.L2067:
	bl	pm_select_ram_region
	uxth	x19, w0
	add	x2, x21, :lo12:.LANCHOR0
	sbfiz	x3, x19, 4, 32
	add	x2, x2, 3784
	mov	w1, w0
	add	x4, x2, x3
	ldrh	w3, [x2,x3]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2069
.L2060:
	adrp	x0, .LANCHOR7+3156
	strb	w19, [x0,#:lo12:.LANCHOR7+3156]
	mov	w0, w25
	bl	load_l2p_region
.L2056:
	cbnz	w23, .L2057
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, x19, lsl 4
	ldr	x0, [x0,3792]
	ldr	w0, [x0,x24,lsl 2]
	str	w0, [x22]
	b	.L2058
.L2057:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x22]
	add	x0, x0, 3784
	add	x0, x0, x19, lsl 4
	ldr	x1, [x0,8]
	str	w2, [x1,x24,lsl 2]
	ldrh	w1, [x0,2]
	orr	w1, w1, -32768
	strh	w1, [x0,2]
	adrp	x0, .LANCHOR4-56
	strb	w20, [x0,#:lo12:.LANCHOR4-56]
.L2058:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w2, 32767
	add	x21, x21, 3784
	mov	w0, 0
	add	x19, x21, x19, lsl 4
	ldrh	w1, [x19,2]
	and	w3, w1, 32767
	cmp	w3, w2
	beq	.L2054
	add	w1, w1, 1
	strh	w1, [x19,2]
	b	.L2054
.L2069:
	ldrsh	w2, [x4,2]
	tbz	w2, #31, .L2060
	str	x1, [x29,88]
	bl	pm_flush_id
	ldr	x1, [x29,88]
	b	.L2060
.L2054:
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
	ldr	w0, [x22,2812]
	cmn	w0, #1
	beq	.L2080
	mov	w0, 0
	bl	buf_alloc
	mov	x19, x0
	mov	w1, -1
	cbz	x0, .L2081
	ldr	w21, [x22,2812]
	str	w21, [x0,36]
	str	w1, [x22,2812]
	ldr	x23, [x0,8]
	ldr	x22, [x0,24]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2073
	mov	w0, w21
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2073:
	ldr	w1, [x29,76]
	str	w1, [x19,40]
	str	w1, [x19,44]
	cmn	w1, #1
	bne	.L2074
	add	x20, x20, :lo12:.LANCHOR0
	mov	x0, x23
	mov	w1, 0
	ldrb	w2, [x20,2776]
	lsl	w2, w2, 9
	bl	ftl_memset
	ldrb	w0, [x19,2]
	orr	w0, w0, 8
	strb	w0, [x19,2]
	b	.L2080
.L2074:
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w23, [x19,52]
	cmn	w23, #1
	bne	.L2075
	ldrb	w0, [x19,2]
	add	x20, x20, :lo12:.LANCHOR0
	and	w0, w0, -9
	strb	w0, [x19,2]
	ldr	x1, [x20,2792]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2076
.L2075:
	cmp	w23, 256
	bne	.L2076
	add	x20, x20, :lo12:.LANCHOR0
	mov	w5, 21
	mov	w24, 1
	ldr	w4, [x29,76]
	ldr	w2, [x19,36]
	adrp	x0, .LC177
	ldrh	w3, [x20,3218]
	add	x0, x0, :lo12:.LC177
	ldrb	w20, [x20,3216]
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
.L2076:
	ldr	w5, [x22,4]
	cmp	w5, w21
	beq	.L2077
	adrp	x0, .LC178
	ldrb	w1, [x19,1]
	ldr	w3, [x29,76]
	add	x0, x0, :lo12:.LC178
	ldr	w4, [x22]
	mov	w2, w21
	ldr	w6, [x22,8]
	ldr	w7, [x22,12]
	bl	printk
.L2077:
	ldr	w0, [x22,4]
	cmp	w0, w21
	cset	w0, ne
	cbnz	w0, .L2083
	cmn	w23, #1
	bne	.L2071
.L2083:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 576
	mov	w2, 781
	bl	printk
.L2079:
	b	.L2079
.L2081:
	mov	w0, w1
	b	.L2071
.L2080:
	mov	w0, 0
.L2071:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_read_ahead, .-ftl_read_ahead
	.align	2
	.global	gc_update_l2p_map
	.type	gc_update_l2p_map, %function
gc_update_l2p_map:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w21, 0
	ldr	x25, [x0,2792]
	mov	w20, w21
	mov	w24, w21
	bl	timer_get_time
	add	x26, x25, 80
.L2086:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,3234]
	cmp	w20, w1
	bge	.L2096
	lsl	w0, w20, 1
	mov	w23, 0
	sub	w0, w0, #1
	str	w0, [x29,124]
.L2097:
	ldrb	w0, [x26,9]
	cmp	w23, w0
	bge	.L2094
	sxtw	x0, w23
	mov	w22, 1
	str	x0, [x29,112]
.L2095:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3193]
	cmp	w22, w0
	bgt	.L2102
	adrp	x1, .LANCHOR4
	sbfiz	x0, x24, 2, 32
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x2, [x1,1016]
	ldr	w27, [x2,x0]
	cmn	w27, #1
	beq	.L2087
	ldr	x1, [x1,1024]
	ldr	w28, [x1,x0]
	mov	w0, w27
	bl	lpa_hash_get_ppa
	str	w0, [x29,140]
	cmn	w0, #1
	bne	.L2088
	mov	w0, w27
	add	x1, x29, 140
	mov	w2, 0
	bl	pm_log2phys
.L2088:
	ldr	w3, [x29,140]
	cmp	w3, w28
	bne	.L2089
	ldr	x0, [x29,112]
	add	x0, x0, 8
	ldrh	w1, [x26,x0,lsl 1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,3230]
	ldrb	w0, [x0,3193]
	cmp	w0, 3
	mul	w1, w1, w2
	bne	.L2090
	add	w1, w1, w20
	orr	w1, w1, w22, lsl 24
	b	.L2101
.L2090:
	cmp	w0, 2
	bne	.L2092
	ldr	w0, [x29,124]
	add	w1, w1, w0
	add	w1, w1, w22
	orr	w1, w1, 33554432
	b	.L2101
.L2092:
	add	w1, w1, w20
.L2101:
	str	w1, [x29,136]
	mov	w2, 1
	add	x1, x29, 136
	mov	w0, w27
	str	x3, [x29,104]
	add	w21, w21, 1
	bl	pm_log2phys
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 21
	mov	w3, 1
	ldrh	w0, [x2,3218]
	sub	w1, w1, w0
	lsl	w1, w3, w1
	ldr	x3, [x29,104]
	sub	w1, w1, #1
	lsr	w3, w3, w0
	ldrb	w0, [x2,3216]
	and	w3, w1, w3
	udiv	w0, w3, w0
	bl	ftl_vpn_decrement
.L2089:
	add	w24, w24, 1
	bl	timer_get_time
.L2087:
	add	w22, w22, 1
	b	.L2095
.L2102:
	add	w23, w23, 1
	b	.L2097
.L2094:
	add	w20, w20, 1
	b	.L2086
.L2096:
	ldrh	w1, [x25,80]
	ldr	x0, [x0,2832]
	strh	w21, [x0,x1,lsl 1]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	gc_update_l2p_map, .-gc_update_l2p_map
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
	ldr	x24, [x0,2792]
	ldrh	w1, [x0,3210]
	add	x28, x24, 80
	ldrb	w0, [x28,9]
	mul	w0, w1, w0
	str	w0, [x29,140]
	bl	timer_get_time
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2104
	adrp	x0, .LC179
	ldrh	w1, [x24,80]
	add	x0, x0, :lo12:.LC179
	bl	printk
.L2104:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x24,80]
	ldr	w2, [x29,140]
	mov	w23, 0
	mov	w21, w23
	ldr	x0, [x0,2832]
	sub	w2, w2, #1
	strh	w2, [x0,x1,lsl 1]
	adrp	x0, .LC182
	add	x0, x0, :lo12:.LC182
	str	x0, [x29,120]
	adrp	x0, .LC181
	add	x0, x0, :lo12:.LC181
	str	x0, [x29,112]
	adrp	x0, .LC180
	add	x0, x0, :lo12:.LC180
	str	x0, [x29,104]
.L2105:
	ldr	w0, [x29,140]
	cmp	w21, w0
	beq	.L2140
	adrp	x22, .LANCHOR4
	str	x22, [x29,128]
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x0, [x0,1016]
	ldr	w2, [x0,w21,sxtw 2]
	cmn	w2, #1
	beq	.L2106
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 7
	udiv	w0, w2, w0
	and	w0, w0, 65535
	str	w0, [x29,136]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2107
	ldr	x0, [x29,104]
	mov	w3, w21
	ldr	w1, [x29,136]
	bl	printk
.L2107:
	mov	w25, w21
.L2113:
	ldr	x0, [x29,128]
	sbfiz	x26, x25, 2, 32
	add	x1, x0, :lo12:.LANCHOR4
	ldr	x0, [x1,1016]
	ldr	w22, [x0,x26]
	cmn	w22, #1
	beq	.L2108
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,136]
	ldrb	w0, [x0,2776]
	lsl	w0, w0, 7
	udiv	w0, w22, w0
	cmp	w2, w0, uxth
	bne	.L2108
	ldr	x0, [x1,1024]
	ldr	w27, [x0,x26]
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L2109
	mov	w0, w22
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L2109:
	ldr	w3, [x29,156]
	cmp	w3, w27
	bne	.L2110
	ldr	x0, [x29,128]
	add	x1, x29, 152
	mov	w2, 1
	add	w23, w23, 1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1032]
	ldr	w0, [x0,x26]
	str	w0, [x29,152]
	mov	w0, w22
	bl	pm_log2phys
	add	x1, x19, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w0, 1
	ldrh	w3, [x1,3218]
	sub	w2, w2, w3
	lsr	w3, w27, w3
	lsl	w2, w0, w2
	sub	w2, w2, #1
	and	w3, w2, w3
	ldrb	w2, [x1,3216]
	udiv	w2, w3, w2
	uxth	w2, w2
	str	x2, [x29,96]
	mov	w0, w2
	bl	ftl_vpn_decrement
	ldrh	w0, [x28]
	ldr	x2, [x29,96]
	cmp	w0, w2
	bne	.L2111
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2111
	ldr	x0, [x29,112]
	mov	w1, w22
	ldr	w3, [x29,156]
	mov	w2, w27
	bl	printk
	b	.L2111
.L2110:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2112
	ldr	x0, [x29,120]
	mov	w1, w22
	mov	w2, w27
	bl	printk
.L2112:
	ldrh	w0, [x28]
	bl	ftl_vpn_decrement
.L2111:
	ldr	x0, [x29,128]
	mov	w1, -1
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1016]
	str	w1, [x0,x26]
	bl	timer_get_time
.L2108:
	ldr	w0, [x29,140]
	add	w25, w25, 1
	cmp	w25, w0
	bne	.L2113
.L2106:
	add	w21, w21, 1
	b	.L2105
.L2140:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,400]
	tbz	x0, 8, .L2115
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w3, [x24,80]
	ldr	x1, [x0,2792]
	ldr	x2, [x0,2832]
	adrp	x0, .LC183
	add	x0, x0, :lo12:.LC183
	ldrh	w1, [x1,80]
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w23
	bl	printk
.L2115:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x24,80]
	ldr	x0, [x19,2832]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, w23
	beq	.L2103
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 592
	mov	w2, 451
	bl	printk
.L2117:
	b	.L2117
.L2103:
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
	add	x2, x3, 2848
	stp	x21, x22, [sp,32]
	ldrb	w0, [x2,4]
	add	x1, x2, x0, sxtw 1
	ldrb	w7, [x3,3212]
	ldrh	w1, [x1,40]
	ldrh	w3, [x2,2]
.L2142:
	cmp	w1, w6
	bne	.L2184
	add	w0, w0, 1
	uxtb	w0, w0
	cmp	w0, w7
	bne	.L2143
	add	w3, w3, 1
	mov	w4, 1
	mov	w0, 0
	uxth	w3, w3
.L2143:
	add	x1, x2, x0, sxtw 1
	mov	w5, 1
	ldrh	w1, [x1,40]
	b	.L2142
.L2184:
	cbz	w5, .L2145
	add	x2, x19, :lo12:.LANCHOR0
	strb	w0, [x2,2852]
.L2145:
	cbz	w4, .L2146
	add	x0, x19, :lo12:.LANCHOR0
	strh	w3, [x0,2850]
.L2146:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,3193]
	ldrh	w20, [x0,3230]
	cmp	w2, 2
	mul	w1, w1, w20
	bne	.L2147
	ldrb	w2, [x0,2854]
	add	x0, x0, 2848
	cbnz	w2, .L2147
	ldrh	w20, [x0,2]
	add	w20, w1, w20, lsl 1
	b	.L2148
.L2147:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w20, [x0,2850]
	add	w20, w1, w20
.L2148:
	add	x2, x19, :lo12:.LANCHOR0
	add	x1, x2, 2848
	ldrb	w0, [x2,2852]
	add	w0, w0, 1
	uxtb	w0, w0
	strb	w0, [x2,2852]
	ldrb	w2, [x2,3212]
	cmp	w2, w0
	bne	.L2149
	ldrh	w0, [x1,2]
	strb	wzr, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,2]
.L2149:
	mov	w0, 1
	mov	w21, 0
	add	x19, x19, :lo12:.LANCHOR0
	bl	buf_alloc
	mov	x22, x0
.L2160:
	ldrb	w0, [x19,2854]
	cbnz	w21, .L2162
	cmp	w0, 3
	beq	.L2150
.L2162:
	cmp	w0, w21
	bgt	.L2152
.L2161:
	mov	x0, x22
	bl	buf_free
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L2152:
	ldrb	w1, [x19,3193]
	cmp	w1, 2
	bne	.L2153
	cbnz	w0, .L2153
	add	w0, w21, w20
	orr	w0, w0, 33554432
	b	.L2183
.L2153:
	orr	w0, w20, w21, lsl 24
.L2183:
	str	w0, [x22,40]
	mov	w1, 1
	mov	x0, x22
	bl	sblk_read_page
	ldr	w0, [x22,52]
	cmp	w0, 512
	beq	.L2157
	cmn	w0, #1
	beq	.L2157
	ldr	x0, [x22,24]
	ldr	w23, [x0,4]
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	bne	.L2158
	mov	w0, w23
	add	x1, x29, 76
	mov	w2, 0
	bl	pm_log2phys
.L2158:
	ldr	w0, [x29,76]
	cmp	w20, w0
	bne	.L2157
	ldrh	w2, [x19,2872]
	ldr	x1, [x19,3184]
	str	w20, [x1,x2,lsl 2]
	ldrh	w1, [x19,2872]
	add	w1, w1, 1
	strh	w1, [x19,2872]
.L2157:
	ldrh	w0, [x19,2874]
	add	w0, w0, 1
	strh	w0, [x19,2874]
.L2150:
	add	w21, w21, 1
	cmp	w21, 4
	bne	.L2160
	b	.L2161
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
	ldr	x0, [x1,2792]
	ldr	w2, [x0,544]
	cmn	w2, #1
	beq	.L2186
	mov	w20, 11
	mov	w21, 21
	mov	w22, 1
.L2187:
	add	x24, x19, :lo12:.LANCHOR0
	add	x1, x29, 76
	mov	w2, 0
	ldr	x0, [x24,2792]
	ldr	w0, [x0,544]
	bl	pm_log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2188
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldr	w1, [x29,76]
	str	w1, [x0,40]
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 256
	bne	.L2189
	ldrh	w1, [x24,3218]
	ldr	w2, [x29,76]
	sub	w3, w21, w1
	ldrb	w0, [x24,3216]
	lsl	w3, w22, w3
	lsr	w1, w2, w1
	sub	w3, w3, #1
	mov	w2, 0
	and	w1, w3, w1
	udiv	w0, w1, w0
	mov	w1, 1
	bl	gc_add_sblk
.L2189:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x23,24]
	ldr	x0, [x0,2792]
	ldr	w1, [x1,4]
	ldr	w0, [x0,544]
	cmp	w1, w0
	beq	.L2190
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 616
	mov	w2, 1156
	bl	printk
.L2191:
	b	.L2191
.L2190:
	mov	x0, x23
	bl	buf_free
.L2188:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x0, [x2,2792]
	ldr	w2, [x2,2784]
	ldr	w1, [x0,544]
	add	w1, w1, 1
	str	w1, [x0,544]
	cmp	w1, w2
	bcc	.L2192
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
	b	.L2185
.L2192:
	ldr	w0, [x29,76]
	cmn	w0, #1
	bne	.L2185
	sub	w20, w20, #1
	uxth	w20, w20
	cbnz	w20, .L2187
	b	.L2185
.L2186:
	ldr	x1, [x1,2800]
	ldr	w2, [x1,40]
	cmp	w2, 20480
	bls	.L2197
	ldr	w3, [x1,44]
	add	w3, w3, w2, lsr 10
	and	w2, w2, 1023
	str	w3, [x1,44]
	str	w2, [x1,40]
.L2197:
	ldr	w2, [x0,536]
	ldr	w3, [x0,12]
	add	w2, w2, 12959744
	add	w2, w2, 256
	cmp	w3, w2
	bhi	.L2198
	ldr	w2, [x0,540]
	ldr	w4, [x1,44]
	add	w2, w2, 98304
	add	w2, w2, 1696
	cmp	w4, w2
	bls	.L2185
.L2198:
	ldr	w1, [x1,44]
	str	w1, [x0,540]
	str	w3, [x0,536]
	str	wzr, [x0,544]
.L2185:
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
	ldr	x1, [x20,2792]
	ldrh	w0, [x20,2826]
	ldr	w1, [x1,608]
	cmp	w1, w0
	bcs	.L2203
	bl	timer_get_time
	ldr	x21, [x20,2792]
	ldr	w1, [x21,604]
	add	w1, w1, 28672
	add	w1, w1, 1328
	cmp	w0, w1
	bls	.L2203
	bl	timer_get_time
	str	w0, [x21,604]
	ldr	x0, [x20,2792]
	ldrh	w2, [x20,2826]
	ldr	w1, [x0,600]
	cmp	w1, w2
	bcs	.L2209
	ldr	x2, [x20,2800]
	ldrh	w2, [x2,134]
	cmp	w1, w2
	bcs	.L2210
.L2209:
	add	x1, x19, :lo12:.LANCHOR0
	ldr	x1, [x1,2800]
	ldrh	w1, [x1,134]
	str	w1, [x0,600]
.L2210:
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
	cbz	w1, .L2203
	add	x2, x19, :lo12:.LANCHOR0
	uxtw	x22, w20
	ldr	x25, [x2,2840]
	add	x25, x25, x22, lsl 2
	ldrb	w1, [x25,2]
	tst	w1, 192
	and	w3, w1, 224
	beq	.L2211
	cmp	w3, 224
	beq	.L2211
	ldr	x1, [x2,2792]
	ldrh	w4, [x1,16]
	cmp	w4, w20
	beq	.L2211
	ldrh	w4, [x1,48]
	cmp	w4, w20
	beq	.L2211
	ldrh	w1, [x1,80]
	cmp	w1, w20
	bne	.L2212
.L2211:
	cbnz	w3, .L2203
	add	x19, x19, :lo12:.LANCHOR0
	lsl	x22, x22, 1
	ldr	x0, [x19,2832]
	ldrh	w1, [x0,x22]
	cbz	w1, .L2213
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 640
	mov	w2, 1361
	bl	printk
.L2214:
	b	.L2214
.L2213:
	strh	w3, [x0,x22]
	b	.L2203
.L2212:
	add	x1, x29, 144
	and	w0, w0, 65535
	sub	w0, w0, #1
	add	x0, x1, x0, sxtw 1
	ldrh	w1, [x0,-16]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2216
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 640
	mov	w2, 1367
	bl	printk
.L2217:
	b	.L2217
.L2216:
	ldrh	w0, [x2,3234]
	cmp	w3, 160
	ldrh	w4, [x2,3230]
	mov	w26, 1
	sub	w0, w0, #1
	mul	w1, w1, w4
	uxth	w0, w0
	bne	.L2218
	ldrb	w3, [x2,3193]
	cmp	w3, 2
	uxth	w26, w3
	orr	w1, w1, w3, lsl 24
	bne	.L2218
	ldrh	w0, [x2,3210]
	sub	w0, w0, #1
	uxth	w0, w0
.L2218:
	orr	w21, w0, w1
	mov	w0, 1
	bl	buf_alloc
	str	w21, [x0,40]
	mov	w1, 1
	add	x21, x19, :lo12:.LANCHOR0
	mov	x23, x0
	bl	sblk_read_page
	ldrh	w0, [x21,3210]
	mov	w1, 255
	ldrb	w2, [x21,3212]
	mul	w2, w0, w2
	ldr	x0, [x21,3184]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2230
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2219
.L2230:
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	b	.L2240
.L2219:
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2221
	mov	w0, w20
	mov	w1, 1
.L2240:
	bl	gc_add_sblk
	mov	x0, x23
	bl	buf_free
	b	.L2203
.L2221:
	ldrb	w0, [x21,3212]
	mov	x27, 0
	ldrh	w1, [x21,3234]
	and	w28, w24, 65535
	mov	w21, w27
	mul	w0, w0, w1
	mul	w26, w26, w0
	uxth	w0, w26
	ldr	x26, [x23,8]
	str	w0, [x29,108]
.L2222:
	ldr	w0, [x29,108]
	cmp	w0, w27
	ble	.L2241
	ldr	w0, [x26,x27,lsl 2]
	cmn	w0, #1
	beq	.L2223
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2224
	ldr	w0, [x26,x27,lsl 2]
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2224:
	add	x4, x19, :lo12:.LANCHOR0
	mov	w1, 21
	ldrh	w0, [x4,3218]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x29,124]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x4,3216]
	and	w0, w2, w0
	udiv	w0, w0, w1
	cmp	w0, w28
	bne	.L2223
	add	w21, w21, 1
	uxth	w21, w21
.L2223:
	add	x27, x27, 1
	b	.L2222
.L2241:
	mov	x0, x23
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2226
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w4, [x25,2]
	uxth	w1, w24
	mov	w3, w21
	ubfx	x4, x4, 5, 3
	ldr	x2, [x0,2832]
	adrp	x0, .LC184
	add	x0, x0, :lo12:.LC184
	ldrh	w2, [x2,x22,lsl 1]
	bl	printk
.L2226:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w21, 31
	ldr	x0, [x19,2832]
	strh	w21, [x0,x22,lsl 1]
	bhi	.L2203
	mov	w0, w20
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
.L2203:
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
	uxth	w24, w0
	mov	w0, 65535
	stp	x19, x20, [sp,64]
	stp	x21, x22, [sp,80]
	stp	x25, x26, [sp,112]
	stp	x27, x28, [sp,128]
	cmp	w24, w0
	beq	.L2265
	adrp	x3, .LANCHOR0
	uxtw	x0, w24
	add	x2, x3, :lo12:.LANCHOR0
	lsl	x20, x0, 2
	str	x0, [x29,128]
	mov	w26, 1
	str	x3, [x29,152]
	ldr	x0, [x2,2840]
	add	x0, x0, x20
	ldrb	w0, [x0,2]
	and	w0, w0, 224
	cmp	w0, 160
	bne	.L2244
	ldrb	w26, [x2,3193]
.L2244:
	str	x1, [x29,144]
	mov	w0, w24
	add	x1, x29, 192
	strh	w24, [x29,176]
	adrp	x19, .LANCHOR2
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	mov	w4, w0
	ldr	x0, [x29,152]
	strb	w1, [x29,185]
	add	x0, x0, :lo12:.LANCHOR0
	strh	wzr, [x29,178]
	strb	wzr, [x29,181]
	strh	wzr, [x29,186]
	ldrh	w2, [x0,3234]
	mul	w1, w1, w2
	strh	w1, [x29,182]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w1, [x1,400]
	tbz	x1, 12, .L2245
	ldr	x0, [x0,2840]
	mov	w1, w24
	mov	w3, w26
	uxtb	w4, w4
	add	x20, x0, x20
	adrp	x0, .LC185
	add	x0, x0, :lo12:.LC185
	ldrb	w2, [x20,2]
	ubfx	x2, x2, 5, 3
	bl	printk
.L2245:
	mov	w0, 1
	mov	w22, 0
	mov	w28, w22
	mov	w20, w22
	bl	buf_alloc
	mov	x27, x0
	adrp	x0, .LC187
	str	w22, [x29,140]
	add	x0, x0, :lo12:.LC187
	str	x0, [x29,120]
	adrp	x0, .LC186
	add	x0, x0, :lo12:.LC186
	str	x0, [x29,112]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29,104]
.L2246:
	ldr	x0, [x29,152]
	add	x21, x0, :lo12:.LANCHOR0
	ldrh	w0, [x21,3234]
	cmp	w0, w20
	bls	.L2262
	lsl	w0, w20, 1
	mov	w25, 0
	sub	w0, w0, #1
	str	w0, [x29,136]
.L2263:
	ldrb	w0, [x29,185]
	cmp	w0, w25
	bls	.L2260
	mov	w23, 1
.L2261:
	cmp	w23, w26
	bhi	.L2295
	add	x0, x29, 176
	add	x0, x0, x25, sxtw 1
	ldrh	w3, [x0,16]
	mov	w0, 65535
	cmp	w3, w0
	beq	.L2247
	ldr	x0, [x29,152]
	cmp	w26, 3
	add	x0, x0, :lo12:.LANCHOR0
	ldrh	w2, [x0,3230]
	mul	w2, w3, w2
	add	w21, w2, w20
	bne	.L2248
	orr	w21, w21, w23, lsl 24
	b	.L2249
.L2248:
	cmp	w26, 2
	bne	.L2249
	ldr	w1, [x29,136]
	ldrb	w21, [x0,3193]
	add	w2, w2, w1
	add	w2, w2, w23
	orr	w21, w2, w21, lsl 24
.L2249:
	str	w21, [x27,40]
	mov	x0, x27
	mov	w1, 1
	str	x3, [x29,96]
	bl	sblk_read_page
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w9, [x27,52]
	ldr	x3, [x29,96]
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2251
	ldr	x7, [x27,8]
	mov	w4, w9
	ldr	x0, [x27,24]
	mov	w2, w20
	str	x9, [x29,96]
	ldr	w1, [x7,12]
	str	w1, [sp]
	ldr	w1, [x0]
	str	w1, [sp,8]
	ldr	w1, [x0,4]
	str	w1, [sp,16]
	ldr	w1, [x0,8]
	str	w1, [sp,24]
	mov	w1, w3
	mov	w3, w21
	ldr	w0, [x0,12]
	str	w0, [sp,32]
	ldr	x0, [x29,104]
	ldr	w5, [x7]
	ldr	w6, [x7,4]
	ldr	w7, [x7,8]
	bl	printk
	ldr	x9, [x29,96]
.L2251:
	cmp	w9, 512
	beq	.L2267
	cmn	w9, #1
	bne	.L2252
.L2267:
	mov	w0, 1
	str	w0, [x29,140]
.L2252:
	ldr	x0, [x27,24]
	ldr	w1, [x0,4]
	str	x1, [x29,96]
	mov	w0, w1
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	ldr	x1, [x29,96]
	bne	.L2254
	mov	w0, w1
	mov	w2, 0
	add	x1, x29, 172
	bl	pm_log2phys
.L2254:
	ldr	w0, [x29,172]
	cmp	w0, w21
	bne	.L2255
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 1
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2255
	ldr	x2, [x27,24]
	mov	w1, w21
	ldr	x0, [x29,112]
	mov	w3, w22
	ldr	w2, [x2,4]
	bl	printk
.L2255:
	ldr	x0, [x29,144]
	cbz	x0, .L2256
	ubfiz	x21, x28, 2, 32
	ldr	w2, [x0,x21]
	ldr	x0, [x27,24]
	ldr	w0, [x0,4]
	cmp	w0, w2
	beq	.L2257
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 12, .L2257
	ldr	x0, [x29,120]
	mov	w1, w28
	bl	printk
.L2257:
	ldr	x0, [x29,144]
	ldr	x1, [x27,24]
	ldr	w0, [x0,x21]
	ldr	w1, [x1,4]
	cmp	w1, w0
	beq	.L2256
	cmn	w0, #1
	beq	.L2256
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 664
	mov	w2, 617
	bl	printk
.L2258:
	b	.L2258
.L2256:
	add	w28, w28, 1
.L2247:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L2261
.L2295:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2263
.L2260:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2246
.L2262:
	mov	x0, x27
	add	x19, x19, :lo12:.LANCHOR2
	bl	buf_free
	ldr	w0, [x19,400]
	tbz	x0, 12, .L2264
	ldr	x3, [x29,128]
	adrp	x0, .LC188
	ldr	x2, [x21,2832]
	add	x0, x0, :lo12:.LC188
	mov	w1, w24
	ldrh	w2, [x2,x3,lsl 1]
	mov	w3, w22
	bl	printk
.L2264:
	ldr	w0, [x29,140]
	b	.L2243
.L2265:
	mov	w0, 0
.L2243:
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
	.global	gc_search_src_blk
	.type	gc_search_src_blk, %function
gc_search_src_blk:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR0
	uxtb	w21, w0
	uxtb	w0, w1
	str	w0, [x29,124]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w24, w2
	ldr	x1, [x0,2800]
	ldr	x0, [x0,2792]
	add	x0, x0, x21, sxtw 1
	ldrh	w0, [x0,120]
	ldrh	w25, [x1,134]
	cbnz	w0, .L2298
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2299
	adrp	x0, .LC189
	ldr	w2, [x29,124]
	add	x0, x0, :lo12:.LC189
	mov	w1, w21
	mov	w3, w24
	bl	printk
.L2299:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2300
	add	x3, x20, :lo12:.LANCHOR0
	adrp	x4, .LANCHOR4+1014
	adrp	x0, .LC190
	add	x0, x0, :lo12:.LC190
	ldr	x1, [x3,2800]
	ldr	x2, [x3,2792]
	ldrh	w4, [x4,#:lo12:.LANCHOR4+1014]
	ldrh	w1, [x1,134]
	ldrh	w2, [x2,126]
	ldrh	w3, [x3,3176]
	bl	printk
.L2300:
	cmp	w24, 64
	bls	.L2301
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 680
	mov	w2, 1033
	bl	printk
.L2302:
	b	.L2302
.L2301:
	add	x0, x20, :lo12:.LANCHOR0
	adrp	x26, .LC191
	mov	w19, 0
	add	x26, x26, :lo12:.LC191
	ldrb	w1, [x0,3212]
	ldrh	w4, [x0,3234]
	ldrh	w3, [x0,3210]
	ldr	x0, [x0,2800]
	mul	w4, w1, w4
	mul	w3, w1, w3
	ldrh	w23, [x0,134]
	adrp	x0, .LANCHOR7
	add	x0, x0, :lo12:.LANCHOR7
	uxth	w27, w4
	uxth	w28, w3
	str	x0, [x29,112]
.L2303:
	add	x2, x20, :lo12:.LANCHOR0
	ldr	x0, [x2,2800]
	ldrh	w1, [x2,2826]
	ldrh	w0, [x0,134]
	sub	w6, w1, w0
	cmp	w23, w6
	bge	.L2312
	ldr	x2, [x2,2792]
	ldrh	w5, [x2,126]
	add	w5, w23, w5
	uxth	w25, w5
	cmp	w25, w1
	bcc	.L2304
	sub	w0, w0, w1
	add	w5, w25, w0
	uxth	w25, w5
	cmp	w25, w1
	bcc	.L2304
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 680
	mov	w2, 1043
	bl	printk
.L2305:
	b	.L2305
.L2304:
	add	x7, x20, :lo12:.LANCHOR0
	uxtw	x6, w25
	ldr	x0, [x7,2840]
	add	x0, x0, x6, lsl 2
	ldrb	w2, [x0,2]
	and	w2, w2, 224
	cmp	w2, 224
	beq	.L2306
	cmp	w2, 32
	cset	w1, eq
	cbnz	w1, .L2306
	lsl	x6, x6, 1
	ldr	x0, [x7,2832]
	cbnz	w2, .L2307
	ldrh	w1, [x0,x6]
	cbz	w1, .L2308
	mov	w2, w25
	mov	x0, x26
	str	x6, [x29,104]
	bl	printk
	mov	w0, w25
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x6, [x29,104]
.L2308:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	ldrh	w0, [x0,x6]
	cbz	w0, .L2306
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 680
	mov	w2, 1052
	bl	printk
.L2309:
	b	.L2309
.L2307:
	ldrh	w0, [x0,x6]
	mov	w3, 65535
	cmp	w0, w3
	beq	.L2306
	ldr	x3, [x29,112]
	cmp	w2, 160
	add	x6, x3, 3160
	ldr	w3, [x29,124]
	ldrb	w6, [x6,w25,sxtw]
	mul	w0, w0, w6
	mov	w6, 2
	csinc	w6, w6, wzr, eq
	uxth	w0, w0
	and	w6, w6, w3
	cbnz	w21, .L2311
	cmp	w27, w0
	csel	w4, w27, w0, cc
	uxth	w27, w4
	cbz	w6, .L2306
	ldrh	w1, [x7,3176]
	cmp	w1, w0
	bls	.L2306
	mov	w0, w25
	mov	w1, w21
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L2306
	b	.L2358
.L2311:
	cbz	w6, .L2313
	cmp	w2, 160
	adrp	x2, .LANCHOR4
	bne	.L2314
	add	x2, x2, :lo12:.LANCHOR4
	cmp	w28, w0
	csel	w3, w28, w0, cc
	ldrh	w2, [x2,1014]
	uxth	w28, w3
	cmp	w2, w0
	bls	.L2313
	b	.L2360
.L2314:
	add	x2, x2, :lo12:.LANCHOR4
	cmp	w27, w0
	csel	w4, w27, w0, cc
	ldrh	w2, [x2,1012]
	uxth	w27, w4
	cmp	w2, w0
	bhi	.L2313
.L2360:
	mov	w0, w25
	mov	w2, w21
	bl	gc_add_sblk
	cbz	w0, .L2313
.L2358:
	add	w19, w19, 1
	uxth	w19, w19
.L2313:
	cmp	w19, w24
	bhi	.L2312
.L2306:
	add	w23, w23, 1
	uxth	w23, w23
	b	.L2303
.L2312:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,2800]
	ldrh	w2, [x0,134]
	ldrh	w0, [x1,2826]
	sub	w0, w0, w2
	cmp	w23, w0
	ldr	x0, [x1,2792]
	blt	.L2316
	strh	w2, [x0,126]
	cbnz	w21, .L2317
	ldrh	w0, [x1,3176]
	cmp	w0, w27
	bcs	.L2318
	strh	w27, [x1,3176]
.L2318:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w1, 7
	ldrh	w0, [x20,3234]
	ldrh	w2, [x20,3176]
	mul	w1, w0, w1
	cmp	w2, w1, lsr 3
	bge	.L2320
	add	w0, w2, w0, lsr 3
	strh	w0, [x20,3176]
	b	.L2320
.L2317:
	ldrh	w0, [x1,3210]
	ldrb	w1, [x1,3216]
	mul	w1, w0, w1
	adrp	x0, .LANCHOR4
	cmp	w28, w1
	beq	.L2321
	add	x2, x0, :lo12:.LANCHOR4
	ldrh	w5, [x2,1014]
	cmp	w5, w28
	bcs	.L2321
	strh	w28, [x2,1014]
.L2321:
	add	x20, x20, :lo12:.LANCHOR0
	add	x7, x0, :lo12:.LANCHOR4
	ldrh	w6, [x20,3234]
	ldrh	w2, [x7,1014]
	lsr	w5, w6, 3
	sub	w1, w1, w5
	cmp	w2, w1
	bge	.L2322
	add	w2, w5, w2
	strh	w2, [x7,1014]
.L2322:
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w1, [x0,1012]
	cmp	w1, w6, lsr 1
	bls	.L2320
	sub	w1, w1, w5
	strh	w1, [x0,1012]
	b	.L2320
.L2316:
	strh	w25, [x0,126]
	ldrh	w0, [x1,3234]
	cbnz	w21, .L2324
	ldrh	w2, [x1,3176]
	cmp	w2, w0, lsr 2
	bls	.L2320
	sub	w0, w2, w0, lsr 3
	strh	w0, [x1,3176]
	b	.L2320
.L2324:
	adrp	x1, .LANCHOR4
	add	x5, x1, :lo12:.LANCHOR4
	ldrh	w2, [x5,1014]
	cmp	w2, w0, lsr 1
	bls	.L2325
	sub	w2, w2, w0, lsr 3
	strh	w2, [x5,1014]
.L2325:
	add	x20, x20, :lo12:.LANCHOR0
	add	x1, x1, :lo12:.LANCHOR4
	ldrb	w5, [x20,3216]
	ldrh	w2, [x1,1012]
	mul	w0, w0, w5
	sub	w0, w0, #32
	cmp	w2, w0
	bge	.L2320
	add	w2, w2, 32
	strh	w2, [x1,1012]
.L2320:
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w0, [x22,400]
	tbz	x0, 8, .L2326
	adrp	x0, .LC192
	mov	w1, w19
	add	x0, x0, :lo12:.LC192
	mov	w2, w27
	mov	w3, w28
	bl	printk
.L2326:
	mov	w0, w19
.L2298:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	gc_search_src_blk, .-gc_search_src_blk
	.align	2
	.global	zftl_read
	.type	zftl_read, %function
zftl_read:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w26, w2
	mov	x23, x3
	cbnz	w0, .L2362
	adrp	x0, .LANCHOR0+4
	ldr	w2, [x0,#:lo12:.LANCHOR0+4]
	mov	w0, 24576
	b	.L2363
.L2362:
	cmp	w0, 3
	bhi	.L2389
	lsl	w0, w0, 13
	mov	w2, 8192
.L2363:
	add	w3, w1, w26
	cmp	w3, w2
	bhi	.L2389
	adrp	x22, .LANCHOR0
	add	w24, w0, w1
	add	x1, x22, :lo12:.LANCHOR0
	add	w28, w24, w26
	str	wzr, [x29,152]
	ldr	x2, [x1,2800]
	ldr	w0, [x2,24]
	add	w0, w0, w26
	str	w0, [x2,24]
	ldrb	w0, [x1,2776]
	sub	w1, w28, #1
	udiv	w27, w24, w0
	mov	w21, w27
	udiv	w0, w1, w0
	str	w0, [x29,148]
	sub	w25, w0, w27
	add	w0, w25, 1
	str	w0, [x29,156]
	uxth	w0, w26
	str	w0, [x29,144]
	adrp	x0, .LC178
	add	x0, x0, :lo12:.LC178
	str	x0, [x29,136]
	adrp	x0, .LC193
	add	x0, x0, :lo12:.LC193
	str	x0, [x29,128]
.L2365:
	ldr	w0, [x29,156]
	cbz	w0, .L2406
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,148]
	cmp	w21, w27
	cset	w2, eq
	cmp	w21, w1
	ldrb	w0, [x0,2776]
	cset	w20, eq
	uxth	w19, w0
	cbnz	w20, .L2392
	cbz	w2, .L2366
	b	.L2405
.L2392:
	cbz	w2, .L2368
.L2405:
	udiv	w20, w24, w0
	msub	w20, w20, w0, w24
	ldr	w0, [x29,144]
	uxth	w20, w20
	sub	w19, w19, w20
	uxth	w19, w19
	cmp	w19, w26
	csel	w19, w0, w19, hi
	b	.L2366
.L2368:
	msub	w19, w0, w21, w28
	mov	w20, w2
	uxtb	w19, w19
.L2366:
	add	x1, x22, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 728
.L2371:
	ldr	w2, [x1,36]
	cmp	w2, w21
	bne	.L2369
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L2369
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
	b	.L2370
.L2369:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L2371
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2372
	mov	w0, w21
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2372:
	ldr	w0, [x29,172]
	cmn	w0, #1
	bne	.L2373
	mov	w19, 0
.L2374:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	cmp	w19, w0
	bcs	.L2370
	madd	w0, w21, w0, w19
	cmp	w0, w24
	bcc	.L2375
	cmp	w0, w28
	bcs	.L2375
	mov	x0, x23
	mov	w1, 0
	mov	w2, 512
	add	x23, x23, 512
	bl	ftl_memset
.L2375:
	add	w19, w19, 1
	b	.L2374
.L2373:
	mov	w0, 0
	bl	buf_alloc
	mov	x25, x0
	cbnz	x0, .L2377
	bl	ftl_read_ahead
	b	.L2365
.L2377:
	add	x1, x22, :lo12:.LANCHOR0
	ldr	x2, [x1,2800]
	ldr	w0, [x2,40]
	add	w0, w0, 1
	str	w0, [x2,40]
	ldr	w0, [x29,172]
	str	w0, [x25,40]
	str	w0, [x25,44]
	ldrb	w0, [x1,2776]
	cmp	w0, w19
	bne	.L2379
	str	x23, [x25,16]
.L2379:
	mov	x0, x25
	mov	w1, 1
	bl	sblk_read_page
	ldr	w9, [x25,52]
	cmn	w9, #1
	bne	.L2380
	add	x0, x22, :lo12:.LANCHOR0
	str	w9, [x29,152]
	ldr	x1, [x0,2792]
	ldr	w0, [x1,552]
	add	w0, w0, 1
	str	w0, [x1,552]
	b	.L2381
.L2380:
	cmp	w9, 256
	bne	.L2381
	add	x1, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w5, 1
	ldr	w0, [x29,172]
	ldr	w3, [x25,40]
	ldrh	w4, [x1,3218]
	str	x9, [x29,104]
	sub	w2, w2, w4
	lsr	w4, w0, w4
	lsl	w2, w5, w2
	ldrb	w0, [x1,3216]
	sub	w2, w2, #1
	str	x5, [x29,112]
	and	w4, w2, w4
	ldr	w2, [x25,36]
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
.L2381:
	ldr	x7, [x25,24]
	ldr	w5, [x7,4]
	cmp	w5, w21
	beq	.L2382
	ldr	w4, [x7]
	mov	w2, w21
	ldr	w6, [x7,8]
	ldrb	w1, [x25,1]
	ldr	w3, [x29,172]
	ldr	w7, [x7,12]
	ldr	x0, [x29,136]
	str	x9, [x29,120]
	bl	printk
	add	x3, x22, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3218]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,172]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3216]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x9, [x29,120]
.L2382:
	ldr	x0, [x25,24]
	ldr	w0, [x0,4]
	cmp	w0, w21
	bne	.L2393
	cmn	w9, #1
	bne	.L2383
.L2393:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 704
	mov	w2, 936
	bl	printk
.L2385:
	b	.L2385
.L2383:
	ldr	x0, [x25,16]
	cbnz	x0, .L2386
	ldr	x1, [x25,8]
	ubfiz	x20, x20, 9, 16
	mov	x0, x23
	lsl	w2, w19, 9
	add	x1, x1, x20
	bl	ftl_memcpy
.L2386:
	mov	x0, x25
	ubfiz	x19, x19, 9, 16
	add	x23, x23, x19
	bl	buf_free
.L2370:
	ldr	w0, [x29,156]
	add	w21, w21, 1
	sub	w0, w0, #1
	str	w0, [x29,156]
	b	.L2365
.L2406:
	ldr	w0, [x29,152]
	b	.L2364
.L2389:
	mov	w0, -1
.L2364:
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
	mov	x3, x2
	add	w1, w0, 512
	add	x29, sp, 0
	mov	w2, 1
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
	mov	w1, w0
	mov	x3, x2
	add	x29, sp, 0
	mov	w2, 1
	mov	w0, 2
	bl	zftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	zftl_sys_read, .-zftl_sys_read
	.align	2
	.global	vpn_check
	.type	vpn_check, %function
vpn_check:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x27, [sp,80]
	add	x20, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	bl	timer_get_time
	mov	w21, w0
	bl	dump_ftl_info
	adrp	x23, .LC197
	ldr	x3, [x20,2792]
	adrp	x0, .LC194
	add	x0, x0, :lo12:.LC194
	adrp	x24, .LC198
	mov	w22, 0
	mov	w25, 21
	ldrh	w1, [x3,16]
	add	x23, x23, :lo12:.LC197
	ldrh	w2, [x3,48]
	add	x24, x24, :lo12:.LC198
	ldrh	w3, [x3,80]
	bl	printk
	ldr	x0, [x20,2792]
	ldr	x3, [x20,2832]
	ldrh	w1, [x0,16]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,22]
	adrp	x0, .LC195
	add	x0, x0, :lo12:.LC195
	sub	w4, w3, w2
	bl	printk
	ldr	x0, [x20,2792]
	ldr	x3, [x20,2832]
	adrp	x20, .LANCHOR8
	ldrh	w1, [x0,48]
	ubfiz	x2, x1, 1, 16
	ldrh	w3, [x3,x2]
	ldrh	w2, [x0,54]
	adrp	x0, .LC196
	add	x0, x0, :lo12:.LC196
	sub	w4, w3, w2
	bl	printk
	add	x0, x20, :lo12:.LANCHOR8
	mov	w1, 0
	add	x0, x0, 1112
	mov	w2, 4352
	bl	ftl_memset
.L2410:
	add	x26, x19, :lo12:.LANCHOR0
	ldr	w0, [x26,2784]
	cmp	w22, w0
	bcs	.L2442
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,108]
	cmn	w0, #1
	bne	.L2411
	mov	w0, w22
	add	x1, x29, 108
	mov	w2, 0
	bl	pm_log2phys
	b	.L2412
.L2411:
	ldrh	w1, [x26,3218]
	mov	w2, 1
	lsr	w0, w0, w1
	sub	w1, w25, w1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w0, w0, w1
	ldrb	w1, [x26,3216]
	udiv	w0, w0, w1
	ldrh	w1, [x26,2826]
	cmp	w1, w0, uxth
	bhi	.L2412
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 720
	mov	w2, 1150
	bl	printk
.L2413:
	b	.L2413
.L2412:
	ldr	w2, [x29,108]
	cmn	w2, #1
	beq	.L2414
	add	x26, x19, :lo12:.LANCHOR0
	mov	w0, 1
	ldrh	w4, [x26,3218]
	sub	w3, w25, w4
	lsr	w4, w2, w4
	lsl	w3, w0, w3
	ldrh	w0, [x26,2826]
	sub	w3, w3, #1
	and	w4, w3, w4
	ldrb	w3, [x26,3216]
	udiv	w3, w4, w3
	uxth	w27, w3
	and	w3, w3, 65535
	cmp	w0, w27
	bls	.L2415
	add	x0, x20, :lo12:.LANCHOR8
	add	x0, x0, 1112
	ldrh	w1, [x0,w3,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x0,w3,sxtw 1]
	b	.L2416
.L2415:
	mov	w1, w22
	mov	x0, x23
	bl	printk
	ldrb	w3, [x26,2776]
	mov	x0, x24
	ldr	x1, [x26,3792]
	mov	w2, 4
	lsl	w3, w3, 7
	bl	rknand_print_hex
.L2416:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,2826]
	cmp	w0, w27
	bhi	.L2414
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 720
	mov	w2, 1160
	bl	printk
.L2417:
	b	.L2417
.L2414:
	bl	timer_get_time
	sub	w1, w0, w21
	cmp	w1, 6
	add	w22, w22, 1
	csel	w21, w21, w0, cc
	b	.L2410
.L2442:
	adrp	x23, .LC199
	mov	w22, 0
	add	x24, x20, :lo12:.LANCHOR8
	add	x23, x23, :lo12:.LC199
.L2420:
	add	x4, x19, :lo12:.LANCHOR0
	ldrh	w0, [x4,2826]
	cmp	w0, w22
	bls	.L2443
	ldr	x0, [x4,2832]
	uxtw	x1, w22
	ldrh	w3, [x0,x1,lsl 1]
	add	x0, x24, 1112
	ldrh	w2, [x0,w22,sxtw 1]
	cmp	w3, w2
	beq	.L2421
	ldr	x0, [x4,2840]
	add	x1, x0, x1, lsl 2
	mov	x0, x23
	ldrb	w4, [x1,2]
	mov	w1, w22
	ubfx	x4, x4, 5, 3
	bl	printk
.L2421:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2420
.L2443:
	adrp	x23, .LC200
	mov	w22, 0
	add	x20, x20, :lo12:.LANCHOR8
	add	x23, x23, :lo12:.LC200
.L2423:
	add	x1, x19, :lo12:.LANCHOR0
	ldrh	w0, [x1,2826]
	cmp	w0, w22
	bls	.L2444
	uxtw	x24, w22
	ldr	x0, [x1,2840]
	add	x0, x0, x24, lsl 2
	ldrb	w0, [x0,2]
	tst	w0, 224
	bne	.L2429
	add	x0, x20, 1112
	sxtw	x26, w22
	ldrh	w0, [x0,w22,sxtw 1]
	cbnz	w0, .L2425
	lsl	x25, x24, 1
	ldr	x0, [x1,2832]
	ldrh	w0, [x0,x25]
	cbz	w0, .L2426
	mov	x0, x23
	mov	w1, w22
	bl	printk
.L2426:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	strh	wzr, [x0,x25]
.L2425:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	ldrh	w0, [x0,x24,lsl 1]
	cbz	w0, .L2429
	add	x0, x20, 1112
	ldrh	w0, [x0,x26,lsl 1]
	cbz	w0, .L2429
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 720
	mov	w2, 1192
	bl	printk
.L2427:
	b	.L2427
.L2429:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2423
.L2444:
	bl	timer_get_time
	sub	w2, w0, w21
	adrp	x1, .LANCHOR3
	adrp	x0, .LC201
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC201
	add	x1, x1, 720
	bl	printk
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 112
	ret
	.size	vpn_check, .-vpn_check
	.align	2
	.global	ftl_info_check
	.type	ftl_info_check, %function
ftl_info_check:
	stp	x29, x30, [sp, -112]!
	mov	w1, 1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	add	x19, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x21, .LANCHOR7
	adrp	x27, .LC202
	add	x21, x21, :lo12:.LANCHOR7
	ldrh	w2, [x19,2826]
	add	x0, x21, 3160
	add	x27, x27, :lo12:.LC202
	bl	ftl_memset
	str	x21, [x29,104]
	ldr	x0, [x19,2800]
	mov	w19, 0
	mov	w25, w19
	mov	w26, w19
	mov	w22, w19
	mov	w23, w19
	ldrh	w28, [x0,134]
	mov	w24, w19
.L2446:
	add	x5, x20, :lo12:.LANCHOR0
	ldrh	w0, [x5,2826]
	cmp	w0, w28
	bls	.L2488
	uxtw	x21, w28
	ldr	x4, [x5,2840]
	add	x4, x4, x21, lsl 2
	ldrb	w6, [x4,3]
	cbz	w6, .L2447
	mov	w0, 0
	ldrb	w2, [x5,3212]
	mov	w5, w0
.L2448:
	cmp	w5, w2
	bge	.L2489
	asr	w1, w6, w5
	add	w5, w5, 1
	and	w1, w1, 1
	eor	w1, w1, 1
	add	w0, w0, w1
	b	.L2448
.L2489:
	cbz	w0, .L2451
	ldr	x1, [x29,104]
	sdiv	w0, w2, w0
	add	x1, x1, 3160
	strb	w0, [x1,w28,sxtw]
	b	.L2447
.L2451:
	ldrb	w0, [x4,2]
	mov	w1, -1
	orr	w0, w0, -32
	strb	w0, [x4,2]
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	strh	w1, [x0,x21,lsl 1]
.L2447:
	ldrb	w0, [x4,2]
	and	w1, w0, 224
	cmp	w1, 64
	bne	.L2452
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2487
.L2452:
	cmp	w1, 96
	bne	.L2455
	add	w22, w22, 1
	uxth	w22, w22
.L2487:
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x0, [x1,2832]
	ldrh	w2, [x0,x21,lsl 1]
	cmp	w2, 15
	bls	.L2458
	ldrh	w0, [x1,3234]
	b	.L2484
.L2455:
	cmp	w1, 160
	bne	.L2457
	add	x1, x20, :lo12:.LANCHOR0
	add	w23, w23, 1
	uxth	w23, w23
	ldr	x0, [x1,2832]
	ldrh	w2, [x0,x21,lsl 1]
	cmp	w2, 15
	bls	.L2458
	ldrh	w0, [x1,3210]
.L2484:
	ldrb	w1, [x1,3212]
	mul	w0, w0, w1
	cmp	w2, w0
	bne	.L2454
.L2458:
	mov	w0, w28
	mov	w1, 1
	mov	w2, 0
	bl	gc_add_sblk
	b	.L2454
.L2457:
	cbnz	w1, .L2454
	ands	w0, w0, 24
	bne	.L2459
	add	w26, w26, 1
	uxth	w26, w26
	b	.L2460
.L2459:
	cmp	w0, 16
	bne	.L2461
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2460
.L2461:
	add	w19, w19, 1
	uxth	w19, w19
.L2460:
	add	x0, x20, :lo12:.LANCHOR0
	lsl	x21, x21, 1
	ldr	x0, [x0,2832]
	ldrh	w2, [x0,x21]
	cbz	w2, .L2462
	mov	x0, x27
	mov	w1, w28
	str	x4, [x29,96]
	bl	printk
	ldr	x4, [x29,96]
	ldrb	w0, [x4,2]
	tbz	x0, 4, .L2463
	mov	w1, 5
	b	.L2482
.L2463:
	mov	w1, 2
.L2482:
	bfi	w0, w1, 5, 3
	mov	w2, 0
	strb	w0, [x4,2]
	mov	w1, 1
	mov	w0, w28
	bl	gc_add_sblk
	bl	vpn_check
.L2462:
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	ldrh	w0, [x0,x21]
	cbz	w0, .L2454
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 736
	mov	w2, 1267
	bl	printk
.L2465:
	b	.L2465
.L2454:
	add	w3, w28, 1
	uxth	w28, w3
	b	.L2446
.L2488:
	adrp	x0, .LC203
	mov	w1, w24
	mov	w2, w23
	mov	w3, w22
	add	x0, x0, :lo12:.LC203
	str	x5, [x29,104]
	bl	printk
	adrp	x0, .LC204
	mov	w1, w26
	add	x0, x0, :lo12:.LC204
	mov	w2, w25
	mov	w3, w19
	bl	printk
	ldr	x5, [x29,104]
	ldr	x0, [x5,2800]
	ldrh	w1, [x0,114]
	cmp	w1, w26
	beq	.L2467
	strh	w26, [x0,114]
.L2467:
	ldrh	w1, [x0,118]
	cmp	w1, w25
	beq	.L2468
	strh	w25, [x0,118]
.L2468:
	ldrh	w1, [x0,116]
	cmp	w1, w19
	beq	.L2469
	strh	w19, [x0,116]
.L2469:
	ldrh	w1, [x0,122]
	cmp	w1, w24
	beq	.L2470
	strh	w24, [x0,122]
.L2470:
	ldrh	w1, [x0,120]
	cmp	w1, w23
	beq	.L2471
	strh	w23, [x0,120]
.L2471:
	ldrh	w1, [x0,124]
	cmp	w1, w22
	beq	.L2445
	strh	w22, [x0,124]
.L2445:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	ftl_info_check, .-ftl_info_check
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
	ldrh	w1, [x0,2848]
	cmp	w1, w2
	bne	.L2491
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 752
	mov	w2, 883
	bl	printk
.L2492:
	b	.L2492
.L2491:
	adrp	x2, .LANCHOR2+400
	ldr	w2, [x2,#:lo12:.LANCHOR2+400]
	tbz	x2, 8, .L2493
	ldr	x3, [x0,2832]
	ubfiz	x2, x1, 1, 16
	adrp	x0, .LC205
	add	x0, x0, :lo12:.LC205
	ldrh	w2, [x3,x2]
	bl	printk
.L2493:
	add	x20, x19, :lo12:.LANCHOR0
	bl	timer_get_time
	add	x21, x20, 2848
	add	x1, x20, 2888
	ldrh	w0, [x20,2848]
	bl	ftl_get_blk_list_in_sblk
	uxtb	w1, w0
	strb	w1, [x21,5]
	cbnz	w1, .L2494
	mov	w0, -1
	strh	w0, [x20,2848]
	b	.L2522
.L2494:
	ldrh	w1, [x20,2848]
	ldr	x2, [x20,2840]
	add	x2, x2, x1, lsl 2
	ldrb	w4, [x2,2]
	tst	w4, 192
	and	w2, w4, 224
	beq	.L2496
	cmp	w2, 224
	beq	.L2496
	ldr	x4, [x20,2792]
	ldrh	w5, [x4,16]
	cmp	w5, w1
	beq	.L2496
	ldrh	w5, [x4,48]
	cmp	w5, w1
	beq	.L2496
	ldrh	w4, [x4,80]
	cmp	w4, w1
	bne	.L2497
.L2496:
	cbnz	w2, .L2498
	add	x3, x19, :lo12:.LANCHOR0
	lsl	x0, x1, 1
	ldr	x1, [x3,2832]
	ldrh	w3, [x1,x0]
	cbz	w3, .L2499
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 752
	mov	w2, 903
	bl	printk
.L2500:
	b	.L2500
.L2499:
	strh	w2, [x1,x0]
.L2498:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	wzr, [x19,2872]
	strh	w0, [x19,2848]
.L2522:
	mov	w0, 0
	b	.L2495
.L2497:
	and	w0, w0, 255
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w1, [x0,40]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2501
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 752
	mov	w2, 911
	bl	printk
.L2502:
	b	.L2502
.L2501:
	ldrh	w0, [x20,3234]
	cmp	w2, 160
	ldrh	w3, [x20,3230]
	mov	w24, 1
	sub	w0, w0, #1
	mul	w1, w1, w3
	uxth	w0, w0
	bne	.L2503
	ldrb	w2, [x20,3193]
	cmp	w2, 2
	uxth	w24, w2
	orr	w1, w1, w2, lsl 24
	bne	.L2503
	ldrh	w0, [x20,3210]
	sub	w0, w0, #1
	uxth	w0, w0
.L2503:
	add	x20, x19, :lo12:.LANCHOR0
	orr	w23, w0, w1
	mov	w0, 1
	strb	w24, [x20,2854]
	strh	wzr, [x20,2850]
	strb	wzr, [x20,2852]
	strh	wzr, [x20,2874]
	bl	buf_alloc
	mov	w1, 1
	str	w23, [x0,40]
	mov	x21, x0
	bl	sblk_read_page
	ldrh	w0, [x20,3210]
	mov	w1, 255
	ldrb	w2, [x20,3212]
	mul	w2, w0, w2
	ldr	x0, [x20,3184]
	lsl	w2, w2, 2
	bl	ftl_memset
	strh	wzr, [x20,2872]
	ldr	w0, [x21,52]
	cmp	w0, 512
	beq	.L2507
	cmn	w0, #1
	beq	.L2507
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2506
	ldrh	w0, [x20,2848]
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	x0, [x21,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2507
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 752
	mov	w2, 941
	bl	printk
.L2508:
	b	.L2508
.L2507:
	mov	x0, x21
	bl	buf_free
	mov	w0, -1
	b	.L2495
.L2506:
	ldrb	w0, [x20,3212]
	mov	x26, 0
	ldrh	w22, [x20,3234]
	mov	w25, 1
	ldr	x23, [x21,8]
	mul	w22, w0, w22
	mul	w22, w24, w22
	mov	w24, 21
	uxth	w22, w22
.L2509:
	cmp	w22, w26
	ble	.L2523
	ldr	w0, [x23,x26,lsl 2]
	cmn	w0, #1
	beq	.L2511
	bl	lpa_hash_get_ppa
	str	w0, [x29,92]
	cmn	w0, #1
	bne	.L2512
	ldr	w0, [x23,x26,lsl 2]
	add	x1, x29, 92
	mov	w2, 0
	bl	pm_log2phys
.L2512:
	ldrh	w0, [x20,3218]
	ldr	w2, [x29,92]
	sub	w1, w24, w0
	ldrh	w3, [x20,2848]
	lsl	w1, w25, w1
	lsr	w0, w2, w0
	sub	w1, w1, #1
	and	w0, w1, w0
	ldrb	w1, [x20,3216]
	udiv	w0, w0, w1
	cmp	w0, w3
	bne	.L2511
	ldrh	w3, [x20,2872]
	ldr	x0, [x20,3184]
	str	w2, [x0,x3,lsl 2]
	ldrh	w0, [x20,2872]
	add	w0, w0, 1
	strh	w0, [x20,2872]
.L2511:
	bl	timer_get_time
	add	x26, x26, 1
	b	.L2509
.L2523:
	mov	x0, x21
	bl	buf_free
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,2848]
	ldr	x3, [x0,2832]
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x3,x2]
	ldrh	w3, [x0,2872]
	cmp	w2, w3
	beq	.L2515
	adrp	x0, .LC206
	add	x0, x0, :lo12:.LC206
	bl	printk
.L2515:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x0,2848]
	ldr	x1, [x0,2832]
	ldrh	w0, [x0,2872]
	ldrh	w1, [x1,x2,lsl 1]
	cmp	w1, w0
	bcs	.L2516
	bl	vpn_check
.L2516:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w2, [x19,2848]
	ldr	x1, [x19,2832]
	ldrh	w3, [x19,2872]
	strh	w3, [x1,x2,lsl 1]
	strh	wzr, [x19,2876]
	ldrh	w0, [x19,2872]
.L2495:
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
	adrp	x24, .LC208
	mov	x19, x0
	adrp	x25, .LC209
	adrp	x0, .LC207
	mov	w1, 0
	add	x0, x0, :lo12:.LC207
	mov	w20, 0
	add	x24, x24, :lo12:.LC208
	add	x27, x25, :lo12:.LC209
	bl	printk
.L2525:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w0, [x0,2784]
	cmp	w20, w0
	bcs	.L2548
	mov	w0, -1
	str	w0, [x19,36]
	and	w0, w20, 4095
	cbnz	w0, .L2526
	ldr	w2, [x29,124]
	mov	x0, x24
	mov	w1, w20
	bl	printk
.L2526:
	mov	w0, w20
	bl	lpa_hash_get_ppa
	str	w0, [x29,124]
	cmn	w0, #1
	bne	.L2527
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2527:
	ldr	w0, [x29,124]
	str	w0, [x19,40]
	str	w0, [x19,44]
	cmn	w0, #1
	beq	.L2529
	mov	x0, x19
	mov	w1, 1
	bl	sblk_read_page
	ldr	w3, [x19,52]
	cmn	w3, #1
	beq	.L2530
	cmp	w3, 256
	beq	.L2530
	ldr	x0, [x19,24]
	ldr	w0, [x0,4]
	cmp	w0, w20
	beq	.L2529
.L2530:
	ldr	x0, [x19,8]
	add	x23, x25, :lo12:.LC209
	ldr	x7, [x19,24]
	adrp	x26, .LC210
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
	bne	.L2532
.L2534:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w0, w20
	bl	lpa_hash_get_ppa
	mov	w4, w0
	ldrb	w22, [x21,2776]
	add	x0, x26, :lo12:.LC210
	mov	w1, w20
	adrp	x26, .LC212
	lsl	w28, w22, 7
	lsl	w3, w22, 7
	sub	w3, w3, #1
	mov	x22, 0
	sdiv	w28, w20, w28
	and	w3, w20, w3
	add	x21, x21, 3784
	add	x26, x26, :lo12:.LC212
	uxth	w3, w3
	uxth	w23, w28
	mov	w2, w23
	bl	printk
	mov	w0, w20
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
	b	.L2533
.L2532:
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w1, [x1,2784]
	cmp	w22, w1
	bcs	.L2534
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
	bne	.L2535
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	bl	pm_log2phys
.L2535:
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
	add	x0, x26, :lo12:.LC210
	uxth	w3, w3
	bl	printk
	mov	w0, w22
	add	x1, x29, 124
	mov	w2, 0
	mov	x22, 0
	bl	pm_log2phys
	ldr	x5, [x29,96]
	adrp	x4, .LC211
	add	x4, x4, :lo12:.LC211
	add	x23, x5, 3784
.L2537:
	add	x0, x23, x22
	ldr	x1, [x0,8]
	cbz	x1, .L2536
	ldrh	w0, [x22,x23]
	cmp	w0, w28
	bne	.L2536
	mov	x0, x4
	mov	w2, 4
	mov	w3, 4096
	str	x4, [x29,104]
	bl	rknand_print_hex
	ldr	x4, [x29,104]
.L2536:
	add	x22, x22, 16
	cmp	x22, 512
	bne	.L2537
	b	.L2534
.L2549:
	ldrh	w0, [x22,x21]
	cmp	w0, w23
	bne	.L2538
	mov	x0, x26
	mov	w2, 4
	mov	w3, 4096
	bl	rknand_print_hex
.L2538:
	add	x22, x22, 16
	cmp	x22, 512
	beq	.L2529
.L2533:
	add	x0, x21, x22
	ldr	x1, [x0,8]
	cbnz	x1, .L2549
	b	.L2538
.L2529:
	add	w20, w20, 1
	b	.L2525
.L2548:
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
	strb	wzr, [x24,3209]
	add	x0, x22, :lo12:.LANCHOR4
	ldr	x20, [x24,2792]
	add	x21, x20, 80
	strb	wzr, [x0,1010]
	bl	gc_init
	ldrh	w1, [x20,80]
	mov	w0, 65535
	str	x22, [x29,152]
	cmp	w1, w0
	ldr	x0, [x24,2792]
	beq	.L2551
	mov	w1, -1
	strh	w1, [x0,130]
	mov	w0, 1
	bl	buf_alloc
	mov	x23, x0
	ldrb	w0, [x21,9]
	ldrh	w1, [x24,3230]
	sub	w0, w0, #1
	add	x0, x21, x0, sxtw 1
	ldrh	w0, [x0,16]
	mul	w0, w0, w1
	ldrb	w1, [x24,3193]
	str	w0, [x29,168]
	cmp	w1, 3
	bne	.L2552
	ldrh	w1, [x24,3234]
	sub	w1, w1, #1
	add	w0, w1, w0
	orr	w0, w0, 50331648
	b	.L2598
.L2552:
	cmp	w1, 2
	bne	.L2553
	ldrh	w1, [x24,3234]
	add	w0, w0, w1, lsl 1
	sub	w0, w0, #1
	orr	w0, w0, 33554432
.L2598:
	str	w0, [x23,40]
.L2553:
	mov	x0, x23
	mov	w1, 1
	bl	sblk_read_page
	ldr	w0, [x23,52]
	cmp	w0, 512
	beq	.L2554
	cmn	w0, #1
	beq	.L2554
	ldr	x0, [x23,24]
	ldr	w1, [x0]
	mov	w0, 15555
	movk	w0, 0xf55f, lsl 16
	cmp	w1, w0
	beq	.L2555
.L2554:
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
	adrp	x0, .LC213
	add	x0, x0, :lo12:.LC213
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x23,40]
	ldr	w2, [x23,52]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2601
.L2555:
	add	x24, x19, :lo12:.LANCHOR0
	ldr	x0, [x29,152]
	ldr	x1, [x23,8]
	add	x25, x0, :lo12:.LANCHOR4
	ldrb	w2, [x24,3212]
	ldrh	w0, [x24,3210]
	mul	w2, w0, w2
	ldr	x0, [x25,1016]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x24,3210]
	ldrb	w2, [x24,3212]
	ldr	x1, [x23,8]
	mul	w2, w0, w2
	add	x1, x1, x2, sxtw 2
	ldr	x0, [x25,1024]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w4, [x24,3210]
	adrp	x0, .LC214
	ldrb	w3, [x24,3212]
	add	x0, x0, :lo12:.LC214
	ldr	x1, [x25,1016]
	mov	w2, 4
	mul	w3, w4, w3
	bl	rknand_print_hex
	ldrh	w4, [x24,3210]
	adrp	x0, .LC215
	ldrb	w3, [x24,3212]
	add	x0, x0, :lo12:.LC215
	ldr	x1, [x25,1024]
	mov	w2, 4
	mul	w3, w4, w3
	bl	rknand_print_hex
	ldrb	w27, [x24,3193]
	ldrh	w26, [x24,3234]
	cmp	w27, 2
	bne	.L2557
	lsl	w26, w26, 1
	mov	w27, 1
.L2557:
	mov	w22, 0
	adrp	x24, .LC216
	add	x0, x24, :lo12:.LC216
	mov	w25, w22
	str	x0, [x29,128]
	b	.L2558
.L2603:
	ldr	x0, [x0,1024]
	ldr	w1, [x0,x12]
	ldr	w0, [x7,8]
	cmp	w1, w0
	bne	.L2562
.L2587:
	add	w25, w25, 1
	add	w28, w28, 1
.L2566:
	cmp	w28, w27
	bhi	.L2602
	ldr	x0, [x29,144]
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x0, 8
	ldrh	w2, [x1,3230]
	ldrb	w1, [x1,3193]
	ldrh	w0, [x21,x0,lsl 1]
	cmp	w1, 3
	mul	w0, w0, w2
	str	w0, [x29,168]
	bne	.L2559
	add	w0, w0, w22
	orr	w0, w0, w28, lsl 24
.L2599:
	str	w0, [x23,40]
	mov	w1, 1
	mov	x0, x23
	bl	sblk_read_page
	ldr	x0, [x29,152]
	sbfiz	x12, x25, 2, 32
	ldr	x7, [x23,24]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w6, [x7,4]
	ldr	x1, [x0,1016]
	ldr	w3, [x1,x12]
	cmp	w3, w6
	beq	.L2603
.L2562:
	ldr	x0, [x29,152]
	str	x12, [x29,120]
	add	x13, x0, :lo12:.LANCHOR4
	ldr	w0, [x7,12]
	str	x13, [x29,112]
	ldr	x4, [x13,1024]
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
	ldr	x0, [x13,1016]
	ldr	w0, [x0,x12]
	cmn	w0, #1
	beq	.L2587
	mov	x0, x23
	bl	buf_free
.L2601:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w2, [x20,80]
	mov	w28, 0
	ldr	x1, [x0,2832]
	strh	wzr, [x1,x2,lsl 1]
	ldr	x0, [x0,2792]
	ldrh	w1, [x20,80]
	strh	w1, [x0,130]
	b	.L2556
.L2565:
	add	w22, w22, 1
.L2558:
	cmp	w22, w26
	beq	.L2567
	sub	w0, w22, #1
	mov	w24, 0
	str	w0, [x29,140]
.L2568:
	ldrb	w0, [x21,9]
	cmp	w24, w0
	bge	.L2565
	sxtw	x0, w24
	mov	w28, 1
	str	x0, [x29,144]
	b	.L2566
.L2559:
	cmp	w1, 2
	bne	.L2561
	ldr	w1, [x29,140]
	add	w0, w0, w1
	add	w0, w0, w28
	orr	w0, w0, 33554432
	b	.L2599
.L2561:
	add	w0, w0, w22
	b	.L2599
.L2602:
	add	w24, w24, 1
	b	.L2568
.L2567:
	mov	x0, x23
	mov	w28, 0
	mov	w23, w28
	bl	buf_free
	str	w28, [x29,144]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,9]
	ldrh	w3, [x20,80]
	ldr	x2, [x0,2832]
	ldrh	w0, [x0,3210]
	mul	w0, w1, w0
	strh	w0, [x2,x3,lsl 1]
	adrp	x0, .LC217
	add	x0, x0, :lo12:.LC217
	str	x0, [x29,104]
.L2569:
	cmp	w23, w26
	beq	.L2582
	sub	w0, w23, #1
	mov	w25, 0
	str	w0, [x29,112]
.L2583:
	ldrb	w0, [x21,9]
	cmp	w25, w0
	bge	.L2580
	sxtw	x0, w25
	mov	w24, 1
	str	x0, [x29,120]
.L2581:
	cmp	w24, w27
	bhi	.L2604
	ldr	x1, [x29,152]
	ldr	w0, [x29,144]
	add	x1, x1, :lo12:.LANCHOR4
	sbfiz	x0, x0, 2, 32
	ldr	x2, [x1,1016]
	ldr	w22, [x2,x0]
	cmn	w22, #1
	beq	.L2570
	ldr	x1, [x1,1024]
	ldr	w0, [x1,x0]
	str	w0, [x29,140]
	mov	w0, w22
	bl	lpa_hash_get_ppa
	str	w0, [x29,172]
	cmn	w0, #1
	bne	.L2571
	mov	w0, w22
	add	x1, x29, 172
	mov	w2, 0
	bl	pm_log2phys
.L2571:
	ldr	x0, [x29,120]
	add	x0, x0, 8
	ldrh	w3, [x21,x0,lsl 1]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,3230]
	ldrb	w0, [x0,3193]
	cmp	w0, 3
	mul	w3, w3, w1
	bne	.L2572
	add	w3, w3, w23
	orr	w3, w3, w24, lsl 24
	b	.L2600
.L2572:
	cmp	w0, 2
	bne	.L2574
	ldr	w0, [x29,112]
	add	w3, w3, w0
	add	w3, w3, w24
	orr	w3, w3, 33554432
	b	.L2600
.L2574:
	add	w3, w3, w23
.L2600:
	add	x0, x19, :lo12:.LANCHOR0
	str	w3, [x29,168]
	ldr	w1, [x29,140]
	mov	w4, 21
	mov	w2, 1
	ldrh	w3, [x0,3218]
	ldrb	w0, [x0,3216]
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
	bne	.L2575
	ldr	x0, [x29,104]
	mov	w1, w22
	ldr	w2, [x29,168]
	bl	pm_ppa_update_check
	cbz	w0, .L2576
.L2597:
	b	.L2597
.L2576:
	mov	w0, w22
	add	x1, x29, 168
	mov	w2, 1
	add	w28, w28, 1
	bl	pm_log2phys
	ldrh	w0, [x29,128]
	bl	ftl_vpn_decrement
	b	.L2578
.L2575:
	ldr	w1, [x29,168]
	cmp	w0, w1
	csinc	w28, w28, w28, ne
.L2578:
	ldr	w0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR0
	add	w0, w0, 1
	str	w0, [x29,144]
	ldr	w0, [x29,128]
	ldr	x1, [x2,2840]
	add	x1, x1, x0, lsl 2
	ldrb	w1, [x1,2]
	ands	w1, w1, 224
	bne	.L2570
	lsl	x0, x0, 1
	ldr	x2, [x2,2832]
	ldrh	w3, [x2,x0]
	cbz	w3, .L2570
	strh	w1, [x2,x0]
.L2570:
	add	w24, w24, 1
	b	.L2581
.L2604:
	add	w25, w25, 1
	b	.L2583
.L2580:
	add	w23, w23, 1
	b	.L2569
.L2582:
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x20,80]
	ldr	x0, [x0,2832]
	strh	w28, [x0,x1,lsl 1]
.L2556:
	ldrh	w1, [x20,80]
	adrp	x0, .LC218
	mov	w2, w28
	add	x0, x0, :lo12:.LC218
	bl	printk
	mov	w0, -1
	strh	w0, [x20,80]
	bl	pm_flush
	bl	ftl_ext_info_flush
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	ldr	x0, [x0,2792]
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2584
	bl	ftl_free_sblk
.L2584:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	ldr	x1, [x19,2800]
	strh	w0, [x1,126]
	ldr	x1, [x19,2792]
	strh	w0, [x1,130]
	mov	w0, 0
	bl	ftl_info_flush
	b	.L2550
.L2551:
	ldrh	w0, [x0,130]
	cmp	w0, w1
	beq	.L2550
	ldr	x1, [x24,2800]
	ldrh	w1, [x1,126]
	cmp	w1, w0
	bne	.L2586
	bl	pm_flush
	ldr	x0, [x24,2792]
	ldrh	w0, [x0,130]
	bl	ftl_free_sblk
	ldr	x0, [x24,2800]
	mov	w1, -1
	strh	w1, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
.L2586:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, -1
	ldr	x0, [x19,2792]
	strh	w1, [x0,130]
.L2550:
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
	adrp	x1, .LANCHOR5+384
	mov	w0, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	str	x23, [sp,48]
	add	x20, x19, :lo12:.LANCHOR0
	strb	w0, [x1,#:lo12:.LANCHOR5+384]
	stp	x21, x22, [sp,32]
	adrp	x1, .LC47
	strb	w0, [x20,2808]
	mov	w0, -1
	str	w0, [x20,2812]
	adrp	x0, .LC46
	add	x1, x1, :lo12:.LC47
	add	x0, x0, :lo12:.LC46
	strb	wzr, [x20,2809]
	strb	wzr, [x20,3772]
	bl	printk
	adrp	x1, .LANCHOR2
	ldrb	w4, [x20,57]
	add	x1, x1, :lo12:.LANCHOR2
	adrp	x8, .LANCHOR7
	strb	w4, [x20,3232]
	ldrh	w5, [x20,170]
	ldrb	w3, [x1,381]
	ldrb	w7, [x1,380]
	ldrh	w2, [x1,378]
	strb	w3, [x20,3216]
	mul	w3, w3, w4
	add	x4, x8, :lo12:.LANCHOR7
	ldrb	w0, [x1,377]
	ldrh	w9, [x1,382]
	mov	w1, 0
	sdiv	w6, w2, w7
	strb	w7, [x20,3193]
	strb	w0, [x20,2776]
	uxtb	w3, w3
	strh	w2, [x20,3210]
	ubfiz	w11, w0, 9, 7
	strh	w6, [x20,3234]
	strb	w3, [x20,3212]
	strh	w9, [x20,2826]
	strh	w11, [x4,3154]
	mov	w4, 1
	strh	w5, [x20,3230]
	mov	x20, x8
.L2606:
	cmp	w4, w5
	bhi	.L2627
	add	w1, w1, 1
	lsl	w4, w4, 1
	uxth	w1, w1
	b	.L2606
.L2627:
	add	x8, x19, :lo12:.LANCHOR0
	sub	w1, w1, #1
	mul	w4, w0, w2
	mul	w6, w6, w3
	strh	w1, [x8,3218]
	mov	w1, 57344
	movk	w1, 0x1c, lsl 16
	adrp	x21, .LANCHOR9
	mul	w2, w4, w9
	mul	w1, w3, w1
	lsr	w5, w2, 21
	add	x10, x21, :lo12:.LANCHOR9
	mul	w2, w2, w3
	mul	w1, w1, w5
	str	w2, [x8,2780]
	cmp	w7, 2
	add	w5, w1, 24576
	str	w1, [x8,4]
	mul	w3, w4, w3
	udiv	w2, w5, w0
	sub	w0, w11, #1
	str	w2, [x8,2784]
	str	w5, [x10,1112]
	add	w0, w0, w2, lsl 2
	add	x2, x20, :lo12:.LANCHOR7
	udiv	w3, w5, w3
	add	w3, w3, 2
	strh	w3, [x8,3224]
	udiv	w0, w0, w11
	strh	w0, [x2,3138]
	adrp	x2, .LANCHOR4
	ubfiz	w0, w0, 4, 16
	add	x11, x2, :lo12:.LANCHOR4
	mov	x22, x2
	sdiv	w0, w0, w6
	strh	w0, [x11,1040]
	add	w0, w9, w9, lsl 1
	asr	w0, w0, 2
	strh	w0, [x8,3226]
	lsr	w0, w9, 4
	strh	w0, [x10,1116]
	mov	w0, 4000
	strh	w0, [x8,3220]
	mov	w0, 50
	strh	w0, [x8,3222]
	beq	.L2608
	ldrb	w0, [x8,3208]
	cbz	w0, .L2609
.L2608:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 150
	strh	w2, [x0,3222]
.L2609:
	adrp	x0, .LC219
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x0, :lo12:.LC219
	add	x21, x21, :lo12:.LANCHOR9
	add	x20, x20, :lo12:.LANCHOR7
	adrp	x23, .LANCHOR6
	bl	printk
	add	x23, x23, :lo12:.LANCHOR6
	ldr	w1, [x19,2780]
	adrp	x0, .LC220
	add	x0, x0, :lo12:.LC220
	bl	printk
	ldr	w1, [x19,2784]
	adrp	x0, .LC221
	add	x0, x0, :lo12:.LC221
	bl	printk
	ldr	w1, [x21,1112]
	adrp	x0, .LC222
	add	x0, x0, :lo12:.LC222
	bl	printk
	ldrh	w1, [x20,3138]
	adrp	x0, .LC223
	add	x0, x0, :lo12:.LC223
	bl	printk
	ldrh	w1, [x20,3154]
	adrp	x0, .LC224
	add	x0, x0, :lo12:.LC224
	bl	printk
	add	x2, x22, :lo12:.LANCHOR4
	adrp	x0, .LC225
	add	x0, x0, :lo12:.LC225
	ldrh	w1, [x2,1040]
	bl	printk
	ldrh	w1, [x21,1116]
	adrp	x0, .LC226
	add	x0, x0, :lo12:.LC226
	bl	printk
	ldrh	w1, [x19,3224]
	adrp	x0, .LC227
	add	x0, x0, :lo12:.LC227
	bl	printk
	bl	buf_init
	ldrb	w1, [x19,3212]
	ldrh	w0, [x19,3234]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,3760]
	ldrb	w1, [x19,3212]
	sub	x2, x23, #192
	ldrh	w0, [x19,3234]
	str	x2, [x19,3752]
	mul	w1, w0, w1
	ldrh	w0, [x19,2826]
	lsl	w1, w1, 1
	add	x3, x2, x1, sxtw 2
	str	x3, [x19,2832]
	add	w1, w1, w0, lsr 1
	adrp	x0, .LC228
	add	x0, x0, :lo12:.LC228
	add	x1, x2, x1, sxtw 2
	str	x1, [x19,2792]
	bl	printk
	ldrb	w21, [x19,3212]
	ldrh	w0, [x19,3234]
	ldrh	w1, [x19,2826]
	ldrh	w22, [x20,3138]
	mul	w21, w0, w21
	lsl	w22, w22, 2
	adrp	x0, .LC229
	add	w22, w22, w1, lsl 2
	ldrh	w3, [x20,3154]
	add	w21, w1, w21, lsl 2
	add	w22, w22, 704
	lsl	w21, w21, 1
	add	x0, x0, :lo12:.LC229
	add	w21, w21, 612
	mov	w2, w22
	mov	w1, w21
	bl	printk
	ldrh	w0, [x20,3154]
	cmp	w21, w0
	bhi	.L2611
	cmp	w22, w0
	bls	.L2610
.L2611:
.L2612:
	b	.L2612
.L2610:
	bl	sblk_init
	bl	ftl_info_blk_init
	cmn	w0, #1
	beq	.L2613
	bl	ftl_ext_info_init
	mov	w0, 1
	bl	pm_init
	bl	lpa_rebuild_hash
	ldr	x0, [x19,2792]
	mov	x1, 0
	add	x0, x0, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,2792]
	add	x0, x1, 48
	add	x1, x1, 16
	bl	ftl_open_sblk_recovery
	ldr	x1, [x19,2800]
	ldr	w0, [x1,8]
	add	w0, w0, 16
	str	w0, [x1,8]
	ldr	x0, [x19,2792]
	add	x0, x0, 16
	bl	ftl_info_data_recovery
	ldr	x0, [x19,2792]
	add	x0, x0, 48
	bl	ftl_info_data_recovery
	ldr	x0, [x19,2792]
	add	x0, x0, 80
	bl	ftl_info_data_recovery
	bl	gc_recovery
	bl	ftl_info_check
	bl	pm_flush
	mov	w0, 1
	bl	ftl_total_vpn_update
	ldrb	w0, [x23,-256]
	cbz	w0, .L2614
	ldr	x1, [x19,2800]
	ldr	w0, [x1,68]
	add	w0, w0, 1
	str	w0, [x1,68]
.L2614:
	bl	ftl_ext_info_flush
	mov	w0, 0
	bl	ftl_info_flush
	bl	print_ftl_debug_info
	mov	w0, 0
.L2613:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	zftl_init, .-zftl_init
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	stp	x29, x30, [sp, -32]!
	adrp	x0, jiffies
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	ldr	x0, [x0,#:lo12:jiffies]
	add	x19, x19, :lo12:.LANCHOR4
	str	x0, [x19,1048]
	mov	w0, 136
	bl	ftl_malloc
	str	x0, [x19,1056]
	cbnz	x0, .L2629
.L2631:
	mov	w0, -1
	b	.L2630
.L2629:
	adrp	x20, .LANCHOR9
	mov	w0, 2048
	add	x20, x20, :lo12:.LANCHOR9
	bl	ftl_malloc
	str	xzr, [x19,1064]
	add	x1, x20, 1128
	str	x0, [x20,1120]
	add	x0, x19, 1064
	str	xzr, [x20,1128]
	bl	rknand_get_reg_addr
	ldr	x0, [x19,1064]
	cbz	x0, .L2631
	bl	rk_nandc_irq_init
	mov	w1, 0
	ldr	x0, [x20,1120]
	mov	w2, w1
	mov	w3, 2048
	bl	flash_sram_load_store
	bl	rknand_flash_cs_init
	ldr	x0, [x19,1056]
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
	ldr	x0, [x19,1064]
	bl	nand_flash_init
	cbnz	w0, .L2632
	bl	zftl_init
	b	.L2637
.L2632:
	ldr	x0, [x19,1056]
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
	ldr	x0, [x19,1064]
	bl	FlashInit
	mov	w20, w0
	adrp	x0, .LANCHOR2
	adrp	x1, gNandParaInfo
	add	x0, x0, :lo12:.LANCHOR2
	add	x1, x1, :lo12:gNandParaInfo
	add	x0, x0, 368
	mov	w2, 32
	bl	ftl_memcpy
	strb	wzr, [x19,1136]
	adrp	x0, .LANCHOR0+58
	strb	wzr, [x0,#:lo12:.LANCHOR0+58]
	cbnz	w20, .L2633
	bl	FtlInit
.L2637:
	mov	w20, w0
.L2633:
	adrp	x0, .LC230
	mov	w1, w20
	add	x0, x0, :lo12:.LC230
	bl	printk
	mov	w0, w20
.L2630:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	rk_ftl_init, .-rk_ftl_init
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
	ldrh	w24, [x0,3234]
	mov	w22, w27
	ldr	x0, [x0,3752]
	ldrh	w20, [x23,12]
	mul	w24, w24, w1
	add	x20, x0, x20, lsl 2
	add	x0, x20, x24, sxtw 2
	ldr	w0, [x0,-4]
	cmn	w0, #1
	beq	.L2639
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 768
	mov	w2, 889
	bl	printk
.L2640:
	b	.L2640
.L2639:
	adrp	x25, .LC232
	adrp	x26, .LC231
	add	x0, x25, :lo12:.LC232
	str	x0, [x29,120]
	add	x0, x26, :lo12:.LC231
	str	x0, [x29,112]
.L2663:
	cmp	w27, w24
	mov	w25, w27
	bge	.L2664
	ldr	w2, [x20,x27,lsl 2]
	cmn	w2, #1
	beq	.L2641
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w21, [x0,2776]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	lsl	w21, w21, 7
	ldr	w0, [x0,400]
	udiv	w21, w2, w21
	and	w21, w21, 65535
	tbz	x0, 12, .L2642
	ldr	x0, [x29,112]
	mov	w1, w21
	mov	w3, w27
	bl	printk
.L2642:
	mov	w28, -1
.L2646:
	sbfiz	x26, x25, 2, 32
	ldr	w0, [x20,x26]
	cmn	w0, #1
	beq	.L2643
	add	x6, x19, :lo12:.LANCHOR0
	ldrb	w1, [x6,2776]
	lsl	w1, w1, 7
	udiv	w0, w0, w1
	cmp	w21, w0, uxth
	bne	.L2643
	ldrb	w0, [x23,9]
	ldrh	w2, [x6,3230]
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
	cbz	w0, .L2644
	adrp	x0, .LC233
	ldr	x1, [x6,3752]
	add	x0, x0, :lo12:.LC233
	mov	w2, 4
	mov	w3, w24
	bl	rknand_print_hex
.L2645:
	b	.L2645
.L2644:
	ldr	w0, [x20,x26]
	add	x1, x29, 140
	mov	w2, 1
	add	w22, w22, 1
	bl	pm_log2phys
	uxth	w22, w22
	str	w28, [x20,x26]
.L2643:
	add	w25, w25, 1
	cmp	w25, w24
	bne	.L2646
.L2641:
	add	x27, x27, 1
	b	.L2663
.L2664:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L2648
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	mov	w2, w22
	ubfiz	x3, x1, 1, 16
	ldr	x4, [x0,2832]
	adrp	x0, .LC234
	add	x0, x0, :lo12:.LC234
	ldrh	w3, [x4,x3]
	bl	printk
.L2648:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w1, [x23]
	ldr	x0, [x19,2832]
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
	adrp	x0, .LANCHOR0+2792
	mov	w2, 2
	ldr	x1, [x0,#:lo12:.LANCHOR0+2792]
	mov	x0, x19
	add	x1, x1, 16
	cmp	x19, x1
	mov	w1, 3
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
	add	x0, x0, 384
	str	x0, [x29,104]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
.L2669:
	ldrb	w0, [x24,2809]
	adrp	x21, .LANCHOR0
	cbz	w0, .L2671
	ldrb	w1, [x24,2808]
	sub	w0, w0, #1
	ldr	x2, [x29,128]
	str	x1, [x29,112]
	lsl	x1, x1, 6
	add	x26, x2, x1
	strb	w0, [x24,2809]
	ldr	w0, [x24,2784]
	ldrb	w1, [x2,x1]
	ldr	w23, [x26,36]
	strb	w1, [x24,2808]
	cmp	w23, w0
	bcc	.L2672
	mov	x0, x26
	bl	buf_free
	mov	w0, -1
	b	.L2726
.L2672:
	ldr	x0, [x26,24]
	str	x0, [x29,136]
	ldrb	w0, [x24,3772]
	ldr	x27, [x26,8]
	ldrb	w22, [x26,57]
	ldrb	w20, [x26,56]
	cbz	w0, .L2674
	ldr	x0, [x29,120]
	add	x1, x21, :lo12:.LANCHOR0
	add	x1, x1, 728
	ldrb	w3, [x0,384]
	ldr	x0, [x29,128]
	add	x3, x0, x3, lsl 6
.L2675:
	ldrb	w0, [x3]
	cmp	w0, 255
	beq	.L2729
	sbfiz	x3, x0, 6, 32
	add	x3, x1, x3
	b	.L2675
.L2729:
	ldr	w0, [x3,36]
	cmp	w0, w23
	bne	.L2674
	ldr	x0, [x3,8]
	ubfiz	x1, x22, 9, 8
	lsl	w2, w20, 9
	add	x0, x0, x1
	add	x1, x27, x1
	bl	ftl_memcpy
	mov	x0, x26
	bl	buf_free
	b	.L2669
.L2674:
	mov	w0, w23
	bl	lpa_hash_get_ppa
	str	w0, [x29,156]
	cmn	w0, #1
	bne	.L2678
	mov	w0, w23
	add	x1, x29, 156
	mov	w2, 0
	bl	pm_log2phys
.L2678:
	add	x1, x21, :lo12:.LANCHOR0
	mov	w0, 0
	add	x1, x1, 728
	ldr	x19, [x1,2064]
.L2681:
	ldr	w2, [x1,36]
	cmp	w2, w23
	bne	.L2679
	ldrb	w2, [x1,2]
	tbz	x2, 3, .L2679
	add	x1, x21, :lo12:.LANCHOR0
	ubfiz	x0, x0, 6, 32
	add	x1, x1, 728
	and	w2, w2, -9
	add	x0, x1, x0
	strb	w2, [x0,2]
	ldr	x25, [x0,8]
	ldr	w0, [x0,40]
	str	w0, [x29,156]
	b	.L2680
.L2679:
	add	w0, w0, 1
	add	x1, x1, 64
	cmp	w0, 32
	bne	.L2681
	mov	x25, 0
.L2680:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,2776]
	cmp	w20, w0
	bcs	.L2727
	cbz	x25, .L2684
	cbz	w22, .L2685
	mov	x0, x27
	mov	x1, x25
	lsl	w2, w22, 9
	bl	ftl_memcpy
.L2685:
	add	x0, x21, :lo12:.LANCHOR0
	add	w20, w22, w20
	ldrb	w3, [x0,2776]
	ldr	x19, [x0,2792]
	cmp	w20, w3
	bcc	.L2686
.L2727:
	add	x19, x19, 16
	b	.L2728
.L2686:
	ubfiz	x1, x20, 9, 9
	sub	w2, w3, w20
	add	x0, x27, x1
	lsl	w2, w2, 9
	add	x1, x25, x1
	add	x19, x19, 48
	bl	ftl_memcpy
.L2728:
	mov	w25, 0
	b	.L2683
.L2684:
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L2687
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
	bne	.L2688
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L2689
.L2688:
	ldrb	w1, [x28,1]
	adrp	x0, .LC235
	ldr	w2, [x29,156]
	mov	w3, w23
	ldr	w4, [x28,52]
	add	x0, x0, :lo12:.LC235
	bl	printk
	mov	w2, 4
	ldr	x1, [x28,24]
	adrp	x0, .LC173
	mov	w3, w2
	add	x0, x0, :lo12:.LC173
	bl	rknand_print_hex
	add	x3, x21, :lo12:.LANCHOR0
	mov	w2, 21
	mov	w1, 1
	ldrh	w0, [x3,3218]
	sub	w2, w2, w0
	lsl	w2, w1, w2
	ldr	w1, [x29,156]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x3,3216]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
.L2689:
	ldr	x0, [x28,24]
	ldr	w0, [x0,4]
	cmp	w0, w23
	bne	.L2690
	ldr	w0, [x28,52]
	cmn	w0, #1
	bne	.L2691
.L2690:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 792
	mov	w2, 464
	bl	printk
.L2692:
	b	.L2692
.L2691:
	cbz	w22, .L2693
	ldr	w0, [x29,156]
	lsl	w2, w22, 9
	cmn	w0, #1
	beq	.L2694
	ldr	x1, [x28,8]
	mov	x0, x27
	bl	ftl_memcpy
	b	.L2693
.L2687:
	cbz	w22, .L2730
	lsl	w2, w22, 9
	mov	x28, x25
.L2694:
	mov	x0, x27
	mov	w1, 0
	bl	ftl_memset
.L2693:
	add	x1, x21, :lo12:.LANCHOR0
	add	w20, w22, w20
	ldrb	w2, [x1,2776]
	ldr	x0, [x1,2792]
	cmp	w20, w2
	add	x19, x0, 48
	bcc	.L2695
	add	x19, x0, 16
	b	.L2696
.L2695:
	ldr	w0, [x29,156]
	sub	w2, w2, w20
	lsl	w2, w2, 9
	cmn	w0, #1
	ubfiz	x0, x20, 7, 9
	beq	.L2697
	ldr	x1, [x28,8]
	lsl	x20, x0, 2
	add	x0, x27, x20
	add	x1, x1, x20
	bl	ftl_memcpy
	b	.L2696
.L2697:
	add	x0, x27, x0, lsl 2
	mov	w1, 0
	bl	ftl_memset
.L2696:
	cbz	x28, .L2683
	ldrb	w0, [x28,2]
	mov	x1, x28
	and	w0, w0, -9
	strb	w0, [x28,2]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 2824
	bl	buf_remove_buf
	mov	x0, x28
	bl	buf_free
.L2683:
	ldrh	w0, [x19,6]
	cbnz	w0, .L2698
	bl	ftl_flush
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L2698:
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
	ldrb	w0, [x20,3772]
	add	w0, w0, 1
	strb	w0, [x20,3772]
	bl	timer_get_time
	ldr	x1, [x29,120]
	ldrb	w2, [x20,3772]
	str	w0, [x1,388]
	cmp	w2, 2
	adrp	x0, .LANCHOR5
	bhi	.L2699
	ldrh	w1, [x19,6]
	cmp	w1, 1
	bne	.L2671
.L2699:
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
	ldrb	w5, [x1,384]
	mov	w1, 0
	mov	w3, w5
.L2703:
	cmp	w1, w4
	beq	.L2731
	ubfiz	x3, x3, 6, 8
	add	w1, w1, 1
	ldrb	w3, [x6,x3]
	b	.L2703
.L2731:
	add	x0, x0, :lo12:.LANCHOR5
	uxtb	w1, w1
	sub	w2, w2, w1
	ubfiz	x5, x5, 6, 8
	strb	w3, [x0,384]
	add	x0, x21, :lo12:.LANCHOR0
	add	x0, x0, 728
	strb	w2, [x0,3044]
	add	x0, x0, x5
	bl	sblk_prog_page
	ldrh	w0, [x19,6]
	cmp	w0, 1
	bne	.L2671
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	mov	x0, x19
	bl	ftl_write_last_log_page
	mov	x0, x19
	bl	ftl_alloc_new_data_sblk
.L2671:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w19, [x21,2809]
	cbnz	w19, .L2669
	bl	ftl_write_completed
	mov	w0, w19
	b	.L2726
.L2730:
	mov	x28, x25
	b	.L2693
.L2726:
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
	ldrb	w0, [x21,3209]
	ldr	x22, [x21,2792]
	cbnz	w0, .L2733
	bl	buf_alloc
	mov	x20, x0
	cbz	x0, .L2732
	ldrh	w23, [x21,2874]
	mov	w0, w23
	add	w23, w23, 1
	bl	gc_get_src_ppa_from_index
	str	w0, [x20,40]
	mov	w22, w0
	mov	w1, 1
	mov	x0, x20
	strh	w23, [x21,2874]
	bl	sblk_read_page
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L2790
	cmn	w0, #1
	bne	.L2735
.L2790:
	add	x3, x19, :lo12:.LANCHOR0
	mov	w2, 21
	ldrh	w1, [x3,3218]
	lsr	w0, w22, w1
	sub	w1, w2, w1
	mov	w2, 1
	lsl	w1, w2, w1
	sub	w1, w1, #1
	and	w1, w0, w1
	ldrb	w0, [x3,3216]
	udiv	w0, w1, w0
	mov	x1, 0
	bl	ftl_sblk_dump
.L2735:
	ldr	w0, [x20,52]
	cmp	w0, 512
	beq	.L2791
	cmn	w0, #1
	cset	w2, eq
	cbz	w2, .L2737
.L2791:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 816
	mov	w2, 559
	bl	printk
.L2739:
	b	.L2739
.L2737:
	ldr	x0, [x20,24]
	str	x2, [x29,120]
	ldr	w21, [x0,4]
	mov	w0, w21
	bl	lpa_hash_get_ppa
	str	w0, [x29,128]
	cmn	w0, #1
	ldr	x2, [x29,120]
	bne	.L2740
	mov	w0, w21
	add	x1, x29, 128
	bl	pm_log2phys
.L2740:
	ldr	w23, [x29,128]
	cmp	w23, w22
	bne	.L2741
	add	x1, x19, :lo12:.LANCHOR0
	add	x0, x1, 728
	add	x1, x1, 2776
.L2744:
	ldr	w2, [x0,36]
	cmp	w2, w21
	bne	.L2742
	ldrb	w2, [x0,2]
	tbz	x2, 1, .L2742
	mov	x0, x20
	bl	buf_free
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2732
	add	x19, x19, :lo12:.LANCHOR0
	adrp	x0, .LC236
	mov	w1, w21
	mov	w2, w23
	add	x0, x0, :lo12:.LC236
	ldrh	w3, [x19,2874]
	bl	printk
	b	.L2732
.L2742:
	add	x0, x0, 64
	cmp	x0, x1
	bne	.L2744
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
	tbz	x0, 8, .L2745
	ldr	w2, [x29,128]
	mov	w0, w21
	str	x2, [x29,120]
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldr	x2, [x29,120]
	adrp	x0, .LC237
	ldrh	w5, [x22,2874]
	add	x0, x0, :lo12:.LC237
	mov	w1, w21
	mov	w4, w23
	bl	printk
.L2745:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x0, x20
	bl	ftl_gc_write_buf
	bl	ftl_write_commit
	ldr	x1, [x19,2800]
	ldr	w0, [x1,60]
	add	w0, w0, 1
	str	w0, [x1,60]
	ldrh	w0, [x19,2876]
	add	w0, w0, 1
	strh	w0, [x19,2876]
	b	.L2732
.L2741:
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2747
	mov	w0, w21
	add	x19, x19, :lo12:.LANCHOR0
	bl	lpa_hash_get_ppa
	mov	w3, w0
	ldrh	w5, [x19,2874]
	adrp	x0, .LC237
	add	x0, x0, :lo12:.LC237
	mov	w1, w21
	mov	w2, w23
	mov	w4, w22
	bl	printk
.L2747:
	mov	x0, x20
	bl	buf_free
	b	.L2732
.L2733:
	ldrb	w24, [x21,3193]
	add	x0, x21, 2848
	add	x22, x22, 80
	cmp	w24, 3
	bne	.L2748
	ldrb	w1, [x21,3208]
	cbz	w1, .L2749
	ldrb	w20, [x22,9]
	ldrh	w21, [x0,314]
	add	w23, w20, w20, lsl 1
	sub	w20, w20, w20, lsl 2
	sdiv	w23, w21, w23
	madd	w20, w23, w20, w21
	uxth	w20, w20
	b	.L2751
.L2749:
	ldrh	w20, [x0,314]
	adrp	x0, .LANCHOR2
	ldrb	w21, [x22,9]
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1336
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
	bne	.L2752
	b	.L2751
.L2748:
	ldrb	w20, [x22,9]
	mov	w24, 1
	ldrb	w1, [x21,3228]
	ldrh	w21, [x0,314]
	sdiv	w23, w21, w20
	msub	w20, w23, w20, w21
	uxth	w20, w20
	cbz	w1, .L2751
	mov	w24, 2
.L2751:
	mov	w25, 0
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x3, .LANCHOR4
.L2754:
	ldrb	w1, [x22,9]
	mul	w0, w24, w1
	cmp	w25, w0
	bge	.L2788
	ldrh	w0, [x2,3210]
	add	w26, w21, w25
	mul	w0, w0, w1
	sub	w0, w0, #1
	cmp	w26, w0
	beq	.L2788
	add	x0, x3, :lo12:.LANCHOR4
	sbfiz	x26, x26, 2, 32
	ldr	x1, [x0,1024]
	ldr	w0, [x1,x26]
	cmn	w0, #1
	bne	.L2755
	ldrh	w27, [x2,2874]
	add	x28, x2, 2848
	ldrh	w0, [x2,2872]
	str	x3, [x29,104]
	str	x1, [x29,112]
	cmp	w0, w27
	str	x2, [x29,120]
	bls	.L2732
	mov	w0, w27
	add	w27, w27, 1
	bl	gc_get_src_ppa_from_index
	strh	w27, [x28,26]
	ldr	x1, [x29,112]
	ldr	x3, [x29,104]
	ldr	x2, [x29,120]
	str	w0, [x1,x26]
.L2755:
	add	w25, w25, 1
	uxth	w25, w25
	b	.L2754
.L2788:
	mov	w25, 1
.L2752:
	adrp	x0, .LANCHOR2
	str	x0, [x29,112]
	add	x1, x0, :lo12:.LANCHOR2
	ldr	w1, [x1,400]
	tbz	x1, 8, .L2757
	adrp	x0, .LANCHOR4+1024
	add	x4, x19, :lo12:.LANCHOR0
	ubfiz	x5, x21, 2, 16
	mov	w1, w23
	mov	w2, w25
	mov	w3, w20
	ldr	x6, [x0,#:lo12:.LANCHOR4+1024]
	adrp	x0, .LC238
	ldrh	w4, [x4,3162]
	add	x0, x0, :lo12:.LC238
	ldr	w5, [x6,x5]
	mov	w6, w21
	bl	printk
.L2757:
	adrp	x0, .LC239
	mov	w28, 0
	add	x0, x0, :lo12:.LC239
	str	x0, [x29,104]
.L2758:
	ldrb	w0, [x22,9]
	mul	w0, w24, w0
	cmp	w28, w0
	bge	.L2832
	add	w1, w21, w28
	sxtw	x0, w1
	str	x0, [x29,120]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x2, [x29,120]
	ldr	x0, [x0,3200]
	ldrb	w0, [x0,x2]
	cmp	w0, 255
	bne	.L2759
	mov	w0, 0
	str	x1, [x29,96]
	bl	buf_alloc
	mov	x27, x0
	ldr	x1, [x29,96]
	cbnz	x0, .L2760
	bl	sblk_wait_write_queue_completed
	bl	ftl_write_completed
	bl	gc_write_completed
	bl	gc_free_temp_buf
	mov	w0, w27
	bl	buf_alloc
	mov	x27, x0
	ldr	x1, [x29,96]
	cbz	x0, .L2732
.L2760:
	add	x26, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,120]
	ldrb	w2, [x27,1]
	ldr	x0, [x26,3200]
	strb	w2, [x0,x3]
	strh	w1, [x27,48]
	ldrb	w0, [x26,2855]
	strb	w25, [x27,61]
	add	w0, w0, 1
	strb	w0, [x26,2855]
	ldrb	w2, [x22,9]
	ldrh	w0, [x26,3210]
	mul	w0, w0, w2
	sub	w0, w0, #1
	cmp	w1, w0
	bne	.L2761
	adrp	x0, .LANCHOR4
	mov	w2, -1
	add	x5, x0, :lo12:.LANCHOR4
	str	x5, [x29,96]
	ldr	x0, [x5,1016]
	str	w2, [x0,x3,lsl 2]
	ldrh	w0, [x26,3210]
	ldrb	w2, [x26,3212]
	ldr	x1, [x5,1016]
	mul	w2, w0, w2
	ldr	x0, [x27,8]
	lsl	w2, w2, 2
	bl	ftl_memcpy
	ldrh	w0, [x26,3210]
	ldrb	w2, [x26,3212]
	ldr	x5, [x29,96]
	mul	w2, w0, w2
	ldr	x1, [x5,1024]
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
	ldrh	w0, [x26,3210]
	ldrb	w1, [x26,3212]
	ldr	x2, [x27,24]
	str	x2, [x29,96]
	mul	w1, w0, w1
	ldr	x0, [x27,8]
	lsl	w1, w1, 2
	bl	js_hash
	ldr	x2, [x29,96]
	str	w0, [x2,4]
	ldr	x1, [x26,2792]
	ldr	x0, [x27,24]
	ldr	w1, [x1,132]
	b	.L2829
.L2761:
	ldr	x0, [x29,120]
	mov	w1, 1
	lsl	x4, x0, 2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x4, [x29,96]
	ldr	x0, [x0,1024]
	ldr	w0, [x0,x4]
	str	w0, [x27,40]
	mov	x0, x27
	bl	sblk_read_page
	ldr	w0, [x27,52]
	ldr	x4, [x29,96]
	cmp	w0, 512
	beq	.L2762
	cmn	w0, #1
	bne	.L2763
.L2762:
	add	x5, x19, :lo12:.LANCHOR0
	mov	w1, 21
	str	x4, [x29,96]
	ldrh	w0, [x5,3218]
	sub	w2, w1, w0
	mov	w1, 1
	lsl	w2, w1, w2
	ldr	w1, [x27,40]
	sub	w2, w2, #1
	lsr	w0, w1, w0
	ldrb	w1, [x5,3216]
	and	w0, w2, w0
	udiv	w0, w0, w1
	mov	x1, 0
	bl	ftl_sblk_dump
	ldr	w0, [x27,52]
	ldr	x4, [x29,96]
	cmp	w0, 512
	beq	.L2792
	cmn	w0, #1
	bne	.L2763
.L2792:
	ldr	x0, [x27,24]
	mov	w1, -1
	str	w1, [x0,4]
.L2763:
	ldr	w0, [x27,52]
	cmp	w0, 512
	beq	.L2793
	cmn	w0, #1
	bne	.L2766
.L2793:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 816
	mov	w2, 674
	bl	printk
.L2768:
	b	.L2768
.L2766:
	ldr	x1, [x27,24]
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w2, [x1,4]
	ldr	w0, [x0,2784]
	cmp	w2, w0
	bcc	.L2769
	mov	w0, -1
	str	w0, [x1,4]
.L2769:
	ldr	x0, [x27,24]
	ldr	w1, [x0,4]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x0, [x0,1016]
	str	w1, [x0,x4]
	ldr	x0, [x27,24]
	ldr	w1, [x27,40]
.L2829:
	str	w1, [x0,8]
.L2759:
	add	x2, x19, :lo12:.LANCHOR0
	ldr	x3, [x29,120]
	add	x1, x2, 728
	mov	w5, 1
	sxtw	x4, w28
	ldr	x0, [x2,3200]
	ldrb	w3, [x0,x3]
	add	x0, x29, 128
	add	x1, x1, x3, lsl 6
	str	x1, [x0,w28,sxtw 3]
	ldrb	w0, [x2,3193]
	strb	w25, [x1,61]
	cmp	w0, 3
	strh	w5, [x1,50]
	bne	.L2770
	udiv	w0, w28, w0
	ldrh	w2, [x2,3230]
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
	ldr	x1, [x3,1032]
	add	w0, w0, 1
	orr	w0, w2, w0, lsl 24
	str	w0, [x1,x5,lsl 2]
	b	.L2771
.L2770:
	cmp	w0, 2
	bne	.L2772
	ldrb	w5, [x2,3228]
	add	x1, x1, 32
	ldrh	w0, [x2,3230]
	cbnz	w5, .L2773
	add	w2, w28, w20
	add	x2, x22, x2, sxtw 1
	ldrh	w2, [x2,16]
	madd	w0, w2, w0, w23
	b	.L2831
.L2773:
	add	w2, w20, w28, lsr 1
	add	x2, x22, x2, sxtw 1
	ldrh	w2, [x2,16]
	madd	w0, w2, w0, w23
	and	w2, w28, 1
	add	w0, w0, w2
.L2831:
	orr	w0, w0, 33554432
	str	w0, [x1,8]
.L2772:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x1, .LANCHOR4
	add	x0, x0, 728
	add	x1, x1, :lo12:.LANCHOR4
	add	x0, x0, x3, lsl 6
	ldr	w3, [x0,40]
	ldrh	w2, [x0,48]
	ldr	x0, [x1,1032]
	str	w3, [x0,x2,lsl 2]
.L2771:
	ldr	x0, [x29,112]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,400]
	tbz	x0, 8, .L2774
	add	x0, x29, 128
	ldr	x1, [x29,120]
	mov	w2, w25
	ldr	x5, [x0,x4,lsl 3]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w3, [x0,2854]
	ldr	x0, [x0,3200]
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
.L2774:
	add	w8, w28, 1
	uxth	w28, w8
	b	.L2758
.L2832:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w1, 0
	ldrb	w0, [x2,3193]
	cmp	w0, 3
	add	x0, x29, 128
	bne	.L2817
	ldrb	w1, [x2,3208]
	cbz	w1, .L2777
.L2780:
	ldr	x1, [x29,128]
	strb	wzr, [x1,60]
	b	.L2778
.L2777:
	cmp	w25, 1
	mov	w2, 9
	beq	.L2830
	cmp	w25, 2
	bne	.L2780
	mov	w2, 13
.L2830:
	ldr	x1, [x29,128]
	strb	w2, [x1,60]
.L2778:
	ldrb	w2, [x22,9]
	add	x1, x0, 24
	bl	sblk_xlc_prog_pages
	b	.L2781
.L2817:
	ldrb	w2, [x22,9]
	mul	w2, w2, w24
	sub	w2, w2, #1
	cmp	w1, w2
	bge	.L2833
	ldr	x2, [x0,w1,sxtw 3]
	add	w1, w1, 1
	ldr	x3, [x0,w1,sxtw 3]
	uxth	w1, w1
	ldrb	w3, [x3,1]
	strb	w3, [x2]
	b	.L2817
.L2833:
	ldr	x0, [x0,w2,sxtw 3]
	mov	w1, -1
	strb	w1, [x0]
	ldrb	w1, [x22,9]
	ldr	x0, [x29,128]
	mul	w1, w24, w1
	bl	sblk_prog_page
.L2781:
	add	x1, x19, :lo12:.LANCHOR0
	ldrb	w2, [x22,9]
	ldrb	w3, [x1,3208]
	cbz	w3, .L2783
	add	w0, w2, w2, lsl 1
	and	w2, w0, 1023
	b	.L2784
.L2783:
	ldrb	w1, [x1,3228]
	ubfiz	w0, w2, 1, 8
	cmp	w1, wzr
	csel	w2, w0, w2, ne
.L2784:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x1, [x19,2800]
	ldr	w0, [x1,52]
	add	w0, w0, w2
	str	w0, [x1,52]
	ldrh	w1, [x19,3162]
	add	w0, w2, w1
	ldrh	w1, [x19,3210]
	uxth	w0, w0
	strh	w0, [x19,3162]
	ldrb	w2, [x22,9]
	mul	w1, w1, w2
	cmp	w0, w1
	blt	.L2785
	ldr	x0, [x19,2792]
	strh	wzr, [x0,86]
.L2785:
	bl	gc_write_completed
.L2732:
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
	add	w2, w0, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w20, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w1, [x1,4]
	cmp	w2, w1
	bhi	.L2856
	add	w23, w0, 24576
	adrp	x0, .LANCHOR2+400
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 12, .L2836
	adrp	x0, .LC240
	mov	w1, 0
	add	x0, x0, :lo12:.LC240
	mov	w2, w23
	mov	w3, w20
	mov	w4, w1
	bl	printk
.L2836:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w3, [x0,2776]
	ldr	x1, [x0,2800]
	udiv	w19, w23, w3
	ldr	w24, [x1,8]
	add	w2, w24, 1
	str	w2, [x1,8]
	msub	w26, w19, w3, w23
	cbz	w26, .L2837
	sub	w3, w3, w26
	mov	w0, w19
	cmp	w3, w20
	csel	w25, w3, w20, ls
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	bne	.L2838
	mov	w0, w19
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
.L2838:
	ldr	w0, [x29,104]
	and	w27, w25, 65535
	cmn	w0, #1
	beq	.L2840
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L2840
	str	w19, [x0,36]
	ubfiz	x1, x26, 9, 25
	strb	w26, [x0,57]
	lsl	w2, w27, 9
	strb	w25, [x0,56]
	str	w24, [x0,32]
	ldr	x0, [x0,8]
	add	x0, x0, x1
	mov	w1, 0
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2840:
	add	w19, w19, 1
	sub	w20, w20, w27
.L2837:
	cbz	w20, .L2842
	bl	ftl_flush
.L2842:
	mov	w0, -1
	mov	w26, 1
	str	w0, [x29,108]
	mov	w27, 21
.L2843:
	add	x25, x21, :lo12:.LANCHOR0
	ldrb	w0, [x25,2776]
	cmp	w20, w0
	bcc	.L2883
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	beq	.L2844
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L2845
	ldrb	w2, [x25,2776]
	mov	w1, 0
	strb	w2, [x0,56]
	str	w19, [x0,36]
	lsl	w2, w2, 9
	strb	wzr, [x0,57]
	str	w24, [x0,32]
	ldr	x0, [x0,8]
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	x1, [x25,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2845:
	mov	w0, w19
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,104]
	cmn	w0, #1
	beq	.L2847
	add	x1, x29, 108
	mov	w0, w19
	mov	w2, 1
	bl	pm_log2phys
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	b	.L2882
.L2844:
	mov	w0, w19
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
	ldr	w0, [x29,104]
	cmn	w0, #1
	beq	.L2847
	add	x1, x29, 108
	mov	w2, 1
	mov	w0, w19
	bl	pm_log2phys
	ldrh	w0, [x25,3218]
	ldr	w1, [x29,104]
	sub	w2, w27, w0
	lsl	w2, w26, w2
	lsr	w0, w1, w0
	sub	w2, w2, #1
	ldrb	w1, [x25,3216]
	and	w0, w2, w0
	udiv	w0, w0, w1
	bl	ftl_vpn_decrement
	ldr	x1, [x25,2800]
.L2882:
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2847:
	add	x0, x21, :lo12:.LANCHOR0
	add	w19, w19, 1
	ldrb	w0, [x0,2776]
	sub	w20, w20, w0
	b	.L2843
.L2883:
	cbz	w20, .L2851
	mov	w0, w19
	bl	lpa_hash_get_ppa
	str	w0, [x29,104]
	cmn	w0, #1
	bne	.L2852
	mov	w0, w19
	add	x1, x29, 104
	mov	w2, 0
	bl	pm_log2phys
.L2852:
	ldr	w0, [x29,104]
	cmn	w0, #1
	beq	.L2851
	mov	w0, 0
	bl	buf_alloc
	mov	x22, x0
	cbz	x0, .L2851
	add	x21, x21, :lo12:.LANCHOR0
	str	w19, [x0,36]
	strb	wzr, [x0,57]
	strb	w20, [x0,56]
	str	w24, [x0,32]
	ldrb	w0, [x21,2776]
	cmp	w20, w0
	bcc	.L2854
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 832
	mov	w2, 1071
	bl	printk
.L2855:
	b	.L2855
.L2854:
	ldr	x0, [x22,8]
	mov	w1, 0
	lsl	w2, w20, 9
	bl	ftl_memset
	mov	x0, x22
	bl	ftl_write_buf
	bl	ftl_write_commit
	ldr	x1, [x21,2800]
	ldr	w0, [x1,76]
	add	w0, w0, 1
	str	w0, [x1,76]
.L2851:
	adrp	x19, .LANCHOR9
	mov	w21, 0
	add	x19, x19, :lo12:.LANCHOR9
	ldr	w1, [x19,1136]
	add	w1, w20, w1
	str	w1, [x19,1136]
	cmp	w1, 8192
	bls	.L2835
	adrp	x0, .LC240
	mov	w2, w23
	mov	w3, w20
	mov	w4, w21
	add	x0, x0, :lo12:.LC240
	bl	printk
	str	w21, [x19,1136]
	bl	flt_sys_flush
	b	.L2835
.L2856:
	mov	w21, -1
.L2835:
	mov	w0, w21
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 112
	ret
	.size	zftl_discard, .-zftl_discard
	.align	2
	.global	zftl_do_gc
	.type	zftl_do_gc, %function
zftl_do_gc:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	adrp	x20, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	x22, [x1,2792]
	ldr	x1, [x1,2800]
	ldrh	w24, [x1,116]
	ldrh	w2, [x1,118]
	ldrh	w25, [x1,114]
	add	w25, w24, w25
	add	w24, w24, w2
	add	x2, x20, :lo12:.LANCHOR4
	uxth	w25, w25
	uxth	w24, w24
	ldrb	w3, [x2,1010]
	cmp	w3, 6
	bhi	.L2885
	adrp	x2, .L2887
	add	x2, x2, :lo12:.L2887
	ldrh	w2, [x2,w3,uxtw #1]
	adr	x3, .Lrtx2887
	add	x2, x3, w2, sxth #2
	br	x2
.Lrtx2887:
	.section	.rodata
	.align	0
	.align	2
.L2887:
	.2byte	(.L2886 - .Lrtx2887) / 4
	.2byte	(.L2888 - .Lrtx2887) / 4
	.2byte	(.L2889 - .Lrtx2887) / 4
	.2byte	(.L2890 - .Lrtx2887) / 4
	.2byte	(.L2891 - .Lrtx2887) / 4
	.2byte	(.L2892 - .Lrtx2887) / 4
	.2byte	(.L2893 - .Lrtx2887) / 4
	.text
.L2886:
	ldrh	w2, [x22,80]
	ldrh	w27, [x1,122]
	ldrh	w28, [x1,124]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2894
	adrp	x0, .LANCHOR2+400
	cmp	w24, 7
	mov	w21, 3
	csinc	w21, w21, wzr, hi
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2896
	add	x2, x19, :lo12:.LANCHOR0
	adrp	x0, .LC241
	ldrh	w5, [x22,124]
	add	x0, x0, :lo12:.LC241
	ldrh	w6, [x22,120]
	mov	w1, 1455
	ldrb	w2, [x2,3209]
	mov	w3, w25
	ldrh	w7, [x22,122]
	mov	w4, w24
	bl	printk
.L2896:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w1, w21
	mov	w2, 1
	ldrb	w0, [x19,3209]
	bl	gc_search_src_blk
	cmp	w0, wzr
	ble	.L2885
.L2897:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 1
	b	.L3049
.L2894:
	cmp	w0, 1
	bne	.L2898
	bl	gc_scan_static_data
	bl	gc_static_wearleveling
	bl	gc_block_vpn_scan
.L2898:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w23, 1
	ldr	x7, [x21,2792]
	strb	w23, [x21,3209]
	ldrh	w26, [x7,124]
	cbz	w26, .L2899
	adrp	x0, .LANCHOR2+400
	add	x20, x20, :lo12:.LANCHOR4
	strb	wzr, [x21,3209]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	strb	w23, [x20,1010]
	tbz	x0, 8, .L2885
	ldrh	w6, [x7,120]
	adrp	x0, .LC241
	ldrh	w7, [x7,122]
	mov	w1, 1483
	mov	w2, 0
	mov	w3, w25
	mov	w4, w24
	mov	w5, w26
	add	x0, x0, :lo12:.LC241
	bl	printk
	b	.L2885
.L2899:
	ldrh	w0, [x21,3214]
	cmp	w25, w0, lsr 2
	bcs	.L2902
	cmp	w24, 1
	bls	.L2903
	cmp	w24, 16
	bhi	.L2904
	ldr	x0, [x21,2800]
	ldrh	w0, [x0,120]
	cmp	w0, 127
	bhi	.L2905
.L2904:
	mov	w0, 1
	mov	w1, 3
	b	.L2908
.L2905:
	mov	w0, w23
	mov	w1, 2
	mov	w2, 8
.L3045:
	bl	gc_search_src_blk
	uxth	w3, w0
	b	.L2906
.L2903:
	adrp	x0, .LANCHOR2+400
	strb	w26, [x21,3209]
	ldr	w0, [x0,#:lo12:.LANCHOR2+400]
	tbz	x0, 8, .L2907
	adrp	x0, .LC241
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	add	x0, x0, :lo12:.LC241
	mov	w1, 1496
	mov	w2, w26
	mov	w3, w25
	mov	w4, w24
	mov	w5, w26
	bl	printk
.L2907:
	add	x19, x19, :lo12:.LANCHOR0
	cmp	w25, 16
	mov	w1, 3
	mov	w2, 1
	ldrb	w0, [x19,3209]
	bhi	.L3045
.L2908:
	mov	w2, 16
	b	.L3045
.L2902:
	add	w22, w28, w27
	cmp	w25, w0, lsr 1
	uxth	w22, w22
	bcs	.L2909
	sub	w0, w24, #2
	uxth	w0, w0
	cmp	w0, 13
	bhi	.L2910
	mov	w0, w23
	mov	w1, 2
	mov	w2, 8
	bl	gc_search_src_blk
	uxth	w0, w0
	cbnz	w0, .L2897
.L2910:
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, .LANCHOR9
	ldrb	w1, [x0,3193]
	cmp	w1, 2
	bne	.L2911
	strh	wzr, [x0,2882]
	cmp	w24, 1
	bls	.L2912
.L2915:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w1, 1
	strb	w1, [x0,3209]
	adrp	x1, .LANCHOR2+400
	ldr	w1, [x1,#:lo12:.LANCHOR2+400]
	tbz	x1, 8, .L2914
	ldr	x7, [x0,2792]
	adrp	x0, .LC241
	mov	w1, 1519
	mov	w2, 1
	mov	w3, w25
	mov	w4, w24
	ldrh	w5, [x7,124]
	add	x0, x0, :lo12:.LC241
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	bl	printk
	b	.L2914
.L2911:
	cmp	w24, 1
	bls	.L2912
	ldrh	w0, [x0,2882]
	cmp	w0, 3
	bls	.L2915
	b	.L2912
.L2914:
	add	x0, x21, :lo12:.LANCHOR9
	add	x2, x19, :lo12:.LANCHOR0
	mov	w3, 0
	ldrh	w0, [x0,1116]
	ldrh	w1, [x2,2882]
	strh	wzr, [x2,2880]
	add	w1, w1, 1
	cmp	w22, w0, lsr 2
	strh	w1, [x2,2882]
	bls	.L2916
	ldrb	w0, [x2,3209]
	mov	w1, 1
	mov	w2, 16
	bl	gc_search_src_blk
	uxth	w3, w0
.L2916:
	cmp	w3, 7
	bhi	.L2989
	cmp	w24, 31
	bls	.L2917
.L2989:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,2800]
	ldrh	w0, [x0,3224]
	ldrh	w1, [x1,120]
	cmp	w1, w0
	bls	.L2926
.L2917:
	add	x21, x21, :lo12:.LANCHOR9
	ldrh	w0, [x21,1116]
	cmp	w22, w0, lsr 2
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,3209]
	bcs	.L3040
	mov	w1, 2
	mov	w2, 8
	b	.L3042
.L2912:
	add	x0, x21, :lo12:.LANCHOR9
	ldrh	w0, [x0,1116]
	cmp	w22, w0, lsr 1
	add	x0, x19, :lo12:.LANCHOR0
	strh	wzr, [x0,2882]
	bls	.L2921
	adrp	x1, .LANCHOR2+400
	strb	wzr, [x0,3209]
	ldr	w1, [x1,#:lo12:.LANCHOR2+400]
	tbz	x1, 8, .L2922
	ldr	x7, [x0,2792]
	adrp	x0, .LC241
	add	x0, x0, :lo12:.LC241
	mov	w1, 1536
	mov	w2, 0
	mov	w3, w25
	ldrh	w5, [x7,124]
	mov	w4, w24
	ldrh	w6, [x7,120]
	ldrh	w7, [x7,122]
	bl	printk
.L2922:
	add	x21, x21, :lo12:.LANCHOR9
	mov	w3, 0
	ldrh	w0, [x21,1116]
	cmp	w22, w0, lsr 1
	bls	.L2926
	add	x0, x19, :lo12:.LANCHOR0
	cmp	w24, 31
	ldrb	w0, [x0,3209]
	bhi	.L2923
	cmp	w25, 32
	bls	.L2923
.L3040:
	mov	w1, 3
	b	.L3041
.L2923:
	mov	w1, 1
.L3041:
	mov	w2, 16
.L3042:
	bl	gc_search_src_blk
	uxth	w3, w0
	b	.L2926
.L2921:
	mov	w3, 0
	b	.L2926
.L2909:
	cmp	w25, w0
	bcs	.L2924
	adrp	x25, .LANCHOR9
	add	x0, x25, :lo12:.LANCHOR9
	ldrh	w0, [x0,1116]
	cmp	w22, w0, lsr 1
	bcc	.L2885
	mov	w0, w26
	mov	w1, w23
	mov	w2, 16
	strb	w26, [x21,3209]
	bl	gc_search_src_blk
	uxth	w3, w0
	cmp	w3, 7
	bhi	.L2926
	cmp	w24, 4
	bls	.L2926
	ldr	x0, [x21,2792]
	mov	w1, 255
	mov	w2, 128
	add	x0, x0, 136
	strh	w26, [x0,-16]
	bl	ftl_memset
	strb	w23, [x21,3209]
	ldr	x0, [x21,2800]
	ldrh	w1, [x0,120]
	ldrh	w0, [x21,3224]
	cmp	w1, w0
	bls	.L2927
	cmp	w24, 16
	bls	.L2928
.L2930:
	mov	w0, 1
	mov	w1, 2
	mov	w2, 8
	b	.L3043
.L2927:
	cmp	w24, 32
	bls	.L2928
	mov	w0, w23
	mov	w1, w23
	b	.L3044
.L2928:
	cmp	w24, 7
	bls	.L2930
	mov	w0, 1
	mov	w1, 3
.L3044:
	mov	w2, 16
.L3043:
	bl	gc_search_src_blk
	add	x25, x25, :lo12:.LANCHOR9
	uxth	w3, w0
	add	x0, x20, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,1010]
	ldrh	w0, [x25,1116]
	add	w0, w0, 16
	cmp	w22, w0
	bge	.L2906
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2826]
	lsr	w0, w0, 4
	b	.L3046
.L2926:
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x19,2826]
	lsr	w0, w0, 3
.L3046:
	strh	w0, [x19,3214]
	b	.L2906
.L2924:
	sub	w24, w24, #2
	uxth	w24, w24
	cmp	w24, 5
	bhi	.L2931
	adrp	x0, .LANCHOR9+1116
	ldrh	w0, [x0,#:lo12:.LANCHOR9+1116]
	cmp	w22, w0, lsr 1
	bls	.L2885
	mov	w0, w23
	mov	w1, 2
	mov	w2, 16
	bl	gc_search_src_blk
	uxth	w0, w0
	cmp	w0, 15
	bhi	.L2897
	ldrb	w0, [x21,3209]
	ldr	x1, [x21,2792]
	add	x2, x1, x0, lsl 1
	add	x0, x1, x0, lsl 7
	add	x0, x0, 136
	mov	w1, 255
	strh	w26, [x2,120]
	mov	w2, 128
	bl	ftl_memset
	b	.L2885
.L2931:
	ldrh	w0, [x21,2826]
	ldrh	w1, [x21,3224]
	lsr	w0, w0, 4
	strh	w0, [x21,3214]
	ldr	x0, [x21,2800]
	ldrh	w0, [x0,120]
	cmp	w0, w1, lsr 2
	bls	.L2885
	ldrb	w1, [x21,3212]
	ldrh	w0, [x21,3210]
	mul	w0, w0, w1
	ldr	w1, [x7,532]
	cmp	w1, w0, lsr 3
	bcs	.L2885
	mov	w0, w23
	mov	w1, 3
	mov	w2, 16
	bl	gc_search_src_blk
	uxth	w3, w0
	add	x0, x20, :lo12:.LANCHOR4
	strb	w23, [x0,1010]
.L2906:
	cbnz	w3, .L2897
	b	.L2885
.L2888:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 65535
	ldrh	w1, [x21,2848]
	cmp	w1, w0
	bne	.L2934
	bl	gc_get_src_blk
	strh	w0, [x21,2848]
.L2934:
	add	x0, x19, :lo12:.LANCHOR0
	mov	w2, 65535
	add	x1, x0, 2848
	ldrh	w3, [x0,2848]
	cmp	w3, w2
	beq	.L2935
	ldrh	w5, [x1,56]
	uxtw	x4, w3
	ldr	x2, [x0,2840]
	add	x2, x2, x4, lsl 2
	cbz	w5, .L2937
	mov	x0, 0
.L2936:
	cmp	w5, w0, uxth
	bls	.L2937
	add	x0, x0, 1
	add	x6, x1, x0, lsl 1
	ldrh	w6, [x6,56]
	cmp	w6, w3
	bne	.L2936
.L2941:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,2848]
	b	.L2885
.L2937:
	ldrb	w0, [x2,2]
	tst	w0, 192
	beq	.L2990
	and	w0, w0, 224
	cmp	w0, 224
	bne	.L2939
.L2990:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x0, [x0,2832]
	ldrh	w0, [x0,x4,lsl 1]
	cbz	w0, .L2941
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1622
	bl	printk
.L2942:
	b	.L2942
.L2939:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 2
	b	.L3049
.L2935:
	add	x20, x20, :lo12:.LANCHOR4
	strb	wzr, [x20,1010]
	b	.L2885
.L2889:
	bl	gc_scan_src_blk
	cmn	w0, #1
	bne	.L2943
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 3
.L3049:
	strb	w0, [x20,1010]
	b	.L2885
.L2943:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, 65535
	add	x21, x22, 2848
	ldrh	w0, [x22,2848]
	cmp	w0, w1
	beq	.L2897
	ldrh	w1, [x21,24]
	add	x20, x20, :lo12:.LANCHOR4
	cbz	w1, .L2944
	mov	w0, 4
	strh	wzr, [x21,26]
	strb	w0, [x20,1010]
	b	.L2885
.L2944:
	mov	w1, 1
	ldr	x2, [x22,2832]
	strb	w1, [x20,1010]
	ubfiz	x1, x0, 1, 16
	ldrh	w20, [x2,x1]
	cbz	w20, .L2945
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1650
	bl	printk
.L2946:
	b	.L2946
.L2945:
	bl	ftl_free_sblk
	ldrh	w1, [x22,2848]
	ldr	x0, [x22,2832]
	strh	w20, [x0,x1,lsl 1]
	ldrh	w1, [x21,30]
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, 8
	bhi	.L2947
	strh	w1, [x21,30]
	b	.L2941
.L2947:
	strh	w20, [x21,30]
	bl	ftl_flush
	bl	pm_flush
	bl	ftl_ext_info_flush
	mov	w0, w20
	bl	ftl_info_flush
	b	.L2941
.L2890:
	add	x21, x19, :lo12:.LANCHOR0
.L3021:
	bl	gc_scan_src_blk_one_page
	ldrh	w2, [x21,2850]
	add	x1, x21, 2848
	ldrh	w0, [x21,3234]
	cmp	w2, w0
	bcs	.L2948
	cmp	w25, 7
	bls	.L3021
	b	.L2885
.L2948:
	ldrh	w3, [x1,24]
	adrp	x0, .LANCHOR2
	cbz	w3, .L2949
	add	x0, x0, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 4
	strh	wzr, [x1,26]
	ldr	w0, [x0,400]
	strb	w2, [x20,1010]
	tbz	x0, 8, .L2950
	ldrh	w1, [x1]
	adrp	x0, .LC242
	ldr	x4, [x21,2832]
	add	x0, x0, :lo12:.LC242
	ubfiz	x2, x1, 1, 16
	ldrh	w2, [x4,x2]
	bl	printk
.L2950:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,2832]
	ldrh	w1, [x19,2848]
	ldrh	w1, [x0,x1,lsl 1]
	ldrh	w0, [x19,2872]
	cmp	w1, w0
	beq	.L2885
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1684
	bl	printk
.L2951:
	b	.L2951
.L2949:
	add	x0, x0, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	mov	w2, 1
	ldrh	w1, [x1]
	ldr	x21, [x21,2840]
	add	x21, x21, x1, uxth 2
	ldr	w0, [x0,400]
	strb	w2, [x20,1010]
	tbz	x0, 8, .L2952
	ldrb	w2, [x21,2]
	adrp	x0, .LC243
	add	x0, x0, :lo12:.LC243
	ubfx	x2, x2, 5, 3
	bl	printk
.L2952:
	ldrb	w0, [x21,2]
	tst	w0, 192
	beq	.L2991
	and	w0, w0, 224
	cmp	w0, 224
	cset	w21, eq
	cbz	w21, .L2953
.L2991:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1694
	bl	printk
.L2955:
	b	.L2955
.L2953:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x19, 2848
	ldrh	w0, [x19,2848]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x19,2848]
	ldrh	w0, [x20,30]
	add	w0, w0, 1
	uxth	w0, w0
	cmp	w0, 8
	bhi	.L2956
	strh	w0, [x20,30]
	b	.L2885
.L2956:
	strh	w21, [x20,30]
	b	.L3050
.L2891:
	ldrh	w26, [x22,80]
	mov	w0, 65535
	add	x23, x22, 80
	cmp	w26, w0
	bne	.L2957
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w1, [x21,3209]
	cmp	w1, 1
	bne	.L2957
	str	x1, [x29,104]
	bl	ftl_flush
	mov	w0, 5
	bl	ftl_alloc_sblk
	uxth	w24, w0
	cmp	w24, w26
	ldr	x1, [x29,104]
	bne	.L2958
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1710
	bl	printk
.L2959:
	b	.L2959
.L2958:
	bl	ftl_erase_sblk
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 5
	add	x1, x22, 96
	strb	w0, [x23,4]
	mov	w0, w24
	bl	ftl_get_blk_list_in_sblk
	uxtb	w0, w0
	ldrh	w1, [x21,3234]
	strb	w0, [x23,9]
	strh	w24, [x22,80]
	mul	w0, w0, w1
	strh	w0, [x23,6]
	mov	w1, 255
	ldrh	w0, [x21,3210]
	ldrb	w2, [x21,3212]
	strh	wzr, [x23,2]
	strb	wzr, [x23,5]
	mul	w2, w0, w2
	ldr	x0, [x20,1016]
	strh	wzr, [x23,10]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w0, [x21,3210]
	mov	w1, 255
	ldrb	w2, [x21,3212]
	mul	w2, w0, w2
	ldr	x0, [x20,1024]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w3, [x21,3210]
	mov	w1, 255
	ldrb	w2, [x21,3212]
	ldr	x0, [x21,3200]
	mul	w2, w3, w2
	bl	ftl_memset
	ldr	x0, [x21,2792]
	mov	w1, -1
	str	w24, [x0,132]
	strh	w1, [x0,128]
	strh	w1, [x0,130]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x21,2904]
	ldr	x0, [x21,2800]
	strh	w24, [x0,126]
	mov	w0, -1
	str	w0, [x21,3168]
	mov	w0, 0
	strh	wzr, [x21,3162]
	strh	wzr, [x21,3164]
	strh	wzr, [x21,3166]
	bl	ftl_info_flush
	b	.L2885
.L2957:
	cmp	w25, 7
	bhi	.L2960
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w21, [x0,3210]
	lsr	w21, w21, 1
	b	.L3047
.L2960:
	cmp	w25, 15
	bhi	.L2962
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w21, [x0,3210]
	lsr	w21, w21, 3
.L3047:
	add	w21, w21, 1
	uxtb	w21, w21
	b	.L2961
.L2962:
	cmp	w25, 31
	bhi	.L2963
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w21, [x0,3210]
	lsr	w21, w21, 4
	b	.L3047
.L2963:
	cmp	w25, 63
	mov	w21, 9
	bls	.L2961
	cmp	w25, 127
	mov	w21, 5
	bls	.L2961
	cmp	w25, 255
	mov	w21, 2
	csinc	w21, w21, wzr, ls
.L2961:
	cmp	w24, 7
	bhi	.L2964
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w0, [x0,3210]
	add	w21, w21, w0, lsr 3
	b	.L3048
.L2964:
	cmp	w24, 15
	bhi	.L2965
	add	w21, w21, 16
.L3048:
	uxtb	w21, w21
.L2965:
	add	x24, x19, :lo12:.LANCHOR0
	bl	timer_get_time
.L2966:
	cbz	w21, .L2885
	bl	gc_do_copy_back
	ldrb	w0, [x24,3209]
	cbnz	w0, .L2967
	ldrb	w0, [x24,2778]
	cmp	w0, 3
	bhi	.L2968
	bl	ftl_write_commit
.L2968:
	add	x25, x24, 2848
	ldrh	w1, [x25,26]
	ldrh	w0, [x25,24]
	cmp	w1, w0
	bcc	.L2992
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x20,1010]
	bl	ftl_write_commit
	bl	ftl_flush
	ldrh	w0, [x25]
	ldr	x1, [x24,2832]
	lsl	x0, x0, 1
	ldrh	w2, [x1,x0]
	cbz	w2, .L2970
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1771
	bl	printk
.L2971:
	b	.L2971
.L2970:
	strh	w2, [x1,x0]
	ldrh	w0, [x25]
	bl	ftl_free_sblk
	mov	w0, -1
	strh	w0, [x25]
	b	.L2885
.L2967:
	add	x0, x24, 2848
	ldrh	w1, [x0,318]
	cbz	w1, .L2972
	strh	wzr, [x0,318]
	ldr	w0, [x0,320]
	cmn	w0, #1
	beq	.L2973
	bl	ftl_mask_bad_block
.L2973:
	add	x21, x19, :lo12:.LANCHOR0
	ldr	x0, [x21,2792]
	str	wzr, [x21,3168]
	strh	wzr, [x21,2904]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x21,2792]
	mov	w0, -1
	ldr	x2, [x21,2800]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x21,2856]
	cbz	x0, .L2974
	bl	buf_free
.L2974:
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	str	xzr, [x19,2856]
	bl	flt_sys_flush
	adrp	x1, .LANCHOR3
	adrp	x0, .LC34
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC34
	add	x1, x1, 848
	mov	w2, 1804
	strb	wzr, [x20,1010]
	bl	printk
.L2975:
	b	.L2975
.L2972:
	ldrh	w1, [x23,6]
	cmp	w1, 1
	bls	.L2976
	ldrh	w2, [x0,26]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L2992
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x20,1010]
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
	b	.L2885
.L2976:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 5
	ldrh	w2, [x0,26]
	strb	w1, [x20,1010]
	ldrh	w1, [x0,24]
	cmp	w2, w1
	bcc	.L2977
	ldrh	w1, [x0,56]
	add	w2, w1, 1
	add	x1, x0, x1, sxtw 1
	strh	w2, [x0,56]
	ldrh	w2, [x0]
	strh	w2, [x1,58]
	mov	w1, -1
	strh	w1, [x0]
.L2977:
	add	x20, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	bl	sblk_wait_write_queue_completed
	bl	gc_write_completed
	ldr	x0, [x20,2792]
	ldrh	w1, [x22,80]
	strh	w1, [x0,128]
	bl	pm_flush
	bl	ftl_ext_info_flush
	strh	wzr, [x20,2864]
	ldrb	w2, [x20,3193]
	ldrh	w1, [x20,3234]
	strh	w1, [x20,2866]
	cmp	w2, 2
	strh	w2, [x20,2868]
	bne	.L2978
	lsl	w1, w1, 1
	strh	w1, [x20,2866]
	mov	w1, 1
	strh	w1, [x20,2868]
.L2978:
	add	x19, x19, :lo12:.LANCHOR0
	strh	wzr, [x19,2870]
	b	.L2885
.L2992:
	sub	w21, w21, #1
	bl	timer_get_time
	uxtb	w21, w21
	b	.L2966
.L2892:
	add	x22, x19, :lo12:.LANCHOR0
.L3022:
	bl	gc_check_data_one_wl
	cbz	w0, .L2980
	add	x19, x19, :lo12:.LANCHOR0
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x0, [x19,2792]
	strh	wzr, [x19,2904]
	ldrh	w0, [x0,80]
	bl	ftl_free_sblk
	ldr	x1, [x19,2792]
	mov	w0, -1
	ldr	x2, [x19,2800]
	strh	w0, [x1,80]
	strh	w0, [x2,126]
	strh	w0, [x1,130]
	ldr	x0, [x19,2856]
	bl	buf_free
	str	xzr, [x19,2856]
	strb	wzr, [x20,1010]
.L3050:
	bl	flt_sys_flush
	b	.L2885
.L2980:
	ldrh	w1, [x22,2864]
	ldrh	w0, [x22,2866]
	cmp	w1, w0
	bcc	.L2981
	add	x20, x20, :lo12:.LANCHOR4
	mov	w0, 6
	strb	w0, [x20,1010]
	ldr	x0, [x22,2856]
	bl	buf_free
	str	xzr, [x22,2856]
	b	.L2885
.L2981:
	cmp	w25, 7
	bls	.L3022
	b	.L2885
.L2893:
	bl	gc_update_l2p_map_new
	mov	w21, -1
	bl	gc_free_src_blk
	add	x19, x19, :lo12:.LANCHOR0
	bl	ftl_flush
	add	x20, x20, :lo12:.LANCHOR4
	bl	pm_flush
	strh	w21, [x22,80]
	bl	ftl_ext_info_flush
	ldr	x0, [x19,2800]
	strh	w21, [x0,126]
	mov	w0, 0
	bl	ftl_info_flush
	strb	wzr, [x20,1010]
	bl	print_ftl_debug_info
.L2885:
	mov	w0, 16
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	zftl_do_gc, .-zftl_do_gc
	.align	2
	.global	zftl_write
	.type	zftl_write, %function
zftl_write:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w28, w2
	mov	x25, x3
	cbnz	w0, .L3052
	adrp	x0, .LANCHOR0+4
	ldr	w3, [x0,#:lo12:.LANCHOR0+4]
	mov	w0, 24576
	b	.L3053
.L3052:
	cmp	w0, 3
	mov	w19, -1
	bhi	.L3054
	lsl	w0, w0, 13
	mov	w3, 8192
.L3053:
	add	w2, w1, w28
	mov	w19, -1
	cmp	w2, w3
	bhi	.L3054
	adrp	x22, .LANCHOR0
	add	w27, w0, w1
	add	x0, x22, :lo12:.LANCHOR0
	add	w21, w28, w19
	add	w21, w21, w27
	add	w24, w27, w28
	ldrb	w0, [x0,2776]
	udiv	w23, w27, w0
	mov	w20, w23
	udiv	w21, w21, w0
	sub	w19, w21, w23
	add	w19, w19, 1
.L3055:
	cbz	w19, .L3068
	mov	w0, 0
	bl	buf_alloc
	mov	x26, x0
	cbnz	x0, .L3056
	bl	ftl_write_commit
	b	.L3055
.L3056:
	strb	wzr, [x0,57]
	add	x0, x22, :lo12:.LANCHOR0
	cmp	w20, w23
	ldrb	w4, [x0,2776]
	cset	w0, eq
	strb	w4, [x26,56]
	cmp	w20, w21
	beq	.L3058
	cbz	w0, .L3059
	b	.L3066
.L3058:
	cbz	w0, .L3060
.L3066:
	udiv	w0, w27, w4
	msub	w0, w0, w4, w27
	uxtb	w0, w0
	strb	w0, [x26,57]
	sub	w4, w4, w0
	uxtb	w4, w4
	cmp	w4, w28
	csel	w4, w28, w4, hi
	b	.L3067
.L3060:
	msub	w4, w4, w20, w24
.L3067:
	strb	w4, [x26,56]
.L3059:
	ldrb	w1, [x26,57]
	sub	w19, w19, #1
	ldr	x0, [x26,8]
	ldrb	w2, [x26,56]
	add	x0, x0, x1, lsl 9
	mov	x1, x25
	lsl	w2, w2, 9
	bl	ftl_memcpy
	str	w20, [x26,36]
	add	x0, x22, :lo12:.LANCHOR0
	add	w20, w20, 1
	ldr	x1, [x0,2800]
	ldr	w0, [x1,8]
	add	w2, w0, 1
	str	w2, [x1,8]
	str	w0, [x26,32]
	mov	x0, x26
	bl	ftl_write_buf
	ldrb	w0, [x26,56]
	add	x25, x25, x0, lsl 9
	b	.L3055
.L3068:
	bl	ftl_write_commit
	mov	w0, w19
	mov	w1, 1
	bl	zftl_do_gc
.L3054:
	mov	w0, w19
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
	mov	x3, x2
	add	w1, w0, 512
	add	x29, sp, 0
	mov	w2, 1
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
	mov	w1, w0
	mov	x3, x2
	add	x29, sp, 0
	mov	w2, 1
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
	ldr	x0, [x0,48]
	ldrb	w2, [x0,19]
	add	x0, x20, :lo12:.LANCHOR4
	ldrb	w0, [x0,1136]
	cbnz	w0, .L3071
	sub	w0, w2, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L3073
	cmp	w2, 68
	beq	.L3073
	sub	w2, w2, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L3071
.L3073:
	adrp	x24, .LC244
	mov	w25, w1
	add	x24, x24, :lo12:.LC244
	add	x20, x20, :lo12:.LANCHOR4
	mov	w27, 65535
.L3075:
	add	x28, x21, :lo12:.LANCHOR0
	ldr	x0, [x28,48]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L3071
	sub	x22, x20, #52
	add	w26, w19, w25
	mov	x0, x24
	mov	w1, w19
	mov	w2, w26
	ldrh	w3, [x22,w19,sxtw 1]
	bl	printk
	ldrh	w0, [x22,w19,sxtw 1]
	cmp	w0, w27
	bne	.L3071
	ldr	x0, [x20,1072]
	mov	w1, 255
	mov	w2, 16384
	add	w19, w19, 1
	bl	ftl_memset
	uxth	w19, w19
	ldr	x4, [x28,48]
	mov	w0, w23
	ldr	x2, [x20,1072]
	mov	w1, w26
	ldrb	w4, [x4,9]
	mov	x3, x2
	bl	flash_prog_page
	b	.L3075
.L3071:
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
	ldr	x0, [x0,48]
	ldrb	w23, [x0,9]
	ldrh	w20, [x0,10]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	bl	ftl_malloc
	mov	x22, x0
	mov	w0, -1
	cbz	x22, .L3111
	add	w19, w19, 511
	add	x2, x26, 253952
	lsr	w19, w19, 9
	add	x2, x2, 2044
	mov	w0, 0
	mov	w24, 63999
	mov	w3, 4097
.L3082:
	ldr	w1, [x2]
	cbnz	w1, .L3079
	ldr	w1, [x26,w0,uxtw 2]
	add	w0, w0, 1
	cmp	w0, w3
	sub	w24, w24, #1
	csel	w0, w0, wzr, cc
	str	w1, [x2],-4
	cmp	w24, 4096
	bne	.L3082
	b	.L3081
.L3079:
	adrp	x0, .LC245
	mov	w2, w24
	add	x0, x0, :lo12:.LC245
	bl	printk
.L3081:
	add	x0, x21, :lo12:.LANCHOR0
	add	w1, w19, 128
	mul	w20, w20, w23
	mov	w25, 0
	ldr	x2, [x0,48]
	ldrh	w0, [x2,10]
	ldrb	w19, [x2,12]
	mov	w2, w24
	lsl	w0, w0, 2
	sdiv	w19, w0, w19
	mov	w0, 500
	cmp	w19, 500
	csel	w19, w19, w0, ls
	adrp	x0, .LC246
	cmp	w19, w1
	add	x0, x0, :lo12:.LC246
	csel	w19, w19, w1, ls
	ldr	w1, [x26,w24,uxtw 2]
	mov	w3, w19
	bl	printk
	str	w25, [x29,140]
	uxth	w0, w20
	str	w0, [x29,116]
.L3101:
	mov	w1, 0
	mov	x0, x22
	mov	w2, 512
	add	x24, x21, :lo12:.LANCHOR0
	bl	ftl_memset
	ldr	x0, [x24,152]
	ldr	w1, [x29,140]
	add	x0, x0, x1, uxtw
	ldrb	w20, [x0,32]
	cmp	w20, 255
	beq	.L3083
	ldr	w0, [x29,116]
	mul	w20, w20, w0
	ldr	x0, [x24,48]
	ldrb	w27, [x0,9]
	ldrh	w23, [x0,10]
	ldrb	w0, [x24,144]
	udiv	w1, w20, w27
	str	w0, [x29,112]
	mul	w23, w27, w23
	mov	w0, 0
	strb	wzr, [x24,144]
	uxth	w23, w23
	bl	flash_erase_block
	udiv	w0, w20, w23
	msub	w0, w0, w23, w20
	str	w0, [x29,136]
	sub	w28, w20, w0
	cmp	w20, w28
	bne	.L3105
	ldrb	w0, [x21,#:lo12:.LANCHOR0]
	cmp	w0, 9
	bne	.L3105
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 1024
	ldr	x3, [x0,1120]
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
	ldr	x2, [x24,48]
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
	b	.L3084
.L3105:
	str	w19, [x29,128]
	mov	x3, 0
.L3084:
	str	x26, [x29,120]
	mov	w24, 0
	add	x23, x21, :lo12:.LANCHOR0
.L3086:
	cmp	w24, w19
	bcs	.L3119
	ldr	w0, [x29,136]
	adrp	x2, .LANCHOR4
	add	x2, x2, :lo12:.LANCHOR4
	add	w4, w24, w0
	add	x0, x23, 172
	ubfx	x4, x4, 2, 16
	add	w1, w4, 1
	ldrb	w2, [x2,1136]
	ldrh	w0, [x0,w1,sxtw 1]
	cbz	w2, .L3088
	ldrb	w6, [x23,168]
	lsl	w0, w1, 1
	cmp	w6, wzr
	csel	w0, w0, w1, ne
.L3088:
	ldrb	w1, [x23]
	cmp	w1, 9
	beq	.L3118
	sub	w0, w0, #1
	lsl	w0, w0, 2
.L3118:
	str	w0, [x29,144]
	mov	w0, 61424
	str	w0, [x29,148]
	add	x0, x23, 172
	ldrh	w0, [x0,w4,sxtw 1]
	cbnz	w2, .L3092
	mov	w4, w0
	b	.L3093
.L3092:
	ldrb	w1, [x23,168]
	lsl	w0, w4, 1
	cmp	w1, wzr
	csel	w4, w0, w4, ne
.L3093:
	mul	w0, w4, w27
	cbnz	x3, .L3094
	ldr	x1, [x29,120]
	add	w0, w0, w28
	add	x2, x29, 144
	str	x4, [x29,104]
	bl	fw_flash_page_prog.constprop.30
	adrp	x0, .LANCHOR4
	ldr	x4, [x29,104]
	add	x0, x0, :lo12:.LANCHOR4
	ldrb	w0, [x0,1136]
	cbnz	w0, .L3095
	udiv	w1, w28, w27
	add	w2, w4, 1
	bl	id_block_prog_msb_ff_data
.L3095:
	ldr	x0, [x29,120]
	add	x0, x0, 2048
	str	x0, [x29,120]
	b	.L3096
.L3094:
	add	w0, w0, w28
	mov	x1, x3
	add	x2, x29, 144
	bl	fw_flash_page_prog.constprop.30
.L3096:
	add	w24, w24, 4
	mov	x3, 0
	uxth	w24, w24
	b	.L3086
.L3119:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w1, [x29,112]
	mov	x2, x22
	strb	w1, [x0,144]
	mov	w0, w20
	ldr	w1, [x29,128]
	bl	id_block_read_data.constprop.29
	ldr	w0, [x29,128]
	lsl	w23, w0, 7
	mov	x0, 0
.L3098:
	cmp	w23, w0
	bls	.L3120
	ldr	w2, [x22,x0,lsl 2]
	add	x0, x0, 1
	add	x1, x26, x0, lsl 2
	ldr	w1, [x1,-4]
	cmp	w2, w1
	beq	.L3098
	mov	x0, x22
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w1, w20
	mov	w0, 0
	bl	flash_erase_block
	b	.L3083
.L3120:
	add	w25, w25, 1
	cmp	w25, 5
	bhi	.L3102
.L3083:
	ldr	w0, [x29,140]
	add	w0, w0, 1
	str	w0, [x29,140]
	cmp	w0, 4
	bne	.L3101
.L3102:
	mov	x0, x22
	bl	ftl_free
	cmp	w25, wzr
	csetm	w0, eq
.L3111:
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
.L3122:
	cmp	w1, w2
	bls	.L3124
	ldrb	w4, [x5,x2]
	add	x6, x3, 2104
	add	x2, x2, 1
	eor	w4, w4, w0, lsr 24
	ldr	w4, [x6,w4,uxtw 2]
	eor	w0, w4, w0, lsl 8
	b	.L3122
.L3124:
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
	beq	.L3127
	mov	w0, 29187
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3128
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3129
	mov	w0, 25726
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3130
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3131
	mov	w0, 25601
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3132
	mov	w0, 25364
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bne	.L3188
	bl	rknand_dev_flush
	b	.L3222
.L3132:
	mov	w0, 25602
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3134
	mov	w0, 25603
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3135
	b	.L3188
.L3130:
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3136
	mov	w0, 27688
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3137
	mov	w0, 25727
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3138
	b	.L3188
.L3137:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3136
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3136
	b	.L3188
.L3128:
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3139
	mov	w0, 29267
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3140
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3141
	mov	w0, 29189
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bcc	.L3142
	mov	w0, 29210
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3143
	mov	w0, 29266
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3144
	b	.L3188
.L3140:
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3145
	mov	w0, 30225
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	bhi	.L3146
	mov	w0, 29268
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3147
	mov	w0, 30224
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3148
	b	.L3188
.L3146:
	mov	w0, 30226
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3148
	mov	w0, 30227
	movk	w0, 0x4004, lsl 16
	cmp	w1, w0
	beq	.L3145
	b	.L3188
.L3142:
	adrp	x0, .LC247
	add	x0, x0, :lo12:.LC247
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbnz	x0, .L3149
.L3154:
	mov	x0, -12
	b	.L3126
.L3149:
	mov	x1, x20
	mov	x2, 512
	bl	rk_copy_from_user
	cbnz	x0, .L3229
	adrp	x0, .LC249
	ldr	w1, [x19]
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC249
	bl	printk
	ldr	w0, [x19,4]
	str	w0, [x29,52]
	cmp	w0, 8
	bhi	.L3226
	bl	rknand_device_lock
	ldr	w1, [x29,52]
	mov	x2, x19
	ldr	w0, [x19]
	bl	id_block_read_data.constprop.29
	bl	rknand_device_unlock
	ldr	w2, [x29,52]
	mov	x0, x20
	mov	x1, x19
	ubfiz	x2, x2, 9, 23
	bl	rk_copy_to_user
	cbz	x0, .L3232
	adrp	x0, .LC250
	add	x0, x0, :lo12:.LC250
.L3224:
	bl	printk
.L3226:
	mov	x0, x19
.L3227:
	bl	ftl_free
.L3225:
	mov	x0, -14
	b	.L3126
.L3141:
	adrp	x0, .LC251
	add	x0, x0, :lo12:.LC251
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3154
	mov	x1, x20
	mov	x2, 4096
	bl	rk_copy_from_user
	cbnz	x0, .L3229
	ldr	w1, [x19]
	adrp	x0, .LC252
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC252
	bl	printk
	adrp	x0, .LANCHOR9
	add	x21, x0, :lo12:.LANCHOR9
	mov	x20, x0
	ldr	x1, [x21,1144]
	cbnz	x1, .L3156
	mov	w0, 260096
	bl	ftl_malloc
	str	x0, [x21,1144]
	cbz	x0, .L3226
.L3156:
	ldr	w2, [x19,4]
	cmp	w2, 4088
	bhi	.L3226
	ldr	w0, [x19]
	mov	w1, 55296
	movk	w1, 0x3, lsl 16
	cmp	w0, w1
	bhi	.L3226
	add	x20, x20, :lo12:.LANCHOR9
	uxtw	x2, w2
	ldr	x1, [x20,1144]
	add	x0, x1, x0, uxtw
	add	x1, x19, 8
	bl	memcpy
.L3232:
	mov	x0, x19
	bl	ftl_free
	b	.L3222
.L3144:
	adrp	x0, .LC253
	add	x0, x0, :lo12:.LC253
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x19, x0
	cbz	x0, .L3154
	mov	x1, x20
	mov	x2, 28
	bl	rk_copy_from_user
	cbz	x0, .L3158
.L3229:
	adrp	x0, .LC248
	add	x0, x0, :lo12:.LC248
	b	.L3224
.L3158:
	ldr	w1, [x19]
	adrp	x0, .LC254
	ldr	w2, [x19,4]
	add	x0, x0, :lo12:.LC254
	bl	printk
	ldr	w1, [x19]
	mov	w0, 59392
	movk	w0, 0x3, lsl 16
	cmp	w1, w0
	bhi	.L3226
	adrp	x20, .LANCHOR9
	add	x20, x20, :lo12:.LANCHOR9
	ldr	x0, [x20,1144]
	cbz	x0, .L3226
	bl	CRC_32
	ldr	w1, [x19,4]
	cmp	w1, w0
	beq	.L3161
	mov	x0, x19
	bl	ftl_free
	mov	x0, -2
	b	.L3126
.L3161:
	bl	rknand_device_lock
	ldr	x1, [x20,1144]
	add	x2, x19, 8
	ldr	w0, [x19]
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	x0, [x20,1144]
	bl	ftl_free
	str	xzr, [x20,1144]
	b	.L3232
.L3143:
	adrp	x0, .LC255
	add	x0, x0, :lo12:.LC255
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3154
	bl	ReadFlashInfo
	mov	x0, x20
	mov	x1, x21
	mov	x2, 11
	b	.L3234
.L3127:
	adrp	x0, .LC256
	add	x0, x0, :lo12:.LC256
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3154
	mov	w1, 0
	mov	w2, 64
	bl	ftl_memset
	mov	x0, x20
	mov	x1, x21
	mov	x2, 64
	b	.L3234
.L3139:
	adrp	x0, .LC257
	add	x0, x0, :lo12:.LC257
	bl	printk
	adrp	x0, .LANCHOR9+1152
	add	x1, x29, 52
	mov	x2, 4
	ldr	x0, [x0,#:lo12:.LANCHOR9+1152]
	ldr	w0, [x0,20]
	str	w0, [x29,52]
	mov	x0, x20
.L3217:
	bl	rk_copy_to_user
	cbnz	x0, .L3225
	b	.L3222
.L3147:
	adrp	x0, .LC258
	add	x0, x0, :lo12:.LC258
	bl	printk
	mov	w0, 4096
	bl	ftl_malloc
	mov	x21, x0
	cbz	x0, .L3154
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
.L3234:
	bl	rk_copy_to_user
	mov	x19, x0
	mov	x0, x21
	cbnz	x19, .L3227
	bl	ftl_free
	b	.L3153
.L3131:
	adrp	x0, .LC259
	add	x0, x0, :lo12:.LC259
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	mov	x19, x0
	cbz	x0, .L3166
.L3171:
	adrp	x0, .LC248
	add	x0, x0, :lo12:.LC248
	bl	printk
	b	.L3225
.L3166:
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	beq	.L3167
.L3168:
	mov	x19, -1
	b	.L3153
.L3167:
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3168
	adrp	x21, .LANCHOR9
	add	x0, x29, 56
	add	x21, x21, :lo12:.LANCHOR9
	mov	x2, 512
	ldr	x1, [x21,1152]
	bl	memcpy
	ldr	w1, [x21,1160]
	mov	w0, 5161
	movk	w0, 0xc059, lsl 16
	cmp	w1, w0
	beq	.L3169
	add	x0, x29, 120
	mov	w1, w19
	mov	x2, 128
	str	w19, [x29,64]
	str	w19, [x29,68]
	bl	memset
.L3169:
	add	x0, x29, 312
	mov	w1, 0
	mov	x2, 256
	str	wzr, [x29,72]
	bl	memset
	b	.L3231
.L3134:
	adrp	x0, .LC260
	add	x0, x0, :lo12:.LC260
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 21060
	movk	w0, 0x4b4d, lsl 16
	cmp	w1, w0
	bne	.L3168
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3168
	adrp	x1, .LANCHOR9
	mov	w2, 5161
	add	x1, x1, :lo12:.LANCHOR9
	movk	w2, 0xc059, lsl 16
	mov	x0, -2
	ldr	w3, [x1,1160]
	cmp	w3, w2
	bne	.L3126
	ldr	w2, [x29,68]
	mov	x0, -3
	sub	w3, w2, #1
	cmp	w3, 127
	bhi	.L3126
	ldr	x19, [x1,1152]
	add	x1, x29, 120
	add	x0, x19, 64
	str	w2, [x19,12]
	ldr	w2, [x29,68]
	bl	memcpy
	mov	w0, 1
	mov	x1, x19
	b	.L3223
.L3138:
	adrp	x0, .LC261
	add	x0, x0, :lo12:.LC261
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 17476
	movk	w0, 0x4253, lsl 16
	cmp	w1, w0
	bne	.L3168
	ldr	w0, [x29,60]
	cmp	w0, 512
	bhi	.L3168
	adrp	x19, .LANCHOR9
	add	x0, x19, :lo12:.LANCHOR9
	ldr	w1, [x0,1164]
	cbnz	w1, .L3172
.L3175:
	mov	x0, 0
	b	.L3126
.L3172:
	ldr	x1, [x0,1168]
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	ldr	w3, [x1]
	cmp	w3, w2
	beq	.L3173
	mov	w2, 22867
	movk	w2, 0x4453, lsl 16
	str	w2, [x1]
	mov	w2, 504
	ldr	x1, [x0,1168]
	str	w2, [x1,4]
	ldr	x0, [x0,1168]
	str	wzr, [x0,8]
	str	wzr, [x0,12]
.L3173:
	add	x20, x19, :lo12:.LANCHOR9
	mov	w0, 0
	ldr	x1, [x20,1168]
	str	wzr, [x1,16]
	bl	StorageSysDataStore
	ldr	x0, [x20,1152]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3174
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1152]
	str	w1, [x0,4]
	ldr	x0, [x20,1152]
	str	wzr, [x0,8]
.L3174:
	add	x19, x19, :lo12:.LANCHOR9
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1152]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	str	wzr, [x19,1164]
	str	wzr, [x19,1160]
	b	.L3222
.L3129:
	adrp	x0, .LC262
	add	x0, x0, :lo12:.LC262
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w2, [x29,56]
	mov	w1, 20037
	movk	w1, 0x4253, lsl 16
	cmp	w2, w1
	bne	.L3168
	ldr	w1, [x29,60]
	cmp	w1, 512
	bhi	.L3168
	adrp	x19, .LANCHOR9
	add	x1, x19, :lo12:.LANCHOR9
	ldr	w2, [x1,1164]
	cmp	w2, 1
	beq	.L3175
	ldr	x2, [x1,1168]
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	ldr	w4, [x2]
	cmp	w4, w3
	beq	.L3176
	mov	w3, 22867
	movk	w3, 0x4453, lsl 16
	str	w3, [x2]
	mov	w3, 504
	ldr	x2, [x1,1168]
	str	w3, [x2,4]
	ldr	x1, [x1,1168]
	str	w0, [x1,8]
	str	w0, [x1,12]
.L3176:
	add	x20, x19, :lo12:.LANCHOR9
	mov	w0, 1
	ldr	x1, [x20,1168]
	str	w0, [x1,16]
	mov	w0, 0
	bl	StorageSysDataStore
	ldr	x0, [x20,1152]
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	ldr	w2, [x0]
	cmp	w2, w1
	beq	.L3177
	mov	w1, 21060
	movk	w1, 0x4b4d, lsl 16
	str	w1, [x0]
	mov	w1, 504
	ldr	x0, [x20,1152]
	str	w1, [x0,4]
	ldr	x0, [x20,1152]
	str	wzr, [x0,8]
.L3177:
	add	x19, x19, :lo12:.LANCHOR9
	mov	w1, 0
	mov	x2, 128
	ldr	x20, [x19,1152]
	add	x0, x20, 64
	str	wzr, [x20,12]
	bl	memset
	mov	w0, 1
	mov	x1, x20
	bl	StorageSysDataStore
	mov	w0, 1
	str	w0, [x19,1164]
	b	.L3222
.L3135:
	adrp	x0, .LC263
	add	x0, x0, :lo12:.LC263
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 20051
	movk	w0, 0x4144, lsl 16
	cmp	w1, w0
	bne	.L3168
	ldr	w2, [x29,60]
	cmp	w2, 512
	bhi	.L3168
	adrp	x1, .LANCHOR9
	add	x0, x29, 64
	add	x1, x1, :lo12:.LANCHOR9
	uxtw	x2, w2
	add	x1, x1, 1176
	b	.L3230
.L3136:
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3178
	adrp	x0, .LC264
	add	x0, x0, :lo12:.LC264
	b	.L3220
.L3178:
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3180
	adrp	x0, .LC265
	add	x0, x0, :lo12:.LC265
	b	.L3220
.L3180:
	adrp	x0, .LC266
	add	x0, x0, :lo12:.LC266
.L3220:
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 17227
	movk	w0, 0x4c4f, lsl 16
	cmp	w1, w0
	bne	.L3225
	mov	w0, 27708
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x0, .LANCHOR9
	bne	.L3181
	add	x0, x0, :lo12:.LANCHOR9
	add	x1, x29, 56
	mov	x2, 16
	ldr	x0, [x0,1152]
	ldr	w0, [x0,20]
	str	w0, [x29,60]
	strb	w0, [x29,64]
	mov	x0, x20
	bl	rk_copy_to_user
	cbz	x0, .L3126
	b	.L3225
.L3181:
	add	x20, x0, :lo12:.LANCHOR9
	ldr	w1, [x20,1688]
	cmp	w1, 10
	bhi	.L3225
	ldr	x1, [x20,1152]
	ldr	w2, [x29,60]
	ldr	w3, [x1,24]
	cmp	w3, w2
	beq	.L3182
	cbz	w3, .L3182
	adrp	x0, .LC267
	mov	w1, w2
	add	x0, x0, :lo12:.LC267
	bl	printk
	ldr	w0, [x20,1688]
	add	w0, w0, 1
	str	w0, [x20,1688]
	b	.L3225
.L3182:
	add	x0, x0, :lo12:.LANCHOR9
	str	wzr, [x0,1688]
	mov	w0, 27698
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	bne	.L3183
	str	wzr, [x1,20]
	str	wzr, [x1,24]
	b	.L3184
.L3183:
	mov	w0, 1
	str	w2, [x1,24]
	str	w0, [x1,20]
.L3184:
	mov	w0, 1
	mov	x19, -2
	bl	StorageSysDataStore
	cmn	w0, #1
	bne	.L3222
	b	.L3153
.L3148:
	adrp	x0, .LC268
	add	x0, x0, :lo12:.LC268
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3168
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3168
	mov	w0, 30224
	adrp	x1, .LANCHOR9
	movk	w0, 0x4004, lsl 16
	uxtw	x2, w2
	cmp	w19, w0
	add	x1, x1, :lo12:.LANCHOR9
	add	x0, x29, 64
	bne	.L3185
	ldr	x1, [x1,1696]
	b	.L3221
.L3185:
	ldr	x1, [x1,1704]
.L3221:
	add	x1, x1, 8
.L3230:
	bl	memcpy
.L3231:
	mov	x0, x20
	add	x1, x29, 56
	mov	x2, 520
	b	.L3217
.L3145:
	adrp	x0, .LC269
	add	x0, x0, :lo12:.LC269
	bl	printk
	add	x0, x29, 56
	mov	x1, x20
	mov	x2, 520
	bl	rk_copy_from_user
	cbnz	x0, .L3171
	ldr	w1, [x29,56]
	mov	w0, 17750
	movk	w0, 0x444e, lsl 16
	cmp	w1, w0
	bne	.L3168
	ldr	w2, [x29,60]
	cmp	w2, 504
	bhi	.L3168
	mov	w0, 30225
	add	w2, w2, 8
	movk	w0, 0x4004, lsl 16
	cmp	w19, w0
	adrp	x19, .LANCHOR9
	add	x19, x19, :lo12:.LANCHOR9
	bne	.L3187
	ldr	x0, [x19,1696]
	add	x1, x29, 56
	bl	memcpy
	mov	w0, 2
	ldr	x1, [x19,1696]
	b	.L3223
.L3187:
	ldr	x0, [x19,1704]
	add	x1, x29, 56
	bl	memcpy
	ldr	x1, [x19,1704]
	mov	w0, 3
.L3223:
	bl	StorageSysDataStore
	uxtw	x19, w0
	b	.L3153
.L3222:
	mov	x19, 0
.L3153:
	adrp	x0, .LC270
	mov	x1, x19
	add	x0, x0, :lo12:.LC270
	bl	printk
	mov	x0, x19
	b	.L3126
.L3188:
	mov	x0, -22
.L3126:
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
	stp	x29, x30, [sp, -48]!
	mov	w2, 512
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR9
	add	x19, x20, :lo12:.LANCHOR9
	stp	x21, x22, [sp,32]
	ldr	x1, [x19,1120]
	str	x1, [x19,1168]
	add	x0, x1, 512
	str	x0, [x19,1152]
	add	x0, x1, 1024
	add	x1, x1, 1536
	str	x0, [x19,1696]
	add	x0, x19, 1176
	str	xzr, [x19,1144]
	str	x1, [x19,1704]
	bl	ftl_memcpy
	ldr	x21, [x19,1168]
	str	wzr, [x19,1160]
	str	wzr, [x19,1688]
	ldr	w0, [x21,16]
	ldr	w22, [x21,508]
	str	w0, [x19,1164]
	cbz	w22, .L3237
	mov	x0, x21
	mov	w1, 508
	bl	js_hash
	cmp	w22, w0
	beq	.L3237
	adrp	x0, .LC271
	str	wzr, [x21,16]
	add	x0, x0, :lo12:.LC271
	str	wzr, [x19,1164]
	bl	printk
.L3237:
	add	x0, x20, :lo12:.LANCHOR9
	ldr	w1, [x0,1164]
	cbz	w1, .L3238
	mov	w1, 5161
	movk	w1, 0xc059, lsl 16
	str	w1, [x0,1160]
.L3238:
	add	x20, x20, :lo12:.LANCHOR9
	mov	w0, 2
	ldr	x1, [x20,1696]
	bl	StorageSysDataLoad
	ldr	x1, [x20,1704]
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
	str	x0, [x1,1712]
	cbz	x0, .L3247
	mov	w23, 0
	adrp	x24, .LC272
	mov	w19, w23
	mov	w22, w23
	add	x24, x24, :lo12:.LC272
.L3251:
	add	x20, x21, :lo12:.LANCHOR9
	lsl	w0, w22, 7
	mov	w1, 128
	ldr	x2, [x20,1712]
	bl	FlashBootVendorRead
	cbnz	w0, .L3248
	ldr	x3, [x20,1712]
	mov	x0, x24
	add	x2, x3, 61440
	ldr	w1, [x3]
	ldr	w3, [x3,4]
	ldr	w2, [x2,4092]
	bl	printk
	ldr	x20, [x20,1712]
	mov	w0, 22084
	movk	w0, 0x524b, lsl 16
	ldr	w1, [x20]
	cmp	w1, w0
	bne	.L3249
	ldr	w0, [x20,4]
	cmp	w19, w0
	bcs	.L3249
	add	x1, x20, 61440
	ldr	w1, [x1,4092]
	cmp	w1, w0
	csel	w23, w23, w22, ne
	csel	w19, w19, w0, ne
.L3249:
	cbnz	w22, .L3259
	mov	w22, 1
	b	.L3251
.L3259:
	cbz	w19, .L3252
	lsl	w0, w23, 7
	mov	w1, 128
	mov	x2, x20
	bl	FlashBootVendorRead
	mov	w19, w0
	cbz	w0, .L3247
	b	.L3248
.L3252:
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
	b	.L3247
.L3248:
	add	x21, x21, :lo12:.LANCHOR9
	mov	w19, -1
	ldr	x0, [x21,1712]
	bl	kfree
	str	xzr, [x21,1712]
.L3247:
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
	adrp	x3, .LANCHOR9+1712
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	x5, [x3,#:lo12:.LANCHOR9+1712]
	mov	w3, -1
	str	x19, [sp,16]
	cbz	x5, .L3261
	ldrh	w3, [x5,10]
	mov	x4, 0
.L3262:
	cmp	w4, w3
	mov	w6, w4
	bcs	.L3266
	add	x7, x5, x4, lsl 3
	add	x4, x4, 1
	ldrh	w7, [x7,16]
	cmp	w7, w0
	bne	.L3262
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
	b	.L3261
.L3266:
	mov	w3, -1
.L3261:
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
	adrp	x0, .LANCHOR9+1712
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	ldr	x19, [x0,#:lo12:.LANCHOR9+1712]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w26, w2
	mov	x28, x1
	mov	w2, -1
	cbz	x19, .L3268
	add	w20, w26, 63
	ldrh	w3, [x19,10]
	ldrh	w23, [x19,8]
	and	w20, w20, -64
	mov	x2, 0
.L3269:
	cmp	w2, w3
	mov	w4, w2
	bcs	.L3284
	add	x0, x19, x2, lsl 3
	add	x2, x2, 1
	ldrh	w0, [x0,16]
	cmp	w0, w6
	bne	.L3269
	add	x21, x19, x4, uxtw 3
	ldrh	w24, [x21,20]
	add	x5, x19, 1024
	add	w24, w24, 63
	and	w24, w24, -64
	cmp	w26, w24
	bls	.L3270
	ldrh	w0, [x19,14]
	mov	w2, -1
	cmp	w0, w20
	bcc	.L3268
	ldrh	w21, [x21,18]
	add	w22, w3, w2
.L3271:
	cmp	w4, w22
	uxtw	x3, w4
	bcs	.L3285
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
	b	.L3271
.L3285:
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
	b	.L3283
.L3270:
	ldrh	w0, [x21,18]
	mov	x1, x28
	uxtw	x2, w26
	add	x0, x5, x0
	bl	memcpy
	strh	w26, [x21,20]
	b	.L3283
.L3284:
	ldrh	w4, [x19,14]
	mov	w2, -1
	cmp	w4, w20
	bcc	.L3268
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
.L3283:
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
.L3268:
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
	cbz	x0, .L3287
	mov	w1, 30209
	movk	w1, 0x4004, lsl 16
	cmp	w22, w1
	beq	.L3289
	mov	w2, 30210
	movk	w2, 0x4004, lsl 16
	cmp	w22, w2
	beq	.L3290
	b	.L3301
.L3289:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3301
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	beq	.L3292
.L3293:
	mov	x20, -1
	b	.L3288
.L3292:
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_read
	cmn	w0, #1
	beq	.L3293
	uxth	x2, w0
	strh	w0, [x19,6]
	mov	x1, x19
	mov	x0, x21
	add	x2, x2, 8
	mov	x20, -14
	bl	rk_copy_to_user
	cmp	x0, xzr
	csel	x20, xzr, x20, eq
	b	.L3288
.L3290:
	mov	x1, x21
	mov	x2, 8
	bl	rk_copy_from_user
	cbnz	x0, .L3301
	ldr	w1, [x19]
	mov	w0, 17745
	movk	w0, 0x5652, lsl 16
	cmp	w1, w0
	bne	.L3293
	ldrh	w3, [x19,6]
	cmp	w3, 4087
	bhi	.L3293
	add	w2, w3, 8
	mov	x0, x19
	mov	x1, x21
	sxtw	x2, w2
	bl	rk_copy_from_user
	cbnz	x0, .L3301
	ldrh	w0, [x19,4]
	add	x1, x19, 8
	ldrh	w2, [x19,6]
	bl	rk_ftl_vendor_write
	sxtw	x20, w0
	b	.L3288
.L3301:
	mov	x20, -14
.L3288:
	mov	x0, x19
	bl	kfree
.L3287:
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
	.global	vpn_mulit_value
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
	.global	gc_free_slc_sblk_th
	.global	gc_slc_mode_vpn_th
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
	.type	__func__.18805, %object
	.size	__func__.18805, 22
__func__.18805:
	.string	"nand_flash_print_info"
	.zero	2
	.type	__func__.18885, %object
	.size	__func__.18885, 11
__func__.18885:
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
	.type	__func__.18413, %object
	.size	__func__.18413, 28
__func__.18413:
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
	.type	__func__.18388, %object
	.size	__func__.18388, 26
__func__.18388:
	.string	"flash_erase_duplane_block"
	.zero	6
	.type	__func__.18399, %object
	.size	__func__.18399, 21
__func__.18399:
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
	.type	__func__.18464, %object
	.size	__func__.18464, 22
__func__.18464:
	.string	"flash_start_page_read"
	.zero	2
	.type	__func__.19059, %object
	.size	__func__.19059, 13
__func__.19059:
	.string	"buf_add_tail"
	.zero	3
	.type	__func__.19072, %object
	.size	__func__.19072, 10
__func__.19072:
	.string	"buf_alloc"
	.zero	6
	.type	__func__.19086, %object
	.size	__func__.19086, 16
__func__.19086:
	.string	"buf_remove_free"
	.type	__func__.19326, %object
	.size	__func__.19326, 13
__func__.19326:
	.string	"ftl_read_buf"
	.zero	3
	.type	__func__.19561, %object
	.size	__func__.19561, 12
__func__.19561:
	.string	"gc_add_sblk"
	.zero	4
	.type	__func__.19651, %object
	.size	__func__.19651, 19
__func__.19651:
	.string	"gc_write_completed"
	.zero	5
	.type	__func__.19994, %object
	.size	__func__.19994, 14
__func__.19994:
	.string	"ftl_free_sblk"
	.zero	2
	.type	__func__.19702, %object
	.size	__func__.19702, 16
__func__.19702:
	.string	"gc_free_src_blk"
	.type	__func__.20028, %object
	.size	__func__.20028, 18
__func__.20028:
	.string	"ftl_alloc_sys_blk"
	.zero	6
	.type	__func__.20038, %object
	.size	__func__.20038, 17
__func__.20038:
	.string	"ftl_free_sys_blk"
	.zero	7
	.type	__func__.20141, %object
	.size	__func__.20141, 23
__func__.20141:
	.string	"ftl_get_ppa_from_index"
	.zero	1
	.type	__func__.20181, %object
	.size	__func__.20181, 22
__func__.20181:
	.string	"ftl_get_new_free_page"
	.zero	2
	.type	__func__.20192, %object
	.size	__func__.20192, 22
__func__.20192:
	.string	"ftl_ext_alloc_new_blk"
	.zero	2
	.type	__func__.19270, %object
	.size	__func__.19270, 14
__func__.19270:
	.string	"ftl_write_buf"
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.20612, %object
	.size	__func__.20612, 17
__func__.20612:
	.string	"pm_alloc_new_blk"
	.zero	7
	.type	__func__.20640, %object
	.size	__func__.20640, 21
__func__.20640:
	.string	"pm_select_ram_region"
	.zero	3
	.type	__func__.18795, %object
	.size	__func__.18795, 20
__func__.18795:
	.string	"flash_die_info_init"
	.zero	4
	.type	__func__.19207, %object
	.size	__func__.19207, 17
__func__.19207:
	.string	"lpa_rebuild_hash"
	.zero	7
	.type	__func__.20576, %object
	.size	__func__.20576, 13
__func__.20576:
	.string	"pm_free_sblk"
	.zero	3
	.type	__func__.19109, %object
	.size	__func__.19109, 21
__func__.19109:
	.string	"flash_into_data_init"
	.zero	3
	.type	__func__.18344, %object
	.size	__func__.18344, 19
__func__.18344:
	.string	"flash_read_page_en"
	.zero	5
	.type	__func__.19098, %object
	.size	__func__.19098, 20
__func__.19098:
	.string	"flash_info_blk_init"
	.zero	4
	.type	__func__.18479, %object
	.size	__func__.18479, 25
__func__.18479:
	.string	"flash_complete_page_read"
	.zero	7
	.type	__func__.19604, %object
	.size	__func__.19604, 21
__func__.19604:
	.string	"gc_check_data_one_wl"
	.zero	3
	.type	__func__.18438, %object
	.size	__func__.18438, 26
__func__.18438:
	.string	"flash_start_tlc_page_prog"
	.zero	6
	.type	__func__.18451, %object
	.size	__func__.18451, 22
__func__.18451:
	.string	"flash_start_page_prog"
	.zero	2
	.type	__func__.20503, %object
	.size	__func__.20503, 15
__func__.20503:
	.string	"sblk_prog_page"
	.zero	1
	.type	__func__.18373, %object
	.size	__func__.18373, 19
__func__.18373:
	.string	"flash_prog_page_en"
	.zero	5
	.type	__func__.20328, %object
	.size	__func__.20328, 14
__func__.20328:
	.string	"ftl_prog_page"
	.zero	2
	.type	__func__.19945, %object
	.size	__func__.19945, 15
__func__.19945:
	.string	"ftl_info_flush"
	.zero	1
	.type	__func__.20217, %object
	.size	__func__.20217, 19
__func__.20217:
	.string	"ftl_ext_info_flush"
	.zero	5
	.type	__func__.20229, %object
	.size	__func__.20229, 18
__func__.20229:
	.string	"ftl_ext_info_init"
	.zero	6
	.type	__func__.20274, %object
	.size	__func__.20274, 15
__func__.20274:
	.string	"ftl_low_format"
	.zero	1
	.type	__func__.20318, %object
	.size	__func__.20318, 23
__func__.20318:
	.string	"ftl_re_low_format_test"
	.zero	1
	.type	__func__.20115, %object
	.size	__func__.20115, 23
__func__.20115:
	.string	"ftl_open_sblk_recovery"
	.zero	1
	.type	__func__.20622, %object
	.size	__func__.20622, 14
__func__.20622:
	.string	"pm_write_page"
	.zero	2
	.type	__func__.19131, %object
	.size	__func__.19131, 17
__func__.19131:
	.string	"flash_info_flush"
	.zero	7
	.type	__func__.18849, %object
	.size	__func__.18849, 16
__func__.18849:
	.string	"nand_flash_init"
	.type	__func__.20096, %object
	.size	__func__.20096, 16
__func__.20096:
	.string	"ftl_sysblk_dump"
	.type	__func__.20562, %object
	.size	__func__.20562, 16
__func__.20562:
	.string	"load_l2p_region"
	.type	__func__.20596, %object
	.size	__func__.20596, 6
__func__.20596:
	.string	"pm_gc"
	.zero	2
	.type	__func__.19318, %object
	.size	__func__.19318, 15
__func__.19318:
	.string	"ftl_read_ahead"
	.zero	1
	.type	__func__.19638, %object
	.size	__func__.19638, 22
__func__.19638:
	.string	"gc_update_l2p_map_new"
	.zero	2
	.type	__func__.19796, %object
	.size	__func__.19796, 20
__func__.19796:
	.string	"gc_scan_static_data"
	.zero	4
	.type	__func__.19852, %object
	.size	__func__.19852, 18
__func__.19852:
	.string	"gc_block_vpn_scan"
	.zero	6
	.type	__func__.20072, %object
	.size	__func__.20072, 14
__func__.20072:
	.string	"ftl_sblk_dump"
	.zero	2
	.type	__func__.19780, %object
	.size	__func__.19780, 18
__func__.19780:
	.string	"gc_search_src_blk"
	.zero	6
	.type	__func__.19364, %object
	.size	__func__.19364, 10
__func__.19364:
	.string	"zftl_read"
	.zero	6
	.type	__func__.19431, %object
	.size	__func__.19431, 10
__func__.19431:
	.string	"vpn_check"
	.zero	6
	.type	__func__.19461, %object
	.size	__func__.19461, 15
__func__.19461:
	.string	"ftl_info_check"
	.zero	1
	.type	__func__.19747, %object
	.size	__func__.19747, 16
__func__.19747:
	.string	"gc_scan_src_blk"
	.type	__func__.20162, %object
	.size	__func__.20162, 19
__func__.20162:
	.string	"ftl_update_l2p_map"
	.zero	5
	.type	__func__.19259, %object
	.size	__func__.19259, 17
__func__.19259:
	.string	"ftl_write_commit"
	.zero	7
	.type	__func__.19680, %object
	.size	__func__.19680, 16
__func__.19680:
	.string	"gc_do_copy_back"
	.type	__func__.19390, %object
	.size	__func__.19390, 13
__func__.19390:
	.string	"_ftl_discard"
	.zero	3
	.type	__func__.19881, %object
	.size	__func__.19881, 11
__func__.19881:
	.string	"zftl_do_gc"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"...%s enter...\n"
.LC1:
	.string	"No.0 FLASH ID: %x %x %x %x %x %x\n"
.LC2:
	.string	"DiePerChip: %x\n"
.LC3:
	.string	"SectPerPage: %x\n"
.LC4:
	.string	"PagePerBlk: %x\n"
.LC5:
	.string	"Cell: %x\n"
.LC6:
	.string	"PlanePerDie: %x\n"
.LC7:
	.string	"BlkPerPlane: %x\n"
.LC8:
	.string	"die gap: %x\n"
.LC9:
	.string	"lsbMode: %x\n"
.LC10:
	.string	"ReadRetryMode: %x\n"
.LC11:
	.string	"ecc: %x\n"
.LC12:
	.string	"idb ecc: %x\n"
.LC13:
	.string	"AccessFreq: %x\n"
.LC14:
	.string	"OptMode: %x\n"
.LC15:
	.string	"g_nand_max_die: %x\n"
.LC16:
	.string	"Cache read enable: %x\n"
.LC17:
	.string	"Cache random read enable: %x\n"
.LC18:
	.string	"Cache prog enable: %x\n"
.LC19:
	.string	"multi read enable: %x\n"
.LC20:
	.string	"multi prog enable: %x\n"
.LC21:
	.string	"interleave enable: %x\n"
.LC22:
	.string	"read retry enable: %x\n"
.LC23:
	.string	"randomizer enable: %x\n"
.LC24:
	.string	"SDR enable: %x\n"
.LC25:
	.string	"ONFI enable: %x\n"
.LC26:
	.string	"TOGGLE enable: %x\n"
.LC27:
	.string	"g_flash_slc_mode: %x\n"
.LC28:
	.string	"MultiPlaneProgCmd: %x %x\n"
.LC29:
	.string	"MultiPlaneReadCmd: %x %x\n"
.LC30:
	.string	"g_flash_toggle_mode_en: %x\n"
.LC31:
	.string	"...%s enter... %p\n"
.LC32:
	.string	"0:%x %x %x %x %x\n"
.LC33:
	.string	"g_nandc_ver...%d\n"
.LC34:
	.string	"\n!!!!! error @ func:%s - line:%d\n"
.LC35:
	.string	"nand sdr mode %x\n"
.LC36:
	.string	"nand ddr mode %x\n"
.LC37:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC38:
	.string	"bad block test:%x %x\n"
.LC39:
	.string	"flash_erase_duplane_block %x %x %x\n"
.LC40:
	.string	"flash_erase_duplane_block pageadd = %x status = %x\n"
.LC41:
	.string	"flash_erase_block %x %x\n"
.LC42:
	.string	"flash_erase_block block = %x status = %x\n"
.LC43:
	.string	"erase done: %x\n"
.LC44:
	.string	"bch%d: %d %d\n"
.LC45:
	.string	"flash_mask_bad_block %d %d\n"
.LC46:
	.string	"%s\n"
.LC47:
	.string	"FTL version: 6.0.3 20180211"
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
	.string	"TBW = %d MB\n"
.LC63:
	.string	"TBR = %d MB\n"
.LC64:
	.string	"POC = %d\n"
.LC65:
	.string	"PLC = %d\n"
.LC66:
	.string	"sys run time = %d S\n"
.LC67:
	.string	"prog err = %d\n"
.LC68:
	.string	"read err = %d\n"
.LC69:
	.string	"GC XLC page = %d\n"
.LC70:
	.string	"GC SLC page = %d\n"
.LC71:
	.string	"discard page = 0x%x\n"
.LC72:
	.string	"version = %d\n"
.LC73:
	.string	"acblk = 0x%x %d %d\n"
.LC74:
	.string	"tmblk = 0x%x %d %d\n"
.LC75:
	.string	"gcblk = 0x%x %d %d\n"
.LC76:
	.string	"slc ec = %d, %d, %d, %d, %d\n"
.LC77:
	.string	"xlc ec = %d, %d, %d, %d, %d\n"
.LC78:
	.string	"gc_add_sblk = %d, %d, %d, %d, %d, %d\n"
.LC79:
	.string	"status: %x, ppa: %x\n"
.LC80:
	.string	"gc_write_completed: %x  %x  %x %x\n"
.LC81:
	.string	"%d gc_free_temp_buf buf id= %x\n"
.LC82:
	.string	"gc_static_wearleveling: slc blk: %d, tlc blk: %d avg slc ec: %d, avg tlc ec: %d \n"
.LC83:
	.string	"gc_static_wearleveling: min slc ec: %d, min tlc ec: %d max slc ec: %d, max tlc ec: %d \n"
.LC84:
	.string	"gc: b:%x,p:%x,i:%x; free buf=%d %d free slc th: %d\n"
.LC85:
	.string	"ftl_alloc_sblk %x, %d %d %d\n"
.LC86:
	.string	"gc_free_src_blk = %x, vpn = %d\n"
.LC87:
	.string	"gc_free_src_blk %x, %d\n"
.LC88:
	.string	"ftl_vpn_decrement %x = %d\n"
.LC89:
	.string	"mask bad block:cs %x block: %x\n"
.LC90:
	.string	"free blk: s:%x,t:%x,m:%x, data blk:s:%x,%x,t%x vpn: s:%x t:%x, gc th: %x\n"
.LC91:
	.string	"totle w: %d MB,r: %d MB %d dv:0x%X,poc:%d\n"
.LC92:
	.string	"gc xlc page: %d,gc slc page: %d, tmp w: %d MB\n"
.LC93:
	.string	"slc ec: %d,%d,%d,%d,%d,tlc ec: %d,%d,%d,%d,%d\n"
.LC94:
	.string	"ftl prog error =%x, lpa = %x, ppa= %x\n"
.LC95:
	.string	"dump_sblk_queue: %d\n"
.LC96:
	.string	"buf id= %d state = %d ppa = %x\n"
.LC97:
	.string	"blk %x is bad block\n"
.LC98:
	.string	"pm_alloc_new_blk: %x %x %x %x\n"
.LC99:
	.string	"%s %d %d\n"
.LC100:
	.string	"ftl_free_no_use_map_blk %x %x %x %d\n"
.LC101:
	.string	"...%d @ %s\n"
.LC102:
	.string	"rk_ftl_de_init %x\n"
.LC103:
	.string	"%s %p:0x%x:"
.LC104:
	.string	"%x "
.LC105:
	.string	""
.LC106:
	.string	"otp error! %d"
.LC107:
	.string	"rr"
.LC108:
	.string	"%d mtrans_cnt = %d page_num = %d\n"
.LC109:
	.string	"nandc:"
.LC110:
	.string	"%d flReg.d32=%x %x\n"
.LC111:
	.string	"nandc_xfer_done read error %x\n"
.LC112:
	.string	"dqs data abort %x\n"
.LC113:
	.string	"xfer error %x\n"
.LC114:
	.string	"flash_read_page %x %x %x\n"
.LC115:
	.string	"micron %d row=%x,count %d,status=%d\n"
.LC116:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC117:
	.string	"toshiba SRR %d row=%x, status=%d\n"
.LC118:
	.string	"toshiba TRR %d row=%x, status=%d\n"
.LC119:
	.string	"toshiba RR %d row=%x,count %d,status=%d\n"
.LC120:
	.string	"hynix RR %d row=%x, count %d, status=%d\n"
.LC121:
	.string	"%d flash_ddr_tunning_read %x ecc=%d\n"
.LC122:
	.string	"sync para %d\n"
.LC123:
	.string	"DDR mode Read error %x %x\n"
.LC124:
	.string	"flash_read_page_en %x %x\n"
.LC125:
	.string	"flash_read_page_en %x %x error_ecc %d %d\n"
.LC126:
	.string	"flash_get_last_written_page: %x %x %x\n"
.LC127:
	.string	"...%d @ %s %d %p\n"
.LC128:
	.string	"no sys info %x\n"
.LC129:
	.string	"read page: %x %x %x %x\n"
.LC130:
	.string	"flash_complete_page_read %x %x error_ecc %d %d\n"
.LC131:
	.string	"set buf %d,status = %x, ppa = %x lun state = %d\n"
.LC132:
	.string	"1ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC133:
	.string	"sblk_prog_page ppa = %x, count = %d\n"
.LC134:
	.string	"flash_prog_page %x %x %x\n"
.LC135:
	.string	"flash_prog_page page_addr = %x status = %x\n"
.LC136:
	.string	"flash_prog_page_en:%x %x\n"
.LC137:
	.string	"spare"
.LC138:
	.string	"data"
.LC139:
	.string	"write error: %x\n"
.LC140:
	.string	"low format %d\n"
.LC141:
	.string	"g_ftl_info_blk blk = %x, page = %x version = %d\n"
.LC142:
	.string	"%d %x @%d %x\n"
.LC143:
	.string	"ftl_info_blk_init %d %d %x\n"
.LC144:
	.string	"low format %d %d %d %d\n"
.LC145:
	.string	"re low formaet %d\n"
.LC146:
	.string	"saved_active_page  = %x\n"
.LC147:
	.string	"saved_active_plane = %x\n"
.LC148:
	.string	"sblk = %x\n"
.LC149:
	.string	"phy_blk = %x %x\n"
.LC150:
	.string	"blk= %x, page=%x, ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC151:
	.string	"sblk = %x, vpn0 = %d, vpn1 = %d\n"
.LC152:
	.string	"dump write = %x\n"
.LC153:
	.string	"pm_write_page write error: %x\n"
.LC154:
	.string	"finfo:"
.LC155:
	.string	"flash_info_flush id = %x, page = %x\n"
.LC156:
	.string	"sys_info_flush error:%x\n"
.LC157:
	.string	"l2p:"
.LC158:
	.string	"g_ftl_info_blk blk:0x%x, index:0x%x, page:0x%x\n"
.LC159:
	.string	"ftl_ext_info_blk blk:0x%x, page:0x%x\n"
.LC160:
	.string	"ac_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC161:
	.string	"tmp_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC162:
	.string	"gc_blk:0x%x, page:0x%x, index:0x%x, free:0x%x, page_index:0x%x\n"
.LC163:
	.string	"lpa:"
.LC164:
	.string	"vpn:"
.LC165:
	.string	"sblk:"
.LC166:
	.string	"lpa_hash:"
.LC167:
	.string	"lpa_hash_index:"
.LC168:
	.string	"%s w error lpn = %x, max ppa = %d\n"
.LC169:
	.string	"region_id = %d, pm_max_region = %d\n"
.LC170:
	.string	"load_l2p_region = %x,%x,%x, %x\n"
.LC171:
	.string	"pm_ppa:"
.LC172:
	.string	"data:"
.LC173:
	.string	"spare:"
.LC174:
	.string	"pm_init posr %x %x %x\n"
.LC175:
	.string	"pm_init recovery %x %x %x\n"
.LC176:
	.string	"pm_log2phys  lpn = %d, max lpn = %d\n"
.LC177:
	.string	"ftl_read_ahead refresh =%x, lpa = %x, ppa= %x\n"
.LC178:
	.string	"id=%d, lpa = %x, ppa = %x spare = %x %x %x %x\n"
.LC179:
	.string	"gc_update_l2p_map_new sblk %x\n"
.LC180:
	.string	"gc_update_l2p_map_new: %x %x %x\n"
.LC181:
	.string	"0lpa: %x %x %x\n"
.LC182:
	.string	"lpa: %x %x %x\n"
.LC183:
	.string	"gc_update_l2p_map_new: %x vpn = %x vpn1 = %x done\n"
.LC184:
	.string	"gc_block_vpn_scan = %x, s vpn0 = %d, c vpn1 = %d f:%d\n"
.LC185:
	.string	"ftl_sblk_dump = %x %x %x %x\n"
.LC186:
	.string	"page_addr = %x, lpa=%x vpn = %d\n"
.LC187:
	.string	"index= %x, lpa=%x\n"
.LC188:
	.string	"block = %x, vpn=%x check vpn = %d\n"
.LC189:
	.string	"gc_search_src_blk = %d, %d, %d\n"
.LC190:
	.string	"gc_search_src_blk = %d, %d, %d %d\n"
.LC191:
	.string	"vpn gc = %d, id = %d\n"
.LC192:
	.string	"gc_search_src_blk count= %d slc_min_vpc = %d xlc_min_vpc = %d\n"
.LC193:
	.string	"ftl_read refresh =%x, lpa = %x, ppa= %x\n"
.LC194:
	.string	"acblk = %x, tempblk = %x, gc_des = %x\n"
.LC195:
	.string	"acblk = %x, free page = %x, vpn = %x %x\n"
.LC196:
	.string	"tmp_blk = %x, free page = %x, vpn = %x %x\n"
.LC197:
	.string	"vpn_check lpa = %x, ppa = %x, blk = %x\n"
.LC198:
	.string	"l2p"
.LC199:
	.string	"vpn_check %x = c %x  s %x function: %x\n"
.LC200:
	.string	"vpn_check blk = %x fix vpc\n"
.LC201:
	.string	"...%s exit...%d ms\n"
.LC202:
	.string	"free blk vpn error: %x %x\n"
.LC203:
	.string	"data blk: %d %d %d\n"
.LC204:
	.string	"free blk: %d %d %d\n"
.LC205:
	.string	"gc_scan_src_blk = %x, vpn = %d\n"
.LC206:
	.string	"gc_scan_src_blk = %x, s vpn0 = %d, c vpn1 = %d\n"
.LC207:
	.string	"ftl_scan_all_data = %x\n"
.LC208:
	.string	"scan lpa = %x ppa= %x\n"
.LC209:
	.string	"lba = %x,addr= %x, ststus = %x, spare= %x %x %x %x data=%x %x\n"
.LC210:
	.string	"lba = %x, id= %x, index = %x hash ppa = %x\n"
.LC211:
	.string	"0pm:"
.LC212:
	.string	"1pm:"
.LC213:
	.string	"ppa = %x, status = %x, data:%x %x %x %x, spare: %x %x %x %x\n"
.LC214:
	.string	"gc_lpa:"
.LC215:
	.string	"gc_ppa:"
.LC216:
	.string	"ppa = %x, status = %x, %x %x spare: %x %x %x %x\n"
.LC217:
	.string	"gc_recovery"
.LC218:
	.string	"gc_recovery: %x vpn = %x\n"
.LC219:
	.string	"_c_user_data_density := %d\n"
.LC220:
	.string	"_c_totle_phy_density := %d\n"
.LC221:
	.string	"_c_totle_log_page := %d\n"
.LC222:
	.string	"_c_totle_data_density := %d\n"
.LC223:
	.string	"_c_ftl_pm_page_num := %d\n"
.LC224:
	.string	"_c_ftl_byte_pre_page := %d\n"
.LC225:
	.string	"_c_max_pm_sblk := %d\n"
.LC226:
	.string	"_min_slc_super_block := %d\n"
.LC227:
	.string	"_max_xlc_super_block := %d\n"
.LC228:
	.string	"gp_ftl_ext_info %p %p %p\n"
.LC229:
	.string	"flash info size: %d %d %d\n"
.LC230:
	.string	"ftl_init %x\n"
.LC231:
	.string	"ftl_update_l2p_map: %x %x %x\n"
.LC232:
	.string	"ftl_update_l2p_map"
.LC233:
	.string	"lpa_tbl:"
.LC234:
	.string	"sblk %x vpn: %d %d\n"
.LC235:
	.string	"%d read error: ppa:%x, lpa:%x, status:%x\n"
.LC236:
	.string	"gc page in buf: lpa %x ppa = %x pageindex= %x\n"
.LC237:
	.string	"gc_do_copy_back: lpa %x des_ppa = %x %x gc_ppa= %x page_index= %d\n"
.LC238:
	.string	"gc_do_copy_back: %x %x %x %x gc_ppa:%x %x\n"
.LC239:
	.string	"%d prog_step: %x %x buf id= %x ppa = %x hash=%x id = %x plane = %x lpa=%x\n"
.LC240:
	.string	"ftl_discard:(%x, %x, %x, %x)\n"
.LC241:
	.string	"gc %d: %d %d %d %d %d %d\n"
.LC242:
	.string	"GC_STATE_SCAN_ALL_PAGE = %x, vpn0 = %d, vpn1 = %d\n"
.LC243:
	.string	"gc free %x, %d\n"
.LC244:
	.string	"id_block_prog_msb_ff_data slc page = %d pageadd=%x %x\n"
.LC245:
	.string	"write_idblock fix data %x %x\n"
.LC246:
	.string	"1 write_idblock fix data %x %x %x\n"
.LC247:
	.string	"READ_SECTOR_IO\n"
.LC248:
	.string	"rk_copy_from_user error\n"
.LC249:
	.string	"READ_SECTOR_IO %x %x\n"
.LC250:
	.string	"rk_copy_to_user error\n"
.LC251:
	.string	"WRITE_SECTOR_IO\n"
.LC252:
	.string	"WRITE_SECTOR_IO %x %x\n"
.LC253:
	.string	"END_WRITE_SECTOR_IO\n"
.LC254:
	.string	"END_WRITE_SECTOR_IO %x %x\n"
.LC255:
	.string	"GET_FLASH_INFO_IO\n"
.LC256:
	.string	"GET_BAD_BLOCK_IO\n"
.LC257:
	.string	"GET_LOCK_FLAG_IO\n"
.LC258:
	.string	"GET_PUBLIC_KEY_IO\n"
.LC259:
	.string	"RKNAND_GET_DRM_KEY\n"
.LC260:
	.string	"RKNAND_STORE_DRM_KEY\n"
.LC261:
	.string	"RKNAND_DIASBLE_SECURE_BOOT\n"
.LC262:
	.string	"RKNAND_ENASBLE_SECURE_BOOT\n"
.LC263:
	.string	"RKNAND_GET_SN_SECTOR\n"
.LC264:
	.string	"RKNAND_LOADER_UNLOCK\n"
.LC265:
	.string	"RKNAND_LOADER_STATUS\n"
.LC266:
	.string	"RKNAND_LOADER_LOCK\n"
.LC267:
	.string	"LockKey not match %d\n"
.LC268:
	.string	"RKNAND_GET_VENDOR_SECTOR\n"
.LC269:
	.string	"RKNAND_STORE_VENDOR_SECTOR\n"
.LC270:
	.string	"return ret = %lx\n"
.LC271:
	.string	"secureBootEn check error\n"
.LC272:
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
	.size	zftl_nand_flash_para_tbl, 768
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
	.byte	2
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
	.byte	2
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
	.byte	2
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
	.byte	0
	.byte	0
	.byte	0
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
	.byte	0
	.byte	0
	.byte	0
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
	.type	g_flash_slc_mode, %object
	.size	g_flash_slc_mode, 1
g_flash_slc_mode:
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
	.type	g_flash_cur_mode, %object
	.size	g_flash_cur_mode, 1
g_flash_cur_mode:
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
	.zero	1
	.type	_c_totle_phy_density, %object
	.size	_c_totle_phy_density, 4
_c_totle_phy_density:
	.zero	4
	.type	_c_totle_log_page, %object
	.size	_c_totle_log_page, 4
_c_totle_log_page:
	.zero	4
	.zero	4
	.type	gp_ftl_ext_info, %object
	.size	gp_ftl_ext_info, 8
gp_ftl_ext_info:
	.zero	8
	.type	gp_ftl_info, %object
	.size	gp_ftl_info, 8
gp_ftl_info:
	.zero	8
	.type	write_buf_head, %object
	.size	write_buf_head, 1
write_buf_head:
	.zero	1
	.type	write_buf_count, %object
	.size	write_buf_count, 1
write_buf_count:
	.zero	1
	.zero	2
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
	.zero	1
	.type	_c_ftl_blk_pre_plane, %object
	.size	_c_ftl_blk_pre_plane, 2
_c_ftl_blk_pre_plane:
	.zero	2
	.zero	4
	.type	ftl_sblk_vpn, %object
	.size	ftl_sblk_vpn, 8
ftl_sblk_vpn:
	.zero	8
	.type	gp_blk_info, %object
	.size	gp_blk_info, 8
gp_blk_info:
	.zero	8
	.type	g_gc_info, %object
	.size	g_gc_info, 328
g_gc_info:
	.zero	328
	.type	gc_slc_mode_vpn_th, %object
	.size	gc_slc_mode_vpn_th, 2
gc_slc_mode_vpn_th:
	.zero	2
	.zero	6
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
	.zero	1
	.type	gc_free_slc_sblk_th, %object
	.size	gc_free_slc_sblk_th, 2
gc_free_slc_sblk_th:
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
	.type	_c_mix_max_slc_ec_count, %object
	.size	_c_mix_max_slc_ec_count, 2
_c_mix_max_slc_ec_count:
	.zero	2
	.type	_c_mix_max_xlc_ec_count, %object
	.size	_c_mix_max_xlc_ec_count, 2
_c_mix_max_xlc_ec_count:
	.zero	2
	.type	_max_xlc_super_block, %object
	.size	_max_xlc_super_block, 2
_max_xlc_super_block:
	.zero	2
	.type	_max_slc_super_block, %object
	.size	_max_slc_super_block, 2
_max_slc_super_block:
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
	.zero	1
	.type	_c_ftl_page_pre_slc_blk, %object
	.size	_c_ftl_page_pre_slc_blk, 2
_c_ftl_page_pre_slc_blk:
	.zero	2
	.zero	4
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
	.zero	1
	.type	gc_tlc_mode_slc_vpn_th, %object
	.size	gc_tlc_mode_slc_vpn_th, 2
gc_tlc_mode_slc_vpn_th:
	.zero	2
	.type	gc_tlc_mode_tlc_vpn_th, %object
	.size	gc_tlc_mode_tlc_vpn_th, 2
gc_tlc_mode_tlc_vpn_th:
	.zero	2
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
	.zero	15
	.type	ftl_tmp_buffer, %object
	.size	ftl_tmp_buffer, 16384
ftl_tmp_buffer:
	.zero	16384
	.type	ftl_tmp_spare, %object
	.size	ftl_tmp_spare, 256
ftl_tmp_spare:
	.zero	256
	.type	write_commit_head, %object
	.size	write_commit_head, 1
write_commit_head:
	.zero	1
	.zero	3
	.type	ftl_flush_jiffies, %object
	.size	ftl_flush_jiffies, 4
ftl_flush_jiffies:
	.zero	4
	.type	g_flash_reversd_blks, %object
	.size	g_flash_reversd_blks, 1
g_flash_reversd_blks:
	.zero	1
	.zero	55
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
	.type	vpn_mulit_value, %object
	.size	vpn_mulit_value, 2304
vpn_mulit_value:
	.zero	2304
	.type	check_vpc_tbl, %object
	.size	check_vpc_tbl, 4352
check_vpc_tbl:
	.zero	4352
	.type	_c_totle_data_density, %object
	.size	_c_totle_data_density, 4
_c_totle_data_density:
	.zero	4
	.type	_min_slc_super_block, %object
	.size	_min_slc_super_block, 2
_min_slc_super_block:
	.zero	2
	.zero	2
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
