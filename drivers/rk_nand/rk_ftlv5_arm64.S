/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-04-08
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
	cmp	w19, 74
	bne	.L130
	b	.L133
.L132:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,2656]
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
	add	x2, x20, 2656
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
	add	x21, x2, 2832
	add	x25, x2, 2784
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
	add	x0, x19, 2784
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
	ldrsb	w0, [x23,2928]
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
	add	x23, x1, 2940
	mov	x20, 0
	add	x22, x22, :lo12:g_maxRegNum
	mov	w24, 161
	mov	x19, x2
.L177:
	ldrb	w0, [x22]
	cmp	w0, w20
	bls	.L179
	str	w24, [x21,8]
	add	x0, x19, 2936
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
	add	x3, x2, 2784
	adrp	x4, g_retryMode
	add	x2, x2, 2832
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
.L260:
	cmp	w22, w24
	beq	.L269
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1676]
	cmp	w2, w0
	bcc	.L261
	mov	w0, -1
	str	w0, [x20]
	b	.L262
.L261:
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
	tbz	x0, 0, .L263
	mov	w0, -1
	str	w0, [x20]
.L263:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L264
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L264:
	mov	w0, w19
	bl	NandcFlashDeCs
.L262:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L260
.L269:
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
	bne	.L271
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3017
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3017]
	b	.L326
.L271:
	cmp	w26, 3
	bne	.L273
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
	b	.L359
.L273:
	cmp	w26, 4
	bne	.L274
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
.L359:
	mov	w27, 8
	strb	w0, [x1,11]
	mov	w28, w27
	b	.L272
.L274:
	cmp	w26, 5
	bne	.L275
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w27, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L358
.L275:
	cmp	w26, 6
	bne	.L276
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w27, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L358
.L276:
	cmp	w26, 7
	bne	.L326
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
	b	.L272
.L326:
	mov	w27, 7
.L358:
	mov	w28, 4
.L272:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L355
	adrp	x22, .LANCHOR1
	add	x25, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x26, x25, 4056
	add	x3, x25, 40
	add	x22, x22, 3000
.L277:
	ldrb	w0, [x25,1676]
	cmp	w0, w21
	bls	.L284
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
.L279:
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
	bhi	.L279
	mov	x0, 0
.L280:
	add	w2, w0, 8
	mov	x1, 0
.L281:
	add	x5, x1, x0
	add	x1, x1, 4
	add	x5, x22, x5
	cmp	x1, 24
	ldrb	w6, [x5,4]
	ldrb	w5, [x19,x0]
	add	w5, w6, w5
	strb	w5, [x19,w2,sxtw]
	add	w2, w2, 8
	bne	.L281
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L280
	add	w21, w21, 1
	strb	wzr, [x19,16]
	strb	wzr, [x19,24]
	strb	wzr, [x19,32]
	uxtb	w21, w21
	strb	wzr, [x19,40]
	strb	wzr, [x19,48]
	strb	wzr, [x19,41]
	strb	wzr, [x19,49]
	b	.L277
.L355:
	sub	w0, w26, #3
	cmp	w0, 4
	bhi	.L284
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
.L285:
	ldrb	w0, [x25,1676]
	cmp	w0, w21
	bhi	.L325
.L284:
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
.L325:
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
	beq	.L286
	add	x1, x6, x1, lsl 6
	add	x6, x1, 20
	b	.L287
.L286:
	mov	x0, 160
	madd	x1, x1, x0, x6
	add	x6, x1, 28
.L287:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x19,2056]
	bne	.L288
	mov	w0, 255
	str	w0, [x19,2052]
	mov	w0, 64
	str	w0, [x19,2048]
	mov	w0, 204
	b	.L360
.L288:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L290
	ldrb	w0, [x25,4060]
	str	w0, [x19,2052]
	mov	w0, 82
	b	.L361
.L290:
	mov	w0, 174
	str	w0, [x19,2052]
	str	wzr, [x19,2048]
	mov	w0, 176
.L360:
	str	w0, [x19,2052]
	mov	w0, 77
.L361:
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
	bne	.L291
	mov	w0, 31
	str	w0, [x19,2052]
	b	.L292
.L291:
	str	wzr, [x19,2052]
.L292:
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
	bls	.L294
	ldr	x7, [x29,112]
	cmp	w26, 7
	mov	w1, 32
	csel	w1, w1, w7, eq
.L294:
	ldr	x0, [x25]
	mov	x7, 0
.L295:
	ldr	w8, [x19,2048]
	strb	w8, [x0,x7]
	add	x7, x7, 1
	cmp	w1, w7, uxtb
	bhi	.L295
	cmp	w26, 7
	bne	.L296
	mov	w1, 0
.L298:
	ldrb	w7, [x0]
	cmp	w7, 12
	beq	.L297
	ldrb	w7, [x0,1]
	cmp	w7, 10
	beq	.L297
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L298
	b	.L299
.L297:
	cmp	w1, 7
	bne	.L300
.L299:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L301:
	b	.L301
.L296:
	cmp	w26, 6
	bne	.L300
	mov	x1, 0
.L302:
	ldrb	w7, [x0,x1]
	cmp	w7, 12
	beq	.L300
	add	x7, x0, x1
	ldrb	w7, [x7,8]
	cmp	w7, 4
	beq	.L300
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L302
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L304:
	b	.L304
.L300:
	ldr	x9, [x25]
	mov	x0, 0
.L305:
	ldr	w1, [x29,124]
	cmp	w1, w0
	ble	.L362
	ldr	w1, [x19,2048]
	strb	w1, [x9,x0]
	add	x0, x0, 1
	b	.L305
.L362:
	ldr	x11, [x25]
	mov	w8, w22
	mov	w7, 8
.L308:
	mov	w0, 0
.L307:
	add	w1, w0, w8
	add	w0, w0, 1
	sbfiz	x1, x1, 2, 32
	cmp	w0, w22
	ldr	w10, [x11,x1]
	mvn	w10, w10
	str	w10, [x11,x1]
	bne	.L307
	ldr	w0, [x29,120]
	subs	w7, w7, #1
	add	w8, w8, w0
	bne	.L308
	mov	x7, 0
	mov	w15, 1
.L309:
	mov	w1, 0
	ldr	x14, [x25]
	mov	w10, w1
.L313:
	lsl	w8, w15, w10
	mov	w12, w7
	mov	w0, 16
	mov	w11, 0
.L311:
	ldr	w13, [x14,w12,sxtw 2]
	add	w12, w12, w22
	and	w13, w8, w13
	cmp	w13, w8
	csinc	w11, w11, w11, ne
	subs	w0, w0, #1
	bne	.L311
	cmp	w11, 9
	orr	w8, w1, w8
	add	w10, w10, 1
	csel	w1, w8, w1, cs
	cmp	w10, 32
	bne	.L313
	str	w1, [x14,x7,lsl 2]
	add	x7, x7, 1
	cmp	w22, w7
	bgt	.L309
	mov	x7, 0
	ldr	x1, [x25]
	mov	w8, w7
.L316:
	ldr	w10, [x1,x7]
	add	x7, x7, 4
	cmp	w10, wzr
	csinc	w8, w8, w8, ne
	cmp	x7, 32
	bne	.L316
	cmp	w8, 7
	ble	.L317
	adrp	x0, .LC3
	mov	w2, 1
	add	x0, x0, :lo12:.LC3
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L318:
	b	.L318
.L317:
	cmp	w26, 6
	mov	w7, 4
	beq	.L319
	cmp	w26, 7
	mov	w7, 10
	mov	w1, 8
	csel	w7, w1, w7, ne
.L319:
	mov	w8, 0
.L320:
	mov	x1, 0
.L321:
	add	w10, w0, w1
	ldrb	w11, [x9,x1]
	add	x1, x1, 1
	cmp	w28, w1, uxtb
	strb	w11, [x6,w10,sxtw]
	bhi	.L321
	add	w8, w8, 1
	add	x9, x9, x23
	cmp	w8, w27
	add	w0, w0, w7
	blt	.L320
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w24
	bl	NandcWaitFlashReady
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L323
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
	b	.L324
.L323:
	mov	w0, 56
	str	w0, [x19,2056]
.L324:
	mov	w0, w24
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L285
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L363
	sub	w2, w0, #1
	cmp	w2, 6
	bhi	.L365
	bl	HynixGetReadRetryDefault
	b	.L363
.L365:
	cmp	w0, 49
	bne	.L366
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
	add	x1, x1, 2936
	b	.L381
.L366:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L372
	cmp	w1, 33
	bne	.L367
.L372:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 4
	b	.L382
.L367:
	cmp	w1, 67
	beq	.L373
	cmp	w1, 34
	bne	.L369
.L373:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 5
.L382:
	strb	w1, [x0,4057]
	mov	w1, 7
	strb	w1, [x0,4058]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 4060
	add	x1, x1, 2784
	mov	w2, 45
	b	.L381
.L369:
	cmp	w1, 68
	beq	.L374
	cmp	w1, 35
	bne	.L363
.L374:
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
	add	x1, x1, 2832
.L381:
	bl	ftl_memcpy
.L363:
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
	bne	.L384
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
	b	.L385
.L384:
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
.L385:
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
	add	x19, x19, :lo12:.LANCHOR2
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	w0, [x19,565]
	cbz	w0, .L387
	ldrb	w0, [x19,564]
	tbz	x0, 0, .L387
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19,565]
.L387:
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
	ble	.L396
	ldr	x0, [x1,3976]
	mov	w1, 8354
	b	.L403
.L396:
	cmp	w0, 220
	ble	.L398
	ldr	x0, [x1,3976]
	b	.L404
.L398:
	cmp	w0, 185
	ble	.L399
	ldr	x0, [x1,3976]
	mov	w1, 4226
	b	.L403
.L399:
	cmp	w0, 160
	ldr	x0, [x1,3976]
	ble	.L400
	mov	w1, 4194
	b	.L403
.L400:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L403
	cmp	w19, 99
	mov	w1, 4225
	bls	.L403
.L404:
	mov	w1, 8322
.L403:
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
	bls	.L406
	mov	w1, 8322
	cmp	w0, w1
	bne	.L407
.L406:
	adrp	x1, .LANCHOR0+3976
	ldr	x1, [x1,#:lo12:.LANCHOR0+3976]
	str	w0, [x1,4]
.L407:
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
	str	w4, [x3,568]
	ldr	w4, [x0,352]
	and	w2, w2, 245760
	orr	w2, w2, 256
	ubfx	x4, x4, 16, 4
	str	w4, [x3,572]
	ldr	w4, [x0,352]
	str	w4, [x3,576]
	cmp	w4, 2049
	bne	.L409
	mov	w1, 8
	str	w1, [x3,572]
.L409:
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
	bne	.L412
.L415:
	and	w1, w1, -17
	b	.L413
.L412:
	cmp	w0, 24
	bne	.L414
	orr	w1, w1, 16
	b	.L413
.L414:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L415
.L413:
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
.L422:
	lsl	x0, x19, 3
	ldrb	w1, [x0,x20]
	sub	w1, w1, #1
	uxtb	w1, w1
	cmp	w1, 253
	bhi	.L421
	mov	w0, w19
	bl	FlashReset
.L421:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L422
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,565]
	cbz	w0, .L423
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,564]
	bl	NandcSetMode
	lsr	w0, w21, 8
	bl	NandcSetDdrPara
.L423:
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
	tbnz	x0, 9, .L430
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L429
.L430:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L429:
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
	cbz	w2, .L433
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L434
.L433:
	mov	w27, 56
	adrp	x28, .LANCHOR2
	add	x25, x20, 1720
.L464:
	cmp	w19, w23
	bcs	.L465
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
	bcc	.L435
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L436
.L435:
	add	x1, x28, :lo12:.LANCHOR2
	mov	x2, 24
	ldrb	w1, [x1,648]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x2, x25
	ldr	x1, [x1,8]
	cbz	x1, .L438
	bl	FlashWaitCmdDone
.L438:
	ldr	w1, [x29,124]
	mov	x0, 24
	ldr	w2, [x29,120]
	madd	x0, x1, x0, x25
	str	x21, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w22, .L439
	add	w2, w19, 1
	umull	x2, w2, w27
	add	x2, x24, x2
	str	x2, [x0,16]
.L439:
	add	x0, x20, x1
	ldrb	w21, [x0,1680]
	mov	x0, 24
	mul	x1, x1, x0
	mov	w0, w21
	strb	w21, [x25,x1]
	bl	NandcFlashCs
	cmp	w26, 1
	bne	.L440
	ldrb	w0, [x20,136]
	cbz	w0, .L440
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L441
.L440:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L441:
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
.L436:
	add	w19, w19, 1
	b	.L464
.L465:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x21, 24
	add	x19, x0, :lo12:.LANCHOR0
	ldr	x0, [x19,3976]
	bl	NandcIqrWaitFlashReady
.L443:
	ldrb	w0, [x19,1676]
	cmp	w0, w20
	bls	.L466
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w26, 1
	bne	.L444
	ldrb	w0, [x19,136]
	cbz	w0, .L444
	madd	x0, x20, x21, x19
	ldrb	w0, [x0,1720]
	bl	flash_exit_slc_mode
.L444:
	add	x20, x20, 1
	b	.L443
.L466:
	adrp	x0, .LANCHOR2+652
	ldr	w0, [x0,#:lo12:.LANCHOR2+652]
	cbnz	w0, .L446
.L448:
	mov	w0, 0
	b	.L434
.L446:
	adrp	x0, IDByte
	ldrb	w0, [x0,#:lo12:IDByte]
	cmp	w0, 69
	bne	.L448
	mov	x0, 0
	mov	x1, 56
.L447:
	cmp	w23, w0
	bls	.L448
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L447
.L434:
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
.L469:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L469
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
	ldr	w0, [x0,572]
	bfi	w20, w3, 4, 1
	cmp	w0, 3
	bls	.L473
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w7, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w7, .L487
	cbz	x22, .L474
.L487:
	cbnz	w25, .L476
.L484:
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
	cbnz	x22, .L478
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,592]
	b	.L478
.L476:
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
.L480:
	cmp	w3, w8
	bcs	.L484
	lsr	w0, w2, 2
	ubfiz	x0, x0, 2, 30
	cbz	w7, .L481
	ldrh	w6, [x5,2]
	ldr	x10, [x1,600]
	ldrh	w11, [x5],4
	orr	w6, w11, w6, lsl 16
	str	w6, [x10,x0]
	b	.L482
.L481:
	ldr	x6, [x1,600]
	str	w9, [x6,x0]
.L482:
	add	w3, w3, 1
	add	w2, w2, w4
	b	.L480
.L478:
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
	cbnz	x22, .L485
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L485:
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
.L474:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L473:
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
	ldr	w0, [x0,572]
	cmp	w0, 3
	bls	.L522
	ldr	w0, [x19,16]
	tbz	x0, 2, .L522
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L493
	adrp	x22, .LC4
	adrp	x23, .LC5
	mov	w21, 0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
.L494:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L502
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,572]
	cmp	w0, 5
	bhi	.L495
.L498:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L494
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
	b	.L494
.L495:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L498
	ldr	w0, [x29,72]
	tbz	x0, 17, .L498
.L502:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 592
	ldr	w0, [x19,40]
	cbz	w0, .L503
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
	b	.L503
.L493:
	adrp	x22, .LC6
	adrp	x23, .LC5
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
.L504:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L530
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L504
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
	b	.L504
.L530:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,640]
	cbz	w0, .L507
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L507:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 592
	ldr	w0, [x21,40]
	cbz	w0, .L508
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
.L508:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,640]
	cbz	w0, .L503
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L503:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20,632]
	b	.L491
.L522:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L522
.L491:
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
	bne	.L532
	cbz	x3, .L533
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L533:
	cbz	x20, .L531
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
	b	.L531
.L532:
	cbz	x3, .L536
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L536:
	cbz	x20, .L531
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
.L531:
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
	cbnz	x0, .L548
	cbnz	x4, .L549
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x21, x29, 112
	bl	ftl_memset
.L549:
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
	cbnz	w20, .L550
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
.L552:
	cmp	x21, x3
	add	w5, w1, w4
	beq	.L594
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
	b	.L552
.L594:
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w23, w23, 2
	ldr	w4, [x0,644]
	ldr	w5, [x0,572]
	mov	w0, 0
	mov	w22, w0
.L554:
	cmp	w0, w23
	bcs	.L550
	cbz	w4, .L550
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L578
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L578
	cmp	w5, 5
	bls	.L556
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
	bls	.L557
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L558
.L557:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L593
.L556:
	cmp	w5, 3
	bls	.L558
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
	bls	.L559
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L593
.L559:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L593:
	orr	w3, w1, w3, lsl 5
.L558:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L555
.L578:
	mov	w22, -1
.L555:
	add	w0, w0, 1
	b	.L554
.L550:
	str	wzr, [x19,16]
	b	.L561
.L548:
	cmp	w20, 1
	bne	.L592
	mov	w22, 0
	mov	w27, 2
.L562:
	cmp	w22, w23
	bcs	.L595
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L564
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L564:
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
	b	.L562
.L595:
	mov	w22, 0
	b	.L561
.L592:
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
.L567:
	cmp	w26, w23
	bcs	.L561
	mov	w0, w24
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L568
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L568:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L583
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L569
.L583:
	mov	w22, -1
.L569:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L570
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L570:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L567
.L561:
	cbnz	w20, .L573
	adrp	x0, .LANCHOR2+572
	ldr	w0, [x0,#:lo12:.LANCHOR2+572]
	cmp	w0, 5
	bls	.L573
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L573
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L573:
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
	cbnz	w19, .L597
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,25]
	ldr	w0, [x0,28]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L597:
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
	ldr	w0, [x27,572]
	cmp	w0, 8
	mov	w0, 12
	csel	w22, w22, w0, cc
	cbz	w4, .L600
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
	bne	.L601
.L610:
	mov	w21, -1
	b	.L602
.L601:
	adrp	x0, .LC7
	mov	w1, w25
	add	x0, x0, :lo12:.LC7
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L603
	add	x20, x20, x24, sxtw 4
	ldr	x0, [x20,40]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L603:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w0, [x19,656]
	add	w0, w0, 1
	str	w0, [x19,656]
	cmp	w0, 2047
	bls	.L602
	mov	x23, 0
	str	wzr, [x19,656]
	mov	x28, x23
.L600:
	mov	w19, 0
	mov	w27, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L608:
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
	bhi	.L604
	cmp	w0, 2
	bhi	.L614
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L614
	sub	w19, w22, w20
	mov	w21, w0
	mov	w27, 0
	b	.L606
.L604:
	cmp	w6, w20
	bcs	.L615
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L616
	mov	w6, w20
	b	.L615
.L614:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w27, 0
	mov	x28, x23
	b	.L605
.L615:
	mov	w20, 0
.L605:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L608
.L606:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L607
.L616:
	mov	w19, w5
.L607:
	cbz	w19, .L609
	adrp	x0, .LC8
	mov	w1, w19
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L609:
	cbz	w27, .L602
	adrp	x0, .LC9
	mov	w1, w24
	add	x0, x0, :lo12:.LC9
	mov	w2, w25
	bl	printk
	cbz	w26, .L610
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L602:
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
	bne	.L626
	adrp	x21, .LANCHOR0
	add	x26, x21, :lo12:.LANCHOR0
	ldrb	w27, [x26,32]
	cbnz	w27, .L627
.L629:
	add	x0, x23, :lo12:.LANCHOR2
	ldrb	w0, [x0,565]
	cbz	w0, .L626
	b	.L647
.L627:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	strb	wzr, [x26,32]
	bl	FlashReadRawPage
	strb	w27, [x26,32]
	cmn	w0, #1
	beq	.L629
	mov	w19, w0
	b	.L626
.L647:
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
	beq	.L630
	ldrb	w0, [x21,4044]
	cmp	w19, w0, lsr 1
	bls	.L626
.L630:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
.L626:
	add	x21, x23, :lo12:.LANCHOR2
	cmn	w19, #1
	ldr	x4, [x21,664]
	bne	.L631
	cbz	x4, .L631
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
	bne	.L631
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L631
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
.L631:
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
	beq	.L652
	cmn	w22, #1
	bne	.L649
.L652:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20,564]
	tbz	x0, 0, .L649
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,565]
	b	.L651
.L649:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,565]
.L651:
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
.L660:
	add	w26, w20, 1
	mov	x22, 0
.L662:
	add	x0, x29, 104
	ldrb	w0, [x22,x0]
	bl	FlashBchSel
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L661
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w26
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L661
	add	x22, x22, 1
	cmp	x22, 4
	beq	.L663
	b	.L662
.L664:
	mov	w1, 2036
	add	x0, x26, 12
	mov	w24, -1
	bl	JSHash
	ldr	w1, [x26,8]
	cmp	w1, w0
	beq	.L674
.L663:
	subs	w27, w27, #1
	add	w20, w20, w28
	bne	.L660
	mov	w0, w27
.L673:
	bl	flash_exit_slc_mode
	mov	w0, w24
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L661:
	add	x22, x25, :lo12:.LANCHOR2
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x26, [x22,672]
	ldr	w1, [x26]
	cmp	w1, w0
	bne	.L663
	cbnz	w24, .L664
	add	x23, x23, :lo12:.LANCHOR1
	add	x19, x19, :lo12:.LANCHOR0
	ldrh	w0, [x23,266]
	udiv	w20, w20, w0
	mov	w0, w24
	add	w20, w20, 1
	str	w20, [x19,8]
	b	.L673
.L674:
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
	strb	w0, [x22,565]
	ldrh	w0, [x21,10]
	udiv	w0, w20, w0
	add	w2, w0, 1
	cbz	w0, .L666
	str	w2, [x24,8]
	b	.L667
.L666:
	mov	w0, 2
	str	w0, [x24,8]
.L667:
	add	x0, x25, :lo12:.LANCHOR2
	ldrh	w1, [x1,14]
	mov	w24, 0
	strb	w1, [x0,684]
	b	.L663
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
	bls	.L676
	adrp	x0, .LANCHOR2+565
	ldrb	w0, [x0,#:lo12:.LANCHOR2+565]
	cbz	w0, .L677
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L677:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L676:
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
.L678:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0,#:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L703
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, w21
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L679
	bl	SandiskSetRRPara
	b	.L680
.L679:
	bl	ToshibaSetRRPara
.L680:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L681
	ldr	x0, [x29,112]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L681
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L681:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L682
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
	b	.L683
.L682:
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L683:
	cmn	w28, #1
	beq	.L684
	ldr	x0, [x29,120]
	cmn	w25, #1
	csel	w25, w25, w28, ne
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L686
	mov	x26, 0
	mov	x27, x26
.L684:
	add	w21, w21, 1
	b	.L678
.L703:
	mov	w28, w25
.L686:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, 0
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L688
	bl	SandiskSetRRPara
	b	.L689
.L688:
	bl	ToshibaSetRRPara
.L689:
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
	bcc	.L690
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L690:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L691
	mov	w0, 4
	bl	NandcSetDdrMode
.L691:
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
.L705:
	ldrb	w0, [x27]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L708
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L706
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,4044]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L711
	mov	x23, 0
	mov	x24, x23
.L706:
	add	w26, w26, 1
	b	.L705
.L711:
	mov	w19, w0
.L708:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,4044]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L710
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L710:
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
	stp	x29, x30, [sp, -144]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	add	x0, x4, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w24, w1
	mov	x25, x2
	ldrb	w20, [x0,4044]
	mov	x23, x3
	ldrb	w0, [x0,136]
	mov	x19, x4
	cbnz	w0, .L718
	add	w20, w20, w20, lsl 1
	ubfx	x20, x20, 2, 8
	b	.L719
.L718:
	mov	w0, 3
	sdiv	w20, w20, w0
.L719:
	mov	w0, w22
	adrp	x28, g_maxRetryCount
	mov	w27, 0
	add	x28, x28, :lo12:g_maxRetryCount
	bl	NandcWaitFlashReady
	add	x4, x19, :lo12:.LANCHOR0
	sbfiz	x0, x22, 4, 32
	add	x4, x4, 40
	mov	w19, -1
	add	x1, x4, x0
	mov	w7, 239
	mov	w8, 137
	ldr	x4, [x4,x0]
	ldrb	w21, [x1,8]
	lsl	x6, x21, 8
.L720:
	ldrb	w0, [x28]
	cmp	w27, w0
	bcs	.L723
	add	x26, x4, x6
	mov	w0, 200
	str	x6, [x29,104]
	str	x4, [x29,112]
	str	w7, [x26,2056]
	str	w8, [x26,2052]
	str	x7, [x29,120]
	str	x8, [x29,128]
	bl	NandcDelayns
	add	w5, w27, 1
	str	w5, [x26,2048]
	str	wzr, [x26,2048]
	mov	w0, w22
	str	wzr, [x26,2048]
	mov	w1, w24
	str	wzr, [x26,2048]
	mov	x2, x25
	mov	x3, x23
	str	x5, [x29,136]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x5, [x29,136]
	ldr	x8, [x29,128]
	ldr	x7, [x29,120]
	ldr	x4, [x29,112]
	ldr	x6, [x29,104]
	beq	.L721
	cmn	w19, #1
	csel	w19, w19, w0, ne
	cmp	w0, w20
	bcc	.L728
	mov	x23, 0
	mov	x25, x23
.L721:
	mov	w27, w5
	b	.L720
.L728:
	mov	w19, w0
.L723:
	add	x21, x4, x21, lsl 8
	mov	w0, 239
	str	w0, [x21,2056]
	mov	w0, 137
	str	w0, [x21,2052]
	mov	w0, 200
	bl	NandcDelayns
	str	wzr, [x21,2048]
	str	wzr, [x21,2048]
	cmp	w19, w20
	str	wzr, [x21,2048]
	str	wzr, [x21,2048]
	bcc	.L725
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L725:
	cmn	w19, #1
	beq	.L729
	cmp	w19, 256
	bne	.L726
.L729:
	adrp	x0, .LC11
	mov	w1, w27
	add	x0, x0, :lo12:.LC11
	mov	w2, w24
	mov	w3, w27
	mov	w4, w19
	bl	printk
.L726:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
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
	bne	.L739
	ldrb	w19, [x0,20]
.L739:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 4060
.L740:
	cmp	w28, w25
	bcs	.L744
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
	beq	.L742
	ldrb	w1, [x4,4044]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L749
	mov	x24, 0
	mov	x26, x24
.L742:
	add	w28, w28, 1
	b	.L740
.L749:
	mov	w20, w0
.L744:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	add	x0, x0, 4056
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	cmp	w1, 7
	bne	.L746
	strb	w19, [x0,20]
	b	.L747
.L746:
	strb	w19, [x0,12]
.L747:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,4044]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L748
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L748:
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
	cbnz	w19, .L756
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	ldr	w2, [x0,28]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L756
	ldrb	w0, [x0,24]
	cbnz	w0, .L757
	sub	w21, w21, #2
	b	.L756
.L757:
	mov	w21, 4
.L756:
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
	ldrb	w1, [x19,565]
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
.L761:
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
	beq	.L759
	ldr	x25, [x24,672]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L759
	mov	w1, 2036
	add	x0, x25, 12
	bl	JSHash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L759
	ldr	w0, [x20,28]
	cmp	w21, 1
	str	w26, [x20,8]
	mul	w19, w19, w0
	str	w19, [x24,680]
	beq	.L762
	mov	w21, 1
.L759:
	cmp	w26, 4
	mov	w19, w26
	bne	.L761
	b	.L760
.L762:
	mov	w21, 2
.L760:
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
	ldr	w1, [x0,576]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L768
	mov	w0, 0
	bl	flash_enter_slc_mode
.L768:
	adrp	x25, .LC12
	mov	x0, x27
	mov	w1, 0
	mov	w2, 2048
	mov	w24, -1
	mov	w20, 2
	add	x25, x25, :lo12:.LC12
	bl	ftl_memset
