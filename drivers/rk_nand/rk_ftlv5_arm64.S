/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-09-23
 * function: rk ftl v5 for rockchip soc base on arm v8 to support MLC NAND.
 */
	.cpu generic+fp+simd
	.file	"rk_ftlv5_arm64.S"
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
	.type	flash_read_ecc, %function
flash_read_ecc:
	adrp	x2, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	stp	x29, x30, [sp, -32]!
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 24
	add	x29, sp, 0
	add	x1, x2, x0
	str	x19, [sp,16]
	ldrb	w1, [x1,8]
	ldr	x19, [x2,x0]
	mov	w0, 122
	add	x19, x19, x1, lsl 8
	str	w0, [x19,2056]
	mov	x0, 400
	bl	__const_udelay
	ldr	w1, [x19,2048]
	ldr	w0, [x19,2048]
	and	w1, w1, 15
	and	w0, w0, 15
	cmp	w0, w1
	csel	w2, w0, w1, cs
	ldr	w1, [x19,2048]
	ldr	w0, [x19,2048]
	and	w1, w1, 15
	ldr	x19, [sp,16]
	cmp	w2, w1
	and	w0, w0, 15
	csel	w1, w2, w1, cs
	cmp	w1, w0
	csel	w0, w1, w0, cs
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_read_ecc, .-flash_read_ecc
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	mov	x5, x0
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L16
	ldrb	w4, [x5,1]
	mov	w0, 0
	ldrb	w3, [x1,1]
	cmp	w4, w3
	beq	.L13
.L16:
	mov	x3, 0
.L14:
	cmp	w3, w2
	mov	w4, w3
	bcs	.L19
	ldrb	w0, [x5,x3]
	add	x3, x3, 1
	add	x6, x1, x3
	ldrb	w6, [x6,-1]
	cmp	w6, w0
	beq	.L14
	add	w0, w4, 1
	b	.L13
.L19:
	mov	w0, 0
.L13:
	ret
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	adrp	x2, .LANCHOR0
	uxtb	w0, w0
	add	x2, x2, :lo12:.LANCHOR0
	ldrb	w3, [x2,89]
	ldr	w2, [x2,92]
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
	adrp	x2, .LANCHOR0+96
	ldrb	w2, [x2,#:lo12:.LANCHOR0+96]
	cbz	w2, .L22
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w19, -1073741824
	csel	w19, w1, w19, ne
.L22:
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
	ldrb	w0, [x0,96]
	cbz	w0, .L29
	mov	w0, w21
	bl	FlashRsvdBlkChk
	cmp	w0, wzr
	orr	w1, w20, -1073741824
	csel	w20, w1, w20, ne
.L29:
	add	x2, x19, :lo12:.LANCHOR0
	add	x2, x2, x21, sxtw 4
	ldr	x0, [x2,24]
	str	w20, [x0,336]
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashBlockAlignInit
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	uxth	w0, w0
	adrp	x1, .LANCHOR0
	cmp	w0, 512
	add	x1, x1, :lo12:.LANCHOR0
	bls	.L36
	mov	w0, 1024
	b	.L40
.L36:
	cmp	w0, 256
	bls	.L38
	mov	w0, 512
	b	.L40
.L38:
	cmp	w0, 128
	bls	.L40
	mov	w0, 256
.L40:
	str	w0, [x1,92]
	ret
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	adrp	x4, .LANCHOR0
	uxtb	w0, w0
	add	x4, x4, :lo12:.LANCHOR0
	sbfiz	x2, x0, 4, 32
	stp	x29, x30, [sp, -16]!
	add	x3, x4, 24
	add	x29, sp, 0
	ldr	x4, [x4,104]
	add	x5, x3, x2
	ldr	x2, [x3,x2]
	ldrb	w4, [x4,7]
	ldrb	w3, [x5,8]
	cmp	w4, 1
	bne	.L42
	sxtw	x4, w3
	mov	w5, 38
	add	x4, x4, 8
	add	x4, x2, x4, lsl 8
	str	w5, [x4,8]
.L42:
	ubfiz	x3, x3, 8, 8
	add	x2, x2, x3
	and	w3, w1, 255
	str	wzr, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w3, [x2,2052]
	lsr	w3, w1, 8
	str	w3, [x2,2052]
	lsr	w3, w1, 16
	str	w3, [x2,2052]
	mov	w3, 48
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
	add	x4, x3, 24
	stp	x29, x30, [sp, -16]!
	add	x5, x4, x2
	add	x29, sp, 0
	ldr	x6, [x4,x2]
	lsr	w4, w1, 8
	ldrb	w2, [x3,128]
	lsr	w3, w1, 16
	cmp	w2, 1
	ldrb	w2, [x5,8]
	and	w5, w1, 255
	add	x2, x6, x2, lsl 8
	bne	.L44
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L46
.L44:
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
.L46:
	mov	w3, 224
	str	w3, [x2,2056]
	bl	FlashSetRandomizer
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	adrp	x3, .LANCHOR0
	uxtb	w0, w0
	add	x3, x3, :lo12:.LANCHOR0
	sbfiz	x5, x0, 4, 32
	add	x3, x3, 24
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
	add	x4, x3, 24
	add	x5, x4, x0
	ldr	x4, [x4,x0]
	ldrb	w0, [x5,8]
	cbz	w2, .L49
	add	x2, x4, x0, lsl 8
	mov	w5, 96
	str	w5, [x2,2056]
	and	w5, w1, 255
	str	w5, [x2,2052]
	lsr	w5, w1, 8
	str	w5, [x2,2052]
	lsr	w5, w1, 16
	str	w5, [x2,2052]
	ldr	w2, [x3,92]
	add	w1, w1, w2
.L49:
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
	add	x5, x2, 24
	lsr	w3, w1, 16
	add	x7, x5, x4
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldrb	w6, [x2,123]
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
	stp	x29, x30, [sp, -32]!
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 24
	add	x29, sp, 0
	add	x2, x1, x0
	stp	x19, x20, [sp,16]
	ldrb	w19, [x2,8]
	ldr	x20, [x1,x0]
	mov	x0, 36284
	add	x19, x19, 8
	movk	x0, 0x6, lsl 16
	add	x19, x20, x19, lsl 8
	bl	__const_udelay
	mov	w0, 16
	str	w0, [x19,8]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x2, x1, 24
	add	x3, x2, x0
	ldrb	w1, [x1,122]
	ldr	x2, [x2,x0]
	ldrb	w0, [x3,8]
	add	x0, x0, 8
	add	x0, x2, x0, lsl 8
	str	w1, [x0,8]
	ret
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	FlashReadStatus
	.type	FlashReadStatus, %function
FlashReadStatus:
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	stp	x29, x30, [sp, -32]!
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 24
	add	x29, sp, 0
	add	x2, x1, x0
	str	x19, [sp,16]
	ldrb	w2, [x2,8]
	ldr	x19, [x1,x0]
	mov	w0, 112
	add	x19, x19, x2, lsl 8
	str	w0, [x19,2056]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x19,2048]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	js_hash
	.type	js_hash, %function
js_hash:
	mov	x5, x0
	mov	w0, 42982
	mov	x2, 0
	movk	w0, 0x47c6, lsl 16
.L58:
	cmp	w1, w2
	bls	.L60
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L58
.L60:
	ret
	.size	js_hash, .-js_hash
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
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w19, w1
	cbnz	w0, .L63
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L64:
	add	x1, x3, 144
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L64
.L70:
	add	x20, x2, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 2048
	add	x0, x20, 1168
	uxth	w19, w19
	bl	ftl_memset
	mov	x0, 0
	mov	x2, x20
	b	.L65
.L63:
	cmp	w0, 1
	bne	.L66
	mov	x0, 0
	mov	w5, 3
	mov	w6, 2
	add	x4, x2, :lo12:.LANCHOR0
.L69:
	cmp	x0, 3
	uxth	w3, w0
	mov	w1, w3
	bls	.L67
	ubfiz	w1, w3, 1, 15
	and	w3, w3, 1
	cmp	w3, wzr
	csel	w3, w5, w6, ne
	sub	w1, w1, w3
	uxth	w1, w1
.L67:
	add	x3, x4, 144
	strh	w1, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L69
	b	.L70
.L66:
	cmp	w0, 2
	bne	.L71
	mov	w1, 65535
	mov	x0, 0
	add	x5, x2, :lo12:.LANCHOR0
.L73:
	add	x4, x5, 144
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w1, w3, hi
	add	w1, w1, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	uxth	w1, w1
	bne	.L73
	b	.L70
.L71:
	cmp	w0, 3
	bne	.L74
	mov	x0, 0
	mov	w6, 5
	mov	w7, 4
	add	x5, x2, :lo12:.LANCHOR0
.L77:
	cmp	x0, 5
	uxth	w4, w0
	mov	w3, w4
	bls	.L75
	ubfiz	w1, w4, 1, 15
	and	w4, w4, 1
	cmp	w4, wzr
	csel	w3, w6, w7, ne
	sub	w3, w1, w3
	uxth	w3, w3
.L75:
	add	x1, x5, 144
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 512
	bne	.L77
	b	.L70
.L74:
	cmp	w0, 4
	bne	.L78
	add	x1, x2, :lo12:.LANCHOR0
	mov	w6, 7
	add	x3, x1, 144
	mov	w5, 6
	strh	w0, [x3,8]
	mov	w0, 5
	strh	wzr, [x1,144]
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
.L80:
	and	w4, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w4, wzr
	csel	w4, w6, w5, ne
	add	x3, x3, 2
	uxth	w0, w0
	sub	w1, w1, w4
	cmp	w0, 512
	strh	w1, [x3,14]
	bne	.L80
	b	.L70
.L78:
	cmp	w0, 5
	bne	.L81
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L82:
	add	x1, x3, 144
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L82
	mov	x0, 0
.L83:
	add	x3, x1, x0
	add	w4, w0, 16
	add	x0, x0, 2
	cmp	x0, 992
	strh	w4, [x3,32]
	bne	.L83
	b	.L70
.L81:
	cmp	w0, 6
	bne	.L84
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
	add	x1, x5, 144
	strh	w0, [x1,x3,lsl 1]
	add	x3, x3, 1
	cmp	x3, 512
	bne	.L87
	b	.L70
.L84:
	cmp	w0, 9
	bne	.L88
	add	x0, x2, :lo12:.LANCHOR0
	add	x1, x0, 144
	strh	wzr, [x0,144]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L89:
	add	x3, x1, x0
	add	w4, w0, 3
	add	x0, x0, 2
	cmp	x0, 1018
	strh	w4, [x3,6]
	bne	.L89
	b	.L70
.L88:
	cmp	w0, 10
	bne	.L90
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L91:
	add	x1, x3, 144
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L91
	mov	x0, 0
.L92:
	add	x3, x1, x0
	add	w4, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w4, [x3,126]
	bne	.L92
	b	.L70
.L90:
	cmp	w0, 11
	bne	.L93
	mov	x0, 0
	add	x1, x2, :lo12:.LANCHOR0
.L94:
	add	x3, x1, 144
	strh	w0, [x3,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 8
	bne	.L94
	mov	w6, 7
	mov	w5, 6
.L96:
	and	w4, w0, 1
	ubfiz	w1, w0, 1, 15
	add	w0, w0, 1
	cmp	w4, wzr
	csel	w4, w6, w5, ne
	add	x3, x3, 2
	uxth	w0, w0
	sub	w1, w1, w4
	cmp	w0, 512
	strh	w1, [x3,14]
	bne	.L96
	b	.L70
.L93:
	cmp	w0, 12
	bne	.L70
	add	x1, x2, :lo12:.LANCHOR0
	mov	w3, 4
	add	x0, x1, 144
	strh	wzr, [x1,144]
	mov	w1, 1
	strh	w1, [x0,2]
	mov	w1, 2
	strh	w1, [x0,4]
	mov	w1, 3
	strh	w1, [x0,6]
.L97:
	sub	w1, w3, #1
	add	x0, x0, 2
	add	w1, w1, w3, lsr 1
	add	w3, w3, 1
	strh	w1, [x0,6]
	uxth	w3, w3
	cmp	w3, 512
	bne	.L97
	b	.L70
.L65:
	cmp	w19, w0, uxth
	bls	.L122
	add	x1, x2, 144
	add	x3, x2, 1168
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x3,w1,sxtw 1]
	b	.L65
.L122:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashPrintInfo
	.type	FlashPrintInfo, %function
FlashPrintInfo:
	ret
	.size	FlashPrintInfo, .-FlashPrintInfo
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
	add	x21, x2, 304
	add	x25, x2, 256
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
.L125:
	ldrb	w0, [x24]
	cmp	w0, w20
	bls	.L131
	mov	w0, 85
	str	w0, [x22,8]
	add	x0, x19, 256
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x26]
	cmp	w0, 34
	bne	.L126
	ldrsb	w0, [x25,x20]
	b	.L130
.L126:
	cmp	w0, 35
	bne	.L128
	ldrsb	w0, [x21,x20]
	b	.L130
.L128:
	ldrsb	w0, [x23,400]
.L130:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L125
.L131:
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
	add	x23, x1, 412
	mov	x20, 0
	add	x22, x22, :lo12:g_maxRegNum
	mov	w24, 161
	mov	x19, x2
.L133:
	ldrb	w0, [x22]
	cmp	w0, w20
	bls	.L135
	str	w24, [x21,8]
	add	x0, x19, 408
	str	wzr, [x21]
	ldrsb	w0, [x20,x0]
	str	w0, [x21]
	ldrsb	w0, [x23,x20]
	add	x20, x20, 1
	str	w0, [x21]
	mov	x0, 1500
	bl	__const_udelay
	b	.L133
.L135:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	stp	x29, x30, [sp, -96]!
	adrp	x0, .LANCHOR1+482
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	ldrh	w0, [x0,#:lo12:.LANCHOR1+482]
	stp	x19, x20, [sp,16]
	add	x19, x21, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	strb	wzr, [x19,3216]
	add	x25, x19, 3220
	strb	wzr, [x19,3217]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	mov	x0, x25
	add	x24, x19, 3228
	adrp	x26, IDByte
	mov	x23, 0
	bl	ftl_memset
	add	x26, x26, :lo12:IDByte
	mov	w1, 0
	mov	w2, 32
	mov	x0, x24
	bl	ftl_memset
	add	x0, x19, 3260
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,104]
	add	x22, x20, 1
.L138:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x26, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L137
	ldrb	w1, [x19,3216]
	str	w0, [x24,w1,sxtw 2]
	add	w0, w1, 1
	strb	w23, [x25,w1,sxtw]
	strb	w0, [x19,3216]
.L137:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L138
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w0, [x19,3216]
	strb	w0, [x19,3217]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L139
.L143:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldrb	w0, [x21,3216]
	ldr	x27, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x21,3452]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 96
	ret
.L139:
	adrp	x24, IDByte
	ldr	w26, [x19,92]
	mov	x23, 0
	add	x24, x24, :lo12:IDByte
	add	x25, x19, 3228
	add	x27, x19, 3220
.L142:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x24, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L140
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,3216]
	mul	w2, w1, w26
	ldrh	w1, [x20,14]
	mov	x0, x3
	and	w1, w1, 65280
	mul	w1, w2, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w2, [x20,23]
	cbz	w2, .L141
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L141:
	add	w0, w0, 1
	strb	w23, [x27,x3]
	strb	w0, [x19,3216]
.L140:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L142
	b	.L143
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
	add	x1, x1, 3456
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
	adrp	x19, .LANCHOR1
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	adrp	x23, IDByte
	add	x21, x19, :lo12:.LANCHOR1
	add	x21, x21, 504
	mov	w20, 0
	add	x23, x23, :lo12:IDByte
.L155:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x23
	bl	FlashMemCmp8
	mov	w22, w0
	cbnz	w0, .L152
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 504
	adds	x20, x1, x20
	beq	.L158
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L157
.L152:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 86
	bne	.L155
	b	.L158
.L157:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3256]
	cmp	w4, w3
	beq	.L156
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L157
	mov	w0, w2
.L156:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3256
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 112
	mov	w2, 32
	add	x19, x19, 472
	bl	ftl_memcpy
	mov	x0, x19
	mov	x1, x20
	mov	w2, 32
	bl	ftl_memcpy
	ldrh	w0, [x19,10]
	bl	FlashBlockAlignInit
	b	.L153
.L158:
	mov	w22, -1
.L153:
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
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,1152]
	ldr	w2, [x1]
	str	w2, [x0,1160]
	ldr	w2, [x1,4]
	str	w2, [x0,1164]
	ldr	w2, [x1,8]
	str	w2, [x0,1168]
	ldr	w2, [x1,12]
	str	w2, [x0,1172]
	ldr	w2, [x1,304]
	str	w2, [x0,1176]
	ldr	w2, [x1,308]
	str	w2, [x0,1180]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,1184]
	str	w1, [x0,1188]
	ret
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	adrp	x6, .LANCHOR2
	uxtb	w4, w4
	add	x7, x6, :lo12:.LANCHOR2
	mov	x9, x6
	add	x7, x7, 1192
	ldrh	w5, [x7,12]
	ldrh	w7, [x7,14]
	mul	w5, w5, w7
	adrp	x7, .LANCHOR0
	add	x8, x7, :lo12:.LANCHOR0
	uxth	w5, w5
	ldrh	w10, [x8,92]
	ldrb	w8, [x8,88]
	ubfiz	w6, w10, 1, 15
	cmp	w8, 1
	ldr	w8, [x0,4]
	csel	w10, w6, w10, eq
	cmp	w1, 1
	ubfx	x6, x8, 10, 16
	and	w8, w8, 1023
	udiv	w11, w6, w5
	msub	w5, w11, w5, w6
	uxth	w6, w5
	bne	.L165
	add	x1, x9, :lo12:.LANCHOR2
	ldrb	w1, [x1,1220]
	cbnz	w1, .L165
	add	x1, x7, :lo12:.LANCHOR0
	add	x1, x1, 144
	ldrh	w8, [x1,w8,sxtw 1]
.L165:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 3228
	mov	w1, 0
	ldr	w5, [x7,w11,uxtw 2]
	madd	w5, w6, w10, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w11, [x3]
	bls	.L166
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L166:
	mov	w0, w1
	ret
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	stp	x29, x30, [sp, -32]!
	adrp	x3, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x4, x3, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x5, x4, 24
	stp	x19, x20, [sp,16]
	add	x6, x5, x0
	uxtb	w2, w2
	ldr	x20, [x5,x0]
	ldr	x0, [x4,104]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L169
	add	x4, x4, 112
	cbnz	w2, .L170
	ldrb	w2, [x4,13]
	b	.L178
.L170:
	ldrb	w2, [x4,14]
.L178:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,127]
	cbz	w4, .L173
	add	x3, x19, 8
	lsl	x3, x3, 8
.L177:
	cmp	w2, w4
	bcs	.L173
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L177
.L169:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L173:
	add	x19, x19, 8
	mov	x0, 400
	lsl	x19, x19, 8
	bl	__const_udelay
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
.L180:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L180
	tbnz	x3, 6, .L181
	mov	x0, 1
	mov	x1, 3
	bl	usleep_range
	b	.L180
.L181:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
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
	ldr	x0, [x1,104]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,92]
	mul	w0, w0, w2
	strh	w0, [x19,4]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w2, [x0,1221]
	ldr	w0, [x0,1224]
	str	w0, [x19]
	strb	w2, [x19,7]
	ldrb	w4, [x1,3216]
	ldr	x0, [x1,104]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,104]
	add	x1, x1, 3220
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L186:
	cmp	w4, w0, uxtb
	bls	.L188
	ldrb	w2, [x0,x1]
	add	x0, x0, 1
	ldrb	w3, [x19,10]
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L186
.L188:
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	ldr	w2, [x1,1228]
	str	w0, [x1,1228]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+104
	ldr	x0, [x0,#:lo12:.LANCHOR0+104]
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
	add	x1, x1, 24
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
	adrp	x1, .LANCHOR2+1152
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1,#:lo12:.LANCHOR2+1152]
	orr	w0, w0, 1
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR2+1152
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR2+1152]
	mov	w1, 16640
	orr	w0, w0, w1
	str	w0, [x2,344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR2+1152
	cmp	w0, wzr
	ldr	x2, [x1,#:lo12:.LANCHOR2+1152]
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
	adrp	x1, .LANCHOR2+1152
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR2+1152]
	ldr	w1, [x2]
	beq	.L199
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
	b	.L201
.L199:
	and	w1, w1, -8193
.L201:
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
	add	x1, x1, 24
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
	add	x1, x1, 24
	ldr	x1, [x1,x0]
	ldr	w0, [x1]
	and	w0, w0, -256
	and	w0, w0, -131073
	str	w0, [x1]
	ret
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	2
	.global	HynixSetRRPara
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	stp	x29, x30, [sp, -96]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	uxtb	x23, w0
	add	x0, x4, :lo12:.LANCHOR0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	adrp	x24, .LANCHOR2
	stp	x21, x22, [sp,32]
	mov	x20, x2
	mov	x26, x23
	ldr	x0, [x0,104]
	uxtb	w27, w1
	uxtb	w25, w3
	mov	x2, x4
	add	x19, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L207
	add	x19, x19, x23, lsl 6
	lsl	w4, w25, 2
	add	x19, x19, 1252
	b	.L213
.L207:
	cmp	w0, 7
	bne	.L209
	mov	x3, 160
	mov	w4, 10
	madd	x19, x23, x3, x19
	mul	w4, w25, w4
	add	x19, x19, 1260
.L213:
	add	x19, x19, x4, sxtw
	b	.L208
.L209:
	cmp	w0, 8
	bne	.L210
	add	x19, x19, 1260
	add	w4, w25, w25, lsl 2
	b	.L213
.L210:
	lsl	x4, x23, 3
	add	x4, x4, x25, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 1252
.L208:
	add	x4, x2, :lo12:.LANCHOR0
	lsl	x0, x23, 4
	add	x4, x4, 24
	uxtb	x27, w27
	add	x1, x4, x0
	ldr	x21, [x4,x0]
	mov	w0, w26
	ldrb	w22, [x1,8]
	bl	NandcFlashCs
	ubfiz	x22, x22, 8, 8
	mov	w0, 54
	add	x21, x21, x22
	mov	x22, 0
	str	w0, [x21,2056]
.L211:
	cmp	x22, x27
	beq	.L214
	ldrb	w0, [x20,x22]
	str	w0, [x21,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19,x22]
	add	x22, x22, 1
	str	w0, [x21,2048]
	b	.L211
.L214:
	add	x24, x24, :lo12:.LANCHOR2
	mov	w0, 22
	add	x23, x24, x23
	str	w0, [x21,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w25, [x23,2088]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 96
	ret
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	adrp	x0, .LANCHOR0+104
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR0+104]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x19, 0
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L215
	adrp	x20, .LANCHOR2
	adrp	x21, IDByte
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:IDByte
	add	x22, x20, 1236
.L220:
	lsl	x1, x19, 3
	uxtb	w0, w19
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L217
	ldrb	w1, [x20,1233]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L217:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L220
.L215:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
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
	add	x0, x20, 3260
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L224
	mov	w0, w21
	add	x20, x20, 3228
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
	cbz	x1, .L224
	str	w0, [x1]
	str	xzr, [x19,16]
.L224:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	NandcDelayns
	.type	NandcDelayns, %function
NandcDelayns:
	stp	x29, x30, [sp, -16]!
	uxtw	x0, w0
	add	x29, sp, 0
	bl	__ndelay
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	NandcWaitFlashReadyNoDelay
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x1, 24
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
.L232:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L233
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L232
	mov	w0, -1
	b	.L231
.L233:
	mov	w0, 0
.L231:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	stp	x29, x30, [sp, -48]!
	adrp	x1, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x1, x1, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x1, 24
	stp	x19, x20, [sp,16]
	mov	w19, 34464
	ldr	x20, [x1,x0]
	movk	w19, 0x1, lsl 16
	mov	x0, 650
	bl	__const_udelay
.L237:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L238
	mov	x0, 1
	mov	x1, 2
	bl	usleep_range
	subs	w19, w19, #1
	bne	.L237
	mov	w0, -1
	b	.L236
.L238:
	mov	w0, 0
.L236:
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
	add	x1, x1, 24
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
	.global	flash_enter_slc_mode
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	uxtb	w22, w0
	add	x0, x21, :lo12:.LANCHOR2
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	ldrb	w0, [x0,1220]
	cbz	w0, .L241
	mov	w0, w22
	bl	NandcFlashCs
	adrp	x1, .LANCHOR0
	sxtw	x0, w22
	add	x1, x1, :lo12:.LANCHOR0
	lsl	x2, x0, 4
	add	x1, x1, 24
	lsl	x0, x0, 3
	add	x3, x1, x2
	ldr	x23, [x1,x2]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	ldrb	w19, [x3,8]
	ldrb	w0, [x1,x0]
	cmp	w0, 44
	bne	.L243
	ubfiz	x20, x19, 8, 8
	mov	w0, 239
	add	x20, x23, x20
	str	w0, [x20,2056]
	mov	w0, 145
	str	w0, [x20,2052]
	mov	x0, 250
	bl	__const_udelay
	str	wzr, [x20,2048]
	mov	w0, 1
	str	w0, [x20,2048]
	str	wzr, [x20,2048]
	mov	x0, 500
	str	wzr, [x20,2048]
	bl	__const_udelay
.L243:
	add	x19, x19, 8
	mov	w0, w22
	add	x21, x21, :lo12:.LANCHOR2
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 218
	str	w0, [x19,8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	mov	w0, 2
	strb	w0, [x21,2092]
.L241:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	uxtb	w22, w0
	add	x0, x21, :lo12:.LANCHOR2
	str	x23, [sp,48]
	stp	x19, x20, [sp,16]
	ldrb	w0, [x0,1220]
	cbz	w0, .L247
	mov	w0, w22
	bl	NandcFlashCs
	adrp	x1, .LANCHOR0
	sxtw	x0, w22
	add	x1, x1, :lo12:.LANCHOR0
	lsl	x2, x0, 4
	add	x1, x1, 24
	lsl	x0, x0, 3
	add	x3, x1, x2
	ldr	x23, [x1,x2]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	ldrb	w19, [x3,8]
	ldrb	w0, [x1,x0]
	cmp	w0, 44
	bne	.L249
	ubfiz	x20, x19, 8, 8
	mov	w0, 239
	add	x20, x23, x20
	str	w0, [x20,2056]
	mov	w0, 145
	str	w0, [x20,2052]
	mov	x0, 250
	bl	__const_udelay
	mov	w0, 2
	str	w0, [x20,2048]
	mov	w0, 1
	str	w0, [x20,2048]
	str	wzr, [x20,2048]
	mov	x0, 500
	str	wzr, [x20,2048]
	bl	__const_udelay
.L249:
	add	x19, x19, 8
	mov	w0, w22
	add	x21, x21, :lo12:.LANCHOR2
	add	x19, x23, x19, lsl 8
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 223
	str	w0, [x19,8]
	mov	w0, w22
	bl	NandcWaitFlashReady
	strb	wzr, [x21,2092]
.L247:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
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
	adrp	x1, .LANCHOR2+2093
	adrp	x3, .LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x3, x3, :lo12:.LANCHOR0
	adrp	x7, IDByte
	add	x29, sp, 0
	ldrb	w1, [x1,#:lo12:.LANCHOR2+2093]
	add	x3, x3, 24
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
.L265:
	ldrb	w4, [x6,x7]
	ldr	x1, [x3]
	cmp	w4, 152
	ldrb	w2, [x3,8]
	beq	.L255
	cmp	w4, 69
	beq	.L255
	cmp	w4, 173
	beq	.L255
	cmp	w4, 44
	bne	.L256
.L255:
	cmp	w0, 1
	bne	.L258
	cbz	w11, .L256
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L259
	str	w0, [x1,2052]
	b	.L276
.L259:
	cmp	w4, 44
	bne	.L261
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L263
.L261:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L263
.L258:
	cbz	w5, .L256
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L262
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L263
.L262:
	cmp	w4, 44
	bne	.L264
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L263
.L264:
	str	w9, [x1,2052]
.L276:
	str	wzr, [x1,2048]
.L263:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L256:
	add	x6, x6, 8
	add	x3, x3, 16
	cmp	x6, 32
	bne	.L265
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
	adrp	x3, .LANCHOR1+481
	adrp	x5, .LANCHOR0
	stp	x29, x30, [sp, -48]!
	uxtb	w0, w0
	add	x5, x5, :lo12:.LANCHOR0
	add	x5, x5, 24
	sbfiz	x6, x0, 4, 32
	add	x29, sp, 0
	ldrb	w4, [x3,#:lo12:.LANCHOR1+481]
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
	add	x3, x3, 24
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
	mov	x0, 400
	bl	__const_udelay
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
	mov	x0, 1000
	bl	__const_udelay
	mov	w1, 5
	adrp	x2, .LANCHOR1
	add	x2, x2, :lo12:.LANCHOR1
	adrp	x0, g_maxRegNum
	smull	x1, w19, w1
	add	x3, x2, 256
	adrp	x4, g_retryMode
	add	x2, x2, 304
	add	x3, x3, x1
	add	x0, x0, :lo12:g_maxRegNum
	add	x1, x2, x1
	add	x4, x4, :lo12:g_retryMode
	mov	x2, 0
.L280:
	ldrb	w5, [x0]
	cmp	w5, w2
	bls	.L285
	ldrb	w5, [x4]
	cmp	w5, 67
	bne	.L281
	ldrsb	w5, [x3,x2]
	b	.L284
.L281:
	ldrsb	w5, [x1,x2]
.L284:
	str	w5, [x20]
	add	x2, x2, 1
	b	.L280
.L285:
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
	add	x3, x3, 24
	add	x0, x3, x19
	ldr	x19, [x3,x19]
	ldrb	w2, [x0,8]
	mov	w0, 239
	add	x19, x19, x2, lsl 8
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
	add	x25, x21, 3220
	add	x23, x23, :lo12:.LC1
.L288:
	cmp	w22, w24
	beq	.L297
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,3216]
	cmp	w2, w0
	bcc	.L289
	mov	w0, -1
	str	w0, [x20]
	b	.L290
.L289:
	uxtw	x2, w2
	mov	x0, 24
	ldrb	w19, [x25,x2]
	madd	x2, x2, x0, x21
	mov	w0, w19
	strb	w19, [x2,3260]
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
	ldr	w2, [x21,92]
	add	w1, w2, w1
	mov	w2, 0
	bl	FlashEraseCmd
	mov	w0, w19
	bl	NandcWaitFlashReady
	ldr	w1, [x29,88]
	mov	w0, w19
	bl	FlashReadStatus
	tbz	x0, 0, .L291
	mov	w0, -1
	str	w0, [x20]
.L291:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L292
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L292:
	mov	w0, w19
	bl	NandcFlashDeCs
.L290:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L288
.L297:
	mov	w0, 0
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	mov	w20, 0
	stp	x25, x26, [sp,64]
	mov	w26, w1
	add	x1, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	x24, x0
	mov	w23, w2
	ldrb	w1, [x1,88]
	cbz	w1, .L299
	mov	w1, w2
	bl	FlashEraseSLc2KBlocks
	b	.L300
.L299:
	mov	w27, 56
	adrp	x28, .LANCHOR2
	mov	x25, 24
.L330:
	cmp	w20, w23
	bcs	.L331
	umull	x5, w20, w27
	mov	w1, 0
	add	x2, x29, 120
	add	x21, x24, x5
	sub	w4, w23, w20
	mov	x0, x21
	add	x3, x29, 124
	str	x5, [x29,104]
	bl	LogAddr2PhyAddr
	mov	w22, w0
	add	x1, x19, :lo12:.LANCHOR0
	ldr	w0, [x29,124]
	ldr	x5, [x29,104]
	ldrb	w2, [x1,3216]
	cmp	w0, w2
	bcc	.L301
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L302
.L301:
	add	x2, x28, :lo12:.LANCHOR2
	add	x1, x1, 3260
	ldrb	w2, [x2,2094]
	cmp	w2, wzr
	uxtw	x2, w0
	csel	w22, w22, wzr, ne
	madd	x1, x2, x25, x1
	ldr	x1, [x1,8]
	cbz	x1, .L304
	bl	FlashWaitCmdDone
.L304:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,124]
	add	x0, x0, 3260
	ldr	w2, [x29,120]
	madd	x0, x1, x25, x0
	str	x21, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w22, .L305
	add	w2, w20, 1
	umull	x2, w2, w27
	add	x2, x24, x2
	str	x2, [x0,16]
.L305:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x0, 3220
	ldrb	w21, [x2,x1]
	madd	x1, x1, x25, x0
	mov	w0, w21
	strb	w21, [x1,3260]
	bl	NandcFlashCs
	cmp	w26, 1
	bne	.L306
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L306
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L307
.L306:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L307:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,124]
	add	x0, x0, 3228
	add	w20, w20, w22
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
.L302:
	add	w20, w20, 1
	b	.L330
.L331:
	add	x19, x19, :lo12:.LANCHOR0
	mov	x20, 0
	adrp	x21, .LANCHOR2
	add	x22, x19, 3260
	mov	x25, 24
.L309:
	ldrb	w0, [x19,3216]
	cmp	w0, w20
	bls	.L332
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w26, 1
	bne	.L310
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L310
	mul	x0, x20, x25
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L310:
	add	x20, x20, 1
	b	.L309
.L332:
	adrp	x0, .LANCHOR2+2096
	ldr	w0, [x0,#:lo12:.LANCHOR2+2096]
	cbnz	w0, .L312
.L314:
	mov	w0, 0
	b	.L300
.L312:
	adrp	x0, IDByte
	ldrb	w0, [x0,#:lo12:IDByte]
	cmp	w0, 69
	bne	.L314
	mov	x0, 0
	mov	x1, 56
.L313:
	cmp	w23, w0
	bls	.L314
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L313
.L300:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	stp	x29, x30, [sp, -144]!
	mov	w4, -83
	mov	w3, -82
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x0, 1232
	cmp	w26, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	strb	w26, [x0,1232]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w3, [x1,6]
	strb	w0, [x1,7]
	bne	.L334
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3401
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3401]
	b	.L397
.L334:
	cmp	w26, 3
	bne	.L336
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
	b	.L437
.L336:
	cmp	w26, 4
	bne	.L337
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
.L437:
	mov	w28, 8
	strb	w0, [x1,11]
	mov	w27, w28
	b	.L335
.L337:
	cmp	w26, 5
	bne	.L338
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w28, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L438
.L338:
	cmp	w26, 6
	bne	.L339
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w28, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L438
.L339:
	cmp	w26, 7
	bne	.L340
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
	mov	w28, 12
	strb	w0, [x1,12]
	mov	w27, 10
	mov	w0, -43
	strb	w0, [x1,13]
	b	.L335
.L340:
	cmp	w26, 8
	bne	.L397
	mov	w0, 6
	strb	w0, [x1,4]
	mov	w0, 7
	strb	w0, [x1,5]
	mov	w0, 9
	strb	w26, [x1,6]
	strb	w0, [x1,7]
	mov	w28, 50
	mov	w0, 10
	mov	w27, 5
	strb	w0, [x1,8]
	b	.L335
.L397:
	mov	w28, 7
.L438:
	mov	w27, 4
.L335:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L434
	adrp	x23, .LANCHOR0
	adrp	x24, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR1
	mov	w21, 0
	add	x4, x23, 3220
	add	x5, x20, :lo12:.LANCHOR2
	add	x24, x24, 3384
.L341:
	ldrb	w0, [x23,3216]
	cmp	w0, w21
	bls	.L348
	ldrb	w1, [x4,w21,sxtw]
	add	x0, x5, 1232
	add	x2, x23, 24
	mov	x25, 0
	mov	x22, x0
	add	x19, x0, x1, lsl 6
	lsl	x1, x1, 4
	add	x3, x2, x1
	add	x19, x19, 20
	ldr	x26, [x2,x1]
	ldrb	w1, [x3,8]
	add	x26, x26, x1, lsl 8
	mov	w1, 55
.L343:
	add	x0, x22, x25
	str	w1, [x26,2056]
	str	x5, [x29,120]
	str	x4, [x29,128]
	ldrb	w0, [x0,4]
	str	w0, [x26,2052]
	mov	x0, 400
	str	x1, [x29,136]
	bl	__const_udelay
	ldr	w0, [x26,2048]
	strb	w0, [x19,x25]
	add	x25, x25, 1
	ldr	x1, [x29,136]
	cmp	w27, w25, uxtb
	ldr	x4, [x29,128]
	ldr	x5, [x29,120]
	bhi	.L343
	mov	x0, 0
.L344:
	add	w2, w0, 8
	mov	x1, 0
.L345:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x19,x0]
	add	w3, w6, w3
	strb	w3, [x19,w2,sxtw]
	add	w2, w2, 8
	bne	.L345
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L344
	add	w21, w21, 1
	strb	wzr, [x19,16]
	strb	wzr, [x19,24]
	strb	wzr, [x19,32]
	uxtb	w21, w21
	strb	wzr, [x19,40]
	strb	wzr, [x19,48]
	strb	wzr, [x19,41]
	strb	wzr, [x19,49]
	b	.L341
.L434:
	sub	w0, w26, #3
	cmp	w0, 5
	bhi	.L348
	mul	w0, w27, w28
	adrp	x24, .LANCHOR0
	sub	w25, w27, #1
	asr	w23, w0, 1
	lsl	w0, w0, 4
	add	x24, x24, :lo12:.LANCHOR0
	str	w0, [x29,136]
	lsl	w0, w23, 1
	uxtb	x25, w25
	str	w0, [x29,128]
	add	x0, x24, 3220
	mov	w21, 0
	str	x0, [x29,120]
	add	x0, x25, 1
	str	x0, [x29,112]
.L349:
	ldrb	w0, [x24,3216]
	adrp	x25, .LANCHOR0
	cmp	w0, w21
	bhi	.L395
.L348:
	add	x20, x20, :lo12:.LANCHOR2
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w27, [x20,1233]
	strb	w28, [x20,1234]
	ldp	x25, x26, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L395:
	ldr	x0, [x29,120]
	ldrb	w1, [x0,w21,sxtw]
	add	x0, x24, 24
	str	x1, [x29,104]
	lsl	x3, x1, 4
	mov	x22, x1
	add	x7, x0, x3
	ldr	x0, [x0,x3]
	ldrb	w19, [x7,8]
	add	x19, x0, x19, lsl 8
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w1
	bl	NandcWaitFlashReady
	cmp	w26, 7
	sub	w7, w26, #5
	ldr	x1, [x29,104]
	bne	.L350
	add	x3, x20, :lo12:.LANCHOR2
	mov	x0, 160
	madd	x3, x1, x0, x3
	add	x3, x3, 1260
	b	.L351
.L350:
	cmp	w26, 8
	beq	.L352
	add	x3, x20, :lo12:.LANCHOR2
	add	x3, x3, x1, lsl 6
	add	x3, x3, 1252
.L351:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x19,2056]
	bne	.L353
	mov	w0, 255
	str	w0, [x19,2052]
	mov	w0, 64
	str	w0, [x19,2048]
	mov	w0, 204
	b	.L439
.L353:
	cmp	w7, 1
	bhi	.L355
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1236]
	str	w0, [x19,2052]
	mov	w0, 82
	b	.L440
.L355:
	cmp	w26, 7
	bne	.L354
	mov	w0, 174
	str	w0, [x19,2052]
	str	wzr, [x19,2048]
	mov	w0, 176
.L439:
	str	w0, [x19,2052]
	mov	w0, 77
.L440:
	str	w0, [x19,2048]
.L354:
	mov	w0, 22
	str	w0, [x19,2056]
	mov	w0, 23
	str	w0, [x19,2056]
	mov	w0, 4
	str	w0, [x19,2056]
	mov	w0, 25
	str	w0, [x19,2056]
	str	wzr, [x19,2056]
	cmp	w26, 6
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	bne	.L356
	mov	w0, 31
	str	w0, [x19,2052]
	b	.L357
.L356:
	str	wzr, [x19,2052]
.L357:
	mov	w0, 2
	str	w0, [x19,2052]
	str	wzr, [x19,2052]
.L396:
	mov	w0, 48
	str	w0, [x19,2056]
	mov	w0, w22
	str	x7, [x29,96]
	str	x3, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x7, [x29,96]
	ldr	x3, [x29,104]
	cmp	w7, 1
	cset	w8, ls
	cmp	w26, 8
	cset	w7, eq
	cbnz	w8, .L399
	cbnz	w7, .L399
	cmp	w26, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L358
.L399:
	mov	w1, 16
.L358:
	ldr	x0, [x25,#:lo12:.LANCHOR0]
	mov	x9, 0
.L359:
	ldr	w10, [x19,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L359
	cbz	w7, .L360
	mov	w1, 0
.L362:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L361
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L361
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L362
	b	.L363
.L361:
	cmp	w1, 6
	bls	.L364
.L363:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L365:
	b	.L365
.L360:
	cmp	w26, 7
	bne	.L366
	mov	w1, w7
.L368:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L367
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L367
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L368
	b	.L369
.L367:
	cmp	w1, 7
	bne	.L364
.L369:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L370:
	b	.L370
.L366:
	cmp	w26, 6
	bne	.L364
	mov	x1, 0
.L371:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L364
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L364
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L371
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L373:
	b	.L373
.L364:
	ldr	x10, [x25,#:lo12:.LANCHOR0]
	mov	x0, 0
.L374:
	ldr	w1, [x29,136]
	cmp	w1, w0
	ble	.L442
	ldr	w1, [x19,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L374
.L442:
	ldr	x12, [x25,#:lo12:.LANCHOR0]
	mov	w11, w23
	mov	w9, 8
.L377:
	mov	w0, 0
.L376:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L376
	ldr	w0, [x29,128]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L377
	mov	x9, 0
	mov	w17, 1
.L378:
	mov	w1, 0
	mov	w11, w1
.L382:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L380:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L380
	cmp	w14, 8
	bls	.L381
	orr	w1, w1, w13
	uxth	w1, w1
.L381:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L382
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L378
	mov	x6, 0
	ldr	x1, [x25,#:lo12:.LANCHOR0]
	mov	w9, w6
.L385:
	ldr	w11, [x1,x6]
	add	x6, x6, 4
	cmp	w11, wzr
	csinc	w9, w9, w9, ne
	cmp	x6, 32
	bne	.L385
	cmp	w9, 7
	ble	.L386
	adrp	x0, .LC3
	mov	w2, 1
	add	x0, x0, :lo12:.LC3
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L387:
	b	.L387
.L386:
	cmp	w26, 6
	mov	w6, 4
	beq	.L388
	cmp	w26, 7
	mov	w6, 10
	beq	.L388
	cmp	w7, wzr
	mov	w6, 5
	mov	w1, 8
	csel	w6, w1, w6, eq
.L388:
	mov	w9, 0
.L389:
	mov	x1, 0
.L390:
	add	w11, w0, w1
	ldrb	w12, [x10,x1]
	add	x1, x1, 1
	cmp	w27, w1, uxtb
	strb	w12, [x3,w11,sxtw]
	bhi	.L390
	add	w9, w9, 1
	ldr	x1, [x29,112]
	cmp	w9, w28
	add	w0, w0, w6
	add	x10, x10, x1
	blt	.L389
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w22
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L392
	mov	w0, 54
	str	w0, [x19,2056]
	adrp	x0, .LANCHOR2+1236
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1236]
	str	w0, [x19,2052]
	mov	w0, 22
	str	wzr, [x19,2048]
	str	w0, [x19,2056]
	mov	w0, w21
	bl	FlashReadCmd
	b	.L393
.L392:
	mov	w0, 190
	cbnz	w7, .L441
	mov	w0, 56
.L441:
	str	w0, [x19,2056]
.L393:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L349
.L352:
	mov	w0, 120
	str	w0, [x19,2056]
	str	wzr, [x19,2052]
	mov	w0, 23
	str	wzr, [x19,2052]
	mov	w1, 25
	str	wzr, [x19,2052]
	add	x3, x20, :lo12:.LANCHOR2
	str	w0, [x19,2056]
	mov	w0, 4
	str	w0, [x19,2056]
	add	x3, x3, 1260
	str	w1, [x19,2056]
	mov	w1, 218
	str	w1, [x19,2056]
	mov	w1, 21
	str	wzr, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w1, [x19,2052]
	str	w0, [x19,2052]
	str	wzr, [x19,2052]
	b	.L396
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L443
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L445
	bl	HynixGetReadRetryDefault
	b	.L443
.L445:
	cmp	w0, 49
	bne	.L446
	adrp	x0, .LANCHOR2
	mov	w2, 64
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1236
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
	b	.L461
.L446:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L452
	cmp	w1, 33
	bne	.L447
.L452:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0,1232]
	mov	w1, 4
	b	.L462
.L447:
	cmp	w1, 67
	beq	.L453
	cmp	w1, 34
	bne	.L449
.L453:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0,1232]
	mov	w1, 5
.L462:
	strb	w1, [x0,1233]
	mov	w1, 7
	strb	w1, [x0,1234]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 1236
	add	x1, x1, 256
	mov	w2, 45
	b	.L461
.L449:
	cmp	w1, 68
	beq	.L454
	cmp	w1, 35
	bne	.L443
.L454:
	adrp	x0, .LANCHOR2
	mov	w2, 95
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1236
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 304
.L461:
	bl	ftl_memcpy
.L443:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	stp	x29, x30, [sp, -80]!
	adrp	x3, .LANCHOR0
	and	w8, w1, 255
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	add	x6, x3, :lo12:.LANCHOR0
	uxtb	w21, w0
	sbfiz	x0, x21, 4, 32
	add	x4, x6, 24
	stp	x19, x20, [sp,16]
	str	x23, [sp,48]
	add	x7, x4, x0
	and	w23, w2, 255
	lsr	w22, w2, 8
	ldr	x5, [x4,x0]
	lsr	w20, w2, 16
	ldrb	w0, [x6,128]
	ldr	x2, [x6,104]
	cmp	w0, 1
	ldrb	w19, [x7,8]
	lsr	w0, w1, 16
	lsr	w7, w1, 8
	ldrb	w2, [x2,7]
	bne	.L464
	cmp	w2, 1
	bne	.L465
	sxtw	x4, w19
	mov	w2, 38
	add	x4, x4, 8
	add	x4, x5, x4, lsl 8
	str	w2, [x4,8]
.L465:
	add	x3, x3, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	str	x1, [x29,72]
	ldrb	w2, [x3,120]
	str	w2, [x19,2056]
	str	wzr, [x19,2052]
	str	wzr, [x19,2052]
	str	w8, [x19,2052]
	str	w7, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,121]
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
	b	.L466
.L464:
	cmp	w2, 1
	bne	.L467
	sxtw	x4, w19
	mov	w2, 38
	add	x4, x4, 8
	add	x4, x5, x4, lsl 8
	str	w2, [x4,8]
.L467:
	add	x3, x3, :lo12:.LANCHOR0
	add	x19, x5, x19, lsl 8
	ldrb	w2, [x3,120]
	str	w2, [x19,2056]
	str	w8, [x19,2052]
	str	w7, [x19,2052]
	str	w0, [x19,2052]
	ldrb	w0, [x3,121]
	str	w0, [x19,2056]
	mov	w0, 48
	str	w23, [x19,2052]
	str	w22, [x19,2052]
	str	w20, [x19,2052]
	str	w0, [x19,2056]
.L466:
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
	ldr	w0, [x0,2100]
	cbz	w0, .L469
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L470
.L469:
	bl	flash_exit_slc_mode
.L470:
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,2104]
	cbz	w0, .L471
	ldrb	w0, [x19,2093]
	tbz	x0, 0, .L471
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19,2104]
.L471:
	adrp	x0, .LANCHOR0+24
	ldr	x0, [x0,#:lo12:.LANCHOR0+24]
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
	add	x2, x2, 24
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
	adrp	x1, .LANCHOR2
	movk	w2, 0xf, lsl 16
	add	x1, x1, :lo12:.LANCHOR2
	sdiv	w0, w0, w2
	cmp	w0, 250
	ble	.L480
	ldr	x0, [x1,1152]
	mov	w1, 8354
	b	.L487
.L480:
	cmp	w0, 220
	ble	.L482
	ldr	x0, [x1,1152]
	b	.L488
.L482:
	cmp	w0, 185
	ble	.L483
	ldr	x0, [x1,1152]
	mov	w1, 4226
	b	.L487
.L483:
	cmp	w0, 160
	ldr	x0, [x1,1152]
	ble	.L484
	mov	w1, 4194
	b	.L487
.L484:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L487
	cmp	w19, 99
	mov	w1, 4225
	bls	.L487
.L488:
	mov	w1, 8322
.L487:
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
	bls	.L490
	mov	w1, 8322
	cmp	w0, w1
	bne	.L491
.L490:
	adrp	x1, .LANCHOR2+1152
	ldr	x1, [x1,#:lo12:.LANCHOR2+1152]
	str	w0, [x1,4]
.L491:
	adrp	x0, .LANCHOR1+493
	ldrb	w0, [x0,#:lo12:.LANCHOR1+493]
	bl	NandcTimeCfg
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	stp	x29, x30, [sp, -32]!
	adrp	x2, .LANCHOR0
	add	x2, x2, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x1, x2, 24
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x2,32]
	str	x0, [x2,24]
	mov	w2, 1
	str	w2, [x1,24]
	mov	w2, 2
	str	w2, [x1,40]
	mov	w2, 3
	str	w2, [x1,56]
	add	x2, x19, :lo12:.LANCHOR2
	str	x0, [x1,16]
	str	x0, [x1,32]
	str	x0, [x1,48]
	str	x0, [x2,1152]
	ldr	w1, [x0]
	ubfx	x3, x1, 13, 1
	str	w3, [x2,2108]
	ldr	w3, [x0,352]
	and	w1, w1, 245760
	orr	w1, w1, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x2,2112]
	ldr	w3, [x0,352]
	str	w3, [x2,2116]
	cmp	w3, 2049
	bne	.L493
	mov	w3, 8
	str	w3, [x2,2112]
.L493:
	add	x19, x19, :lo12:.LANCHOR2
	str	w1, [x0]
	ldr	x0, [x19,1152]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19,1152]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	str	wzr, [x19,2168]
	str	x0, [x19,2120]
	str	x0, [x19,2128]
	add	x0, x0, 32768
	str	wzr, [x19,2176]
	str	x0, [x19,2136]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	adrp	x4, .LANCHOR2
	add	x4, x4, :lo12:.LANCHOR2
	ldr	x5, [x4,1152]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4,1152]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4,1152]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4,1152]
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
	adrp	x1, .LANCHOR2
	uxtb	w0, w0
	add	x1, x1, :lo12:.LANCHOR2
	mov	w3, 1
	cmp	w0, 16
	ldr	x2, [x1,1152]
	str	w0, [x1,2180]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L496
.L499:
	and	w1, w1, -17
	b	.L497
.L496:
	cmp	w0, 24
	bne	.L498
	orr	w1, w1, 16
	b	.L497
.L498:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L499
.L497:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2+1221
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR2+1221]
	bl	NandcBchSel
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	ftl_flash_resume
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x21, [sp,32]
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x21, IDByte
	mov	x20, 0
	add	x21, x21, :lo12:IDByte
	ldr	x1, [x0,1152]
	ldr	w2, [x0,1160]
	str	w2, [x1]
	ldr	w2, [x0,1164]
	ldr	x1, [x0,1152]
	str	w2, [x1,4]
	ldr	w2, [x0,1168]
	ldr	x1, [x0,1152]
	str	w2, [x1,8]
	ldr	w2, [x0,1172]
	str	w2, [x1,12]
	ldr	w2, [x0,1176]
	str	w2, [x1,304]
	ldr	w2, [x0,1180]
	str	w2, [x1,308]
	ldr	w2, [x0,1184]
	str	w2, [x1,336]
	ldr	w0, [x0,1188]
	str	w0, [x1,344]
.L506:
	lsl	x0, x20, 3
	ldrb	w0, [x0,x21]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L505
	mov	w0, w20
	bl	FlashReset
.L505:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L506
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,2104]
	cbz	w0, .L507
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2093]
	bl	NandcSetMode
	ldr	w0, [x19,1176]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L507:
	adrp	x0, .LANCHOR0+104
	ldr	x0, [x0,#:lo12:.LANCHOR0+104]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
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
	ret
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
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
.L516:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L516
	add	sp, sp, 16
	ret
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	2
	.global	NandcXferStart
	.type	NandcXferStart, %function
NandcXferStart:
	stp	x29, x30, [sp, -96]!
	adrp	x6, .LANCHOR0
	ubfiz	x0, x0, 4, 8
	add	x6, x6, :lo12:.LANCHOR0
	add	x29, sp, 0
	add	x6, x6, 24
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	uxtb	w23, w1
	add	x1, x6, x0
	ldr	x21, [x6,x0]
	ubfiz	w20, w23, 1, 1
	orr	w20, w20, 8
	adrp	x19, .LANCHOR2
	ldrb	w0, [x1,8]
	mov	w1, 16
	ldr	w22, [x21,12]
	ubfx	x3, x3, 1, 7
	uxtb	w2, w2
	bfi	w22, w1, 8, 8
	and	w22, w22, -9
	bfi	w22, w0, 5, 3
	mov	w0, 1
	bfi	w20, w0, 5, 2
	add	x0, x19, :lo12:.LANCHOR2
	orr	w20, w20, 536870912
	orr	w20, w20, 1024
	ldr	w0, [x0,2112]
	bfi	w20, w3, 4, 1
	cmp	w0, 3
	bls	.L520
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w9, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w9, .L534
	cbz	x4, .L521
.L534:
	cbnz	w23, .L523
.L531:
	add	w2, w2, 1
	mov	x0, x4
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x4, .L525
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2128]
	b	.L525
.L523:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w7, 128
	mov	w3, 0
	lsr	w10, w2, 1
	mov	w6, w3
	mov	w11, -1
	ldr	w0, [x1,2180]
	cmp	w0, 25
	mov	w0, 64
	csel	w7, w0, w7, cc
.L527:
	cmp	w6, w10
	bcs	.L531
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w9, .L528
	ldrh	w8, [x5,2]
	ldr	x12, [x1,2136]
	ldrh	w13, [x5],4
	orr	w8, w13, w8, lsl 16
	str	w8, [x12,x0]
	b	.L529
.L528:
	ldr	x8, [x1,2136]
	str	w11, [x8,x0]
.L529:
	add	w6, w6, 1
	add	w3, w3, w7
	b	.L527
.L525:
	add	x19, x19, :lo12:.LANCHOR2
	ubfx	x24, x20, 22, 5
	mov	w2, w23
	str	x4, [x29,72]
	ldr	x1, [x19,2136]
	str	x1, [x19,2152]
	lsl	w1, w24, 10
	str	x0, [x19,2144]
	bl	rknand_dma_map_single
	str	w0, [x19,2160]
	ldr	x0, [x19,2152]
	lsl	w1, w24, 7
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x19,2164]
	mov	w0, 1
	str	w0, [x19,2168]
	ldr	w0, [x19,2160]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	x4, [x29,72]
	ldr	w0, [x19,2164]
	str	w0, [x21,24]
	and	x4, x4, 3
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x4, .L532
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L532:
	ldr	w0, [x29,88]
	cmp	w23, wzr
	cset	w1, eq
	orr	w0, w0, 4
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 1, 1
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 1
	str	w0, [x29,88]
.L521:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L520:
	str	w22, [x21,12]
	str	w20, [x21,8]
	orr	w20, w20, 4
	str	w20, [x21,8]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	add	x1, x1, 24
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	adrp	x20, .LANCHOR2
	ldr	x19, [x1,x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2112]
	cmp	w0, 3
	bls	.L569
	ldr	w0, [x19,16]
	tbz	x0, 2, .L569
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L540
	adrp	x22, .LC4
	adrp	x23, .LC5
	mov	w21, 0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
.L541:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L549
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2112]
	cmp	w0, 5
	bhi	.L542
.L545:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L544
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
	b	.L544
.L542:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L545
	ldr	w0, [x29,72]
	tbz	x0, 17, .L545
.L549:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 2128
	ldr	w0, [x19,40]
	cbz	w0, .L550
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
	b	.L550
.L544:
	mov	x0, 1
	mov	x1, 5
	bl	usleep_range
	b	.L541
.L540:
	adrp	x22, .LC6
	adrp	x23, .LC5
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
.L551:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L577
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L552
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
.L552:
	mov	x0, 1
	mov	x1, 5
	bl	usleep_range
	b	.L551
.L577:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2176]
	cbz	w0, .L554
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L554:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 2128
	ldr	w0, [x21,40]
	cbz	w0, .L555
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
.L555:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2176]
	cbz	w0, .L550
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L550:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20,2168]
	b	.L538
.L569:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L569
.L538:
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
	bne	.L579
	cbz	x3, .L580
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L580:
	cbz	x20, .L578
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
	b	.L578
.L579:
	cbz	x3, .L583
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L583:
	cbz	x20, .L578
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
.L578:
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
	ldr	x19, [x0,24]
	and	x0, x3, 63
	uxtb	w20, w1
	uxtb	w23, w2
	mov	x25, x3
	mov	x21, x4
	cbnz	x0, .L595
	cbnz	x4, .L596
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x21, x29, 112
	bl	ftl_memset
.L596:
	mov	w0, w24
	mov	w1, w20
	mov	w2, w23
	mov	w3, 0
	mov	x4, x25
	mov	x5, x21
	mov	w22, 0
	bl	NandcXferStart
	mov	w0, w24
	mov	w1, w20
	bl	NandcXferComp
	cbnz	w20, .L597
	adrp	x0, .LANCHOR2
	ubfx	x3, x23, 1, 7
	add	x2, x0, :lo12:.LANCHOR2
	mov	w4, 128
	add	x3, x21, x3, lsl 2
	ldr	w1, [x2,2180]
	cmp	w1, 25
	mov	w1, 64
	csel	w4, w1, w4, cc
	mov	w1, w20
.L599:
	cmp	x21, x3
	add	w5, w1, w4
	beq	.L641
	ldr	x6, [x2,2136]
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
	b	.L599
.L641:
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w23, w23, 2
	ldr	w4, [x0,2180]
	ldr	w5, [x0,2112]
	mov	w0, 0
	mov	w22, w0
.L601:
	cmp	w0, w23
	bcs	.L597
	cbz	w4, .L597
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L625
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L625
	cmp	w5, 5
	bls	.L603
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
	bls	.L604
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L605
.L604:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L640
.L603:
	cmp	w5, 3
	bls	.L605
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
	bls	.L606
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L640
.L606:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L640:
	orr	w3, w1, w3, lsl 5
.L605:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L602
.L625:
	mov	w22, -1
.L602:
	add	w0, w0, 1
	b	.L601
.L597:
	str	wzr, [x19,16]
	b	.L608
.L595:
	cmp	w20, 1
	bne	.L639
	mov	w22, 0
	mov	w27, 2
.L609:
	cmp	w22, w23
	bcs	.L642
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L611
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L611:
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
	mov	w1, 1
	bl	NandcXferComp
	b	.L609
.L642:
	mov	w22, 0
	b	.L608
.L639:
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
.L614:
	cmp	w26, w23
	bcs	.L608
	mov	w0, w24
	mov	w1, w20
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L615
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L615:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L630
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L616
.L630:
	mov	w22, -1
.L616:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L617
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L617:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L614
.L608:
	cbnz	w20, .L620
	adrp	x0, .LANCHOR2+2112
	ldr	w0, [x0,#:lo12:.LANCHOR2+2112]
	cmp	w0, 5
	bls	.L620
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L620
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L620:
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
	adrp	x0, .LANCHOR1+481
	str	x21, [sp,32]
	mov	x21, x2
	ldrb	w20, [x0,#:lo12:.LANCHOR1+481]
	cbnz	w19, .L644
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,89]
	ldr	w0, [x0,92]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L644:
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
	stp	x23, x24, [sp,48]
	uxtb	w24, w0
	adrp	x0, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x20, x0, :lo12:.LANCHOR2
	mov	x19, x0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w26, w1
	mov	w0, 12
	ldr	x1, [x20,1152]
	mov	w22, 6
	mov	x25, x2
	mov	x23, x3
	mov	w27, w4
	mov	w21, 1024
	ldr	w1, [x1,304]
	str	w1, [x29,124]
	ldr	w1, [x20,2112]
	cmp	w1, 8
	csel	w22, w22, w0, cc
	cbz	w4, .L647
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	mov	w0, w24
	bl	FlashReset
	mov	w1, w26
	mov	x2, x25
	mov	x3, x23
	mov	w0, w24
	bl	FlashReadRawPage
	mov	w21, w0
	ldrb	w0, [x20,2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2093]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L648
.L657:
	mov	w21, -1
	b	.L649
.L648:
	adrp	x0, .LC7
	mov	w1, w26
	add	x0, x0, :lo12:.LC7
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L650
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, x24, sxtw 4
	ldr	x0, [x0,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L650:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2184]
	add	w1, w1, 1
	str	w1, [x0,2184]
	cmp	w1, 2047
	bls	.L649
	mov	x23, 0
	str	wzr, [x0,2184]
	mov	x25, x23
.L647:
	mov	w19, 0
	mov	w28, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L655:
	mov	w0, w22
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	NandcSetDdrPara
	mov	w1, w26
	mov	w0, w24
	mov	x2, x25
	mov	x3, x23
	bl	FlashReadRawPage
	add	w1, w21, 1
	ldr	x6, [x29,112]
	cmp	w0, w1
	ldr	x5, [x29,104]
	bhi	.L651
	cmp	w0, 2
	bhi	.L661
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L661
	sub	w19, w22, w20
	mov	w21, w0
	mov	w28, 0
	b	.L653
.L651:
	cmp	w6, w20
	bcs	.L662
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L663
	mov	w6, w20
	b	.L662
.L661:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w28, 0
	mov	x25, x23
	b	.L652
.L662:
	mov	w20, 0
.L652:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L655
.L653:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L654
.L663:
	mov	w19, w5
.L654:
	cbz	w19, .L656
	adrp	x0, .LC8
	mov	w1, w19
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L656:
	cbz	w28, .L649
	adrp	x0, .LC9
	mov	w1, w24
	add	x0, x0, :lo12:.LC9
	mov	w2, w26
	bl	printk
	cbz	w27, .L657
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L649:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w0
	mov	w0, w20
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w22, w1
	mov	x25, x2
	mov	x24, x3
	bl	FlashReadRawPage
	cmn	w0, #1
	mov	w19, w0
	adrp	x21, .LANCHOR2
	bne	.L673
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	ldrb	w26, [x23,96]
	cbnz	w26, .L674
.L676:
	add	x23, x21, :lo12:.LANCHOR2
	ldrb	w0, [x23,2104]
	cbz	w0, .L673
	b	.L694
.L674:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	strb	wzr, [x23,96]
	bl	FlashReadRawPage
	strb	w26, [x23,96]
	cmn	w0, #1
	beq	.L676
	mov	w19, w0
	b	.L673
.L694:
	ldr	x0, [x23,1152]
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	mov	w4, 1
	ldr	w26, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L677
	ldrb	w0, [x23,1221]
	cmp	w19, w0, lsr 1
	bls	.L673
.L677:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
.L673:
	add	x21, x21, :lo12:.LANCHOR2
	cmn	w19, #1
	ldr	x4, [x21,2192]
	bne	.L678
	cbz	x4, .L678
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
	bne	.L678
	ldrb	w0, [x21,1220]
	cbz	w0, .L678
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,2192]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L678:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 80
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
	ldrb	w0, [x22,2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2093]
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
	beq	.L699
	cmn	w22, #1
	bne	.L696
.L699:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20,2093]
	tbz	x0, 0, .L696
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2104]
	b	.L698
.L696:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,2104]
.L698:
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
	adrp	x20, .LANCHOR0
	adrp	x19, .LANCHOR2
	strb	w0, [x29,104]
	mov	w0, 40
	strb	w0, [x29,105]
	mov	w0, 24
	strb	w0, [x29,106]
	mov	w0, 16
	strb	w0, [x29,107]
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR1
	stp	x27, x28, [sp,80]
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	ldr	w24, [x0,92]
	mov	w23, 4
	ldr	x0, [x20,#:lo12:.LANCHOR0]
	mov	w26, -1
	str	x0, [x19,2200]
	add	x22, x22, 472
	mov	w0, 0
	add	x25, x19, 1232
	str	wzr, [x19,2208]
	bl	flash_enter_slc_mode
.L707:
	add	w28, w21, 1
	mov	x27, 0
.L709:
	add	x0, x29, 104
	ldrb	w0, [x27,x0]
	bl	FlashBchSel
	ldr	x2, [x20,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w21
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L708
	ldr	x2, [x20,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w28
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L708
	add	x27, x27, 1
	cmp	x27, 4
	beq	.L710
	b	.L709
.L711:
	mov	w1, 2036
	add	x0, x27, 12
	mov	w26, -1
	bl	js_hash
	ldr	w1, [x27,8]
	cmp	w1, w0
	beq	.L722
.L710:
	subs	w23, w23, #1
	add	w21, w21, w24
	bne	.L707
	mov	w0, w23
.L721:
	bl	flash_exit_slc_mode
	mov	w0, w26
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L708:
	ldr	x27, [x19,2200]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x27]
	cmp	w1, w0
	bne	.L710
	cbnz	w26, .L711
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w0, [x20,92]
	udiv	w21, w21, w0
	mov	w0, w26
	add	w21, w21, 1
	str	w21, [x20,8]
	b	.L721
.L722:
	add	x1, x27, 160
	mov	w2, 32
	mov	x0, x22
	add	x26, x20, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldr	x1, [x19,2200]
	mov	w2, 32
	add	x0, x26, 112
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,2200]
	mov	w2, 852
	mov	x0, x25
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x22,10]
	bl	FlashBlockAlignInit
	str	w21, [x19,2208]
	ldr	x0, [x19,2200]
	ldr	w1, [x0,1076]
	strb	w1, [x19,2104]
	ldr	w1, [x26,92]
	udiv	w1, w21, w1
	add	w1, w1, 1
	cmp	w1, 1
	bhi	.L720
	mov	w1, 2
.L720:
	ldrh	w0, [x0,14]
	str	w1, [x26,8]
	mov	w26, 0
	strb	w0, [x19,2212]
	b	.L710
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
	adrp	x0, .LANCHOR0
	sbfiz	x1, x23, 4, 32
	add	x0, x0, :lo12:.LANCHOR0
	mov	w22, 0
	add	x0, x0, 24
	add	x2, x0, x1
	ldr	x24, [x0,x1]
	ldrb	w0, [x2,8]
	str	x0, [x29,136]
	add	x19, x0, 8
	mov	x28, x0
	ldrb	w0, [x20,#:lo12:g_retryMode]
	add	x19, x24, x19, lsl 8
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	bls	.L724
	adrp	x0, .LANCHOR2+2104
	ldrb	w0, [x0,#:lo12:.LANCHOR2+2104]
	cbz	w0, .L725
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L725:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L724:
	adrp	x0, g_maxRetryCount
	mov	w21, 1
	add	x0, x0, :lo12:g_maxRetryCount
	str	x0, [x29,120]
	ldr	x0, [x29,136]
	mov	w25, -1
	add	x0, x0, 8
	lsl	x0, x0, 8
	str	x0, [x29,112]
	ubfiz	x0, x28, 8, 8
	str	x0, [x29,104]
.L726:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0,#:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L751
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, w21
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L727
	bl	SandiskSetRRPara
	b	.L728
.L727:
	bl	ToshibaSetRRPara
.L728:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L729
	ldr	x0, [x29,120]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L729
	ldr	x0, [x29,112]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L729:
	ldr	x0, [x29,104]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L730
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
	b	.L731
.L730:
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L731:
	cmn	w28, #1
	beq	.L732
	adrp	x0, .LANCHOR2+1221
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L734
	mov	x26, 0
	mov	x27, x26
.L732:
	add	w21, w21, 1
	b	.L726
.L751:
	mov	w28, w25
.L734:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, 0
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L736
	bl	SandiskSetRRPara
	b	.L737
.L736:
	bl	ToshibaSetRRPara
.L737:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	adrp	x0, .LANCHOR2+1221
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L738
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L738:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L739
	mov	w0, 4
	bl	NandcSetDdrMode
.L739:
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
	uxtb	w21, w0
	str	x27, [sp,80]
	mov	w0, w21
	mov	x22, x3
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w24, w1
	mov	x23, x2
	bl	NandcWaitFlashReady
	adrp	x26, g_maxRetryCount
	adrp	x4, .LANCHOR0
	sbfiz	x0, x21, 4, 32
	add	x4, x4, :lo12:.LANCHOR0
	mov	w25, 1
	add	x4, x4, 24
	mov	w19, -1
	add	x1, x4, x0
	add	x26, x26, :lo12:g_maxRetryCount
	adrp	x27, .LANCHOR2
	ldr	x0, [x4,x0]
	ldrb	w20, [x1,8]
	add	x20, x20, 8
	add	x20, x0, x20, lsl 8
.L753:
	ldrb	w0, [x26]
	add	w0, w0, 1
	cmp	w25, w0
	bcs	.L756
	mov	x0, x20
	mov	w1, w25
	bl	SamsungSetRRPara
	mov	w0, w21
	mov	w1, w24
	mov	x2, x23
	mov	x3, x22
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L754
	add	x1, x27, :lo12:.LANCHOR2
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1221]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L759
	mov	x22, 0
	mov	x23, x22
.L754:
	add	w25, w25, 1
	b	.L753
.L759:
	mov	w19, w0
.L756:
	mov	x0, x20
	mov	w1, 0
	bl	SamsungSetRRPara
	adrp	x0, .LANCHOR2+1221
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1221]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L758
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L758:
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
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	adrp	x0, .LANCHOR2
	stp	x27, x28, [sp,80]
	mov	w28, w1
	add	x1, x0, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	str	x0, [x29,152]
	mov	x25, x2
	ldrb	w19, [x1,1221]
	mov	x22, x3
	ldrb	w1, [x1,1220]
	cbnz	w1, .L766
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L767
.L766:
	mov	w0, 3
	sdiv	w19, w19, w0
.L767:
	adrp	x26, g_maxRetryCount
	adrp	x27, .LC11
	adrp	x20, .LANCHOR0
	add	x0, x26, :lo12:g_maxRetryCount
	mov	w24, 0
	str	x0, [x29,144]
	add	x27, x27, :lo12:.LC11
	add	x20, x20, :lo12:.LANCHOR0
	add	x20, x20, 24
	add	x20, x20, x21, sxtw 4
.L778:
	mov	w0, w21
	mov	w23, 0
	mov	w26, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x20,8]
	mov	w9, 239
	ldr	x6, [x20]
	mov	w10, 137
	lsl	x8, x4, 8
.L768:
	ldr	x0, [x29,144]
	ldrb	w0, [x0]
	cmp	w23, w0
	bcs	.L771
	add	x2, x6, x8
	mov	x0, 1000
	str	x4, [x29,96]
	str	x8, [x29,104]
	str	w9, [x2,2056]
	str	w10, [x2,2052]
	str	x6, [x29,112]
	str	x9, [x29,120]
	str	x10, [x29,128]
	str	x2, [x29,136]
	bl	__const_udelay
	ldr	x2, [x29,136]
	add	w7, w23, 1
	mov	w0, w21
	mov	w1, w28
	mov	x3, x22
	str	x7, [x29,136]
	str	w7, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	str	wzr, [x2,2048]
	mov	x2, x25
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x7, [x29,136]
	ldr	x10, [x29,128]
	ldr	x9, [x29,120]
	ldr	x6, [x29,112]
	ldr	x8, [x29,104]
	ldr	x4, [x29,96]
	beq	.L769
	cmn	w26, #1
	csel	w26, w26, w0, ne
	cmp	w0, w19
	bcc	.L780
	mov	x22, 0
	mov	x25, x22
.L769:
	mov	w23, w7
	b	.L768
.L780:
	mov	x22, 0
	mov	w26, w0
	mov	x25, x22
.L771:
	add	x1, x6, x4, lsl 8
	mov	w0, 239
	str	x1, [x29,136]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldr	x1, [x29,136]
	cmp	w26, w19
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	bcc	.L773
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L773:
	cmn	w26, #1
	cset	w6, eq
	cbnz	w6, .L782
	cmp	w26, 256
	cset	w1, eq
	cbz	w1, .L774
.L782:
	mov	x0, x27
	mov	w1, w23
	mov	w2, w28
	mov	w3, w23
	mov	w4, w26
	str	x6, [x29,136]
	bl	printk
	cbnz	w24, .L776
	ldr	x6, [x29,136]
	cbz	w6, .L787
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L787
	mov	w0, w21
	mov	w1, 3
	mov	w24, 1
	bl	micron_auto_read_calibration_config
	b	.L778
.L776:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
	b	.L787
.L774:
	cbz	w24, .L787
	mov	w0, w21
	mov	w26, 256
	bl	micron_auto_read_calibration_config
.L787:
	mov	w0, w26
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR2
	adrp	x22, .LANCHOR0
	stp	x23, x24, [sp,48]
	uxtb	x23, w0
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 1232
	mov	x24, x23
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w28, w1
	add	x1, x0, x23
	ldrb	w26, [x0,2]
	add	x0, x22, :lo12:.LANCHOR0
	mov	x27, x2
	mov	x25, x3
	ldrb	w19, [x1,12]
	ldr	x0, [x0,104]
	ldrb	w0, [x0,19]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L796
	ldrb	w19, [x1,20]
.L796:
	mov	w0, w24
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x5, x21, :lo12:.LANCHOR2
	mov	w4, 0
	add	x6, x5, 1236
.L797:
	cmp	w4, w26
	bcs	.L801
	add	w19, w19, 1
	ldrb	w1, [x5,1233]
	mov	x2, x6
	mov	w0, w24
	uxtb	w19, w19
	str	x4, [x29,104]
	cmp	w19, w26
	str	x5, [x29,112]
	csel	w19, w19, wzr, cc
	str	x6, [x29,120]
	mov	w3, w19
	bl	HynixSetRRPara
	mov	w0, w24
	mov	w1, w28
	mov	x2, x27
	mov	x3, x25
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x6, [x29,120]
	ldr	x5, [x29,112]
	ldr	x4, [x29,104]
	beq	.L799
	ldrb	w1, [x5,1221]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L806
	mov	x25, 0
	mov	x27, x25
.L799:
	add	w4, w4, 1
	b	.L797
.L806:
	mov	w20, w0
.L801:
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,104]
	ldrb	w0, [x0,19]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x23, x0, x23
	bhi	.L803
	strb	w19, [x23,1252]
	b	.L804
.L803:
	strb	w19, [x23,1244]
.L804:
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w0, [x21,1221]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L805
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L805:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	uxtb	w22, w0
	adrp	x21, .LANCHOR3
	mov	w0, w22
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	mov	w23, w1
	mov	x24, x2
	stp	x27, x28, [sp,80]
	mov	x26, x3
	bl	NandcWaitFlashReady
	adrp	x0, .LANCHOR0
	sbfiz	x1, x22, 4, 32
	add	x0, x0, :lo12:.LANCHOR0
	adrp	x25, .LANCHOR2
	add	x0, x0, 24
	add	x2, x0, x1
	ldr	x5, [x0,x1]
	add	x0, x25, :lo12:.LANCHOR2
	ldrb	w19, [x2,8]
	ldrb	w0, [x0,2092]
	cbnz	w0, .L813
	lsl	x19, x19, 8
	mov	x4, 0
	mov	w20, -1
	add	x27, x5, x19
	mov	w6, 239
	mov	w7, 141
	add	x21, x21, :lo12:.LANCHOR3
.L817:
	str	w6, [x27,2056]
	add	x0, x21, x4
	str	w7, [x27,2052]
	mov	w1, w23
	mov	x2, x24
	mov	x3, x26
	ldrsb	w0, [x0,1]
	add	w28, w4, 1
	str	w0, [x27,2048]
	mov	w0, w22
	str	wzr, [x27,2048]
	str	wzr, [x27,2048]
	str	wzr, [x27,2048]
	str	x5, [x29,96]
	str	x6, [x29,104]
	str	x7, [x29,112]
	str	x4, [x29,120]
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x4, [x29,120]
	ldr	x7, [x29,112]
	ldr	x6, [x29,104]
	ldr	x5, [x29,96]
	beq	.L814
	add	x1, x25, :lo12:.LANCHOR2
	cmn	w20, #1
	csel	w20, w20, w0, ne
	ldrb	w1, [x1,1221]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L826
	mov	x26, 0
	mov	x24, x26
.L814:
	add	x4, x4, 1
	cmp	x4, 25
	bne	.L817
	mov	w28, 26
	b	.L816
.L826:
	mov	w20, w0
.L816:
	add	x3, x5, x19
	mov	w0, 239
	str	w0, [x3,2056]
	mov	w0, 141
	b	.L842
.L813:
	add	x21, x21, :lo12:.LANCHOR3
	lsl	x19, x19, 8
	add	x21, x21, 32
	mov	w20, -1
	mov	w28, 1
	add	x27, x5, x19
	mov	w4, 239
	mov	w6, 137
.L822:
	str	w4, [x27,2056]
	mov	w1, w23
	str	w6, [x27,2052]
	mov	x2, x24
	mov	x3, x26
	str	x5, [x29,104]
	ldrb	w0, [x21,4]
	str	w0, [x27,2048]
	str	x4, [x29,112]
	ldrb	w0, [x21,5]
	str	w0, [x27,2048]
	str	x6, [x29,120]
	ldrb	w0, [x21,6]
	str	w0, [x27,2048]
	ldrb	w0, [x21,7]
	str	w0, [x27,2048]
	mov	w0, w22
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x6, [x29,120]
	ldr	x4, [x29,112]
	ldr	x5, [x29,104]
	beq	.L819
	add	x1, x25, :lo12:.LANCHOR2
	cmn	w20, #1
	csel	w20, w20, w0, ne
	ldrb	w1, [x1,1221]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L827
	mov	x26, 0
	mov	x24, x26
.L819:
	add	w28, w28, 1
	add	x21, x21, 4
	cmp	w28, 26
	bne	.L822
	b	.L821
.L827:
	mov	w20, w0
.L821:
	add	x3, x5, x19
	mov	w0, 239
	str	w0, [x3,2056]
	mov	w0, 137
.L842:
	str	w0, [x3,2052]
	add	x25, x25, :lo12:.LANCHOR2
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	str	wzr, [x3,2048]
	ldrb	w0, [x25,1221]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L823
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L823:
	cmn	w20, #1
	beq	.L828
	cmp	w20, 256
	bne	.L824
.L828:
	adrp	x0, .LC12
	mov	w1, w28
	add	x0, x0, :lo12:.LC12
	mov	w2, w23
	mov	w3, w28
	mov	w4, w20
	bl	printk
.L824:
	mov	w0, w22
	bl	NandcWaitFlashReady
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	FlashProgPage
	.type	FlashProgPage, %function
FlashProgPage:
	stp	x29, x30, [sp, -64]!
	mov	x4, x3
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w19, w0
	adrp	x0, .LANCHOR1+481
	stp	x21, x22, [sp,32]
	mov	w20, w1
	mov	x22, x2
	ldrb	w21, [x0,#:lo12:.LANCHOR1+481]
	cbnz	w19, .L844
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,89]
	ldr	w2, [x0,92]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L844
	ldrb	w0, [x0,88]
	cmp	w0, wzr
	mov	w0, 4
	csel	w21, w21, w0, eq
.L844:
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
	adrp	x21, .LANCHOR0
	adrp	x24, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x24, :lo12:.LANCHOR2
	add	x20, x21, :lo12:.LANCHOR0
	stp	x25, x26, [sp,64]
	ldr	x0, [x21,#:lo12:.LANCHOR0]
	mov	w23, 0
	str	x0, [x19,2200]
	mov	w22, w23
	ldrb	w0, [x19,2213]
	bl	FlashBchSel
	ldr	x0, [x21,#:lo12:.LANCHOR0]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,2200]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,2200]
	ldrb	w1, [x20,3216]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,89]
	strh	w1, [x0,-2]
	ldrb	w1, [x19,2104]
	str	w1, [x0,1060]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	bl	ftl_memcpy
	ldr	x0, [x19,2200]
	add	x1, x20, 3220
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,2200]
	add	x1, x20, 3228
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,2200]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,2200]
	add	x1, x20, 112
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,2200]
	add	x1, x19, 1232
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,2200]
	mov	w1, 2036
	add	x0, x20, 12
	bl	js_hash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,2216]
	str	x0, [x19,2200]
	mov	w0, 0
	mov	x19, x21
	bl	flash_enter_slc_mode
.L852:
	add	x21, x19, :lo12:.LANCHOR0
	mov	w0, 0
	mov	w2, 0
	ldr	w1, [x21,92]
	mul	w1, w22, w1
	bl	FlashEraseBlock
	add	x0, x24, :lo12:.LANCHOR2
	ldrb	w20, [x0,1220]
	cbz	w20, .L847
	mov	w20, 0
.L848:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	x3, 0
	ldr	w1, [x0,92]
	mov	w0, 0
	madd	w1, w22, w1, w20
	add	w20, w20, 1
	bl	FlashProgPage
	cmp	w20, 10
	bne	.L848
	b	.L849
.L847:
	ldr	w1, [x21,92]
	mov	x3, 0
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, w20
	mul	w1, w22, w1
	bl	FlashProgPage
	ldr	w1, [x21,92]
	mov	w0, w20
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	x3, 0
	mul	w1, w22, w1
	add	w1, w1, 1
	bl	FlashProgPage
.L849:
	add	x21, x19, :lo12:.LANCHOR0
	add	x20, x24, :lo12:.LANCHOR2
	mov	w0, 0
	mov	x3, 0
	add	w26, w22, 1
	ldr	w1, [x21,92]
	ldr	x2, [x20,2216]
	mul	w1, w22, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L850
	ldr	x25, [x20,2200]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L850
	mov	w1, 2036
	add	x0, x25, 12
	bl	js_hash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L850
	ldr	w0, [x21,92]
	cmp	w23, 1
	str	w26, [x21,8]
	mul	w22, w22, w0
	str	w22, [x20,2208]
	beq	.L853
	mov	w23, 1
.L850:
	cmp	w26, 4
	mov	w22, w26
	bne	.L852
	b	.L851
.L853:
	mov	w23, 2
.L851:
	mov	w0, 0
	bl	flash_exit_slc_mode
	cmp	w23, wzr
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	mov	x25, x0
	mov	w0, 60
	strb	w0, [x29,120]
	mov	w0, 40
	strb	w0, [x29,121]
	mov	w0, 24
	stp	x21, x22, [sp,32]
	strb	w0, [x29,122]
	adrp	x21, .LANCHOR2
	mov	w0, 16
	strb	w0, [x29,123]
	add	x0, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	ldrb	w28, [x0,1221]
	ldr	w0, [x0,2100]
	cbz	w0, .L860
	mov	w0, 0
	bl	flash_enter_slc_mode
.L860:
	adrp	x26, .LANCHOR0
	adrp	x24, .LC13
	mov	x0, x25
	mov	w1, 0
	mov	w2, 2048
	mov	w23, -1
	mov	w20, 2
	mov	x27, x26
	add	x24, x24, :lo12:.LC13
	bl	ftl_memset
.L861:
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,89]
	cmp	w20, w0
	bcs	.L865
	mov	x22, 0
.L863:
	add	x0, x29, 120
	add	x19, x27, :lo12:.LANCHOR0
	ldrb	w4, [x22,x0]
	str	x4, [x29,104]
	mov	w0, w4
	bl	FlashBchSel
	ldr	w1, [x19,92]
	mov	w0, 0
	ldr	x2, [x27,#:lo12:.LANCHOR0]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	ldr	x4, [x29,104]
	bne	.L862
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L863
	b	.L864
.L862:
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L864
	mov	w1, w4
	mov	x0, x24
	bl	printk
	ldr	x1, [x19]
	mov	x0, x25
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x19]
	ldr	w0, [x0,512]
	strb	w0, [x19,89]
	ldr	w0, [x19,8]
	cmp	w0, w20
	bls	.L868
	mov	w23, 0
	str	w20, [x19,8]
	bl	FlashSavePhyInfo
.L864:
	add	w20, w20, 1
	b	.L861
.L868:
	mov	w23, 0
.L865:
	mov	w0, w28
	add	x21, x21, :lo12:.LANCHOR2
	bl	FlashBchSel
	ldr	w0, [x21,2100]
	cbz	w0, .L872
	mov	w0, 0
	bl	flash_exit_slc_mode
.L872:
	mov	w0, w23
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	adrp	x20, .LANCHOR0
	mov	x23, x0
	mov	w0, 32768
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	bl	ftl_malloc
	add	x21, x19, :lo12:.LANCHOR2
	str	x0, [x20,#:lo12:.LANCHOR0]
	mov	w0, 32768
	add	x22, x20, :lo12:.LANCHOR0
	adrp	x24, .LC14
	bl	ftl_malloc
	str	x0, [x21,2216]
	mov	w0, 4096
	add	x22, x22, 24
	mov	w27, 1
	mov	w28, 4
	bl	ftl_malloc
	str	x0, [x21,2224]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x21,2232]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x21,2240]
	mov	w0, 128
	str	w0, [x22,68]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x21,2213]
	mov	x0, x23
	strb	w1, [x22,65]
	mov	w23, 0
	strb	w1, [x21,2212]
	str	wzr, [x22,-16]
	strb	wzr, [x21,2104]
	str	wzr, [x21,2184]
	strb	wzr, [x22,64]
	strb	wzr, [x21,2248]
	bl	NandcInit
	adrp	x21, IDByte
	add	x0, x24, :lo12:.LC14
	add	x26, x21, :lo12:IDByte
	str	x0, [x29,104]
.L884:
	ldrb	w2, [x22,8]
	uxtb	w25, w23
	mov	w0, w25
	str	x2, [x29,96]
	ldr	x24, [x22]
	bl	FlashReset
	mov	w0, w25
	bl	NandcFlashCs
	ldr	x2, [x29,96]
	mov	w0, 144
	ubfiz	x2, x2, 8, 8
	add	x24, x24, x2
	str	w0, [x24,2056]
	mov	x0, 1000
	str	wzr, [x24,2052]
	bl	__const_udelay
	ldr	w0, [x24,2048]
	uxtb	w0, w0
	strb	w0, [x26]
	cmp	w0, 44
	ldr	w2, [x24,2048]
	strb	w2, [x26,1]
	ldr	w2, [x24,2048]
	strb	w2, [x26,2]
	ldr	w2, [x24,2048]
	strb	w2, [x26,3]
	ldr	w2, [x24,2048]
	strb	w2, [x26,4]
	ldr	w2, [x24,2048]
	strb	w2, [x26,5]
	bne	.L879
	mov	w0, 239
	str	w0, [x24,2056]
	str	w27, [x24,2052]
	mov	x0, 1000
	bl	__const_udelay
	str	w28, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
.L879:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x26]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L880
	ldr	x0, [x29,104]
	add	w1, w23, 1
	ldrb	w3, [x26,1]
	ldrb	w4, [x26,2]
	ldrb	w5, [x26,3]
	ldrb	w6, [x26,4]
	ldrb	w7, [x26,5]
	bl	printk
.L880:
	cbnz	w23, .L881
	ldrb	w0, [x21,#:lo12:IDByte]
	add	x1, x21, :lo12:IDByte
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L936
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L936
	bl	FlashCs123Init
.L881:
	ldrb	w0, [x26]
	cmp	w0, 181
	bne	.L883
	mov	w0, 44
	strb	w0, [x26]
.L883:
	add	w23, w23, 1
	add	x22, x22, 16
	cmp	w23, 4
	add	x26, x26, 8
	bne	.L884
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 173
	beq	.L885
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2108]
	bl	NandcSetDdrMode
.L885:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 852
	add	x0, x22, 1232
	bl	ftl_memset
	add	x2, x20, :lo12:.LANCHOR0
	adrp	x1, .LANCHOR1
	add	x0, x1, :lo12:.LANCHOR1
	ldr	w4, [x22,2116]
	add	x0, x0, 472
	strb	wzr, [x2,96]
	str	x0, [x2,104]
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L886
	ldrb	w0, [x0,19]
	cmp	w0, 50
	beq	.L886
	mov	w0, 1
	str	w0, [x22,2100]
.L886:
	add	x3, x21, :lo12:IDByte
	ldrb	w0, [x3,1]
	cmp	w0, 218
	cset	w6, eq
	cmp	w0, 241
	cset	w5, eq
	cmp	w0, 161
	cset	w2, eq
	orr	w2, w5, w2
	orr	w2, w6, w2
	cbnz	w2, .L887
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L887
	cmp	w0, 220
	bne	.L888
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L888
.L887:
	add	x2, x20, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR2
	mov	w7, 16
	mov	w5, 1
	add	x8, x21, :lo12:IDByte
	strb	w7, [x2,89]
	strb	w5, [x2,88]
	add	x2, x1, :lo12:.LANCHOR1
	strb	w7, [x3,2213]
	ldrb	w7, [x21,#:lo12:IDByte]
	strb	w7, [x2,3417]
	strb	w0, [x2,3418]
	cmp	w7, 152
	bne	.L890
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L891
	mov	w2, 24
	strb	w2, [x3,2213]
.L890:
	cmp	w4, 2049
	beq	.L939
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L893
.L939:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w3, 16
	strb	w3, [x2,2213]
.L893:
	cbz	w6, .L895
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3430]
	mov	w2, -38
	b	.L984
.L895:
	cmp	w0, 220
	bne	.L897
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3430]
	mov	w2, -36
.L984:
	strb	w2, [x0,3418]
	b	.L896
.L897:
	cmp	w0, 211
	bne	.L896
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3430]
	mov	w2, 2
	strb	w2, [x0,3429]
.L896:
	add	x22, x1, :lo12:.LANCHOR1
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x22, 3288
	mov	w2, 32
	add	x0, x0, 112
	bl	ftl_memcpy
	add	x0, x22, 472
	add	x1, x22, 3416
	mov	w2, 32
	bl	ftl_memcpy
.L888:
	add	x22, x20, :lo12:.LANCHOR0
	ldrb	w0, [x22,88]
	cbnz	w0, .L898
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L900
	ldr	x0, [x22,104]
	add	x22, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x22,2093]
	tbnz	x1, 0, .L900
	mov	w1, 1
	strb	w1, [x22,2104]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2093]
	bl	NandcSetMode
.L900:
	add	x0, x20, :lo12:.LANCHOR0
	add	x22, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,104]
	ldrb	w0, [x0,26]
	strb	w0, [x22,1220]
	bl	FlashLoadPhyInfo
	cbz	w0, .L898
	ldr	w0, [x22,2108]
	cbz	w0, .L903
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L980
.L903:
	ldrb	w0, [x22,2093]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2093]
.L980:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L898
	mov	w0, 1
	add	x22, x20, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x22,104]
	adrp	x0, .LC15
	add	x0, x0, :lo12:.LC15
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L954
	bl	FlashDieInfoInit
	ldr	x0, [x22,104]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x22,104]
	ldrh	w1, [x22,3452]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L905
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L906
.L905:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L906:
	add	x22, x19, :lo12:.LANCHOR2
	ldrb	w0, [x22,2093]
	tst	w0, 6
	beq	.L907
	bl	FlashSavePhyInfo
	mov	w0, 0
	bl	flash_enter_slc_mode
	ldr	w1, [x22,2208]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L907:
	bl	FlashSavePhyInfo
.L898:
	add	x23, x20, :lo12:.LANCHOR0
	add	x22, x19, :lo12:.LANCHOR2
	ldr	x2, [x23,104]
	str	xzr, [x22,2192]
	ldrb	w0, [x2,26]
	strb	w0, [x22,1220]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x23,96]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,2249]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,2094]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x22,2093]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x23,104]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L909
	adrp	x1, g_retryMode
	ldrb	w0, [x0,19]
	ldrb	w2, [x22,1233]
	ldrb	w3, [x22,1234]
	strb	w0, [x1,#:lo12:g_retryMode]
	adrp	x1, g_maxRegNum
	strb	w2, [x1,#:lo12:g_maxRegNum]
	adrp	x2, g_maxRetryCount
	strb	w3, [x2,#:lo12:g_maxRetryCount]
	sub	w3, w0, #1
	uxtb	w3, w3
	cmp	w3, 7
	mov	x3, x2
	bhi	.L910
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,2192]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L940
	cbz	w2, .L911
.L940:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w3, 1
	str	w3, [x1,2176]
.L911:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR2
	beq	.L938
	cbnz	w2, .L938
	add	x1, x1, 1252
	b	.L914
.L938:
	add	x1, x1, 1260
.L914:
	mov	x2, 0
	mov	w3, w2
.L916:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L916
	cmp	w3, 27
	bls	.L909
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L909
.L910:
	sub	w4, w0, #17
	uxtb	w4, w4
	cmp	w4, 2
	bhi	.L918
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,2192]
	beq	.L919
	mov	w0, 7
	b	.L982
.L919:
	mov	w0, 15
.L982:
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	b	.L909
.L918:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L941
	cmp	w0, 33
	bne	.L920
.L941:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x2, ToshibaReadRetrial
	add	x2, x2, :lo12:ToshibaReadRetrial
	str	x2, [x0,2192]
	mov	w0, 4
	strb	w0, [x1,#:lo12:g_maxRegNum]
	mov	w0, 7
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	b	.L909
.L920:
	sub	w2, w0, #67
	sub	w4, w0, #34
	uxtb	w2, w2
	uxtb	w4, w4
	cmp	w2, 1
	cset	w2, ls
	cmp	w4, 1
	bls	.L942
	cbz	w2, .L922
.L942:
	add	x4, x19, :lo12:.LANCHOR2
	adrp	x5, ToshibaReadRetrial
	add	x5, x5, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x5, [x4,2192]
	beq	.L924
	cmp	w0, 68
	beq	.L924
	mov	w0, 7
	b	.L981
.L924:
	mov	w0, 17
.L981:
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	mov	w0, 4
	cbnz	w2, .L983
	mov	w0, 5
.L983:
	strb	w0, [x1,#:lo12:g_maxRegNum]
	b	.L909
.L922:
	cmp	w0, 49
	bne	.L927
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,2192]
	b	.L909
.L927:
	cmp	w0, 50
	bne	.L909
	adrp	x0, samsung_read_retrial
	str	w2, [x22,2100]
	add	x0, x0, :lo12:samsung_read_retrial
	str	x0, [x22,2192]
.L909:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,2116]
	cmp	w2, w1
	bne	.L928
	ldrb	w0, [x0,1220]
	cbz	w0, .L928
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,104]
	strb	wzr, [x0,18]
.L928:
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 44
	bne	.L929
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,2104]
	cbz	w1, .L929
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L930
	ldrb	w0, [x0,1220]
	cbnz	w0, .L929
.L930:
	add	x0, x19, :lo12:.LANCHOR2
	strb	wzr, [x0,2104]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L929:
	add	x1, x19, :lo12:.LANCHOR2
	ldrb	w0, [x1,2093]
	tst	w0, 6
	beq	.L931
	ldrb	w1, [x1,2104]
	cbnz	w1, .L932
	tbnz	x0, 0, .L931
.L932:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 0
	ldr	w1, [x1,2208]
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L931:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w22, 16
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x20,104]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	add	x0, x20, 3456
	bl	FlashReadIdbDataRaw
	strb	w22, [x20,89]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x20,104]
	add	x4, x21, :lo12:IDByte
	ldrb	w2, [x1,12]
	strh	w2, [x19,1200]
	ldrb	w2, [x1,7]
	str	w2, [x19,1196]
	ldrb	w2, [x4,1]
	lsl	w3, w2, w22
	orr	w2, w3, w2, lsl 8
	ldrb	w3, [x21,#:lo12:IDByte]
	orr	w2, w2, w3
	ldrb	w3, [x4,3]
	ldrh	w4, [x1,14]
	orr	w2, w2, w3, lsl 24
	str	w2, [x19,1192]
	ldrb	w2, [x20,3216]
	strh	w2, [x19,1202]
	ldrb	w2, [x1,13]
	strh	w2, [x19,1204]
	strh	w4, [x19,1206]
	ldrh	w2, [x1,10]
	strh	w2, [x19,1208]
	ldrb	w3, [x1,12]
	ldrh	w2, [x1,10]
	sdiv	w2, w2, w3
	strh	w2, [x19,1210]
	ldrb	w3, [x1,9]
	strh	w3, [x19,1212]
	ldrh	w5, [x1,10]
	ldrb	w2, [x1,9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19,1216]
	ldrb	w5, [x20,89]
	strh	w5, [x19,1218]
	uxth	w2, w2
	ldrb	w5, [x20,88]
	strh	w2, [x19,1214]
	cmp	w5, 1
	bne	.L934
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,1214]
	strb	w22, [x20,89]
	mov	w2, 8
	strh	w4, [x19,1206]
	strh	w3, [x19,1212]
	strh	w2, [x19,1218]
.L934:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L954
.L936:
	mov	w0, -2
	b	.L954
.L891:
	strb	w5, [x3,2248]
	b	.L890
.L954:
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
	uxth	w19, w2
	adrp	x20, .LANCHOR0
	adrp	x2, .LANCHOR2
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	add	x3, x2, :lo12:.LANCHOR2
	uxtb	w22, w0
	add	x0, x20, :lo12:.LANCHOR0
	mov	x21, x2
	stp	x23, x24, [sp,48]
	ldr	x0, [x0,104]
	ldrb	w4, [x3,1220]
	ldrb	w0, [x0,19]
	cbz	w4, .L986
	ldr	w2, [x3,2100]
	cbnz	w2, .L985
.L986:
	sub	w2, w0, #5
	uxtb	w2, w2
	cmp	w2, 2
	bls	.L987
	cmp	w0, 50
	beq	.L987
	sub	w2, w0, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbz	w2, .L987
	cmp	w0, 68
	bne	.L985
.L987:
	mov	w23, w1
	add	x20, x20, :lo12:.LANCHOR0
	mov	w25, 65535
.L990:
	ldr	x0, [x20,104]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L985
	add	x0, x20, 1168
	ldrh	w0, [x0,w19,sxtw 1]
	cmp	w0, w25
	bne	.L985
	add	x24, x21, :lo12:.LANCHOR2
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x24,2216]
	bl	ftl_memset
	ldr	x2, [x24,2216]
	add	w1, w19, w23
	mov	w0, w22
	add	w19, w19, 1
	mov	x3, x2
	uxth	w19, w19
	bl	FlashProgPage
	b	.L990
.L985:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldr	x25, [sp,64]
	ldp	x29, x30, [sp], 80
	ret
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	mov	w24, w1
	adrp	x23, .LANCHOR0
	adrp	x1, .LANCHOR1+481
	str	w2, [x29,116]
	stp	x19, x20, [sp,16]
	mov	x19, x0
	add	x0, x23, :lo12:.LANCHOR0
	str	x0, [x29,120]
	add	x0, x0, 3220
	stp	x25, x26, [sp,64]
	str	x0, [x29,104]
	adrp	x0, .LC16
	ldrb	w25, [x1,#:lo12:.LANCHOR1+481]
	add	x0, x0, :lo12:.LC16
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w22, 0
	str	x0, [x29,96]
.L1002:
	cmp	w22, w24
	beq	.L1066
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,3216]
	cmp	w0, w1
	bcc	.L1003
	mov	w0, -1
	str	w0, [x19]
	b	.L1004
.L1003:
	ldr	x1, [x29,104]
	mov	w23, 0
	adrp	x27, .LANCHOR2
	mov	w28, 256
	ldrb	w20, [x1,w0,uxtw]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L1005:
	ldr	w1, [x29,140]
	mov	w0, w20
	adrp	x21, .LANCHOR2
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	x3, [x19,8]
	mov	w0, w20
	ldr	x4, [x19,16]
	mov	w1, 0
	mov	w2, w25
	bl	NandcXferData
	mov	w26, w0
	add	x0, x27, :lo12:.LANCHOR2
	ldrb	w0, [x0,2248]
	cbz	w0, .L1006
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L1006:
	cmn	w26, #1
	bne	.L1007
	cmp	w23, 9
	bhi	.L1007
	add	w23, w23, 1
	b	.L1005
.L1007:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L1009:
	adrp	x0, .LANCHOR0
	ldr	w1, [x29,140]
	add	x0, x0, :lo12:.LANCHOR0
	ldr	w2, [x0,92]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashReadCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	x0, [x19,8]
	mov	w1, 0
	mov	w2, w25
	cmp	x0, xzr
	add	x3, x0, 2048
	ldr	x0, [x19,16]
	csel	x3, x3, xzr, ne
	add	x4, x0, 8
	cmp	x0, xzr
	csel	x4, x4, xzr, ne
	mov	w0, w20
	bl	NandcXferData
	mov	w28, w0
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w0, [x0,2248]
	cbz	w0, .L1012
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L1012:
	cmn	w28, #1
	bne	.L1013
	cmp	w23, 9
	bhi	.L1013
	add	w23, w23, 1
	b	.L1009
.L1013:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1015
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L1015:
	cmp	w28, 256
	beq	.L1026
	cmn	w28, #1
	bne	.L1016
.L1026:
	str	w28, [x19]
	b	.L1018
.L1016:
	str	wzr, [x19]
.L1018:
	ldr	x0, [x19,16]
	cbz	x0, .L1019
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L1019
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L1019
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L1019
	str	w1, [x19]
.L1019:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L1004
	add	x21, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,4]
	ldr	x0, [x29,96]
	ldrb	w2, [x21,1221]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L1021
	adrp	x0, .LC17
	mov	w2, 4
	add	x0, x0, :lo12:.LC17
	mov	w3, 8
	bl	rknand_print_hex
.L1021:
	ldr	x1, [x19,16]
	cbz	x1, .L1004
	adrp	x0, .LC18
	mov	w2, 4
	add	x0, x0, :lo12:.LC18
	mov	w3, w2
	bl	rknand_print_hex
.L1004:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L1002
.L1066:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	stp	x29, x30, [sp, -160]!
	adrp	x3, .LANCHOR1+481
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR0
	ldrb	w3, [x3,#:lo12:.LANCHOR1+481]
	add	x23, x23, :lo12:.LANCHOR0
	str	w3, [x29,136]
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	ldrb	w3, [x23,96]
	mov	x26, x0
	ldrb	w24, [x23,88]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	str	w1, [x29,132]
	str	w2, [x29,128]
	str	w3, [x29,116]
	cbz	w24, .L1107
	bl	FlashReadSlc2KPages
	b	.L1156
.L1107:
	add	x0, x23, 3220
	str	x0, [x29,104]
	adrp	x0, .LANCHOR2
	mov	w25, w24
	add	x21, x0, :lo12:.LANCHOR2
	add	x0, x21, 1236
	str	x0, [x29,120]
.L1068:
	ldr	w0, [x29,132]
	cmp	w25, w0
	bcs	.L1158
	mov	w27, 56
	ldr	w0, [x29,132]
	ldr	w1, [x29,128]
	add	x2, x29, 156
	umull	x27, w25, w27
	sub	w4, w0, w25
	add	x3, x29, 152
	add	x22, x26, x27
	mov	x0, x22
	ldr	w28, [x22,4]
	bl	LogAddr2PhyAddr
	ldrb	w1, [x23,3216]
	mov	w20, w0
	ldr	w0, [x29,152]
	cmp	w0, w1
	bcc	.L1070
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1071
.L1070:
	ldr	x1, [x29,104]
	ldrb	w19, [x1,w0,uxtw]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,2249]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x0, [x23,104]
	ldrb	w1, [x0,19]
	sub	w0, w1, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1073
	sub	w1, w1, #7
	add	x0, x21, 1232
	sxtw	x2, w19
	add	x0, x0, x2
	uxtb	w1, w1
	cmp	w1, 1
	ldrb	w3, [x0,12]
	bhi	.L1074
	ldrb	w3, [x0,20]
.L1074:
	add	x2, x21, x2
	ldrb	w0, [x2,2088]
	cmp	w0, w3
	beq	.L1073
	ldrb	w1, [x21,1233]
	mov	w0, w19
	ldr	x2, [x29,120]
	bl	HynixSetRRPara
.L1073:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,128]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,140]
	cbz	w0, .L1075
	ldrb	w0, [x21,1220]
	cbz	w0, .L1075
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1076
.L1075:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1076:
	cmp	w19, 255
	ldr	w1, [x29,156]
	bne	.L1114
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1108
.L1114:
	cbz	w20, .L1080
	ldr	w2, [x23,92]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1081
.L1080:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1081
.L1108:
	mov	w20, w0
.L1078:
	ldrb	w2, [x29,136]
	mov	w0, w19
	ldr	x3, [x22,8]
	mov	w1, 0
	ldr	x4, [x22,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1082
	ldrb	w0, [x23,96]
	cbz	w0, .L1082
	strb	wzr, [x23,96]
	mov	w20, 0
	b	.L1076
.L1082:
	cbz	w20, .L1083
	ldr	w2, [x23,92]
	mov	w0, w19
	ldr	w1, [x29,156]
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w25, 1
	mov	w4, 56
	ldrb	w2, [x29,136]
	mov	w1, 0
	umull	x4, w0, w4
	mov	w0, w19
	add	x4, x26, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w24, w0
	csel	w20, w20, wzr, ne
.L1083:
	mov	w0, w19
	bl	NandcFlashDeCs
	ldrb	w0, [x29,116]
	cmn	w28, #1
	strb	w0, [x23,96]
	bne	.L1090
	ldrb	w0, [x21,2104]
	cbnz	w0, .L1085
.L1089:
	ldr	x20, [x21,2192]
	cbnz	x20, .L1086
	b	.L1159
.L1085:
	ldr	x0, [x21,1152]
	mov	w4, 1
	ldr	w1, [x29,156]
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	ldr	w20, [x0,304]
	mov	w0, w19
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L1088
	ldrb	w0, [x21,1221]
	cmp	w28, w0, lsr 1
	bls	.L1110
.L1088:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1089
	b	.L1110
.L1086:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1091
	ldr	x0, [x23,104]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1092
	ldrb	w1, [x21,1233]
	mov	w0, w19
	ldr	x2, [x29,120]
	mov	w3, w20
	bl	HynixSetRRPara
.L1092:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	mov	w20, 0
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	ldr	w1, [x22,4]
	adrp	x0, .LC19
	ldrb	w2, [x21,1221]
	add	x0, x0, :lo12:.LC19
	mov	w3, w28
	bl	printk
	cmn	w28, #1
	bne	.L1091
	ldrb	w0, [x21,1220]
	cbz	w0, .L1091
	ldr	w0, [x29,140]
	cbz	w0, .L1093
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1094
.L1093:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1094:
	ldr	x4, [x21,2192]
	mov	w0, w19
	ldr	w1, [x29,156]
	mov	w20, 0
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	blr	x4
	mov	w28, w0
	b	.L1091
.L1159:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1091
.L1110:
	mov	w20, 0
.L1090:
	ldrb	w0, [x21,1221]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1091
	ldr	x0, [x21,2192]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1091:
	cmp	w28, 256
	beq	.L1115
	cmn	w28, #1
	bne	.L1095
.L1115:
	str	w28, [x26,x27]
	b	.L1097
.L1095:
	str	wzr, [x26,x27]
.L1097:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1099
	ldr	w1, [x22,4]
	adrp	x0, .LC16
	ldrb	w2, [x21,1221]
	add	x0, x0, :lo12:.LC16
	bl	printk
	ldr	x1, [x22,16]
	cbz	x1, .L1099
	adrp	x0, .LC18
	mov	w2, 4
	add	x0, x0, :lo12:.LC18
	mov	w3, w2
	bl	rknand_print_hex
.L1099:
	cbz	w20, .L1101
	ldrb	w0, [x21,1221]
	add	w0, w0, w0, lsl 1
	cmp	w24, w0, lsr 2
	bls	.L1102
	ldr	x0, [x21,2192]
	cmp	x0, xzr
	mov	w0, 256
	csel	w24, w24, w0, ne
.L1102:
	add	w0, w25, 1
	mov	w1, 56
	cmp	w24, 256
	umull	x0, w0, w1
	beq	.L1116
	cmn	w24, #1
	bne	.L1103
.L1116:
	str	w24, [x26,x0]
	b	.L1101
.L1103:
	str	wzr, [x26,x0]
.L1101:
	ldr	w0, [x29,140]
	add	w25, w25, w20
	cbz	w0, .L1071
	ldrb	w0, [x21,1220]
	cbz	w0, .L1071
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1071:
	add	w25, w25, 1
	b	.L1068
.L1158:
	mov	w0, 0
	b	.L1156
.L1081:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1078
	ldr	w1, [x29,156]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1078
.L1156:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	stp	x29, x30, [sp, -192]!
	mov	w1, 0
	mov	w2, 16
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	w27, -1
	mov	w28, 0
	ldrh	w0, [x19,1204]
	adrp	x25, .LC20
	ldrh	w21, [x19,1206]
	adrp	x26, .LANCHOR0
	add	x25, x25, :lo12:.LC20
	mul	w21, w0, w21
	add	x0, x19, 2256
	uxth	w21, w21
	bl	ftl_memset
	add	w24, w21, w27
	ldr	x0, [x19,2224]
	str	xzr, [x29,144]
	mov	w19, w28
	str	x0, [x29,152]
	uxth	w24, w24
.L1161:
	add	x1, x26, :lo12:.LANCHOR0
	ldrb	w1, [x1,3216]
	cmp	w1, w19
	bls	.L1171
	mul	w4, w19, w21
	mov	w20, w24
	sub	w3, w21, #12
	mov	w5, 61664
.L1162:
	cmp	w20, w3
	ble	.L1164
	add	w1, w4, w20
	add	x0, x29, 136
	lsl	w1, w1, 10
	str	w1, [x29,140]
	mov	w1, 1
	str	x5, [x29,104]
	mov	w2, w1
	str	x3, [x29,112]
	str	x4, [x29,120]
	bl	FlashReadPages
	ldr	w0, [x29,136]
	ldr	x4, [x29,120]
	cmn	w0, #1
	ldr	x3, [x29,112]
	ldr	x5, [x29,104]
	beq	.L1163
	add	x23, x22, :lo12:.LANCHOR2
	ldr	x0, [x23,2224]
	ldrh	w0, [x0]
	cmp	w0, w5
	bne	.L1163
	add	x23, x23, 2256
	mov	x0, x25
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x23,w19,sxtw 1]
	uxth	w28, w28
	b	.L1164
.L1163:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1162
.L1164:
	adrp	x0, .LANCHOR0
	add	w19, w19, 1
	add	x1, x0, :lo12:.LANCHOR0
	uxtb	w19, w19
	ldrb	w1, [x1,3216]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1161
.L1171:
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
	adrp	x4, .LANCHOR2
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	mov	x22, x0
	add	x0, x4, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	mov	w23, w1
	mov	w25, w2
	ldrh	w1, [x0,1206]
	mov	x21, x4
	ldrh	w3, [x0,1204]
	mov	w26, 61664
	ldr	x0, [x0,2224]
	str	x0, [x29,104]
	mul	w3, w3, w1
	adrp	x1, .LANCHOR0
	uxth	w3, w3
	sub	w20, w3, #1
	ldr	x1, [x1,#:lo12:.LANCHOR0]
	mul	w24, w23, w3
	str	x1, [x29,96]
	uxth	w20, w20
	sub	w19, w3, #16
.L1173:
	cmp	w20, w19
	ble	.L1181
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1174
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,2224]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1174
	mov	w0, w22
	cbz	x22, .L1175
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.6
	b	.L1175
.L1174:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1173
.L1181:
	mov	w0, -1
.L1175:
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
	adrp	x0, .LANCHOR0+104
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x0, [x0,#:lo12:.LANCHOR0+104]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR2
	mul	w19, w2, w19
	ldr	x0, [x0,2216]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L1183
.L1187:
	mov	w1, 0
	b	.L1184
.L1183:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L1185:
	cmp	w2, w19
	bge	.L1184
	add	x3, x20, :lo12:.LANCHOR2
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,2216]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L1186
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L1186:
	cmp	w1, w0
	bcs	.L1187
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1185
.L1184:
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
	adrp	x1, .LANCHOR1+481
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR0
	str	x27, [sp,80]
	stp	x25, x26, [sp,64]
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x19, x20, [sp,16]
	mov	w24, w2
	mov	x19, x0
	mov	x21, x0
	mov	w25, 0
	add	x27, x22, 3220
.L1193:
	cmp	w25, w23
	beq	.L1219
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,3216]
	cmp	w0, w1
	bcc	.L1194
	mov	w0, -1
	str	w0, [x21]
	b	.L1195
.L1194:
	ldrb	w20, [x27,w0,uxtw]
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
	ldr	w2, [x22,92]
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
	ldr	w2, [x0,92]
	mov	w0, w20
	add	w1, w2, w1
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcWaitFlashReady
	ldr	w1, [x29,96]
	mov	w0, w20
	bl	FlashReadStatus
	tbz	x0, 0, .L1198
	mov	w0, -1
	str	w0, [x21]
.L1198:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1195:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1193
.L1219:
	adrp	x25, .LC25
	adrp	x26, .LC24
	adrp	x27, .LC23
	mov	w21, 0
	add	x25, x25, :lo12:.LC25
	add	x26, x26, :lo12:.LC24
	add	x27, x27, :lo12:.LC23
.L1200:
	cmp	w21, w23
	beq	.L1220
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1201
	adrp	x0, .LC21
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC21
	bl	printk
	b	.L1202
.L1201:
	adrp	x20, .LANCHOR2
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,2232]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,2240]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,2232]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,2240]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1203
	adrp	x0, .LC22
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC22
	bl	printk
	str	w22, [x19]
.L1203:
	ldr	w22, [x29,104]
	cmp	w22, 256
	bne	.L1204
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1204:
	ldr	x0, [x19,16]
	cbz	x0, .L1205
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,2240]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1205
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1205:
	ldr	x0, [x19,8]
	cbz	x0, .L1202
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,2232]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1202
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1202:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1200
.L1220:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 160
	ret
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x21, .LANCHOR0
	mov	w22, 0
	add	x4, x21, :lo12:.LANCHOR0
	str	w3, [x29,116]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x19, x0
	mov	w25, w1
	ldr	x5, [x4,104]
	mov	w23, w2
	ldrb	w4, [x4,88]
	ldrb	w5, [x5,19]
	str	w5, [x29,120]
	adrp	x5, .LANCHOR1+481
	ldrb	w5, [x5,#:lo12:.LANCHOR1+481]
	str	w5, [x29,124]
	cbz	w4, .L1222
	bl	FlashProgSlc2KPages
	b	.L1223
.L1222:
	adrp	x28, .LANCHOR2
	add	x26, x28, :lo12:.LANCHOR2
	add	x0, x26, 1236
	str	x0, [x29,104]
.L1273:
	cmp	w22, w25
	bcs	.L1274
	mov	w20, 56
	mov	w1, w23
	add	x2, x29, 128
	sub	w4, w25, w22
	umull	x20, w22, w20
	add	x3, x29, 132
	add	x27, x19, x20
	mov	x0, x27
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w2, [x1,3216]
	cmp	w0, w2
	bcc	.L1224
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1225
.L1224:
	add	x2, x28, :lo12:.LANCHOR2
	add	x1, x1, 3260
	mov	x3, 24
	ldrb	w2, [x2,2094]
	cmp	w2, wzr
	uxtw	x2, w0
	csel	w24, w24, wzr, ne
	madd	x1, x2, x3, x1
	ldr	x1, [x1,8]
	cbz	x1, .L1227
	bl	FlashWaitCmdDone
.L1227:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	mov	x1, 24
	add	x0, x0, 3260
	madd	x0, x2, x1, x0
	ldr	w1, [x29,128]
	str	x27, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1228
	add	w1, w22, 1
	mov	w3, 56
	umull	x1, w1, w3
	add	x1, x19, x1
	str	x1, [x0,16]
.L1228:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, 3220
	ldrb	w20, [x0,x2]
	mov	x0, 24
	madd	x2, x2, x0, x1
	ldrb	w0, [x1,3216]
	strb	w20, [x2,3260]
	cmp	w0, 1
	bne	.L1229
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1230
.L1229:
	mov	w0, w20
	str	x1, [x29,96]
	bl	NandcFlashCs
	ldr	x1, [x29,96]
	ldr	w0, [x29,132]
	add	x1, x1, 3228
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1230:
	ldr	w0, [x29,120]
	sub	w0, w0, #1
	cmp	w0, 7
	bhi	.L1231
	add	x0, x26, x20, sxtw
	ldrb	w0, [x0,2088]
	cbz	w0, .L1231
	ldrb	w1, [x26,1233]
	mov	w0, w20
	ldr	x2, [x29,104]
	mov	w3, 0
	bl	HynixSetRRPara
.L1231:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1232
	ldrb	w0, [x26,1220]
	cbz	w0, .L1232
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1233
.L1232:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1233:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x27,8]
	mov	w1, 1
	ldr	x4, [x27,16]
	bl	NandcXferData
	cbz	w24, .L1234
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x27, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x27, 3228
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x27,92]
	mov	w0, w20
	ldr	w1, [x29,128]
	add	w1, w2, w1
	bl	FlashProgDpSecondCmd
	add	w0, w22, 1
	mov	w4, 56
	ldrb	w2, [x29,124]
	mov	w1, 1
	umull	x4, w0, w4
	mov	w0, w20
	add	x4, x19, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
.L1234:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1225:
	add	w22, w22, 1
	b	.L1273
.L1274:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	adrp	x22, .LANCHOR2
	add	x24, x21, 3260
	mov	x26, 24
.L1236:
	ldrb	w0, [x21,3216]
	cmp	w0, w20
	bls	.L1275
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1237
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L1237
	mul	x0, x20, x26
	ldrb	w0, [x0,x24]
	bl	flash_exit_slc_mode
.L1237:
	add	x20, x20, 1
	b	.L1236
.L1275:
	ldr	w0, [x29,116]
	cbnz	w0, .L1239
.L1247:
	mov	w0, 0
	b	.L1223
.L1239:
	adrp	x24, .LC25
	adrp	x26, .LC24
	adrp	x27, .LC22
	mov	w21, 0
	add	x24, x24, :lo12:.LC25
	add	x26, x26, :lo12:.LC24
	add	x27, x27, :lo12:.LC22
.L1240:
	cmp	w21, w25
	beq	.L1247
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1241
	adrp	x0, .LC21
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC21
	bl	printk
	b	.L1242
.L1241:
	adrp	x20, .LANCHOR2
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,2232]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,2240]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,2232]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,2240]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1243
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1243:
	ldr	x0, [x19,16]
	cbz	x0, .L1244
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,2240]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1244
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1244:
	ldr	x0, [x19,8]
	cbz	x0, .L1242
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,2232]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1242
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1242:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1240
.L1223:
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
	ldr	x0, [x19,2216]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,2216]
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
	bcc	.L1278
	mov	w0, w1
	bl	FlashTestBlk.part.7
.L1278:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR2
	add	x0, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x19, .LANCHOR0
	mov	w20, 0
	ldr	x1, [x0,2224]
	ldrh	w22, [x0,1206]
	str	x1, [x29,160]
	ldrh	w1, [x0,1204]
	add	x0, x19, :lo12:.LANCHOR0
	add	x19, x19, :lo12:.LANCHOR0
	mul	w22, w1, w22
	ldr	x1, [x0,104]
	uxth	w22, w22
	ldrb	w1, [x1,24]
	str	w1, [x29,128]
	ldrh	w1, [x0,92]
	str	w1, [x29,172]
	ldrb	w0, [x0,88]
	ubfiz	w1, w1, 1, 15
	cmp	w0, 1
	ldr	w0, [x29,172]
	csel	w0, w1, w0, eq
	str	w0, [x29,172]
	adrp	x0, .LC26
	mov	w1, 1
	add	x0, x0, :lo12:.LC26
	bl	printk
	add	x0, x23, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,2224]
	bl	ftl_memset
	lsr	w0, w22, 4
	str	w0, [x29,168]
	add	x0, x19, 3220
	str	x0, [x29,112]
	ldr	w0, [x29,128]
	and	w0, w0, 1
	str	w0, [x29,108]
	sub	w0, w22, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L1282:
	ldrb	w0, [x19,3216]
	cmp	w0, w20
	bls	.L1332
	add	x0, x23, :lo12:.LANCHOR2
	sxtw	x25, w20
	add	x1, x0, 2256
	ldrh	w21, [x1,w20,sxtw 1]
	cbnz	w21, .L1312
	ldrh	w2, [x0,1212]
	mov	w1, w21
	ldr	x0, [x19]
	mov	w26, w21
	lsl	w2, w2, 9
	mov	w24, w21
	add	x28, x19, 3228
	bl	ftl_memset
	ldr	x0, [x29,112]
	ldrb	w27, [x0,x25]
	ldr	w0, [x29,128]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,120]
.L1284:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w22
	bcs	.L1294
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,108]
	cbz	w0, .L1286
	ldr	w3, [x28,x25,lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w3, w21, w3
	str	x3, [x29,96]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x19,88]
	ldr	x3, [x29,96]
	cmp	w0, 1
	bne	.L1286
	ldr	w1, [x19,92]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1286:
	ldr	w0, [x29,120]
	cbz	w0, .L1288
	ldr	x0, [x19,104]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x25,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w21
	bl	FlashReadSpare
.L1288:
	ldr	x0, [x19,104]
	ldrb	w1, [x29,182]
	ldrb	w2, [x0,7]
	cmp	w2, 8
	beq	.L1289
	cmp	w2, 1
	beq	.L1289
	ldrb	w0, [x0,18]
	cmp	w0, 12
	bne	.L1290
.L1289:
	mov	w0, 1
	cbz	w1, .L1291
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1291
.L1290:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1291
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1291:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1292
	ldr	w1, [x28,x25,lsl 2]
	mov	w0, w27
	add	w1, w21, w1
	bl	SandiskProgTestBadBlock
.L1292:
	cbz	w0, .L1293
	adrp	x0, .LC27
	mov	w1, w20
	mov	w2, w26
	add	x0, x0, :lo12:.LC27
	add	w24, w24, 1
	bl	printk
	ldr	x0, [x29,144]
	uxth	w24, w24
	ldr	x2, [x19]
	ubfx	x1, x0, 5, 11
	ldrb	w3, [x29,144]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	ldr	w1, [x29,168]
	ldrb	w0, [x19,3216]
	mul	w0, w1, w0
	cmp	w24, w0
	bgt	.L1294
.L1293:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w21, w21, w0
	b	.L1284
.L1294:
	adrp	x0, .LC28
	mov	w1, w20
	add	x0, x0, :lo12:.LC28
	mov	w2, w24
	bl	printk
	ldrb	w0, [x19,3216]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w24, w0
	blt	.L1296
	add	x0, x23, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,1212]
	ldr	x0, [x19]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1296:
	cbnz	w20, .L1298
	adrp	x26, .LC29
	ldrh	w27, [x19,8]
	mov	w21, w20
	add	x26, x26, :lo12:.LC29
	mov	w24, 1
.L1299:
	ldrb	w0, [x19,89]
	cmp	w0, w27
	bls	.L1333
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1300
	mov	w1, w27
	mov	x0, x26
	add	w21, w21, 1
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x19]
	lsl	x1, x1, 2
	lsl	w0, w24, w27
	uxth	w21, w21
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1300:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1299
.L1333:
	adrp	x26, .LC29
	ldr	w28, [x29,124]
	sub	w27, w22, #50
	add	x26, x26, :lo12:.LC29
	mov	w24, 1
.L1302:
	cmp	w28, w27
	ble	.L1334
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1303
	mov	w1, w28
	mov	x0, x26
	bl	printk
	ubfx	x1, x28, 5, 11
	ldr	x2, [x19]
	lsl	x1, x1, 2
	lsl	w0, w24, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1303:
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1302
.L1334:
	ldrb	w0, [x19,89]
	ldr	w1, [x19,8]
	sub	w0, w0, w1
	cmp	w21, w0
	bcc	.L1298
	add	x0, x23, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,1212]
	ldr	x0, [x19]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1298:
	adrp	x24, .LC30
	ldr	w27, [x29,124]
	mul	w26, w20, w22
	add	x24, x24, :lo12:.LC30
	add	x28, x23, :lo12:.LANCHOR2
	mov	w21, -3872
.L1306:
	mov	w2, w27
	mov	x0, x24
	mov	w1, w20
	bl	printk
	ldr	x2, [x19]
.L1307:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1335
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1307
.L1335:
	add	x0, x28, 2256
	strh	w27, [x0,x25,lsl 1]
	ldr	x0, [x29,160]
	strh	w21, [x0]
	strh	w27, [x0,2]
	strh	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x19]
	mov	w2, w1
	str	x0, [x29,192]
	ldr	x0, [x28,2224]
	str	x0, [x29,200]
	add	w0, w27, w26
	lsl	w0, w0, 10
	str	w0, [x29,188]
	add	x0, x29, 184
	bl	FlashEraseBlocks
	mov	w1, 1
	add	x0, x29, 184
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldr	w0, [x29,184]
	cbz	w0, .L1312
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1306
.L1312:
	add	w20, w20, 1
	uxtb	w20, w20
	b	.L1282
.L1332:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 240
	ret
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	mov	w2, 1
	mov	w1, 0
.L1337:
	cmp	w2, w0
	bhi	.L1339
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L1337
.L1339:
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
	ldrh	w2, [x1,2276]
	ldrh	w3, [x1,2286]
	str	w0, [x1,2272]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,2284]
	ldr	w0, [x1,2292]
	str	w2, [x1,2280]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,2288]
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
	adrp	x20, .LANCHOR2
	mov	x23, x0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	str	x27, [sp,80]
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w4, [x23,14]
	ldrh	w6, [x0,8]
	ldrh	w2, [x0,10]
	ldrh	w0, [x0,12]
	strh	w6, [x1,2296]
	strh	w2, [x1,2298]
	strh	w0, [x1,2300]
	strh	w4, [x1,2286]
.L1344:
	add	x5, x1, 2304
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1344
	ldrh	w3, [x23,20]
	ldrb	w1, [x23,15]
	cmp	w3, w1
	bcs	.L1345
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR2
.L1346:
	cmp	w3, w0
	bcs	.L1348
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L1349:
	cmp	w7, w2
	bcs	.L1371
	add	x9, x12, 2304
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L1349
.L1371:
	add	w3, w3, 1
	b	.L1346
.L1348:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,2298]
	strh	w4, [x1,2286]
.L1345:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,2336]
	strh	wzr, [x1,2338]
	bne	.L1350
	strh	w6, [x1,2336]
.L1350:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 4352
	strh	w2, [x1,2340]
	adrp	x2, .LANCHOR0+88
	ldrb	w26, [x2,#:lo12:.LANCHOR0+88]
	cbz	w26, .L1351
	mov	w2, 384
	strh	w2, [x1,2340]
.L1351:
	add	x21, x20, :lo12:.LANCHOR2
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	ldrh	w27, [x23,18]
	ldrh	w22, [x21,2298]
	ldrh	w19, [x21,2286]
	strh	w25, [x21,2344]
	mul	w22, w0, w22
	mul	w0, w19, w0
	strh	w0, [x21,2342]
	uxth	w22, w22
	strh	w27, [x21,2346]
	strh	w22, [x21,2276]
	mul	w0, w22, w25
	strh	w0, [x21,2348]
	mov	w0, w24
	strh	w24, [x21,2350]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w24, 9
	strh	w0, [x21,2352]
	ldrh	w0, [x23,26]
	cmp	w19, 1024
	strh	w2, [x21,2354]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2358]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2356]
	str	w0, [x21,2292]
	bls	.L1352
	and	w0, w19, 255
	strh	w0, [x21,2338]
.L1352:
	add	x2, x20, :lo12:.LANCHOR2
	mul	w0, w24, w27
	ldrh	w1, [x2,2338]
	sub	w1, w19, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2360]
	ldrh	w1, [x2,2340]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L1368
	mov	w0, 4
.L1368:
	strh	w0, [x2,2364]
	cbz	w26, .L1355
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0,2340]
.L1355:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2340]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2368]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2366]
	mul	w0, w19, w22
	str	w0, [x1,2372]
	add	w19, w19, 8
	ldrh	w0, [x1,2364]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L1369
	add	w19, w19, 4
.L1369:
	str	w19, [x1,2272]
	add	x19, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19,2272]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,1224]
	ldr	w0, [x19,2272]
	str	w0, [x19,2376]
	ldr	w0, [x19,2288]
	ldrh	w1, [x19,2344]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2350]
	ldrh	w2, [x19,2364]
	mul	w1, w0, w1
	ldrh	w0, [x19,2352]
	ldrb	w5, [x19,1220]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2382]
	uxth	w0, w0
	strh	w0, [x19,2380]
	add	w1, w2, 3
	strh	w1, [x19,2364]
	ldr	w1, [x19,2372]
	add	w4, w1, 3
	str	w4, [x19,2372]
	cbz	w5, .L1358
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2364]
	b	.L1370
.L1358:
	cmp	w4, 7
	bhi	.L1359
	mov	w1, 8
.L1370:
	str	w1, [x19,2372]
.L1359:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x20,2284]
	strh	wzr, [x20,2384]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L1360
	mov	w0, 1
	strh	w0, [x20,2384]
.L1360:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	str	w1, [x0,2456]
	mov	w1, -1
	strh	wzr, [x0,2386]
	strh	w1, [x0,2472]
	strh	w1, [x0,2474]
	strh	w1, [x0,2476]
	str	wzr, [x0,2388]
	str	wzr, [x0,2392]
	str	wzr, [x0,2396]
	str	wzr, [x0,2400]
	str	wzr, [x0,2404]
	str	wzr, [x0,2408]
	str	wzr, [x0,2412]
	str	wzr, [x0,2416]
	str	wzr, [x0,2420]
	str	wzr, [x0,2424]
	str	wzr, [x0,2428]
	str	wzr, [x0,2432]
	str	wzr, [x0,2436]
	str	wzr, [x0,2440]
	str	wzr, [x0,2444]
	str	wzr, [x0,2448]
	str	wzr, [x0,2452]
	str	wzr, [x0,2460]
	str	wzr, [x0,2464]
	str	wzr, [x0,2468]
	strh	w1, [x0,2478]
	mov	w1, 32
	ldrh	w2, [x0,2350]
	strh	w1, [x0,2480]
	mov	w1, 128
	strh	w1, [x0,2482]
	mov	w1, 1024
	strh	wzr, [x0,2484]
	sdiv	w2, w1, w2
	ldrh	w1, [x0,2276]
	strh	wzr, [x0,2486]
	lsl	w1, w1, 2
	strh	wzr, [x0,2488]
	strh	wzr, [x0,2490]
	cmp	w2, w1
	str	w2, [x0,2492]
	bls	.L1373
	str	w1, [x0,2492]
.L1373:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19,2348]
	str	wzr, [x19,2496]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2504]
	ldrh	w1, [x19,2348]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,2276]
	str	x0, [x19,2512]
	mul	w21, w21, w22
	lsl	w23, w21, 3
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2520]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2528]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2536]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2544]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2552]
	ldr	w0, [x19,2492]
	mul	w0, w0, w22
	bl	ftl_malloc
	str	x0, [x19,2560]
	ldrh	w0, [x19,2276]
	ldrh	w21, [x19,2354]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,2568]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2576]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2584]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2592]
	ldr	w0, [x19,2568]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,2600]
	ldr	w0, [x19,2492]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,2608]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2616]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2624]
	ldr	w1, [x19,2568]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,2276]
	str	x0, [x19,2632]
	ldrh	w0, [x19,2356]
	mul	w21, w0, w21
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2640]
	lsl	w0, w21, 3
	bl	ftl_malloc
	str	x0, [x19,2648]
	ldrh	w1, [x19,2356]
	ldr	w0, [x19,2568]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2656]
	ldrh	w1, [x19,2356]
	ldr	w0, [x19,2492]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2664]
	ldrh	w0, [x19,2286]
	lsl	w0, w0, 1
	strh	w0, [x19,2672]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,2680]
	ldrh	w0, [x19,2672]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,2672]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19,2286]
	str	x0, [x19,2688]
	add	x0, x0, 32
	lsl	w21, w21, 1
	str	x0, [x19,2696]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2704]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19,2372]
	str	x0, [x19,2712]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2720]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2728]
	ldrh	w0, [x19,2286]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	adrp	x1, .LANCHOR0+16
	str	x0, [x1,#:lo12:.LANCHOR0+16]
	ldrh	w0, [x19,2364]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2736]
	ldrh	w0, [x19,2364]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2744]
	ldrh	w0, [x19,2364]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2752]
	ldrh	w0, [x19,2366]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2760]
	ldrh	w2, [x19,2366]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w21, [x19,2380]
	lsl	w21, w21, 2
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2768]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2776]
	ldr	w0, [x19,2372]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2784]
	ldrh	w0, [x19,2382]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,2792]
	ldrh	w1, [x19,2382]
	ldrh	w0, [x19,2354]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2800]
	ldrh	w1, [x19,2286]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2808]
	ldrh	w0, [x19,2342]
	ldrh	w1, [x19,2298]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2816]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2856]
	add	x1, x19, 2824
	ldrh	w4, [x19,2816]
	ldrh	w5, [x19,2298]
	mov	w0, 1
	mov	w2, w4
.L1374:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L1474
	ldr	x3, [x19,2856]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L1374
.L1474:
	add	x1, x20, :lo12:.LANCHOR2
.L1376:
	cmp	w0, 8
	beq	.L1475
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2856]
	b	.L1376
.L1475:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2720]
	cbnz	x1, .L1378
.L1380:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC31
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC31
	add	x1, x1, 136
	bl	printk
	mov	w0, -1
	b	.L1379
.L1378:
	ldr	x1, [x0,2728]
	cbz	x1, .L1380
	ldr	x1, [x0,2768]
	cbz	x1, .L1380
	ldr	x1, [x0,2784]
	cbz	x1, .L1380
	ldr	x1, [x0,2792]
	cbz	x1, .L1380
	ldr	x1, [x0,2800]
	cbz	x1, .L1380
	ldr	x1, [x0,2808]
	cbz	x1, .L1380
	ldr	x1, [x0,2856]
	cbz	x1, .L1380
	ldr	x0, [x0,2712]
	cbz	x0, .L1380
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2504]
	cbz	x1, .L1380
	ldr	x1, [x0,2512]
	cbz	x1, .L1380
	ldr	x1, [x0,2520]
	cbz	x1, .L1380
	ldr	x1, [x0,2536]
	cbz	x1, .L1380
	ldr	x1, [x0,2544]
	cbz	x1, .L1380
	ldr	x1, [x0,2552]
	cbz	x1, .L1380
	ldr	x1, [x0,2528]
	cbz	x1, .L1380
	ldr	x1, [x0,2576]
	cbz	x1, .L1380
	ldr	x1, [x0,2584]
	cbz	x1, .L1380
	ldr	x0, [x0,2592]
	cbz	x0, .L1380
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2600]
	cbz	x1, .L1380
	ldr	x1, [x0,2616]
	cbz	x1, .L1380
	ldr	x1, [x0,2624]
	cbz	x1, .L1380
	ldr	x1, [x0,2632]
	cbz	x1, .L1380
	ldr	x1, [x0,2640]
	cbz	x1, .L1380
	ldr	x1, [x0,2648]
	cbz	x1, .L1380
	ldr	x1, [x0,2656]
	cbz	x1, .L1380
	ldr	x1, [x0,2696]
	cbz	x1, .L1380
	ldr	x1, [x0,2680]
	cbz	x1, .L1380
	ldr	x0, [x0,2736]
	cbz	x0, .L1380
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2744]
	cbz	x1, .L1380
	ldr	x1, [x0,2752]
	cbz	x1, .L1380
	ldr	x0, [x0,2760]
	cbz	x0, .L1380
	mov	w0, 0
.L1379:
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
	ldrh	w2, [x1,2920]
	cbz	w2, .L1477
	ldrh	w2, [x1,2364]
	ldr	x3, [x1,2736]
	mov	x1, 0
.L1478:
	cmp	w2, w1, uxth
	bls	.L1483
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L1478
	mov	w0, 1
	b	.L1477
.L1483:
	mov	w0, 0
.L1477:
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
	ldr	w3, [x2,2496]
	cbz	w3, .L1485
	ldr	x5, [x2,2560]
	mov	x0, 56
	mov	x2, 0
.L1486:
	cmp	w3, w2
	bls	.L1495
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L1486
	cmp	w4, w1
	bhi	.L1486
	mov	w0, 1
	b	.L1485
.L1495:
	mov	w0, 0
.L1485:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0,#:lo12:.LANCHOR2+1224]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0,#:lo12:.LANCHOR2+1224]
	ret
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR2+1224
	ldr	w0, [x0,#:lo12:.LANCHOR2+1224]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR2+2924
	ldr	w0, [x0,#:lo12:.LANCHOR2+2924]
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
	ldrh	w3, [x19,2342]
	add	x19, x19, 2824
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
	adrp	x0, .LC32
	add	x0, x0, :lo12:.LC32
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
	ldrh	w1, [x3,2342]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2856]
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x21, [sp,32]
	add	x19, x19, :lo12:.LANCHOR2
	mov	x21, x0
	mov	x20, x1
	mov	x0, x1
	mov	w1, 0
	ldrh	w2, [x19,2816]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L1505:
	ldrh	w2, [x21,x3]
	cmp	w2, w0
	beq	.L1503
	ubfx	x4, x2, 5, 11
	lsl	w2, w5, w2
	lsl	x4, x4, 2
	add	x3, x3, 2
	cmp	x3, 1024
	ldr	w1, [x20,x4]
	orr	w2, w1, w2
	str	w2, [x20,x4]
	ldrh	w2, [x19,2830]
	add	w2, w2, 1
	strh	w2, [x19,2830]
	bne	.L1505
.L1503:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
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
	ldr	w1, [x19,2928]
	stp	x25, x26, [sp,64]
	cbnz	w1, .L1510
	ldr	x0, [x19,2576]
	mov	x22, 0
	ldr	x3, [x19,2640]
	add	x21, x19, 2824
	ldrh	w2, [x19,2354]
	str	x0, [x19,2944]
	str	x3, [x19,2952]
	bl	ftl_memset
.L1511:
	ldrh	w1, [x19,2298]
	mov	w0, w22
	add	x20, x19, 2936
	add	x22, x22, 1
	cmp	w0, w1
	bge	.L1524
	add	x1, x21, x22, lsl 3
	ldrh	w2, [x19,2816]
	ldr	x3, [x20,8]
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1511
.L1524:
	ldr	x26, [x20,16]
	mov	w1, 255
	mov	w2, 16
	mov	w22, 0
	mov	x0, x26
	adrp	x24, .LC33
	adrp	x25, .LC34
	mov	w23, w22
	bl	ftl_memset
	add	x24, x24, :lo12:.LC33
	mov	w0, -3887
	strh	w0, [x26]
	add	x25, x25, :lo12:.LC34
	ldr	w0, [x21,8]
	str	w0, [x26,4]
	ldrh	w0, [x19,2824]
	strh	w0, [x26,2]
	ldrh	w0, [x21,4]
	strh	w0, [x26,8]
	ldrh	w0, [x21,6]
	strh	w0, [x26,10]
	ldr	w0, [x19,2272]
	strh	w0, [x26,12]
.L1513:
	ldr	x0, [x19,2576]
	str	x0, [x20,8]
	ldrh	w1, [x19,2824]
	ldr	x0, [x19,2640]
	str	x0, [x20,16]
	ldrh	w2, [x19,2826]
	ldrh	w3, [x19,2828]
	orr	w0, w2, w1, lsl 10
	ldrh	w4, [x26,10]
	str	w0, [x20,4]
	mov	x0, x24
	str	wzr, [x20]
	bl	printk
	ldrh	w1, [x19,2346]
	ldrh	w0, [x19,2826]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1514
	ldr	w1, [x19,2832]
	ldrh	w0, [x19,2824]
	add	w1, w1, 1
	str	w1, [x19,2832]
	str	w1, [x26,4]
	strh	w0, [x26,8]
	ldrh	w1, [x19,2828]
	strh	w0, [x19,2828]
	ldr	x0, [x19,2544]
	strh	w1, [x19,2824]
	lsl	w1, w1, 10
	str	w1, [x20,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x19,2826]
	ldr	x0, [x19,2544]
	bl	FlashEraseBlocks
.L1514:
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,2826]
	ldr	w0, [x20]
	add	w1, w1, 1
	strh	w1, [x19,2826]
	cmn	w0, #1
	bne	.L1515
	ldr	w1, [x20,4]
	mov	x0, x25
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	cmp	w22, 3
	bls	.L1513
	adrp	x0, .LC35
	ldr	w1, [x20,4]
	add	x0, x0, :lo12:.LC35
	mov	w2, w22
	bl	printk
	mov	w0, 1
	str	w0, [x19,2928]
	b	.L1510
.L1515:
	add	w23, w23, 1
	cmp	w23, 1
	beq	.L1513
	cmp	w0, 256
	beq	.L1513
.L1510:
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
	add	x21, x1, 2936
	add	x23, x1, 2824
	ldr	x0, [x1,2576]
	mov	w22, 0
	ldr	x24, [x1,2640]
	mov	x19, x1
	str	x0, [x21,8]
	mov	w25, -1
	str	x24, [x21,16]
	mov	w26, 61664
.L1526:
	ldrh	w0, [x19,2298]
	cmp	w22, w0
	bcs	.L1535
	ldrh	w20, [x19,2342]
	strh	w25, [x23,12]
.L1528:
	sub	w20, w20, #1
	ldrh	w0, [x19,2342]
	uxth	w20, w20
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1529
	madd	w0, w22, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1528
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1528
	strh	w20, [x23,12]
.L1529:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L1526
.L1535:
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
	add	x0, x0, 2836
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
	ldrh	w1, [x0,2342]
	ldrh	w21, [x0,2298]
	mul	w21, w1, w21
.L1538:
	cmp	w20, w21
	bge	.L1544
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1539
	add	w19, w19, 1
	uxth	w19, w19
.L1539:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1538
.L1544:
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
	ldr	w22, [x20,2928]
	cbnz	w22, .L1546
	add	x24, x20, 2824
	mov	x19, x20
	mov	x25, x24
	add	x20, x20, 2936
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1547:
	ldrh	w0, [x19,2298]
	cmp	w22, w0
	bcs	.L1565
	ldrh	w4, [x25,12]
	mov	w1, 65535
	ldr	x0, [x19,2576]
	ldr	x26, [x19,2640]
	cmp	w4, w1
	str	x0, [x20,8]
	str	x26, [x20,16]
	beq	.L1548
	ldrh	w0, [x19,2342]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w22, w0, w4
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x19,2342]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L1549
.L1548:
	mov	w1, w22
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w23, [x19,2342]
.L1551:
	sub	w23, w23, #1
	uxth	w23, w23
.L1550:
	ldrh	w0, [x19,2342]
	madd	w0, w22, w0, w23
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1551
	ldr	x0, [x19,2640]
	mov	w1, 0
	mov	w2, 16
	strh	w23, [x25,12]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldrh	w2, [x19,2816]
	ldrh	w0, [x25,12]
	strh	w0, [x26,2]
	lsl	w2, w2, 2
	ldrh	w0, [x19,2342]
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
	bne	.L1549
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L1550
.L1549:
	mov	w0, w27
	add	w22, w22, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L1547
.L1565:
	mov	w19, 0
.L1554:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,2358]
	cmp	w0, w19
	bls	.L1566
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	uxth	w19, w19
	b	.L1554
.L1566:
	ldrh	w19, [x20,2836]
	mov	w23, 65535
	sub	w19, w19, #1
	uxth	w19, w19
.L1556:
	add	x22, x20, 2824
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1560
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1557
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1558
	mov	w0, w19
	bl	FtlBbmMapBadBlock
	b	.L1557
.L1558:
	ldrh	w0, [x20,2824]
	cmp	w0, w23
	bne	.L1559
	strh	w19, [x20,2824]
	b	.L1557
.L1559:
	strh	w19, [x22,4]
	b	.L1560
.L1557:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1556
.L1560:
	add	x19, x21, :lo12:.LANCHOR2
	mov	w2, 2
	ldr	x1, [x19,2544]
	ldrh	w0, [x19,2824]
	str	wzr, [x19,2832]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,2826]
	ldr	x0, [x19,2544]
	ldrh	w1, [x19,2828]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2824]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2828]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2826]
	ldr	w0, [x19,2832]
	ldrh	w1, [x19,2828]
	add	w0, w0, 1
	str	w0, [x19,2832]
	ldrh	w0, [x19,2824]
	strh	w0, [x19,2828]
	strh	w1, [x19,2824]
	bl	FtlBbmTblFlush
.L1546:
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
	ldrh	w4, [x3,2300]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2342]
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
	ldrh	w1, [x2,2300]
	ldrh	w3, [x2,2342]
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
	ldrh	w3, [x2,2342]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,2300]
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
	bls	.L1571
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L1572
.L1571:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L1572:
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
	bne	.L1574
	ldrh	w19, [x3,2346]
	b	.L1575
.L1574:
	ldrh	w19, [x3,2344]
.L1575:
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
	bne	.L1576
	mov	w22, 0
	mov	w25, 2
.L1577:
	cmp	w22, w19
	bgt	.L1576
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
	bne	.L1578
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1578
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1578
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1577
.L1578:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1577
.L1576:
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
	add	x21, x23, 2936
	ldr	x0, [x23,2576]
	ldr	x22, [x23,2640]
	str	x0, [x21,8]
	str	x22, [x21,16]
	bl	FtlBbtMemInit
	ldrh	w20, [x23,2342]
	sub	w20, w20, #1
	uxth	w20, w20
.L1585:
	ldrh	w0, [x23,2342]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1588
	lsl	w0, w20, 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1586
	ldr	w0, [x21,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
.L1586:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1587
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1587
	ldr	w1, [x22,4]
	str	w1, [x23,2832]
	strh	w20, [x23,2824]
	ldrh	w1, [x22,8]
	strh	w1, [x23,2828]
	b	.L1588
.L1587:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1585
.L1588:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2824]
	cmp	w1, w2
	beq	.L1590
	ldrh	w1, [x21,2828]
	cmp	w1, w2
	beq	.L1592
	add	x0, x21, 2936
	lsl	w1, w1, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x21,2936]
	cmn	w0, #1
	beq	.L1592
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1592
	ldr	w0, [x22,4]
	ldr	w1, [x21,2832]
	cmp	w0, w1
	bls	.L1592
	str	w0, [x21,2832]
	ldrh	w1, [x21,2828]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2824]
	strh	w0, [x21,2828]
.L1592:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 2936
	mov	w24, 61649
	ldrh	w0, [x20,2824]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20,2826]
.L1594:
	tbnz	w21, #31, .L1599
	ldrh	w0, [x20,2824]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w21, w0, lsl 10
	str	w0, [x23,4]
	ldr	x0, [x20,2576]
	str	x0, [x23,8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1595
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1595
.L1599:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2830]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1596
	b	.L1597
.L1595:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1594
.L1596:
	ldr	w2, [x1,2272]
	cmp	w0, w2
	beq	.L1597
	ldrh	w1, [x1,2286]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1597
	cmp	w2, w1
	bcs	.L1597
	bl	FtlSysBlkNumInit
.L1597:
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, 0
	add	x21, x19, 2824
.L1600:
	ldrh	w0, [x19,2298]
	mov	w1, w20
	add	x20, x20, 1
	cmp	w1, w0
	bcs	.L1618
	ldrh	w2, [x19,2816]
	add	x0, x21, x20, lsl 3
	ldr	x3, [x19,2944]
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1600
.L1618:
	mov	w0, 0
.L1590:
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
	strh	w0, [x1,2992]
	add	x0, x1, 3000
	strh	wzr, [x1,2994]
	strh	wzr, [x1,2996]
	strh	wzr, [x1,2998]
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
	adrp	x0, .LANCHOR2+2998
	ldrh	w0, [x0,#:lo12:.LANCHOR2+2998]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR2+2998
	ldrh	w0, [x0,#:lo12:.LANCHOR2+2998]
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
	bcc	.L1622
	adrp	x2, .LANCHOR2
	add	x20, x2, :lo12:.LANCHOR2
	mov	x19, x2
	ldrh	w0, [x20,2998]
	cmp	w0, 1024
	beq	.L1622
	cbz	w1, .L1624
	ldr	w0, [x20,2928]
	cbnz	w0, .L1624
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,2544]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,2544]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,2696]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,2436]
	add	w0, w0, 1
	str	w0, [x20,2436]
.L1624:
	add	x1, x19, :lo12:.LANCHOR2
	add	x1, x1, 2992
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L1622:
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
	stp	x27, x28, [sp,80]
	add	x28, x21, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	uxth	w25, w0
	ldr	w2, [x28,2928]
	ldrb	w0, [x28,2094]
	stp	x23, x24, [sp,48]
	str	w0, [x29,120]
	uxtb	w24, w1
	stp	x19, x20, [sp,16]
	mov	w0, 0
	cbnz	w2, .L1632
	ldrb	w0, [x28,1220]
	mov	w26, w2
	str	w0, [x29,124]
	mov	w20, w2
	str	w25, [x28,2468]
	mov	w19, w2
	mov	w23, 56
	mov	w27, 4
.L1633:
	ldrh	w0, [x28,2276]
	cmp	w0, w26
	bls	.L1677
	umull	x0, w26, w23
	ldr	x1, [x28,2544]
	str	wzr, [x1,x0]
	add	x0, x28, 2304
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbz	w24, .L1634
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1635
.L1634:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1636
	umull	x0, w20, w23
	ldr	x1, [x28,2544]
	lsl	w22, w22, 10
	add	x1, x1, x0
	str	w22, [x1,4]
	ldr	x1, [x28,2544]
	ldr	x3, [x28,2648]
	add	x1, x1, x0
	ldrh	w0, [x28,2356]
	mul	w0, w20, w0
	add	w20, w20, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w27
	uxth	w20, w20
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1635
.L1636:
	add	w19, w19, 1
	uxth	w19, w19
.L1635:
	add	w2, w26, 1
	uxth	w26, w2
	b	.L1633
.L1677:
	cbz	w20, .L1656
	ldr	w0, [x29,124]
	mov	w2, w20
	strb	wzr, [x28,2094]
	mov	x23, 0
	cmp	w0, wzr
	ldr	x0, [x28,2544]
	cset	w22, ne
	mov	x26, 56
	mov	w1, w22
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x28,2094]
.L1639:
	cmp	w20, w23, uxth
	bls	.L1678
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,2544]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1640
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1640:
	add	x23, x23, 1
	b	.L1639
.L1678:
	cbnz	w24, .L1642
	mov	w0, 1
	uxth	w22, w22
	mov	w27, 6
	str	w0, [x29,124]
	b	.L1643
.L1642:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,2346]
	ldrb	w0, [x0,1220]
	str	w1, [x29,124]
	cbnz	w0, .L1659
	uxtw	x0, w1
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1643
.L1659:
	mov	w22, 1
	mov	w27, w22
.L1643:
	mov	w23, 0
	add	x28, x21, :lo12:.LANCHOR2
.L1652:
	mov	w26, 0
	mov	w20, w26
.L1644:
	ldrh	w0, [x28,2276]
	cmp	w0, w26
	bls	.L1679
	mov	w0, 56
	ldr	x1, [x28,2544]
	umull	x0, w26, w0
	str	wzr, [x1,x0]
	add	x0, x28, 2304
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w24, .L1645
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1646
.L1645:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1646
	mov	w0, 56
	ldr	x1, [x28,2544]
	ldr	w2, [x29,112]
	umull	x0, w20, w0
	add	x1, x1, x0
	add	w2, w23, w2, lsl 10
	str	w2, [x1,4]
	mov	w2, 4
	ldr	x1, [x28,2544]
	add	x1, x1, x0
	ldr	x0, [x28,2616]
	str	x0, [x1,8]
	ldrh	w0, [x28,2356]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w2
	uxth	w20, w20
	ldr	x2, [x28,2624]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1646:
	add	w3, w26, 1
	uxth	w26, w3
	b	.L1644
.L1679:
	cbz	w20, .L1656
	ldr	x0, [x28,2544]
	mov	w1, w20
	mov	w2, w22
	mov	w3, 1
	strb	wzr, [x28,2094]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x28,2094]
.L1649:
	cmp	w20, w1, uxth
	bls	.L1680
	mov	x0, 56
	ldr	x2, [x28,2544]
	mul	x0, x1, x0
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1650
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1650:
	add	x1, x1, 1
	b	.L1649
.L1680:
	add	w23, w23, w27
	ldr	w0, [x29,124]
	uxth	w23, w23
	cmp	w23, w0
	bcc	.L1652
	mov	x23, 0
	mov	x26, 56
.L1653:
	cmp	w20, w23, uxth
	bls	.L1681
	cbz	w24, .L1654
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,2544]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1654
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1654:
	add	x23, x23, 1
	b	.L1653
.L1681:
	cmp	w25, 63
	bls	.L1660
	cbz	w24, .L1656
.L1660:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, w20
	ldr	x0, [x21,2544]
	bl	FlashEraseBlocks
.L1656:
	mov	w0, w19
.L1632:
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
	adrp	x0, .LANCHOR4+724
	ldrh	w6, [x0,#:lo12:.LANCHOR4+724]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	add	x1, x1, 2992
	ldrh	w2, [x1,6]
	cbz	w2, .L1682
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L1684:
	cmp	w4, w6
	bge	.L1692
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
	b	.L1684
.L1692:
	cbz	w5, .L1682
	add	x0, x0, :lo12:.LANCHOR2
	strh	w3, [x0,2994]
	strh	w2, [x0,2996]
.L1682:
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
	adrp	x23, .LC37
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	add	x21, x19, 2992
	mov	w24, 65533
	add	x23, x23, :lo12:.LC37
.L1694:
	ldrh	w1, [x21,6]
	adrp	x22, .LANCHOR2
	cbz	w1, .L1695
	ldrh	w2, [x21,2]
	sub	w1, w1, #1
	add	x0, x21, x2, sxtw 1
	strh	w1, [x21,6]
	add	w2, w2, 1
	ldr	w1, [x19,2928]
	and	w2, w2, 1023
	strh	w2, [x21,2]
	ldrh	w20, [x0,8]
	cbnz	w1, .L1696
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x19,2544]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x19,1220]
	cbz	w0, .L1697
	ldr	x0, [x19,2544]
	mov	w2, 1
	bl	FlashEraseBlocks
.L1697:
	add	x25, x22, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,2544]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x25,2696]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x25,2436]
	add	w0, w0, 1
	str	w0, [x25,2436]
	b	.L1696
.L1695:
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	bl	printk
.L1698:
	b	.L1698
.L1696:
	sub	w0, w20, #1
	cmp	w24, w0, uxth
	bcs	.L1699
	add	x22, x22, :lo12:.LANCHOR2
	mov	x0, x23
	mov	w1, w20
	ldrh	w2, [x22,2998]
	bl	printk
	b	.L1694
.L1699:
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
	adrp	x0, .LANCHOR2+2808
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+2808]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L1705:
	cmp	w1, w0
	beq	.L1706
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L1707
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L1705
.L1706:
	mov	w0, 1
	b	.L1704
.L1707:
	mov	w0, 0
.L1704:
	ret
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	sub	sp, sp, #16
	adrp	x10, .LANCHOR2
	add	x3, x10, :lo12:.LANCHOR2
	uxth	w0, w0
	str	x19, [sp]
	ldrh	w1, [x3,2284]
	cmp	w1, w0
	bls	.L1710
	mov	w7, 6
	ldr	x12, [x3,2808]
	adrp	x2, .LANCHOR4
	mov	w1, -1
	umull	x11, w0, w7
	add	x6, x2, :lo12:.LANCHOR4
	mov	x4, x2
	add	x5, x12, x11
	strh	w1, [x5,2]
	strh	w1, [x12,x11]
	ldr	x1, [x6,744]
	cbnz	x1, .L1711
	str	x5, [x6,744]
	b	.L1710
.L1711:
	ubfiz	x8, x0, 1, 16
	ldr	x14, [x3,2712]
	ldrh	w2, [x5,4]
	mov	x9, -6148914691236517206
	ldr	x13, [x3,2808]
	mov	w19, 65535
	ldrh	w6, [x14,x8]
	cmp	w2, wzr
	ldr	x15, [x3,2696]
	ldrh	w18, [x3,2284]
	mov	w3, 0
	mul	w6, w6, w2
	sub	x2, x1, x13
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x9, x2, x2
	add	x9, x15, x8
	mov	w8, w7
	uxth	w2, w2
.L1719:
	add	w3, w3, 1
	cmp	w0, w2
	uxth	w3, w3
	beq	.L1710
	cmp	w3, w18
	bhi	.L1710
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1,4]
	cmp	w17, wzr
	ldrh	w7, [x14,x16]
	mul	w7, w7, w17
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L1715
	ldrh	w16, [x15,x16]
	ldrh	w7, [x9]
	cmp	w16, w7
	bcc	.L1717
	b	.L1716
.L1715:
	bhi	.L1716
.L1717:
	ldrh	w7, [x1]
	cmp	w7, w19
	bne	.L1718
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR4
	strh	w0, [x1]
	str	x5, [x2,752]
	b	.L1710
.L1718:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x13, x1
	b	.L1719
.L1716:
	strh	w2, [x12,x11]
	ldrh	w2, [x1,2]
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR4
	ldr	x3, [x2,744]
	cmp	x1, x3
	bne	.L1720
	strh	w0, [x1,2]
	str	x5, [x2,744]
	b	.L1710
.L1720:
	ldrh	w2, [x1,2]
	add	x3, x10, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3,2808]
	umull	x2, w2, w4
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L1710:
	mov	w0, 0
	ldr	x19, [sp]
	add	sp, sp, 16
	ret
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	bl	insert_data_list
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w1, [x0,760]
	add	w1, w1, 1
	strh	w1, [x0,760]
	ldp	x29, x30, [sp], 16
	ret
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	uxth	w0, w0
	mov	w5, 65535
	cmp	w0, w5
	beq	.L1725
	adrp	x3, .LANCHOR2
	mov	w4, 6
	add	x8, x3, :lo12:.LANCHOR2
	adrp	x2, .LANCHOR4
	umull	x6, w0, w4
	mov	w1, -1
	add	x9, x2, :lo12:.LANCHOR4
	ldr	x7, [x8,2808]
	mov	x11, x3
	mov	x3, x2
	add	x12, x7, x6
	strh	w1, [x12,2]
	strh	w1, [x7,x6]
	ldr	x1, [x9,768]
	cbnz	x1, .L1726
	str	x12, [x9,768]
	b	.L1725
.L1726:
	ldr	x10, [x8,2696]
	ubfiz	x2, x0, 1, 16
	ldr	x9, [x8,2808]
	mov	x8, -6148914691236517206
	ldrh	w13, [x10,x2]
	sub	x2, x1, x9
	asr	x2, x2, 1
	madd	x2, x8, x2, x2
	uxth	w2, w2
.L1729:
	ubfiz	x8, x2, 1, 16
	ldrh	w8, [x10,x8]
	cmp	w8, w13
	bcs	.L1727
	ldrh	w8, [x1]
	cmp	w8, w5
	bne	.L1728
	strh	w2, [x12,2]
	strh	w0, [x1]
	b	.L1725
.L1728:
	umull	x1, w8, w4
	mov	w2, w8
	add	x1, x9, x1
	b	.L1729
.L1727:
	ldrh	w4, [x1,2]
	strh	w4, [x12,2]
	strh	w2, [x7,x6]
	add	x2, x3, :lo12:.LANCHOR4
	ldr	x3, [x2,768]
	cmp	x1, x3
	bne	.L1730
	strh	w0, [x1,2]
	str	x12, [x2,768]
	b	.L1725
.L1730:
	ldrh	w2, [x1,2]
	add	x3, x11, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3,2808]
	umull	x2, w2, w4
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L1725:
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
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w1, [x0,776]
	add	w1, w1, 1
	strh	w1, [x0,776]
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
	ldr	x2, [x6,2808]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L1733
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L1734
	str	xzr, [x0]
	b	.L1735
.L1734:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L1735
.L1733:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L1736
	cmp	w3, w0
	beq	.L1735
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L1735
.L1736:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,2808]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L1735:
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
	cbz	x2, .L1738
	adrp	x3, .LANCHOR2+2808
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+2808]
.L1739:
	cbnz	w1, .L1740
.L1742:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L1738
.L1740:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L1742
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L1739
.L1738:
	mov	w0, w19
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	uxth	w3, w0
	adrp	x0, .LANCHOR4+744
	ldr	x1, [x0,#:lo12:.LANCHOR4+744]
	mov	w0, 65535
	cbz	x1, .L1750
	adrp	x0, .LANCHOR2+2808
	mov	w5, 6
	ldr	x4, [x0,#:lo12:.LANCHOR2+2808]
	mov	w0, 65535
.L1746:
	cbz	w3, .L1747
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L1750
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L1746
.L1747:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L1750:
	ret
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	uxth	w20, w0
	add	x4, x19, :lo12:.LANCHOR4
	ldrh	w0, [x4,784]
	cmp	w0, w20
	beq	.L1752
	ldrh	w0, [x4,832]
	cmp	w0, w20
	beq	.L1752
	ldrh	w0, [x4,880]
	cmp	w0, w20
	beq	.L1752
	adrp	x3, .LANCHOR2
	mov	w2, 6
	add	x3, x3, :lo12:.LANCHOR2
	ldr	x0, [x4,744]
	umull	x2, w20, w2
	ldr	x5, [x3,2808]
	add	x1, x5, x2
	cmp	x1, x0
	beq	.L1752
	ubfiz	x0, x20, 1, 16
	ldr	x4, [x3,2712]
	ldrh	w6, [x1,4]
	ldrh	w1, [x1,2]
	ldrh	w3, [x4,x0]
	cmp	w6, wzr
	mov	w0, 65535
	mul	w3, w3, w6
	csinv	w3, w3, wzr, ne
	cmp	w1, w0
	bne	.L1754
	ldrh	w0, [x5,x2]
	cmp	w0, w1
	beq	.L1752
.L1754:
	mov	w0, 6
	umull	x1, w1, w0
	mov	x0, -6148914691236517206
	asr	x2, x1, 1
	add	x1, x5, x1
	madd	x2, x0, x2, x2
	ldrh	w2, [x4,x2,lsl 1]
	ldrh	w4, [x1,4]
	cmp	w4, wzr
	mul	w1, w2, w4
	csinv	w1, w1, wzr, ne
	cmp	w3, w1
	bcs	.L1752
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, w20
	add	x0, x19, 744
	bl	List_remove_node
	ldrh	w1, [x19,760]
	mov	w0, w20
	sub	w1, w1, #1
	strh	w1, [x19,760]
	bl	INSERT_DATA_LIST
.L1752:
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
.L1759:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1781
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1760:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L1782
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L1761
	cbz	w4, .L1761
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L1761:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1760
.L1782:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1759
.L1781:
	adrp	x0, .LANCHOR2+2346
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR2+2346]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L1764:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L1783
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L1765
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L1766
	b	.L1767
.L1765:
	cbnz	w1, .L1767
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L1767
.L1769:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L1767:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1764
.L1766:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L1769
	b	.L1767
.L1783:
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
.L1785:
	cmp	w20, w2
	beq	.L1789
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L1786
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1787
	adrp	x2, .LANCHOR2+2998
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	ldrh	w2, [x2,#:lo12:.LANCHOR2+2998]
	bl	printk
.L1788:
	b	.L1788
.L1787:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L1789
.L1786:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1785
.L1789:
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
	stp	x23, x24, [sp,48]
	adrp	x26, .LANCHOR2
	adrp	x23, .LC39
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w25, w1
	mov	x19, x0
	mov	x27, x2
	mov	w22, 0
	mov	w28, 65535
	add	x21, x26, :lo12:.LANCHOR2
	add	x23, x23, :lo12:.LC39
.L1792:
	add	x0, x26, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR2
	ldr	w1, [x0,2420]
	add	w1, w1, 1
	str	w1, [x0,2420]
	ldrh	w0, [x0,2346]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1793
	ldrh	w0, [x19]
	cmp	w0, w28
	bne	.L1794
.L1793:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1794:
	ldr	w1, [x21,2928]
	cbnz	w1, .L1795
	ldrh	w2, [x19]
	add	x3, x21, 2936
	ldr	x0, [x19,16]
	str	x3, [x29,104]
	ldrh	w24, [x0,x2,lsl 1]
	mov	w2, 16
	ldrh	w0, [x19,2]
	str	x27, [x21,2944]
	orr	w0, w0, w24, lsl 10
	str	w0, [x21,2940]
	ldr	x0, [x21,2640]
	str	x0, [x21,2952]
	bl	ftl_memset
	ldr	x3, [x29,104]
	ldr	w0, [x19,48]
	ldr	x2, [x3,16]
	str	w0, [x2,4]
	strh	w25, [x2,8]
	ldrh	w0, [x19,4]
	strh	w0, [x2]
	adrp	x0, .LANCHOR0+88
	strh	w24, [x2,2]
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L1796
	ldr	x0, [x3,8]
	ldrh	w1, [x21,2354]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	str	w0, [x2,12]
.L1796:
	add	x4, x21, 2936
	mov	w1, 1
	mov	w2, w1
	mov	x0, x4
	mov	w3, w1
	str	x4, [x29,104]
	bl	FlashProgPages
	ldrh	w1, [x19,2]
	ldr	x4, [x29,104]
	add	w1, w1, 1
	uxth	w1, w1
	strh	w1, [x19,2]
	ldr	w2, [x21,2936]
	cmn	w2, #1
	bne	.L1797
	ldr	w1, [x4,4]
	mov	x0, x23
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L1798
	ldrh	w0, [x21,2346]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L1798:
	cmp	w22, 3
	bls	.L1792
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x0, .LC40
	add	x0, x0, :lo12:.LC40
	mov	w2, w22
	ldr	w1, [x20,2940]
	bl	printk
	mov	w0, 1
	str	w0, [x20,2928]
	b	.L1795
.L1797:
	cbz	w2, .L1800
	strh	w24, [x19,60]
.L1800:
	cmp	w1, 1
	beq	.L1801
	cmp	w2, 256
	beq	.L1801
	ldr	w0, [x19,56]
	cbz	w0, .L1802
.L1801:
	str	wzr, [x19,56]
	b	.L1792
.L1802:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x19,40]
	ldr	w1, [x20,2940]
	str	w1, [x0,w25,uxtw 2]
.L1795:
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
	blt	.L1810
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x21,x1]
	cbz	w24, .L1810
	ldr	w0, [x19,52]
	cbnz	w0, .L1810
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x21,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,2346]
	cmp	w1, w0
	bcc	.L1811
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1811:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w21, 0
	add	x22, x26, 2936
.L1812:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L1821
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L1813
	ldr	x1, [x26,2584]
	ldr	x23, [x26,2640]
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
	bne	.L1814
.L1816:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x27]
	adrp	x0, .LC41
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC41
	ldr	w1, [x20,2940]
	bl	printk
	mov	w0, 1
	str	w0, [x20,2928]
	b	.L1815
.L1814:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L1816
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1816
	ldr	x2, [x22,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L1813:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1812
.L1821:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1810:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2346]
	cmp	w1, w0
	bcc	.L1815
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1815:
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
	add	x1, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	ldr	w20, [x1,2928]
	ldr	x22, [x0,16]
	ldr	x24, [x0,40]
	cbnz	w20, .L1823
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1824
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
	b	.L1823
.L1824:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldrh	w23, [x22,x0]
	ldr	x22, [x1,2640]
	str	x22, [x1,2952]
	orr	w2, w2, w23, lsl 10
	str	w2, [x1,2940]
	ldr	x2, [x1,2576]
	str	x2, [x1,2944]
	ldr	w0, [x19,48]
	str	w0, [x22,4]
	mov	w0, -1291
	strh	w0, [x22,8]
	ldrh	w0, [x19,4]
	strh	w0, [x22]
	strh	w23, [x22,2]
	ldrh	w2, [x1,2346]
	ldr	x0, [x1,2576]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L1825:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1832
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x24,x3]
	cmp	w23, w1, lsr 10
	bne	.L1826
	add	x2, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,2576]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,2576]
	ldr	w3, [x24,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L1826:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1825
.L1832:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L1828
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,2944]
	ldrh	w1, [x1,2354]
	bl	js_hash
	str	w0, [x22,12]
.L1828:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 2936
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L1823:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
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
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
	ldr	x1, [x20,2792]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,2792]
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
	mov	x3, 0
	add	x0, x0, :lo12:.LANCHOR2
	mov	w4, 65535
	ldrh	w2, [x0,2382]
	ldr	x1, [x0,2792]
.L1835:
	uxth	w0, w3
	cmp	w0, w2
	bcs	.L1851
	add	x3, x3, 1
	add	x5, x1, x3, lsl 4
	ldrh	w5, [x5,-16]
	cmp	w5, w4
	bne	.L1835
	b	.L1836
.L1851:
	mov	w0, w2
	mov	x3, 0
	mov	w6, -2147483648
.L1838:
	uxth	w5, w3
	cmp	w5, w2
	bcs	.L1852
	add	x4, x1, x3, lsl 4
	ldr	w4, [x4,4]
	tbnz	w4, #31, .L1847
	cmp	w4, w6
	bcc	.L1839
.L1847:
	mov	w4, w6
	mov	w5, w0
.L1839:
	add	x3, x3, 1
	mov	w6, w4
	mov	w0, w5
	b	.L1838
.L1852:
	cmp	w0, w2
	bcc	.L1836
	adrp	x0, .LANCHOR4+992
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,#:lo12:.LANCHOR4+992]
	mov	w0, w2
.L1842:
	cmp	w3, w2
	beq	.L1836
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L1843
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L1843:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L1842
.L1836:
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
	add	x4, x21, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w5, [x4,2924]
	ldrh	w3, [x4,2352]
	cmp	w0, w5
	bcs	.L1854
	add	w27, w3, 7
	str	x1, [x29,104]
	lsr	w28, w0, w27
	mov	w25, w0
	str	w2, [x29,100]
	mov	x0, 0
	uxth	w22, w28
	ldrh	w1, [x4,2382]
	ldr	x19, [x4,2792]
	b	.L1855
.L1854:
	mov	w0, -1
	cbnz	w2, .L1856
	str	w0, [x1]
	b	.L1856
.L1860:
	add	x0, x0, 1
	add	x2, x19, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w22
	beq	.L1857
.L1855:
	uxth	x20, w0
	cmp	w20, w1
	bcc	.L1860
	bl	select_l2p_ram_region
	uxth	x20, w0
	ubfiz	x26, x20, 4, 16
	mov	w2, 65535
	add	x1, x19, x26
	ldrh	w3, [x19,x26]
	cmp	w3, w2
	bne	.L1868
.L1861:
	add	x19, x21, :lo12:.LANCHOR2
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x19,2768]
	ldr	w24, [x1,x0]
	cbnz	w24, .L1862
	ldr	x0, [x19,2792]
	mov	w1, 255
	ldrh	w2, [x19,2354]
	add	x0, x0, x26
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x19,2792]
	strh	w22, [x0,x26]
	ldr	x0, [x19,2792]
	add	x26, x0, x26
	str	w24, [x26,4]
.L1857:
	mov	x1, 1
	lsl	x3, x1, x27
	ldr	w1, [x29,100]
	sub	w3, w3, #1
	and	w0, w25, w3
	uxth	x0, w0
	cbnz	w1, .L1858
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1,2792]
	add	x1, x1, x20, lsl 4
	ldr	x1, [x1,8]
	ldr	w0, [x1,x0,lsl 2]
	ldr	x1, [x29,104]
	str	w0, [x1]
	b	.L1859
.L1858:
	ldr	x2, [x29,104]
	add	x3, x21, :lo12:.LANCHOR2
	lsl	x1, x20, 4
	ldr	w4, [x2]
	ldr	x2, [x3,2792]
	add	x2, x2, x1
	ldr	x2, [x2,8]
	str	w4, [x2,x0,lsl 2]
	ldr	x0, [x3,2792]
	add	x0, x0, x1
	ldr	w1, [x0,4]
	orr	w1, w1, -2147483648
	str	w1, [x0,4]
	adrp	x0, .LANCHOR4+992
	strh	w22, [x0,#:lo12:.LANCHOR4+992]
.L1859:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21,2792]
	add	x20, x0, x20, lsl 4
	mov	w0, 0
	ldr	w1, [x20,4]
	cmn	w1, #1
	beq	.L1856
	add	w1, w1, 1
	str	w1, [x20,4]
	b	.L1856
.L1868:
	ldr	w1, [x1,4]
	tbz	w1, #31, .L1861
	bl	flush_l2p_region
	b	.L1861
.L1862:
	ldr	x0, [x19,2792]
	mov	w1, 1
	add	x23, x19, 2936
	mov	w2, w1
	add	x0, x0, x26
	str	w24, [x19,2940]
	ldr	x0, [x0,8]
	str	x0, [x19,2944]
	ldr	x0, [x19,2640]
	str	x0, [x19,2952]
	mov	x0, x23
	bl	FlashReadPages
	ldr	x0, [x19,2952]
	ldrh	w0, [x0,8]
	cmp	w0, w22
	beq	.L1863
	adrp	x0, .LC42
	uxth	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC42
	bl	printk
	mov	w2, 4
	ldr	x1, [x19,2952]
	adrp	x0, .LC18
	mov	w3, w2
	add	x0, x0, :lo12:.LC18
	bl	rknand_print_hex
	adrp	x0, .LC43
	ldr	x1, [x19,2768]
	ldrh	w3, [x19,2380]
	add	x0, x0, :lo12:.LC43
	mov	w2, 4
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x19,2928]
	b	.L1864
.L1863:
	ldr	w0, [x19,2936]
	cmp	w0, 256
	bne	.L1864
	and	w28, w28, 65535
	adrp	x0, .LC44
	mov	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC44
	bl	printk
	ldr	x2, [x19,2792]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, w28
	add	x2, x2, x26
	add	x0, x0, 928
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1864:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,2792]
	add	x0, x0, x26
	str	wzr, [x0,4]
	ldr	x0, [x1,2792]
	strh	w22, [x0,x26]
	b	.L1857
.L1856:
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
	stp	x29, x30, [sp, -176]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	mov	w23, w0
	add	x0, x24, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w21, w1
	add	w1, w23, w1
	ldrh	w22, [x0,2352]
	mov	x26, x2
	ldrh	w0, [x0,2340]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L1870
	adrp	x27, .LANCHOR4
	lsr	w22, w23, w22
	add	x27, x27, :lo12:.LANCHOR4
	mov	w20, 0
	add	x27, x27, 1000
.L1871:
	cbz	w21, .L1870
	add	x2, x24, :lo12:.LANCHOR2
	ldrh	w1, [x2,2350]
	ldr	x0, [x2,2760]
	udiv	w25, w23, w1
	ldr	w3, [x0,w22,uxtw 2]
	uxth	w0, w21
	msub	w25, w25, w1, w23
	sub	w19, w1, w25
	uxth	w19, w19
	cmp	w19, w21
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L1873
	cbz	w3, .L1873
	ldr	x0, [x2,2592]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w3, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L1874
.L1873:
	add	x2, x24, :lo12:.LANCHOR2
	mov	w1, 0
	ldr	x0, [x2,2592]
	ldrh	w2, [x2,2354]
	bl	ftl_memset
.L1874:
	add	x3, x24, :lo12:.LANCHOR2
	lsl	w28, w19, 9
	ubfiz	x25, x25, 9, 16
	mov	x1, x26
	mov	w2, w28
	str	x3, [x29,104]
	ldr	x0, [x3,2592]
	sub	w21, w21, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	add	x0, x0, x25
	bl	ftl_memcpy
	ldr	x3, [x29,104]
	mov	w1, w22
	mov	x0, x27
	add	w22, w22, 1
	ldr	x2, [x3,2592]
	bl	FtlMapWritePage
	cmn	w0, #1
	csinv	w20, w20, wzr, ne
	b	.L1871
.L1870:
	mov	w0, w20
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	stp	x29, x30, [sp, -192]!
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR2
	stp	x23, x24, [sp,48]
	mov	w23, w0
	add	x0, x25, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w22, w1
	mov	x27, x2
	add	w2, w23, w1
	ldrh	w1, [x0,2340]
	ldrh	w20, [x0,2352]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L1885
	adrp	x0, .LC45
	adrp	x26, .LANCHOR4
	add	x0, x0, :lo12:.LC45
	add	x26, x26, :lo12:.LANCHOR4
	str	x0, [x29,120]
	lsr	w20, w23, w20
	add	x0, x26, 1000
	mov	w21, 0
	str	x0, [x29,112]
.L1886:
	cbz	w22, .L1894
	add	x26, x25, :lo12:.LANCHOR2
	ldrh	w19, [x26,2350]
	ldr	x0, [x26,2760]
	udiv	w24, w23, w19
	ldr	w4, [x0,w20,uxtw 2]
	uxth	w0, w22
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w4, .L1888
	ldr	x0, [x26,2592]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w1
	add	x0, x29, 136
	str	w4, [x29,140]
	str	x4, [x29,104]
	str	xzr, [x29,152]
	bl	FlashReadPages
	ldr	w0, [x29,136]
	ldr	x4, [x29,104]
	cmn	w0, #1
	ldr	w0, [x26,2936]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L1890
	ldr	x0, [x29,120]
	mov	w1, w20
	mov	w2, w4
	bl	printk
	ldr	x0, [x29,112]
	mov	w1, w20
	ldr	x2, [x26,2592]
	bl	FtlMapWritePage
.L1890:
	add	x0, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	w2, w28
	ldr	x1, [x0,2592]
	mov	x0, x27
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L1891
.L1888:
	mov	x0, x27
	mov	w1, w4
	mov	w2, w28
	bl	ftl_memset
.L1891:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x27, x27, x28, sxtw
	b	.L1886
.L1894:
	mov	w0, w21
.L1885:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 192
	ret
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR4
	add	x3, x1, :lo12:.LANCHOR4
	ldrh	w2, [x3,1064]
	cbnz	w0, .L1896
	cmp	w2, 4
	bhi	.L1896
	add	w2, w2, 1
	strh	w2, [x3,1064]
	b	.L1895
.L1896:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w7, 65535
	strh	wzr, [x0,1064]
	str	wzr, [x0,1068]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w5, [x0,2284]
	ldr	x6, [x0,2712]
	mov	x0, 0
.L1897:
	cmp	w5, w0, uxth
	bls	.L1895
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L1898
	add	x3, x1, :lo12:.LANCHOR4
	ldr	w2, [x3,1068]
	add	w2, w4, w2
	str	w2, [x3,1068]
.L1898:
	add	x0, x0, 1
	b	.L1897
.L1895:
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
	cbz	w1, .L1901
	adrp	x1, .LANCHOR2
	str	wzr, [x0,56]
	add	x2, x1, :lo12:.LANCHOR2
	mov	x21, x1
	ldr	w3, [x2,2928]
	cbnz	w3, .L1901
	mov	x20, x0
	sub	w19, w19, #1
	add	x0, x2, 2936
	ldr	x3, [x2,2640]
	uxth	w19, w19
	ldr	x1, [x2,2584]
	str	x3, [x0,16]
	ubfiz	x3, x19, 2, 16
	str	x1, [x0,8]
	ldr	w3, [x4,x3]
	str	w3, [x0,4]
	cbz	w3, .L1905
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1906
.L1905:
	ldrh	w2, [x2,2354]
	mov	x0, x1
	mov	w1, 255
	bl	ftl_memset
.L1906:
	add	x2, x21, :lo12:.LANCHOR2
	mov	x0, x20
	mov	w1, w19
	ldr	x2, [x2,2944]
	bl	FtlMapWritePage
.L1901:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	stp	x29, x30, [sp, -96]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x27, [sp,80]
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w1, [x19,2928]
	cbnz	w1, .L1909
	ldr	x0, [x19,2576]
	adrp	x25, .LANCHOR4
	str	x0, [x19,2944]
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x21, [x19,2640]
	add	x20, x0, 696
	str	x21, [x19,2952]
	adrp	x23, .LANCHOR0
	ldrh	w3, [x0,1072]
	strh	w3, [x21,2]
	mov	w3, -3932
	strh	w3, [x21]
	str	w1, [x21,12]
	ldr	w3, [x0,1080]
	str	w1, [x21,8]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w3, [x21,4]
	ldrh	w2, [x0,784]
	str	w1, [x0,696]
	mov	w1, 99
	strh	w2, [x20,14]
	movk	w1, 0x5000, lsl 16
	ldrh	w2, [x0,786]
	ldrb	w3, [x0,790]
	str	w1, [x20,4]
	ldrh	w1, [x0,1078]
	orr	w2, w3, w2, lsl 6
	strh	w1, [x20,8]
	ldrh	w1, [x19,2298]
	strb	w1, [x20,10]
	ldrb	w1, [x0,792]
	strh	w2, [x20,16]
	strb	w1, [x20,11]
	ldrh	w2, [x0,832]
	ldrb	w1, [x0,840]
	strb	w1, [x20,12]
	add	x1, x0, 880
	strh	w2, [x20,18]
	ldrh	w2, [x0,834]
	ldrb	w3, [x0,838]
	ldrh	w0, [x0,880]
	orr	w2, w3, w2, lsl 6
	strh	w0, [x20,22]
	strh	w2, [x20,20]
	ldrh	w0, [x1,2]
	ldrb	w2, [x1,6]
	orr	w0, w2, w0, lsl 6
	strh	w0, [x20,24]
	ldrb	w0, [x1,8]
	mov	w1, 255
	strb	w0, [x20,13]
	ldr	w0, [x19,2428]
	str	w0, [x20,32]
	ldr	w0, [x19,2396]
	str	w0, [x20,40]
	ldr	w0, [x19,2400]
	str	w0, [x20,36]
	ldrh	w0, [x19,2472]
	strh	w0, [x20,44]
	ldrh	w0, [x19,2474]
	strh	w0, [x20,46]
	ldrh	w2, [x19,2354]
	ldr	x0, [x19,2944]
	bl	ftl_memset
	ldr	x0, [x19,2944]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	bl	ftl_memcpy
	ldr	x0, [x19,2944]
	ldrh	w2, [x19,2284]
	ldr	x1, [x19,2712]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,2284]
	add	x1, x23, :lo12:.LANCHOR0
	ldr	x3, [x19,2944]
	ubfiz	x0, x2, 1, 16
	lsr	w2, w2, 3
	add	x0, x0, 51
	ldr	x1, [x1,16]
	and	x0, x0, 262140
	add	w2, w2, 4
	add	x0, x3, x0
	bl	ftl_memcpy
	ldrh	w0, [x19,2384]
	cbz	w0, .L1910
	ldrh	w0, [x19,2284]
	ldrh	w2, [x19,2380]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19,2944]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,2768]
	bl	ftl_memcpy
.L1910:
	mov	w0, 0
	mov	w24, 0
	mov	w25, 65535
	add	x19, x22, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	bl	FtlUpdateVaildLpn
.L1911:
	ldrh	w1, [x20,1072]
	ldr	x0, [x19,2576]
	ldrh	w2, [x20,1074]
	str	x0, [x19,2944]
	ldr	x0, [x19,2640]
	str	x0, [x19,2952]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,2940]
	ldrh	w0, [x19,2346]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1912
	ldrh	w25, [x20,1076]
	strh	wzr, [x20,1074]
	strh	w1, [x20,1076]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,2396]
	str	w1, [x20,1080]
	add	w2, w1, 1
	str	w2, [x19,2396]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19,2940]
	strh	w0, [x20,1072]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L1912:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L1913
	ldr	x0, [x19,2576]
	ldrh	w1, [x19,2354]
	bl	js_hash
	str	w0, [x21,12]
.L1913:
	mov	w1, 1
	add	x0, x19, 2936
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x20,1074]
	ldr	w2, [x19,2936]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x20,1074]
	bne	.L1914
	cmp	w1, 1
	bne	.L1915
	ldrh	w1, [x19,2346]
	sub	w1, w1, #1
	strh	w1, [x20,1074]
.L1915:
	add	w24, w24, 1
	uxth	w24, w24
	cmp	w24, 3
	bls	.L1911
	add	x22, x22, :lo12:.LANCHOR2
	adrp	x0, .LC46
	add	x0, x0, :lo12:.LC46
	mov	w2, w24
	ldr	w1, [x22,2940]
	bl	printk
	mov	w0, 1
	str	w0, [x22,2928]
	b	.L1909
.L1914:
	cmp	w1, 1
	beq	.L1911
	cmp	w2, 256
	beq	.L1911
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1909
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1909:
	mov	w0, 0
	ldr	x27, [sp,80]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x20, x22, :lo12:.LANCHOR2
	adrp	x19, .LANCHOR4
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x21, x19, :lo12:.LANCHOR4
	strh	wzr, [x20,2920]
	ldr	w2, [x20,2372]
	mov	x24, x22
	ldr	x0, [x20,2784]
	mov	w22, 65535
	lsl	w2, w2, 2
	strh	wzr, [x21,1088]
	bl	ftl_memset
	ldr	w2, [x20,2372]
	mov	w1, 0
	ldr	x0, [x20,2720]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2364]
	mov	w1, 0
	ldr	x0, [x20,2752]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2364]
	mov	w1, 0
	ldr	x0, [x20,2736]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 1072
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,2284]
.L1925:
	add	x1, x24, :lo12:.LANCHOR2
	ldrh	w0, [x1,2286]
	cmp	w0, w21
	bls	.L1966
	mov	x27, 0
	ldrh	w4, [x1,2276]
	ldrh	w2, [x1,2356]
	mov	w20, w27
	mov	x23, x1
.L1967:
	cmp	w4, w27, uxth
	bls	.L2000
	add	x0, x23, 2304
	mov	w1, w21
	str	x2, [x29,112]
	str	x4, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x4, [x29,120]
	ldr	x2, [x29,112]
	cbnz	w0, .L1926
	mov	w0, 56
	ldr	x1, [x23,2520]
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x23,2520]
	ldr	x5, [x23,2656]
	add	x0, x1, x0
	ldr	x1, [x23,2600]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L1926:
	add	x27, x27, 1
	b	.L1967
.L2000:
	cbz	w20, .L1929
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2520]
	bl	FlashReadPages
.L1930:
	cmp	w20, w23, uxth
	bls	.L1929
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1932
	mov	w7, 16
.L1931:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,2520]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,2520]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,2520]
	bne	.L1933
	str	w27, [x0,x6]
	b	.L1932
.L1933:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1932
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1931
.L1932:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2520]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1935
	ldr	w0, [x0,2396]
	cmn	w0, #1
	beq	.L1936
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1937
.L1936:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1937
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,2396]
.L1937:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1939
	bhi	.L1940
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1938
	add	x6, x24, :lo12:.LANCHOR2
	ldrh	w7, [x6,2364]
	ldrh	w0, [x6,2920]
	sub	w2, w7, #1
	ldr	x6, [x6,2752]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1953
.L1940:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1942
	cmp	w0, w22
	bne	.L1938
	mov	w0, w25
	b	.L1999
.L1942:
	add	x7, x24, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w6, [x7,2372]
	ldrh	w0, [x0,1088]
	ldr	x7, [x7,2784]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1944:
	cmp	w1, w2
	ble	.L2001
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1945
	ldr	w2, [x7]
	cbnz	w2, .L1946
	cmp	w0, w6
	beq	.L1946
	add	x2, x19, :lo12:.LANCHOR4
	add	w0, w0, 1
	strh	w0, [x2,1088]
.L1946:
	mov	w0, 0
.L1947:
	cmp	w0, w1
	beq	.L2002
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2784]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2720]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1947
.L2002:
	add	x2, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x0, [x2,2784]
	str	w6, [x0,x8]
	ldr	x0, [x2,2720]
	strh	w25, [x0,x9,lsl 1]
	tbz	w1, #31, .L1949
	b	.L1938
.L1945:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1944
.L2001:
	tbz	w1, #31, .L1982
	b	.L1938
.L1949:
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w2, [x2,2372]
	ldrh	w0, [x0,1088]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1938
.L1982:
	add	x2, x19, :lo12:.LANCHOR4
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,1088]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x2, [x0,2784]
	str	w4, [x2,x1,lsl 2]
	ldr	x0, [x0,2720]
	b	.L1998
.L1959:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x6,x8]
	cmp	w11, w10
	bhi	.L2003
	sub	w1, w1, #1
	sxth	x1, w1
.L1953:
	cmp	w1, w2
	bgt	.L1959
	b	.L1958
.L2003:
	ldr	w2, [x6]
	cbnz	w2, .L1955
	cmp	w0, w7
	beq	.L1955
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2920]
.L1955:
	mov	w0, 0
.L1956:
	cmp	w0, w1
	beq	.L2004
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2752]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2736]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1956
.L2004:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2752]
	str	w6, [x2,x8]
	ldr	x0, [x0,2736]
	strh	w25, [x0,x9,lsl 1]
.L1958:
	tbnz	w1, #31, .L1938
	add	x0, x24, :lo12:.LANCHOR2
	ldrh	w2, [x0,2364]
	ldrh	w6, [x0,2920]
	sub	w2, w2, #1
	sub	w2, w2, w6
	cmp	w1, w2, sxth
	bgt	.L1938
	ldr	x2, [x0,2752]
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x0,2920]
	str	w4, [x2,x1,lsl 2]
	ldr	x0, [x0,2736]
.L1998:
	strh	w25, [x0,x1,lsl 1]
	b	.L1938
.L1939:
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x0, 1072
	ldrh	w2, [x0,1072]
	cmp	w2, w22
	bne	.L1960
	strh	w25, [x0,1072]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1938
.L1960:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1961
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1961:
	add	x1, x19, :lo12:.LANCHOR4
	ldr	w2, [x26,4]
	add	x0, x1, 1072
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1962
	ldrh	w2, [x1,1072]
	strh	w2, [x0,4]
	strh	w25, [x1,1072]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1938
.L1962:
	strh	w25, [x0,4]
	b	.L1938
.L1935:
	ldrb	w1, [x0,1220]
	mov	w0, w25
	cbz	w1, .L1963
.L1999:
	mov	w1, 0
.L1963:
	bl	FtlFreeSysBlkQueueIn
.L1938:
	add	x23, x23, 1
	b	.L1930
.L1929:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1925
.L1966:
	ldr	x2, [x1,2720]
	ldrh	w0, [x2]
	cbz	w0, .L1968
.L1971:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	x2, [x1,2736]
	ldrh	w0, [x2]
	cbz	w0, .L1969
	b	.L1992
.L1968:
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w4, [x19,1088]
	cbz	w4, .L1971
	ldr	w1, [x1,2372]
.L1972:
	cmp	w0, w1
	bcs	.L1971
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1973
	mov	w7, w0
.L1974:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x1,2372]
	cmp	w0, w2
	bcs	.L1971
	sxtw	x6, w0
	sub	w2, w0, w7
	ldr	x5, [x1,2720]
	lsl	x4, x6, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x2,lsl 1]
	ldr	x5, [x1,2784]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x2,lsl 2]
	ldr	x1, [x1,2720]
	strh	wzr, [x1,x4]
	b	.L1974
.L1973:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1972
.L1969:
	ldrh	w4, [x1,2920]
	cbz	w4, .L1992
	ldrh	w1, [x1,2364]
.L1977:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1992
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1978
.L1979:
	add	x1, x24, :lo12:.LANCHOR2
	ldrh	w2, [x1,2364]
	cmp	w0, w2
	bge	.L1992
	sxtw	x6, w0
	sub	w2, w0, w7
	ldr	x5, [x1,2736]
	lsl	x4, x6, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x2,lsl 1]
	ldr	x5, [x1,2752]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x2,lsl 2]
	ldr	x1, [x1,2736]
	strh	wzr, [x1,x4]
	b	.L1979
.L1978:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1977
.L1992:
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
	ldrh	w1, [x19,2672]
	ldr	x2, [x19,2688]
	bl	FtlVendorPartRead
	ldr	x0, [x19,2688]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L2006
	adrp	x0, .LC47
	adrp	x1, .LC48
	add	x1, x1, :lo12:.LC48
	add	x0, x0, :lo12:.LC47
	bl	printk
	ldrh	w2, [x19,2672]
	mov	w1, 0
	ldr	x0, [x19,2688]
	lsl	w2, w2, 9
	bl	ftl_memset
.L2006:
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
	cbz	w1, .L2008
	bl	ftl_set_blk_mode.part.17
	b	.L2007
.L2008:
	adrp	x1, .LANCHOR0+16
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L2007:
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
	stp	x29, x30, [sp, -128]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC49
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 152
	add	x0, x0, :lo12:.LC49
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR4
	mov	w19, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x21, x20, :lo12:.LANCHOR4
	bl	printk
	add	x0, x21, 1096
	mov	w1, 0
	mov	x2, 8192
	adrp	x22, .LANCHOR2
	bl	memset
.L2012:
	add	x0, x22, :lo12:.LANCHOR2
	adrp	x23, .LANCHOR2
	ldr	w0, [x0,2924]
	cmp	w19, w0
	bcs	.L2031
	mov	w0, w19
	add	x1, x29, 124
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,124]
	cmn	w0, #1
	beq	.L2013
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, 1096
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L2013:
	add	w19, w19, 1
	b	.L2012
.L2031:
	mov	w21, 0
	adrp	x24, .LC50
	mov	w19, w21
	add	x27, x20, :lo12:.LANCHOR4
	add	x24, x24, :lo12:.LC50
	mov	w28, 65535
.L2015:
	add	x22, x23, :lo12:.LANCHOR2
	ldrh	w0, [x22,2284]
	cmp	w0, w19
	bls	.L2032
	ubfiz	x26, x19, 1, 16
	ldr	x0, [x22,2712]
	add	x25, x27, 1096
	sxtw	x4, w19
	ldrh	w2, [x0,x26]
	ldrh	w3, [x25,w19,sxtw 1]
	cmp	w2, w3
	beq	.L2016
	mov	x0, x24
	mov	w1, w19
	str	x4, [x29,104]
	bl	printk
	ldr	x0, [x22,2712]
	ldrh	w0, [x0,x26]
	cmp	w0, w28
	beq	.L2016
	ldr	x4, [x29,104]
	ldrh	w1, [x25,x4,lsl 1]
	cmp	w1, w0
	csinc	w21, w21, wzr, ls
.L2016:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2015
.L2032:
	add	x2, x20, :lo12:.LANCHOR4
	ldr	x0, [x2,768]
	cbz	x0, .L2018
	ldr	x19, [x22,2808]
	adrp	x24, .LC51
	ldrh	w25, [x2,776]
	mov	w22, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x24, x24, :lo12:.LC51
	mov	x20, x2
	mov	w26, 6
	madd	x19, x0, x19, x19
	mov	w27, 65535
	uxth	w19, w19
.L2019:
	cmp	w22, w25
	beq	.L2018
	add	x1, x23, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,2712]
	ldrh	w2, [x1,x0]
	cbz	w2, .L2020
	add	x3, x20, 1096
	mov	x0, x24
	mov	w1, w19
	mov	w21, 1
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L2020:
	add	x0, x23, :lo12:.LANCHOR2
	umull	x19, w19, w26
	ldr	x0, [x0,2808]
	ldrh	w19, [x0,x19]
	cmp	w19, w27
	beq	.L2018
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2019
.L2018:
	adrp	x0, .LC52
	mov	w1, w21
	add	x0, x0, :lo12:.LC52
	bl	printk
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
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
	add	x19, x2, 2936
	uxth	w23, w0
	adrp	x22, .LC53
	ldr	x0, [x2,2576]
	adrp	x25, .LC54
	str	x0, [x19,8]
	lsl	w24, w23, 10
	ldr	x0, [x2,2640]
	mov	w20, 0
	str	x0, [x19,16]
	mov	x21, x2
	add	x22, x22, :lo12:.LC53
	add	x25, x25, :lo12:.LC54
.L2034:
	ldrh	w0, [x21,2346]
	cmp	w20, w0
	bge	.L2040
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
	beq	.L2035
	ldr	x1, [x21,2576]
	mov	x0, x25
	mov	w2, 4
	mov	w3, 768
	bl	rknand_print_hex
.L2035:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L2034
.L2040:
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
	adrp	x0, .LC55
	add	x0, x0, :lo12:.LC55
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	adrp	x23, .LANCHOR2
	stp	x21, x22, [sp,48]
	adrp	x21, .LC56
	add	x22, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp,32]
	add	x21, x21, :lo12:.LC56
	mov	w20, 0
	add	x19, x22, 2936
	bl	printk
.L2042:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,2924]
	cmp	w20, w0
	bcs	.L2049
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L2043
	ldr	w2, [x29,76]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L2043:
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2045
	str	w0, [x19,4]
	mov	w1, 1
	ldr	x0, [x22,2576]
	mov	w2, 0
	str	x0, [x19,8]
	mov	x0, x19
	ldr	x24, [x22,2640]
	str	w20, [x19,24]
	str	x24, [x19,16]
	str	wzr, [x19]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L2046
	cmp	w0, 256
	beq	.L2046
	ldr	w0, [x24,8]
	cmp	w0, w20
	beq	.L2045
.L2046:
	ldr	x7, [x19,8]
	mov	w1, w20
	ldr	x6, [x19,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC57
	add	x0, x0, :lo12:.LC57
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L2045:
	add	w20, w20, 1
	b	.L2042
.L2049:
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
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	stp	x25, x26, [sp,80]
	stp	x23, x24, [sp,64]
	stp	x27, x28, [sp,96]
	stp	x21, x22, [sp,48]
	adrp	x24, .LC58
	mov	w25, 56
	ldrh	w22, [x0,2284]
	mov	w26, 4
	mov	x27, 56
	add	x24, x24, :lo12:.LC58
.L2051:
	add	x20, x19, :lo12:.LANCHOR2
	ldrh	w0, [x20,2286]
	cmp	w0, w22
	bls	.L2064
	mov	x23, 0
	ldrh	w3, [x20,2276]
	ldrh	w2, [x20,2356]
	mov	w21, w23
.L2060:
	cmp	w3, w23, uxth
	bls	.L2065
	add	x0, x20, 2304
	mov	w1, w22
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,x23]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L2052
	umull	x0, w21, w25
	ldr	x1, [x20,2520]
	lsl	w28, w28, 10
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x20,2520]
	ldr	x4, [x20,2656]
	add	x0, x1, x0
	ldr	x1, [x20,2600]
	str	x1, [x0,8]
	mul	w1, w21, w2
	add	w21, w21, 1
	sdiv	w1, w1, w26
	uxth	w21, w21
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2052:
	add	x23, x23, 1
	b	.L2060
.L2065:
	cbz	w21, .L2055
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, 1
	mov	x20, 0
	ldr	x0, [x0,2520]
	bl	FlashReadPages
.L2056:
	cmp	w21, w20, uxth
	bls	.L2055
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	madd	x0, x20, x27, x0
	add	x20, x20, 1
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
	b	.L2056
.L2055:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2051
.L2064:
	adrp	x23, .LC59
	mov	w22, 0
	adrp	x26, .LANCHOR4
	add	x25, x20, 2936
	add	x23, x23, :lo12:.LC59
.L2059:
	add	x1, x26, :lo12:.LANCHOR4
	ldrh	w1, [x1,1088]
	cmp	w22, w1
	bge	.L2062
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L2063:
	ldrh	w0, [x20,2346]
	cmp	w21, w0
	bge	.L2066
	ldr	x0, [x20,2720]
	ldrh	w1, [x0,x24]
	mov	x0, x25
	orr	w1, w21, w1, lsl 10
	str	w1, [x25,4]
	mov	w1, 1
	add	w21, w21, 1
	mov	w2, w1
	sxth	w21, w21
	bl	FlashReadPages
	ldr	x2, [x25,8]
	ldr	x0, [x20,2720]
	ldr	x7, [x25,16]
	ldrh	w1, [x0,x24]
	ldr	w0, [x2]
	str	w0, [sp]
	ldr	w0, [x2,4]
	str	w0, [sp,8]
	mov	x0, x23
	ldr	w4, [x7]
	ldr	w5, [x7,4]
	ldr	w6, [x7,8]
	ldr	w2, [x25]
	ldr	w3, [x25,4]
	ldr	w7, [x7,12]
	bl	printk
	b	.L2063
.L2066:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L2059
.L2062:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC60
	add	x0, x0, :lo12:.LC60
	mov	w2, 2
	ldr	x1, [x19,2720]
	ldr	w3, [x19,2372]
	bl	rknand_print_hex
	ldr	x1, [x19,2768]
	adrp	x0, .LC61
	ldrh	w3, [x19,2380]
	add	x0, x0, :lo12:.LC61
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x19,2776]
	adrp	x0, .LC62
	ldrh	w3, [x19,2380]
	add	x0, x0, :lo12:.LC62
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
	stp	x29, x30, [sp, -144]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x23, x24, [sp,48]
	mov	x27, x0
	adrp	x20, .LANCHOR2
	ldr	x22, [x0,40]
	mov	w19, 0
	ldrh	w24, [x0,6]
	ldr	x23, [x0,16]
	ldr	x26, [x0,24]
	lsl	w2, w24, 2
	ldrh	w0, [x0,8]
	str	w0, [x29,136]
	mov	x0, x22
	bl	ftl_memset
	str	x20, [x29,128]
	add	x6, x20, :lo12:.LANCHOR2
	str	wzr, [x27,52]
	add	x4, x6, 2936
	str	wzr, [x27,48]
	mov	x20, x6
	mov	x25, x4
	ldr	x0, [x6,2576]
	ldr	x21, [x6,2640]
	str	x0, [x6,2944]
	mov	w0, -1
	str	x21, [x6,2952]
	strh	w0, [x27]
	strh	w0, [x27,2]
	mov	w0, 1
	str	w0, [x27,56]
	ldr	w0, [x29,136]
	sub	w0, w0, #1
	str	w0, [x29,116]
.L2068:
	ldr	w0, [x29,136]
	cmp	w19, w0
	bge	.L2086
	ldr	w0, [x29,116]
	sxtw	x28, w19
	cmp	w19, w0
	bne	.L2069
	lsl	x0, x28, 1
	mov	w1, 1
	add	x25, x23, x0
	mov	w20, 0
	ldrh	w0, [x23,x0]
	bl	FtlGetLastWrittenPage
	strh	w19, [x27]
	sxth	w1, w0
	add	w0, w0, 1
	strh	w0, [x27,2]
	add	w19, w1, 1
	ldr	w0, [x26,x28,lsl 2]
	adrp	x26, .LANCHOR0
	str	w0, [x27,48]
	ldr	x0, [x29,128]
	add	x23, x0, :lo12:.LANCHOR2
	add	x28, x23, 2936
.L2070:
	cmp	w20, w19
	bge	.L2086
	ldrh	w0, [x25]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w20, w0, lsl 10
	str	w0, [x28,4]
	mov	x0, x28
	bl	FlashReadPages
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2071
	ldr	x0, [x28,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2071
	ldr	x0, [x28,8]
	ldrh	w1, [x23,2354]
	str	x2, [x29,136]
	bl	js_hash
	ldr	x2, [x29,136]
	cmp	w2, w0
	beq	.L2071
	mov	w0, -1
	str	w0, [x28]
.L2071:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L2072
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2072
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2072
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28,4]
	str	w1, [x22,x0]
.L2072:
	add	w4, w20, 1
	sxth	w20, w4
	b	.L2070
.L2086:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	ldr	x0, [x29,128]
	ldrh	w1, [x27,2]
	add	x20, x0, :lo12:.LANCHOR2
	ldrh	w0, [x20,2346]
	cmp	w1, w0
	bne	.L2075
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2075
.L2069:
	ldr	x0, [x20,2576]
	str	x0, [x25,8]
	lsl	x0, x28, 1
	add	x1, x23, x0
	str	x1, [x29,120]
	ldrh	w1, [x23,x0]
	ldrh	w0, [x20,2346]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x25,4]
	mov	w2, w1
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2087
	ldrh	w1, [x21]
	mov	w28, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2108
	ldrh	w1, [x21,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2108
	mov	w5, w28
.L2077:
	ldrh	w0, [x20,2346]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2080
	ldr	x2, [x20,2576]
	sbfiz	x1, x5, 3, 32
	ldrh	w0, [x2,x1]
	cmp	w0, w24
	bcs	.L2078
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x1,4]
	str	w1, [x22,x0]
.L2078:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2077
.L2087:
	mov	w28, 0
.L2108:
	ldrh	w0, [x20,2346]
	cmp	w28, w0
	bge	.L2080
	ldr	x0, [x29,120]
	mov	w1, 1
	mov	w2, w1
	ldrh	w0, [x0]
	orr	w0, w28, w0, lsl 10
	str	w0, [x25,4]
	mov	x0, x25
	bl	FlashReadPages
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2081
	ldr	x0, [x25,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2081
	ldr	x0, [x25,8]
	ldrh	w1, [x20,2354]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2081
	mov	w0, -1
	str	w0, [x25]
.L2081:
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2082
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2082
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2082
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25,4]
	str	w1, [x22,x0]
.L2082:
	add	w5, w28, 1
	sxth	w28, w5
	b	.L2108
.L2080:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2068
.L2075:
	mov	x0, x27
	bl	ftl_map_blk_gc
	mov	x0, x27
	bl	ftl_map_blk_gc
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	adrp	x0, .LANCHOR4
	adrp	x1, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	add	x1, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x0, x0, 1000
	add	x29, sp, 0
	ldrh	w2, [x1,2364]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2920]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2366]
	strh	w2, [x0,6]
	ldr	x2, [x1,2736]
	str	x2, [x0,16]
	ldr	x2, [x1,2752]
	str	x2, [x0,24]
	ldr	x2, [x1,2744]
	ldr	x1, [x1,2760]
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
	ldr	w2, [x19,2372]
	ldr	x0, [x19,2728]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x19,2354]
	mov	w1, 255
	ldrh	w2, [x19,2382]
	ldr	x0, [x19,2800]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w2, 0
	mov	w4, -1
.L2111:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,2382]
	cmp	w1, w2
	bls	.L2113
	ldr	x3, [x0,2792]
	ubfiz	x1, x2, 4, 16
	add	x3, x3, x1
	str	wzr, [x3,4]
	ldr	x3, [x0,2792]
	strh	w4, [x3,x1]
	ldr	x3, [x0,2792]
	add	x3, x3, x1
	ldrh	w1, [x0,2354]
	ldr	x0, [x0,2800]
	mul	w1, w1, w2
	add	w2, w2, 1
	sxtw	x1, w1
	uxth	w2, w2
	and	x1, x1, -4
	add	x0, x0, x1
	str	x0, [x3,8]
	b	.L2111
.L2113:
	adrp	x2, .LANCHOR4
	mov	w3, -1
	add	x2, x2, :lo12:.LANCHOR4
	add	x1, x2, 928
	strh	w3, [x2,930]
	strh	w3, [x2,928]
	ldr	w3, [x0,2372]
	strh	w3, [x2,938]
	mov	w3, -3902
	strh	w3, [x2,932]
	ldrh	w2, [x2,1088]
	strh	w2, [x1,8]
	ldrh	w2, [x0,2380]
	strh	w2, [x1,6]
	ldr	x2, [x0,2720]
	str	x2, [x1,16]
	ldr	x2, [x0,2784]
	str	x2, [x1,24]
	ldr	x2, [x0,2728]
	ldr	x0, [x0,2768]
	str	x2, [x1,32]
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
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 928
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
	ldrh	w4, [x4,2276]
.L2116:
	cmp	w2, w4
	bcs	.L2122
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L2117
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L2117:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2116
.L2122:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2344]
	mov	x3, x0
.L2119:
	cmp	x3, x4
	beq	.L2123
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L2120
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L2120:
	add	x3, x3, 2
	b	.L2119
.L2123:
	ret
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	w20, w0
	lsr	x0, x1, 10
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	str	w1, [x29,76]
	bl	P2V_block_in_plane
	adrp	x2, .LANCHOR2
	uxth	w0, w0
	add	x4, x2, :lo12:.LANCHOR2
	ubfiz	x23, x0, 1, 16
	mov	x22, x2
	ldr	x3, [x4,2712]
	ldrh	w1, [x3,x23]
	cbnz	w1, .L2125
	adrp	x21, .LANCHOR4
	add	x2, x21, :lo12:.LANCHOR4
	ldr	x19, [x2,768]
	cbz	x19, .L2126
	ldrh	w3, [x2,776]
	mov	w5, 65535
	ldr	x2, [x4,2808]
	mov	x4, -6148914691236517206
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2127:
	cmp	w1, w3
	beq	.L2126
	cmp	w19, w0
	bne	.L2128
	add	x21, x21, :lo12:.LANCHOR4
	mov	w1, w19
	add	x0, x21, 768
	bl	List_remove_node
	ldrh	w0, [x21,776]
	sub	w0, w0, #1
	strh	w0, [x21,776]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	add	x2, x22, :lo12:.LANCHOR2
	ldr	x1, [x2,2712]
	ldrh	w0, [x1,x23]
	add	w0, w0, 1
	strh	w0, [x1,x23]
	b	.L2126
.L2128:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2126
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2127
.L2125:
	add	w1, w1, 1
	strh	w1, [x3,x23]
.L2126:
	add	x1, x29, 76
	mov	w0, w20
	mov	w2, 1
	bl	log2phys
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 80
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
	ldrh	w24, [x1,2276]
.L2134:
	cmp	w24, w21, uxth
	bls	.L2145
	add	x0, x23, 2304
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2135
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L2135:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L2134
.L2145:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x0,2344]
	mul	w1, w1, w2
	strh	w1, [x19,4]
	ldr	w1, [x0,2096]
	cbz	w1, .L2137
	ldrh	w1, [x19]
	ldr	x0, [x0,2696]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L2137
	mov	w0, 1
	strb	w0, [x19,9]
.L2137:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L2138
	mov	w0, 1
	strb	w0, [x19,9]
.L2138:
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
	sub	sp, sp, #128
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x21, x22, [sp,48]
	adrp	x22, .LANCHOR2
	stp	x23, x24, [sp,64]
	add	x23, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,32]
	add	x20, x23, 2936
	adrp	x19, .LANCHOR4
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x23,2576]
	add	x24, x19, :lo12:.LANCHOR4
	str	x0, [x20,8]
	add	x25, x24, 1072
	ldr	x0, [x23,2640]
	ldrh	w2, [x23,2284]
	str	x0, [x20,16]
	ldr	x0, [x23,2712]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x24,1072]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2147
.L2158:
	mov	w19, -1
	b	.L2148
.L2147:
	mov	w1, 1
	adrp	x26, .LC63
	add	x26, x26, :lo12:.LC63
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	ldrsh	w25, [x24,1072]
.L2149:
	tbnz	w21, #31, .L2157
	orr	w0, w21, w25, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	ldr	x0, [x23,2576]
	str	x0, [x20,8]
	mov	x0, x20
	bl	FlashReadPages
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2150
	ldr	x24, [x20,16]
	ldr	w7, [x24,12]
	cbz	w7, .L2150
	ldr	x28, [x20,8]
	ldrh	w1, [x23,2354]
	mov	x0, x28
	str	x7, [x29,104]
	bl	js_hash
	ldr	x7, [x29,104]
	cmp	w7, w0
	beq	.L2150
	cbnz	w21, .L2151
	add	x0, x19, :lo12:.LANCHOR4
	add	x27, x0, 1072
	ldrh	w1, [x0,1076]
	cmp	w25, w1
	beq	.L2151
	ldrh	w1, [x0,1072]
	ldr	w0, [x28]
	str	w0, [sp]
	mov	x0, x26
	ldr	w2, [x20]
	ldr	w3, [x20,4]
	ldr	w4, [x24]
	ldr	w5, [x24,4]
	ldr	w6, [x24,8]
	bl	printk
	ldrsh	w25, [x27,4]
	ldrh	w21, [x23,2346]
	b	.L2153
.L2151:
	mov	w0, -1
	str	w0, [x20]
.L2150:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L2153
	ldr	x0, [x23,2576]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L2153
	ldr	x0, [x23,2640]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L2153
.L2157:
	add	x20, x22, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR4
	add	x21, x20, 2936
	mov	w2, 48
	add	x0, x0, 696
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x20,2284]
	ldr	x0, [x20,2712]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x20,2284]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x3, [x21,8]
	ubfiz	x1, x2, 1, 16
	lsr	w2, w2, 3
	add	x1, x1, 51
	add	w2, w2, 4
	and	x1, x1, 262140
	ldr	x0, [x0,16]
	add	x1, x3, x1
	bl	ftl_memcpy
	ldrh	w0, [x20,2384]
	cbz	w0, .L2155
	ldrh	w0, [x20,2284]
	ldr	x3, [x21,8]
	lsr	w1, w0, 3
	ldrh	w2, [x20,2380]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x20,2776]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L2155
.L2153:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L2149
.L2155:
	add	x6, x19, :lo12:.LANCHOR4
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	ldr	w2, [x6,696]
	cmp	w2, w0
	bne	.L2158
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w3, [x6,706]
	ldrh	w5, [x6,704]
	strh	w5, [x6,1078]
	ldrh	w2, [x0,2298]
	cmp	w3, w2
	bne	.L2158
	ldrh	w3, [x0,2344]
	adrp	x7, .LANCHOR5
	ldrh	w2, [x0,2350]
	add	x4, x7, :lo12:.LANCHOR5
	mov	x20, x7
	mul	w3, w3, w5
	str	w3, [x0,2924]
	str	w5, [x4,584]
	mul	w3, w3, w2
	str	w3, [x0,1224]
	ldrh	w2, [x0,2830]
	ldr	w3, [x0,2288]
	strh	wzr, [x4,594]
	sub	w2, w3, w2
	ldrh	w3, [x0,2276]
	sub	w2, w2, w5
	strb	wzr, [x4,598]
	strb	wzr, [x4,600]
	udiv	w2, w2, w3
	mov	w3, -1
	strh	w2, [x4,588]
	strh	w3, [x4,592]
	ldrh	w5, [x6,710]
	strh	w5, [x6,784]
	ldrh	w2, [x6,712]
	lsr	w8, w2, 6
	and	w2, w2, 63
	strb	w2, [x6,790]
	ldrb	w2, [x6,707]
	strb	w2, [x6,792]
	ldrh	w2, [x6,714]
	strh	w2, [x6,832]
	ldrh	w2, [x6,716]
	strh	w8, [x6,786]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x6,838]
	ldrb	w2, [x6,708]
	strb	w2, [x6,840]
	ldrh	w2, [x6,718]
	strh	w2, [x6,880]
	ldrh	w2, [x6,720]
	strh	w4, [x6,834]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strh	w4, [x6,882]
	strb	w2, [x6,886]
	ldrb	w2, [x6,709]
	strb	w2, [x6,888]
	ldr	w2, [x6,728]
	str	w2, [x0,2428]
	ldr	w1, [x0,2396]
	ldr	w2, [x6,736]
	str	wzr, [x0,2404]
	str	wzr, [x0,2408]
	cmp	w2, w1
	str	wzr, [x0,2424]
	str	wzr, [x0,2420]
	str	wzr, [x0,2432]
	str	wzr, [x0,2440]
	str	wzr, [x0,2416]
	bls	.L2159
	str	w2, [x0,2396]
.L2159:
	add	x0, x19, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,732]
	ldr	w1, [x22,2400]
	cmp	w0, w1
	bls	.L2160
	str	w0, [x22,2400]
.L2160:
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2161
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 784
	bl	make_superblock
.L2161:
	add	x2, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x0, x2, 832
	ldrh	w2, [x2,832]
	cmp	w2, w1
	beq	.L2162
	bl	make_superblock
.L2162:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x0, x19, 880
	ldrh	w2, [x19,880]
	cmp	w2, w1
	beq	.L2163
	bl	make_superblock
.L2163:
	add	x1, x20, :lo12:.LANCHOR5
	mov	w19, 0
	add	x0, x1, 592
	ldrh	w2, [x1,592]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2148
	bl	make_superblock
.L2148:
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
	adrp	x0, .LC49
	add	x1, x1, 168
	add	x0, x0, :lo12:.LC49
	ldrh	w23, [x19,2344]
	bl	printk
	ldr	x3, [x19,2712]
	ubfiz	x2, x22, 1, 16
	adrp	x0, .LC64
	mov	w1, w22
	add	x0, x0, :lo12:.LC64
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w22, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w24, .L2183
	ldrb	w0, [x19,1220]
	cbz	w0, .L2183
	mov	w0, w22
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w22, w0
	bne	.L2175
	ldrh	w23, [x19,2346]
	b	.L2175
.L2183:
	mov	w22, 0
.L2175:
	add	x3, x20, :lo12:.LANCHOR2
	adrp	x0, .LC65
	adrp	x24, .LC59
	add	x0, x0, :lo12:.LC65
	mov	w1, w22
	mov	w2, w23
	ldrh	w3, [x3,2344]
	mov	w21, 0
	mov	w26, 65535
	mov	w27, 56
	mov	w25, 4
	add	x24, x24, :lo12:.LC59
	bl	printk
.L2176:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x3, 0
	mov	w19, w3
	ldrh	w5, [x0,2276]
	ldrh	w6, [x0,2354]
	ldrh	w7, [x0,2356]
.L2177:
	cmp	w5, w3, uxth
	bls	.L2191
	add	x0, x29, 112
	add	x0, x0, x3, lsl 1
	ldrh	w1, [x0,16]
	cmp	w1, w26
	beq	.L2178
	add	x2, x20, :lo12:.LANCHOR2
	orr	w1, w21, w1, lsl 10
	umull	x0, w19, w27
	ldr	x4, [x2,2520]
	add	x4, x4, x0
	str	w1, [x4,4]
	ldr	x1, [x2,2520]
	ldr	x4, [x2,2600]
	add	x0, x1, x0
	ldr	x2, [x2,2656]
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
.L2178:
	add	x3, x3, 1
	b	.L2177
.L2191:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w22
	mov	x28, 0
	ldr	x0, [x0,2520]
	bl	FlashReadPages
	mov	x8, 56
.L2180:
	cmp	w19, w28, uxth
	bls	.L2192
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x28, x8
	str	x8, [x29,104]
	add	x28, x28, 1
	ldr	x4, [x0,2520]
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
	b	.L2180
.L2192:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, w23
	bne	.L2176
.L2182:
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
	adrp	x0, .LC49
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC49
	add	x1, x1, 192
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	stp	x21, x22, [sp,48]
	adrp	x23, .LC67
	adrp	x22, .LC66
	adrp	x24, .LC68
	add	x22, x22, :lo12:.LC66
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC67
	mov	w20, 0
	add	x24, x24, :lo12:.LC68
	stp	x25, x26, [sp,80]
	bl	printk
.L2194:
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	ldrh	w0, [x19,2286]
	cmp	w0, w20
	bls	.L2203
	mov	w0, w20
	strh	w20, [x29,80]
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x4, [x19,2712]
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,2276]
	ldrh	w7, [x19,2354]
	mov	w9, 65535
	ldrh	w8, [x19,2356]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L2195:
	cmp	w6, w1, uxth
	bls	.L2204
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w0, [x0,16]
	cmp	w0, w9
	beq	.L2196
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w0, w0, 10
	umull	x2, w19, w10
	ldr	x4, [x3,2520]
	add	x4, x4, x2
	str	w0, [x4,4]
	ldr	x0, [x3,2520]
	ldr	x4, [x3,2600]
	add	x2, x0, x2
	ldr	x3, [x3,2656]
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
.L2196:
	add	x1, x1, 1
	b	.L2195
.L2204:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,2520]
	bl	FlashReadPages
.L2198:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L2205
	ldr	x4, [x0,2520]
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
	b	.L2198
.L2205:
	ldr	x0, [x0,2520]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L2200:
	cmp	w19, w25, uxth
	bls	.L2206
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,2520]
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
	b	.L2200
.L2206:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2194
.L2203:
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
	stp	x29, x30, [sp, -128]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR2
	stp	x19, x20, [sp,16]
	add	x19, x23, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	w25, 6
	adrp	x22, .LANCHOR4
	ldrh	w2, [x19,2286]
	add	x21, x22, :lo12:.LANCHOR4
	ldr	x0, [x19,2808]
	mov	w28, 0
	mov	w24, w28
	mov	w26, -1
	mul	w2, w2, w25
	mov	w27, 32768
	bl	ftl_memset
	strh	wzr, [x19,2386]
	str	xzr, [x21,768]
	mov	w19, w28
	str	xzr, [x21,744]
	str	xzr, [x21,752]
	strh	wzr, [x21,760]
	strh	wzr, [x21,776]
.L2208:
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w1, [x0,2284]
	cmp	w19, w1
	bge	.L2215
	mov	w20, 0
	ldrh	w5, [x0,2276]
	ldrh	w4, [x0,2344]
	mov	w3, w20
	mov	x2, x0
.L2216:
	cmp	w3, w5
	bge	.L2217
	add	x0, x2, 2304
	mov	w1, w19
	str	x4, [x29,96]
	str	x5, [x29,104]
	ldrb	w0, [x0,w3,sxtw]
	str	x2, [x29,112]
	str	x3, [x29,120]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,120]
	ldr	x2, [x29,112]
	ldr	x5, [x29,104]
	ldr	x4, [x29,96]
	cbnz	w0, .L2209
	add	w20, w4, w20
	sxth	w20, w20
.L2209:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L2216
.L2217:
	cbz	w20, .L2211
	sdiv	w20, w27, w20
	sxth	w20, w20
	b	.L2212
.L2211:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x0, [x0,2712]
	strh	w26, [x0,w19,sxtw 1]
.L2212:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	x0, [x1,2808]
	smaddl	x0, w19, w25, x0
	strh	w20, [x0,4]
	ldrh	w0, [x21,784]
	cmp	w19, w0
	beq	.L2213
	ldrh	w0, [x21,832]
	cmp	w19, w0
	beq	.L2213
	ldrh	w0, [x21,880]
	cmp	w19, w0
	beq	.L2213
	ldr	x0, [x1,2712]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L2214
	add	w28, w28, 1
	mov	w0, w19
	uxth	w28, w28
	bl	INSERT_FREE_LIST
	b	.L2213
.L2214:
	add	w24, w24, 1
	mov	w0, w19
	uxth	w24, w24
	bl	INSERT_DATA_LIST
.L2213:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2208
.L2215:
	add	x22, x22, :lo12:.LANCHOR4
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x25, x26, [sp,64]
	strh	w24, [x22,760]
	strh	w28, [x22,776]
	ldp	x23, x24, [sp,48]
	ldp	x21, x22, [sp,32]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	adrp	x0, .LANCHOR5
	mov	w1, 19539
	add	x0, x0, :lo12:.LANCHOR5
	movk	w1, 0x4654, lsl 16
	stp	x29, x30, [sp, -16]!
	add	x2, x0, 640
	add	x29, sp, 0
	ldr	w3, [x0,640]
	cmp	w3, w1
	bne	.L2218
	mov	w1, 99
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,1152]
	ldr	w0, [x0,1156]
	str	w0, [x2,92]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x2,88]
	ldr	w1, [x0,2404]
	str	w1, [x2,8]
	ldr	w1, [x0,2408]
	str	w1, [x2,12]
	ldr	w1, [x0,2424]
	str	w1, [x2,16]
	ldr	w1, [x0,2420]
	str	w1, [x2,20]
	ldr	w1, [x0,2432]
	str	w1, [x2,28]
	ldr	w1, [x0,2436]
	str	w1, [x2,32]
	ldr	w1, [x0,2412]
	str	w1, [x2,36]
	ldr	w1, [x0,2416]
	str	w1, [x2,40]
	ldr	w1, [x0,2440]
	str	w1, [x2,44]
	ldr	w1, [x0,2444]
	str	w1, [x2,48]
	ldr	w1, [x0,2392]
	ldr	w0, [x0,2388]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2218:
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
	ldr	w4, [x3,2096]
	cbz	w4, .L2221
	ldr	w3, [x3,2444]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2221:
	adrp	x3, .LANCHOR5
	add	x3, x3, :lo12:.LANCHOR5
	ldrh	w4, [x3,1160]
	cmp	w4, 31
	bhi	.L2222
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,1160]
.L2222:
	cbnz	w0, .L2223
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2688]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2224
.L2223:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,2688]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,2688]
	ldrh	w1, [x2,2672]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,2688]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2224:
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
	adrp	x19, .LANCHOR5
	str	x21, [sp,32]
	add	x20, x19, :lo12:.LANCHOR5
	add	x21, x20, 640
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,640]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2229
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,640]
.L2229:
	add	x1, x19, :lo12:.LANCHOR5
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	add	x0, x1, 640
	adrp	x20, .LANCHOR2
	ldr	w3, [x1,640]
	cmp	w3, w2
	bne	.L2230
	ldr	w2, [x1,728]
	str	w2, [x1,1152]
	ldr	w2, [x1,732]
	str	w2, [x1,1156]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x0,8]
	str	w2, [x1,2404]
	ldr	w2, [x0,12]
	str	w2, [x1,2408]
	ldr	w2, [x0,16]
	str	w2, [x1,2424]
	ldr	w2, [x0,20]
	str	w2, [x1,2420]
	ldr	w2, [x0,28]
	str	w2, [x1,2432]
	ldr	w2, [x0,32]
	str	w2, [x1,2436]
	ldr	w2, [x0,36]
	str	w2, [x1,2412]
	ldr	w2, [x0,40]
	str	w2, [x1,2416]
	ldr	w2, [x0,44]
	str	w2, [x1,2440]
	ldr	w2, [x0,48]
	ldr	w0, [x0,60]
	str	w2, [x1,2444]
	str	w0, [x1,2392]
.L2230:
	add	x0, x19, :lo12:.LANCHOR5
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 34661
	add	x0, x0, 640
	movk	w2, 0x1234, lsl 16
	ldr	w3, [x0,68]
	str	wzr, [x1,2388]
	cmp	w3, w2
	bne	.L2231
	ldrb	w2, [x1,1220]
	cbz	w2, .L2232
	str	wzr, [x0,68]
	bl	Ftl_save_ext_data
	b	.L2231
.L2232:
	mov	w0, 1
	str	w0, [x1,2096]
	adrp	x0, .LC47
	adrp	x1, .LC69
	add	x0, x0, :lo12:.LC47
	add	x1, x1, :lo12:.LC69
	bl	printk
.L2231:
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR5
	ldr	x21, [sp,32]
	ldrh	w2, [x0,2336]
	ldr	w3, [x0,2428]
	ldr	w1, [x0,2432]
	ldrh	w0, [x0,2284]
	madd	w1, w2, w3, w1
	udiv	w0, w1, w0
	str	w0, [x19,1164]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #112
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x21, x22, [sp,48]
	adrp	x21, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR2
	stp	x25, x26, [sp,80]
	stp	x23, x24, [sp,64]
	stp	x19, x20, [sp,32]
	stp	x27, x28, [sp,96]
	adrp	x24, .LC70
	adrp	x25, .LC71
	ldrh	w19, [x0,2342]
	adrp	x26, .LC72
	add	x24, x24, :lo12:.LC70
	add	x25, x25, :lo12:.LC71
	sub	w19, w19, #16
	add	x26, x26, :lo12:.LC72
.L2234:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,2342]
	cmp	w19, w0
	bge	.L2249
	uxth	w27, w19
	mov	w0, w27
	bl	ftl_get_blk_mode
	ldrb	w1, [x20,1220]
	cbz	w1, .L2235
	ldrh	w1, [x20,2284]
	cmp	w19, w1
	bge	.L2236
	ldrh	w1, [x20,2358]
	cmp	w19, w1
	blt	.L2236
.L2235:
	cmp	w0, 1
	bne	.L2237
.L2236:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w22, -2147483648
	ldrh	w23, [x0,2346]
	b	.L2238
.L2237:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w22, 0
	ldrh	w23, [x0,2344]
.L2238:
	mov	x0, x24
	mov	w1, w19
	mov	w2, w23
	mov	w3, w22
	bl	printk
	mov	w0, w27
	bl	FtlBbmIsBadBlock
	cbz	w0, .L2239
	mov	x0, x25
	mov	w1, w19
	mov	w2, w23
	mov	w3, w22
	bl	printk
.L2239:
	add	x28, x21, :lo12:.LANCHOR2
	mov	w27, 0
	add	x20, x28, 2936
	add	w22, w22, w19, lsl 10
.L2240:
	cmp	w27, w23
	beq	.L2250
	add	w0, w22, w27
	str	w0, [x20,4]
	ldr	x0, [x28,2576]
	mov	w1, 1
	str	x0, [x20,8]
	mov	w2, 0
	ldr	x0, [x28,2640]
	add	w27, w27, 1
	str	x0, [x20,16]
	mov	x0, x20
	str	wzr, [x20]
	bl	FlashReadPages
	ldr	x7, [x20,8]
	ldr	x6, [x20,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	mov	x0, x26
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x20,4]
	ldr	w2, [x20]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2240
.L2250:
	add	w19, w19, 1
	b	.L2234
.L2249:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC73
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC73
	add	x1, x1, 208
	bl	printk
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x25, x26, [sp,80]
	ldp	x27, x28, [sp,96]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 112
	ret
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
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
	ldrh	w24, [x1,2276]
	mov	w19, w22
	ldrh	w25, [x1,2344]
	mov	x23, x1
.L2252:
	cmp	w24, w22, uxth
	bls	.L2259
	add	x0, x23, 2304
	mov	w1, w20
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2253
	add	w19, w19, w25
	uxth	w19, w19
.L2253:
	add	x22, x22, 1
	b	.L2252
.L2259:
	cbz	w19, .L2255
	mov	w0, 32768
	sdiv	w19, w0, w19
.L2255:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 6
	umull	x20, w20, w1
	ldr	x0, [x0,2808]
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
	adrp	x0, .LANCHOR4+768
	ldr	x1, [x0,#:lo12:.LANCHOR4+768]
	mov	w0, w1
	cbz	x1, .L2261
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,2808]
	ldr	x0, [x0,2696]
	sub	x1, x1, x2
	mov	x2, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x2, x1, x1
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L2261:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR4
	uxth	w4, w0
	add	x2, x2, :lo12:.LANCHOR4
	ldr	x1, [x2,768]
	mov	w0, w1
	cbz	x1, .L2264
	ldrh	w2, [x2,776]
	mov	w0, 7
	mov	x3, -6148914691236517206
	mov	w6, 6
	mov	w7, 65535
	mul	w2, w2, w0
	asr	w2, w2, 3
	cmp	w4, w2
	csel	w4, w2, w4, gt
	adrp	x2, .LANCHOR2
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x0, [x0,2808]
	sub	x1, x1, x0
	asr	x1, x1, 1
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L2266:
	cmp	w3, w4
	beq	.L2269
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L2267
.L2269:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,2696]
	ldrh	w0, [x0,x1]
	b	.L2264
.L2267:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L2266
.L2264:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC74
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	add	x1, x1, :lo12:.LC74
	stp	x21, x22, [sp,32]
	add	x22, x24, :lo12:.LANCHOR2
	mov	x23, x0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x22,1192]
	adrp	x1, .LC75
	mov	x0, x20
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC76
	ldr	w2, [x22,2360]
	mov	x0, x20
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+3448
	ldr	w0, [x0,#:lo12:.LANCHOR1+3448]
	cmp	w0, 1
	beq	.L2272
	sub	w0, w20, w23
	b	.L2273
.L2272:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x26, .LANCHOR4
	bl	NandcGetTimeCfg
	add	x25, x26, :lo12:.LANCHOR4
	ldr	w4, [x29,104]
	adrp	x1, .LC77
	ldr	w5, [x29,108]
	mov	x0, x20
	ldr	w3, [x29,100]
	add	x1, x1, :lo12:.LC77
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC78
	mov	x0, x21
	add	x1, x1, :lo12:.LC78
	add	x21, x21, 10
	adrp	x20, .LANCHOR5
	bl	strcpy
	add	x20, x20, :lo12:.LANCHOR5
	ldr	w2, [x22,2924]
	adrp	x1, .LC79
	mov	x0, x21
	add	x1, x1, :lo12:.LC79
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1068]
	adrp	x1, .LC80
	mov	x0, x21
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2424]
	adrp	x1, .LC81
	mov	x0, x21
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2412]
	adrp	x1, .LC82
	mov	x0, x21
	add	x1, x1, :lo12:.LC82
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2408]
	adrp	x1, .LC83
	mov	x0, x21
	add	x1, x1, :lo12:.LC83
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2416]
	adrp	x1, .LC84
	mov	x0, x21
	add	x1, x1, :lo12:.LC84
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2420]
	adrp	x1, .LC85
	mov	x0, x21
	add	x1, x1, :lo12:.LC85
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2404]
	adrp	x1, .LC86
	mov	x0, x21
	add	x1, x1, :lo12:.LC86
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1152]
	adrp	x1, .LC87
	mov	x0, x21
	add	x1, x1, :lo12:.LC87
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1156]
	adrp	x1, .LC88
	mov	x0, x21
	add	x1, x1, :lo12:.LC88
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2396]
	adrp	x1, .LC89
	mov	x0, x21
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2400]
	adrp	x1, .LC90
	add	x1, x1, :lo12:.LC90
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x22,2830]
	adrp	x1, .LC91
	mov	x0, x21
	add	x1, x1, :lo12:.LC91
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,776]
	adrp	x1, .LC92
	mov	x0, x21
	add	x1, x1, :lo12:.LC92
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2428]
	adrp	x1, .LC93
	mov	x0, x21
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2432]
	adrp	x1, .LC94
	mov	x0, x21
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1164]
	adrp	x1, .LC95
	mov	x0, x21
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2436]
	adrp	x1, .LC96
	mov	x0, x21
	add	x1, x1, :lo12:.LC96
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2440]
	adrp	x1, .LC97
	mov	x0, x21
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2444]
	adrp	x1, .LC98
	mov	x0, x21
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,726]
	adrp	x1, .LC99
	mov	x0, x21
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,724]
	adrp	x1, .LC100
	mov	x0, x21
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,1224]
	adrp	x1, .LC101
	mov	x0, x21
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2376]
	adrp	x1, .LC102
	mov	x0, x21
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2272]
	adrp	x1, .LC103
	mov	x0, x21
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2998]
	adrp	x1, .LC104
	mov	x0, x21
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2284]
	adrp	x1, .LC105
	mov	x0, x21
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,588]
	adrp	x1, .LC106
	mov	x0, x21
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2288]
	adrp	x1, .LC107
	mov	x0, x21
	add	x1, x1, :lo12:.LC107
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,1072]
	adrp	x1, .LC108
	mov	x0, x21
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2824]
	adrp	x1, .LC109
	mov	x0, x21
	add	x1, x1, :lo12:.LC109
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,786]
	adrp	x1, .LC110
	mov	x0, x21
	add	x1, x1, :lo12:.LC110
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,790]
	adrp	x1, .LC111
	mov	x0, x21
	add	x1, x1, :lo12:.LC111
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,784]
	adrp	x1, .LC112
	mov	x0, x21
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,792]
	adrp	x1, .LC113
	mov	x0, x21
	add	x1, x1, :lo12:.LC113
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,788]
	adrp	x1, .LC114
	mov	x0, x21
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x25,784]
	adrp	x1, .LC115
	ldr	x2, [x22,2712]
	mov	x0, x21
	add	x1, x1, :lo12:.LC115
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,834]
	adrp	x1, .LC116
	mov	x0, x21
	add	x1, x1, :lo12:.LC116
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,838]
	adrp	x1, .LC117
	mov	x0, x21
	add	x1, x1, :lo12:.LC117
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,832]
	adrp	x1, .LC118
	mov	x0, x21
	add	x1, x1, :lo12:.LC118
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,840]
	adrp	x1, .LC119
	mov	x0, x21
	add	x1, x1, :lo12:.LC119
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,836]
	adrp	x1, .LC120
	mov	x0, x21
	add	x1, x1, :lo12:.LC120
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x25,832]
	adrp	x1, .LC121
	ldr	x2, [x22,2712]
	mov	x0, x21
	add	x1, x1, :lo12:.LC121
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,882]
	adrp	x1, .LC122
	mov	x0, x21
	add	x1, x1, :lo12:.LC122
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,886]
	adrp	x1, .LC123
	mov	x0, x21
	add	x1, x1, :lo12:.LC123
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,880]
	adrp	x1, .LC124
	mov	x0, x21
	add	x1, x1, :lo12:.LC124
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,888]
	adrp	x1, .LC125
	mov	x0, x21
	add	x1, x1, :lo12:.LC125
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,884]
	adrp	x1, .LC126
	mov	x0, x21
	add	x1, x1, :lo12:.LC126
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,594]
	adrp	x1, .LC127
	mov	x0, x21
	add	x1, x1, :lo12:.LC127
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,598]
	adrp	x1, .LC128
	mov	x0, x21
	add	x1, x1, :lo12:.LC128
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,592]
	adrp	x1, .LC129
	mov	x0, x21
	add	x1, x1, :lo12:.LC129
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,600]
	adrp	x1, .LC130
	mov	x0, x21
	add	x1, x1, :lo12:.LC130
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,596]
	adrp	x1, .LC131
	mov	x0, x21
	add	x1, x1, :lo12:.LC131
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,2392]
	adrp	x1, .LC132
	ldr	w2, [x22,2096]
	add	x1, x1, :lo12:.LC132
	ldr	w4, [x20,724]
	mov	x0, x21
	ldr	w5, [x20,716]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x20,720]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x20,712]
	adrp	x1, .LC133
	mov	x0, x19
	add	x1, x1, :lo12:.LC133
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x20,736]
	adrp	x1, .LC134
	mov	x0, x19
	add	x1, x1, :lo12:.LC134
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2480]
	adrp	x1, .LC135
	mov	x0, x19
	add	x1, x1, :lo12:.LC135
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2482]
	adrp	x1, .LC136
	mov	x0, x19
	add	x1, x1, :lo12:.LC136
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,2464]
	adrp	x1, .LC137
	mov	x0, x19
	add	x1, x1, :lo12:.LC137
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2484]
	adrp	x1, .LC138
	add	x1, x1, :lo12:.LC138
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC139
	mov	x0, x19
	add	x1, x1, :lo12:.LC139
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x25,776]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC140
	mov	x0, x19
	add	x1, x1, :lo12:.LC140
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x20,592]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2274
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,2712]
	adrp	x1, .LC141
	mov	x0, x19
	add	x1, x1, :lo12:.LC141
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L2274:
	mov	w0, 0
	adrp	x22, .LC142
	mov	w20, 0
	mov	w28, 65535
	bl	List_get_gc_head_node
	mov	w27, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC142
.L2276:
	cmp	w3, w28
	beq	.L2275
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w27
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,2808]
	mov	x1, x22
	ldr	x4, [x21,2712]
	add	w20, w20, 1
	ldr	x7, [x21,2696]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2808]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L2276
.L2275:
	add	x26, x26, :lo12:.LANCHOR4
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC143
	mov	w20, 0
	mov	w27, 65535
	add	x22, x22, :lo12:.LC143
	ldr	x0, [x26,768]
	mov	w26, 6
	ldr	x3, [x1,2808]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L2278:
	cmp	w3, w27
	beq	.L2277
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,2808]
	mov	x1, x22
	ldr	x6, [x21,2696]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2808]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L2278
.L2277:
	sub	w0, w19, w23
.L2273:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	stp	x29, x30, [sp, -32]!
	adrp	x1, .LC47
	adrp	x2, .LC144
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC47
	add	x2, x2, :lo12:.LC144
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
	adrp	x0, .LANCHOR5
	add	x1, x0, :lo12:.LANCHOR5
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	adrp	x19, .LANCHOR2
	ldr	w3, [x1,1164]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,2444]
	cmp	w3, w2
	bcs	.L2283
	str	wzr, [x1,2428]
	ldrh	w3, [x1,2284]
	ldr	x5, [x1,2696]
	mov	x1, 0
.L2284:
	cmp	w3, w1
	bls	.L2316
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x1,lsl 1]
	add	x1, x1, 1
	ldr	w2, [x4,2428]
	add	w2, w6, w2
	str	w2, [x4,2428]
	b	.L2284
.L2316:
	add	x2, x19, :lo12:.LANCHOR2
	add	x4, x0, :lo12:.LANCHOR5
	ldr	w1, [x2,2428]
	udiv	w3, w1, w3
	str	w3, [x4,1164]
	ldr	w3, [x2,2432]
	sub	w1, w1, w3
	ldrh	w3, [x2,2336]
	udiv	w1, w1, w3
	str	w1, [x2,2428]
	b	.L2286
.L2283:
	ldr	w2, [x1,2440]
	cmp	w3, w2
	bls	.L2286
	add	w2, w2, 1
	str	w2, [x1,2440]
	mov	w1, 0
.L2288:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w3, [x2,2284]
	cmp	w1, w3
	bcs	.L2286
	ubfiz	x3, x1, 1, 32
	ldr	x4, [x2,2696]
	add	w1, w1, 1
	ldrh	w2, [x4,x3]
	add	w2, w2, 1
	strh	w2, [x4,x3]
	b	.L2288
.L2286:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w21, [x1,2444]
	ldr	w23, [x0,1164]
	add	w0, w21, 256
	cmp	w0, w23
	bls	.L2291
	ldr	w2, [x1,2440]
	add	w0, w21, 768
	cmp	w0, w2
	bls	.L2291
	ldr	w0, [x1,2096]
	cbz	w0, .L2294
	cmp	w21, 40
	bls	.L2291
.L2294:
	mov	w0, 65535
	b	.L2293
.L2291:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L2303
	cmp	w21, 40
	bhi	.L2294
.L2303:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x3, [x20,744]
	cbz	x3, .L2294
	add	x0, x19, :lo12:.LANCHOR2
	mov	w26, 65535
	mov	w1, 0
	mov	w20, w26
	mov	w8, w26
	mov	w10, 6
	ldrh	w9, [x0,2284]
	ldr	x7, [x0,2808]
	ldr	x25, [x0,2696]
	mov	x0, -6148914691236517206
	add	x0, x0, 1
.L2296:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L2298
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L2294
	ldrh	w2, [x3,4]
	cbz	w2, .L2297
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w21
	bls	.L2302
	cmp	w2, w26
	bcs	.L2297
	mov	w26, w2
	mov	w20, w3
.L2297:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L2296
.L2302:
	mov	w20, w3
.L2298:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2294
	ubfiz	x24, x20, 1, 16
	ldrh	w22, [x25,x24]
	cmp	w22, w21
	bls	.L2300
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L2300
	add	x0, x19, :lo12:.LANCHOR2
	str	w26, [x0,2444]
.L2300:
	cmp	w22, w23
	bcs	.L2294
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L2294
	add	w0, w22, 256
	cmp	w0, w23
	bcc	.L2301
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,2440]
	cmp	w22, w0
	bcs	.L2294
.L2301:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC145
	ldrh	w5, [x25,x24]
	add	x0, x0, :lo12:.LC145
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,2712]
	ldr	w3, [x19,2440]
	ldrh	w4, [x4,x24]
	bl	printk
	mov	w0, 1
	str	w0, [x19,2452]
	mov	w0, w20
.L2293:
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
	beq	.L2318
	adrp	x2, .LANCHOR2+2712
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+2712]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L2318:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x1, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w1, [x1,2928]
	cbnz	w1, .L2320
	adrp	x21, .LANCHOR4
	mov	x20, x0
	add	x21, x21, :lo12:.LANCHOR4
	add	x23, x21, 880
	mov	x22, x21
	add	x24, x21, 768
.L2321:
	cmp	x20, x23
	bne	.L2322
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w0, [x21,776]
	lsr	w3, w0, 1
	ldr	w4, [x2,2452]
	add	w5, w3, 1
	mul	w1, w0, w4
	add	w1, w5, w1, lsr 2
	ldr	w5, [x2,2096]
	uxth	w1, w1
	cbz	w5, .L2323
	ldr	w2, [x2,2444]
	cmp	w2, 39
	bhi	.L2323
	cmp	w2, 2
	mov	w1, 0
	bls	.L2324
	tbz	x0, 0, .L2351
	cbz	w4, .L2324
.L2351:
	mov	w1, w3
	b	.L2323
.L2322:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2324
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w0, [x2,2296]
	cmp	w0, 1
	beq	.L2324
	ldrb	w0, [x2,1220]
	cbnz	w0, .L2324
	ldr	w3, [x2,2096]
	ldrh	w0, [x21,776]
	lsr	w1, w0, 3
	cbz	w3, .L2323
	ldr	w2, [x2,2444]
	cmp	w2, 1
	bhi	.L2323
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L2323:
	cbz	w1, .L2324
	sub	w1, w1, #1
	uxth	w1, w1
.L2324:
	ldrb	w2, [x20,8]
	mov	x0, x24
	add	x26, x19, :lo12:.LANCHOR2
	bl	List_pop_index_node
	uxth	w27, w0
	ldrh	w0, [x22,776]
	sub	w0, w0, #1
	strh	w0, [x22,776]
	ldrh	w0, [x26,2284]
	cmp	w0, w27
	bls	.L2321
	ubfiz	x25, x27, 1, 16
	ldr	x0, [x26,2712]
	ldrh	w0, [x0,x25]
	cbnz	w0, .L2321
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2327
	mov	x0, 0
	ldrh	w5, [x26,2276]
	mov	x3, 56
	mov	w26, w0
	mov	w6, 65535
	b	.L2328
.L2327:
	ldr	x0, [x26,2712]
	b	.L2376
.L2330:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x4, [x1,2544]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2329
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L2329:
	add	x0, x0, 1
.L2328:
	cmp	w5, w0, uxth
	bhi	.L2330
	add	x0, x22, 784
	cmp	x20, x0
	bne	.L2331
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,2096]
	cbz	w2, .L2331
	ldr	x1, [x1,2696]
	ldrh	w1, [x1,x25]
	cmp	w1, 40
	bls	.L2331
	strb	wzr, [x22,792]
.L2331:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2332
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x0, [x1,2696]
	ldrh	w2, [x0,x25]
	cbz	w2, .L2333
	ldrh	w1, [x1,2336]
	add	w1, w2, w1
	b	.L2377
.L2333:
	mov	w1, 2
.L2377:
	strh	w1, [x0,x25]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,2428]
	add	w0, w0, 1
	str	w0, [x1,2428]
	mov	w0, w27
	mov	w1, 0
	bl	ftl_set_blk_mode
	b	.L2335
.L2332:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2696]
	ldrh	w1, [x2,x25]
	add	w1, w1, 1
	strh	w1, [x2,x25]
	ldr	w1, [x0,2432]
	add	w1, w1, 1
	str	w1, [x0,2432]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.17
.L2335:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2696]
	ldr	w2, [x0,2440]
	ldrh	w1, [x1,x25]
	cmp	w1, w2
	bls	.L2336
	str	w1, [x0,2440]
.L2336:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,2336]
	ldr	w3, [x0,2428]
	ldr	w1, [x0,2432]
	madd	w1, w2, w3, w1
	ldrh	w2, [x0,2284]
	mov	x3, 56
	udiv	w1, w1, w2
	adrp	x2, .LANCHOR5+1164
	str	w1, [x2,#:lo12:.LANCHOR5+1164]
	ldr	x1, [x0,2688]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2337:
	cmp	w26, w0, uxth
	bls	.L2379
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,2544]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2337
.L2379:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,1220]
	cbz	w1, .L2339
	ldrb	w1, [x20,8]
	ldr	x0, [x0,2544]
	cmp	w1, 1
	bne	.L2340
	mov	w1, 0
	b	.L2378
.L2340:
	mov	w1, 1
.L2378:
	mov	w2, w26
	bl	FlashEraseBlocks
.L2339:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x20,8]
	mov	w2, w26
	mov	x28, 0
	ldr	x0, [x0,2544]
	bl	FlashEraseBlocks
	mov	w1, w28
	mov	x3, 56
.L2341:
	cmp	w26, w28, uxth
	bls	.L2380
	add	x2, x19, :lo12:.LANCHOR2
	mul	x0, x28, x3
	ldr	x2, [x2,2544]
	add	x4, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2342
	ldr	w0, [x4,4]
	add	w1, w1, 1
	str	x3, [x29,96]
	lsr	w0, w0, 10
	str	x2, [x29,104]
	str	x1, [x29,112]
	str	x1, [x29,120]
	bl	FtlBbmMapBadBlock
	add	x0, x20, x28, lsl 1
	ldr	x2, [x29,104]
	ldr	x3, [x29,96]
	ldr	x1, [x29,120]
	strh	w2, [x0,16]
	ldrb	w0, [x20,7]
	sub	w0, w0, #1
	strb	w0, [x20,7]
.L2342:
	add	x28, x28, 1
	b	.L2341
.L2380:
	cbz	w1, .L2344
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2344:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2345
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2712]
.L2376:
	mov	w1, -1
	strh	w1, [x0,x25]
	b	.L2321
.L2345:
	add	x19, x19, :lo12:.LANCHOR2
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x19,2344]
	strh	w27, [x20]
	mul	w0, w0, w1
	ldr	w1, [x19,2396]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x19,2396]
	ldr	x1, [x19,2712]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2320:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	adrp	x0, .LANCHOR5+1168
	mov	w1, 0
	adrp	x7, .LANCHOR2
	mov	w8, 24
	mov	w9, 1
	mov	w4, 4
	str	wzr, [x0,#:lo12:.LANCHOR5+1168]
	mov	w10, 56
.L2382:
	add	x2, x7, :lo12:.LANCHOR2
	adrp	x6, .LANCHOR2
	ldrh	w0, [x2,2276]
	cmp	w1, w0
	bcs	.L2386
	umull	x3, w1, w8
	ldr	x5, [x2,2632]
	ldr	x6, [x2,2600]
	add	x0, x5, x3
	str	w9, [x0,16]
	ldrh	w0, [x2,2354]
	mul	w0, w1, w0
	sdiv	w0, w0, w4
	add	x0, x6, x0, sxtw 2
	str	x0, [x5,x3]
	ldrh	w0, [x2,2356]
	ldr	x6, [x2,2632]
	ldr	x11, [x2,2656]
	mul	w0, w1, w0
	add	x5, x6, x3
	ldr	x2, [x2,2552]
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
	b	.L2382
.L2386:
	mov	w7, 24
	mov	w4, 4
.L2384:
	add	x1, x6, :lo12:.LANCHOR2
	ldr	w2, [x1,2568]
	cmp	w0, w2
	bcs	.L2387
	umull	x3, w0, w7
	ldr	x5, [x1,2632]
	ldr	x8, [x1,2600]
	add	x2, x5, x3
	str	wzr, [x2,16]
	ldrh	w2, [x1,2354]
	mul	w2, w0, w2
	sdiv	w2, w2, w4
	add	x2, x8, x2, sxtw 2
	str	x2, [x5,x3]
	ldrh	w2, [x1,2356]
	ldr	x5, [x1,2632]
	ldr	x1, [x1,2656]
	mul	w2, w0, w2
	add	x3, x5, x3
	add	w0, w0, 1
	sdiv	w2, w2, w4
	uxth	w0, w0
	add	x1, x1, x2, sxtw 2
	str	x1, [x3,8]
	b	.L2384
.L2387:
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
	adrp	x20, .LANCHOR5
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x19,2364]
	strh	w0, [x20,1184]
	mov	w0, -1
	str	w0, [x20,1196]
	lsl	w2, w2, 1
	ldr	x0, [x19,2736]
	str	xzr, [x20,1176]
	str	wzr, [x20,1188]
	str	wzr, [x20,1192]
	str	wzr, [x19,2096]
	strh	wzr, [x19,2920]
	bl	ftl_memset
	ldrh	w2, [x19,2286]
	mov	w1, 0
	ldr	x0, [x19,2696]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19,2286]
	mov	w1, 0
	ldr	x0, [x19,2680]
	lsl	w2, w2, 1
	bl	ftl_memset
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 48
	add	x0, x0, 696
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x20, 640
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
	ldr	w8, [x2,2568]
	ldr	x5, [x2,2632]
.L2390:
	cmp	w3, w1
	bcs	.L2389
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L2395:
	cmp	w2, w8
	bcs	.L2392
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L2391
	str	wzr, [x10,16]
	b	.L2392
.L2391:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2395
.L2392:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2390
.L2389:
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
	ldr	w5, [x2,2568]
	ldr	x6, [x2,2632]
	mov	w2, 0
.L2397:
	cmp	w2, w1
	bcs	.L2403
	mov	w3, 0
.L2401:
	cmp	w3, w5
	bcs	.L2399
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L2398
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L2399
.L2398:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2401
.L2399:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2397
.L2403:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR5+1200
	uxth	w0, w0
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1200]
	adrp	x1, .LANCHOR2+2504
	ldr	x3, [x1,#:lo12:.LANCHOR2+2504]
	mov	x1, 0
.L2405:
	cmp	w2, w1, uxth
	bls	.L2409
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L2405
	mov	w0, 1
	b	.L2406
.L2409:
	mov	w0, 0
.L2406:
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
	adrp	x3, .LANCHOR5
	ldr	x2, [x29,40]
	add	x4, x3, :lo12:.LANCHOR5
	ldr	x1, [x29,32]
	uxth	w9, w0
	mov	x6, 0
	ldrh	w8, [x4,1200]
	adrp	x4, .LANCHOR2
	add	x5, x4, :lo12:.LANCHOR2
	ldr	x7, [x5,2504]
.L2411:
	uxth	w5, w6
	cmp	w5, w8
	bcs	.L2415
	add	x6, x6, 1
	add	x10, x7, x6, lsl 1
	ldrh	w10, [x10,-2]
	cmp	w10, w9
	bne	.L2411
.L2415:
	cmp	w5, w8
	bne	.L2413
	ubfiz	x5, x5, 1, 16
	strh	w0, [x7,x5]
	add	x0, x3, :lo12:.LANCHOR5
	ldrh	w5, [x0,1200]
	add	w5, w5, 1
	strh	w5, [x0,1200]
.L2413:
	add	x3, x3, :lo12:.LANCHOR5
	add	x4, x4, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w5, [x3,1202]
	ldr	x6, [x4,2512]
	umull	x5, w5, w0
	add	x6, x6, x5
	str	w1, [x6,4]
	ldr	x1, [x4,2512]
	add	x0, x1, x5
	str	w2, [x0,8]
	str	w19, [x1,x5]
	ldrh	w0, [x3,1202]
	add	w0, w0, 1
	strh	w0, [x3,1202]
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR5
	mov	w1, 255
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x19,2348]
	strh	wzr, [x0,1200]
	strh	wzr, [x0,1202]
	lsl	w2, w2, 1
	ldr	x0, [x19,2504]
	bl	ftl_memset
	ldrh	w3, [x19,2348]
	mov	w2, 12
	ldr	x0, [x19,2512]
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
	stp	x29, x30, [sp, -224]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	adrp	x0, .LANCHOR1
	str	w1, [x29,136]
	add	x1, x0, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x0, [x29,128]
	ldrh	w27, [x1,3452]
	mov	w1, 65535
	cmp	w27, w1
	beq	.L2451
	cbnz	w27, .L2419
	b	.L2420
.L2451:
	mov	w27, 0
.L2419:
	adrp	x0, .LANCHOR2+2344
	ldr	w1, [x29,136]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+2344]
	cmp	w1, w0
	bne	.L2421
.L2420:
	bl	FtlGcPageVarInit
.L2421:
	mov	w0, -1
	adrp	x23, .LANCHOR2
	str	w0, [x29,140]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	mov	w24, 0
	mov	x22, x23
	str	x0, [x29,120]
.L2422:
	ldrh	w1, [x19]
	mov	w0, 65535
	strb	wzr, [x19,8]
	cmp	w1, w0
	beq	.L2452
.L2424:
	mov	w28, w0
.L2448:
	adrp	x0, .LANCHOR2
	mov	x2, x19
	add	x0, x0, :lo12:.LANCHOR2
	mov	w20, 0
	mov	w10, 56
	mov	w7, 4
	ldrh	w5, [x0,2276]
	ldrh	w8, [x0,2354]
	ldrh	w9, [x0,2356]
	add	x5, x19, x5, lsl 1
.L2425:
	cmp	x2, x5
	beq	.L2470
	ldrh	w1, [x2,16]
	cmp	w1, w28
	beq	.L2426
	add	x4, x23, :lo12:.LANCHOR2
	orr	w1, w27, w1, lsl 10
	umull	x0, w20, w10
	ldr	x6, [x4,2520]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x4,2520]
	ldr	x6, [x4,2600]
	add	x0, x1, x0
	ldr	x4, [x4,2656]
	mul	w1, w20, w8
	sdiv	w1, w1, w7
	add	x1, x6, x1, sxtw 2
	str	x1, [x0,8]
	mul	w1, w20, w9
	add	w20, w20, 1
	uxth	w20, w20
	sdiv	w1, w1, w7
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L2426:
	add	x2, x2, 2
	b	.L2425
.L2470:
	add	x0, x23, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 0
	mov	x25, 0
	ldr	x0, [x0,2520]
	bl	FlashReadPages
.L2428:
	cmp	w20, w25, uxth
	bls	.L2471
	add	x26, x23, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x21, x25, x0
	ldr	x1, [x26,2520]
	str	x1, [x29,104]
	add	x2, x1, x21
	str	x2, [x29,112]
	ldr	w0, [x2,4]
	str	w0, [x29,144]
	ldr	x0, [x29,144]
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w6, w0
	ldr	x1, [x29,104]
	ldr	x2, [x29,112]
	ldr	w7, [x1,x21]
	ldr	x5, [x2,16]
	cbnz	w7, .L2429
	ldrh	w0, [x5]
	cmp	w0, w28
	bne	.L2430
.L2433:
	add	x1, x22, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR5
	ldrb	w0, [x1,1220]
	cbz	w0, .L2467
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	str	w0, [x20,1192]
	b	.L2423
.L2430:
	ldr	w0, [x5,8]
	ldr	w1, [x26,2924]
	cmp	w0, w1
	bls	.L2468
	b	.L2433
.L2467:
	ldrh	w2, [x19]
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x1, [x1,2712]
	strh	w0, [x1,x2,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x20,592]
	b	.L2469
.L2468:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,88]
	cbz	w1, .L2437
	add	x1, x29, 164
	mov	w2, w7
	str	x5, [x29,112]
	str	x7, [x29,104]
	bl	log2phys
	ldr	x5, [x29,112]
	ldr	w1, [x29,164]
	ldr	w0, [x5,12]
	cmp	w1, w0
	bne	.L2437
	cmn	w1, #1
	beq	.L2437
	str	w0, [x29,172]
	mov	w1, 1
	ldr	x0, [x26,2624]
	ldr	x7, [x29,104]
	str	x0, [x29,176]
	ldr	x0, [x26,2648]
	mov	w2, w7
	str	x0, [x29,184]
	add	x0, x29, 168
	bl	FlashReadPages
	ldr	x0, [x26,2520]
	ldrh	w2, [x26,2350]
	ldr	x4, [x29,176]
	add	x21, x0, x21
	ldr	x5, [x29,112]
	lsl	w2, w2, 7
	mov	x0, 0
.L2439:
	cmp	w2, w0
	bls	.L2437
	lsl	x1, x0, 2
	ldr	x6, [x21,8]
	add	x0, x0, 1
	ldr	w6, [x6,x1]
	ldr	w1, [x4,x1]
	cmp	w6, w1
	beq	.L2439
	ldrh	w1, [x19]
	adrp	x0, .LC146
	ldr	w2, [x29,172]
	add	x0, x0, :lo12:.LC146
	bl	printk
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2712]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	adrp	x1, .LANCHOR5+592
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x1,#:lo12:.LANCHOR5+592]
	b	.L2469
.L2437:
	ldr	w0, [x5,12]
	add	x25, x25, 1
	ldr	w1, [x29,144]
	ldr	w2, [x5,8]
	bl	FtlGcUpdatePage
	b	.L2428
.L2429:
	ldr	x0, [x29,120]
	ldrh	w1, [x19]
	ldr	w2, [x29,144]
	bl	printk
	ldr	w0, [x26,2096]
	cbnz	w0, .L2442
	ldrb	w0, [x26,1220]
	cbz	w0, .L2443
.L2442:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2696]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2444
.L2443:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2445
.L2444:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	add	x21, x0, x21
	ldr	w0, [x21,4]
	str	w0, [x29,140]
.L2445:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2712]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
.L2469:
	bl	FtlGcPageVarInit
	mov	w27, 0
	b	.L2422
.L2471:
	ldr	w0, [x29,136]
	add	w24, w24, 1
	add	w3, w27, 1
	cmp	w24, w0
	uxth	w27, w3
	bcs	.L2447
.L2449:
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w0, [x0,2344]
	cmp	w0, w27
	bhi	.L2448
	b	.L2452
.L2447:
	ldr	x0, [x29,128]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3452]
	cmp	w0, w28
	beq	.L2449
	add	w0, w0, w24
	strh	w0, [x1,3452]
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w0, [x0,2344]
	cmp	w0, w27
	bls	.L2449
	b	.L2450
.L2452:
	mov	w6, 0
.L2423:
	ldr	x0, [x29,128]
	mov	w1, -1
	strh	w27, [x19,2]
	mov	w2, w6
	add	x0, x0, :lo12:.LANCHOR1
	strb	w6, [x19,6]
	strh	w1, [x0,3452]
	mov	x0, x19
	mov	w1, w27
	bl	ftl_sb_update_avl_pages
.L2450:
	ldr	w0, [x29,140]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,2472]
	cmp	w0, w20
	beq	.L2474
	ldrh	w0, [x19,2474]
	cmp	w0, w20
	beq	.L2474
	ldrh	w0, [x19,2476]
	cmp	w0, w20
	beq	.L2474
	ldrh	w0, [x19,2478]
	cmp	w0, w20
	beq	.L2474
	adrp	x0, .LC148
	mov	w1, w20
	add	x0, x0, :lo12:.LC148
	bl	printk
	ldrh	w1, [x19,2472]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2476
	strh	w20, [x19,2472]
	b	.L2474
.L2476:
	ldrh	w1, [x19,2474]
	cmp	w1, w0
	bne	.L2477
	strh	w20, [x19,2474]
	b	.L2474
.L2477:
	ldrh	w1, [x19,2476]
	cmp	w1, w0
	bne	.L2478
	strh	w20, [x19,2476]
	b	.L2474
.L2478:
	ldrh	w1, [x19,2478]
	cmp	w1, w0
	bne	.L2474
	strh	w20, [x19,2478]
.L2474:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	uxth	w20, w0
	add	x19, x19, :lo12:.LANCHOR2
	str	x21, [sp,32]
	mov	w21, 0
	ldrh	w0, [x19,2472]
	cmp	w0, w20
	beq	.L2480
	ldrh	w0, [x19,2474]
	cmp	w0, w20
	beq	.L2480
	ldrh	w0, [x19,2476]
	cmp	w0, w20
	beq	.L2480
	ldrh	w0, [x19,2478]
	cmp	w0, w20
	beq	.L2480
	adrp	x0, .LC148
	mov	w1, w20
	add	x0, x0, :lo12:.LC148
	bl	printk
	ldrh	w1, [x19,2472]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2481
	strh	w20, [x19,2472]
	b	.L2480
.L2481:
	ldrh	w1, [x19,2474]
	cmp	w1, w0
	bne	.L2482
	strh	w20, [x19,2474]
	b	.L2480
.L2482:
	ldrh	w1, [x19,2476]
	cmp	w1, w0
	bne	.L2483
	strh	w20, [x19,2476]
	b	.L2480
.L2483:
	ldrh	w1, [x19,2478]
	cmp	w1, w0
	bne	.L2488
	strh	w20, [x19,2478]
	b	.L2480
.L2488:
	mov	w21, -1
.L2480:
	mov	w0, w21
	ldr	x21, [sp,32]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
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
	ldrh	w1, [x22,2486]
	adrp	x0, .LC149
	mov	w2, w20
	add	x0, x0, :lo12:.LC149
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,2096]
	cbz	w0, .L2490
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,2696]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L2490
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L2490:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR5
	add	x1, x1, :lo12:.LANCHOR5
	add	x1, x1, 1208
	ldrh	w3, [x0,2486]
	mov	x0, 0
.L2491:
	cmp	w3, w0, uxth
	bls	.L2497
	add	x0, x0, 1
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L2491
	b	.L2492
.L2497:
	cmp	w3, 15
	bhi	.L2492
	adrp	x1, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR5
	add	w0, w3, 1
	add	x1, x1, 1208
	strh	w0, [x19,2486]
	strh	w20, [x1,w3,sxtw 1]
.L2492:
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
	ldrh	w1, [x0,2486]
	cbz	w1, .L2499
	ldrh	w3, [x0,2472]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2499
	ldrh	w2, [x0,2490]
	cmp	w2, w1
	bcc	.L2500
	strh	wzr, [x0,2490]
.L2500:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	add	x0, x0, 1208
	ldrh	w1, [x19,2490]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,2490]
	add	w0, w0, 1
	strh	w0, [x19,2490]
.L2499:
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
	ldrh	w0, [x19,2486]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	cbz	w0, .L2506
	adrp	x24, .LANCHOR5
	adrp	x22, .LC150
	add	x22, x22, :lo12:.LC150
	add	x25, x24, :lo12:.LANCHOR5
.L2514:
	ldrh	w0, [x19,2276]
	cmp	w0, w21
	bls	.L2515
	add	x0, x19, 2304
	mov	w1, w23
	mov	w20, 0
	add	x27, x24, :lo12:.LANCHOR5
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w26, w0
.L2507:
	ldrh	w0, [x19,2486]
	cmp	w0, w20
	bls	.L2516
	add	x0, x27, 1208
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w26
	bne	.L2508
	mov	w1, w26
	mov	x0, x22
	bl	printk
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,2486]
	mov	w3, w20
.L2509:
	cmp	w3, w2
	bcs	.L2517
	add	x4, x25, 1208
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2509
.L2517:
	sub	w2, w2, #1
	strh	w2, [x19,2486]
.L2508:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2507
.L2516:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2514
.L2515:
	bl	FtlGcReFreshBadBlk
.L2506:
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
	adrp	x1, .LANCHOR2+2712
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w0
	ldr	x1, [x1,#:lo12:.LANCHOR2+2712]
	ubfiz	x0, x19, 1, 16
	ldrh	w1, [x1,x0]
	cbnz	w1, .L2519
	adrp	x0, .LANCHOR5
	adrp	x20, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x0,592]
	cmp	w2, w19
	bne	.L2520
	mov	w1, -1
	strh	w1, [x0,592]
	b	.L2521
.L2520:
	add	x2, x20, :lo12:.LANCHOR4
	mov	w0, w1
	ldrh	w3, [x2,784]
	cmp	w3, w19
	beq	.L2522
	ldrh	w3, [x2,832]
	cmp	w3, w19
	beq	.L2522
	ldrh	w2, [x2,880]
	cmp	w2, w19
	beq	.L2522
.L2521:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, w19
	add	x0, x20, 744
	bl	List_remove_node
	ldrh	w0, [x20,760]
	sub	w0, w0, #1
	strh	w0, [x20,760]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2522
.L2519:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2522:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w0
	mov	w0, 65535
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	cmp	w19, w0
	beq	.L2527
	adrp	x20, .LANCHOR2
	ubfiz	x22, x19, 1, 16
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,2712]
	ldrh	w2, [x0,x22]
	cbnz	w2, .L2528
	adrp	x0, .LC151
	mov	w1, w19
	add	x0, x0, :lo12:.LC151
	adrp	x21, .LANCHOR4
	add	x21, x21, :lo12:.LANCHOR4
	bl	printk
	add	x23, x21, 768
	ldr	x0, [x20,2712]
	mov	w1, 32
	strh	w1, [x0,x22]
	mov	x0, x23
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2529
	mov	w1, w19
	mov	x0, x23
	bl	List_remove_node
	ldrh	w0, [x21,776]
	sub	w0, w0, #1
	strh	w0, [x21,776]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,2712]
	adrp	x0, .LC152
	add	x0, x0, :lo12:.LC152
	mov	w1, w19
	ldrh	w2, [x2,x22]
	bl	printk
.L2529:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2536
.L2528:
	sub	w2, w2, #1
	strh	w2, [x0,x22]
.L2527:
	adrp	x20, .LANCHOR5
	mov	w0, 65535
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w1, [x20,1184]
	cmp	w1, w0
	bne	.L2531
	strh	w19, [x20,1184]
.L2536:
	mov	w0, 0
	b	.L2530
.L2531:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2530
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,1184]
.L2530:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldr	x23, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	stp	x29, x30, [sp, -224]!
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
	beq	.L2698
	ldrh	w0, [x19,2]
	adrp	x20, .LANCHOR2
	str	w0, [x29,204]
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w28, [x19,6]
	ldr	w1, [x29,204]
	ldrh	w0, [x0,2344]
	cmp	w0, w1
	bne	.L2540
	strh	wzr, [x19,4]
	b	.L2704
.L2540:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2541:
	cmp	w0, w2
	bne	.L2707
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2541
.L2707:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2543
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2544
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, w21
	ldrb	w0, [x0,1220]
	cbnz	w0, .L2618
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 144
	ldrh	w23, [x0,w21,sxtw 1]
	b	.L2618
.L2543:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2544
	mov	w23, w0
.L2618:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w25, 0
	mov	w8, 65535
	mov	w9, 56
	mov	w10, 4
	ldrh	w4, [x0,2276]
	ldrh	w7, [x0,2356]
	add	x4, x19, x4, lsl 1
	b	.L2546
.L2544:
	strh	wzr, [x19,2]
.L2704:
	strb	wzr, [x19,6]
	b	.L2698
.L2548:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2547
	add	x5, x20, :lo12:.LANCHOR2
	orr	w1, w23, w1, lsl 10
	umull	x0, w25, w9
	add	w3, w25, 1
	ldr	x6, [x5,2520]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,2520]
	ldr	x5, [x5,2656]
	add	x0, x1, x0
	mul	w1, w25, w7
	uxth	w25, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2547:
	add	x2, x2, 2
.L2546:
	cmp	x2, x4
	bne	.L2548
	ldrb	w0, [x19,8]
	str	wzr, [x29,200]
	cmp	w0, 1
	bne	.L2549
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,200]
.L2549:
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,200]
	mov	w1, w25
	mov	x26, 0
	mov	w24, 65535
	adrp	x27, .LANCHOR5
	ldr	x0, [x22,2520]
	bl	FlashReadPages
	ldr	w0, [x22,2400]
	adrp	x22, .LC153
	str	w0, [x29,192]
	add	x22, x22, :lo12:.LC153
	uxth	w0, w23
	str	w0, [x29,184]
.L2550:
	uxth	w2, w26
	cmp	w2, w25
	bcs	.L2557
	add	x6, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x26, x0
	ldr	x4, [x6,2520]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2551
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2552
	ldr	w1, [x6,2400]
	mov	w0, w4
	str	x7, [x29,152]
	str	x2, [x29,160]
	str	x4, [x29,168]
	str	x6, [x29,176]
	bl	ftl_cmp_data_ver
	ldr	x6, [x29,176]
	ldr	x4, [x29,168]
	ldr	x2, [x29,160]
	ldr	x7, [x29,152]
	cbz	w0, .L2552
	add	w4, w4, 1
	str	w4, [x6,2400]
.L2552:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2553
.L2557:
	cmp	w2, w25
	bne	.L2699
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,2520]
	ldr	w0, [x0,4]
	b	.L2700
.L2551:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w24, [x29,184]
	add	x0, x27, :lo12:.LANCHOR5
	ldrh	w1, [x19]
	strh	w1, [x0,1242]
.L2553:
	add	x26, x26, 1
	b	.L2550
.L2699:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,2520]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2700:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2559
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbnz	w0, .L2559
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 144
	ldrh	w21, [x0,w21,sxtw 1]
.L2559:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,2344]
	cmp	w0, w21
	bne	.L2560
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2560:
	uxth	w0, w28
	str	w0, [x29,160]
	ldr	w0, [x29,204]
	cmp	w21, w0
	bne	.L2561
	ldr	w0, [x29,160]
	cmp	w26, w0
	beq	.L2705
.L2561:
	ldr	w0, [x29,192]
	adrp	x25, .LANCHOR5
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w24, w0
	bne	.L2562
	cbnz	w1, .L2563
.L2562:
	add	x0, x25, :lo12:.LANCHOR5
	uxth	w6, w23
	ldr	w1, [x0,1196]
	cmn	w1, #1
	bne	.L2564
	str	w22, [x0,1196]
.L2564:
	add	x0, x25, :lo12:.LANCHOR5
	ldr	w24, [x29,204]
	ldr	w3, [x0,1196]
	ldr	w0, [x29,204]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2565
	sub	w24, w6, #7
	uxth	w24, w24
.L2565:
	mov	w28, -1
	adrp	x5, .LANCHOR0
	mov	w27, w28
	mov	w4, 65535
	mov	w7, 56
	add	x5, x5, :lo12:.LANCHOR0
.L2566:
	cmp	w24, w6
	bhi	.L2579
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, 0
	ldrh	w2, [x0,2276]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2580:
	cmp	x0, x2
	beq	.L2708
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2567
	add	x9, x20, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x8, w23, w7
	add	w23, w23, 1
	ldr	x9, [x9,2520]
	uxth	w23, w23
	add	x8, x9, x8
	str	w1, [x8,4]
.L2567:
	add	x0, x0, 2
	b	.L2580
.L2708:
	add	x8, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,200]
	mov	w1, w23
	str	x5, [x29,144]
	str	x7, [x29,152]
	ldr	x0, [x8,2520]
	str	x4, [x29,168]
	str	x3, [x29,176]
	str	x6, [x29,184]
	str	x8, [x29,192]
	bl	FlashReadPages
	ldr	x8, [x29,192]
	mov	w1, 0
	ldr	x6, [x29,184]
	sxtw	x10, w24
	ldr	x3, [x29,176]
	ldr	x0, [x8,2520]
	ldrb	w9, [x8,1220]
	ldr	x4, [x29,168]
	add	x0, x0, 16
	ldr	x7, [x29,152]
	ldr	x5, [x29,144]
.L2569:
	cmp	w1, w23
	beq	.L2709
	ldr	w2, [x0,-16]
	cbnz	w2, .L2570
	ldr	x2, [x0]
	ldrh	w8, [x2]
	cmp	w8, w4
	beq	.L2571
	ldr	w8, [x2,4]
	cmn	w8, #1
	beq	.L2571
	add	x2, x25, :lo12:.LANCHOR5
	cmn	w28, #1
	ldr	w27, [x2,1196]
	str	w8, [x2,1196]
	bne	.L2571
	add	x2, x5, 1168
	ldrh	w2, [x2,x10,lsl 1]
	cmp	w2, w4
	bne	.L2572
	cbz	w9, .L2571
.L2572:
	cmp	w27, w22
	csinv	w28, w27, wzr, ne
	b	.L2571
.L2570:
	add	x0, x25, :lo12:.LANCHOR5
	ldrh	w1, [x19]
	strh	w1, [x0,1242]
	ldrb	w1, [x19,8]
	cbnz	w1, .L2563
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 1168
	ldrh	w2, [x1,w24,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2574
	cmn	w28, #1
	beq	.L2575
	str	w28, [x0,1196]
	b	.L2563
.L2575:
	cmp	w3, w22
	beq	.L2576
	str	w3, [x0,1196]
	b	.L2563
.L2576:
	ldr	w1, [x0,1196]
	b	.L2706
.L2574:
	cmp	w27, w22
	beq	.L2577
	cmn	w27, #1
	beq	.L2563
	str	w27, [x0,1196]
	b	.L2563
.L2577:
	ldr	w1, [x0,1196]
	cmp	w1, w22
	beq	.L2563
.L2706:
	sub	w1, w1, #1
	b	.L2701
.L2571:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2569
.L2709:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2566
.L2579:
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, -1
.L2701:
	str	w1, [x0,1196]
.L2563:
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, 1
	ldr	w23, [x29,204]
	strh	w1, [x0,1244]
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	str	x0, [x29,136]
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	str	x0, [x29,128]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29,168]
.L2581:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x1, x19
	str	wzr, [x29,192]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,2276]
	ldrb	w6, [x0,1220]
	add	x3, x19, x3, lsl 1
.L2582:
	cmp	x1, x3
	beq	.L2710
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2583
	ldr	w0, [x29,192]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w23, w2, lsl 10
	ldr	x4, [x5,2520]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2584
	cbz	w6, .L2584
	ldr	x2, [x5,2520]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2584:
	ldr	w0, [x29,192]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,192]
.L2583:
	add	x1, x1, 2
	b	.L2582
.L2710:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,192]
	ldr	w2, [x29,200]
	ldr	x0, [x0,2520]
	bl	FlashReadPages
	str	xzr, [x29,176]
.L2586:
	ldrh	w0, [x29,176]
	ldr	w1, [x29,192]
	cmp	w1, w0, uxth
	bls	.L2711
	ldr	x0, [x29,176]
	mov	x24, 56
	mul	x24, x0, x24
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x28, [x0,2520]
	add	x27, x28, x24
	ldr	w1, [x27,4]
	str	w1, [x29,220]
	lsr	x0, x1, 10
	str	x1, [x29,184]
	bl	P2V_plane
	uxth	w0, w0
	ldr	w1, [x29,204]
	cmp	w23, w1
	ldr	x1, [x29,184]
	bcc	.L2587
	ldr	w2, [x29,160]
	cmp	w0, w2
	bcs	.L2622
	ldr	w2, [x29,204]
	cmp	w23, w2
	beq	.L2587
.L2622:
	cmp	w0, w26
	bne	.L2623
	cmp	w23, w21
	beq	.L2589
.L2623:
	ldr	w0, [x28,x24]
	cmn	w0, #1
	beq	.L2591
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2592
	ldrh	w0, [x19]
	b	.L2702
.L2592:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2593
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,2400]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2593
	add	w0, w22, 1
	str	w0, [x27,2400]
.L2593:
	ldr	w27, [x28,8]
	add	x1, x29, 216
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,212]
	mov	w0, w27
	bl	log2phys
	ldr	w28, [x29,212]
	add	x0, x25, :lo12:.LANCHOR5
	str	x0, [x29,144]
	ldr	w1, [x0,1196]
	cmn	w1, #1
	beq	.L2594
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2594
	cmn	w28, #1
	beq	.L2595
	add	x3, x20, :lo12:.LANCHOR2
	mov	w2, 0
	str	x3, [x29,120]
	ldr	x0, [x3,2520]
	add	x0, x0, x24
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,2520]
	str	x1, [x29,184]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x3, [x29,120]
	ldr	x0, [x29,184]
	ldr	x2, [x3,2520]
	ldr	w0, [x0,4]
	str	w0, [x29,152]
	add	x4, x2, x24
	ldr	w0, [x2,x24]
	cmn	w0, #1
	bne	.L2596
	b	.L2597
.L2595:
	ldr	w0, [x29,220]
	ldr	w1, [x29,216]
	cmp	w1, w0
	bne	.L2587
	mov	w0, w27
	add	x1, x29, 212
	mov	w2, 1
	bl	log2phys
	b	.L2587
.L2596:
	ldr	x0, [x29,184]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2597
	ldr	x0, [x29,144]
	ldr	w1, [x29,152]
	str	x2, [x29,104]
	ldr	w0, [x0,1196]
	str	x3, [x29,112]
	str	x4, [x29,120]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,120]
	ldr	x3, [x29,112]
	ldr	x2, [x29,104]
	cbz	w0, .L2597
	ldr	w0, [x29,216]
	ldr	w1, [x29,220]
	cmp	w0, w1
	beq	.L2602
	ldr	w1, [x29,212]
	cmp	w0, w1
	beq	.L2597
	cmn	w0, #1
	beq	.L2600
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2520]
	str	x1, [x29,184]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2601
.L2600:
	str	w0, [x2,x24]
.L2601:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2602
	ldr	x0, [x29,184]
	ldr	w24, [x0,4]
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, w24
	ldr	w0, [x0,1196]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2602
	ldr	w0, [x29,152]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2597
.L2602:
	ldr	w1, [x29,212]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2597:
	mov	w0, -1
	str	w0, [x29,212]
	b	.L2604
.L2594:
	ldr	w0, [x29,220]
	ldr	w1, [x29,216]
	cmp	w1, w0
	beq	.L2604
	cmn	w28, #1
	beq	.L2606
	add	x0, x20, :lo12:.LANCHOR2
	ubfx	x28, x28, 10, 21
	ldr	w0, [x0,2292]
	cmp	w28, w0
	bcs	.L2587
.L2606:
	mov	w0, w27
	add	x1, x29, 220
	mov	w2, 1
	bl	log2phys
	ldr	w28, [x29,216]
	cmn	w28, #1
	beq	.L2604
	ldr	w0, [x29,212]
	cmp	w28, w0
	beq	.L2604
	lsr	x0, x28, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	ldr	x1, [x29,168]
	ldrh	w1, [x1,784]
	cmp	w1, w0
	beq	.L2608
	ldr	x1, [x29,168]
	ldrh	w1, [x1,832]
	cmp	w1, w0
	beq	.L2608
	ldr	x1, [x29,168]
	ldrh	w1, [x1,880]
	cmp	w1, w0
	bne	.L2604
.L2608:
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x24, x0, :lo12:.LANCHOR2
	mov	w2, 0
	ldr	x0, [x24,2520]
	str	w28, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x24,2520]
	str	x3, [x29,184]
	bl	FlashReadPages
	ldr	x0, [x24,2520]
	ldr	x3, [x29,184]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2604
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2604
	mov	w0, w27
	add	x1, x29, 216
	mov	w2, 1
	bl	log2phys
.L2604:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L2587
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,2712]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2609
.L2702:
	bl	decrement_vpc_count
	b	.L2587
.L2609:
	ldr	x0, [x29,128]
	bl	printk
	b	.L2587
.L2591:
	add	x24, x25, :lo12:.LANCHOR5
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x24,1242]
	ldr	x0, [x29,136]
	bl	printk
	ldr	w0, [x24,1248]
	cmp	w0, 31
	bhi	.L2610
	add	x1, x24, 1256
	ldr	w2, [x29,220]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x24,1248]
.L2610:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x25, :lo12:.LANCHOR5
	ldr	w1, [x0,1196]
	cmn	w1, #1
	beq	.L2703
	cmp	w1, w22
	bls	.L2587
.L2703:
	str	w22, [x0,1196]
.L2587:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L2586
.L2711:
	ldrb	w0, [x19,8]
	add	w23, w23, 1
	cmp	w0, 1
	uxth	w23, w23
	bne	.L2613
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x0,1220]
	cbz	w1, .L2613
	cmp	w21, w23
	bne	.L2613
	ldrh	w0, [x0,2346]
	cmp	w0, w23
	beq	.L2589
.L2613:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,2344]
	cmp	w23, w1
	bne	.L2581
	ldrh	w2, [x0,2276]
	mov	w3, 65535
	strh	w23, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2615:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2698
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2615
	strb	w1, [x19,6]
	b	.L2698
.L2589:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2705:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2698:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 224
	ret
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldrh	w1, [x0,4]
	cbz	w1, .L2712
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2712
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	adrp	x22, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,x0,lsl 1]
.L2716:
	cmp	w0, w20
	bne	.L2723
.L2718:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,2276]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2717
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2717:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2716
.L2723:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2719
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbnz	w0, .L2719
	ldrh	w2, [x19,2]
	add	x0, x21, 1168
	ldrh	w0, [x0,w2,sxtw 1]
	cmp	w0, w20
	bne	.L2719
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2718
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2712
.L2719:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w2, [x0,1220]
	cbz	w2, .L2712
	cmp	w1, 1
	bne	.L2712
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2346]
	cmp	w2, w1
	bcc	.L2712
	ldrh	w1, [x19]
	ldr	x3, [x0,2712]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2344]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2712:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	mov	x19, x0
	strb	wzr, [x0,10]
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	mov	w22, 65535
	adrp	x23, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,x0,lsl 1]
.L2725:
	cmp	w0, w22
	adrp	x20, .LANCHOR2
	bne	.L2740
.L2726:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,2276]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2727
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2727:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2725
.L2740:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2728
	add	x1, x23, :lo12:.LANCHOR2
	ldrb	w1, [x1,1220]
	cbnz	w1, .L2728
	ldrh	w2, [x19,2]
	add	x1, x21, 1168
	ldrh	w1, [x1,w2,sxtw 1]
	cmp	w1, w22
	bne	.L2728
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2726
.L2728:
	ldrh	w1, [x19,4]
	mov	w23, 65535
	ldrh	w22, [x19,2]
	adrp	x21, .LANCHOR0
	sub	w1, w1, #1
	mov	w24, w23
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	add	x21, x21, :lo12:.LANCHOR0
.L2729:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,2276]
.L2731:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2730
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2730:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2731
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2736
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbnz	w0, .L2733
	ldrh	w1, [x19,2]
	add	x0, x21, 1168
	ldrh	w0, [x0,w1,sxtw 1]
	cmp	w0, w24
	bne	.L2733
	ldrh	w1, [x19,4]
	cbz	w1, .L2733
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2729
.L2733:
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w0, [x1,1220]
	cbz	w0, .L2736
	ldrh	w2, [x19,2]
	ldrh	w0, [x1,2346]
	cmp	w2, w0
	bcc	.L2736
	ldrh	w2, [x19]
	ldr	x0, [x1,2712]
	lsl	x2, x2, 1
	ldrh	w4, [x19,4]
	ldrh	w3, [x0,x2]
	sub	w3, w3, w4
	strh	w3, [x0,x2]
	strh	wzr, [x19,4]
	ldrh	w0, [x1,2344]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2736:
	mov	w0, w22
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	stp	x29, x30, [sp, -144]!
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x25, [sp,64]
	stp	x19, x20, [sp,16]
	add	x20, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	ldr	w0, [x20,2928]
	cbnz	w0, .L2741
	adrp	x19, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR4
	add	x1, x1, 784
	ldrh	w2, [x1,4]
	cbz	w2, .L2743
	ldrb	w0, [x1,8]
	cbnz	w0, .L2743
	ldrb	w0, [x1,7]
	ldrh	w3, [x20,2344]
	mul	w0, w0, w3
	cmp	w2, w0
	beq	.L2743
	ldrb	w24, [x1,10]
	ldr	w23, [x20,2924]
	ldrh	w25, [x20,2276]
	cbnz	w24, .L2741
	sub	w23, w23, #1
	add	x1, x29, 84
	mov	w0, w23
	mov	w2, w24
	bl	log2phys
	ldr	x21, [x20,2640]
	ldr	w0, [x29,84]
	ldr	x1, [x20,2576]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w23, [x29,112]
	str	x1, [x29,96]
	str	x21, [x29,104]
	str	w24, [x21,4]
	beq	.L2745
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2746
.L2745:
	ldr	x0, [x20,2576]
	mov	w1, 255
	ldrh	w2, [x20,2354]
	bl	ftl_memset
.L2746:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x21]
	mov	w24, 0
	add	x20, x20, 784
.L2747:
	cmp	w24, w25
	beq	.L2748
	ldrh	w0, [x20,4]
	cbz	w0, .L2748
	ldr	w0, [x29,92]
	add	w24, w24, 1
	str	w0, [x21,12]
	ldrh	w0, [x20]
	strh	w0, [x21,2]
	mov	x0, x20
	str	w23, [x21,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	add	x0, x22, :lo12:.LANCHOR2
	mov	w2, 0
	mov	w3, w2
	ldr	w1, [x0,2400]
	str	w1, [x21,4]
	add	w1, w1, 1
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x0,2400]
	mov	w1, 1
	add	x0, x29, 88
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2747
.L2748:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x19,794]
	b	.L2741
.L2743:
	add	x19, x19, :lo12:.LANCHOR4
	strb	wzr, [x19,794]
.L2741:
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
.L2759:
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w0, [x1,2382]
	cmp	w0, w19
	bls	.L2762
	ldr	x1, [x1,2792]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2760
	mov	w0, w19
	bl	flush_l2p_region
.L2760:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2759
.L2762:
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
	add	x2, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w1, [x2,2928]
	cbnz	w1, .L2763
	ldrb	w21, [x2,1220]
	cbz	w21, .L2778
	ldrb	w21, [x0,8]
	cmp	w21, 1
	bne	.L2774
	ldrh	w23, [x0,4]
	b	.L2765
.L2774:
	mov	w21, w1
.L2778:
	mov	w23, 12
.L2765:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x22, [x0,2640]
	ldr	x1, [x0,2576]
	str	x1, [x29,80]
	mov	w1, -3
	str	x22, [x29,88]
	str	w1, [x22,8]
	mov	w1, -2
	str	w1, [x22,12]
	ldrh	w1, [x19]
	strh	w1, [x22,2]
	strh	wzr, [x22]
	ldr	x1, [x0,2576]
	str	w2, [x1]
	ldr	x1, [x0,2576]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2766:
	sub	w23, w23, #1
	cmn	w23, #1
	beq	.L2769
	ldrh	w0, [x19,4]
	cbnz	w0, .L2767
.L2769:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	ldr	x2, [x20,2712]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x20,2344]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2763
.L2767:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2769
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w3, 0
	ldr	w0, [x1,2400]
	str	w0, [x22,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,2400]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2766
.L2763:
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
	stp	x29, x30, [sp, -96]!
	adrp	x1, .LANCHOR3
	adrp	x0, .LC49
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 232
	add	x0, x0, :lo12:.LC49
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	adrp	x22, .LANCHOR2
	bl	printk
	mov	w19, 0
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,2286]
	ldr	x0, [x0,2704]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2780:
	add	x20, x22, :lo12:.LANCHOR2
	ldr	w0, [x20,2924]
	cmp	w19, w0
	bcs	.L2796
	mov	w0, w19
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L2781
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x20,2704]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2781:
	add	w19, w19, 1
	b	.L2780
.L2796:
	adrp	x23, .LANCHOR4
	adrp	x24, .LC156
	mov	w19, 0
	mov	w25, 65535
	add	x23, x23, :lo12:.LANCHOR4
	add	x24, x24, :lo12:.LC156
.L2783:
	add	x21, x22, :lo12:.LANCHOR2
	ldrh	w0, [x21,2284]
	cmp	w0, w19
	bls	.L2797
	ubfiz	x20, x19, 1, 16
	ldr	x0, [x21,2712]
	ldrh	w2, [x0,x20]
	ldr	x0, [x21,2704]
	cmp	w2, w25
	ldrh	w3, [x0,x20]
	beq	.L2784
	cmp	w2, w3
	beq	.L2784
	ldrh	w0, [x23,784]
	cmp	w0, w19
	beq	.L2784
	ldrh	w0, [x23,880]
	cmp	w0, w19
	beq	.L2784
	ldrh	w0, [x23,832]
	cmp	w0, w19
	beq	.L2784
	mov	w1, w19
	mov	x0, x24
	bl	printk
	ldr	x0, [x21,2712]
	ldrh	w1, [x0,x20]
	cbnz	w1, .L2785
	ldr	x1, [x21,2704]
	ldrh	w1, [x1,x20]
	strh	w1, [x0,x20]
	b	.L2784
.L2785:
	ldr	x1, [x21,2704]
	ldrh	w1, [x1,x20]
	strh	w1, [x0,x20]
	mov	w0, w19
	bl	update_vpc_list
.L2784:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2783
.L2797:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	x25, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 96
	ret
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x1, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w2, [x1,2928]
	ldrh	w22, [x0]
	cbnz	w2, .L2799
	mov	x19, x0
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2800
	ubfiz	x0, x22, 1, 16
	ldr	x1, [x1,2712]
	ldrh	w0, [x1,x0]
	cbz	w0, .L2801
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L2800
.L2801:
	mov	w0, w22
	bl	INSERT_FREE_LIST
.L2800:
	adrp	x0, .LANCHOR4
	strb	wzr, [x19,8]
	add	x1, x0, :lo12:.LANCHOR4
	add	x2, x1, 832
	cmp	x19, x2
	beq	.L2802
	add	x2, x20, :lo12:.LANCHOR2
	ldrh	w3, [x2,2296]
	cmp	w3, 1
	beq	.L2802
	ldrb	w4, [x2,1220]
	cbz	w4, .L2803
.L2802:
	mov	w0, 1
	strb	w0, [x19,8]
	b	.L2804
.L2803:
	add	x1, x1, 784
	cmp	x19, x1
	bne	.L2804
	cmp	w3, 3
	beq	.L2806
	ldr	w1, [x2,2392]
	cmp	w1, 1
	bne	.L2807
.L2806:
	add	x1, x0, :lo12:.LANCHOR4
	mov	w2, 1
	strb	w2, [x1,792]
.L2807:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x1,2096]
	cbz	w2, .L2804
	ldr	w1, [x1,2444]
	cmp	w1, 39
	bhi	.L2804
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,792]
.L2804:
	adrp	x1, .LANCHOR5
	mov	w2, 65535
	add	x0, x1, :lo12:.LANCHOR5
	mov	x21, x1
	ldrh	w0, [x0,1184]
	cmp	w0, w2
	beq	.L2809
	cmp	w22, w0
	bne	.L2810
	add	x20, x20, :lo12:.LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x20,2712]
	ldrh	w1, [x2,x1]
	cbz	w1, .L2811
.L2810:
	bl	update_vpc_list
.L2811:
	add	x1, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x1,1184]
.L2809:
	mov	x0, x19
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2799:
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
	adrp	x1, .LANCHOR5
	add	x3, x1, :lo12:.LANCHOR5
	add	x29, sp, 0
	add	x2, x3, 640
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR2
	ldr	w0, [x2,80]
	cbz	w0, .L2825
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w4, [x2,84]
	ldr	w1, [x0,2924]
	cmp	w4, w1
	bcs	.L2826
	mov	w21, 2048
	mov	x20, x3
.L2831:
	add	x22, x20, 640
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x22,84]
	ldr	w1, [x1,2924]
	cmp	w0, w1
	bcs	.L2830
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2829
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
	bne	.L2830
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2830:
	mov	w0, -1
	b	.L2833
.L2829:
	subs	w21, w21, #1
	bne	.L2831
	b	.L2830
.L2826:
	ldr	w0, [x0,2424]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2840
.L2825:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w5, 10000
	mov	w8, 63
	ldr	w6, [x2,76]
	ldr	w3, [x19,2440]
	ldr	w7, [x19,2424]
	cmp	w3, w5
	mov	w5, 31
	csel	w8, w5, w8, hi
	add	w5, w7, 1048576
	cmp	w6, w5
	ldr	w4, [x19,2924]
	adrp	x5, .LANCHOR4
	bhi	.L2835
	mov	w9, 1000
	lsr	w3, w3, 10
	add	w3, w3, 1
	mul	w4, w4, w9
	udiv	w3, w4, w3
	add	w3, w3, w6
	cmp	w3, w7
	bcc	.L2835
	add	x3, x5, :lo12:.LANCHOR4
	ldrh	w3, [x3,724]
	tst	w8, w3
	bne	.L2833
	ldr	w2, [x2,100]
	cmp	w2, w3
	beq	.L2833
.L2835:
	add	x5, x5, :lo12:.LANCHOR4
	add	x0, x1, :lo12:.LANCHOR5
	ldrh	w1, [x5,724]
	str	w1, [x0,740]
	mov	w1, 1
	str	wzr, [x0,724]
	str	w1, [x0,720]
	str	w7, [x0,716]
.L2840:
	mov	w0, 0
.L2833:
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
	add	x4, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w3, [x4,2928]
	cbnz	w3, .L2994
	adrp	x3, .LANCHOR1
	add	x3, x3, :lo12:.LANCHOR1
	ldr	w20, [x3,3448]
	cmp	w20, 1
	bne	.L2994
	ldr	w4, [x4,2448]
	cbnz	w4, .L2994
	adrp	x23, .LANCHOR4
	add	x4, x23, :lo12:.LANCHOR4
	ldrh	w5, [x4,760]
	cmp	w5, 47
	bls	.L2994
	mov	w24, w1
	ldrh	w1, [x3,3452]
	str	w0, [x29,132]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2843
.L2846:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,2474]
	cmp	w4, w2
	bne	.L2844
	b	.L2845
.L2843:
	ldrh	w1, [x4,880]
	cmp	w1, w0
	beq	.L2846
	mov	w0, w20
	bl	FtlGcFreeTempBlock
	mov	w2, w20
	cbz	w0, .L2846
	b	.L2994
.L2844:
	ldrh	w1, [x0,2472]
	cmp	w1, w2
	bne	.L2845
	ldrh	w3, [x0,2476]
	cmp	w3, w1
	beq	.L2845
	ldrh	w2, [x0,2478]
	cmp	w2, w1
	beq	.L2845
	mov	w1, -1
	strh	w4, [x0,2472]
	strh	w3, [x0,2474]
	strh	w2, [x0,2476]
	strh	w1, [x0,2478]
.L2845:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,132]
	adrp	x20, .LANCHOR5
	cmp	w2, 1
	ldr	w0, [x1,2464]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,2464]
	bne	.L2857
	ldr	w2, [x1,2096]
	cbnz	w2, .L2848
	ldrb	w1, [x1,1220]
	cbz	w1, .L2857
.L2848:
	add	x21, x19, :lo12:.LANCHOR2
	ldr	w1, [x21,2444]
	cmp	w1, 39
	bhi	.L2857
	add	x22, x20, :lo12:.LANCHOR5
	ldrh	w1, [x22,1384]
	add	w0, w0, w1
	str	w0, [x21,2464]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x22,592]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2857
	ldrh	w1, [x21,2472]
	cmp	w1, w0
	bne	.L2857
	ldr	w0, [x21,2464]
	cmp	w0, 1024
	bhi	.L2849
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	cmp	w0, 63
	bhi	.L2857
.L2849:
	add	x1, x23, :lo12:.LANCHOR4
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w3, [x1,776]
	add	x1, x19, :lo12:.LANCHOR2
	strh	wzr, [x0,1384]
	ldrh	w2, [x1,2482]
	add	w2, w2, 64
	cmp	w3, w2
	bgt	.L2857
	str	wzr, [x1,2464]
	ldr	w1, [x1,2444]
	cbnz	w1, .L2850
	mov	w1, 6
	b	.L3016
.L2850:
	cmp	w1, 5
	bhi	.L2851
	mov	w1, 18
.L3016:
	strh	w1, [x0,1384]
.L2851:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2856
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,2484]
	cbz	w0, .L2853
	ldrh	w1, [x22,2346]
	ubfiz	x25, x25, 1, 16
	ldrh	w3, [x22,2276]
	ldr	x26, [x22,2712]
	mul	w1, w1, w3
	ldrh	w2, [x26,x25]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2856
	add	w1, w0, 1
	str	wzr, [x22,2452]
	uxth	w1, w1
	strh	w1, [x22,2484]
	str	x1, [x29,136]
	bl	List_get_gc_head_node
	uxth	w21, w0
	cmp	w21, w27
	ldr	x1, [x29,136]
	beq	.L2856
	ubfiz	x27, x21, 1, 16
	adrp	x0, .LC157
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC157
	mov	w2, w21
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x22,2484]
	cmp	w0, 40
	bls	.L2854
	ldr	x0, [x22,2712]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2854
	strh	wzr, [x22,2484]
.L2854:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, 6
	strh	w1, [x0,1384]
	b	.L2858
.L2853:
	mov	w0, 1
	strh	w0, [x22,2484]
.L2856:
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2858
	add	x0, x20, :lo12:.LANCHOR5
	strh	wzr, [x0,1384]
.L2857:
	add	x1, x20, :lo12:.LANCHOR5
	mov	w0, 65535
	ldrh	w21, [x1,592]
	cmp	w21, w0
	bne	.L3021
	add	x26, x23, :lo12:.LANCHOR4
	ldrh	w0, [x26,880]
	cmp	w0, w21
	beq	.L3022
.L2858:
	mov	w0, 65535
	cmp	w21, w0
	cset	w0, eq
	cbz	w0, .L2871
	ldr	w1, [x29,132]
	cbnz	w1, .L2871
	add	x0, x23, :lo12:.LANCHOR4
	mov	w22, 1
	ldrh	w0, [x0,776]
	cmp	w0, 24
	bhi	.L2872
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w0, 16
	ldrh	w22, [x1,2344]
	bls	.L2873
	lsr	w22, w22, 5
	b	.L2872
.L2873:
	cmp	w0, 12
	bls	.L2874
	lsr	w22, w22, 4
	b	.L2872
.L2874:
	cmp	w0, 8
	bls	.L2872
	lsr	w22, w22, 2
.L2872:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,2480]
	cmp	w2, w0
	bcs	.L2876
	add	x3, x23, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w0, [x3,880]
	cmp	w0, w2
	bne	.L2877
	ldrh	w2, [x1,2472]
	cmp	w2, w0
	bne	.L2877
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x0,1384]
	cbnz	w2, .L2878
	ldr	w0, [x1,2924]
	ldr	w3, [x3,1068]
	add	w0, w0, w0, lsl 1
	cmp	w3, w0, lsr 2
	bcs	.L2879
.L2878:
	add	x20, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x20,588]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	b	.L3019
.L2879:
	mov	w0, 18
.L3019:
	add	x19, x19, :lo12:.LANCHOR2
	strh	w0, [x1,2480]
	str	wzr, [x19,2452]
	b	.L2994
.L2877:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,588]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,2480]
.L2876:
	cmp	w24, 2
	bhi	.L2948
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2096]
	cbz	w0, .L2948
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2948
.L2871:
	add	x2, x23, :lo12:.LANCHOR4
	mov	w3, 65535
	ldrh	w1, [x2,880]
	cmp	w1, w3
	bne	.L2883
	cbz	w0, .L2883
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w3, [x0,2472]
	cmp	w3, w1
	bne	.L2883
	add	x3, x20, :lo12:.LANCHOR5
	ldrh	w4, [x3,592]
	cmp	w4, w1
	beq	.L2884
.L2889:
	mov	w21, 65535
	b	.L2883
.L2884:
	ldrh	w4, [x2,776]
	ldrh	w1, [x0,2480]
	str	wzr, [x0,2452]
	cmp	w4, w1
	bls	.L2885
	ldrh	w1, [x3,1384]
	cbnz	w1, .L2886
	ldr	w1, [x0,2924]
	ldr	w2, [x2,1068]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2887
.L2886:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,588]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,2480]
	b	.L2888
.L2887:
	mov	w1, 18
	strh	w1, [x0,2480]
.L2888:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,2712]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2885
	add	x20, x20, :lo12:.LANCHOR5
	b	.L3020
.L2885:
	add	x24, x20, :lo12:.LANCHOR5
	ldrh	w0, [x24,1384]
	cbnz	w0, .L2889
	ldrh	w21, [x24,588]
	add	x22, x19, :lo12:.LANCHOR2
	add	w1, w21, w21, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,2480]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x22,2712]
	ldrh	w2, [x22,2276]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x22,2346]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L2890
	add	x0, x23, :lo12:.LANCHOR4
	sub	w21, w21, #1
	ldrh	w0, [x0,776]
	cmp	w0, w21
	blt	.L2890
	bl	FtlReadRefresh
	ldrh	w2, [x24,1384]
	b	.L2994
.L2890:
	cbnz	w1, .L2889
	mov	w0, -1
	add	x23, x23, :lo12:.LANCHOR4
	bl	decrement_vpc_count
	ldrh	w2, [x23,776]
	add	w2, w2, 1
	b	.L2994
.L2883:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 2
	ldr	w0, [x0,2096]
	cmp	w0, wzr
	csinc	w22, w22, wzr, ne
	b	.L2882
.L2948:
	mov	w21, 65535
.L2882:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w2, 65535
	ldrh	w1, [x0,592]
	cmp	w1, w2
	bne	.L2892
	cmp	w21, w1
	beq	.L2893
	strh	w21, [x0,592]
	b	.L2894
.L2893:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,2472]
	cmp	w0, w21
	beq	.L2894
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1,2712]
	ldrh	w0, [x2,x0]
	cbnz	w0, .L2895
	mov	w0, -1
	strh	w0, [x1,2472]
.L2895:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x1,2472]
	strh	w2, [x0,592]
	mov	w0, -1
	strh	w0, [x1,2472]
.L2894:
	add	x24, x20, :lo12:.LANCHOR5
	mov	w1, 65535
	ldrh	w0, [x24,592]
	strb	wzr, [x24,600]
	cmp	w0, w1
	beq	.L2892
	bl	IsBlkInGcList
	cbz	w0, .L2897
	mov	w0, -1
	strh	w0, [x24,592]
.L2897:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L2898
	add	x0, x20, :lo12:.LANCHOR5
	add	x24, x0, 592
	ldrh	w0, [x0,592]
	bl	ftl_get_blk_mode
	strb	w0, [x24,8]
.L2898:
	add	x24, x20, :lo12:.LANCHOR5
	mov	w0, 65535
	add	x25, x24, 592
	ldrh	w1, [x24,592]
	cmp	w1, w0
	beq	.L2892
	mov	x0, x25
	bl	make_superblock
	strh	wzr, [x24,594]
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x24,592]
	strb	wzr, [x24,598]
	strh	wzr, [x24,1386]
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x1,lsl 1]
	strh	w0, [x24,1388]
.L2892:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,592]
	ldrh	w2, [x1,784]
	cmp	w2, w0
	beq	.L2899
	ldrh	w2, [x1,832]
	cmp	w2, w0
	beq	.L2899
	ldrh	w1, [x1,880]
	cmp	w1, w0
	bne	.L2900
.L2899:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, -1
	strh	w1, [x0,592]
.L2900:
	add	x25, x20, :lo12:.LANCHOR5
.L2933:
	ldrh	w0, [x25,592]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2901
	add	x0, x19, :lo12:.LANCHOR2
	str	wzr, [x0,2452]
.L2902:
	add	x26, x19, :lo12:.LANCHOR2
	ldrh	w24, [x26,2484]
	mov	w0, w24
	bl	List_get_gc_head_node
	uxth	w27, w0
	mov	w0, 65535
	strh	w27, [x25,592]
	cmp	w27, w0
	bne	.L2903
	strh	wzr, [x26,2484]
	mov	w2, 8
	b	.L2994
.L2903:
	mov	w0, w27
	add	w24, w24, 1
	bl	IsBlkInGcList
	cbz	w0, .L2904
	strh	w24, [x26,2484]
	b	.L2902
.L2904:
	ldrh	w5, [x26,2276]
	ubfiz	x1, x27, 1, 16
	ldrh	w0, [x26,2344]
	uxth	w24, w24
	ldr	x2, [x26,2712]
	mov	w3, 2
	strh	w24, [x26,2484]
	mul	w0, w0, w5
	ldrh	w4, [x2,x1]
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2906
	cmp	w4, 8
	bls	.L2907
	cmp	w24, 48
	bls	.L2907
	ldrh	w4, [x25,1200]
	cmp	w4, 35
	bhi	.L2907
.L2906:
	add	x4, x19, :lo12:.LANCHOR2
	strh	wzr, [x4,2484]
.L2907:
	ldrh	w1, [x2,x1]
	mov	w2, 65535
	cmp	w21, w2
	bne	.L2908
	cmp	w1, w0
	blt	.L2908
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,2484]
	cmp	w2, 3
	bhi	.L2908
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, -1
	strh	wzr, [x0,2484]
	strh	w1, [x20,592]
.L3020:
	ldrh	w2, [x20,1384]
	b	.L2994
.L2908:
	cbnz	w1, .L2909
	mov	w0, -1
	bl	decrement_vpc_count
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,2484]
	add	w0, w0, 1
	strh	w0, [x1,2484]
	b	.L2902
.L2909:
	add	x0, x19, :lo12:.LANCHOR2
	strb	wzr, [x25,600]
	ldrb	w0, [x0,1220]
	cbz	w0, .L2910
	mov	w0, w27
	bl	ftl_get_blk_mode
	strb	w0, [x25,600]
.L2910:
	add	x24, x25, 592
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x25,1386]
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x25,592]
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x1,lsl 1]
	strh	w0, [x25,1388]
	strh	wzr, [x25,594]
	strb	wzr, [x25,598]
.L2901:
	ldr	w0, [x29,132]
	cmp	w0, 1
	bne	.L2911
	bl	FtlReadRefresh
.L2911:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	str	w1, [x0,2448]
	ldrh	w1, [x0,2344]
	str	w1, [x29,136]
	ldrb	w1, [x0,1220]
	cbz	w1, .L2912
	ldrb	w1, [x25,600]
	cmp	w1, 1
	bne	.L2912
	ldrh	w0, [x0,2346]
	str	w0, [x29,136]
.L2912:
	ldrh	w0, [x25,594]
	ldr	w2, [x29,136]
	add	w1, w0, w22
	cmp	w1, w2
	ble	.L2913
	sub	w22, w2, w0
	uxth	w22, w22
.L2913:
	mov	w27, 0
.L2914:
	cmp	w22, w27, uxth
	bls	.L2921
	add	x1, x25, 592
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w4, [x1,2]
	ldrh	w7, [x0,2276]
	mov	x0, 0
	add	w4, w4, w27
	mov	w24, w0
.L2922:
	cmp	w7, w0, uxth
	bls	.L3023
	add	x2, x1, x0, lsl 1
	mov	w3, 65535
	ldrh	w2, [x2,16]
	cmp	w2, w3
	beq	.L2915
	add	x6, x19, :lo12:.LANCHOR2
	mov	w3, 56
	orr	w2, w4, w2, lsl 10
	umull	x5, w24, w3
	ldr	x6, [x6,2552]
	add	w24, w24, 1
	add	x5, x6, x5
	uxth	w24, w24
	str	w2, [x5,4]
.L2915:
	add	x0, x0, 1
	b	.L2922
.L3023:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w2, [x25,600]
	mov	w1, w24
	mov	x28, 0
	ldr	x0, [x0,2552]
	bl	FlashReadPages
	add	x0, x23, :lo12:.LANCHOR4
	str	x0, [x29,120]
.L2917:
	cmp	w24, w28, uxth
	bls	.L3024
	add	x26, x19, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x5, x28, x0
	ldr	x0, [x26,2552]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x6, [x1,16]
	beq	.L2951
	ldrh	w0, [x6]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2951
	ldr	w0, [x6,8]
	add	x1, x29, 152
	mov	w2, 0
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	log2phys
	ldr	x2, [x26,2552]
	ldr	x5, [x29,104]
	ldr	w0, [x29,152]
	add	x2, x2, x5
	ldr	x6, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2951
	ldrh	w0, [x25,1386]
	mov	x3, 56
	ldr	x1, [x26,2528]
	add	w0, w0, 1
	strh	w0, [x25,1386]
	ldr	w0, [x25,1168]
	str	x5, [x29,96]
	str	x6, [x29,104]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,112]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,112]
	mov	x2, 56
	ldr	x5, [x29,96]
	ldr	x6, [x29,104]
	str	w0, [x1,4]
	ldr	w0, [x25,1168]
	ldr	x1, [x26,2528]
	madd	x0, x0, x2, x1
	ldr	x1, [x26,2552]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,152]
	str	w0, [x6,12]
	ldr	x0, [x29,120]
	add	x2, x0, 880
	str	x2, [x29,112]
	ldrh	w0, [x0,880]
	strh	w0, [x6,2]
	ldr	w0, [x26,2400]
	str	w0, [x6,4]
	ldr	w0, [x25,1168]
	add	w0, w0, 1
	str	w0, [x25,1168]
	ldr	x0, [x26,2552]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	ldrb	w0, [x26,1220]
	cbnz	w0, .L2919
	ldr	x2, [x29,112]
	ldr	w1, [x25,1168]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2919
	ldrh	w0, [x2,4]
	cbnz	w0, .L2951
.L2919:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2951
	add	x20, x20, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	str	wzr, [x19,2448]
	strh	w0, [x20,592]
	strh	wzr, [x20,594]
	b	.L3020
.L2951:
	add	x28, x28, 1
	b	.L2917
.L3024:
	add	w27, w27, 1
	b	.L2914
.L2921:
	ldrh	w0, [x25,594]
	add	w22, w22, w0
	ldr	w0, [x29,136]
	uxth	w22, w22
	strh	w22, [x25,594]
	cmp	w22, w0
	bcc	.L2923
	ldr	w0, [x25,1168]
	cbz	w0, .L2924
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2924
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x25,1384]
	str	wzr, [x19,2448]
	b	.L2994
.L2924:
	ldrh	w22, [x25,1386]
	cbnz	w22, .L2925
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x25,592]
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L2925
.L2926:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2924]
	cmp	w22, w0
	bcs	.L2931
	mov	w0, w22
	add	x1, x29, 156
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L2927
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x25,592]
	cmp	w1, w0, uxth
	bne	.L2927
.L2931:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2924]
	cmp	w22, w1
	bcc	.L2925
	ldrh	w1, [x25,592]
	ldr	x0, [x0,2712]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x25,592]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2925
.L2927:
	add	w22, w22, 1
	b	.L2926
.L2925:
	mov	w0, -1
	strh	w0, [x25,592]
.L2923:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	cmp	w0, 2
	bhi	.L2932
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w22, [x0,2344]
	b	.L2933
.L2932:
	add	x20, x20, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x20,1384]
	str	wzr, [x19,2448]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2994
.L3022:
	add	x25, x19, :lo12:.LANCHOR2
	ldrh	w27, [x25,2472]
	cmp	w27, w21
	bne	.L2858
	ldrh	w0, [x26,776]
	mov	w3, 1024
	ldr	w2, [x25,2464]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w3, cc
	cmp	w2, w0
	bls	.L2858
	str	wzr, [x25,2464]
	strh	wzr, [x1,1384]
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	cmp	w21, w27
	bne	.L2860
	ldrh	w1, [x26,776]
	ldrh	w0, [x25,2482]
	cmp	w1, w0
	bcs	.L2861
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w21
	beq	.L2870
	ldr	w2, [x25,2388]
	uxtw	x1, w0
	cbnz	w2, .L2863
	ldrh	w0, [x25,2296]
	cmp	w0, 3
	beq	.L2863
	ldr	w0, [x25,2392]
	cbnz	w0, .L2863
	ldr	w0, [x25,2096]
	cbnz	w0, .L2863
	ldrb	w0, [x25,1220]
	cbz	w0, .L2864
.L2863:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2712]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x2,2346]
	ldrh	w0, [x2,2276]
	ldrh	w2, [x2,2296]
	mul	w3, w1, w0
	cmp	w2, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w3, w0
	cmp	w4, w0
	bgt	.L2866
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x1, x19, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR4
	ldr	w0, [x1,2924]
	ldr	w2, [x2,1068]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2867
	mov	w0, 128
	b	.L3017
.L2867:
	mov	w0, 160
.L3017:
	strh	w0, [x1,2482]
	b	.L2868
.L2866:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L3018
.L2864:
	ldr	x2, [x25,2712]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2869
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,2482]
	b	.L2868
.L2869:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L3018:
	strh	w1, [x0,2482]
	b	.L2870
.L2861:
	mov	w0, 80
	strh	w0, [x25,2482]
	b	.L2870
.L2868:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2870
.L2860:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x21, 1, 32
	add	x2, x23, :lo12:.LANCHOR4
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	mov	w1, w21
	ldr	x3, [x5,2712]
	ldr	x6, [x5,2696]
	ldrh	w2, [x2,776]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,2480]
	ldrh	w4, [x6,x4]
	bl	printk
.L2870:
	bl	FtlGcReFreshBadBlk
	b	.L2858
.L3021:
	mov	w21, w0
	b	.L2858
.L2994:
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR5
	add	x0, x23, :lo12:.LANCHOR5
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	adrp	x19, .LANCHOR2
	ldr	x20, [x0,1392]
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w22, [x0,2928]
	cbnz	w22, .L3027
	ldr	w1, [x0,2496]
	cbz	w1, .L3027
	ldrb	w0, [x0,1220]
	cbz	w0, .L3029
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L3029:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w3, [x20,9]
	adrp	x26, .LC159
	mov	w2, w22
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2560]
	add	x26, x26, :lo12:.LC159
	bl	FlashProgPages
.L3030:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2496]
	cmp	w24, w1
	bcs	.L3048
	umull	x21, w24, w27
	ldr	x1, [x0,2560]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L3051
	ldr	w1, [x0,4]
	cbz	w22, .L3065
	orr	w1, w1, -2147483648
.L3065:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,124]
	add	x25, x19, :lo12:.LANCHOR2
	add	x1, x29, 124
	bl	log2phys
	ldr	x0, [x25,2560]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3034
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x25,2712]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3035
	mov	x0, x26
	bl	printk
.L3035:
	mov	w0, w21
	bl	decrement_vpc_count
.L3034:
	add	w24, w24, 1
	b	.L3030
.L3067:
	mov	w20, 16386
.L3047:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2486]
	cbz	w0, .L3048
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L3047
.L3048:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,2496]
	b	.L3027
.L3051:
	adrp	x26, .LC159
	add	x0, x23, :lo12:.LANCHOR5
	mov	w24, 0
	mov	w27, 56
	str	x0, [x29,104]
	add	x26, x26, :lo12:.LC159
.L3031:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2496]
	cmp	w24, w1
	bcs	.L3067
	umull	x21, w24, w27
	ldr	x0, [x0,2560]
	mov	w28, 1
	str	w25, [x0,x21]
.L3037:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2560]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3068
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L3038
	ldr	x3, [x2,2712]
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2344]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L3038:
	ldrh	w0, [x20,4]
	cbnz	w0, .L3039
	mov	x0, x20
	bl	allocate_new_data_superblock
.L3039:
	ldr	x0, [x29,104]
	add	x23, x19, :lo12:.LANCHOR2
	add	x1, x0, 640
	ldr	w0, [x0,736]
	add	w0, w0, 1
	str	w0, [x1,96]
	ldr	x0, [x23,2560]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x23,2560]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x23,2560]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x23,2560]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3040
	str	w28, [x23,2928]
.L3040:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2928]
	cbz	w0, .L3037
	b	.L3027
.L3068:
	ldr	w0, [x1,4]
	cbz	w22, .L3066
	orr	w0, w0, -2147483648
.L3066:
	str	w0, [x29,124]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x28, x19, :lo12:.LANCHOR2
	add	x1, x29, 124
	bl	log2phys
	ldr	x0, [x28,2560]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L3044
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x28,2712]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L3045
	mov	x0, x26
	bl	printk
.L3045:
	mov	w0, w21
	bl	decrement_vpc_count
.L3044:
	add	w24, w24, 1
	b	.L3031
.L3027:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 128
	ret
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	adrp	x0, .LANCHOR2+2928
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+2928]
	str	x19, [sp,16]
	cbnz	w0, .L3070
	adrp	x0, .LANCHOR1+3448
	ldr	w19, [x0,#:lo12:.LANCHOR1+3448]
	cmp	w19, 1
	bne	.L3070
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, w19
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3070:
	mov	w0, 0
	ldr	x19, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3448
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+3448]
	cmp	w0, 1
	bne	.L3072
	bl	FtlSysFlush
.L3072:
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
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x23, [sp,48]
	add	x21, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	mov	w19, w1
	mov	w20, w0
	ldr	w1, [x21,1224]
	cmp	w19, w1
	bhi	.L3083
	cmp	w0, w1
	bcs	.L3083
	add	w2, w0, w19
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3076
	cmp	w19, 31
	bls	.L3091
	ldr	w0, [x21,2928]
	cbnz	w0, .L3091
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2350]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3079
	sub	w20, w1, w20
	add	w21, w21, 1
	cmp	w20, w19
	csel	w20, w20, w19, ls
	sub	w19, w19, w20, uxth
.L3079:
	mov	w0, -1
	adrp	x23, .LANCHOR5
	str	w0, [x29,76]
.L3080:
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w0, [x20,2350]
	cmp	w19, w0
	bcc	.L3092
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3081
	add	x1, x23, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,1400]
	add	w0, w0, 1
	str	w0, [x1,1400]
	ldr	w0, [x20,2412]
	add	x1, x29, 76
	add	w0, w0, 1
	str	w0, [x20,2412]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3081:
	add	x0, x22, :lo12:.LANCHOR2
	add	w21, w21, 1
	ldrh	w0, [x0,2350]
	sub	w19, w19, w0
	b	.L3080
.L3092:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,1400]
	cmp	w1, 32
	bls	.L3091
	str	wzr, [x0,1400]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3091:
	mov	w0, 0
	b	.L3076
.L3083:
	mov	w0, -1
.L3076:
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
	stp	x29, x30, [sp, -112]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x27, [sp,80]
	add	x2, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w3, [x2,2928]
	ldrh	w1, [x2,2344]
	cbnz	w3, .L3129
	adrp	x20, .LANCHOR4
	mov	w4, 65535
	add	x2, x20, :lo12:.LANCHOR4
	ldrh	w21, [x2,880]
	cmp	w21, w4
	bne	.L3096
.L3105:
	add	x22, x20, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR5
	add	x23, x21, :lo12:.LANCHOR5
	mov	w0, 65535
	add	x24, x22, 880
	ldrh	w1, [x22,880]
	str	wzr, [x23,1192]
	cmp	w1, w0
	beq	.L3129
	bl	FtlCacheWriteBack
	mov	w25, 12
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x24,7]
	ldrh	w3, [x22,880]
	mov	w24, 0
	ldrh	w4, [x0,2344]
	ldr	x2, [x0,2712]
	mul	w1, w1, w4
	strh	w1, [x2,x3,lsl 1]
	ldrh	w2, [x23,1202]
	mov	x23, x22
	ldr	w1, [x0,2404]
	add	w1, w2, w1
	str	w1, [x0,2404]
	b	.L3106
.L3096:
	cbz	w0, .L3099
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3452]
	cmp	w5, w4
	beq	.L3100
.L3101:
	mov	w1, 2
	b	.L3099
.L3100:
	strh	w3, [x0,3452]
	ldrh	w0, [x2,776]
	cmp	w0, 17
	bhi	.L3101
.L3099:
	add	x0, x20, :lo12:.LANCHOR4
	add	x0, x0, 880
	bl	FtlGcScanTempBlk
	str	w0, [x29,108]
	cmn	w0, #1
	beq	.L3102
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x21, x21, 1, 16
	ldr	x1, [x19,2696]
	ldrh	w0, [x1,x21]
	cmp	w0, 4
	bls	.L3103
	sub	w0, w0, #5
	strh	w0, [x1,x21]
	mov	w0, 1
	bl	FtlEctTblFlush
.L3103:
	adrp	x0, .LANCHOR5
	add	x1, x0, :lo12:.LANCHOR5
	mov	x19, x0
	ldr	w2, [x1,1192]
	cbnz	w2, .L3104
	ldr	w0, [x1,736]
	add	w0, w0, 1
	str	w0, [x1,736]
	ldr	w0, [x29,108]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3104:
	add	x0, x19, :lo12:.LANCHOR5
	str	wzr, [x0,1192]
	mov	w0, 1
	b	.L3095
.L3102:
	adrp	x0, .LANCHOR1+3452
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3452]
	mov	w0, 1
	cmp	w2, w1
	bne	.L3095
	b	.L3105
.L3109:
	ldr	w1, [x22,4]
	cmp	w0, w1
	bne	.L3127
.L3108:
	add	w24, w24, 1
	uxth	w24, w24
.L3106:
	add	x0, x21, :lo12:.LANCHOR5
	ldrh	w0, [x0,1202]
	cmp	w0, w24
	bls	.L3130
	add	x1, x19, :lo12:.LANCHOR2
	umull	x26, w24, w25
	ldr	x27, [x1,2512]
	ldr	w1, [x1,2924]
	add	x22, x27, x26
	ldr	w0, [x22,8]
	cmp	w0, w1
	bcs	.L3127
	add	x1, x29, 108
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,108]
	ldr	w1, [x27,x26]
	cmp	w0, w1
	bne	.L3109
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w26, w0
	ldr	w0, [x22,8]
	add	x1, x22, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w26
	b	.L3128
.L3127:
	ldrh	w0, [x23,880]
.L3128:
	bl	decrement_vpc_count
	b	.L3108
.L3130:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L3111
	add	x1, x20, :lo12:.LANCHOR4
	adrp	x0, .LC160
	add	x0, x0, :lo12:.LC160
	ldrh	w1, [x1,880]
	bl	printk
.L3111:
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,880]
	ldr	x1, [x1,2712]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L3112
	bl	INSERT_DATA_LIST
	b	.L3113
.L3112:
	bl	INSERT_FREE_LIST
.L3113:
	add	x22, x21, :lo12:.LANCHOR5
	add	x23, x20, :lo12:.LANCHOR4
	mov	w24, -1
	strh	wzr, [x22,1202]
	strh	w24, [x23,880]
	strh	wzr, [x22,1200]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w24, [x22,592]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,2096]
	cbz	w0, .L3114
	ldr	w0, [x1,2444]
	cmp	w0, 39
	bhi	.L3114
	ldrh	w0, [x22,588]
	ldrh	w2, [x23,776]
	cmp	w2, w0
	bcs	.L3129
	lsl	w0, w0, 1
	strh	w0, [x1,2480]
.L3129:
	mov	w0, 0
	b	.L3095
.L3114:
	add	x21, x21, :lo12:.LANCHOR5
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w1, [x21,588]
	ldrh	w0, [x20,776]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L3129
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,1220]
	cbz	w0, .L3115
	sub	w1, w1, #2
	strh	w1, [x19,2480]
	b	.L3129
.L3115:
	mov	w1, 20
	strh	w1, [x19,2480]
.L3095:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldr	x27, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	adrp	x19, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR4
	str	x21, [sp,32]
	add	x21, x19, 880
	mov	x0, x21
	ldrh	w1, [x20,2344]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,882]
	ldrh	w0, [x20,2344]
	cmp	w1, w0
	bcc	.L3131
	add	x0, x19, 928
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, .LANCHOR5+1192
	str	wzr, [x0,#:lo12:.LANCHOR5+1192]
.L3131:
	ldp	x19, x20, [sp,16]
	ldr	x21, [sp,32]
	ldp	x29, x30, [sp], 48
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR5+1248
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	str	wzr, [x0,#:lo12:.LANCHOR5+1248]
	add	x19, x19, :lo12:.LANCHOR4
	add	x20, x19, 784
	add	x19, x19, 832
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
	stp	x29, x30, [sp, -64]!
	mov	w1, -1
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x20, .LANCHOR2
	adrp	x21, .LANCHOR5
	str	x23, [sp,48]
	add	x0, x21, :lo12:.LANCHOR5
	add	x23, x20, :lo12:.LANCHOR2
	adrp	x19, .LANCHOR4
	strh	w1, [x0,1242]
	strh	wzr, [x0,1244]
	ldrh	w0, [x23,2280]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	x0, x19, :lo12:.LANCHOR4
	ldrh	w1, [x0,1072]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3135
.L3137:
	mov	w22, -1
	b	.L3136
.L3135:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L3137
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x23,2382]
	mov	x1, 0
	ldr	x0, [x23,2792]
.L3138:
	cmp	w1, w2
	mov	w3, w1
	bge	.L3142
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L3138
.L3142:
	add	x0, x19, :lo12:.LANCHOR4
	cmp	w3, w2
	ldrh	w1, [x0,724]
	add	w1, w1, 1
	strh	w1, [x0,724]
	blt	.L3139
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x21,1244]
	cbz	w0, .L3143
.L3139:
	add	x1, x19, :lo12:.LANCHOR4
	add	x2, x20, :lo12:.LANCHOR2
	ldrh	w3, [x1,784]
	ldr	x5, [x2,2712]
	lsl	x3, x3, 1
	ldrh	w6, [x1,788]
	ldrh	w4, [x5,x3]
	sub	w4, w4, w6
	strh	w4, [x5,x3]
	strb	wzr, [x1,790]
	ldrh	w3, [x2,2344]
	strh	w3, [x1,786]
	ldrh	w3, [x1,832]
	ldr	x5, [x2,2712]
	lsl	x3, x3, 1
	strh	wzr, [x1,788]
	ldrh	w6, [x1,836]
	ldrh	w4, [x5,x3]
	sub	w4, w4, w6
	strh	w4, [x5,x3]
	strb	wzr, [x1,838]
	ldrh	w0, [x1,726]
	ldrh	w2, [x2,2344]
	add	w0, w0, 1
	strh	w2, [x1,834]
	strh	wzr, [x1,836]
	strh	w0, [x1,726]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3143:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x23, x20, 784
	ldrh	w0, [x20,784]
	cmp	w0, w1
	beq	.L3144
	ldrh	w1, [x20,788]
	cbnz	w1, .L3144
	ldrh	w1, [x20,836]
	add	x21, x20, 832
	cbnz	w1, .L3144
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,832]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x23
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3144:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbnz	w0, .L3145
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w0, [x19,724]
	and	w0, w0, 31
	cbnz	w0, .L3136
.L3145:
	bl	FtlVpcCheckAndModify
.L3136:
	mov	w0, w22
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
	stp	x29, x30, [sp, -64]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w21, [x20,2928]
	cbnz	w21, .L3153
	ldrh	w2, [x20,2380]
	mov	w1, w21
	ldr	x0, [x20,2776]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2380]
	mov	w1, w21
	ldr	x0, [x20,2768]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w21, [x20,2396]
	ldrh	w0, [x20,2280]
	str	w21, [x20,2400]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3154
	bl	FtlMakeBbt
.L3154:
	mov	w0, 0
.L3155:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,2350]
	cmp	w0, w2, lsl 7
	bge	.L3182
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,2616]
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	str	w2, [x4,x3]
	add	w0, w0, 1
	ldr	x2, [x1,2624]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	str	w1, [x2,x3]
	b	.L3155
.L3182:
	ldrh	w21, [x1,2284]
	mov	w20, 0
.L3157:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,2286]
	cmp	w0, w21
	bls	.L3183
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3157
.L3183:
	ldrh	w0, [x22,2276]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	blt	.L3159
	udiv	w0, w20, w0
	ldr	w20, [x22,2376]
	add	w0, w0, w20
	mov	w20, 0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22,2280]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w21, [x22,2284]
.L3160:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2286]
	cmp	w0, w21
	bls	.L3159
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3160
.L3159:
	mov	w21, 0
	mov	w22, w21
.L3162:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2284]
	cmp	w1, w22
	bls	.L3184
	mov	w0, w22
	mov	w1, 0
	add	w22, w22, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w21, w0
	uxth	w22, w22
	uxth	w21, w0
	b	.L3162
.L3184:
	ldrh	w1, [x0,2286]
	adrp	x22, .LANCHOR5
	ldrh	w2, [x0,2276]
	add	x6, x22, :lo12:.LANCHOR5
	str	w1, [x0,2468]
	ldr	w1, [x0,2288]
	udiv	w3, w1, w2
	str	w3, [x0,2924]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,588]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w21, w4
	ble	.L3164
	sub	w1, w1, w21
	udiv	w1, w1, w2
	str	w1, [x0,2924]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,588]
.L3164:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2096]
	cmp	w0, 1
	bne	.L3165
	add	x1, x22, :lo12:.LANCHOR5
	udiv	w0, w21, w2
	ldrh	w4, [x1,588]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,588]
.L3165:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L3166
	add	x1, x22, :lo12:.LANCHOR5
	udiv	w0, w21, w2
	ldrh	w4, [x1,588]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,588]
.L3166:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w1, [x6,2338]
	cbz	w1, .L3168
	add	x0, x22, :lo12:.LANCHOR5
	ldrh	w4, [x0,588]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,588]
	mul	w4, w1, w2
	cmp	w4, w21
	ble	.L3168
	add	w1, w1, 32
	str	w3, [x6,2924]
	add	w1, w5, w1
	strh	w1, [x0,588]
.L3168:
	add	x23, x19, :lo12:.LANCHOR2
	add	x3, x22, :lo12:.LANCHOR5
	ldr	w1, [x23,2924]
	ldrh	w0, [x3,588]
	sub	w0, w1, w0
	ldrh	w1, [x23,2344]
	mul	w0, w0, w2
	str	w0, [x3,584]
	mul	w0, w0, w1
	ldrh	w1, [x23,2350]
	str	w0, [x23,2924]
	mul	w0, w0, w1
	str	w0, [x23,1224]
	bl	FtlBbmTblFlush
	ldr	w2, [x23,2292]
	add	w1, w21, w20
	ldrh	w0, [x23,2358]
	add	w0, w0, w2, lsr 3
	cmp	w1, w0
	bls	.L3170
	adrp	x0, .LC161
	lsr	w2, w2, 5
	add	x0, x0, :lo12:.LC161
	bl	printk
.L3170:
	add	x24, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR4
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, 0
	add	x21, x20, 784
	mov	w23, -1
	ldrh	w2, [x24,2286]
	ldr	x0, [x24,2712]
	lsl	w2, w2, 1
	bl	ftl_memset
	str	wzr, [x20,1068]
	mov	w0, 1
	strb	w0, [x21,8]
	adrp	x0, .LANCHOR0+16
	add	x1, x22, :lo12:.LANCHOR5
	ldrh	w2, [x24,2284]
	strh	wzr, [x21,2]
	ldr	x0, [x0,#:lo12:.LANCHOR0+16]
	lsr	w2, w2, 3
	strh	w23, [x1,592]
	strh	wzr, [x1,594]
	strb	wzr, [x1,598]
	strb	wzr, [x1,600]
	mov	w1, 255
	strb	wzr, [x21,6]
	strh	wzr, [x20,784]
	bl	ftl_memset
.L3171:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L3172
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldr	x0, [x0,2712]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L3171
.L3172:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x21]
	ldrh	w3, [x21,4]
	mov	w23, -1
	ldr	w0, [x1,2396]
	str	w0, [x21,12]
	add	w0, w0, 1
	str	w0, [x1,2396]
	ldr	x0, [x1,2712]
	strh	w3, [x0,x2,lsl 1]
	add	x0, x20, 832
	strh	wzr, [x20,834]
	ldrh	w1, [x21]
	mov	x21, x0
	strb	wzr, [x20,838]
	add	w1, w1, 1
	strh	w1, [x20,832]
	mov	w1, 1
	strb	w1, [x20,840]
.L3173:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L3174
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldr	x0, [x0,2712]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L3173
.L3174:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldrh	w2, [x21,4]
	add	x22, x22, :lo12:.LANCHOR5
	ldr	w0, [x19,2396]
	str	w0, [x21,12]
	mov	w21, -1
	add	w0, w0, 1
	str	w0, [x19,2396]
	ldr	x0, [x19,2712]
	strh	w2, [x0,x1,lsl 1]
	strh	w21, [x20,880]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20,1072]
	ldr	w0, [x22,584]
	strh	w0, [x20,1078]
	ldr	w0, [x19,2396]
	str	w0, [x20,1080]
	add	w0, w0, 1
	strh	wzr, [x20,1074]
	strh	w21, [x20,1076]
	str	w0, [x19,2396]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L3153
	adrp	x0, .LANCHOR1+3448
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+3448]
.L3153:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	str	x19, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1220]
	cbz	w0, .L3186
.L3188:
	mov	w0, 0
	b	.L3187
.L3186:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0,#:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	cbnz	w0, .L3188
	bl	FlashLoadFactorBbt
	cbz	w0, .L3189
	bl	FlashMakeFactorBbt
.L3189:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2216]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3190
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L3192:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L3192
	cmp	w0, 6
	bhi	.L3193
	adrp	x0, .LANCHOR0+89
	strb	w1, [x0,#:lo12:.LANCHOR0+89]
	b	.L3194
.L3193:
	mov	w1, 0
	mov	w4, 1
.L3196:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L3196
	cmp	w0, 17
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	bls	.L3215
	mov	w1, 36
.L3215:
	strb	w1, [x0,89]
.L3194:
	adrp	x1, .LANCHOR0+89
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x1,#:lo12:.LANCHOR0+89]
	strh	w1, [x0,1218]
.L3190:
	adrp	x0, .LC47
	adrp	x1, .LC144
	add	x1, x1, :lo12:.LC144
	add	x0, x0, :lo12:.LC47
	add	x19, x19, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, 1192
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,2280]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L3198:
	bl	FtlLoadBbt
	cbz	w0, .L3199
.L3217:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3218
	add	w19, w19, 1
	b	.L3198
.L3218:
	mov	w0, -1
	b	.L3187
.L3199:
	bl	FtlSysBlkInit
	cbnz	w0, .L3217
	adrp	x1, .LANCHOR1+3448
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+3448]
.L3187:
	ldr	x19, [sp,16]
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
	ldr	w1, [x0,2928]
	cbz	w1, .L3220
.L3223:
	mov	w0, 0
	b	.L3221
.L3220:
	ldrb	w0, [x0,1220]
	adrp	x22, .LANCHOR5
	cbz	w0, .L3222
	add	x0, x22, :lo12:.LANCHOR5
	ldr	w0, [x0,1168]
	tbz	x0, 0, .L3222
	adrp	x0, .LANCHOR4+884
	ldrh	w0, [x0,#:lo12:.LANCHOR4+884]
	cbnz	w0, .L3223
.L3222:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x22, :lo12:.LANCHOR5
	mov	w2, 0
	mov	w21, 0
	mov	w3, w2
	mov	w23, 56
	ldr	x0, [x0,2528]
	ldr	w1, [x1,1168]
	bl	FlashProgPages
.L3224:
	add	x20, x22, :lo12:.LANCHOR5
	ldr	w1, [x20,1168]
	cmp	w21, w1
	bcs	.L3234
	add	x2, x19, :lo12:.LANCHOR2
	umull	x0, w21, w23
	ldr	x4, [x2,2528]
	add	x1, x4, x0
	ldr	w4, [x4,x0]
	cmn	w4, #1
	ldr	x3, [x1,16]
	bne	.L3225
	adrp	x1, .LANCHOR4
	ldr	x3, [x2,2712]
	add	x1, x1, :lo12:.LANCHOR4
	ldrh	w5, [x1,880]
	strh	wzr, [x3,x5,lsl 1]
	strh	w4, [x1,880]
	ldr	w1, [x20,736]
	add	w1, w1, 1
	str	w1, [x20,736]
	ldr	x1, [x2,2528]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3233
.L3225:
	ldr	w0, [x3,12]
	add	w21, w21, 1
	ldr	w1, [x1,4]
	ldr	w2, [x3,8]
	uxth	w21, w21
	bl	FtlGcUpdatePage
	b	.L3224
.L3234:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x19,2528]
	bl	FtlGcBufFree
	str	wzr, [x20,1168]
	adrp	x0, .LANCHOR4+884
	ldrh	w0, [x0,#:lo12:.LANCHOR4+884]
	cbnz	w0, .L3223
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L3233:
	mov	w0, 1
.L3221:
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
	adrp	x19, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x0, 880
	ldrh	w2, [x0,880]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3236
	ldrh	w0, [x1,4]
	cbnz	w0, .L3237
.L3236:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 880
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strh	wzr, [x0,1200]
	strh	wzr, [x0,1202]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3237:
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 880
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
	mov	w4, w0
	adrp	x0, .LANCHOR1+3448
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x21, x22, [sp,48]
	ldr	w21, [x0,#:lo12:.LANCHOR1+3448]
	mov	w0, -1
	stp	x19, x20, [sp,32]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmp	w21, 1
	bne	.L3239
	cmp	w4, 16
	mov	x24, x3
	mov	w25, w2
	mov	w20, w1
	bne	.L3240
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3239
.L3240:
	adrp	x19, .LANCHOR2
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,1224]
	cmp	w20, w2
	bcs	.L3263
	cmp	w25, w2
	bhi	.L3263
	add	w0, w20, w25
	str	w0, [x29,188]
	mov	w0, -1
	ldr	w3, [x29,188]
	cmp	w3, w2
	bhi	.L3239
	adrp	x22, .LANCHOR5
	ldrh	w0, [x1,2350]
	add	x2, x22, :lo12:.LANCHOR5
	sub	w26, w3, #1
	udiv	w28, w20, w0
	sub	w21, w21, w28
	udiv	w26, w26, w0
	ldr	w0, [x2,1156]
	add	w0, w25, w0
	str	w0, [x2,1156]
	ldr	w0, [x1,2424]
	add	w21, w21, w26
	add	w0, w21, w0
	str	w0, [x1,2424]
	mov	w0, w28
	mov	w1, w26
	bl	FtlCacheMetchLpa
	str	x22, [x29,120]
	cbz	w0, .L3241
	bl	FtlCacheWriteBack
.L3241:
	mov	w27, 0
	adrp	x0, .LC57
	add	x0, x0, :lo12:.LC57
	mov	w22, w28
	str	w27, [x29,184]
	mov	w23, w27
	str	w27, [x29,172]
	str	x0, [x29,112]
.L3242:
	cbz	w21, .L3286
	mov	w0, w22
	add	x1, x29, 204
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,204]
	cmn	w3, #1
	bne	.L3284
	mov	w3, 0
.L3243:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2350]
	cmp	w3, w0
	bcs	.L3247
	madd	w0, w22, w0, w3
	cmp	w0, w20
	bcc	.L3245
	ldr	w1, [x29,188]
	cmp	w0, w1
	bcs	.L3245
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x24, x0
	str	x3, [x29,176]
	bl	ftl_memset
	ldr	x3, [x29,176]
.L3245:
	add	w3, w3, 1
	b	.L3243
.L3284:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w22, w28
	umull	x1, w23, w0
	ldr	x0, [x2,2520]
	add	x0, x0, x1
	str	w3, [x0,4]
	ldrh	w0, [x2,2350]
	bne	.L3248
	ldr	x3, [x2,2520]
	ldr	x2, [x2,2616]
	add	x3, x3, x1
	str	x2, [x3,8]
	udiv	w2, w20, w0
	msub	w2, w2, w0, w20
	str	w2, [x29,144]
	sub	w2, w0, w2
	cmp	w2, w25
	csel	w2, w2, w25, ls
	str	w2, [x29,184]
	cmp	w2, w0
	bne	.L3249
	str	x24, [x3,8]
	b	.L3249
.L3248:
	cmp	w22, w26
	bne	.L3250
	ldr	x3, [x2,2520]
	ldr	x2, [x2,2624]
	add	x3, x3, x1
	ldr	w4, [x29,188]
	str	x2, [x3,8]
	mul	w2, w22, w0
	sub	w27, w4, w2
	cmp	w27, w0
	bne	.L3249
	sub	w2, w2, w20
	ubfiz	x2, x2, 9, 23
	add	x2, x24, x2
	str	x2, [x3,8]
	b	.L3249
.L3250:
	ldr	x2, [x2,2520]
	mul	w0, w22, w0
	add	x2, x2, x1
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
	str	x0, [x2,8]
.L3249:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2520]
	add	x1, x0, x1
	ldrh	w0, [x2,2356]
	ldr	x2, [x2,2648]
	mul	w0, w23, w0
	str	w22, [x1,24]
	add	w23, w23, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3247:
	subs	w21, w21, #1
	add	w22, w22, 1
	beq	.L3251
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2276]
	cmp	w23, w0, lsl 3
	bne	.L3242
.L3251:
	cbz	w23, .L3242
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w23
	mov	w2, 0
	ldr	x0, [x0,2520]
	bl	FlashReadPages
	str	xzr, [x29,176]
	ldr	x0, [x29,144]
	ubfiz	x0, x0, 9, 23
	str	x0, [x29,136]
	ldr	w0, [x29,184]
	lsl	w0, w0, 9
	str	w0, [x29,168]
	lsl	w0, w27, 9
	str	w0, [x29,132]
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR5
	str	x0, [x29,160]
.L3253:
	ldr	w0, [x29,176]
	cmp	w23, w0
	bls	.L3287
	ldr	x0, [x29,176]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	mul	x8, x0, x1
	ldr	x0, [x2,2520]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w28
	bne	.L3254
	ldr	x1, [x0,8]
	ldr	x0, [x2,2616]
	cmp	x1, x0
	bne	.L3255
	ldr	x2, [x29,136]
	mov	x0, x24
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,168]
	b	.L3285
.L3254:
	cmp	w1, w26
	bne	.L3255
	ldr	x1, [x0,8]
	ldr	x0, [x2,2624]
	cmp	x1, x0
	bne	.L3255
	ldrh	w0, [x2,2350]
	ldr	w2, [x29,132]
	str	x8, [x29,104]
	mul	w0, w26, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x24, x0
.L3285:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3255:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	add	x2, x0, x8
	ldr	w1, [x0,x8]
	cmn	w1, #1
	bne	.L3256
	ldr	x0, [x29,160]
	str	w1, [x29,172]
	add	x3, x0, 640
	ldr	w0, [x0,712]
	add	w0, w0, 1
	str	w0, [x3,72]
.L3256:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3257
	ldr	x0, [x29,160]
	str	x8, [x29,104]
	add	x1, x0, 640
	ldr	w0, [x0,712]
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
.L3257:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2520]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3258
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3258:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L3253
.L3287:
	mov	w23, 0
	b	.L3242
.L3286:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,2486]
	cbz	w0, .L3261
	mov	w0, w21
	mov	w1, 1
	bl	ftl_do_gc
.L3261:
	ldr	w0, [x29,172]
	b	.L3239
.L3263:
	mov	w0, -1
.L3239:
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
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	stp	x29, x30, [sp, -16]!
	mov	w5, w0
	mov	w4, w1
	add	x29, sp, 0
	mov	x3, x2
	mov	w1, w5
	mov	w2, w4
	mov	w0, 16
	bl	ftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	2
	.global	ftl_sys_read
	.type	ftl_sys_read, %function
ftl_sys_read:
	stp	x29, x30, [sp, -16]!
	mov	w4, w1
	mov	x3, x2
	add	x29, sp, 0
	add	w1, w0, 256
	mov	w2, w4
	mov	w0, 16
	bl	ftl_read
	ldp	x29, x30, [sp], 16
	ret
	.size	ftl_sys_read, .-ftl_sys_read
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	stp	x29, x30, [sp, -64]!
	mov	w0, -1
	adrp	x1, .LC144
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR1
	add	x1, x1, :lo12:.LC144
	add	x23, x23, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	adrp	x22, .LANCHOR2
	adrp	x20, .LANCHOR5
	add	x21, x22, :lo12:.LANCHOR2
	str	w0, [x23,3448]
	add	x20, x20, :lo12:.LANCHOR5
	adrp	x0, .LC47
	add	x0, x0, :lo12:.LC47
	str	wzr, [x21,2928]
	str	wzr, [x20,1404]
	bl	printk
	add	x0, x21, 1192
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x21,2280]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3291
	adrp	x0, .LC162
	add	x0, x0, :lo12:.LC162
	b	.L3305
.L3291:
	bl	FtlSysBlkInit
	mov	w19, w0
	cbz	w0, .L3293
	adrp	x0, .LC163
	add	x0, x0, :lo12:.LC163
.L3305:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 256
	bl	printk
	b	.L3292
.L3293:
	mov	w1, 1
	str	w1, [x23,3448]
	bl	ftl_do_gc
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x24, x1
	ldrh	w0, [x0,776]
	cmp	w0, 15
	bhi	.L3294
	mov	w23, 65535
	add	w21, w0, 2
.L3297:
	ldrh	w0, [x20,592]
	cmp	w0, w23
	bne	.L3295
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,2472]
	cmp	w0, w23
	bne	.L3295
	and	w0, w19, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3295:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	add	x0, x24, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	cmp	w0, w21
	bhi	.L3292
	add	w19, w19, 1
	cmp	w19, 4096
	bne	.L3297
	b	.L3292
.L3294:
	ldrb	w0, [x21,1220]
	cbz	w0, .L3292
	mov	w19, 128
.L3299:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3299
.L3292:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x29, x30, [sp], 64
	ret
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	stp	x29, x30, [sp, -288]!
	mov	w4, w0
	mov	w0, 0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	stp	x23, x24, [sp,48]
	mov	w24, w2
	add	x2, x22, :lo12:.LANCHOR2
	stp	x25, x26, [sp,64]
	stp	x19, x20, [sp,16]
	stp	x27, x28, [sp,80]
	mov	w23, w1
	mov	x25, x3
	ldr	w1, [x2,2928]
	cbnz	w1, .L3307
	adrp	x1, .LANCHOR1
	str	x1, [x29,176]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,3448]
	cmp	w3, 1
	bne	.L3307
	cmp	w4, 16
	bne	.L3308
	add	w0, w23, 256
	mov	w1, w24
	mov	x2, x25
	bl	FtlVendorPartWrite
	b	.L3307
.L3308:
	ldr	w1, [x2,1224]
	cmp	w23, w1
	bcs	.L3350
	cmp	w24, w1
	bhi	.L3350
	add	w3, w23, w24
	mov	w0, -1
	cmp	w3, w1
	bhi	.L3307
	adrp	x4, .LANCHOR5
	ldrh	w1, [x2,2350]
	add	x21, x4, :lo12:.LANCHOR5
	sub	w3, w3, #1
	mov	w0, 2048
	str	x4, [x29,216]
	udiv	w26, w23, w1
	cmp	w24, w1, lsl 1
	str	w0, [x21,1408]
	udiv	w0, w3, w1
	ldr	w3, [x2,2496]
	str	w0, [x29,208]
	sub	w27, w0, w26
	add	w20, w27, 1
	ldr	w0, [x2,2408]
	add	w0, w20, w0
	str	w0, [x2,2408]
	ldr	w0, [x21,1152]
	add	w0, w24, w0
	str	w0, [x21,1152]
	cset	w0, cs
	str	w0, [x29,212]
	cbz	w3, .L3310
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x2,2560]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w26, w0
	bne	.L3311
	ldr	w0, [x2,2416]
	add	w0, w0, 1
	str	w0, [x2,2416]
	ldr	w0, [x21,1412]
	add	w0, w0, 1
	str	w0, [x21,1412]
	msub	w0, w26, w1, w23
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w24
	csel	w19, w1, w24, ls
	ldr	x1, [x3,8]
	lsl	w20, w19, 9
	add	x0, x1, x0
	mov	w2, w20
	mov	x1, x25
	bl	ftl_memcpy
	cbnz	w27, .L3312
	ldr	w0, [x21,1412]
	cmp	w0, 2
	bgt	.L3312
.L3346:
	mov	w0, 0
	b	.L3307
.L3312:
	add	x25, x25, x20
	sub	w24, w24, w19
	add	w23, w23, w19
	add	w26, w26, 1
	mov	w20, w27
.L3311:
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1412]
.L3310:
	ldr	w1, [x29,208]
	mov	w0, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L3313
	bl	FtlCacheWriteBack
.L3313:
	ldr	x0, [x29,216]
	adrp	x2, .LANCHOR4
	add	x1, x2, :lo12:.LANCHOR4
	str	w26, [x29,192]
	add	x0, x0, :lo12:.LANCHOR5
	add	x21, x1, 784
	str	x2, [x29,128]
	str	x1, [x29,120]
	str	x21, [x0,1392]
	adrp	x0, .LC164
	add	x0, x0, :lo12:.LC164
	str	x21, [x29,184]
	str	x0, [x29,104]
.L3314:
	cbz	w20, .L3378
	ldrh	w0, [x21,4]
	cbnz	w0, .L3315
	ldr	x1, [x29,184]
	cmp	x21, x1
	bne	.L3316
	ldr	x0, [x29,120]
	add	x0, x0, 832
	ldrh	w19, [x0,4]
	cbnz	w19, .L3317
	bl	allocate_new_data_superblock
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3456]
.L3317:
	ldr	x0, [x29,184]
	bl	allocate_new_data_superblock
	ldr	x0, [x29,176]
	ldr	x1, [x29,120]
	add	x0, x0, :lo12:.LANCHOR1
	add	x21, x1, 832
	ldr	w0, [x0,3456]
	cbnz	w0, .L3318
.L3319:
	ldr	x21, [x29,184]
	b	.L3318
.L3316:
	ldr	x1, [x29,176]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3456]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3319
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3318:
	ldrh	w0, [x21,4]
	cbnz	w0, .L3320
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3320:
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	str	x21, [x0,1392]
.L3315:
	add	x2, x22, :lo12:.LANCHOR2
	ldrh	w1, [x21,4]
	str	xzr, [x29,200]
	ldr	w0, [x2,2492]
	ldr	w2, [x2,2496]
	sub	w0, w0, w2
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w20
	csel	w0, w0, w20, ls
	str	w0, [x29,156]
	ldr	w0, [x29,212]
	and	w0, w0, 1
	str	w0, [x29,116]
	add	w0, w24, w23
	str	w0, [x29,152]
.L3321:
	ldr	w0, [x29,200]
	ldr	w2, [x29,156]
	ldr	w1, [x29,192]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L3379
	ldrh	w1, [x21,4]
	cbz	w1, .L3354
	ldr	w1, [x29,208]
	cmp	w28, w1
	cset	w6, eq
	cbz	w0, .L3323
	ldr	w1, [x29,116]
	tst	w6, w1
	beq	.L3323
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w2, [x29,152]
	ldrh	w1, [x1,2350]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L3354
.L3323:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w28
	str	x6, [x29,96]
	add	x19, x22, :lo12:.LANCHOR2
	bl	log2phys
	mov	x0, x21
	bl	get_new_active_ppa
	ldr	w5, [x19,2496]
	mov	w4, 56
	ldr	x2, [x19,2560]
	str	x4, [x29,160]
	umull	x1, w5, w4
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x19,2356]
	ldr	x0, [x19,2560]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,144]
	ldr	x0, [x19,2664]
	ldr	x3, [x29,144]
	str	x0, [x29,136]
	add	x27, x0, x3
	ldrh	w0, [x19,2354]
	str	x27, [x1,16]
	str	w28, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x19,2608]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	mov	x0, x27
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w26
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3358
	ldr	x6, [x29,96]
	ldr	x4, [x29,160]
	cbz	w6, .L3324
	ldr	w0, [x29,152]
	ldrh	w19, [x19,2350]
	msub	w19, w28, w19, w0
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	uxth	w19, w19
	b	.L3327
.L3358:
	ldrh	w19, [x19,2350]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29,160]
	sub	w19, w19, w0
	cmp	w19, w24
	csel	w19, w19, w24, ls
.L3327:
	add	x2, x22, :lo12:.LANCHOR2
	ldrh	w0, [x2,2350]
	cmp	w19, w0
	bne	.L3328
	ldr	w0, [x29,196]
	mov	x1, x25
	cbnz	w0, .L3329
	mul	w1, w19, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
.L3329:
	ldr	w2, [x29,212]
	mov	w0, 56
	add	x4, x22, :lo12:.LANCHOR2
	cbz	w2, .L3330
	ldr	w2, [x4,2496]
	umull	x0, w2, w0
	ldr	x2, [x4,2560]
	add	x0, x2, x0
	b	.L3376
.L3330:
	ldr	w2, [x4,2496]
	umull	x0, w2, w0
	ldr	x2, [x4,2560]
	add	x0, x2, x0
	ldrh	w2, [x4,2354]
	ldr	x0, [x0,8]
	b	.L3377
.L3328:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3332
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2496]
	str	w28, [x29,256]
	umull	x0, w0, w1
	ldr	x1, [x2,2560]
	mov	w2, 0
	add	x0, x1, x0
	ldr	x1, [x0,8]
	ldr	x0, [x0,16]
	str	x1, [x29,240]
	mov	w1, 1
	str	x0, [x29,248]
	add	x0, x29, 232
	bl	FlashReadPages
	ldr	w0, [x29,232]
	cmn	w0, #1
	bne	.L3333
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,712]
	add	w1, w1, 1
	str	w1, [x0,712]
	b	.L3335
.L3333:
	ldr	w0, [x27,8]
	cmp	w0, w28
	beq	.L3335
	ldr	x0, [x29,216]
	mov	w2, w28
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,712]
	add	w1, w1, 1
	str	w1, [x0,712]
	ldr	x0, [x29,104]
	ldr	w1, [x27,8]
	bl	printk
	b	.L3335
.L3332:
	ldr	w0, [x2,2496]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2560]
	ldrh	w2, [x2,2354]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3335:
	ldr	w1, [x29,196]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3336
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w4, [x1,2496]
	umull	x0, w4, w0
	ldr	x4, [x1,2560]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x25
	b	.L3377
.L3336:
	add	x5, x22, :lo12:.LANCHOR2
	ldr	w4, [x5,2496]
	ldr	x1, [x5,2560]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2350]
	mul	w1, w28, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
	b	.L3377
.L3324:
	ldr	w0, [x29,212]
	ldrh	w1, [x19,2350]
	ldr	w2, [x19,2496]
	cbz	w0, .L3337
	mul	w1, w28, w1
	umull	x0, w2, w4
	sub	w1, w1, w23
	ldr	x2, [x19,2560]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x25, x1
.L3376:
	str	x1, [x0,8]
	b	.L3331
.L3337:
	umull	x0, w2, w4
	ldr	x2, [x19,2560]
	mul	w1, w28, w1
	add	x0, x2, x0
	ldrh	w2, [x19,2354]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x25, x1
.L3377:
	bl	ftl_memcpy
.L3331:
	ldr	x1, [x29,136]
	mov	w0, -3947
	ldr	x2, [x29,144]
	strh	w0, [x1,x2]
	add	x1, x22, :lo12:.LANCHOR2
	str	w28, [x27,8]
	ldr	w0, [x1,2400]
	str	w0, [x27,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,2400]
	ldr	w0, [x29,228]
	str	w0, [x27,12]
	ldrh	w0, [x21]
	strh	w0, [x27,2]
	ldr	w0, [x1,2496]
	add	w0, w0, 1
	str	w0, [x1,2496]
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L3321
.L3379:
	str	w28, [x29,192]
	mov	x0, x2
	b	.L3322
.L3354:
	str	w28, [x29,192]
.L3322:
	sub	w20, w20, w0
	ldr	w0, [x29,212]
	cbnz	w0, .L3341
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w1, [x0,2496]
	ldr	w0, [x0,2492]
	cmp	w1, w0
	bcs	.L3341
	ldrh	w0, [x21,4]
	cbz	w0, .L3341
.L3343:
	str	wzr, [x29,212]
	b	.L3314
.L3341:
	bl	FtlCacheWriteBack
	add	x0, x22, :lo12:.LANCHOR2
	cmp	w20, 1
	str	wzr, [x0,2496]
	bhi	.L3314
	b	.L3343
.L3378:
	ldr	w1, [x29,208]
	mov	w0, w20
	sub	w1, w1, w26
	bl	ftl_do_gc
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	cmp	w0, 5
	bls	.L3345
	cmp	w0, 31
	bhi	.L3346
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbnz	w0, .L3346
.L3345:
	ldr	x0, [x29,216]
	mov	w19, 65535
	mov	w23, 128
	add	x24, x0, :lo12:.LANCHOR5
.L3369:
	ldrh	w0, [x24,592]
	cmp	w0, w19
	bne	.L3347
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x0,2472]
	cmp	w1, w19
	bne	.L3347
	ldrh	w0, [x0,2474]
	cmp	w0, w19
	bne	.L3347
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3347:
	add	x21, x22, :lo12:.LANCHOR2
	mov	w0, 1
	mov	w1, w0
	strh	w23, [x21,2482]
	strh	w23, [x21,2480]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x21,2928]
	cbnz	w0, .L3346
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w0, [x0,776]
	cmp	w0, 2
	bhi	.L3346
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3369
	b	.L3346
.L3350:
	mov	w0, -1
.L3307:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 288
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
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrb	w1, [x0,1220]
	cbz	w1, .L3382
	adrp	x21, .LANCHOR5
	ldr	x2, [x0,2712]
	add	x1, x21, :lo12:.LANCHOR5
	adrp	x24, .LC165
	adrp	x20, .LANCHOR4
	add	x0, x24, :lo12:.LC165
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w23, [x1,1242]
	add	x26, x20, 784
	add	x25, x20, 832
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x20,784]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,832]
	mov	w20, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x26
	bl	allocate_new_data_superblock
	mov	x0, x25
	bl	allocate_new_data_superblock
.L3384:
	subs	w20, w20, #1
	beq	.L3388
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3384
.L3388:
	add	x20, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC165
	mov	w1, w23
	ldr	x2, [x20,2712]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x20,2712]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3386
	add	x0, x29, 80
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,2276]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3389:
	cmp	w5, w0, uxth
	bls	.L3396
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3390
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,2544]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,2544]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3390:
	add	x0, x0, 1
	b	.L3389
.L3396:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC166
	mov	w1, w23
	add	x0, x0, :lo12:.LC166
	ldr	x2, [x19,2712]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,2544]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,2544]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3386:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x21,1242]
.L3382:
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
	.global	idb_flash_slc_mode
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
	.global	g_nand_ecc_en
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
	.global	gFlashCurMode
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
	.type	__func__.20983, %object
	.size	__func__.20983, 11
__func__.20983:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.21730, %object
	.size	__func__.21730, 12
__func__.21730:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.21762, %object
	.size	__func__.21762, 17
__func__.21762:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.21781, %object
	.size	__func__.21781, 16
__func__.21781:
	.string	"FtlScanAllBlock"
	.type	__func__.22049, %object
	.size	__func__.22049, 17
__func__.22049:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.22029, %object
	.size	__func__.22029, 21
__func__.22029:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.21056, %object
	.size	__func__.21056, 8
__func__.21056:
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
	.string	"samsung RR %d row=%x,count %d,status=%d\n"
.LC13:
	.string	"ECC:%d\n"
.LC14:
	.string	"No.%d FLASH ID:%x %x %x %x %x %x\n"
.LC15:
	.string	"FlashLoadPhyInfo fail %x!!\n"
.LC16:
	.string	"Read pageadd=%x  ecc=%x err=%x\n"
.LC17:
	.string	"data:"
.LC18:
	.string	"spare:"
.LC19:
	.string	"ReadRetry pageadd=%x ecc=%x err=%x\n"
.LC20:
	.string	"FLFB:%d %d\n"
.LC21:
	.string	"prog error: = %x\n"
.LC22:
	.string	"prog read error: = %x\n"
.LC23:
	.string	"prog read REFRESH: = %x\n"
.LC24:
	.string	"prog read s error: = %x %x %x\n"
.LC25:
	.string	"prog read d error: = %x %x %x\n"
.LC26:
	.string	"FlashMakeFactorBbt %d\n"
.LC27:
	.string	"bad block:%d %d\n"
.LC28:
	.string	"FMFB:%d %d\n"
.LC29:
	.string	"E:bad block:%d\n"
.LC30:
	.string	"FMFB:Save %d %d\n"
.LC31:
	.string	"%s error allocating memory. return -1\n"
.LC32:
	.string	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
.LC33:
	.string	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
.LC34:
	.string	"FtlBbmTblFlush error:%x\n"
.LC35:
	.string	"FtlBbmTblFlush error = %x error count = %d\n"
.LC36:
	.string	"FtlFreeSysBlkQueueOut free count = %d\n"
.LC37:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
.LC38:
	.string	"FtlFreeSysBlkQueueOut = %x, free count = %d\n"
.LC39:
	.string	"FtlMapWritePage error = %x\n"
.LC40:
	.string	"FtlMapWritePage error = %x error count = %d\n"
.LC41:
	.string	"page map lost: %x %x\n"
.LC42:
	.string	"region_id = %x phyAddr = %x\n"
.LC43:
	.string	"map_ppn:"
.LC44:
	.string	"load_l2p_region refresh = %x phyAddr = %x\n"
.LC45:
	.string	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
.LC46:
	.string	"FtlVpcTblFlush error = %x error count = %d\n"
.LC47:
	.string	"%s\n"
.LC48:
	.string	"no ect"
.LC49:
	.string	"...%s enter...\n"
.LC50:
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC51:
	.string	"free blk vpc error %x = %x  %x\n"
.LC52:
	.string	"error_flag %x\n"
.LC53:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC54:
	.string	":"
.LC55:
	.string	"Ftlscanalldata = %x\n"
.LC56:
	.string	"scan lpa = %x ppa= %x\n"
.LC57:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC58:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC59:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC60:
	.string	"Mblk:"
.LC61:
	.string	"L2P:"
.LC62:
	.string	"L2PC:"
.LC63:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\n"
.LC64:
	.string	"superBlkID = %x vpc=%x\n"
.LC65:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC66:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC67:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC68:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC69:
	.string	"slc mode"
.LC70:
	.string	"ftl_scan_all_ppa blk %x page %x flag: %x\n"
.LC71:
	.string	"ftl_scan_all_ppa blk %x page %x flag: %x ............... is bad block\n"
.LC72:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC73:
	.string	"%s finished\n"
.LC74:
	.string	"FLASH INFO:\n"
.LC75:
	.string	"FLASH ID: %x\n"
.LC76:
	.string	"Device Capacity: %d MB\n"
.LC77:
	.string	"FMWAIT: %x %x %x %x\n"
.LC78:
	.string	"FTL INFO:\n"
.LC79:
	.string	"g_MaxLpn = 0x%x\n"
.LC80:
	.string	"g_VaildLpn = 0x%x\n"
.LC81:
	.string	"read_page_count = 0x%x\n"
.LC82:
	.string	"discard_page_count = 0x%x\n"
.LC83:
	.string	"write_page_count = 0x%x\n"
.LC84:
	.string	"cache_write_count = 0x%x\n"
.LC85:
	.string	"l2p_write_count = 0x%x\n"
.LC86:
	.string	"gc_page_count = 0x%x\n"
.LC87:
	.string	"totle_write = %d MB\n"
.LC88:
	.string	"totle_read = %d MB\n"
.LC89:
	.string	"GSV = 0x%x\n"
.LC90:
	.string	"GDV = 0x%x\n"
.LC91:
	.string	"bad blk num = %d %d\n"
.LC92:
	.string	"free_superblocks = 0x%x\n"
.LC93:
	.string	"mlc_EC = 0x%x\n"
.LC94:
	.string	"slc_EC = 0x%x\n"
.LC95:
	.string	"avg_EC = 0x%x\n"
.LC96:
	.string	"sys_EC = 0x%x\n"
.LC97:
	.string	"max_EC = 0x%x\n"
.LC98:
	.string	"min_EC = 0x%x\n"
.LC99:
	.string	"PLT = 0x%x\n"
.LC100:
	.string	"POT = 0x%x\n"
.LC101:
	.string	"MaxSector = 0x%x\n"
.LC102:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC103:
	.string	"sys_blks_pp = 0x%x\n"
.LC104:
	.string	"free sysblock = 0x%x\n"
.LC105:
	.string	"data_blks_pp = 0x%x\n"
.LC106:
	.string	"data_op_blks_pp = 0x%x\n"
.LC107:
	.string	"max_data_blks = 0x%x\n"
.LC108:
	.string	"Sys.id = 0x%x\n"
.LC109:
	.string	"Bbt.id = 0x%x\n"
.LC110:
	.string	"ACT.page = 0x%x\n"
.LC111:
	.string	"ACT.plane = 0x%x\n"
.LC112:
	.string	"ACT.id = 0x%x\n"
.LC113:
	.string	"ACT.mode = 0x%x\n"
.LC114:
	.string	"ACT.a_pages = 0x%x\n"
.LC115:
	.string	"ACT VPC = 0x%x\n"
.LC116:
	.string	"BUF.page = 0x%x\n"
.LC117:
	.string	"BUF.plane = 0x%x\n"
.LC118:
	.string	"BUF.id = 0x%x\n"
.LC119:
	.string	"BUF.mode = 0x%x\n"
.LC120:
	.string	"BUF.a_pages = 0x%x\n"
.LC121:
	.string	"BUF VPC = 0x%x\n"
.LC122:
	.string	"TMP.page = 0x%x\n"
.LC123:
	.string	"TMP.plane = 0x%x\n"
.LC124:
	.string	"TMP.id = 0x%x\n"
.LC125:
	.string	"TMP.mode = 0x%x\n"
.LC126:
	.string	"TMP.a_pages = 0x%x\n"
.LC127:
	.string	"GC.page = 0x%x\n"
.LC128:
	.string	"GC.plane = 0x%x\n"
.LC129:
	.string	"GC.id = 0x%x\n"
.LC130:
	.string	"GC.mode = 0x%x\n"
.LC131:
	.string	"GC.a_pages = 0x%x\n"
.LC132:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC133:
	.string	"Read Err = 0x%x\n"
.LC134:
	.string	"Prog Err = 0x%x\n"
.LC135:
	.string	"gc_free_blk_th= 0x%x\n"
.LC136:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC137:
	.string	"gc_skip_write_count= 0x%x\n"
.LC138:
	.string	"gc_blk_index= 0x%x\n"
.LC139:
	.string	"free min EC= 0x%x\n"
.LC140:
	.string	"free max EC= 0x%x\n"
.LC141:
	.string	"GC__SB VPC = 0x%x\n"
.LC142:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC143:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC144:
	.string	"FTL version: 5.0.63 20200923"
.LC145:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC146:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC147:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC148:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC149:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC150:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC151:
	.string	"decrement_vpc_count %x = %d\n"
.LC152:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC153:
	.string	"RSB refresh addr %x\n"
.LC154:
	.string	"spuer block %x vpn is 0\n "
.LC155:
	.string	"g_recovery_ppa %x ver %x\n "
.LC156:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC157:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC158:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC159:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC160:
	.string	"GC des block %x done\n"
.LC161:
	.string	"too many bad block  = %d %d\n"
.LC162:
	.string	"...%s: no bad block mapping table, format device\n"
.LC163:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC164:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC165:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC166:
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
	.size	NandFlashParaTbl, 2752
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
	.byte	4
	.byte	0
	.byte	1
	.byte	0
	.zero	4
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
	.hword	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.byte	1
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
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	60
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
	.hword	1479
	.byte	10
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	4
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
	.byte	4
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
	.byte	0
	.byte	0
	.zero	4
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	59
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.hword	192
	.byte	2
	.byte	2
	.hword	2048
	.hword	279
	.byte	12
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
	.byte	5
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
	.byte	1
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
	.type	gNandChipMap, %object
	.size	gNandChipMap, 64
gNandChipMap:
	.zero	64
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 8
gpNandParaInfo:
	.zero	8
	.type	gNandOptPara, %object
	.size	gNandOptPara, 32
gNandOptPara:
	.zero	32
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 1024
slcPageToMlcPageTbl:
	.zero	1024
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 2048
mlcPageToSlcPageTbl:
	.zero	2048
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
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.zero	2
	.zero	2
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
	.type	gFlashSlcMode, %object
	.size	gFlashSlcMode, 1
gFlashSlcMode:
	.zero	1
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.zero	1
	.zero	2
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
	.type	gFlashCurMode, %object
	.size	gFlashCurMode, 1
gFlashCurMode:
	.zero	1
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.zero	1
	.zero	1
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.zero	4
	.type	idb_flash_slc_mode, %object
	.size	idb_flash_slc_mode, 4
idb_flash_slc_mode:
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
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
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
	.type	g_nand_ecc_en, %object
	.size	g_nand_ecc_en, 1
g_nand_ecc_en:
	.zero	1
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.zero	1
	.zero	6
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
	.size	gSysInfo, 16
gSysInfo:
	.zero	16
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.zero	2
	.zero	6
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
