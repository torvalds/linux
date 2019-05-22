/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-05-08
 * function: rk ftl v5 for rockchip soc base on arm v8 to support MLC NAND.
 */
	.cpu generic+fp+simd
	.file	"rk_ftlv5_arm_v8.S"
#APP
	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
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
	cmp	w0, 256
	add	x1, x1, :lo12:.LANCHOR0
	bls	.L36
	mov	w0, 512
	b	.L39
.L36:
	cmp	w0, 128
	bls	.L39
	mov	w0, 256
.L39:
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
	bne	.L42
	mov	w6, 6
	str	w6, [x2,2056]
	str	wzr, [x2,2052]
	str	wzr, [x2,2052]
	str	w5, [x2,2052]
	str	w4, [x2,2052]
	str	w3, [x2,2052]
	b	.L44
.L42:
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
.L44:
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
	cbz	w2, .L47
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
.L47:
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
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 24
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
.L56:
	cmp	w1, w2
	bls	.L58
	lsl	w3, w0, 5
	ldrb	w4, [x5,x2]
	add	w3, w3, w0, lsr 2
	add	x2, x2, 1
	add	w3, w3, w4
	eor	w0, w0, w3
	b	.L56
.L58:
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
	cbnz	w0, .L61
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L62:
	add	x1, x3, 136
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L62
.L68:
	add	x19, x2, :lo12:.LANCHOR0
	mov	w1, 255
	mov	w2, 1024
	add	x0, x19, 648
	uxth	w20, w20
	bl	ftl_memset
	mov	x0, 0
	mov	x2, x19
	b	.L63
.L61:
	cmp	w0, 1
	bne	.L64
	mov	x0, 0
	mov	w5, 3
	mov	w6, 2
	add	x4, x2, :lo12:.LANCHOR0
.L67:
	cmp	x0, 3
	uxth	w1, w0
	mov	w3, w1
	bls	.L65
	ubfiz	w3, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w5, w6, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L65:
	add	x1, x4, 136
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L67
	b	.L68
.L64:
	cmp	w0, 2
	bne	.L69
	mov	w1, 65535
	mov	x0, 0
	add	x5, x2, :lo12:.LANCHOR0
.L71:
	add	x4, x5, 136
	cmp	x0, 1
	uxth	w3, w0
	csel	w3, w1, w3, hi
	add	w1, w1, 2
	strh	w3, [x4,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	uxth	w1, w1
	bne	.L71
	b	.L68
.L69:
	cmp	w0, 3
	bne	.L72
	mov	x0, 0
	mov	w5, 5
	mov	w6, 4
	add	x4, x2, :lo12:.LANCHOR0
.L75:
	cmp	x0, 5
	uxth	w1, w0
	mov	w3, w1
	bls	.L73
	ubfiz	w3, w1, 1, 15
	and	w1, w1, 1
	cmp	w1, wzr
	csel	w1, w5, w6, ne
	sub	w3, w3, w1
	uxth	w3, w3
.L73:
	add	x1, x4, 136
	strh	w3, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 256
	bne	.L75
	b	.L68
.L72:
	cmp	w0, 4
	bne	.L76
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
.L78:
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
	bne	.L78
	b	.L68
.L76:
	cmp	w0, 5
	bne	.L79
	mov	x0, 0
	add	x3, x2, :lo12:.LANCHOR0
.L80:
	add	x1, x3, 136
	strh	w0, [x1,x0,lsl 1]
	add	x0, x0, 1
	cmp	x0, 16
	bne	.L80
	mov	x0, 0
.L81:
	add	x3, x1, x0
	add	w4, w0, 16
	add	x0, x0, 2
	cmp	x0, 480
	strh	w4, [x3,32]
	bne	.L81
	b	.L68
.L79:
	cmp	w0, 6
	bne	.L82
	mov	x3, 0
	mov	w6, 12
	mov	w7, 10
	add	x5, x2, :lo12:.LANCHOR0
.L85:
	cmp	x3, 5
	uxth	w4, w3
	mov	w0, w4
	bls	.L83
	add	w0, w4, w4, lsl 1
	and	w4, w4, 1
	cmp	w4, wzr
	uxth	w0, w0
	csel	w1, w6, w7, ne
	sub	w0, w0, w1
	uxth	w0, w0
.L83:
	add	x1, x5, 136
	strh	w0, [x1,x3,lsl 1]
	add	x3, x3, 1
	cmp	x3, 256
	bne	.L85
	b	.L68
.L82:
	cmp	w0, 9
	bne	.L68
	add	x0, x2, :lo12:.LANCHOR0
	add	x3, x0, 136
	strh	wzr, [x0,136]
	mov	w0, 1
	strh	w0, [x3,2]
	mov	w0, 2
	strh	w0, [x3,4]
	mov	x0, 0
.L86:
	add	x1, x3, x0
	add	w4, w0, 3
	add	x0, x0, 2
	cmp	x0, 506
	strh	w4, [x1,6]
	bne	.L86
	b	.L68
.L63:
	cmp	w20, w0, uxth
	bls	.L105
	add	x1, x2, 136
	add	x3, x2, 648
	ldrh	w1, [x1,x0,lsl 1]
	add	x0, x0, 1
	strh	w1, [x3,w1,sxtw 1]
	b	.L63
.L105:
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
.L108:
	ldrb	w0, [x24]
	cmp	w0, w20
	bls	.L114
	mov	w0, 85
	str	w0, [x22,8]
	add	x0, x19, 256
	ldrsb	w0, [x20,x0]
	str	w0, [x22,4]
	mov	x0, 1000
	bl	__const_udelay
	ldrb	w0, [x26]
	cmp	w0, 34
	bne	.L109
	ldrsb	w0, [x25,x20]
	b	.L113
.L109:
	cmp	w0, 35
	bne	.L111
	ldrsb	w0, [x21,x20]
	b	.L113
.L111:
	ldrsb	w0, [x23,400]
.L113:
	str	w0, [x22]
	add	x20, x20, 1
	b	.L108
.L114:
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
.L116:
	ldrb	w0, [x22]
	cmp	w0, w20
	bls	.L118
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
	b	.L116
.L118:
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
	strb	wzr, [x19,1672]
	add	x25, x19, 1676
	strb	wzr, [x19,1673]
	bl	FlashBlockAlignInit
	mov	w1, 0
	mov	w2, 8
	mov	x0, x25
	add	x24, x19, 1684
	adrp	x26, IDByte
	mov	x23, 0
	bl	ftl_memset
	add	x26, x26, :lo12:IDByte
	mov	w1, 0
	mov	w2, 32
	mov	x0, x24
	bl	ftl_memset
	add	x0, x19, 1716
	mov	w1, 0
	mov	w2, 192
	bl	ftl_memset
	ldr	x20, [x19,1912]
	add	x22, x20, 1
.L121:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x26, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L120
	ldrb	w1, [x19,1672]
	str	w0, [x24,w1,sxtw 2]
	add	w0, w1, 1
	strb	w23, [x25,w1,sxtw]
	strb	w0, [x19,1672]
.L120:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L121
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w0, [x19,1672]
	strb	w0, [x19,1673]
	ldrb	w0, [x20,8]
	cmp	w0, 2
	beq	.L122
.L126:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w1, [x20,13]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldrb	w0, [x21,1672]
	ldr	x27, [sp,80]
	mul	w0, w0, w1
	ldrh	w1, [x20,14]
	ldp	x19, x20, [sp,16]
	mul	w0, w0, w1
	strh	w0, [x21,1920]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 96
	ret
.L122:
	adrp	x24, IDByte
	ldr	w26, [x19,92]
	mov	x23, 0
	add	x24, x24, :lo12:IDByte
	add	x25, x19, 1684
	add	x27, x19, 1676
.L125:
	ldrb	w2, [x20]
	mov	x0, x22
	add	x1, x24, x23, lsl 3
	bl	FlashMemCmp8
	cbnz	w0, .L123
	ldrb	w1, [x20,13]
	ldrb	w3, [x19,1672]
	mul	w2, w1, w26
	ldrh	w1, [x20,14]
	mov	x0, x3
	and	w1, w1, 65280
	mul	w1, w2, w1
	str	w1, [x25,w3,sxtw 2]
	ldrb	w2, [x20,23]
	cbz	w2, .L124
	lsl	w1, w1, 1
	str	w1, [x25,w3,sxtw 2]
.L124:
	add	w0, w0, 1
	strb	w23, [x27,x3]
	strb	w0, [x19,1672]
.L123:
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L125
	b	.L126
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
	adrp	x19, .LANCHOR1
	str	x23, [sp,48]
	stp	x21, x22, [sp,32]
	adrp	x23, IDByte
	add	x21, x19, :lo12:.LANCHOR1
	add	x21, x21, 504
	mov	w20, 0
	add	x23, x23, :lo12:IDByte
.L138:
	ldrb	w2, [x21]
	add	x0, x21, 1
	mov	x1, x23
	bl	FlashMemCmp8
	mov	w22, w0
	cbnz	w0, .L135
	add	x0, x19, :lo12:.LANCHOR1
	ubfiz	x20, x20, 5, 32
	add	x1, x0, 504
	adds	x20, x1, x20
	beq	.L141
	ldrb	w3, [x20,22]
	mov	x2, 0
	mov	x1, x0
	b	.L140
.L135:
	add	w20, w20, 1
	add	x21, x21, 32
	cmp	w20, 81
	bne	.L138
	b	.L141
.L140:
	add	x4, x1, x2, lsl 5
	mov	w0, w2
	ldrb	w4, [x4,3096]
	cmp	w4, w3
	beq	.L139
	add	x2, x2, 1
	cmp	x2, 4
	bne	.L140
	mov	w0, w2
.L139:
	add	x19, x19, :lo12:.LANCHOR1
	ubfiz	x1, x0, 5, 32
	adrp	x0, .LANCHOR0
	add	x2, x19, 3096
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
	b	.L136
.L141:
	mov	w22, -1
.L136:
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
	ldrh	w9, [x6,92]
	ldrb	w6, [x6,88]
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
	bne	.L148
	add	x1, x7, :lo12:.LANCHOR0
	ldrb	w5, [x1,4044]
	cbnz	w5, .L148
	add	x1, x1, 136
	ldrh	w8, [x1,w8,sxtw 1]
.L148:
	add	x7, x7, :lo12:.LANCHOR0
	cmp	w4, 1
	add	x7, x7, 1684
	mov	w1, 0
	ldr	w5, [x7,w10,uxtw 2]
	madd	w5, w6, w9, w5
	add	w5, w5, w8
	str	w5, [x2]
	str	w10, [x3]
	bls	.L149
	ldr	w1, [x0,4]
	ldr	w0, [x0,60]
	add	w1, w1, 1024
	cmp	w1, w0
	cset	w1, eq
.L149:
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
	ldr	x0, [x4,1912]
	ldrb	w19, [x6,8]
	ldrb	w0, [x0,8]
	cmp	w0, 2
	bne	.L152
	add	x4, x4, 104
	cbnz	w2, .L153
	ldrb	w2, [x4,13]
	b	.L161
.L153:
	ldrb	w2, [x4,14]
.L161:
	add	x0, x19, 8
	add	x3, x3, :lo12:.LANCHOR0
	add	x0, x20, x0, lsl 8
	str	w2, [x0,8]
	mov	w2, 0
	ldrb	w4, [x3,119]
	cbz	w4, .L156
	add	x3, x19, 8
	lsl	x3, x3, 8
.L160:
	cmp	w2, w4
	bcs	.L156
	add	x5, x20, x3
	lsl	w0, w2, 3
	lsr	w0, w1, w0
	add	w2, w2, 1
	and	w0, w0, 255
	str	w0, [x5,4]
	b	.L160
.L152:
	add	x0, x19, 8
	mov	w1, 112
	add	x0, x20, x0, lsl 8
	str	w1, [x0,8]
.L156:
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
.L166:
	mov	w0, w19
	mov	w1, w20
	mov	w2, w21
	bl	FlashReadStatusEN
	cmp	w0, 255
	mov	w3, w0
	beq	.L166
	tbz	x3, 6, .L166
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
	ldr	x0, [x1,1912]
	ldrb	w2, [x0,9]
	ldr	w0, [x1,92]
	add	x1, x1, 1676
	mul	w0, w0, w2
	strh	w0, [x19,4]
	ldrb	w0, [x1,2369]
	strb	w0, [x19,7]
	ldr	w0, [x1,2372]
	str	w0, [x19]
	ldr	x0, [x1,236]
	ldrb	w4, [x1,-4]
	ldrb	w0, [x0,9]
	strb	w0, [x19,6]
	mov	w0, 32
	strb	w0, [x19,8]
	ldr	x0, [x1,236]
	ldrb	w0, [x0,7]
	strb	w0, [x19,9]
	mov	x0, 0
	strb	wzr, [x19,10]
.L170:
	cmp	w4, w0, uxtb
	bls	.L172
	ldrb	w2, [x0,x1]
	add	x0, x0, 1
	ldrb	w3, [x19,10]
	lsl	w2, w5, w2
	orr	w2, w2, w3
	strb	w2, [x19,10]
	b	.L170
.L172:
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
	beq	.L183
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
	b	.L185
.L183:
	and	w1, w1, -8193
.L185:
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
	stp	x29, x30, [sp, -80]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x20, x2
	adrp	x2, .LANCHOR0
	add	x19, x2, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x21, x22, [sp,32]
	uxtb	x23, w0
	uxtb	w26, w1
	ldr	x0, [x19,1912]
	mov	x25, x23
	uxtb	w24, w3
	ldrb	w0, [x0,19]
	cmp	w0, 6
	bne	.L191
	add	x19, x19, x23, lsl 6
	lsl	w4, w24, 2
	add	x19, x19, 4076
	b	.L197
.L191:
	cmp	w0, 7
	bne	.L193
	mov	x0, 160
	mov	w4, 10
	madd	x19, x23, x0, x19
	mul	w4, w24, w4
	add	x19, x19, 4084
.L197:
	add	x19, x19, x4, sxtw
	b	.L192
.L193:
	cmp	w0, 8
	bne	.L194
	add	x19, x19, 4084
	add	w4, w24, w24, lsl 2
	b	.L197
.L194:
	lsl	x4, x23, 3
	add	x4, x4, x24, sxtw
	add	x19, x19, x4, lsl 3
	add	x19, x19, 4076
.L192:
	add	x2, x2, :lo12:.LANCHOR0
	lsl	x0, x23, 4
	add	x2, x2, 24
	uxtb	x26, w26
	add	x1, x2, x0
	ldr	x21, [x2,x0]
	mov	w0, w25
	ldrb	w22, [x1,8]
	bl	NandcFlashCs
	ubfiz	x22, x22, 8, 8
	mov	w0, 54
	add	x21, x21, x22
	mov	x22, 0
	str	w0, [x21,2056]
.L195:
	cmp	x22, x26
	beq	.L198
	ldrb	w0, [x20,x22]
	str	w0, [x21,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldrsb	w0, [x19,x22]
	add	x22, x22, 1
	str	w0, [x21,2048]
	b	.L195
.L198:
	mov	w0, 22
	str	w0, [x21,2056]
	mov	w0, w25
	bl	NandcFlashDeCs
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	add	x23, x0, x23
	strb	w24, [x23,560]
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
	cmp	w0, 7
	bhi	.L199
	adrp	x21, IDByte
	add	x22, x19, 4060
	add	x21, x21, :lo12:IDByte
.L204:
	lsl	x1, x20, 3
	uxtb	w0, w20
	ldrb	w1, [x1,x21]
	cmp	w1, 173
	bne	.L201
	ldrb	w1, [x19,4057]
	mov	x2, x22
	mov	w3, 0
	bl	HynixSetRRPara
.L201:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L204
.L199:
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
	add	x0, x20, 1716
	add	x19, x0, x1
	ldrb	w21, [x0,x1]
	ldr	x0, [x19,8]
	cbz	x0, .L208
	mov	w0, w21
	add	x20, x20, 1684
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
	cbz	x1, .L208
	str	w0, [x1]
	str	xzr, [x19,16]
.L208:
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
.L216:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L217
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L216
	mov	w0, -1
	b	.L215
.L217:
	mov	w0, 0
.L215:
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
.L221:
	ldr	w0, [x20]
	str	w0, [x29,40]
	ldr	w0, [x29,40]
	tbnz	x0, 9, .L222
	mov	x0, 50
	bl	__const_udelay
	subs	w19, w19, #1
	bne	.L221
	mov	w0, -1
	b	.L220
.L222:
	mov	w0, 0
.L220:
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
	adrp	x19, .LANCHOR0
	uxtb	w20, w0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,4044]
	cbz	w0, .L225
	mov	w0, w20
	add	x19, x19, 24
	bl	NandcFlashCs
	sbfiz	x0, x20, 4, 32
	add	x1, x19, x0
	ldr	x19, [x19,x0]
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
.L225:
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
	adrp	x19, .LANCHOR0
	uxtb	w20, w0
	add	x19, x19, :lo12:.LANCHOR0
	ldrb	w0, [x19,4044]
	cbz	w0, .L230
	mov	w0, w20
	add	x19, x19, 24
	bl	NandcFlashCs
	sbfiz	x0, x20, 4, 32
	add	x1, x19, x0
	ldr	x19, [x19,x0]
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
.L230:
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
	adrp	x1, .LANCHOR2+564
	adrp	x3, .LANCHOR0
	stp	x29, x30, [sp, -16]!
	add	x3, x3, :lo12:.LANCHOR0
	adrp	x7, IDByte
	add	x29, sp, 0
	ldrb	w1, [x1,#:lo12:.LANCHOR2+564]
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
.L247:
	ldrb	w4, [x6,x7]
	ldr	x1, [x3]
	cmp	w4, 152
	ldrb	w2, [x3,8]
	beq	.L237
	cmp	w4, 69
	beq	.L237
	cmp	w4, 173
	beq	.L237
	cmp	w4, 44
	bne	.L238
.L237:
	cmp	w0, 1
	bne	.L240
	cbz	w11, .L238
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L241
	str	w0, [x1,2052]
	b	.L258
.L241:
	cmp	w4, 44
	bne	.L243
	str	w0, [x1,2052]
	str	w14, [x1,2048]
	b	.L245
.L243:
	str	w9, [x1,2052]
	str	w0, [x1,2048]
	b	.L245
.L240:
	cbz	w5, .L238
	ubfiz	x2, x2, 8, 8
	cmp	w4, 173
	add	x1, x1, x2
	str	w8, [x1,2056]
	bne	.L244
	str	w10, [x1,2052]
	str	w13, [x1,2048]
	b	.L245
.L244:
	cmp	w4, 44
	bne	.L246
	str	w10, [x1,2052]
	str	w12, [x1,2048]
	b	.L245
.L246:
	str	w9, [x1,2052]
.L258:
	str	wzr, [x1,2048]
.L245:
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
.L238:
	add	x6, x6, 8
	add	x3, x3, 16
	cmp	x6, 32
	bne	.L247
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
.L262:
	ldrb	w5, [x0]
	cmp	w5, w2
	bls	.L267
	ldrb	w5, [x4]
	cmp	w5, 67
	bne	.L263
	ldrsb	w5, [x3,x2]
	b	.L266
.L263:
	ldrsb	w5, [x1,x2]
.L266:
	str	w5, [x20]
	add	x2, x2, 1
	b	.L262
.L267:
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
	add	x25, x21, 1676
	add	x23, x23, :lo12:.LC1
.L270:
	cmp	w22, w24
	beq	.L279
	add	x2, x29, 88
	sub	w4, w24, w22
	mov	x0, x20
	mov	w1, 0
	add	x3, x29, 92
	bl	LogAddr2PhyAddr
	ldr	w2, [x29,92]
	ldrb	w0, [x21,1672]
	cmp	w2, w0
	bcc	.L271
	mov	w0, -1
	str	w0, [x20]
	b	.L272
.L271:
	uxtw	x2, w2
	mov	x0, 24
	ldrb	w19, [x25,x2]
	madd	x2, x2, x0, x21
	mov	w0, w19
	strb	w19, [x2,1716]
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
	tbz	x0, 0, .L273
	mov	w0, -1
	str	w0, [x20]
.L273:
	ldr	w0, [x20]
	cmn	w0, #1
	bne	.L274
	ldr	w1, [x29,88]
	mov	x0, x23
	bl	printk
.L274:
	mov	w0, w19
	bl	NandcFlashDeCs
.L272:
	add	w22, w22, 1
	add	x20, x20, 56
	b	.L270
.L279:
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
	bne	.L281
	mov	w0, -89
	strb	w0, [x1,4]
	adrp	x0, .LANCHOR1+3241
	mov	w1, -9
	strb	w1, [x0,#:lo12:.LANCHOR1+3241]
	b	.L344
.L281:
	cmp	w26, 3
	bne	.L283
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
	b	.L384
.L283:
	cmp	w26, 4
	bne	.L284
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
.L384:
	mov	w28, 8
	strb	w0, [x1,11]
	mov	w27, w28
	b	.L282
.L284:
	cmp	w26, 5
	bne	.L285
	mov	w0, 56
	strb	w0, [x1,4]
	mov	w0, 57
	strb	w0, [x1,5]
	mov	w0, 58
	mov	w28, 8
	strb	w0, [x1,6]
	mov	w0, 59
	strb	w0, [x1,7]
	b	.L385
.L285:
	cmp	w26, 6
	bne	.L286
	mov	w0, 14
	strb	w0, [x1,4]
	mov	w0, 15
	strb	w0, [x1,5]
	mov	w0, 16
	mov	w28, 12
	strb	w0, [x1,6]
	mov	w0, 17
	strb	w0, [x1,7]
	b	.L385
.L286:
	cmp	w26, 7
	bne	.L287
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
	b	.L282
.L287:
	cmp	w26, 8
	bne	.L344
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
	b	.L282
.L344:
	mov	w28, 7
.L385:
	mov	w27, 4
.L282:
	sub	w0, w26, #1
	cmp	w0, 1
	bhi	.L381
	adrp	x22, .LANCHOR1
	add	x25, x20, :lo12:.LANCHOR0
	add	x22, x22, :lo12:.LANCHOR1
	mov	w21, 0
	add	x3, x25, 1676
	add	x26, x25, 4056
	add	x22, x22, 3224
.L288:
	ldrb	w0, [x25,1672]
	cmp	w0, w21
	bls	.L295
	ldrb	w0, [x3,w21,sxtw]
	add	x1, x25, 24
	mov	x23, 0
	add	x19, x26, x0, lsl 6
	lsl	x0, x0, 4
	add	x2, x1, x0
	add	x19, x19, 20
	ldr	x24, [x1,x0]
	mov	w1, 55
	ldrb	w0, [x2,8]
	add	x24, x24, x0, lsl 8
.L290:
	add	x0, x26, x23
	str	w1, [x24,2056]
	str	x3, [x29,128]
	str	x1, [x29,136]
	ldrb	w0, [x0,4]
	str	w0, [x24,2052]
	mov	x0, 400
	bl	__const_udelay
	ldr	w0, [x24,2048]
	strb	w0, [x19,x23]
	add	x23, x23, 1
	ldr	x1, [x29,136]
	cmp	w27, w23, uxtb
	ldr	x3, [x29,128]
	bhi	.L290
	mov	x0, 0
.L291:
	add	w2, w0, 8
	mov	x1, 0
.L292:
	add	x5, x1, x0
	add	x1, x1, 4
	add	x5, x22, x5
	cmp	x1, 24
	ldrb	w6, [x5,4]
	ldrb	w5, [x19,x0]
	add	w5, w6, w5
	strb	w5, [x19,w2,sxtw]
	add	w2, w2, 8
	bne	.L292
	add	x0, x0, 1
	cmp	x0, 4
	bne	.L291
	add	w21, w21, 1
	strb	wzr, [x19,16]
	strb	wzr, [x19,24]
	strb	wzr, [x19,32]
	uxtb	w21, w21
	strb	wzr, [x19,40]
	strb	wzr, [x19,48]
	strb	wzr, [x19,41]
	strb	wzr, [x19,49]
	b	.L288
.L381:
	sub	w0, w26, #3
	cmp	w0, 5
	bhi	.L295
	mul	w25, w27, w28
	sub	w24, w27, #1
	mov	w21, 0
	lsl	w0, w25, 4
	asr	w23, w25, 1
	str	w0, [x29,128]
	add	x25, x20, :lo12:.LANCHOR0
	lsl	w0, w23, 1
	uxtb	x24, w24
	str	w0, [x29,124]
	add	x0, x25, 1676
	add	x24, x24, 1
	str	x0, [x29,112]
	sub	w0, w26, #5
	str	w0, [x29,136]
.L296:
	ldrb	w0, [x25,1672]
	cmp	w0, w21
	bhi	.L342
.L295:
	add	x20, x20, :lo12:.LANCHOR0
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	strb	w27, [x20,4057]
	strb	w28, [x20,4058]
	ldp	x25, x26, [sp,64]
	ldp	x19, x20, [sp,16]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 144
	ret
.L342:
	ldr	x0, [x29,112]
	ldrb	w5, [x0,w21,sxtw]
	add	x0, x25, 24
	str	x5, [x29,104]
	lsl	x1, x5, 4
	mov	x22, x5
	add	x7, x0, x1
	ldr	x0, [x0,x1]
	ldrb	w19, [x7,8]
	add	x19, x0, x19, lsl 8
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w5
	bl	NandcWaitFlashReady
	cmp	w26, 7
	ldr	x5, [x29,104]
	bne	.L297
	mov	x0, 160
	madd	x5, x5, x0, x25
	add	x5, x5, 4084
	b	.L298
.L297:
	cmp	w26, 8
	beq	.L299
	add	x5, x25, x5, lsl 6
	add	x5, x5, 4076
.L298:
	mov	w0, 54
	cmp	w26, 4
	str	w0, [x19,2056]
	bne	.L300
	mov	w0, 255
	str	w0, [x19,2052]
	mov	w0, 64
	str	w0, [x19,2048]
	mov	w0, 204
	b	.L386
.L300:
	ldr	w0, [x29,136]
	cmp	w0, 1
	bhi	.L302
	ldrb	w0, [x25,4060]
	str	w0, [x19,2052]
	mov	w0, 82
	b	.L387
.L302:
	cmp	w26, 7
	bne	.L301
	mov	w0, 174
	str	w0, [x19,2052]
	str	wzr, [x19,2048]
	mov	w0, 176
.L386:
	str	w0, [x19,2052]
	mov	w0, 77
.L387:
	str	w0, [x19,2048]
.L301:
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
	bne	.L303
	mov	w0, 31
	str	w0, [x19,2052]
	b	.L304
.L303:
	str	wzr, [x19,2052]
.L304:
	mov	w0, 2
	str	w0, [x19,2052]
	str	wzr, [x19,2052]
.L343:
	mov	w0, 48
	str	w0, [x19,2056]
	mov	w0, w22
	str	x5, [x29,104]
	bl	NandcWaitFlashReady
	ldr	w0, [x29,136]
	ldr	x5, [x29,104]
	cmp	w0, 1
	cset	w8, ls
	cmp	w26, 8
	cset	w7, eq
	cbnz	w8, .L346
	cbnz	w7, .L346
	cmp	w26, 7
	mov	w1, 32
	mov	w0, 2
	csel	w1, w0, w1, ne
	b	.L305
.L346:
	mov	w1, 16
.L305:
	ldr	x0, [x25]
	mov	x9, 0
.L306:
	ldr	w10, [x19,2048]
	strb	w10, [x0,x9]
	add	x9, x9, 1
	cmp	w1, w9, uxtb
	bhi	.L306
	cbz	w7, .L307
	mov	w1, 0
.L309:
	ldrb	w9, [x0]
	cmp	w9, 50
	beq	.L308
	ldrb	w9, [x0,1]
	cmp	w9, 5
	beq	.L308
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L309
	b	.L310
.L308:
	cmp	w1, 6
	bls	.L311
.L310:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L312:
	b	.L312
.L307:
	cmp	w26, 7
	bne	.L313
	mov	w1, w7
.L315:
	ldrb	w9, [x0]
	cmp	w9, 12
	beq	.L314
	ldrb	w9, [x0,1]
	cmp	w9, 10
	beq	.L314
	add	w1, w1, 1
	add	x0, x0, 4
	uxtb	w1, w1
	cmp	w1, 8
	bne	.L315
	b	.L316
.L314:
	cmp	w1, 7
	bne	.L311
.L316:
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L317:
	b	.L317
.L313:
	cmp	w26, 6
	bne	.L311
	mov	x1, 0
.L318:
	ldrb	w9, [x0,x1]
	cmp	w9, 12
	beq	.L311
	add	x9, x0, x1
	ldrb	w9, [x9,8]
	cmp	w9, 4
	beq	.L311
	add	x1, x1, 1
	cmp	x1, 8
	bne	.L318
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L320:
	b	.L320
.L311:
	ldr	x10, [x25]
	mov	x0, 0
.L321:
	ldr	w1, [x29,128]
	cmp	w1, w0
	ble	.L389
	ldr	w1, [x19,2048]
	strb	w1, [x10,x0]
	add	x0, x0, 1
	b	.L321
.L389:
	ldr	x12, [x25]
	mov	w11, w23
	mov	w9, 8
.L324:
	mov	w0, 0
.L323:
	add	w1, w0, w11
	add	w0, w0, 1
	sbfiz	x1, x1, 1, 32
	cmp	w0, w23
	ldrh	w13, [x12,x1]
	mvn	w13, w13
	strh	w13, [x12,x1]
	bne	.L323
	ldr	w0, [x29,124]
	subs	w9, w9, #1
	add	w11, w11, w0
	bne	.L324
	mov	x9, 0
	mov	w17, 1
.L325:
	mov	w1, 0
	mov	w11, w1
.L329:
	lsl	w13, w17, w11
	mov	w15, w9
	mov	w0, 16
	mov	w14, 0
.L327:
	ldrh	w16, [x12,w15,sxtw 1]
	add	w15, w15, w23
	and	w16, w16, w13
	cmp	w16, w13
	csinc	w14, w14, w14, ne
	subs	w0, w0, #1
	bne	.L327
	cmp	w14, 8
	bls	.L328
	orr	w1, w1, w13
	uxth	w1, w1
.L328:
	add	w11, w11, 1
	cmp	w11, 16
	bne	.L329
	strh	w1, [x12,x9,lsl 1]
	add	x9, x9, 1
	cmp	w23, w9
	bgt	.L325
	mov	x9, 0
	ldr	x1, [x25]
	mov	w11, w9
.L332:
	ldr	w12, [x1,x9]
	add	x9, x9, 4
	cmp	w12, wzr
	csinc	w11, w11, w11, ne
	cmp	x9, 32
	bne	.L332
	cmp	w11, 7
	ble	.L333
	adrp	x0, .LC3
	mov	w2, 1
	add	x0, x0, :lo12:.LC3
	mov	w3, 1024
	bl	rknand_print_hex
	adrp	x0, .LC2
	mov	w1, 0
	add	x0, x0, :lo12:.LC2
	bl	printk
.L334:
	b	.L334
.L333:
	cmp	w26, 6
	mov	w9, 4
	beq	.L335
	cmp	w26, 7
	mov	w9, 10
	beq	.L335
	cmp	w7, wzr
	mov	w9, 5
	mov	w1, 8
	csel	w9, w1, w9, eq
.L335:
	mov	w11, 0
.L336:
	mov	x1, 0
.L337:
	add	w12, w0, w1
	ldrb	w13, [x10,x1]
	add	x1, x1, 1
	cmp	w27, w1, uxtb
	strb	w13, [x5,w12,sxtw]
	bhi	.L337
	add	w11, w11, 1
	add	x10, x10, x24
	cmp	w11, w28
	add	w0, w0, w9
	blt	.L336
	mov	w0, 255
	str	w0, [x19,2056]
	mov	w0, w22
	str	x7, [x29,96]
	str	x8, [x29,104]
	bl	NandcWaitFlashReady
	ldr	x8, [x29,104]
	ldr	x7, [x29,96]
	cbz	w8, .L339
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
	b	.L340
.L339:
	mov	w0, 190
	cbnz	w7, .L388
	mov	w0, 56
.L388:
	str	w0, [x19,2056]
.L340:
	mov	w0, w22
	add	w21, w21, 1
	bl	NandcWaitFlashReady
	uxtb	w21, w21
	b	.L296
.L299:
	mov	w0, 120
	str	w0, [x19,2056]
	str	wzr, [x19,2052]
	mov	w0, 23
	str	wzr, [x19,2052]
	mov	w1, 25
	str	wzr, [x19,2052]
	add	x5, x25, 4084
	str	w0, [x19,2056]
	mov	w0, 4
	str	w0, [x19,2056]
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
	b	.L343
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	stp	x29, x30, [sp, -16]!
	mov	w1, w0
	add	x29, sp, 0
	cbz	w0, .L390
	sub	w2, w0, #1
	cmp	w2, 7
	bhi	.L392
	bl	HynixGetReadRetryDefault
	b	.L390
.L392:
	cmp	w0, 49
	bne	.L393
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
	add	x1, x1, 408
	b	.L408
.L393:
	sub	w0, w0, #65
	cmp	w0, 1
	bls	.L399
	cmp	w1, 33
	bne	.L394
.L399:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 4
	b	.L409
.L394:
	cmp	w1, 67
	beq	.L400
	cmp	w1, 34
	bne	.L396
.L400:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	strb	w1, [x0,4056]
	mov	w1, 5
.L409:
	strb	w1, [x0,4057]
	mov	w1, 7
	strb	w1, [x0,4058]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	add	x0, x0, 4060
	add	x1, x1, 256
	mov	w2, 45
	b	.L408
.L396:
	cmp	w1, 68
	beq	.L401
	cmp	w1, 35
	bne	.L390
.L401:
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
	add	x1, x1, 304
.L408:
	bl	ftl_memcpy
.L390:
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
	bne	.L411
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
	b	.L412
.L411:
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
.L412:
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
	ldr	w0, [x0,568]
	cbz	w0, .L414
	mov	w0, 0
	bl	flash_enter_slc_mode
	b	.L415
.L414:
	bl	flash_exit_slc_mode
.L415:
	add	x19, x19, :lo12:.LANCHOR2
	ldrb	w0, [x19,572]
	cbz	w0, .L416
	ldrb	w0, [x19,564]
	tbz	x0, 0, .L416
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x19,572]
.L416:
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
	adrp	x1, .LANCHOR0
	movk	w2, 0xf, lsl 16
	add	x1, x1, :lo12:.LANCHOR0
	sdiv	w0, w0, w2
	cmp	w0, 250
	ble	.L425
	ldr	x0, [x1,3976]
	mov	w1, 8354
	b	.L432
.L425:
	cmp	w0, 220
	ble	.L427
	ldr	x0, [x1,3976]
	b	.L433
.L427:
	cmp	w0, 185
	ble	.L428
	ldr	x0, [x1,3976]
	mov	w1, 4226
	b	.L432
.L428:
	cmp	w0, 160
	ldr	x0, [x1,3976]
	ble	.L429
	mov	w1, 4194
	b	.L432
.L429:
	cmp	w19, 35
	mov	w1, 4193
	bls	.L432
	cmp	w19, 99
	mov	w1, 4225
	bls	.L432
.L433:
	mov	w1, 8322
.L432:
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
	bls	.L435
	mov	w1, 8322
	cmp	w0, w1
	bne	.L436
.L435:
	adrp	x1, .LANCHOR0+3976
	ldr	x1, [x1,#:lo12:.LANCHOR0+3976]
	str	w0, [x1,4]
.L436:
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
	mov	w3, 1
	adrp	x1, .LANCHOR2
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR0
	mov	x19, x1
	add	x2, x20, :lo12:.LANCHOR0
	str	w3, [x2,48]
	mov	w3, 2
	str	wzr, [x2,32]
	str	w3, [x2,64]
	mov	w3, 3
	str	x0, [x2,24]
	str	w3, [x2,80]
	add	x3, x1, :lo12:.LANCHOR2
	str	x0, [x2,40]
	str	x0, [x2,56]
	str	x0, [x2,72]
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
	str	w4, [x3,584]
	cmp	w4, 2049
	bne	.L438
	mov	w1, 8
	str	w1, [x3,580]
.L438:
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
	str	x0, [x1,592]
	str	x0, [x1,600]
	add	x0, x0, 32768
	str	wzr, [x1,640]
	str	x0, [x1,608]
	str	wzr, [x1,648]
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
	adrp	x1, .LANCHOR2+652
	str	w0, [x1,#:lo12:.LANCHOR2+652]
	mov	w1, 4096
	bne	.L441
.L444:
	and	w1, w1, -17
	b	.L442
.L441:
	cmp	w0, 24
	bne	.L443
	orr	w1, w1, 16
	b	.L442
.L443:
	cmp	w0, 40
	orr	w1, w1, 262144
	orr	w1, w1, 16
	beq	.L444
.L442:
	orr	w1, w1, 1
	str	w1, [x2,12]
	ret
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	stp	x29, x30, [sp, -16]!
	adrp	x1, .LANCHOR0+4045
	uxtb	w0, w0
	add	x29, sp, 0
	strb	w0, [x1,#:lo12:.LANCHOR0+4045]
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
	adrp	x19, .LANCHOR0
	str	x21, [sp,32]
	add	x0, x19, :lo12:.LANCHOR0
	adrp	x21, IDByte
	mov	x20, 0
	add	x21, x21, :lo12:IDByte
	ldr	x1, [x0,3976]
	ldr	w2, [x0,3984]
	str	w2, [x1]
	ldr	w2, [x0,3988]
	ldr	x1, [x0,3976]
	str	w2, [x1,4]
	ldr	w2, [x0,3992]
	ldr	x1, [x0,3976]
	str	w2, [x1,8]
	ldr	w2, [x0,3996]
	str	w2, [x1,12]
	ldr	w2, [x0,4000]
	str	w2, [x1,304]
	ldr	w2, [x0,4004]
	str	w2, [x1,308]
	ldr	w2, [x0,4008]
	str	w2, [x1,336]
	ldr	w0, [x0,4012]
	str	w0, [x1,344]
.L451:
	lsl	x0, x20, 3
	ldrb	w0, [x0,x21]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L450
	mov	w0, w20
	bl	FlashReset
.L450:
	add	x20, x20, 1
	cmp	x20, 4
	bne	.L451
	adrp	x20, .LANCHOR2
	add	x20, x20, :lo12:.LANCHOR2
	ldrb	w0, [x20,572]
	cbz	w0, .L452
	mov	w0, 1
	bl	NandcSetMode
	ldrb	w0, [x20,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x20,564]
	bl	NandcSetMode
	add	x0, x19, :lo12:.LANCHOR0
	ldr	w0, [x0,4000]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L452:
	add	x19, x19, :lo12:.LANCHOR0
	ldr	x0, [x19,1912]
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
	tbnz	x0, 9, .L459
	mov	x0, x19
	bl	wait_for_nand_flash_ready
	b	.L458
.L459:
	ldr	w0, [x19,368]
	orr	w0, w0, 2
	str	w0, [x19,368]
	ldr	w0, [x19,364]
	and	w0, w0, -3
	str	w0, [x19,364]
.L458:
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
	ldrb	w2, [x20,88]
	mov	w19, 0
	cbz	w2, .L462
	mov	w1, w23
	bl	FlashEraseSLc2KBlocks
	b	.L463
.L462:
	mov	w27, 56
	adrp	x28, .LANCHOR2
	add	x25, x20, 1716
.L493:
	cmp	w19, w23
	bcs	.L494
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
	ldrb	w1, [x20,1672]
	ldr	w0, [x29,124]
	ldr	x5, [x29,96]
	cmp	w0, w1
	bcc	.L464
	mov	w0, -1
	str	w0, [x24,x5]
	b	.L465
.L464:
	add	x1, x28, :lo12:.LANCHOR2
	mov	x2, 24
	ldrb	w1, [x1,656]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w22, w22, wzr, ne
	madd	x1, x1, x2, x25
	ldr	x1, [x1,8]
	cbz	x1, .L467
	bl	FlashWaitCmdDone
.L467:
	ldr	w1, [x29,124]
	mov	x0, 24
	ldr	w2, [x29,120]
	madd	x0, x1, x0, x25
	str	x21, [x0,8]
	str	xzr, [x0,16]
	str	w2, [x0,4]
	cbz	w22, .L468
	add	w2, w19, 1
	umull	x2, w2, w27
	add	x2, x24, x2
	str	x2, [x0,16]
.L468:
	add	x0, x20, 1676
	ldrb	w21, [x0,x1]
	mov	x0, 24
	mul	x1, x1, x0
	mov	w0, w21
	strb	w21, [x25,x1]
	bl	NandcFlashCs
	cmp	w26, 1
	bne	.L469
	ldrb	w0, [x20,4044]
	cbz	w0, .L469
	mov	w0, w21
	bl	flash_enter_slc_mode
	b	.L470
.L469:
	mov	w0, w21
	bl	flash_exit_slc_mode
.L470:
	ldr	w1, [x29,124]
	add	x0, x20, 1684
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
.L465:
	add	w19, w19, 1
	b	.L493
.L494:
	ldr	x0, [x29,104]
	mov	x20, 0
	mov	x22, 24
	add	x19, x0, :lo12:.LANCHOR0
	add	x21, x19, 1716
	ldr	x0, [x19,3976]
	bl	NandcIqrWaitFlashReady
.L472:
	ldrb	w0, [x19,1672]
	cmp	w0, w20
	bls	.L495
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w26, 1
	bne	.L473
	ldrb	w0, [x19,4044]
	cbz	w0, .L473
	mul	x0, x20, x22
	ldrb	w0, [x0,x21]
	bl	flash_exit_slc_mode
.L473:
	add	x20, x20, 1
	b	.L472
.L495:
	adrp	x0, .LANCHOR2+660
	ldr	w0, [x0,#:lo12:.LANCHOR2+660]
	cbnz	w0, .L475
.L477:
	mov	w0, 0
	b	.L463
.L475:
	adrp	x0, IDByte
	ldrb	w0, [x0,#:lo12:IDByte]
	cmp	w0, 69
	bne	.L477
	mov	x0, 0
	mov	x1, 56
.L476:
	cmp	w23, w0
	bls	.L477
	mul	x2, x0, x1
	add	x0, x0, 1
	str	wzr, [x24,x2]
	b	.L476
.L463:
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
.L498:
	ldr	w1, [x0,8]
	str	w1, [sp,8]
	ldr	w1, [sp,8]
	tbz	x1, 20, .L498
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
	add	x4, x4, 24
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
	bls	.L502
	ldr	w0, [x21,16]
	cmp	x5, xzr
	str	w0, [x29,88]
	cset	w7, ne
	ldr	w0, [x29,88]
	and	w0, w0, -5
	str	w0, [x29,88]
	cbnz	w7, .L516
	cbz	x22, .L503
.L516:
	cbnz	w25, .L505
.L513:
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
	cbnz	x22, .L507
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,600]
	b	.L507
.L505:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w4, 128
	mov	w2, 0
	lsr	w8, w23, 1
	mov	w3, w2
	mov	w9, -1
	ldr	w0, [x1,652]
	cmp	w0, 25
	mov	w0, 64
	csel	w4, w0, w4, cc
.L509:
	cmp	w3, w8
	bcs	.L513
	lsr	w0, w2, 2
	ubfiz	x0, x0, 2, 30
	cbz	w7, .L510
	ldrh	w6, [x5,2]
	ldr	x10, [x1,608]
	ldrh	w11, [x5],4
	orr	w6, w11, w6, lsl 16
	str	w6, [x10,x0]
	b	.L511
.L510:
	ldr	x6, [x1,608]
	str	w9, [x6,x0]
.L511:
	add	w3, w3, 1
	add	w2, w2, w4
	b	.L509
.L507:
	add	x19, x19, :lo12:.LANCHOR2
	ubfx	x23, x20, 22, 5
	mov	w2, w25
	and	x22, x22, 3
	ldr	x1, [x19,608]
	str	x1, [x19,624]
	lsl	w1, w23, 10
	str	x0, [x19,616]
	bl	rknand_dma_map_single
	str	w0, [x19,632]
	ldr	x0, [x19,624]
	lsl	w1, w23, 7
	mov	w2, w25
	bl	rknand_dma_map_single
	str	w0, [x19,636]
	mov	w0, 1
	str	w0, [x19,640]
	ldr	w0, [x19,632]
	mov	w1, 16
	str	w0, [x21,20]
	ldr	w0, [x19,636]
	str	w0, [x21,24]
	str	wzr, [x29,88]
	ldr	w0, [x29,88]
	bfi	w0, w1, 9, 5
	str	w0, [x29,88]
	ldr	w0, [x29,88]
	orr	w0, w0, 448
	str	w0, [x29,88]
	cbnz	x22, .L514
	ldr	w0, [x29,88]
	mov	w1, 2
	bfi	w0, w1, 3, 3
	str	w0, [x29,88]
.L514:
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
.L503:
	ldr	w0, [x29,88]
	str	w0, [x21,16]
.L502:
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
	add	x1, x1, 24
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	str	x23, [sp,48]
	adrp	x20, .LANCHOR2
	ldr	x19, [x1,x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,580]
	cmp	w0, 3
	bls	.L551
	ldr	w0, [x19,16]
	tbz	x0, 2, .L551
	mov	x0, x19
	bl	wait_for_nandc_xfer_completed
	ldr	w21, [x19,16]
	ldr	w0, [x19,8]
	ubfx	x21, x21, 1, 1
	str	w0, [x29,64]
	cbz	w21, .L522
	adrp	x22, .LC4
	adrp	x23, .LC5
	mov	w21, 0
	add	x22, x22, :lo12:.LC4
	add	x23, x23, :lo12:.LC5
.L523:
	ldr	w2, [x19,28]
	ldr	w1, [x29,64]
	ubfx	x2, x2, 16, 5
	ubfx	x1, x1, 22, 6
	cmp	w2, w1
	bge	.L531
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,580]
	cmp	w0, 5
	bhi	.L524
.L527:
	add	w21, w21, 1
	and	w0, w21, 16777215
	cbnz	w0, .L523
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
	b	.L523
.L524:
	ldr	w0, [x19]
	str	w0, [x29,72]
	ldr	w0, [x29,72]
	tbz	x0, 13, .L527
	ldr	w0, [x29,72]
	tbz	x0, 17, .L527
.L531:
	add	x19, x20, :lo12:.LANCHOR2
	add	x19, x19, 600
	ldr	w0, [x19,40]
	cbz	w0, .L532
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
	b	.L532
.L522:
	adrp	x22, .LC6
	adrp	x23, .LC5
	add	x22, x22, :lo12:.LC6
	add	x23, x23, :lo12:.LC5
.L533:
	ldr	w0, [x29,64]
	tbnz	x0, 20, .L559
	ldr	w0, [x19,8]
	add	w21, w21, 1
	str	w0, [x29,64]
	and	w0, w21, 16777215
	cbnz	w0, .L533
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
	b	.L533
.L559:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,648]
	cbz	w0, .L536
	mov	x0, x19
	bl	NandcSendDumpDataStart
.L536:
	add	x21, x20, :lo12:.LANCHOR2
	add	x21, x21, 600
	ldr	w0, [x21,40]
	cbz	w0, .L537
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
.L537:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,648]
	cbz	w0, .L532
	mov	x0, x19
	bl	NandcSendDumpDataDone
.L532:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x20,640]
	b	.L520
.L551:
	ldr	w0, [x19,8]
	str	w0, [x29,64]
	ldr	w0, [x29,64]
	tbz	x0, 20, .L551
.L520:
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
	bne	.L561
	cbz	x3, .L562
	mov	x0, x2
	mov	x1, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L562:
	cbz	x20, .L560
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
	b	.L560
.L561:
	cbz	x3, .L565
	mov	x1, x2
	mov	x0, x3
	mov	w2, 1024
	bl	ftl_memcpy
.L565:
	cbz	x20, .L560
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
.L560:
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
	cbnz	x0, .L577
	cbnz	x4, .L578
	add	x0, x29, 112
	mov	w1, 255
	mov	w2, 64
	add	x21, x29, 112
	bl	ftl_memset
.L578:
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
	cbnz	w20, .L579
	adrp	x0, .LANCHOR2
	ubfx	x3, x23, 1, 7
	add	x2, x0, :lo12:.LANCHOR2
	mov	w4, 128
	add	x3, x21, x3, lsl 2
	ldr	w1, [x2,652]
	cmp	w1, 25
	mov	w1, 64
	csel	w4, w1, w4, cc
	mov	w1, w20
.L581:
	cmp	x21, x3
	add	w5, w1, w4
	beq	.L623
	ldr	x6, [x2,608]
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
	b	.L581
.L623:
	add	x0, x0, :lo12:.LANCHOR2
	lsr	w23, w23, 2
	ldr	w4, [x0,652]
	ldr	w5, [x0,580]
	mov	w0, 0
	mov	w22, w0
.L583:
	cmp	w0, w23
	bcs	.L579
	cbz	w4, .L579
	uxtw	x1, w0
	add	x1, x1, 8
	ldr	w1, [x19,x1,lsl 2]
	str	w1, [x29,104]
	ldr	w1, [x29,104]
	tbnz	x1, 2, .L607
	ldr	w3, [x29,104]
	ubfx	x3, x3, 15, 1
	cbnz	w3, .L607
	cmp	w5, 5
	bls	.L585
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
	bls	.L586
	ldr	w1, [x29,104]
	ubfx	x3, x3, 3, 5
	ubfx	x1, x1, 27, 1
	orr	w3, w3, w1, lsl 5
	b	.L587
.L586:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 29, 1
	b	.L622
.L585:
	cmp	w5, 3
	bls	.L587
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
	bls	.L588
	ubfx	x1, x3, 3, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 28, 1
	b	.L622
.L588:
	ubfx	x1, x3, 16, 5
	ldr	w3, [x29,104]
	ubfx	x3, x3, 30, 1
.L622:
	orr	w3, w1, w3, lsl 5
.L587:
	cmp	w22, w3
	csel	w22, w22, w3, cs
	b	.L584
.L607:
	mov	w22, -1
.L584:
	add	w0, w0, 1
	b	.L583
.L579:
	str	wzr, [x19,16]
	b	.L590
.L577:
	cmp	w20, 1
	bne	.L621
	mov	w22, 0
	mov	w27, 2
.L591:
	cmp	w22, w23
	bcs	.L624
	and	w26, w22, 3
	mov	x3, x25
	cbz	x25, .L593
	ubfiz	x0, x22, 9, 23
	add	x3, x25, x0
.L593:
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
	b	.L591
.L624:
	mov	w22, 0
	b	.L590
.L621:
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
.L596:
	cmp	w26, w23
	bcs	.L590
	mov	w0, w24
	add	w28, w26, 2
	bl	NandcXferComp
	ldr	w0, [x19,32]
	cmp	w28, w23
	str	w0, [x29,104]
	bcs	.L597
	mov	x4, 0
	mov	w0, w24
	mov	w1, 0
	mov	w2, 2
	and	w3, w28, 3
	mov	x5, x4
	bl	NandcXferStart
.L597:
	ldr	w0, [x29,104]
	tbnz	x0, 2, .L612
	ldr	w0, [x29,104]
	ubfx	x1, x0, 3, 5
	ldr	w0, [x29,104]
	ubfx	x0, x0, 27, 1
	orr	w0, w1, w0, lsl 5
	cmp	w22, w0
	csel	w22, w22, w0, cs
	b	.L598
.L612:
	mov	w22, -1
.L598:
	and	w2, w26, 3
	mov	x3, x25
	cbz	x25, .L599
	ubfiz	x3, x26, 9, 23
	add	x3, x25, x3
.L599:
	cmp	x21, xzr
	mov	x0, x19
	csel	w4, w27, wzr, ne
	mov	w1, 0
	mul	w4, w4, w26
	mov	w26, w28
	add	x4, x21, x4
	bl	NandcCopy1KB
	b	.L596
.L590:
	cbnz	w20, .L602
	adrp	x0, .LANCHOR2+580
	ldr	w0, [x0,#:lo12:.LANCHOR2+580]
	cmp	w0, 5
	bls	.L602
	ldr	w0, [x19]
	mov	w1, 8192
	movk	w1, 0x2, lsl 16
	and	w1, w0, w1
	cmp	w1, 139264
	bne	.L602
	orr	w0, w0, 131072
	mov	w22, -1
	str	w0, [x19]
.L602:
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
	cbnz	w19, .L626
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w5, [x0,89]
	ldr	w0, [x0,92]
	mul	w0, w5, w0
	cmp	w1, w0
	mov	w0, 4
	csel	w20, w20, w0, cs
.L626:
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
	cbz	w4, .L629
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
	bne	.L630
.L639:
	mov	w21, -1
	b	.L631
.L630:
	adrp	x0, .LC7
	mov	w1, w25
	add	x0, x0, :lo12:.LC7
	mov	w2, w21
	bl	printk
	cmp	w21, 9
	bhi	.L632
	add	x20, x20, x24, sxtw 4
	ldr	x0, [x20,24]
	ldr	w1, [x0,3840]
	ldr	w1, [x0]
	orr	w1, w1, 131072
	str	w1, [x0]
.L632:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	w0, [x19,664]
	add	w0, w0, 1
	str	w0, [x19,664]
	cmp	w0, 2047
	bls	.L631
	mov	x23, 0
	str	wzr, [x19,664]
	mov	x28, x23
.L629:
	mov	w19, 0
	mov	w27, -1
	mov	w5, w19
	mov	w6, w19
	mov	w20, w19
.L637:
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
	bhi	.L633
	cmp	w0, 2
	bhi	.L643
	add	w20, w20, 1
	cmp	w20, 9
	bls	.L643
	sub	w19, w22, w20
	mov	w21, w0
	mov	w27, 0
	b	.L635
.L633:
	cmp	w6, w20
	bcs	.L644
	cmp	w20, 7
	sub	w5, w19, w20
	bhi	.L645
	mov	w6, w20
	b	.L644
.L643:
	mov	x23, 0
	mov	w19, w22
	mov	w21, w0
	mov	w27, 0
	mov	x28, x23
	b	.L634
.L644:
	mov	w20, 0
.L634:
	add	w22, w22, 2
	cmp	w22, 69
	bls	.L637
.L635:
	cmp	w6, w20
	csel	w19, w19, w5, cc
	b	.L636
.L645:
	mov	w19, w5
.L636:
	cbz	w19, .L638
	adrp	x0, .LC8
	mov	w1, w19
	add	x0, x0, :lo12:.LC8
	bl	printk
	mov	w0, w19
	bl	NandcSetDdrPara
.L638:
	cbz	w27, .L631
	adrp	x0, .LC9
	mov	w1, w24
	add	x0, x0, :lo12:.LC9
	mov	w2, w25
	bl	printk
	cbz	w26, .L639
	ldr	w0, [x29,124]
	lsr	w0, w0, 8
	bl	NandcSetDdrPara
.L631:
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
	bne	.L655
	adrp	x21, .LANCHOR0
	add	x26, x21, :lo12:.LANCHOR0
	ldrb	w27, [x26,96]
	cbnz	w27, .L656
.L658:
	add	x0, x23, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbz	w0, .L655
	b	.L676
.L656:
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	strb	wzr, [x26,96]
	bl	FlashReadRawPage
	strb	w27, [x26,96]
	cmn	w0, #1
	beq	.L658
	mov	w19, w0
	b	.L655
.L676:
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
	beq	.L659
	ldrb	w0, [x21,4045]
	cmp	w19, w0, lsr 1
	bls	.L655
.L659:
	lsr	w0, w26, 8
	bl	NandcSetDdrPara
.L655:
	add	x21, x23, :lo12:.LANCHOR2
	cmn	w19, #1
	ldr	x4, [x21,672]
	bne	.L660
	cbz	x4, .L660
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
	bne	.L660
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L660
	mov	w0, w20
	bl	flash_enter_slc_mode
	ldr	x4, [x21,672]
	mov	w0, w20
	mov	w1, w22
	mov	x2, x25
	mov	x3, x24
	blr	x4
	mov	w19, w0
	mov	w0, w20
	bl	flash_exit_slc_mode
.L660:
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
	beq	.L681
	cmn	w22, #1
	bne	.L678
.L681:
	add	x20, x19, :lo12:.LANCHOR2
	ldrb	w0, [x20,564]
	tbz	x0, 0, .L678
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	strb	wzr, [x20,572]
	b	.L680
.L678:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,572]
.L680:
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
	adrp	x19, .LANCHOR0
	str	x27, [sp,80]
	strb	w0, [x29,104]
	mov	w0, 40
	strb	w0, [x29,105]
	mov	w0, 24
	strb	w0, [x29,106]
	mov	w0, 16
	strb	w0, [x29,107]
	add	x0, x19, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	adrp	x23, .LANCHOR2
	adrp	x22, .LANCHOR1
	ldr	w25, [x0,92]
	add	x0, x23, :lo12:.LANCHOR2
	ldr	x1, [x19,#:lo12:.LANCHOR0]
	add	x22, x22, :lo12:.LANCHOR1
	mov	w20, 0
	mov	w24, 4
	str	x1, [x0,680]
	mov	w26, -1
	str	wzr, [x0,688]
	add	x22, x22, 472
	mov	w0, 0
	bl	flash_enter_slc_mode
.L689:
	add	w27, w20, 1
	mov	x21, 0
.L691:
	add	x0, x29, 104
	ldrb	w0, [x21,x0]
	bl	FlashBchSel
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w20
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L690
	ldr	x2, [x19,#:lo12:.LANCHOR0]
	mov	w0, 0
	mov	w1, w27
	mov	x3, 0
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L690
	add	x21, x21, 1
	cmp	x21, 4
	beq	.L692
	b	.L691
.L693:
	mov	w1, 2036
	add	x0, x27, 12
	mov	w26, -1
	bl	js_hash
	ldr	w1, [x27,8]
	cmp	w1, w0
	beq	.L703
.L692:
	subs	w24, w24, #1
	add	w20, w20, w25
	bne	.L689
	mov	w0, w24
.L702:
	bl	flash_exit_slc_mode
	ldr	x27, [sp,80]
	mov	w0, w26
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x29, x30, [sp], 112
	ret
.L690:
	add	x21, x23, :lo12:.LANCHOR2
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	x27, [x21,680]
	ldr	w1, [x27]
	cmp	w1, w0
	bne	.L692
	cbnz	w26, .L693
	add	x19, x19, :lo12:.LANCHOR0
	ldr	w0, [x19,92]
	udiv	w20, w20, w0
	mov	w0, w26
	add	w20, w20, 1
	str	w20, [x19,8]
	b	.L702
.L703:
	add	x1, x27, 160
	mov	w2, 32
	mov	x0, x22
	add	x26, x19, :lo12:.LANCHOR0
	bl	ftl_memcpy
	ldr	x1, [x21,680]
	mov	w2, 32
	add	x0, x26, 104
	add	x1, x1, 192
	bl	ftl_memcpy
	ldr	x1, [x21,680]
	mov	w2, 852
	add	x0, x26, 4056
	add	x1, x1, 224
	bl	ftl_memcpy
	ldrh	w0, [x22,10]
	bl	FlashBlockAlignInit
	str	w20, [x21,688]
	ldr	w1, [x26,92]
	ldr	x2, [x21,680]
	udiv	w1, w20, w1
	ldr	w0, [x2,1076]
	add	w1, w1, 1
	strb	w0, [x21,572]
	cmp	w1, 1
	bls	.L695
	str	w1, [x26,8]
	b	.L696
.L695:
	mov	w0, 2
	str	w0, [x26,8]
.L696:
	add	x0, x23, :lo12:.LANCHOR2
	ldrh	w1, [x2,14]
	mov	w26, 0
	strb	w1, [x0,692]
	b	.L692
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
	add	x0, x0, 24
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
	bls	.L705
	adrp	x0, .LANCHOR2+572
	ldrb	w0, [x0,#:lo12:.LANCHOR2+572]
	cbz	w0, .L706
	mov	w0, w22
	mov	w22, 1
	bl	NandcSetDdrMode
.L706:
	ubfiz	x0, x28, 8, 8
	mov	w2, 92
	add	x0, x24, x0
	str	w2, [x0,2056]
	mov	w2, 197
	str	w2, [x0,2056]
.L705:
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
.L707:
	adrp	x0, g_maxRetryCount
	ldrb	w0, [x0,#:lo12:g_maxRetryCount]
	add	w0, w0, 1
	cmp	w21, w0
	bcs	.L732
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, w21
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L708
	bl	SandiskSetRRPara
	b	.L709
.L708:
	bl	ToshibaSetRRPara
.L709:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	cmp	w0, 34
	bne	.L710
	ldr	x0, [x29,112]
	ldrb	w0, [x0]
	sub	w0, w0, #3
	cmp	w21, w0
	bne	.L710
	ldr	x0, [x29,104]
	mov	w1, 179
	add	x0, x24, x0
	str	w1, [x0,8]
.L710:
	ldr	x0, [x29,96]
	mov	w1, 38
	add	x0, x24, x0
	str	w1, [x0,2056]
	mov	w1, 93
	str	w1, [x0,2056]
	cbz	w22, .L711
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
	b	.L712
.L711:
	ldr	w1, [x29,132]
	mov	w0, w23
	mov	x2, x27
	mov	x3, x26
	bl	FlashReadRawPage
	mov	w28, w0
.L712:
	cmn	w28, #1
	beq	.L713
	ldr	x0, [x29,120]
	cmn	w25, #1
	csel	w25, w25, w28, ne
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4045]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L715
	mov	x26, 0
	mov	x27, x26
.L713:
	add	w21, w21, 1
	b	.L707
.L732:
	mov	w28, w25
.L715:
	ldrb	w0, [x20,#:lo12:g_retryMode]
	mov	w1, 0
	sub	w0, w0, #67
	uxtb	w0, w0
	cmp	w0, 1
	mov	x0, x19
	bhi	.L717
	bl	SandiskSetRRPara
	b	.L718
.L717:
	bl	ToshibaSetRRPara
.L718:
	ldr	x0, [x29,136]
	add	x0, x0, 8
	add	x24, x24, x0, lsl 8
	mov	w0, 255
	str	w0, [x24,8]
	ldr	x0, [x29,120]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4045]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bcc	.L719
	cmn	w28, #1
	mov	w0, 256
	csel	w28, w28, w0, eq
.L719:
	mov	w0, w23
	bl	NandcWaitFlashReady
	cbz	w22, .L720
	mov	w0, 4
	bl	NandcSetDdrMode
.L720:
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
	add	x0, x0, 24
	mov	w19, -1
	add	x2, x0, x1
	add	x27, x27, :lo12:g_maxRetryCount
	ldr	x0, [x0,x1]
	ldrb	w21, [x2,8]
	add	x21, x21, 8
	add	x21, x0, x21, lsl 8
.L734:
	ldrb	w0, [x27]
	add	w0, w0, 1
	cmp	w26, w0
	bcs	.L737
	mov	x0, x21
	mov	w1, w26
	bl	SamsungSetRRPara
	mov	w0, w22
	mov	w1, w25
	mov	x2, x24
	mov	x3, x23
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L735
	add	x1, x20, :lo12:.LANCHOR0
	cmn	w19, #1
	csel	w19, w19, w0, ne
	ldrb	w1, [x1,4045]
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L740
	mov	x23, 0
	mov	x24, x23
.L735:
	add	w26, w26, 1
	b	.L734
.L740:
	mov	w19, w0
.L737:
	mov	x0, x21
	mov	w1, 0
	add	x20, x20, :lo12:.LANCHOR0
	bl	SamsungSetRRPara
	ldrb	w0, [x20,4045]
	add	w0, w0, w0, lsl 1
	cmp	w19, w0, lsr 2
	bcc	.L739
	cmn	w19, #1
	mov	w0, 256
	csel	w19, w19, w0, eq
.L739:
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
	ldrb	w19, [x0,4045]
	mov	x22, x3
	ldrb	w0, [x0,4044]
	cbnz	w0, .L747
	add	w19, w19, w19, lsl 1
	ubfx	x19, x19, 2, 8
	b	.L748
.L747:
	mov	w0, 3
	sdiv	w19, w19, w0
.L748:
	adrp	x0, g_maxRetryCount
	adrp	x27, .LC11
	add	x0, x0, :lo12:g_maxRetryCount
	mov	w25, 0
	str	x0, [x29,152]
	add	x0, x27, :lo12:.LC11
	str	x0, [x29,144]
	add	x20, x20, :lo12:.LANCHOR0
	add	x23, x20, 24
	add	x23, x23, x21, sxtw 4
.L759:
	mov	w0, w21
	mov	w24, 0
	mov	w27, -1
	bl	NandcWaitFlashReady
	ldrb	w4, [x23,8]
	mov	w9, 239
	ldr	x6, [x23]
	mov	w10, 137
	lsl	x8, x4, 8
.L749:
	ldr	x0, [x29,152]
	ldrb	w0, [x0]
	cmp	w24, w0
	bcs	.L752
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
	beq	.L750
	cmn	w27, #1
	csel	w27, w27, w0, ne
	cmp	w0, w19
	bcc	.L761
	mov	x22, 0
	mov	x26, x22
.L750:
	mov	w24, w7
	b	.L749
.L761:
	mov	x22, 0
	mov	w27, w0
	mov	x26, x22
.L752:
	add	x1, x6, x4, lsl 8
	mov	w0, 239
	str	x1, [x29,136]
	str	w0, [x1,2056]
	mov	w0, 137
	str	w0, [x1,2052]
	mov	x0, 1000
	bl	__const_udelay
	ldr	x1, [x29,136]
	cmp	w27, w19
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	str	wzr, [x1,2048]
	bcc	.L754
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
.L754:
	cmn	w27, #1
	cset	w6, eq
	cbnz	w6, .L763
	cmp	w27, 256
	cset	w1, eq
	cbz	w1, .L755
.L763:
	ldr	x0, [x29,144]
	mov	w1, w24
	mov	w2, w28
	mov	w3, w24
	mov	w4, w27
	str	x6, [x29,136]
	bl	printk
	cbnz	w25, .L757
	ldr	x6, [x29,136]
	cbz	w6, .L768
	ldrb	w0, [x20,4044]
	cbz	w0, .L768
	mov	w0, w21
	mov	w1, 3
	mov	w25, 1
	bl	micron_auto_read_calibration_config
	b	.L759
.L757:
	mov	w0, w21
	mov	w1, 0
	bl	micron_auto_read_calibration_config
	cmn	w27, #1
	mov	w0, 256
	csel	w27, w27, w0, eq
	b	.L768
.L755:
	cbz	w25, .L768
	mov	w0, w21
	mov	w27, 256
	bl	micron_auto_read_calibration_config
.L768:
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
	add	x0, x21, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	stp	x19, x20, [sp,16]
	mov	x26, x2
	add	x2, x0, 4056
	ldrb	w25, [x0,4058]
	mov	w27, w1
	ldr	x0, [x0,1912]
	add	x1, x2, x22
	mov	x23, x22
	mov	x24, x3
	ldrb	w0, [x0,19]
	ldrb	w19, [x1,12]
	sub	w0, w0, #7
	uxtb	w0, w0
	cmp	w0, 1
	bhi	.L777
	ldrb	w19, [x1,20]
.L777:
	mov	w0, w23
	mov	w28, 0
	mov	w20, -1
	bl	NandcWaitFlashReady
	add	x4, x21, :lo12:.LANCHOR0
	add	x5, x4, 4060
.L778:
	cmp	w28, w25
	bcs	.L782
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
	beq	.L780
	ldrb	w1, [x4,4045]
	cmn	w20, #1
	csel	w20, w20, w0, ne
	add	w1, w1, w1, lsl 1
	cmp	w0, w1, lsr 2
	bcc	.L787
	mov	x24, 0
	mov	x26, x24
.L780:
	add	w28, w28, 1
	b	.L778
.L787:
	mov	w20, w0
.L782:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	add	x0, x0, 4056
	add	x0, x0, x22
	ldrb	w1, [x1,19]
	sub	w1, w1, #7
	uxtb	w1, w1
	cmp	w1, 1
	bhi	.L784
	strb	w19, [x0,20]
	b	.L785
.L784:
	strb	w19, [x0,12]
.L785:
	add	x21, x21, :lo12:.LANCHOR0
	ldrb	w0, [x21,4045]
	add	w0, w0, w0, lsl 1
	cmp	w20, w0, lsr 2
	bcc	.L786
	cmn	w20, #1
	mov	w0, 256
	csel	w20, w20, w0, eq
.L786:
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
	adrp	x0, .LANCHOR1+481
	stp	x21, x22, [sp,32]
	mov	w20, w1
	mov	x22, x2
	ldrb	w21, [x0,#:lo12:.LANCHOR1+481]
	cbnz	w19, .L794
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,89]
	ldr	w2, [x0,92]
	mul	w1, w1, w2
	cmp	w20, w1
	bcs	.L794
	ldrb	w0, [x0,88]
	cbnz	w0, .L795
	sub	w21, w21, #2
	b	.L794
.L795:
	mov	w21, 4
.L794:
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
	str	x0, [x19,680]
	ldrb	w0, [x19,693]
	bl	FlashBchSel
	ldr	x0, [x22,#:lo12:.LANCHOR0]
	mov	w1, 0
	mov	w2, 2048
	bl	ftl_memset
	ldr	x1, [x19,680]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	mov	w2, 32
	str	w0, [x1]
	ldr	x0, [x19,680]
	ldrb	w1, [x20,1672]
	add	x0, x0, 16
	strh	w1, [x0,-4]
	ldrb	w1, [x20,89]
	strh	w1, [x0,-2]
	ldrb	w1, [x19,572]
	str	w1, [x0,1060]
	adrp	x1, IDByte
	add	x1, x1, :lo12:IDByte
	bl	ftl_memcpy
	ldr	x0, [x19,680]
	add	x1, x20, 1676
	mov	w2, 8
	add	x0, x0, 80
	bl	ftl_memcpy
	ldr	x0, [x19,680]
	add	x1, x20, 1684
	mov	w2, 32
	add	x0, x0, 96
	bl	ftl_memcpy
	ldr	x0, [x19,680]
	adrp	x1, .LANCHOR1
	add	x1, x1, :lo12:.LANCHOR1
	mov	w2, 32
	add	x1, x1, 472
	add	x0, x0, 160
	bl	ftl_memcpy
	ldr	x0, [x19,680]
	add	x1, x20, 104
	mov	w2, 32
	add	x0, x0, 192
	bl	ftl_memcpy
	ldr	x0, [x19,680]
	add	x1, x20, 4056
	mov	w2, 852
	add	x0, x0, 224
	bl	ftl_memcpy
	ldr	x20, [x19,680]
	mov	w1, 2036
	add	x0, x20, 12
	bl	js_hash
	str	w0, [x20,8]
	mov	w0, 1592
	str	w0, [x20,4]
	ldr	x0, [x19,696]
	str	x0, [x19,680]
	mov	w0, 0
	mov	w19, w21
	bl	flash_enter_slc_mode
.L799:
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
	ldr	x2, [x24,696]
	mov	x3, 0
	mul	w1, w19, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	beq	.L797
	ldr	x25, [x24,680]
	mov	w0, 20036
	movk	w0, 0x4e41, lsl 16
	ldr	w1, [x25]
	cmp	w1, w0
	bne	.L797
	mov	w1, 2036
	add	x0, x25, 12
	bl	js_hash
	ldr	w1, [x25,8]
	cmp	w1, w0
	bne	.L797
	ldr	w0, [x20,92]
	cmp	w21, 1
	str	w26, [x20,8]
	mul	w19, w19, w0
	str	w19, [x24,688]
	beq	.L800
	mov	w21, 1
.L797:
	cmp	w26, 4
	mov	w19, w26
	bne	.L799
	b	.L798
.L800:
	mov	w21, 2
.L798:
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
	ldrb	w28, [x0,4045]
	add	x0, x22, :lo12:.LANCHOR2
	ldr	w0, [x0,568]
	cbz	w0, .L806
	mov	w0, 0
	bl	flash_enter_slc_mode
.L806:
	adrp	x25, .LC12
	mov	x0, x27
	mov	w1, 0
	mov	w2, 2048
	mov	w24, -1
	mov	w20, 2
	add	x25, x25, :lo12:.LC12
	bl	ftl_memset
.L807:
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,89]
	cmp	w20, w0
	bcs	.L811
	mov	x23, 0
.L809:
	add	x0, x29, 104
	add	x19, x21, :lo12:.LANCHOR0
	ldrb	w26, [x23,x0]
	mov	w0, w26
	bl	FlashBchSel
	ldr	w1, [x19,92]
	mov	w0, 0
	ldr	x2, [x21,#:lo12:.LANCHOR0]
	mov	x3, 0
	mul	w1, w20, w1
	bl	FlashReadRawPage
	cmn	w0, #1
	bne	.L808
	add	x23, x23, 1
	cmp	x23, 4
	bne	.L809
	b	.L810
.L808:
	ldr	x0, [x19]
	ldr	w1, [x0]
	mov	w0, 35899
	movk	w0, 0xfcdc, lsl 16
	cmp	w1, w0
	bne	.L810
	mov	w1, w26
	mov	x0, x25
	bl	printk
	ldr	x1, [x19]
	mov	x0, x27
	mov	w2, 2048
	bl	ftl_memcpy
	ldr	x0, [x19]
	ldr	w0, [x0,512]
	strb	w0, [x19,89]
	ldr	w0, [x19,8]
	cmp	w0, w20
	bls	.L814
	mov	w24, 0
	str	w20, [x19,8]
	bl	FlashSavePhyInfo
.L810:
	add	w20, w20, 1
	b	.L807
.L814:
	mov	w24, 0
.L811:
	mov	w0, w28
	add	x22, x22, :lo12:.LANCHOR2
	bl	FlashBchSel
	ldr	w0, [x22,568]
	cbz	w0, .L818
	mov	w0, 0
	bl	flash_exit_slc_mode
.L818:
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
	stp	x23, x24, [sp,48]
	adrp	x19, .LANCHOR0
	mov	x23, x0
	mov	w0, 32768
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	stp	x25, x26, [sp,64]
	bl	ftl_malloc
	add	x21, x20, :lo12:.LANCHOR2
	str	x0, [x19,#:lo12:.LANCHOR0]
	mov	w0, 32768
	add	x22, x19, :lo12:.LANCHOR0
	adrp	x24, .LC13
	bl	ftl_malloc
	str	x0, [x21,696]
	mov	w0, 4096
	add	x22, x22, 24
	mov	w27, 1
	mov	w28, 4
	bl	ftl_malloc
	str	x0, [x21,704]
	mov	w0, 32768
	bl	ftl_malloc
	str	x0, [x21,712]
	mov	w0, 4096
	bl	ftl_malloc
	str	x0, [x21,720]
	mov	w0, 128
	str	w0, [x22,68]
	mov	w0, 60
	mov	w1, 50
	strb	w0, [x21,693]
	mov	x0, x23
	strb	w1, [x22,65]
	mov	w23, 0
	strb	w1, [x21,692]
	str	wzr, [x22,-16]
	strb	wzr, [x21,572]
	str	wzr, [x21,664]
	strb	wzr, [x22,64]
	strb	wzr, [x21,728]
	bl	NandcInit
	adrp	x21, IDByte
	add	x0, x24, :lo12:.LC13
	add	x26, x21, :lo12:IDByte
	str	x0, [x29,104]
.L830:
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
	bne	.L825
	mov	w0, 239
	str	w0, [x24,2056]
	str	w27, [x24,2052]
	mov	x0, 1000
	bl	__const_udelay
	str	w28, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
	str	wzr, [x24,2048]
.L825:
	mov	w0, w25
	bl	NandcFlashDeCs
	ldrb	w2, [x26]
	sub	w0, w2, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L826
	ldr	x0, [x29,104]
	add	w1, w23, 1
	ldrb	w3, [x26,1]
	ldrb	w4, [x26,2]
	ldrb	w5, [x26,3]
	ldrb	w6, [x26,4]
	ldrb	w7, [x26,5]
	bl	printk
.L826:
	cbnz	w23, .L827
	ldrb	w0, [x21,#:lo12:IDByte]
	add	x1, x21, :lo12:IDByte
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 253
	bhi	.L881
	ldrb	w0, [x1,1]
	cmp	w0, 255
	beq	.L881
	bl	FlashCs123Init
.L827:
	ldrb	w0, [x26]
	cmp	w0, 181
	bne	.L829
	mov	w0, 44
	strb	w0, [x26]
.L829:
	add	w23, w23, 1
	add	x22, x22, 16
	cmp	w23, 4
	add	x26, x26, 8
	bne	.L830
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 173
	beq	.L831
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,576]
	bl	NandcSetDdrMode
.L831:
	add	x22, x19, :lo12:.LANCHOR0
	mov	w1, 0
	mov	w2, 852
	add	x0, x22, 4056
	bl	ftl_memset
	strb	wzr, [x22,96]
	adrp	x1, .LANCHOR1
	mov	w2, 12336
	add	x0, x1, :lo12:.LANCHOR1
	movk	w2, 0x5638, lsl 16
	add	x0, x0, 472
	str	x0, [x22,1912]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w4, [x0,584]
	cmp	w4, w2
	bne	.L832
	mov	w2, 1
	str	w2, [x0,568]
.L832:
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
	cbnz	w2, .L833
	and	w2, w0, -3
	cmp	w2, 209
	beq	.L833
	cmp	w0, 220
	bne	.L834
	ldrb	w2, [x3,3]
	cmp	w2, 149
	bne	.L834
.L833:
	add	x2, x19, :lo12:.LANCHOR0
	add	x3, x20, :lo12:.LANCHOR2
	mov	w7, 16
	mov	w5, 1
	add	x8, x21, :lo12:IDByte
	strb	w7, [x2,89]
	strb	w5, [x2,88]
	add	x2, x1, :lo12:.LANCHOR1
	strb	w7, [x3,693]
	ldrb	w7, [x21,#:lo12:IDByte]
	strb	w7, [x2,3257]
	strb	w0, [x2,3258]
	cmp	w7, 152
	bne	.L836
	ldrsb	w2, [x8,4]
	tbnz	w2, #31, .L837
	mov	w2, 24
	strb	w2, [x3,693]
.L836:
	cmp	w4, 2049
	beq	.L884
	mov	w2, 12336
	movk	w2, 0x5638, lsl 16
	cmp	w4, w2
	bne	.L839
.L884:
	add	x2, x20, :lo12:.LANCHOR2
	mov	w3, 16
	strb	w3, [x2,693]
.L839:
	cbz	w6, .L841
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 2048
	strh	w2, [x0,3270]
	mov	w2, -38
	b	.L926
.L841:
	cmp	w0, 220
	bne	.L843
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3270]
	mov	w2, -36
.L926:
	strb	w2, [x0,3258]
	b	.L842
.L843:
	cmp	w0, 211
	bne	.L842
	add	x0, x1, :lo12:.LANCHOR1
	mov	w2, 4096
	strh	w2, [x0,3270]
	mov	w2, 2
	strb	w2, [x0,3269]
.L842:
	add	x22, x1, :lo12:.LANCHOR1
	add	x0, x19, :lo12:.LANCHOR0
	add	x1, x22, 3128
	mov	w2, 32
	add	x0, x0, 104
	bl	ftl_memcpy
	add	x0, x22, 472
	add	x1, x22, 3256
	mov	w2, 32
	bl	ftl_memcpy
.L834:
	add	x22, x19, :lo12:.LANCHOR0
	ldrb	w0, [x22,88]
	cbnz	w0, .L844
	bl	FlashLoadPhyInfoInRam
	cbnz	w0, .L846
	ldr	x0, [x22,1912]
	add	x22, x20, :lo12:.LANCHOR2
	ldrb	w1, [x0,17]
	and	w0, w1, 7
	strb	w0, [x22,564]
	tbnz	x1, 0, .L846
	mov	w1, 1
	strb	w1, [x22,572]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,564]
	bl	NandcSetMode
.L846:
	add	x0, x19, :lo12:.LANCHOR0
	ldr	x1, [x0,1912]
	ldrb	w1, [x1,26]
	strb	w1, [x0,4044]
	bl	FlashLoadPhyInfo
	cbz	w0, .L844
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w0, [x22,576]
	cbz	w0, .L849
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	b	.L922
.L849:
	ldrb	w0, [x22,564]
	bl	FlashSetInterfaceMode
	ldrb	w0, [x22,564]
.L922:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cbz	w0, .L844
	mov	w0, 1
	add	x22, x19, :lo12:.LANCHOR0
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
	ldr	x1, [x22,1912]
	adrp	x0, .LC14
	add	x0, x0, :lo12:.LC14
	ldrh	w1, [x1,14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	w0, #1
	beq	.L898
	bl	FlashDieInfoInit
	ldr	x0, [x22,1912]
	ldrb	w0, [x0,19]
	bl	FlashGetReadRetryDefault
	ldr	x0, [x22,1912]
	ldrh	w1, [x22,1920]
	ldrb	w2, [x0,9]
	add	w1, w1, 4095
	cmp	w2, w1, lsr 12
	blt	.L851
	ldrh	w1, [x0,14]
	add	w1, w1, 255
	cmp	w2, w1, lsr 8
	bge	.L852
.L851:
	ldrh	w1, [x0,14]
	and	w1, w1, -256
	strh	w1, [x0,14]
.L852:
	add	x22, x20, :lo12:.LANCHOR2
	ldrb	w0, [x22,564]
	tst	w0, 6
	beq	.L853
	bl	FlashSavePhyInfo
	ldr	w1, [x22,688]
	mov	w0, 0
	bl	FlashDdrParaScan
.L853:
	bl	FlashSavePhyInfo
.L844:
	add	x22, x19, :lo12:.LANCHOR0
	add	x23, x20, :lo12:.LANCHOR2
	ldr	x2, [x22,1912]
	str	xzr, [x23,672]
	ldrb	w0, [x2,26]
	strb	w0, [x22,4044]
	ldrh	w0, [x2,16]
	ldrh	w3, [x2,10]
	ubfx	x1, x0, 7, 1
	strb	w1, [x22,96]
	ubfx	x1, x0, 3, 1
	strb	w1, [x23,729]
	ubfx	x1, x0, 4, 1
	strb	w1, [x23,656]
	ldrb	w1, [x2,12]
	ubfx	x0, x0, 8, 3
	strb	w0, [x23,564]
	ldrb	w0, [x2,18]
	sdiv	w1, w3, w1
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	x0, [x22,1912]
	ldrh	w1, [x0,16]
	tbz	x1, 6, .L855
	adrp	x1, g_retryMode
	ldrb	w0, [x0,19]
	ldrb	w2, [x22,4057]
	ldrb	w3, [x22,4058]
	strb	w0, [x1,#:lo12:g_retryMode]
	adrp	x1, g_maxRegNum
	strb	w2, [x1,#:lo12:g_maxRegNum]
	adrp	x2, g_maxRetryCount
	strb	w3, [x2,#:lo12:g_maxRetryCount]
	sub	w3, w0, #1
	uxtb	w3, w3
	cmp	w3, 7
	mov	x3, x2
	bhi	.L856
	adrp	x1, HynixReadRetrial
	cmp	w0, 8
	add	x1, x1, :lo12:HynixReadRetrial
	str	x1, [x23,672]
	sub	w1, w0, #5
	cset	w2, eq
	uxtb	w1, w1
	cmp	w1, 1
	bls	.L885
	cbz	w2, .L857
.L885:
	add	x1, x20, :lo12:.LANCHOR2
	mov	w3, 1
	str	w3, [x1,648]
.L857:
	cmp	w0, 7
	add	x1, x19, :lo12:.LANCHOR0
	beq	.L883
	cbnz	w2, .L883
	add	x1, x1, 4076
	b	.L860
.L883:
	add	x1, x1, 4084
.L860:
	mov	x2, 0
	mov	w3, w2
.L862:
	ldrsb	w4, [x1,x2]
	add	x2, x2, 1
	cmp	w4, wzr
	csinc	w3, w3, w3, ne
	cmp	x2, 32
	bne	.L862
	cmp	w3, 27
	bls	.L855
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L855
.L856:
	sub	w4, w0, #17
	uxtb	w4, w4
	cmp	w4, 2
	bhi	.L864
	adrp	x1, MicronReadRetrial
	cmp	w0, 19
	add	x1, x1, :lo12:MicronReadRetrial
	str	x1, [x23,672]
	beq	.L865
	mov	w0, 7
	b	.L924
.L865:
	mov	w0, 15
.L924:
	strb	w0, [x2,#:lo12:g_maxRetryCount]
	b	.L855
.L864:
	sub	w2, w0, #65
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L886
	cmp	w0, 33
	bne	.L866
.L886:
	add	x0, x20, :lo12:.LANCHOR2
	adrp	x2, ToshibaReadRetrial
	add	x2, x2, :lo12:ToshibaReadRetrial
	str	x2, [x0,672]
	mov	w0, 4
	strb	w0, [x1,#:lo12:g_maxRegNum]
	mov	w0, 7
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	b	.L855
.L866:
	sub	w2, w0, #67
	uxtb	w2, w2
	cmp	w2, 1
	sub	w2, w0, #34
	cset	w5, ls
	uxtb	w2, w2
	cmp	w2, 1
	bls	.L887
	cbz	w5, .L868
.L887:
	add	x2, x20, :lo12:.LANCHOR2
	adrp	x4, ToshibaReadRetrial
	add	x4, x4, :lo12:ToshibaReadRetrial
	cmp	w0, 35
	str	x4, [x2,672]
	beq	.L870
	cmp	w0, 68
	beq	.L870
	mov	w0, 7
	b	.L923
.L870:
	mov	w0, 17
.L923:
	strb	w0, [x3,#:lo12:g_maxRetryCount]
	mov	w0, 4
	cbnz	w5, .L925
	mov	w0, 5
.L925:
	strb	w0, [x1,#:lo12:g_maxRegNum]
	b	.L855
.L868:
	cmp	w0, 49
	bne	.L855
	adrp	x0, SamsungReadRetrial
	add	x0, x0, :lo12:SamsungReadRetrial
	str	x0, [x23,672]
.L855:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,584]
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L873
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w2, [x0,4044]
	cbz	w2, .L873
	ldr	x0, [x0,1912]
	strb	wzr, [x0,18]
.L873:
	ldrb	w0, [x21,#:lo12:IDByte]
	cmp	w0, 44
	bne	.L874
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbz	w0, .L874
	mov	w0, 12336
	movk	w0, 0x5638, lsl 16
	cmp	w1, w0
	bne	.L875
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbnz	w0, .L874
.L875:
	add	x0, x20, :lo12:.LANCHOR2
	strb	wzr, [x0,572]
	mov	w0, 1
	bl	FlashSetInterfaceMode
	mov	w0, 1
	bl	NandcSetMode
.L874:
	add	x1, x20, :lo12:.LANCHOR2
	ldrb	w0, [x1,564]
	tst	w0, 6
	beq	.L876
	ldrb	w1, [x1,572]
	cbnz	w1, .L877
	tbnz	x0, 0, .L876
.L877:
	mov	w0, 0
	add	x20, x20, :lo12:.LANCHOR2
	bl	flash_enter_slc_mode
	ldr	w1, [x20,688]
	mov	w0, 0
	bl	FlashDdrParaScan
	mov	w0, 0
	bl	flash_exit_slc_mode
.L876:
	add	x19, x19, :lo12:.LANCHOR0
	mov	w20, 16
	add	x21, x21, :lo12:IDByte
	ldr	x0, [x19,1912]
	ldrb	w0, [x0,20]
	bl	FlashBchSel
	add	x0, x19, 1928
	bl	FlashReadIdbDataRaw
	strb	w20, [x19,89]
	mov	w0, 18928
	movk	w0, 0x2, lsl 16
	bl	FlashTimingCfg
	ldr	x1, [x19,1912]
	ldrb	w2, [x1,12]
	strh	w2, [x19,4024]
	ldrh	w4, [x1,14]
	ldrb	w2, [x1,7]
	str	w2, [x19,4020]
	ldr	w2, [x21]
	str	w2, [x19,4016]
	ldrb	w2, [x19,1672]
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
	ldrb	w5, [x19,89]
	strh	w5, [x19,4042]
	uxth	w2, w2
	ldrb	w5, [x19,88]
	strh	w2, [x19,4038]
	cmp	w5, 1
	bne	.L879
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	lsl	w3, w3, 1
	strh	w2, [x19,4038]
	strb	w20, [x19,89]
	mov	w2, 8
	strh	w4, [x19,4030]
	strh	w3, [x19,4036]
	strh	w2, [x19,4042]
.L879:
	ldrb	w0, [x1,20]
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	w0, 0
	b	.L898
.L881:
	mov	w0, -2
	b	.L898
.L837:
	strb	w5, [x3,728]
	b	.L836
.L898:
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
	adrp	x2, .LANCHOR0
	stp	x21, x22, [sp,32]
	uxtb	w21, w0
	add	x0, x2, :lo12:.LANCHOR0
	stp	x23, x24, [sp,48]
	str	x25, [sp,64]
	ldr	x3, [x0,1912]
	ldrb	w0, [x0,4044]
	ldrb	w20, [x3,19]
	mov	x3, x2
	cbz	w0, .L928
	adrp	x0, .LANCHOR2+568
	ldr	w0, [x0,#:lo12:.LANCHOR2+568]
	cbnz	w0, .L927
.L928:
	sub	w2, w20, #5
	uxtb	w2, w2
	cmp	w2, 3
	bls	.L929
	cmp	w20, 68
	beq	.L929
	sub	w2, w20, #19
	and	w2, w2, -17
	uxtb	w2, w2
	cbnz	w2, .L927
.L929:
	mov	w24, w1
	add	x22, x3, :lo12:.LANCHOR0
	mov	w25, 65535
	adrp	x23, .LANCHOR2
.L931:
	ldr	x0, [x22,1912]
	ldrh	w0, [x0,10]
	cmp	w0, w19
	bls	.L927
	add	x0, x22, 648
	ldrh	w0, [x0,w19,sxtw 1]
	cmp	w0, w25
	bne	.L927
	add	x0, x23, :lo12:.LANCHOR2
	cmp	w20, 8
	mov	w1, 0
	ldr	x0, [x0,696]
	beq	.L941
	mov	w1, 255
.L941:
	mov	w2, 32768
	bl	ftl_memset
	add	x0, x23, :lo12:.LANCHOR2
	add	w1, w19, w24
	add	w19, w19, 1
	ldr	x2, [x0,696]
	mov	w0, w21
	uxth	w19, w19
	mov	x3, x2
	bl	FlashProgPage
	b	.L931
.L927:
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
	add	x0, x0, 1676
	stp	x25, x26, [sp,64]
	str	x0, [x29,104]
	adrp	x0, .LC15
	ldrb	w25, [x1,#:lo12:.LANCHOR1+481]
	add	x0, x0, :lo12:.LC15
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	mov	w22, 0
	str	x0, [x29,96]
.L943:
	cmp	w22, w24
	beq	.L1007
	ldr	w1, [x29,116]
	sub	w4, w24, w22
	mov	x0, x19
	add	x2, x29, 140
	add	x3, x29, 136
	adrp	x21, .LANCHOR0
	bl	LogAddr2PhyAddr
	ldr	x1, [x29,120]
	ldr	w0, [x29,136]
	ldrb	w1, [x1,1672]
	cmp	w0, w1
	bcc	.L944
	mov	w0, -1
	str	w0, [x19]
	b	.L945
.L944:
	ldr	x1, [x29,104]
	mov	w23, 0
	adrp	x27, .LANCHOR2
	mov	w28, 256
	ldrb	w20, [x1,w0,uxtw]
	mov	w0, w20
	bl	NandcWaitFlashReady
	mov	w0, w20
	bl	NandcFlashCs
.L946:
	ldr	w1, [x29,140]
	mov	w0, w20
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
	ldrb	w0, [x0,728]
	cbz	w0, .L947
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w26, w26, w28, ls
.L947:
	cmn	w26, #1
	bne	.L948
	cmp	w23, 9
	bhi	.L948
	add	w23, w23, 1
	b	.L946
.L948:
	cmp	w23, wzr
	mov	w27, 256
	csel	w26, w26, w27, eq
	mov	w23, 0
.L950:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w1, [x29,140]
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
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldrb	w0, [x0,728]
	cbz	w0, .L953
	mov	w0, w20
	bl	flash_read_ecc
	cmp	w0, 5
	csel	w28, w28, w27, ls
.L953:
	cmn	w28, #1
	bne	.L954
	cmp	w23, 9
	bhi	.L954
	add	w23, w23, 1
	b	.L950
.L954:
	cmp	w23, wzr
	mov	w0, w20
	mov	w27, 256
	csel	w28, w28, w27, eq
	bl	NandcFlashDeCs
	add	x0, x21, :lo12:.LANCHOR0
	cmp	w28, w26
	csel	w28, w28, w26, cs
	ldrb	w0, [x0,4045]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L956
	cmn	w28, #1
	csel	w28, w28, w27, eq
.L956:
	cmp	w28, 256
	beq	.L967
	cmn	w28, #1
	bne	.L957
.L967:
	str	w28, [x19]
	b	.L959
.L957:
	str	wzr, [x19]
.L959:
	ldr	x0, [x19,16]
	cbz	x0, .L960
	ldr	w1, [x0,12]
	cmn	w1, #1
	bne	.L960
	ldr	w1, [x0,8]
	cmn	w1, #1
	bne	.L960
	ldr	w0, [x0]
	cmn	w0, #1
	beq	.L960
	str	w1, [x19]
.L960:
	ldr	w3, [x19]
	cmn	w3, #1
	bne	.L945
	add	x21, x21, :lo12:.LANCHOR0
	ldr	w1, [x19,4]
	ldr	x0, [x29,96]
	ldrb	w2, [x21,4045]
	bl	printk
	ldr	x1, [x19,8]
	cbz	x1, .L962
	adrp	x0, .LC16
	mov	w2, 4
	add	x0, x0, :lo12:.LC16
	mov	w3, 8
	bl	rknand_print_hex
.L962:
	ldr	x1, [x19,16]
	cbz	x1, .L945
	adrp	x0, .LC17
	mov	w2, 4
	add	x0, x0, :lo12:.LC17
	mov	w3, w2
	bl	rknand_print_hex
.L945:
	add	w22, w22, 1
	add	x19, x19, 56
	b	.L943
.L1007:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR0
	ldrb	w3, [x3,#:lo12:.LANCHOR1+481]
	add	x19, x19, :lo12:.LANCHOR0
	str	w3, [x29,136]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldrb	w3, [x19,96]
	mov	x26, x0
	ldrb	w23, [x19,88]
	stp	x21, x22, [sp,32]
	stp	x27, x28, [sp,80]
	str	w1, [x29,132]
	str	w2, [x29,128]
	str	w3, [x29,124]
	cbz	w23, .L1048
	bl	FlashReadSlc2KPages
	b	.L1097
.L1048:
	add	x0, x19, 1676
	str	x0, [x29,112]
	adrp	x0, .LC15
	mov	w25, w23
	add	x0, x0, :lo12:.LC15
	str	x0, [x29,104]
	adrp	x0, .LC17
	add	x0, x0, :lo12:.LC17
	str	x0, [x29,96]
.L1009:
	ldr	w0, [x29,132]
	cmp	w25, w0
	bcs	.L1099
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
	ldrb	w1, [x19,1672]
	mov	w21, w0
	ldr	w0, [x29,152]
	cmp	w0, w1
	bcc	.L1011
	mov	w0, -1
	str	w0, [x26,x27]
	b	.L1012
.L1011:
	ldr	x1, [x29,112]
	adrp	x24, .LANCHOR2
	ldrb	w20, [x1,w0,uxtw]
	add	x0, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,729]
	cmp	w0, wzr
	mov	w0, w20
	csel	w21, w21, wzr, ne
	bl	NandcWaitFlashReady
	ldr	x0, [x19,1912]
	ldrb	w1, [x0,19]
	sub	w0, w1, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1014
	sub	w1, w1, #7
	add	x0, x19, 4056
	sxtw	x2, w20
	add	x0, x0, x2
	uxtb	w1, w1
	cmp	w1, 1
	ldrb	w3, [x0,12]
	bhi	.L1015
	ldrb	w3, [x0,20]
.L1015:
	add	x0, x24, :lo12:.LANCHOR2
	add	x2, x0, x2
	ldrb	w0, [x2,560]
	cmp	w0, w3
	beq	.L1014
	ldrb	w1, [x19,4057]
	mov	w0, w20
	add	x2, x19, 4060
	bl	HynixSetRRPara
.L1014:
	mov	w0, w20
	bl	NandcFlashCs
	ldr	w0, [x29,128]
	cmp	w0, 1
	cset	w0, eq
	orr	w0, w0, w28, lsr 31
	str	w0, [x29,140]
	cbz	w0, .L1016
	ldrb	w0, [x19,4044]
	cbz	w0, .L1016
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1017
.L1016:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1017:
	cmp	w20, 255
	ldr	w1, [x29,156]
	bne	.L1055
	cmn	w1, #1
	cset	w0, ne
	cbz	w0, .L1049
.L1055:
	cbz	w21, .L1021
	ldr	w2, [x19,92]
	mov	w0, w20
	add	w2, w1, w2
	bl	FlashReadDpCmd
	b	.L1022
.L1021:
	mov	w0, w20
	bl	FlashReadCmd
	b	.L1022
.L1049:
	mov	w21, w0
.L1019:
	ldrb	w2, [x29,136]
	mov	w0, w20
	ldr	x3, [x22,8]
	mov	w1, 0
	ldr	x4, [x22,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w28, w0
	bne	.L1023
	ldrb	w0, [x19,96]
	cbz	w0, .L1023
	strb	wzr, [x19,96]
	mov	w21, 0
	b	.L1017
.L1023:
	cbz	w21, .L1024
	ldr	w2, [x19,92]
	mov	w0, w20
	ldr	w1, [x29,156]
	add	w1, w2, w1
	bl	FlashReadDpDataOutCmd
	add	w0, w25, 1
	mov	w4, 56
	ldrb	w2, [x29,136]
	mov	w1, 0
	umull	x4, w0, w4
	mov	w0, w20
	add	x4, x26, x4
	ldr	x3, [x4,8]
	ldr	x4, [x4,16]
	bl	NandcXferData
	cmn	w0, #1
	mov	w23, w0
	csel	w21, w21, wzr, ne
.L1024:
	mov	w0, w20
	bl	NandcFlashDeCs
	ldrb	w0, [x29,124]
	cmn	w28, #1
	strb	w0, [x19,96]
	bne	.L1031
	add	x0, x24, :lo12:.LANCHOR2
	ldrb	w0, [x0,572]
	cbnz	w0, .L1026
.L1030:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x21, [x0,672]
	cbnz	x21, .L1027
	b	.L1100
.L1026:
	ldr	x0, [x19,3976]
	mov	w4, 1
	ldr	w1, [x29,156]
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	ldr	w21, [x0,304]
	mov	w0, w20
	bl	FlashDdrTunningRead
	cmn	w0, #1
	mov	w28, w0
	beq	.L1029
	ldrb	w0, [x19,4045]
	cmp	w28, w0, lsr 1
	bls	.L1051
.L1029:
	lsr	w0, w21, 8
	bl	NandcSetDdrPara
	cmn	w28, #1
	beq	.L1030
	b	.L1051
.L1027:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	blr	x21
	cmn	w0, #1
	mov	w28, w0
	mov	w21, 0
	bne	.L1032
	ldr	x0, [x19,1912]
	ldrb	w0, [x0,19]
	sub	w0, w0, #1
	uxtb	w0, w0
	cmp	w0, 7
	bhi	.L1033
	ldrb	w1, [x19,4057]
	mov	w0, w20
	add	x2, x19, 4060
	mov	w3, w21
	bl	HynixSetRRPara
.L1033:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x22,8]
	mov	w21, 0
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	ldr	w1, [x22,4]
	adrp	x0, .LC18
	ldrb	w2, [x19,4045]
	add	x0, x0, :lo12:.LC18
	mov	w3, w28
	bl	printk
	cmn	w28, #1
	bne	.L1032
	ldrb	w0, [x19,4044]
	cbz	w0, .L1032
	ldr	w0, [x29,140]
	cbz	w0, .L1034
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1035
.L1034:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1035:
	add	x0, x24, :lo12:.LANCHOR2
	ldr	w1, [x29,156]
	ldr	x2, [x22,8]
	mov	w21, 0
	ldr	x3, [x22,16]
	ldr	x4, [x0,672]
	mov	w0, w20
	blr	x4
	mov	w28, w0
	b	.L1032
.L1100:
	ldr	w1, [x29,156]
	mov	w0, w20
	ldr	x2, [x22,8]
	ldr	x3, [x22,16]
	bl	FlashReadRawPage
	mov	w28, w0
	b	.L1032
.L1051:
	mov	w21, 0
.L1031:
	ldrb	w0, [x19,4045]
	add	w0, w0, w0, lsl 1
	cmp	w28, w0, lsr 2
	bls	.L1032
	add	x0, x24, :lo12:.LANCHOR2
	ldr	x0, [x0,672]
	cmp	x0, xzr
	mov	w0, 256
	csel	w28, w28, w0, ne
.L1032:
	cmp	w28, 256
	beq	.L1056
	cmn	w28, #1
	bne	.L1036
.L1056:
	str	w28, [x26,x27]
	b	.L1038
.L1036:
	str	wzr, [x26,x27]
.L1038:
	ldr	w3, [x26,x27]
	cmn	w3, #1
	bne	.L1040
	ldr	w1, [x22,4]
	ldr	x0, [x29,104]
	ldrb	w2, [x19,4045]
	bl	printk
	ldr	x1, [x22,16]
	cbz	x1, .L1040
	mov	w2, 4
	ldr	x0, [x29,96]
	mov	w3, w2
	bl	rknand_print_hex
.L1040:
	cbz	w21, .L1042
	ldrb	w0, [x19,4045]
	add	w0, w0, w0, lsl 1
	cmp	w23, w0, lsr 2
	bls	.L1043
	add	x24, x24, :lo12:.LANCHOR2
	ldr	x0, [x24,672]
	cmp	x0, xzr
	mov	w0, 256
	csel	w23, w23, w0, ne
.L1043:
	add	w0, w25, 1
	mov	w1, 56
	cmp	w23, 256
	umull	x0, w0, w1
	beq	.L1057
	cmn	w23, #1
	bne	.L1044
.L1057:
	str	w23, [x26,x0]
	b	.L1042
.L1044:
	str	wzr, [x26,x0]
.L1042:
	ldr	w0, [x29,140]
	add	w25, w25, w21
	cbz	w0, .L1012
	ldrb	w0, [x19,4044]
	cbz	w0, .L1012
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1012:
	add	w25, w25, 1
	b	.L1009
.L1099:
	mov	w0, 0
	b	.L1097
.L1022:
	mov	w0, w20
	bl	NandcWaitFlashReady
	cbz	w21, .L1019
	ldr	w1, [x29,156]
	mov	w0, w20
	bl	FlashReadDpDataOutCmd
	b	.L1019
.L1097:
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
	add	x0, x19, 736
	mov	w28, 0
	adrp	x26, .LC19
	add	x26, x26, :lo12:.LC19
	mul	w21, w1, w21
	mov	w1, 0
	uxth	w21, w21
	bl	ftl_memset
	add	w25, w21, w27
	ldr	x0, [x19,704]
	str	x0, [x29,152]
	mov	w19, w28
	sub	w0, w21, #12
	str	xzr, [x29,144]
	uxth	w25, w25
	str	w0, [x29,124]
.L1102:
	add	x1, x22, :lo12:.LANCHOR0
	ldrb	w1, [x1,1672]
	cmp	w1, w19
	bls	.L1112
	mul	w3, w19, w21
	mov	w20, w25
	mov	w4, 61664
.L1103:
	ldr	w0, [x29,124]
	cmp	w20, w0
	ble	.L1105
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
	beq	.L1104
	add	x24, x23, :lo12:.LANCHOR2
	ldr	x0, [x24,704]
	ldrh	w0, [x0]
	cmp	w0, w4
	bne	.L1104
	add	x24, x24, 736
	mov	x0, x26
	mov	w1, w19
	mov	w2, w20
	add	w28, w28, 1
	bl	printk
	strh	w20, [x24,w19,sxtw 1]
	uxth	w28, w28
	b	.L1105
.L1104:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1103
.L1105:
	add	x1, x22, :lo12:.LANCHOR0
	add	w19, w19, 1
	uxtb	w19, w19
	ldrb	w1, [x1,1672]
	cmp	w1, w28
	csel	w27, w27, wzr, ne
	b	.L1102
.L1112:
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
	ldr	x0, [x0,704]
	str	x0, [x29,104]
	uxth	w3, w3
	sub	w20, w3, #1
	sub	w19, w3, #16
	mul	w24, w23, w3
	uxth	w20, w20
.L1114:
	cmp	w20, w19
	ble	.L1122
	add	w0, w20, w24
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x29,92]
	add	x0, x29, 88
	bl	FlashReadPages
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1115
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,704]
	ldrh	w0, [x0]
	cmp	w0, w26
	bne	.L1115
	mov	w0, w22
	cbz	x22, .L1116
	mov	x0, x22
	mov	w1, w23
	mov	w2, w25
	bl	FlashReadFacBbtData.part.6
	b	.L1116
.L1115:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1114
.L1122:
	mov	w0, -1
.L1116:
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
	ldr	x0, [x0,696]
	uxth	w19, w19
	add	w2, w19, 7
	lsr	w2, w2, 3
	bl	FlashReadFacBbtData
	cmn	w0, #1
	bne	.L1124
.L1128:
	mov	w1, 0
	b	.L1125
.L1124:
	mov	w2, 0
	lsr	w0, w19, 4
	mov	w1, w2
	sub	w19, w19, #1
	mov	w5, 1
.L1126:
	cmp	w2, w19
	bge	.L1125
	add	x3, x20, :lo12:.LANCHOR2
	ubfx	x4, x2, 5, 11
	ldr	x6, [x3,696]
	lsl	w3, w5, w2
	ldr	w4, [x6,x4,lsl 2]
	tst	w3, w4
	beq	.L1127
	add	w3, w1, 1
	ubfiz	x1, x1, 1, 16
	strh	w2, [x21,x1]
	uxth	w1, w3
.L1127:
	cmp	w1, w0
	bcs	.L1128
	add	w2, w2, 1
	uxth	w2, w2
	b	.L1126
.L1125:
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
	add	x28, x22, 1676
.L1134:
	cmp	w25, w23
	beq	.L1163
	mov	w1, w24
	sub	w4, w23, w25
	mov	x0, x21
	add	x2, x29, 96
	add	x3, x29, 100
	bl	LogAddr2PhyAddr
	ldr	w0, [x29,100]
	ldrb	w1, [x22,1672]
	cmp	w0, w1
	bcc	.L1135
	mov	w0, -1
	str	w0, [x21]
	b	.L1136
.L1135:
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
	tbz	x0, 0, .L1139
	mov	w0, -1
	str	w0, [x21]
.L1139:
	mov	w0, w20
	bl	NandcFlashDeCs
.L1136:
	add	w25, w25, 1
	add	x21, x21, 56
	b	.L1134
.L1163:
	mov	w21, 0
	cbz	w27, .L1161
	adrp	x25, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	add	x25, x25, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1162:
	cmp	w21, w23
	beq	.L1161
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1142
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1143
.L1142:
	adrp	x20, .LANCHOR2
	sub	w4, w23, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 100
	mov	w1, w24
	add	x2, x29, 96
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,712]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,720]
	str	wzr, [x0]
	add	x0, x29, 104
	bl	memcpy
	ldr	x0, [x22,712]
	mov	w1, 1
	str	x0, [x29,112]
	mov	w2, w24
	ldr	x0, [x22,720]
	str	x0, [x29,120]
	add	x0, x29, 104
	bl	FlashReadPages
	ldr	w22, [x29,104]
	cmn	w22, #1
	bne	.L1144
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1144:
	ldr	x0, [x19,16]
	cbz	x0, .L1145
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,720]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1145
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1145:
	ldr	x0, [x19,8]
	cbz	x0, .L1143
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1143
	ldr	w1, [x19,4]
	mov	x0, x25
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1143:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1162
.L1161:
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
	adrp	x6, .LANCHOR1+481
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
	ldrb	w4, [x4,88]
	ldrb	w27, [x6,#:lo12:.LANCHOR1+481]
	ldrb	w5, [x5,19]
	cbz	w4, .L1165
	bl	FlashProgSlc2KPages
	b	.L1166
.L1165:
	sub	w0, w5, #1
	adrp	x28, .LANCHOR2
	str	w0, [x29,120]
	add	x0, x28, :lo12:.LANCHOR2
	str	x0, [x29,112]
.L1217:
	cmp	w22, w25
	bcs	.L1218
	mov	w20, 56
	sub	w4, w25, w22
	add	x2, x29, 128
	mov	w1, w23
	umull	x20, w22, w20
	add	x3, x29, 132
	add	x26, x19, x20
	mov	x0, x26
	bl	LogAddr2PhyAddr
	mov	w24, w0
	add	x2, x21, :lo12:.LANCHOR0
	ldr	w0, [x29,132]
	ldrb	w4, [x2,1672]
	cmp	w0, w4
	bcc	.L1167
	mov	w0, -1
	str	w0, [x19,x20]
	b	.L1168
.L1167:
	add	x1, x28, :lo12:.LANCHOR2
	add	x3, x2, 1716
	mov	x5, 24
	ldrb	w1, [x1,656]
	cmp	w1, wzr
	uxtw	x1, w0
	csel	w24, w24, wzr, ne
	madd	x0, x1, x5, x3
	ldr	x0, [x0,8]
	cbz	x0, .L1170
	cmp	w4, 1
	bne	.L1171
	ldr	x0, [x2,3976]
	bl	NandcIqrWaitFlashReady
.L1171:
	ldrb	w0, [x29,132]
	bl	FlashWaitCmdDone
.L1170:
	add	x0, x21, :lo12:.LANCHOR0
	ldr	w2, [x29,132]
	mov	x1, 24
	add	x0, x0, 1716
	madd	x0, x2, x1, x0
	ldr	w1, [x29,128]
	str	x26, [x0,8]
	str	xzr, [x0,16]
	str	w1, [x0,4]
	cbz	w24, .L1172
	add	w1, w22, 1
	mov	w3, 56
	umull	x1, w1, w3
	add	x1, x19, x1
	str	x1, [x0,16]
.L1172:
	add	x1, x21, :lo12:.LANCHOR0
	add	x0, x1, 1676
	ldrb	w20, [x0,x2]
	mov	x0, 24
	madd	x2, x2, x0, x1
	ldrb	w0, [x1,1672]
	strb	w20, [x2,1716]
	cmp	w0, 1
	bne	.L1173
	mov	w0, w20
	bl	NandcWaitFlashReady
	b	.L1174
.L1173:
	mov	w0, w20
	str	x1, [x29,104]
	bl	NandcFlashCs
	ldr	x1, [x29,104]
	ldr	w0, [x29,132]
	add	x1, x1, 1684
	ldr	w0, [x1,x0,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	mov	w0, w20
	bl	NandcFlashDeCs
.L1174:
	ldr	w0, [x29,120]
	cmp	w0, 7
	bhi	.L1175
	ldr	x0, [x29,112]
	add	x0, x0, x20, sxtw
	ldrb	w0, [x0,560]
	cbz	w0, .L1175
	add	x2, x21, :lo12:.LANCHOR0
	mov	w0, w20
	add	x2, x2, 4060
	mov	w3, 0
	ldrb	w1, [x2,-3]
	bl	HynixSetRRPara
.L1175:
	mov	w0, w20
	bl	NandcFlashCs
	cmp	w23, 1
	bne	.L1176
	add	x0, x21, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L1176
	mov	w0, w20
	bl	flash_enter_slc_mode
	b	.L1177
.L1176:
	mov	w0, w20
	bl	flash_exit_slc_mode
.L1177:
	ldr	w1, [x29,128]
	mov	w0, w20
	bl	FlashProgFirstCmd
	ldr	x3, [x26,8]
	mov	w0, w20
	ldr	x4, [x26,16]
	mov	w1, 1
	mov	w2, w27
	bl	NandcXferData
	cbz	w24, .L1178
	ldr	w1, [x29,128]
	mov	w0, w20
	add	x26, x21, :lo12:.LANCHOR0
	bl	FlashProgDpFirstCmd
	ldr	w1, [x29,132]
	add	x0, x26, 1684
	ldr	w0, [x0,x1,lsl 2]
	ldr	w1, [x29,128]
	cmp	w0, wzr
	mov	w0, w20
	cset	w2, ne
	bl	FlashWaitReadyEN
	ldr	w2, [x26,92]
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
.L1178:
	ldr	w1, [x29,128]
	mov	w0, w20
	add	w22, w22, w24
	bl	FlashProgSecondCmd
	mov	w0, w20
	bl	NandcFlashDeCs
.L1168:
	add	w22, w22, 1
	b	.L1217
.L1218:
	add	x21, x21, :lo12:.LANCHOR0
	mov	x20, 0
	add	x22, x21, 1716
	mov	x24, 24
	ldr	x0, [x21,3976]
	bl	NandcIqrWaitFlashReady
.L1180:
	ldrb	w0, [x21,1672]
	cmp	w0, w20
	bls	.L1219
	mov	w0, w20
	bl	FlashWaitCmdDone
	cmp	w23, 1
	bne	.L1181
	ldrb	w0, [x21,4044]
	cbz	w0, .L1181
	mul	x0, x20, x24
	ldrb	w0, [x0,x22]
	bl	flash_exit_slc_mode
.L1181:
	add	x20, x20, 1
	b	.L1180
.L1219:
	ldr	w0, [x29,124]
	cbnz	w0, .L1183
.L1191:
	mov	w0, 0
	b	.L1166
.L1183:
	adrp	x24, .LC23
	adrp	x26, .LC22
	adrp	x27, .LC21
	mov	w21, 0
	add	x24, x24, :lo12:.LC23
	add	x26, x26, :lo12:.LC22
	add	x27, x27, :lo12:.LC21
.L1184:
	cmp	w21, w25
	beq	.L1191
	ldr	w0, [x19]
	cmn	w0, #1
	bne	.L1185
	adrp	x0, .LC20
	ldr	w1, [x19,4]
	add	x0, x0, :lo12:.LC20
	bl	printk
	b	.L1186
.L1185:
	adrp	x20, .LANCHOR2
	sub	w4, w25, w21
	add	x22, x20, :lo12:.LANCHOR2
	add	x3, x29, 132
	mov	w1, w23
	add	x2, x29, 128
	mov	x0, x19
	bl	LogAddr2PhyAddr
	ldr	x0, [x22,712]
	mov	x1, x19
	mov	x2, 56
	str	wzr, [x0]
	ldr	x0, [x22,720]
	str	wzr, [x0]
	add	x0, x29, 136
	bl	memcpy
	ldr	x0, [x22,712]
	mov	w1, 1
	str	x0, [x29,144]
	mov	w2, w23
	ldr	x0, [x22,720]
	str	x0, [x29,152]
	add	x0, x29, 136
	bl	FlashReadPages
	ldr	w22, [x29,136]
	cmn	w22, #1
	bne	.L1187
	ldr	w1, [x19,4]
	mov	x0, x27
	bl	printk
	str	w22, [x19]
.L1187:
	ldr	x0, [x19,16]
	cbz	x0, .L1188
	ldr	w2, [x0]
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,720]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1188
	ldr	w1, [x19,4]
	mov	x0, x26
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1188:
	ldr	x0, [x19,8]
	cbz	x0, .L1186
	add	x20, x20, :lo12:.LANCHOR2
	ldr	w2, [x0]
	ldr	x0, [x20,712]
	ldr	w3, [x0]
	cmp	w2, w3
	beq	.L1186
	ldr	w1, [x19,4]
	mov	x0, x24
	bl	printk
	mov	w0, -1
	str	w0, [x19]
.L1186:
	add	w21, w21, 1
	add	x19, x19, 56
	b	.L1184
.L1166:
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
	ldr	x0, [x19,696]
	str	x0, [x29,48]
	add	x0, x29, 96
	str	x0, [x29,56]
	bl	ftl_memset
	ldr	x0, [x19,696]
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
	bcc	.L1222
	mov	w0, w1
	bl	FlashTestBlk.part.7
.L1222:
	ldp	x29, x30, [sp], 16
	ret
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	stp	x29, x30, [sp, -240]!
	adrp	x2, .LANCHOR2
	adrp	x4, .LANCHOR0
	add	x29, sp, 0
	add	x0, x2, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x23, x24, [sp,48]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x20, x4
	str	x2, [x29,136]
	ldr	x0, [x0,704]
	add	x24, x20, :lo12:.LANCHOR0
	str	x0, [x29,160]
	add	x0, x4, :lo12:.LANCHOR0
	mov	w19, 0
	ldrh	w1, [x0,4028]
	ldrh	w21, [x0,4030]
	mul	w21, w1, w21
	ldr	x1, [x0,1912]
	uxth	w21, w21
	ldrb	w1, [x1,24]
	str	w1, [x29,144]
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
	ldr	x0, [x29,136]
	mov	w1, 0
	mov	w2, 4096
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,704]
	bl	ftl_memset
	lsr	w0, w21, 4
	str	w0, [x29,168]
	add	x0, x24, 1676
	str	x0, [x29,120]
	ldr	w0, [x29,144]
	and	w0, w0, 1
	str	w0, [x29,116]
	sub	w0, w21, #1
	uxth	w0, w0
	str	w0, [x29,132]
.L1226:
	ldrb	w0, [x24,1672]
	cmp	w0, w19
	bls	.L1277
	ldr	x0, [x29,136]
	sxtw	x23, w19
	add	x0, x0, :lo12:.LANCHOR2
	add	x0, x0, 736
	ldrh	w20, [x0,w19,sxtw 1]
	cbnz	w20, .L1256
	ldrh	w2, [x24,4036]
	mov	w1, w20
	ldr	x0, [x24]
	mov	w25, w20
	lsl	w2, w2, 9
	mov	w22, w20
	add	x28, x24, 1684
	bl	ftl_memset
	ldr	x0, [x29,120]
	ldrb	w26, [x0,x23]
	ldr	w0, [x29,144]
	and	w0, w0, 2
	uxtb	w0, w0
	str	w0, [x29,128]
.L1228:
	uxth	w27, w25
	cmp	w27, w21
	bcs	.L1238
	mov	w0, -1
	strb	w0, [x29,182]
	strb	w0, [x29,183]
	ldr	w0, [x29,116]
	cbz	w0, .L1230
	ldr	w3, [x28,x23,lsl 2]
	mov	w0, w26
	add	x2, x29, 182
	add	w3, w20, w3
	str	x3, [x29,104]
	mov	w1, w3
	bl	FlashReadSpare
	ldrb	w0, [x24,88]
	ldr	x3, [x29,104]
	cmp	w0, 1
	bne	.L1230
	ldr	w1, [x24,92]
	mov	w0, w26
	add	x2, x29, 183
	add	w1, w3, w1
	bl	FlashReadSpare
	ldrb	w0, [x29,182]
	ldrb	w1, [x29,183]
	and	w0, w1, w0
	strb	w0, [x29,182]
.L1230:
	ldr	w0, [x29,128]
	cbz	w0, .L1232
	ldr	x0, [x24,1912]
	add	x2, x29, 183
	ldrh	w1, [x0,10]
	ldr	w0, [x28,x23,lsl 2]
	sub	w1, w1, #1
	add	w1, w1, w0
	mov	w0, w26
	add	w1, w1, w20
	bl	FlashReadSpare
.L1232:
	ldr	x0, [x24,1912]
	ldrb	w1, [x29,182]
	ldrb	w0, [x0,7]
	cmp	w0, 8
	beq	.L1257
	cmp	w0, 1
	bne	.L1233
.L1257:
	mov	w0, 1
	cbz	w1, .L1235
	ldrb	w0, [x29,183]
	cmp	w0, wzr
	cset	w0, eq
	b	.L1235
.L1233:
	cmp	w1, 255
	mov	w0, 1
	bne	.L1235
	ldrb	w0, [x29,183]
	cmp	w0, 255
	cset	w0, ne
.L1235:
	ldr	x1, [x29,144]
	tbz	x1, 2, .L1236
	ldr	w1, [x28,x23,lsl 2]
	mov	w0, w26
	add	w1, w20, w1
	bl	SandiskProgTestBadBlock
.L1236:
	cbz	w0, .L1237
	adrp	x0, .LC25
	mov	w1, w19
	mov	w2, w25
	add	x0, x0, :lo12:.LC25
	add	w22, w22, 1
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x24]
	lsl	x1, x1, 2
	mov	w0, 1
	lsl	w27, w0, w27
	uxth	w22, w22
	ldr	w0, [x2,x1]
	orr	w27, w0, w27
	str	w27, [x2,x1]
	ldr	w1, [x29,168]
	ldrb	w0, [x24,1672]
	mul	w0, w1, w0
	cmp	w22, w0
	bgt	.L1238
.L1237:
	ldr	w0, [x29,172]
	add	w25, w25, 1
	add	w20, w20, w0
	b	.L1228
.L1238:
	adrp	x0, .LC26
	mov	w1, w19
	add	x0, x0, :lo12:.LC26
	mov	w2, w22
	bl	printk
	ldrb	w0, [x24,1672]
	ldr	w1, [x29,168]
	mul	w0, w1, w0
	cmp	w22, w0
	blt	.L1240
	ldrh	w2, [x24,4036]
	mov	w1, 0
	ldr	x0, [x24]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1240:
	cbnz	w19, .L1242
	adrp	x25, .LC27
	ldrh	w26, [x24,8]
	mov	w20, w19
	add	x25, x25, :lo12:.LC27
	mov	w22, 1
.L1243:
	ldrb	w0, [x24,89]
	cmp	w0, w26
	bls	.L1278
	mov	w0, w26
	bl	FlashTestBlk
	cbz	w0, .L1244
	mov	w1, w26
	mov	x0, x25
	add	w20, w20, 1
	bl	printk
	ubfx	x1, x26, 5, 11
	ldr	x2, [x24]
	lsl	x1, x1, 2
	lsl	w0, w22, w26
	uxth	w20, w20
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1244:
	add	w26, w26, 1
	uxth	w26, w26
	b	.L1243
.L1278:
	adrp	x25, .LC27
	ldr	w27, [x29,132]
	sub	w26, w21, #50
	add	x25, x25, :lo12:.LC27
	mov	w22, 1
.L1246:
	cmp	w27, w26
	ble	.L1279
	mov	w0, w27
	bl	FlashTestBlk
	cbz	w0, .L1247
	mov	w1, w27
	mov	x0, x25
	bl	printk
	ubfx	x1, x27, 5, 11
	ldr	x2, [x24]
	lsl	x1, x1, 2
	lsl	w0, w22, w27
	ldr	w3, [x2,x1]
	orr	w0, w3, w0
	str	w0, [x2,x1]
.L1247:
	sub	w27, w27, #1
	uxth	w27, w27
	b	.L1246
.L1279:
	ldrb	w0, [x24,89]
	ldr	w1, [x24,8]
	sub	w0, w0, w1
	cmp	w20, w0
	bcc	.L1242
	ldrh	w2, [x24,4036]
	mov	w1, 0
	ldr	x0, [x24]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1242:
	ldr	x0, [x29,136]
	adrp	x22, .LC28
	ldr	w26, [x29,132]
	add	x22, x22, :lo12:.LC28
	mul	w25, w19, w21
	add	x27, x0, :lo12:.LANCHOR2
	mov	w20, -3872
.L1250:
	mov	w2, w26
	mov	x0, x22
	mov	w1, w19
	bl	printk
	ldr	x2, [x24]
.L1251:
	ubfx	x0, x26, 5, 11
	ldr	w0, [x2,x0,lsl 2]
	lsr	w0, w0, w26
	and	w1, w0, 1
	tbz	x0, 0, .L1280
	sub	w26, w26, #1
	uxth	w26, w26
	b	.L1251
.L1280:
	add	x0, x27, 736
	strh	w26, [x0,x23,lsl 1]
	ldr	x0, [x29,160]
	strh	w20, [x0]
	strh	w26, [x0,2]
	strh	w1, [x0,8]
	mov	w1, 1
	ldr	x0, [x24]
	mov	w2, w1
	str	x0, [x29,192]
	ldr	x0, [x27,704]
	str	x0, [x29,200]
	add	w0, w26, w25
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
	cbz	w0, .L1256
	sub	w26, w26, #1
	uxth	w26, w26
	b	.L1250
.L1256:
	add	w19, w19, 1
	uxtb	w19, w19
	b	.L1226
.L1277:
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
.L1282:
	cmp	w2, w0
	bhi	.L1284
	add	w1, w1, 1
	lsl	w2, w2, 1
	uxth	w1, w1
	b	.L1282
.L1284:
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
	ldrh	w2, [x1,756]
	ldrh	w3, [x1,766]
	str	w0, [x1,752]
	mul	w2, w0, w2
	sub	w0, w3, w0
	strh	w0, [x1,764]
	ldr	w0, [x1,772]
	str	w2, [x1,760]
	sub	w2, w0, w2
	mov	w0, 0
	str	w2, [x1,768]
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
	adrp	x19, .LANCHOR2
	mov	x24, x0
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w4, [x24,14]
	ldrh	w6, [x0,8]
	ldrh	w2, [x0,10]
	ldrh	w0, [x0,12]
	strh	w6, [x1,776]
	strh	w2, [x1,778]
	strh	w0, [x1,780]
	strh	w4, [x1,766]
.L1289:
	add	x5, x1, 784
	strb	w3, [x3,x5]
	add	x3, x3, 1
	cmp	x3, 32
	bne	.L1289
	ldrh	w3, [x24,20]
	ldrb	w1, [x24,15]
	cmp	w3, w1
	bcs	.L1290
	uxtb	w8, w0
	mov	w3, 0
	mul	w11, w0, w2
	ubfiz	w10, w8, 1, 7
	add	x12, x19, :lo12:.LANCHOR2
.L1291:
	cmp	w3, w0
	bcs	.L1293
	uxtb	w1, w3
	mov	w5, w3
	mov	w7, 0
.L1294:
	cmp	w7, w2
	bcs	.L1316
	add	x9, x12, 784
	add	w13, w5, w11
	add	w14, w1, w8
	add	w7, w7, 1
	strb	w1, [x9,w5,uxtw]
	add	w1, w1, w10
	strb	w14, [x9,x13]
	add	w5, w5, w0
	uxtb	w1, w1
	b	.L1294
.L1316:
	add	w3, w3, 1
	b	.L1291
.L1293:
	add	x1, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 1
	lsr	w4, w4, 1
	strh	w2, [x1,778]
	strh	w4, [x1,766]
.L1290:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w2, 5
	cmp	w6, 1
	strh	w2, [x1,816]
	strh	wzr, [x1,818]
	bne	.L1295
	strh	w6, [x1,816]
.L1295:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w2, 4352
	adrp	x21, .LANCHOR0
	strh	w2, [x1,820]
	add	x2, x21, :lo12:.LANCHOR0
	ldrb	w27, [x2,88]
	cbz	w27, .L1296
	mov	w2, 384
	strh	w2, [x1,820]
.L1296:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w26, [x24,16]
	ldrh	w25, [x24,20]
	ldrh	w28, [x24,18]
	ldrh	w23, [x22,778]
	ldrh	w20, [x22,766]
	strh	w26, [x22,824]
	mul	w23, w0, w23
	mul	w0, w20, w0
	strh	w0, [x22,822]
	uxth	w23, w23
	strh	w28, [x22,826]
	strh	w23, [x22,756]
	mul	w0, w23, w26
	strh	w0, [x22,828]
	mov	w0, w25
	strh	w25, [x22,830]
	bl	Ftl_log2
	uxth	w3, w0
	lsl	w2, w25, 9
	strh	w0, [x22,832]
	ldrh	w0, [x24,26]
	cmp	w20, 1024
	strh	w2, [x22,834]
	ubfx	x2, x2, 9, 7
	strh	w0, [x22,838]
	lsl	w2, w2, 1
	mul	w0, w23, w20
	strh	w2, [x22,836]
	str	w0, [x22,772]
	bls	.L1297
	and	w0, w20, 255
	strh	w0, [x22,818]
.L1297:
	add	x2, x19, :lo12:.LANCHOR2
	mul	w0, w25, w28
	ldrh	w1, [x2,818]
	sub	w1, w20, w1
	mul	w1, w23, w1
	mul	w1, w1, w25
	mul	w1, w1, w26
	asr	w1, w1, 11
	str	w1, [x2,840]
	ldrh	w1, [x2,820]
	lsl	w1, w1, 3
	sdiv	w0, w1, w0
	uxth	w0, w0
	cmp	w0, 4
	bhi	.L1313
	mov	w0, 4
.L1313:
	strh	w0, [x2,844]
	cbz	w27, .L1300
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 640
	strh	w1, [x0,820]
.L1300:
	add	x1, x19, :lo12:.LANCHOR2
	lsl	w20, w20, 6
	cmp	w23, 1
	ldrh	w0, [x1,820]
	asr	w0, w0, w3
	add	w3, w3, 9
	asr	w20, w20, w3
	strh	w20, [x1,848]
	and	w20, w20, 65535
	add	w0, w0, 2
	strh	w0, [x1,846]
	mul	w0, w20, w23
	str	w0, [x1,852]
	add	w20, w20, 8
	ldrh	w0, [x1,844]
	udiv	w0, w0, w23
	add	w20, w20, w0
	bne	.L1314
	add	w20, w20, 4
.L1314:
	str	w20, [x1,752]
	add	x20, x19, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x20,752]
	bl	FtlSysBlkNumInit
	str	wzr, [x21,4048]
	ldr	w0, [x20,752]
	str	w0, [x20,856]
	ldr	w0, [x20,768]
	ldrh	w1, [x20,824]
	lsl	w0, w0, 2
	ldrh	w3, [x20,830]
	ldrh	w2, [x20,844]
	mul	w1, w0, w1
	ldrh	w0, [x20,832]
	ldrb	w5, [x21,4044]
	add	w0, w0, 9
	lsr	w0, w1, w0
	mov	w1, 2048
	add	w0, w0, 2
	sdiv	w1, w1, w3
	strh	w1, [x20,862]
	uxth	w0, w0
	strh	w0, [x20,860]
	add	w1, w2, 3
	strh	w1, [x20,844]
	ldr	w1, [x20,852]
	add	w4, w1, 3
	str	w4, [x20,852]
	cbz	w5, .L1303
	add	w2, w2, 4
	add	w1, w1, 5
	strh	w2, [x20,844]
	b	.L1315
.L1303:
	cmp	w4, 7
	bhi	.L1304
	mov	w1, 8
.L1315:
	str	w1, [x20,852]
.L1304:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x19,764]
	strh	wzr, [x19,864]
	lsr	w2, w1, 3
	add	w1, w2, w1, lsl 1
	add	w1, w1, 52
	add	w0, w1, w0, lsl 2
	cmp	w0, w3, lsl 9
	bcs	.L1305
	mov	w0, 1
	strh	w0, [x19,864]
.L1305:
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
	str	w1, [x0,936]
	mov	w1, -1
	strh	wzr, [x0,866]
	strh	w1, [x0,952]
	strh	w1, [x0,954]
	strh	w1, [x0,956]
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
	str	wzr, [x0,932]
	str	wzr, [x0,940]
	str	wzr, [x0,944]
	str	wzr, [x0,948]
	strh	w1, [x0,958]
	mov	w1, 32
	ldrh	w2, [x0,830]
	strh	w1, [x0,960]
	mov	w1, 128
	strh	w1, [x0,962]
	mov	w1, 1024
	strh	wzr, [x0,964]
	sdiv	w2, w1, w2
	ldrh	w1, [x0,756]
	strh	wzr, [x0,966]
	lsl	w1, w1, 2
	strh	wzr, [x0,968]
	strh	wzr, [x0,970]
	cmp	w2, w1
	str	w2, [x0,972]
	bls	.L1318
	str	w1, [x0,972]
.L1318:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w22, 56
	ldrh	w0, [x19,828]
	str	wzr, [x19,976]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,984]
	ldrh	w1, [x19,828]
	mov	w0, 12
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,756]
	str	x0, [x19,992]
	mul	w21, w21, w22
	lsl	w23, w21, 3
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,1000]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1008]
	mov	w0, w23
	bl	ftl_malloc
	str	x0, [x19,1016]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1024]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1032]
	ldr	w0, [x19,972]
	mul	w0, w0, w22
	bl	ftl_malloc
	str	x0, [x19,1040]
	ldrh	w0, [x19,756]
	ldrh	w21, [x19,834]
	lsl	w0, w0, 1
	add	w0, w0, 1
	str	w0, [x19,1048]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1056]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1064]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1072]
	ldr	w0, [x19,1048]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,1080]
	ldr	w0, [x19,972]
	mul	w0, w21, w0
	bl	ftl_malloc
	str	x0, [x19,1088]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1096]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1104]
	ldr	w1, [x19,1048]
	mov	w0, 24
	mul	w0, w1, w0
	bl	ftl_malloc
	ldrh	w21, [x19,756]
	str	x0, [x19,1112]
	ldrh	w0, [x19,836]
	mul	w21, w0, w21
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1120]
	lsl	w0, w21, 3
	bl	ftl_malloc
	str	x0, [x19,1128]
	ldrh	w1, [x19,836]
	ldr	w0, [x19,1048]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1136]
	ldrh	w1, [x19,836]
	ldr	w0, [x19,972]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1144]
	ldrh	w0, [x19,766]
	lsl	w0, w0, 1
	strh	w0, [x19,1152]
	and	w0, w0, 65534
	bl	ftl_malloc
	str	x0, [x19,1160]
	ldrh	w0, [x19,1152]
	add	x0, x0, 547
	lsr	x0, x0, 9
	strh	w0, [x19,1152]
	lsl	w0, w0, 9
	bl	ftl_malloc
	ldrh	w21, [x19,766]
	str	x0, [x19,1168]
	add	x0, x0, 32
	lsl	w21, w21, 1
	str	x0, [x19,1176]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1184]
	mov	w0, w21
	bl	ftl_malloc
	ldr	w21, [x19,852]
	str	x0, [x19,1192]
	lsl	w21, w21, 1
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1200]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1208]
	ldrh	w0, [x19,766]
	lsr	w0, w0, 3
	add	w0, w0, 4
	bl	ftl_malloc
	adrp	x1, .LANCHOR0+16
	str	x0, [x1,#:lo12:.LANCHOR0+16]
	ldrh	w0, [x19,844]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1216]
	ldrh	w0, [x19,844]
	lsl	w0, w0, 1
	bl	ftl_malloc
	str	x0, [x19,1224]
	ldrh	w0, [x19,844]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1232]
	ldrh	w0, [x19,846]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1240]
	ldrh	w2, [x19,846]
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w21, [x19,860]
	lsl	w21, w21, 2
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1248]
	mov	w0, w21
	bl	ftl_malloc
	str	x0, [x19,1256]
	ldr	w0, [x19,852]
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1264]
	ldrh	w0, [x19,862]
	lsl	w0, w0, 4
	bl	ftl_malloc
	str	x0, [x19,1272]
	ldrh	w1, [x19,862]
	ldrh	w0, [x19,834]
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1280]
	ldrh	w1, [x19,766]
	mov	w0, 6
	mul	w0, w1, w0
	bl	ftl_malloc
	str	x0, [x19,1288]
	ldrh	w0, [x19,822]
	ldrh	w1, [x19,778]
	add	w0, w0, 31
	asr	w0, w0, 5
	strh	w0, [x19,1296]
	mul	w0, w0, w1
	lsl	w0, w0, 2
	bl	ftl_malloc
	str	x0, [x19,1336]
	add	x1, x19, 1304
	ldrh	w4, [x19,1296]
	ldrh	w5, [x19,778]
	mov	w0, 1
	mov	w2, w4
.L1319:
	cmp	w0, w5
	add	x1, x1, 8
	bcs	.L1419
	ldr	x3, [x19,1336]
	add	w0, w0, 1
	add	x3, x3, x2, uxtw 2
	add	w2, w2, w4
	str	x3, [x1,32]
	b	.L1319
.L1419:
	add	x1, x20, :lo12:.LANCHOR2
.L1321:
	cmp	w0, 8
	beq	.L1420
	add	x2, x1, x0, uxtw 3
	add	w0, w0, 1
	str	xzr, [x2,1336]
	b	.L1321
.L1420:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1200]
	cbnz	x1, .L1323
.L1325:
	adrp	x0, .LC29
	adrp	x1, .LANCHOR3
	add	x0, x0, :lo12:.LC29
	add	x1, x1, :lo12:.LANCHOR3
	bl	printk
	mov	w0, -1
	b	.L1324
.L1323:
	ldr	x1, [x0,1208]
	cbz	x1, .L1325
	ldr	x1, [x0,1248]
	cbz	x1, .L1325
	ldr	x1, [x0,1264]
	cbz	x1, .L1325
	ldr	x1, [x0,1272]
	cbz	x1, .L1325
	ldr	x1, [x0,1280]
	cbz	x1, .L1325
	ldr	x1, [x0,1288]
	cbz	x1, .L1325
	ldr	x1, [x0,1336]
	cbz	x1, .L1325
	ldr	x0, [x0,1192]
	cbz	x0, .L1325
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,984]
	cbz	x1, .L1325
	ldr	x1, [x0,992]
	cbz	x1, .L1325
	ldr	x1, [x0,1000]
	cbz	x1, .L1325
	ldr	x1, [x0,1016]
	cbz	x1, .L1325
	ldr	x1, [x0,1024]
	cbz	x1, .L1325
	ldr	x1, [x0,1032]
	cbz	x1, .L1325
	ldr	x1, [x0,1008]
	cbz	x1, .L1325
	ldr	x1, [x0,1056]
	cbz	x1, .L1325
	ldr	x1, [x0,1064]
	cbz	x1, .L1325
	ldr	x0, [x0,1072]
	cbz	x0, .L1325
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x1, [x0,1080]
	cbz	x1, .L1325
	ldr	x1, [x0,1096]
	cbz	x1, .L1325
	ldr	x1, [x0,1104]
	cbz	x1, .L1325
	ldr	x1, [x0,1112]
	cbz	x1, .L1325
	ldr	x1, [x0,1120]
	cbz	x1, .L1325
	ldr	x1, [x0,1128]
	cbz	x1, .L1325
	ldr	x1, [x0,1136]
	cbz	x1, .L1325
	ldr	x1, [x0,1176]
	cbz	x1, .L1325
	ldr	x1, [x0,1160]
	cbz	x1, .L1325
	ldr	x0, [x0,1216]
	cbz	x0, .L1325
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x1, [x0,1224]
	cbz	x1, .L1325
	ldr	x1, [x0,1232]
	cbz	x1, .L1325
	ldr	x0, [x0,1240]
	cbz	x0, .L1325
	mov	w0, 0
.L1324:
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
	ldrh	w2, [x1,1400]
	cbz	w2, .L1422
	ldrh	w2, [x1,844]
	ldr	x3, [x1,1216]
	mov	x1, 0
.L1423:
	cmp	w2, w1, uxth
	bls	.L1428
	add	x1, x1, 1
	add	x0, x3, x1, lsl 1
	ldrh	w0, [x0,-2]
	cmp	w0, w4
	bne	.L1423
	mov	w0, 1
	b	.L1422
.L1428:
	mov	w0, 0
.L1422:
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
	ldr	w3, [x2,976]
	cbz	w3, .L1430
	ldr	x5, [x2,1040]
	mov	x0, 56
	mov	x2, 0
.L1431:
	cmp	w3, w2
	bls	.L1440
	madd	x4, x2, x0, x5
	add	x2, x2, 1
	ldr	w4, [x4,24]
	cmp	w4, w6
	bcc	.L1431
	cmp	w4, w1
	bhi	.L1431
	mov	w0, 1
	b	.L1430
.L1440:
	mov	w0, 0
.L1430:
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
	adrp	x0, .LANCHOR2+1404
	ldr	w0, [x0,#:lo12:.LANCHOR2+1404]
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
	ldrh	w3, [x19,822]
	add	x19, x19, 1304
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
	ldrh	w1, [x3,822]
	udiv	w2, w0, w1
	msub	w0, w2, w1, w0
	add	x2, x3, x2, uxth 3
	ldr	x1, [x2,1336]
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
	adrp	x0, .LANCHOR2+1296
	mov	x19, x1
	ldrh	w2, [x0,#:lo12:.LANCHOR2+1296]
	mov	x0, x1
	mov	w1, 0
	lsl	w2, w2, 2
	bl	ftl_memset
	mov	x3, 0
	mov	w0, 65535
	mov	w5, 1
.L1449:
	ldrh	w2, [x20,x3]
	cmp	w2, w0
	beq	.L1447
	ubfx	x4, x2, 5, 11
	add	x3, x3, 2
	lsl	x4, x4, 2
	lsl	w2, w5, w2
	cmp	x3, 1024
	ldr	w1, [x19,x4]
	orr	w2, w1, w2
	str	w2, [x19,x4]
	bne	.L1449
.L1447:
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
	ldr	w1, [x19,1408]
	stp	x25, x26, [sp,64]
	cbnz	w1, .L1454
	ldr	x0, [x19,1056]
	mov	x22, 0
	ldr	x3, [x19,1120]
	add	x21, x19, 1304
	ldrh	w2, [x19,834]
	str	x0, [x19,1424]
	str	x3, [x19,1432]
	bl	ftl_memset
.L1455:
	ldrh	w1, [x19,778]
	mov	w0, w22
	add	x20, x19, 1416
	add	x22, x22, 1
	cmp	w0, w1
	bge	.L1468
	add	x1, x21, x22, lsl 3
	ldrh	w2, [x19,1296]
	ldr	x3, [x20,8]
	ldr	x1, [x1,24]
	mul	w0, w0, w2
	add	x0, x3, x0, sxtw 2
	lsl	w2, w2, 2
	bl	ftl_memcpy
	b	.L1455
.L1468:
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
	ldrh	w0, [x19,1304]
	strh	w0, [x26,2]
	ldrh	w0, [x21,4]
	strh	w0, [x26,8]
	ldrh	w0, [x21,6]
	strh	w0, [x26,10]
	ldr	w0, [x19,752]
	strh	w0, [x26,12]
.L1457:
	ldr	x0, [x19,1056]
	str	x0, [x20,8]
	ldrh	w1, [x19,1304]
	ldr	x0, [x19,1120]
	str	x0, [x20,16]
	ldrh	w2, [x19,1306]
	ldrh	w3, [x19,1308]
	orr	w0, w2, w1, lsl 10
	ldrh	w4, [x26,10]
	str	w0, [x20,4]
	mov	x0, x24
	str	wzr, [x20]
	bl	printk
	ldrh	w1, [x19,826]
	ldrh	w0, [x19,1306]
	sub	w1, w1, #1
	cmp	w0, w1
	blt	.L1458
	ldr	w1, [x19,1312]
	ldrh	w0, [x19,1304]
	add	w1, w1, 1
	str	w1, [x19,1312]
	str	w1, [x26,4]
	strh	w0, [x26,8]
	ldrh	w1, [x19,1308]
	strh	w0, [x19,1308]
	ldr	x0, [x19,1024]
	strh	w1, [x19,1304]
	lsl	w1, w1, 10
	str	w1, [x20,4]
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	strh	wzr, [x19,1306]
	ldr	x0, [x19,1024]
	bl	FlashEraseBlocks
.L1458:
	mov	w1, 1
	mov	x0, x20
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,1306]
	ldr	w0, [x20]
	add	w1, w1, 1
	strh	w1, [x19,1306]
	cmn	w0, #1
	bne	.L1459
	ldr	w1, [x20,4]
	mov	x0, x25
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	cmp	w22, 3
	bls	.L1457
	adrp	x0, .LC33
	ldr	w1, [x20,4]
	add	x0, x0, :lo12:.LC33
	mov	w2, w22
	bl	printk
	mov	w0, 1
	str	w0, [x19,1408]
	b	.L1454
.L1459:
	add	w23, w23, 1
	cmp	w23, 1
	beq	.L1457
	cmp	w0, 256
	beq	.L1457
.L1454:
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
	add	x21, x1, 1416
	add	x23, x1, 1304
	ldr	x0, [x1,1056]
	mov	w22, 0
	ldr	x24, [x1,1120]
	mov	x19, x1
	str	x0, [x21,8]
	mov	w25, -1
	str	x24, [x21,16]
	mov	w26, 61664
.L1470:
	ldrh	w0, [x19,778]
	cmp	w22, w0
	bcs	.L1479
	ldrh	w20, [x19,822]
	strh	w25, [x23,12]
.L1472:
	sub	w20, w20, #1
	ldrh	w0, [x19,822]
	uxth	w20, w20
	sub	w1, w0, #16
	cmp	w20, w1
	ble	.L1473
	madd	w0, w22, w0, w20
	mov	w1, 1
	lsl	w0, w0, 10
	mov	w2, w1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1472
	ldrh	w0, [x24]
	cmp	w0, w26
	bne	.L1472
	strh	w20, [x23,12]
.L1473:
	add	w22, w22, 1
	add	x23, x23, 2
	b	.L1470
.L1479:
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
	add	x0, x0, 1316
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
	ldrh	w1, [x0,822]
	ldrh	w21, [x0,778]
	mul	w21, w1, w21
.L1482:
	cmp	w20, w21
	bge	.L1488
	mov	w0, w20
	bl	FtlBbmIsBadBlock
	cbz	w0, .L1483
	add	w19, w19, 1
	uxth	w19, w19
.L1483:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1482
.L1488:
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
	ldr	w22, [x20,1408]
	cbnz	w22, .L1490
	add	x24, x20, 1304
	mov	x19, x20
	mov	x25, x24
	add	x20, x20, 1416
	mov	w28, -3872
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
.L1491:
	ldrh	w0, [x19,778]
	cmp	w22, w0
	bcs	.L1509
	ldrh	w4, [x25,12]
	mov	w1, 65535
	ldr	x0, [x19,1056]
	ldr	x26, [x19,1120]
	cmp	w4, w1
	str	x0, [x20,8]
	str	x26, [x20,16]
	beq	.L1492
	ldrh	w0, [x19,822]
	mov	w1, 1
	mov	w2, w1
	madd	w27, w22, w0, w4
	lsl	w0, w27, 10
	str	w0, [x20,4]
	mov	x0, x20
	bl	FlashReadPages
	ldrh	w2, [x19,822]
	ldr	x0, [x24,32]
	add	w2, w2, 7
	ldr	x1, [x20,8]
	lsr	w2, w2, 3
	bl	ftl_memcpy
	b	.L1493
.L1492:
	mov	w1, w22
	bl	FlashGetBadBlockList
	ldr	x0, [x20,8]
	ldr	x1, [x24,32]
	bl	FtlBbt2Bitmap
	ldrh	w23, [x19,822]
.L1495:
	sub	w23, w23, #1
	uxth	w23, w23
.L1494:
	ldrh	w0, [x19,822]
	madd	w0, w22, w0, w23
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1495
	ldr	x0, [x19,1120]
	mov	w1, 0
	mov	w2, 16
	strh	w23, [x25,12]
	bl	ftl_memset
	strh	w28, [x26]
	str	wzr, [x26,4]
	ldrh	w2, [x19,1296]
	ldrh	w0, [x25,12]
	strh	w0, [x26,2]
	lsl	w2, w2, 2
	ldrh	w0, [x19,822]
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
	bne	.L1493
	mov	w0, w27
	bl	FtlBbmMapBadBlock
	b	.L1494
.L1493:
	mov	w0, w27
	add	w22, w22, 1
	add	x24, x24, 8
	add	x25, x25, 2
	bl	FtlBbmMapBadBlock
	b	.L1491
.L1509:
	mov	w19, 0
.L1498:
	add	x20, x21, :lo12:.LANCHOR2
	ldrh	w0, [x20,838]
	cmp	w0, w19
	bls	.L1510
	mov	w0, w19
	add	w19, w19, 1
	bl	FtlBbmMapBadBlock
	uxth	w19, w19
	b	.L1498
.L1510:
	ldrh	w19, [x20,1316]
	mov	w23, 65535
	sub	w19, w19, #1
	uxth	w19, w19
.L1500:
	add	x22, x20, 1304
	ldrh	w0, [x22,12]
	sub	w0, w0, #48
	cmp	w19, w0
	ble	.L1504
	mov	w0, w19
	bl	FtlBbmIsBadBlock
	cmp	w0, 1
	beq	.L1501
	mov	w0, w19
	bl	FlashTestBlk
	cbz	w0, .L1502
	mov	w0, w19
	bl	FtlBbmMapBadBlock
	b	.L1501
.L1502:
	ldrh	w0, [x20,1304]
	cmp	w0, w23
	bne	.L1503
	strh	w19, [x20,1304]
	b	.L1501
.L1503:
	strh	w19, [x22,4]
	b	.L1504
.L1501:
	sub	w19, w19, #1
	uxth	w19, w19
	b	.L1500
.L1504:
	add	x19, x21, :lo12:.LANCHOR2
	mov	w2, 2
	ldr	x1, [x19,1024]
	ldrh	w0, [x19,1304]
	str	wzr, [x19,1312]
	lsl	w0, w0, 10
	str	w0, [x1,4]
	strh	wzr, [x19,1306]
	ldr	x0, [x19,1024]
	ldrh	w1, [x19,1308]
	lsl	w1, w1, 10
	str	w1, [x0,60]
	mov	w1, 1
	bl	FlashEraseBlocks
	ldrh	w0, [x19,1304]
	bl	FtlBbmMapBadBlock
	ldrh	w0, [x19,1308]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	strh	wzr, [x19,1306]
	ldr	w0, [x19,1312]
	ldrh	w1, [x19,1308]
	add	w0, w0, 1
	str	w0, [x19,1312]
	ldrh	w0, [x19,1304]
	strh	w0, [x19,1308]
	strh	w1, [x19,1304]
	bl	FtlBbmTblFlush
.L1490:
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
	ldrh	w4, [x3,780]
	udiv	w2, w0, w4
	msub	w0, w2, w4, w0
	madd	w0, w1, w4, w0
	ldrh	w1, [x3,822]
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
	ldrh	w1, [x2,780]
	ldrh	w3, [x2,822]
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
	ldrh	w3, [x2,822]
	udiv	w1, w0, w3
	msub	w0, w1, w3, w0
	ldrh	w1, [x2,780]
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
	bls	.L1515
	sub	w1, w0, w1
	cmp	w1, w2
	cset	w0, ls
	b	.L1516
.L1515:
	sub	w1, w1, w0
	cmp	w1, w2
	cset	w0, hi
.L1516:
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
	bne	.L1518
	ldrh	w19, [x3,826]
	b	.L1519
.L1518:
	ldrh	w19, [x3,824]
.L1519:
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
	bne	.L1520
	mov	w22, 0
	mov	w25, 2
.L1521:
	cmp	w22, w19
	bgt	.L1520
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
	bne	.L1522
	ldr	w0, [x24,4]
	cmn	w0, #1
	bne	.L1522
	ldr	w0, [x29,88]
	cmn	w0, #1
	beq	.L1522
	sub	w19, w20, #1
	sxth	w19, w19
	b	.L1521
.L1522:
	add	w20, w20, 1
	sxth	w22, w20
	b	.L1521
.L1520:
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
	add	x21, x23, 1416
	ldr	x0, [x23,1056]
	ldr	x22, [x23,1120]
	str	x0, [x21,8]
	str	x22, [x21,16]
	bl	FtlBbtMemInit
	ldrh	w20, [x23,822]
	sub	w20, w20, #1
	uxth	w20, w20
.L1529:
	ldrh	w0, [x23,822]
	sub	w0, w0, #48
	cmp	w20, w0
	ble	.L1532
	lsl	w0, w20, 10
	mov	w1, 1
	str	w0, [x21,4]
	mov	w2, w1
	mov	x0, x21
	bl	FlashReadPages
	ldr	w0, [x21]
	cmn	w0, #1
	bne	.L1530
	ldr	w0, [x21,4]
	mov	w1, 1
	mov	w2, w1
	add	w0, w0, 1
	str	w0, [x21,4]
	mov	x0, x21
	bl	FlashReadPages
.L1530:
	ldr	w0, [x21]
	cmn	w0, #1
	beq	.L1531
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1531
	ldr	w1, [x22,4]
	str	w1, [x23,1312]
	strh	w20, [x23,1304]
	ldrh	w1, [x22,8]
	strh	w1, [x23,1308]
	b	.L1532
.L1531:
	sub	w20, w20, #1
	uxth	w20, w20
	b	.L1529
.L1532:
	add	x21, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	mov	w0, -1
	ldrh	w1, [x21,1304]
	cmp	w1, w2
	beq	.L1534
	ldrh	w1, [x21,1308]
	cmp	w1, w2
	beq	.L1536
	add	x0, x21, 1416
	lsl	w1, w1, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	ldr	w0, [x21,1416]
	cmn	w0, #1
	beq	.L1536
	ldrh	w1, [x22]
	mov	w0, 61649
	cmp	w1, w0
	bne	.L1536
	ldr	w0, [x22,4]
	ldr	w1, [x21,1312]
	cmp	w0, w1
	bls	.L1536
	str	w0, [x21,1312]
	ldrh	w1, [x21,1308]
	ldrh	w0, [x22,8]
	strh	w1, [x21,1304]
	strh	w0, [x21,1308]
.L1536:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 1
	add	x23, x20, 1416
	mov	w24, 61649
	ldrh	w0, [x20,1304]
	bl	FtlGetLastWrittenPage
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x20,1306]
.L1538:
	tbnz	w21, #31, .L1543
	ldrh	w0, [x20,1304]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w21, w0, lsl 10
	str	w0, [x23,4]
	ldr	x0, [x20,1056]
	str	x0, [x23,8]
	mov	x0, x23
	bl	FlashReadPages
	ldr	w0, [x23]
	cmn	w0, #1
	beq	.L1539
	ldrh	w0, [x22]
	cmp	w0, w24
	bne	.L1539
.L1543:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,10]
	mov	w2, 65535
	strh	w0, [x1,1310]
	ldrh	w0, [x22,12]
	cmp	w0, w2
	bne	.L1540
	b	.L1541
.L1539:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L1538
.L1540:
	ldr	w2, [x1,752]
	cmp	w0, w2
	beq	.L1541
	ldrh	w1, [x1,766]
	lsr	w1, w1, 2
	cmp	w0, w1
	bcs	.L1541
	cmp	w2, w1
	bcs	.L1541
	bl	FtlSysBlkNumInit
.L1541:
	add	x19, x19, :lo12:.LANCHOR2
	mov	x20, 0
	add	x21, x19, 1304
.L1544:
	ldrh	w0, [x19,778]
	mov	w1, w20
	add	x20, x20, 1
	cmp	w1, w0
	bcs	.L1562
	ldrh	w2, [x19,1296]
	add	x0, x21, x20, lsl 3
	ldr	x3, [x19,1424]
	mul	w1, w1, w2
	ldr	x0, [x0,24]
	lsl	w2, w2, 2
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	b	.L1544
.L1562:
	mov	w0, 0
.L1534:
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
	strh	w0, [x1,1472]
	add	x0, x1, 1480
	strh	wzr, [x1,1474]
	strh	wzr, [x1,1476]
	strh	wzr, [x1,1478]
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
	adrp	x0, .LANCHOR2+1478
	ldrh	w0, [x0,#:lo12:.LANCHOR2+1478]
	cmp	w0, wzr
	cset	w0, eq
	ret
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	adrp	x0, .LANCHOR2+1478
	ldrh	w0, [x0,#:lo12:.LANCHOR2+1478]
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
	bcc	.L1566
	adrp	x2, .LANCHOR2
	add	x20, x2, :lo12:.LANCHOR2
	mov	x19, x2
	ldrh	w0, [x20,1478]
	cmp	w0, 1024
	beq	.L1566
	cbz	w1, .L1568
	ldr	w0, [x20,1408]
	cbnz	w0, .L1568
	mov	w0, w21
	bl	P2V_block_in_plane
	uxth	w22, w0
	ldr	x0, [x20,1024]
	lsl	w1, w21, 10
	str	w1, [x0,4]
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x20,1024]
	bl	FlashEraseBlocks
	ubfiz	x0, x22, 1, 16
	ldr	x2, [x20,1176]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x20,916]
	add	w0, w0, 1
	str	w0, [x20,916]
.L1568:
	add	x1, x19, :lo12:.LANCHOR2
	add	x1, x1, 1472
	ldrh	w0, [x1,6]
	ldrh	w2, [x1,4]
	add	w0, w0, 1
	strh	w0, [x1,6]
	add	x0, x1, x2, sxtw 1
	add	w2, w2, 1
	and	w2, w2, 1023
	strh	w21, [x0,8]
	strh	w2, [x1,4]
.L1566:
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
	ldrb	w0, [x3,656]
	ldr	w2, [x3,1408]
	str	w0, [x29,120]
	mov	w0, 0
	cbnz	w2, .L1576
	adrp	x28, .LANCHOR0
	str	w25, [x3,948]
	add	x0, x28, :lo12:.LANCHOR0
	mov	w27, w2
	mov	w20, w2
	mov	w19, w2
	mov	x26, x3
	mov	w23, 56
	ldrb	w0, [x0,4044]
	str	w0, [x29,124]
.L1577:
	ldrh	w0, [x26,756]
	cmp	w0, w27
	bls	.L1621
	umull	x0, w27, w23
	ldr	x1, [x26,1024]
	str	wzr, [x1,x0]
	add	x0, x26, 784
	mov	w1, w25
	ldrb	w0, [x0,w27,sxtw]
	bl	V2P_block
	uxth	w22, w0
	mov	w1, w22
	cbz	w24, .L1578
	str	x1, [x29,112]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,112]
	cbnz	w0, .L1579
.L1578:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1580
	umull	x0, w20, w23
	ldr	x1, [x26,1024]
	lsl	w22, w22, 10
	mov	w2, 4
	add	x1, x1, x0
	str	w22, [x1,4]
	ldr	x1, [x26,1024]
	ldr	x4, [x26,1128]
	add	x1, x1, x0
	ldrh	w0, [x26,836]
	mul	w0, w20, w0
	add	w20, w20, 1
	str	xzr, [x1,8]
	sdiv	w0, w0, w2
	uxth	w20, w20
	add	x0, x4, x0, sxtw 2
	str	x0, [x1,16]
	b	.L1579
.L1580:
	add	w19, w19, 1
	uxth	w19, w19
.L1579:
	add	w2, w27, 1
	uxth	w27, w2
	b	.L1577
.L1621:
	cbz	w20, .L1600
	ldr	w0, [x29,124]
	mov	w2, w20
	strb	wzr, [x26,656]
	mov	x23, 0
	cmp	w0, wzr
	ldr	x0, [x26,1024]
	cset	w22, ne
	mov	w1, w22
	bl	FlashEraseBlocks
	ldrb	w0, [x29,120]
	strb	w0, [x26,656]
	mov	x26, 56
.L1583:
	cmp	w20, w23, uxth
	bls	.L1622
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1024]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cmn	w0, #1
	bne	.L1584
	ldr	w0, [x2,4]
	add	w19, w19, 1
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
.L1584:
	add	x23, x23, 1
	b	.L1583
.L1622:
	cbnz	w24, .L1586
	mov	w0, 1
	uxth	w22, w22
	mov	w27, 6
	str	w0, [x29,124]
	b	.L1587
.L1586:
	add	x0, x21, :lo12:.LANCHOR2
	add	x28, x28, :lo12:.LANCHOR0
	ldrh	w0, [x0,826]
	str	w0, [x29,124]
	ldrb	w0, [x28,4044]
	cbnz	w0, .L1603
	ldr	w0, [x29,124]
	mov	w22, 1
	lsr	w27, w0, 2
	b	.L1587
.L1603:
	mov	w22, 1
	mov	w27, w22
.L1587:
	mov	w23, 0
	add	x28, x21, :lo12:.LANCHOR2
.L1596:
	mov	w26, 0
	mov	w20, w26
.L1588:
	ldrh	w0, [x28,756]
	cmp	w0, w26
	bls	.L1623
	mov	w0, 56
	ldr	x1, [x28,1024]
	umull	x0, w26, w0
	str	wzr, [x1,x0]
	add	x0, x28, 784
	mov	w1, w25
	ldrb	w0, [x0,w26,sxtw]
	bl	V2P_block
	uxth	w2, w0
	str	w2, [x29,112]
	mov	w1, w2
	cbz	w24, .L1589
	str	x1, [x29,104]
	bl	IsBlkInVendorPart
	ldr	x1, [x29,104]
	cbnz	w0, .L1590
.L1589:
	mov	w0, w1
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L1590
	mov	w0, 56
	ldr	x1, [x28,1024]
	ldr	w2, [x29,112]
	umull	x0, w20, w0
	add	x1, x1, x0
	add	w2, w23, w2, lsl 10
	str	w2, [x1,4]
	mov	w2, 4
	ldr	x1, [x28,1024]
	add	x1, x1, x0
	ldr	x0, [x28,1096]
	str	x0, [x1,8]
	ldrh	w0, [x28,836]
	mul	w0, w20, w0
	add	w20, w20, 1
	sdiv	w0, w0, w2
	uxth	w20, w20
	ldr	x2, [x28,1104]
	add	x0, x2, x0, sxtw 2
	str	x0, [x1,16]
.L1590:
	add	w3, w26, 1
	uxth	w26, w3
	b	.L1588
.L1623:
	cbz	w20, .L1600
	ldr	x0, [x28,1024]
	mov	w1, w20
	mov	w2, w22
	mov	w3, 1
	strb	wzr, [x28,656]
	bl	FlashProgPages
	ldrb	w0, [x29,120]
	mov	x1, 0
	strb	w0, [x28,656]
.L1593:
	cmp	w20, w1, uxth
	bls	.L1624
	mov	x0, 56
	ldr	x2, [x28,1024]
	mul	x0, x1, x0
	add	x3, x2, x0
	ldr	w0, [x2,x0]
	cbz	w0, .L1594
	ldr	w0, [x3,4]
	add	w19, w19, 1
	str	x1, [x29,112]
	lsr	w0, w0, 10
	uxth	w19, w19
	bl	FtlBbmMapBadBlock
	ldr	x1, [x29,112]
.L1594:
	add	x1, x1, 1
	b	.L1593
.L1624:
	add	w23, w23, w27
	ldr	w0, [x29,124]
	uxth	w23, w23
	cmp	w23, w0
	bcc	.L1596
	mov	x23, 0
	mov	x26, 56
.L1597:
	cmp	w20, w23, uxth
	bls	.L1625
	cbz	w24, .L1598
	add	x1, x21, :lo12:.LANCHOR2
	mul	x0, x23, x26
	ldr	x1, [x1,1024]
	add	x2, x1, x0
	ldr	w0, [x1,x0]
	cbnz	w0, .L1598
	ldr	w0, [x2,4]
	mov	w1, 1
	lsr	w0, w0, 10
	bl	FtlFreeSysBlkQueueIn
.L1598:
	add	x23, x23, 1
	b	.L1597
.L1625:
	cmp	w25, 63
	bls	.L1604
	cbz	w24, .L1600
.L1604:
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, w20
	ldr	x0, [x21,1024]
	bl	FlashEraseBlocks
.L1600:
	mov	w0, w19
.L1576:
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
	add	x0, x0, 1472
	ldrh	w6, [x0,2084]
	ldrh	w2, [x0,6]
	cbz	w2, .L1626
	mov	w5, 0
	ldrh	w3, [x0,2]
	ldrh	w2, [x0,4]
	mov	w4, w5
	and	w6, w6, 31
.L1628:
	cmp	w4, w6
	bge	.L1636
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
	b	.L1628
.L1636:
	cbz	w5, .L1626
	add	x0, x1, :lo12:.LANCHOR2
	strh	w3, [x0,1474]
	strh	w2, [x0,1476]
.L1626:
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
	add	x21, x19, 1472
	adrp	x24, .LANCHOR0
	add	x23, x23, :lo12:.LC35
.L1638:
	ldrh	w1, [x21,6]
	adrp	x22, .LANCHOR2
	cbz	w1, .L1639
	ldrh	w2, [x21,2]
	sub	w1, w1, #1
	add	x0, x21, x2, sxtw 1
	strh	w1, [x21,6]
	add	w2, w2, 1
	ldr	w1, [x19,1408]
	and	w2, w2, 1023
	strh	w2, [x21,2]
	ldrh	w20, [x0,8]
	cbnz	w1, .L1640
	mov	w0, w20
	str	x1, [x29,88]
	bl	P2V_block_in_plane
	uxth	w26, w0
	ldr	x0, [x19,1024]
	lsl	w2, w20, 10
	ldr	x1, [x29,88]
	str	w2, [x0,4]
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L1641
	ldr	x0, [x19,1024]
	mov	w2, 1
	bl	FlashEraseBlocks
.L1641:
	add	x25, x22, :lo12:.LANCHOR2
	mov	w1, 1
	mov	w2, w1
	ldr	x0, [x25,1024]
	bl	FlashEraseBlocks
	ubfiz	x0, x26, 1, 16
	ldr	x2, [x25,1176]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
	ldr	w0, [x25,916]
	add	w0, w0, 1
	str	w0, [x25,916]
	b	.L1640
.L1639:
	adrp	x0, .LC34
	add	x0, x0, :lo12:.LC34
	bl	printk
.L1642:
	b	.L1642
.L1640:
	sub	w0, w20, #1
	mov	w1, 65533
	cmp	w1, w0, uxth
	bcs	.L1643
	add	x22, x22, :lo12:.LANCHOR2
	mov	x0, x23
	mov	w1, w20
	ldrh	w2, [x22,1478]
	bl	printk
	b	.L1638
.L1643:
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
	adrp	x0, .LANCHOR2+1288
	mov	x4, -6148914691236517206
	uxth	w1, w1
	mov	w5, 65535
	ldr	x3, [x0,#:lo12:.LANCHOR2+1288]
	sub	x0, x2, x3
	asr	x0, x0, 1
	madd	x0, x4, x0, x0
	mov	w4, 6
	uxth	w0, w0
.L1649:
	cmp	w1, w0
	beq	.L1650
	ldrh	w0, [x2]
	cmp	w0, w5
	beq	.L1651
	umull	x2, w0, w4
	add	x2, x3, x2
	b	.L1649
.L1650:
	mov	w0, 1
	b	.L1648
.L1651:
	mov	w0, 0
.L1648:
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
	ldrh	w1, [x3,764]
	cmp	w1, w0
	bls	.L1654
	mov	w7, 6
	ldr	x11, [x3,1288]
	mov	w1, -1
	umull	x10, w0, w7
	add	x5, x11, x10
	strh	w1, [x5,2]
	strh	w1, [x11,x10]
	ldr	x1, [x3,3576]
	cbnz	x1, .L1655
	str	x5, [x3,3576]
	b	.L1654
.L1655:
	ubfiz	x8, x0, 1, 16
	ldr	x13, [x3,1192]
	ldrh	w2, [x5,4]
	mov	x9, -6148914691236517206
	ldr	x12, [x3,1288]
	mov	w18, 65535
	ldrh	w6, [x13,x8]
	cmp	w2, wzr
	ldr	x14, [x3,1176]
	ldrh	w17, [x3,764]
	mov	w3, 0
	mul	w6, w6, w2
	sub	x2, x1, x12
	asr	x2, x2, 1
	csinv	w6, w6, wzr, ne
	madd	x2, x9, x2, x2
	add	x9, x14, x8
	mov	w8, w7
	uxth	w2, w2
.L1663:
	add	w3, w3, 1
	cmp	w0, w2
	uxth	w3, w3
	beq	.L1654
	cmp	w3, w17
	bhi	.L1654
	ubfiz	x15, x2, 1, 16
	ldrh	w16, [x1,4]
	cmp	w16, wzr
	ldrh	w7, [x13,x15]
	mul	w7, w7, w16
	csinv	w7, w7, wzr, ne
	cmp	w7, w6
	bne	.L1659
	ldrh	w15, [x14,x15]
	ldrh	w7, [x9]
	cmp	w15, w7
	bcc	.L1661
	b	.L1660
.L1659:
	bhi	.L1660
.L1661:
	ldrh	w7, [x1]
	cmp	w7, w18
	bne	.L1662
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	strh	w0, [x1]
	str	x5, [x2,3584]
	b	.L1654
.L1662:
	umull	x1, w7, w8
	mov	w2, w7
	add	x1, x12, x1
	b	.L1663
.L1660:
	strh	w2, [x11,x10]
	ldrh	w2, [x1,2]
	strh	w2, [x5,2]
	add	x2, x4, :lo12:.LANCHOR2
	ldr	x3, [x2,3576]
	cmp	x1, x3
	bne	.L1664
	strh	w0, [x1,2]
	str	x5, [x2,3576]
	b	.L1654
.L1664:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1288]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1654:
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
	ldrh	w1, [x0,3592]
	add	w1, w1, 1
	strh	w1, [x0,3592]
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
	beq	.L1669
	adrp	x2, .LANCHOR2
	mov	w6, 6
	add	x5, x2, :lo12:.LANCHOR2
	mov	w1, -1
	umull	x8, w0, w6
	mov	x3, x2
	ldr	x9, [x5,1288]
	add	x4, x9, x8
	strh	w1, [x4,2]
	strh	w1, [x9,x8]
	ldr	x1, [x5,3600]
	cbnz	x1, .L1670
	str	x4, [x5,3600]
	b	.L1669
.L1670:
	ldr	x11, [x5,1176]
	ubfiz	x2, x0, 1, 16
	ldr	x10, [x5,1288]
	mov	x5, -6148914691236517206
	ldrh	w12, [x11,x2]
	sub	x2, x1, x10
	asr	x2, x2, 1
	madd	x2, x5, x2, x2
	uxth	w2, w2
.L1673:
	ubfiz	x5, x2, 1, 16
	ldrh	w5, [x11,x5]
	cmp	w5, w12
	bcs	.L1671
	ldrh	w5, [x1]
	cmp	w5, w7
	bne	.L1672
	strh	w2, [x4,2]
	strh	w0, [x1]
	b	.L1669
.L1672:
	umull	x1, w5, w6
	mov	w2, w5
	add	x1, x10, x1
	b	.L1673
.L1671:
	ldrh	w5, [x1,2]
	strh	w5, [x4,2]
	strh	w2, [x9,x8]
	add	x2, x3, :lo12:.LANCHOR2
	ldr	x3, [x2,3600]
	cmp	x1, x3
	bne	.L1674
	strh	w0, [x1,2]
	str	x4, [x2,3600]
	b	.L1669
.L1674:
	ldrh	w3, [x1,2]
	mov	w4, 6
	ldr	x2, [x2,1288]
	umull	x3, w3, w4
	strh	w0, [x2,x3]
	strh	w0, [x1,2]
.L1669:
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
	ldrh	w1, [x0,3608]
	add	w1, w1, 1
	strh	w1, [x0,3608]
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
	ldr	x2, [x6,1288]
	add	x4, x2, x1
	cmp	x4, x3
	bne	.L1677
	ldrh	w3, [x2,x1]
	cmp	w3, w7
	bne	.L1678
	str	xzr, [x0]
	b	.L1679
.L1678:
	umull	x3, w3, w5
	add	x3, x2, x3
	str	x3, [x0]
	mov	w0, -1
	strh	w0, [x3,2]
	b	.L1679
.L1677:
	ldrh	w0, [x2,x1]
	ldrh	w3, [x4,2]
	cmp	w0, w7
	bne	.L1680
	cmp	w3, w0
	beq	.L1679
	umull	x3, w3, w5
	mov	w0, -1
	strh	w0, [x2,x3]
	b	.L1679
.L1680:
	umull	x0, w0, w5
	add	x0, x2, x0
	strh	w3, [x0,2]
	ldrh	w3, [x4,2]
	ldr	x0, [x6,1288]
	ldrh	w7, [x2,x1]
	umull	x3, w3, w5
	strh	w7, [x0,x3]
.L1679:
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
	cbz	x2, .L1682
	adrp	x3, .LANCHOR2+1288
	mov	w5, w19
	mov	w6, 6
	ldr	x4, [x3,#:lo12:.LANCHOR2+1288]
.L1683:
	cbnz	w1, .L1684
.L1686:
	sub	x2, x2, x4
	mov	x1, -6148914691236517206
	asr	x2, x2, 1
	madd	x2, x1, x2, x2
	uxth	w19, w2
	mov	w1, w19
	bl	List_remove_node
	b	.L1682
.L1684:
	ldrh	w3, [x2]
	cmp	w3, w5
	beq	.L1686
	umull	x3, w3, w6
	sub	w1, w1, #1
	add	x2, x4, x3
	uxth	w1, w1
	b	.L1683
.L1682:
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
	ldr	x1, [x2,3576]
	cbz	x1, .L1694
	ldr	x4, [x2,1288]
	mov	w5, 6
.L1690:
	cbz	w3, .L1691
	ldrh	w2, [x1]
	cmp	w2, w0
	beq	.L1694
	umull	x2, w2, w5
	sub	w3, w3, #1
	add	x1, x4, x2
	uxth	w3, w3
	b	.L1690
.L1691:
	sub	x1, x1, x4
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	uxth	w0, w1
.L1694:
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
	ldrh	w1, [x0,3616]
	cmp	w1, w20
	beq	.L1696
	ldrh	w1, [x0,3664]
	cmp	w1, w20
	beq	.L1696
	ldrh	w1, [x0,3712]
	cmp	w1, w20
	beq	.L1696
	mov	w3, 6
	ldr	x4, [x0,1288]
	ldr	x2, [x0,3576]
	umull	x3, w20, w3
	add	x1, x4, x3
	cmp	x1, x2
	beq	.L1696
	ldr	x5, [x0,1192]
	ubfiz	x0, x20, 1, 16
	ldrh	w2, [x5,x0]
	ldrh	w0, [x1,4]
	ldrh	w1, [x1,2]
	cmp	w0, wzr
	mul	w2, w2, w0
	mov	w0, 65535
	csinv	w2, w2, wzr, ne
	cmp	w1, w0
	bne	.L1698
	ldrh	w0, [x4,x3]
	cmp	w0, w1
	beq	.L1696
.L1698:
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
	bcs	.L1696
	add	x19, x19, :lo12:.LANCHOR2
	mov	w1, w20
	add	x0, x19, 3576
	bl	List_remove_node
	ldrh	w0, [x19,3592]
	sub	w0, w0, #1
	strh	w0, [x19,3592]
	mov	w0, w20
	bl	INSERT_DATA_LIST
.L1696:
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
.L1703:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1725
	ubfiz	x1, x0, 2, 16
	ldr	w2, [x20,x1]
	mov	w1, 0
	ubfx	x2, x2, 10, 16
.L1704:
	ldrh	w3, [x19,10]
	cmp	w3, w1
	bls	.L1726
	ubfiz	x3, x1, 1, 16
	ldrh	w4, [x22,x3]
	cmp	w2, w4
	bne	.L1705
	cbz	w4, .L1705
	ldrh	w4, [x21,x3]
	add	w4, w4, 1
	strh	w4, [x21,x3]
.L1705:
	add	w1, w1, 1
	uxth	w1, w1
	b	.L1704
.L1726:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1703
.L1725:
	adrp	x0, .LANCHOR2+826
	mov	w23, 0
	mov	w20, w23
	ldrh	w1, [x0,#:lo12:.LANCHOR2+826]
	ldrh	w0, [x19]
	strh	w1, [x21,x0,lsl 1]
	ldrh	w24, [x21]
.L1708:
	ldrh	w0, [x19,10]
	cmp	w0, w20
	bls	.L1727
	ubfiz	x2, x20, 1, 16
	ldrh	w1, [x21,x2]
	cmp	w24, w1
	bls	.L1709
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbnz	w0, .L1710
	b	.L1711
.L1709:
	cbnz	w1, .L1711
	ldrh	w0, [x22,x2]
	add	x25, x22, x2
	cbz	w0, .L1711
.L1713:
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	strh	wzr, [x25]
	ldrh	w0, [x19,8]
	sub	w0, w0, #1
	strh	w0, [x19,8]
.L1711:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1708
.L1710:
	mov	w23, w20
	mov	w24, w1
	cbz	w1, .L1713
	b	.L1711
.L1727:
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
.L1729:
	cmp	w20, w2
	beq	.L1733
	mov	x22, x1
	ldrh	w21, [x1],2
	cbnz	w21, .L1730
	mov	x19, x0
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x22]
	uxth	w1, w0
	sub	w2, w1, #1
	mov	w0, 65533
	cmp	w0, w2, uxth
	bcs	.L1731
	adrp	x2, .LANCHOR2+1478
	adrp	x0, .LC36
	add	x0, x0, :lo12:.LC36
	ldrh	w2, [x2,#:lo12:.LANCHOR2+1478]
	bl	printk
.L1732:
	b	.L1732
.L1731:
	ldr	w0, [x19,48]
	strh	w21, [x19,2]
	add	w0, w0, 1
	str	w0, [x19,48]
	ldrh	w0, [x19,8]
	strh	w20, [x19]
	add	w0, w0, 1
	strh	w0, [x19,8]
	b	.L1733
.L1730:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L1729
.L1733:
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
.L1736:
	add	x0, x26, :lo12:.LANCHOR2
	adrp	x20, .LANCHOR2
	ldr	w1, [x0,900]
	add	w1, w1, 1
	str	w1, [x0,900]
	ldrh	w0, [x0,826]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	cmp	w1, w0
	bge	.L1737
	ldrh	w0, [x19]
	cmp	w0, w28
	bne	.L1738
.L1737:
	mov	x0, x19
	bl	Ftl_write_map_blk_to_last_page
.L1738:
	ldr	w1, [x21,1408]
	cbnz	w1, .L1739
	ldrh	w2, [x19]
	add	x3, x21, 1416
	ldr	x0, [x19,16]
	str	x3, [x29,104]
	ldrh	w24, [x0,x2,lsl 1]
	mov	w2, 16
	ldrh	w0, [x19,2]
	str	x27, [x21,1424]
	orr	w0, w0, w24, lsl 10
	str	w0, [x21,1420]
	ldr	x0, [x21,1120]
	str	x0, [x21,1432]
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
	cbz	w0, .L1740
	ldr	x0, [x3,8]
	ldrh	w1, [x21,834]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	str	w0, [x2,12]
.L1740:
	add	x4, x21, 1416
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
	ldr	w2, [x21,1416]
	cmn	w2, #1
	bne	.L1741
	ldr	w1, [x4,4]
	mov	x0, x23
	add	w22, w22, 1
	bl	printk
	uxth	w22, w22
	ldrh	w0, [x19,2]
	cmp	w0, 2
	bhi	.L1742
	ldrh	w0, [x21,826]
	sub	w0, w0, #1
	strh	w0, [x19,2]
.L1742:
	cmp	w22, 3
	bls	.L1736
	add	x20, x20, :lo12:.LANCHOR2
	adrp	x0, .LC38
	add	x0, x0, :lo12:.LC38
	mov	w2, w22
	ldr	w1, [x20,1420]
	bl	printk
	mov	w0, 1
	str	w0, [x20,1408]
	b	.L1739
.L1741:
	cbz	w2, .L1744
	strh	w24, [x19,60]
.L1744:
	cmp	w1, 1
	beq	.L1745
	cmp	w2, 256
	beq	.L1745
	ldr	w0, [x19,56]
	cbz	w0, .L1746
.L1745:
	str	wzr, [x19,56]
	b	.L1736
.L1746:
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x19,40]
	ldr	w1, [x20,1420]
	str	w1, [x0,w25,uxtw 2]
.L1739:
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
	blt	.L1754
	ubfiz	x1, x0, 1, 16
	ldrh	w24, [x21,x1]
	cbz	w24, .L1754
	ldr	w0, [x19,52]
	cbnz	w0, .L1754
	mov	w2, 1
	str	w2, [x19,52]
	strh	w0, [x21,x1]
	ldrh	w0, [x19,8]
	ldrh	w1, [x19,2]
	sub	w0, w0, #1
	strh	w0, [x19,8]
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,826]
	cmp	w1, w0
	bcc	.L1755
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1755:
	add	x26, x20, :lo12:.LANCHOR2
	mov	w21, 0
	add	x22, x26, 1416
.L1756:
	ldrh	w0, [x19,6]
	cmp	w0, w21
	bls	.L1765
	ubfiz	x0, x21, 2, 16
	add	x27, x25, x0
	ldr	w1, [x25,x0]
	cmp	w24, w1, lsr 10
	bne	.L1757
	ldr	x1, [x26,1064]
	ldr	x23, [x26,1120]
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
	bne	.L1758
.L1760:
	add	x20, x20, :lo12:.LANCHOR2
	str	wzr, [x27]
	adrp	x0, .LC39
	ldrh	w2, [x23,8]
	add	x0, x0, :lo12:.LC39
	ldr	w1, [x20,1420]
	bl	printk
	mov	w0, 1
	str	w0, [x20,1408]
	b	.L1759
.L1758:
	ldrh	w0, [x23,8]
	cmp	w0, w21
	bne	.L1760
	ldrh	w1, [x23]
	ldrh	w0, [x19,4]
	cmp	w1, w0
	bne	.L1760
	ldr	x2, [x22,8]
	mov	x0, x19
	mov	w1, w21
	bl	FtlMapWritePage
.L1757:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1756
.L1765:
	mov	w0, w24
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
	str	wzr, [x19,52]
.L1754:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x20,826]
	cmp	w1, w0
	bcc	.L1759
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.L1759:
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
	ldr	w20, [x1,1408]
	ldr	x22, [x0,16]
	ldr	x24, [x0,40]
	cbnz	w20, .L1767
	mov	x19, x0
	ldrh	w0, [x0]
	mov	w2, 65535
	cmp	w0, w2
	bne	.L1768
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
	b	.L1767
.L1768:
	ubfiz	x0, x0, 1, 16
	ldrh	w2, [x19,2]
	ldrh	w23, [x22,x0]
	ldr	x22, [x1,1120]
	str	x22, [x1,1432]
	orr	w2, w2, w23, lsl 10
	str	w2, [x1,1420]
	ldr	x2, [x1,1056]
	str	x2, [x1,1424]
	ldr	w0, [x19,48]
	str	w0, [x22,4]
	mov	w0, -1291
	strh	w0, [x22,8]
	ldrh	w0, [x19,4]
	strh	w0, [x22]
	strh	w23, [x22,2]
	ldrh	w2, [x1,826]
	ldr	x0, [x1,1056]
	mov	w1, 255
	lsl	w2, w2, 3
	bl	ftl_memset
	mov	w0, w20
.L1769:
	ldrh	w1, [x19,6]
	cmp	w1, w0
	bls	.L1776
	ubfiz	x3, x0, 2, 32
	ldr	w1, [x24,x3]
	cmp	w23, w1, lsr 10
	bne	.L1770
	add	x2, x21, :lo12:.LANCHOR2
	add	w20, w20, 1
	uxth	w20, w20
	ldr	x4, [x2,1056]
	ubfiz	x1, x20, 3, 16
	str	w0, [x4,x1]
	ldr	x2, [x2,1056]
	ldr	w3, [x24,x3]
	add	x1, x2, x1
	str	w3, [x1,4]
.L1770:
	add	w0, w0, 1
	uxth	w0, w0
	b	.L1769
.L1776:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L1772
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,1424]
	ldrh	w1, [x1,834]
	bl	js_hash
	str	w0, [x22,12]
.L1772:
	mov	w1, 1
	add	x0, x21, :lo12:.LANCHOR2
	add	x0, x0, 1416
	mov	w2, w1
	mov	w3, 0
	bl	FlashProgPages
	ldrh	w0, [x19,2]
	add	w0, w0, 1
	strh	w0, [x19,2]
	mov	x0, x19
	bl	ftl_map_blk_gc
.L1767:
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
	add	x0, x20, 3760
	ldr	x1, [x20,1272]
	add	x2, x1, x19
	ldrh	w1, [x1,x19]
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
	ldr	x0, [x20,1272]
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
	ldrh	w2, [x1,862]
	ldr	x1, [x1,1272]
.L1779:
	uxth	w0, w4
	cmp	w0, w2
	bcs	.L1795
	add	x4, x4, 1
	add	x6, x1, x4, lsl 4
	ldrh	w6, [x6,-16]
	cmp	w6, w5
	bne	.L1779
	b	.L1780
.L1795:
	mov	w0, w2
	mov	x4, 0
	mov	w7, -2147483648
.L1782:
	uxth	w6, w4
	cmp	w6, w2
	bcs	.L1796
	add	x5, x1, x4, lsl 4
	ldr	w5, [x5,4]
	tbnz	w5, #31, .L1791
	cmp	w5, w7
	bcc	.L1783
.L1791:
	mov	w5, w7
	mov	w6, w0
.L1783:
	add	x4, x4, 1
	mov	w7, w5
	mov	w0, w6
	b	.L1782
.L1796:
	cmp	w0, w2
	bcc	.L1780
	add	x0, x3, :lo12:.LANCHOR2
	add	x1, x1, 4
	mov	w4, -1
	mov	w3, 0
	ldrh	w5, [x0,3824]
	mov	w0, w2
.L1786:
	cmp	w3, w2
	beq	.L1780
	ldr	w7, [x1]
	cmp	w7, w4
	bcs	.L1787
	ldrh	w6, [x1,-4]
	cmp	w6, w5
	csel	w4, w4, w7, eq
	cmp	w6, w5
	csel	w0, w0, w3, eq
.L1787:
	add	w3, w3, 1
	add	x1, x1, 16
	uxth	w3, w3
	b	.L1786
.L1780:
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
	ldr	w5, [x4,1404]
	ldrh	w3, [x4,832]
	cmp	w0, w5
	bcs	.L1798
	add	w27, w3, 7
	str	x1, [x29,104]
	lsr	w28, w0, w27
	mov	w25, w0
	str	w2, [x29,100]
	mov	x0, 0
	uxth	w22, w28
	ldrh	w1, [x4,862]
	ldr	x19, [x4,1272]
	b	.L1799
.L1798:
	mov	w0, -1
	cbnz	w2, .L1800
	str	w0, [x1]
	b	.L1800
.L1804:
	add	x0, x0, 1
	add	x2, x19, x0, lsl 4
	ldrh	w2, [x2,-16]
	cmp	w2, w22
	beq	.L1801
.L1799:
	uxth	x20, w0
	cmp	w20, w1
	bcc	.L1804
	bl	select_l2p_ram_region
	uxth	x20, w0
	ubfiz	x26, x20, 4, 16
	mov	w2, 65535
	add	x1, x19, x26
	ldrh	w3, [x19,x26]
	cmp	w3, w2
	bne	.L1812
.L1805:
	add	x19, x21, :lo12:.LANCHOR2
	ubfiz	x0, x22, 2, 16
	ldr	x1, [x19,1248]
	ldr	w24, [x1,x0]
	cbnz	w24, .L1806
	ldr	x0, [x19,1272]
	mov	w1, 255
	ldrh	w2, [x19,834]
	add	x0, x0, x26
	ldr	x0, [x0,8]
	bl	ftl_memset
	ldr	x0, [x19,1272]
	strh	w22, [x0,x26]
	ldr	x0, [x19,1272]
	add	x26, x0, x26
	str	w24, [x26,4]
.L1801:
	mov	x1, 1
	lsl	x3, x1, x27
	ldr	w1, [x29,100]
	sub	w3, w3, #1
	and	w0, w25, w3
	uxth	x0, w0
	cbnz	w1, .L1802
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x1, [x1,1272]
	add	x1, x1, x20, lsl 4
	ldr	x1, [x1,8]
	ldr	w0, [x1,x0,lsl 2]
	ldr	x1, [x29,104]
	str	w0, [x1]
	b	.L1803
.L1802:
	ldr	x2, [x29,104]
	lsl	x1, x20, 4
	ldr	w4, [x2]
	add	x2, x21, :lo12:.LANCHOR2
	ldr	x3, [x2,1272]
	add	x3, x3, x1
	ldr	x3, [x3,8]
	str	w4, [x3,x0,lsl 2]
	strh	w22, [x2,3824]
	ldr	x0, [x2,1272]
	add	x0, x0, x1
	ldr	w1, [x0,4]
	orr	w1, w1, -2147483648
	str	w1, [x0,4]
.L1803:
	add	x21, x21, :lo12:.LANCHOR2
	ldr	x0, [x21,1272]
	add	x20, x0, x20, lsl 4
	mov	w0, 0
	ldr	w1, [x20,4]
	cmn	w1, #1
	beq	.L1800
	add	w1, w1, 1
	str	w1, [x20,4]
	b	.L1800
.L1812:
	ldr	w1, [x1,4]
	tbz	w1, #31, .L1805
	bl	flush_l2p_region
	b	.L1805
.L1806:
	ldr	x0, [x19,1272]
	mov	w1, 1
	add	x23, x19, 1416
	mov	w2, w1
	add	x0, x0, x26
	str	w24, [x19,1420]
	ldr	x0, [x0,8]
	str	x0, [x19,1424]
	ldr	x0, [x19,1120]
	str	x0, [x19,1432]
	mov	x0, x23
	bl	FlashReadPages
	ldr	x0, [x19,1432]
	ldrh	w0, [x0,8]
	cmp	w0, w22
	beq	.L1807
	adrp	x0, .LC40
	uxth	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC40
	bl	printk
	mov	w2, 4
	ldr	x1, [x19,1432]
	adrp	x0, .LC17
	mov	w3, w2
	add	x0, x0, :lo12:.LC17
	bl	rknand_print_hex
	adrp	x0, .LC41
	ldr	x1, [x19,1248]
	ldrh	w3, [x19,860]
	add	x0, x0, :lo12:.LC41
	mov	w2, 4
	bl	rknand_print_hex
	mov	w0, 1
	str	w0, [x19,1408]
	b	.L1808
.L1807:
	ldr	w0, [x19,1416]
	cmp	w0, 256
	bne	.L1808
	and	w28, w28, 65535
	adrp	x0, .LC42
	mov	w1, w28
	mov	w2, w24
	add	x0, x0, :lo12:.LC42
	bl	printk
	ldr	x2, [x19,1272]
	add	x0, x19, 3760
	mov	w1, w28
	add	x2, x2, x26
	ldr	x2, [x2,8]
	bl	FtlMapWritePage
.L1808:
	add	x1, x21, :lo12:.LANCHOR2
	ldr	x0, [x1,1272]
	add	x0, x0, x26
	str	wzr, [x0,4]
	ldr	x0, [x1,1272]
	strh	w22, [x0,x26]
	b	.L1801
.L1800:
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
	ldrh	w0, [x19,820]
	stp	x23, x24, [sp,48]
	stp	x27, x28, [sp,80]
	cmp	w1, w0
	mov	x24, x2
	ldrh	w23, [x19,832]
	mov	w21, -1
	bhi	.L1814
	lsr	w23, w25, w23
	mov	w21, 0
	add	x27, x19, 3832
.L1815:
	cbz	w22, .L1814
	ldrh	w1, [x19,830]
	ldr	x0, [x19,1240]
	udiv	w26, w25, w1
	ldr	w2, [x0,w23,uxtw 2]
	uxth	w0, w22
	msub	w26, w26, w1, w25
	sub	w20, w1, w26
	uxth	w20, w20
	cmp	w20, w22
	csel	w20, w0, w20, hi
	cmp	w20, w1
	beq	.L1817
	cbz	w2, .L1817
	ldr	x0, [x19,1072]
	mov	w1, 1
	str	w2, [x29,108]
	mov	w2, w1
	str	x0, [x29,112]
	add	x0, x29, 104
	str	xzr, [x29,120]
	bl	FlashReadPages
	b	.L1818
.L1817:
	ldr	x0, [x19,1072]
	mov	w1, 0
	ldrh	w2, [x19,834]
	bl	ftl_memset
.L1818:
	ldr	x0, [x19,1072]
	lsl	w28, w20, 9
	ubfiz	x26, x26, 9, 16
	mov	x1, x24
	mov	w2, w28
	add	x0, x0, x26
	sub	w22, w22, w20
	add	w25, w25, w20
	bl	ftl_memcpy
	add	x24, x24, x28, sxtw
	ldr	x2, [x19,1072]
	mov	w1, w23
	mov	x0, x27
	add	w23, w23, 1
	bl	FtlMapWritePage
	cmn	w0, #1
	csinv	w21, w21, wzr, ne
	b	.L1815
.L1814:
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
	ldrh	w1, [x3,820]
	mov	w23, w0
	ldrh	w20, [x3,832]
	cmp	w2, w1
	mov	w0, -1
	bhi	.L1829
	adrp	x26, .LC43
	add	x0, x3, 3832
	lsr	w20, w23, w20
	mov	w21, 0
	mov	x28, x3
	add	x26, x26, :lo12:.LC43
	str	x0, [x29,104]
.L1830:
	cbz	w22, .L1838
	ldrh	w19, [x28,830]
	ldr	x0, [x28,1240]
	udiv	w24, w23, w19
	ldr	w4, [x0,w20,uxtw 2]
	uxth	w0, w22
	msub	w24, w24, w19, w23
	sub	w19, w19, w24
	uxth	w19, w19
	cmp	w19, w22
	csel	w19, w0, w19, hi
	lsl	w27, w19, 9
	cbz	w4, .L1832
	ldr	x0, [x28,1072]
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
	ldr	w0, [x28,1416]
	csinv	w21, w21, wzr, ne
	cmp	w0, 256
	bne	.L1834
	mov	w1, w20
	mov	w2, w4
	mov	x0, x26
	bl	printk
	ldr	x0, [x29,104]
	mov	w1, w20
	ldr	x2, [x28,1072]
	bl	FtlMapWritePage
.L1834:
	ldr	x1, [x28,1072]
	ubfiz	x24, x24, 9, 16
	mov	x0, x25
	mov	w2, w27
	add	x1, x1, x24
	bl	ftl_memcpy
	b	.L1835
.L1832:
	mov	x0, x25
	mov	w1, w4
	mov	w2, w27
	bl	ftl_memset
.L1835:
	add	w20, w20, 1
	sub	w22, w22, w19
	add	w23, w23, w19
	add	x25, x25, x27, sxtw
	b	.L1830
.L1838:
	mov	w0, w21
.L1829:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 176
	ret
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	adrp	x1, .LANCHOR2
	add	x3, x1, :lo12:.LANCHOR2
	ldrh	w2, [x3,3896]
	cbnz	w0, .L1840
	cmp	w2, 4
	bhi	.L1840
	add	w2, w2, 1
	strh	w2, [x3,3896]
	b	.L1839
.L1840:
	add	x0, x1, :lo12:.LANCHOR2
	mov	w7, 65535
	strh	wzr, [x0,3896]
	str	wzr, [x0,3900]
	ldrh	w5, [x0,764]
	ldr	x6, [x0,1192]
	mov	x0, 0
.L1841:
	cmp	w5, w0, uxth
	bls	.L1839
	ldrh	w4, [x6,x0,lsl 1]
	cmp	w4, w7
	beq	.L1842
	add	x3, x1, :lo12:.LANCHOR2
	ldr	w2, [x3,3900]
	add	w2, w4, w2
	str	w2, [x3,3900]
.L1842:
	add	x0, x0, 1
	b	.L1841
.L1839:
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
	cbz	w1, .L1845
	adrp	x1, .LANCHOR2
	str	wzr, [x0,56]
	add	x2, x1, :lo12:.LANCHOR2
	mov	x21, x1
	ldr	w3, [x2,1408]
	cbnz	w3, .L1845
	mov	x20, x0
	sub	w19, w19, #1
	add	x0, x2, 1416
	ldr	x3, [x2,1120]
	uxth	w19, w19
	ldr	x1, [x2,1064]
	str	x3, [x0,16]
	ubfiz	x3, x19, 2, 16
	str	x1, [x0,8]
	ldr	w3, [x4,x3]
	str	w3, [x0,4]
	cbz	w3, .L1849
	mov	w1, 1
	mov	w2, w1
	bl	FlashReadPages
	b	.L1850
.L1849:
	ldrh	w2, [x2,834]
	mov	x0, x1
	mov	w1, 255
	bl	ftl_memset
.L1850:
	add	x2, x21, :lo12:.LANCHOR2
	mov	x0, x20
	mov	w1, w19
	ldr	x2, [x2,1424]
	bl	FtlMapWritePage
.L1845:
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
	stp	x19, x20, [sp,16]
	add	x19, x22, :lo12:.LANCHOR2
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	ldr	w0, [x19,1408]
	cbnz	w0, .L1853
	ldr	x21, [x19,1120]
	add	x20, x19, 3528
	ldr	x1, [x19,1056]
	str	x1, [x19,1424]
	str	x21, [x19,1432]
	ldrh	w2, [x19,3904]
	strh	w2, [x21,2]
	mov	w2, -3932
	strh	w2, [x21]
	str	w0, [x21,12]
	ldr	w2, [x19,3912]
	str	w2, [x21,4]
	str	w0, [x21,8]
	mov	w0, 19539
	ldrh	w1, [x19,3616]
	movk	w0, 0x4654, lsl 16
	strh	w1, [x20,14]
	ldrh	w1, [x19,3618]
	ldrb	w2, [x19,3622]
	str	w0, [x19,3528]
	mov	w0, 86
	orr	w1, w2, w1, lsl 6
	strh	w1, [x20,16]
	ldrh	w1, [x19,3664]
	movk	w0, 0x5000, lsl 16
	strh	w1, [x20,18]
	ldrh	w1, [x19,3666]
	str	w0, [x20,4]
	ldrb	w2, [x19,3670]
	ldrh	w0, [x19,3910]
	strh	w0, [x20,8]
	orr	w1, w2, w1, lsl 6
	ldrh	w0, [x19,778]
	strb	w0, [x20,10]
	ldrb	w0, [x19,3624]
	strb	w0, [x20,11]
	strh	w1, [x20,20]
	ldrb	w0, [x19,3672]
	ldrh	w1, [x19,3712]
	strb	w0, [x20,12]
	strh	w1, [x20,22]
	ldrh	w1, [x19,3714]
	ldrb	w0, [x19,3720]
	strb	w0, [x20,13]
	ldr	w0, [x19,908]
	str	w0, [x20,32]
	ldr	w0, [x19,876]
	str	w0, [x20,40]
	ldr	w0, [x19,880]
	ldrb	w2, [x19,3718]
	str	w0, [x20,36]
	ldrh	w0, [x19,952]
	orr	w1, w2, w1, lsl 6
	strh	w0, [x20,44]
	ldrh	w0, [x19,954]
	strh	w0, [x20,46]
	ldrh	w2, [x19,834]
	ldr	x0, [x19,1424]
	strh	w1, [x20,24]
	mov	w1, 255
	bl	ftl_memset
	ldr	x0, [x19,1424]
	mov	x1, x20
	mov	w2, 48
	adrp	x20, .LANCHOR0
	bl	ftl_memcpy
	ldr	x0, [x19,1424]
	ldrh	w2, [x19,764]
	ldr	x1, [x19,1192]
	add	x0, x0, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,764]
	add	x1, x20, :lo12:.LANCHOR0
	ldr	x3, [x19,1424]
	uxth	x0, w2
	ldr	x1, [x1,16]
	add	x0, x0, 24
	lsr	w2, w2, 3
	lsr	x0, x0, 1
	add	w2, w2, 4
	add	x0, x3, x0, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x19,864]
	cbz	w0, .L1854
	ldrh	w0, [x19,764]
	ldrh	w2, [x19,860]
	lsr	w1, w0, 3
	add	w0, w1, w0, lsl 1
	ldr	x1, [x19,1424]
	add	w0, w0, 52
	lsl	w2, w2, 2
	and	x0, x0, 65532
	add	x0, x1, x0
	ldr	x1, [x19,1248]
	bl	ftl_memcpy
.L1854:
	mov	w0, 0
	mov	w23, 0
	mov	w25, 65535
	add	x19, x22, :lo12:.LANCHOR2
	bl	FtlUpdateVaildLpn
.L1855:
	ldrh	w1, [x19,3904]
	ldr	x0, [x19,1056]
	ldrh	w2, [x19,3906]
	str	x0, [x19,1424]
	ldr	x0, [x19,1120]
	str	x0, [x19,1432]
	orr	w0, w2, w1, lsl 10
	str	w0, [x19,1420]
	ldrh	w0, [x19,826]
	sub	w0, w0, #1
	cmp	w2, w0
	blt	.L1856
	ldrh	w25, [x19,3908]
	strh	wzr, [x19,3906]
	strh	w1, [x19,3908]
	bl	FtlFreeSysBlkQueueOut
	ldr	w1, [x19,876]
	str	w1, [x19,3912]
	add	w2, w1, 1
	str	w2, [x19,876]
	ubfiz	w2, w0, 10, 16
	str	w2, [x19,1420]
	strh	w0, [x19,3904]
	str	w1, [x21,4]
	strh	w0, [x21,2]
.L1856:
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L1857
	ldr	x0, [x19,1056]
	ldrh	w1, [x19,834]
	bl	js_hash
	str	w0, [x21,12]
.L1857:
	mov	w1, 1
	add	x0, x19, 1416
	mov	w2, w1
	mov	w3, w1
	bl	FlashProgPages
	ldrh	w1, [x19,3906]
	ldr	w2, [x19,1416]
	add	w1, w1, 1
	cmn	w2, #1
	uxth	w1, w1
	strh	w1, [x19,3906]
	bne	.L1858
	cmp	w1, 1
	bne	.L1859
	ldrh	w1, [x19,826]
	sub	w1, w1, #1
	strh	w1, [x19,3906]
.L1859:
	add	w23, w23, 1
	uxth	w23, w23
	cmp	w23, 3
	bls	.L1855
	add	x22, x22, :lo12:.LANCHOR2
	adrp	x0, .LC44
	add	x0, x0, :lo12:.LC44
	mov	w2, w23
	ldr	w1, [x22,1420]
	bl	printk
	mov	w0, 1
	str	w0, [x22,1408]
	b	.L1853
.L1858:
	cmp	w1, 1
	beq	.L1855
	cmp	w2, 256
	beq	.L1855
	mov	w0, 65535
	cmp	w25, w0
	beq	.L1853
	mov	w0, w25
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1853:
	mov	w0, 0
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
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
	strh	wzr, [x20,3920]
	mov	x26, 56
	ldr	w2, [x20,852]
	mov	w22, 65535
	ldr	x0, [x20,1264]
	lsl	w2, w2, 2
	strh	wzr, [x20,1400]
	bl	ftl_memset
	ldr	w2, [x20,852]
	mov	w1, 0
	ldr	x0, [x20,1200]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x20,844]
	mov	w1, 0
	ldr	x0, [x20,1232]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,844]
	mov	w1, 0
	ldr	x0, [x20,1216]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x20, 3904
	mov	w1, 255
	mov	w2, 16
	bl	ftl_memset
	ldrh	w21, [x20,764]
.L1869:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,766]
	cmp	w0, w21
	bls	.L1910
	mov	x27, 0
	ldrh	w3, [x1,756]
	ldrh	w2, [x1,836]
	mov	w20, w27
	mov	x23, x1
.L1911:
	cmp	w3, w27, uxth
	bls	.L1944
	add	x0, x23, 784
	mov	w1, w21
	str	x2, [x29,112]
	str	x3, [x29,120]
	ldrb	w0, [x0,x27]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,120]
	ldr	x2, [x29,112]
	cbnz	w0, .L1870
	mov	w0, 56
	ldr	x1, [x23,1000]
	lsl	w28, w28, 10
	mov	w4, 4
	umull	x0, w20, w0
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x23,1000]
	add	x0, x1, x0
	ldr	x1, [x23,1080]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w4
	uxth	w20, w20
	ldr	x4, [x23,1136]
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1870:
	add	x27, x27, 1
	b	.L1911
.L1944:
	cbz	w20, .L1873
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x23, 0
	mov	w27, -1
	adrp	x28, .LANCHOR0
	ldr	x0, [x0,1000]
	bl	FlashReadPages
.L1874:
	cmp	w20, w23, uxth
	bls	.L1873
	add	x0, x19, :lo12:.LANCHOR2
	mul	x5, x23, x26
	ldr	x0, [x0,1000]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	ldr	w4, [x1,4]
	cmn	w0, #1
	ldr	x25, [x1,16]
	ubfx	x24, x4, 10, 16
	bne	.L1876
	mov	w6, 16
.L1875:
	add	x7, x19, :lo12:.LANCHOR2
	str	x6, [x29,104]
	str	x7, [x29,112]
	str	x5, [x29,120]
	ldr	x0, [x7,1000]
	add	x0, x0, x5
	ldr	w1, [x0,4]
	add	w1, w1, 1
	str	w1, [x0,4]
	mov	w1, 1
	ldr	x0, [x7,1000]
	mov	w2, w1
	add	x0, x0, x5
	bl	FlashReadPages
	ldrh	w0, [x25]
	ldr	x7, [x29,112]
	cmp	w0, w22
	ldr	x5, [x29,120]
	ldr	x6, [x29,104]
	ldr	x0, [x7,1000]
	bne	.L1877
	str	w27, [x0,x5]
	b	.L1876
.L1877:
	ldr	w0, [x0,x5]
	cmn	w0, #1
	bne	.L1876
	sub	w6, w6, #1
	uxth	w6, w6
	cbnz	w6, .L1875
.L1876:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x1, [x0,1000]
	ldr	w1, [x1,x5]
	cmn	w1, #1
	beq	.L1879
	ldr	w0, [x0,876]
	cmn	w0, #1
	beq	.L1880
	ldr	w1, [x25,4]
	cmp	w0, w1
	bhi	.L1881
.L1880:
	ldr	w0, [x25,4]
	cmn	w0, #1
	beq	.L1881
	add	x1, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	str	w0, [x1,876]
.L1881:
	ldrh	w0, [x25]
	mov	w1, 61604
	cmp	w0, w1
	beq	.L1883
	bhi	.L1884
	mov	w1, 61574
	cmp	w0, w1
	bne	.L1882
	add	x5, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,844]
	ldrh	w0, [x5,1400]
	sub	w2, w6, #1
	ldr	x5, [x5,1232]
	sxth	x1, w2
	sub	w2, w2, w0
	b	.L1897
.L1884:
	mov	w1, 61634
	cmp	w0, w1
	beq	.L1886
	cmp	w0, w22
	bne	.L1882
	mov	w0, w24
	b	.L1943
.L1886:
	add	x5, x19, :lo12:.LANCHOR2
	ldr	w6, [x5,852]
	ldrh	w0, [x5,3920]
	ldr	x5, [x5,1264]
	uxth	w2, w6
	sub	w1, w2, #1
	sub	w2, w2, w0
	sub	w2, w2, #1
	sxth	x1, w1
	sxth	w2, w2
.L1888:
	cmp	w1, w2
	ble	.L1945
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bls	.L1889
	ldr	w2, [x5]
	cbnz	w2, .L1890
	cmp	w0, w6
	beq	.L1890
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,3920]
.L1890:
	mov	w0, 0
.L1891:
	cmp	w0, w1
	beq	.L1946
	add	x5, x19, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x9, [x5,1264]
	sxth	w0, w0
	add	x10, x9, x6
	ldr	w10, [x10,4]
	str	w10, [x9,x6]
	ldr	x5, [x5,1200]
	add	x6, x5, x2
	ldrh	w6, [x6,2]
	strh	w6, [x5,x2]
	b	.L1891
.L1946:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x0, [x2,1264]
	str	w5, [x0,x7]
	ldr	x0, [x2,1200]
	strh	w24, [x0,x8,lsl 1]
	tbz	w1, #31, .L1893
	b	.L1882
.L1889:
	sub	w1, w1, #1
	sxth	x1, w1
	b	.L1888
.L1945:
	tbz	w1, #31, .L1926
	b	.L1882
.L1893:
	ldrh	w0, [x2,3920]
	ldr	w2, [x2,852]
	sub	w2, w2, w0
	sub	w2, w2, #1
	cmp	w1, w2, sxth
	bgt	.L1882
.L1926:
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldr	w3, [x25,4]
	strh	w0, [x2,3920]
	ldr	x0, [x2,1264]
	str	w3, [x0,x1,lsl 2]
	ldr	x0, [x2,1200]
	b	.L1942
.L1903:
	sbfiz	x7, x1, 2, 32
	ldr	w10, [x25,4]
	sxth	x8, w1
	ldr	w9, [x5,x7]
	cmp	w10, w9
	bhi	.L1947
	sub	w1, w1, #1
	sxth	x1, w1
.L1897:
	cmp	w1, w2
	bgt	.L1903
	b	.L1902
.L1947:
	ldr	w2, [x5]
	cbnz	w2, .L1899
	cmp	w0, w6
	beq	.L1899
	add	x2, x19, :lo12:.LANCHOR2
	add	w0, w0, 1
	strh	w0, [x2,1400]
.L1899:
	mov	w0, 0
.L1900:
	cmp	w0, w1
	beq	.L1948
	add	x5, x19, :lo12:.LANCHOR2
	sxtw	x2, w0
	lsl	x6, x2, 2
	lsl	x2, x2, 1
	add	w0, w0, 1
	ldr	x9, [x5,1232]
	sxth	w0, w0
	add	x10, x9, x6
	ldr	w10, [x10,4]
	str	w10, [x9,x6]
	ldr	x5, [x5,1216]
	add	x6, x5, x2
	ldrh	w6, [x6,2]
	strh	w6, [x5,x2]
	b	.L1900
.L1948:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w5, [x25,4]
	ldr	x2, [x0,1232]
	str	w5, [x2,x7]
	ldr	x0, [x0,1216]
	strh	w24, [x0,x8,lsl 1]
.L1902:
	tbnz	w1, #31, .L1882
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,844]
	ldrh	w5, [x0,1400]
	sub	w2, w2, #1
	sub	w2, w2, w5
	cmp	w1, w2, sxth
	bgt	.L1882
	ldr	x2, [x0,1232]
	add	w5, w5, 1
	ldr	w3, [x25,4]
	strh	w5, [x0,1400]
	str	w3, [x2,x1,lsl 2]
	ldr	x0, [x0,1216]
.L1942:
	strh	w24, [x0,x1,lsl 1]
	b	.L1882
.L1883:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x0, 3904
	ldrh	w2, [x0,3904]
	cmp	w2, w22
	bne	.L1904
	strh	w24, [x0,3904]
	ldr	w0, [x25,4]
	str	w0, [x1,8]
	b	.L1882
.L1904:
	ldrh	w0, [x1,4]
	cmp	w0, w22
	beq	.L1905
	mov	w1, 1
	bl	FtlFreeSysBlkQueueIn
.L1905:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x25,4]
	add	x0, x1, 3904
	ldr	w5, [x0,8]
	cmp	w5, w2
	bcs	.L1906
	ldrh	w2, [x1,3904]
	strh	w2, [x0,4]
	strh	w24, [x1,3904]
	ldr	w1, [x25,4]
	str	w1, [x0,8]
	b	.L1882
.L1906:
	strh	w24, [x0,4]
	b	.L1882
.L1879:
	add	x0, x28, :lo12:.LANCHOR0
	ldrb	w1, [x0,4044]
	mov	w0, w24
	cbz	w1, .L1907
.L1943:
	mov	w1, 0
.L1907:
	bl	FtlFreeSysBlkQueueIn
.L1882:
	add	x23, x23, 1
	b	.L1874
.L1873:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1869
.L1910:
	ldr	x2, [x1,1200]
	ldrh	w0, [x2]
	cbz	w0, .L1912
.L1915:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x2, [x1,1216]
	ldrh	w0, [x2]
	cbz	w0, .L1913
	b	.L1936
.L1912:
	ldrh	w3, [x1,3920]
	cbz	w3, .L1915
	ldr	w1, [x1,852]
.L1916:
	cmp	w0, w1
	bcs	.L1915
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1917
	mov	w6, w0
.L1918:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,852]
	cmp	w0, w2
	bcs	.L1915
	sxtw	x5, w0
	sub	w2, w0, w6
	ldr	x4, [x1,1200]
	lsl	x3, x5, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w7, [x4,x3]
	sxth	w0, w0
	strh	w7, [x4,x2,lsl 1]
	ldr	x4, [x1,1264]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x2,lsl 2]
	ldr	x1, [x1,1200]
	strh	wzr, [x1,x3]
	b	.L1918
.L1917:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1916
.L1913:
	ldrh	w3, [x1,1400]
	cbz	w3, .L1936
	ldrh	w1, [x1,844]
.L1921:
	cmp	w0, w1
	mov	w6, w0
	bge	.L1936
	ldrh	w3, [x2,w0,sxtw 1]
	cbz	w3, .L1922
.L1923:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,844]
	cmp	w0, w2
	bge	.L1936
	sxtw	x5, w0
	sub	w2, w0, w6
	ldr	x4, [x1,1216]
	lsl	x3, x5, 1
	sxtw	x2, w2
	add	w0, w0, 1
	ldrh	w7, [x4,x3]
	sxth	w0, w0
	strh	w7, [x4,x2,lsl 1]
	ldr	x4, [x1,1232]
	ldr	w5, [x4,x5,lsl 2]
	str	w5, [x4,x2,lsl 2]
	ldr	x1, [x1,1216]
	strh	wzr, [x1,x3]
	b	.L1923
.L1922:
	add	w0, w0, 1
	sxth	w0, w0
	b	.L1921
.L1936:
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
	ldrh	w1, [x19,1152]
	ldr	x2, [x19,1168]
	bl	FtlVendorPartRead
	ldr	x0, [x19,1168]
	ldr	w1, [x0]
	mov	w0, 17221
	movk	w0, 0x4254, lsl 16
	cmp	w1, w0
	beq	.L1950
	adrp	x0, .LC45
	adrp	x1, .LC46
	add	x1, x1, :lo12:.LC46
	add	x0, x0, :lo12:.LC45
	bl	printk
	ldrh	w2, [x19,1152]
	mov	w1, 0
	ldr	x0, [x19,1168]
	lsl	w2, w2, 9
	bl	ftl_memset
.L1950:
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
	cbz	w1, .L1952
	bl	ftl_set_blk_mode.part.17
	b	.L1951
.L1952:
	adrp	x1, .LANCHOR0+16
	ubfx	x2, x0, 5, 11
	lsl	x2, x2, 2
	ldr	x3, [x1,#:lo12:.LANCHOR0+16]
	mov	w1, 1
	lsl	w0, w1, w0
	ldr	w1, [x3,x2]
	bic	w0, w1, w0
	str	w0, [x3,x2]
.L1951:
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
	adrp	x20, .LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	add	x19, x20, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, 3928
	mov	w1, 0
	mov	x2, 8192
	mov	w21, 0
	bl	memset
.L1956:
	ldr	w0, [x19,1404]
	cmp	w21, w0
	bcs	.L1973
	mov	w0, w21
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	cmn	w0, #1
	beq	.L1957
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x2, x19, 3928
	ldrh	w1, [x2,w0,sxtw 1]
	add	w1, w1, 1
	strh	w1, [x2,w0,sxtw 1]
.L1957:
	add	w21, w21, 1
	b	.L1956
.L1973:
	adrp	x21, .LC48
	mov	w19, 0
	add	x20, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LC48
.L1959:
	ldrh	w0, [x20,764]
	cmp	w0, w19
	bls	.L1974
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x20,1192]
	ldrh	w2, [x1,x0]
	add	x0, x20, 3928
	ldrh	w3, [x0,w19,sxtw 1]
	cmp	w2, w3
	beq	.L1960
	mov	x0, x21
	mov	w1, w19
	bl	printk
.L1960:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L1959
.L1974:
	ldr	x0, [x20,3600]
	cbz	x0, .L1955
	ldr	x19, [x20,1288]
	adrp	x22, .LC49
	ldrh	w23, [x20,3608]
	mov	w21, 0
	sub	x19, x0, x19
	mov	x0, -6148914691236517206
	asr	x19, x19, 1
	add	x22, x22, :lo12:.LC49
	mov	w24, 6
	mov	w25, 65535
	madd	x19, x0, x19, x19
	uxth	w19, w19
.L1963:
	cmp	w21, w23
	beq	.L1955
	ubfiz	x0, x19, 1, 16
	ldr	x1, [x20,1192]
	ldrh	w2, [x1,x0]
	cbz	w2, .L1964
	add	x3, x20, 3928
	mov	x0, x22
	mov	w1, w19
	ldrh	w3, [x3,w19,sxtw 1]
	bl	printk
.L1964:
	umull	x19, w19, w24
	ldr	x0, [x20,1288]
	ldrh	w19, [x0,x19]
	cmp	w19, w25
	beq	.L1955
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1963
.L1955:
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
	add	x19, x2, 1416
	uxth	w23, w0
	adrp	x22, .LC50
	ldr	x0, [x2,1056]
	adrp	x25, .LC51
	str	x0, [x19,8]
	lsl	w24, w23, 10
	ldr	x0, [x2,1120]
	mov	w20, 0
	str	x0, [x19,16]
	mov	x21, x2
	add	x22, x22, :lo12:.LC50
	add	x25, x25, :lo12:.LC51
.L1976:
	ldrh	w0, [x21,826]
	cmp	w20, w0
	bge	.L1982
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
	beq	.L1977
	ldr	x1, [x21,1056]
	mov	x0, x25
	mov	w2, 4
	mov	w3, 768
	bl	rknand_print_hex
.L1977:
	add	w20, w20, 1
	sxth	w20, w20
	b	.L1976
.L1982:
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
	add	x19, x22, 1416
	bl	printk
.L1984:
	add	x0, x23, :lo12:.LANCHOR2
	ldr	w0, [x0,1404]
	cmp	w20, w0
	bcs	.L1991
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	and	w0, w20, 2047
	cbnz	w0, .L1985
	ldr	w2, [x29,76]
	mov	x0, x21
	mov	w1, w20
	bl	printk
.L1985:
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L1987
	str	w0, [x19,4]
	mov	w1, 1
	ldr	x0, [x22,1056]
	mov	w2, 0
	str	x0, [x19,8]
	mov	x0, x19
	ldr	x24, [x22,1120]
	str	w20, [x19,24]
	str	x24, [x19,16]
	str	wzr, [x19]
	bl	FlashReadPages
	ldr	w0, [x19]
	cmn	w0, #1
	beq	.L1988
	cmp	w0, 256
	beq	.L1988
	ldr	w0, [x24,8]
	cmp	w0, w20
	beq	.L1987
.L1988:
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
.L1987:
	add	w20, w20, 1
	b	.L1984
.L1991:
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
	adrp	x24, .LC55
	mov	w25, 56
	ldrh	w21, [x0,764]
	mov	w26, 4
	mov	x27, 56
	add	x24, x24, :lo12:.LC55
.L1993:
	add	x19, x22, :lo12:.LANCHOR2
	ldrh	w0, [x19,766]
	cmp	w0, w21
	bls	.L2006
	mov	x23, 0
	ldrh	w3, [x19,756]
	ldrh	w2, [x19,836]
	mov	w20, w23
.L2002:
	cmp	w3, w23, uxth
	bls	.L2007
	add	x0, x19, 784
	mov	w1, w21
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,x23]
	bl	V2P_block
	uxth	w28, w0
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L1994
	umull	x0, w20, w25
	ldr	x1, [x19,1000]
	lsl	w28, w28, 10
	add	x1, x1, x0
	str	w28, [x1,4]
	ldr	x1, [x19,1000]
	ldr	x4, [x19,1136]
	add	x0, x1, x0
	ldr	x1, [x19,1080]
	str	x1, [x0,8]
	mul	w1, w20, w2
	add	w20, w20, 1
	sdiv	w1, w1, w26
	uxth	w20, w20
	add	x1, x4, x1, sxtw 2
	str	x1, [x0,16]
.L1994:
	add	x23, x23, 1
	b	.L2002
.L2007:
	cbz	w20, .L1997
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, w20
	mov	w2, 1
	mov	x19, 0
	ldr	x0, [x0,1000]
	bl	FlashReadPages
.L1998:
	cmp	w20, w19, uxth
	bls	.L1997
	add	x0, x22, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
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
	b	.L1998
.L1997:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L1993
.L2006:
	adrp	x23, .LC56
	mov	w21, 0
	add	x22, x19, 1416
	add	x23, x23, :lo12:.LC56
.L2001:
	ldrh	w0, [x19,3920]
	cmp	w21, w0
	bge	.L2004
	sbfiz	x24, x21, 1, 32
	mov	w20, 0
.L2005:
	ldrh	w0, [x19,826]
	cmp	w20, w0
	bge	.L2008
	ldr	x0, [x19,1200]
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
	ldr	x1, [x19,1200]
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
	b	.L2005
.L2008:
	add	w21, w21, 1
	sxth	w21, w21
	b	.L2001
.L2004:
	ldr	x1, [x19,1200]
	adrp	x0, .LC57
	ldr	w3, [x19,852]
	add	x0, x0, :lo12:.LC57
	mov	w2, 2
	bl	rknand_print_hex
	ldr	x1, [x19,1248]
	adrp	x0, .LC58
	ldrh	w3, [x19,860]
	add	x0, x0, :lo12:.LC58
	mov	w2, 4
	bl	rknand_print_hex
	ldr	x1, [x19,1256]
	adrp	x0, .LC59
	ldrh	w3, [x19,860]
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
	add	x4, x6, 1416
	str	wzr, [x27,48]
	mov	x20, x6
	mov	x25, x4
	ldr	x0, [x6,1056]
	ldr	x21, [x6,1120]
	str	x0, [x6,1424]
	mov	w0, -1
	str	x21, [x6,1432]
	strh	w0, [x27]
	strh	w0, [x27,2]
	mov	w0, 1
	str	w0, [x27,56]
	ldr	w0, [x29,136]
	sub	w0, w0, #1
	str	w0, [x29,116]
.L2010:
	ldr	w0, [x29,136]
	cmp	w19, w0
	bge	.L2028
	ldr	w0, [x29,116]
	sxtw	x28, w19
	cmp	w19, w0
	bne	.L2011
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
	add	x28, x23, 1416
.L2012:
	cmp	w20, w19
	bge	.L2028
	ldrh	w0, [x25]
	mov	w1, 1
	mov	w2, w1
	orr	w0, w20, w0, lsl 10
	str	w0, [x28,4]
	mov	x0, x28
	bl	FlashReadPages
	add	x0, x26, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2013
	ldr	x0, [x28,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2013
	ldr	x0, [x28,8]
	ldrh	w1, [x23,834]
	str	x2, [x29,136]
	bl	js_hash
	ldr	x2, [x29,136]
	cmp	w2, w0
	beq	.L2013
	mov	w0, -1
	str	w0, [x28]
.L2013:
	ldr	w0, [x28]
	cmn	w0, #1
	beq	.L2014
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2014
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2014
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x28,4]
	str	w1, [x22,x0]
.L2014:
	add	w4, w20, 1
	sxth	w20, w4
	b	.L2012
.L2028:
	mov	x0, x27
	bl	ftl_free_no_use_map_blk
	ldr	x0, [x29,128]
	ldrh	w1, [x27,2]
	add	x20, x0, :lo12:.LANCHOR2
	ldrh	w0, [x20,826]
	cmp	w1, w0
	bne	.L2017
	mov	x0, x27
	bl	ftl_map_blk_alloc_new_blk
	b	.L2017
.L2011:
	ldr	x0, [x20,1056]
	str	x0, [x25,8]
	lsl	x0, x28, 1
	add	x1, x23, x0
	str	x1, [x29,120]
	ldrh	w1, [x23,x0]
	ldrh	w0, [x20,826]
	sub	w0, w0, #1
	orr	w0, w0, w1, lsl 10
	mov	w1, 1
	str	w0, [x25,4]
	mov	w2, w1
	mov	x0, x25
	bl	FlashReadPages
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2029
	ldrh	w1, [x21]
	mov	w28, 0
	ldrh	w0, [x27,4]
	cmp	w1, w0
	bne	.L2050
	ldrh	w1, [x21,8]
	mov	w0, 64245
	cmp	w1, w0
	bne	.L2050
	mov	w5, w28
.L2019:
	ldrh	w0, [x20,826]
	sub	w0, w0, #1
	cmp	w5, w0
	bge	.L2022
	ldr	x2, [x20,1056]
	sbfiz	x1, x5, 3, 32
	ldrh	w0, [x2,x1]
	cmp	w0, w24
	bcs	.L2020
	add	x1, x2, x1
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x1,4]
	str	w1, [x22,x0]
.L2020:
	add	w5, w5, 1
	sxth	w5, w5
	b	.L2019
.L2029:
	mov	w28, 0
.L2050:
	ldrh	w0, [x20,826]
	cmp	w28, w0
	bge	.L2022
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
	cbz	w0, .L2023
	ldr	x0, [x25,16]
	ldr	w2, [x0,12]
	cbz	w2, .L2023
	ldr	x0, [x25,8]
	ldrh	w1, [x20,834]
	str	x2, [x29,104]
	bl	js_hash
	ldr	x2, [x29,104]
	cmp	w2, w0
	beq	.L2023
	mov	w0, -1
	str	w0, [x25]
.L2023:
	ldr	w0, [x25]
	cmn	w0, #1
	beq	.L2024
	ldrh	w0, [x21,8]
	cmp	w0, w24
	bcs	.L2024
	ldrh	w2, [x21]
	ldrh	w1, [x27,4]
	cmp	w2, w1
	bne	.L2024
	ubfiz	x0, x0, 2, 16
	ldr	w1, [x25,4]
	str	w1, [x22,x0]
.L2024:
	add	w5, w28, 1
	sxth	w28, w5
	b	.L2050
.L2022:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2010
.L2017:
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
	adrp	x1, .LANCHOR2
	add	x1, x1, :lo12:.LANCHOR2
	stp	x29, x30, [sp, -16]!
	add	x0, x1, 3832
	add	x29, sp, 0
	ldrh	w2, [x1,844]
	strh	w2, [x0,10]
	mov	w2, -3962
	strh	w2, [x0,4]
	ldrh	w2, [x1,1400]
	strh	w2, [x0,8]
	ldrh	w2, [x1,846]
	strh	w2, [x0,6]
	ldr	x2, [x1,1216]
	str	x2, [x0,16]
	ldr	x2, [x1,1232]
	str	x2, [x0,24]
	ldr	x2, [x1,1224]
	ldr	x1, [x1,1240]
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
	ldr	w2, [x19,852]
	ldr	x0, [x19,1208]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w3, [x19,834]
	mov	w1, 255
	ldrh	w2, [x19,862]
	ldr	x0, [x19,1280]
	mul	w2, w3, w2
	bl	ftl_memset
	mov	w2, 0
	mov	w4, -1
.L2053:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,862]
	cmp	w1, w2
	bls	.L2055
	ldr	x3, [x0,1272]
	ubfiz	x1, x2, 4, 16
	add	x3, x3, x1
	str	wzr, [x3,4]
	ldr	x3, [x0,1272]
	strh	w4, [x3,x1]
	ldr	x3, [x0,1272]
	add	x3, x3, x1
	ldrh	w1, [x0,834]
	ldr	x0, [x0,1280]
	mul	w1, w1, w2
	add	w2, w2, 1
	sxtw	x1, w1
	uxth	w2, w2
	and	x1, x1, -4
	add	x0, x0, x1
	str	x0, [x3,8]
	b	.L2053
.L2055:
	mov	w2, -1
	strh	w2, [x0,3762]
	strh	w2, [x0,3760]
	add	x1, x0, 3760
	ldr	w2, [x0,852]
	strh	w2, [x0,3770]
	mov	w2, -3902
	strh	w2, [x0,3764]
	ldrh	w2, [x0,3920]
	strh	w2, [x0,3768]
	ldrh	w2, [x0,860]
	strh	w2, [x0,3766]
	ldr	x2, [x0,1200]
	str	x2, [x0,3776]
	ldr	x2, [x0,1264]
	str	x2, [x0,3784]
	ldr	x2, [x0,1208]
	str	x2, [x0,3792]
	ldr	x0, [x0,1248]
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
	add	x0, x0, 3760
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
	ldrh	w4, [x4,756]
.L2058:
	cmp	w2, w4
	bcs	.L2064
	add	x5, x0, x2, sxtw 1
	ldrh	w5, [x5,16]
	cmp	w5, w6
	beq	.L2059
	ldrh	w5, [x0,4]
	add	w5, w5, 1
	strh	w5, [x0,4]
.L2059:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2058
.L2064:
	add	x3, x3, :lo12:.LANCHOR2
	mov	w6, 65535
	add	x4, x0, x4, uxth 1
	ldrh	w5, [x3,824]
	mov	x3, x0
.L2061:
	cmp	x3, x4
	beq	.L2065
	ldrh	w2, [x3,16]
	cmp	w2, w6
	beq	.L2062
	ldrh	w2, [x0,4]
	add	w2, w5, w2
	sub	w2, w2, #1
	sub	w2, w2, w1
	strh	w2, [x0,4]
.L2062:
	add	x3, x3, 2
	b	.L2061
.L2065:
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
	ldr	x3, [x2,1192]
	ldrh	w1, [x3,x22]
	cbnz	w1, .L2067
	ldr	x19, [x2,3600]
	cbz	x19, .L2068
	ldrh	w3, [x2,3608]
	mov	x4, -6148914691236517206
	ldr	x2, [x2,1288]
	mov	w5, 65535
	sub	x19, x19, x2
	asr	x19, x19, 1
	madd	x19, x4, x19, x19
	mov	w4, 6
	uxth	w19, w19
.L2069:
	cmp	w1, w3
	beq	.L2068
	cmp	w19, w0
	bne	.L2070
	add	x21, x21, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x21, 3600
	bl	List_remove_node
	ldrh	w0, [x21,3608]
	sub	w0, w0, #1
	strh	w0, [x21,3608]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x1, [x21,1192]
	ldrh	w0, [x1,x22]
	add	w0, w0, 1
	strh	w0, [x1,x22]
	b	.L2068
.L2070:
	umull	x19, w19, w4
	ldrh	w19, [x2,x19]
	cmp	w19, w5
	beq	.L2068
	add	w1, w1, 1
	uxth	w1, w1
	b	.L2069
.L2067:
	add	w1, w1, 1
	strh	w1, [x3,x22]
.L2068:
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
	ldrh	w24, [x1,756]
.L2076:
	cmp	w24, w21, uxth
	bls	.L2087
	add	x0, x23, 784
	ldrh	w1, [x19]
	ldrb	w0, [x0,x21]
	bl	V2P_block
	strh	w25, [x22]
	mov	w26, w0
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2077
	strh	w26, [x22]
	ldrb	w0, [x19,7]
	add	w0, w0, 1
	strb	w0, [x19,7]
.L2077:
	add	x21, x21, 1
	add	x22, x22, 2
	b	.L2076
.L2087:
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w1, [x19,7]
	strb	wzr, [x19,9]
	ldrh	w2, [x0,824]
	mul	w1, w1, w2
	strh	w1, [x19,4]
	ldr	w1, [x0,660]
	cbz	w1, .L2079
	ldrh	w1, [x19]
	ldr	x0, [x0,1176]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 79
	bhi	.L2079
	mov	w0, 1
	strb	w0, [x19,9]
.L2079:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbz	w0, .L2080
	mov	w0, 1
	strb	w0, [x19,9]
.L2080:
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
	stp	x29, x30, [sp, -96]!
	mov	w1, 0
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	add	x19, x20, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	add	x22, x19, 1416
	add	x23, x19, 3904
	ldr	x0, [x19,1056]
	ldrh	w2, [x19,764]
	str	x0, [x22,8]
	ldr	x0, [x19,1120]
	lsl	w2, w2, 1
	str	x0, [x22,16]
	ldr	x0, [x19,1192]
	bl	ftl_memset
	ldrh	w0, [x19,3904]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2089
.L2100:
	mov	w19, -1
	b	.L2090
.L2089:
	mov	w1, 1
	adrp	x24, .LANCHOR0
	mov	w25, 61604
	mov	w26, -1
	bl	FtlGetLastWrittenPage
	ldrsh	w28, [x19,3904]
	sxth	w21, w0
	add	w0, w0, 1
	strh	w0, [x23,2]
.L2091:
	adrp	x23, .LANCHOR0
	tbnz	w21, #31, .L2099
	orr	w0, w21, w28, lsl 10
	mov	w1, 1
	str	w0, [x22,4]
	mov	w2, w1
	ldr	x0, [x19,1056]
	str	x0, [x22,8]
	mov	x0, x22
	bl	FlashReadPages
	add	x0, x24, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbz	w0, .L2092
	ldr	x0, [x22,16]
	ldr	w27, [x0,12]
	cbz	w27, .L2092
	ldr	x0, [x22,8]
	ldrh	w1, [x19,834]
	bl	js_hash
	cmp	w27, w0
	beq	.L2092
	cbnz	w21, .L2093
	ldrh	w0, [x19,3908]
	cmp	w28, w0
	beq	.L2093
	sxth	w28, w0
	ldrh	w21, [x19,826]
	b	.L2095
.L2093:
	str	w26, [x22]
.L2092:
	ldr	w0, [x22]
	cmn	w0, #1
	beq	.L2095
	ldr	x0, [x19,1056]
	ldr	w1, [x0]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	bne	.L2095
	ldr	x0, [x19,1120]
	ldrh	w0, [x0]
	cmp	w0, w25
	bne	.L2095
.L2099:
	add	x19, x20, :lo12:.LANCHOR2
	mov	w2, 48
	add	x21, x19, 1416
	add	x0, x19, 3528
	ldr	x1, [x21,8]
	bl	ftl_memcpy
	ldr	x1, [x21,8]
	ldrh	w2, [x19,764]
	ldr	x0, [x19,1192]
	add	x1, x1, 48
	lsl	w2, w2, 1
	bl	ftl_memcpy
	ldrh	w2, [x19,764]
	add	x0, x23, :lo12:.LANCHOR0
	ldr	x3, [x21,8]
	uxth	x1, w2
	ldr	x0, [x0,16]
	add	x1, x1, 24
	lsr	w2, w2, 3
	lsr	x1, x1, 1
	add	w2, w2, 4
	add	x1, x3, x1, lsl 2
	bl	ftl_memcpy
	ldrh	w0, [x19,864]
	cbz	w0, .L2097
	ldrh	w0, [x19,764]
	ldr	x3, [x21,8]
	lsr	w1, w0, 3
	ldrh	w2, [x19,860]
	add	w1, w1, w0, lsl 1
	ldr	x0, [x19,1256]
	add	w1, w1, 52
	lsl	w2, w2, 2
	and	x1, x1, 65532
	add	x1, x3, x1
	bl	ftl_memcpy
	b	.L2097
.L2095:
	sub	w21, w21, #1
	sxth	w21, w21
	b	.L2091
.L2097:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	ldr	w3, [x0,3528]
	cmp	w3, w2
	bne	.L2100
	ldrb	w3, [x0,3538]
	ldrh	w2, [x0,778]
	ldrh	w5, [x0,3536]
	strh	w5, [x0,3910]
	cmp	w3, w2
	bne	.L2100
	ldrh	w3, [x0,824]
	add	x23, x23, :lo12:.LANCHOR0
	ldrh	w2, [x0,830]
	adrp	x6, .LANCHOR4
	add	x4, x6, :lo12:.LANCHOR4
	mov	x19, x6
	mul	w3, w3, w5
	str	w3, [x0,1404]
	str	w5, [x4,3416]
	mul	w3, w3, w2
	str	w3, [x23,4048]
	ldrh	w2, [x0,1310]
	ldr	w3, [x0,768]
	strh	wzr, [x4,3426]
	sub	w2, w3, w2
	ldrh	w3, [x0,756]
	sub	w2, w2, w5
	strb	wzr, [x4,3430]
	strb	wzr, [x4,3432]
	udiv	w2, w2, w3
	ldrh	w3, [x0,3544]
	strh	w2, [x4,3420]
	lsr	w7, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,3622]
	ldrb	w3, [x0,3539]
	strb	w3, [x0,3624]
	mov	w3, -1
	strh	w3, [x4,3424]
	ldrh	w3, [x0,3546]
	strh	w3, [x0,3664]
	ldrh	w3, [x0,3548]
	ldrh	w5, [x0,3542]
	lsr	w4, w3, 6
	and	w3, w3, 63
	strb	w3, [x0,3670]
	ldrb	w3, [x0,3540]
	strb	w3, [x0,3672]
	ldrh	w3, [x0,3550]
	strh	w3, [x0,3712]
	ldrh	w3, [x0,3552]
	strh	w4, [x0,3666]
	lsr	w4, w3, 6
	and	w3, w3, 63
	strh	w5, [x0,3616]
	strb	w3, [x0,3718]
	strh	w7, [x0,3618]
	strh	w4, [x0,3714]
	ldrb	w3, [x0,3541]
	strb	w3, [x0,3720]
	ldr	w2, [x0,3560]
	str	w2, [x0,908]
	ldr	w1, [x0,3568]
	ldr	w2, [x0,876]
	str	wzr, [x0,884]
	str	wzr, [x0,888]
	cmp	w1, w2
	str	wzr, [x0,904]
	str	wzr, [x0,900]
	str	wzr, [x0,912]
	str	wzr, [x0,920]
	str	wzr, [x0,896]
	bls	.L2101
	str	w1, [x0,876]
.L2101:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x0,3564]
	ldr	w2, [x0,880]
	cmp	w1, w2
	bls	.L2102
	str	w1, [x0,880]
.L2102:
	mov	w0, 65535
	cmp	w5, w0
	beq	.L2103
	add	x0, x20, :lo12:.LANCHOR2
	add	x0, x0, 3616
	bl	make_superblock
.L2103:
	add	x1, x20, :lo12:.LANCHOR2
	add	x0, x1, 3664
	ldrh	w2, [x1,3664]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2104
	bl	make_superblock
.L2104:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x0, x20, 3712
	ldrh	w2, [x20,3712]
	cmp	w2, w1
	beq	.L2105
	bl	make_superblock
.L2105:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w19, 0
	add	x0, x1, 3424
	ldrh	w2, [x1,3424]
	mov	w1, 65535
	cmp	w2, w1
	beq	.L2090
	bl	make_superblock
.L2090:
	mov	w0, w19
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 96
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
	ldrh	w23, [x19,824]
	bl	printk
	ldr	x3, [x19,1192]
	ubfiz	x2, x22, 1, 16
	adrp	x0, .LC60
	mov	w1, w22
	add	x0, x0, :lo12:.LC60
	ldrh	w2, [x3,x2]
	bl	printk
	strh	w22, [x29,112]
	add	x0, x29, 112
	bl	make_superblock
	cbnz	w24, .L2125
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2125
	mov	w0, w22
	bl	ftl_get_blk_mode
	cmp	w0, 1
	mov	w22, w0
	bne	.L2117
	ldrh	w23, [x19,826]
	b	.L2117
.L2125:
	mov	w22, 0
.L2117:
	add	x3, x20, :lo12:.LANCHOR2
	adrp	x0, .LC61
	adrp	x24, .LC56
	add	x0, x0, :lo12:.LC61
	mov	w1, w22
	mov	w2, w23
	ldrh	w3, [x3,824]
	mov	w21, 0
	mov	w26, 65535
	mov	w27, 56
	mov	w25, 4
	add	x24, x24, :lo12:.LC56
	bl	printk
.L2118:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x3, 0
	mov	w19, w3
	ldrh	w5, [x0,756]
	ldrh	w6, [x0,834]
	ldrh	w7, [x0,836]
.L2119:
	cmp	w5, w3, uxth
	bls	.L2133
	add	x0, x29, 112
	add	x0, x0, x3, lsl 1
	ldrh	w1, [x0,16]
	cmp	w1, w26
	beq	.L2120
	add	x2, x20, :lo12:.LANCHOR2
	orr	w1, w21, w1, lsl 10
	umull	x0, w19, w27
	ldr	x4, [x2,1000]
	add	x4, x4, x0
	str	w1, [x4,4]
	ldr	x1, [x2,1000]
	ldr	x4, [x2,1080]
	add	x0, x1, x0
	ldr	x2, [x2,1136]
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
.L2120:
	add	x3, x3, 1
	b	.L2119
.L2133:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, w22
	mov	x28, 0
	ldr	x0, [x0,1000]
	bl	FlashReadPages
	mov	x8, 56
.L2122:
	cmp	w19, w28, uxth
	bls	.L2134
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x29,112]
	mul	x2, x28, x8
	str	x8, [x29,104]
	add	x28, x28, 1
	ldr	x4, [x0,1000]
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
	b	.L2122
.L2134:
	add	w21, w21, 1
	uxth	w21, w21
	cmp	w21, w23
	bne	.L2118
.L2124:
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
	adrp	x23, .LC63
	adrp	x22, .LC62
	adrp	x24, .LC64
	add	x22, x22, :lo12:.LC62
	stp	x19, x20, [sp,32]
	add	x23, x23, :lo12:.LC63
	mov	w20, 0
	add	x24, x24, :lo12:.LC64
	stp	x25, x26, [sp,80]
	bl	printk
.L2136:
	adrp	x21, .LANCHOR2
	add	x19, x21, :lo12:.LANCHOR2
	ldrh	w0, [x19,766]
	cmp	w0, w20
	bls	.L2145
	mov	w0, w20
	strh	w20, [x29,80]
	bl	ftl_get_blk_mode
	mov	w3, w0
	ldr	x4, [x19,1192]
	ubfiz	x2, x20, 1, 16
	mov	w1, w20
	mov	x0, x22
	ldrh	w2, [x4,x2]
	bl	printk
	add	x0, x29, 80
	bl	make_superblock
	mov	x1, 0
	ldrh	w6, [x19,756]
	ldrh	w7, [x19,834]
	mov	w9, 65535
	ldrh	w8, [x19,836]
	mov	w10, 56
	mov	w19, w1
	mov	w5, 4
.L2137:
	cmp	w6, w1, uxth
	bls	.L2146
	add	x0, x29, 80
	add	x0, x0, x1, lsl 1
	ldrh	w0, [x0,16]
	cmp	w0, w9
	beq	.L2138
	add	x3, x21, :lo12:.LANCHOR2
	lsl	w0, w0, 10
	umull	x2, w19, w10
	ldr	x4, [x3,1000]
	add	x4, x4, x2
	str	w0, [x4,4]
	ldr	x0, [x3,1000]
	ldr	x4, [x3,1080]
	add	x2, x0, x2
	ldr	x3, [x3,1136]
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
.L2138:
	add	x1, x1, 1
	b	.L2137
.L2146:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, w19
	mov	w2, 0
	mov	x25, 0
	mov	x26, 56
	ldr	x0, [x0,1000]
	bl	FlashReadPages
.L2140:
	add	x0, x21, :lo12:.LANCHOR2
	cmp	w19, w25, uxth
	bls	.L2147
	ldr	x4, [x0,1000]
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
	b	.L2140
.L2147:
	ldr	x0, [x0,1000]
	mov	w1, w19
	mov	w2, 1
	mov	x25, 0
	mov	x26, 56
	bl	FlashReadPages
.L2142:
	cmp	w19, w25, uxth
	bls	.L2148
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x29,80]
	mul	x2, x25, x26
	add	x25, x25, 1
	ldr	x4, [x0,1000]
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
	b	.L2142
.L2148:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2136
.L2145:
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
	ldrh	w2, [x19,766]
	mov	w22, w27
	ldr	x0, [x19,1288]
	mov	w24, -1
	mov	w25, 32768
	mul	w2, w2, w23
	bl	ftl_memset
	str	xzr, [x19,3600]
	str	xzr, [x19,3576]
	str	xzr, [x19,3584]
	strh	wzr, [x19,3592]
	strh	wzr, [x19,3608]
	strh	wzr, [x19,866]
	mov	w19, w27
.L2150:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x0,764]
	cmp	w19, w1
	bge	.L2157
	mov	w20, 0
	ldrh	w3, [x0,756]
	ldrh	w2, [x0,824]
	mov	w28, w20
	mov	x26, x0
.L2158:
	cmp	w28, w3
	bge	.L2159
	add	x0, x26, 784
	mov	w1, w19
	str	x2, [x29,96]
	str	x3, [x29,104]
	ldrb	w0, [x0,w28,sxtw]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbnz	w0, .L2151
	add	w20, w2, w20
	sxth	w20, w20
.L2151:
	add	w28, w28, 1
	sxth	w28, w28
	b	.L2158
.L2159:
	cbz	w20, .L2153
	sdiv	w20, w25, w20
	sxth	w20, w20
	b	.L2154
.L2153:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,1192]
	strh	w24, [x0,w19,sxtw 1]
.L2154:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x1, [x0,1288]
	smaddl	x1, w19, w23, x1
	strh	w20, [x1,4]
	ldrh	w1, [x0,3616]
	cmp	w19, w1
	beq	.L2155
	ldrh	w1, [x0,3664]
	cmp	w19, w1
	beq	.L2155
	ldrh	w1, [x0,3712]
	cmp	w19, w1
	beq	.L2155
	ldr	x0, [x0,1192]
	ldrh	w0, [x0,w19,sxtw 1]
	cbnz	w0, .L2156
	add	w27, w27, 1
	mov	w0, w19
	uxth	w27, w27
	bl	INSERT_FREE_LIST
	b	.L2155
.L2156:
	add	w22, w22, 1
	mov	w0, w19
	uxth	w22, w22
	bl	INSERT_DATA_LIST
.L2155:
	add	w19, w19, 1
	sxth	w19, w19
	b	.L2150
.L2157:
	strh	w22, [x0,3592]
	strh	w27, [x0,3608]
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
	add	x2, x0, 3472
	add	x29, sp, 0
	ldr	w3, [x0,3472]
	cmp	w3, w1
	bne	.L2160
	mov	w1, 86
	movk	w1, 0x5000, lsl 16
	str	w1, [x2,4]
	ldr	w1, [x0,3984]
	ldr	w0, [x0,3988]
	str	w0, [x2,92]
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	str	w1, [x2,88]
	ldr	w1, [x0,884]
	str	w1, [x2,8]
	ldr	w1, [x0,888]
	str	w1, [x2,12]
	ldr	w1, [x0,904]
	str	w1, [x2,16]
	ldr	w1, [x0,900]
	str	w1, [x2,20]
	ldr	w1, [x0,912]
	str	w1, [x2,28]
	ldr	w1, [x0,916]
	str	w1, [x2,32]
	ldr	w1, [x0,892]
	str	w1, [x2,36]
	ldr	w1, [x0,896]
	str	w1, [x2,40]
	ldr	w1, [x0,920]
	str	w1, [x2,44]
	ldr	w1, [x0,924]
	str	w1, [x2,48]
	ldr	w1, [x0,872]
	ldr	w0, [x0,868]
	str	w1, [x2,60]
	mov	w1, 1
	str	w0, [x2,64]
	mov	w0, 0
	bl	FtlVendorPartWrite
.L2160:
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
	ldr	w4, [x3,660]
	cbz	w4, .L2163
	ldr	w3, [x3,924]
	cmp	w3, 39
	mov	w3, 4
	csel	w1, w1, w3, hi
.L2163:
	adrp	x3, .LANCHOR4
	add	x3, x3, :lo12:.LANCHOR4
	ldrh	w4, [x3,3992]
	cmp	w4, 31
	bhi	.L2164
	add	w4, w4, 1
	mov	w1, 1
	strh	w4, [x3,3992]
.L2164:
	cbnz	w0, .L2165
	add	x0, x2, :lo12:.LANCHOR2
	ldr	x3, [x0,1168]
	ldr	w0, [x3,20]
	add	w1, w1, w0
	ldr	w0, [x3,16]
	cmp	w0, w1
	bcc	.L2166
.L2165:
	add	x2, x2, :lo12:.LANCHOR2
	ldr	x0, [x2,1168]
	ldr	w1, [x0,16]
	str	w1, [x0,20]
	mov	w1, 17221
	movk	w1, 0x4254, lsl 16
	str	w1, [x0]
	ldr	x0, [x2,1168]
	ldrh	w1, [x2,1152]
	str	wzr, [x0,4]
	lsl	w3, w1, 9
	str	w3, [x0,12]
	ldr	w3, [x0,8]
	ldr	x2, [x2,1168]
	add	w3, w3, 1
	str	w3, [x0,8]
	mov	w0, 64
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2166:
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
	add	x21, x20, 3472
	mov	x2, x21
	bl	FtlVendorPartRead
	ldr	w1, [x20,3472]
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	cmp	w1, w0
	beq	.L2171
	mov	x0, x21
	mov	w1, 0
	mov	w2, 512
	bl	ftl_memset
	mov	w0, 19539
	movk	w0, 0x4654, lsl 16
	str	w0, [x20,3472]
.L2171:
	add	x1, x19, :lo12:.LANCHOR4
	mov	w2, 19539
	movk	w2, 0x4654, lsl 16
	add	x0, x1, 3472
	adrp	x20, .LANCHOR2
	ldr	w3, [x1,3472]
	cmp	w3, w2
	bne	.L2172
	ldr	w2, [x1,3560]
	str	w2, [x1,3984]
	ldr	w2, [x1,3564]
	str	w2, [x1,3988]
	add	x1, x20, :lo12:.LANCHOR2
	ldr	w2, [x0,8]
	str	w2, [x1,884]
	ldr	w2, [x0,12]
	str	w2, [x1,888]
	ldr	w2, [x0,16]
	str	w2, [x1,904]
	ldr	w2, [x0,20]
	str	w2, [x1,900]
	ldr	w2, [x0,28]
	str	w2, [x1,912]
	ldr	w2, [x0,32]
	str	w2, [x1,916]
	ldr	w2, [x0,36]
	str	w2, [x1,892]
	ldr	w2, [x0,40]
	str	w2, [x1,896]
	ldr	w2, [x0,44]
	str	w2, [x1,920]
	ldr	w2, [x0,48]
	ldr	w0, [x0,60]
	str	w2, [x1,924]
	str	w0, [x1,872]
.L2172:
	add	x1, x19, :lo12:.LANCHOR4
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w2, [x1,3540]
	mov	w1, 34661
	movk	w1, 0x1234, lsl 16
	str	wzr, [x0,868]
	cmp	w2, w1
	bne	.L2173
	mov	w1, 1
	str	w1, [x0,660]
	adrp	x0, .LC45
	adrp	x1, .LC65
	add	x0, x0, :lo12:.LC45
	add	x1, x1, :lo12:.LC65
	bl	printk
.L2173:
	add	x0, x20, :lo12:.LANCHOR2
	add	x19, x19, :lo12:.LANCHOR4
	ldr	x21, [sp,32]
	ldrh	w2, [x0,816]
	ldr	w3, [x0,908]
	ldr	w1, [x0,912]
	ldrh	w0, [x0,764]
	madd	w1, w2, w3, w1
	udiv	w0, w1, w0
	str	w0, [x19,3996]
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 48
	ret
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	sub	sp, sp, #80
	adrp	x0, .LC66
	add	x0, x0, :lo12:.LC66
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	stp	x19, x20, [sp,32]
	adrp	x19, .LANCHOR2
	add	x19, x19, :lo12:.LANCHOR2
	stp	x23, x24, [sp,64]
	adrp	x23, .LC67
	add	x20, x19, 1416
	ldrh	w1, [x19,822]
	add	x23, x23, :lo12:.LC67
	ldrh	w2, [x19,824]
	stp	x21, x22, [sp,48]
	mov	w22, 0
	bl	printk
.L2175:
	ldrh	w0, [x19,822]
	cmp	w22, w0
	bcs	.L2177
	lsl	w24, w22, 10
	mov	w21, 0
.L2178:
	ldrh	w0, [x19,824]
	cmp	w21, w0
	bcs	.L2179
	add	w0, w21, w24
	str	w0, [x20,4]
	ldr	x0, [x19,1056]
	mov	w1, 1
	str	x0, [x20,8]
	mov	w2, 0
	ldr	x0, [x19,1120]
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
	b	.L2178
.L2179:
	add	w22, w22, 1
	b	.L2175
.L2177:
	adrp	x1, .LANCHOR3
	adrp	x0, .LC68
	add	x1, x1, :lo12:.LANCHOR3
	add	x0, x0, :lo12:.LC68
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
	ldrh	w24, [x1,756]
	mov	w19, w22
	ldrh	w25, [x1,824]
	mov	x23, x1
.L2181:
	cmp	w24, w22, uxth
	bls	.L2188
	add	x0, x23, 784
	mov	w1, w20
	ldrb	w0, [x0,x22]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	w0, .L2182
	add	w19, w19, w25
	uxth	w19, w19
.L2182:
	add	x22, x22, 1
	b	.L2181
.L2188:
	cbz	w19, .L2184
	mov	w0, 32768
	sdiv	w19, w0, w19
.L2184:
	add	x0, x21, :lo12:.LANCHOR2
	mov	w1, 6
	umull	x20, w20, w1
	ldr	x0, [x0,1288]
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
	ldr	x1, [x2,3600]
	mov	w0, w1
	cbz	x1, .L2190
	ldr	x0, [x2,1288]
	sub	x1, x1, x0
	mov	x0, -6148914691236517206
	asr	x1, x1, 1
	madd	x1, x0, x1, x1
	ldr	x0, [x2,1176]
	and	x1, x1, 65535
	ldrh	w0, [x0,x1,lsl 1]
.L2190:
	ret
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	adrp	x2, .LANCHOR2
	uxth	w4, w0
	add	x5, x2, :lo12:.LANCHOR2
	ldr	x1, [x5,3600]
	mov	w0, w1
	cbz	x1, .L2193
	ldrh	w3, [x5,3608]
	mov	w0, 7
	mov	w6, 6
	mov	w7, 65535
	mul	w3, w3, w0
	ldr	x0, [x5,1288]
	asr	w3, w3, 3
	cmp	w4, w3
	sub	x1, x1, x0
	csel	w4, w3, w4, gt
	asr	x1, x1, 1
	mov	x3, -6148914691236517206
	madd	x1, x3, x1, x1
	mov	w3, 0
	uxth	w1, w1
.L2195:
	cmp	w3, w4
	beq	.L2198
	umull	x5, w1, w6
	ldrh	w5, [x0,x5]
	cmp	w5, w7
	bne	.L2196
.L2198:
	add	x2, x2, :lo12:.LANCHOR2
	ubfiz	x1, x1, 1, 16
	ldr	x0, [x2,1176]
	ldrh	w0, [x0,x1]
	b	.L2193
.L2196:
	add	w3, w3, 1
	mov	w1, w5
	uxth	w3, w3
	b	.L2195
.L2193:
	ret
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	stp	x29, x30, [sp, -112]!
	adrp	x1, .LC69
	add	x29, sp, 0
	stp	x25, x26, [sp,64]
	adrp	x25, .LANCHOR0
	add	x1, x1, :lo12:.LC69
	add	x25, x25, :lo12:.LANCHOR0
	str	x27, [sp,80]
	stp	x23, x24, [sp,48]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	mov	x23, x0
	bl	strcpy
	add	x20, x23, 12
	ldr	w2, [x25,4016]
	adrp	x1, .LC70
	adrp	x24, .LANCHOR2
	mov	x0, x20
	add	x1, x1, :lo12:.LC70
	add	x22, x24, :lo12:.LANCHOR2
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x1, .LC71
	mov	x0, x20
	ldr	w2, [x22,840]
	add	x1, x1, :lo12:.LC71
	bl	sprintf
	add	x20, x20, x0, sxtw
	adrp	x0, .LANCHOR1+3288
	ldr	w0, [x0,#:lo12:.LANCHOR1+3288]
	cmp	w0, 1
	beq	.L2201
	sub	w0, w20, w23
	b	.L2202
.L2201:
	add	x0, x29, 96
	add	x1, x29, 100
	add	x2, x29, 104
	add	x3, x29, 108
	bl	NandcGetTimeCfg
	ldr	w4, [x29,104]
	adrp	x1, .LC72
	ldr	w5, [x29,108]
	mov	x0, x20
	ldr	w3, [x29,100]
	add	x1, x1, :lo12:.LC72
	ldr	w2, [x29,96]
	bl	sprintf
	add	x21, x20, x0, sxtw
	adrp	x1, .LC73
	mov	x0, x21
	add	x1, x1, :lo12:.LC73
	add	x21, x21, 10
	adrp	x20, .LANCHOR4
	bl	strcpy
	add	x20, x20, :lo12:.LANCHOR4
	ldr	w2, [x22,1404]
	adrp	x1, .LC74
	mov	x0, x21
	add	x1, x1, :lo12:.LC74
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,3900]
	adrp	x1, .LC75
	mov	x0, x21
	add	x1, x1, :lo12:.LC75
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,904]
	adrp	x1, .LC76
	mov	x0, x21
	add	x1, x1, :lo12:.LC76
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,892]
	adrp	x1, .LC77
	mov	x0, x21
	add	x1, x1, :lo12:.LC77
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,888]
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
	ldr	w2, [x22,900]
	adrp	x1, .LC80
	mov	x0, x21
	add	x1, x1, :lo12:.LC80
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,884]
	adrp	x1, .LC81
	mov	x0, x21
	add	x1, x1, :lo12:.LC81
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3984]
	adrp	x1, .LC82
	mov	x0, x21
	add	x1, x1, :lo12:.LC82
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3988]
	adrp	x1, .LC83
	mov	x0, x21
	add	x1, x1, :lo12:.LC83
	lsr	w2, w2, 11
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,876]
	adrp	x1, .LC84
	mov	x0, x21
	add	x1, x1, :lo12:.LC84
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,880]
	adrp	x1, .LC85
	add	x1, x1, :lo12:.LC85
	mov	x0, x21
	bl	sprintf
	add	x21, x21, x0, sxtw
	bl	FtlBbtCalcTotleCnt
	uxth	w3, w0
	ldrh	w2, [x22,1310]
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
	ldr	w2, [x22,908]
	adrp	x1, .LC88
	mov	x0, x21
	add	x1, x1, :lo12:.LC88
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,912]
	adrp	x1, .LC89
	mov	x0, x21
	add	x1, x1, :lo12:.LC89
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x20,3996]
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
	ldr	w2, [x22,924]
	adrp	x1, .LC93
	mov	x0, x21
	add	x1, x1, :lo12:.LC93
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3558]
	adrp	x1, .LC94
	mov	x0, x21
	add	x1, x1, :lo12:.LC94
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3556]
	adrp	x1, .LC95
	mov	x0, x21
	add	x1, x1, :lo12:.LC95
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x25,4048]
	adrp	x1, .LC96
	mov	x0, x21
	add	x1, x1, :lo12:.LC96
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,856]
	adrp	x1, .LC97
	mov	x0, x21
	add	x1, x1, :lo12:.LC97
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,752]
	adrp	x1, .LC98
	mov	x0, x21
	add	x1, x1, :lo12:.LC98
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1478]
	adrp	x1, .LC99
	mov	x0, x21
	add	x1, x1, :lo12:.LC99
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,764]
	adrp	x1, .LC100
	mov	x0, x21
	add	x1, x1, :lo12:.LC100
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3420]
	adrp	x1, .LC101
	mov	x0, x21
	add	x1, x1, :lo12:.LC101
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w2, [x22,768]
	adrp	x1, .LC102
	mov	x0, x21
	add	x1, x1, :lo12:.LC102
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3904]
	adrp	x1, .LC103
	mov	x0, x21
	add	x1, x1, :lo12:.LC103
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,1304]
	adrp	x1, .LC104
	mov	x0, x21
	add	x1, x1, :lo12:.LC104
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3618]
	adrp	x1, .LC105
	mov	x0, x21
	add	x1, x1, :lo12:.LC105
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3622]
	adrp	x1, .LC106
	mov	x0, x21
	add	x1, x1, :lo12:.LC106
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3616]
	adrp	x1, .LC107
	mov	x0, x21
	add	x1, x1, :lo12:.LC107
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3624]
	adrp	x1, .LC108
	mov	x0, x21
	add	x1, x1, :lo12:.LC108
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3620]
	adrp	x1, .LC109
	mov	x0, x21
	add	x1, x1, :lo12:.LC109
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3616]
	adrp	x1, .LC110
	ldr	x2, [x22,1192]
	mov	x0, x21
	add	x1, x1, :lo12:.LC110
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3666]
	adrp	x1, .LC111
	mov	x0, x21
	add	x1, x1, :lo12:.LC111
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3670]
	adrp	x1, .LC112
	mov	x0, x21
	add	x1, x1, :lo12:.LC112
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3664]
	adrp	x1, .LC113
	mov	x0, x21
	add	x1, x1, :lo12:.LC113
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3672]
	adrp	x1, .LC114
	mov	x0, x21
	add	x1, x1, :lo12:.LC114
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3668]
	adrp	x1, .LC115
	mov	x0, x21
	add	x1, x1, :lo12:.LC115
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w3, [x22,3664]
	adrp	x1, .LC116
	ldr	x2, [x22,1192]
	mov	x0, x21
	add	x1, x1, :lo12:.LC116
	ldrh	w2, [x2,x3,lsl 1]
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3714]
	adrp	x1, .LC117
	mov	x0, x21
	add	x1, x1, :lo12:.LC117
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3718]
	adrp	x1, .LC118
	mov	x0, x21
	add	x1, x1, :lo12:.LC118
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3712]
	adrp	x1, .LC119
	mov	x0, x21
	add	x1, x1, :lo12:.LC119
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x22,3720]
	adrp	x1, .LC120
	mov	x0, x21
	add	x1, x1, :lo12:.LC120
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x22,3716]
	adrp	x1, .LC121
	mov	x0, x21
	add	x1, x1, :lo12:.LC121
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3426]
	adrp	x1, .LC122
	mov	x0, x21
	add	x1, x1, :lo12:.LC122
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,3430]
	adrp	x1, .LC123
	mov	x0, x21
	add	x1, x1, :lo12:.LC123
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3424]
	adrp	x1, .LC124
	mov	x0, x21
	add	x1, x1, :lo12:.LC124
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrb	w2, [x20,3432]
	adrp	x1, .LC125
	mov	x0, x21
	add	x1, x1, :lo12:.LC125
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldrh	w2, [x20,3428]
	adrp	x1, .LC126
	mov	x0, x21
	add	x1, x1, :lo12:.LC126
	bl	sprintf
	add	x21, x21, x0, sxtw
	ldr	w3, [x22,872]
	adrp	x1, .LC127
	ldr	w2, [x22,660]
	add	x1, x1, :lo12:.LC127
	ldr	w4, [x20,3556]
	mov	x0, x21
	ldr	w5, [x20,3548]
	orr	w2, w2, w3, lsl 8
	ldr	w3, [x20,3552]
	bl	sprintf
	add	x19, x21, x0, sxtw
	ldr	w2, [x20,3544]
	adrp	x1, .LC128
	mov	x0, x19
	add	x1, x1, :lo12:.LC128
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x20,3568]
	adrp	x1, .LC129
	mov	x0, x19
	add	x1, x1, :lo12:.LC129
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,960]
	adrp	x1, .LC130
	mov	x0, x19
	add	x1, x1, :lo12:.LC130
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,962]
	adrp	x1, .LC131
	mov	x0, x19
	add	x1, x1, :lo12:.LC131
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	w2, [x22,944]
	adrp	x1, .LC132
	mov	x0, x19
	add	x1, x1, :lo12:.LC132
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w2, [x22,964]
	adrp	x1, .LC133
	add	x1, x1, :lo12:.LC133
	mov	x0, x19
	bl	sprintf
	add	x19, x19, x0, sxtw
	bl	GetFreeBlockMinEraseCount
	uxth	w2, w0
	adrp	x1, .LC134
	mov	x0, x19
	add	x1, x1, :lo12:.LC134
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x22,3608]
	bl	GetFreeBlockMaxEraseCount
	uxth	w2, w0
	adrp	x1, .LC135
	mov	x0, x19
	add	x1, x1, :lo12:.LC135
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldrh	w0, [x20,3424]
	mov	w1, 65535
	cmp	w0, w1
	beq	.L2203
	ubfiz	x2, x0, 1, 16
	ldr	x3, [x22,1192]
	adrp	x1, .LC136
	mov	x0, x19
	add	x1, x1, :lo12:.LC136
	ldrh	w2, [x3,x2]
	bl	sprintf
	add	x19, x19, x0, sxtw
.L2203:
	mov	w0, 0
	adrp	x22, .LC137
	mov	w20, 0
	mov	w27, 65535
	bl	List_get_gc_head_node
	mov	w26, 6
	uxth	w3, w0
	add	x22, x22, :lo12:.LC137
.L2205:
	cmp	w3, w27
	beq	.L2204
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x6, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x5, [x21,1288]
	mov	x1, x22
	ldr	x4, [x21,1192]
	add	w20, w20, 1
	ldr	x7, [x21,1176]
	add	x5, x5, x25
	ldrh	w4, [x4,x6]
	ldrh	w5, [x5,4]
	ldrh	w6, [x7,x6]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,1288]
	cmp	w20, 16
	ldrh	w3, [x0,x25]
	bne	.L2205
.L2204:
	add	x1, x24, :lo12:.LANCHOR2
	adrp	x22, .LC138
	mov	w20, 0
	mov	w27, 65535
	mov	w26, 6
	add	x22, x22, :lo12:.LC138
	ldr	x0, [x1,3600]
	ldr	x3, [x1,1288]
	sub	x3, x0, x3
	mov	x0, -6148914691236517206
	asr	x3, x3, 1
	madd	x3, x0, x3, x3
	uxth	w3, w3
.L2207:
	cmp	w3, w27
	beq	.L2206
	add	x21, x24, :lo12:.LANCHOR2
	ubfiz	x5, x3, 1, 16
	umull	x25, w3, w26
	mov	x0, x19
	mov	w2, w20
	ldr	x4, [x21,1288]
	mov	x1, x22
	ldr	x6, [x21,1176]
	add	w20, w20, 1
	add	x4, x4, x25
	ldrh	w5, [x6,x5]
	ldrh	w4, [x4,4]
	bl	sprintf
	add	x19, x19, x0, sxtw
	ldr	x0, [x21,1288]
	cmp	w20, 4
	ldrh	w3, [x0,x25]
	bne	.L2207
.L2206:
	sub	w0, w19, w23
.L2202:
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
	adrp	x2, .LC139
	add	x29, sp, 0
	add	x1, x1, :lo12:.LC45
	add	x2, x2, :lo12:.LC139
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
	ldr	w3, [x1,3996]
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x1,924]
	cmp	w3, w2
	bcs	.L2212
	str	wzr, [x1,908]
	ldrh	w3, [x1,764]
	ldr	x5, [x1,1176]
	mov	x1, 0
.L2213:
	cmp	w3, w1
	bls	.L2245
	add	x4, x19, :lo12:.LANCHOR2
	ldrh	w6, [x5,x1,lsl 1]
	add	x1, x1, 1
	ldr	w2, [x4,908]
	add	w2, w6, w2
	str	w2, [x4,908]
	b	.L2213
.L2245:
	add	x2, x19, :lo12:.LANCHOR2
	add	x4, x0, :lo12:.LANCHOR4
	ldr	w1, [x2,908]
	udiv	w3, w1, w3
	str	w3, [x4,3996]
	ldr	w3, [x2,912]
	sub	w1, w1, w3
	ldrh	w3, [x2,816]
	udiv	w1, w1, w3
	str	w1, [x2,908]
	b	.L2215
.L2212:
	ldr	w2, [x1,920]
	cmp	w3, w2
	bls	.L2215
	add	w2, w2, 1
	str	w2, [x1,920]
	mov	w1, 0
.L2217:
	add	x2, x19, :lo12:.LANCHOR2
	ldrh	w3, [x2,764]
	cmp	w1, w3
	bcs	.L2215
	ubfiz	x3, x1, 1, 32
	ldr	x4, [x2,1176]
	add	w1, w1, 1
	ldrh	w2, [x4,x3]
	add	w2, w2, 1
	strh	w2, [x4,x3]
	b	.L2217
.L2215:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w21, [x1,924]
	ldr	w23, [x0,3996]
	add	w0, w21, 256
	cmp	w0, w23
	bls	.L2220
	ldr	w2, [x1,920]
	add	w0, w21, 768
	cmp	w0, w2
	bls	.L2220
	ldr	w0, [x1,660]
	cbz	w0, .L2223
	cmp	w21, 40
	bls	.L2220
.L2223:
	mov	w0, 65535
	b	.L2222
.L2220:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3608]
	add	w0, w0, w0, lsl 1
	lsr	w0, w0, 2
	bl	GetFreeBlockMaxEraseCount
	uxth	w6, w0
	add	w0, w21, 64
	cmp	w6, w0
	bcs	.L2232
	cmp	w21, 40
	bhi	.L2223
.L2232:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x3, [x1,3576]
	cbz	x3, .L2223
	mov	w26, 65535
	mov	x0, -6148914691236517206
	ldrh	w9, [x1,764]
	mov	w20, w26
	ldr	x7, [x1,1288]
	mov	w8, w26
	ldr	x25, [x1,1176]
	add	x0, x0, 1
	mov	w1, 0
	mov	w10, 6
.L2225:
	ldrh	w5, [x3]
	cmp	w5, w8
	beq	.L2227
	add	w1, w1, 1
	uxth	w1, w1
	cmp	w1, w9
	bhi	.L2223
	ldrh	w2, [x3,4]
	cbz	w2, .L2226
	sub	x4, x3, x7
	asr	x4, x4, 1
	mul	x4, x4, x0
	uxth	w3, w4
	and	x4, x4, 65535
	ldrh	w2, [x25,x4,lsl 1]
	cmp	w2, w21
	bls	.L2231
	cmp	w2, w26
	bcs	.L2226
	mov	w26, w2
	mov	w20, w3
.L2226:
	umull	x3, w5, w10
	add	x3, x7, x3
	b	.L2225
.L2231:
	mov	w20, w3
.L2227:
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2223
	ubfiz	x24, x20, 1, 16
	ldrh	w22, [x25,x24]
	cmp	w22, w21
	bls	.L2229
	str	x6, [x29,88]
	bl	GetFreeBlockMinEraseCount
	ldr	x6, [x29,88]
	cmp	w21, w0, uxth
	bcs	.L2229
	add	x0, x19, :lo12:.LANCHOR2
	str	w26, [x0,924]
.L2229:
	cmp	w22, w23
	bcs	.L2223
	add	w0, w22, 128
	cmp	w6, w0
	ble	.L2223
	add	w0, w22, 256
	cmp	w0, w23
	bcc	.L2230
	add	x0, x19, :lo12:.LANCHOR2
	add	w22, w22, 768
	ldr	w0, [x0,920]
	cmp	w22, w0
	bcs	.L2223
.L2230:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC140
	ldrh	w5, [x25,x24]
	add	x0, x0, :lo12:.LC140
	mov	w1, w20
	mov	w2, w23
	ldr	x4, [x19,1192]
	ldr	w3, [x19,920]
	ldrh	w4, [x4,x24]
	bl	printk
	mov	w0, 1
	str	w0, [x19,932]
	mov	w0, w20
.L2222:
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
	beq	.L2247
	adrp	x2, .LANCHOR2+1192
	ubfiz	x1, x0, 1, 16
	ldr	x2, [x2,#:lo12:.LANCHOR2+1192]
	strh	wzr, [x2,x1]
	bl	INSERT_FREE_LIST
.L2247:
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
	ldr	w1, [x19,1408]
	stp	x27, x28, [sp,80]
	cbnz	w1, .L2249
	mov	x20, x0
	add	x23, x19, 3712
	adrp	x22, .LANCHOR0
	mov	w24, 7
	add	x25, x19, 3600
.L2250:
	cmp	x20, x23
	bne	.L2251
	ldrh	w2, [x19,3608]
	ldr	w4, [x19,932]
	lsr	w3, w2, 1
	add	w1, w3, 1
	mul	w0, w2, w4
	add	w1, w1, w0, lsr 2
	ldr	w0, [x19,660]
	uxth	w1, w1
	cbz	w0, .L2252
	ldr	w0, [x19,924]
	cmp	w0, 39
	bhi	.L2252
	cmp	w0, 2
	mov	w1, 0
	bls	.L2253
	tbz	x2, 0, .L2280
	cbz	w4, .L2253
.L2280:
	mov	w1, w3
	b	.L2252
.L2251:
	ldrb	w0, [x20,8]
	mov	w1, 0
	cmp	w0, 1
	bne	.L2253
	ldrh	w0, [x19,776]
	cmp	w0, 1
	beq	.L2253
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbnz	w0, .L2253
	ldr	w2, [x19,660]
	ldrh	w0, [x19,3608]
	lsr	w1, w0, 3
	cbz	w2, .L2252
	ldr	w2, [x19,924]
	cmp	w2, 1
	bhi	.L2252
	mul	w1, w0, w24
	lsr	w1, w1, 3
.L2252:
	cbz	w1, .L2253
	sub	w1, w1, #1
	uxth	w1, w1
.L2253:
	ldrb	w2, [x20,8]
	mov	x0, x25
	bl	List_pop_index_node
	uxth	w28, w0
	ldrh	w0, [x19,3608]
	sub	w0, w0, #1
	strh	w0, [x19,3608]
	ldrh	w0, [x19,764]
	cmp	w0, w28
	bls	.L2250
	ubfiz	x26, x28, 1, 16
	ldr	x0, [x19,1192]
	ldrh	w0, [x0,x26]
	cbnz	w0, .L2250
	strh	w28, [x20]
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbz	w0, .L2305
	mov	x0, 0
	ldrh	w5, [x19,756]
	mov	w27, w0
	mov	x3, 56
	mov	w6, 65535
.L2257:
	cmp	w5, w0, uxth
	bls	.L2308
	ldr	x4, [x19,1024]
	madd	x1, x0, x3, x4
	str	xzr, [x1,16]
	str	xzr, [x1,8]
	add	x1, x20, x0, lsl 1
	ldrh	w1, [x1,16]
	cmp	w1, w6
	beq	.L2258
	umull	x2, w27, w3
	add	w27, w27, 1
	lsl	w1, w1, 10
	add	x2, x4, x2
	uxth	w27, w27
	str	w1, [x2,4]
.L2258:
	add	x0, x0, 1
	b	.L2257
.L2308:
	add	x0, x19, 3616
	cmp	x20, x0
	bne	.L2260
	ldr	w1, [x19,660]
	cbz	w1, .L2260
	ldr	x1, [x19,1176]
	ldrh	w1, [x1,x26]
	cmp	w1, 40
	bls	.L2260
	strb	wzr, [x19,3624]
.L2260:
	ldrb	w0, [x20,8]
	cbnz	w0, .L2261
	ldr	x0, [x19,1176]
	mov	w1, 2
	ldrh	w2, [x0,x26]
	cbz	w2, .L2306
	ldrh	w1, [x19,816]
	add	w1, w2, w1
.L2306:
	strh	w1, [x0,x26]
	mov	w1, 0
	ldr	w0, [x19,908]
	add	w0, w0, 1
	str	w0, [x19,908]
	mov	w0, w28
	bl	ftl_set_blk_mode
	b	.L2264
.L2261:
	ldr	x1, [x19,1176]
	ldrh	w0, [x1,x26]
	add	w0, w0, 1
	strh	w0, [x1,x26]
	ldr	w0, [x19,912]
	add	w0, w0, 1
	str	w0, [x19,912]
	mov	w0, w28
	bl	ftl_set_blk_mode.part.17
.L2264:
	ldr	x0, [x19,1176]
	ldr	w1, [x19,920]
	ldrh	w0, [x0,x26]
	cmp	w0, w1
	bls	.L2265
	str	w0, [x19,920]
.L2265:
	ldrh	w1, [x19,816]
	mov	x3, 56
	ldr	w2, [x19,908]
	ldr	w0, [x19,912]
	madd	w0, w1, w2, w0
	ldrh	w1, [x19,764]
	udiv	w0, w0, w1
	adrp	x1, .LANCHOR4+3996
	str	w0, [x1,#:lo12:.LANCHOR4+3996]
	ldr	x1, [x19,1168]
	ldr	w0, [x1,16]
	add	w0, w0, 1
	str	w0, [x1,16]
	mov	x0, 0
.L2266:
	cmp	w27, w0, uxth
	bls	.L2309
	ldr	x1, [x19,1024]
	madd	x1, x0, x3, x1
	add	x0, x0, 1
	ldr	w2, [x1,4]
	and	w2, w2, -1024
	str	w2, [x1,4]
	b	.L2266
.L2309:
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L2268
	ldrb	w0, [x20,8]
	cmp	w0, 1
	ldr	x0, [x19,1024]
	bne	.L2269
	mov	w1, 0
	b	.L2307
.L2269:
	mov	w1, 1
.L2307:
	mov	w2, w27
	bl	FlashEraseBlocks
.L2268:
	ldrb	w1, [x20,8]
	mov	w2, w27
	ldr	x0, [x19,1024]
	bl	FlashEraseBlocks
	mov	x1, 0
	mov	x4, 56
	mov	w2, w1
.L2270:
	cmp	w27, w1, uxth
	bls	.L2310
	mul	x0, x1, x4
	ldr	x3, [x19,1024]
	add	x5, x3, x0
	ldr	w3, [x3,x0]
	cmn	w3, #1
	bne	.L2271
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
.L2271:
	add	x1, x1, 1
	b	.L2270
.L2310:
	cbz	w2, .L2273
	mov	w0, w28
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2273:
	ldrb	w0, [x20,7]
	cbnz	w0, .L2274
.L2305:
	ldr	x0, [x19,1192]
	mov	w1, -1
	strh	w1, [x0,x26]
	b	.L2250
.L2274:
	add	x21, x21, :lo12:.LANCHOR2
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	ldrh	w1, [x21,824]
	strh	w28, [x20]
	mul	w0, w0, w1
	ldr	w1, [x21,876]
	str	w1, [x20,12]
	uxth	w0, w0
	add	w1, w1, 1
	strh	w0, [x20,4]
	str	w1, [x21,876]
	ldr	x1, [x21,1192]
	ldrh	w2, [x20]
	strh	w0, [x1,x2,lsl 1]
.L2249:
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
	adrp	x0, .LANCHOR4+4000
	mov	w1, 0
	adrp	x7, .LANCHOR2
	mov	w8, 24
	mov	w9, 1
	mov	w4, 4
	str	wzr, [x0,#:lo12:.LANCHOR4+4000]
	mov	w10, 56
.L2312:
	add	x2, x7, :lo12:.LANCHOR2
	adrp	x6, .LANCHOR2
	ldrh	w0, [x2,756]
	cmp	w1, w0
	bcs	.L2316
	umull	x3, w1, w8
	ldr	x5, [x2,1112]
	ldr	x6, [x2,1080]
	add	x0, x5, x3
	str	w9, [x0,16]
	ldrh	w0, [x2,834]
	mul	w0, w1, w0
	sdiv	w0, w0, w4
	add	x0, x6, x0, sxtw 2
	str	x0, [x5,x3]
	ldrh	w0, [x2,836]
	ldr	x6, [x2,1112]
	ldr	x11, [x2,1136]
	mul	w0, w1, w0
	add	x5, x6, x3
	ldr	x2, [x2,1032]
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
	b	.L2312
.L2316:
	mov	w7, 24
	mov	w4, 4
.L2314:
	add	x1, x6, :lo12:.LANCHOR2
	ldr	w2, [x1,1048]
	cmp	w0, w2
	bcs	.L2317
	umull	x3, w0, w7
	ldr	x5, [x1,1112]
	ldr	x8, [x1,1080]
	add	x2, x5, x3
	str	wzr, [x2,16]
	ldrh	w2, [x1,834]
	mul	w2, w0, w2
	sdiv	w2, w2, w4
	add	x2, x8, x2, sxtw 2
	str	x2, [x5,x3]
	ldrh	w2, [x1,836]
	ldr	x5, [x1,1112]
	ldr	x1, [x1,1136]
	mul	w2, w0, w2
	add	x3, x5, x3
	add	w0, w0, 1
	sdiv	w2, w2, w4
	uxth	w0, w0
	add	x1, x1, x2, sxtw 2
	str	x1, [x3,8]
	b	.L2314
.L2317:
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
	ldrh	w2, [x19,844]
	strh	w0, [x20,4016]
	mov	w0, -1
	str	w0, [x20,4028]
	lsl	w2, w2, 1
	ldr	x0, [x19,1216]
	str	xzr, [x20,4008]
	str	wzr, [x20,4020]
	str	wzr, [x20,4024]
	str	wzr, [x19,660]
	strh	wzr, [x19,1400]
	bl	ftl_memset
	ldrh	w2, [x19,766]
	mov	w1, 0
	ldr	x0, [x19,1176]
	lsl	w2, w2, 1
	bl	ftl_memset
	ldrh	w2, [x19,766]
	mov	w1, 0
	ldr	x0, [x19,1160]
	lsl	w2, w2, 1
	bl	ftl_memset
	add	x0, x19, 3528
	mov	w1, 0
	mov	w2, 48
	bl	ftl_memset
	mov	w1, 0
	mov	w2, 512
	add	x0, x20, 3472
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
	ldr	w8, [x2,1048]
	ldr	x5, [x2,1112]
.L2320:
	cmp	w3, w1
	bcs	.L2319
	umull	x4, w3, w7
	mov	w2, 0
	add	x4, x0, x4
.L2325:
	cmp	w2, w8
	bcs	.L2322
	umull	x6, w2, w9
	add	x10, x5, x6
	ldr	x11, [x5,x6]
	ldr	x6, [x4,8]
	cmp	x11, x6
	bne	.L2321
	str	wzr, [x10,16]
	b	.L2322
.L2321:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2325
.L2322:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2320
.L2319:
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
	ldr	w5, [x2,1048]
	ldr	x6, [x2,1112]
	mov	w2, 0
.L2327:
	cmp	w2, w1
	bcs	.L2333
	mov	w3, 0
.L2331:
	cmp	w3, w5
	bcs	.L2329
	umull	x4, w3, w7
	add	x4, x6, x4
	ldr	w10, [x4,16]
	cbnz	w10, .L2328
	umull	x3, w2, w9
	str	w8, [x4,16]
	add	x3, x0, x3
	ldr	x10, [x4]
	str	x10, [x3,8]
	ldr	x4, [x4,8]
	str	x4, [x3,16]
	b	.L2329
.L2328:
	add	w3, w3, 1
	uxth	w3, w3
	b	.L2331
.L2329:
	add	w2, w2, 1
	uxth	w2, w2
	b	.L2327
.L2333:
	ret
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	adrp	x1, .LANCHOR4+4032
	uxth	w0, w0
	ldrh	w2, [x1,#:lo12:.LANCHOR4+4032]
	adrp	x1, .LANCHOR2+984
	ldr	x3, [x1,#:lo12:.LANCHOR2+984]
	mov	x1, 0
.L2335:
	cmp	w2, w1, uxth
	bls	.L2339
	add	x1, x1, 1
	add	x4, x3, x1, lsl 1
	ldrh	w4, [x4,-2]
	cmp	w4, w0
	bne	.L2335
	mov	w0, 1
	b	.L2336
.L2339:
	mov	w0, 0
.L2336:
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
	ldrh	w8, [x4,4032]
	adrp	x4, .LANCHOR2
	add	x5, x4, :lo12:.LANCHOR2
	ldr	x7, [x5,984]
.L2341:
	uxth	w5, w6
	cmp	w5, w8
	bcs	.L2345
	add	x6, x6, 1
	add	x10, x7, x6, lsl 1
	ldrh	w10, [x10,-2]
	cmp	w10, w9
	bne	.L2341
.L2345:
	cmp	w5, w8
	bne	.L2343
	ubfiz	x5, x5, 1, 16
	strh	w0, [x7,x5]
	add	x0, x3, :lo12:.LANCHOR4
	ldrh	w5, [x0,4032]
	add	w5, w5, 1
	strh	w5, [x0,4032]
.L2343:
	add	x3, x3, :lo12:.LANCHOR4
	add	x4, x4, :lo12:.LANCHOR2
	mov	w0, 12
	ldrh	w5, [x3,4034]
	ldr	x6, [x4,992]
	umull	x5, w5, w0
	add	x6, x6, x5
	str	w1, [x6,4]
	ldr	x1, [x4,992]
	add	x0, x1, x5
	str	w2, [x0,8]
	str	w19, [x1,x5]
	ldrh	w0, [x3,4034]
	add	w0, w0, 1
	strh	w0, [x3,4034]
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
	ldrh	w2, [x19,828]
	strh	wzr, [x0,4032]
	strh	wzr, [x0,4034]
	lsl	w2, w2, 1
	ldr	x0, [x19,984]
	bl	ftl_memset
	ldrh	w3, [x19,828]
	mov	w2, 12
	ldr	x0, [x19,992]
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
	stp	x29, x30, [sp, -208]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	mov	x19, x0
	adrp	x0, .LANCHOR1
	str	w1, [x29,128]
	add	x1, x0, :lo12:.LANCHOR1
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	str	x0, [x29,120]
	ldrh	w26, [x1,3292]
	mov	w1, 65535
	cmp	w26, w1
	beq	.L2378
	cbnz	w26, .L2349
	b	.L2350
.L2378:
	mov	w26, 0
.L2349:
	adrp	x0, .LANCHOR2+824
	ldr	w1, [x29,128]
	ldrh	w0, [x0,#:lo12:.LANCHOR2+824]
	cmp	w1, w0
	bne	.L2351
.L2350:
	bl	FtlGcPageVarInit
.L2351:
	mov	w0, -1
	adrp	x25, .LANCHOR2
	str	w0, [x29,132]
	adrp	x0, .LC142
	add	x0, x0, :lo12:.LC142
	mov	w23, 0
	mov	x22, x25
	mov	x21, x25
	str	x0, [x29,112]
.L2352:
	ldrh	w0, [x19]
	mov	w20, 65535
	strb	wzr, [x19,8]
	cmp	w0, w20
	beq	.L2353
.L2354:
	str	w20, [x29,140]
.L2375:
	add	x0, x25, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w27, 0
	mov	w11, 56
	mov	w8, 4
	ldrh	w6, [x0,756]
	ldrh	w9, [x0,834]
	ldrh	w10, [x0,836]
	add	x6, x19, x6, lsl 1
.L2355:
	cmp	x2, x6
	beq	.L2395
	ldrh	w1, [x2,16]
	ldr	w0, [x29,140]
	cmp	w1, w0
	beq	.L2356
	add	x5, x22, :lo12:.LANCHOR2
	orr	w1, w26, w1, lsl 10
	umull	x0, w27, w11
	add	w4, w27, 1
	ldr	x7, [x5,1000]
	add	x7, x7, x0
	str	w1, [x7,4]
	ldr	x1, [x5,1000]
	ldr	x7, [x5,1080]
	add	x0, x1, x0
	ldr	x5, [x5,1136]
	mul	w1, w27, w9
	sdiv	w1, w1, w8
	add	x1, x7, x1, sxtw 2
	str	x1, [x0,8]
	mul	w1, w27, w10
	uxth	w27, w4
	sdiv	w1, w1, w8
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2356:
	add	x2, x2, 2
	b	.L2355
.L2395:
	add	x0, x22, :lo12:.LANCHOR2
	mov	w1, w27
	mov	w2, 0
	mov	x24, 0
	ldr	x0, [x0,1000]
	bl	FlashReadPages
.L2358:
	cmp	w27, w24, uxth
	bls	.L2396
	add	x20, x22, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x28, x24, x0
	ldr	x0, [x20,1000]
	add	x1, x0, x28
	ldr	w8, [x0,x28]
	ldr	w2, [x1,4]
	str	w2, [x29,136]
	ldr	x7, [x1,16]
	cbnz	w8, .L2359
	ldrh	w0, [x7]
	ldr	w1, [x29,140]
	cmp	w0, w1
	beq	.L2394
	ldr	w0, [x7,8]
	ldr	w1, [x20,1404]
	cmp	w0, w1
	bhi	.L2394
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w1, [x1,88]
	cbz	w1, .L2364
	add	x1, x29, 148
	mov	w2, w8
	str	x7, [x29,104]
	str	x8, [x29,96]
	bl	log2phys
	ldr	x7, [x29,104]
	ldr	w1, [x29,148]
	ldr	w0, [x7,12]
	cmp	w1, w0
	bne	.L2364
	cmn	w1, #1
	beq	.L2364
	str	w0, [x29,156]
	mov	w1, 1
	ldr	x0, [x20,1104]
	ldr	x8, [x29,96]
	str	x0, [x29,160]
	ldr	x0, [x20,1128]
	mov	w2, w8
	str	x0, [x29,168]
	add	x0, x29, 152
	bl	FlashReadPages
	ldr	x0, [x20,1000]
	ldrh	w2, [x20,830]
	ldr	x6, [x29,160]
	add	x5, x0, x28
	ldr	x7, [x29,104]
	lsl	w2, w2, 7
	mov	x0, 0
.L2366:
	cmp	w2, w0
	bls	.L2364
	lsl	x1, x0, 2
	ldr	x8, [x5,8]
	add	x0, x0, 1
	ldr	w8, [x8,x1]
	ldr	w1, [x6,x1]
	cmp	w8, w1
	beq	.L2366
	adrp	x0, .LC141
	ldrh	w1, [x19]
	ldr	w2, [x29,156]
	add	x0, x0, :lo12:.LC141
	bl	printk
.L2394:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,1192]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	adrp	x1, .LANCHOR4+3424
	mov	w0, -1
	strh	w0, [x19]
	strh	w0, [x1,#:lo12:.LANCHOR4+3424]
	b	.L2393
.L2364:
	ldr	w0, [x7,12]
	add	x24, x24, 1
	ldr	w1, [x29,136]
	ldr	w2, [x7,8]
	bl	FtlGcUpdatePage
	b	.L2358
.L2359:
	ldr	x0, [x29,112]
	ldrh	w1, [x19]
	ldr	w2, [x29,136]
	bl	printk
	ldr	w0, [x20,660]
	cbnz	w0, .L2369
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2370
.L2369:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,1176]
	ldrh	w0, [x0,x1,lsl 1]
	cmp	w0, 159
	bls	.L2371
.L2370:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
	ldr	w0, [x0,x28]
	cmn	w0, #1
	bne	.L2372
.L2371:
	add	x0, x21, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
	add	x5, x0, x28
	ldr	w0, [x5,4]
	str	w0, [x29,132]
.L2372:
	add	x0, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19]
	ldr	x0, [x0,1192]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x19]
	bl	INSERT_FREE_LIST
	mov	w0, -1
	strh	w0, [x19]
.L2393:
	bl	FtlGcPageVarInit
	mov	w26, 0
	b	.L2352
.L2396:
	ldr	w0, [x29,128]
	add	w23, w23, 1
	add	w3, w26, 1
	cmp	w23, w0
	uxth	w26, w3
	bcs	.L2374
.L2376:
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,824]
	cmp	w0, w26
	bhi	.L2375
	b	.L2353
.L2374:
	ldr	x0, [x29,120]
	ldr	w2, [x29,140]
	add	x1, x0, :lo12:.LANCHOR1
	ldrh	w0, [x1,3292]
	cmp	w0, w2
	beq	.L2376
	add	w0, w0, w23
	strh	w0, [x1,3292]
	add	x0, x22, :lo12:.LANCHOR2
	ldrh	w0, [x0,824]
	cmp	w0, w26
	bls	.L2376
	b	.L2377
.L2353:
	ldr	x0, [x29,120]
	mov	w1, -1
	strh	w26, [x19,2]
	mov	w2, 0
	add	x0, x0, :lo12:.LANCHOR1
	strb	wzr, [x19,6]
	strh	w1, [x0,3292]
	mov	x0, x19
	mov	w1, w26
	bl	ftl_sb_update_avl_pages
.L2377:
	ldr	w0, [x29,132]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x23, x24, [sp,48]
	ldp	x25, x26, [sp,64]
	ldp	x27, x28, [sp,80]
	ldp	x29, x30, [sp], 208
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
	ldrh	w0, [x19,952]
	cmp	w0, w20
	beq	.L2399
	ldrh	w0, [x19,954]
	cmp	w0, w20
	beq	.L2399
	ldrh	w0, [x19,956]
	cmp	w0, w20
	beq	.L2399
	ldrh	w0, [x19,958]
	cmp	w0, w20
	beq	.L2399
	adrp	x0, .LC143
	mov	w1, w20
	add	x0, x0, :lo12:.LC143
	bl	printk
	ldrh	w1, [x19,952]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2401
	strh	w20, [x19,952]
	b	.L2399
.L2401:
	ldrh	w1, [x19,954]
	cmp	w1, w0
	bne	.L2402
	strh	w20, [x19,954]
	b	.L2399
.L2402:
	ldrh	w1, [x19,956]
	cmp	w1, w0
	bne	.L2403
	strh	w20, [x19,956]
	b	.L2399
.L2403:
	ldrh	w1, [x19,958]
	cmp	w1, w0
	bne	.L2399
	strh	w20, [x19,958]
.L2399:
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
	ldrh	w0, [x19,952]
	cmp	w0, w20
	beq	.L2405
	ldrh	w0, [x19,954]
	cmp	w0, w20
	beq	.L2405
	ldrh	w0, [x19,956]
	cmp	w0, w20
	beq	.L2405
	ldrh	w0, [x19,958]
	cmp	w0, w20
	beq	.L2405
	adrp	x0, .LC143
	mov	w1, w20
	add	x0, x0, :lo12:.LC143
	bl	printk
	ldrh	w1, [x19,952]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2406
	strh	w20, [x19,952]
	b	.L2405
.L2406:
	ldrh	w1, [x19,954]
	cmp	w1, w0
	bne	.L2407
	strh	w20, [x19,954]
	b	.L2405
.L2407:
	ldrh	w1, [x19,956]
	cmp	w1, w0
	bne	.L2408
	strh	w20, [x19,956]
	b	.L2405
.L2408:
	ldrh	w1, [x19,958]
	cmp	w1, w0
	bne	.L2413
	strh	w20, [x19,958]
	b	.L2405
.L2413:
	mov	w21, -1
.L2405:
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
	ldrh	w1, [x22,966]
	adrp	x0, .LC144
	mov	w2, w20
	add	x0, x0, :lo12:.LC144
	bl	printk
	mov	w0, w21
	bl	FtlGcRefreshBlock
	ldr	w0, [x22,660]
	cbz	w0, .L2415
	ubfiz	x0, x21, 1, 16
	ldr	x2, [x22,1176]
	ldrh	w1, [x2,x0]
	cmp	w1, 39
	bls	.L2415
	sub	w1, w1, #40
	strh	w1, [x2,x0]
.L2415:
	add	x0, x19, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR4
	add	x1, x1, :lo12:.LANCHOR4
	add	x1, x1, 4040
	ldrh	w3, [x0,966]
	mov	x0, 0
.L2416:
	cmp	w3, w0, uxth
	bls	.L2422
	add	x0, x0, 1
	add	x2, x1, x0, lsl 1
	ldrh	w2, [x2,-2]
	cmp	w2, w20
	bne	.L2416
	b	.L2417
.L2422:
	cmp	w3, 15
	bhi	.L2417
	adrp	x1, .LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR4
	add	w0, w3, 1
	add	x1, x1, 4040
	strh	w0, [x19,966]
	strh	w20, [x1,w3,sxtw 1]
.L2417:
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
	ldrh	w1, [x0,966]
	cbz	w1, .L2424
	ldrh	w3, [x0,952]
	mov	w2, 65535
	cmp	w3, w2
	bne	.L2424
	ldrh	w2, [x0,970]
	cmp	w2, w1
	bcc	.L2425
	strh	wzr, [x0,970]
.L2425:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	add	x0, x0, 4040
	ldrh	w1, [x19,970]
	ldrh	w0, [x0,w1,sxtw 1]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	w0, [x19,970]
	add	w0, w0, 1
	strh	w0, [x19,970]
.L2424:
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
	ldrh	w0, [x19,966]
	stp	x21, x22, [sp,32]
	stp	x25, x26, [sp,64]
	mov	w21, 0
	cbz	w0, .L2431
	adrp	x24, .LANCHOR4
	adrp	x22, .LC145
	add	x22, x22, :lo12:.LC145
	add	x25, x24, :lo12:.LANCHOR4
.L2439:
	ldrh	w0, [x19,756]
	cmp	w0, w21
	bls	.L2440
	add	x0, x19, 784
	mov	w1, w23
	mov	w20, 0
	add	x27, x24, :lo12:.LANCHOR4
	ldrb	w0, [x0,w21,sxtw]
	bl	V2P_block
	uxth	w26, w0
.L2432:
	ldrh	w0, [x19,966]
	cmp	w0, w20
	bls	.L2441
	add	x0, x27, 4040
	ldrh	w0, [x0,w20,sxtw 1]
	cmp	w0, w26
	bne	.L2433
	mov	w1, w26
	mov	x0, x22
	bl	printk
	mov	w0, w26
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,966]
	mov	w3, w20
.L2434:
	cmp	w3, w2
	bcs	.L2442
	add	x4, x25, 4040
	add	w0, w3, 1
	ldrh	w1, [x4,w0,sxtw 1]
	strh	w1, [x4,w3,sxtw 1]
	uxth	w3, w0
	b	.L2434
.L2442:
	sub	w2, w2, #1
	strh	w2, [x19,966]
.L2433:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2432
.L2441:
	add	w21, w21, 1
	uxth	w21, w21
	b	.L2439
.L2440:
	bl	FtlGcReFreshBadBlk
.L2431:
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
	ldr	x1, [x2,1192]
	ldrh	w1, [x1,x0]
	cbnz	w1, .L2444
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	ldrh	w3, [x0,3424]
	cmp	w3, w19
	bne	.L2445
	mov	w1, -1
	strh	w1, [x0,3424]
	b	.L2446
.L2445:
	ldrh	w3, [x2,3616]
	mov	w0, w1
	cmp	w3, w19
	beq	.L2447
	ldrh	w3, [x2,3664]
	cmp	w3, w19
	beq	.L2447
	ldrh	w2, [x2,3712]
	cmp	w2, w19
	beq	.L2447
.L2446:
	add	x20, x20, :lo12:.LANCHOR2
	mov	w1, w19
	add	x0, x20, 3576
	bl	List_remove_node
	ldrh	w0, [x20,3592]
	sub	w0, w0, #1
	strh	w0, [x20,3592]
	mov	w0, w19
	bl	free_data_superblock
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	mov	w0, 1
	b	.L2447
.L2444:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, 0
.L2447:
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
	beq	.L2452
	adrp	x20, .LANCHOR2
	ubfiz	x21, x19, 1, 16
	add	x20, x20, :lo12:.LANCHOR2
	ldr	x0, [x20,1192]
	ldrh	w2, [x0,x21]
	cbnz	w2, .L2453
	adrp	x0, .LC146
	mov	w1, w19
	add	x0, x0, :lo12:.LC146
	add	x22, x20, 3600
	bl	printk
	ldr	x0, [x20,1192]
	mov	w1, 32
	strh	w1, [x0,x21]
	mov	x0, x22
	mov	w1, w19
	bl	test_node_in_list
	cbz	w0, .L2454
	mov	w1, w19
	mov	x0, x22
	bl	List_remove_node
	ldrh	w0, [x20,3608]
	sub	w0, w0, #1
	strh	w0, [x20,3608]
	mov	w0, w19
	bl	INSERT_DATA_LIST
	ldr	x2, [x20,1192]
	adrp	x0, .LC147
	add	x0, x0, :lo12:.LC147
	mov	w1, w19
	ldrh	w2, [x2,x21]
	bl	printk
.L2454:
	mov	w0, w19
	bl	FtlGcRefreshBlock
	b	.L2461
.L2453:
	sub	w2, w2, #1
	strh	w2, [x0,x21]
.L2452:
	adrp	x20, .LANCHOR4
	mov	w0, 65535
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w1, [x20,4016]
	cmp	w1, w0
	bne	.L2456
	strh	w19, [x20,4016]
.L2461:
	mov	w0, 0
	b	.L2455
.L2456:
	cmp	w1, w19
	mov	w0, 0
	beq	.L2455
	mov	w0, w1
	bl	update_vpc_list
	cmp	w0, wzr
	cset	w0, ne
	strh	w19, [x20,4016]
.L2455:
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
	beq	.L2624
	ldrh	w0, [x19,2]
	adrp	x20, .LANCHOR2
	str	w0, [x29,188]
	add	x0, x20, :lo12:.LANCHOR2
	ldrb	w28, [x19,6]
	ldr	w1, [x29,188]
	ldrh	w0, [x0,824]
	cmp	w0, w1
	bne	.L2465
	strh	wzr, [x19,4]
	b	.L2630
.L2465:
	ldrh	w0, [x19,16]
	mov	w1, 0
.L2466:
	cmp	w0, w2
	bne	.L2633
	add	w1, w1, 1
	uxth	w1, w1
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	b	.L2466
.L2633:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2468
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2469
	adrp	x0, .LANCHOR0
	mov	w23, w21
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w1, [x0,4044]
	cbnz	w1, .L2543
	add	x0, x0, 136
	ldrh	w23, [x0,w21,sxtw 1]
	b	.L2543
.L2468:
	mov	w1, 0
	bl	FtlGetLastWrittenPage
	cmn	w0, #1
	mov	w21, w0
	beq	.L2469
	mov	w23, w0
.L2543:
	add	x0, x20, :lo12:.LANCHOR2
	mov	x2, x19
	mov	w25, 0
	mov	w8, 65535
	mov	w9, 56
	mov	w10, 4
	ldrh	w4, [x0,756]
	ldrh	w7, [x0,836]
	add	x4, x19, x4, lsl 1
	b	.L2471
.L2469:
	strh	wzr, [x19,2]
.L2630:
	strb	wzr, [x19,6]
	b	.L2624
.L2473:
	ldrh	w1, [x2,16]
	cmp	w1, w8
	beq	.L2472
	add	x5, x20, :lo12:.LANCHOR2
	orr	w1, w23, w1, lsl 10
	umull	x0, w25, w9
	add	w3, w25, 1
	ldr	x6, [x5,1000]
	add	x6, x6, x0
	str	w1, [x6,4]
	ldr	x1, [x5,1000]
	ldr	x5, [x5,1136]
	add	x0, x1, x0
	mul	w1, w25, w7
	uxth	w25, w3
	str	xzr, [x0,8]
	sdiv	w1, w1, w10
	add	x1, x5, x1, sxtw 2
	str	x1, [x0,16]
.L2472:
	add	x2, x2, 2
.L2471:
	cmp	x2, x4
	bne	.L2473
	ldrb	w0, [x19,8]
	str	wzr, [x29,184]
	cmp	w0, 1
	bne	.L2474
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cmp	w0, wzr
	cset	w0, ne
	str	w0, [x29,184]
.L2474:
	add	x22, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,184]
	mov	w1, w25
	mov	x26, 0
	mov	w24, 65535
	adrp	x27, .LANCHOR4
	ldr	x0, [x22,1000]
	bl	FlashReadPages
	ldr	w0, [x22,880]
	adrp	x22, .LC148
	str	w0, [x29,176]
	add	x22, x22, :lo12:.LC148
	uxth	w0, w23
	str	w0, [x29,168]
.L2475:
	uxth	w2, w26
	cmp	w2, w25
	bcs	.L2482
	add	x6, x20, :lo12:.LANCHOR2
	mov	x0, 56
	mul	x0, x26, x0
	ldr	x4, [x6,1000]
	add	x1, x4, x0
	ldr	w0, [x4,x0]
	cbnz	w0, .L2476
	ldr	x7, [x1,16]
	ldr	w4, [x7,4]
	cmn	w4, #1
	beq	.L2477
	ldr	w1, [x6,880]
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
	cbz	w0, .L2477
	add	w4, w4, 1
	str	w4, [x6,880]
.L2477:
	ldr	w0, [x7]
	cmn	w0, #1
	bne	.L2478
.L2482:
	cmp	w2, w25
	bne	.L2625
	add	x0, x20, :lo12:.LANCHOR2
	add	w21, w21, 1
	uxth	w21, w21
	ldr	x0, [x0,1000]
	ldr	w0, [x0,4]
	b	.L2626
.L2476:
	ldr	w1, [x1,4]
	mov	x0, x22
	bl	printk
	ldr	w24, [x29,168]
	add	x0, x27, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,4074]
.L2478:
	add	x26, x26, 1
	b	.L2475
.L2625:
	add	x0, x20, :lo12:.LANCHOR2
	mov	w1, 56
	uxth	w21, w21
	umull	x2, w2, w1
	ldr	x0, [x0,1000]
	add	x2, x0, x2
	ldr	w0, [x2,4]
.L2626:
	lsr	x0, x0, 10
	bl	P2V_plane
	uxth	w26, w0
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2484
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w2, [x0,4044]
	cbnz	w2, .L2484
	add	x0, x0, 136
	ldrh	w21, [x0,w21,sxtw 1]
.L2484:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,824]
	cmp	w0, w21
	bne	.L2485
	strh	w21, [x19,2]
	strb	wzr, [x19,6]
	strh	wzr, [x19,4]
.L2485:
	uxth	w0, w28
	str	w0, [x29,152]
	ldr	w0, [x29,188]
	cmp	w21, w0
	bne	.L2486
	ldr	w0, [x29,152]
	cmp	w26, w0
	beq	.L2631
.L2486:
	ldr	w0, [x29,176]
	adrp	x25, .LANCHOR4
	sub	w22, w0, #1
	mov	w0, 65535
	cmp	w24, w0
	bne	.L2487
	cbnz	w1, .L2488
.L2487:
	add	x0, x25, :lo12:.LANCHOR4
	uxth	w5, w23
	ldr	w1, [x0,4028]
	cmn	w1, #1
	bne	.L2489
	str	w22, [x0,4028]
.L2489:
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w24, [x29,188]
	ldr	w3, [x0,4028]
	ldr	w0, [x29,188]
	add	w0, w0, 7
	cmp	w0, w23, uxth
	bge	.L2490
	sub	w24, w5, #7
	uxth	w24, w24
.L2490:
	mov	w28, -1
	adrp	x6, .LANCHOR0
	mov	w27, w28
	mov	w4, 65535
	mov	w7, 56
	add	x8, x6, :lo12:.LANCHOR0
.L2491:
	cmp	w24, w5
	bhi	.L2504
	add	x0, x20, :lo12:.LANCHOR2
	mov	w23, 0
	ldrh	w2, [x0,756]
	mov	x0, x19
	add	x2, x19, x2, lsl 1
.L2505:
	cmp	x0, x2
	beq	.L2634
	ldrh	w1, [x0,16]
	cmp	w1, w4
	beq	.L2492
	add	x10, x20, :lo12:.LANCHOR2
	orr	w1, w24, w1, lsl 10
	umull	x9, w23, w7
	add	w23, w23, 1
	ldr	x10, [x10,1000]
	uxth	w23, w23
	add	x9, x10, x9
	str	w1, [x9,4]
.L2492:
	add	x0, x0, 2
	b	.L2505
.L2634:
	add	x9, x20, :lo12:.LANCHOR2
	ldr	w2, [x29,184]
	mov	w1, w23
	str	x8, [x29,120]
	str	x6, [x29,128]
	ldr	x0, [x9,1000]
	str	x7, [x29,136]
	str	x4, [x29,144]
	str	x3, [x29,160]
	str	x5, [x29,168]
	str	x9, [x29,176]
	bl	FlashReadPages
	ldr	x6, [x29,128]
	mov	w1, 0
	ldr	x9, [x29,176]
	sxtw	x11, w24
	add	x0, x6, :lo12:.LANCHOR0
	ldr	x5, [x29,168]
	ldr	x3, [x29,160]
	ldr	x4, [x29,144]
	ldrb	w10, [x0,4044]
	ldr	x0, [x9,1000]
	ldr	x7, [x29,136]
	ldr	x8, [x29,120]
	add	x0, x0, 16
.L2494:
	cmp	w1, w23
	beq	.L2635
	ldr	w2, [x0,-16]
	cbnz	w2, .L2495
	ldr	x2, [x0]
	ldrh	w9, [x2]
	cmp	w9, w4
	beq	.L2496
	ldr	w9, [x2,4]
	cmn	w9, #1
	beq	.L2496
	add	x2, x25, :lo12:.LANCHOR4
	cmn	w28, #1
	ldr	w27, [x2,4028]
	str	w9, [x2,4028]
	bne	.L2496
	add	x2, x8, 648
	ldrh	w2, [x2,x11,lsl 1]
	cmp	w2, w4
	bne	.L2497
	cbz	w10, .L2496
.L2497:
	cmp	w27, w22
	csinv	w28, w27, wzr, ne
	b	.L2496
.L2495:
	add	x0, x25, :lo12:.LANCHOR4
	ldrh	w1, [x19]
	strh	w1, [x0,4074]
	ldrb	w1, [x19,8]
	cbnz	w1, .L2488
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	add	x1, x1, 648
	ldrh	w2, [x1,w24,sxtw 1]
	mov	w1, 65535
	cmp	w2, w1
	bne	.L2499
	cmn	w28, #1
	beq	.L2500
	str	w28, [x0,4028]
	b	.L2488
.L2500:
	cmp	w3, w22
	beq	.L2501
	str	w3, [x0,4028]
	b	.L2488
.L2501:
	ldr	w1, [x0,4028]
	b	.L2632
.L2499:
	cmp	w27, w22
	beq	.L2502
	cmn	w27, #1
	beq	.L2488
	str	w27, [x0,4028]
	b	.L2488
.L2502:
	ldr	w1, [x0,4028]
	cmp	w1, w22
	beq	.L2488
.L2632:
	sub	w1, w1, #1
	b	.L2627
.L2496:
	add	w1, w1, 1
	add	x0, x0, 56
	uxth	w1, w1
	b	.L2494
.L2635:
	add	w24, w24, 1
	uxth	w24, w24
	b	.L2491
.L2504:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, -1
.L2627:
	str	w1, [x0,4028]
.L2488:
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, 1
	ldr	w23, [x29,188]
	strh	w1, [x0,4076]
	adrp	x0, .LC150
	add	x0, x0, :lo12:.LC150
	str	x0, [x29,128]
	adrp	x0, .LC149
	add	x0, x0, :lo12:.LC149
	str	x0, [x29,120]
.L2506:
	add	x0, x20, :lo12:.LANCHOR2
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	str	wzr, [x29,176]
	mov	w7, 65535
	mov	w8, 56
	ldrh	w3, [x0,756]
	ldrb	w6, [x1,4044]
	mov	x1, x19
	add	x3, x19, x3, lsl 1
.L2507:
	cmp	x1, x3
	beq	.L2636
	ldrh	w2, [x1,16]
	cmp	w2, w7
	beq	.L2508
	ldr	w0, [x29,176]
	add	x5, x20, :lo12:.LANCHOR2
	orr	w2, w23, w2, lsl 10
	ldr	x4, [x5,1000]
	umull	x0, w0, w8
	add	x4, x4, x0
	str	w2, [x4,4]
	ldrb	w2, [x19,8]
	cmp	w2, 1
	bne	.L2509
	cbz	w6, .L2509
	ldr	x2, [x5,1000]
	add	x0, x2, x0
	ldr	w2, [x0,4]
	orr	w2, w2, -2147483648
	str	w2, [x0,4]
.L2509:
	ldr	w0, [x29,176]
	add	w0, w0, 1
	uxth	w0, w0
	str	w0, [x29,176]
.L2508:
	add	x1, x1, 2
	b	.L2507
.L2636:
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w1, [x29,176]
	ldr	w2, [x29,184]
	ldr	x0, [x0,1000]
	bl	FlashReadPages
	str	xzr, [x29,160]
.L2511:
	ldrh	w0, [x29,160]
	ldr	w1, [x29,176]
	cmp	w1, w0, uxth
	bls	.L2637
	ldr	x0, [x29,160]
	mov	x24, 56
	mul	x24, x0, x24
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x28, [x0,1000]
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
	bcc	.L2512
	ldr	w2, [x29,152]
	cmp	w0, w2
	bcs	.L2547
	ldr	w2, [x29,188]
	cmp	w23, w2
	beq	.L2512
.L2547:
	cmp	w0, w26
	bne	.L2548
	cmp	w23, w21
	beq	.L2514
.L2548:
	ldr	w0, [x28,x24]
	cmn	w0, #1
	beq	.L2516
	ldr	x28, [x27,16]
	mov	w0, 61589
	ldrh	w1, [x28]
	cmp	w1, w0
	beq	.L2517
	ldrh	w0, [x19]
	b	.L2628
.L2517:
	ldr	w22, [x28,4]
	cmn	w22, #1
	beq	.L2518
	add	x27, x20, :lo12:.LANCHOR2
	mov	w0, w22
	ldr	w1, [x27,880]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2518
	add	w0, w22, 1
	str	w0, [x27,880]
.L2518:
	ldr	w27, [x28,8]
	add	x1, x29, 200
	ldr	w0, [x28,12]
	mov	w2, 0
	str	w0, [x29,196]
	mov	w0, w27
	bl	log2phys
	ldr	w28, [x29,196]
	add	x0, x25, :lo12:.LANCHOR4
	str	x0, [x29,136]
	ldr	w1, [x0,4028]
	cmn	w1, #1
	beq	.L2519
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbz	w0, .L2519
	cmn	w28, #1
	beq	.L2520
	add	x3, x20, :lo12:.LANCHOR2
	mov	w2, 0
	str	x3, [x29,112]
	ldr	x0, [x3,1000]
	add	x0, x0, x24
	str	w28, [x0,4]
	ldr	x1, [x0,16]
	ldr	x0, [x3,1000]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	ldr	x3, [x29,112]
	ldr	x0, [x29,168]
	ldr	x2, [x3,1000]
	ldr	w0, [x0,4]
	str	w0, [x29,144]
	add	x4, x2, x24
	ldr	w0, [x2,x24]
	cmn	w0, #1
	bne	.L2521
	b	.L2522
.L2520:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	bne	.L2512
	mov	w0, w27
	add	x1, x29, 196
	mov	w2, 1
	bl	log2phys
	b	.L2512
.L2521:
	ldr	x0, [x29,168]
	ldr	w28, [x0,8]
	cmp	w28, w27
	bne	.L2522
	ldr	x0, [x29,136]
	ldr	w1, [x29,144]
	str	x2, [x29,96]
	ldr	w0, [x0,4028]
	str	x3, [x29,104]
	str	x4, [x29,112]
	bl	ftl_cmp_data_ver
	ldr	x4, [x29,112]
	ldr	x3, [x29,104]
	ldr	x2, [x29,96]
	cbz	w0, .L2522
	ldr	w0, [x29,200]
	ldr	w1, [x29,204]
	cmp	w0, w1
	beq	.L2527
	ldr	w1, [x29,196]
	cmp	w0, w1
	beq	.L2522
	cmn	w0, #1
	beq	.L2525
	str	w0, [x4,4]
	mov	w2, 0
	ldr	x1, [x4,16]
	ldr	x0, [x3,1000]
	str	x1, [x29,168]
	mov	w1, 1
	add	x0, x0, x24
	bl	FlashReadPages
	b	.L2526
.L2525:
	str	w0, [x2,x24]
.L2526:
	adrp	x0, .LANCHOR2
	add	x0, x0, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
	ldr	w0, [x0,x24]
	cmn	w0, #1
	beq	.L2527
	ldr	x0, [x29,168]
	ldr	w24, [x0,4]
	add	x0, x25, :lo12:.LANCHOR4
	mov	w1, w24
	ldr	w0, [x0,4028]
	bl	ftl_cmp_data_ver
	cbz	w0, .L2527
	ldr	w0, [x29,144]
	mov	w1, w24
	bl	ftl_cmp_data_ver
	cbz	w0, .L2522
.L2527:
	ldr	w1, [x29,196]
	mov	w0, w28
	bl	FtlReUsePrevPpa
.L2522:
	mov	w0, -1
	str	w0, [x29,196]
	b	.L2529
.L2519:
	ldr	w0, [x29,204]
	ldr	w1, [x29,200]
	cmp	w1, w0
	beq	.L2529
	add	x0, x20, :lo12:.LANCHOR2
	ldr	w0, [x0,772]
	cmp	w0, w28, lsr 10
	bhi	.L2549
	cmn	w28, #1
	bne	.L2512
.L2549:
	mov	w0, w27
	add	x1, x29, 204
	mov	w2, 1
	bl	log2phys
	ldr	w28, [x29,200]
	cmn	w28, #1
	beq	.L2529
	ldr	w0, [x29,196]
	cmp	w28, w0
	beq	.L2529
	lsr	x0, x28, 10
	bl	P2V_block_in_plane
	uxth	w0, w0
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w2, [x1,3616]
	cmp	w2, w0
	beq	.L2533
	ldrh	w2, [x1,3664]
	cmp	w2, w0
	beq	.L2533
	ldrh	w1, [x1,3712]
	cmp	w1, w0
	bne	.L2529
.L2533:
	adrp	x0, .LANCHOR2
	mov	w1, 1
	add	x24, x0, :lo12:.LANCHOR2
	mov	w2, 0
	ldr	x0, [x24,1000]
	str	w28, [x0,4]
	ldr	x3, [x0,16]
	ldr	x0, [x24,1000]
	str	x3, [x29,168]
	bl	FlashReadPages
	ldr	x0, [x24,1000]
	ldr	x3, [x29,168]
	ldr	w0, [x0]
	ldr	w1, [x3,4]
	cmn	w0, #1
	beq	.L2529
	mov	w0, w22
	bl	ftl_cmp_data_ver
	cbnz	w0, .L2529
	mov	w0, w27
	add	x1, x29, 200
	mov	w2, 1
	bl	log2phys
.L2529:
	ldr	w0, [x29,196]
	cmn	w0, #1
	beq	.L2512
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	add	x2, x20, :lo12:.LANCHOR2
	ubfiz	x3, x1, 1, 16
	ldr	x2, [x2,1192]
	ldrh	w2, [x2,x3]
	cbz	w2, .L2534
.L2628:
	bl	decrement_vpc_count
	b	.L2512
.L2534:
	ldr	x0, [x29,120]
	bl	printk
	b	.L2512
.L2516:
	add	x24, x25, :lo12:.LANCHOR4
	ldrh	w0, [x19]
	mov	w2, w22
	strh	w0, [x24,4074]
	ldr	x0, [x29,128]
	bl	printk
	ldr	w0, [x24,4080]
	cmp	w0, 31
	bhi	.L2535
	add	x1, x24, 4088
	ldr	w2, [x29,204]
	str	w2, [x1,w0,uxtw 2]
	add	w0, w0, 1
	str	w0, [x24,4080]
.L2535:
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	add	x0, x25, :lo12:.LANCHOR4
	ldr	w1, [x0,4028]
	cmn	w1, #1
	beq	.L2629
	cmp	w1, w22
	bls	.L2512
.L2629:
	str	w22, [x0,4028]
.L2512:
	ldr	x0, [x29,160]
	add	x0, x0, 1
	str	x0, [x29,160]
	b	.L2511
.L2637:
	ldrb	w0, [x19,8]
	add	w23, w23, 1
	cmp	w0, 1
	uxth	w23, w23
	bne	.L2538
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L2538
	cmp	w21, w23
	bne	.L2538
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,826]
	cmp	w0, w23
	beq	.L2514
.L2538:
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w1, [x0,824]
	cmp	w23, w1
	bne	.L2506
	ldrh	w2, [x0,756]
	mov	w3, 65535
	strh	w23, [x19,2]
	mov	x0, 0
	strh	wzr, [x19,4]
.L2540:
	uxth	w1, w0
	cmp	w1, w2
	bcs	.L2624
	add	x0, x0, 1
	add	x4, x19, x0, lsl 1
	ldrh	w4, [x4,14]
	cmp	w4, w3
	beq	.L2540
	strb	w1, [x19,6]
	b	.L2624
.L2514:
	strb	w26, [x19,6]
	strh	w21, [x19,2]
.L2631:
	mov	x0, x19
	mov	w1, w21
	mov	w2, w26
	bl	ftl_sb_update_avl_pages
.L2624:
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
	stp	x29, x30, [sp, -48]!
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	ldrh	w1, [x0,4]
	cbz	w1, .L2638
	ldrh	w1, [x0]
	mov	w20, 65535
	cmp	w1, w20
	beq	.L2638
	mov	x19, x0
	ldrb	w0, [x0,6]
	adrp	x21, .LANCHOR0
	adrp	x22, .LANCHOR2
	add	x0, x0, 8
	add	x21, x21, :lo12:.LANCHOR0
	ldrh	w0, [x19,x0,lsl 1]
.L2642:
	cmp	w0, w20
	bne	.L2649
.L2644:
	ldrb	w0, [x19,6]
	add	x1, x22, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,756]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2643
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2643:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2642
.L2649:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2645
	ldrb	w0, [x21,4044]
	cbnz	w0, .L2645
	add	x0, x21, 648
	ldrh	w2, [x19,2]
	ldrh	w0, [x0,w2,sxtw 1]
	cmp	w0, w20
	bne	.L2645
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	ldrh	w1, [x19,4]
	cbnz	w1, .L2644
	ldrh	w0, [x19,2]
	strb	w1, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
	b	.L2638
.L2645:
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L2638
	cmp	w1, 1
	bne	.L2638
	adrp	x0, .LANCHOR2
	ldrh	w2, [x19,2]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,826]
	cmp	w2, w1
	bcc	.L2638
	ldrh	w1, [x19]
	ldr	x3, [x0,1192]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w2, [x3,x1]
	sub	w2, w2, w4
	strh	w2, [x3,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x0,824]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2638:
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
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	mov	x19, x0
	strb	wzr, [x0,10]
	ldrb	w0, [x0,6]
	adrp	x20, .LANCHOR0
	mov	w22, 65535
	adrp	x23, .LANCHOR2
	add	x0, x0, 8
	add	x20, x20, :lo12:.LANCHOR0
	ldrh	w0, [x19,x0,lsl 1]
.L2651:
	cmp	w0, w22
	adrp	x21, .LANCHOR2
	bne	.L2666
.L2652:
	ldrb	w0, [x19,6]
	add	x1, x23, :lo12:.LANCHOR2
	add	w0, w0, 1
	ldrh	w1, [x1,756]
	uxtb	w0, w0
	strb	w0, [x19,6]
	cmp	w1, w0
	bne	.L2653
	ldrh	w0, [x19,2]
	strb	wzr, [x19,6]
	add	w0, w0, 1
	strh	w0, [x19,2]
.L2653:
	ldrb	w0, [x19,6]
	add	x0, x0, 8
	ldrh	w0, [x19,x0,lsl 1]
	b	.L2651
.L2666:
	ldrb	w1, [x19,8]
	cmp	w1, 1
	bne	.L2654
	ldrb	w1, [x20,4044]
	cbnz	w1, .L2654
	add	x1, x20, 648
	ldrh	w2, [x19,2]
	ldrh	w1, [x1,w2,sxtw 1]
	cmp	w1, w22
	bne	.L2654
	ldrh	w0, [x19,4]
	sub	w0, w0, #1
	strh	w0, [x19,4]
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2652
.L2654:
	ldrh	w1, [x19,4]
	mov	w23, 65535
	ldrh	w22, [x19,2]
	adrp	x20, .LANCHOR0
	sub	w1, w1, #1
	mov	w24, w23
	orr	w22, w22, w0, lsl 10
	strh	w1, [x19,4]
	add	x20, x20, :lo12:.LANCHOR0
.L2655:
	add	x0, x21, :lo12:.LANCHOR2
	ldrb	w1, [x19,6]
	ldrh	w2, [x0,756]
.L2657:
	add	w1, w1, 1
	uxtb	w1, w1
	cmp	w1, w2
	bne	.L2656
	ldrh	w1, [x19,2]
	add	w1, w1, 1
	strh	w1, [x19,2]
	mov	w1, 0
.L2656:
	add	x0, x19, x1, sxtw 1
	ldrh	w0, [x0,16]
	cmp	w0, w23
	beq	.L2657
	ldrb	w0, [x19,8]
	strb	w1, [x19,6]
	cmp	w0, 1
	bne	.L2662
	ldrb	w0, [x20,4044]
	cbnz	w0, .L2659
	add	x1, x20, 648
	ldrh	w0, [x19,2]
	ldrh	w0, [x1,w0,sxtw 1]
	cmp	w0, w24
	bne	.L2659
	ldrh	w1, [x19,4]
	cbz	w1, .L2659
	ldrh	w0, [x19]
	sub	w1, w1, #1
	strh	w1, [x19,4]
	bl	decrement_vpc_count
	b	.L2655
.L2659:
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrb	w0, [x1,4044]
	cbz	w0, .L2662
	add	x2, x21, :lo12:.LANCHOR2
	ldrh	w1, [x19,2]
	ldrh	w0, [x2,826]
	cmp	w1, w0
	bcc	.L2662
	ldrh	w1, [x19]
	ldr	x0, [x2,1192]
	lsl	x1, x1, 1
	ldrh	w4, [x19,4]
	ldrh	w3, [x0,x1]
	sub	w3, w3, w4
	strh	w3, [x0,x1]
	strh	wzr, [x19,4]
	ldrh	w0, [x2,824]
	strh	w0, [x19,2]
	strb	wzr, [x19,6]
.L2662:
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
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	x25, [sp,64]
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	ldr	w0, [x20,1408]
	cbnz	w0, .L2667
	ldrh	w2, [x20,3620]
	cbz	w2, .L2669
	ldrb	w1, [x20,3624]
	cbnz	w1, .L2669
	ldrb	w1, [x20,3623]
	ldrh	w3, [x20,824]
	mul	w1, w1, w3
	cmp	w2, w1
	beq	.L2669
	ldrb	w23, [x20,3626]
	ldr	w22, [x20,1404]
	ldrh	w24, [x20,756]
	cbnz	w23, .L2667
	sub	w22, w22, #1
	add	x1, x29, 84
	mov	w0, w22
	mov	w2, w23
	bl	log2phys
	ldr	x21, [x20,1120]
	ldr	w0, [x29,84]
	ldr	x1, [x20,1056]
	str	w0, [x29,92]
	cmn	w0, #1
	str	w22, [x29,112]
	str	x1, [x29,96]
	str	x21, [x29,104]
	str	w23, [x21,4]
	beq	.L2671
	add	x0, x29, 88
	mov	w1, 1
	mov	w2, w23
	bl	FlashReadPages
	b	.L2672
.L2671:
	ldr	x0, [x20,1056]
	mov	w1, 255
	ldrh	w2, [x20,834]
	bl	ftl_memset
.L2672:
	add	x25, x19, :lo12:.LANCHOR2
	mov	w0, -3947
	lsl	w24, w24, 2
	strh	w0, [x21]
	mov	w23, 0
	add	x20, x25, 3616
.L2673:
	cmp	w23, w24
	beq	.L2674
	ldrh	w0, [x20,4]
	cbz	w0, .L2674
	ldr	w0, [x29,92]
	add	w23, w23, 1
	str	w0, [x21,12]
	ldrh	w0, [x20]
	strh	w0, [x21,2]
	mov	x0, x20
	str	w22, [x21,8]
	bl	get_new_active_ppa
	str	w0, [x29,92]
	ldr	w1, [x25,880]
	mov	w2, 0
	str	w1, [x21,4]
	mov	w3, w2
	add	w1, w1, 1
	add	x0, x29, 88
	cmn	w1, #1
	csel	w1, w1, wzr, ne
	str	w1, [x25,880]
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	b	.L2673
.L2674:
	add	x19, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x19,3626]
	b	.L2667
.L2669:
	add	x19, x19, :lo12:.LANCHOR2
	strb	wzr, [x19,3626]
.L2667:
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
.L2685:
	add	x1, x20, :lo12:.LANCHOR2
	ldrh	w0, [x1,862]
	cmp	w0, w19
	bls	.L2688
	ldr	x1, [x1,1272]
	ubfiz	x0, x19, 4, 16
	add	x0, x1, x0
	ldr	w0, [x0,4]
	tbz	w0, #31, .L2686
	mov	w0, w19
	bl	flush_l2p_region
.L2686:
	add	w19, w19, 1
	uxth	w19, w19
	b	.L2685
.L2688:
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
	ldr	w1, [x1,1408]
	cbnz	w1, .L2689
	adrp	x2, .LANCHOR0+4044
	ldrb	w21, [x2,#:lo12:.LANCHOR0+4044]
	cbz	w21, .L2704
	ldrb	w21, [x0,8]
	cmp	w21, 1
	bne	.L2700
	ldrh	w23, [x0,4]
	b	.L2691
.L2700:
	mov	w21, w1
.L2704:
	mov	w23, 12
.L2691:
	mov	x19, x0
	mov	w0, -1
	str	w0, [x29,96]
	add	x0, x20, :lo12:.LANCHOR2
	mov	w2, 61589
	ldr	x22, [x0,1120]
	ldr	x1, [x0,1056]
	str	x1, [x29,80]
	mov	w1, -3
	str	x22, [x29,88]
	str	w1, [x22,8]
	mov	w1, -2
	str	w1, [x22,12]
	ldrh	w1, [x19]
	strh	w1, [x22,2]
	strh	wzr, [x22]
	ldr	x1, [x0,1056]
	str	w2, [x1]
	ldr	x1, [x0,1056]
	mov	w0, 22136
	movk	w0, 0x1234, lsl 16
	str	w0, [x1,4]
.L2692:
	sub	w23, w23, #1
	cmn	w23, #1
	beq	.L2695
	ldrh	w0, [x19,4]
	cbnz	w0, .L2693
.L2695:
	add	x20, x20, :lo12:.LANCHOR2
	ldrh	w0, [x19]
	ldrh	w3, [x19,4]
	lsl	x0, x0, 1
	ldr	x2, [x20,1192]
	ldrh	w1, [x2,x0]
	sub	w1, w1, w3
	strh	w1, [x2,x0]
	strb	wzr, [x19,6]
	ldrh	w0, [x20,824]
	strh	w0, [x19,2]
	strh	wzr, [x19,4]
	b	.L2689
.L2693:
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x29,76]
	cmn	w0, #1
	beq	.L2695
	add	x1, x20, :lo12:.LANCHOR2
	mov	w2, w21
	mov	w3, 0
	ldr	w0, [x1,880]
	str	w0, [x22,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x1,880]
	add	x0, x29, 72
	mov	w1, 1
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	b	.L2692
.L2689:
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
	add	x1, x1, 96
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
	ldrh	w2, [x0,766]
	ldr	x0, [x0,1184]
	lsl	w2, w2, 1
	bl	ftl_memset
.L2706:
	add	x19, x21, :lo12:.LANCHOR2
	ldr	w0, [x19,1404]
	cmp	w20, w0
	bcs	.L2722
	mov	w0, w20
	add	x1, x29, 76
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,76]
	cmn	w0, #1
	beq	.L2707
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x19,1184]
	ldrh	w1, [x2,x0]
	add	w1, w1, 1
	strh	w1, [x2,x0]
.L2707:
	add	w20, w20, 1
	b	.L2706
.L2722:
	adrp	x22, .LC151
	mov	w20, 0
	mov	w23, 65535
	add	x22, x22, :lo12:.LC151
.L2709:
	ldrh	w0, [x19,764]
	cmp	w0, w20
	bls	.L2723
	ubfiz	x21, x20, 1, 16
	ldr	x0, [x19,1192]
	ldrh	w2, [x0,x21]
	ldr	x0, [x19,1184]
	ldrh	w3, [x0,x21]
	cmp	w2, w3
	beq	.L2712
	cmp	w2, w23
	beq	.L2712
	mov	x0, x22
	mov	w1, w20
	bl	printk
	ldrh	w0, [x19,3616]
	cmp	w0, w20
	beq	.L2712
	ldrh	w0, [x19,3712]
	cmp	w0, w20
	beq	.L2712
	ldrh	w0, [x19,3664]
	cmp	w0, w20
	beq	.L2712
	ldr	x0, [x19,1192]
	ldrh	w1, [x0,x21]
	cbnz	w1, .L2714
	ldr	x1, [x19,1184]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	b	.L2712
.L2714:
	ldr	x1, [x19,1184]
	ldrh	w1, [x1,x21]
	strh	w1, [x0,x21]
	mov	w0, w20
	bl	update_vpc_list
.L2712:
	add	w20, w20, 1
	uxth	w20, w20
	b	.L2709
.L2723:
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
	ldr	w1, [x2,1408]
	ldrh	w22, [x0]
	cbnz	w1, .L2725
	mov	x21, x0
	mov	w0, 65535
	cmp	w22, w0
	beq	.L2726
	ubfiz	x1, x22, 1, 16
	ldr	x0, [x2,1192]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2727
	mov	w0, w22
	bl	INSERT_DATA_LIST
	b	.L2726
.L2727:
	mov	w0, w22
	bl	INSERT_FREE_LIST
.L2726:
	add	x1, x19, :lo12:.LANCHOR2
	strb	wzr, [x21,8]
	add	x0, x1, 3664
	cmp	x21, x0
	beq	.L2728
	ldrh	w2, [x1,776]
	cmp	w2, 1
	beq	.L2728
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2729
.L2728:
	mov	w0, 1
	strb	w0, [x21,8]
	b	.L2730
.L2729:
	add	x0, x1, 3616
	cmp	x21, x0
	bne	.L2730
	cmp	w2, 3
	beq	.L2732
	ldr	w0, [x1,872]
	cmp	w0, 1
	bne	.L2733
.L2732:
	add	x1, x19, :lo12:.LANCHOR2
	mov	w0, 1
	strb	w0, [x1,3624]
.L2733:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x1,660]
	cbz	w0, .L2730
	ldr	w0, [x1,924]
	cmp	w0, 39
	bhi	.L2730
	mov	w0, 1
	strb	w0, [x1,3624]
.L2730:
	adrp	x1, .LANCHOR4
	add	x0, x1, :lo12:.LANCHOR4
	mov	x20, x1
	ldrh	w2, [x0,4016]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L2735
	cmp	w22, w2
	bne	.L2736
	add	x19, x19, :lo12:.LANCHOR2
	ubfiz	x1, x2, 1, 16
	ldr	x0, [x19,1192]
	ldrh	w0, [x0,x1]
	cbz	w0, .L2737
.L2736:
	mov	w0, w2
	bl	update_vpc_list
.L2737:
	add	x1, x20, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x1,4016]
.L2735:
	mov	x0, x21
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2725:
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
	add	x3, x1, :lo12:.LANCHOR4
	add	x29, sp, 0
	add	x2, x3, 3472
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	adrp	x19, .LANCHOR2
	ldr	w0, [x2,80]
	cbz	w0, .L2751
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w4, [x2,84]
	ldr	w1, [x0,1404]
	cmp	w4, w1
	bcs	.L2752
	mov	w21, 2048
	mov	x20, x3
.L2757:
	add	x22, x20, 3472
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w0, [x22,84]
	ldr	w1, [x1,1404]
	cmp	w0, w1
	bcs	.L2756
	add	x1, x29, 52
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x22,84]
	ldr	w1, [x29,52]
	add	w0, w0, 1
	str	w0, [x22,84]
	cmn	w1, #1
	beq	.L2755
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
	bne	.L2756
	ldr	w0, [x29,52]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2756:
	mov	w0, -1
	b	.L2759
.L2755:
	subs	w21, w21, #1
	bne	.L2757
	b	.L2756
.L2752:
	ldr	w0, [x0,904]
	str	wzr, [x2,80]
	str	wzr, [x2,84]
	str	w0, [x2,76]
	b	.L2766
.L2751:
	add	x5, x19, :lo12:.LANCHOR2
	mov	w6, 10000
	mov	w8, 63
	ldr	w3, [x5,920]
	ldr	w7, [x5,904]
	cmp	w3, w6
	mov	w6, 31
	csel	w8, w6, w8, hi
	ldr	w6, [x2,76]
	add	w9, w7, 1048576
	ldr	w4, [x5,1404]
	cmp	w6, w9
	bhi	.L2761
	mov	w9, 1000
	lsr	w3, w3, 10
	add	w3, w3, 1
	mul	w4, w4, w9
	udiv	w3, w4, w3
	add	w3, w3, w6
	cmp	w3, w7
	bcc	.L2761
	ldrh	w3, [x5,3556]
	tst	w8, w3
	bne	.L2759
	ldr	w2, [x2,100]
	cmp	w2, w3
	beq	.L2759
.L2761:
	add	x19, x19, :lo12:.LANCHOR2
	add	x0, x1, :lo12:.LANCHOR4
	ldrh	w1, [x19,3556]
	str	w1, [x0,3572]
	mov	w1, 1
	str	wzr, [x0,3556]
	str	w1, [x0,3552]
	str	w7, [x0,3548]
.L2766:
	mov	w0, 0
.L2759:
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
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
	ldr	w4, [x3,1408]
	cbnz	w4, .L2920
	adrp	x4, .LANCHOR1
	add	x4, x4, :lo12:.LANCHOR1
	ldr	w5, [x4,3288]
	cmn	w5, #1
	beq	.L2920
	ldr	w5, [x3,928]
	cbnz	w5, .L2920
	ldrh	w5, [x3,3592]
	cmp	w5, 47
	bls	.L2920
	mov	w24, w1
	ldrh	w1, [x4,3292]
	str	w0, [x29,124]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L2769
.L2772:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w2, 65535
	ldrh	w4, [x0,954]
	cmp	w4, w2
	bne	.L2770
	b	.L2771
.L2769:
	ldrh	w1, [x3,3712]
	cmp	w1, w0
	beq	.L2772
	mov	w0, 1
	bl	FtlGcFreeTempBlock
	mov	w2, 1
	cbz	w0, .L2772
	b	.L2920
.L2770:
	ldrh	w1, [x0,952]
	cmp	w1, w2
	bne	.L2771
	ldrh	w3, [x0,956]
	cmp	w3, w1
	beq	.L2771
	ldrh	w2, [x0,958]
	cmp	w2, w1
	beq	.L2771
	mov	w1, -1
	strh	w4, [x0,952]
	strh	w3, [x0,954]
	strh	w2, [x0,956]
	strh	w1, [x0,958]
.L2771:
	add	x1, x19, :lo12:.LANCHOR2
	ldr	w2, [x29,124]
	adrp	x21, .LANCHOR4
	cmp	w2, 1
	ldr	w0, [x1,944]
	add	w0, w0, 1
	add	w0, w0, w2, lsl 7
	str	w0, [x1,944]
	bne	.L2783
	ldr	w1, [x1,660]
	cbnz	w1, .L2774
	adrp	x1, .LANCHOR0+4044
	ldrb	w1, [x1,#:lo12:.LANCHOR0+4044]
	cbz	w1, .L2783
.L2774:
	add	x20, x19, :lo12:.LANCHOR2
	ldr	w1, [x20,924]
	cmp	w1, 39
	bhi	.L2783
	adrp	x22, .LANCHOR5
	add	x1, x22, :lo12:.LANCHOR5
	ldrh	w1, [x1,-136]
	add	w0, w0, w1
	str	w0, [x20,944]
	bl	FtlGcReFreshBadBlk
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x0,3424]
	cmp	w0, w1
	bne	.L2783
	ldrh	w1, [x20,952]
	cmp	w1, w0
	bne	.L2783
	ldr	w0, [x20,944]
	cmp	w0, 1024
	bhi	.L2775
	ldrh	w0, [x20,3608]
	cmp	w0, 63
	bhi	.L2783
.L2775:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x22, :lo12:.LANCHOR5
	ldrh	w2, [x0,962]
	ldrh	w3, [x0,3608]
	add	w2, w2, 64
	strh	wzr, [x1,-136]
	cmp	w3, w2
	bgt	.L2783
	str	wzr, [x0,944]
	ldr	w0, [x0,924]
	cbnz	w0, .L2776
	mov	w0, 6
	b	.L2942
.L2776:
	cmp	w0, 5
	bhi	.L2777
	mov	w0, 18
.L2942:
	strh	w0, [x1,-136]
.L2777:
	mov	w0, 32
	mov	w27, 65535
	bl	List_get_gc_head_node
	uxth	w25, w0
	cmp	w25, w27
	beq	.L2782
	add	x23, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,964]
	cbz	w0, .L2779
	ldrh	w1, [x23,826]
	ubfiz	x25, x25, 1, 16
	ldrh	w3, [x23,756]
	ldr	x26, [x23,1192]
	mul	w1, w1, w3
	ldrh	w2, [x26,x25]
	add	w1, w1, 1
	cmp	w2, w1
	bgt	.L2782
	add	w1, w0, 1
	str	wzr, [x23,932]
	uxth	w1, w1
	strh	w1, [x23,964]
	str	x1, [x29,112]
	bl	List_get_gc_head_node
	uxth	w20, w0
	cmp	w20, w27
	ldr	x1, [x29,112]
	beq	.L2782
	ubfiz	x27, x20, 1, 16
	adrp	x0, .LC152
	ldrh	w4, [x26,x25]
	add	x0, x0, :lo12:.LC152
	mov	w2, w20
	ldrh	w3, [x26,x27]
	bl	printk
	ldrh	w0, [x23,964]
	cmp	w0, 40
	bls	.L2780
	ldr	x0, [x23,1192]
	ldrh	w0, [x0,x27]
	cmp	w0, 32
	bls	.L2780
	strh	wzr, [x23,964]
.L2780:
	add	x22, x22, :lo12:.LANCHOR5
	mov	w0, 6
	strh	w0, [x22,-136]
	b	.L2784
.L2779:
	mov	w0, 1
	strh	w0, [x23,964]
.L2782:
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2784
	add	x22, x22, :lo12:.LANCHOR5
	strh	wzr, [x22,-136]
.L2783:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w20, [x0,3424]
	mov	w0, 65535
	cmp	w20, w0
	bne	.L2946
	add	x23, x19, :lo12:.LANCHOR2
	ldrh	w0, [x23,3712]
	cmp	w0, w20
	beq	.L2947
.L2784:
	mov	w0, 65535
	cmp	w20, w0
	cset	w2, eq
	cbz	w2, .L2797
	ldr	w0, [x29,124]
	cbnz	w0, .L2797
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 1
	ldrh	w1, [x0,3608]
	cmp	w1, 24
	bhi	.L2798
	cmp	w1, 16
	ldrh	w22, [x0,824]
	bls	.L2799
	lsr	w22, w22, 5
	b	.L2798
.L2799:
	cmp	w1, 12
	bls	.L2800
	lsr	w22, w22, 4
	b	.L2798
.L2800:
	lsr	w0, w22, 2
	cmp	w1, 9
	csel	w22, w0, w22, cs
.L2798:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,960]
	cmp	w2, w1
	bcs	.L2802
	ldrh	w1, [x0,3712]
	mov	w2, 65535
	cmp	w1, w2
	bne	.L2803
	ldrh	w2, [x0,952]
	cmp	w2, w1
	bne	.L2803
	adrp	x1, .LANCHOR5-136
	ldrh	w2, [x1,#:lo12:.LANCHOR5-136]
	cbnz	w2, .L2804
	ldr	w1, [x0,1404]
	ldr	w3, [x0,3900]
	add	w1, w1, w1, lsl 1
	cmp	w3, w1, lsr 2
	bcs	.L2805
.L2804:
	add	x21, x21, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x21,3420]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,960]
	b	.L2806
.L2805:
	mov	w1, 18
	strh	w1, [x0,960]
.L2806:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,932]
	b	.L2920
.L2803:
	add	x0, x21, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3420]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,960]
.L2802:
	cmp	w24, 2
	bhi	.L2874
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,660]
	cbz	w0, .L2874
	add	w22, w22, 1
	uxth	w22, w22
	b	.L2874
.L2797:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w3, 65535
	ldrh	w1, [x0,3712]
	cmp	w1, w3
	bne	.L2809
	cbz	w2, .L2809
	ldrh	w2, [x0,952]
	cmp	w2, w1
	bne	.L2809
	add	x2, x21, :lo12:.LANCHOR4
	ldrh	w2, [x2,3424]
	cmp	w2, w1
	beq	.L2810
.L2815:
	mov	w20, 65535
	b	.L2809
.L2810:
	ldrh	w2, [x0,3608]
	adrp	x20, .LANCHOR5
	ldrh	w1, [x0,960]
	str	wzr, [x0,932]
	cmp	w2, w1
	bls	.L2811
	add	x1, x20, :lo12:.LANCHOR5
	ldrh	w1, [x1,-136]
	cbnz	w1, .L2812
	ldr	w1, [x0,1404]
	ldr	w2, [x0,3900]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bcs	.L2813
.L2812:
	add	x0, x21, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3420]
	add	w0, w0, w0, lsl 1
	asr	w0, w0, 2
	strh	w0, [x1,960]
	b	.L2814
.L2813:
	mov	w1, 18
	strh	w1, [x0,960]
.L2814:
	bl	FtlReadRefresh
	mov	w0, 0
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	add	x1, x19, :lo12:.LANCHOR2
	ldr	x1, [x1,1192]
	ldrh	w0, [x1,x0]
	cmp	w0, 4
	bls	.L2811
	add	x20, x20, :lo12:.LANCHOR5
	ldrh	w2, [x20,-136]
	b	.L2920
.L2811:
	add	x23, x20, :lo12:.LANCHOR5
	ldrh	w0, [x23,-136]
	cbnz	w0, .L2815
	add	x1, x21, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w20, [x1,3420]
	add	w1, w20, w20, lsl 1
	asr	w1, w1, 2
	strh	w1, [x22,960]
	bl	List_get_gc_head_node
	ubfiz	x0, x0, 1, 16
	ldr	x1, [x22,1192]
	ldrh	w2, [x22,756]
	ldrh	w1, [x1,x0]
	ldrh	w0, [x22,826]
	mul	w0, w0, w2
	mov	w2, 2
	sdiv	w0, w0, w2
	cmp	w1, w0
	ble	.L2816
	ldrh	w0, [x22,3608]
	sub	w20, w20, #1
	cmp	w0, w20
	blt	.L2816
	bl	FtlReadRefresh
	ldrh	w2, [x23,-136]
	b	.L2920
.L2816:
	cbnz	w1, .L2815
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	bl	decrement_vpc_count
	ldrh	w2, [x19,3608]
	add	w2, w2, 1
	b	.L2920
.L2809:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w22, 2
	ldr	w0, [x0,660]
	cmp	w0, wzr
	csinc	w22, w22, wzr, ne
	b	.L2808
.L2874:
	mov	w20, 65535
.L2808:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w2, 65535
	ldrh	w1, [x0,3424]
	cmp	w1, w2
	bne	.L2818
	cmp	w20, w1
	beq	.L2819
	strh	w20, [x0,3424]
	b	.L2820
.L2819:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,952]
	cmp	w0, w20
	beq	.L2820
	ubfiz	x0, x0, 1, 16
	ldr	x2, [x1,1192]
	ldrh	w0, [x2,x0]
	cbnz	w0, .L2821
	mov	w0, -1
	strh	w0, [x1,952]
.L2821:
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w2, [x1,952]
	strh	w2, [x0,3424]
	mov	w0, -1
	strh	w0, [x1,952]
.L2820:
	add	x23, x21, :lo12:.LANCHOR4
	mov	w1, 65535
	ldrh	w0, [x23,3424]
	strb	wzr, [x23,3432]
	cmp	w0, w1
	beq	.L2818
	bl	IsBlkInGcList
	cbz	w0, .L2823
	mov	w0, -1
	strh	w0, [x23,3424]
.L2823:
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2824
	add	x0, x21, :lo12:.LANCHOR4
	add	x23, x0, 3424
	ldrh	w0, [x0,3424]
	bl	ftl_get_blk_mode
	strb	w0, [x23,8]
.L2824:
	add	x23, x21, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x24, x23, 3424
	ldrh	w1, [x23,3424]
	cmp	w1, w0
	beq	.L2818
	mov	x0, x24
	bl	make_superblock
	strh	wzr, [x23,3426]
	adrp	x0, .LANCHOR5
	add	x1, x19, :lo12:.LANCHOR2
	add	x0, x0, :lo12:.LANCHOR5
	ldrh	w2, [x23,3424]
	strb	wzr, [x23,3430]
	ldr	x1, [x1,1192]
	strh	wzr, [x0,-134]
	ldrh	w1, [x1,x2,lsl 1]
	strh	w1, [x0,-132]
.L2818:
	add	x0, x21, :lo12:.LANCHOR4
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,3424]
	ldrh	w2, [x1,3616]
	cmp	w2, w0
	beq	.L2825
	ldrh	w2, [x1,3664]
	cmp	w2, w0
	beq	.L2825
	ldrh	w1, [x1,3712]
	cmp	w1, w0
	bne	.L2826
.L2825:
	add	x0, x21, :lo12:.LANCHOR4
	mov	w1, -1
	strh	w1, [x0,3424]
.L2826:
	add	x23, x21, :lo12:.LANCHOR4
.L2859:
	ldrh	w0, [x23,3424]
	mov	w1, 65535
	cmp	w0, w1
	bne	.L2827
	add	x0, x19, :lo12:.LANCHOR2
	mov	w26, 2
	str	wzr, [x0,932]
.L2828:
	add	x27, x19, :lo12:.LANCHOR2
	ldrh	w24, [x27,964]
	mov	w0, w24
	bl	List_get_gc_head_node
	uxth	w28, w0
	mov	w0, 65535
	strh	w28, [x23,3424]
	cmp	w28, w0
	bne	.L2829
	strh	wzr, [x27,964]
	mov	w2, 8
	b	.L2920
.L2829:
	mov	w0, w28
	add	w24, w24, 1
	bl	IsBlkInGcList
	cbz	w0, .L2830
	strh	w24, [x27,964]
	b	.L2828
.L2830:
	ldrh	w4, [x27,756]
	ubfiz	x1, x28, 1, 16
	ldrh	w0, [x27,824]
	uxth	w24, w24
	ldr	x2, [x27,1192]
	strh	w24, [x27,964]
	mul	w0, w0, w4
	ldrh	w3, [x2,x1]
	sdiv	w4, w0, w26
	cmp	w3, w4
	bgt	.L2832
	cmp	w3, 8
	bls	.L2833
	cmp	w24, 48
	bls	.L2833
	ldrh	w3, [x23,4032]
	cmp	w3, 35
	bhi	.L2833
.L2832:
	add	x3, x19, :lo12:.LANCHOR2
	strh	wzr, [x3,964]
.L2833:
	ldrh	w1, [x2,x1]
	mov	w2, 65535
	cmp	w20, w2
	bne	.L2834
	cmp	w1, w0
	blt	.L2834
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w2, [x0,964]
	cmp	w2, 3
	bhi	.L2834
	add	x21, x21, :lo12:.LANCHOR4
	strh	wzr, [x0,964]
	adrp	x0, .LANCHOR5-136
	mov	w1, -1
	strh	w1, [x21,3424]
	ldrh	w2, [x0,#:lo12:.LANCHOR5-136]
	b	.L2920
.L2834:
	cbnz	w1, .L2835
	mov	w0, -1
	bl	decrement_vpc_count
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,964]
	add	w0, w0, 1
	strh	w0, [x1,964]
	b	.L2828
.L2835:
	adrp	x0, .LANCHOR0
	strb	wzr, [x23,3432]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L2836
	mov	w0, w28
	bl	ftl_get_blk_mode
	strb	w0, [x23,3432]
.L2836:
	add	x24, x23, 3424
	mov	x0, x24
	bl	make_superblock
	adrp	x1, .LANCHOR5
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x1, :lo12:.LANCHOR5
	ldrh	w2, [x23,3424]
	ldr	x0, [x0,1192]
	strh	wzr, [x1,-134]
	ldrh	w0, [x0,x2,lsl 1]
	strh	w0, [x1,-132]
	strh	wzr, [x23,3426]
	strb	wzr, [x23,3430]
.L2827:
	ldr	w0, [x29,124]
	cmp	w0, 1
	bne	.L2837
	bl	FtlReadRefresh
.L2837:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 1
	str	w1, [x0,928]
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	ldrh	w26, [x0,824]
	ldrb	w1, [x1,4044]
	cbz	w1, .L2838
	ldrb	w1, [x23,3432]
	cmp	w1, 1
	bne	.L2838
	ldrh	w26, [x0,826]
.L2838:
	ldrh	w0, [x23,3426]
	add	w1, w0, w22
	cmp	w1, w26
	ble	.L2839
	sub	w22, w26, w0
	uxth	w22, w22
.L2839:
	mov	w27, 0
.L2840:
	cmp	w22, w27, uxth
	bls	.L2847
	add	x1, x23, 3424
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w3, [x1,2]
	ldrh	w6, [x0,756]
	mov	x0, 0
	add	w3, w3, w27
	mov	w24, w0
.L2848:
	cmp	w6, w0, uxth
	bls	.L2948
	add	x2, x1, x0, lsl 1
	mov	w4, 65535
	ldrh	w2, [x2,16]
	cmp	w2, w4
	beq	.L2841
	add	x5, x19, :lo12:.LANCHOR2
	mov	w4, 56
	orr	w2, w3, w2, lsl 10
	umull	x4, w24, w4
	ldr	x5, [x5,1032]
	add	w24, w24, 1
	add	x4, x5, x4
	uxth	w24, w24
	str	w2, [x4,4]
.L2841:
	add	x0, x0, 1
	b	.L2848
.L2948:
	add	x25, x19, :lo12:.LANCHOR2
	ldrb	w2, [x23,3432]
	mov	w1, w24
	mov	x28, 0
	ldr	x0, [x25,1032]
	bl	FlashReadPages
.L2843:
	cmp	w24, w28, uxth
	bls	.L2949
	mov	x0, 56
	mul	x5, x28, x0
	ldr	x0, [x25,1032]
	add	x1, x0, x5
	ldr	w0, [x0,x5]
	cmn	w0, #1
	ldr	x4, [x1,16]
	beq	.L2877
	ldrh	w0, [x4]
	mov	w1, 61589
	cmp	w0, w1
	bne	.L2877
	ldr	w0, [x4,8]
	add	x1, x29, 136
	mov	w2, 0
	str	x5, [x29,104]
	str	x4, [x29,112]
	bl	log2phys
	ldr	x2, [x25,1032]
	ldr	x5, [x29,104]
	ldr	w0, [x29,136]
	add	x2, x2, x5
	ldr	x4, [x29,112]
	and	w0, w0, 2147483647
	ldr	w1, [x2,4]
	cmp	w0, w1
	bne	.L2877
	adrp	x0, .LANCHOR5
	mov	x3, 56
	add	x1, x0, :lo12:.LANCHOR5
	str	x5, [x29,96]
	str	x4, [x29,104]
	ldrh	w0, [x1,-134]
	add	w0, w0, 1
	strh	w0, [x1,-134]
	ldr	w0, [x23,4000]
	ldr	x1, [x25,1008]
	madd	x1, x0, x3, x1
	ldr	w0, [x2,24]
	str	x1, [x29,112]
	str	w0, [x1,24]
	bl	Ftl_get_new_temp_ppa
	ldr	x1, [x29,112]
	mov	x2, 56
	ldr	x5, [x29,96]
	ldr	x4, [x29,104]
	str	w0, [x1,4]
	ldr	w0, [x23,4000]
	ldr	x1, [x25,1008]
	madd	x0, x0, x2, x1
	ldr	x1, [x25,1032]
	add	x1, x1, x5
	ldr	x2, [x1,8]
	str	x2, [x0,8]
	add	x2, x25, 3712
	str	x2, [x29,112]
	ldr	x1, [x1,16]
	str	x1, [x0,16]
	mov	w1, 1
	ldr	w0, [x29,136]
	str	w0, [x4,12]
	ldrh	w0, [x25,3712]
	strh	w0, [x4,2]
	ldr	w0, [x25,880]
	str	w0, [x4,4]
	ldr	w0, [x23,4000]
	add	w0, w0, 1
	str	w0, [x23,4000]
	ldr	x0, [x25,1032]
	add	x0, x0, x5
	bl	FtlGcBufAlloc
	adrp	x0, .LANCHOR0
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbnz	w0, .L2845
	ldr	x2, [x29,112]
	ldr	w1, [x23,4000]
	ldrb	w0, [x2,7]
	cmp	w1, w0
	beq	.L2845
	ldrh	w0, [x2,4]
	cbnz	w0, .L2877
.L2845:
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2877
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	add	x19, x19, :lo12:.LANCHOR2
	strh	w0, [x21,3424]
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x19,928]
	strh	wzr, [x21,3426]
	ldrh	w2, [x0,-136]
	b	.L2920
.L2877:
	add	x28, x28, 1
	b	.L2843
.L2949:
	add	w27, w27, 1
	b	.L2840
.L2847:
	ldrh	w0, [x23,3426]
	add	w22, w22, w0
	uxth	w22, w22
	strh	w22, [x23,3426]
	cmp	w22, w26
	bcc	.L2849
	ldr	w0, [x23,4000]
	adrp	x22, .LANCHOR5
	cbz	w0, .L2850
	bl	Ftl_gc_temp_data_write_back
	cbz	w0, .L2850
	add	x19, x19, :lo12:.LANCHOR2
	add	x22, x22, :lo12:.LANCHOR5
	str	wzr, [x19,928]
	ldrh	w2, [x22,-136]
	b	.L2920
.L2850:
	add	x22, x22, :lo12:.LANCHOR5
	ldrh	w22, [x22,-134]
	cbnz	w22, .L2851
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x23,3424]
	ldr	x0, [x0,1192]
	ldrh	w0, [x0,x1,lsl 1]
	cbz	w0, .L2851
.L2852:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,1404]
	cmp	w22, w0
	bcs	.L2857
	mov	w0, w22
	add	x1, x29, 140
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,140]
	cmn	w0, #1
	beq	.L2853
	lsr	w0, w0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x23,3424]
	cmp	w1, w0, uxth
	bne	.L2853
.L2857:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,1404]
	cmp	w22, w1
	bcc	.L2851
	ldrh	w1, [x23,3424]
	ldr	x0, [x0,1192]
	strh	wzr, [x0,x1,lsl 1]
	ldrh	w0, [x23,3424]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2851
.L2853:
	add	w22, w22, 1
	b	.L2852
.L2851:
	mov	w0, -1
	strh	w0, [x23,3424]
.L2849:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3608]
	cmp	w0, 2
	bhi	.L2858
	ldrh	w22, [x1,824]
	b	.L2859
.L2858:
	str	wzr, [x1,928]
	adrp	x1, .LANCHOR5-136
	ldrh	w2, [x1,#:lo12:.LANCHOR5-136]
	cmp	w2, wzr
	csinc	w2, w2, w0, ne
	b	.L2920
.L2947:
	ldrh	w25, [x23,952]
	cmp	w25, w20
	bne	.L2784
	ldrh	w0, [x23,3608]
	mov	w2, 1024
	ldr	w1, [x23,944]
	cmp	w0, 24
	mov	w0, 5120
	csel	w0, w0, w2, cc
	cmp	w1, w0
	bls	.L2784
	adrp	x0, .LANCHOR5-136
	str	wzr, [x23,944]
	strh	wzr, [x0,#:lo12:.LANCHOR5-136]
	bl	GetSwlReplaceBlock
	uxth	w20, w0
	cmp	w20, w25
	bne	.L2786
	ldrh	w1, [x23,3608]
	ldrh	w0, [x23,962]
	cmp	w1, w0
	bcs	.L2787
	mov	w0, 64
	bl	List_get_gc_head_node
	uxth	w0, w0
	cmp	w0, w20
	beq	.L2796
	ldr	w2, [x23,868]
	uxtw	x1, w0
	cbnz	w2, .L2789
	ldrh	w0, [x23,776]
	cmp	w0, 3
	beq	.L2789
	ldr	w0, [x23,872]
	cbnz	w0, .L2789
	ldr	w0, [x23,660]
	cbnz	w0, .L2789
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2790
.L2789:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,1192]
	ldrh	w4, [x0,x1,lsl 1]
	ldrh	w1, [x2,826]
	ldrh	w0, [x2,756]
	ldrh	w2, [x2,776]
	mul	w3, w1, w0
	cmp	w2, 3
	mov	w0, 0
	lsr	w1, w1, 1
	csel	w0, w1, w0, eq
	add	w0, w3, w0
	cmp	w4, w0
	bgt	.L2792
	mov	w0, 0
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,1404]
	ldr	w2, [x0,3900]
	add	w1, w1, w1, lsl 1
	cmp	w2, w1, lsr 2
	bhi	.L2945
	mov	w1, 160
.L2943:
	strh	w1, [x0,962]
	mov	w0, 65535
	cmp	w20, w0
	beq	.L2796
	b	.L2786
.L2792:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 128
	b	.L2944
.L2790:
	ldr	x2, [x23,1192]
	ldrh	w1, [x2,x1,lsl 1]
	cmp	w1, 7
	bhi	.L2795
	bl	List_get_gc_head_node
	uxth	w20, w0
	add	x0, x19, :lo12:.LANCHOR2
.L2945:
	mov	w1, 128
	b	.L2943
.L2795:
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, 64
.L2944:
	strh	w1, [x0,962]
	b	.L2796
.L2787:
	mov	w0, 80
	strh	w0, [x23,962]
	b	.L2796
.L2786:
	add	x5, x19, :lo12:.LANCHOR2
	ubfiz	x4, x20, 1, 32
	adrp	x0, .LC153
	mov	w1, w20
	add	x0, x0, :lo12:.LC153
	ldr	x3, [x5,1192]
	ldr	x6, [x5,1176]
	ldrh	w2, [x5,3608]
	ldrh	w3, [x3,x4]
	ldrh	w5, [x5,960]
	ldrh	w4, [x6,x4]
	bl	printk
.L2796:
	bl	FtlGcReFreshBadBlk
	b	.L2784
.L2946:
	mov	w20, w0
	b	.L2784
.L2920:
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
	adrp	x0, .LANCHOR5-128
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	ldr	x20, [x0,#:lo12:.LANCHOR5-128]
	add	x0, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	ldr	w22, [x0,1408]
	cbnz	w22, .L2952
	ldr	w1, [x0,976]
	cbz	w1, .L2952
	adrp	x0, .LANCHOR0+4044
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	cbz	w0, .L2954
	ldrb	w0, [x20,8]
	cmp	w0, 1
	cset	w22, eq
.L2954:
	add	x0, x19, :lo12:.LANCHOR2
	ldrb	w3, [x20,9]
	adrp	x24, .LC154
	mov	w2, w22
	mov	w23, 0
	mov	w25, 56
	ldr	x0, [x0,1040]
	add	x24, x24, :lo12:.LC154
	bl	FlashProgPages
.L2955:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,976]
	cmp	w23, w1
	bcs	.L2973
	umull	x21, w23, w25
	ldr	x1, [x0,1040]
	add	x0, x1, x21
	ldr	w1, [x1,x21]
	cmn	w1, #1
	beq	.L2976
	ldr	w1, [x0,4]
	cbz	w22, .L2990
	orr	w1, w1, -2147483648
.L2990:
	ldr	w0, [x0,24]
	mov	w2, 1
	str	w1, [x29,108]
	add	x26, x19, :lo12:.LANCHOR2
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x26,1040]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2959
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x26,1192]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2960
	mov	x0, x24
	bl	printk
.L2960:
	mov	w0, w21
	bl	decrement_vpc_count
.L2959:
	add	w23, w23, 1
	b	.L2955
.L2992:
	mov	w20, 16386
.L2972:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,966]
	cbz	w0, .L2973
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w20, w20, #1
	bne	.L2972
.L2973:
	add	x19, x19, :lo12:.LANCHOR2
	str	wzr, [x19,976]
	b	.L2952
.L2976:
	adrp	x25, .LANCHOR4
	adrp	x26, .LC154
	mov	w24, 0
	mov	w27, 56
	add	x25, x25, :lo12:.LANCHOR4
	add	x26, x26, :lo12:.LC154
.L2956:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w1, [x0,976]
	cmp	w24, w1
	bcs	.L2992
	umull	x21, w24, w27
	ldr	x0, [x0,1040]
	mov	w1, -1
	mov	w28, 1
	str	w1, [x0,x21]
.L2962:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	x0, [x23,1040]
	add	x1, x0, x21
	ldr	w0, [x0,x21]
	cmn	w0, #1
	ldr	w0, [x1,4]
	bne	.L2993
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	ldrh	w1, [x20]
	cmp	w1, w0, uxth
	bne	.L2963
	ldr	x2, [x23,1192]
	ubfiz	x1, x1, 1, 16
	ldrh	w3, [x20,4]
	ldrh	w0, [x2,x1]
	sub	w0, w0, w3
	strh	w0, [x2,x1]
	strb	wzr, [x20,6]
	ldrh	w0, [x23,824]
	strh	w0, [x20,2]
	strh	wzr, [x20,4]
.L2963:
	ldrh	w0, [x20,4]
	cbnz	w0, .L2964
	mov	x0, x20
	bl	allocate_new_data_superblock
.L2964:
	add	x23, x19, :lo12:.LANCHOR2
	ldr	w0, [x25,3568]
	add	w0, w0, 1
	str	w0, [x25,3568]
	ldr	x0, [x23,1040]
	add	x0, x0, x21
	ldr	w0, [x0,4]
	lsr	x0, x0, 10
	bl	FtlGcMarkBadPhyBlk
	mov	x0, x20
	bl	get_new_active_ppa
	str	w0, [x29,108]
	ldr	x1, [x23,1040]
	mov	w2, w22
	add	x1, x1, x21
	str	w0, [x1,4]
	mov	w1, 1
	ldr	x0, [x23,1040]
	ldrb	w3, [x20,9]
	add	x0, x0, x21
	bl	FlashProgPages
	ldr	x0, [x23,1040]
	ldr	w0, [x0,x21]
	cmn	w0, #1
	bne	.L2965
	str	w28, [x23,1408]
.L2965:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,1408]
	cbz	w0, .L2962
	b	.L2952
.L2993:
	cbz	w22, .L2991
	orr	w0, w0, -2147483648
.L2991:
	str	w0, [x29,108]
	mov	w2, 1
	ldr	w0, [x1,24]
	add	x23, x19, :lo12:.LANCHOR2
	add	x1, x29, 108
	bl	log2phys
	ldr	x0, [x23,1040]
	add	x21, x0, x21
	ldr	x0, [x21,16]
	ldr	w0, [x0,12]
	cmn	w0, #1
	beq	.L2969
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	uxth	w1, w0
	ubfiz	x0, x1, 1, 16
	ldr	x2, [x23,1192]
	mov	w21, w1
	ldrh	w2, [x2,x0]
	cbnz	w2, .L2970
	mov	x0, x26
	bl	printk
.L2970:
	mov	w0, w21
	bl	decrement_vpc_count
.L2969:
	add	w24, w24, 1
	b	.L2956
.L2952:
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
	adrp	x0, .LANCHOR2+1408
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR2+1408]
	cbnz	w0, .L2995
	adrp	x0, .LANCHOR1+3288
	ldr	w0, [x0,#:lo12:.LANCHOR1+3288]
	cmn	w0, #1
	beq	.L2995
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	w0, 1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2995:
	mov	w0, 0
	ldp	x29, x30, [sp], 16
	ret
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	adrp	x0, .LANCHOR1+3288
	stp	x29, x30, [sp, -16]!
	add	x29, sp, 0
	ldr	w0, [x0,#:lo12:.LANCHOR1+3288]
	cmp	w0, 1
	bne	.L3000
	bl	FtlSysFlush
.L3000:
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
	bhi	.L3004
	adrp	x0, .LANCHOR1+3288
	ldr	w1, [x0,#:lo12:.LANCHOR1+3288]
	cmn	w1, #1
	mov	w0, w1
	beq	.L3004
	cmp	w19, 31
	bls	.L3019
	adrp	x22, .LANCHOR2
	add	x21, x22, :lo12:.LANCHOR2
	ldr	w0, [x21,1408]
	cbnz	w0, .L3019
	bl	FtlCacheWriteBack
	ldrh	w1, [x21,830]
	udiv	w21, w20, w1
	msub	w20, w1, w21, w20
	uxth	w20, w20
	cbz	w20, .L3007
	sub	w1, w1, w20
	add	w21, w21, 1
	cmp	w1, w19
	csel	w1, w1, w19, ls
	sub	w19, w19, w1, uxth
.L3007:
	mov	w0, -1
	adrp	x23, .LANCHOR5
	str	w0, [x29,76]
.L3008:
	add	x20, x22, :lo12:.LANCHOR2
	ldrh	w0, [x20,830]
	cmp	w19, w0
	bcc	.L3020
	mov	w0, w21
	add	x1, x29, 72
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,72]
	cmn	w0, #1
	beq	.L3009
	add	x1, x23, :lo12:.LANCHOR5
	mov	w2, 1
	ldr	w0, [x1,-120]
	add	w0, w0, 1
	str	w0, [x1,-120]
	ldr	w0, [x20,892]
	add	x1, x29, 76
	add	w0, w0, 1
	str	w0, [x20,892]
	mov	w0, w21
	bl	log2phys
	ldr	w0, [x29,72]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3009:
	add	x0, x22, :lo12:.LANCHOR2
	add	w21, w21, 1
	ldrh	w0, [x0,830]
	sub	w19, w19, w0
	b	.L3008
.L3020:
	adrp	x0, .LANCHOR5
	add	x0, x0, :lo12:.LANCHOR5
	ldr	w1, [x0,-120]
	cmp	w1, 32
	bls	.L3019
	str	wzr, [x0,-120]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3019:
	mov	w0, 0
.L3004:
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
	ldr	w3, [x2,1408]
	ldrh	w1, [x2,824]
	cbnz	w3, .L3057
	ldrh	w20, [x2,3712]
	mov	w4, 65535
	cmp	w20, w4
	bne	.L3024
.L3033:
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x21, .LANCHOR4
	add	x22, x21, :lo12:.LANCHOR4
	mov	w0, 65535
	add	x23, x20, 3712
	ldrh	w1, [x20,3712]
	str	wzr, [x22,4024]
	cmp	w1, w0
	beq	.L3057
	bl	FtlCacheWriteBack
	mov	w26, 12
	ldrb	w0, [x23,7]
	ldrh	w3, [x20,824]
	ldr	x1, [x20,1192]
	ldrh	w2, [x20,3712]
	mul	w0, w0, w3
	strh	w0, [x1,x2,lsl 1]
	ldrh	w1, [x22,4034]
	mov	w22, 0
	ldr	w0, [x20,884]
	add	w0, w1, w0
	str	w0, [x20,884]
	b	.L3034
.L3024:
	cbz	w0, .L3027
	adrp	x0, .LANCHOR1
	add	x0, x0, :lo12:.LANCHOR1
	ldrh	w5, [x0,3292]
	cmp	w5, w4
	beq	.L3028
.L3029:
	mov	w1, 2
	b	.L3027
.L3028:
	strh	w3, [x0,3292]
	ldrh	w0, [x2,3608]
	cmp	w0, 17
	bhi	.L3029
.L3027:
	add	x21, x19, :lo12:.LANCHOR2
	add	x0, x21, 3712
	bl	FtlGcScanTempBlk
	str	w0, [x29,92]
	cmn	w0, #1
	beq	.L3030
	ubfiz	x20, x20, 1, 16
	ldr	x1, [x21,1176]
	ldrh	w0, [x1,x20]
	cmp	w0, 4
	bls	.L3031
	sub	w0, w0, #5
	strh	w0, [x1,x20]
	mov	w0, 1
	bl	FtlEctTblFlush
.L3031:
	adrp	x0, .LANCHOR4
	add	x1, x0, :lo12:.LANCHOR4
	mov	x19, x0
	ldr	w2, [x1,4024]
	cbnz	w2, .L3032
	ldr	w0, [x1,3568]
	add	w0, w0, 1
	str	w0, [x1,3568]
	ldr	w0, [x29,92]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3032:
	add	x0, x19, :lo12:.LANCHOR4
	str	wzr, [x0,4024]
	mov	w0, 1
	b	.L3023
.L3030:
	adrp	x0, .LANCHOR1+3292
	mov	w1, 65535
	ldrh	w2, [x0,#:lo12:.LANCHOR1+3292]
	mov	w0, 1
	cmp	w2, w1
	bne	.L3023
	b	.L3033
.L3037:
	ldr	w1, [x23,4]
	cmp	w0, w1
	bne	.L3055
.L3036:
	add	w22, w22, 1
	uxth	w22, w22
.L3034:
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w0, [x0,4034]
	cmp	w0, w22
	bls	.L3058
	umull	x24, w22, w26
	ldr	x25, [x20,992]
	ldr	w1, [x20,1404]
	add	x23, x25, x24
	ldr	w0, [x23,8]
	cmp	w0, w1
	bcs	.L3055
	add	x1, x29, 92
	mov	w2, 0
	bl	log2phys
	ldr	w0, [x29,92]
	ldr	w1, [x25,x24]
	cmp	w0, w1
	bne	.L3037
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	mov	w24, w0
	ldr	w0, [x23,8]
	add	x1, x23, 4
	mov	w2, 1
	bl	log2phys
	mov	w0, w24
	b	.L3056
.L3055:
	ldrh	w0, [x20,3712]
.L3056:
	bl	decrement_vpc_count
	b	.L3036
.L3058:
	mov	w0, -1
	adrp	x20, .LANCHOR0
	bl	decrement_vpc_count
	add	x0, x20, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L3039
	add	x1, x19, :lo12:.LANCHOR2
	adrp	x0, .LC155
	add	x0, x0, :lo12:.LC155
	ldrh	w1, [x1,3712]
	bl	printk
.L3039:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w0, [x1,3712]
	ldr	x1, [x1,1192]
	ubfiz	x2, x0, 1, 16
	ldrh	w1, [x1,x2]
	cbz	w1, .L3040
	bl	INSERT_DATA_LIST
	b	.L3041
.L3040:
	bl	INSERT_FREE_LIST
.L3041:
	add	x23, x21, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR2
	mov	w24, -1
	strh	wzr, [x23,4034]
	strh	w24, [x22,3712]
	strh	wzr, [x23,4032]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	strh	w24, [x23,3424]
	ldr	w0, [x22,660]
	cbz	w0, .L3042
	ldr	w0, [x22,924]
	cmp	w0, 39
	bhi	.L3042
	ldrh	w0, [x23,3420]
	ldrh	w1, [x22,3608]
	cmp	w1, w0
	bcs	.L3057
	lsl	w0, w0, 1
	strh	w0, [x22,960]
.L3057:
	mov	w0, 0
	b	.L3023
.L3042:
	add	x21, x21, :lo12:.LANCHOR4
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w1, [x21,3420]
	ldrh	w0, [x19,3608]
	add	w2, w1, w1, lsl 1
	cmp	w0, w2, lsr 2
	ble	.L3057
	add	x20, x20, :lo12:.LANCHOR0
	ldrb	w0, [x20,4044]
	cbz	w0, .L3043
	sub	w1, w1, #2
	strh	w1, [x19,960]
	b	.L3057
.L3043:
	mov	w1, 20
	strh	w1, [x19,960]
.L3023:
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
	add	x20, x19, 3712
	mov	x0, x20
	ldrh	w1, [x19,824]
	bl	FtlGcScanTempBlk
	ldrh	w1, [x19,3714]
	ldrh	w0, [x19,824]
	cmp	w1, w0
	bcc	.L3059
	add	x0, x19, 3760
	bl	FtlMapBlkWriteDumpData
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	adrp	x0, .LANCHOR4+4024
	str	wzr, [x0,#:lo12:.LANCHOR4+4024]
.L3059:
	ldp	x19, x20, [sp,16]
	ldp	x29, x30, [sp], 32
	ret
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	stp	x29, x30, [sp, -32]!
	adrp	x0, .LANCHOR4+4080
	add	x29, sp, 0
	stp	x19, x20, [sp,16]
	adrp	x19, .LANCHOR2
	str	wzr, [x0,#:lo12:.LANCHOR4+4080]
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x19, 3616
	add	x19, x19, 3664
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
	adrp	x20, .LANCHOR4
	adrp	x19, .LANCHOR2
	stp	x21, x22, [sp,32]
	add	x0, x20, :lo12:.LANCHOR4
	add	x22, x19, :lo12:.LANCHOR2
	str	x23, [sp,48]
	strh	w1, [x0,4074]
	strh	wzr, [x0,4076]
	ldrh	w0, [x22,760]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	w1, [x22,3904]
	mov	w0, 65535
	cmp	w1, w0
	bne	.L3063
.L3065:
	mov	w21, -1
	b	.L3064
.L3063:
	bl	FtlLoadSysInfo
	mov	w21, w0
	cbnz	w0, .L3065
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	w0, 1
	bl	FtlUpdateVaildLpn
	ldrh	w2, [x22,862]
	mov	x1, 0
	ldr	x0, [x22,1272]
.L3066:
	cmp	w1, w2
	mov	w3, w1
	bge	.L3070
	add	x4, x0, x1, lsl 4
	add	x1, x1, 1
	ldr	w4, [x4,4]
	tbz	w4, #31, .L3066
.L3070:
	add	x0, x19, :lo12:.LANCHOR2
	cmp	w3, w2
	ldrh	w1, [x0,3556]
	add	w1, w1, 1
	strh	w1, [x0,3556]
	blt	.L3067
	add	x20, x20, :lo12:.LANCHOR4
	ldrh	w0, [x20,4076]
	cbz	w0, .L3071
.L3067:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,3616]
	ldr	x4, [x1,1192]
	lsl	x2, x2, 1
	ldrh	w5, [x1,3620]
	ldrh	w3, [x4,x2]
	sub	w3, w3, w5
	strh	w3, [x4,x2]
	strb	wzr, [x1,3622]
	ldrh	w3, [x1,3664]
	ldrh	w2, [x1,824]
	lsl	x3, x3, 1
	ldr	x4, [x1,1192]
	strh	w2, [x1,3618]
	strh	wzr, [x1,3620]
	ldrh	w5, [x1,3668]
	ldrh	w0, [x4,x3]
	sub	w0, w0, w5
	strh	w0, [x4,x3]
	strb	wzr, [x1,3670]
	ldrh	w0, [x1,824]
	strh	w0, [x1,3666]
	ldrh	w0, [x1,3558]
	strh	wzr, [x1,3668]
	add	w0, w0, 1
	strh	w0, [x1,3558]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3071:
	add	x20, x19, :lo12:.LANCHOR2
	mov	w1, 65535
	add	x23, x20, 3616
	ldrh	w0, [x20,3616]
	cmp	w0, w1
	beq	.L3072
	ldrh	w1, [x20,3620]
	cbnz	w1, .L3072
	ldrh	w1, [x20,3668]
	add	x22, x20, 3664
	cbnz	w1, .L3072
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,3664]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	x0, x23
	bl	allocate_new_data_superblock
	mov	x0, x22
	bl	allocate_new_data_superblock
.L3072:
	adrp	x0, .LANCHOR0+88
	ldrb	w0, [x0,#:lo12:.LANCHOR0+88]
	cbnz	w0, .L3073
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,3556]
	and	w0, w0, 31
	cbnz	w0, .L3064
.L3073:
	bl	FtlVpcCheckAndModify
.L3064:
	mov	w0, w21
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
	str	x23, [sp,48]
	add	x20, x19, :lo12:.LANCHOR2
	stp	x21, x22, [sp,32]
	ldr	w21, [x20,1408]
	cbnz	w21, .L3081
	ldrh	w2, [x20,860]
	mov	w1, w21
	ldr	x0, [x20,1256]
	lsl	w2, w2, 2
	bl	ftl_memset
	ldrh	w2, [x20,860]
	mov	w1, w21
	ldr	x0, [x20,1248]
	lsl	w2, w2, 2
	bl	ftl_memset
	str	w21, [x20,876]
	ldrh	w0, [x20,760]
	str	w21, [x20,880]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3082
	bl	FtlMakeBbt
.L3082:
	mov	w0, 0
.L3083:
	add	x1, x19, :lo12:.LANCHOR2
	ldrh	w2, [x1,830]
	cmp	w0, w2, lsl 7
	bge	.L3110
	ubfiz	x3, x0, 2, 16
	ldr	x4, [x1,1096]
	mvn	w2, w0
	orr	w2, w0, w2, lsl 16
	str	w2, [x4,x3]
	add	w0, w0, 1
	ldr	x2, [x1,1104]
	mov	w1, 23752
	movk	w1, 0xa0f, lsl 16
	uxth	w0, w0
	str	w1, [x2,x3]
	b	.L3083
.L3110:
	ldrh	w21, [x1,764]
	mov	w20, 0
.L3085:
	add	x22, x19, :lo12:.LANCHOR2
	ldrh	w0, [x22,766]
	cmp	w0, w21
	bls	.L3111
	mov	w0, w21
	mov	w1, 1
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3085
.L3111:
	ldrh	w0, [x22,756]
	sub	w1, w20, #3
	cmp	w1, w0, lsl 1
	bge	.L3087
.L3091:
	mov	w20, 0
	mov	w21, w20
	b	.L3088
.L3087:
	udiv	w20, w20, w0
	ldr	w0, [x22,856]
	add	w0, w20, w0
	bl	FtlSysBlkNumInit
	ldrh	w0, [x22,760]
	bl	FtlFreeSysBlkQueueInit
	ldrh	w20, [x22,764]
.L3089:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,766]
	cmp	w0, w20
	bls	.L3091
	mov	w0, w20
	mov	w1, 1
	add	w20, w20, 1
	bl	FtlLowFormatEraseBlock
	uxth	w20, w20
	b	.L3089
.L3088:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w1, [x0,764]
	cmp	w1, w21
	bls	.L3112
	mov	w0, w21
	mov	w1, 0
	add	w21, w21, 1
	bl	FtlLowFormatEraseBlock
	add	w0, w20, w0
	uxth	w21, w21
	uxth	w20, w0
	b	.L3088
.L3112:
	ldrh	w1, [x0,766]
	adrp	x21, .LANCHOR4
	ldrh	w2, [x0,756]
	add	x6, x21, :lo12:.LANCHOR4
	str	w1, [x0,948]
	ldr	w1, [x0,768]
	udiv	w3, w1, w2
	str	w3, [x0,1404]
	ubfx	x5, x3, 5, 16
	add	w4, w5, 36
	strh	w4, [x6,3420]
	mov	w4, 24
	mul	w4, w2, w4
	cmp	w20, w4
	ble	.L3093
	sub	w1, w1, w20
	udiv	w1, w1, w2
	str	w1, [x0,1404]
	lsr	w1, w1, 5
	add	w1, w1, 24
	strh	w1, [x6,3420]
.L3093:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	w0, [x0,660]
	cmp	w0, 1
	bne	.L3094
	add	x1, x21, :lo12:.LANCHOR4
	udiv	w0, w20, w2
	ldrh	w4, [x1,3420]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,3420]
.L3094:
	adrp	x22, .LANCHOR0
	add	x0, x22, :lo12:.LANCHOR0
	ldrb	w0, [x0,4044]
	cbz	w0, .L3095
	add	x1, x21, :lo12:.LANCHOR4
	udiv	w0, w20, w2
	ldrh	w4, [x1,3420]
	add	w0, w4, w0
	add	w0, w4, w0, asr 2
	strh	w0, [x1,3420]
.L3095:
	add	x6, x19, :lo12:.LANCHOR2
	ldrh	w1, [x6,818]
	cbz	w1, .L3097
	add	x0, x21, :lo12:.LANCHOR4
	ldrh	w4, [x0,3420]
	add	w4, w4, w1, lsr 1
	strh	w4, [x0,3420]
	mul	w4, w1, w2
	cmp	w4, w20
	ble	.L3097
	add	w1, w1, 32
	str	w3, [x6,1404]
	add	w1, w5, w1
	strh	w1, [x0,3420]
.L3097:
	add	x19, x19, :lo12:.LANCHOR2
	add	x20, x21, :lo12:.LANCHOR4
	add	x22, x22, :lo12:.LANCHOR0
	mov	w23, -1
	ldrh	w0, [x20,3420]
	ldr	w1, [x19,1404]
	sub	w0, w1, w0
	ldrh	w1, [x19,824]
	mul	w0, w0, w2
	str	w0, [x20,3416]
	mul	w0, w0, w1
	ldrh	w1, [x19,830]
	str	w0, [x19,1404]
	mul	w0, w0, w1
	str	w0, [x22,4048]
	bl	FtlBbmTblFlush
	ldrh	w2, [x19,766]
	mov	w1, 0
	ldr	x0, [x19,1192]
	lsl	w2, w2, 1
	bl	ftl_memset
	strh	w23, [x20,3424]
	strh	wzr, [x20,3426]
	mov	w0, 1
	strb	wzr, [x20,3430]
	mov	w1, 255
	strb	wzr, [x20,3432]
	add	x20, x19, 3616
	ldrh	w2, [x19,764]
	strb	w0, [x20,8]
	ldr	x0, [x22,16]
	lsr	w2, w2, 3
	str	wzr, [x19,3900]
	strh	wzr, [x20,2]
	strb	wzr, [x20,6]
	strh	wzr, [x19,3616]
	bl	ftl_memset
.L3099:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L3100
	ldrh	w1, [x20]
	ldr	x0, [x19,1192]
	strh	w23, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3099
.L3100:
	ldr	w0, [x19,876]
	mov	w22, -1
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,876]
	ldr	x0, [x19,1192]
	ldrh	w2, [x20,4]
	strh	w2, [x0,x1,lsl 1]
	add	x0, x19, 3664
	strh	wzr, [x19,3666]
	ldrh	w1, [x20]
	mov	x20, x0
	strb	wzr, [x19,3670]
	add	w1, w1, 1
	strh	w1, [x19,3664]
	mov	w1, 1
	strb	w1, [x19,3672]
.L3101:
	mov	x0, x20
	bl	make_superblock
	ldrb	w0, [x20,7]
	cbnz	w0, .L3102
	ldrh	w1, [x20]
	ldr	x0, [x19,1192]
	strh	w22, [x0,x1,lsl 1]
	ldrh	w0, [x20]
	add	w0, w0, 1
	strh	w0, [x20]
	b	.L3101
.L3102:
	ldr	w0, [x19,876]
	add	x21, x21, :lo12:.LANCHOR4
	str	w0, [x20,12]
	add	w0, w0, 1
	ldrh	w1, [x20]
	str	w0, [x19,876]
	ldr	x0, [x19,1192]
	ldrh	w2, [x20,4]
	mov	w20, -1
	strh	w2, [x0,x1,lsl 1]
	strh	w20, [x19,3712]
	bl	FtlFreeSysBlkQueueOut
	strh	w0, [x19,3904]
	ldr	w0, [x21,3416]
	strh	w0, [x19,3910]
	ldr	w0, [x19,876]
	str	w0, [x19,3912]
	add	w0, w0, 1
	strh	wzr, [x19,3906]
	strh	w20, [x19,3908]
	str	w0, [x19,876]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .L3081
	adrp	x0, .LANCHOR1+3288
	mov	w1, 1
	str	w1, [x0,#:lo12:.LANCHOR1+3288]
.L3081:
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
	ldrb	w0, [x0,4044]
	cbz	w0, .L3114
.L3116:
	mov	w0, 0
	b	.L3115
.L3114:
	adrp	x0, RK29_NANDC_REG_BASE
	ldr	x0, [x0,#:lo12:RK29_NANDC_REG_BASE]
	bl	FlashInit
	cbnz	w0, .L3116
	bl	FlashLoadFactorBbt
	cbz	w0, .L3117
	bl	FlashMakeFactorBbt
.L3117:
	adrp	x20, .LANCHOR2
	add	x0, x20, :lo12:.LANCHOR2
	ldr	x0, [x0,696]
	bl	FlashReadIdbDataRaw
	cbz	w0, .L3118
	mov	w1, 0
	mov	w2, 16
	add	x0, x29, 32
	bl	FlashReadFacBbtData
	mov	w0, 0
	ldr	w2, [x29,32]
	mov	w1, w0
	mov	w4, 1
.L3120:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 16
	add	w0, w0, w3
	bne	.L3120
	cmp	w0, 6
	bhi	.L3121
	add	x0, x19, :lo12:.LANCHOR0
	b	.L3143
.L3121:
	mov	w1, 0
	mov	w4, 1
.L3124:
	lsl	w3, w4, w1
	add	w1, w1, 1
	tst	w3, w2
	cset	w3, ne
	cmp	w1, 24
	add	w0, w0, w3
	bne	.L3124
	cmp	w0, 17
	add	x0, x19, :lo12:.LANCHOR0
	bls	.L3143
	mov	w1, 36
.L3143:
	strb	w1, [x0,89]
	add	x0, x19, :lo12:.LANCHOR0
	ldrb	w1, [x0,89]
	strh	w1, [x0,4042]
.L3118:
	adrp	x0, .LC45
	adrp	x1, .LC139
	add	x1, x1, :lo12:.LC139
	add	x0, x0, :lo12:.LC45
	add	x20, x20, :lo12:.LANCHOR2
	bl	printk
	add	x0, x19, :lo12:.LANCHOR0
	mov	w19, 1
	add	x0, x0, 4016
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	w0, [x20,760]
	bl	FtlFreeSysBlkQueueInit
.L3126:
	bl	FtlLoadBbt
	cbz	w0, .L3127
.L3145:
	bl	FtlLowFormat
	cmp	w19, 3
	bhi	.L3146
	add	w19, w19, 1
	b	.L3126
.L3146:
	mov	w0, -1
	b	.L3115
.L3127:
	bl	FtlSysBlkInit
	cbnz	w0, .L3145
	adrp	x1, .LANCHOR1+3288
	mov	w2, 1
	str	w2, [x1,#:lo12:.LANCHOR1+3288]
.L3115:
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
	ldr	w1, [x0,1408]
	cbz	w1, .L3148
.L3151:
	mov	w0, 0
	b	.L3149
.L3148:
	adrp	x1, .LANCHOR0+4044
	adrp	x22, .LANCHOR4
	ldrb	w1, [x1,#:lo12:.LANCHOR0+4044]
	cbz	w1, .L3150
	add	x1, x22, :lo12:.LANCHOR4
	ldr	w1, [x1,4000]
	tbz	x1, 0, .L3150
	ldrh	w0, [x0,3716]
	cbnz	w0, .L3151
.L3150:
	add	x0, x19, :lo12:.LANCHOR2
	add	x1, x22, :lo12:.LANCHOR4
	mov	w2, 0
	mov	w21, 0
	mov	w3, w2
	mov	w23, 56
	ldr	x0, [x0,1008]
	ldr	w1, [x1,4000]
	bl	FlashProgPages
.L3152:
	add	x20, x22, :lo12:.LANCHOR4
	ldr	w1, [x20,4000]
	cmp	w21, w1
	bcs	.L3162
	add	x1, x19, :lo12:.LANCHOR2
	umull	x0, w21, w23
	ldr	x4, [x1,1008]
	add	x3, x4, x0
	ldr	w4, [x4,x0]
	cmn	w4, #1
	ldr	x2, [x3,16]
	bne	.L3153
	ldrh	w3, [x1,3712]
	ldr	x2, [x1,1192]
	strh	wzr, [x2,x3,lsl 1]
	strh	w4, [x1,3712]
	ldr	x1, [x1,1008]
	ldr	w2, [x20,3568]
	add	x0, x1, x0
	add	w2, w2, 1
	str	w2, [x20,3568]
	ldr	w0, [x0,4]
	lsr	w0, w0, 10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3161
.L3153:
	ldr	w0, [x2,12]
	add	w21, w21, 1
	ldr	w1, [x3,4]
	ldr	w2, [x2,8]
	uxth	w21, w21
	bl	FtlGcUpdatePage
	b	.L3152
.L3162:
	add	x19, x19, :lo12:.LANCHOR2
	ldr	x0, [x19,1008]
	bl	FtlGcBufFree
	str	wzr, [x20,4000]
	ldrh	w0, [x19,3716]
	cbnz	w0, .L3151
	mov	w0, 1
	bl	FtlGcFreeTempBlock
.L3161:
	mov	w0, 1
.L3149:
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
	add	x1, x0, 3712
	ldrh	w2, [x0,3712]
	mov	w0, 65535
	cmp	w2, w0
	beq	.L3164
	ldrh	w0, [x1,4]
	cbnz	w0, .L3165
.L3164:
	bl	FtlCacheWriteBack
	mov	w0, 0
	bl	FtlGcFreeTempBlock
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3712
	strb	wzr, [x0,8]
	bl	allocate_data_superblock
	adrp	x0, .LANCHOR4
	add	x0, x0, :lo12:.LANCHOR4
	strh	wzr, [x0,4032]
	strh	wzr, [x0,4034]
	bl	l2p_flush
	mov	w0, 0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3165:
	add	x0, x19, :lo12:.LANCHOR2
	add	x0, x0, 3712
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
	adrp	x0, .LANCHOR1+3288
	stp	x29, x30, [sp,16]
	add	x29, sp, 16
	ldr	w5, [x0,#:lo12:.LANCHOR1+3288]
	stp	x19, x20, [sp,32]
	stp	x21, x22, [sp,48]
	stp	x23, x24, [sp,64]
	stp	x25, x26, [sp,80]
	stp	x27, x28, [sp,96]
	cmn	w5, #1
	mov	w0, w5
	beq	.L3167
	cmp	w4, 16
	mov	x23, x3
	mov	w26, w2
	mov	w20, w1
	bne	.L3168
	add	w0, w1, 256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartRead
	b	.L3167
.L3168:
	add	w0, w1, w2
	str	w0, [x29,188]
	adrp	x0, .LANCHOR0+4048
	ldr	w2, [x29,188]
	ldr	w1, [x0,#:lo12:.LANCHOR0+4048]
	mov	w0, -1
	cmp	w2, w1
	bhi	.L3167
	adrp	x19, .LANCHOR2
	adrp	x21, .LANCHOR4
	add	x0, x19, :lo12:.LANCHOR2
	sub	w25, w2, #1
	add	x2, x21, :lo12:.LANCHOR4
	ldrh	w1, [x0,830]
	udiv	w28, w20, w1
	udiv	w25, w25, w1
	ldr	w1, [x2,3988]
	add	w1, w26, w1
	sub	w24, w25, w28
	str	w1, [x2,3988]
	add	w24, w24, 1
	ldr	w1, [x0,904]
	add	w1, w24, w1
	str	w1, [x0,904]
	mov	w0, w28
	mov	w1, w25
	bl	FtlCacheMetchLpa
	str	x21, [x29,120]
	cbz	w0, .L3169
	bl	FtlCacheWriteBack
.L3169:
	mov	w27, 0
	adrp	x0, .LC54
	add	x0, x0, :lo12:.LC54
	mov	w21, w28
	str	w27, [x29,184]
	mov	w22, w27
	str	w27, [x29,172]
	str	x0, [x29,112]
.L3170:
	cbz	w24, .L3213
	mov	w0, w21
	add	x1, x29, 204
	mov	w2, 0
	bl	log2phys
	ldr	w3, [x29,204]
	cmn	w3, #1
	bne	.L3211
	mov	w3, 0
.L3171:
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,830]
	cmp	w3, w0
	bcs	.L3175
	madd	w0, w21, w0, w3
	cmp	w0, w20
	bcc	.L3173
	ldr	w1, [x29,188]
	cmp	w0, w1
	bcs	.L3173
	sub	w0, w0, w20
	mov	w1, 0
	ubfiz	x0, x0, 9, 23
	mov	w2, 512
	add	x0, x23, x0
	str	x3, [x29,176]
	bl	ftl_memset
	ldr	x3, [x29,176]
.L3173:
	add	w3, w3, 1
	b	.L3171
.L3211:
	add	x2, x19, :lo12:.LANCHOR2
	mov	w0, 56
	cmp	w21, w28
	umull	x1, w22, w0
	ldr	x0, [x2,1000]
	add	x0, x0, x1
	str	w3, [x0,4]
	ldrh	w0, [x2,830]
	bne	.L3176
	ldr	x3, [x2,1000]
	ldr	x2, [x2,1096]
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
	bne	.L3177
	str	x23, [x3,8]
	b	.L3177
.L3176:
	cmp	w21, w25
	bne	.L3178
	ldr	x3, [x2,1000]
	ldr	x2, [x2,1104]
	add	x3, x3, x1
	ldr	w4, [x29,188]
	str	x2, [x3,8]
	mul	w2, w21, w0
	sub	w27, w4, w2
	cmp	w27, w0
	bne	.L3177
	sub	w2, w2, w20
	ubfiz	x2, x2, 9, 23
	add	x2, x23, x2
	str	x2, [x3,8]
	b	.L3177
.L3178:
	ldr	x2, [x2,1000]
	mul	w0, w21, w0
	add	x2, x2, x1
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x23, x0
	str	x0, [x2,8]
.L3177:
	add	x2, x19, :lo12:.LANCHOR2
	ldr	x0, [x2,1000]
	add	x1, x0, x1
	ldrh	w0, [x2,836]
	ldr	x2, [x2,1128]
	mul	w0, w22, w0
	str	w21, [x1,24]
	add	w22, w22, 1
	and	x0, x0, 4294967292
	add	x0, x2, x0
	str	x0, [x1,16]
.L3175:
	subs	w24, w24, #1
	add	w21, w21, 1
	beq	.L3179
	add	x0, x19, :lo12:.LANCHOR2
	ldrh	w0, [x0,756]
	cmp	w22, w0, lsl 3
	bne	.L3170
.L3179:
	cbz	w22, .L3170
	add	x0, x19, :lo12:.LANCHOR2
	mov	w1, w22
	mov	w2, 0
	ldr	x0, [x0,1000]
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
	add	x0, x0, :lo12:.LANCHOR4
	str	x0, [x29,160]
.L3181:
	ldr	w0, [x29,176]
	cmp	w22, w0
	bls	.L3214
	ldr	x0, [x29,176]
	add	x2, x19, :lo12:.LANCHOR2
	mov	x1, 56
	mul	x8, x0, x1
	ldr	x0, [x2,1000]
	add	x0, x0, x8
	ldr	w1, [x0,24]
	cmp	w1, w28
	bne	.L3182
	ldr	x1, [x0,8]
	ldr	x0, [x2,1096]
	cmp	x1, x0
	bne	.L3183
	ldr	x2, [x29,136]
	mov	x0, x23
	str	x8, [x29,104]
	add	x1, x1, x2
	ldr	w2, [x29,168]
	b	.L3212
.L3182:
	cmp	w1, w25
	bne	.L3183
	ldr	x1, [x0,8]
	ldr	x0, [x2,1104]
	cmp	x1, x0
	bne	.L3183
	ldrh	w0, [x2,830]
	ldr	w2, [x29,132]
	str	x8, [x29,104]
	mul	w0, w25, w0
	sub	w0, w0, w20
	ubfiz	x0, x0, 9, 23
	add	x0, x23, x0
.L3212:
	bl	ftl_memcpy
	ldr	x8, [x29,104]
.L3183:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
	add	x2, x0, x8
	ldr	w1, [x0,x8]
	cmn	w1, #1
	bne	.L3184
	ldr	x0, [x29,160]
	str	w1, [x29,172]
	add	x3, x0, 3472
	ldr	w0, [x0,3544]
	add	w0, w0, 1
	str	w0, [x3,72]
.L3184:
	ldr	x0, [x2,16]
	ldr	w1, [x2,24]
	ldr	w0, [x0,8]
	cmp	w1, w0
	beq	.L3185
	ldr	x0, [x29,160]
	str	x8, [x29,104]
	add	x1, x0, 3472
	ldr	w0, [x0,3544]
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
.L3185:
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,1000]
	add	x1, x0, x8
	ldr	w0, [x0,x8]
	cmp	w0, 256
	bne	.L3186
	ldr	w0, [x1,4]
	lsr	x0, x0, 10
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3186:
	ldr	x0, [x29,176]
	add	x0, x0, 1
	str	x0, [x29,176]
	b	.L3181
.L3214:
	mov	w22, 0
	b	.L3170
.L3213:
	add	x19, x19, :lo12:.LANCHOR2
	ldrh	w0, [x19,966]
	cbz	w0, .L3189
	mov	w0, w24
	mov	w1, 1
	bl	ftl_do_gc
.L3189:
	ldr	w0, [x29,172]
.L3167:
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
	adrp	x1, .LC139
	add	x29, sp, 0
	stp	x21, x22, [sp,32]
	adrp	x22, .LANCHOR1
	str	x23, [sp,48]
	add	x22, x22, :lo12:.LANCHOR1
	add	x1, x1, :lo12:.LC139
	stp	x19, x20, [sp,16]
	adrp	x20, .LANCHOR2
	adrp	x21, .LANCHOR0
	str	w0, [x22,3288]
	adrp	x0, .LANCHOR5-116
	add	x23, x20, :lo12:.LANCHOR2
	add	x21, x21, :lo12:.LANCHOR0
	str	wzr, [x0,#:lo12:.LANCHOR5-116]
	adrp	x0, .LC45
	add	x0, x0, :lo12:.LC45
	str	wzr, [x23,1408]
	bl	printk
	add	x0, x21, 4016
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	w0, [x23,760]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	w0, .L3218
	adrp	x0, .LC156
	add	x0, x0, :lo12:.LC156
	b	.L3232
.L3218:
	bl	FtlSysBlkInit
	mov	w19, w0
	cbz	w0, .L3220
	adrp	x0, .LC157
	add	x0, x0, :lo12:.LC157
.L3232:
	adrp	x1, .LANCHOR3
	add	x1, x1, :lo12:.LANCHOR3
	add	x1, x1, 120
	bl	printk
	b	.L3219
.L3220:
	mov	w1, 1
	str	w1, [x22,3288]
	bl	ftl_do_gc
	ldrh	w0, [x23,3608]
	cmp	w0, 15
	bhi	.L3221
	adrp	x22, .LANCHOR4
	mov	w23, 65535
	add	x22, x22, :lo12:.LANCHOR4
	add	w21, w0, 2
.L3224:
	ldrh	w0, [x22,3424]
	cmp	w0, w23
	bne	.L3222
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,952]
	cmp	w0, w23
	bne	.L3222
	and	w0, w19, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3222:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	add	x0, x20, :lo12:.LANCHOR2
	ldrh	w0, [x0,3608]
	cmp	w0, w21
	bhi	.L3219
	add	w19, w19, 1
	cmp	w19, 1024
	bne	.L3224
	b	.L3219
.L3221:
	ldrb	w0, [x21,4044]
	cbz	w0, .L3219
	mov	w19, 128
.L3226:
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	subs	w19, w19, #1
	bne	.L3226
.L3219:
	mov	w0, 0
	ldr	x23, [sp,48]
	ldp	x19, x20, [sp,16]
	ldp	x21, x22, [sp,32]
	ldp	x29, x30, [sp], 64
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
	stp	x21, x22, [sp,32]
	mov	w22, w1
	adrp	x1, .LANCHOR2
	stp	x23, x24, [sp,48]
	mov	w23, w2
	add	x2, x1, :lo12:.LANCHOR2
	stp	x19, x20, [sp,16]
	stp	x25, x26, [sp,64]
	stp	x27, x28, [sp,80]
	mov	x24, x3
	str	x1, [x29,168]
	ldr	w3, [x2,1408]
	cbnz	w3, .L3234
	adrp	x1, .LANCHOR1
	str	x1, [x29,152]
	add	x3, x1, :lo12:.LANCHOR1
	ldr	w3, [x3,3288]
	cmn	w3, #1
	beq	.L3234
	cmp	w4, 16
	bne	.L3235
	add	w0, w22, 256
	mov	w1, w23
	mov	x2, x24
	bl	FtlVendorPartWrite
	b	.L3234
.L3235:
	adrp	x1, .LANCHOR0
	add	w3, w22, w23
	add	x0, x1, :lo12:.LANCHOR0
	str	x1, [x29,104]
	ldr	w4, [x0,4048]
	mov	w0, -1
	cmp	w3, w4
	bhi	.L3234
	adrp	x5, .LANCHOR5
	ldrh	w1, [x2,830]
	add	x21, x5, :lo12:.LANCHOR5
	mov	w0, 2048
	sub	w3, w3, #1
	adrp	x4, .LANCHOR4
	cmp	w23, w1, lsl 1
	str	x5, [x29,144]
	str	w0, [x21,-112]
	udiv	w0, w22, w1
	str	x4, [x29,136]
	str	w0, [x29,204]
	udiv	w0, w3, w1
	ldr	w3, [x29,204]
	str	w0, [x29,188]
	sub	w26, w0, w3
	add	x3, x4, :lo12:.LANCHOR4
	add	w20, w26, 1
	ldr	w0, [x2,888]
	add	w0, w20, w0
	str	w0, [x2,888]
	ldr	w0, [x3,3984]
	add	w0, w23, w0
	str	w0, [x3,3984]
	ldr	w3, [x2,976]
	cset	w0, cs
	str	w0, [x29,200]
	cbz	w3, .L3237
	mov	w0, 56
	sub	w3, w3, #1
	ldr	w4, [x29,204]
	umull	x3, w3, w0
	ldr	x0, [x2,1040]
	add	x3, x0, x3
	ldr	w0, [x3,24]
	cmp	w4, w0
	bne	.L3238
	ldr	w0, [x2,896]
	add	w0, w0, 1
	str	w0, [x2,896]
	ldr	w0, [x21,-108]
	add	w0, w0, 1
	str	w0, [x21,-108]
	msub	w0, w4, w1, w22
	sub	w1, w1, w0
	ubfiz	x0, x0, 9, 23
	cmp	w1, w23
	csel	w19, w1, w23, ls
	ldr	x1, [x3,8]
	lsl	w20, w19, 9
	add	x0, x1, x0
	mov	w2, w20
	mov	x1, x24
	bl	ftl_memcpy
	cbnz	w26, .L3239
	ldr	w0, [x21,-108]
	cmp	w0, 2
	bgt	.L3239
.L3273:
	mov	w0, 0
	b	.L3234
.L3239:
	ldr	w0, [x29,204]
	add	x24, x24, x20
	sub	w23, w23, w19
	add	w22, w22, w19
	add	w0, w0, 1
	mov	w20, w26
	str	w0, [x29,204]
.L3238:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	wzr, [x0,-108]
.L3237:
	ldr	w0, [x29,204]
	ldr	w1, [x29,188]
	bl	FtlCacheMetchLpa
	cbz	w0, .L3240
	bl	FtlCacheWriteBack
.L3240:
	ldr	x0, [x29,144]
	ldr	x1, [x29,168]
	add	x0, x0, :lo12:.LANCHOR5
	add	x3, x1, :lo12:.LANCHOR2
	add	x21, x3, 3616
	mov	x28, x3
	str	x21, [x0,-128]
	ldr	w0, [x29,204]
	str	w0, [x29,180]
	adrp	x0, .LC158
	add	x0, x0, :lo12:.LC158
	str	x21, [x29,160]
	str	x0, [x29,96]
.L3241:
	cbz	w20, .L3305
	ldrh	w0, [x21,4]
	cbnz	w0, .L3242
	ldr	x1, [x29,160]
	cmp	x21, x1
	bne	.L3243
	add	x0, x28, 3664
	ldrh	w19, [x0,4]
	cbnz	w19, .L3244
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	str	w19, [x0,3296]
.L3244:
	ldr	x0, [x29,160]
	add	x21, x28, 3664
	bl	allocate_new_data_superblock
	ldr	x0, [x29,152]
	add	x0, x0, :lo12:.LANCHOR1
	ldr	w0, [x0,3296]
	cbnz	w0, .L3245
.L3246:
	ldr	x21, [x29,160]
	b	.L3245
.L3243:
	ldr	x1, [x29,152]
	add	x1, x1, :lo12:.LANCHOR1
	str	w0, [x1,3296]
	ldr	x0, [x29,160]
	ldrh	w0, [x0,4]
	cbnz	w0, .L3246
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3245:
	ldrh	w0, [x21,4]
	cbnz	w0, .L3247
	mov	x0, x21
	bl	allocate_new_data_superblock
.L3247:
	ldr	x0, [x29,144]
	add	x0, x0, :lo12:.LANCHOR5
	str	x21, [x0,-128]
.L3242:
	ldr	w0, [x28,972]
	ldr	w2, [x28,976]
	ldrh	w1, [x21,4]
	sub	w0, w0, w2
	str	xzr, [x29,192]
	cmp	w1, w0
	csel	w0, w1, w0, ls
	cmp	w0, w20
	csel	w0, w0, w20, ls
	str	w0, [x29,176]
	ldr	w0, [x29,200]
	and	w0, w0, 1
	str	w0, [x29,112]
	add	w0, w23, w22
	str	w0, [x29,116]
.L3248:
	ldr	w0, [x29,192]
	ldr	w2, [x29,176]
	ldr	w1, [x29,180]
	cmp	w0, w2
	add	w25, w1, w0
	bcs	.L3306
	ldrh	w1, [x21,4]
	cbz	w1, .L3280
	ldr	w1, [x29,188]
	cmp	w25, w1
	cset	w27, eq
	cbz	w0, .L3250
	ldr	w1, [x29,112]
	tst	w27, w1
	beq	.L3250
	ldrh	w1, [x28,830]
	ldr	w2, [x29,116]
	msub	w2, w25, w1, w2
	cmp	w2, w1
	bne	.L3280
.L3250:
	add	x1, x29, 212
	mov	w2, 0
	mov	w0, w25
	mov	w19, 56
	bl	log2phys
	mov	x0, x21
	bl	get_new_active_ppa
	ldr	w5, [x28,976]
	ldr	x2, [x28,1040]
	umull	x1, w5, w19
	add	x2, x2, x1
	str	w0, [x2,4]
	ldrh	w2, [x28,836]
	ldr	x0, [x28,1040]
	add	x1, x0, x1
	mul	w0, w2, w5
	and	x0, x0, 4294967292
	str	x0, [x29,128]
	ldr	x0, [x28,1144]
	ldr	x3, [x29,128]
	str	x0, [x29,120]
	add	x26, x0, x3
	ldrh	w0, [x28,834]
	str	x26, [x1,16]
	str	w25, [x1,24]
	mul	w0, w0, w5
	ldr	x5, [x28,1088]
	and	x0, x0, 4294967292
	add	x0, x5, x0
	str	x0, [x1,8]
	mov	x0, x26
	mov	w1, 0
	bl	ftl_memset
	ldr	w0, [x29,204]
	cmp	w25, w0
	cset	w0, eq
	str	w0, [x29,184]
	cbnz	w0, .L3284
	cbz	w27, .L3251
	ldrh	w19, [x28,830]
	ldr	w0, [x29,116]
	ldr	w27, [x29,184]
	msub	w19, w25, w19, w0
	uxth	w19, w19
	b	.L3254
.L3284:
	ldrh	w19, [x28,830]
	udiv	w27, w22, w19
	msub	w27, w27, w19, w22
	sub	w19, w19, w27
	cmp	w19, w23
	csel	w19, w19, w23, ls
.L3254:
	ldrh	w0, [x28,830]
	cmp	w19, w0
	bne	.L3255
	ldr	w0, [x29,184]
	mov	x1, x24
	cbnz	w0, .L3256
	mul	w1, w19, w25
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
.L3256:
	ldr	w2, [x29,200]
	mov	w0, 56
	cbz	w2, .L3257
	ldr	w2, [x28,976]
	umull	x0, w2, w0
	ldr	x2, [x28,1040]
	add	x0, x2, x0
	b	.L3302
.L3257:
	ldr	w2, [x28,976]
	umull	x0, w2, w0
	ldr	x2, [x28,1040]
	add	x0, x2, x0
	ldr	x0, [x0,8]
	b	.L3303
.L3255:
	ldr	w0, [x29,212]
	cmn	w0, #1
	beq	.L3259
	str	w0, [x29,220]
	mov	w1, 56
	ldr	w0, [x28,976]
	mov	w2, 0
	str	w25, [x29,240]
	umull	x0, w0, w1
	ldr	x1, [x28,1040]
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
	bne	.L3260
	ldr	x0, [x29,136]
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,3544]
	add	w1, w1, 1
	str	w1, [x0,3544]
	b	.L3262
.L3260:
	ldr	w0, [x26,8]
	cmp	w0, w25
	beq	.L3262
	ldr	x0, [x29,136]
	mov	w2, w25
	add	x0, x0, :lo12:.LANCHOR4
	ldr	w1, [x0,3544]
	add	w1, w1, 1
	str	w1, [x0,3544]
	ldr	x0, [x29,96]
	ldr	w1, [x26,8]
	bl	printk
	b	.L3262
.L3259:
	ldr	w0, [x28,976]
	mov	w1, 56
	ldrh	w2, [x28,834]
	umull	x0, w0, w1
	ldr	x1, [x28,1040]
	add	x0, x1, x0
	mov	w1, 0
	ldr	x0, [x0,8]
	bl	ftl_memset
.L3262:
	ldr	w1, [x29,184]
	mov	w0, 56
	lsl	w2, w19, 9
	cbz	w1, .L3263
	ldr	w1, [x28,976]
	ubfiz	x27, x27, 9, 23
	umull	x0, w1, w0
	ldr	x1, [x28,1040]
	add	x0, x1, x0
	mov	x1, x24
	ldr	x0, [x0,8]
	add	x0, x0, x27
	b	.L3304
.L3263:
	ldr	w1, [x28,976]
	umull	x0, w1, w0
	ldr	x1, [x28,1040]
	add	x0, x1, x0
	ldrh	w1, [x28,830]
	mul	w1, w25, w1
	ldr	x0, [x0,8]
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	add	x1, x24, x1
	b	.L3304
.L3251:
	ldr	w0, [x29,200]
	ldrh	w1, [x28,830]
	ldr	w2, [x28,976]
	cbz	w0, .L3264
	mul	w1, w25, w1
	umull	x0, w2, w19
	sub	w1, w1, w22
	ldr	x2, [x28,1040]
	ubfiz	x1, x1, 9, 23
	add	x0, x2, x0
	add	x1, x24, x1
.L3302:
	str	x1, [x0,8]
	b	.L3258
.L3264:
	umull	x0, w2, w19
	ldr	x2, [x28,1040]
	mul	w1, w25, w1
	add	x0, x2, x0
	sub	w1, w1, w22
	ubfiz	x1, x1, 9, 23
	ldr	x0, [x0,8]
	add	x1, x24, x1
.L3303:
	ldrh	w2, [x28,834]
.L3304:
	bl	ftl_memcpy
.L3258:
	ldr	x1, [x29,120]
	mov	w0, -3947
	ldr	x2, [x29,128]
	strh	w0, [x1,x2]
	str	w25, [x26,8]
	ldr	w0, [x28,880]
	str	w0, [x26,4]
	add	w0, w0, 1
	cmn	w0, #1
	csel	w0, w0, wzr, ne
	str	w0, [x28,880]
	ldr	w0, [x29,212]
	str	w0, [x26,12]
	ldrh	w0, [x21]
	strh	w0, [x26,2]
	ldr	w0, [x28,976]
	add	w0, w0, 1
	str	w0, [x28,976]
	ldr	x0, [x29,192]
	add	x0, x0, 1
	str	x0, [x29,192]
	b	.L3248
.L3306:
	str	w25, [x29,180]
	mov	x0, x2
	b	.L3249
.L3280:
	str	w25, [x29,180]
.L3249:
	sub	w20, w20, w0
	ldr	w0, [x29,200]
	cbnz	w0, .L3268
	ldr	w1, [x28,976]
	ldr	w0, [x28,972]
	cmp	w1, w0
	bcs	.L3268
	ldrh	w0, [x21,4]
	cbz	w0, .L3268
.L3270:
	str	wzr, [x29,200]
	b	.L3241
.L3268:
	bl	FtlCacheWriteBack
	str	wzr, [x28,976]
	cmp	w20, 1
	bhi	.L3241
	b	.L3270
.L3305:
	ldr	w1, [x29,188]
	mov	w0, w20
	ldr	w2, [x29,204]
	sub	w1, w1, w2
	bl	ftl_do_gc
	ldr	x0, [x29,168]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w0, [x0,3608]
	cmp	w0, 5
	bls	.L3272
	cmp	w0, 31
	bhi	.L3273
	ldr	x0, [x29,104]
	add	x0, x0, :lo12:.LANCHOR0
	ldrb	w0, [x0,88]
	cbnz	w0, .L3273
.L3272:
	ldr	x0, [x29,136]
	mov	w21, 65535
	mov	w22, 128
	add	x23, x0, :lo12:.LANCHOR4
.L3295:
	ldrh	w0, [x23,3424]
	cmp	w0, w21
	bne	.L3274
	ldr	x0, [x29,168]
	add	x0, x0, :lo12:.LANCHOR2
	ldrh	w1, [x0,952]
	cmp	w1, w21
	bne	.L3274
	ldrh	w0, [x0,954]
	cmp	w0, w21
	bne	.L3274
	and	w0, w20, 7
	bl	List_get_gc_head_node
	bl	FtlGcRefreshBlock
.L3274:
	ldr	x0, [x29,168]
	add	x19, x0, :lo12:.LANCHOR2
	mov	w0, 1
	mov	w1, w0
	strh	w22, [x19,962]
	strh	w22, [x19,960]
	bl	ftl_do_gc
	mov	w0, 0
	mov	w1, 1
	bl	ftl_do_gc
	ldr	w0, [x19,1408]
	cbnz	w0, .L3273
	ldrh	w0, [x19,3608]
	cmp	w0, 2
	bhi	.L3273
	add	w20, w20, 1
	cmp	w20, 256
	bne	.L3295
	b	.L3273
.L3234:
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
	adrp	x0, .LANCHOR0+4044
	stp	x29, x30, [sp, -128]!
	add	x29, sp, 0
	ldrb	w0, [x0,#:lo12:.LANCHOR0+4044]
	stp	x19, x20, [sp,16]
	stp	x21, x22, [sp,32]
	stp	x23, x24, [sp,48]
	stp	x25, x26, [sp,64]
	cbz	w0, .L3309
	adrp	x21, .LANCHOR4
	adrp	x19, .LANCHOR2
	add	x0, x21, :lo12:.LANCHOR4
	add	x20, x19, :lo12:.LANCHOR2
	adrp	x24, .LC159
	add	x26, x20, 3616
	add	x25, x20, 3664
	ldrh	w23, [x0,4074]
	add	x0, x24, :lo12:.LC159
	ldr	x2, [x20,1192]
	ubfiz	x22, x23, 1, 16
	mov	w1, w23
	ldrh	w2, [x2,x22]
	bl	printk
	ldrh	w0, [x20,3616]
	bl	FtlGcRefreshOpenBlock
	ldrh	w0, [x20,3664]
	mov	w20, 4097
	bl	FtlGcRefreshOpenBlock
	mov	x0, x26
	bl	allocate_new_data_superblock
	mov	x0, x25
	bl	allocate_new_data_superblock
.L3311:
	subs	w20, w20, #1
	beq	.L3315
	mov	w0, 1
	mov	w1, w0
	bl	ftl_do_gc
	add	x0, x19, :lo12:.LANCHOR2
	ldr	x0, [x0,1192]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3311
.L3315:
	add	x20, x19, :lo12:.LANCHOR2
	add	x0, x24, :lo12:.LC159
	mov	w1, w23
	ldr	x2, [x20,1192]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x20,1192]
	ldrh	w0, [x0,x22]
	cbnz	w0, .L3313
	add	x0, x29, 80
	strh	w23, [x29,80]
	bl	make_superblock
	mov	x0, 0
	ldrh	w5, [x20,756]
	mov	w6, 65535
	mov	w20, w0
	mov	w7, 56
.L3316:
	cmp	w5, w0, uxth
	bls	.L3323
	add	x1, x29, 80
	add	x1, x1, x0, lsl 1
	ldrh	w2, [x1,16]
	cmp	w2, w6
	beq	.L3317
	add	x4, x19, :lo12:.LANCHOR2
	lsl	w2, w2, 10
	umull	x1, w20, w7
	add	w20, w20, 1
	ldr	x3, [x4,1024]
	uxth	w20, w20
	add	x3, x3, x1
	str	w2, [x3,4]
	ldr	x2, [x4,1024]
	add	x1, x2, x1
	str	xzr, [x1,8]
	str	xzr, [x1,16]
.L3317:
	add	x0, x0, 1
	b	.L3316
.L3323:
	add	x19, x19, :lo12:.LANCHOR2
	adrp	x0, .LC160
	mov	w1, w23
	add	x0, x0, :lo12:.LC160
	ldr	x2, [x19,1192]
	ldrh	w2, [x2,x22]
	bl	printk
	ldr	x0, [x19,1024]
	mov	w1, 0
	mov	w2, w20
	bl	FlashEraseBlocks
	ldr	x0, [x19,1024]
	mov	w1, 1
	mov	w2, w20
	bl	FlashEraseBlocks
.L3313:
	add	x21, x21, :lo12:.LANCHOR4
	mov	w0, -1
	strh	w0, [x21,4074]
.L3309:
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
	.type	__func__.19591, %object
	.size	__func__.19591, 11
__func__.19591:
	.string	"FtlMemInit"
	.zero	5
	.type	__func__.20334, %object
	.size	__func__.20334, 12
__func__.20334:
	.string	"FtlCheckVpc"
	.zero	4
	.type	__func__.20366, %object
	.size	__func__.20366, 17
__func__.20366:
	.string	"FtlDumpBlockInfo"
	.zero	7
	.type	__func__.20385, %object
	.size	__func__.20385, 16
__func__.20385:
	.string	"FtlScanAllBlock"
	.type	__func__.20651, %object
	.size	__func__.20651, 17
__func__.20651:
	.string	"ftl_scan_all_ppa"
	.zero	7
	.type	__func__.20633, %object
	.size	__func__.20633, 21
__func__.20633:
	.string	"FtlVpcCheckAndModify"
	.zero	3
	.type	__func__.19664, %object
	.size	__func__.19664, 8
__func__.19664:
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
	.string	"superBlkID = %x vpc=%x\n"
.LC61:
	.string	"flashmode = %x pagenum = %x %x\n"
.LC62:
	.string	"blk = %x vpc=%x mode = %x\n"
.LC63:
	.string	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC64:
	.string	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\n"
.LC65:
	.string	"slc mode"
.LC66:
	.string	"ftl_scan_all_ppa blk %x page %x\n"
.LC67:
	.string	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\n"
.LC68:
	.string	"%s finished\n"
.LC69:
	.string	"FLASH INFO:\n"
.LC70:
	.string	"FLASH ID: %x\n"
.LC71:
	.string	"Device Capacity: %d MB\n"
.LC72:
	.string	"FMWAIT: %x %x %x %x\n"
.LC73:
	.string	"FTL INFO:\n"
.LC74:
	.string	"g_MaxLpn = 0x%x\n"
.LC75:
	.string	"g_VaildLpn = 0x%x\n"
.LC76:
	.string	"read_page_count = 0x%x\n"
.LC77:
	.string	"discard_page_count = 0x%x\n"
.LC78:
	.string	"write_page_count = 0x%x\n"
.LC79:
	.string	"cache_write_count = 0x%x\n"
.LC80:
	.string	"l2p_write_count = 0x%x\n"
.LC81:
	.string	"gc_page_count = 0x%x\n"
.LC82:
	.string	"totle_write = %d MB\n"
.LC83:
	.string	"totle_read = %d MB\n"
.LC84:
	.string	"GSV = 0x%x\n"
.LC85:
	.string	"GDV = 0x%x\n"
.LC86:
	.string	"bad blk num = %d %d\n"
.LC87:
	.string	"free_superblocks = 0x%x\n"
.LC88:
	.string	"mlc_EC = 0x%x\n"
.LC89:
	.string	"slc_EC = 0x%x\n"
.LC90:
	.string	"avg_EC = 0x%x\n"
.LC91:
	.string	"sys_EC = 0x%x\n"
.LC92:
	.string	"max_EC = 0x%x\n"
.LC93:
	.string	"min_EC = 0x%x\n"
.LC94:
	.string	"PLT = 0x%x\n"
.LC95:
	.string	"POT = 0x%x\n"
.LC96:
	.string	"MaxSector = 0x%x\n"
.LC97:
	.string	"init_sys_blks_pp = 0x%x\n"
.LC98:
	.string	"sys_blks_pp = 0x%x\n"
.LC99:
	.string	"free sysblock = 0x%x\n"
.LC100:
	.string	"data_blks_pp = 0x%x\n"
.LC101:
	.string	"data_op_blks_pp = 0x%x\n"
.LC102:
	.string	"max_data_blks = 0x%x\n"
.LC103:
	.string	"Sys.id = 0x%x\n"
.LC104:
	.string	"Bbt.id = 0x%x\n"
.LC105:
	.string	"ACT.page = 0x%x\n"
.LC106:
	.string	"ACT.plane = 0x%x\n"
.LC107:
	.string	"ACT.id = 0x%x\n"
.LC108:
	.string	"ACT.mode = 0x%x\n"
.LC109:
	.string	"ACT.a_pages = 0x%x\n"
.LC110:
	.string	"ACT VPC = 0x%x\n"
.LC111:
	.string	"BUF.page = 0x%x\n"
.LC112:
	.string	"BUF.plane = 0x%x\n"
.LC113:
	.string	"BUF.id = 0x%x\n"
.LC114:
	.string	"BUF.mode = 0x%x\n"
.LC115:
	.string	"BUF.a_pages = 0x%x\n"
.LC116:
	.string	"BUF VPC = 0x%x\n"
.LC117:
	.string	"TMP.page = 0x%x\n"
.LC118:
	.string	"TMP.plane = 0x%x\n"
.LC119:
	.string	"TMP.id = 0x%x\n"
.LC120:
	.string	"TMP.mode = 0x%x\n"
.LC121:
	.string	"TMP.a_pages = 0x%x\n"
.LC122:
	.string	"GC.page = 0x%x\n"
.LC123:
	.string	"GC.plane = 0x%x\n"
.LC124:
	.string	"GC.id = 0x%x\n"
.LC125:
	.string	"GC.mode = 0x%x\n"
.LC126:
	.string	"GC.a_pages = 0x%x\n"
.LC127:
	.string	"WR_CHK = 0x%x %x %x %x\n"
.LC128:
	.string	"Read Err = 0x%x\n"
.LC129:
	.string	"Prog Err = 0x%x\n"
.LC130:
	.string	"gc_free_blk_th= 0x%x\n"
.LC131:
	.string	"gc_merge_free_blk_th= 0x%x\n"
.LC132:
	.string	"gc_skip_write_count= 0x%x\n"
.LC133:
	.string	"gc_blk_index= 0x%x\n"
.LC134:
	.string	"free min EC= 0x%x\n"
.LC135:
	.string	"free max EC= 0x%x\n"
.LC136:
	.string	"GC__SB VPC = 0x%x\n"
.LC137:
	.string	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
.LC138:
	.string	"free %d. [0x%x] 0x%x  0x%x\n"
.LC139:
	.string	"FTL version: 5.0.56 20190508"
.LC140:
	.string	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
.LC141:
	.string	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
.LC142:
	.string	"FtlGcScanTempBlkError ID %x %x!!!!!!!\n"
.LC143:
	.string	"FtlGcRefreshBlock  0x%x\n"
.LC144:
	.string	"FtlGcMarkBadPhyBlk %d 0x%x\n"
.LC145:
	.string	"FtlGcFreeBadSuperBlk 0x%x\n"
.LC146:
	.string	"decrement_vpc_count %x = %d\n"
.LC147:
	.string	"decrement_vpc_count %x = %d in free list\n"
.LC148:
	.string	"RSB refresh addr %x\n"
.LC149:
	.string	"spuer block %x vpn is 0\n "
.LC150:
	.string	"g_recovery_ppa %x ver %x\n "
.LC151:
	.string	"FtlCheckVpc %x = %x  %x\n"
.LC152:
	.string	"%d GC datablk  = %x vpc %x %x\n"
.LC153:
	.string	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
.LC154:
	.string	"Ftlwrite decrement_vpc_count %x = %d\n"
.LC155:
	.string	"GC des block %x done\n"
.LC156:
	.string	"...%s: no bad block mapping table, format device\n"
.LC157:
	.string	"...%s FtlSysBlkInit error ,format device!\n"
.LC158:
	.string	"FtlWrite: lpa error:%x %x\n"
.LC159:
	.string	"fix power lost blk = %x vpc=%x\n"
.LC160:
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
	.size	NandFlashParaTbl, 2592
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