.L769:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,25]
	cmp	w20, w0
	bcs	.L773
	mov	x23, 0
.L771:
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
	bne	.L770
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L771
	b	.L772
.L770:
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L772
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
	bls	.L776
	mov	w24, 0
	str	w20, [x19,8]
	bl	FlashSavePhyInfo
.L772:
	add	w20, w20, 1
	b	.L769
.L776:
	mov	w24, 0
.L773:
	mov	w0, w28
	add	x22, x22, :lo12:.LANCHOR2
	bl	FlashBchSel
	ldr	w1, [x22,576]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L778
	mov	w0, 0
	bl	flash_exit_slc_mode
.L778:
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
	strb	wzr, [x21,565]
	str	wzr, [x21,656]
	add	x21, x23, :lo12:IDByte
	strb	wzr, [x24,-16]
	bl	NandcInit
	add	x0, x25, :lo12:.LC13
	mov	x26, x21
	str	x0, [x29,104]
.L785:
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
	bhi	.L781
	ldr	x0, [x29,104]
	add	w1, w27, 1
	ldrb	w3, [x21,1]
	ldrb	w4, [x21,2]
	ldrb	w5, [x21,3]
	ldrb	w6, [x21,4]
	ldrb	w7, [x21,5]
	bl	printk
.L781:
	cbnz	w27, .L782
	ldrb	w0, [x26]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L829
	ldrb	w0, [x26,1]
	cmp	w0, 255
	beq	.L829
	bl	FlashCs123Init
.L782:
	ldrb	w0, [x21]
	cmp	w0, 181
	bne	.L784
	strb	w28, [x21]
.L784:
	add	w27, w27, 1
	add	x24, x24, 16
	cmp	w27, 4
	add	x21, x21, 8
	bne	.L785
	ldrb	w0, [x23,#:lo12:IDByte]
	cmp	w0, 173
	beq	.L786
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,568]
	bl	NandcSetDdrMode
.L786:
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
	cbnz	w2, .L787
	and	w2, w0, -33
	cmp	w2, 209
	beq	.L787
	cmp	w0, 220
	bne	.L788
	ldrb	w2, [x4,3]
	cmp	w2, 149
	bne	.L788
.L787:
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
	strb	w5, [x2,3033]
	strb	w0, [x2,3034]
	cmp	w5, 152
	bne	.L789
	ldrsb	w2, [x6,4]
	tbnz	w2, #31, .L789
	mov	w2, 24
	strb	w2, [x4,685]
.L789:
	add	x2, x20, :lo12:.LANCHOR2
	mov	w4, 12336
	movk	w4, 0x5638, lsl 16
	ldr	w5, [x2,576]
	cmp	w5, w4
	bne	.L790
	mov	w4, 16
	strb	w4, [x2,685]
.L790:
	cbz	w3, .L791
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3046]
	mov	w2, -38
	b	.L860
.L791:
	cmp	w0, 220
	bne	.L792
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3046]
	mov	w2, -36
.L860:
	strb	w2, [x0,3034]
.L792:
	add	x21, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x21, 2688
	mov	w2, 32
	add	x0, x0, 104
	bl	ftl_memcpy
	add	x0, x21, 256
	add	x1, x21, 3032
	mov	w2, 32
	bl	ftl_memcpy
.L788:
	add	x21, x19, :lo12:.LANCHOR0
	ldrb	w0, [x21,24]
	cbnz	w0, .L793
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L795
	ldr	x0, [x21,1912]
	add	x21, x20, :lo12:.LANCHOR2
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x21,564]
	tbnz	x1, 0, .L795
	mov	w1, 1
	strb	w1, [x21,565]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21,564]
	bl	NandcSetMode
.L795:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	ldrb	w1, [x1,26]
	strb	w1, [x0,136]
	bl	FlashLoadPhyInfo
	cbz	w0, .L793
	add	x21, x20, :lo12:.LANCHOR2
	ldr	w0, [x21,568]
	cbz	w0, .L798
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L861
.L798:
	ldrb	w0, [x21,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x21,564]
.L861:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L793
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
	beq	.L783
	bl	FlashDieInfoInit
	ldr	x0, [x21,1912]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x21,1912]
	ldrh	w1, [x21,1920]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L800
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L801
.L800:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L801:
	add	x21, x20, :lo12:.LANCHOR2
	ldrb	w0, [x21,564]
	tst	w0, 6
	beq	.L802
	bl	FlashSavePhyInfo
	ldr	w1, [x21,680]
	mov	w0, 0
	bl	FlashDdrParaScan
.L802:
	bl	FlashSavePhyInfo
.L793:
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
	tbz	x1, 6, .L804
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
	bhi	.L805
	adrp	x1, HynixReadRetrial
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,664]
	sub	w1, w0, #5
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L806
	mov	w1, 1
	str	w1, [x22,640]
	b	.L807
.L806:
	cmp	w0, 7
	add	x21, x21, 4084
	beq	.L808
.L807:
	add	x21, x19, :lo12:.LANCHOR0
	add	x21, x21, 4076
.L808:
	mov	x1, 0
	mov	w2, w1
.L810:
	ldrsb	w3, [x21,x1]
	add	x1, x1, 1
	cmp	w3, wzr
	csinc	w2, w2, w2, ne
	cmp	x1, 32
	bne	.L810
	cmp	w2, 27
	bls	.L804
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L804
.L805:
	sub	w3, w0, #17
	uxtb	w3, w3
	cmp	w3, 2
	bhi	.L812
	adrp	x2, MicronReadRetrial
	cmp	w0, 19
	add	x2, x2, :lo12:MicronReadRetrial
	str	x2, [x22,664]
	beq	.L813
	mov	w0, 7
	b	.L863
.L813:
	mov	w0, 15
.L863:
	strb	w0, [x1,#:lo12:g_maxRetryCount]
	b	.L804
.L812:
	sub	w1, w0, #65
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L831
	cmp	w0, 33
	bne	.L814
.L831:
	add	x0, x20, :lo12:.LANCHOR2
	adrp	x1, ToshibaReadRetrial
	add	x1, x1, :lo12:ToshibaReadRetrial
	str	x1, [x0,664]
	mov	w0, 4
	strb	w0, [x5,#:lo12:g_maxRegNum]
	mov	w0, 7
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	b	.L804
.L814:
	sub	w1, w0, #67
	uxtb	w1, w1
	cmp	w1, 1
	sub	w1, w0, #34
	cset	w4, ls
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L832
	cbz	w4, .L816
.L832:
	add	x1, x20, :lo12:.LANCHOR2
	adrp	x3, ToshibaReadRetrial
	add	x3, x3, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x3, [x1,664]
	beq	.L818
	cmp	w0, 68
	beq	.L818
	mov	w0, 7
	b	.L862
.L818:
	mov	w0, 17
.L862:
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	mov	w0, 4
	cbnz	w4, .L864
	mov	w0, 5
.L864:
	strb	w0, [x5,#:lo12:g_maxRegNum]
	b	.L804
.L816:
	cmp	w0, 49
	bne	.L804
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,664]
.L804:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,576]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L821
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,136]
	cbz	w2, .L821
	ldr	x0, [x0,1912]
	strb	wzr, [x0,18]
.L821:
	ldrb	w0, [x23,#:lo12:IDByte]
	cmp	w0, 44
	bne	.L822
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,565]
	cbz	w0, .L822
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L823
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L822
.L823:
	add	x0, x20, :lo12:.LANCHOR2
	strb	wzr, [x0,565]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L822:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w0, [x1,564]
	tst	w0, 6
	beq	.L824
	ldrb	w1, [x1,565]
	cbnz	w1, .L825
	tbnz	x0, 0, .L824
.L825:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w0, 0
	ldr	w1, [x20,680]
	bl	FlashDdrParaScan
.L824:
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
	bne	.L827
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,4038]
	strb	w20, [x19,25]
	mov	w2, 8
	strh	w4, [x19,4030]
	strh	w3, [x19,4036]
	strh	w2, [x19,4042]
.L827:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L783
.L829:
	mov	w0, -2
.L783:
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
	cbz	w0, .L866
	adrp	x0, .LANCHOR2+576
	ldr	w2, [x0,#:lo12:.LANCHOR2+576]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w2, w0
	beq	.L865
.L866:
	sub	w0, w3, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L867
	cmp	w3, 68
	beq	.L867
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L865
.L867:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w21, w1
	add	x24, x20, 652
	mov	w25, 65535
	adrp	x26, .LANCHOR2
.L869:
	ldr	x0, [x20,1912]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L865
	ldrh	w0, [x24,w19,sxtw 1]
	cmp	w0, w25
	bne	.L865
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
	b	.L869
.L865:
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
.L875:
	cmp	w22, w25
	beq	.L909
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
	bcc	.L876
	mov	w0, -1
	str	w0, [x19]
	b	.L877
.L876:
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
	bls	.L880
	cmn	w3, #1
	mov	w0, 256
	csel	w3, w3, w0, eq
.L880:
	cmp	w3, 256
	beq	.L891
	cmn	w3, #1
	bne	.L881
.L891:
	str	w3, [x19]
	b	.L883
.L881:
	str	wzr, [x19]
.L883:
	ldr	x0, [x19,16]
	cbz	x0, .L884
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L884
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L884
	str	w1, [x19]
.L884:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L877
	add	x5, x5, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	mov	x0, x23
	ldrb	w2, [x5,4044]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L886
	adrp	x0, .LC16
	mov	w2, 4
	add	x0, x0, :lo12:.LC16
	mov	w3, 8
	bl	rknand_print_hex
.L886:
	ldr	x1, [x19,16]
	cbz	x1, .L877
	mov	w2, 4
	mov	x0, x24
	mov	w3, w2
	bl	rknand_print_hex
.L877:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L875
.L909:
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
	cbz	w23, .L950
	bl	FlashReadSlc2KPages
	b	.L999
.L950:
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
.L911:
	ldr	w0, [x29,144]
	cmp	w25, w0
	bcs	.L1001
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
	bcc	.L913
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L914
.L913:
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
	bhi	.L916
	add	x1, x1, 4056
	sxtw	x0, w19
	add	x1, x1, x0
	cmp	w2, 7
	ldrb	w3, [x1,12]
	bne	.L917
	ldrb	w3, [x1,20]
.L917:
	add	x1, x24, :lo12:.LANCHOR2
	add	x0, x1, x0
	ldrb	w0, [x0,560]
	cmp	w0, w3
	beq	.L916
	add	x2, x22, :lo12:.LANCHOR0
	mov	w0, w19
	add	x2, x2, 4060
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L916:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,140]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,152]
	cbz	w0, .L918
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L918
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L919
.L918:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L919:
	cmp	w19, 255
	ldr	w1, [x29,172]
	bne	.L957
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L951
.L957:
	cbz	w20, .L923
	add	x0, x22, :lo12:.LANCHOR0
	ldr	w2, [x0,28]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L924
.L923:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L924
.L951:
	mov	w20, w0
.L921:
	ldrb	w2, [x29,148]
	mov	w0, w19
	ldr	x3, [x21,8]
	mov	w1, 0
	ldr	x4, [x21,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L925
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w1, [x0,32]
	cbz	w1, .L925
	strb	wzr, [x0,32]
	mov	w20, 0
	b	.L919
.L925:
	cbz	w20, .L926
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
.L926:
	mov	w0, w19
	bl	NandcFlashDeCs
	add	x6, x22, :lo12:.LANCHOR0
	ldrb	w0, [x29,136]
	cmn	w28, #1
	strb	w0, [x6,32]
	bne	.L933
	add	x0, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,565]
	cbnz	w0, .L928
.L932:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x20, [x0,664]
	cbnz	x20, .L929
	b	.L1002
.L928:
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
	beq	.L931
	ldr	x6, [x29,104]
	ldrb	w0, [x6,4044]
	cmp	w28, w0, lsr 1
	bls	.L953
.L931:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L932
	b	.L953
.L929:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L934
	add	x2, x22, :lo12:.LANCHOR0
	ldr	x0, [x2,1912]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 6
	bhi	.L935
	ldrb	w1, [x2,4057]
	mov	w0, w19
	add	x2, x2, 4060
	mov	w3, w20
	bl	HynixSetRRPara
.L935:
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
	bne	.L934
	ldrb	w0, [x4,136]
	cbz	w0, .L934
	ldr	w0, [x29,152]
	cbz	w0, .L936
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L937
.L936:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L937:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w1, [x29,172]
	ldr	x2, [x21,8]
	mov	w20, 0
	ldr	x3, [x21,16]
	ldr	x4, [x0,664]
	mov	w0, w19
	blr	x4
	mov	w28, w0
	b	.L934
.L1002:
	ldr	w1, [x29,172]
	mov	w0, w19
	ldr	x2, [x21,8]
	ldr	x3, [x21,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L934
.L953:
	mov	w20, 0
.L933:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L934
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,664]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L934:
	cmp	w28, 256
	beq	.L958
	cmn	w28, #1
	bne	.L938
.L958:
	str	w28, [x26,x27]
	b	.L940
.L938:
	str	wzr, [x26,x27]
.L940:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L942
	add	x2, x22, :lo12:.LANCHOR0
	ldr	w1, [x21,4]
	ldr	x0, [x29,128]
	ldrb	w2, [x2,4044]
	bl	printk
	ldr	x1, [x21,16]
	cbz	x1, .L942
	mov	w2, 4
	ldr	x0, [x29,120]
	mov	w3, w2
	bl	rknand_print_hex
.L942:
	cbz	w20, .L944
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L945
	add	x24, x24, :lo12:.LANCHOR2
	ldr	x0, [x24,664]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L945:
	add	w0, w25, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L959
	cmn	w23, #1
	bne	.L946
.L959:
	str	w23, [x26,x0]
	b	.L944
.L946:
	str	wzr, [x26,x0]
.L944:
	ldr	w0, [x29,152]
	add	w25, w25, w20
	cbz	w0, .L914
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L914
	mov	w0, w19
	bl	flash_exit_slc_mode
.L914:
	add	w25, w25, 1
	b	.L911
.L1001:
	mov	w0, 0
	b	.L999
.L924:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L921
	ldr	w1, [x29,172]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L921
.L999:
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
.L1004:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1676]
	cmp	w1, w19
	bls	.L1014
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1005:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1007
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
	beq	.L1006
	add	x24, x23, :lo12:.LANCHOR2
	ldr	x0, [x24,696]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1006
	add	x24, x24, 728
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1007
.L1006:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1005
.L1007:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1676]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1004
.L1014:
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
.L1016:
	cmp	w20, w19
	ble	.L1024
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1017
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,696]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1017
	mov	w0, w22
	cbz	x22, .L1018
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.6
	b	.L1018
.L1017:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1016
.L1024:
	mov	w0, -1
.L1018:
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
	bne	.L1026
.L1030:
	mov	w1, 0
	b	.L1027
.L1026:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L1028:
	cmp	w2, w19
	bge	.L1027
	add	x3, x20, :lo12:.LANCHOR2
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,688]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L1029
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L1029:
	cmp	w1, w0
	bcs	.L1030
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1028
.L1027:
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
.L1036:
	cmp	w25, w23
	beq	.L1065
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1676]
	cmp	w0, w1
	bcc	.L1037
	str	w28, [x21]
	b	.L1038
.L1037:
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
	tbz	x0, 0, .L1041
	str	w28, [x21]
.L1041:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1038:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1036
.L1065:
	mov	w21, 0
	cbz	w27, .L1063
	adrp	x25, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	add	x25, x25, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1064:
	cmp	w21, w23
	beq	.L1063
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1044
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1045
.L1044:
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
	bne	.L1046
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1046:
	ldr	x0, [x19,16]
	cbz	x0, .L1047
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1047
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1047:
	ldr	x0, [x19,8]
	cbz	x0, .L1045
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,704]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1045
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1045:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1064
.L1063:
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
	cbz	w4, .L1067
	bl	FlashProgSlc2KPages
	b	.L1068
.L1067:
	sub	w0, w5, #1
	adrp	x28, .LANCHOR2
	str	w0, [x29,120]
	add	x0, x28, :lo12:.LANCHOR2
	str	x0, [x29,112]
.L1119:
	cmp	w22, w25
	bcs	.L1120
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
	bcc	.L1069
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1070
.L1069:
	add	x3, x28, :lo12:.LANCHOR2
	uxtw	x0, w0
	ldrb	w3, [x3,648]
	cmp	w3, wzr
	mov	x3, 24
	csel	w24, w24, wzr, ne
	madd	x0, x0, x3, x1
	ldr	x0, [x0,1728]
	cbz	x0, .L1072
	cmp	w2, 1
	bne	.L1073
	ldr	x0, [x1,3976]
	bl	NandcIqrWaitFlashReady
.L1073:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1072:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	mov	x1, 24
	add	x0, x0, 1720
	madd	x0, x2, x1, x0
	ldr	w1, [x29,128]
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1074
	add	w1, w22, 1
	mov	w3, 56
	umull	x1, w1, w3
	add	x1, x19, x1
	str	x1, [x0,16]
.L1074:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, x2
	ldrb	w20, [x0,1680]
	mov	x0, 24
	madd	x2, x2, x0, x1
	ldrb	w0, [x1,1676]
	strb	w20, [x2,1720]
	cmp	w0, 1
	bne	.L1075
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1076
.L1075:
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
.L1076:
	ldr	w0, [x29,120]
	cmp	w0, 6
	bhi	.L1077
	ldr	x0, [x29,112]
	add	x0, x0, x20, sxtw
	ldrb	w0, [x0,560]
	cbz	w0, .L1077
	add	x2, x21, :lo12:.LANCHOR0
	mov	w0, w20
	add	x2, x2, 4060
	mov	w3, 0
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L1077:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1078
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L1078
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1079
.L1078:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1079:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x3, [x26,8]
	mov	w0, w20
	ldr	x4, [x26,16]
	mov	w1, 1
	mov	w2, w27
	bl	NandcXferData
	cbz	w24, .L1080
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
.L1080:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1070:
	add	w22, w22, 1
	b	.L1119
.L1120:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	mov	x22, 24
	ldr	x0, [x21,3976]
	bl	NandcIqrWaitFlashReady
.L1082:
	ldrb	w0, [x21,1676]
	cmp	w0, w20
	bls	.L1121
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1083
	ldrb	w0, [x21,136]
	cbz	w0, .L1083
	madd	x0, x20, x22, x21
	ldrb	w0, [x0,1720]
	bl	flash_exit_slc_mode
.L1083:
	add	x20, x20, 1
	b	.L1082
.L1121:
	ldr	w0, [x29,124]
	cbnz	w0, .L1085
.L1093:
	mov	w0, 0
	b	.L1068
.L1085:
	adrp	x24, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	mov	w21, 0
	add	x24, x24, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1086:
	cmp	w21, w25
	beq	.L1093
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1087
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1088
.L1087:
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
	bne	.L1089
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1089:
	ldr	x0, [x19,16]
	cbz	x0, .L1090
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1090
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1090:
	ldr	x0, [x19,8]
	cbz	x0, .L1088
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,704]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1088
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1088:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1086
.L1068:
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
	bcc	.L1124
	mov	w0, w1
	bl	FlashTestBlk.part.7
.L1124:
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
.L1128:
	add	x23, x19, :lo12:.LANCHOR0
	ldrb	w0, [x23,1676]
	cmp	w0, w20
	bls	.L1179
	ldr	x0, [x29,136]
	sxtw	x24, w20
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 728
	ldrh	w21, [x0,w20,sxtw 1]
	cbnz	w21, .L1158
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
.L1130:
	uxth	w28, w26
	cmp	w28, w22
	bcs	.L1140
	mov	w0, -1
	strb	w0, [x29,166]
	strb	w0, [x29,167]
	ldr	w0, [x29,124]
	cbz	w0, .L1132
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
	bne	.L1132
	ldr	w1, [x4,28]
	mov	w0, w27
	add	x2, x29, 167
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,166]
	ldrb	w1, [x29,167]
	and	w0, w1, w0
	strb	w0, [x29,166]
.L1132:
	ldr	w0, [x29,120]
	cbz	w0, .L1134
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
.L1134:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x29,166]
	ldr	x0, [x0,1912]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1159
	cmp	w0, 1
	bne	.L1135
.L1159:
	mov	w0, 1
	cbz	w1, .L1137
	ldrb	w0, [x29,167]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1137
.L1135:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1137
	ldrb	w0, [x29,167]
	cmp	w0, 255
	cset	w0, ne
.L1137:
	ldr	w1, [x29,128]
	cbz	w1, .L1138
	add	x0, x19, :lo12:.LANCHOR0
	add	x0, x0, 1688
	ldr	w1, [x0,x24,lsl 2]
	mov	w0, w27
	add	w1, w21, w1
	bl	SandiskProgTestBadBlock
.L1138:
	cbz	w0, .L1139
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
	bgt	.L1140
.L1139:
	add	w26, w26, 1
	add	w21, w21, w25
	b	.L1130
.L1140:
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
	blt	.L1142
	ldrh	w2, [x1,4036]
	mov	w1, 0
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1142:
	cbnz	w20, .L1144
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x26, .LC27
	mov	w21, w20
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
	ldrh	w27, [x0,8]
.L1145:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,25]
	cmp	w0, w27
	bls	.L1180
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1146
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
.L1146:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1145
.L1180:
	adrp	x26, .LC27
	ldr	w28, [x29,132]
	sub	w27, w22, #50
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
.L1148:
	cmp	w28, w27
	ble	.L1181
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1149
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
.L1149:
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1148
.L1181:
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	ldr	w2, [x0,8]
	sub	w1, w1, w2
	cmp	w21, w1
	bcc	.L1144
	ldrh	w2, [x0,4036]
	mov	w1, 0
	ldr	x0, [x19,#:lo12:.LANCHOR0]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1144:
	ldr	x0, [x29,136]
	adrp	x23, .LC28
	ldr	w27, [x29,132]
	add	x23, x23, :lo12:.LC28
	mul	w26, w20, w22
	add	x28, x0, :lo12:.LANCHOR2
	mov	w21, -3872
.L1152:
	mov	w2, w27
	mov	x0, x23
	mov	w1, w20
	bl	printk
	ldr	x2, [x19,#:lo12:.LANCHOR0]
.L1153:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1182
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1153
.L1182:
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
	cbz	w0, .L1158
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1152
.L1158:
	add	w20, w20, 1
	uxtb	w20, w20
	b	.L1128
.L1179:
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
.L1184:
	cmp	w2, w0
	bhi	.L1186
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L1184
.L1186:
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
.L1191:
	add	x5, x0, 776
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1191
	ldrh	w3, [x24,20]
	ldrb	w0, [x24,15]
	cmp	w3, w0
	bcs	.L1192
	uxtb	w8, w1
	mov	w3, 0
	mul	w11, w1, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR2
.L1193:
	cmp	w3, w1
	bcs	.L1195
	uxtb	w0, w3
	mov	w5, w3
	mov	w7, 0
.L1196:
	cmp	w7, w2
	bcs	.L1218
	add	x9, x12, 776
	add	w13, w5, w11
	add	w14, w0, w8
	add	w7, w7, 1
	strb	w0, [x9,w5,uxtw]
	add	w0, w0, w10
	strb	w14, [x9,x13]
	add	w5, w5, w1
	uxtb	w0, w0
	b	.L1196
.L1218:
	add	w3, w3, 1
	b	.L1193
.L1195:
	add	x0, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x0,770]
	strh	w4, [x0,758]
.L1192:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x0,808]
	strh	wzr, [x0,810]
	bne	.L1197
	strh	w6, [x0,808]
.L1197:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 4352
	adrp	x22, .LANCHOR0
	strh	w2, [x0,812]
	add	x2, x22, :lo12:.LANCHOR0
	ldrb	w27, [x2,24]
	cbz	w27, .L1198
	mov	w2, 384
	strh	w2, [x0,812]
.L1198:
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
	bls	.L1199
	and	w0, w20, 255
	strh	w0, [x21,810]
.L1199:
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
	bhi	.L1215
	mov	w0, 4
.L1215:
	strh	w0, [x2,840]
	cbz	w27, .L1202
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0,812]
.L1202:
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
	bne	.L1216
	add	w20, w20, 4
.L1216:
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
	cbz	w5, .L1205
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x20,840]
	b	.L1217
.L1205:
	cmp	w4, 7
	bhi	.L1206
	mov	w1, 8
.L1217:
	str	w1, [x20,848]
.L1206:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,756]
	strh	wzr, [x19,860]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L1207
	mov	w0, 1
	strh	w0, [x19,860]
.L1207:
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
	mov	w1, 32
	str	wzr, [x0,864]
	strh	w1, [x0,952]
	mov	w1, 128
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
	mov	w1, 1024
	ldrh	w2, [x0,824]
	strh	wzr, [x0,956]
	strh	wzr, [x0,958]
	sdiv	w2, w1, w2
	ldrh	w1, [x0,748]
	strh	wzr, [x0,960]
	lsl	w1, w1, 2
	strh	wzr, [x0,962]
	str	w2, [x0,964]
	cmp	w2, w1
	bls	.L1220
	str	w1, [x0,964]
.L1220:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19,822]
	str	wzr, [x19,968]
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
	ldr	w0, [x19,964]
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
	ldr	w0, [x19,964]
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
	ldr	w0, [x19,964]
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
.L1221:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L1321
	ldr	x3, [x19,1328]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L1221
.L1321:
	add	x1, x20, :lo12:.LANCHOR2
.L1223:
	cmp	w0, 8
	beq	.L1322
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,1328]
	b	.L1223
.L1322:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1192]
	cbnz	x1, .L1225
.L1227:
	adrp	x0, .LC29
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC29
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L1226
.L1225:
	ldr	x1, [x0,1200]
	cbz	x1, .L1227
	ldr	x1, [x0,1240]
	cbz	x1, .L1227
	ldr	x1, [x0,1256]
	cbz	x1, .L1227
	ldr	x1, [x0,1264]
	cbz	x1, .L1227
	ldr	x1, [x0,1272]
	cbz	x1, .L1227
	ldr	x1, [x0,1280]
	cbz	x1, .L1227
	ldr	x1, [x0,1328]
	cbz	x1, .L1227
	ldr	x0, [x0,1184]
	cbz	x0, .L1227
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,976]
	cbz	x1, .L1227
	ldr	x1, [x0,984]
	cbz	x1, .L1227
	ldr	x1, [x0,992]
	cbz	x1, .L1227
	ldr	x1, [x0,1008]
	cbz	x1, .L1227
	ldr	x1, [x0,1016]
	cbz	x1, .L1227
	ldr	x1, [x0,1024]
	cbz	x1, .L1227
	ldr	x1, [x0,1000]
	cbz	x1, .L1227
	ldr	x1, [x0,1048]
	cbz	x1, .L1227
	ldr	x1, [x0,1056]
	cbz	x1, .L1227
	ldr	x0, [x0,1064]
	cbz	x0, .L1227
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1072]
	cbz	x1, .L1227
	ldr	x1, [x0,1088]
	cbz	x1, .L1227
	ldr	x1, [x0,1096]
	cbz	x1, .L1227
	ldr	x1, [x0,1104]
	cbz	x1, .L1227
	ldr	x1, [x0,1112]
	cbz	x1, .L1227
	ldr	x1, [x0,1120]
	cbz	x1, .L1227
	ldr	x1, [x0,1128]
	cbz	x1, .L1227
	ldr	x1, [x0,1168]
	cbz	x1, .L1227
	ldr	x1, [x0,1152]
	cbz	x1, .L1227
	ldr	x0, [x0,1208]
	cbz	x0, .L1227
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,1216]
	cbz	x1, .L1227
	ldr	x1, [x0,1224]
	cbz	x1, .L1227
	ldr	x0, [x0,1232]
	cbz	x0, .L1227
	mov	w0, 0
