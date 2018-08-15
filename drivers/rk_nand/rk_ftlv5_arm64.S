/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-09-05
 * function: rk ftl v5 for rockchip soc base on arm v8 to support MLC NAND.
 */
	.cpu generic+fp+simd
	.file	"rk_ftlv5_arm_v8.S"
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
	.type	FlashReadFacBbtData.part.6, %function
FlashReadFacBbtData.part.6:
	stp	x29, x30, [sp, -32]!
	mov	w3, 0
	adrp	x5, .LANCHOR0
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w7, 1
	mov	x19, x0
	cbnz	w1, .L4
.L6:
	add	x0, x5, :lo12:.LANCHOR0
	ldr	w0, [x0,8]
	cmp	w3, w0
	bcs	.L4
	ubfx	x4, x3, 5, 11
	ldr	x0, [x5,#:lo12:.LANCHOR0]
	lsl	x4, x4, 2
	lsl	w6, w7, w3
	add	w3, w3, 1
	ldr	w1, [x0,x4]
	uxth	w3, w3
	orr	w6, w1, w6
	str	w6, [x0,x4]
	b	.L6
.L4:
	ldr	x1, [x5,#:lo12:.LANCHOR0]
	mov	x0, x19
	bl	ftl_memcpy
	mov	w2, 4
	adrp	x0, .LC0
	mov	x1, x19
	add	x0, x0, :lo12:.LC0
	mov	w3, w2
	bl	rknand_print_hex
	ldr	x19, [sp,16]
	mov	w0, 0
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadFacBbtData.part.6, .-FlashReadFacBbtData.part.6
	.align	2
	.type	ftl_set_blk_mode.part.17, %function
ftl_set_blk_mode.part.17:
	adrp	x1, .LANCHOR0+16
	uxth	w0, w0
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	orr	w0, w1, w0
	str	w0, [x3,x2]
	ret
	.size	ftl_set_blk_mode.part.17, .-ftl_set_blk_mode.part.17
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	mov	x5, x0
	adrp	x0, .LANCHOR0+24
	ldrb	w0, [x0,#:lo12:.LANCHOR0+24]
	cbz	w0, .L15
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L12
.L15:
	mov	x3, 0
.L13:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L18
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L13
	add	w0, w4, 1
	b	.L12
.L18:
	mov	w0, 0
.L12:
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, .LANCHOR0
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrb	w3, [x2,25]
	ldr	w2, [x2,28]
	mul	w2, w3, w2
	cmp	w1, w2
	cset	w1, cc
	cmp	w0, wzr
	cset	w0, eq
	and	w0, w1, w0
	eor	w0, w0, 1
	ret
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	2
	.global	FlashGetRandomizer
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR1
	and	w3, w1, 127
	add	x2, x2, :lo12:.LANCHOR1
	add	x29, sp, 0
	uxtb	w0, w0
	str	x19, [sp,16]
	ldrh	w19, [x2,w3,uxtw 1]
	adrp	x2, .LANCHOR0+32
	ldrb	w2, [x2,#:lo12:.LANCHOR0+32]
	cbz	w2, .L21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L21:
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	2
	.global	FlashSetRandomizer
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	stp	x29, x30, [sp, -48]!
	and	w2, w1, 127
	add	x29, sp, 0
	str	x21, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	stp	x19, x20, [sp,16]
	ldrh	w20, [x0,w2,uxtw 1]
	adrp	x2, .LANCHOR0
	add	x0, x2, :lo12:.LANCHOR0
	mov	x19, x2
	ldrb	w0, [x0,32]
	cbz	w0, .L28
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w20, -1073741824
	csel	w20, w1, w20, ne
.L28:
	add	x2, x19, :lo12:.LANCHOR0
	add	x2, x2, x21, sxtw 4
	ldr	x0, [x2,40]
	str	w20, [x0,336]
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	sbfiz	x5, x0, 4, 32
	add	x3, x3, 40
	lsr	w4, w1, 16
	add	x2, x3, x5
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x2,8]
	ldr	x3, [x3,x5]
	add	x2, x3, x2, lsl 8
	and	w3, w1, 255
	str	wzr, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w3, [x2,2052]
	lsr	w3, w1, 8
	str	w3, [x2,2052]
	mov	w3, 48
	str	w4, [x2,2052]
	str	w3, [x2,2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashReadCmd, .-FlashReadCmd
	.align	2
	.global	FlashReadDpDataOutCmd
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	sbfiz	x2, x0, 4, 32
	add	x4, x3, 40
	stp	x29, x30, [sp, -16]!
	add	x5, x4, x2
	add	x29, sp, 0
	ldr	x6, [x4,x2]
	lsr	w4, w1, 8
	ldrb	w2, [x3,120]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x5,8]
	and	w5, w1, 255
	add	x2, x6, x2, lsl 8
	bne	.L36
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L38
.L36:
	str	wzr, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	mov	w3, 5
	str	w3, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
.L38:
	mov	w3, 224
	str	w3, [x2,2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	flash_enter_slc_mode
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,136]
	cbz	w2, .L39
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 40
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 218
	str	w1, [x0,8]
.L39:
	ret
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	adrp	x1, .LANCHOR0
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w2, [x1,136]
	cbz	w2, .L44
	sbfiz	x0, x0, 4, 32
	add	x1, x1, 40
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 223
	str	w1, [x0,8]
.L44:
	ret
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	sbfiz	x5, x0, 4, 32
	add	x3, x3, 40
	lsr	w4, w1, 16
	add	x2, x3, x5
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w2, [x2,8]
	ldr	x3, [x3,x5]
	add	x2, x3, x2, lsl 8
	mov	w3, 128
	str	w3, [x2,2056]
	and	w3, w1, 255
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w3, [x2,2052]
	lsr	w3, w1, 8
	str	w3, [x2,2052]
	str	w4, [x2,2052]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	2
	.global	FlashEraseCmd
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	adrp	x3, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x4, x3, 40
	add	x5, x4, x0
	ldr	x4, [x4,x0]
	ldrb	w0, [x5,8]
	cbz	w2, .L51
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2,2056]
	and	w5, w1, 255
	str	w5, [x2,2052]
	lsr	w5, w1, 8
	str	w5, [x2,2052]
	lsr	w5, w1, 16
	str	w5, [x2,2052]
	ldr	w2, [x3,28]
	add	w1, w1, w2
.L51:
	add	x0, x4, x0, lsl 8
	mov	w2, 96
	str	w2, [x0,2056]
	and	w2, w1, 255
	str	w2, [x0,2052]
	lsr	w2, w1, 8
	str	w2, [x0,2052]
	lsr	w1, w1, 16
	str	w1, [x0,2052]
	mov	w1, 208
	str	w1, [x0,2056]
	ret
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	2
	.global	FlashProgDpSecondCmd
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	adrp	x2, .LANCHOR0
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	sbfiz	x4, x0, 4, 32
	add	x5, x2, 40
	lsr	w3, w1, 16
	add	x7, x5, x4
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w6, [x2,115]
	ldrb	w2, [x7,8]
	ldr	x4, [x5,x4]
	add	x2, x4, x2, lsl 8
	and	w4, w1, 255
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w4, [x2,2052]
	lsr	w4, w1, 8
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	2
	.global	FlashProgSecondCmd
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 40
	add	x2, x1, x0
	ldr	x1, [x1,x0]
	ldrb	w0, [x2,8]
	add	x0, x0, 8
	add	x0, x1, x0, lsl 8
	mov	w1, 16
	str	w1, [x0,8]
	ret
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, 40
	add	x3, x2, x0
	ldrb	w1, [x1,114]
	ldr	x2, [x2,x0]
	ldrb	w0, [x3,8]
	add	x0, x0, 8
	add	x0, x2, x0, lsl 8
	str	w1, [x0,8]
	ret
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	JSHash
	.type	JSHash, %function
JSHash:
	mov	x5, x0
	mov	w0, 42982
	mov	x2, 0
	movk	w0, 0x47c6, lsl 16
.L59:
	cmp	w1, w2
	bls	.L61
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L59
.L61:
	ret
	.size	JSHash, .-JSHash
	.align	2
	.global	FlashLoadIdbInfo
	.type	FlashLoadIdbInfo, %function
FlashLoadIdbInfo:
	mov	w0, 0
	ret
	.size	FlashLoadIdbInfo, .-FlashLoadIdbInfo
	.align	2
	.global	BuildFlashLsbPageTable
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	stp	x29, x30, [sp, -48]!
	adrp	x2, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, w1
	cbnz	w0, .L64
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L65:
	add	x1, x3, 140
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L65
.L71:
	add	x20, x2, :lo12:.LANCHOR0
	mov	w1, 255
	add	x21, x20, 652
	mov	w2, 1024
	mov	x0, x21
	uxth	w19, w19
	bl	ftl_memset
	mov	x1, 0
	mov	x2, x20
	b	.L66
.L64:
	cmp	w0, 1
	bne	.L67
	mov	x0, 0
	mov	w5, 3
	mov	w6, 2
	add	x4, x2, :lo12:.LANCHOR0
.L70:
	cmp	x0, 3
	uxth	w1, w0
	mov	w3, w1
	bls	.L68
	ubfiz	w3, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w5, w6, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L68:
	add	x1, x4, 140
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L70
	b	.L71
.L67:
	cmp	w0, 2
	bne	.L72
	mov	w1, 65535
	mov	x0, 0
	add	x5, x2, :lo12:.LANCHOR0
.L74:
	add	x4, x5, 140
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w1, w3, hi
	add	w1, w1, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L74
	b	.L71
.L72:
	cmp	w0, 3
	bne	.L75
	mov	x0, 0
	mov	w6, 5
	mov	w7, 4
	add	x5, x2, :lo12:.LANCHOR0
.L78:
	cmp	x0, 5
	uxth	w4, w0
	mov	w3, w4
	bls	.L76
	ubfiz	w3, w4, 1, 15
	and	w4, w4, 1
	cmp	w4, wzr
	csel	w1, w6, w7, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L76:
	add	x1, x5, 140
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L78
	b	.L71
.L75:
	cmp	w0, 4
	bne	.L79
	add	x1, x2, :lo12:.LANCHOR0
	mov	w6, 7
	add	x3, x1, 140
	mov	w5, 6
	strh	w0, [x3,8]
	mov	w0, 5
	strh	wzr, [x1,140]
	mov	w1, 1
	strh	w0, [x3,10]
	mov	w0, 7
	strh	w1, [x3,2]
	mov	w1, 2
	strh	w0, [x3,12]
	mov	w0, 8
	strh	w1, [x3,4]
	mov	w1, 3
	strh	w0, [x3,14]
	mov	w0, 8
	strh	w1, [x3,6]
.L81:
	and	w4, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w4, wzr
	csel	w4, w6, w5, ne
	add	x3, x3, 2
	uxth	w0, w0
	sub	w1, w1, w4
	cmp	w0, 256
	strh	w1, [x3,14]
	bne	.L81
	b	.L71
.L79:
	cmp	w0, 5
	bne	.L82
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L83:
	add	x1, x3, 140
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L83
	mov	x0, 0
.L84:
	add	x3, x1, x0
	add	w4, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w4, [x3,32]
	bne	.L84
	b	.L71
.L82:
	cmp	w0, 6
	bne	.L71
	mov	x3, 0
	mov	w6, 12
	mov	w7, 10
	add	x5, x2, :lo12:.LANCHOR0
.L87:
	cmp	x3, 5
	uxth	w4, w3
	mov	w0, w4
	bls	.L85
	add	w0, w4, w4, lsl 1
	and	w4, w4, 1
	cmp	w4, wzr
	uxth	w0, w0
	csel	w1, w6, w7, ne
	sub	w0, w0, w1
	uxth	w0, w0
.L85:
	add	x1, x5, 140
	strh	w0, [x1,x3,lsl 1]
	add	x3, x3, 1
	cmp	x3, 256
	bne	.L87
	b	.L71
.L66:
	cmp	w19, w1, uxth
	bls	.L105
	add	x0, x2, 140
	ldrh	w0, [x0,x1,lsl 1]
	add	x1, x1, 1
	strh	w0, [x21,w0,sxtw 1]
	b	.L66
.L105:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashPrintInfo
	.type	FlashPrintInfo, %function
FlashPrintInfo:
	ret
	.size	FlashPrintInfo, .-FlashPrintInfo
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR1+266
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	ldrh	w1, [x1,#:lo12:.LANCHOR1+266]
	add	x0, x21, :lo12:.LANCHOR0
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	strb	wzr, [x0,1676]
	cmp	w1, 256
	strb	wzr, [x0,1677]
	bls	.L108
	mov	w1, 512
	b	.L124
.L108:
	cmp	w1, 128
	bls	.L124
	mov	w1, 256
.L124:
	add	x19, x21, :lo12:.LANCHOR0
	str	w1, [x0,28]
	mov	w2, 8
	mov	w1, 0
	add	x0, x19, 1680
	adrp	x24, IDByte
	mov	x23, 0
	add	x24, x24, :lo12:IDByte
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 32
	add	x0, x19, 1688
	bl	ftl_memset
	add	x0, x19, 1720
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,1912]
	add	x22, x20, 1
.L112:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x24, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L111
	ldrb	w1, [x19,1676]
	add	x2, x19, 1688
	str	w0, [x2,w1,sxtw 2]
	add	w0, w1, 1
	add	x1, x19, x1, sxtw
	strb	w0, [x19,1676]
	strb	w23, [x1,1680]
.L111:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L112
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w0, [x19,1676]
	strb	w0, [x19,1677]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L113
.L117:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldrb	w0, [x21,1676]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x21,1920]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
.L113:
	adrp	x24, IDByte
	ldr	w25, [x19,28]
	mov	x23, 0
	add	x24, x24, :lo12:IDByte
.L116:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x24, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L114
	ldrb	w1, [x20,13]
	add	x4, x19, 1688
	ldrb	w3, [x19,1676]
	mul	w2, w1, w25
	ldrh	w1, [x20,14]
	mov	x0, x3
	and	w1, w1, 65280
	mul	w1, w2, w1
	str	w1, [x4,w3,sxtw 2]
	ldrb	w2, [x20,23]
	cbz	w2, .L115
	lsl	w1, w1, 1
	str	w1, [x4,w3,sxtw 2]
.L115:
	add	w0, w0, 1
	strb	w0, [x19,1676]
	add	x0, x19, x3
	strb	w23, [x0,1680]
.L114:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L116
	b	.L117
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	2
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0
	mov	w2, 2048
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x1, 1928
	bl	ftl_memcpy
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	2
	.global	FlashLoadPhyInfoInRam
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR1
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	adrp	x23, IDByte
	add	x21, x20, :lo12:.LANCHOR1
	add	x21, x21, 288
	mov	w19, 0
	add	x23, x23, :lo12:IDByte
.L130:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x23
	bl	FlashMemCmp8
	mov	w22, w0
	cbnz	w0, .L127
	add	x0, x20, :lo12:.LANCHOR1
	ubfiz	x19, x19, 5, 32
	add	x1, x0, 288
	adds	x19, x1, x19
	beq	.L133
	ldrb	w3, [x19,22]
	mov	x2, 0
	mov	x1, x0
	b	.L132
.L127:
	add	w19, w19, 1
	add	x21, x21, 32
	cmp	w19, 77
	bne	.L130
	b	.L133
.L132:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2752]
	cmp	w4, w3
	beq	.L131
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L132
	mov	w0, w2
.L131:
	add	x20, x20, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x20, 2752
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 104
	mov	w2, 32
	bl	ftl_memcpy
	add	x0, x20, 256
	mov	x1, x19
	mov	w2, 32
	bl	ftl_memcpy
	b	.L128
.L133:
	mov	w22, -1
.L128:
	mov	w0, w22
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x1, [x0,3976]
	ldr	w2, [x1]
	str	w2, [x0,3984]
	ldr	w2, [x1,4]
	str	w2, [x0,3988]
	ldr	w2, [x1,8]
	str	w2, [x0,3992]
	ldr	w2, [x1,12]
	str	w2, [x0,3996]
	ldr	w2, [x1,304]
	str	w2, [x0,4000]
	ldr	w2, [x1,308]
	str	w2, [x0,4004]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,4008]
	str	w1, [x0,4012]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x7, .LANCHOR0
	uxtb	w4, w4
	add	x6, x7, :lo12:.LANCHOR0
	ldrh	w8, [x6,4030]
	ldrh	w5, [x6,4028]
	ldrh	w9, [x6,28]
	ldrb	w6, [x6,24]
	mul	w5, w5, w8
	cmp	w6, 1
	ubfiz	w8, w9, 1, 15
	csel	w9, w8, w9, eq
	ldr	w8, [x0,4]
	uxth	w5, w5
	cmp	w1, 1
	ubfx	x6, x8, 10, 16
	and	w8, w8, 1023
	udiv	w10, w6, w5
	msub	w5, w10, w5, w6
	uxth	w6, w5
	bne	.L140
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,136]
	cbnz	w5, .L140
	add	x1, x1, 140
	ldrh	w8, [x1,w8,sxtw 1]
.L140:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 1688
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L141
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L141:
	mov	w0, w1
	ret
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	ftl_read_flash_info
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	stp	x29, x30, [sp, -32]!
	mov	w1, 0
	mov	w2, 11
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	bl	ftl_memset
	adrp	x1, .LANCHOR0
	mov	w5, 1
	add	x1, x1, :lo12:.LANCHOR0
	ldr	x0, [x1,1912]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,28]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,4044]
	strb	w0, [x19,7]
	ldr	w0, [x1,4048]
	str	w0, [x19]
	ldr	x0, [x1,1912]
	ldrb	w4, [x1,1676]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,1912]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L144:
	cmp	w4, w0, uxtb
	bls	.L146
	add	x2, x1, 1680
	ldrb	w3, [x19,10]
	ldrb	w2, [x0,x2]
	add	x0, x0, 1
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L144
.L146:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldr	w2, [x1,4052]
	str	w0, [x1,4052]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+1912
	ldr	x0, [x0,#:lo12:.LANCHOR0+1912]
	ldrb	w0, [x0,9]
	ret
	.size	FlashGetPageSize, .-FlashGetPageSize
	.align	2
	.global	NandcReadDontCaseBusyEn
	.type	NandcReadDontCaseBusyEn, %function
NandcReadDontCaseBusyEn:
	ret
	.size	NandcReadDontCaseBusyEn, .-NandcReadDontCaseBusyEn
	.align	2
	.global	NandcGetChipIf
	.type	NandcGetChipIf, %function
NandcGetChipIf:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 40
	add	x2, x1, x0
	ldr	x0, [x1,x0]
	ldrb	w2, [x2,8]
	add	x2, x2, 8
	add	x0, x0, x2, lsl 8
	ret
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	2
	.global	NandcSetDdrPara
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	adrp	x1, .LANCHOR0+3976
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1,#:lo12:.LANCHOR0+3976]
	orr	w0, w0, 1
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR0+3976
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR0+3976]
	mov	w1, 16640
	orr	w0, w0, w1
	str	w0, [x2,344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR0+3976
	cmp	w0, wzr
	ldr	x2, [x1,#:lo12:.LANCHOR0+3976]
	ldr	w1, [x2]
	and	w3, w1, -8193
	orr	w1, w1, 253952
	csel	w1, w1, w3, ne
	str	w1, [x2]
	ret
	.size	NandcSetDdrMode, .-NandcSetDdrMode
	.align	2
	.global	NandcSetMode
	.type	NandcSetMode, %function
NandcSetMode:
	adrp	x1, .LANCHOR0+3976
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR0+3976]
	ldr	w1, [x2]
	beq	.L157
	and	w0, w0, 4
	orr	w1, w1, 24576
	cmp	w0, wzr
	mov	w0, 8322
	and	w1, w1, -32769
	str	w0, [x2,344]
	mov	w0, 4099
	orr	w1, w1, 196608
	movk	w0, 0x10, lsl 16
	str	w0, [x2,304]
	orr	w3, w1, 32768
	mov	w0, 38
	str	w0, [x2,308]
	csel	w1, w3, w1, ne
	mov	w0, 39
	str	w0, [x2,308]
	b	.L159
.L157:
	and	w1, w1, -8193
.L159:
	str	w1, [x2]
	mov	w0, 0
	ret
	.size	NandcSetMode, .-NandcSetMode
	.align	2
	.global	NandcFlashCs
	.type	NandcFlashCs, %function
NandcFlashCs:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 40
	add	x3, x1, x0
	ldr	x2, [x1,x0]
	mov	w1, 1
	ldrb	w3, [x3,8]
	ldr	w0, [x2]
	lsl	w1, w1, w3
	bfi	w0, w1, 0, 8
	str	w0, [x2]
	ret
	.size	NandcFlashCs, .-NandcFlashCs
	.align	2
	.global	NandcFlashDeCs
	.type	NandcFlashDeCs, %function
NandcFlashDeCs:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 40
	ldr	x1, [x1,x0]
	ldr	w0, [x1]
	and	w0, w0, -256
	and	w0, w0, -131073
	str	w0, [x1]
	ret
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	2
	.global	NandcDelayns
	.type	NandcDelayns, %function
NandcDelayns:
	sub	sp, sp, #16
	lsr	w0, w0, 4
	str	w0, [sp,12]
.L165:
	ldr	w0, [sp,12]
	sub	w1, w0, #1
	str	w1, [sp,12]
	cbnz	w0, .L165
	add	sp, sp, 16
	ret
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	FlashReadStatus
	.type	FlashReadStatus, %function
FlashReadStatus:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	stp	x29, x30, [sp, -32]!
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 40
	add	x29, sp, 0
	add	x2, x1, x0
	str	x19, [sp,16]
	ldrb	w2, [x2,8]
	ldr	x19, [x1,x0]
	mov	w0, 112
	add	x19, x19, x2, lsl 8
	str	w0, [x19,2056]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x19,2048]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	ToshibaSetRRPara
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	uxtb	w1, w1
	mov	w3, 5
	add	w2, w1, 1
	stp	x29, x30, [sp, -80]!
	smull	x3, w2, w3
	adrp	x2, .LANCHOR1
	add	x29, sp, 0
	add	x2, x2, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	add	x23, x2, x1, sxtw
	stp	x25, x26, [sp,64]
	add	x21, x2, 2928
	add	x25, x2, 2880
	adrp	x24, g_maxRegNum
	adrp	x26, g_retryMode
	stp	x19, x20, [sp,16]
	mov	x22, x0
	add	x25, x25, x3
	add	x21, x21, x3
	mov	x20, 0
	add	x24, x24, :lo12:g_maxRegNum
	mov	x19, x2
	add	x26, x26, :lo12:g_retryMode
.L169:
	ldrb	w0, [x24]
	cmp	w0, w20
	bls	.L175
	mov	w0, 85
	str	w0, [x22,8]
	add	x0, x19, 2880
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	w0, 200
	bl	NandcDelayns
	ldrb	w0, [x26]
	cmp	w0, 34
	bne	.L170
	ldrsb	w0, [x25,x20]
	b	.L174
.L170:
	cmp	w0, 35
	bne	.L172
	ldrsb	w0, [x21,x20]
	b	.L174
.L172:
	ldrsb	w0, [x23,3024]
.L174:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L169
.L175:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	stp	x29, x30, [sp, -64]!
	adrp	x2, .LANCHOR1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	add	x2, x2, :lo12:.LANCHOR1
	adrp	x22, g_maxRegNum
	stp	x19, x20, [sp,16]
	add	x1, x2, x1, uxtb 2
	stp	x23, x24, [sp,48]
	mov	x21, x0
	add	x23, x1, 3036
	mov	x20, 0
	add	x22, x22, :lo12:g_maxRegNum
	mov	w24, 161
	mov	x19, x2
.L177:
	ldrb	w0, [x22]
	cmp	w0, w20
	bls	.L179
	str	w24, [x21,8]
	add	x0, x19, 3032
	str	wzr, [x21]
	ldrsb	w0, [x20,x0]
	str	w0, [x21]
	ldrsb	w0, [x23,x20]
	add	x20, x20, 1
	str	w0, [x21]
	mov	w0, 300
	bl	NandcDelayns
	b	.L177
.L179:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	HynixSetRRPara
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x21, x2
	adrp	x2, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x19, x2, :lo12:.LANCHOR0
	uxtb	w20, w3
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	uxtb	x24, w0
	uxtb	w26, w1
	ldr	x0, [x19,1912]
	mov	x25, x24
	add	x19, x19, 4056
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L181
	add	x19, x19, x24, lsl 6
	lsl	w4, w20, 2
	add	x19, x19, 20
	b	.L186
.L181:
	cmp	w0, 7
	bne	.L183
	mov	x0, 160
	mov	w4, 10
	madd	x19, x24, x0, x19
	mul	w4, w20, w4
	add	x19, x19, 28
.L186:
	add	x19, x19, x4, sxtw
	b	.L182
.L183:
	lsl	x4, x24, 3
	add	x4, x4, x20, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 20
.L182:
	add	x2, x2, :lo12:.LANCHOR0
	lsl	x0, x24, 4
	add	x2, x2, 40
	uxtb	x26, w26
	add	x1, x2, x0
	ldr	x22, [x2,x0]
	mov	w0, w25
	ldrb	w23, [x1,8]
	bl	NandcFlashCs
	ubfiz	x23, x23, 8, 8
	mov	w0, 54
	add	x22, x22, x23
	mov	x23, 0
	str	w0, [x22,2056]
.L184:
	cmp	x23, x26
	beq	.L187
	ldrb	w0, [x21,x23]
	str	w0, [x22,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldrsb	w0, [x19,x23]
	add	x23, x23, 1
	str	w0, [x22,2048]
	b	.L184
.L187:
	mov	w0, 22
	str	w0, [x22,2056]
	mov	w0, w25
	bl	NandcFlashDeCs
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x24, x0, x24
	strb	w20, [x24,560]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	x20, 0
	add	x19, x19, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	ldr	x0, [x19,1912]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L188
	adrp	x21, IDByte
	add	x22, x19, 4060
	add	x21, x21, :lo12:IDByte
.L193:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L190
	ldrb	w1, [x19,4057]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L190:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L193
.L188:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x4, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x5, x4, 40
	stp	x19, x20, [sp,16]
	add	x6, x5, x0
	uxtb	w2, w2
	ldr	x20, [x5,x0]
	ldr	x0, [x4,1912]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L196
	add	x4, x4, 104
	cbnz	w2, .L197
	ldrb	w2, [x4,13]
	b	.L205
.L197:
	ldrb	w2, [x4,14]
.L205:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,119]
	cbz	w4, .L200
	add	x3, x19, 8
	lsl	x3, x3, 8
.L204:
	cmp	w2, w4
	bcs	.L200
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L204
.L196:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L200:
	add	x19, x19, 8
	mov	w0, 80
	lsl	x19, x19, 8
	bl	NandcDelayns
	ldr	w0, [x20,x19]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	uxtb	w0, w0
	ret
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	2
	.global	FlashWaitReadyEN
	.type	FlashWaitReadyEN, %function
FlashWaitReadyEN:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	uxtb	w19, w0
	mov	w20, w1
	uxtb	w21, w2
.L210:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L210
	tbz	x3, 6, .L210
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashWaitCmdDone
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	stp	x29, x30, [sp, -48]!
	uxtb	x1, w0
	mov	x2, 24
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp,32]
	add	x20, x20, :lo12:.LANCHOR0
	mov	x22, x1
	mul	x1, x1, x2
	add	x0, x20, 1720
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L215
	mov	w0, w21
	add	x20, x20, 1688
	bl	NandcFlashCs
	ldr	w0, [x20,w22,sxtw 2]
	ldr	w1, [x19,4]
	cmp	w0, wzr
	mov	w0, w21
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w20, w0
	mov	w0, w21
	bl	NandcFlashDeCs
	ldr	x1, [x19,8]
	sbfx	x0, x20, 0, 1
	str	w0, [x1]
	str	xzr, [x19,8]
	ldr	x1, [x19,16]
	cbz	x1, .L215
	str	w0, [x1]
	str	xzr, [x19,16]
.L215:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x1, 40
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
.L222:
	mov	w0, 100
	bl	NandcDelayns
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L223
	subs	w19, w19, #1
	bne	.L222
	mov	w0, -1
	b	.L221
.L223:
	mov	w0, 0
.L221:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashReset
	.type	FlashReset, %function
FlashReset:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x1, x1, :lo12:.LANCHOR0
	uxtb	w20, w0
	add	x1, x1, 40
	sbfiz	x0, x20, 4, 32
	add	x2, x1, x0
	str	x21, [sp,32]
	ldr	x21, [x1,x0]
	mov	w0, w20
	ldrb	w19, [x2,8]
	bl	NandcFlashCs
	add	x19, x19, 8
	mov	w0, 255
	add	x19, x21, x19, lsl 8
	str	w0, [x19,8]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashDeCs
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashReset, .-FlashReset
	.align	2
	.global	FlashEraseBlock
	.type	FlashEraseBlock, %function
FlashEraseBlock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	mov	w20, w1
	mov	w0, w19
	str	x2, [x29,40]
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	ldr	x2, [x29,40]
	mov	w1, w20
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w1, w20
	mov	w0, w19
	bl	FlashReadStatus
	mov	w20, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	and	w0, w20, 1
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashEraseBlock, .-FlashEraseBlock
	.align	2
	.global	FlashSetInterfaceMode
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	adrp	x1, .LANCHOR2+564
	adrp	x3, .LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x3, x3, :lo12:.LANCHOR0
	adrp	x7, IDByte
	add	x29, sp, 0
	ldrb	w1, [x1,#:lo12:.LANCHOR2+564]
	add	x3, x3, 40
	mov	x6, 0
	add	x7, x7, :lo12:IDByte
	and	w11, w1, 1
	and	w1, w1, 4
	mov	w8, 239
	mov	w9, 128
	uxtb	w5, w1
	mov	w10, 1
	mov	w12, 35
	mov	w13, 32
	mov	w14, 5
.L238:
	ldrb	w4, [x6,x7]
	ldr	x1, [x3]
	cmp	w4, 152
	ldrb	w2, [x3,8]
	beq	.L228
	cmp	w4, 69
	beq	.L228
	cmp	w4, 173
	beq	.L228
	cmp	w4, 44
	bne	.L229
.L228:
	cmp	w0, 1
	bne	.L231
	cbz	w11, .L229
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L232
	str	w0, [x1,2052]
	b	.L249
.L232:
	cmp	w4, 44
	bne	.L234
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L236
.L234:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L236
.L231:
	cbz	w5, .L229
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L235
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L236
.L235:
	cmp	w4, 44
	bne	.L237
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L236
.L237:
	str	w9, [x1,2052]
.L249:
	str	wzr, [x1,2048]
.L236:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L229:
	add	x6, x6, 8
	add	x3, x3, 16
	cmp	x6, 32
	bne	.L238
	mov	w0, 0
	bl	NandcWaitFlashReady
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	adrp	x3, .LANCHOR1+265
	adrp	x5, .LANCHOR0
	stp	x29, x30, [sp, -48]!
	uxtb	w0, w0
	add	x5, x5, :lo12:.LANCHOR0
	add	x5, x5, 40
	sbfiz	x6, x0, 4, 32
	add	x29, sp, 0
	ldrb	w4, [x3,#:lo12:.LANCHOR1+265]
	add	x3, x5, x6
	str	x19, [sp,16]
	lsl	w4, w4, 9
	str	x2, [x29,40]
	ldrb	w3, [x3,8]
	ldr	x19, [x5,x6]
	add	x19, x19, x3, lsl 8
	and	w3, w1, 255
	str	wzr, [x19,2056]
	str	w4, [x19,2052]
	lsr	w4, w4, 8
	str	w4, [x19,2052]
	str	w3, [x19,2052]
	lsr	w3, w1, 8
	str	w3, [x19,2052]
	lsr	w1, w1, 16
	str	w1, [x19,2052]
	mov	w1, 48
	str	w1, [x19,2056]
	bl	NandcWaitFlashReady
	ldr	x2, [x29,40]
	ldr	w0, [x19,2048]
	strb	w0, [x2]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashReadSpare, .-FlashReadSpare
	.align	2
	.global	SandiskProgTestBadBlock
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	stp	x29, x30, [sp, -32]!
	add	x3, x3, :lo12:.LANCHOR0
	sbfiz	x4, x0, 4, 32
	add	x3, x3, 40
	add	x29, sp, 0
	add	x2, x3, x4
	str	x19, [sp,16]
	ldrb	w2, [x2,8]
	ldr	x19, [x3,x4]
	add	x19, x19, x2, lsl 8
	mov	w2, 162
	str	w2, [x19,2056]
	mov	w2, 128
	str	w2, [x19,2056]
	and	w2, w1, 255
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w2, [x19,2052]
	lsr	w2, w1, 8
	str	w2, [x19,2052]
	lsr	w1, w1, 16
	str	w1, [x19,2052]
	mov	w1, 16
	str	w1, [x19,2056]
	bl	NandcWaitFlashReady
	mov	w0, 112
	str	w0, [x19,2056]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x19,2048]
	ldr	x19, [sp,16]
	and	w0, w0, 1
	ldp	x29, x30, [sp], 32
	ret
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	2
	.global	SandiskSetRRPara
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
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
	bl	NandcDelayns
	mov	w1, 5
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	adrp	x0, g_maxRegNum
	smull	x1, w19, w1
	add	x3, x2, 2880
	adrp	x4, g_retryMode
	add	x2, x2, 2928
	add	x3, x3, x1
	add	x0, x0, :lo12:g_maxRegNum
	add	x1, x2, x1
	add	x4, x4, :lo12:g_retryMode
	mov	x2, 0
.L253:
	ldrb	w5, [x0]
	cmp	w5, w2
	bls	.L258
	ldrb	w5, [x4]
	cmp	w5, 67
	bne	.L254
	ldrsb	w5, [x3,x2]
	b	.L257
.L254:
	ldrsb	w5, [x1,x2]
.L257:
	str	w5, [x20]
	add	x2, x2, 1
	b	.L253
.L258:
	mov	w0, 0
	bl	NandcWaitFlashReady
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	micron_auto_read_calibration_config
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxtb	w19, w0
	mov	w0, w19
	sbfiz	x19, x19, 4, 32
	str	x1, [x29,40]
	bl	NandcWaitFlashReady
	adrp	x3, .LANCHOR0
	add	x3, x3, :lo12:.LANCHOR0
	add	x3, x3, 40
	add	x0, x3, x19
	ldr	x19, [x3,x19]
	ldrb	w2, [x0,8]
	mov	w0, 239
	add	x19, x19, x2, lsl 8
	str	w0, [x19,2056]
	mov	w0, 150
	str	w0, [x19,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldr	x1, [x29,40]
	str	w1, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	str	wzr, [x19,2048]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	2
	.global	FlashEraseSLc2KBlocks
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	str	x25, [sp,64]
	stp	x23, x24, [sp,48]
	add	x21, x21, :lo12:.LANCHOR0
	adrp	x23, .LC1
	stp	x19, x20, [sp,16]
	mov	w24, w1
	mov	x20, x0
	mov	w22, 0
	add	x25, x21, 1720
	add	x23, x23, :lo12:.LC1
.L261:
	cmp	w22, w24
	beq	.L270
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1676]
	cmp	w2, w0
	bcc	.L262
	mov	w0, -1
	str	w0, [x20]
	b	.L263
.L262:
	uxtw	x2, w2
	add	x0, x21, x2
	ldrb	w19, [x0,1680]
	mov	x0, 24
	mul	x2, x2, x0
	mov	w0, w19
	strb	w19, [x25,x2]
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w1, [x29,88]
	mov	w2, 0
	mov	w0, w19
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29,88]
	mov	w0, w19
	bl	FlashReadStatus
	sbfx	x0, x0, 0, 1
	str	w0, [x20]
	mov	w0, w19
	ldr	w1, [x29,88]
	ldr	w2, [x21,28]
	add	w1, w2, w1
	mov	w2, 0
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29,88]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L264
	mov	w0, -1
	str	w0, [x20]
.L264:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L265
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L265:
	mov	w0, w19
	bl	NandcFlashDeCs
.L263:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L261
.L270:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -144]!
	mov	w4, -83
	mov	w3, -82
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x0, 4056
	cmp	w26, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	strb	w26, [x0,4056]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w3, [x1,6]
	strb	w0, [x1,7]
	bne	.L272
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3113
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3113]
	b	.L327
.L272:
	cmp	w26, 3
	bne	.L274
	mov	w0, -80
	strb	w0, [x1,4]
	mov	w0, -79
	strb	w0, [x1,5]
	mov	w0, -78
	strb	w0, [x1,6]
	mov	w0, -77
	strb	w0, [x1,7]
	mov	w0, -76
	strb	w0, [x1,8]
	mov	w0, -75
	strb	w0, [x1,9]
	mov	w0, -74
	strb	w0, [x1,10]
	mov	w0, -73
	b	.L360
.L274:
	cmp	w26, 4
	bne	.L275
	mov	w5, -52
	strb	w5, [x1,4]
	mov	w5, -65
	strb	w5, [x1,5]
	mov	w5, -86
	strb	w5, [x1,6]
	mov	w5, -85
	strb	w4, [x1,9]
	strb	w5, [x1,7]
	mov	w5, -51
	strb	w3, [x1,10]
	strb	w5, [x1,8]
.L360:
	mov	w27, 8
	strb	w0, [x1,11]
	mov	w28, w27
	b	.L273
.L275:
	cmp	w26, 5
	bne	.L276
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L359
.L276:
	cmp	w26, 6
	bne	.L277
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L359
.L277:
	cmp	w26, 7
	bne	.L327
	mov	w0, -80
	strb	w0, [x1,4]
	mov	w0, -79
	strb	w0, [x1,5]
	mov	w0, -78
	strb	w0, [x1,6]
	mov	w0, -77
	strb	w0, [x1,7]
	mov	w0, -76
	strb	w0, [x1,8]
	mov	w0, -75
	strb	w0, [x1,9]
	mov	w0, -74
	strb	w0, [x1,10]
	mov	w0, -73
	strb	w0, [x1,11]
	mov	w0, -44
	mov	w27, 12
	strb	w0, [x1,12]
	mov	w28, 10
	mov	w0, -43
	strb	w0, [x1,13]
	b	.L273
.L327:
	mov	w27, 7
.L359:
	mov	w28, 4
.L273:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L356
	adrp	x22, .LANCHOR1
	add	x25, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x26, x25, 4056
	add	x3, x25, 40
	add	x22, x22, 3096
.L278:
	ldrb	w0, [x25,1676]
	cmp	w0, w21
	bls	.L285
	add	x0, x25, x21, sxtw
	ldrb	w0, [x0,1680]
	mov	x23, 0
	add	x19, x26, x0, lsl 6
	lsl	x0, x0, 4
	add	x1, x3, x0
	add	x19, x19, 20
	ldr	x24, [x3,x0]
	ldrb	w0, [x1,8]
	mov	w1, 55
	add	x24, x24, x0, lsl 8
.L280:
	add	x0, x26, x23
	str	w1, [x24,2056]
	str	x3, [x29,128]
	str	x1, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x24,2052]
	mov	w0, 80
	bl	NandcDelayns
	ldr	w0, [x24,2048]
	strb	w0, [x19,x23]
	add	x23, x23, 1
	ldr	x1, [x29,136]
	cmp	w28, w23, uxtb
	ldr	x3, [x29,128]
	bhi	.L280
	mov	x0, 0
.L281:
	add	w2, w0, 8
	mov	x1, 0
.L282:
	add	x5, x1, x0
	add	x1, x1, 4
	add	x5, x22, x5
	cmp	x1, 24
	ldrb	w6, [x5,4]
	ldrb	w5, [x19,x0]
	add	w5, w6, w5
	strb	w5, [x19,w2,sxtw]
	add	w2, w2, 8
	bne	.L282
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L281
	add	w21, w21, 1
	strb	wzr, [x19,16]
	strb	wzr, [x19,24]
	strb	wzr, [x19,32]
	uxtb	w21, w21
	strb	wzr, [x19,40]
	strb	wzr, [x19,48]
	strb	wzr, [x19,41]
	strb	wzr, [x19,49]
	b	.L278
.L356:
	sub	w0, w26, #3
	cmp	w0, 4
	bhi	.L285
	mul	w25, w28, w27
	sub	w23, w28, #1
	mov	w21, 0
	lsl	w0, w25, 4
	asr	w22, w25, 2
	str	w0, [x29,124]
	add	x25, x20, :lo12:.LANCHOR0
	lsl	w0, w22, 1
	uxtb	x23, w23
	str	w0, [x29,120]
	add	x0, x25, 40
	add	x23, x23, 1
	str	x0, [x29,128]
	sub	w0, w26, #5
	str	w0, [x29,136]
.L286:
	ldrb	w0, [x25,1676]
	cmp	w0, w21
	bhi	.L326
.L285:
	add	x20, x20, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w28, [x20,4057]
	strb	w27, [x20,4058]
	ldp	x25, x26, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L326:
	add	x0, x25, x21, sxtw
	ldrb	w1, [x0,1680]
	str	x1, [x29,112]
	lsl	x0, x1, 4
	mov	x24, x1
	ldr	x1, [x29,128]
	add	x6, x1, x0
	ldr	x0, [x1,x0]
	ldrb	w19, [x6,8]
	add	x19, x0, x19, lsl 8
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	cmp	w26, 7
	add	x6, x25, 4056
	ldr	x1, [x29,112]
	beq	.L287
	add	x1, x6, x1, lsl 6
	add	x6, x1, 20
	b	.L288
.L287:
	mov	x0, 160
	madd	x1, x1, x0, x6
	add	x6, x1, 28
.L288:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x19,2056]
	bne	.L289
	mov	w0, 255
	str	w0, [x19,2052]
	mov	w0, 64
	str	w0, [x19,2048]
	mov	w0, 204
	b	.L361
.L289:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L291
	ldrb	w0, [x25,4060]
	str	w0, [x19,2052]
	mov	w0, 82
	b	.L362
.L291:
	mov	w0, 174
	str	w0, [x19,2052]
	str	wzr, [x19,2048]
	mov	w0, 176
.L361:
	str	w0, [x19,2052]
	mov	w0, 77
.L362:
	str	w0, [x19,2048]
	mov	w0, 22
	str	w0, [x19,2056]
	mov	w0, 23
	str	w0, [x19,2056]
	mov	w0, 4
	str	w0, [x19,2056]
	mov	w0, 25
	str	w0, [x19,2056]
	cmp	w26, 6
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	bne	.L292
	mov	w0, 31
	str	w0, [x19,2052]
	b	.L293
.L292:
	str	wzr, [x19,2052]
.L293:
	mov	w7, 2
	str	w7, [x19,2052]
	str	wzr, [x19,2052]
	mov	w0, 48
	str	w0, [x19,2056]
	mov	w0, w24
	str	x6, [x29,104]
	str	x7, [x29,112]
	bl	NandcWaitFlashReady
	ldr	w0, [x29,136]
	mov	w1, 16
	ldr	x6, [x29,104]
	cmp	w0, 1
	bls	.L295
	ldr	x7, [x29,112]
	cmp	w26, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L295:
	ldr	x0, [x25]
	mov	x7, 0
.L296:
	ldr	w8, [x19,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L296
	cmp	w26, 7
	bne	.L297
	mov	w1, 0
.L299:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L298
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L298
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L299
	b	.L300
.L298:
	cmp	w1, 7
	bne	.L301
.L300:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L302:
	b	.L302
.L297:
	cmp	w26, 6
	bne	.L301
	mov	x1, 0
.L303:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L301
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L301
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L303
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L305:
	b	.L305
.L301:
	ldr	x9, [x25]
	mov	x0, 0
.L306:
	ldr	w1, [x29,124]
	cmp	w1, w0
	ble	.L363
	ldr	w1, [x19,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L306
.L363:
	ldr	x11, [x25]
	mov	w8, w22
	mov	w7, 8
.L309:
	mov	w0, 0
.L308:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L308
	ldr	w0, [x29,120]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L309
	mov	x7, 0
	mov	w15, 1
.L310:
	mov	w1, 0
	ldr	x14, [x25]
	mov	w10, w1
.L314:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L312:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L312
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L314
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L310
	mov	x7, 0
	ldr	x1, [x25]
	mov	w8, w7
.L317:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L317
	cmp	w8, 7
	ble	.L318
	adrp	x0, .LC3
	mov	w2, 1
	add	x0, x0, :lo12:.LC3
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L319:
	b	.L319
.L318:
	cmp	w26, 6
	mov	w7, 4
	beq	.L320
	cmp	w26, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L320:
	mov	w8, 0
.L321:
	mov	x1, 0
.L322:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x6,w10,sxtw]
	bhi	.L322
	add	w8, w8, 1
	add	x9, x9, x23
	cmp	w8, w27
	add	w0, w0, w7
	blt	.L321
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L324
	mov	w0, 54
	str	w0, [x19,2056]
	adrp	x0, .LANCHOR0+4060
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4060]
	str	w0, [x19,2052]
	mov	w0, 22
	str	wzr, [x19,2048]
	str	w0, [x19,2056]
	mov	w0, w21
	bl	FlashReadCmd
	b	.L325
.L324:
	mov	w0, 56
	str	w0, [x19,2056]
.L325:
	mov	w0, w24
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L286
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L364
	sub	w2, w0, #1
	cmp	w2, 6
	bhi	.L366
	bl	HynixGetReadRetryDefault
	b	.L364
.L366:
	cmp	w0, 49
	bne	.L367
	adrp	x0, .LANCHOR0
	mov	w2, 64
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 4060
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 3032
	b	.L382
.L367:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L373
	cmp	w1, 33
	bne	.L368
.L373:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 4
	b	.L383
.L368:
	cmp	w1, 67
	beq	.L374
	cmp	w1, 34
	bne	.L370
.L374:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 5
.L383:
	strb	w1, [x0,4057]
	mov	w1, 7
	strb	w1, [x0,4058]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 4060
	add	x1, x1, 2880
	mov	w2, 45
	b	.L382
.L370:
	cmp	w1, 68
	beq	.L375
	cmp	w1, 35
	bne	.L364
.L375:
	adrp	x0, .LANCHOR0
	mov	w2, 95
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 4060
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 2928
.L382:
	bl	ftl_memcpy
.L364:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	stp	x29, x30, [sp, -80]!
	adrp	x3, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	add	x3, x3, :lo12:.LANCHOR0
	uxtb	w21, w0
	add	x4, x3, 40
	sbfiz	x0, x21, 4, 32
	add	x3, x3, 104
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	add	x5, x4, x0
	and	w23, w2, 255
	ldr	x6, [x4,x0]
	lsr	w22, w2, 8
	ldrb	w0, [x3,16]
	lsr	w4, w1, 8
	ldrb	w19, [x5,8]
	lsr	w20, w2, 16
	cmp	w0, 1
	and	w5, w1, 255
	lsr	w0, w1, 16
	bne	.L385
	add	x19, x6, x19, lsl 8
	ldrb	w2, [x3,8]
	str	x1, [x29,72]
	str	w2, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w5, [x19,2052]
	str	w4, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,9]
	str	w0, [x19,2056]
	mov	w0, w21
	bl	NandcWaitFlashReady
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	mov	w0, 48
	str	wzr, [x19,2052]
	str	w23, [x19,2052]
	str	w22, [x19,2052]
	str	w20, [x19,2052]
	str	w0, [x19,2056]
	ldr	x1, [x29,72]
	b	.L386
.L385:
	add	x19, x6, x19, lsl 8
	ldrb	w2, [x3,8]
	str	w2, [x19,2056]
	str	w5, [x19,2052]
	str	w4, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,9]
	str	w0, [x19,2056]
	mov	w0, 48
	str	w23, [x19,2052]
	str	w22, [x19,2052]
	str	w20, [x19,2052]
	str	w0, [x19,2056]
.L386:
	mov	w0, w21
	bl	FlashSetRandomizer
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	stp	x29, x30, [sp, -32]!
	mov	w0, 0
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,568]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L388
	mov	w0, 0
	bl	flash_enter_slc_mode
.L388:
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,572]
	cbz	w0, .L389
	ldrb	w0, [x19,564]
	tbz	x0, 0, .L389
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19,572]
.L389:
	adrp	x0, .LANCHOR0+40
	ldr	x0, [x0,#:lo12:.LANCHOR0+40]
	str	wzr, [x0,336]
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	adrp	x2, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 40
	ldr	x0, [x2,x0]
	str	w1, [x0,336]
	ret
	.size	NandcRandmzSel, .-NandcRandmzSel
	.align	2
	.global	NandcTimeCfg
	.type	NandcTimeCfg, %function
NandcTimeCfg:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, w0
	mov	w0, 0
	bl	rknand_get_clk_rate
	mov	w2, 16960
	adrp	x1, .LANCHOR0
	movk	w2, 0xf, lsl 16
	add	x1, x1, :lo12:.LANCHOR0
	sdiv	w0, w0, w2
	cmp	w0, 250
	ble	.L398
	ldr	x0, [x1,3976]
	mov	w1, 8354
	b	.L405
.L398:
	cmp	w0, 220
	ble	.L400
	ldr	x0, [x1,3976]
	b	.L406
.L400:
	cmp	w0, 185
	ble	.L401
	ldr	x0, [x1,3976]
	mov	w1, 4226
	b	.L405
.L401:
	cmp	w0, 160
	ldr	x0, [x1,3976]
	ble	.L402
	mov	w1, 4194
	b	.L405
.L402:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L405
	cmp	w19, 99
	mov	w1, 4225
	bls	.L405
.L406:
	mov	w1, 8322
.L405:
	str	w1, [x0,4]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	2
	.global	FlashTimingCfg
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	sub	w1, w0, #4096
	sub	w1, w1, #97
	stp	x29, x30, [sp, -16]!
	and	w1, w1, -33
	cmp	w1, 1
	add	x29, sp, 0
	bls	.L408
	mov	w1, 8322
	cmp	w0, w1
	bne	.L409
.L408:
	adrp	x1, .LANCHOR0+3976
	ldr	x1, [x1,#:lo12:.LANCHOR0+3976]
	str	w0, [x1,4]
.L409:
	adrp	x0, .LANCHOR1+277
	ldrb	w0, [x0,#:lo12:.LANCHOR1+277]
	bl	NandcTimeCfg
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	stp	x29, x30, [sp, -32]!
	mov	w3, 1
	adrp	x1, .LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	x19, x1
	add	x2, x20, :lo12:.LANCHOR0
	str	w3, [x2,64]
	mov	w3, 2
	str	wzr, [x2,48]
	str	w3, [x2,80]
	mov	w3, 3
	str	x0, [x2,40]
	str	w3, [x2,96]
	add	x3, x1, :lo12:.LANCHOR2
	str	x0, [x2,56]
	str	x0, [x2,72]
	str	x0, [x2,88]
	str	x0, [x2,3976]
	ldr	w2, [x0]
	ubfx	x4, x2, 13, 1
	str	w4, [x3,576]
	ldr	w4, [x0,352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x4, x4, 16, 4
	str	w4, [x3,580]
	ldr	w4, [x0,352]
	str	w4, [x3,568]
	cmp	w4, 2049
	bne	.L411
	mov	w1, 8
	str	w1, [x3,580]
.L411:
	add	x20, x20, :lo12:.LANCHOR0
	str	w2, [x0]
	ldr	x0, [x20,3976]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x20,3976]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	add	x1, x19, :lo12:.LANCHOR2
	str	x0, [x1,584]
	str	x0, [x1,592]
	add	x0, x0, 32768
	str	wzr, [x1,632]
	str	x0, [x1,600]
	str	wzr, [x1,640]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	ldr	x5, [x4,3976]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4,3976]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4,3976]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4,3976]
	ldr	w0, [x1,308]
	ldr	w1, [x1,344]
	and	w0, w0, 255
	orr	w0, w0, w1, lsl 16
	str	w0, [x3]
	ret
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	2
	.global	NandcBchSel
	.type	NandcBchSel, %function
NandcBchSel:
	adrp	x1, .LANCHOR0+3976
	uxtb	w0, w0
	cmp	w0, 16
	ldr	x2, [x1,#:lo12:.LANCHOR0+3976]
	mov	w1, 1
	str	w1, [x2,8]
	adrp	x1, .LANCHOR2+644
	str	w0, [x1,#:lo12:.LANCHOR2+644]
	mov	w1, 4096
	bne	.L414
.L417:
	and	w1, w1, -17
	b	.L415
.L414:
	cmp	w0, 24
	bne	.L416
	orr	w1, w1, 16
	b	.L415
.L416:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L417
.L415:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+4044
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+4044]
	bl	NandcBchSel
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	ftl_flash_resume
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR0
	add	x1, x0, :lo12:.LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, IDByte
	mov	x19, 0
	ldr	x2, [x1,3976]
	mov	x22, x0
	ldr	w3, [x1,3984]
	add	x20, x20, :lo12:IDByte
	ldr	w21, [x1,4000]
	str	w3, [x2]
	ldr	w3, [x1,3988]
	ldr	x2, [x1,3976]
	str	w3, [x2,4]
	ldr	w3, [x1,3992]
	ldr	x2, [x1,3976]
	str	w3, [x2,8]
	ldr	w3, [x1,3996]
	str	w3, [x2,12]
	str	w21, [x2,304]
	ldr	w3, [x1,4004]
	str	w3, [x2,308]
	ldr	w3, [x1,4008]
	str	w3, [x2,336]
	ldr	w1, [x1,4012]
	str	w1, [x2,344]
.L424:
	lsl	x0, x19, 3
	ldrb	w1, [x0,x20]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 253
	bhi	.L423
	mov	w0, w19
	bl	FlashReset
.L423:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L424
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,572]
	cbz	w0, .L425
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,564]
	bl	NandcSetMode
	lsr	w0, w21, 8
	bl	NandcSetDdrPara
.L425:
	add	x0, x22, :lo12:.LANCHOR0
	ldr	x0, [x0,1912]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	2
	.global	ftl_nandc_get_irq_status
	.type	ftl_nandc_get_irq_status, %function
ftl_nandc_get_irq_status:
	ldr	w0, [x0,372]
	ret
	.size	ftl_nandc_get_irq_status, .-ftl_nandc_get_irq_status
	.align	2
	.global	NandcIqrWaitFlashReady
	.type	NandcIqrWaitFlashReady, %function
NandcIqrWaitFlashReady:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	x19, x0
	bl	rk_nandc_rb_irq_flag_init
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	orr	w0, w0, 2
	str	w0, [x19,364]
	ldr	w0, [x19]
	tbnz	x0, 9, .L432
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L431
.L432:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L431:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	w26, w1
	adrp	x1, .LANCHOR0
	stp	x19, x20, [sp,16]
	add	x20, x1, :lo12:.LANCHOR0
	str	x1, [x29,104]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w23, w2
	mov	x24, x0
	ldrb	w2, [x20,24]
	mov	w19, 0
	cbz	w2, .L435
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L436
.L435:
	mov	w27, 56
	adrp	x28, .LANCHOR2
	add	x25, x20, 1720
.L466:
	cmp	w19, w23
	bcs	.L467
	umull	x5, w19, w27
	mov	w1, 0
	sub	w4, w23, w19
	add	x21, x24, x5
	add	x2, x29, 120
	mov	x0, x21
	add	x3, x29, 124
	str	x5, [x29,96]
	bl	LogAddr2PhyAddr
	mov	w22, w0
	ldrb	w1, [x20,1676]
	ldr	w0, [x29,124]
	ldr	x5, [x29,96]
	cmp	w0, w1
	bcc	.L437
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L438
.L437:
	add	x1, x28, :lo12:.LANCHOR2
	mov	x2, 24
	ldrb	w1, [x1,648]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x2, x25
	ldr	x1, [x1,8]
	cbz	x1, .L440
	bl	FlashWaitCmdDone
.L440:
	ldr	w1, [x29,124]
	mov	x0, 24
	ldr	w2, [x29,120]
	madd	x0, x1, x0, x25
	str	x21, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w22, .L441
	add	w2, w19, 1
	umull	x2, w2, w27
	add	x2, x24, x2
	str	x2, [x0,16]
.L441:
	add	x0, x20, x1
	ldrb	w21, [x0,1680]
	mov	x0, 24
	mul	x1, x1, x0
	mov	w0, w21
	strb	w21, [x25,x1]
	bl	NandcFlashCs
	cmp	w26, 1
	bne	.L442
	ldrb	w0, [x20,136]
	cbz	w0, .L442
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L443
.L442:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L443:
	ldr	w1, [x29,124]
	add	x0, x20, 1688
	add	w19, w19, w22
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,120]
	cmp	w0, wzr
	mov	w0, w21
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w1, [x29,120]
	mov	w0, w21
	mov	w2, w22
	bl	FlashEraseCmd
	mov	w0, w21
	bl	NandcFlashDeCs
.L438:
	add	w19, w19, 1
	b	.L466
.L467:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x21, 24
	add	x19, x0, :lo12:.LANCHOR0
	ldr	x0, [x19,3976]
	bl	NandcIqrWaitFlashReady
.L445:
	ldrb	w0, [x19,1676]
	cmp	w0, w20
	bls	.L468
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w26, 1
	bne	.L446
	ldrb	w0, [x19,136]
	cbz	w0, .L446
	madd	x0, x20, x21, x19
	ldrb	w0, [x0,1720]
	bl	flash_exit_slc_mode
.L446:
	add	x20, x20, 1
	b	.L445
.L468:
	adrp	x0, .LANCHOR2+652
	ldr	w0, [x0,#:lo12:.LANCHOR2+652]
	cbnz	w0, .L448
.L450:
	mov	w0, 0
	b	.L436
.L448:
	adrp	x0, IDByte
	ldrb	w0, [x0,#:lo12:IDByte]
	cmp	w0, 69
	bne	.L450
	mov	x0, 0
	mov	x1, 56
.L449:
	cmp	w23, w0
	bls	.L450
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L449
.L436:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	NandcSendDumpDataStart
	.type	NandcSendDumpDataStart, %function
NandcSendDumpDataStart:
	sub	sp, sp, #16
	mov	w2, 1
	mov	w1, 10
	bfi	w1, w2, 5, 2
	ldr	w2, [x0,16]
	str	w2, [sp,8]
	orr	w1, w1, 536870912
	orr	w1, w1, 1024
	ldr	w2, [sp,8]
	orr	w1, w1, 2097152
	and	w2, w2, -5
	str	w2, [sp,8]
	ldr	w2, [sp,8]
	add	sp, sp, 16
	str	w2, [x0,16]
	str	w1, [x0,8]
	orr	w1, w1, 4
	str	w1, [x0,8]
	ret
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	2
	.global	NandcSendDumpDataDone
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	sub	sp, sp, #16
.L471:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L471
	add	sp, sp, 16
	ret
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	2
	.global	NandcXferStart
	.type	NandcXferStart, %function
NandcXferStart:
	stp	x29, x30, [sp, -96]!
	ubfiz	x0, x0, 4, 8
	ubfx	x3, x3, 1, 7
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x22, x4
	adrp	x4, .LANCHOR0
	add	x4, x4, :lo12:.LANCHOR0
	str	x25, [sp,64]
	add	x4, x4, 40
	uxtb	w25, w1
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	add	x1, x4, x0
	ubfiz	w20, w25, 1, 1
	ldr	x21, [x4,x0]
	orr	w20, w20, 8
	adrp	x19, .LANCHOR2
	uxtb	w23, w2
	ldrb	w0, [x1,8]
	mov	w1, 16
	ldr	w24, [x21,12]
	bfi	w24, w1, 8, 8
	and	w24, w24, -9
	bfi	w24, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	add	x0, x19, :lo12:.LANCHOR2
	orr	w20, w20, 536870912
	orr	w20, w20, 1024
	ldr	w0, [x0,580]
	bfi	w20, w3, 4, 1
	cmp	w0, 3
	bls	.L475
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w7, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w7, .L489
	cbz	x22, .L476
.L489:
	cbnz	w25, .L478
.L486:
	mov	x0, x21
	add	w23, w23, 1
	asr	w23, w23, 1
	bl	rk_nandc_xfer_irq_flag_init
	bfi	w20, w23, 22, 6
	ldr	w0, [x21,368]
	orr	w0, w0, 1
	str	w0, [x21,368]
	ldr	w0, [x21,364]
	orr	w0, w0, 1
	str	w0, [x21,364]
	mov	x0, x22
	cbnz	x22, .L480
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,592]
	b	.L480
.L478:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w4, 128
	mov	w2, 0
	lsr	w8, w23, 1
	mov	w3, w2
	mov	w9, -1
	ldr	w0, [x1,644]
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
.L482:
	cmp	w3, w8
	bcs	.L486
	lsr	w0, w2, 2
	ubfiz	x0, x0, 2, 30
	cbz	w7, .L483
	ldrh	w6, [x5,2]
	ldr	x10, [x1,600]
	ldrh	w11, [x5],4
	orr	w6, w11, w6, lsl 16
	str	w6, [x10,x0]
	b	.L484
.L483:
	ldr	x6, [x1,600]
	str	w9, [x6,x0]
.L484:
	add	w3, w3, 1
	add	w2, w2, w4
	b	.L482
.L480:
	add	x19, x19, :lo12:.LANCHOR2
	ubfx	x23, x20, 22, 5
	mov	w2, w25
	and	x22, x22, 3
	ldr	x1, [x19,600]
	str	x1, [x19,616]
	lsl	w1, w23, 10
	str	x0, [x19,608]
	bl	rknand_dma_map_single
	str	w0, [x19,624]
	ldr	x0, [x19,616]
	lsl	w1, w23, 7
	mov	w2, w25
	bl	rknand_dma_map_single
	str	w0, [x19,628]
	mov	w0, 1
	str	w0, [x19,632]
	ldr	w0, [x19,624]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	w0, [x19,628]
	str	w0, [x21,24]
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x22, .L487
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L487:
	ldr	w0, [x29,88]
	cmp	w25, wzr
	cset	w1, eq
	orr	w0, w0, 4
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 1, 1
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 1
	str	w0, [x29,88]
.L476:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L475:
	str	w24, [x21,12]
	str	w20, [x21,8]
	orr	w20, w20, 4
	str	w20, [x21,8]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	NandcXferStart, .-NandcXferStart
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x1, 40
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	adrp	x20, .LANCHOR2
	ldr	x19, [x1,x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,580]
	cmp	w0, 3
	bls	.L524
	ldr	w0, [x19,16]
	tbz	x0, 2, .L524
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L495
	adrp	x22, .LC4
	adrp	x23, .LC5
	mov	w21, 0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
.L496:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L504
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,580]
	cmp	w0, 5
	bhi	.L497
.L500:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L496
	ldr	w2, [x19,28]
	mov	w1, w21
	ldr	w3, [x29,64]
	mov	x0, x22
	ubfx	x2, x2, 16, 5
	ubfx	x3, x3, 22, 6
	bl	printk
	mov	x0, x23
	mov	x1, x19
	mov	w2, 4
	mov	w3, 512
	bl	rknand_print_hex
	b	.L496
.L497:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L500
	ldr	w0, [x29,72]
	tbz	x0, 17, .L500
.L504:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 592
	ldr	w0, [x19,40]
	cbz	w0, .L505
	ldr	w1, [x29,64]
	mov	w2, 0
	ldr	w0, [x19,32]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	mov	w2, 0
	ldr	w0, [x19,36]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 7
	bl	rknand_dma_unmap_single
	b	.L505
.L495:
	adrp	x22, .LC6
	adrp	x23, .LC5
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
.L506:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L532
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L506
	ldr	w2, [x29,64]
	mov	w1, w21
	ldr	w3, [x19,28]
	mov	x0, x22
	ubfx	x3, x3, 16, 5
	bl	printk
	mov	x0, x23
	mov	x1, x19
	mov	w2, 4
	mov	w3, 512
	bl	rknand_print_hex
	b	.L506
.L532:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,640]
	cbz	w0, .L509
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L509:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 592
	ldr	w0, [x21,40]
	cbz	w0, .L510
	ldr	w1, [x29,64]
	mov	w2, 1
	ldr	w0, [x21,32]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 10
	bl	rknand_dma_unmap_single
	ldr	w1, [x29,64]
	mov	w2, 1
	ldr	w0, [x21,36]
	ubfx	x1, x1, 22, 5
	lsl	w1, w1, 7
	bl	rknand_dma_unmap_single
.L510:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,640]
	cbz	w0, .L505
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L505:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20,632]
	b	.L493
.L524:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L524
.L493:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcCopy1KB
	.type	NandcCopy1KB, %function
NandcCopy1KB:
	stp	x29, x30, [sp, -48]!
	uxtb	w1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	uxtb	w19, w2
	cmp	w1, 1
	mov	x20, x4
	ubfiz	x2, x19, 9, 8
	add	x4, x0, 4096
	add	x21, x0, 512
	add	x2, x4, x2
	bne	.L534
	cbz	x3, .L535
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L535:
	cbz	x20, .L533
	mov	w0, 48
	lsr	w19, w19, 1
	ldrb	w1, [x20,1]
	mul	w19, w19, w0
	ldrb	w0, [x20,2]
	asr	w19, w19, 2
	lsl	w0, w0, 16
	orr	w0, w0, w1, lsl 8
	ldrb	w1, [x20]
	orr	w0, w0, w1
	ldrb	w1, [x20,3]
	orr	w0, w0, w1, lsl 24
	str	w0, [x21,w19,sxtw 2]
	b	.L533
.L534:
	cbz	x3, .L538
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L538:
	cbz	x20, .L533
	mov	w0, 48
	lsr	w19, w19, 1
	mul	w19, w19, w0
	asr	w19, w19, 2
	ldr	w0, [x21,w19,sxtw 2]
	strb	w0, [x20]
	lsr	w1, w0, 8
	strb	w1, [x20,1]
	lsr	w1, w0, 16
	lsr	w0, w0, 24
	strb	w1, [x20,2]
	strb	w0, [x20,3]
.L533:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	uxtb	w24, w0
	adrp	x0, .LANCHOR0
	stp	x27, x28, [sp,80]
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, x24, sxtw 4
	ldr	x19, [x0,40]
	and	x0, x3, 63
	uxtb	w20, w1
	uxtb	w23, w2
	mov	x25, x3
	mov	x21, x4
	cbnz	x0, .L550
	cbnz	x4, .L551
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x21, x29, 112
	bl	ftl_memset
.L551:
	mov	w0, w24
	mov	w1, w20
	mov	w2, w23
	mov	w3, 0
	mov	x4, x25
	mov	x5, x21
	mov	w22, 0
	bl	NandcXferStart
	mov	w0, w24
	bl	NandcXferComp
	cbnz	w20, .L552
	adrp	x0, .LANCHOR2
	ubfx	x3, x23, 1, 7
	add	x2, x0, :lo12:.LANCHOR2
	mov	w4, 128
	add	x3, x21, x3, lsl 2
	ldr	w1, [x2,644]
	cmp	w1, 25
	mov	w1, 64
	csel	w4, w1, w4, cc
	mov	w1, w20
.L554:
	cmp	x21, x3
	add	w5, w1, w4
	beq	.L596
	ldr	x6, [x2,600]
	and	x1, x1, 4294967292
	add	x21, x21, 4
	ldr	w1, [x6,x1]
	strb	w1, [x21,-4]
	lsr	w6, w1, 8
	strb	w6, [x21,-3]
	lsr	w6, w1, 16
	strb	w6, [x21,-2]
	lsr	w1, w1, 24
	strb	w1, [x21,-1]
	mov	w1, w5
	b	.L554
.L596:
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w23, w23, 2
	ldr	w4, [x0,644]
	ldr	w5, [x0,580]
	mov	w0, 0
	mov	w22, w0
.L556:
	cmp	w0, w23
	bcs	.L552
	cbz	w4, .L552
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L580
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L580
	cmp	w5, 5
	bls	.L558
	ldr	w2, [x29,104]
	ubfx	x6, x2, 3, 5
	ldr	w2, [x29,104]
	ldr	w1, [x29,104]
	ubfx	x2, x2, 27, 1
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,104]
	orr	w2, w6, w2, lsl 5
	ubfx	x1, x1, 29, 1
	orr	w1, w3, w1, lsl 5
	ldr	w3, [x29,104]
	cmp	w2, w1
	bls	.L559
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L560
.L559:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L595
.L558:
	cmp	w5, 3
	bls	.L560
	ldr	w2, [x29,104]
	ubfx	x6, x2, 3, 5
	ldr	w2, [x29,104]
	ldr	w1, [x29,104]
	ubfx	x2, x2, 28, 1
	ubfx	x3, x1, 16, 5
	ldr	w1, [x29,104]
	orr	w2, w6, w2, lsl 5
	ubfx	x1, x1, 30, 1
	orr	w1, w3, w1, lsl 5
	ldr	w3, [x29,104]
	cmp	w2, w1
	bls	.L561
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L595
.L561:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L595:
	orr	w3, w1, w3, lsl 5
.L560:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L557
.L580:
	mov	w22, -1
.L557:
	add	w0, w0, 1
	b	.L556
.L552:
	str	wzr, [x19,16]
	b	.L563
.L550:
	cmp	w20, 1
	bne	.L594
	mov	w22, 0
	mov	w27, 2
.L564:
	cmp	w22, w23
	bcs	.L597
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L566
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L566:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 1
	mov	w2, w26
	mul	w4, w4, w22
	add	w22, w22, 2
	add	x4, x21, x4
	bl	NandcCopy1KB
	mov	x4, 0
	mov	w0, w24
	mov	w1, 1
	mov	w2, 2
	mov	w3, w26
	mov	x5, x4
	bl	NandcXferStart
	mov	w0, w24
	bl	NandcXferComp
	b	.L564
.L597:
	mov	w22, 0
	b	.L563
.L594:
	mov	w1, 0
	mov	x4, 0
	mov	w26, 0
	mov	w0, w24
	mov	w2, 2
	mov	w3, w1
	mov	x5, x4
	mov	w22, w26
	mov	w27, 2
	bl	NandcXferStart
.L569:
	cmp	w26, w23
	bcs	.L563
	mov	w0, w24
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L570
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L570:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L585
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L571
.L585:
	mov	w22, -1
.L571:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L572
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L572:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L569
.L563:
	cbnz	w20, .L575
	adrp	x0, .LANCHOR2+580
	ldr	w0, [x0,#:lo12:.LANCHOR2+580]
	cmp	w0, 5
	bls	.L575
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L575
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L575:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	NandcXferData, .-NandcXferData
	.align	2
	.global	FlashReadRawPage
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	stp	x29, x30, [sp, -64]!
	mov	x4, x3
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	adrp	x0, .LANCHOR1+265
	str	x21, [sp,32]
	mov	x21, x2
	ldrb	w20, [x0,#:lo12:.LANCHOR1+265]
	cbnz	w19, .L599
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,25]
	ldr	w0, [x0,28]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L599:
	mov	w0, w19
	str	x4, [x29,48]
	str	x1, [x29,56]
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	ldr	x1, [x29,56]
	mov	w0, w19
	bl	FlashReadCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	x4, [x29,48]
	mov	w2, w20
	mov	x3, x21
	mov	w1, 0
	mov	w0, w19
	bl	NandcXferData
	mov	w20, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	ldr	x21, [sp,32]
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	2
	.global	FlashDdrTunningRead
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	uxtb	w24, w0
	add	x27, x19, :lo12:.LANCHOR2
	ldr	x0, [x20,3976]
	mov	w22, 6
	mov	w25, w1
	mov	x28, x2
	mov	x23, x3
	mov	w26, w4
	ldr	w0, [x0,304]
	mov	w21, 1024
	str	w0, [x29,124]
	ldr	w0, [x27,580]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L602
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w24
	bl	FlashReset
	mov	w1, w25
	mov	x2, x28
	mov	x3, x23
	mov	w0, w24
	bl	FlashReadRawPage
	mov	w21, w0
	ldrb	w0, [x27,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x27,564]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L603
.L612:
	mov	w21, -1
	b	.L604
.L603:
	adrp	x0, .LC7
	mov	w1, w25
	add	x0, x0, :lo12:.LC7
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L605
	add	x20, x20, x24, sxtw 4
	ldr	x0, [x20,40]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L605:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w0, [x19,656]
	add	w0, w0, 1
	str	w0, [x19,656]
	cmp	w0, 2047
	bls	.L604
	mov	x23, 0
	str	wzr, [x19,656]
	mov	x28, x23
.L602:
	mov	w19, 0
	mov	w27, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L610:
	mov	w0, w22
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	NandcSetDdrPara
	mov	w1, w25
	mov	w0, w24
	mov	x2, x28
	mov	x3, x23
	bl	FlashReadRawPage
	add	w1, w21, 1
	ldr	x6, [x29,112]
	cmp	w0, w1
	ldr	x5, [x29,104]
	bhi	.L606
	cmp	w0, 2
	bhi	.L616
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L616
	sub	w19, w22, w20
	mov	w21, w0
	mov	w27, 0
	b	.L608
.L606:
	cmp	w6, w20
	bcs	.L617
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L618
	mov	w6, w20
	b	.L617
.L616:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w27, 0
	mov	x28, x23
	b	.L607
.L617:
	mov	w20, 0
.L607:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L610
.L608:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L609
.L618:
	mov	w19, w5
.L609:
	cbz	w19, .L611
	adrp	x0, .LC8
	mov	w1, w19
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L611:
	cbz	w27, .L604
	adrp	x0, .LC9
	mov	w1, w24
	add	x0, x0, :lo12:.LC9
	mov	w2, w25
	bl	printk
	cbz	w26, .L612
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L604:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w0
	str	x27, [sp,80]
	mov	w0, w20
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w22, w1
	mov	x25, x2
	mov	x24, x3
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w19, w0
	adrp	x23, .LANCHOR2
	bne	.L628
	adrp	x21, .LANCHOR0
	add	x26, x21, :lo12:.LANCHOR0
	ldrb	w27, [x26,32]
	cbnz	w27, .L629
.L631:
	add	x0, x23, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbz	w0, .L628
	b	.L649
.L629:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	strb	wzr, [x26,32]
	bl	FlashReadRawPage
	strb	w27, [x26,32]
	cmn	w0, #1
	beq	.L631
	mov	w19, w0
	b	.L628
.L649:
	add	x21, x21, :lo12:.LANCHOR0
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	mov	w4, 1
	ldr	x0, [x21,3976]
	ldr	w26, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L632
	ldrb	w0, [x21,4044]
	cmp	w19, w0, lsr 1
	bls	.L628
.L632:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
.L628:
	add	x21, x23, :lo12:.LANCHOR2
	cmn	w19, #1
	ldr	x4, [x21,664]
	bne	.L633
	cbz	x4, .L633
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	mov	w0, w20
	blr	x4
	mov	w19, w0
	adrp	x0, .LC10
	mov	w1, w19
	add	x0, x0, :lo12:.LC10
	mov	w2, w20
	mov	w3, w22
	bl	printk
	cmn	w19, #1
	bne	.L633
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L633
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,664]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L633:
	mov	w0, w19
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashDdrParaScan
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	mov	w20, w1
	stp	x21, x22, [sp,32]
	add	x22, x19, :lo12:.LANCHOR2
	uxtb	w21, w0
	ldrb	w0, [x22,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,564]
	bl	NandcSetMode
	mov	x2, 0
	mov	w1, w20
	mov	x3, x2
	mov	w4, w2
	mov	w0, w21
	bl	FlashDdrTunningRead
	mov	w22, w0
	mov	x2, 0
	mov	w0, w21
	mov	w1, w20
	mov	x3, x2
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L654
	cmn	w22, #1
	bne	.L651
.L654:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20,564]
	tbz	x0, 0, .L651
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,572]
	b	.L653
.L651:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,572]
.L653:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashLoadPhyInfo
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	stp	x29, x30, [sp, -112]!
	mov	w0, 60
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	strb	w0, [x29,104]
	mov	w0, 40
	strb	w0, [x29,105]
	mov	w0, 24
	adrp	x25, .LANCHOR2
	strb	w0, [x29,106]
	adrp	x19, .LANCHOR0
	mov	w0, 16
	stp	x23, x24, [sp,48]
	strb	w0, [x29,107]
	adrp	x23, .LANCHOR1
	add	x0, x25, :lo12:.LANCHOR2
	ldr	x1, [x19,#:lo12:.LANCHOR0]
	stp	x21, x22, [sp,32]
	add	x21, x23, :lo12:.LANCHOR1
	mov	w20, 0
	stp	x27, x28, [sp,80]
	add	x21, x21, 256
	str	x1, [x0,672]
	str	wzr, [x0,680]
	mov	w27, 4
	mov	w0, 0
	mov	w24, -1
	ldrh	w28, [x21,10]
	bl	flash_enter_slc_mode
.L662:
	add	w26, w20, 1
	mov	x22, 0
.L664:
	add	x0, x29, 104
	ldrb	w0, [x22,x0]
	bl	FlashBchSel
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L663
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w26
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L663
	add	x22, x22, 1
	cmp	x22, 4
	beq	.L665
	b	.L664
.L666:
	mov	w1, 2036
	add	x0, x26, 12
	mov	w24, -1
	bl	JSHash
	ldr	w1, [x26,8]
	cmp	w1, w0
	beq	.L676
.L665:
	subs	w27, w27, #1
	add	w20, w20, w28
	bne	.L662
	mov	w0, w27
.L675:
	bl	flash_exit_slc_mode
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L663:
	add	x22, x25, :lo12:.LANCHOR2
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x26, [x22,672]
	ldr	w1, [x26]
	cmp	w1, w0
	bne	.L665
	cbnz	w24, .L666
	add	x23, x23, :lo12:.LANCHOR1
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x23,266]
	udiv	w20, w20, w0
	mov	w0, w24
	add	w20, w20, 1
	str	w20, [x19,8]
	b	.L675
.L676:
	add	x1, x26, 160
	mov	w2, 32
	mov	x0, x21
	add	x24, x19, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldr	x1, [x22,672]
	mov	w2, 32
	add	x0, x24, 104
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x22,672]
	mov	w2, 852
	add	x0, x24, 4056
	add	x1, x1, 224
	bl	ftl_memcpy
	str	w20, [x22,680]
	ldr	x1, [x22,672]
	ldr	w0, [x1,1076]
	strb	w0, [x22,572]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L668
	str	w2, [x24,8]
	b	.L669
.L668:
	mov	w0, 2
	str	w0, [x24,8]
.L669:
	add	x0, x25, :lo12:.LANCHOR2
	ldrh	w1, [x1,14]
	mov	w24, 0
	strb	w1, [x0,684]
	b	.L665
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	w23, w0
	mov	w0, w23
	str	w1, [x29,132]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	x27, x2
	mov	x26, x3
	bl	NandcWaitFlashReady
	adrp	x20, g_retryMode
	adrp	x2, .LANCHOR0
	sbfiz	x1, x23, 4, 32
	add	x0, x2, :lo12:.LANCHOR0
	str	x2, [x29,120]
	add	x0, x0, 40
	mov	w22, 0
	add	x3, x0, x1
	ldr	x24, [x0,x1]
	ldrb	w0, [x3,8]
	str	x0, [x29,136]
	add	x19, x0, 8
	mov	x28, x0
	ldrb	w0, [x20,#:lo12:g_retryMode]
	add	x19, x24, x19, lsl 8
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L678
	adrp	x0, .LANCHOR2+572
	ldrb	w0, [x0,#:lo12:.LANCHOR2+572]
	cbz	w0, .L679
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L679:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L678:
	adrp	x0, g_maxRetryCount
	mov	w21, 1
	add	x0, x0, :lo12:g_maxRetryCount
	str	x0, [x29,112]
	ldr	x0, [x29,136]
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,104]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,96]
.L680:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0,#:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L705
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, w21
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L681
	bl	SandiskSetRRPara
	b	.L682
.L681:
	bl	ToshibaSetRRPara
.L682:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L683
	ldr	x0, [x29,112]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L683
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L683:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L684
	mov	w0, 4
	bl	NandcSetDdrMode
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
	mov	w0, 0
	bl	NandcSetDdrMode
	b	.L685
.L684:
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L685:
	cmn	w28, #1
	beq	.L686
	ldr	x0, [x29,120]
	cmn	w25, #1
	csel	w25, w25, w28, ne
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L688
	mov	x26, 0
	mov	x27, x26
.L686:
	add	w21, w21, 1
	b	.L680
.L705:
	mov	w28, w25
.L688:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, 0
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L690
	bl	SandiskSetRRPara
	b	.L691
.L690:
	bl	ToshibaSetRRPara
.L691:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L692
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L692:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L693
	mov	w0, 4
	bl	NandcSetDdrMode
.L693:
	mov	w0, w28
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	str	x27, [sp,80]
	mov	w0, w22
	adrp	x27, g_maxRetryCount
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x24, x2
	mov	w25, w1
	mov	x23, x3
	adrp	x20, .LANCHOR0
	bl	NandcWaitFlashReady
	mov	w26, 1
	add	x0, x20, :lo12:.LANCHOR0
	sbfiz	x1, x22, 4, 32
	add	x0, x0, 40
	mov	w19, -1
	add	x2, x0, x1
	add	x27, x27, :lo12:g_maxRetryCount
	ldr	x0, [x0,x1]
	ldrb	w21, [x2,8]
	add	x21, x21, 8
	add	x21, x0, x21, lsl 8
.L707:
	ldrb	w0, [x27]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L710
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L708
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,4044]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L713
	mov	x23, 0
	mov	x24, x23
.L708:
	add	w26, w26, 1
	b	.L707
.L713:
	mov	w19, w0
.L710:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,4044]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L712
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L712:
	mov	w0, w19
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	w28, w1
	mov	x26, x2
	ldrb	w19, [x0,4044]
	mov	x22, x3
	ldrb	w0, [x0,136]
	cbnz	w0, .L720
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L721
.L720:
	mov	w0, 3
	sdiv	w19, w19, w0
.L721:
	adrp	x0, g_maxRetryCount
	adrp	x27, .LC11
	add	x0, x0, :lo12:g_maxRetryCount
	mov	w25, 0
	str	x0, [x29,152]
	add	x0, x27, :lo12:.LC11
	str	x0, [x29,144]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 40
	add	x23, x23, x21, sxtw 4
.L732:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L722:
	ldr	x0, [x29,152]
	ldrb	w0, [x0]
	cmp	w24, w0
	bcs	.L725
	add	x2, x6, x8
	mov	w0, 200
	str	x4, [x29,96]
	str	x8, [x29,104]
	str	w9, [x2,2056]
	str	w10, [x2,2052]
	str	x6, [x29,112]
	str	x9, [x29,120]
	str	x10, [x29,128]
	str	x2, [x29,136]
	bl	NandcDelayns
	ldr	x2, [x29,136]
	add	w7, w24, 1
	mov	w0, w21
	mov	w1, w28
	mov	x3, x22
	str	x7, [x29,136]
	str	w7, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	mov	x2, x26
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x7, [x29,136]
	ldr	x10, [x29,128]
	ldr	x9, [x29,120]
	ldr	x6, [x29,112]
	ldr	x8, [x29,104]
	ldr	x4, [x29,96]
	beq	.L723
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L734
	mov	x22, 0
	mov	x26, x22
.L723:
	mov	w24, w7
	b	.L722
.L734:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L725:
	add	x1, x6, x4, lsl 8
	mov	w0, 239
	str	x1, [x29,136]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	w0, 200
	bl	NandcDelayns
	ldr	x1, [x29,136]
	cmp	w27, w19
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	bcc	.L727
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L727:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L736
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L728
.L736:
	ldr	x0, [x29,144]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,136]
	bl	printk
	cbnz	w25, .L730
	ldr	x6, [x29,136]
	cbz	w6, .L741
	ldrb	w0, [x20,136]
	cbz	w0, .L741
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L732
.L730:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L741
.L728:
	cbz	w25, .L741
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L741:
	mov	w0, w27
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	uxtb	x22, w0
	stp	x27, x28, [sp,80]
	mov	w27, w1
	add	x1, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	x26, x2
	add	x2, x1, 4056
	ldrb	w25, [x1,4058]
	add	x0, x2, x22
	ldr	x1, [x1,1912]
	mov	x23, x22
	mov	x24, x3
	ldrb	w19, [x0,12]
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L750
	ldrb	w19, [x0,20]
.L750:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 4060
.L751:
	cmp	w28, w25
	bcs	.L755
	add	w19, w19, 1
	ldrb	w1, [x4,4057]
	mov	x2, x5
	mov	w0, w23
	uxtb	w19, w19
	str	x4, [x29,96]
	cmp	w19, w25
	str	x5, [x29,104]
	csel	w19, w19, wzr, cc
	mov	w3, w19
	bl	HynixSetRRPara
	mov	w0, w23
	mov	w1, w27
	mov	x2, x26
	mov	x3, x24
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	beq	.L753
	ldrb	w1, [x4,4044]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L760
	mov	x24, 0
	mov	x26, x24
.L753:
	add	w28, w28, 1
	b	.L751
.L760:
	mov	w20, w0
.L755:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	add	x0, x0, 4056
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L757
	strb	w19, [x0,20]
	b	.L758
.L757:
	strb	w19, [x0,12]
.L758:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,4044]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L759
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L759:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.global	FlashProgPage
	.type	FlashProgPage, %function
FlashProgPage:
	stp	x29, x30, [sp, -64]!
	mov	x4, x3
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	adrp	x0, .LANCHOR1+265
	stp	x21, x22, [sp,32]
	mov	w20, w1
	mov	x22, x2
	ldrb	w21, [x0,#:lo12:.LANCHOR1+265]
	cbnz	w19, .L767
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	ldr	w2, [x0,28]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L767
	ldrb	w0, [x0,24]
	cbnz	w0, .L768
	sub	w21, w21, #2
	b	.L767
.L768:
	mov	w21, 4
.L767:
	mov	w0, w19
	str	x4, [x29,56]
	bl	NandcWaitFlashReady
	mov	w0, w19
	bl	NandcFlashCs
	mov	w0, w19
	mov	w1, w20
	bl	FlashProgFirstCmd
	ldr	x4, [x29,56]
	mov	w2, w21
	mov	x3, x22
	mov	w0, w19
	mov	w1, 1
	bl	NandcXferData
	mov	w1, w20
	mov	w0, w19
	bl	FlashProgSecondCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	mov	w1, w20
	mov	w0, w19
	bl	FlashReadStatus
	mov	w20, w0
	mov	w0, w19
	bl	NandcFlashDeCs
	and	w0, w20, 1
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashSavePhyInfo
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x22, .LANCHOR0
	adrp	x23, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR2
	add	x20, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	ldr	x0, [x22,#:lo12:.LANCHOR0]
	mov	w21, 0
	str	x0, [x19,672]
	ldrb	w0, [x19,685]
	bl	FlashBchSel
	ldr	x0, [x22,#:lo12:.LANCHOR0]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,672]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,672]
	ldrb	w1, [x20,1676]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,25]
	strh	w1, [x0,-2]
	ldrb	w1, [x19,572]
	str	w1, [x0,1060]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	bl	ftl_memcpy
	ldr	x0, [x19,672]
	add	x1, x20, 1680
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,672]
	add	x1, x20, 1688
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,672]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 256
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,672]
	add	x1, x20, 104
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,672]
	add	x1, x20, 4056
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,672]
	mov	w1, 2036
	add	x0, x20, 12
	bl	JSHash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,688]
	str	x0, [x19,672]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L772:
	add	x20, x22, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w0, 0
	add	x24, x23, :lo12:.LANCHOR2
	add	w26, w19, 1
	ldr	w1, [x20,28]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldr	w1, [x20,28]
	mov	x3, 0
	ldr	x2, [x22,#:lo12:.LANCHOR0]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x20,28]
	mov	x3, 0
	ldr	x2, [x22,#:lo12:.LANCHOR0]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x20,28]
	mov	w0, 0
	ldr	x2, [x24,688]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L770
	ldr	x25, [x24,672]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L770
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L770
	ldr	w0, [x20,28]
	cmp	w21, 1
	str	w26, [x20,8]
	mul	w19, w19, w0
	str	w19, [x24,680]
	beq	.L773
	mov	w21, 1
.L770:
	cmp	w26, 4
	mov	w19, w26
	bne	.L772
	b	.L771
.L773:
	mov	w21, 2
.L771:
	mov	w0, 0
	bl	flash_exit_slc_mode
	cmp	w21, wzr
	csetm	w0, eq
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x27, x28, [sp,80]
	mov	x27, x0
	mov	w0, 60
	strb	w0, [x29,104]
	mov	w0, 40
	strb	w0, [x29,105]
	mov	w0, 24
	stp	x21, x22, [sp,32]
	strb	w0, [x29,106]
	adrp	x21, .LANCHOR0
	mov	w0, 16
	strb	w0, [x29,107]
	add	x0, x21, :lo12:.LANCHOR0
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrb	w28, [x0,4044]
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w1, [x0,568]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L779
	mov	w0, 0
	bl	flash_enter_slc_mode
.L779:
	adrp	x25, .LC12
	mov	x0, x27
	mov	w1, 0
	mov	w2, 2048
	mov	w24, -1
	mov	w20, 2
	add	x25, x25, :lo12:.LC12
	bl	ftl_memset
.L780:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,25]
	cmp	w20, w0
	bcs	.L784
	mov	x23, 0
.L782:
	add	x0, x29, 104
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w26, [x23,x0]
	mov	w0, w26
	bl	FlashBchSel
	ldr	w1, [x19,28]
	mov	w0, 0
	ldr	x2, [x21,#:lo12:.LANCHOR0]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L781
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L782
	b	.L783
.L781:
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L783
	mov	w1, w26
	mov	x0, x25
	bl	printk
	ldr	x1, [x19]
	mov	x0, x27
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x19]
	ldr	w0, [x0,512]
	strb	w0, [x19,25]
	ldr	w0, [x19,8]
	cmp	w0, w20
	bls	.L787
	mov	w24, 0
	str	w20, [x19,8]
	bl	FlashSavePhyInfo
.L783:
	add	w20, w20, 1
	b	.L780
.L787:
	mov	w24, 0
.L784:
	mov	w0, w28
	add	x22, x22, :lo12:.LANCHOR2
	bl	FlashBchSel
	ldr	w1, [x22,568]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L789
	mov	w0, 0
	bl	flash_exit_slc_mode
.L789:
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR0
	mov	x22, x0
	mov	w0, 32768
	adrp	x20, .LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	ftl_malloc
	add	x21, x20, :lo12:.LANCHOR2
	str	x0, [x19,#:lo12:.LANCHOR0]
	mov	w0, 32768
	add	x24, x19, :lo12:.LANCHOR0
	adrp	x23, IDByte
	bl	ftl_malloc
	str	x0, [x21,688]
	mov	w0, 4096
	adrp	x25, .LC13
	add	x24, x24, 40
	mov	w27, 0
	bl	ftl_malloc
	str	x0, [x21,696]
	mov	w0, 32768
	mov	w28, 44
	bl	ftl_malloc
	str	x0, [x21,704]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x21,712]
	mov	w0, 128
	str	w0, [x24,-12]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x21,685]
	mov	x0, x22
	strb	w1, [x24,-15]
	strb	w1, [x21,684]
	str	wzr, [x24,-32]
	strb	wzr, [x21,572]
	str	wzr, [x21,656]
	add	x21, x23, :lo12:IDByte
	strb	wzr, [x24,-16]
	bl	NandcInit
	add	x0, x25, :lo12:.LC13
	mov	x26, x21
	str	x0, [x29,104]
.L796:
	uxtb	w25, w27
	mov	w0, w25
	bl	FlashReset
	ldrb	w22, [x24,8]
	ldr	x2, [x24]
	mov	w0, w25
	str	x2, [x29,96]
	ubfiz	x22, x22, 8, 8
	bl	NandcFlashCs
	ldr	x2, [x29,96]
	mov	w0, 144
	add	x22, x2, x22
	str	w0, [x22,2056]
	mov	w0, 200
	str	wzr, [x22,2052]
	bl	NandcDelayns
	ldr	w0, [x22,2048]
	strb	w0, [x21]
	ldr	w0, [x22,2048]
	strb	w0, [x21,1]
	ldr	w0, [x22,2048]
	strb	w0, [x21,2]
	ldr	w0, [x22,2048]
	strb	w0, [x21,3]
	ldr	w0, [x22,2048]
	strb	w0, [x21,4]
	ldr	w0, [x22,2048]
	strb	w0, [x21,5]
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x21]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L792
	ldr	x0, [x29,104]
	add	w1, w27, 1
	ldrb	w3, [x21,1]
	ldrb	w4, [x21,2]
	ldrb	w5, [x21,3]
	ldrb	w6, [x21,4]
	ldrb	w7, [x21,5]
	bl	printk
.L792:
	cbnz	w27, .L793
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L841
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L841
	bl	FlashCs123Init
.L793:
	ldrb	w0, [x21]
	cmp	w0, 181
	bne	.L795
	strb	w28, [x21]
.L795:
	add	w27, w27, 1
	add	x24, x24, 16
	cmp	w27, 4
	add	x21, x21, 8
	bne	.L796
	ldrb	w0, [x23,#:lo12:IDByte]
	cmp	w0, 173
	beq	.L797
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,576]
	bl	NandcSetDdrMode
.L797:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 852
	add	x0, x21, 4056
	bl	ftl_memset
	strb	wzr, [x21,32]
	adrp	x1, .LANCHOR1
	add	x4, x23, :lo12:IDByte
	add	x0, x1, :lo12:.LANCHOR1
	add	x0, x0, 256
	str	x0, [x21,1912]
	ldrb	w0, [x4,1]
	cmp	w0, 218
	cset	w3, eq
	cmp	w0, 161
	cset	w2, eq
	orr	w2, w3, w2
	cbnz	w2, .L798
	and	w2, w0, -33
	cmp	w2, 209
	beq	.L798
	cmp	w0, 220
	bne	.L799
	ldrb	w2, [x4,3]
	cmp	w2, 149
	bne	.L799
.L798:
	add	x2, x19, :lo12:.LANCHOR0
	mov	w4, 1
	mov	w5, 16
	add	x6, x23, :lo12:IDByte
	strb	w4, [x2,24]
	add	x4, x20, :lo12:.LANCHOR2
	strb	w5, [x2,25]
	add	x2, x1, :lo12:.LANCHOR1
	strb	w5, [x4,685]
	ldrb	w5, [x23,#:lo12:IDByte]
	strb	w5, [x2,3129]
	strb	w0, [x2,3130]
	cmp	w5, 152
	bne	.L800
	ldrsb	w2, [x6,4]
	tbnz	w2, #31, .L800
	mov	w2, 24
	strb	w2, [x4,685]
.L800:
	add	x2, x20, :lo12:.LANCHOR2
	ldr	w4, [x2,568]
	cmp	w4, 2049
	beq	.L843
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L801
.L843:
	add	x2, x20, :lo12:.LANCHOR2
	mov	w4, 16
	strb	w4, [x2,685]
.L801:
	cbz	w3, .L803
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3142]
	mov	w2, -38
	b	.L876
.L803:
	cmp	w0, 220
	bne	.L804
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3142]
	mov	w2, -36
.L876:
	strb	w2, [x0,3130]
.L804:
	add	x21, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x21, 2784
	mov	w2, 32
	add	x0, x0, 104
	bl	ftl_memcpy
	add	x0, x21, 256
	add	x1, x21, 3128
	mov	w2, 32
	bl	ftl_memcpy
.L799:
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w0, [x21,24]
	cbnz	w0, .L805
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L807
	ldr	x0, [x21,1912]
	add	x21, x20, :lo12:.LANCHOR2
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x21,564]
	tbnz	x1, 0, .L807
	mov	w1, 1
	strb	w1, [x21,572]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21,564]
	bl	NandcSetMode
.L807:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	ldrb	w1, [x1,26]
	strb	w1, [x0,136]
	bl	FlashLoadPhyInfo
	cbz	w0, .L805
	add	x21, x20, :lo12:.LANCHOR2
	ldr	w0, [x21,576]
	cbz	w0, .L810
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L877
.L810:
	ldrb	w0, [x21,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21,564]
.L877:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L805
	mov	w0, 1
	add	x21, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x21,1912]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L794
	bl	FlashDieInfoInit
	ldr	x0, [x21,1912]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x21,1912]
	ldrh	w1, [x21,1920]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L812
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L813
.L812:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L813:
	add	x21, x20, :lo12:.LANCHOR2
	ldrb	w0, [x21,564]
	tst	w0, 6
	beq	.L814
	bl	FlashSavePhyInfo
	ldr	w1, [x21,680]
	mov	w0, 0
	bl	FlashDdrParaScan
.L814:
	bl	FlashSavePhyInfo
.L805:
	add	x21, x19, :lo12:.LANCHOR0
	add	x22, x20, :lo12:.LANCHOR2
	ldr	x2, [x21,1912]
	str	xzr, [x22,664]
	ldrb	w0, [x2,26]
	strb	w0, [x21,136]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x21,32]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,720]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,648]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x22,564]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x21,1912]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L816
	adrp	x1, g_retryMode
	adrp	x3, g_maxRegNum
	ldrb	w0, [x0,19]
	mov	x5, x3
	ldrb	w2, [x21,4058]
	strb	w0, [x1,#:lo12:g_retryMode]
	ldrb	w1, [x21,4057]
	strb	w1, [x3,#:lo12:g_maxRegNum]
	adrp	x1, g_maxRetryCount
	strb	w2, [x1,#:lo12:g_maxRetryCount]
	sub	w2, w0, #1
	uxtb	w2, w2
	cmp	w2, 6
	mov	x2, x1
	bhi	.L817
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,664]
	sub	w1, w0, #5
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L818
	mov	w1, 1
	str	w1, [x22,640]
	b	.L819
.L818:
	cmp	w0, 7
	add	x21, x21, 4084
	beq	.L820
.L819:
	add	x21, x19, :lo12:.LANCHOR0
	add	x21, x21, 4076
.L820:
	mov	x1, 0
	mov	w2, w1
.L822:
	ldrsb	w3, [x21,x1]
	add	x1, x1, 1
	cmp	w3, wzr
	csinc	w2, w2, w2, ne
	cmp	x1, 32
	bne	.L822
	cmp	w2, 27
	bls	.L816
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L816
.L817:
	sub	w3, w0, #17
	uxtb	w3, w3
	cmp	w3, 2
	bhi	.L824
	adrp	x2, MicronReadRetrial
	cmp	w0, 19
	add	x2, x2, :lo12:MicronReadRetrial
	str	x2, [x22,664]
	beq	.L825
	mov	w0, 7
	b	.L879
.L825:
	mov	w0, 15
.L879:
	strb	w0, [x1,#:lo12:g_maxRetryCount]
	b	.L816
.L824:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L844
	cmp	w0, 33
	bne	.L826
.L844:
	add	x0, x20, :lo12:.LANCHOR2
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	str	x1, [x0,664]
	mov	w0, 4
	strb	w0, [x5,#:lo12:g_maxRegNum]
	mov	w0, 7
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	b	.L816
.L826:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w4, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L845
	cbz	w4, .L828
.L845:
	add	x1, x20, :lo12:.LANCHOR2
	adrp	x3, ToshibaReadRetrial
	add	x3, x3, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x3, [x1,664]
	beq	.L830
	cmp	w0, 68
	beq	.L830
	mov	w0, 7
	b	.L878
.L830:
	mov	w0, 17
.L878:
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	mov	w0, 4
	cbnz	w4, .L880
	mov	w0, 5
.L880:
	strb	w0, [x5,#:lo12:g_maxRegNum]
	b	.L816
.L828:
	cmp	w0, 49
	bne	.L816
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,664]
.L816:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,568]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L833
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,136]
	cbz	w2, .L833
	ldr	x0, [x0,1912]
	strb	wzr, [x0,18]
.L833:
	ldrb	w0, [x23,#:lo12:IDByte]
	cmp	w0, 44
	bne	.L834
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbz	w0, .L834
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L835
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L834
.L835:
	add	x0, x20, :lo12:.LANCHOR2
	strb	wzr, [x0,572]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L834:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w0, [x1,564]
	tst	w0, 6
	beq	.L836
	ldrb	w1, [x1,572]
	cbnz	w1, .L837
	tbnz	x0, 0, .L836
.L837:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 0
	ldr	w1, [x20,680]
	bl	FlashDdrParaScan
.L836:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w20, 16
	add	x23, x23, :lo12:IDByte
	bl	flash_exit_slc_mode
	ldr	x0, [x19,1912]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	add	x0, x19, 1928
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,25]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19,1912]
	ldrb	w2, [x1,12]
	strh	w2, [x19,4024]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,4020]
	ldr	w2, [x23]
	str	w2, [x19,4016]
	ldrb	w2, [x19,1676]
	strh	w2, [x19,4026]
	ldrb	w2, [x1,13]
	strh	w2, [x19,4028]
	strh	w4, [x19,4030]
	ldrh	w2, [x1,10]
	strh	w2, [x19,4032]
	ldrb	w3, [x1,12]
	ldrh	w2, [x1,10]
	sdiv	w2, w2, w3
	strh	w2, [x19,4034]
	ldrb	w3, [x1,9]
	strh	w3, [x19,4036]
	ldrh	w5, [x1,10]
	ldrb	w2, [x1,9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19,4040]
	ldrb	w5, [x19,25]
	strh	w5, [x19,4042]
	uxth	w2, w2
	ldrb	w5, [x19,24]
	strh	w2, [x19,4038]
	cmp	w5, 1
	bne	.L839
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,4038]
	strb	w20, [x19,25]
	mov	w2, 8
	strh	w4, [x19,4030]
	strh	w3, [x19,4036]
	strh	w2, [x19,4042]
.L839:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L794
.L841:
	mov	w0, -2
.L794:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	uxth	w19, w2
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	x2, [x0,1912]
	ldrb	w0, [x0,136]
	ldrb	w3, [x2,19]
	cbz	w0, .L882
	adrp	x0, .LANCHOR2+568
	ldr	w2, [x0,#:lo12:.LANCHOR2+568]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L881
.L882:
	sub	w0, w3, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L883
	cmp	w3, 68
	beq	.L883
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L881
.L883:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, w1
	add	x24, x20, 652
	mov	w25, 65535
	adrp	x26, .LANCHOR2
.L885:
	ldr	x0, [x20,1912]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L881
	ldrh	w0, [x24,w19,sxtw 1]
	cmp	w0, w25
	bne	.L881
	add	x23, x26, :lo12:.LANCHOR2
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x23,688]
	bl	ftl_memset
	ldr	x2, [x23,688]
	add	w1, w19, w21
	mov	w0, w22
	mov	x3, 0
	add	w19, w19, 1
	bl	FlashProgPage
	uxth	w19, w19
	b	.L885
.L881:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	w25, w1
	adrp	x1, .LANCHOR1+265
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	adrp	x21, .LANCHOR0
	adrp	x23, .LC15
	adrp	x24, .LC17
	ldrb	w26, [x1,#:lo12:.LANCHOR1+265]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w27, w2
	mov	w22, 0
	add	x21, x21, :lo12:.LANCHOR0
	add	x23, x23, :lo12:.LC15
	add	x24, x24, :lo12:.LC17
.L891:
	cmp	w22, w25
	beq	.L925
	mov	w1, w27
	sub	w4, w25, w22
	mov	x0, x19
	add	x2, x29, 124
	add	x3, x29, 120
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,120]
	adrp	x5, .LANCHOR0
	ldrb	w1, [x21,1676]
	cmp	w0, w1
	bcc	.L892
	mov	w0, -1
	str	w0, [x19]
	b	.L893
.L892:
	add	x0, x21, x0, uxtw
	ldrb	w28, [x0,1680]
	str	x5, [x29,96]
	mov	w0, w28
	bl	NandcWaitFlashReady
	mov	w0, w28
	bl	NandcFlashCs
	ldr	w1, [x29,124]
	mov	w0, w28
	bl	FlashReadCmd
	mov	w0, w28
	bl	NandcWaitFlashReady
	ldr	x3, [x19,8]
	mov	w1, 0
	ldr	x4, [x19,16]
	mov	w2, w26
	mov	w0, w28
	bl	NandcXferData
	mov	w6, w0
	ldr	w2, [x21,28]
	mov	w0, w28
	ldr	w1, [x29,124]
	str	x6, [x29,104]
	add	w1, w2, w1
	bl	FlashReadCmd
	ldr	x0, [x19,8]
	mov	w1, 0
	mov	w2, w26
	cmp	x0, xzr
	add	x3, x0, 2048
	ldr	x0, [x19,16]
	csel	x3, x3, xzr, ne
	add	x4, x0, 8
	cmp	x0, xzr
	csel	x4, x4, xzr, ne
	mov	w0, w28
	bl	NandcXferData
	mov	w20, w0
	mov	w0, w28
	bl	NandcFlashDeCs
	ldr	x5, [x29,96]
	ldr	x6, [x29,104]
	add	x0, x5, :lo12:.LANCHOR0
	cmp	w20, w6
	csel	w3, w20, w6, cs
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w3, w0, lsr 2
	bls	.L896
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L896:
	cmp	w3, 256
	beq	.L907
	cmn	w3, #1
	bne	.L897
.L907:
	str	w3, [x19]
	b	.L899
.L897:
	str	wzr, [x19]
.L899:
	ldr	x0, [x19,16]
	cbz	x0, .L900
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L900
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L900
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L900
	str	w1, [x19]
.L900:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L893
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,4044]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L902
	adrp	x0, .LC16
	mov	w2, 4
	add	x0, x0, :lo12:.LC16
	mov	w3, 8
	bl	rknand_print_hex
.L902:
	ldr	x1, [x19,16]
	cbz	x1, .L893
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L893:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L891
.L925:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -176]!
	adrp	x3, .LANCHOR1+265
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	ldrb	w3, [x3,#:lo12:.LANCHOR1+265]
	adrp	x22, .LANCHOR0
	str	w3, [x29,148]
	add	x3, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	str	w1, [x29,144]
	mov	x26, x0
	ldrb	w4, [x3,32]
	ldrb	w23, [x3,24]
	str	w2, [x29,140]
	str	w4, [x29,136]
	cbz	w23, .L966
	bl	FlashReadSlc2KPages
	b	.L1015
.L966:
	adrp	x0, .LC15
	mov	w25, w23
	add	x0, x0, :lo12:.LC15
	str	x0, [x29,128]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	str	x0, [x29,120]
	adrp	x0, .LC18
	add	x0, x0, :lo12:.LC18
	str	x0, [x29,112]
.L927:
	ldr	w0, [x29,144]
	cmp	w25, w0
	bcs	.L1017
	mov	w27, 56
	ldr	w0, [x29,144]
	ldr	w1, [x29,140]
	add	x2, x29, 172
	umull	x27, w25, w27
	sub	w4, w0, w25
	add	x3, x29, 168
	add	x21, x26, x27
	mov	x0, x21
	ldr	w28, [x21,4]
	bl	LogAddr2PhyAddr
	add	x1, x22, :lo12:.LANCHOR0
	mov	w20, w0
	ldr	w0, [x29,168]
	ldrb	w2, [x1,1676]
	cmp	w0, w2
	bcc	.L929
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L930
.L929:
	add	x0, x1, x0, uxtw
	adrp	x24, .LANCHOR2
	ldrb	w19, [x0,1680]
	add	x0, x24, :lo12:.LANCHOR2
	str	x1, [x29,152]
	ldrb	w0, [x0,720]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x1, [x29,152]
	ldr	x0, [x1,1912]
	ldrb	w2, [x0,19]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L932
	add	x1, x1, 4056
	sxtw	x0, w19
	add	x1, x1, x0
	cmp	w2, 7
	ldrb	w3, [x1,12]
	bne	.L933
	ldrb	w3, [x1,20]
.L933:
	add	x1, x24, :lo12:.LANCHOR2
	add	x0, x1, x0
	ldrb	w0, [x0,560]
	cmp	w0, w3
	beq	.L932
	add	x2, x22, :lo12:.LANCHOR0
	mov	w0, w19
	add	x2, x2, 4060
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L932:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L934
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L934
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L935
.L934:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L935:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L973
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L967
.L973:
	cbz	w20, .L939
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,28]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L940
.L939:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L940
.L967:
	mov	w20, w0
.L937:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L941
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,32]
	cbz	w1, .L941
	strb	wzr, [x0,32]
	mov	w20, 0
	b	.L935
.L941:
	cbz	w20, .L942
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w1, [x29,172]
	ldr	w2, [x0,28]
	mov	w0, w19
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w25, 1
	mov	w4, 56
	ldrb	w2, [x29,148]
	mov	w1, 0
	umull	x4, w0, w4
	mov	w0, w19
	add	x4, x26, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w23, w0
	csel	w20, w20, wzr, ne
.L942:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,32]
	bne	.L949
	add	x0, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbnz	w0, .L944
.L948:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x20, [x0,664]
	cbnz	x20, .L945
	b	.L1018
.L944:
	ldr	x0, [x6,3976]
	mov	w4, 1
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	ldr	w20, [x0,304]
	mov	w0, w19
	str	x6, [x29,104]
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L947
	ldr	x6, [x29,104]
	ldrb	w0, [x6,4044]
	cmp	w28, w0, lsr 1
	bls	.L969
.L947:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L948
	b	.L969
.L945:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L950
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,1912]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L951
	ldrb	w1, [x2,4057]
	mov	w0, w19
	add	x2, x2, 4060
	mov	w3, w20
	bl	HynixSetRRPara
.L951:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	add	x4, x22, :lo12:.LANCHOR0
	ldr	x0, [x29,112]
	ldr	w1, [x21,4]
	mov	w3, w28
	str	x4, [x29,104]
	ldrb	w2, [x4,4044]
	bl	printk
	cmn	w28, #1
	ldr	x4, [x29,104]
	bne	.L950
	ldrb	w0, [x4,136]
	cbz	w0, .L950
	ldr	w0, [x29,152]
	cbz	w0, .L952
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L953
.L952:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L953:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,664]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L950
.L1018:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L950
.L969:
	mov	w20, 0
.L949:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L950
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,664]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L950:
	cmp	w28, 256
	beq	.L974
	cmn	w28, #1
	bne	.L954
.L974:
	str	w28, [x26,x27]
	b	.L956
.L954:
	str	wzr, [x26,x27]
.L956:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L958
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,4044]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L958
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L958:
	cbz	w20, .L960
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L961
	add	x24, x24, :lo12:.LANCHOR2
	ldr	x0, [x24,664]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L961:
	add	w0, w25, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L975
	cmn	w23, #1
	bne	.L962
.L975:
	str	w23, [x26,x0]
	b	.L960
.L962:
	str	wzr, [x26,x0]
.L960:
	ldr	w0, [x29,152]
	add	w25, w25, w20
	cbz	w0, .L930
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L930
	mov	w0, w19
	bl	flash_exit_slc_mode
.L930:
	add	w25, w25, 1
	b	.L927
.L1017:
	mov	w0, 0
	b	.L1015
.L940:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L937
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L937
.L1015:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	stp	x29, x30, [sp, -192]!
	mov	w2, 16
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR2
	mov	w27, -1
	ldrh	w1, [x0,4028]
	add	x19, x23, :lo12:.LANCHOR2
	ldrh	w21, [x0,4030]
	add	x0, x19, 728
	mov	w28, 0
	adrp	x26, .LC19
	add	x26, x26, :lo12:.LC19
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,696]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1020:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1676]
	cmp	w1, w19
	bls	.L1030
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1021:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1023
	add	w1, w3, w20
	add	x0, x29, 136
	lsl	w1, w1, 10
	str	w1, [x29,140]
	mov	w1, 1
	str	x4, [x29,104]
	mov	w2, w1
	str	x3, [x29,112]
	bl	FlashReadPages
	ldr	w0, [x29,136]
	ldr	x3, [x29,112]
	cmn	w0, #1
	ldr	x4, [x29,104]
	beq	.L1022
	add	x24, x23, :lo12:.LANCHOR2
	ldr	x0, [x24,696]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1022
	add	x24, x24, 728
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1023
.L1022:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1021
.L1023:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1676]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1020
.L1030:
	mov	w0, w27
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x22, x0
	adrp	x0, .LANCHOR0
	add	x4, x0, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w23, w1
	ldr	x0, [x0,#:lo12:.LANCHOR0]
	ldrh	w3, [x4,4028]
	mov	w25, w2
	ldrh	w1, [x4,4030]
	adrp	x4, .LANCHOR2
	str	x0, [x29,96]
	add	x0, x4, :lo12:.LANCHOR2
	mov	x21, x4
	mov	w26, 61664
	mul	w3, w3, w1
	ldr	x0, [x0,696]
	str	x0, [x29,104]
	uxth	w3, w3
	sub	w20, w3, #1
	sub	w19, w3, #16
	mul	w24, w23, w3
	uxth	w20, w20
.L1032:
	cmp	w20, w19
	ble	.L1040
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1033
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,696]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1033
	mov	w0, w22
	cbz	x22, .L1034
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.6
	b	.L1034
.L1033:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1032
.L1040:
	mov	w0, -1
.L1034:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 144
	ret
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x21, [sp,32]
	mov	x21, x0
	adrp	x0, .LANCHOR0+1912
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x0, [x0,#:lo12:.LANCHOR0+1912]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR2
	mul	w19, w2, w19
	ldr	x0, [x0,688]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L1042
.L1046:
	mov	w1, 0
	b	.L1043
.L1042:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L1044:
	cmp	w2, w19
	bge	.L1043
	add	x3, x20, :lo12:.LANCHOR2
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,688]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L1045
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L1045:
	cmp	w1, w0
	bcs	.L1046
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1044
.L1043:
	ubfiz	x1, x1, 1, 16
	mov	w0, -1
	ldp	x19, x20, [sp,16]
	strh	w0, [x21,x1]
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	w23, w1
	adrp	x1, .LANCHOR1+265
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x22, .LANCHOR0
	ldrb	w26, [x1,#:lo12:.LANCHOR1+265]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w24, w2
	mov	w27, w3
	mov	x19, x0
	mov	x21, x0
	mov	w25, 0
	add	x22, x22, :lo12:.LANCHOR0
	mov	w28, -1
.L1052:
	cmp	w25, w23
	beq	.L1081
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1676]
	cmp	w0, w1
	bcc	.L1053
	str	w28, [x21]
	b	.L1054
.L1053:
	add	x0, x22, x0, uxtw
	ldrb	w20, [x0,1680]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x3, [x21,8]
	mov	w2, w26
	ldr	x4, [x21,16]
	mov	w1, 1
	mov	w0, w20
	bl	NandcXferData
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashReadStatus
	sbfx	x0, x0, 0, 1
	str	w0, [x21]
	mov	w0, w20
	ldr	w1, [x29,96]
	ldr	w2, [x22,28]
	add	w1, w2, w1
	bl	FlashProgFirstCmd
	ldr	x0, [x21,8]
	mov	w1, 1
	mov	w2, w26
	cmp	x0, xzr
	add	x3, x0, 2048
	ldr	x0, [x21,16]
	csel	x3, x3, xzr, ne
	add	x4, x0, 8
	cmp	x0, xzr
	csel	x4, x4, xzr, ne
	mov	w0, w20
	bl	NandcXferData
	adrp	x0, .LANCHOR0
	ldr	w1, [x29,96]
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w2, [x0,28]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashReadStatus
	tbz	x0, 0, .L1057
	str	w28, [x21]
.L1057:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1054:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1052
.L1081:
	mov	w21, 0
	cbz	w27, .L1079
	adrp	x25, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	add	x25, x25, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1080:
	cmp	w21, w23
	beq	.L1079
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1060
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1061
.L1060:
	adrp	x20, .LANCHOR2
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,704]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,712]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,704]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,712]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1062
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1062:
	ldr	x0, [x19,16]
	cbz	x0, .L1063
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1063
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1063:
	ldr	x0, [x19,8]
	cbz	x0, .L1061
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,704]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1061
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1061:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1080
.L1079:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -192]!
	adrp	x6, .LANCHOR1+265
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	mov	w22, 0
	add	x4, x21, :lo12:.LANCHOR0
	str	w3, [x29,124]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w25, w1
	ldr	x5, [x4,1912]
	mov	w23, w2
	ldrb	w4, [x4,24]
	ldrb	w27, [x6,#:lo12:.LANCHOR1+265]
	ldrb	w5, [x5,19]
	cbz	w4, .L1083
	bl	FlashProgSlc2KPages
	b	.L1084
.L1083:
	sub	w0, w5, #1
	adrp	x28, .LANCHOR2
	str	w0, [x29,120]
	add	x0, x28, :lo12:.LANCHOR2
	str	x0, [x29,112]
.L1135:
	cmp	w22, w25
	bcs	.L1136
	mov	w20, 56
	mov	w1, w23
	add	x2, x29, 128
	sub	w4, w25, w22
	umull	x20, w22, w20
	add	x3, x29, 132
	add	x26, x19, x20
	mov	x0, x26
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w2, [x1,1676]
	cmp	w0, w2
	bcc	.L1085
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1086
.L1085:
	add	x3, x28, :lo12:.LANCHOR2
	uxtw	x0, w0
	ldrb	w3, [x3,648]
	cmp	w3, wzr
	mov	x3, 24
	csel	w24, w24, wzr, ne
	madd	x0, x0, x3, x1
	ldr	x0, [x0,1728]
	cbz	x0, .L1088
	cmp	w2, 1
	bne	.L1089
	ldr	x0, [x1,3976]
	bl	NandcIqrWaitFlashReady
.L1089:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1088:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	mov	x1, 24
	add	x0, x0, 1720
	madd	x0, x2, x1, x0
	ldr	w1, [x29,128]
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1090
	add	w1, w22, 1
	mov	w3, 56
	umull	x1, w1, w3
	add	x1, x19, x1
	str	x1, [x0,16]
.L1090:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	ldrb	w20, [x0,1680]
	mov	x0, 24
	madd	x2, x2, x0, x1
	ldrb	w0, [x1,1676]
	strb	w20, [x2,1720]
	cmp	w0, 1
	bne	.L1091
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1092
.L1091:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 1688
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1092:
	ldr	w0, [x29,120]
	cmp	w0, 6
	bhi	.L1093
	ldr	x0, [x29,112]
	add	x0, x0, x20, sxtw
	ldrb	w0, [x0,560]
	cbz	w0, .L1093
	add	x2, x21, :lo12:.LANCHOR0
	mov	w0, w20
	add	x2, x2, 4060
	mov	w3, 0
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L1093:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1094
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L1094
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1095
.L1094:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1095:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x3, [x26,8]
	mov	w0, w20
	ldr	x4, [x26,16]
	mov	w1, 1
	mov	w2, w27
	bl	NandcXferData
	cbz	w24, .L1096
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 1688
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x26,28]
	mov	w0, w20
	ldr	w1, [x29,128]
	add	w1, w2, w1
	bl	FlashProgDpSecondCmd
	add	w0, w22, 1
	mov	w4, 56
	mov	w1, 1
	mov	w2, w27
	umull	x4, w0, w4
	mov	w0, w20
	add	x4, x19, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
.L1096:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1086:
	add	w22, w22, 1
	b	.L1135
.L1136:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	mov	x22, 24
	ldr	x0, [x21,3976]
	bl	NandcIqrWaitFlashReady
.L1098:
	ldrb	w0, [x21,1676]
	cmp	w0, w20
	bls	.L1137
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1099
	ldrb	w0, [x21,136]
	cbz	w0, .L1099
	madd	x0, x20, x22, x21
	ldrb	w0, [x0,1720]
	bl	flash_exit_slc_mode
.L1099:
	add	x20, x20, 1
	b	.L1098
.L1137:
	ldr	w0, [x29,124]
	cbnz	w0, .L1101
.L1109:
	mov	w0, 0
	b	.L1084
.L1101:
	adrp	x24, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	mov	w21, 0
	add	x24, x24, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1102:
	cmp	w21, w25
	beq	.L1109
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1103
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1104
.L1103:
	adrp	x20, .LANCHOR2
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,704]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,712]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,704]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,712]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1105
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1105:
	ldr	x0, [x19,16]
	cbz	x0, .L1106
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1106
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1106:
	ldr	x0, [x19,8]
	cbz	x0, .L1104
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,704]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1104
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1104:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1102
.L1084:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FlashTestBlk.part.7, %function
FlashTestBlk.part.7:
	stp	x29, x30, [sp, -160]!
	mov	w1, 165
	mov	w2, 32
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR2
	lsl	w20, w20, 10
	ldr	x0, [x19,688]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,688]
	mov	w1, 90
	mov	w2, 8
	bl	ftl_memset
	str	w20, [x29,44]
	mov	w1, 1
	add	x0, x29, 40
	mov	w2, w1
	bl	FlashEraseBlocks
	mov	w1, 1
	add	x0, x29, 40
	mov	w3, w1
	mov	w2, w1
	bl	FlashProgPages
	ldr	w0, [x29,40]
	mov	w1, 0
	mov	w2, 1
	cmp	w0, wzr
	add	x0, x29, 40
	csetm	w19, ne
	bl	FlashEraseBlocks
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashTestBlk.part.7, .-FlashTestBlk.part.7
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	uxth	w1, w0
	adrp	x0, .LANCHOR0+8
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w2, [x0,#:lo12:.LANCHOR0+8]
	mov	w0, 0
	cmp	w1, w2
	bcc	.L1140
	mov	w0, w1
	bl	FlashTestBlk.part.7
.L1140:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -224]!
	adrp	x2, .LANCHOR2
	add	x29, sp, 0
	add	x0, x2, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x19, .LANCHOR0
	str	x2, [x29,136]
	ldr	x0, [x0,696]
	mov	w20, 0
	str	x0, [x29,152]
	add	x0, x19, :lo12:.LANCHOR0
	ldrh	w1, [x0,4028]
	ldrh	w22, [x0,4030]
	ldrh	w25, [x0,28]
	mul	w22, w1, w22
	ldr	x1, [x0,1912]
	ldrb	w0, [x0,24]
	uxth	w22, w22
	ldrb	w1, [x1,24]
	cmp	w0, 1
	str	w1, [x29,148]
	adrp	x0, .LC24
	ubfiz	w1, w25, 1, 15
	add	x0, x0, :lo12:.LC24
	csel	w25, w1, w25, eq
	mov	w1, 1
	bl	printk
	ldr	x0, [x29,136]
	mov	w1, 0
	mov	w2, 4096
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,696]
	bl	ftl_memset
	lsr	w0, w22, 4
	str	w0, [x29,144]
	ldr	w0, [x29,148]
	and	w0, w0, 1
	str	w0, [x29,124]
	ldr	w0, [x29,148]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,120]
	sub	w0, w22, #1
	uxth	w0, w0
	str	w0, [x29,132]
.L1144:
	add	x23, x19, :lo12:.LANCHOR0
	ldrb	w0, [x23,1676]
	cmp	w0, w20
	bls	.L1195
	ldr	x0, [x29,136]
	sxtw	x24, w20
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 728
	ldrh	w21, [x0,w20,sxtw 1]
	cbnz	w21, .L1174
	ldrh	w2, [x23,4036]
	mov	w1, w21
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	add	x23, x23, x24
	lsl	w2, w2, 9
	mov	w26, w21
	bl	ftl_memset
	ldrb	w27, [x23,1680]
	ldr	w0, [x29,148]
	mov	w23, w21
	and	w0, w0, 4
	uxtb	w0, w0
	str	w0, [x29,128]
.L1146:
	uxth	w28, w26
	cmp	w28, w22
	bcs	.L1156
	mov	w0, -1
	strb	w0, [x29,166]
	strb	w0, [x29,167]
	ldr	w0, [x29,124]
	cbz	w0, .L1148
	add	x4, x19, :lo12:.LANCHOR0
	add	x2, x29, 166
	add	x0, x4, 1688
	str	x4, [x29,104]
	ldr	w3, [x0,x24,lsl 2]
	mov	w0, w27
	add	w3, w21, w3
	str	x3, [x29,112]
	mov	w1, w3
	bl	FlashReadSpare
	ldr	x4, [x29,104]
	ldr	x3, [x29,112]
	ldrb	w0, [x4,24]
	cmp	w0, 1
	bne	.L1148
	ldr	w1, [x4,28]
	mov	w0, w27
	add	x2, x29, 167
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,166]
	ldrb	w1, [x29,167]
	and	w0, w1, w0
	strb	w0, [x29,166]
.L1148:
	ldr	w0, [x29,120]
	cbz	w0, .L1150
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x29, 167
	add	x0, x0, 1688
	ldr	x1, [x0,224]
	ldr	w0, [x0,x24,lsl 2]
	ldrh	w1, [x1,10]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w21
	bl	FlashReadSpare
.L1150:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x29,166]
	ldr	x0, [x0,1912]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1175
	cmp	w0, 1
	bne	.L1151
.L1175:
	mov	w0, 1
	cbz	w1, .L1153
	ldrb	w0, [x29,167]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1153
.L1151:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1153
	ldrb	w0, [x29,167]
	cmp	w0, 255
	cset	w0, ne
.L1153:
	ldr	w1, [x29,128]
	cbz	w1, .L1154
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1688
	ldr	w1, [x0,x24,lsl 2]
	mov	w0, w27
	add	w1, w21, w1
	bl	SandiskProgTestBadBlock
.L1154:
	cbz	w0, .L1155
	adrp	x0, .LC25
	mov	w1, w20
	mov	w2, w26
	add	x0, x0, :lo12:.LC25
	add	w23, w23, 1
	bl	printk
	ubfx	x1, x28, 5, 11
	ldr	x3, [x19,#:lo12:.LANCHOR0]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w28, w0, w28
	add	x2, x19, :lo12:.LANCHOR0
	uxth	w23, w23
	ldr	w0, [x3,x1]
	orr	w28, w0, w28
	str	w28, [x3,x1]
	ldr	w1, [x29,144]
	ldrb	w0, [x2,1676]
	mul	w0, w1, w0
	cmp	w23, w0
	bgt	.L1156
.L1155:
	add	w26, w26, 1
	add	w21, w21, w25
	b	.L1146
.L1156:
	adrp	x0, .LC26
	mov	w1, w20
	mov	w2, w23
	add	x0, x0, :lo12:.LC26
	bl	printk
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w2, [x29,144]
	ldrb	w0, [x1,1676]
	mul	w0, w2, w0
	cmp	w23, w0
	blt	.L1158
	ldrh	w2, [x1,4036]
	mov	w1, 0
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1158:
	cbnz	w20, .L1160
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x26, .LC27
	mov	w21, w20
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
	ldrh	w27, [x0,8]
.L1161:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,25]
	cmp	w0, w27
	bls	.L1196
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1162
	mov	w1, w27
	mov	x0, x26
	add	w21, w21, 1
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	lsl	x1, x1, 2
	lsl	w0, w23, w27
	uxth	w21, w21
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1162:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1161
.L1196:
	adrp	x26, .LC27
	ldr	w28, [x29,132]
	sub	w27, w22, #50
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
.L1164:
	cmp	w28, w27
	ble	.L1197
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1165
	mov	w1, w28
	mov	x0, x26
	bl	printk
	ubfx	x1, x28, 5, 11
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	lsl	x1, x1, 2
	lsl	w0, w23, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1165:
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1164
.L1197:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	ldr	w2, [x0,8]
	sub	w1, w1, w2
	cmp	w21, w1
	bcc	.L1160
	ldrh	w2, [x0,4036]
	mov	w1, 0
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1160:
	ldr	x0, [x29,136]
	adrp	x23, .LC28
	ldr	w27, [x29,132]
	add	x23, x23, :lo12:.LC28
	mul	w26, w20, w22
	add	x28, x0, :lo12:.LANCHOR2
	mov	w21, -3872
.L1168:
	mov	w2, w27
	mov	x0, x23
	mov	w1, w20
	bl	printk
	ldr	x2, [x19,#:lo12:.LANCHOR0]
.L1169:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1198
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1169
.L1198:
	add	x0, x28, 728
	strh	w27, [x0,x24,lsl 1]
	ldr	x0, [x29,152]
	strh	w21, [x0]
	strh	w27, [x0,2]
	strh	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	mov	w2, w1
	str	x0, [x29,176]
	ldr	x0, [x28,696]
	str	x0, [x29,184]
	add	w0, w27, w26
	lsl	w0, w0, 10
	str	w0, [x29,172]
	add	x0, x29, 168
	bl	FlashEraseBlocks
	mov	w1, 1
	add	x0, x29, 168
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x29,168]
	cbz	w0, .L1174
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1168
.L1174:
	add	w20, w20, 1
	uxtb	w20, w20
	b	.L1144
.L1195:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L1200:
	cmp	w2, w0
	bhi	.L1202
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L1200
.L1202:
	sub	w0, w1, #1
	ret
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	ret
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	uxth	w0, w0
	mov	w1, 24
	cmp	w0, 23
	csel	w0, w0, w1, hi
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldrh	w2, [x1,748]
	ldrh	w3, [x1,758]
	str	w0, [x1,744]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,756]
	ldr	w0, [x1,764]
	str	w2, [x1,752]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,760]
	ret
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	stp	x29, x30, [sp, -96]!
	mov	x3, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x24, x0
	adrp	x19, .LANCHOR2
	ldrh	w6, [x0,8]
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x24,10]
	ldrh	w1, [x24,12]
	ldrh	w4, [x24,14]
	strh	w6, [x0,768]
	strh	w2, [x0,770]
	strh	w1, [x0,772]
	strh	w4, [x0,758]
.L1207:
	add	x5, x0, 776
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1207
	ldrh	w3, [x24,20]
	ldrb	w0, [x24,15]
	cmp	w3, w0
	bcs	.L1208
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR2
.L1209:
	cmp	w3, w1
	bcs	.L1211
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L1212:
	cmp	w7, w2
	bcs	.L1234
	add	x9, x12, 776
	add	w13, w5, w11
	add	w14, w0, w8
	add	w7, w7, 1
	strb	w0, [x9,w5,uxtw]
	add	w0, w0, w10
	strb	w14, [x9,x13]
	add	w5, w5, w1
	uxtb	w0, w0
	b	.L1212
.L1234:
	add	w3, w3, 1
	b	.L1209
.L1211:
	add	x0, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,770]
	strh	w4, [x0,758]
.L1208:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,808]
	strh	wzr, [x0,810]
	bne	.L1213
	strh	w6, [x0,808]
.L1213:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 4352
	adrp	x22, .LANCHOR0
	strh	w2, [x0,812]
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w27, [x2,24]
	cbz	w27, .L1214
	mov	w2, 384
	strh	w2, [x0,812]
.L1214:
	add	x21, x19, :lo12:.LANCHOR2
	ldrh	w20, [x21,758]
	ldrh	w23, [x21,770]
	mul	w23, w1, w23
	mul	w1, w20, w1
	uxth	w23, w23
	strh	w23, [x21,748]
	uxth	w0, w1
	strh	w0, [x21,814]
	bl	Ftl_log2
	ldrh	w26, [x24,16]
	ldrh	w25, [x24,20]
	strh	w0, [x21,816]
	mul	w0, w23, w26
	strh	w0, [x21,822]
	mov	w0, w25
	ldrh	w28, [x24,18]
	strh	w26, [x21,818]
	strh	w28, [x21,820]
	strh	w25, [x21,824]
	bl	Ftl_log2
	lsl	w2, w25, 9
	uxth	w3, w0
	strh	w0, [x21,826]
	cmp	w20, 1024
	ldrh	w0, [x24,26]
	strh	w2, [x21,828]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,832]
	lsl	w2, w2, 1
	mul	w0, w23, w20
	strh	w2, [x21,830]
	str	w0, [x21,764]
	bls	.L1215
	and	w0, w20, 255
	strh	w0, [x21,810]
.L1215:
	add	x2, x19, :lo12:.LANCHOR2
	mul	w0, w25, w28
	ldrh	w1, [x2,810]
	sub	w1, w20, w1
	mul	w1, w23, w1
	mul	w1, w1, w25
	mul	w1, w1, w26
	asr	w1, w1, 11
	str	w1, [x2,836]
	ldrh	w1, [x2,812]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L1231
	mov	w0, 4
.L1231:
	strh	w0, [x2,840]
	cbz	w27, .L1218
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0,812]
.L1218:
	add	x1, x19, :lo12:.LANCHOR2
	lsl	w20, w20, 6
	cmp	w23, 1
	ldrh	w0, [x1,812]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w20, w20, w3
	strh	w20, [x1,844]
	and	w20, w20, 65535
	add	w0, w0, 2
	strh	w0, [x1,842]
	mul	w0, w20, w23
	str	w0, [x1,848]
	add	w20, w20, 8
	ldrh	w0, [x1,840]
	udiv	w0, w0, w23
	add	w20, w20, w0
	bne	.L1232
	add	w20, w20, 4
.L1232:
	str	w20, [x1,744]
	add	x20, x19, :lo12:.LANCHOR2
	add	x22, x22, :lo12:.LANCHOR0
	ldrh	w0, [x20,744]
	bl	FtlSysBlkNumInit
	str	wzr, [x22,4048]
	ldr	w0, [x20,744]
	str	w0, [x20,852]
	ldr	w0, [x20,760]
	ldrh	w1, [x20,818]
	lsl	w0, w0, 2
	ldrh	w3, [x20,824]
	ldrh	w2, [x20,840]
	mul	w1, w0, w1
	ldrh	w0, [x20,826]
	ldrb	w5, [x22,136]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x20,858]
	uxth	w0, w0
	strh	w0, [x20,856]
	add	w1, w2, 3
	strh	w1, [x20,840]
	ldr	w1, [x20,848]
	add	w4, w1, 3
	str	w4, [x20,848]
	cbz	w5, .L1221
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x20,840]
	b	.L1233
.L1221:
	cmp	w4, 7
	bhi	.L1222
	mov	w1, 8
.L1233:
	str	w1, [x20,848]
.L1222:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,756]
	strh	wzr, [x19,860]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L1223
	mov	w0, 1
	strh	w0, [x19,860]
.L1223:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	stp	x29, x30, [sp, -64]!
	mov	w1, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	str	x23, [sp,48]
	add	x0, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	str	w1, [x0,932]
	mov	w1, -1
	strh	wzr, [x0,862]
	strh	w1, [x0,948]
	strh	w1, [x0,950]
	strh	w1, [x0,952]
	str	wzr, [x0,864]
	str	wzr, [x0,868]
	str	wzr, [x0,872]
	str	wzr, [x0,876]
	str	wzr, [x0,880]
	str	wzr, [x0,884]
	str	wzr, [x0,888]
	str	wzr, [x0,892]
	str	wzr, [x0,896]
	str	wzr, [x0,900]
	str	wzr, [x0,904]
	str	wzr, [x0,908]
	str	wzr, [x0,912]
	str	wzr, [x0,916]
	str	wzr, [x0,920]
	str	wzr, [x0,924]
	str	wzr, [x0,928]
	str	wzr, [x0,936]
	str	wzr, [x0,940]
	str	wzr, [x0,944]
	strh	w1, [x0,954]
	mov	w1, 32
	ldrh	w2, [x0,824]
	strh	w1, [x0,956]
	mov	w1, 128
	strh	w1, [x0,958]
	mov	w1, 1024
	strh	wzr, [x0,960]
	sdiv	w2, w1, w2
	ldrh	w1, [x0,748]
	strh	wzr, [x0,962]
	lsl	w1, w1, 2
	strh	wzr, [x0,964]
	strh	wzr, [x0,966]
	cmp	w2, w1
	str	w2, [x0,968]
	bls	.L1236
	str	w1, [x0,968]
.L1236:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19,822]
	str	wzr, [x19,972]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,976]
	ldrh	w1, [x19,822]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,748]
	str	x0, [x19,984]
	mul	w21, w21, w22
	lsl	w23, w21, 3
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,992]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1000]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,1008]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1016]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1024]
	ldr	w0, [x19,968]
	mul	w0, w0, w22
	bl	ftl_malloc
	str	x0, [x19,1032]
	ldrh	w0, [x19,748]
	ldrh	w21, [x19,828]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,1040]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1048]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1056]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1064]
	ldr	w0, [x19,1040]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,1072]
	ldr	w0, [x19,968]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,1080]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1088]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1096]
	ldr	w1, [x19,1040]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,748]
	str	x0, [x19,1104]
	ldrh	w0, [x19,830]
	mul	w21, w0, w21
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1112]
	lsl	w0, w21, 3
	bl	ftl_malloc
	str	x0, [x19,1120]
	ldrh	w1, [x19,830]
	ldr	w0, [x19,1040]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1128]
	ldrh	w1, [x19,830]
	ldr	w0, [x19,968]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1136]
	ldrh	w0, [x19,758]
	lsl	w0, w0, 1
	strh	w0, [x19,1144]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,1152]
	ldrh	w0, [x19,1144]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,1144]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19,758]
	str	x0, [x19,1160]
	add	x0, x0, 32
	lsl	w21, w21, 1
	str	x0, [x19,1168]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1176]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19,848]
	str	x0, [x19,1184]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1192]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1200]
	ldrh	w0, [x19,758]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	adrp	x1, .LANCHOR0+16
	str	x0, [x1,#:lo12:.LANCHOR0+16]
	ldrh	w0, [x19,840]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1208]
	ldrh	w0, [x19,840]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1216]
	ldrh	w0, [x19,840]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1224]
	ldrh	w0, [x19,842]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1232]
	ldrh	w2, [x19,842]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w21, [x19,856]
	lsl	w21, w21, 2
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1240]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1248]
	ldr	w0, [x19,848]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1256]
	ldrh	w0, [x19,858]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,1264]
	ldrh	w1, [x19,858]
	ldrh	w0, [x19,828]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1272]
	ldrh	w1, [x19,758]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1280]
	ldrh	w0, [x19,814]
	ldrh	w1, [x19,770]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,1288]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1328]
	add	x1, x19, 1296
	ldrh	w4, [x19,1288]
	ldrh	w5, [x19,770]
	mov	w0, 1
	mov	w2, w4
.L1237:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L1337
	ldr	x3, [x19,1328]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L1237
.L1337:
	add	x1, x20, :lo12:.LANCHOR2
.L1239:
	cmp	w0, 8
	beq	.L1338
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,1328]
	b	.L1239
.L1338:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1192]
	cbnz	x1, .L1241
.L1243:
	adrp	x0, .LC29
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC29
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L1242
.L1241:
	ldr	x1, [x0,1200]
	cbz	x1, .L1243
	ldr	x1, [x0,1240]
	cbz	x1, .L1243
	ldr	x1, [x0,1256]
	cbz	x1, .L1243
	ldr	x1, [x0,1264]
	cbz	x1, .L1243
	ldr	x1, [x0,1272]
	cbz	x1, .L1243
	ldr	x1, [x0,1280]
	cbz	x1, .L1243
	ldr	x1, [x0,1328]
	cbz	x1, .L1243
	ldr	x0, [x0,1184]
	cbz	x0, .L1243
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,976]
	cbz	x1, .L1243
	ldr	x1, [x0,984]
	cbz	x1, .L1243
	ldr	x1, [x0,992]
	cbz	x1, .L1243
	ldr	x1, [x0,1008]
	cbz	x1, .L1243
	ldr	x1, [x0,1016]
	cbz	x1, .L1243
	ldr	x1, [x0,1024]
	cbz	x1, .L1243
	ldr	x1, [x0,1000]
	cbz	x1, .L1243
	ldr	x1, [x0,1048]
	cbz	x1, .L1243
	ldr	x1, [x0,1056]
	cbz	x1, .L1243
	ldr	x0, [x0,1064]
	cbz	x0, .L1243
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1072]
	cbz	x1, .L1243
	ldr	x1, [x0,1088]
	cbz	x1, .L1243
	ldr	x1, [x0,1096]
	cbz	x1, .L1243
	ldr	x1, [x0,1104]
	cbz	x1, .L1243
	ldr	x1, [x0,1112]
	cbz	x1, .L1243
	ldr	x1, [x0,1120]
	cbz	x1, .L1243
	ldr	x1, [x0,1128]
	cbz	x1, .L1243
	ldr	x1, [x0,1168]
	cbz	x1, .L1243
	ldr	x1, [x0,1152]
	cbz	x1, .L1243
	ldr	x0, [x0,1208]
	cbz	x0, .L1243
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,1216]
	cbz	x1, .L1243
	ldr	x1, [x0,1224]
	cbz	x1, .L1243
	ldr	x0, [x0,1232]
	cbz	x0, .L1243
	mov	w0, 0
.L1242:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	adrp	x1, .LANCHOR2
	uxth	w4, w0
	add	x1, x1, :lo12:.LANCHOR2
	mov	w0, 0
	ldrh	w2, [x1,1392]
	cbz	w2, .L1340
	ldrh	w2, [x1,840]
	ldr	x3, [x1,1208]
	mov	x1, 0
.L1341:
	cmp	w2, w1, uxth
	bls	.L1346
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L1341
	mov	w0, 1
	b	.L1340
.L1346:
	mov	w0, 0
.L1340:
	ret
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	adrp	x2, .LANCHOR2
	mov	w6, w0
	add	x2, x2, :lo12:.LANCHOR2
	mov	w0, 0
	ldr	w3, [x2,972]
	cbz	w3, .L1348
	ldr	x5, [x2,1032]
	mov	x0, 56
	mov	x2, 0
.L1349:
	cmp	w3, w2
	bls	.L1358
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L1349
	cmp	w4, w1
	bhi	.L1349
	mov	w0, 1
	b	.L1348
.L1358:
	mov	w0, 0
.L1348:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR0+4048
	ldr	w0, [x0,#:lo12:.LANCHOR0+4048]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR0+4048
	ldr	w0, [x0,#:lo12:.LANCHOR0+4048]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR2+1396
	ldr	w0, [x0,#:lo12:.LANCHOR2+1396]
	ret
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	stp	x29, x30, [sp, -32]!
	uxth	w1, w0
	mov	w4, 1
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w3, [x19,814]
	add	x19, x19, 1296
	udiv	w2, w1, w3
	add	x0, x19, x2, uxth 3
	ldr	x0, [x0,32]
	msub	w3, w2, w3, w1
	uxth	w3, w3
	ubfx	x6, x3, 5, 11
	lsl	w4, w4, w3
	lsl	x6, x6, 2
	ldr	w5, [x0,x6]
	orr	w4, w4, w5
	str	w4, [x0,x6]
	adrp	x0, .LC30
	add	x0, x0, :lo12:.LC30
	bl	printk
	ldrh	w0, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,6]
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	add	x3, x2, :lo12:.LANCHOR2
	ldrh	w1, [x3,814]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,1328]
	uxth	w0, w0
	ubfx	x3, x0, 5, 11
	ldr	w1, [x1,x3,lsl 2]
	lsr	w0, w1, w0
	and	w0, w0, 1
	ret
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	ret
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x0
	adrp	x0, .LANCHOR2+1288
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+1288]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L1367:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L1365
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L1367
.L1365:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w1, [x19,1400]
	stp	x25, x26, [sp,64]
	cbnz	w1, .L1372
	ldr	x0, [x19,1048]
	mov	x22, 0
	ldr	x3, [x19,1112]
	add	x21, x19, 1296
	ldrh	w2, [x19,828]
	str	x0, [x19,1416]
	str	x3, [x19,1424]
	bl	ftl_memset
.L1373:
	ldrh	w1, [x19,770]
	mov	w0, w22
	add	x20, x19, 1408
	add	x22, x22, 1
	cmp	w0, w1
	bge	.L1381
	add	x1, x21, x22, lsl 3
	ldrh	w2, [x19,1288]
	ldr	x3, [x20,8]
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1373
.L1381:
	ldr	x26, [x20,16]
	mov	w1, 255
	mov	w2, 16
	mov	w22, 0
	mov	x0, x26
	adrp	x23, .LC31
	adrp	x25, .LC32
	mov	w24, w22
	bl	ftl_memset
	add	x23, x23, :lo12:.LC31
	mov	w0, -3887
	strh	w0, [x26]
	add	x25, x25, :lo12:.LC32
	ldr	w0, [x21,8]
	str	w0, [x26,4]
	ldrh	w0, [x19,1296]
	strh	w0, [x26,2]
	ldrh	w0, [x21,4]
	strh	w0, [x26,8]
	ldrh	w0, [x21,6]
	strh	w0, [x26,10]
	ldr	w0, [x19,744]
	strh	w0, [x26,12]
.L1375:
	ldr	x0, [x19,1048]
	str	x0, [x20,8]
	ldrh	w1, [x19,1296]
	ldr	x0, [x19,1112]
	str	x0, [x20,16]
	ldrh	w2, [x19,1298]
	ldrh	w3, [x19,1300]
	orr	w0, w2, w1, lsl 10
	ldrh	w4, [x26,10]
	str	w0, [x20,4]
	mov	x0, x23
	str	wzr, [x20]
	bl	printk
	ldrh	w1, [x19,820]
	ldrh	w0, [x19,1298]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1376
	ldr	w1, [x19,1304]
	ldrh	w0, [x19,1296]
	add	w1, w1, 1
	str	w1, [x19,1304]
	str	w1, [x26,4]
	strh	w0, [x26,8]
	ldrh	w1, [x19,1300]
	strh	w0, [x19,1300]
	ldr	x0, [x19,1016]
	strh	w1, [x19,1296]
	lsl	w1, w1, 10
	str	w1, [x20,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x19,1298]
	ldr	x0, [x19,1016]
	bl	FlashEraseBlocks
.L1376:
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,1298]
	ldr	w0, [x20]
	add	w1, w1, 1
	strh	w1, [x19,1298]
	cmn	w0, #1
	bne	.L1377
	ldr	w1, [x20,4]
	mov	x0, x25
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	cmp	w22, 3
	bls	.L1375
	adrp	x0, .LC33
	ldr	w1, [x20,4]
	add	x0, x0, :lo12:.LC33
	mov	w2, w22
	bl	printk
	mov	w0, 1
	str	w0, [x19,1400]
	b	.L1372
.L1377:
	cbnz	w24, .L1372
	mov	w24, 1
	b	.L1375
.L1372:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	add	x21, x1, 1408
	add	x23, x1, 1296
	ldr	x0, [x1,1048]
	mov	w22, 0
	ldr	x24, [x1,1112]
	mov	x19, x1
	str	x0, [x21,8]
	mov	w25, -1
	str	x24, [x21,16]
	mov	w26, 61664
.L1383:
	ldrh	w0, [x19,770]
	cmp	w22, w0
	bcs	.L1392
	ldrh	w20, [x19,814]
	strh	w25, [x23,12]
.L1385:
	sub	w20, w20, #1
	ldrh	w0, [x19,814]
	uxth	w20, w20
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1386
	madd	w0, w22, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1385
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1385
	strh	w20, [x23,12]
.L1386:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L1383
.L1392:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	adrp	x0, .LANCHOR2
	mov	w1, -1
	stp	x29, x30, [sp, -16]!
	add	x0, x0, :lo12:.LANCHOR2
	mov	w2, 16
	add	x29, sp, 0
	add	x0, x0, 1308
	strh	w1, [x0,-12]
	mov	w1, 255
	strh	wzr, [x0,-6]
	bl	ftl_memset
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	stp	x29, x30, [sp, -48]!
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	mov	w19, 0
	mov	w20, w19
	ldrh	w1, [x0,814]
	ldrh	w21, [x0,770]
	mul	w21, w1, w21
.L1395:
	cmp	w20, w21
	bge	.L1401
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1396
	add	w19, w19, 1
	uxth	w19, w19
.L1396:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1395
.L1401:
	mov	w0, w19
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x21, x19
	ldr	w22, [x20,1400]
	cbnz	w22, .L1403
	add	x24, x20, 1296
	mov	x19, x20
	mov	x25, x24
	add	x20, x20, 1408
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1404:
	ldrh	w0, [x19,770]
	cmp	w22, w0
	bcs	.L1422
	ldrh	w4, [x25,12]
	mov	w1, 65535
	ldr	x0, [x19,1048]
	ldr	x26, [x19,1112]
	cmp	w4, w1
	str	x0, [x20,8]
	str	x26, [x20,16]
	beq	.L1405
	ldrh	w0, [x19,814]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w22, w0, w4
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x19,814]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L1406
.L1405:
	mov	w1, w22
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w23, [x19,814]
.L1408:
	sub	w23, w23, #1
	uxth	w23, w23
.L1407:
	ldrh	w0, [x19,814]
	madd	w0, w22, w0, w23
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1408
	ldr	x0, [x19,1112]
	mov	w1, 0
	mov	w2, 16
	strh	w23, [x25,12]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldrh	w2, [x19,1288]
	ldrh	w0, [x25,12]
	strh	w0, [x26,2]
	lsl	w2, w2, 2
	ldrh	w0, [x19,814]
	ldrh	w4, [x25,12]
	ldr	x1, [x24,32]
	madd	w27, w22, w0, w4
	lsl	w0, w27, 10
	str	w0, [x20,4]
	ldr	x0, [x20,8]
	bl	ftl_memcpy
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	bl	FlashEraseBlocks
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L1406
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L1407
.L1406:
	mov	w0, w27
	add	w22, w22, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L1404
.L1422:
	mov	w19, 0
.L1411:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,832]
	cmp	w0, w19
	bls	.L1423
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	uxth	w19, w19
	b	.L1411
.L1423:
	ldrh	w19, [x20,1308]
	mov	w23, 65535
	sub	w19, w19, #1
	uxth	w19, w19
.L1413:
	add	x22, x20, 1296
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1417
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1414
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1415
	mov	w0, w19
	bl	FtlBbmMapBadBlock
	b	.L1414
.L1415:
	ldrh	w0, [x20,1296]
	cmp	w0, w23
	bne	.L1416
	strh	w19, [x20,1296]
	b	.L1414
.L1416:
	strh	w19, [x22,4]
	b	.L1417
.L1414:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1413
.L1417:
	add	x19, x21, :lo12:.LANCHOR2
	mov	w2, 2
	ldr	x1, [x19,1016]
	ldrh	w0, [x19,1296]
	str	wzr, [x19,1304]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,1298]
	ldr	x0, [x19,1016]
	ldrh	w1, [x19,1300]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,1296]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,1300]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,1298]
	ldr	w0, [x19,1304]
	ldrh	w1, [x19,1300]
	add	w0, w0, 1
	str	w0, [x19,1304]
	ldrh	w0, [x19,1296]
	strh	w0, [x19,1300]
	strh	w1, [x19,1296]
	bl	FtlBbmTblFlush
.L1403:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	adrp	x3, .LANCHOR2
	uxth	w0, w0
	add	x3, x3, :lo12:.LANCHOR2
	uxth	w1, w1
	ldrh	w4, [x3,772]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,814]
	madd	w0, w2, w1, w0
	ret
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w1, [x2,772]
	ldrh	w3, [x2,814]
	udiv	w2, w0, w1
	udiv	w3, w0, w3
	msub	w0, w2, w1, w0
	madd	w0, w1, w3, w0
	ret
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w3, [x2,814]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,772]
	uxth	w0, w0
	udiv	w0, w0, w1
	ret
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	cmp	w0, w1
	mov	w2, -2147483648
	bls	.L1428
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L1429
.L1428:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L1429:
	ret
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	stp	x29, x30, [sp, -208]!
	cmp	w1, 1
	adrp	x3, .LANCHOR2
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x25, [sp,64]
	mov	w23, w1
	add	x3, x3, :lo12:.LANCHOR2
	bne	.L1431
	ldrh	w19, [x3,820]
	b	.L1432
.L1431:
	ldrh	w19, [x3,818]
.L1432:
	sub	w19, w19, #1
	lsl	w21, w0, 10
	mov	w1, 1
	mov	w2, w23
	sxth	w19, w19
	str	xzr, [x29,96]
	orr	w0, w19, w21
	str	w0, [x29,92]
	add	x0, x29, 88
	add	x24, x29, 144
	str	x24, [x29,104]
	bl	FlashReadPages
	ldr	w0, [x29,144]
	cmn	w0, #1
	bne	.L1433
	mov	w22, 0
	mov	w25, 2
.L1434:
	cmp	w22, w19
	bgt	.L1433
	add	w3, w22, w19
	mov	w1, 1
	mov	w2, w23
	sdiv	w20, w3, w25
	sxth	w0, w20
	orr	w0, w0, w21
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x24]
	cmn	w0, #1
	bne	.L1435
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1435
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1435
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1434
.L1435:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1434
.L1433:
	mov	w0, w19
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 208
	ret
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	stp	x23, x24, [sp,48]
	add	x23, x19, :lo12:.LANCHOR2
	mov	w24, 61649
	stp	x21, x22, [sp,32]
	add	x21, x23, 1408
	ldr	x0, [x23,1048]
	ldr	x22, [x23,1112]
	str	x0, [x21,8]
	str	x22, [x21,16]
	bl	FtlBbtMemInit
	ldrh	w20, [x23,814]
	sub	w20, w20, #1
	uxth	w20, w20
.L1442:
	ldrh	w0, [x23,814]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1445
	lsl	w0, w20, 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1443
	ldr	w0, [x21,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
.L1443:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1444
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1444
	ldr	w1, [x22,4]
	str	w1, [x23,1304]
	strh	w20, [x23,1296]
	ldrh	w1, [x22,8]
	strh	w1, [x23,1300]
	b	.L1445
.L1444:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1442
.L1445:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,1296]
	cmp	w1, w2
	beq	.L1447
	ldrh	w1, [x21,1300]
	cmp	w1, w2
	beq	.L1449
	add	x0, x21, 1408
	lsl	w1, w1, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x21,1408]
	cmn	w0, #1
	beq	.L1449
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1449
	ldr	w0, [x22,4]
	ldr	w1, [x21,1304]
	cmp	w0, w1
	bls	.L1449
	str	w0, [x21,1304]
	ldrh	w1, [x21,1300]
	ldrh	w0, [x22,8]
	strh	w1, [x21,1296]
	strh	w0, [x21,1300]
.L1449:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 1408
	mov	w24, 61649
	ldrh	w0, [x20,1296]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20,1298]
.L1451:
	tbnz	w21, #31, .L1456
	ldrh	w0, [x20,1296]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w21, w0, lsl 10
	str	w0, [x23,4]
	ldr	x0, [x20,1048]
	str	x0, [x23,8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1452
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1452
.L1456:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,1302]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1453
	b	.L1454
.L1452:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1451
.L1453:
	ldr	w2, [x1,744]
	cmp	w0, w2
	beq	.L1454
	ldrh	w1, [x1,758]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1454
	cmp	w2, w1
	bcs	.L1454
	bl	FtlSysBlkNumInit
.L1454:
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, 0
	add	x21, x19, 1296
.L1457:
	ldrh	w0, [x19,770]
	mov	w1, w20
	add	x20, x20, 1
	cmp	w1, w0
	bcs	.L1475
	ldrh	w2, [x19,1288]
	add	x0, x21, x20, lsl 3
	ldr	x3, [x19,1416]
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1457
.L1475:
	mov	w0, 0
.L1447:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	adrp	x1, .LANCHOR2
	mov	w2, 2048
	stp	x29, x30, [sp, -16]!
	add	x1, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	strh	w0, [x1,1464]
	add	x0, x1, 1472
	strh	wzr, [x1,1466]
	strh	wzr, [x1,1468]
	strh	wzr, [x1,1470]
	mov	w1, 0
	bl	ftl_memset
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	adrp	x0, .LANCHOR2+1470
	ldrh	w0, [x0,#:lo12:.LANCHOR2+1470]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR2+1470
	ldrh	w0, [x0,#:lo12:.LANCHOR2+1470]
	cmp	w0, 1024
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	stp	x29, x30, [sp, -48]!
	uxth	w1, w1
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	uxth	w21, w0
	mov	w0, 65533
	sub	w2, w21, #1
	cmp	w0, w2, uxth
	bcc	.L1479
	adrp	x2, .LANCHOR2
	add	x20, x2, :lo12:.LANCHOR2
	mov	x19, x2
	ldrh	w0, [x20,1470]
	cmp	w0, 1024
	beq	.L1479
	cbz	w1, .L1481
	ldr	w0, [x20,1400]
	cbnz	w0, .L1481
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,1016]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,1016]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,1168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,912]
	add	w0, w0, 1
	str	w0, [x20,912]
.L1481:
	add	x1, x19, :lo12:.LANCHOR2
	add	x1, x1, 1464
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L1479:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	uxth	w25, w0
	uxtb	w24, w1
	ldrb	w0, [x3,648]
	ldr	w2, [x3,1400]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L1489
	adrp	x28, .LANCHOR0
	str	w25, [x3,944]
	add	x0, x28, :lo12:.LANCHOR0
	mov	w27, w2
	mov	w20, w2
	mov	w19, w2
	mov	x26, x3
	mov	w23, 56
	ldrb	w0, [x0,136]
	str	w0, [x29,124]
.L1490:
	ldrh	w0, [x26,748]
	cmp	w0, w27
	bls	.L1534
	umull	x0, w27, w23
	ldr	x1, [x26,1016]
	str	wzr, [x1,x0]
	add	x0, x26, 776
	mov	w1, w25
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbz	w24, .L1491
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1492
.L1491:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1493
	umull	x0, w20, w23
	ldr	x1, [x26,1016]
	lsl	w22, w22, 10
	mov	w2, 4
	add	x1, x1, x0
	str	w22, [x1,4]
	ldr	x1, [x26,1016]
	ldr	x4, [x26,1120]
	add	x1, x1, x0
	ldrh	w0, [x26,830]
	mul	w0, w20, w0
	add	w20, w20, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w20, w20
	add	x0, x4, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1492
.L1493:
	add	w19, w19, 1
	uxth	w19, w19
.L1492:
	add	w2, w27, 1
	uxth	w27, w2
	b	.L1490
.L1534:
	cbz	w20, .L1513
	ldr	w0, [x29,124]
	mov	w2, w20
	strb	wzr, [x26,648]
	mov	x23, 0
	cmp	w0, wzr
	ldr	x0, [x26,1016]
	cset	w22, ne
	mov	w1, w22
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x26,648]
	mov	x26, 56
.L1496:
	cmp	w20, w23, uxth
	bls	.L1535
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1016]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1497
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1497:
	add	x23, x23, 1
	b	.L1496
.L1535:
	cbnz	w24, .L1499
	mov	w0, 1
	uxth	w22, w22
	mov	w27, 6
	str	w0, [x29,124]
	b	.L1500
.L1499:
	add	x0, x21, :lo12:.LANCHOR2
	add	x28, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,820]
	str	w0, [x29,124]
	ldrb	w0, [x28,136]
	cbnz	w0, .L1516
	ldr	w0, [x29,124]
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1500
.L1516:
	mov	w22, 1
	mov	w27, w22
.L1500:
	mov	w23, 0
	add	x28, x21, :lo12:.LANCHOR2
.L1509:
	mov	w26, 0
	mov	w20, w26
.L1501:
	ldrh	w0, [x28,748]
	cmp	w0, w26
	bls	.L1536
	mov	w0, 56
	ldr	x1, [x28,1016]
	umull	x0, w26, w0
	str	wzr, [x1,x0]
	add	x0, x28, 776
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w24, .L1502
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1503
.L1502:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1503
	mov	w0, 56
	ldr	x1, [x28,1016]
	ldr	w2, [x29,112]
	umull	x0, w20, w0
	add	x1, x1, x0
	add	w2, w23, w2, lsl 10
	str	w2, [x1,4]
	mov	w2, 4
	ldr	x1, [x28,1016]
	add	x1, x1, x0
	ldr	x0, [x28,1088]
	str	x0, [x1,8]
	ldrh	w0, [x28,830]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w2
	uxth	w20, w20
	ldr	x2, [x28,1096]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1503:
	add	w3, w26, 1
	uxth	w26, w3
	b	.L1501
.L1536:
	cbz	w20, .L1513
	ldr	x0, [x28,1016]
	mov	w1, w20
	mov	w2, w22
	mov	w3, 1
	strb	wzr, [x28,648]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x28,648]
.L1506:
	cmp	w20, w1, uxth
	bls	.L1537
	mov	x0, 56
	ldr	x2, [x28,1016]
	mul	x0, x1, x0
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1507
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1507:
	add	x1, x1, 1
	b	.L1506
.L1537:
	add	w23, w23, w27
	ldr	w0, [x29,124]
	uxth	w23, w23
	cmp	w23, w0
	bcc	.L1509
	mov	x23, 0
	mov	x26, 56
.L1510:
	cmp	w20, w23, uxth
	bls	.L1538
	cbz	w24, .L1511
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1016]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1511
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1511:
	add	x23, x23, 1
	b	.L1510
.L1538:
	cmp	w25, 63
	bls	.L1517
	cbz	w24, .L1513
.L1517:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, w20
	ldr	x0, [x21,1016]
	bl	FlashEraseBlocks
.L1513:
	mov	w0, w19
.L1489:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	adrp	x1, .LANCHOR2
	add	x0, x1, :lo12:.LANCHOR2
	add	x0, x0, 1464
	ldrh	w6, [x0,2084]
	ldrh	w2, [x0,6]
	cbz	w2, .L1539
	mov	w5, 0
	ldrh	w3, [x0,2]
	ldrh	w2, [x0,4]
	mov	w4, w5
	and	w6, w6, 31
.L1541:
	cmp	w4, w6
	bge	.L1549
	add	x5, x0, x3, sxtw 1
	ldrh	w7, [x5,8]
	add	x5, x0, x2, sxtw 1
	add	w4, w4, 1
	strh	w7, [x5,8]
	mov	w5, 1
	add	w3, w3, 1
	add	w2, w2, w5
	uxth	w4, w4
	and	w3, w3, 1023
	and	w2, w2, 1023
	b	.L1541
.L1549:
	cbz	w5, .L1539
	add	x0, x1, :lo12:.LANCHOR2
	strh	w3, [x0,1466]
	strh	w2, [x0,1468]
.L1539:
	ret
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	stp	x23, x24, [sp,48]
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x23, .LC35
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x21, x19, 1464
	adrp	x24, .LANCHOR0
	add	x23, x23, :lo12:.LC35
.L1551:
	ldrh	w1, [x21,6]
	adrp	x22, .LANCHOR2
	cbz	w1, .L1552
	ldrh	w2, [x21,2]
	sub	w1, w1, #1
	add	x0, x21, x2, sxtw 1
	strh	w1, [x21,6]
	add	w2, w2, 1
	ldr	w1, [x19,1400]
	and	w2, w2, 1023
	strh	w2, [x21,2]
	ldrh	w20, [x0,8]
	cbnz	w1, .L1553
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x19,1016]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L1554
	ldr	x0, [x19,1016]
	mov	w2, 1
	bl	FlashEraseBlocks
.L1554:
	add	x25, x22, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,1016]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x25,1168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x25,912]
	add	w0, w0, 1
	str	w0, [x25,912]
	b	.L1553
.L1552:
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	bl	printk
.L1555:
	b	.L1555
.L1553:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L1556
	add	x22, x22, :lo12:.LANCHOR2
	mov	x0, x23
	mov	w1, w20
	ldrh	w2, [x22,1470]
	bl	printk
	b	.L1551
.L1556:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	ldr	x2, [x0]
	adrp	x0, .LANCHOR2+1280
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+1280]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L1562:
	cmp	w1, w0
	beq	.L1563
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L1564
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L1562
.L1563:
	mov	w0, 1
	b	.L1561
.L1564:
	mov	w0, 0
.L1561:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	add	x3, x2, :lo12:.LANCHOR2
	mov	x4, x2
	ldrh	w1, [x3,756]
	cmp	w1, w0
	bls	.L1567
	mov	w7, 6
	ldr	x11, [x3,1280]
	mov	w1, -1
	umull	x10, w0, w7
	add	x5, x11, x10
	strh	w1, [x5,2]
	strh	w1, [x11,x10]
	ldr	x1, [x3,3568]
	cbnz	x1, .L1568
	str	x5, [x3,3568]
	b	.L1567
.L1568:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x3,1184]
	ldrh	w2, [x5,4]
	mov	x9, -6148914691236517206
	ldr	x12, [x3,1280]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldr	x14, [x3,1168]
	ldrh	w17, [x3,756]
	mov	w3, 0
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x9, x2, x2
	add	x9, x14, x8
	mov	w8, w7
	uxth	w2, w2
.L1576:
	add	w3, w3, 1
	cmp	w0, w2
	uxth	w3, w3
	beq	.L1567
	cmp	w3, w17
	bhi	.L1567
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L1572
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L1574
	b	.L1573
.L1572:
	bhi	.L1573
.L1574:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L1575
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x5, [x2,3576]
	b	.L1567
.L1575:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L1576
.L1573:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	ldr	x3, [x2,3568]
	cmp	x1, x3
	bne	.L1577
	strh	w0, [x1,2]
	str	x5, [x2,3568]
	b	.L1567
.L1577:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1280]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1567:
	mov	w0, 0
	ret
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	insert_data_list
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,3584]
	add	w1, w1, 1
	strh	w1, [x0,3584]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	uxth	w0, w0
	mov	w7, 65535
	cmp	w0, w7
	beq	.L1582
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,1280]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,3592]
	cbnz	x1, .L1583
	str	x4, [x5,3592]
	b	.L1582
.L1583:
	ldr	x11, [x5,1168]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,1280]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L1586:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L1584
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L1585
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L1582
.L1585:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L1586
.L1584:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,3592]
	cmp	x1, x3
	bne	.L1587
	strh	w0, [x1,2]
	str	x4, [x2,3592]
	b	.L1582
.L1587:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1280]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1582:
	mov	w0, 0
	ret
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	insert_free_list
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,3600]
	add	w1, w1, 1
	strh	w1, [x0,3600]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	adrp	x6, .LANCHOR2
	uxth	w1, w1
	add	x6, x6, :lo12:.LANCHOR2
	mov	w5, 6
	ldr	x3, [x0]
	mov	w7, 65535
	umull	x1, w1, w5
	ldr	x2, [x6,1280]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L1590
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L1591
	str	xzr, [x0]
	b	.L1592
.L1591:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L1592
.L1590:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L1593
	cmp	w3, w0
	beq	.L1592
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L1592
.L1593:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,1280]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L1592:
	mov	w0, -1
	strh	w0, [x2,x1]
	strh	w0, [x4,2]
	mov	w0, 0
	ret
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	stp	x29, x30, [sp, -32]!
	uxth	w1, w1
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, 65535
	ldr	x2, [x0]
	cbz	x2, .L1595
	adrp	x3, .LANCHOR2+1280
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+1280]
.L1596:
	cbnz	w1, .L1597
.L1599:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L1595
.L1597:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L1599
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L1596
.L1595:
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	adrp	x2, .LANCHOR2
	uxth	w3, w0
	add	x2, x2, :lo12:.LANCHOR2
	mov	w0, 65535
	ldr	x1, [x2,3568]
	cbz	x1, .L1607
	ldr	x4, [x2,1280]
	mov	w5, 6
.L1603:
	cbz	w3, .L1604
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L1607
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L1603
.L1604:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L1607:
	ret
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,3608]
	cmp	w1, w20
	beq	.L1609
	ldrh	w1, [x0,3656]
	cmp	w1, w20
	beq	.L1609
	ldrh	w1, [x0,3704]
	cmp	w1, w20
	beq	.L1609
	mov	w3, 6
	ldr	x4, [x0,1280]
	ldr	x2, [x0,3568]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L1609
	ldr	x5, [x0,1184]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x5,x0]
	ldrh	w0, [x1,4]
	ldrh	w1, [x1,2]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 65535
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L1611
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L1609
.L1611:
	mov	w0, 6
	mov	x3, -6148914691236517206
	umull	x1, w1, w0
	asr	x0, x1, 1
	add	x1, x4, x1
	madd	x0, x3, x0, x0
	ldrh	w1, [x1,4]
	ldrh	w0, [x5,x0,lsl 1]
	cmp	w1, wzr
	mul	w0, w0, w1
	csinv	w0, w0, wzr, ne
	cmp	w2, w0
	bcs	.L1609
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	add	x0, x19, 3568
	bl	List_remove_node
	ldrh	w0, [x19,3584]
	sub	w0, w0, #1
	strh	w0, [x19,3584]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1609:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	stp	x29, x30, [sp, -80]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	mov	x19, x0
	ldrh	w2, [x0,10]
	ldr	x21, [x0,32]
	ldr	x22, [x0,16]
	lsl	w2, w2, 1
	ldr	x20, [x0,40]
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L1616:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1638
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1617:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L1639
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L1618
	cbz	w4, .L1618
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L1618:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1617
.L1639:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1616
.L1638:
	adrp	x0, .LANCHOR2+820
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR2+820]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L1621:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L1640
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L1622
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L1623
	b	.L1624
.L1622:
	cbnz	w1, .L1624
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L1624
.L1626:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L1624:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1621
.L1623:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L1626
	b	.L1624
.L1640:
	mov	w0, w23
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	w20, 0
	ldrh	w2, [x0,10]
	ldr	x1, [x0,16]
.L1642:
	cmp	w20, w2
	beq	.L1646
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L1643
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1644
	adrp	x2, .LANCHOR2+1470
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	ldrh	w2, [x2,#:lo12:.LANCHOR2+1470]
	bl	printk
.L1645:
	b	.L1645
.L1644:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L1646
.L1643:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1642
.L1646:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR2
	mov	x26, x2
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	adrp	x23, .LC37
	add	x28, x25, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	mov	w24, w1
	mov	x19, x0
	mov	w22, 0
	add	x0, x23, :lo12:.LC37
	mov	w27, 65535
	add	x21, x28, 1408
	str	x0, [x29,104]
.L1649:
	add	x0, x25, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR2
	ldr	w1, [x0,896]
	add	w1, w1, 1
	str	w1, [x0,896]
	ldrh	w0, [x0,820]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1650
	ldrh	w0, [x19]
	cmp	w0, w27
	bne	.L1651
.L1650:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1651:
	ldr	w1, [x28,1400]
	cbnz	w1, .L1652
	ldrh	w2, [x19]
	ldr	x0, [x19,16]
	ldrh	w23, [x0,x2,lsl 1]
	mov	w2, 16
	ldrh	w0, [x19,2]
	str	x26, [x21,8]
	orr	w0, w0, w23, lsl 10
	str	w0, [x21,4]
	ldr	x0, [x28,1112]
	str	x0, [x21,16]
	bl	ftl_memset
	ldr	x0, [x21,16]
	ldr	w1, [x19,48]
	str	w1, [x0,4]
	strh	w24, [x0,8]
	ldrh	w1, [x19,4]
	strh	w1, [x0]
	mov	w1, 1
	strh	w23, [x0,2]
	mov	w3, w1
	mov	x0, x21
	mov	w2, w1
	bl	FlashProgPages
	ldrh	w3, [x19,2]
	add	w3, w3, 1
	uxth	w3, w3
	strh	w3, [x19,2]
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1653
	ldr	x0, [x29,104]
	add	w22, w22, 1
	ldr	w1, [x21,4]
	uxth	w22, w22
	bl	printk
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L1654
	ldrh	w0, [x28,820]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L1654:
	cmp	w22, 3
	bls	.L1649
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	mov	w2, w22
	ldr	w1, [x20,1412]
	bl	printk
	mov	w0, 1
	str	w0, [x20,1400]
	b	.L1652
.L1653:
	cbz	w0, .L1656
	strh	w23, [x19,60]
.L1656:
	cmp	w3, 1
	beq	.L1657
	cmp	w0, 256
	beq	.L1657
	ldr	w0, [x19,56]
	cbz	w0, .L1658
.L1657:
	str	wzr, [x19,56]
	b	.L1649
.L1658:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x19,40]
	ldr	w1, [x20,1412]
	str	w1, [x0,w24,uxtw 2]
.L1652:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x19, x0
	adrp	x20, .LANCHOR2
	ldr	x21, [x0,16]
	ldr	x25, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w1, #4
	cmp	w2, w1
	blt	.L1663
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x21,x1]
	cbz	w24, .L1663
	ldr	w0, [x19,52]
	cbnz	w0, .L1663
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x21,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,820]
	cmp	w1, w0
	bcc	.L1664
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1664:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w21, 0
	add	x22, x26, 1408
.L1665:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L1674
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L1666
	ldr	x1, [x26,1056]
	ldr	x23, [x26,1112]
	str	x1, [x22,8]
	mov	w1, 1
	str	x23, [x22,16]
	mov	w2, w1
	ldr	w0, [x25,x0]
	str	w0, [x22,4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	cmn	w0, #1
	bne	.L1667
.L1669:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x27]
	adrp	x0, .LC39
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC39
	ldr	w1, [x20,1412]
	bl	printk
	mov	w0, 1
	str	w0, [x20,1400]
	b	.L1668
.L1667:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L1669
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1669
	ldr	x2, [x22,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L1666:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1665
.L1674:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1663:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,820]
	cmp	w1, w0
	bcc	.L1668
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1668:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	str	x23, [sp,48]
	add	x1, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	ldr	w20, [x1,1400]
	ldr	x22, [x0,16]
	ldr	x23, [x0,40]
	cbnz	w20, .L1676
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1677
	ldrh	w0, [x19,8]
	add	w0, w0, 1
	strh	w0, [x19,8]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	strh	w20, [x19,2]
	ldr	w0, [x19,48]
	strh	w20, [x19]
	add	w0, w0, 1
	str	w0, [x19,48]
	b	.L1676
.L1677:
	ubfiz	x0, x0, 1, 16
	ldr	w2, [x19,48]
	ldrh	w22, [x22,x0]
	ldrh	w0, [x19,2]
	orr	w0, w0, w22, lsl 10
	str	w0, [x1,1412]
	ldr	x0, [x1,1048]
	str	x0, [x1,1416]
	ldr	x0, [x1,1112]
	str	x0, [x1,1424]
	str	w2, [x0,4]
	mov	w2, -1291
	strh	w2, [x0,8]
	ldrh	w2, [x19,4]
	strh	w2, [x0]
	strh	w22, [x0,2]
	ldrh	w2, [x1,820]
	ldr	x0, [x1,1048]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L1678:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1681
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x23,x3]
	cmp	w22, w1, lsr 10
	bne	.L1679
	add	x2, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,1048]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,1048]
	ldr	w3, [x23,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L1679:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1678
.L1681:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 1408
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L1676:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ubfiz	x19, x0, 4, 16
	add	x20, x20, :lo12:.LANCHOR2
	add	x0, x20, 3752
	ldr	x1, [x20,1264]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,1264]
	add	x19, x0, x19
	ldr	w0, [x19,4]
	and	w0, w0, 2147483647
	str	w0, [x19,4]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	adrp	x0, .LANCHOR2
	mov	x4, 0
	add	x1, x0, :lo12:.LANCHOR2
	mov	x3, x0
	mov	w5, 65535
	ldrh	w2, [x1,858]
	ldr	x1, [x1,1264]
.L1684:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L1700
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L1684
	b	.L1685
.L1700:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L1687:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L1701
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L1696
	cmp	w5, w7
	bcc	.L1688
.L1696:
	mov	w5, w7
	mov	w6, w0
.L1688:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L1687
.L1701:
	cmp	w0, w2
	bcc	.L1685
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,3816]
	mov	w0, w2
.L1691:
	cmp	w3, w2
	beq	.L1685
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L1692
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L1692:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L1691
.L1685:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	x26, x1
	mov	w27, w2
	ldrh	w1, [x3,826]
	mov	x2, 1
	ldr	x19, [x3,1264]
	add	w1, w1, 7
	lsr	w25, w0, w1
	lsl	x1, x2, x1
	sub	w1, w1, #1
	and	w23, w1, w0
	ldrh	w1, [x3,858]
	uxth	w24, w25
	uxth	w0, w23
	str	w0, [x29,108]
	mov	x0, 0
.L1703:
	uxth	x20, w0
	cmp	w20, w1
	bcs	.L1718
	add	x0, x0, 1
	add	x2, x19, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w24
	bne	.L1703
.L1704:
	ldr	w23, [x29,108]
	cbnz	w27, .L1705
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1264]
	add	x1, x1, x20, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x23,lsl 2]
	str	w0, [x26]
	b	.L1706
.L1705:
	add	x1, x21, :lo12:.LANCHOR2
	lsl	x0, x20, 4
	ldr	w3, [x26]
	ldr	x2, [x1,1264]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x23,lsl 2]
	strh	w24, [x1,3816]
	ldr	x2, [x1,1264]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L1706:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21,1264]
	add	x20, x0, x20, lsl 4
	ldr	w0, [x20,4]
	cmn	w0, #1
	beq	.L1715
	add	w0, w0, 1
	str	w0, [x20,4]
	b	.L1715
.L1718:
	bl	select_l2p_ram_region
	uxth	x20, w0
	ubfiz	x22, x20, 4, 16
	mov	w2, 65535
	add	x1, x19, x22
	ldrh	w3, [x19,x22]
	cmp	w3, w2
	beq	.L1709
	ldr	w1, [x1,4]
	tbz	w1, #31, .L1709
	bl	flush_l2p_region
.L1709:
	add	x19, x21, :lo12:.LANCHOR2
	ubfiz	x0, x24, 2, 16
	ldr	x1, [x19,1240]
	ldr	w23, [x1,x0]
	cbnz	w23, .L1710
	ldr	x0, [x19,1264]
	mov	w1, 255
	ldrh	w2, [x19,828]
	add	x0, x0, x22
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x19,1264]
	strh	w24, [x0,x22]
	ldr	x0, [x19,1264]
	add	x22, x0, x22
	str	w23, [x22,4]
	b	.L1704
.L1710:
	ldr	x0, [x19,1264]
	mov	w1, 1
	add	x28, x19, 1408
	mov	w2, w1
	add	x0, x0, x22
	str	w23, [x19,1412]
	ldr	x0, [x0,8]
	str	x0, [x19,1416]
	ldr	x0, [x19,1112]
	str	x0, [x19,1424]
	mov	x0, x28
	bl	FlashReadPages
	ldr	x0, [x19,1424]
	ldrh	w0, [x0,8]
	cmp	w0, w24
	beq	.L1711
	adrp	x0, .LC40
	uxth	w1, w25
	mov	w2, w23
	add	x0, x0, :lo12:.LC40
	bl	printk
	mov	w2, 4
	ldr	x1, [x19,1424]
	adrp	x0, .LC17
	mov	w3, w2
	add	x0, x0, :lo12:.LC17
	bl	rknand_print_hex
	adrp	x0, .LC41
	ldr	x1, [x19,1240]
	ldrh	w3, [x19,856]
	add	x0, x0, :lo12:.LC41
	mov	w2, 4
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x19,1400]
	b	.L1712
.L1711:
	ldr	w0, [x19,1408]
	cmp	w0, 256
	bne	.L1712
	and	w25, w25, 65535
	adrp	x0, .LC42
	mov	w1, w25
	mov	w2, w23
	add	x0, x0, :lo12:.LC42
	bl	printk
	ldr	x2, [x19,1264]
	add	x0, x19, 3752
	mov	w1, w25
	add	x2, x2, x22
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1712:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,1264]
	add	x0, x0, x22
	str	wzr, [x0,4]
	ldr	x0, [x1,1264]
	strh	w24, [x0,x22]
	b	.L1704
.L1715:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w22, w1
	mov	w25, w0
	add	w1, w0, w1
	ldrh	w0, [x19,812]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	cmp	w1, w0
	mov	x24, x2
	ldrh	w23, [x19,826]
	mov	w21, -1
	bhi	.L1720
	lsr	w23, w25, w23
	mov	w21, 0
	add	x27, x19, 3824
.L1721:
	cbz	w22, .L1720
	ldrh	w1, [x19,824]
	ldr	x0, [x19,1232]
	udiv	w26, w25, w1
	ldr	w2, [x0,w23,uxtw 2]
	uxth	w0, w22
	msub	w26, w26, w1, w25
	sub	w20, w1, w26
	uxth	w20, w20
	cmp	w20, w22
	csel	w20, w0, w20, hi
	cmp	w20, w1
	beq	.L1723
	cbz	w2, .L1723
	ldr	x0, [x19,1064]
	mov	w1, 1
	str	w2, [x29,108]
	mov	w2, w1
	str	x0, [x29,112]
	add	x0, x29, 104
	str	xzr, [x29,120]
	bl	FlashReadPages
	b	.L1724
.L1723:
	ldr	x0, [x19,1064]
	mov	w1, 0
	ldrh	w2, [x19,828]
	bl	ftl_memset
.L1724:
	ldr	x0, [x19,1064]
	lsl	w28, w20, 9
	ubfiz	x26, x26, 9, 16
	mov	x1, x24
	mov	w2, w28
	add	x0, x0, x26
	sub	w22, w22, w20
	add	w25, w25, w20
	bl	ftl_memcpy
	add	x24, x24, x28, sxtw
	ldr	x2, [x19,1064]
	mov	w1, w23
	mov	x0, x27
	add	w23, w23, 1
	bl	FtlMapWritePage
	cmn	w0, #1
	csinv	w21, w21, wzr, ne
	b	.L1721
.L1720:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -176]!
	adrp	x3, .LANCHOR2
	add	x3, x3, :lo12:.LANCHOR2
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	w22, w1
	mov	x25, x2
	add	w2, w0, w1
	ldrh	w1, [x3,812]
	mov	w23, w0
	ldrh	w20, [x3,826]
	cmp	w2, w1
	mov	w0, -1
	bhi	.L1735
	adrp	x26, .LC43
	add	x0, x3, 3824
	lsr	w20, w23, w20
	mov	w21, 0
	mov	x28, x3
	add	x26, x26, :lo12:.LC43
	str	x0, [x29,104]
.L1736:
	cbz	w22, .L1744
	ldrh	w19, [x28,824]
	ldr	x0, [x28,1232]
	udiv	w24, w23, w19
	ldr	w4, [x0,w20,uxtw 2]
	uxth	w0, w22
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w27, w19, 9
	cbz	w4, .L1738
	ldr	x0, [x28,1064]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w4, [x29,124]
	str	x4, [x29,96]
	str	xzr, [x29,136]
	bl	FlashReadPages
	ldr	w0, [x29,120]
	ldr	x4, [x29,96]
	cmn	w0, #1
	ldr	w0, [x28,1408]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L1740
	mov	w1, w20
	mov	w2, w4
	mov	x0, x26
	bl	printk
	ldr	x0, [x29,104]
	mov	w1, w20
	ldr	x2, [x28,1064]
	bl	FtlMapWritePage
.L1740:
	ldr	x1, [x28,1064]
	ubfiz	x24, x24, 9, 16
	mov	x0, x25
	mov	w2, w27
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L1741
.L1738:
	mov	x0, x25
	mov	w1, w4
	mov	w2, w27
	bl	ftl_memset
.L1741:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1736
.L1744:
	mov	w0, w21
.L1735:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 256
	add	x29, sp, 0
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	2
	.global	ftl_sys_read
	.type	ftl_sys_read, %function
ftl_sys_read:
	stp	x29, x30, [sp, -16]!
	add	w0, w0, 512
	add	x29, sp, 0
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_sys_read, .-ftl_sys_read
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,3888]
	cbnz	w0, .L1748
	cmp	w2, 4
	bhi	.L1748
	add	w2, w2, 1
	strh	w2, [x3,3888]
	b	.L1747
.L1748:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	strh	wzr, [x0,3888]
	str	wzr, [x0,3892]
	ldrh	w5, [x0,756]
	ldr	x6, [x0,1184]
	mov	x0, 0
.L1749:
	cmp	w5, w0, uxth
	bls	.L1747
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L1750
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,3892]
	add	w2, w4, w2
	str	w2, [x3,3892]
.L1750:
	add	x0, x0, 1
	b	.L1749
.L1747:
	ret
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	ldr	w1, [x0,56]
	ldrh	w19, [x0,6]
	ldr	x4, [x0,40]
	cbz	w1, .L1753
	adrp	x1, .LANCHOR2
	str	wzr, [x0,56]
	add	x2, x1, :lo12:.LANCHOR2
	mov	x21, x1
	ldr	w3, [x2,1400]
	cbnz	w3, .L1753
	mov	x20, x0
	sub	w19, w19, #1
	add	x0, x2, 1408
	ldr	x3, [x2,1112]
	uxth	w19, w19
	ldr	x1, [x2,1056]
	str	x3, [x0,16]
	ubfiz	x3, x19, 2, 16
	str	x1, [x0,8]
	ldr	w3, [x4,x3]
	str	w3, [x0,4]
	cbz	w3, .L1757
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1758
.L1757:
	ldrh	w2, [x2,828]
	mov	x0, x1
	mov	w1, 255
	bl	ftl_memset
.L1758:
	add	x2, x21, :lo12:.LANCHOR2
	mov	x0, x20
	mov	w1, w19
	ldr	x2, [x2,1416]
	bl	FtlMapWritePage
.L1753:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	ldr	w0, [x19,1400]
	cbnz	w0, .L1761
	ldr	x21, [x19,1112]
	add	x20, x19, 3520
	ldr	x1, [x19,1048]
	str	x1, [x19,1416]
	str	x21, [x19,1424]
	ldrh	w2, [x19,3896]
	strh	w2, [x21,2]
	mov	w2, -3932
	strh	w2, [x21]
	str	w0, [x21,12]
	ldr	w2, [x19,3904]
	str	w2, [x21,4]
	str	w0, [x21,8]
	mov	w0, 19539
	ldrh	w1, [x19,3608]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x20,14]
	ldrh	w1, [x19,3610]
	ldrb	w2, [x19,3614]
	str	w0, [x19,3520]
	mov	w0, 80
	orr	w1, w2, w1, lsl 6
	strh	w1, [x20,16]
	ldrh	w1, [x19,3656]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x20,18]
	ldrh	w1, [x19,3658]
	ldrb	w2, [x19,3662]
	str	w0, [x20,4]
	ldrh	w0, [x19,3902]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20,8]
	ldrh	w0, [x19,770]
	strb	w0, [x20,10]
	ldrb	w0, [x19,3616]
	strb	w0, [x20,11]
	strh	w1, [x20,20]
	ldrb	w0, [x19,3664]
	ldrh	w1, [x19,3704]
	strb	w0, [x20,12]
	strh	w1, [x20,22]
	ldrh	w1, [x19,3706]
	ldrb	w0, [x19,3712]
	strb	w0, [x20,13]
	ldr	w0, [x19,904]
	str	w0, [x20,32]
	ldr	w0, [x19,872]
	str	w0, [x20,40]
	ldr	w0, [x19,876]
	ldrb	w2, [x19,3710]
	str	w0, [x20,36]
	ldrh	w0, [x19,948]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20,44]
	ldrh	w0, [x19,950]
	strh	w0, [x20,46]
	ldrh	w2, [x19,828]
	ldr	x0, [x19,1416]
	strh	w1, [x20,24]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19,1416]
	mov	x1, x20
	mov	w2, 48
	bl	ftl_memcpy
	ldr	x0, [x19,1416]
	ldrh	w2, [x19,756]
	ldr	x1, [x19,1184]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,756]
	ldr	x1, [x19,1416]
	uxth	x0, w2
	lsr	w2, w2, 3
	add	x0, x0, 24
	add	w2, w2, 4
	lsr	x0, x0, 1
	add	x0, x1, x0, lsl 2
	adrp	x1, .LANCHOR0+16
	ldr	x1, [x1,#:lo12:.LANCHOR0+16]
	bl	ftl_memcpy
	ldrh	w0, [x19,860]
	cbz	w0, .L1762
	ldrh	w0, [x19,756]
	ldrh	w2, [x19,856]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19,1416]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,1240]
	bl	ftl_memcpy
.L1762:
	mov	w0, 0
	mov	w20, 0
	mov	w25, 65535
	add	x19, x22, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L1763:
	ldrh	w1, [x19,3896]
	ldr	x0, [x19,1048]
	ldrh	w2, [x19,3898]
	str	x0, [x19,1416]
	ldr	x0, [x19,1112]
	str	x0, [x19,1424]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,1412]
	ldrh	w0, [x19,820]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1764
	ldrh	w25, [x19,3900]
	strh	wzr, [x19,3898]
	strh	w1, [x19,3900]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,872]
	str	w1, [x19,3904]
	add	w2, w1, 1
	str	w2, [x19,872]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19,1412]
	strh	w0, [x19,3896]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L1764:
	mov	w1, 1
	add	x0, x19, 1408
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,3898]
	ldr	w2, [x19,1408]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,3898]
	bne	.L1765
	cmp	w1, 1
	bne	.L1766
	ldrh	w1, [x19,820]
	sub	w1, w1, #1
	strh	w1, [x19,3898]
.L1766:
	add	w20, w20, 1
	uxth	w20, w20
	cmp	w20, 3
	bls	.L1763
	add	x22, x22, :lo12:.LANCHOR2
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w20
	ldr	w1, [x22,1412]
	bl	printk
	mov	w0, 1
	str	w0, [x22,1400]
	b	.L1761
.L1765:
	cmp	w1, 1
	beq	.L1763
	cmp	w2, 256
	beq	.L1763
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1761
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1761:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	strh	wzr, [x20,3908]
	mov	x26, 56
	ldr	w2, [x20,848]
	mov	w22, 65535
	ldr	x0, [x20,1256]
	lsl	w2, w2, 2
	strh	wzr, [x20,1392]
	bl	ftl_memset
	ldr	w2, [x20,848]
	mov	w1, 0
	ldr	x0, [x20,1192]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,840]
	mov	w1, 0
	ldr	x0, [x20,1224]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,840]
	mov	w1, 0
	ldr	x0, [x20,1208]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x20, 3896
	mov	w1, 255
	mov	w2, 12
	bl	ftl_memset
	ldrh	w21, [x20,756]
.L1773:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,758]
	cmp	w0, w21
	bls	.L1814
	mov	x27, 0
	ldrh	w3, [x1,748]
	ldrh	w2, [x1,830]
	mov	w20, w27
	mov	x23, x1
.L1815:
	cmp	w3, w27, uxth
	bls	.L1848
	add	x0, x23, 776
	mov	w1, w21
	str	x2, [x29,112]
	str	x3, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,120]
	ldr	x2, [x29,112]
	cbnz	w0, .L1774
	mov	w0, 56
	ldr	x1, [x23,992]
	lsl	w28, w28, 10
	mov	w4, 4
	umull	x0, w20, w0
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x23,992]
	add	x0, x1, x0
	ldr	x1, [x23,1072]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w4
	uxth	w20, w20
	ldr	x4, [x23,1128]
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1774:
	add	x27, x27, 1
	b	.L1815
.L1848:
	cbz	w20, .L1777
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	adrp	x28, .LANCHOR0
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L1778:
	cmp	w20, w23, uxth
	bls	.L1777
	add	x0, x19, :lo12:.LANCHOR2
	mul	x5, x23, x26
	ldr	x0, [x0,992]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	ldr	w4, [x1,4]
	cmn	w0, #1
	ldr	x25, [x1,16]
	ubfx	x24, x4, 10, 16
	bne	.L1780
	mov	w6, 16
.L1779:
	add	x7, x19, :lo12:.LANCHOR2
	str	x6, [x29,104]
	str	x7, [x29,112]
	str	x5, [x29,120]
	ldr	x0, [x7,992]
	add	x0, x0, x5
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x7,992]
	mov	w2, w1
	add	x0, x0, x5
	bl	FlashReadPages
	ldrh	w0, [x25]
	ldr	x7, [x29,112]
	cmp	w0, w22
	ldr	x5, [x29,120]
	ldr	x6, [x29,104]
	ldr	x0, [x7,992]
	bne	.L1781
	str	w27, [x0,x5]
	b	.L1780
.L1781:
	ldr	w0, [x0,x5]
	cmn	w0, #1
	bne	.L1780
	sub	w6, w6, #1
	uxth	w6, w6
	cbnz	w6, .L1779
.L1780:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,992]
	ldr	w1, [x1,x5]
	cmn	w1, #1
	beq	.L1783
	ldr	w0, [x0,872]
	cmn	w0, #1
	beq	.L1784
	ldr	w1, [x25,4]
	cmp	w0, w1
	bhi	.L1785
.L1784:
	ldr	w0, [x25,4]
	cmn	w0, #1
	beq	.L1785
	add	x1, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,872]
.L1785:
	ldrh	w0, [x25]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1787
	bhi	.L1788
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1786
	add	x5, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,840]
	ldrh	w0, [x5,1392]
	sub	w2, w6, #1
	ldr	x5, [x5,1224]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1801
.L1788:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1790
	cmp	w0, w22
	bne	.L1786
	mov	w0, w24
	b	.L1847
.L1790:
	add	x5, x19, :lo12:.LANCHOR2
	ldr	w6, [x5,848]
	ldrh	w0, [x5,3908]
	ldr	x5, [x5,1256]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1792:
	cmp	w1, w2
	ble	.L1849
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bls	.L1793
	ldr	w2, [x5]
	cbnz	w2, .L1794
	cmp	w0, w6
	beq	.L1794
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,3908]
.L1794:
	mov	w0, 0
.L1795:
	cmp	w0, w1
	beq	.L1850
	add	x5, x19, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x9, [x5,1256]
	sxth	w0, w0
	add	x10, x9, x6
	ldr	w10, [x10,4]
	str	w10, [x9,x6]
	ldr	x5, [x5,1192]
	add	x6, x5, x2
	ldrh	w6, [x6,2]
	strh	w6, [x5,x2]
	b	.L1795
.L1850:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x0, [x2,1256]
	str	w5, [x0,x7]
	ldr	x0, [x2,1192]
	strh	w24, [x0,x8,lsl 1]
	tbz	w1, #31, .L1797
	b	.L1786
.L1793:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1792
.L1849:
	tbz	w1, #31, .L1830
	b	.L1786
.L1797:
	ldrh	w0, [x2,3908]
	ldr	w2, [x2,848]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1786
.L1830:
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w3, [x25,4]
	strh	w0, [x2,3908]
	ldr	x0, [x2,1256]
	str	w3, [x0,x1,lsl 2]
	ldr	x0, [x2,1192]
	b	.L1846
.L1807:
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bhi	.L1851
	sub	w1, w1, #1
	sxth	x1, w1
.L1801:
	cmp	w1, w2
	bgt	.L1807
	b	.L1806
.L1851:
	ldr	w2, [x5]
	cbnz	w2, .L1803
	cmp	w0, w6
	beq	.L1803
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,1392]
.L1803:
	mov	w0, 0
.L1804:
	cmp	w0, w1
	beq	.L1852
	add	x5, x19, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x9, [x5,1224]
	sxth	w0, w0
	add	x10, x9, x6
	ldr	w10, [x10,4]
	str	w10, [x9,x6]
	ldr	x5, [x5,1208]
	add	x6, x5, x2
	ldrh	w6, [x6,2]
	strh	w6, [x5,x2]
	b	.L1804
.L1852:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x2, [x0,1224]
	str	w5, [x2,x7]
	ldr	x0, [x0,1208]
	strh	w24, [x0,x8,lsl 1]
.L1806:
	tbnz	w1, #31, .L1786
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,840]
	ldrh	w5, [x0,1392]
	sub	w2, w2, #1
	sub	w2, w2, w5
	cmp	w1, w2, sxth
	bgt	.L1786
	ldr	x2, [x0,1224]
	add	w5, w5, 1
	ldr	w3, [x25,4]
	strh	w5, [x0,1392]
	str	w3, [x2,x1,lsl 2]
	ldr	x0, [x0,1208]
.L1846:
	strh	w24, [x0,x1,lsl 1]
	b	.L1786
.L1787:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 3896
	ldrh	w2, [x0,3896]
	cmp	w2, w22
	bne	.L1808
	strh	w24, [x0,3896]
	ldr	w0, [x25,4]
	str	w0, [x1,8]
	b	.L1786
.L1808:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1809
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1809:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x25,4]
	add	x0, x1, 3896
	ldr	w5, [x0,8]
	cmp	w5, w2
	bcs	.L1810
	ldrh	w2, [x1,3896]
	strh	w2, [x0,4]
	strh	w24, [x1,3896]
	ldr	w1, [x25,4]
	str	w1, [x0,8]
	b	.L1786
.L1810:
	strh	w24, [x0,4]
	b	.L1786
.L1783:
	add	x0, x28, :lo12:.LANCHOR0
	ldrb	w1, [x0,136]
	mov	w0, w24
	cbz	w1, .L1811
.L1847:
	mov	w1, 0
.L1811:
	bl	FtlFreeSysBlkQueueIn
.L1786:
	add	x23, x23, 1
	b	.L1778
.L1777:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1773
.L1814:
	ldr	x2, [x1,1192]
	ldrh	w0, [x2]
	cbz	w0, .L1816
.L1819:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x2, [x1,1208]
	ldrh	w0, [x2]
	cbz	w0, .L1817
	b	.L1840
.L1816:
	ldrh	w3, [x1,3908]
	cbz	w3, .L1819
	ldr	w1, [x1,848]
.L1820:
	cmp	w0, w1
	bcs	.L1819
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1821
	mov	w6, w0
.L1822:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,848]
	cmp	w0, w2
	bcs	.L1819
	sxtw	x5, w0
	sub	w2, w0, w6
	ldr	x4, [x1,1192]
	lsl	x3, x5, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w7, [x4,x3]
	sxth	w0, w0
	strh	w7, [x4,x2,lsl 1]
	ldr	x4, [x1,1256]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x2,lsl 2]
	ldr	x1, [x1,1192]
	strh	wzr, [x1,x3]
	b	.L1822
.L1821:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1820
.L1817:
	ldrh	w3, [x1,1392]
	cbz	w3, .L1840
	ldrh	w1, [x1,840]
.L1825:
	cmp	w0, w1
	mov	w6, w0
	bge	.L1840
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1826
.L1827:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,840]
	cmp	w0, w2
	bge	.L1840
	sxtw	x5, w0
	sub	w2, w0, w6
	ldr	x4, [x1,1208]
	lsl	x3, x5, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w7, [x4,x3]
	sxth	w0, w0
	strh	w7, [x4,x2,lsl 1]
	ldr	x4, [x1,1224]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x2,lsl 2]
	ldr	x1, [x1,1208]
	strh	wzr, [x1,x3]
	b	.L1827
.L1826:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1825
.L1840:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	stp	x29, x30, [sp, -32]!
	mov	w0, 64
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,1144]
	ldr	x2, [x19,1160]
	bl	FtlVendorPartRead
	ldr	x0, [x19,1160]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1854
	adrp	x0, .LC45
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	add	x0, x0, :lo12:.LC45
	bl	printk
	ldrh	w2, [x19,1144]
	mov	w1, 0
	ldr	x0, [x19,1160]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1854:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	add	x29, sp, 0
	cbz	w1, .L1856
	bl	ftl_set_blk_mode.part.17
	b	.L1855
.L1856:
	adrp	x1, .LANCHOR0+16
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L1855:
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	adrp	x1, .LANCHOR0+16
	uxth	w0, w0
	ubfx	x2, x0, 5, 11
	ldr	x1, [x1,#:lo12:.LANCHOR0+16]
	ldr	w1, [x1,x2,lsl 2]
	lsr	w0, w1, w0
	and	w0, w0, 1
	ret
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	stp	x29, x30, [sp, -96]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC47
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC47
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	mov	w20, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	add	x19, x19, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, 3912
	mov	w1, 0
	mov	x2, 8192
	bl	memset
.L1860:
	ldr	w0, [x19,1396]
	cmp	w20, w0
	bcs	.L1877
	mov	w0, w20
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L1861
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x19, 3912
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L1861:
	add	w20, w20, 1
	b	.L1860
.L1877:
	ldr	x1, [x19,1184]
	adrp	x0, .LC48
	ldrh	w3, [x19,756]
	add	x0, x0, :lo12:.LC48
	mov	w2, 2
	adrp	x21, .LC50
	mov	w20, 0
	add	x21, x21, :lo12:.LC50
	bl	rknand_print_hex
	adrp	x0, .LC49
	ldrh	w3, [x19,756]
	add	x0, x0, :lo12:.LC49
	add	x1, x19, 3912
	mov	w2, 2
	bl	rknand_print_hex
.L1863:
	ldrh	w0, [x19,756]
	cmp	w0, w20
	bls	.L1878
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x19,1184]
	ldrh	w2, [x1,x0]
	add	x0, x19, 3912
	ldrh	w3, [x0,w20,sxtw 1]
	cmp	w2, w3
	beq	.L1864
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L1864:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1863
.L1878:
	ldr	x0, [x19,3592]
	cbz	x0, .L1859
	ldr	x20, [x19,1280]
	adrp	x22, .LC51
	ldrh	w23, [x19,3600]
	mov	w21, 0
	sub	x20, x0, x20
	mov	x0, -6148914691236517206
	asr	x20, x20, 1
	add	x22, x22, :lo12:.LC51
	mov	w24, 6
	mov	w25, 65535
	madd	x20, x0, x20, x20
	uxth	w20, w20
.L1867:
	cmp	w21, w23
	beq	.L1859
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x19,1184]
	ldrh	w2, [x1,x0]
	cbz	w2, .L1868
	add	x3, x19, 3912
	mov	x0, x22
	mov	w1, w20
	ldrh	w3, [x3,w20,sxtw 1]
	bl	printk
.L1868:
	umull	x20, w20, w24
	ldr	x0, [x19,1280]
	ldrh	w20, [x0,x20]
	cmp	w20, w25
	beq	.L1859
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1867
.L1859:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	sub	sp, sp, #96
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	str	x25, [sp,80]
	add	x19, x2, 1408
	uxth	w23, w0
	adrp	x22, .LC52
	ldr	x0, [x2,1048]
	adrp	x25, .LC53
	str	x0, [x19,8]
	lsl	w24, w23, 10
	ldr	x0, [x2,1112]
	mov	w20, 0
	str	x0, [x19,16]
	mov	x21, x2
	add	x22, x22, :lo12:.LC52
	add	x25, x25, :lo12:.LC53
.L1880:
	ldrh	w0, [x21,820]
	cmp	w20, w0
	bge	.L1886
	mov	w1, 1
	orr	w0, w20, w24
	mov	w2, w1
	str	w0, [x19,4]
	mov	x0, x19
	bl	FlashReadPages
	ldr	x0, [x19,8]
	mov	w1, w23
	ldr	x7, [x19,16]
	ldr	w0, [x0]
	str	w0, [sp]
	mov	x0, x22
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x19]
	ldr	w3, [x19,4]
	ldr	w7, [x7,12]
	bl	printk
	ldr	x0, [x19,16]
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1881
	ldr	x1, [x21,1048]
	mov	x0, x25
	mov	w2, 4
	mov	w3, 768
	bl	rknand_print_hex
.L1881:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1880
.L1886:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldr	x25, [sp,80]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 96
	ret
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	sub	sp, sp, #96
	adrp	x0, .LC54
	add	x0, x0, :lo12:.LC54
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	adrp	x23, .LANCHOR2
	stp	x21, x22, [sp,48]
	adrp	x21, .LC55
	add	x22, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp,32]
	add	x21, x21, :lo12:.LC55
	mov	w20, 0
	add	x19, x22, 1408
	bl	printk
.L1888:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,1396]
	cmp	w20, w0
	bcs	.L1895
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L1889
	ldr	w2, [x29,76]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L1889:
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L1891
	str	w0, [x19,4]
	mov	w1, 1
	ldr	x0, [x22,1048]
	mov	w2, 0
	str	x0, [x19,8]
	mov	x0, x19
	ldr	x24, [x22,1112]
	str	w20, [x19,24]
	str	x24, [x19,16]
	str	wzr, [x19]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1892
	cmp	w0, 256
	beq	.L1892
	ldr	w0, [x24,8]
	cmp	w0, w20
	beq	.L1891
.L1892:
	ldr	x7, [x19,8]
	mov	w1, w20
	ldr	x6, [x19,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC56
	add	x0, x0, :lo12:.LC56
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L1891:
	add	w20, w20, 1
	b	.L1888
.L1895:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 96
	ret
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	sub	sp, sp, #128
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x21, x22, [sp,48]
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp,80]
	stp	x23, x24, [sp,64]
	stp	x27, x28, [sp,96]
	stp	x19, x20, [sp,32]
	adrp	x24, .LC57
	mov	w25, 56
	ldrh	w21, [x0,756]
	mov	w26, 4
	mov	x27, 56
	add	x24, x24, :lo12:.LC57
.L1897:
	add	x19, x22, :lo12:.LANCHOR2
	ldrh	w0, [x19,758]
	cmp	w0, w21
	bls	.L1910
	mov	x23, 0
	ldrh	w3, [x19,748]
	ldrh	w2, [x19,830]
	mov	w20, w23
.L1906:
	cmp	w3, w23, uxth
	bls	.L1911
	add	x0, x19, 776
	mov	w1, w21
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,x23]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L1898
	umull	x0, w20, w25
	ldr	x1, [x19,992]
	lsl	w28, w28, 10
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x19,992]
	ldr	x4, [x19,1128]
	add	x0, x1, x0
	ldr	x1, [x19,1072]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w26
	uxth	w20, w20
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1898:
	add	x23, x23, 1
	b	.L1906
.L1911:
	cbz	w20, .L1901
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x19, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L1902:
	cmp	w20, w19, uxth
	bls	.L1901
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	madd	x0, x19, x27, x0
	add	x19, x19, 1
	ldr	x7, [x0,8]
	ldr	x6, [x0,16]
	ldr	w2, [x0,4]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ubfx	x1, x2, 10, 16
	mov	x0, x24
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w7, [x7]
	ldr	w6, [x6,12]
	bl	printk
	b	.L1902
.L1901:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1897
.L1910:
	adrp	x23, .LC58
	mov	w21, 0
	add	x22, x19, 1408
	add	x23, x23, :lo12:.LC58
.L1905:
	ldrh	w0, [x19,3908]
	cmp	w21, w0
	bge	.L1908
	sbfiz	x24, x21, 1, 32
	mov	w20, 0
.L1909:
	ldrh	w0, [x19,820]
	cmp	w20, w0
	bge	.L1912
	ldr	x0, [x19,1192]
	mov	w1, 1
	mov	w2, w1
	ldrh	w0, [x0,x24]
	orr	w0, w20, w0, lsl 10
	str	w0, [x22,4]
	mov	x0, x22
	add	w20, w20, 1
	bl	FlashReadPages
	sxth	w20, w20
	ldr	x0, [x22,8]
	ldr	x1, [x19,1192]
	ldr	x7, [x22,16]
	ldr	w2, [x0]
	ldrh	w1, [x1,x24]
	str	w2, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x22]
	ldr	w3, [x22,4]
	ldr	w7, [x7,12]
	bl	printk
	b	.L1909
.L1912:
	add	w21, w21, 1
	sxth	w21, w21
	b	.L1905
.L1908:
	ldr	x1, [x19,1192]
	adrp	x0, .LC59
	ldr	w3, [x19,848]
	add	x0, x0, :lo12:.LC59
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x1, [x19,1240]
	adrp	x0, .LC60
	ldrh	w3, [x19,856]
	add	x0, x0, :lo12:.LC60
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x19,1248]
	adrp	x0, .LC61
	ldrh	w3, [x19,856]
	add	x0, x0, :lo12:.LC61
	mov	w2, 4
	bl	rknand_print_hex
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 128
	ret
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldrh	w23, [x19,6]
	mov	w27, 0
	ldr	x0, [x0,40]
	lsl	w2, w23, 2
	ldrh	w24, [x19,8]
	str	x0, [x29,120]
	ldr	x22, [x19,16]
	ldr	x25, [x19,24]
	bl	ftl_memset
	add	x5, x20, :lo12:.LANCHOR2
	str	x20, [x29,112]
	add	x4, x5, 1408
	str	wzr, [x19,52]
	str	wzr, [x19,48]
	mov	x26, x5
	ldr	x0, [x5,1048]
	mov	x20, x4
	ldr	x21, [x5,1112]
	str	x0, [x5,1416]
	mov	w0, -1
	str	x21, [x5,1424]
	strh	w0, [x19]
	strh	w0, [x19,2]
	mov	w0, 1
	str	w0, [x19,56]
	sub	w0, w24, #1
	str	w0, [x29,108]
.L1914:
	cmp	w27, w24
	bge	.L1930
	ldr	w0, [x29,108]
	sxtw	x28, w27
	cmp	w27, w0
	bne	.L1915
	lsl	x0, x28, 1
	mov	w1, 1
	add	x24, x22, x0
	ldrh	w0, [x22,x0]
	mov	w22, 0
	bl	FtlGetLastWrittenPage
	strh	w27, [x19]
	sxth	w26, w0
	add	w0, w0, 1
	strh	w0, [x19,2]
	add	w26, w26, 1
	ldr	w0, [x25,x28,lsl 2]
	str	w0, [x19,48]
	ldr	x0, [x29,112]
	add	x25, x0, :lo12:.LANCHOR2
	add	x25, x25, 1408
.L1916:
	cmp	w22, w26
	bge	.L1930
	ldrh	w0, [x24]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w22, w0, lsl 10
	str	w0, [x25,4]
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L1917
	ldrh	w0, [x21,8]
	cmp	w0, w23
	bcs	.L1917
	ldrh	w2, [x21]
	ldrh	w1, [x19,4]
	cmp	w2, w1
	bne	.L1917
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,120]
	ldr	w1, [x25,4]
	str	w1, [x2,x0]
.L1917:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L1916
.L1930:
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldr	x0, [x29,112]
	ldrh	w1, [x19,2]
	add	x20, x0, :lo12:.LANCHOR2
	ldrh	w0, [x20,820]
	cmp	w1, w0
	bne	.L1920
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
	b	.L1920
.L1915:
	ldr	x0, [x26,1048]
	str	x0, [x20,8]
	lsl	x0, x28, 1
	add	x28, x22, x0
	ldrh	w1, [x22,x0]
	ldrh	w0, [x26,820]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L1931
	ldrh	w1, [x21]
	mov	w3, 0
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1940
	ldrh	w0, [x21,8]
	mov	w1, 64245
	cmp	w0, w1
	bne	.L1940
.L1922:
	ldrh	w0, [x26,820]
	sub	w0, w0, #1
	cmp	w3, w0
	bge	.L1925
	ldr	x2, [x26,1048]
	sbfiz	x1, x3, 3, 32
	ldrh	w0, [x2,x1]
	cmp	w0, w23
	bcs	.L1923
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,120]
	ldr	w1, [x1,4]
	str	w1, [x2,x0]
.L1923:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L1922
.L1931:
	mov	w3, 0
.L1940:
	ldrh	w0, [x26,820]
	cmp	w3, w0
	bge	.L1925
	ldrh	w0, [x28]
	mov	w1, 1
	mov	w2, w1
	str	x3, [x29,96]
	orr	w0, w3, w0, lsl 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldr	w0, [x20]
	ldr	x3, [x29,96]
	cmn	w0, #1
	beq	.L1926
	ldrh	w0, [x21,8]
	cmp	w0, w23
	bcs	.L1926
	ldrh	w2, [x21]
	ldrh	w1, [x19,4]
	cmp	w2, w1
	bne	.L1926
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,120]
	ldr	w1, [x20,4]
	str	w1, [x2,x0]
.L1926:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L1940
.L1925:
	add	w6, w27, 1
	sxth	w27, w6
	b	.L1914
.L1920:
	mov	x0, x19
	bl	ftl_map_blk_gc
	mov	x0, x19
	bl	ftl_map_blk_gc
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x0, x1, 3824
	add	x29, sp, 0
	ldrh	w2, [x1,840]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,1392]
	strh	w2, [x0,8]
	ldrh	w2, [x1,842]
	strh	w2, [x0,6]
	ldr	x2, [x1,1208]
	str	x2, [x0,16]
	ldr	x2, [x1,1224]
	str	x2, [x0,24]
	ldr	x2, [x1,1216]
	ldr	x1, [x1,1232]
	str	x2, [x0,32]
	str	x1, [x0,40]
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	stp	x29, x30, [sp, -32]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	ldr	w2, [x19,848]
	ldr	x0, [x19,1200]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x19,828]
	mov	w1, 255
	ldrh	w2, [x19,858]
	ldr	x0, [x19,1272]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w2, 0
	mov	w4, -1
.L1943:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,858]
	cmp	w1, w2
	bls	.L1945
	ldr	x3, [x0,1264]
	ubfiz	x1, x2, 4, 16
	add	x3, x3, x1
	str	wzr, [x3,4]
	ldr	x3, [x0,1264]
	strh	w4, [x3,x1]
	ldr	x3, [x0,1264]
	add	x3, x3, x1
	ldrh	w1, [x0,828]
	ldr	x0, [x0,1272]
	mul	w1, w1, w2
	add	w2, w2, 1
	sxtw	x1, w1
	uxth	w2, w2
	and	x1, x1, -4
	add	x0, x0, x1
	str	x0, [x3,8]
	b	.L1943
.L1945:
	mov	w2, -1
	strh	w2, [x0,3754]
	strh	w2, [x0,3752]
	add	x1, x0, 3752
	ldr	w2, [x0,848]
	strh	w2, [x0,3762]
	mov	w2, -3902
	strh	w2, [x0,3756]
	ldrh	w2, [x0,3908]
	strh	w2, [x0,3760]
	ldrh	w2, [x0,856]
	strh	w2, [x0,3758]
	ldr	x2, [x0,1192]
	str	x2, [x0,3768]
	ldr	x2, [x0,1256]
	str	x2, [x0,3776]
	ldr	x2, [x0,1200]
	str	x2, [x0,3784]
	ldr	x0, [x0,1240]
	str	x0, [x1,40]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlL2PDataInit
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 3752
	bl	FtlMapTblRecovery
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	adrp	x3, .LANCHOR2
	uxth	w1, w1
	add	x4, x3, :lo12:.LANCHOR2
	uxth	w2, w2
	strh	wzr, [x0,4]
	mov	w6, 65535
	ldrh	w4, [x4,748]
.L1948:
	cmp	w2, w4
	bcs	.L1954
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L1949
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L1949:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1948
.L1954:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,818]
	mov	x3, x0
.L1951:
	cmp	x3, x4
	beq	.L1955
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L1952
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L1952:
	add	x3, x3, 2
	b	.L1951
.L1955:
	ret
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	lsr	x0, x1, 10
	stp	x21, x22, [sp,32]
	str	w1, [x29,60]
	adrp	x21, .LANCHOR2
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, :lo12:.LANCHOR2
	ubfiz	x22, x0, 1, 16
	ldr	x3, [x2,1184]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L1957
	ldr	x19, [x2,3592]
	cbz	x19, .L1958
	ldrh	w3, [x2,3600]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,1280]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L1959:
	cmp	w1, w3
	beq	.L1958
	cmp	w19, w0
	bne	.L1960
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x21, 3592
	bl	List_remove_node
	ldrh	w0, [x21,3600]
	sub	w0, w0, #1
	strh	w0, [x21,3600]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,1184]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L1958
.L1960:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L1958
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1959
.L1957:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L1958:
	add	x1, x29, 60
	mov	w0, w20
	mov	w2, 1
	bl	log2phys
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	x19, x0
	strh	wzr, [x0,4]
	strb	wzr, [x0,7]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	add	x22, x19, 16
	mov	x21, 0
	mov	x20, x0
	mov	x23, x1
	mov	w25, -1
	ldrh	w24, [x1,748]
.L1966:
	cmp	w24, w21, uxth
	bls	.L1977
	add	x0, x23, 776
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1967
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L1967:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L1966
.L1977:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x0,818]
	mul	w1, w1, w2
	strh	w1, [x19,4]
	ldr	w1, [x0,652]
	cbz	w1, .L1969
	ldrh	w1, [x19]
	ldr	x0, [x0,1168]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L1969
	mov	w0, 1
	strb	w0, [x19,9]
.L1969:
	adrp	x0, .LANCHOR0+24
	ldrb	w0, [x0,#:lo12:.LANCHOR0+24]
	cbz	w0, .L1970
	mov	w0, 1
	strb	w0, [x19,9]
.L1970:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	make_superblock, .-make_superblock
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	stp	x29, x30, [sp, -64]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	str	x23, [sp,48]
	add	x19, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	add	x21, x19, 1408
	add	x23, x19, 3896
	ldr	x0, [x19,1048]
	ldrh	w2, [x19,756]
	str	x0, [x21,8]
	ldr	x0, [x19,1112]
	lsl	w2, w2, 1
	str	x0, [x21,16]
	ldr	x0, [x19,1184]
	bl	ftl_memset
	ldrh	w0, [x19,3896]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1979
.L1987:
	mov	w19, -1
	b	.L1980
.L1979:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x23,2]
	mov	w23, 61604
.L1981:
	tbnz	w22, #31, .L1986
	ldrh	w0, [x19,3896]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w22, w0, lsl 10
	str	w0, [x21,4]
	ldr	x0, [x19,1048]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1982
	ldr	x0, [x19,1048]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1982
	ldr	x0, [x19,1112]
	ldrh	w0, [x0]
	cmp	w0, w23
	bne	.L1982
.L1986:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w2, 48
	add	x22, x19, 1408
	add	x0, x19, 3520
	adrp	x21, .LANCHOR0
	ldr	x1, [x22,8]
	bl	ftl_memcpy
	ldr	x1, [x22,8]
	ldrh	w2, [x19,756]
	ldr	x0, [x19,1184]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,756]
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x3, [x22,8]
	uxth	x1, w2
	ldr	x0, [x0,16]
	add	x1, x1, 24
	lsr	w2, w2, 3
	lsr	x1, x1, 1
	add	w2, w2, 4
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x19,860]
	cbz	w0, .L1984
	ldrh	w0, [x19,756]
	ldr	x3, [x22,8]
	lsr	w1, w0, 3
	ldrh	w2, [x19,856]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x19,1248]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1984
.L1982:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1981
.L1984:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,3520]
	cmp	w3, w2
	bne	.L1987
	ldrb	w3, [x0,3530]
	ldrh	w2, [x0,770]
	ldrh	w6, [x0,3528]
	strh	w6, [x0,3902]
	cmp	w3, w2
	bne	.L1987
	ldrh	w3, [x0,818]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,824]
	adrp	x4, .LANCHOR4
	add	x5, x4, :lo12:.LANCHOR4
	mov	x19, x4
	mul	w3, w3, w6
	str	w3, [x0,1396]
	str	w6, [x5,3400]
	mul	w3, w3, w2
	str	w3, [x21,4048]
	ldrh	w2, [x0,1302]
	ldr	w3, [x0,760]
	strh	wzr, [x5,3410]
	sub	w2, w3, w2
	ldrh	w3, [x0,748]
	sub	w2, w2, w6
	strb	wzr, [x5,3414]
	strb	wzr, [x5,3416]
	udiv	w2, w2, w3
	ldrh	w3, [x0,3536]
	strh	w2, [x5,3404]
	lsr	w7, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,3614]
	ldrb	w3, [x0,3531]
	strb	w3, [x0,3616]
	mov	w3, -1
	strh	w3, [x5,3408]
	ldrh	w3, [x0,3538]
	strh	w3, [x0,3656]
	ldrh	w3, [x0,3540]
	ldrh	w6, [x0,3534]
	lsr	w5, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,3662]
	ldrb	w3, [x0,3532]
	strb	w3, [x0,3664]
	ldrh	w3, [x0,3542]
	strh	w3, [x0,3704]
	ldrh	w3, [x0,3544]
	strh	w5, [x0,3658]
	lsr	w5, w3, 6
	and	w3, w3, 63
	strh	w6, [x0,3608]
	strb	w3, [x0,3710]
	strh	w7, [x0,3610]
	strh	w5, [x0,3706]
	ldrb	w3, [x0,3533]
	strb	w3, [x0,3712]
	ldr	w2, [x0,3552]
	str	w2, [x0,904]
	ldr	w1, [x0,3560]
	ldr	w2, [x0,872]
	str	wzr, [x0,880]
	str	wzr, [x0,884]
	cmp	w1, w2
	str	wzr, [x0,900]
	str	wzr, [x0,896]
	str	wzr, [x0,908]
	str	wzr, [x0,916]
	str	wzr, [x0,892]
	bls	.L1988
	str	w1, [x0,872]
.L1988:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,3556]
	ldr	w2, [x0,876]
	cmp	w1, w2
	bls	.L1989
	str	w1, [x0,876]
.L1989:
	mov	w0, 65535
	cmp	w6, w0
	beq	.L1990
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 3608
	bl	make_superblock
.L1990:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 3656
	ldrh	w2, [x1,3656]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1991
	bl	make_superblock
.L1991:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x20, 3704
	ldrh	w2, [x20,3704]
	cmp	w2, w1
	beq	.L1992
	bl	make_superblock
.L1992:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w19, 0
	add	x0, x1, 3408
	ldrh	w2, [x1,3408]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1980
	bl	make_superblock
.L1980:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	sub	sp, sp, #176
	lsr	x0, x0, 10
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	uxtb	w24, w1
	bl	P2V_block_in_plane
	adrp	x2, .LANCHOR3
	adrp	x20, .LANCHOR2
	uxth	w22, w0
	add	x1, x2, :lo12:.LANCHOR3
	add	x19, x20, :lo12:.LANCHOR2
	adrp	x0, .LC47
	add	x1, x1, 32
	add	x0, x0, :lo12:.LC47
	ldrh	w23, [x19,818]
	bl	printk
	ldr	x3, [x19,1184]
	ubfiz	x2, x22, 1, 16
	adrp	x0, .LC62
	mov	w1, w22
	add	x0, x0, :lo12:.LC62
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w22, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w24, .L2006
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2006
	mov	w0, w22
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w22, w0
	bne	.L1998
	ldrh	w23, [x19,820]
	b	.L1998
.L2006:
	mov	w22, 0
.L1998:
	add	x3, x20, :lo12:.LANCHOR2
	adrp	x0, .LC63
	adrp	x24, .LC58
	add	x0, x0, :lo12:.LC63
	mov	w1, w22
	mov	w2, w23
	ldrh	w3, [x3,818]
	mov	w21, 0
	mov	w26, 65535
	mov	w27, 56
	mov	w25, 4
	add	x24, x24, :lo12:.LC58
	bl	printk
.L1999:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x3, 0
	mov	w19, w3
	ldrh	w5, [x0,748]
	ldrh	w6, [x0,828]
	ldrh	w7, [x0,830]
.L2000:
	cmp	w5, w3, uxth
	bls	.L2014
	add	x0, x29, 112
	add	x0, x0, x3, lsl 1
	ldrh	w1, [x0,16]
	cmp	w1, w26
	beq	.L2001
	add	x2, x20, :lo12:.LANCHOR2
	orr	w1, w21, w1, lsl 10
	umull	x0, w19, w27
	ldr	x4, [x2,992]
	add	x4, x4, x0
	str	w1, [x4,4]
	ldr	x1, [x2,992]
	ldr	x4, [x2,1072]
	add	x0, x1, x0
	ldr	x2, [x2,1128]
	mul	w1, w19, w6
	sdiv	w1, w1, w25
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,8]
	mul	w1, w19, w7
	add	w19, w19, 1
	uxth	w19, w19
	sdiv	w1, w1, w25
	add	x1, x2, x1, sxtw 2
	str	x1, [x0,16]
.L2001:
	add	x3, x3, 1
	b	.L2000
.L2014:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w22
	mov	x28, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
	mov	x8, 56
.L2003:
	cmp	w19, w28, uxth
	bls	.L2015
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x28, x8
	str	x8, [x29,104]
	add	x28, x28, 1
	ldr	x4, [x0,992]
	add	x3, x4, x2
	ldr	x0, [x3,8]
	ldr	x7, [x3,16]
	ldr	w5, [x0]
	str	w5, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x24
	ldr	w2, [x4,x2]
	ldr	w5, [x7,4]
	ldr	w4, [x7]
	ldr	w6, [x7,8]
	ldr	w3, [x3,4]
	ldr	w7, [x7,12]
	bl	printk
	ldr	x8, [x29,104]
	b	.L2003
.L2015:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, w23
	bne	.L1999
.L2005:
	sub	sp, x29, #16
	mov	w0, 0
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 176
	ret
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	sub	sp, sp, #144
	adrp	x1, .LANCHOR3
	adrp	x0, .LC47
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC47
	add	x1, x1, 56
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LC65
	adrp	x22, .LC64
	adrp	x24, .LC66
	add	x22, x22, :lo12:.LC64
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC65
	mov	w20, 0
	add	x24, x24, :lo12:.LC66
	stp	x25, x26, [sp,80]
	bl	printk
.L2017:
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	ldrh	w0, [x19,758]
	cmp	w0, w20
	bls	.L2026
	mov	w0, w20
	strh	w20, [x29,80]
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x4, [x19,1184]
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,748]
	ldrh	w7, [x19,828]
	mov	w9, 65535
	ldrh	w8, [x19,830]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L2018:
	cmp	w6, w1, uxth
	bls	.L2027
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w0, [x0,16]
	cmp	w0, w9
	beq	.L2019
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w0, w0, 10
	umull	x2, w19, w10
	ldr	x4, [x3,992]
	add	x4, x4, x2
	str	w0, [x4,4]
	ldr	x0, [x3,992]
	ldr	x4, [x3,1072]
	add	x2, x0, x2
	ldr	x3, [x3,1128]
	mul	w0, w19, w7
	sdiv	w0, w0, w5
	add	x0, x4, x0, sxtw 2
	str	x0, [x2,8]
	mul	w0, w19, w8
	add	w19, w19, 1
	uxth	w19, w19
	sdiv	w0, w0, w5
	add	x0, x3, x0, sxtw 2
	str	x0, [x2,16]
.L2019:
	add	x1, x1, 1
	b	.L2018
.L2027:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L2021:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L2028
	ldr	x4, [x0,992]
	mul	x2, x25, x26
	ldrh	w1, [x29,80]
	add	x25, x25, 1
	add	x3, x4, x2
	ldr	x0, [x3,8]
	ldr	x7, [x3,16]
	ldr	w5, [x0]
	str	w5, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w2, [x4,x2]
	ldr	w5, [x7,4]
	ldr	w4, [x7]
	ldr	w6, [x7,8]
	ldr	w3, [x3,4]
	ldr	w7, [x7,12]
	bl	printk
	b	.L2021
.L2028:
	ldr	x0, [x0,992]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L2023:
	cmp	w19, w25, uxth
	bls	.L2029
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,992]
	add	x3, x4, x2
	ldr	x0, [x3,8]
	ldr	x7, [x3,16]
	ldr	w5, [x0]
	str	w5, [sp]
	ldr	w0, [x0,4]
	str	w0, [sp,8]
	mov	x0, x24
	ldr	w2, [x4,x2]
	ldr	w5, [x7,4]
	ldr	w4, [x7]
	ldr	w6, [x7,8]
	ldr	w3, [x3,4]
	ldr	w7, [x7,12]
	bl	printk
	b	.L2023
.L2029:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2017
.L2026:
	sub	sp, x29, #16
	mov	w0, 0
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 144
	ret
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	stp	x29, x30, [sp, -112]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w23, 6
	mov	w27, 0
	ldrh	w2, [x19,758]
	mov	w22, w27
	ldr	x0, [x19,1280]
	mov	w24, -1
	mov	w25, 32768
	mul	w2, w2, w23
	bl	ftl_memset
	str	xzr, [x19,3592]
	str	xzr, [x19,3568]
	str	xzr, [x19,3576]
	strh	wzr, [x19,3584]
	strh	wzr, [x19,3600]
	strh	wzr, [x19,862]
	mov	w19, w27
.L2031:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,756]
	cmp	w19, w1
	bge	.L2038
	mov	w20, 0
	ldrh	w3, [x0,748]
	ldrh	w2, [x0,818]
	mov	w28, w20
	mov	x26, x0
.L2039:
	cmp	w28, w3
	bge	.L2040
	add	x0, x26, 776
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L2032
	add	w20, w2, w20
	sxth	w20, w20
.L2032:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L2039
.L2040:
	cbz	w20, .L2034
	sdiv	w20, w25, w20
	sxth	w20, w20
	b	.L2035
.L2034:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,1184]
	strh	w24, [x0,w19,sxtw 1]
.L2035:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1280]
	smaddl	x1, w19, w23, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,3608]
	cmp	w19, w1
	beq	.L2036
	ldrh	w1, [x0,3656]
	cmp	w19, w1
	beq	.L2036
	ldrh	w1, [x0,3704]
	cmp	w19, w1
	beq	.L2036
	ldr	x0, [x0,1184]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L2037
	add	w27, w27, 1
	mov	w0, w19
	uxth	w27, w27
	bl	INSERT_FREE_LIST
	b	.L2036
.L2037:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L2036:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2031
.L2038:
	strh	w22, [x0,3584]
	strh	w27, [x0,3600]
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR4
	mov	w1, 19539
	add	x0, x0, :lo12:.LANCHOR4
	movk	w1, 0x4654, lsl 16
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 3456
	add	x29, sp, 0
	ldr	w3, [x0,3456]
	cmp	w3, w1
	bne	.L2041
	mov	w1, 80
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,3968]
	ldr	w0, [x0,3972]
	str	w0, [x2,92]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x2,88]
	ldr	w1, [x0,880]
	str	w1, [x2,8]
	ldr	w1, [x0,884]
	str	w1, [x2,12]
	ldr	w1, [x0,900]
	str	w1, [x2,16]
	ldr	w1, [x0,896]
	str	w1, [x2,20]
	ldr	w1, [x0,908]
	str	w1, [x2,28]
	ldr	w1, [x0,912]
	str	w1, [x2,32]
	ldr	w1, [x0,888]
	str	w1, [x2,36]
	ldr	w1, [x0,892]
	str	w1, [x2,40]
	ldr	w1, [x0,916]
	str	w1, [x2,44]
	ldr	w1, [x0,920]
	str	w1, [x2,48]
	ldr	w1, [x0,868]
	ldr	w0, [x0,864]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2041:
	ldp	x29, x30, [sp], 16
	ret
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	adrp	x2, .LANCHOR2
	mov	w1, 32
	add	x3, x2, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w4, [x3,652]
	cbz	w4, .L2044
	ldr	w3, [x3,920]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2044:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,3976]
	cmp	w4, 31
	bhi	.L2045
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,3976]
.L2045:
	cbnz	w0, .L2046
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,1160]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2047
.L2046:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,1160]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,1160]
	ldrh	w1, [x2,1144]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,1160]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2047:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	stp	x29, x30, [sp, -48]!
	mov	w1, 1
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	str	x21, [sp,32]
	add	x20, x19, :lo12:.LANCHOR4
	add	x21, x20, 3456
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,3456]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2052
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,3456]
.L2052:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	add	x0, x1, 3456
	adrp	x20, .LANCHOR2
	ldr	w3, [x1,3456]
	cmp	w3, w2
	bne	.L2053
	ldr	w2, [x1,3544]
	str	w2, [x1,3968]
	ldr	w2, [x1,3548]
	str	w2, [x1,3972]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x0,8]
	str	w2, [x1,880]
	ldr	w2, [x0,12]
	str	w2, [x1,884]
	ldr	w2, [x0,16]
	str	w2, [x1,900]
	ldr	w2, [x0,20]
	str	w2, [x1,896]
	ldr	w2, [x0,28]
	str	w2, [x1,908]
	ldr	w2, [x0,32]
	str	w2, [x1,912]
	ldr	w2, [x0,36]
	str	w2, [x1,888]
	ldr	w2, [x0,40]
	str	w2, [x1,892]
	ldr	w2, [x0,44]
	str	w2, [x1,916]
	ldr	w2, [x0,48]
	ldr	w0, [x0,60]
	str	w2, [x1,920]
	str	w0, [x1,868]
.L2053:
	add	x1, x19, :lo12:.LANCHOR4
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w2, [x1,3524]
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	str	wzr, [x0,864]
	cmp	w2, w1
	bne	.L2054
	mov	w1, 1
	str	w1, [x0,652]
	adrp	x0, .LC45
	adrp	x1, .LC67
	add	x0, x0, :lo12:.LC45
	add	x1, x1, :lo12:.LC67
	bl	printk
.L2054:
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR4
	ldr	x21, [sp,32]
	ldrh	w2, [x0,808]
	ldr	w3, [x0,904]
	ldr	w1, [x0,908]
	ldrh	w0, [x0,756]
	madd	w1, w2, w3, w1
	udiv	w0, w1, w0
	str	w0, [x19,3980]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x22, 0
	mov	x21, x0
	ldrh	w24, [x1,748]
	mov	w19, w22
	ldrh	w25, [x1,818]
	mov	x23, x1
.L2056:
	cmp	w24, w22, uxth
	bls	.L2063
	add	x0, x23, 776
	mov	w1, w20
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2057
	add	w19, w19, w25
	uxth	w19, w19
.L2057:
	add	x22, x22, 1
	b	.L2056
.L2063:
	cbz	w19, .L2059
	mov	w0, 32768
	sdiv	w19, w0, w19
.L2059:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 6
	umull	x20, w20, w1
	ldr	x0, [x0,1280]
	add	x20, x0, x20
	mov	w0, 0
	strh	w19, [x20,4]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x1, [x2,3592]
	mov	w0, w1
	cbz	x1, .L2065
	ldr	x0, [x2,1280]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,1168]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L2065:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,3592]
	mov	w0, w1
	cbz	x1, .L2068
	ldrh	w3, [x5,3600]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,1280]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L2070:
	cmp	w3, w4
	beq	.L2073
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L2071
.L2073:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,1168]
	ldrh	w0, [x0,x1]
	b	.L2068
.L2071:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L2070
.L2068:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC68
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	add	x1, x1, :lo12:.LC68
	add	x25, x25, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x23, x0
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x25,4016]
	adrp	x1, .LC69
	adrp	x24, .LANCHOR2
	mov	x0, x20
	add	x1, x1, :lo12:.LC69
	add	x22, x24, :lo12:.LANCHOR2
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC70
	mov	x0, x20
	ldr	w2, [x22,836]
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+3160
	ldr	w0, [x0,#:lo12:.LANCHOR1+3160]
	cmp	w0, 1
	beq	.L2076
	sub	w0, w20, w23
	b	.L2077
.L2076:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	bl	NandcGetTimeCfg
	ldr	w4, [x29,104]
	adrp	x1, .LC71
	ldr	w5, [x29,108]
	mov	x0, x20
	ldr	w3, [x29,100]
	add	x1, x1, :lo12:.LC71
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC72
	mov	x0, x21
	add	x1, x1, :lo12:.LC72
	add	x21, x21, 10
	adrp	x20, .LANCHOR4
	bl	strcpy
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x22,1396]
	adrp	x1, .LC73
	mov	x0, x21
	add	x1, x1, :lo12:.LC73
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,3892]
	adrp	x1, .LC74
	mov	x0, x21
	add	x1, x1, :lo12:.LC74
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,900]
	adrp	x1, .LC75
	mov	x0, x21
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,888]
	adrp	x1, .LC76
	mov	x0, x21
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,884]
	adrp	x1, .LC77
	mov	x0, x21
	add	x1, x1, :lo12:.LC77
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,892]
	adrp	x1, .LC78
	mov	x0, x21
	add	x1, x1, :lo12:.LC78
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,896]
	adrp	x1, .LC79
	mov	x0, x21
	add	x1, x1, :lo12:.LC79
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,880]
	adrp	x1, .LC80
	mov	x0, x21
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3968]
	adrp	x1, .LC81
	mov	x0, x21
	add	x1, x1, :lo12:.LC81
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3972]
	adrp	x1, .LC82
	mov	x0, x21
	add	x1, x1, :lo12:.LC82
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,872]
	adrp	x1, .LC83
	mov	x0, x21
	add	x1, x1, :lo12:.LC83
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,876]
	adrp	x1, .LC84
	add	x1, x1, :lo12:.LC84
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x22,1302]
	adrp	x1, .LC85
	mov	x0, x21
	add	x1, x1, :lo12:.LC85
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3600]
	adrp	x1, .LC86
	mov	x0, x21
	add	x1, x1, :lo12:.LC86
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,904]
	adrp	x1, .LC87
	mov	x0, x21
	add	x1, x1, :lo12:.LC87
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,908]
	adrp	x1, .LC88
	mov	x0, x21
	add	x1, x1, :lo12:.LC88
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3980]
	adrp	x1, .LC89
	mov	x0, x21
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,912]
	adrp	x1, .LC90
	mov	x0, x21
	add	x1, x1, :lo12:.LC90
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,916]
	adrp	x1, .LC91
	mov	x0, x21
	add	x1, x1, :lo12:.LC91
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,920]
	adrp	x1, .LC92
	mov	x0, x21
	add	x1, x1, :lo12:.LC92
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3550]
	adrp	x1, .LC93
	mov	x0, x21
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3548]
	adrp	x1, .LC94
	mov	x0, x21
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,4048]
	adrp	x1, .LC95
	mov	x0, x21
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,852]
	adrp	x1, .LC96
	mov	x0, x21
	add	x1, x1, :lo12:.LC96
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,744]
	adrp	x1, .LC97
	mov	x0, x21
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1470]
	adrp	x1, .LC98
	mov	x0, x21
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,756]
	adrp	x1, .LC99
	mov	x0, x21
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3404]
	adrp	x1, .LC100
	mov	x0, x21
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,760]
	adrp	x1, .LC101
	mov	x0, x21
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3896]
	adrp	x1, .LC102
	mov	x0, x21
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1296]
	adrp	x1, .LC103
	mov	x0, x21
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3610]
	adrp	x1, .LC104
	mov	x0, x21
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3614]
	adrp	x1, .LC105
	mov	x0, x21
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3608]
	adrp	x1, .LC106
	mov	x0, x21
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3616]
	adrp	x1, .LC107
	mov	x0, x21
	add	x1, x1, :lo12:.LC107
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3612]
	adrp	x1, .LC108
	mov	x0, x21
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3608]
	adrp	x1, .LC109
	ldr	x2, [x22,1184]
	mov	x0, x21
	add	x1, x1, :lo12:.LC109
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3658]
	adrp	x1, .LC110
	mov	x0, x21
	add	x1, x1, :lo12:.LC110
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3662]
	adrp	x1, .LC111
	mov	x0, x21
	add	x1, x1, :lo12:.LC111
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3656]
	adrp	x1, .LC112
	mov	x0, x21
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3664]
	adrp	x1, .LC113
	mov	x0, x21
	add	x1, x1, :lo12:.LC113
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3660]
	adrp	x1, .LC114
	mov	x0, x21
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3656]
	adrp	x1, .LC115
	ldr	x2, [x22,1184]
	mov	x0, x21
	add	x1, x1, :lo12:.LC115
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3706]
	adrp	x1, .LC116
	mov	x0, x21
	add	x1, x1, :lo12:.LC116
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3710]
	adrp	x1, .LC117
	mov	x0, x21
	add	x1, x1, :lo12:.LC117
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3704]
	adrp	x1, .LC118
	mov	x0, x21
	add	x1, x1, :lo12:.LC118
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3712]
	adrp	x1, .LC119
	mov	x0, x21
	add	x1, x1, :lo12:.LC119
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3708]
	adrp	x1, .LC120
	mov	x0, x21
	add	x1, x1, :lo12:.LC120
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3410]
	adrp	x1, .LC121
	mov	x0, x21
	add	x1, x1, :lo12:.LC121
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,3414]
	adrp	x1, .LC122
	mov	x0, x21
	add	x1, x1, :lo12:.LC122
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3408]
	adrp	x1, .LC123
	mov	x0, x21
	add	x1, x1, :lo12:.LC123
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,3416]
	adrp	x1, .LC124
	mov	x0, x21
	add	x1, x1, :lo12:.LC124
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3412]
	adrp	x1, .LC125
	mov	x0, x21
	add	x1, x1, :lo12:.LC125
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,868]
	adrp	x1, .LC126
	ldr	w2, [x22,652]
	add	x1, x1, :lo12:.LC126
	ldr	w4, [x20,3540]
	mov	x0, x21
	ldr	w5, [x20,3532]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x20,3536]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x20,3528]
	adrp	x1, .LC127
	mov	x0, x19
	add	x1, x1, :lo12:.LC127
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x20,3552]
	adrp	x1, .LC128
	mov	x0, x19
	add	x1, x1, :lo12:.LC128
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,956]
	adrp	x1, .LC129
	mov	x0, x19
	add	x1, x1, :lo12:.LC129
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,958]
	adrp	x1, .LC130
	mov	x0, x19
	add	x1, x1, :lo12:.LC130
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,940]
	adrp	x1, .LC131
	mov	x0, x19
	add	x1, x1, :lo12:.LC131
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,960]
	adrp	x1, .LC132
	add	x1, x1, :lo12:.LC132
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC133
	mov	x0, x19
	add	x1, x1, :lo12:.LC133
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,3600]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC134
	mov	x0, x19
	add	x1, x1, :lo12:.LC134
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x20,3408]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2078
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,1184]
	adrp	x1, .LC135
	mov	x0, x19
	add	x1, x1, :lo12:.LC135
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L2078:
	mov	w0, 0
	adrp	x22, .LC136
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC136
.L2080:
	cmp	w3, w27
	beq	.L2079
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,1280]
	mov	x1, x22
	ldr	x4, [x21,1184]
	add	w20, w20, 1
	ldr	x7, [x21,1168]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,1280]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L2080
.L2079:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC137
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x22, x22, :lo12:.LC137
	ldr	x0, [x1,3592]
	ldr	x3, [x1,1280]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L2082:
	cmp	w3, w27
	beq	.L2081
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,1280]
	mov	x1, x22
	ldr	x6, [x21,1168]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,1280]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L2082
.L2081:
	sub	w0, w19, w23
.L2077:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC45
	adrp	x2, .LC138
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC45
	add	x2, x2, :lo12:.LC138
	stp	x19, x20, [sp,16]
	mov	x20, x0
	bl	sprintf
	add	x19, x20, x0, sxtw
	mov	x0, x19
	bl	FtlPrintInfo2buf
	add	x0, x19, x0, sxtw
	sub	w0, w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	stp	x29, x30, [sp, -96]!
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	adrp	x19, .LANCHOR2
	ldr	w3, [x1,3980]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,920]
	cmp	w3, w2
	bcs	.L2087
	str	wzr, [x1,904]
	ldrh	w3, [x1,756]
	ldr	x5, [x1,1168]
	mov	x1, 0
.L2088:
	cmp	w3, w1
	bls	.L2120
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x1,lsl 1]
	add	x1, x1, 1
	ldr	w2, [x4,904]
	add	w2, w6, w2
	str	w2, [x4,904]
	b	.L2088
.L2120:
	add	x2, x19, :lo12:.LANCHOR2
	add	x4, x0, :lo12:.LANCHOR4
	ldr	w1, [x2,904]
	udiv	w3, w1, w3
	str	w3, [x4,3980]
	ldr	w3, [x2,908]
	sub	w1, w1, w3
	ldrh	w3, [x2,808]
	udiv	w1, w1, w3
	str	w1, [x2,904]
	b	.L2090
.L2087:
	ldr	w2, [x1,916]
	cmp	w3, w2
	bls	.L2090
	add	w2, w2, 1
	str	w2, [x1,916]
	mov	w1, 0
.L2092:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w3, [x2,756]
	cmp	w1, w3
	bcs	.L2090
	ubfiz	x3, x1, 1, 32
	ldr	x4, [x2,1168]
	add	w1, w1, 1
	ldrh	w2, [x4,x3]
	add	w2, w2, 1
	strh	w2, [x4,x3]
	b	.L2092
.L2090:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w21, [x1,920]
	ldr	w23, [x0,3980]
	add	w0, w21, 256
	cmp	w0, w23
	bls	.L2095
	ldr	w2, [x1,916]
	add	w0, w21, 768
	cmp	w0, w2
	bls	.L2095
	ldr	w0, [x1,652]
	cbz	w0, .L2098
	cmp	w21, 40
	bls	.L2095
.L2098:
	mov	w0, 65535
	b	.L2097
.L2095:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3600]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L2107
	cmp	w21, 40
	bhi	.L2098
.L2107:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x3, [x1,3568]
	cbz	x3, .L2098
	mov	w26, 65535
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,756]
	mov	w20, w26
	ldr	x7, [x1,1280]
	mov	w8, w26
	ldr	x25, [x1,1168]
	add	x0, x0, 1
	mov	w1, 0
	mov	w10, 6
.L2100:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L2102
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L2098
	ldrh	w2, [x3,4]
	cbz	w2, .L2101
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w21
	bls	.L2106
	cmp	w2, w26
	bcs	.L2101
	mov	w26, w2
	mov	w20, w3
.L2101:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L2100
.L2106:
	mov	w20, w3
.L2102:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2098
	ubfiz	x24, x20, 1, 16
	ldrh	w22, [x25,x24]
	cmp	w22, w21
	bls	.L2104
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L2104
	add	x0, x19, :lo12:.LANCHOR2
	str	w26, [x0,920]
.L2104:
	cmp	w22, w23
	bcs	.L2098
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L2098
	add	w0, w22, 256
	cmp	w0, w23
	bcc	.L2105
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,916]
	cmp	w22, w0
	bcs	.L2098
.L2105:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC139
	ldrh	w5, [x25,x24]
	add	x0, x0, :lo12:.LC139
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,1184]
	ldr	w3, [x19,916]
	ldrh	w4, [x4,x24]
	bl	printk
	mov	w0, 1
	str	w0, [x19,928]
	mov	w0, w20
.L2097:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	stp	x29, x30, [sp, -16]!
	uxth	w0, w0
	mov	w1, 65535
	cmp	w0, w1
	add	x29, sp, 0
	beq	.L2122
	adrp	x2, .LANCHOR2+1184
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+1184]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L2122:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w1, [x19,1400]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L2124
	mov	x20, x0
	add	x23, x19, 3704
	adrp	x22, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 3592
.L2125:
	cmp	x20, x23
	bne	.L2126
	ldrh	w2, [x19,3600]
	ldr	w4, [x19,928]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	ldr	w0, [x19,652]
	uxth	w1, w1
	cbz	w0, .L2127
	ldr	w0, [x19,920]
	cmp	w0, 39
	bhi	.L2127
	cmp	w0, 2
	mov	w1, 0
	bls	.L2128
	tbz	x2, 0, .L2155
	cbz	w4, .L2128
.L2155:
	mov	w1, w3
	b	.L2127
.L2126:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2128
	ldrh	w0, [x19,768]
	cmp	w0, 1
	beq	.L2128
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L2128
	ldr	w2, [x19,652]
	ldrh	w0, [x19,3600]
	lsr	w1, w0, 3
	cbz	w2, .L2127
	ldr	w2, [x19,920]
	cmp	w2, 1
	bhi	.L2127
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2127:
	cbz	w1, .L2128
	sub	w1, w1, #1
	uxth	w1, w1
.L2128:
	ldrb	w2, [x20,8]
	mov	x0, x25
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,3600]
	sub	w0, w0, #1
	strh	w0, [x19,3600]
	ldrh	w0, [x19,756]
	cmp	w0, w28
	bls	.L2125
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19,1184]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2125
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2180
	mov	x0, 0
	ldrh	w5, [x19,748]
	mov	w27, w0
	mov	x3, 56
	mov	w6, 65535
.L2132:
	cmp	w5, w0, uxth
	bls	.L2183
	ldr	x4, [x19,1016]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2133
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2133:
	add	x0, x0, 1
	b	.L2132
.L2183:
	add	x0, x19, 3608
	cmp	x20, x0
	bne	.L2135
	ldr	w1, [x19,652]
	cbz	w1, .L2135
	ldr	x1, [x19,1168]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2135
	strb	wzr, [x19,3616]
.L2135:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2136
	ldr	x0, [x19,1168]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2181
	ldrh	w1, [x19,808]
	add	w1, w2, w1
.L2181:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,904]
	add	w0, w0, 1
	str	w0, [x19,904]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2139
.L2136:
	ldr	x1, [x19,1168]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,908]
	add	w0, w0, 1
	str	w0, [x19,908]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.17
.L2139:
	ldr	x0, [x19,1168]
	ldr	w1, [x19,916]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2140
	str	w0, [x19,916]
.L2140:
	ldrh	w1, [x19,808]
	mov	x3, 56
	ldr	w2, [x19,904]
	ldr	w0, [x19,908]
	madd	w0, w1, w2, w0
	ldrh	w1, [x19,756]
	udiv	w0, w0, w1
	adrp	x1, .LANCHOR4+3980
	str	w0, [x1,#:lo12:.LANCHOR4+3980]
	ldr	x1, [x19,1160]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2141:
	cmp	w27, w0, uxth
	bls	.L2184
	ldr	x1, [x19,1016]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2141
.L2184:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2143
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,1016]
	bne	.L2144
	mov	w1, 0
	b	.L2182
.L2144:
	mov	w1, 1
.L2182:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2143:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,1016]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2145:
	cmp	w27, w1, uxth
	bls	.L2185
	mul	x0, x1, x4
	ldr	x3, [x19,1016]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2146
	ldr	w0, [x5,4]
	add	w2, w2, 1
	str	x4, [x29,104]
	lsr	w0, w0, 10
	str	x3, [x29,112]
	str	x1, [x29,120]
	str	x2, [x29,128]
	str	x2, [x29,136]
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,120]
	ldr	x3, [x29,112]
	ldr	x4, [x29,104]
	add	x0, x20, x1, lsl 1
	ldr	x2, [x29,136]
	strh	w3, [x0,16]
	ldrb	w0, [x20,7]
	sub	w0, w0, #1
	strb	w0, [x20,7]
.L2146:
	add	x1, x1, 1
	b	.L2145
.L2185:
	cbz	w2, .L2148
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2148:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2149
.L2180:
	ldr	x0, [x19,1184]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2125
.L2149:
	add	x21, x21, :lo12:.LANCHOR2
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x21,818]
	strh	w28, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21,872]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x21,872]
	ldr	x1, [x21,1184]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2124:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, .LANCHOR4+3984
	mov	w1, 0
	adrp	x7, .LANCHOR2
	mov	w8, 24
	mov	w9, 1
	mov	w4, 4
	str	wzr, [x0,#:lo12:.LANCHOR4+3984]
	mov	w10, 56
.L2187:
	add	x2, x7, :lo12:.LANCHOR2
	adrp	x6, .LANCHOR2
	ldrh	w0, [x2,748]
	cmp	w1, w0
	bcs	.L2191
	umull	x3, w1, w8
	ldr	x5, [x2,1104]
	ldr	x6, [x2,1072]
	add	x0, x5, x3
	str	w9, [x0,16]
	ldrh	w0, [x2,828]
	mul	w0, w1, w0
	sdiv	w0, w0, w4
	add	x0, x6, x0, sxtw 2
	str	x0, [x5,x3]
	ldrh	w0, [x2,830]
	ldr	x6, [x2,1104]
	ldr	x11, [x2,1128]
	mul	w0, w1, w0
	add	x5, x6, x3
	ldr	x2, [x2,1024]
	sdiv	w0, w0, w4
	add	x0, x11, x0, sxtw 2
	str	x0, [x5,8]
	umull	x0, w1, w10
	add	w1, w1, 1
	add	x0, x2, x0
	ldr	x2, [x6,x3]
	uxth	w1, w1
	str	x2, [x0,8]
	ldr	x2, [x5,8]
	str	x2, [x0,16]
	b	.L2187
.L2191:
	mov	w7, 24
	mov	w4, 4
.L2189:
	add	x1, x6, :lo12:.LANCHOR2
	ldr	w2, [x1,1040]
	cmp	w0, w2
	bcs	.L2192
	umull	x3, w0, w7
	ldr	x5, [x1,1104]
	ldr	x8, [x1,1072]
	add	x2, x5, x3
	str	wzr, [x2,16]
	ldrh	w2, [x1,828]
	mul	w2, w0, w2
	sdiv	w2, w2, w4
	add	x2, x8, x2, sxtw 2
	str	x2, [x5,x3]
	ldrh	w2, [x1,830]
	ldr	x5, [x1,1104]
	ldr	x1, [x1,1128]
	mul	w2, w0, w2
	add	x3, x5, x3
	add	w0, w0, 1
	sdiv	w2, w2, w4
	uxth	w0, w0
	add	x1, x1, x2, sxtw 2
	str	x1, [x3,8]
	b	.L2189
.L2192:
	ret
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	mov	w0, -1
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w2, [x19,840]
	strh	w0, [x20,4000]
	mov	w0, -1
	str	w0, [x20,4012]
	lsl	w2, w2, 1
	ldr	x0, [x19,1208]
	str	xzr, [x20,3992]
	str	wzr, [x20,4004]
	str	wzr, [x20,4008]
	str	wzr, [x19,652]
	strh	wzr, [x19,1392]
	bl	ftl_memset
	ldrh	w2, [x19,758]
	mov	w1, 0
	ldr	x0, [x19,1168]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19,758]
	mov	w1, 0
	ldr	x0, [x19,1152]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 3520
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x20, 3456
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	adrp	x2, .LANCHOR2
	mov	w3, 0
	add	x2, x2, :lo12:.LANCHOR2
	mov	w7, 56
	mov	w9, 24
	ldr	w8, [x2,1040]
	ldr	x5, [x2,1104]
.L2195:
	cmp	w3, w1
	bcs	.L2194
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L2200:
	cmp	w2, w8
	bcs	.L2197
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L2196
	str	wzr, [x10,16]
	b	.L2197
.L2196:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2200
.L2197:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2195
.L2194:
	ret
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	adrp	x2, .LANCHOR2
	mov	w7, 24
	add	x2, x2, :lo12:.LANCHOR2
	mov	w8, 1
	mov	w9, 56
	ldr	w5, [x2,1040]
	ldr	x6, [x2,1104]
	mov	w2, 0
.L2202:
	cmp	w2, w1
	bcs	.L2208
	mov	w3, 0
.L2206:
	cmp	w3, w5
	bcs	.L2204
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L2203
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L2204
.L2203:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2206
.L2204:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2202
.L2208:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR4+4016
	uxth	w0, w0
	ldrh	w2, [x1,#:lo12:.LANCHOR4+4016]
	adrp	x1, .LANCHOR2+976
	ldr	x3, [x1,#:lo12:.LANCHOR2+976]
	mov	x1, 0
.L2210:
	cmp	w2, w1, uxth
	bls	.L2214
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L2210
	mov	w0, 1
	b	.L2211
.L2214:
	mov	w0, 0
.L2211:
	ret
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	mov	w19, w0
	lsr	x0, x19, 10
	str	x1, [x29,32]
	str	x2, [x29,40]
	bl	P2V_block_in_plane
	adrp	x3, .LANCHOR4
	ldr	x2, [x29,40]
	add	x4, x3, :lo12:.LANCHOR4
	ldr	x1, [x29,32]
	uxth	w9, w0
	mov	x6, 0
	ldrh	w8, [x4,4016]
	adrp	x4, .LANCHOR2
	add	x5, x4, :lo12:.LANCHOR2
	ldr	x7, [x5,976]
.L2216:
	uxth	w5, w6
	cmp	w5, w8
	bcs	.L2220
	add	x6, x6, 1
	add	x10, x7, x6, lsl 1
	ldrh	w10, [x10,-2]
	cmp	w10, w9
	bne	.L2216
.L2220:
	cmp	w5, w8
	bne	.L2218
	ubfiz	x5, x5, 1, 16
	strh	w0, [x7,x5]
	add	x0, x3, :lo12:.LANCHOR4
	ldrh	w5, [x0,4016]
	add	w5, w5, 1
	strh	w5, [x0,4016]
.L2218:
	add	x3, x3, :lo12:.LANCHOR4
	add	x4, x4, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w5, [x3,4018]
	ldr	x6, [x4,984]
	umull	x5, w5, w0
	add	x6, x6, x5
	str	w1, [x6,4]
	ldr	x1, [x4,984]
	add	x0, x1, x5
	str	w2, [x0,8]
	str	w19, [x1,x5]
	ldrh	w0, [x3,4018]
	add	w0, w0, 1
	strh	w0, [x3,4018]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4
	mov	w1, 255
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w2, [x19,822]
	strh	wzr, [x0,4016]
	strh	wzr, [x0,4018]
	lsl	w2, w2, 1
	ldr	x0, [x19,976]
	bl	ftl_memset
	ldrh	w3, [x19,822]
	mov	w2, 12
	ldr	x0, [x19,984]
	mov	w1, 255
	mul	w2, w3, w2
	bl	ftl_memset
	bl	FtlGcBufInit
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	stp	x29, x30, [sp, -160]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR1
	mov	x19, x0
	add	x0, x20, :lo12:.LANCHOR1
	str	w1, [x29,152]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x20, [x29,136]
	ldrh	w26, [x0,3164]
	mov	w0, 65535
	cmp	w26, w0
	beq	.L2245
	cbnz	w26, .L2224
	b	.L2225
.L2245:
	mov	w26, 0
.L2224:
	adrp	x0, .LANCHOR2+818
	ldr	w1, [x29,152]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+818]
	cmp	w1, w0
	bne	.L2226
.L2225:
	bl	FtlGcPageVarInit
.L2226:
	mov	w0, -1
	adrp	x25, .LANCHOR2
	str	w0, [x29,156]
	adrp	x0, .LC140
	add	x0, x0, :lo12:.LC140
	mov	w22, 0
	mov	w23, 65535
	mov	x21, x25
	mov	x24, x25
	str	x0, [x29,128]
.L2239:
	ldrh	w0, [x19]
	strb	wzr, [x19,8]
	cmp	w0, w23
	beq	.L2246
.L2228:
.L2242:
	add	x0, x25, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w20, 0
	ldrh	w5, [x0,748]
	ldrh	w8, [x0,830]
	add	x5, x19, x5, lsl 1
.L2229:
	cmp	x2, x5
	beq	.L2254
	ldrh	w1, [x2,16]
	cmp	w1, w23
	beq	.L2230
	add	x6, x21, :lo12:.LANCHOR2
	mov	w0, 56
	orr	w1, w26, w1, lsl 10
	mov	w3, 4
	umull	x0, w20, w0
	ldr	x7, [x6,992]
	add	w4, w20, 1
	add	x7, x7, x0
	str	w1, [x7,4]
	ldr	x1, [x6,992]
	ldr	x6, [x6,1128]
	add	x0, x1, x0
	mul	w1, w20, w8
	uxth	w20, w4
	str	xzr, [x0,8]
	sdiv	w1, w1, w3
	add	x1, x6, x1, sxtw 2
	str	x1, [x0,16]
.L2230:
	add	x2, x2, 2
	b	.L2229
.L2254:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 0
	mov	x27, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L2232:
	cmp	w20, w27, uxth
	bls	.L2255
	mov	x0, 56
	mul	x28, x27, x0
	add	x0, x21, :lo12:.LANCHOR2
	str	x0, [x29,144]
	ldr	x8, [x0,992]
	str	x8, [x29,104]
	add	x1, x8, x28
	str	x1, [x29,112]
	ldr	w2, [x1,4]
	str	x2, [x29,120]
	lsr	x0, x2, 10
	bl	P2V_plane
	uxth	w6, w0
	ldr	x8, [x29,104]
	ldr	x2, [x29,120]
	ldr	w0, [x8,x28]
	cbnz	w0, .L2233
	ldr	x1, [x29,112]
	add	x27, x27, 1
	ldr	x5, [x1,16]
	ldrh	w0, [x5]
	cmp	w0, w23
	bne	.L2234
	adrp	x0, .LANCHOR4+4008
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR4+4008]
	b	.L2227
.L2234:
	mov	w1, w2
	ldr	w0, [x5,12]
	ldr	w2, [x5,8]
	bl	FtlGcUpdatePage
	b	.L2232
.L2233:
	ldr	x0, [x29,128]
	ldrh	w1, [x19]
	bl	printk
	ldr	x0, [x29,144]
	ldr	w0, [x0,652]
	cbnz	w0, .L2235
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2236
.L2235:
	add	x0, x24, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,1168]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2237
.L2236:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	ldr	w0, [x0,x28]
	cmn	w0, #1
	bne	.L2238
.L2237:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	add	x5, x0, x28
	ldr	w0, [x5,4]
	str	w0, [x29,156]
.L2238:
	add	x0, x24, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	mov	w26, 0
	ldr	x0, [x0,1184]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	bl	FtlGcPageVarInit
	b	.L2239
.L2255:
	ldr	w0, [x29,152]
	add	w22, w22, 1
	add	w3, w26, 1
	cmp	w22, w0
	uxth	w26, w3
	bcs	.L2241
.L2243:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w26
	bhi	.L2242
	b	.L2246
.L2241:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3164]
	cmp	w0, w23
	beq	.L2243
	add	w0, w0, w22
	strh	w0, [x1,3164]
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w26
	bls	.L2243
	b	.L2244
.L2246:
	mov	w6, 0
.L2227:
	ldr	x0, [x29,136]
	mov	w1, w26
	strh	w26, [x19,2]
	mov	w2, w6
	add	x20, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w6, [x19,6]
	strh	w0, [x20,3164]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
.L2244:
	ldr	w0, [x29,156]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, .LC141
	mov	w1, w19
	add	x0, x0, :lo12:.LC141
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w5, [x2,948]
	cmp	w5, w19
	beq	.L2257
	ldrh	w4, [x2,950]
	cmp	w4, w19
	beq	.L2257
	ldrh	w3, [x2,952]
	cmp	w3, w19
	beq	.L2257
	ldrh	w1, [x2,954]
	cmp	w1, w19
	beq	.L2257
	mov	w0, 65535
	cmp	w5, w0
	bne	.L2258
	strh	w19, [x2,948]
	b	.L2257
.L2258:
	cmp	w4, w0
	bne	.L2259
	strh	w19, [x2,950]
	b	.L2257
.L2259:
	cmp	w3, w0
	bne	.L2260
	strh	w19, [x2,952]
	b	.L2257
.L2260:
	cmp	w1, w0
	bne	.L2257
	strh	w19, [x2,954]
.L2257:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	uxth	w19, w0
	adrp	x0, .LC141
	mov	w1, w19
	add	x0, x0, :lo12:.LC141
	bl	printk
	adrp	x2, .LANCHOR2
	mov	w0, 0
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w6, [x2,948]
	cmp	w6, w19
	beq	.L2262
	ldrh	w5, [x2,950]
	cmp	w5, w19
	beq	.L2262
	ldrh	w4, [x2,952]
	cmp	w4, w19
	beq	.L2262
	ldrh	w3, [x2,954]
	cmp	w3, w19
	beq	.L2262
	mov	w1, 65535
	cmp	w6, w1
	bne	.L2263
	strh	w19, [x2,948]
	b	.L2262
.L2263:
	cmp	w5, w1
	bne	.L2264
	strh	w19, [x2,950]
	b	.L2271
.L2264:
	cmp	w4, w1
	bne	.L2265
	strh	w19, [x2,952]
	b	.L2271
.L2265:
	cmp	w3, w1
	mov	w0, -1
	bne	.L2262
	strh	w19, [x2,954]
.L2271:
	mov	w0, 0
.L2262:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w20, w0
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	mov	w0, w20
	add	x22, x19, :lo12:.LANCHOR2
	str	x23, [sp,48]
	bl	P2V_block_in_plane
	uxth	w21, w0
	ldrh	w1, [x22,962]
	adrp	x0, .LC142
	mov	w2, w20
	add	x0, x0, :lo12:.LC142
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,652]
	cbz	w0, .L2273
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,1168]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L2273
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L2273:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 4024
	ldrh	w3, [x0,962]
	mov	x0, 0
.L2274:
	cmp	w3, w0, uxth
	bls	.L2280
	add	x0, x0, 1
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L2274
	b	.L2275
.L2280:
	cmp	w3, 15
	bhi	.L2275
	adrp	x1, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR4
	add	w0, w3, 1
	add	x1, x1, 4024
	strh	w0, [x19,962]
	strh	w20, [x1,w3,sxtw 1]
.L2275:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,962]
	cbz	w1, .L2282
	ldrh	w3, [x0,948]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2282
	ldrh	w2, [x0,966]
	cmp	w2, w1
	bcc	.L2283
	strh	wzr, [x0,966]
.L2283:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 4024
	ldrh	w1, [x19,966]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,966]
	add	w0, w0, 1
	strh	w0, [x19,966]
.L2282:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x27, [sp,80]
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	uxth	w23, w0
	ldrh	w0, [x19,962]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	cbz	w0, .L2289
	adrp	x24, .LANCHOR4
	adrp	x22, .LC143
	add	x22, x22, :lo12:.LC143
	add	x25, x24, :lo12:.LANCHOR4
.L2297:
	ldrh	w0, [x19,748]
	cmp	w0, w21
	bls	.L2298
	add	x0, x19, 776
	mov	w1, w23
	mov	w20, 0
	add	x27, x24, :lo12:.LANCHOR4
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w26, w0
.L2290:
	ldrh	w0, [x19,962]
	cmp	w0, w20
	bls	.L2299
	add	x0, x27, 4024
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w26
	bne	.L2291
	mov	w1, w26
	mov	x0, x22
	bl	printk
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,962]
	mov	w3, w20
.L2292:
	cmp	w3, w2
	bcs	.L2300
	add	x4, x25, 4024
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2292
.L2300:
	sub	w2, w2, #1
	strh	w2, [x19,962]
.L2291:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2290
.L2299:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2297
.L2298:
	bl	FtlGcReFreshBadBlk
.L2289:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	uxth	w19, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x2,1184]
	ldrh	w1, [x1,x0]
	cbnz	w1, .L2302
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w3, [x0,3408]
	cmp	w3, w19
	bne	.L2303
	mov	w1, -1
	strh	w1, [x0,3408]
	b	.L2304
.L2303:
	ldrh	w3, [x2,3608]
	mov	w0, w1
	cmp	w3, w19
	beq	.L2305
	ldrh	w3, [x2,3656]
	cmp	w3, w19
	beq	.L2305
	ldrh	w2, [x2,3704]
	cmp	w2, w19
	beq	.L2305
.L2304:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x20, 3568
	bl	List_remove_node
	ldrh	w0, [x20,3584]
	sub	w0, w0, #1
	strh	w0, [x20,3584]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2305
.L2302:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2305:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w0
	mov	w0, 65535
	stp	x21, x22, [sp,32]
	cmp	w19, w0
	beq	.L2310
	adrp	x20, .LANCHOR2
	ubfiz	x21, x19, 1, 16
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,1184]
	ldrh	w2, [x0,x21]
	cbnz	w2, .L2311
	adrp	x0, .LC144
	mov	w1, w19
	add	x0, x0, :lo12:.LC144
	add	x22, x20, 3592
	bl	printk
	ldr	x0, [x20,1184]
	mov	w1, 32
	strh	w1, [x0,x21]
	mov	x0, x22
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2312
	mov	w1, w19
	mov	x0, x22
	bl	List_remove_node
	ldrh	w0, [x20,3600]
	sub	w0, w0, #1
	strh	w0, [x20,3600]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,1184]
	adrp	x0, .LC145
	add	x0, x0, :lo12:.LC145
	mov	w1, w19
	ldrh	w2, [x2,x21]
	bl	printk
.L2312:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2319
.L2311:
	sub	w2, w2, #1
	strh	w2, [x0,x21]
.L2310:
	adrp	x20, .LANCHOR4
	mov	w0, 65535
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w1, [x20,4000]
	cmp	w1, w0
	bne	.L2314
	strh	w19, [x20,4000]
.L2319:
	mov	w0, 0
	b	.L2313
.L2314:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2313
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,4000]
.L2313:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -208]!
	mov	w2, 65535
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	ldrh	w0, [x0]
	cmp	w0, w2
	beq	.L2477
	ldrh	w0, [x19,2]
	adrp	x20, .LANCHOR2
	str	w0, [x29,188]
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w28, [x19,6]
	ldr	w1, [x29,188]
	ldrh	w0, [x0,818]
	cmp	w0, w1
	bne	.L2323
	strh	wzr, [x19,4]
	b	.L2483
.L2323:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2324:
	cmp	w0, w2
	bne	.L2486
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2324
.L2486:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2326
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2327
	adrp	x0, .LANCHOR0
	mov	w23, w21
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,136]
	cbnz	w1, .L2400
	add	x0, x0, 140
	ldrh	w23, [x0,w21,sxtw 1]
	b	.L2400
.L2326:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2327
	mov	w23, w0
.L2400:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w25, 0
	mov	w8, 65535
	mov	w9, 56
	mov	w10, 4
	ldrh	w4, [x0,748]
	ldrh	w7, [x0,830]
	add	x4, x19, x4, lsl 1
	b	.L2329
.L2327:
	strh	wzr, [x19,2]
.L2483:
	strb	wzr, [x19,6]
	b	.L2477
.L2331:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2330
	add	x5, x20, :lo12:.LANCHOR2
	orr	w1, w23, w1, lsl 10
	umull	x0, w25, w9
	add	w3, w25, 1
	ldr	x6, [x5,992]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,992]
	ldr	x5, [x5,1128]
	add	x0, x1, x0
	mul	w1, w25, w7
	uxth	w25, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2330:
	add	x2, x2, 2
.L2329:
	cmp	x2, x4
	bne	.L2331
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2332
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2332:
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,184]
	mov	w1, w25
	mov	x26, 0
	mov	w24, 65535
	adrp	x27, .LANCHOR4
	ldr	x0, [x22,992]
	bl	FlashReadPages
	ldr	w0, [x22,876]
	adrp	x22, .LC146
	str	w0, [x29,176]
	add	x22, x22, :lo12:.LC146
	uxth	w0, w23
	str	w0, [x29,168]
.L2333:
	uxth	w2, w26
	cmp	w2, w25
	bcs	.L2340
	add	x6, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x26, x0
	ldr	x4, [x6,992]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2334
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2335
	ldr	w1, [x6,876]
	mov	w0, w4
	str	x7, [x29,136]
	str	x2, [x29,144]
	str	x4, [x29,152]
	str	x6, [x29,160]
	bl	ftl_cmp_data_ver
	ldr	x6, [x29,160]
	ldr	x4, [x29,152]
	ldr	x2, [x29,144]
	ldr	x7, [x29,136]
	cbz	w0, .L2335
	add	w4, w4, 1
	str	w4, [x6,876]
.L2335:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2336
.L2340:
	cmp	w2, w25
	bne	.L2478
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,992]
	ldr	w0, [x0,4]
	b	.L2479
.L2334:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w24, [x29,168]
	add	x0, x27, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,4058]
.L2336:
	add	x26, x26, 1
	b	.L2333
.L2478:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,992]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2479:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2342
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,136]
	cbnz	w2, .L2342
	add	x0, x0, 140
	ldrh	w21, [x0,w21,sxtw 1]
.L2342:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w21
	bne	.L2343
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2343:
	uxth	w0, w28
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2344
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2484
.L2344:
	ldr	w0, [x29,176]
	adrp	x25, .LANCHOR4
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w24, w0
	bne	.L2345
	cbnz	w1, .L2346
.L2345:
	add	x0, x25, :lo12:.LANCHOR4
	uxth	w6, w23
	ldr	w1, [x0,4012]
	cmn	w1, #1
	bne	.L2347
	str	w22, [x0,4012]
.L2347:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w24, [x29,188]
	ldr	w3, [x0,4012]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2348
	sub	w24, w6, #7
	uxth	w24, w24
.L2348:
	adrp	x7, .LANCHOR0
	mov	w28, -1
	add	x5, x7, :lo12:.LANCHOR0
	mov	w27, w28
	mov	w4, 65535
	add	x5, x5, 652
.L2349:
	cmp	w24, w6
	bhi	.L2362
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, 0
	mov	w10, 56
	ldrh	w2, [x0,748]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2363:
	cmp	x0, x2
	beq	.L2487
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2350
	add	x9, x20, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x8, w23, w10
	add	w23, w23, 1
	ldr	x9, [x9,992]
	uxth	w23, w23
	add	x8, x9, x8
	str	w1, [x8,4]
.L2350:
	add	x0, x0, 2
	b	.L2363
.L2487:
	add	x8, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,184]
	mov	w1, w23
	str	x5, [x29,128]
	str	x7, [x29,136]
	ldr	x0, [x8,992]
	str	x4, [x29,144]
	str	x3, [x29,160]
	str	x6, [x29,168]
	str	x8, [x29,176]
	bl	FlashReadPages
	ldr	x7, [x29,136]
	mov	w1, 0
	ldr	x8, [x29,176]
	sxtw	x10, w24
	add	x0, x7, :lo12:.LANCHOR0
	ldr	x6, [x29,168]
	ldr	x3, [x29,160]
	ldr	x4, [x29,144]
	ldrb	w9, [x0,136]
	ldr	x0, [x8,992]
	ldr	x5, [x29,128]
	add	x0, x0, 16
.L2352:
	cmp	w1, w23
	beq	.L2488
	ldr	w2, [x0,-16]
	cbnz	w2, .L2353
	ldr	x2, [x0]
	ldrh	w8, [x2]
	cmp	w8, w4
	beq	.L2354
	ldr	w8, [x2,4]
	cmn	w8, #1
	beq	.L2354
	add	x2, x25, :lo12:.LANCHOR4
	cmn	w28, #1
	ldr	w27, [x2,4012]
	str	w8, [x2,4012]
	bne	.L2354
	ldrh	w2, [x5,x10,lsl 1]
	cmp	w2, w4
	bne	.L2355
	cbz	w9, .L2354
.L2355:
	cmp	w27, w22
	csinv	w28, w27, wzr, ne
	b	.L2354
.L2353:
	add	x0, x25, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,4058]
	ldrb	w1, [x19,8]
	cbnz	w1, .L2346
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 652
	ldrh	w2, [x1,w24,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2357
	cmn	w28, #1
	beq	.L2358
	str	w28, [x0,4012]
	b	.L2346
.L2358:
	cmp	w3, w22
	beq	.L2359
	str	w3, [x0,4012]
	b	.L2346
.L2359:
	ldr	w1, [x0,4012]
	b	.L2485
.L2357:
	cmp	w27, w22
	beq	.L2360
	cmn	w27, #1
	beq	.L2346
	str	w27, [x0,4012]
	b	.L2346
.L2360:
	ldr	w1, [x0,4012]
	cmp	w1, w22
	beq	.L2346
.L2485:
	sub	w1, w1, #1
	b	.L2480
.L2354:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2352
.L2488:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2349
.L2362:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, -1
.L2480:
	str	w1, [x0,4012]
.L2346:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, 1
	ldr	w23, [x29,188]
	strh	w1, [x0,4060]
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	str	x0, [x29,128]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	str	x0, [x29,120]
.L2364:
	add	x0, x20, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,748]
	ldrb	w6, [x1,136]
	mov	x1, x19
	add	x3, x19, x3, lsl 1
.L2365:
	cmp	x1, x3
	beq	.L2489
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2366
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w23, w2, lsl 10
	ldr	x4, [x5,992]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2367
	cbz	w6, .L2367
	ldr	x2, [x5,992]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2367:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2366:
	add	x1, x1, 2
	b	.L2365
.L2489:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,992]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2369:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2490
	ldr	x0, [x29,160]
	mov	x24, 56
	mul	x24, x0, x24
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x28, [x0,992]
	add	x27, x28, x24
	ldr	w1, [x27,4]
	str	w1, [x29,204]
	lsr	x0, x1, 10
	str	x1, [x29,168]
	bl	P2V_plane
	uxth	w0, w0
	ldr	w1, [x29,188]
	cmp	w23, w1
	ldr	x1, [x29,168]
	bcc	.L2370
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2404
	ldr	w2, [x29,188]
	cmp	w23, w2
	beq	.L2370
.L2404:
	cmp	w0, w26
	bne	.L2405
	cmp	w23, w21
	beq	.L2372
.L2405:
	ldr	w0, [x28,x24]
	cmn	w0, #1
	beq	.L2374
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2375
	ldrh	w0, [x19]
	b	.L2481
.L2375:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2376
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,876]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2376
	add	w0, w22, 1
	str	w0, [x27,876]
.L2376:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x0, x25, :lo12:.LANCHOR4
	str	x0, [x29,136]
	ldr	w1, [x0,4012]
	cmn	w1, #1
	beq	.L2377
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2377
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2378
	add	x3, x20, :lo12:.LANCHOR2
	str	x3, [x29,112]
	ldr	x0, [x3,992]
	add	x0, x0, x24
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x2, [x0,16]
	ldr	x0, [x3,992]
	str	x2, [x29,168]
	mov	w2, 0
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,992]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x24
	ldr	w0, [x2,x24]
	cmn	w0, #1
	bne	.L2379
	b	.L2380
.L2378:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2370
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2370
.L2379:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2380
	ldr	x0, [x29,136]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	ldr	w0, [x0,4012]
	str	x3, [x29,104]
	str	x4, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,112]
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2380
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2385
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2380
	cmn	w0, #1
	beq	.L2383
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,992]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2384
.L2383:
	str	w0, [x2,x24]
.L2384:
	adrp	x0, .LANCHOR2+992
	ldr	x0, [x0,#:lo12:.LANCHOR2+992]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2385
	ldr	x0, [x29,168]
	ldr	w24, [x0,4]
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, w24
	ldr	w0, [x0,4012]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2385
	ldr	w0, [x29,144]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2380
.L2385:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2380:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2387
.L2377:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2387
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w24, [x29,200]
	cmn	w24, #1
	beq	.L2387
	ldr	w0, [x29,196]
	cmp	w24, w0
	beq	.L2387
	lsr	x0, x24, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w2, [x1,3608]
	cmp	w2, w0
	beq	.L2390
	ldrh	w2, [x1,3656]
	cmp	w2, w0
	beq	.L2390
	ldrh	w1, [x1,3704]
	cmp	w1, w0
	bne	.L2387
.L2390:
	add	x28, x20, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, 0
	ldr	x0, [x28,992]
	str	w24, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x28,992]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x28,992]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2387
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2387
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2387:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2370
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,1184]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2391
.L2481:
	bl	decrement_vpc_count
	b	.L2370
.L2391:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2370
.L2374:
	add	x24, x25, :lo12:.LANCHOR4
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x24,4058]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x24,4064]
	cmp	w0, 31
	bhi	.L2392
	add	x1, x24, 4072
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x24,4064]
.L2392:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x0,4012]
	cmn	w1, #1
	beq	.L2482
	cmp	w1, w22
	bls	.L2370
.L2482:
	str	w22, [x0,4012]
.L2370:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2369
.L2490:
	ldrb	w0, [x19,8]
	add	w23, w23, 1
	cmp	w0, 1
	uxth	w23, w23
	bne	.L2395
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2395
	cmp	w21, w23
	bne	.L2395
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,820]
	cmp	w0, w23
	beq	.L2372
.L2395:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,818]
	cmp	w23, w1
	bne	.L2364
	ldrh	w2, [x0,748]
	mov	w3, 65535
	strh	w23, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2397:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2477
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2397
	strb	w1, [x19,6]
	b	.L2477
.L2372:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2484:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2477:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 208
	ret
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	ldrh	w1, [x0,4]
	cbz	w1, .L2491
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2491
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	adrp	x23, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x21, 652
	ldrh	w0, [x19,x0,lsl 1]
.L2495:
	cmp	w0, w20
	bne	.L2502
.L2497:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,748]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2496
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2496:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2495
.L2502:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2498
	ldrb	w0, [x21,136]
	cbnz	w0, .L2498
	ldrh	w0, [x19,2]
	ldrh	w0, [x22,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2498
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2497
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2491
.L2498:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2491
	cmp	w1, 1
	bne	.L2491
	adrp	x0, .LANCHOR2
	ldrh	w2, [x19,2]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,820]
	cmp	w2, w1
	bcc	.L2491
	ldrh	w1, [x19]
	ldr	x3, [x0,1184]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,818]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2491:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	str	x25, [sp,64]
	mov	x19, x0
	strb	wzr, [x0,10]
	adrp	x20, .LANCHOR0
	ldrb	w0, [x0,6]
	add	x20, x20, :lo12:.LANCHOR0
	mov	w22, 65535
	adrp	x23, .LANCHOR2
	add	x0, x0, 8
	add	x24, x20, 652
	ldrh	w0, [x19,x0,lsl 1]
.L2504:
	cmp	w0, w22
	adrp	x21, .LANCHOR2
	bne	.L2519
.L2505:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,748]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2506
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2506:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2504
.L2519:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2507
	ldrb	w1, [x20,136]
	cbnz	w1, .L2507
	ldrh	w1, [x19,2]
	ldrh	w1, [x24,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2507
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2505
.L2507:
	ldrh	w1, [x19,4]
	adrp	x20, .LANCHOR0
	ldrh	w22, [x19,2]
	mov	w23, 65535
	add	x20, x20, :lo12:.LANCHOR0
	sub	w1, w1, #1
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	mov	w24, w23
	add	x25, x20, 652
.L2508:
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,748]
.L2510:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2509
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2509:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2510
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2515
	ldrb	w0, [x20,136]
	cbnz	w0, .L2512
	ldrh	w0, [x19,2]
	ldrh	w0, [x25,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2512
	ldrh	w1, [x19,4]
	cbz	w1, .L2512
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2508
.L2512:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w0, [x1,136]
	cbz	w0, .L2515
	add	x2, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x2,820]
	cmp	w1, w0
	bcc	.L2515
	ldrh	w1, [x19]
	ldr	x0, [x2,1184]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w3, [x0,x1]
	sub	w3, w3, w4
	strh	w3, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x2,818]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2515:
	mov	w0, w22
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x25, [sp,64]
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x20,1400]
	cbnz	w0, .L2520
	ldrh	w2, [x20,3612]
	cbz	w2, .L2522
	ldrb	w1, [x20,3616]
	cbnz	w1, .L2522
	ldrb	w1, [x20,3615]
	ldrh	w3, [x20,818]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2522
	ldrb	w23, [x20,3618]
	ldr	w22, [x20,1396]
	ldrh	w24, [x20,748]
	cbnz	w23, .L2520
	sub	w22, w22, #1
	add	x1, x29, 84
	mov	w0, w22
	mov	w2, w23
	bl	log2phys
	ldr	x21, [x20,1112]
	ldr	w0, [x29,84]
	ldr	x1, [x20,1048]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w22, [x29,112]
	str	x1, [x29,96]
	str	x21, [x29,104]
	str	w23, [x21,4]
	beq	.L2524
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w23
	bl	FlashReadPages
	b	.L2525
.L2524:
	ldr	x0, [x20,1048]
	mov	w1, 255
	ldrh	w2, [x20,828]
	bl	ftl_memset
.L2525:
	add	x25, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w24, w24, 2
	strh	w0, [x21]
	mov	w23, 0
	add	x20, x25, 3608
.L2526:
	cmp	w23, w24
	beq	.L2527
	ldrh	w0, [x20,4]
	cbz	w0, .L2527
	ldr	w0, [x29,92]
	add	w23, w23, 1
	str	w0, [x21,12]
	ldrh	w0, [x20]
	strh	w0, [x21,2]
	mov	x0, x20
	str	w22, [x21,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x25,876]
	mov	w2, 0
	str	w1, [x21,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x25,876]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2526
.L2527:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,3618]
	b	.L2520
.L2522:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,3618]
.L2520:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 144
	ret
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, 0
	adrp	x20, .LANCHOR2
	bl	FtlWriteDumpData
.L2538:
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w0, [x1,858]
	cmp	w0, w19
	bls	.L2541
	ldr	x1, [x1,1264]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2539
	mov	w0, w19
	bl	flush_l2p_region
.L2539:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2538
.L2541:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	str	x23, [sp,48]
	add	x1, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w1, [x1,1400]
	cbnz	w1, .L2542
	adrp	x2, .LANCHOR0+136
	ldrb	w21, [x2,#:lo12:.LANCHOR0+136]
	cbz	w21, .L2557
	ldrb	w21, [x0,8]
	cmp	w21, 1
	bne	.L2553
	ldrh	w23, [x0,4]
	b	.L2544
.L2553:
	mov	w21, w1
.L2557:
	mov	w23, 12
.L2544:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x22, [x0,1112]
	ldr	x1, [x0,1048]
	str	x1, [x29,80]
	mov	w1, -3
	str	x22, [x29,88]
	str	w1, [x22,8]
	mov	w1, -2
	str	w1, [x22,12]
	ldrh	w1, [x19]
	strh	w1, [x22,2]
	strh	wzr, [x22]
	ldr	x1, [x0,1048]
	str	w2, [x1]
	ldr	x1, [x0,1048]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2545:
	sub	w23, w23, #1
	cmn	w23, #1
	beq	.L2548
	ldrh	w0, [x19,4]
	cbnz	w0, .L2546
.L2548:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	ldr	x2, [x20,1184]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x20,818]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2542
.L2546:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2548
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w3, 0
	ldr	w0, [x1,876]
	str	w0, [x22,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,876]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2545
.L2542:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC47
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 72
	add	x0, x0, :lo12:.LC47
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR2
	str	x23, [sp,48]
	bl	printk
	mov	w20, 0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 0
	mov	x21, x19
	ldrh	w2, [x0,758]
	ldr	x0, [x0,1176]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2559:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w0, [x19,1396]
	cmp	w20, w0
	bcs	.L2575
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2560
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x19,1176]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2560:
	add	w20, w20, 1
	b	.L2559
.L2575:
	adrp	x22, .LC149
	mov	w20, 0
	mov	w23, 65535
	add	x22, x22, :lo12:.LC149
.L2562:
	ldrh	w0, [x19,756]
	cmp	w0, w20
	bls	.L2576
	ubfiz	x21, x20, 1, 16
	ldr	x0, [x19,1184]
	ldrh	w2, [x0,x21]
	ldr	x0, [x19,1176]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2565
	cmp	w2, w23
	beq	.L2565
	mov	x0, x22
	mov	w1, w20
	bl	printk
	ldrh	w0, [x19,3608]
	cmp	w0, w20
	beq	.L2565
	ldrh	w0, [x19,3704]
	cmp	w0, w20
	beq	.L2565
	ldrh	w0, [x19,3656]
	cmp	w0, w20
	beq	.L2565
	ldr	x0, [x19,1184]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2567
	ldr	x1, [x19,1176]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2565
.L2567:
	ldr	x1, [x19,1176]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w20
	bl	update_vpc_list
.L2565:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2562
.L2576:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
	ret
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w1, [x2,1400]
	ldrh	w22, [x0]
	cbnz	w1, .L2578
	mov	x21, x0
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2579
	ubfiz	x1, x22, 1, 16
	ldr	x0, [x2,1184]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2580
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L2579
.L2580:
	mov	w0, w22
	bl	INSERT_FREE_LIST
.L2579:
	add	x1, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x1, 3656
	cmp	x21, x0
	beq	.L2581
	ldrh	w2, [x1,768]
	cmp	w2, 1
	beq	.L2581
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2582
.L2581:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2583
.L2582:
	add	x0, x1, 3608
	cmp	x21, x0
	bne	.L2583
	cmp	w2, 3
	beq	.L2585
	ldr	w0, [x1,868]
	cmp	w0, 1
	bne	.L2586
.L2585:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x1,3616]
.L2586:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,652]
	cbz	w0, .L2583
	ldr	w0, [x1,920]
	cmp	w0, 39
	bhi	.L2583
	mov	w0, 1
	strb	w0, [x1,3616]
.L2583:
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x20, x1
	ldrh	w2, [x0,4000]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2588
	cmp	w22, w2
	bne	.L2589
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x1, x2, 1, 16
	ldr	x0, [x19,1184]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2590
.L2589:
	mov	w0, w2
	bl	update_vpc_list
.L2590:
	add	x1, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x1,4000]
.L2588:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2578:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LANCHOR4
	add	x2, x1, :lo12:.LANCHOR4
	add	x29, sp, 0
	add	x0, x2, 3456
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR2
	ldr	w3, [x0,80]
	cbz	w3, .L2604
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w4, [x0,84]
	ldr	w3, [x1,1396]
	cmp	w4, w3
	bcs	.L2605
	mov	w21, 2048
	mov	x20, x2
.L2610:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x20,3540]
	ldr	w1, [x1,1396]
	cmp	w0, w1
	bcs	.L2607
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x20,3540]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x20,3540]
	cmn	w1, #1
	beq	.L2608
	str	w1, [x29,60]
	mov	w2, 0
	str	w0, [x29,80]
	mov	w1, 1
	add	x0, x29, 56
	str	xzr, [x29,64]
	str	xzr, [x29,72]
	str	wzr, [x29,56]
	bl	FlashReadPages
	ldr	w0, [x29,56]
	cmp	w0, 256
	bne	.L2607
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2607
.L2608:
	subs	w21, w21, #1
	bne	.L2610
.L2607:
	mov	w0, -1
	b	.L2612
.L2605:
	ldr	w1, [x1,900]
	str	wzr, [x0,80]
	str	wzr, [x0,84]
	str	w1, [x0,76]
	b	.L2618
.L2604:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w0, [x2,916]
	ldr	w3, [x2,652]
	adrp	x2, .LANCHOR0+136
	ldrb	w4, [x2,#:lo12:.LANCHOR0+136]
	mov	w2, 4194304
	cbnz	w4, .L2613
	add	w0, w3, w0, lsr 10
	mov	w3, 33554432
	asr	w2, w3, w0
.L2613:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x1, :lo12:.LANCHOR4
	ldr	w3, [x19,900]
	ldr	w0, [x0,3532]
	add	w4, w3, 1048576
	cmp	w0, w4
	bhi	.L2614
	add	w0, w2, w0
	cmp	w0, w3
	bcc	.L2614
	ldrb	w2, [x19,3548]
	mov	w0, 0
	cbnz	w2, .L2612
.L2614:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w1, 1
	str	wzr, [x0,3540]
	str	w1, [x0,3536]
	str	w3, [x0,3532]
.L2618:
	mov	w0, 0
.L2612:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	stp	x29, x30, [sp, -160]!
	mov	w2, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x3, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w4, [x3,1400]
	cbnz	w4, .L2764
	ldr	w4, [x3,924]
	cbnz	w4, .L2764
	ldrh	w4, [x3,3584]
	cmp	w4, 47
	bls	.L2764
	str	w0, [x29,140]
	adrp	x0, .LANCHOR1+3164
	mov	w24, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3164]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2621
.L2624:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,950]
	cmp	w4, w2
	bne	.L2622
	b	.L2623
.L2621:
	ldrh	w1, [x3,3704]
	cmp	w1, w0
	beq	.L2624
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2624
	b	.L2764
.L2622:
	ldrh	w1, [x0,948]
	cmp	w1, w2
	bne	.L2623
	ldrh	w3, [x0,952]
	cmp	w3, w1
	beq	.L2623
	ldrh	w2, [x0,954]
	cmp	w2, w1
	beq	.L2623
	mov	w1, -1
	strh	w4, [x0,948]
	strh	w3, [x0,950]
	strh	w2, [x0,952]
	strh	w1, [x0,954]
.L2623:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,140]
	adrp	x23, .LANCHOR4
	cmp	w2, 1
	ldr	w0, [x1,940]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,940]
	bne	.L2635
	ldr	w1, [x1,652]
	cbnz	w1, .L2626
	adrp	x1, .LANCHOR0+136
	ldrb	w1, [x1,#:lo12:.LANCHOR0+136]
	cbz	w1, .L2635
.L2626:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,920]
	cmp	w1, 39
	bhi	.L2635
	adrp	x21, .LANCHOR5
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w1, [x1,-152]
	add	w0, w0, w1
	str	w0, [x20,940]
	bl	FtlGcReFreshBadBlk
	add	x0, x23, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x0,3408]
	cmp	w0, w1
	bne	.L2635
	ldrh	w1, [x20,948]
	cmp	w1, w0
	bne	.L2635
	ldr	w0, [x20,940]
	cmp	w0, 1024
	bhi	.L2627
	ldrh	w0, [x20,3600]
	cmp	w0, 63
	bhi	.L2635
.L2627:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR5
	ldrh	w2, [x0,958]
	ldrh	w3, [x0,3600]
	add	w2, w2, 64
	strh	wzr, [x1,-152]
	cmp	w3, w2
	bgt	.L2635
	str	wzr, [x0,940]
	ldr	w0, [x0,920]
	cbnz	w0, .L2628
	mov	w0, 6
	b	.L2786
.L2628:
	cmp	w0, 5
	bhi	.L2629
	mov	w0, 18
.L2786:
	strh	w0, [x1,-152]
.L2629:
	mov	w0, 32
	mov	w28, 65535
	bl	List_get_gc_head_node
	uxth	w26, w0
	cmp	w26, w28
	beq	.L2634
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,960]
	cbz	w0, .L2631
	ldrh	w1, [x22,820]
	ubfiz	x26, x26, 1, 16
	ldrh	w3, [x22,748]
	ldr	x27, [x22,1184]
	mul	w1, w1, w3
	ldrh	w2, [x27,x26]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2634
	add	w1, w0, 1
	str	wzr, [x22,928]
	uxth	w1, w1
	strh	w1, [x22,960]
	str	x1, [x29,128]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w28
	ldr	x1, [x29,128]
	beq	.L2634
	ubfiz	x28, x20, 1, 16
	adrp	x0, .LC150
	ldrh	w4, [x27,x26]
	add	x0, x0, :lo12:.LC150
	mov	w2, w20
	ldrh	w3, [x27,x28]
	bl	printk
	ldrh	w0, [x22,960]
	cmp	w0, 40
	bls	.L2632
	ldr	x0, [x22,1184]
	ldrh	w0, [x0,x28]
	cmp	w0, 32
	bls	.L2632
	strh	wzr, [x22,960]
.L2632:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, 6
	strh	w0, [x21,-152]
	b	.L2636
.L2631:
	mov	w0, 1
	strh	w0, [x22,960]
.L2634:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2636
	add	x21, x21, :lo12:.LANCHOR5
	strh	wzr, [x21,-152]
.L2635:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w20, [x0,3408]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2791
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,3704]
	cmp	w0, w20
	beq	.L2792
.L2636:
	mov	w0, 65535
	cmp	w20, w0
	cset	w2, eq
	cbz	w2, .L2649
	ldr	w0, [x29,140]
	cbnz	w0, .L2649
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 1
	ldrh	w1, [x0,3600]
	cmp	w1, 24
	bhi	.L2650
	cmp	w1, 16
	ldrh	w22, [x0,818]
	bls	.L2651
	lsr	w22, w22, 5
	b	.L2650
.L2651:
	cmp	w1, 12
	bls	.L2652
	lsr	w22, w22, 4
	b	.L2650
.L2652:
	lsr	w0, w22, 2
	cmp	w1, 9
	csel	w22, w0, w22, cs
.L2650:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,956]
	cmp	w2, w1
	bcs	.L2654
	ldrh	w1, [x0,3704]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2655
	ldrh	w2, [x0,948]
	cmp	w2, w1
	bne	.L2655
	adrp	x1, .LANCHOR5-152
	ldrh	w2, [x1,#:lo12:.LANCHOR5-152]
	cbnz	w2, .L2656
	ldr	w1, [x0,1396]
	ldr	w3, [x0,3892]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2657
.L2656:
	add	x23, x23, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,3404]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,956]
	b	.L2658
.L2657:
	mov	w1, 18
	strh	w1, [x0,956]
.L2658:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,928]
	b	.L2764
.L2655:
	add	x0, x23, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3404]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,956]
.L2654:
	cmp	w24, 2
	bhi	.L2719
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,652]
	cbz	w0, .L2719
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2719
.L2649:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w3, 65535
	ldrh	w1, [x0,3704]
	cmp	w1, w3
	bne	.L2661
	cbz	w2, .L2661
	ldrh	w2, [x0,948]
	cmp	w2, w1
	bne	.L2661
	add	x2, x23, :lo12:.LANCHOR4
	ldrh	w2, [x2,3408]
	cmp	w2, w1
	beq	.L2662
.L2667:
	mov	w20, 65535
	b	.L2661
.L2662:
	ldrh	w2, [x0,3600]
	adrp	x20, .LANCHOR5
	ldrh	w1, [x0,956]
	str	wzr, [x0,928]
	cmp	w2, w1
	bls	.L2663
	add	x1, x20, :lo12:.LANCHOR5
	ldrh	w1, [x1,-152]
	cbnz	w1, .L2664
	ldr	w1, [x0,1396]
	ldr	w2, [x0,3892]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2665
.L2664:
	add	x0, x23, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3404]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,956]
	b	.L2666
.L2665:
	mov	w1, 18
	strh	w1, [x0,956]
.L2666:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,1184]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2663
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x20,-152]
	b	.L2764
.L2663:
	add	x22, x20, :lo12:.LANCHOR5
	ldrh	w0, [x22,-152]
	cbnz	w0, .L2667
	add	x1, x23, :lo12:.LANCHOR4
	add	x21, x19, :lo12:.LANCHOR2
	ldrh	w20, [x1,3404]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x21,956]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x21,1184]
	ldrh	w2, [x21,748]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x21,820]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L2668
	ldrh	w0, [x21,3600]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L2668
	bl	FtlReadRefresh
	ldrh	w2, [x22,-152]
	b	.L2764
.L2668:
	cbnz	w1, .L2667
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,3600]
	add	w2, w2, 1
	b	.L2764
.L2661:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 2
	ldr	w0, [x0,652]
	cmp	w0, wzr
	csinc	w22, w22, wzr, ne
	b	.L2660
.L2719:
	mov	w20, 65535
.L2660:
	add	x0, x23, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w1, [x0,3408]
	cmp	w1, w2
	bne	.L2670
	cmp	w20, w1
	beq	.L2671
	strh	w20, [x0,3408]
	b	.L2672
.L2671:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,948]
	cmp	w0, w20
	beq	.L2672
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1,1184]
	ldrh	w0, [x2,x0]
	cbnz	w0, .L2673
	mov	w0, -1
	strh	w0, [x1,948]
.L2673:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w2, [x1,948]
	strh	w2, [x0,3408]
	mov	w0, -1
	strh	w0, [x1,948]
.L2672:
	add	x21, x23, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x21,3408]
	strb	wzr, [x21,3416]
	cmp	w0, w1
	beq	.L2670
	bl	IsBlkInGcList
	cbz	w0, .L2675
	mov	w0, -1
	strh	w0, [x21,3408]
.L2675:
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2676
	add	x0, x23, :lo12:.LANCHOR4
	add	x21, x0, 3408
	ldrh	w0, [x0,3408]
	bl	ftl_get_blk_mode
	strb	w0, [x21,8]
.L2676:
	add	x21, x23, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x24, x21, 3408
	ldrh	w1, [x21,3408]
	cmp	w1, w0
	beq	.L2670
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x21,3410]
	adrp	x0, .LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x21,3408]
	strb	wzr, [x21,3414]
	ldr	x1, [x1,1184]
	strh	wzr, [x0,-150]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,-148]
.L2670:
	add	x21, x23, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x21,3408]
	ldrh	w2, [x1,3608]
	cmp	w2, w0
	beq	.L2677
	ldrh	w1, [x1,3656]
	cmp	w1, w0
	beq	.L2677
.L2678:
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	str	x0, [x29,128]
	b	.L2679
.L2677:
	add	x23, x23, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x23,3408]
	b	.L2790
.L2700:
	ldrh	w0, [x21,3410]
	add	w22, w22, w0
	uxth	w22, w22
	strh	w22, [x21,3410]
	cmp	w22, w26
	bcs	.L2793
.L2702:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3600]
	cmp	w0, 2
	bhi	.L2705
	ldrh	w22, [x1,818]
.L2679:
	ldrh	w26, [x21,3408]
	mov	w0, 65535
	cmp	w26, w0
	bne	.L2680
	add	x0, x19, :lo12:.LANCHOR2
	mov	w27, 2
	str	wzr, [x0,928]
.L2681:
	add	x28, x19, :lo12:.LANCHOR2
	ldrh	w24, [x28,960]
	mov	w0, w24
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w26
	strh	w25, [x21,3408]
	bne	.L2682
	strh	wzr, [x28,960]
	mov	w2, 8
	b	.L2764
.L2682:
	mov	w0, w25
	add	w24, w24, 1
	bl	IsBlkInGcList
	cbz	w0, .L2683
	strh	w24, [x28,960]
	b	.L2681
.L2683:
	ldrh	w5, [x28,748]
	ubfiz	x2, x25, 1, 16
	ldrh	w0, [x28,818]
	uxth	w24, w24
	ldr	x3, [x28,1184]
	strh	w24, [x28,960]
	mul	w0, w0, w5
	ldrh	w4, [x3,x2]
	sdiv	w5, w0, w27
	cmp	w4, w5
	bgt	.L2685
	cmp	w4, 8
	bls	.L2686
	cmp	w24, 48
	bls	.L2686
	ldrh	w4, [x21,4016]
	cmp	w4, 35
	bhi	.L2686
.L2685:
	add	x4, x19, :lo12:.LANCHOR2
	strh	wzr, [x4,960]
.L2686:
	ldrh	w2, [x3,x2]
	cmp	w2, w0
	blt	.L2687
	cmp	w20, w26
	bne	.L2687
	add	x23, x23, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,960]
	strh	w0, [x23,3408]
.L2790:
	adrp	x0, .LANCHOR5-152
	ldrh	w2, [x0,#:lo12:.LANCHOR5-152]
	b	.L2764
.L2687:
	cbnz	w2, .L2688
	mov	w0, -1
	bl	decrement_vpc_count
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,960]
	add	w0, w0, 1
	strh	w0, [x1,960]
	b	.L2681
.L2688:
	adrp	x0, .LANCHOR0
	strb	wzr, [x21,3416]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2689
	mov	w0, w25
	bl	ftl_get_blk_mode
	strb	w0, [x21,3416]
.L2689:
	add	x24, x21, 3408
	mov	x0, x24
	bl	make_superblock
	adrp	x1, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR5
	ldrh	w2, [x21,3408]
	ldr	x0, [x0,1184]
	strh	wzr, [x1,-150]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,-148]
	strh	wzr, [x21,3410]
	strb	wzr, [x21,3414]
.L2680:
	ldr	w0, [x29,140]
	cmp	w0, 1
	bne	.L2690
	bl	FtlReadRefresh
.L2690:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	str	w1, [x0,924]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w26, [x0,818]
	ldrb	w1, [x1,136]
	cbz	w1, .L2691
	ldrb	w1, [x21,3416]
	cmp	w1, 1
	bne	.L2691
	ldrh	w26, [x0,820]
.L2691:
	ldrh	w0, [x21,3410]
	add	w1, w0, w22
	cmp	w1, w26
	ble	.L2692
	sub	w22, w26, w0
	uxth	w22, w22
.L2692:
	mov	w27, 0
.L2693:
	cmp	w22, w27, uxth
	bls	.L2700
	add	x1, x21, 3408
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w3, [x1,2]
	ldrh	w6, [x0,748]
	mov	x0, 0
	add	w3, w3, w27
	mov	w24, w0
.L2701:
	cmp	w6, w0, uxth
	bls	.L2794
	add	x2, x1, x0, lsl 1
	mov	w4, 65535
	ldrh	w2, [x2,16]
	cmp	w2, w4
	beq	.L2694
	add	x5, x19, :lo12:.LANCHOR2
	mov	w4, 56
	orr	w2, w3, w2, lsl 10
	umull	x4, w24, w4
	ldr	x5, [x5,1024]
	add	w24, w24, 1
	add	x4, x5, x4
	uxth	w24, w24
	str	w2, [x4,4]
.L2694:
	add	x0, x0, 1
	b	.L2701
.L2794:
	add	x25, x19, :lo12:.LANCHOR2
	ldrb	w2, [x21,3416]
	mov	w1, w24
	mov	x28, 0
	ldr	x0, [x25,1024]
	bl	FlashReadPages
.L2696:
	cmp	w24, w28, uxth
	bls	.L2795
	mov	x0, 56
	mul	x5, x28, x0
	ldr	x0, [x25,1024]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L2721
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2721
	ldr	w0, [x4,8]
	add	x1, x29, 156
	mov	w2, 0
	str	x5, [x29,112]
	str	x4, [x29,120]
	bl	log2phys
	ldr	x2, [x25,1024]
	ldr	x5, [x29,112]
	ldr	w0, [x29,156]
	add	x2, x2, x5
	ldr	x4, [x29,120]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2721
	adrp	x0, .LANCHOR5
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR5
	str	x5, [x29,104]
	str	x4, [x29,112]
	ldrh	w0, [x1,-150]
	add	w0, w0, 1
	strh	w0, [x1,-150]
	ldr	w0, [x21,3984]
	ldr	x1, [x25,1000]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,120]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,120]
	mov	x2, 56
	ldr	x5, [x29,104]
	ldr	x4, [x29,112]
	str	w0, [x1,4]
	ldr	w0, [x21,3984]
	ldr	x1, [x25,1000]
	madd	x0, x0, x2, x1
	ldr	x1, [x25,1024]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x25, 3704
	str	x2, [x29,120]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,156]
	str	w0, [x4,12]
	ldrh	w0, [x25,3704]
	strh	w0, [x4,2]
	ldr	w0, [x25,876]
	str	w0, [x4,4]
	ldr	w0, [x21,3984]
	add	w0, w0, 1
	str	w0, [x21,3984]
	ldr	x0, [x25,1024]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L2698
	ldr	x2, [x29,120]
	ldr	w1, [x21,3984]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2698
	ldrh	w0, [x2,4]
	cbnz	w0, .L2721
.L2698:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2721
	adrp	x0, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x19,924]
	ldrh	w2, [x0,-152]
	b	.L2764
.L2721:
	add	x28, x28, 1
	b	.L2696
.L2795:
	add	w27, w27, 1
	b	.L2693
.L2793:
	ldr	w0, [x21,3984]
	adrp	x22, .LANCHOR5
	cbz	w0, .L2703
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2703
	add	x19, x19, :lo12:.LANCHOR2
	add	x5, x22, :lo12:.LANCHOR5
	str	wzr, [x19,924]
	ldrh	w2, [x5,-152]
	b	.L2764
.L2703:
	add	x5, x22, :lo12:.LANCHOR5
	ldrh	w22, [x5,-150]
	cbnz	w22, .L2704
	add	x24, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21,3408]
	ubfiz	x0, x1, 1, 16
	ldr	x3, [x24,1184]
	ldrh	w4, [x3,x0]
	cbz	w4, .L2704
	ldrh	w2, [x21,3410]
	mov	w3, w22
	ldrh	w5, [x5,-148]
	ldr	x0, [x29,128]
	bl	printk
	ldrh	w1, [x21,3408]
	ldr	x0, [x24,1184]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x21,3408]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2704:
	mov	w0, -1
	strh	w0, [x21,3408]
	b	.L2702
.L2705:
	str	wzr, [x1,924]
	adrp	x1, .LANCHOR5-152
	ldrh	w2, [x1,#:lo12:.LANCHOR5-152]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2764
.L2792:
	ldrh	w26, [x22,948]
	cmp	w26, w20
	bne	.L2636
	ldrh	w0, [x22,3600]
	mov	w2, 1024
	ldr	w1, [x22,940]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2636
	adrp	x0, .LANCHOR5-152
	str	wzr, [x22,940]
	strh	wzr, [x0,#:lo12:.LANCHOR5-152]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w26
	bne	.L2638
	ldrh	w1, [x22,3600]
	ldrh	w0, [x22,958]
	cmp	w1, w0
	bcs	.L2639
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2648
	ldr	w2, [x22,864]
	uxtw	x1, w0
	cbnz	w2, .L2641
	ldrh	w0, [x22,768]
	cmp	w0, 3
	beq	.L2641
	ldr	w0, [x22,868]
	cbnz	w0, .L2641
	ldr	w0, [x22,652]
	cbnz	w0, .L2641
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2642
.L2641:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,1184]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x2,820]
	ldrh	w0, [x2,748]
	ldrh	w2, [x2,768]
	mul	w3, w1, w0
	cmp	w2, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w3, w0
	cmp	w4, w0
	bgt	.L2644
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,1396]
	ldr	w2, [x0,3892]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bhi	.L2788
	mov	w1, 160
	b	.L2787
.L2644:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2789
.L2642:
	ldr	x2, [x22,1184]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2647
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
.L2788:
	mov	w1, 128
.L2787:
	strh	w1, [x0,958]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2648
	b	.L2638
.L2647:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2789:
	strh	w1, [x0,958]
	b	.L2648
.L2639:
	mov	w0, 80
	strh	w0, [x22,958]
	b	.L2648
.L2638:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC151
	mov	w1, w20
	add	x0, x0, :lo12:.LC151
	ldr	x3, [x5,1184]
	ldr	x6, [x5,1168]
	ldrh	w2, [x5,3600]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,956]
	ldrh	w4, [x6,x4]
	bl	printk
.L2648:
	bl	FtlGcReFreshBadBlk
	b	.L2636
.L2791:
	mov	w20, w0
	b	.L2636
.L2764:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -112]!
	adrp	x0, .LANCHOR5-144
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	ldr	x20, [x0,#:lo12:.LANCHOR5-144]
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w22, [x0,1400]
	cbnz	w22, .L2798
	ldr	w1, [x0,972]
	cbz	w1, .L2798
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2800
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2800:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w3, [x20,9]
	adrp	x24, .LC153
	mov	w2, w22
	mov	w23, 0
	mov	w25, 56
	ldr	x0, [x0,1032]
	add	x24, x24, :lo12:.LC153
	bl	FlashProgPages
.L2801:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,972]
	cmp	w23, w1
	bcs	.L2819
	umull	x21, w23, w25
	ldr	x1, [x0,1032]
	add	x0, x1, x21
	ldr	w1, [x1,x21]
	cmn	w1, #1
	beq	.L2822
	ldr	w1, [x0,4]
	cbz	w22, .L2836
	orr	w1, w1, -2147483648
.L2836:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,108]
	add	x26, x19, :lo12:.LANCHOR2
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x26,1032]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2805
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x26,1184]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2806
	mov	x0, x24
	bl	printk
.L2806:
	mov	w0, w21
	bl	decrement_vpc_count
.L2805:
	add	w23, w23, 1
	b	.L2801
.L2838:
	mov	w20, 16386
.L2818:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,962]
	cbz	w0, .L2819
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L2818
.L2819:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,972]
	b	.L2798
.L2822:
	adrp	x25, .LANCHOR4
	adrp	x26, .LC153
	mov	w24, 0
	mov	w27, 56
	add	x25, x25, :lo12:.LANCHOR4
	add	x26, x26, :lo12:.LC153
.L2802:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,972]
	cmp	w24, w1
	bcs	.L2838
	umull	x21, w24, w27
	ldr	x0, [x0,1032]
	mov	w1, -1
	mov	w28, 1
	str	w1, [x0,x21]
.L2808:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	x0, [x23,1032]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2839
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2809
	ldr	x2, [x23,1184]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20,4]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x23,818]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2809:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2810
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2810:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	w0, [x25,3552]
	add	w0, w0, 1
	str	w0, [x25,3552]
	ldr	x0, [x23,1032]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,108]
	ldr	x1, [x23,1032]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x23,1032]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x23,1032]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2811
	str	w28, [x23,1400]
.L2811:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,1400]
	cbz	w0, .L2808
	b	.L2798
.L2839:
	cbz	w22, .L2837
	orr	w0, w0, -2147483648
.L2837:
	str	w0, [x29,108]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x23, x19, :lo12:.LANCHOR2
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x23,1032]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2815
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x23,1184]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2816
	mov	x0, x26
	bl	printk
.L2816:
	mov	w0, w21
	bl	decrement_vpc_count
.L2815:
	add	w24, w24, 1
	b	.L2802
.L2798:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	adrp	x0, .LANCHOR2+1400
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+1400]
	cbnz	w0, .L2841
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2841:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3160
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+3160]
	cmp	w0, 1
	bne	.L2843
	bl	FtlSysFlush
.L2843:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlDeInit, .-FtlDeInit
	.align	2
	.global	ftl_deinit
	.type	ftl_deinit, %function
ftl_deinit:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	ftl_flash_de_init
	bl	FtlDeInit
	bl	ftl_flash_de_init
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_deinit, .-ftl_deinit
	.align	2
	.global	ftl_cache_flush
	.type	ftl_cache_flush, %function
ftl_cache_flush:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	FtlCacheWriteBack
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_cache_flush, .-ftl_cache_flush
	.align	2
	.global	ftl_discard
	.type	ftl_discard, %function
ftl_discard:
	stp	x29, x30, [sp, -80]!
	add	w2, w0, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	adrp	x0, .LANCHOR0+4048
	mov	w19, w1
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2847
	cmp	w19, 31
	bls	.L2861
	adrp	x22, .LANCHOR2
	add	x21, x22, :lo12:.LANCHOR2
	ldr	w0, [x21,1400]
	cbnz	w0, .L2861
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,824]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2850
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2850:
	mov	w0, -1
	adrp	x23, .LANCHOR5
	str	w0, [x29,76]
.L2851:
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w0, [x20,824]
	cmp	w19, w0
	bcc	.L2862
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2852
	add	x1, x23, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,-136]
	add	w0, w0, 1
	str	w0, [x1,-136]
	ldr	w0, [x20,888]
	add	x1, x29, 76
	add	w0, w0, 1
	str	w0, [x20,888]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2852:
	add	x0, x22, :lo12:.LANCHOR2
	add	w21, w21, 1
	ldrh	w0, [x0,824]
	sub	w19, w19, w0
	b	.L2851
.L2862:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,-136]
	cmp	w1, 32
	bls	.L2861
	str	wzr, [x0,-136]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2861:
	mov	w0, 0
.L2847:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 80
	ret
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w3, [x2,1400]
	ldrh	w1, [x2,818]
	cbnz	w3, .L2897
	ldrh	w20, [x2,3704]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2866
.L2875:
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR4
	add	x22, x21, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x23, x20, 3704
	ldrh	w1, [x20,3704]
	str	wzr, [x22,4008]
	cmp	w1, w0
	beq	.L2897
	bl	FtlCacheWriteBack
	mov	w26, 12
	ldrb	w0, [x23,7]
	ldrh	w3, [x20,818]
	ldr	x1, [x20,1184]
	ldrh	w2, [x20,3704]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldrh	w1, [x22,4018]
	mov	w22, 0
	ldr	w0, [x20,880]
	add	w0, w1, w0
	str	w0, [x20,880]
	b	.L2876
.L2866:
	cbz	w0, .L2869
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3164]
	cmp	w5, w4
	beq	.L2870
.L2871:
	mov	w1, 2
	b	.L2869
.L2870:
	strh	w3, [x0,3164]
	ldrh	w0, [x2,3600]
	cmp	w0, 17
	bhi	.L2871
.L2869:
	add	x21, x19, :lo12:.LANCHOR2
	add	x0, x21, 3704
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2872
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x21,1168]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2873
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2873:
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	x19, x0
	ldr	w2, [x1,4008]
	cbnz	w2, .L2874
	ldr	w0, [x1,3552]
	add	w0, w0, 1
	str	w0, [x1,3552]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2874:
	add	x0, x19, :lo12:.LANCHOR4
	str	wzr, [x0,4008]
	mov	w0, 1
	b	.L2865
.L2872:
	adrp	x0, .LANCHOR1+3164
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3164]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2865
	b	.L2875
.L2879:
	ldr	x25, [x20,984]
	add	x1, x29, 92
	umull	x24, w22, w26
	mov	w2, 0
	add	x23, x25, x24
	ldr	w0, [x23,8]
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L2877
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2896:
	bl	decrement_vpc_count
.L2878:
	add	w22, w22, 1
	uxth	w22, w22
.L2876:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,4018]
	cmp	w0, w22
	bhi	.L2879
	b	.L2898
.L2877:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2878
	ldrh	w0, [x20,3704]
	b	.L2896
.L2898:
	mov	w0, -1
	adrp	x20, .LANCHOR0
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2880
	add	x1, x19, :lo12:.LANCHOR2
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	ldrh	w1, [x1,3704]
	bl	printk
.L2880:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3704]
	ldr	x1, [x1,1184]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L2881
	bl	INSERT_DATA_LIST
	b	.L2882
.L2881:
	bl	INSERT_FREE_LIST
.L2882:
	add	x23, x21, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR2
	mov	w24, -1
	strh	wzr, [x23,4018]
	strh	w24, [x22,3704]
	strh	wzr, [x23,4016]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w24, [x23,3408]
	ldr	w0, [x22,652]
	cbz	w0, .L2883
	ldr	w0, [x22,920]
	cmp	w0, 39
	bhi	.L2883
	ldrh	w0, [x23,3404]
	ldrh	w1, [x22,3600]
	cmp	w1, w0
	bcs	.L2897
	lsl	w0, w0, 1
	strh	w0, [x22,956]
.L2897:
	mov	w0, 0
	b	.L2865
.L2883:
	add	x21, x21, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21,3404]
	ldrh	w0, [x19,3600]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2897
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,136]
	cbz	w0, .L2884
	sub	w1, w1, #2
	strh	w1, [x19,956]
	b	.L2897
.L2884:
	mov	w1, 20
	strh	w1, [x19,956]
.L2865:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x19, 3704
	mov	x0, x20
	ldrh	w1, [x19,818]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,3706]
	ldrh	w0, [x19,818]
	cmp	w1, w0
	bcc	.L2899
	add	x0, x19, 3752
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, .LANCHOR4+4008
	str	wzr, [x0,#:lo12:.LANCHOR4+4008]
.L2899:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4+4064
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+4064]
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x19, 3608
	add	x19, x19, 3656
	mov	x0, x20
	bl	FtlRecoverySuperblock
	mov	x0, x20
	bl	FtlSlcSuperblockCheck
	mov	x0, x19
	bl	FtlRecoverySuperblock
	mov	x0, x19
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	mov	w0, -1
	bl	decrement_vpc_count
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -48]!
	mov	w1, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	add	x0, x20, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR2
	strh	w1, [x0,4058]
	strh	wzr, [x0,4060]
	ldrh	w0, [x22,752]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x22,3896]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2903
.L2905:
	mov	w21, -1
	b	.L2904
.L2903:
	bl	FtlLoadSysInfo
	mov	w21, w0
	cbnz	w0, .L2905
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x22,858]
	mov	x1, 0
	ldr	x0, [x22,1264]
.L2906:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2910
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2906
.L2910:
	add	x0, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,3548]
	add	w1, w1, 1
	strh	w1, [x0,3548]
	blt	.L2907
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w0, [x20,4060]
	cbz	w0, .L2911
.L2907:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,3608]
	ldr	x4, [x1,1184]
	lsl	x2, x2, 1
	ldrh	w5, [x1,3612]
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	strb	wzr, [x1,3614]
	ldrh	w3, [x1,3656]
	ldrh	w2, [x1,818]
	lsl	x3, x3, 1
	ldr	x4, [x1,1184]
	strh	w2, [x1,3610]
	strh	wzr, [x1,3612]
	ldrh	w5, [x1,3660]
	ldrh	w0, [x4,x3]
	sub	w0, w0, w5
	strh	w0, [x4,x3]
	strb	wzr, [x1,3662]
	ldrh	w0, [x1,818]
	strh	w0, [x1,3658]
	ldrh	w0, [x1,3550]
	strh	wzr, [x1,3660]
	add	w0, w0, 1
	strh	w0, [x1,3550]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2911:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3548]
	and	w0, w0, 31
	cbnz	w0, .L2912
	bl	FtlVpcCheckAndModify
.L2912:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x22, x19, 3608
	ldrh	w0, [x19,3608]
	cmp	w0, w1
	beq	.L2904
	ldrh	w1, [x19,3612]
	cbnz	w1, .L2904
	ldrh	w1, [x19,3660]
	add	x20, x19, 3656
	cbnz	w1, .L2904
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x19,3656]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x22
	bl	allocate_new_data_superblock
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2904:
	mov	w0, w21
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x23, [sp,48]
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w21, [x20,1400]
	cbnz	w21, .L2920
	ldrh	w2, [x20,856]
	mov	w1, w21
	ldr	x0, [x20,1248]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,856]
	mov	w1, w21
	ldr	x0, [x20,1240]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w21, [x20,872]
	ldrh	w0, [x20,752]
	str	w21, [x20,876]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L2921
	bl	FtlMakeBbt
.L2921:
	mov	w0, 0
.L2922:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,824]
	cmp	w0, w2, lsl 7
	bge	.L2949
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1088]
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	str	w2, [x4,x3]
	add	w0, w0, 1
	ldr	x2, [x1,1096]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	str	w1, [x2,x3]
	b	.L2922
.L2949:
	ldrh	w21, [x1,756]
	mov	w20, 0
.L2924:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,758]
	cmp	w0, w21
	bls	.L2950
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2924
.L2950:
	ldrh	w0, [x22,748]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L2926
.L2930:
	mov	w20, 0
	mov	w21, w20
	b	.L2927
.L2926:
	udiv	w20, w20, w0
	ldr	w0, [x22,852]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22,752]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x22,756]
.L2928:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,758]
	cmp	w0, w20
	bls	.L2930
	mov	w0, w20
	mov	w1, 1
	add	w20, w20, 1
	bl	FtlLowFormatEraseBlock
	uxth	w20, w20
	b	.L2928
.L2927:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,756]
	cmp	w1, w21
	bls	.L2951
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2927
.L2951:
	ldrh	w1, [x0,758]
	adrp	x21, .LANCHOR4
	ldrh	w2, [x0,748]
	add	x6, x21, :lo12:.LANCHOR4
	str	w1, [x0,944]
	ldr	w1, [x0,760]
	udiv	w3, w1, w2
	str	w3, [x0,1396]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,3404]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w20, w4
	ble	.L2932
	sub	w1, w1, w20
	udiv	w1, w1, w2
	str	w1, [x0,1396]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,3404]
.L2932:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,652]
	cmp	w0, 1
	bne	.L2933
	add	x1, x21, :lo12:.LANCHOR4
	udiv	w0, w20, w2
	ldrh	w4, [x1,3404]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,3404]
.L2933:
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2934
	add	x1, x21, :lo12:.LANCHOR4
	udiv	w0, w20, w2
	ldrh	w4, [x1,3404]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,3404]
.L2934:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w1, [x6,810]
	cbz	w1, .L2936
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w4, [x0,3404]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,3404]
	mul	w4, w1, w2
	cmp	w4, w20
	ble	.L2936
	add	w1, w1, 32
	str	w3, [x6,1396]
	add	w1, w5, w1
	strh	w1, [x0,3404]
.L2936:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x21, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR0
	mov	w23, -1
	ldrh	w0, [x20,3404]
	ldr	w1, [x19,1396]
	sub	w0, w1, w0
	ldrh	w1, [x19,818]
	mul	w0, w0, w2
	str	w0, [x20,3400]
	mul	w0, w0, w1
	ldrh	w1, [x19,824]
	str	w0, [x19,1396]
	mul	w0, w0, w1
	str	w0, [x22,4048]
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,758]
	mov	w1, 0
	ldr	x0, [x19,1184]
	lsl	w2, w2, 1
	bl	ftl_memset
	strh	w23, [x20,3408]
	strh	wzr, [x20,3410]
	mov	w0, 1
	strb	wzr, [x20,3414]
	mov	w1, 255
	strb	wzr, [x20,3416]
	add	x20, x19, 3608
	ldrh	w2, [x19,756]
	strb	w0, [x20,8]
	ldr	x0, [x22,16]
	lsr	w2, w2, 3
	str	wzr, [x19,3892]
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	strh	wzr, [x19,3608]
	bl	ftl_memset
.L2938:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2939
	ldrh	w1, [x20]
	ldr	x0, [x19,1184]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2938
.L2939:
	ldr	w0, [x19,872]
	mov	w22, -1
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,872]
	ldr	x0, [x19,1184]
	ldrh	w2, [x20,4]
	strh	w2, [x0,x1,lsl 1]
	add	x0, x19, 3656
	strh	wzr, [x19,3658]
	ldrh	w1, [x20]
	mov	x20, x0
	strb	wzr, [x19,3662]
	add	w1, w1, 1
	strh	w1, [x19,3656]
	mov	w1, 1
	strb	w1, [x19,3664]
.L2940:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L2941
	ldrh	w1, [x20]
	ldr	x0, [x19,1184]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L2940
.L2941:
	ldr	w0, [x19,872]
	add	x21, x21, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,872]
	ldr	x0, [x19,1184]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,3704]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,3896]
	ldr	w0, [x21,3400]
	strh	w0, [x19,3902]
	ldr	w0, [x19,872]
	str	w0, [x19,3904]
	add	w0, w0, 1
	strh	wzr, [x19,3898]
	strh	w20, [x19,3900]
	str	w0, [x19,872]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2920
	adrp	x0, .LANCHOR1+3160
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+3160]
.L2920:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2953
.L2955:
	mov	w0, 0
	b	.L2954
.L2953:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0,#:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	cbnz	w0, .L2955
	bl	FlashLoadFactorBbt
	cbz	w0, .L2956
	bl	FlashMakeFactorBbt
.L2956:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,688]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L2957
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L2959:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L2959
	cmp	w0, 6
	bhi	.L2960
	add	x0, x19, :lo12:.LANCHOR0
	b	.L2982
.L2960:
	mov	w1, 0
	mov	w4, 1
.L2963:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L2963
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L2982
	mov	w1, 36
.L2982:
	strb	w1, [x0,25]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	strh	w1, [x0,4042]
.L2957:
	adrp	x0, .LC45
	adrp	x1, .LC138
	add	x1, x1, :lo12:.LC138
	add	x0, x0, :lo12:.LC45
	add	x20, x20, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w19, 1
	add	x0, x0, 4016
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x20,752]
	bl	FtlFreeSysBlkQueueInit
.L2965:
	bl	FtlLoadBbt
	cbz	w0, .L2966
.L2984:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L2985
	add	w19, w19, 1
	b	.L2965
.L2985:
	mov	w0, -1
	b	.L2954
.L2966:
	bl	FtlSysBlkInit
	cbnz	w0, .L2984
	adrp	x1, .LANCHOR1+3160
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+3160]
.L2954:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x23, [sp,48]
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w1, [x0,1400]
	cbz	w1, .L2987
.L2990:
	mov	w0, 0
	b	.L2988
.L2987:
	adrp	x1, .LANCHOR0+136
	adrp	x22, .LANCHOR4
	ldrb	w1, [x1,#:lo12:.LANCHOR0+136]
	cbz	w1, .L2989
	add	x1, x22, :lo12:.LANCHOR4
	ldr	w1, [x1,3984]
	tbz	x1, 0, .L2989
	ldrh	w0, [x0,3708]
	cbnz	w0, .L2990
.L2989:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x22, :lo12:.LANCHOR4
	mov	w2, 0
	mov	w21, 0
	mov	w3, w2
	mov	w23, 56
	ldr	x0, [x0,1000]
	ldr	w1, [x1,3984]
	bl	FlashProgPages
.L2991:
	add	x20, x22, :lo12:.LANCHOR4
	ldr	w1, [x20,3984]
	cmp	w21, w1
	bcs	.L3001
	add	x1, x19, :lo12:.LANCHOR2
	umull	x0, w21, w23
	ldr	x2, [x1,1000]
	add	x3, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2992
	ldrh	w4, [x1,3704]
	ldr	x3, [x1,1184]
	strh	wzr, [x3,x4,lsl 1]
	strh	w2, [x1,3704]
	ldr	x1, [x1,1000]
	ldr	w2, [x20,3552]
	add	x0, x1, x0
	add	w2, w2, 1
	str	w2, [x20,3552]
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3000
.L2992:
	ldr	x2, [x3,16]
	add	w21, w21, 1
	ldr	w1, [x3,4]
	uxth	w21, w21
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2991
.L3001:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x19,1000]
	bl	FtlGcBufFree
	str	wzr, [x20,3984]
	ldrh	w0, [x19,3708]
	cbnz	w0, .L2990
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L3000:
	mov	w0, 1
.L2988:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 3704
	ldrh	w2, [x0,3704]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3003
	ldrh	w0, [x1,4]
	cbnz	w0, .L3004
.L3003:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3704
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	strh	wzr, [x0,4016]
	strh	wzr, [x0,4018]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3004:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3704
	bl	get_new_active_ppa
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_read
	.type	ftl_read, %function
ftl_read:
	sub	sp, sp, #224
	cmp	w0, 16
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	mov	w20, w1
	mov	w28, w2
	mov	x25, x3
	bne	.L3006
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3007
.L3006:
	add	w0, w1, w2
	str	w0, [x29,188]
	adrp	x0, .LANCHOR0+4048
	ldr	w2, [x29,188]
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3007
	adrp	x19, .LANCHOR2
	adrp	x21, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR2
	sub	w24, w2, #1
	add	x2, x21, :lo12:.LANCHOR4
	ldrh	w1, [x0,824]
	udiv	w27, w20, w1
	udiv	w24, w24, w1
	ldr	w1, [x2,3972]
	add	w1, w28, w1
	sub	w23, w24, w27
	str	w1, [x2,3972]
	add	w23, w23, 1
	ldr	w1, [x0,900]
	add	w1, w23, w1
	str	w1, [x0,900]
	mov	w0, w27
	mov	w1, w24
	bl	FtlCacheMetchLpa
	str	x21, [x29,120]
	cbz	w0, .L3008
	bl	FtlCacheWriteBack
.L3008:
	mov	w26, 0
	adrp	x0, .LC56
	add	x0, x0, :lo12:.LC56
	mov	w21, w27
	str	w26, [x29,184]
	mov	w22, w26
	str	w26, [x29,172]
	str	x0, [x29,112]
.L3009:
	cbz	w23, .L3051
	mov	w0, w21
	add	x1, x29, 204
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,204]
	cmn	w3, #1
	bne	.L3049
	mov	w3, 0
.L3010:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,824]
	cmp	w3, w0
	bcs	.L3014
	madd	w0, w21, w0, w3
	cmp	w0, w20
	bcc	.L3012
	ldr	w1, [x29,188]
	cmp	w0, w1
	bcs	.L3012
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x25, x0
	str	x3, [x29,176]
	bl	ftl_memset
	ldr	x3, [x29,176]
.L3012:
	add	w3, w3, 1
	b	.L3010
.L3049:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w21, w27
	umull	x1, w22, w0
	ldr	x0, [x2,992]
	add	x0, x0, x1
	str	w3, [x0,4]
	ldrh	w0, [x2,824]
	bne	.L3015
	ldr	x3, [x2,992]
	ldr	x2, [x2,1088]
	add	x3, x3, x1
	str	x2, [x3,8]
	udiv	w2, w20, w0
	msub	w2, w2, w0, w20
	str	w2, [x29,144]
	sub	w2, w0, w2
	cmp	w2, w28
	csel	w2, w2, w28, ls
	str	w2, [x29,184]
	cmp	w2, w0
	bne	.L3016
	str	x25, [x3,8]
	b	.L3016
.L3015:
	cmp	w21, w24
	bne	.L3017
	ldr	x3, [x2,992]
	ldr	x2, [x2,1096]
	add	x3, x3, x1
	ldr	w4, [x29,188]
	str	x2, [x3,8]
	mul	w2, w21, w0
	sub	w26, w4, w2
	cmp	w26, w0
	bne	.L3016
	sub	w2, w2, w20
	ubfiz	x2, x2, 9, 23
	add	x2, x25, x2
	str	x2, [x3,8]
	b	.L3016
.L3017:
	ldr	x2, [x2,992]
	mul	w0, w21, w0
	add	x2, x2, x1
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
	str	x0, [x2,8]
.L3016:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,992]
	add	x1, x0, x1
	ldrh	w0, [x2,830]
	ldr	x2, [x2,1120]
	mul	w0, w22, w0
	str	w21, [x1,24]
	add	w22, w22, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3014:
	subs	w23, w23, #1
	add	w21, w21, 1
	beq	.L3018
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,748]
	cmp	w22, w0, lsl 3
	bne	.L3009
.L3018:
	cbz	w22, .L3009
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
	str	xzr, [x29,176]
	ldr	x0, [x29,144]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,136]
	ldr	w0, [x29,184]
	lsl	w0, w0, 9
	str	w0, [x29,168]
	lsl	w0, w26, 9
	str	w0, [x29,132]
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29,160]
.L3020:
	ldr	w0, [x29,176]
	cmp	w22, w0
	bls	.L3052
	ldr	x0, [x29,176]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	mul	x8, x0, x1
	ldr	x0, [x2,992]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w27
	bne	.L3021
	ldr	x1, [x0,8]
	ldr	x0, [x2,1088]
	cmp	x1, x0
	bne	.L3022
	ldr	x2, [x29,136]
	mov	x0, x25
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,168]
	b	.L3050
.L3021:
	cmp	w1, w24
	bne	.L3022
	ldr	x1, [x0,8]
	ldr	x0, [x2,1096]
	cmp	x1, x0
	bne	.L3022
	ldrh	w0, [x2,824]
	ldr	w2, [x29,132]
	str	x8, [x29,104]
	mul	w0, w24, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
.L3050:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3022:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	add	x2, x0, x8
	ldr	w1, [x0,x8]
	cmn	w1, #1
	bne	.L3023
	ldr	x0, [x29,160]
	str	w1, [x29,172]
	add	x3, x0, 3456
	ldr	w0, [x0,3528]
	add	w0, w0, 1
	str	w0, [x3,72]
.L3023:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3024
	ldr	x0, [x29,160]
	str	x8, [x29,104]
	add	x1, x0, 3456
	ldr	w0, [x0,3528]
	add	w0, w0, 1
	str	w0, [x1,72]
	ldr	x7, [x2,8]
	ldr	x6, [x2,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	ldr	x0, [x29,112]
	ldr	w1, [x2,24]
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x2,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	ldr	x8, [x29,104]
.L3024:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3025
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3025:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L3020
.L3052:
	mov	w22, 0
	b	.L3009
.L3051:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,962]
	cbz	w0, .L3028
	mov	w0, w23
	mov	w1, 1
	bl	ftl_do_gc
.L3028:
	ldr	w0, [x29,172]
.L3007:
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 224
	ret
	.size	ftl_read, .-ftl_read
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -48]!
	mov	w0, -1
	adrp	x1, .LC138
	add	x29, sp, 0
	str	x21, [sp,32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x1, x1, :lo12:.LC138
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	str	w0, [x21,3160]
	adrp	x0, .LANCHOR5-132
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x0,#:lo12:.LANCHOR5-132]
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	str	wzr, [x19,1400]
	bl	printk
	add	x0, x20, 4016
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,752]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3054
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	b	.L3066
.L3054:
	bl	FtlSysBlkInit
	cbz	w0, .L3056
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
.L3066:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 96
	bl	printk
	b	.L3055
.L3056:
	mov	w1, 1
	str	w1, [x21,3160]
	bl	ftl_do_gc
	ldrh	w0, [x19,3600]
	cmp	w0, 15
	bhi	.L3057
	mov	w19, 1024
.L3058:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3058
	b	.L3055
.L3057:
	ldrb	w0, [x20,136]
	cbz	w0, .L3055
	mov	w19, 128
.L3060:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3060
.L3055:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	stp	x29, x30, [sp, -272]!
	mov	w4, w0
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w1
	adrp	x1, .LANCHOR2
	stp	x21, x22, [sp,32]
	mov	w21, w2
	add	x2, x1, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x22, x3
	str	x1, [x29,152]
	ldr	w3, [x2,1400]
	cbnz	w3, .L3068
	cmp	w4, 16
	bne	.L3069
	add	w0, w20, 256
	mov	w1, w21
	mov	x2, x22
	bl	FtlVendorPartWrite
	b	.L3068
.L3069:
	adrp	x0, .LANCHOR0+4048
	add	w24, w20, w21
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w24, w1
	bhi	.L3068
	adrp	x5, .LANCHOR5
	ldrh	w1, [x2,824]
	add	x26, x5, :lo12:.LANCHOR5
	sub	w24, w24, #1
	mov	w0, 2048
	adrp	x4, .LANCHOR4
	udiv	w23, w20, w1
	add	x3, x4, :lo12:.LANCHOR4
	str	w0, [x26,-128]
	cmp	w21, w1, lsl 1
	str	x5, [x29,144]
	str	x4, [x29,112]
	udiv	w0, w24, w1
	str	w0, [x29,200]
	sub	w28, w0, w23
	add	w25, w28, 1
	ldr	w0, [x2,884]
	add	w0, w25, w0
	str	w0, [x2,884]
	ldr	w0, [x3,3968]
	add	w0, w21, w0
	str	w0, [x3,3968]
	ldr	w3, [x2,972]
	cset	w0, cs
	str	w0, [x29,204]
	cbz	w3, .L3071
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x2,1032]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w23, w0
	bne	.L3072
	ldr	w0, [x2,892]
	add	w0, w0, 1
	str	w0, [x2,892]
	ldr	w0, [x26,-124]
	add	w0, w0, 1
	str	w0, [x26,-124]
	msub	w0, w23, w1, w20
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w21
	csel	w19, w1, w21, ls
	ldr	x1, [x3,8]
	lsl	w25, w19, 9
	add	x0, x1, x0
	mov	w2, w25
	mov	x1, x22
	bl	ftl_memcpy
	cbnz	w28, .L3073
	ldr	w0, [x26,-124]
	cmp	w0, 2
	bgt	.L3073
.L3106:
	mov	w0, 0
	b	.L3068
.L3073:
	add	x22, x22, x25
	sub	w21, w21, w19
	add	w20, w20, w19
	add	w23, w23, 1
	mov	w25, w28
.L3072:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,-124]
.L3071:
	ldr	w1, [x29,200]
	mov	w0, w23
	bl	FtlCacheMetchLpa
	cbz	w0, .L3074
	bl	FtlCacheWriteBack
.L3074:
	ldr	x0, [x29,144]
	ldr	x1, [x29,152]
	add	x0, x0, :lo12:.LANCHOR5
	str	w23, [x29,192]
	add	x3, x1, :lo12:.LANCHOR2
	add	x19, x3, 3608
	mov	x24, x3
	str	x19, [x0,-144]
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	str	x19, [x29,176]
	str	x0, [x29,96]
.L3075:
	cbz	w25, .L3135
	ldrh	w0, [x19,4]
	cbnz	w0, .L3076
	ldr	x1, [x29,176]
	adrp	x26, .LANCHOR1
	cmp	x19, x1
	bne	.L3077
	add	x0, x24, 3656
	ldrh	w19, [x0,4]
	cbnz	w19, .L3078
	bl	allocate_new_data_superblock
	add	x0, x26, :lo12:.LANCHOR1
	str	w19, [x0,3168]
.L3078:
	ldr	x0, [x29,176]
	add	x26, x26, :lo12:.LANCHOR1
	add	x19, x24, 3656
	bl	allocate_new_data_superblock
	ldr	w0, [x26,3168]
	cbnz	w0, .L3079
.L3080:
	ldr	x19, [x29,176]
	b	.L3079
.L3077:
	add	x26, x26, :lo12:.LANCHOR1
	str	w0, [x26,3168]
	ldr	x0, [x29,176]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3080
	mov	x0, x19
	bl	allocate_new_data_superblock
.L3079:
	ldrh	w0, [x19,4]
	cbnz	w0, .L3081
	mov	x0, x19
	bl	allocate_new_data_superblock
.L3081:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	x19, [x0,-144]
.L3076:
	ldr	w0, [x24,968]
	mov	x28, 0
	ldr	w2, [x24,972]
	ldrh	w1, [x19,4]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w25
	csel	w0, w0, w25, ls
	str	w0, [x29,140]
	ldr	w0, [x29,204]
	and	w0, w0, 1
	str	w0, [x29,108]
	add	w0, w21, w20
	str	w0, [x29,136]
.L3082:
	ldr	w1, [x29,192]
	mov	w0, w28
	add	w27, w1, w28
	ldr	w1, [x29,140]
	cmp	w28, w1
	bcs	.L3136
	ldrh	w1, [x19,4]
	cbz	w1, .L3112
	ldr	w1, [x29,200]
	cmp	w27, w1
	cset	w7, eq
	cbz	w0, .L3084
	ldr	w1, [x29,108]
	tst	w7, w1
	beq	.L3084
	ldrh	w1, [x24,824]
	ldr	w2, [x29,136]
	msub	w2, w27, w1, w2
	cmp	w2, w1
	bne	.L3112
.L3084:
	add	x1, x29, 212
	mov	w2, 0
	mov	w0, w27
	str	x7, [x29,160]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	ldr	w6, [x24,972]
	mov	w5, 56
	ldr	x2, [x24,1032]
	str	x5, [x29,184]
	umull	x1, w6, w5
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x24,830]
	ldr	x0, [x24,1032]
	add	x1, x0, x1
	mul	w0, w2, w6
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x24,1136]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x26, x0, x3
	ldrh	w0, [x24,828]
	str	x26, [x1,16]
	str	w27, [x1,24]
	mul	w0, w0, w6
	ldr	x6, [x24,1080]
	and	x0, x0, 4294967292
	add	x0, x6, x0
	str	x0, [x1,8]
	mov	x0, x26
	mov	w1, 0
	bl	ftl_memset
	cmp	w27, w23
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3115
	ldr	x7, [x29,160]
	ldr	x5, [x29,184]
	cbz	w7, .L3085
	ldr	w0, [x29,136]
	ldrh	w2, [x24,824]
	msub	w2, w27, w2, w0
	uxth	w0, w2
	str	w0, [x29,184]
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	b	.L3088
.L3115:
	ldrh	w2, [x24,824]
	udiv	w0, w20, w2
	msub	w0, w0, w2, w20
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w21
	csel	w0, w2, w21, ls
	str	w0, [x29,184]
.L3088:
	ldrh	w0, [x24,824]
	ldr	w1, [x29,184]
	cmp	w1, w0
	bne	.L3089
	ldr	w0, [x29,196]
	mov	x1, x22
	cbnz	w0, .L3090
	ldr	w0, [x29,184]
	mul	w1, w0, w27
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	add	x1, x22, x1
.L3090:
	ldr	w2, [x29,204]
	mov	w0, 56
	cbz	w2, .L3091
	ldr	w2, [x24,972]
	umull	x0, w2, w0
	ldr	x2, [x24,1032]
	add	x0, x2, x0
	b	.L3132
.L3091:
	ldr	w2, [x24,972]
	umull	x0, w2, w0
	ldr	x2, [x24,1032]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L3133
.L3089:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L3093
	str	w0, [x29,220]
	mov	w1, 56
	ldr	w0, [x24,972]
	mov	w2, 0
	str	w27, [x29,240]
	umull	x0, w0, w1
	ldr	x1, [x24,1032]
	add	x0, x1, x0
	ldr	x1, [x0,8]
	ldr	x0, [x0,16]
	str	x1, [x29,224]
	mov	w1, 1
	str	x0, [x29,232]
	add	x0, x29, 216
	bl	FlashReadPages
	ldr	w0, [x29,216]
	cmn	w0, #1
	bne	.L3094
	ldr	x0, [x29,112]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,3528]
	add	w1, w1, 1
	str	w1, [x0,3528]
	b	.L3096
.L3094:
	ldr	w0, [x26,8]
	cmp	w0, w27
	beq	.L3096
	ldr	x0, [x29,112]
	mov	w2, w27
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,3528]
	add	w1, w1, 1
	str	w1, [x0,3528]
	ldr	x0, [x29,96]
	ldr	w1, [x26,8]
	bl	printk
	b	.L3096
.L3093:
	ldr	w0, [x24,972]
	mov	w1, 56
	ldrh	w2, [x24,828]
	umull	x0, w0, w1
	ldr	x1, [x24,1032]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3096:
	ldr	w1, [x29,184]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,196]
	cbz	w1, .L3097
	ldr	w1, [x24,972]
	umull	x0, w1, w0
	ldr	x1, [x24,1032]
	add	x1, x1, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x1,8]
	add	x0, x1, x0
	mov	x1, x22
	b	.L3134
.L3097:
	ldr	w1, [x24,972]
	umull	x0, w1, w0
	ldr	x1, [x24,1032]
	add	x0, x1, x0
	ldrh	w1, [x24,824]
	mul	w1, w27, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	add	x1, x22, x1
	b	.L3134
.L3085:
	ldr	w0, [x29,204]
	ldrh	w1, [x24,824]
	ldr	w2, [x24,972]
	cbz	w0, .L3098
	mul	w1, w27, w1
	umull	x0, w2, w5
	sub	w1, w1, w20
	ldr	x2, [x24,1032]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x22, x1
.L3132:
	str	x1, [x0,8]
	b	.L3092
.L3098:
	umull	x0, w2, w5
	ldr	x2, [x24,1032]
	mul	w1, w27, w1
	add	x0, x2, x0
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x22, x1
.L3133:
	ldrh	w2, [x24,828]
.L3134:
	bl	ftl_memcpy
.L3092:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	add	x28, x28, 1
	strh	w0, [x1,x2]
	str	w27, [x26,8]
	ldr	w0, [x24,876]
	str	w0, [x26,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x24,876]
	ldr	w0, [x29,212]
	str	w0, [x26,12]
	ldrh	w0, [x19]
	strh	w0, [x26,2]
	ldr	w0, [x24,972]
	add	w0, w0, 1
	str	w0, [x24,972]
	b	.L3082
.L3136:
	str	w27, [x29,192]
	mov	x0, x1
	b	.L3083
.L3112:
	str	w27, [x29,192]
.L3083:
	sub	w25, w25, w0
	ldr	w0, [x29,204]
	cbnz	w0, .L3102
	ldr	w1, [x24,972]
	ldr	w0, [x24,968]
	cmp	w1, w0
	bcs	.L3102
	ldrh	w0, [x19,4]
	cbz	w0, .L3102
.L3104:
	str	wzr, [x29,204]
	b	.L3075
.L3102:
	bl	FtlCacheWriteBack
	str	wzr, [x24,972]
	cmp	w25, 1
	bhi	.L3075
	b	.L3104
.L3135:
	ldr	w1, [x29,200]
	mov	w0, w25
	sub	w1, w1, w23
	bl	ftl_do_gc
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,3600]
	cmp	w1, 31
	bhi	.L3106
	mov	w1, 128
	mov	w19, 16
	strh	w1, [x0,958]
	strh	w1, [x0,956]
.L3107:
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,1400]
	cbnz	w0, .L3106
	subs	w19, w19, #1
	bne	.L3107
	b	.L3106
.L3068:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 272
	ret
	.size	ftl_write, .-ftl_write
	.align	2
	.global	ftl_vendor_write
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	stp	x29, x30, [sp, -16]!
	mov	w5, w0
	mov	w4, w1
	add	x29, sp, 0
	mov	x3, x2
	mov	w1, w5
	mov	w2, w4
	mov	w0, 16
	bl	ftl_write
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_vendor_write, .-ftl_vendor_write
	.align	2
	.global	ftl_sys_write
	.type	ftl_sys_write, %function
ftl_sys_write:
	stp	x29, x30, [sp, -16]!
	mov	w4, w1
	mov	x3, x2
	add	x29, sp, 0
	add	w1, w0, 256
	mov	w2, w4
	mov	w0, 16
	bl	ftl_write
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_sys_write, .-ftl_sys_write
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	adrp	x0, .LANCHOR0+136
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	cbz	w0, .L3139
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x24, .LC158
	add	x26, x20, 3608
	add	x25, x20, 3656
	ldrh	w23, [x0,4058]
	add	x0, x24, :lo12:.LC158
	ldr	x2, [x20,1184]
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x20,3608]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,3656]
	mov	w20, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x26
	bl	allocate_new_data_superblock
	mov	x0, x25
	bl	allocate_new_data_superblock
.L3141:
	subs	w20, w20, #1
	beq	.L3145
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,1184]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3141
.L3145:
	add	x20, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC158
	mov	w1, w23
	ldr	x2, [x20,1184]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x20,1184]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3143
	add	x0, x29, 80
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,748]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3146:
	cmp	w5, w0, uxth
	bls	.L3153
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3147
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,1016]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,1016]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3147:
	add	x0, x0, 1
	b	.L3146
.L3153:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC159
	mov	w1, w23
	add	x0, x0, :lo12:.LC159
	ldr	x2, [x19,1184]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,1016]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,1016]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3143:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21,4058]
.L3139:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.global	gc_ink_free_return_value
	.global	check_valid_page_count_table
	.global	FtlUpdateVaildLpnCount
	.global	g_ect_tbl_power_up_flush
	.global	last_cache_match_count
	.global	power_up_flag
	.global	g_LowFormat
	.global	gFtlInitStatus
	.global	DeviceCapacity
	.global	ToshibaRefValue
	.global	Toshiba15RefValue
	.global	ToshibaA19RefValue
	.global	SamsungRefValue
	.global	refValueDefault
	.global	FbbtBlk
	.global	random_seed
	.global	gSlcNandParaInfo
	.global	gNandParaInfo
	.global	g_page_map_check_enable
	.global	g_power_lost_ecc_error_blk
	.global	g_power_lost_recovery_flag
	.global	c_mlc_erase_count_value
	.global	g_recovery_ppa_tbl
	.global	g_recovery_page_min_ver
	.global	g_recovery_page_num
	.global	g_cur_erase_blk
	.global	g_gc_skip_write_count
	.global	g_gc_head_data_block_count
	.global	g_gc_head_data_block
	.global	g_ftl_nand_free_count
	.global	g_in_swl_replace
	.global	g_in_gc_progress
	.global	g_all_blk_used_slc_mode
	.global	g_max_erase_count
	.global	g_totle_sys_slc_erase_count
	.global	g_totle_slc_erase_count
	.global	g_min_erase_count
	.global	g_totle_avg_erase_count
	.global	g_totle_mlc_erase_count
	.global	g_totle_l2p_write_count
	.global	g_totle_cache_write_count
	.global	g_tmp_data_superblock_id
	.global	g_totle_read_page_count
	.global	g_totle_discard_page_count
	.global	g_totle_read_sector
	.global	g_totle_write_sector
	.global	g_totle_write_page_count
	.global	g_totle_gc_page_count
	.global	g_gc_blk_index
	.global	g_gc_merge_free_blk_threshold
	.global	g_gc_free_blk_threshold
	.global	g_gc_refresh_block_temp_tbl
	.global	g_free_slc_blk_num
	.global	g_gc_refresh_block_temp_num
	.global	g_gc_bad_block_temp_tbl
	.global	g_gc_bad_block_gc_index
	.global	g_gc_bad_block_temp_num
	.global	g_gc_next_blk_3
	.global	g_gc_next_blk_2
	.global	g_gc_next_blk_1
	.global	g_gc_next_blk
	.global	g_gc_cur_blk_max_valid_pages
	.global	g_gc_cur_blk_valid_pages
	.global	g_gc_page_offset
	.global	g_gc_blk_num
	.global	p_gc_blk_tbl
	.global	p_gc_page_info
	.global	g_sys_ext_data
	.global	g_sys_save_data
	.global	gp_last_act_superblock
	.global	g_gc_superblock
	.global	g_gc_temp_superblock
	.global	g_buffer_superblock
	.global	g_active_superblock
	.global	g_num_data_superblocks
	.global	g_num_free_superblocks
	.global	p_data_block_list_tail
	.global	p_data_block_list_head
	.global	p_free_data_block_list_head
	.global	p_data_block_list_table
	.global	g_l2p_last_update_region_id
	.global	p_l2p_map_buf
	.global	p_l2p_ram_map
	.global	g_totle_vendor_block
	.global	p_vendor_region_ppn_table
	.global	p_vendor_block_ver_table
	.global	p_vendor_block_valid_page_count
	.global	p_vendor_block_table
	.global	g_totle_map_block
	.global	p_map_region_ppn_check_table
	.global	p_map_region_ppn_table
	.global	p_map_block_ver_table
	.global	p_map_block_valid_page_count
	.global	p_map_block_table
	.global	p_blk_mode_table
	.global	p_valid_page_count_check_table
	.global	p_valid_page_count_table
	.global	g_totle_swl_count
	.global	p_swl_mul_table
	.global	p_erase_count_table
	.global	g_ect_tbl_info_size
	.global	gp_ect_tbl_info
	.global	g_gc_num_req
	.global	c_gc_page_buf_num
	.global	gp_gc_page_buf_info
	.global	p_gc_data_buf
	.global	p_gc_spare_buf
	.global	p_io_spare_buf
	.global	p_io_data_buf_1
	.global	p_io_data_buf_0
	.global	p_sys_spare_buf
	.global	p_vendor_data_buf
	.global	p_sys_data_buf_1
	.global	p_sys_data_buf
	.global	g_wr_page_num
	.global	req_wr_io
	.global	c_wr_page_buf_num
	.global	p_wr_io_data_buf
	.global	p_wr_io_spare_buf
	.global	p_plane_order_table
	.global	g_req_cache
	.global	req_gc_dst
	.global	req_gc
	.global	req_erase
	.global	req_prgm
	.global	req_read
	.global	req_sys
	.global	gVendorBlkInfo
	.global	gL2pMapInfo
	.global	gSysFreeQueue
	.global	gSysInfo
	.global	gBbtInfo
	.global	g_flash_read_only_en
	.global	g_inkDie_check_enable
	.global	g_SlcPartLbaEndSector
	.global	g_MaxLbn
	.global	g_VaildLpn
	.global	g_MaxLpn
	.global	g_MaxLbaSector
	.global	g_GlobalDataVersion
	.global	g_GlobalSysVersion
	.global	ftl_gc_temp_power_lost_recovery_flag
	.global	c_ftl_nand_max_data_blks
	.global	c_ftl_nand_data_op_blks_per_plane
	.global	c_ftl_nand_data_blks_per_plane
	.global	c_ftl_nand_max_sys_blks
	.global	c_ftl_nand_init_sys_blks_per_plane
	.global	c_ftl_nand_sys_blks_per_plane
	.global	c_ftl_vendor_part_size
	.global	c_ftl_nand_max_vendor_blks
	.global	c_ftl_nand_max_map_blks
	.global	c_ftl_nand_map_blks_per_plane
	.global	c_ftl_nand_vendor_region_num
	.global	c_ftl_nand_l2pmap_ram_region_num
	.global	c_ftl_nand_map_region_num
	.global	c_ftl_nand_totle_phy_blks
	.global	c_ftl_nand_reserved_blks
	.global	c_ftl_nand_byte_pre_oob
	.global	c_ftl_nand_byte_pre_page
	.global	c_ftl_nand_sec_pre_page_shift
	.global	c_ftl_nand_sec_pre_page
	.global	c_ftl_nand_page_pre_super_blk
	.global	c_ftl_nand_page_pre_slc_blk
	.global	c_ftl_nand_page_pre_blk
	.global	c_ftl_nand_bbm_buf_size
	.global	c_ftl_nand_ext_blk_pre_plane
	.global	c_ftl_nand_blk_pre_plane
	.global	c_ftl_nand_planes_num
	.global	c_ftl_nand_blks_per_die_shift
	.global	c_ftl_nand_blks_per_die
	.global	c_ftl_nand_planes_per_die
	.global	c_ftl_nand_die_num
	.global	c_ftl_nand_type
	.global	gMasterTempBuf
	.global	gMasterInfo
	.global	gNandcDumpWriteEn
	.global	gToggleModeClkDiv
	.global	gBootDdrMode
	.global	gNandcEccBits
	.global	gpNandc1
	.global	gpNandc
	.global	g_nandc_version_data
	.global	gNandcVer
	.global	gNandChipMap
	.global	gNandIDataBuf
	.global	FlashDdrTunningReadCount
	.global	FlashWaitBusyScheduleEn
	.global	gNandPhyInfo
	.global	gFlashProgCheckSpareBuffer
	.global	gFlashProgCheckBuffer
	.global	gFlashSpareBuffer
	.global	gFlashPageBuffer1
	.global	gFlashPageBuffer0
	.global	gpFlashSaveInfo
	.global	gReadRetryInfo
	.global	gpNandParaInfo
	.global	gNandOptPara
	.global	g_slc2KBNand
	.global	gNandIDBResBlkNumSaveInFlash
	.global	gNandIDBResBlkNum
	.global	gNandFlashResEndPageAddr
	.global	gNandFlashInfoBlockAddr
	.global	gNandFlashIdbBlockAddr
	.global	gNandFlashInfoBlockEcc
	.global	gNandFlashIDBEccBits
	.global	gNandFlashEccBits
	.global	gNandRandomizer
	.global	gBlockPageAlignSize
	.global	gTotleBlock
	.global	gNandMaxChip
	.global	gNandMaxDie
	.global	gFlashInterfaceMode
	.global	gFlashSlcMode
	.global	gFlashOnfiModeEn
	.global	gFlashToggleModeEn
	.global	gFlashSdrModeEn
	.global	gMultiPageProgEn
	.global	gMultiPageReadEn
	.global	gpReadRetrial
	.global	mlcPageToSlcPageTbl
	.global	slcPageToMlcPageTbl
	.global	DieAddrs
	.global	gDieOp
	.global	DieCsIndex
	.global	read_retry_cur_offset
	.section	.rodata
	.align	3
.LANCHOR3 = . + 0
	.type	__func__.19520, %object
	.size	__func__.19520, 11
__func__.19520:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20261, %object
	.size	__func__.20261, 12
__func__.20261:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.20293, %object
	.size	__func__.20293, 17
__func__.20293:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.20312, %object
	.size	__func__.20312, 16
__func__.20312:
	.string	"FtlScanAllBlock"
	.type	__func__.20560, %object
	.size	__func__.20560, 21
__func__.20560:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19593, %object
	.size	__func__.19593, 8
__func__.19593:
	.string	"FtlInit"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.string	"BBT:"
.LC1:
	.string	"FlashEraseBlocks pageAddr error %x\n"
.LC2:
	.string	"otp error! %d"
.LC3:
	.string	"rr"
.LC4:
	.string	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\n"
.LC5:
	.string	"nandc:"
.LC6:
	.string	"%d flReg.d32=%x %x\n"
.LC7:
	.string	"sdr read ok %x ecc=%d\n"
.LC8:
	.string	"sync para %d\n"
.LC9:
	.string	"TOG mode Read error %x %x\n"
.LC10:
	.string	"read retry status %x %x %x\n"
.LC11:
	.string	"micron RR %d row=%x,count %d,status=%d\n"
.LC12:
	.string	"ECC:%d\n"
.LC13:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC14:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC15:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC16:
	.string	"data:"
.LC17:
	.string	"spare:"
.LC18:
	.string	"ReadRetry pageadd=%x ecc=%x err=%x\n"
.LC19:
	.string	"FLFB:%d %d\n"
.LC20:
	.string	"prog error: = %x\n"
.LC21:
	.string	"prog read error: = %x\n"
.LC22:
	.string	"prog read s error: = %x %x %x\n"
.LC23:
	.string	"prog read d error: = %x %x %x\n"
.LC24:
	.string	"FlashMakeFactorBbt %d\n"
.LC25:
	.string	"bad block:%d %d\n"
.LC26:
	.string	"FMFB:%d %d\n"
.LC27:
	.string	"E:bad block:%d\n"
.LC28:
	.string	"FMFB:Save %d %d\n"
.LC29:
	.string	"%s error allocating memory. return -1\n"
.LC30:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC31:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC32:
	.string	"FtlBbmTblFlush error:%x\n"
.LC33:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC34:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC35:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC36:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC37:
	.string	"FtlMapWritePage error = %x\n"
.LC38:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC39:
	.string	"page map lost: %x %x\n"
.LC40:
	.string	"region_id = %x phyAddr = %x\n"
.LC41:
	.string	"map_ppn:"
.LC42:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC43:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC44:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC45:
	.string	"%s\n"
.LC46:
	.string	"no ect"
.LC47:
	.string	"...%s enter...\n"
.LC48:
	.string	"vpc1"
.LC49:
	.string	"vpc2"
.LC50:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC51:
	.string	"free blk vpc error %x = %x  %x\n"
.LC52:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC53:
	.string	":"
.LC54:
	.string	"Ftlscanalldata = %x\n"
.LC55:
	.string	"scan lpa = %x ppa= %x\n"
.LC56:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC57:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC58:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC59:
	.string	"Mblk:"
.LC60:
	.string	"L2P:"
.LC61:
	.string	"L2PC:"
.LC62:
	.string	"superBlkID = %x vpc=%x\n"
.LC63:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC64:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC65:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC66:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC67:
	.string	"slc mode"
.LC68:
	.string	"FLASH INFO:\n"
.LC69:
	.string	"FLASH ID: %x\n"
.LC70:
	.string	"Device Capacity: %d MB\n"
.LC71:
	.string	"FMWAIT: %x %x %x %x\n"
.LC72:
	.string	"FTL INFO:\n"
.LC73:
	.string	"g_MaxLpn = 0x%x\n"
.LC74:
	.string	"g_VaildLpn = 0x%x\n"
.LC75:
	.string	"read_page_count = 0x%x\n"
.LC76:
	.string	"discard_page_count = 0x%x\n"
.LC77:
	.string	"write_page_count = 0x%x\n"
.LC78:
	.string	"cache_write_count = 0x%x\n"
.LC79:
	.string	"l2p_write_count = 0x%x\n"
.LC80:
	.string	"gc_page_count = 0x%x\n"
.LC81:
	.string	"totle_write = %d MB\n"
.LC82:
	.string	"totle_read = %d MB\n"
.LC83:
	.string	"GSV = 0x%x\n"
.LC84:
	.string	"GDV = 0x%x\n"
.LC85:
	.string	"bad blk num = %d %d\n"
.LC86:
	.string	"free_superblocks = 0x%x\n"
.LC87:
	.string	"mlc_EC = 0x%x\n"
.LC88:
	.string	"slc_EC = 0x%x\n"
.LC89:
	.string	"avg_EC = 0x%x\n"
.LC90:
	.string	"sys_EC = 0x%x\n"
.LC91:
	.string	"max_EC = 0x%x\n"
.LC92:
	.string	"min_EC = 0x%x\n"
.LC93:
	.string	"PLT = 0x%x\n"
.LC94:
	.string	"POT = 0x%x\n"
.LC95:
	.string	"MaxSector = 0x%x\n"
.LC96:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC97:
	.string	"sys_blks_pp = 0x%x\n"
.LC98:
	.string	"free sysblock = 0x%x\n"
.LC99:
	.string	"data_blks_pp = 0x%x\n"
.LC100:
	.string	"data_op_blks_pp = 0x%x\n"
.LC101:
	.string	"max_data_blks = 0x%x\n"
.LC102:
	.string	"Sys.id = 0x%x\n"
.LC103:
	.string	"Bbt.id = 0x%x\n"
.LC104:
	.string	"ACT.page = 0x%x\n"
.LC105:
	.string	"ACT.plane = 0x%x\n"
.LC106:
	.string	"ACT.id = 0x%x\n"
.LC107:
	.string	"ACT.mode = 0x%x\n"
.LC108:
	.string	"ACT.a_pages = 0x%x\n"
.LC109:
	.string	"ACT VPC = 0x%x\n"
.LC110:
	.string	"BUF.page = 0x%x\n"
.LC111:
	.string	"BUF.plane = 0x%x\n"
.LC112:
	.string	"BUF.id = 0x%x\n"
.LC113:
	.string	"BUF.mode = 0x%x\n"
.LC114:
	.string	"BUF.a_pages = 0x%x\n"
.LC115:
	.string	"BUF VPC = 0x%x\n"
.LC116:
	.string	"TMP.page = 0x%x\n"
.LC117:
	.string	"TMP.plane = 0x%x\n"
.LC118:
	.string	"TMP.id = 0x%x\n"
.LC119:
	.string	"TMP.mode = 0x%x\n"
.LC120:
	.string	"TMP.a_pages = 0x%x\n"
.LC121:
	.string	"GC.page = 0x%x\n"
.LC122:
	.string	"GC.plane = 0x%x\n"
.LC123:
	.string	"GC.id = 0x%x\n"
.LC124:
	.string	"GC.mode = 0x%x\n"
.LC125:
	.string	"GC.a_pages = 0x%x\n"
.LC126:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC127:
	.string	"Read Err = 0x%x\n"
.LC128:
	.string	"Prog Err = 0x%x\n"
.LC129:
	.string	"gc_free_blk_th= 0x%x\n"
.LC130:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC131:
	.string	"gc_skip_write_count= 0x%x\n"
.LC132:
	.string	"gc_blk_index= 0x%x\n"
.LC133:
	.string	"free min EC= 0x%x\n"
.LC134:
	.string	"free max EC= 0x%x\n"
.LC135:
	.string	"GC__SB VPC = 0x%x\n"
.LC136:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC137:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC138:
	.string	"FTL version: 5.0.50 20180905"
.LC139:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC140:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC141:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC142:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC143:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC144:
	.string	"decrement_vpc_count %x = %d\n"
.LC145:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC146:
	.string	"RSB refresh addr %x\n"
.LC147:
	.string	"spuer block %x vpn is 0\n "
.LC148:
	.string	"g_recovery_ppa %x ver %x\n "
.LC149:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC150:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC151:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC152:
	.string	"g_gc_superblock_free %x %x %x %x %x\n"
.LC153:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC154:
	.string	"GC des block %x done\n"
.LC155:
	.string	"...%s: no bad block mapping table, format device\n"
.LC156:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC157:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC158:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC159:
	.string	"erase power lost blk = %x vpc=%x\n"
	.data
	.align	3
.LANCHOR1 = . + 0
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
	.type	gNandParaInfo, %object
	.size	gNandParaInfo, 32
gNandParaInfo:
	.byte	0
	.byte	0
	.zero	5
	.byte	0
	.byte	1
	.byte	8
	.hword	128
	.byte	2
	.byte	1
	.hword	2048
	.hword	0
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.type	NandFlashParaTbl, %object
	.size	NandFlashParaTbl, 2464
NandFlashParaTbl:
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
	.byte	2
	.hword	2048
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	104
	.byte	4
	.byte	74
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	5
	.byte	44
	.byte	-120
	.byte	4
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-88
	.byte	5
	.byte	-53
	.byte	-87
	.byte	0
	.byte	4
	.byte	2
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	104
	.byte	4
	.byte	70
	.byte	-119
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	72
	.byte	4
	.byte	74
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	1024
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-41
	.byte	-108
	.byte	62
	.byte	-124
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	128
	.byte	2
	.byte	2
	.hword	4096
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	72
	.byte	4
	.byte	70
	.byte	-123
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	1024
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-120
	.byte	5
	.byte	-58
	.byte	-119
	.byte	0
	.byte	4
	.byte	2
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	5
	.byte	44
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	104
	.byte	0
	.byte	39
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.hword	128
	.byte	1
	.byte	2
	.hword	2048
	.hword	287
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-124
	.byte	-59
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	2
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-43
	.byte	-47
	.byte	-90
	.byte	104
	.byte	0
	.byte	4
	.byte	2
	.byte	8
	.hword	64
	.byte	1
	.byte	2
	.hword	2048
	.hword	279
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	44
	.byte	-36
	.byte	-112
	.byte	-90
	.byte	84
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.hword	64
	.byte	1
	.byte	2
	.hword	1024
	.hword	279
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.zero	4
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
	.hword	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.zero	4
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
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.zero	4
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
	.hword	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-46
	.byte	4
	.byte	67
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	473
	.byte	1
	.byte	1
	.byte	24
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-61
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	1024
	.hword	473
	.byte	1
	.byte	2
	.byte	40
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-60
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2090
	.hword	473
	.byte	1
	.byte	4
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-43
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-60
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	530
	.hword	473
	.byte	1
	.byte	3
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-102
	.byte	116
	.byte	66
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	1024
	.hword	281
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-89
	.byte	66
	.byte	72
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
	.byte	0
	.byte	0
	.zero	4
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
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-83
	.byte	-43
	.byte	-108
	.byte	-102
	.byte	116
	.byte	66
	.byte	2
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	1
	.hword	1024
	.hword	273
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	2
	.hword	2048
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	2
	.hword	2048
	.hword	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	36
	.byte	74
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	4
	.byte	74
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	-41
	.byte	-108
	.byte	62
	.byte	-124
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	4
	.byte	70
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.hword	256
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	58
	.byte	-123
	.byte	-109
	.byte	118
	.byte	87
	.byte	1
	.byte	2
	.byte	32
	.hword	256
	.byte	2
	.byte	1
	.hword	2092
	.hword	1505
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	-43
	.byte	-124
	.byte	50
	.byte	114
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	1
	.hword	2056
	.hword	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-108
	.byte	50
	.byte	118
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2058
	.hword	1489
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-126
	.byte	118
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2062
	.hword	1489
	.byte	1
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1497
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	58
	.byte	-107
	.byte	-109
	.byte	122
	.byte	80
	.byte	1
	.byte	2
	.byte	32
	.hword	256
	.byte	2
	.byte	2
	.hword	1066
	.hword	1497
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-108
	.byte	50
	.byte	118
	.byte	85
	.byte	1
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2050
	.hword	401
	.byte	2
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1497
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1473
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	1473
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-92
	.byte	-126
	.byte	118
	.byte	86
	.byte	8
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2082
	.hword	473
	.byte	1
	.byte	65
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.hword	256
	.byte	2
	.byte	1
	.hword	1060
	.hword	1473
	.byte	2
	.byte	67
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-92
	.byte	-126
	.byte	118
	.byte	-41
	.byte	8
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2090
	.hword	1241
	.byte	1
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-34
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
	.hword	2092
	.hword	1473
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	473
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.zero	4
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
	.hword	473
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	58
	.byte	-92
	.byte	-109
	.byte	122
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.hword	256
	.byte	2
	.byte	2
	.hword	2138
	.hword	1497
	.byte	2
	.byte	0
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-126
	.byte	118
	.byte	86
	.byte	8
	.byte	1
	.byte	16
	.hword	256
	.byte	2
	.byte	2
	.hword	2062
	.hword	473
	.byte	1
	.byte	0
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	-41
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
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-41
	.byte	-108
	.byte	126
	.byte	100
	.byte	68
	.byte	0
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2048
	.hword	473
	.byte	2
	.byte	49
	.byte	60
	.byte	36
	.byte	3
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-43
	.byte	126
	.byte	104
	.byte	68
	.byte	0
	.byte	2
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2048
	.hword	505
	.byte	2
	.byte	49
	.byte	60
	.byte	36
	.byte	3
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-41
	.byte	-108
	.byte	122
	.byte	84
	.byte	67
	.byte	0
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2076
	.hword	409
	.byte	2
	.byte	0
	.byte	40
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-43
	.byte	122
	.byte	88
	.byte	67
	.byte	0
	.byte	2
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2076
	.hword	441
	.byte	2
	.byte	0
	.byte	40
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-43
	.byte	-108
	.byte	118
	.byte	84
	.byte	67
	.byte	0
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	1038
	.hword	281
	.byte	2
	.byte	0
	.byte	24
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-20
	.byte	-41
	.byte	20
	.byte	118
	.byte	84
	.byte	-62
	.byte	0
	.byte	1
	.byte	16
	.hword	128
	.byte	2
	.byte	2
	.hword	2076
	.hword	1169
	.byte	2
	.byte	0
	.byte	24
	.byte	40
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.type	NandOptPara, %object
	.size	NandOptPara, 128
NandOptPara:
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
	.type	ToshibaA19RefValue, %object
	.size	ToshibaA19RefValue, 45
ToshibaA19RefValue:
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
	.type	Toshiba15RefValue, %object
	.size	Toshiba15RefValue, 95
Toshiba15RefValue:
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
	.type	ToshibaRefValue, %object
	.size	ToshibaRefValue, 8
ToshibaRefValue:
	.byte	0
	.byte	4
	.byte	124
	.byte	120
	.byte	116
	.byte	8
	.byte	12
	.byte	112
	.type	SamsungRefValue, %object
	.size	SamsungRefValue, 64
SamsungRefValue:
	.byte	-89
	.byte	-92
	.byte	-91
	.byte	-90
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	10
	.byte	0
	.byte	0
	.byte	40
	.byte	0
	.byte	-20
	.byte	-40
	.byte	-19
	.byte	-11
	.byte	-19
	.byte	-26
	.byte	10
	.byte	15
	.byte	5
	.byte	0
	.byte	15
	.byte	10
	.byte	-5
	.byte	-20
	.byte	-24
	.byte	-17
	.byte	-24
	.byte	-36
	.byte	-15
	.byte	-5
	.byte	-2
	.byte	-16
	.byte	10
	.byte	0
	.byte	-5
	.byte	-20
	.byte	-48
	.byte	-30
	.byte	-48
	.byte	-62
	.byte	20
	.byte	15
	.byte	-5
	.byte	-20
	.byte	-24
	.byte	-5
	.byte	-24
	.byte	-36
	.byte	30
	.byte	20
	.byte	-5
	.byte	-20
	.byte	-5
	.byte	-1
	.byte	-5
	.byte	-8
	.byte	7
	.byte	12
	.byte	2
	.byte	0
	.type	refValueDefault, %object
	.size	refValueDefault, 28
refValueDefault:
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
	.type	gSlcNandParaInfo, %object
	.size	gSlcNandParaInfo, 32
gSlcNandParaInfo:
	.byte	2
	.byte	-104
	.byte	-15
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	1
	.byte	1
	.byte	4
	.hword	64
	.byte	1
	.byte	1
	.hword	1024
	.hword	256
	.byte	0
	.byte	0
	.byte	16
	.byte	40
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.zero	4
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
	.type	ftl_gc_temp_block_bops_scan_page_addr, %object
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2
ftl_gc_temp_block_bops_scan_page_addr:
	.hword	-1
	.zero	2
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
	.bss
	.align	3
.LANCHOR0 = . + 0
.LANCHOR2 = . + 4352
.LANCHOR4 = . + 13056
.LANCHOR5 = . + 17408
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 8
gFlashPageBuffer0:
	.zero	8
	.type	gNandFlashIdbBlockAddr, %object
	.size	gNandFlashIdbBlockAddr, 4
gNandFlashIdbBlockAddr:
	.zero	4
	.zero	4
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 8
p_blk_mode_table:
	.zero	8
	.type	g_slc2KBNand, %object
	.size	g_slc2KBNand, 1
g_slc2KBNand:
	.zero	1
	.type	gNandIDBResBlkNum, %object
	.size	gNandIDBResBlkNum, 1
gNandIDBResBlkNum:
	.zero	1
	.zero	2
	.type	gBlockPageAlignSize, %object
	.size	gBlockPageAlignSize, 4
gBlockPageAlignSize:
	.zero	4
	.type	gNandRandomizer, %object
	.size	gNandRandomizer, 1
gNandRandomizer:
	.zero	1
	.zero	7
	.type	gNandChipMap, %object
	.size	gNandChipMap, 64
gNandChipMap:
	.zero	64
	.type	gNandOptPara, %object
	.size	gNandOptPara, 32
gNandOptPara:
	.zero	32
	.type	gFlashSlcMode, %object
	.size	gFlashSlcMode, 1
gFlashSlcMode:
	.zero	1
	.zero	3
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.zero	512
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.zero	1024
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.zero	1
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.zero	1
	.zero	2
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.zero	8
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.zero	32
	.type	gDieOp, %object
	.size	gDieOp, 192
gDieOp:
	.zero	192
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 8
gpNandParaInfo:
	.zero	8
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.zero	2
	.zero	6
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.zero	2048
	.type	gpNandc, %object
	.size	gpNandc, 8
gpNandc:
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
	.type	gNandPhyInfo, %object
	.size	gNandPhyInfo, 28
gNandPhyInfo:
	.zero	28
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.zero	1
	.zero	3
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.zero	4
	.type	FlashWaitBusyScheduleEn, %object
	.size	FlashWaitBusyScheduleEn, 4
FlashWaitBusyScheduleEn:
	.zero	4
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.zero	852
	.zero	4
	.type	read_retry_cur_offset, %object
	.size	read_retry_cur_offset, 4
read_retry_cur_offset:
	.zero	4
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.zero	3
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
	.zero	4
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.zero	1
	.zero	3
	.type	gBootDdrMode, %object
	.size	gBootDdrMode, 4
gBootDdrMode:
	.zero	4
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.zero	4
	.type	gMasterTempBuf, %object
	.size	gMasterTempBuf, 8
gMasterTempBuf:
	.zero	8
	.type	gMasterInfo, %object
	.size	gMasterInfo, 48
gMasterInfo:
	.zero	48
	.type	gNandcDumpWriteEn, %object
	.size	gNandcDumpWriteEn, 4
gNandcDumpWriteEn:
	.zero	4
	.type	gNandcEccBits, %object
	.size	gNandcEccBits, 4
gNandcEccBits:
	.zero	4
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.zero	3
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.zero	4
	.type	FlashDdrTunningReadCount, %object
	.size	FlashDdrTunningReadCount, 4
FlashDdrTunningReadCount:
	.zero	4
	.zero	4
	.type	gpReadRetrial, %object
	.size	gpReadRetrial, 8
gpReadRetrial:
	.zero	8
	.type	gpFlashSaveInfo, %object
	.size	gpFlashSaveInfo, 8
gpFlashSaveInfo:
	.zero	8
	.type	gNandFlashInfoBlockAddr, %object
	.size	gNandFlashInfoBlockAddr, 4
gNandFlashInfoBlockAddr:
	.zero	4
	.type	gNandIDBResBlkNumSaveInFlash, %object
	.size	gNandIDBResBlkNumSaveInFlash, 1
gNandIDBResBlkNumSaveInFlash:
	.zero	1
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.zero	1
	.zero	2
	.type	gFlashPageBuffer1, %object
	.size	gFlashPageBuffer1, 8
gFlashPageBuffer1:
	.zero	8
	.type	gFlashSpareBuffer, %object
	.size	gFlashSpareBuffer, 8
gFlashSpareBuffer:
	.zero	8
	.type	gFlashProgCheckBuffer, %object
	.size	gFlashProgCheckBuffer, 8
gFlashProgCheckBuffer:
	.zero	8
	.type	gFlashProgCheckSpareBuffer, %object
	.size	gFlashProgCheckSpareBuffer, 8
gFlashProgCheckSpareBuffer:
	.zero	8
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.zero	1
	.zero	7
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.zero	16
	.type	c_ftl_nand_sys_blks_per_plane, %object
	.size	c_ftl_nand_sys_blks_per_plane, 4
c_ftl_nand_sys_blks_per_plane:
	.zero	4
	.type	c_ftl_nand_planes_num, %object
	.size	c_ftl_nand_planes_num, 2
c_ftl_nand_planes_num:
	.zero	2
	.zero	2
	.type	c_ftl_nand_max_sys_blks, %object
	.size	c_ftl_nand_max_sys_blks, 4
c_ftl_nand_max_sys_blks:
	.zero	4
	.type	c_ftl_nand_data_blks_per_plane, %object
	.size	c_ftl_nand_data_blks_per_plane, 2
c_ftl_nand_data_blks_per_plane:
	.zero	2
	.type	c_ftl_nand_blk_pre_plane, %object
	.size	c_ftl_nand_blk_pre_plane, 2
c_ftl_nand_blk_pre_plane:
	.zero	2
	.type	c_ftl_nand_max_data_blks, %object
	.size	c_ftl_nand_max_data_blks, 4
c_ftl_nand_max_data_blks:
	.zero	4
	.type	c_ftl_nand_totle_phy_blks, %object
	.size	c_ftl_nand_totle_phy_blks, 4
c_ftl_nand_totle_phy_blks:
	.zero	4
	.type	c_ftl_nand_type, %object
	.size	c_ftl_nand_type, 2
c_ftl_nand_type:
	.zero	2
	.type	c_ftl_nand_die_num, %object
	.size	c_ftl_nand_die_num, 2
c_ftl_nand_die_num:
	.zero	2
	.type	c_ftl_nand_planes_per_die, %object
	.size	c_ftl_nand_planes_per_die, 2
c_ftl_nand_planes_per_die:
	.zero	2
	.zero	2
	.type	p_plane_order_table, %object
	.size	p_plane_order_table, 32
p_plane_order_table:
	.zero	32
	.type	c_mlc_erase_count_value, %object
	.size	c_mlc_erase_count_value, 2
c_mlc_erase_count_value:
	.zero	2
	.type	c_ftl_nand_ext_blk_pre_plane, %object
	.size	c_ftl_nand_ext_blk_pre_plane, 2
c_ftl_nand_ext_blk_pre_plane:
	.zero	2
	.type	c_ftl_vendor_part_size, %object
	.size	c_ftl_vendor_part_size, 2
c_ftl_vendor_part_size:
	.zero	2
	.type	c_ftl_nand_blks_per_die, %object
	.size	c_ftl_nand_blks_per_die, 2
c_ftl_nand_blks_per_die:
	.zero	2
	.type	c_ftl_nand_blks_per_die_shift, %object
	.size	c_ftl_nand_blks_per_die_shift, 2
c_ftl_nand_blks_per_die_shift:
	.zero	2
	.type	c_ftl_nand_page_pre_blk, %object
	.size	c_ftl_nand_page_pre_blk, 2
c_ftl_nand_page_pre_blk:
	.zero	2
	.type	c_ftl_nand_page_pre_slc_blk, %object
	.size	c_ftl_nand_page_pre_slc_blk, 2
c_ftl_nand_page_pre_slc_blk:
	.zero	2
	.type	c_ftl_nand_page_pre_super_blk, %object
	.size	c_ftl_nand_page_pre_super_blk, 2
c_ftl_nand_page_pre_super_blk:
	.zero	2
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
	.zero	2
	.type	c_ftl_nand_sec_pre_page_shift, %object
	.size	c_ftl_nand_sec_pre_page_shift, 2
c_ftl_nand_sec_pre_page_shift:
	.zero	2
	.type	c_ftl_nand_byte_pre_page, %object
	.size	c_ftl_nand_byte_pre_page, 2
c_ftl_nand_byte_pre_page:
	.zero	2
	.type	c_ftl_nand_byte_pre_oob, %object
	.size	c_ftl_nand_byte_pre_oob, 2
c_ftl_nand_byte_pre_oob:
	.zero	2
	.type	c_ftl_nand_reserved_blks, %object
	.size	c_ftl_nand_reserved_blks, 2
c_ftl_nand_reserved_blks:
	.zero	2
	.zero	2
	.type	DeviceCapacity, %object
	.size	DeviceCapacity, 4
DeviceCapacity:
	.zero	4
	.type	c_ftl_nand_max_vendor_blks, %object
	.size	c_ftl_nand_max_vendor_blks, 2
c_ftl_nand_max_vendor_blks:
	.zero	2
	.type	c_ftl_nand_vendor_region_num, %object
	.size	c_ftl_nand_vendor_region_num, 2
c_ftl_nand_vendor_region_num:
	.zero	2
	.type	c_ftl_nand_map_blks_per_plane, %object
	.size	c_ftl_nand_map_blks_per_plane, 2
c_ftl_nand_map_blks_per_plane:
	.zero	2
	.zero	2
	.type	c_ftl_nand_max_map_blks, %object
	.size	c_ftl_nand_max_map_blks, 4
c_ftl_nand_max_map_blks:
	.zero	4
	.type	c_ftl_nand_init_sys_blks_per_plane, %object
	.size	c_ftl_nand_init_sys_blks_per_plane, 4
c_ftl_nand_init_sys_blks_per_plane:
	.zero	4
	.type	c_ftl_nand_map_region_num, %object
	.size	c_ftl_nand_map_region_num, 2
c_ftl_nand_map_region_num:
	.zero	2
	.type	c_ftl_nand_l2pmap_ram_region_num, %object
	.size	c_ftl_nand_l2pmap_ram_region_num, 2
c_ftl_nand_l2pmap_ram_region_num:
	.zero	2
	.type	g_page_map_check_enable, %object
	.size	g_page_map_check_enable, 2
g_page_map_check_enable:
	.zero	2
	.type	g_free_slc_blk_num, %object
	.size	g_free_slc_blk_num, 2
g_free_slc_blk_num:
	.zero	2
	.type	g_SlcPartLbaEndSector, %object
	.size	g_SlcPartLbaEndSector, 4
g_SlcPartLbaEndSector:
	.zero	4
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.zero	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.zero	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.zero	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.zero	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.zero	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.zero	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.zero	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.zero	4
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.zero	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.zero	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.zero	4
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.zero	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.zero	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.zero	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.zero	4
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.zero	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.zero	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.zero	4
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.zero	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.zero	4
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.zero	2
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.zero	2
	.type	g_gc_next_blk_2, %object
	.size	g_gc_next_blk_2, 2
g_gc_next_blk_2:
	.zero	2
	.type	g_gc_next_blk_3, %object
	.size	g_gc_next_blk_3, 2
g_gc_next_blk_3:
	.zero	2
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
	.zero	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.zero	2
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.zero	2
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.zero	2
	.type	g_gc_refresh_block_temp_num, %object
	.size	g_gc_refresh_block_temp_num, 2
g_gc_refresh_block_temp_num:
	.zero	2
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.zero	2
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.zero	4
	.type	g_wr_page_num, %object
	.size	g_wr_page_num, 4
g_wr_page_num:
	.zero	4
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 8
p_gc_blk_tbl:
	.zero	8
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 8
p_gc_page_info:
	.zero	8
	.type	req_read, %object
	.size	req_read, 8
req_read:
	.zero	8
	.type	req_gc_dst, %object
	.size	req_gc_dst, 8
req_gc_dst:
	.zero	8
	.type	req_prgm, %object
	.size	req_prgm, 8
req_prgm:
	.zero	8
	.type	req_erase, %object
	.size	req_erase, 8
req_erase:
	.zero	8
	.type	req_gc, %object
	.size	req_gc, 8
req_gc:
	.zero	8
	.type	req_wr_io, %object
	.size	req_wr_io, 8
req_wr_io:
	.zero	8
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
	.zero	4
	.zero	4
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 8
p_sys_data_buf:
	.zero	8
	.type	p_sys_data_buf_1, %object
	.size	p_sys_data_buf_1, 8
p_sys_data_buf_1:
	.zero	8
	.type	p_vendor_data_buf, %object
	.size	p_vendor_data_buf, 8
p_vendor_data_buf:
	.zero	8
	.type	p_gc_data_buf, %object
	.size	p_gc_data_buf, 8
p_gc_data_buf:
	.zero	8
	.type	p_wr_io_data_buf, %object
	.size	p_wr_io_data_buf, 8
p_wr_io_data_buf:
	.zero	8
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 8
p_io_data_buf_0:
	.zero	8
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 8
p_io_data_buf_1:
	.zero	8
	.type	gp_gc_page_buf_info, %object
	.size	gp_gc_page_buf_info, 8
gp_gc_page_buf_info:
	.zero	8
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 8
p_sys_spare_buf:
	.zero	8
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 8
p_io_spare_buf:
	.zero	8
	.type	p_gc_spare_buf, %object
	.size	p_gc_spare_buf, 8
p_gc_spare_buf:
	.zero	8
	.type	p_wr_io_spare_buf, %object
	.size	p_wr_io_spare_buf, 8
p_wr_io_spare_buf:
	.zero	8
	.type	g_ect_tbl_info_size, %object
	.size	g_ect_tbl_info_size, 2
g_ect_tbl_info_size:
	.zero	2
	.zero	6
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 8
p_swl_mul_table:
	.zero	8
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 8
gp_ect_tbl_info:
	.zero	8
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 8
p_erase_count_table:
	.zero	8
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 8
p_valid_page_count_check_table:
	.zero	8
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 8
p_valid_page_count_table:
	.zero	8
	.type	p_map_block_table, %object
	.size	p_map_block_table, 8
p_map_block_table:
	.zero	8
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 8
p_map_block_valid_page_count:
	.zero	8
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 8
p_vendor_block_table:
	.zero	8
	.type	p_vendor_block_valid_page_count, %object
	.size	p_vendor_block_valid_page_count, 8
p_vendor_block_valid_page_count:
	.zero	8
	.type	p_vendor_block_ver_table, %object
	.size	p_vendor_block_ver_table, 8
p_vendor_block_ver_table:
	.zero	8
	.type	p_vendor_region_ppn_table, %object
	.size	p_vendor_region_ppn_table, 8
p_vendor_region_ppn_table:
	.zero	8
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 8
p_map_region_ppn_table:
	.zero	8
	.type	p_map_region_ppn_check_table, %object
	.size	p_map_region_ppn_check_table, 8
p_map_region_ppn_check_table:
	.zero	8
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 8
p_map_block_ver_table:
	.zero	8
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 8
p_l2p_ram_map:
	.zero	8
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 8
p_l2p_map_buf:
	.zero	8
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 8
p_data_block_list_table:
	.zero	8
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.zero	2
	.zero	6
	.type	gBbtInfo, %object
	.size	gBbtInfo, 96
gBbtInfo:
	.zero	96
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.zero	2
	.zero	2
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.zero	4
	.type	g_flash_read_only_en, %object
	.size	g_flash_read_only_en, 4
g_flash_read_only_en:
	.zero	4
	.zero	4
	.type	req_sys, %object
	.size	req_sys, 56
req_sys:
	.zero	56
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.zero	2056
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.zero	48
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 8
p_data_block_list_head:
	.zero	8
	.type	p_data_block_list_tail, %object
	.size	p_data_block_list_tail, 8
p_data_block_list_tail:
	.zero	8
	.type	g_num_data_superblocks, %object
	.size	g_num_data_superblocks, 2
g_num_data_superblocks:
	.zero	2
	.zero	6
	.type	p_free_data_block_list_head, %object
	.size	p_free_data_block_list_head, 8
p_free_data_block_list_head:
	.zero	8
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.zero	2
	.zero	6
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.zero	48
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.zero	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.zero	48
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 64
gL2pMapInfo:
	.zero	64
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.zero	2
	.zero	6
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 64
gVendorBlkInfo:
	.zero	64
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.zero	2
	.zero	2
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.zero	4
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.zero	12
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.zero	2
	.zero	2
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.zero	8192
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.zero	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.zero	2
	.zero	2
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.zero	48
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.zero	512
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.zero	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.zero	4
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.zero	2
	.zero	2
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.zero	4
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.zero	4
	.zero	4
	.type	g_req_cache, %object
	.size	g_req_cache, 8
g_req_cache:
	.zero	8
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
	.zero	2
	.zero	2
	.type	g_totle_swl_count, %object
	.size	g_totle_swl_count, 4
g_totle_swl_count:
	.zero	4
	.type	ftl_gc_temp_power_lost_recovery_flag, %object
	.size	ftl_gc_temp_power_lost_recovery_flag, 4
ftl_gc_temp_power_lost_recovery_flag:
	.zero	4
	.type	g_recovery_page_min_ver, %object
	.size	g_recovery_page_min_ver, 4
g_recovery_page_min_ver:
	.zero	4
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.zero	2
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.zero	2
	.zero	4
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.zero	34
	.type	g_power_lost_ecc_error_blk, %object
	.size	g_power_lost_ecc_error_blk, 2
g_power_lost_ecc_error_blk:
	.zero	2
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.zero	2
	.zero	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.zero	4
	.zero	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.zero	128
	.type	gc_ink_free_return_value, %object
	.size	gc_ink_free_return_value, 2
gc_ink_free_return_value:
	.zero	2
	.type	g_gc_cur_blk_valid_pages, %object
	.size	g_gc_cur_blk_valid_pages, 2
g_gc_cur_blk_valid_pages:
	.zero	2
	.type	g_gc_cur_blk_max_valid_pages, %object
	.size	g_gc_cur_blk_max_valid_pages, 2
g_gc_cur_blk_max_valid_pages:
	.zero	2
	.zero	2
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 8
gp_last_act_superblock:
	.zero	8
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.zero	4
	.type	g_LowFormat, %object
	.size	g_LowFormat, 4
g_LowFormat:
	.zero	4
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.zero	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.zero	4
	.type	g_gc_refresh_block_temp_tbl, %object
	.size	g_gc_refresh_block_temp_tbl, 34
g_gc_refresh_block_temp_tbl:
	.zero	34
	.zero	2
	.type	gToggleModeClkDiv, %object
	.size	gToggleModeClkDiv, 4
gToggleModeClkDiv:
	.zero	4
	.type	gpNandc1, %object
	.size	gpNandc1, 8
gpNandc1:
	.zero	8
	.type	gNandFlashResEndPageAddr, %object
	.size	gNandFlashResEndPageAddr, 4
gNandFlashResEndPageAddr:
	.zero	4
	.type	gNandFlashInfoBlockEcc, %object
	.size	gNandFlashInfoBlockEcc, 1
gNandFlashInfoBlockEcc:
	.zero	1
	.type	gFlashOnfiModeEn, %object
	.size	gFlashOnfiModeEn, 1
gFlashOnfiModeEn:
	.zero	1
	.type	gFlashSdrModeEn, %object
	.size	gFlashSdrModeEn, 1
gFlashSdrModeEn:
	.zero	1
