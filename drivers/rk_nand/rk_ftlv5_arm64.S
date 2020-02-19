/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
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
	add	x4, x3, 24
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
	bne	.L43
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L45
.L43:
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
.L45:
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
	cbz	w2, .L48
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
.L48:
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
	ldrb	w1, [x1,114]
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
.L57:
	cmp	w1, w2
	bls	.L59
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L57
.L59:
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
	mov	w20, w1
	cbnz	w0, .L62
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L63:
	add	x1, x3, 136
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L63
.L69:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 2048
	add	x0, x19, 1160
	uxth	w20, w20
	bl	ftl_memset
	mov	x0, 0
	mov	x2, x19
	b	.L64
.L62:
	cmp	w0, 1
	bne	.L65
	mov	x0, 0
	mov	w5, 3
	mov	w6, 2
	add	x4, x2, :lo12:.LANCHOR0
.L68:
	cmp	x0, 3
	uxth	w1, w0
	mov	w3, w1
	bls	.L66
	ubfiz	w3, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w5, w6, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L66:
	add	x1, x4, 136
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L68
	b	.L69
.L65:
	cmp	w0, 2
	bne	.L70
	mov	w1, 65535
	mov	x0, 0
	add	x5, x2, :lo12:.LANCHOR0
.L72:
	add	x4, x5, 136
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w1, w3, hi
	add	w1, w1, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L72
	b	.L69
.L70:
	cmp	w0, 3
	bne	.L73
	mov	x0, 0
	mov	w5, 5
	mov	w6, 4
	add	x4, x2, :lo12:.LANCHOR0
.L76:
	cmp	x0, 5
	uxth	w1, w0
	mov	w3, w1
	bls	.L74
	ubfiz	w3, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w5, w6, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L74:
	add	x1, x4, 136
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L76
	b	.L69
.L73:
	cmp	w0, 4
	bne	.L77
	add	x1, x2, :lo12:.LANCHOR0
	mov	w6, 7
	add	x3, x1, 136
	mov	w5, 6
	strh	w0, [x3,8]
	mov	w0, 5
	strh	wzr, [x1,136]
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
.L79:
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
	bne	.L79
	b	.L69
.L77:
	cmp	w0, 5
	bne	.L80
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L81:
	add	x1, x3, 136
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L81
	mov	x0, 0
.L82:
	add	x3, x1, x0
	add	w4, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w4, [x3,32]
	bne	.L82
	b	.L69
.L80:
	cmp	w0, 6
	bne	.L83
	mov	x3, 0
	mov	w6, 12
	mov	w7, 10
	add	x5, x2, :lo12:.LANCHOR0
.L86:
	cmp	x3, 5
	uxth	w4, w3
	mov	w0, w4
	bls	.L84
	add	w0, w4, w4, lsl 1
	and	w4, w4, 1
	cmp	w4, wzr
	uxth	w0, w0
	csel	w1, w6, w7, ne
	sub	w0, w0, w1
	uxth	w0, w0
.L84:
	add	x1, x5, 136
	strh	w0, [x1,x3,lsl 1]
	add	x3, x3, 1
	cmp	x3, 256
	bne	.L86
	b	.L69
.L83:
	cmp	w0, 9
	bne	.L87
	add	x0, x2, :lo12:.LANCHOR0
	add	x1, x0, 136
	strh	wzr, [x0,136]
	mov	w0, 1
	strh	w0, [x1,2]
	mov	w0, 2
	strh	w0, [x1,4]
	mov	x0, 0
.L88:
	add	x3, x1, x0
	add	w4, w0, 3
	add	x0, x0, 2
	cmp	x0, 506
	strh	w4, [x3,6]
	bne	.L88
	b	.L69
.L87:
	cmp	w0, 10
	bne	.L69
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L89:
	add	x1, x3, 136
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 63
	bne	.L89
	mov	x0, 0
.L90:
	add	x3, x1, x0
	add	w4, w0, 63
	add	x0, x0, 2
	cmp	x0, 898
	strh	w4, [x3,126]
	bne	.L90
	b	.L69
.L64:
	cmp	w20, w0, uxth
	bls	.L111
	add	x1, x2, 136
	add	x3, x2, 1160
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x3,w1,sxtw 1]
	b	.L64
.L111:
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
.L114:
	ldrb	w0, [x24]
	cmp	w0, w20
	bls	.L120
	mov	w0, 85
	str	w0, [x22,8]
	add	x0, x19, 256
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x26]
	cmp	w0, 34
	bne	.L115
	ldrsb	w0, [x25,x20]
	b	.L119
.L115:
	cmp	w0, 35
	bne	.L117
	ldrsb	w0, [x21,x20]
	b	.L119
.L117:
	ldrsb	w0, [x23,400]
.L119:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L114
.L120:
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
.L122:
	ldrb	w0, [x22]
	cmp	w0, w20
	bls	.L124
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
	b	.L122
.L124:
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
	strb	wzr, [x19,3208]
	add	x25, x19, 3212
	strb	wzr, [x19,3209]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	mov	x0, x25
	add	x24, x19, 3220
	adrp	x26, IDByte
	mov	x23, 0
	bl	ftl_memset
	add	x26, x26, :lo12:IDByte
	mov	w1, 0
	mov	w2, 32
	mov	x0, x24
	bl	ftl_memset
	add	x0, x19, 3252
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,3448]
	add	x22, x20, 1
.L127:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x26, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L126
	ldrb	w1, [x19,3208]
	str	w0, [x24,w1,sxtw 2]
	add	w0, w1, 1
	strb	w23, [x25,w1,sxtw]
	strb	w0, [x19,3208]
.L126:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L127
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w0, [x19,3208]
	strb	w0, [x19,3209]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L128
.L132:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldrb	w0, [x21,3208]
	ldr	x27, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x21,3456]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 96
	ret
.L128:
	adrp	x24, IDByte
	ldr	w26, [x19,92]
	mov	x23, 0
	add	x24, x24, :lo12:IDByte
	add	x25, x19, 3220
	add	x27, x19, 3212
.L131:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x24, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L129
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,3208]
	mul	w2, w1, w26
	ldrh	w1, [x20,14]
	mov	x0, x3
	and	w1, w1, 65280
	mul	w1, w2, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w2, [x20,23]
	cbz	w2, .L130
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L130:
	add	w0, w0, 1
	strb	w23, [x27,x3]
	strb	w0, [x19,3208]
.L129:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L131
	b	.L132
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
	add	x1, x1, 3464
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
.L144:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x23
	bl	FlashMemCmp8
	mov	w22, w0
	cbnz	w0, .L141
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 504
	adds	x20, x1, x20
	beq	.L147
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L146
.L141:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 83
	bne	.L144
	b	.L147
.L146:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3160]
	cmp	w4, w3
	beq	.L145
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L146
	mov	w0, w2
.L145:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3160
	add	x0, x0, :lo12:.LANCHOR0
	add	x1, x2, x1
	add	x0, x0, 104
	mov	w2, 32
	add	x19, x19, 472
	bl	ftl_memcpy
	mov	x0, x19
	mov	x1, x20
	mov	w2, 32
	bl	ftl_memcpy
	ldrh	w0, [x19,10]
	bl	FlashBlockAlignInit
	b	.L142
.L147:
	mov	w22, -1
.L142:
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
	ldr	x1, [x0,1160]
	ldr	w2, [x1]
	str	w2, [x0,1168]
	ldr	w2, [x1,4]
	str	w2, [x0,1172]
	ldr	w2, [x1,8]
	str	w2, [x0,1176]
	ldr	w2, [x1,12]
	str	w2, [x0,1180]
	ldr	w2, [x1,304]
	str	w2, [x0,1184]
	ldr	w2, [x1,308]
	str	w2, [x0,1188]
	ldr	w2, [x1,336]
	ldr	w1, [x1,344]
	str	w2, [x0,1192]
	str	w1, [x0,1196]
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
	add	x7, x7, 1200
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
	bne	.L154
	add	x1, x9, :lo12:.LANCHOR2
	ldrb	w1, [x1,1228]
	cbnz	w1, .L154
	add	x1, x7, :lo12:.LANCHOR0
	add	x1, x1, 136
	ldrh	w8, [x1,w8,sxtw 1]
.L154:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 3220
	mov	w1, 0
	ldr	w5, [x7,w11,uxtw 2]
	madd	w5, w6, w10, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w11, [x3]
	bls	.L155
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L155:
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
	ldr	x0, [x4,3448]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L158
	add	x4, x4, 104
	cbnz	w2, .L159
	ldrb	w2, [x4,13]
	b	.L167
.L159:
	ldrb	w2, [x4,14]
.L167:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,119]
	cbz	w4, .L162
	add	x3, x19, 8
	lsl	x3, x3, 8
.L166:
	cmp	w2, w4
	bcs	.L162
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L166
.L158:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L162:
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
.L172:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L172
	tbz	x3, 6, .L172
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
	ldr	x0, [x1,3448]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,92]
	add	x1, x1, 3212
	mul	w0, w0, w2
	strh	w0, [x19,4]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w2, [x0,1229]
	ldr	w0, [x0,1232]
	str	w0, [x19]
	strb	w2, [x19,7]
	ldrb	w4, [x1,-4]
	ldr	x0, [x1,236]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,236]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L176:
	cmp	w4, w0, uxtb
	bls	.L178
	ldrb	w2, [x0,x1]
	add	x0, x0, 1
	ldrb	w3, [x19,10]
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L176
.L178:
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
	ldr	w2, [x1,1236]
	str	w0, [x1,1236]
	mov	w0, w2
	ret
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	adrp	x0, .LANCHOR0+3448
	ldr	x0, [x0,#:lo12:.LANCHOR0+3448]
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
	adrp	x1, .LANCHOR2+1160
	uxtb	w0, w0
	lsl	w2, w0, 8
	orr	w0, w2, w0, lsl 16
	ldr	x1, [x1,#:lo12:.LANCHOR2+1160]
	orr	w0, w0, 1
	str	w0, [x1,304]
	ret
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	adrp	x1, .LANCHOR2+1160
	uxtb	w0, w0
	ldr	x2, [x1,#:lo12:.LANCHOR2+1160]
	mov	w1, 16640
	orr	w0, w0, w1
	str	w0, [x2,344]
	ret
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	adrp	x1, .LANCHOR2+1160
	cmp	w0, wzr
	ldr	x2, [x1,#:lo12:.LANCHOR2+1160]
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
	adrp	x1, .LANCHOR2+1160
	uxtb	w0, w0
	tst	w0, 6
	ldr	x2, [x1,#:lo12:.LANCHOR2+1160]
	ldr	w1, [x2]
	beq	.L189
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
	b	.L191
.L189:
	and	w1, w1, -8193
.L191:
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
	ldr	x0, [x0,3448]
	uxtb	w27, w1
	uxtb	w25, w3
	mov	x2, x4
	add	x19, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L197
	add	x19, x19, x23, lsl 6
	lsl	w4, w25, 2
	add	x19, x19, 1260
	b	.L203
.L197:
	cmp	w0, 7
	bne	.L199
	mov	x3, 160
	mov	w4, 10
	madd	x19, x23, x3, x19
	mul	w4, w25, w4
	add	x19, x19, 1268
.L203:
	add	x19, x19, x4, sxtw
	b	.L198
.L199:
	cmp	w0, 8
	bne	.L200
	add	x19, x19, 1268
	add	w4, w25, w25, lsl 2
	b	.L203
.L200:
	lsl	x4, x23, 3
	add	x4, x4, x25, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 1260
.L198:
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
.L201:
	cmp	x22, x27
	beq	.L204
	ldrb	w0, [x20,x22]
	str	w0, [x21,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19,x22]
	add	x22, x22, 1
	str	w0, [x21,2048]
	b	.L201
.L204:
	add	x24, x24, :lo12:.LANCHOR2
	mov	w0, 22
	add	x23, x24, x23
	str	w0, [x21,2056]
	mov	w0, w26
	bl	NandcFlashDeCs
	strb	w25, [x23,2096]
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
	adrp	x0, .LANCHOR0+3448
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	ldr	x0, [x0,#:lo12:.LANCHOR0+3448]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x19, 0
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L205
	adrp	x20, .LANCHOR2
	adrp	x21, IDByte
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:IDByte
	add	x22, x20, 1244
.L210:
	lsl	x1, x19, 3
	uxtb	w0, w19
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L207
	ldrb	w1, [x20,1241]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L207:
	add	x19, x19, 1
	cmp	x19, 4
	bne	.L210
.L205:
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
	add	x0, x20, 3252
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L214
	mov	w0, w21
	add	x20, x20, 3220
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
	cbz	x1, .L214
	str	w0, [x1]
	str	xzr, [x19,16]
.L214:
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
.L222:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L223
	mov	x0, 50
	bl	__const_udelay
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
	mov	x0, 750
	bl	__const_udelay
.L227:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L228
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L227
	mov	w0, -1
	b	.L226
.L228:
	mov	w0, 0
.L226:
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
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w0
	adrp	x0, .LANCHOR2+1228
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1228]
	cbz	w0, .L231
	mov	w0, w20
	bl	NandcFlashCs
	adrp	x2, .LANCHOR0
	sbfiz	x0, x20, 4, 32
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 24
	add	x1, x2, x0
	ldr	x19, [x2,x0]
	mov	w0, 239
	ldrb	w1, [x1,8]
	add	x19, x19, x1, lsl 8
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	x0, 500
	bl	__const_udelay
	str	wzr, [x19,2048]
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	mov	x0, 500
	str	wzr, [x19,2048]
	bl	__const_udelay
	mov	w0, w20
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 218
	str	w0, [x19,2056]
	mov	w0, w20
	bl	NandcWaitFlashReady
.L231:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	stp	x29, x30, [sp, -32]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxtb	w20, w0
	adrp	x0, .LANCHOR2+1228
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1228]
	cbz	w0, .L236
	mov	w0, w20
	bl	NandcFlashCs
	adrp	x2, .LANCHOR0
	sbfiz	x0, x20, 4, 32
	add	x2, x2, :lo12:.LANCHOR0
	add	x2, x2, 24
	add	x1, x2, x0
	ldr	x19, [x2,x0]
	mov	w0, 239
	ldrb	w1, [x1,8]
	add	x19, x19, x1, lsl 8
	str	w0, [x19,2056]
	mov	w0, 145
	str	w0, [x19,2052]
	mov	x0, 500
	bl	__const_udelay
	mov	w0, 2
	str	w0, [x19,2048]
	mov	w0, 1
	str	w0, [x19,2048]
	str	wzr, [x19,2048]
	mov	x0, 500
	str	wzr, [x19,2048]
	bl	__const_udelay
	mov	w0, w20
	bl	NandcWaitFlashReadyNoDelay
	mov	w0, 223
	str	w0, [x19,2056]
	mov	w0, w20
	bl	NandcWaitFlashReady
.L236:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
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
	adrp	x1, .LANCHOR2+2100
	adrp	x3, .LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x3, x3, :lo12:.LANCHOR0
	adrp	x7, IDByte
	add	x29, sp, 0
	ldrb	w1, [x1,#:lo12:.LANCHOR2+2100]
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
.L253:
	ldrb	w4, [x6,x7]
	ldr	x1, [x3]
	cmp	w4, 152
	ldrb	w2, [x3,8]
	beq	.L243
	cmp	w4, 69
	beq	.L243
	cmp	w4, 173
	beq	.L243
	cmp	w4, 44
	bne	.L244
.L243:
	cmp	w0, 1
	bne	.L246
	cbz	w11, .L244
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L247
	str	w0, [x1,2052]
	b	.L264
.L247:
	cmp	w4, 44
	bne	.L249
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L251
.L249:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L251
.L246:
	cbz	w5, .L244
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L250
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L251
.L250:
	cmp	w4, 44
	bne	.L252
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L251
.L252:
	str	w9, [x1,2052]
.L264:
	str	wzr, [x1,2048]
.L251:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L244:
	add	x6, x6, 8
	add	x3, x3, 16
	cmp	x6, 32
	bne	.L253
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
.L268:
	ldrb	w5, [x0]
	cmp	w5, w2
	bls	.L273
	ldrb	w5, [x4]
	cmp	w5, 67
	bne	.L269
	ldrsb	w5, [x3,x2]
	b	.L272
.L269:
	ldrsb	w5, [x1,x2]
.L272:
	str	w5, [x20]
	add	x2, x2, 1
	b	.L268
.L273:
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
	add	x25, x21, 3212
	add	x23, x23, :lo12:.LC1
.L276:
	cmp	w22, w24
	beq	.L285
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,3208]
	cmp	w2, w0
	bcc	.L277
	mov	w0, -1
	str	w0, [x20]
	b	.L278
.L277:
	uxtw	x2, w2
	mov	x0, 24
	ldrb	w19, [x25,x2]
	madd	x2, x2, x0, x21
	mov	w0, w19
	strb	w19, [x2,3252]
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
	tbz	x0, 0, .L279
	mov	w0, -1
	str	w0, [x20]
.L279:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L280
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L280:
	mov	w0, w19
	bl	NandcFlashDeCs
.L278:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L276
.L285:
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
	adrp	x20, .LANCHOR2
	stp	x25, x26, [sp,64]
	mov	w26, w0
	add	x0, x20, :lo12:.LANCHOR2
	add	x1, x0, 1240
	cmp	w26, 2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	strb	w26, [x0,1240]
	mov	w0, -84
	strb	w4, [x1,5]
	strb	w0, [x1,4]
	mov	w0, -81
	strb	w3, [x1,6]
	strb	w0, [x1,7]
	bne	.L287
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3305
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3305]
	b	.L350
.L287:
	cmp	w26, 3
	bne	.L289
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
	b	.L390
.L289:
	cmp	w26, 4
	bne	.L290
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
.L390:
	mov	w28, 8
	strb	w0, [x1,11]
	mov	w27, w28
	b	.L288
.L290:
	cmp	w26, 5
	bne	.L291
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w28, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L391
.L291:
	cmp	w26, 6
	bne	.L292
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w28, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L391
.L292:
	cmp	w26, 7
	bne	.L293
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
	b	.L288
.L293:
	cmp	w26, 8
	bne	.L350
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
	b	.L288
.L350:
	mov	w28, 7
.L391:
	mov	w27, 4
.L288:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L387
	adrp	x23, .LANCHOR0
	adrp	x24, .LANCHOR1
	add	x23, x23, :lo12:.LANCHOR0
	add	x24, x24, :lo12:.LANCHOR1
	mov	w21, 0
	add	x4, x23, 3212
	add	x5, x20, :lo12:.LANCHOR2
	add	x24, x24, 3288
.L294:
	ldrb	w0, [x23,3208]
	cmp	w0, w21
	bls	.L301
	ldrb	w1, [x4,w21,sxtw]
	add	x0, x5, 1240
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
.L296:
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
	bhi	.L296
	mov	x0, 0
.L297:
	add	w2, w0, 8
	mov	x1, 0
.L298:
	add	x3, x1, x0
	add	x1, x1, 4
	add	x3, x24, x3
	cmp	x1, 24
	ldrb	w6, [x3,4]
	ldrb	w3, [x19,x0]
	add	w3, w6, w3
	strb	w3, [x19,w2,sxtw]
	add	w2, w2, 8
	bne	.L298
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L297
	add	w21, w21, 1
	strb	wzr, [x19,16]
	strb	wzr, [x19,24]
	strb	wzr, [x19,32]
	uxtb	w21, w21
	strb	wzr, [x19,40]
	strb	wzr, [x19,48]
	strb	wzr, [x19,41]
	strb	wzr, [x19,49]
	b	.L294
.L387:
	sub	w0, w26, #3
	cmp	w0, 5
	bhi	.L301
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
	add	x0, x24, 3212
	mov	w21, 0
	str	x0, [x29,120]
	add	x0, x25, 1
	str	x0, [x29,112]
.L302:
	ldrb	w0, [x24,3208]
	adrp	x25, .LANCHOR0
	cmp	w0, w21
	bhi	.L348
.L301:
	add	x20, x20, :lo12:.LANCHOR2
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w27, [x20,1241]
	strb	w28, [x20,1242]
	ldp	x25, x26, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L348:
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
	bne	.L303
	add	x3, x20, :lo12:.LANCHOR2
	mov	x0, 160
	madd	x3, x1, x0, x3
	add	x3, x3, 1268
	b	.L304
.L303:
	cmp	w26, 8
	beq	.L305
	add	x3, x20, :lo12:.LANCHOR2
	add	x3, x3, x1, lsl 6
	add	x3, x3, 1260
.L304:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x19,2056]
	bne	.L306
	mov	w0, 255
	str	w0, [x19,2052]
	mov	w0, 64
	str	w0, [x19,2048]
	mov	w0, 204
	b	.L392
.L306:
	cmp	w7, 1
	bhi	.L308
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1244]
	str	w0, [x19,2052]
	mov	w0, 82
	b	.L393
.L308:
	cmp	w26, 7
	bne	.L307
	mov	w0, 174
	str	w0, [x19,2052]
	str	wzr, [x19,2048]
	mov	w0, 176
.L392:
	str	w0, [x19,2052]
	mov	w0, 77
.L393:
	str	w0, [x19,2048]
.L307:
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
	bne	.L309
	mov	w0, 31
	str	w0, [x19,2052]
	b	.L310
.L309:
	str	wzr, [x19,2052]
.L310:
	mov	w0, 2
	str	w0, [x19,2052]
	str	wzr, [x19,2052]
.L349:
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
	cbnz	w8, .L352
	cbnz	w7, .L352
	cmp	w26, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L311
.L352:
	mov	w1, 16
.L311:
	ldr	x0, [x25,#:lo12:.LANCHOR0]
	mov	x9, 0
.L312:
	ldr	w10, [x19,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L312
	cbz	w7, .L313
	mov	w1, 0
.L315:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L314
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L314
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L315
	b	.L316
.L314:
	cmp	w1, 6
	bls	.L317
.L316:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L318:
	b	.L318
.L313:
	cmp	w26, 7
	bne	.L319
	mov	w1, w7
.L321:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L320
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L320
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L321
	b	.L322
.L320:
	cmp	w1, 7
	bne	.L317
.L322:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L323:
	b	.L323
.L319:
	cmp	w26, 6
	bne	.L317
	mov	x1, 0
.L324:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L317
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L317
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L324
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L326:
	b	.L326
.L317:
	ldr	x10, [x25,#:lo12:.LANCHOR0]
	mov	x0, 0
.L327:
	ldr	w1, [x29,136]
	cmp	w1, w0
	ble	.L395
	ldr	w1, [x19,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L327
.L395:
	ldr	x12, [x25,#:lo12:.LANCHOR0]
	mov	w11, w23
	mov	w9, 8
.L330:
	mov	w0, 0
.L329:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L329
	ldr	w0, [x29,128]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L330
	mov	x9, 0
	mov	w17, 1
.L331:
	mov	w1, 0
	mov	w11, w1
.L335:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L333:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L333
	cmp	w14, 8
	bls	.L334
	orr	w1, w1, w13
	uxth	w1, w1
.L334:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L335
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L331
	mov	x6, 0
	ldr	x1, [x25,#:lo12:.LANCHOR0]
	mov	w9, w6
.L338:
	ldr	w11, [x1,x6]
	add	x6, x6, 4
	cmp	w11, wzr
	csinc	w9, w9, w9, ne
	cmp	x6, 32
	bne	.L338
	cmp	w9, 7
	ble	.L339
	adrp	x0, .LC3
	mov	w2, 1
	add	x0, x0, :lo12:.LC3
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L340:
	b	.L340
.L339:
	cmp	w26, 6
	mov	w6, 4
	beq	.L341
	cmp	w26, 7
	mov	w6, 10
	beq	.L341
	cmp	w7, wzr
	mov	w6, 5
	mov	w1, 8
	csel	w6, w1, w6, eq
.L341:
	mov	w9, 0
.L342:
	mov	x1, 0
.L343:
	add	w11, w0, w1
	ldrb	w12, [x10,x1]
	add	x1, x1, 1
	cmp	w27, w1, uxtb
	strb	w12, [x3,w11,sxtw]
	bhi	.L343
	add	w9, w9, 1
	ldr	x1, [x29,112]
	cmp	w9, w28
	add	w0, w0, w6
	add	x10, x10, x1
	blt	.L342
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w22
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L345
	mov	w0, 54
	str	w0, [x19,2056]
	adrp	x0, .LANCHOR2+1244
	mov	w1, -1
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1244]
	str	w0, [x19,2052]
	mov	w0, 22
	str	wzr, [x19,2048]
	str	w0, [x19,2056]
	mov	w0, w21
	bl	FlashReadCmd
	b	.L346
.L345:
	mov	w0, 190
	cbnz	w7, .L394
	mov	w0, 56
.L394:
	str	w0, [x19,2056]
.L346:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L302
.L305:
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
	add	x3, x3, 1268
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
	b	.L349
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L396
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L398
	bl	HynixGetReadRetryDefault
	b	.L396
.L398:
	cmp	w0, 49
	bne	.L399
	adrp	x0, .LANCHOR2
	mov	w2, 64
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1244
	strb	w1, [x0,-4]
	mov	w1, 4
	strb	w1, [x0,-3]
	mov	w1, 15
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 408
	b	.L414
.L399:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L405
	cmp	w1, 33
	bne	.L400
.L405:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0,1240]
	mov	w1, 4
	b	.L415
.L400:
	cmp	w1, 67
	beq	.L406
	cmp	w1, 34
	bne	.L402
.L406:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	strb	w1, [x0,1240]
	mov	w1, 5
.L415:
	strb	w1, [x0,1241]
	mov	w1, 7
	strb	w1, [x0,1242]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 1244
	add	x1, x1, 256
	mov	w2, 45
	b	.L414
.L402:
	cmp	w1, 68
	beq	.L407
	cmp	w1, 35
	bne	.L396
.L407:
	adrp	x0, .LANCHOR2
	mov	w2, 95
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 1244
	strb	w1, [x0,-4]
	mov	w1, 5
	strb	w1, [x0,-3]
	mov	w1, 17
	strb	w1, [x0,-2]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x1, x1, 304
.L414:
	bl	ftl_memcpy
.L396:
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
	add	x4, x3, 24
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
	bne	.L417
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
	b	.L418
.L417:
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
.L418:
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
	ldr	w0, [x0,2104]
	cbz	w0, .L420
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L421
.L420:
	bl	flash_exit_slc_mode
.L421:
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,2108]
	cbz	w0, .L422
	ldrb	w0, [x19,2100]
	tbz	x0, 0, .L422
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19,2108]
.L422:
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
	ble	.L431
	ldr	x0, [x1,1160]
	mov	w1, 8354
	b	.L438
.L431:
	cmp	w0, 220
	ble	.L433
	ldr	x0, [x1,1160]
	b	.L439
.L433:
	cmp	w0, 185
	ble	.L434
	ldr	x0, [x1,1160]
	mov	w1, 4226
	b	.L438
.L434:
	cmp	w0, 160
	ldr	x0, [x1,1160]
	ble	.L435
	mov	w1, 4194
	b	.L438
.L435:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L438
	cmp	w19, 99
	mov	w1, 4225
	bls	.L438
.L439:
	mov	w1, 8322
.L438:
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
	bls	.L441
	mov	w1, 8322
	cmp	w0, w1
	bne	.L442
.L441:
	adrp	x1, .LANCHOR2+1160
	ldr	x1, [x1,#:lo12:.LANCHOR2+1160]
	str	w0, [x1,4]
.L442:
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
	str	x0, [x2,1160]
	ldr	w1, [x0]
	ubfx	x3, x1, 13, 1
	str	w3, [x2,2112]
	ldr	w3, [x0,352]
	and	w1, w1, 245760
	orr	w1, w1, 256
	ubfx	x3, x3, 16, 4
	str	w3, [x2,2116]
	ldr	w3, [x0,352]
	str	w3, [x2,2120]
	cmp	w3, 2049
	bne	.L444
	mov	w3, 8
	str	w3, [x2,2116]
.L444:
	add	x19, x19, :lo12:.LANCHOR2
	str	w1, [x0]
	ldr	x0, [x19,1160]
	str	wzr, [x0,336]
	mov	w0, 40
	bl	NandcTimeCfg
	ldr	x0, [x19,1160]
	mov	w1, 8322
	str	w1, [x0,344]
	mov	w1, 6145
	movk	w1, 0x18, lsl 16
	str	w1, [x0,304]
	mov	w0, 36864
	bl	ftl_malloc
	str	wzr, [x19,2176]
	str	x0, [x19,2128]
	str	x0, [x19,2136]
	add	x0, x0, 32768
	str	wzr, [x19,2184]
	str	x0, [x19,2144]
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
	ldr	x5, [x4,1160]
	ldr	w5, [x5,4]
	str	w5, [x0]
	ldr	x0, [x4,1160]
	ldr	w0, [x0]
	str	w0, [x1]
	ldr	x0, [x4,1160]
	ldr	w0, [x0,304]
	str	w0, [x2]
	ldr	x1, [x4,1160]
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
	ldr	x2, [x1,1160]
	str	w0, [x1,2188]
	mov	w1, 4096
	str	w3, [x2,8]
	bne	.L447
.L450:
	and	w1, w1, -17
	b	.L448
.L447:
	cmp	w0, 24
	bne	.L449
	orr	w1, w1, 16
	b	.L448
.L449:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L450
.L448:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR2+1229
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR2+1229]
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
	ldr	x1, [x0,1160]
	ldr	w2, [x0,1168]
	str	w2, [x1]
	ldr	w2, [x0,1172]
	ldr	x1, [x0,1160]
	str	w2, [x1,4]
	ldr	w2, [x0,1176]
	ldr	x1, [x0,1160]
	str	w2, [x1,8]
	ldr	w2, [x0,1180]
	str	w2, [x1,12]
	ldr	w2, [x0,1184]
	str	w2, [x1,304]
	ldr	w2, [x0,1188]
	str	w2, [x1,308]
	ldr	w2, [x0,1192]
	str	w2, [x1,336]
	ldr	w0, [x0,1196]
	str	w0, [x1,344]
.L457:
	lsl	x0, x20, 3
	ldrb	w0, [x0,x21]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L456
	mov	w0, w20
	bl	FlashReset
.L456:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L457
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,2108]
	cbz	w0, .L458
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x19,2100]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x19,2100]
	bl	NandcSetMode
	ldr	w0, [x19,1184]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L458:
	adrp	x0, .LANCHOR0+3448
	ldr	x0, [x0,#:lo12:.LANCHOR0+3448]
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
	tbnz	x0, 9, .L465
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L464
.L465:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L464:
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
	cbz	w1, .L468
	mov	w1, w2
	bl	FlashEraseSLc2KBlocks
	b	.L469
.L468:
	mov	w27, 56
	adrp	x28, .LANCHOR2
	mov	x25, 24
.L499:
	cmp	w20, w23
	bcs	.L500
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
	ldrb	w2, [x1,3208]
	cmp	w0, w2
	bcc	.L470
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L471
.L470:
	add	x2, x28, :lo12:.LANCHOR2
	add	x1, x1, 3252
	ldrb	w2, [x2,2192]
	cmp	w2, wzr
	uxtw	x2, w0
	csel	w22, w22, wzr, ne
	madd	x1, x2, x25, x1
	ldr	x1, [x1,8]
	cbz	x1, .L473
	bl	FlashWaitCmdDone
.L473:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,124]
	add	x0, x0, 3252
	ldr	w2, [x29,120]
	madd	x0, x1, x25, x0
	str	x21, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w22, .L474
	add	w2, w20, 1
	umull	x2, w2, w27
	add	x2, x24, x2
	str	x2, [x0,16]
.L474:
	add	x0, x19, :lo12:.LANCHOR0
	add	x2, x0, 3212
	ldrb	w21, [x2,x1]
	madd	x1, x1, x25, x0
	mov	w0, w21
	strb	w21, [x1,3252]
	bl	NandcFlashCs
	cmp	w26, 1
	bne	.L475
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L475
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L476
.L475:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L476:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w1, [x29,124]
	add	x0, x0, 3220
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
.L471:
	add	w20, w20, 1
	b	.L499
.L500:
	adrp	x20, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR0
	add	x0, x20, :lo12:.LANCHOR2
	mov	x21, 0
	add	x22, x19, 3252
	mov	x25, 24
	ldr	x0, [x0,1160]
	bl	NandcIqrWaitFlashReady
.L478:
	ldrb	w0, [x19,3208]
	cmp	w0, w21
	bls	.L501
	mov	w0, w21
	bl	FlashWaitCmdDone
	cmp	w26, 1
	bne	.L479
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L479
	mul	x0, x21, x25
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L479:
	add	x21, x21, 1
	b	.L478
.L501:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w0, [x20,2196]
	cbnz	w0, .L481
.L483:
	mov	w0, 0
	b	.L469
.L481:
	adrp	x0, IDByte
	ldrb	w0, [x0,#:lo12:IDByte]
	cmp	w0, 69
	bne	.L483
	mov	x0, 0
	mov	x1, 56
.L482:
	cmp	w23, w0
	bls	.L483
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L482
.L469:
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
.L504:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L504
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
	ldr	w0, [x0,2116]
	bfi	w20, w3, 4, 1
	cmp	w0, 3
	bls	.L508
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w9, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w9, .L522
	cbz	x4, .L509
.L522:
	cbnz	w23, .L511
.L519:
	add	w2, w2, 1
	mov	x0, x4
	asr	w2, w2, 1
	bfi	w20, w2, 22, 6
	cbnz	x4, .L513
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2136]
	b	.L513
.L511:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w7, 128
	mov	w3, 0
	lsr	w10, w2, 1
	mov	w6, w3
	mov	w11, -1
	ldr	w0, [x1,2188]
	cmp	w0, 25
	mov	w0, 64
	csel	w7, w0, w7, cc
.L515:
	cmp	w6, w10
	bcs	.L519
	lsr	w0, w3, 2
	ubfiz	x0, x0, 2, 30
	cbz	w9, .L516
	ldrh	w8, [x5,2]
	ldr	x12, [x1,2144]
	ldrh	w13, [x5],4
	orr	w8, w13, w8, lsl 16
	str	w8, [x12,x0]
	b	.L517
.L516:
	ldr	x8, [x1,2144]
	str	w11, [x8,x0]
.L517:
	add	w6, w6, 1
	add	w3, w3, w7
	b	.L515
.L513:
	add	x19, x19, :lo12:.LANCHOR2
	ubfx	x24, x20, 22, 5
	mov	w2, w23
	str	x4, [x29,72]
	ldr	x1, [x19,2144]
	str	x1, [x19,2160]
	lsl	w1, w24, 10
	str	x0, [x19,2152]
	bl	rknand_dma_map_single
	str	w0, [x19,2168]
	ldr	x0, [x19,2160]
	lsl	w1, w24, 7
	mov	w2, w23
	bl	rknand_dma_map_single
	str	w0, [x19,2172]
	mov	w0, 1
	str	w0, [x19,2176]
	ldr	w0, [x19,2168]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	x4, [x29,72]
	ldr	w0, [x19,2172]
	str	w0, [x21,24]
	and	x4, x4, 3
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x4, .L520
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L520:
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
.L509:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L508:
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
	ldr	w0, [x0,2116]
	cmp	w0, 3
	bls	.L557
	ldr	w0, [x19,16]
	tbz	x0, 2, .L557
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L528
	adrp	x22, .LC4
	adrp	x23, .LC5
	mov	w21, 0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
.L529:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L537
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2116]
	cmp	w0, 5
	bhi	.L530
.L533:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L532
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
	b	.L532
.L530:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L533
	ldr	w0, [x29,72]
	tbz	x0, 17, .L533
.L537:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 2136
	ldr	w0, [x19,40]
	cbz	w0, .L538
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
	b	.L538
.L532:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L529
.L528:
	adrp	x22, .LC6
	adrp	x23, .LC5
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
.L539:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L565
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L540
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
.L540:
	mov	x0, 5
	mov	x1, 10
	bl	usleep_range
	b	.L539
.L565:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2184]
	cbz	w0, .L542
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L542:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 2136
	ldr	w0, [x21,40]
	cbz	w0, .L543
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
.L543:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,2184]
	cbz	w0, .L538
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L538:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20,2176]
	b	.L526
.L557:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L557
.L526:
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
	bne	.L567
	cbz	x3, .L568
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L568:
	cbz	x20, .L566
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
	b	.L566
.L567:
	cbz	x3, .L571
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L571:
	cbz	x20, .L566
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
.L566:
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
	cbnz	x0, .L583
	cbnz	x4, .L584
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x21, x29, 112
	bl	ftl_memset
.L584:
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
	cbnz	w20, .L585
	adrp	x0, .LANCHOR2
	ubfx	x3, x23, 1, 7
	add	x2, x0, :lo12:.LANCHOR2
	mov	w4, 128
	add	x3, x21, x3, lsl 2
	ldr	w1, [x2,2188]
	cmp	w1, 25
	mov	w1, 64
	csel	w4, w1, w4, cc
	mov	w1, w20
.L587:
	cmp	x21, x3
	add	w5, w1, w4
	beq	.L629
	ldr	x6, [x2,2144]
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
	b	.L587
.L629:
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w23, w23, 2
	ldr	w4, [x0,2188]
	ldr	w5, [x0,2116]
	mov	w0, 0
	mov	w22, w0
.L589:
	cmp	w0, w23
	bcs	.L585
	cbz	w4, .L585
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L613
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L613
	cmp	w5, 5
	bls	.L591
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
	bls	.L592
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L593
.L592:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L628
.L591:
	cmp	w5, 3
	bls	.L593
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
	bls	.L594
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L628
.L594:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L628:
	orr	w3, w1, w3, lsl 5
.L593:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L590
.L613:
	mov	w22, -1
.L590:
	add	w0, w0, 1
	b	.L589
.L585:
	str	wzr, [x19,16]
	b	.L596
.L583:
	cmp	w20, 1
	bne	.L627
	mov	w22, 0
	mov	w27, 2
.L597:
	cmp	w22, w23
	bcs	.L630
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L599
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L599:
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
	b	.L597
.L630:
	mov	w22, 0
	b	.L596
.L627:
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
.L602:
	cmp	w26, w23
	bcs	.L596
	mov	w0, w24
	mov	w1, w20
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L603
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L603:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L618
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L604
.L618:
	mov	w22, -1
.L604:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L605
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L605:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L602
.L596:
	cbnz	w20, .L608
	adrp	x0, .LANCHOR2+2116
	ldr	w0, [x0,#:lo12:.LANCHOR2+2116]
	cmp	w0, 5
	bls	.L608
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L608
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L608:
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
	cbnz	w19, .L632
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,89]
	ldr	w0, [x0,92]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L632:
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
	ldr	x1, [x20,1160]
	mov	w22, 6
	mov	x25, x2
	mov	x23, x3
	mov	w27, w4
	mov	w21, 1024
	ldr	w1, [x1,304]
	str	w1, [x29,124]
	ldr	w1, [x20,2116]
	cmp	w1, 8
	csel	w22, w22, w0, cc
	cbz	w4, .L635
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
	ldrb	w0, [x20,2100]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,2100]
	bl	NandcSetMode
	cmn	w21, #1
	bne	.L636
.L645:
	mov	w21, -1
	b	.L637
.L636:
	adrp	x0, .LC7
	mov	w1, w26
	add	x0, x0, :lo12:.LC7
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L638
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, x24, sxtw 4
	ldr	x0, [x0,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L638:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2200]
	add	w1, w1, 1
	str	w1, [x0,2200]
	cmp	w1, 2047
	bls	.L637
	mov	x23, 0
	str	wzr, [x0,2200]
	mov	x25, x23
.L635:
	mov	w19, 0
	mov	w28, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L643:
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
	bhi	.L639
	cmp	w0, 2
	bhi	.L649
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L649
	sub	w19, w22, w20
	mov	w21, w0
	mov	w28, 0
	b	.L641
.L639:
	cmp	w6, w20
	bcs	.L650
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L651
	mov	w6, w20
	b	.L650
.L649:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w28, 0
	mov	x25, x23
	b	.L640
.L650:
	mov	w20, 0
.L640:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L643
.L641:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L642
.L651:
	mov	w19, w5
.L642:
	cbz	w19, .L644
	adrp	x0, .LC8
	mov	w1, w19
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L644:
	cbz	w28, .L637
	adrp	x0, .LC9
	mov	w1, w24
	add	x0, x0, :lo12:.LC9
	mov	w2, w26
	bl	printk
	cbz	w27, .L645
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L637:
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
	bne	.L661
	adrp	x23, .LANCHOR0
	add	x23, x23, :lo12:.LANCHOR0
	ldrb	w26, [x23,96]
	cbnz	w26, .L662
.L664:
	add	x23, x21, :lo12:.LANCHOR2
	ldrb	w0, [x23,2108]
	cbz	w0, .L661
	b	.L682
.L662:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	strb	wzr, [x23,96]
	bl	FlashReadRawPage
	strb	w26, [x23,96]
	cmn	w0, #1
	beq	.L664
	mov	w19, w0
	b	.L661
.L682:
	ldr	x0, [x23,1160]
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	mov	w4, 1
	ldr	w26, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w19, w0
	beq	.L665
	ldrb	w0, [x23,1229]
	cmp	w19, w0, lsr 1
	bls	.L661
.L665:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
.L661:
	add	x21, x21, :lo12:.LANCHOR2
	cmn	w19, #1
	ldr	x4, [x21,2208]
	bne	.L666
	cbz	x4, .L666
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
	bne	.L666
	ldrb	w0, [x21,1228]
	cbz	w0, .L666
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,2208]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L666:
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
	ldrb	w0, [x22,2100]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2100]
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
	beq	.L687
	cmn	w22, #1
	bne	.L684
.L687:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20,2100]
	tbz	x0, 0, .L684
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,2108]
	b	.L686
.L684:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,2108]
.L686:
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
	str	x0, [x19,2216]
	add	x22, x22, 472
	mov	w0, 0
	add	x25, x19, 1240
	str	wzr, [x19,2224]
	bl	flash_enter_slc_mode
.L695:
	add	w28, w21, 1
	mov	x27, 0
.L697:
	add	x0, x29, 104
	ldrb	w0, [x27,x0]
	bl	FlashBchSel
	ldr	x2, [x20,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w21
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L696
	ldr	x2, [x20,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w28
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L696
	add	x27, x27, 1
	cmp	x27, 4
	beq	.L698
	b	.L697
.L699:
	mov	w1, 2036
	add	x0, x27, 12
	mov	w26, -1
	bl	js_hash
	ldr	w1, [x27,8]
	cmp	w1, w0
	beq	.L710
.L698:
	subs	w23, w23, #1
	add	w21, w21, w24
	bne	.L695
	mov	w0, w23
.L709:
	bl	flash_exit_slc_mode
	mov	w0, w26
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 112
	ret
.L696:
	ldr	x27, [x19,2216]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x27]
	cmp	w1, w0
	bne	.L698
	cbnz	w26, .L699
	add	x20, x20, :lo12:.LANCHOR0
	ldr	w0, [x20,92]
	udiv	w21, w21, w0
	mov	w0, w26
	add	w21, w21, 1
	str	w21, [x20,8]
	b	.L709
.L710:
	add	x1, x27, 160
	mov	w2, 32
	mov	x0, x22
	add	x26, x20, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldr	x1, [x19,2216]
	mov	w2, 32
	add	x0, x26, 104
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x19,2216]
	mov	w2, 852
	mov	x0, x25
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x22,10]
	bl	FlashBlockAlignInit
	str	w21, [x19,2224]
	ldr	x0, [x19,2216]
	ldr	w1, [x0,1076]
	strb	w1, [x19,2108]
	ldr	w1, [x26,92]
	udiv	w1, w21, w1
	add	w1, w1, 1
	cmp	w1, 1
	bhi	.L708
	mov	w1, 2
.L708:
	ldrh	w0, [x0,14]
	str	w1, [x26,8]
	mov	w26, 0
	strb	w0, [x19,2228]
	b	.L698
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
	bls	.L712
	adrp	x0, .LANCHOR2+2108
	ldrb	w0, [x0,#:lo12:.LANCHOR2+2108]
	cbz	w0, .L713
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L713:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L712:
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
.L714:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0,#:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L739
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, w21
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L715
	bl	SandiskSetRRPara
	b	.L716
.L715:
	bl	ToshibaSetRRPara
.L716:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L717
	ldr	x0, [x29,120]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L717
	ldr	x0, [x29,112]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L717:
	ldr	x0, [x29,104]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L718
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
	b	.L719
.L718:
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L719:
	cmn	w28, #1
	beq	.L720
	adrp	x0, .LANCHOR2+1229
	cmn	w25, #1
	csel	w25, w25, w28, ne
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1229]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L722
	mov	x26, 0
	mov	x27, x26
.L720:
	add	w21, w21, 1
	b	.L714
.L739:
	mov	w28, w25
.L722:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, 0
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L724
	bl	SandiskSetRRPara
	b	.L725
.L724:
	bl	ToshibaSetRRPara
.L725:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	adrp	x0, .LANCHOR2+1229
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1229]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L726
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L726:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L727
	mov	w0, 4
	bl	NandcSetDdrMode
.L727:
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
.L741:
	ldrb	w0, [x26]
	add	w0, w0, 1
	cmp	w25, w0
	bcs	.L744
	mov	x0, x20
	mov	w1, w25
	bl	SamsungSetRRPara
	mov	w0, w21
	mov	w1, w24
	mov	x2, x23
	mov	x3, x22
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L742
	add	x1, x27, :lo12:.LANCHOR2
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,1229]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L747
	mov	x22, 0
	mov	x23, x22
.L742:
	add	w25, w25, 1
	b	.L741
.L747:
	mov	w19, w0
.L744:
	mov	x0, x20
	mov	w1, 0
	bl	SamsungSetRRPara
	adrp	x0, .LANCHOR2+1229
	ldrb	w0, [x0,#:lo12:.LANCHOR2+1229]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L746
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L746:
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
	ldrb	w19, [x1,1229]
	mov	x22, x3
	ldrb	w1, [x1,1228]
	cbnz	w1, .L754
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L755
.L754:
	mov	w0, 3
	sdiv	w19, w19, w0
.L755:
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
.L766:
	mov	w0, w21
	mov	w23, 0
	mov	w26, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x20,8]
	mov	w9, 239
	ldr	x6, [x20]
	mov	w10, 137
	lsl	x8, x4, 8
.L756:
	ldr	x0, [x29,144]
	ldrb	w0, [x0]
	cmp	w23, w0
	bcs	.L759
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
	beq	.L757
	cmn	w26, #1
	csel	w26, w26, w0, ne
	cmp	w0, w19
	bcc	.L768
	mov	x22, 0
	mov	x25, x22
.L757:
	mov	w23, w7
	b	.L756
.L768:
	mov	x22, 0
	mov	w26, w0
	mov	x25, x22
.L759:
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
	bcc	.L761
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
.L761:
	cmn	w26, #1
	cset	w6, eq
	cbnz	w6, .L770
	cmp	w26, 256
	cset	w1, eq
	cbz	w1, .L762
.L770:
	mov	x0, x27
	mov	w1, w23
	mov	w2, w28
	mov	w3, w23
	mov	w4, w26
	str	x6, [x29,136]
	bl	printk
	cbnz	w24, .L764
	ldr	x6, [x29,136]
	cbz	w6, .L775
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L775
	mov	w0, w21
	mov	w1, 3
	mov	w24, 1
	bl	micron_auto_read_calibration_config
	b	.L766
.L764:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w26, #1
	mov	w0, 256
	csel	w26, w26, w0, eq
	b	.L775
.L762:
	cbz	w24, .L775
	mov	w0, w21
	mov	w26, 256
	bl	micron_auto_read_calibration_config
.L775:
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
	add	x0, x0, 1240
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
	ldr	x0, [x0,3448]
	ldrb	w0, [x0,19]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L784
	ldrb	w19, [x1,20]
.L784:
	mov	w0, w24
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x5, x21, :lo12:.LANCHOR2
	mov	w4, 0
	add	x6, x5, 1244
.L785:
	cmp	w4, w26
	bcs	.L789
	add	w19, w19, 1
	ldrb	w1, [x5,1241]
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
	beq	.L787
	ldrb	w1, [x5,1229]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L794
	mov	x25, 0
	mov	x27, x25
.L787:
	add	w4, w4, 1
	b	.L785
.L794:
	mov	w20, w0
.L789:
	add	x22, x22, :lo12:.LANCHOR0
	ldr	x0, [x22,3448]
	ldrb	w0, [x0,19]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x23, x0, x23
	bhi	.L791
	strb	w19, [x23,1260]
	b	.L792
.L791:
	strb	w19, [x23,1252]
.L792:
	add	x21, x21, :lo12:.LANCHOR2
	ldrb	w0, [x21,1229]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L793
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L793:
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
	cbnz	w19, .L801
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,89]
	ldr	w2, [x0,92]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L801
	ldrb	w0, [x0,88]
	cmp	w0, wzr
	mov	w0, 4
	csel	w21, w21, w0, eq
.L801:
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
	str	x0, [x19,2216]
	ldrb	w0, [x19,2229]
	bl	FlashBchSel
	ldr	x0, [x22,#:lo12:.LANCHOR0]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,2216]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,2216]
	ldrb	w1, [x20,3208]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,89]
	strh	w1, [x0,-2]
	ldrb	w1, [x19,2108]
	str	w1, [x0,1060]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	bl	ftl_memcpy
	ldr	x0, [x19,2216]
	add	x1, x20, 3212
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,2216]
	add	x1, x20, 3220
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,2216]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,2216]
	add	x1, x20, 104
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,2216]
	add	x1, x19, 1240
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,2216]
	mov	w1, 2036
	add	x0, x20, 12
	bl	js_hash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,2232]
	str	x0, [x19,2216]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L806:
	add	x20, x22, :lo12:.LANCHOR0
	mov	w2, 0
	mov	w0, 0
	add	x24, x23, :lo12:.LANCHOR2
	add	w26, w19, 1
	ldr	w1, [x20,92]
	mul	w1, w19, w1
	bl	FlashEraseBlock
	ldr	w1, [x20,92]
	mov	x3, 0
	ldr	x2, [x22,#:lo12:.LANCHOR0]
	mov	w0, 0
	mul	w1, w19, w1
	bl	FlashProgPage
	ldr	w1, [x20,92]
	mov	x3, 0
	ldr	x2, [x22,#:lo12:.LANCHOR0]
	mov	w0, 0
	mul	w1, w19, w1
	add	w1, w1, 1
	bl	FlashProgPage
	ldr	w1, [x20,92]
	mov	w0, 0
	ldr	x2, [x24,2232]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L804
	ldr	x25, [x24,2216]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L804
	mov	w1, 2036
	add	x0, x25, 12
	bl	js_hash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L804
	ldr	w0, [x20,92]
	cmp	w21, 1
	str	w26, [x20,8]
	mul	w19, w19, w0
	str	w19, [x24,2224]
	beq	.L807
	mov	w21, 1
.L804:
	cmp	w26, 4
	mov	w19, w26
	bne	.L806
	b	.L805
.L807:
	mov	w21, 2
.L805:
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
	ldrb	w28, [x0,1229]
	ldr	w0, [x0,2104]
	cbz	w0, .L813
	mov	w0, 0
	bl	flash_enter_slc_mode
.L813:
	adrp	x26, .LANCHOR0
	adrp	x24, .LC12
	mov	x0, x25
	mov	w1, 0
	mov	w2, 2048
	mov	w23, -1
	mov	w20, 2
	mov	x27, x26
	add	x24, x24, :lo12:.LC12
	bl	ftl_memset
.L814:
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,89]
	cmp	w20, w0
	bcs	.L818
	mov	x22, 0
.L816:
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
	bne	.L815
	add	x22, x22, 1
	cmp	x22, 4
	bne	.L816
	b	.L817
.L815:
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L817
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
	bls	.L821
	mov	w23, 0
	str	w20, [x19,8]
	bl	FlashSavePhyInfo
.L817:
	add	w20, w20, 1
	b	.L814
.L821:
	mov	w23, 0
.L818:
	mov	w0, w28
	add	x21, x21, :lo12:.LANCHOR2
	bl	FlashBchSel
	ldr	w0, [x21,2104]
	cbz	w0, .L825
	mov	w0, 0
	bl	flash_exit_slc_mode
.L825:
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
	adrp	x24, .LC13
	bl	ftl_malloc
	str	x0, [x21,2232]
	mov	w0, 4096
	add	x22, x22, 24
	mov	w27, 1
	mov	w28, 4
	bl	ftl_malloc
	str	x0, [x21,2240]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x21,2248]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x21,2256]
	mov	w0, 128
	str	w0, [x22,68]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x21,2229]
	mov	x0, x23
	strb	w1, [x22,65]
	mov	w23, 0
	strb	w1, [x21,2228]
	str	wzr, [x22,-16]
	strb	wzr, [x21,2108]
	str	wzr, [x21,2200]
	strb	wzr, [x22,64]
	strb	wzr, [x21,2264]
	bl	NandcInit
	adrp	x21, IDByte
	add	x0, x24, :lo12:.LC13
	add	x26, x21, :lo12:IDByte
	str	x0, [x29,104]
.L837:
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
	bne	.L832
	mov	w0, 239
	str	w0, [x24,2056]
	str	w27, [x24,2052]
	mov	x0, 1000
	bl	__const_udelay
	str	w28, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
.L832:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x26]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L833
	ldr	x0, [x29,104]
	add	w1, w23, 1
	ldrb	w3, [x26,1]
	ldrb	w4, [x26,2]
	ldrb	w5, [x26,3]
	ldrb	w6, [x26,4]
	ldrb	w7, [x26,5]
	bl	printk
.L833:
	cbnz	w23, .L834
	ldrb	w0, [x21,#:lo12:IDByte]
	add	x1, x21, :lo12:IDByte
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L888
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L888
	bl	FlashCs123Init
.L834:
	ldrb	w0, [x26]
	cmp	w0, 181
	bne	.L836
	mov	w0, 44
	strb	w0, [x26]
.L836:
	add	w23, w23, 1
	add	x22, x22, 16
	cmp	w23, 4
	add	x26, x26, 8
	bne	.L837
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 173
	beq	.L838
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2112]
	bl	NandcSetDdrMode
.L838:
	add	x22, x19, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 852
	add	x0, x22, 1240
	bl	ftl_memset
	add	x0, x20, :lo12:.LANCHOR0
	adrp	x1, .LANCHOR1
	add	x2, x1, :lo12:.LANCHOR1
	ldr	w4, [x22,2120]
	add	x2, x2, 472
	strb	wzr, [x0,96]
	str	x2, [x0,3448]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w4, w0
	bne	.L839
	mov	w0, 1
	str	w0, [x22,2104]
.L839:
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
	cbnz	w2, .L840
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L840
	cmp	w0, 220
	bne	.L841
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L841
.L840:
	add	x2, x20, :lo12:.LANCHOR0
	add	x3, x19, :lo12:.LANCHOR2
	mov	w7, 16
	mov	w5, 1
	add	x8, x21, :lo12:IDByte
	strb	w7, [x2,89]
	strb	w5, [x2,88]
	add	x2, x1, :lo12:.LANCHOR1
	strb	w7, [x3,2229]
	ldrb	w7, [x21,#:lo12:IDByte]
	strb	w7, [x2,3321]
	strb	w0, [x2,3322]
	cmp	w7, 152
	bne	.L843
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L844
	mov	w2, 24
	strb	w2, [x3,2229]
.L843:
	cmp	w4, 2049
	beq	.L891
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L846
.L891:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w3, 16
	strb	w3, [x2,2229]
.L846:
	cbz	w6, .L848
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3334]
	mov	w2, -38
	b	.L933
.L848:
	cmp	w0, 220
	bne	.L850
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3334]
	mov	w2, -36
.L933:
	strb	w2, [x0,3322]
	b	.L849
.L850:
	cmp	w0, 211
	bne	.L849
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3334]
	mov	w2, 2
	strb	w2, [x0,3333]
.L849:
	add	x22, x1, :lo12:.LANCHOR1
	add	x0, x20, :lo12:.LANCHOR0
	add	x1, x22, 3192
	mov	w2, 32
	add	x0, x0, 104
	bl	ftl_memcpy
	add	x0, x22, 472
	add	x1, x22, 3320
	mov	w2, 32
	bl	ftl_memcpy
.L841:
	add	x22, x20, :lo12:.LANCHOR0
	ldrb	w0, [x22,88]
	cbnz	w0, .L851
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L853
	ldr	x0, [x22,3448]
	add	x22, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x22,2100]
	tbnz	x1, 0, .L853
	mov	w1, 1
	strb	w1, [x22,2108]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2100]
	bl	NandcSetMode
.L853:
	add	x0, x20, :lo12:.LANCHOR0
	add	x22, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,3448]
	ldrb	w0, [x0,26]
	strb	w0, [x22,1228]
	bl	FlashLoadPhyInfo
	cbz	w0, .L851
	ldr	w0, [x22,2112]
	cbz	w0, .L856
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L929
.L856:
	ldrb	w0, [x22,2100]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,2100]
.L929:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L851
	mov	w0, 1
	add	x22, x20, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x22,3448]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L905
	bl	FlashDieInfoInit
	ldr	x0, [x22,3448]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x22,3448]
	ldrh	w1, [x22,3456]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L858
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L859
.L858:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L859:
	add	x22, x19, :lo12:.LANCHOR2
	ldrb	w0, [x22,2100]
	tst	w0, 6
	beq	.L860
	bl	FlashSavePhyInfo
	ldr	w1, [x22,2224]
	mov	w0, 0
	bl	FlashDdrParaScan
.L860:
	bl	FlashSavePhyInfo
.L851:
	add	x23, x20, :lo12:.LANCHOR0
	add	x22, x19, :lo12:.LANCHOR2
	ldr	x2, [x23,3448]
	str	xzr, [x22,2208]
	ldrb	w0, [x2,26]
	strb	w0, [x22,1228]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x23,96]
	ubfx	x1, x0, 3, 1
	strb	w1, [x22,2265]
	ubfx	x1, x0, 4, 1
	strb	w1, [x22,2192]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x22,2100]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x23,3448]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L862
	adrp	x1, g_retryMode
	ldrb	w0, [x0,19]
	ldrb	w2, [x22,1241]
	ldrb	w3, [x22,1242]
	strb	w0, [x1,#:lo12:g_retryMode]
	adrp	x1, g_maxRegNum
	strb	w2, [x1,#:lo12:g_maxRegNum]
	adrp	x2, g_maxRetryCount
	strb	w3, [x2,#:lo12:g_maxRetryCount]
	sub	w3, w0, #1
	uxtb	w3, w3
	cmp	w3, 7
	mov	x3, x2
	bhi	.L863
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x22,2208]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L892
	cbz	w2, .L864
.L892:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w3, 1
	str	w3, [x1,2184]
.L864:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR2
	beq	.L890
	cbnz	w2, .L890
	add	x1, x1, 1260
	b	.L867
.L890:
	add	x1, x1, 1268
.L867:
	mov	x2, 0
	mov	w3, w2
.L869:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L869
	cmp	w3, 27
	bls	.L862
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L862
.L863:
	sub	w4, w0, #17
	uxtb	w4, w4
	cmp	w4, 2
	bhi	.L871
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x22,2208]
	beq	.L872
	mov	w0, 7
	b	.L931
.L872:
	mov	w0, 15
.L931:
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	b	.L862
.L871:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L893
	cmp	w0, 33
	bne	.L873
.L893:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x2, ToshibaReadRetrial
	add	x2, x2, :lo12:ToshibaReadRetrial
	str	x2, [x0,2208]
	mov	w0, 4
	strb	w0, [x1,#:lo12:g_maxRegNum]
	mov	w0, 7
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	b	.L862
.L873:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w5, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L894
	cbz	w5, .L875
.L894:
	add	x2, x19, :lo12:.LANCHOR2
	adrp	x4, ToshibaReadRetrial
	add	x4, x4, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x4, [x2,2208]
	beq	.L877
	cmp	w0, 68
	beq	.L877
	mov	w0, 7
	b	.L930
.L877:
	mov	w0, 17
.L930:
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	mov	w0, 4
	cbnz	w5, .L932
	mov	w0, 5
.L932:
	strb	w0, [x1,#:lo12:g_maxRegNum]
	b	.L862
.L875:
	cmp	w0, 49
	bne	.L862
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x22,2208]
.L862:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	ldr	w2, [x0,2120]
	cmp	w2, w1
	bne	.L880
	ldrb	w0, [x0,1228]
	cbz	w0, .L880
	add	x0, x20, :lo12:.LANCHOR0
	ldr	x0, [x0,3448]
	strb	wzr, [x0,18]
.L880:
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 44
	bne	.L881
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,2108]
	cbz	w1, .L881
	mov	w1, 12336
	movk	w1, 0x5638, lsl 16
	cmp	w2, w1
	bne	.L882
	ldrb	w0, [x0,1228]
	cbnz	w0, .L881
.L882:
	add	x0, x19, :lo12:.LANCHOR2
	strb	wzr, [x0,2108]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L881:
	add	x1, x19, :lo12:.LANCHOR2
	ldrb	w0, [x1,2100]
	tst	w0, 6
	beq	.L883
	ldrb	w1, [x1,2108]
	cbnz	w1, .L884
	tbnz	x0, 0, .L883
.L884:
	mov	w0, 0
	bl	flash_enter_slc_mode
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 0
	ldr	w1, [x1,2224]
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L883:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w22, 16
	add	x19, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:IDByte
	ldr	x0, [x20,3448]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	add	x0, x20, 3464
	bl	FlashReadIdbDataRaw
	strb	w22, [x20,89]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x20,3448]
	ldrb	w2, [x1,12]
	strh	w2, [x19,1208]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,1204]
	ldr	w2, [x21]
	str	w2, [x19,1200]
	ldrb	w2, [x20,3208]
	strh	w2, [x19,1210]
	ldrb	w2, [x1,13]
	strh	w2, [x19,1212]
	strh	w4, [x19,1214]
	ldrh	w2, [x1,10]
	strh	w2, [x19,1216]
	ldrb	w3, [x1,12]
	ldrh	w2, [x1,10]
	sdiv	w2, w2, w3
	strh	w2, [x19,1218]
	ldrb	w3, [x1,9]
	strh	w3, [x19,1220]
	ldrh	w5, [x1,10]
	ldrb	w2, [x1,9]
	mul	w2, w2, w5
	mov	w5, 512
	strh	w5, [x19,1224]
	ldrb	w5, [x20,89]
	strh	w5, [x19,1226]
	uxth	w2, w2
	ldrb	w5, [x20,88]
	strh	w2, [x19,1222]
	cmp	w5, 1
	bne	.L886
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,1222]
	strb	w22, [x20,89]
	mov	w2, 8
	strh	w4, [x19,1214]
	strh	w3, [x19,1220]
	strh	w2, [x19,1226]
.L886:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L905
.L888:
	mov	w0, -2
	b	.L905
.L844:
	strb	w5, [x3,2264]
	b	.L843
.L905:
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
	str	x25, [sp,64]
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	uxth	w19, w2
	adrp	x2, .LANCHOR2
	ldr	x0, [x0,3448]
	mov	x22, x2
	ldrb	w3, [x0,19]
	add	x0, x2, :lo12:.LANCHOR2
	ldrb	w4, [x0,1228]
	cbz	w4, .L935
	ldr	w0, [x0,2104]
	cbnz	w0, .L934
.L935:
	sub	w0, w3, #5
	uxtb	w0, w0
	cmp	w0, 2
	bls	.L936
	cmp	w3, 68
	beq	.L936
	sub	w3, w3, #19
	and	w3, w3, -17
	uxtb	w3, w3
	cbnz	w3, .L934
.L936:
	mov	w23, w1
	add	x20, x20, :lo12:.LANCHOR0
	mov	w25, 65535
.L938:
	ldr	x0, [x20,3448]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L934
	add	x0, x20, 1160
	ldrh	w0, [x0,w19,sxtw 1]
	cmp	w0, w25
	bne	.L934
	add	x24, x22, :lo12:.LANCHOR2
	mov	w1, 255
	mov	w2, 32768
	ldr	x0, [x24,2232]
	bl	ftl_memset
	ldr	x2, [x24,2232]
	add	w1, w19, w23
	mov	w0, w21
	add	w19, w19, 1
	mov	x3, x2
	uxth	w19, w19
	bl	FlashProgPage
	b	.L938
.L934:
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
	add	x0, x0, 3212
	stp	x25, x26, [sp,64]
	str	x0, [x29,104]
	adrp	x0, .LC15
	ldrb	w25, [x1,#:lo12:.LANCHOR1+481]
	add	x0, x0, :lo12:.LC15
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w22, 0
	str	x0, [x29,96]
.L947:
	cmp	w22, w24
	beq	.L1011
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,3208]
	cmp	w0, w1
	bcc	.L948
	mov	w0, -1
	str	w0, [x19]
	b	.L949
.L948:
	ldr	x1, [x29,104]
	mov	w23, 0
	adrp	x27, .LANCHOR2
	mov	w28, 256
	ldrb	w20, [x1,w0,uxtw]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L950:
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
	ldrb	w0, [x0,2264]
	cbz	w0, .L951
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L951:
	cmn	w26, #1
	bne	.L952
	cmp	w23, 9
	bhi	.L952
	add	w23, w23, 1
	b	.L950
.L952:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L954:
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
	ldrb	w0, [x0,2264]
	cbz	w0, .L957
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L957:
	cmn	w28, #1
	bne	.L958
	cmp	w23, 9
	bhi	.L958
	add	w23, w23, 1
	b	.L954
.L958:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,1229]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L960
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L960:
	cmp	w28, 256
	beq	.L971
	cmn	w28, #1
	bne	.L961
.L971:
	str	w28, [x19]
	b	.L963
.L961:
	str	wzr, [x19]
.L963:
	ldr	x0, [x19,16]
	cbz	x0, .L964
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L964
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L964
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L964
	str	w1, [x19]
.L964:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L949
	add	x21, x21, :lo12:.LANCHOR2
	ldr	w1, [x19,4]
	ldr	x0, [x29,96]
	ldrb	w2, [x21,1229]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L966
	adrp	x0, .LC16
	mov	w2, 4
	add	x0, x0, :lo12:.LC16
	mov	w3, 8
	bl	rknand_print_hex
.L966:
	ldr	x1, [x19,16]
	cbz	x1, .L949
	adrp	x0, .LC17
	mov	w2, 4
	add	x0, x0, :lo12:.LC17
	mov	w3, w2
	bl	rknand_print_hex
.L949:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L947
.L1011:
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
	cbz	w24, .L1052
	bl	FlashReadSlc2KPages
	b	.L1101
.L1052:
	add	x0, x23, 3212
	str	x0, [x29,104]
	adrp	x0, .LANCHOR2
	mov	w25, w24
	add	x21, x0, :lo12:.LANCHOR2
	add	x0, x21, 1244
	str	x0, [x29,120]
.L1013:
	ldr	w0, [x29,132]
	cmp	w25, w0
	bcs	.L1103
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
	ldrb	w1, [x23,3208]
	mov	w20, w0
	ldr	w0, [x29,152]
	cmp	w0, w1
	bcc	.L1015
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1016
.L1015:
	ldr	x1, [x29,104]
	ldrb	w19, [x1,w0,uxtw]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,2265]
	cmp	w0, wzr
	mov	w0, w19
	csel	w20, w20, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x0, [x23,3448]
	ldrb	w1, [x0,19]
	sub	w0, w1, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1018
	sub	w1, w1, #7
	add	x0, x21, 1240
	sxtw	x2, w19
	add	x0, x0, x2
	uxtb	w1, w1
	cmp	w1, 1
	ldrb	w3, [x0,12]
	bhi	.L1019
	ldrb	w3, [x0,20]
.L1019:
	add	x2, x21, x2
	ldrb	w0, [x2,2096]
	cmp	w0, w3
	beq	.L1018
	ldrb	w1, [x21,1241]
	mov	w0, w19
	ldr	x2, [x29,120]
	bl	HynixSetRRPara
.L1018:
	mov	w0, w19
	bl	NandcFlashCs
	ldr	w0, [x29,128]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,140]
	cbz	w0, .L1020
	ldrb	w0, [x21,1228]
	cbz	w0, .L1020
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1021
.L1020:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1021:
	cmp	w19, 255
	ldr	w1, [x29,156]
	bne	.L1059
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1053
.L1059:
	cbz	w20, .L1025
	ldr	w2, [x23,92]
	mov	w0, w19
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1026
.L1025:
	mov	w0, w19
	bl	FlashReadCmd
	b	.L1026
.L1053:
	mov	w20, w0
.L1023:
	ldrb	w2, [x29,136]
	mov	w0, w19
	ldr	x3, [x22,8]
	mov	w1, 0
	ldr	x4, [x22,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1027
	ldrb	w0, [x23,96]
	cbz	w0, .L1027
	strb	wzr, [x23,96]
	mov	w20, 0
	b	.L1021
.L1027:
	cbz	w20, .L1028
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
.L1028:
	mov	w0, w19
	bl	NandcFlashDeCs
	ldrb	w0, [x29,116]
	cmn	w28, #1
	strb	w0, [x23,96]
	bne	.L1035
	ldrb	w0, [x21,2108]
	cbnz	w0, .L1030
.L1034:
	ldr	x20, [x21,2208]
	cbnz	x20, .L1031
	b	.L1104
.L1030:
	ldr	x0, [x21,1160]
	mov	w4, 1
	ldr	w1, [x29,156]
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	ldr	w20, [x0,304]
	mov	w0, w19
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L1033
	ldrb	w0, [x21,1229]
	cmp	w28, w0, lsr 1
	bls	.L1055
.L1033:
	lsr	w0, w20, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1034
	b	.L1055
.L1031:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	blr	x20
	cmn	w0, #1
	mov	w28, w0
	mov	w20, 0
	bne	.L1036
	ldr	x0, [x23,3448]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1037
	ldrb	w1, [x21,1241]
	mov	w0, w19
	ldr	x2, [x29,120]
	mov	w3, w20
	bl	HynixSetRRPara
.L1037:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	mov	w20, 0
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	ldr	w1, [x22,4]
	adrp	x0, .LC18
	ldrb	w2, [x21,1229]
	add	x0, x0, :lo12:.LC18
	mov	w3, w28
	bl	printk
	cmn	w28, #1
	bne	.L1036
	ldrb	w0, [x21,1228]
	cbz	w0, .L1036
	ldr	w0, [x29,140]
	cbz	w0, .L1038
	mov	w0, w19
	bl	flash_enter_slc_mode
	b	.L1039
.L1038:
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1039:
	ldr	x4, [x21,2208]
	mov	w0, w19
	ldr	w1, [x29,156]
	mov	w20, 0
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	blr	x4
	mov	w28, w0
	b	.L1036
.L1104:
	ldr	w1, [x29,156]
	mov	w0, w19
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1036
.L1055:
	mov	w20, 0
.L1035:
	ldrb	w0, [x21,1229]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1036
	ldr	x0, [x21,2208]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1036:
	cmp	w28, 256
	beq	.L1060
	cmn	w28, #1
	bne	.L1040
.L1060:
	str	w28, [x26,x27]
	b	.L1042
.L1040:
	str	wzr, [x26,x27]
.L1042:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1044
	ldr	w1, [x22,4]
	adrp	x0, .LC15
	ldrb	w2, [x21,1229]
	add	x0, x0, :lo12:.LC15
	bl	printk
	ldr	x1, [x22,16]
	cbz	x1, .L1044
	adrp	x0, .LC17
	mov	w2, 4
	add	x0, x0, :lo12:.LC17
	mov	w3, w2
	bl	rknand_print_hex
.L1044:
	cbz	w20, .L1046
	ldrb	w0, [x21,1229]
	add	w0, w0, w0, lsl 1
	cmp	w24, w0, lsr 2
	bls	.L1047
	ldr	x0, [x21,2208]
	cmp	x0, xzr
	mov	w0, 256
	csel	w24, w24, w0, ne
.L1047:
	add	w0, w25, 1
	mov	w1, 56
	cmp	w24, 256
	umull	x0, w0, w1
	beq	.L1061
	cmn	w24, #1
	bne	.L1048
.L1061:
	str	w24, [x26,x0]
	b	.L1046
.L1048:
	str	wzr, [x26,x0]
.L1046:
	ldr	w0, [x29,140]
	add	w25, w25, w20
	cbz	w0, .L1016
	ldrb	w0, [x21,1228]
	cbz	w0, .L1016
	mov	w0, w19
	bl	flash_exit_slc_mode
.L1016:
	add	w25, w25, 1
	b	.L1013
.L1103:
	mov	w0, 0
	b	.L1101
.L1026:
	mov	w0, w19
	bl	NandcWaitFlashReady
	cbz	w20, .L1023
	ldr	w1, [x29,156]
	mov	w0, w19
	bl	FlashReadDpDataOutCmd
	b	.L1023
.L1101:
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
	ldrh	w0, [x19,1212]
	adrp	x25, .LC19
	ldrh	w21, [x19,1214]
	adrp	x26, .LANCHOR0
	add	x25, x25, :lo12:.LC19
	mul	w21, w0, w21
	add	x0, x19, 2272
	uxth	w21, w21
	bl	ftl_memset
	add	w24, w21, w27
	ldr	x0, [x19,2240]
	str	xzr, [x29,144]
	mov	w19, w28
	str	x0, [x29,152]
	uxth	w24, w24
.L1106:
	add	x1, x26, :lo12:.LANCHOR0
	ldrb	w1, [x1,3208]
	cmp	w1, w19
	bls	.L1116
	mul	w4, w19, w21
	mov	w20, w24
	sub	w3, w21, #12
	mov	w5, 61664
.L1107:
	cmp	w20, w3
	ble	.L1109
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
	beq	.L1108
	add	x23, x22, :lo12:.LANCHOR2
	ldr	x0, [x23,2240]
	ldrh	w0, [x0]
	cmp	w0, w5
	bne	.L1108
	add	x23, x23, 2272
	mov	x0, x25
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x23,w19,sxtw 1]
	uxth	w28, w28
	b	.L1109
.L1108:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1107
.L1109:
	adrp	x0, .LANCHOR0
	add	w19, w19, 1
	add	x1, x0, :lo12:.LANCHOR0
	uxtb	w19, w19
	ldrb	w1, [x1,3208]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1106
.L1116:
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
	ldrh	w1, [x0,1214]
	mov	x21, x4
	ldrh	w3, [x0,1212]
	mov	w26, 61664
	ldr	x0, [x0,2240]
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
.L1118:
	cmp	w20, w19
	ble	.L1126
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1119
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,2240]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1119
	mov	w0, w22
	cbz	x22, .L1120
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.6
	b	.L1120
.L1119:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1118
.L1126:
	mov	w0, -1
.L1120:
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
	adrp	x0, .LANCHOR0+3448
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	ldr	x0, [x0,#:lo12:.LANCHOR0+3448]
	ldrb	w2, [x0,13]
	ldrh	w19, [x0,14]
	add	x0, x20, :lo12:.LANCHOR2
	mul	w19, w2, w19
	ldr	x0, [x0,2232]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L1128
.L1132:
	mov	w1, 0
	b	.L1129
.L1128:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L1130:
	cmp	w2, w19
	bge	.L1129
	add	x3, x20, :lo12:.LANCHOR2
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,2232]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L1131
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L1131:
	cmp	w1, w0
	bcs	.L1132
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1130
.L1129:
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
	mov	w24, w2
	stp	x25, x26, [sp,64]
	add	x22, x22, :lo12:.LANCHOR0
	ldrb	w26, [x1,#:lo12:.LANCHOR1+481]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w27, w3
	mov	x19, x0
	mov	x21, x0
	mov	w25, 0
	add	x28, x22, 3212
.L1138:
	cmp	w25, w23
	beq	.L1167
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,3208]
	cmp	w0, w1
	bcc	.L1139
	mov	w0, -1
	str	w0, [x21]
	b	.L1140
.L1139:
	ldrb	w20, [x28,w0,uxtw]
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
	tbz	x0, 0, .L1143
	mov	w0, -1
	str	w0, [x21]
.L1143:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1140:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1138
.L1167:
	mov	w21, 0
	cbz	w27, .L1165
	adrp	x25, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	add	x25, x25, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1166:
	cmp	w21, w23
	beq	.L1165
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1146
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1147
.L1146:
	adrp	x20, .LANCHOR2
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,2248]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,2256]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,2248]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,2256]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1148
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1148:
	ldr	x0, [x19,16]
	cbz	x0, .L1149
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,2256]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1149
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1149:
	ldr	x0, [x19,8]
	cbz	x0, .L1147
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,2248]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1147
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1147:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1166
.L1165:
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
	mov	w26, w1
	ldr	x5, [x4,3448]
	mov	w23, w2
	ldrb	w4, [x4,88]
	ldrb	w5, [x5,19]
	str	w5, [x29,120]
	adrp	x5, .LANCHOR1+481
	ldrb	w5, [x5,#:lo12:.LANCHOR1+481]
	str	w5, [x29,124]
	cbz	w4, .L1169
	bl	FlashProgSlc2KPages
	b	.L1170
.L1169:
	adrp	x28, .LANCHOR2
	add	x24, x28, :lo12:.LANCHOR2
	add	x0, x24, 1244
	str	x0, [x29,104]
.L1221:
	cmp	w22, w26
	bcs	.L1222
	mov	w20, 56
	mov	w1, w23
	add	x3, x29, 132
	sub	w4, w26, w22
	umull	x20, w22, w20
	add	x2, x29, 128
	add	x27, x19, x20
	mov	x0, x27
	bl	LogAddr2PhyAddr
	mov	w25, w0
	add	x1, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w3, [x1,3208]
	cmp	w0, w3
	bcc	.L1171
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1172
.L1171:
	add	x2, x28, :lo12:.LANCHOR2
	ldrb	w2, [x2,2192]
	cmp	w2, wzr
	add	x2, x1, 3252
	uxtw	x1, w0
	mov	x0, 24
	csel	w25, w25, wzr, ne
	madd	x0, x1, x0, x2
	ldr	x0, [x0,8]
	cbz	x0, .L1174
	cmp	w3, 1
	bne	.L1175
	ldr	x0, [x24,1160]
	bl	NandcIqrWaitFlashReady
.L1175:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1174:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	mov	x1, 24
	add	x0, x0, 3252
	madd	x0, x2, x1, x0
	ldr	w1, [x29,128]
	str	x27, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w25, .L1176
	add	w1, w22, 1
	mov	w3, 56
	umull	x1, w1, w3
	add	x1, x19, x1
	str	x1, [x0,16]
.L1176:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, 3212
	ldrb	w20, [x0,x2]
	mov	x0, 24
	madd	x2, x2, x0, x1
	ldrb	w0, [x1,3208]
	strb	w20, [x2,3252]
	cmp	w0, 1
	bne	.L1177
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1178
.L1177:
	mov	w0, w20
	str	x1, [x29,96]
	bl	NandcFlashCs
	ldr	x1, [x29,96]
	ldr	w0, [x29,132]
	add	x1, x1, 3220
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1178:
	ldr	w0, [x29,120]
	sub	w0, w0, #1
	cmp	w0, 7
	bhi	.L1179
	add	x0, x24, x20, sxtw
	ldrb	w0, [x0,2096]
	cbz	w0, .L1179
	ldrb	w1, [x24,1241]
	mov	w0, w20
	ldr	x2, [x29,104]
	mov	w3, 0
	bl	HynixSetRRPara
.L1179:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1180
	ldrb	w0, [x24,1228]
	cbz	w0, .L1180
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1181
.L1180:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1181:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldrb	w2, [x29,124]
	mov	w0, w20
	ldr	x3, [x27,8]
	mov	w1, 1
	ldr	x4, [x27,16]
	bl	NandcXferData
	cbz	w25, .L1182
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x27, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x27, 3220
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
.L1182:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w25
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1172:
	add	w22, w22, 1
	b	.L1221
.L1222:
	adrp	x20, .LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	add	x0, x20, :lo12:.LANCHOR2
	mov	x22, 0
	add	x24, x21, 3252
	mov	x25, 24
	ldr	x0, [x0,1160]
	bl	NandcIqrWaitFlashReady
.L1184:
	ldrb	w0, [x21,3208]
	cmp	w0, w22
	bls	.L1223
	mov	w0, w22
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1185
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L1185
	mul	x0, x22, x25
	ldrb	w0, [x0,x24]
	bl	flash_exit_slc_mode
.L1185:
	add	x22, x22, 1
	b	.L1184
.L1223:
	ldr	w0, [x29,116]
	cbnz	w0, .L1187
.L1195:
	mov	w0, 0
	b	.L1170
.L1187:
	adrp	x24, .LC23
	adrp	x25, .LC22
	adrp	x27, .LC21
	mov	w21, 0
	add	x24, x24, :lo12:.LC23
	add	x25, x25, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1188:
	cmp	w21, w26
	beq	.L1195
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1189
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1190
.L1189:
	add	x22, x20, :lo12:.LANCHOR2
	sub	w4, w26, w21
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,2248]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,2256]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,2248]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,2256]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1191
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1191:
	ldr	x0, [x19,16]
	cbz	x0, .L1192
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,2256]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1192
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1192:
	ldr	x0, [x19,8]
	cbz	x0, .L1190
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,2248]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1190
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1190:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1188
.L1170:
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
	ldr	x0, [x19,2232]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,2232]
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
	bcc	.L1226
	mov	w0, w1
	bl	FlashTestBlk.part.7
.L1226:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	add	x0, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	mov	x20, x4
	mov	w19, 0
	ldr	x1, [x0,2240]
	add	x25, x20, :lo12:.LANCHOR0
	ldrh	w21, [x0,1214]
	str	x1, [x29,160]
	ldrh	w1, [x0,1212]
	add	x0, x4, :lo12:.LANCHOR0
	mul	w21, w1, w21
	ldr	x1, [x0,3448]
	uxth	w21, w21
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
	adrp	x0, .LC24
	mov	w1, 1
	add	x0, x0, :lo12:.LC24
	bl	printk
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	mov	w2, 4096
	ldr	x0, [x0,2240]
	bl	ftl_memset
	lsr	w0, w21, 4
	str	w0, [x29,168]
	add	x0, x25, 3212
	str	x0, [x29,112]
	ldr	w0, [x29,128]
	and	w0, w0, 1
	str	w0, [x29,108]
	sub	w0, w21, #1
	uxth	w0, w0
	str	w0, [x29,124]
.L1230:
	ldrb	w0, [x25,3208]
	cmp	w0, w19
	bls	.L1281
	add	x0, x22, :lo12:.LANCHOR2
	sxtw	x24, w19
	add	x1, x0, 2272
	ldrh	w20, [x1,w19,sxtw 1]
	cbnz	w20, .L1260
	ldrh	w2, [x0,1220]
	mov	w1, w20
	ldr	x0, [x25]
	mov	w26, w20
	lsl	w2, w2, 9
	mov	w23, w20
	add	x28, x25, 3220
	bl	ftl_memset
	ldr	x0, [x29,112]
	ldrb	w27, [x0,x24]
	ldr	w0, [x29,128]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,120]
.L1232:
	uxth	w0, w26
	str	w0, [x29,144]
	cmp	w0, w21
	bcs	.L1242
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,108]
	cbz	w0, .L1234
	ldr	w3, [x28,x24,lsl 2]
	mov	w0, w27
	add	x2, x29, 182
	add	w3, w20, w3
	str	x3, [x29,96]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x25,88]
	ldr	x3, [x29,96]
	cmp	w0, 1
	bne	.L1234
	ldr	w1, [x25,92]
	mov	w0, w27
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1234:
	ldr	w0, [x29,120]
	cbz	w0, .L1236
	ldr	x0, [x25,3448]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x24,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w27
	add	w1, w1, w20
	bl	FlashReadSpare
.L1236:
	ldr	x0, [x25,3448]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1261
	cmp	w0, 1
	bne	.L1237
.L1261:
	mov	w0, 1
	cbz	w1, .L1239
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1239
.L1237:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1239
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1239:
	ldr	x1, [x29,128]
	tbz	x1, 2, .L1240
	ldr	w1, [x28,x24,lsl 2]
	mov	w0, w27
	add	w1, w20, w1
	bl	SandiskProgTestBadBlock
.L1240:
	cbz	w0, .L1241
	adrp	x0, .LC25
	mov	w1, w19
	mov	w2, w26
	add	x0, x0, :lo12:.LC25
	add	w23, w23, 1
	bl	printk
	ldr	x0, [x29,144]
	uxth	w23, w23
	ldr	x2, [x25]
	ubfx	x1, x0, 5, 11
	ldrb	w3, [x29,144]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w0, w0, w3
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
	ldr	w1, [x29,168]
	ldrb	w0, [x25,3208]
	mul	w0, w1, w0
	cmp	w23, w0
	bgt	.L1242
.L1241:
	ldr	w0, [x29,172]
	add	w26, w26, 1
	add	w20, w20, w0
	b	.L1232
.L1242:
	adrp	x0, .LC26
	mov	w1, w19
	add	x0, x0, :lo12:.LC26
	mov	w2, w23
	bl	printk
	ldrb	w0, [x25,3208]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w23, w0
	blt	.L1244
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,1220]
	ldr	x0, [x25]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1244:
	cbnz	w19, .L1246
	adrp	x26, .LC27
	ldrh	w27, [x25,8]
	mov	w20, w19
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
.L1247:
	ldrb	w0, [x25,89]
	cmp	w0, w27
	bls	.L1282
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1248
	mov	w1, w27
	mov	x0, x26
	add	w20, w20, 1
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x25]
	lsl	x1, x1, 2
	lsl	w0, w23, w27
	uxth	w20, w20
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1248:
	add	w27, w27, 1
	uxth	w27, w27
	b	.L1247
.L1282:
	adrp	x26, .LC27
	ldr	w28, [x29,124]
	sub	w27, w21, #50
	add	x26, x26, :lo12:.LC27
	mov	w23, 1
.L1250:
	cmp	w28, w27
	ble	.L1283
	mov	w0, w28
	bl	FlashTestBlk
	cbz	w0, .L1251
	mov	w1, w28
	mov	x0, x26
	bl	printk
	ubfx	x1, x28, 5, 11
	ldr	x2, [x25]
	lsl	x1, x1, 2
	lsl	w0, w23, w28
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1251:
	sub	w28, w28, #1
	uxth	w28, w28
	b	.L1250
.L1283:
	ldrb	w0, [x25,89]
	ldr	w1, [x25,8]
	sub	w0, w0, w1
	cmp	w20, w0
	bcc	.L1246
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,1220]
	ldr	x0, [x25]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1246:
	adrp	x23, .LC28
	ldr	w27, [x29,124]
	mul	w26, w19, w21
	add	x23, x23, :lo12:.LC28
	add	x28, x22, :lo12:.LANCHOR2
	mov	w20, -3872
.L1254:
	mov	w2, w27
	mov	x0, x23
	mov	w1, w19
	bl	printk
	ldr	x2, [x25]
.L1255:
	ubfx	x0, x27, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w27
	and	w1, w0, 1
	tbz	x0, 0, .L1284
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1255
.L1284:
	add	x0, x28, 2272
	strh	w27, [x0,x24,lsl 1]
	ldr	x0, [x29,160]
	strh	w20, [x0]
	strh	w27, [x0,2]
	strh	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x25]
	mov	w2, w1
	str	x0, [x29,192]
	ldr	x0, [x28,2240]
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
	cbz	w0, .L1260
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1254
.L1260:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L1230
.L1281:
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
.L1286:
	cmp	w2, w0
	bhi	.L1288
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L1286
.L1288:
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
	ldrh	w2, [x1,2292]
	ldrh	w3, [x1,2302]
	str	w0, [x1,2288]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,2300]
	ldr	w0, [x1,2308]
	str	w2, [x1,2296]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,2304]
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
	strh	w6, [x1,2312]
	strh	w2, [x1,2314]
	strh	w0, [x1,2316]
	strh	w4, [x1,2302]
.L1293:
	add	x5, x1, 2320
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1293
	ldrh	w3, [x23,20]
	ldrb	w1, [x23,15]
	cmp	w3, w1
	bcs	.L1294
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x20, :lo12:.LANCHOR2
.L1295:
	cmp	w3, w0
	bcs	.L1297
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L1298:
	cmp	w7, w2
	bcs	.L1320
	add	x9, x12, 2320
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L1298
.L1320:
	add	w3, w3, 1
	b	.L1295
.L1297:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,2314]
	strh	w4, [x1,2302]
.L1294:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,2352]
	strh	wzr, [x1,2354]
	bne	.L1299
	strh	w6, [x1,2352]
.L1299:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 4352
	strh	w2, [x1,2356]
	adrp	x2, .LANCHOR0+88
	ldrb	w26, [x2,#:lo12:.LANCHOR0+88]
	cbz	w26, .L1300
	mov	w2, 384
	strh	w2, [x1,2356]
.L1300:
	add	x21, x20, :lo12:.LANCHOR2
	ldrh	w25, [x23,16]
	ldrh	w24, [x23,20]
	ldrh	w27, [x23,18]
	ldrh	w22, [x21,2314]
	ldrh	w19, [x21,2302]
	strh	w25, [x21,2360]
	mul	w22, w0, w22
	mul	w0, w19, w0
	strh	w0, [x21,2358]
	uxth	w22, w22
	strh	w27, [x21,2362]
	strh	w22, [x21,2292]
	mul	w0, w22, w25
	strh	w0, [x21,2364]
	mov	w0, w24
	strh	w24, [x21,2366]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w24, 9
	strh	w0, [x21,2368]
	ldrh	w0, [x23,26]
	cmp	w19, 1024
	strh	w2, [x21,2370]
	ubfx	x2, x2, 9, 7
	strh	w0, [x21,2374]
	lsl	w2, w2, 1
	mul	w0, w22, w19
	strh	w2, [x21,2372]
	str	w0, [x21,2308]
	bls	.L1301
	and	w0, w19, 255
	strh	w0, [x21,2354]
.L1301:
	add	x2, x20, :lo12:.LANCHOR2
	mul	w0, w24, w27
	ldrh	w1, [x2,2354]
	sub	w1, w19, w1
	mul	w1, w22, w1
	mul	w1, w1, w24
	mul	w1, w1, w25
	asr	w1, w1, 11
	str	w1, [x2,2376]
	ldrh	w1, [x2,2356]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L1317
	mov	w0, 4
.L1317:
	strh	w0, [x2,2380]
	cbz	w26, .L1304
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0,2356]
.L1304:
	add	x1, x20, :lo12:.LANCHOR2
	lsl	w19, w19, 6
	cmp	w22, 1
	ldrh	w0, [x1,2356]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w19, w19, w3
	strh	w19, [x1,2384]
	and	w19, w19, 65535
	add	w0, w0, 2
	strh	w0, [x1,2382]
	mul	w0, w19, w22
	str	w0, [x1,2388]
	add	w19, w19, 8
	ldrh	w0, [x1,2380]
	udiv	w0, w0, w22
	add	w19, w19, w0
	bne	.L1318
	add	w19, w19, 4
.L1318:
	str	w19, [x1,2288]
	add	x19, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19,2288]
	bl	FtlSysBlkNumInit
	str	wzr, [x19,1232]
	ldr	w0, [x19,2288]
	str	w0, [x19,2392]
	ldr	w0, [x19,2304]
	ldrh	w1, [x19,2360]
	lsl	w0, w0, 2
	ldrh	w3, [x19,2366]
	ldrh	w2, [x19,2380]
	mul	w1, w0, w1
	ldrh	w0, [x19,2368]
	ldrb	w5, [x19,1228]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x19,2398]
	uxth	w0, w0
	strh	w0, [x19,2396]
	add	w1, w2, 3
	strh	w1, [x19,2380]
	ldr	w1, [x19,2388]
	add	w4, w1, 3
	str	w4, [x19,2388]
	cbz	w5, .L1307
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x19,2380]
	b	.L1319
.L1307:
	cmp	w4, 7
	bhi	.L1308
	mov	w1, 8
.L1319:
	str	w1, [x19,2388]
.L1308:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x20,2300]
	strh	wzr, [x20,2400]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L1309
	mov	w0, 1
	strh	w0, [x20,2400]
.L1309:
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
	str	w1, [x0,2472]
	mov	w1, -1
	strh	wzr, [x0,2402]
	strh	w1, [x0,2488]
	strh	w1, [x0,2490]
	strh	w1, [x0,2492]
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
	str	wzr, [x0,2456]
	str	wzr, [x0,2460]
	str	wzr, [x0,2464]
	str	wzr, [x0,2468]
	str	wzr, [x0,2476]
	str	wzr, [x0,2480]
	str	wzr, [x0,2484]
	strh	w1, [x0,2494]
	mov	w1, 32
	ldrh	w2, [x0,2366]
	strh	w1, [x0,2496]
	mov	w1, 128
	strh	w1, [x0,2498]
	mov	w1, 1024
	strh	wzr, [x0,2500]
	sdiv	w2, w1, w2
	ldrh	w1, [x0,2292]
	strh	wzr, [x0,2502]
	lsl	w1, w1, 2
	strh	wzr, [x0,2504]
	strh	wzr, [x0,2506]
	cmp	w2, w1
	str	w2, [x0,2508]
	bls	.L1322
	str	w1, [x0,2508]
.L1322:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19,2364]
	str	wzr, [x19,2512]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2520]
	ldrh	w1, [x19,2364]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,2292]
	str	x0, [x19,2528]
	mul	w21, w21, w22
	lsl	w23, w21, 3
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2536]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2544]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,2552]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2560]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2568]
	ldr	w0, [x19,2508]
	mul	w0, w0, w22
	bl	ftl_malloc
	str	x0, [x19,2576]
	ldrh	w0, [x19,2292]
	ldrh	w21, [x19,2370]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,2584]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2592]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2600]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2608]
	ldr	w0, [x19,2584]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,2616]
	ldr	w0, [x19,2508]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,2624]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2632]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2640]
	ldr	w1, [x19,2584]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,2292]
	str	x0, [x19,2648]
	ldrh	w0, [x19,2372]
	mul	w21, w0, w21
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2656]
	lsl	w0, w21, 3
	bl	ftl_malloc
	str	x0, [x19,2664]
	ldrh	w1, [x19,2372]
	ldr	w0, [x19,2584]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2672]
	ldrh	w1, [x19,2372]
	ldr	w0, [x19,2508]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2680]
	ldrh	w0, [x19,2302]
	lsl	w0, w0, 1
	strh	w0, [x19,2688]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,2696]
	ldrh	w0, [x19,2688]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,2688]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19,2302]
	str	x0, [x19,2704]
	add	x0, x0, 32
	lsl	w21, w21, 1
	str	x0, [x19,2712]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2720]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19,2388]
	str	x0, [x19,2728]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2736]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2744]
	ldrh	w0, [x19,2302]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	adrp	x1, .LANCHOR0+16
	str	x0, [x1,#:lo12:.LANCHOR0+16]
	ldrh	w0, [x19,2380]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2752]
	ldrh	w0, [x19,2380]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,2760]
	ldrh	w0, [x19,2380]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2768]
	ldrh	w0, [x19,2382]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2776]
	ldrh	w2, [x19,2382]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w21, [x19,2396]
	lsl	w21, w21, 2
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2784]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,2792]
	ldr	w0, [x19,2388]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2800]
	ldrh	w0, [x19,2398]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,2808]
	ldrh	w1, [x19,2398]
	ldrh	w0, [x19,2370]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2816]
	ldrh	w1, [x19,2302]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,2824]
	ldrh	w0, [x19,2358]
	ldrh	w1, [x19,2314]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,2832]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,2872]
	add	x1, x19, 2840
	ldrh	w4, [x19,2832]
	ldrh	w5, [x19,2314]
	mov	w0, 1
	mov	w2, w4
.L1323:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L1423
	ldr	x3, [x19,2872]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L1323
.L1423:
	add	x1, x20, :lo12:.LANCHOR2
.L1325:
	cmp	w0, 8
	beq	.L1424
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,2872]
	b	.L1325
.L1424:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2736]
	cbnz	x1, .L1327
.L1329:
	adrp	x0, .LC29
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC29
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L1328
.L1327:
	ldr	x1, [x0,2744]
	cbz	x1, .L1329
	ldr	x1, [x0,2784]
	cbz	x1, .L1329
	ldr	x1, [x0,2800]
	cbz	x1, .L1329
	ldr	x1, [x0,2808]
	cbz	x1, .L1329
	ldr	x1, [x0,2816]
	cbz	x1, .L1329
	ldr	x1, [x0,2824]
	cbz	x1, .L1329
	ldr	x1, [x0,2872]
	cbz	x1, .L1329
	ldr	x0, [x0,2728]
	cbz	x0, .L1329
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2520]
	cbz	x1, .L1329
	ldr	x1, [x0,2528]
	cbz	x1, .L1329
	ldr	x1, [x0,2536]
	cbz	x1, .L1329
	ldr	x1, [x0,2552]
	cbz	x1, .L1329
	ldr	x1, [x0,2560]
	cbz	x1, .L1329
	ldr	x1, [x0,2568]
	cbz	x1, .L1329
	ldr	x1, [x0,2544]
	cbz	x1, .L1329
	ldr	x1, [x0,2592]
	cbz	x1, .L1329
	ldr	x1, [x0,2600]
	cbz	x1, .L1329
	ldr	x0, [x0,2608]
	cbz	x0, .L1329
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,2616]
	cbz	x1, .L1329
	ldr	x1, [x0,2632]
	cbz	x1, .L1329
	ldr	x1, [x0,2640]
	cbz	x1, .L1329
	ldr	x1, [x0,2648]
	cbz	x1, .L1329
	ldr	x1, [x0,2656]
	cbz	x1, .L1329
	ldr	x1, [x0,2664]
	cbz	x1, .L1329
	ldr	x1, [x0,2672]
	cbz	x1, .L1329
	ldr	x1, [x0,2712]
	cbz	x1, .L1329
	ldr	x1, [x0,2696]
	cbz	x1, .L1329
	ldr	x0, [x0,2752]
	cbz	x0, .L1329
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,2760]
	cbz	x1, .L1329
	ldr	x1, [x0,2768]
	cbz	x1, .L1329
	ldr	x0, [x0,2776]
	cbz	x0, .L1329
	mov	w0, 0
.L1328:
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
	ldrh	w2, [x1,2936]
	cbz	w2, .L1426
	ldrh	w2, [x1,2380]
	ldr	x3, [x1,2752]
	mov	x1, 0
.L1427:
	cmp	w2, w1, uxth
	bls	.L1432
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L1427
	mov	w0, 1
	b	.L1426
.L1432:
	mov	w0, 0
.L1426:
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
	ldr	w3, [x2,2512]
	cbz	w3, .L1434
	ldr	x5, [x2,2576]
	mov	x0, 56
	mov	x2, 0
.L1435:
	cmp	w3, w2
	bls	.L1444
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L1435
	cmp	w4, w1
	bhi	.L1435
	mov	w0, 1
	b	.L1434
.L1444:
	mov	w0, 0
.L1434:
	ret
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	adrp	x0, .LANCHOR2+1232
	ldr	w0, [x0,#:lo12:.LANCHOR2+1232]
	ret
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	adrp	x0, .LANCHOR2+1232
	ldr	w0, [x0,#:lo12:.LANCHOR2+1232]
	ret
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	adrp	x0, .LANCHOR2+2940
	ldr	w0, [x0,#:lo12:.LANCHOR2+2940]
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
	ldrh	w3, [x19,2358]
	add	x19, x19, 2840
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
	ldrh	w1, [x3,2358]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,2872]
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
	ldrh	w2, [x19,2832]
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L1453:
	ldrh	w2, [x21,x3]
	cmp	w2, w0
	beq	.L1451
	ubfx	x4, x2, 5, 11
	lsl	w2, w5, w2
	lsl	x4, x4, 2
	add	x3, x3, 2
	cmp	x3, 1024
	ldr	w1, [x20,x4]
	orr	w2, w1, w2
	str	w2, [x20,x4]
	ldrh	w2, [x19,2846]
	add	w2, w2, 1
	strh	w2, [x19,2846]
	bne	.L1453
.L1451:
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
	ldr	w1, [x19,2944]
	stp	x25, x26, [sp,64]
	cbnz	w1, .L1458
	ldr	x0, [x19,2592]
	mov	x22, 0
	ldr	x3, [x19,2656]
	add	x21, x19, 2840
	ldrh	w2, [x19,2370]
	str	x0, [x19,2960]
	str	x3, [x19,2968]
	bl	ftl_memset
.L1459:
	ldrh	w1, [x19,2314]
	mov	w0, w22
	add	x20, x19, 2952
	add	x22, x22, 1
	cmp	w0, w1
	bge	.L1472
	add	x1, x21, x22, lsl 3
	ldrh	w2, [x19,2832]
	ldr	x3, [x20,8]
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1459
.L1472:
	ldr	x26, [x20,16]
	mov	w1, 255
	mov	w2, 16
	mov	w22, 0
	mov	x0, x26
	adrp	x24, .LC31
	adrp	x25, .LC32
	mov	w23, w22
	bl	ftl_memset
	add	x24, x24, :lo12:.LC31
	mov	w0, -3887
	strh	w0, [x26]
	add	x25, x25, :lo12:.LC32
	ldr	w0, [x21,8]
	str	w0, [x26,4]
	ldrh	w0, [x19,2840]
	strh	w0, [x26,2]
	ldrh	w0, [x21,4]
	strh	w0, [x26,8]
	ldrh	w0, [x21,6]
	strh	w0, [x26,10]
	ldr	w0, [x19,2288]
	strh	w0, [x26,12]
.L1461:
	ldr	x0, [x19,2592]
	str	x0, [x20,8]
	ldrh	w1, [x19,2840]
	ldr	x0, [x19,2656]
	str	x0, [x20,16]
	ldrh	w2, [x19,2842]
	ldrh	w3, [x19,2844]
	orr	w0, w2, w1, lsl 10
	ldrh	w4, [x26,10]
	str	w0, [x20,4]
	mov	x0, x24
	str	wzr, [x20]
	bl	printk
	ldrh	w1, [x19,2362]
	ldrh	w0, [x19,2842]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1462
	ldr	w1, [x19,2848]
	ldrh	w0, [x19,2840]
	add	w1, w1, 1
	str	w1, [x19,2848]
	str	w1, [x26,4]
	strh	w0, [x26,8]
	ldrh	w1, [x19,2844]
	strh	w0, [x19,2844]
	ldr	x0, [x19,2560]
	strh	w1, [x19,2840]
	lsl	w1, w1, 10
	str	w1, [x20,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x19,2842]
	ldr	x0, [x19,2560]
	bl	FlashEraseBlocks
.L1462:
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,2842]
	ldr	w0, [x20]
	add	w1, w1, 1
	strh	w1, [x19,2842]
	cmn	w0, #1
	bne	.L1463
	ldr	w1, [x20,4]
	mov	x0, x25
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	cmp	w22, 3
	bls	.L1461
	adrp	x0, .LC33
	ldr	w1, [x20,4]
	add	x0, x0, :lo12:.LC33
	mov	w2, w22
	bl	printk
	mov	w0, 1
	str	w0, [x19,2944]
	b	.L1458
.L1463:
	add	w23, w23, 1
	cmp	w23, 1
	beq	.L1461
	cmp	w0, 256
	beq	.L1461
.L1458:
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
	add	x21, x1, 2952
	add	x23, x1, 2840
	ldr	x0, [x1,2592]
	mov	w22, 0
	ldr	x24, [x1,2656]
	mov	x19, x1
	str	x0, [x21,8]
	mov	w25, -1
	str	x24, [x21,16]
	mov	w26, 61664
.L1474:
	ldrh	w0, [x19,2314]
	cmp	w22, w0
	bcs	.L1483
	ldrh	w20, [x19,2358]
	strh	w25, [x23,12]
.L1476:
	sub	w20, w20, #1
	ldrh	w0, [x19,2358]
	uxth	w20, w20
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1477
	madd	w0, w22, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1476
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1476
	strh	w20, [x23,12]
.L1477:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L1474
.L1483:
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
	add	x0, x0, 2852
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
	ldrh	w1, [x0,2358]
	ldrh	w21, [x0,2314]
	mul	w21, w1, w21
.L1486:
	cmp	w20, w21
	bge	.L1492
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1487
	add	w19, w19, 1
	uxth	w19, w19
.L1487:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1486
.L1492:
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
	ldr	w22, [x20,2944]
	cbnz	w22, .L1494
	add	x24, x20, 2840
	mov	x19, x20
	mov	x25, x24
	add	x20, x20, 2952
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1495:
	ldrh	w0, [x19,2314]
	cmp	w22, w0
	bcs	.L1513
	ldrh	w4, [x25,12]
	mov	w1, 65535
	ldr	x0, [x19,2592]
	ldr	x26, [x19,2656]
	cmp	w4, w1
	str	x0, [x20,8]
	str	x26, [x20,16]
	beq	.L1496
	ldrh	w0, [x19,2358]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w22, w0, w4
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x19,2358]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L1497
.L1496:
	mov	w1, w22
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w23, [x19,2358]
.L1499:
	sub	w23, w23, #1
	uxth	w23, w23
.L1498:
	ldrh	w0, [x19,2358]
	madd	w0, w22, w0, w23
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1499
	ldr	x0, [x19,2656]
	mov	w1, 0
	mov	w2, 16
	strh	w23, [x25,12]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldrh	w2, [x19,2832]
	ldrh	w0, [x25,12]
	strh	w0, [x26,2]
	lsl	w2, w2, 2
	ldrh	w0, [x19,2358]
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
	bne	.L1497
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L1498
.L1497:
	mov	w0, w27
	add	w22, w22, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L1495
.L1513:
	mov	w19, 0
.L1502:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,2374]
	cmp	w0, w19
	bls	.L1514
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	uxth	w19, w19
	b	.L1502
.L1514:
	ldrh	w19, [x20,2852]
	mov	w23, 65535
	sub	w19, w19, #1
	uxth	w19, w19
.L1504:
	add	x22, x20, 2840
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1508
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1505
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1506
	mov	w0, w19
	bl	FtlBbmMapBadBlock
	b	.L1505
.L1506:
	ldrh	w0, [x20,2840]
	cmp	w0, w23
	bne	.L1507
	strh	w19, [x20,2840]
	b	.L1505
.L1507:
	strh	w19, [x22,4]
	b	.L1508
.L1505:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1504
.L1508:
	add	x19, x21, :lo12:.LANCHOR2
	mov	w2, 2
	ldr	x1, [x19,2560]
	ldrh	w0, [x19,2840]
	str	wzr, [x19,2848]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,2842]
	ldr	x0, [x19,2560]
	ldrh	w1, [x19,2844]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,2840]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,2844]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,2842]
	ldr	w0, [x19,2848]
	ldrh	w1, [x19,2844]
	add	w0, w0, 1
	str	w0, [x19,2848]
	ldrh	w0, [x19,2840]
	strh	w0, [x19,2844]
	strh	w1, [x19,2840]
	bl	FtlBbmTblFlush
.L1494:
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
	ldrh	w4, [x3,2316]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,2358]
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
	ldrh	w1, [x2,2316]
	ldrh	w3, [x2,2358]
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
	ldrh	w3, [x2,2358]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,2316]
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
	bls	.L1519
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L1520
.L1519:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L1520:
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
	bne	.L1522
	ldrh	w19, [x3,2362]
	b	.L1523
.L1522:
	ldrh	w19, [x3,2360]
.L1523:
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
	bne	.L1524
	mov	w22, 0
	mov	w25, 2
.L1525:
	cmp	w22, w19
	bgt	.L1524
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
	bne	.L1526
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1526
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1526
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1525
.L1526:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1525
.L1524:
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
	add	x21, x23, 2952
	ldr	x0, [x23,2592]
	ldr	x22, [x23,2656]
	str	x0, [x21,8]
	str	x22, [x21,16]
	bl	FtlBbtMemInit
	ldrh	w20, [x23,2358]
	sub	w20, w20, #1
	uxth	w20, w20
.L1533:
	ldrh	w0, [x23,2358]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1536
	lsl	w0, w20, 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1534
	ldr	w0, [x21,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
.L1534:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1535
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1535
	ldr	w1, [x22,4]
	str	w1, [x23,2848]
	strh	w20, [x23,2840]
	ldrh	w1, [x22,8]
	strh	w1, [x23,2844]
	b	.L1536
.L1535:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1533
.L1536:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,2840]
	cmp	w1, w2
	beq	.L1538
	ldrh	w1, [x21,2844]
	cmp	w1, w2
	beq	.L1540
	add	x0, x21, 2952
	lsl	w1, w1, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x21,2952]
	cmn	w0, #1
	beq	.L1540
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1540
	ldr	w0, [x22,4]
	ldr	w1, [x21,2848]
	cmp	w0, w1
	bls	.L1540
	str	w0, [x21,2848]
	ldrh	w1, [x21,2844]
	ldrh	w0, [x22,8]
	strh	w1, [x21,2840]
	strh	w0, [x21,2844]
.L1540:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 2952
	mov	w24, 61649
	ldrh	w0, [x20,2840]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20,2842]
.L1542:
	tbnz	w21, #31, .L1547
	ldrh	w0, [x20,2840]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w21, w0, lsl 10
	str	w0, [x23,4]
	ldr	x0, [x20,2592]
	str	x0, [x23,8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1543
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1543
.L1547:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,2846]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1544
	b	.L1545
.L1543:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1542
.L1544:
	ldr	w2, [x1,2288]
	cmp	w0, w2
	beq	.L1545
	ldrh	w1, [x1,2302]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1545
	cmp	w2, w1
	bcs	.L1545
	bl	FtlSysBlkNumInit
.L1545:
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, 0
	add	x21, x19, 2840
.L1548:
	ldrh	w0, [x19,2314]
	mov	w1, w20
	add	x20, x20, 1
	cmp	w1, w0
	bcs	.L1566
	ldrh	w2, [x19,2832]
	add	x0, x21, x20, lsl 3
	ldr	x3, [x19,2960]
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1548
.L1566:
	mov	w0, 0
.L1538:
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
	strh	w0, [x1,3008]
	add	x0, x1, 3016
	strh	wzr, [x1,3010]
	strh	wzr, [x1,3012]
	strh	wzr, [x1,3014]
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
	adrp	x0, .LANCHOR2+3014
	ldrh	w0, [x0,#:lo12:.LANCHOR2+3014]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR2+3014
	ldrh	w0, [x0,#:lo12:.LANCHOR2+3014]
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
	bcc	.L1570
	adrp	x2, .LANCHOR2
	add	x20, x2, :lo12:.LANCHOR2
	mov	x19, x2
	ldrh	w0, [x20,3014]
	cmp	w0, 1024
	beq	.L1570
	cbz	w1, .L1572
	ldr	w0, [x20,2944]
	cbnz	w0, .L1572
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,2560]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,2560]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,2712]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,2452]
	add	w0, w0, 1
	str	w0, [x20,2452]
.L1572:
	add	x1, x19, :lo12:.LANCHOR2
	add	x1, x1, 3008
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L1570:
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
	ldr	w2, [x28,2944]
	ldrb	w0, [x28,2192]
	stp	x23, x24, [sp,48]
	str	w0, [x29,120]
	uxtb	w24, w1
	stp	x19, x20, [sp,16]
	mov	w0, 0
	cbnz	w2, .L1580
	ldrb	w0, [x28,1228]
	mov	w26, w2
	str	w0, [x29,124]
	mov	w20, w2
	str	w25, [x28,2484]
	mov	w19, w2
	mov	w23, 56
	mov	w27, 4
.L1581:
	ldrh	w0, [x28,2292]
	cmp	w0, w26
	bls	.L1625
	umull	x0, w26, w23
	ldr	x1, [x28,2560]
	str	wzr, [x1,x0]
	add	x0, x28, 2320
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbz	w24, .L1582
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1583
.L1582:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1584
	umull	x0, w20, w23
	ldr	x1, [x28,2560]
	lsl	w22, w22, 10
	add	x1, x1, x0
	str	w22, [x1,4]
	ldr	x1, [x28,2560]
	ldr	x3, [x28,2664]
	add	x1, x1, x0
	ldrh	w0, [x28,2372]
	mul	w0, w20, w0
	add	w20, w20, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w27
	uxth	w20, w20
	add	x0, x3, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1583
.L1584:
	add	w19, w19, 1
	uxth	w19, w19
.L1583:
	add	w2, w26, 1
	uxth	w26, w2
	b	.L1581
.L1625:
	cbz	w20, .L1604
	ldr	w0, [x29,124]
	mov	w2, w20
	strb	wzr, [x28,2192]
	mov	x23, 0
	cmp	w0, wzr
	ldr	x0, [x28,2560]
	cset	w22, ne
	mov	x26, 56
	mov	w1, w22
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x28,2192]
.L1587:
	cmp	w20, w23, uxth
	bls	.L1626
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,2560]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1588
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1588:
	add	x23, x23, 1
	b	.L1587
.L1626:
	cbnz	w24, .L1590
	mov	w0, 1
	uxth	w22, w22
	mov	w27, 6
	str	w0, [x29,124]
	b	.L1591
.L1590:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,2362]
	ldrb	w0, [x0,1228]
	str	w1, [x29,124]
	cbnz	w0, .L1607
	uxtw	x0, w1
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1591
.L1607:
	mov	w22, 1
	mov	w27, w22
.L1591:
	mov	w23, 0
	add	x28, x21, :lo12:.LANCHOR2
.L1600:
	mov	w26, 0
	mov	w20, w26
.L1592:
	ldrh	w0, [x28,2292]
	cmp	w0, w26
	bls	.L1627
	mov	w0, 56
	ldr	x1, [x28,2560]
	umull	x0, w26, w0
	str	wzr, [x1,x0]
	add	x0, x28, 2320
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w24, .L1593
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1594
.L1593:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1594
	mov	w0, 56
	ldr	x1, [x28,2560]
	ldr	w2, [x29,112]
	umull	x0, w20, w0
	add	x1, x1, x0
	add	w2, w23, w2, lsl 10
	str	w2, [x1,4]
	mov	w2, 4
	ldr	x1, [x28,2560]
	add	x1, x1, x0
	ldr	x0, [x28,2632]
	str	x0, [x1,8]
	ldrh	w0, [x28,2372]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w2
	uxth	w20, w20
	ldr	x2, [x28,2640]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1594:
	add	w3, w26, 1
	uxth	w26, w3
	b	.L1592
.L1627:
	cbz	w20, .L1604
	ldr	x0, [x28,2560]
	mov	w1, w20
	mov	w2, w22
	mov	w3, 1
	strb	wzr, [x28,2192]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x28,2192]
.L1597:
	cmp	w20, w1, uxth
	bls	.L1628
	mov	x0, 56
	ldr	x2, [x28,2560]
	mul	x0, x1, x0
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1598
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1598:
	add	x1, x1, 1
	b	.L1597
.L1628:
	add	w23, w23, w27
	ldr	w0, [x29,124]
	uxth	w23, w23
	cmp	w23, w0
	bcc	.L1600
	mov	x23, 0
	mov	x26, 56
.L1601:
	cmp	w20, w23, uxth
	bls	.L1629
	cbz	w24, .L1602
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,2560]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1602
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1602:
	add	x23, x23, 1
	b	.L1601
.L1629:
	cmp	w25, 63
	bls	.L1608
	cbz	w24, .L1604
.L1608:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, w20
	ldr	x0, [x21,2560]
	bl	FlashEraseBlocks
.L1604:
	mov	w0, w19
.L1580:
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
	adrp	x0, .LANCHOR4+740
	ldrh	w6, [x0,#:lo12:.LANCHOR4+740]
	adrp	x0, .LANCHOR2
	add	x1, x0, :lo12:.LANCHOR2
	add	x1, x1, 3008
	ldrh	w2, [x1,6]
	cbz	w2, .L1630
	mov	w5, 0
	ldrh	w3, [x1,2]
	ldrh	w2, [x1,4]
	mov	w4, w5
	and	w6, w6, 31
.L1632:
	cmp	w4, w6
	bge	.L1640
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
	b	.L1632
.L1640:
	cbz	w5, .L1630
	add	x0, x0, :lo12:.LANCHOR2
	strh	w3, [x0,3010]
	strh	w2, [x0,3012]
.L1630:
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
	add	x21, x19, 3008
	mov	w24, 65533
	add	x23, x23, :lo12:.LC35
.L1642:
	ldrh	w1, [x21,6]
	adrp	x22, .LANCHOR2
	cbz	w1, .L1643
	ldrh	w2, [x21,2]
	sub	w1, w1, #1
	add	x0, x21, x2, sxtw 1
	strh	w1, [x21,6]
	add	w2, w2, 1
	ldr	w1, [x19,2944]
	and	w2, w2, 1023
	strh	w2, [x21,2]
	ldrh	w20, [x0,8]
	cbnz	w1, .L1644
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x19,2560]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	ldrb	w0, [x19,1228]
	cbz	w0, .L1645
	ldr	x0, [x19,2560]
	mov	w2, 1
	bl	FlashEraseBlocks
.L1645:
	add	x25, x22, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,2560]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x25,2712]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x25,2452]
	add	w0, w0, 1
	str	w0, [x25,2452]
	b	.L1644
.L1643:
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	bl	printk
.L1646:
	b	.L1646
.L1644:
	sub	w0, w20, #1
	cmp	w24, w0, uxth
	bcs	.L1647
	add	x22, x22, :lo12:.LANCHOR2
	mov	x0, x23
	mov	w1, w20
	ldrh	w2, [x22,3014]
	bl	printk
	b	.L1642
.L1647:
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
	adrp	x0, .LANCHOR2+2824
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+2824]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L1653:
	cmp	w1, w0
	beq	.L1654
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L1655
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L1653
.L1654:
	mov	w0, 1
	b	.L1652
.L1655:
	mov	w0, 0
.L1652:
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
	ldrh	w1, [x3,2300]
	cmp	w1, w0
	bls	.L1658
	mov	w7, 6
	ldr	x12, [x3,2824]
	adrp	x2, .LANCHOR4
	mov	w1, -1
	umull	x11, w0, w7
	add	x6, x2, :lo12:.LANCHOR4
	mov	x4, x2
	add	x5, x12, x11
	strh	w1, [x5,2]
	strh	w1, [x12,x11]
	ldr	x1, [x6,760]
	cbnz	x1, .L1659
	str	x5, [x6,760]
	b	.L1658
.L1659:
	ubfiz	x8, x0, 1, 16
	ldr	x14, [x3,2728]
	ldrh	w2, [x5,4]
	mov	x9, -6148914691236517206
	ldr	x13, [x3,2824]
	mov	w19, 65535
	ldrh	w6, [x14,x8]
	cmp	w2, wzr
	ldr	x15, [x3,2712]
	ldrh	w18, [x3,2300]
	mov	w3, 0
	mul	w6, w6, w2
	sub	x2, x1, x13
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x9, x2, x2
	add	x9, x15, x8
	mov	w8, w7
	uxth	w2, w2
.L1667:
	add	w3, w3, 1
	cmp	w0, w2
	uxth	w3, w3
	beq	.L1658
	cmp	w3, w18
	bhi	.L1658
	ubfiz	x16, x2, 1, 16
	ldrh	w17, [x1,4]
	cmp	w17, wzr
	ldrh	w7, [x14,x16]
	mul	w7, w7, w17
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L1663
	ldrh	w16, [x15,x16]
	ldrh	w7, [x9]
	cmp	w16, w7
	bcc	.L1665
	b	.L1664
.L1663:
	bhi	.L1664
.L1665:
	ldrh	w7, [x1]
	cmp	w7, w19
	bne	.L1666
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR4
	strh	w0, [x1]
	str	x5, [x2,768]
	b	.L1658
.L1666:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x13, x1
	b	.L1667
.L1664:
	strh	w2, [x12,x11]
	ldrh	w2, [x1,2]
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR4
	ldr	x3, [x2,760]
	cmp	x1, x3
	bne	.L1668
	strh	w0, [x1,2]
	str	x5, [x2,760]
	b	.L1658
.L1668:
	ldrh	w2, [x1,2]
	add	x3, x10, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3,2824]
	umull	x2, w2, w4
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L1658:
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
	ldrh	w1, [x0,776]
	add	w1, w1, 1
	strh	w1, [x0,776]
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
	beq	.L1673
	adrp	x3, .LANCHOR2
	mov	w4, 6
	add	x8, x3, :lo12:.LANCHOR2
	adrp	x2, .LANCHOR4
	umull	x6, w0, w4
	mov	w1, -1
	add	x9, x2, :lo12:.LANCHOR4
	ldr	x7, [x8,2824]
	mov	x11, x3
	mov	x3, x2
	add	x12, x7, x6
	strh	w1, [x12,2]
	strh	w1, [x7,x6]
	ldr	x1, [x9,784]
	cbnz	x1, .L1674
	str	x12, [x9,784]
	b	.L1673
.L1674:
	ldr	x10, [x8,2712]
	ubfiz	x2, x0, 1, 16
	ldr	x9, [x8,2824]
	mov	x8, -6148914691236517206
	ldrh	w13, [x10,x2]
	sub	x2, x1, x9
	asr	x2, x2, 1
	madd	x2, x8, x2, x2
	uxth	w2, w2
.L1677:
	ubfiz	x8, x2, 1, 16
	ldrh	w8, [x10,x8]
	cmp	w8, w13
	bcs	.L1675
	ldrh	w8, [x1]
	cmp	w8, w5
	bne	.L1676
	strh	w2, [x12,2]
	strh	w0, [x1]
	b	.L1673
.L1676:
	umull	x1, w8, w4
	mov	w2, w8
	add	x1, x9, x1
	b	.L1677
.L1675:
	ldrh	w4, [x1,2]
	strh	w4, [x12,2]
	strh	w2, [x7,x6]
	add	x2, x3, :lo12:.LANCHOR4
	ldr	x3, [x2,784]
	cmp	x1, x3
	bne	.L1678
	strh	w0, [x1,2]
	str	x12, [x2,784]
	b	.L1673
.L1678:
	ldrh	w2, [x1,2]
	add	x3, x11, :lo12:.LANCHOR2
	mov	w4, 6
	ldr	x3, [x3,2824]
	umull	x2, w2, w4
	strh	w0, [x3,x2]
	strh	w0, [x1,2]
.L1673:
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
	ldrh	w1, [x0,792]
	add	w1, w1, 1
	strh	w1, [x0,792]
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
	ldr	x2, [x6,2824]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L1681
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L1682
	str	xzr, [x0]
	b	.L1683
.L1682:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L1683
.L1681:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L1684
	cmp	w3, w0
	beq	.L1683
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L1683
.L1684:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,2824]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L1683:
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
	cbz	x2, .L1686
	adrp	x3, .LANCHOR2+2824
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+2824]
.L1687:
	cbnz	w1, .L1688
.L1690:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L1686
.L1688:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L1690
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L1687
.L1686:
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
	adrp	x0, .LANCHOR4+760
	ldr	x1, [x0,#:lo12:.LANCHOR4+760]
	mov	w0, 65535
	cbz	x1, .L1698
	adrp	x0, .LANCHOR2+2824
	mov	w5, 6
	ldr	x4, [x0,#:lo12:.LANCHOR2+2824]
	mov	w0, 65535
.L1694:
	cbz	w3, .L1695
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L1698
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L1694
.L1695:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L1698:
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
	ldrh	w0, [x4,800]
	cmp	w0, w20
	beq	.L1700
	ldrh	w0, [x4,848]
	cmp	w0, w20
	beq	.L1700
	ldrh	w0, [x4,896]
	cmp	w0, w20
	beq	.L1700
	adrp	x3, .LANCHOR2
	mov	w2, 6
	add	x3, x3, :lo12:.LANCHOR2
	ldr	x0, [x4,760]
	umull	x2, w20, w2
	ldr	x5, [x3,2824]
	add	x1, x5, x2
	cmp	x1, x0
	beq	.L1700
	ubfiz	x0, x20, 1, 16
	ldr	x4, [x3,2728]
	ldrh	w6, [x1,4]
	ldrh	w1, [x1,2]
	ldrh	w3, [x4,x0]
	cmp	w6, wzr
	mov	w0, 65535
	mul	w3, w3, w6
	csinv	w3, w3, wzr, ne
	cmp	w1, w0
	bne	.L1702
	ldrh	w0, [x5,x2]
	cmp	w0, w1
	beq	.L1700
.L1702:
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
	bcs	.L1700
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, w20
	add	x0, x19, 760
	bl	List_remove_node
	ldrh	w1, [x19,776]
	mov	w0, w20
	sub	w1, w1, #1
	strh	w1, [x19,776]
	bl	INSERT_DATA_LIST
.L1700:
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
.L1707:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1729
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1708:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L1730
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L1709
	cbz	w4, .L1709
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L1709:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1708
.L1730:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1707
.L1729:
	adrp	x0, .LANCHOR2+2362
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR2+2362]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L1712:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L1731
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L1713
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L1714
	b	.L1715
.L1713:
	cbnz	w1, .L1715
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L1715
.L1717:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L1715:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1712
.L1714:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L1717
	b	.L1715
.L1731:
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
.L1733:
	cmp	w20, w2
	beq	.L1737
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L1734
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1735
	adrp	x2, .LANCHOR2+3014
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	ldrh	w2, [x2,#:lo12:.LANCHOR2+3014]
	bl	printk
.L1736:
	b	.L1736
.L1735:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L1737
.L1734:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1733
.L1737:
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
	adrp	x23, .LC37
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	w25, w1
	mov	x19, x0
	mov	x27, x2
	mov	w22, 0
	mov	w28, 65535
	add	x21, x26, :lo12:.LANCHOR2
	add	x23, x23, :lo12:.LC37
.L1740:
	add	x0, x26, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR2
	ldr	w1, [x0,2436]
	add	w1, w1, 1
	str	w1, [x0,2436]
	ldrh	w0, [x0,2362]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1741
	ldrh	w0, [x19]
	cmp	w0, w28
	bne	.L1742
.L1741:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1742:
	ldr	w1, [x21,2944]
	cbnz	w1, .L1743
	ldrh	w2, [x19]
	add	x3, x21, 2952
	ldr	x0, [x19,16]
	str	x3, [x29,104]
	ldrh	w24, [x0,x2,lsl 1]
	mov	w2, 16
	ldrh	w0, [x19,2]
	str	x27, [x21,2960]
	orr	w0, w0, w24, lsl 10
	str	w0, [x21,2956]
	ldr	x0, [x21,2656]
	str	x0, [x21,2968]
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
	cbz	w0, .L1744
	ldr	x0, [x3,8]
	ldrh	w1, [x21,2370]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	str	w0, [x2,12]
.L1744:
	add	x4, x21, 2952
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
	ldr	w2, [x21,2952]
	cmn	w2, #1
	bne	.L1745
	ldr	w1, [x4,4]
	mov	x0, x23
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L1746
	ldrh	w0, [x21,2362]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L1746:
	cmp	w22, 3
	bls	.L1740
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	mov	w2, w22
	ldr	w1, [x20,2956]
	bl	printk
	mov	w0, 1
	str	w0, [x20,2944]
	b	.L1743
.L1745:
	cbz	w2, .L1748
	strh	w24, [x19,60]
.L1748:
	cmp	w1, 1
	beq	.L1749
	cmp	w2, 256
	beq	.L1749
	ldr	w0, [x19,56]
	cbz	w0, .L1750
.L1749:
	str	wzr, [x19,56]
	b	.L1740
.L1750:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x19,40]
	ldr	w1, [x20,2956]
	str	w1, [x0,w25,uxtw 2]
.L1743:
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
	blt	.L1758
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x21,x1]
	cbz	w24, .L1758
	ldr	w0, [x19,52]
	cbnz	w0, .L1758
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x21,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,2362]
	cmp	w1, w0
	bcc	.L1759
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1759:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w21, 0
	add	x22, x26, 2952
.L1760:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L1769
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L1761
	ldr	x1, [x26,2600]
	ldr	x23, [x26,2656]
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
	bne	.L1762
.L1764:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x27]
	adrp	x0, .LC39
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC39
	ldr	w1, [x20,2956]
	bl	printk
	mov	w0, 1
	str	w0, [x20,2944]
	b	.L1763
.L1762:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L1764
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1764
	ldr	x2, [x22,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L1761:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1760
.L1769:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1758:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,2362]
	cmp	w1, w0
	bcc	.L1763
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1763:
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
	ldr	w20, [x1,2944]
	ldr	x22, [x0,16]
	ldr	x24, [x0,40]
	cbnz	w20, .L1771
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1772
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
	b	.L1771
.L1772:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldrh	w23, [x22,x0]
	ldr	x22, [x1,2656]
	str	x22, [x1,2968]
	orr	w2, w2, w23, lsl 10
	str	w2, [x1,2956]
	ldr	x2, [x1,2592]
	str	x2, [x1,2960]
	ldr	w0, [x19,48]
	str	w0, [x22,4]
	mov	w0, -1291
	strh	w0, [x22,8]
	ldrh	w0, [x19,4]
	strh	w0, [x22]
	strh	w23, [x22,2]
	ldrh	w2, [x1,2362]
	ldr	x0, [x1,2592]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L1773:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1780
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x24,x3]
	cmp	w23, w1, lsr 10
	bne	.L1774
	add	x2, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,2592]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,2592]
	ldr	w3, [x24,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L1774:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1773
.L1780:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L1776
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,2960]
	ldrh	w1, [x1,2370]
	bl	js_hash
	str	w0, [x22,12]
.L1776:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 2952
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L1771:
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
	add	x0, x0, 944
	ldr	x1, [x20,2808]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,2808]
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
	ldrh	w2, [x0,2398]
	ldr	x1, [x0,2808]
.L1783:
	uxth	w0, w3
	cmp	w0, w2
	bcs	.L1799
	add	x3, x3, 1
	add	x5, x1, x3, lsl 4
	ldrh	w5, [x5,-16]
	cmp	w5, w4
	bne	.L1783
	b	.L1784
.L1799:
	mov	w0, w2
	mov	x3, 0
	mov	w6, -2147483648
.L1786:
	uxth	w5, w3
	cmp	w5, w2
	bcs	.L1800
	add	x4, x1, x3, lsl 4
	ldr	w4, [x4,4]
	tbnz	w4, #31, .L1795
	cmp	w4, w6
	bcc	.L1787
.L1795:
	mov	w4, w6
	mov	w5, w0
.L1787:
	add	x3, x3, 1
	mov	w6, w4
	mov	w0, w5
	b	.L1786
.L1800:
	cmp	w0, w2
	bcc	.L1784
	adrp	x0, .LANCHOR4+1008
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,#:lo12:.LANCHOR4+1008]
	mov	w0, w2
.L1790:
	cmp	w3, w2
	beq	.L1784
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L1791
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L1791:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L1790
.L1784:
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
	ldr	w5, [x4,2940]
	ldrh	w3, [x4,2368]
	cmp	w0, w5
	bcs	.L1802
	add	w27, w3, 7
	str	x1, [x29,104]
	lsr	w28, w0, w27
	mov	w25, w0
	str	w2, [x29,100]
	mov	x0, 0
	uxth	w22, w28
	ldrh	w1, [x4,2398]
	ldr	x19, [x4,2808]
	b	.L1803
.L1802:
	mov	w0, -1
	cbnz	w2, .L1804
	str	w0, [x1]
	b	.L1804
.L1808:
	add	x0, x0, 1
	add	x2, x19, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w22
	beq	.L1805
.L1803:
	uxth	x20, w0
	cmp	w20, w1
	bcc	.L1808
	bl	select_l2p_ram_region
	uxth	x20, w0
	ubfiz	x26, x20, 4, 16
	mov	w2, 65535
	add	x1, x19, x26
	ldrh	w3, [x19,x26]
	cmp	w3, w2
	bne	.L1816
.L1809:
	add	x19, x21, :lo12:.LANCHOR2
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x19,2784]
	ldr	w24, [x1,x0]
	cbnz	w24, .L1810
	ldr	x0, [x19,2808]
	mov	w1, 255
	ldrh	w2, [x19,2370]
	add	x0, x0, x26
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x19,2808]
	strh	w22, [x0,x26]
	ldr	x0, [x19,2808]
	add	x26, x0, x26
	str	w24, [x26,4]
.L1805:
	mov	x1, 1
	lsl	x3, x1, x27
	ldr	w1, [x29,100]
	sub	w3, w3, #1
	and	w0, w25, w3
	uxth	x0, w0
	cbnz	w1, .L1806
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1,2808]
	add	x1, x1, x20, lsl 4
	ldr	x1, [x1,8]
	ldr	w0, [x1,x0,lsl 2]
	ldr	x1, [x29,104]
	str	w0, [x1]
	b	.L1807
.L1806:
	ldr	x2, [x29,104]
	add	x3, x21, :lo12:.LANCHOR2
	lsl	x1, x20, 4
	ldr	w4, [x2]
	ldr	x2, [x3,2808]
	add	x2, x2, x1
	ldr	x2, [x2,8]
	str	w4, [x2,x0,lsl 2]
	ldr	x0, [x3,2808]
	add	x0, x0, x1
	ldr	w1, [x0,4]
	orr	w1, w1, -2147483648
	str	w1, [x0,4]
	adrp	x0, .LANCHOR4+1008
	strh	w22, [x0,#:lo12:.LANCHOR4+1008]
.L1807:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21,2808]
	add	x20, x0, x20, lsl 4
	mov	w0, 0
	ldr	w1, [x20,4]
	cmn	w1, #1
	beq	.L1804
	add	w1, w1, 1
	str	w1, [x20,4]
	b	.L1804
.L1816:
	ldr	w1, [x1,4]
	tbz	w1, #31, .L1809
	bl	flush_l2p_region
	b	.L1809
.L1810:
	ldr	x0, [x19,2808]
	mov	w1, 1
	add	x23, x19, 2952
	mov	w2, w1
	add	x0, x0, x26
	str	w24, [x19,2956]
	ldr	x0, [x0,8]
	str	x0, [x19,2960]
	ldr	x0, [x19,2656]
	str	x0, [x19,2968]
	mov	x0, x23
	bl	FlashReadPages
	ldr	x0, [x19,2968]
	ldrh	w0, [x0,8]
	cmp	w0, w22
	beq	.L1811
	adrp	x0, .LC40
	uxth	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC40
	bl	printk
	mov	w2, 4
	ldr	x1, [x19,2968]
	adrp	x0, .LC17
	mov	w3, w2
	add	x0, x0, :lo12:.LC17
	bl	rknand_print_hex
	adrp	x0, .LC41
	ldr	x1, [x19,2784]
	ldrh	w3, [x19,2396]
	add	x0, x0, :lo12:.LC41
	mov	w2, 4
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x19,2944]
	b	.L1812
.L1811:
	ldr	w0, [x19,2952]
	cmp	w0, 256
	bne	.L1812
	and	w28, w28, 65535
	adrp	x0, .LC42
	mov	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC42
	bl	printk
	ldr	x2, [x19,2808]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, w28
	add	x2, x2, x26
	add	x0, x0, 944
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1812:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,2808]
	add	x0, x0, x26
	str	wzr, [x0,4]
	ldr	x0, [x1,2808]
	strh	w22, [x0,x26]
	b	.L1805
.L1804:
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
	ldrh	w22, [x0,2368]
	mov	x26, x2
	ldrh	w0, [x0,2356]
	mov	w20, -1
	cmp	w1, w0
	bhi	.L1818
	adrp	x27, .LANCHOR4
	lsr	w22, w23, w22
	add	x27, x27, :lo12:.LANCHOR4
	mov	w20, 0
	add	x27, x27, 1016
.L1819:
	cbz	w21, .L1818
	add	x2, x24, :lo12:.LANCHOR2
	ldrh	w1, [x2,2366]
	ldr	x0, [x2,2776]
	udiv	w25, w23, w1
	ldr	w3, [x0,w22,uxtw 2]
	uxth	w0, w21
	msub	w25, w25, w1, w23
	sub	w19, w1, w25
	uxth	w19, w19
	cmp	w19, w21
	csel	w19, w0, w19, hi
	cmp	w19, w1
	beq	.L1821
	cbz	w3, .L1821
	ldr	x0, [x2,2608]
	mov	w1, 1
	str	x0, [x29,128]
	mov	w2, w1
	add	x0, x29, 120
	str	w3, [x29,124]
	str	xzr, [x29,136]
	bl	FlashReadPages
	b	.L1822
.L1821:
	add	x2, x24, :lo12:.LANCHOR2
	mov	w1, 0
	ldr	x0, [x2,2608]
	ldrh	w2, [x2,2370]
	bl	ftl_memset
.L1822:
	add	x3, x24, :lo12:.LANCHOR2
	lsl	w28, w19, 9
	ubfiz	x25, x25, 9, 16
	mov	x1, x26
	mov	w2, w28
	str	x3, [x29,104]
	ldr	x0, [x3,2608]
	sub	w21, w21, w19
	add	w23, w23, w19
	add	x26, x26, x28, sxtw
	add	x0, x0, x25
	bl	ftl_memcpy
	ldr	x3, [x29,104]
	mov	w1, w22
	mov	x0, x27
	add	w22, w22, 1
	ldr	x2, [x3,2608]
	bl	FtlMapWritePage
	cmn	w0, #1
	csinv	w20, w20, wzr, ne
	b	.L1819
.L1818:
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
	ldrh	w1, [x0,2356]
	ldrh	w20, [x0,2368]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L1833
	adrp	x0, .LC43
	adrp	x26, .LANCHOR4
	add	x0, x0, :lo12:.LC43
	add	x26, x26, :lo12:.LANCHOR4
	str	x0, [x29,120]
	lsr	w20, w23, w20
	add	x0, x26, 1016
	mov	w21, 0
	str	x0, [x29,112]
.L1834:
	cbz	w22, .L1842
	add	x26, x25, :lo12:.LANCHOR2
	ldrh	w19, [x26,2366]
	ldr	x0, [x26,2776]
	udiv	w24, w23, w19
	ldr	w4, [x0,w20,uxtw 2]
	uxth	w0, w22
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w28, w19, 9
	cbz	w4, .L1836
	ldr	x0, [x26,2608]
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
	ldr	w0, [x26,2952]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L1838
	ldr	x0, [x29,120]
	mov	w1, w20
	mov	w2, w4
	bl	printk
	ldr	x0, [x29,112]
	mov	w1, w20
	ldr	x2, [x26,2608]
	bl	FtlMapWritePage
.L1838:
	add	x0, x25, :lo12:.LANCHOR2
	ubfiz	x24, x24, 9, 16
	mov	w2, w28
	ldr	x1, [x0,2608]
	mov	x0, x27
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L1839
.L1836:
	mov	x0, x27
	mov	w1, w4
	mov	w2, w28
	bl	ftl_memset
.L1839:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x27, x27, x28, sxtw
	b	.L1834
.L1842:
	mov	w0, w21
.L1833:
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
	ldrh	w2, [x3,1080]
	cbnz	w0, .L1844
	cmp	w2, 4
	bhi	.L1844
	add	w2, w2, 1
	strh	w2, [x3,1080]
	b	.L1843
.L1844:
	add	x0, x1, :lo12:.LANCHOR4
	mov	w7, 65535
	strh	wzr, [x0,1080]
	str	wzr, [x0,1084]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w5, [x0,2300]
	ldr	x6, [x0,2728]
	mov	x0, 0
.L1845:
	cmp	w5, w0, uxth
	bls	.L1843
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L1846
	add	x3, x1, :lo12:.LANCHOR4
	ldr	w2, [x3,1084]
	add	w2, w4, w2
	str	w2, [x3,1084]
.L1846:
	add	x0, x0, 1
	b	.L1845
.L1843:
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
	cbz	w1, .L1849
	adrp	x1, .LANCHOR2
	str	wzr, [x0,56]
	add	x2, x1, :lo12:.LANCHOR2
	mov	x21, x1
	ldr	w3, [x2,2944]
	cbnz	w3, .L1849
	mov	x20, x0
	sub	w19, w19, #1
	add	x0, x2, 2952
	ldr	x3, [x2,2656]
	uxth	w19, w19
	ldr	x1, [x2,2600]
	str	x3, [x0,16]
	ubfiz	x3, x19, 2, 16
	str	x1, [x0,8]
	ldr	w3, [x4,x3]
	str	w3, [x0,4]
	cbz	w3, .L1853
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1854
.L1853:
	ldrh	w2, [x2,2370]
	mov	x0, x1
	mov	w1, 255
	bl	ftl_memset
.L1854:
	add	x2, x21, :lo12:.LANCHOR2
	mov	x0, x20
	mov	w1, w19
	ldr	x2, [x2,2960]
	bl	FtlMapWritePage
.L1849:
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
	ldr	w1, [x19,2944]
	cbnz	w1, .L1857
	ldr	x0, [x19,2592]
	adrp	x25, .LANCHOR4
	str	x0, [x19,2960]
	add	x0, x25, :lo12:.LANCHOR4
	ldr	x21, [x19,2656]
	add	x20, x0, 712
	str	x21, [x19,2968]
	adrp	x23, .LANCHOR0
	ldrh	w3, [x0,1088]
	strh	w3, [x21,2]
	mov	w3, -3932
	strh	w3, [x21]
	str	w1, [x21,12]
	ldr	w3, [x0,1096]
	str	w1, [x21,8]
	mov	w1, 19539
	movk	w1, 0x4654, lsl 16
	str	w3, [x21,4]
	ldrh	w2, [x0,800]
	str	w1, [x0,712]
	mov	w1, 96
	strh	w2, [x20,14]
	movk	w1, 0x5000, lsl 16
	ldrh	w2, [x0,802]
	ldrb	w3, [x0,806]
	str	w1, [x20,4]
	ldrh	w1, [x0,1094]
	orr	w2, w3, w2, lsl 6
	strh	w1, [x20,8]
	ldrh	w1, [x19,2314]
	strb	w1, [x20,10]
	ldrb	w1, [x0,808]
	strh	w2, [x20,16]
	strb	w1, [x20,11]
	ldrh	w2, [x0,848]
	ldrb	w1, [x0,856]
	strb	w1, [x20,12]
	add	x1, x0, 896
	strh	w2, [x20,18]
	ldrh	w2, [x0,850]
	ldrb	w3, [x0,854]
	ldrh	w0, [x0,896]
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
	ldr	w0, [x19,2444]
	str	w0, [x20,32]
	ldr	w0, [x19,2412]
	str	w0, [x20,40]
	ldr	w0, [x19,2416]
	str	w0, [x20,36]
	ldrh	w0, [x19,2488]
	strh	w0, [x20,44]
	ldrh	w0, [x19,2490]
	strh	w0, [x20,46]
	ldrh	w2, [x19,2370]
	ldr	x0, [x19,2960]
	bl	ftl_memset
	ldr	x0, [x19,2960]
	mov	x1, x20
	mov	w2, 48
	mov	x20, x25
	bl	ftl_memcpy
	ldr	x0, [x19,2960]
	ldrh	w2, [x19,2300]
	ldr	x1, [x19,2728]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,2300]
	add	x1, x23, :lo12:.LANCHOR0
	ldr	x3, [x19,2960]
	uxth	x0, w2
	ldr	x1, [x1,16]
	add	x0, x0, 24
	lsr	w2, w2, 3
	lsr	x0, x0, 1
	add	w2, w2, 4
	add	x0, x3, x0, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x19,2400]
	cbz	w0, .L1858
	ldrh	w0, [x19,2300]
	ldrh	w2, [x19,2396]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19,2960]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,2784]
	bl	ftl_memcpy
.L1858:
	mov	w0, 0
	mov	w24, 0
	mov	w25, 65535
	add	x19, x22, :lo12:.LANCHOR2
	add	x20, x20, :lo12:.LANCHOR4
	bl	FtlUpdateVaildLpn
.L1859:
	ldrh	w1, [x20,1088]
	ldr	x0, [x19,2592]
	ldrh	w2, [x20,1090]
	str	x0, [x19,2960]
	ldr	x0, [x19,2656]
	str	x0, [x19,2968]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,2956]
	ldrh	w0, [x19,2362]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1860
	ldrh	w25, [x20,1092]
	strh	wzr, [x20,1090]
	strh	w1, [x20,1092]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,2412]
	str	w1, [x20,1096]
	add	w2, w1, 1
	str	w2, [x19,2412]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19,2956]
	strh	w0, [x20,1088]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L1860:
	add	x0, x23, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L1861
	ldr	x0, [x19,2592]
	ldrh	w1, [x19,2370]
	bl	js_hash
	str	w0, [x21,12]
.L1861:
	mov	w1, 1
	add	x0, x19, 2952
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x20,1090]
	ldr	w2, [x19,2952]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x20,1090]
	bne	.L1862
	cmp	w1, 1
	bne	.L1863
	ldrh	w1, [x19,2362]
	sub	w1, w1, #1
	strh	w1, [x20,1090]
.L1863:
	add	w24, w24, 1
	uxth	w24, w24
	cmp	w24, 3
	bls	.L1859
	add	x22, x22, :lo12:.LANCHOR2
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w24
	ldr	w1, [x22,2956]
	bl	printk
	mov	w0, 1
	str	w0, [x22,2944]
	b	.L1857
.L1862:
	cmp	w1, 1
	beq	.L1859
	cmp	w2, 256
	beq	.L1859
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1857
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1857:
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
	strh	wzr, [x20,2936]
	ldr	w2, [x20,2388]
	mov	x24, x22
	ldr	x0, [x20,2800]
	mov	w22, 65535
	lsl	w2, w2, 2
	strh	wzr, [x21,1104]
	bl	ftl_memset
	ldr	w2, [x20,2388]
	mov	w1, 0
	ldr	x0, [x20,2736]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,2380]
	mov	w1, 0
	ldr	x0, [x20,2768]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2380]
	mov	w1, 0
	ldr	x0, [x20,2752]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x21, 1088
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,2300]
.L1873:
	add	x1, x24, :lo12:.LANCHOR2
	ldrh	w0, [x1,2302]
	cmp	w0, w21
	bls	.L1914
	mov	x27, 0
	ldrh	w4, [x1,2292]
	ldrh	w2, [x1,2372]
	mov	w20, w27
	mov	x23, x1
.L1915:
	cmp	w4, w27, uxth
	bls	.L1948
	add	x0, x23, 2320
	mov	w1, w21
	str	x2, [x29,112]
	str	x4, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x4, [x29,120]
	ldr	x2, [x29,112]
	cbnz	w0, .L1874
	mov	w0, 56
	ldr	x1, [x23,2536]
	lsl	w28, w28, 10
	mov	w3, 4
	umull	x0, w20, w0
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x23,2536]
	ldr	x5, [x23,2672]
	add	x0, x1, x0
	ldr	x1, [x23,2616]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w3
	uxth	w20, w20
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L1874:
	add	x27, x27, 1
	b	.L1915
.L1948:
	cbz	w20, .L1877
	add	x0, x24, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	mov	w28, 61604
	ldr	x0, [x0,2536]
	bl	FlashReadPages
.L1878:
	cmp	w20, w23, uxth
	bls	.L1877
	mov	x0, 56
	mul	x6, x23, x0
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	add	x1, x0, x6
	ldr	w0, [x0,x6]
	ldr	w5, [x1,4]
	cmn	w0, #1
	ldr	x26, [x1,16]
	ubfx	x25, x5, 10, 16
	bne	.L1880
	mov	w7, 16
.L1879:
	add	x8, x24, :lo12:.LANCHOR2
	str	x7, [x29,104]
	str	x8, [x29,112]
	str	x6, [x29,120]
	ldr	x0, [x8,2536]
	add	x0, x0, x6
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x8,2536]
	mov	w2, w1
	add	x0, x0, x6
	bl	FlashReadPages
	ldrh	w0, [x26]
	ldr	x8, [x29,112]
	cmp	w0, w22
	ldr	x6, [x29,120]
	ldr	x7, [x29,104]
	ldr	x0, [x8,2536]
	bne	.L1881
	str	w27, [x0,x6]
	b	.L1880
.L1881:
	ldr	w0, [x0,x6]
	cmn	w0, #1
	bne	.L1880
	sub	w7, w7, #1
	uxth	w7, w7
	cbnz	w7, .L1879
.L1880:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x1, [x0,2536]
	ldr	w1, [x1,x6]
	cmn	w1, #1
	beq	.L1883
	ldr	w0, [x0,2412]
	cmn	w0, #1
	beq	.L1884
	ldr	w1, [x26,4]
	cmp	w0, w1
	bhi	.L1885
.L1884:
	ldr	w0, [x26,4]
	cmn	w0, #1
	beq	.L1885
	add	x1, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,2412]
.L1885:
	ldrh	w0, [x26]
	cmp	w0, w28
	beq	.L1887
	bhi	.L1888
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1886
	add	x6, x24, :lo12:.LANCHOR2
	ldrh	w7, [x6,2380]
	ldrh	w0, [x6,2936]
	sub	w2, w7, #1
	ldr	x6, [x6,2768]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1901
.L1888:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1890
	cmp	w0, w22
	bne	.L1886
	mov	w0, w25
	b	.L1947
.L1890:
	add	x7, x24, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w6, [x7,2388]
	ldrh	w0, [x0,1104]
	ldr	x7, [x7,2800]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1892:
	cmp	w1, w2
	ble	.L1949
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x7,x8]
	cmp	w11, w10
	bls	.L1893
	ldr	w2, [x7]
	cbnz	w2, .L1894
	cmp	w0, w6
	beq	.L1894
	add	x2, x19, :lo12:.LANCHOR4
	add	w0, w0, 1
	strh	w0, [x2,1104]
.L1894:
	mov	w0, 0
.L1895:
	cmp	w0, w1
	beq	.L1950
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2800]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2736]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1895
.L1950:
	add	x2, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x0, [x2,2800]
	str	w6, [x0,x8]
	ldr	x0, [x2,2736]
	strh	w25, [x0,x9,lsl 1]
	tbz	w1, #31, .L1897
	b	.L1886
.L1893:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1892
.L1949:
	tbz	w1, #31, .L1930
	b	.L1886
.L1897:
	add	x0, x19, :lo12:.LANCHOR4
	ldr	w2, [x2,2388]
	ldrh	w0, [x0,1104]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1886
.L1930:
	add	x2, x19, :lo12:.LANCHOR4
	add	w0, w0, 1
	ldr	w4, [x26,4]
	strh	w0, [x2,1104]
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x2, [x0,2800]
	str	w4, [x2,x1,lsl 2]
	ldr	x0, [x0,2736]
	b	.L1946
.L1907:
	sbfiz	x8, x1, 2, 32
	ldr	w11, [x26,4]
	sxth	x9, w1
	ldr	w10, [x6,x8]
	cmp	w11, w10
	bhi	.L1951
	sub	w1, w1, #1
	sxth	x1, w1
.L1901:
	cmp	w1, w2
	bgt	.L1907
	b	.L1906
.L1951:
	ldr	w2, [x6]
	cbnz	w2, .L1903
	cmp	w0, w7
	beq	.L1903
	add	x2, x24, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,2936]
.L1903:
	mov	w0, 0
.L1904:
	cmp	w0, w1
	beq	.L1952
	add	x6, x24, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x7, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x10, [x6,2768]
	sxth	w0, w0
	add	x11, x10, x7
	ldr	w11, [x11,4]
	str	w11, [x10,x7]
	ldr	x6, [x6,2752]
	add	x7, x6, x2
	ldrh	w7, [x7,2]
	strh	w7, [x6,x2]
	b	.L1904
.L1952:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w6, [x26,4]
	ldr	x2, [x0,2768]
	str	w6, [x2,x8]
	ldr	x0, [x0,2752]
	strh	w25, [x0,x9,lsl 1]
.L1906:
	tbnz	w1, #31, .L1886
	add	x0, x24, :lo12:.LANCHOR2
	ldrh	w2, [x0,2380]
	ldrh	w6, [x0,2936]
	sub	w2, w2, #1
	sub	w2, w2, w6
	cmp	w1, w2, sxth
	bgt	.L1886
	ldr	x2, [x0,2768]
	add	w6, w6, 1
	ldr	w4, [x26,4]
	strh	w6, [x0,2936]
	str	w4, [x2,x1,lsl 2]
	ldr	x0, [x0,2752]
.L1946:
	strh	w25, [x0,x1,lsl 1]
	b	.L1886
.L1887:
	add	x0, x19, :lo12:.LANCHOR4
	add	x1, x0, 1088
	ldrh	w2, [x0,1088]
	cmp	w2, w22
	bne	.L1908
	strh	w25, [x0,1088]
	ldr	w0, [x26,4]
	str	w0, [x1,8]
	b	.L1886
.L1908:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1909
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1909:
	add	x1, x19, :lo12:.LANCHOR4
	ldr	w2, [x26,4]
	add	x0, x1, 1088
	ldr	w6, [x0,8]
	cmp	w6, w2
	bcs	.L1910
	ldrh	w2, [x1,1088]
	strh	w2, [x0,4]
	strh	w25, [x1,1088]
	ldr	w1, [x26,4]
	str	w1, [x0,8]
	b	.L1886
.L1910:
	strh	w25, [x0,4]
	b	.L1886
.L1883:
	ldrb	w1, [x0,1228]
	mov	w0, w25
	cbz	w1, .L1911
.L1947:
	mov	w1, 0
.L1911:
	bl	FtlFreeSysBlkQueueIn
.L1886:
	add	x23, x23, 1
	b	.L1878
.L1877:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1873
.L1914:
	ldr	x2, [x1,2736]
	ldrh	w0, [x2]
	cbz	w0, .L1916
.L1919:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	x2, [x1,2752]
	ldrh	w0, [x2]
	cbz	w0, .L1917
	b	.L1940
.L1916:
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w4, [x19,1104]
	cbz	w4, .L1919
	ldr	w1, [x1,2388]
.L1920:
	cmp	w0, w1
	bcs	.L1919
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1921
	mov	w7, w0
.L1922:
	add	x1, x24, :lo12:.LANCHOR2
	ldr	w2, [x1,2388]
	cmp	w0, w2
	bcs	.L1919
	sxtw	x6, w0
	sub	w2, w0, w7
	ldr	x5, [x1,2736]
	lsl	x4, x6, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x2,lsl 1]
	ldr	x5, [x1,2800]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x2,lsl 2]
	ldr	x1, [x1,2736]
	strh	wzr, [x1,x4]
	b	.L1922
.L1921:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1920
.L1917:
	ldrh	w4, [x1,2936]
	cbz	w4, .L1940
	ldrh	w1, [x1,2380]
.L1925:
	cmp	w0, w1
	mov	w7, w0
	bge	.L1940
	ldrh	w4, [x2,w0,sxtw 1]
	cbz	w4, .L1926
.L1927:
	add	x1, x24, :lo12:.LANCHOR2
	ldrh	w2, [x1,2380]
	cmp	w0, w2
	bge	.L1940
	sxtw	x6, w0
	sub	w2, w0, w7
	ldr	x5, [x1,2752]
	lsl	x4, x6, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w8, [x5,x4]
	sxth	w0, w0
	strh	w8, [x5,x2,lsl 1]
	ldr	x5, [x1,2768]
	ldr	w6, [x5,x6,lsl 2]
	str	w6, [x5,x2,lsl 2]
	ldr	x1, [x1,2752]
	strh	wzr, [x1,x4]
	b	.L1927
.L1926:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1925
.L1940:
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
	ldrh	w1, [x19,2688]
	ldr	x2, [x19,2704]
	bl	FtlVendorPartRead
	ldr	x0, [x19,2704]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1954
	adrp	x0, .LC45
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	add	x0, x0, :lo12:.LC45
	bl	printk
	ldrh	w2, [x19,2688]
	mov	w1, 0
	ldr	x0, [x19,2704]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1954:
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
	cbz	w1, .L1956
	bl	ftl_set_blk_mode.part.17
	b	.L1955
.L1956:
	adrp	x1, .LANCHOR0+16
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L1955:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	mov	w20, 0
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	add	x21, x19, :lo12:.LANCHOR4
	bl	printk
	add	x0, x21, 1112
	mov	w1, 0
	mov	x2, 8192
	adrp	x23, .LANCHOR2
	bl	memset
.L1960:
	add	x0, x23, :lo12:.LANCHOR2
	adrp	x22, .LANCHOR2
	ldr	w0, [x0,2940]
	cmp	w20, w0
	bcs	.L1977
	mov	w0, w20
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L1961
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x21, 1112
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L1961:
	add	w20, w20, 1
	b	.L1960
.L1977:
	adrp	x21, .LC48
	mov	w20, 0
	add	x23, x19, :lo12:.LANCHOR4
	add	x21, x21, :lo12:.LC48
.L1963:
	add	x2, x22, :lo12:.LANCHOR2
	ldrh	w0, [x2,2300]
	cmp	w0, w20
	bls	.L1978
	ubfiz	x0, x20, 1, 16
	ldr	x1, [x2,2728]
	ldrh	w2, [x1,x0]
	add	x0, x23, 1112
	ldrh	w3, [x0,w20,sxtw 1]
	cmp	w2, w3
	beq	.L1964
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L1964:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1963
.L1978:
	add	x1, x19, :lo12:.LANCHOR4
	ldr	x0, [x1,784]
	cbz	x0, .L1959
	ldr	x19, [x2,2824]
	adrp	x23, .LC49
	ldrh	w24, [x1,792]
	mov	w21, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x23, x23, :lo12:.LC49
	mov	x20, x1
	mov	w25, 6
	madd	x19, x0, x19, x19
	mov	w26, 65535
	uxth	w19, w19
.L1967:
	cmp	w21, w24
	beq	.L1959
	add	x1, x22, :lo12:.LANCHOR2
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x1,2728]
	ldrh	w2, [x1,x0]
	cbz	w2, .L1968
	add	x3, x20, 1112
	mov	x0, x23
	mov	w1, w19
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L1968:
	add	x0, x22, :lo12:.LANCHOR2
	umull	x19, w19, w25
	ldr	x0, [x0,2824]
	ldrh	w19, [x0,x19]
	cmp	w19, w26
	beq	.L1959
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1967
.L1959:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	add	x19, x2, 2952
	uxth	w23, w0
	adrp	x22, .LC50
	ldr	x0, [x2,2592]
	adrp	x25, .LC51
	str	x0, [x19,8]
	lsl	w24, w23, 10
	ldr	x0, [x2,2656]
	mov	w20, 0
	str	x0, [x19,16]
	mov	x21, x2
	add	x22, x22, :lo12:.LC50
	add	x25, x25, :lo12:.LC51
.L1980:
	ldrh	w0, [x21,2362]
	cmp	w20, w0
	bge	.L1986
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
	beq	.L1981
	ldr	x1, [x21,2592]
	mov	x0, x25
	mov	w2, 4
	mov	w3, 768
	bl	rknand_print_hex
.L1981:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1980
.L1986:
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
	adrp	x0, .LC52
	add	x0, x0, :lo12:.LC52
	mov	w1, 0
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x23, x24, [sp,64]
	adrp	x23, .LANCHOR2
	stp	x21, x22, [sp,48]
	adrp	x21, .LC53
	add	x22, x23, :lo12:.LANCHOR2
	stp	x19, x20, [sp,32]
	add	x21, x21, :lo12:.LC53
	mov	w20, 0
	add	x19, x22, 2952
	bl	printk
.L1988:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,2940]
	cmp	w20, w0
	bcs	.L1995
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L1989
	ldr	w2, [x29,76]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L1989:
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L1991
	str	w0, [x19,4]
	mov	w1, 1
	ldr	x0, [x22,2592]
	mov	w2, 0
	str	x0, [x19,8]
	mov	x0, x19
	ldr	x24, [x22,2656]
	str	w20, [x19,24]
	str	x24, [x19,16]
	str	wzr, [x19]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1992
	cmp	w0, 256
	beq	.L1992
	ldr	w0, [x24,8]
	cmp	w0, w20
	beq	.L1991
.L1992:
	ldr	x7, [x19,8]
	mov	w1, w20
	ldr	x6, [x19,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	adrp	x0, .LC54
	add	x0, x0, :lo12:.LC54
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w2, [x19,4]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
.L1991:
	add	w20, w20, 1
	b	.L1988
.L1995:
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
	adrp	x24, .LC55
	mov	w25, 56
	ldrh	w22, [x0,2300]
	mov	w26, 4
	mov	x27, 56
	add	x24, x24, :lo12:.LC55
.L1997:
	add	x20, x19, :lo12:.LANCHOR2
	ldrh	w0, [x20,2302]
	cmp	w0, w22
	bls	.L2010
	mov	x23, 0
	ldrh	w3, [x20,2292]
	ldrh	w2, [x20,2372]
	mov	w21, w23
.L2006:
	cmp	w3, w23, uxth
	bls	.L2011
	add	x0, x20, 2320
	mov	w1, w22
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,x23]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L1998
	umull	x0, w21, w25
	ldr	x1, [x20,2536]
	lsl	w28, w28, 10
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x20,2536]
	ldr	x4, [x20,2672]
	add	x0, x1, x0
	ldr	x1, [x20,2616]
	str	x1, [x0,8]
	mul	w1, w21, w2
	add	w21, w21, 1
	sdiv	w1, w1, w26
	uxth	w21, w21
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1998:
	add	x23, x23, 1
	b	.L2006
.L2011:
	cbz	w21, .L2001
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w21
	mov	w2, 1
	mov	x20, 0
	ldr	x0, [x0,2536]
	bl	FlashReadPages
.L2002:
	cmp	w21, w20, uxth
	bls	.L2001
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
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
	b	.L2002
.L2001:
	add	w22, w22, 1
	uxth	w22, w22
	b	.L1997
.L2010:
	adrp	x23, .LC56
	mov	w22, 0
	adrp	x26, .LANCHOR4
	add	x25, x20, 2952
	add	x23, x23, :lo12:.LC56
.L2005:
	add	x1, x26, :lo12:.LANCHOR4
	ldrh	w1, [x1,1104]
	cmp	w22, w1
	bge	.L2008
	sbfiz	x24, x22, 1, 32
	mov	w21, 0
.L2009:
	ldrh	w0, [x20,2362]
	cmp	w21, w0
	bge	.L2012
	ldr	x0, [x20,2736]
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
	ldr	x0, [x20,2736]
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
	b	.L2009
.L2012:
	add	w22, w22, 1
	sxth	w22, w22
	b	.L2005
.L2008:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC57
	add	x0, x0, :lo12:.LC57
	mov	w2, 2
	ldr	x1, [x19,2736]
	ldr	w3, [x19,2388]
	bl	rknand_print_hex
	ldr	x1, [x19,2784]
	adrp	x0, .LC58
	ldrh	w3, [x19,2396]
	add	x0, x0, :lo12:.LC58
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x19,2792]
	adrp	x0, .LC59
	ldrh	w3, [x19,2396]
	add	x0, x0, :lo12:.LC59
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
	add	x4, x6, 2952
	str	wzr, [x27,48]
	mov	x20, x6
	mov	x25, x4
	ldr	x0, [x6,2592]
	ldr	x21, [x6,2656]
	str	x0, [x6,2960]
	mov	w0, -1
	str	x21, [x6,2968]
	strh	w0, [x27]
	strh	w0, [x27,2]
	mov	w0, 1
	str	w0, [x27,56]
	ldr	w0, [x29,136]
	sub	w0, w0, #1
	str	w0, [x29,116]
.L2014:
	ldr	w0, [x29,136]
	cmp	w19, w0
	bge	.L2032
	ldr	w0, [x29,116]
	sxtw	x28, w19
	cmp	w19, w0
	bne	.L2015
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
	add	x28, x23, 2952
.L2016:
	cmp	w20, w19
	bge	.L2032
	ldrh	w0, [x25]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w20, w0, lsl 10
	str	w0, [x28,4]
	mov	x0, x28
	bl	FlashReadPages
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2017
	ldr	x0, [x28,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2017
	ldr	x0, [x28,8]
	ldrh	w1, [x23,2370]
	str	x2, [x29,136]
	bl	js_hash
	ldr	x2, [x29,136]
	cmp	w2, w0
	beq	.L2017
	mov	w0, -1
	str	w0, [x28]
.L2017:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L2018
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2018
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2018
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28,4]
	str	w1, [x22,x0]
.L2018:
	add	w4, w20, 1
	sxth	w20, w4
	b	.L2016
.L2032:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	ldr	x0, [x29,128]
	ldrh	w1, [x27,2]
	add	x20, x0, :lo12:.LANCHOR2
	ldrh	w0, [x20,2362]
	cmp	w1, w0
	bne	.L2021
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2021
.L2015:
	ldr	x0, [x20,2592]
	str	x0, [x25,8]
	lsl	x0, x28, 1
	add	x1, x23, x0
	str	x1, [x29,120]
	ldrh	w1, [x23,x0]
	ldrh	w0, [x20,2362]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x25,4]
	mov	w2, w1
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2033
	ldrh	w1, [x21]
	mov	w28, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2054
	ldrh	w1, [x21,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2054
	mov	w5, w28
.L2023:
	ldrh	w0, [x20,2362]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2026
	ldr	x2, [x20,2592]
	sbfiz	x1, x5, 3, 32
	ldrh	w0, [x2,x1]
	cmp	w0, w24
	bcs	.L2024
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x1,4]
	str	w1, [x22,x0]
.L2024:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2023
.L2033:
	mov	w28, 0
.L2054:
	ldrh	w0, [x20,2362]
	cmp	w28, w0
	bge	.L2026
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
	cbz	w0, .L2027
	ldr	x0, [x25,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2027
	ldr	x0, [x25,8]
	ldrh	w1, [x20,2370]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2027
	mov	w0, -1
	str	w0, [x25]
.L2027:
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2028
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2028
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2028
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25,4]
	str	w1, [x22,x0]
.L2028:
	add	w5, w28, 1
	sxth	w28, w5
	b	.L2054
.L2026:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2014
.L2021:
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
	add	x0, x0, 1016
	add	x29, sp, 0
	ldrh	w2, [x1,2380]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,2936]
	strh	w2, [x0,8]
	ldrh	w2, [x1,2382]
	strh	w2, [x0,6]
	ldr	x2, [x1,2752]
	str	x2, [x0,16]
	ldr	x2, [x1,2768]
	str	x2, [x0,24]
	ldr	x2, [x1,2760]
	ldr	x1, [x1,2776]
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
	ldr	w2, [x19,2388]
	ldr	x0, [x19,2744]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x19,2370]
	mov	w1, 255
	ldrh	w2, [x19,2398]
	ldr	x0, [x19,2816]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w2, 0
	mov	w4, -1
.L2057:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,2398]
	cmp	w1, w2
	bls	.L2059
	ldr	x3, [x0,2808]
	ubfiz	x1, x2, 4, 16
	add	x3, x3, x1
	str	wzr, [x3,4]
	ldr	x3, [x0,2808]
	strh	w4, [x3,x1]
	ldr	x3, [x0,2808]
	add	x3, x3, x1
	ldrh	w1, [x0,2370]
	ldr	x0, [x0,2816]
	mul	w1, w1, w2
	add	w2, w2, 1
	sxtw	x1, w1
	uxth	w2, w2
	and	x1, x1, -4
	add	x0, x0, x1
	str	x0, [x3,8]
	b	.L2057
.L2059:
	adrp	x2, .LANCHOR4
	mov	w3, -1
	add	x2, x2, :lo12:.LANCHOR4
	add	x1, x2, 944
	strh	w3, [x2,946]
	strh	w3, [x2,944]
	ldr	w3, [x0,2388]
	strh	w3, [x2,954]
	mov	w3, -3902
	strh	w3, [x2,948]
	ldrh	w2, [x2,1104]
	strh	w2, [x1,8]
	ldrh	w2, [x0,2396]
	strh	w2, [x1,6]
	ldr	x2, [x0,2736]
	str	x2, [x1,16]
	ldr	x2, [x0,2800]
	str	x2, [x1,24]
	ldr	x2, [x0,2744]
	ldr	x0, [x0,2784]
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
	add	x0, x0, 944
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
	ldrh	w4, [x4,2292]
.L2062:
	cmp	w2, w4
	bcs	.L2068
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L2063
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L2063:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2062
.L2068:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,2360]
	mov	x3, x0
.L2065:
	cmp	x3, x4
	beq	.L2069
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L2066
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L2066:
	add	x3, x3, 2
	b	.L2065
.L2069:
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
	ldr	x3, [x4,2728]
	ldrh	w1, [x3,x23]
	cbnz	w1, .L2071
	adrp	x21, .LANCHOR4
	add	x2, x21, :lo12:.LANCHOR4
	ldr	x19, [x2,784]
	cbz	x19, .L2072
	ldrh	w3, [x2,792]
	mov	w5, 65535
	ldr	x2, [x4,2824]
	mov	x4, -6148914691236517206
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2073:
	cmp	w1, w3
	beq	.L2072
	cmp	w19, w0
	bne	.L2074
	add	x21, x21, :lo12:.LANCHOR4
	mov	w1, w19
	add	x0, x21, 784
	bl	List_remove_node
	ldrh	w0, [x21,792]
	sub	w0, w0, #1
	strh	w0, [x21,792]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	add	x2, x22, :lo12:.LANCHOR2
	ldr	x1, [x2,2728]
	ldrh	w0, [x1,x23]
	add	w0, w0, 1
	strh	w0, [x1,x23]
	b	.L2072
.L2074:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2072
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2073
.L2071:
	add	w1, w1, 1
	strh	w1, [x3,x23]
.L2072:
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
	ldrh	w24, [x1,2292]
.L2080:
	cmp	w24, w21, uxth
	bls	.L2091
	add	x0, x23, 2320
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2081
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L2081:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L2080
.L2091:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x0,2360]
	mul	w1, w1, w2
	strh	w1, [x19,4]
	ldr	w1, [x0,2196]
	cbz	w1, .L2083
	ldrh	w1, [x19]
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L2083
	mov	w0, 1
	strb	w0, [x19,9]
.L2083:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L2084
	mov	w0, 1
	strb	w0, [x19,9]
.L2084:
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
	add	x20, x23, 2952
	adrp	x19, .LANCHOR4
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	ldr	x0, [x23,2592]
	add	x24, x19, :lo12:.LANCHOR4
	str	x0, [x20,8]
	add	x25, x24, 1088
	ldr	x0, [x23,2656]
	ldrh	w2, [x23,2300]
	str	x0, [x20,16]
	ldr	x0, [x23,2728]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w0, [x24,1088]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2093
.L2104:
	mov	w19, -1
	b	.L2094
.L2093:
	mov	w1, 1
	adrp	x26, .LC60
	add	x26, x26, :lo12:.LC60
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x25,2]
	ldrsh	w25, [x24,1088]
.L2095:
	tbnz	w21, #31, .L2103
	orr	w0, w21, w25, lsl 10
	mov	w1, 1
	str	w0, [x20,4]
	mov	w2, w1
	ldr	x0, [x23,2592]
	str	x0, [x20,8]
	mov	x0, x20
	bl	FlashReadPages
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2096
	ldr	x24, [x20,16]
	ldr	w7, [x24,12]
	cbz	w7, .L2096
	ldr	x28, [x20,8]
	ldrh	w1, [x23,2370]
	mov	x0, x28
	str	x7, [x29,104]
	bl	js_hash
	ldr	x7, [x29,104]
	cmp	w7, w0
	beq	.L2096
	cbnz	w21, .L2097
	add	x0, x19, :lo12:.LANCHOR4
	add	x27, x0, 1088
	ldrh	w1, [x0,1092]
	cmp	w25, w1
	beq	.L2097
	ldrh	w1, [x0,1088]
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
	ldrh	w21, [x23,2362]
	b	.L2099
.L2097:
	mov	w0, -1
	str	w0, [x20]
.L2096:
	ldr	w0, [x20]
	cmn	w0, #1
	beq	.L2099
	ldr	x0, [x23,2592]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L2099
	ldr	x0, [x23,2656]
	ldrh	w1, [x0]
	mov	w0, 61604
	cmp	w1, w0
	bne	.L2099
.L2103:
	add	x20, x22, :lo12:.LANCHOR2
	add	x0, x19, :lo12:.LANCHOR4
	add	x21, x20, 2952
	mov	w2, 48
	add	x0, x0, 712
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x20,2300]
	ldr	x0, [x20,2728]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x20,2300]
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldr	x3, [x21,8]
	uxth	x1, w2
	lsr	w2, w2, 3
	add	x1, x1, 24
	ldr	x0, [x0,16]
	lsr	x1, x1, 1
	add	w2, w2, 4
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x20,2400]
	cbz	w0, .L2101
	ldrh	w0, [x20,2300]
	ldr	x3, [x21,8]
	lsr	w1, w0, 3
	ldrh	w2, [x20,2396]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x20,2792]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L2101
.L2099:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L2095
.L2101:
	add	x6, x19, :lo12:.LANCHOR4
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	ldr	w2, [x6,712]
	cmp	w2, w0
	bne	.L2104
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w3, [x6,722]
	ldrh	w5, [x6,720]
	strh	w5, [x6,1094]
	ldrh	w2, [x0,2314]
	cmp	w3, w2
	bne	.L2104
	ldrh	w3, [x0,2360]
	adrp	x7, .LANCHOR5
	ldrh	w2, [x0,2366]
	add	x4, x7, :lo12:.LANCHOR5
	mov	x20, x7
	mul	w3, w3, w5
	str	w3, [x0,2940]
	str	w5, [x4,600]
	mul	w3, w3, w2
	str	w3, [x0,1232]
	ldrh	w2, [x0,2846]
	ldr	w3, [x0,2304]
	strh	wzr, [x4,610]
	sub	w2, w3, w2
	ldrh	w3, [x0,2292]
	sub	w2, w2, w5
	strb	wzr, [x4,614]
	strb	wzr, [x4,616]
	udiv	w2, w2, w3
	mov	w3, -1
	strh	w2, [x4,604]
	strh	w3, [x4,608]
	ldrh	w5, [x6,726]
	strh	w5, [x6,800]
	ldrh	w2, [x6,728]
	lsr	w8, w2, 6
	and	w2, w2, 63
	strb	w2, [x6,806]
	ldrb	w2, [x6,723]
	strb	w2, [x6,808]
	ldrh	w2, [x6,730]
	strh	w2, [x6,848]
	ldrh	w2, [x6,732]
	strh	w8, [x6,802]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strb	w2, [x6,854]
	ldrb	w2, [x6,724]
	strb	w2, [x6,856]
	ldrh	w2, [x6,734]
	strh	w2, [x6,896]
	ldrh	w2, [x6,736]
	strh	w4, [x6,850]
	lsr	w4, w2, 6
	and	w2, w2, 63
	strh	w4, [x6,898]
	strb	w2, [x6,902]
	ldrb	w2, [x6,725]
	strb	w2, [x6,904]
	ldr	w2, [x6,744]
	str	w2, [x0,2444]
	ldr	w1, [x0,2412]
	ldr	w2, [x6,752]
	str	wzr, [x0,2420]
	str	wzr, [x0,2424]
	cmp	w2, w1
	str	wzr, [x0,2440]
	str	wzr, [x0,2436]
	str	wzr, [x0,2448]
	str	wzr, [x0,2456]
	str	wzr, [x0,2432]
	bls	.L2105
	str	w2, [x0,2412]
.L2105:
	add	x0, x19, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,748]
	ldr	w1, [x22,2416]
	cmp	w0, w1
	bls	.L2106
	str	w0, [x22,2416]
.L2106:
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2107
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 800
	bl	make_superblock
.L2107:
	add	x2, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x0, x2, 848
	ldrh	w2, [x2,848]
	cmp	w2, w1
	beq	.L2108
	bl	make_superblock
.L2108:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x0, x19, 896
	ldrh	w2, [x19,896]
	cmp	w2, w1
	beq	.L2109
	bl	make_superblock
.L2109:
	add	x1, x20, :lo12:.LANCHOR5
	mov	w19, 0
	add	x0, x1, 608
	ldrh	w2, [x1,608]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2094
	bl	make_superblock
.L2094:
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
	adrp	x0, .LC47
	add	x1, x1, 32
	add	x0, x0, :lo12:.LC47
	ldrh	w23, [x19,2360]
	bl	printk
	ldr	x3, [x19,2728]
	ubfiz	x2, x22, 1, 16
	adrp	x0, .LC61
	mov	w1, w22
	add	x0, x0, :lo12:.LC61
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w22, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w24, .L2129
	ldrb	w0, [x19,1228]
	cbz	w0, .L2129
	mov	w0, w22
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w22, w0
	bne	.L2121
	ldrh	w23, [x19,2362]
	b	.L2121
.L2129:
	mov	w22, 0
.L2121:
	add	x3, x20, :lo12:.LANCHOR2
	adrp	x0, .LC62
	adrp	x24, .LC56
	add	x0, x0, :lo12:.LC62
	mov	w1, w22
	mov	w2, w23
	ldrh	w3, [x3,2360]
	mov	w21, 0
	mov	w26, 65535
	mov	w27, 56
	mov	w25, 4
	add	x24, x24, :lo12:.LC56
	bl	printk
.L2122:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x3, 0
	mov	w19, w3
	ldrh	w5, [x0,2292]
	ldrh	w6, [x0,2370]
	ldrh	w7, [x0,2372]
.L2123:
	cmp	w5, w3, uxth
	bls	.L2137
	add	x0, x29, 112
	add	x0, x0, x3, lsl 1
	ldrh	w1, [x0,16]
	cmp	w1, w26
	beq	.L2124
	add	x2, x20, :lo12:.LANCHOR2
	orr	w1, w21, w1, lsl 10
	umull	x0, w19, w27
	ldr	x4, [x2,2536]
	add	x4, x4, x0
	str	w1, [x4,4]
	ldr	x1, [x2,2536]
	ldr	x4, [x2,2616]
	add	x0, x1, x0
	ldr	x2, [x2,2672]
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
.L2124:
	add	x3, x3, 1
	b	.L2123
.L2137:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w22
	mov	x28, 0
	ldr	x0, [x0,2536]
	bl	FlashReadPages
	mov	x8, 56
.L2126:
	cmp	w19, w28, uxth
	bls	.L2138
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x28, x8
	str	x8, [x29,104]
	add	x28, x28, 1
	ldr	x4, [x0,2536]
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
	b	.L2126
.L2138:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, w23
	bne	.L2122
.L2128:
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
	adrp	x23, .LC64
	adrp	x22, .LC63
	adrp	x24, .LC65
	add	x22, x22, :lo12:.LC63
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC64
	mov	w20, 0
	add	x24, x24, :lo12:.LC65
	stp	x25, x26, [sp,80]
	bl	printk
.L2140:
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	ldrh	w0, [x19,2302]
	cmp	w0, w20
	bls	.L2149
	mov	w0, w20
	strh	w20, [x29,80]
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x4, [x19,2728]
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,2292]
	ldrh	w7, [x19,2370]
	mov	w9, 65535
	ldrh	w8, [x19,2372]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L2141:
	cmp	w6, w1, uxth
	bls	.L2150
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w0, [x0,16]
	cmp	w0, w9
	beq	.L2142
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w0, w0, 10
	umull	x2, w19, w10
	ldr	x4, [x3,2536]
	add	x4, x4, x2
	str	w0, [x4,4]
	ldr	x0, [x3,2536]
	ldr	x4, [x3,2616]
	add	x2, x0, x2
	ldr	x3, [x3,2672]
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
.L2142:
	add	x1, x1, 1
	b	.L2141
.L2150:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,2536]
	bl	FlashReadPages
.L2144:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L2151
	ldr	x4, [x0,2536]
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
	b	.L2144
.L2151:
	ldr	x0, [x0,2536]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L2146:
	cmp	w19, w25, uxth
	bls	.L2152
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,2536]
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
	b	.L2146
.L2152:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2140
.L2149:
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
	ldrh	w2, [x19,2302]
	add	x21, x22, :lo12:.LANCHOR4
	ldr	x0, [x19,2824]
	mov	w28, 0
	mov	w24, w28
	mov	w26, -1
	mul	w2, w2, w25
	mov	w27, 32768
	bl	ftl_memset
	strh	wzr, [x19,2402]
	str	xzr, [x21,784]
	mov	w19, w28
	str	xzr, [x21,760]
	str	xzr, [x21,768]
	strh	wzr, [x21,776]
	strh	wzr, [x21,792]
.L2154:
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w1, [x0,2300]
	cmp	w19, w1
	bge	.L2161
	mov	w20, 0
	ldrh	w5, [x0,2292]
	ldrh	w4, [x0,2360]
	mov	w3, w20
	mov	x2, x0
.L2162:
	cmp	w3, w5
	bge	.L2163
	add	x0, x2, 2320
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
	cbnz	w0, .L2155
	add	w20, w4, w20
	sxth	w20, w20
.L2155:
	add	w3, w3, 1
	sxth	w3, w3
	b	.L2162
.L2163:
	cbz	w20, .L2157
	sdiv	w20, w27, w20
	sxth	w20, w20
	b	.L2158
.L2157:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x0, [x0,2728]
	strh	w26, [x0,w19,sxtw 1]
.L2158:
	add	x1, x23, :lo12:.LANCHOR2
	ldr	x0, [x1,2824]
	smaddl	x0, w19, w25, x0
	strh	w20, [x0,4]
	ldrh	w0, [x21,800]
	cmp	w19, w0
	beq	.L2159
	ldrh	w0, [x21,848]
	cmp	w19, w0
	beq	.L2159
	ldrh	w0, [x21,896]
	cmp	w19, w0
	beq	.L2159
	ldr	x0, [x1,2728]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L2160
	add	w28, w28, 1
	mov	w0, w19
	uxth	w28, w28
	bl	INSERT_FREE_LIST
	b	.L2159
.L2160:
	add	w24, w24, 1
	mov	w0, w19
	uxth	w24, w24
	bl	INSERT_DATA_LIST
.L2159:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2154
.L2161:
	add	x22, x22, :lo12:.LANCHOR4
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x25, x26, [sp,64]
	strh	w24, [x22,776]
	strh	w28, [x22,792]
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
	add	x2, x0, 656
	add	x29, sp, 0
	ldr	w3, [x0,656]
	cmp	w3, w1
	bne	.L2164
	mov	w1, 96
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,1168]
	ldr	w0, [x0,1172]
	str	w0, [x2,92]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x2,88]
	ldr	w1, [x0,2420]
	str	w1, [x2,8]
	ldr	w1, [x0,2424]
	str	w1, [x2,12]
	ldr	w1, [x0,2440]
	str	w1, [x2,16]
	ldr	w1, [x0,2436]
	str	w1, [x2,20]
	ldr	w1, [x0,2448]
	str	w1, [x2,28]
	ldr	w1, [x0,2452]
	str	w1, [x2,32]
	ldr	w1, [x0,2428]
	str	w1, [x2,36]
	ldr	w1, [x0,2432]
	str	w1, [x2,40]
	ldr	w1, [x0,2456]
	str	w1, [x2,44]
	ldr	w1, [x0,2460]
	str	w1, [x2,48]
	ldr	w1, [x0,2408]
	ldr	w0, [x0,2404]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2164:
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
	ldr	w4, [x3,2196]
	cbz	w4, .L2167
	ldr	w3, [x3,2460]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2167:
	adrp	x3, .LANCHOR5
	add	x3, x3, :lo12:.LANCHOR5
	ldrh	w4, [x3,1176]
	cmp	w4, 31
	bhi	.L2168
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,1176]
.L2168:
	cbnz	w0, .L2169
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,2704]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2170
.L2169:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,2704]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,2704]
	ldrh	w1, [x2,2688]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,2704]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2170:
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
	add	x21, x20, 656
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,656]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2175
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,656]
.L2175:
	add	x1, x19, :lo12:.LANCHOR5
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	add	x0, x1, 656
	adrp	x20, .LANCHOR2
	ldr	w3, [x1,656]
	cmp	w3, w2
	bne	.L2176
	ldr	w2, [x1,744]
	str	w2, [x1,1168]
	ldr	w2, [x1,748]
	str	w2, [x1,1172]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x0,8]
	str	w2, [x1,2420]
	ldr	w2, [x0,12]
	str	w2, [x1,2424]
	ldr	w2, [x0,16]
	str	w2, [x1,2440]
	ldr	w2, [x0,20]
	str	w2, [x1,2436]
	ldr	w2, [x0,28]
	str	w2, [x1,2448]
	ldr	w2, [x0,32]
	str	w2, [x1,2452]
	ldr	w2, [x0,36]
	str	w2, [x1,2428]
	ldr	w2, [x0,40]
	str	w2, [x1,2432]
	ldr	w2, [x0,44]
	str	w2, [x1,2456]
	ldr	w2, [x0,48]
	ldr	w0, [x0,60]
	str	w2, [x1,2460]
	str	w0, [x1,2408]
.L2176:
	add	x0, x19, :lo12:.LANCHOR5
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, 34661
	add	x0, x0, 656
	movk	w2, 0x1234, lsl 16
	ldr	w3, [x0,68]
	str	wzr, [x1,2404]
	cmp	w3, w2
	bne	.L2177
	ldrb	w2, [x1,1228]
	cbz	w2, .L2178
	str	wzr, [x0,68]
	bl	Ftl_save_ext_data
	b	.L2177
.L2178:
	mov	w0, 1
	str	w0, [x1,2196]
	adrp	x0, .LC45
	adrp	x1, .LC66
	add	x0, x0, :lo12:.LC45
	add	x1, x1, :lo12:.LC66
	bl	printk
.L2177:
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR5
	ldr	x21, [sp,32]
	ldrh	w2, [x0,2352]
	ldr	w3, [x0,2444]
	ldr	w1, [x0,2448]
	ldrh	w0, [x0,2300]
	madd	w1, w2, w3, w1
	udiv	w0, w1, w0
	str	w0, [x19,1180]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #80
	adrp	x0, .LC67
	add	x0, x0, :lo12:.LC67
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,64]
	adrp	x23, .LC68
	add	x20, x19, 2952
	ldrh	w1, [x19,2358]
	add	x23, x23, :lo12:.LC68
	ldrh	w2, [x19,2360]
	stp	x21, x22, [sp,48]
	mov	w22, 0
	bl	printk
.L2180:
	ldrh	w0, [x19,2358]
	cmp	w22, w0
	bcs	.L2182
	lsl	w24, w22, 10
	mov	w21, 0
.L2183:
	ldrh	w0, [x19,2360]
	cmp	w21, w0
	bcs	.L2184
	add	w0, w21, w24
	str	w0, [x20,4]
	ldr	x0, [x19,2592]
	mov	w1, 1
	str	x0, [x20,8]
	mov	w2, 0
	ldr	x0, [x19,2656]
	add	w21, w21, 1
	str	x0, [x20,16]
	mov	x0, x20
	str	wzr, [x20]
	bl	FlashReadPages
	ldr	x7, [x20,8]
	ldr	x6, [x20,16]
	ldr	w0, [x7,4]
	str	w0, [sp]
	mov	x0, x23
	ldr	w3, [x6]
	ldr	w4, [x6,4]
	ldr	w5, [x6,8]
	ldr	w1, [x20,4]
	ldr	w2, [x20]
	ldr	w6, [x6,12]
	ldr	w7, [x7]
	bl	printk
	b	.L2183
.L2184:
	add	w22, w22, 1
	b	.L2180
.L2182:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC69
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC69
	add	x1, x1, 72
	bl	printk
	sub	sp, x29, #16
	ldp	x19, x20, [sp,32]
	ldp	x21, x22, [sp,48]
	ldp	x23, x24, [sp,64]
	ldp	x29, x30, [sp,16]
	add	sp, sp, 80
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
	ldrh	w24, [x1,2292]
	mov	w19, w22
	ldrh	w25, [x1,2360]
	mov	x23, x1
.L2186:
	cmp	w24, w22, uxth
	bls	.L2193
	add	x0, x23, 2320
	mov	w1, w20
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2187
	add	w19, w19, w25
	uxth	w19, w19
.L2187:
	add	x22, x22, 1
	b	.L2186
.L2193:
	cbz	w19, .L2189
	mov	w0, 32768
	sdiv	w19, w0, w19
.L2189:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 6
	umull	x20, w20, w1
	ldr	x0, [x0,2824]
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
	adrp	x0, .LANCHOR4+784
	ldr	x1, [x0,#:lo12:.LANCHOR4+784]
	mov	w0, w1
	cbz	x1, .L2195
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x2, [x0,2824]
	ldr	x0, [x0,2712]
	sub	x1, x1, x2
	mov	x2, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x2, x1, x1
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L2195:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR4
	uxth	w4, w0
	add	x2, x2, :lo12:.LANCHOR4
	ldr	x1, [x2,784]
	mov	w0, w1
	cbz	x1, .L2198
	ldrh	w2, [x2,792]
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
	ldr	x0, [x0,2824]
	sub	x1, x1, x0
	asr	x1, x1, 1
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L2200:
	cmp	w3, w4
	beq	.L2203
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L2201
.L2203:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,2712]
	ldrh	w0, [x0,x1]
	b	.L2198
.L2201:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L2200
.L2198:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC70
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x24, .LANCHOR2
	add	x1, x1, :lo12:.LC70
	stp	x21, x22, [sp,32]
	add	x22, x24, :lo12:.LANCHOR2
	mov	x23, x0
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x22,1200]
	adrp	x1, .LC71
	mov	x0, x20
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC72
	ldr	w2, [x22,2376]
	mov	x0, x20
	add	x1, x1, :lo12:.LC72
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+3352
	ldr	w0, [x0,#:lo12:.LANCHOR1+3352]
	cmp	w0, 1
	beq	.L2206
	sub	w0, w20, w23
	b	.L2207
.L2206:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	adrp	x26, .LANCHOR4
	bl	NandcGetTimeCfg
	add	x25, x26, :lo12:.LANCHOR4
	ldr	w4, [x29,104]
	adrp	x1, .LC73
	ldr	w5, [x29,108]
	mov	x0, x20
	ldr	w3, [x29,100]
	add	x1, x1, :lo12:.LC73
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC74
	mov	x0, x21
	add	x1, x1, :lo12:.LC74
	add	x21, x21, 10
	adrp	x20, .LANCHOR5
	bl	strcpy
	add	x20, x20, :lo12:.LANCHOR5
	ldr	w2, [x22,2940]
	adrp	x1, .LC75
	mov	x0, x21
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,1084]
	adrp	x1, .LC76
	mov	x0, x21
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2440]
	adrp	x1, .LC77
	mov	x0, x21
	add	x1, x1, :lo12:.LC77
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2428]
	adrp	x1, .LC78
	mov	x0, x21
	add	x1, x1, :lo12:.LC78
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2424]
	adrp	x1, .LC79
	mov	x0, x21
	add	x1, x1, :lo12:.LC79
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2432]
	adrp	x1, .LC80
	mov	x0, x21
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2436]
	adrp	x1, .LC81
	mov	x0, x21
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2420]
	adrp	x1, .LC82
	mov	x0, x21
	add	x1, x1, :lo12:.LC82
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1168]
	adrp	x1, .LC83
	mov	x0, x21
	add	x1, x1, :lo12:.LC83
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1172]
	adrp	x1, .LC84
	mov	x0, x21
	add	x1, x1, :lo12:.LC84
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2412]
	adrp	x1, .LC85
	mov	x0, x21
	add	x1, x1, :lo12:.LC85
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2416]
	adrp	x1, .LC86
	add	x1, x1, :lo12:.LC86
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x22,2846]
	adrp	x1, .LC87
	mov	x0, x21
	add	x1, x1, :lo12:.LC87
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,792]
	adrp	x1, .LC88
	mov	x0, x21
	add	x1, x1, :lo12:.LC88
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2444]
	adrp	x1, .LC89
	mov	x0, x21
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2448]
	adrp	x1, .LC90
	mov	x0, x21
	add	x1, x1, :lo12:.LC90
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,1180]
	adrp	x1, .LC91
	mov	x0, x21
	add	x1, x1, :lo12:.LC91
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2452]
	adrp	x1, .LC92
	mov	x0, x21
	add	x1, x1, :lo12:.LC92
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2456]
	adrp	x1, .LC93
	mov	x0, x21
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2460]
	adrp	x1, .LC94
	mov	x0, x21
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,742]
	adrp	x1, .LC95
	mov	x0, x21
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,740]
	adrp	x1, .LC96
	mov	x0, x21
	add	x1, x1, :lo12:.LC96
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,1232]
	adrp	x1, .LC97
	mov	x0, x21
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2392]
	adrp	x1, .LC98
	mov	x0, x21
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2288]
	adrp	x1, .LC99
	mov	x0, x21
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3014]
	adrp	x1, .LC100
	mov	x0, x21
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2300]
	adrp	x1, .LC101
	mov	x0, x21
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,604]
	adrp	x1, .LC102
	mov	x0, x21
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,2304]
	adrp	x1, .LC103
	mov	x0, x21
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,1088]
	adrp	x1, .LC104
	mov	x0, x21
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,2840]
	adrp	x1, .LC105
	mov	x0, x21
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,802]
	adrp	x1, .LC106
	mov	x0, x21
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,806]
	adrp	x1, .LC107
	mov	x0, x21
	add	x1, x1, :lo12:.LC107
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,800]
	adrp	x1, .LC108
	mov	x0, x21
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,808]
	adrp	x1, .LC109
	mov	x0, x21
	add	x1, x1, :lo12:.LC109
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,804]
	adrp	x1, .LC110
	mov	x0, x21
	add	x1, x1, :lo12:.LC110
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x25,800]
	adrp	x1, .LC111
	ldr	x2, [x22,2728]
	mov	x0, x21
	add	x1, x1, :lo12:.LC111
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,850]
	adrp	x1, .LC112
	mov	x0, x21
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,854]
	adrp	x1, .LC113
	mov	x0, x21
	add	x1, x1, :lo12:.LC113
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,848]
	adrp	x1, .LC114
	mov	x0, x21
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,856]
	adrp	x1, .LC115
	mov	x0, x21
	add	x1, x1, :lo12:.LC115
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,852]
	adrp	x1, .LC116
	mov	x0, x21
	add	x1, x1, :lo12:.LC116
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x25,848]
	adrp	x1, .LC117
	ldr	x2, [x22,2728]
	mov	x0, x21
	add	x1, x1, :lo12:.LC117
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,898]
	adrp	x1, .LC118
	mov	x0, x21
	add	x1, x1, :lo12:.LC118
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,902]
	adrp	x1, .LC119
	mov	x0, x21
	add	x1, x1, :lo12:.LC119
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,896]
	adrp	x1, .LC120
	mov	x0, x21
	add	x1, x1, :lo12:.LC120
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x25,904]
	adrp	x1, .LC121
	mov	x0, x21
	add	x1, x1, :lo12:.LC121
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x25,900]
	adrp	x1, .LC122
	mov	x0, x21
	add	x1, x1, :lo12:.LC122
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,610]
	adrp	x1, .LC123
	mov	x0, x21
	add	x1, x1, :lo12:.LC123
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,614]
	adrp	x1, .LC124
	mov	x0, x21
	add	x1, x1, :lo12:.LC124
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,608]
	adrp	x1, .LC125
	mov	x0, x21
	add	x1, x1, :lo12:.LC125
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,616]
	adrp	x1, .LC126
	mov	x0, x21
	add	x1, x1, :lo12:.LC126
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,612]
	adrp	x1, .LC127
	mov	x0, x21
	add	x1, x1, :lo12:.LC127
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,2408]
	adrp	x1, .LC128
	ldr	w2, [x22,2196]
	add	x1, x1, :lo12:.LC128
	ldr	w4, [x20,740]
	mov	x0, x21
	ldr	w5, [x20,732]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x20,736]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x20,728]
	adrp	x1, .LC129
	mov	x0, x19
	add	x1, x1, :lo12:.LC129
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x20,752]
	adrp	x1, .LC130
	mov	x0, x19
	add	x1, x1, :lo12:.LC130
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2496]
	adrp	x1, .LC131
	mov	x0, x19
	add	x1, x1, :lo12:.LC131
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2498]
	adrp	x1, .LC132
	mov	x0, x19
	add	x1, x1, :lo12:.LC132
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,2480]
	adrp	x1, .LC133
	mov	x0, x19
	add	x1, x1, :lo12:.LC133
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,2500]
	adrp	x1, .LC134
	add	x1, x1, :lo12:.LC134
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC135
	mov	x0, x19
	add	x1, x1, :lo12:.LC135
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x25,792]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC136
	mov	x0, x19
	add	x1, x1, :lo12:.LC136
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x20,608]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2208
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,2728]
	adrp	x1, .LC137
	mov	x0, x19
	add	x1, x1, :lo12:.LC137
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L2208:
	mov	w0, 0
	adrp	x22, .LC138
	mov	w20, 0
	mov	w28, 65535
	bl	List_get_gc_head_node
	mov	w27, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC138
.L2210:
	cmp	w3, w28
	beq	.L2209
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w27
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,2824]
	mov	x1, x22
	ldr	x4, [x21,2728]
	add	w20, w20, 1
	ldr	x7, [x21,2712]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2824]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L2210
.L2209:
	add	x26, x26, :lo12:.LANCHOR4
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC139
	mov	w20, 0
	mov	w27, 65535
	add	x22, x22, :lo12:.LC139
	ldr	x0, [x26,784]
	mov	w26, 6
	ldr	x3, [x1,2824]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L2212:
	cmp	w3, w27
	beq	.L2211
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,2824]
	mov	x1, x22
	ldr	x6, [x21,2712]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,2824]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L2212
.L2211:
	sub	w0, w19, w23
.L2207:
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
	adrp	x1, .LC45
	adrp	x2, .LC140
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC45
	add	x2, x2, :lo12:.LC140
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
	ldr	w3, [x1,1180]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,2460]
	cmp	w3, w2
	bcs	.L2217
	str	wzr, [x1,2444]
	ldrh	w3, [x1,2300]
	ldr	x5, [x1,2712]
	mov	x1, 0
.L2218:
	cmp	w3, w1
	bls	.L2250
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x1,lsl 1]
	add	x1, x1, 1
	ldr	w2, [x4,2444]
	add	w2, w6, w2
	str	w2, [x4,2444]
	b	.L2218
.L2250:
	add	x2, x19, :lo12:.LANCHOR2
	add	x4, x0, :lo12:.LANCHOR5
	ldr	w1, [x2,2444]
	udiv	w3, w1, w3
	str	w3, [x4,1180]
	ldr	w3, [x2,2448]
	sub	w1, w1, w3
	ldrh	w3, [x2,2352]
	udiv	w1, w1, w3
	str	w1, [x2,2444]
	b	.L2220
.L2217:
	ldr	w2, [x1,2456]
	cmp	w3, w2
	bls	.L2220
	add	w2, w2, 1
	str	w2, [x1,2456]
	mov	w1, 0
.L2222:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w3, [x2,2300]
	cmp	w1, w3
	bcs	.L2220
	ubfiz	x3, x1, 1, 32
	ldr	x4, [x2,2712]
	add	w1, w1, 1
	ldrh	w2, [x4,x3]
	add	w2, w2, 1
	strh	w2, [x4,x3]
	b	.L2222
.L2220:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w21, [x1,2460]
	ldr	w23, [x0,1180]
	add	w0, w21, 256
	cmp	w0, w23
	bls	.L2225
	ldr	w2, [x1,2456]
	add	w0, w21, 768
	cmp	w0, w2
	bls	.L2225
	ldr	w0, [x1,2196]
	cbz	w0, .L2228
	cmp	w21, 40
	bls	.L2225
.L2228:
	mov	w0, 65535
	b	.L2227
.L2225:
	adrp	x20, .LANCHOR4
	add	x0, x20, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L2237
	cmp	w21, 40
	bhi	.L2228
.L2237:
	add	x20, x20, :lo12:.LANCHOR4
	ldr	x3, [x20,760]
	cbz	x3, .L2228
	add	x0, x19, :lo12:.LANCHOR2
	mov	w26, 65535
	mov	w1, 0
	mov	w20, w26
	mov	w8, w26
	mov	w10, 6
	ldrh	w9, [x0,2300]
	ldr	x7, [x0,2824]
	ldr	x25, [x0,2712]
	mov	x0, -6148914691236517206
	add	x0, x0, 1
.L2230:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L2232
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L2228
	ldrh	w2, [x3,4]
	cbz	w2, .L2231
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w21
	bls	.L2236
	cmp	w2, w26
	bcs	.L2231
	mov	w26, w2
	mov	w20, w3
.L2231:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L2230
.L2236:
	mov	w20, w3
.L2232:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2228
	ubfiz	x24, x20, 1, 16
	ldrh	w22, [x25,x24]
	cmp	w22, w21
	bls	.L2234
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L2234
	add	x0, x19, :lo12:.LANCHOR2
	str	w26, [x0,2460]
.L2234:
	cmp	w22, w23
	bcs	.L2228
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L2228
	add	w0, w22, 256
	cmp	w0, w23
	bcc	.L2235
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,2456]
	cmp	w22, w0
	bcs	.L2228
.L2235:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC141
	ldrh	w5, [x25,x24]
	add	x0, x0, :lo12:.LC141
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,2728]
	ldr	w3, [x19,2456]
	ldrh	w4, [x4,x24]
	bl	printk
	mov	w0, 1
	str	w0, [x19,2468]
	mov	w0, w20
.L2227:
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
	beq	.L2252
	adrp	x2, .LANCHOR2+2728
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+2728]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L2252:
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
	ldr	w1, [x1,2944]
	cbnz	w1, .L2254
	adrp	x21, .LANCHOR4
	mov	x20, x0
	add	x21, x21, :lo12:.LANCHOR4
	add	x23, x21, 896
	mov	x22, x21
	add	x24, x21, 784
.L2255:
	cmp	x20, x23
	bne	.L2256
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w0, [x21,792]
	lsr	w3, w0, 1
	ldr	w4, [x2,2468]
	add	w5, w3, 1
	mul	w1, w0, w4
	add	w1, w5, w1, lsr 2
	ldr	w5, [x2,2196]
	uxth	w1, w1
	cbz	w5, .L2257
	ldr	w2, [x2,2460]
	cmp	w2, 39
	bhi	.L2257
	cmp	w2, 2
	mov	w1, 0
	bls	.L2258
	tbz	x0, 0, .L2285
	cbz	w4, .L2258
.L2285:
	mov	w1, w3
	b	.L2257
.L2256:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2258
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w0, [x2,2312]
	cmp	w0, 1
	beq	.L2258
	ldrb	w0, [x2,1228]
	cbnz	w0, .L2258
	ldr	w3, [x2,2196]
	ldrh	w0, [x21,792]
	lsr	w1, w0, 3
	cbz	w3, .L2257
	ldr	w2, [x2,2460]
	cmp	w2, 1
	bhi	.L2257
	mov	w1, 7
	mul	w1, w0, w1
	lsr	w1, w1, 3
.L2257:
	cbz	w1, .L2258
	sub	w1, w1, #1
	uxth	w1, w1
.L2258:
	ldrb	w2, [x20,8]
	mov	x0, x24
	add	x26, x19, :lo12:.LANCHOR2
	bl	List_pop_index_node
	uxth	w27, w0
	ldrh	w0, [x22,792]
	sub	w0, w0, #1
	strh	w0, [x22,792]
	ldrh	w0, [x26,2300]
	cmp	w0, w27
	bls	.L2255
	ubfiz	x25, x27, 1, 16
	ldr	x0, [x26,2728]
	ldrh	w0, [x0,x25]
	cbnz	w0, .L2255
	strh	w27, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2261
	mov	x0, 0
	ldrh	w5, [x26,2292]
	mov	x3, 56
	mov	w26, w0
	mov	w6, 65535
	b	.L2262
.L2261:
	ldr	x0, [x26,2728]
	b	.L2310
.L2264:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x4, [x1,2560]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2263
	umull	x2, w26, w3
	add	w26, w26, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w26, w26
	str	w1, [x2,4]
.L2263:
	add	x0, x0, 1
.L2262:
	cmp	w5, w0, uxth
	bhi	.L2264
	add	x0, x22, 800
	cmp	x20, x0
	bne	.L2265
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,2196]
	cbz	w2, .L2265
	ldr	x1, [x1,2712]
	ldrh	w1, [x1,x25]
	cmp	w1, 40
	bls	.L2265
	strb	wzr, [x22,808]
.L2265:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2266
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x0, [x1,2712]
	ldrh	w2, [x0,x25]
	cbz	w2, .L2267
	ldrh	w1, [x1,2352]
	add	w1, w2, w1
	b	.L2311
.L2267:
	mov	w1, 2
.L2311:
	strh	w1, [x0,x25]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,2444]
	add	w0, w0, 1
	str	w0, [x1,2444]
	mov	w0, w27
	mov	w1, 0
	bl	ftl_set_blk_mode
	b	.L2269
.L2266:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x2, [x0,2712]
	ldrh	w1, [x2,x25]
	add	w1, w1, 1
	strh	w1, [x2,x25]
	ldr	w1, [x0,2448]
	add	w1, w1, 1
	str	w1, [x0,2448]
	mov	w0, w27
	bl	ftl_set_blk_mode.part.17
.L2269:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,2712]
	ldr	w2, [x0,2456]
	ldrh	w1, [x1,x25]
	cmp	w1, w2
	bls	.L2270
	str	w1, [x0,2456]
.L2270:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,2352]
	ldr	w3, [x0,2444]
	ldr	w1, [x0,2448]
	madd	w1, w2, w3, w1
	ldrh	w2, [x0,2300]
	mov	x3, 56
	udiv	w1, w1, w2
	adrp	x2, .LANCHOR5+1180
	str	w1, [x2,#:lo12:.LANCHOR5+1180]
	ldr	x1, [x0,2704]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2271:
	cmp	w26, w0, uxth
	bls	.L2313
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,2560]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2271
.L2313:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x0,1228]
	cbz	w1, .L2273
	ldrb	w1, [x20,8]
	ldr	x0, [x0,2560]
	cmp	w1, 1
	bne	.L2274
	mov	w1, 0
	b	.L2312
.L2274:
	mov	w1, 1
.L2312:
	mov	w2, w26
	bl	FlashEraseBlocks
.L2273:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x20,8]
	mov	w2, w26
	mov	x28, 0
	ldr	x0, [x0,2560]
	bl	FlashEraseBlocks
	mov	w1, w28
	mov	x3, 56
.L2275:
	cmp	w26, w28, uxth
	bls	.L2314
	add	x2, x19, :lo12:.LANCHOR2
	mul	x0, x28, x3
	ldr	x2, [x2,2560]
	add	x4, x2, x0
	ldr	w2, [x2,x0]
	cmn	w2, #1
	bne	.L2276
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
.L2276:
	add	x28, x28, 1
	b	.L2275
.L2314:
	cbz	w1, .L2278
	mov	w0, w27
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2278:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2279
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2728]
.L2310:
	mov	w1, -1
	strh	w1, [x0,x25]
	b	.L2255
.L2279:
	add	x19, x19, :lo12:.LANCHOR2
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x19,2360]
	strh	w27, [x20]
	mul	w0, w0, w1
	ldr	w1, [x19,2412]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x19,2412]
	ldr	x1, [x19,2728]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2254:
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
	adrp	x0, .LANCHOR5+1184
	mov	w1, 0
	adrp	x7, .LANCHOR2
	mov	w8, 24
	mov	w9, 1
	mov	w4, 4
	str	wzr, [x0,#:lo12:.LANCHOR5+1184]
	mov	w10, 56
.L2316:
	add	x2, x7, :lo12:.LANCHOR2
	adrp	x6, .LANCHOR2
	ldrh	w0, [x2,2292]
	cmp	w1, w0
	bcs	.L2320
	umull	x3, w1, w8
	ldr	x5, [x2,2648]
	ldr	x6, [x2,2616]
	add	x0, x5, x3
	str	w9, [x0,16]
	ldrh	w0, [x2,2370]
	mul	w0, w1, w0
	sdiv	w0, w0, w4
	add	x0, x6, x0, sxtw 2
	str	x0, [x5,x3]
	ldrh	w0, [x2,2372]
	ldr	x6, [x2,2648]
	ldr	x11, [x2,2672]
	mul	w0, w1, w0
	add	x5, x6, x3
	ldr	x2, [x2,2568]
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
	b	.L2316
.L2320:
	mov	w7, 24
	mov	w4, 4
.L2318:
	add	x1, x6, :lo12:.LANCHOR2
	ldr	w2, [x1,2584]
	cmp	w0, w2
	bcs	.L2321
	umull	x3, w0, w7
	ldr	x5, [x1,2648]
	ldr	x8, [x1,2616]
	add	x2, x5, x3
	str	wzr, [x2,16]
	ldrh	w2, [x1,2370]
	mul	w2, w0, w2
	sdiv	w2, w2, w4
	add	x2, x8, x2, sxtw 2
	str	x2, [x5,x3]
	ldrh	w2, [x1,2372]
	ldr	x5, [x1,2648]
	ldr	x1, [x1,2672]
	mul	w2, w0, w2
	add	x3, x5, x3
	add	w0, w0, 1
	sdiv	w2, w2, w4
	uxth	w0, w0
	add	x1, x1, x2, sxtw 2
	str	x1, [x3,8]
	b	.L2318
.L2321:
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
	ldrh	w2, [x19,2380]
	strh	w0, [x20,1200]
	mov	w0, -1
	str	w0, [x20,1212]
	lsl	w2, w2, 1
	ldr	x0, [x19,2752]
	str	xzr, [x20,1192]
	str	wzr, [x20,1204]
	str	wzr, [x20,1208]
	str	wzr, [x19,2196]
	strh	wzr, [x19,2936]
	bl	ftl_memset
	ldrh	w2, [x19,2302]
	mov	w1, 0
	ldr	x0, [x19,2712]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19,2302]
	mov	w1, 0
	ldr	x0, [x19,2696]
	lsl	w2, w2, 1
	bl	ftl_memset
	adrp	x0, .LANCHOR4
	mov	w1, 0
	add	x0, x0, :lo12:.LANCHOR4
	mov	w2, 48
	add	x0, x0, 712
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x20, 656
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
	ldr	w8, [x2,2584]
	ldr	x5, [x2,2648]
.L2324:
	cmp	w3, w1
	bcs	.L2323
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L2329:
	cmp	w2, w8
	bcs	.L2326
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L2325
	str	wzr, [x10,16]
	b	.L2326
.L2325:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2329
.L2326:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2324
.L2323:
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
	ldr	w5, [x2,2584]
	ldr	x6, [x2,2648]
	mov	w2, 0
.L2331:
	cmp	w2, w1
	bcs	.L2337
	mov	w3, 0
.L2335:
	cmp	w3, w5
	bcs	.L2333
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L2332
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L2333
.L2332:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2335
.L2333:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2331
.L2337:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR5+1216
	uxth	w0, w0
	ldrh	w2, [x1,#:lo12:.LANCHOR5+1216]
	adrp	x1, .LANCHOR2+2520
	ldr	x3, [x1,#:lo12:.LANCHOR2+2520]
	mov	x1, 0
.L2339:
	cmp	w2, w1, uxth
	bls	.L2343
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L2339
	mov	w0, 1
	b	.L2340
.L2343:
	mov	w0, 0
.L2340:
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
	ldrh	w8, [x4,1216]
	adrp	x4, .LANCHOR2
	add	x5, x4, :lo12:.LANCHOR2
	ldr	x7, [x5,2520]
.L2345:
	uxth	w5, w6
	cmp	w5, w8
	bcs	.L2349
	add	x6, x6, 1
	add	x10, x7, x6, lsl 1
	ldrh	w10, [x10,-2]
	cmp	w10, w9
	bne	.L2345
.L2349:
	cmp	w5, w8
	bne	.L2347
	ubfiz	x5, x5, 1, 16
	strh	w0, [x7,x5]
	add	x0, x3, :lo12:.LANCHOR5
	ldrh	w5, [x0,1216]
	add	w5, w5, 1
	strh	w5, [x0,1216]
.L2347:
	add	x3, x3, :lo12:.LANCHOR5
	add	x4, x4, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w5, [x3,1218]
	ldr	x6, [x4,2528]
	umull	x5, w5, w0
	add	x6, x6, x5
	str	w1, [x6,4]
	ldr	x1, [x4,2528]
	add	x0, x1, x5
	str	w2, [x0,8]
	str	w19, [x1,x5]
	ldrh	w0, [x3,1218]
	add	w0, w0, 1
	strh	w0, [x3,1218]
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
	ldrh	w2, [x19,2364]
	strh	wzr, [x0,1216]
	strh	wzr, [x0,1218]
	lsl	w2, w2, 1
	ldr	x0, [x19,2520]
	bl	ftl_memset
	ldrh	w3, [x19,2364]
	mov	w2, 12
	ldr	x0, [x19,2528]
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
	ldrh	w27, [x1,3356]
	mov	w1, 65535
	cmp	w27, w1
	beq	.L2385
	cbnz	w27, .L2353
	b	.L2354
.L2385:
	mov	w27, 0
.L2353:
	adrp	x0, .LANCHOR2+2360
	ldr	w1, [x29,136]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+2360]
	cmp	w1, w0
	bne	.L2355
.L2354:
	bl	FtlGcPageVarInit
.L2355:
	mov	w0, -1
	adrp	x23, .LANCHOR2
	str	w0, [x29,140]
	adrp	x0, .LC143
	add	x0, x0, :lo12:.LC143
	mov	w24, 0
	mov	x22, x23
	str	x0, [x29,120]
.L2356:
	ldrh	w1, [x19]
	mov	w0, 65535
	strb	wzr, [x19,8]
	cmp	w1, w0
	beq	.L2386
.L2358:
	mov	w28, w0
.L2382:
	adrp	x0, .LANCHOR2
	mov	x2, x19
	add	x0, x0, :lo12:.LANCHOR2
	mov	w20, 0
	mov	w10, 56
	mov	w7, 4
	ldrh	w5, [x0,2292]
	ldrh	w8, [x0,2370]
	ldrh	w9, [x0,2372]
	add	x5, x19, x5, lsl 1
.L2359:
	cmp	x2, x5
	beq	.L2404
	ldrh	w1, [x2,16]
	cmp	w1, w28
	beq	.L2360
	add	x4, x23, :lo12:.LANCHOR2
	orr	w1, w27, w1, lsl 10
	umull	x0, w20, w10
	ldr	x6, [x4,2536]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x4,2536]
	ldr	x6, [x4,2616]
	add	x0, x1, x0
	ldr	x4, [x4,2672]
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
.L2360:
	add	x2, x2, 2
	b	.L2359
.L2404:
	add	x0, x23, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 0
	mov	x25, 0
	ldr	x0, [x0,2536]
	bl	FlashReadPages
.L2362:
	cmp	w20, w25, uxth
	bls	.L2405
	add	x26, x23, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x21, x25, x0
	ldr	x1, [x26,2536]
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
	cbnz	w7, .L2363
	ldrh	w0, [x5]
	cmp	w0, w28
	bne	.L2364
.L2367:
	add	x1, x22, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR5
	ldrb	w0, [x1,1228]
	cbz	w0, .L2401
	add	x20, x20, :lo12:.LANCHOR5
	mov	w0, 1
	str	w0, [x20,1208]
	b	.L2357
.L2364:
	ldr	w0, [x5,8]
	ldr	w1, [x26,2940]
	cmp	w0, w1
	bls	.L2402
	b	.L2367
.L2401:
	ldrh	w2, [x19]
	add	x20, x20, :lo12:.LANCHOR5
	ldr	x1, [x1,2728]
	strh	w0, [x1,x2,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x20,608]
	b	.L2403
.L2402:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,88]
	cbz	w1, .L2371
	add	x1, x29, 164
	mov	w2, w7
	str	x5, [x29,112]
	str	x7, [x29,104]
	bl	log2phys
	ldr	x5, [x29,112]
	ldr	w1, [x29,164]
	ldr	w0, [x5,12]
	cmp	w1, w0
	bne	.L2371
	cmn	w1, #1
	beq	.L2371
	str	w0, [x29,172]
	mov	w1, 1
	ldr	x0, [x26,2640]
	ldr	x7, [x29,104]
	str	x0, [x29,176]
	ldr	x0, [x26,2664]
	mov	w2, w7
	str	x0, [x29,184]
	add	x0, x29, 168
	bl	FlashReadPages
	ldr	x0, [x26,2536]
	ldrh	w2, [x26,2366]
	ldr	x4, [x29,176]
	add	x21, x0, x21
	ldr	x5, [x29,112]
	lsl	w2, w2, 7
	mov	x0, 0
.L2373:
	cmp	w2, w0
	bls	.L2371
	lsl	x1, x0, 2
	ldr	x6, [x21,8]
	add	x0, x0, 1
	ldr	w6, [x6,x1]
	ldr	w1, [x4,x1]
	cmp	w6, w1
	beq	.L2373
	ldrh	w1, [x19]
	adrp	x0, .LC142
	ldr	w2, [x29,172]
	add	x0, x0, :lo12:.LC142
	bl	printk
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2728]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	adrp	x1, .LANCHOR5+608
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x1,#:lo12:.LANCHOR5+608]
	b	.L2403
.L2371:
	ldr	w0, [x5,12]
	add	x25, x25, 1
	ldr	w1, [x29,144]
	ldr	w2, [x5,8]
	bl	FtlGcUpdatePage
	b	.L2362
.L2363:
	ldr	x0, [x29,120]
	ldrh	w1, [x19]
	ldr	w2, [x29,144]
	bl	printk
	ldr	w0, [x26,2196]
	cbnz	w0, .L2376
	ldrb	w0, [x26,1228]
	cbz	w0, .L2377
.L2376:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2712]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2378
.L2377:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2379
.L2378:
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	add	x21, x0, x21
	ldr	w0, [x21,4]
	str	w0, [x29,140]
.L2379:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,2728]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
.L2403:
	bl	FtlGcPageVarInit
	mov	w27, 0
	b	.L2356
.L2405:
	ldr	w0, [x29,136]
	add	w24, w24, 1
	add	w3, w27, 1
	cmp	w24, w0
	uxth	w27, w3
	bcs	.L2381
.L2383:
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w0, [x0,2360]
	cmp	w0, w27
	bhi	.L2382
	b	.L2386
.L2381:
	ldr	x0, [x29,128]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3356]
	cmp	w0, w28
	beq	.L2383
	add	w0, w0, w24
	strh	w0, [x1,3356]
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w0, [x0,2360]
	cmp	w0, w27
	bls	.L2383
	b	.L2384
.L2386:
	mov	w6, 0
.L2357:
	ldr	x0, [x29,128]
	mov	w1, -1
	strh	w27, [x19,2]
	mov	w2, w6
	add	x0, x0, :lo12:.LANCHOR1
	strb	w6, [x19,6]
	strh	w1, [x0,3356]
	mov	x0, x19
	mov	w1, w27
	bl	ftl_sb_update_avl_pages
.L2384:
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
	ldrh	w0, [x19,2488]
	cmp	w0, w20
	beq	.L2408
	ldrh	w0, [x19,2490]
	cmp	w0, w20
	beq	.L2408
	ldrh	w0, [x19,2492]
	cmp	w0, w20
	beq	.L2408
	ldrh	w0, [x19,2494]
	cmp	w0, w20
	beq	.L2408
	adrp	x0, .LC144
	mov	w1, w20
	add	x0, x0, :lo12:.LC144
	bl	printk
	ldrh	w1, [x19,2488]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2410
	strh	w20, [x19,2488]
	b	.L2408
.L2410:
	ldrh	w1, [x19,2490]
	cmp	w1, w0
	bne	.L2411
	strh	w20, [x19,2490]
	b	.L2408
.L2411:
	ldrh	w1, [x19,2492]
	cmp	w1, w0
	bne	.L2412
	strh	w20, [x19,2492]
	b	.L2408
.L2412:
	ldrh	w1, [x19,2494]
	cmp	w1, w0
	bne	.L2408
	strh	w20, [x19,2494]
.L2408:
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
	ldrh	w0, [x19,2488]
	cmp	w0, w20
	beq	.L2414
	ldrh	w0, [x19,2490]
	cmp	w0, w20
	beq	.L2414
	ldrh	w0, [x19,2492]
	cmp	w0, w20
	beq	.L2414
	ldrh	w0, [x19,2494]
	cmp	w0, w20
	beq	.L2414
	adrp	x0, .LC144
	mov	w1, w20
	add	x0, x0, :lo12:.LC144
	bl	printk
	ldrh	w1, [x19,2488]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2415
	strh	w20, [x19,2488]
	b	.L2414
.L2415:
	ldrh	w1, [x19,2490]
	cmp	w1, w0
	bne	.L2416
	strh	w20, [x19,2490]
	b	.L2414
.L2416:
	ldrh	w1, [x19,2492]
	cmp	w1, w0
	bne	.L2417
	strh	w20, [x19,2492]
	b	.L2414
.L2417:
	ldrh	w1, [x19,2494]
	cmp	w1, w0
	bne	.L2422
	strh	w20, [x19,2494]
	b	.L2414
.L2422:
	mov	w21, -1
.L2414:
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
	ldrh	w1, [x22,2502]
	adrp	x0, .LC145
	mov	w2, w20
	add	x0, x0, :lo12:.LC145
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,2196]
	cbz	w0, .L2424
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,2712]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L2424
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L2424:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR5
	add	x1, x1, :lo12:.LANCHOR5
	add	x1, x1, 1224
	ldrh	w3, [x0,2502]
	mov	x0, 0
.L2425:
	cmp	w3, w0, uxth
	bls	.L2431
	add	x0, x0, 1
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L2425
	b	.L2426
.L2431:
	cmp	w3, 15
	bhi	.L2426
	adrp	x1, .LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR5
	add	w0, w3, 1
	add	x1, x1, 1224
	strh	w0, [x19,2502]
	strh	w20, [x1,w3,sxtw 1]
.L2426:
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
	ldrh	w1, [x0,2502]
	cbz	w1, .L2433
	ldrh	w3, [x0,2488]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2433
	ldrh	w2, [x0,2506]
	cmp	w2, w1
	bcc	.L2434
	strh	wzr, [x0,2506]
.L2434:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	add	x0, x0, 1224
	ldrh	w1, [x19,2506]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,2506]
	add	w0, w0, 1
	strh	w0, [x19,2506]
.L2433:
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
	ldrh	w0, [x19,2502]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	cbz	w0, .L2440
	adrp	x24, .LANCHOR5
	adrp	x22, .LC146
	add	x22, x22, :lo12:.LC146
	add	x25, x24, :lo12:.LANCHOR5
.L2448:
	ldrh	w0, [x19,2292]
	cmp	w0, w21
	bls	.L2449
	add	x0, x19, 2320
	mov	w1, w23
	mov	w20, 0
	add	x27, x24, :lo12:.LANCHOR5
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w26, w0
.L2441:
	ldrh	w0, [x19,2502]
	cmp	w0, w20
	bls	.L2450
	add	x0, x27, 1224
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w26
	bne	.L2442
	mov	w1, w26
	mov	x0, x22
	bl	printk
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,2502]
	mov	w3, w20
.L2443:
	cmp	w3, w2
	bcs	.L2451
	add	x4, x25, 1224
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2443
.L2451:
	sub	w2, w2, #1
	strh	w2, [x19,2502]
.L2442:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2441
.L2450:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2448
.L2449:
	bl	FtlGcReFreshBadBlk
.L2440:
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
	adrp	x1, .LANCHOR2+2728
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	uxth	w19, w0
	ldr	x1, [x1,#:lo12:.LANCHOR2+2728]
	ubfiz	x0, x19, 1, 16
	ldrh	w1, [x1,x0]
	cbnz	w1, .L2453
	adrp	x0, .LANCHOR5
	adrp	x20, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x0,608]
	cmp	w2, w19
	bne	.L2454
	mov	w1, -1
	strh	w1, [x0,608]
	b	.L2455
.L2454:
	add	x2, x20, :lo12:.LANCHOR4
	mov	w0, w1
	ldrh	w3, [x2,800]
	cmp	w3, w19
	beq	.L2456
	ldrh	w3, [x2,848]
	cmp	w3, w19
	beq	.L2456
	ldrh	w2, [x2,896]
	cmp	w2, w19
	beq	.L2456
.L2455:
	add	x20, x20, :lo12:.LANCHOR4
	mov	w1, w19
	add	x0, x20, 760
	bl	List_remove_node
	ldrh	w0, [x20,776]
	sub	w0, w0, #1
	strh	w0, [x20,776]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2456
.L2453:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2456:
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
	beq	.L2461
	adrp	x20, .LANCHOR2
	ubfiz	x22, x19, 1, 16
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,2728]
	ldrh	w2, [x0,x22]
	cbnz	w2, .L2462
	adrp	x0, .LC147
	mov	w1, w19
	add	x0, x0, :lo12:.LC147
	adrp	x21, .LANCHOR4
	add	x21, x21, :lo12:.LANCHOR4
	bl	printk
	add	x23, x21, 784
	ldr	x0, [x20,2728]
	mov	w1, 32
	strh	w1, [x0,x22]
	mov	x0, x23
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2463
	mov	w1, w19
	mov	x0, x23
	bl	List_remove_node
	ldrh	w0, [x21,792]
	sub	w0, w0, #1
	strh	w0, [x21,792]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,2728]
	adrp	x0, .LC148
	add	x0, x0, :lo12:.LC148
	mov	w1, w19
	ldrh	w2, [x2,x22]
	bl	printk
.L2463:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2470
.L2462:
	sub	w2, w2, #1
	strh	w2, [x0,x22]
.L2461:
	adrp	x20, .LANCHOR5
	mov	w0, 65535
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w1, [x20,1200]
	cmp	w1, w0
	bne	.L2465
	strh	w19, [x20,1200]
.L2470:
	mov	w0, 0
	b	.L2464
.L2465:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2464
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,1200]
.L2464:
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
	beq	.L2632
	ldrh	w0, [x19,2]
	adrp	x20, .LANCHOR2
	str	w0, [x29,204]
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w28, [x19,6]
	ldr	w1, [x29,204]
	ldrh	w0, [x0,2360]
	cmp	w0, w1
	bne	.L2474
	strh	wzr, [x19,4]
	b	.L2638
.L2474:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2475:
	cmp	w0, w2
	bne	.L2641
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2475
.L2641:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2477
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2478
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, w21
	ldrb	w0, [x0,1228]
	cbnz	w0, .L2552
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 136
	ldrh	w23, [x0,w21,sxtw 1]
	b	.L2552
.L2477:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2478
	mov	w23, w0
.L2552:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w25, 0
	mov	w8, 65535
	mov	w9, 56
	mov	w10, 4
	ldrh	w4, [x0,2292]
	ldrh	w7, [x0,2372]
	add	x4, x19, x4, lsl 1
	b	.L2480
.L2478:
	strh	wzr, [x19,2]
.L2638:
	strb	wzr, [x19,6]
	b	.L2632
.L2482:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2481
	add	x5, x20, :lo12:.LANCHOR2
	orr	w1, w23, w1, lsl 10
	umull	x0, w25, w9
	add	w3, w25, 1
	ldr	x6, [x5,2536]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,2536]
	ldr	x5, [x5,2672]
	add	x0, x1, x0
	mul	w1, w25, w7
	uxth	w25, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2481:
	add	x2, x2, 2
.L2480:
	cmp	x2, x4
	bne	.L2482
	ldrb	w0, [x19,8]
	str	wzr, [x29,200]
	cmp	w0, 1
	bne	.L2483
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,200]
.L2483:
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,200]
	mov	w1, w25
	mov	x26, 0
	mov	w24, 65535
	adrp	x27, .LANCHOR5
	ldr	x0, [x22,2536]
	bl	FlashReadPages
	ldr	w0, [x22,2416]
	adrp	x22, .LC149
	str	w0, [x29,192]
	add	x22, x22, :lo12:.LC149
	uxth	w0, w23
	str	w0, [x29,184]
.L2484:
	uxth	w2, w26
	cmp	w2, w25
	bcs	.L2491
	add	x6, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x26, x0
	ldr	x4, [x6,2536]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2485
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2486
	ldr	w1, [x6,2416]
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
	cbz	w0, .L2486
	add	w4, w4, 1
	str	w4, [x6,2416]
.L2486:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2487
.L2491:
	cmp	w2, w25
	bne	.L2633
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,2536]
	ldr	w0, [x0,4]
	b	.L2634
.L2485:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w24, [x29,184]
	add	x0, x27, :lo12:.LANCHOR5
	ldrh	w1, [x19]
	strh	w1, [x0,1258]
.L2487:
	add	x26, x26, 1
	b	.L2484
.L2633:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,2536]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2634:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2493
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbnz	w0, .L2493
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	add	x0, x0, 136
	ldrh	w21, [x0,w21,sxtw 1]
.L2493:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,2360]
	cmp	w0, w21
	bne	.L2494
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2494:
	uxth	w0, w28
	str	w0, [x29,160]
	ldr	w0, [x29,204]
	cmp	w21, w0
	bne	.L2495
	ldr	w0, [x29,160]
	cmp	w26, w0
	beq	.L2639
.L2495:
	ldr	w0, [x29,192]
	adrp	x25, .LANCHOR5
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w24, w0
	bne	.L2496
	cbnz	w1, .L2497
.L2496:
	add	x0, x25, :lo12:.LANCHOR5
	uxth	w6, w23
	ldr	w1, [x0,1212]
	cmn	w1, #1
	bne	.L2498
	str	w22, [x0,1212]
.L2498:
	add	x0, x25, :lo12:.LANCHOR5
	ldr	w24, [x29,204]
	ldr	w3, [x0,1212]
	ldr	w0, [x29,204]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2499
	sub	w24, w6, #7
	uxth	w24, w24
.L2499:
	mov	w28, -1
	adrp	x5, .LANCHOR0
	mov	w27, w28
	mov	w4, 65535
	mov	w7, 56
	add	x5, x5, :lo12:.LANCHOR0
.L2500:
	cmp	w24, w6
	bhi	.L2513
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, 0
	ldrh	w2, [x0,2292]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2514:
	cmp	x0, x2
	beq	.L2642
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2501
	add	x9, x20, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x8, w23, w7
	add	w23, w23, 1
	ldr	x9, [x9,2536]
	uxth	w23, w23
	add	x8, x9, x8
	str	w1, [x8,4]
.L2501:
	add	x0, x0, 2
	b	.L2514
.L2642:
	add	x8, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,200]
	mov	w1, w23
	str	x5, [x29,144]
	str	x7, [x29,152]
	ldr	x0, [x8,2536]
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
	ldr	x0, [x8,2536]
	ldrb	w9, [x8,1228]
	ldr	x4, [x29,168]
	add	x0, x0, 16
	ldr	x7, [x29,152]
	ldr	x5, [x29,144]
.L2503:
	cmp	w1, w23
	beq	.L2643
	ldr	w2, [x0,-16]
	cbnz	w2, .L2504
	ldr	x2, [x0]
	ldrh	w8, [x2]
	cmp	w8, w4
	beq	.L2505
	ldr	w8, [x2,4]
	cmn	w8, #1
	beq	.L2505
	add	x2, x25, :lo12:.LANCHOR5
	cmn	w28, #1
	ldr	w27, [x2,1212]
	str	w8, [x2,1212]
	bne	.L2505
	add	x2, x5, 1160
	ldrh	w2, [x2,x10,lsl 1]
	cmp	w2, w4
	bne	.L2506
	cbz	w9, .L2505
.L2506:
	cmp	w27, w22
	csinv	w28, w27, wzr, ne
	b	.L2505
.L2504:
	add	x0, x25, :lo12:.LANCHOR5
	ldrh	w1, [x19]
	strh	w1, [x0,1258]
	ldrb	w1, [x19,8]
	cbnz	w1, .L2497
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 1160
	ldrh	w2, [x1,w24,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2508
	cmn	w28, #1
	beq	.L2509
	str	w28, [x0,1212]
	b	.L2497
.L2509:
	cmp	w3, w22
	beq	.L2510
	str	w3, [x0,1212]
	b	.L2497
.L2510:
	ldr	w1, [x0,1212]
	b	.L2640
.L2508:
	cmp	w27, w22
	beq	.L2511
	cmn	w27, #1
	beq	.L2497
	str	w27, [x0,1212]
	b	.L2497
.L2511:
	ldr	w1, [x0,1212]
	cmp	w1, w22
	beq	.L2497
.L2640:
	sub	w1, w1, #1
	b	.L2635
.L2505:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2503
.L2643:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2500
.L2513:
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, -1
.L2635:
	str	w1, [x0,1212]
.L2497:
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, 1
	ldr	w23, [x29,204]
	strh	w1, [x0,1260]
	adrp	x0, .LC151
	add	x0, x0, :lo12:.LC151
	str	x0, [x29,136]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29,128]
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29,168]
.L2515:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x1, x19
	str	wzr, [x29,192]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,2292]
	ldrb	w6, [x0,1228]
	add	x3, x19, x3, lsl 1
.L2516:
	cmp	x1, x3
	beq	.L2644
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2517
	ldr	w0, [x29,192]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w23, w2, lsl 10
	ldr	x4, [x5,2536]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2518
	cbz	w6, .L2518
	ldr	x2, [x5,2536]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2518:
	ldr	w0, [x29,192]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,192]
.L2517:
	add	x1, x1, 2
	b	.L2516
.L2644:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,192]
	ldr	w2, [x29,200]
	ldr	x0, [x0,2536]
	bl	FlashReadPages
	str	xzr, [x29,176]
.L2520:
	ldrh	w0, [x29,176]
	ldr	w1, [x29,192]
	cmp	w1, w0, uxth
	bls	.L2645
	ldr	x0, [x29,176]
	mov	x24, 56
	mul	x24, x0, x24
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x28, [x0,2536]
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
	bcc	.L2521
	ldr	w2, [x29,160]
	cmp	w0, w2
	bcs	.L2556
	ldr	w2, [x29,204]
	cmp	w23, w2
	beq	.L2521
.L2556:
	cmp	w0, w26
	bne	.L2557
	cmp	w23, w21
	beq	.L2523
.L2557:
	ldr	w0, [x28,x24]
	cmn	w0, #1
	beq	.L2525
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2526
	ldrh	w0, [x19]
	b	.L2636
.L2526:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2527
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,2416]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2527
	add	w0, w22, 1
	str	w0, [x27,2416]
.L2527:
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
	ldr	w1, [x0,1212]
	cmn	w1, #1
	beq	.L2528
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2528
	cmn	w28, #1
	beq	.L2529
	add	x3, x20, :lo12:.LANCHOR2
	mov	w2, 0
	str	x3, [x29,120]
	ldr	x0, [x3,2536]
	add	x0, x0, x24
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,2536]
	str	x1, [x29,184]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x3, [x29,120]
	ldr	x0, [x29,184]
	ldr	x2, [x3,2536]
	ldr	w0, [x0,4]
	str	w0, [x29,152]
	add	x4, x2, x24
	ldr	w0, [x2,x24]
	cmn	w0, #1
	bne	.L2530
	b	.L2531
.L2529:
	ldr	w0, [x29,220]
	ldr	w1, [x29,216]
	cmp	w1, w0
	bne	.L2521
	mov	w0, w27
	add	x1, x29, 212
	mov	w2, 1
	bl	log2phys
	b	.L2521
.L2530:
	ldr	x0, [x29,184]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2531
	ldr	x0, [x29,144]
	ldr	w1, [x29,152]
	str	x2, [x29,104]
	ldr	w0, [x0,1212]
	str	x3, [x29,112]
	str	x4, [x29,120]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,120]
	ldr	x3, [x29,112]
	ldr	x2, [x29,104]
	cbz	w0, .L2531
	ldr	w0, [x29,216]
	ldr	w1, [x29,220]
	cmp	w0, w1
	beq	.L2536
	ldr	w1, [x29,212]
	cmp	w0, w1
	beq	.L2531
	cmn	w0, #1
	beq	.L2534
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,2536]
	str	x1, [x29,184]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2535
.L2534:
	str	w0, [x2,x24]
.L2535:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2536
	ldr	x0, [x29,184]
	ldr	w24, [x0,4]
	add	x0, x25, :lo12:.LANCHOR5
	mov	w1, w24
	ldr	w0, [x0,1212]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2536
	ldr	w0, [x29,152]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2531
.L2536:
	ldr	w1, [x29,212]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2531:
	mov	w0, -1
	str	w0, [x29,212]
	b	.L2538
.L2528:
	ldr	w0, [x29,220]
	ldr	w1, [x29,216]
	cmp	w1, w0
	beq	.L2538
	cmn	w28, #1
	beq	.L2540
	add	x0, x20, :lo12:.LANCHOR2
	ubfx	x28, x28, 10, 21
	ldr	w0, [x0,2308]
	cmp	w28, w0
	bcs	.L2521
.L2540:
	mov	w0, w27
	add	x1, x29, 220
	mov	w2, 1
	bl	log2phys
	ldr	w28, [x29,216]
	cmn	w28, #1
	beq	.L2538
	ldr	w0, [x29,212]
	cmp	w28, w0
	beq	.L2538
	lsr	x0, x28, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	ldr	x1, [x29,168]
	ldrh	w1, [x1,800]
	cmp	w1, w0
	beq	.L2542
	ldr	x1, [x29,168]
	ldrh	w1, [x1,848]
	cmp	w1, w0
	beq	.L2542
	ldr	x1, [x29,168]
	ldrh	w1, [x1,896]
	cmp	w1, w0
	bne	.L2538
.L2542:
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x24, x0, :lo12:.LANCHOR2
	mov	w2, 0
	ldr	x0, [x24,2536]
	str	w28, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x24,2536]
	str	x3, [x29,184]
	bl	FlashReadPages
	ldr	x0, [x24,2536]
	ldr	x3, [x29,184]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2538
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2538
	mov	w0, w27
	add	x1, x29, 216
	mov	w2, 1
	bl	log2phys
.L2538:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L2521
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,2728]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2543
.L2636:
	bl	decrement_vpc_count
	b	.L2521
.L2543:
	ldr	x0, [x29,128]
	bl	printk
	b	.L2521
.L2525:
	add	x24, x25, :lo12:.LANCHOR5
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x24,1258]
	ldr	x0, [x29,136]
	bl	printk
	ldr	w0, [x24,1264]
	cmp	w0, 31
	bhi	.L2544
	add	x1, x24, 1272
	ldr	w2, [x29,220]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x24,1264]
.L2544:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x25, :lo12:.LANCHOR5
	ldr	w1, [x0,1212]
	cmn	w1, #1
	beq	.L2637
	cmp	w1, w22
	bls	.L2521
.L2637:
	str	w22, [x0,1212]
.L2521:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L2520
.L2645:
	ldrb	w0, [x19,8]
	add	w23, w23, 1
	cmp	w0, 1
	uxth	w23, w23
	bne	.L2547
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x0,1228]
	cbz	w1, .L2547
	cmp	w21, w23
	bne	.L2547
	ldrh	w0, [x0,2362]
	cmp	w0, w23
	beq	.L2523
.L2547:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,2360]
	cmp	w23, w1
	bne	.L2515
	ldrh	w2, [x0,2292]
	mov	w3, 65535
	strh	w23, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2549:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2632
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2549
	strb	w1, [x19,6]
	b	.L2632
.L2523:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2639:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2632:
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
	cbz	w1, .L2646
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2646
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	adrp	x22, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,x0,lsl 1]
.L2650:
	cmp	w0, w20
	bne	.L2657
.L2652:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,2292]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2651
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2651:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2650
.L2657:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2653
	add	x0, x22, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbnz	w0, .L2653
	ldrh	w2, [x19,2]
	add	x0, x21, 1160
	ldrh	w0, [x0,w2,sxtw 1]
	cmp	w0, w20
	bne	.L2653
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2652
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2646
.L2653:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w2, [x0,1228]
	cbz	w2, .L2646
	cmp	w1, 1
	bne	.L2646
	ldrh	w2, [x19,2]
	ldrh	w1, [x0,2362]
	cmp	w2, w1
	bcc	.L2646
	ldrh	w1, [x19]
	ldr	x3, [x0,2728]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,2360]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2646:
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
.L2659:
	cmp	w0, w22
	adrp	x20, .LANCHOR2
	bne	.L2674
.L2660:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,2292]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2661
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2661:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2659
.L2674:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2662
	add	x1, x23, :lo12:.LANCHOR2
	ldrb	w1, [x1,1228]
	cbnz	w1, .L2662
	ldrh	w2, [x19,2]
	add	x1, x21, 1160
	ldrh	w1, [x1,w2,sxtw 1]
	cmp	w1, w22
	bne	.L2662
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2660
.L2662:
	ldrh	w1, [x19,4]
	mov	w23, 65535
	ldrh	w22, [x19,2]
	adrp	x21, .LANCHOR0
	sub	w1, w1, #1
	mov	w24, w23
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	add	x21, x21, :lo12:.LANCHOR0
.L2663:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,2292]
.L2665:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2664
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2664:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2665
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2670
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbnz	w0, .L2667
	ldrh	w1, [x19,2]
	add	x0, x21, 1160
	ldrh	w0, [x0,w1,sxtw 1]
	cmp	w0, w24
	bne	.L2667
	ldrh	w1, [x19,4]
	cbz	w1, .L2667
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2663
.L2667:
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w0, [x1,1228]
	cbz	w0, .L2670
	ldrh	w2, [x19,2]
	ldrh	w0, [x1,2362]
	cmp	w2, w0
	bcc	.L2670
	ldrh	w2, [x19]
	ldr	x0, [x1,2728]
	lsl	x2, x2, 1
	ldrh	w4, [x19,4]
	ldrh	w3, [x0,x2]
	sub	w3, w3, w4
	strh	w3, [x0,x2]
	strh	wzr, [x19,4]
	ldrh	w0, [x1,2360]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2670:
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
	ldr	w0, [x20,2944]
	cbnz	w0, .L2675
	adrp	x19, .LANCHOR4
	add	x1, x19, :lo12:.LANCHOR4
	add	x1, x1, 800
	ldrh	w2, [x1,4]
	cbz	w2, .L2677
	ldrb	w0, [x1,8]
	cbnz	w0, .L2677
	ldrb	w0, [x1,7]
	ldrh	w3, [x20,2360]
	mul	w0, w0, w3
	cmp	w2, w0
	beq	.L2677
	ldrb	w24, [x1,10]
	ldr	w23, [x20,2940]
	ldrh	w25, [x20,2292]
	cbnz	w24, .L2675
	sub	w23, w23, #1
	add	x1, x29, 84
	mov	w0, w23
	mov	w2, w24
	bl	log2phys
	ldr	x21, [x20,2656]
	ldr	w0, [x29,84]
	ldr	x1, [x20,2592]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w23, [x29,112]
	str	x1, [x29,96]
	str	x21, [x29,104]
	str	w24, [x21,4]
	beq	.L2679
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w24
	bl	FlashReadPages
	b	.L2680
.L2679:
	ldr	x0, [x20,2592]
	mov	w1, 255
	ldrh	w2, [x20,2370]
	bl	ftl_memset
.L2680:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w0, -3947
	lsl	w25, w25, 2
	strh	w0, [x21]
	mov	w24, 0
	add	x20, x20, 800
.L2681:
	cmp	w24, w25
	beq	.L2682
	ldrh	w0, [x20,4]
	cbz	w0, .L2682
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
	ldr	w1, [x0,2416]
	str	w1, [x21,4]
	add	w1, w1, 1
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x0,2416]
	mov	w1, 1
	add	x0, x29, 88
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2681
.L2682:
	add	x19, x19, :lo12:.LANCHOR4
	mov	w0, 1
	strb	w0, [x19,810]
	b	.L2675
.L2677:
	add	x19, x19, :lo12:.LANCHOR4
	strb	wzr, [x19,810]
.L2675:
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
.L2693:
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w0, [x1,2398]
	cmp	w0, w19
	bls	.L2696
	ldr	x1, [x1,2808]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2694
	mov	w0, w19
	bl	flush_l2p_region
.L2694:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2693
.L2696:
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
	ldr	w1, [x2,2944]
	cbnz	w1, .L2697
	ldrb	w21, [x2,1228]
	cbz	w21, .L2712
	ldrb	w21, [x0,8]
	cmp	w21, 1
	bne	.L2708
	ldrh	w23, [x0,4]
	b	.L2699
.L2708:
	mov	w21, w1
.L2712:
	mov	w23, 12
.L2699:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x22, [x0,2656]
	ldr	x1, [x0,2592]
	str	x1, [x29,80]
	mov	w1, -3
	str	x22, [x29,88]
	str	w1, [x22,8]
	mov	w1, -2
	str	w1, [x22,12]
	ldrh	w1, [x19]
	strh	w1, [x22,2]
	strh	wzr, [x22]
	ldr	x1, [x0,2592]
	str	w2, [x1]
	ldr	x1, [x0,2592]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2700:
	sub	w23, w23, #1
	cmn	w23, #1
	beq	.L2703
	ldrh	w0, [x19,4]
	cbnz	w0, .L2701
.L2703:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	ldr	x2, [x20,2728]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x20,2360]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2697
.L2701:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2703
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w3, 0
	ldr	w0, [x1,2416]
	str	w0, [x22,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,2416]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2700
.L2697:
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
	adrp	x0, .LC47
	add	x1, x1, :lo12:.LANCHOR3
	add	x29, sp, 0
	add	x1, x1, 96
	add	x0, x0, :lo12:.LC47
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	adrp	x22, .LANCHOR2
	bl	printk
	mov	w19, 0
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, 0
	ldrh	w2, [x0,2302]
	ldr	x0, [x0,2720]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2714:
	add	x20, x22, :lo12:.LANCHOR2
	ldr	w0, [x20,2940]
	cmp	w19, w0
	bcs	.L2730
	mov	w0, w19
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L2715
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x20,2720]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2715:
	add	w19, w19, 1
	b	.L2714
.L2730:
	adrp	x23, .LANCHOR4
	adrp	x24, .LC152
	mov	w19, 0
	mov	w25, 65535
	add	x23, x23, :lo12:.LANCHOR4
	add	x24, x24, :lo12:.LC152
.L2717:
	add	x21, x22, :lo12:.LANCHOR2
	ldrh	w0, [x21,2300]
	cmp	w0, w19
	bls	.L2731
	ubfiz	x20, x19, 1, 16
	ldr	x0, [x21,2728]
	ldrh	w2, [x0,x20]
	ldr	x0, [x21,2720]
	cmp	w2, w25
	ldrh	w3, [x0,x20]
	beq	.L2718
	cmp	w2, w3
	beq	.L2718
	ldrh	w0, [x23,800]
	cmp	w0, w19
	beq	.L2718
	ldrh	w0, [x23,896]
	cmp	w0, w19
	beq	.L2718
	ldrh	w0, [x23,848]
	cmp	w0, w19
	beq	.L2718
	mov	w1, w19
	mov	x0, x24
	bl	printk
	ldr	x0, [x21,2728]
	ldrh	w1, [x0,x20]
	cbnz	w1, .L2719
	ldr	x1, [x21,2720]
	ldrh	w1, [x1,x20]
	strh	w1, [x0,x20]
	b	.L2718
.L2719:
	ldr	x1, [x21,2720]
	ldrh	w1, [x1,x20]
	strh	w1, [x0,x20]
	mov	w0, w19
	bl	update_vpc_list
.L2718:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2717
.L2731:
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
	ldr	w2, [x1,2944]
	ldrh	w22, [x0]
	cbnz	w2, .L2733
	mov	x19, x0
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2734
	ubfiz	x0, x22, 1, 16
	ldr	x1, [x1,2728]
	ldrh	w0, [x1,x0]
	cbz	w0, .L2735
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L2734
.L2735:
	mov	w0, w22
	bl	INSERT_FREE_LIST
.L2734:
	adrp	x0, .LANCHOR4
	strb	wzr, [x19,8]
	add	x1, x0, :lo12:.LANCHOR4
	add	x2, x1, 848
	cmp	x19, x2
	beq	.L2736
	add	x2, x20, :lo12:.LANCHOR2
	ldrh	w3, [x2,2312]
	cmp	w3, 1
	beq	.L2736
	ldrb	w4, [x2,1228]
	cbz	w4, .L2737
.L2736:
	mov	w0, 1
	strb	w0, [x19,8]
	b	.L2738
.L2737:
	add	x1, x1, 800
	cmp	x19, x1
	bne	.L2738
	cmp	w3, 3
	beq	.L2740
	ldr	w1, [x2,2408]
	cmp	w1, 1
	bne	.L2741
.L2740:
	add	x1, x0, :lo12:.LANCHOR4
	mov	w2, 1
	strb	w2, [x1,808]
.L2741:
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x1,2196]
	cbz	w2, .L2738
	ldr	w1, [x1,2460]
	cmp	w1, 39
	bhi	.L2738
	add	x0, x0, :lo12:.LANCHOR4
	mov	w1, 1
	strb	w1, [x0,808]
.L2738:
	adrp	x1, .LANCHOR5
	mov	w2, 65535
	add	x0, x1, :lo12:.LANCHOR5
	mov	x21, x1
	ldrh	w0, [x0,1200]
	cmp	w0, w2
	beq	.L2743
	cmp	w22, w0
	bne	.L2744
	add	x20, x20, :lo12:.LANCHOR2
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x20,2728]
	ldrh	w1, [x2,x1]
	cbz	w1, .L2745
.L2744:
	bl	update_vpc_list
.L2745:
	add	x1, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x1,1200]
.L2743:
	mov	x0, x19
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2733:
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
	add	x2, x3, 656
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR2
	ldr	w0, [x2,80]
	cbz	w0, .L2759
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w4, [x2,84]
	ldr	w1, [x0,2940]
	cmp	w4, w1
	bcs	.L2760
	mov	w21, 2048
	mov	x20, x3
.L2765:
	add	x22, x20, 656
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x22,84]
	ldr	w1, [x1,2940]
	cmp	w0, w1
	bcs	.L2764
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2763
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
	bne	.L2764
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2764:
	mov	w0, -1
	b	.L2767
.L2763:
	subs	w21, w21, #1
	bne	.L2765
	b	.L2764
.L2760:
	ldr	w0, [x0,2440]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2774
.L2759:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w5, 10000
	mov	w8, 63
	ldr	w6, [x2,76]
	ldr	w3, [x19,2456]
	ldr	w7, [x19,2440]
	cmp	w3, w5
	mov	w5, 31
	csel	w8, w5, w8, hi
	add	w5, w7, 1048576
	cmp	w6, w5
	ldr	w4, [x19,2940]
	adrp	x5, .LANCHOR4
	bhi	.L2769
	mov	w9, 1000
	lsr	w3, w3, 10
	add	w3, w3, 1
	mul	w4, w4, w9
	udiv	w3, w4, w3
	add	w3, w3, w6
	cmp	w3, w7
	bcc	.L2769
	add	x3, x5, :lo12:.LANCHOR4
	ldrh	w3, [x3,740]
	tst	w8, w3
	bne	.L2767
	ldr	w2, [x2,100]
	cmp	w2, w3
	beq	.L2767
.L2769:
	add	x5, x5, :lo12:.LANCHOR4
	add	x0, x1, :lo12:.LANCHOR5
	ldrh	w1, [x5,740]
	str	w1, [x0,756]
	mov	w1, 1
	str	wzr, [x0,740]
	str	w1, [x0,736]
	str	w7, [x0,732]
.L2774:
	mov	w0, 0
.L2767:
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
	ldr	w3, [x4,2944]
	cbnz	w3, .L2928
	adrp	x3, .LANCHOR1
	add	x3, x3, :lo12:.LANCHOR1
	ldr	w5, [x3,3352]
	cmn	w5, #1
	beq	.L2928
	ldr	w4, [x4,2464]
	cbnz	w4, .L2928
	adrp	x23, .LANCHOR4
	add	x4, x23, :lo12:.LANCHOR4
	ldrh	w5, [x4,776]
	cmp	w5, 47
	bls	.L2928
	mov	w24, w1
	ldrh	w1, [x3,3356]
	str	w0, [x29,132]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2777
.L2780:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,2490]
	cmp	w4, w2
	bne	.L2778
	b	.L2779
.L2777:
	ldrh	w1, [x4,896]
	cmp	w1, w0
	beq	.L2780
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2780
	b	.L2928
.L2778:
	ldrh	w1, [x0,2488]
	cmp	w1, w2
	bne	.L2779
	ldrh	w3, [x0,2492]
	cmp	w3, w1
	beq	.L2779
	ldrh	w2, [x0,2494]
	cmp	w2, w1
	beq	.L2779
	mov	w1, -1
	strh	w4, [x0,2488]
	strh	w3, [x0,2490]
	strh	w2, [x0,2492]
	strh	w1, [x0,2494]
.L2779:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,132]
	adrp	x20, .LANCHOR5
	cmp	w2, 1
	ldr	w0, [x1,2480]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,2480]
	bne	.L2791
	ldr	w2, [x1,2196]
	cbnz	w2, .L2782
	ldrb	w1, [x1,1228]
	cbz	w1, .L2791
.L2782:
	add	x21, x19, :lo12:.LANCHOR2
	ldr	w1, [x21,2460]
	cmp	w1, 39
	bhi	.L2791
	add	x22, x20, :lo12:.LANCHOR5
	ldrh	w1, [x22,1400]
	add	w0, w0, w1
	str	w0, [x21,2480]
	bl	FtlGcReFreshBadBlk
	ldrh	w0, [x22,608]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2791
	ldrh	w1, [x21,2488]
	cmp	w1, w0
	bne	.L2791
	ldr	w0, [x21,2480]
	cmp	w0, 1024
	bhi	.L2783
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	cmp	w0, 63
	bhi	.L2791
.L2783:
	add	x1, x23, :lo12:.LANCHOR4
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w3, [x1,792]
	add	x1, x19, :lo12:.LANCHOR2
	strh	wzr, [x0,1400]
	ldrh	w2, [x1,2498]
	add	w2, w2, 64
	cmp	w3, w2
	bgt	.L2791
	str	wzr, [x1,2480]
	ldr	w1, [x1,2460]
	cbnz	w1, .L2784
	mov	w1, 6
	b	.L2950
.L2784:
	cmp	w1, 5
	bhi	.L2785
	mov	w1, 18
.L2950:
	strh	w1, [x0,1400]
.L2785:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2790
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,2500]
	cbz	w0, .L2787
	ldrh	w1, [x22,2362]
	ubfiz	x25, x25, 1, 16
	ldrh	w3, [x22,2292]
	ldr	x26, [x22,2728]
	mul	w1, w1, w3
	ldrh	w2, [x26,x25]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2790
	add	w1, w0, 1
	str	wzr, [x22,2468]
	uxth	w1, w1
	strh	w1, [x22,2500]
	str	x1, [x29,136]
	bl	List_get_gc_head_node
	uxth	w21, w0
	cmp	w21, w27
	ldr	x1, [x29,136]
	beq	.L2790
	ubfiz	x27, x21, 1, 16
	adrp	x0, .LC153
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC153
	mov	w2, w21
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x22,2500]
	cmp	w0, 40
	bls	.L2788
	ldr	x0, [x22,2728]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2788
	strh	wzr, [x22,2500]
.L2788:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, 6
	strh	w1, [x0,1400]
	b	.L2792
.L2787:
	mov	w0, 1
	strh	w0, [x22,2500]
.L2790:
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	mov	w0, 65535
	cmp	w21, w0
	bne	.L2792
	add	x0, x20, :lo12:.LANCHOR5
	strh	wzr, [x0,1400]
.L2791:
	add	x1, x20, :lo12:.LANCHOR5
	mov	w0, 65535
	ldrh	w21, [x1,608]
	cmp	w21, w0
	bne	.L2955
	add	x26, x23, :lo12:.LANCHOR4
	ldrh	w0, [x26,896]
	cmp	w0, w21
	beq	.L2956
.L2792:
	mov	w0, 65535
	cmp	w21, w0
	cset	w0, eq
	cbz	w0, .L2805
	ldr	w1, [x29,132]
	cbnz	w1, .L2805
	add	x0, x23, :lo12:.LANCHOR4
	mov	w22, 1
	ldrh	w0, [x0,792]
	cmp	w0, 24
	bhi	.L2806
	add	x1, x19, :lo12:.LANCHOR2
	cmp	w0, 16
	ldrh	w22, [x1,2360]
	bls	.L2807
	lsr	w22, w22, 5
	b	.L2806
.L2807:
	cmp	w0, 12
	bls	.L2808
	lsr	w22, w22, 4
	b	.L2806
.L2808:
	cmp	w0, 8
	bls	.L2806
	lsr	w22, w22, 2
.L2806:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,2496]
	cmp	w2, w0
	bcs	.L2810
	add	x3, x23, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w0, [x3,896]
	cmp	w0, w2
	bne	.L2811
	ldrh	w2, [x1,2488]
	cmp	w2, w0
	bne	.L2811
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x0,1400]
	cbnz	w2, .L2812
	ldr	w0, [x1,2940]
	ldr	w3, [x3,1084]
	add	w0, w0, w0, lsl 1
	cmp	w3, w0, lsr 2
	bcs	.L2813
.L2812:
	add	x20, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x20,604]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	b	.L2953
.L2813:
	mov	w0, 18
.L2953:
	add	x19, x19, :lo12:.LANCHOR2
	strh	w0, [x1,2496]
	str	wzr, [x19,2468]
	b	.L2928
.L2811:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,604]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,2496]
.L2810:
	cmp	w24, 2
	bhi	.L2882
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2196]
	cbz	w0, .L2882
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2882
.L2805:
	add	x2, x23, :lo12:.LANCHOR4
	mov	w3, 65535
	ldrh	w1, [x2,896]
	cmp	w1, w3
	bne	.L2817
	cbz	w0, .L2817
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w3, [x0,2488]
	cmp	w3, w1
	bne	.L2817
	add	x3, x20, :lo12:.LANCHOR5
	ldrh	w4, [x3,608]
	cmp	w4, w1
	beq	.L2818
.L2823:
	mov	w21, 65535
	b	.L2817
.L2818:
	ldrh	w4, [x2,792]
	ldrh	w1, [x0,2496]
	str	wzr, [x0,2468]
	cmp	w4, w1
	bls	.L2819
	ldrh	w1, [x3,1400]
	cbnz	w1, .L2820
	ldr	w1, [x0,2940]
	ldr	w2, [x2,1084]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2821
.L2820:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,604]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,2496]
	b	.L2822
.L2821:
	mov	w1, 18
	strh	w1, [x0,2496]
.L2822:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,2728]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2819
	add	x20, x20, :lo12:.LANCHOR5
	b	.L2954
.L2819:
	add	x24, x20, :lo12:.LANCHOR5
	ldrh	w0, [x24,1400]
	cbnz	w0, .L2823
	ldrh	w21, [x24,604]
	add	x22, x19, :lo12:.LANCHOR2
	add	w1, w21, w21, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,2496]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x22,2728]
	ldrh	w2, [x22,2292]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x22,2362]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L2824
	add	x0, x23, :lo12:.LANCHOR4
	sub	w21, w21, #1
	ldrh	w0, [x0,792]
	cmp	w0, w21
	blt	.L2824
	bl	FtlReadRefresh
	ldrh	w2, [x24,1400]
	b	.L2928
.L2824:
	cbnz	w1, .L2823
	mov	w0, -1
	add	x23, x23, :lo12:.LANCHOR4
	bl	decrement_vpc_count
	ldrh	w2, [x23,792]
	add	w2, w2, 1
	b	.L2928
.L2817:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 2
	ldr	w0, [x0,2196]
	cmp	w0, wzr
	csinc	w22, w22, wzr, ne
	b	.L2816
.L2882:
	mov	w21, 65535
.L2816:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w2, 65535
	ldrh	w1, [x0,608]
	cmp	w1, w2
	bne	.L2826
	cmp	w21, w1
	beq	.L2827
	strh	w21, [x0,608]
	b	.L2828
.L2827:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,2488]
	cmp	w0, w21
	beq	.L2828
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1,2728]
	ldrh	w0, [x2,x0]
	cbnz	w0, .L2829
	mov	w0, -1
	strh	w0, [x1,2488]
.L2829:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x20, :lo12:.LANCHOR5
	ldrh	w2, [x1,2488]
	strh	w2, [x0,608]
	mov	w0, -1
	strh	w0, [x1,2488]
.L2828:
	add	x24, x20, :lo12:.LANCHOR5
	mov	w1, 65535
	ldrh	w0, [x24,608]
	strb	wzr, [x24,616]
	cmp	w0, w1
	beq	.L2826
	bl	IsBlkInGcList
	cbz	w0, .L2831
	mov	w0, -1
	strh	w0, [x24,608]
.L2831:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L2832
	add	x0, x20, :lo12:.LANCHOR5
	add	x24, x0, 608
	ldrh	w0, [x0,608]
	bl	ftl_get_blk_mode
	strb	w0, [x24,8]
.L2832:
	add	x24, x20, :lo12:.LANCHOR5
	mov	w0, 65535
	add	x25, x24, 608
	ldrh	w1, [x24,608]
	cmp	w1, w0
	beq	.L2826
	mov	x0, x25
	bl	make_superblock
	strh	wzr, [x24,610]
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x24,608]
	strb	wzr, [x24,614]
	strh	wzr, [x24,1402]
	ldr	x0, [x0,2728]
	ldrh	w0, [x0,x1,lsl 1]
	strh	w0, [x24,1404]
.L2826:
	add	x0, x20, :lo12:.LANCHOR5
	add	x1, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,608]
	ldrh	w2, [x1,800]
	cmp	w2, w0
	beq	.L2833
	ldrh	w2, [x1,848]
	cmp	w2, w0
	beq	.L2833
	ldrh	w1, [x1,896]
	cmp	w1, w0
	bne	.L2834
.L2833:
	add	x0, x20, :lo12:.LANCHOR5
	mov	w1, -1
	strh	w1, [x0,608]
.L2834:
	add	x25, x20, :lo12:.LANCHOR5
.L2867:
	ldrh	w0, [x25,608]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2835
	add	x0, x19, :lo12:.LANCHOR2
	str	wzr, [x0,2468]
.L2836:
	add	x26, x19, :lo12:.LANCHOR2
	ldrh	w24, [x26,2500]
	mov	w0, w24
	bl	List_get_gc_head_node
	uxth	w27, w0
	mov	w0, 65535
	strh	w27, [x25,608]
	cmp	w27, w0
	bne	.L2837
	strh	wzr, [x26,2500]
	mov	w2, 8
	b	.L2928
.L2837:
	mov	w0, w27
	add	w24, w24, 1
	bl	IsBlkInGcList
	cbz	w0, .L2838
	strh	w24, [x26,2500]
	b	.L2836
.L2838:
	ldrh	w5, [x26,2292]
	ubfiz	x1, x27, 1, 16
	ldrh	w0, [x26,2360]
	uxth	w24, w24
	ldr	x2, [x26,2728]
	mov	w3, 2
	strh	w24, [x26,2500]
	mul	w0, w0, w5
	ldrh	w4, [x2,x1]
	sdiv	w5, w0, w3
	cmp	w4, w5
	bgt	.L2840
	cmp	w4, 8
	bls	.L2841
	cmp	w24, 48
	bls	.L2841
	ldrh	w4, [x25,1216]
	cmp	w4, 35
	bhi	.L2841
.L2840:
	add	x4, x19, :lo12:.LANCHOR2
	strh	wzr, [x4,2500]
.L2841:
	ldrh	w1, [x2,x1]
	mov	w2, 65535
	cmp	w21, w2
	bne	.L2842
	cmp	w1, w0
	blt	.L2842
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,2500]
	cmp	w2, 3
	bhi	.L2842
	add	x20, x20, :lo12:.LANCHOR5
	mov	w1, -1
	strh	wzr, [x0,2500]
	strh	w1, [x20,608]
.L2954:
	ldrh	w2, [x20,1400]
	b	.L2928
.L2842:
	cbnz	w1, .L2843
	mov	w0, -1
	bl	decrement_vpc_count
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,2500]
	add	w0, w0, 1
	strh	w0, [x1,2500]
	b	.L2836
.L2843:
	add	x0, x19, :lo12:.LANCHOR2
	strb	wzr, [x25,616]
	ldrb	w0, [x0,1228]
	cbz	w0, .L2844
	mov	w0, w27
	bl	ftl_get_blk_mode
	strb	w0, [x25,616]
.L2844:
	add	x24, x25, 608
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x25,1402]
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x25,608]
	ldr	x0, [x0,2728]
	ldrh	w0, [x0,x1,lsl 1]
	strh	w0, [x25,1404]
	strh	wzr, [x25,610]
	strb	wzr, [x25,614]
.L2835:
	ldr	w0, [x29,132]
	cmp	w0, 1
	bne	.L2845
	bl	FtlReadRefresh
.L2845:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	str	w1, [x0,2464]
	ldrh	w1, [x0,2360]
	str	w1, [x29,136]
	ldrb	w1, [x0,1228]
	cbz	w1, .L2846
	ldrb	w1, [x25,616]
	cmp	w1, 1
	bne	.L2846
	ldrh	w0, [x0,2362]
	str	w0, [x29,136]
.L2846:
	ldrh	w0, [x25,610]
	ldr	w2, [x29,136]
	add	w1, w0, w22
	cmp	w1, w2
	ble	.L2847
	sub	w22, w2, w0
	uxth	w22, w22
.L2847:
	mov	w27, 0
.L2848:
	cmp	w22, w27, uxth
	bls	.L2855
	add	x1, x25, 608
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w4, [x1,2]
	ldrh	w7, [x0,2292]
	mov	x0, 0
	add	w4, w4, w27
	mov	w24, w0
.L2856:
	cmp	w7, w0, uxth
	bls	.L2957
	add	x2, x1, x0, lsl 1
	mov	w3, 65535
	ldrh	w2, [x2,16]
	cmp	w2, w3
	beq	.L2849
	add	x6, x19, :lo12:.LANCHOR2
	mov	w3, 56
	orr	w2, w4, w2, lsl 10
	umull	x5, w24, w3
	ldr	x6, [x6,2568]
	add	w24, w24, 1
	add	x5, x6, x5
	uxth	w24, w24
	str	w2, [x5,4]
.L2849:
	add	x0, x0, 1
	b	.L2856
.L2957:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w2, [x25,616]
	mov	w1, w24
	mov	x28, 0
	ldr	x0, [x0,2568]
	bl	FlashReadPages
	add	x0, x23, :lo12:.LANCHOR4
	str	x0, [x29,120]
.L2851:
	cmp	w24, w28, uxth
	bls	.L2958
	add	x26, x19, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x5, x28, x0
	ldr	x0, [x26,2568]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x6, [x1,16]
	beq	.L2885
	ldrh	w0, [x6]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2885
	ldr	w0, [x6,8]
	add	x1, x29, 152
	mov	w2, 0
	str	x5, [x29,104]
	str	x6, [x29,112]
	bl	log2phys
	ldr	x2, [x26,2568]
	ldr	x5, [x29,104]
	ldr	w0, [x29,152]
	add	x2, x2, x5
	ldr	x6, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2885
	ldrh	w0, [x25,1402]
	mov	x3, 56
	ldr	x1, [x26,2544]
	add	w0, w0, 1
	strh	w0, [x25,1402]
	ldr	w0, [x25,1184]
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
	ldr	w0, [x25,1184]
	ldr	x1, [x26,2544]
	madd	x0, x0, x2, x1
	ldr	x1, [x26,2568]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,152]
	str	w0, [x6,12]
	ldr	x0, [x29,120]
	add	x2, x0, 896
	str	x2, [x29,112]
	ldrh	w0, [x0,896]
	strh	w0, [x6,2]
	ldr	w0, [x26,2416]
	str	w0, [x6,4]
	ldr	w0, [x25,1184]
	add	w0, w0, 1
	str	w0, [x25,1184]
	ldr	x0, [x26,2568]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	ldrb	w0, [x26,1228]
	cbnz	w0, .L2853
	ldr	x2, [x29,112]
	ldr	w1, [x25,1184]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2853
	ldrh	w0, [x2,4]
	cbnz	w0, .L2885
.L2853:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2885
	add	x20, x20, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, -1
	str	wzr, [x19,2464]
	strh	w0, [x20,608]
	strh	wzr, [x20,610]
	b	.L2954
.L2885:
	add	x28, x28, 1
	b	.L2851
.L2958:
	add	w27, w27, 1
	b	.L2848
.L2855:
	ldrh	w0, [x25,610]
	add	w22, w22, w0
	ldr	w0, [x29,136]
	uxth	w22, w22
	strh	w22, [x25,610]
	cmp	w22, w0
	bcc	.L2857
	ldr	w0, [x25,1184]
	cbz	w0, .L2858
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2858
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x25,1400]
	str	wzr, [x19,2464]
	b	.L2928
.L2858:
	ldrh	w22, [x25,1402]
	cbnz	w22, .L2859
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x25,608]
	ldr	x0, [x0,2728]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L2859
.L2860:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2940]
	cmp	w22, w0
	bcs	.L2865
	mov	w0, w22
	add	x1, x29, 156
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,156]
	cmn	w0, #1
	beq	.L2861
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x25,608]
	cmp	w1, w0, uxth
	bne	.L2861
.L2865:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2940]
	cmp	w22, w1
	bcc	.L2859
	ldrh	w1, [x25,608]
	ldr	x0, [x0,2728]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x25,608]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2859
.L2861:
	add	w22, w22, 1
	b	.L2860
.L2859:
	mov	w0, -1
	strh	w0, [x25,608]
.L2857:
	add	x0, x23, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	cmp	w0, 2
	bhi	.L2866
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w22, [x0,2360]
	b	.L2867
.L2866:
	add	x20, x20, :lo12:.LANCHOR5
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w2, [x20,1400]
	str	wzr, [x19,2464]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2928
.L2956:
	add	x25, x19, :lo12:.LANCHOR2
	ldrh	w27, [x25,2488]
	cmp	w27, w21
	bne	.L2792
	ldrh	w0, [x26,792]
	mov	w3, 1024
	ldr	w2, [x25,2480]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w3, cc
	cmp	w2, w0
	bls	.L2792
	str	wzr, [x25,2480]
	strh	wzr, [x1,1400]
	bl	GetSwlReplaceBlock
	uxth	w21, w0
	cmp	w21, w27
	bne	.L2794
	ldrh	w1, [x26,792]
	ldrh	w0, [x25,2498]
	cmp	w1, w0
	bcs	.L2795
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w21
	beq	.L2804
	ldr	w2, [x25,2404]
	uxtw	x1, w0
	cbnz	w2, .L2797
	ldrh	w0, [x25,2312]
	cmp	w0, 3
	beq	.L2797
	ldr	w0, [x25,2408]
	cbnz	w0, .L2797
	ldr	w0, [x25,2196]
	cbnz	w0, .L2797
	ldrb	w0, [x25,1228]
	cbz	w0, .L2798
.L2797:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2728]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x2,2362]
	ldrh	w0, [x2,2292]
	ldrh	w2, [x2,2312]
	mul	w3, w1, w0
	cmp	w2, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w3, w0
	cmp	w4, w0
	bgt	.L2800
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x1, x19, :lo12:.LANCHOR2
	add	x2, x23, :lo12:.LANCHOR4
	ldr	w0, [x1,2940]
	ldr	w2, [x2,1084]
	add	w0, w0, w0, lsl 1
	cmp	w2, w0, lsr 2
	bls	.L2801
	mov	w0, 128
	b	.L2951
.L2801:
	mov	w0, 160
.L2951:
	strh	w0, [x1,2498]
	b	.L2802
.L2800:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2952
.L2798:
	ldr	x2, [x25,2728]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2803
	bl	List_get_gc_head_node
	uxth	w21, w0
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	strh	w1, [x0,2498]
	b	.L2802
.L2803:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2952:
	strh	w1, [x0,2498]
	b	.L2804
.L2795:
	mov	w0, 80
	strh	w0, [x25,2498]
	b	.L2804
.L2802:
	mov	w0, 65535
	cmp	w21, w0
	beq	.L2804
.L2794:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x21, 1, 32
	add	x2, x23, :lo12:.LANCHOR4
	adrp	x0, .LC154
	add	x0, x0, :lo12:.LC154
	mov	w1, w21
	ldr	x3, [x5,2728]
	ldr	x6, [x5,2712]
	ldrh	w2, [x2,792]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,2496]
	ldrh	w4, [x6,x4]
	bl	printk
.L2804:
	bl	FtlGcReFreshBadBlk
	b	.L2792
.L2955:
	mov	w21, w0
	b	.L2792
.L2928:
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
	ldr	x20, [x0,1408]
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w22, [x0,2944]
	cbnz	w22, .L2961
	ldr	w1, [x0,2512]
	cbz	w1, .L2961
	ldrb	w0, [x0,1228]
	cbz	w0, .L2963
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2963:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w3, [x20,9]
	adrp	x26, .LC155
	mov	w2, w22
	mov	w24, 0
	mov	w27, 56
	ldr	x0, [x0,2576]
	add	x26, x26, :lo12:.LC155
	bl	FlashProgPages
.L2964:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2512]
	cmp	w24, w1
	bcs	.L2982
	umull	x21, w24, w27
	ldr	x1, [x0,2576]
	add	x0, x1, x21
	ldr	w25, [x1,x21]
	cmn	w25, #1
	beq	.L2985
	ldr	w1, [x0,4]
	cbz	w22, .L2999
	orr	w1, w1, -2147483648
.L2999:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,124]
	add	x25, x19, :lo12:.LANCHOR2
	add	x1, x29, 124
	bl	log2phys
	ldr	x0, [x25,2576]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2968
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x25,2728]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2969
	mov	x0, x26
	bl	printk
.L2969:
	mov	w0, w21
	bl	decrement_vpc_count
.L2968:
	add	w24, w24, 1
	b	.L2964
.L3001:
	mov	w20, 16386
.L2981:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2502]
	cbz	w0, .L2982
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L2981
.L2982:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,2512]
	b	.L2961
.L2985:
	adrp	x26, .LC155
	add	x0, x23, :lo12:.LANCHOR5
	mov	w24, 0
	mov	w27, 56
	str	x0, [x29,104]
	add	x26, x26, :lo12:.LC155
.L2965:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,2512]
	cmp	w24, w1
	bcs	.L3001
	umull	x21, w24, w27
	ldr	x0, [x0,2576]
	mov	w28, 1
	str	w25, [x0,x21]
.L2971:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2576]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L3002
	ldr	w0, [x1,4]
	str	x2, [x29,96]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	ldr	x2, [x29,96]
	bne	.L2972
	ldr	x3, [x2,2728]
	ubfiz	x1, x1, 1, 16
	ldrh	w4, [x20,4]
	ldrh	w0, [x3,x1]
	sub	w0, w0, w4
	strh	w0, [x3,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x2,2360]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2972:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2973
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2973:
	ldr	x0, [x29,104]
	add	x23, x19, :lo12:.LANCHOR2
	add	x1, x0, 656
	ldr	w0, [x0,752]
	add	w0, w0, 1
	str	w0, [x1,96]
	ldr	x0, [x23,2576]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,124]
	ldr	x1, [x23,2576]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x23,2576]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x23,2576]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2974
	str	w28, [x23,2944]
.L2974:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2944]
	cbz	w0, .L2971
	b	.L2961
.L3002:
	ldr	w0, [x1,4]
	cbz	w22, .L3000
	orr	w0, w0, -2147483648
.L3000:
	str	w0, [x29,124]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x28, x19, :lo12:.LANCHOR2
	add	x1, x29, 124
	bl	log2phys
	ldr	x0, [x28,2576]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2978
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x28,2728]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2979
	mov	x0, x26
	bl	printk
.L2979:
	mov	w0, w21
	bl	decrement_vpc_count
.L2978:
	add	w24, w24, 1
	b	.L2965
.L2961:
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
	adrp	x0, .LANCHOR2+2944
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+2944]
	cbnz	w0, .L3004
	adrp	x0, .LANCHOR1+3352
	ldr	w0, [x0,#:lo12:.LANCHOR1+3352]
	cmn	w0, #1
	beq	.L3004
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3004:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3352
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+3352]
	cmp	w0, 1
	bne	.L3009
	bl	FtlSysFlush
.L3009:
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
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR2
	str	x23, [sp,48]
	add	x21, x22, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	mov	w19, w1
	mov	w20, w0
	ldr	w1, [x21,1232]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3013
	adrp	x0, .LANCHOR1+3352
	ldr	w1, [x0,#:lo12:.LANCHOR1+3352]
	cmn	w1, #1
	mov	w0, w1
	beq	.L3013
	cmp	w19, 31
	bls	.L3028
	ldr	w0, [x21,2944]
	cbnz	w0, .L3028
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,2366]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3016
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L3016:
	mov	w0, -1
	adrp	x23, .LANCHOR5
	str	w0, [x29,76]
.L3017:
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w0, [x20,2366]
	cmp	w19, w0
	bcc	.L3029
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3018
	add	x1, x23, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,1416]
	add	w0, w0, 1
	str	w0, [x1,1416]
	ldr	w0, [x20,2428]
	add	x1, x29, 76
	add	w0, w0, 1
	str	w0, [x20,2428]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3018:
	add	x0, x22, :lo12:.LANCHOR2
	add	w21, w21, 1
	ldrh	w0, [x0,2366]
	sub	w19, w19, w0
	b	.L3017
.L3029:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,1416]
	cmp	w1, 32
	bls	.L3028
	str	wzr, [x0,1416]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3028:
	mov	w0, 0
.L3013:
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
	ldr	w3, [x2,2944]
	ldrh	w1, [x2,2360]
	cbnz	w3, .L3066
	adrp	x20, .LANCHOR4
	mov	w4, 65535
	add	x2, x20, :lo12:.LANCHOR4
	ldrh	w21, [x2,896]
	cmp	w21, w4
	bne	.L3033
.L3042:
	add	x22, x20, :lo12:.LANCHOR4
	adrp	x21, .LANCHOR5
	add	x23, x21, :lo12:.LANCHOR5
	mov	w0, 65535
	add	x24, x22, 896
	ldrh	w1, [x22,896]
	str	wzr, [x23,1208]
	cmp	w1, w0
	beq	.L3066
	bl	FtlCacheWriteBack
	mov	w25, 12
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x24,7]
	ldrh	w3, [x22,896]
	mov	w24, 0
	ldrh	w4, [x0,2360]
	ldr	x2, [x0,2728]
	mul	w1, w1, w4
	strh	w1, [x2,x3,lsl 1]
	ldrh	w2, [x23,1218]
	mov	x23, x22
	ldr	w1, [x0,2420]
	add	w1, w2, w1
	str	w1, [x0,2420]
	b	.L3043
.L3033:
	cbz	w0, .L3036
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3356]
	cmp	w5, w4
	beq	.L3037
.L3038:
	mov	w1, 2
	b	.L3036
.L3037:
	strh	w3, [x0,3356]
	ldrh	w0, [x2,792]
	cmp	w0, 17
	bhi	.L3038
.L3036:
	add	x0, x20, :lo12:.LANCHOR4
	add	x0, x0, 896
	bl	FtlGcScanTempBlk
	str	w0, [x29,108]
	cmn	w0, #1
	beq	.L3039
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x21, x21, 1, 16
	ldr	x1, [x19,2712]
	ldrh	w0, [x1,x21]
	cmp	w0, 4
	bls	.L3040
	sub	w0, w0, #5
	strh	w0, [x1,x21]
	mov	w0, 1
	bl	FtlEctTblFlush
.L3040:
	adrp	x0, .LANCHOR5
	add	x1, x0, :lo12:.LANCHOR5
	mov	x19, x0
	ldr	w2, [x1,1208]
	cbnz	w2, .L3041
	ldr	w0, [x1,752]
	add	w0, w0, 1
	str	w0, [x1,752]
	ldr	w0, [x29,108]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3041:
	add	x0, x19, :lo12:.LANCHOR5
	str	wzr, [x0,1208]
	mov	w0, 1
	b	.L3032
.L3039:
	adrp	x0, .LANCHOR1+3356
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3356]
	mov	w0, 1
	cmp	w2, w1
	bne	.L3032
	b	.L3042
.L3046:
	ldr	w1, [x22,4]
	cmp	w0, w1
	bne	.L3064
.L3045:
	add	w24, w24, 1
	uxth	w24, w24
.L3043:
	add	x0, x21, :lo12:.LANCHOR5
	ldrh	w0, [x0,1218]
	cmp	w0, w24
	bls	.L3067
	add	x1, x19, :lo12:.LANCHOR2
	umull	x26, w24, w25
	ldr	x27, [x1,2528]
	ldr	w1, [x1,2940]
	add	x22, x27, x26
	ldr	w0, [x22,8]
	cmp	w0, w1
	bcs	.L3064
	add	x1, x29, 108
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,108]
	ldr	w1, [x27,x26]
	cmp	w0, w1
	bne	.L3046
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w26, w0
	ldr	w0, [x22,8]
	add	x1, x22, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w26
	b	.L3065
.L3064:
	ldrh	w0, [x23,896]
.L3065:
	bl	decrement_vpc_count
	b	.L3045
.L3067:
	mov	w0, -1
	bl	decrement_vpc_count
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L3048
	add	x1, x20, :lo12:.LANCHOR4
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	ldrh	w1, [x1,896]
	bl	printk
.L3048:
	add	x0, x20, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,896]
	ldr	x1, [x1,2728]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L3049
	bl	INSERT_DATA_LIST
	b	.L3050
.L3049:
	bl	INSERT_FREE_LIST
.L3050:
	add	x22, x21, :lo12:.LANCHOR5
	add	x23, x20, :lo12:.LANCHOR4
	mov	w24, -1
	strh	wzr, [x22,1218]
	strh	w24, [x23,896]
	strh	wzr, [x22,1216]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w24, [x22,608]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,2196]
	cbz	w0, .L3051
	ldr	w0, [x1,2460]
	cmp	w0, 39
	bhi	.L3051
	ldrh	w0, [x22,604]
	ldrh	w2, [x23,792]
	cmp	w2, w0
	bcs	.L3066
	lsl	w0, w0, 1
	strh	w0, [x1,2496]
.L3066:
	mov	w0, 0
	b	.L3032
.L3051:
	add	x21, x21, :lo12:.LANCHOR5
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w1, [x21,604]
	ldrh	w0, [x20,792]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L3066
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,1228]
	cbz	w0, .L3052
	sub	w1, w1, #2
	strh	w1, [x19,2496]
	b	.L3066
.L3052:
	mov	w1, 20
	strh	w1, [x19,2496]
.L3032:
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
	add	x21, x19, 896
	mov	x0, x21
	ldrh	w1, [x20,2360]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,898]
	ldrh	w0, [x20,2360]
	cmp	w1, w0
	bcc	.L3068
	add	x0, x19, 944
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, .LANCHOR5+1208
	str	wzr, [x0,#:lo12:.LANCHOR5+1208]
.L3068:
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
	adrp	x0, .LANCHOR5+1264
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR4
	str	wzr, [x0,#:lo12:.LANCHOR5+1264]
	add	x19, x19, :lo12:.LANCHOR4
	add	x20, x19, 800
	add	x19, x19, 848
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
	strh	w1, [x0,1258]
	strh	wzr, [x0,1260]
	ldrh	w0, [x23,2296]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	x0, x19, :lo12:.LANCHOR4
	ldrh	w1, [x0,1088]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3072
.L3074:
	mov	w22, -1
	b	.L3073
.L3072:
	bl	FtlLoadSysInfo
	mov	w22, w0
	cbnz	w0, .L3074
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x23,2398]
	mov	x1, 0
	ldr	x0, [x23,2808]
.L3075:
	cmp	w1, w2
	mov	w3, w1
	bge	.L3079
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L3075
.L3079:
	add	x0, x19, :lo12:.LANCHOR4
	cmp	w3, w2
	ldrh	w1, [x0,740]
	add	w1, w1, 1
	strh	w1, [x0,740]
	blt	.L3076
	add	x21, x21, :lo12:.LANCHOR5
	ldrh	w0, [x21,1260]
	cbz	w0, .L3080
.L3076:
	add	x1, x19, :lo12:.LANCHOR4
	add	x2, x20, :lo12:.LANCHOR2
	ldrh	w3, [x1,800]
	ldr	x5, [x2,2728]
	lsl	x3, x3, 1
	ldrh	w6, [x1,804]
	ldrh	w4, [x5,x3]
	sub	w4, w4, w6
	strh	w4, [x5,x3]
	strb	wzr, [x1,806]
	ldrh	w3, [x2,2360]
	strh	w3, [x1,802]
	ldrh	w3, [x1,848]
	ldr	x5, [x2,2728]
	lsl	x3, x3, 1
	strh	wzr, [x1,804]
	ldrh	w6, [x1,852]
	ldrh	w4, [x5,x3]
	sub	w4, w4, w6
	strh	w4, [x5,x3]
	strb	wzr, [x1,854]
	ldrh	w0, [x1,742]
	ldrh	w2, [x2,2360]
	add	w0, w0, 1
	strh	w2, [x1,850]
	strh	wzr, [x1,852]
	strh	w0, [x1,742]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3080:
	add	x20, x19, :lo12:.LANCHOR4
	mov	w1, 65535
	add	x23, x20, 800
	ldrh	w0, [x20,800]
	cmp	w0, w1
	beq	.L3081
	ldrh	w1, [x20,804]
	cbnz	w1, .L3081
	ldrh	w1, [x20,852]
	add	x21, x20, 848
	cbnz	w1, .L3081
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,848]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x23
	bl	allocate_new_data_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3081:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbnz	w0, .L3082
	add	x19, x19, :lo12:.LANCHOR4
	ldrh	w0, [x19,740]
	and	w0, w0, 31
	cbnz	w0, .L3073
.L3082:
	bl	FtlVpcCheckAndModify
.L3073:
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
	ldr	w21, [x20,2944]
	cbnz	w21, .L3090
	ldrh	w2, [x20,2396]
	mov	w1, w21
	ldr	x0, [x20,2792]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,2396]
	mov	w1, w21
	ldr	x0, [x20,2784]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w21, [x20,2412]
	ldrh	w0, [x20,2296]
	str	w21, [x20,2416]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3091
	bl	FtlMakeBbt
.L3091:
	mov	w0, 0
.L3092:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,2366]
	cmp	w0, w2, lsl 7
	bge	.L3119
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,2632]
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	str	w2, [x4,x3]
	add	w0, w0, 1
	ldr	x2, [x1,2640]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	str	w1, [x2,x3]
	b	.L3092
.L3119:
	ldrh	w21, [x1,2300]
	mov	w20, 0
.L3094:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,2302]
	cmp	w0, w21
	bls	.L3120
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3094
.L3120:
	ldrh	w0, [x22,2292]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L3096
.L3100:
	mov	w20, 0
	mov	w21, w20
	b	.L3097
.L3096:
	udiv	w20, w20, w0
	ldr	w0, [x22,2392]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22,2296]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x22,2300]
.L3098:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2302]
	cmp	w0, w20
	bls	.L3100
	mov	w0, w20
	mov	w1, 1
	add	w20, w20, 1
	bl	FtlLowFormatEraseBlock
	uxth	w20, w20
	b	.L3098
.L3097:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,2300]
	cmp	w1, w21
	bls	.L3121
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3097
.L3121:
	ldrh	w1, [x0,2302]
	adrp	x22, .LANCHOR5
	ldrh	w2, [x0,2292]
	add	x6, x22, :lo12:.LANCHOR5
	str	w1, [x0,2484]
	ldr	w1, [x0,2304]
	udiv	w3, w1, w2
	str	w3, [x0,2940]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,604]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w20, w4
	ble	.L3102
	sub	w1, w1, w20
	udiv	w1, w1, w2
	str	w1, [x0,2940]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,604]
.L3102:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,2196]
	cmp	w0, 1
	bne	.L3103
	add	x1, x22, :lo12:.LANCHOR5
	udiv	w0, w20, w2
	ldrh	w4, [x1,604]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,604]
.L3103:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w0, [x0,1228]
	cbz	w0, .L3104
	add	x1, x22, :lo12:.LANCHOR5
	udiv	w0, w20, w2
	ldrh	w4, [x1,604]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,604]
.L3104:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w1, [x6,2354]
	cbz	w1, .L3106
	add	x0, x22, :lo12:.LANCHOR5
	ldrh	w4, [x0,604]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,604]
	mul	w4, w1, w2
	cmp	w4, w20
	ble	.L3106
	add	w1, w1, 32
	str	w3, [x6,2940]
	add	w1, w5, w1
	strh	w1, [x0,604]
.L3106:
	add	x21, x22, :lo12:.LANCHOR5
	add	x23, x19, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR4
	mov	w24, -1
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w0, [x21,604]
	ldr	w1, [x23,2940]
	sub	w0, w1, w0
	ldrh	w1, [x23,2360]
	mul	w0, w0, w2
	str	w0, [x21,600]
	mul	w0, w0, w1
	ldrh	w1, [x23,2366]
	str	w0, [x23,2940]
	mul	w0, w0, w1
	str	w0, [x23,1232]
	bl	FtlBbmTblFlush
	ldrh	w2, [x23,2302]
	mov	w1, 0
	ldr	x0, [x23,2728]
	lsl	w2, w2, 1
	bl	ftl_memset
	strh	w24, [x21,608]
	strh	wzr, [x21,610]
	mov	w0, 1
	strb	wzr, [x21,614]
	mov	w1, 255
	strb	wzr, [x21,616]
	add	x21, x20, 800
	ldrh	w2, [x23,2300]
	strb	w0, [x21,8]
	adrp	x0, .LANCHOR0+16
	lsr	w2, w2, 3
	str	wzr, [x20,1084]
	strh	wzr, [x21,2]
	ldr	x0, [x0,#:lo12:.LANCHOR0+16]
	strb	wzr, [x21,6]
	strh	wzr, [x20,800]
	bl	ftl_memset
.L3108:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L3109
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldr	x0, [x0,2728]
	strh	w24, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L3108
.L3109:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x21]
	ldrh	w3, [x21,4]
	mov	w23, -1
	ldr	w0, [x1,2412]
	str	w0, [x21,12]
	add	w0, w0, 1
	str	w0, [x1,2412]
	ldr	x0, [x1,2728]
	strh	w3, [x0,x2,lsl 1]
	add	x0, x20, 848
	strh	wzr, [x20,850]
	ldrh	w1, [x21]
	mov	x21, x0
	strb	wzr, [x20,854]
	add	w1, w1, 1
	strh	w1, [x20,848]
	mov	w1, 1
	strb	w1, [x20,856]
.L3110:
	mov	x0, x21
	bl	make_superblock
	ldrb	w0, [x21,7]
	cbnz	w0, .L3111
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldr	x0, [x0,2728]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x21]
	add	w0, w0, 1
	strh	w0, [x21]
	b	.L3110
.L3111:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21]
	ldrh	w2, [x21,4]
	add	x22, x22, :lo12:.LANCHOR5
	ldr	w0, [x19,2412]
	str	w0, [x21,12]
	mov	w21, -1
	add	w0, w0, 1
	str	w0, [x19,2412]
	ldr	x0, [x19,2728]
	strh	w2, [x0,x1,lsl 1]
	strh	w21, [x20,896]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x20,1088]
	ldr	w0, [x22,600]
	strh	w0, [x20,1094]
	ldr	w0, [x19,2412]
	str	w0, [x20,1096]
	add	w0, w0, 1
	strh	wzr, [x20,1090]
	strh	w21, [x20,1092]
	str	w0, [x19,2412]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L3090
	adrp	x0, .LANCHOR1+3352
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+3352]
.L3090:
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
	ldrb	w0, [x0,1228]
	cbz	w0, .L3123
.L3125:
	mov	w0, 0
	b	.L3124
.L3123:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0,#:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	cbnz	w0, .L3125
	bl	FlashLoadFactorBbt
	cbz	w0, .L3126
	bl	FlashMakeFactorBbt
.L3126:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2232]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3127
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L3129:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L3129
	cmp	w0, 6
	bhi	.L3130
	adrp	x0, .LANCHOR0+89
	strb	w1, [x0,#:lo12:.LANCHOR0+89]
	b	.L3131
.L3130:
	mov	w1, 0
	mov	w4, 1
.L3133:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L3133
	cmp	w0, 17
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	bls	.L3152
	mov	w1, 36
.L3152:
	strb	w1, [x0,89]
.L3131:
	adrp	x1, .LANCHOR0+89
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w1, [x1,#:lo12:.LANCHOR0+89]
	strh	w1, [x0,1226]
.L3127:
	adrp	x0, .LC45
	adrp	x1, .LC140
	add	x1, x1, :lo12:.LC140
	add	x0, x0, :lo12:.LC45
	add	x19, x19, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, 1200
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x19,2296]
	mov	w19, 1
	bl	FtlFreeSysBlkQueueInit
.L3135:
	bl	FtlLoadBbt
	cbz	w0, .L3136
.L3154:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3155
	add	w19, w19, 1
	b	.L3135
.L3155:
	mov	w0, -1
	b	.L3124
.L3136:
	bl	FtlSysBlkInit
	cbnz	w0, .L3154
	adrp	x1, .LANCHOR1+3352
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+3352]
.L3124:
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
	ldr	w1, [x0,2944]
	cbz	w1, .L3157
.L3160:
	mov	w0, 0
	b	.L3158
.L3157:
	ldrb	w0, [x0,1228]
	adrp	x22, .LANCHOR5
	cbz	w0, .L3159
	add	x0, x22, :lo12:.LANCHOR5
	ldr	w0, [x0,1184]
	tbz	x0, 0, .L3159
	adrp	x0, .LANCHOR4+900
	ldrh	w0, [x0,#:lo12:.LANCHOR4+900]
	cbnz	w0, .L3160
.L3159:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x22, :lo12:.LANCHOR5
	mov	w2, 0
	mov	w21, 0
	mov	w3, w2
	mov	w23, 56
	ldr	x0, [x0,2544]
	ldr	w1, [x1,1184]
	bl	FlashProgPages
.L3161:
	add	x20, x22, :lo12:.LANCHOR5
	ldr	w1, [x20,1184]
	cmp	w21, w1
	bcs	.L3171
	add	x2, x19, :lo12:.LANCHOR2
	umull	x0, w21, w23
	ldr	x4, [x2,2544]
	add	x1, x4, x0
	ldr	w4, [x4,x0]
	cmn	w4, #1
	ldr	x3, [x1,16]
	bne	.L3162
	adrp	x1, .LANCHOR4
	ldr	x3, [x2,2728]
	add	x1, x1, :lo12:.LANCHOR4
	ldrh	w5, [x1,896]
	strh	wzr, [x3,x5,lsl 1]
	strh	w4, [x1,896]
	ldr	w1, [x20,752]
	add	w1, w1, 1
	str	w1, [x20,752]
	ldr	x1, [x2,2544]
	add	x0, x1, x0
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3170
.L3162:
	ldr	w0, [x3,12]
	add	w21, w21, 1
	ldr	w1, [x1,4]
	ldr	w2, [x3,8]
	uxth	w21, w21
	bl	FtlGcUpdatePage
	b	.L3161
.L3171:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x19,2544]
	bl	FtlGcBufFree
	str	wzr, [x20,1184]
	adrp	x0, .LANCHOR4+900
	ldrh	w0, [x0,#:lo12:.LANCHOR4+900]
	cbnz	w0, .L3160
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L3170:
	mov	w0, 1
.L3158:
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
	add	x1, x0, 896
	ldrh	w2, [x0,896]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3173
	ldrh	w0, [x1,4]
	cbnz	w0, .L3174
.L3173:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 896
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	strh	wzr, [x0,1216]
	strh	wzr, [x0,1218]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3174:
	add	x0, x19, :lo12:.LANCHOR4
	add	x0, x0, 896
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
	adrp	x0, .LANCHOR1+3352
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	ldr	w5, [x0,#:lo12:.LANCHOR1+3352]
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmn	w5, #1
	mov	w0, w5
	beq	.L3176
	cmp	w4, 16
	mov	x23, x3
	mov	w26, w2
	mov	w20, w1
	bne	.L3177
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3176
.L3177:
	adrp	x19, .LANCHOR2
	add	w0, w1, w2
	add	x1, x19, :lo12:.LANCHOR2
	str	w0, [x29,188]
	mov	w0, -1
	ldr	w3, [x29,188]
	ldr	w2, [x1,1232]
	cmp	w3, w2
	bhi	.L3176
	adrp	x21, .LANCHOR5
	ldrh	w0, [x1,2366]
	add	x2, x21, :lo12:.LANCHOR5
	sub	w25, w3, #1
	udiv	w28, w20, w0
	udiv	w25, w25, w0
	ldr	w0, [x2,1172]
	add	w0, w26, w0
	sub	w24, w25, w28
	str	w0, [x2,1172]
	add	w24, w24, 1
	ldr	w0, [x1,2440]
	add	w0, w24, w0
	str	w0, [x1,2440]
	mov	w0, w28
	mov	w1, w25
	bl	FtlCacheMetchLpa
	str	x21, [x29,120]
	cbz	w0, .L3178
	bl	FtlCacheWriteBack
.L3178:
	mov	w27, 0
	adrp	x0, .LC54
	add	x0, x0, :lo12:.LC54
	mov	w21, w28
	str	w27, [x29,184]
	mov	w22, w27
	str	w27, [x29,172]
	str	x0, [x29,112]
.L3179:
	cbz	w24, .L3222
	mov	w0, w21
	add	x1, x29, 204
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,204]
	cmn	w3, #1
	bne	.L3220
	mov	w3, 0
.L3180:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2366]
	cmp	w3, w0
	bcs	.L3184
	madd	w0, w21, w0, w3
	cmp	w0, w20
	bcc	.L3182
	ldr	w1, [x29,188]
	cmp	w0, w1
	bcs	.L3182
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x23, x0
	str	x3, [x29,176]
	bl	ftl_memset
	ldr	x3, [x29,176]
.L3182:
	add	w3, w3, 1
	b	.L3180
.L3220:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w21, w28
	umull	x1, w22, w0
	ldr	x0, [x2,2536]
	add	x0, x0, x1
	str	w3, [x0,4]
	ldrh	w0, [x2,2366]
	bne	.L3185
	ldr	x3, [x2,2536]
	ldr	x2, [x2,2632]
	add	x3, x3, x1
	str	x2, [x3,8]
	udiv	w2, w20, w0
	msub	w2, w2, w0, w20
	str	w2, [x29,144]
	sub	w2, w0, w2
	cmp	w2, w26
	csel	w2, w2, w26, ls
	str	w2, [x29,184]
	cmp	w2, w0
	bne	.L3186
	str	x23, [x3,8]
	b	.L3186
.L3185:
	cmp	w21, w25
	bne	.L3187
	ldr	x3, [x2,2536]
	ldr	x2, [x2,2640]
	add	x3, x3, x1
	ldr	w4, [x29,188]
	str	x2, [x3,8]
	mul	w2, w21, w0
	sub	w27, w4, w2
	cmp	w27, w0
	bne	.L3186
	sub	w2, w2, w20
	ubfiz	x2, x2, 9, 23
	add	x2, x23, x2
	str	x2, [x3,8]
	b	.L3186
.L3187:
	ldr	x2, [x2,2536]
	mul	w0, w21, w0
	add	x2, x2, x1
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x23, x0
	str	x0, [x2,8]
.L3186:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,2536]
	add	x1, x0, x1
	ldrh	w0, [x2,2372]
	ldr	x2, [x2,2664]
	mul	w0, w22, w0
	str	w21, [x1,24]
	add	w22, w22, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3184:
	subs	w24, w24, #1
	add	w21, w21, 1
	beq	.L3188
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,2292]
	cmp	w22, w0, lsl 3
	bne	.L3179
.L3188:
	cbz	w22, .L3179
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, 0
	ldr	x0, [x0,2536]
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
.L3190:
	ldr	w0, [x29,176]
	cmp	w22, w0
	bls	.L3223
	ldr	x0, [x29,176]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	mul	x8, x0, x1
	ldr	x0, [x2,2536]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w28
	bne	.L3191
	ldr	x1, [x0,8]
	ldr	x0, [x2,2632]
	cmp	x1, x0
	bne	.L3192
	ldr	x2, [x29,136]
	mov	x0, x23
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,168]
	b	.L3221
.L3191:
	cmp	w1, w25
	bne	.L3192
	ldr	x1, [x0,8]
	ldr	x0, [x2,2640]
	cmp	x1, x0
	bne	.L3192
	ldrh	w0, [x2,2366]
	ldr	w2, [x29,132]
	str	x8, [x29,104]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x23, x0
.L3221:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3192:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	add	x2, x0, x8
	ldr	w1, [x0,x8]
	cmn	w1, #1
	bne	.L3193
	ldr	x0, [x29,160]
	str	w1, [x29,172]
	add	x3, x0, 656
	ldr	w0, [x0,728]
	add	w0, w0, 1
	str	w0, [x3,72]
.L3193:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3194
	ldr	x0, [x29,160]
	str	x8, [x29,104]
	add	x1, x0, 656
	ldr	w0, [x0,728]
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
.L3194:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2536]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3195
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3195:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L3190
.L3223:
	mov	w22, 0
	b	.L3179
.L3222:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,2502]
	cbz	w0, .L3198
	mov	w0, w24
	mov	w1, 1
	bl	ftl_do_gc
.L3198:
	ldr	w0, [x29,172]
.L3176:
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
	adrp	x1, .LC140
	add	x29, sp, 0
	stp	x23, x24, [sp,48]
	adrp	x23, .LANCHOR1
	add	x1, x1, :lo12:.LC140
	add	x23, x23, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x19, x20, [sp,16]
	adrp	x22, .LANCHOR2
	adrp	x20, .LANCHOR5
	add	x21, x22, :lo12:.LANCHOR2
	str	w0, [x23,3352]
	add	x20, x20, :lo12:.LANCHOR5
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	str	wzr, [x21,2944]
	str	wzr, [x20,1420]
	bl	printk
	add	x0, x21, 1200
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x21,2296]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3227
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
	b	.L3241
.L3227:
	bl	FtlSysBlkInit
	mov	w19, w0
	cbz	w0, .L3229
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
.L3241:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 120
	bl	printk
	b	.L3228
.L3229:
	mov	w1, 1
	str	w1, [x23,3352]
	bl	ftl_do_gc
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x24, x1
	ldrh	w0, [x0,792]
	cmp	w0, 15
	bhi	.L3230
	mov	w23, 65535
	add	w21, w0, 2
.L3233:
	ldrh	w0, [x20,608]
	cmp	w0, w23
	bne	.L3231
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,2488]
	cmp	w0, w23
	bne	.L3231
	and	w0, w19, 63
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3231:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	add	x0, x24, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	cmp	w0, w21
	bhi	.L3228
	add	w19, w19, 1
	cmp	w19, 4096
	bne	.L3233
	b	.L3228
.L3230:
	ldrb	w0, [x21,1228]
	cbz	w0, .L3228
	mov	w19, 128
.L3235:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3235
.L3228:
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
	ldr	w1, [x2,2944]
	cbnz	w1, .L3243
	adrp	x1, .LANCHOR1
	str	x1, [x29,176]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,3352]
	cmn	w3, #1
	beq	.L3243
	cmp	w4, 16
	bne	.L3244
	add	w0, w23, 256
	mov	w1, w24
	mov	x2, x25
	bl	FtlVendorPartWrite
	b	.L3243
.L3244:
	ldr	w1, [x2,1232]
	add	w3, w23, w24
	mov	w0, -1
	cmp	w3, w1
	bhi	.L3243
	adrp	x4, .LANCHOR5
	ldrh	w1, [x2,2366]
	add	x21, x4, :lo12:.LANCHOR5
	sub	w3, w3, #1
	mov	w0, 2048
	str	x4, [x29,216]
	udiv	w26, w23, w1
	cmp	w24, w1, lsl 1
	str	w0, [x21,1424]
	udiv	w0, w3, w1
	ldr	w3, [x2,2512]
	str	w0, [x29,208]
	sub	w27, w0, w26
	add	w20, w27, 1
	ldr	w0, [x2,2424]
	add	w0, w20, w0
	str	w0, [x2,2424]
	ldr	w0, [x21,1168]
	add	w0, w24, w0
	str	w0, [x21,1168]
	cset	w0, cs
	str	w0, [x29,212]
	cbz	w3, .L3246
	mov	w0, 56
	sub	w3, w3, #1
	umull	x3, w3, w0
	ldr	x0, [x2,2576]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w26, w0
	bne	.L3247
	ldr	w0, [x2,2432]
	add	w0, w0, 1
	str	w0, [x2,2432]
	ldr	w0, [x21,1428]
	add	w0, w0, 1
	str	w0, [x21,1428]
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
	cbnz	w27, .L3248
	ldr	w0, [x21,1428]
	cmp	w0, 2
	bgt	.L3248
.L3282:
	mov	w0, 0
	b	.L3243
.L3248:
	add	x25, x25, x20
	sub	w24, w24, w19
	add	w23, w23, w19
	add	w26, w26, 1
	mov	w20, w27
.L3247:
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,1428]
.L3246:
	ldr	w1, [x29,208]
	mov	w0, w26
	bl	FtlCacheMetchLpa
	cbz	w0, .L3249
	bl	FtlCacheWriteBack
.L3249:
	ldr	x0, [x29,216]
	adrp	x2, .LANCHOR4
	add	x1, x2, :lo12:.LANCHOR4
	str	w26, [x29,192]
	add	x0, x0, :lo12:.LANCHOR5
	add	x21, x1, 800
	str	x2, [x29,128]
	str	x1, [x29,120]
	str	x21, [x0,1408]
	adrp	x0, .LC159
	add	x0, x0, :lo12:.LC159
	str	x21, [x29,184]
	str	x0, [x29,104]
.L3250:
	cbz	w20, .L3313
	ldrh	w0, [x21,4]
	cbnz	w0, .L3251
	ldr	x1, [x29,184]
	cmp	x21, x1
	bne	.L3252
	ldr	x0, [x29,120]
	add	x0, x0, 848
	ldrh	w19, [x0,4]
	cbnz	w19, .L3253
	bl	allocate_new_data_superblock
	ldr	x0, [x29,176]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3360]
.L3253:
	ldr	x0, [x29,184]
	bl	allocate_new_data_superblock
	ldr	x0, [x29,176]
	ldr	x1, [x29,120]
	add	x0, x0, :lo12:.LANCHOR1
	add	x21, x1, 848
	ldr	w0, [x0,3360]
	cbnz	w0, .L3254
.L3255:
	ldr	x21, [x29,184]
	b	.L3254
.L3252:
	ldr	x1, [x29,176]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3360]
	ldr	x0, [x29,184]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3255
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3254:
	ldrh	w0, [x21,4]
	cbnz	w0, .L3256
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3256:
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	str	x21, [x0,1408]
.L3251:
	add	x2, x22, :lo12:.LANCHOR2
	ldrh	w1, [x21,4]
	str	xzr, [x29,200]
	ldr	w0, [x2,2508]
	ldr	w2, [x2,2512]
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
.L3257:
	ldr	w0, [x29,200]
	ldr	w2, [x29,156]
	ldr	w1, [x29,192]
	cmp	w0, w2
	add	w28, w1, w0
	bcs	.L3314
	ldrh	w1, [x21,4]
	cbz	w1, .L3289
	ldr	w1, [x29,208]
	cmp	w28, w1
	cset	w6, eq
	cbz	w0, .L3259
	ldr	w1, [x29,116]
	tst	w6, w1
	beq	.L3259
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w2, [x29,152]
	ldrh	w1, [x1,2366]
	msub	w2, w28, w1, w2
	cmp	w2, w1
	bne	.L3289
.L3259:
	add	x1, x29, 228
	mov	w2, 0
	mov	w0, w28
	str	x6, [x29,96]
	add	x19, x22, :lo12:.LANCHOR2
	bl	log2phys
	mov	x0, x21
	bl	get_new_active_ppa
	ldr	w5, [x19,2512]
	mov	w4, 56
	ldr	x2, [x19,2576]
	str	x4, [x29,160]
	umull	x1, w5, w4
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x19,2372]
	ldr	x0, [x19,2576]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,144]
	ldr	x0, [x19,2680]
	ldr	x3, [x29,144]
	str	x0, [x29,136]
	add	x27, x0, x3
	ldrh	w0, [x19,2370]
	str	x27, [x1,16]
	str	w28, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x19,2624]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	mov	x0, x27
	mov	w1, 0
	bl	ftl_memset
	cmp	w28, w26
	cset	w0, eq
	str	w0, [x29,196]
	cbnz	w0, .L3293
	ldr	x6, [x29,96]
	ldr	x4, [x29,160]
	cbz	w6, .L3260
	ldr	w0, [x29,152]
	ldrh	w19, [x19,2366]
	msub	w19, w28, w19, w0
	ldr	w0, [x29,196]
	str	w0, [x29,160]
	uxth	w19, w19
	b	.L3263
.L3293:
	ldrh	w19, [x19,2366]
	udiv	w0, w23, w19
	msub	w0, w0, w19, w23
	str	w0, [x29,160]
	sub	w19, w19, w0
	cmp	w19, w24
	csel	w19, w19, w24, ls
.L3263:
	add	x2, x22, :lo12:.LANCHOR2
	ldrh	w0, [x2,2366]
	cmp	w19, w0
	bne	.L3264
	ldr	w0, [x29,196]
	mov	x1, x25
	cbnz	w0, .L3265
	mul	w1, w19, w28
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
.L3265:
	ldr	w2, [x29,212]
	mov	w0, 56
	add	x4, x22, :lo12:.LANCHOR2
	cbz	w2, .L3266
	ldr	w2, [x4,2512]
	umull	x0, w2, w0
	ldr	x2, [x4,2576]
	add	x0, x2, x0
	b	.L3311
.L3266:
	ldr	w2, [x4,2512]
	umull	x0, w2, w0
	ldr	x2, [x4,2576]
	add	x0, x2, x0
	ldrh	w2, [x4,2370]
	ldr	x0, [x0,8]
	b	.L3312
.L3264:
	ldr	w0, [x29,228]
	cmn	w0, #1
	beq	.L3268
	str	w0, [x29,236]
	mov	w1, 56
	ldr	w0, [x2,2512]
	str	w28, [x29,256]
	umull	x0, w0, w1
	ldr	x1, [x2,2576]
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
	bne	.L3269
	ldr	x0, [x29,216]
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,728]
	add	w1, w1, 1
	str	w1, [x0,728]
	b	.L3271
.L3269:
	ldr	w0, [x27,8]
	cmp	w0, w28
	beq	.L3271
	ldr	x0, [x29,216]
	mov	w2, w28
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,728]
	add	w1, w1, 1
	str	w1, [x0,728]
	ldr	x0, [x29,104]
	ldr	w1, [x27,8]
	bl	printk
	b	.L3271
.L3268:
	ldr	w0, [x2,2512]
	mov	w1, 56
	umull	x0, w0, w1
	ldr	x1, [x2,2576]
	ldrh	w2, [x2,2370]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3271:
	ldr	w1, [x29,196]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3272
	add	x1, x22, :lo12:.LANCHOR2
	ldr	w4, [x1,2512]
	umull	x0, w4, w0
	ldr	x4, [x1,2576]
	add	x4, x4, x0
	ldr	x0, [x29,160]
	ubfiz	x0, x0, 9, 23
	ldr	x1, [x4,8]
	add	x0, x1, x0
	mov	x1, x25
	b	.L3312
.L3272:
	add	x5, x22, :lo12:.LANCHOR2
	ldr	w4, [x5,2512]
	ldr	x1, [x5,2576]
	umull	x0, w4, w0
	add	x0, x1, x0
	ldrh	w1, [x5,2366]
	mul	w1, w28, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	add	x1, x25, x1
	b	.L3312
.L3260:
	ldr	w0, [x29,212]
	ldrh	w1, [x19,2366]
	ldr	w2, [x19,2512]
	cbz	w0, .L3273
	mul	w1, w28, w1
	umull	x0, w2, w4
	sub	w1, w1, w23
	ldr	x2, [x19,2576]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x25, x1
.L3311:
	str	x1, [x0,8]
	b	.L3267
.L3273:
	umull	x0, w2, w4
	ldr	x2, [x19,2576]
	mul	w1, w28, w1
	add	x0, x2, x0
	ldrh	w2, [x19,2370]
	sub	w1, w1, w23
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x25, x1
.L3312:
	bl	ftl_memcpy
.L3267:
	ldr	x1, [x29,136]
	mov	w0, -3947
	ldr	x2, [x29,144]
	strh	w0, [x1,x2]
	add	x1, x22, :lo12:.LANCHOR2
	str	w28, [x27,8]
	ldr	w0, [x1,2416]
	str	w0, [x27,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,2416]
	ldr	w0, [x29,228]
	str	w0, [x27,12]
	ldrh	w0, [x21]
	strh	w0, [x27,2]
	ldr	w0, [x1,2512]
	add	w0, w0, 1
	str	w0, [x1,2512]
	ldr	x0, [x29,200]
	add	x0, x0, 1
	str	x0, [x29,200]
	b	.L3257
.L3314:
	str	w28, [x29,192]
	mov	x0, x2
	b	.L3258
.L3289:
	str	w28, [x29,192]
.L3258:
	sub	w20, w20, w0
	ldr	w0, [x29,212]
	cbnz	w0, .L3277
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w1, [x0,2512]
	ldr	w0, [x0,2508]
	cmp	w1, w0
	bcs	.L3277
	ldrh	w0, [x21,4]
	cbz	w0, .L3277
.L3279:
	str	wzr, [x29,212]
	b	.L3250
.L3277:
	bl	FtlCacheWriteBack
	add	x0, x22, :lo12:.LANCHOR2
	cmp	w20, 1
	str	wzr, [x0,2512]
	bhi	.L3250
	b	.L3279
.L3313:
	ldr	w1, [x29,208]
	mov	w0, w20
	sub	w1, w1, w26
	bl	ftl_do_gc
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	cmp	w0, 5
	bls	.L3281
	cmp	w0, 31
	bhi	.L3282
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbnz	w0, .L3282
.L3281:
	ldr	x0, [x29,216]
	mov	w19, 65535
	mov	w23, 128
	add	x24, x0, :lo12:.LANCHOR5
.L3304:
	ldrh	w0, [x24,608]
	cmp	w0, w19
	bne	.L3283
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w1, [x0,2488]
	cmp	w1, w19
	bne	.L3283
	ldrh	w0, [x0,2490]
	cmp	w0, w19
	bne	.L3283
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3283:
	add	x21, x22, :lo12:.LANCHOR2
	mov	w0, 1
	mov	w1, w0
	strh	w23, [x21,2498]
	strh	w23, [x21,2496]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x21,2944]
	cbnz	w0, .L3282
	ldr	x0, [x29,128]
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w0, [x0,792]
	cmp	w0, 2
	bhi	.L3282
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3304
	b	.L3282
.L3243:
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
	ldrb	w1, [x0,1228]
	cbz	w1, .L3317
	adrp	x21, .LANCHOR5
	ldr	x2, [x0,2728]
	add	x1, x21, :lo12:.LANCHOR5
	adrp	x24, .LC160
	adrp	x20, .LANCHOR4
	add	x0, x24, :lo12:.LC160
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w23, [x1,1258]
	add	x26, x20, 800
	add	x25, x20, 848
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x20,800]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,848]
	mov	w20, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x26
	bl	allocate_new_data_superblock
	mov	x0, x25
	bl	allocate_new_data_superblock
.L3319:
	subs	w20, w20, #1
	beq	.L3323
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,2728]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3319
.L3323:
	add	x20, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC160
	mov	w1, w23
	ldr	x2, [x20,2728]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x20,2728]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3321
	add	x0, x29, 80
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,2292]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3324:
	cmp	w5, w0, uxth
	bls	.L3331
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3325
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,2560]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,2560]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3325:
	add	x0, x0, 1
	b	.L3324
.L3331:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC161
	mov	w1, w23
	add	x0, x0, :lo12:.LC161
	ldr	x2, [x19,2728]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,2560]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,2560]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3321:
	add	x21, x21, :lo12:.LANCHOR5
	mov	w0, -1
	strh	w0, [x21,1258]
.L3317:
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
	.type	__func__.20884, %object
	.size	__func__.20884, 11
__func__.20884:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.21627, %object
	.size	__func__.21627, 12
__func__.21627:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.21659, %object
	.size	__func__.21659, 17
__func__.21659:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.21678, %object
	.size	__func__.21678, 16
__func__.21678:
	.string	"FtlScanAllBlock"
	.type	__func__.21944, %object
	.size	__func__.21944, 17
__func__.21944:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.21926, %object
	.size	__func__.21926, 21
__func__.21926:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.20957, %object
	.size	__func__.20957, 8
__func__.20957:
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
	.string	"FtlCheckVpc2 %x = %x  %x\n"
.LC49:
	.string	"free blk vpc error %x = %x  %x\n"
.LC50:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\n"
.LC51:
	.string	":"
.LC52:
	.string	"Ftlscanalldata = %x\n"
.LC53:
	.string	"scan lpa = %x ppa= %x\n"
.LC54:
	.string	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC55:
	.string	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC56:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC57:
	.string	"Mblk:"
.LC58:
	.string	"L2P:"
.LC59:
	.string	"L2PC:"
.LC60:
	.string	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\n"
.LC61:
	.string	"superBlkID = %x vpc=%x\n"
.LC62:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC63:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC64:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC65:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC66:
	.string	"slc mode"
.LC67:
	.string	"ftl_scan_all_ppa blk %x page %x\n"
.LC68:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC69:
	.string	"%s finished\n"
.LC70:
	.string	"FLASH INFO:\n"
.LC71:
	.string	"FLASH ID: %x\n"
.LC72:
	.string	"Device Capacity: %d MB\n"
.LC73:
	.string	"FMWAIT: %x %x %x %x\n"
.LC74:
	.string	"FTL INFO:\n"
.LC75:
	.string	"g_MaxLpn = 0x%x\n"
.LC76:
	.string	"g_VaildLpn = 0x%x\n"
.LC77:
	.string	"read_page_count = 0x%x\n"
.LC78:
	.string	"discard_page_count = 0x%x\n"
.LC79:
	.string	"write_page_count = 0x%x\n"
.LC80:
	.string	"cache_write_count = 0x%x\n"
.LC81:
	.string	"l2p_write_count = 0x%x\n"
.LC82:
	.string	"gc_page_count = 0x%x\n"
.LC83:
	.string	"totle_write = %d MB\n"
.LC84:
	.string	"totle_read = %d MB\n"
.LC85:
	.string	"GSV = 0x%x\n"
.LC86:
	.string	"GDV = 0x%x\n"
.LC87:
	.string	"bad blk num = %d %d\n"
.LC88:
	.string	"free_superblocks = 0x%x\n"
.LC89:
	.string	"mlc_EC = 0x%x\n"
.LC90:
	.string	"slc_EC = 0x%x\n"
.LC91:
	.string	"avg_EC = 0x%x\n"
.LC92:
	.string	"sys_EC = 0x%x\n"
.LC93:
	.string	"max_EC = 0x%x\n"
.LC94:
	.string	"min_EC = 0x%x\n"
.LC95:
	.string	"PLT = 0x%x\n"
.LC96:
	.string	"POT = 0x%x\n"
.LC97:
	.string	"MaxSector = 0x%x\n"
.LC98:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC99:
	.string	"sys_blks_pp = 0x%x\n"
.LC100:
	.string	"free sysblock = 0x%x\n"
.LC101:
	.string	"data_blks_pp = 0x%x\n"
.LC102:
	.string	"data_op_blks_pp = 0x%x\n"
.LC103:
	.string	"max_data_blks = 0x%x\n"
.LC104:
	.string	"Sys.id = 0x%x\n"
.LC105:
	.string	"Bbt.id = 0x%x\n"
.LC106:
	.string	"ACT.page = 0x%x\n"
.LC107:
	.string	"ACT.plane = 0x%x\n"
.LC108:
	.string	"ACT.id = 0x%x\n"
.LC109:
	.string	"ACT.mode = 0x%x\n"
.LC110:
	.string	"ACT.a_pages = 0x%x\n"
.LC111:
	.string	"ACT VPC = 0x%x\n"
.LC112:
	.string	"BUF.page = 0x%x\n"
.LC113:
	.string	"BUF.plane = 0x%x\n"
.LC114:
	.string	"BUF.id = 0x%x\n"
.LC115:
	.string	"BUF.mode = 0x%x\n"
.LC116:
	.string	"BUF.a_pages = 0x%x\n"
.LC117:
	.string	"BUF VPC = 0x%x\n"
.LC118:
	.string	"TMP.page = 0x%x\n"
.LC119:
	.string	"TMP.plane = 0x%x\n"
.LC120:
	.string	"TMP.id = 0x%x\n"
.LC121:
	.string	"TMP.mode = 0x%x\n"
.LC122:
	.string	"TMP.a_pages = 0x%x\n"
.LC123:
	.string	"GC.page = 0x%x\n"
.LC124:
	.string	"GC.plane = 0x%x\n"
.LC125:
	.string	"GC.id = 0x%x\n"
.LC126:
	.string	"GC.mode = 0x%x\n"
.LC127:
	.string	"GC.a_pages = 0x%x\n"
.LC128:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC129:
	.string	"Read Err = 0x%x\n"
.LC130:
	.string	"Prog Err = 0x%x\n"
.LC131:
	.string	"gc_free_blk_th= 0x%x\n"
.LC132:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC133:
	.string	"gc_skip_write_count= 0x%x\n"
.LC134:
	.string	"gc_blk_index= 0x%x\n"
.LC135:
	.string	"free min EC= 0x%x\n"
.LC136:
	.string	"free max EC= 0x%x\n"
.LC137:
	.string	"GC__SB VPC = 0x%x\n"
.LC138:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC139:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC140:
	.string	"FTL version: 5.0.60 20200218"
.LC141:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC142:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC143:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC144:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC145:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC146:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC147:
	.string	"decrement_vpc_count %x = %d\n"
.LC148:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC149:
	.string	"RSB refresh addr %x\n"
.LC150:
	.string	"spuer block %x vpn is 0\n "
.LC151:
	.string	"g_recovery_ppa %x ver %x\n "
.LC152:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC153:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC154:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC155:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC156:
	.string	"GC des block %x done\n"
.LC157:
	.string	"...%s: no bad block mapping table, format device\n"
.LC158:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC159:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC160:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC161:
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
	.size	NandFlashParaTbl, 2656
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
	.zero	4
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.zero	1
	.zero	3
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