.L1226:
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
	cbz	w2, .L1324
	ldrh	w2, [x1,840]
	ldr	x3, [x1,1208]
	mov	x1, 0
.L1325:
	cmp	w2, w1, uxth
	bls	.L1330
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L1325
	mov	w0, 1
	b	.L1324
.L1330:
	mov	w0, 0
.L1324:
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
	ldr	w3, [x2,968]
	cbz	w3, .L1332
	ldr	x5, [x2,1032]
	mov	x0, 56
	mov	x2, 0
.L1333:
	cmp	w3, w2
	bls	.L1342
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L1333
	cmp	w4, w1
	bhi	.L1333
	mov	w0, 1
	b	.L1332
.L1342:
	mov	w0, 0
.L1332:
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
.L1351:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L1349
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L1351
.L1349:
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
	cbnz	w1, .L1356
	ldr	x0, [x19,1048]
	mov	x22, 0
	ldr	x3, [x19,1112]
	add	x21, x19, 1296
	ldrh	w2, [x19,828]
	str	x0, [x19,1416]
	str	x3, [x19,1424]
	bl	ftl_memset
.L1357:
	ldrh	w1, [x19,770]
	mov	w0, w22
	add	x20, x19, 1408
	add	x22, x22, 1
	cmp	w0, w1
	bge	.L1365
	add	x1, x21, x22, lsl 3
	ldrh	w2, [x19,1288]
	ldr	x3, [x20,8]
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1357
.L1365:
	ldr	x25, [x20,16]
	mov	w1, 255
	mov	w2, 16
	mov	w26, 0
	mov	x0, x25
	adrp	x22, .LC31
	adrp	x24, .LC32
	mov	w23, w26
	bl	ftl_memset
	add	x22, x22, :lo12:.LC31
	mov	w0, -3887
	strh	w0, [x25]
	add	x24, x24, :lo12:.LC32
	ldr	w0, [x21,8]
	str	w0, [x25,4]
	ldrh	w0, [x19,1296]
	strh	w0, [x25,2]
	ldrh	w0, [x21,4]
	strh	w0, [x25,8]
	ldrh	w0, [x21,6]
	strh	w0, [x25,10]
	ldr	w0, [x19,744]
	strh	w0, [x25,12]
.L1359:
	ldr	x0, [x19,1048]
	str	x0, [x20,8]
	ldrh	w1, [x19,1296]
	ldr	x0, [x19,1112]
	str	x0, [x20,16]
	ldrh	w2, [x19,1298]
	ldrh	w3, [x19,1300]
	orr	w0, w2, w1, lsl 10
	ldrh	w4, [x25,10]
	str	w0, [x20,4]
	mov	x0, x22
	str	wzr, [x20]
	bl	printk
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,820]
	ldrh	w0, [x19,1298]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1360
	ldr	w1, [x19,1304]
	ldrh	w0, [x19,1296]
	add	w1, w1, 1
	str	w1, [x19,1304]
	str	w1, [x25,4]
	strh	w0, [x25,8]
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
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
.L1360:
	ldrh	w1, [x19,1298]
	ldr	w0, [x20]
	add	w1, w1, 1
	strh	w1, [x19,1298]
	cmn	w0, #1
	bne	.L1361
	ldr	w1, [x20,4]
	mov	x0, x24
	add	w26, w26, 1
	bl	printk
	uxth	w26, w26
	cmp	w26, 3
	bls	.L1359
	adrp	x0, .LC33
	ldr	w1, [x20,4]
	add	x0, x0, :lo12:.LC33
	mov	w2, w26
	bl	printk
	mov	w0, 1
	str	w0, [x19,1400]
	b	.L1356
.L1361:
	cbnz	w23, .L1356
	mov	w23, 1
	b	.L1359
.L1356:
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
.L1367:
	ldrh	w0, [x19,770]
	cmp	w22, w0
	bcs	.L1376
	ldrh	w20, [x19,814]
	strh	w25, [x23,12]
.L1369:
	sub	w20, w20, #1
	ldrh	w0, [x19,814]
	uxth	w20, w20
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1370
	madd	w0, w22, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1369
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1369
	strh	w20, [x23,12]
.L1370:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L1367
.L1376:
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
.L1379:
	cmp	w20, w21
	bge	.L1385
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1380
	add	w19, w19, 1
	uxth	w19, w19
.L1380:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1379
.L1385:
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
	cbnz	w22, .L1387
	add	x24, x20, 1296
	mov	x19, x20
	mov	x25, x24
	add	x20, x20, 1408
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1388:
	ldrh	w0, [x19,770]
	cmp	w22, w0
	bcs	.L1406
	ldrh	w4, [x25,12]
	mov	w1, 65535
	ldr	x0, [x19,1048]
	ldr	x26, [x19,1112]
	cmp	w4, w1
	str	x0, [x20,8]
	str	x26, [x20,16]
	beq	.L1389
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
	b	.L1390
.L1389:
	mov	w1, w22
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w23, [x19,814]
.L1392:
	sub	w23, w23, #1
	uxth	w23, w23
.L1391:
	ldrh	w0, [x19,814]
	madd	w0, w22, w0, w23
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1392
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
	bne	.L1390
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L1391
.L1390:
	mov	w0, w27
	add	w22, w22, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L1388
.L1406:
	mov	w19, 0
.L1395:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,832]
	cmp	w0, w19
	bls	.L1407
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	uxth	w19, w19
	b	.L1395
.L1407:
	ldrh	w19, [x20,1308]
	mov	w23, 65535
	sub	w19, w19, #1
	uxth	w19, w19
.L1397:
	add	x22, x20, 1296
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1401
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1398
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1399
	mov	w0, w19
	bl	FtlBbmMapBadBlock
	b	.L1398
.L1399:
	ldrh	w0, [x20,1296]
	cmp	w0, w23
	bne	.L1400
	strh	w19, [x20,1296]
	b	.L1398
.L1400:
	strh	w19, [x22,4]
	b	.L1401
.L1398:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1397
.L1401:
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
.L1387:
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
	bls	.L1412
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L1413
.L1412:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L1413:
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
	bne	.L1415
	ldrh	w19, [x3,820]
	b	.L1416
.L1415:
	ldrh	w19, [x3,818]
.L1416:
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
	bne	.L1417
	mov	w22, 0
	mov	w25, 2
.L1418:
	cmp	w22, w19
	bgt	.L1417
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
	bne	.L1419
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1419
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1419
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1418
.L1419:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1418
.L1417:
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
.L1426:
	ldrh	w0, [x23,814]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1429
	lsl	w0, w20, 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1427
	ldr	w0, [x21,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
.L1427:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1428
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1428
	ldr	w1, [x22,4]
	str	w1, [x23,1304]
	strh	w20, [x23,1296]
	ldrh	w1, [x22,8]
	strh	w1, [x23,1300]
	b	.L1429
.L1428:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1426
.L1429:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,1296]
	cmp	w1, w2
	beq	.L1431
	ldrh	w1, [x21,1300]
	cmp	w1, w2
	beq	.L1433
	add	x0, x21, 1408
	lsl	w1, w1, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x21,1408]
	cmn	w0, #1
	beq	.L1433
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1433
	ldr	w0, [x22,4]
	ldr	w1, [x21,1304]
	cmp	w0, w1
	bls	.L1433
	str	w0, [x21,1304]
	ldrh	w1, [x21,1300]
	ldrh	w0, [x22,8]
	strh	w1, [x21,1296]
	strh	w0, [x21,1300]
.L1433:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 1408
	mov	w24, 61649
	ldrh	w0, [x20,1296]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20,1298]
.L1435:
	tbnz	w21, #31, .L1440
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
	beq	.L1436
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1436
.L1440:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,1302]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1437
	b	.L1438
.L1436:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1435
.L1437:
	ldr	w2, [x1,744]
	cmp	w0, w2
	beq	.L1438
	ldrh	w1, [x1,758]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1438
	cmp	w2, w1
	bcs	.L1438
	bl	FtlSysBlkNumInit
.L1438:
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, 0
	add	x21, x19, 1296
.L1441:
	ldrh	w0, [x19,770]
	mov	w1, w20
	add	x20, x20, 1
	cmp	w1, w0
	bcs	.L1459
	ldrh	w2, [x19,1288]
	add	x0, x21, x20, lsl 3
	ldr	x3, [x19,1416]
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1441
.L1459:
	mov	w0, 0
.L1431:
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
	adrp	x2, .LANCHOR2
	uxth	w1, w1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	add	x20, x2, :lo12:.LANCHOR2
	mov	x19, x2
	stp	x21, x22, [sp,32]
	uxth	w22, w0
	ldrh	w0, [x20,1470]
	cmp	w0, 1024
	beq	.L1463
	cbz	w1, .L1465
	ldr	w0, [x20,1400]
	cbnz	w0, .L1465
	mov	w0, w22
	bl	P2V_block_in_plane
	uxth	w21, w0
	ldr	x0, [x20,1016]
	lsl	w1, w22, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,1016]
	bl	FlashEraseBlocks
	ubfiz	x1, x21, 1, 16
	ldr	x2, [x20,1168]
	ldrh	w0, [x2,x1]
	add	w0, w0, 1
	strh	w0, [x2,x1]
	ldr	w0, [x20,912]
	add	w0, w0, 1
	str	w0, [x20,912]
.L1465:
	add	x1, x19, :lo12:.LANCHOR2
	add	x1, x1, 1464
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w22, [x0,8]
	strh	w2, [x1,4]
.L1463:
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
	cbnz	w2, .L1473
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
.L1474:
	ldrh	w0, [x26,748]
	cmp	w0, w27
	bls	.L1518
	umull	x0, w27, w23
	ldr	x1, [x26,1016]
	str	wzr, [x1,x0]
	add	x0, x26, 776
	mov	w1, w25
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbz	w24, .L1475
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1476
.L1475:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1477
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
	b	.L1476
.L1477:
	add	w19, w19, 1
	uxth	w19, w19
.L1476:
	add	w2, w27, 1
	uxth	w27, w2
	b	.L1474
.L1518:
	cbz	w20, .L1497
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
.L1480:
	cmp	w20, w23, uxth
	bls	.L1519
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1016]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1481
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1481:
	add	x23, x23, 1
	b	.L1480
.L1519:
	cbnz	w24, .L1483
	mov	w0, 1
	uxth	w22, w22
	mov	w27, 6
	str	w0, [x29,124]
	b	.L1484
.L1483:
	add	x0, x21, :lo12:.LANCHOR2
	add	x28, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,820]
	str	w0, [x29,124]
	ldrb	w0, [x28,136]
	cbnz	w0, .L1500
	ldr	w0, [x29,124]
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1484
.L1500:
	mov	w22, 1
	mov	w27, w22
.L1484:
	mov	w23, 0
	add	x28, x21, :lo12:.LANCHOR2
.L1493:
	mov	w26, 0
	mov	w20, w26
.L1485:
	ldrh	w0, [x28,748]
	cmp	w0, w26
	bls	.L1520
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
	cbz	w24, .L1486
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1487
.L1486:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1487
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
.L1487:
	add	w3, w26, 1
	uxth	w26, w3
	b	.L1485
.L1520:
	cbz	w20, .L1497
	ldr	x0, [x28,1016]
	mov	w1, w20
	mov	w2, w22
	mov	w3, 1
	strb	wzr, [x28,648]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x28,648]
.L1490:
	cmp	w20, w1, uxth
	bls	.L1521
	mov	x0, 56
	ldr	x2, [x28,1016]
	mul	x0, x1, x0
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1491
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1491:
	add	x1, x1, 1
	b	.L1490
.L1521:
	add	w23, w23, w27
	ldr	w0, [x29,124]
	uxth	w23, w23
	cmp	w23, w0
	bcc	.L1493
	mov	x23, 0
	mov	x26, 56
.L1494:
	cmp	w20, w23, uxth
	bls	.L1522
	cbz	w24, .L1495
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1016]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1495
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1495:
	add	x23, x23, 1
	b	.L1494
.L1522:
	cmp	w25, 63
	bls	.L1501
	cbz	w24, .L1497
.L1501:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, w20
	ldr	x0, [x21,1016]
	bl	FlashEraseBlocks
.L1497:
	mov	w0, w19
.L1473:
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
	adrp	x0, .LANCHOR2
	mov	w5, 0
	add	x1, x0, :lo12:.LANCHOR2
	mov	w4, w5
	add	x1, x1, 1464
	ldrh	w6, [x1,2084]
	and	w6, w6, 31
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
.L1524:
	cmp	w4, w6
	bge	.L1531
	add	x5, x1, x3, sxtw 1
	ldrh	w7, [x5,8]
	add	x5, x1, x2, sxtw 1
	add	w4, w4, 1
	strh	w7, [x5,8]
	mov	w5, 1
	add	w3, w3, 1
	add	w2, w2, w5
	uxth	w4, w4
	and	w3, w3, 1023
	and	w2, w2, 1023
	b	.L1524
.L1531:
	cbz	w5, .L1523
	add	x0, x0, :lo12:.LANCHOR2
	strh	w3, [x0,1466]
	strh	w2, [x0,1468]
.L1523:
	ret
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	remove_from_free_sys_Queue
	.type	remove_from_free_sys_Queue, %function
remove_from_free_sys_Queue:
	stp	x29, x30, [sp, -32]!
	uxth	w1, w0
	mov	w0, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	add	x20, x20, 1464
	ldrh	w3, [x20,6]
	cbz	w3, .L1533
	ldrh	w4, [x20,2]
	mov	w19, w0
.L1534:
	cmp	w19, w3
	bcs	.L1539
	add	w2, w19, w4
	ubfiz	x2, x2, 1, 10
	add	x2, x20, x2
	ldrh	w0, [x2,8]
	cmp	w0, w1
	bne	.L1535
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	bl	printk
	ldrh	w0, [x20,2]
	add	x1, x20, x0, sxtw 1
	ldrh	w1, [x1,8]
	add	w19, w19, w0
	add	w0, w0, 1
	and	w0, w0, 1023
	ubfiz	x19, x19, 1, 10
	add	x19, x20, x19
	strh	w0, [x20,2]
	ldrh	w0, [x20,6]
	sub	w0, w0, #1
	strh	w1, [x19,8]
	strh	w0, [x20,6]
	mov	w0, 1
	b	.L1533
.L1535:
	add	w19, w19, 1
	b	.L1534
.L1539:
	mov	w0, 0
.L1533:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	remove_from_free_sys_Queue, .-remove_from_free_sys_Queue
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
	adrp	x23, .LC36
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x21, x19, 1464
	adrp	x24, .LANCHOR0
	add	x23, x23, :lo12:.LC36
.L1541:
	ldrh	w1, [x21,6]
	adrp	x22, .LANCHOR2
	cbz	w1, .L1542
	ldrh	w2, [x21,2]
	sub	w1, w1, #1
	add	x0, x21, x2, sxtw 1
	strh	w1, [x21,6]
	add	w2, w2, 1
	ldr	w1, [x19,1400]
	and	w2, w2, 1023
	strh	w2, [x21,2]
	ldrh	w20, [x0,8]
	cbnz	w1, .L1543
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
	cbz	w0, .L1544
	ldr	x0, [x19,1016]
	mov	w2, 1
	bl	FlashEraseBlocks
.L1544:
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
	b	.L1543
.L1542:
	adrp	x0, .LC35
	add	x0, x0, :lo12:.LC35
	bl	printk
.L1545:
	b	.L1545
.L1543:
	cbnz	w20, .L1546
	add	x22, x22, :lo12:.LANCHOR2
	mov	x0, x23
	mov	w1, w20
	ldrh	w2, [x22,1470]
	bl	printk
	b	.L1541
.L1546:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
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
	bls	.L1552
	mov	w7, 6
	ldr	x11, [x3,1280]
	mov	w1, -1
	umull	x10, w0, w7
	add	x5, x11, x10
	strh	w1, [x5,2]
	strh	w1, [x11,x10]
	ldr	x1, [x3,3568]
	cbnz	x1, .L1553
	str	x5, [x3,3568]
	b	.L1552
.L1553:
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
.L1561:
	add	w3, w3, 1
	cmp	w0, w2
	uxth	w3, w3
	beq	.L1552
	cmp	w3, w17
	bhi	.L1552
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L1557
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L1559
	b	.L1558
.L1557:
	bhi	.L1558
.L1559:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L1560
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x5, [x2,3576]
	b	.L1552
.L1560:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L1561
.L1558:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	ldr	x3, [x2,3568]
	cmp	x1, x3
	bne	.L1562
	strh	w0, [x1,2]
	str	x5, [x2,3568]
	b	.L1552
.L1562:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1280]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1552:
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
	beq	.L1567
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
	cbnz	x1, .L1568
	str	x4, [x5,3592]
	b	.L1567
.L1568:
	ldr	x11, [x5,1168]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,1280]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L1571:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L1569
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L1570
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L1567
.L1570:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L1571
.L1569:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,3592]
	cmp	x1, x3
	bne	.L1572
	strh	w0, [x1,2]
	str	x4, [x2,3592]
	b	.L1567
.L1572:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1280]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1567:
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
	bne	.L1575
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L1576
	str	xzr, [x0]
	b	.L1577
.L1576:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L1577
.L1575:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L1578
	cmp	w3, w0
	beq	.L1577
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L1577
.L1578:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,1280]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L1577:
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
	cbz	x2, .L1580
	adrp	x3, .LANCHOR2+1280
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+1280]
.L1581:
	cbnz	w1, .L1582
.L1584:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L1580
.L1582:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L1584
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L1581
.L1580:
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
	cbz	x1, .L1592
	ldr	x4, [x2,1280]
	mov	w5, 6
.L1588:
	cbz	w3, .L1589
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L1592
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L1588
.L1589:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L1592:
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
	beq	.L1594
	ldrh	w1, [x0,3656]
	cmp	w1, w20
	beq	.L1594
	ldrh	w1, [x0,3704]
	cmp	w1, w20
	beq	.L1594
	mov	w3, 6
	ldr	x4, [x0,1280]
	ldr	x2, [x0,3568]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L1594
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
	bne	.L1596
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L1594
.L1596:
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
	bcs	.L1594
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	add	x0, x19, 3568
	bl	List_remove_node
	ldrh	w0, [x19,3584]
	sub	w0, w0, #1
	strh	w0, [x19,3584]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1594:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	mov	x19, x0
	ldrh	w2, [x0,10]
	ldr	x21, [x0,32]
	ldr	x25, [x0,16]
	lsl	w2, w2, 1
	ldr	x20, [x0,40]
	mov	x0, x21
	bl	ftl_memset
	mov	w0, 0
.L1601:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1614
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1602:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L1615
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x25,x3]
	cmp	w4, w2
	bne	.L1603
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L1603:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1602
.L1615:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1601
.L1614:
	mov	w24, 0
	ldrh	w26, [x21]
	mov	w20, w24
	adrp	x27, .LANCHOR2
.L1606:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L1616
	ldrh	w0, [x19]
	uxtw	x22, w20
	cmp	w0, w20
	bne	.L1607
	add	x0, x27, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x0,820]
	cmp	w1, w0
	bcs	.L1607
	strh	w0, [x21,x22,lsl 1]
.L1607:
	lsl	x22, x22, 1
	ldrh	w23, [x21,x22]
	cmp	w26, w23
	bls	.L1608
	mov	w24, w20
	mov	w26, w23
.L1608:
	cbnz	w23, .L1609
	ldrh	w0, [x25,x22]
	cbz	w0, .L1609
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	w23, [x25,x22]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L1609:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1606
.L1616:
	mov	w0, w24
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
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
.L1618:
	cmp	w20, w2
	beq	.L1622
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L1619
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1620
	adrp	x2, .LANCHOR2+1470
	adrp	x0, .LC37
	add	x0, x0, :lo12:.LC37
	ldrh	w2, [x2,#:lo12:.LANCHOR2+1470]
	bl	printk
.L1621:
	b	.L1621
.L1620:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L1622
.L1619:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1618
.L1622:
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
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	mov	x19, x0
	stp	x21, x22, [sp,32]
	add	x22, x20, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w21, [x22,1400]
	mov	w24, w1
	stp	x27, x28, [sp,80]
	mov	x25, x2
	cbnz	w21, .L1625
	adrp	x23, .LC38
	mov	w26, 65535
	add	x28, x22, 1408
	add	x23, x23, :lo12:.LC38
.L1626:
	ldr	w1, [x22,896]
	add	w1, w1, 1
	str	w1, [x22,896]
	ldrh	w1, [x22,820]
	ldrh	w0, [x19,2]
	sub	w1, w1, #1
	cmp	w0, w1
	bge	.L1627
	ldrh	w0, [x19]
	cmp	w0, w26
	bne	.L1628
.L1627:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1628:
	ldrh	w1, [x19]
	mov	w2, 16
	ldr	x0, [x19,16]
	ldrh	w27, [x0,x1,lsl 1]
	ldrh	w1, [x19,2]
	ldr	x0, [x22,1112]
	orr	w1, w1, w27, lsl 10
	str	w1, [x28,4]
	mov	w1, 0
	str	x25, [x28,8]
	str	x0, [x28,16]
	bl	ftl_memset
	ldr	x1, [x28,16]
	ldr	w0, [x19,48]
	str	w0, [x1,4]
	strh	w24, [x1,8]
	ldrh	w0, [x19,4]
	strh	w0, [x1]
	mov	x0, x28
	strh	w27, [x1,2]
	mov	w1, 1
	mov	w3, w1
	mov	w2, w1
	bl	FlashProgPages
	ldrh	w3, [x19,2]
	add	w3, w3, 1
	uxth	w3, w3
	strh	w3, [x19,2]
	ldr	w1, [x28]
	cmn	w1, #1
	bne	.L1629
	ldr	w1, [x28,4]
	mov	x0, x23
	add	w21, w21, 1
	bl	printk
	uxth	w21, w21
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L1630
	ldrh	w1, [x22,820]
	sub	w1, w1, #1
	strh	w1, [x19,2]
.L1630:
	cmp	w21, 3
	bls	.L1626
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x0, .LC39
	add	x0, x0, :lo12:.LC39
	mov	w2, w21
	ldr	w1, [x20,1412]
	bl	printk
	mov	w0, 1
	str	w0, [x20,1400]
	b	.L1625
.L1629:
	cbz	w1, .L1632
	strh	w27, [x19,60]
.L1632:
	cmp	w3, 1
	beq	.L1626
	cmp	w1, 256
	beq	.L1626
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x19,40]
	ldr	w1, [x20,1412]
	str	w1, [x0,w24,uxtw 2]
.L1625:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	adrp	x20, .LANCHOR2
	ldr	x22, [x0,16]
	ldr	x23, [x0,40]
	bl	ftl_free_no_use_map_blk
	ldrh	w4, [x19,10]
	ldrh	w2, [x19,8]
	sub	w1, w4, #4
	cmp	w2, w1
	bge	.L1637
	ldrh	w2, [x19,60]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1639
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w2, [x19,2]
	ldrh	w1, [x1,820]
	cmp	w2, w1
	bcc	.L1639
.L1637:
	ldrh	w1, [x19,60]
	uxth	w21, w0
	mov	w0, 65535
	cmp	w1, w0
	beq	.L1641
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w3, [x19,2]
	ldrh	w0, [x0,820]
	cmp	w0, w3
	bhi	.L1641
	mov	x2, 0
.L1642:
	uxth	w0, w2
	cmp	w0, w4
	bcs	.L1643
	add	x2, x2, 1
	add	x5, x22, x2, lsl 1
	ldrh	w5, [x5,-2]
	cmp	w5, w1
	bne	.L1642
	mov	w21, w0
.L1643:
	ubfiz	x2, x0, 1, 16
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	ldrh	w2, [x22,x2]
	bl	printk
	mov	w0, -1
	strh	w0, [x19,60]
.L1641:
	ubfiz	x21, x21, 1, 16
	ldrh	w25, [x22,x21]
	cbz	w25, .L1639
	ldr	w0, [x19,52]
	cbnz	w0, .L1639
	mov	w1, 1
	str	w1, [x19,52]
	strh	w0, [x22,x21]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,820]
	cmp	w1, w0
	bcc	.L1645
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1645:
	add	x24, x20, :lo12:.LANCHOR2
	adrp	x26, .LC41
	mov	w21, 0
	add	x22, x24, 1408
	mov	w27, 1
	add	x26, x26, :lo12:.LC41
.L1646:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L1656
	ubfiz	x28, x21, 2, 16
	ldr	w0, [x23,x28]
	cmp	w25, w0, lsr 10
	bne	.L1647
	ldr	x3, [x24,1112]
	mov	w1, 1
	ldr	x0, [x24,1056]
	mov	w2, w1
	str	x3, [x22,16]
	str	x0, [x22,8]
	str	x3, [x29,104]
	ldr	w0, [x23,x28]
	str	w0, [x22,4]
	mov	x0, x22
	bl	FlashReadPages
	ldr	w0, [x22]
	ldr	x3, [x29,104]
	cmn	w0, #1
	bne	.L1648
	str	wzr, [x23,x28]
	mov	x0, x26
	ldr	w1, [x22,4]
	ldrh	w2, [x3,8]
	bl	printk
	str	w27, [x24,1400]
	b	.L1647
.L1648:
	ldr	x2, [x22,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L1647:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1646
.L1656:
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1639:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,820]
	cmp	w1, w0
	bcc	.L1650
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1650:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
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
	cbnz	w20, .L1658
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1659
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
	b	.L1658
.L1659:
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
.L1660:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1663
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x23,x3]
	cmp	w22, w1, lsr 10
	bne	.L1661
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
.L1661:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1660
.L1663:
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
.L1658:
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
.L1666:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L1682
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L1666
	b	.L1667
.L1682:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L1669:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L1683
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L1678
	cmp	w5, w7
	bcc	.L1670
.L1678:
	mov	w5, w7
	mov	w6, w0
.L1670:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L1669
.L1683:
	cmp	w0, w2
	bcc	.L1667
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,3816]
	mov	w0, w2
.L1673:
	cmp	w3, w2
	beq	.L1667
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L1674
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L1674:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L1673
.L1667:
	ret
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	add	x3, x21, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	mov	x26, x1
	mov	w27, w2
	ldrh	w1, [x3,826]
	mov	x2, 1
	ldr	x20, [x3,1264]
	add	w1, w1, 7
	lsr	w28, w0, w1
	lsl	x1, x2, x1
	sub	w1, w1, #1
	and	w23, w1, w0
	ldrh	w1, [x3,858]
	uxth	w25, w28
	mov	x0, 0
	uxth	x23, w23
.L1685:
	uxth	x19, w0
	cmp	w19, w1
	bcs	.L1699
	add	x0, x0, 1
	add	x2, x20, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w25
	bne	.L1685
.L1686:
	cbnz	w27, .L1687
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1264]
	add	x1, x1, x19, lsl 4
	ldr	x0, [x1,8]
	ldr	w0, [x0,x23,lsl 2]
	str	w0, [x26]
	b	.L1688
.L1687:
	add	x1, x21, :lo12:.LANCHOR2
	lsl	x0, x19, 4
	ldr	w3, [x26]
	ldr	x2, [x1,1264]
	add	x2, x2, x0
	ldr	x2, [x2,8]
	str	w3, [x2,x23,lsl 2]
	strh	w25, [x1,3816]
	ldr	x2, [x1,1264]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L1688:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21,1264]
	add	x19, x0, x19, lsl 4
	ldr	w0, [x19,4]
	cmn	w0, #1
	beq	.L1696
	add	w0, w0, 1
	str	w0, [x19,4]
	b	.L1696
.L1699:
	bl	select_l2p_ram_region
	uxth	x19, w0
	ubfiz	x22, x19, 4, 16
	mov	w2, 65535
	add	x1, x20, x22
	ldrh	w3, [x20,x22]
	cmp	w3, w2
	beq	.L1691
	ldr	w1, [x1,4]
	tbz	w1, #31, .L1691
	bl	flush_l2p_region
.L1691:
	add	x20, x21, :lo12:.LANCHOR2
	ubfiz	x0, x25, 2, 16
	ldr	x1, [x20,1240]
	ldr	w24, [x1,x0]
	cbnz	w24, .L1692
	ldr	x0, [x20,1264]
	mov	w1, 255
	ldrh	w2, [x20,828]
	add	x0, x0, x22
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x20,1264]
	strh	w25, [x0,x22]
	ldr	x0, [x20,1264]
	add	x22, x0, x22
	str	w24, [x22,4]
	b	.L1686
.L1692:
	ldr	x1, [x20,1264]
	add	x0, x20, 1408
	str	w24, [x0,4]
	add	x1, x1, x22
	ldr	x1, [x1,8]
	str	x1, [x0,8]
	ldr	x1, [x20,1112]
	str	x1, [x0,16]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x20,1408]
	cmp	w0, 256
	bne	.L1693
	and	w28, w28, 65535
	adrp	x0, .LC42
	mov	w2, w24
	mov	w1, w28
	add	x0, x0, :lo12:.LC42
	lsr	w24, w24, 10
	bl	printk
	strh	w24, [x20,3812]
	add	x0, x20, 3752
	ldr	x2, [x20,1264]
	mov	w1, w28
	add	x2, x2, x22
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1693:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,1264]
	add	x0, x0, x22
	str	wzr, [x0,4]
	ldr	x0, [x1,1264]
	strh	w25, [x0,x22]
	b	.L1686
.L1696:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
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
	bhi	.L1701
	lsr	w23, w25, w23
	mov	w21, 0
	add	x27, x19, 3824
.L1702:
	cbz	w22, .L1701
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
	beq	.L1704
	cbz	w2, .L1704
	ldr	x0, [x19,1064]
	mov	w1, 1
	str	w2, [x29,108]
	mov	w2, w1
	str	x0, [x29,112]
	add	x0, x29, 104
	str	xzr, [x29,120]
	bl	FlashReadPages
	b	.L1705
.L1704:
	ldr	x0, [x19,1064]
	mov	w1, 0
	ldrh	w2, [x19,828]
	bl	ftl_memset
.L1705:
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
	b	.L1702
.L1701:
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
	bhi	.L1716
	adrp	x26, .LC43
	add	x0, x3, 3824
	lsr	w20, w23, w20
	mov	w21, 0
	mov	x28, x3
	add	x26, x26, :lo12:.LC43
	str	x0, [x29,104]
.L1717:
	cbz	w22, .L1725
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
	cbz	w4, .L1719
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
	bne	.L1721
	mov	w1, w20
	mov	w2, w4
	mov	x0, x26
	bl	printk
	ldr	x0, [x29,104]
	mov	w1, w20
	ldr	x2, [x28,1064]
	bl	FtlMapWritePage
.L1721:
	ldr	x1, [x28,1064]
	ubfiz	x24, x24, 9, 16
	mov	x0, x25
	mov	w2, w27
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L1722
.L1719:
	mov	x0, x25
	mov	w1, w4
	mov	w2, w27
	bl	ftl_memset
.L1722:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1717
.L1725:
	mov	w0, w21
.L1716:
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
	cbnz	w0, .L1729
	cmp	w2, 4
	bhi	.L1729
	add	w2, w2, 1
	strh	w2, [x3,3888]
	b	.L1728
.L1729:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	strh	wzr, [x0,3888]
	str	wzr, [x0,3892]
	ldrh	w5, [x0,756]
	ldr	x6, [x0,1184]
	mov	x0, 0
.L1730:
	cmp	w5, w0, uxth
	bls	.L1728
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L1731
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,3892]
	add	w2, w4, w2
	str	w2, [x3,3892]
.L1731:
	add	x0, x0, 1
	b	.L1730
.L1728:
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
	cbz	w1, .L1734
	adrp	x1, .LANCHOR2
	str	wzr, [x0,56]
	add	x2, x1, :lo12:.LANCHOR2
	mov	x21, x1
	ldr	w3, [x2,1400]
	cbnz	w3, .L1734
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
	cbz	w3, .L1738
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1739
.L1738:
	ldrh	w2, [x2,828]
	mov	x0, x1
	mov	w1, 255
	bl	ftl_memset
.L1739:
	add	x2, x21, :lo12:.LANCHOR2
	mov	x0, x20
	mov	w1, w19
	ldr	x2, [x2,1416]
	bl	FtlMapWritePage
.L1734:
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
	cbnz	w0, .L1742
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
	strh	w1, [x19,3534]
	ldrh	w1, [x19,3610]
	ldrb	w2, [x19,3614]
	str	w0, [x19,3520]
	mov	w0, 72
	orr	w1, w2, w1, lsl 6
	strh	w1, [x19,3536]
	ldrh	w1, [x19,3656]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x19,3538]
	ldrh	w1, [x19,3658]
	ldrb	w2, [x19,3662]
	str	w0, [x19,3524]
	ldrh	w0, [x19,3902]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x19,3528]
	ldrh	w0, [x19,770]
	strb	w0, [x19,3530]
	ldrb	w0, [x19,3616]
	strb	w0, [x19,3531]
	strh	w1, [x19,3540]
	ldrb	w0, [x19,3664]
	ldrh	w1, [x19,3704]
	strb	w0, [x19,3532]
	strh	w1, [x19,3542]
	ldrh	w1, [x19,3706]
	ldrb	w0, [x19,3712]
	strb	w0, [x19,3533]
	ldr	w0, [x19,904]
	ldrb	w2, [x19,3710]
	str	w0, [x19,3552]
	ldr	w0, [x19,872]
	orr	w1, w2, w1, lsl 6
	str	w0, [x19,3560]
	ldr	w0, [x19,876]
	str	w0, [x19,3556]
	ldrh	w2, [x19,828]
	ldr	x0, [x19,1416]
	strh	w1, [x19,3544]
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
	cbz	w0, .L1743
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
.L1743:
	add	x19, x22, :lo12:.LANCHOR2
	mov	w0, 0
	mov	w23, 0
	mov	w25, 65535
	add	x20, x19, 1408
	bl	FtlUpdateVaildLpn
.L1744:
	ldr	x0, [x19,1048]
	str	x0, [x20,8]
	ldr	x0, [x19,1112]
	str	x0, [x20,16]
	ldrh	w0, [x19,3896]
	ldrh	w1, [x19,3898]
	orr	w0, w1, w0, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
	ldrh	w0, [x19,820]
	ldrh	w1, [x19,3898]
	sub	w0, w0, #1
	cmp	w1, w0
	blt	.L1745
	ldrh	w0, [x19,3896]
	ldrh	w25, [x19,3900]
	strh	wzr, [x19,3898]
	strh	w0, [x19,3900]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,872]
	str	w1, [x19,3904]
	add	w2, w1, 1
	str	w2, [x19,872]
	ubfiz	w2, w0, 10, 16
	str	w2, [x20,4]
	str	w1, [x21,4]
	mov	w1, 1
	strh	w0, [x19,3896]
	mov	w2, w1
	strh	w0, [x21,2]
	mov	w3, w1
	mov	x0, x20
	bl	FlashProgPages
.L1745:
	ldrh	w1, [x19,3898]
	ldr	w2, [x20]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,3898]
	bne	.L1746
	cmp	w1, 1
	bne	.L1747
	ldrh	w1, [x19,820]
	sub	w1, w1, #1
	strh	w1, [x19,3898]
.L1747:
	add	w23, w23, 1
	uxth	w23, w23
	cmp	w23, 3
	bls	.L1744
	add	x22, x22, :lo12:.LANCHOR2
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w23
	ldr	w1, [x22,1412]
	bl	printk
	mov	w0, 1
	str	w0, [x22,1400]
	b	.L1742
.L1746:
	cmp	w1, 1
	beq	.L1744
	cmp	w2, 256
	beq	.L1744
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1742
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1742:
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
.L1754:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,758]
	cmp	w0, w21
	bls	.L1795
	mov	x27, 0
	ldrh	w3, [x1,748]
	ldrh	w2, [x1,830]
	mov	w20, w27
	mov	x23, x1
.L1796:
	cmp	w3, w27, uxth
	bls	.L1829
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
	cbnz	w0, .L1755
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
.L1755:
	add	x27, x27, 1
	b	.L1796
.L1829:
	cbz	w20, .L1758
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	adrp	x28, .LANCHOR0
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L1759:
	cmp	w20, w23, uxth
	bls	.L1758
	add	x0, x19, :lo12:.LANCHOR2
	mul	x5, x23, x26
	ldr	x0, [x0,992]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	ldr	w4, [x1,4]
	cmn	w0, #1
	ldr	x25, [x1,16]
	ubfx	x24, x4, 10, 16
	bne	.L1761
	mov	w6, 16
.L1760:
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
	bne	.L1762
	str	w27, [x0,x5]
	b	.L1761
.L1762:
	ldr	w0, [x0,x5]
	cmn	w0, #1
	bne	.L1761
	sub	w6, w6, #1
	uxth	w6, w6
	cbnz	w6, .L1760
.L1761:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,992]
	ldr	w1, [x1,x5]
	cmn	w1, #1
	beq	.L1764
	ldr	w0, [x0,872]
	cmn	w0, #1
	beq	.L1765
	ldr	w1, [x25,4]
	cmp	w0, w1
	bhi	.L1766
.L1765:
	ldr	w0, [x25,4]
	cmn	w0, #1
	beq	.L1766
	add	x1, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,872]
.L1766:
	ldrh	w0, [x25]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1768
	bhi	.L1769
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1767
	add	x5, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,840]
	ldrh	w0, [x5,1392]
	sub	w2, w6, #1
	ldr	x5, [x5,1224]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1782
.L1769:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1771
	cmp	w0, w22
	bne	.L1767
	mov	w0, w24
	b	.L1828
.L1771:
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
.L1773:
	cmp	w1, w2
	ble	.L1830
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bls	.L1774
	ldr	w2, [x5]
	cbnz	w2, .L1775
	cmp	w0, w6
	beq	.L1775
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,3908]
.L1775:
	mov	w0, 0
.L1776:
	cmp	w0, w1
	beq	.L1831
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
	b	.L1776
.L1831:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x0, [x2,1256]
	str	w5, [x0,x7]
	ldr	x0, [x2,1192]
	strh	w24, [x0,x8,lsl 1]
	tbz	w1, #31, .L1778
	b	.L1767
.L1774:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1773
.L1830:
	tbz	w1, #31, .L1811
	b	.L1767
.L1778:
	ldrh	w0, [x2,3908]
	ldr	w2, [x2,848]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1767
.L1811:
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w3, [x25,4]
	strh	w0, [x2,3908]
	ldr	x0, [x2,1256]
	str	w3, [x0,x1,lsl 2]
	ldr	x0, [x2,1192]
	b	.L1827
.L1788:
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bhi	.L1832
	sub	w1, w1, #1
	sxth	x1, w1
.L1782:
	cmp	w1, w2
	bgt	.L1788
	b	.L1787
.L1832:
	ldr	w2, [x5]
	cbnz	w2, .L1784
	cmp	w0, w6
	beq	.L1784
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,1392]
.L1784:
	mov	w0, 0
.L1785:
	cmp	w0, w1
	beq	.L1833
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
	b	.L1785
.L1833:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x2, [x0,1224]
	str	w5, [x2,x7]
	ldr	x0, [x0,1208]
	strh	w24, [x0,x8,lsl 1]
.L1787:
	tbnz	w1, #31, .L1767
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,840]
	ldrh	w5, [x0,1392]
	sub	w2, w2, #1
	sub	w2, w2, w5
	cmp	w1, w2, sxth
	bgt	.L1767
	ldr	x2, [x0,1224]
	add	w5, w5, 1
	ldr	w3, [x25,4]
	strh	w5, [x0,1392]
	str	w3, [x2,x1,lsl 2]
	ldr	x0, [x0,1208]
.L1827:
	strh	w24, [x0,x1,lsl 1]
	b	.L1767
.L1768:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 3896
	ldrh	w2, [x0,3896]
	cmp	w2, w22
	bne	.L1789
	strh	w24, [x0,3896]
	ldr	w0, [x25,4]
	str	w0, [x1,8]
	b	.L1767
.L1789:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1790
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1790:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x25,4]
	add	x0, x1, 3896
	ldr	w5, [x0,8]
	cmp	w5, w2
	bcs	.L1791
	ldrh	w2, [x1,3896]
	strh	w2, [x0,4]
	strh	w24, [x1,3896]
	ldr	w1, [x25,4]
	str	w1, [x0,8]
	b	.L1767
.L1791:
	strh	w24, [x0,4]
	b	.L1767
.L1764:
	add	x0, x28, :lo12:.LANCHOR0
	ldrb	w1, [x0,136]
	mov	w0, w24
	cbz	w1, .L1792
.L1828:
	mov	w1, 0
.L1792:
	bl	FtlFreeSysBlkQueueIn
.L1767:
	add	x23, x23, 1
	b	.L1759
.L1758:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1754
.L1795:
	ldr	x2, [x1,1192]
	ldrh	w0, [x2]
	cbz	w0, .L1797
.L1800:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x2, [x1,1208]
	ldrh	w0, [x2]
	cbz	w0, .L1798
	b	.L1821
.L1797:
	ldrh	w3, [x1,3908]
	cbz	w3, .L1800
	ldr	w1, [x1,848]
.L1801:
	cmp	w0, w1
	bcs	.L1800
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1802
	mov	w6, w0
.L1803:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,848]
	cmp	w0, w2
	bcs	.L1800
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
	b	.L1803
.L1802:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1801
.L1798:
	ldrh	w3, [x1,1392]
	cbz	w3, .L1821
	ldrh	w1, [x1,840]
.L1806:
	cmp	w0, w1
	mov	w6, w0
	bge	.L1821
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1807
.L1808:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,840]
	cmp	w0, w2
	bge	.L1821
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
	b	.L1808
.L1807:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1806
.L1821:
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
	beq	.L1835
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
.L1835:
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
	cbz	w1, .L1837
	bl	ftl_set_blk_mode.part.17
	b	.L1836
.L1837:
	adrp	x1, .LANCHOR0+16
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L1836:
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
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	adrp	x21, .LANCHOR2
	ldr	x0, [x0,40]
	mov	w26, 0
	str	x0, [x29,120]
	ldrh	w23, [x19,6]
	ldrh	w0, [x19,8]
	str	w0, [x29,116]
	lsl	w2, w23, 2
	ldr	x0, [x29,120]
	ldr	x22, [x19,16]
	ldr	x25, [x19,24]
	bl	ftl_memset
	str	wzr, [x19,52]
	add	x1, x21, :lo12:.LANCHOR2
	str	wzr, [x19,48]
	add	x6, x1, 1408
	mov	x27, x1
	mov	x24, x6
	ldr	x0, [x1,1048]
	ldr	x20, [x1,1112]
	str	x0, [x1,1416]
	mov	w0, -1
	str	x20, [x1,1424]
	strh	w0, [x19]
	strh	w0, [x19,2]
	mov	w0, 1
	str	w0, [x19,56]
	ldr	w0, [x29,116]
	sub	w0, w0, #1
	str	w0, [x29,112]
.L1841:
	ldr	w0, [x29,116]
	cmp	w26, w0
	bge	.L1858
	ldr	w0, [x29,112]
	sxtw	x28, w26
	cmp	w26, w0
	bne	.L1842
	lsl	x0, x28, 1
	mov	w1, 1
	add	x24, x22, x0
	ldrh	w0, [x22,x0]
	bl	FtlGetLastWrittenPage
	strh	w26, [x19]
	sxth	w27, w0
	add	w0, w0, 1
	strh	w0, [x19,2]
	add	x26, x21, :lo12:.LANCHOR2
	add	w27, w27, 1
	add	x26, x26, 1408
	ldr	w0, [x25,x28,lsl 2]
	mov	w25, 0
	str	w0, [x19,48]
.L1843:
	cmp	w25, w27
	bge	.L1858
	ldrh	w0, [x24]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w25, w0, lsl 10
	str	w0, [x26,4]
	mov	x0, x26
	bl	FlashReadPages
	ldr	w0, [x26]
	cmn	w0, #1
	beq	.L1844
	ldrh	w0, [x20,8]
	cmp	w0, w23
	bcs	.L1845
	ldrh	w2, [x20]
	ldrh	w1, [x19,4]
	cmp	w2, w1
	bne	.L1845
	ubfiz	x0, x0, 2, 16
	ldr	x2, [x29,120]
	ldr	w1, [x26,4]
	str	w1, [x2,x0]
	b	.L1845
.L1844:
	ldrh	w0, [x24]
	strh	w0, [x19,60]
.L1845:
	add	w25, w25, 1
	sxth	w25, w25
	b	.L1843
.L1858:
	add	x20, x21, :lo12:.LANCHOR2
	add	x0, x20, 3752
	cmp	x19, x0
	bne	.L1848
	b	.L1885
.L1842:
	ldr	x0, [x27,1048]
	lsl	x1, x28, 1
	str	x0, [x24,8]
	add	x28, x22, x1
	ldrh	w0, [x27,820]
	ldrh	w1, [x22,x1]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x24,4]
	mov	w2, w1
	mov	x0, x24
	bl	FlashReadPages
	ldr	w0, [x24]
	cmn	w0, #1
	beq	.L1872
	ldrh	w1, [x20]
	mov	w3, 0
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1882
	ldrh	w0, [x20,8]
	mov	w1, 64245
	cmp	w0, w1
	bne	.L1882
.L1850:
	ldrh	w0, [x27,820]
	sub	w0, w0, #1
	cmp	w3, w0
	bge	.L1853
	ldr	x0, [x27,1048]
	sbfiz	x2, x3, 3, 32
	ldrh	w1, [x0,x2]
	cmp	w1, w23
	bcs	.L1851
	add	x2, x0, x2
	ubfiz	x1, x1, 2, 16
	ldr	w0, [x2,4]
	ldr	x2, [x29,120]
	str	w0, [x2,x1]
.L1851:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L1850
.L1872:
	mov	w3, 0
.L1882:
	ldrh	w0, [x27,820]
	cmp	w3, w0
	bge	.L1853
	ldrh	w0, [x28]
	mov	w1, 1
	mov	w2, w1
	str	x3, [x29,104]
	orr	w0, w3, w0, lsl 10
	str	w0, [x24,4]
	mov	x0, x24
	bl	FlashReadPages
	ldr	w0, [x24]
	ldr	x3, [x29,104]
	cmn	w0, #1
	beq	.L1854
	ldrh	w1, [x20,8]
	cmp	w1, w23
	bcs	.L1854
	ldrh	w2, [x20]
	ldrh	w0, [x19,4]
	cmp	w2, w0
	bne	.L1854
	ubfiz	x1, x1, 2, 16
	ldr	x2, [x29,120]
	ldr	w0, [x24,4]
	str	w0, [x2,x1]
.L1854:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L1882
.L1853:
	add	w4, w26, 1
	sxth	w26, w4
	b	.L1841
.L1870:
	ubfiz	x23, x24, 2, 32
	ldr	x0, [x20,1248]
	ldr	w1, [x0,x23]
	sub	w2, w1, #1
	cmn	w2, #3
	bls	.L1886
.L1863:
	add	w24, w24, 1
.L1861:
	ldrh	w0, [x20,856]
	cmp	w24, w0
	bcc	.L1870
.L1848:
	mov	x0, x19
	add	x21, x21, :lo12:.LANCHOR2
	bl	ftl_free_no_use_map_blk
	ldrh	w1, [x19,2]
	ldrh	w0, [x21,820]
	cmp	w1, w0
	bne	.L1860
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
	b	.L1860
.L1885:
	ldrh	w0, [x20,860]
	cbz	w0, .L1848
	adrp	x25, .LC47
	mov	w24, 0
	add	x27, x20, 1408
	add	x25, x25, :lo12:.LC47
	b	.L1861
.L1886:
	ldr	x2, [x20,1240]
	ldr	w2, [x2,x23]
	cmp	w1, w2
	beq	.L1863
	ldr	x1, [x20,1048]
	ldr	x26, [x20,1112]
	str	x1, [x27,8]
	mov	w1, 1
	str	x26, [x27,16]
	mov	w2, w1
	ldr	w0, [x0,x23]
	str	w0, [x27,4]
	mov	x0, x27
	bl	FlashReadPages
	ldr	w0, [x27]
	cmn	w0, #1
	beq	.L1863
	ldrh	w1, [x26]
	ldrh	w0, [x20,3756]
	cmp	w1, w0
	bne	.L1863
	ldr	x0, [x20,1240]
	ldr	w3, [x26,4]
	ldr	w1, [x0,x23]
	str	w1, [x27,4]
	ldr	w0, [x0,x23]
	cbnz	w0, .L1865
	str	w0, [x26,4]
	b	.L1866
.L1865:
	mov	w1, 1
	mov	x0, x27
	mov	w2, w1
	str	x3, [x29,120]
	bl	FlashReadPages
	ldr	x3, [x29,120]
.L1866:
	ldr	w4, [x26,4]
	cmp	w3, w4
	bls	.L1863
	ldr	x1, [x20,1248]
	mov	x0, x25
	ldr	x2, [x20,1240]
	ldr	w1, [x1,x23]
	ldr	w2, [x2,x23]
	bl	printk
	ldr	x0, [x20,1248]
	ldr	w1, [x0,x23]
	ldr	x0, [x20,1240]
	str	w1, [x0,x23]
	mov	w1, 0
	ldr	x0, [x20,1248]
	ldrh	w3, [x20,3908]
	ldr	w2, [x0,x23]
	ubfx	x2, x2, 10, 16
.L1867:
	cmp	w1, w3
	bge	.L1887
	ldrh	w0, [x22,w1,sxtw 1]
	cmp	w0, w2
	beq	.L1863
	add	w1, w1, 1
	sxth	w1, w1
	b	.L1867
.L1887:
	ldr	w0, [x20,848]
	cmp	w3, w0
	bcs	.L1863
	ubfiz	x3, x3, 1, 16
	strh	w2, [x22,x3]
	ldrh	w0, [x20,3908]
	add	w0, w0, 1
	strh	w0, [x20,3908]
	mov	w0, w2
	bl	remove_from_free_sys_Queue
	b	.L1863
.L1860:
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
	str	x1, [x0,40]
	mov	w1, -1
	str	x2, [x0,32]
	strh	w1, [x0,60]
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
.L1890:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,858]
	cmp	w1, w2
	bls	.L1892
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
	b	.L1890
.L1892:
	ldr	w3, [x0,848]
	add	x1, x0, 3752
	strh	w3, [x0,3762]
	mov	w3, -3902
	strh	w3, [x0,3756]
	mov	w2, -1
	ldrh	w3, [x0,3908]
	strh	w3, [x0,3760]
	ldrh	w3, [x0,856]
	strh	w3, [x0,3758]
	ldr	x3, [x0,1192]
	str	x3, [x0,3768]
	ldr	x3, [x0,1256]
	str	x3, [x0,3776]
	ldr	x3, [x0,1200]
	strh	w2, [x0,3754]
	strh	w2, [x0,3752]
	str	x3, [x0,3784]
	ldr	x0, [x0,1240]
	str	x0, [x1,40]
	strh	w2, [x1,60]
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
.L1895:
	cmp	w2, w4
	bcs	.L1901
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L1896
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L1896:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1895
.L1901:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,818]
	mov	x3, x0
.L1898:
	cmp	x3, x4
	beq	.L1902
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L1899
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L1899:
	add	x3, x3, 2
	b	.L1898
.L1902:
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
	cbnz	w1, .L1904
	ldr	x19, [x2,3592]
	cbz	x19, .L1905
	ldrh	w3, [x2,3600]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,1280]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L1906:
	cmp	w1, w3
	beq	.L1905
	cmp	w19, w0
	bne	.L1907
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
	b	.L1905
.L1907:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L1905
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1906
.L1904:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L1905:
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
.L1913:
	cmp	w24, w21, uxth
	bls	.L1924
	add	x0, x23, 776
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1914
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L1914:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L1913
.L1924:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x0,818]
	mul	w1, w1, w2
	strh	w1, [x19,4]
	ldr	w1, [x0,652]
	cbz	w1, .L1916
	ldrh	w1, [x19]
	ldr	x0, [x0,1168]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 59
	bhi	.L1916
	mov	w0, 1
	strb	w0, [x19,9]
.L1916:
	adrp	x0, .LANCHOR0+24
	ldrb	w0, [x0,#:lo12:.LANCHOR0+24]
	cbz	w0, .L1917
	mov	w0, 1
	strb	w0, [x19,9]
.L1917:
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
	adrp	x19, .LANCHOR2
	str	x23, [sp,48]
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	add	x21, x20, 1408
	add	x23, x20, 3896
	ldr	x0, [x20,1112]
	ldrh	w2, [x20,756]
	str	x0, [x21,16]
	ldr	x0, [x20,1184]
	lsl	w2, w2, 1
	str	xzr, [x21,8]
	bl	ftl_memset
	ldrh	w0, [x20,3896]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L1926
.L1934:
	mov	w19, -1
	b	.L1927
.L1926:
	mov	w1, 1
	bl	FtlGetLastWrittenPage
	sxth	w22, w0
	add	w0, w0, 1
	strh	w0, [x23,2]
	mov	w23, 61604
.L1928:
	tbnz	w22, #31, .L1933
	ldrh	w0, [x20,3896]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w22, w0, lsl 10
	str	w0, [x21,4]
	ldr	x0, [x20,1048]
	str	x0, [x21,8]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1929
	ldr	x0, [x20,1048]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L1929
	ldr	x0, [x20,1112]
	ldrh	w0, [x0]
	cmp	w0, w23
	bne	.L1929
.L1933:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w2, 48
	add	x22, x20, 1408
	add	x0, x20, 3520
	adrp	x21, .LANCHOR0
	ldr	x1, [x22,8]
	bl	ftl_memcpy
	ldr	x1, [x22,8]
	ldrh	w2, [x20,756]
	ldr	x0, [x20,1184]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x20,756]
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
	ldrh	w0, [x20,860]
	cbz	w0, .L1931
	ldrh	w0, [x20,756]
	ldr	x3, [x22,8]
	lsr	w1, w0, 3
	ldrh	w2, [x20,856]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x20,1248]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L1931
.L1929:
	sub	w22, w22, #1
	sxth	w22, w22
	b	.L1928
.L1931:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,3520]
	cmp	w3, w2
	bne	.L1934
	ldrb	w3, [x0,3530]
	ldrh	w2, [x0,770]
	ldrh	w4, [x0,3528]
	strh	w4, [x0,3902]
	cmp	w3, w2
	bne	.L1934
	ldrh	w3, [x0,818]
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w2, [x0,824]
	str	w4, [x0,3912]
	mul	w3, w3, w4
	str	w3, [x0,1396]
	strh	wzr, [x0,3922]
	mul	w3, w3, w2
	str	w3, [x21,4048]
	ldrh	w2, [x0,1302]
	ldr	w3, [x0,760]
	strb	wzr, [x0,3926]
	sub	w2, w3, w2
	ldrh	w3, [x0,748]
	sub	w2, w2, w4
	ldrh	w4, [x0,3534]
	strh	w4, [x0,3608]
	udiv	w2, w2, w3
	ldrh	w3, [x0,3536]
	strh	w2, [x0,3916]
	lsr	w5, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,3614]
	ldrb	w3, [x0,3531]
	strb	w3, [x0,3616]
	mov	w3, -1
	strh	w3, [x0,3920]
	ldrh	w3, [x0,3538]
	strh	w3, [x0,3656]
	ldrh	w3, [x0,3540]
	strh	w5, [x0,3610]
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
	strb	wzr, [x0,3928]
	strb	w3, [x0,3710]
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
	bls	.L1935
	str	w1, [x0,872]
.L1935:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,3556]
	ldr	w2, [x0,876]
	cmp	w1, w2
	bls	.L1936
	str	w1, [x0,876]
.L1936:
	mov	w0, 65535
	cmp	w4, w0
	beq	.L1937
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3608
	bl	make_superblock
.L1937:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x1, 3656
	ldrh	w2, [x1,3656]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1938
	bl	make_superblock
.L1938:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x1, 3704
	ldrh	w2, [x1,3704]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L1939
	bl	make_superblock
.L1939:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x19, 3920
	ldrh	w2, [x19,3920]
	mov	w19, 0
	cmp	w2, w1
	beq	.L1927
	bl	make_superblock
.L1927:
	mov	w0, w19
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
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
.L1945:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,756]
	cmp	w19, w1
	bge	.L1952
	mov	w20, 0
	ldrh	w3, [x0,748]
	ldrh	w2, [x0,818]
	mov	w28, w20
	mov	x26, x0
.L1953:
	cmp	w28, w3
	bge	.L1954
	add	x0, x26, 776
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L1946
	add	w20, w2, w20
	sxth	w20, w20
.L1946:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L1953
.L1954:
	cbz	w20, .L1948
	sdiv	w20, w25, w20
	sxth	w20, w20
	b	.L1949
.L1948:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,1184]
	strh	w24, [x0,w19,sxtw 1]
.L1949:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1280]
	smaddl	x1, w19, w23, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,3608]
	cmp	w19, w1
	beq	.L1950
	ldrh	w1, [x0,3656]
	cmp	w19, w1
	beq	.L1950
	ldrh	w1, [x0,3704]
	cmp	w19, w1
	beq	.L1950
	ldr	x0, [x0,1184]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L1951
	add	w27, w27, 1
	mov	w0, w19
	uxth	w27, w27
	bl	INSERT_FREE_LIST
	b	.L1950
.L1951:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L1950:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L1945
.L1952:
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
	adrp	x0, .LANCHOR2
	mov	w1, 19539
	add	x0, x0, :lo12:.LANCHOR2
	movk	w1, 0x4654, lsl 16
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 3968
	add	x29, sp, 0
	ldr	w3, [x0,3968]
	cmp	w3, w1
	bne	.L1955
	mov	w1, 72
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	ldr	w3, [x1,128]
	ldr	w1, [x1,132]
	str	w1, [x2,92]
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
	str	w3, [x2,88]
	bl	FtlVendorPartWrite
.L1955:
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
	cbz	w4, .L1958
	ldr	w3, [x3,920]
	cmp	w3, 29
	mov	w3, 4
	csel	w1, w1, w3, hi
.L1958:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,136]
	cmp	w4, 31
	bhi	.L1959
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,136]
.L1959:
	cbnz	w0, .L1960
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,1160]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L1961
.L1960:
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
.L1961:
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
	adrp	x19, .LANCHOR2
	str	x21, [sp,32]
	add	x20, x19, :lo12:.LANCHOR2
	add	x21, x20, 3968
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,3968]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L1966
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,3968]
.L1966:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	adrp	x20, .LANCHOR4
	ldr	w3, [x0,3968]
	cmp	w3, w2
	bne	.L1967
	add	x2, x20, :lo12:.LANCHOR4
	ldr	w3, [x0,4056]
	ldr	w1, [x0,4028]
	str	w1, [x0,868]
	str	w3, [x2,128]
	ldr	w3, [x0,4060]
	str	w3, [x2,132]
	ldr	w2, [x0,3976]
	str	w2, [x0,880]
	ldr	w2, [x0,3980]
	str	w2, [x0,884]
	ldr	w2, [x0,3984]
	str	w2, [x0,900]
	ldr	w2, [x0,3988]
	str	w2, [x0,896]
	ldr	w2, [x0,3996]
	str	w2, [x0,908]
	ldr	w2, [x0,4000]
	str	w2, [x0,912]
	ldr	w2, [x0,4004]
	str	w2, [x0,888]
	ldr	w2, [x0,4008]
	str	w2, [x0,892]
	ldr	w2, [x0,4012]
	str	w2, [x0,916]
	ldr	w2, [x0,4016]
	str	w2, [x0,920]
.L1967:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	ldr	w2, [x0,4036]
	str	wzr, [x0,864]
	cmp	w2, w1
	bne	.L1968
	mov	w1, 1
	str	w1, [x0,652]
	adrp	x0, .LC45
	adrp	x1, .LC48
	add	x0, x0, :lo12:.LC45
	add	x1, x1, :lo12:.LC48
	bl	printk
.L1968:
	add	x0, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w2, [x0,808]
	ldr	w3, [x0,904]
	ldr	w1, [x0,908]
	ldrh	w0, [x0,756]
	madd	w1, w2, w3, w1
	udiv	w0, w1, w0
	str	w0, [x20,140]
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
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
.L1970:
	cmp	w24, w22, uxth
	bls	.L1977
	add	x0, x23, 776
	mov	w1, w20
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1971
	add	w19, w19, w25
	uxth	w19, w19
.L1971:
	add	x22, x22, 1
	b	.L1970
.L1977:
	cbz	w19, .L1973
	mov	w0, 32768
	sdiv	w19, w0, w19
.L1973:
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
	cbz	x1, .L1979
	ldr	x0, [x2,1280]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,1168]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L1979:
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
	cbz	x1, .L1982
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
.L1984:
	cmp	w3, w4
	beq	.L1987
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L1985
.L1987:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,1168]
	ldrh	w0, [x0,x1]
	b	.L1982
.L1985:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L1984
.L1982:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC49
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	add	x1, x1, :lo12:.LC49
	add	x25, x25, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x23, x0
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x25,4016]
	adrp	x1, .LC50
	adrp	x24, .LANCHOR2
	mov	x0, x20
	add	x1, x1, :lo12:.LC50
	add	x22, x24, :lo12:.LANCHOR2
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC51
	mov	x0, x20
	ldr	w2, [x22,836]
	add	x1, x1, :lo12:.LC51
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+3064
	ldr	w0, [x0,#:lo12:.LANCHOR1+3064]
	cmp	w0, 1
	beq	.L1990
	sub	w0, w20, w23
	b	.L1991
.L1990:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x19, .LANCHOR4
	bl	NandcGetTimeCfg
	add	x19, x19, :lo12:.LANCHOR4
	ldr	w4, [x29,104]
	adrp	x1, .LC52
	ldr	w5, [x29,108]
	add	x1, x1, :lo12:.LC52
	ldr	w3, [x29,100]
	mov	x0, x20
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC53
	mov	x0, x21
	add	x1, x1, :lo12:.LC53
	add	x21, x21, 10
	bl	strcpy
	ldr	w2, [x22,1396]
	adrp	x1, .LC54
	mov	x0, x21
	add	x1, x1, :lo12:.LC54
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,3892]
	adrp	x1, .LC55
	mov	x0, x21
	add	x1, x1, :lo12:.LC55
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,900]
	adrp	x1, .LC56
	mov	x0, x21
	add	x1, x1, :lo12:.LC56
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,888]
	adrp	x1, .LC57
	mov	x0, x21
	add	x1, x1, :lo12:.LC57
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,884]
	adrp	x1, .LC58
	mov	x0, x21
	add	x1, x1, :lo12:.LC58
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,892]
	adrp	x1, .LC59
	mov	x0, x21
	add	x1, x1, :lo12:.LC59
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,896]
	adrp	x1, .LC60
	mov	x0, x21
	add	x1, x1, :lo12:.LC60
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,880]
	adrp	x1, .LC61
	mov	x0, x21
	add	x1, x1, :lo12:.LC61
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x19,128]
	adrp	x1, .LC62
	mov	x0, x21
	add	x1, x1, :lo12:.LC62
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x19,132]
	adrp	x1, .LC63
	mov	x0, x21
	add	x1, x1, :lo12:.LC63
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,872]
	adrp	x1, .LC64
	mov	x0, x21
	add	x1, x1, :lo12:.LC64
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,876]
	adrp	x1, .LC65
	add	x1, x1, :lo12:.LC65
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x22,1302]
	adrp	x1, .LC66
	mov	x0, x21
	add	x1, x1, :lo12:.LC66
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3600]
	adrp	x1, .LC67
	mov	x0, x21
	add	x1, x1, :lo12:.LC67
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,904]
	adrp	x1, .LC68
	mov	x0, x21
	add	x1, x1, :lo12:.LC68
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,908]
	adrp	x1, .LC69
	mov	x0, x21
	add	x1, x1, :lo12:.LC69
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x19,140]
	adrp	x1, .LC70
	mov	x0, x21
	add	x1, x1, :lo12:.LC70
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,912]
	adrp	x1, .LC71
	mov	x0, x21
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,916]
	adrp	x1, .LC72
	mov	x0, x21
	add	x1, x1, :lo12:.LC72
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,920]
	adrp	x1, .LC73
	mov	x0, x21
	add	x1, x1, :lo12:.LC73
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3550]
	adrp	x1, .LC74
	mov	x0, x21
	add	x1, x1, :lo12:.LC74
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3548]
	adrp	x1, .LC75
	mov	x0, x21
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,4048]
	adrp	x1, .LC76
	mov	x0, x21
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,852]
	adrp	x1, .LC77
	mov	x0, x21
	add	x1, x1, :lo12:.LC77
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,744]
	adrp	x1, .LC78
	mov	x0, x21
	add	x1, x1, :lo12:.LC78
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1470]
	adrp	x1, .LC79
	mov	x0, x21
	add	x1, x1, :lo12:.LC79
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,756]
	adrp	x1, .LC80
	mov	x0, x21
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3916]
	adrp	x1, .LC81
	mov	x0, x21
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,760]
	adrp	x1, .LC82
	mov	x0, x21
	add	x1, x1, :lo12:.LC82
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3896]
	adrp	x1, .LC83
	mov	x0, x21
	add	x1, x1, :lo12:.LC83
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1296]
	adrp	x1, .LC84
	mov	x0, x21
	add	x1, x1, :lo12:.LC84
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3610]
	adrp	x1, .LC85
	mov	x0, x21
	add	x1, x1, :lo12:.LC85
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3614]
	adrp	x1, .LC86
	mov	x0, x21
	add	x1, x1, :lo12:.LC86
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3608]
	adrp	x1, .LC87
	mov	x0, x21
	add	x1, x1, :lo12:.LC87
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3616]
	adrp	x1, .LC88
	mov	x0, x21
	add	x1, x1, :lo12:.LC88
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3612]
	adrp	x1, .LC89
	mov	x0, x21
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3608]
	adrp	x1, .LC90
	ldr	x2, [x22,1184]
	mov	x0, x21
	add	x1, x1, :lo12:.LC90
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3658]
	adrp	x1, .LC91
	mov	x0, x21
	add	x1, x1, :lo12:.LC91
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3662]
	adrp	x1, .LC92
	mov	x0, x21
	add	x1, x1, :lo12:.LC92
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3656]
	adrp	x1, .LC93
	mov	x0, x21
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3664]
	adrp	x1, .LC94
	mov	x0, x21
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3660]
	adrp	x1, .LC95
	mov	x0, x21
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3656]
	adrp	x1, .LC96
	ldr	x2, [x22,1184]
	mov	x0, x21
	add	x1, x1, :lo12:.LC96
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3706]
	adrp	x1, .LC97
	mov	x0, x21
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3710]
	adrp	x1, .LC98
	mov	x0, x21
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3704]
	adrp	x1, .LC99
	mov	x0, x21
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3712]
	adrp	x1, .LC100
	mov	x0, x21
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3708]
	adrp	x1, .LC101
	mov	x0, x21
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3922]
	adrp	x1, .LC102
	mov	x0, x21
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3926]
	adrp	x1, .LC103
	mov	x0, x21
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3920]
	adrp	x1, .LC104
	mov	x0, x21
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3928]
	adrp	x1, .LC105
	mov	x0, x21
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3924]
	adrp	x1, .LC106
	mov	x0, x21
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,868]
	adrp	x1, .LC107
	ldr	w2, [x22,652]
	add	x1, x1, :lo12:.LC107
	ldr	w4, [x22,4052]
	mov	x0, x21
	ldr	w5, [x22,4044]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x22,4048]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x22,4040]
	adrp	x1, .LC108
	mov	x0, x19
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,4064]
	adrp	x1, .LC109
	mov	x0, x19
	add	x1, x1, :lo12:.LC109
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,952]
	adrp	x1, .LC110
	mov	x0, x19
	add	x1, x1, :lo12:.LC110
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,954]
	adrp	x1, .LC111
	mov	x0, x19
	add	x1, x1, :lo12:.LC111
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,940]
	adrp	x1, .LC112
	mov	x0, x19
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,956]
	adrp	x1, .LC113
	add	x1, x1, :lo12:.LC113
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC114
	mov	x0, x19
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,3600]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC115
	mov	x0, x19
	add	x1, x1, :lo12:.LC115
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,3920]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L1992
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,1184]
	adrp	x1, .LC116
	mov	x0, x19
	add	x1, x1, :lo12:.LC116
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L1992:
	mov	w0, 0
	adrp	x22, .LC117
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC117
.L1994:
	cmp	w3, w27
	beq	.L1993
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
	bne	.L1994
.L1993:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC118
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x22, x22, :lo12:.LC118
	ldr	x0, [x1,3592]
	ldr	x3, [x1,1280]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L1996:
	cmp	w3, w27
	beq	.L1995
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
	bne	.L1996
.L1995:
	sub	w0, w19, w23
.L1991:
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
	adrp	x2, .LC119
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC45
	add	x2, x2, :lo12:.LC119
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
	ldr	w3, [x1,140]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,920]
	cmp	w3, w2
	bcs	.L2001
	str	wzr, [x1,904]
	ldrh	w3, [x1,756]
	ldr	x5, [x1,1168]
	mov	x1, 0
.L2002:
	cmp	w3, w1
	bls	.L2034
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x1,lsl 1]
	add	x1, x1, 1
	ldr	w2, [x4,904]
	add	w2, w6, w2
	str	w2, [x4,904]
	b	.L2002
.L2034:
	add	x2, x19, :lo12:.LANCHOR2
	add	x4, x0, :lo12:.LANCHOR4
	ldr	w1, [x2,904]
	udiv	w3, w1, w3
	str	w3, [x4,140]
	ldr	w3, [x2,908]
	sub	w1, w1, w3
	ldrh	w3, [x2,808]
	udiv	w1, w1, w3
	str	w1, [x2,904]
	b	.L2004
.L2001:
	ldr	w2, [x1,916]
	cmp	w3, w2
	bls	.L2004
	add	w2, w2, 1
	str	w2, [x1,916]
	mov	w1, 0
.L2006:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w3, [x2,756]
	cmp	w1, w3
	bcs	.L2004
	ubfiz	x3, x1, 1, 32
	ldr	x4, [x2,1168]
	add	w1, w1, 1
	ldrh	w2, [x4,x3]
	add	w2, w2, 1
	strh	w2, [x4,x3]
	b	.L2006
.L2004:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w21, [x1,920]
	ldr	w23, [x0,140]
	add	w0, w21, 256
	cmp	w0, w23
	bls	.L2009
	ldr	w2, [x1,916]
	add	w0, w21, 768
	cmp	w0, w2
	bls	.L2009
	ldr	w0, [x1,652]
	cbz	w0, .L2012
	cmp	w21, 30
	bls	.L2009
.L2012:
	mov	w0, 65535
	b	.L2011
.L2009:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3600]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L2021
	cmp	w21, 30
	bhi	.L2012
.L2021:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x3, [x1,3568]
	cbz	x3, .L2012
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
.L2014:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L2016
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L2012
	ldrh	w2, [x3,4]
	cbz	w2, .L2015
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w21
	bls	.L2020
	cmp	w2, w26
	bcs	.L2015
	mov	w26, w2
	mov	w20, w3
.L2015:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L2014
.L2020:
	mov	w20, w3
.L2016:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2012
	ubfiz	x24, x20, 1, 16
	ldrh	w22, [x25,x24]
	cmp	w22, w21
	bls	.L2018
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L2018
	add	x0, x19, :lo12:.LANCHOR2
	str	w26, [x0,920]
.L2018:
	cmp	w22, w23
	bcs	.L2012
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L2012
	add	w0, w22, 256
	cmp	w0, w23
	bcc	.L2019
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,916]
	cmp	w22, w0
	bcs	.L2012
.L2019:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC120
	ldrh	w5, [x25,x24]
	add	x0, x0, :lo12:.LC120
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,1184]
	ldr	w3, [x19,916]
	ldrh	w4, [x4,x24]
	bl	printk
	mov	w0, 1
	str	w0, [x19,928]
	mov	w0, w20
.L2011:
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
	beq	.L2036
	adrp	x2, .LANCHOR2+1184
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+1184]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L2036:
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
	cbnz	w1, .L2038
	mov	x20, x0
	add	x23, x19, 3704
	adrp	x22, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 3592
.L2039:
	cmp	x20, x23
	bne	.L2040
	ldrh	w2, [x19,3600]
	ldr	w4, [x19,928]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	ldr	w0, [x19,652]
	uxth	w1, w1
	cbz	w0, .L2041
	ldr	w0, [x19,920]
	cmp	w0, 29
	bhi	.L2041
	cmp	w0, 2
	mov	w1, 0
	bls	.L2042
	tbz	x2, 0, .L2069
	cbz	w4, .L2042
.L2069:
	mov	w1, w3
	b	.L2041
.L2040:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2042
	ldrh	w0, [x19,768]
	cmp	w0, 1
	beq	.L2042
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L2042
	ldr	w2, [x19,652]
	ldrh	w0, [x19,3600]
	lsr	w1, w0, 3
	cbz	w2, .L2041
	ldr	w2, [x19,920]
	cmp	w2, 1
	bhi	.L2041
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2041:
	cbz	w1, .L2042
	sub	w1, w1, #1
	uxth	w1, w1
.L2042:
	ldrb	w2, [x20,8]
	mov	x0, x25
	bl	List_pop_index_node
	uxth	w27, w0
	ldrh	w0, [x19,3600]
	sub	w0, w0, #1
	strh	w0, [x19,3600]
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2044
	mov	x0, 0
	ldrh	w5, [x19,748]
	mov	w26, w0
	mov	x3, 56
	mov	w6, 65535
	b	.L2045
.L2044:
	ubfiz	x27, x27, 1, 16
	ldr	x0, [x19,1184]
	mov	w1, -1
	strh	w1, [x0,x27]
	b	.L2039
.L2048:
	ldr	x4, [x19,1016]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2047
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L2047:
	add	x0, x0, 1
.L2045:
	cmp	w5, w0, uxth
	bhi	.L2048
	add	x0, x19, 3608
	uxtw	x28, w27
	cmp	x20, x0
	bne	.L2049
	ldr	w1, [x19,652]
	cbz	w1, .L2049
	ldr	x1, [x19,1168]
	ldrh	w1, [x1,x28,lsl 1]
	cmp	w1, 30
	bls	.L2049
	strb	wzr, [x19,3616]
.L2049:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2050
	lsl	x0, x28, 1
	ldr	x1, [x19,1168]
	mov	w2, 2
	ldrh	w3, [x1,x0]
	cbz	w3, .L2094
	ldrh	w2, [x19,808]
	add	w2, w3, w2
.L2094:
	strh	w2, [x1,x0]
	mov	w1, 0
	ldr	w0, [x19,904]
	add	w0, w0, 1
	str	w0, [x19,904]
	mov	w0, w27
	bl	ftl_set_blk_mode
	b	.L2053
.L2050:
	lsl	x0, x28, 1
	ldr	x2, [x19,1168]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x19,908]
	add	w0, w0, 1
	str	w0, [x19,908]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.17
.L2053:
	lsl	x28, x28, 1
	ldr	x0, [x19,1168]
	ldr	w1, [x19,916]
	ldrh	w0, [x0,x28]
	cmp	w0, w1
	bls	.L2054
	str	w0, [x19,916]
.L2054:
	ldrh	w1, [x19,808]
	mov	x3, 56
	ldr	w2, [x19,904]
	ldr	w0, [x19,908]
	madd	w0, w1, w2, w0
	ldrh	w1, [x19,756]
	udiv	w0, w0, w1
	adrp	x1, .LANCHOR4+140
	str	w0, [x1,#:lo12:.LANCHOR4+140]
	ldr	x1, [x19,1160]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2055:
	cmp	w26, w0, uxth
	bls	.L2096
	ldr	x1, [x19,1016]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2055
.L2096:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2057
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,1016]
	bne	.L2058
	mov	w1, 0
	b	.L2095
.L2058:
	mov	w1, 1
.L2095:
	mov	w2, w26
	bl	FlashEraseBlocks
.L2057:
	ldrb	w1, [x20,8]
	mov	w2, w26
	ldr	x0, [x19,1016]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2059:
	cmp	w26, w1, uxth
	bls	.L2097
	mul	x0, x1, x4
	ldr	x3, [x19,1016]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2060
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
.L2060:
	add	x1, x1, 1
	b	.L2059
.L2097:
	cbz	w2, .L2062
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2062:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2063
	ldr	x0, [x19,1184]
	mov	w1, -1
	strh	w1, [x0,x28]
	b	.L2039
.L2063:
	add	x21, x21, :lo12:.LANCHOR2
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x21,818]
	strh	w27, [x20]
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
.L2038:
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
	adrp	x0, .LANCHOR4+144
	mov	w1, 0
	adrp	x7, .LANCHOR2
	mov	w8, 24
	mov	w9, 1
	mov	w4, 4
	str	wzr, [x0,#:lo12:.LANCHOR4+144]
	mov	w10, 56
.L2099:
	add	x2, x7, :lo12:.LANCHOR2
	adrp	x6, .LANCHOR2
	ldrh	w0, [x2,748]
	cmp	w1, w0
	bcs	.L2103
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
	b	.L2099
.L2103:
	mov	w7, 24
	mov	w4, 4
.L2101:
	add	x1, x6, :lo12:.LANCHOR2
	ldr	w2, [x1,1040]
	cmp	w0, w2
	bcs	.L2104
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
	b	.L2101
.L2104:
	ret
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4
	mov	w1, -1
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w2, [x19,840]
	strh	w1, [x0,160]
	mov	w1, -1
	str	xzr, [x0,152]
	lsl	w2, w2, 1
	str	w1, [x0,172]
	mov	w1, 0
	str	wzr, [x0,164]
	str	wzr, [x0,168]
	ldr	x0, [x19,1208]
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
	add	x0, x19, 3968
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	ldr	x19, [sp,16]
	mov	w0, 0
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
.L2107:
	cmp	w3, w1
	bcs	.L2106
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L2112:
	cmp	w2, w8
	bcs	.L2109
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L2108
	str	wzr, [x10,16]
	b	.L2109
.L2108:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2112
.L2109:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2107
.L2106:
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
.L2114:
	cmp	w2, w1
	bcs	.L2120
	mov	w3, 0
.L2118:
	cmp	w3, w5
	bcs	.L2116
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L2115
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L2116
.L2115:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2118
.L2116:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2114
.L2120:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR4+176
	uxth	w0, w0
	ldrh	w2, [x1,#:lo12:.LANCHOR4+176]
	adrp	x1, .LANCHOR2+976
	ldr	x3, [x1,#:lo12:.LANCHOR2+976]
	mov	x1, 0
.L2122:
	cmp	w2, w1, uxth
	bls	.L2126
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L2122
	mov	w0, 1
	b	.L2123
.L2126:
	mov	w0, 0
.L2123:
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
	ldrh	w8, [x4,176]
	adrp	x4, .LANCHOR2
	add	x5, x4, :lo12:.LANCHOR2
	ldr	x7, [x5,976]
.L2128:
	uxth	w5, w6
	cmp	w5, w8
	bcs	.L2132
	add	x6, x6, 1
	add	x10, x7, x6, lsl 1
	ldrh	w10, [x10,-2]
	cmp	w10, w9
	bne	.L2128
.L2132:
	cmp	w5, w8
	bne	.L2130
	ubfiz	x5, x5, 1, 16
	strh	w0, [x7,x5]
	add	x0, x3, :lo12:.LANCHOR4
	ldrh	w5, [x0,176]
	add	w5, w5, 1
	strh	w5, [x0,176]
.L2130:
	add	x3, x3, :lo12:.LANCHOR4
	add	x4, x4, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w5, [x3,178]
	ldr	x6, [x4,984]
	umull	x5, w5, w0
	add	x6, x6, x5
	str	w1, [x6,4]
	ldr	x1, [x4,984]
	add	x0, x1, x5
	str	w2, [x0,8]
	str	w19, [x1,x5]
	ldrh	w0, [x3,178]
	add	w0, w0, 1
	strh	w0, [x3,178]
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
	strh	wzr, [x0,176]
	strh	wzr, [x0,178]
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
	ldrh	w26, [x0,3068]
	mov	w0, 65535
	cmp	w26, w0
	beq	.L2157
	cbnz	w26, .L2136
	b	.L2137
.L2157:
	mov	w26, 0
.L2136:
	adrp	x0, .LANCHOR2+818
	ldr	w1, [x29,152]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+818]
	cmp	w1, w0
	bne	.L2138
.L2137:
	bl	FtlGcPageVarInit
.L2138:
	mov	w0, -1
	adrp	x25, .LANCHOR2
	str	w0, [x29,156]
	adrp	x0, .LC121
	add	x0, x0, :lo12:.LC121
	mov	w22, 0
	mov	w23, 65535
	mov	x21, x25
	mov	x24, x25
	str	x0, [x29,128]
.L2151:
	ldrh	w0, [x19]
	strb	wzr, [x19,8]
	cmp	w0, w23
	beq	.L2158
.L2140:
.L2154:
	add	x0, x25, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w20, 0
	ldrh	w5, [x0,748]
	ldrh	w8, [x0,830]
	add	x5, x19, x5, lsl 1
.L2141:
	cmp	x2, x5
	beq	.L2166
	ldrh	w1, [x2,16]
	cmp	w1, w23
	beq	.L2142
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
.L2142:
	add	x2, x2, 2
	b	.L2141
.L2166:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 0
	mov	x27, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
.L2144:
	cmp	w20, w27, uxth
	bls	.L2167
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
	cbnz	w0, .L2145
	ldr	x1, [x29,112]
	add	x27, x27, 1
	ldr	x5, [x1,16]
	ldrh	w0, [x5]
	cmp	w0, w23
	bne	.L2146
	adrp	x0, .LANCHOR4+168
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR4+168]
	b	.L2139
.L2146:
	mov	w1, w2
	ldr	w0, [x5,12]
	ldr	w2, [x5,8]
	bl	FtlGcUpdatePage
	b	.L2144
.L2145:
	ldr	x0, [x29,128]
	ldrh	w1, [x19]
	bl	printk
	ldr	x0, [x29,144]
	ldr	w0, [x0,652]
	cbnz	w0, .L2147
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2148
.L2147:
	add	x0, x24, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,1168]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 119
	bls	.L2149
.L2148:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	ldr	w0, [x0,x28]
	cmn	w0, #1
	bne	.L2150
.L2149:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,992]
	add	x5, x0, x28
	ldr	w0, [x5,4]
	str	w0, [x29,156]
.L2150:
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
	b	.L2151
.L2167:
	ldr	w0, [x29,152]
	add	w22, w22, 1
	add	w3, w26, 1
	cmp	w22, w0
	uxth	w26, w3
	bcs	.L2153
.L2155:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w26
	bhi	.L2154
	b	.L2158
.L2153:
	ldr	x0, [x29,136]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3068]
	cmp	w0, w23
	beq	.L2155
	add	w0, w0, w22
	strh	w0, [x1,3068]
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w26
	bls	.L2155
	b	.L2156
.L2158:
	mov	w6, 0
.L2139:
	ldr	x0, [x29,136]
	mov	w1, w26
	strh	w26, [x19,2]
	mov	w2, w6
	add	x20, x0, :lo12:.LANCHOR1
	mov	w0, -1
	strb	w6, [x19,6]
	strh	w0, [x20,3068]
	mov	x0, x19
	bl	ftl_sb_update_avl_pages
.L2156:
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
	adrp	x0, .LC122
	mov	w1, w19
	add	x0, x0, :lo12:.LC122
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w3, [x2,948]
	cmp	w3, w19
	beq	.L2169
	ldrh	w1, [x2,950]
	cmp	w1, w19
	beq	.L2169
	mov	w0, 65535
	cmp	w3, w0
	bne	.L2170
	strh	w19, [x2,948]
	b	.L2169
.L2170:
	cmp	w1, w0
	bne	.L2169
	strh	w19, [x2,950]
.L2169:
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
	adrp	x0, .LC122
	mov	w1, w19
	add	x0, x0, :lo12:.LC122
	bl	printk
	adrp	x2, .LANCHOR2
	add	x2, x2, :lo12:.LANCHOR2
	ldrh	w3, [x2,948]
	cmp	w3, w19
	beq	.L2172
	ldrh	w1, [x2,950]
	cmp	w1, w19
	beq	.L2172
	mov	w0, 65535
	cmp	w3, w0
	bne	.L2173
	strh	w19, [x2,948]
	b	.L2172
.L2173:
	cmp	w1, w0
	bne	.L2172
	strh	w19, [x2,950]
.L2172:
	mov	w0, 0
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
	ldrh	w1, [x22,958]
	adrp	x0, .LC123
	mov	w2, w20
	add	x0, x0, :lo12:.LC123
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,652]
	cbz	w0, .L2175
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,1168]
	ldrh	w1, [x2,x0]
	cmp	w1, 29
	bls	.L2175
	sub	w1, w1, #30
	strh	w1, [x2,x0]
.L2175:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 184
	ldrh	w3, [x0,958]
	mov	x0, 0
.L2176:
	cmp	w3, w0, uxth
	bls	.L2182
	add	x0, x0, 1
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L2176
	b	.L2177
.L2182:
	cmp	w3, 15
	bhi	.L2177
	adrp	x1, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR4
	add	w0, w3, 1
	add	x1, x1, 184
	strh	w0, [x19,958]
	strh	w20, [x1,w3,sxtw 1]
.L2177:
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
	ldrh	w1, [x0,958]
	cbz	w1, .L2184
	ldrh	w3, [x0,948]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2184
	ldrh	w2, [x0,962]
	cmp	w2, w1
	bcc	.L2185
	strh	wzr, [x0,962]
.L2185:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 184
	ldrh	w1, [x19,962]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,962]
	add	w0, w0, 1
	strh	w0, [x19,962]
.L2184:
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
	ldrh	w0, [x19,958]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	cbz	w0, .L2191
	adrp	x24, .LANCHOR4
	adrp	x22, .LC124
	add	x22, x22, :lo12:.LC124
	add	x25, x24, :lo12:.LANCHOR4
.L2199:
	ldrh	w0, [x19,748]
	cmp	w0, w21
	bls	.L2200
	add	x0, x19, 776
	mov	w1, w23
	mov	w20, 0
	add	x27, x24, :lo12:.LANCHOR4
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w26, w0
.L2192:
	ldrh	w0, [x19,958]
	cmp	w0, w20
	bls	.L2201
	add	x0, x27, 184
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w26
	bne	.L2193
	mov	w1, w26
	mov	x0, x22
	bl	printk
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,958]
	mov	w3, w20
.L2194:
	cmp	w3, w2
	bcs	.L2202
	add	x4, x25, 184
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2194
.L2202:
	sub	w2, w2, #1
	strh	w2, [x19,958]
.L2193:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2192
.L2201:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2199
.L2200:
	bl	FtlGcReFreshBadBlk
.L2191:
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
	add	x1, x20, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x2, [x1,1184]
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2204
	ldrh	w0, [x1,3920]
	cmp	w0, w19
	bne	.L2205
	mov	w0, -1
	strh	w0, [x1,3920]
	b	.L2206
.L2205:
	ldrh	w3, [x1,3608]
	mov	w0, w2
	cmp	w3, w19
	beq	.L2207
	ldrh	w3, [x1,3656]
	cmp	w3, w19
	beq	.L2207
	ldrh	w1, [x1,3704]
	cmp	w1, w19
	beq	.L2207
.L2206:
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
	b	.L2207
.L2204:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2207:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -48]!
	uxth	w1, w0
	mov	w0, 65535
	add	x29, sp, 0
	str	x19, [sp,16]
	cmp	w1, w0
	beq	.L2212
	adrp	x2, .LANCHOR2+1184
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+1184]
	ldrh	w19, [x2,x0]
	cbnz	w19, .L2213
	adrp	x0, .LC125
	mov	w2, w19
	add	x0, x0, :lo12:.LC125
	bl	printk
	b	.L2217
.L2213:
	sub	w19, w19, #1
	strh	w19, [x2,x0]
.L2212:
	adrp	x19, .LANCHOR4
	mov	w0, 65535
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w2, [x19,160]
	cmp	w2, w0
	bne	.L2215
	strh	w1, [x19,160]
.L2217:
	mov	w0, 0
	b	.L2214
.L2215:
	cmp	w2, w1
	str	x1, [x29,40]
	mov	w0, 0
	beq	.L2214
	mov	w0, w2
	bl	update_vpc_list
	cmp	w0, wzr
	ldr	x1, [x29,40]
	cset	w0, ne
	strh	w1, [x19,160]
.L2214:
	ldr	x19, [sp,16]
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
	beq	.L2376
	ldrh	w0, [x19,2]
	adrp	x20, .LANCHOR2
	str	w0, [x29,188]
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w28, [x19,6]
	ldr	w1, [x29,188]
	ldrh	w0, [x0,818]
	cmp	w0, w1
	bne	.L2221
	strh	wzr, [x19,4]
	b	.L2382
.L2221:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2222:
	cmp	w0, w2
	bne	.L2385
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2222
.L2385:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2224
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2225
	adrp	x0, .LANCHOR0
	mov	w23, w21
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,136]
	cbnz	w1, .L2299
	add	x0, x0, 140
	ldrh	w23, [x0,w21,sxtw 1]
	b	.L2299
.L2224:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2225
	mov	w23, w0
.L2299:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w25, 0
	mov	w8, 65535
	mov	w9, 56
	mov	w10, 4
	ldrh	w4, [x0,748]
	ldrh	w7, [x0,830]
	add	x4, x19, x4, lsl 1
	b	.L2227
.L2225:
	strh	wzr, [x19,2]
.L2382:
	strb	wzr, [x19,6]
	b	.L2376
.L2229:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2228
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
.L2228:
	add	x2, x2, 2
.L2227:
	cmp	x2, x4
	bne	.L2229
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2230
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2230:
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,184]
	mov	w1, w25
	mov	x26, 0
	mov	w24, 65535
	adrp	x27, .LANCHOR4
	ldr	x0, [x22,992]
	bl	FlashReadPages
	ldr	w0, [x22,876]
	adrp	x22, .LC126
	str	w0, [x29,176]
	add	x22, x22, :lo12:.LC126
	uxth	w0, w23
	str	w0, [x29,168]
.L2231:
	uxth	w2, w26
	cmp	w2, w25
	bcs	.L2238
	add	x6, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x26, x0
	ldr	x4, [x6,992]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2232
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2233
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
	cbz	w0, .L2233
	add	w4, w4, 1
	str	w4, [x6,876]
.L2233:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2234
.L2238:
	cmp	w2, w25
	bne	.L2377
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,992]
	ldr	w0, [x0,4]
	b	.L2378
.L2232:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w24, [x29,168]
	add	x0, x27, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,218]
.L2234:
	add	x26, x26, 1
	b	.L2231
.L2377:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,992]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2378:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2240
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,136]
	cbnz	w2, .L2240
	add	x0, x0, 140
	ldrh	w21, [x0,w21,sxtw 1]
.L2240:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,818]
	cmp	w0, w21
	bne	.L2241
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2241:
	uxth	w0, w28
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2242
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2383
.L2242:
	ldr	w0, [x29,176]
	adrp	x25, .LANCHOR4
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w24, w0
	bne	.L2243
	cbnz	w1, .L2244
.L2243:
	add	x0, x25, :lo12:.LANCHOR4
	uxth	w6, w23
	ldr	w1, [x0,172]
	cmn	w1, #1
	bne	.L2245
	str	w22, [x0,172]
.L2245:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w24, [x29,188]
	ldr	w3, [x0,172]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2246
	sub	w24, w6, #7
	uxth	w24, w24
.L2246:
	adrp	x7, .LANCHOR0
	mov	w28, -1
	add	x5, x7, :lo12:.LANCHOR0
	mov	w27, w28
	mov	w4, 65535
	add	x5, x5, 652
.L2247:
	cmp	w24, w6
	bhi	.L2260
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, 0
	mov	w10, 56
	ldrh	w2, [x0,748]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2261:
	cmp	x0, x2
	beq	.L2386
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2248
	add	x9, x20, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x8, w23, w10
	add	w23, w23, 1
	ldr	x9, [x9,992]
	uxth	w23, w23
	add	x8, x9, x8
	str	w1, [x8,4]
.L2248:
	add	x0, x0, 2
	b	.L2261
.L2386:
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
.L2250:
	cmp	w1, w23
	beq	.L2387
	ldr	w2, [x0,-16]
	cbnz	w2, .L2251
	ldr	x2, [x0]
	ldrh	w8, [x2]
	cmp	w8, w4
	beq	.L2252
	ldr	w8, [x2,4]
	cmn	w8, #1
	beq	.L2252
	add	x2, x25, :lo12:.LANCHOR4
	cmn	w28, #1
	ldr	w27, [x2,172]
	str	w8, [x2,172]
	bne	.L2252
	ldrh	w2, [x5,x10,lsl 1]
	cmp	w2, w4
	bne	.L2253
	cbz	w9, .L2252
.L2253:
	cmp	w27, w22
	csinv	w28, w27, wzr, ne
	b	.L2252
.L2251:
	add	x0, x25, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,218]
	ldrb	w1, [x19,8]
	cbnz	w1, .L2244
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 652
	ldrh	w2, [x1,w24,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2255
	cmn	w28, #1
	beq	.L2256
	str	w28, [x0,172]
	b	.L2244
.L2256:
	cmp	w3, w22
	beq	.L2257
	str	w3, [x0,172]
	b	.L2244
.L2257:
	ldr	w1, [x0,172]
	b	.L2384
.L2255:
	cmp	w27, w22
	beq	.L2258
	cmn	w27, #1
	beq	.L2244
	str	w27, [x0,172]
	b	.L2244
.L2258:
	ldr	w1, [x0,172]
	cmp	w1, w22
	beq	.L2244
.L2384:
	sub	w1, w1, #1
	b	.L2379
.L2252:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2250
.L2387:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2247
.L2260:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, -1
.L2379:
	str	w1, [x0,172]
.L2244:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, 1
	strh	w1, [x0,220]
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 3752
	bl	FtlMapBlkWriteDumpData
	ldr	w23, [x29,188]
	adrp	x0, .LC128
	add	x0, x0, :lo12:.LC128
	str	x0, [x29,128]
	adrp	x0, .LC127
	add	x0, x0, :lo12:.LC127
	str	x0, [x29,120]
.L2262:
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
.L2263:
	cmp	x1, x3
	beq	.L2388
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2264
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w23, w2, lsl 10
	ldr	x4, [x5,992]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2265
	cbz	w6, .L2265
	ldr	x2, [x5,992]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2265:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2264:
	add	x1, x1, 2
	b	.L2263
.L2388:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,992]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2267:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2389
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
	bcc	.L2268
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2303
	ldr	w2, [x29,188]
	cmp	w23, w2
	beq	.L2268
.L2303:
	cmp	w0, w26
	bne	.L2304
	cmp	w23, w21
	beq	.L2270
.L2304:
	ldr	w0, [x28,x24]
	cmn	w0, #1
	beq	.L2272
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	bne	.L2279
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2274
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,876]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2274
	add	w0, w22, 1
	str	w0, [x27,876]
.L2274:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	add	x0, x25, :lo12:.LANCHOR4
	str	x0, [x29,136]
	ldr	w1, [x0,172]
	cmn	w1, #1
	beq	.L2275
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2275
	ldr	w1, [x29,196]
	cmn	w1, #1
	beq	.L2276
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
	bne	.L2277
	b	.L2278
.L2276:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2279
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
.L2279:
	ldrh	w0, [x19]
	b	.L2381
.L2277:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2278
	ldr	x0, [x29,136]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	ldr	w0, [x0,172]
	str	x3, [x29,104]
	str	x4, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,112]
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2278
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2284
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2278
	cmn	w0, #1
	beq	.L2282
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,992]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2283
.L2282:
	str	w0, [x2,x24]
.L2283:
	adrp	x0, .LANCHOR2+992
	ldr	x0, [x0,#:lo12:.LANCHOR2+992]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2284
	ldr	x0, [x29,168]
	ldr	w24, [x0,4]
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, w24
	ldr	w0, [x0,172]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2284
	ldr	w0, [x29,144]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2278
.L2284:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2278:
	mov	w0, -1
	str	w0, [x29,196]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2286
.L2275:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2286
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w24, [x29,200]
	cmn	w24, #1
	beq	.L2286
	ldr	w0, [x29,196]
	cmp	w24, w0
	beq	.L2286
	lsr	x0, x24, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w2, [x1,3608]
	cmp	w2, w0
	beq	.L2289
	ldrh	w2, [x1,3656]
	cmp	w2, w0
	beq	.L2289
	ldrh	w1, [x1,3704]
	cmp	w1, w0
	bne	.L2286
.L2289:
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
	beq	.L2286
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2286
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2286:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2268
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,1184]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2290
.L2381:
	bl	decrement_vpc_count
	b	.L2268
.L2290:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2268
.L2272:
	add	x24, x25, :lo12:.LANCHOR4
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x24,218]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x24,224]
	cmp	w0, 31
	bhi	.L2291
	add	x1, x24, 232
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x24,224]
.L2291:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x0,172]
	cmn	w1, #1
	beq	.L2380
	cmp	w1, w22
	bls	.L2268
.L2380:
	str	w22, [x0,172]
.L2268:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2267
.L2389:
	ldrb	w0, [x19,8]
	add	w23, w23, 1
	cmp	w0, 1
	uxth	w23, w23
	bne	.L2294
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2294
	cmp	w21, w23
	bne	.L2294
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,820]
	cmp	w0, w23
	beq	.L2270
.L2294:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,818]
	cmp	w23, w1
	bne	.L2262
	ldrh	w2, [x0,748]
	mov	w3, 65535
	strh	w23, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2296:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2376
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2296
	strb	w1, [x19,6]
	b	.L2376
.L2270:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2383:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2376:
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
	cbz	w1, .L2390
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2390
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	adrp	x23, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	add	x22, x21, 652
	ldrh	w0, [x19,x0,lsl 1]
.L2394:
	cmp	w0, w20
	bne	.L2401
.L2396:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,748]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2395
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2395:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2394
.L2401:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2397
	ldrb	w0, [x21,136]
	cbnz	w0, .L2397
	ldrh	w0, [x19,2]
	ldrh	w0, [x22,w0,sxtw 1]
	cmp	w0, w20
	bne	.L2397
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2396
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2390
.L2397:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2390
	cmp	w1, 1
	bne	.L2390
	adrp	x0, .LANCHOR2
	ldrh	w2, [x19,2]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,820]
	cmp	w2, w1
	bcc	.L2390
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
.L2390:
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
.L2403:
	cmp	w0, w22
	adrp	x21, .LANCHOR2
	bne	.L2418
.L2404:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,748]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2405
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2405:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2403
.L2418:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2406
	ldrb	w1, [x20,136]
	cbnz	w1, .L2406
	ldrh	w1, [x19,2]
	ldrh	w1, [x24,w1,sxtw 1]
	cmp	w1, w22
	bne	.L2406
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2404
.L2406:
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
.L2407:
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,748]
.L2409:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2408
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2408:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2409
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2414
	ldrb	w0, [x20,136]
	cbnz	w0, .L2411
	ldrh	w0, [x19,2]
	ldrh	w0, [x25,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2411
	ldrh	w1, [x19,4]
	cbz	w1, .L2411
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2407
.L2411:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w0, [x1,136]
	cbz	w0, .L2414
	add	x2, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x2,820]
	cmp	w1, w0
	bcc	.L2414
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
.L2414:
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
	cbnz	w0, .L2419
	ldrh	w2, [x20,3612]
	cbz	w2, .L2421
	ldrb	w1, [x20,3616]
	cbnz	w1, .L2421
	ldrb	w1, [x20,3615]
	ldrh	w3, [x20,818]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2421
	ldrb	w23, [x20,3618]
	ldr	w22, [x20,1396]
	ldrh	w24, [x20,748]
	cbnz	w23, .L2419
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
	beq	.L2423
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w23
	bl	FlashReadPages
	b	.L2424
.L2423:
	ldr	x0, [x20,1048]
	mov	w1, 255
	ldrh	w2, [x20,828]
	bl	ftl_memset
.L2424:
	add	x25, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w24, w24, 2
	strh	w0, [x21]
	mov	w23, 0
	add	x20, x25, 3608
.L2425:
	cmp	w23, w24
	beq	.L2426
	ldrh	w0, [x20,4]
	cbz	w0, .L2426
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
	b	.L2425
.L2426:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,3618]
	b	.L2419
.L2421:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,3618]
.L2419:
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
.L2437:
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w0, [x1,858]
	cmp	w0, w19
	bls	.L2440
	ldr	x1, [x1,1264]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2438
	mov	w0, w19
	bl	flush_l2p_region
.L2438:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2437
.L2440:
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
	mov	x19, x0
	adrp	x0, .LANCHOR0+136
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	ldrb	w20, [x0,#:lo12:.LANCHOR0+136]
	cbz	w20, .L2455
	ldrb	w20, [x19,8]
	cmp	w20, 1
	bne	.L2450
	ldrh	w21, [x19,4]
	b	.L2442
.L2450:
	mov	w20, 0
.L2455:
	mov	w21, 12
.L2442:
	adrp	x23, .LANCHOR2
	mov	w22, -1
.L2443:
	sub	w21, w21, #1
	cmn	w21, #1
	beq	.L2445
	ldrh	w0, [x19,4]
	cbnz	w0, .L2444
.L2445:
	adrp	x0, .LANCHOR2
	ldrh	w1, [x19]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w4, [x19,4]
	lsl	x1, x1, 1
	ldr	x3, [x0,1184]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strb	wzr, [x19,6]
	ldrh	w0, [x0,818]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	ldp	x21, x22, [sp,32]
	ldp	x19, x20, [sp,16]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 128
	ret
.L2444:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2445
	add	x2, x23, :lo12:.LANCHOR2
	str	w22, [x29,96]
	mov	w3, 0
	ldr	x1, [x2,1112]
	ldr	x0, [x2,1048]
	str	x0, [x29,80]
	str	x1, [x29,88]
	str	w22, [x1,8]
	str	w22, [x1,12]
	ldrh	w0, [x19]
	strh	w0, [x1,2]
	strh	wzr, [x1]
	ldr	w0, [x2,876]
	str	w0, [x1,4]
	mov	w1, 1
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x2,876]
	add	x0, x29, 72
	mov	w2, w20
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2443
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	stp	x29, x30, [sp, -80]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC129
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 16
	add	x0, x0, :lo12:.LC129
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
.L2457:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w0, [x19,1396]
	cmp	w20, w0
	bcs	.L2472
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2458
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x19,1176]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2458:
	add	w20, w20, 1
	b	.L2457
.L2472:
	adrp	x22, .LC130
	mov	w20, 0
	mov	w23, 65535
	add	x22, x22, :lo12:.LC130
.L2460:
	ldrh	w0, [x19,756]
	cmp	w0, w20
	bls	.L2473
	ubfiz	x21, x20, 1, 16
	ldr	x0, [x19,1184]
	ldrh	w2, [x0,x21]
	ldr	x0, [x19,1176]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2463
	cmp	w2, w23
	beq	.L2463
	mov	x0, x22
	mov	w1, w20
	bl	printk
	ldrh	w0, [x19,3608]
	cmp	w0, w20
	beq	.L2463
	ldrh	w0, [x19,3704]
	cmp	w0, w20
	beq	.L2463
	ldrh	w0, [x19,3656]
	cmp	w0, w20
	beq	.L2463
	ldr	x0, [x19,1176]
	ldrh	w1, [x0,x21]
	ldr	x0, [x19,1184]
	strh	w1, [x0,x21]
	mov	w0, w20
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2463:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2460
.L2473:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
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
	cbnz	w1, .L2475
	mov	x21, x0
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2476
	ubfiz	x1, x22, 1, 16
	ldr	x0, [x2,1184]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2477
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L2476
.L2477:
	mov	w0, w22
	bl	INSERT_FREE_LIST
.L2476:
	add	x1, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x1, 3656
	cmp	x21, x0
	beq	.L2478
	ldrh	w2, [x1,768]
	cmp	w2, 1
	beq	.L2478
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2479
.L2478:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2480
.L2479:
	add	x0, x1, 3608
	cmp	x21, x0
	bne	.L2480
	cmp	w2, 3
	beq	.L2482
	ldr	w0, [x1,868]
	cmp	w0, 1
	bne	.L2483
.L2482:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x1,3616]
.L2483:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,652]
	cbz	w0, .L2480
	ldr	w0, [x1,920]
	cmp	w0, 29
	bhi	.L2480
	mov	w0, 1
	strb	w0, [x1,3616]
.L2480:
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x20, x1
	ldrh	w2, [x0,160]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2485
	cmp	w22, w2
	bne	.L2486
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x1, x2, 1, 16
	ldr	x0, [x19,1184]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2487
.L2486:
	mov	w0, w2
	bl	update_vpc_list
.L2487:
	add	x1, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x1,160]
.L2485:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2475:
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
	adrp	x1, .LANCHOR2
	add	x0, x1, :lo12:.LANCHOR2
	add	x29, sp, 0
	add	x2, x0, 3968
	stp	x19, x20, [sp,16]
	str	x21, [sp,32]
	ldr	w3, [x2,80]
	cbz	w3, .L2501
	ldr	w3, [x2,84]
	ldr	w1, [x0,1396]
	cmp	w3, w1
	bcs	.L2502
	mov	w20, 2048
	mov	x19, x0
.L2507:
	ldr	w0, [x19,4052]
	ldr	w1, [x19,1396]
	cmp	w0, w1
	bcs	.L2504
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x19,4052]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x19,4052]
	cmn	w1, #1
	beq	.L2505
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
	bne	.L2504
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2504
.L2505:
	subs	w20, w20, #1
	bne	.L2507
.L2504:
	mov	w0, -1
	b	.L2509
.L2502:
	ldr	w0, [x0,900]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2515
.L2501:
	adrp	x2, .LANCHOR0+136
	ldr	w3, [x0,916]
	ldr	w0, [x0,652]
	ldrb	w4, [x2,#:lo12:.LANCHOR0+136]
	mov	w2, 4194304
	cbnz	w4, .L2510
	add	w0, w0, w3, lsr 10
	mov	w3, 33554432
	asr	w2, w3, w0
.L2510:
	add	x4, x1, :lo12:.LANCHOR2
	ldr	w3, [x4,900]
	ldr	w0, [x4,4044]
	add	w5, w3, 1048576
	cmp	w0, w5
	bhi	.L2511
	add	w0, w2, w0
	cmp	w0, w3
	bcc	.L2511
	ldrb	w2, [x4,3548]
	mov	w0, 0
	cbnz	w2, .L2509
.L2511:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w1, 1
	str	wzr, [x0,4052]
	str	w1, [x0,4048]
	str	w3, [x0,4044]
.L2515:
	mov	w0, 0
.L2509:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	stp	x29, x30, [sp, -144]!
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
	cbnz	w4, .L2661
	ldr	w4, [x3,924]
	cbnz	w4, .L2661
	ldrh	w4, [x3,3584]
	cmp	w4, 47
	bls	.L2661
	mov	w23, w0
	adrp	x0, .LANCHOR1+3068
	mov	w24, w1
	ldrh	w1, [x0,#:lo12:.LANCHOR1+3068]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2518
.L2521:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,950]
	cmp	w1, w2
	bne	.L2519
	b	.L2520
.L2518:
	ldrh	w1, [x3,3704]
	cmp	w1, w0
	beq	.L2521
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2521
	b	.L2661
.L2519:
	ldrh	w3, [x0,948]
	cmp	w3, w2
	bne	.L2520
	strh	w1, [x0,948]
	mov	w1, -1
	strh	w1, [x0,950]
.L2520:
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w23, 1
	ldr	w0, [x1,940]
	add	w0, w0, 1
	add	w0, w0, w23, lsl 7
	str	w0, [x1,940]
	bne	.L2532
	ldr	w1, [x1,652]
	cbnz	w1, .L2523
	adrp	x1, .LANCHOR0+136
	ldrb	w1, [x1,#:lo12:.LANCHOR0+136]
	cbz	w1, .L2532
.L2523:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,920]
	cmp	w1, 29
	bhi	.L2532
	adrp	x21, .LANCHOR4
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w1, [x1,360]
	add	w0, w0, w1
	str	w0, [x20,940]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x20,3920]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2532
	ldrh	w1, [x20,948]
	cmp	w1, w0
	bne	.L2532
	ldr	w0, [x20,940]
	cmp	w0, 1024
	bhi	.L2524
	ldrh	w0, [x20,3600]
	cmp	w0, 63
	bhi	.L2532
.L2524:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0,954]
	ldrh	w3, [x0,3600]
	add	w2, w2, 64
	strh	wzr, [x1,360]
	cmp	w3, w2
	bgt	.L2532
	str	wzr, [x0,940]
	ldr	w0, [x0,920]
	cbnz	w0, .L2525
	mov	w0, 6
	b	.L2683
.L2525:
	cmp	w0, 5
	bhi	.L2526
	mov	w0, 18
.L2683:
	strh	w0, [x1,360]
.L2526:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2531
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,956]
	cbz	w0, .L2528
	ldrh	w1, [x22,820]
	ubfiz	x25, x25, 1, 16
	ldrh	w3, [x22,748]
	ldr	x26, [x22,1184]
	mul	w1, w1, w3
	ldrh	w2, [x26,x25]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2531
	add	w1, w0, 1
	str	wzr, [x22,928]
	uxth	w1, w1
	strh	w1, [x22,956]
	str	x1, [x29,120]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w27
	ldr	x1, [x29,120]
	beq	.L2531
	ubfiz	x27, x20, 1, 16
	adrp	x0, .LC131
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC131
	mov	w2, w20
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x22,956]
	cmp	w0, 40
	bls	.L2529
	ldr	x0, [x22,1184]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2529
	strh	wzr, [x22,956]
.L2529:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, 6
	strh	w0, [x21,360]
	b	.L2533
.L2528:
	mov	w0, 1
	strh	w0, [x22,956]
.L2531:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2533
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x21,360]
.L2532:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	ldrh	w20, [x22,3920]
	cmp	w20, w0
	bne	.L2689
	ldrh	w0, [x22,3704]
	cmp	w0, w20
	beq	.L2690
.L2533:
	mov	w0, 65535
	cmp	w20, w0
	cset	w1, eq
	cbz	w1, .L2546
	cbnz	w23, .L2546
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 1
	ldrh	w1, [x0,3600]
	cmp	w1, 24
	bhi	.L2547
	cmp	w1, 16
	ldrh	w21, [x0,818]
	bls	.L2548
	lsr	w21, w21, 5
	b	.L2547
.L2548:
	cmp	w1, 12
	bls	.L2549
	lsr	w21, w21, 4
	b	.L2547
.L2549:
	lsr	w0, w21, 2
	cmp	w1, 9
	csel	w21, w0, w21, cs
.L2547:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,952]
	cmp	w2, w1
	bcs	.L2551
	ldrh	w1, [x0,3704]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2552
	ldrh	w2, [x0,948]
	cmp	w2, w1
	bne	.L2552
	adrp	x1, .LANCHOR4+360
	ldrh	w2, [x1,#:lo12:.LANCHOR4+360]
	cbnz	w2, .L2553
	ldr	w1, [x0,1396]
	ldr	w3, [x0,3892]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2554
.L2553:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3916]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,952]
	b	.L2555
.L2554:
	mov	w1, 18
	strh	w1, [x0,952]
.L2555:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,928]
	b	.L2661
.L2552:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3916]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,952]
.L2551:
	cmp	w24, 2
	bhi	.L2616
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,652]
	cbz	w0, .L2616
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2616
.L2546:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w0, [x21,3704]
	cmp	w0, w2
	bne	.L2558
	cbz	w1, .L2558
	ldrh	w1, [x21,948]
	cmp	w1, w0
	bne	.L2558
	ldrh	w1, [x21,3920]
	cmp	w1, w0
	beq	.L2559
.L2564:
	mov	w20, 65535
	b	.L2558
.L2559:
	ldrh	w24, [x21,3600]
	adrp	x20, .LANCHOR4
	ldrh	w0, [x21,952]
	str	wzr, [x21,928]
	cmp	w0, w24
	bcs	.L2560
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,360]
	cbnz	w0, .L2561
	ldr	w0, [x21,1396]
	ldr	w1, [x21,3892]
	add	w0, w0, w0, lsl 1
	cmp	w1, w0, lsr 2
	bcs	.L2562
.L2561:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,3916]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x19,952]
	b	.L2563
.L2562:
	mov	w0, 18
	strh	w0, [x21,952]
.L2563:
	add	x20, x20, :lo12:.LANCHOR4
	bl	FtlReadRefresh
	ldrh	w2, [x20,360]
	b	.L2661
.L2560:
	add	x22, x20, :lo12:.LANCHOR4
	ldrh	w0, [x22,360]
	cbnz	w0, .L2564
	ldrh	w20, [x21,3916]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x21,952]
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
	ble	.L2565
	sub	w20, w20, #1
	cmp	w24, w20
	blt	.L2565
	bl	FtlReadRefresh
	b	.L2687
.L2565:
	cbnz	w1, .L2564
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,3600]
	add	w2, w2, 1
	b	.L2661
.L2558:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w21, 2
	ldr	w0, [x0,652]
	cmp	w0, wzr
	csinc	w21, w21, wzr, ne
	b	.L2557
.L2616:
	mov	w20, 65535
.L2557:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w1, [x0,3920]
	cmp	w1, w2
	bne	.L2567
	cmp	w20, w1
	beq	.L2568
	strh	w20, [x0,3920]
	b	.L2569
.L2568:
	ldrh	w1, [x0,948]
	cmp	w1, w20
	beq	.L2569
	ubfiz	x1, x1, 1, 16
	ldr	x2, [x0,1184]
	ldrh	w1, [x2,x1]
	cbnz	w1, .L2570
	mov	w1, -1
	strh	w1, [x0,948]
.L2570:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,948]
	strh	w1, [x0,3920]
	mov	w1, -1
	strh	w1, [x0,948]
.L2569:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	ldrh	w0, [x22,3920]
	strb	wzr, [x22,3928]
	cmp	w0, w1
	beq	.L2567
	bl	IsBlkInGcList
	cbz	w0, .L2572
	mov	w0, -1
	strh	w0, [x22,3920]
.L2572:
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2573
	add	x0, x19, :lo12:.LANCHOR2
	add	x22, x0, 3920
	ldrh	w0, [x0,3920]
	bl	ftl_get_blk_mode
	strb	w0, [x22,8]
.L2573:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w0, 65535
	add	x24, x22, 3920
	ldrh	w1, [x22,3920]
	cmp	w1, w0
	beq	.L2567
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x22,3922]
	adrp	x0, .LANCHOR4
	ldrh	w2, [x22,3920]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x1, [x22,1184]
	strb	wzr, [x22,3926]
	strh	wzr, [x0,362]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,364]
.L2567:
	add	x3, x19, :lo12:.LANCHOR2
	ldrh	w0, [x3,3920]
	ldrh	w1, [x3,3608]
	cmp	w1, w0
	beq	.L2574
	ldrh	w1, [x3,3656]
	cmp	w1, w0
	beq	.L2574
.L2575:
	mov	x28, x3
	mov	w24, 65535
	b	.L2576
.L2574:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,3920]
	b	.L2688
.L2597:
	ldrh	w0, [x28,3922]
	add	w21, w21, w0
	uxth	w21, w21
	strh	w21, [x28,3922]
	cmp	w21, w25
	bcs	.L2691
.L2599:
	ldrh	w0, [x28,3600]
	cmp	w0, 2
	bhi	.L2602
	ldrh	w21, [x28,818]
.L2576:
	ldrh	w0, [x28,3920]
	cmp	w0, w24
	bne	.L2577
	str	wzr, [x28,928]
	adrp	x25, .LANCHOR4
.L2578:
	ldrh	w22, [x28,956]
	mov	w0, w22
	bl	List_get_gc_head_node
	uxth	w26, w0
	cmp	w26, w24
	strh	w26, [x28,3920]
	bne	.L2579
	strh	wzr, [x28,956]
	mov	w2, 8
	b	.L2661
.L2579:
	mov	w0, w26
	add	w22, w22, 1
	bl	IsBlkInGcList
	cbz	w0, .L2580
	strh	w22, [x28,956]
	b	.L2578
.L2580:
	ldrh	w5, [x28,748]
	ubfiz	x1, x26, 1, 16
	ldrh	w0, [x28,818]
	uxth	w22, w22
	ldr	x2, [x28,1184]
	mov	w3, 2
	strh	w22, [x28,956]
	mul	w0, w0, w5
	ldrh	w4, [x2,x1]
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2582
	cmp	w4, 8
	bls	.L2583
	cmp	w22, 48
	bls	.L2583
	add	x4, x25, :lo12:.LANCHOR4
	ldrh	w4, [x4,176]
	cmp	w4, 35
	bhi	.L2583
.L2582:
	strh	wzr, [x28,956]
.L2583:
	ldrh	w1, [x2,x1]
	cmp	w1, w0
	blt	.L2584
	cmp	w20, w24
	bne	.L2584
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	wzr, [x19,956]
	strh	w0, [x19,3920]
.L2688:
	adrp	x0, .LANCHOR4+360
	ldrh	w2, [x0,#:lo12:.LANCHOR4+360]
	b	.L2661
.L2584:
	cbnz	w1, .L2585
	mov	w0, -1
	bl	decrement_vpc_count
	ldrh	w0, [x28,956]
	add	w0, w0, 1
	strh	w0, [x28,956]
	b	.L2578
.L2585:
	adrp	x0, .LANCHOR0
	strb	wzr, [x28,3928]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2586
	mov	w0, w26
	bl	ftl_get_blk_mode
	strb	w0, [x28,3928]
.L2586:
	add	x22, x28, 3920
	mov	x0, x22
	bl	make_superblock
	adrp	x1, .LANCHOR4
	ldrh	w2, [x28,3920]
	add	x1, x1, :lo12:.LANCHOR4
	ldr	x0, [x28,1184]
	strh	wzr, [x1,362]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,364]
	strh	wzr, [x28,3922]
	strb	wzr, [x28,3926]
.L2577:
	cmp	w23, 1
	bne	.L2587
	bl	FtlReadRefresh
.L2587:
	mov	w0, 1
	str	w0, [x28,924]
	adrp	x0, .LANCHOR0
	ldrh	w25, [x28,818]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2588
	ldrb	w0, [x28,3928]
	cmp	w0, 1
	bne	.L2588
	ldrh	w25, [x28,820]
.L2588:
	ldrh	w0, [x28,3922]
	add	w1, w0, w21
	cmp	w1, w25
	ble	.L2589
	sub	w21, w25, w0
	uxth	w21, w21
.L2589:
	mov	w26, 0
.L2590:
	cmp	w21, w26, uxth
	bls	.L2597
	add	x1, x28, 3920
	ldrh	w4, [x1,2]
	mov	x0, 0
	ldrh	w6, [x28,748]
	add	w4, w4, w26
	mov	w22, w0
.L2598:
	cmp	w6, w0, uxth
	bls	.L2692
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,16]
	cmp	w2, w24
	beq	.L2591
	mov	w3, 56
	ldr	x7, [x28,1024]
	orr	w2, w4, w2, lsl 10
	umull	x5, w22, w3
	add	w22, w22, 1
	add	x5, x7, x5
	uxth	w22, w22
	str	w2, [x5,4]
.L2591:
	add	x0, x0, 1
	b	.L2598
.L2692:
	ldr	x0, [x28,1024]
	mov	w1, w22
	ldrb	w2, [x28,3928]
	mov	x27, 0
	bl	FlashReadPages
.L2593:
	cmp	w22, w27, uxth
	bls	.L2693
	mov	x0, 56
	mul	x5, x27, x0
	ldr	x0, [x28,1024]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L2618
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2618
	ldr	w0, [x4,8]
	add	x1, x29, 140
	mov	w2, 0
	str	x5, [x29,112]
	str	x4, [x29,120]
	bl	log2phys
	ldr	x6, [x28,1024]
	ldr	x5, [x29,112]
	ldr	w0, [x29,140]
	add	x6, x6, x5
	ldr	x4, [x29,120]
	and	w0, w0, 2147483647
	ldr	w1, [x6,4]
	cmp	w0, w1
	bne	.L2618
	adrp	x0, .LANCHOR4
	ldr	x1, [x28,1000]
	add	x2, x0, :lo12:.LANCHOR4
	str	x2, [x29,112]
	str	x5, [x29,96]
	str	x4, [x29,104]
	ldrh	w0, [x2,362]
	add	w0, w0, 1
	strh	w0, [x2,362]
	ldr	w0, [x2,144]
	mov	x2, 56
	madd	x1, x0, x2, x1
	ldr	w0, [x6,24]
	str	x1, [x29,120]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,120]
	mov	x3, 56
	ldr	x2, [x29,112]
	ldr	x5, [x29,96]
	str	w0, [x1,4]
	ldr	w0, [x2,144]
	ldr	x1, [x28,1000]
	ldr	x4, [x29,104]
	str	x2, [x29,120]
	madd	x0, x0, x3, x1
	ldr	x1, [x28,1024]
	add	x1, x1, x5
	ldr	x6, [x1,8]
	str	x6, [x0,8]
	add	x6, x28, 3704
	str	x6, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,140]
	str	w0, [x4,12]
	ldrh	w0, [x28,3704]
	strh	w0, [x4,2]
	ldr	w0, [x28,876]
	str	w0, [x4,4]
	ldr	w0, [x2,144]
	add	w0, w0, 1
	str	w0, [x2,144]
	ldr	x0, [x28,1024]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbnz	w0, .L2595
	ldr	x6, [x29,112]
	ldr	x2, [x29,120]
	ldrb	w0, [x6,7]
	ldr	w1, [x2,144]
	cmp	w1, w0
	beq	.L2595
	ldrh	w0, [x6,4]
	cbnz	w0, .L2618
.L2595:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2618
	adrp	x0, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	str	wzr, [x19,924]
	ldrh	w2, [x0,360]
	b	.L2661
.L2618:
	add	x27, x27, 1
	b	.L2593
.L2693:
	add	w26, w26, 1
	b	.L2590
.L2691:
	adrp	x0, .LANCHOR4
	add	x22, x0, :lo12:.LANCHOR4
	mov	x21, x0
	ldr	w1, [x22,144]
	cbz	w1, .L2600
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2600
	str	wzr, [x28,924]
.L2687:
	ldrh	w2, [x22,360]
	b	.L2661
.L2600:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w2, [x0,362]
	cbnz	w2, .L2601
	ldrh	w0, [x28,3920]
	ldr	x1, [x28,1184]
	lsl	x0, x0, 1
	ldrh	w4, [x1,x0]
	cbz	w4, .L2601
	strh	w2, [x1,x0]
	ldrh	w0, [x28,3920]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2601:
	mov	w0, -1
	strh	w0, [x28,3920]
	b	.L2599
.L2602:
	adrp	x1, .LANCHOR4+360
	str	wzr, [x28,924]
	ldrh	w2, [x1,#:lo12:.LANCHOR4+360]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2661
.L2690:
	ldrh	w25, [x22,948]
	cmp	w25, w20
	bne	.L2533
	ldrh	w0, [x22,3600]
	mov	w2, 1024
	ldr	w1, [x22,940]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2533
	adrp	x0, .LANCHOR4+360
	str	wzr, [x22,940]
	strh	wzr, [x0,#:lo12:.LANCHOR4+360]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L2535
	ldrh	w1, [x22,3600]
	ldrh	w0, [x22,954]
	cmp	w1, w0
	bcs	.L2536
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2545
	ldr	w2, [x22,864]
	uxtw	x1, w0
	cbnz	w2, .L2538
	ldrh	w0, [x22,768]
	cmp	w0, 3
	beq	.L2538
	ldr	w0, [x22,868]
	cbnz	w0, .L2538
	ldr	w0, [x22,652]
	cbnz	w0, .L2538
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2539
.L2538:
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
	bgt	.L2541
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,1396]
	ldr	w2, [x0,3892]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bhi	.L2685
	mov	w1, 160
	b	.L2684
.L2541:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2686
.L2539:
	ldr	x2, [x22,1184]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2544
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
.L2685:
	mov	w1, 128
.L2684:
	strh	w1, [x0,954]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2545
	b	.L2535
.L2544:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2686:
	strh	w1, [x0,954]
	b	.L2545
.L2536:
	mov	w0, 80
	strh	w0, [x22,954]
	b	.L2545
.L2535:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC132
	mov	w1, w20
	add	x0, x0, :lo12:.LC132
	ldr	x3, [x5,1184]
	ldr	x6, [x5,1168]
	ldrh	w2, [x5,3600]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,952]
	ldrh	w4, [x6,x4]
	bl	printk
.L2545:
	bl	FtlGcReFreshBadBlk
	b	.L2533
.L2689:
	mov	w20, w0
	b	.L2533
.L2661:
	mov	w0, w2
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	stp	x29, x30, [sp, -112]!
	adrp	x0, .LANCHOR4+368
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	ldr	x20, [x0,#:lo12:.LANCHOR4+368]
	add	x0, x19, :lo12:.LANCHOR2
	str	x27, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w22, [x0,1400]
	cbnz	w22, .L2696
	ldr	w1, [x0,968]
	cbz	w1, .L2696
	adrp	x0, .LANCHOR0+136
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	cbz	w0, .L2698
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2698:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w3, [x20,9]
	adrp	x24, .LC133
	mov	w2, w22
	mov	w23, 0
	mov	w26, 56
	ldr	x0, [x0,1032]
	add	x24, x24, :lo12:.LC133
	bl	FlashProgPages
.L2699:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,968]
	cmp	w23, w1
	bcs	.L2716
	umull	x21, w23, w26
	ldr	x1, [x0,1032]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L2719
	ldr	w1, [x0,4]
	cbz	w22, .L2733
	orr	w1, w1, -2147483648
.L2733:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,108]
	add	x25, x19, :lo12:.LANCHOR2
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x25,1032]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2703
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x25,1184]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2704
	mov	x0, x24
	bl	printk
.L2704:
	mov	w0, w21
	bl	decrement_vpc_count
.L2703:
	add	w23, w23, 1
	b	.L2699
.L2735:
	mov	w20, 16386
.L2715:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,958]
	cbz	w0, .L2716
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L2715
.L2716:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,968]
	b	.L2696
.L2719:
	adrp	x26, .LC133
	mov	w24, 0
	mov	w27, 56
	add	x26, x26, :lo12:.LC133
.L2700:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,968]
	cmp	w24, w1
	bcs	.L2735
	umull	x21, w24, w27
	ldr	x0, [x0,1032]
	str	w25, [x0,x21]
.L2706:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	x0, [x23,1032]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2736
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2707
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
.L2707:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2708
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2708:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	w0, [x23,4064]
	add	w0, w0, 1
	str	w0, [x23,4064]
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
	ldr	w0, [x23,1400]
	cbz	w0, .L2706
	b	.L2696
.L2736:
	cbz	w22, .L2734
	orr	w0, w0, -2147483648
.L2734:
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
	beq	.L2712
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x23,1184]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2713
	mov	x0, x26
	bl	printk
.L2713:
	mov	w0, w21
	bl	decrement_vpc_count
.L2712:
	add	w24, w24, 1
	b	.L2700
.L2696:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	cbnz	w0, .L2738
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2738:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3064
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+3064]
	cmp	w0, 1
	bne	.L2740
	bl	FtlSysFlush
.L2740:
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
	bhi	.L2744
	cmp	w19, 31
	bls	.L2758
	adrp	x22, .LANCHOR2
	add	x21, x22, :lo12:.LANCHOR2
	ldr	w0, [x21,1400]
	cbnz	w0, .L2758
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,824]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L2747
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L2747:
	mov	w0, -1
	adrp	x23, .LANCHOR4
	str	w0, [x29,76]
.L2748:
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w0, [x20,824]
	cmp	w19, w0
	bcc	.L2759
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L2749
	add	x1, x23, :lo12:.LANCHOR4
	mov	w2, 1
	ldr	w0, [x1,376]
	add	w0, w0, 1
	str	w0, [x1,376]
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
.L2749:
	add	x0, x22, :lo12:.LANCHOR2
	add	w21, w21, 1
	ldrh	w0, [x0,824]
	sub	w19, w19, w0
	b	.L2748
.L2759:
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,376]
	cmp	w1, 32
	bls	.L2758
	str	wzr, [x0,376]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2758:
	mov	w0, 0
.L2744:
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
	cbnz	w3, .L2795
	ldrh	w20, [x2,3704]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L2763
.L2772:
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR4
	add	x22, x21, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x23, x20, 3704
	ldrh	w1, [x20,3704]
	str	wzr, [x22,168]
	cmp	w1, w0
	beq	.L2795
	bl	FtlCacheWriteBack
	mov	w26, 12
	ldrb	w0, [x23,7]
	ldrh	w3, [x20,818]
	ldr	x1, [x20,1184]
	ldrh	w2, [x20,3704]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldrh	w1, [x22,178]
	mov	w22, 0
	ldr	w0, [x20,880]
	add	w0, w1, w0
	str	w0, [x20,880]
	b	.L2773
.L2763:
	cbz	w0, .L2766
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3068]
	cmp	w5, w4
	beq	.L2767
.L2768:
	mov	w1, 2
	b	.L2766
.L2767:
	strh	w3, [x0,3068]
	ldrh	w0, [x2,3600]
	cmp	w0, 17
	bhi	.L2768
.L2766:
	add	x21, x19, :lo12:.LANCHOR2
	add	x0, x21, 3704
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L2769
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x21,1168]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L2770
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L2770:
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	x20, x0
	ldr	w1, [x1,168]
	cbnz	w1, .L2771
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w0, [x19,4064]
	add	w0, w0, 1
	str	w0, [x19,4064]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2771:
	add	x0, x20, :lo12:.LANCHOR4
	str	wzr, [x0,168]
	mov	w0, 1
	b	.L2762
.L2769:
	adrp	x0, .LANCHOR1+3068
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3068]
	mov	w0, 1
	cmp	w2, w1
	bne	.L2762
	b	.L2772
.L2776:
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
	bne	.L2774
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
.L2794:
	bl	decrement_vpc_count
.L2775:
	add	w22, w22, 1
	uxth	w22, w22
.L2773:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,178]
	cmp	w0, w22
	bhi	.L2776
	b	.L2796
.L2774:
	ldr	w1, [x23,4]
	cmp	w0, w1
	beq	.L2775
	ldrh	w0, [x20,3704]
	b	.L2794
.L2796:
	mov	w0, -1
	adrp	x20, .LANCHOR0
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2777
	add	x1, x19, :lo12:.LANCHOR2
	adrp	x0, .LC134
	add	x0, x0, :lo12:.LC134
	ldrh	w1, [x1,3704]
	bl	printk
.L2777:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3704]
	ldr	x1, [x1,1184]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L2778
	bl	INSERT_DATA_LIST
	b	.L2779
.L2778:
	bl	INSERT_FREE_LIST
.L2779:
	add	x22, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	wzr, [x21,178]
	strh	w0, [x22,3704]
	strh	wzr, [x21,176]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	w0, [x22,652]
	cbz	w0, .L2780
	ldr	w0, [x22,920]
	cmp	w0, 29
	bhi	.L2780
	ldrh	w0, [x22,3916]
	ldrh	w1, [x22,3600]
	cmp	w1, w0
	bcs	.L2781
	lsl	w0, w0, 1
	strh	w0, [x22,952]
.L2781:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	strh	w0, [x19,3920]
.L2795:
	mov	w0, 0
	b	.L2762
.L2780:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,3916]
	ldrh	w0, [x19,3600]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L2795
	add	x20, x20, :lo12:.LANCHOR0
	mov	w0, -1
	strh	w0, [x19,3920]
	ldrb	w0, [x20,136]
	cbz	w0, .L2782
	sub	w1, w1, #2
	strh	w1, [x19,952]
	b	.L2795
.L2782:
	mov	w1, 20
	strh	w1, [x19,952]
.L2762:
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
	bcc	.L2797
	add	x0, x19, 3752
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, .LANCHOR4+168
	str	wzr, [x0,#:lo12:.LANCHOR4+168]
.L2797:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4+224
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+224]
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
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	adrp	x0, .LANCHOR0+136
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+136]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	cbz	w0, .L2801
	add	x0, x21, :lo12:.LANCHOR4
	ldr	w0, [x0,144]
	tbz	x0, 0, .L2801
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3708]
	cbz	w0, .L2801
.L2806:
	mov	w0, 0
	b	.L2802
.L2801:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x21, :lo12:.LANCHOR4
	mov	w2, 0
	mov	w20, 0
	mov	w3, w2
	mov	w23, 56
	ldr	x0, [x0,1000]
	ldr	w1, [x1,144]
	bl	FlashProgPages
.L2803:
	add	x22, x21, :lo12:.LANCHOR4
	ldr	w1, [x22,144]
	cmp	w20, w1
	bcs	.L2817
	add	x0, x19, :lo12:.LANCHOR2
	umull	x1, w20, w23
	ldr	x2, [x0,1000]
	add	x3, x2, x1
	ldr	w2, [x2,x1]
	cmn	w2, #1
	bne	.L2804
	ldrh	w4, [x0,3704]
	ldr	x3, [x0,1184]
	strh	wzr, [x3,x4,lsl 1]
	strh	w2, [x0,3704]
	ldr	w2, [x0,4064]
	add	w2, w2, 1
	str	w2, [x0,4064]
	ldr	x0, [x0,1000]
	add	x1, x0, x1
	ldr	w0, [x1,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2816
.L2804:
	ldr	x2, [x3,16]
	add	w20, w20, 1
	ldr	w1, [x3,4]
	uxth	w20, w20
	ldr	w0, [x2,12]
	ldr	w2, [x2,8]
	bl	FtlGcUpdatePage
	b	.L2803
.L2817:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x19,1000]
	bl	FtlGcBufFree
	str	wzr, [x22,144]
	ldrh	w0, [x19,3708]
	cbnz	w0, .L2806
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L2816:
	mov	w0, 1
.L2802:
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
	beq	.L2819
	ldrh	w0, [x1,4]
	cbnz	w0, .L2820
.L2819:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3704
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	strh	wzr, [x0,176]
	strh	wzr, [x0,178]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2820:
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
	stp	x29, x30, [sp, -176]!
	cmp	w0, 16
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	w20, w1
	mov	w28, w2
	mov	x25, x3
	bne	.L2822
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L2823
.L2822:
	add	w0, w1, w2
	str	w0, [x29,156]
	adrp	x0, .LANCHOR0+4048
	ldr	w2, [x29,156]
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L2823
	adrp	x19, .LANCHOR2
	sub	w24, w2, #1
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,824]
	udiv	w27, w20, w0
	udiv	w24, w24, w0
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	sub	w23, w24, w27
	add	w23, w23, 1
	ldr	w2, [x0,132]
	add	w2, w28, w2
	str	w2, [x0,132]
	ldr	w0, [x1,900]
	add	w0, w23, w0
	str	w0, [x1,900]
	mov	w0, w27
	mov	w1, w24
	bl	FtlCacheMetchLpa
	cbz	w0, .L2824
	bl	FtlCacheWriteBack
.L2824:
	mov	w26, 0
	mov	w21, w27
	str	w26, [x29,152]
	mov	w22, w26
	str	w26, [x29,140]
.L2825:
	cbz	w23, .L2866
	mov	w0, w21
	add	x1, x29, 172
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,172]
	cmn	w3, #1
	bne	.L2864
	mov	w3, 0
.L2826:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,824]
	cmp	w3, w0
	bcs	.L2830
	madd	w0, w21, w0, w3
	cmp	w0, w20
	bcc	.L2828
	ldr	w1, [x29,156]
	cmp	w0, w1
	bcs	.L2828
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x25, x0
	str	x3, [x29,144]
	bl	ftl_memset
	ldr	x3, [x29,144]
.L2828:
	add	w3, w3, 1
	b	.L2826
.L2864:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w21, w27
	umull	x1, w22, w0
	ldr	x0, [x2,992]
	add	x0, x0, x1
	str	w3, [x0,4]
	ldrh	w0, [x2,824]
	bne	.L2831
	ldr	x3, [x2,992]
	ldr	x2, [x2,1088]
	add	x3, x3, x1
	str	x2, [x3,8]
	udiv	w2, w20, w0
	msub	w2, w2, w0, w20
	str	w2, [x29,112]
	sub	w2, w0, w2
	cmp	w2, w28
	csel	w2, w2, w28, ls
	str	w2, [x29,152]
	cmp	w2, w0
	bne	.L2832
	str	x25, [x3,8]
	b	.L2832
.L2831:
	cmp	w21, w24
	bne	.L2833
	ldr	x3, [x2,992]
	ldr	x2, [x2,1096]
	add	x3, x3, x1
	ldr	w4, [x29,156]
	str	x2, [x3,8]
	mul	w2, w21, w0
	sub	w26, w4, w2
	cmp	w26, w0
	bne	.L2832
	sub	w2, w2, w20
	ubfiz	x2, x2, 9, 23
	add	x2, x25, x2
	str	x2, [x3,8]
	b	.L2832
.L2833:
	ldr	x2, [x2,992]
	mul	w0, w21, w0
	add	x2, x2, x1
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
	str	x0, [x2,8]
.L2832:
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
.L2830:
	subs	w23, w23, #1
	add	w21, w21, 1
	beq	.L2834
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,748]
	cmp	w22, w0, lsl 3
	bne	.L2825
.L2834:
	cbz	w22, .L2825
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, 0
	ldr	x0, [x0,992]
	bl	FlashReadPages
	str	xzr, [x29,144]
	ldr	x0, [x29,112]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,128]
	ldr	w0, [x29,152]
	lsl	w0, w0, 9
	str	w0, [x29,136]
	lsl	w0, w26, 9
	str	w0, [x29,108]
.L2836:
	ldr	w0, [x29,144]
	cmp	w22, w0
	bls	.L2867
	ldr	x0, [x29,144]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	mul	x3, x0, x1
	ldr	x0, [x2,992]
	add	x0, x0, x3
	ldr	w1, [x0,24]
	cmp	w1, w27
	bne	.L2837
	ldr	x1, [x0,8]
	ldr	x0, [x2,1088]
	cmp	x1, x0
	bne	.L2838
	ldr	x2, [x29,128]
	mov	x0, x25
	str	x3, [x29,96]
	add	x1, x1, x2
	ldr	w2, [x29,136]
	b	.L2865
.L2837:
	cmp	w1, w24
	bne	.L2838
	ldr	x1, [x0,8]
	ldr	x0, [x2,1096]
	cmp	x1, x0
	bne	.L2838
	ldrh	w0, [x2,824]
	ldr	w2, [x29,108]
	str	x3, [x29,96]
	mul	w0, w24, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x25, x0
.L2865:
	bl	ftl_memcpy
	ldr	x3, [x29,96]
.L2838:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,992]
	add	x5, x1, x3
	ldr	w4, [x1,x3]
	cmn	w4, #1
	bne	.L2839
	ldr	w2, [x0,4040]
	str	w4, [x29,140]
	add	w2, w2, 1
	str	w2, [x0,4040]
.L2839:
	ldr	w0, [x1,x3]
	cmp	w0, 256
	bne	.L2840
	ldr	w0, [x5,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2840:
	ldr	x0, [x29,144]
	add	x0, x0, 1
	str	x0, [x29,144]
	b	.L2836
.L2867:
	mov	w22, 0
	b	.L2825
.L2866:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,958]
	cbz	w0, .L2843
	mov	w0, w23
	mov	w1, 1
	bl	ftl_do_gc
.L2843:
	ldr	w0, [x29,140]
.L2823:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	ftl_read, .-ftl_read
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
	cbnz	w3, .L2869
	cmp	w4, 16
	bne	.L2870
	add	w0, w20, 256
	mov	w1, w21
	mov	x2, x22
	bl	FtlVendorPartWrite
	b	.L2869
.L2870:
	adrp	x0, .LANCHOR0+4048
	add	w24, w20, w21
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w24, w1
	bhi	.L2869
	adrp	x3, .LANCHOR4
	ldrh	w1, [x2,824]
	add	x26, x3, :lo12:.LANCHOR4
	sub	w24, w24, #1
	mov	w0, 2048
	str	x3, [x29,144]
	udiv	w23, w20, w1
	cmp	w21, w1, lsl 1
	str	w0, [x26,380]
	add	x19, x2, 3608
	ldr	w3, [x2,968]
	udiv	w0, w24, w1
	str	w0, [x29,200]
	sub	w28, w0, w23
	add	w25, w28, 1
	ldr	w0, [x2,884]
	add	w0, w25, w0
	str	w0, [x2,884]
	ldr	w0, [x26,128]
	add	w0, w21, w0
	str	w0, [x26,128]
	cset	w0, cs
	str	w0, [x29,204]
	cmp	w21, 8
	add	x0, x2, 3656
	csel	x19, x19, x0, hi
	cbz	w3, .L2873
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x2,1032]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w23, w0
	bne	.L2874
	ldr	w0, [x2,892]
	add	w0, w0, 1
	str	w0, [x2,892]
	ldr	w0, [x26,384]
	add	w0, w0, 1
	str	w0, [x26,384]
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
	cbnz	w28, .L2875
	ldr	w0, [x26,384]
	cmp	w0, 2
	bgt	.L2875
.L2908:
	mov	w0, 0
	b	.L2869
.L2875:
	add	x22, x22, x25
	sub	w21, w21, w19
	add	w20, w20, w19
	add	w23, w23, 1
	mov	w25, w28
.L2874:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	x19, [x0,368]
	str	wzr, [x0,384]
.L2873:
	ldr	w1, [x29,200]
	mov	w0, w23
	bl	FtlCacheMetchLpa
	cbz	w0, .L2876
	bl	FtlCacheWriteBack
.L2876:
	ldr	x0, [x29,144]
	str	w23, [x29,192]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,368]
	ldr	x0, [x29,152]
	add	x24, x0, :lo12:.LANCHOR2
	add	x0, x24, 3608
	str	x0, [x29,176]
	adrp	x0, .LC135
	add	x0, x0, :lo12:.LC135
	str	x0, [x29,104]
.L2877:
	cbz	w25, .L2938
	ldrh	w0, [x19,4]
	cbnz	w0, .L2878
	ldr	x1, [x29,176]
	adrp	x26, .LANCHOR1
	cmp	x19, x1
	bne	.L2879
	add	x0, x24, 3656
	ldrh	w19, [x0,4]
	cbnz	w19, .L2880
	bl	allocate_new_data_superblock
	add	x0, x26, :lo12:.LANCHOR1
	str	w19, [x0,3072]
.L2880:
	ldr	x0, [x29,176]
	add	x26, x26, :lo12:.LANCHOR1
	add	x19, x24, 3656
	bl	allocate_new_data_superblock
	ldr	w0, [x26,3072]
	cbnz	w0, .L2881
.L2882:
	ldr	x19, [x29,176]
	b	.L2881
.L2879:
	add	x26, x26, :lo12:.LANCHOR1
	str	w0, [x26,3072]
	ldr	x0, [x29,176]
	ldrh	w0, [x0,4]
	cbnz	w0, .L2882
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2881:
	ldrh	w0, [x19,4]
	cbnz	w0, .L2883
	mov	x0, x19
	bl	allocate_new_data_superblock
.L2883:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR4
	str	x19, [x0,368]
.L2878:
	ldr	w0, [x24,964]
	mov	x28, 0
	ldr	w2, [x24,968]
	ldrh	w1, [x19,4]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w25
	csel	w0, w0, w25, ls
	str	w0, [x29,140]
	ldr	w0, [x29,204]
	and	w0, w0, 1
	str	w0, [x29,116]
	add	w0, w21, w20
	str	w0, [x29,136]
.L2884:
	ldr	w1, [x29,192]
	mov	w0, w28
	add	w27, w1, w28
	ldr	w1, [x29,140]
	cmp	w28, w1
	bcs	.L2939
	ldrh	w1, [x19,4]
	cbz	w1, .L2915
	ldr	w1, [x29,200]
	cmp	w27, w1
	cset	w7, eq
	cbz	w0, .L2886
	ldr	w1, [x29,116]
	tst	w7, w1
	beq	.L2886
	ldrh	w1, [x24,824]
	ldr	w2, [x29,136]
	msub	w2, w27, w1, w2
	cmp	w2, w1
	bne	.L2915
.L2886:
	add	x1, x29, 212
	mov	w2, 0
	mov	w0, w27
	str	x7, [x29,160]
	bl	log2phys
	mov	x0, x19
	bl	get_new_active_ppa
	ldr	w6, [x24,968]
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
	cbnz	w0, .L2918
	ldr	x7, [x29,160]
	ldr	x5, [x29,184]
	cbz	w7, .L2887
	ldr	w0, [x29,136]
	ldrh	w2, [x24,824]
	msub	w2, w27, w2, w0
	uxth	w0, w2
	str	w0, [x29,184]
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	b	.L2890
.L2918:
	ldrh	w2, [x24,824]
	udiv	w0, w20, w2
	msub	w0, w0, w2, w20
	str	w0, [x29,160]
	sub	w2, w2, w0
	cmp	w2, w21
	csel	w0, w2, w21, ls
	str	w0, [x29,184]
.L2890:
	ldrh	w0, [x24,824]
	ldr	w1, [x29,184]
	cmp	w1, w0
	bne	.L2891
	ldr	w0, [x29,196]
	mov	x1, x22
	cbnz	w0, .L2892
	ldr	w0, [x29,184]
	mul	w1, w0, w27
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	add	x1, x22, x1
.L2892:
	ldr	w2, [x29,204]
	mov	w0, 56
	cbz	w2, .L2893
	ldr	w2, [x24,968]
	umull	x0, w2, w0
	ldr	x2, [x24,1032]
	add	x0, x2, x0
	b	.L2935
.L2893:
	ldr	w2, [x24,968]
	umull	x0, w2, w0
	ldr	x2, [x24,1032]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L2936
.L2891:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L2895
	str	w0, [x29,220]
	mov	w1, 56
	ldr	w0, [x24,968]
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
	bne	.L2896
	ldr	w0, [x24,4040]
	add	w0, w0, 1
	str	w0, [x24,4040]
	b	.L2898
.L2896:
	ldr	w0, [x26,8]
	cmp	w0, w27
	beq	.L2898
	ldr	w0, [x24,4040]
	mov	w2, w27
	add	w0, w0, 1
	str	w0, [x24,4040]
	ldr	x0, [x29,104]
	ldr	w1, [x26,8]
	bl	printk
	b	.L2898
.L2895:
	ldr	w0, [x24,968]
	mov	w1, 56
	ldrh	w2, [x24,828]
	umull	x0, w0, w1
	ldr	x1, [x24,1032]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L2898:
	ldr	w1, [x29,184]
	mov	w0, 56
	lsl	w2, w1, 9
	ldr	w1, [x29,196]
	cbz	w1, .L2899
	ldr	w1, [x24,968]
	umull	x0, w1, w0
	ldr	x1, [x24,1032]
	add	x1, x1, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x1,8]
	add	x0, x1, x0
	mov	x1, x22
	b	.L2937
.L2899:
	ldr	w1, [x24,968]
	umull	x0, w1, w0
	ldr	x1, [x24,1032]
	add	x0, x1, x0
	ldrh	w1, [x24,824]
	mul	w1, w27, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	add	x1, x22, x1
	b	.L2937
.L2887:
	ldr	w0, [x29,204]
	ldrh	w1, [x24,824]
	ldr	w2, [x24,968]
	cbz	w0, .L2900
	mul	w1, w27, w1
	umull	x0, w2, w5
	sub	w1, w1, w20
	ldr	x2, [x24,1032]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x22, x1
.L2935:
	str	x1, [x0,8]
	b	.L2894
.L2900:
	umull	x0, w2, w5
	ldr	x2, [x24,1032]
	mul	w1, w27, w1
	add	x0, x2, x0
	sub	w1, w1, w20
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x22, x1
.L2936:
	ldrh	w2, [x24,828]
.L2937:
	bl	ftl_memcpy
.L2894:
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
	ldr	w0, [x24,968]
	add	w0, w0, 1
	str	w0, [x24,968]
	b	.L2884
.L2939:
	str	w27, [x29,192]
	mov	x0, x1
	b	.L2885
.L2915:
	str	w27, [x29,192]
.L2885:
	sub	w25, w25, w0
	ldr	w0, [x29,204]
	cbnz	w0, .L2904
	ldr	w1, [x24,968]
	ldr	w0, [x24,964]
	cmp	w1, w0
	bcs	.L2904
	ldrh	w0, [x19,4]
	cbz	w0, .L2904
.L2906:
	str	wzr, [x29,204]
	b	.L2877
.L2904:
	bl	FtlCacheWriteBack
	str	wzr, [x24,968]
	cmp	w25, 3
	bls	.L2906
	b	.L2877
.L2938:
	ldr	w1, [x29,200]
	mov	w0, w25
	sub	w1, w1, w23
	bl	ftl_do_gc
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,3600]
	cmp	w1, 31
	bhi	.L2908
	mov	w1, 128
	mov	w19, 16
	strh	w1, [x0,954]
	strh	w1, [x0,952]
.L2909:
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldr	w0, [x0,1400]
	cbnz	w0, .L2908
	subs	w19, w19, #1
	bne	.L2909
	b	.L2908
.L2869:
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
	cbz	w0, .L2942
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x24, .LC136
	add	x26, x20, 3608
	add	x25, x20, 3656
	ldrh	w23, [x0,218]
	add	x0, x24, :lo12:.LC136
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
.L2944:
	subs	w20, w20, #1
	beq	.L2948
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,1184]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L2944
.L2948:
	add	x20, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC136
	mov	w1, w23
	ldr	x2, [x20,1184]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x20,1184]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L2946
	add	x0, x29, 80
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,748]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L2949:
	cmp	w5, w0, uxth
	bls	.L2956
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L2950
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
.L2950:
	add	x0, x0, 1
	b	.L2949
.L2956:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC137
	mov	w1, w23
	add	x0, x0, :lo12:.LC137
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
.L2946:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21,218]
.L2942:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 128
	ret
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	stp	x29, x30, [sp, -64]!
	mov	w1, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp,32]
	add	x0, x19, :lo12:.LANCHOR4
	add	x21, x20, :lo12:.LANCHOR2
	str	x23, [sp,48]
	strh	w1, [x0,218]
	strh	wzr, [x0,220]
	ldrh	w0, [x21,752]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x21,3896]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2958
.L2960:
	mov	w23, -1
	b	.L2959
.L2958:
	bl	FtlLoadSysInfo
	mov	w23, w0
	cbnz	w0, .L2960
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x21,858]
	mov	x1, 0
	ldr	x0, [x21,1264]
.L2961:
	cmp	w1, w2
	mov	w3, w1
	bge	.L2965
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L2961
.L2965:
	add	x0, x20, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,3548]
	add	w1, w1, 1
	strh	w1, [x0,3548]
	blt	.L2962
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w0, [x19,220]
	cbz	w0, .L2966
.L2962:
	add	x19, x20, :lo12:.LANCHOR2
	add	x22, x19, 3608
	add	x21, x19, 3656
	mov	x0, x22
	bl	FtlSuperblockPowerLostFix
	mov	x0, x21
	bl	FtlSuperblockPowerLostFix
	ldrh	w0, [x19,3608]
	ldr	x2, [x19,1184]
	lsl	x0, x0, 1
	ldrh	w3, [x19,3612]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,3614]
	ldrh	w0, [x19,818]
	strh	w0, [x19,3610]
	ldrh	w0, [x19,3656]
	ldr	x2, [x19,1184]
	lsl	x0, x0, 1
	strh	wzr, [x19,3612]
	ldrh	w3, [x19,3660]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,3662]
	ldrh	w0, [x19,818]
	strh	w0, [x19,3658]
	add	x0, x19, 3752
	strh	wzr, [x19,3660]
	bl	FtlMapBlkWriteDumpData
	add	x0, x19, 3824
	bl	FtlMapBlkWriteDumpData
	ldrh	w0, [x19,3550]
	add	w0, w0, 1
	strh	w0, [x19,3550]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2980
.L2966:
	bl	l2p_flush
.L2980:
	add	x19, x20, :lo12:.LANCHOR2
	bl	FtlVpcTblFlush
	mov	w0, 65535
	add	x22, x19, 3608
	ldrh	w1, [x19,3608]
	cmp	w1, w0
	bne	.L2968
.L2970:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x20,3548]
	and	w0, w0, 31
	cbnz	w0, .L2959
	bl	FtlVpcCheckAndModify
	b	.L2959
.L2968:
	ldrh	w0, [x22,4]
	cbnz	w0, .L2970
	ldrh	w0, [x19,3660]
	add	x21, x19, 3656
	cbnz	w0, .L2970
	bl	FtlVpcTblFlush
	ldrh	w0, [x19,3608]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x19,3656]
	bl	FtlGcRefreshOpenBlock
	mov	x0, x22
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
	add	x0, x19, 3824
	bl	FtlMapBlkWriteDumpData
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,136]
	cbnz	w1, .L2972
	ldrb	w0, [x0,24]
	cbz	w0, .L2970
.L2972:
	mov	w19, 4096
.L2975:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L2975
	b	.L2970
.L2959:
	mov	w0, w23
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w21, [x20,1400]
	cbnz	w21, .L2983
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
	cbz	w0, .L2984
	bl	FtlMakeBbt
.L2984:
	mov	w0, 0
.L2985:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,824]
	cmp	w0, w2, lsl 7
	bge	.L3012
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
	b	.L2985
.L3012:
	ldrh	w21, [x1,756]
	mov	w20, 0
.L2987:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,758]
	cmp	w0, w21
	bls	.L3013
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2987
.L3013:
	ldrh	w0, [x22,748]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L2989
.L2993:
	mov	w20, 0
	mov	w21, w20
	b	.L2990
.L2989:
	udiv	w20, w20, w0
	ldr	w0, [x22,852]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22,752]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x22,756]
.L2991:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,758]
	cmp	w0, w20
	bls	.L2993
	mov	w0, w20
	mov	w1, 1
	add	w20, w20, 1
	bl	FtlLowFormatEraseBlock
	uxth	w20, w20
	b	.L2991
.L2990:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w0, [x2,756]
	cmp	w0, w21
	bls	.L3014
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L2990
.L3014:
	ldrh	w3, [x2,748]
	ldr	w1, [x2,760]
	ldrh	w0, [x2,758]
	str	w0, [x2,944]
	udiv	w4, w1, w3
	str	w4, [x2,1396]
	ubfx	x5, x4, 5, 16
	add	w0, w5, 36
	strh	w0, [x2,3916]
	mov	w0, 24
	mul	w0, w3, w0
	cmp	w20, w0
	ble	.L2995
	sub	w1, w1, w20
	udiv	w1, w1, w3
	str	w1, [x2,1396]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x2,3916]
.L2995:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w0, [x2,652]
	cmp	w0, 1
	bne	.L2996
	ldrh	w1, [x2,3916]
	udiv	w0, w20, w3
	add	w0, w1, w0
	add	w0, w1, w0, asr 2
	strh	w0, [x2,3916]
.L2996:
	adrp	x21, .LANCHOR0
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,136]
	cbz	w0, .L2997
	add	x1, x19, :lo12:.LANCHOR2
	udiv	w0, w20, w3
	ldrh	w2, [x1,3916]
	add	w0, w2, w0
	add	w0, w2, w0, asr 2
	strh	w0, [x1,3916]
.L2997:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,810]
	cbz	w1, .L2999
	ldrh	w2, [x0,3916]
	add	w2, w2, w1, lsr 1
	strh	w2, [x0,3916]
	mul	w2, w1, w3
	cmp	w2, w20
	ble	.L2999
	add	w1, w1, 32
	str	w4, [x0,1396]
	add	w1, w5, w1
	strh	w1, [x0,3916]
.L2999:
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x20, x19, 3608
	mov	w22, -1
	ldr	w1, [x19,1396]
	ldrh	w0, [x19,3916]
	sub	w0, w1, w0
	ldrh	w1, [x19,818]
	mul	w0, w0, w3
	str	w0, [x19,3912]
	mul	w0, w0, w1
	ldrh	w1, [x19,824]
	str	w0, [x19,1396]
	mul	w0, w0, w1
	str	w0, [x21,4048]
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,758]
	mov	w1, 0
	ldr	x0, [x19,1184]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x19,3892]
	mov	w0, 1
	ldrh	w2, [x19,756]
	strb	w0, [x20,8]
	mov	w1, 255
	ldr	x0, [x21,16]
	lsr	w2, w2, 3
	strh	w22, [x19,3920]
	strh	wzr, [x19,3922]
	strb	wzr, [x19,3926]
	strb	wzr, [x19,3928]
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	strh	wzr, [x19,3608]
	bl	ftl_memset
.L3001:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L3002
	ldrh	w1, [x20]
	ldr	x0, [x19,1184]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3001
.L3002:
	ldr	w0, [x19,872]
	mov	w21, -1
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
.L3003:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L3004
	ldrh	w1, [x20]
	ldr	x0, [x19,1184]
	strh	w21, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3003
.L3004:
	ldr	w0, [x19,872]
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
	ldr	w0, [x19,3912]
	strh	w0, [x19,3902]
	ldr	w0, [x19,872]
	str	w0, [x19,3904]
	add	w0, w0, 1
	strh	wzr, [x19,3898]
	strh	w20, [x19,3900]
	str	w0, [x19,872]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L2983
	adrp	x0, .LANCHOR1+3064
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+3064]
.L2983:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
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
	cbz	w0, .L3016
.L3018:
	mov	w0, 0
	b	.L3017
.L3016:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0,#:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	cbnz	w0, .L3018
	bl	FlashLoadFactorBbt
	cbz	w0, .L3019
	bl	FlashMakeFactorBbt
.L3019:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,688]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3020
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L3022:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L3022
	cmp	w0, 6
	bhi	.L3023
	add	x0, x19, :lo12:.LANCHOR0
	b	.L3045
.L3023:
	mov	w1, 0
	mov	w4, 1
.L3026:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L3026
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L3045
	mov	w1, 36
.L3045:
	strb	w1, [x0,25]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,25]
	strh	w1, [x0,4042]
.L3020:
	adrp	x0, .LC45
	adrp	x1, .LC119
	add	x1, x1, :lo12:.LC119
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
.L3028:
	bl	FtlLoadBbt
	cbz	w0, .L3029
.L3047:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3048
	add	w19, w19, 1
	b	.L3028
.L3048:
	mov	w0, -1
	b	.L3017
.L3029:
	bl	FtlSysBlkInit
	cbnz	w0, .L3047
	adrp	x1, .LANCHOR1+3064
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+3064]
.L3017:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -48]!
	mov	w0, -1
	adrp	x1, .LC119
	add	x29, sp, 0
	str	x21, [sp,32]
	adrp	x21, .LANCHOR1
	add	x21, x21, :lo12:.LANCHOR1
	add	x1, x1, :lo12:.LC119
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	adrp	x20, .LANCHOR0
	str	w0, [x21,3064]
	adrp	x0, .LANCHOR4+388
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR0
	str	wzr, [x0,#:lo12:.LANCHOR4+388]
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
	cbz	w0, .L3050
	adrp	x0, .LC138
	add	x0, x0, :lo12:.LC138
	b	.L3062
.L3050:
	bl	FtlSysBlkInit
	cbz	w0, .L3052
	adrp	x0, .LC139
	add	x0, x0, :lo12:.LC139
.L3062:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 40
	bl	printk
	b	.L3051
.L3052:
	mov	w1, 1
	str	w1, [x21,3064]
	bl	ftl_do_gc
	ldrh	w0, [x19,3600]
	cmp	w0, 15
	bhi	.L3053
	mov	w19, 1024
.L3054:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3054
	b	.L3051
.L3053:
	ldrb	w0, [x20,136]
	cbz	w0, .L3051
	mov	w19, 128
.L3056:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3056
.L3051:
	mov	w0, 0
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlInit, .-FtlInit
	.global	gc_ink_free_return_value
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
	.type	__func__.19429, %object
	.size	__func__.19429, 11
__func__.19429:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20378, %object
	.size	__func__.20378, 21
__func__.20378:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19502, %object
	.size	__func__.19502, 8
__func__.19502:
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
	.string	"remove_from_free_sys_Queue %x\n"
.LC35:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC36:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC37:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC38:
	.string	"FtlMapWritePage error = %x\n"
.LC39:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC40:
	.string	"ftl_map_blk_gc blk info: %x %x %x\n"
.LC41:
	.string	"page map lost: %x %x\n"
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
	.string	"page map lost %x %x %x %x\n"
.LC48:
	.string	"slc mode"
.LC49:
	.string	"FLASH INFO:\n"
.LC50:
	.string	"FLASH ID: %x\n"
.LC51:
	.string	"Device Capacity: %d MB\n"
.LC52:
	.string	"FMWAIT: %x %x %x %x\n"
.LC53:
	.string	"FTL INFO:\n"
.LC54:
	.string	"g_MaxLpn = 0x%x\n"
.LC55:
	.string	"g_VaildLpn = 0x%x\n"
.LC56:
	.string	"read_page_count = 0x%x\n"
.LC57:
	.string	"discard_page_count = 0x%x\n"
.LC58:
	.string	"write_page_count = 0x%x\n"
.LC59:
	.string	"cache_write_count = 0x%x\n"
.LC60:
	.string	"l2p_write_count = 0x%x\n"
.LC61:
	.string	"gc_page_count = 0x%x\n"
.LC62:
	.string	"totle_write = %d MB\n"
.LC63:
	.string	"totle_read = %d MB\n"
.LC64:
	.string	"GSV = 0x%x\n"
.LC65:
	.string	"GDV = 0x%x\n"
.LC66:
	.string	"bad blk num = %d %d\n"
.LC67:
	.string	"free_superblocks = 0x%x\n"
.LC68:
	.string	"mlc_EC = 0x%x\n"
.LC69:
	.string	"slc_EC = 0x%x\n"
.LC70:
	.string	"avg_EC = 0x%x\n"
.LC71:
	.string	"sys_EC = 0x%x\n"
.LC72:
	.string	"max_EC = 0x%x\n"
.LC73:
	.string	"min_EC = 0x%x\n"
.LC74:
	.string	"PLT = 0x%x\n"
.LC75:
	.string	"POT = 0x%x\n"
.LC76:
	.string	"MaxSector = 0x%x\n"
.LC77:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC78:
	.string	"sys_blks_pp = 0x%x\n"
.LC79:
	.string	"free sysblock = 0x%x\n"
.LC80:
	.string	"data_blks_pp = 0x%x\n"
.LC81:
	.string	"data_op_blks_pp = 0x%x\n"
.LC82:
	.string	"max_data_blks = 0x%x\n"
.LC83:
	.string	"Sys.id = 0x%x\n"
.LC84:
	.string	"Bbt.id = 0x%x\n"
.LC85:
	.string	"ACT.page = 0x%x\n"
.LC86:
	.string	"ACT.plane = 0x%x\n"
.LC87:
	.string	"ACT.id = 0x%x\n"
.LC88:
	.string	"ACT.mode = 0x%x\n"
.LC89:
	.string	"ACT.a_pages = 0x%x\n"
.LC90:
	.string	"ACT VPC = 0x%x\n"
.LC91:
	.string	"BUF.page = 0x%x\n"
.LC92:
	.string	"BUF.plane = 0x%x\n"
.LC93:
	.string	"BUF.id = 0x%x\n"
.LC94:
	.string	"BUF.mode = 0x%x\n"
.LC95:
	.string	"BUF.a_pages = 0x%x\n"
.LC96:
	.string	"BUF VPC = 0x%x\n"
.LC97:
	.string	"TMP.page = 0x%x\n"
.LC98:
	.string	"TMP.plane = 0x%x\n"
.LC99:
	.string	"TMP.id = 0x%x\n"
.LC100:
	.string	"TMP.mode = 0x%x\n"
.LC101:
	.string	"TMP.a_pages = 0x%x\n"
.LC102:
	.string	"GC.page = 0x%x\n"
.LC103:
	.string	"GC.plane = 0x%x\n"
.LC104:
	.string	"GC.id = 0x%x\n"
.LC105:
	.string	"GC.mode = 0x%x\n"
.LC106:
	.string	"GC.a_pages = 0x%x\n"
.LC107:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC108:
	.string	"Read Err Cnt = 0x%x\n"
.LC109:
	.string	"Prog Err Cnt = 0x%x\n"
.LC110:
	.string	"gc_free_blk_th= 0x%x\n"
.LC111:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC112:
	.string	"gc_skip_write_count= 0x%x\n"
.LC113:
	.string	"gc_blk_index= 0x%x\n"
.LC114:
	.string	"free min EC= 0x%x\n"
.LC115:
	.string	"free max EC= 0x%x\n"
.LC116:
	.string	"GC__SB VPC = 0x%x\n"
.LC117:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC118:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC119:
	.string	"FTL version: 5.0.48 20180408"
.LC120:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC121:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC122:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC123:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC124:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC125:
	.string	"decrement_vpc_count %x = %d\n"
.LC126:
	.string	"RSB refresh addr %x\n"
.LC127:
	.string	"spuer block %x vpn is 0\n "
.LC128:
	.string	"g_recovery_ppa %x ver %x\n "
.LC129:
	.string	"...%s enter...\n"
.LC130:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC131:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC132:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC133:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC134:
	.string	"GC des block %x done\n"
.LC135:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC136:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC137:
	.string	"erase power lost blk = %x vpc=%x\n"
.LC138:
	.string	"...%s: no bad block mapping table, format device\n"
.LC139:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
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
	.size	NandFlashParaTbl, 2368
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
	.byte	2
	.hword	1096
	.hword	1503
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
	.hword	1503
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
.LANCHOR4 = . + 8704
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
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.zero	1
	.zero	2
	.type	gBootDdrMode, %object
	.size	gBootDdrMode, 4
gBootDdrMode:
	.zero	4
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.zero	4
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
	.zero	4
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
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.zero	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.zero	4
	.type	g_LowFormat, %object
	.size	g_LowFormat, 4
g_LowFormat:
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
